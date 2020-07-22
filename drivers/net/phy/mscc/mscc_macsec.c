// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Driver for Microsemi VSC85xx PHYs
 *
 * Author: Nagaraju Lakkaraju
 * License: Dual MIT/GPL
 * Copyright (c) 2016 Microsemi Corporation
 */

#include <linux/phy.h>
#include <dt-bindings/net/mscc-phy-vsc8531.h>

#include <crypto/skcipher.h>

#include <net/macsec.h>

#include "mscc.h"
#include "mscc_mac.h"
#include "mscc_macsec.h"
#include "mscc_fc_buffer.h"

static u32 vsc8584_macsec_phy_read(struct phy_device *phydev,
				   enum macsec_bank bank, u32 reg)
{
	u32 val, val_l = 0, val_h = 0;
	unsigned long deadline;
	int rc;

	rc = phy_select_page(phydev, MSCC_PHY_PAGE_MACSEC);
	if (rc < 0)
		goto failed;

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_20,
		    MSCC_PHY_MACSEC_20_TARGET(bank >> 2));

	if (bank >> 2 == 0x1)
		/* non-MACsec access */
		bank &= 0x3;
	else
		bank = 0;

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_19,
		    MSCC_PHY_MACSEC_19_CMD | MSCC_PHY_MACSEC_19_READ |
		    MSCC_PHY_MACSEC_19_REG_ADDR(reg) |
		    MSCC_PHY_MACSEC_19_TARGET(bank));

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		val = __phy_read(phydev, MSCC_EXT_PAGE_MACSEC_19);
	} while (time_before(jiffies, deadline) && !(val & MSCC_PHY_MACSEC_19_CMD));

	val_l = __phy_read(phydev, MSCC_EXT_PAGE_MACSEC_17);
	val_h = __phy_read(phydev, MSCC_EXT_PAGE_MACSEC_18);

failed:
	phy_restore_page(phydev, rc, rc);

	return (val_h << 16) | val_l;
}

static void vsc8584_macsec_phy_write(struct phy_device *phydev,
				     enum macsec_bank bank, u32 reg, u32 val)
{
	unsigned long deadline;
	int rc;

	rc = phy_select_page(phydev, MSCC_PHY_PAGE_MACSEC);
	if (rc < 0)
		goto failed;

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_20,
		    MSCC_PHY_MACSEC_20_TARGET(bank >> 2));

	if ((bank >> 2 == 0x1) || (bank >> 2 == 0x3))
		bank &= 0x3;
	else
		/* MACsec access */
		bank = 0;

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_17, (u16)val);
	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_18, (u16)(val >> 16));

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_19,
		    MSCC_PHY_MACSEC_19_CMD | MSCC_PHY_MACSEC_19_REG_ADDR(reg) |
		    MSCC_PHY_MACSEC_19_TARGET(bank));

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		val = __phy_read(phydev, MSCC_EXT_PAGE_MACSEC_19);
	} while (time_before(jiffies, deadline) && !(val & MSCC_PHY_MACSEC_19_CMD));

failed:
	phy_restore_page(phydev, rc, rc);
}

static void vsc8584_macsec_classification(struct phy_device *phydev,
					  enum macsec_bank bank)
{
	/* enable VLAN tag parsing */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_CP_TAG,
				 MSCC_MS_SAM_CP_TAG_PARSE_STAG |
				 MSCC_MS_SAM_CP_TAG_PARSE_QTAG |
				 MSCC_MS_SAM_CP_TAG_PARSE_QINQ);
}

static void vsc8584_macsec_flow_default_action(struct phy_device *phydev,
					       enum macsec_bank bank,
					       bool block)
{
	u32 port = (bank == MACSEC_INGR) ?
		    MSCC_MS_PORT_UNCONTROLLED : MSCC_MS_PORT_COMMON;
	u32 action = MSCC_MS_FLOW_BYPASS;

	if (block)
		action = MSCC_MS_FLOW_DROP;

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_NM_FLOW_NCP,
				 /* MACsec untagged */
				 MSCC_MS_SAM_NM_FLOW_NCP_UNTAGGED_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_NCP_UNTAGGED_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_NCP_UNTAGGED_DEST_PORT(port) |
				 /* MACsec tagged */
				 MSCC_MS_SAM_NM_FLOW_NCP_TAGGED_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_NCP_TAGGED_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_NCP_TAGGED_DEST_PORT(port) |
				 /* Bad tag */
				 MSCC_MS_SAM_NM_FLOW_NCP_BADTAG_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_NCP_BADTAG_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_NCP_BADTAG_DEST_PORT(port) |
				 /* Kay tag */
				 MSCC_MS_SAM_NM_FLOW_NCP_KAY_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_NCP_KAY_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_NCP_KAY_DEST_PORT(port));
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_NM_FLOW_CP,
				 /* MACsec untagged */
				 MSCC_MS_SAM_NM_FLOW_NCP_UNTAGGED_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_CP_UNTAGGED_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_CP_UNTAGGED_DEST_PORT(port) |
				 /* MACsec tagged */
				 MSCC_MS_SAM_NM_FLOW_NCP_TAGGED_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_CP_TAGGED_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_CP_TAGGED_DEST_PORT(port) |
				 /* Bad tag */
				 MSCC_MS_SAM_NM_FLOW_NCP_BADTAG_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_CP_BADTAG_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_CP_BADTAG_DEST_PORT(port) |
				 /* Kay tag */
				 MSCC_MS_SAM_NM_FLOW_NCP_KAY_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_CP_KAY_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_CP_KAY_DEST_PORT(port));
}

