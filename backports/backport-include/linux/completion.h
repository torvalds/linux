#ifndef __BACKPORT_COMPLETION_H
#define __BACKPORT_COMPLETION_H
#include_next <linux/completion.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
/**
 * reinit_completion - reinitialize a completion structure
 * @x:  pointer to completion structure that is to be reinitialized
 *
 * This inline function should be used to reinitialize a completion structure so it can
 * be reused. This is especially important after complete_all() is used.
 */
static inline void reinit_completion(struct completion *x)
{
	x->done = 0;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0) */

#endif /* __BACKPORT_COMPLETION_H */
