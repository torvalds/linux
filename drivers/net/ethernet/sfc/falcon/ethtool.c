// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
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

struct ef4_sw_stat_desc {
	const char *name;
	enum {
		EF4_ETHTOOL_STAT_SOURCE_nic,
		EF4_ETHTOOL_STAT_SOURCE_channel,
		EF4_ETHTOOL_STAT_SOURCE_tx_queue
	} source;
	unsigned offset;
	u64(*get_stat) (void *field); /* Reader function */
};

/* Initialiser for a struct ef4_sw_stat_desc with type-checking */
#define EF4_ETHTOOL_STAT(stat_name, source_name, field, field_type, \
				get_stat_function) {			\
	.name = #stat_name,						\
	.source = EF4_ETHTOOL_STAT_SOURCE_##source_name,		\
	.offset = ((((field_type *) 0) ==				\
		      &((struct ef4_##source_name *)0)->field) ?	\
		    offsetof(struct ef4_##source_name, field) :		\
		    offsetof(struct ef4_##source_name, field)),		\
	.get_stat = get_stat_function,					\
}

static u64 ef4_get_uint_stat(void *field)
{
	return *(unsigned int *)field;
}

static u64 ef4_get_atomic_stat(void *field)
{
	return atomic_read((atomic_t *) field);
}

#define EF4_ETHTOOL_ATOMIC_NIC_ERROR_STAT(field)		\
	EF4_ETHTOOL_STAT(field, nic, field,			\
			 atomic_t, ef4_get_atomic_stat)

#define EF4_ETHTOOL_UINT_CHANNEL_STAT(field)			\
	EF4_ETHTOOL_STAT(field, channel, n_##field,		\
			 unsigned int, ef4_get_uint_stat)

#define EF4_ETHTOOL_UINT_TXQ_STAT(field)			\
	EF4_ETHTOOL_STAT(tx_##field, tx_queue, field,		\
			 unsigned int, ef4_get_uint_stat)

static const struct ef4_sw_stat_desc ef4_sw_stat_desc[] = {
	EF4_ETHTOOL_UINT_TXQ_STAT(merge_events),
	EF4_ETHTOOL_UINT_TXQ_STAT(pushes),
	EF4_ETHTOOL_UINT_TXQ_STAT(cb_packets),
	EF4_ETHTOOL_ATOMIC_NIC_ERROR_STAT(rx_reset),
	EF4_ETHTOOL_UINT_CHANNEL_STAT(rx_tobe_disc),
	EF4_ETHTOOL_UINT_CHANNEL_STAT(rx_ip_hdr_chksum_err),
	EF4_ETHTOOL_UINT_CHANNEL_STAT(rx_tcp_udp_chksum_err),
	EF4_ETHTOOL_UINT_CHANNEL_STAT(rx_mcast_mismatch),
	EF4_ETHTOOL_UINT_CHANNEL_STAT(rx_frm_trunc),
	EF4_ETHTOOL_UINT_CHANNEL_STAT(rx_merge_events),
	EF4_ETHTOOL_UINT_CHANNEL_STAT(rx_merge_packets),
};

#define EF4_ETHTOOL_SW_STAT_COUNT ARRAY_SIZE(ef4_sw_stat_desc)

#define EF4_ETHTOOL_EEPROM_MAGIC 0xEFAB

/**************************************************************************
 *
 * Ethtool operations
 *
 **************************************************************************
 */

/* Identify device by flashing LEDs */
static int ef4_ethtool_phys_id(struct net_device *net_dev,
			       enum ethtool_phys_id_state state)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	enum ef4_led_mode mode = EF4_LED_DEFAULT;

	switch (state) {
	case ETHTOOL_ID_ON:
		mode = EF4_LED_ON;
		break;
	case ETHTOOL_ID_OFF:
		mode = EF4_LED_OFF;
		break;
	case ETHTOOL_ID_INACTIVE:
		mode = EF4_LED_DEFAULT;
		break;
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */
	}

	efx->type->set_id_led(efx, mode);
	return 0;
}

/* This must be called with rtnl_lock held. */
static int
ef4_ethtool_get_link_ksettings(struct net_device *net_dev,
			       struct ethtool_link_ksettings *cmd)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	struct ef4_link_state *link_state = &efx->link_state;

	mutex_lock(&efx->mac_lock);
	efx->phy_op->get_link_ksettings(efx, cmd);
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
static int
ef4_ethtool_set_link_ksettings(struct net_device *net_dev,
			       const struct ethtool_link_ksettings *cmd)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	int rc;

	/* GMAC does not support 1000Mbps HD */
	if ((cmd->base.speed == SPEED_1000) &&
	    (cmd->base.duplex != DUPLEX_FULL)) {
		netif_dbg(efx, drv, efx->net_dev,
			  "rejecting unsupported 1000Mbps HD setting\n");
		return -EINVAL;
	}

	mutex_lock(&efx->mac_lock);
	rc = efx->phy_op->set_link_ksettings(efx, cmd);
	mutex_unlock(&efx->mac_lock);
	return rc;
}

static void ef4_ethtool_get_drvinfo(struct net_device *net_dev,
				    struct ethtool_drvinfo *info)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	strscpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strscpy(info->version, EF4_DRIVER_VERSION, sizeof(info->version));
	strscpy(info->bus_info, pci_name(efx->pci_dev), sizeof(info->bus_info));
}

static int ef4_ethtool_get_regs_len(struct net_device *net_dev)
{
	return ef4_nic_get_regs_len(netdev_priv(net_dev));
}

static void ef4_ethtool_get_regs(struct net_device *net_dev,
				 struct ethtool_regs *regs, void *buf)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	regs->version = efx->type->revision;
	ef4_nic_get_regs(efx, buf);
}

static u32 ef4_ethtool_get_msglevel(struct net_device *net_dev)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	return efx->msg_enable;
}

static void ef4_ethtool_set_msglevel(struct net_device *net_dev, u32 msg_enable)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	efx->msg_enable = msg_enable;
}

