#ifndef __BACKPORT_LINUX_SECURITY_H
#define __BACKPORT_LINUX_SECURITY_H
#include_next <linux/security.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
/*
 * This has been defined in include/linux/security.h for some time, but was
 * only given an EXPORT_SYMBOL for 3.1.  Add a compat_* definition to avoid
 * breaking the compile.
 */
#define security_sk_clone(a, b) compat_security_sk_clone(a, b)

static inline void security_sk_clone(const struct sock *sk, struct sock *newsk)
{
}
#endif

#endif /* __BACKPORT_LINUX_SECURITY_H */
