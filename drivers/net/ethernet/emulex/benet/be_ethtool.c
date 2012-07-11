/*
 * Copyright (C) 2005 - 2011 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include "be.h"
#include "be_cmds.h"
#include <linux/ethtool.h>

struct be_ethtool_stat {
	char desc[ETH_GSTRING_LEN];
	int type;
	int size;
	int offset;
};

enum {DRVSTAT_TX, DRVSTAT_RX, DRVSTAT};
#define FIELDINFO(_struct, field) FIELD_SIZEOF(_struct, field), \
					offsetof(_struct, field)
#define DRVSTAT_TX_INFO(field)	#field, DRVSTAT_TX,\
					FIELDINFO(struct be_tx_stats, field)
#define DRVSTAT_RX_INFO(field)	#field, DRVSTAT_RX,\
					FIELDINFO(struct be_rx_stats, field)
#define	DRVSTAT_INFO(field)	#field, DRVSTAT,\
					FIELDINFO(struct be_drv_stats, field)

static const struct be_ethtool_stat et_stats[] = {
	{DRVSTAT_INFO(rx_crc_errors)},
	{DRVSTAT_INFO(rx_alignment_symbol_errors)},
	{DRVSTAT_INFO(rx_pause_frames)},
	{DRVSTAT_INFO(rx_control_frames)},
	/* Received packets dropped when the Ethernet length field
	 * is not equal to the actual Ethernet data length.
	 */
	{DRVSTAT_INFO(rx_in_range_errors)},
	/* Received packets dropped when their length field is >= 1501 bytes
	 * and <= 1535 bytes.
	 */
	{DRVSTAT_INFO(rx_out_range_errors)},
	/* Received packets dropped when they are longer than 9216 bytes */
	{DRVSTAT_INFO(rx_frame_too_long)},
	/* Received packets dropped when they don't pass the unicast or
	 * multicast address filtering.
	 */
	{DRVSTAT_INFO(rx_address_mismatch_drops)},
	/* Received packets dropped when IP packet length field is less than
	 * the IP header length field.
	 */
	{DRVSTAT_INFO(rx_dropped_too_small)},
	/* Received packets dropped when IP length field is greater than
	 * the actual packet length.
	 */
	{DRVSTAT_INFO(rx_dropped_too_short)},
	/* Received packets dropped when the IP header length field is less
	 * than 5.
	 */
	{DRVSTAT_INFO(rx_dropped_header_too_small)},
	/* Received packets dropped when the TCP header length field is less
	 * than 5 or the TCP header length + IP header length is more
	 * than IP packet length.
	 */
	{DRVSTAT_INFO(rx_dropped_tcp_length)},
	{DRVSTAT_INFO(rx_dropped_runt)},
	/* Number of received packets dropped when a fifo for descriptors going
	 * into the packet demux block overflows. In normal operation, this
	 * fifo must never overflow.
	 */
	{DRVSTAT_INFO(rxpp_fifo_overflow_drop)},
	{DRVSTAT_INFO(rx_input_fifo_overflow_drop)},
	{DRVSTAT_INFO(rx_ip_checksum_errs)},
	{DRVSTAT_INFO(rx_tcp_checksum_errs)},
	{DRVSTAT_INFO(rx_udp_checksum_errs)},
	{DRVSTAT_INFO(tx_pauseframes)},
	{DRVSTAT_INFO(tx_controlframes)},
	{DRVSTAT_INFO(rx_priority_pause_frames)},
	/* Received packets dropped when an internal fifo going into
	 * main packet buffer tank (PMEM) overflows.
	 */
	{DRVSTAT_INFO(pmem_fifo_overflow_drop)},
	{DRVSTAT_INFO(jabber_events)},
	/* Received packets dropped due to lack of available HW packet buffers
	 * used to temporarily hold the received packets.
	 */
	{DRVSTAT_INFO(rx_drops_no_pbuf)},
	/* Received packets dropped due to input receive buffer
	 * descriptor fifo overflowing.
	 */
	{DRVSTAT_INFO(rx_drops_no_erx_descr)},
	/* Packets dropped because the internal FIFO to the offloaded TCP
	 * receive processing block is full. This could happen only for
	 * offloaded iSCSI or FCoE trarffic.
	 */
	{DRVSTAT_INFO(rx_drops_no_tpre_descr)},
	/* Received packets dropped when they need more than 8
	 * receive buffers. This cannot happen as the driver configures
	 * 2048 byte receive buffers.
	 */
	{DRVSTAT_INFO(rx_drops_too_many_frags)},
	{DRVSTAT_INFO(forwarded_packets)},
	/* Received packets dropped when the frame length
	 * is more than 9018 bytes
	 */
	{DRVSTAT_INFO(rx_drops_mtu)},
	/* Number of packets dropped due to random early drop function */
	{DRVSTAT_INFO(eth_red_drops)},
	{DRVSTAT_INFO(be_on_die_temperature)}
};
#define ETHTOOL_STATS_NUM ARRAY_SIZE(et_stats)