/**
 * ef4_fill_test - fill in an individual self-test entry
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
static void ef4_fill_test(unsigned int test_index, u8 *strings, u64 *data,
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

#define EF4_CHANNEL_NAME(_channel) "chan%d", _channel->channel
#define EF4_TX_QUEUE_NAME(_tx_queue) "txq%d", _tx_queue->queue
#define EF4_RX_QUEUE_NAME(_rx_queue) "rxq%d", _rx_queue->queue
#define EF4_LOOPBACK_NAME(_mode, _counter)			\
	"loopback.%s." _counter, STRING_TABLE_LOOKUP(_mode, ef4_loopback_mode)

/**
 * ef4_fill_loopback_test - fill in a block of loopback self-test entries
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
static int ef4_fill_loopback_test(struct ef4_nic *efx,
				  struct ef4_loopback_self_tests *lb_tests,
				  enum ef4_loopback_mode mode,
				  unsigned int test_index,
				  u8 *strings, u64 *data)
{
	struct ef4_channel *channel =
		ef4_get_channel(efx, efx->tx_channel_offset);
	struct ef4_tx_queue *tx_queue;

	ef4_for_each_channel_tx_queue(tx_queue, channel) {
		ef4_fill_test(test_index++, strings, data,
			      &lb_tests->tx_sent[tx_queue->queue],
			      EF4_TX_QUEUE_NAME(tx_queue),
			      EF4_LOOPBACK_NAME(mode, "tx_sent"));
		ef4_fill_test(test_index++, strings, data,
			      &lb_tests->tx_done[tx_queue->queue],
			      EF4_TX_QUEUE_NAME(tx_queue),
			      EF4_LOOPBACK_NAME(mode, "tx_done"));
	}
	ef4_fill_test(test_index++, strings, data,
		      &lb_tests->rx_good,
		      "rx", 0,
		      EF4_LOOPBACK_NAME(mode, "rx_good"));
	ef4_fill_test(test_index++, strings, data,
		      &lb_tests->rx_bad,
		      "rx", 0,
		      EF4_LOOPBACK_NAME(mode, "rx_bad"));

	return test_index;
}

/**
 * ef4_ethtool_fill_self_tests - get self-test details
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
static int ef4_ethtool_fill_self_tests(struct ef4_nic *efx,
				       struct ef4_self_tests *tests,
				       u8 *strings, u64 *data)
{
	struct ef4_channel *channel;
	unsigned int n = 0, i;
	enum ef4_loopback_mode mode;

	ef4_fill_test(n++, strings, data, &tests->phy_alive,
		      "phy", 0, "alive", NULL);
	ef4_fill_test(n++, strings, data, &tests->nvram,
		      "core", 0, "nvram", NULL);
	ef4_fill_test(n++, strings, data, &tests->interrupt,
		      "core", 0, "interrupt", NULL);

	/* Event queues */
	ef4_for_each_channel(channel, efx) {
		ef4_fill_test(n++, strings, data,
			      &tests->eventq_dma[channel->channel],
			      EF4_CHANNEL_NAME(channel),
			      "eventq.dma", NULL);
		ef4_fill_test(n++, strings, data,
			      &tests->eventq_int[channel->channel],
			      EF4_CHANNEL_NAME(channel),
			      "eventq.int", NULL);
	}

	ef4_fill_test(n++, strings, data, &tests->memory,
		      "core", 0, "memory", NULL);
	ef4_fill_test(n++, strings, data, &tests->registers,
		      "core", 0, "registers", NULL);

	if (efx->phy_op->run_tests != NULL) {
		EF4_BUG_ON_PARANOID(efx->phy_op->test_name == NULL);

		for (i = 0; true; ++i) {
			const char *name;

			EF4_BUG_ON_PARANOID(i >= EF4_MAX_PHY_TESTS);
			name = efx->phy_op->test_name(efx, i);
			if (name == NULL)
				break;

			ef4_fill_test(n++, strings, data, &tests->phy_ext[i],
				      "phy", 0, name, NULL);
		}
	}

	/* Loopback tests */
	for (mode = LOOPBACK_NONE; mode <= LOOPBACK_TEST_MAX; mode++) {
		if (!(efx->loopback_modes & (1 << mode)))
			continue;
		n = ef4_fill_loopback_test(efx,
					   &tests->loopback[mode], mode, n,
					   strings, data);
	}

	return n;
}

static size_t ef4_describe_per_queue_stats(struct ef4_nic *efx, u8 **strings)
{
	size_t n_stats = 0;
	struct ef4_channel *channel;

	ef4_for_each_channel(channel, efx) {
		if (ef4_channel_has_tx_queues(channel)) {
			n_stats++;
			if (!strings)
				continue;

			ethtool_sprintf(strings, "tx-%u.tx_packets",
					channel->tx_queue[0].queue /
						EF4_TXQ_TYPES);
		}
	}
	ef4_for_each_channel(channel, efx) {
		if (ef4_channel_has_rx_queue(channel)) {
			n_stats++;
			if (!strings)
				continue;

			ethtool_sprintf(strings, "rx-%d.rx_packets",
					channel->channel);
		}
	}
	return n_stats;
}

