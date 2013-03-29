#ifndef _M68K_SIGNAL_H
#define _M68K_SIGNAL_H

#include <uapi/asm/signal.h>

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

#define _NSIG		64
#define _NSIG_BPW	32
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef unsigned long old_sigset_t;		/* at least 32 bits */

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

#define __ARCH_HAS_SA_RESTORER

#include <asm/sigcontext.h>

#ifndef CONFIG_CPU_HAS_NO_BITFIELDS
#define __HAVE_ARCH_SIG_BITOPS

static inline void sigaddset(sigset_t *set, int _sig)
{
	asm ("bfset %0{%1,#1}"
		: "+o" (*set)
		: "id" ((_sig - 1) ^ 31)
		: "cc");
}

static inline void sigdelset(sigset_t *set, int _sig)
{
	asm ("bfclr %0{%1,#1}"
		: "+o" (*set)
		: "id" ((_sig - 1) ^ 31)
		: "cc");
}

static inline int __const_sigismember(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	return 1 & (set->sig[sig / _NSIG_BPW] >> (sig % _NSIG_BPW));
}

static inline int __gen_sigismember(sigset_t *set, int _sig)
{
	int ret;
	asm ("bfextu %1{%2,#1},%0"
		: "=d" (ret)
		: "o" (*set), "id" ((_sig-1) ^ 31)
		: "cc");
	return ret;
}

#define sigismember(set,sig)			\
	(__builtin_constant_p(sig) ?		\
	 __const_sigismember(set,sig) :		\
	 __gen_sigismember(set,sig))

static inline int sigfindinword(unsigned long word)
{
	asm ("bfffo %1{#0,#0},%0"
		: "=d" (word)
		: "d" (word & -word)
		: "cc");
	return word ^ 31;
}

#endif /* !CONFIG_CPU_HAS_NO_BITFIELDS */

#ifndef __uClinux__
extern void ptrace_signal_deliver(void);
#define ptrace_signal_deliver ptrace_signal_deliver
#endif /* __uClinux__ */

#endif /* _M68K_SIGNAL_H */