/* Stats related to multi RX queues: get_stats routine assumes bytes, pkts
 * are first and second members respectively.
 */
static const struct be_ethtool_stat et_rx_stats[] = {
	{DRVSTAT_RX_INFO(rx_bytes)},/* If moving this member see above note */
	{DRVSTAT_RX_INFO(rx_pkts)}, /* If moving this member see above note */
	{DRVSTAT_RX_INFO(rx_compl)},
	{DRVSTAT_RX_INFO(rx_mcast_pkts)},
	/* Number of page allocation failures while posting receive buffers
	 * to HW.
	 */
	{DRVSTAT_RX_INFO(rx_post_fail)},
	/* Recevied packets dropped due to skb allocation failure */
	{DRVSTAT_RX_INFO(rx_drops_no_skbs)},
	/* Received packets dropped due to lack of available fetched buffers
	 * posted by the driver.
	 */
	{DRVSTAT_RX_INFO(rx_drops_no_frags)}
};
#define ETHTOOL_RXSTATS_NUM (ARRAY_SIZE(et_rx_stats))

/* Stats related to multi TX queues: get_stats routine assumes compl is the
 * first member
 */
static const struct be_ethtool_stat et_tx_stats[] = {
	{DRVSTAT_TX_INFO(tx_compl)}, /* If moving this member see above note */
	{DRVSTAT_TX_INFO(tx_bytes)},
	{DRVSTAT_TX_INFO(tx_pkts)},
	/* Number of skbs queued for trasmission by the driver */
	{DRVSTAT_TX_INFO(tx_reqs)},
	/* Number of TX work request blocks DMAed to HW */
	{DRVSTAT_TX_INFO(tx_wrbs)},
	/* Number of times the TX queue was stopped due to lack
	 * of spaces in the TXQ.
	 */
	{DRVSTAT_TX_INFO(tx_stops)}
};
#define ETHTOOL_TXSTATS_NUM (ARRAY_SIZE(et_tx_stats))

static const char et_self_tests[][ETH_GSTRING_LEN] = {
	"MAC Loopback test",
	"PHY Loopback test",
	"External Loopback test",
	"DDR DMA test",
	"Link test"
};

#define ETHTOOL_TESTS_NUM ARRAY_SIZE(et_self_tests)
#define BE_MAC_LOOPBACK 0x0
#define BE_PHY_LOOPBACK 0x1
#define BE_ONE_PORT_EXT_LOOPBACK 0x2
#define BE_NO_LOOPBACK 0xff

static void be_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *drvinfo)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	char fw_on_flash[FW_VER_LEN];

	memset(fw_on_flash, 0 , sizeof(fw_on_flash));
	be_cmd_get_fw_ver(adapter, adapter->fw_ver, fw_on_flash);

	strlcpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRV_VER, sizeof(drvinfo->version));
	strncpy(drvinfo->fw_version, adapter->fw_ver, FW_VER_LEN);
	if (memcmp(adapter->fw_ver, fw_on_flash, FW_VER_LEN) != 0) {
		strcat(drvinfo->fw_version, " [");
		strcat(drvinfo->fw_version, fw_on_flash);
		strcat(drvinfo->fw_version, "]");
	}

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

