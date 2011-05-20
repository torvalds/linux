/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_NLM_XLR_H
#define _ASM_NLM_XLR_H

/* Platform UART functions */
struct uart_port;
unsigned int nlm_xlr_uart_in(struct uart_port *, int);
void nlm_xlr_uart_out(struct uart_port *, int, int);

/* SMP support functions */
struct irq_desc;
void nlm_smp_function_ipi_handler(unsigned int irq, struct irq_desc *desc);
void nlm_smp_resched_ipi_handler(unsigned int irq, struct irq_desc *desc);
int nlm_wakeup_secondary_cpus(u32 wakeup_mask);
void nlm_smp_irq_init(void);
void nlm_boot_smp_nmi(void);
void prom_pre_boot_secondary_cpus(void);

extern struct plat_smp_ops nlm_smp_ops;
extern unsigned long nlm_common_ebase;

/* XLS B silicon "Rook" */
static inline unsigned int nlm_chip_is_xls_b(void)
{
	uint32_t prid = read_c0_prid();

	return ((prid & 0xf000) == 0x4000);
}

/*
 *  XLR chip types
 */
 /* The XLS product line has chip versions 0x[48c]? */
static inline unsigned int nlm_chip_is_xls(void)
{
	uint32_t prid = read_c0_prid();

	return ((prid & 0xf000) == 0x8000 || (prid & 0xf000) == 0x4000 ||
		(prid & 0xf000) == 0xc000);
}

#endif /* _ASM_NLM_XLR_H */
