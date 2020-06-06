// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include <linux/spi/spi.h>
#include "sja1105.h"

/* The adjfine API clamps ppb between [-32,768,000, 32,768,000], and
 * therefore scaled_ppm between [-2,147,483,648, 2,147,483,647].
 * Set the maximum supported ppb to a round value smaller than the maximum.
 *
 * Percentually speaking, this is a +/- 0.032x adjustment of the
 * free-running counter (0.968x to 1.032x).
 */
#define SJA1105_MAX_ADJ_PPB		32000000
#define SJA1105_SIZE_PTP_CMD		4

/*            This range is actually +/- SJA1105_MAX_ADJ_PPB
 *            divided by 1000 (ppb -> ppm) and with a 16-bit
 *            "fractional" part (actually fixed point).
 *                                    |
 *                                    v
 * Convert scaled_ppm from the +/- ((10^6) << 16) range
 * into the +/- (1 << 31) range.
 *
 * This forgoes a "ppb" numeric representation (up to NSEC_PER_SEC)
 * and defines the scaling factor between scaled_ppm and the actual
 * frequency adjustments of the PHC.
 *
 *   ptpclkrate = scaled_ppm * 2^31 / (10^6 * 2^16)
 *   simplifies to
 *   ptpclkrate = scaled_ppm * 2^9 / 5^6
 */
#define SJA1105_CC_MULT_NUM		(1 << 9)
#define SJA1105_CC_MULT_DEM		15625
#define SJA1105_CC_MULT			0x80000000

enum sja1105_ptp_clk_mode {
	PTP_ADD_MODE = 1,
	PTP_SET_MODE = 0,
};

#define ptp_caps_to_data(d) \
		container_of((d), struct sja1105_ptp_data, caps)
#define ptp_data_to_sja1105(d) \
		container_of((d), struct sja1105_private, ptp_data)

static int sja1105_init_avb_params(struct sja1105_private *priv,
				   bool on)
{
	struct sja1105_avb_params_entry *avb;
	struct sja1105_table *table;

	table = &priv->static_config.tables[BLK_IDX_AVB_PARAMS];

	/* Discard previous AVB Parameters Table */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Configure the reception of meta frames only if requested */
	if (!on)
		return 0;

	table->entries = kcalloc(SJA1105_MAX_AVB_PARAMS_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = SJA1105_MAX_AVB_PARAMS_COUNT;

	avb = table->entries;

	avb->destmeta = SJA1105_META_DMAC;
	avb->srcmeta  = SJA1105_META_SMAC;

	return 0;
}

/* Must be called only with priv->tagger_data.state bit
 * SJA1105_HWTS_RX_EN cleared
 */
static int sja1105_change_rxtstamping(struct sja1105_private *priv,
				      bool on)
{
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;
	struct sja1105_general_params_entry *general_params;
	struct sja1105_table *table;
	int rc;

	table = &priv->static_config.tables[BLK_IDX_GENERAL_PARAMS];
	general_params = table->entries;
	general_params->send_meta1 = on;
	general_params->send_meta0 = on;

	rc = sja1105_init_avb_params(priv, on);
	if (rc < 0)
		return rc;

	/* Initialize the meta state machine to a known state */
	if (priv->tagger_data.stampable_skb) {
		kfree_skb(priv->tagger_data.stampable_skb);
		priv->tagger_data.stampable_skb = NULL;
	}
	ptp_cancel_worker_sync(ptp_data->clock);
	skb_queue_purge(&ptp_data->skb_rxtstamp_queue);

	return sja1105_static_config_reload(priv, SJA1105_RX_HWTSTAMPING);
}

int sja1105_hwtstamp_set(struct dsa_switch *ds, int port, struct ifreq *ifr)
{
	struct sja1105_private *priv = ds->priv;
	struct hwtstamp_config config;
	bool rx_on;
	int rc;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		priv->ports[port].hwts_tx_en = false;
		break;
	case HWTSTAMP_TX_ON:
		priv->ports[port].hwts_tx_en = true;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		rx_on = false;
		break;
	default:
		rx_on = true;
		break;
	}

	if (rx_on != test_bit(SJA1105_HWTS_RX_EN, &priv->tagger_data.state)) {
		clear_bit(SJA1105_HWTS_RX_EN, &priv->tagger_data.state);

		rc = sja1105_change_rxtstamping(priv, rx_on);
		if (rc < 0) {
			dev_err(ds->dev,
				"Failed to change RX timestamping: %d\n", rc);
			return rc;
		}
		if (rx_on)
			set_bit(SJA1105_HWTS_RX_EN, &priv->tagger_data.state);
	}

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;
	return 0;
}