static u32
lancer_cmd_get_file_len(struct be_adapter *adapter, u8 *file_name)
{
	u32 data_read = 0, eof;
	u8 addn_status;
	struct be_dma_mem data_len_cmd;
	int status;

	memset(&data_len_cmd, 0, sizeof(data_len_cmd));
	/* data_offset and data_size should be 0 to get reg len */
	status = lancer_cmd_read_object(adapter, &data_len_cmd, 0, 0,
				file_name, &data_read, &eof, &addn_status);

	return data_read;
}

static int
lancer_cmd_read_file(struct be_adapter *adapter, u8 *file_name,
		u32 buf_len, void *buf)
{
	struct be_dma_mem read_cmd;
	u32 read_len = 0, total_read_len = 0, chunk_size;
	u32 eof = 0;
	u8 addn_status;
	int status = 0;

	read_cmd.size = LANCER_READ_FILE_CHUNK;
	read_cmd.va = pci_alloc_consistent(adapter->pdev, read_cmd.size,
			&read_cmd.dma);

	if (!read_cmd.va) {
		dev_err(&adapter->pdev->dev,
				"Memory allocation failure while reading dump\n");
		return -ENOMEM;
	}

	while ((total_read_len < buf_len) && !eof) {
		chunk_size = min_t(u32, (buf_len - total_read_len),
				LANCER_READ_FILE_CHUNK);
		chunk_size = ALIGN(chunk_size, 4);
		status = lancer_cmd_read_object(adapter, &read_cmd, chunk_size,
				total_read_len, file_name, &read_len,
				&eof, &addn_status);
		if (!status) {
			memcpy(buf + total_read_len, read_cmd.va, read_len);
			total_read_len += read_len;
			eof &= LANCER_READ_FILE_EOF_MASK;
		} else {
			status = -EIO;
			break;
		}
	}
	pci_free_consistent(adapter->pdev, read_cmd.size, read_cmd.va,
			read_cmd.dma);

	return status;
}

static int
be_get_reg_len(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	u32 log_size = 0;

	if (be_physfn(adapter)) {
		if (lancer_chip(adapter))
			log_size = lancer_cmd_get_file_len(adapter,
					LANCER_FW_DUMP_FILE);
		else
			be_cmd_get_reg_len(adapter, &log_size);
	}
	return log_size;
}

static void
be_get_regs(struct net_device *netdev, struct ethtool_regs *regs, void *buf)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (be_physfn(adapter)) {
		memset(buf, 0, regs->len);
		if (lancer_chip(adapter))
			lancer_cmd_read_file(adapter, LANCER_FW_DUMP_FILE,
					regs->len, buf);
		else
			be_cmd_get_regs(adapter, regs->len, buf);
	}
}

static int be_get_coalesce(struct net_device *netdev,
			   struct ethtool_coalesce *et)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *eqo = &adapter->eq_obj[0];


	et->rx_coalesce_usecs = eqo->cur_eqd;
	et->rx_coalesce_usecs_high = eqo->max_eqd;
	et->rx_coalesce_usecs_low = eqo->min_eqd;

	et->tx_coalesce_usecs = eqo->cur_eqd;
	et->tx_coalesce_usecs_high = eqo->max_eqd;
	et->tx_coalesce_usecs_low = eqo->min_eqd;

	et->use_adaptive_rx_coalesce = eqo->enable_aic;
	et->use_adaptive_tx_coalesce = eqo->enable_aic;

	return 0;
}

/* TX attributes are ignored. Only RX attributes are considered
 * eqd cmd is issued in the worker thread.
 */
static int be_set_coalesce(struct net_device *netdev,
			   struct ethtool_coalesce *et)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *eqo;
	int i;

	for_all_evt_queues(adapter, eqo, i) {
		eqo->enable_aic = et->use_adaptive_rx_coalesce;
		eqo->max_eqd = min(et->rx_coalesce_usecs_high, BE_MAX_EQD);
		eqo->min_eqd = min(et->rx_coalesce_usecs_low, eqo->max_eqd);
		eqo->eqd = et->rx_coalesce_usecs;
	}

	return 0;
}

