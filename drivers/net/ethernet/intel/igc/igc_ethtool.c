// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

/* ethtool support for igc */
#include <linux/if_vlan.h>
#include <linux/pm_runtime.h>
#include <linux/mdio.h>

#include "igc.h"
#include "igc_diag.h"

/* forward declaration */
struct igc_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define IGC_STAT(_name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = sizeof_field(struct igc_adapter, _stat), \
	.stat_offset = offsetof(struct igc_adapter, _stat) \
}

static const struct igc_stats igc_gstrings_stats[] = {
	IGC_STAT("rx_packets", stats.gprc),
	IGC_STAT("tx_packets", stats.gptc),
	IGC_STAT("rx_bytes", stats.gorc),
	IGC_STAT("tx_bytes", stats.gotc),
	IGC_STAT("rx_broadcast", stats.bprc),
	IGC_STAT("tx_broadcast", stats.bptc),
	IGC_STAT("rx_multicast", stats.mprc),
	IGC_STAT("tx_multicast", stats.mptc),
	IGC_STAT("multicast", stats.mprc),
	IGC_STAT("collisions", stats.colc),
	IGC_STAT("rx_crc_errors", stats.crcerrs),
	IGC_STAT("rx_no_buffer_count", stats.rnbc),
	IGC_STAT("rx_missed_errors", stats.mpc),
	IGC_STAT("tx_aborted_errors", stats.ecol),
	IGC_STAT("tx_carrier_errors", stats.tncrs),
	IGC_STAT("tx_window_errors", stats.latecol),
	IGC_STAT("tx_abort_late_coll", stats.latecol),
	IGC_STAT("tx_deferred_ok", stats.dc),
	IGC_STAT("tx_single_coll_ok", stats.scc),
	IGC_STAT("tx_multi_coll_ok", stats.mcc),
	IGC_STAT("tx_timeout_count", tx_timeout_count),
	IGC_STAT("rx_long_length_errors", stats.roc),
	IGC_STAT("rx_short_length_errors", stats.ruc),
	IGC_STAT("rx_align_errors", stats.algnerrc),
	IGC_STAT("tx_tcp_seg_good", stats.tsctc),
	IGC_STAT("tx_tcp_seg_failed", stats.tsctfc),
	IGC_STAT("rx_flow_control_xon", stats.xonrxc),
	IGC_STAT("rx_flow_control_xoff", stats.xoffrxc),
	IGC_STAT("tx_flow_control_xon", stats.xontxc),
	IGC_STAT("tx_flow_control_xoff", stats.xofftxc),
	IGC_STAT("rx_long_byte_count", stats.gorc),
	IGC_STAT("tx_dma_out_of_sync", stats.doosync),
	IGC_STAT("tx_smbus", stats.mgptc),
	IGC_STAT("rx_smbus", stats.mgprc),
	IGC_STAT("dropped_smbus", stats.mgpdc),
	IGC_STAT("os2bmc_rx_by_bmc", stats.o2bgptc),
	IGC_STAT("os2bmc_tx_by_bmc", stats.b2ospc),
	IGC_STAT("os2bmc_tx_by_host", stats.o2bspc),
	IGC_STAT("os2bmc_rx_by_host", stats.b2ogprc),
	IGC_STAT("tx_hwtstamp_timeouts", tx_hwtstamp_timeouts),
	IGC_STAT("tx_hwtstamp_skipped", tx_hwtstamp_skipped),
	IGC_STAT("rx_hwtstamp_cleared", rx_hwtstamp_cleared),
	IGC_STAT("tx_lpi_counter", stats.tlpic),
	IGC_STAT("rx_lpi_counter", stats.rlpic),
};

#define IGC_NETDEV_STAT(_net_stat) { \
	.stat_string = __stringify(_net_stat), \
	.sizeof_stat = sizeof_field(struct rtnl_link_stats64, _net_stat), \
	.stat_offset = offsetof(struct rtnl_link_stats64, _net_stat) \
}

static const struct igc_stats igc_gstrings_net_stats[] = {
	IGC_NETDEV_STAT(rx_errors),
	IGC_NETDEV_STAT(tx_errors),
	IGC_NETDEV_STAT(tx_dropped),
	IGC_NETDEV_STAT(rx_length_errors),
	IGC_NETDEV_STAT(rx_over_errors),
	IGC_NETDEV_STAT(rx_frame_errors),
	IGC_NETDEV_STAT(rx_fifo_errors),
	IGC_NETDEV_STAT(tx_fifo_errors),
	IGC_NETDEV_STAT(tx_heartbeat_errors)
};

enum igc_diagnostics_results {
	TEST_REG = 0,
	TEST_EEP,
	TEST_IRQ,
	TEST_LOOP,
	TEST_LINK
};

static const char igc_gstrings_test[][ETH_GSTRING_LEN] = {
	[TEST_REG]  = "Register test  (offline)",
	[TEST_EEP]  = "Eeprom test    (offline)",
	[TEST_IRQ]  = "Interrupt test (offline)",
	[TEST_LOOP] = "Loopback test  (offline)",
	[TEST_LINK] = "Link test   (on/offline)"
};

#define IGC_TEST_LEN (sizeof(igc_gstrings_test) / ETH_GSTRING_LEN)

#define IGC_GLOBAL_STATS_LEN	\
	(sizeof(igc_gstrings_stats) / sizeof(struct igc_stats))
#define IGC_NETDEV_STATS_LEN	\
	(sizeof(igc_gstrings_net_stats) / sizeof(struct igc_stats))
#define IGC_RX_QUEUE_STATS_LEN \
	(sizeof(struct igc_rx_queue_stats) / sizeof(u64))
#define IGC_TX_QUEUE_STATS_LEN 3 /* packets, bytes, restart_queue */
#define IGC_QUEUE_STATS_LEN \
	((((struct igc_adapter *)netdev_priv(netdev))->num_rx_queues * \
	  IGC_RX_QUEUE_STATS_LEN) + \
	 (((struct igc_adapter *)netdev_priv(netdev))->num_tx_queues * \
	  IGC_TX_QUEUE_STATS_LEN))
#define IGC_STATS_LEN \
	(IGC_GLOBAL_STATS_LEN + IGC_NETDEV_STATS_LEN + IGC_QUEUE_STATS_LEN)

static const char igc_priv_flags_strings[][ETH_GSTRING_LEN] = {
#define IGC_PRIV_FLAGS_LEGACY_RX	BIT(0)
	"legacy-rx",
};

#define IGC_PRIV_FLAGS_STR_LEN ARRAY_SIZE(igc_priv_flags_strings)

static void igc_ethtool_get_drvinfo(struct net_device *netdev,
				    struct ethtool_drvinfo *drvinfo)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u16 nvm_version = 0;
	u16 gphy_version;

	strscpy(drvinfo->driver, igc_driver_name, sizeof(drvinfo->driver));

	/* NVM image version is reported as firmware version for i225 device */
	hw->nvm.ops.read(hw, IGC_NVM_DEV_STARTER, 1, &nvm_version);

	/* gPHY firmware version is reported as PHY FW version */
	gphy_version = igc_read_phy_fw_version(hw);

	scnprintf(adapter->fw_version,
		  sizeof(adapter->fw_version),
		  "%x:%x",
		  nvm_version,
		  gphy_version);

	strscpy(drvinfo->fw_version, adapter->fw_version,
		sizeof(drvinfo->fw_version));

	strscpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));

	drvinfo->n_priv_flags = IGC_PRIV_FLAGS_STR_LEN;
}

static int igc_ethtool_get_regs_len(struct net_device *netdev)
{
	return IGC_REGS_LEN * sizeof(u32);
}

