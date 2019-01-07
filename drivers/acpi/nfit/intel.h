// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 * Intel specific definitions for NVDIMM Firmware Interface Table - NFIT
 */
#ifndef _NFIT_INTEL_H_
#define _NFIT_INTEL_H_

#define ND_INTEL_SMART 1

#define ND_INTEL_SMART_SHUTDOWN_COUNT_VALID     (1 << 5)
#define ND_INTEL_SMART_SHUTDOWN_VALID           (1 << 10)

struct nd_intel_smart {
	u32 status;
	union {
		struct {
			u32 flags;
			u8 reserved0[4];
			u8 health;
			u8 spares;
			u8 life_used;
			u8 alarm_flags;
			u16 media_temperature;
			u16 ctrl_temperature;
			u32 shutdown_count;
			u8 ait_status;
			u16 pmic_temperature;
			u8 reserved1[8];
			u8 shutdown_state;
			u32 vendor_size;
			u8 vendor_data[92];
		} __packed;
		u8 data[128];
	};
} __packed;

#endif
