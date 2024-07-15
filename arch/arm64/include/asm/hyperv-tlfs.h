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
 * Group C Features. See the asm-generic version of hyperv-tlfs.h
 * for a description of Feature Groups.
 */

/* Crash MSRs available */
#define HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE	BIT(8)

/* STIMER direct mode is available */
#define HV_STIMER_DIRECT_MODE_AVAILABLE		BIT(13)

/*
 * To support arch-generic code calling hv_set/get_register:
 * - On x86, HV_MSR_ indicates an MSR accessed via rdmsrl/wrmsrl
 * - On ARM, HV_MSR_ indicates a VP register accessed via hypercall
 */
#define HV_MSR_CRASH_P0		(HV_REGISTER_GUEST_CRASH_P0)
#define HV_MSR_CRASH_P1		(HV_REGISTER_GUEST_CRASH_P1)
#define HV_MSR_CRASH_P2		(HV_REGISTER_GUEST_CRASH_P2)
#define HV_MSR_CRASH_P3		(HV_REGISTER_GUEST_CRASH_P3)
#define HV_MSR_CRASH_P4		(HV_REGISTER_GUEST_CRASH_P4)
#define HV_MSR_CRASH_CTL	(HV_REGISTER_GUEST_CRASH_CTL)

#define HV_MSR_VP_INDEX		(HV_REGISTER_VP_INDEX)
#define HV_MSR_TIME_REF_COUNT	(HV_REGISTER_TIME_REF_COUNT)
#define HV_MSR_REFERENCE_TSC	(HV_REGISTER_REFERENCE_TSC)

#define HV_MSR_SINT0		(HV_REGISTER_SINT0)
#define HV_MSR_SCONTROL		(HV_REGISTER_SCONTROL)
#define HV_MSR_SIEFP		(HV_REGISTER_SIEFP)
#define HV_MSR_SIMP		(HV_REGISTER_SIMP)
#define HV_MSR_EOM		(HV_REGISTER_EOM)

#define HV_MSR_STIMER0_CONFIG	(HV_REGISTER_STIMER0_CONFIG)
#define HV_MSR_STIMER0_COUNT	(HV_REGISTER_STIMER0_COUNT)

union hv_msi_entry {
	u64 as_uint64[2];
	struct {
		u64 address;
		u32 data;
		u32 reserved;
	} __packed;
};

#include <asm-generic/hyperv-tlfs.h>

#endif
