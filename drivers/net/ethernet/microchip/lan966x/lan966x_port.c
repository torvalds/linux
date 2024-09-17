// SPDX-License-Identifier: GPL-2.0+

#include <linux/netdevice.h>
#include <linux/phy/phy.h>

#include "lan966x_main.h"

/* Watermark encode */
#define MULTIPLIER_BIT BIT(8)
static u32 lan966x_wm_enc(u32 value)
{
	value /= LAN966X_BUFFER_CELL_SZ;

	if (value >= MULTIPLIER_BIT) {
		value /= 16;
		if (value >= MULTIPLIER_BIT)
			value = (MULTIPLIER_BIT - 1);

		value |= MULTIPLIER_BIT;
	}

	return value;
}

static void lan966x_port_link_down(struct lan966x_port *port)
{
	struct lan966x *lan966x = port->lan966x;
	u32 val, delay = 0;

	/* 0.5: Disable any AFI */
	lan_rmw(AFI_PORT_CFG_FC_SKIP_TTI_INJ_SET(1) |
		AFI_PORT_CFG_FRM_OUT_MAX_SET(0),
		AFI_PORT_CFG_FC_SKIP_TTI_INJ |
		AFI_PORT_CFG_FRM_OUT_MAX,
		lan966x, AFI_PORT_CFG(port->chip_port));

	/* wait for reg afi_port_frm_out to become 0 for the port */
	while (true) {
		val = lan_rd(lan966x, AFI_PORT_FRM_OUT(port->chip_port));
		if (!AFI_PORT_FRM_OUT_FRM_OUT_CNT_GET(val))
			break;

		usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);
		delay++;
		if (delay == 2000) {
			pr_err("AFI timeout chip port %u", port->chip_port);
			break;
		}
	}

	delay = 0;

	/* 1: Reset the PCS Rx clock domain  */
	lan_rmw(DEV_CLOCK_CFG_PCS_RX_RST_SET(1),
		DEV_CLOCK_CFG_PCS_RX_RST,
		lan966x, DEV_CLOCK_CFG(port->chip_port));

	/* 2: Disable MAC frame reception */
	lan_rmw(DEV_MAC_ENA_CFG_RX_ENA_SET(0),
		DEV_MAC_ENA_CFG_RX_ENA,
		lan966x, DEV_MAC_ENA_CFG(port->chip_port));

	/* 3: Disable traffic being sent to or from switch port */
	lan_rmw(QSYS_SW_PORT_MODE_PORT_ENA_SET(0),
		QSYS_SW_PORT_MODE_PORT_ENA,
		lan966x, QSYS_SW_PORT_MODE(port->chip_port));

	/* 4: Disable dequeuing from the egress queues  */
	lan_rmw(QSYS_PORT_MODE_DEQUEUE_DIS_SET(1),
		QSYS_PORT_MODE_DEQUEUE_DIS,
		lan966x, QSYS_PORT_MODE(port->chip_port));

	/* 5: Disable Flowcontrol */
	lan_rmw(SYS_PAUSE_CFG_PAUSE_ENA_SET(0),
		SYS_PAUSE_CFG_PAUSE_ENA,
		lan966x, SYS_PAUSE_CFG(port->chip_port));

	/* 5.1: Disable PFC */
	lan_rmw(QSYS_SW_PORT_MODE_TX_PFC_ENA_SET(0),
		QSYS_SW_PORT_MODE_TX_PFC_ENA,
		lan966x, QSYS_SW_PORT_MODE(port->chip_port));

	/* 6: Wait a worst case time 8ms (jumbo/10Mbit) */
	usleep_range(8 * USEC_PER_MSEC, 9 * USEC_PER_MSEC);

	/* 7: Disable HDX backpressure */
	lan_rmw(SYS_FRONT_PORT_MODE_HDX_MODE_SET(0),
		SYS_FRONT_PORT_MODE_HDX_MODE,
		lan966x, SYS_FRONT_PORT_MODE(port->chip_port));

	/* 8: Flush the queues accociated with the port */
	lan_rmw(QSYS_SW_PORT_MODE_AGING_MODE_SET(3),
		QSYS_SW_PORT_MODE_AGING_MODE,
		lan966x, QSYS_SW_PORT_MODE(port->chip_port));

	/* 9: Enable dequeuing from the egress queues */
	lan_rmw(QSYS_PORT_MODE_DEQUEUE_DIS_SET(0),
		QSYS_PORT_MODE_DEQUEUE_DIS,
		lan966x, QSYS_PORT_MODE(port->chip_port));

	/* 10: Wait until flushing is complete */
	while (true) {
		val = lan_rd(lan966x, QSYS_SW_STATUS(port->chip_port));
		if (!QSYS_SW_STATUS_EQ_AVAIL_GET(val))
			break;

		usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);
		delay++;
		if (delay == 2000) {
			pr_err("Flush timeout chip port %u", port->chip_port);
			break;
		}
	}

	/* 11: Reset the Port and MAC clock domains */
	lan_rmw(DEV_MAC_ENA_CFG_TX_ENA_SET(0),
		DEV_MAC_ENA_CFG_TX_ENA,
		lan966x, DEV_MAC_ENA_CFG(port->chip_port));

	lan_rmw(DEV_CLOCK_CFG_PORT_RST_SET(1),
		DEV_CLOCK_CFG_PORT_RST,
		lan966x, DEV_CLOCK_CFG(port->chip_port));

	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	lan_rmw(DEV_CLOCK_CFG_MAC_TX_RST_SET(1) |
		DEV_CLOCK_CFG_MAC_RX_RST_SET(1) |
		DEV_CLOCK_CFG_PORT_RST_SET(1),
		DEV_CLOCK_CFG_MAC_TX_RST |
		DEV_CLOCK_CFG_MAC_RX_RST |
		DEV_CLOCK_CFG_PORT_RST,
		lan966x, DEV_CLOCK_CFG(port->chip_port));

	/* 12: Clear flushing */
	lan_rmw(QSYS_SW_PORT_MODE_AGING_MODE_SET(2),
		QSYS_SW_PORT_MODE_AGING_MODE,
		lan966x, QSYS_SW_PORT_MODE(port->chip_port));

	/* The port is disabled and flushed, now set up the port in the
	 * new operating mode
	 */
}

