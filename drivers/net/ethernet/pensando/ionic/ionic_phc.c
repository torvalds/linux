// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2021 Pensando Systems, Inc */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_ethtool.h"

static int ionic_hwstamp_tx_mode(int config_tx_type)
{
	switch (config_tx_type) {
	case HWTSTAMP_TX_OFF:
		return IONIC_TXSTAMP_OFF;
	case HWTSTAMP_TX_ON:
		return IONIC_TXSTAMP_ON;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		return IONIC_TXSTAMP_ONESTEP_SYNC;
	case HWTSTAMP_TX_ONESTEP_P2P:
		return IONIC_TXSTAMP_ONESTEP_P2P;
	default:
		return -ERANGE;
	}
}

static u64 ionic_hwstamp_rx_filt(int config_rx_filter)
{
	switch (config_rx_filter) {
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		return IONIC_PKT_CLS_PTP1_ALL;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		return IONIC_PKT_CLS_PTP1_SYNC;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		return IONIC_PKT_CLS_PTP1_SYNC | IONIC_PKT_CLS_PTP1_DREQ;

	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		return IONIC_PKT_CLS_PTP2_L4_ALL;
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		return IONIC_PKT_CLS_PTP2_L4_SYNC;
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		return IONIC_PKT_CLS_PTP2_L4_SYNC | IONIC_PKT_CLS_PTP2_L4_DREQ;

	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		return IONIC_PKT_CLS_PTP2_L2_ALL;
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
		return IONIC_PKT_CLS_PTP2_L2_SYNC;
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		return IONIC_PKT_CLS_PTP2_L2_SYNC | IONIC_PKT_CLS_PTP2_L2_DREQ;

	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		return IONIC_PKT_CLS_PTP2_ALL;
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		return IONIC_PKT_CLS_PTP2_SYNC;
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		return IONIC_PKT_CLS_PTP2_SYNC | IONIC_PKT_CLS_PTP2_DREQ;

	case HWTSTAMP_FILTER_NTP_ALL:
		return IONIC_PKT_CLS_NTP_ALL;

	default:
		return 0;
	}
}

static int ionic_lif_hwstamp_set_ts_config(struct ionic_lif *lif,
					   struct hwtstamp_config *new_ts)
{
	struct ionic *ionic = lif->ionic;
	struct hwtstamp_config *config;
	struct hwtstamp_config ts;
	int tx_mode = 0;
	u64 rx_filt = 0;
	int err, err2;
	bool rx_all;
	__le64 mask;

	if (!lif->phc || !lif->phc->ptp)
		return -EOPNOTSUPP;

	mutex_lock(&lif->phc->config_lock);

	if (new_ts) {
		config = new_ts;
	} else {
		/* If called with new_ts == NULL, replay the previous request
		 * primarily for recovery after a FW_RESET.
		 * We saved the previous configuration request info, so copy
		 * the previous request for reference, clear the current state
		 * to match the device's reset state, and run with it.
		 */
		config = &ts;
		memcpy(config, &lif->phc->ts_config, sizeof(*config));
		memset(&lif->phc->ts_config, 0, sizeof(lif->phc->ts_config));
		lif->phc->ts_config_tx_mode = 0;
		lif->phc->ts_config_rx_filt = 0;
	}

	tx_mode = ionic_hwstamp_tx_mode(config->tx_type);
	if (tx_mode < 0) {
		err = tx_mode;
		goto err_queues;
	}

	mask = cpu_to_le64(BIT_ULL(tx_mode));
	if ((ionic->ident.lif.eth.hwstamp_tx_modes & mask) != mask) {
		err = -ERANGE;
		goto err_queues;
	}

	rx_filt = ionic_hwstamp_rx_filt(config->rx_filter);
	rx_all = config->rx_filter != HWTSTAMP_FILTER_NONE && !rx_filt;

	mask = cpu_to_le64(rx_filt);
	if ((ionic->ident.lif.eth.hwstamp_rx_filters & mask) != mask) {
		rx_filt = 0;
		rx_all = true;
		config->rx_filter = HWTSTAMP_FILTER_ALL;
	}

	dev_dbg(ionic->dev, "%s: config_rx_filter %d rx_filt %#llx rx_all %d\n",
		__func__, config->rx_filter, rx_filt, rx_all);

	if (tx_mode) {
		err = ionic_lif_create_hwstamp_txq(lif);
		if (err)
			goto err_queues;
	}

	if (rx_filt) {
		err = ionic_lif_create_hwstamp_rxq(lif);
		if (err)
			goto err_queues;
	}