static void igc_ethtool_get_regs(struct net_device *netdev,
				 struct ethtool_regs *regs, void *p)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u8 i;

	memset(p, 0, IGC_REGS_LEN * sizeof(u32));

	regs->version = (2u << 24) | (hw->revision_id << 16) | hw->device_id;

	/* General Registers */
	regs_buff[0] = rd32(IGC_CTRL);
	regs_buff[1] = rd32(IGC_STATUS);
	regs_buff[2] = rd32(IGC_CTRL_EXT);
	regs_buff[3] = rd32(IGC_MDIC);
	regs_buff[4] = rd32(IGC_CONNSW);

	/* NVM Register */
	regs_buff[5] = rd32(IGC_EECD);

	/* Interrupt */
	/* Reading EICS for EICR because they read the
	 * same but EICS does not clear on read
	 */
	regs_buff[6] = rd32(IGC_EICS);
	regs_buff[7] = rd32(IGC_EICS);
	regs_buff[8] = rd32(IGC_EIMS);
	regs_buff[9] = rd32(IGC_EIMC);
	regs_buff[10] = rd32(IGC_EIAC);
	regs_buff[11] = rd32(IGC_EIAM);
	/* Reading ICS for ICR because they read the
	 * same but ICS does not clear on read
	 */
	regs_buff[12] = rd32(IGC_ICS);
	regs_buff[13] = rd32(IGC_ICS);
	regs_buff[14] = rd32(IGC_IMS);
	regs_buff[15] = rd32(IGC_IMC);
	regs_buff[16] = rd32(IGC_IAC);
	regs_buff[17] = rd32(IGC_IAM);

	/* Flow Control */
	regs_buff[18] = rd32(IGC_FCAL);
	regs_buff[19] = rd32(IGC_FCAH);
	regs_buff[20] = rd32(IGC_FCTTV);
	regs_buff[21] = rd32(IGC_FCRTL);
	regs_buff[22] = rd32(IGC_FCRTH);
	regs_buff[23] = rd32(IGC_FCRTV);

	/* Receive */
	regs_buff[24] = rd32(IGC_RCTL);
	regs_buff[25] = rd32(IGC_RXCSUM);
	regs_buff[26] = rd32(IGC_RLPML);
	regs_buff[27] = rd32(IGC_RFCTL);

	/* Transmit */
	regs_buff[28] = rd32(IGC_TCTL);
	regs_buff[29] = rd32(IGC_TIPG);

	/* Wake Up */

	/* MAC */

	/* Statistics */
	regs_buff[30] = adapter->stats.crcerrs;
	regs_buff[31] = adapter->stats.algnerrc;
	regs_buff[32] = adapter->stats.symerrs;
	regs_buff[33] = adapter->stats.rxerrc;
	regs_buff[34] = adapter->stats.mpc;
	regs_buff[35] = adapter->stats.scc;
	regs_buff[36] = adapter->stats.ecol;
	regs_buff[37] = adapter->stats.mcc;
	regs_buff[38] = adapter->stats.latecol;
	regs_buff[39] = adapter->stats.colc;
	regs_buff[40] = adapter->stats.dc;
	regs_buff[41] = adapter->stats.tncrs;
	regs_buff[42] = adapter->stats.sec;
	regs_buff[43] = adapter->stats.htdpmc;
	regs_buff[44] = adapter->stats.rlec;
	regs_buff[45] = adapter->stats.xonrxc;
	regs_buff[46] = adapter->stats.xontxc;
	regs_buff[47] = adapter->stats.xoffrxc;
	regs_buff[48] = adapter->stats.xofftxc;
	regs_buff[49] = adapter->stats.fcruc;
	regs_buff[50] = adapter->stats.prc64;
	regs_buff[51] = adapter->stats.prc127;
	regs_buff[52] = adapter->stats.prc255;
	regs_buff[53] = adapter->stats.prc511;
	regs_buff[54] = adapter->stats.prc1023;
	regs_buff[55] = adapter->stats.prc1522;
	regs_buff[56] = adapter->stats.gprc;
	regs_buff[57] = adapter->stats.bprc;
	regs_buff[58] = adapter->stats.mprc;
	regs_buff[59] = adapter->stats.gptc;
	regs_buff[60] = adapter->stats.gorc;
	regs_buff[61] = adapter->stats.gotc;
	regs_buff[62] = adapter->stats.rnbc;
	regs_buff[63] = adapter->stats.ruc;
	regs_buff[64] = adapter->stats.rfc;
	regs_buff[65] = adapter->stats.roc;
	regs_buff[66] = adapter->stats.rjc;
	regs_buff[67] = adapter->stats.mgprc;
	regs_buff[68] = adapter->stats.mgpdc;
	regs_buff[69] = adapter->stats.mgptc;
	regs_buff[70] = adapter->stats.tor;
	regs_buff[71] = adapter->stats.tot;
	regs_buff[72] = adapter->stats.tpr;
	regs_buff[73] = adapter->stats.tpt;
	regs_buff[74] = adapter->stats.ptc64;
	regs_buff[75] = adapter->stats.ptc127;
	regs_buff[76] = adapter->stats.ptc255;
	regs_buff[77] = adapter->stats.ptc511;
	regs_buff[78] = adapter->stats.ptc1023;
	regs_buff[79] = adapter->stats.ptc1522;
	regs_buff[80] = adapter->stats.mptc;
	regs_buff[81] = adapter->stats.bptc;
	regs_buff[82] = adapter->stats.tsctc;
	regs_buff[83] = adapter->stats.iac;
	regs_buff[84] = adapter->stats.rpthc;
	regs_buff[85] = adapter->stats.hgptc;
	regs_buff[86] = adapter->stats.hgorc;
	regs_buff[87] = adapter->stats.hgotc;
	regs_buff[88] = adapter->stats.lenerrs;
	regs_buff[89] = adapter->stats.scvpc;
	regs_buff[90] = adapter->stats.hrmpc;

	for (i = 0; i < 4; i++)
		regs_buff[91 + i] = rd32(IGC_SRRCTL(i));
	for (i = 0; i < 4; i++)
		regs_buff[95 + i] = rd32(IGC_PSRTYPE(i));
	for (i = 0; i < 4; i++)
		regs_buff[99 + i] = rd32(IGC_RDBAL(i));
	for (i = 0; i < 4; i++)
		regs_buff[103 + i] = rd32(IGC_RDBAH(i));
	for (i = 0; i < 4; i++)
		regs_buff[107 + i] = rd32(IGC_RDLEN(i));
	for (i = 0; i < 4; i++)
		regs_buff[111 + i] = rd32(IGC_RDH(i));
	for (i = 0; i < 4; i++)
		regs_buff[115 + i] = rd32(IGC_RDT(i));
	for (i = 0; i < 4; i++)
		regs_buff[119 + i] = rd32(IGC_RXDCTL(i));

	for (i = 0; i < 10; i++)
		regs_buff[123 + i] = rd32(IGC_EITR(i));
	for (i = 0; i < 16; i++)
		regs_buff[139 + i] = rd32(IGC_RAL(i));
	for (i = 0; i < 16; i++)
		regs_buff[145 + i] = rd32(IGC_RAH(i));

	for (i = 0; i < 4; i++)
		regs_buff[149 + i] = rd32(IGC_TDBAL(i));
	for (i = 0; i < 4; i++)
		regs_buff[152 + i] = rd32(IGC_TDBAH(i));
	for (i = 0; i < 4; i++)
		regs_buff[156 + i] = rd32(IGC_TDLEN(i));
	for (i = 0; i < 4; i++)
		regs_buff[160 + i] = rd32(IGC_TDH(i));
	for (i = 0; i < 4; i++)
		regs_buff[164 + i] = rd32(IGC_TDT(i));
	for (i = 0; i < 4; i++)
		regs_buff[168 + i] = rd32(IGC_TXDCTL(i));

	/* XXX: Due to a bug few lines above, RAL and RAH registers are
	 * overwritten. To preserve the ABI, we write these registers again in
	 * regs_buff.
	 */
	for (i = 0; i < 16; i++)
		regs_buff[172 + i] = rd32(IGC_RAL(i));
	for (i = 0; i < 16; i++)
		regs_buff[188 + i] = rd32(IGC_RAH(i));

	regs_buff[204] = rd32(IGC_VLANPQF);

	for (i = 0; i < 8; i++)
		regs_buff[205 + i] = rd32(IGC_ETQF(i));

	regs_buff[213] = adapter->stats.tlpic;
	regs_buff[214] = adapter->stats.rlpic;
}

