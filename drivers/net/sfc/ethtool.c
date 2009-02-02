/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include "net_driver.h"
#include "workarounds.h"
#include "selftest.h"
#include "efx.h"
#include "ethtool.h"
#include "falcon.h"
#include "spi.h"
#include "mdio_10g.h"

const char *efx_loopback_mode_names[] = {
	[LOOPBACK_NONE]		= "NONE",
	[LOOPBACK_GMAC]		= "GMAC",
	[LOOPBACK_XGMII]	= "XGMII",
	[LOOPBACK_XGXS]		= "XGXS",
	[LOOPBACK_XAUI] 	= "XAUI",
	[LOOPBACK_GPHY]		= "GPHY",
	[LOOPBACK_PHYXS]	= "PHYXS",
	[LOOPBACK_PCS]	 	= "PCS",
	[LOOPBACK_PMAPMD]	= "PMA/PMD",
	[LOOPBACK_NETWORK]	= "NETWORK",
};

struct ethtool_string {
	char name[ETH_GSTRING_LEN];
};

struct efx_ethtool_stat {
	const char *name;
	enum {
		EFX_ETHTOOL_STAT_SOURCE_mac_stats,
		EFX_ETHTOOL_STAT_SOURCE_nic,
		EFX_ETHTOOL_STAT_SOURCE_channel
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

static u64 efx_get_ulong_stat(void *field)
{
	return *(unsigned long *)field;
}

static u64 efx_get_u64_stat(void *field)
{
	return *(u64 *) field;
}

static u64 efx_get_atomic_stat(void *field)
{
	return atomic_read((atomic_t *) field);
}

#define EFX_ETHTOOL_ULONG_MAC_STAT(field)			\
	EFX_ETHTOOL_STAT(field, mac_stats, field, 		\
			  unsigned long, efx_get_ulong_stat)

#define EFX_ETHTOOL_U64_MAC_STAT(field)				\
	EFX_ETHTOOL_STAT(field, mac_stats, field, 		\
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

static struct efx_ethtool_stat efx_ethtool_stats[] = {
	EFX_ETHTOOL_U64_MAC_STAT(tx_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(tx_good_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(tx_bad_bytes),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_packets),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_bad),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_pause),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_control),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_unicast),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_multicast),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_broadcast),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_lt64),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_64),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_65_to_127),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_128_to_255),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_256_to_511),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_512_to_1023),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_1024_to_15xx),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_15xx_to_jumbo),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_gtjumbo),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_collision),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_single_collision),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_multiple_collision),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_excessive_collision),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_deferred),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_late_collision),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_excessive_deferred),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_non_tcpudp),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_mac_src_error),
	EFX_ETHTOOL_ULONG_MAC_STAT(tx_ip_src_error),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(rx_good_bytes),
	EFX_ETHTOOL_U64_MAC_STAT(rx_bad_bytes),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_packets),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_good),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_bad),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_pause),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_control),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_unicast),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_multicast),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_broadcast),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_lt64),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_64),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_65_to_127),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_128_to_255),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_256_to_511),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_512_to_1023),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_1024_to_15xx),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_15xx_to_jumbo),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_gtjumbo),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_bad_lt64),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_bad_64_to_15xx),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_bad_15xx_to_jumbo),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_bad_gtjumbo),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_overflow),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_missed),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_false_carrier),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_symbol_error),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_align_error),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_length_error),
	EFX_ETHTOOL_ULONG_MAC_STAT(rx_internal_error),
	EFX_ETHTOOL_UINT_NIC_STAT(rx_nodesc_drop_cnt),
	EFX_ETHTOOL_ATOMIC_NIC_ERROR_STAT(rx_reset),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_tobe_disc),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_ip_hdr_chksum_err),
	EFX_ETHTOOL_UINT_CHANNEL_STAT(rx_tcp_udp_chksum_err),
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
static int efx_ethtool_phys_id(struct net_device *net_dev, u32 count)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	efx->board_info.blink(efx, 1);
	set_current_state(TASK_INTERRUPTIBLE);
	if (count)
		schedule_timeout(count * HZ);
	else
		schedule();
	efx->board_info.blink(efx, 0);
	return 0;
}

