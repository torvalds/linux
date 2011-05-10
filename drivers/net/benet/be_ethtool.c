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

enum {NETSTAT, PORTSTAT, MISCSTAT, DRVSTAT_TX, DRVSTAT_RX, ERXSTAT,
			PMEMSTAT, DRVSTAT};
#define FIELDINFO(_struct, field) FIELD_SIZEOF(_struct, field), \
					offsetof(_struct, field)
#define NETSTAT_INFO(field) 	#field, NETSTAT,\
					FIELDINFO(struct net_device_stats,\
						field)
#define DRVSTAT_TX_INFO(field)	#field, DRVSTAT_TX,\
					FIELDINFO(struct be_tx_stats, field)
#define DRVSTAT_RX_INFO(field)	#field, DRVSTAT_RX,\
					FIELDINFO(struct be_rx_stats, field)
#define MISCSTAT_INFO(field) 	#field, MISCSTAT,\
					FIELDINFO(struct be_rxf_stats, field)
#define PORTSTAT_INFO(field) 	#field, PORTSTAT,\
					FIELDINFO(struct be_port_rxf_stats, \
						field)
#define ERXSTAT_INFO(field) 	#field, ERXSTAT,\
					FIELDINFO(struct be_erx_stats, field)
#define PMEMSTAT_INFO(field) 	#field, PMEMSTAT,\
					FIELDINFO(struct be_pmem_stats, field)
#define	DRVSTAT_INFO(field)	#field, DRVSTAT,\
					FIELDINFO(struct be_drv_stats, \
						field)

static const struct be_ethtool_stat et_stats[] = {
	{NETSTAT_INFO(rx_packets)},
	{NETSTAT_INFO(tx_packets)},
	{NETSTAT_INFO(rx_bytes)},
	{NETSTAT_INFO(tx_bytes)},
	{NETSTAT_INFO(rx_errors)},
	{NETSTAT_INFO(tx_errors)},
	{NETSTAT_INFO(rx_dropped)},
	{NETSTAT_INFO(tx_dropped)},
	{DRVSTAT_TX_INFO(be_tx_rate)},
	{DRVSTAT_TX_INFO(be_tx_reqs)},
	{DRVSTAT_TX_INFO(be_tx_wrbs)},
	{DRVSTAT_TX_INFO(be_tx_stops)},
	{DRVSTAT_TX_INFO(be_tx_events)},
	{DRVSTAT_TX_INFO(be_tx_compl)},
	{PORTSTAT_INFO(rx_unicast_frames)},
	{PORTSTAT_INFO(rx_multicast_frames)},
	{PORTSTAT_INFO(rx_broadcast_frames)},
	{PORTSTAT_INFO(rx_crc_errors)},
	{PORTSTAT_INFO(rx_alignment_symbol_errors)},
	{PORTSTAT_INFO(rx_pause_frames)},
	{PORTSTAT_INFO(rx_control_frames)},
	{PORTSTAT_INFO(rx_in_range_errors)},
	{PORTSTAT_INFO(rx_out_range_errors)},
	{PORTSTAT_INFO(rx_frame_too_long)},
	{PORTSTAT_INFO(rx_address_match_errors)},
	{PORTSTAT_INFO(rx_vlan_mismatch)},
	{PORTSTAT_INFO(rx_dropped_too_small)},
	{PORTSTAT_INFO(rx_dropped_too_short)},
	{PORTSTAT_INFO(rx_dropped_header_too_small)},
	{PORTSTAT_INFO(rx_dropped_tcp_length)},
	{PORTSTAT_INFO(rx_dropped_runt)},
	{PORTSTAT_INFO(rx_fifo_overflow)},
	{PORTSTAT_INFO(rx_input_fifo_overflow)},
	{PORTSTAT_INFO(rx_ip_checksum_errs)},
	{PORTSTAT_INFO(rx_tcp_checksum_errs)},
	{PORTSTAT_INFO(rx_udp_checksum_errs)},
	{PORTSTAT_INFO(rx_non_rss_packets)},
	{PORTSTAT_INFO(rx_ipv4_packets)},
	{PORTSTAT_INFO(rx_ipv6_packets)},
	{PORTSTAT_INFO(rx_switched_unicast_packets)},
	{PORTSTAT_INFO(rx_switched_multicast_packets)},
	{PORTSTAT_INFO(rx_switched_broadcast_packets)},
	{PORTSTAT_INFO(tx_unicastframes)},
	{PORTSTAT_INFO(tx_multicastframes)},
	{PORTSTAT_INFO(tx_broadcastframes)},
	{PORTSTAT_INFO(tx_pauseframes)},
	{PORTSTAT_INFO(tx_controlframes)},
	{MISCSTAT_INFO(rx_drops_no_pbuf)},
	{MISCSTAT_INFO(rx_drops_no_txpb)},
	{MISCSTAT_INFO(rx_drops_no_erx_descr)},
	{MISCSTAT_INFO(rx_drops_no_tpre_descr)},
	{MISCSTAT_INFO(rx_drops_too_many_frags)},
	{MISCSTAT_INFO(rx_drops_invalid_ring)},
	{MISCSTAT_INFO(forwarded_packets)},
	{MISCSTAT_INFO(rx_drops_mtu)},
	{MISCSTAT_INFO(port0_jabber_events)},
	{MISCSTAT_INFO(port1_jabber_events)},
	{PMEMSTAT_INFO(eth_red_drops)},
	{DRVSTAT_INFO(be_on_die_temperature)}
};
#define ETHTOOL_STATS_NUM ARRAY_SIZE(et_stats)

