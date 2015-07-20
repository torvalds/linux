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
#define RESET_VEC_PHYS		0x1fc00000
#define RESET_VEC_SIZE		8192		/* 8KB reset code and data */
#define RESET_DATA_PHYS		(RESET_VEC_PHYS + (1<<10))

/* Offsets of parameters in the RESET_DATA_PHYS area */
#define BOOT_THREAD_MODE	0
#define BOOT_NMI_LOCK		4
#define BOOT_NMI_HANDLER	8

/* CPU ready flags for each CPU */
#define BOOT_CPU_READY		2048

#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <asm/irq.h>
#include <asm/mach-netlogic/multi-node.h>

struct irq_desc;
void nlm_smp_function_ipi_handler(unsigned int irq, struct irq_desc *desc);
void nlm_smp_resched_ipi_handler(unsigned int irq, struct irq_desc *desc);
void nlm_smp_irq_init(int hwcpuid);
void nlm_boot_secondary_cpus(void);
int nlm_wakeup_secondary_cpus(void);
void nlm_rmiboot_preboot(void);
void nlm_percpu_init(int hwcpuid);

static inline void *
nlm_get_boot_data(int offset)
{
	return (void *)(CKSEG1ADDR(RESET_DATA_PHYS) + offset);
}

static inline void
nlm_set_nmi_handler(void *handler)
{
	void *nmih = nlm_get_boot_data(BOOT_NMI_HANDLER);

	*(int64_t *)nmih = (long)handler;
}

/*
 * Misc.
 */
void nlm_init_boot_cpu(void);
unsigned int nlm_get_cpu_frequency(void);
extern struct plat_smp_ops nlm_smp_ops;
extern char nlm_reset_entry[], nlm_reset_entry_end[];

/* SWIOTLB */
extern struct dma_map_ops nlm_swiotlb_dma_ops;

extern unsigned int nlm_threads_per_core;
extern cpumask_t nlm_cpumask;

struct irq_data;
uint64_t nlm_pci_irqmask(int node);
void nlm_setup_pic_irq(int node, int picirq, int irq, int irt);
void nlm_set_pic_extra_ack(int node, int irq,  void (*xack)(struct irq_data *));

#ifdef CONFIG_PCI_MSI
void nlm_dispatch_msi(int node, int lirq);
void nlm_dispatch_msix(int node, int msixirq);
#endif

/*
 * The NR_IRQs is divided between nodes, each of them has a separate irq space
 */
static inline int nlm_irq_to_xirq(int node, int irq)
{
	return node * NR_IRQS / NLM_NR_NODES + irq;
}

#ifdef CONFIG_CPU_XLR
#define nlm_cores_per_node()	8
#else
static inline int nlm_cores_per_node(void)
{
	return ((read_c0_prid() & PRID_IMP_MASK)
				== PRID_IMP_NETLOGIC_XLP9XX) ? 32 : 8;
}
#endif
static inline int nlm_threads_per_node(void)
{
	return nlm_cores_per_node() * NLM_THREADS_PER_CORE;
}

static inline int nlm_hwtid_to_node(int hwtid)
{
	return hwtid / nlm_threads_per_node();
}

extern int nlm_cpu_ready[];
#endif /* __ASSEMBLY__ */
#endif /* _NETLOGIC_COMMON_H_ */
