/*
 * V9FS VFS extensions.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */
#ifndef FS_9P_V9FS_VFS_H
#define FS_9P_V9FS_VFS_H

/* plan9 semantics are that created files are implicitly opened.
 * But linux semantics are that you call create, then open.
 * the plan9 approach is superior as it provides an atomic
 * open.
 * we track the create fid here. When the file is opened, if fidopen is
 * non-zero, we use the fid and can skip some steps.
 * there may be a better way to do this, but I don't know it.
 * one BAD way is to clunk the fid on create, then open it again:
 * you lose the atomicity of file open
 */

/* special case:
 * unlink calls remove, which is an implicit clunk. So we have to track
 * that kind of thing so that we don't try to clunk a dead fid.
 */
#define P9_LOCK_TIMEOUT (30*HZ)

extern struct file_system_type v9fs_fs_type;
extern const struct address_space_operations v9fs_addr_operations;
extern const struct file_operations v9fs_file_operations;
extern const struct file_operations v9fs_file_operations_dotl;
extern const struct file_operations v9fs_dir_operations;
extern const struct file_operations v9fs_dir_operations_dotl;
extern const struct dentry_operations v9fs_dentry_operations;
extern const struct dentry_operations v9fs_cached_dentry_operations;
extern const struct file_operations v9fs_cached_file_operations;
extern const struct file_operations v9fs_cached_file_operations_dotl;
extern const struct file_operations v9fs_mmap_file_operations;
extern const struct file_operations v9fs_mmap_file_operations_dotl;
extern struct kmem_cache *v9fs_inode_cache;

struct inode *v9fs_alloc_inode(struct super_block *sb);
void v9fs_destroy_inode(struct inode *inode);
struct inode *v9fs_get_inode(struct super_block *sb, umode_t mode, dev_t);
int v9fs_init_inode(struct v9fs_session_info *v9ses,
		    struct inode *inode, umode_t mode, dev_t);
void v9fs_evict_inode(struct inode *inode);
ino_t v9fs_qid2ino(struct p9_qid *qid);
void v9fs_stat2inode(struct p9_wstat *, struct inode *, struct super_block *);
void v9fs_stat2inode_dotl(struct p9_stat_dotl *, struct inode *);
int v9fs_dir_release(struct inode *inode, struct file *filp);
int v9fs_file_open(struct inode *inode, struct file *file);
void v9fs_inode2stat(struct inode *inode, struct p9_wstat *stat);
int v9fs_uflags2omode(int uflags, int extended);

ssize_t v9fs_file_readn(struct file *, char *, char __user *, u32, u64);
ssize_t v9fs_fid_readn(struct p9_fid *, char *, char __user *, u32, u64);
void v9fs_blank_wstat(struct p9_wstat *wstat);
int v9fs_vfs_setattr_dotl(struct dentry *, struct iattr *);
int v9fs_file_fsync_dotl(struct file *filp, loff_t start, loff_t end,
			 int datasync);
ssize_t v9fs_file_write_internal(struct inode *, struct p9_fid *,
				 const char __user *, size_t, loff_t *, int);
int v9fs_refresh_inode(struct p9_fid *fid, struct inode *inode);
int v9fs_refresh_inode_dotl(struct p9_fid *fid, struct inode *inode);
static inline void v9fs_invalidate_inode_attr(struct inode *inode)
{
	struct v9fs_inode *v9inode;
	v9inode = V9FS_I(inode);
	v9inode->cache_validity |= V9FS_INO_INVALID_ATTR;
	return;
}

int v9fs_open_to_dotl_flags(int flags);
#endif
