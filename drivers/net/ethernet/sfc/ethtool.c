/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/in.h>
#include "net_driver.h"
#include "workarounds.h"
#include "selftest.h"
#include "efx.h"
#include "filter.h"
#include "nic.h"

struct ethtool_string {
	char name[ETH_GSTRING_LEN];
};

struct efx_ethtool_stat {
	const char *name;
	enum {
		EFX_ETHTOOL_STAT_SOURCE_mac_stats,
		EFX_ETHTOOL_STAT_SOURCE_nic,
		EFX_ETHTOOL_STAT_SOURCE_channel,
		EFX_ETHTOOL_STAT_SOURCE_tx_queue
	} source;
	unsigned offset;
	u64(*get_stat) (void *field); /* Reader function */
};

/* Initialiser for a struct #efx_ethtool_stat with type-checking */
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

static u64 efx_get_u64_stat(void *field)
{
	return *(u64 *) field;
}

static u64 efx_get_atomic_stat(void *field)
{
	return atomic_read((atomic_t *) field);
}

#define EFX_ETHTOOL_U64_MAC_STAT(field)				\
	EFX_ETHTOOL_STAT(field, mac_stats, field,		\
			  u64, efx_get_u64_stat)

#define EFX_ETHTOOL_UINT_NIC_STAT(name)				\
	EFX_ETHTOOL_STAT(name, nic, n_##name,			\
			 unsigned int, efx_get_uint_stat)

#define EFX_ETHTOOL_ATOMIC_NIC_ERROR_STAT(field)		\
	EFX_ETHTOOL_STAT(field, nic, field,			\
			 atomic_t, efx_get_atomic_stat)

#define EFX_ETHTOOL_UINT_CHANNEL_STAT(field)			\
	EFX_ETHTOOL_STAT(field, channel, n_##field,		\
			 unsigned int, efx_get_uint_stat)

#define EFX_ETHTOOL_UINT_TXQ_STAT(field)			\
	EFX_ETHTOOL_STAT(tx_##field, tx_queue, field,		\
			 unsigned int, efx_get_uint_stat)

static const struct efx_ethtool_stat efx_ethtool_stats[] = {
	EFX_ETHTOOL_U64_MAC_STAT(tx_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(tx_good_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(tx_bad_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(tx_packets),
	EFX_ETHTOOL_U64_MAC_STAT(tx_bad),
	EFX_ETHTOOL_U64_MAC_STAT(tx_pause),
	EFX_ETHTOOL_U64_MAC_STAT(tx_control),
	EFX_ETHTOOL_U64_MAC_STAT(tx_unicast),
	EFX_ETHTOOL_U64_MAC_STAT(tx_multicast),
	EFX_ETHTOOL_U64_MAC_STAT(tx_broadcast),
	EFX_ETHTOOL_U64_MAC_STAT(tx_lt64),
	EFX_ETHTOOL_U64_MAC_STAT(tx_64),
	EFX_ETHTOOL_U64_MAC_STAT(tx_65_to_127),
	EFX_ETHTOOL_U64_MAC_STAT(tx_128_to_255),
	EFX_ETHTOOL_U64_MAC_STAT(tx_256_to_511),
	EFX_ETHTOOL_U64_MAC_STAT(tx_512_to_1023),
	EFX_ETHTOOL_U64_MAC_STAT(tx_1024_to_15xx),
	EFX_ETHTOOL_U64_MAC_STAT(tx_15xx_to_jumbo),
	EFX_ETHTOOL_U64_MAC_STAT(tx_gtjumbo),
	EFX_ETHTOOL_U64_MAC_STAT(tx_collision),
	EFX_ETHTOOL_U64_MAC_STAT(tx_single_collision),
	EFX_ETHTOOL_U64_MAC_STAT(tx_multiple_collision),
	EFX_ETHTOOL_U64_MAC_STAT(tx_excessive_collision),
	EFX_ETHTOOL_U64_MAC_STAT(tx_deferred),
	EFX_ETHTOOL_U64_MAC_STAT(tx_late_collision),
	EFX_ETHTOOL_U64_MAC_STAT(tx_excessive_deferred),
	EFX_ETHTOOL_U64_MAC_STAT(tx_non_tcpudp),
	EFX_ETHTOOL_U64_MAC_STAT(tx_mac_src_error),
	EFX_ETHTOOL_U64_MAC_STAT(tx_ip_src_error),
	EFX_ETHTOOL_UINT_TXQ_STAT(tso_bursts),
	EFX_ETHTOOL_UINT_TXQ_STAT(tso_long_headers),
	EFX_ETHTOOL_UINT_TXQ_STAT(tso_packets),
	EFX_ETHTOOL_UINT_TXQ_STAT(pushes),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(rx_good_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bad_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(rx_packets),
	EFX_ETHTOOL_U64_MAC_STAT(rx_good),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bad),
	EFX_ETHTOOL_U64_MAC_STAT(rx_pause),
	EFX_ETHTOOL_U64_MAC_STAT(rx_control),
	EFX_ETHTOOL_U64_MAC_STAT(rx_unicast),
	EFX_ETHTOOL_U64_MAC_STAT(rx_multicast),
	EFX_ETHTOOL_U64_MAC_STAT(rx_broadcast),
	EFX_ETHTOOL_U64_MAC_STAT(rx_lt64),
	EFX_ETHTOOL_U64_MAC_STAT(rx_64),
	EFX_ETHTOOL_U64_MAC_STAT(rx_65_to_127),
	EFX_ETHTOOL_U64_MAC_STAT(rx_128_to_255),
	EFX_ETHTOOL_U64_MAC_STAT(rx_256_to_511),
	EFX_ETHTOOL_U64_MAC_STAT(rx_512_to_1023),
	EFX_ETHTOOL_U64_MAC_STAT(rx_1024_to_15xx),
	EFX_ETHTOOL_U64_MAC_STAT(rx_15xx_to_jumbo),
	EFX_ETHTOOL_U64_MAC_STAT(rx_gtjumbo),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bad_lt64),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bad_64_to_15xx),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bad_15xx_to_jumbo),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bad_gtjumbo),
	EFX_ETHTOOL_U64_MAC_STAT(rx_overflow),
	EFX_ETHTOOL_U64_MAC_STAT(rx_missed),
	EFX_ETHTOOL_U64_MAC_STAT(rx_false_carrier),
	EFX_ETHTOOL_U64_MAC_STAT(rx_symbol_error),
	EFX_ETHTOOL_U64_MAC_STAT(rx_align_error),
	EFX_ETHTOOL_U64_MAC_STAT(rx_length_error),
	EFX_ETHTOOL_U64_MAC_STAT(rx_internal_error),
	EFX_ETHTOOL_UINT_NIC_STAT(rx_nodesc_drop_cnt),
	EFX_ETHTOOL_ATOMIC_NIC_ERROR_STAT(rx_reset),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_tobe_disc),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_ip_hdr_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_tcp_udp_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_mcast_mismatch),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_frm_trunc),
};

