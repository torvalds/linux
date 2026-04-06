// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2026 Broadcom.

#include <linux/linkmode.h>

#include "bnge.h"
#include "bnge_link.h"
#include "bnge_hwrm_lib.h"

enum bnge_media_type {
	BNGE_MEDIA_UNKNOWN = 0,
	BNGE_MEDIA_CR,
	BNGE_MEDIA_SR,
	BNGE_MEDIA_LR_ER_FR,
	BNGE_MEDIA_KR,
	__BNGE_MEDIA_END,
};

static const enum bnge_media_type bnge_phy_types[] = {
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASECR4] = BNGE_MEDIA_CR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASESR4] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASELR4] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASEER4] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASESR10] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_200G_BASECR4] = BNGE_MEDIA_CR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_200G_BASESR4] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_200G_BASELR4] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_200G_BASEER4] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_50G_BASECR] = BNGE_MEDIA_CR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_50G_BASESR] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_50G_BASELR] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_50G_BASEER] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASECR2] = BNGE_MEDIA_CR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASESR2] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASELR2] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASEER2] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASECR] = BNGE_MEDIA_CR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASESR] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASELR] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_100G_BASEER] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_200G_BASECR2] = BNGE_MEDIA_CR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_200G_BASESR2] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_200G_BASELR2] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_200G_BASEER2] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_400G_BASECR8] = BNGE_MEDIA_CR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_400G_BASESR8] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_400G_BASELR8] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_400G_BASEER8] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_400G_BASECR4] = BNGE_MEDIA_CR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_400G_BASESR4] = BNGE_MEDIA_SR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_400G_BASELR4] = BNGE_MEDIA_LR_ER_FR,
	[PORT_PHY_QCFG_RESP_PHY_TYPE_400G_BASEER4] = BNGE_MEDIA_LR_ER_FR,
};

static u32 bnge_fw_to_ethtool_speed(u16 fw_link_speed)
{
	switch (fw_link_speed) {
	case BNGE_LINK_SPEED_50GB:
	case BNGE_LINK_SPEED_50GB_PAM4:
		return SPEED_50000;
	case BNGE_LINK_SPEED_100GB:
	case BNGE_LINK_SPEED_100GB_PAM4:
	case BNGE_LINK_SPEED_100GB_PAM4_112:
		return SPEED_100000;
	case BNGE_LINK_SPEED_200GB:
	case BNGE_LINK_SPEED_200GB_PAM4:
	case BNGE_LINK_SPEED_200GB_PAM4_112:
		return SPEED_200000;
	case BNGE_LINK_SPEED_400GB:
	case BNGE_LINK_SPEED_400GB_PAM4:
	case BNGE_LINK_SPEED_400GB_PAM4_112:
		return SPEED_400000;
	case BNGE_LINK_SPEED_800GB:
	case BNGE_LINK_SPEED_800GB_PAM4_112:
		return SPEED_800000;
	default:
		return SPEED_UNKNOWN;
	}
}

static void bnge_set_auto_speed(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct bnge_link_info *link_info;

	link_info = &bn->bd->link_info;
	elink_info->advertising = link_info->auto_link_speeds2;
}

static void bnge_set_force_speed(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct bnge_link_info *link_info;

	link_info = &bn->bd->link_info;
	elink_info->req_link_speed = link_info->force_link_speed2;
	switch (elink_info->req_link_speed) {
	case BNGE_LINK_SPEED_50GB_PAM4:
	case BNGE_LINK_SPEED_100GB_PAM4:
	case BNGE_LINK_SPEED_200GB_PAM4:
	case BNGE_LINK_SPEED_400GB_PAM4:
		elink_info->req_signal_mode = BNGE_SIG_MODE_PAM4;
		break;
	case BNGE_LINK_SPEED_100GB_PAM4_112:
	case BNGE_LINK_SPEED_200GB_PAM4_112:
	case BNGE_LINK_SPEED_400GB_PAM4_112:
	case BNGE_LINK_SPEED_800GB_PAM4_112:
		elink_info->req_signal_mode = BNGE_SIG_MODE_PAM4_112;
		break;
	default:
		elink_info->req_signal_mode = BNGE_SIG_MODE_NRZ;
		break;
	}
}

void bnge_init_ethtool_link_settings(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct bnge_link_info *link_info;
	struct bnge_dev *bd = bn->bd;

	link_info = &bd->link_info;

	if (BNGE_AUTO_MODE(link_info->auto_mode)) {
		elink_info->autoneg = BNGE_AUTONEG_SPEED;
		if (link_info->auto_pause_setting &
		    PORT_PHY_QCFG_RESP_AUTO_PAUSE_AUTONEG_PAUSE)
			elink_info->autoneg |= BNGE_AUTONEG_FLOW_CTRL;
		bnge_set_auto_speed(bn);
	} else {
		elink_info->autoneg = 0;
		elink_info->advertising = 0;
		bnge_set_force_speed(bn);
		elink_info->req_duplex = link_info->duplex_setting;
	}
	if (elink_info->autoneg & BNGE_AUTONEG_FLOW_CTRL)
		elink_info->req_flow_ctrl =
			link_info->auto_pause_setting & BNGE_LINK_PAUSE_BOTH;
	else
		elink_info->req_flow_ctrl = link_info->force_pause_setting;
}

int bnge_probe_phy(struct bnge_net *bn, bool fw_dflt)
{
	struct bnge_dev *bd = bn->bd;
	int rc;

	bd->phy_flags = 0;
	rc = bnge_hwrm_phy_qcaps(bd);
	if (rc) {
		netdev_err(bn->netdev,
			   "Probe PHY can't get PHY qcaps (rc: %d)\n", rc);
		return rc;
	}
	if (bd->phy_flags & BNGE_PHY_FL_NO_FCS)
		bn->netdev->priv_flags |= IFF_SUPP_NOFCS;
	else
		bn->netdev->priv_flags &= ~IFF_SUPP_NOFCS;
	if (!fw_dflt)
		return 0;

	rc = bnge_update_link(bn, false);
	if (rc) {
		netdev_err(bn->netdev, "Probe PHY can't update link (rc: %d)\n",
			   rc);
		return rc;
	}
	bnge_init_ethtool_link_settings(bn);

	return 0;
}

