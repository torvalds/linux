/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 2003, 2004 Ralf Baechle
 * Copyright (C) 2004  Maciej W. Rozycki
 */
#ifndef __ASM_CPU_FEATURES_H
#define __ASM_CPU_FEATURES_H

#include <asm/cpu.h>
#include <asm/cpu-info.h>

#define cpu_opt(opt)			(cpu_data[0].options & (opt))
#define cpu_has(feat)			(cpu_data[0].options & BIT_ULL(feat))

#define cpu_has_loongarch		(cpu_has_loongarch32 | cpu_has_loongarch64)
#define cpu_has_loongarch32		(cpu_data[0].isa_level & LOONGARCH_CPU_ISA_32BIT)
#define cpu_has_loongarch64		(cpu_data[0].isa_level & LOONGARCH_CPU_ISA_64BIT)

#ifdef CONFIG_32BIT
# define cpu_has_64bits			(cpu_data[0].isa_level & LOONGARCH_CPU_ISA_64BIT)
# define cpu_vabits			31
# define cpu_pabits			31
#endif

#ifdef CONFIG_64BIT
# define cpu_has_64bits			1
# define cpu_vabits			cpu_data[0].vabits
# define cpu_pabits			cpu_data[0].pabits
# define __NEED_ADDRBITS_PROBE
#endif

/*
 * SMP assumption: Options of CPU 0 are a superset of all processors.
 * This is true for all known LoongArch systems.
 */
#define cpu_has_cpucfg		cpu_opt(LOONGARCH_CPU_CPUCFG)
#define cpu_has_lam		cpu_opt(LOONGARCH_CPU_LAM)
#define cpu_has_ual		cpu_opt(LOONGARCH_CPU_UAL)
#define cpu_has_fpu		cpu_opt(LOONGARCH_CPU_FPU)
#define cpu_has_lsx		cpu_opt(LOONGARCH_CPU_LSX)
#define cpu_has_lasx		cpu_opt(LOONGARCH_CPU_LASX)
#define cpu_has_complex		cpu_opt(LOONGARCH_CPU_COMPLEX)
#define cpu_has_crypto		cpu_opt(LOONGARCH_CPU_CRYPTO)
#define cpu_has_lvz		cpu_opt(LOONGARCH_CPU_LVZ)
#define cpu_has_lbt_x86		cpu_opt(LOONGARCH_CPU_LBT_X86)
#define cpu_has_lbt_arm		cpu_opt(LOONGARCH_CPU_LBT_ARM)
#define cpu_has_lbt_mips	cpu_opt(LOONGARCH_CPU_LBT_MIPS)
#define cpu_has_lbt		(cpu_has_lbt_x86|cpu_has_lbt_arm|cpu_has_lbt_mips)
#define cpu_has_csr		cpu_opt(LOONGARCH_CPU_CSR)
#define cpu_has_tlb		cpu_opt(LOONGARCH_CPU_TLB)
#define cpu_has_watch		cpu_opt(LOONGARCH_CPU_WATCH)
#define cpu_has_vint		cpu_opt(LOONGARCH_CPU_VINT)
#define cpu_has_csripi		cpu_opt(LOONGARCH_CPU_CSRIPI)
#define cpu_has_extioi		cpu_opt(LOONGARCH_CPU_EXTIOI)
#define cpu_has_prefetch	cpu_opt(LOONGARCH_CPU_PREFETCH)
#define cpu_has_pmp		cpu_opt(LOONGARCH_CPU_PMP)
#define cpu_has_perf		cpu_opt(LOONGARCH_CPU_PMP)
#define cpu_has_scalefreq	cpu_opt(LOONGARCH_CPU_SCALEFREQ)
#define cpu_has_flatmode	cpu_opt(LOONGARCH_CPU_FLATMODE)
#define cpu_has_eiodecode	cpu_opt(LOONGARCH_CPU_EIODECODE)
#define cpu_has_guestid		cpu_opt(LOONGARCH_CPU_GUESTID)
#define cpu_has_hypervisor	cpu_opt(LOONGARCH_CPU_HYPERVISOR)


#endif /* __ASM_CPU_FEATURES_H */
