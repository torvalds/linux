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
#include "rx_common.h"
#include "ethtool_common.h"
#include "mcdi_port_common.h"

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
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_ip_hdr_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_tcp_udp_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_inner_ip_hdr_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_inner_tcp_udp_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_outer_ip_hdr_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_outer_tcp_udp_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_eth_crc_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_frm_trunc),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_overlength),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_merge_events),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_merge_packets),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_xdp_drops),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_xdp_bad_drops),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_xdp_tx),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_xdp_redirect),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_mport_bad),
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
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	strscpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	efx_mcdi_print_fwver(efx, info->fw_version,
			     sizeof(info->fw_version));
	strscpy(info->bus_info, pci_name(efx->pci_dev), sizeof(info->bus_info));
}

u32 efx_ethtool_get_msglevel(struct net_device *net_dev)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	return efx->msg_enable;
}

void efx_ethtool_set_msglevel(struct net_device *net_dev, u32 msg_enable)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	efx->msg_enable = msg_enable;
}

void efx_ethtool_self_test(struct net_device *net_dev,
			   struct ethtool_test *test, u64 *data)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	struct efx_self_tests *efx_tests;
	bool already_up;
	int rc = -ENOMEM;

	efx_tests = kzalloc(sizeof(*efx_tests), GFP_KERNEL);
	if (!efx_tests)
		goto fail;

	if (!efx_net_active(efx->state)) {
		rc = -EBUSY;
		goto out;
	}

	netif_info(efx, drv, efx->net_dev, "starting %sline testing\n",
		   (test->flags & ETH_TEST_FL_OFFLINE) ? "off" : "on");

	/* We need rx buffers and interrupts. */
	already_up = (efx->net_dev->flags & IFF_UP);
	if (!already_up) {
		rc = dev_open(efx->net_dev, NULL);
		if (rc) {
			netif_err(efx, drv, efx->net_dev,
				  "failed opening device.\n");
			goto out;
		}
	}

	rc = efx_selftest(efx, efx_tests, test->flags);

	if (!already_up)
		dev_close(efx->net_dev);

	netif_info(efx, drv, efx->net_dev, "%s %sline self-tests\n",
		   rc == 0 ? "passed" : "failed",
		   (test->flags & ETH_TEST_FL_OFFLINE) ? "off" : "on");

out:
	efx_ethtool_fill_self_tests(efx, efx_tests, NULL, data);
	kfree(efx_tests);
fail:
	if (rc)
		test->flags |= ETH_TEST_FL_FAILED;
}

void efx_ethtool_get_pauseparam(struct net_device *net_dev,
				struct ethtool_pauseparam *pause)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	pause->rx_pause = !!(efx->wanted_fc & EFX_FC_RX);
	pause->tx_pause = !!(efx->wanted_fc & EFX_FC_TX);
	pause->autoneg = !!(efx->wanted_fc & EFX_FC_AUTO);
}

int efx_ethtool_set_pauseparam(struct net_device *net_dev,
			       struct ethtool_pauseparam *pause)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	u8 wanted_fc, old_fc;
	u32 old_adv;
	int rc = 0;

	mutex_lock(&efx->mac_lock);

	wanted_fc = ((pause->rx_pause ? EFX_FC_RX : 0) |
		     (pause->tx_pause ? EFX_FC_TX : 0) |
		     (pause->autoneg ? EFX_FC_AUTO : 0));

	if ((wanted_fc & EFX_FC_TX) && !(wanted_fc & EFX_FC_RX)) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Flow control unsupported: tx ON rx OFF\n");
		rc = -EINVAL;
		goto out;
	}

	if ((wanted_fc & EFX_FC_AUTO) && !efx->link_advertising[0]) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Autonegotiation is disabled\n");
		rc = -EINVAL;
		goto out;
	}

	/* Hook for Falcon bug 11482 workaround */
	if (efx->type->prepare_enable_fc_tx &&
	    (wanted_fc & EFX_FC_TX) && !(efx->wanted_fc & EFX_FC_TX))
		efx->type->prepare_enable_fc_tx(efx);

	old_adv = efx->link_advertising[0];
	old_fc = efx->wanted_fc;
	efx_link_set_wanted_fc(efx, wanted_fc);
	if (efx->link_advertising[0] != old_adv ||
	    (efx->wanted_fc ^ old_fc) & EFX_FC_AUTO) {
		rc = efx_mcdi_port_reconfigure(efx);
		if (rc) {
			netif_err(efx, drv, efx->net_dev,
				  "Unable to advertise requested flow "
				  "control setting\n");
			goto out;
		}
	}

	/* Reconfigure the MAC. The PHY *may* generate a link state change event
	 * if the user just changed the advertised capabilities, but there's no
	 * harm doing this twice */
	efx_mac_reconfigure(efx, false);

