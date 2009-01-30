#ifndef _ASM_X86_CPUMASK_H
#define _ASM_X86_CPUMASK_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>

#ifdef CONFIG_X86_64

extern cpumask_var_t cpu_callin_mask;
extern cpumask_var_t cpu_callout_mask;
extern cpumask_var_t cpu_initialized_mask;
extern cpumask_var_t cpu_sibling_setup_mask;

extern void setup_cpu_local_masks(void);

#else /* CONFIG_X86_32 */

extern cpumask_t cpu_callin_map;
extern cpumask_t cpu_callout_map;
extern cpumask_t cpu_initialized;
extern cpumask_t cpu_sibling_setup_map;

#define cpu_callin_mask		((struct cpumask *)&cpu_callin_map)
#define cpu_callout_mask	((struct cpumask *)&cpu_callout_map)
#define cpu_initialized_mask	((struct cpumask *)&cpu_initialized)
#define cpu_sibling_setup_mask	((struct cpumask *)&cpu_sibling_setup_map)

static inline void setup_cpu_local_masks(void) { }

#endif /* CONFIG_X86_32 */

#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_CPUMASK_H */