static void
be_get_ethtool_stats(struct net_device *netdev,
		struct ethtool_stats *stats, uint64_t *data)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_rx_obj *rxo;
	struct be_tx_obj *txo;
	void *p;
	unsigned int i, j, base = 0, start;

	for (i = 0; i < ETHTOOL_STATS_NUM; i++) {
		p = (u8 *)&adapter->drv_stats + et_stats[i].offset;
		data[i] = *(u32 *)p;
	}
	base += ETHTOOL_STATS_NUM;

	for_all_rx_queues(adapter, rxo, j) {
		struct be_rx_stats *stats = rx_stats(rxo);

		do {
			start = u64_stats_fetch_begin_bh(&stats->sync);
			data[base] = stats->rx_bytes;
			data[base + 1] = stats->rx_pkts;
		} while (u64_stats_fetch_retry_bh(&stats->sync, start));

		for (i = 2; i < ETHTOOL_RXSTATS_NUM; i++) {
			p = (u8 *)stats + et_rx_stats[i].offset;
			data[base + i] = *(u32 *)p;
		}
		base += ETHTOOL_RXSTATS_NUM;
	}

	for_all_tx_queues(adapter, txo, j) {
		struct be_tx_stats *stats = tx_stats(txo);

		do {
			start = u64_stats_fetch_begin_bh(&stats->sync_compl);
			data[base] = stats->tx_compl;
		} while (u64_stats_fetch_retry_bh(&stats->sync_compl, start));

		do {
			start = u64_stats_fetch_begin_bh(&stats->sync);
			for (i = 1; i < ETHTOOL_TXSTATS_NUM; i++) {
				p = (u8 *)stats + et_tx_stats[i].offset;
				data[base + i] =
					(et_tx_stats[i].size == sizeof(u64)) ?
						*(u64 *)p : *(u32 *)p;
			}
		} while (u64_stats_fetch_retry_bh(&stats->sync, start));
		base += ETHTOOL_TXSTATS_NUM;
	}
}

