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
 *          David Woodhouse <dwmw2@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/parser.h>
#include <linux/statfs.h>
#include <linux/sched.h>
#include "internal.h"

#define AFS_FS_MAGIC 0x6B414653 /* 'kAFS' */

static void afs_i_init_once(struct kmem_cache *cachep, void *foo);
static int afs_get_sb(struct file_system_type *fs_type,
		      int flags, const char *dev_name,
		      void *data, struct vfsmount *mnt);
static struct inode *afs_alloc_inode(struct super_block *sb);
static void afs_put_super(struct super_block *sb);
static void afs_destroy_inode(struct inode *inode);
static int afs_statfs(struct dentry *dentry, struct kstatfs *buf);

struct file_system_type afs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "afs",
	.get_sb		= afs_get_sb,
	.kill_sb	= kill_anon_super,
	.fs_flags	= 0,
};

static const struct super_operations afs_super_ops = {
	.statfs		= afs_statfs,
	.alloc_inode	= afs_alloc_inode,
	.write_inode	= afs_write_inode,
	.destroy_inode	= afs_destroy_inode,
	.clear_inode	= afs_clear_inode,
	.put_super	= afs_put_super,
	.show_options	= generic_show_options,
};

static struct kmem_cache *afs_inode_cachep;
static atomic_t afs_count_active_inodes;

enum {
	afs_no_opt,
	afs_opt_cell,
	afs_opt_rwpath,
	afs_opt_vol,
};

static match_table_t afs_options_list = {
	{ afs_opt_cell,		"cell=%s"	},
	{ afs_opt_rwpath,	"rwpath"	},
	{ afs_opt_vol,		"vol=%s"	},
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
					     SLAB_HWCACHE_ALIGN,
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

	kmem_cache_destroy(afs_inode_cachep);
	_leave("");
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
			cell = afs_cell_lookup(args[0].from,
					       args[0].to - args[0].from);
			if (IS_ERR(cell))
				return PTR_ERR(cell);
			afs_put_cell(params->cell);
			params->cell = cell;
			break;

		case afs_opt_rwpath:
			params->rwpath = 1;
			break;

		case afs_opt_vol:
			*devname = args[0].from;
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
		cell = afs_cell_lookup(cellname, cellnamesz);
		if (IS_ERR(cell)) {
			printk(KERN_ERR "kAFS: unable to lookup cell '%s'\n",
			       cellname ?: "");
			return PTR_ERR(cell);
		}
		afs_put_cell(params->cell);
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
	struct afs_mount_params *params = data;
	struct afs_super_info *as = sb->s_fs_info;

	return as->volume == params->volume;
}

/*
 * fill in the superblock
 */
static int afs_fill_super(struct super_block *sb, void *data)
{
	struct afs_mount_params *params = data;
	struct afs_super_info *as = NULL;
	struct afs_fid fid;
	struct dentry *root = NULL;
	struct inode *inode = NULL;
	int ret;

	_enter("");

	/* allocate a superblock info record */
	as = kzalloc(sizeof(struct afs_super_info), GFP_KERNEL);
	if (!as) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	afs_get_volume(params->volume);
	as->volume = params->volume;

	/* fill in the superblock */
	sb->s_blocksize		= PAGE_CACHE_SIZE;
	sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
	sb->s_magic		= AFS_FS_MAGIC;
	sb->s_op		= &afs_super_ops;
	sb->s_fs_info		= as;

	/* allocate the root inode and dentry */
	fid.vid		= as->volume->vid;
	fid.vnode	= 1;
	fid.unique	= 1;
	inode = afs_iget(sb, params->key, &fid, NULL, NULL);
	if (IS_ERR(inode))
		goto error_inode;

	ret = -ENOMEM;
	root = d_alloc_root(inode);
	if (!root)
		goto error;

	sb->s_root = root;

	_leave(" = 0");
	return 0;

error_inode:
	ret = PTR_ERR(inode);
	inode = NULL;
error:
	iput(inode);
	afs_put_volume(as->volume);
	kfree(as);

	sb->s_fs_info = NULL;

	_leave(" = %d", ret);
	return ret;
}

/*
 * get an AFS superblock
 */
static int afs_get_sb(struct file_system_type *fs_type,
		      int flags,
		      const char *dev_name,
		      void *options,
		      struct vfsmount *mnt)
{
	struct afs_mount_params params;
	struct super_block *sb;
	struct afs_volume *vol;
	struct key *key;
	char *new_opts = kstrdup(options, GFP_KERNEL);
	int ret;

