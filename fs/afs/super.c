/*
 * Copyright (c) 2002 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Howells <dhowells@redhat.com>
 *          David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "vnode.h"
#include "volume.h"
#include "cell.h"
#include "cmservice.h"
#include "fsclient.h"
#include "super.h"
#include "internal.h"

#define AFS_FS_MAGIC 0x6B414653 /* 'kAFS' */

struct afs_mount_params {
	int			rwpath;
	struct afs_cell		*default_cell;
	struct afs_volume	*volume;
};

static void afs_i_init_once(void *foo, kmem_cache_t *cachep,
			    unsigned long flags);

static struct super_block *afs_get_sb(struct file_system_type *fs_type,
				      int flags, const char *dev_name,
				      void *data);

static struct inode *afs_alloc_inode(struct super_block *sb);

static void afs_put_super(struct super_block *sb);

static void afs_destroy_inode(struct inode *inode);

static struct file_system_type afs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "afs",
	.get_sb		= afs_get_sb,
	.kill_sb	= kill_anon_super,
	.fs_flags	= FS_BINARY_MOUNTDATA,
};

static struct super_operations afs_super_ops = {
	.statfs		= simple_statfs,
	.alloc_inode	= afs_alloc_inode,
	.drop_inode	= generic_delete_inode,
	.destroy_inode	= afs_destroy_inode,
	.clear_inode	= afs_clear_inode,
	.put_super	= afs_put_super,
};

static kmem_cache_t *afs_inode_cachep;
static atomic_t afs_count_active_inodes;

/*****************************************************************************/
/*
 * initialise the filesystem
 */
int __init afs_fs_init(void)
{
	int ret;

	_enter("");

	afs_timer_init(&afs_mntpt_expiry_timer, &afs_mntpt_expiry_timer_ops);

	/* create ourselves an inode cache */
	atomic_set(&afs_count_active_inodes, 0);

	ret = -ENOMEM;
	afs_inode_cachep = kmem_cache_create("afs_inode_cache",
					     sizeof(struct afs_vnode),
					     0,
					     SLAB_HWCACHE_ALIGN,
					     afs_i_init_once,
					     NULL);
	if (!afs_inode_cachep) {
		printk(KERN_NOTICE "kAFS: Failed to allocate inode cache\n");
		return ret;
	}

	/* now export our filesystem to lesser mortals */
	ret = register_filesystem(&afs_fs_type);
	if (ret < 0) {
		kmem_cache_destroy(afs_inode_cachep);
		kleave(" = %d", ret);
		return ret;
	}

	kleave(" = 0");
	return 0;
} /* end afs_fs_init() */

/*****************************************************************************/
/*
 * clean up the filesystem
 */
void __exit afs_fs_exit(void)
{
	unregister_filesystem(&afs_fs_type);

	if (atomic_read(&afs_count_active_inodes) != 0) {
		printk("kAFS: %d active inode objects still present\n",
		       atomic_read(&afs_count_active_inodes));
		BUG();
	}

	kmem_cache_destroy(afs_inode_cachep);

} /* end afs_fs_exit() */

/*****************************************************************************/
/*
 * check that an argument has a value
 */
static int want_arg(char **_value, const char *option)
{
	if (!_value || !*_value || !**_value) {
		printk(KERN_NOTICE "kAFS: %s: argument missing\n", option);
		return 0;
	}
	return 1;
} /* end want_arg() */

/*****************************************************************************/
/*
 * check that there's no subsequent value
 */
static int want_no_value(char *const *_value, const char *option)
{
	if (*_value && **_value) {
		printk(KERN_NOTICE "kAFS: %s: Invalid argument: %s\n",
		       option, *_value);
		return 0;
	}
	return 1;
} /* end want_no_value() */

/*****************************************************************************/
/*
 * parse the mount options
 * - this function has been shamelessly adapted from the ext3 fs which
 *   shamelessly adapted it from the msdos fs
 */
static int afs_super_parse_options(struct afs_mount_params *params,
				   char *options,
				   const char **devname)
{
	char *key, *value;
	int ret;

	_enter("%s", options);

	options[PAGE_SIZE - 1] = 0;

	ret = 0;
	while ((key = strsep(&options, ",")) != 0)
	{
		value = strchr(key, '=');
		if (value)
			*value++ = 0;

		printk("kAFS: KEY: %s, VAL:%s\n", key, value ?: "-");

		if (strcmp(key, "rwpath") == 0) {
			if (!want_no_value(&value, "rwpath"))
				return -EINVAL;
			params->rwpath = 1;
			continue;
		}
		else if (strcmp(key, "vol") == 0) {
			if (!want_arg(&value, "vol"))
				return -EINVAL;
			*devname = value;
			continue;
		}
		else if (strcmp(key, "cell") == 0) {
			if (!want_arg(&value, "cell"))
				return -EINVAL;
			afs_put_cell(params->default_cell);
			ret = afs_cell_lookup(value,
					      strlen(value),
					      &params->default_cell);
			if (ret < 0)
				return -EINVAL;
			continue;
		}

		printk("kAFS: Unknown mount option: '%s'\n",  key);
		ret = -EINVAL;
		goto error;
	}

	ret = 0;

 error:
	_leave(" = %d", ret);
	return ret;
} /* end afs_super_parse_options() */

/*****************************************************************************/
/*
 * check a superblock to see if it's the one we're looking for
 */