static void vsc8584_macsec_integrity_checks(struct phy_device *phydev,
					    enum macsec_bank bank)
{
	u32 val;

	if (bank != MACSEC_INGR)
		return;

	/* Set default rules to pass unmatched frames */
	val = vsc8584_macsec_phy_read(phydev, bank,
				      MSCC_MS_PARAMS2_IG_CC_CONTROL);
	val |= MSCC_MS_PARAMS2_IG_CC_CONTROL_NON_MATCH_CTRL_ACT |
	       MSCC_MS_PARAMS2_IG_CC_CONTROL_NON_MATCH_ACT;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_PARAMS2_IG_CC_CONTROL,
				 val);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_PARAMS2_IG_CP_TAG,
				 MSCC_MS_PARAMS2_IG_CP_TAG_PARSE_STAG |
				 MSCC_MS_PARAMS2_IG_CP_TAG_PARSE_QTAG |
				 MSCC_MS_PARAMS2_IG_CP_TAG_PARSE_QINQ);
}

static void vsc8584_macsec_block_init(struct phy_device *phydev,
				      enum macsec_bank bank)
{
	u32 val;
	int i;

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_ENA_CFG,
				 MSCC_MS_ENA_CFG_SW_RST |
				 MSCC_MS_ENA_CFG_MACSEC_BYPASS_ENA);

	/* Set the MACsec block out of s/w reset and enable clocks */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_ENA_CFG,
				 MSCC_MS_ENA_CFG_CLK_ENA);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_STATUS_CONTEXT_CTRL,
				 bank == MACSEC_INGR ? 0xe5880214 : 0xe5880218);
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_MISC_CONTROL,
				 MSCC_MS_MISC_CONTROL_MC_LATENCY_FIX(bank == MACSEC_INGR ? 57 : 40) |
				 MSCC_MS_MISC_CONTROL_XFORM_REC_SIZE(bank == MACSEC_INGR ? 1 : 2));

	/* Clear the counters */
	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_COUNT_CONTROL);
	val |= MSCC_MS_COUNT_CONTROL_AUTO_CNTR_RESET;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_COUNT_CONTROL, val);

	/* Enable octet increment mode */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_PP_CTRL,
				 MSCC_MS_PP_CTRL_MACSEC_OCTET_INCR_MODE);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_BLOCK_CTX_UPDATE, 0x3);

	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_COUNT_CONTROL);
	val |= MSCC_MS_COUNT_CONTROL_RESET_ALL;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_COUNT_CONTROL, val);

	/* Set the MTU */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_NON_VLAN_MTU_CHECK,
				 MSCC_MS_NON_VLAN_MTU_CHECK_NV_MTU_COMPARE(32761) |
				 MSCC_MS_NON_VLAN_MTU_CHECK_NV_MTU_COMP_DROP);

	for (i = 0; i < 8; i++)
		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_VLAN_MTU_CHECK(i),
					 MSCC_MS_VLAN_MTU_CHECK_MTU_COMPARE(32761) |
					 MSCC_MS_VLAN_MTU_CHECK_MTU_COMP_DROP);

	if (bank == MACSEC_EGR) {
		val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_INTR_CTRL_STATUS);
		val &= ~MSCC_MS_INTR_CTRL_STATUS_INTR_ENABLE_M;
		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_INTR_CTRL_STATUS, val);

		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_FC_CFG,
					 MSCC_MS_FC_CFG_FCBUF_ENA |
					 MSCC_MS_FC_CFG_LOW_THRESH(0x1) |
					 MSCC_MS_FC_CFG_HIGH_THRESH(0x4) |
					 MSCC_MS_FC_CFG_LOW_BYTES_VAL(0x4) |
					 MSCC_MS_FC_CFG_HIGH_BYTES_VAL(0x6));
	}

	vsc8584_macsec_classification(phydev, bank);
	vsc8584_macsec_flow_default_action(phydev, bank, false);
	vsc8584_macsec_integrity_checks(phydev, bank);

	/* Enable the MACsec block */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_ENA_CFG,
				 MSCC_MS_ENA_CFG_CLK_ENA |
				 MSCC_MS_ENA_CFG_MACSEC_ENA |
				 MSCC_MS_ENA_CFG_MACSEC_SPEED_MODE(0x5));
}