static void lan966x_port_link_up(struct lan966x_port *port)
{
	struct lan966x_port_config *config = &port->config;
	struct lan966x *lan966x = port->lan966x;
	int speed = 0, mode = 0;
	int atop_wm = 0;

	switch (config->speed) {
	case SPEED_10:
		speed = LAN966X_SPEED_10;
		break;
	case SPEED_100:
		speed = LAN966X_SPEED_100;
		break;
	case SPEED_1000:
		speed = LAN966X_SPEED_1000;
		mode = DEV_MAC_MODE_CFG_GIGA_MODE_ENA_SET(1);
		break;
	case SPEED_2500:
		speed = LAN966X_SPEED_2500;
		mode = DEV_MAC_MODE_CFG_GIGA_MODE_ENA_SET(1);
		break;
	}

	lan966x_taprio_speed_set(port, config->speed);

	/* Also the GIGA_MODE_ENA(1) needs to be set regardless of the
	 * port speed for QSGMII or SGMII ports.
	 */
	if (phy_interface_num_ports(config->portmode) == 4 ||
	    config->portmode == PHY_INTERFACE_MODE_SGMII)
		mode = DEV_MAC_MODE_CFG_GIGA_MODE_ENA_SET(1);

	lan_wr(config->duplex | mode,
	       lan966x, DEV_MAC_MODE_CFG(port->chip_port));

	lan_rmw(DEV_MAC_IFG_CFG_TX_IFG_SET(config->duplex ? 6 : 5) |
		DEV_MAC_IFG_CFG_RX_IFG1_SET(config->speed == SPEED_10 ? 2 : 1) |
		DEV_MAC_IFG_CFG_RX_IFG2_SET(2),
		DEV_MAC_IFG_CFG_TX_IFG |
		DEV_MAC_IFG_CFG_RX_IFG1 |
		DEV_MAC_IFG_CFG_RX_IFG2,
		lan966x, DEV_MAC_IFG_CFG(port->chip_port));

	lan_rmw(DEV_MAC_HDX_CFG_SEED_SET(4) |
		DEV_MAC_HDX_CFG_SEED_LOAD_SET(1),
		DEV_MAC_HDX_CFG_SEED |
		DEV_MAC_HDX_CFG_SEED_LOAD,
		lan966x, DEV_MAC_HDX_CFG(port->chip_port));

	if (config->portmode == PHY_INTERFACE_MODE_GMII) {
		if (config->speed == SPEED_1000)
			lan_rmw(CHIP_TOP_CUPHY_PORT_CFG_GTX_CLK_ENA_SET(1),
				CHIP_TOP_CUPHY_PORT_CFG_GTX_CLK_ENA,
				lan966x,
				CHIP_TOP_CUPHY_PORT_CFG(port->chip_port));
		else
			lan_rmw(CHIP_TOP_CUPHY_PORT_CFG_GTX_CLK_ENA_SET(0),
				CHIP_TOP_CUPHY_PORT_CFG_GTX_CLK_ENA,
				lan966x,
				CHIP_TOP_CUPHY_PORT_CFG(port->chip_port));
	}

	/* No PFC */
	lan_wr(ANA_PFC_CFG_FC_LINK_SPEED_SET(speed),
	       lan966x, ANA_PFC_CFG(port->chip_port));

	lan_rmw(DEV_PCS1G_CFG_PCS_ENA_SET(1),
		DEV_PCS1G_CFG_PCS_ENA,
		lan966x, DEV_PCS1G_CFG(port->chip_port));

	lan_rmw(DEV_PCS1G_SD_CFG_SD_ENA_SET(0),
		DEV_PCS1G_SD_CFG_SD_ENA,
		lan966x, DEV_PCS1G_SD_CFG(port->chip_port));

	/* Set Pause WM hysteresis, start/stop are in 1518 byte units */
	lan_wr(SYS_PAUSE_CFG_PAUSE_ENA_SET(1) |
	       SYS_PAUSE_CFG_PAUSE_STOP_SET(lan966x_wm_enc(4 * 1518)) |
	       SYS_PAUSE_CFG_PAUSE_START_SET(lan966x_wm_enc(6 * 1518)),
	       lan966x, SYS_PAUSE_CFG(port->chip_port));

	/* Set SMAC of Pause frame (00:00:00:00:00:00) */
	lan_wr(0, lan966x, DEV_FC_MAC_LOW_CFG(port->chip_port));
	lan_wr(0, lan966x, DEV_FC_MAC_HIGH_CFG(port->chip_port));

	/* Flow control */
	lan_rmw(SYS_MAC_FC_CFG_FC_LINK_SPEED_SET(speed) |
		SYS_MAC_FC_CFG_FC_LATENCY_CFG_SET(7) |
		SYS_MAC_FC_CFG_ZERO_PAUSE_ENA_SET(1) |
		SYS_MAC_FC_CFG_PAUSE_VAL_CFG_SET(0xffff) |
		SYS_MAC_FC_CFG_RX_FC_ENA_SET(config->pause & MLO_PAUSE_RX ? 1 : 0) |
		SYS_MAC_FC_CFG_TX_FC_ENA_SET(config->pause & MLO_PAUSE_TX ? 1 : 0),
		SYS_MAC_FC_CFG_FC_LINK_SPEED |
		SYS_MAC_FC_CFG_FC_LATENCY_CFG |
		SYS_MAC_FC_CFG_ZERO_PAUSE_ENA |
		SYS_MAC_FC_CFG_PAUSE_VAL_CFG |
		SYS_MAC_FC_CFG_RX_FC_ENA |
		SYS_MAC_FC_CFG_TX_FC_ENA,
		lan966x, SYS_MAC_FC_CFG(port->chip_port));

	/* Tail dropping watermark */
	atop_wm = lan966x->shared_queue_sz;

	/* The total memory size is diveded by number of front ports plus CPU
	 * port
	 */
	lan_wr(lan966x_wm_enc(atop_wm / lan966x->num_phys_ports + 1), lan966x,
	       SYS_ATOP(port->chip_port));
	lan_wr(lan966x_wm_enc(atop_wm), lan966x, SYS_ATOP_TOT_CFG);

	/* This needs to be at the end */
	/* Enable MAC module */
	lan_wr(DEV_MAC_ENA_CFG_RX_ENA_SET(1) |
	       DEV_MAC_ENA_CFG_TX_ENA_SET(1),
	       lan966x, DEV_MAC_ENA_CFG(port->chip_port));

	/* Take out the clock from reset */
	lan_wr(DEV_CLOCK_CFG_LINK_SPEED_SET(speed),
	       lan966x, DEV_CLOCK_CFG(port->chip_port));

	/* Core: Enable port for frame transfer */
	lan_wr(QSYS_SW_PORT_MODE_PORT_ENA_SET(1) |
	       QSYS_SW_PORT_MODE_SCH_NEXT_CFG_SET(1) |
	       QSYS_SW_PORT_MODE_INGRESS_DROP_MODE_SET(1),
	       lan966x, QSYS_SW_PORT_MODE(port->chip_port));

	lan_rmw(AFI_PORT_CFG_FC_SKIP_TTI_INJ_SET(0) |
		AFI_PORT_CFG_FRM_OUT_MAX_SET(16),
		AFI_PORT_CFG_FC_SKIP_TTI_INJ |
		AFI_PORT_CFG_FRM_OUT_MAX,
		lan966x, AFI_PORT_CFG(port->chip_port));
}

