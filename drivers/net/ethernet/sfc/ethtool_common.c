// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include "net_driver.h"
#include "mcdi.h"
#include "nic.h"
#include "selftest.h"
#include "ethtool_common.h"

struct efx_sw_stat_desc {
	const char *name;
	enum {
		EFX_ETHTOOL_STAT_SOURCE_nic,
		EFX_ETHTOOL_STAT_SOURCE_channel,
		EFX_ETHTOOL_STAT_SOURCE_tx_queue
	} source;
	unsigned int offset;
	u64 (*get_stat)(void *field); /* Reader function */
};

/* Initialiser for a struct efx_sw_stat_desc with type-checking */
#define EFX_ETHTOOL_STAT(stat_name, source_name, field, field_type, \
				get_stat_function) {			\
	.name = #stat_name,						\
	.source = EFX_ETHTOOL_STAT_SOURCE_##source_name,		\
	.offset = ((((field_type *) 0) ==				\
		      &((struct efx_##source_name *)0)->field) ?	\
		    offsetof(struct efx_##source_name, field) :		\
		    offsetof(struct efx_##source_name, field)),		\
	.get_stat = get_stat_function,					\
}

static u64 efx_get_uint_stat(void *field)
{
	return *(unsigned int *)field;
}

static u64 efx_get_atomic_stat(void *field)
{
	return atomic_read((atomic_t *) field);
}

#define EFX_ETHTOOL_ATOMIC_NIC_ERROR_STAT(field)		\
	EFX_ETHTOOL_STAT(field, nic, field,			\
			 atomic_t, efx_get_atomic_stat)

#define EFX_ETHTOOL_UINT_CHANNEL_STAT(field)			\
	EFX_ETHTOOL_STAT(field, channel, n_##field,		\
			 unsigned int, efx_get_uint_stat)
#define EFX_ETHTOOL_UINT_CHANNEL_STAT_NO_N(field)		\
	EFX_ETHTOOL_STAT(field, channel, field,			\
			 unsigned int, efx_get_uint_stat)

#define EFX_ETHTOOL_UINT_TXQ_STAT(field)			\
	EFX_ETHTOOL_STAT(tx_##field, tx_queue, field,		\
			 unsigned int, efx_get_uint_stat)

static const struct efx_sw_stat_desc efx_sw_stat_desc[] = {
	EFX_ETHTOOL_UINT_TXQ_STAT(merge_events),
	EFX_ETHTOOL_UINT_TXQ_STAT(tso_bursts),
	EFX_ETHTOOL_UINT_TXQ_STAT(tso_long_headers),
	EFX_ETHTOOL_UINT_TXQ_STAT(tso_packets),
	EFX_ETHTOOL_UINT_TXQ_STAT(tso_fallbacks),
	EFX_ETHTOOL_UINT_TXQ_STAT(pushes),
	EFX_ETHTOOL_UINT_TXQ_STAT(pio_packets),
	EFX_ETHTOOL_UINT_TXQ_STAT(cb_packets),
	EFX_ETHTOOL_ATOMIC_NIC_ERROR_STAT(rx_reset),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_tobe_disc),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_ip_hdr_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_tcp_udp_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_inner_ip_hdr_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_inner_tcp_udp_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_outer_ip_hdr_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_outer_tcp_udp_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_eth_crc_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_mcast_mismatch),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_frm_trunc),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_merge_events),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_merge_packets),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_xdp_drops),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_xdp_bad_drops),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_xdp_tx),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_xdp_redirect),
#ifdef CONFIG_RFS_ACCEL
	EFX_ETHTOOL_UINT_CHANNEL_STAT_NO_N(rfs_filter_count),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rfs_succeeded),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rfs_failed),
#endif
};

#define EFX_ETHTOOL_SW_STAT_COUNT ARRAY_SIZE(efx_sw_stat_desc)

void efx_ethtool_get_drvinfo(struct net_device *net_dev,
			     struct ethtool_drvinfo *info)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, EFX_DRIVER_VERSION, sizeof(info->version));
	efx_mcdi_print_fwver(efx, info->fw_version,
			     sizeof(info->fw_version));
	strlcpy(info->bus_info, pci_name(efx->pci_dev), sizeof(info->bus_info));
}