out:
	mutex_unlock(&efx->mac_lock);

	return rc;
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
#define EFX_TX_QUEUE_NAME(_tx_queue) "txq%d", _tx_queue->label
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
			      &lb_tests->tx_sent[tx_queue->label],
			      EFX_TX_QUEUE_NAME(tx_queue),
			      EFX_LOOPBACK_NAME(mode, "tx_sent"));
		efx_fill_test(test_index++, strings, data,
			      &lb_tests->tx_done[tx_queue->label],
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

	for (i = 0; true; ++i) {
		const char *name;

		EFX_WARN_ON_PARANOID(i >= EFX_MAX_PHY_TESTS);
		name = efx_mcdi_phy_test_name(efx, i);
		if (name == NULL)
			break;

		efx_fill_test(n++, strings, data, &tests->phy_ext[i], "phy", 0, name, NULL);
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

static size_t efx_describe_per_queue_stats(struct efx_nic *efx, u8 **strings)
{
	size_t n_stats = 0;
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx) {
		if (efx_channel_has_tx_queues(channel)) {
			n_stats++;
			if (!strings)
				continue;

			ethtool_sprintf(strings, "tx-%u.tx_packets",
					channel->tx_queue[0].queue /
						EFX_MAX_TXQ_PER_CHANNEL);
		}
	}
	efx_for_each_channel(channel, efx) {
		if (efx_channel_has_rx_queue(channel)) {
			n_stats++;
			if (!strings)
				continue;

			ethtool_sprintf(strings, "rx-%d.rx_packets",
					channel->channel);
		}
	}
	if (efx->xdp_tx_queue_count && efx->xdp_tx_queues) {
		unsigned short xdp;

		for (xdp = 0; xdp < efx->xdp_tx_queue_count; xdp++) {
			n_stats++;
			if (!strings)
				continue;

			ethtool_sprintf(strings, "tx-xdp-cpu-%hu.tx_packets",
					xdp);
		}
	}

	return n_stats;
}

int efx_ethtool_get_sset_count(struct net_device *net_dev, int string_set)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

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
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int i;

	switch (string_set) {
	case ETH_SS_STATS:
		efx->type->describe_stats(efx, &strings);
		for (i = 0; i < EFX_ETHTOOL_SW_STAT_COUNT; i++)
			ethtool_puts(&strings, efx_sw_stat_desc[i].name);
		efx_describe_per_queue_stats(efx, &strings);
		efx_ptp_describe_stats(efx, &strings);
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
	struct efx_nic *efx = efx_netdev_priv(net_dev);
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

/* This must be called with rtnl_lock held. */
int efx_ethtool_get_link_ksettings(struct net_device *net_dev,
				   struct ethtool_link_ksettings *cmd)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	struct efx_link_state *link_state = &efx->link_state;

	mutex_lock(&efx->mac_lock);
	efx_mcdi_phy_get_link_ksettings(efx, cmd);
	mutex_unlock(&efx->mac_lock);

	/* Both MACs support pause frames (bidirectional and respond-only) */
	ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Asym_Pause);

	if (LOOPBACK_INTERNAL(efx)) {
		cmd->base.speed = link_state->speed;
		cmd->base.duplex = link_state->fd ? DUPLEX_FULL : DUPLEX_HALF;
	}

	return 0;
}

/* This must be called with rtnl_lock held. */
int efx_ethtool_set_link_ksettings(struct net_device *net_dev,
				   const struct ethtool_link_ksettings *cmd)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int rc;

	/* GMAC does not support 1000Mbps HD */
	if ((cmd->base.speed == SPEED_1000) &&
	    (cmd->base.duplex != DUPLEX_FULL)) {
		netif_dbg(efx, drv, efx->net_dev,
			  "rejecting unsupported 1000Mbps HD setting\n");
		return -EINVAL;
	}

	mutex_lock(&efx->mac_lock);
	rc = efx_mcdi_phy_set_link_ksettings(efx, cmd);
	mutex_unlock(&efx->mac_lock);
	return rc;
}

int efx_ethtool_get_fecparam(struct net_device *net_dev,
			     struct ethtool_fecparam *fecparam)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int rc;

	mutex_lock(&efx->mac_lock);
	rc = efx_mcdi_phy_get_fecparam(efx, fecparam);
	mutex_unlock(&efx->mac_lock);

	return rc;
}

int efx_ethtool_set_fecparam(struct net_device *net_dev,
			     struct ethtool_fecparam *fecparam)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int rc;

	mutex_lock(&efx->mac_lock);
	rc = efx_mcdi_phy_set_fecparam(efx, fecparam);
	mutex_unlock(&efx->mac_lock);

	return rc;
}