static void
be_get_stat_strings(struct net_device *netdev, uint32_t stringset,
		uint8_t *data)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ETHTOOL_STATS_NUM; i++) {
			memcpy(data, et_stats[i].desc, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->num_rx_qs; i++) {
			for (j = 0; j < ETHTOOL_RXSTATS_NUM; j++) {
				sprintf(data, "rxq%d: %s", i,
					et_rx_stats[j].desc);
				data += ETH_GSTRING_LEN;
			}
		}
		for (i = 0; i < adapter->num_tx_qs; i++) {
			for (j = 0; j < ETHTOOL_TXSTATS_NUM; j++) {
				sprintf(data, "txq%d: %s", i,
					et_tx_stats[j].desc);
				data += ETH_GSTRING_LEN;
			}
		}
		break;
	case ETH_SS_TEST:
		for (i = 0; i < ETHTOOL_TESTS_NUM; i++) {
			memcpy(data, et_self_tests[i], ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int be_get_sset_count(struct net_device *netdev, int stringset)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	switch (stringset) {
	case ETH_SS_TEST:
		return ETHTOOL_TESTS_NUM;
	case ETH_SS_STATS:
		return ETHTOOL_STATS_NUM +
			adapter->num_rx_qs * ETHTOOL_RXSTATS_NUM +
			adapter->num_tx_qs * ETHTOOL_TXSTATS_NUM;
	default:
		return -EINVAL;
	}
}

static u32 be_get_port_type(u32 phy_type, u32 dac_cable_len)
{
	u32 port;

	switch (phy_type) {
	case PHY_TYPE_BASET_1GB:
	case PHY_TYPE_BASEX_1GB:
	case PHY_TYPE_SGMII:
		port = PORT_TP;
		break;
	case PHY_TYPE_SFP_PLUS_10GB:
		port = dac_cable_len ? PORT_DA : PORT_FIBRE;
		break;
	case PHY_TYPE_XFP_10GB:
	case PHY_TYPE_SFP_1GB:
		port = PORT_FIBRE;
		break;
	case PHY_TYPE_BASET_10GB:
		port = PORT_TP;
		break;
	default:
		port = PORT_OTHER;
	}

	return port;
}

static u32 convert_to_et_setting(u32 if_type, u32 if_speeds)
{
	u32 val = 0;

	switch (if_type) {
	case PHY_TYPE_BASET_1GB:
	case PHY_TYPE_BASEX_1GB:
	case PHY_TYPE_SGMII:
		val |= SUPPORTED_TP;
		if (if_speeds & BE_SUPPORTED_SPEED_1GBPS)
			val |= SUPPORTED_1000baseT_Full;
		if (if_speeds & BE_SUPPORTED_SPEED_100MBPS)
			val |= SUPPORTED_100baseT_Full;
		if (if_speeds & BE_SUPPORTED_SPEED_10MBPS)
			val |= SUPPORTED_10baseT_Full;
		break;
	case PHY_TYPE_KX4_10GB:
		val |= SUPPORTED_Backplane;
		if (if_speeds & BE_SUPPORTED_SPEED_1GBPS)
			val |= SUPPORTED_1000baseKX_Full;
		if (if_speeds & BE_SUPPORTED_SPEED_10GBPS)
			val |= SUPPORTED_10000baseKX4_Full;
		break;
	case PHY_TYPE_KR_10GB:
		val |= SUPPORTED_Backplane |
				SUPPORTED_10000baseKR_Full;
		break;
	case PHY_TYPE_SFP_PLUS_10GB:
	case PHY_TYPE_XFP_10GB:
	case PHY_TYPE_SFP_1GB:
		val |= SUPPORTED_FIBRE;
		if (if_speeds & BE_SUPPORTED_SPEED_10GBPS)
			val |= SUPPORTED_10000baseT_Full;
		if (if_speeds & BE_SUPPORTED_SPEED_1GBPS)
			val |= SUPPORTED_1000baseT_Full;
		break;
	case PHY_TYPE_BASET_10GB:
		val |= SUPPORTED_TP;
		if (if_speeds & BE_SUPPORTED_SPEED_10GBPS)
			val |= SUPPORTED_10000baseT_Full;
		if (if_speeds & BE_SUPPORTED_SPEED_1GBPS)
			val |= SUPPORTED_1000baseT_Full;
		if (if_speeds & BE_SUPPORTED_SPEED_100MBPS)
			val |= SUPPORTED_100baseT_Full;
		break;
	default:
		val |= SUPPORTED_TP;
	}

	return val;
}

static int convert_to_et_speed(u32 be_speed)
{
	int et_speed = SPEED_10000;

	switch (be_speed) {
	case PHY_LINK_SPEED_10MBPS:
		et_speed = SPEED_10;
		break;
	case PHY_LINK_SPEED_100MBPS:
		et_speed = SPEED_100;
		break;
	case PHY_LINK_SPEED_1GBPS:
		et_speed = SPEED_1000;
		break;
	case PHY_LINK_SPEED_10GBPS:
		et_speed = SPEED_10000;
		break;
	}

	return et_speed;
}

bool be_pause_supported(struct be_adapter *adapter)
{
	return (adapter->phy.interface_type == PHY_TYPE_SFP_PLUS_10GB ||
		adapter->phy.interface_type == PHY_TYPE_XFP_10GB) ?
		false : true;
}

static int be_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	u8 port_speed = 0;
	u16 link_speed = 0;
	u8 link_status;
	u32 et_speed = 0;
	int status;

	if (adapter->phy.link_speed < 0 || !(netdev->flags & IFF_UP)) {
		if (adapter->phy.forced_port_speed < 0) {
			status = be_cmd_link_status_query(adapter, &port_speed,
						&link_speed, &link_status, 0);
			if (!status)
				be_link_status_update(adapter, link_status);
			if (link_speed)
				et_speed = link_speed * 10;
			else if (link_status)
				et_speed = convert_to_et_speed(port_speed);
		} else {
			et_speed = adapter->phy.forced_port_speed;
		}

		ethtool_cmd_speed_set(ecmd, et_speed);

		status = be_cmd_get_phy_info(adapter);
		if (status)
			return status;

		ecmd->supported =
			convert_to_et_setting(adapter->phy.interface_type,
					adapter->phy.auto_speeds_supported |
					adapter->phy.fixed_speeds_supported);
		ecmd->advertising =
			convert_to_et_setting(adapter->phy.interface_type,
					adapter->phy.auto_speeds_supported);

		ecmd->port = be_get_port_type(adapter->phy.interface_type,
					      adapter->phy.dac_cable_len);

		if (adapter->phy.auto_speeds_supported) {
			ecmd->supported |= SUPPORTED_Autoneg;
			ecmd->autoneg = AUTONEG_ENABLE;
			ecmd->advertising |= ADVERTISED_Autoneg;
		}

		if (be_pause_supported(adapter)) {
			ecmd->supported |= SUPPORTED_Pause;
			ecmd->advertising |= ADVERTISED_Pause;
		}

		switch (adapter->phy.interface_type) {
		case PHY_TYPE_KR_10GB:
		case PHY_TYPE_KX4_10GB:
			ecmd->transceiver = XCVR_INTERNAL;
			break;
		default:
			ecmd->transceiver = XCVR_EXTERNAL;
			break;
		}

		/* Save for future use */
		adapter->phy.link_speed = ethtool_cmd_speed(ecmd);
		adapter->phy.port_type = ecmd->port;
		adapter->phy.transceiver = ecmd->transceiver;
		adapter->phy.autoneg = ecmd->autoneg;
		adapter->phy.advertising = ecmd->advertising;
		adapter->phy.supported = ecmd->supported;
	} else {
		ethtool_cmd_speed_set(ecmd, adapter->phy.link_speed);
		ecmd->port = adapter->phy.port_type;
		ecmd->transceiver = adapter->phy.transceiver;
		ecmd->autoneg = adapter->phy.autoneg;
		ecmd->advertising = adapter->phy.advertising;
		ecmd->supported = adapter->phy.supported;
	}

	ecmd->duplex = netif_carrier_ok(netdev) ? DUPLEX_FULL : DUPLEX_UNKNOWN;
	ecmd->phy_address = adapter->port_num;

	return 0;
}

static void be_get_ringparam(struct net_device *netdev,
			     struct ethtool_ringparam *ring)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = ring->rx_pending = adapter->rx_obj[0].q.len;
	ring->tx_max_pending = ring->tx_pending = adapter->tx_obj[0].q.len;
}