static void vsc8584_macsec_mac_init(struct phy_device *phydev,
				    enum macsec_bank bank)
{
	u32 val;
	int i;

	/* Clear host & line stats */
	for (i = 0; i < 36; i++)
		vsc8584_macsec_phy_write(phydev, bank, 0x1c + i, 0);

	val = vsc8584_macsec_phy_read(phydev, bank,
				      MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL);
	val &= ~MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_PAUSE_MODE_M;
	val |= MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_PAUSE_MODE(2) |
	       MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_PAUSE_VALUE(0xffff);
	vsc8584_macsec_phy_write(phydev, bank,
				 MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL, val);

	val = vsc8584_macsec_phy_read(phydev, bank,
				      MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_2);
	val |= 0xffff;
	vsc8584_macsec_phy_write(phydev, bank,
				 MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_2, val);

	val = vsc8584_macsec_phy_read(phydev, bank,
				      MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL);
	if (bank == HOST_MAC)
		val |= MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_TIMER_ENA |
		       MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_FRAME_DROP_ENA;
	else
		val |= MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_REACT_ENA |
		       MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_FRAME_DROP_ENA |
		       MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_MODE |
		       MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_EARLY_PAUSE_DETECT_ENA;
	vsc8584_macsec_phy_write(phydev, bank,
				 MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL, val);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_PKTINF_CFG,
				 MSCC_MAC_CFG_PKTINF_CFG_STRIP_FCS_ENA |
				 MSCC_MAC_CFG_PKTINF_CFG_INSERT_FCS_ENA |
				 MSCC_MAC_CFG_PKTINF_CFG_LPI_RELAY_ENA |
				 MSCC_MAC_CFG_PKTINF_CFG_STRIP_PREAMBLE_ENA |
				 MSCC_MAC_CFG_PKTINF_CFG_INSERT_PREAMBLE_ENA |
				 (bank == HOST_MAC ?
				  MSCC_MAC_CFG_PKTINF_CFG_ENABLE_TX_PADDING : 0));

	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MAC_CFG_MODE_CFG);
	val &= ~MSCC_MAC_CFG_MODE_CFG_DISABLE_DIC;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_MODE_CFG, val);

	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MAC_CFG_MAXLEN_CFG);
	val &= ~MSCC_MAC_CFG_MAXLEN_CFG_MAX_LEN_M;
	val |= MSCC_MAC_CFG_MAXLEN_CFG_MAX_LEN(10240);
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_MAXLEN_CFG, val);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_ADV_CHK_CFG,
				 MSCC_MAC_CFG_ADV_CHK_CFG_SFD_CHK_ENA |
				 MSCC_MAC_CFG_ADV_CHK_CFG_PRM_CHK_ENA |
				 MSCC_MAC_CFG_ADV_CHK_CFG_OOR_ERR_ENA |
				 MSCC_MAC_CFG_ADV_CHK_CFG_INR_ERR_ENA);

	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MAC_CFG_LFS_CFG);
	val &= ~MSCC_MAC_CFG_LFS_CFG_LFS_MODE_ENA;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_LFS_CFG, val);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_ENA_CFG,
				 MSCC_MAC_CFG_ENA_CFG_RX_CLK_ENA |
				 MSCC_MAC_CFG_ENA_CFG_TX_CLK_ENA |
				 MSCC_MAC_CFG_ENA_CFG_RX_ENA |
				 MSCC_MAC_CFG_ENA_CFG_TX_ENA);
}