static void igc_ethtool_get_wol(struct net_device *netdev,
				struct ethtool_wolinfo *wol)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	wol->wolopts = 0;

	if (!(adapter->flags & IGC_FLAG_WOL_SUPPORTED))
		return;

	wol->supported = WAKE_UCAST | WAKE_MCAST |
			 WAKE_BCAST | WAKE_MAGIC |
			 WAKE_PHY;

	/* apply any specific unsupported masks here */
	switch (adapter->hw.device_id) {
	default:
		break;
	}

	if (adapter->wol & IGC_WUFC_EX)
		wol->wolopts |= WAKE_UCAST;
	if (adapter->wol & IGC_WUFC_MC)
		wol->wolopts |= WAKE_MCAST;
	if (adapter->wol & IGC_WUFC_BC)
		wol->wolopts |= WAKE_BCAST;
	if (adapter->wol & IGC_WUFC_MAG)
		wol->wolopts |= WAKE_MAGIC;
	if (adapter->wol & IGC_WUFC_LNKC)
		wol->wolopts |= WAKE_PHY;
}

static int igc_ethtool_set_wol(struct net_device *netdev,
			       struct ethtool_wolinfo *wol)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_ARP | WAKE_MAGICSECURE | WAKE_FILTER))
		return -EOPNOTSUPP;

	if (!(adapter->flags & IGC_FLAG_WOL_SUPPORTED))
		return wol->wolopts ? -EOPNOTSUPP : 0;

	/* these settings will always override what we currently have */
	adapter->wol = 0;

	if (wol->wolopts & WAKE_UCAST)
		adapter->wol |= IGC_WUFC_EX;
	if (wol->wolopts & WAKE_MCAST)
		adapter->wol |= IGC_WUFC_MC;
	if (wol->wolopts & WAKE_BCAST)
		adapter->wol |= IGC_WUFC_BC;
	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol |= IGC_WUFC_MAG;
	if (wol->wolopts & WAKE_PHY)
		adapter->wol |= IGC_WUFC_LNKC;
	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	return 0;
}

static u32 igc_ethtool_get_msglevel(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void igc_ethtool_set_msglevel(struct net_device *netdev, u32 data)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = data;
}

static int igc_ethtool_nway_reset(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev))
		igc_reinit_locked(adapter);
	return 0;
}

static u32 igc_ethtool_get_link(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_mac_info *mac = &adapter->hw.mac;

	/* If the link is not reported up to netdev, interrupts are disabled,
	 * and so the physical link state may have changed since we last
	 * looked. Set get_link_status to make sure that the true link
	 * state is interrogated, rather than pulling a cached and possibly
	 * stale link state from the driver.
	 */
	if (!netif_carrier_ok(netdev))
		mac->get_link_status = 1;

	return igc_has_link(adapter);
}

static int igc_ethtool_get_eeprom_len(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	return adapter->hw.nvm.word_size * 2;
}

static int igc_ethtool_get_eeprom(struct net_device *netdev,
				  struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	int first_word, last_word;
	u16 *eeprom_buff;
	int ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;

	eeprom_buff = kmalloc_array(last_word - first_word + 1, sizeof(u16),
				    GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	if (hw->nvm.type == igc_nvm_eeprom_spi) {
		ret_val = hw->nvm.ops.read(hw, first_word,
					   last_word - first_word + 1,
					   eeprom_buff);
	} else {
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

static int igc_ethtool_set_eeprom(struct net_device *netdev,
				  struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	int max_len, first_word, last_word, ret_val = 0;
	u16 *eeprom_buff;
	void *ptr;
	u16 i;

	if (eeprom->len == 0)
		return -EOPNOTSUPP;

	if (hw->mac.type >= igc_i225 &&
	    !igc_get_flash_presence_i225(hw)) {
		return -EOPNOTSUPP;
	}

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
		/* need read/modify/write of first changed EEPROM word
		 * only the second byte of the word is being modified
		 */
		ret_val = hw->nvm.ops.read(hw, first_word, 1,
					    &eeprom_buff[0]);
		ptr++;
	}
	if (((eeprom->offset + eeprom->len) & 1) && ret_val == 0) {
		/* need read/modify/write of last changed EEPROM word
		 * only the first byte of the word is being modified
		 */
		ret_val = hw->nvm.ops.read(hw, last_word, 1,
				   &eeprom_buff[last_word - first_word]);
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i < last_word - first_word + 1; i++)
		cpu_to_le16s(&eeprom_buff[i]);

	ret_val = hw->nvm.ops.write(hw, first_word,
				    last_word - first_word + 1, eeprom_buff);

	/* Update the checksum if nvm write succeeded */
	if (ret_val == 0)
		hw->nvm.ops.update(hw);

	kfree(eeprom_buff);
	return ret_val;
}

static void igc_ethtool_get_ringparam(struct net_device *netdev,
				      struct ethtool_ringparam *ring)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = IGC_MAX_RXD;
	ring->tx_max_pending = IGC_MAX_TXD;
	ring->rx_pending = adapter->rx_ring_count;
	ring->tx_pending = adapter->tx_ring_count;
}

static int igc_ethtool_set_ringparam(struct net_device *netdev,
				     struct ethtool_ringparam *ring)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_ring *temp_ring;
	u16 new_rx_count, new_tx_count;
	int i, err = 0;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	new_rx_count = min_t(u32, ring->rx_pending, IGC_MAX_RXD);
	new_rx_count = max_t(u16, new_rx_count, IGC_MIN_RXD);
	new_rx_count = ALIGN(new_rx_count, REQ_RX_DESCRIPTOR_MULTIPLE);

	new_tx_count = min_t(u32, ring->tx_pending, IGC_MAX_TXD);
	new_tx_count = max_t(u16, new_tx_count, IGC_MIN_TXD);
	new_tx_count = ALIGN(new_tx_count, REQ_TX_DESCRIPTOR_MULTIPLE);

	if (new_tx_count == adapter->tx_ring_count &&
	    new_rx_count == adapter->rx_ring_count) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__IGC_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

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
		temp_ring = vmalloc(array_size(sizeof(struct igc_ring),
					       adapter->num_tx_queues));
	else
		temp_ring = vmalloc(array_size(sizeof(struct igc_ring),
					       adapter->num_rx_queues));

	if (!temp_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	igc_down(adapter);

	/* We can't just free everything and then setup again,
	 * because the ISRs in MSI-X mode get passed pointers
	 * to the Tx and Rx ring structs.
	 */
	if (new_tx_count != adapter->tx_ring_count) {
		for (i = 0; i < adapter->num_tx_queues; i++) {
			memcpy(&temp_ring[i], adapter->tx_ring[i],
			       sizeof(struct igc_ring));

			temp_ring[i].count = new_tx_count;
			err = igc_setup_tx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					igc_free_tx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}
		}

		for (i = 0; i < adapter->num_tx_queues; i++) {
			igc_free_tx_resources(adapter->tx_ring[i]);

			memcpy(adapter->tx_ring[i], &temp_ring[i],
			       sizeof(struct igc_ring));
		}

		adapter->tx_ring_count = new_tx_count;
	}

	if (new_rx_count != adapter->rx_ring_count) {
		for (i = 0; i < adapter->num_rx_queues; i++) {
			memcpy(&temp_ring[i], adapter->rx_ring[i],
			       sizeof(struct igc_ring));

			temp_ring[i].count = new_rx_count;
			err = igc_setup_rx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					igc_free_rx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}
		}

		for (i = 0; i < adapter->num_rx_queues; i++) {
			igc_free_rx_resources(adapter->rx_ring[i]);

			memcpy(adapter->rx_ring[i], &temp_ring[i],
			       sizeof(struct igc_ring));
		}

		adapter->rx_ring_count = new_rx_count;
	}
err_setup:
	igc_up(adapter);
	vfree(temp_ring);
clear_reset:
	clear_bit(__IGC_RESETTING, &adapter->state);
	return err;
}