static void
be_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *ecmd)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	be_cmd_get_flow_control(adapter, &ecmd->tx_pause, &ecmd->rx_pause);
	ecmd->autoneg = adapter->phy.fc_autoneg;
}

static int
be_set_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *ecmd)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status;

	if (ecmd->autoneg != 0)
		return -EINVAL;
	adapter->tx_fc = ecmd->tx_pause;
	adapter->rx_fc = ecmd->rx_pause;

	status = be_cmd_set_flow_control(adapter,
					adapter->tx_fc, adapter->rx_fc);
	if (status)
		dev_warn(&adapter->pdev->dev, "Pause param set failed.\n");

	return status;
}

static int
be_set_phys_id(struct net_device *netdev,
	       enum ethtool_phys_id_state state)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		be_cmd_get_beacon_state(adapter, adapter->hba_port_num,
					&adapter->beacon_state);
		return 1;	/* cycle on/off once per second */

	case ETHTOOL_ID_ON:
		be_cmd_set_beacon_state(adapter, adapter->hba_port_num, 0, 0,
					BEACON_STATE_ENABLED);
		break;

	case ETHTOOL_ID_OFF:
		be_cmd_set_beacon_state(adapter, adapter->hba_port_num, 0, 0,
					BEACON_STATE_DISABLED);
		break;

	case ETHTOOL_ID_INACTIVE:
		be_cmd_set_beacon_state(adapter, adapter->hba_port_num, 0, 0,
					adapter->beacon_state);
	}

	return 0;
}


static void
be_get_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (be_is_wol_supported(adapter)) {
		wol->supported |= WAKE_MAGIC;
		wol->wolopts |= WAKE_MAGIC;
	} else
		wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int
be_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EOPNOTSUPP;

	if (!be_is_wol_supported(adapter)) {
		dev_warn(&adapter->pdev->dev, "WOL not supported\n");
		return -EOPNOTSUPP;
	}

	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol = true;
	else
		adapter->wol = false;

	return 0;
}