/* Number of ethtool statistics */
#define EFX_ETHTOOL_NUM_STATS ARRAY_SIZE(efx_ethtool_stats)

#define EFX_ETHTOOL_EEPROM_MAGIC 0xEFAB

/**************************************************************************
 *
 * Ethtool operations
 *
 **************************************************************************
 */

/* Identify device by flashing LEDs */
static int efx_ethtool_phys_id(struct net_device *net_dev,
			       enum ethtool_phys_id_state state)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	enum efx_led_mode mode = EFX_LED_DEFAULT;

	switch (state) {
	case ETHTOOL_ID_ON:
		mode = EFX_LED_ON;
		break;
	case ETHTOOL_ID_OFF:
		mode = EFX_LED_OFF;
		break;
	case ETHTOOL_ID_INACTIVE:
		mode = EFX_LED_DEFAULT;
		break;
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */
	}

	efx->type->set_id_led(efx, mode);
	return 0;
}

/* This must be called with rtnl_lock held. */
static int efx_ethtool_get_settings(struct net_device *net_dev,
				    struct ethtool_cmd *ecmd)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_link_state *link_state = &efx->link_state;

	mutex_lock(&efx->mac_lock);
	efx->phy_op->get_settings(efx, ecmd);
	mutex_unlock(&efx->mac_lock);

	/* GMAC does not support 1000Mbps HD */
	ecmd->supported &= ~SUPPORTED_1000baseT_Half;
	/* Both MACs support pause frames (bidirectional and respond-only) */
	ecmd->supported |= SUPPORTED_Pause | SUPPORTED_Asym_Pause;

	if (LOOPBACK_INTERNAL(efx)) {
		ethtool_cmd_speed_set(ecmd, link_state->speed);
		ecmd->duplex = link_state->fd ? DUPLEX_FULL : DUPLEX_HALF;
	}

	return 0;
}