void lan966x_port_config_down(struct lan966x_port *port)
{
	lan966x_port_link_down(port);
}

void lan966x_port_config_up(struct lan966x_port *port)
{
	lan966x_port_link_up(port);
}

void lan966x_port_status_get(struct lan966x_port *port,
			     struct phylink_link_state *state)
{
	struct lan966x *lan966x = port->lan966x;
	bool link_down;
	u16 bmsr = 0;
	u16 lp_adv;
	u32 val;

	val = lan_rd(lan966x, DEV_PCS1G_STICKY(port->chip_port));
	link_down = DEV_PCS1G_STICKY_LINK_DOWN_STICKY_GET(val);
	if (link_down)
		lan_wr(val, lan966x, DEV_PCS1G_STICKY(port->chip_port));

	/* Get both current Link and Sync status */
	val = lan_rd(lan966x, DEV_PCS1G_LINK_STATUS(port->chip_port));
	state->link = DEV_PCS1G_LINK_STATUS_LINK_STATUS_GET(val) &&
		      DEV_PCS1G_LINK_STATUS_SYNC_STATUS_GET(val);
	state->link &= !link_down;

	/* Get PCS ANEG status register */
	val = lan_rd(lan966x, DEV_PCS1G_ANEG_STATUS(port->chip_port));
	/* Aneg complete provides more information  */
	if (DEV_PCS1G_ANEG_STATUS_ANEG_COMPLETE_GET(val)) {
		state->an_complete = true;

		bmsr |= state->link ? BMSR_LSTATUS : 0;
		bmsr |= BMSR_ANEGCOMPLETE;

		lp_adv = DEV_PCS1G_ANEG_STATUS_LP_ADV_GET(val);
		phylink_mii_c22_pcs_decode_state(state, bmsr, lp_adv);
	} else {
		if (!state->link)
			return;

		if (state->interface == PHY_INTERFACE_MODE_1000BASEX)
			state->speed = SPEED_1000;
		else if (state->interface == PHY_INTERFACE_MODE_2500BASEX)
			state->speed = SPEED_2500;

		state->duplex = DUPLEX_FULL;
	}
}