/* Must be called with mdio_lock taken */
static int __vsc8584_macsec_init(struct phy_device *phydev)
{
	struct vsc8531_private *priv = phydev->priv;
	enum macsec_bank proc_bank;
	u32 val;

	vsc8584_macsec_block_init(phydev, MACSEC_INGR);
	vsc8584_macsec_block_init(phydev, MACSEC_EGR);
	vsc8584_macsec_mac_init(phydev, HOST_MAC);
	vsc8584_macsec_mac_init(phydev, LINE_MAC);

	vsc8584_macsec_phy_write(phydev, FC_BUFFER,
				 MSCC_FCBUF_FC_READ_THRESH_CFG,
				 MSCC_FCBUF_FC_READ_THRESH_CFG_TX_THRESH(4) |
				 MSCC_FCBUF_FC_READ_THRESH_CFG_RX_THRESH(5));

	val = vsc8584_macsec_phy_read(phydev, FC_BUFFER, MSCC_FCBUF_MODE_CFG);
	val |= MSCC_FCBUF_MODE_CFG_PAUSE_GEN_ENA |
	       MSCC_FCBUF_MODE_CFG_RX_PPM_RATE_ADAPT_ENA |
	       MSCC_FCBUF_MODE_CFG_TX_PPM_RATE_ADAPT_ENA;
	vsc8584_macsec_phy_write(phydev, FC_BUFFER, MSCC_FCBUF_MODE_CFG, val);

	vsc8584_macsec_phy_write(phydev, FC_BUFFER, MSCC_FCBUF_PPM_RATE_ADAPT_THRESH_CFG,
				 MSCC_FCBUF_PPM_RATE_ADAPT_THRESH_CFG_TX_THRESH(8) |
				 MSCC_FCBUF_PPM_RATE_ADAPT_THRESH_CFG_TX_OFFSET(9));

	val = vsc8584_macsec_phy_read(phydev, FC_BUFFER,
				      MSCC_FCBUF_TX_DATA_QUEUE_CFG);
	val &= ~(MSCC_FCBUF_TX_DATA_QUEUE_CFG_START_M |
		 MSCC_FCBUF_TX_DATA_QUEUE_CFG_END_M);
	val |= MSCC_FCBUF_TX_DATA_QUEUE_CFG_START(0) |
		MSCC_FCBUF_TX_DATA_QUEUE_CFG_END(5119);
	vsc8584_macsec_phy_write(phydev, FC_BUFFER,
				 MSCC_FCBUF_TX_DATA_QUEUE_CFG, val);

	val = vsc8584_macsec_phy_read(phydev, FC_BUFFER, MSCC_FCBUF_ENA_CFG);
	val |= MSCC_FCBUF_ENA_CFG_TX_ENA | MSCC_FCBUF_ENA_CFG_RX_ENA;
	vsc8584_macsec_phy_write(phydev, FC_BUFFER, MSCC_FCBUF_ENA_CFG, val);

	proc_bank = (priv->addr < 2) ? PROC_0 : PROC_2;

	val = vsc8584_macsec_phy_read(phydev, proc_bank,
				      MSCC_PROC_IP_1588_TOP_CFG_STAT_MODE_CTL);
	val &= ~MSCC_PROC_IP_1588_TOP_CFG_STAT_MODE_CTL_PROTOCOL_MODE_M;
	val |= MSCC_PROC_IP_1588_TOP_CFG_STAT_MODE_CTL_PROTOCOL_MODE(4);
	vsc8584_macsec_phy_write(phydev, proc_bank,
				 MSCC_PROC_IP_1588_TOP_CFG_STAT_MODE_CTL, val);

	return 0;
}

static void vsc8584_macsec_flow(struct phy_device *phydev,
				struct macsec_flow *flow)
{
	struct vsc8531_private *priv = phydev->priv;
	enum macsec_bank bank = flow->bank;
	u32 val, match = 0, mask = 0, action = 0, idx = flow->index;

	if (flow->match.tagged)
		match |= MSCC_MS_SAM_MISC_MATCH_TAGGED;
	if (flow->match.untagged)
		match |= MSCC_MS_SAM_MISC_MATCH_UNTAGGED;

	if (bank == MACSEC_INGR && flow->assoc_num >= 0) {
		match |= MSCC_MS_SAM_MISC_MATCH_AN(flow->assoc_num);
		mask |= MSCC_MS_SAM_MASK_AN_MASK(0x3);
	}