/* This must be called with rtnl_lock held. */
static int efx_ethtool_set_settings(struct net_device *net_dev,
				    struct ethtool_cmd *ecmd)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int rc;

	/* GMAC does not support 1000Mbps HD */
	if ((ethtool_cmd_speed(ecmd) == SPEED_1000) &&
	    (ecmd->duplex != DUPLEX_FULL)) {
		netif_dbg(efx, drv, efx->net_dev,
			  "rejecting unsupported 1000Mbps HD setting\n");
		return -EINVAL;
	}

	mutex_lock(&efx->mac_lock);
	rc = efx->phy_op->set_settings(efx, ecmd);
	mutex_unlock(&efx->mac_lock);
	return rc;
}

static void efx_ethtool_get_drvinfo(struct net_device *net_dev,
				    struct ethtool_drvinfo *info)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, EFX_DRIVER_VERSION, sizeof(info->version));
	if (efx_nic_rev(efx) >= EFX_REV_SIENA_A0)
		efx_mcdi_print_fwver(efx, info->fw_version,
				     sizeof(info->fw_version));
	strlcpy(info->bus_info, pci_name(efx->pci_dev), sizeof(info->bus_info));
}

static int efx_ethtool_get_regs_len(struct net_device *net_dev)
{
	return efx_nic_get_regs_len(netdev_priv(net_dev));
}

static void efx_ethtool_get_regs(struct net_device *net_dev,
				 struct ethtool_regs *regs, void *buf)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	regs->version = efx->type->revision;
	efx_nic_get_regs(efx, buf);
}

static u32 efx_ethtool_get_msglevel(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	return efx->msg_enable;
}

