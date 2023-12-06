/*
 *   Copyright (c) 2011, 2012, Qualcomm Atheros Communications Inc.
 *   Copyright (c) 2014, I2SE GmbH
 *
 *   Permission to use, copy, modify, and/or distribute this software
 *   for any purpose with or without fee is hereby granted, provided
 *   that the above copyright notice and this permission notice appear
 *   in all copies.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 *   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 *   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 *   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *   NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*   This file contains debugging routines for use in the QCA7K driver.
 */

#include <linux/debugfs.h>
#include <linux/ethtool.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include "qca_7k.h"
#include "qca_debug.h"

#define QCASPI_MAX_REGS 0x20

#define QCASPI_RX_MAX_FRAMES 4

static const u16 qcaspi_spi_regs[] = {
	SPI_REG_BFR_SIZE,
	SPI_REG_WRBUF_SPC_AVA,
	SPI_REG_RDBUF_BYTE_AVA,
	SPI_REG_SPI_CONFIG,
	SPI_REG_SPI_STATUS,
	SPI_REG_INTR_CAUSE,
	SPI_REG_INTR_ENABLE,
	SPI_REG_RDBUF_WATERMARK,
	SPI_REG_WRBUF_WATERMARK,
	SPI_REG_SIGNATURE,
	SPI_REG_ACTION_CTRL
};

/* The order of these strings must match the order of the fields in
 * struct qcaspi_stats
 * See qca_spi.h
 */
static const char qcaspi_gstrings_stats[][ETH_GSTRING_LEN] = {
	"Triggered resets",
	"Device resets",
	"Reset timeouts",
	"Read errors",
	"Write errors",
	"Read buffer errors",
	"Write buffer errors",
	"Out of memory",
	"Write buffer misses",
	"Transmit ring full",
	"SPI errors",
	"Write verify errors",
	"Buffer available errors",
	"Bad signature",
};

#ifdef CONFIG_DEBUG_FS

static int
qcaspi_info_show(struct seq_file *s, void *what)
{
	struct qcaspi *qca = s->private;

	seq_printf(s, "RX buffer size   : %lu\n",
		   (unsigned long)qca->buffer_size);

	seq_puts(s, "TX ring state    : ");

	if (qca->txr.skb[qca->txr.head] == NULL)
		seq_puts(s, "empty");
	else if (qca->txr.skb[qca->txr.tail])
		seq_puts(s, "full");
	else
		seq_puts(s, "in use");

	seq_puts(s, "\n");

	seq_printf(s, "TX ring size     : %u\n",
		   qca->txr.size);

	seq_printf(s, "Sync state       : %u (",
		   (unsigned int)qca->sync);
	switch (qca->sync) {
	case QCASPI_SYNC_UNKNOWN:
		seq_puts(s, "QCASPI_SYNC_UNKNOWN");
		break;
	case QCASPI_SYNC_RESET:
		seq_puts(s, "QCASPI_SYNC_RESET");
		break;
	case QCASPI_SYNC_READY:
		seq_puts(s, "QCASPI_SYNC_READY");
		break;
	default:
		seq_puts(s, "INVALID");
		break;
	}
	seq_puts(s, ")\n");

	seq_printf(s, "IRQ              : %d\n",
		   qca->spi_dev->irq);
	seq_printf(s, "INTR REQ         : %u\n",
		   qca->intr_req);
	seq_printf(s, "INTR SVC         : %u\n",
		   qca->intr_svc);

	seq_printf(s, "SPI max speed    : %lu\n",
		   (unsigned long)qca->spi_dev->max_speed_hz);
	seq_printf(s, "SPI mode         : %x\n",
		   qca->spi_dev->mode);
	seq_printf(s, "SPI chip select  : %u\n",
		   (unsigned int)spi_get_chipselect(qca->spi_dev, 0));
	seq_printf(s, "SPI legacy mode  : %u\n",
		   (unsigned int)qca->legacy_mode);
	seq_printf(s, "SPI burst length : %u\n",
		   (unsigned int)qca->burst_len);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(qcaspi_info);

void
qcaspi_init_device_debugfs(struct qcaspi *qca)
{
	qca->device_root = debugfs_create_dir(dev_name(&qca->net_dev->dev),
					      NULL);

	debugfs_create_file("info", S_IFREG | 0444, qca->device_root, qca,
			    &qcaspi_info_fops);
}

void
qcaspi_remove_device_debugfs(struct qcaspi *qca)
{
	debugfs_remove_recursive(qca->device_root);
}

#else /* CONFIG_DEBUG_FS */

void
qcaspi_init_device_debugfs(struct qcaspi *qca)
{
}

void
qcaspi_remove_device_debugfs(struct qcaspi *qca)
{
}

#endif

static void
qcaspi_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *p)
{
	struct qcaspi *qca = netdev_priv(dev);

	strscpy(p->driver, QCASPI_DRV_NAME, sizeof(p->driver));
	strscpy(p->version, QCASPI_DRV_VERSION, sizeof(p->version));
	strscpy(p->fw_version, "QCA7000", sizeof(p->fw_version));
	strscpy(p->bus_info, dev_name(&qca->spi_dev->dev),
		sizeof(p->bus_info));
}

