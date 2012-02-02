/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* ethtool support for igb */

#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/ethtool.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include "igb.h"

struct igb_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define IGB_STAT(_name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = FIELD_SIZEOF(struct igb_adapter, _stat), \
	.stat_offset = offsetof(struct igb_adapter, _stat) \
}
static const struct igb_stats igb_gstrings_stats[] = {
	IGB_STAT("rx_packets", stats.gprc),
	IGB_STAT("tx_packets", stats.gptc),
	IGB_STAT("rx_bytes", stats.gorc),
	IGB_STAT("tx_bytes", stats.gotc),
	IGB_STAT("rx_broadcast", stats.bprc),
	IGB_STAT("tx_broadcast", stats.bptc),
	IGB_STAT("rx_multicast", stats.mprc),
	IGB_STAT("tx_multicast", stats.mptc),
	IGB_STAT("multicast", stats.mprc),
	IGB_STAT("collisions", stats.colc),
	IGB_STAT("rx_crc_errors", stats.crcerrs),
	IGB_STAT("rx_no_buffer_count", stats.rnbc),
	IGB_STAT("rx_missed_errors", stats.mpc),
	IGB_STAT("tx_aborted_errors", stats.ecol),
	IGB_STAT("tx_carrier_errors", stats.tncrs),
	IGB_STAT("tx_window_errors", stats.latecol),
	IGB_STAT("tx_abort_late_coll", stats.latecol),
	IGB_STAT("tx_deferred_ok", stats.dc),
	IGB_STAT("tx_single_coll_ok", stats.scc),
	IGB_STAT("tx_multi_coll_ok", stats.mcc),
	IGB_STAT("tx_timeout_count", tx_timeout_count),
	IGB_STAT("rx_long_length_errors", stats.roc),
	IGB_STAT("rx_short_length_errors", stats.ruc),
	IGB_STAT("rx_align_errors", stats.algnerrc),
	IGB_STAT("tx_tcp_seg_good", stats.tsctc),
	IGB_STAT("tx_tcp_seg_failed", stats.tsctfc),
	IGB_STAT("rx_flow_control_xon", stats.xonrxc),
	IGB_STAT("rx_flow_control_xoff", stats.xoffrxc),
	IGB_STAT("tx_flow_control_xon", stats.xontxc),
	IGB_STAT("tx_flow_control_xoff", stats.xofftxc),
	IGB_STAT("rx_long_byte_count", stats.gorc),
	IGB_STAT("tx_dma_out_of_sync", stats.doosync),
	IGB_STAT("tx_smbus", stats.mgptc),
	IGB_STAT("rx_smbus", stats.mgprc),
	IGB_STAT("dropped_smbus", stats.mgpdc),
	IGB_STAT("os2bmc_rx_by_bmc", stats.o2bgptc),
	IGB_STAT("os2bmc_tx_by_bmc", stats.b2ospc),
	IGB_STAT("os2bmc_tx_by_host", stats.o2bspc),
	IGB_STAT("os2bmc_rx_by_host", stats.b2ogprc),
};

#define IGB_NETDEV_STAT(_net_stat) { \
	.stat_string = __stringify(_net_stat), \
	.sizeof_stat = FIELD_SIZEOF(struct rtnl_link_stats64, _net_stat), \
	.stat_offset = offsetof(struct rtnl_link_stats64, _net_stat) \
}
static const struct igb_stats igb_gstrings_net_stats[] = {
	IGB_NETDEV_STAT(rx_errors),
	IGB_NETDEV_STAT(tx_errors),
	IGB_NETDEV_STAT(tx_dropped),
	IGB_NETDEV_STAT(rx_length_errors),
	IGB_NETDEV_STAT(rx_over_errors),
	IGB_NETDEV_STAT(rx_frame_errors),
	IGB_NETDEV_STAT(rx_fifo_errors),
	IGB_NETDEV_STAT(tx_fifo_errors),
	IGB_NETDEV_STAT(tx_heartbeat_errors)
};

#define IGB_GLOBAL_STATS_LEN	\
	(sizeof(igb_gstrings_stats) / sizeof(struct igb_stats))
#define IGB_NETDEV_STATS_LEN	\
	(sizeof(igb_gstrings_net_stats) / sizeof(struct igb_stats))
#define IGB_RX_QUEUE_STATS_LEN \
	(sizeof(struct igb_rx_queue_stats) / sizeof(u64))

#define IGB_TX_QUEUE_STATS_LEN 3 /* packets, bytes, restart_queue */

#define IGB_QUEUE_STATS_LEN \
	((((struct igb_adapter *)netdev_priv(netdev))->num_rx_queues * \
	  IGB_RX_QUEUE_STATS_LEN) + \
	 (((struct igb_adapter *)netdev_priv(netdev))->num_tx_queues * \
	  IGB_TX_QUEUE_STATS_LEN))
#define IGB_STATS_LEN \
	(IGB_GLOBAL_STATS_LEN + IGB_NETDEV_STATS_LEN + IGB_QUEUE_STATS_LEN)

static const char igb_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)", "Eeprom test    (offline)",
	"Interrupt test (offline)", "Loopback test  (offline)",
	"Link test   (on/offline)"
};
#define IGB_TEST_LEN (sizeof(igb_gstrings_test) / ETH_GSTRING_LEN)

static int igb_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 status;

	if (hw->phy.media_type == e1000_media_type_copper) {

		ecmd->supported = (SUPPORTED_10baseT_Half |
				   SUPPORTED_10baseT_Full |
				   SUPPORTED_100baseT_Half |
				   SUPPORTED_100baseT_Full |
				   SUPPORTED_1000baseT_Full|
				   SUPPORTED_Autoneg |
				   SUPPORTED_TP);
		ecmd->advertising = (ADVERTISED_TP |
				     ADVERTISED_Pause);

		if (hw->mac.autoneg == 1) {
			ecmd->advertising |= ADVERTISED_Autoneg;
			/* the e1000 autoneg seems to match ethtool nicely */
			ecmd->advertising |= hw->phy.autoneg_advertised;
		}

		ecmd->port = PORT_TP;
		ecmd->phy_address = hw->phy.addr;
	} else {
		ecmd->supported   = (SUPPORTED_1000baseT_Full |
				     SUPPORTED_FIBRE |
				     SUPPORTED_Autoneg);

		ecmd->advertising = (ADVERTISED_1000baseT_Full |
				     ADVERTISED_FIBRE |
				     ADVERTISED_Autoneg |
				     ADVERTISED_Pause);

		ecmd->port = PORT_FIBRE;
	}

	ecmd->transceiver = XCVR_INTERNAL;

	status = rd32(E1000_STATUS);

	if (status & E1000_STATUS_LU) {

		if ((status & E1000_STATUS_SPEED_1000) ||
		    hw->phy.media_type != e1000_media_type_copper)
			ethtool_cmd_speed_set(ecmd, SPEED_1000);
		else if (status & E1000_STATUS_SPEED_100)
			ethtool_cmd_speed_set(ecmd, SPEED_100);
		else
			ethtool_cmd_speed_set(ecmd, SPEED_10);

		if ((status & E1000_STATUS_FD) ||
		    hw->phy.media_type != e1000_media_type_copper)
			ecmd->duplex = DUPLEX_FULL;
		else
			ecmd->duplex = DUPLEX_HALF;
	} else {
		ethtool_cmd_speed_set(ecmd, -1);
		ecmd->duplex = -1;
	}

	ecmd->autoneg = hw->mac.autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;
	return 0;
}

static int igb_set_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	/* When SoL/IDER sessions are active, autoneg/speed/duplex
	 * cannot be changed */
	if (igb_check_reset_block(hw)) {
		dev_err(&adapter->pdev->dev, "Cannot change link "
			"characteristics when SoL/IDER is active.\n");
		return -EINVAL;
	}

	while (test_and_set_bit(__IGB_RESETTING, &adapter->state))
		msleep(1);

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		hw->mac.autoneg = 1;
		hw->phy.autoneg_advertised = ecmd->advertising |
					     ADVERTISED_TP |
					     ADVERTISED_Autoneg;
		ecmd->advertising = hw->phy.autoneg_advertised;
		if (adapter->fc_autoneg)
			hw->fc.requested_mode = e1000_fc_default;
	} else {
		u32 speed = ethtool_cmd_speed(ecmd);
		if (igb_set_spd_dplx(adapter, speed, ecmd->duplex)) {
			clear_bit(__IGB_RESETTING, &adapter->state);
			return -EINVAL;
		}
	}

	/* reset the link */
	if (netif_running(adapter->netdev)) {
		igb_down(adapter);
		igb_up(adapter);
	} else
		igb_reset(adapter);

	clear_bit(__IGB_RESETTING, &adapter->state);
	return 0;
}

static u32 igb_get_link(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_mac_info *mac = &adapter->hw.mac;

	/*
	 * If the link is not reported up to netdev, interrupts are disabled,
	 * and so the physical link state may have changed since we last
	 * looked. Set get_link_status to make sure that the true link
	 * state is interrogated, rather than pulling a cached and possibly
	 * stale link state from the driver.
	 */
	if (!netif_carrier_ok(netdev))
		mac->get_link_status = 1;

	return igb_has_link(adapter);
}

