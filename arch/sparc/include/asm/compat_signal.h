#ifndef _COMPAT_SIGNAL_H
#define _COMPAT_SIGNAL_H

#include <linux/compat.h>
#include <asm/signal.h>

#ifdef CONFIG_COMPAT
struct __new_sigaction32 {
	unsigned int		sa_handler;
	unsigned int    	sa_flags;
	unsigned int		sa_restorer;     /* not used by Linux/SPARC yet */
	compat_sigset_t 	sa_mask;
};

struct __old_sigaction32 {
	unsigned int		sa_handler;
	compat_old_sigset_t  	sa_mask;
	unsigned int    	sa_flags;
	unsigned int		sa_restorer;     /* not used by Linux/SPARC yet */
};
#endif

#endif /* !(_COMPAT_SIGNAL_H) */
