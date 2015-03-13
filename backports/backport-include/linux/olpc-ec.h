#ifndef _COMPAT_LINUX_OLPC_EC_H
#define _COMPAT_LINUX_OLPC_EC_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
#include_next <linux/olpc-ec.h>
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3,6,0)) */

#endif	/* _COMPAT_LINUX_OLPC_EC_H */