void bnge_hwrm_set_link_common(struct bnge_net *bn,
			       struct hwrm_port_phy_cfg_input *req)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;

	if (elink_info->autoneg & BNGE_AUTONEG_SPEED) {
		req->auto_mode |= PORT_PHY_CFG_REQ_AUTO_MODE_SPEED_MASK;
		req->enables |= cpu_to_le32(BNGE_PHY_AUTO_SPEEDS2_MASK);
		req->auto_link_speeds2_mask =
			cpu_to_le16(elink_info->advertising);
		req->enables |= cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_MODE);
		req->flags |= cpu_to_le32(BNGE_PHY_FLAGS_RESTART_AUTO);
	} else {
		req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_FORCE);
		req->force_link_speeds2 =
			cpu_to_le16(elink_info->req_link_speed);
		req->enables |=
			cpu_to_le32(BNGE_PHY_FLAGS_ENA_FORCE_SPEEDS2);
		netif_info(bn, link, bn->netdev,
			   "Forcing FW speed2: %d\n",
			   (u32)elink_info->req_link_speed);
	}

	/* tell FW that the setting takes effect immediately */
	req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_RESET_PHY);
}

static bool bnge_auto_speed_updated(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct bnge_link_info *link_info = &bn->bd->link_info;

	return elink_info->advertising != link_info->auto_link_speeds2;
}

void bnge_hwrm_set_pause_common(struct bnge_net *bn,
				struct hwrm_port_phy_cfg_input *req)
{
	if (bn->eth_link_info.autoneg & BNGE_AUTONEG_FLOW_CTRL) {
		req->auto_pause = PORT_PHY_CFG_REQ_AUTO_PAUSE_AUTONEG_PAUSE;
		if (bn->eth_link_info.req_flow_ctrl & BNGE_LINK_PAUSE_RX)
			req->auto_pause |= PORT_PHY_CFG_REQ_AUTO_PAUSE_RX;
		if (bn->eth_link_info.req_flow_ctrl & BNGE_LINK_PAUSE_TX)
			req->auto_pause |= PORT_PHY_CFG_REQ_AUTO_PAUSE_TX;
		req->enables |=
			cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_PAUSE);
	} else {
		if (bn->eth_link_info.req_flow_ctrl & BNGE_LINK_PAUSE_RX)
			req->force_pause |= PORT_PHY_CFG_REQ_FORCE_PAUSE_RX;
		if (bn->eth_link_info.req_flow_ctrl & BNGE_LINK_PAUSE_TX)
			req->force_pause |= PORT_PHY_CFG_REQ_FORCE_PAUSE_TX;
		req->enables |=
			cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_FORCE_PAUSE);
		req->auto_pause = req->force_pause;
		req->enables |=
			cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_PAUSE);
	}
}

static bool bnge_force_speed_updated(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct bnge_link_info *link_info = &bn->bd->link_info;

	return elink_info->req_link_speed != link_info->force_link_speed2;
}

int bnge_update_phy_setting(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info;
	struct bnge_link_info *link_info;
	struct bnge_dev *bd = bn->bd;
	bool update_pause = false;
	bool update_link = false;
	bool hw_pause_autoneg;
	bool pause_autoneg;
	int rc;

	link_info = &bd->link_info;
	elink_info = &bn->eth_link_info;
	rc = bnge_update_link(bn, true);
	if (rc) {
		netdev_err(bn->netdev, "failed to update link (rc: %d)\n",
			   rc);
		return rc;
	}

	pause_autoneg = !!(elink_info->autoneg & BNGE_AUTONEG_FLOW_CTRL);
	hw_pause_autoneg = !!(link_info->auto_pause_setting &
			      PORT_PHY_QCFG_RESP_AUTO_PAUSE_AUTONEG_PAUSE);

	/* Check if pause autonegotiation state has changed */
	if (pause_autoneg != hw_pause_autoneg) {
		update_pause = true;
	} else if (pause_autoneg) {
		/* If pause autoneg is enabled, check if the
		 * requested RX/TX bits changed
		 */
		if ((link_info->auto_pause_setting & BNGE_LINK_PAUSE_BOTH) !=
		    elink_info->req_flow_ctrl)
			update_pause = true;
	} else {
		/* If pause autoneg is disabled, check if the
		 * forced RX/TX bits changed
		 */
		if (link_info->force_pause_setting != elink_info->req_flow_ctrl)
			update_pause = true;
	}

	/* Force update if link change is requested */
	if (elink_info->force_link_chng)
		update_pause = true;

	/* Check if link speed or duplex settings have changed */
	if (!(elink_info->autoneg & BNGE_AUTONEG_SPEED)) {
		if (BNGE_AUTO_MODE(link_info->auto_mode) ||
		    bnge_force_speed_updated(bn) ||
		    elink_info->req_duplex != link_info->duplex_setting)
			update_link = true;
	} else {
		if (link_info->auto_mode == BNGE_LINK_AUTO_NONE ||
		    bnge_auto_speed_updated(bn))
			update_link = true;
	}

	/* The last close may have shut down the link, so need to call
	 * PHY_CFG to bring it back up.
	 */
	if (!BNGE_LINK_IS_UP(bd))
		update_link = true;

	if (update_link)
		rc = bnge_hwrm_set_link_setting(bn, update_pause);
	else if (update_pause)
		rc = bnge_hwrm_set_pause(bn);

	if (rc) {
		netdev_err(bn->netdev,
			   "failed to update PHY setting (rc: %d)\n", rc);
		return rc;
	}

	return 0;
}

