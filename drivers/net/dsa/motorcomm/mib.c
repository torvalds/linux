// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 David Yang
 */

#include "chip.h"
#include "mib.h"
#include "smi.h"

struct yt921x_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

#define MIB_DESC(_size, _offset, _name) \
	{_size, _offset, _name}

/* Must agree with yt921x_mib_stats
 *
 * Unstructured fields (name != NULL) will appear in get_ethtool_stats(),
 * structured go to their *_stats() methods, but we need their sizes and offsets
 * to perform 32bit MIB overflow wraparound.
 */
static const struct yt921x_mib_desc yt921x_mib_descs[] = {
	MIB_DESC(1, YT921X_MIB_DATA_RX_BROADCAST, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PAUSE, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_MULTICAST, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_CRC_ERR, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_RX_ALIGN_ERR, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_UNDERSIZE_ERR, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_FRAG_ERR, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_64, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_65_TO_127, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_128_TO_255, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_256_TO_511, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_512_TO_1023, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_1024_TO_1518, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_1519_TO_MAX, NULL),
	MIB_DESC(2, YT921X_MIB_DATA_RX_GOOD_BYTES, NULL),

	MIB_DESC(2, YT921X_MIB_DATA_RX_BAD_BYTES, "RxBadBytes"),
	MIB_DESC(1, YT921X_MIB_DATA_RX_OVERSIZE_ERR, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_RX_DROPPED, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_BROADCAST, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PAUSE, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_MULTICAST, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_TX_UNDERSIZE_ERR, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_64, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_65_TO_127, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_128_TO_255, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_256_TO_511, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_512_TO_1023, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_1024_TO_1518, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_1519_TO_MAX, NULL),

	MIB_DESC(2, YT921X_MIB_DATA_TX_GOOD_BYTES, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_COLLISION, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_TX_EXCESSIVE_COLLISION, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_MULTIPLE_COLLISION, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_SINGLE_COLLISION, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_TX_DEFERRED, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_LATE_COLLISION, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_OAM, "RxOAM"),
	MIB_DESC(1, YT921X_MIB_DATA_TX_OAM, "TxOAM"),
};

static_assert(ARRAY_SIZE(yt921x_mib_descs) == YT921X_MIB_STAT_NUM);

/* The interval should be small enough to avoid overflow of 32bit MIBs.
 *
 * Until we can read MIBs from stats64 call directly (i.e. sleep
 * there), we have to poll stats more frequently then it is actually needed.
 * For overflow protection, normally, 100 sec interval should have been OK.
 */
#define YT921X_STATS_INTERVAL_JIFFIES	(3 * HZ)

/* Read and handle overflow of 32bit MIBs. MIB buffer must be zeroed before. */
static int yt921x_mib_read(struct yt921x_priv *priv, int port)
{
	struct device *dev = yt921x_priv_to_device(priv);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *pm = pp->mib;
	struct yt921x_mib_stats *mib;
	u64 *buf = pm->data;
	unsigned long flags;
	u64 rx_frames;
	u64 tx_frames;
	int res = 0;

	mib = &pm->stats;

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];
		u32 reg = YT921X_MIBn_DATA0(port) + desc->offset;
		u32 val0;
		u64 val;

		res = yt921x_reg_read(priv, reg, &val0);
		if (res)
			break;

		if (desc->size <= 1) {
			u64 old_val = buf[i];

			val = (old_val & ~(u64)U32_MAX) | val0;
			if (val < old_val)
				val += 1ull << 32;
		} else {
			u32 val1;

			res = yt921x_reg_read(priv, reg + 4, &val1);
			if (res)
				break;
			val = ((u64)val1 << 32) | val0;
		}

		buf[i] = val;
	}

	flags = u64_stats_update_begin_irqsave(&pm->syncp);
	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++)
		u64_stats_set(&((u64_stats_t *)mib)[i], buf[i]);
	rx_frames = u64_stats_read(&mib->rx_64byte) +
		    u64_stats_read(&mib->rx_65_127byte) +
		    u64_stats_read(&mib->rx_128_255byte) +
		    u64_stats_read(&mib->rx_256_511byte) +
		    u64_stats_read(&mib->rx_512_1023byte) +
		    u64_stats_read(&mib->rx_1024_1518byte) +
		    u64_stats_read(&mib->rx_jumbo);
	tx_frames = u64_stats_read(&mib->tx_64byte) +
		    u64_stats_read(&mib->tx_65_127byte) +
		    u64_stats_read(&mib->tx_128_255byte) +
		    u64_stats_read(&mib->tx_256_511byte) +
		    u64_stats_read(&mib->tx_512_1023byte) +
		    u64_stats_read(&mib->tx_1024_1518byte) +
		    u64_stats_read(&mib->tx_jumbo);
	u64_stats_set(&pm->rx_frames, rx_frames);
	u64_stats_set(&pm->tx_frames, tx_frames);
	u64_stats_update_end_irqrestore(&pm->syncp, flags);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "read stats for",
			port, res);
	return res;
}

