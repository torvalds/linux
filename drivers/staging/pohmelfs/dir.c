/*
 * 2007+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/jhash.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/pagemap.h>

#include "netfs.h"

static int pohmelfs_cmp_hash(struct pohmelfs_name *n, u32 hash)
{
	if (n->hash > hash)
		return -1;
	if (n->hash < hash)
		return 1;

	return 0;
}

static struct pohmelfs_name *pohmelfs_search_hash_unprecise(struct pohmelfs_inode *pi, u32 hash)
{
	struct rb_node *n = pi->hash_root.rb_node;
	struct pohmelfs_name *tmp = NULL;
	int cmp;

	while (n) {
		tmp = rb_entry(n, struct pohmelfs_name, hash_node);

		cmp = pohmelfs_cmp_hash(tmp, hash);
		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			break;

	}

	return tmp;
}

struct pohmelfs_name *pohmelfs_search_hash(struct pohmelfs_inode *pi, u32 hash)
{
	struct pohmelfs_name *tmp;

	tmp = pohmelfs_search_hash_unprecise(pi, hash);
	if (tmp && (tmp->hash == hash))
		return tmp;

	return NULL;
}

static void __pohmelfs_name_del(struct pohmelfs_inode *parent, struct pohmelfs_name *node)
{
	rb_erase(&node->hash_node, &parent->hash_root);
}

/*
 * Remove name cache entry from its caches and free it.
 */
static void pohmelfs_name_free(struct pohmelfs_inode *parent, struct pohmelfs_name *node)
{
	__pohmelfs_name_del(parent, node);
	list_del(&node->sync_create_entry);
	kfree(node);
}

static struct pohmelfs_name *pohmelfs_insert_hash(struct pohmelfs_inode *pi,
		struct pohmelfs_name *new)
{
	struct rb_node **n = &pi->hash_root.rb_node, *parent = NULL;
	struct pohmelfs_name *ret = NULL, *tmp;
	int cmp;

	while (*n) {
		parent = *n;

		tmp = rb_entry(parent, struct pohmelfs_name, hash_node);

		cmp = pohmelfs_cmp_hash(tmp, new->hash);
		if (cmp < 0)
			n = &parent->rb_left;
		else if (cmp > 0)
			n = &parent->rb_right;
		else {
			ret = tmp;
			break;
		}
	}

	if (ret) {
		printk("%s: exist: parent: %llu, ino: %llu, hash: %x, len: %u, data: '%s', "
				           "new: ino: %llu, hash: %x, len: %u, data: '%s'.\n",
				__func__, pi->ino,
				ret->ino, ret->hash, ret->len, ret->data,
				new->ino, new->hash, new->len, new->data);
		ret->ino = new->ino;
		return ret;
	}

	rb_link_node(&new->hash_node, parent, n);
	rb_insert_color(&new->hash_node, &pi->hash_root);

	return NULL;
}

/*
 * Free name cache for given inode.
 */
void pohmelfs_free_names(struct pohmelfs_inode *parent)
{
	struct rb_node *rb_node;
	struct pohmelfs_name *n;

	for (rb_node = rb_first(&parent->hash_root); rb_node;) {
		n = rb_entry(rb_node, struct pohmelfs_name, hash_node);
		rb_node = rb_next(rb_node);

		pohmelfs_name_free(parent, n);
	}
}

static void pohmelfs_fix_offset(struct pohmelfs_inode *parent, struct pohmelfs_name *node)
{
	parent->total_len -= node->len;
}

/*
 * Free name cache entry helper.
 */
void pohmelfs_name_del(struct pohmelfs_inode *parent, struct pohmelfs_name *node)
{
	pohmelfs_fix_offset(parent, node);
	pohmelfs_name_free(parent, node);
}

/*
 * Insert new name cache entry into all hash cache.
 */
static int pohmelfs_insert_name(struct pohmelfs_inode *parent, struct pohmelfs_name *n)
{
	struct pohmelfs_name *name;

	name = pohmelfs_insert_hash(parent, n);
	if (name)
		return -EEXIST;

	parent->total_len += n->len;
	list_add_tail(&n->sync_create_entry, &parent->sync_create_list);

	return 0;
}

