/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_SMP_H
#define __UM_SMP_H

#if IS_ENABLED(CONFIG_SMP)

#include <linux/cpumask.h>
#include <shared/smp.h>

#define raw_smp_processor_id() uml_curr_cpu()

void arch_smp_send_reschedule(int cpu);

void arch_send_call_function_single_ipi(int cpu);

void arch_send_call_function_ipi_mask(const struct cpumask *mask);

#endif /* CONFIG_SMP */

#endif