static int afs_test_super(struct super_block *sb, void *data)
{
	struct afs_mount_params *params = data;
	struct afs_super_info *as = sb->s_fs_info;

	return as->volume == params->volume;
} /* end afs_test_super() */

/*****************************************************************************/
/*
 * fill in the superblock
 */
static int afs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct afs_mount_params *params = data;
	struct afs_super_info *as = NULL;
	struct afs_fid fid;
	struct dentry *root = NULL;
	struct inode *inode = NULL;
	int ret;

	kenter("");

	/* allocate a superblock info record */
	as = kmalloc(sizeof(struct afs_super_info), GFP_KERNEL);
	if (!as) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	memset(as, 0, sizeof(struct afs_super_info));

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
	ret = afs_iget(sb, &fid, &inode);
	if (ret < 0)
		goto error;

	ret = -ENOMEM;
	root = d_alloc_root(inode);
	if (!root)
		goto error;

	sb->s_root = root;

	kleave(" = 0");
	return 0;

 error:
	iput(inode);
	afs_put_volume(as->volume);
	kfree(as);

	sb->s_fs_info = NULL;

	kleave(" = %d", ret);
	return ret;
} /* end afs_fill_super() */

/*****************************************************************************/
/*
 * get an AFS superblock
 * - TODO: don't use get_sb_nodev(), but rather call sget() directly
 */
static struct super_block *afs_get_sb(struct file_system_type *fs_type,
				      int flags,
				      const char *dev_name,
				      void *options)
{
	struct afs_mount_params params;
	struct super_block *sb;
	int ret;

	_enter(",,%s,%p", dev_name, options);

	memset(&params, 0, sizeof(params));

	/* start the cache manager */
	ret = afscm_start();
	if (ret < 0) {
		_leave(" = %d", ret);
		return ERR_PTR(ret);
	}

	/* parse the options */
	if (options) {
		ret = afs_super_parse_options(&params, options, &dev_name);
		if (ret < 0)
			goto error;
		if (!dev_name) {
			printk("kAFS: no volume name specified\n");
			ret = -EINVAL;
			goto error;
		}
	}

	/* parse the device name */
	ret = afs_volume_lookup(dev_name,
				params.default_cell,
				params.rwpath,
				&params.volume);
	if (ret < 0)
		goto error;

	/* allocate a deviceless superblock */
	sb = sget(fs_type, afs_test_super, set_anon_super, &params);
	if (IS_ERR(sb))
		goto error;

	sb->s_flags = flags;

	ret = afs_fill_super(sb, &params, flags & MS_SILENT ? 1 : 0);
	if (ret < 0) {
		up_write(&sb->s_umount);
		deactivate_super(sb);
		goto error;
	}
	sb->s_flags |= MS_ACTIVE;

	afs_put_volume(params.volume);
	afs_put_cell(params.default_cell);
	_leave(" = %p", sb);
	return sb;

 error:
	afs_put_volume(params.volume);
	afs_put_cell(params.default_cell);
	afscm_stop();
	_leave(" = %d", ret);
	return ERR_PTR(ret);
} /* end afs_get_sb() */

/*****************************************************************************/
/*
 * finish the unmounting process on the superblock
 */
static void afs_put_super(struct super_block *sb)
{
	struct afs_super_info *as = sb->s_fs_info;

	_enter("");

	afs_put_volume(as->volume);
	afscm_stop();

	_leave("");
} /* end afs_put_super() */

/*****************************************************************************/
/*
 * initialise an inode cache slab element prior to any use
 */
static void afs_i_init_once(void *_vnode, kmem_cache_t *cachep,
			    unsigned long flags)
{
	struct afs_vnode *vnode = (struct afs_vnode *) _vnode;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		memset(vnode, 0, sizeof(*vnode));
		inode_init_once(&vnode->vfs_inode);
		init_waitqueue_head(&vnode->update_waitq);
		spin_lock_init(&vnode->lock);
		INIT_LIST_HEAD(&vnode->cb_link);
		INIT_LIST_HEAD(&vnode->cb_hash_link);
		afs_timer_init(&vnode->cb_timeout,
			       &afs_vnode_cb_timed_out_ops);
	}

} /* end afs_i_init_once() */

/*****************************************************************************/
/*
 * allocate an AFS inode struct from our slab cache
 */
static struct inode *afs_alloc_inode(struct super_block *sb)
{
	struct afs_vnode *vnode;

	vnode = (struct afs_vnode *)
		kmem_cache_alloc(afs_inode_cachep, SLAB_KERNEL);
	if (!vnode)
		return NULL;

	atomic_inc(&afs_count_active_inodes);

	memset(&vnode->fid, 0, sizeof(vnode->fid));
	memset(&vnode->status, 0, sizeof(vnode->status));

	vnode->volume		= NULL;
	vnode->update_cnt	= 0;
	vnode->flags		= 0;

	return &vnode->vfs_inode;
} /* end afs_alloc_inode() */

/*****************************************************************************/
/*
 * destroy an AFS inode struct
 */
static void afs_destroy_inode(struct inode *inode)
{
	_enter("{%lu}", inode->i_ino);

	kmem_cache_free(afs_inode_cachep, AFS_FS_I(inode));

	atomic_dec(&afs_count_active_inodes);

} /* end afs_destroy_inode() */
