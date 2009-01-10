#ifndef _ASM_X86_CPUMASK_H
#define _ASM_X86_CPUMASK_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>

#ifdef CONFIG_X86_64

extern cpumask_var_t cpu_callin_mask;
extern cpumask_var_t cpu_callout_mask;
extern cpumask_var_t cpu_initialized_mask;

#else /* CONFIG_X86_32 */

extern cpumask_t cpu_callin_map;
extern cpumask_t cpu_callout_map;
extern cpumask_t cpu_initialized;

#define cpu_callin_mask		((struct cpumask *)&cpu_callin_map)
#define cpu_callout_mask	((struct cpumask *)&cpu_callout_map)
#define cpu_initialized_mask	((struct cpumask *)&cpu_initialized)

#endif /* CONFIG_X86_32 */

#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_CPUMASK_H */
