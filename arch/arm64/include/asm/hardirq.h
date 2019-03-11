/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/cache.h>
#include <linux/percpu.h>
#include <linux/threads.h>
#include <asm/barrier.h>
#include <asm/irq.h>
#include <asm/kvm_arm.h>
#include <asm/sysreg.h>

#define NR_IPI	7

typedef struct {
	unsigned int __softirq_pending;
	unsigned int ipi_irqs[NR_IPI];
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define __inc_irq_stat(cpu, member)	__IRQ_STAT(cpu, member)++
#define __get_irq_stat(cpu, member)	__IRQ_STAT(cpu, member)

u64 smp_irq_stat_cpu(unsigned int cpu);
#define arch_irq_stat_cpu	smp_irq_stat_cpu

#define __ARCH_IRQ_EXIT_IRQS_DISABLED	1

struct nmi_ctx {
	u64 hcr;
};

DECLARE_PER_CPU(struct nmi_ctx, nmi_contexts);

#define arch_nmi_enter()							\
	do {									\
		if (is_kernel_in_hyp_mode()) {					\
			struct nmi_ctx *nmi_ctx = this_cpu_ptr(&nmi_contexts);	\
			nmi_ctx->hcr = read_sysreg(hcr_el2);			\
			if (!(nmi_ctx->hcr & HCR_TGE)) {			\
				write_sysreg(nmi_ctx->hcr | HCR_TGE, hcr_el2);	\
				isb();						\
			}							\
		}								\
	} while (0)

#define arch_nmi_exit()								\
	do {									\
		if (is_kernel_in_hyp_mode()) {					\
			struct nmi_ctx *nmi_ctx = this_cpu_ptr(&nmi_contexts);	\
			if (!(nmi_ctx->hcr & HCR_TGE))				\
				write_sysreg(nmi_ctx->hcr, hcr_el2);		\
		}								\
	} while (0)

static inline void ack_bad_irq(unsigned int irq)
{
	extern unsigned long irq_err_count;
	irq_err_count++;
}

#endif /* __ASM_HARDIRQ_H */