void bnge_get_port_module_status(struct bnge_net *bn)
{
	struct hwrm_port_phy_qcfg_output *resp;
	struct bnge_link_info *link_info;
	struct bnge_dev *bd = bn->bd;
	u8 module_status;

	link_info = &bd->link_info;
	resp = &link_info->phy_qcfg_resp;

	if (bnge_update_link(bn, true))
		return;

	module_status = link_info->module_status;
	switch (module_status) {
	case PORT_PHY_QCFG_RESP_MODULE_STATUS_DISABLETX:
	case PORT_PHY_QCFG_RESP_MODULE_STATUS_PWRDOWN:
	case PORT_PHY_QCFG_RESP_MODULE_STATUS_WARNINGMSG:
		netdev_warn(bn->netdev,
			    "Unqualified SFP+ module detected on port %d\n",
			    bd->pf.port_id);
		netdev_warn(bn->netdev, "Module part number %.*s\n",
			    (int)sizeof(resp->phy_vendor_partnumber),
			    resp->phy_vendor_partnumber);
		if (module_status == PORT_PHY_QCFG_RESP_MODULE_STATUS_DISABLETX)
			netdev_warn(bn->netdev, "TX is disabled\n");
		if (module_status == PORT_PHY_QCFG_RESP_MODULE_STATUS_PWRDOWN)
			netdev_warn(bn->netdev, "SFP+ module is shut down\n");
		break;
	}
}

static void bnge_set_default_adv_speeds(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct bnge_link_info *link_info = &bn->bd->link_info;

	elink_info->advertising = link_info->support_auto_speeds2;
}

static bool bnge_support_dropped(u16 advertising, u16 supported)
{
	return (advertising & ~supported) != 0;
}

bool bnge_support_speed_dropped(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct bnge_link_info *link_info = &bn->bd->link_info;

	/* Check if any advertised speeds are no longer supported. The caller
	 * holds the netdev instance lock, so we can modify link_info settings.
	 */
	if (bnge_support_dropped(elink_info->advertising,
				 link_info->support_auto_speeds2)) {
		elink_info->advertising = link_info->support_auto_speeds2;
		return true;
	}
	return false;
}

static char *bnge_report_fec(struct bnge_link_info *link_info)
{
	u8 active_fec = link_info->active_fec_sig_mode &
			PORT_PHY_QCFG_RESP_ACTIVE_FEC_MASK;

	switch (active_fec) {
	default:
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_NONE_ACTIVE:
		return "None";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_CLAUSE74_ACTIVE:
		return "Clause 74 BaseR";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_CLAUSE91_ACTIVE:
		return "Clause 91 RS(528,514)";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS544_1XN_ACTIVE:
		return "Clause 91 RS544_1XN";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS544_IEEE_ACTIVE:
		return "Clause 91 RS(544,514)";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS272_1XN_ACTIVE:
		return "Clause 91 RS272_1XN";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS272_IEEE_ACTIVE:
		return "Clause 91 RS(272,257)";
	}
}

void bnge_report_link(struct bnge_dev *bd)
{
	if (BNGE_LINK_IS_UP(bd)) {
		const char *signal = "";
		const char *flow_ctrl;
		const char *duplex;
		u32 speed;
		u16 fec;

		netif_carrier_on(bd->netdev);
		speed = bnge_fw_to_ethtool_speed(bd->link_info.link_speed);
		if (speed == SPEED_UNKNOWN) {
			netdev_info(bd->netdev,
				    "NIC Link is Up, speed unknown\n");
			return;
		}
		if (bd->link_info.duplex == BNGE_LINK_DUPLEX_FULL)
			duplex = "full";
		else
			duplex = "half";
		if (bd->link_info.pause == BNGE_LINK_PAUSE_BOTH)
			flow_ctrl = "ON - receive & transmit";
		else if (bd->link_info.pause == BNGE_LINK_PAUSE_TX)
			flow_ctrl = "ON - transmit";
		else if (bd->link_info.pause == BNGE_LINK_PAUSE_RX)
			flow_ctrl = "ON - receive";
		else
			flow_ctrl = "none";
		if (bd->link_info.phy_qcfg_resp.option_flags &
		    PORT_PHY_QCFG_RESP_OPTION_FLAGS_SIGNAL_MODE_KNOWN) {
			u8 sig_mode = bd->link_info.active_fec_sig_mode &
				      PORT_PHY_QCFG_RESP_SIGNAL_MODE_MASK;
			switch (sig_mode) {
			case PORT_PHY_QCFG_RESP_SIGNAL_MODE_NRZ:
				signal = "(NRZ) ";
				break;
			case PORT_PHY_QCFG_RESP_SIGNAL_MODE_PAM4:
				signal = "(PAM4 56Gbps) ";
				break;
			case PORT_PHY_QCFG_RESP_SIGNAL_MODE_PAM4_112:
				signal = "(PAM4 112Gbps) ";
				break;
			default:
				break;
			}
		}
		netdev_info(bd->netdev, "NIC Link is Up, %u Mbps %s%s duplex, Flow control: %s\n",
			    speed, signal, duplex, flow_ctrl);
		fec = bd->link_info.fec_cfg;
		if (!(fec & PORT_PHY_QCFG_RESP_FEC_CFG_FEC_NONE_SUPPORTED))
			netdev_info(bd->netdev, "FEC autoneg %s encoding: %s\n",
				    (fec & BNGE_FEC_AUTONEG) ? "on" : "off",
				    bnge_report_fec(&bd->link_info));
	} else {
		netif_carrier_off(bd->netdev);
		netdev_info(bd->netdev, "NIC Link is Down\n");
	}
}

