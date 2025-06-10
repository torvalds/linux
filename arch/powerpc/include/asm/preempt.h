/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_POWERPC_PREEMPT_H
#define __ASM_POWERPC_PREEMPT_H

#include <asm-generic/preempt.h>

#if defined(CONFIG_PREEMPT_DYNAMIC)
#include <linux/jump_label.h>
DECLARE_STATIC_KEY_TRUE(sk_dynamic_irqentry_exit_cond_resched);
#define need_irq_preemption() \
	(static_branch_unlikely(&sk_dynamic_irqentry_exit_cond_resched))
#else
#define need_irq_preemption()   (IS_ENABLED(CONFIG_PREEMPTION))
#endif

#endif /* __ASM_POWERPC_PREEMPT_H */