void yt921x_mib_poll(struct work_struct *work)
{
	struct yt921x_mib *pm = container_of_const(work, struct yt921x_mib,
						   work.work);
	struct yt921x_port *pp = pm->port;
	unsigned long delay = YT921X_STATS_INTERVAL_JIFFIES;
	struct yt921x_priv *priv = yt921x_port_to_priv(pp);
	int port = pp->index;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_mib_read(priv, port);
	mutex_unlock(&priv->reg_lock);
	if (res)
		delay *= 4;

	schedule_delayed_work(&pm->work, delay);
}

void
yt921x_dsa_get_strings(struct dsa_switch *ds, int port, u32 stringset,
		       uint8_t *data)
{
	if (stringset != ETH_SS_STATS)
		return;

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (desc->name)
			ethtool_puts(&data, desc->name);
	}
}

void
yt921x_dsa_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *data)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *pm = pp->mib;
	u64 *buf;
	size_t j;

	if (!pm)
		return;
	buf = pm->data;

	mutex_lock(&priv->reg_lock);

	yt921x_mib_read(priv, port);

	j = 0;
	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (!desc->name)
			continue;

		data[j] = buf[i];
		j++;
	}

	mutex_unlock(&priv->reg_lock);
}

int yt921x_dsa_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	int cnt = 0;

	if (sset != ETH_SS_STATS)
		return 0;

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (desc->name)
			cnt++;
	}

	return cnt;
}

void
yt921x_dsa_get_eth_mac_stats(struct dsa_switch *ds, int port,
			     struct ethtool_eth_mac_stats *mac_stats)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *pm = pp->mib;
	struct yt921x_mib_stats *mib;

	if (!pm)
		return;
	mib = &pm->stats;

	mutex_lock(&priv->reg_lock);

	yt921x_mib_read(priv, port);

	mac_stats->FramesTransmittedOK = u64_stats_read(&pm->tx_frames);
	mac_stats->SingleCollisionFrames = u64_stats_read(&mib->tx_single_collisions);
	mac_stats->MultipleCollisionFrames = u64_stats_read(&mib->tx_multiple_collisions);
	mac_stats->FramesReceivedOK = u64_stats_read(&pm->rx_frames);
	mac_stats->FrameCheckSequenceErrors = u64_stats_read(&mib->rx_crc_errors);
	mac_stats->AlignmentErrors = u64_stats_read(&mib->rx_alignment_errors);
	mac_stats->OctetsTransmittedOK = u64_stats_read(&mib->tx_good_bytes);
	mac_stats->FramesWithDeferredXmissions = u64_stats_read(&mib->tx_deferred);
	mac_stats->LateCollisions = u64_stats_read(&mib->tx_late_collisions);
	mac_stats->FramesAbortedDueToXSColls = u64_stats_read(&mib->tx_aborted_errors);
	/* mac_stats->FramesLostDueToIntMACXmitError */
	/* mac_stats->CarrierSenseErrors */
	mac_stats->OctetsReceivedOK = u64_stats_read(&mib->rx_good_bytes);
	/* mac_stats->FramesLostDueToIntMACRcvError */
	mac_stats->MulticastFramesXmittedOK = u64_stats_read(&mib->tx_multicast);
	mac_stats->BroadcastFramesXmittedOK = u64_stats_read(&mib->tx_broadcast);
	/* mac_stats->FramesWithExcessiveDeferral */
	mac_stats->MulticastFramesReceivedOK = u64_stats_read(&mib->rx_multicast);
	mac_stats->BroadcastFramesReceivedOK = u64_stats_read(&mib->rx_broadcast);
	/* mac_stats->InRangeLengthErrors */
	/* mac_stats->OutOfRangeLengthField */
	mac_stats->FrameTooLongErrors = u64_stats_read(&mib->rx_oversize_errors);

	mutex_unlock(&priv->reg_lock);
}

void
yt921x_dsa_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
			      struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *pm = pp->mib;
	struct yt921x_mib_stats *mib;

	if (!pm)
		return;
	mib = &pm->stats;

	mutex_lock(&priv->reg_lock);

	yt921x_mib_read(priv, port);

	ctrl_stats->MACControlFramesTransmitted = u64_stats_read(&mib->tx_pause);
	ctrl_stats->MACControlFramesReceived = u64_stats_read(&mib->rx_pause);
	/* ctrl_stats->UnsupportedOpcodesReceived */

	mutex_unlock(&priv->reg_lock);
}

static const struct ethtool_rmon_hist_range yt921x_rmon_ranges[] = {
	{ 0, 64 },
	{ 65, 127 },
	{ 128, 255 },
	{ 256, 511 },
	{ 512, 1023 },
	{ 1024, 1518 },
	{ 1519, YT921X_FRAME_SIZE_MAX },
	{}
};