static void bnge_get_ethtool_modes(struct bnge_net *bn,
				   struct ethtool_link_ksettings *lk_ksettings)
{
	struct bnge_ethtool_link_info *elink_info;
	struct bnge_link_info *link_info;
	struct bnge_dev *bd = bn->bd;

	elink_info = &bn->eth_link_info;
	link_info = &bd->link_info;

	if (!(bd->phy_flags & BNGE_PHY_FL_NO_PAUSE)) {
		linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				 lk_ksettings->link_modes.supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				 lk_ksettings->link_modes.supported);
	}

	if (link_info->support_auto_speeds2)
		linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				 lk_ksettings->link_modes.supported);

	if (~elink_info->autoneg & BNGE_AUTONEG_FLOW_CTRL)
		return;

	if (link_info->auto_pause_setting & BNGE_LINK_PAUSE_RX)
		linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				 lk_ksettings->link_modes.advertising);
	if (hweight8(link_info->auto_pause_setting & BNGE_LINK_PAUSE_BOTH) == 1)
		linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				 lk_ksettings->link_modes.advertising);
	if (link_info->lp_pause & BNGE_LINK_PAUSE_RX)
		linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				 lk_ksettings->link_modes.lp_advertising);
	if (hweight8(link_info->lp_pause & BNGE_LINK_PAUSE_BOTH) == 1)
		linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				 lk_ksettings->link_modes.lp_advertising);
}

u32 bnge_get_link(struct net_device *dev)
{
	struct bnge_net *bn = netdev_priv(dev);

	return BNGE_LINK_IS_UP(bn->bd);
}

static enum bnge_media_type
bnge_get_media(struct bnge_link_info *link_info)
{
	switch (link_info->media_type) {
	case PORT_PHY_QCFG_RESP_MEDIA_TYPE_DAC:
		return BNGE_MEDIA_CR;
	default:
		if (link_info->phy_type < ARRAY_SIZE(bnge_phy_types))
			return bnge_phy_types[link_info->phy_type];
		return BNGE_MEDIA_UNKNOWN;
	}
}

enum bnge_link_speed_indices {
	BNGE_LINK_SPEED_UNKNOWN = 0,
	BNGE_LINK_SPEED_50GB_IDX,
	BNGE_LINK_SPEED_100GB_IDX,
	BNGE_LINK_SPEED_200GB_IDX,
	BNGE_LINK_SPEED_400GB_IDX,
	BNGE_LINK_SPEED_800GB_IDX,
	__BNGE_LINK_SPEED_END
};

static enum bnge_link_speed_indices bnge_fw_speed_idx(u16 speed)
{
	switch (speed) {
	case BNGE_LINK_SPEED_50GB:
	case BNGE_LINK_SPEED_50GB_PAM4:
		return BNGE_LINK_SPEED_50GB_IDX;
	case BNGE_LINK_SPEED_100GB:
	case BNGE_LINK_SPEED_100GB_PAM4:
	case BNGE_LINK_SPEED_100GB_PAM4_112:
		return BNGE_LINK_SPEED_100GB_IDX;
	case BNGE_LINK_SPEED_200GB:
	case BNGE_LINK_SPEED_200GB_PAM4:
	case BNGE_LINK_SPEED_200GB_PAM4_112:
		return BNGE_LINK_SPEED_200GB_IDX;
	case BNGE_LINK_SPEED_400GB:
	case BNGE_LINK_SPEED_400GB_PAM4:
	case BNGE_LINK_SPEED_400GB_PAM4_112:
		return BNGE_LINK_SPEED_400GB_IDX;
	case BNGE_LINK_SPEED_800GB:
	case BNGE_LINK_SPEED_800GB_PAM4_112:
		return BNGE_LINK_SPEED_800GB_IDX;
	default:
		return BNGE_LINK_SPEED_UNKNOWN;
	}
}

/* Compile-time link mode mapping table.
 * Indexed by [speed_idx][sig_mode][media].
 */
