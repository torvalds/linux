/*
 * arch/arm64/include/asm/cpucaps.h
 *
 * Copyright (C) 2016 ARM Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_CPUCAPS_H
#define __ASM_CPUCAPS_H

#define ARM64_WORKAROUND_CLEAN_CACHE		0
#define ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE	1
#define ARM64_WORKAROUND_845719			2
#define ARM64_HAS_SYSREG_GIC_CPUIF		3
#define ARM64_HAS_PAN				4
#define ARM64_HAS_LSE_ATOMICS			5
#define ARM64_WORKAROUND_CAVIUM_23154		6
#define ARM64_WORKAROUND_834220			7
#define ARM64_HAS_NO_HW_PREFETCH		8
#define ARM64_HAS_UAO				9
#define ARM64_ALT_PAN_NOT_UAO			10
#define ARM64_HAS_VIRT_HOST_EXTN		11
#define ARM64_WORKAROUND_CAVIUM_27456		12
#define ARM64_HAS_32BIT_EL0			13
#define ARM64_HYP_OFFSET_LOW			14
#define ARM64_MISMATCHED_CACHE_LINE_SIZE	15
#define ARM64_HAS_NO_FPSIMD			16
#define ARM64_WORKAROUND_REPEAT_TLBI		17
#define ARM64_WORKAROUND_QCOM_FALKOR_E1003	18
#define ARM64_WORKAROUND_858921			19

#define ARM64_NCAPS				20

#endif /* __ASM_CPUCAPS_H */