	_enter(",,%s,%p", dev_name, options);

	memset(&params, 0, sizeof(params));

	/* parse the options and device name */
	if (options) {
		ret = afs_parse_options(&params, options, &dev_name);
		if (ret < 0)
			goto error;
	}

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

	/* parse the device name */
	vol = afs_volume_lookup(&params);
	if (IS_ERR(vol)) {
		ret = PTR_ERR(vol);
		goto error;
	}
	params.volume = vol;

	/* allocate a deviceless superblock */
	sb = sget(fs_type, afs_test_super, set_anon_super, &params);
	if (IS_ERR(sb)) {
		ret = PTR_ERR(sb);
		goto error;
	}

	if (!sb->s_root) {
		/* initial superblock/root creation */
		_debug("create");
		sb->s_flags = flags;
		ret = afs_fill_super(sb, &params);
		if (ret < 0) {
			up_write(&sb->s_umount);
			deactivate_super(sb);
			goto error;
		}
		sb->s_options = new_opts;
		sb->s_flags |= MS_ACTIVE;
	} else {
		_debug("reuse");
		kfree(new_opts);
		ASSERTCMP(sb->s_flags, &, MS_ACTIVE);
	}

	simple_set_mnt(mnt, sb);
	afs_put_volume(params.volume);
	afs_put_cell(params.cell);
	_leave(" = 0 [%p]", sb);
	return 0;

error:
	afs_put_volume(params.volume);
	afs_put_cell(params.cell);
	key_put(params.key);
	kfree(new_opts);
	_leave(" = %d", ret);
	return ret;
}

/*
 * finish the unmounting process on the superblock
 */
static void afs_put_super(struct super_block *sb)
{
	struct afs_super_info *as = sb->s_fs_info;

	_enter("");

	afs_put_volume(as->volume);

	_leave("");
}

/*
 * initialise an inode cache slab element prior to any use
 */
static void afs_i_init_once(struct kmem_cache *cachep, void *_vnode)
{
	struct afs_vnode *vnode = _vnode;

	memset(vnode, 0, sizeof(*vnode));
	inode_init_once(&vnode->vfs_inode);
	init_waitqueue_head(&vnode->update_waitq);
	mutex_init(&vnode->permits_lock);
	mutex_init(&vnode->validate_lock);
	spin_lock_init(&vnode->writeback_lock);
	spin_lock_init(&vnode->lock);
	INIT_LIST_HEAD(&vnode->writebacks);
	INIT_LIST_HEAD(&vnode->pending_locks);
	INIT_LIST_HEAD(&vnode->granted_locks);
	INIT_DELAYED_WORK(&vnode->lock_work, afs_lock_work);
	INIT_WORK(&vnode->cb_broken_work, afs_broken_callback_work);
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

	memset(&vnode->fid, 0, sizeof(vnode->fid));
	memset(&vnode->status, 0, sizeof(vnode->status));

	vnode->volume		= NULL;
	vnode->update_cnt	= 0;
	vnode->flags		= 1 << AFS_VNODE_UNSET;
	vnode->cb_promised	= false;

	_leave(" = %p", &vnode->vfs_inode);
	return &vnode->vfs_inode;
}

/*
 * destroy an AFS inode struct
 */
static void afs_destroy_inode(struct inode *inode)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);

	_enter("%p{%x:%u}", inode, vnode->fid.vid, vnode->fid.vnode);

	_debug("DESTROY INODE %p", inode);

	ASSERTCMP(vnode->server, ==, NULL);

	kmem_cache_free(afs_inode_cachep, vnode);
	atomic_dec(&afs_count_active_inodes);
}

/*
 * return information about an AFS volume
 */
static int afs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct afs_volume_status vs;
	struct afs_vnode *vnode = AFS_FS_I(dentry->d_inode);
	struct key *key;
	int ret;

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key))
		return PTR_ERR(key);

	ret = afs_vnode_get_volume_status(vnode, key, &vs);
	key_put(key);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}

	buf->f_type	= dentry->d_sb->s_magic;
	buf->f_bsize	= AFS_BLOCK_SIZE;
	buf->f_namelen	= AFSNAMEMAX - 1;

	if (vs.max_quota == 0)
		buf->f_blocks = vs.part_max_blocks;
	else
		buf->f_blocks = vs.max_quota;
	buf->f_bavail = buf->f_bfree = buf->f_blocks - vs.blocks_in_use;
	return 0;
}