void
yt921x_dsa_get_rmon_stats(struct dsa_switch *ds, int port,
			  struct ethtool_rmon_stats *rmon_stats,
			  const struct ethtool_rmon_hist_range **ranges)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *pm = pp->mib;
	struct yt921x_mib_stats *mib;

	if (!pm)
		return;
	mib = &pm->stats;

	mutex_lock(&priv->reg_lock);

	yt921x_mib_read(priv, port);

	*ranges = yt921x_rmon_ranges;

	rmon_stats->undersize_pkts = u64_stats_read(&mib->rx_undersize_errors);
	rmon_stats->oversize_pkts = u64_stats_read(&mib->rx_oversize_errors);
	rmon_stats->fragments = u64_stats_read(&mib->rx_alignment_errors);
	/* rmon_stats->jabbers */

	rmon_stats->hist[0] = u64_stats_read(&mib->rx_64byte);
	rmon_stats->hist[1] = u64_stats_read(&mib->rx_65_127byte);
	rmon_stats->hist[2] = u64_stats_read(&mib->rx_128_255byte);
	rmon_stats->hist[3] = u64_stats_read(&mib->rx_256_511byte);
	rmon_stats->hist[4] = u64_stats_read(&mib->rx_512_1023byte);
	rmon_stats->hist[5] = u64_stats_read(&mib->rx_1024_1518byte);
	rmon_stats->hist[6] = u64_stats_read(&mib->rx_jumbo);

	rmon_stats->hist_tx[0] = u64_stats_read(&mib->tx_64byte);
	rmon_stats->hist_tx[1] = u64_stats_read(&mib->tx_65_127byte);
	rmon_stats->hist_tx[2] = u64_stats_read(&mib->tx_128_255byte);
	rmon_stats->hist_tx[3] = u64_stats_read(&mib->tx_256_511byte);
	rmon_stats->hist_tx[4] = u64_stats_read(&mib->tx_512_1023byte);
	rmon_stats->hist_tx[5] = u64_stats_read(&mib->tx_1024_1518byte);
	rmon_stats->hist_tx[6] = u64_stats_read(&mib->tx_jumbo);

	mutex_unlock(&priv->reg_lock);
}

void
yt921x_dsa_get_stats64(struct dsa_switch *ds, int port,
		       struct rtnl_link_stats64 *stats)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *pm = pp->mib;
	struct yt921x_mib_stats *mib;
	unsigned int start;

	if (!pm)
		return;
	mib = &pm->stats;

	do {
		start = u64_stats_fetch_begin(&pm->syncp);

		stats->rx_length_errors = u64_stats_read(&mib->rx_undersize_errors) +
					  u64_stats_read(&mib->rx_fragment_errors);
		stats->rx_over_errors = u64_stats_read(&mib->rx_oversize_errors);
		stats->rx_crc_errors = u64_stats_read(&mib->rx_crc_errors);
		stats->rx_frame_errors = u64_stats_read(&mib->rx_alignment_errors);
		/* stats->rx_fifo_errors */
		/* stats->rx_missed_errors */

		stats->tx_aborted_errors = u64_stats_read(&mib->tx_aborted_errors);
		/* stats->tx_carrier_errors */
		stats->tx_fifo_errors = u64_stats_read(&mib->tx_undersize_errors);
		/* stats->tx_heartbeat_errors */
		stats->tx_window_errors = u64_stats_read(&mib->tx_late_collisions);

		stats->rx_packets = u64_stats_read(&pm->rx_frames);
		stats->tx_packets = u64_stats_read(&pm->tx_frames);
		stats->rx_bytes = u64_stats_read(&mib->rx_good_bytes) -
				  ETH_FCS_LEN * stats->rx_packets;
		stats->tx_bytes = u64_stats_read(&mib->tx_good_bytes) -
				  ETH_FCS_LEN * stats->tx_packets;
		stats->rx_dropped = u64_stats_read(&mib->rx_dropped);
		/* stats->tx_dropped */
		stats->multicast = u64_stats_read(&mib->rx_multicast);
		stats->collisions = u64_stats_read(&mib->tx_collisions);
	} while (u64_stats_fetch_retry(&pm->syncp, start));

	stats->rx_errors = stats->rx_length_errors + stats->rx_over_errors +
			   stats->rx_crc_errors + stats->rx_frame_errors;
	stats->tx_errors = stats->tx_aborted_errors + stats->tx_fifo_errors +
			   stats->tx_window_errors;
}

void
yt921x_dsa_get_pause_stats(struct dsa_switch *ds, int port,
			   struct ethtool_pause_stats *pause_stats)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *pm = pp->mib;
	struct yt921x_mib_stats *mib;

	if (!pm)
		return;
	mib = &pm->stats;

	mutex_lock(&priv->reg_lock);

	yt921x_mib_read(priv, port);

	pause_stats->tx_pause_frames = u64_stats_read(&mib->tx_pause);
	pause_stats->rx_pause_frames = u64_stats_read(&mib->rx_pause);

	mutex_unlock(&priv->reg_lock);
}