/* This must be called with rtnl_lock held. */
int efx_ethtool_get_settings(struct net_device *net_dev,
			     struct ethtool_cmd *ecmd)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	mutex_lock(&efx->mac_lock);
	efx->phy_op->get_settings(efx, ecmd);
	mutex_unlock(&efx->mac_lock);

	/* Falcon GMAC does not support 1000Mbps HD */
	ecmd->supported &= ~SUPPORTED_1000baseT_Half;

	return 0;
}

/* This must be called with rtnl_lock held. */
int efx_ethtool_set_settings(struct net_device *net_dev,
			     struct ethtool_cmd *ecmd)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int rc;

	/* Falcon GMAC does not support 1000Mbps HD */
	if (ecmd->speed == SPEED_1000 && ecmd->duplex != DUPLEX_FULL) {
		EFX_LOG(efx, "rejecting unsupported 1000Mbps HD"
			" setting\n");
		return -EINVAL;
	}

	mutex_lock(&efx->mac_lock);
	rc = efx->phy_op->set_settings(efx, ecmd);
	mutex_unlock(&efx->mac_lock);
	if (!rc)
		efx_reconfigure_port(efx);

	return rc;
}

static void efx_ethtool_get_drvinfo(struct net_device *net_dev,
				    struct ethtool_drvinfo *info)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	strlcpy(info->driver, EFX_DRIVER_NAME, sizeof(info->driver));
	strlcpy(info->version, EFX_DRIVER_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(efx->pci_dev), sizeof(info->bus_info));
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
	"loopback.%s." _counter, LOOPBACK_MODE_NAME(mode)

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
	struct efx_tx_queue *tx_queue;

	efx_for_each_tx_queue(tx_queue, efx) {
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

	efx_fill_test(n++, strings, data, &tests->mii,
		      "core", 0, "mii", NULL);
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
		efx_fill_test(n++, strings, data,
			      &tests->eventq_poll[channel->channel],
			      EFX_CHANNEL_NAME(channel),
			      "eventq.poll", NULL);
	}

	efx_fill_test(n++, strings, data, &tests->registers,
		      "core", 0, "registers", NULL);

	for (i = 0; i < efx->phy_op->num_tests; i++)
		efx_fill_test(n++, strings, data, &tests->phy[i],
			      "phy", 0, efx->phy_op->test_names[i], NULL);

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
			strncpy(ethtool_strings[i].name,
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
	struct efx_ethtool_stat *stat;
	struct efx_channel *channel;
	int i;

	EFX_BUG_ON_PARANOID(stats->n_stats != EFX_ETHTOOL_NUM_STATS);

	/* Update MAC and NIC statistics */
	dev_get_stats(net_dev);

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
		}
	}
}

static int efx_ethtool_set_rx_csum(struct net_device *net_dev, u32 enable)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	/* No way to stop the hardware doing the checks; we just
	 * ignore the result.
	 */
	efx->rx_checksum_enabled = !!enable;

	return 0;
}

static u32 efx_ethtool_get_rx_csum(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	return efx->rx_checksum_enabled;
}

static void efx_ethtool_self_test(struct net_device *net_dev,
				  struct ethtool_test *test, u64 *data)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_self_tests efx_tests;
	int already_up;
	int rc;

	ASSERT_RTNL();
	if (efx->state != STATE_RUNNING) {
		rc = -EIO;
		goto fail1;
	}

	/* We need rx buffers and interrupts. */
	already_up = (efx->net_dev->flags & IFF_UP);
	if (!already_up) {
		rc = dev_open(efx->net_dev);
		if (rc) {
			EFX_ERR(efx, "failed opening device.\n");
			goto fail2;
		}
	}

	memset(&efx_tests, 0, sizeof(efx_tests));

	rc = efx_selftest(efx, &efx_tests, test->flags);

	if (!already_up)
		dev_close(efx->net_dev);

	EFX_LOG(efx, "%s %sline self-tests\n",
		rc == 0 ? "passed" : "failed",
		(test->flags & ETH_TEST_FL_OFFLINE) ? "off" : "on");

 fail2:
 fail1:
	/* Fill ethtool results structures */
	efx_ethtool_fill_self_tests(efx, &efx_tests, NULL, data);
	if (rc)
		test->flags |= ETH_TEST_FL_FAILED;
}

