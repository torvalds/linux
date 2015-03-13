#ifndef __BACKPORT_PROC_FS_H
#define __BACKPORT_PROC_FS_H
#include_next <linux/proc_fs.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)

#ifdef CONFIG_PROC_FS
/*
 * backport of:
 * procfs: new helper - PDE_DATA(inode)
 */
#define PDE_DATA LINUX_BACKPORT(PDE_DATA)
static inline void *PDE_DATA(const struct inode *inode)
{
	return PROC_I(inode)->pde->data;
}
extern void proc_set_size(struct proc_dir_entry *, loff_t);
extern void proc_set_user(struct proc_dir_entry *, kuid_t, kgid_t);
#else
#define PDE_DATA LINUX_BACKPORT(PDE_DATA)
static inline void *PDE_DATA(const struct inode *inode) {BUG(); return NULL;}
static inline void proc_set_size(struct proc_dir_entry *de, loff_t size) {}
static inline void proc_set_user(struct proc_dir_entry *de, kuid_t uid, kgid_t gid) {}
#endif /* CONFIG_PROC_FS */

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0) */

#endif /* __BACKPORT_PROC_FS_H */