/*
 * Allocate new name cache entry.
 */
static struct pohmelfs_name *pohmelfs_name_alloc(unsigned int len)
{
	struct pohmelfs_name *n;

	n = kzalloc(sizeof(struct pohmelfs_name) + len, GFP_KERNEL);
	if (!n)
		return NULL;

	INIT_LIST_HEAD(&n->sync_create_entry);

	n->data = (char *)(n+1);

	return n;
}

/*
 * Add new name entry into directory's cache.
 */
static int pohmelfs_add_dir(struct pohmelfs_sb *psb, struct pohmelfs_inode *parent,
		struct pohmelfs_inode *npi, struct qstr *str, unsigned int mode, int link)
{
	int err = -ENOMEM;
	struct pohmelfs_name *n;

	n = pohmelfs_name_alloc(str->len + 1);
	if (!n)
		goto err_out_exit;

	n->ino = npi->ino;
	n->mode = mode;
	n->len = str->len;
	n->hash = str->hash;
	sprintf(n->data, "%s", str->name);

	mutex_lock(&parent->offset_lock);
	err = pohmelfs_insert_name(parent, n);
	mutex_unlock(&parent->offset_lock);

	if (err) {
		if (err != -EEXIST)
			goto err_out_free;
		kfree(n);
	}

	return 0;

err_out_free:
	kfree(n);
err_out_exit:
	return err;
}

/*
 * Create new inode for given parameters (name, inode info, parent).
 * This does not create object on the server, it will be synced there during writeback.
 */
struct pohmelfs_inode *pohmelfs_new_inode(struct pohmelfs_sb *psb,
		struct pohmelfs_inode *parent, struct qstr *str,
		struct netfs_inode_info *info, int link)
{
	struct inode *new = NULL;
	struct pohmelfs_inode *npi;
	int err = -EEXIST;

	dprintk("%s: creating inode: parent: %llu, ino: %llu, str: %p.\n",
			__func__, (parent)?parent->ino:0, info->ino, str);

	err = -ENOMEM;
	new = iget_locked(psb->sb, info->ino);
	if (!new)
		goto err_out_exit;

	npi = POHMELFS_I(new);
	npi->ino = info->ino;
	err = 0;

	if (new->i_state & I_NEW) {
		dprintk("%s: filling VFS inode: %lu/%llu.\n",
				__func__, new->i_ino, info->ino);
		pohmelfs_fill_inode(new, info);

		if (S_ISDIR(info->mode)) {
			struct qstr s;

			s.name = ".";
			s.len = 1;
			s.hash = jhash(s.name, s.len, 0);

			err = pohmelfs_add_dir(psb, npi, npi, &s, info->mode, 0);
			if (err)
				goto err_out_put;

			s.name = "..";
			s.len = 2;
			s.hash = jhash(s.name, s.len, 0);

			err = pohmelfs_add_dir(psb, npi, (parent)?parent:npi, &s,
					(parent)?parent->vfs_inode.i_mode:npi->vfs_inode.i_mode, 0);
			if (err)
				goto err_out_put;
		}
	}

	if (str) {
		if (parent) {
			err = pohmelfs_add_dir(psb, parent, npi, str, info->mode, link);

			dprintk("%s: %s inserted name: '%s', new_offset: %llu, ino: %llu, parent: %llu.\n",
					__func__, (err)?"unsuccessfully":"successfully",
					str->name, parent->total_len, info->ino, parent->ino);

			if (err && err != -EEXIST)
				goto err_out_put;
		}
	}

	if (new->i_state & I_NEW) {
		if (parent)
			mark_inode_dirty(&parent->vfs_inode);
		mark_inode_dirty(new);
	}

	set_bit(NETFS_INODE_OWNED, &npi->state);
	npi->lock_type = POHMELFS_WRITE_LOCK;
	unlock_new_inode(new);

