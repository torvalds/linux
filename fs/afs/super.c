/* AFS superblock handling
 *
 * Copyright (c) 2002, 2007 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Howells <dhowells@redhat.com>
 *          David Woodhouse <dwmw2@infradead.org>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/parser.h>
#include <linux/statfs.h>
#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/magic.h>
#include <net/net_namespace.h>
#include "internal.h"

static void afs_i_init_once(void *foo);
static struct dentry *afs_mount(struct file_system_type *fs_type,
		      int flags, const char *dev_name, void *data);
static void afs_kill_super(struct super_block *sb);
static struct inode *afs_alloc_inode(struct super_block *sb);
static void afs_destroy_inode(struct inode *inode);
static int afs_statfs(struct dentry *dentry, struct kstatfs *buf);
static int afs_show_devname(struct seq_file *m, struct dentry *root);
static int afs_show_options(struct seq_file *m, struct dentry *root);

struct file_system_type afs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "afs",
	.mount		= afs_mount,
	.kill_sb	= afs_kill_super,
	.fs_flags	= 0,
};
MODULE_ALIAS_FS("afs");

static const struct super_operations afs_super_ops = {
	.statfs		= afs_statfs,
	.alloc_inode	= afs_alloc_inode,
	.drop_inode	= afs_drop_inode,
	.destroy_inode	= afs_destroy_inode,
	.evict_inode	= afs_evict_inode,
	.show_devname	= afs_show_devname,
	.show_options	= afs_show_options,
};

static struct kmem_cache *afs_inode_cachep;
static atomic_t afs_count_active_inodes;

enum {
	afs_no_opt,
	afs_opt_cell,
	afs_opt_dyn,
	afs_opt_rwpath,
	afs_opt_vol,
	afs_opt_autocell,
};

static const match_table_t afs_options_list = {
	{ afs_opt_cell,		"cell=%s"	},
	{ afs_opt_dyn,		"dyn"		},
	{ afs_opt_rwpath,	"rwpath"	},
	{ afs_opt_vol,		"vol=%s"	},
	{ afs_opt_autocell,	"autocell"	},
	{ afs_no_opt,		NULL		},
};

/*
 * initialise the filesystem
 */
int __init afs_fs_init(void)
{
	int ret;

	_enter("");

	/* create ourselves an inode cache */
	atomic_set(&afs_count_active_inodes, 0);

	ret = -ENOMEM;
	afs_inode_cachep = kmem_cache_create("afs_inode_cache",
					     sizeof(struct afs_vnode),
					     0,
					     SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT,
					     afs_i_init_once);
	if (!afs_inode_cachep) {
		printk(KERN_NOTICE "kAFS: Failed to allocate inode cache\n");
		return ret;
	}

	/* now export our filesystem to lesser mortals */
	ret = register_filesystem(&afs_fs_type);
	if (ret < 0) {
		kmem_cache_destroy(afs_inode_cachep);
		_leave(" = %d", ret);
		return ret;
	}

	_leave(" = 0");
	return 0;
}

/*
 * clean up the filesystem
 */
void __exit afs_fs_exit(void)
{
	_enter("");

	afs_mntpt_kill_timer();
	unregister_filesystem(&afs_fs_type);

	if (atomic_read(&afs_count_active_inodes) != 0) {
		printk("kAFS: %d active inode objects still present\n",
		       atomic_read(&afs_count_active_inodes));
		BUG();
	}

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(afs_inode_cachep);
	_leave("");
}

/*
 * Display the mount device name in /proc/mounts.
 */
static int afs_show_devname(struct seq_file *m, struct dentry *root)
{
	struct afs_super_info *as = AFS_FS_S(root->d_sb);
	struct afs_volume *volume = as->volume;
	struct afs_cell *cell = as->cell;
	const char *suf = "";
	char pref = '%';

	if (as->dyn_root) {
		seq_puts(m, "none");
		return 0;
	}

	switch (volume->type) {
	case AFSVL_RWVOL:
		break;
	case AFSVL_ROVOL:
		pref = '#';
		if (volume->type_force)
			suf = ".readonly";
		break;
	case AFSVL_BACKVOL:
		pref = '#';
		suf = ".backup";
		break;
	}

	seq_printf(m, "%c%s:%s%s", pref, cell->name, volume->name, suf);
	return 0;
}

/*
 * Display the mount options in /proc/mounts.
 */
static int afs_show_options(struct seq_file *m, struct dentry *root)
{
	struct afs_super_info *as = AFS_FS_S(root->d_sb);

	if (as->dyn_root)
		seq_puts(m, ",dyn");
	if (test_bit(AFS_VNODE_AUTOCELL, &AFS_FS_I(d_inode(root))->flags))
		seq_puts(m, ",autocell");
	return 0;
}

