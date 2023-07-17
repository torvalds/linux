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

#endif /* _ASM_IA64_CMPXCHG_H */