	return npi;

err_out_put:
	printk("%s: putting inode: %p, npi: %p, error: %d.\n", __func__, new, npi, err);
	iput(new);
err_out_exit:
	return ERR_PTR(err);
}

static int pohmelfs_remote_sync_complete(struct page **pages, unsigned int page_num,
		void *private, int err)
{
	struct pohmelfs_inode *pi = private;
	struct pohmelfs_sb *psb = POHMELFS_SB(pi->vfs_inode.i_sb);

	dprintk("%s: ino: %llu, err: %d.\n", __func__, pi->ino, err);

	if (err)
		pi->error = err;
	wake_up(&psb->wait);
	pohmelfs_put_inode(pi);

	return err;
}

/*
 * Receive directory content from the server.
 * This should be only done for objects, which were not created locally,
 * and which were not synced previously.
 */
static int pohmelfs_sync_remote_dir(struct pohmelfs_inode *pi)
{
	struct inode *inode = &pi->vfs_inode;
	struct pohmelfs_sb *psb = POHMELFS_SB(inode->i_sb);
	long ret = psb->wait_on_page_timeout;
	int err;

	dprintk("%s: dir: %llu, state: %lx: remote_synced: %d.\n",
		__func__, pi->ino, pi->state, test_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state));

	if (test_bit(NETFS_INODE_REMOTE_DIR_SYNCED, &pi->state))
		return 0;

	if (!igrab(inode)) {
		err = -ENOENT;
		goto err_out_exit;
	}

	err = pohmelfs_meta_command(pi, NETFS_READDIR, NETFS_TRANS_SINGLE_DST,
			pohmelfs_remote_sync_complete, pi, 0);
	if (err)
		goto err_out_exit;

	pi->error = 0;
	ret = wait_event_interruptible_timeout(psb->wait,
			test_bit(NETFS_INODE_REMOTE_DIR_SYNCED, &pi->state) || pi->error, ret);
	dprintk("%s: awake dir: %llu, ret: %ld, err: %d.\n", __func__, pi->ino, ret, pi->error);
	if (ret <= 0) {
		err = ret;
		if (!err)
			err = -ETIMEDOUT;
		goto err_out_exit;
	}

	if (pi->error)
		return pi->error;

	return 0;

err_out_exit:
	clear_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state);

	return err;
}

static int pohmelfs_dir_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

/*
 * VFS readdir callback. Syncs directory content from server if needed,
 * and provides direntry info to the userspace.
 */
static int pohmelfs_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	struct pohmelfs_name *n;
	struct rb_node *rb_node;
	int err = 0, mode;
	u64 len;

	dprintk("%s: parent: %llu, fpos: %llu, hash: %08lx.\n",
			__func__, pi->ino, (u64)file->f_pos,
			(unsigned long)file->private_data);
#if 0
	err = pohmelfs_data_lock(pi, 0, ~0, POHMELFS_READ_LOCK);
	if (err)
		return err;
#endif
	err = pohmelfs_sync_remote_dir(pi);
	if (err)
		return err;

	if (file->private_data && (file->private_data == (void *)(unsigned long)file->f_pos))
		return 0;

	mutex_lock(&pi->offset_lock);
	n = pohmelfs_search_hash_unprecise(pi, (unsigned long)file->private_data);

	while (n) {
		mode = (n->mode >> 12) & 15;

		dprintk("%s: offset: %llu, parent ino: %llu, name: '%s', len: %u, ino: %llu, "
				"mode: %o/%o, fpos: %llu, hash: %08x.\n",
				__func__, file->f_pos, pi->ino, n->data, n->len,
				n->ino, n->mode, mode, file->f_pos, n->hash);

		file->private_data = (void *)(unsigned long)n->hash;

		len = n->len;
		err = filldir(dirent, n->data, n->len, file->f_pos, n->ino, mode);

		if (err < 0) {
			dprintk("%s: err: %d.\n", __func__, err);
			err = 0;
			break;
		}

		file->f_pos += len;

		rb_node = rb_next(&n->hash_node);

		if (!rb_node || (rb_node == &n->hash_node)) {
			file->private_data = (void *)(unsigned long)file->f_pos;
			break;
		}

		n = rb_entry(rb_node, struct pohmelfs_name, hash_node);
	}
	mutex_unlock(&pi->offset_lock);

	return err;
}