/*
 * parse the mount options
 * - this function has been shamelessly adapted from the ext3 fs which
 *   shamelessly adapted it from the msdos fs
 */
static int afs_parse_options(struct afs_mount_params *params,
			     char *options, const char **devname)
{
	struct afs_cell *cell;
	substring_t args[MAX_OPT_ARGS];
	char *p;
	int token;

	_enter("%s", options);

	options[PAGE_SIZE - 1] = 0;

	while ((p = strsep(&options, ","))) {
		if (!*p)
			continue;

		token = match_token(p, afs_options_list, args);
		switch (token) {
		case afs_opt_cell:
			rcu_read_lock();
			cell = afs_lookup_cell_rcu(params->net,
						   args[0].from,
						   args[0].to - args[0].from);
			rcu_read_unlock();
			if (IS_ERR(cell))
				return PTR_ERR(cell);
			afs_put_cell(params->net, params->cell);
			params->cell = cell;
			break;

		case afs_opt_rwpath:
			params->rwpath = true;
			break;

		case afs_opt_vol:
			*devname = args[0].from;
			break;

		case afs_opt_autocell:
			params->autocell = true;
			break;

		case afs_opt_dyn:
			params->dyn_root = true;
			break;

		default:
			printk(KERN_ERR "kAFS:"
			       " Unknown or invalid mount option: '%s'\n", p);
			return -EINVAL;
		}
	}

	_leave(" = 0");
	return 0;
}

/*
 * parse a device name to get cell name, volume name, volume type and R/W
 * selector
 * - this can be one of the following:
 *	"%[cell:]volume[.]"		R/W volume
 *	"#[cell:]volume[.]"		R/O or R/W volume (rwpath=0),
 *					 or R/W (rwpath=1) volume
 *	"%[cell:]volume.readonly"	R/O volume
 *	"#[cell:]volume.readonly"	R/O volume
 *	"%[cell:]volume.backup"		Backup volume
 *	"#[cell:]volume.backup"		Backup volume
 */
static int afs_parse_device_name(struct afs_mount_params *params,
				 const char *name)
{
	struct afs_cell *cell;
	const char *cellname, *suffix;
	int cellnamesz;

	_enter(",%s", name);

	if (!name) {
		printk(KERN_ERR "kAFS: no volume name specified\n");
		return -EINVAL;
	}

	if ((name[0] != '%' && name[0] != '#') || !name[1]) {
		printk(KERN_ERR "kAFS: unparsable volume name\n");
		return -EINVAL;
	}

	/* determine the type of volume we're looking for */
	params->type = AFSVL_ROVOL;
	params->force = false;
	if (params->rwpath || name[0] == '%') {
		params->type = AFSVL_RWVOL;
		params->force = true;
	}
	name++;

	/* split the cell name out if there is one */
	params->volname = strchr(name, ':');
	if (params->volname) {
		cellname = name;
		cellnamesz = params->volname - name;
		params->volname++;
	} else {
		params->volname = name;
		cellname = NULL;
		cellnamesz = 0;
	}

	/* the volume type is further affected by a possible suffix */
	suffix = strrchr(params->volname, '.');
	if (suffix) {
		if (strcmp(suffix, ".readonly") == 0) {
			params->type = AFSVL_ROVOL;
			params->force = true;
		} else if (strcmp(suffix, ".backup") == 0) {
			params->type = AFSVL_BACKVOL;
			params->force = true;
		} else if (suffix[1] == 0) {
		} else {
			suffix = NULL;
		}
	}

	params->volnamesz = suffix ?
		suffix - params->volname : strlen(params->volname);

	_debug("cell %*.*s [%p]",
	       cellnamesz, cellnamesz, cellname ?: "", params->cell);

	/* lookup the cell record */
	if (cellname || !params->cell) {
		cell = afs_lookup_cell(params->net, cellname, cellnamesz,
				       NULL, false);
		if (IS_ERR(cell)) {
			printk(KERN_ERR "kAFS: unable to lookup cell '%*.*s'\n",
			       cellnamesz, cellnamesz, cellname ?: "");
			return PTR_ERR(cell);
		}
		afs_put_cell(params->net, params->cell);
		params->cell = cell;
	}

	_debug("CELL:%s [%p] VOLUME:%*.*s SUFFIX:%s TYPE:%d%s",
	       params->cell->name, params->cell,
	       params->volnamesz, params->volnamesz, params->volname,
	       suffix ?: "-", params->type, params->force ? " FORCE" : "");

	return 0;
}

/*
 * check a superblock to see if it's the one we're looking for
 */
static int afs_test_super(struct super_block *sb, void *data)
{
	struct afs_super_info *as1 = data;
	struct afs_super_info *as = AFS_FS_S(sb);

	return (as->net == as1->net &&
		as->volume &&
		as->volume->vid == as1->volume->vid);
}