int sja1105_hwtstamp_get(struct dsa_switch *ds, int port, struct ifreq *ifr)
{
	struct sja1105_private *priv = ds->priv;
	struct hwtstamp_config config;

	config.flags = 0;
	if (priv->ports[port].hwts_tx_en)
		config.tx_type = HWTSTAMP_TX_ON;
	else
		config.tx_type = HWTSTAMP_TX_OFF;
	if (test_bit(SJA1105_HWTS_RX_EN, &priv->tagger_data.state))
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
	else
		config.rx_filter = HWTSTAMP_FILTER_NONE;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

int sja1105_get_ts_info(struct dsa_switch *ds, int port,
			struct ethtool_ts_info *info)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;

	/* Called during cleanup */
	if (!ptp_data->clock)
		return -ENODEV;

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = (1 << HWTSTAMP_TX_OFF) |
			 (1 << HWTSTAMP_TX_ON);
	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT);
	info->phc_index = ptp_clock_index(ptp_data->clock);
	return 0;
}

void sja1105et_ptp_cmd_packing(u8 *buf, struct sja1105_ptp_cmd *cmd,
			       enum packing_op op)
{
	const int size = SJA1105_SIZE_PTP_CMD;
	/* No need to keep this as part of the structure */
	u64 valid = 1;

	sja1105_packing(buf, &valid,           31, 31, size, op);
	sja1105_packing(buf, &cmd->ptpstrtsch, 30, 30, size, op);
	sja1105_packing(buf, &cmd->ptpstopsch, 29, 29, size, op);
	sja1105_packing(buf, &cmd->resptp,      2,  2, size, op);
	sja1105_packing(buf, &cmd->corrclk4ts,  1,  1, size, op);
	sja1105_packing(buf, &cmd->ptpclkadd,   0,  0, size, op);
}

void sja1105pqrs_ptp_cmd_packing(u8 *buf, struct sja1105_ptp_cmd *cmd,
				 enum packing_op op)
{
	const int size = SJA1105_SIZE_PTP_CMD;
	/* No need to keep this as part of the structure */
	u64 valid = 1;

	sja1105_packing(buf, &valid,           31, 31, size, op);
	sja1105_packing(buf, &cmd->ptpstrtsch, 30, 30, size, op);
	sja1105_packing(buf, &cmd->ptpstopsch, 29, 29, size, op);
	sja1105_packing(buf, &cmd->resptp,      3,  3, size, op);
	sja1105_packing(buf, &cmd->corrclk4ts,  2,  2, size, op);
	sja1105_packing(buf, &cmd->ptpclkadd,   0,  0, size, op);
}

int sja1105_ptp_commit(struct dsa_switch *ds, struct sja1105_ptp_cmd *cmd,
		       sja1105_spi_rw_mode_t rw)
{
	const struct sja1105_private *priv = ds->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	u8 buf[SJA1105_SIZE_PTP_CMD] = {0};
	int rc;

	if (rw == SPI_WRITE)
		priv->info->ptp_cmd_packing(buf, cmd, PACK);

	rc = sja1105_xfer_buf(priv, rw, regs->ptp_control, buf,
			      SJA1105_SIZE_PTP_CMD);

	if (rw == SPI_READ)
		priv->info->ptp_cmd_packing(buf, cmd, UNPACK);

	return rc;
}

/* The switch returns partial timestamps (24 bits for SJA1105 E/T, which wrap
 * around in 0.135 seconds, and 32 bits for P/Q/R/S, wrapping around in 34.35
 * seconds).
 *
 * This receives the RX or TX MAC timestamps, provided by hardware as
 * the lower bits of the cycle counter, sampled at the time the timestamp was
 * collected.
 *
 * To reconstruct into a full 64-bit-wide timestamp, the cycle counter is
 * read and the high-order bits are filled in.
 *
 * Must be called within one wraparound period of the partial timestamp since
 * it was generated by the MAC.
 */