static loff_t pohmelfs_dir_lseek(struct file *file, loff_t offset, int origin)
{
	file->f_pos = offset;
	file->private_data = NULL;
	return offset;
}

const struct file_operations pohmelfs_dir_fops = {
	.open = pohmelfs_dir_open,
	.read = generic_read_dir,
	.llseek = pohmelfs_dir_lseek,
	.readdir = pohmelfs_readdir,
};

/*
 * Lookup single object on server.
 */
static int pohmelfs_lookup_single(struct pohmelfs_inode *parent,
		struct qstr *str, u64 ino)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(parent->vfs_inode.i_sb);
	long ret = msecs_to_jiffies(5000);
	int err;

	set_bit(NETFS_COMMAND_PENDING, &parent->state);
	err = pohmelfs_meta_command_data(parent, parent->ino, NETFS_LOOKUP,
			(char *)str->name, NETFS_TRANS_SINGLE_DST, NULL, NULL, ino);
	if (err)
		goto err_out_exit;

	err = 0;
	ret = wait_event_interruptible_timeout(psb->wait,
			!test_bit(NETFS_COMMAND_PENDING, &parent->state), ret);
	if (ret <= 0) {
		err = ret;
		if (!err)
			err = -ETIMEDOUT;
	}

	if (err)
		goto err_out_exit;

	return 0;

err_out_exit:
	clear_bit(NETFS_COMMAND_PENDING, &parent->state);

	printk("%s: failed: parent: %llu, ino: %llu, name: '%s', err: %d.\n",
			__func__, parent->ino, ino, str->name, err);

	return err;
}

/*
 * VFS lookup callback.
 * We first try to get inode number from local name cache, if we have one,
 * then inode can be found in inode cache. If there is no inode or no object in
 * local cache, try to lookup it on server. This only should be done for directories,
 * which were not created locally, otherwise remote server does not know about dir at all,
 * so no need to try to know that.
 */
struct dentry *pohmelfs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct pohmelfs_inode *parent = POHMELFS_I(dir);
	struct pohmelfs_name *n;
	struct inode *inode = NULL;
	unsigned long ino = 0;
	int err, lock_type = POHMELFS_READ_LOCK, need_lock = 1;
	struct qstr str = dentry->d_name;

	if ((nd->intent.open.flags & O_ACCMODE) > 1)
		lock_type = POHMELFS_WRITE_LOCK;

	if (test_bit(NETFS_INODE_OWNED, &parent->state)) {
		if (lock_type == parent->lock_type)
			need_lock = 0;
		if ((lock_type == POHMELFS_READ_LOCK) && (parent->lock_type == POHMELFS_WRITE_LOCK))
			need_lock = 0;
	}

	if ((lock_type == POHMELFS_READ_LOCK) && !test_bit(NETFS_INODE_REMOTE_DIR_SYNCED, &parent->state))
		need_lock = 1;

	str.hash = jhash(dentry->d_name.name, dentry->d_name.len, 0);

	mutex_lock(&parent->offset_lock);
	n = pohmelfs_search_hash(parent, str.hash);
	if (n)
		ino = n->ino;
	mutex_unlock(&parent->offset_lock);

	dprintk("%s: start ino: %lu, inode: %p, name: '%s', hash: %x, parent_state: %lx, need_lock: %d.\n",
			__func__, ino, inode, str.name, str.hash, parent->state, need_lock);

	if (ino) {
		inode = ilookup(dir->i_sb, ino);
		if (inode)
			goto out;
	}

	dprintk("%s: no inode dir: %p, dir_ino: %llu, name: '%s', len: %u, dir_state: %lx, ino: %lu.\n",
			__func__, dir, parent->ino,
			str.name, str.len, parent->state, ino);

	if (!ino) {
		if (!need_lock)
			goto out;
	}

	err = pohmelfs_data_lock(parent, 0, ~0, lock_type);
	if (err)
		goto out;

	err = pohmelfs_lookup_single(parent, &str, ino);
	if (err)
		goto out;

	if (!ino) {
		mutex_lock(&parent->offset_lock);
		n = pohmelfs_search_hash(parent, str.hash);
		if (n)
			ino = n->ino;
		mutex_unlock(&parent->offset_lock);
	}

	if (ino) {
		inode = ilookup(dir->i_sb, ino);
		dprintk("%s: second lookup ino: %lu, inode: %p, name: '%s', hash: %x.\n",
				__func__, ino, inode, str.name, str.hash);
		if (!inode) {
			dprintk("%s: No inode for ino: %lu, name: '%s', hash: %x.\n",
				__func__, ino, str.name, str.hash);
			/* return NULL; */
			return ERR_PTR(-EACCES);
		}
	} else {
		printk("%s: No inode number : name: '%s', hash: %x.\n",
			__func__, str.name, str.hash);
	}
