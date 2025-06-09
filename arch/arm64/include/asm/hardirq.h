/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
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

#define ack_bad_irq ack_bad_irq
#include <asm-generic/hardirq.h>

#define __ARCH_IRQ_EXIT_IRQS_DISABLED	1

struct nmi_ctx {
	u64 hcr;
	unsigned int cnt;
};

DECLARE_PER_CPU(struct nmi_ctx, nmi_contexts);

#define arch_nmi_enter()						\
do {									\
	struct nmi_ctx *___ctx;						\
	u64 ___hcr;							\
									\
	if (!is_kernel_in_hyp_mode())					\
		break;							\
									\
	___ctx = this_cpu_ptr(&nmi_contexts);				\
	if (___ctx->cnt) {						\
		___ctx->cnt++;						\
		break;							\
	}								\
									\
	___hcr = read_sysreg(hcr_el2);					\
	if (!(___hcr & HCR_TGE)) {					\
		write_sysreg_hcr(___hcr | HCR_TGE);			\
		isb();							\
	}								\
	/*								\
	 * Make sure the sysreg write is performed before ___ctx->cnt	\
	 * is set to 1. NMIs that see cnt == 1 will rely on us.		\
	 */								\
	barrier();							\
	___ctx->cnt = 1;                                                \
	/*								\
	 * Make sure ___ctx->cnt is set before we save ___hcr. We	\
	 * don't want ___ctx->hcr to be overwritten.			\
	 */								\
	barrier();							\
	___ctx->hcr = ___hcr;						\
} while (0)

#define arch_nmi_exit()							\
do {									\
	struct nmi_ctx *___ctx;						\
	u64 ___hcr;							\
									\
	if (!is_kernel_in_hyp_mode())					\
		break;							\
									\
	___ctx = this_cpu_ptr(&nmi_contexts);				\
	___hcr = ___ctx->hcr;						\
	/*								\
	 * Make sure we read ___ctx->hcr before we release		\
	 * ___ctx->cnt as it makes ___ctx->hcr updatable again.		\
	 */								\
	barrier();							\
	___ctx->cnt--;							\
	/*								\
	 * Make sure ___ctx->cnt release is visible before we		\
	 * restore the sysreg. Otherwise a new NMI occurring		\
	 * right after write_sysreg() can be fooled and think		\
	 * we secured things for it.					\
	 */								\
	barrier();							\
	if (!___ctx->cnt && !(___hcr & HCR_TGE))			\
		write_sysreg_hcr(___hcr);				\
} while (0)

static inline void ack_bad_irq(unsigned int irq)
{
	extern unsigned long irq_err_count;
	irq_err_count++;
}

#endif /* __ASM_HARDIRQ_H */