static void igc_ethtool_get_pauseparam(struct net_device *netdev,
				       struct ethtool_pauseparam *pause)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;

	pause->autoneg =
		(adapter->fc_autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (hw->fc.current_mode == igc_fc_rx_pause) {
		pause->rx_pause = 1;
	} else if (hw->fc.current_mode == igc_fc_tx_pause) {
		pause->tx_pause = 1;
	} else if (hw->fc.current_mode == igc_fc_full) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

static int igc_ethtool_set_pauseparam(struct net_device *netdev,
				      struct ethtool_pauseparam *pause)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	int retval = 0;

	adapter->fc_autoneg = pause->autoneg;

	while (test_and_set_bit(__IGC_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (adapter->fc_autoneg == AUTONEG_ENABLE) {
		hw->fc.requested_mode = igc_fc_default;
		if (netif_running(adapter->netdev)) {
			igc_down(adapter);
			igc_up(adapter);
		} else {
			igc_reset(adapter);
		}
	} else {
		if (pause->rx_pause && pause->tx_pause)
			hw->fc.requested_mode = igc_fc_full;
		else if (pause->rx_pause && !pause->tx_pause)
			hw->fc.requested_mode = igc_fc_rx_pause;
		else if (!pause->rx_pause && pause->tx_pause)
			hw->fc.requested_mode = igc_fc_tx_pause;
		else if (!pause->rx_pause && !pause->tx_pause)
			hw->fc.requested_mode = igc_fc_none;

		hw->fc.current_mode = hw->fc.requested_mode;

		retval = ((hw->phy.media_type == igc_media_type_copper) ?
			  igc_force_mac_fc(hw) : igc_setup_link(hw));
	}

	clear_bit(__IGC_RESETTING, &adapter->state);
	return retval;
}

static void igc_ethtool_get_strings(struct net_device *netdev, u32 stringset,
				    u8 *data)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *igc_gstrings_test,
		       IGC_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (i = 0; i < IGC_GLOBAL_STATS_LEN; i++)
			ethtool_sprintf(&p, igc_gstrings_stats[i].stat_string);
		for (i = 0; i < IGC_NETDEV_STATS_LEN; i++)
			ethtool_sprintf(&p,
					igc_gstrings_net_stats[i].stat_string);
		for (i = 0; i < adapter->num_tx_queues; i++) {
			ethtool_sprintf(&p, "tx_queue_%u_packets", i);
			ethtool_sprintf(&p, "tx_queue_%u_bytes", i);
			ethtool_sprintf(&p, "tx_queue_%u_restart", i);
		}
		for (i = 0; i < adapter->num_rx_queues; i++) {
			ethtool_sprintf(&p, "rx_queue_%u_packets", i);
			ethtool_sprintf(&p, "rx_queue_%u_bytes", i);
			ethtool_sprintf(&p, "rx_queue_%u_drops", i);
			ethtool_sprintf(&p, "rx_queue_%u_csum_err", i);
			ethtool_sprintf(&p, "rx_queue_%u_alloc_failed", i);
		}
		/* BUG_ON(p - data != IGC_STATS_LEN * ETH_GSTRING_LEN); */
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(data, igc_priv_flags_strings,
		       IGC_PRIV_FLAGS_STR_LEN * ETH_GSTRING_LEN);
		break;
	}
}

static int igc_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return IGC_STATS_LEN;
	case ETH_SS_TEST:
		return IGC_TEST_LEN;
	case ETH_SS_PRIV_FLAGS:
		return IGC_PRIV_FLAGS_STR_LEN;
	default:
		return -ENOTSUPP;
	}
}

