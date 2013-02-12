#ifndef __ASMARM_ARCH_SCU_H
#define __ASMARM_ARCH_SCU_H

#define SCU_PM_NORMAL	0
#define SCU_PM_DORMANT	2
#define SCU_PM_POWEROFF	3

#ifndef __ASSEMBLER__
unsigned int scu_get_core_count(void __iomem *);
int scu_power_mode(void __iomem *, unsigned int);

#ifdef CONFIG_SMP
void scu_enable(void __iomem *scu_base);
#else
static inline void scu_enable(void __iomem *scu_base) {}
#endif

#endif

#endif