static int
be_test_ddr_dma(struct be_adapter *adapter)
{
	int ret, i;
	struct be_dma_mem ddrdma_cmd;
	static const u64 pattern[2] = {
		0x5a5a5a5a5a5a5a5aULL, 0xa5a5a5a5a5a5a5a5ULL
	};

	ddrdma_cmd.size = sizeof(struct be_cmd_req_ddrdma_test);
	ddrdma_cmd.va = dma_alloc_coherent(&adapter->pdev->dev, ddrdma_cmd.size,
					   &ddrdma_cmd.dma, GFP_KERNEL);
	if (!ddrdma_cmd.va) {
		dev_err(&adapter->pdev->dev, "Memory allocation failure\n");
		return -ENOMEM;
	}

	for (i = 0; i < 2; i++) {
		ret = be_cmd_ddr_dma_test(adapter, pattern[i],
					4096, &ddrdma_cmd);
		if (ret != 0)
			goto err;
	}

err:
	dma_free_coherent(&adapter->pdev->dev, ddrdma_cmd.size, ddrdma_cmd.va,
			  ddrdma_cmd.dma);
	return ret;
}

static u64 be_loopback_test(struct be_adapter *adapter, u8 loopback_type,
				u64 *status)
{
	be_cmd_set_loopback(adapter, adapter->hba_port_num,
				loopback_type, 1);
	*status = be_cmd_loopback_test(adapter, adapter->hba_port_num,
				loopback_type, 1500,
				2, 0xabc);
	be_cmd_set_loopback(adapter, adapter->hba_port_num,
				BE_NO_LOOPBACK, 1);
	return *status;
}

static void
be_self_test(struct net_device *netdev, struct ethtool_test *test, u64 *data)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	u8 mac_speed = 0;
	u16 qos_link_speed = 0;

	memset(data, 0, sizeof(u64) * ETHTOOL_TESTS_NUM);

	if (test->flags & ETH_TEST_FL_OFFLINE) {
		if (be_loopback_test(adapter, BE_MAC_LOOPBACK,
						&data[0]) != 0) {
			test->flags |= ETH_TEST_FL_FAILED;
		}
		if (be_loopback_test(adapter, BE_PHY_LOOPBACK,
						&data[1]) != 0) {
			test->flags |= ETH_TEST_FL_FAILED;
		}
		if (be_loopback_test(adapter, BE_ONE_PORT_EXT_LOOPBACK,
						&data[2]) != 0) {
			test->flags |= ETH_TEST_FL_FAILED;
		}
	}

	if (!lancer_chip(adapter) && be_test_ddr_dma(adapter) != 0) {
		data[3] = 1;
		test->flags |= ETH_TEST_FL_FAILED;
	}

	if (be_cmd_link_status_query(adapter, &mac_speed,
				     &qos_link_speed, NULL, 0) != 0) {
		test->flags |= ETH_TEST_FL_FAILED;
		data[4] = -1;
	} else if (!mac_speed) {
		test->flags |= ETH_TEST_FL_FAILED;
		data[4] = 1;
	}
}

static int
be_do_flash(struct net_device *netdev, struct ethtool_flash *efl)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	return be_load_fw(adapter, efl->data);
}

static int
be_get_eeprom_len(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	if (lancer_chip(adapter)) {
		if (be_physfn(adapter))
			return lancer_cmd_get_file_len(adapter,
					LANCER_VPD_PF_FILE);
		else
			return lancer_cmd_get_file_len(adapter,
					LANCER_VPD_VF_FILE);
	} else {
		return BE_READ_SEEPROM_LEN;
	}
}

static int
be_read_eeprom(struct net_device *netdev, struct ethtool_eeprom *eeprom,
			uint8_t *data)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_dma_mem eeprom_cmd;
	struct be_cmd_resp_seeprom_read *resp;
	int status;

	if (!eeprom->len)
		return -EINVAL;

	if (lancer_chip(adapter)) {
		if (be_physfn(adapter))
			return lancer_cmd_read_file(adapter, LANCER_VPD_PF_FILE,
					eeprom->len, data);
		else
			return lancer_cmd_read_file(adapter, LANCER_VPD_VF_FILE,
					eeprom->len, data);
	}

	eeprom->magic = BE_VENDOR_ID | (adapter->pdev->device<<16);

	memset(&eeprom_cmd, 0, sizeof(struct be_dma_mem));
	eeprom_cmd.size = sizeof(struct be_cmd_req_seeprom_read);
	eeprom_cmd.va = dma_alloc_coherent(&adapter->pdev->dev, eeprom_cmd.size,
					   &eeprom_cmd.dma, GFP_KERNEL);

	if (!eeprom_cmd.va) {
		dev_err(&adapter->pdev->dev,
			"Memory allocation failure. Could not read eeprom\n");
		return -ENOMEM;
	}

	status = be_cmd_get_seeprom_data(adapter, &eeprom_cmd);

	if (!status) {
		resp = eeprom_cmd.va;
		memcpy(data, resp->seeprom_data + eeprom->offset, eeprom->len);
	}
	dma_free_coherent(&adapter->pdev->dev, eeprom_cmd.size, eeprom_cmd.va,
			  eeprom_cmd.dma);

	return status;
}

