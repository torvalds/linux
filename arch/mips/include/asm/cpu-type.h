/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle
 * Copyright (C) 2004  Maciej W. Rozycki
 */
#ifndef __ASM_CPU_TYPE_H
#define __ASM_CPU_TYPE_H

#include <linux/smp.h>
#include <linux/compiler.h>

static inline int __pure __get_cpu_type(const int cpu_type)
{
	switch (cpu_type) {
#if defined(CONFIG_SYS_HAS_CPU_LOONGSON2E) || \
    defined(CONFIG_SYS_HAS_CPU_LOONGSON2F)
	case CPU_LOONGSON2EF:
#endif

#ifdef CONFIG_SYS_HAS_CPU_LOONGSON64
	case CPU_LOONGSON64:
#endif

#if defined(CONFIG_SYS_HAS_CPU_LOONGSON1B) || \
    defined(CONFIG_SYS_HAS_CPU_LOONGSON1C)
	case CPU_LOONGSON32:
#endif

#ifdef CONFIG_SYS_HAS_CPU_MIPS32_R1
	case CPU_4KC:
	case CPU_ALCHEMY:
	case CPU_PR4450:
#endif

#if defined(CONFIG_SYS_HAS_CPU_MIPS32_R1) || \
    defined(CONFIG_SYS_HAS_CPU_MIPS32_R2)
	case CPU_4KEC:
	case CPU_XBURST:
#endif

#ifdef CONFIG_SYS_HAS_CPU_MIPS32_R2
	case CPU_4KSC:
	case CPU_24K:
	case CPU_34K:
	case CPU_1004K:
	case CPU_74K:
	case CPU_1074K:
	case CPU_M14KC:
	case CPU_M14KEC:
	case CPU_INTERAPTIV:
	case CPU_PROAPTIV:
#endif

#ifdef CONFIG_SYS_HAS_CPU_MIPS32_R5
	case CPU_M5150:
	case CPU_P5600:
#endif

#if defined(CONFIG_SYS_HAS_CPU_MIPS32_R2) || \
    defined(CONFIG_SYS_HAS_CPU_MIPS32_R5) || \
    defined(CONFIG_SYS_HAS_CPU_MIPS32_R6) || \
    defined(CONFIG_SYS_HAS_CPU_MIPS64_R2) || \
    defined(CONFIG_SYS_HAS_CPU_MIPS64_R5) || \
    defined(CONFIG_SYS_HAS_CPU_MIPS64_R6)
	case CPU_QEMU_GENERIC:
#endif

#ifdef CONFIG_SYS_HAS_CPU_MIPS64_R1
	case CPU_5KC:
	case CPU_5KE:
	case CPU_20KC:
	case CPU_25KF:
	case CPU_SB1:
	case CPU_SB1A:
#endif

#ifdef CONFIG_SYS_HAS_CPU_MIPS64_R2
	/*
	 * All MIPS64 R2 processors have their own special symbols.  That is,
	 * there currently is no pure R2 core
	 */
#endif

#ifdef CONFIG_SYS_HAS_CPU_MIPS32_R6
	case CPU_M6250:
#endif

#ifdef CONFIG_SYS_HAS_CPU_MIPS64_R6
	case CPU_I6400:
	case CPU_I6500:
	case CPU_P6600:
#endif

#ifdef CONFIG_SYS_HAS_CPU_R3000
	case CPU_R2000:
	case CPU_R3000:
	case CPU_R3000A:
	case CPU_R3041:
	case CPU_R3051:
	case CPU_R3052:
	case CPU_R3081:
	case CPU_R3081E:
#endif

#ifdef CONFIG_SYS_HAS_CPU_TX39XX
	case CPU_TX3912:
	case CPU_TX3922:
	case CPU_TX3927:
#endif

#ifdef CONFIG_SYS_HAS_CPU_VR41XX
	case CPU_VR41XX:
	case CPU_VR4111:
	case CPU_VR4121:
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
	case CPU_VR4181:
	case CPU_VR4181A:
#endif

#ifdef CONFIG_SYS_HAS_CPU_R4X00
	case CPU_R4000PC:
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4200:
	case CPU_R4400PC:
	case CPU_R4400SC:
	case CPU_R4400MC:
	case CPU_R4600:
	case CPU_R4700:
	case CPU_R4640:
	case CPU_R4650:
#endif

#ifdef CONFIG_SYS_HAS_CPU_TX49XX
	case CPU_TX49XX:
#endif

#ifdef CONFIG_SYS_HAS_CPU_R5000
	case CPU_R5000:
#endif

#ifdef CONFIG_SYS_HAS_CPU_R5500
	case CPU_R5500:
#endif

#ifdef CONFIG_SYS_HAS_CPU_NEVADA
	case CPU_NEVADA:
#endif

#ifdef CONFIG_SYS_HAS_CPU_R10000
	case CPU_R10000:
	case CPU_R12000:
	case CPU_R14000:
	case CPU_R16000:
#endif
#ifdef CONFIG_SYS_HAS_CPU_RM7000
	case CPU_RM7000:
	case CPU_SR71000:
#endif
#ifdef CONFIG_SYS_HAS_CPU_SB1
	case CPU_SB1:
	case CPU_SB1A:
#endif
#ifdef CONFIG_SYS_HAS_CPU_CAVIUM_OCTEON
	case CPU_CAVIUM_OCTEON:
	case CPU_CAVIUM_OCTEON_PLUS:
	case CPU_CAVIUM_OCTEON2:
	case CPU_CAVIUM_OCTEON3:
#endif

#if defined(CONFIG_SYS_HAS_CPU_BMIPS32_3300) || \
	defined (CONFIG_SYS_HAS_CPU_MIPS32_R1)
	case CPU_BMIPS32:
	case CPU_BMIPS3300:
#endif

#ifdef CONFIG_SYS_HAS_CPU_BMIPS4350
	case CPU_BMIPS4350:
#endif

#ifdef CONFIG_SYS_HAS_CPU_BMIPS4380
	case CPU_BMIPS4380:
#endif

#ifdef CONFIG_SYS_HAS_CPU_BMIPS5000
	case CPU_BMIPS5000:
#endif

#ifdef CONFIG_SYS_HAS_CPU_XLP
	case CPU_XLP:
#endif

#ifdef CONFIG_SYS_HAS_CPU_XLR
	case CPU_XLR:
#endif
		break;
	default:
		unreachable();
	}

	return cpu_type;
}

static inline int __pure current_cpu_type(void)
{
	const int cpu_type = current_cpu_data.cputype;

	return __get_cpu_type(cpu_type);
}

static inline int __pure boot_cpu_type(void)
{
	const int cpu_type = cpu_data[0].cputype;

	return __get_cpu_type(cpu_type);
}

#endif /* __ASM_CPU_TYPE_H */