/* Restart autonegotiation */
static int efx_ethtool_nway_reset(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	return mii_nway_restart(&efx->mii);
}

static u32 efx_ethtool_get_link(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	return efx->link_up;
}

static int efx_ethtool_get_eeprom_len(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_spi_device *spi = efx->spi_eeprom;

	if (!spi)
		return 0;
	return min(spi->size, EFX_EEPROM_BOOTCONFIG_END) -
		min(spi->size, EFX_EEPROM_BOOTCONFIG_START);
}

static int efx_ethtool_get_eeprom(struct net_device *net_dev,
				  struct ethtool_eeprom *eeprom, u8 *buf)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_spi_device *spi = efx->spi_eeprom;
	size_t len;
	int rc;

	rc = mutex_lock_interruptible(&efx->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_read(spi, eeprom->offset + EFX_EEPROM_BOOTCONFIG_START,
			     eeprom->len, &len, buf);
	mutex_unlock(&efx->spi_lock);

	eeprom->magic = EFX_ETHTOOL_EEPROM_MAGIC;
	eeprom->len = len;
	return rc;
}

static int efx_ethtool_set_eeprom(struct net_device *net_dev,
				  struct ethtool_eeprom *eeprom, u8 *buf)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_spi_device *spi = efx->spi_eeprom;
	size_t len;
	int rc;

	if (eeprom->magic != EFX_ETHTOOL_EEPROM_MAGIC)
		return -EINVAL;

	rc = mutex_lock_interruptible(&efx->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_write(spi, eeprom->offset + EFX_EEPROM_BOOTCONFIG_START,
			      eeprom->len, &len, buf);
	mutex_unlock(&efx->spi_lock);

	eeprom->len = len;
	return rc;
}

static int efx_ethtool_get_coalesce(struct net_device *net_dev,
				    struct ethtool_coalesce *coalesce)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	struct efx_channel *channel;

	memset(coalesce, 0, sizeof(*coalesce));

	/* Find lowest IRQ moderation across all used TX queues */
	coalesce->tx_coalesce_usecs_irq = ~((u32) 0);
	efx_for_each_tx_queue(tx_queue, efx) {
		channel = tx_queue->channel;
		if (channel->irq_moderation < coalesce->tx_coalesce_usecs_irq) {
			if (channel->used_flags != EFX_USED_BY_RX_TX)
				coalesce->tx_coalesce_usecs_irq =
					channel->irq_moderation;
			else
				coalesce->tx_coalesce_usecs_irq = 0;
		}
	}

	/* Find lowest IRQ moderation across all used RX queues */
	coalesce->rx_coalesce_usecs_irq = ~((u32) 0);
	efx_for_each_rx_queue(rx_queue, efx) {
		channel = rx_queue->channel;
		if (channel->irq_moderation < coalesce->rx_coalesce_usecs_irq)
			coalesce->rx_coalesce_usecs_irq =
				channel->irq_moderation;
	}

	return 0;
}

/* Set coalescing parameters
 * The difficulties occur for shared channels
 */
static int efx_ethtool_set_coalesce(struct net_device *net_dev,
				    struct ethtool_coalesce *coalesce)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_channel *channel;
	struct efx_tx_queue *tx_queue;
	unsigned tx_usecs, rx_usecs;

	if (coalesce->use_adaptive_rx_coalesce ||
	    coalesce->use_adaptive_tx_coalesce)
		return -EOPNOTSUPP;

	if (coalesce->rx_coalesce_usecs || coalesce->tx_coalesce_usecs) {
		EFX_ERR(efx, "invalid coalescing setting. "
			"Only rx/tx_coalesce_usecs_irq are supported\n");
		return -EOPNOTSUPP;
	}

	rx_usecs = coalesce->rx_coalesce_usecs_irq;
	tx_usecs = coalesce->tx_coalesce_usecs_irq;

	/* If the channel is shared only allow RX parameters to be set */
	efx_for_each_tx_queue(tx_queue, efx) {
		if ((tx_queue->channel->used_flags == EFX_USED_BY_RX_TX) &&
		    tx_usecs) {
			EFX_ERR(efx, "Channel is shared. "
				"Only RX coalescing may be set\n");
			return -EOPNOTSUPP;
		}
	}

	efx_init_irq_moderation(efx, tx_usecs, rx_usecs);

	/* Reset channel to pick up new moderation value.  Note that
	 * this may change the value of the irq_moderation field
	 * (e.g. to allow for hardware timer granularity).
	 */
	efx_for_each_channel(channel, efx)
		falcon_set_int_moderation(channel);

	return 0;
}