/* Stats related to multi RX queues */
static const struct be_ethtool_stat et_rx_stats[] = {
	{DRVSTAT_RX_INFO(rx_bytes)},
	{DRVSTAT_RX_INFO(rx_pkts)},
	{DRVSTAT_RX_INFO(rx_rate)},
	{DRVSTAT_RX_INFO(rx_polls)},
	{DRVSTAT_RX_INFO(rx_events)},
	{DRVSTAT_RX_INFO(rx_compl)},
	{DRVSTAT_RX_INFO(rx_mcast_pkts)},
	{DRVSTAT_RX_INFO(rx_post_fail)},
	{ERXSTAT_INFO(rx_drops_no_fragments)}
};
#define ETHTOOL_RXSTATS_NUM (ARRAY_SIZE(et_rx_stats))

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

static void
be_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	strcpy(drvinfo->driver, DRV_NAME);
	strcpy(drvinfo->version, DRV_VER);
	strncpy(drvinfo->fw_version, adapter->fw_ver, FW_VER_LEN);
	strcpy(drvinfo->bus_info, pci_name(adapter->pdev));
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

static int
be_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *coalesce)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *rx_eq = &adapter->rx_obj[0].rx_eq;
	struct be_eq_obj *tx_eq = &adapter->tx_eq;

	coalesce->rx_coalesce_usecs = rx_eq->cur_eqd;
	coalesce->rx_coalesce_usecs_high = rx_eq->max_eqd;
	coalesce->rx_coalesce_usecs_low = rx_eq->min_eqd;

	coalesce->tx_coalesce_usecs = tx_eq->cur_eqd;
	coalesce->tx_coalesce_usecs_high = tx_eq->max_eqd;
	coalesce->tx_coalesce_usecs_low = tx_eq->min_eqd;

	coalesce->use_adaptive_rx_coalesce = rx_eq->enable_aic;
	coalesce->use_adaptive_tx_coalesce = tx_eq->enable_aic;

	return 0;
}

/*
 * This routine is used to set interrup coalescing delay
 */
static int
be_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *coalesce)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_rx_obj *rxo;
	struct be_eq_obj *rx_eq;
	struct be_eq_obj *tx_eq = &adapter->tx_eq;
	u32 tx_max, tx_min, tx_cur;
	u32 rx_max, rx_min, rx_cur;
	int status = 0, i;

	if (coalesce->use_adaptive_tx_coalesce == 1)
		return -EINVAL;

	for_all_rx_queues(adapter, rxo, i) {
		rx_eq = &rxo->rx_eq;

		if (!rx_eq->enable_aic && coalesce->use_adaptive_rx_coalesce)
			rx_eq->cur_eqd = 0;
		rx_eq->enable_aic = coalesce->use_adaptive_rx_coalesce;

		rx_max = coalesce->rx_coalesce_usecs_high;
		rx_min = coalesce->rx_coalesce_usecs_low;
		rx_cur = coalesce->rx_coalesce_usecs;

		if (rx_eq->enable_aic) {
			if (rx_max > BE_MAX_EQD)
				rx_max = BE_MAX_EQD;
			if (rx_min > rx_max)
				rx_min = rx_max;
			rx_eq->max_eqd = rx_max;
			rx_eq->min_eqd = rx_min;
			if (rx_eq->cur_eqd > rx_max)
				rx_eq->cur_eqd = rx_max;
			if (rx_eq->cur_eqd < rx_min)
				rx_eq->cur_eqd = rx_min;
		} else {
			if (rx_cur > BE_MAX_EQD)
				rx_cur = BE_MAX_EQD;
			if (rx_eq->cur_eqd != rx_cur) {
				status = be_cmd_modify_eqd(adapter, rx_eq->q.id,
						rx_cur);
				if (!status)
					rx_eq->cur_eqd = rx_cur;
			}
		}
	}

	tx_max = coalesce->tx_coalesce_usecs_high;
	tx_min = coalesce->tx_coalesce_usecs_low;
	tx_cur = coalesce->tx_coalesce_usecs;

	if (tx_cur > BE_MAX_EQD)
		tx_cur = BE_MAX_EQD;
	if (tx_eq->cur_eqd != tx_cur) {
		status = be_cmd_modify_eqd(adapter, tx_eq->q.id, tx_cur);
		if (!status)
			tx_eq->cur_eqd = tx_cur;
	}

	return 0;
}