	if (bank == MACSEC_INGR && flow->match.sci && flow->rx_sa->sc->sci) {
		match |= MSCC_MS_SAM_MISC_MATCH_TCI(BIT(3));
		mask |= MSCC_MS_SAM_MASK_TCI_MASK(BIT(3)) |
			MSCC_MS_SAM_MASK_SCI_MASK;

		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MATCH_SCI_LO(idx),
					 lower_32_bits(flow->rx_sa->sc->sci));
		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MATCH_SCI_HI(idx),
					 upper_32_bits(flow->rx_sa->sc->sci));
	}

	if (flow->match.etype) {
		mask |= MSCC_MS_SAM_MASK_MAC_ETYPE_MASK;

		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MAC_SA_MATCH_HI(idx),
					 MSCC_MS_SAM_MAC_SA_MATCH_HI_ETYPE(htons(flow->etype)));
	}

	match |= MSCC_MS_SAM_MISC_MATCH_PRIORITY(flow->priority);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MISC_MATCH(idx), match);
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MASK(idx), mask);

	/* Action for matching packets */
	if (flow->action.drop)
		action = MSCC_MS_FLOW_DROP;
	else if (flow->action.bypass || flow->port == MSCC_MS_PORT_UNCONTROLLED)
		action = MSCC_MS_FLOW_BYPASS;
	else
		action = (bank == MACSEC_INGR) ?
			 MSCC_MS_FLOW_INGRESS : MSCC_MS_FLOW_EGRESS;

	val = MSCC_MS_SAM_FLOW_CTRL_FLOW_TYPE(action) |
	      MSCC_MS_SAM_FLOW_CTRL_DROP_ACTION(MSCC_MS_ACTION_DROP) |
	      MSCC_MS_SAM_FLOW_CTRL_DEST_PORT(flow->port);

	if (action == MSCC_MS_FLOW_BYPASS)
		goto write_ctrl;

	if (bank == MACSEC_INGR) {
		if (priv->secy->replay_protect)
			val |= MSCC_MS_SAM_FLOW_CTRL_REPLAY_PROTECT;
		if (priv->secy->validate_frames == MACSEC_VALIDATE_STRICT)
			val |= MSCC_MS_SAM_FLOW_CTRL_VALIDATE_FRAMES(MSCC_MS_VALIDATE_STRICT);
		else if (priv->secy->validate_frames == MACSEC_VALIDATE_CHECK)
			val |= MSCC_MS_SAM_FLOW_CTRL_VALIDATE_FRAMES(MSCC_MS_VALIDATE_CHECK);
	} else if (bank == MACSEC_EGR) {
		if (priv->secy->protect_frames)
			val |= MSCC_MS_SAM_FLOW_CTRL_PROTECT_FRAME;
		if (priv->secy->tx_sc.encrypt)
			val |= MSCC_MS_SAM_FLOW_CTRL_CONF_PROTECT;
		if (priv->secy->tx_sc.send_sci)
			val |= MSCC_MS_SAM_FLOW_CTRL_INCLUDE_SCI;
	}

write_ctrl:
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx), val);
}

static struct macsec_flow *vsc8584_macsec_find_flow(struct macsec_context *ctx,
						    enum macsec_bank bank)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &priv->macsec_flows, list)
		if (pos->assoc_num == ctx->sa.assoc_num && pos->bank == bank)
			return pos;

	return ERR_PTR(-ENOENT);
}

static void vsc8584_macsec_flow_enable(struct phy_device *phydev,
				       struct macsec_flow *flow)
{
	enum macsec_bank bank = flow->bank;
	u32 val, idx = flow->index;

	if ((flow->bank == MACSEC_INGR && flow->rx_sa && !flow->rx_sa->active) ||
	    (flow->bank == MACSEC_EGR && flow->tx_sa && !flow->tx_sa->active))
		return;

	/* Enable */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_ENTRY_SET1, BIT(idx));

	/* Set in-use */
	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx));
	val |= MSCC_MS_SAM_FLOW_CTRL_SA_IN_USE;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx), val);
}

static void vsc8584_macsec_flow_disable(struct phy_device *phydev,
					struct macsec_flow *flow)
{
	enum macsec_bank bank = flow->bank;
	u32 val, idx = flow->index;

	/* Disable */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_ENTRY_CLEAR1, BIT(idx));

	/* Clear in-use */
	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx));
	val &= ~MSCC_MS_SAM_FLOW_CTRL_SA_IN_USE;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx), val);
}

static u32 vsc8584_macsec_flow_context_id(struct macsec_flow *flow)
{
	if (flow->bank == MACSEC_INGR)
		return flow->index + MSCC_MS_MAX_FLOWS;

	return flow->index;
}

/* Derive the AES key to get a key for the hash autentication */
static int vsc8584_macsec_derive_key(const u8 key[MACSEC_KEYID_LEN],
				     u16 key_len, u8 hkey[16])
{
	struct crypto_skcipher *tfm = crypto_alloc_skcipher("ecb(aes)", 0, 0);
	struct skcipher_request *req = NULL;
	struct scatterlist src, dst;
	DECLARE_CRYPTO_WAIT(wait);
	u32 input[4] = {0};
	int ret;

	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto out;
	}

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP, crypto_req_done,
				      &wait);
	ret = crypto_skcipher_setkey(tfm, key, key_len);
	if (ret < 0)
		goto out;

	sg_init_one(&src, input, 16);
	sg_init_one(&dst, hkey, 16);
	skcipher_request_set_crypt(req, &src, &dst, 16, NULL);

	ret = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);

out:
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	return ret;
}

static int vsc8584_macsec_transformation(struct phy_device *phydev,
					 struct macsec_flow *flow)
{
	struct vsc8531_private *priv = phydev->priv;
	enum macsec_bank bank = flow->bank;
	int i, ret, index = flow->index;
	u32 rec = 0, control = 0;
	u8 hkey[16];
	sci_t sci;

	ret = vsc8584_macsec_derive_key(flow->key, priv->secy->key_len, hkey);
	if (ret)
		return ret;