#define BNGE_LINK_M(speed, sig, media, lm)	\
	[BNGE_LINK_SPEED_##speed##_IDX]	\
	[BNGE_SIG_MODE_##sig]		\
	[BNGE_MEDIA_##media] = ETHTOOL_LINK_MODE_##lm##_Full_BIT

static const enum ethtool_link_mode_bit_indices
bnge_link_modes[__BNGE_LINK_SPEED_END]
	       [BNGE_SIG_MODE_MAX]
	       [__BNGE_MEDIA_END] = {
	/* 50GB PAM4 */
	BNGE_LINK_M(50GB,  PAM4, CR,        50000baseCR),
	BNGE_LINK_M(50GB,  PAM4, SR,        50000baseSR),
	BNGE_LINK_M(50GB,  PAM4, LR_ER_FR,  50000baseLR_ER_FR),
	BNGE_LINK_M(50GB,  PAM4, KR,        50000baseKR),

	/* 100GB NRZ */
	BNGE_LINK_M(100GB, NRZ,  CR,        100000baseCR4),
	BNGE_LINK_M(100GB, NRZ,  SR,        100000baseSR4),
	BNGE_LINK_M(100GB, NRZ,  LR_ER_FR,  100000baseLR4_ER4),
	BNGE_LINK_M(100GB, NRZ,  KR,        100000baseKR4),

	/* 100GB PAM4 */
	BNGE_LINK_M(100GB, PAM4, CR,        100000baseCR2),
	BNGE_LINK_M(100GB, PAM4, SR,        100000baseSR2),
	BNGE_LINK_M(100GB, PAM4, LR_ER_FR,  100000baseLR2_ER2_FR2),
	BNGE_LINK_M(100GB, PAM4, KR,        100000baseKR2),

	/* 100GB PAM4_112 */
	BNGE_LINK_M(100GB, PAM4_112, CR,        100000baseCR),
	BNGE_LINK_M(100GB, PAM4_112, SR,        100000baseSR),
	BNGE_LINK_M(100GB, PAM4_112, LR_ER_FR,  100000baseLR_ER_FR),
	BNGE_LINK_M(100GB, PAM4_112, KR,        100000baseKR),

	/* 200GB PAM4 */
	BNGE_LINK_M(200GB, PAM4, CR,        200000baseCR4),
	BNGE_LINK_M(200GB, PAM4, SR,        200000baseSR4),
	BNGE_LINK_M(200GB, PAM4, LR_ER_FR,  200000baseLR4_ER4_FR4),
	BNGE_LINK_M(200GB, PAM4, KR,        200000baseKR4),

	/* 200GB PAM4_112 */
	BNGE_LINK_M(200GB, PAM4_112, CR,        200000baseCR2),
	BNGE_LINK_M(200GB, PAM4_112, SR,        200000baseSR2),
	BNGE_LINK_M(200GB, PAM4_112, LR_ER_FR,  200000baseLR2_ER2_FR2),
	BNGE_LINK_M(200GB, PAM4_112, KR,        200000baseKR2),

	/* 400GB PAM4 */
	BNGE_LINK_M(400GB, PAM4, CR,        400000baseCR8),
	BNGE_LINK_M(400GB, PAM4, SR,        400000baseSR8),
	BNGE_LINK_M(400GB, PAM4, LR_ER_FR,  400000baseLR8_ER8_FR8),
	BNGE_LINK_M(400GB, PAM4, KR,        400000baseKR8),

	/* 400GB PAM4_112 */
	BNGE_LINK_M(400GB, PAM4_112, CR,        400000baseCR4),
	BNGE_LINK_M(400GB, PAM4_112, SR,        400000baseSR4),
	BNGE_LINK_M(400GB, PAM4_112, LR_ER_FR,  400000baseLR4_ER4_FR4),
	BNGE_LINK_M(400GB, PAM4_112, KR,        400000baseKR4),

	/* 800GB PAM4_112 */
	BNGE_LINK_M(800GB, PAM4_112, CR,        800000baseCR8),
	BNGE_LINK_M(800GB, PAM4_112, SR,        800000baseSR8),
	BNGE_LINK_M(800GB, PAM4_112, KR,        800000baseKR8),
};

#define BNGE_LINK_MODE_UNKNOWN -1

static enum ethtool_link_mode_bit_indices
bnge_get_link_mode(struct bnge_net *bn)
{
	enum ethtool_link_mode_bit_indices link_mode;
	struct bnge_ethtool_link_info *elink_info;
	enum bnge_link_speed_indices speed;
	struct bnge_link_info *link_info;
	struct bnge_dev *bd = bn->bd;
	enum bnge_media_type media;
	u8 sig_mode;

	elink_info = &bn->eth_link_info;
	link_info = &bd->link_info;

	if (link_info->phy_link_status != BNGE_LINK_LINK)
		return BNGE_LINK_MODE_UNKNOWN;

	media = bnge_get_media(link_info);
	if (BNGE_AUTO_MODE(link_info->auto_mode)) {
		speed = bnge_fw_speed_idx(link_info->link_speed);
		sig_mode = link_info->active_fec_sig_mode &
			PORT_PHY_QCFG_RESP_SIGNAL_MODE_MASK;
	} else {
		speed = bnge_fw_speed_idx(elink_info->req_link_speed);
		sig_mode = elink_info->req_signal_mode;
	}
	if (sig_mode >= BNGE_SIG_MODE_MAX)
		return BNGE_LINK_MODE_UNKNOWN;

	/* Since ETHTOOL_LINK_MODE_10baseT_Half_BIT is defined as 0 and
	 * not actually supported, the zeroes in this map can be safely
	 * used to represent unknown link modes.
	 */
	link_mode = bnge_link_modes[speed][sig_mode][media];
	if (!link_mode)
		return BNGE_LINK_MODE_UNKNOWN;

	return link_mode;
}

static const u16 bnge_nrz_speeds2_masks[__BNGE_LINK_SPEED_END] = {
	[BNGE_LINK_SPEED_100GB_IDX] = BNGE_LINK_SPEEDS2_MSK_100GB,
};

static const u16 bnge_pam4_speeds2_masks[__BNGE_LINK_SPEED_END] = {
	[BNGE_LINK_SPEED_50GB_IDX] = BNGE_LINK_SPEEDS2_MSK_50GB_PAM4,
	[BNGE_LINK_SPEED_100GB_IDX] = BNGE_LINK_SPEEDS2_MSK_100GB_PAM4,
	[BNGE_LINK_SPEED_200GB_IDX] = BNGE_LINK_SPEEDS2_MSK_200GB_PAM4,
	[BNGE_LINK_SPEED_400GB_IDX] = BNGE_LINK_SPEEDS2_MSK_400GB_PAM4,
};

static const u16 bnge_pam4_112_speeds2_masks[__BNGE_LINK_SPEED_END] = {
	[BNGE_LINK_SPEED_100GB_IDX] = BNGE_LINK_SPEEDS2_MSK_100GB_PAM4_112,
	[BNGE_LINK_SPEED_200GB_IDX] = BNGE_LINK_SPEEDS2_MSK_200GB_PAM4_112,
	[BNGE_LINK_SPEED_400GB_IDX] = BNGE_LINK_SPEEDS2_MSK_400GB_PAM4_112,
	[BNGE_LINK_SPEED_800GB_IDX] = BNGE_LINK_SPEEDS2_MSK_800GB_PAM4_112,
};

static enum bnge_link_speed_indices
bnge_encoding_speed_idx(u8 sig_mode, u16 speed_msk)
{
	const u16 *speeds;
	int idx, len;

	switch (sig_mode) {
	case BNGE_SIG_MODE_NRZ:
		speeds = bnge_nrz_speeds2_masks;
		len = ARRAY_SIZE(bnge_nrz_speeds2_masks);
		break;
	case BNGE_SIG_MODE_PAM4:
		speeds = bnge_pam4_speeds2_masks;
		len = ARRAY_SIZE(bnge_pam4_speeds2_masks);
		break;
	case BNGE_SIG_MODE_PAM4_112:
		speeds = bnge_pam4_112_speeds2_masks;
		len = ARRAY_SIZE(bnge_pam4_112_speeds2_masks);
		break;
	default:
		return BNGE_LINK_SPEED_UNKNOWN;
	}

	for (idx = 0; idx < len; idx++) {
		if (speeds[idx] == speed_msk)
			return idx;
	}

	return BNGE_LINK_SPEED_UNKNOWN;
}

#define BNGE_FW_SPEED_MSK_BITS 16

static void
__bnge_get_ethtool_speeds(unsigned long fw_mask, enum bnge_media_type media,
			  u8 sig_mode, unsigned long *et_mask)
{
	enum ethtool_link_mode_bit_indices link_mode;
	enum bnge_link_speed_indices speed;
	u8 bit;

	for_each_set_bit(bit, &fw_mask, BNGE_FW_SPEED_MSK_BITS) {
		speed = bnge_encoding_speed_idx(sig_mode, 1 << bit);
		if (!speed)
			continue;

		link_mode = bnge_link_modes[speed][sig_mode][media];
		if (!link_mode)
			continue;

		linkmode_set_bit(link_mode, et_mask);
	}
}

static void
bnge_get_ethtool_speeds(unsigned long fw_mask, enum bnge_media_type media,
			u8 sig_mode, unsigned long *et_mask)
{
	if (media) {
		__bnge_get_ethtool_speeds(fw_mask, media, sig_mode, et_mask);
		return;
	}

	/* list speeds for all media if unknown */
	for (media = 1; media < __BNGE_MEDIA_END; media++)
		__bnge_get_ethtool_speeds(fw_mask, media, sig_mode, et_mask);
}

static void
bnge_get_all_ethtool_support_speeds(struct bnge_dev *bd,
				    enum bnge_media_type media,
				    struct ethtool_link_ksettings *lk_ksettings)
{
	u16 sp = bd->link_info.support_speeds2;

	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_NRZ,
				lk_ksettings->link_modes.supported);
	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_PAM4,
				lk_ksettings->link_modes.supported);
	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_PAM4_112,
				lk_ksettings->link_modes.supported);
}