static int ef4_ethtool_get_sset_count(struct net_device *net_dev,
				      int string_set)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	switch (string_set) {
	case ETH_SS_STATS:
		return efx->type->describe_stats(efx, NULL) +
		       EF4_ETHTOOL_SW_STAT_COUNT +
		       ef4_describe_per_queue_stats(efx, NULL);
	case ETH_SS_TEST:
		return ef4_ethtool_fill_self_tests(efx, NULL, NULL, NULL);
	default:
		return -EINVAL;
	}
}

static void ef4_ethtool_get_strings(struct net_device *net_dev,
				    u32 string_set, u8 *strings)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	int i;

	switch (string_set) {
	case ETH_SS_STATS:
		efx->type->describe_stats(efx, &strings);
		for (i = 0; i < EF4_ETHTOOL_SW_STAT_COUNT; i++)
			ethtool_puts(&strings, ef4_sw_stat_desc[i].name);
		ef4_describe_per_queue_stats(efx, &strings);
		break;
	case ETH_SS_TEST:
		ef4_ethtool_fill_self_tests(efx, NULL, strings, NULL);
		break;
	default:
		/* No other string sets */
		break;
	}
}

static void ef4_ethtool_get_stats(struct net_device *net_dev,
				  struct ethtool_stats *stats,
				  u64 *data)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	const struct ef4_sw_stat_desc *stat;
	struct ef4_channel *channel;
	struct ef4_tx_queue *tx_queue;
	struct ef4_rx_queue *rx_queue;
	int i;

	spin_lock_bh(&efx->stats_lock);

	/* Get NIC statistics */
	data += efx->type->update_stats(efx, data, NULL);

	/* Get software statistics */
	for (i = 0; i < EF4_ETHTOOL_SW_STAT_COUNT; i++) {
		stat = &ef4_sw_stat_desc[i];
		switch (stat->source) {
		case EF4_ETHTOOL_STAT_SOURCE_nic:
			data[i] = stat->get_stat((void *)efx + stat->offset);
			break;
		case EF4_ETHTOOL_STAT_SOURCE_channel:
			data[i] = 0;
			ef4_for_each_channel(channel, efx)
				data[i] += stat->get_stat((void *)channel +
							  stat->offset);
			break;
		case EF4_ETHTOOL_STAT_SOURCE_tx_queue:
			data[i] = 0;
			ef4_for_each_channel(channel, efx) {
				ef4_for_each_channel_tx_queue(tx_queue, channel)
					data[i] +=
						stat->get_stat((void *)tx_queue
							       + stat->offset);
			}
			break;
		}
	}
	data += EF4_ETHTOOL_SW_STAT_COUNT;

	spin_unlock_bh(&efx->stats_lock);

	ef4_for_each_channel(channel, efx) {
		if (ef4_channel_has_tx_queues(channel)) {
			*data = 0;
			ef4_for_each_channel_tx_queue(tx_queue, channel) {
				*data += tx_queue->tx_packets;
			}
			data++;
		}
	}
	ef4_for_each_channel(channel, efx) {
		if (ef4_channel_has_rx_queue(channel)) {
			*data = 0;
			ef4_for_each_channel_rx_queue(rx_queue, channel) {
				*data += rx_queue->rx_packets;
			}
			data++;
		}
	}
}

static void ef4_ethtool_self_test(struct net_device *net_dev,
				  struct ethtool_test *test, u64 *data)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	struct ef4_self_tests *ef4_tests;
	bool already_up;
	int rc = -ENOMEM;

	ef4_tests = kzalloc(sizeof(*ef4_tests), GFP_KERNEL);
	if (!ef4_tests)
		goto fail;

	if (efx->state != STATE_READY) {
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

	rc = ef4_selftest(efx, ef4_tests, test->flags);

	if (!already_up)
		dev_close(efx->net_dev);

	netif_info(efx, drv, efx->net_dev, "%s %sline self-tests\n",
		   rc == 0 ? "passed" : "failed",
		   (test->flags & ETH_TEST_FL_OFFLINE) ? "off" : "on");

out:
	ef4_ethtool_fill_self_tests(efx, ef4_tests, NULL, data);
	kfree(ef4_tests);
fail:
	if (rc)
		test->flags |= ETH_TEST_FL_FAILED;
}

/* Restart autonegotiation */
static int ef4_ethtool_nway_reset(struct net_device *net_dev)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

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

static int ef4_ethtool_get_coalesce(struct net_device *net_dev,
				    struct ethtool_coalesce *coalesce,
				    struct kernel_ethtool_coalesce *kernel_coal,
				    struct netlink_ext_ack *extack)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	unsigned int tx_usecs, rx_usecs;
	bool rx_adaptive;

	ef4_get_irq_moderation(efx, &tx_usecs, &rx_usecs, &rx_adaptive);

	coalesce->tx_coalesce_usecs = tx_usecs;
	coalesce->tx_coalesce_usecs_irq = tx_usecs;
	coalesce->rx_coalesce_usecs = rx_usecs;
	coalesce->rx_coalesce_usecs_irq = rx_usecs;
	coalesce->use_adaptive_rx_coalesce = rx_adaptive;

	return 0;
}