static int efx_ethtool_set_pauseparam(struct net_device *net_dev,
				      struct ethtool_pauseparam *pause)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	enum efx_fc_type wanted_fc;
	bool reset;

	wanted_fc = ((pause->rx_pause ? EFX_FC_RX : 0) |
		     (pause->tx_pause ? EFX_FC_TX : 0) |
		     (pause->autoneg ? EFX_FC_AUTO : 0));

	if ((wanted_fc & EFX_FC_TX) && !(wanted_fc & EFX_FC_RX)) {
		EFX_LOG(efx, "Flow control unsupported: tx ON rx OFF\n");
		return -EINVAL;
	}

	if (!(efx->phy_op->mmds & DEV_PRESENT_BIT(MDIO_MMD_AN)) &&
	    (wanted_fc & EFX_FC_AUTO)) {
		EFX_LOG(efx, "PHY does not support flow control "
			"autonegotiation\n");
		return -EINVAL;
	}

	/* TX flow control may automatically turn itself off if the
	 * link partner (intermittently) stops responding to pause
	 * frames. There isn't any indication that this has happened,
	 * so the best we do is leave it up to the user to spot this
	 * and fix it be cycling transmit flow control on this end. */
	reset = (wanted_fc & EFX_FC_TX) && !(efx->wanted_fc & EFX_FC_TX);
	if (EFX_WORKAROUND_11482(efx) && reset) {
		if (falcon_rev(efx) >= FALCON_REV_B0) {
			/* Recover by resetting the EM block */
			if (efx->link_up)
				falcon_drain_tx_fifo(efx);
		} else {
			/* Schedule a reset to recover */
			efx_schedule_reset(efx, RESET_TYPE_INVISIBLE);
		}
	}

	/* Try to push the pause parameters */
	mutex_lock(&efx->mac_lock);

	efx->wanted_fc = wanted_fc;
	mdio_clause45_set_pause(efx);
	__efx_reconfigure_port(efx);

	mutex_unlock(&efx->mac_lock);

	return 0;
}

static void efx_ethtool_get_pauseparam(struct net_device *net_dev,
				       struct ethtool_pauseparam *pause)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	pause->rx_pause = !!(efx->wanted_fc & EFX_FC_RX);
	pause->tx_pause = !!(efx->wanted_fc & EFX_FC_TX);
	pause->autoneg = !!(efx->wanted_fc & EFX_FC_AUTO);
}


struct ethtool_ops efx_ethtool_ops = {
	.get_settings		= efx_ethtool_get_settings,
	.set_settings		= efx_ethtool_set_settings,
	.get_drvinfo		= efx_ethtool_get_drvinfo,
	.nway_reset		= efx_ethtool_nway_reset,
	.get_link		= efx_ethtool_get_link,
	.get_eeprom_len		= efx_ethtool_get_eeprom_len,
	.get_eeprom		= efx_ethtool_get_eeprom,
	.set_eeprom		= efx_ethtool_set_eeprom,
	.get_coalesce		= efx_ethtool_get_coalesce,
	.set_coalesce		= efx_ethtool_set_coalesce,
	.get_pauseparam         = efx_ethtool_get_pauseparam,
	.set_pauseparam         = efx_ethtool_set_pauseparam,
	.get_rx_csum		= efx_ethtool_get_rx_csum,
	.set_rx_csum		= efx_ethtool_set_rx_csum,
	.get_tx_csum		= ethtool_op_get_tx_csum,
	.set_tx_csum		= ethtool_op_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= ethtool_op_set_tso,
	.get_flags		= ethtool_op_get_flags,
	.set_flags		= ethtool_op_set_flags,
	.get_sset_count		= efx_ethtool_get_sset_count,
	.self_test		= efx_ethtool_self_test,
	.get_strings		= efx_ethtool_get_strings,
	.phys_id		= efx_ethtool_phys_id,
	.get_ethtool_stats	= efx_ethtool_get_stats,
};
