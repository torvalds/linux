/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DP_AUX_REGS_H__
#define __INTEL_DP_AUX_REGS_H__

#include "intel_display_reg_defs.h"

/*
 * The aux channel provides a way to talk to the signal sink for DDC etc. Max
 * packet size supported is 20 bytes in each direction, hence the 5 fixed data
 * registers
 */
#define _DPA_AUX_CH_CTL		(DISPLAY_MMIO_BASE(dev_priv) + 0x64010)
#define _DPA_AUX_CH_DATA1	(DISPLAY_MMIO_BASE(dev_priv) + 0x64014)

#define _DPB_AUX_CH_CTL		(DISPLAY_MMIO_BASE(dev_priv) + 0x64110)
#define _DPB_AUX_CH_DATA1	(DISPLAY_MMIO_BASE(dev_priv) + 0x64114)

#define DP_AUX_CH_CTL(aux_ch)	_MMIO_PORT(aux_ch, _DPA_AUX_CH_CTL, _DPB_AUX_CH_CTL)
#define DP_AUX_CH_DATA(aux_ch, i)	_MMIO(_PORT(aux_ch, _DPA_AUX_CH_DATA1, _DPB_AUX_CH_DATA1) + (i) * 4) /* 5 registers */

#define _XELPDP_USBC1_AUX_CH_CTL	0x16F210
#define _XELPDP_USBC2_AUX_CH_CTL	0x16F410
#define _XELPDP_USBC3_AUX_CH_CTL	0x16F610
#define _XELPDP_USBC4_AUX_CH_CTL	0x16F810

#define XELPDP_DP_AUX_CH_CTL(aux_ch)		_MMIO(_PICK(aux_ch, \
						       _DPA_AUX_CH_CTL, \
						       _DPB_AUX_CH_CTL, \
						       0, /* port/aux_ch C is non-existent */ \
						       _XELPDP_USBC1_AUX_CH_CTL, \
						       _XELPDP_USBC2_AUX_CH_CTL, \
						       _XELPDP_USBC3_AUX_CH_CTL, \
						       _XELPDP_USBC4_AUX_CH_CTL))

#define _XELPDP_USBC1_AUX_CH_DATA1      0x16F214
#define _XELPDP_USBC2_AUX_CH_DATA1      0x16F414
#define _XELPDP_USBC3_AUX_CH_DATA1      0x16F614
#define _XELPDP_USBC4_AUX_CH_DATA1      0x16F814

#define XELPDP_DP_AUX_CH_DATA(aux_ch, i)	_MMIO(_PICK(aux_ch, \
						       _DPA_AUX_CH_DATA1, \
						       _DPB_AUX_CH_DATA1, \
						       0, /* port/aux_ch C is non-existent */ \
						       _XELPDP_USBC1_AUX_CH_DATA1, \
						       _XELPDP_USBC2_AUX_CH_DATA1, \
						       _XELPDP_USBC3_AUX_CH_DATA1, \
						       _XELPDP_USBC4_AUX_CH_DATA1) + (i) * 4)

#define   DP_AUX_CH_CTL_SEND_BUSY	    (1 << 31)
#define   DP_AUX_CH_CTL_DONE		    (1 << 30)
#define   DP_AUX_CH_CTL_INTERRUPT	    (1 << 29)
#define   DP_AUX_CH_CTL_TIME_OUT_ERROR	    (1 << 28)
#define   DP_AUX_CH_CTL_TIME_OUT_400us	    (0 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_600us	    (1 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_800us	    (2 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_MAX	    (3 << 26) /* Varies per platform */
#define   DP_AUX_CH_CTL_TIME_OUT_MASK	    (3 << 26)
#define   DP_AUX_CH_CTL_RECEIVE_ERROR	    (1 << 25)
#define   DP_AUX_CH_CTL_MESSAGE_SIZE_MASK    (0x1f << 20)
#define   DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT   20
#define   XELPDP_DP_AUX_CH_CTL_POWER_REQUEST REG_BIT(19)
#define   XELPDP_DP_AUX_CH_CTL_POWER_STATUS  REG_BIT(18)
#define   DP_AUX_CH_CTL_PRECHARGE_2US_MASK   (0xf << 16)
#define   DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT  16
#define   DP_AUX_CH_CTL_AUX_AKSV_SELECT	    (1 << 15)
#define   DP_AUX_CH_CTL_MANCHESTER_TEST	    (1 << 14)
#define   DP_AUX_CH_CTL_SYNC_TEST	    (1 << 13)
#define   DP_AUX_CH_CTL_DEGLITCH_TEST	    (1 << 12)
#define   DP_AUX_CH_CTL_PRECHARGE_TEST	    (1 << 11)
#define   DP_AUX_CH_CTL_BIT_CLOCK_2X_MASK    (0x7ff)
#define   DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT   0
#define   DP_AUX_CH_CTL_PSR_DATA_AUX_REG_SKL	(1 << 14)
#define   DP_AUX_CH_CTL_FS_DATA_AUX_REG_SKL	(1 << 13)
#define   DP_AUX_CH_CTL_GTC_DATA_AUX_REG_SKL	(1 << 12)
#define   DP_AUX_CH_CTL_TBT_IO			(1 << 11)
#define   DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL_MASK (0x1f << 5)
#define   DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(c) (((c) - 1) << 5)
#define   DP_AUX_CH_CTL_SYNC_PULSE_SKL(c)   ((c) - 1)

#endif /* __INTEL_DP_AUX_REGS_H__ */