static int ef4_ethtool_set_coalesce(struct net_device *net_dev,
				    struct ethtool_coalesce *coalesce,
				    struct kernel_ethtool_coalesce *kernel_coal,
				    struct netlink_ext_ack *extack)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	struct ef4_channel *channel;
	unsigned int tx_usecs, rx_usecs;
	bool adaptive, rx_may_override_tx;
	int rc;

	ef4_get_irq_moderation(efx, &tx_usecs, &rx_usecs, &adaptive);

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

	rc = ef4_init_irq_moderation(efx, tx_usecs, rx_usecs, adaptive,
				     rx_may_override_tx);
	if (rc != 0)
		return rc;

	ef4_for_each_channel(channel, efx)
		efx->type->push_irq_moderation(channel);

	return 0;
}

static void
ef4_ethtool_get_ringparam(struct net_device *net_dev,
			  struct ethtool_ringparam *ring,
			  struct kernel_ethtool_ringparam *kernel_ring,
			  struct netlink_ext_ack *extack)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	ring->rx_max_pending = EF4_MAX_DMAQ_SIZE;
	ring->tx_max_pending = EF4_MAX_DMAQ_SIZE;
	ring->rx_pending = efx->rxq_entries;
	ring->tx_pending = efx->txq_entries;
}

static int
ef4_ethtool_set_ringparam(struct net_device *net_dev,
			  struct ethtool_ringparam *ring,
			  struct kernel_ethtool_ringparam *kernel_ring,
			  struct netlink_ext_ack *extack)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	u32 txq_entries;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending ||
	    ring->rx_pending > EF4_MAX_DMAQ_SIZE ||
	    ring->tx_pending > EF4_MAX_DMAQ_SIZE)
		return -EINVAL;

	if (ring->rx_pending < EF4_RXQ_MIN_ENT) {
		netif_err(efx, drv, efx->net_dev,
			  "RX queues cannot be smaller than %u\n",
			  EF4_RXQ_MIN_ENT);
		return -EINVAL;
	}

	txq_entries = max(ring->tx_pending, EF4_TXQ_MIN_ENT(efx));
	if (txq_entries != ring->tx_pending)
		netif_warn(efx, drv, efx->net_dev,
			   "increasing TX queue size to minimum of %u\n",
			   txq_entries);

	return ef4_realloc_channels(efx, ring->rx_pending, txq_entries);
}

static int ef4_ethtool_set_pauseparam(struct net_device *net_dev,
				      struct ethtool_pauseparam *pause)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	u8 wanted_fc, old_fc;
	u32 old_adv;
	int rc = 0;

	mutex_lock(&efx->mac_lock);

	wanted_fc = ((pause->rx_pause ? EF4_FC_RX : 0) |
		     (pause->tx_pause ? EF4_FC_TX : 0) |
		     (pause->autoneg ? EF4_FC_AUTO : 0));

	if ((wanted_fc & EF4_FC_TX) && !(wanted_fc & EF4_FC_RX)) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Flow control unsupported: tx ON rx OFF\n");
		rc = -EINVAL;
		goto out;
	}

	if ((wanted_fc & EF4_FC_AUTO) && !efx->link_advertising) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Autonegotiation is disabled\n");
		rc = -EINVAL;
		goto out;
	}

	/* Hook for Falcon bug 11482 workaround */
	if (efx->type->prepare_enable_fc_tx &&
	    (wanted_fc & EF4_FC_TX) && !(efx->wanted_fc & EF4_FC_TX))
		efx->type->prepare_enable_fc_tx(efx);

	old_adv = efx->link_advertising;
	old_fc = efx->wanted_fc;
	ef4_link_set_wanted_fc(efx, wanted_fc);
	if (efx->link_advertising != old_adv ||
	    (efx->wanted_fc ^ old_fc) & EF4_FC_AUTO) {
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
	ef4_mac_reconfigure(efx);

out:
	mutex_unlock(&efx->mac_lock);

	return rc;
}

static void ef4_ethtool_get_pauseparam(struct net_device *net_dev,
				       struct ethtool_pauseparam *pause)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	pause->rx_pause = !!(efx->wanted_fc & EF4_FC_RX);
	pause->tx_pause = !!(efx->wanted_fc & EF4_FC_TX);
	pause->autoneg = !!(efx->wanted_fc & EF4_FC_AUTO);
}

static void ef4_ethtool_get_wol(struct net_device *net_dev,
				struct ethtool_wolinfo *wol)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	return efx->type->get_wol(efx, wol);
}


static int ef4_ethtool_set_wol(struct net_device *net_dev,
			       struct ethtool_wolinfo *wol)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	return efx->type->set_wol(efx, wol->wolopts);
}

static int ef4_ethtool_reset(struct net_device *net_dev, u32 *flags)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	int rc;

	rc = efx->type->map_reset_flags(flags);
	if (rc < 0)
		return rc;

	return ef4_reset(efx, rc);
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

