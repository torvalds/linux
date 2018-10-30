/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_BMIPS_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_BMIPS_CPU_FEATURE_OVERRIDES_H

/* Invariants across all BMIPS processors */
#define cpu_has_vtag_icache		0
#define cpu_icache_snoops_remote_store	1

/* Processor ISA compatibility is MIPS32R1 */
#define cpu_has_mips32r1		1
#define cpu_has_mips32r2		0
#define cpu_has_mips64r1		0
#define cpu_has_mips64r2		0

#endif /* __ASM_MACH_BMIPS_CPU_FEATURE_OVERRIDES_H */
