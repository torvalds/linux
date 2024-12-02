/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MACH_LOONGSON64_CPUCFG_EMUL_H_
#define _ASM_MACH_LOONGSON64_CPUCFG_EMUL_H_

#include <asm/cpu-info.h>

#ifdef CONFIG_CPU_LOONGSON3_CPUCFG_EMULATION

#include <loongson_regs.h>

#define LOONGSON_FPREV_MASK 0x7

void loongson3_cpucfg_synthesize_data(struct cpuinfo_mips *c);

static inline bool loongson3_cpucfg_emulation_enabled(struct cpuinfo_mips *c)
{
	/* All supported cores have non-zero LOONGSON_CFG1 data. */
	return c->loongson3_cpucfg_data[0] != 0;
}

static inline u32 loongson3_cpucfg_read_synthesized(struct cpuinfo_mips *c,
	__u64 sel)
{
	switch (sel) {
	case LOONGSON_CFG0:
		return c->processor_id;
	case LOONGSON_CFG1:
	case LOONGSON_CFG2:
	case LOONGSON_CFG3:
		return c->loongson3_cpucfg_data[sel - 1];
	case LOONGSON_CFG4:
	case LOONGSON_CFG5:
		/* CPUCFG selects 4 and 5 are related to the input clock
		 * signal.
		 *
		 * Unimplemented for now.
		 */
		return 0;
	case LOONGSON_CFG6:
		/* CPUCFG select 6 is for the undocumented Safe Extension. */
		return 0;
	case LOONGSON_CFG7:
		/* CPUCFG select 7 is for the virtualization extension.
		 * We don't know if the two currently known features are
		 * supported on older cores according to the public
		 * documentation, so leave this at zero.
		 */
		return 0;
	}

	/*
	 * Return 0 for unrecognized CPUCFG selects, which is real hardware
	 * behavior observed on Loongson 3A R4.
	 */
	return 0;
}
#else
static inline void loongson3_cpucfg_synthesize_data(struct cpuinfo_mips *c)
{
}

static inline bool loongson3_cpucfg_emulation_enabled(struct cpuinfo_mips *c)
{
	return false;
}

static inline u32 loongson3_cpucfg_read_synthesized(struct cpuinfo_mips *c,
	__u64 sel)
{
	return 0;
}
#endif

#endif /* _ASM_MACH_LOONGSON64_CPUCFG_EMUL_H_ */
