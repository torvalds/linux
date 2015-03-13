#ifndef __BACKPORT_LINUX_MODULEPARAM_H
#define __BACKPORT_LINUX_MODULEPARAM_H
#include_next <linux/moduleparam.h>

#ifndef kparam_block_sysfs_write
#define kparam_block_sysfs_write(a)
#endif
#ifndef kparam_unblock_sysfs_write
#define kparam_unblock_sysfs_write(a)
#endif

#endif /* __BACKPORT_LINUX_MODULEPARAM_H */
