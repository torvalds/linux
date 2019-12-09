/* AFS superblock handling
 *
 * Copyright (c) 2002, 2007, 2018 Red Hat, Inc. All rights reserved.
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
#include <linux/fs_parser.h>
#include <linux/statfs.h>
#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/magic.h>
#include <net/net_namespace.h>
#include "internal.h"

static void afs_i_init_once(void *foo);
static void afs_kill_super(struct super_block *sb);
static struct inode *afs_alloc_inode(struct super_block *sb);
static void afs_destroy_inode(struct inode *inode);
static void afs_free_inode(struct inode *inode);
static int afs_statfs(struct dentry *dentry, struct kstatfs *buf);
static int afs_show_devname(struct seq_file *m, struct dentry *root);
static int afs_show_options(struct seq_file *m, struct dentry *root);
static int afs_init_fs_context(struct fs_context *fc);
static const struct fs_parameter_description afs_fs_parameters;

struct file_system_type afs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "afs",
	.init_fs_context	= afs_init_fs_context,
	.parameters		= &afs_fs_parameters,
	.kill_sb		= afs_kill_super,
	.fs_flags		= FS_RENAME_DOES_D_MOVE,
};
MODULE_ALIAS_FS("afs");

int afs_net_id;

static const struct super_operations afs_super_ops = {
	.statfs		= afs_statfs,
	.alloc_inode	= afs_alloc_inode,
	.drop_inode	= afs_drop_inode,
	.destroy_inode	= afs_destroy_inode,
	.free_inode	= afs_free_inode,
	.evict_inode	= afs_evict_inode,
	.show_devname	= afs_show_devname,
	.show_options	= afs_show_options,
};

static struct kmem_cache *afs_inode_cachep;
static atomic_t afs_count_active_inodes;

enum afs_param {
	Opt_autocell,
	Opt_dyn,
	Opt_flock,
	Opt_source,
};

static const struct fs_parameter_spec afs_param_specs[] = {
	fsparam_flag  ("autocell",	Opt_autocell),
	fsparam_flag  ("dyn",		Opt_dyn),
	fsparam_enum  ("flock",		Opt_flock),
	fsparam_string("source",	Opt_source),
	{}
};

static const struct fs_parameter_enum afs_param_enums[] = {
	{ Opt_flock,	"local",	afs_flock_mode_local },
	{ Opt_flock,	"openafs",	afs_flock_mode_openafs },
	{ Opt_flock,	"strict",	afs_flock_mode_strict },
	{ Opt_flock,	"write",	afs_flock_mode_write },
	{}
};

static const struct fs_parameter_description afs_fs_parameters = {
	.name		= "kAFS",
	.specs		= afs_param_specs,
	.enums		= afs_param_enums,
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
void afs_fs_exit(void)
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
	const char *p = NULL;

	if (as->dyn_root)
		seq_puts(m, ",dyn");
	if (test_bit(AFS_VNODE_AUTOCELL, &AFS_FS_I(d_inode(root))->flags))
		seq_puts(m, ",autocell");
	switch (as->flock_mode) {
	case afs_flock_mode_unset:	break;
	case afs_flock_mode_local:	p = "local";	break;
	case afs_flock_mode_openafs:	p = "openafs";	break;
	case afs_flock_mode_strict:	p = "strict";	break;
	case afs_flock_mode_write:	p = "write";	break;
	}
	if (p)
		seq_printf(m, ",flock=%s", p);

	return 0;
}

/*
 * Parse the source name to get cell name, volume name, volume type and R/W
 * selector.
 *
 * This can be one of the following:
 *	"%[cell:]volume[.]"		R/W volume
 *	"#[cell:]volume[.]"		R/O or R/W volume (R/O parent),
 *					 or R/W (R/W parent) volume
 *	"%[cell:]volume.readonly"	R/O volume
 *	"#[cell:]volume.readonly"	R/O volume
 *	"%[cell:]volume.backup"		Backup volume
 *	"#[cell:]volume.backup"		Backup volume
 */