static void efx_ethtool_set_msglevel(struct net_device *net_dev, u32 msg_enable)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	efx->msg_enable = msg_enable;
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
static void efx_fill_test(unsigned int test_index,
			  struct ethtool_string *strings, u64 *data,
			  int *test, const char *unit_format, int unit_id,
			  const char *test_format, const char *test_id)
{
	struct ethtool_string unit_str, test_str;

	/* Fill data value, if applicable */
	if (data)
		data[test_index] = *test;

	/* Fill string, if applicable */
	if (strings) {
		if (strchr(unit_format, '%'))
			snprintf(unit_str.name, sizeof(unit_str.name),
				 unit_format, unit_id);
		else
			strcpy(unit_str.name, unit_format);
		snprintf(test_str.name, sizeof(test_str.name),
			 test_format, test_id);
		snprintf(strings[test_index].name,
			 sizeof(strings[test_index].name),
			 "%-6s %-24s", unit_str.name, test_str.name);
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
 */
static int efx_fill_loopback_test(struct efx_nic *efx,
				  struct efx_loopback_self_tests *lb_tests,
				  enum efx_loopback_mode mode,
				  unsigned int test_index,
				  struct ethtool_string *strings, u64 *data)
{
	struct efx_channel *channel = efx_get_channel(efx, 0);
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
 */
static int efx_ethtool_fill_self_tests(struct efx_nic *efx,
				       struct efx_self_tests *tests,
				       struct ethtool_string *strings,
				       u64 *data)
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

	efx_fill_test(n++, strings, data, &tests->registers,
		      "core", 0, "registers", NULL);

	if (efx->phy_op->run_tests != NULL) {
		EFX_BUG_ON_PARANOID(efx->phy_op->test_name == NULL);

		for (i = 0; true; ++i) {
			const char *name;

			EFX_BUG_ON_PARANOID(i >= EFX_MAX_PHY_TESTS);
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

static int efx_ethtool_get_sset_count(struct net_device *net_dev,
				      int string_set)
{
	switch (string_set) {
	case ETH_SS_STATS:
		return EFX_ETHTOOL_NUM_STATS;
	case ETH_SS_TEST:
		return efx_ethtool_fill_self_tests(netdev_priv(net_dev),
						   NULL, NULL, NULL);
	default:
		return -EINVAL;
	}
}

static void efx_ethtool_get_strings(struct net_device *net_dev,
				    u32 string_set, u8 *strings)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct ethtool_string *ethtool_strings =
		(struct ethtool_string *)strings;
	int i;

	switch (string_set) {
	case ETH_SS_STATS:
		for (i = 0; i < EFX_ETHTOOL_NUM_STATS; i++)
			strlcpy(ethtool_strings[i].name,
				efx_ethtool_stats[i].name,
				sizeof(ethtool_strings[i].name));
		break;
	case ETH_SS_TEST:
		efx_ethtool_fill_self_tests(efx, NULL,
					    ethtool_strings, NULL);
		break;
	default:
		/* No other string sets */
		break;
	}
}

static void efx_ethtool_get_stats(struct net_device *net_dev,
				  struct ethtool_stats *stats,
				  u64 *data)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_mac_stats *mac_stats = &efx->mac_stats;
	const struct efx_ethtool_stat *stat;
	struct efx_channel *channel;
	struct efx_tx_queue *tx_queue;
	int i;

	EFX_BUG_ON_PARANOID(stats->n_stats != EFX_ETHTOOL_NUM_STATS);

	spin_lock_bh(&efx->stats_lock);

	/* Update MAC and NIC statistics */
	efx->type->update_stats(efx);

	/* Fill detailed statistics buffer */
	for (i = 0; i < EFX_ETHTOOL_NUM_STATS; i++) {
		stat = &efx_ethtool_stats[i];
		switch (stat->source) {
		case EFX_ETHTOOL_STAT_SOURCE_mac_stats:
			data[i] = stat->get_stat((void *)mac_stats +
						 stat->offset);
			break;
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

	spin_unlock_bh(&efx->stats_lock);
}

static void efx_ethtool_self_test(struct net_device *net_dev,
				  struct ethtool_test *test, u64 *data)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_self_tests *efx_tests;
	int already_up;
	int rc = -ENOMEM;

	efx_tests = kzalloc(sizeof(*efx_tests), GFP_KERNEL);
	if (!efx_tests)
		goto fail;

	if (efx->state != STATE_READY) {
		rc = -EIO;
		goto fail1;
	}

	netif_info(efx, drv, efx->net_dev, "starting %sline testing\n",
		   (test->flags & ETH_TEST_FL_OFFLINE) ? "off" : "on");

	/* We need rx buffers and interrupts. */
	already_up = (efx->net_dev->flags & IFF_UP);
	if (!already_up) {
		rc = dev_open(efx->net_dev);
		if (rc) {
			netif_err(efx, drv, efx->net_dev,
				  "failed opening device.\n");
			goto fail1;
		}
	}

	rc = efx_selftest(efx, efx_tests, test->flags);

	if (!already_up)
		dev_close(efx->net_dev);

	netif_info(efx, drv, efx->net_dev, "%s %sline self-tests\n",
		   rc == 0 ? "passed" : "failed",
		   (test->flags & ETH_TEST_FL_OFFLINE) ? "off" : "on");

fail1:
	/* Fill ethtool results structures */
	efx_ethtool_fill_self_tests(efx, efx_tests, NULL, data);
	kfree(efx_tests);
fail:
	if (rc)
		test->flags |= ETH_TEST_FL_FAILED;
}

/* Restart autonegotiation */
static int efx_ethtool_nway_reset(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	return mdio45_nway_restart(&efx->mdio);
}

/*
 * Each channel has a single IRQ and moderation timer, started by any
 * completion (or other event).  Unless the module parameter
 * separate_tx_channels is set, IRQs and moderation are therefore
 * shared between RX and TX completions.  In this case, when RX IRQ
 * moderation is explicitly changed then TX IRQ moderation is
 * automatically changed too, but otherwise we fail if the two values
 * are requested to be different.
 *
 * The hardware does not support a limit on the number of completions
 * before an IRQ, so we do not use the max_frames fields.  We should
 * report and require that max_frames == (usecs != 0), but this would
 * invalidate existing user documentation.
 *
 * The hardware does not have distinct settings for interrupt
 * moderation while the previous IRQ is being handled, so we should
 * not use the 'irq' fields.  However, an earlier developer
 * misunderstood the meaning of the 'irq' fields and the driver did
 * not support the standard fields.  To avoid invalidating existing
 * user documentation, we report and accept changes through either the
 * standard or 'irq' fields.  If both are changed at the same time, we
 * prefer the standard field.
 *
 * We implement adaptive IRQ moderation, but use a different algorithm
 * from that assumed in the definition of struct ethtool_coalesce.
 * Therefore we do not use any of the adaptive moderation parameters
 * in it.
 */

static int efx_ethtool_get_coalesce(struct net_device *net_dev,
				    struct ethtool_coalesce *coalesce)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	unsigned int tx_usecs, rx_usecs;
	bool rx_adaptive;

	efx_get_irq_moderation(efx, &tx_usecs, &rx_usecs, &rx_adaptive);

	coalesce->tx_coalesce_usecs = tx_usecs;
	coalesce->tx_coalesce_usecs_irq = tx_usecs;
	coalesce->rx_coalesce_usecs = rx_usecs;
	coalesce->rx_coalesce_usecs_irq = rx_usecs;
	coalesce->use_adaptive_rx_coalesce = rx_adaptive;

	return 0;
}

static int efx_ethtool_set_coalesce(struct net_device *net_dev,
				    struct ethtool_coalesce *coalesce)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_channel *channel;
	unsigned int tx_usecs, rx_usecs;
	bool adaptive, rx_may_override_tx;
	int rc;

	if (coalesce->use_adaptive_tx_coalesce)
		return -EINVAL;

	efx_get_irq_moderation(efx, &tx_usecs, &rx_usecs, &adaptive);

	if (coalesce->rx_coalesce_usecs != rx_usecs)
		rx_usecs = coalesce->rx_coalesce_usecs;
	else
		rx_usecs = coalesce->rx_coalesce_usecs_irq;

	adaptive = coalesce->use_adaptive_rx_coalesce;

	/* If channels are shared, TX IRQ moderation can be quietly
	 * overridden unless it is changed from its old value.
	 */
	rx_may_override_tx = (coalesce->tx_coalesce_usecs == tx_usecs &&
			      coalesce->tx_coalesce_usecs_irq == tx_usecs);
	if (coalesce->tx_coalesce_usecs != tx_usecs)
		tx_usecs = coalesce->tx_coalesce_usecs;
	else
		tx_usecs = coalesce->tx_coalesce_usecs_irq;

	rc = efx_init_irq_moderation(efx, tx_usecs, rx_usecs, adaptive,
				     rx_may_override_tx);
	if (rc != 0)
		return rc;

	efx_for_each_channel(channel, efx)
		efx->type->push_irq_moderation(channel);

	return 0;
}

static void efx_ethtool_get_ringparam(struct net_device *net_dev,
				      struct ethtool_ringparam *ring)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	ring->rx_max_pending = EFX_MAX_DMAQ_SIZE;
	ring->tx_max_pending = EFX_MAX_DMAQ_SIZE;
	ring->rx_pending = efx->rxq_entries;
	ring->tx_pending = efx->txq_entries;
}

static int efx_ethtool_set_ringparam(struct net_device *net_dev,
				     struct ethtool_ringparam *ring)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	u32 txq_entries;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending ||
	    ring->rx_pending > EFX_MAX_DMAQ_SIZE ||
	    ring->tx_pending > EFX_MAX_DMAQ_SIZE)
		return -EINVAL;

	if (ring->rx_pending < EFX_RXQ_MIN_ENT) {
		netif_err(efx, drv, efx->net_dev,
			  "RX queues cannot be smaller than %u\n",
			  EFX_RXQ_MIN_ENT);
		return -EINVAL;
	}

	txq_entries = max(ring->tx_pending, EFX_TXQ_MIN_ENT(efx));
	if (txq_entries != ring->tx_pending)
		netif_warn(efx, drv, efx->net_dev,
			   "increasing TX queue size to minimum of %u\n",
			   txq_entries);

	return efx_realloc_channels(efx, ring->rx_pending, txq_entries);
}

