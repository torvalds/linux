// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/module.h>
#include <linux/phy/phy.h>
#include <net/dcbnl.h>

#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_port.h"

#define SPX5_ETYPE_TAG_C     0x8100
#define SPX5_ETYPE_TAG_S     0x88a8

#define SPX5_WAIT_US         1000
#define SPX5_WAIT_MAX_US     2000

enum port_error {
	SPX5_PERR_SPEED,
	SPX5_PERR_IFTYPE,
};

#define PAUSE_DISCARD        0xC
#define ETH_MAXLEN           (ETH_DATA_LEN + ETH_HLEN + ETH_FCS_LEN)

static void decode_sgmii_word(u16 lp_abil, struct sparx5_port_status *status)
{
	status->an_complete = true;
	if (!(lp_abil & LPA_SGMII_LINK)) {
		status->link = false;
		return;
	}

	switch (lp_abil & LPA_SGMII_SPD_MASK) {
	case LPA_SGMII_10:
		status->speed = SPEED_10;
		break;
	case LPA_SGMII_100:
		status->speed = SPEED_100;
		break;
	case LPA_SGMII_1000:
		status->speed = SPEED_1000;
		break;
	default:
		status->link = false;
		return;
	}
	if (lp_abil & LPA_SGMII_FULL_DUPLEX)
		status->duplex = DUPLEX_FULL;
	else
		status->duplex = DUPLEX_HALF;
}

static void decode_cl37_word(u16 lp_abil, uint16_t ld_abil, struct sparx5_port_status *status)
{
	status->link = !(lp_abil & ADVERTISE_RFAULT) && status->link;
	status->an_complete = true;
	status->duplex = (ADVERTISE_1000XFULL & lp_abil) ?
		DUPLEX_FULL : DUPLEX_UNKNOWN; // 1G HDX not supported

	if ((ld_abil & ADVERTISE_1000XPAUSE) &&
	    (lp_abil & ADVERTISE_1000XPAUSE)) {
		status->pause = MLO_PAUSE_RX | MLO_PAUSE_TX;
	} else if ((ld_abil & ADVERTISE_1000XPSE_ASYM) &&
		   (lp_abil & ADVERTISE_1000XPSE_ASYM)) {
		status->pause |= (lp_abil & ADVERTISE_1000XPAUSE) ?
			MLO_PAUSE_TX : 0;
		status->pause |= (ld_abil & ADVERTISE_1000XPAUSE) ?
			MLO_PAUSE_RX : 0;
	} else {
		status->pause = MLO_PAUSE_NONE;
	}
}

static int sparx5_get_dev2g5_status(struct sparx5 *sparx5,
				    struct sparx5_port *port,
				    struct sparx5_port_status *status)
{
	u32 portno = port->portno;
	u16 lp_adv, ld_adv;
	u32 value;

	/* Get PCS Link down sticky */
	value = spx5_rd(sparx5, DEV2G5_PCS1G_STICKY(portno));
	status->link_down = DEV2G5_PCS1G_STICKY_LINK_DOWN_STICKY_GET(value);
	if (status->link_down)	/* Clear the sticky */
		spx5_wr(value, sparx5, DEV2G5_PCS1G_STICKY(portno));

	/* Get both current Link and Sync status */
	value = spx5_rd(sparx5, DEV2G5_PCS1G_LINK_STATUS(portno));
	status->link = DEV2G5_PCS1G_LINK_STATUS_LINK_STATUS_GET(value) &&
		       DEV2G5_PCS1G_LINK_STATUS_SYNC_STATUS_GET(value);

	if (port->conf.portmode == PHY_INTERFACE_MODE_1000BASEX)
		status->speed = SPEED_1000;
	else if (port->conf.portmode == PHY_INTERFACE_MODE_2500BASEX)
		status->speed = SPEED_2500;

	status->duplex = DUPLEX_FULL;

	/* Get PCS ANEG status register */
	value = spx5_rd(sparx5, DEV2G5_PCS1G_ANEG_STATUS(portno));

	/* Aneg complete provides more information  */
	if (DEV2G5_PCS1G_ANEG_STATUS_ANEG_COMPLETE_GET(value)) {
		lp_adv = DEV2G5_PCS1G_ANEG_STATUS_LP_ADV_ABILITY_GET(value);
		if (port->conf.portmode == PHY_INTERFACE_MODE_SGMII) {
			decode_sgmii_word(lp_adv, status);
		} else {
			value = spx5_rd(sparx5, DEV2G5_PCS1G_ANEG_CFG(portno));
			ld_adv = DEV2G5_PCS1G_ANEG_CFG_ADV_ABILITY_GET(value);
			decode_cl37_word(lp_adv, ld_adv, status);
		}
	}
	return 0;
}

static int sparx5_get_sfi_status(struct sparx5 *sparx5,
				 struct sparx5_port *port,
				 struct sparx5_port_status *status)
{
	bool high_speed_dev = sparx5_is_baser(port->conf.portmode);
	u32 portno = port->portno;
	u32 value, dev, tinst;
	void __iomem *inst;

	if (!high_speed_dev) {
		netdev_err(port->ndev, "error: low speed and SFI mode\n");
		return -EINVAL;
	}

	dev = sparx5_to_high_dev(portno);
	tinst = sparx5_port_dev_index(portno);
	inst = spx5_inst_get(sparx5, dev, tinst);

	value = spx5_inst_rd(inst, DEV10G_MAC_TX_MONITOR_STICKY(0));
	if (value != DEV10G_MAC_TX_MONITOR_STICKY_IDLE_STATE_STICKY) {
		/* The link is or has been down. Clear the sticky bit */
		status->link_down = 1;
		spx5_inst_wr(0xffffffff, inst, DEV10G_MAC_TX_MONITOR_STICKY(0));
		value = spx5_inst_rd(inst, DEV10G_MAC_TX_MONITOR_STICKY(0));
	}
	status->link = (value == DEV10G_MAC_TX_MONITOR_STICKY_IDLE_STATE_STICKY);
	status->duplex = DUPLEX_FULL;
	if (port->conf.portmode == PHY_INTERFACE_MODE_5GBASER)
		status->speed = SPEED_5000;
	else if (port->conf.portmode == PHY_INTERFACE_MODE_10GBASER)
		status->speed = SPEED_10000;
	else
		status->speed = SPEED_25000;

	return 0;
}

/* Get link status of 1000Base-X/in-band and SFI ports.
 */
int sparx5_get_port_status(struct sparx5 *sparx5,
			   struct sparx5_port *port,
			   struct sparx5_port_status *status)
{
	memset(status, 0, sizeof(*status));
	status->speed = port->conf.speed;
	if (port->conf.power_down) {
		status->link = false;
		return 0;
	}
	switch (port->conf.portmode) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return sparx5_get_dev2g5_status(sparx5, port, status);
	case PHY_INTERFACE_MODE_5GBASER:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_25GBASER:
		return sparx5_get_sfi_status(sparx5, port, status);
	case PHY_INTERFACE_MODE_NA:
		return 0;
	default:
		netdev_err(port->ndev, "Status not supported");
		return -ENODEV;
	}
	return 0;
}