static void igb_get_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	pause->autoneg =
		(adapter->fc_autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (hw->fc.current_mode == e1000_fc_rx_pause)
		pause->rx_pause = 1;
	else if (hw->fc.current_mode == e1000_fc_tx_pause)
		pause->tx_pause = 1;
	else if (hw->fc.current_mode == e1000_fc_full) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

static int igb_set_pauseparam(struct net_device *netdev,
			      struct ethtool_pauseparam *pause)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int retval = 0;

	adapter->fc_autoneg = pause->autoneg;

	while (test_and_set_bit(__IGB_RESETTING, &adapter->state))
		msleep(1);

	if (adapter->fc_autoneg == AUTONEG_ENABLE) {
		hw->fc.requested_mode = e1000_fc_default;
		if (netif_running(adapter->netdev)) {
			igb_down(adapter);
			igb_up(adapter);
		} else {
			igb_reset(adapter);
		}
	} else {
		if (pause->rx_pause && pause->tx_pause)
			hw->fc.requested_mode = e1000_fc_full;
		else if (pause->rx_pause && !pause->tx_pause)
			hw->fc.requested_mode = e1000_fc_rx_pause;
		else if (!pause->rx_pause && pause->tx_pause)
			hw->fc.requested_mode = e1000_fc_tx_pause;
		else if (!pause->rx_pause && !pause->tx_pause)
			hw->fc.requested_mode = e1000_fc_none;

		hw->fc.current_mode = hw->fc.requested_mode;

		retval = ((hw->phy.media_type == e1000_media_type_copper) ?
			  igb_force_mac_fc(hw) : igb_setup_link(hw));
	}

	clear_bit(__IGB_RESETTING, &adapter->state);
	return retval;
}

static u32 igb_get_msglevel(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	return adapter->msg_enable;
}

static void igb_set_msglevel(struct net_device *netdev, u32 data)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	adapter->msg_enable = data;
}

static int igb_get_regs_len(struct net_device *netdev)
{
#define IGB_REGS_LEN 551
	return IGB_REGS_LEN * sizeof(u32);
}

static void igb_get_regs(struct net_device *netdev,
			 struct ethtool_regs *regs, void *p)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u8 i;

	memset(p, 0, IGB_REGS_LEN * sizeof(u32));

	regs->version = (1 << 24) | (hw->revision_id << 16) | hw->device_id;

	/* General Registers */
	regs_buff[0] = rd32(E1000_CTRL);
	regs_buff[1] = rd32(E1000_STATUS);
	regs_buff[2] = rd32(E1000_CTRL_EXT);
	regs_buff[3] = rd32(E1000_MDIC);
	regs_buff[4] = rd32(E1000_SCTL);
	regs_buff[5] = rd32(E1000_CONNSW);
	regs_buff[6] = rd32(E1000_VET);
	regs_buff[7] = rd32(E1000_LEDCTL);
	regs_buff[8] = rd32(E1000_PBA);
	regs_buff[9] = rd32(E1000_PBS);
	regs_buff[10] = rd32(E1000_FRTIMER);
	regs_buff[11] = rd32(E1000_TCPTIMER);

	/* NVM Register */
	regs_buff[12] = rd32(E1000_EECD);

	/* Interrupt */
	/* Reading EICS for EICR because they read the
	 * same but EICS does not clear on read */
	regs_buff[13] = rd32(E1000_EICS);
	regs_buff[14] = rd32(E1000_EICS);
	regs_buff[15] = rd32(E1000_EIMS);
	regs_buff[16] = rd32(E1000_EIMC);
	regs_buff[17] = rd32(E1000_EIAC);
	regs_buff[18] = rd32(E1000_EIAM);
	/* Reading ICS for ICR because they read the
	 * same but ICS does not clear on read */
	regs_buff[19] = rd32(E1000_ICS);
	regs_buff[20] = rd32(E1000_ICS);
	regs_buff[21] = rd32(E1000_IMS);
	regs_buff[22] = rd32(E1000_IMC);
	regs_buff[23] = rd32(E1000_IAC);
	regs_buff[24] = rd32(E1000_IAM);
	regs_buff[25] = rd32(E1000_IMIRVP);

	/* Flow Control */
	regs_buff[26] = rd32(E1000_FCAL);
	regs_buff[27] = rd32(E1000_FCAH);
	regs_buff[28] = rd32(E1000_FCTTV);
	regs_buff[29] = rd32(E1000_FCRTL);
	regs_buff[30] = rd32(E1000_FCRTH);
	regs_buff[31] = rd32(E1000_FCRTV);

	/* Receive */
	regs_buff[32] = rd32(E1000_RCTL);
	regs_buff[33] = rd32(E1000_RXCSUM);
	regs_buff[34] = rd32(E1000_RLPML);
	regs_buff[35] = rd32(E1000_RFCTL);
	regs_buff[36] = rd32(E1000_MRQC);
	regs_buff[37] = rd32(E1000_VT_CTL);

	/* Transmit */
	regs_buff[38] = rd32(E1000_TCTL);
	regs_buff[39] = rd32(E1000_TCTL_EXT);
	regs_buff[40] = rd32(E1000_TIPG);
	regs_buff[41] = rd32(E1000_DTXCTL);

	/* Wake Up */
	regs_buff[42] = rd32(E1000_WUC);
	regs_buff[43] = rd32(E1000_WUFC);
	regs_buff[44] = rd32(E1000_WUS);
	regs_buff[45] = rd32(E1000_IPAV);
	regs_buff[46] = rd32(E1000_WUPL);

	/* MAC */
	regs_buff[47] = rd32(E1000_PCS_CFG0);
	regs_buff[48] = rd32(E1000_PCS_LCTL);
	regs_buff[49] = rd32(E1000_PCS_LSTAT);
	regs_buff[50] = rd32(E1000_PCS_ANADV);
	regs_buff[51] = rd32(E1000_PCS_LPAB);
	regs_buff[52] = rd32(E1000_PCS_NPTX);
	regs_buff[53] = rd32(E1000_PCS_LPABNP);

	/* Statistics */
	regs_buff[54] = adapter->stats.crcerrs;
	regs_buff[55] = adapter->stats.algnerrc;
	regs_buff[56] = adapter->stats.symerrs;
	regs_buff[57] = adapter->stats.rxerrc;
	regs_buff[58] = adapter->stats.mpc;
	regs_buff[59] = adapter->stats.scc;
	regs_buff[60] = adapter->stats.ecol;
	regs_buff[61] = adapter->stats.mcc;
	regs_buff[62] = adapter->stats.latecol;
	regs_buff[63] = adapter->stats.colc;
	regs_buff[64] = adapter->stats.dc;
	regs_buff[65] = adapter->stats.tncrs;
	regs_buff[66] = adapter->stats.sec;
	regs_buff[67] = adapter->stats.htdpmc;
	regs_buff[68] = adapter->stats.rlec;
	regs_buff[69] = adapter->stats.xonrxc;
	regs_buff[70] = adapter->stats.xontxc;
	regs_buff[71] = adapter->stats.xoffrxc;
	regs_buff[72] = adapter->stats.xofftxc;
	regs_buff[73] = adapter->stats.fcruc;
	regs_buff[74] = adapter->stats.prc64;
	regs_buff[75] = adapter->stats.prc127;
	regs_buff[76] = adapter->stats.prc255;
	regs_buff[77] = adapter->stats.prc511;
	regs_buff[78] = adapter->stats.prc1023;
	regs_buff[79] = adapter->stats.prc1522;
	regs_buff[80] = adapter->stats.gprc;
	regs_buff[81] = adapter->stats.bprc;
	regs_buff[82] = adapter->stats.mprc;
	regs_buff[83] = adapter->stats.gptc;
	regs_buff[84] = adapter->stats.gorc;
	regs_buff[86] = adapter->stats.gotc;
	regs_buff[88] = adapter->stats.rnbc;
	regs_buff[89] = adapter->stats.ruc;
	regs_buff[90] = adapter->stats.rfc;
	regs_buff[91] = adapter->stats.roc;
	regs_buff[92] = adapter->stats.rjc;
	regs_buff[93] = adapter->stats.mgprc;
	regs_buff[94] = adapter->stats.mgpdc;
	regs_buff[95] = adapter->stats.mgptc;
	regs_buff[96] = adapter->stats.tor;
	regs_buff[98] = adapter->stats.tot;
	regs_buff[100] = adapter->stats.tpr;
	regs_buff[101] = adapter->stats.tpt;
	regs_buff[102] = adapter->stats.ptc64;
	regs_buff[103] = adapter->stats.ptc127;
	regs_buff[104] = adapter->stats.ptc255;
	regs_buff[105] = adapter->stats.ptc511;
	regs_buff[106] = adapter->stats.ptc1023;
	regs_buff[107] = adapter->stats.ptc1522;
	regs_buff[108] = adapter->stats.mptc;
	regs_buff[109] = adapter->stats.bptc;
	regs_buff[110] = adapter->stats.tsctc;
	regs_buff[111] = adapter->stats.iac;
	regs_buff[112] = adapter->stats.rpthc;
	regs_buff[113] = adapter->stats.hgptc;
	regs_buff[114] = adapter->stats.hgorc;
	regs_buff[116] = adapter->stats.hgotc;
	regs_buff[118] = adapter->stats.lenerrs;
	regs_buff[119] = adapter->stats.scvpc;
	regs_buff[120] = adapter->stats.hrmpc;

	for (i = 0; i < 4; i++)
		regs_buff[121 + i] = rd32(E1000_SRRCTL(i));
	for (i = 0; i < 4; i++)
		regs_buff[125 + i] = rd32(E1000_PSRTYPE(i));
	for (i = 0; i < 4; i++)
		regs_buff[129 + i] = rd32(E1000_RDBAL(i));
	for (i = 0; i < 4; i++)
		regs_buff[133 + i] = rd32(E1000_RDBAH(i));
	for (i = 0; i < 4; i++)
		regs_buff[137 + i] = rd32(E1000_RDLEN(i));
	for (i = 0; i < 4; i++)
		regs_buff[141 + i] = rd32(E1000_RDH(i));
	for (i = 0; i < 4; i++)
		regs_buff[145 + i] = rd32(E1000_RDT(i));
	for (i = 0; i < 4; i++)
		regs_buff[149 + i] = rd32(E1000_RXDCTL(i));

	for (i = 0; i < 10; i++)
		regs_buff[153 + i] = rd32(E1000_EITR(i));
	for (i = 0; i < 8; i++)
		regs_buff[163 + i] = rd32(E1000_IMIR(i));
	for (i = 0; i < 8; i++)
		regs_buff[171 + i] = rd32(E1000_IMIREXT(i));
	for (i = 0; i < 16; i++)
		regs_buff[179 + i] = rd32(E1000_RAL(i));
	for (i = 0; i < 16; i++)
		regs_buff[195 + i] = rd32(E1000_RAH(i));

	for (i = 0; i < 4; i++)
		regs_buff[211 + i] = rd32(E1000_TDBAL(i));
	for (i = 0; i < 4; i++)
		regs_buff[215 + i] = rd32(E1000_TDBAH(i));
	for (i = 0; i < 4; i++)
		regs_buff[219 + i] = rd32(E1000_TDLEN(i));
	for (i = 0; i < 4; i++)
		regs_buff[223 + i] = rd32(E1000_TDH(i));
	for (i = 0; i < 4; i++)
		regs_buff[227 + i] = rd32(E1000_TDT(i));
	for (i = 0; i < 4; i++)
		regs_buff[231 + i] = rd32(E1000_TXDCTL(i));
	for (i = 0; i < 4; i++)
		regs_buff[235 + i] = rd32(E1000_TDWBAL(i));
	for (i = 0; i < 4; i++)
		regs_buff[239 + i] = rd32(E1000_TDWBAH(i));
	for (i = 0; i < 4; i++)
		regs_buff[243 + i] = rd32(E1000_DCA_TXCTRL(i));

	for (i = 0; i < 4; i++)
		regs_buff[247 + i] = rd32(E1000_IP4AT_REG(i));
	for (i = 0; i < 4; i++)
		regs_buff[251 + i] = rd32(E1000_IP6AT_REG(i));
	for (i = 0; i < 32; i++)
		regs_buff[255 + i] = rd32(E1000_WUPM_REG(i));
	for (i = 0; i < 128; i++)
		regs_buff[287 + i] = rd32(E1000_FFMT_REG(i));
	for (i = 0; i < 128; i++)
		regs_buff[415 + i] = rd32(E1000_FFVT_REG(i));
	for (i = 0; i < 4; i++)
		regs_buff[543 + i] = rd32(E1000_FFLT_REG(i));

	regs_buff[547] = rd32(E1000_TDFH);
	regs_buff[548] = rd32(E1000_TDFT);
	regs_buff[549] = rd32(E1000_TDFHS);
	regs_buff[550] = rd32(E1000_TDFPC);
	regs_buff[551] = adapter->stats.o2bgptc;
	regs_buff[552] = adapter->stats.b2ospc;
	regs_buff[553] = adapter->stats.o2bspc;
	regs_buff[554] = adapter->stats.b2ogprc;
}

