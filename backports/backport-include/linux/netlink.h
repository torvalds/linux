#ifndef __BACKPORT_LINUX_NETLINK_H
#define __BACKPORT_LINUX_NETLINK_H
#include_next <linux/netlink.h>
#include <linux/version.h>

/* this is for patches we apply */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
#define netlink_notify_portid(__notify) (__notify->pid)
#define NETLINK_CB_PORTID(__skb) NETLINK_CB(__skb).pid
#else
#define netlink_notify_portid(__notify) (__notify->portid)
#define NETLINK_CB_PORTID(__skb) NETLINK_CB(__skb).portid
#endif

#endif /* __BACKPORT_LINUX_NETLINK_H */