static int sparx5_port_error(struct sparx5_port *port,
			     struct sparx5_port_config *conf,
			     enum port_error errtype)
{
	switch (errtype) {
	case SPX5_PERR_SPEED:
		netdev_err(port->ndev,
			   "Interface does not support speed: %u: for %s\n",
			   conf->speed, phy_modes(conf->portmode));
		break;
	case SPX5_PERR_IFTYPE:
		netdev_err(port->ndev,
			   "Switch port does not support interface type: %s\n",
			   phy_modes(conf->portmode));
		break;
	default:
		netdev_err(port->ndev,
			   "Interface configuration error\n");
	}

	return -EINVAL;
}

static int sparx5_port_verify_speed(struct sparx5 *sparx5,
				    struct sparx5_port *port,
				    struct sparx5_port_config *conf)
{
	if ((sparx5_port_is_2g5(port->portno) &&
	     conf->speed > SPEED_2500) ||
	    (sparx5_port_is_5g(port->portno)  &&
	     conf->speed > SPEED_5000) ||
	    (sparx5_port_is_10g(port->portno) &&
	     conf->speed > SPEED_10000))
		return sparx5_port_error(port, conf, SPX5_PERR_SPEED);

	switch (conf->portmode) {
	case PHY_INTERFACE_MODE_NA:
		return -EINVAL;
	case PHY_INTERFACE_MODE_1000BASEX:
		if (conf->speed != SPEED_1000 ||
		    sparx5_port_is_2g5(port->portno))
			return sparx5_port_error(port, conf, SPX5_PERR_SPEED);
		if (sparx5_port_is_2g5(port->portno))
			return sparx5_port_error(port, conf, SPX5_PERR_IFTYPE);
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		if (conf->speed != SPEED_2500 ||
		    sparx5_port_is_2g5(port->portno))
			return sparx5_port_error(port, conf, SPX5_PERR_SPEED);
		break;
	case PHY_INTERFACE_MODE_QSGMII:
		if (port->portno > 47)
			return sparx5_port_error(port, conf, SPX5_PERR_IFTYPE);
		fallthrough;
	case PHY_INTERFACE_MODE_SGMII:
		if (conf->speed != SPEED_1000 &&
		    conf->speed != SPEED_100 &&
		    conf->speed != SPEED_10 &&
		    conf->speed != SPEED_2500)
			return sparx5_port_error(port, conf, SPX5_PERR_SPEED);
		break;
	case PHY_INTERFACE_MODE_5GBASER:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_25GBASER:
		if ((conf->speed != SPEED_5000 &&
		     conf->speed != SPEED_10000 &&
		     conf->speed != SPEED_25000))
			return sparx5_port_error(port, conf, SPX5_PERR_SPEED);
		break;
	default:
		return sparx5_port_error(port, conf, SPX5_PERR_IFTYPE);
	}
	return 0;
}

static bool sparx5_dev_change(struct sparx5 *sparx5,
			      struct sparx5_port *port,
			      struct sparx5_port_config *conf)
{
	return sparx5_is_baser(port->conf.portmode) ^
		sparx5_is_baser(conf->portmode);
}

static int sparx5_port_flush_poll(struct sparx5 *sparx5, u32 portno)
{
	u32  value, resource, prio, delay_cnt = 0;
	bool poll_src = true;
	char *mem = "";

	/* Resource == 0: Memory tracked per source (SRC-MEM)
	 * Resource == 1: Frame references tracked per source (SRC-REF)
	 * Resource == 2: Memory tracked per destination (DST-MEM)
	 * Resource == 3: Frame references tracked per destination. (DST-REF)
	 */
	while (1) {
		bool empty = true;

		for (resource = 0; resource < (poll_src ? 2 : 1); resource++) {
			u32 base;

			base = (resource == 0 ? 2048 : 0) + SPX5_PRIOS * portno;
			for (prio = 0; prio < SPX5_PRIOS; prio++) {
				value = spx5_rd(sparx5,
						QRES_RES_STAT(base + prio));
				if (value) {
					mem = resource == 0 ?
						"DST-MEM" : "SRC-MEM";
					empty = false;
				}
			}
		}

		if (empty)
			break;

		if (delay_cnt++ == 2000) {
			dev_err(sparx5->dev,
				"Flush timeout port %u. %s queue not empty\n",
				portno, mem);
			return -EINVAL;
		}

		usleep_range(SPX5_WAIT_US, SPX5_WAIT_MAX_US);
	}
	return 0;
}