static int
qcaspi_get_link_ksettings(struct net_device *dev,
			  struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT_Half);

	cmd->base.speed = SPEED_10;
	cmd->base.duplex = DUPLEX_HALF;
	cmd->base.port = PORT_OTHER;
	cmd->base.autoneg = AUTONEG_DISABLE;

	return 0;
}

static void
qcaspi_get_ethtool_stats(struct net_device *dev, struct ethtool_stats *estats, u64 *data)
{
	struct qcaspi *qca = netdev_priv(dev);
	struct qcaspi_stats *st = &qca->stats;

	memcpy(data, st, ARRAY_SIZE(qcaspi_gstrings_stats) * sizeof(u64));
}

static void
qcaspi_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &qcaspi_gstrings_stats,
		       sizeof(qcaspi_gstrings_stats));
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int
qcaspi_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(qcaspi_gstrings_stats);
	default:
		return -EINVAL;
	}
}

static int
qcaspi_get_regs_len(struct net_device *dev)
{
	return sizeof(u32) * QCASPI_MAX_REGS;
}

static void
qcaspi_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *p)
{
	struct qcaspi *qca = netdev_priv(dev);
	u32 *regs_buff = p;
	unsigned int i;

	regs->version = 1;
	memset(regs_buff, 0, sizeof(u32) * QCASPI_MAX_REGS);

	for (i = 0; i < ARRAY_SIZE(qcaspi_spi_regs); i++) {
		u16 offset, value;

		qcaspi_read_register(qca, qcaspi_spi_regs[i], &value);
		offset = qcaspi_spi_regs[i] >> 8;
		regs_buff[offset] = value;
	}
}

static void
qcaspi_get_ringparam(struct net_device *dev, struct ethtool_ringparam *ring,
		     struct kernel_ethtool_ringparam *kernel_ring,
		     struct netlink_ext_ack *extack)
{
	struct qcaspi *qca = netdev_priv(dev);

	ring->rx_max_pending = QCASPI_RX_MAX_FRAMES;
	ring->tx_max_pending = TX_RING_MAX_LEN;
	ring->rx_pending = QCASPI_RX_MAX_FRAMES;
	ring->tx_pending = qca->txr.count;
}

static int
qcaspi_set_ringparam(struct net_device *dev, struct ethtool_ringparam *ring,
		     struct kernel_ethtool_ringparam *kernel_ring,
		     struct netlink_ext_ack *extack)
{
	struct qcaspi *qca = netdev_priv(dev);

	if (ring->rx_pending != QCASPI_RX_MAX_FRAMES ||
	    (ring->rx_mini_pending) ||
	    (ring->rx_jumbo_pending))
		return -EINVAL;

	if (qca->spi_thread)
		kthread_park(qca->spi_thread);

	qca->txr.count = max_t(u32, ring->tx_pending, TX_RING_MIN_LEN);
	qca->txr.count = min_t(u16, qca->txr.count, TX_RING_MAX_LEN);

	if (qca->spi_thread)
		kthread_unpark(qca->spi_thread);

	return 0;
}

static const struct ethtool_ops qcaspi_ethtool_ops = {
	.get_drvinfo = qcaspi_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ethtool_stats = qcaspi_get_ethtool_stats,
	.get_strings = qcaspi_get_strings,
	.get_sset_count = qcaspi_get_sset_count,
	.get_regs_len = qcaspi_get_regs_len,
	.get_regs = qcaspi_get_regs,
	.get_ringparam = qcaspi_get_ringparam,
	.set_ringparam = qcaspi_set_ringparam,
	.get_link_ksettings = qcaspi_get_link_ksettings,
};

void qcaspi_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &qcaspi_ethtool_ops;
}
