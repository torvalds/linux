/*
 *  arch/arm/include/asm/domain.h
 *
 *  Copyright (C) 1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PROC_DOMAIN_H
#define __ASM_PROC_DOMAIN_H

#ifndef __ASSEMBLY__
#include <asm/barrier.h>
#endif

/*
 * Domain numbers
 *
 *  DOMAIN_IO     - domain 2 includes all IO only
 *  DOMAIN_USER   - domain 1 includes all user memory only
 *  DOMAIN_KERNEL - domain 0 includes all kernel memory only
 *
 * The domain numbering depends on whether we support 36 physical
 * address for I/O or not.  Addresses above the 32 bit boundary can
 * only be mapped using supersections and supersections can only
 * be set for domain 0.  We could just default to DOMAIN_IO as zero,
 * but there may be systems with supersection support and no 36-bit
 * addressing.  In such cases, we want to map system memory with
 * supersections to reduce TLB misses and footprint.
 *
 * 36-bit addressing and supersections are only available on
 * CPUs based on ARMv6+ or the Intel XSC3 core.
 */
#ifndef CONFIG_IO_36
#define DOMAIN_KERNEL	0
#define DOMAIN_TABLE	0
#define DOMAIN_USER	1
#define DOMAIN_IO	2
#else
#define DOMAIN_KERNEL	2
#define DOMAIN_TABLE	2
#define DOMAIN_USER	1
#define DOMAIN_IO	0
#endif

/*
 * Domain types
 */
#define DOMAIN_NOACCESS	0
#define DOMAIN_CLIENT	1
#ifdef CONFIG_CPU_USE_DOMAINS
#define DOMAIN_MANAGER	3
#else
#define DOMAIN_MANAGER	1
#endif

#define domain_val(dom,type)	((type) << (2*(dom)))

#ifndef __ASSEMBLY__

static inline unsigned int get_domain(void)
{
	unsigned int domain;

	asm(
	"mrc	p15, 0, %0, c3, c0	@ get domain"
	 : "=r" (domain));

	return domain;
}

#ifdef CONFIG_CPU_USE_DOMAINS
static inline void set_domain(unsigned val)
{
	asm volatile(
	"mcr	p15, 0, %0, c3, c0	@ set domain"
	  : : "r" (val));
	isb();
}

#define modify_domain(dom,type)					\
	do {							\
		unsigned int domain = get_domain();		\
		domain &= ~domain_val(dom, DOMAIN_MANAGER);	\
		domain = domain | domain_val(dom, type);	\
		set_domain(domain);				\
	} while (0)

#else
static inline void set_domain(unsigned val) { }
static inline void modify_domain(unsigned dom, unsigned type)	{ }
#endif

/*
 * Generate the T (user) versions of the LDR/STR and related
 * instructions (inline assembly)
 */
#ifdef CONFIG_CPU_USE_DOMAINS
#define TUSER(instr)	#instr "t"
#else
#define TUSER(instr)	#instr
#endif

#else /* __ASSEMBLY__ */

/*
 * Generate the T (user) versions of the LDR/STR and related
 * instructions
 */
#ifdef CONFIG_CPU_USE_DOMAINS
#define TUSER(instr)	instr ## t
#else
#define TUSER(instr)	instr
#endif

#endif /* __ASSEMBLY__ */

#endif /* !__ASM_PROC_DOMAIN_H */