u32 efx_ethtool_get_msglevel(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	return efx->msg_enable;
}

void efx_ethtool_set_msglevel(struct net_device *net_dev, u32 msg_enable)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	efx->msg_enable = msg_enable;
}

void efx_ethtool_get_pauseparam(struct net_device *net_dev,
				struct ethtool_pauseparam *pause)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	pause->rx_pause = !!(efx->wanted_fc & EFX_FC_RX);
	pause->tx_pause = !!(efx->wanted_fc & EFX_FC_TX);
	pause->autoneg = !!(efx->wanted_fc & EFX_FC_AUTO);
}

/**
 * efx_fill_test - fill in an individual self-test entry
 * @test_index:		Index of the test
 * @strings:		Ethtool strings, or %NULL
 * @data:		Ethtool test results, or %NULL
 * @test:		Pointer to test result (used only if data != %NULL)
 * @unit_format:	Unit name format (e.g. "chan\%d")
 * @unit_id:		Unit id (e.g. 0 for "chan0")
 * @test_format:	Test name format (e.g. "loopback.\%s.tx.sent")
 * @test_id:		Test id (e.g. "PHYXS" for "loopback.PHYXS.tx_sent")
 *
 * Fill in an individual self-test entry.
 */
static void efx_fill_test(unsigned int test_index, u8 *strings, u64 *data,
			  int *test, const char *unit_format, int unit_id,
			  const char *test_format, const char *test_id)
{
	char unit_str[ETH_GSTRING_LEN], test_str[ETH_GSTRING_LEN];

	/* Fill data value, if applicable */
	if (data)
		data[test_index] = *test;

	/* Fill string, if applicable */
	if (strings) {
		if (strchr(unit_format, '%'))
			snprintf(unit_str, sizeof(unit_str),
				 unit_format, unit_id);
		else
			strcpy(unit_str, unit_format);
		snprintf(test_str, sizeof(test_str), test_format, test_id);
		snprintf(strings + test_index * ETH_GSTRING_LEN,
			 ETH_GSTRING_LEN,
			 "%-6s %-24s", unit_str, test_str);
	}
}

#define EFX_CHANNEL_NAME(_channel) "chan%d", _channel->channel
#define EFX_TX_QUEUE_NAME(_tx_queue) "txq%d", _tx_queue->queue
#define EFX_RX_QUEUE_NAME(_rx_queue) "rxq%d", _rx_queue->queue
#define EFX_LOOPBACK_NAME(_mode, _counter)			\
	"loopback.%s." _counter, STRING_TABLE_LOOKUP(_mode, efx_loopback_mode)

/**
 * efx_fill_loopback_test - fill in a block of loopback self-test entries
 * @efx:		Efx NIC
 * @lb_tests:		Efx loopback self-test results structure
 * @mode:		Loopback test mode
 * @test_index:		Starting index of the test
 * @strings:		Ethtool strings, or %NULL
 * @data:		Ethtool test results, or %NULL
 *
 * Fill in a block of loopback self-test entries.  Return new test
 * index.
 */
static int efx_fill_loopback_test(struct efx_nic *efx,
				  struct efx_loopback_self_tests *lb_tests,
				  enum efx_loopback_mode mode,
				  unsigned int test_index,
				  u8 *strings, u64 *data)
{
	struct efx_channel *channel =
		efx_get_channel(efx, efx->tx_channel_offset);
	struct efx_tx_queue *tx_queue;

	efx_for_each_channel_tx_queue(tx_queue, channel) {
		efx_fill_test(test_index++, strings, data,
			      &lb_tests->tx_sent[tx_queue->queue],
			      EFX_TX_QUEUE_NAME(tx_queue),
			      EFX_LOOPBACK_NAME(mode, "tx_sent"));
		efx_fill_test(test_index++, strings, data,
			      &lb_tests->tx_done[tx_queue->queue],
			      EFX_TX_QUEUE_NAME(tx_queue),
			      EFX_LOOPBACK_NAME(mode, "tx_done"));
	}
	efx_fill_test(test_index++, strings, data,
		      &lb_tests->rx_good,
		      "rx", 0,
		      EFX_LOOPBACK_NAME(mode, "rx_good"));
	efx_fill_test(test_index++, strings, data,
		      &lb_tests->rx_bad,
		      "rx", 0,
		      EFX_LOOPBACK_NAME(mode, "rx_bad"));

	return test_index;
}

