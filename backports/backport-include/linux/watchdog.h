#ifndef __BACKPORT_WATCHDOG_H
#define __BACKPORT_WATCHDOG_H
#include_next <linux/watchdog.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
#define watchdog_device LINUX_BACKPORT(watchdog_device)
struct watchdog_device {
};
#endif

#endif /* __BACKPORT_WATCHDOG_H */
