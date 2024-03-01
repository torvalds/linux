/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024,  Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_BWPROF_H
#define _QCOM_BWPROF_H

#define HW_HZ				19200000
#define HW_SAMPLE_MS			50
#define HW_SAMPLE_TICKS			mult_frac(HW_SAMPLE_MS, HW_HZ, \
							MSEC_PER_SEC)

#define BWMON_EN(m)			((m)->base + 0x10)
#define BWMON_CLEAR(m)			((m)->base + 0x14)
#define BWMON_SW(m)			((m)->base + 0x20)
#define BWMON_THRES_HI(m)		((m)->base + 0x24)
#define BWMON_THRES_MED(m)		((m)->base + 0x28)
#define BWMON_THRES_LO(m)		((m)->base + 0x2C)
#define BWMON_ZONE_ACTIONS(m)		((m)->base + 0x30)
#define BWMON_ZONE_CNT_THRES(m)		((m)->base + 0x34)
#define BWMON_BYTE_CNT(m)		((m)->base + 0x38)
#define BWMON_ZONE_CNT(m)		((m)->base + 0x40)
#define BWMON_ZONE1_MAX_BYTE_COUNT(m)	((m)->base + 0x48)
#define DDR_FREQ(m)			((m)->memfreq_base + 0x00)

#endif /* _QCOM_BWPROF_H */