out:
	return d_splice_alias(inode, dentry);
}

/*
 * Create new object in local cache. Object will be synced to server
 * during writeback for given inode.
 */
struct pohmelfs_inode *pohmelfs_create_entry_local(struct pohmelfs_sb *psb,
	struct pohmelfs_inode *parent, struct qstr *str, u64 start, int mode)
{
	struct pohmelfs_inode *npi;
	int err = -ENOMEM;
	struct netfs_inode_info info;

	dprintk("%s: name: '%s', mode: %o, start: %llu.\n",
			__func__, str->name, mode, start);

	info.mode = mode;
	info.ino = start;

	if (!start)
		info.ino = pohmelfs_new_ino(psb);

	info.nlink = S_ISDIR(mode)?2:1;
	info.uid = current_fsuid();
	info.gid = current_fsgid();
	info.size = 0;
	info.blocksize = 512;
	info.blocks = 0;
	info.rdev = 0;
	info.version = 0;

	npi = pohmelfs_new_inode(psb, parent, str, &info, !!start);
	if (IS_ERR(npi)) {
		err = PTR_ERR(npi);
		goto err_out_unlock;
	}

	return npi;

err_out_unlock:
	dprintk("%s: err: %d.\n", __func__, err);
	return ERR_PTR(err);
}

/*
 * Create local object and bind it to dentry.
 */
static int pohmelfs_create_entry(struct inode *dir, struct dentry *dentry, u64 start, int mode)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(dir->i_sb);
	struct pohmelfs_inode *npi, *parent;
	struct qstr str = dentry->d_name;
	int err;

	parent = POHMELFS_I(dir);

	err = pohmelfs_data_lock(parent, 0, ~0, POHMELFS_WRITE_LOCK);
	if (err)
		return err;

	str.hash = jhash(dentry->d_name.name, dentry->d_name.len, 0);

	npi = pohmelfs_create_entry_local(psb, parent, &str, start, mode);
	if (IS_ERR(npi))
		return PTR_ERR(npi);

	d_instantiate(dentry, &npi->vfs_inode);

	dprintk("%s: parent: %llu, inode: %llu, name: '%s', parent_nlink: %d, nlink: %d.\n",
			__func__, parent->ino, npi->ino, dentry->d_name.name,
			(signed)dir->i_nlink, (signed)npi->vfs_inode.i_nlink);

	return 0;
}

/*
 * VFS create and mkdir callbacks.
 */
static int pohmelfs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	return pohmelfs_create_entry(dir, dentry, 0, mode);
}

static int pohmelfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int err;

	inode_inc_link_count(dir);
	err = pohmelfs_create_entry(dir, dentry, 0, mode | S_IFDIR);
	if (err)
		inode_dec_link_count(dir);

	return err;
}