/**
 * efx_ethtool_fill_self_tests - get self-test details
 * @efx:		Efx NIC
 * @tests:		Efx self-test results structure, or %NULL
 * @strings:		Ethtool strings, or %NULL
 * @data:		Ethtool test results, or %NULL
 *
 * Get self-test number of strings, strings, and/or test results.
 * Return number of strings (== number of test results).
 *
 * The reason for merging these three functions is to make sure that
 * they can never be inconsistent.
 */
int efx_ethtool_fill_self_tests(struct efx_nic *efx,
				struct efx_self_tests *tests,
				u8 *strings, u64 *data)
{
	struct efx_channel *channel;
	unsigned int n = 0, i;
	enum efx_loopback_mode mode;

	efx_fill_test(n++, strings, data, &tests->phy_alive,
		      "phy", 0, "alive", NULL);
	efx_fill_test(n++, strings, data, &tests->nvram,
		      "core", 0, "nvram", NULL);
	efx_fill_test(n++, strings, data, &tests->interrupt,
		      "core", 0, "interrupt", NULL);

	/* Event queues */
	efx_for_each_channel(channel, efx) {
		efx_fill_test(n++, strings, data,
			      &tests->eventq_dma[channel->channel],
			      EFX_CHANNEL_NAME(channel),
			      "eventq.dma", NULL);
		efx_fill_test(n++, strings, data,
			      &tests->eventq_int[channel->channel],
			      EFX_CHANNEL_NAME(channel),
			      "eventq.int", NULL);
	}

	efx_fill_test(n++, strings, data, &tests->memory,
		      "core", 0, "memory", NULL);
	efx_fill_test(n++, strings, data, &tests->registers,
		      "core", 0, "registers", NULL);

	if (efx->phy_op->run_tests != NULL) {
		EFX_WARN_ON_PARANOID(efx->phy_op->test_name == NULL);

		for (i = 0; true; ++i) {
			const char *name;

			EFX_WARN_ON_PARANOID(i >= EFX_MAX_PHY_TESTS);
			name = efx->phy_op->test_name(efx, i);
			if (name == NULL)
				break;

			efx_fill_test(n++, strings, data, &tests->phy_ext[i],
				      "phy", 0, name, NULL);
		}
	}

	/* Loopback tests */
	for (mode = LOOPBACK_NONE; mode <= LOOPBACK_TEST_MAX; mode++) {
		if (!(efx->loopback_modes & (1 << mode)))
			continue;
		n = efx_fill_loopback_test(efx,
					   &tests->loopback[mode], mode, n,
					   strings, data);
	}

	return n;
}

static size_t efx_describe_per_queue_stats(struct efx_nic *efx, u8 *strings)
{
	size_t n_stats = 0;
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx) {
		if (efx_channel_has_tx_queues(channel)) {
			n_stats++;
			if (strings != NULL) {
				snprintf(strings, ETH_GSTRING_LEN,
					 "tx-%u.tx_packets",
					 channel->tx_queue[0].queue /
					 EFX_TXQ_TYPES);

				strings += ETH_GSTRING_LEN;
			}
		}
	}
	efx_for_each_channel(channel, efx) {
		if (efx_channel_has_rx_queue(channel)) {
			n_stats++;
			if (strings != NULL) {
				snprintf(strings, ETH_GSTRING_LEN,
					 "rx-%d.rx_packets", channel->channel);
				strings += ETH_GSTRING_LEN;
			}
		}
	}
	if (efx->xdp_tx_queue_count && efx->xdp_tx_queues) {
		unsigned short xdp;

		for (xdp = 0; xdp < efx->xdp_tx_queue_count; xdp++) {
			n_stats++;
			if (strings) {
				snprintf(strings, ETH_GSTRING_LEN,
					 "tx-xdp-cpu-%hu.tx_packets", xdp);
				strings += ETH_GSTRING_LEN;
			}
		}
	}

	return n_stats;
}

