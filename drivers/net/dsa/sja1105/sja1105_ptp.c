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

/* PTPSYNCTS has no interrupt or update mechanism, because the intended
 * hardware use case is for the timestamp to be collected synchronously,
 * immediately after the CAS_MASTER SJA1105 switch has performed a CASSYNC
 * one-shot toggle (no return to level) on the PTP_CLK pin. When used as a
 * generic extts source, the PTPSYNCTS register needs polling and a comparison
 * with the old value. The polling interval is configured as the Nyquist rate
 * of a signal with 50% duty cycle and 1Hz frequency, which is sadly all that
 * this hardware can do (but may be enough for some setups). Anything of higher
 * frequency than 1 Hz will be lost, since there is no timestamp FIFO.
 */
#define SJA1105_EXTTS_INTERVAL		(HZ / 6)

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

#define extts_to_data(t) \
		container_of((t), struct sja1105_ptp_data, extts_timer)
#define ptp_caps_to_data(d) \
		container_of((d), struct sja1105_ptp_data, caps)
#define ptp_data_to_sja1105(d) \
		container_of((d), struct sja1105_private, ptp_data)

int sja1105_hwtstamp_set(struct dsa_switch *ds, int port, struct ifreq *ifr)
{
	struct sja1105_private *priv = ds->priv;
	unsigned long hwts_tx_en, hwts_rx_en;
	struct hwtstamp_config config;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	hwts_tx_en = priv->hwts_tx_en;
	hwts_rx_en = priv->hwts_rx_en;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		hwts_tx_en &= ~BIT(port);
		break;
	case HWTSTAMP_TX_ON:
		hwts_tx_en |= BIT(port);
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		hwts_rx_en &= ~BIT(port);
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		hwts_rx_en |= BIT(port);
		break;
	default:
		return -ERANGE;
	}

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	priv->hwts_tx_en = hwts_tx_en;
	priv->hwts_rx_en = hwts_rx_en;

	return 0;
}

int sja1105_hwtstamp_get(struct dsa_switch *ds, int port, struct ifreq *ifr)
{
	struct sja1105_private *priv = ds->priv;
	struct hwtstamp_config config;

	config.flags = 0;
	if (priv->hwts_tx_en & BIT(port))
		config.tx_type = HWTSTAMP_TX_ON;
	else
		config.tx_type = HWTSTAMP_TX_OFF;
	if (priv->hwts_rx_en & BIT(port))
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
	else
		config.rx_filter = HWTSTAMP_FILTER_NONE;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

int sja1105_get_ts_info(struct dsa_switch *ds, int port,
			struct kernel_ethtool_ts_info *info)
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
	sja1105_packing(buf, &cmd->startptpcp, 28, 28, size, op);
	sja1105_packing(buf, &cmd->stopptpcp,  27, 27, size, op);
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
	sja1105_packing(buf, &cmd->startptpcp, 28, 28, size, op);
	sja1105_packing(buf, &cmd->stopptpcp,  27, 27, size, op);
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

static void sja1105_extts_poll(struct sja1105_private *priv)
{
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;
	const struct sja1105_regs *regs = priv->info->regs;
	struct ptp_clock_event event;
	u64 ptpsyncts = 0;
	int rc;

	rc = sja1105_xfer_u64(priv, SPI_READ, regs->ptpsyncts, &ptpsyncts,
			      NULL);
	if (rc < 0)
		dev_err_ratelimited(priv->ds->dev,
				    "Failed to read PTPSYNCTS: %d\n", rc);

	if (ptpsyncts && ptp_data->ptpsyncts != ptpsyncts) {
		event.index = 0;
		event.type = PTP_CLOCK_EXTTS;
		event.timestamp = ns_to_ktime(sja1105_ticks_to_ns(ptpsyncts));
		ptp_clock_event(ptp_data->clock, &event);

		ptp_data->ptpsyncts = ptpsyncts;
	}
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

		ts = SJA1105_SKB_CB(skb)->tstamp;
		ts = sja1105_tstamp_reconstruct(ds, ticks, ts);

		shwt->hwtstamp = ns_to_ktime(sja1105_ticks_to_ns(ts));
		netif_rx(skb);
	}

	if (ptp_data->extts_enabled)
		sja1105_extts_poll(priv);

	mutex_unlock(&ptp_data->lock);

	/* Don't restart */
	return -1;
}

