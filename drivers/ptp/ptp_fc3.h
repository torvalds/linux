/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PTP hardware clock driver for the FemtoClock3 family of timing and
 * synchronization devices.
 *
 * Copyright (C) 2023 Integrated Device Technology, Inc., a Renesas Company.
 */
#ifndef PTP_IDTFC3_H
#define PTP_IDTFC3_H

#include <linux/ktime.h>
#include <linux/ptp_clock.h>
#include <linux/regmap.h>

#define FW_FILENAME	"idtfc3.bin"

#define MAX_FFO_PPB	(244000)
#define TDC_GET_PERIOD	(10)

struct idtfc3 {
	struct ptp_clock_info	caps;
	struct ptp_clock	*ptp_clock;
	struct device		*dev;
	/* Mutex to protect operations from being interrupted */
	struct mutex		*lock;
	struct device		*mfd;
	struct regmap		*regmap;
	struct idtfc3_hw_param	hw_param;
	u32			sub_sync_count;
	u32			ns_per_sync;
	int			tdc_offset_sign;
	u64			tdc_apll_freq;
	u32			time_ref_freq;
	u16			fod_n;
	u8			lpf_mode;
	/* Time counter */
	u32			last_counter;
	s64			ns;
	u32			ns_per_counter;
	u32			tc_update_period;
	u32			tc_write_timeout;
	s64			tod_write_overhead;
};

#endif /* PTP_IDTFC3_H */