static int pohmelfs_remove_entry(struct inode *dir, struct dentry *dentry)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(dir->i_sb);
	struct inode *inode = dentry->d_inode;
	struct pohmelfs_inode *parent = POHMELFS_I(dir), *pi = POHMELFS_I(inode);
	struct pohmelfs_name *n;
	int err = -ENOENT;
	struct qstr str = dentry->d_name;

	err = pohmelfs_data_lock(parent, 0, ~0, POHMELFS_WRITE_LOCK);
	if (err)
		return err;

	str.hash = jhash(dentry->d_name.name, dentry->d_name.len, 0);

	dprintk("%s: dir_ino: %llu, inode: %llu, name: '%s', nlink: %d.\n",
			__func__, parent->ino, pi->ino,
			str.name, (signed)inode->i_nlink);

	BUG_ON(!inode);

	mutex_lock(&parent->offset_lock);
	n = pohmelfs_search_hash(parent, str.hash);
	if (n) {
		pohmelfs_fix_offset(parent, n);
		if (test_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state))
			pohmelfs_remove_child(pi, n);

		pohmelfs_name_free(parent, n);
		err = 0;
	}
	mutex_unlock(&parent->offset_lock);

	if (!err) {
		psb->avail_size += inode->i_size;

		pohmelfs_inode_del_inode(psb, pi);

		mark_inode_dirty(dir);

		inode->i_ctime = dir->i_ctime;
		if (inode->i_nlink)
			inode_dec_link_count(inode);
	}

	return err;
}

/*
 * Unlink and rmdir VFS callbacks.
 */
static int pohmelfs_unlink(struct inode *dir, struct dentry *dentry)
{
	return pohmelfs_remove_entry(dir, dentry);
}

static int pohmelfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int err;
	struct inode *inode = dentry->d_inode;

	dprintk("%s: parent: %llu, inode: %llu, name: '%s', parent_nlink: %d, nlink: %d.\n",
			__func__, POHMELFS_I(dir)->ino, POHMELFS_I(inode)->ino,
			dentry->d_name.name, (signed)dir->i_nlink, (signed)inode->i_nlink);

	err = pohmelfs_remove_entry(dir, dentry);
	if (!err) {
		inode_dec_link_count(dir);
		inode_dec_link_count(inode);
	}

	return err;
}

/*
 * Link creation is synchronous.
 * I'm lazy.
 * Earth is somewhat round.
 */
static int pohmelfs_create_link(struct pohmelfs_inode *parent, struct qstr *obj,
		struct pohmelfs_inode *target, struct qstr *tstr)
{
	struct super_block *sb = parent->vfs_inode.i_sb;
	struct pohmelfs_sb *psb = POHMELFS_SB(sb);
	struct netfs_cmd *cmd;
	struct netfs_trans *t;
	void *data;
	int err, parent_len, target_len = 0, cur_len, path_size = 0;

	err = pohmelfs_data_lock(parent, 0, ~0, POHMELFS_WRITE_LOCK);
	if (err)
		return err;

	err = sb->s_op->write_inode(&parent->vfs_inode, 0);
	if (err)
		goto err_out_exit;

	if (tstr)
		target_len = tstr->len;

	parent_len = pohmelfs_path_length(parent);
	if (target)
		target_len += pohmelfs_path_length(target);

	if (parent_len < 0) {
		err = parent_len;
		goto err_out_exit;
	}

	if (target_len < 0) {
		err = target_len;
		goto err_out_exit;
	}

	t = netfs_trans_alloc(psb, parent_len + target_len + obj->len + 2, 0, 0);
	if (!t) {
		err = -ENOMEM;
		goto err_out_exit;
	}
	cur_len = netfs_trans_cur_len(t);