static int efx_ethtool_set_pauseparam(struct net_device *net_dev,
				      struct ethtool_pauseparam *pause)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	u8 wanted_fc, old_fc;
	u32 old_adv;
	bool reset;
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

	if ((wanted_fc & EFX_FC_AUTO) && !efx->link_advertising) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Autonegotiation is disabled\n");
		rc = -EINVAL;
		goto out;
	}

	/* TX flow control may automatically turn itself off if the
	 * link partner (intermittently) stops responding to pause
	 * frames. There isn't any indication that this has happened,
	 * so the best we do is leave it up to the user to spot this
	 * and fix it be cycling transmit flow control on this end. */
	reset = (wanted_fc & EFX_FC_TX) && !(efx->wanted_fc & EFX_FC_TX);
	if (EFX_WORKAROUND_11482(efx) && reset) {
		if (efx_nic_rev(efx) == EFX_REV_FALCON_B0) {
			/* Recover by resetting the EM block */
			falcon_stop_nic_stats(efx);
			falcon_drain_tx_fifo(efx);
			falcon_reconfigure_xmac(efx);
			falcon_start_nic_stats(efx);
		} else {
			/* Schedule a reset to recover */
			efx_schedule_reset(efx, RESET_TYPE_INVISIBLE);
		}
	}

	old_adv = efx->link_advertising;
	old_fc = efx->wanted_fc;
	efx_link_set_wanted_fc(efx, wanted_fc);
	if (efx->link_advertising != old_adv ||
	    (efx->wanted_fc ^ old_fc) & EFX_FC_AUTO) {
		rc = efx->phy_op->reconfigure(efx);
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
	efx->type->reconfigure_mac(efx);

out:
	mutex_unlock(&efx->mac_lock);

	return rc;
}