	if (tx_mode != lif->phc->ts_config_tx_mode) {
		err = ionic_lif_set_hwstamp_txmode(lif, tx_mode);
		if (err)
			goto err_txmode;
	}

	if (rx_filt != lif->phc->ts_config_rx_filt) {
		err = ionic_lif_set_hwstamp_rxfilt(lif, rx_filt);
		if (err)
			goto err_rxfilt;
	}

	if (rx_all != (lif->phc->ts_config.rx_filter == HWTSTAMP_FILTER_ALL)) {
		err = ionic_lif_config_hwstamp_rxq_all(lif, rx_all);
		if (err)
			goto err_rxall;
	}

	memcpy(&lif->phc->ts_config, config, sizeof(*config));
	lif->phc->ts_config_rx_filt = rx_filt;
	lif->phc->ts_config_tx_mode = tx_mode;

	mutex_unlock(&lif->phc->config_lock);

	return 0;

err_rxall:
	if (rx_filt != lif->phc->ts_config_rx_filt) {
		rx_filt = lif->phc->ts_config_rx_filt;
		err2 = ionic_lif_set_hwstamp_rxfilt(lif, rx_filt);
		if (err2)
			dev_err(ionic->dev,
				"Failed to revert rx timestamp filter: %d\n", err2);
	}
err_rxfilt:
	if (tx_mode != lif->phc->ts_config_tx_mode) {
		tx_mode = lif->phc->ts_config_tx_mode;
		err2 = ionic_lif_set_hwstamp_txmode(lif, tx_mode);
		if (err2)
			dev_err(ionic->dev,
				"Failed to revert tx timestamp mode: %d\n", err2);
	}
err_txmode:
	/* special queues remain allocated, just unused */
err_queues:
	mutex_unlock(&lif->phc->config_lock);
	return err;
}

int ionic_lif_hwstamp_set(struct ionic_lif *lif, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (!lif->phc || !lif->phc->ptp)
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	mutex_lock(&lif->queue_lock);
	err = ionic_lif_hwstamp_set_ts_config(lif, &config);
	mutex_unlock(&lif->queue_lock);
	if (err) {
		netdev_info(lif->netdev, "hwstamp set failed: %d\n", err);
		return err;
	}

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
}

void ionic_lif_hwstamp_replay(struct ionic_lif *lif)
{
	int err;

	if (!lif->phc || !lif->phc->ptp)
		return;

	mutex_lock(&lif->queue_lock);
	err = ionic_lif_hwstamp_set_ts_config(lif, NULL);
	mutex_unlock(&lif->queue_lock);
	if (err)
		netdev_info(lif->netdev, "hwstamp replay failed: %d\n", err);
}

void ionic_lif_hwstamp_recreate_queues(struct ionic_lif *lif)
{
	int err;

	if (!lif->phc || !lif->phc->ptp)
		return;

	mutex_lock(&lif->phc->config_lock);

	if (lif->phc->ts_config_tx_mode) {
		err = ionic_lif_create_hwstamp_txq(lif);
		if (err)
			netdev_info(lif->netdev, "hwstamp recreate txq failed: %d\n", err);
	}

	if (lif->phc->ts_config_rx_filt) {
		err = ionic_lif_create_hwstamp_rxq(lif);
		if (err)
			netdev_info(lif->netdev, "hwstamp recreate rxq failed: %d\n", err);
	}

	mutex_unlock(&lif->phc->config_lock);
}

int ionic_lif_hwstamp_get(struct ionic_lif *lif, struct ifreq *ifr)
{
	struct hwtstamp_config config;

	if (!lif->phc || !lif->phc->ptp)
		return -EOPNOTSUPP;

	mutex_lock(&lif->phc->config_lock);
	memcpy(&config, &lif->phc->ts_config, sizeof(config));
	mutex_unlock(&lif->phc->config_lock);

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;
	return 0;
}

static u64 ionic_hwstamp_read(struct ionic *ionic,
			      struct ptp_system_timestamp *sts)
{
	u32 tick_high_before, tick_high, tick_low;

	/* read and discard low part to defeat hw staging of high part */
	(void)ioread32(&ionic->idev.hwstamp_regs->tick_low);

	tick_high_before = ioread32(&ionic->idev.hwstamp_regs->tick_high);

	ptp_read_system_prets(sts);
	tick_low = ioread32(&ionic->idev.hwstamp_regs->tick_low);
	ptp_read_system_postts(sts);

	tick_high = ioread32(&ionic->idev.hwstamp_regs->tick_high);