	cmd = netfs_trans_current(t);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err_out_free;
	}

	data = (void *)(cmd + 1);
	cur_len -= sizeof(struct netfs_cmd);

	err = pohmelfs_construct_path_string(parent, data, parent_len);
	if (err > 0) {
		/* Do not place null-byte before the slash */
		path_size = err - 1;
		cur_len -= path_size;

		err = snprintf(data + path_size, cur_len, "/%s|", obj->name);

		path_size += err;
		cur_len -= err;

		cmd->ext = path_size - 1; /* No | symbol */

		if (target) {
			err = pohmelfs_construct_path_string(target, data + path_size, target_len);
			if (err > 0) {
				path_size += err;
				cur_len -= err;
			}
		}
	}

	if (err < 0)
		goto err_out_free;

	cmd->start = 0;

	if (!target && tstr) {
		if (tstr->len > cur_len - 1) {
			err = -ENAMETOOLONG;
			goto err_out_free;
		}

		err = snprintf(data + path_size, cur_len, "%s", tstr->name) + 1; /* 0-byte */
		path_size += err;
		cur_len -= err;
		cmd->start = 1;
	}

	dprintk("%s: parent: %llu, obj: '%s', target_inode: %llu, target_str: '%s', full: '%s'.\n",
			__func__, parent->ino, obj->name, (target)?target->ino:0, (tstr)?tstr->name:NULL,
			(char *)data);

	cmd->cmd = NETFS_LINK;
	cmd->size = path_size;
	cmd->id = parent->ino;

	netfs_convert_cmd(cmd);

	netfs_trans_update(cmd, t, path_size);

	err = netfs_trans_finish(t, psb);
	if (err)
		goto err_out_exit;

	return 0;

err_out_free:
	t->result = err;
	netfs_trans_put(t);
err_out_exit:
	return err;
}

/*
 *  VFS hard and soft link callbacks.
 */
static int pohmelfs_link(struct dentry *old_dentry, struct inode *dir,
	struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	struct pohmelfs_inode *pi = POHMELFS_I(inode);
	int err;
	struct qstr str = dentry->d_name;

	str.hash = jhash(dentry->d_name.name, dentry->d_name.len, 0);

	err = inode->i_sb->s_op->write_inode(inode, 0);
	if (err)
		return err;

	err = pohmelfs_create_link(POHMELFS_I(dir), &str, pi, NULL);
	if (err)
		return err;

	return pohmelfs_create_entry(dir, dentry, pi->ino, inode->i_mode);
}

static int pohmelfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct qstr sym_str;
	struct qstr str = dentry->d_name;
	struct inode *inode;
	int err;

	str.hash = jhash(dentry->d_name.name, dentry->d_name.len, 0);

	sym_str.name = symname;
	sym_str.len = strlen(symname);

	err = pohmelfs_create_link(POHMELFS_I(dir), &str, NULL, &sym_str);
	if (err)
		goto err_out_exit;

	err = pohmelfs_create_entry(dir, dentry, 0, S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO);
	if (err)
		goto err_out_exit;

	inode = dentry->d_inode;

	err = page_symlink(inode, symname, sym_str.len + 1);
	if (err)
		goto err_out_put;

	return 0;

err_out_put:
	iput(inode);
err_out_exit:
	return err;
}

static int pohmelfs_send_rename(struct pohmelfs_inode *pi, struct pohmelfs_inode *parent,
		struct qstr *str)
{
	int path_len, err, total_len = 0, inode_len, parent_len;
	char *path;
	struct netfs_trans *t;
	struct netfs_cmd *cmd;
	struct pohmelfs_sb *psb = POHMELFS_SB(pi->vfs_inode.i_sb);

	parent_len = pohmelfs_path_length(parent);
	inode_len = pohmelfs_path_length(pi);

	if (parent_len < 0 || inode_len < 0)
		return -EINVAL;

	path_len = parent_len + inode_len + str->len + 3;

	t = netfs_trans_alloc(psb, path_len, 0, 0);
	if (!t)
		return -ENOMEM;

	cmd = netfs_trans_current(t);
	path = (char *)(cmd + 1);

	err = pohmelfs_construct_path_string(pi, path, inode_len);
	if (err < 0)
		goto err_out_unlock;

	cmd->ext = err;

	path += err;
	total_len += err;
	path_len -= err;

	*path = '|';
	path++;
	total_len++;
	path_len--;

