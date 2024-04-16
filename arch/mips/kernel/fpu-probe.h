/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <linux/kernel.h>

#include <asm/cpu.h>
#include <asm/cpu-info.h>

#ifdef CONFIG_MIPS_FP_SUPPORT

extern int mips_fpu_disabled;

int __cpu_has_fpu(void);
void cpu_set_fpu_opts(struct cpuinfo_mips *c);
void cpu_set_nofpu_opts(struct cpuinfo_mips *c);

#else /* !CONFIG_MIPS_FP_SUPPORT */

#define mips_fpu_disabled 1

static inline unsigned long cpu_get_fpu_id(void)
{
	return FPIR_IMP_NONE;
}

static inline int __cpu_has_fpu(void)
{
	return 0;
}

static inline void cpu_set_fpu_opts(struct cpuinfo_mips *c)
{
	/* no-op */
}

static inline void cpu_set_nofpu_opts(struct cpuinfo_mips *c)
{
	/* no-op */
}

#endif /* CONFIG_MIPS_FP_SUPPORT */