/* MAC address mask including only I/G bit */
static const u8 mac_addr_ig_mask[ETH_ALEN] __aligned(2) = {0x01, 0, 0, 0, 0, 0};

#define IP4_ADDR_FULL_MASK	((__force __be32)~0)
#define IP_PROTO_FULL_MASK	0xFF
#define PORT_FULL_MASK		((__force __be16)~0)
#define ETHER_TYPE_FULL_MASK	((__force __be16)~0)

static inline void ip6_fill_mask(__be32 *mask)
{
	mask[0] = mask[1] = mask[2] = mask[3] = ~(__be32)0;
}

static int efx_ethtool_get_class_rule(struct efx_nic *efx,
				      struct ethtool_rx_flow_spec *rule,
				      u32 *rss_context)
{
	struct ethtool_tcpip4_spec *ip_entry = &rule->h_u.tcp_ip4_spec;
	struct ethtool_tcpip4_spec *ip_mask = &rule->m_u.tcp_ip4_spec;
	struct ethtool_usrip4_spec *uip_entry = &rule->h_u.usr_ip4_spec;
	struct ethtool_usrip4_spec *uip_mask = &rule->m_u.usr_ip4_spec;
	struct ethtool_tcpip6_spec *ip6_entry = &rule->h_u.tcp_ip6_spec;
	struct ethtool_tcpip6_spec *ip6_mask = &rule->m_u.tcp_ip6_spec;
	struct ethtool_usrip6_spec *uip6_entry = &rule->h_u.usr_ip6_spec;
	struct ethtool_usrip6_spec *uip6_mask = &rule->m_u.usr_ip6_spec;
	struct ethhdr *mac_entry = &rule->h_u.ether_spec;
	struct ethhdr *mac_mask = &rule->m_u.ether_spec;
	struct efx_filter_spec spec;
	int rc;

	rc = efx_filter_get_filter_safe(efx, EFX_FILTER_PRI_MANUAL,
					rule->location, &spec);
	if (rc)
		return rc;

	if (spec.dmaq_id == EFX_FILTER_RX_DMAQ_ID_DROP)
		rule->ring_cookie = RX_CLS_FLOW_DISC;
	else
		rule->ring_cookie = spec.dmaq_id;

	if ((spec.match_flags & EFX_FILTER_MATCH_ETHER_TYPE) &&
	    spec.ether_type == htons(ETH_P_IP) &&
	    (spec.match_flags & EFX_FILTER_MATCH_IP_PROTO) &&
	    (spec.ip_proto == IPPROTO_TCP || spec.ip_proto == IPPROTO_UDP) &&
	    !(spec.match_flags &
	      ~(EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_OUTER_VID |
		EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_REM_HOST |
		EFX_FILTER_MATCH_IP_PROTO |
		EFX_FILTER_MATCH_LOC_PORT | EFX_FILTER_MATCH_REM_PORT))) {
		rule->flow_type = ((spec.ip_proto == IPPROTO_TCP) ?
				   TCP_V4_FLOW : UDP_V4_FLOW);
		if (spec.match_flags & EFX_FILTER_MATCH_LOC_HOST) {
			ip_entry->ip4dst = spec.loc_host[0];
			ip_mask->ip4dst = IP4_ADDR_FULL_MASK;
		}
		if (spec.match_flags & EFX_FILTER_MATCH_REM_HOST) {
			ip_entry->ip4src = spec.rem_host[0];
			ip_mask->ip4src = IP4_ADDR_FULL_MASK;
		}
		if (spec.match_flags & EFX_FILTER_MATCH_LOC_PORT) {
			ip_entry->pdst = spec.loc_port;
			ip_mask->pdst = PORT_FULL_MASK;
		}
		if (spec.match_flags & EFX_FILTER_MATCH_REM_PORT) {
			ip_entry->psrc = spec.rem_port;
			ip_mask->psrc = PORT_FULL_MASK;
		}
	} else if ((spec.match_flags & EFX_FILTER_MATCH_ETHER_TYPE) &&
	    spec.ether_type == htons(ETH_P_IPV6) &&
	    (spec.match_flags & EFX_FILTER_MATCH_IP_PROTO) &&
	    (spec.ip_proto == IPPROTO_TCP || spec.ip_proto == IPPROTO_UDP) &&
	    !(spec.match_flags &
	      ~(EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_OUTER_VID |
		EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_REM_HOST |
		EFX_FILTER_MATCH_IP_PROTO |
		EFX_FILTER_MATCH_LOC_PORT | EFX_FILTER_MATCH_REM_PORT))) {
		rule->flow_type = ((spec.ip_proto == IPPROTO_TCP) ?
				   TCP_V6_FLOW : UDP_V6_FLOW);
		if (spec.match_flags & EFX_FILTER_MATCH_LOC_HOST) {
			memcpy(ip6_entry->ip6dst, spec.loc_host,
			       sizeof(ip6_entry->ip6dst));
			ip6_fill_mask(ip6_mask->ip6dst);
		}
		if (spec.match_flags & EFX_FILTER_MATCH_REM_HOST) {
			memcpy(ip6_entry->ip6src, spec.rem_host,
			       sizeof(ip6_entry->ip6src));
			ip6_fill_mask(ip6_mask->ip6src);
		}
		if (spec.match_flags & EFX_FILTER_MATCH_LOC_PORT) {
			ip6_entry->pdst = spec.loc_port;
			ip6_mask->pdst = PORT_FULL_MASK;
		}
		if (spec.match_flags & EFX_FILTER_MATCH_REM_PORT) {
			ip6_entry->psrc = spec.rem_port;
			ip6_mask->psrc = PORT_FULL_MASK;
		}
	} else if (!(spec.match_flags &
		     ~(EFX_FILTER_MATCH_LOC_MAC | EFX_FILTER_MATCH_LOC_MAC_IG |
		       EFX_FILTER_MATCH_REM_MAC | EFX_FILTER_MATCH_ETHER_TYPE |
		       EFX_FILTER_MATCH_OUTER_VID))) {
		rule->flow_type = ETHER_FLOW;
		if (spec.match_flags &
		    (EFX_FILTER_MATCH_LOC_MAC | EFX_FILTER_MATCH_LOC_MAC_IG)) {
			ether_addr_copy(mac_entry->h_dest, spec.loc_mac);
			if (spec.match_flags & EFX_FILTER_MATCH_LOC_MAC)
				eth_broadcast_addr(mac_mask->h_dest);
			else
				ether_addr_copy(mac_mask->h_dest,
						mac_addr_ig_mask);
		}
		if (spec.match_flags & EFX_FILTER_MATCH_REM_MAC) {
			ether_addr_copy(mac_entry->h_source, spec.rem_mac);
			eth_broadcast_addr(mac_mask->h_source);
		}
		if (spec.match_flags & EFX_FILTER_MATCH_ETHER_TYPE) {
			mac_entry->h_proto = spec.ether_type;
			mac_mask->h_proto = ETHER_TYPE_FULL_MASK;
		}
	} else if (spec.match_flags & EFX_FILTER_MATCH_ETHER_TYPE &&
		   spec.ether_type == htons(ETH_P_IP) &&
		   !(spec.match_flags &
		     ~(EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_OUTER_VID |
		       EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_REM_HOST |
		       EFX_FILTER_MATCH_IP_PROTO))) {
		rule->flow_type = IPV4_USER_FLOW;
		uip_entry->ip_ver = ETH_RX_NFC_IP4;
		if (spec.match_flags & EFX_FILTER_MATCH_IP_PROTO) {
			uip_mask->proto = IP_PROTO_FULL_MASK;
			uip_entry->proto = spec.ip_proto;
		}
		if (spec.match_flags & EFX_FILTER_MATCH_LOC_HOST) {
			uip_entry->ip4dst = spec.loc_host[0];
			uip_mask->ip4dst = IP4_ADDR_FULL_MASK;
		}
		if (spec.match_flags & EFX_FILTER_MATCH_REM_HOST) {
			uip_entry->ip4src = spec.rem_host[0];
			uip_mask->ip4src = IP4_ADDR_FULL_MASK;
		}
	} else if (spec.match_flags & EFX_FILTER_MATCH_ETHER_TYPE &&
		   spec.ether_type == htons(ETH_P_IPV6) &&
		   !(spec.match_flags &
		     ~(EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_OUTER_VID |
		       EFX_FILTER_MATCH_LOC_HOST | EFX_FILTER_MATCH_REM_HOST |
		       EFX_FILTER_MATCH_IP_PROTO))) {
		rule->flow_type = IPV6_USER_FLOW;
		if (spec.match_flags & EFX_FILTER_MATCH_IP_PROTO) {
			uip6_mask->l4_proto = IP_PROTO_FULL_MASK;
			uip6_entry->l4_proto = spec.ip_proto;
		}
		if (spec.match_flags & EFX_FILTER_MATCH_LOC_HOST) {
			memcpy(uip6_entry->ip6dst, spec.loc_host,
			       sizeof(uip6_entry->ip6dst));
			ip6_fill_mask(uip6_mask->ip6dst);
		}
		if (spec.match_flags & EFX_FILTER_MATCH_REM_HOST) {
			memcpy(uip6_entry->ip6src, spec.rem_host,
			       sizeof(uip6_entry->ip6src));
			ip6_fill_mask(uip6_mask->ip6src);
		}
	} else {
		/* The above should handle all filters that we insert */
		WARN_ON(1);
		return -EINVAL;
	}

	if (spec.match_flags & EFX_FILTER_MATCH_OUTER_VID) {
		rule->flow_type |= FLOW_EXT;
		rule->h_ext.vlan_tci = spec.outer_vid;
		rule->m_ext.vlan_tci = htons(0xfff);
	}

	if (spec.flags & EFX_FILTER_FLAG_RX_RSS) {
		rule->flow_type |= FLOW_RSS;
		*rss_context = spec.rss_context;
	}

	return rc;
}