static void igc_ethtool_get_stats(struct net_device *netdev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct rtnl_link_stats64 *net_stats = &adapter->stats64;
	unsigned int start;
	struct igc_ring *ring;
	int i, j;
	char *p;

	spin_lock(&adapter->stats64_lock);
	igc_update_stats(adapter);

	for (i = 0; i < IGC_GLOBAL_STATS_LEN; i++) {
		p = (char *)adapter + igc_gstrings_stats[i].stat_offset;
		data[i] = (igc_gstrings_stats[i].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	for (j = 0; j < IGC_NETDEV_STATS_LEN; j++, i++) {
		p = (char *)net_stats + igc_gstrings_net_stats[j].stat_offset;
		data[i] = (igc_gstrings_net_stats[j].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	for (j = 0; j < adapter->num_tx_queues; j++) {
		u64	restart2;

		ring = adapter->tx_ring[j];
		do {
			start = u64_stats_fetch_begin_irq(&ring->tx_syncp);
			data[i]   = ring->tx_stats.packets;
			data[i + 1] = ring->tx_stats.bytes;
			data[i + 2] = ring->tx_stats.restart_queue;
		} while (u64_stats_fetch_retry_irq(&ring->tx_syncp, start));
		do {
			start = u64_stats_fetch_begin_irq(&ring->tx_syncp2);
			restart2  = ring->tx_stats.restart_queue2;
		} while (u64_stats_fetch_retry_irq(&ring->tx_syncp2, start));
		data[i + 2] += restart2;

		i += IGC_TX_QUEUE_STATS_LEN;
	}
	for (j = 0; j < adapter->num_rx_queues; j++) {
		ring = adapter->rx_ring[j];
		do {
			start = u64_stats_fetch_begin_irq(&ring->rx_syncp);
			data[i]   = ring->rx_stats.packets;
			data[i + 1] = ring->rx_stats.bytes;
			data[i + 2] = ring->rx_stats.drops;
			data[i + 3] = ring->rx_stats.csum_err;
			data[i + 4] = ring->rx_stats.alloc_failed;
		} while (u64_stats_fetch_retry_irq(&ring->rx_syncp, start));
		i += IGC_RX_QUEUE_STATS_LEN;
	}
	spin_unlock(&adapter->stats64_lock);
}

static int igc_ethtool_get_coalesce(struct net_device *netdev,
				    struct ethtool_coalesce *ec)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	if (adapter->rx_itr_setting <= 3)
		ec->rx_coalesce_usecs = adapter->rx_itr_setting;
	else
		ec->rx_coalesce_usecs = adapter->rx_itr_setting >> 2;

	if (!(adapter->flags & IGC_FLAG_QUEUE_PAIRS)) {
		if (adapter->tx_itr_setting <= 3)
			ec->tx_coalesce_usecs = adapter->tx_itr_setting;
		else
			ec->tx_coalesce_usecs = adapter->tx_itr_setting >> 2;
	}

	return 0;
}

static int igc_ethtool_set_coalesce(struct net_device *netdev,
				    struct ethtool_coalesce *ec)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	int i;

	if (ec->rx_coalesce_usecs > IGC_MAX_ITR_USECS ||
	    (ec->rx_coalesce_usecs > 3 &&
	     ec->rx_coalesce_usecs < IGC_MIN_ITR_USECS) ||
	    ec->rx_coalesce_usecs == 2)
		return -EINVAL;

	if (ec->tx_coalesce_usecs > IGC_MAX_ITR_USECS ||
	    (ec->tx_coalesce_usecs > 3 &&
	     ec->tx_coalesce_usecs < IGC_MIN_ITR_USECS) ||
	    ec->tx_coalesce_usecs == 2)
		return -EINVAL;

	if ((adapter->flags & IGC_FLAG_QUEUE_PAIRS) && ec->tx_coalesce_usecs)
		return -EINVAL;

	/* If ITR is disabled, disable DMAC */
	if (ec->rx_coalesce_usecs == 0) {
		if (adapter->flags & IGC_FLAG_DMAC)
			adapter->flags &= ~IGC_FLAG_DMAC;
	}

	/* convert to rate of irq's per second */
	if (ec->rx_coalesce_usecs && ec->rx_coalesce_usecs <= 3)
		adapter->rx_itr_setting = ec->rx_coalesce_usecs;
	else
		adapter->rx_itr_setting = ec->rx_coalesce_usecs << 2;

	/* convert to rate of irq's per second */
	if (adapter->flags & IGC_FLAG_QUEUE_PAIRS)
		adapter->tx_itr_setting = adapter->rx_itr_setting;
	else if (ec->tx_coalesce_usecs && ec->tx_coalesce_usecs <= 3)
		adapter->tx_itr_setting = ec->tx_coalesce_usecs;
	else
		adapter->tx_itr_setting = ec->tx_coalesce_usecs << 2;

	for (i = 0; i < adapter->num_q_vectors; i++) {
		struct igc_q_vector *q_vector = adapter->q_vector[i];

		q_vector->tx.work_limit = adapter->tx_work_limit;
		if (q_vector->rx.ring)
			q_vector->itr_val = adapter->rx_itr_setting;
		else
			q_vector->itr_val = adapter->tx_itr_setting;
		if (q_vector->itr_val && q_vector->itr_val <= 3)
			q_vector->itr_val = IGC_START_ITR;
		q_vector->set_itr = 1;
	}

	return 0;
}

#define ETHER_TYPE_FULL_MASK ((__force __be16)~0)
static int igc_ethtool_get_nfc_rule(struct igc_adapter *adapter,
				    struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = &cmd->fs;
	struct igc_nfc_rule *rule = NULL;

	cmd->data = IGC_MAX_RXNFC_RULES;

	mutex_lock(&adapter->nfc_rule_lock);

	rule = igc_get_nfc_rule(adapter, fsp->location);
	if (!rule)
		goto out;

	fsp->flow_type = ETHER_FLOW;
	fsp->ring_cookie = rule->action;

	if (rule->filter.match_flags & IGC_FILTER_FLAG_ETHER_TYPE) {
		fsp->h_u.ether_spec.h_proto = htons(rule->filter.etype);
		fsp->m_u.ether_spec.h_proto = ETHER_TYPE_FULL_MASK;
	}

	if (rule->filter.match_flags & IGC_FILTER_FLAG_VLAN_TCI) {
		fsp->flow_type |= FLOW_EXT;
		fsp->h_ext.vlan_tci = htons(rule->filter.vlan_tci);
		fsp->m_ext.vlan_tci = htons(VLAN_PRIO_MASK);
	}

	if (rule->filter.match_flags & IGC_FILTER_FLAG_DST_MAC_ADDR) {
		ether_addr_copy(fsp->h_u.ether_spec.h_dest,
				rule->filter.dst_addr);
		eth_broadcast_addr(fsp->m_u.ether_spec.h_dest);
	}

	if (rule->filter.match_flags & IGC_FILTER_FLAG_SRC_MAC_ADDR) {
		ether_addr_copy(fsp->h_u.ether_spec.h_source,
				rule->filter.src_addr);
		eth_broadcast_addr(fsp->m_u.ether_spec.h_source);
	}

	if (rule->filter.match_flags & IGC_FILTER_FLAG_USER_DATA) {
		fsp->flow_type |= FLOW_EXT;
		memcpy(fsp->h_ext.data, rule->filter.user_data, sizeof(fsp->h_ext.data));
		memcpy(fsp->m_ext.data, rule->filter.user_mask, sizeof(fsp->m_ext.data));
	}

	mutex_unlock(&adapter->nfc_rule_lock);
	return 0;

out:
	mutex_unlock(&adapter->nfc_rule_lock);
	return -EINVAL;
}

static int igc_ethtool_get_nfc_rules(struct igc_adapter *adapter,
				     struct ethtool_rxnfc *cmd,
				     u32 *rule_locs)
{
	struct igc_nfc_rule *rule;
	int cnt = 0;

	cmd->data = IGC_MAX_RXNFC_RULES;

	mutex_lock(&adapter->nfc_rule_lock);

	list_for_each_entry(rule, &adapter->nfc_rule_list, list) {
		if (cnt == cmd->rule_cnt) {
			mutex_unlock(&adapter->nfc_rule_lock);
			return -EMSGSIZE;
		}
		rule_locs[cnt] = rule->location;
		cnt++;
	}

	mutex_unlock(&adapter->nfc_rule_lock);

	cmd->rule_cnt = cnt;

	return 0;
}

static int igc_ethtool_get_rss_hash_opts(struct igc_adapter *adapter,
					 struct ethtool_rxnfc *cmd)
{
	cmd->data = 0;

	/* Report default options for RSS on igc */
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		fallthrough;
	case UDP_V4_FLOW:
		if (adapter->flags & IGC_FLAG_RSS_FIELD_IPV4_UDP)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		fallthrough;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case IPV4_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	case TCP_V6_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		fallthrough;
	case UDP_V6_FLOW:
		if (adapter->flags & IGC_FLAG_RSS_FIELD_IPV6_UDP)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		fallthrough;
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV6_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int igc_ethtool_get_rxnfc(struct net_device *dev,
				 struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct igc_adapter *adapter = netdev_priv(dev);

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = adapter->num_rx_queues;
		return 0;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = adapter->nfc_rule_count;
		return 0;
	case ETHTOOL_GRXCLSRULE:
		return igc_ethtool_get_nfc_rule(adapter, cmd);
	case ETHTOOL_GRXCLSRLALL:
		return igc_ethtool_get_nfc_rules(adapter, cmd, rule_locs);
	case ETHTOOL_GRXFH:
		return igc_ethtool_get_rss_hash_opts(adapter, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

#define UDP_RSS_FLAGS (IGC_FLAG_RSS_FIELD_IPV4_UDP | \
		       IGC_FLAG_RSS_FIELD_IPV6_UDP)
static int igc_ethtool_set_rss_hash_opt(struct igc_adapter *adapter,
					struct ethtool_rxnfc *nfc)
{
	u32 flags = adapter->flags;

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST) ||
		    !(nfc->data & RXH_L4_B_0_1) ||
		    !(nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		break;
	case UDP_V4_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST))
			return -EINVAL;
		switch (nfc->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			flags &= ~IGC_FLAG_RSS_FIELD_IPV4_UDP;
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			flags |= IGC_FLAG_RSS_FIELD_IPV4_UDP;
			break;
		default:
			return -EINVAL;
		}
		break;
	case UDP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST))
			return -EINVAL;
		switch (nfc->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			flags &= ~IGC_FLAG_RSS_FIELD_IPV6_UDP;
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			flags |= IGC_FLAG_RSS_FIELD_IPV6_UDP;
			break;
		default:
			return -EINVAL;
		}
		break;
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST) ||
		    (nfc->data & RXH_L4_B_0_1) ||
		    (nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	/* if we changed something we need to update flags */
	if (flags != adapter->flags) {
		struct igc_hw *hw = &adapter->hw;
		u32 mrqc = rd32(IGC_MRQC);

		if ((flags & UDP_RSS_FLAGS) &&
		    !(adapter->flags & UDP_RSS_FLAGS))
			netdev_err(adapter->netdev,
				   "Enabling UDP RSS: fragmented packets may arrive out of order to the stack above\n");

		adapter->flags = flags;

		/* Perform hash on these packet types */
		mrqc |= IGC_MRQC_RSS_FIELD_IPV4 |
			IGC_MRQC_RSS_FIELD_IPV4_TCP |
			IGC_MRQC_RSS_FIELD_IPV6 |
			IGC_MRQC_RSS_FIELD_IPV6_TCP;

		mrqc &= ~(IGC_MRQC_RSS_FIELD_IPV4_UDP |
			  IGC_MRQC_RSS_FIELD_IPV6_UDP);

		if (flags & IGC_FLAG_RSS_FIELD_IPV4_UDP)
			mrqc |= IGC_MRQC_RSS_FIELD_IPV4_UDP;

		if (flags & IGC_FLAG_RSS_FIELD_IPV6_UDP)
			mrqc |= IGC_MRQC_RSS_FIELD_IPV6_UDP;

		wr32(IGC_MRQC, mrqc);
	}

	return 0;
}

