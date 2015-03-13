#ifndef __BACKPORT_LINUX_ERR_H
#define __BACKPORT_LINUX_ERR_H
#include_next <linux/err.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0)
#define PTR_ERR_OR_ZERO(p) PTR_RET(p)
#endif

#endif /* __BACKPORT_LINUX_ERR_H */