static u64 sja1105_tstamp_reconstruct(struct dsa_switch *ds, u64 now,
				      u64 ts_partial)
{
	struct sja1105_private *priv = ds->priv;
	u64 partial_tstamp_mask = CYCLECOUNTER_MASK(priv->info->ptp_ts_bits);
	u64 ts_reconstructed;

	ts_reconstructed = (now & ~partial_tstamp_mask) | ts_partial;

	/* Check lower bits of current cycle counter against the timestamp.
	 * If the current cycle counter is lower than the partial timestamp,
	 * then wraparound surely occurred and must be accounted for.
	 */
	if ((now & partial_tstamp_mask) <= ts_partial)
		ts_reconstructed -= (partial_tstamp_mask + 1);

	return ts_reconstructed;
}

/* Reads the SPI interface for an egress timestamp generated by the switch
 * for frames sent using management routes.
 *
 * SJA1105 E/T layout of the 4-byte SPI payload:
 *
 * 31    23    15    7     0
 * |     |     |     |     |
 * +-----+-----+-----+     ^
 *          ^              |
 *          |              |
 *  24-bit timestamp   Update bit
 *
 *
 * SJA1105 P/Q/R/S layout of the 8-byte SPI payload:
 *
 * 31    23    15    7     0     63    55    47    39    32
 * |     |     |     |     |     |     |     |     |     |
 *                         ^     +-----+-----+-----+-----+
 *                         |                 ^
 *                         |                 |
 *                    Update bit    32-bit timestamp
 *
 * Notice that the update bit is in the same place.
 * To have common code for E/T and P/Q/R/S for reading the timestamp,
 * we need to juggle with the offset and the bit indices.
 */
static int sja1105_ptpegr_ts_poll(struct dsa_switch *ds, int port, u64 *ts)
{
	struct sja1105_private *priv = ds->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	int tstamp_bit_start, tstamp_bit_end;
	int timeout = 10;
	u8 packed_buf[8];
	u64 update;
	int rc;

	do {
		rc = sja1105_xfer_buf(priv, SPI_READ, regs->ptpegr_ts[port],
				      packed_buf, priv->info->ptpegr_ts_bytes);
		if (rc < 0)
			return rc;

		sja1105_unpack(packed_buf, &update, 0, 0,
			       priv->info->ptpegr_ts_bytes);
		if (update)
			break;

		usleep_range(10, 50);
	} while (--timeout);

	if (!timeout)
		return -ETIMEDOUT;

	/* Point the end bit to the second 32-bit word on P/Q/R/S,
	 * no-op on E/T.
	 */
	tstamp_bit_end = (priv->info->ptpegr_ts_bytes - 4) * 8;
	/* Shift the 24-bit timestamp on E/T to be collected from 31:8.
	 * No-op on P/Q/R/S.
	 */
	tstamp_bit_end += 32 - priv->info->ptp_ts_bits;
	tstamp_bit_start = tstamp_bit_end + priv->info->ptp_ts_bits - 1;

	*ts = 0;

	sja1105_unpack(packed_buf, ts, tstamp_bit_start, tstamp_bit_end,
		       priv->info->ptpegr_ts_bytes);

	return 0;
}

/* Caller must hold ptp_data->lock */
static int sja1105_ptpclkval_read(struct sja1105_private *priv, u64 *ticks,
				  struct ptp_system_timestamp *ptp_sts)
{
	const struct sja1105_regs *regs = priv->info->regs;

	return sja1105_xfer_u64(priv, SPI_READ, regs->ptpclkval, ticks,
				ptp_sts);
}

/* Caller must hold ptp_data->lock */
static int sja1105_ptpclkval_write(struct sja1105_private *priv, u64 ticks,
				   struct ptp_system_timestamp *ptp_sts)
{
	const struct sja1105_regs *regs = priv->info->regs;

	return sja1105_xfer_u64(priv, SPI_WRITE, regs->ptpclkval, &ticks,
				ptp_sts);
}

static long sja1105_rxtstamp_work(struct ptp_clock_info *ptp)
{
	struct sja1105_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct sja1105_private *priv = ptp_data_to_sja1105(ptp_data);
	struct dsa_switch *ds = priv->ds;
	struct sk_buff *skb;

	mutex_lock(&ptp_data->lock);

	while ((skb = skb_dequeue(&ptp_data->skb_rxtstamp_queue)) != NULL) {
		struct skb_shared_hwtstamps *shwt = skb_hwtstamps(skb);
		u64 ticks, ts;
		int rc;

		rc = sja1105_ptpclkval_read(priv, &ticks, NULL);
		if (rc < 0) {
			dev_err(ds->dev, "Failed to read PTP clock: %d\n", rc);
			kfree_skb(skb);
			continue;
		}

		*shwt = (struct skb_shared_hwtstamps) {0};

		ts = SJA1105_SKB_CB(skb)->meta_tstamp;
		ts = sja1105_tstamp_reconstruct(ds, ticks, ts);

		shwt->hwtstamp = ns_to_ktime(sja1105_ticks_to_ns(ts));
		netif_rx_ni(skb);
	}

	mutex_unlock(&ptp_data->lock);

	/* Don't restart */
	return -1;
}