static void efx_ethtool_get_pauseparam(struct net_device *net_dev,
				       struct ethtool_pauseparam *pause)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	pause->rx_pause = !!(efx->wanted_fc & EFX_FC_RX);
	pause->tx_pause = !!(efx->wanted_fc & EFX_FC_TX);
	pause->autoneg = !!(efx->wanted_fc & EFX_FC_AUTO);
}


static void efx_ethtool_get_wol(struct net_device *net_dev,
				struct ethtool_wolinfo *wol)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	return efx->type->get_wol(efx, wol);
}


static int efx_ethtool_set_wol(struct net_device *net_dev,
			       struct ethtool_wolinfo *wol)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	return efx->type->set_wol(efx, wol->wolopts);
}

static int efx_ethtool_reset(struct net_device *net_dev, u32 *flags)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int rc;

	rc = efx->type->map_reset_flags(flags);
	if (rc < 0)
		return rc;

	return efx_reset(efx, rc);
}

/* MAC address mask including only MC flag */
static const u8 mac_addr_mc_mask[ETH_ALEN] = { 0x01, 0, 0, 0, 0, 0 };

static int efx_ethtool_get_class_rule(struct efx_nic *efx,
				      struct ethtool_rx_flow_spec *rule)
{
	struct ethtool_tcpip4_spec *ip_entry = &rule->h_u.tcp_ip4_spec;
	struct ethtool_tcpip4_spec *ip_mask = &rule->m_u.tcp_ip4_spec;
	struct ethhdr *mac_entry = &rule->h_u.ether_spec;
	struct ethhdr *mac_mask = &rule->m_u.ether_spec;
	struct efx_filter_spec spec;
	u16 vid;
	u8 proto;
	int rc;

	rc = efx_filter_get_filter_safe(efx, EFX_FILTER_PRI_MANUAL,
					rule->location, &spec);
	if (rc)
		return rc;

	if (spec.dmaq_id == 0xfff)
		rule->ring_cookie = RX_CLS_FLOW_DISC;
	else
		rule->ring_cookie = spec.dmaq_id;

	if (spec.type == EFX_FILTER_MC_DEF || spec.type == EFX_FILTER_UC_DEF) {
		rule->flow_type = ETHER_FLOW;
		memcpy(mac_mask->h_dest, mac_addr_mc_mask, ETH_ALEN);
		if (spec.type == EFX_FILTER_MC_DEF)
			memcpy(mac_entry->h_dest, mac_addr_mc_mask, ETH_ALEN);
		return 0;
	}

	rc = efx_filter_get_eth_local(&spec, &vid, mac_entry->h_dest);
	if (rc == 0) {
		rule->flow_type = ETHER_FLOW;
		memset(mac_mask->h_dest, ~0, ETH_ALEN);
		if (vid != EFX_FILTER_VID_UNSPEC) {
			rule->flow_type |= FLOW_EXT;
			rule->h_ext.vlan_tci = htons(vid);
			rule->m_ext.vlan_tci = htons(0xfff);
		}
		return 0;
	}

	rc = efx_filter_get_ipv4_local(&spec, &proto,
				       &ip_entry->ip4dst, &ip_entry->pdst);
	if (rc != 0) {
		rc = efx_filter_get_ipv4_full(
			&spec, &proto, &ip_entry->ip4dst, &ip_entry->pdst,
			&ip_entry->ip4src, &ip_entry->psrc);
		EFX_WARN_ON_PARANOID(rc);
		ip_mask->ip4src = ~0;
		ip_mask->psrc = ~0;
	}
	rule->flow_type = (proto == IPPROTO_TCP) ? TCP_V4_FLOW : UDP_V4_FLOW;
	ip_mask->ip4dst = ~0;
	ip_mask->pdst = ~0;
	return rc;
}

static int
efx_ethtool_get_rxnfc(struct net_device *net_dev,
		      struct ethtool_rxnfc *info, u32 *rule_locs)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = efx->n_rx_channels;
		return 0;

	case ETHTOOL_GRXFH: {
		unsigned min_revision = 0;

		info->data = 0;
		switch (info->flow_type) {
		case TCP_V4_FLOW:
			info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
			/* fall through */
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
		case AH_ESP_V4_FLOW:
		case IPV4_FLOW:
			info->data |= RXH_IP_SRC | RXH_IP_DST;
			min_revision = EFX_REV_FALCON_B0;
			break;
		case TCP_V6_FLOW:
			info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
			/* fall through */
		case UDP_V6_FLOW:
		case SCTP_V6_FLOW:
		case AH_ESP_V6_FLOW:
		case IPV6_FLOW:
			info->data |= RXH_IP_SRC | RXH_IP_DST;
			min_revision = EFX_REV_SIENA_A0;
			break;
		default:
			break;
		}
		if (efx_nic_rev(efx) < min_revision)
			info->data = 0;
		return 0;
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
		return efx_ethtool_get_class_rule(efx, &info->fs);

	case ETHTOOL_GRXCLSRLALL: {
		s32 rc;
		info->data = efx_filter_get_rx_id_limit(efx);
		if (info->data == 0)
			return -EOPNOTSUPP;
		rc = efx_filter_get_rx_ids(efx, EFX_FILTER_PRI_MANUAL,
					   rule_locs, info->rule_cnt);
		if (rc < 0)
			return rc;
		info->rule_cnt = rc;
		return 0;
	}

	default:
		return -EOPNOTSUPP;
	}
}