bool sja1105_rxtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;

	if (!(priv->hwts_rx_en & BIT(port)))
		return false;

	/* We need to read the full PTP clock to reconstruct the Rx
	 * timestamp. For that we need a sleepable context.
	 */
	skb_queue_tail(&ptp_data->skb_rxtstamp_queue, skb);
	ptp_schedule_worker(ptp_data->clock, 0);
	return true;
}

bool sja1110_rxtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps *shwt = skb_hwtstamps(skb);
	u64 ts = SJA1105_SKB_CB(skb)->tstamp;

	*shwt = (struct skb_shared_hwtstamps) {0};

	shwt->hwtstamp = ns_to_ktime(sja1105_ticks_to_ns(ts));

	/* Don't defer */
	return false;
}

/* Called from dsa_skb_defer_rx_timestamp */
bool sja1105_port_rxtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type)
{
	struct sja1105_private *priv = ds->priv;

	return priv->info->rxtstamp(ds, port, skb);
}

void sja1110_process_meta_tstamp(struct dsa_switch *ds, int port, u8 ts_id,
				 enum sja1110_meta_tstamp dir, u64 tstamp)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;
	struct sk_buff *skb, *skb_tmp, *skb_match = NULL;
	struct skb_shared_hwtstamps shwt = {0};

	/* We don't care about RX timestamps on the CPU port */
	if (dir == SJA1110_META_TSTAMP_RX)
		return;

	spin_lock(&ptp_data->skb_txtstamp_queue.lock);

	skb_queue_walk_safe(&ptp_data->skb_txtstamp_queue, skb, skb_tmp) {
		if (SJA1105_SKB_CB(skb)->ts_id != ts_id)
			continue;

		__skb_unlink(skb, &ptp_data->skb_txtstamp_queue);
		skb_match = skb;

		break;
	}

	spin_unlock(&ptp_data->skb_txtstamp_queue.lock);

	if (WARN_ON(!skb_match))
		return;

	shwt.hwtstamp = ns_to_ktime(sja1105_ticks_to_ns(tstamp));
	skb_complete_tx_timestamp(skb_match, &shwt);
}

/* In addition to cloning the skb which is done by the common
 * sja1105_port_txtstamp, we need to generate a timestamp ID and save the
 * packet to the TX timestamping queue.
 */
void sja1110_txtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb)
{
	struct sk_buff *clone = SJA1105_SKB_CB(skb)->clone;
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;
	u8 ts_id;

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	spin_lock(&priv->ts_id_lock);

	ts_id = priv->ts_id;
	/* Deal automatically with 8-bit wraparound */
	priv->ts_id++;

	SJA1105_SKB_CB(clone)->ts_id = ts_id;

	spin_unlock(&priv->ts_id_lock);

	skb_queue_tail(&ptp_data->skb_txtstamp_queue, clone);
}

/* Called from dsa_skb_tx_timestamp. This callback is just to clone
 * the skb and have it available in SJA1105_SKB_CB in the .port_deferred_xmit
 * callback, where we will timestamp it synchronously.
 */
