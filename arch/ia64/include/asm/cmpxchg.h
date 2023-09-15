/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_CMPXCHG_H
#define _ASM_IA64_CMPXCHG_H

#include <uapi/asm/cmpxchg.h>

#define arch_xchg(ptr, x)	\
({(__typeof__(*(ptr))) __arch_xchg((unsigned long) (x), (ptr), sizeof(*(ptr)));})

#define arch_cmpxchg(ptr, o, n)		cmpxchg_acq((ptr), (o), (n))
#define arch_cmpxchg64(ptr, o, n)	cmpxchg_acq((ptr), (o), (n))

#define arch_cmpxchg_local		arch_cmpxchg
#define arch_cmpxchg64_local		arch_cmpxchg64

#ifdef CONFIG_IA64_DEBUG_CMPXCHG
# define CMPXCHG_BUGCHECK_DECL	int _cmpxchg_bugcheck_count = 128;
# define CMPXCHG_BUGCHECK(v)						\
do {									\
	if (_cmpxchg_bugcheck_count-- <= 0) {				\
		void *ip;						\
		extern int _printk(const char *fmt, ...);		\
		ip = (void *) ia64_getreg(_IA64_REG_IP);		\
		_printk("CMPXCHG_BUGCHECK: stuck at %p on word %p\n", ip, (v));\
		break;							\
	}								\
} while (0)
#else /* !CONFIG_IA64_DEBUG_CMPXCHG */
# define CMPXCHG_BUGCHECK_DECL
# define CMPXCHG_BUGCHECK(v)
#endif /* !CONFIG_IA64_DEBUG_CMPXCHG */

#endif /* _ASM_IA64_CMPXCHG_H */
