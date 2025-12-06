/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UFS Host driver for Synopsys Designware Core
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <jpinto@synopsys.com>
 */

#ifndef _UFSHCD_DWC_H
#define _UFSHCD_DWC_H

#include <ufs/ufshcd.h>

/* RMMI Attributes */
#define CBREFCLKCTRL2		0x8132
#define CBCRCTRL		0x811F
#define CBC10DIRECTCONF2	0x810E
#define CBRATESEL		0x8114
#define CBCREGADDRLSB		0x8116
#define CBCREGADDRMSB		0x8117
#define CBCREGWRLSB		0x8118
#define CBCREGWRMSB		0x8119
#define CBCREGRDLSB		0x811A
#define CBCREGRDMSB		0x811B
#define CBCREGRDWRSEL		0x811C

#define CBREFREFCLK_GATE_OVR_EN		BIT(7)

/* M-PHY Attributes */
#define MTX_FSM_STATE		0x41
#define MRX_FSM_STATE		0xC1

/* M-PHY registers */
#define RX_OVRD_IN_1(n)		(0x3006 + ((n) * 0x100))
#define RX_PCS_OUT(n)		(0x300F + ((n) * 0x100))
#define FAST_FLAGS(n)		(0x401C + ((n) * 0x100))
#define RX_AFE_ATT_IDAC(n)	(0x4000 + ((n) * 0x100))
#define RX_AFE_CTLE_IDAC(n)	(0x4001 + ((n) * 0x100))
#define FW_CALIB_CCFG(n)	(0x404D + ((n) * 0x100))

/* Tx/Rx FSM state */
enum rx_fsm_state {
	RX_STATE_DISABLED = 0,
	RX_STATE_HIBERN8 = 1,
	RX_STATE_SLEEP = 2,
	RX_STATE_STALL = 3,
	RX_STATE_LSBURST = 4,
	RX_STATE_HSBURST = 5,
};

enum tx_fsm_state {
	TX_STATE_DISABLED = 0,
	TX_STATE_HIBERN8 = 1,
	TX_STATE_SLEEP = 2,
	TX_STATE_STALL = 3,
	TX_STATE_LSBURST = 4,
	TX_STATE_HSBURST = 5,
};

struct ufshcd_dme_attr_val {
	u32 attr_sel;
	u32 mib_val;
	u8 peer;
};

int ufshcd_dwc_link_startup_notify(struct ufs_hba *hba,
					enum ufs_notify_change_status status);
int ufshcd_dwc_dme_set_attrs(struct ufs_hba *hba,
				const struct ufshcd_dme_attr_val *v, int n);
#endif /* End of Header */
