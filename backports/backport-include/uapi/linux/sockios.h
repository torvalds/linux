#ifndef __BACKPORT_LINUX_SOCKIOS_H
#define __BACKPORT_LINUX_SOCKIOS_H
#include_next <linux/sockios.h>
#include <linux/version.h>

/*
 * Kernel backports UAPI note:
 *
 * We carry UAPI headers for backports to enable compilation
 * of kernel / driver code to compile without any changes. If
 * it so happens that a feature is backported it can be added
 * here but notice that if full subsystems are backported you
 * should just include the respective full header onto the
 * copy-list file so that its copied intact. This strategy
 * is used to either backport a specific feature or to just
 * avoid having to do ifdef changes to compile.
 *
 * Userspace is not expected to copy over backports headers
 * to compile userspace programs, userspace programs can
 * and should consider carrying over a respective copy-list
 * of the latest UAPI kernel headers they need in their
 * upstream sources, the kernel the user uses, whether with
 * backports or not should be able to return -EOPNOTSUPP if
 * the feature is not available and let it through if its
 * supported and meats the expected form.
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#define SIOCGHWTSTAMP	0x89b1		/* get config			*/
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0) */
#endif /* __BACKPORT_LINUX_SOCKIOS_H */