	switch (priv->secy->key_len) {
	case 16:
		control |= CONTROL_CRYPTO_ALG(CTRYPTO_ALG_AES_CTR_128);
		break;
	case 32:
		control |= CONTROL_CRYPTO_ALG(CTRYPTO_ALG_AES_CTR_256);
		break;
	default:
		return -EINVAL;
	}

	control |= (bank == MACSEC_EGR) ?
		   (CONTROL_TYPE_EGRESS | CONTROL_AN(priv->secy->tx_sc.encoding_sa)) :
		   (CONTROL_TYPE_INGRESS | CONTROL_SEQ_MASK);

	control |= CONTROL_UPDATE_SEQ | CONTROL_ENCRYPT_AUTH | CONTROL_KEY_IN_CTX |
		   CONTROL_IV0 | CONTROL_IV1 | CONTROL_IV_IN_SEQ |
		   CONTROL_DIGEST_TYPE(0x2) | CONTROL_SEQ_TYPE(0x1) |
		   CONTROL_AUTH_ALG(AUTH_ALG_AES_GHAS) | CONTROL_CONTEXT_ID;

	/* Set the control word */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 control);

	/* Set the context ID. Must be unique. */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 vsc8584_macsec_flow_context_id(flow));

	/* Set the encryption/decryption key */
	for (i = 0; i < priv->secy->key_len / sizeof(u32); i++)
		vsc8584_macsec_phy_write(phydev, bank,
					 MSCC_MS_XFORM_REC(index, rec++),
					 ((u32 *)flow->key)[i]);

	/* Set the authentication key */
	for (i = 0; i < 4; i++)
		vsc8584_macsec_phy_write(phydev, bank,
					 MSCC_MS_XFORM_REC(index, rec++),
					 ((u32 *)hkey)[i]);

	/* Initial sequence number */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 bank == MACSEC_INGR ?
				 flow->rx_sa->next_pn : flow->tx_sa->next_pn);

	if (bank == MACSEC_INGR)
		/* Set the mask (replay window size) */
		vsc8584_macsec_phy_write(phydev, bank,
					 MSCC_MS_XFORM_REC(index, rec++),
					 priv->secy->replay_window);

	/* Set the input vectors */
	sci = bank == MACSEC_INGR ? flow->rx_sa->sc->sci : priv->secy->sci;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 lower_32_bits(sci));
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 upper_32_bits(sci));

	while (rec < 20)
		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
					 0);

	flow->has_transformation = true;
	return 0;
}

static struct macsec_flow *vsc8584_macsec_alloc_flow(struct vsc8531_private *priv,
						     enum macsec_bank bank)
{
	unsigned long *bitmap = bank == MACSEC_INGR ?
				&priv->ingr_flows : &priv->egr_flows;
	struct macsec_flow *flow;
	int index;

	index = find_first_zero_bit(bitmap, MSCC_MS_MAX_FLOWS);

	if (index == MSCC_MS_MAX_FLOWS)
		return ERR_PTR(-ENOMEM);

	flow = kzalloc(sizeof(*flow), GFP_KERNEL);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	set_bit(index, bitmap);
	flow->index = index;
	flow->bank = bank;
	flow->priority = 8;
	flow->assoc_num = -1;

	list_add_tail(&flow->list, &priv->macsec_flows);
	return flow;
}

static void vsc8584_macsec_free_flow(struct vsc8531_private *priv,
				     struct macsec_flow *flow)
{
	unsigned long *bitmap = flow->bank == MACSEC_INGR ?
				&priv->ingr_flows : &priv->egr_flows;

	list_del(&flow->list);
	clear_bit(flow->index, bitmap);
	kfree(flow);
}

static int vsc8584_macsec_add_flow(struct phy_device *phydev,
				   struct macsec_flow *flow, bool update)
{
	int ret;

	flow->port = MSCC_MS_PORT_CONTROLLED;
	vsc8584_macsec_flow(phydev, flow);

	if (update)
		return 0;

	ret = vsc8584_macsec_transformation(phydev, flow);
	if (ret) {
		vsc8584_macsec_free_flow(phydev->priv, flow);
		return ret;
	}

	return 0;
}