static int sparx5_port_disable(struct sparx5 *sparx5, struct sparx5_port *port, bool high_spd_dev)
{
	u32 tinst = high_spd_dev ?
		    sparx5_port_dev_index(port->portno) : port->portno;
	u32 dev = high_spd_dev ?
		  sparx5_to_high_dev(port->portno) : TARGET_DEV2G5;
	void __iomem *devinst = spx5_inst_get(sparx5, dev, tinst);
	u32 spd = port->conf.speed;
	u32 spd_prm;
	int err;

	if (high_spd_dev) {
		/* 1: Reset the PCS Rx clock domain  */
		spx5_inst_rmw(DEV10G_DEV_RST_CTRL_PCS_RX_RST,
			      DEV10G_DEV_RST_CTRL_PCS_RX_RST,
			      devinst,
			      DEV10G_DEV_RST_CTRL(0));

		/* 2: Disable MAC frame reception */
		spx5_inst_rmw(0,
			      DEV10G_MAC_ENA_CFG_RX_ENA,
			      devinst,
			      DEV10G_MAC_ENA_CFG(0));
	} else {
		/* 1: Reset the PCS Rx clock domain  */
		spx5_inst_rmw(DEV2G5_DEV_RST_CTRL_PCS_RX_RST,
			      DEV2G5_DEV_RST_CTRL_PCS_RX_RST,
			      devinst,
			      DEV2G5_DEV_RST_CTRL(0));
		/* 2: Disable MAC frame reception */
		spx5_inst_rmw(0,
			      DEV2G5_MAC_ENA_CFG_RX_ENA,
			      devinst,
			      DEV2G5_MAC_ENA_CFG(0));
	}
	/* 3: Disable traffic being sent to or from switch port->portno */
	spx5_rmw(0,
		 QFWD_SWITCH_PORT_MODE_PORT_ENA,
		 sparx5,
		 QFWD_SWITCH_PORT_MODE(port->portno));

	/* 4: Disable dequeuing from the egress queues  */
	spx5_rmw(HSCH_PORT_MODE_DEQUEUE_DIS,
		 HSCH_PORT_MODE_DEQUEUE_DIS,
		 sparx5,
		 HSCH_PORT_MODE(port->portno));

	/* 5: Disable Flowcontrol */
	spx5_rmw(QSYS_PAUSE_CFG_PAUSE_STOP_SET(0xFFF - 1),
		 QSYS_PAUSE_CFG_PAUSE_STOP,
		 sparx5,
		 QSYS_PAUSE_CFG(port->portno));

	spd_prm = spd == SPEED_10 ? 1000 : spd == SPEED_100 ? 100 : 10;
	/* 6: Wait while the last frame is exiting the queues */
	usleep_range(8 * spd_prm, 10 * spd_prm);

	/* 7: Flush the queues accociated with the port->portno */
	spx5_rmw(HSCH_FLUSH_CTRL_FLUSH_PORT_SET(port->portno) |
		 HSCH_FLUSH_CTRL_FLUSH_DST_SET(1) |
		 HSCH_FLUSH_CTRL_FLUSH_SRC_SET(1) |
		 HSCH_FLUSH_CTRL_FLUSH_ENA_SET(1),
		 HSCH_FLUSH_CTRL_FLUSH_PORT |
		 HSCH_FLUSH_CTRL_FLUSH_DST |
		 HSCH_FLUSH_CTRL_FLUSH_SRC |
		 HSCH_FLUSH_CTRL_FLUSH_ENA,
		 sparx5,
		 HSCH_FLUSH_CTRL);

	/* 8: Enable dequeuing from the egress queues */
	spx5_rmw(0,
		 HSCH_PORT_MODE_DEQUEUE_DIS,
		 sparx5,
		 HSCH_PORT_MODE(port->portno));

	/* 9: Wait until flushing is complete */
	err = sparx5_port_flush_poll(sparx5, port->portno);
	if (err)
		return err;

	/* 10: Reset the  MAC clock domain */
	if (high_spd_dev) {
		spx5_inst_rmw(DEV10G_DEV_RST_CTRL_PCS_TX_RST_SET(1) |
			      DEV10G_DEV_RST_CTRL_MAC_RX_RST_SET(1) |
			      DEV10G_DEV_RST_CTRL_MAC_TX_RST_SET(1),
			      DEV10G_DEV_RST_CTRL_PCS_TX_RST |
			      DEV10G_DEV_RST_CTRL_MAC_RX_RST |
			      DEV10G_DEV_RST_CTRL_MAC_TX_RST,
			      devinst,
			      DEV10G_DEV_RST_CTRL(0));

	} else {
		spx5_inst_rmw(DEV2G5_DEV_RST_CTRL_SPEED_SEL_SET(3) |
			      DEV2G5_DEV_RST_CTRL_PCS_TX_RST_SET(1) |
			      DEV2G5_DEV_RST_CTRL_PCS_RX_RST_SET(1) |
			      DEV2G5_DEV_RST_CTRL_MAC_TX_RST_SET(1) |
			      DEV2G5_DEV_RST_CTRL_MAC_RX_RST_SET(1),
			      DEV2G5_DEV_RST_CTRL_SPEED_SEL |
			      DEV2G5_DEV_RST_CTRL_PCS_TX_RST |
			      DEV2G5_DEV_RST_CTRL_PCS_RX_RST |
			      DEV2G5_DEV_RST_CTRL_MAC_TX_RST |
			      DEV2G5_DEV_RST_CTRL_MAC_RX_RST,
			      devinst,
			      DEV2G5_DEV_RST_CTRL(0));
	}
	/* 11: Clear flushing */
	spx5_rmw(HSCH_FLUSH_CTRL_FLUSH_PORT_SET(port->portno) |
		 HSCH_FLUSH_CTRL_FLUSH_ENA_SET(0),
		 HSCH_FLUSH_CTRL_FLUSH_PORT |
		 HSCH_FLUSH_CTRL_FLUSH_ENA,
		 sparx5,
		 HSCH_FLUSH_CTRL);

	if (high_spd_dev) {
		u32 pcs = sparx5_to_pcs_dev(port->portno);
		void __iomem *pcsinst = spx5_inst_get(sparx5, pcs, tinst);

		/* 12: Disable 5G/10G/25 BaseR PCS */
		spx5_inst_rmw(PCS10G_BR_PCS_CFG_PCS_ENA_SET(0),
			      PCS10G_BR_PCS_CFG_PCS_ENA,
			      pcsinst,
			      PCS10G_BR_PCS_CFG(0));

		if (sparx5_port_is_25g(port->portno))
			/* Disable 25G PCS */
			spx5_rmw(DEV25G_PCS25G_CFG_PCS25G_ENA_SET(0),
				 DEV25G_PCS25G_CFG_PCS25G_ENA,
				 sparx5,
				 DEV25G_PCS25G_CFG(tinst));
	} else {
		/* 12: Disable 1G PCS */
		spx5_rmw(DEV2G5_PCS1G_CFG_PCS_ENA_SET(0),
			 DEV2G5_PCS1G_CFG_PCS_ENA,
			 sparx5,
			 DEV2G5_PCS1G_CFG(port->portno));
	}

	/* The port is now flushed and disabled  */
	return 0;
}

static int sparx5_port_fifo_sz(struct sparx5 *sparx5,
			       u32 portno, u32 speed)
{
	u32 sys_clk = sparx5_clk_period(sparx5->coreclock);
	const u32 taxi_dist[SPX5_PORTS_ALL] = {
		6, 8, 10, 6, 8, 10, 6, 8, 10, 6, 8, 10,
		4, 4, 4, 4,
		11, 12, 13, 14, 15, 16, 17, 18,
		11, 12, 13, 14, 15, 16, 17, 18,
		11, 12, 13, 14, 15, 16, 17, 18,
		11, 12, 13, 14, 15, 16, 17, 18,
		4, 6, 8, 4, 6, 8, 6, 8,
		2, 2, 2, 2, 2, 2, 2, 4, 2
	};
	u32 mac_per    = 6400, tmp1, tmp2, tmp3;
	u32 fifo_width = 16;
	u32 mac_width  = 8;
	u32 addition   = 0;

	switch (speed) {
	case SPEED_25000:
		return 0;
	case SPEED_10000:
		mac_per = 6400;
		mac_width = 8;
		addition = 1;
		break;
	case SPEED_5000:
		mac_per = 12800;
		mac_width = 8;
		addition = 0;
		break;
	case SPEED_2500:
		mac_per = 3200;
		mac_width = 1;
		addition = 0;
		break;
	case SPEED_1000:
		mac_per =  8000;
		mac_width = 1;
		addition = 0;
		break;
	case SPEED_100:
	case SPEED_10:
		return 1;
	default:
		break;
	}

	tmp1 = 1000 * mac_width / fifo_width;
	tmp2 = 3000 + ((12000 + 2 * taxi_dist[portno] * 1000)
		       * sys_clk / mac_per);
	tmp3 = tmp1 * tmp2 / 1000;
	return  (tmp3 + 2000 + 999) / 1000 + addition;
}