static int afs_dynroot_test_super(struct super_block *sb, void *data)
{
	return false;
}

static int afs_set_super(struct super_block *sb, void *data)
{
	struct afs_super_info *as = data;

	sb->s_fs_info = as;
	return set_anon_super(sb, NULL);
}

/*
 * fill in the superblock
 */
static int afs_fill_super(struct super_block *sb,
			  struct afs_mount_params *params)
{
	struct afs_super_info *as = AFS_FS_S(sb);
	struct afs_fid fid;
	struct inode *inode = NULL;
	int ret;

	_enter("");

	/* fill in the superblock */
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= AFS_FS_MAGIC;
	sb->s_op		= &afs_super_ops;
	if (!as->dyn_root)
		sb->s_xattr	= afs_xattr_handlers;
	ret = super_setup_bdi(sb);
	if (ret)
		return ret;
	sb->s_bdi->ra_pages	= VM_MAX_READAHEAD * 1024 / PAGE_SIZE;

	/* allocate the root inode and dentry */
	if (as->dyn_root) {
		inode = afs_iget_pseudo_dir(sb, true);
		sb->s_flags	|= SB_RDONLY;
	} else {
		sprintf(sb->s_id, "%u", as->volume->vid);
		afs_activate_volume(as->volume);
		fid.vid		= as->volume->vid;
		fid.vnode	= 1;
		fid.unique	= 1;
		inode = afs_iget(sb, params->key, &fid, NULL, NULL, NULL);
	}

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (params->autocell || params->dyn_root)
		set_bit(AFS_VNODE_AUTOCELL, &AFS_FS_I(inode)->flags);

	ret = -ENOMEM;
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		goto error;

	if (params->dyn_root)
		sb->s_d_op = &afs_dynroot_dentry_operations;
	else
		sb->s_d_op = &afs_fs_dentry_operations;

	_leave(" = 0");
	return 0;

error:
	_leave(" = %d", ret);
	return ret;
}

static struct afs_super_info *afs_alloc_sbi(struct afs_mount_params *params)
{
	struct afs_super_info *as;

	as = kzalloc(sizeof(struct afs_super_info), GFP_KERNEL);
	if (as) {
		as->net = afs_get_net(params->net);
		if (params->dyn_root)
			as->dyn_root = true;
		else
			as->cell = afs_get_cell(params->cell);
	}
	return as;
}

static void afs_destroy_sbi(struct afs_super_info *as)
{
	if (as) {
		afs_put_volume(as->cell, as->volume);
		afs_put_cell(as->net, as->cell);
		afs_put_net(as->net);
		kfree(as);
	}
}

/*
 * get an AFS superblock
 */