static u32 be_get_rx_csum(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	return adapter->rx_csum;
}

static int be_set_rx_csum(struct net_device *netdev, uint32_t data)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (data)
		adapter->rx_csum = true;
	else
		adapter->rx_csum = false;

	return 0;
}

static void
be_get_ethtool_stats(struct net_device *netdev,
		struct ethtool_stats *stats, uint64_t *data)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_hw_stats *hw_stats = hw_stats_from_cmd(adapter->stats_cmd.va);
	struct be_erx_stats *erx_stats = &hw_stats->erx;
	struct be_rx_obj *rxo;
	void *p = NULL;
	int i, j;

	for (i = 0; i < ETHTOOL_STATS_NUM; i++) {
		switch (et_stats[i].type) {
		case NETSTAT:
			p = &netdev->stats;
			break;
		case DRVSTAT_TX:
			p = &adapter->tx_stats;
			break;
		case PORTSTAT:
			p = &hw_stats->rxf.port[adapter->port_num];
			break;
		case MISCSTAT:
			p = &hw_stats->rxf;
			break;
		case PMEMSTAT:
			p = &hw_stats->pmem;
			break;
		case DRVSTAT:
			p = &adapter->drv_stats;
			break;
		}

		p = (u8 *)p + et_stats[i].offset;
		data[i] = (et_stats[i].size == sizeof(u64)) ?
				*(u64 *)p: *(u32 *)p;
	}

	for_all_rx_queues(adapter, rxo, j) {
		for (i = 0; i < ETHTOOL_RXSTATS_NUM; i++) {
			switch (et_rx_stats[i].type) {
			case DRVSTAT_RX:
				p = (u8 *)&rxo->stats + et_rx_stats[i].offset;
				break;
			case ERXSTAT:
				p = (u32 *)erx_stats + rxo->q.id;
				break;
			}
			data[ETHTOOL_STATS_NUM + j * ETHTOOL_RXSTATS_NUM + i] =
				(et_rx_stats[i].size == sizeof(u64)) ?
					*(u64 *)p: *(u32 *)p;
		}
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
			adapter->num_rx_qs * ETHTOOL_RXSTATS_NUM;
	default:
		return -EINVAL;
	}
}

static int be_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_dma_mem phy_cmd;
	struct be_cmd_resp_get_phy_info *resp;
	u8 mac_speed = 0;
	u16 link_speed = 0;
	bool link_up = false;
	int status;
	u16 intf_type;

	if ((adapter->link_speed < 0) || (!(netdev->flags & IFF_UP))) {
		status = be_cmd_link_status_query(adapter, &link_up,
						&mac_speed, &link_speed);

		be_link_status_update(adapter, link_up);
		/* link_speed is in units of 10 Mbps */
		if (link_speed) {
			ecmd->speed = link_speed*10;
		} else {
			switch (mac_speed) {
			case PHY_LINK_SPEED_1GBPS:
				ecmd->speed = SPEED_1000;
				break;
			case PHY_LINK_SPEED_10GBPS:
				ecmd->speed = SPEED_10000;
				break;
			}
		}

		phy_cmd.size = sizeof(struct be_cmd_req_get_phy_info);
		phy_cmd.va = dma_alloc_coherent(&adapter->pdev->dev,
						phy_cmd.size, &phy_cmd.dma,
						GFP_KERNEL);
		if (!phy_cmd.va) {
			dev_err(&adapter->pdev->dev, "Memory alloc failure\n");
			return -ENOMEM;
		}
		status = be_cmd_get_phy_info(adapter, &phy_cmd);
		if (!status) {
			resp = (struct be_cmd_resp_get_phy_info *) phy_cmd.va;
			intf_type = le16_to_cpu(resp->interface_type);

			switch (intf_type) {
			case PHY_TYPE_XFP_10GB:
			case PHY_TYPE_SFP_1GB:
			case PHY_TYPE_SFP_PLUS_10GB:
				ecmd->port = PORT_FIBRE;
				break;
			default:
				ecmd->port = PORT_TP;
				break;
			}

			switch (intf_type) {
			case PHY_TYPE_KR_10GB:
			case PHY_TYPE_KX4_10GB:
				ecmd->autoneg = AUTONEG_ENABLE;
			ecmd->transceiver = XCVR_INTERNAL;
				break;
			default:
				ecmd->autoneg = AUTONEG_DISABLE;
				ecmd->transceiver = XCVR_EXTERNAL;
				break;
			}
		}

		/* Save for future use */
		adapter->link_speed = ecmd->speed;
		adapter->port_type = ecmd->port;
		adapter->transceiver = ecmd->transceiver;
		adapter->autoneg = ecmd->autoneg;
		dma_free_coherent(&adapter->pdev->dev, phy_cmd.size, phy_cmd.va,
				  phy_cmd.dma);
	} else {
		ecmd->speed = adapter->link_speed;
		ecmd->port = adapter->port_type;
		ecmd->transceiver = adapter->transceiver;
		ecmd->autoneg = adapter->autoneg;
	}

	ecmd->duplex = DUPLEX_FULL;
	ecmd->phy_address = adapter->port_num;
	switch (ecmd->port) {
	case PORT_FIBRE:
		ecmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
		break;
	case PORT_TP:
		ecmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_TP);
		break;
	case PORT_AUI:
		ecmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_AUI);
		break;
	}

	if (ecmd->autoneg) {
		ecmd->supported |= SUPPORTED_1000baseT_Full;
		ecmd->supported |= SUPPORTED_Autoneg;
		ecmd->advertising |= (ADVERTISED_10000baseT_Full |
				ADVERTISED_1000baseT_Full);
	}

	return 0;
}