int efx_ethtool_get_rxnfc(struct net_device *net_dev,
			  struct ethtool_rxnfc *info, u32 *rule_locs)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	u32 rss_context = 0;
	s32 rc = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = efx->n_rx_channels;
		return 0;

	case ETHTOOL_GRXFH: {
		struct efx_rss_context_priv *ctx = &efx->rss_context.priv;
		__u64 data;

		mutex_lock(&net_dev->ethtool->rss_lock);
		if (info->flow_type & FLOW_RSS && info->rss_context) {
			ctx = efx_find_rss_context_entry(efx, info->rss_context);
			if (!ctx) {
				rc = -ENOENT;
				goto out_unlock;
			}
		}

		data = 0;
		if (!efx_rss_active(ctx)) /* No RSS */
			goto out_setdata_unlock;

		switch (info->flow_type & ~FLOW_RSS) {
		case UDP_V4_FLOW:
		case UDP_V6_FLOW:
			if (ctx->rx_hash_udp_4tuple)
				data = (RXH_L4_B_0_1 | RXH_L4_B_2_3 |
					RXH_IP_SRC | RXH_IP_DST);
			else
				data = RXH_IP_SRC | RXH_IP_DST;
			break;
		case TCP_V4_FLOW:
		case TCP_V6_FLOW:
			data = (RXH_L4_B_0_1 | RXH_L4_B_2_3 |
				RXH_IP_SRC | RXH_IP_DST);
			break;
		case SCTP_V4_FLOW:
		case SCTP_V6_FLOW:
		case AH_ESP_V4_FLOW:
		case AH_ESP_V6_FLOW:
		case IPV4_FLOW:
		case IPV6_FLOW:
			data = RXH_IP_SRC | RXH_IP_DST;
			break;
		default:
			break;
		}
out_setdata_unlock:
		info->data = data;
out_unlock:
		mutex_unlock(&net_dev->ethtool->rss_lock);
		return rc;
	}

	case ETHTOOL_GRXCLSRLCNT:
		info->data = efx_filter_get_rx_id_limit(efx);
		if (info->data == 0)
			return -EOPNOTSUPP;
		info->data |= RX_CLS_LOC_SPECIAL;
		info->rule_cnt =
			efx_filter_count_rx_used(efx, EFX_FILTER_PRI_MANUAL);
		return 0;

	case ETHTOOL_GRXCLSRULE:
		if (efx_filter_get_rx_id_limit(efx) == 0)
			return -EOPNOTSUPP;
		rc = efx_ethtool_get_class_rule(efx, &info->fs, &rss_context);
		if (rc < 0)
			return rc;
		if (info->fs.flow_type & FLOW_RSS)
			info->rss_context = rss_context;
		return 0;

	case ETHTOOL_GRXCLSRLALL:
		info->data = efx_filter_get_rx_id_limit(efx);
		if (info->data == 0)
			return -EOPNOTSUPP;
		rc = efx_filter_get_rx_ids(efx, EFX_FILTER_PRI_MANUAL,
					   rule_locs, info->rule_cnt);
		if (rc < 0)
			return rc;
		info->rule_cnt = rc;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static inline bool ip6_mask_is_full(__be32 mask[4])
{
	return !~(mask[0] & mask[1] & mask[2] & mask[3]);
}

