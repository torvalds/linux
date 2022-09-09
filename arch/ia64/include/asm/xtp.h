/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_XTP_H
#define _ASM_IA64_XTP_H

#include <asm/io.h>

#ifdef CONFIG_SMP

#define XTP_OFFSET		0x1e0008

#define SMP_IRQ_REDIRECTION	(1 << 0)
#define SMP_IPI_REDIRECTION	(1 << 1)

extern unsigned char smp_int_redirect;

/*
 * XTP control functions:
 *	min_xtp   : route all interrupts to this CPU
 *	normal_xtp: nominal XTP value
 *	max_xtp   : never deliver interrupts to this CPU.
 */

static inline void
min_xtp (void)
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x00, ipi_base_addr + XTP_OFFSET); /* XTP to min */
}

static inline void
normal_xtp (void)
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x08, ipi_base_addr + XTP_OFFSET); /* XTP normal */
}

static inline void
max_xtp (void)
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x0f, ipi_base_addr + XTP_OFFSET); /* Set XTP to max */
}

#endif /* CONFIG_SMP */

#endif /* _ASM_IA64_XTP_Hy */
