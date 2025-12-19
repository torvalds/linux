/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_SMP_INTERNAL_H
#define __UM_SMP_INTERNAL_H

#if IS_ENABLED(CONFIG_SMP)

void prefill_possible_map(void);

#else /* !CONFIG_SMP */

static inline void prefill_possible_map(void) { }

#endif /* CONFIG_SMP */

extern char cpu_irqstacks[NR_CPUS][THREAD_SIZE] __aligned(THREAD_SIZE);

#endif /* __UM_SMP_INTERNAL_H */
