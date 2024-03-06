/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from arch/arm64/include/asm/hwcap.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */
#ifndef _ASM_RISCV_HWCAP_H
#define _ASM_RISCV_HWCAP_H

#include <uapi/asm/hwcap.h>

#define RISCV_ISA_EXT_a		('a' - 'a')
#define RISCV_ISA_EXT_c		('c' - 'a')
#define RISCV_ISA_EXT_d		('d' - 'a')
#define RISCV_ISA_EXT_f		('f' - 'a')
#define RISCV_ISA_EXT_h		('h' - 'a')
#define RISCV_ISA_EXT_i		('i' - 'a')
#define RISCV_ISA_EXT_m		('m' - 'a')
#define RISCV_ISA_EXT_q		('q' - 'a')
#define RISCV_ISA_EXT_v		('v' - 'a')

/*
 * These macros represent the logical IDs of each multi-letter RISC-V ISA
 * extension and are used in the ISA bitmap. The logical IDs start from
 * RISCV_ISA_EXT_BASE, which allows the 0-25 range to be reserved for single
 * letter extensions. The maximum, RISCV_ISA_EXT_MAX, is defined in order
 * to allocate the bitmap and may be increased when necessary.
 *
 * New extensions should just be added to the bottom, rather than added
 * alphabetically, in order to avoid unnecessary shuffling.
 */
#define RISCV_ISA_EXT_BASE		26

#define RISCV_ISA_EXT_SSCOFPMF		26
#define RISCV_ISA_EXT_SSTC		27
#define RISCV_ISA_EXT_SVINVAL		28
#define RISCV_ISA_EXT_SVPBMT		29
#define RISCV_ISA_EXT_ZBB		30
#define RISCV_ISA_EXT_ZICBOM		31
#define RISCV_ISA_EXT_ZIHINTPAUSE	32
#define RISCV_ISA_EXT_SVNAPOT		33
#define RISCV_ISA_EXT_ZICBOZ		34
#define RISCV_ISA_EXT_SMAIA		35
#define RISCV_ISA_EXT_SSAIA		36
#define RISCV_ISA_EXT_ZBA		37
#define RISCV_ISA_EXT_ZBS		38
#define RISCV_ISA_EXT_ZICNTR		39
#define RISCV_ISA_EXT_ZICSR		40
#define RISCV_ISA_EXT_ZIFENCEI		41
#define RISCV_ISA_EXT_ZIHPM		42
#define RISCV_ISA_EXT_SMSTATEEN		43
#define RISCV_ISA_EXT_ZICOND		44
#define RISCV_ISA_EXT_ZBC		45
#define RISCV_ISA_EXT_ZBKB		46
#define RISCV_ISA_EXT_ZBKC		47
#define RISCV_ISA_EXT_ZBKX		48
#define RISCV_ISA_EXT_ZKND		49
#define RISCV_ISA_EXT_ZKNE		50
#define RISCV_ISA_EXT_ZKNH		51
#define RISCV_ISA_EXT_ZKR		52
#define RISCV_ISA_EXT_ZKSED		53
#define RISCV_ISA_EXT_ZKSH		54
#define RISCV_ISA_EXT_ZKT		55
#define RISCV_ISA_EXT_ZVBB		56
#define RISCV_ISA_EXT_ZVBC		57
#define RISCV_ISA_EXT_ZVKB		58
#define RISCV_ISA_EXT_ZVKG		59
#define RISCV_ISA_EXT_ZVKNED		60
#define RISCV_ISA_EXT_ZVKNHA		61
#define RISCV_ISA_EXT_ZVKNHB		62
#define RISCV_ISA_EXT_ZVKSED		63
#define RISCV_ISA_EXT_ZVKSH		64
#define RISCV_ISA_EXT_ZVKT		65
#define RISCV_ISA_EXT_ZFH		66
#define RISCV_ISA_EXT_ZFHMIN		67
#define RISCV_ISA_EXT_ZIHINTNTL		68
#define RISCV_ISA_EXT_ZVFH		69
#define RISCV_ISA_EXT_ZVFHMIN		70
#define RISCV_ISA_EXT_ZFA		71
#define RISCV_ISA_EXT_ZTSO		72
#define RISCV_ISA_EXT_ZACAS		73

#define RISCV_ISA_EXT_XLINUXENVCFG	127

#define RISCV_ISA_EXT_MAX		128
#define RISCV_ISA_EXT_INVALID		U32_MAX

#ifdef CONFIG_RISCV_M_MODE
#define RISCV_ISA_EXT_SxAIA		RISCV_ISA_EXT_SMAIA
#else
#define RISCV_ISA_EXT_SxAIA		RISCV_ISA_EXT_SSAIA
#endif

#endif /* _ASM_RISCV_HWCAP_H */
