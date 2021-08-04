/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file contains definitions from the Hyper-V Hypervisor Top-Level
 * Functional Specification (TLFS):
 * https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/reference/tlfs
 *
 * Copyright (C) 2021, Microsoft, Inc.
 *
 * Author : Michael Kelley <mikelley@microsoft.com>
 */

#ifndef _ASM_HYPERV_TLFS_H
#define _ASM_HYPERV_TLFS_H

#include <linux/types.h>

/*
 * All data structures defined in the TLFS that are shared between Hyper-V
 * and a guest VM use Little Endian byte ordering.  This matches the default
 * byte ordering of Linux running on ARM64, so no special handling is required.
 */

/*
 * These Hyper-V registers provide information equivalent to the CPUID
 * instruction on x86/x64.
 */
#define HV_REGISTER_HYPERVISOR_VERSION		0x00000100 /*CPUID 0x40000002 */
#define HV_REGISTER_FEATURES			0x00000200 /*CPUID 0x40000003 */
#define HV_REGISTER_ENLIGHTENMENTS		0x00000201 /*CPUID 0x40000004 */

/*
 * Group C Features. See the asm-generic version of hyperv-tlfs.h
 * for a description of Feature Groups.
 */

/* Crash MSRs available */
#define HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE	BIT(8)

/* STIMER direct mode is available */
#define HV_STIMER_DIRECT_MODE_AVAILABLE		BIT(13)

/*
 * Synthetic register definitions equivalent to MSRs on x86/x64
 */
#define HV_REGISTER_CRASH_P0		0x00000210
#define HV_REGISTER_CRASH_P1		0x00000211
#define HV_REGISTER_CRASH_P2		0x00000212
#define HV_REGISTER_CRASH_P3		0x00000213
#define HV_REGISTER_CRASH_P4		0x00000214
#define HV_REGISTER_CRASH_CTL		0x00000215

#define HV_REGISTER_GUEST_OSID		0x00090002
#define HV_REGISTER_VP_INDEX		0x00090003
#define HV_REGISTER_TIME_REF_COUNT	0x00090004
#define HV_REGISTER_REFERENCE_TSC	0x00090017

#define HV_REGISTER_SINT0		0x000A0000
#define HV_REGISTER_SCONTROL		0x000A0010
#define HV_REGISTER_SIEFP		0x000A0012
#define HV_REGISTER_SIMP		0x000A0013
#define HV_REGISTER_EOM			0x000A0014

#define HV_REGISTER_STIMER0_CONFIG	0x000B0000
#define HV_REGISTER_STIMER0_COUNT	0x000B0001

#include <asm-generic/hyperv-tlfs.h>

#endif
