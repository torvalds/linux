/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_CACHE_INSNS_32_H
#define __ASM_SH_CACHE_INSNS_32_H

#include <linux/types.h>

#if defined(CONFIG_CPU_SH4A)
#define __icbi(addr)	__asm__ __volatile__ ( "icbi @%0\n\t" : : "r" (addr))
#else
#define __icbi(addr)	mb()
#endif

#define __ocbp(addr)	__asm__ __volatile__ ( "ocbp @%0\n\t" : : "r" (addr))
#define __ocbi(addr)	__asm__ __volatile__ ( "ocbi @%0\n\t" : : "r" (addr))
#define __ocbwb(addr)	__asm__ __volatile__ ( "ocbwb @%0\n\t" : : "r" (addr))

static inline reg_size_t register_align(void *val)
{
	return (unsigned long)(signed long)val;
}

#endif /* __ASM_SH_CACHE_INSNS_32_H */