static void
bnge_get_all_ethtool_adv_speeds(struct bnge_net *bn,
				enum bnge_media_type media,
				struct ethtool_link_ksettings *lk_ksettings)
{
	u16 sp = bn->eth_link_info.advertising;

	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_NRZ,
				lk_ksettings->link_modes.advertising);
	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_PAM4,
				lk_ksettings->link_modes.advertising);
	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_PAM4_112,
				lk_ksettings->link_modes.advertising);
}

static void
bnge_get_all_ethtool_lp_speeds(struct bnge_dev *bd,
			       enum bnge_media_type media,
			       struct ethtool_link_ksettings *lk_ksettings)
{
	u16 sp = bd->link_info.lp_auto_link_speeds;

	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_NRZ,
				lk_ksettings->link_modes.lp_advertising);
	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_PAM4,
				lk_ksettings->link_modes.lp_advertising);
	bnge_get_ethtool_speeds(sp, media, BNGE_SIG_MODE_PAM4_112,
				lk_ksettings->link_modes.lp_advertising);
}

static void bnge_update_speed(u32 *delta, bool installed_media, u16 *speeds,
			      u16 speed_msk, const unsigned long *et_mask,
			      enum ethtool_link_mode_bit_indices mode)
{
	bool mode_desired = linkmode_test_bit(mode, et_mask);

	if (!mode || !mode_desired)
		return;

	/* installed media takes priority; for non-installed media, only allow
	 * one change per fw_speed bit (many to one mapping).
	 */
	if (installed_media || !(*delta & speed_msk)) {
		*speeds |= speed_msk;
		*delta |= speed_msk;
	}
}

static void bnge_set_ethtool_speeds(struct bnge_net *bn,
				    const unsigned long *et_mask)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	enum bnge_media_type media;
	u32 delta_pam4_112 = 0;
	u32 delta_pam4 = 0;
	u32 delta_nrz = 0;
	int i, m;

	elink_info->advertising = 0;

	media = bnge_get_media(&bn->bd->link_info);
	for (i = 1; i < __BNGE_LINK_SPEED_END; i++) {
		/* accept any legal media from user */
		for (m = 1; m < __BNGE_MEDIA_END; m++) {
			bnge_update_speed(&delta_nrz, m == media,
					  &elink_info->advertising,
					  bnge_nrz_speeds2_masks[i], et_mask,
					  bnge_link_modes[i][BNGE_SIG_MODE_NRZ][m]);
			bnge_update_speed(&delta_pam4, m == media,
					  &elink_info->advertising,
					  bnge_pam4_speeds2_masks[i], et_mask,
					  bnge_link_modes[i][BNGE_SIG_MODE_PAM4][m]);
			bnge_update_speed(&delta_pam4_112, m == media,
					  &elink_info->advertising,
					  bnge_pam4_112_speeds2_masks[i],
					  et_mask,
					  bnge_link_modes[i][BNGE_SIG_MODE_PAM4_112][m]);
		}
	}
}