/* Configure port muxing:
 * QSGMII:     4x2G5 devices
 */
static int sparx5_port_mux_set(struct sparx5 *sparx5,
			       struct sparx5_port *port,
			       struct sparx5_port_config *conf)
{
	u32 portno = port->portno;
	u32 inst;

	if (port->conf.portmode == conf->portmode)
		return 0; /* Nothing to do */

	switch (conf->portmode) {
	case PHY_INTERFACE_MODE_QSGMII: /* QSGMII: 4x2G5 devices. Mode Q'  */
		inst = (portno - portno % 4) / 4;
		spx5_rmw(BIT(inst),
			 BIT(inst),
			 sparx5,
			 PORT_CONF_QSGMII_ENA);

		if ((portno / 4 % 2) == 0) {
			/* Affects d0-d3,d8-d11..d40-d43 */
			spx5_rmw(PORT_CONF_USGMII_CFG_BYPASS_SCRAM_SET(1) |
				 PORT_CONF_USGMII_CFG_BYPASS_DESCRAM_SET(1) |
				 PORT_CONF_USGMII_CFG_QUAD_MODE_SET(1),
				 PORT_CONF_USGMII_CFG_BYPASS_SCRAM |
				 PORT_CONF_USGMII_CFG_BYPASS_DESCRAM |
				 PORT_CONF_USGMII_CFG_QUAD_MODE,
				 sparx5,
				 PORT_CONF_USGMII_CFG((portno / 8)));
		}
		break;
	default:
		break;
	}
	return 0;
}

static int sparx5_port_max_tags_set(struct sparx5 *sparx5,
				    struct sparx5_port *port)
{
	enum sparx5_port_max_tags max_tags    = port->max_vlan_tags;
	int tag_ct          = max_tags == SPX5_PORT_MAX_TAGS_ONE ? 1 :
			      max_tags == SPX5_PORT_MAX_TAGS_TWO ? 2 : 0;
	bool dtag           = max_tags == SPX5_PORT_MAX_TAGS_TWO;
	enum sparx5_vlan_port_type vlan_type  = port->vlan_type;
	bool dotag          = max_tags != SPX5_PORT_MAX_TAGS_NONE;
	u32 dev             = sparx5_to_high_dev(port->portno);
	u32 tinst           = sparx5_port_dev_index(port->portno);
	void __iomem *inst  = spx5_inst_get(sparx5, dev, tinst);
	u32 etype;

	etype = (vlan_type == SPX5_VLAN_PORT_TYPE_S_CUSTOM ?
		 port->custom_etype :
		 vlan_type == SPX5_VLAN_PORT_TYPE_C ?
		 SPX5_ETYPE_TAG_C : SPX5_ETYPE_TAG_S);

	spx5_wr(DEV2G5_MAC_TAGS_CFG_TAG_ID_SET(etype) |
		DEV2G5_MAC_TAGS_CFG_PB_ENA_SET(dtag) |
		DEV2G5_MAC_TAGS_CFG_VLAN_AWR_ENA_SET(dotag) |
		DEV2G5_MAC_TAGS_CFG_VLAN_LEN_AWR_ENA_SET(dotag),
		sparx5,
		DEV2G5_MAC_TAGS_CFG(port->portno));

	if (sparx5_port_is_2g5(port->portno))
		return 0;

	spx5_inst_rmw(DEV10G_MAC_TAGS_CFG_TAG_ID_SET(etype) |
		      DEV10G_MAC_TAGS_CFG_TAG_ENA_SET(dotag),
		      DEV10G_MAC_TAGS_CFG_TAG_ID |
		      DEV10G_MAC_TAGS_CFG_TAG_ENA,
		      inst,
		      DEV10G_MAC_TAGS_CFG(0, 0));

	spx5_inst_rmw(DEV10G_MAC_NUM_TAGS_CFG_NUM_TAGS_SET(tag_ct),
		      DEV10G_MAC_NUM_TAGS_CFG_NUM_TAGS,
		      inst,
		      DEV10G_MAC_NUM_TAGS_CFG(0));

	spx5_inst_rmw(DEV10G_MAC_MAXLEN_CFG_MAX_LEN_TAG_CHK_SET(dotag),
		      DEV10G_MAC_MAXLEN_CFG_MAX_LEN_TAG_CHK,
		      inst,
		      DEV10G_MAC_MAXLEN_CFG(0));
	return 0;
}

int sparx5_port_fwd_urg(struct sparx5 *sparx5, u32 speed)
{
	u32 clk_period_ps = 1600; /* 625Mhz for now */
	u32 urg = 672000;

	switch (speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		urg = 672000;
		break;
	case SPEED_2500:
		urg = 270000;
		break;
	case SPEED_5000:
		urg = 135000;
		break;
	case SPEED_10000:
		urg = 67200;
		break;
	case SPEED_25000:
		urg = 27000;
		break;
	}
	return urg / clk_period_ps - 1;
}

static u16 sparx5_wm_enc(u16 value)
{
	if (value >= 2048)
		return 2048 + value / 16;

	return value;
}

static int sparx5_port_fc_setup(struct sparx5 *sparx5,
				struct sparx5_port *port,
				struct sparx5_port_config *conf)
{
	bool fc_obey = conf->pause & MLO_PAUSE_RX ? 1 : 0;
	u32 pause_stop = 0xFFF - 1; /* FC gen disabled */

	if (conf->pause & MLO_PAUSE_TX)
		pause_stop = sparx5_wm_enc(4  * (ETH_MAXLEN /
						 SPX5_BUFFER_CELL_SZ));

	/* Set HDX flowcontrol */
	spx5_rmw(DSM_MAC_CFG_HDX_BACKPREASSURE_SET(conf->duplex == DUPLEX_HALF),
		 DSM_MAC_CFG_HDX_BACKPREASSURE,
		 sparx5,
		 DSM_MAC_CFG(port->portno));

	/* Obey flowcontrol  */
	spx5_rmw(DSM_RX_PAUSE_CFG_RX_PAUSE_EN_SET(fc_obey),
		 DSM_RX_PAUSE_CFG_RX_PAUSE_EN,
		 sparx5,
		 DSM_RX_PAUSE_CFG(port->portno));

	/* Disable forward pressure */
	spx5_rmw(QSYS_FWD_PRESSURE_FWD_PRESSURE_DIS_SET(fc_obey),
		 QSYS_FWD_PRESSURE_FWD_PRESSURE_DIS,
		 sparx5,
		 QSYS_FWD_PRESSURE(port->portno));