static inline bool ip6_mask_is_empty(__be32 mask[4])
{
	return !(mask[0] | mask[1] | mask[2] | mask[3]);
}

static int efx_ethtool_set_class_rule(struct efx_nic *efx,
				      struct ethtool_rx_flow_spec *rule,
				      u32 rss_context)
{
	struct ethtool_tcpip4_spec *ip_entry = &rule->h_u.tcp_ip4_spec;
	struct ethtool_tcpip4_spec *ip_mask = &rule->m_u.tcp_ip4_spec;
	struct ethtool_usrip4_spec *uip_entry = &rule->h_u.usr_ip4_spec;
	struct ethtool_usrip4_spec *uip_mask = &rule->m_u.usr_ip4_spec;
	struct ethtool_tcpip6_spec *ip6_entry = &rule->h_u.tcp_ip6_spec;
	struct ethtool_tcpip6_spec *ip6_mask = &rule->m_u.tcp_ip6_spec;
	struct ethtool_usrip6_spec *uip6_entry = &rule->h_u.usr_ip6_spec;
	struct ethtool_usrip6_spec *uip6_mask = &rule->m_u.usr_ip6_spec;
	u32 flow_type = rule->flow_type & ~(FLOW_EXT | FLOW_RSS);
	struct ethhdr *mac_entry = &rule->h_u.ether_spec;
	struct ethhdr *mac_mask = &rule->m_u.ether_spec;
	enum efx_filter_flags flags = 0;
	struct efx_filter_spec spec;
	int rc;

	/* Check that user wants us to choose the location */
	if (rule->location != RX_CLS_LOC_ANY)
		return -EINVAL;

	/* Range-check ring_cookie */
	if (rule->ring_cookie >= efx->n_rx_channels &&
	    rule->ring_cookie != RX_CLS_FLOW_DISC)
		return -EINVAL;

	/* Check for unsupported extensions */
	if ((rule->flow_type & FLOW_EXT) &&
	    (rule->m_ext.vlan_etype || rule->m_ext.data[0] ||
	     rule->m_ext.data[1]))
		return -EINVAL;

	if (efx->rx_scatter)
		flags |= EFX_FILTER_FLAG_RX_SCATTER;
	if (rule->flow_type & FLOW_RSS)
		flags |= EFX_FILTER_FLAG_RX_RSS;

	efx_filter_init_rx(&spec, EFX_FILTER_PRI_MANUAL, flags,
			   (rule->ring_cookie == RX_CLS_FLOW_DISC) ?
			   EFX_FILTER_RX_DMAQ_ID_DROP : rule->ring_cookie);

	if (rule->flow_type & FLOW_RSS)
		spec.rss_context = rss_context;

	switch (flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		spec.match_flags = (EFX_FILTER_MATCH_ETHER_TYPE |
				    EFX_FILTER_MATCH_IP_PROTO);
		spec.ether_type = htons(ETH_P_IP);
		spec.ip_proto = flow_type == TCP_V4_FLOW ? IPPROTO_TCP
							 : IPPROTO_UDP;
		if (ip_mask->ip4dst) {
			if (ip_mask->ip4dst != IP4_ADDR_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_LOC_HOST;
			spec.loc_host[0] = ip_entry->ip4dst;
		}
		if (ip_mask->ip4src) {
			if (ip_mask->ip4src != IP4_ADDR_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_REM_HOST;
			spec.rem_host[0] = ip_entry->ip4src;
		}
		if (ip_mask->pdst) {
			if (ip_mask->pdst != PORT_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_LOC_PORT;
			spec.loc_port = ip_entry->pdst;
		}
		if (ip_mask->psrc) {
			if (ip_mask->psrc != PORT_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_REM_PORT;
			spec.rem_port = ip_entry->psrc;
		}
		if (ip_mask->tos)
			return -EINVAL;
		break;

	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		spec.match_flags = (EFX_FILTER_MATCH_ETHER_TYPE |
				    EFX_FILTER_MATCH_IP_PROTO);
		spec.ether_type = htons(ETH_P_IPV6);
		spec.ip_proto = flow_type == TCP_V6_FLOW ? IPPROTO_TCP
							 : IPPROTO_UDP;
		if (!ip6_mask_is_empty(ip6_mask->ip6dst)) {
			if (!ip6_mask_is_full(ip6_mask->ip6dst))
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_LOC_HOST;
			memcpy(spec.loc_host, ip6_entry->ip6dst, sizeof(spec.loc_host));
		}
		if (!ip6_mask_is_empty(ip6_mask->ip6src)) {
			if (!ip6_mask_is_full(ip6_mask->ip6src))
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_REM_HOST;
			memcpy(spec.rem_host, ip6_entry->ip6src, sizeof(spec.rem_host));
		}
		if (ip6_mask->pdst) {
			if (ip6_mask->pdst != PORT_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_LOC_PORT;
			spec.loc_port = ip6_entry->pdst;
		}
		if (ip6_mask->psrc) {
			if (ip6_mask->psrc != PORT_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_REM_PORT;
			spec.rem_port = ip6_entry->psrc;
		}
		if (ip6_mask->tclass)
			return -EINVAL;
		break;

	case IPV4_USER_FLOW:
		if (uip_mask->l4_4_bytes || uip_mask->tos || uip_mask->ip_ver ||
		    uip_entry->ip_ver != ETH_RX_NFC_IP4)
			return -EINVAL;
		spec.match_flags = EFX_FILTER_MATCH_ETHER_TYPE;
		spec.ether_type = htons(ETH_P_IP);
		if (uip_mask->ip4dst) {
			if (uip_mask->ip4dst != IP4_ADDR_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_LOC_HOST;
			spec.loc_host[0] = uip_entry->ip4dst;
		}
		if (uip_mask->ip4src) {
			if (uip_mask->ip4src != IP4_ADDR_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_REM_HOST;
			spec.rem_host[0] = uip_entry->ip4src;
		}
		if (uip_mask->proto) {
			if (uip_mask->proto != IP_PROTO_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_IP_PROTO;
			spec.ip_proto = uip_entry->proto;
		}
		break;

	case IPV6_USER_FLOW:
		if (uip6_mask->l4_4_bytes || uip6_mask->tclass)
			return -EINVAL;
		spec.match_flags = EFX_FILTER_MATCH_ETHER_TYPE;
		spec.ether_type = htons(ETH_P_IPV6);
		if (!ip6_mask_is_empty(uip6_mask->ip6dst)) {
			if (!ip6_mask_is_full(uip6_mask->ip6dst))
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_LOC_HOST;
			memcpy(spec.loc_host, uip6_entry->ip6dst, sizeof(spec.loc_host));
		}
		if (!ip6_mask_is_empty(uip6_mask->ip6src)) {
			if (!ip6_mask_is_full(uip6_mask->ip6src))
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_REM_HOST;
			memcpy(spec.rem_host, uip6_entry->ip6src, sizeof(spec.rem_host));
		}
		if (uip6_mask->l4_proto) {
			if (uip6_mask->l4_proto != IP_PROTO_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_IP_PROTO;
			spec.ip_proto = uip6_entry->l4_proto;
		}
		break;

	case ETHER_FLOW:
		if (!is_zero_ether_addr(mac_mask->h_dest)) {
			if (ether_addr_equal(mac_mask->h_dest,
					     mac_addr_ig_mask))
				spec.match_flags |= EFX_FILTER_MATCH_LOC_MAC_IG;
			else if (is_broadcast_ether_addr(mac_mask->h_dest))
				spec.match_flags |= EFX_FILTER_MATCH_LOC_MAC;
			else
				return -EINVAL;
			ether_addr_copy(spec.loc_mac, mac_entry->h_dest);
		}
		if (!is_zero_ether_addr(mac_mask->h_source)) {
			if (!is_broadcast_ether_addr(mac_mask->h_source))
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_REM_MAC;
			ether_addr_copy(spec.rem_mac, mac_entry->h_source);
		}
		if (mac_mask->h_proto) {
			if (mac_mask->h_proto != ETHER_TYPE_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EFX_FILTER_MATCH_ETHER_TYPE;
			spec.ether_type = mac_entry->h_proto;
		}
		break;

	default:
		return -EINVAL;
	}

	if ((rule->flow_type & FLOW_EXT) && rule->m_ext.vlan_tci) {
		if (rule->m_ext.vlan_tci != htons(0xfff))
			return -EINVAL;
		spec.match_flags |= EFX_FILTER_MATCH_OUTER_VID;
		spec.outer_vid = rule->h_ext.vlan_tci;
	}

	rc = efx_filter_insert_filter(efx, &spec, true);
	if (rc < 0)
		return rc;

	rule->location = rc;
	return 0;
}

int efx_ethtool_set_rxnfc(struct net_device *net_dev,
			  struct ethtool_rxnfc *info)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	if (efx_filter_get_rx_id_limit(efx) == 0)
		return -EOPNOTSUPP;

	switch (info->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		return efx_ethtool_set_class_rule(efx, &info->fs,
						  info->rss_context);

	case ETHTOOL_SRXCLSRLDEL:
		return efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_MANUAL,
						 info->fs.location);

	default:
		return -EOPNOTSUPP;
	}
}

u32 efx_ethtool_get_rxfh_indir_size(struct net_device *net_dev)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	if (efx->n_rx_channels == 1)
		return 0;
	return ARRAY_SIZE(efx->rss_context.rx_indir_table);
}

u32 efx_ethtool_get_rxfh_key_size(struct net_device *net_dev)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	return efx->type->rx_hash_key_size;
}

int efx_ethtool_get_rxfh(struct net_device *net_dev,
			 struct ethtool_rxfh_param *rxfh)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int rc;

	if (rxfh->rss_context) /* core should never call us for these */
		return -EINVAL;

	rc = efx->type->rx_pull_rss_config(efx);
	if (rc)
		return rc;

	rxfh->hfunc = ETH_RSS_HASH_TOP;
	if (rxfh->indir)
		memcpy(rxfh->indir, efx->rss_context.rx_indir_table,
		       sizeof(efx->rss_context.rx_indir_table));
	if (rxfh->key)
		memcpy(rxfh->key, efx->rss_context.rx_hash_key,
		       efx->type->rx_hash_key_size);
	return 0;
}

int efx_ethtool_modify_rxfh_context(struct net_device *net_dev,
				    struct ethtool_rxfh_context *ctx,
				    const struct ethtool_rxfh_param *rxfh,
				    struct netlink_ext_ack *extack)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	struct efx_rss_context_priv *priv;
	const u32 *indir = rxfh->indir;
	const u8 *key = rxfh->key;

	if (!efx->type->rx_push_rss_context_config) {
		NL_SET_ERR_MSG_MOD(extack,
				   "NIC type does not support custom contexts");
		return -EOPNOTSUPP;
	}
	/* Hash function is Toeplitz, cannot be changed */
	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP) {
		NL_SET_ERR_MSG_MOD(extack, "Only Toeplitz hash is supported");
		return -EOPNOTSUPP;
	}

	priv = ethtool_rxfh_context_priv(ctx);

	if (!key)
		key = ethtool_rxfh_context_key(ctx);
	if (!indir)
		indir = ethtool_rxfh_context_indir(ctx);

	return efx->type->rx_push_rss_context_config(efx, priv, indir, key,
						     false);
}

int efx_ethtool_create_rxfh_context(struct net_device *net_dev,
				    struct ethtool_rxfh_context *ctx,
				    const struct ethtool_rxfh_param *rxfh,
				    struct netlink_ext_ack *extack)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	struct efx_rss_context_priv *priv;

	priv = ethtool_rxfh_context_priv(ctx);

	priv->context_id = EFX_MCDI_RSS_CONTEXT_INVALID;
	priv->rx_hash_udp_4tuple = false;
	/* Generate default indir table and/or key if not specified.
	 * We use ctx as a place to store these; this is fine because
	 * we're doing a create, so if we fail then the ctx will just
	 * be deleted.
	 */
	if (!rxfh->indir)
		efx_set_default_rx_indir_table(efx, ethtool_rxfh_context_indir(ctx));
	if (!rxfh->key)
		netdev_rss_key_fill(ethtool_rxfh_context_key(ctx),
				    ctx->key_size);
	if (rxfh->hfunc == ETH_RSS_HASH_NO_CHANGE)
		ctx->hfunc = ETH_RSS_HASH_TOP;
	if (rxfh->input_xfrm == RXH_XFRM_NO_CHANGE)
		ctx->input_xfrm = 0;
	return efx_ethtool_modify_rxfh_context(net_dev, ctx, rxfh, extack);
}

int efx_ethtool_remove_rxfh_context(struct net_device *net_dev,
				    struct ethtool_rxfh_context *ctx,
				    u32 rss_context,
				    struct netlink_ext_ack *extack)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	struct efx_rss_context_priv *priv;

	if (!efx->type->rx_push_rss_context_config) {
		NL_SET_ERR_MSG_MOD(extack,
				   "NIC type does not support custom contexts");
		return -EOPNOTSUPP;
	}

	priv = ethtool_rxfh_context_priv(ctx);
	return efx->type->rx_push_rss_context_config(efx, priv, NULL, NULL,
						     true);
}

int efx_ethtool_set_rxfh(struct net_device *net_dev,
			 struct ethtool_rxfh_param *rxfh,
			 struct netlink_ext_ack *extack)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	u32 *indir = rxfh->indir;
	u8 *key = rxfh->key;

	/* Hash function is Toeplitz, cannot be changed */
	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	/* Custom contexts should use new API */
	if (WARN_ON_ONCE(rxfh->rss_context))
		return -EIO;

	if (!indir && !key)
		return 0;

	if (!key)
		key = efx->rss_context.rx_hash_key;
	if (!indir)
		indir = efx->rss_context.rx_indir_table;

	return efx->type->rx_push_rss_config(efx, true, indir, key);
}

int efx_ethtool_reset(struct net_device *net_dev, u32 *flags)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int rc;

	rc = efx->type->map_reset_flags(flags);
	if (rc < 0)
		return rc;

	return efx_reset(efx, rc);
}

int efx_ethtool_get_module_eeprom(struct net_device *net_dev,
				  struct ethtool_eeprom *ee,
				  u8 *data)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int ret;

	mutex_lock(&efx->mac_lock);
	ret = efx_mcdi_phy_get_module_eeprom(efx, ee, data);
	mutex_unlock(&efx->mac_lock);

	return ret;
}

int efx_ethtool_get_module_info(struct net_device *net_dev,
				struct ethtool_modinfo *modinfo)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int ret;

	mutex_lock(&efx->mac_lock);
	ret = efx_mcdi_phy_get_module_info(efx, modinfo);
	mutex_unlock(&efx->mac_lock);

	return ret;
}
