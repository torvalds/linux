#ifndef _COMPAT_LINUX_FS_H
#define _COMPAT_LINUX_FS_H
#include_next <linux/fs.h>
#include <linux/version.h>
/*
 * some versions don't have this and thus don't
 * include it from the original fs.h
 */
#include <linux/uidgid.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))
#define simple_open LINUX_BACKPORT(simple_open)
extern int simple_open(struct inode *inode, struct file *file);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
/**
 * backport of:
 *
 * commit 496ad9aa8ef448058e36ca7a787c61f2e63f0f54
 * Author: Al Viro <viro@zeniv.linux.org.uk>
 * Date:   Wed Jan 23 17:07:38 2013 -0500
 *
 *     new helper: file_inode(file)
 */
static inline struct inode *file_inode(struct file *f)
{
	return f->f_path.dentry->d_inode;
}
#endif

#ifndef replace_fops
/*
 * This one is to be used *ONLY* from ->open() instances.
 * fops must be non-NULL, pinned down *and* module dependencies
 * should be sufficient to pin the caller down as well.
 */
#define replace_fops(f, fops) \
	do {	\
		struct file *__file = (f); \
		fops_put(__file->f_op); \
		BUG_ON(!(__file->f_op = (fops))); \
	} while(0)
#endif /* replace_fops */

#endif	/* _COMPAT_LINUX_FS_H */