	/* Generate pause frames */
	spx5_rmw(QSYS_PAUSE_CFG_PAUSE_STOP_SET(pause_stop),
		 QSYS_PAUSE_CFG_PAUSE_STOP,
		 sparx5,
		 QSYS_PAUSE_CFG(port->portno));

	return 0;
}

static u16 sparx5_get_aneg_word(struct sparx5_port_config *conf)
{
	if (conf->portmode == PHY_INTERFACE_MODE_1000BASEX) /* cl-37 aneg */
		return (conf->pause_adv | ADVERTISE_LPACK | ADVERTISE_1000XFULL);
	else
		return 1; /* Enable SGMII Aneg */
}

int sparx5_serdes_set(struct sparx5 *sparx5,
		      struct sparx5_port *port,
		      struct sparx5_port_config *conf)
{
	int portmode, err, speed = conf->speed;

	if (conf->portmode == PHY_INTERFACE_MODE_QSGMII &&
	    ((port->portno % 4) != 0)) {
		return 0;
	}
	if (sparx5_is_baser(conf->portmode)) {
		if (conf->portmode == PHY_INTERFACE_MODE_25GBASER)
			speed = SPEED_25000;
		else if (conf->portmode == PHY_INTERFACE_MODE_10GBASER)
			speed = SPEED_10000;
		else
			speed = SPEED_5000;
	}

	err = phy_set_media(port->serdes, conf->media);
	if (err)
		return err;
	if (speed > 0) {
		err = phy_set_speed(port->serdes, speed);
		if (err)
			return err;
	}
	if (conf->serdes_reset) {
		err = phy_reset(port->serdes);
		if (err)
			return err;
	}

	/* Configure SerDes with port parameters
	 * For BaseR, the serdes driver supports 10GGBASE-R and speed 5G/10G/25G
	 */
	portmode = conf->portmode;
	if (sparx5_is_baser(conf->portmode))
		portmode = PHY_INTERFACE_MODE_10GBASER;
	err = phy_set_mode_ext(port->serdes, PHY_MODE_ETHERNET, portmode);
	if (err)
		return err;
	conf->serdes_reset = false;
	return err;
}

static int sparx5_port_pcs_low_set(struct sparx5 *sparx5,
				   struct sparx5_port *port,
				   struct sparx5_port_config *conf)
{
	bool sgmii = false, inband_aneg = false;
	int err;

	if (port->conf.inband) {
		if (conf->portmode == PHY_INTERFACE_MODE_SGMII ||
		    conf->portmode == PHY_INTERFACE_MODE_QSGMII)
			inband_aneg = true; /* Cisco-SGMII in-band-aneg */
		else if (conf->portmode == PHY_INTERFACE_MODE_1000BASEX &&
			 conf->autoneg)
			inband_aneg = true; /* Clause-37 in-band-aneg */

		err = sparx5_serdes_set(sparx5, port, conf);
		if (err)
			return -EINVAL;
	} else {
		sgmii = true; /* Phy is connected to the MAC */
	}

	/* Choose SGMII or 1000BaseX/2500BaseX PCS mode */
	spx5_rmw(DEV2G5_PCS1G_MODE_CFG_SGMII_MODE_ENA_SET(sgmii),
		 DEV2G5_PCS1G_MODE_CFG_SGMII_MODE_ENA,
		 sparx5,
		 DEV2G5_PCS1G_MODE_CFG(port->portno));

	/* Enable PCS */
	spx5_wr(DEV2G5_PCS1G_CFG_PCS_ENA_SET(1),
		sparx5,
		DEV2G5_PCS1G_CFG(port->portno));

	if (inband_aneg) {
		u16 abil = sparx5_get_aneg_word(conf);

		/* Enable in-band aneg */
		spx5_wr(DEV2G5_PCS1G_ANEG_CFG_ADV_ABILITY_SET(abil) |
			DEV2G5_PCS1G_ANEG_CFG_SW_RESOLVE_ENA_SET(1) |
			DEV2G5_PCS1G_ANEG_CFG_ANEG_ENA_SET(1) |
			DEV2G5_PCS1G_ANEG_CFG_ANEG_RESTART_ONE_SHOT_SET(1),
			sparx5,
			DEV2G5_PCS1G_ANEG_CFG(port->portno));
	} else {
		spx5_wr(0, sparx5, DEV2G5_PCS1G_ANEG_CFG(port->portno));
	}

	/* Take PCS out of reset */
	spx5_rmw(DEV2G5_DEV_RST_CTRL_SPEED_SEL_SET(2) |
		 DEV2G5_DEV_RST_CTRL_PCS_TX_RST_SET(0) |
		 DEV2G5_DEV_RST_CTRL_PCS_RX_RST_SET(0),
		 DEV2G5_DEV_RST_CTRL_SPEED_SEL |
		 DEV2G5_DEV_RST_CTRL_PCS_TX_RST |
		 DEV2G5_DEV_RST_CTRL_PCS_RX_RST,
		 sparx5,
		 DEV2G5_DEV_RST_CTRL(port->portno));

	return 0;
}

static int sparx5_port_pcs_high_set(struct sparx5 *sparx5,
				    struct sparx5_port *port,
				    struct sparx5_port_config *conf)
{
	u32 clk_spd = conf->portmode == PHY_INTERFACE_MODE_5GBASER ? 1 : 0;
	u32 pix = sparx5_port_dev_index(port->portno);
	u32 dev = sparx5_to_high_dev(port->portno);
	u32 pcs = sparx5_to_pcs_dev(port->portno);
	void __iomem *devinst;
	void __iomem *pcsinst;
	int err;

	devinst = spx5_inst_get(sparx5, dev, pix);
	pcsinst = spx5_inst_get(sparx5, pcs, pix);

	/*  SFI : No in-band-aneg. Speeds 5G/10G/25G */
	err = sparx5_serdes_set(sparx5, port, conf);
	if (err)
		return -EINVAL;
	if (conf->portmode == PHY_INTERFACE_MODE_25GBASER) {
		/* Enable PCS for 25G device, speed 25G */
		spx5_rmw(DEV25G_PCS25G_CFG_PCS25G_ENA_SET(1),
			 DEV25G_PCS25G_CFG_PCS25G_ENA,
			 sparx5,
			 DEV25G_PCS25G_CFG(pix));
	} else {
		/* Enable PCS for 5G/10G/25G devices, speed 5G/10G */
		spx5_inst_rmw(PCS10G_BR_PCS_CFG_PCS_ENA_SET(1),
			      PCS10G_BR_PCS_CFG_PCS_ENA,
			      pcsinst,
			      PCS10G_BR_PCS_CFG(0));
	}