static void igc_ethtool_init_nfc_rule(struct igc_nfc_rule *rule,
				      const struct ethtool_rx_flow_spec *fsp)
{
	INIT_LIST_HEAD(&rule->list);

	rule->action = fsp->ring_cookie;
	rule->location = fsp->location;

	if ((fsp->flow_type & FLOW_EXT) && fsp->m_ext.vlan_tci) {
		rule->filter.vlan_tci = ntohs(fsp->h_ext.vlan_tci);
		rule->filter.match_flags |= IGC_FILTER_FLAG_VLAN_TCI;
	}

	if (fsp->m_u.ether_spec.h_proto == ETHER_TYPE_FULL_MASK) {
		rule->filter.etype = ntohs(fsp->h_u.ether_spec.h_proto);
		rule->filter.match_flags = IGC_FILTER_FLAG_ETHER_TYPE;
	}

	/* Both source and destination address filters only support the full
	 * mask.
	 */
	if (is_broadcast_ether_addr(fsp->m_u.ether_spec.h_source)) {
		rule->filter.match_flags |= IGC_FILTER_FLAG_SRC_MAC_ADDR;
		ether_addr_copy(rule->filter.src_addr,
				fsp->h_u.ether_spec.h_source);
	}

	if (is_broadcast_ether_addr(fsp->m_u.ether_spec.h_dest)) {
		rule->filter.match_flags |= IGC_FILTER_FLAG_DST_MAC_ADDR;
		ether_addr_copy(rule->filter.dst_addr,
				fsp->h_u.ether_spec.h_dest);
	}

	/* VLAN etype matching */
	if ((fsp->flow_type & FLOW_EXT) && fsp->h_ext.vlan_etype) {
		rule->filter.vlan_etype = fsp->h_ext.vlan_etype;
		rule->filter.match_flags |= IGC_FILTER_FLAG_VLAN_ETYPE;
	}

	/* Check for user defined data */
	if ((fsp->flow_type & FLOW_EXT) &&
	    (fsp->h_ext.data[0] || fsp->h_ext.data[1])) {
		rule->filter.match_flags |= IGC_FILTER_FLAG_USER_DATA;
		memcpy(rule->filter.user_data, fsp->h_ext.data, sizeof(fsp->h_ext.data));
		memcpy(rule->filter.user_mask, fsp->m_ext.data, sizeof(fsp->m_ext.data));
	}

	/* When multiple filter options or user data or vlan etype is set, use a
	 * flex filter.
	 */
	if ((rule->filter.match_flags & IGC_FILTER_FLAG_USER_DATA) ||
	    (rule->filter.match_flags & IGC_FILTER_FLAG_VLAN_ETYPE) ||
	    (rule->filter.match_flags & (rule->filter.match_flags - 1)))
		rule->flex = true;
	else
		rule->flex = false;
}

/**
 * igc_ethtool_check_nfc_rule() - Check if NFC rule is valid
 * @adapter: Pointer to adapter
 * @rule: Rule under evaluation
 *
 * The driver doesn't support rules with multiple matches so if more than
 * one bit in filter flags is set, @rule is considered invalid.
 *
 * Also, if there is already another rule with the same filter in a different
 * location, @rule is considered invalid.
 *
 * Context: Expects adapter->nfc_rule_lock to be held by caller.
 *
 * Return: 0 in case of success, negative errno code otherwise.
 */
static int igc_ethtool_check_nfc_rule(struct igc_adapter *adapter,
				      struct igc_nfc_rule *rule)
{
	struct net_device *dev = adapter->netdev;
	u8 flags = rule->filter.match_flags;
	struct igc_nfc_rule *tmp;

	if (!flags) {
		netdev_dbg(dev, "Rule with no match\n");
		return -EINVAL;
	}

	list_for_each_entry(tmp, &adapter->nfc_rule_list, list) {
		if (!memcmp(&rule->filter, &tmp->filter,
			    sizeof(rule->filter)) &&
		    tmp->location != rule->location) {
			netdev_dbg(dev, "Rule already exists\n");
			return -EEXIST;
		}
	}

	return 0;
}

static int igc_ethtool_add_nfc_rule(struct igc_adapter *adapter,
				    struct ethtool_rxnfc *cmd)
{
	struct net_device *netdev = adapter->netdev;
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct igc_nfc_rule *rule, *old_rule;
	int err;

	if (!(netdev->hw_features & NETIF_F_NTUPLE)) {
		netdev_dbg(netdev, "N-tuple filters disabled\n");
		return -EOPNOTSUPP;
	}

	if ((fsp->flow_type & ~FLOW_EXT) != ETHER_FLOW) {
		netdev_dbg(netdev, "Only ethernet flow type is supported\n");
		return -EOPNOTSUPP;
	}

	if (fsp->ring_cookie >= adapter->num_rx_queues) {
		netdev_dbg(netdev, "Invalid action\n");
		return -EINVAL;
	}

	if (fsp->location >= IGC_MAX_RXNFC_RULES) {
		netdev_dbg(netdev, "Invalid location\n");
		return -EINVAL;
	}

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return -ENOMEM;

	igc_ethtool_init_nfc_rule(rule, fsp);

	mutex_lock(&adapter->nfc_rule_lock);

	err = igc_ethtool_check_nfc_rule(adapter, rule);
	if (err)
		goto err;

	old_rule = igc_get_nfc_rule(adapter, fsp->location);
	if (old_rule)
		igc_del_nfc_rule(adapter, old_rule);

	err = igc_add_nfc_rule(adapter, rule);
	if (err)
		goto err;

	mutex_unlock(&adapter->nfc_rule_lock);
	return 0;

err:
	mutex_unlock(&adapter->nfc_rule_lock);
	kfree(rule);
	return err;
}

static int igc_ethtool_del_nfc_rule(struct igc_adapter *adapter,
				    struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct igc_nfc_rule *rule;

	mutex_lock(&adapter->nfc_rule_lock);

	rule = igc_get_nfc_rule(adapter, fsp->location);
	if (!rule) {
		mutex_unlock(&adapter->nfc_rule_lock);
		return -EINVAL;
	}

	igc_del_nfc_rule(adapter, rule);

	mutex_unlock(&adapter->nfc_rule_lock);
	return 0;
}

static int igc_ethtool_set_rxnfc(struct net_device *dev,
				 struct ethtool_rxnfc *cmd)
{
	struct igc_adapter *adapter = netdev_priv(dev);

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		return igc_ethtool_set_rss_hash_opt(adapter, cmd);
	case ETHTOOL_SRXCLSRLINS:
		return igc_ethtool_add_nfc_rule(adapter, cmd);
	case ETHTOOL_SRXCLSRLDEL:
		return igc_ethtool_del_nfc_rule(adapter, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

void igc_write_rss_indir_tbl(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 reg = IGC_RETA(0);
	u32 shift = 0;
	int i = 0;

	while (i < IGC_RETA_SIZE) {
		u32 val = 0;
		int j;

		for (j = 3; j >= 0; j--) {
			val <<= 8;
			val |= adapter->rss_indir_tbl[i + j];
		}

		wr32(reg, val << shift);
		reg += 4;
		i += 4;
	}
}

static u32 igc_ethtool_get_rxfh_indir_size(struct net_device *netdev)
{
	return IGC_RETA_SIZE;
}

static int igc_ethtool_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
				u8 *hfunc)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	int i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;
	if (!indir)
		return 0;
	for (i = 0; i < IGC_RETA_SIZE; i++)
		indir[i] = adapter->rss_indir_tbl[i];

	return 0;
}

static int igc_ethtool_set_rxfh(struct net_device *netdev, const u32 *indir,
				const u8 *key, const u8 hfunc)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	u32 num_queues;
	int i;

	/* We do not allow change in unsupported parameters */
	if (key ||
	    (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP))
		return -EOPNOTSUPP;
	if (!indir)
		return 0;

	num_queues = adapter->rss_queues;

	/* Verify user input. */
	for (i = 0; i < IGC_RETA_SIZE; i++)
		if (indir[i] >= num_queues)
			return -EINVAL;

	for (i = 0; i < IGC_RETA_SIZE; i++)
		adapter->rss_indir_tbl[i] = indir[i];

	igc_write_rss_indir_tbl(adapter);

	return 0;
}

static void igc_ethtool_get_channels(struct net_device *netdev,
				     struct ethtool_channels *ch)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	/* Report maximum channels */
	ch->max_combined = igc_get_max_rss_queues(adapter);

	/* Report info for other vector */
	if (adapter->flags & IGC_FLAG_HAS_MSIX) {
		ch->max_other = NON_Q_VECTORS;
		ch->other_count = NON_Q_VECTORS;
	}

	ch->combined_count = adapter->rss_queues;
}