static struct dentry *afs_mount(struct file_system_type *fs_type,
				int flags, const char *dev_name, void *options)
{
	struct afs_mount_params params;
	struct super_block *sb;
	struct afs_volume *candidate;
	struct key *key;
	struct afs_super_info *as;
	int ret;

	_enter(",,%s,%p", dev_name, options);

	memset(&params, 0, sizeof(params));
	params.net = &__afs_net;

	ret = -EINVAL;
	if (current->nsproxy->net_ns != &init_net)
		goto error;

	/* parse the options and device name */
	if (options) {
		ret = afs_parse_options(&params, options, &dev_name);
		if (ret < 0)
			goto error;
	}

	if (!params.dyn_root) {
		ret = afs_parse_device_name(&params, dev_name);
		if (ret < 0)
			goto error;

		/* try and do the mount securely */
		key = afs_request_key(params.cell);
		if (IS_ERR(key)) {
			_leave(" = %ld [key]", PTR_ERR(key));
			ret = PTR_ERR(key);
			goto error;
		}
		params.key = key;
	}

	/* allocate a superblock info record */
	ret = -ENOMEM;
	as = afs_alloc_sbi(&params);
	if (!as)
		goto error_key;

	if (!params.dyn_root) {
		/* Assume we're going to need a volume record; at the very
		 * least we can use it to update the volume record if we have
		 * one already.  This checks that the volume exists within the
		 * cell.
		 */
		candidate = afs_create_volume(&params);
		if (IS_ERR(candidate)) {
			ret = PTR_ERR(candidate);
			goto error_as;
		}

		as->volume = candidate;
	}

	/* allocate a deviceless superblock */
	sb = sget(fs_type,
		  as->dyn_root ? afs_dynroot_test_super : afs_test_super,
		  afs_set_super, flags, as);
	if (IS_ERR(sb)) {
		ret = PTR_ERR(sb);
		goto error_as;
	}

	if (!sb->s_root) {
		/* initial superblock/root creation */
		_debug("create");
		ret = afs_fill_super(sb, &params);
		if (ret < 0)
			goto error_sb;
		as = NULL;
		sb->s_flags |= SB_ACTIVE;
	} else {
		_debug("reuse");
		ASSERTCMP(sb->s_flags, &, SB_ACTIVE);
		afs_destroy_sbi(as);
		as = NULL;
	}

	afs_put_cell(params.net, params.cell);
	key_put(params.key);
	_leave(" = 0 [%p]", sb);
	return dget(sb->s_root);

error_sb:
	deactivate_locked_super(sb);
	goto error_key;
error_as:
	afs_destroy_sbi(as);
error_key:
	key_put(params.key);
error:
	afs_put_cell(params.net, params.cell);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

static void afs_kill_super(struct super_block *sb)
{
	struct afs_super_info *as = AFS_FS_S(sb);

	/* Clear the callback interests (which will do ilookup5) before
	 * deactivating the superblock.
	 */
	if (as->volume)
		afs_clear_callback_interests(as->net, as->volume->servers);
	kill_anon_super(sb);
	if (as->volume)
		afs_deactivate_volume(as->volume);
	afs_destroy_sbi(as);
}

/*
 * Initialise an inode cache slab element prior to any use.  Note that
 * afs_alloc_inode() *must* reset anything that could incorrectly leak from one
 * inode to another.
 */
static void afs_i_init_once(void *_vnode)
{
	struct afs_vnode *vnode = _vnode;

	memset(vnode, 0, sizeof(*vnode));
	inode_init_once(&vnode->vfs_inode);
	mutex_init(&vnode->io_lock);
	init_rwsem(&vnode->validate_lock);
	spin_lock_init(&vnode->wb_lock);
	spin_lock_init(&vnode->lock);
	INIT_LIST_HEAD(&vnode->wb_keys);
	INIT_LIST_HEAD(&vnode->pending_locks);
	INIT_LIST_HEAD(&vnode->granted_locks);
	INIT_DELAYED_WORK(&vnode->lock_work, afs_lock_work);
	seqlock_init(&vnode->cb_lock);
}

/*
 * allocate an AFS inode struct from our slab cache
 */
static struct inode *afs_alloc_inode(struct super_block *sb)
{
	struct afs_vnode *vnode;

	vnode = kmem_cache_alloc(afs_inode_cachep, GFP_KERNEL);
	if (!vnode)
		return NULL;

	atomic_inc(&afs_count_active_inodes);

	/* Reset anything that shouldn't leak from one inode to the next. */
	memset(&vnode->fid, 0, sizeof(vnode->fid));
	memset(&vnode->status, 0, sizeof(vnode->status));

	vnode->volume		= NULL;
	vnode->lock_key		= NULL;
	vnode->permit_cache	= NULL;
	vnode->cb_interest	= NULL;
#ifdef CONFIG_AFS_FSCACHE
	vnode->cache		= NULL;
#endif

	vnode->flags		= 1 << AFS_VNODE_UNSET;
	vnode->cb_type		= 0;
	vnode->lock_state	= AFS_VNODE_LOCK_NONE;

	_leave(" = %p", &vnode->vfs_inode);
	return &vnode->vfs_inode;
}

static void afs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	kmem_cache_free(afs_inode_cachep, vnode);
}

/*
 * destroy an AFS inode struct
 */
static void afs_destroy_inode(struct inode *inode)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);

	_enter("%p{%x:%u}", inode, vnode->fid.vid, vnode->fid.vnode);

	_debug("DESTROY INODE %p", inode);

	ASSERTCMP(vnode->cb_interest, ==, NULL);

	call_rcu(&inode->i_rcu, afs_i_callback);
	atomic_dec(&afs_count_active_inodes);
}

/*
 * return information about an AFS volume
 */
static int afs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct afs_super_info *as = AFS_FS_S(dentry->d_sb);
	struct afs_fs_cursor fc;
	struct afs_volume_status vs;
	struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));
	struct key *key;
	int ret;

	buf->f_type	= dentry->d_sb->s_magic;
	buf->f_bsize	= AFS_BLOCK_SIZE;
	buf->f_namelen	= AFSNAMEMAX - 1;

	if (as->dyn_root) {
		buf->f_blocks	= 1;
		buf->f_bavail	= 0;
		buf->f_bfree	= 0;
		return 0;
	}

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key))
		return PTR_ERR(key);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		fc.flags |= AFS_FS_CURSOR_NO_VSLEEP;
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			afs_fs_get_volume_status(&fc, &vs);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	key_put(key);

	if (ret == 0) {
		if (vs.max_quota == 0)
			buf->f_blocks = vs.part_max_blocks;
		else
			buf->f_blocks = vs.max_quota;
		buf->f_bavail = buf->f_bfree = buf->f_blocks - vs.blocks_in_use;
	}

	return ret;
}