int lan966x_port_pcs_set(struct lan966x_port *port,
			 struct lan966x_port_config *config)
{
	struct lan966x *lan966x = port->lan966x;
	bool inband_aneg = false;
	bool outband;
	bool full_preamble = false;

	if (config->portmode == PHY_INTERFACE_MODE_QUSGMII)
		full_preamble = true;

	if (config->inband) {
		if (config->portmode == PHY_INTERFACE_MODE_SGMII ||
		    phy_interface_num_ports(config->portmode) == 4)
			inband_aneg = true; /* Cisco-SGMII in-band-aneg */
		else if (config->portmode == PHY_INTERFACE_MODE_1000BASEX &&
			 config->autoneg)
			inband_aneg = true; /* Clause-37 in-band-aneg */

		outband = false;
	} else {
		outband = true;
	}

	/* Disable or enable inband.
	 * For QUSGMII, we rely on the preamble to transmit data such as
	 * timestamps, therefore force full preamble transmission, and prevent
	 * premable shortening
	 */
	lan_rmw(DEV_PCS1G_MODE_CFG_SGMII_MODE_ENA_SET(outband) |
		DEV_PCS1G_MODE_CFG_SAVE_PREAMBLE_ENA_SET(full_preamble),
		DEV_PCS1G_MODE_CFG_SGMII_MODE_ENA |
		DEV_PCS1G_MODE_CFG_SAVE_PREAMBLE_ENA,
		lan966x, DEV_PCS1G_MODE_CFG(port->chip_port));

