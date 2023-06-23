/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_RUNLATCH_H
#define _ASM_POWERPC_RUNLATCH_H

#ifdef CONFIG_PPC64

extern void __ppc64_runlatch_on(void);
extern void __ppc64_runlatch_off(void);

/*
 * We manually hard enable-disable, this is called
 * in the idle loop and we don't want to mess up
 * with soft-disable/enable & interrupt replay.
 */
#define ppc64_runlatch_off()					\
	do {							\
		if (cpu_has_feature(CPU_FTR_CTRL) &&		\
		    test_thread_local_flags(_TLF_RUNLATCH)) {	\
			__hard_irq_disable();			\
			__ppc64_runlatch_off();			\
			if (!(local_paca->irq_happened & PACA_IRQ_HARD_DIS)) \
				__hard_irq_enable();		\
		}      						\
	} while (0)

#define ppc64_runlatch_on()					\
	do {							\
		if (cpu_has_feature(CPU_FTR_CTRL) &&		\
		    !test_thread_local_flags(_TLF_RUNLATCH)) {	\
			__hard_irq_disable();			\
			__ppc64_runlatch_on();			\
			if (!(local_paca->irq_happened & PACA_IRQ_HARD_DIS)) \
				__hard_irq_enable();		\
		}      						\
	} while (0)
#else
#define ppc64_runlatch_on()
#define ppc64_runlatch_off()
#endif /* CONFIG_PPC64 */

#endif /* _ASM_POWERPC_RUNLATCH_H */