static int afs_parse_source(struct fs_context *fc, struct fs_parameter *param)
{
	struct afs_fs_context *ctx = fc->fs_private;
	struct afs_cell *cell;
	const char *cellname, *suffix, *name = param->string;
	int cellnamesz;

	_enter(",%s", name);

	if (!name) {
		printk(KERN_ERR "kAFS: no volume name specified\n");
		return -EINVAL;
	}

	if ((name[0] != '%' && name[0] != '#') || !name[1]) {
		/* To use dynroot, we don't want to have to provide a source */
		if (strcmp(name, "none") == 0) {
			ctx->no_cell = true;
			return 0;
		}
		printk(KERN_ERR "kAFS: unparsable volume name\n");
		return -EINVAL;
	}

	/* determine the type of volume we're looking for */
	if (name[0] == '%') {
		ctx->type = AFSVL_RWVOL;
		ctx->force = true;
	}
	name++;

	/* split the cell name out if there is one */
	ctx->volname = strchr(name, ':');
	if (ctx->volname) {
		cellname = name;
		cellnamesz = ctx->volname - name;
		ctx->volname++;
	} else {
		ctx->volname = name;
		cellname = NULL;
		cellnamesz = 0;
	}

	/* the volume type is further affected by a possible suffix */
	suffix = strrchr(ctx->volname, '.');
	if (suffix) {
		if (strcmp(suffix, ".readonly") == 0) {
			ctx->type = AFSVL_ROVOL;
			ctx->force = true;
		} else if (strcmp(suffix, ".backup") == 0) {
			ctx->type = AFSVL_BACKVOL;
			ctx->force = true;
		} else if (suffix[1] == 0) {
		} else {
			suffix = NULL;
		}
	}

	ctx->volnamesz = suffix ?
		suffix - ctx->volname : strlen(ctx->volname);

	_debug("cell %*.*s [%p]",
	       cellnamesz, cellnamesz, cellname ?: "", ctx->cell);

	/* lookup the cell record */
	if (cellname) {
		cell = afs_lookup_cell(ctx->net, cellname, cellnamesz,
				       NULL, false);
		if (IS_ERR(cell)) {
			pr_err("kAFS: unable to lookup cell '%*.*s'\n",
			       cellnamesz, cellnamesz, cellname ?: "");
			return PTR_ERR(cell);
		}
		afs_put_cell(ctx->net, ctx->cell);
		ctx->cell = cell;
	}

	_debug("CELL:%s [%p] VOLUME:%*.*s SUFFIX:%s TYPE:%d%s",
	       ctx->cell->name, ctx->cell,
	       ctx->volnamesz, ctx->volnamesz, ctx->volname,
	       suffix ?: "-", ctx->type, ctx->force ? " FORCE" : "");

	fc->source = param->string;
	param->string = NULL;
	return 0;
}

/*
 * Parse a single mount parameter.
 */
static int afs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct afs_fs_context *ctx = fc->fs_private;
	int opt;

	opt = fs_parse(fc, &afs_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_source:
		return afs_parse_source(fc, param);

	case Opt_autocell:
		ctx->autocell = true;
		break;

	case Opt_dyn:
		ctx->dyn_root = true;
		break;

	case Opt_flock:
		ctx->flock_mode = result.uint_32;
		break;

	default:
		return -EINVAL;
	}

	_leave(" = 0");
	return 0;
}

/*
 * Validate the options, get the cell key and look up the volume.
 */
static int afs_validate_fc(struct fs_context *fc)
{
	struct afs_fs_context *ctx = fc->fs_private;
	struct afs_volume *volume;
	struct key *key;

	if (!ctx->dyn_root) {
		if (ctx->no_cell) {
			pr_warn("kAFS: Can only specify source 'none' with -o dyn\n");
			return -EINVAL;
		}

		if (!ctx->cell) {
			pr_warn("kAFS: No cell specified\n");
			return -EDESTADDRREQ;
		}

		/* We try to do the mount securely. */
		key = afs_request_key(ctx->cell);
		if (IS_ERR(key))
			return PTR_ERR(key);

		ctx->key = key;

		if (ctx->volume) {
			afs_put_volume(ctx->cell, ctx->volume);
			ctx->volume = NULL;
		}

		volume = afs_create_volume(ctx);
		if (IS_ERR(volume))
			return PTR_ERR(volume);

		ctx->volume = volume;
	}

	return 0;
}