static void
bnge_fw_to_ethtool_advertised_fec(struct bnge_link_info *link_info,
				  struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fec_cfg = link_info->fec_cfg;

	if ((fec_cfg & BNGE_FEC_NONE) || !(fec_cfg & BNGE_FEC_AUTONEG)) {
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT,
				 lk_ksettings->link_modes.advertising);
		return;
	}
	if (fec_cfg & BNGE_FEC_ENC_BASE_R)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_BASER_BIT,
				 lk_ksettings->link_modes.advertising);
	if (fec_cfg & BNGE_FEC_ENC_RS)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT,
				 lk_ksettings->link_modes.advertising);
	if (fec_cfg & BNGE_FEC_ENC_LLRS)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_LLRS_BIT,
				 lk_ksettings->link_modes.advertising);
}

static void
bnge_fw_to_ethtool_support_fec(struct bnge_link_info *link_info,
			       struct ethtool_link_ksettings *lk_ksettings)
{
	u16 fec_cfg = link_info->fec_cfg;

	if (fec_cfg & BNGE_FEC_NONE) {
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT,
				 lk_ksettings->link_modes.supported);
		return;
	}
	if (fec_cfg & BNGE_FEC_ENC_BASE_R_CAP)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_BASER_BIT,
				 lk_ksettings->link_modes.supported);
	if (fec_cfg & BNGE_FEC_ENC_RS_CAP)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT,
				 lk_ksettings->link_modes.supported);
	if (fec_cfg & BNGE_FEC_ENC_LLRS_CAP)
		linkmode_set_bit(ETHTOOL_LINK_MODE_FEC_LLRS_BIT,
				 lk_ksettings->link_modes.supported);
}

static void bnge_get_default_speeds(struct bnge_net *bn,
				    struct ethtool_link_ksettings *lk_ksettings)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct ethtool_link_settings *base = &lk_ksettings->base;
	struct bnge_link_info *link_info;
	struct bnge_dev *bd = bn->bd;

	link_info = &bd->link_info;

	if (link_info->link_state == BNGE_LINK_STATE_UP) {
		base->speed = bnge_fw_to_ethtool_speed(link_info->link_speed);
		base->duplex = DUPLEX_HALF;
		if (link_info->duplex & BNGE_LINK_DUPLEX_FULL)
			base->duplex = DUPLEX_FULL;
		lk_ksettings->lanes = link_info->active_lanes;
	} else if (!elink_info->autoneg) {
		base->speed =
			bnge_fw_to_ethtool_speed(elink_info->req_link_speed);
		base->duplex = DUPLEX_HALF;
		if (elink_info->req_duplex == BNGE_LINK_DUPLEX_FULL)
			base->duplex = DUPLEX_FULL;
	}
}

int bnge_get_link_ksettings(struct net_device *dev,
			    struct ethtool_link_ksettings *lk_ksettings)
{
	struct ethtool_link_settings *base = &lk_ksettings->base;
	enum ethtool_link_mode_bit_indices link_mode;
	struct bnge_net *bn = netdev_priv(dev);
	struct bnge_link_info *link_info;
	struct bnge_dev *bd = bn->bd;
	enum bnge_media_type media;

	ethtool_link_ksettings_zero_link_mode(lk_ksettings, lp_advertising);
	ethtool_link_ksettings_zero_link_mode(lk_ksettings, advertising);
	ethtool_link_ksettings_zero_link_mode(lk_ksettings, supported);
	base->duplex = DUPLEX_UNKNOWN;
	base->speed = SPEED_UNKNOWN;
	link_info = &bd->link_info;

	bnge_get_ethtool_modes(bn, lk_ksettings);
	media = bnge_get_media(link_info);
	bnge_get_all_ethtool_support_speeds(bd, media, lk_ksettings);
	bnge_fw_to_ethtool_support_fec(link_info, lk_ksettings);
	link_mode = bnge_get_link_mode(bn);
	if (link_mode != BNGE_LINK_MODE_UNKNOWN)
		ethtool_params_from_link_mode(lk_ksettings, link_mode);
	else
		bnge_get_default_speeds(bn, lk_ksettings);

	if (bn->eth_link_info.autoneg) {
		bnge_fw_to_ethtool_advertised_fec(link_info, lk_ksettings);
		linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				 lk_ksettings->link_modes.advertising);
		base->autoneg = AUTONEG_ENABLE;
		bnge_get_all_ethtool_adv_speeds(bn, media, lk_ksettings);
		if (link_info->phy_link_status == BNGE_LINK_LINK)
			bnge_get_all_ethtool_lp_speeds(bd, media, lk_ksettings);
	} else {
		base->autoneg = AUTONEG_DISABLE;
	}

	linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			 lk_ksettings->link_modes.supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			 lk_ksettings->link_modes.advertising);

	if (link_info->media_type == PORT_PHY_QCFG_RESP_MEDIA_TYPE_DAC)
		base->port = PORT_DA;
	else
		base->port = PORT_FIBRE;
	base->phy_address = link_info->phy_addr;

	return 0;
}

static bool bnge_lanes_match(u32 user_lanes, u32 supported_lanes)
{
	/* 0 means lanes unspecified (auto) */
	return !user_lanes || user_lanes == supported_lanes;
}