	/* Enable 5G/10G/25G MAC module */
	spx5_inst_wr(DEV10G_MAC_ENA_CFG_RX_ENA_SET(1) |
		     DEV10G_MAC_ENA_CFG_TX_ENA_SET(1),
		     devinst,
		     DEV10G_MAC_ENA_CFG(0));

	/* Take the device out of reset */
	spx5_inst_rmw(DEV10G_DEV_RST_CTRL_PCS_RX_RST_SET(0) |
		      DEV10G_DEV_RST_CTRL_PCS_TX_RST_SET(0) |
		      DEV10G_DEV_RST_CTRL_MAC_RX_RST_SET(0) |
		      DEV10G_DEV_RST_CTRL_MAC_TX_RST_SET(0) |
		      DEV10G_DEV_RST_CTRL_SPEED_SEL_SET(clk_spd),
		      DEV10G_DEV_RST_CTRL_PCS_RX_RST |
		      DEV10G_DEV_RST_CTRL_PCS_TX_RST |
		      DEV10G_DEV_RST_CTRL_MAC_RX_RST |
		      DEV10G_DEV_RST_CTRL_MAC_TX_RST |
		      DEV10G_DEV_RST_CTRL_SPEED_SEL,
		      devinst,
		      DEV10G_DEV_RST_CTRL(0));

	return 0;
}

/* Switch between 1G/2500 and 5G/10G/25G devices */
static void sparx5_dev_switch(struct sparx5 *sparx5, int port, bool hsd)
{
	int bt_indx = BIT(sparx5_port_dev_index(port));

	if (sparx5_port_is_5g(port)) {
		spx5_rmw(hsd ? 0 : bt_indx,
			 bt_indx,
			 sparx5,
			 PORT_CONF_DEV5G_MODES);
	} else if (sparx5_port_is_10g(port)) {
		spx5_rmw(hsd ? 0 : bt_indx,
			 bt_indx,
			 sparx5,
			 PORT_CONF_DEV10G_MODES);
	} else if (sparx5_port_is_25g(port)) {
		spx5_rmw(hsd ? 0 : bt_indx,
			 bt_indx,
			 sparx5,
			 PORT_CONF_DEV25G_MODES);
	}
}

/* Configure speed/duplex dependent registers */
static int sparx5_port_config_low_set(struct sparx5 *sparx5,
				      struct sparx5_port *port,
				      struct sparx5_port_config *conf)
{
	u32 clk_spd, gig_mode, tx_gap, hdx_gap_1, hdx_gap_2;
	bool fdx = conf->duplex == DUPLEX_FULL;
	int spd = conf->speed;

	clk_spd = spd == SPEED_10 ? 0 : spd == SPEED_100 ? 1 : 2;
	gig_mode = spd == SPEED_1000 || spd == SPEED_2500;
	tx_gap = spd == SPEED_1000 ? 4 : fdx ? 6 : 5;
	hdx_gap_1 = spd == SPEED_1000 ? 0 : spd == SPEED_100 ? 1 : 2;
	hdx_gap_2 = spd == SPEED_1000 ? 0 : spd == SPEED_100 ? 4 : 1;

	/* GIG/FDX mode */
	spx5_rmw(DEV2G5_MAC_MODE_CFG_GIGA_MODE_ENA_SET(gig_mode) |
		 DEV2G5_MAC_MODE_CFG_FDX_ENA_SET(fdx),
		 DEV2G5_MAC_MODE_CFG_GIGA_MODE_ENA |
		 DEV2G5_MAC_MODE_CFG_FDX_ENA,
		 sparx5,
		 DEV2G5_MAC_MODE_CFG(port->portno));

	/* Set MAC IFG Gaps */
	spx5_wr(DEV2G5_MAC_IFG_CFG_TX_IFG_SET(tx_gap) |
		DEV2G5_MAC_IFG_CFG_RX_IFG1_SET(hdx_gap_1) |
		DEV2G5_MAC_IFG_CFG_RX_IFG2_SET(hdx_gap_2),
		sparx5,
		DEV2G5_MAC_IFG_CFG(port->portno));

	/* Disabling frame aging when in HDX (due to HDX issue) */
	spx5_rmw(HSCH_PORT_MODE_AGE_DIS_SET(fdx == 0),
		 HSCH_PORT_MODE_AGE_DIS,
		 sparx5,
		 HSCH_PORT_MODE(port->portno));

	/* Enable MAC module */
	spx5_wr(DEV2G5_MAC_ENA_CFG_RX_ENA |
		DEV2G5_MAC_ENA_CFG_TX_ENA,
		sparx5,
		DEV2G5_MAC_ENA_CFG(port->portno));

	/* Select speed and take MAC out of reset */
	spx5_rmw(DEV2G5_DEV_RST_CTRL_SPEED_SEL_SET(clk_spd) |
		 DEV2G5_DEV_RST_CTRL_MAC_TX_RST_SET(0) |
		 DEV2G5_DEV_RST_CTRL_MAC_RX_RST_SET(0),
		 DEV2G5_DEV_RST_CTRL_SPEED_SEL |
		 DEV2G5_DEV_RST_CTRL_MAC_TX_RST |
		 DEV2G5_DEV_RST_CTRL_MAC_RX_RST,
		 sparx5,
		 DEV2G5_DEV_RST_CTRL(port->portno));

	return 0;
}

int sparx5_port_pcs_set(struct sparx5 *sparx5,
			struct sparx5_port *port,
			struct sparx5_port_config *conf)

{
	bool high_speed_dev = sparx5_is_baser(conf->portmode);
	int err;

	if (sparx5_dev_change(sparx5, port, conf)) {
		/* switch device */
		sparx5_dev_switch(sparx5, port->portno, high_speed_dev);

		/* Disable the not-in-use device */
		err = sparx5_port_disable(sparx5, port, !high_speed_dev);
		if (err)
			return err;
	}
	/* Disable the port before re-configuring */
	err = sparx5_port_disable(sparx5, port, high_speed_dev);
	if (err)
		return -EINVAL;

	if (high_speed_dev)
		err = sparx5_port_pcs_high_set(sparx5, port, conf);
	else
		err = sparx5_port_pcs_low_set(sparx5, port, conf);

	if (err)
		return -EINVAL;

	if (port->conf.inband) {
		/* Enable/disable 1G counters in ASM */
		spx5_rmw(ASM_PORT_CFG_CSC_STAT_DIS_SET(high_speed_dev),
			 ASM_PORT_CFG_CSC_STAT_DIS,
			 sparx5,
			 ASM_PORT_CFG(port->portno));

		/* Enable/disable 1G counters in DSM */
		spx5_rmw(DSM_BUF_CFG_CSC_STAT_DIS_SET(high_speed_dev),
			 DSM_BUF_CFG_CSC_STAT_DIS,
			 sparx5,
			 DSM_BUF_CFG(port->portno));
	}

	port->conf = *conf;

	return 0;
}

