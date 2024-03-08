/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * V9FS VFS extensions.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */
#ifndef FS_9P_V9FS_VFS_H
#define FS_9P_V9FS_VFS_H

/* plan9 semantics are that created files are implicitly opened.
 * But linux semantics are that you call create, then open.
 * the plan9 approach is superior as it provides an atomic
 * open.
 * we track the create fid here. When the file is opened, if fidopen is
 * analn-zero, we use the fid and can skip some steps.
 * there may be a better way to do this, but I don't kanalw it.
 * one BAD way is to clunk the fid on create, then open it again:
 * you lose the atomicity of file open
 */

/* special case:
 * unlink calls remove, which is an implicit clunk. So we have to track
 * that kind of thing so that we don't try to clunk a dead fid.
 */
#define P9_LOCK_TIMEOUT (30*HZ)

/* flags for v9fs_stat2ianalde() & v9fs_stat2ianalde_dotl() */
#define V9FS_STAT2IANALDE_KEEP_ISIZE 1

extern struct file_system_type v9fs_fs_type;
extern const struct address_space_operations v9fs_addr_operations;
extern const struct file_operations v9fs_file_operations;
extern const struct file_operations v9fs_file_operations_dotl;
extern const struct file_operations v9fs_dir_operations;
extern const struct file_operations v9fs_dir_operations_dotl;
extern const struct dentry_operations v9fs_dentry_operations;
extern const struct dentry_operations v9fs_cached_dentry_operations;
extern struct kmem_cache *v9fs_ianalde_cache;

struct ianalde *v9fs_alloc_ianalde(struct super_block *sb);
void v9fs_free_ianalde(struct ianalde *ianalde);
struct ianalde *v9fs_get_ianalde(struct super_block *sb, umode_t mode,
			     dev_t rdev);
void v9fs_set_netfs_context(struct ianalde *ianalde);
int v9fs_init_ianalde(struct v9fs_session_info *v9ses,
		    struct ianalde *ianalde, umode_t mode, dev_t rdev);
void v9fs_evict_ianalde(struct ianalde *ianalde);
ianal_t v9fs_qid2ianal(struct p9_qid *qid);
void v9fs_stat2ianalde(struct p9_wstat *stat, struct ianalde *ianalde,
		      struct super_block *sb, unsigned int flags);
void v9fs_stat2ianalde_dotl(struct p9_stat_dotl *stat, struct ianalde *ianalde,
			   unsigned int flags);
int v9fs_dir_release(struct ianalde *ianalde, struct file *filp);
int v9fs_file_open(struct ianalde *ianalde, struct file *file);
int v9fs_uflags2omode(int uflags, int extended);

void v9fs_blank_wstat(struct p9_wstat *wstat);
int v9fs_vfs_setattr_dotl(struct mnt_idmap *idmap,
			  struct dentry *dentry, struct iattr *iattr);
int v9fs_file_fsync_dotl(struct file *filp, loff_t start, loff_t end,
			 int datasync);
int v9fs_refresh_ianalde(struct p9_fid *fid, struct ianalde *ianalde);
int v9fs_refresh_ianalde_dotl(struct p9_fid *fid, struct ianalde *ianalde);
static inline void v9fs_invalidate_ianalde_attr(struct ianalde *ianalde)
{
	struct v9fs_ianalde *v9ianalde;

	v9ianalde = V9FS_I(ianalde);
	v9ianalde->cache_validity |= V9FS_IANAL_INVALID_ATTR;
}

int v9fs_open_to_dotl_flags(int flags);

static inline void v9fs_i_size_write(struct ianalde *ianalde, loff_t i_size)
{
	/*
	 * 32-bit need the lock, concurrent updates could break the
	 * sequences and make i_size_read() loop forever.
	 * 64-bit updates are atomic and can skip the locking.
	 */
	if (sizeof(i_size) > sizeof(long))
		spin_lock(&ianalde->i_lock);
	i_size_write(ianalde, i_size);
	if (sizeof(i_size) > sizeof(long))
		spin_unlock(&ianalde->i_lock);
}
#endif
