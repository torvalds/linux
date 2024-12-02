/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Texas Instruments Ethernet Switch media-access-controller (MAC) submodule/
 * Ethernet MAC Sliver (CPGMAC_SL) APIs
 *
 * Copyright (C) 2019 Texas Instruments
 *
 */

#ifndef __TI_CPSW_SL_H__
#define __TI_CPSW_SL_H__

#include <linux/device.h>

enum cpsw_sl_regs {
	CPSW_SL_IDVER,
	CPSW_SL_MACCONTROL,
	CPSW_SL_MACSTATUS,
	CPSW_SL_SOFT_RESET,
	CPSW_SL_RX_MAXLEN,
	CPSW_SL_BOFFTEST,
	CPSW_SL_RX_PAUSE,
	CPSW_SL_TX_PAUSE,
	CPSW_SL_EMCONTROL,
	CPSW_SL_RX_PRI_MAP,
	CPSW_SL_TX_GAP,
};

enum {
	CPSW_SL_CTL_FULLDUPLEX = BIT(0), /* Full Duplex mode */
	CPSW_SL_CTL_LOOPBACK = BIT(1), /* Loop Back Mode */
	CPSW_SL_CTL_MTEST = BIT(2), /* Manufacturing Test mode */
	CPSW_SL_CTL_RX_FLOW_EN = BIT(3), /* Receive Flow Control Enable */
	CPSW_SL_CTL_TX_FLOW_EN = BIT(4), /* Transmit Flow Control Enable */
	CPSW_SL_CTL_GMII_EN = BIT(5), /* GMII Enable */
	CPSW_SL_CTL_TX_PACE = BIT(6), /* Transmit Pacing Enable */
	CPSW_SL_CTL_GIG = BIT(7), /* Gigabit Mode */
	CPSW_SL_CTL_XGIG = BIT(8), /* 10 Gigabit Mode */
	CPSW_SL_CTL_TX_SHORT_GAP_EN = BIT(10), /* Transmit Short Gap Enable */
	CPSW_SL_CTL_CMD_IDLE = BIT(11), /* Command Idle */
	CPSW_SL_CTL_CRC_TYPE = BIT(12), /* Port CRC Type */
	CPSW_SL_CTL_XGMII_EN = BIT(13), /* XGMII Enable */
	CPSW_SL_CTL_IFCTL_A = BIT(15), /* Interface Control A */
	CPSW_SL_CTL_IFCTL_B = BIT(16), /* Interface Control B */
	CPSW_SL_CTL_GIG_FORCE = BIT(17), /* Gigabit Mode Force */
	CPSW_SL_CTL_EXT_EN = BIT(18), /* External Control Enable */
	CPSW_SL_CTL_EXT_EN_RX_FLO = BIT(19), /* Ext RX Flow Control Enable */
	CPSW_SL_CTL_EXT_EN_TX_FLO = BIT(20), /* Ext TX Flow Control Enable */
	CPSW_SL_CTL_TX_SG_LIM_EN = BIT(21), /* TXt Short Gap Limit Enable */
	CPSW_SL_CTL_RX_CEF_EN = BIT(22), /* RX Copy Error Frames Enable */
	CPSW_SL_CTL_RX_CSF_EN = BIT(23), /* RX Copy Short Frames Enable */
	CPSW_SL_CTL_RX_CMF_EN = BIT(24), /* RX Copy MAC Control Frames Enable */
	CPSW_SL_CTL_EXT_EN_XGIG = BIT(25),  /* Ext XGIG Control En, k3 only */

	CPSW_SL_CTL_FUNCS_COUNT
};

struct cpsw_sl;

struct cpsw_sl *cpsw_sl_get(const char *device_id, struct device *dev,
			    void __iomem *sl_base);

void cpsw_sl_reset(struct cpsw_sl *sl, unsigned long tmo);

u32 cpsw_sl_ctl_set(struct cpsw_sl *sl, u32 ctl_funcs);
u32 cpsw_sl_ctl_clr(struct cpsw_sl *sl, u32 ctl_funcs);
void cpsw_sl_ctl_reset(struct cpsw_sl *sl);
int cpsw_sl_wait_for_idle(struct cpsw_sl *sl, unsigned long tmo);

u32 cpsw_sl_reg_read(struct cpsw_sl *sl, enum cpsw_sl_regs reg);
void cpsw_sl_reg_write(struct cpsw_sl *sl, enum cpsw_sl_regs reg, u32 val);

#endif /* __TI_CPSW_SL_H__ */