static int igb_get_eeprom_len(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	return adapter->hw.nvm.word_size * 2;
}

static int igb_get_eeprom(struct net_device *netdev,
			  struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	int first_word, last_word;
	int ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;

	eeprom_buff = kmalloc(sizeof(u16) *
			(last_word - first_word + 1), GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	if (hw->nvm.type == e1000_nvm_eeprom_spi)
		ret_val = hw->nvm.ops.read(hw, first_word,
					    last_word - first_word + 1,
					    eeprom_buff);
	else {
		for (i = 0; i < last_word - first_word + 1; i++) {
			ret_val = hw->nvm.ops.read(hw, first_word + i, 1,
						    &eeprom_buff[i]);
			if (ret_val)
				break;
		}
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(bytes, (u8 *)eeprom_buff + (eeprom->offset & 1),
			eeprom->len);
	kfree(eeprom_buff);

	return ret_val;
}

static int igb_set_eeprom(struct net_device *netdev,
			  struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	void *ptr;
	int max_len, first_word, last_word, ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EOPNOTSUPP;

	if (eeprom->magic != (hw->vendor_id | (hw->device_id << 16)))
		return -EFAULT;

	max_len = hw->nvm.word_size * 2;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_buff = kmalloc(max_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ptr = (void *)eeprom_buff;

	if (eeprom->offset & 1) {
		/* need read/modify/write of first changed EEPROM word */
		/* only the second byte of the word is being modified */
		ret_val = hw->nvm.ops.read(hw, first_word, 1,
					    &eeprom_buff[0]);
		ptr++;
	}
	if (((eeprom->offset + eeprom->len) & 1) && (ret_val == 0)) {
		/* need read/modify/write of last changed EEPROM word */
		/* only the first byte of the word is being modified */
		ret_val = hw->nvm.ops.read(hw, last_word, 1,
				   &eeprom_buff[last_word - first_word]);
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i < last_word - first_word + 1; i++)
		eeprom_buff[i] = cpu_to_le16(eeprom_buff[i]);

	ret_val = hw->nvm.ops.write(hw, first_word,
				     last_word - first_word + 1, eeprom_buff);

	/* Update the checksum over the first part of the EEPROM if needed
	 * and flush shadow RAM for 82573 controllers */
	if ((ret_val == 0) && ((first_word <= NVM_CHECKSUM_REG)))
		hw->nvm.ops.update(hw);

	kfree(eeprom_buff);
	return ret_val;
}

static void igb_get_drvinfo(struct net_device *netdev,
			    struct ethtool_drvinfo *drvinfo)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	u16 eeprom_data;

	strlcpy(drvinfo->driver,  igb_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, igb_driver_version, sizeof(drvinfo->version));

	/* EEPROM image version # is reported as firmware version # for
	 * 82575 controllers */
	adapter->hw.nvm.ops.read(&adapter->hw, 5, 1, &eeprom_data);
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		"%d.%d-%d",
		(eeprom_data & 0xF000) >> 12,
		(eeprom_data & 0x0FF0) >> 4,
		eeprom_data & 0x000F);

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->n_stats = IGB_STATS_LEN;
	drvinfo->testinfo_len = IGB_TEST_LEN;
	drvinfo->regdump_len = igb_get_regs_len(netdev);
	drvinfo->eedump_len = igb_get_eeprom_len(netdev);
}

static void igb_get_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = IGB_MAX_RXD;
	ring->tx_max_pending = IGB_MAX_TXD;
	ring->rx_pending = adapter->rx_ring_count;
	ring->tx_pending = adapter->tx_ring_count;
}

static int igb_set_ringparam(struct net_device *netdev,
			     struct ethtool_ringparam *ring)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct igb_ring *temp_ring;
	int i, err = 0;
	u16 new_rx_count, new_tx_count;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_rx_count = min_t(u32, ring->rx_pending, IGB_MAX_RXD);
	new_rx_count = max_t(u16, new_rx_count, IGB_MIN_RXD);
	new_rx_count = ALIGN(new_rx_count, REQ_RX_DESCRIPTOR_MULTIPLE);

	new_tx_count = min_t(u32, ring->tx_pending, IGB_MAX_TXD);
	new_tx_count = max_t(u16, new_tx_count, IGB_MIN_TXD);
	new_tx_count = ALIGN(new_tx_count, REQ_TX_DESCRIPTOR_MULTIPLE);

	if ((new_tx_count == adapter->tx_ring_count) &&
	    (new_rx_count == adapter->rx_ring_count)) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__IGB_RESETTING, &adapter->state))
		msleep(1);

	if (!netif_running(adapter->netdev)) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			adapter->tx_ring[i]->count = new_tx_count;
		for (i = 0; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i]->count = new_rx_count;
		adapter->tx_ring_count = new_tx_count;
		adapter->rx_ring_count = new_rx_count;
		goto clear_reset;
	}

	if (adapter->num_tx_queues > adapter->num_rx_queues)
		temp_ring = vmalloc(adapter->num_tx_queues * sizeof(struct igb_ring));
	else
		temp_ring = vmalloc(adapter->num_rx_queues * sizeof(struct igb_ring));

	if (!temp_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	igb_down(adapter);

	/*
	 * We can't just free everything and then setup again,
	 * because the ISRs in MSI-X mode get passed pointers
	 * to the tx and rx ring structs.
	 */
	if (new_tx_count != adapter->tx_ring_count) {
		for (i = 0; i < adapter->num_tx_queues; i++) {
			memcpy(&temp_ring[i], adapter->tx_ring[i],
			       sizeof(struct igb_ring));

			temp_ring[i].count = new_tx_count;
			err = igb_setup_tx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					igb_free_tx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}
		}

		for (i = 0; i < adapter->num_tx_queues; i++) {
			igb_free_tx_resources(adapter->tx_ring[i]);

			memcpy(adapter->tx_ring[i], &temp_ring[i],
			       sizeof(struct igb_ring));
		}

		adapter->tx_ring_count = new_tx_count;
	}

	if (new_rx_count != adapter->rx_ring_count) {
		for (i = 0; i < adapter->num_rx_queues; i++) {
			memcpy(&temp_ring[i], adapter->rx_ring[i],
			       sizeof(struct igb_ring));

			temp_ring[i].count = new_rx_count;
			err = igb_setup_rx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					igb_free_rx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}

		}

		for (i = 0; i < adapter->num_rx_queues; i++) {
			igb_free_rx_resources(adapter->rx_ring[i]);

			memcpy(adapter->rx_ring[i], &temp_ring[i],
			       sizeof(struct igb_ring));
		}

		adapter->rx_ring_count = new_rx_count;
	}
err_setup:
	igb_up(adapter);
	vfree(temp_ring);
clear_reset:
	clear_bit(__IGB_RESETTING, &adapter->state);
	return err;
}

/* ethtool register test data */
struct igb_reg_test {
	u16 reg;
	u16 reg_offset;
	u16 array_len;
	u16 test_type;
	u32 mask;
	u32 write;
};