static int igc_ethtool_set_channels(struct net_device *netdev,
				    struct ethtool_channels *ch)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	unsigned int count = ch->combined_count;
	unsigned int max_combined = 0;

	/* Verify they are not requesting separate vectors */
	if (!count || ch->rx_count || ch->tx_count)
		return -EINVAL;

	/* Verify other_count is valid and has not been changed */
	if (ch->other_count != NON_Q_VECTORS)
		return -EINVAL;

	/* Verify the number of channels doesn't exceed hw limits */
	max_combined = igc_get_max_rss_queues(adapter);
	if (count > max_combined)
		return -EINVAL;

	if (count != adapter->rss_queues) {
		adapter->rss_queues = count;
		igc_set_flag_queue_pairs(adapter, max_combined);

		/* Hardware has to reinitialize queues and interrupts to
		 * match the new configuration.
		 */
		return igc_reinit_queues(adapter);
	}

	return 0;
}

static int igc_ethtool_get_ts_info(struct net_device *dev,
				   struct ethtool_ts_info *info)
{
	struct igc_adapter *adapter = netdev_priv(dev);

	if (adapter->ptp_clock)
		info->phc_index = ptp_clock_index(adapter->ptp_clock);
	else
		info->phc_index = -1;

	switch (adapter->hw.mac.type) {
	case igc_i225:
		info->so_timestamping =
			SOF_TIMESTAMPING_TX_SOFTWARE |
			SOF_TIMESTAMPING_RX_SOFTWARE |
			SOF_TIMESTAMPING_SOFTWARE |
			SOF_TIMESTAMPING_TX_HARDWARE |
			SOF_TIMESTAMPING_RX_HARDWARE |
			SOF_TIMESTAMPING_RAW_HARDWARE;

		info->tx_types =
			BIT(HWTSTAMP_TX_OFF) |
			BIT(HWTSTAMP_TX_ON);

		info->rx_filters = BIT(HWTSTAMP_FILTER_NONE);
		info->rx_filters |= BIT(HWTSTAMP_FILTER_ALL);

		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static u32 igc_ethtool_get_priv_flags(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	u32 priv_flags = 0;

	if (adapter->flags & IGC_FLAG_RX_LEGACY)
		priv_flags |= IGC_PRIV_FLAGS_LEGACY_RX;

	return priv_flags;
}

static int igc_ethtool_set_priv_flags(struct net_device *netdev, u32 priv_flags)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	unsigned int flags = adapter->flags;

	flags &= ~IGC_FLAG_RX_LEGACY;
	if (priv_flags & IGC_PRIV_FLAGS_LEGACY_RX)
		flags |= IGC_FLAG_RX_LEGACY;

	if (flags != adapter->flags) {
		adapter->flags = flags;

		/* reset interface to repopulate queues */
		if (netif_running(netdev))
			igc_reinit_locked(adapter);
	}

	return 0;
}

static int igc_ethtool_get_eee(struct net_device *netdev,
			       struct ethtool_eee *edata)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u32 eeer;

	if (hw->dev_spec._base.eee_enable)
		edata->advertised =
			mmd_eee_adv_to_ethtool_adv_t(adapter->eee_advert);

	*edata = adapter->eee;
	edata->supported = SUPPORTED_Autoneg;

	eeer = rd32(IGC_EEER);

	/* EEE status on negotiated link */
	if (eeer & IGC_EEER_EEE_NEG)
		edata->eee_active = true;

	if (eeer & IGC_EEER_TX_LPI_EN)
		edata->tx_lpi_enabled = true;

	edata->eee_enabled = hw->dev_spec._base.eee_enable;

	edata->advertised = SUPPORTED_Autoneg;
	edata->lp_advertised = SUPPORTED_Autoneg;

	/* Report correct negotiated EEE status for devices that
	 * wrongly report EEE at half-duplex
	 */
	if (adapter->link_duplex == HALF_DUPLEX) {
		edata->eee_enabled = false;
		edata->eee_active = false;
		edata->tx_lpi_enabled = false;
		edata->advertised &= ~edata->advertised;
	}

	return 0;
}

static int igc_ethtool_set_eee(struct net_device *netdev,
			       struct ethtool_eee *edata)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	struct ethtool_eee eee_curr;
	s32 ret_val;

	memset(&eee_curr, 0, sizeof(struct ethtool_eee));

	ret_val = igc_ethtool_get_eee(netdev, &eee_curr);
	if (ret_val) {
		netdev_err(netdev,
			   "Problem setting EEE advertisement options\n");
		return -EINVAL;
	}

	if (eee_curr.eee_enabled) {
		if (eee_curr.tx_lpi_enabled != edata->tx_lpi_enabled) {
			netdev_err(netdev,
				   "Setting EEE tx-lpi is not supported\n");
			return -EINVAL;
		}

		/* Tx LPI timer is not implemented currently */
		if (edata->tx_lpi_timer) {
			netdev_err(netdev,
				   "Setting EEE Tx LPI timer is not supported\n");
			return -EINVAL;
		}
	} else if (!edata->eee_enabled) {
		netdev_err(netdev,
			   "Setting EEE options are not supported with EEE disabled\n");
		return -EINVAL;
	}

	adapter->eee_advert = ethtool_adv_to_mmd_eee_adv_t(edata->advertised);
	if (hw->dev_spec._base.eee_enable != edata->eee_enabled) {
		hw->dev_spec._base.eee_enable = edata->eee_enabled;
		adapter->flags |= IGC_FLAG_EEE;

		/* reset link */
		if (netif_running(netdev))
			igc_reinit_locked(adapter);
		else
			igc_reset(adapter);
	}

	return 0;
}

static int igc_ethtool_begin(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	pm_runtime_get_sync(&adapter->pdev->dev);
	return 0;
}

static void igc_ethtool_complete(struct net_device *netdev)
{
	struct igc_adapter *adapter = netdev_priv(netdev);

	pm_runtime_put(&adapter->pdev->dev);
}

static int igc_ethtool_get_link_ksettings(struct net_device *netdev,
					  struct ethtool_link_ksettings *cmd)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u32 status;
	u32 speed;

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);

	/* supported link modes */
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 1000baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 2500baseT_Full);

	/* twisted pair */
	cmd->base.port = PORT_TP;
	cmd->base.phy_address = hw->phy.addr;

	/* advertising link modes */
	if (hw->phy.autoneg_advertised & ADVERTISE_10_HALF)
		ethtool_link_ksettings_add_link_mode(cmd, advertising, 10baseT_Half);
	if (hw->phy.autoneg_advertised & ADVERTISE_10_FULL)
		ethtool_link_ksettings_add_link_mode(cmd, advertising, 10baseT_Full);
	if (hw->phy.autoneg_advertised & ADVERTISE_100_HALF)
		ethtool_link_ksettings_add_link_mode(cmd, advertising, 100baseT_Half);
	if (hw->phy.autoneg_advertised & ADVERTISE_100_FULL)
		ethtool_link_ksettings_add_link_mode(cmd, advertising, 100baseT_Full);
	if (hw->phy.autoneg_advertised & ADVERTISE_1000_FULL)
		ethtool_link_ksettings_add_link_mode(cmd, advertising, 1000baseT_Full);
	if (hw->phy.autoneg_advertised & ADVERTISE_2500_FULL)
		ethtool_link_ksettings_add_link_mode(cmd, advertising, 2500baseT_Full);

	/* set autoneg settings */
	if (hw->mac.autoneg == 1) {
		ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Autoneg);
	}

	/* Set pause flow control settings */
	ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);

	switch (hw->fc.requested_mode) {
	case igc_fc_full:
		ethtool_link_ksettings_add_link_mode(cmd, advertising, Pause);
		break;
	case igc_fc_rx_pause:
		ethtool_link_ksettings_add_link_mode(cmd, advertising, Pause);
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);
		break;
	case igc_fc_tx_pause:
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);
		break;
	default:
		break;
	}

	status = pm_runtime_suspended(&adapter->pdev->dev) ?
		 0 : rd32(IGC_STATUS);

	if (status & IGC_STATUS_LU) {
		if (status & IGC_STATUS_SPEED_1000) {
			/* For I225, STATUS will indicate 1G speed in both
			 * 1 Gbps and 2.5 Gbps link modes.
			 * An additional bit is used
			 * to differentiate between 1 Gbps and 2.5 Gbps.
			 */
			if (hw->mac.type == igc_i225 &&
			    (status & IGC_STATUS_SPEED_2500)) {
				speed = SPEED_2500;
			} else {
				speed = SPEED_1000;
			}
		} else if (status & IGC_STATUS_SPEED_100) {
			speed = SPEED_100;
		} else {
			speed = SPEED_10;
		}
		if ((status & IGC_STATUS_FD) ||
		    hw->phy.media_type != igc_media_type_copper)
			cmd->base.duplex = DUPLEX_FULL;
		else
			cmd->base.duplex = DUPLEX_HALF;
	} else {
		speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}
	cmd->base.speed = speed;
	if (hw->mac.autoneg)
		cmd->base.autoneg = AUTONEG_ENABLE;
	else
		cmd->base.autoneg = AUTONEG_DISABLE;

	/* MDI-X => 2; MDI =>1; Invalid =>0 */
	if (hw->phy.media_type == igc_media_type_copper)
		cmd->base.eth_tp_mdix = hw->phy.is_mdix ? ETH_TP_MDI_X :
						      ETH_TP_MDI;
	else
		cmd->base.eth_tp_mdix = ETH_TP_MDI_INVALID;

	if (hw->phy.mdix == AUTO_ALL_MODES)
		cmd->base.eth_tp_mdix_ctrl = ETH_TP_MDI_AUTO;
	else
		cmd->base.eth_tp_mdix_ctrl = hw->phy.mdix;

	return 0;
}