static int vsc8584_macsec_default_flows(struct phy_device *phydev)
{
	struct macsec_flow *flow;

	/* Add a rule to let the MKA traffic go through, ingress */
	flow = vsc8584_macsec_alloc_flow(phydev->priv, MACSEC_INGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	flow->priority = 15;
	flow->port = MSCC_MS_PORT_UNCONTROLLED;
	flow->match.tagged = 1;
	flow->match.untagged = 1;
	flow->match.etype = 1;
	flow->etype = ETH_P_PAE;
	flow->action.bypass = 1;

	vsc8584_macsec_flow(phydev, flow);
	vsc8584_macsec_flow_enable(phydev, flow);

	/* Add a rule to let the MKA traffic go through, egress */
	flow = vsc8584_macsec_alloc_flow(phydev->priv, MACSEC_EGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	flow->priority = 15;
	flow->port = MSCC_MS_PORT_COMMON;
	flow->match.untagged = 1;
	flow->match.etype = 1;
	flow->etype = ETH_P_PAE;
	flow->action.bypass = 1;

	vsc8584_macsec_flow(phydev, flow);
	vsc8584_macsec_flow_enable(phydev, flow);

	return 0;
}

static void vsc8584_macsec_del_flow(struct phy_device *phydev,
				    struct macsec_flow *flow)
{
	vsc8584_macsec_flow_disable(phydev, flow);
	vsc8584_macsec_free_flow(phydev->priv, flow);
}

static int __vsc8584_macsec_add_rxsa(struct macsec_context *ctx,
				     struct macsec_flow *flow, bool update)
{
	struct phy_device *phydev = ctx->phydev;
	struct vsc8531_private *priv = phydev->priv;

	if (!flow) {
		flow = vsc8584_macsec_alloc_flow(priv, MACSEC_INGR);
		if (IS_ERR(flow))
			return PTR_ERR(flow);

		memcpy(flow->key, ctx->sa.key, priv->secy->key_len);
	}

	flow->assoc_num = ctx->sa.assoc_num;
	flow->rx_sa = ctx->sa.rx_sa;

	/* Always match tagged packets on ingress */
	flow->match.tagged = 1;
	flow->match.sci = 1;

	if (priv->secy->validate_frames != MACSEC_VALIDATE_DISABLED)
		flow->match.untagged = 1;

	return vsc8584_macsec_add_flow(phydev, flow, update);
}

static int __vsc8584_macsec_add_txsa(struct macsec_context *ctx,
				     struct macsec_flow *flow, bool update)
{
	struct phy_device *phydev = ctx->phydev;
	struct vsc8531_private *priv = phydev->priv;

	if (!flow) {
		flow = vsc8584_macsec_alloc_flow(priv, MACSEC_EGR);
		if (IS_ERR(flow))
			return PTR_ERR(flow);

		memcpy(flow->key, ctx->sa.key, priv->secy->key_len);
	}

	flow->assoc_num = ctx->sa.assoc_num;
	flow->tx_sa = ctx->sa.tx_sa;

	/* Always match untagged packets on egress */
	flow->match.untagged = 1;

	return vsc8584_macsec_add_flow(phydev, flow, update);
}

static int vsc8584_macsec_dev_open(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *flow, *tmp;

	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list)
		vsc8584_macsec_flow_enable(ctx->phydev, flow);

	return 0;
}

static int vsc8584_macsec_dev_stop(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *flow, *tmp;

	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list)
		vsc8584_macsec_flow_disable(ctx->phydev, flow);

	return 0;
}

static int vsc8584_macsec_add_secy(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_secy *secy = ctx->secy;

	if (ctx->prepare) {
		if (priv->secy)
			return -EEXIST;

		return 0;
	}

	priv->secy = secy;

	vsc8584_macsec_flow_default_action(ctx->phydev, MACSEC_EGR,
					   secy->validate_frames != MACSEC_VALIDATE_DISABLED);
	vsc8584_macsec_flow_default_action(ctx->phydev, MACSEC_INGR,
					   secy->validate_frames != MACSEC_VALIDATE_DISABLED);

	return vsc8584_macsec_default_flows(ctx->phydev);
}

static int vsc8584_macsec_del_secy(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *flow, *tmp;

	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list)
		vsc8584_macsec_del_flow(ctx->phydev, flow);

	vsc8584_macsec_flow_default_action(ctx->phydev, MACSEC_EGR, false);
	vsc8584_macsec_flow_default_action(ctx->phydev, MACSEC_INGR, false);

	priv->secy = NULL;
	return 0;
}

static int vsc8584_macsec_upd_secy(struct macsec_context *ctx)
{
	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	vsc8584_macsec_del_secy(ctx);
	return vsc8584_macsec_add_secy(ctx);
}

static int vsc8584_macsec_add_rxsc(struct macsec_context *ctx)
{
	/* Nothing to do */
	return 0;
}

static int vsc8584_macsec_upd_rxsc(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int vsc8584_macsec_del_rxsc(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *flow, *tmp;

	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list) {
		if (flow->bank == MACSEC_INGR && flow->rx_sa &&
		    flow->rx_sa->sc->sci == ctx->rx_sc->sci)
			vsc8584_macsec_del_flow(ctx->phydev, flow);
	}

	return 0;
}

