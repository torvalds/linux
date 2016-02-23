#ifndef __ASM_COMPAT_SIGNAL_H
#define __ASM_COMPAT_SIGNAL_H

#include <linux/bug.h>
#include <linux/compat.h>
#include <linux/compiler.h>

#include <asm/signal.h>
#include <asm/siginfo.h>

#include <asm/uaccess.h>

static inline int __copy_conv_sigset_to_user(compat_sigset_t __user *d,
	const sigset_t *s)
{
	int err;

	BUG_ON(sizeof(*d) != sizeof(*s));
	BUG_ON(_NSIG_WORDS != 2);

	err  = __put_user(s->sig[0],	   &d->sig[0]);
	err |= __put_user(s->sig[0] >> 32, &d->sig[1]);
	err |= __put_user(s->sig[1],	   &d->sig[2]);
	err |= __put_user(s->sig[1] >> 32, &d->sig[3]);

	return err;
}

static inline int __copy_conv_sigset_from_user(sigset_t *d,
	const compat_sigset_t __user *s)
{
	int err;
	union sigset_u {
		sigset_t	s;
		compat_sigset_t c;
	} *u = (union sigset_u *) d;

	BUG_ON(sizeof(*d) != sizeof(*s));
	BUG_ON(_NSIG_WORDS != 2);

#ifdef CONFIG_CPU_BIG_ENDIAN
	err  = __get_user(u->c.sig[1], &s->sig[0]);
	err |= __get_user(u->c.sig[0], &s->sig[1]);
	err |= __get_user(u->c.sig[3], &s->sig[2]);
	err |= __get_user(u->c.sig[2], &s->sig[3]);
#endif
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	err  = __get_user(u->c.sig[0], &s->sig[0]);
	err |= __get_user(u->c.sig[1], &s->sig[1]);
	err |= __get_user(u->c.sig[2], &s->sig[2]);
	err |= __get_user(u->c.sig[3], &s->sig[3]);
#endif

	return err;
}

#endif /* __ASM_COMPAT_SIGNAL_H */