static void
be_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = adapter->rx_obj[0].q.len;
	ring->tx_max_pending = adapter->tx_obj.q.len;

	ring->rx_pending = atomic_read(&adapter->rx_obj[0].q.used);
	ring->tx_pending = atomic_read(&adapter->tx_obj.q.used);
}

static void
be_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *ecmd)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	be_cmd_get_flow_control(adapter, &ecmd->tx_pause, &ecmd->rx_pause);
	ecmd->autoneg = 0;
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
be_phys_id(struct net_device *netdev, u32 data)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status;
	u32 cur;

	be_cmd_get_beacon_state(adapter, adapter->hba_port_num, &cur);

	if (cur == BEACON_STATE_ENABLED)
		return 0;

	if (data < 2)
		data = 2;

	status = be_cmd_set_beacon_state(adapter, adapter->hba_port_num, 0, 0,
			BEACON_STATE_ENABLED);
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(data*HZ);

	status = be_cmd_set_beacon_state(adapter, adapter->hba_port_num, 0, 0,
			BEACON_STATE_DISABLED);

	return status;
}

static bool
be_is_wol_supported(struct be_adapter *adapter)
{
	if (!be_physfn(adapter))
		return false;
	else
		return true;
}

static void
be_get_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (be_is_wol_supported(adapter))
		wol->supported = WAKE_MAGIC;

	if (adapter->wol)
		wol->wolopts = WAKE_MAGIC;
	else
		wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int
be_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	if ((wol->wolopts & WAKE_MAGIC) && be_is_wol_supported(adapter))
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
	bool link_up;
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

	if (be_test_ddr_dma(adapter) != 0) {
		data[3] = 1;
		test->flags |= ETH_TEST_FL_FAILED;
	}

	if (be_cmd_link_status_query(adapter, &link_up, &mac_speed,
				&qos_link_speed) != 0) {
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
	char file_name[ETHTOOL_FLASH_MAX_FILENAME];
	u32 region;

	file_name[ETHTOOL_FLASH_MAX_FILENAME - 1] = 0;
	strcpy(file_name, efl->data);
	region = efl->region;

	return be_load_fw(adapter, file_name);
}

static int
be_get_eeprom_len(struct net_device *netdev)
{
	return BE_READ_SEEPROM_LEN;
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
		resp = (struct be_cmd_resp_seeprom_read *) eeprom_cmd.va;
		memcpy(data, resp->seeprom_data + eeprom->offset, eeprom->len);
	}
	dma_free_coherent(&adapter->pdev->dev, eeprom_cmd.size, eeprom_cmd.va,
			  eeprom_cmd.dma);

	return status;
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
	.get_rx_csum = be_get_rx_csum,
	.set_rx_csum = be_set_rx_csum,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_hw_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
	.get_strings = be_get_stat_strings,
	.phys_id = be_phys_id,
	.get_sset_count = be_get_sset_count,
	.get_ethtool_stats = be_get_ethtool_stats,
	.flash_device = be_do_flash,
	.self_test = be_self_test,
};