static int
igc_ethtool_set_link_ksettings(struct net_device *netdev,
			       const struct ethtool_link_ksettings *cmd)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct net_device *dev = adapter->netdev;
	struct igc_hw *hw = &adapter->hw;
	u32 advertising;

	/* When adapter in resetting mode, autoneg/speed/duplex
	 * cannot be changed
	 */
	if (igc_check_reset_block(hw)) {
		netdev_err(dev, "Cannot change link characteristics when reset is active\n");
		return -EINVAL;
	}

	/* MDI setting is only allowed when autoneg enabled because
	 * some hardware doesn't allow MDI setting when speed or
	 * duplex is forced.
	 */
	if (cmd->base.eth_tp_mdix_ctrl) {
		if (cmd->base.eth_tp_mdix_ctrl != ETH_TP_MDI_AUTO &&
		    cmd->base.autoneg != AUTONEG_ENABLE) {
			netdev_err(dev, "Forcing MDI/MDI-X state is not supported when link speed and/or duplex are forced\n");
			return -EINVAL;
		}
	}

	while (test_and_set_bit(__IGC_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);
	/* Converting to legacy u32 drops ETHTOOL_LINK_MODE_2500baseT_Full_BIT.
	 * We have to check this and convert it to ADVERTISE_2500_FULL
	 * (aka ETHTOOL_LINK_MODE_2500baseX_Full_BIT) explicitly.
	 */
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising, 2500baseT_Full))
		advertising |= ADVERTISE_2500_FULL;

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		hw->mac.autoneg = 1;
		hw->phy.autoneg_advertised = advertising;
		if (adapter->fc_autoneg)
			hw->fc.requested_mode = igc_fc_default;
	} else {
		netdev_info(dev, "Force mode currently not supported\n");
	}

	/* MDI-X => 2; MDI => 1; Auto => 3 */
	if (cmd->base.eth_tp_mdix_ctrl) {
		/* fix up the value for auto (3 => 0) as zero is mapped
		 * internally to auto
		 */
		if (cmd->base.eth_tp_mdix_ctrl == ETH_TP_MDI_AUTO)
			hw->phy.mdix = AUTO_ALL_MODES;
		else
			hw->phy.mdix = cmd->base.eth_tp_mdix_ctrl;
	}

	/* reset the link */
	if (netif_running(adapter->netdev)) {
		igc_down(adapter);
		igc_up(adapter);
	} else {
		igc_reset(adapter);
	}

	clear_bit(__IGC_RESETTING, &adapter->state);

	return 0;
}

static void igc_ethtool_diag_test(struct net_device *netdev,
				  struct ethtool_test *eth_test, u64 *data)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	bool if_running = netif_running(netdev);

	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		netdev_info(adapter->netdev, "Offline testing starting");
		set_bit(__IGC_TESTING, &adapter->state);

		/* Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result
		 */
		if (!igc_link_test(adapter, &data[TEST_LINK]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			igc_close(netdev);
		else
			igc_reset(adapter);

		netdev_info(adapter->netdev, "Register testing starting");
		if (!igc_reg_test(adapter, &data[TEST_REG]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		igc_reset(adapter);

		netdev_info(adapter->netdev, "EEPROM testing starting");
		if (!igc_eeprom_test(adapter, &data[TEST_EEP]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		igc_reset(adapter);

		/* loopback and interrupt tests
		 * will be implemented in the future
		 */
		data[TEST_LOOP] = 0;
		data[TEST_IRQ] = 0;

		clear_bit(__IGC_TESTING, &adapter->state);
		if (if_running)
			igc_open(netdev);
	} else {
		netdev_info(adapter->netdev, "Online testing starting");

		/* register, eeprom, intr and loopback tests not run online */
		data[TEST_REG] = 0;
		data[TEST_EEP] = 0;
		data[TEST_IRQ] = 0;
		data[TEST_LOOP] = 0;

		if (!igc_link_test(adapter, &data[TEST_LINK]))
			eth_test->flags |= ETH_TEST_FL_FAILED;
	}

	msleep_interruptible(4 * 1000);
}

static const struct ethtool_ops igc_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS,
	.get_drvinfo		= igc_ethtool_get_drvinfo,
	.get_regs_len		= igc_ethtool_get_regs_len,
	.get_regs		= igc_ethtool_get_regs,
	.get_wol		= igc_ethtool_get_wol,
	.set_wol		= igc_ethtool_set_wol,
	.get_msglevel		= igc_ethtool_get_msglevel,
	.set_msglevel		= igc_ethtool_set_msglevel,
	.nway_reset		= igc_ethtool_nway_reset,
	.get_link		= igc_ethtool_get_link,
	.get_eeprom_len		= igc_ethtool_get_eeprom_len,
	.get_eeprom		= igc_ethtool_get_eeprom,
	.set_eeprom		= igc_ethtool_set_eeprom,
	.get_ringparam		= igc_ethtool_get_ringparam,
	.set_ringparam		= igc_ethtool_set_ringparam,
	.get_pauseparam		= igc_ethtool_get_pauseparam,
	.set_pauseparam		= igc_ethtool_set_pauseparam,
	.get_strings		= igc_ethtool_get_strings,
	.get_sset_count		= igc_ethtool_get_sset_count,
	.get_ethtool_stats	= igc_ethtool_get_stats,
	.get_coalesce		= igc_ethtool_get_coalesce,
	.set_coalesce		= igc_ethtool_set_coalesce,
	.get_rxnfc		= igc_ethtool_get_rxnfc,
	.set_rxnfc		= igc_ethtool_set_rxnfc,
	.get_rxfh_indir_size	= igc_ethtool_get_rxfh_indir_size,
	.get_rxfh		= igc_ethtool_get_rxfh,
	.set_rxfh		= igc_ethtool_set_rxfh,
	.get_ts_info		= igc_ethtool_get_ts_info,
	.get_channels		= igc_ethtool_get_channels,
	.set_channels		= igc_ethtool_set_channels,
	.get_priv_flags		= igc_ethtool_get_priv_flags,
	.set_priv_flags		= igc_ethtool_set_priv_flags,
	.get_eee		= igc_ethtool_get_eee,
	.set_eee		= igc_ethtool_set_eee,
	.begin			= igc_ethtool_begin,
	.complete		= igc_ethtool_complete,
	.get_link_ksettings	= igc_ethtool_get_link_ksettings,
	.set_link_ksettings	= igc_ethtool_set_link_ksettings,
	.self_test		= igc_ethtool_diag_test,
};

void igc_ethtool_set_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &igc_ethtool_ops;
}
