#ifndef __BACKPORT_LINUX_POLL_H
#define __BACKPORT_LINUX_POLL_H
#include_next <linux/poll.h>
#include <linux/version.h>

#if  LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
#define poll_does_not_wait LINUX_BACKPORT(poll_does_not_wait)
static inline bool poll_does_not_wait(const poll_table *p)
{
	return p == NULL || p->qproc == NULL;
}

#define poll_requested_events LINUX_BACKPORT(poll_requested_events)
static inline unsigned long poll_requested_events(const poll_table *p)
{
	return p ? p->key : ~0UL;
}
#endif /* < 3.4 */

#endif /* __BACKPORT_LINUX_POLL_H */