	err = pohmelfs_construct_path_string(parent, path, parent_len);
	if (err < 0)
		goto err_out_unlock;

	/*
	 * Do not place a null-byte before the final slash and the name.
	 */
	err--;
	path += err;
	total_len += err;
	path_len -= err;

	err = snprintf(path, path_len - 1, "/%s", str->name);

	total_len += err + 1; /* 0 symbol */
	path_len -= err + 1;

	cmd->cmd = NETFS_RENAME;
	cmd->id = pi->ino;
	cmd->start = parent->ino;
	cmd->size = total_len;

	netfs_convert_cmd(cmd);

	netfs_trans_update(cmd, t, total_len);

	return netfs_trans_finish(t, psb);

err_out_unlock:
	netfs_trans_free(t);
	return err;
}

static int pohmelfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *inode = old_dentry->d_inode;
	struct pohmelfs_inode *old_parent, *pi, *new_parent;
	struct qstr str = new_dentry->d_name;
	struct pohmelfs_name *n;
	unsigned int old_hash;
	int err = -ENOENT;

	pi = POHMELFS_I(inode);
	old_parent = POHMELFS_I(old_dir);

	if (new_dir)
		new_dir->i_sb->s_op->write_inode(new_dir, 0);

	old_hash = jhash(old_dentry->d_name.name, old_dentry->d_name.len, 0);
	str.hash = jhash(new_dentry->d_name.name, new_dentry->d_name.len, 0);

	str.len = new_dentry->d_name.len;
	str.name = new_dentry->d_name.name;
	str.hash = jhash(new_dentry->d_name.name, new_dentry->d_name.len, 0);

	if (new_dir) {
		new_parent = POHMELFS_I(new_dir);
		err = -ENOTEMPTY;

		if (S_ISDIR(inode->i_mode) &&
				new_parent->total_len <= 3)
			goto err_out_exit;
	} else {
		new_parent = old_parent;
	}

	dprintk("%s: ino: %llu, parent: %llu, name: '%s' -> parent: %llu, name: '%s', i_size: %llu.\n",
			__func__, pi->ino, old_parent->ino, old_dentry->d_name.name,
			new_parent->ino, new_dentry->d_name.name, inode->i_size);

	if (test_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state) &&
			test_bit(NETFS_INODE_OWNED, &pi->state)) {
		err = pohmelfs_send_rename(pi, new_parent, &str);
		if (err)
			goto err_out_exit;
	}

	n = pohmelfs_name_alloc(str.len + 1);
	if (!n)
		goto err_out_exit;

	mutex_lock(&new_parent->offset_lock);
	n->ino = pi->ino;
	n->mode = inode->i_mode;
	n->len = str.len;
	n->hash = str.hash;
	sprintf(n->data, "%s", str.name);

	err = pohmelfs_insert_name(new_parent, n);
	mutex_unlock(&new_parent->offset_lock);

	if (err)
		goto err_out_exit;

	mutex_lock(&old_parent->offset_lock);
	n = pohmelfs_search_hash(old_parent, old_hash);
	if (n)
		pohmelfs_name_del(old_parent, n);
	mutex_unlock(&old_parent->offset_lock);

	mark_inode_dirty(inode);
	mark_inode_dirty(&new_parent->vfs_inode);

	WARN_ON_ONCE(list_empty(&inode->i_dentry));

	return 0;

err_out_exit:

	clear_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state);

	mutex_unlock(&inode->i_mutex);
	return err;
}

/*
 * POHMELFS directory inode operations.
 */
const struct inode_operations pohmelfs_dir_inode_ops = {
	.link		= pohmelfs_link,
	.symlink	= pohmelfs_symlink,
	.unlink		= pohmelfs_unlink,
	.mkdir		= pohmelfs_mkdir,
	.rmdir		= pohmelfs_rmdir,
	.create		= pohmelfs_create,
	.lookup 	= pohmelfs_lookup,
	.setattr	= pohmelfs_setattr,
	.rename		= pohmelfs_rename,
};