/*
 * check a superblock to see if it's the one we're looking for
 */
static int afs_test_super(struct super_block *sb, struct fs_context *fc)
{
	struct afs_fs_context *ctx = fc->fs_private;
	struct afs_super_info *as = AFS_FS_S(sb);

	return (as->net_ns == fc->net_ns &&
		as->volume &&
		as->volume->vid == ctx->volume->vid &&
		!as->dyn_root);
}

static int afs_dynroot_test_super(struct super_block *sb, struct fs_context *fc)
{
	struct afs_super_info *as = AFS_FS_S(sb);

	return (as->net_ns == fc->net_ns &&
		as->dyn_root);
}

static int afs_set_super(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super(sb, NULL);
}

/*
 * fill in the superblock
 */
static int afs_fill_super(struct super_block *sb, struct afs_fs_context *ctx)
{
	struct afs_super_info *as = AFS_FS_S(sb);
	struct afs_iget_data iget_data;
	struct inode *inode = NULL;
	int ret;

	_enter("");

	/* fill in the superblock */
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_magic		= AFS_FS_MAGIC;
	sb->s_op		= &afs_super_ops;
	if (!as->dyn_root)
		sb->s_xattr	= afs_xattr_handlers;
	ret = super_setup_bdi(sb);
	if (ret)
		return ret;
	sb->s_bdi->ra_pages	= VM_READAHEAD_PAGES;

	/* allocate the root inode and dentry */
	if (as->dyn_root) {
		inode = afs_iget_pseudo_dir(sb, true);
	} else {
		sprintf(sb->s_id, "%llu", as->volume->vid);
		afs_activate_volume(as->volume);
		iget_data.fid.vid	= as->volume->vid;
		iget_data.fid.vnode	= 1;
		iget_data.fid.vnode_hi	= 0;
		iget_data.fid.unique	= 1;
		iget_data.cb_v_break	= as->volume->cb_v_break;
		iget_data.cb_s_break	= 0;
		inode = afs_iget(sb, ctx->key, &iget_data, NULL, NULL, NULL);
	}

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (ctx->autocell || as->dyn_root)
		set_bit(AFS_VNODE_AUTOCELL, &AFS_FS_I(inode)->flags);

	ret = -ENOMEM;
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		goto error;

	if (as->dyn_root) {
		sb->s_d_op = &afs_dynroot_dentry_operations;
		ret = afs_dynroot_populate(sb);
		if (ret < 0)
			goto error;
	} else {
		sb->s_d_op = &afs_fs_dentry_operations;
	}

	_leave(" = 0");
	return 0;

error:
	_leave(" = %d", ret);
	return ret;
}

static struct afs_super_info *afs_alloc_sbi(struct fs_context *fc)
{
	struct afs_fs_context *ctx = fc->fs_private;
	struct afs_super_info *as;

	as = kzalloc(sizeof(struct afs_super_info), GFP_KERNEL);
	if (as) {
		as->net_ns = get_net(fc->net_ns);
		as->flock_mode = ctx->flock_mode;
		if (ctx->dyn_root) {
			as->dyn_root = true;
		} else {
			as->cell = afs_get_cell(ctx->cell);
			as->volume = __afs_get_volume(ctx->volume);
		}
	}
	return as;
}

static void afs_destroy_sbi(struct afs_super_info *as)
{
	if (as) {
		afs_put_volume(as->cell, as->volume);
		afs_put_cell(afs_net(as->net_ns), as->cell);
		put_net(as->net_ns);
		kfree(as);
	}
}

static void afs_kill_super(struct super_block *sb)
{
	struct afs_super_info *as = AFS_FS_S(sb);
	struct afs_net *net = afs_net(as->net_ns);

	if (as->dyn_root)
		afs_dynroot_depopulate(sb);

	/* Clear the callback interests (which will do ilookup5) before
	 * deactivating the superblock.
	 */
	if (as->volume)
		afs_clear_callback_interests(net, as->volume->servers);
	kill_anon_super(sb);
	if (as->volume)
		afs_deactivate_volume(as->volume);
	afs_destroy_sbi(as);
}