static int
bnge_force_link_speed(struct net_device *dev, u32 ethtool_speed, u32 user_lanes)
{
	struct bnge_ethtool_link_info *elink_info;
	struct bnge_net *bn = netdev_priv(dev);
	u8 sig_mode = BNGE_SIG_MODE_NRZ;
	u16 support_spds2;
	u16 fw_speed = 0;

	elink_info = &bn->eth_link_info;
	support_spds2 = bn->bd->link_info.support_speeds2;

	switch (ethtool_speed) {
	case SPEED_50000:
		if (bnge_lanes_match(user_lanes, 1) &&
		    (support_spds2 & BNGE_LINK_SPEEDS2_MSK_50GB_PAM4)) {
			fw_speed = BNGE_LINK_SPEED_50GB_PAM4;
			sig_mode = BNGE_SIG_MODE_PAM4;
		}
		break;
	case SPEED_100000:
		if (bnge_lanes_match(user_lanes, 4) &&
		    (support_spds2 & BNGE_LINK_SPEEDS2_MSK_100GB)) {
			fw_speed = PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_100GB;
		} else if (bnge_lanes_match(user_lanes, 2) &&
			   (support_spds2 & BNGE_LINK_SPEEDS2_MSK_100GB_PAM4)) {
			fw_speed = BNGE_LINK_SPEED_100GB_PAM4;
			sig_mode = BNGE_SIG_MODE_PAM4;
		} else if (bnge_lanes_match(user_lanes, 1) &&
			   (support_spds2 & BNGE_LINK_SPEEDS2_MSK_100GB_PAM4_112)) {
			fw_speed = BNGE_LINK_SPEED_100GB_PAM4_112;
			sig_mode = BNGE_SIG_MODE_PAM4_112;
		}
		break;
	case SPEED_200000:
		if (bnge_lanes_match(user_lanes, 4) &&
		    (support_spds2 & BNGE_LINK_SPEEDS2_MSK_200GB_PAM4)) {
			fw_speed = BNGE_LINK_SPEED_200GB_PAM4;
			sig_mode = BNGE_SIG_MODE_PAM4;
		} else if (bnge_lanes_match(user_lanes, 2) &&
			   (support_spds2 & BNGE_LINK_SPEEDS2_MSK_200GB_PAM4_112)) {
			fw_speed = BNGE_LINK_SPEED_200GB_PAM4_112;
			sig_mode = BNGE_SIG_MODE_PAM4_112;
		}
		break;
	case SPEED_400000:
		if (bnge_lanes_match(user_lanes, 8) &&
		    (support_spds2 & BNGE_LINK_SPEEDS2_MSK_400GB_PAM4)) {
			fw_speed = BNGE_LINK_SPEED_400GB_PAM4;
			sig_mode = BNGE_SIG_MODE_PAM4;
		} else if (bnge_lanes_match(user_lanes, 4) &&
			   (support_spds2 & BNGE_LINK_SPEEDS2_MSK_400GB_PAM4_112)) {
			fw_speed = BNGE_LINK_SPEED_400GB_PAM4_112;
			sig_mode = BNGE_SIG_MODE_PAM4_112;
		}
		break;
	case SPEED_800000:
		if (bnge_lanes_match(user_lanes, 8) &&
		    (support_spds2 & BNGE_LINK_SPEEDS2_MSK_800GB_PAM4_112)) {
			fw_speed = BNGE_LINK_SPEED_800GB_PAM4_112;
			sig_mode = BNGE_SIG_MODE_PAM4_112;
		}
		break;
	default:
		break;
	}

	if (!fw_speed) {
		if (user_lanes)
			netdev_err(dev, "unsupported speed or number of lanes!\n");
		else
			netdev_err(dev, "unsupported speed!\n");
		return -EINVAL;
	}

	if (elink_info->req_link_speed == fw_speed &&
	    elink_info->req_signal_mode == sig_mode &&
	    elink_info->autoneg == 0)
		return -EALREADY;

	elink_info->req_link_speed = fw_speed;
	elink_info->req_signal_mode = sig_mode;
	elink_info->req_duplex = BNGE_LINK_DUPLEX_FULL;
	elink_info->autoneg = 0;
	elink_info->advertising = 0;

	return 0;
}

int bnge_set_link_ksettings(struct net_device *dev,
			    const struct ethtool_link_ksettings *lk_ksettings)
{
	const struct ethtool_link_settings *base = &lk_ksettings->base;
	struct bnge_ethtool_link_info old_elink_info;
	struct bnge_ethtool_link_info *elink_info;
	struct bnge_net *bn = netdev_priv(dev);
	struct bnge_dev *bd = bn->bd;
	bool set_pause = false;
	int rc = 0;

	if (!BNGE_PHY_CFG_ABLE(bd))
		return -EOPNOTSUPP;

	elink_info = &bn->eth_link_info;
	old_elink_info = *elink_info;

	if (base->autoneg == AUTONEG_ENABLE) {
		bnge_set_ethtool_speeds(bn,
					lk_ksettings->link_modes.advertising);
		elink_info->autoneg |= BNGE_AUTONEG_SPEED;
		if (!elink_info->advertising)
			bnge_set_default_adv_speeds(bn);
		/* any change to autoneg will cause link change, therefore the
		 * driver should put back the original pause setting in autoneg
		 */
		if (!(bd->phy_flags & BNGE_PHY_FL_NO_PAUSE))
			set_pause = true;
	} else {
		if (base->duplex == DUPLEX_HALF) {
			netdev_err(dev, "HALF DUPLEX is not supported!\n");
			rc = -EINVAL;
			goto set_setting_exit;
		}
		rc = bnge_force_link_speed(dev, base->speed,
					   lk_ksettings->lanes);
		if (rc) {
			if (rc == -EALREADY)
				rc = 0;
			goto set_setting_exit;
		}
	}

	if (netif_running(dev)) {
		rc = bnge_hwrm_set_link_setting(bn, set_pause);
		if (rc)
			*elink_info = old_elink_info;
	}

set_setting_exit:
	return rc;
}

void bnge_link_async_event_process(struct bnge_net *bn, u16 event_id)
{
	switch (event_id) {
	case ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_CHANGE:
		set_bit(BNGE_LINK_SPEED_CHNG_SP_EVENT, &bn->sp_event);
		break;
	case ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CHANGE:
	case ASYNC_EVENT_CMPL_EVENT_ID_PORT_PHY_CFG_CHANGE:
		set_bit(BNGE_LINK_CFG_CHANGE_SP_EVENT, &bn->sp_event);
		break;
	case ASYNC_EVENT_CMPL_EVENT_ID_LINK_STATUS_CHANGE:
		set_bit(BNGE_LINK_CHNG_SP_EVENT, &bn->sp_event);
		break;
	default:
		break;
	}
}