static int efx_ethtool_set_class_rule(struct efx_nic *efx,
				      struct ethtool_rx_flow_spec *rule)
{
	struct ethtool_tcpip4_spec *ip_entry = &rule->h_u.tcp_ip4_spec;
	struct ethtool_tcpip4_spec *ip_mask = &rule->m_u.tcp_ip4_spec;
	struct ethhdr *mac_entry = &rule->h_u.ether_spec;
	struct ethhdr *mac_mask = &rule->m_u.ether_spec;
	struct efx_filter_spec spec;
	int rc;

	/* Check that user wants us to choose the location */
	if (rule->location != RX_CLS_LOC_ANY &&
	    rule->location != RX_CLS_LOC_FIRST &&
	    rule->location != RX_CLS_LOC_LAST)
		return -EINVAL;

	/* Range-check ring_cookie */
	if (rule->ring_cookie >= efx->n_rx_channels &&
	    rule->ring_cookie != RX_CLS_FLOW_DISC)
		return -EINVAL;

	/* Check for unsupported extensions */
	if ((rule->flow_type & FLOW_EXT) &&
	    (rule->m_ext.vlan_etype | rule->m_ext.data[0] |
	     rule->m_ext.data[1]))
		return -EINVAL;

	efx_filter_init_rx(&spec, EFX_FILTER_PRI_MANUAL,
			   (rule->location == RX_CLS_LOC_FIRST) ?
			   EFX_FILTER_FLAG_RX_OVERRIDE_IP : 0,
			   (rule->ring_cookie == RX_CLS_FLOW_DISC) ?
			   0xfff : rule->ring_cookie);

	switch (rule->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW: {
		u8 proto = (rule->flow_type == TCP_V4_FLOW ?
			    IPPROTO_TCP : IPPROTO_UDP);

		/* Must match all of destination, */
		if ((__force u32)~ip_mask->ip4dst |
		    (__force u16)~ip_mask->pdst)
			return -EINVAL;
		/* all or none of source, */
		if ((ip_mask->ip4src | ip_mask->psrc) &&
		    ((__force u32)~ip_mask->ip4src |
		     (__force u16)~ip_mask->psrc))
			return -EINVAL;
		/* and nothing else */
		if (ip_mask->tos | rule->m_ext.vlan_tci)
			return -EINVAL;

		if (ip_mask->ip4src)
			rc = efx_filter_set_ipv4_full(&spec, proto,
						      ip_entry->ip4dst,
						      ip_entry->pdst,
						      ip_entry->ip4src,
						      ip_entry->psrc);
		else
			rc = efx_filter_set_ipv4_local(&spec, proto,
						       ip_entry->ip4dst,
						       ip_entry->pdst);
		if (rc)
			return rc;
		break;
	}

	case ETHER_FLOW | FLOW_EXT:
	case ETHER_FLOW: {
		u16 vlan_tag_mask = (rule->flow_type & FLOW_EXT ?
				     ntohs(rule->m_ext.vlan_tci) : 0);

		/* Must not match on source address or Ethertype */
		if (!is_zero_ether_addr(mac_mask->h_source) ||
		    mac_mask->h_proto)
			return -EINVAL;

		/* Is it a default UC or MC filter? */
		if (ether_addr_equal(mac_mask->h_dest, mac_addr_mc_mask) &&
		    vlan_tag_mask == 0) {
			if (is_multicast_ether_addr(mac_entry->h_dest))
				rc = efx_filter_set_mc_def(&spec);
			else
				rc = efx_filter_set_uc_def(&spec);
		}
		/* Otherwise, it must match all of destination and all
		 * or none of VID.
		 */
		else if (is_broadcast_ether_addr(mac_mask->h_dest) &&
			 (vlan_tag_mask == 0xfff || vlan_tag_mask == 0)) {
			rc = efx_filter_set_eth_local(
				&spec,
				vlan_tag_mask ?
				ntohs(rule->h_ext.vlan_tci) : EFX_FILTER_VID_UNSPEC,
				mac_entry->h_dest);
		} else {
			rc = -EINVAL;
		}
		if (rc)
			return rc;
		break;
	}

	default:
		return -EINVAL;
	}

