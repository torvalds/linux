/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_COMPAT_SIGNAL_H
#define __ASM_COMPAT_SIGNAL_H

#include <linux/bug.h>
#include <linux/compat.h>
#include <linux/compiler.h>

#include <asm/signal.h>
#include <asm/siginfo.h>

#include <linux/uaccess.h>

static inline int __copy_conv_sigset_to_user(compat_sigset_t __user *d,
	const sigset_t *s)
{
	BUILD_BUG_ON(sizeof(*d) != sizeof(*s));
	BUILD_BUG_ON(_NSIG_WORDS != 2);

	return put_compat_sigset(d, s, sizeof(*d));
}

static inline int __copy_conv_sigset_from_user(sigset_t *d,
	const compat_sigset_t __user *s)
{
	return get_compat_sigset(d, s);
}

#endif /* __ASM_COMPAT_SIGNAL_H */
