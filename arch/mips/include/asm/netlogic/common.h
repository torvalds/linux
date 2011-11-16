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

#ifndef _NETLOGIC_COMMON_H_
#define _NETLOGIC_COMMON_H_

/*
 * Common SMP definitions
 */
#define	RESET_VEC_PHYS		0x1fc00000
#define	RESET_DATA_PHYS		(RESET_VEC_PHYS + (1<<10))
#define	BOOT_THREAD_MODE	0
#define	BOOT_NMI_LOCK		4
#define	BOOT_NMI_HANDLER	8

#ifndef __ASSEMBLY__
struct irq_desc;
extern struct plat_smp_ops nlm_smp_ops;
extern char nlm_reset_entry[], nlm_reset_entry_end[];
void nlm_smp_function_ipi_handler(unsigned int irq, struct irq_desc *desc);
void nlm_smp_resched_ipi_handler(unsigned int irq, struct irq_desc *desc);
void nlm_smp_irq_init(void);
void nlm_boot_secondary_cpus(void);
int nlm_wakeup_secondary_cpus(u32 wakeup_mask);
void nlm_rmiboot_preboot(void);

static inline void
nlm_set_nmi_handler(void *handler)
{
	char *reset_data;

	reset_data = (char *)CKSEG1ADDR(RESET_DATA_PHYS);
	*(int64_t *)(reset_data + BOOT_NMI_HANDLER) = (long)handler;
}

/*
 * Misc.
 */
unsigned int nlm_get_cpu_frequency(void);

extern unsigned long nlm_common_ebase;
extern int nlm_threads_per_core;
extern uint32_t nlm_cpumask, nlm_coremask;
#endif
#endif /* _NETLOGIC_COMMON_H_ */
