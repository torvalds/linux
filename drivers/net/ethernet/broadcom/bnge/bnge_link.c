// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2026 Broadcom.

#include <linux/linkmode.h>

#include "bnge.h"
#include "bnge_link.h"
#include "bnge_hwrm_lib.h"

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