int efx_ethtool_get_sset_count(struct net_device *net_dev, int string_set)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	switch (string_set) {
	case ETH_SS_STATS:
		return efx->type->describe_stats(efx, NULL) +
		       EFX_ETHTOOL_SW_STAT_COUNT +
		       efx_describe_per_queue_stats(efx, NULL) +
		       efx_ptp_describe_stats(efx, NULL);
	case ETH_SS_TEST:
		return efx_ethtool_fill_self_tests(efx, NULL, NULL, NULL);
	default:
		return -EINVAL;
	}
}

void efx_ethtool_get_strings(struct net_device *net_dev,
			     u32 string_set, u8 *strings)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int i;

	switch (string_set) {
	case ETH_SS_STATS:
		strings += (efx->type->describe_stats(efx, strings) *
			    ETH_GSTRING_LEN);
		for (i = 0; i < EFX_ETHTOOL_SW_STAT_COUNT; i++)
			strlcpy(strings + i * ETH_GSTRING_LEN,
				efx_sw_stat_desc[i].name, ETH_GSTRING_LEN);
		strings += EFX_ETHTOOL_SW_STAT_COUNT * ETH_GSTRING_LEN;
		strings += (efx_describe_per_queue_stats(efx, strings) *
			    ETH_GSTRING_LEN);
		efx_ptp_describe_stats(efx, strings);
		break;
	case ETH_SS_TEST:
		efx_ethtool_fill_self_tests(efx, NULL, strings, NULL);
		break;
	default:
		/* No other string sets */
		break;
	}
}

void efx_ethtool_get_stats(struct net_device *net_dev,
			   struct ethtool_stats *stats,
			   u64 *data)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	const struct efx_sw_stat_desc *stat;
	struct efx_channel *channel;
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	int i;

	spin_lock_bh(&efx->stats_lock);

	/* Get NIC statistics */
	data += efx->type->update_stats(efx, data, NULL);

	/* Get software statistics */
	for (i = 0; i < EFX_ETHTOOL_SW_STAT_COUNT; i++) {
		stat = &efx_sw_stat_desc[i];
		switch (stat->source) {
		case EFX_ETHTOOL_STAT_SOURCE_nic:
			data[i] = stat->get_stat((void *)efx + stat->offset);
			break;
		case EFX_ETHTOOL_STAT_SOURCE_channel:
			data[i] = 0;
			efx_for_each_channel(channel, efx)
				data[i] += stat->get_stat((void *)channel +
							  stat->offset);
			break;
		case EFX_ETHTOOL_STAT_SOURCE_tx_queue:
			data[i] = 0;
			efx_for_each_channel(channel, efx) {
				efx_for_each_channel_tx_queue(tx_queue, channel)
					data[i] +=
						stat->get_stat((void *)tx_queue
							       + stat->offset);
			}
			break;
		}
	}
	data += EFX_ETHTOOL_SW_STAT_COUNT;

	spin_unlock_bh(&efx->stats_lock);

	efx_for_each_channel(channel, efx) {
		if (efx_channel_has_tx_queues(channel)) {
			*data = 0;
			efx_for_each_channel_tx_queue(tx_queue, channel) {
				*data += tx_queue->tx_packets;
			}
			data++;
		}
	}
	efx_for_each_channel(channel, efx) {
		if (efx_channel_has_rx_queue(channel)) {
			*data = 0;
			efx_for_each_channel_rx_queue(rx_queue, channel) {
				*data += rx_queue->rx_packets;
			}
			data++;
		}
	}
	if (efx->xdp_tx_queue_count && efx->xdp_tx_queues) {
		int xdp;

		for (xdp = 0; xdp < efx->xdp_tx_queue_count; xdp++) {
			data[0] = efx->xdp_tx_queues[xdp]->tx_packets;
			data++;
		}
	}

	efx_ptp_update_stats(efx, data);
}