	/* If tick_high changed, re-read tick_low once more.  Assume tick_high
	 * cannot change again so soon as in the span of re-reading tick_low.
	 */
	if (tick_high != tick_high_before) {
		ptp_read_system_prets(sts);
		tick_low = ioread32(&ionic->idev.hwstamp_regs->tick_low);
		ptp_read_system_postts(sts);
	}

	return (u64)tick_low | ((u64)tick_high << 32);
}

static u64 ionic_cc_read(const struct cyclecounter *cc)
{
	struct ionic_phc *phc = container_of(cc, struct ionic_phc, cc);
	struct ionic *ionic = phc->lif->ionic;

	return ionic_hwstamp_read(ionic, NULL);
}

static int ionic_setphc_cmd(struct ionic_phc *phc, struct ionic_admin_ctx *ctx)
{
	ctx->work = COMPLETION_INITIALIZER_ONSTACK(ctx->work);

	ctx->cmd.lif_setphc.opcode = IONIC_CMD_LIF_SETPHC;
	ctx->cmd.lif_setphc.lif_index = cpu_to_le16(phc->lif->index);

	ctx->cmd.lif_setphc.tick = cpu_to_le64(phc->tc.cycle_last);
	ctx->cmd.lif_setphc.nsec = cpu_to_le64(phc->tc.nsec);
	ctx->cmd.lif_setphc.frac = cpu_to_le64(phc->tc.frac);
	ctx->cmd.lif_setphc.mult = cpu_to_le32(phc->cc.mult);
	ctx->cmd.lif_setphc.shift = cpu_to_le32(phc->cc.shift);

	return ionic_adminq_post(phc->lif, ctx);
}

static int ionic_phc_adjfine(struct ptp_clock_info *info, long scaled_ppm)
{
	struct ionic_phc *phc = container_of(info, struct ionic_phc, ptp_info);
	struct ionic_admin_ctx ctx = {};
	unsigned long irqflags;
	s64 adj;
	int err;

	/* Reject phc adjustments during device upgrade */
	if (test_bit(IONIC_LIF_F_FW_RESET, phc->lif->state))
		return -EBUSY;

	/* Adjustment value scaled by 2^16 million */
	adj = (s64)scaled_ppm * phc->init_cc_mult;

	/* Adjustment value to scale */
	adj /= (s64)SCALED_PPM;

	/* Final adjusted multiplier */
	adj += phc->init_cc_mult;

	spin_lock_irqsave(&phc->lock, irqflags);

	/* update the point-in-time basis to now, before adjusting the rate */
	timecounter_read(&phc->tc);
	phc->cc.mult = adj;

	/* Setphc commands are posted in-order, sequenced by phc->lock.  We
	 * need to drop the lock before waiting for the command to complete.
	 */
	err = ionic_setphc_cmd(phc, &ctx);

	spin_unlock_irqrestore(&phc->lock, irqflags);

	return ionic_adminq_wait(phc->lif, &ctx, err);
}

static int ionic_phc_adjtime(struct ptp_clock_info *info, s64 delta)
{
	struct ionic_phc *phc = container_of(info, struct ionic_phc, ptp_info);
	struct ionic_admin_ctx ctx = {};
	unsigned long irqflags;
	int err;

	/* Reject phc adjustments during device upgrade */
	if (test_bit(IONIC_LIF_F_FW_RESET, phc->lif->state))
		return -EBUSY;

	spin_lock_irqsave(&phc->lock, irqflags);

	timecounter_adjtime(&phc->tc, delta);

	/* Setphc commands are posted in-order, sequenced by phc->lock.  We
	 * need to drop the lock before waiting for the command to complete.
	 */
	err = ionic_setphc_cmd(phc, &ctx);

	spin_unlock_irqrestore(&phc->lock, irqflags);

	return ionic_adminq_wait(phc->lif, &ctx, err);
}

static int ionic_phc_settime64(struct ptp_clock_info *info,
			       const struct timespec64 *ts)
{
	struct ionic_phc *phc = container_of(info, struct ionic_phc, ptp_info);
	struct ionic_admin_ctx ctx = {};
	unsigned long irqflags;
	int err;
	u64 ns;

	/* Reject phc adjustments during device upgrade */
	if (test_bit(IONIC_LIF_F_FW_RESET, phc->lif->state))
		return -EBUSY;

	ns = timespec64_to_ns(ts);

	spin_lock_irqsave(&phc->lock, irqflags);

	timecounter_init(&phc->tc, &phc->cc, ns);

	/* Setphc commands are posted in-order, sequenced by phc->lock.  We
	 * need to drop the lock before waiting for the command to complete.
	 */
	err = ionic_setphc_cmd(phc, &ctx);

	spin_unlock_irqrestore(&phc->lock, irqflags);

	return ionic_adminq_wait(phc->lif, &ctx, err);
}