static u32 be_get_msg_level(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (lancer_chip(adapter)) {
		dev_err(&adapter->pdev->dev, "Operation not supported\n");
		return -EOPNOTSUPP;
	}

	return adapter->msg_enable;
}

static void be_set_fw_log_level(struct be_adapter *adapter, u32 level)
{
	struct be_dma_mem extfat_cmd;
	struct be_fat_conf_params *cfgs;
	int status;
	int i, j;

	memset(&extfat_cmd, 0, sizeof(struct be_dma_mem));
	extfat_cmd.size = sizeof(struct be_cmd_resp_get_ext_fat_caps);
	extfat_cmd.va = pci_alloc_consistent(adapter->pdev, extfat_cmd.size,
					     &extfat_cmd.dma);
	if (!extfat_cmd.va) {
		dev_err(&adapter->pdev->dev, "%s: Memory allocation failure\n",
			__func__);
		goto err;
	}
	status = be_cmd_get_ext_fat_capabilites(adapter, &extfat_cmd);
	if (!status) {
		cfgs = (struct be_fat_conf_params *)(extfat_cmd.va +
					sizeof(struct be_cmd_resp_hdr));
		for (i = 0; i < cfgs->num_modules; i++) {
			for (j = 0; j < cfgs->module[i].num_modes; j++) {
				if (cfgs->module[i].trace_lvl[j].mode ==
								MODE_UART)
					cfgs->module[i].trace_lvl[j].dbg_lvl =
							cpu_to_le32(level);
			}
		}
		status = be_cmd_set_ext_fat_capabilites(adapter, &extfat_cmd,
							cfgs);
		if (status)
			dev_err(&adapter->pdev->dev,
				"Message level set failed\n");
	} else {
		dev_err(&adapter->pdev->dev, "Message level get failed\n");
	}

	pci_free_consistent(adapter->pdev, extfat_cmd.size, extfat_cmd.va,
			    extfat_cmd.dma);
err:
	return;
}

static void be_set_msg_level(struct net_device *netdev, u32 level)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (lancer_chip(adapter)) {
		dev_err(&adapter->pdev->dev, "Operation not supported\n");
		return;
	}

	if (adapter->msg_enable == level)
		return;

	if ((level & NETIF_MSG_HW) != (adapter->msg_enable & NETIF_MSG_HW))
		be_set_fw_log_level(adapter, level & NETIF_MSG_HW ?
				    FW_LOG_LEVEL_DEFAULT : FW_LOG_LEVEL_FATAL);
	adapter->msg_enable = level;

	return;
}

const struct ethtool_ops be_ethtool_ops = {
	.get_settings = be_get_settings,
	.get_drvinfo = be_get_drvinfo,
	.get_wol = be_get_wol,
	.set_wol = be_set_wol,
	.get_link = ethtool_op_get_link,
	.get_eeprom_len = be_get_eeprom_len,
	.get_eeprom = be_read_eeprom,
	.get_coalesce = be_get_coalesce,
	.set_coalesce = be_set_coalesce,
	.get_ringparam = be_get_ringparam,
	.get_pauseparam = be_get_pauseparam,
	.set_pauseparam = be_set_pauseparam,
	.get_strings = be_get_stat_strings,
	.set_phys_id = be_set_phys_id,
	.get_msglevel = be_get_msg_level,
	.set_msglevel = be_set_msg_level,
	.get_sset_count = be_get_sset_count,
	.get_ethtool_stats = be_get_ethtool_stats,
	.get_regs_len = be_get_reg_len,
	.get_regs = be_get_regs,
	.flash_device = be_do_flash,
	.self_test = be_self_test,
};