static int ef4_ethtool_get_class_rule(struct ef4_nic *efx,
				      struct ethtool_rx_flow_spec *rule)
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
	struct ef4_filter_spec spec;
	int rc;

	rc = ef4_filter_get_filter_safe(efx, EF4_FILTER_PRI_MANUAL,
					rule->location, &spec);
	if (rc)
		return rc;

	if (spec.dmaq_id == EF4_FILTER_RX_DMAQ_ID_DROP)
		rule->ring_cookie = RX_CLS_FLOW_DISC;
	else
		rule->ring_cookie = spec.dmaq_id;

	if ((spec.match_flags & EF4_FILTER_MATCH_ETHER_TYPE) &&
	    spec.ether_type == htons(ETH_P_IP) &&
	    (spec.match_flags & EF4_FILTER_MATCH_IP_PROTO) &&
	    (spec.ip_proto == IPPROTO_TCP || spec.ip_proto == IPPROTO_UDP) &&
	    !(spec.match_flags &
	      ~(EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_OUTER_VID |
		EF4_FILTER_MATCH_LOC_HOST | EF4_FILTER_MATCH_REM_HOST |
		EF4_FILTER_MATCH_IP_PROTO |
		EF4_FILTER_MATCH_LOC_PORT | EF4_FILTER_MATCH_REM_PORT))) {
		rule->flow_type = ((spec.ip_proto == IPPROTO_TCP) ?
				   TCP_V4_FLOW : UDP_V4_FLOW);
		if (spec.match_flags & EF4_FILTER_MATCH_LOC_HOST) {
			ip_entry->ip4dst = spec.loc_host[0];
			ip_mask->ip4dst = IP4_ADDR_FULL_MASK;
		}
		if (spec.match_flags & EF4_FILTER_MATCH_REM_HOST) {
			ip_entry->ip4src = spec.rem_host[0];
			ip_mask->ip4src = IP4_ADDR_FULL_MASK;
		}
		if (spec.match_flags & EF4_FILTER_MATCH_LOC_PORT) {
			ip_entry->pdst = spec.loc_port;
			ip_mask->pdst = PORT_FULL_MASK;
		}
		if (spec.match_flags & EF4_FILTER_MATCH_REM_PORT) {
			ip_entry->psrc = spec.rem_port;
			ip_mask->psrc = PORT_FULL_MASK;
		}
	} else if ((spec.match_flags & EF4_FILTER_MATCH_ETHER_TYPE) &&
	    spec.ether_type == htons(ETH_P_IPV6) &&
	    (spec.match_flags & EF4_FILTER_MATCH_IP_PROTO) &&
	    (spec.ip_proto == IPPROTO_TCP || spec.ip_proto == IPPROTO_UDP) &&
	    !(spec.match_flags &
	      ~(EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_OUTER_VID |
		EF4_FILTER_MATCH_LOC_HOST | EF4_FILTER_MATCH_REM_HOST |
		EF4_FILTER_MATCH_IP_PROTO |
		EF4_FILTER_MATCH_LOC_PORT | EF4_FILTER_MATCH_REM_PORT))) {
		rule->flow_type = ((spec.ip_proto == IPPROTO_TCP) ?
				   TCP_V6_FLOW : UDP_V6_FLOW);
		if (spec.match_flags & EF4_FILTER_MATCH_LOC_HOST) {
			memcpy(ip6_entry->ip6dst, spec.loc_host,
			       sizeof(ip6_entry->ip6dst));
			ip6_fill_mask(ip6_mask->ip6dst);
		}
		if (spec.match_flags & EF4_FILTER_MATCH_REM_HOST) {
			memcpy(ip6_entry->ip6src, spec.rem_host,
			       sizeof(ip6_entry->ip6src));
			ip6_fill_mask(ip6_mask->ip6src);
		}
		if (spec.match_flags & EF4_FILTER_MATCH_LOC_PORT) {
			ip6_entry->pdst = spec.loc_port;
			ip6_mask->pdst = PORT_FULL_MASK;
		}
		if (spec.match_flags & EF4_FILTER_MATCH_REM_PORT) {
			ip6_entry->psrc = spec.rem_port;
			ip6_mask->psrc = PORT_FULL_MASK;
		}
	} else if (!(spec.match_flags &
		     ~(EF4_FILTER_MATCH_LOC_MAC | EF4_FILTER_MATCH_LOC_MAC_IG |
		       EF4_FILTER_MATCH_REM_MAC | EF4_FILTER_MATCH_ETHER_TYPE |
		       EF4_FILTER_MATCH_OUTER_VID))) {
		rule->flow_type = ETHER_FLOW;
		if (spec.match_flags &
		    (EF4_FILTER_MATCH_LOC_MAC | EF4_FILTER_MATCH_LOC_MAC_IG)) {
			ether_addr_copy(mac_entry->h_dest, spec.loc_mac);
			if (spec.match_flags & EF4_FILTER_MATCH_LOC_MAC)
				eth_broadcast_addr(mac_mask->h_dest);
			else
				ether_addr_copy(mac_mask->h_dest,
						mac_addr_ig_mask);
		}
		if (spec.match_flags & EF4_FILTER_MATCH_REM_MAC) {
			ether_addr_copy(mac_entry->h_source, spec.rem_mac);
			eth_broadcast_addr(mac_mask->h_source);
		}
		if (spec.match_flags & EF4_FILTER_MATCH_ETHER_TYPE) {
			mac_entry->h_proto = spec.ether_type;
			mac_mask->h_proto = ETHER_TYPE_FULL_MASK;
		}
	} else if (spec.match_flags & EF4_FILTER_MATCH_ETHER_TYPE &&
		   spec.ether_type == htons(ETH_P_IP) &&
		   !(spec.match_flags &
		     ~(EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_OUTER_VID |
		       EF4_FILTER_MATCH_LOC_HOST | EF4_FILTER_MATCH_REM_HOST |
		       EF4_FILTER_MATCH_IP_PROTO))) {
		rule->flow_type = IPV4_USER_FLOW;
		uip_entry->ip_ver = ETH_RX_NFC_IP4;
		if (spec.match_flags & EF4_FILTER_MATCH_IP_PROTO) {
			uip_mask->proto = IP_PROTO_FULL_MASK;
			uip_entry->proto = spec.ip_proto;
		}
		if (spec.match_flags & EF4_FILTER_MATCH_LOC_HOST) {
			uip_entry->ip4dst = spec.loc_host[0];
			uip_mask->ip4dst = IP4_ADDR_FULL_MASK;
		}
		if (spec.match_flags & EF4_FILTER_MATCH_REM_HOST) {
			uip_entry->ip4src = spec.rem_host[0];
			uip_mask->ip4src = IP4_ADDR_FULL_MASK;
		}
	} else if (spec.match_flags & EF4_FILTER_MATCH_ETHER_TYPE &&
		   spec.ether_type == htons(ETH_P_IPV6) &&
		   !(spec.match_flags &
		     ~(EF4_FILTER_MATCH_ETHER_TYPE | EF4_FILTER_MATCH_OUTER_VID |
		       EF4_FILTER_MATCH_LOC_HOST | EF4_FILTER_MATCH_REM_HOST |
		       EF4_FILTER_MATCH_IP_PROTO))) {
		rule->flow_type = IPV6_USER_FLOW;
		if (spec.match_flags & EF4_FILTER_MATCH_IP_PROTO) {
			uip6_mask->l4_proto = IP_PROTO_FULL_MASK;
			uip6_entry->l4_proto = spec.ip_proto;
		}
		if (spec.match_flags & EF4_FILTER_MATCH_LOC_HOST) {
			memcpy(uip6_entry->ip6dst, spec.loc_host,
			       sizeof(uip6_entry->ip6dst));
			ip6_fill_mask(uip6_mask->ip6dst);
		}
		if (spec.match_flags & EF4_FILTER_MATCH_REM_HOST) {
			memcpy(uip6_entry->ip6src, spec.rem_host,
			       sizeof(uip6_entry->ip6src));
			ip6_fill_mask(uip6_mask->ip6src);
		}
	} else {
		/* The above should handle all filters that we insert */
		WARN_ON(1);
		return -EINVAL;
	}

	if (spec.match_flags & EF4_FILTER_MATCH_OUTER_VID) {
		rule->flow_type |= FLOW_EXT;
		rule->h_ext.vlan_tci = spec.outer_vid;
		rule->m_ext.vlan_tci = htons(0xfff);
	}

	return rc;
}