static int ionic_phc_gettimex64(struct ptp_clock_info *info,
				struct timespec64 *ts,
				struct ptp_system_timestamp *sts)
{
	struct ionic_phc *phc = container_of(info, struct ionic_phc, ptp_info);
	struct ionic *ionic = phc->lif->ionic;
	unsigned long irqflags;
	u64 tick, ns;

	/* Do not attempt to read device time during upgrade */
	if (test_bit(IONIC_LIF_F_FW_RESET, phc->lif->state))
		return -EBUSY;

	spin_lock_irqsave(&phc->lock, irqflags);

	tick = ionic_hwstamp_read(ionic, sts);

	ns = timecounter_cyc2time(&phc->tc, tick);

	spin_unlock_irqrestore(&phc->lock, irqflags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static long ionic_phc_aux_work(struct ptp_clock_info *info)
{
	struct ionic_phc *phc = container_of(info, struct ionic_phc, ptp_info);
	struct ionic_admin_ctx ctx = {};
	unsigned long irqflags;
	int err;

	/* Do not update phc during device upgrade, but keep polling to resume
	 * after upgrade.  Since we don't update the point in time basis, there
	 * is no expectation that we are maintaining the phc time during the
	 * upgrade.  After upgrade, it will need to be readjusted back to the
	 * correct time by the ptp daemon.
	 */
	if (test_bit(IONIC_LIF_F_FW_RESET, phc->lif->state))
		return phc->aux_work_delay;

	spin_lock_irqsave(&phc->lock, irqflags);

	/* update point-in-time basis to now */
	timecounter_read(&phc->tc);

	/* Setphc commands are posted in-order, sequenced by phc->lock.  We
	 * need to drop the lock before waiting for the command to complete.
	 */
	err = ionic_setphc_cmd(phc, &ctx);

	spin_unlock_irqrestore(&phc->lock, irqflags);

	ionic_adminq_wait(phc->lif, &ctx, err);

	return phc->aux_work_delay;
}

ktime_t ionic_lif_phc_ktime(struct ionic_lif *lif, u64 tick)
{
	unsigned long irqflags;
	u64 ns;

	if (!lif->phc)
		return 0;

	spin_lock_irqsave(&lif->phc->lock, irqflags);
	ns = timecounter_cyc2time(&lif->phc->tc, tick);
	spin_unlock_irqrestore(&lif->phc->lock, irqflags);

	return ns_to_ktime(ns);
}

static const struct ptp_clock_info ionic_ptp_info = {
	.owner		= THIS_MODULE,
	.name		= "ionic_ptp",
	.adjfine	= ionic_phc_adjfine,
	.adjtime	= ionic_phc_adjtime,
	.gettimex64	= ionic_phc_gettimex64,
	.settime64	= ionic_phc_settime64,
	.do_aux_work	= ionic_phc_aux_work,
};

void ionic_lif_register_phc(struct ionic_lif *lif)
{
	if (!lif->phc || !(lif->hw_features & IONIC_ETH_HW_TIMESTAMP))
		return;

	lif->phc->ptp = ptp_clock_register(&lif->phc->ptp_info, lif->ionic->dev);

	if (IS_ERR(lif->phc->ptp)) {
		dev_warn(lif->ionic->dev, "Cannot register phc device: %ld\n",
			 PTR_ERR(lif->phc->ptp));

		lif->phc->ptp = NULL;
	}

	if (lif->phc->ptp)
		ptp_schedule_worker(lif->phc->ptp, lif->phc->aux_work_delay);
}

void ionic_lif_unregister_phc(struct ionic_lif *lif)
{
	if (!lif->phc || !lif->phc->ptp)
		return;

	ptp_clock_unregister(lif->phc->ptp);

	lif->phc->ptp = NULL;
}

void ionic_lif_alloc_phc(struct ionic_lif *lif)
{
	struct ionic *ionic = lif->ionic;
	struct ionic_phc *phc;
	u64 delay, diff, mult;
	u64 frac = 0;
	u64 features;
	u32 shift;

	if (!ionic->idev.hwstamp_regs)
		return;

	features = le64_to_cpu(ionic->ident.lif.eth.config.features);
	if (!(features & IONIC_ETH_HW_TIMESTAMP))
		return;

	phc = devm_kzalloc(ionic->dev, sizeof(*phc), GFP_KERNEL);
	if (!phc)
		return;

	phc->lif = lif;

	phc->cc.read = ionic_cc_read;
	phc->cc.mask = le64_to_cpu(ionic->ident.dev.hwstamp_mask);
	phc->cc.mult = le32_to_cpu(ionic->ident.dev.hwstamp_mult);
	phc->cc.shift = le32_to_cpu(ionic->ident.dev.hwstamp_shift);

	if (!phc->cc.mult) {
		dev_err(lif->ionic->dev,
			"Invalid device PHC mask multiplier %u, disabling HW timestamp support\n",
			phc->cc.mult);
		devm_kfree(lif->ionic->dev, phc);
		lif->phc = NULL;
		return;
	}

	dev_dbg(lif->ionic->dev, "Device PHC mask %#llx mult %u shift %u\n",
		phc->cc.mask, phc->cc.mult, phc->cc.shift);

	spin_lock_init(&phc->lock);
	mutex_init(&phc->config_lock);

	/* max ticks is limited by the multiplier, or by the update period. */
	if (phc->cc.shift + 2 + ilog2(IONIC_PHC_UPDATE_NS) >= 64) {
		/* max ticks that do not overflow when multiplied by max
		 * adjusted multiplier (twice the initial multiplier)
		 */
		diff = U64_MAX / phc->cc.mult / 2;
	} else {
		/* approx ticks at four times the update period */
		diff = (u64)IONIC_PHC_UPDATE_NS << (phc->cc.shift + 2);
		diff = DIV_ROUND_UP(diff, phc->cc.mult);
	}

	/* transform to bitmask */
	diff |= diff >> 1;
	diff |= diff >> 2;
	diff |= diff >> 4;
	diff |= diff >> 8;
	diff |= diff >> 16;
	diff |= diff >> 32;

	/* constrain to the hardware bitmask, and use this as the bitmask */
	diff &= phc->cc.mask;
	phc->cc.mask = diff;

	/* the wrap period is now defined by diff (or phc->cc.mask)
	 *
	 * we will update the time basis at about 1/4 the wrap period, so
	 * should not see a difference of more than +/- diff/4.
	 *
	 * this is sufficient not see a difference of more than +/- diff/2, as
	 * required by timecounter_cyc2time, to detect an old time stamp.
	 *
	 * adjust the initial multiplier, being careful to avoid overflow:
	 *  - do not overflow 63 bits: init_cc_mult * SCALED_PPM
	 *  - do not overflow 64 bits: max_mult * (diff / 2)
	 *
	 * we want to increase the initial multiplier as much as possible, to
	 * allow for more precise adjustment in ionic_phc_adjfine.
	 *
	 * only adjust the multiplier if we can double it or more.
	 */
	mult = U64_MAX / 2 / max(diff / 2, SCALED_PPM);
	shift = mult / phc->cc.mult;
	if (shift >= 2) {
		/* initial multiplier will be 2^n of hardware cc.mult */
		shift = fls(shift);
		/* increase cc.mult and cc.shift by the same 2^n and n. */
		phc->cc.mult <<= shift;
		phc->cc.shift += shift;
	}

	dev_dbg(lif->ionic->dev, "Initial PHC mask %#llx mult %u shift %u\n",
		phc->cc.mask, phc->cc.mult, phc->cc.shift);

	/* frequency adjustments are relative to the initial multiplier */
	phc->init_cc_mult = phc->cc.mult;

	timecounter_init(&phc->tc, &phc->cc, ktime_get_real_ns());

	/* Update cycle_last at 1/4 the wrap period, or IONIC_PHC_UPDATE_NS */
	delay = min_t(u64, IONIC_PHC_UPDATE_NS,
		      cyclecounter_cyc2ns(&phc->cc, diff / 4, 0, &frac));
	dev_dbg(lif->ionic->dev, "Work delay %llu ms\n", delay / NSEC_PER_MSEC);

	phc->aux_work_delay = nsecs_to_jiffies(delay);

	phc->ptp_info = ionic_ptp_info;

	/* We have allowed to adjust the multiplier up to +/- 1 part per 1.
	 * Here expressed as NORMAL_PPB (1 billion parts per billion).
	 */
	phc->ptp_info.max_adj = NORMAL_PPB;

	lif->phc = phc;
}

void ionic_lif_free_phc(struct ionic_lif *lif)
{
	if (!lif->phc)
		return;

	mutex_destroy(&lif->phc->config_lock);

	devm_kfree(lif->ionic->dev, lif->phc);
	lif->phc = NULL;
}