	/* Enable PCS */
	lan_wr(DEV_PCS1G_CFG_PCS_ENA_SET(1),
	       lan966x, DEV_PCS1G_CFG(port->chip_port));

	if (inband_aneg) {
		int adv = phylink_mii_c22_pcs_encode_advertisement(config->portmode,
								   config->advertising);
		if (adv >= 0)
			/* Enable in-band aneg */
			lan_wr(DEV_PCS1G_ANEG_CFG_ADV_ABILITY_SET(adv) |
			       DEV_PCS1G_ANEG_CFG_SW_RESOLVE_ENA_SET(1) |
			       DEV_PCS1G_ANEG_CFG_ENA_SET(1) |
			       DEV_PCS1G_ANEG_CFG_RESTART_ONE_SHOT_SET(1),
			       lan966x, DEV_PCS1G_ANEG_CFG(port->chip_port));
	} else {
		lan_wr(0, lan966x, DEV_PCS1G_ANEG_CFG(port->chip_port));
	}

	/* Take PCS out of reset */
	lan_rmw(DEV_CLOCK_CFG_LINK_SPEED_SET(LAN966X_SPEED_1000) |
		DEV_CLOCK_CFG_PCS_RX_RST_SET(0) |
		DEV_CLOCK_CFG_PCS_TX_RST_SET(0),
		DEV_CLOCK_CFG_LINK_SPEED |
		DEV_CLOCK_CFG_PCS_RX_RST |
		DEV_CLOCK_CFG_PCS_TX_RST,
		lan966x, DEV_CLOCK_CFG(port->chip_port));

	port->config = *config;

	return 0;
}

void lan966x_port_init(struct lan966x_port *port)
{
	struct lan966x_port_config *config = &port->config;
	struct lan966x *lan966x = port->lan966x;

	lan_rmw(ANA_PORT_CFG_LEARN_ENA_SET(0),
		ANA_PORT_CFG_LEARN_ENA,
		lan966x, ANA_PORT_CFG(port->chip_port));

	lan966x_port_config_down(port);

	if (lan966x->fdma)
		lan966x_fdma_netdev_init(lan966x, port->dev);

	if (phy_interface_num_ports(config->portmode) != 4)
		return;

	lan_rmw(DEV_CLOCK_CFG_PCS_RX_RST_SET(0) |
		DEV_CLOCK_CFG_PCS_TX_RST_SET(0) |
		DEV_CLOCK_CFG_LINK_SPEED_SET(LAN966X_SPEED_1000),
		DEV_CLOCK_CFG_PCS_RX_RST |
		DEV_CLOCK_CFG_PCS_TX_RST |
		DEV_CLOCK_CFG_LINK_SPEED,
		lan966x, DEV_CLOCK_CFG(port->chip_port));
}