void sja1105_port_txtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb)
{
	struct sja1105_private *priv = ds->priv;
	struct sk_buff *clone;

	if (!(priv->hwts_tx_en & BIT(port)))
		return;

	clone = skb_clone_sk(skb);
	if (!clone)
		return;

	SJA1105_SKB_CB(skb)->clone = clone;

	if (priv->info->txtstamp)
		priv->info->txtstamp(ds, port, skb);
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

static void sja1105_ptp_extts_setup_timer(struct sja1105_ptp_data *ptp_data)
{
	unsigned long expires = ((jiffies / SJA1105_EXTTS_INTERVAL) + 1) *
				SJA1105_EXTTS_INTERVAL;

	mod_timer(&ptp_data->extts_timer, expires);
}

static void sja1105_ptp_extts_timer(struct timer_list *t)
{
	struct sja1105_ptp_data *ptp_data = extts_to_data(t);

	ptp_schedule_worker(ptp_data->clock, 0);

	sja1105_ptp_extts_setup_timer(ptp_data);
}

static int sja1105_change_ptp_clk_pin_func(struct sja1105_private *priv,
					   enum ptp_pin_function func)
{
	struct sja1105_avb_params_entry *avb;
	enum ptp_pin_function old_func;

	avb = priv->static_config.tables[BLK_IDX_AVB_PARAMS].entries;

	if (priv->info->device_id == SJA1105E_DEVICE_ID ||
	    priv->info->device_id == SJA1105T_DEVICE_ID ||
	    avb->cas_master)
		old_func = PTP_PF_PEROUT;
	else
		old_func = PTP_PF_EXTTS;

	if (func == old_func)
		return 0;

	avb->cas_master = (func == PTP_PF_PEROUT);

	return sja1105_dynamic_config_write(priv, BLK_IDX_AVB_PARAMS, 0, avb,
					    true);
}

/* The PTP_CLK pin may be configured to toggle with a 50% duty cycle and a
 * frequency f:
 *
 *           NSEC_PER_SEC
 * f = ----------------------
 *     (PTPPINDUR * 8 ns) * 2
 */
static int sja1105_per_out_enable(struct sja1105_private *priv,
				  struct ptp_perout_request *perout,
				  bool on)
{
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_ptp_cmd cmd = ptp_data->cmd;
	int rc;

	/* We only support one channel */
	if (perout->index != 0)
		return -EOPNOTSUPP;

	/* Reject requests with unsupported flags */
	if (perout->flags)
		return -EOPNOTSUPP;

	mutex_lock(&ptp_data->lock);

	rc = sja1105_change_ptp_clk_pin_func(priv, PTP_PF_PEROUT);
	if (rc)
		goto out;

	if (on) {
		struct timespec64 pin_duration_ts = {
			.tv_sec = perout->period.sec,
			.tv_nsec = perout->period.nsec,
		};
		struct timespec64 pin_start_ts = {
			.tv_sec = perout->start.sec,
			.tv_nsec = perout->start.nsec,
		};
		u64 pin_duration = timespec64_to_ns(&pin_duration_ts);
		u64 pin_start = timespec64_to_ns(&pin_start_ts);
		u32 pin_duration32;
		u64 now;

		/* ptppindur: 32 bit register which holds the interval between
		 * 2 edges on PTP_CLK. So check for truncation which happens
		 * at periods larger than around 68.7 seconds.
		 */
		pin_duration = ns_to_sja1105_ticks(pin_duration / 2);
		if (pin_duration > U32_MAX) {
			rc = -ERANGE;
			goto out;
		}
		pin_duration32 = pin_duration;

		/* ptppins: 64 bit register which needs to hold a PTP time
		 * larger than the current time, otherwise the startptpcp
		 * command won't do anything. So advance the current time
		 * by a number of periods in a way that won't alter the
		 * phase offset.
		 */
		rc = __sja1105_ptp_gettimex(priv->ds, &now, NULL);
		if (rc < 0)
			goto out;

		pin_start = future_base_time(pin_start, pin_duration,
					     now + 1ull * NSEC_PER_SEC);
		pin_start = ns_to_sja1105_ticks(pin_start);

		rc = sja1105_xfer_u64(priv, SPI_WRITE, regs->ptppinst,
				      &pin_start, NULL);
		if (rc < 0)
			goto out;

		rc = sja1105_xfer_u32(priv, SPI_WRITE, regs->ptppindur,
				      &pin_duration32, NULL);
		if (rc < 0)
			goto out;
	}

	if (on)
		cmd.startptpcp = true;
	else
		cmd.stopptpcp = true;

	rc = sja1105_ptp_commit(priv->ds, &cmd, SPI_WRITE);

out:
	mutex_unlock(&ptp_data->lock);

	return rc;
}

static int sja1105_extts_enable(struct sja1105_private *priv,
				struct ptp_extts_request *extts,
				bool on)
{
	int rc;

	/* We only support one channel */
	if (extts->index != 0)
		return -EOPNOTSUPP;

	/* Reject requests with unsupported flags */
	if (extts->flags & ~(PTP_ENABLE_FEATURE |
			     PTP_RISING_EDGE |
			     PTP_FALLING_EDGE |
			     PTP_STRICT_FLAGS))
		return -EOPNOTSUPP;

	/* We can only enable time stamping on both edges, sadly. */
	if ((extts->flags & PTP_STRICT_FLAGS) &&
	    (extts->flags & PTP_ENABLE_FEATURE) &&
	    (extts->flags & PTP_EXTTS_EDGES) != PTP_EXTTS_EDGES)
		return -EOPNOTSUPP;

	rc = sja1105_change_ptp_clk_pin_func(priv, PTP_PF_EXTTS);
	if (rc)
		return rc;

	priv->ptp_data.extts_enabled = on;

	if (on)
		sja1105_ptp_extts_setup_timer(&priv->ptp_data);
	else
		timer_delete_sync(&priv->ptp_data.extts_timer);

	return 0;
}

static int sja1105_ptp_enable(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *req, int on)
{
	struct sja1105_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct sja1105_private *priv = ptp_data_to_sja1105(ptp_data);
	int rc = -EOPNOTSUPP;

	if (req->type == PTP_CLK_REQ_PEROUT)
		rc = sja1105_per_out_enable(priv, &req->perout, on);
	else if (req->type == PTP_CLK_REQ_EXTTS)
		rc = sja1105_extts_enable(priv, &req->extts, on);

	return rc;
}

static int sja1105_ptp_verify_pin(struct ptp_clock_info *ptp, unsigned int pin,
				  enum ptp_pin_function func, unsigned int chan)
{
	struct sja1105_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct sja1105_private *priv = ptp_data_to_sja1105(ptp_data);

	if (chan != 0 || pin != 0)
		return -1;

	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
		break;
	case PTP_PF_EXTTS:
		if (priv->info->device_id == SJA1105E_DEVICE_ID ||
		    priv->info->device_id == SJA1105T_DEVICE_ID)
			return -1;
		break;
	default:
		return -1;
	}
	return 0;
}

