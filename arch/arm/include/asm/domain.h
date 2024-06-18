/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/domain.h
 *
 *  Copyright (C) 1999 Russell King.
 */
#ifndef __ASM_PROC_DOMAIN_H
#define __ASM_PROC_DOMAIN_H

#ifndef __ASSEMBLY__
#include <linux/thread_info.h>
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
#define DOMAIN_USER	1
#define DOMAIN_IO	2
#else
#define DOMAIN_KERNEL	2
#define DOMAIN_USER	1
#define DOMAIN_IO	0
#endif
#define DOMAIN_VECTORS	3

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

#define domain_mask(dom)	((3) << (2 * (dom)))
#define domain_val(dom,type)	((type) << (2 * (dom)))

#ifdef CONFIG_CPU_SW_DOMAIN_PAN
#define DACR_INIT \
	(domain_val(DOMAIN_USER, DOMAIN_NOACCESS) | \
	 domain_val(DOMAIN_KERNEL, DOMAIN_MANAGER) | \
	 domain_val(DOMAIN_IO, DOMAIN_CLIENT) | \
	 domain_val(DOMAIN_VECTORS, DOMAIN_CLIENT))
#else
#define DACR_INIT \
	(domain_val(DOMAIN_USER, DOMAIN_CLIENT) | \
	 domain_val(DOMAIN_KERNEL, DOMAIN_MANAGER) | \
	 domain_val(DOMAIN_IO, DOMAIN_CLIENT) | \
	 domain_val(DOMAIN_VECTORS, DOMAIN_CLIENT))
#endif

#define __DACR_DEFAULT \
	domain_val(DOMAIN_KERNEL, DOMAIN_CLIENT) | \
	domain_val(DOMAIN_IO, DOMAIN_CLIENT) | \
	domain_val(DOMAIN_VECTORS, DOMAIN_CLIENT)

#define DACR_UACCESS_DISABLE	\
	(__DACR_DEFAULT | domain_val(DOMAIN_USER, DOMAIN_NOACCESS))
#define DACR_UACCESS_ENABLE	\
	(__DACR_DEFAULT | domain_val(DOMAIN_USER, DOMAIN_CLIENT))

#ifndef __ASSEMBLY__

#ifdef CONFIG_CPU_CP15_MMU
static __always_inline unsigned int get_domain(void)
{
	unsigned int domain;

	asm(
	"mrc	p15, 0, %0, c3, c0	@ get domain"
	 : "=r" (domain)
	 : "m" (current_thread_info()->cpu_domain));

	return domain;
}

static __always_inline void set_domain(unsigned int val)
{
	asm volatile(
	"mcr	p15, 0, %0, c3, c0	@ set domain"
	  : : "r" (val) : "memory");
	isb();
}
#else
static __always_inline unsigned int get_domain(void)
{
	return 0;
}

static __always_inline void set_domain(unsigned int val)
{
}
#endif

/*
 * Generate the T (user) versions of the LDR/STR and related
 * instructions (inline assembly)
 */
#ifdef CONFIG_CPU_USE_DOMAINS
#define TUSER(instr)		TUSERCOND(instr, )
#define TUSERCOND(instr, cond)	#instr "t" #cond
#else
#define TUSER(instr)		TUSERCOND(instr, )
#define TUSERCOND(instr, cond)	#instr #cond
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