/* Called from dsa_skb_defer_rx_timestamp */
bool sja1105_port_rxtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;

	if (!test_bit(SJA1105_HWTS_RX_EN, &priv->tagger_data.state))
		return false;

	/* We need to read the full PTP clock to reconstruct the Rx
	 * timestamp. For that we need a sleepable context.
	 */
	skb_queue_tail(&ptp_data->skb_rxtstamp_queue, skb);
	ptp_schedule_worker(ptp_data->clock, 0);
	return true;
}

/* Called from dsa_skb_tx_timestamp. This callback is just to make DSA clone
 * the skb and have it available in DSA_SKB_CB in the .port_deferred_xmit
 * callback, where we will timestamp it synchronously.
 */
bool sja1105_port_txtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_port *sp = &priv->ports[port];

	if (!sp->hwts_tx_en)
		return false;

	return true;
}

static int sja1105_ptp_reset(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;
	struct sja1105_ptp_cmd cmd = ptp_data->cmd;
	int rc;

	mutex_lock(&ptp_data->lock);

	cmd.resptp = 1;

	dev_dbg(ds->dev, "Resetting PTP clock\n");
	rc = sja1105_ptp_commit(ds, &cmd, SPI_WRITE);

	sja1105_tas_clockstep(priv->ds);

	mutex_unlock(&ptp_data->lock);

	return rc;
}

/* Caller must hold ptp_data->lock */
int __sja1105_ptp_gettimex(struct dsa_switch *ds, u64 *ns,
			   struct ptp_system_timestamp *ptp_sts)
{
	struct sja1105_private *priv = ds->priv;
	u64 ticks;
	int rc;

	rc = sja1105_ptpclkval_read(priv, &ticks, ptp_sts);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to read PTP clock: %d\n", rc);
		return rc;
	}

	*ns = sja1105_ticks_to_ns(ticks);

	return 0;
}

static int sja1105_ptp_gettimex(struct ptp_clock_info *ptp,
				struct timespec64 *ts,
				struct ptp_system_timestamp *ptp_sts)
{
	struct sja1105_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct sja1105_private *priv = ptp_data_to_sja1105(ptp_data);
	u64 now = 0;
	int rc;

	mutex_lock(&ptp_data->lock);

	rc = __sja1105_ptp_gettimex(priv->ds, &now, ptp_sts);
	*ts = ns_to_timespec64(now);

	mutex_unlock(&ptp_data->lock);

	return rc;
}

/* Caller must hold ptp_data->lock */
static int sja1105_ptp_mode_set(struct sja1105_private *priv,
				enum sja1105_ptp_clk_mode mode)
{
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;

	if (ptp_data->cmd.ptpclkadd == mode)
		return 0;

	ptp_data->cmd.ptpclkadd = mode;

	return sja1105_ptp_commit(priv->ds, &ptp_data->cmd, SPI_WRITE);
}

/* Write to PTPCLKVAL while PTPCLKADD is 0 */
int __sja1105_ptp_settime(struct dsa_switch *ds, u64 ns,
			  struct ptp_system_timestamp *ptp_sts)
{
	struct sja1105_private *priv = ds->priv;
	u64 ticks = ns_to_sja1105_ticks(ns);
	int rc;

	rc = sja1105_ptp_mode_set(priv, PTP_SET_MODE);
	if (rc < 0) {
		dev_err(priv->ds->dev, "Failed to put PTPCLK in set mode\n");
		return rc;
	}

	rc = sja1105_ptpclkval_write(priv, ticks, ptp_sts);

	sja1105_tas_clockstep(priv->ds);

	return rc;
}

static int sja1105_ptp_settime(struct ptp_clock_info *ptp,
			       const struct timespec64 *ts)
{
	struct sja1105_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct sja1105_private *priv = ptp_data_to_sja1105(ptp_data);
	u64 ns = timespec64_to_ns(ts);
	int rc;

	mutex_lock(&ptp_data->lock);

	rc = __sja1105_ptp_settime(priv->ds, ns, NULL);

	mutex_unlock(&ptp_data->lock);

	return rc;
}