static struct ptp_pin_desc sja1105_ptp_pin = {
	.name = "ptp_clk",
	.index = 0,
	.func = PTP_PF_NONE,
};

int sja1105_ptp_clock_register(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;

	ptp_data->caps = (struct ptp_clock_info) {
		.owner		= THIS_MODULE,
		.name		= "SJA1105 PHC",
		.adjfine	= sja1105_ptp_adjfine,
		.adjtime	= sja1105_ptp_adjtime,
		.gettimex64	= sja1105_ptp_gettimex,
		.settime64	= sja1105_ptp_settime,
		.enable		= sja1105_ptp_enable,
		.verify		= sja1105_ptp_verify_pin,
		.do_aux_work	= sja1105_rxtstamp_work,
		.max_adj	= SJA1105_MAX_ADJ_PPB,
		.pin_config	= &sja1105_ptp_pin,
		.n_pins		= 1,
		.n_ext_ts	= 1,
		.n_per_out	= 1,
	};

	/* Only used on SJA1105 */
	skb_queue_head_init(&ptp_data->skb_rxtstamp_queue);
	/* Only used on SJA1110 */
	skb_queue_head_init(&ptp_data->skb_txtstamp_queue);

	ptp_data->clock = ptp_clock_register(&ptp_data->caps, ds->dev);
	if (IS_ERR_OR_NULL(ptp_data->clock))
		return PTR_ERR(ptp_data->clock);

	ptp_data->cmd.corrclk4ts = true;
	ptp_data->cmd.ptpclkadd = PTP_SET_MODE;

	timer_setup(&ptp_data->extts_timer, sja1105_ptp_extts_timer, 0);

	return sja1105_ptp_reset(ds);
}

void sja1105_ptp_clock_unregister(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;

	if (IS_ERR_OR_NULL(ptp_data->clock))
		return;

	timer_delete_sync(&ptp_data->extts_timer);
	ptp_cancel_worker_sync(ptp_data->clock);
	skb_queue_purge(&ptp_data->skb_txtstamp_queue);
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

	rc = sja1105_ptpegr_ts_poll(ds, port, &ts);
	if (rc < 0) {
		dev_err(ds->dev, "timed out polling for tstamp\n");
		kfree_skb(skb);
		goto out;
	}

	rc = sja1105_ptpclkval_read(priv, &ticks, NULL);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to read PTP clock: %d\n", rc);
		kfree_skb(skb);
		goto out;
	}

	ts = sja1105_tstamp_reconstruct(ds, ticks, ts);

	shwt.hwtstamp = ns_to_ktime(sja1105_ticks_to_ns(ts));
	skb_complete_tx_timestamp(skb, &shwt);

out:
	mutex_unlock(&ptp_data->lock);
}