static int vsc8584_macsec_add_rxsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow = NULL;

	if (ctx->prepare)
		return __vsc8584_macsec_add_rxsa(ctx, flow, false);

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_INGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	vsc8584_macsec_flow_enable(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_upd_rxsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow;

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_INGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	if (ctx->prepare) {
		/* Make sure the flow is disabled before updating it */
		vsc8584_macsec_flow_disable(ctx->phydev, flow);

		return __vsc8584_macsec_add_rxsa(ctx, flow, true);
	}

	vsc8584_macsec_flow_enable(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_del_rxsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow;

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_INGR);

	if (IS_ERR(flow))
		return PTR_ERR(flow);
	if (ctx->prepare)
		return 0;

	vsc8584_macsec_del_flow(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_add_txsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow = NULL;

	if (ctx->prepare)
		return __vsc8584_macsec_add_txsa(ctx, flow, false);

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_EGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	vsc8584_macsec_flow_enable(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_upd_txsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow;

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_EGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	if (ctx->prepare) {
		/* Make sure the flow is disabled before updating it */
		vsc8584_macsec_flow_disable(ctx->phydev, flow);

		return __vsc8584_macsec_add_txsa(ctx, flow, true);
	}

	vsc8584_macsec_flow_enable(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_del_txsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow;

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_EGR);

	if (IS_ERR(flow))
		return PTR_ERR(flow);
	if (ctx->prepare)
		return 0;

	vsc8584_macsec_del_flow(ctx->phydev, flow);
	return 0;
}

static struct macsec_ops vsc8584_macsec_ops = {
	.mdo_dev_open = vsc8584_macsec_dev_open,
	.mdo_dev_stop = vsc8584_macsec_dev_stop,
	.mdo_add_secy = vsc8584_macsec_add_secy,
	.mdo_upd_secy = vsc8584_macsec_upd_secy,
	.mdo_del_secy = vsc8584_macsec_del_secy,
	.mdo_add_rxsc = vsc8584_macsec_add_rxsc,
	.mdo_upd_rxsc = vsc8584_macsec_upd_rxsc,
	.mdo_del_rxsc = vsc8584_macsec_del_rxsc,
	.mdo_add_rxsa = vsc8584_macsec_add_rxsa,
	.mdo_upd_rxsa = vsc8584_macsec_upd_rxsa,
	.mdo_del_rxsa = vsc8584_macsec_del_rxsa,
	.mdo_add_txsa = vsc8584_macsec_add_txsa,
	.mdo_upd_txsa = vsc8584_macsec_upd_txsa,
	.mdo_del_txsa = vsc8584_macsec_del_txsa,
};

int vsc8584_macsec_init(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531 = phydev->priv;

	switch (phydev->phy_id & phydev->drv->phy_id_mask) {
	case PHY_ID_VSC856X:
	case PHY_ID_VSC8575:
	case PHY_ID_VSC8582:
	case PHY_ID_VSC8584:
		INIT_LIST_HEAD(&vsc8531->macsec_flows);
		vsc8531->secy = NULL;

		phydev->macsec_ops = &vsc8584_macsec_ops;

		return __vsc8584_macsec_init(phydev);
	}

	return 0;
}

void vsc8584_handle_macsec_interrupt(struct phy_device *phydev)
{
	struct vsc8531_private *priv = phydev->priv;
	struct macsec_flow *flow, *tmp;
	u32 cause, rec;

	/* Check MACsec PN rollover */
	cause = vsc8584_macsec_phy_read(phydev, MACSEC_EGR,
					MSCC_MS_INTR_CTRL_STATUS);
	cause &= MSCC_MS_INTR_CTRL_STATUS_INTR_CLR_STATUS_M;
	if (!(cause & MACSEC_INTR_CTRL_STATUS_ROLLOVER))
		return;

	rec = 6 + priv->secy->key_len / sizeof(u32);
	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list) {
		u32 val;

		if (flow->bank != MACSEC_EGR || !flow->has_transformation)
			continue;

		val = vsc8584_macsec_phy_read(phydev, MACSEC_EGR,
					      MSCC_MS_XFORM_REC(flow->index, rec));
		if (val == 0xffffffff) {
			vsc8584_macsec_flow_disable(phydev, flow);
			macsec_pn_wrapped(priv->secy, flow->tx_sa);
			return;
		}
	}
}

void vsc8584_config_macsec_intr(struct phy_device *phydev)
{
	phy_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED_2);
	phy_write(phydev, MSCC_PHY_EXTENDED_INT, MSCC_PHY_EXTENDED_INT_MS_EGR);
	phy_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	vsc8584_macsec_phy_write(phydev, MACSEC_EGR, MSCC_MS_AIC_CTRL, 0xf);
	vsc8584_macsec_phy_write(phydev, MACSEC_EGR, MSCC_MS_INTR_CTRL_STATUS,
				 MSCC_MS_INTR_CTRL_STATUS_INTR_ENABLE(MACSEC_INTR_CTRL_STATUS_ROLLOVER));
}
