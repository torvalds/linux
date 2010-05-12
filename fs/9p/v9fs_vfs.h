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

extern struct file_system_type v9fs_fs_type;
extern const struct address_space_operations v9fs_addr_operations;
extern const struct file_operations v9fs_file_operations;
extern const struct file_operations v9fs_dir_operations;
extern const struct dentry_operations v9fs_dentry_operations;
extern const struct dentry_operations v9fs_cached_dentry_operations;

#ifdef CONFIG_9P_FSCACHE
struct inode *v9fs_alloc_inode(struct super_block *sb);
void v9fs_destroy_inode(struct inode *inode);
#endif

struct inode *v9fs_get_inode(struct super_block *sb, int mode);
void v9fs_clear_inode(struct inode *inode);
ino_t v9fs_qid2ino(struct p9_qid *qid);
void v9fs_stat2inode(struct p9_wstat *, struct inode *, struct super_block *);
int v9fs_dir_release(struct inode *inode, struct file *filp);
int v9fs_file_open(struct inode *inode, struct file *file);
void v9fs_inode2stat(struct inode *inode, struct p9_wstat *stat);
void v9fs_dentry_release(struct dentry *);
int v9fs_uflags2omode(int uflags, int extended);

ssize_t v9fs_file_readn(struct file *, char *, char __user *, u32, u64);
void v9fs_blank_wstat(struct p9_wstat *wstat);