int sparx5_port_config(struct sparx5 *sparx5,
		       struct sparx5_port *port,
		       struct sparx5_port_config *conf)
{
	bool high_speed_dev = sparx5_is_baser(conf->portmode);
	int err, urgency, stop_wm;

	err = sparx5_port_verify_speed(sparx5, port, conf);
	if (err)
		return err;

	/* high speed device is already configured */
	if (!high_speed_dev)
		sparx5_port_config_low_set(sparx5, port, conf);

	/* Configure flow control */
	err = sparx5_port_fc_setup(sparx5, port, conf);
	if (err)
		return err;

	/* Set the DSM stop watermark */
	stop_wm = sparx5_port_fifo_sz(sparx5, port->portno, conf->speed);
	spx5_rmw(DSM_DEV_TX_STOP_WM_CFG_DEV_TX_STOP_WM_SET(stop_wm),
		 DSM_DEV_TX_STOP_WM_CFG_DEV_TX_STOP_WM,
		 sparx5,
		 DSM_DEV_TX_STOP_WM_CFG(port->portno));

	/* Enable port in queue system */
	urgency = sparx5_port_fwd_urg(sparx5, conf->speed);
	spx5_rmw(QFWD_SWITCH_PORT_MODE_PORT_ENA_SET(1) |
		 QFWD_SWITCH_PORT_MODE_FWD_URGENCY_SET(urgency),
		 QFWD_SWITCH_PORT_MODE_PORT_ENA |
		 QFWD_SWITCH_PORT_MODE_FWD_URGENCY,
		 sparx5,
		 QFWD_SWITCH_PORT_MODE(port->portno));

	/* Save the new values */
	port->conf = *conf;

	return 0;
}

/* Initialize port config to default */
int sparx5_port_init(struct sparx5 *sparx5,
		     struct sparx5_port *port,
		     struct sparx5_port_config *conf)
{
	u32 pause_start = sparx5_wm_enc(6  * (ETH_MAXLEN / SPX5_BUFFER_CELL_SZ));
	u32 atop = sparx5_wm_enc(20 * (ETH_MAXLEN / SPX5_BUFFER_CELL_SZ));
	u32 devhigh = sparx5_to_high_dev(port->portno);
	u32 pix = sparx5_port_dev_index(port->portno);
	u32 pcs = sparx5_to_pcs_dev(port->portno);
	bool sd_pol = port->signd_active_high;
	bool sd_sel = !port->signd_internal;
	bool sd_ena = port->signd_enable;
	u32 pause_stop = 0xFFF - 1; /* FC generate disabled */
	void __iomem *devinst;
	void __iomem *pcsinst;
	int err;

	devinst = spx5_inst_get(sparx5, devhigh, pix);
	pcsinst = spx5_inst_get(sparx5, pcs, pix);

	/* Set the mux port mode  */
	err = sparx5_port_mux_set(sparx5, port, conf);
	if (err)
		return err;

	/* Configure MAC vlan awareness */
	err = sparx5_port_max_tags_set(sparx5, port);
	if (err)
		return err;

	/* Set Max Length */
	spx5_rmw(DEV2G5_MAC_MAXLEN_CFG_MAX_LEN_SET(ETH_MAXLEN),
		 DEV2G5_MAC_MAXLEN_CFG_MAX_LEN,
		 sparx5,
		 DEV2G5_MAC_MAXLEN_CFG(port->portno));

	/* 1G/2G5: Signal Detect configuration */
	spx5_wr(DEV2G5_PCS1G_SD_CFG_SD_POL_SET(sd_pol) |
		DEV2G5_PCS1G_SD_CFG_SD_SEL_SET(sd_sel) |
		DEV2G5_PCS1G_SD_CFG_SD_ENA_SET(sd_ena),
		sparx5,
		DEV2G5_PCS1G_SD_CFG(port->portno));

	/* Set Pause WM hysteresis */
	spx5_rmw(QSYS_PAUSE_CFG_PAUSE_START_SET(pause_start) |
		 QSYS_PAUSE_CFG_PAUSE_STOP_SET(pause_stop) |
		 QSYS_PAUSE_CFG_PAUSE_ENA_SET(1),
		 QSYS_PAUSE_CFG_PAUSE_START |
		 QSYS_PAUSE_CFG_PAUSE_STOP |
		 QSYS_PAUSE_CFG_PAUSE_ENA,
		 sparx5,
		 QSYS_PAUSE_CFG(port->portno));

	/* Port ATOP. Frames are tail dropped when this WM is hit */
	spx5_wr(QSYS_ATOP_ATOP_SET(atop),
		sparx5,
		QSYS_ATOP(port->portno));

	/* Discard pause frame 01-80-C2-00-00-01 */
	spx5_wr(PAUSE_DISCARD, sparx5, ANA_CL_CAPTURE_BPDU_CFG(port->portno));

	if (conf->portmode == PHY_INTERFACE_MODE_QSGMII ||
	    conf->portmode == PHY_INTERFACE_MODE_SGMII) {
		err = sparx5_serdes_set(sparx5, port, conf);
		if (err)
			return err;

		if (!sparx5_port_is_2g5(port->portno))
			/* Enable shadow device */
			spx5_rmw(DSM_DEV_TX_STOP_WM_CFG_DEV10G_SHADOW_ENA_SET(1),
				 DSM_DEV_TX_STOP_WM_CFG_DEV10G_SHADOW_ENA,
				 sparx5,
				 DSM_DEV_TX_STOP_WM_CFG(port->portno));

		sparx5_dev_switch(sparx5, port->portno, false);
	}
	if (conf->portmode == PHY_INTERFACE_MODE_QSGMII) {
		// All ports must be PCS enabled in QSGMII mode
		spx5_rmw(DEV2G5_DEV_RST_CTRL_PCS_TX_RST_SET(0),
			 DEV2G5_DEV_RST_CTRL_PCS_TX_RST,
			 sparx5,
			 DEV2G5_DEV_RST_CTRL(port->portno));
	}
	/* Default IFGs for 1G */
	spx5_wr(DEV2G5_MAC_IFG_CFG_TX_IFG_SET(6) |
		DEV2G5_MAC_IFG_CFG_RX_IFG1_SET(0) |
		DEV2G5_MAC_IFG_CFG_RX_IFG2_SET(0),
		sparx5,
		DEV2G5_MAC_IFG_CFG(port->portno));

	if (sparx5_port_is_2g5(port->portno))
		return 0; /* Low speed device only - return */

	/* Now setup the high speed device */
	if (conf->portmode == PHY_INTERFACE_MODE_NA)
		conf->portmode = PHY_INTERFACE_MODE_10GBASER;

	if (sparx5_is_baser(conf->portmode))
		sparx5_dev_switch(sparx5, port->portno, true);

