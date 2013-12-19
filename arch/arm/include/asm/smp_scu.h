#ifndef __ASMARM_ARCH_SCU_H
#define __ASMARM_ARCH_SCU_H

#define SCU_PM_NORMAL	0
#define SCU_PM_DORMANT	2
#define SCU_PM_POWEROFF	3

#ifndef __ASSEMBLER__

#include <asm/cputype.h>

static inline bool scu_a9_has_base(void)
{
	return read_cpuid_part_number() == ARM_CPU_PART_CORTEX_A9;
}

static inline unsigned long scu_a9_get_base(void)
{
	unsigned long pa;

	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (pa));

	return pa;
}

#ifdef CONFIG_HAVE_ARM_SCU
unsigned int scu_get_core_count(void __iomem *);
int scu_power_mode(void __iomem *, unsigned int);
#else
static inline unsigned int scu_get_core_count(void __iomem *scu_base)
{
	return 0;
}
static inline int scu_power_mode(void __iomem *scu_base, unsigned int mode)
{
	return -EINVAL;
}
#endif

#if defined(CONFIG_SMP) && defined(CONFIG_HAVE_ARM_SCU)
void scu_enable(void __iomem *scu_base);
#else
static inline void scu_enable(void __iomem *scu_base) {}
#endif

#endif

#endif
