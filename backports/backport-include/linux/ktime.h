#ifndef __BACKPORT_LINUX_KTIME_H
#define __BACKPORT_LINUX_KTIME_H
#include_next <linux/ktime.h>
#include <linux/version.h>

#if  LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0)
#define ktime_get_raw LINUX_BACKPORT(ktime_get_raw)
extern ktime_t ktime_get_raw(void);

#endif /* < 3.17 */

#endif /* __BACKPORT_LINUX_KTIME_H */