static int
ef4_ethtool_get_rxfh_fields(struct net_device *net_dev,
			    struct ethtool_rxfh_fields *info)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	info->data = 0;
	/* Falcon A0 and A1 had a 4-tuple hash for TCP and UDP, but it was
	 * broken so we do not enable it.
	 * Falcon B0 adds a Toeplitz hash, 4-tuple for TCP and 2-tuple for
	 * other IPv4, including UDP.
	 * See falcon_init_rx_cfg().
	 */
	if (ef4_nic_rev(efx) < EF4_REV_FALCON_B0)
		return 0;
	switch (info->flow_type) {
	case TCP_V4_FLOW:
		info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		fallthrough;
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case IPV4_FLOW:
		info->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		break;
	}
	return 0;
}

static int
ef4_ethtool_get_rxnfc(struct net_device *net_dev,
		      struct ethtool_rxnfc *info, u32 *rule_locs)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = efx->n_rx_channels;
		return 0;

	case ETHTOOL_GRXCLSRLCNT:
		info->data = ef4_filter_get_rx_id_limit(efx);
		if (info->data == 0)
			return -EOPNOTSUPP;
		info->data |= RX_CLS_LOC_SPECIAL;
		info->rule_cnt =
			ef4_filter_count_rx_used(efx, EF4_FILTER_PRI_MANUAL);
		return 0;

	case ETHTOOL_GRXCLSRULE:
		if (ef4_filter_get_rx_id_limit(efx) == 0)
			return -EOPNOTSUPP;
		return ef4_ethtool_get_class_rule(efx, &info->fs);

	case ETHTOOL_GRXCLSRLALL: {
		s32 rc;
		info->data = ef4_filter_get_rx_id_limit(efx);
		if (info->data == 0)
			return -EOPNOTSUPP;
		rc = ef4_filter_get_rx_ids(efx, EF4_FILTER_PRI_MANUAL,
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

static inline bool ip6_mask_is_full(__be32 mask[4])
{
	return !~(mask[0] & mask[1] & mask[2] & mask[3]);
}

static inline bool ip6_mask_is_empty(__be32 mask[4])
{
	return !(mask[0] | mask[1] | mask[2] | mask[3]);
}

static int ef4_ethtool_set_class_rule(struct ef4_nic *efx,
				      struct ethtool_rx_flow_spec *rule)
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
	struct ef4_filter_spec spec;
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

	ef4_filter_init_rx(&spec, EF4_FILTER_PRI_MANUAL,
			   efx->rx_scatter ? EF4_FILTER_FLAG_RX_SCATTER : 0,
			   (rule->ring_cookie == RX_CLS_FLOW_DISC) ?
			   EF4_FILTER_RX_DMAQ_ID_DROP : rule->ring_cookie);

	switch (rule->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		spec.match_flags = (EF4_FILTER_MATCH_ETHER_TYPE |
				    EF4_FILTER_MATCH_IP_PROTO);
		spec.ether_type = htons(ETH_P_IP);
		spec.ip_proto = ((rule->flow_type & ~FLOW_EXT) == TCP_V4_FLOW ?
				 IPPROTO_TCP : IPPROTO_UDP);
		if (ip_mask->ip4dst) {
			if (ip_mask->ip4dst != IP4_ADDR_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_LOC_HOST;
			spec.loc_host[0] = ip_entry->ip4dst;
		}
		if (ip_mask->ip4src) {
			if (ip_mask->ip4src != IP4_ADDR_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_REM_HOST;
			spec.rem_host[0] = ip_entry->ip4src;
		}
		if (ip_mask->pdst) {
			if (ip_mask->pdst != PORT_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_LOC_PORT;
			spec.loc_port = ip_entry->pdst;
		}
		if (ip_mask->psrc) {
			if (ip_mask->psrc != PORT_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_REM_PORT;
			spec.rem_port = ip_entry->psrc;
		}
		if (ip_mask->tos)
			return -EINVAL;
		break;

	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		spec.match_flags = (EF4_FILTER_MATCH_ETHER_TYPE |
				    EF4_FILTER_MATCH_IP_PROTO);
		spec.ether_type = htons(ETH_P_IPV6);
		spec.ip_proto = ((rule->flow_type & ~FLOW_EXT) == TCP_V6_FLOW ?
				 IPPROTO_TCP : IPPROTO_UDP);
		if (!ip6_mask_is_empty(ip6_mask->ip6dst)) {
			if (!ip6_mask_is_full(ip6_mask->ip6dst))
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_LOC_HOST;
			memcpy(spec.loc_host, ip6_entry->ip6dst, sizeof(spec.loc_host));
		}
		if (!ip6_mask_is_empty(ip6_mask->ip6src)) {
			if (!ip6_mask_is_full(ip6_mask->ip6src))
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_REM_HOST;
			memcpy(spec.rem_host, ip6_entry->ip6src, sizeof(spec.rem_host));
		}
		if (ip6_mask->pdst) {
			if (ip6_mask->pdst != PORT_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_LOC_PORT;
			spec.loc_port = ip6_entry->pdst;
		}
		if (ip6_mask->psrc) {
			if (ip6_mask->psrc != PORT_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_REM_PORT;
			spec.rem_port = ip6_entry->psrc;
		}
		if (ip6_mask->tclass)
			return -EINVAL;
		break;

	case IPV4_USER_FLOW:
		if (uip_mask->l4_4_bytes || uip_mask->tos || uip_mask->ip_ver ||
		    uip_entry->ip_ver != ETH_RX_NFC_IP4)
			return -EINVAL;
		spec.match_flags = EF4_FILTER_MATCH_ETHER_TYPE;
		spec.ether_type = htons(ETH_P_IP);
		if (uip_mask->ip4dst) {
			if (uip_mask->ip4dst != IP4_ADDR_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_LOC_HOST;
			spec.loc_host[0] = uip_entry->ip4dst;
		}
		if (uip_mask->ip4src) {
			if (uip_mask->ip4src != IP4_ADDR_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_REM_HOST;
			spec.rem_host[0] = uip_entry->ip4src;
		}
		if (uip_mask->proto) {
			if (uip_mask->proto != IP_PROTO_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_IP_PROTO;
			spec.ip_proto = uip_entry->proto;
		}
		break;

	case IPV6_USER_FLOW:
		if (uip6_mask->l4_4_bytes || uip6_mask->tclass)
			return -EINVAL;
		spec.match_flags = EF4_FILTER_MATCH_ETHER_TYPE;
		spec.ether_type = htons(ETH_P_IPV6);
		if (!ip6_mask_is_empty(uip6_mask->ip6dst)) {
			if (!ip6_mask_is_full(uip6_mask->ip6dst))
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_LOC_HOST;
			memcpy(spec.loc_host, uip6_entry->ip6dst, sizeof(spec.loc_host));
		}
		if (!ip6_mask_is_empty(uip6_mask->ip6src)) {
			if (!ip6_mask_is_full(uip6_mask->ip6src))
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_REM_HOST;
			memcpy(spec.rem_host, uip6_entry->ip6src, sizeof(spec.rem_host));
		}
		if (uip6_mask->l4_proto) {
			if (uip6_mask->l4_proto != IP_PROTO_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_IP_PROTO;
			spec.ip_proto = uip6_entry->l4_proto;
		}
		break;

	case ETHER_FLOW:
		if (!is_zero_ether_addr(mac_mask->h_dest)) {
			if (ether_addr_equal(mac_mask->h_dest,
					     mac_addr_ig_mask))
				spec.match_flags |= EF4_FILTER_MATCH_LOC_MAC_IG;
			else if (is_broadcast_ether_addr(mac_mask->h_dest))
				spec.match_flags |= EF4_FILTER_MATCH_LOC_MAC;
			else
				return -EINVAL;
			ether_addr_copy(spec.loc_mac, mac_entry->h_dest);
		}
		if (!is_zero_ether_addr(mac_mask->h_source)) {
			if (!is_broadcast_ether_addr(mac_mask->h_source))
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_REM_MAC;
			ether_addr_copy(spec.rem_mac, mac_entry->h_source);
		}
		if (mac_mask->h_proto) {
			if (mac_mask->h_proto != ETHER_TYPE_FULL_MASK)
				return -EINVAL;
			spec.match_flags |= EF4_FILTER_MATCH_ETHER_TYPE;
			spec.ether_type = mac_entry->h_proto;
		}
		break;

	default:
		return -EINVAL;
	}

	if ((rule->flow_type & FLOW_EXT) && rule->m_ext.vlan_tci) {
		if (rule->m_ext.vlan_tci != htons(0xfff))
			return -EINVAL;
		spec.match_flags |= EF4_FILTER_MATCH_OUTER_VID;
		spec.outer_vid = rule->h_ext.vlan_tci;
	}

	rc = ef4_filter_insert_filter(efx, &spec, true);
	if (rc < 0)
		return rc;

	rule->location = rc;
	return 0;
}

static int ef4_ethtool_set_rxnfc(struct net_device *net_dev,
				 struct ethtool_rxnfc *info)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	if (ef4_filter_get_rx_id_limit(efx) == 0)
		return -EOPNOTSUPP;

	switch (info->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		return ef4_ethtool_set_class_rule(efx, &info->fs);

	case ETHTOOL_SRXCLSRLDEL:
		return ef4_filter_remove_id_safe(efx, EF4_FILTER_PRI_MANUAL,
						 info->fs.location);

	default:
		return -EOPNOTSUPP;
	}
}

static u32 ef4_ethtool_get_rxfh_indir_size(struct net_device *net_dev)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	return ((ef4_nic_rev(efx) < EF4_REV_FALCON_B0 ||
		 efx->n_rx_channels == 1) ?
		0 : ARRAY_SIZE(efx->rx_indir_table));
}

static int ef4_ethtool_get_rxfh(struct net_device *net_dev,
				struct ethtool_rxfh_param *rxfh)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	rxfh->hfunc = ETH_RSS_HASH_TOP;
	if (rxfh->indir)
		memcpy(rxfh->indir, efx->rx_indir_table,
		       sizeof(efx->rx_indir_table));
	return 0;
}

static int ef4_ethtool_set_rxfh(struct net_device *net_dev,
				struct ethtool_rxfh_param *rxfh,
				struct netlink_ext_ack *extack)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	/* We do not allow change in unsupported parameters */
	if (rxfh->key ||
	    (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	     rxfh->hfunc != ETH_RSS_HASH_TOP))
		return -EOPNOTSUPP;
	if (!rxfh->indir)
		return 0;

	return efx->type->rx_push_rss_config(efx, true, rxfh->indir);
}

static int ef4_ethtool_get_module_eeprom(struct net_device *net_dev,
					 struct ethtool_eeprom *ee,
					 u8 *data)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	int ret;

	if (!efx->phy_op || !efx->phy_op->get_module_eeprom)
		return -EOPNOTSUPP;

	mutex_lock(&efx->mac_lock);
	ret = efx->phy_op->get_module_eeprom(efx, ee, data);
	mutex_unlock(&efx->mac_lock);

	return ret;
}

static int ef4_ethtool_get_module_info(struct net_device *net_dev,
				       struct ethtool_modinfo *modinfo)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	int ret;

	if (!efx->phy_op || !efx->phy_op->get_module_info)
		return -EOPNOTSUPP;

	mutex_lock(&efx->mac_lock);
	ret = efx->phy_op->get_module_info(efx, modinfo);
	mutex_unlock(&efx->mac_lock);

	return ret;
}

const struct ethtool_ops ef4_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_USECS_IRQ |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX,
	.get_drvinfo		= ef4_ethtool_get_drvinfo,
	.get_regs_len		= ef4_ethtool_get_regs_len,
	.get_regs		= ef4_ethtool_get_regs,
	.get_msglevel		= ef4_ethtool_get_msglevel,
	.set_msglevel		= ef4_ethtool_set_msglevel,
	.nway_reset		= ef4_ethtool_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= ef4_ethtool_get_coalesce,
	.set_coalesce		= ef4_ethtool_set_coalesce,
	.get_ringparam		= ef4_ethtool_get_ringparam,
	.set_ringparam		= ef4_ethtool_set_ringparam,
	.get_pauseparam         = ef4_ethtool_get_pauseparam,
	.set_pauseparam         = ef4_ethtool_set_pauseparam,
	.get_sset_count		= ef4_ethtool_get_sset_count,
	.self_test		= ef4_ethtool_self_test,
	.get_strings		= ef4_ethtool_get_strings,
	.set_phys_id		= ef4_ethtool_phys_id,
	.get_ethtool_stats	= ef4_ethtool_get_stats,
	.get_wol                = ef4_ethtool_get_wol,
	.set_wol                = ef4_ethtool_set_wol,
	.reset			= ef4_ethtool_reset,
	.get_rxnfc		= ef4_ethtool_get_rxnfc,
	.set_rxnfc		= ef4_ethtool_set_rxnfc,
	.get_rxfh_indir_size	= ef4_ethtool_get_rxfh_indir_size,
	.get_rxfh		= ef4_ethtool_get_rxfh,
	.set_rxfh		= ef4_ethtool_set_rxfh,
	.get_rxfh_fields	= ef4_ethtool_get_rxfh_fields,
	.get_module_info	= ef4_ethtool_get_module_info,
	.get_module_eeprom	= ef4_ethtool_get_module_eeprom,
	.get_link_ksettings	= ef4_ethtool_get_link_ksettings,
	.set_link_ksettings	= ef4_ethtool_set_link_ksettings,
};