	rc = efx_filter_insert_filter(efx, &spec, true);
	if (rc < 0)
		return rc;

	rule->location = rc;
	return 0;
}

static int efx_ethtool_set_rxnfc(struct net_device *net_dev,
				 struct ethtool_rxnfc *info)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (efx_filter_get_rx_id_limit(efx) == 0)
		return -EOPNOTSUPP;

	switch (info->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		return efx_ethtool_set_class_rule(efx, &info->fs);

	case ETHTOOL_SRXCLSRLDEL:
		return efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_MANUAL,
						 info->fs.location);

	default:
		return -EOPNOTSUPP;
	}
}

static u32 efx_ethtool_get_rxfh_indir_size(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	return ((efx_nic_rev(efx) < EFX_REV_FALCON_B0 ||
		 efx->n_rx_channels == 1) ?
		0 : ARRAY_SIZE(efx->rx_indir_table));
}

static int efx_ethtool_get_rxfh_indir(struct net_device *net_dev, u32 *indir)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	memcpy(indir, efx->rx_indir_table, sizeof(efx->rx_indir_table));
	return 0;
}

static int efx_ethtool_set_rxfh_indir(struct net_device *net_dev,
				      const u32 *indir)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	memcpy(efx->rx_indir_table, indir, sizeof(efx->rx_indir_table));
	efx_nic_push_rx_indir_table(efx);
	return 0;
}

static int efx_ethtool_get_module_eeprom(struct net_device *net_dev,
					 struct ethtool_eeprom *ee,
					 u8 *data)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int ret;

	if (!efx->phy_op || !efx->phy_op->get_module_eeprom)
		return -EOPNOTSUPP;

	mutex_lock(&efx->mac_lock);
	ret = efx->phy_op->get_module_eeprom(efx, ee, data);
	mutex_unlock(&efx->mac_lock);

	return ret;
}

static int efx_ethtool_get_module_info(struct net_device *net_dev,
				       struct ethtool_modinfo *modinfo)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int ret;

	if (!efx->phy_op || !efx->phy_op->get_module_info)
		return -EOPNOTSUPP;

	mutex_lock(&efx->mac_lock);
	ret = efx->phy_op->get_module_info(efx, modinfo);
	mutex_unlock(&efx->mac_lock);

	return ret;
}

const struct ethtool_ops efx_ethtool_ops = {
	.get_settings		= efx_ethtool_get_settings,
	.set_settings		= efx_ethtool_set_settings,
	.get_drvinfo		= efx_ethtool_get_drvinfo,
	.get_regs_len		= efx_ethtool_get_regs_len,
	.get_regs		= efx_ethtool_get_regs,
	.get_msglevel		= efx_ethtool_get_msglevel,
	.set_msglevel		= efx_ethtool_set_msglevel,
	.nway_reset		= efx_ethtool_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= efx_ethtool_get_coalesce,
	.set_coalesce		= efx_ethtool_set_coalesce,
	.get_ringparam		= efx_ethtool_get_ringparam,
	.set_ringparam		= efx_ethtool_set_ringparam,
	.get_pauseparam         = efx_ethtool_get_pauseparam,
	.set_pauseparam         = efx_ethtool_set_pauseparam,
	.get_sset_count		= efx_ethtool_get_sset_count,
	.self_test		= efx_ethtool_self_test,
	.get_strings		= efx_ethtool_get_strings,
	.set_phys_id		= efx_ethtool_phys_id,
	.get_ethtool_stats	= efx_ethtool_get_stats,
	.get_wol                = efx_ethtool_get_wol,
	.set_wol                = efx_ethtool_set_wol,
	.reset			= efx_ethtool_reset,
	.get_rxnfc		= efx_ethtool_get_rxnfc,
	.set_rxnfc		= efx_ethtool_set_rxnfc,
	.get_rxfh_indir_size	= efx_ethtool_get_rxfh_indir_size,
	.get_rxfh_indir		= efx_ethtool_get_rxfh_indir,
	.set_rxfh_indir		= efx_ethtool_set_rxfh_indir,
	.get_module_info	= efx_ethtool_get_module_info,
	.get_module_eeprom	= efx_ethtool_get_module_eeprom,
};