static int sja1105_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct sja1105_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct sja1105_private *priv = ptp_data_to_sja1105(ptp_data);
	const struct sja1105_regs *regs = priv->info->regs;
	u32 clkrate32;
	s64 clkrate;
	int rc;

	clkrate = (s64)scaled_ppm * SJA1105_CC_MULT_NUM;
	clkrate = div_s64(clkrate, SJA1105_CC_MULT_DEM);

	/* Take a +/- value and re-center it around 2^31. */
	clkrate = SJA1105_CC_MULT + clkrate;
	WARN_ON(abs(clkrate) >= GENMASK_ULL(31, 0));
	clkrate32 = clkrate;

	mutex_lock(&ptp_data->lock);

	rc = sja1105_xfer_u32(priv, SPI_WRITE, regs->ptpclkrate, &clkrate32,
			      NULL);

	sja1105_tas_adjfreq(priv->ds);

	mutex_unlock(&ptp_data->lock);

	return rc;
}

/* Write to PTPCLKVAL while PTPCLKADD is 1 */
int __sja1105_ptp_adjtime(struct dsa_switch *ds, s64 delta)
{
	struct sja1105_private *priv = ds->priv;
	s64 ticks = ns_to_sja1105_ticks(delta);
	int rc;

	rc = sja1105_ptp_mode_set(priv, PTP_ADD_MODE);
	if (rc < 0) {
		dev_err(priv->ds->dev, "Failed to put PTPCLK in add mode\n");
		return rc;
	}

	rc = sja1105_ptpclkval_write(priv, ticks, NULL);

	sja1105_tas_clockstep(priv->ds);

	return rc;
}

static int sja1105_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct sja1105_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct sja1105_private *priv = ptp_data_to_sja1105(ptp_data);
	int rc;

	mutex_lock(&ptp_data->lock);

	rc = __sja1105_ptp_adjtime(priv->ds, delta);

	mutex_unlock(&ptp_data->lock);

	return rc;
}

int sja1105_ptp_clock_register(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_tagger_data *tagger_data = &priv->tagger_data;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;

	ptp_data->caps = (struct ptp_clock_info) {
		.owner		= THIS_MODULE,
		.name		= "SJA1105 PHC",
		.adjfine	= sja1105_ptp_adjfine,
		.adjtime	= sja1105_ptp_adjtime,
		.gettimex64	= sja1105_ptp_gettimex,
		.settime64	= sja1105_ptp_settime,
		.do_aux_work	= sja1105_rxtstamp_work,
		.max_adj	= SJA1105_MAX_ADJ_PPB,
	};

	skb_queue_head_init(&ptp_data->skb_rxtstamp_queue);
	spin_lock_init(&tagger_data->meta_lock);

	ptp_data->clock = ptp_clock_register(&ptp_data->caps, ds->dev);
	if (IS_ERR_OR_NULL(ptp_data->clock))
		return PTR_ERR(ptp_data->clock);

	ptp_data->cmd.corrclk4ts = true;
	ptp_data->cmd.ptpclkadd = PTP_SET_MODE;

	return sja1105_ptp_reset(ds);
}

void sja1105_ptp_clock_unregister(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;

	if (IS_ERR_OR_NULL(ptp_data->clock))
		return;

	ptp_cancel_worker_sync(ptp_data->clock);
	skb_queue_purge(&ptp_data->skb_rxtstamp_queue);
	ptp_clock_unregister(ptp_data->clock);
	ptp_data->clock = NULL;
}

void sja1105_ptp_txtstamp_skb(struct dsa_switch *ds, int port,
			      struct sk_buff *skb)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;
	struct skb_shared_hwtstamps shwt = {0};
	u64 ticks, ts;
	int rc;

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	mutex_lock(&ptp_data->lock);

	rc = sja1105_ptpclkval_read(priv, &ticks, NULL);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to read PTP clock: %d\n", rc);
		kfree_skb(skb);
		goto out;
	}

	rc = sja1105_ptpegr_ts_poll(ds, port, &ts);
	if (rc < 0) {
		dev_err(ds->dev, "timed out polling for tstamp\n");
		kfree_skb(skb);
		goto out;
	}

	ts = sja1105_tstamp_reconstruct(ds, ticks, ts);

	shwt.hwtstamp = ns_to_ktime(sja1105_ticks_to_ns(ts));
	skb_complete_tx_timestamp(skb, &shwt);

out:
	mutex_unlock(&ptp_data->lock);
}