/* In the hardware, registers are laid out either singly, in arrays
 * spaced 0x100 bytes apart, or in contiguous tables.  We assume
 * most tests take place on arrays or single registers (handled
 * as a single-element array) and special-case the tables.
 * Table tests are always pattern tests.
 *
 * We also make provision for some required setup steps by specifying
 * registers to be written without any read-back testing.
 */

#define PATTERN_TEST	1
#define SET_READ_TEST	2
#define WRITE_NO_TEST	3
#define TABLE32_TEST	4
#define TABLE64_TEST_LO	5
#define TABLE64_TEST_HI	6

/* i350 reg test */
static struct igb_reg_test reg_test_i350[] = {
	{ E1000_FCAL,	   0x100, 1,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_FCAH,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0xFFFFFFFF },
	{ E1000_FCT,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0xFFFFFFFF },
	{ E1000_VET,	   0x100, 1,  PATTERN_TEST, 0xFFFF0000, 0xFFFF0000 },
	{ E1000_RDBAL(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_RDBAH(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDLEN(0),  0x100, 4,  PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	{ E1000_RDBAL(4),  0x40,  4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_RDBAH(4),  0x40,  4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDLEN(4),  0x40,  4,  PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	/* RDH is read-only for i350, only test RDT. */
	{ E1000_RDT(0),	   0x100, 4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_RDT(4),	   0x40,  4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_FCRTH,	   0x100, 1,  PATTERN_TEST, 0x0000FFF0, 0x0000FFF0 },
	{ E1000_FCTTV,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_TIPG,	   0x100, 1,  PATTERN_TEST, 0x3FFFFFFF, 0x3FFFFFFF },
	{ E1000_TDBAL(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_TDBAH(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_TDLEN(0),  0x100, 4,  PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	{ E1000_TDBAL(4),  0x40,  4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_TDBAH(4),  0x40,  4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_TDLEN(4),  0x40,  4,  PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	{ E1000_TDT(0),	   0x100, 4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_TDT(4),	   0x40,  4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_RCTL,	   0x100, 1,  SET_READ_TEST, 0xFFFFFFFF, 0x00000000 },
	{ E1000_RCTL, 	   0x100, 1,  SET_READ_TEST, 0x04CFB0FE, 0x003FFFFB },
	{ E1000_RCTL, 	   0x100, 1,  SET_READ_TEST, 0x04CFB0FE, 0xFFFFFFFF },
	{ E1000_TCTL,	   0x100, 1,  SET_READ_TEST, 0xFFFFFFFF, 0x00000000 },
	{ E1000_RA,	   0, 16, TABLE64_TEST_LO,
						0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RA,	   0, 16, TABLE64_TEST_HI,
						0xC3FFFFFF, 0xFFFFFFFF },
	{ E1000_RA2,	   0, 16, TABLE64_TEST_LO,
						0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RA2,	   0, 16, TABLE64_TEST_HI,
						0xC3FFFFFF, 0xFFFFFFFF },
	{ E1000_MTA,	   0, 128, TABLE32_TEST,
						0xFFFFFFFF, 0xFFFFFFFF },
	{ 0, 0, 0, 0 }
};

/* 82580 reg test */
static struct igb_reg_test reg_test_82580[] = {
	{ E1000_FCAL,	   0x100, 1,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_FCAH,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0xFFFFFFFF },
	{ E1000_FCT,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0xFFFFFFFF },
	{ E1000_VET,	   0x100, 1,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDBAL(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_RDBAH(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDLEN(0),  0x100, 4,  PATTERN_TEST, 0x000FFFF0, 0x000FFFFF },
	{ E1000_RDBAL(4),  0x40,  4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_RDBAH(4),  0x40,  4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDLEN(4),  0x40,  4,  PATTERN_TEST, 0x000FFFF0, 0x000FFFFF },
	/* RDH is read-only for 82580, only test RDT. */
	{ E1000_RDT(0),	   0x100, 4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_RDT(4),	   0x40,  4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_FCRTH,	   0x100, 1,  PATTERN_TEST, 0x0000FFF0, 0x0000FFF0 },
	{ E1000_FCTTV,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_TIPG,	   0x100, 1,  PATTERN_TEST, 0x3FFFFFFF, 0x3FFFFFFF },
	{ E1000_TDBAL(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_TDBAH(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_TDLEN(0),  0x100, 4,  PATTERN_TEST, 0x000FFFF0, 0x000FFFFF },
	{ E1000_TDBAL(4),  0x40,  4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_TDBAH(4),  0x40,  4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_TDLEN(4),  0x40,  4,  PATTERN_TEST, 0x000FFFF0, 0x000FFFFF },
	{ E1000_TDT(0),	   0x100, 4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_TDT(4),	   0x40,  4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_RCTL,	   0x100, 1,  SET_READ_TEST, 0xFFFFFFFF, 0x00000000 },
	{ E1000_RCTL, 	   0x100, 1,  SET_READ_TEST, 0x04CFB0FE, 0x003FFFFB },
	{ E1000_RCTL, 	   0x100, 1,  SET_READ_TEST, 0x04CFB0FE, 0xFFFFFFFF },
	{ E1000_TCTL,	   0x100, 1,  SET_READ_TEST, 0xFFFFFFFF, 0x00000000 },
	{ E1000_RA,	   0, 16, TABLE64_TEST_LO,
						0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RA,	   0, 16, TABLE64_TEST_HI,
						0x83FFFFFF, 0xFFFFFFFF },
	{ E1000_RA2,	   0, 8, TABLE64_TEST_LO,
						0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RA2,	   0, 8, TABLE64_TEST_HI,
						0x83FFFFFF, 0xFFFFFFFF },
	{ E1000_MTA,	   0, 128, TABLE32_TEST,
						0xFFFFFFFF, 0xFFFFFFFF },
	{ 0, 0, 0, 0 }
};

/* 82576 reg test */
static struct igb_reg_test reg_test_82576[] = {
	{ E1000_FCAL,	   0x100, 1,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_FCAH,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0xFFFFFFFF },
	{ E1000_FCT,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0xFFFFFFFF },
	{ E1000_VET,	   0x100, 1,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDBAL(0),  0x100, 4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_RDBAH(0),  0x100, 4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDLEN(0),  0x100, 4, PATTERN_TEST, 0x000FFFF0, 0x000FFFFF },
	{ E1000_RDBAL(4),  0x40, 12, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_RDBAH(4),  0x40, 12, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDLEN(4),  0x40, 12, PATTERN_TEST, 0x000FFFF0, 0x000FFFFF },
	/* Enable all RX queues before testing. */
	{ E1000_RXDCTL(0), 0x100, 4,  WRITE_NO_TEST, 0, E1000_RXDCTL_QUEUE_ENABLE },
	{ E1000_RXDCTL(4), 0x40, 12,  WRITE_NO_TEST, 0, E1000_RXDCTL_QUEUE_ENABLE },
	/* RDH is read-only for 82576, only test RDT. */
	{ E1000_RDT(0),	   0x100, 4,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_RDT(4),	   0x40, 12,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_RXDCTL(0), 0x100, 4,  WRITE_NO_TEST, 0, 0 },
	{ E1000_RXDCTL(4), 0x40, 12,  WRITE_NO_TEST, 0, 0 },
	{ E1000_FCRTH,	   0x100, 1,  PATTERN_TEST, 0x0000FFF0, 0x0000FFF0 },
	{ E1000_FCTTV,	   0x100, 1,  PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_TIPG,	   0x100, 1,  PATTERN_TEST, 0x3FFFFFFF, 0x3FFFFFFF },
	{ E1000_TDBAL(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_TDBAH(0),  0x100, 4,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_TDLEN(0),  0x100, 4,  PATTERN_TEST, 0x000FFFF0, 0x000FFFFF },
	{ E1000_TDBAL(4),  0x40, 12,  PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_TDBAH(4),  0x40, 12,  PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_TDLEN(4),  0x40, 12,  PATTERN_TEST, 0x000FFFF0, 0x000FFFFF },
	{ E1000_RCTL,	   0x100, 1,  SET_READ_TEST, 0xFFFFFFFF, 0x00000000 },
	{ E1000_RCTL, 	   0x100, 1,  SET_READ_TEST, 0x04CFB0FE, 0x003FFFFB },
	{ E1000_RCTL, 	   0x100, 1,  SET_READ_TEST, 0x04CFB0FE, 0xFFFFFFFF },
	{ E1000_TCTL,	   0x100, 1,  SET_READ_TEST, 0xFFFFFFFF, 0x00000000 },
	{ E1000_RA,	   0, 16, TABLE64_TEST_LO, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RA,	   0, 16, TABLE64_TEST_HI, 0x83FFFFFF, 0xFFFFFFFF },
	{ E1000_RA2,	   0, 8, TABLE64_TEST_LO, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RA2,	   0, 8, TABLE64_TEST_HI, 0x83FFFFFF, 0xFFFFFFFF },
	{ E1000_MTA,	   0, 128,TABLE32_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ 0, 0, 0, 0 }
};

/* 82575 register test */
static struct igb_reg_test reg_test_82575[] = {
	{ E1000_FCAL,      0x100, 1, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_FCAH,      0x100, 1, PATTERN_TEST, 0x0000FFFF, 0xFFFFFFFF },
	{ E1000_FCT,       0x100, 1, PATTERN_TEST, 0x0000FFFF, 0xFFFFFFFF },
	{ E1000_VET,       0x100, 1, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDBAL(0),  0x100, 4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_RDBAH(0),  0x100, 4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RDLEN(0),  0x100, 4, PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	/* Enable all four RX queues before testing. */
	{ E1000_RXDCTL(0), 0x100, 4, WRITE_NO_TEST, 0, E1000_RXDCTL_QUEUE_ENABLE },
	/* RDH is read-only for 82575, only test RDT. */
	{ E1000_RDT(0),    0x100, 4, PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_RXDCTL(0), 0x100, 4, WRITE_NO_TEST, 0, 0 },
	{ E1000_FCRTH,     0x100, 1, PATTERN_TEST, 0x0000FFF0, 0x0000FFF0 },
	{ E1000_FCTTV,     0x100, 1, PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ E1000_TIPG,      0x100, 1, PATTERN_TEST, 0x3FFFFFFF, 0x3FFFFFFF },
	{ E1000_TDBAL(0),  0x100, 4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ E1000_TDBAH(0),  0x100, 4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_TDLEN(0),  0x100, 4, PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	{ E1000_RCTL,      0x100, 1, SET_READ_TEST, 0xFFFFFFFF, 0x00000000 },
	{ E1000_RCTL,      0x100, 1, SET_READ_TEST, 0x04CFB3FE, 0x003FFFFB },
	{ E1000_RCTL,      0x100, 1, SET_READ_TEST, 0x04CFB3FE, 0xFFFFFFFF },
	{ E1000_TCTL,      0x100, 1, SET_READ_TEST, 0xFFFFFFFF, 0x00000000 },
	{ E1000_TXCW,      0x100, 1, PATTERN_TEST, 0xC000FFFF, 0x0000FFFF },
	{ E1000_RA,        0, 16, TABLE64_TEST_LO, 0xFFFFFFFF, 0xFFFFFFFF },
	{ E1000_RA,        0, 16, TABLE64_TEST_HI, 0x800FFFFF, 0xFFFFFFFF },
	{ E1000_MTA,       0, 128, TABLE32_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ 0, 0, 0, 0 }
};

static bool reg_pattern_test(struct igb_adapter *adapter, u64 *data,
			     int reg, u32 mask, u32 write)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 pat, val;
	static const u32 _test[] =
		{0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF};
	for (pat = 0; pat < ARRAY_SIZE(_test); pat++) {
		wr32(reg, (_test[pat] & write));
		val = rd32(reg) & mask;
		if (val != (_test[pat] & write & mask)) {
			dev_err(&adapter->pdev->dev, "pattern test reg %04X "
				"failed: got 0x%08X expected 0x%08X\n",
				reg, val, (_test[pat] & write & mask));
			*data = reg;
			return 1;
		}
	}

	return 0;
}

static bool reg_set_and_check(struct igb_adapter *adapter, u64 *data,
			      int reg, u32 mask, u32 write)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 val;
	wr32(reg, write & mask);
	val = rd32(reg);
	if ((write & mask) != (val & mask)) {
		dev_err(&adapter->pdev->dev, "set/check reg %04X test failed:"
			" got 0x%08X expected 0x%08X\n", reg,
			(val & mask), (write & mask));
		*data = reg;
		return 1;
	}

	return 0;
}

#define REG_PATTERN_TEST(reg, mask, write) \
	do { \
		if (reg_pattern_test(adapter, data, reg, mask, write)) \
			return 1; \
	} while (0)

#define REG_SET_AND_CHECK(reg, mask, write) \
	do { \
		if (reg_set_and_check(adapter, data, reg, mask, write)) \
			return 1; \
	} while (0)

static int igb_reg_test(struct igb_adapter *adapter, u64 *data)
{
	struct e1000_hw *hw = &adapter->hw;
	struct igb_reg_test *test;
	u32 value, before, after;
	u32 i, toggle;

	switch (adapter->hw.mac.type) {
	case e1000_i350:
		test = reg_test_i350;
		toggle = 0x7FEFF3FF;
		break;
	case e1000_82580:
		test = reg_test_82580;
		toggle = 0x7FEFF3FF;
		break;
	case e1000_82576:
		test = reg_test_82576;
		toggle = 0x7FFFF3FF;
		break;
	default:
		test = reg_test_82575;
		toggle = 0x7FFFF3FF;
		break;
	}

	/* Because the status register is such a special case,
	 * we handle it separately from the rest of the register
	 * tests.  Some bits are read-only, some toggle, and some
	 * are writable on newer MACs.
	 */
	before = rd32(E1000_STATUS);
	value = (rd32(E1000_STATUS) & toggle);
	wr32(E1000_STATUS, toggle);
	after = rd32(E1000_STATUS) & toggle;
	if (value != after) {
		dev_err(&adapter->pdev->dev, "failed STATUS register test "
			"got: 0x%08X expected: 0x%08X\n", after, value);
		*data = 1;
		return 1;
	}
	/* restore previous status */
	wr32(E1000_STATUS, before);

	/* Perform the remainder of the register test, looping through
	 * the test table until we either fail or reach the null entry.
	 */
	while (test->reg) {
		for (i = 0; i < test->array_len; i++) {
			switch (test->test_type) {
			case PATTERN_TEST:
				REG_PATTERN_TEST(test->reg +
						(i * test->reg_offset),
						test->mask,
						test->write);
				break;
			case SET_READ_TEST:
				REG_SET_AND_CHECK(test->reg +
						(i * test->reg_offset),
						test->mask,
						test->write);
				break;
			case WRITE_NO_TEST:
				writel(test->write,
				    (adapter->hw.hw_addr + test->reg)
					+ (i * test->reg_offset));
				break;
			case TABLE32_TEST:
				REG_PATTERN_TEST(test->reg + (i * 4),
						test->mask,
						test->write);
				break;
			case TABLE64_TEST_LO:
				REG_PATTERN_TEST(test->reg + (i * 8),
						test->mask,
						test->write);
				break;
			case TABLE64_TEST_HI:
				REG_PATTERN_TEST((test->reg + 4) + (i * 8),
						test->mask,
						test->write);
				break;
			}
		}
		test++;
	}

	*data = 0;
	return 0;
}

static int igb_eeprom_test(struct igb_adapter *adapter, u64 *data)
{
	u16 temp;
	u16 checksum = 0;
	u16 i;

	*data = 0;
	/* Read and add up the contents of the EEPROM */
	for (i = 0; i < (NVM_CHECKSUM_REG + 1); i++) {
		if ((adapter->hw.nvm.ops.read(&adapter->hw, i, 1, &temp)) < 0) {
			*data = 1;
			break;
		}
		checksum += temp;
	}

	/* If Checksum is not Correct return error else test passed */
	if ((checksum != (u16) NVM_SUM) && !(*data))
		*data = 2;

	return *data;
}

static irqreturn_t igb_test_intr(int irq, void *data)
{
	struct igb_adapter *adapter = (struct igb_adapter *) data;
	struct e1000_hw *hw = &adapter->hw;

	adapter->test_icr |= rd32(E1000_ICR);

	return IRQ_HANDLED;
}

static int igb_intr_test(struct igb_adapter *adapter, u64 *data)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 mask, ics_mask, i = 0, shared_int = true;
	u32 irq = adapter->pdev->irq;

	*data = 0;

	/* Hook up test interrupt handler just for this test */
	if (adapter->msix_entries) {
		if (request_irq(adapter->msix_entries[0].vector,
		                igb_test_intr, 0, netdev->name, adapter)) {
			*data = 1;
			return -1;
		}
	} else if (adapter->flags & IGB_FLAG_HAS_MSI) {
		shared_int = false;
		if (request_irq(irq,
		                igb_test_intr, 0, netdev->name, adapter)) {
			*data = 1;
			return -1;
		}
	} else if (!request_irq(irq, igb_test_intr, IRQF_PROBE_SHARED,
				netdev->name, adapter)) {
		shared_int = false;
	} else if (request_irq(irq, igb_test_intr, IRQF_SHARED,
		 netdev->name, adapter)) {
		*data = 1;
		return -1;
	}
	dev_info(&adapter->pdev->dev, "testing %s interrupt\n",
		(shared_int ? "shared" : "unshared"));

	/* Disable all the interrupts */
	wr32(E1000_IMC, ~0);
	wrfl();
	msleep(10);

	/* Define all writable bits for ICS */
	switch (hw->mac.type) {
	case e1000_82575:
		ics_mask = 0x37F47EDD;
		break;
	case e1000_82576:
		ics_mask = 0x77D4FBFD;
		break;
	case e1000_82580:
		ics_mask = 0x77DCFED5;
		break;
	case e1000_i350:
		ics_mask = 0x77DCFED5;
		break;
	default:
		ics_mask = 0x7FFFFFFF;
		break;
	}

	/* Test each interrupt */
	for (; i < 31; i++) {
		/* Interrupt to test */
		mask = 1 << i;

		if (!(mask & ics_mask))
			continue;

		if (!shared_int) {
			/* Disable the interrupt to be reported in
			 * the cause register and then force the same
			 * interrupt and see if one gets posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;

			/* Flush any pending interrupts */
			wr32(E1000_ICR, ~0);

			wr32(E1000_IMC, mask);
			wr32(E1000_ICS, mask);
			wrfl();
			msleep(10);

			if (adapter->test_icr & mask) {
				*data = 3;
				break;
			}
		}

		/* Enable the interrupt to be reported in
		 * the cause register and then force the same
		 * interrupt and see if one gets posted.  If
		 * an interrupt was not posted to the bus, the
		 * test failed.
		 */
		adapter->test_icr = 0;

		/* Flush any pending interrupts */
		wr32(E1000_ICR, ~0);

		wr32(E1000_IMS, mask);
		wr32(E1000_ICS, mask);
		wrfl();
		msleep(10);

		if (!(adapter->test_icr & mask)) {
			*data = 4;
			break;
		}

		if (!shared_int) {
			/* Disable the other interrupts to be reported in
			 * the cause register and then force the other
			 * interrupts and see if any get posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;

			/* Flush any pending interrupts */
			wr32(E1000_ICR, ~0);

			wr32(E1000_IMC, ~mask);
			wr32(E1000_ICS, ~mask);
			wrfl();
			msleep(10);

			if (adapter->test_icr & mask) {
				*data = 5;
				break;
			}
		}
	}

	/* Disable all the interrupts */
	wr32(E1000_IMC, ~0);
	wrfl();
	msleep(10);

	/* Unhook test interrupt handler */
	if (adapter->msix_entries)
		free_irq(adapter->msix_entries[0].vector, adapter);
	else
		free_irq(irq, adapter);

	return *data;
}

static void igb_free_desc_rings(struct igb_adapter *adapter)
{
	igb_free_tx_resources(&adapter->test_tx_ring);
	igb_free_rx_resources(&adapter->test_rx_ring);
}

static int igb_setup_desc_rings(struct igb_adapter *adapter)
{
	struct igb_ring *tx_ring = &adapter->test_tx_ring;
	struct igb_ring *rx_ring = &adapter->test_rx_ring;
	struct e1000_hw *hw = &adapter->hw;
	int ret_val;

	/* Setup Tx descriptor ring and Tx buffers */
	tx_ring->count = IGB_DEFAULT_TXD;
	tx_ring->dev = &adapter->pdev->dev;
	tx_ring->netdev = adapter->netdev;
	tx_ring->reg_idx = adapter->vfs_allocated_count;

	if (igb_setup_tx_resources(tx_ring)) {
		ret_val = 1;
		goto err_nomem;
	}

	igb_setup_tctl(adapter);
	igb_configure_tx_ring(adapter, tx_ring);

	/* Setup Rx descriptor ring and Rx buffers */
	rx_ring->count = IGB_DEFAULT_RXD;
	rx_ring->dev = &adapter->pdev->dev;
	rx_ring->netdev = adapter->netdev;
	rx_ring->reg_idx = adapter->vfs_allocated_count;

	if (igb_setup_rx_resources(rx_ring)) {
		ret_val = 3;
		goto err_nomem;
	}

	/* set the default queue to queue 0 of PF */
	wr32(E1000_MRQC, adapter->vfs_allocated_count << 3);

	/* enable receive ring */
	igb_setup_rctl(adapter);
	igb_configure_rx_ring(adapter, rx_ring);

	igb_alloc_rx_buffers(rx_ring, igb_desc_unused(rx_ring));

	return 0;

err_nomem:
	igb_free_desc_rings(adapter);
	return ret_val;
}

static void igb_phy_disable_receiver(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	/* Write out to PHY registers 29 and 30 to disable the Receiver. */
	igb_write_phy_reg(hw, 29, 0x001F);
	igb_write_phy_reg(hw, 30, 0x8FFC);
	igb_write_phy_reg(hw, 29, 0x001A);
	igb_write_phy_reg(hw, 30, 0x8FF0);
}

static int igb_integrated_phy_loopback(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_reg = 0;

	hw->mac.autoneg = false;

	if (hw->phy.type == e1000_phy_m88) {
		/* Auto-MDI/MDIX Off */
		igb_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, 0x0808);
		/* reset to update Auto-MDI/MDIX */
		igb_write_phy_reg(hw, PHY_CONTROL, 0x9140);
		/* autoneg off */
		igb_write_phy_reg(hw, PHY_CONTROL, 0x8140);
	} else if (hw->phy.type == e1000_phy_82580) {
		/* enable MII loopback */
		igb_write_phy_reg(hw, I82580_PHY_LBK_CTRL, 0x8041);
	}

	ctrl_reg = rd32(E1000_CTRL);

	/* force 1000, set loopback */
	igb_write_phy_reg(hw, PHY_CONTROL, 0x4140);

	/* Now set up the MAC to the same speed/duplex as the PHY. */
	ctrl_reg = rd32(E1000_CTRL);
	ctrl_reg &= ~E1000_CTRL_SPD_SEL; /* Clear the speed sel bits */
	ctrl_reg |= (E1000_CTRL_FRCSPD | /* Set the Force Speed Bit */
		     E1000_CTRL_FRCDPX | /* Set the Force Duplex Bit */
		     E1000_CTRL_SPD_1000 |/* Force Speed to 1000 */
		     E1000_CTRL_FD |	 /* Force Duplex to FULL */
		     E1000_CTRL_SLU);	 /* Set link up enable bit */

	if (hw->phy.type == e1000_phy_m88)
		ctrl_reg |= E1000_CTRL_ILOS; /* Invert Loss of Signal */

	wr32(E1000_CTRL, ctrl_reg);

	/* Disable the receiver on the PHY so when a cable is plugged in, the
	 * PHY does not begin to autoneg when a cable is reconnected to the NIC.
	 */
	if (hw->phy.type == e1000_phy_m88)
		igb_phy_disable_receiver(adapter);

	udelay(500);

	return 0;
}

static int igb_set_phy_loopback(struct igb_adapter *adapter)
{
	return igb_integrated_phy_loopback(adapter);
}

static int igb_setup_loopback_test(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 reg;

	reg = rd32(E1000_CTRL_EXT);

	/* use CTRL_EXT to identify link type as SGMII can appear as copper */
	if (reg & E1000_CTRL_EXT_LINK_MODE_MASK) {
		if ((hw->device_id == E1000_DEV_ID_DH89XXCC_SGMII) ||
		(hw->device_id == E1000_DEV_ID_DH89XXCC_SERDES) ||
		(hw->device_id == E1000_DEV_ID_DH89XXCC_BACKPLANE) ||
		(hw->device_id == E1000_DEV_ID_DH89XXCC_SFP)) {

			/* Enable DH89xxCC MPHY for near end loopback */
			reg = rd32(E1000_MPHY_ADDR_CTL);
			reg = (reg & E1000_MPHY_ADDR_CTL_OFFSET_MASK) |
			E1000_MPHY_PCS_CLK_REG_OFFSET;
			wr32(E1000_MPHY_ADDR_CTL, reg);

			reg = rd32(E1000_MPHY_DATA);
			reg |= E1000_MPHY_PCS_CLK_REG_DIGINELBEN;
			wr32(E1000_MPHY_DATA, reg);
		}

		reg = rd32(E1000_RCTL);
		reg |= E1000_RCTL_LBM_TCVR;
		wr32(E1000_RCTL, reg);

		wr32(E1000_SCTL, E1000_ENABLE_SERDES_LOOPBACK);

		reg = rd32(E1000_CTRL);
		reg &= ~(E1000_CTRL_RFCE |
			 E1000_CTRL_TFCE |
			 E1000_CTRL_LRST);
		reg |= E1000_CTRL_SLU |
		       E1000_CTRL_FD;
		wr32(E1000_CTRL, reg);

		/* Unset switch control to serdes energy detect */
		reg = rd32(E1000_CONNSW);
		reg &= ~E1000_CONNSW_ENRGSRC;
		wr32(E1000_CONNSW, reg);

		/* Set PCS register for forced speed */
		reg = rd32(E1000_PCS_LCTL);
		reg &= ~E1000_PCS_LCTL_AN_ENABLE;     /* Disable Autoneg*/
		reg |= E1000_PCS_LCTL_FLV_LINK_UP |   /* Force link up */
		       E1000_PCS_LCTL_FSV_1000 |      /* Force 1000    */
		       E1000_PCS_LCTL_FDV_FULL |      /* SerDes Full duplex */
		       E1000_PCS_LCTL_FSD |           /* Force Speed */
		       E1000_PCS_LCTL_FORCE_LINK;     /* Force Link */
		wr32(E1000_PCS_LCTL, reg);

		return 0;
	}

	return igb_set_phy_loopback(adapter);
}

static void igb_loopback_cleanup(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;
	u16 phy_reg;

	if ((hw->device_id == E1000_DEV_ID_DH89XXCC_SGMII) ||
	(hw->device_id == E1000_DEV_ID_DH89XXCC_SERDES) ||
	(hw->device_id == E1000_DEV_ID_DH89XXCC_BACKPLANE) ||
	(hw->device_id == E1000_DEV_ID_DH89XXCC_SFP)) {
		u32 reg;

		/* Disable near end loopback on DH89xxCC */
		reg = rd32(E1000_MPHY_ADDR_CTL);
		reg = (reg & E1000_MPHY_ADDR_CTL_OFFSET_MASK) |
		E1000_MPHY_PCS_CLK_REG_OFFSET;
		wr32(E1000_MPHY_ADDR_CTL, reg);

		reg = rd32(E1000_MPHY_DATA);
		reg &= ~E1000_MPHY_PCS_CLK_REG_DIGINELBEN;
		wr32(E1000_MPHY_DATA, reg);
	}

	rctl = rd32(E1000_RCTL);
	rctl &= ~(E1000_RCTL_LBM_TCVR | E1000_RCTL_LBM_MAC);
	wr32(E1000_RCTL, rctl);

	hw->mac.autoneg = true;
	igb_read_phy_reg(hw, PHY_CONTROL, &phy_reg);
	if (phy_reg & MII_CR_LOOPBACK) {
		phy_reg &= ~MII_CR_LOOPBACK;
		igb_write_phy_reg(hw, PHY_CONTROL, phy_reg);
		igb_phy_sw_reset(hw);
	}
}

static void igb_create_lbtest_frame(struct sk_buff *skb,
				    unsigned int frame_size)
{
	memset(skb->data, 0xFF, frame_size);
	frame_size /= 2;
	memset(&skb->data[frame_size], 0xAA, frame_size - 1);
	memset(&skb->data[frame_size + 10], 0xBE, 1);
	memset(&skb->data[frame_size + 12], 0xAF, 1);
}

static int igb_check_lbtest_frame(struct sk_buff *skb, unsigned int frame_size)
{
	frame_size /= 2;
	if (*(skb->data + 3) == 0xFF) {
		if ((*(skb->data + frame_size + 10) == 0xBE) &&
		   (*(skb->data + frame_size + 12) == 0xAF)) {
			return 0;
		}
	}
	return 13;
}

static int igb_clean_test_rings(struct igb_ring *rx_ring,
                                struct igb_ring *tx_ring,
                                unsigned int size)
{
	union e1000_adv_rx_desc *rx_desc;
	struct igb_rx_buffer *rx_buffer_info;
	struct igb_tx_buffer *tx_buffer_info;
	u16 rx_ntc, tx_ntc, count = 0;

	/* initialize next to clean and descriptor values */
	rx_ntc = rx_ring->next_to_clean;
	tx_ntc = tx_ring->next_to_clean;
	rx_desc = IGB_RX_DESC(rx_ring, rx_ntc);

	while (igb_test_staterr(rx_desc, E1000_RXD_STAT_DD)) {
		/* check rx buffer */
		rx_buffer_info = &rx_ring->rx_buffer_info[rx_ntc];

		/* unmap rx buffer, will be remapped by alloc_rx_buffers */
		dma_unmap_single(rx_ring->dev,
				 rx_buffer_info->dma,
				 IGB_RX_HDR_LEN,
				 DMA_FROM_DEVICE);
		rx_buffer_info->dma = 0;

		/* verify contents of skb */
		if (!igb_check_lbtest_frame(rx_buffer_info->skb, size))
			count++;

		/* unmap buffer on tx side */
		tx_buffer_info = &tx_ring->tx_buffer_info[tx_ntc];
		igb_unmap_and_free_tx_resource(tx_ring, tx_buffer_info);

		/* increment rx/tx next to clean counters */
		rx_ntc++;
		if (rx_ntc == rx_ring->count)
			rx_ntc = 0;
		tx_ntc++;
		if (tx_ntc == tx_ring->count)
			tx_ntc = 0;

		/* fetch next descriptor */
		rx_desc = IGB_RX_DESC(rx_ring, rx_ntc);
	}

	/* re-map buffers to ring, store next to clean values */
	igb_alloc_rx_buffers(rx_ring, count);
	rx_ring->next_to_clean = rx_ntc;
	tx_ring->next_to_clean = tx_ntc;

	return count;
}

static int igb_run_loopback_test(struct igb_adapter *adapter)
{
	struct igb_ring *tx_ring = &adapter->test_tx_ring;
	struct igb_ring *rx_ring = &adapter->test_rx_ring;
	u16 i, j, lc, good_cnt;
	int ret_val = 0;
	unsigned int size = IGB_RX_HDR_LEN;
	netdev_tx_t tx_ret_val;
	struct sk_buff *skb;

	/* allocate test skb */
	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return 11;

	/* place data into test skb */
	igb_create_lbtest_frame(skb, size);
	skb_put(skb, size);

	/*
	 * Calculate the loop count based on the largest descriptor ring
	 * The idea is to wrap the largest ring a number of times using 64
	 * send/receive pairs during each loop
	 */

	if (rx_ring->count <= tx_ring->count)
		lc = ((tx_ring->count / 64) * 2) + 1;
	else
		lc = ((rx_ring->count / 64) * 2) + 1;

	for (j = 0; j <= lc; j++) { /* loop count loop */
		/* reset count of good packets */
		good_cnt = 0;

		/* place 64 packets on the transmit queue*/
		for (i = 0; i < 64; i++) {
			skb_get(skb);
			tx_ret_val = igb_xmit_frame_ring(skb, tx_ring);
			if (tx_ret_val == NETDEV_TX_OK)
				good_cnt++;
		}

		if (good_cnt != 64) {
			ret_val = 12;
			break;
		}

		/* allow 200 milliseconds for packets to go from tx to rx */
		msleep(200);

		good_cnt = igb_clean_test_rings(rx_ring, tx_ring, size);
		if (good_cnt != 64) {
			ret_val = 13;
			break;
		}
	} /* end loop count loop */

	/* free the original skb */
	kfree_skb(skb);

	return ret_val;
}

static int igb_loopback_test(struct igb_adapter *adapter, u64 *data)
{
	/* PHY loopback cannot be performed if SoL/IDER
	 * sessions are active */
	if (igb_check_reset_block(&adapter->hw)) {
		dev_err(&adapter->pdev->dev,
			"Cannot do PHY loopback test "
			"when SoL/IDER is active.\n");
		*data = 0;
		goto out;
	}
	*data = igb_setup_desc_rings(adapter);
	if (*data)
		goto out;
	*data = igb_setup_loopback_test(adapter);
	if (*data)
		goto err_loopback;
	*data = igb_run_loopback_test(adapter);
	igb_loopback_cleanup(adapter);

err_loopback:
	igb_free_desc_rings(adapter);
out:
	return *data;
}

static int igb_link_test(struct igb_adapter *adapter, u64 *data)
{
	struct e1000_hw *hw = &adapter->hw;
	*data = 0;
	if (hw->phy.media_type == e1000_media_type_internal_serdes) {
		int i = 0;
		hw->mac.serdes_has_link = false;

		/* On some blade server designs, link establishment
		 * could take as long as 2-3 minutes */
		do {
			hw->mac.ops.check_for_link(&adapter->hw);
			if (hw->mac.serdes_has_link)
				return *data;
			msleep(20);
		} while (i++ < 3750);

		*data = 1;
	} else {
		hw->mac.ops.check_for_link(&adapter->hw);
		if (hw->mac.autoneg)
			msleep(4000);

		if (!(rd32(E1000_STATUS) & E1000_STATUS_LU))
			*data = 1;
	}
	return *data;
}

static void igb_diag_test(struct net_device *netdev,
			  struct ethtool_test *eth_test, u64 *data)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	u16 autoneg_advertised;
	u8 forced_speed_duplex, autoneg;
	bool if_running = netif_running(netdev);

	set_bit(__IGB_TESTING, &adapter->state);
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */

		/* save speed, duplex, autoneg settings */
		autoneg_advertised = adapter->hw.phy.autoneg_advertised;
		forced_speed_duplex = adapter->hw.mac.forced_speed_duplex;
		autoneg = adapter->hw.mac.autoneg;

		dev_info(&adapter->pdev->dev, "offline testing starting\n");

		/* power up link for link test */
		igb_power_up_link(adapter);

		/* Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result */
		if (igb_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			/* indicate we're in test mode */
			dev_close(netdev);
		else
			igb_reset(adapter);

		if (igb_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		igb_reset(adapter);
		if (igb_eeprom_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		igb_reset(adapter);
		if (igb_intr_test(adapter, &data[2]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		igb_reset(adapter);
		/* power up link for loopback test */
		igb_power_up_link(adapter);
		if (igb_loopback_test(adapter, &data[3]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* restore speed, duplex, autoneg settings */
		adapter->hw.phy.autoneg_advertised = autoneg_advertised;
		adapter->hw.mac.forced_speed_duplex = forced_speed_duplex;
		adapter->hw.mac.autoneg = autoneg;

		/* force this routine to wait until autoneg complete/timeout */
		adapter->hw.phy.autoneg_wait_to_complete = true;
		igb_reset(adapter);
		adapter->hw.phy.autoneg_wait_to_complete = false;

		clear_bit(__IGB_TESTING, &adapter->state);
		if (if_running)
			dev_open(netdev);
	} else {
		dev_info(&adapter->pdev->dev, "online testing starting\n");

		/* PHY is powered down when interface is down */
		if (if_running && igb_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;
		else
			data[4] = 0;

		/* Online tests aren't run; pass by default */
		data[0] = 0;
		data[1] = 0;
		data[2] = 0;
		data[3] = 0;

		clear_bit(__IGB_TESTING, &adapter->state);
	}
	msleep_interruptible(4 * 1000);
}

static int igb_wol_exclusion(struct igb_adapter *adapter,
			     struct ethtool_wolinfo *wol)
{
	struct e1000_hw *hw = &adapter->hw;
	int retval = 1; /* fail by default */

	switch (hw->device_id) {
	case E1000_DEV_ID_82575GB_QUAD_COPPER:
		/* WoL not supported */
		wol->supported = 0;
		break;
	case E1000_DEV_ID_82575EB_FIBER_SERDES:
	case E1000_DEV_ID_82576_FIBER:
	case E1000_DEV_ID_82576_SERDES:
		/* Wake events not supported on port B */
		if (rd32(E1000_STATUS) & E1000_STATUS_FUNC_1) {
			wol->supported = 0;
			break;
		}
		/* return success for non excluded adapter ports */
		retval = 0;
		break;
	case E1000_DEV_ID_82576_QUAD_COPPER:
	case E1000_DEV_ID_82576_QUAD_COPPER_ET2:
		/* quad port adapters only support WoL on port A */
		if (!(adapter->flags & IGB_FLAG_QUAD_PORT_A)) {
			wol->supported = 0;
			break;
		}
		/* return success for non excluded adapter ports */
		retval = 0;
		break;
	default:
		/* dual port cards only support WoL on port A from now on
		 * unless it was enabled in the eeprom for port B
		 * so exclude FUNC_1 ports from having WoL enabled */
		if ((rd32(E1000_STATUS) & E1000_STATUS_FUNC_MASK) &&
		    !adapter->eeprom_wol) {
			wol->supported = 0;
			break;
		}

		retval = 0;
	}

	return retval;
}

static void igb_get_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	wol->supported = WAKE_UCAST | WAKE_MCAST |
	                 WAKE_BCAST | WAKE_MAGIC |
	                 WAKE_PHY;
	wol->wolopts = 0;

	/* this function will set ->supported = 0 and return 1 if wol is not
	 * supported by this hardware */
	if (igb_wol_exclusion(adapter, wol) ||
	    !device_can_wakeup(&adapter->pdev->dev))
		return;

	/* apply any specific unsupported masks here */
	switch (adapter->hw.device_id) {
	default:
		break;
	}

	if (adapter->wol & E1000_WUFC_EX)
		wol->wolopts |= WAKE_UCAST;
	if (adapter->wol & E1000_WUFC_MC)
		wol->wolopts |= WAKE_MCAST;
	if (adapter->wol & E1000_WUFC_BC)
		wol->wolopts |= WAKE_BCAST;
	if (adapter->wol & E1000_WUFC_MAG)
		wol->wolopts |= WAKE_MAGIC;
	if (adapter->wol & E1000_WUFC_LNKC)
		wol->wolopts |= WAKE_PHY;
}

static int igb_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_ARP | WAKE_MAGICSECURE))
		return -EOPNOTSUPP;

	if (igb_wol_exclusion(adapter, wol) ||
	    !device_can_wakeup(&adapter->pdev->dev))
		return wol->wolopts ? -EOPNOTSUPP : 0;

	/* these settings will always override what we currently have */
	adapter->wol = 0;

	if (wol->wolopts & WAKE_UCAST)
		adapter->wol |= E1000_WUFC_EX;
	if (wol->wolopts & WAKE_MCAST)
		adapter->wol |= E1000_WUFC_MC;
	if (wol->wolopts & WAKE_BCAST)
		adapter->wol |= E1000_WUFC_BC;
	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol |= E1000_WUFC_MAG;
	if (wol->wolopts & WAKE_PHY)
		adapter->wol |= E1000_WUFC_LNKC;
	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	return 0;
}

/* bit defines for adapter->led_status */
#define IGB_LED_ON		0

static int igb_set_phys_id(struct net_device *netdev,
			   enum ethtool_phys_id_state state)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		igb_blink_led(hw);
		return 2;
	case ETHTOOL_ID_ON:
		igb_blink_led(hw);
		break;
	case ETHTOOL_ID_OFF:
		igb_led_off(hw);
		break;
	case ETHTOOL_ID_INACTIVE:
		igb_led_off(hw);
		clear_bit(IGB_LED_ON, &adapter->led_status);
		igb_cleanup_led(hw);
		break;
	}

	return 0;
}

static int igb_set_coalesce(struct net_device *netdev,
			    struct ethtool_coalesce *ec)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	int i;

	if ((ec->rx_coalesce_usecs > IGB_MAX_ITR_USECS) ||
	    ((ec->rx_coalesce_usecs > 3) &&
	     (ec->rx_coalesce_usecs < IGB_MIN_ITR_USECS)) ||
	    (ec->rx_coalesce_usecs == 2))
		return -EINVAL;

	if ((ec->tx_coalesce_usecs > IGB_MAX_ITR_USECS) ||
	    ((ec->tx_coalesce_usecs > 3) &&
	     (ec->tx_coalesce_usecs < IGB_MIN_ITR_USECS)) ||
	    (ec->tx_coalesce_usecs == 2))
		return -EINVAL;

	if ((adapter->flags & IGB_FLAG_QUEUE_PAIRS) && ec->tx_coalesce_usecs)
		return -EINVAL;

	/* If ITR is disabled, disable DMAC */
	if (ec->rx_coalesce_usecs == 0) {
		if (adapter->flags & IGB_FLAG_DMAC)
			adapter->flags &= ~IGB_FLAG_DMAC;
	}

	/* convert to rate of irq's per second */
	if (ec->rx_coalesce_usecs && ec->rx_coalesce_usecs <= 3)
		adapter->rx_itr_setting = ec->rx_coalesce_usecs;
	else
		adapter->rx_itr_setting = ec->rx_coalesce_usecs << 2;

	/* convert to rate of irq's per second */
	if (adapter->flags & IGB_FLAG_QUEUE_PAIRS)
		adapter->tx_itr_setting = adapter->rx_itr_setting;
	else if (ec->tx_coalesce_usecs && ec->tx_coalesce_usecs <= 3)
		adapter->tx_itr_setting = ec->tx_coalesce_usecs;
	else
		adapter->tx_itr_setting = ec->tx_coalesce_usecs << 2;

	for (i = 0; i < adapter->num_q_vectors; i++) {
		struct igb_q_vector *q_vector = adapter->q_vector[i];
		q_vector->tx.work_limit = adapter->tx_work_limit;
		if (q_vector->rx.ring)
			q_vector->itr_val = adapter->rx_itr_setting;
		else
			q_vector->itr_val = adapter->tx_itr_setting;
		if (q_vector->itr_val && q_vector->itr_val <= 3)
			q_vector->itr_val = IGB_START_ITR;
		q_vector->set_itr = 1;
	}

	return 0;
}

static int igb_get_coalesce(struct net_device *netdev,
			    struct ethtool_coalesce *ec)
{
	struct igb_adapter *adapter = netdev_priv(netdev);

	if (adapter->rx_itr_setting <= 3)
		ec->rx_coalesce_usecs = adapter->rx_itr_setting;
	else
		ec->rx_coalesce_usecs = adapter->rx_itr_setting >> 2;

	if (!(adapter->flags & IGB_FLAG_QUEUE_PAIRS)) {
		if (adapter->tx_itr_setting <= 3)
			ec->tx_coalesce_usecs = adapter->tx_itr_setting;
		else
			ec->tx_coalesce_usecs = adapter->tx_itr_setting >> 2;
	}

	return 0;
}

static int igb_nway_reset(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	if (netif_running(netdev))
		igb_reinit_locked(adapter);
	return 0;
}

static int igb_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return IGB_STATS_LEN;
	case ETH_SS_TEST:
		return IGB_TEST_LEN;
	default:
		return -ENOTSUPP;
	}
}

static void igb_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct rtnl_link_stats64 *net_stats = &adapter->stats64;
	unsigned int start;
	struct igb_ring *ring;
	int i, j;
	char *p;

	spin_lock(&adapter->stats64_lock);
	igb_update_stats(adapter, net_stats);

	for (i = 0; i < IGB_GLOBAL_STATS_LEN; i++) {
		p = (char *)adapter + igb_gstrings_stats[i].stat_offset;
		data[i] = (igb_gstrings_stats[i].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	for (j = 0; j < IGB_NETDEV_STATS_LEN; j++, i++) {
		p = (char *)net_stats + igb_gstrings_net_stats[j].stat_offset;
		data[i] = (igb_gstrings_net_stats[j].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	for (j = 0; j < adapter->num_tx_queues; j++) {
		u64	restart2;

		ring = adapter->tx_ring[j];
		do {
			start = u64_stats_fetch_begin_bh(&ring->tx_syncp);
			data[i]   = ring->tx_stats.packets;
			data[i+1] = ring->tx_stats.bytes;
			data[i+2] = ring->tx_stats.restart_queue;
		} while (u64_stats_fetch_retry_bh(&ring->tx_syncp, start));
		do {
			start = u64_stats_fetch_begin_bh(&ring->tx_syncp2);
			restart2  = ring->tx_stats.restart_queue2;
		} while (u64_stats_fetch_retry_bh(&ring->tx_syncp2, start));
		data[i+2] += restart2;

		i += IGB_TX_QUEUE_STATS_LEN;
	}
	for (j = 0; j < adapter->num_rx_queues; j++) {
		ring = adapter->rx_ring[j];
		do {
			start = u64_stats_fetch_begin_bh(&ring->rx_syncp);
			data[i]   = ring->rx_stats.packets;
			data[i+1] = ring->rx_stats.bytes;
			data[i+2] = ring->rx_stats.drops;
			data[i+3] = ring->rx_stats.csum_err;
			data[i+4] = ring->rx_stats.alloc_failed;
		} while (u64_stats_fetch_retry_bh(&ring->rx_syncp, start));
		i += IGB_RX_QUEUE_STATS_LEN;
	}
	spin_unlock(&adapter->stats64_lock);
}

static void igb_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *igb_gstrings_test,
			IGB_TEST_LEN*ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (i = 0; i < IGB_GLOBAL_STATS_LEN; i++) {
			memcpy(p, igb_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < IGB_NETDEV_STATS_LEN; i++) {
			memcpy(p, igb_gstrings_net_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->num_tx_queues; i++) {
			sprintf(p, "tx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_queue_%u_restart", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->num_rx_queues; i++) {
			sprintf(p, "rx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_drops", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_csum_err", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_alloc_failed", i);
			p += ETH_GSTRING_LEN;
		}
/*		BUG_ON(p - data != IGB_STATS_LEN * ETH_GSTRING_LEN); */
		break;
	}
}

static int igb_ethtool_begin(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	pm_runtime_get_sync(&adapter->pdev->dev);
	return 0;
}

static void igb_ethtool_complete(struct net_device *netdev)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	pm_runtime_put(&adapter->pdev->dev);
}

static const struct ethtool_ops igb_ethtool_ops = {
	.get_settings           = igb_get_settings,
	.set_settings           = igb_set_settings,
	.get_drvinfo            = igb_get_drvinfo,
	.get_regs_len           = igb_get_regs_len,
	.get_regs               = igb_get_regs,
	.get_wol                = igb_get_wol,
	.set_wol                = igb_set_wol,
	.get_msglevel           = igb_get_msglevel,
	.set_msglevel           = igb_set_msglevel,
	.nway_reset             = igb_nway_reset,
	.get_link               = igb_get_link,
	.get_eeprom_len         = igb_get_eeprom_len,
	.get_eeprom             = igb_get_eeprom,
	.set_eeprom             = igb_set_eeprom,
	.get_ringparam          = igb_get_ringparam,
	.set_ringparam          = igb_set_ringparam,
	.get_pauseparam         = igb_get_pauseparam,
	.set_pauseparam         = igb_set_pauseparam,
	.self_test              = igb_diag_test,
	.get_strings            = igb_get_strings,
	.set_phys_id            = igb_set_phys_id,
	.get_sset_count         = igb_get_sset_count,
	.get_ethtool_stats      = igb_get_ethtool_stats,
	.get_coalesce           = igb_get_coalesce,
	.set_coalesce           = igb_set_coalesce,
	.begin			= igb_ethtool_begin,
	.complete		= igb_ethtool_complete,
};

void igb_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &igb_ethtool_ops);
}