	/* Set Max Length */
	spx5_inst_rmw(DEV10G_MAC_MAXLEN_CFG_MAX_LEN_SET(ETH_MAXLEN),
		      DEV10G_MAC_MAXLEN_CFG_MAX_LEN,
		      devinst,
		      DEV10G_MAC_ENA_CFG(0));

	/* Handle Signal Detect in 10G PCS */
	spx5_inst_wr(PCS10G_BR_PCS_SD_CFG_SD_POL_SET(sd_pol) |
		     PCS10G_BR_PCS_SD_CFG_SD_SEL_SET(sd_sel) |
		     PCS10G_BR_PCS_SD_CFG_SD_ENA_SET(sd_ena),
		     pcsinst,
		     PCS10G_BR_PCS_SD_CFG(0));

	if (sparx5_port_is_25g(port->portno)) {
		/* Handle Signal Detect in 25G PCS */
		spx5_wr(DEV25G_PCS25G_SD_CFG_SD_POL_SET(sd_pol) |
			DEV25G_PCS25G_SD_CFG_SD_SEL_SET(sd_sel) |
			DEV25G_PCS25G_SD_CFG_SD_ENA_SET(sd_ena),
			sparx5,
			DEV25G_PCS25G_SD_CFG(pix));
	}

	return 0;
}

void sparx5_port_enable(struct sparx5_port *port, bool enable)
{
	struct sparx5 *sparx5 = port->sparx5;

	/* Enable port for frame transfer? */
	spx5_rmw(QFWD_SWITCH_PORT_MODE_PORT_ENA_SET(enable),
		 QFWD_SWITCH_PORT_MODE_PORT_ENA,
		 sparx5,
		 QFWD_SWITCH_PORT_MODE(port->portno));
}

int sparx5_port_qos_set(struct sparx5_port *port,
			struct sparx5_port_qos *qos)
{
	sparx5_port_qos_dscp_set(port, &qos->dscp);
	sparx5_port_qos_pcp_set(port, &qos->pcp);
	sparx5_port_qos_default_set(port, qos);

	return 0;
}

int sparx5_port_qos_pcp_set(const struct sparx5_port *port,
			    struct sparx5_port_qos_pcp *qos)
{
	struct sparx5 *sparx5 = port->sparx5;
	u8 *pcp_itr = qos->map.map;
	u8 pcp, dp;
	int i;

	/* Enable/disable pcp and dp for qos classification. */
	spx5_rmw(ANA_CL_QOS_CFG_PCP_DEI_QOS_ENA_SET(qos->qos_enable) |
		 ANA_CL_QOS_CFG_PCP_DEI_DP_ENA_SET(qos->dp_enable),
		 ANA_CL_QOS_CFG_PCP_DEI_QOS_ENA | ANA_CL_QOS_CFG_PCP_DEI_DP_ENA,
		 sparx5, ANA_CL_QOS_CFG(port->portno));

	/* Map each pcp and dei value to priority and dp */
	for (i = 0; i < ARRAY_SIZE(qos->map.map); i++) {
		pcp = *(pcp_itr + i);
		dp = (i < SPARX5_PORT_QOS_PCP_COUNT) ? 0 : 1;
		spx5_rmw(ANA_CL_PCP_DEI_MAP_CFG_PCP_DEI_QOS_VAL_SET(pcp) |
			 ANA_CL_PCP_DEI_MAP_CFG_PCP_DEI_DP_VAL_SET(dp),
			 ANA_CL_PCP_DEI_MAP_CFG_PCP_DEI_QOS_VAL |
			 ANA_CL_PCP_DEI_MAP_CFG_PCP_DEI_DP_VAL, sparx5,
			 ANA_CL_PCP_DEI_MAP_CFG(port->portno, i));
	}

	return 0;
}

int sparx5_port_qos_dscp_set(const struct sparx5_port *port,
			     struct sparx5_port_qos_dscp *qos)
{
	struct sparx5 *sparx5 = port->sparx5;
	u8 *dscp = qos->map.map;
	int i;

	/* Enable/disable dscp and dp for qos classification.
	 * Disable rewrite of dscp values for now.
	 */
	spx5_rmw(ANA_CL_QOS_CFG_DSCP_QOS_ENA_SET(qos->qos_enable) |
		 ANA_CL_QOS_CFG_DSCP_DP_ENA_SET(qos->dp_enable) |
		 ANA_CL_QOS_CFG_DSCP_KEEP_ENA_SET(1),
		 ANA_CL_QOS_CFG_DSCP_QOS_ENA | ANA_CL_QOS_CFG_DSCP_DP_ENA |
		 ANA_CL_QOS_CFG_DSCP_KEEP_ENA, sparx5,
		 ANA_CL_QOS_CFG(port->portno));

	/* Map each dscp value to priority and dp */
	for (i = 0; i < ARRAY_SIZE(qos->map.map); i++) {
		spx5_rmw(ANA_CL_DSCP_CFG_DSCP_QOS_VAL_SET(*(dscp + i)) |
			 ANA_CL_DSCP_CFG_DSCP_DP_VAL_SET(0),
			 ANA_CL_DSCP_CFG_DSCP_QOS_VAL |
			 ANA_CL_DSCP_CFG_DSCP_DP_VAL, sparx5,
			 ANA_CL_DSCP_CFG(i));
	}

	/* Set per-dscp trust */
	for (i = 0; i <  ARRAY_SIZE(qos->map.map); i++) {
		if (qos->qos_enable) {
			spx5_rmw(ANA_CL_DSCP_CFG_DSCP_TRUST_ENA_SET(1),
				 ANA_CL_DSCP_CFG_DSCP_TRUST_ENA, sparx5,
				 ANA_CL_DSCP_CFG(i));
		}
	}

	return 0;
}

int sparx5_port_qos_default_set(const struct sparx5_port *port,
				const struct sparx5_port_qos *qos)
{
	struct sparx5 *sparx5 = port->sparx5;

	/* Set default prio and dp level */
	spx5_rmw(ANA_CL_QOS_CFG_DEFAULT_QOS_VAL_SET(qos->default_prio) |
		 ANA_CL_QOS_CFG_DEFAULT_DP_VAL_SET(0),
		 ANA_CL_QOS_CFG_DEFAULT_QOS_VAL |
		 ANA_CL_QOS_CFG_DEFAULT_DP_VAL,
		 sparx5, ANA_CL_QOS_CFG(port->portno));

	/* Set default pcp and dei for untagged frames */
	spx5_rmw(ANA_CL_VLAN_CTRL_PORT_PCP_SET(0) |
		 ANA_CL_VLAN_CTRL_PORT_DEI_SET(0),
		 ANA_CL_VLAN_CTRL_PORT_PCP |
		 ANA_CL_VLAN_CTRL_PORT_DEI,
		 sparx5, ANA_CL_VLAN_CTRL(port->portno));

	return 0;
}