/*
 * Get an AFS superblock and root directory.
 */
static int afs_get_tree(struct fs_context *fc)
{
	struct afs_fs_context *ctx = fc->fs_private;
	struct super_block *sb;
	struct afs_super_info *as;
	int ret;

	ret = afs_validate_fc(fc);
	if (ret)
		goto error;

	_enter("");

	/* allocate a superblock info record */
	ret = -ENOMEM;
	as = afs_alloc_sbi(fc);
	if (!as)
		goto error;
	fc->s_fs_info = as;

	/* allocate a deviceless superblock */
	sb = sget_fc(fc,
		     as->dyn_root ? afs_dynroot_test_super : afs_test_super,
		     afs_set_super);
	if (IS_ERR(sb)) {
		ret = PTR_ERR(sb);
		goto error;
	}

	if (!sb->s_root) {
		/* initial superblock/root creation */
		_debug("create");
		ret = afs_fill_super(sb, ctx);
		if (ret < 0)
			goto error_sb;
		sb->s_flags |= SB_ACTIVE;
	} else {
		_debug("reuse");
		ASSERTCMP(sb->s_flags, &, SB_ACTIVE);
	}

	fc->root = dget(sb->s_root);
	trace_afs_get_tree(as->cell, as->volume);
	_leave(" = 0 [%p]", sb);
	return 0;

error_sb:
	deactivate_locked_super(sb);
error:
	_leave(" = %d", ret);
	return ret;
}

static void afs_free_fc(struct fs_context *fc)
{
	struct afs_fs_context *ctx = fc->fs_private;

	afs_destroy_sbi(fc->s_fs_info);
	afs_put_volume(ctx->cell, ctx->volume);
	afs_put_cell(ctx->net, ctx->cell);
	key_put(ctx->key);
	kfree(ctx);
}

static const struct fs_context_operations afs_context_ops = {
	.free		= afs_free_fc,
	.parse_param	= afs_parse_param,
	.get_tree	= afs_get_tree,
};

/*
 * Set up the filesystem mount context.
 */
static int afs_init_fs_context(struct fs_context *fc)
{
	struct afs_fs_context *ctx;
	struct afs_cell *cell;

	ctx = kzalloc(sizeof(struct afs_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->type = AFSVL_ROVOL;
	ctx->net = afs_net(fc->net_ns);

	/* Default to the workstation cell. */
	rcu_read_lock();
	cell = afs_lookup_cell_rcu(ctx->net, NULL, 0);
	rcu_read_unlock();
	if (IS_ERR(cell))
		cell = NULL;
	ctx->cell = cell;

	fc->fs_private = ctx;
	fc->ops = &afs_context_ops;
	return 0;
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
	RCU_INIT_POINTER(vnode->cb_interest, NULL);
#ifdef CONFIG_AFS_FSCACHE
	vnode->cache		= NULL;
#endif

	vnode->flags		= 1 << AFS_VNODE_UNSET;
	vnode->lock_state	= AFS_VNODE_LOCK_NONE;

	init_rwsem(&vnode->rmdir_lock);

	_leave(" = %p", &vnode->vfs_inode);
	return &vnode->vfs_inode;
}

static void afs_free_inode(struct inode *inode)
{
	kmem_cache_free(afs_inode_cachep, AFS_FS_I(inode));
}

/*
 * destroy an AFS inode struct
 */
static void afs_destroy_inode(struct inode *inode)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);

	_enter("%p{%llx:%llu}", inode, vnode->fid.vid, vnode->fid.vnode);

	_debug("DESTROY INODE %p", inode);

	ASSERTCMP(rcu_access_pointer(vnode->cb_interest), ==, NULL);

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
	if (afs_begin_vnode_operation(&fc, vnode, key, true)) {
		fc.flags |= AFS_FS_CURSOR_NO_VSLEEP;
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			afs_fs_get_volume_status(&fc, &vs);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
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
