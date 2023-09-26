// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Technologies, Inc. SDHCI Platform driver.
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of_device.h>
#include "sdhci-msm.h"
#include "sdhci-msm-scaling.h"

#define cls_dev_to_mmc_host(d)  container_of(d, struct mmc_host, class_dev)

static int mmc_dt_get_array(struct device *dev, const char *prop_name,
				u32 **out, int *len, u32 size)
{
	int ret = 0;
	struct device_node *np = dev->of_node;
	size_t sz;
	u32 *arr = NULL;

	if (!of_get_property(np, prop_name, len)) {
		ret = -EINVAL;
		goto out;
	}
	sz = *len = *len / sizeof(*arr);
	if (sz <= 0 || (size > 0 && (sz > size))) {
		dev_err(dev, "%s invalid size\n", prop_name);
		ret = -EINVAL;
		goto out;
	}

	arr = devm_kcalloc(dev, sz, sizeof(*arr), GFP_KERNEL);
	if (!arr) {
		ret = -ENOMEM;
		goto out;
	}

	ret = of_property_read_u32_array(np, prop_name, arr, sz);
	if (ret < 0) {
		dev_err(dev, "%s failed reading array %d\n", prop_name, ret);
		goto out;
	}
	*out = arr;
out:
	if (ret)
		*len = 0;
	return ret;

}

void sdhci_msm_scale_parse_dt(struct device *dev, struct sdhci_msm_host *msm_host)
{
	struct device_node *np = dev->of_node;
	const char *lower_bus_speed = NULL;

	if (mmc_dt_get_array(dev, "qcom,devfreq,freq-table",
				&msm_host->clk_scaling.pltfm_freq_table,
				&msm_host->clk_scaling.pltfm_freq_table_sz, 0))
		pr_debug("%s: no clock scaling frequencies were supplied\n",
				dev_name(dev));
	else if (!msm_host->clk_scaling.pltfm_freq_table ||
			msm_host->clk_scaling.pltfm_freq_table_sz)
		dev_info(dev, "bad dts clock scaling frequencies\n");

	/*
	 * Few hosts can support DDR52 mode at the same lower
	 * system voltage corner as high-speed mode. In such
	 * cases, it is always better to put it in DDR
	 * mode which will improve the performance
	 * without any power impact.
	 */
	if (!of_property_read_string(np, "qcom,scaling-lower-bus-speed-mode",
			&lower_bus_speed)) {
		if (!strcmp(lower_bus_speed, "DDR52"))
			msm_host->clk_scaling.lower_bus_speed_mode |=
				MMC_SCALING_LOWER_DDR52_MODE;
	}
}
EXPORT_SYMBOL_GPL(sdhci_msm_scale_parse_dt);

void sdhci_msm_dec_active_req(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	atomic_dec(&host->active_reqs);
}

void sdhci_msm_inc_active_req(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	atomic_inc(&host->active_reqs);
}

void sdhci_msm_is_dcmd(int data, int *err)
{
	if (data)
		*err = 1;
	else
		*err = 0;
}

void sdhci_msm_mmc_cqe_clk_scaling_stop_busy(struct mmc_host *mhost, struct mmc_request *mrq)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
							brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;
	int is_dcmd;

	sdhci_msm_dec_active_req(mhost);
	is_dcmd = (mmc_issue_type(mq, req) == MMC_ISSUE_DCMD);
	_sdhci_msm_mmc_cqe_clk_scaling_stop_busy(host, true, is_dcmd);
}
EXPORT_SYMBOL_GPL(sdhci_msm_mmc_cqe_clk_scaling_stop_busy);

void sdhci_msm_mmc_cqe_clk_scaling_start_busy(struct mmc_host *mhost, struct mmc_request *mrq)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
							brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;

	sdhci_msm_mmc_init_clk_scaling(mhost);

	if (host->defer_clk_scaling_resume == 1) {
		sdhci_msm_mmc_resume_clk_scaling(mhost);
		host->defer_clk_scaling_resume = 0;
	}

	sdhci_msm_inc_active_req(mhost);

	sdhci_msm_mmc_deferred_scaling(host);
	_sdhci_msm_mmc_cqe_clk_scaling_start_busy(mq, host, true);
}
EXPORT_SYMBOL_GPL(sdhci_msm_mmc_cqe_clk_scaling_start_busy);

void sdhci_msm_cqe_scaling_resume(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	if (host->scaling_suspended == 1) {
		sdhci_msm_mmc_resume_clk_scaling(mhost);
		host->scaling_suspended = 0;
	}
}
EXPORT_SYMBOL_GPL(sdhci_msm_cqe_scaling_resume);

void sdhci_msm_set_active_reqs(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	atomic_set(&host->active_reqs, 0);
}

void sdhci_msm_set_factors(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	host->clk_scaling.upthreshold = MMC_DEVFRQ_DEFAULT_UP_THRESHOLD;
	host->clk_scaling.downthreshold = MMC_DEVFRQ_DEFAULT_DOWN_THRESHOLD;
	host->clk_scaling.polling_delay_ms = MMC_DEVFRQ_DEFAULT_POLLING_MSEC;
	host->clk_scaling.skip_clk_scale_freq_update = false;
}

void sdhci_msm_mmc_init_setup_scaling(struct mmc_card *card, struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	host->clk_scaling_lowest = mhost->f_min;
	if ((card->mmc_avail_type & EXT_CSD_CARD_TYPE_HS400) ||
			(card->mmc_avail_type & EXT_CSD_CARD_TYPE_HS200))
		host->clk_scaling_highest = card->ext_csd.hs200_max_dtr;
	else if ((card->mmc_avail_type & EXT_CSD_CARD_TYPE_HS) ||
			(card->mmc_avail_type & EXT_CSD_CARD_TYPE_DDR_52))
		host->clk_scaling_highest = card->ext_csd.hs_max_dtr;
	else
		host->clk_scaling_highest = card->csd.max_dtr;
}


void sdhci_msm_mmc_exit_clk_scaling(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	if (host->scale_caps & MMC_CAP2_CLK_SCALE)
		_sdhci_msm_mmc_exit_clk_scaling(host);
}

void sdhci_msm_mmc_suspend_clk_scaling(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	_sdhci_msm_mmc_suspend_clk_scaling(host);

}
EXPORT_SYMBOL_GPL(sdhci_msm_mmc_suspend_clk_scaling);

void sdhci_msm_mmc_resume_clk_scaling(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	_sdhci_msm_mmc_resume_clk_scaling(host);
}
EXPORT_SYMBOL_GPL(sdhci_msm_mmc_resume_clk_scaling);

void sdhci_msm_mmc_init_clk_scaling(struct mmc_host *mhost)
{
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	if (host->clk_scale_init_done)
		return;
	sdhci_msm_set_factors(mhost);
	sdhci_msm_set_active_reqs(mhost);
	sdhci_msm_mmc_init_setup_scaling(mhost->card, mhost);

	_sdhci_msm_mmc_init_clk_scaling(host);
	host->clk_scale_init_done = 1;
}
EXPORT_SYMBOL_GPL(sdhci_msm_mmc_init_clk_scaling);

static int sdhci_msm_mmc_select_hs_ddr52(struct sdhci_msm_host *host, unsigned long freq)
{
	struct mmc_host *mhost = host->mmc;
	int err;

	mmc_select_hs(mhost->card);
	err = mmc_select_bus_width(mhost->card);
	if (err < 0) {
		pr_err("%s: %s: select_bus_width failed(%d)\n",
			mmc_hostname(mhost), __func__, err);
		return err;
	}

	err = mmc_select_hs_ddr(mhost->card);
	mmc_set_clock(mhost, freq);

	return err;
}

/*
 * Scale down from HS400 to HS in order to allow frequency change.
 * This is needed for cards that doesn't support changing frequency in HS400
 */
static int sdhci_msm_mmc_scale_low(struct sdhci_msm_host *host, unsigned long freq)
{
	struct mmc_host *mhost = host->mmc;
	int err = 0;

	mmc_set_timing(mhost, MMC_TIMING_LEGACY);
	mmc_set_clock(mhost, MMC_HIGH_26_MAX_DTR);

	if (host->clk_scaling.lower_bus_speed_mode &
	    MMC_SCALING_LOWER_DDR52_MODE) {
		err = sdhci_msm_mmc_select_hs_ddr52(host, freq);
		if (err)
			pr_err("%s: %s: failed to switch to DDR52: err: %d\n",
			       mmc_hostname(mhost), __func__, err);
		else
			return err;
	}

	err = mmc_select_hs(mhost->card);
	if (err) {
		pr_err("%s: %s: scaling low: failed (%d)\n",
		       mmc_hostname(mhost), __func__, err);
		return err;
	}

	err = mmc_select_bus_width(mhost->card);
	if (err < 0) {
		pr_err("%s: %s: select_bus_width failed(%d)\n",
			mmc_hostname(mhost), __func__, err);
		return err;
	}

	mmc_set_clock(mhost, freq);

	return 0;
}

/*
 * Scale UP from HS to HS200/H400
 */
static int sdhci_msm_mmc_scale_high(struct sdhci_msm_host *host)
{
	struct mmc_host *mhost = host->mmc;
	int err = 0;

	if (mmc_card_ddr52(mhost->card)) {
		mmc_set_timing(mhost, MMC_TIMING_LEGACY);
		mmc_set_clock(mhost, MMC_HIGH_26_MAX_DTR);
	}

	mmc_set_initial_state(mhost);
	err = mmc_select_timing(mhost->card);
	if (err) {
		pr_err("%s: %s: select hs400 failed (%d)\n",
				mmc_hostname(mhost), __func__, err);
		return err;
	}

	if (mmc_card_hs200(mhost->card)) {
		err = mmc_hs200_tuning(mhost->card);
		if (err) {
			pr_err("%s: %s: hs200 tuning failed (%d)\n",
					mmc_hostname(mhost), __func__, err);
			return err;
		}

		err = mmc_select_hs400(mhost->card);
		if (err) {
			pr_err("%s: %s: select hs400 failed (%d)\n",
				mmc_hostname(mhost), __func__, err);
			return err;
		}
	}

	return 0;
}

static int sdhci_msm_mmc_set_clock_bus_speed(struct sdhci_msm_host *host, unsigned long freq)
{
	int err = 0;

	if (freq == MMC_HS200_MAX_DTR)
		err = sdhci_msm_mmc_scale_high(host);
	else
		err = sdhci_msm_mmc_scale_low(host, freq);

	return err;
}

static inline unsigned long sdhci_msm_mmc_ddr_freq_accommodation(unsigned long freq)
{
	if (freq == MMC_HIGH_DDR_MAX_DTR)
		return freq;

	return freq/2;
}

/**
 * mmc_change_bus_speed() - Change MMC card bus frequency at runtime
 * @host: pointer to mmc host structure
 * @freq: pointer to desired frequency to be set
 *
 * Change the MMC card bus frequency at runtime after the card is
 * initialized. Callers are expected to make sure of the card's
 * state (DATA/RCV/TRANSFER) before changing the frequency at runtime.
 *
 * If the frequency to change is greater than max. supported by card,
 * *freq is changed to max. supported by card. If it is less than min.
 * supported by host, *freq is changed to min. supported by host.
 * Host is assumed to be calimed while calling this funciton.
 */
static int sdhci_msm_mmc_change_bus_speed(struct sdhci_msm_host *host, unsigned long *freq)
{
	struct mmc_host *mhost = host->mmc;
	int err = 0;
	struct mmc_card *card;
	unsigned long actual_freq;

	card = mhost->card;

	if (!card || !freq) {
		err = -EINVAL;
		goto out;
	}
	actual_freq = *freq;

	WARN_ON(!mhost->claimed);

	/*
	 * For scaling up/down HS400 we'll need special handling,
	 * for other timings we can simply do clock frequency change
	 */
	if (mmc_card_hs400(card) ||
		(!mmc_card_hs200(mhost->card) && *freq == MMC_HS200_MAX_DTR)) {
		err = sdhci_msm_mmc_set_clock_bus_speed(host, *freq);
		if (err) {
			pr_err("%s: %s: failed (%d)to set bus and clock speed (freq=%lu)\n",
				mmc_hostname(mhost), __func__, err, *freq);
			goto out;
		}
	} else if (mmc_card_hs200(mhost->card)) {
		mmc_set_clock(mhost, *freq);
		err = mmc_hs200_tuning(mhost->card);
		if (err) {
			pr_warn("%s: %s: tuning execution failed %d\n",
				mmc_hostname(card->host),
				__func__, err);
			mmc_set_clock(mhost, host->clk_scaling.curr_freq);
		}
	} else {
		if (mmc_card_ddr52(mhost->card))
			actual_freq = sdhci_msm_mmc_ddr_freq_accommodation(*freq);
		mmc_set_clock(mhost, actual_freq);
	}

out:
	return err;
}

bool sdhci_msm_mmc_is_data_request(u32 opcode)
{
	switch (opcode) {
	case MMC_READ_SINGLE_BLOCK:
	case MMC_READ_MULTIPLE_BLOCK:
	case MMC_WRITE_BLOCK:
	case MMC_WRITE_MULTIPLE_BLOCK:
		return true;
	default:
		return false;
	}
}

void _sdhci_msm_mmc_clk_scaling_start_busy(struct sdhci_msm_host *host, bool lock_needed)
{
	struct sdhci_msm_mmc_devfeq_clk_scaling *clk_scaling = &host->clk_scaling;
	unsigned long flags;

	if (!clk_scaling->enable)
		return;

	if (lock_needed)
		spin_lock_irqsave(&clk_scaling->lock, flags);

	clk_scaling->start_busy = ktime_get();
	clk_scaling->is_busy_started = true;

	if (lock_needed)
		spin_unlock_irqrestore(&clk_scaling->lock, flags);
}

void _sdhci_msm_mmc_clk_scaling_stop_busy(struct sdhci_msm_host *host, bool lock_needed)
{
	struct sdhci_msm_mmc_devfeq_clk_scaling *clk_scaling = &host->clk_scaling;
	unsigned long flags;

	if (!clk_scaling->enable)
		return;

	if (lock_needed)
		spin_lock_irqsave(&clk_scaling->lock, flags);

	if (!clk_scaling->is_busy_started) {
		WARN_ON(1);
		goto out;
	}

	clk_scaling->total_busy_time_us +=
		ktime_to_us(ktime_sub(ktime_get(),
			clk_scaling->start_busy));
	pr_debug("%s: accumulated busy time is %lu usec\n",
		mmc_hostname(host->mmc), clk_scaling->total_busy_time_us);
	clk_scaling->is_busy_started = false;

out:
	if (lock_needed)
		spin_unlock_irqrestore(&clk_scaling->lock, flags);
}

/* mmc_cqe_clk_scaling_start_busy() - start busy timer for data requests
 * @host: pointer to mmc host structure
 * @lock_needed: flag indication if locking is needed
 *
 * This function starts the busy timer in case it was not already started.
 */
void _sdhci_msm_mmc_cqe_clk_scaling_start_busy(struct mmc_queue *mq,
			struct sdhci_msm_host *host, bool lock_needed)
{
	unsigned long flags;

	if (!host->clk_scaling.enable)
		return;

	if (lock_needed)
		spin_lock_irqsave(&host->clk_scaling.lock, flags);

	if (!host->clk_scaling.is_busy_started &&
			!(mq->cqe_busy & MMC_CQE_DCMD_BUSY)) {
		host->clk_scaling.start_busy = ktime_get();
		host->clk_scaling.is_busy_started = true;
	}

	if (lock_needed)
		spin_unlock_irqrestore(&host->clk_scaling.lock, flags);
}
EXPORT_SYMBOL_GPL(_sdhci_msm_mmc_cqe_clk_scaling_start_busy);

/**
 * mmc_cqe_clk_scaling_stop_busy() - stop busy timer for last data requests
 * @host: pointer to mmc host structure
 * @lock_needed: flag indication if locking is needed
 *
 * This function stops the busy timer in case it is the last data request.
 * In case the current request is not the last one, the busy time till
 * now will be accumulated and the counter will be restarted.
 */
void _sdhci_msm_mmc_cqe_clk_scaling_stop_busy(struct sdhci_msm_host *host,
	bool lock_needed, int is_cqe_dcmd)
{
	unsigned int cqe_active_reqs = 0;
	unsigned long flags;

	if (!host->clk_scaling.enable)
		return;

	cqe_active_reqs = atomic_read(&host->active_reqs);

	if (lock_needed)
		spin_lock_irqsave(&host->clk_scaling.lock, flags);

	host->clk_scaling.total_busy_time_us +=
		ktime_to_us(ktime_sub(ktime_get(),
			host->clk_scaling.start_busy));

	if (cqe_active_reqs) {
		host->clk_scaling.is_busy_started = true;
		host->clk_scaling.start_busy = ktime_get();
	} else {
		host->clk_scaling.is_busy_started = false;
	}

	if (lock_needed)
		spin_unlock_irqrestore(&host->clk_scaling.lock, flags);

}
EXPORT_SYMBOL_GPL(_sdhci_msm_mmc_cqe_clk_scaling_stop_busy);

/**
 * mmc_can_scale_clk() - Check clock scaling capability
 * @host: pointer to mmc host structure
 */
bool sdhci_msm_mmc_can_scale_clk(struct sdhci_msm_host *msm_host)
{
	struct mmc_host *host = msm_host->mmc;

	if (!host) {
		pr_err("bad host parameter\n");
		WARN_ON(1);
		return false;
	}

	return msm_host->scale_caps & MMC_CAP2_CLK_SCALE;
}
EXPORT_SYMBOL_GPL(sdhci_msm_mmc_can_scale_clk);

static int sdhci_msm_mmc_devfreq_get_dev_status(struct device *dev,
		struct devfreq_dev_status *status)
{
	struct mmc_host *mhost = container_of(dev, struct mmc_host, class_dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);
	struct sdhci_msm_mmc_devfeq_clk_scaling *clk_scaling;
	unsigned long flags;

	if (!host) {
		pr_err("bad host parameter\n");
		WARN_ON(1);
		return -EINVAL;
	}

	clk_scaling = &host->clk_scaling;

	if (!clk_scaling->enable)
		return 0;

	spin_lock_irqsave(&host->clk_scaling.lock, flags);

	/* accumulate the busy time of ongoing work */
	memset(status, 0, sizeof(*status));
	if (clk_scaling->is_busy_started) {
		if (mhost->cqe_on) {
			/* the "busy-timer" will be restarted in case there
			 * are pending data requests
			 */
			_sdhci_msm_mmc_cqe_clk_scaling_stop_busy(host, false, false);
		} else {
			_sdhci_msm_mmc_clk_scaling_stop_busy(host, false);
			_sdhci_msm_mmc_clk_scaling_start_busy(host, false);
		}
	}

	status->busy_time = clk_scaling->total_busy_time_us;
	status->total_time = ktime_to_us(ktime_sub(ktime_get(),
		clk_scaling->measure_interval_start));
	clk_scaling->total_busy_time_us = 0;
	status->current_frequency = clk_scaling->curr_freq;
	clk_scaling->measure_interval_start = ktime_get();

	pr_debug("%s: status: load = %lu%% - total_time=%lu busy_time = %lu, clk=%lu\n",
		mmc_hostname(mhost),
		(status->busy_time*100)/status->total_time,
		status->total_time, status->busy_time,
		status->current_frequency);

	spin_unlock_irqrestore(&host->clk_scaling.lock, flags);

	return 0;
}

static bool sdhci_msm_mmc_is_valid_state_for_clk_scaling(struct sdhci_msm_host *host)
{
	struct mmc_host *mhost = host->mmc;
	struct mmc_card *card = mhost->card;
	u32 status;

	/*
	 * If the current partition type is RPMB, clock switching may not
	 * work properly as sending tuning command (CMD21) is illegal in
	 * this mode.
	 * For RPMB transaction cmdq would be disabled.
	 */
	if (!card || (mmc_card_mmc(card) && !card->ext_csd.cmdq_en))
		return false;

	if (mmc_send_status(card, &status)) {
		pr_err("%s: Get card status fail\n", mmc_hostname(card->host));
		return false;
	}

	return R1_CURRENT_STATE(status) == R1_STATE_TRAN;
}

static int sdhci_msm_notify_load(struct sdhci_msm_host *msm_host, enum sdhci_msm_mmc_load state)
{
	int ret = 0;
	u32 clk_rate = 0;

	if (!IS_ERR(msm_host->bulk_clks[2].clk)) {
		clk_rate = (state == MMC_LOAD_LOW) ?
			msm_host->ice_clk_min :
			msm_host->ice_clk_max;
		if (msm_host->ice_clk_rate == clk_rate)
			return 0;
		pr_debug("%s: changing ICE clk rate to %u\n",
			mmc_hostname(msm_host->mmc), clk_rate);
		ret = clk_set_rate(msm_host->bulk_clks[2].clk, clk_rate);
		if (ret) {
			pr_err("%s: ICE_CLK rate set failed (%d) for %u\n",
				mmc_hostname(msm_host->mmc), ret, clk_rate);
			return ret;
		}
		msm_host->ice_clk_rate = clk_rate;
	}
	return 0;
}


int sdhci_msm_mmc_clk_update_freq(struct sdhci_msm_host *host,
		unsigned long freq, enum sdhci_msm_mmc_load state)
{
	struct mmc_host *mhost = host->mmc;
	int err = 0;

	if (!host) {
		pr_err("bad host parameter\n");
		WARN_ON(1);
		return -EINVAL;
	}

	/* make sure the card supports the frequency we want */
	if (unlikely(freq > host->clk_scaling_highest)) {
		freq = host->clk_scaling_highest;
		pr_warn("%s: %s: High freq was overridden to %lu\n",
				mmc_hostname(mhost), __func__,
				host->clk_scaling_highest);
	}

	if (unlikely(freq < host->clk_scaling_lowest)) {
		freq = host->clk_scaling_lowest;
		pr_warn("%s: %s: Low freq was overridden to %lu\n",
			mmc_hostname(mhost), __func__,
			host->clk_scaling_lowest);
	}

	if (freq == host->clk_scaling.curr_freq)
		goto out;

	if (mhost->cqe_on) {
		err = mhost->cqe_ops->cqe_wait_for_idle(mhost);
		if (err) {
			pr_err("%s: %s: CQE went in recovery path\n",
				mmc_hostname(mhost), __func__);
			goto out;
		}
		mhost->cqe_ops->cqe_off(mhost);
	}

	err = sdhci_msm_notify_load(host, state);
	if (err) {
		pr_err("%s: %s: fail on notify_load\n",
			mmc_hostname(mhost), __func__);
		goto out;
	}

	if (!sdhci_msm_mmc_is_valid_state_for_clk_scaling(host)) {
		pr_debug("%s: invalid state for clock scaling - skipping\n",
			mmc_hostname(mhost));
		goto out;
	}

	if (mmc_card_mmc(mhost->card))
		err = sdhci_msm_mmc_change_bus_speed(host, &freq);
	if (!err)
		host->clk_scaling.curr_freq = freq;
	else
		pr_err("%s: %s: failed (%d) at freq=%lu\n",
			mmc_hostname(mhost), __func__, err, freq);

	/*
	 * CQE would be enabled as part of CQE issueing path
	 * So no need to unhalt it explicitly
	 */

out:
	return err;
}
EXPORT_SYMBOL_GPL(sdhci_msm_mmc_clk_update_freq);

static int sdhci_msm_mmc_devfreq_set_target(struct device *dev,
				unsigned long *freq, u32 devfreq_flags)
{
	struct mmc_host *mhost = container_of(dev, struct mmc_host, class_dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	struct sdhci_msm_mmc_devfeq_clk_scaling *clk_scaling;
	int err = 0;
	int abort;
	unsigned long pflags = current->flags;
	unsigned long flags;

	/* Ensure scaling would happen even in memory pressure conditions */
	current->flags |= PF_MEMALLOC;

	if (!(host && freq)) {
		pr_err("%s: unexpected host/freq parameter\n", __func__);
		err = -EINVAL;
		goto out;
	}

	clk_scaling = &host->clk_scaling;

	if (!clk_scaling->enable)
		goto out;

	pr_debug("%s: target freq = %lu (%s)\n", mmc_hostname(mhost),
		*freq, current->comm);

	spin_lock_irqsave(&clk_scaling->lock, flags);
	if (clk_scaling->target_freq == *freq) {
		spin_unlock_irqrestore(&clk_scaling->lock, flags);
		goto out;
	}

	clk_scaling->need_freq_change = true;
	clk_scaling->target_freq = *freq;
	clk_scaling->state = *freq < clk_scaling->curr_freq ?
		MMC_LOAD_LOW : MMC_LOAD_HIGH;
	spin_unlock_irqrestore(&clk_scaling->lock, flags);

	if (!clk_scaling->is_suspended && mhost->ios.clock)
		abort = __mmc_claim_host(mhost, NULL,
				&clk_scaling->devfreq_abort);
	else
		goto out;

	if (abort)
		goto out;

	/*
	 * In case we were able to claim host there is no need to
	 * defer the frequency change. It will be done now
	 */
	clk_scaling->need_freq_change = false;

	err = sdhci_msm_mmc_clk_update_freq(host, *freq, clk_scaling->state);
	if (err && err != -EAGAIN)
		pr_err("%s: clock scale to %lu failed with error %d\n",
			mmc_hostname(mhost), *freq, err);
	else
		pr_debug("%s: clock change to %lu finished successfully (%s)\n",
			mmc_hostname(mhost), *freq, current->comm);

	mmc_release_host(mhost);
out:
	current_restore_flags(pflags, PF_MEMALLOC);
	return err;
}

/**
 * mmc_deferred_scaling() - scale clocks from data path (mmc thread context)
 * @host: pointer to mmc host structure
 *
 * This function does clock scaling in case "need_freq_change" flag was set
 * by the clock scaling logic.
 */
void sdhci_msm_mmc_deferred_scaling(struct sdhci_msm_host *host)
{
	unsigned long target_freq;
	int err;
	struct sdhci_msm_mmc_devfeq_clk_scaling clk_scaling;
	unsigned long flags;
	struct mmc_host *mhost = host->mmc;

	if (!host->clk_scaling.enable)
		return;

	spin_lock_irqsave(&host->clk_scaling.lock, flags);

	if (!host->clk_scaling.need_freq_change) {
		spin_unlock_irqrestore(&host->clk_scaling.lock, flags);
		return;
	}

	atomic_inc(&host->clk_scaling.devfreq_abort);
	target_freq = host->clk_scaling.target_freq;
	/*
	 * Store the clock scaling state while the lock is acquired so that
	 * if devfreq context modifies clk_scaling, it will get reflected only
	 * in the next deferred scaling check.
	 */
	clk_scaling = host->clk_scaling;
	host->clk_scaling.need_freq_change = false;
	spin_unlock_irqrestore(&host->clk_scaling.lock, flags);

	pr_debug("%s: doing deferred frequency change (%lu) (%s)\n",
				mmc_hostname(mhost),
				target_freq, current->comm);

	err = sdhci_msm_mmc_clk_update_freq(host, target_freq,
		clk_scaling.state);
	if (err && err != -EAGAIN)
		pr_err("%s: failed on deferred scale clocks (%d)\n",
			mmc_hostname(mhost), err);
	else
		pr_debug("%s: clocks were successfully scaled to %lu (%s)\n",
			mmc_hostname(mhost),
			target_freq, current->comm);
	atomic_dec(&host->clk_scaling.devfreq_abort);
}
EXPORT_SYMBOL_GPL(sdhci_msm_mmc_deferred_scaling);

static int sdhci_msm_mmc_devfreq_create_freq_table(struct sdhci_msm_host *host)
{
	int i;
	struct sdhci_msm_mmc_devfeq_clk_scaling *clk_scaling = &host->clk_scaling;
	struct mmc_host *mhost = host->mmc;

	pr_debug("%s: supported: lowest=%lu, highest=%lu\n",
		mmc_hostname(mhost),
		host->clk_scaling_lowest,
		host->clk_scaling_highest);

	/*
	 * Create the frequency table and initialize it with default values.
	 * Initialize it with platform specific frequencies if the frequency
	 * table supplied by platform driver is present, otherwise initialize
	 * it with min and max frequencies supported by the card.
	 */
	if (!clk_scaling->freq_table) {
		if (clk_scaling->pltfm_freq_table_sz)
			clk_scaling->freq_table_sz =
				clk_scaling->pltfm_freq_table_sz;
		else
			clk_scaling->freq_table_sz = 2;

		clk_scaling->freq_table = kcalloc(
			clk_scaling->freq_table_sz,
			sizeof(*(clk_scaling->freq_table)), GFP_KERNEL);
		if (!clk_scaling->freq_table)
			return -ENOMEM;

		if (clk_scaling->pltfm_freq_table) {
			memcpy(clk_scaling->freq_table,
				clk_scaling->pltfm_freq_table,
				(clk_scaling->pltfm_freq_table_sz *
				sizeof(*(clk_scaling->pltfm_freq_table))));
		} else {
			pr_debug("%s: no frequency table defined -  setting default\n",
				mmc_hostname(mhost));
			clk_scaling->freq_table[0] =
				host->clk_scaling_lowest;
			clk_scaling->freq_table[1] =
				host->clk_scaling_highest;
			goto out;
		}
	}

	if (host->clk_scaling_lowest >
		clk_scaling->freq_table[0])
		pr_debug("%s: frequency table undershot possible freq\n",
			mmc_hostname(mhost));

	for (i = 0; i < clk_scaling->freq_table_sz; i++) {
		if (clk_scaling->freq_table[i] <=
			host->clk_scaling_highest)
			continue;
		clk_scaling->freq_table[i] =
			host->clk_scaling_highest;
		clk_scaling->freq_table_sz = i + 1;
		pr_debug("%s: frequency table overshot possible freq (%d)\n",
				mmc_hostname(mhost), clk_scaling->freq_table[i]);
		break;
	}

	if (mmc_card_sd(mhost->card) && (clk_scaling->freq_table_sz < 2)) {
		clk_scaling->freq_table[clk_scaling->freq_table_sz] =
			host->clk_scaling_highest;
		clk_scaling->freq_table_sz++;
	}
out:
	/**
	 * devfreq requires unsigned long type freq_table while the
	 * freq_table in clk_scaling is un32. Here allocates an individual
	 * memory space for it and release it when exit clock scaling.
	 */
	clk_scaling->devfreq_profile.freq_table =  kcalloc(
			clk_scaling->freq_table_sz,
			sizeof(*(clk_scaling->devfreq_profile.freq_table)),
			GFP_KERNEL);
	if (!clk_scaling->devfreq_profile.freq_table) {
		kfree(clk_scaling->freq_table);
		return -ENOMEM;
	}
	clk_scaling->devfreq_profile.max_state = clk_scaling->freq_table_sz;

	for (i = 0; i < clk_scaling->freq_table_sz; i++) {
		clk_scaling->devfreq_profile.freq_table[i] =
			clk_scaling->freq_table[i];
		pr_debug("%s: freq[%d] = %u\n",
			mmc_hostname(mhost), i, clk_scaling->freq_table[i]);
	}

	return 0;
}

static ssize_t enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *mhost = cls_dev_to_mmc_host(dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	if (!host)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%d\n", sdhci_msm_mmc_can_scale_clk(host));
}

static ssize_t enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mmc_host *mhost = cls_dev_to_mmc_host(dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);
	unsigned long value;

	if (!host || !mhost->card || kstrtoul(buf, 0, &value))
		return -EINVAL;

	mmc_get_card(mhost->card, NULL);

	if (!value) {
		/* Suspend the clock scaling and mask host capability */
		if (host->clk_scaling.enable)
			sdhci_msm_mmc_suspend_clk_scaling(mhost);
		host->clk_scaling.enable = false;
		mhost->caps2 &= ~MMC_CAP2_CLK_SCALE;
		host->scale_caps = mhost->caps2;
		host->clk_scaling.state = MMC_LOAD_HIGH;
		/* Set to max. frequency when disabling */
		sdhci_msm_mmc_clk_update_freq(host, host->clk_scaling_highest,
					host->clk_scaling.state);
	} else if (value) {
		/* Unmask host capability and resume scaling */
		mhost->caps2 |= MMC_CAP2_CLK_SCALE;
		host->scale_caps = mhost->caps2;
		if (!host->clk_scaling.enable) {
			host->clk_scaling.enable = true;
			sdhci_msm_mmc_resume_clk_scaling(mhost);
		}
	}

	mmc_put_card(mhost->card, NULL);

	return count;
}

static ssize_t up_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *mhost = cls_dev_to_mmc_host(dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	if (!host)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%d\n", host->clk_scaling.upthreshold);
}

#define MAX_PERCENTAGE	100
static ssize_t up_threshold_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mmc_host *mhost = cls_dev_to_mmc_host(dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);
	unsigned long value;

	if (!host || kstrtoul(buf, 0, &value) || (value > MAX_PERCENTAGE))
		return -EINVAL;

	host->clk_scaling.upthreshold = value;

	pr_debug("%s: clkscale_up_thresh set to %lu\n",
			mmc_hostname(mhost), value);
	return count;
}

static ssize_t down_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *mhost = cls_dev_to_mmc_host(dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	if (!host)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			host->clk_scaling.downthreshold);
}

static ssize_t down_threshold_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mmc_host *mhost = cls_dev_to_mmc_host(dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);
	unsigned long value;

	if (!host || kstrtoul(buf, 0, &value) || (value > MAX_PERCENTAGE))
		return -EINVAL;

	host->clk_scaling.downthreshold = value;

	pr_debug("%s: clkscale_down_thresh set to %lu\n",
			mmc_hostname(mhost), value);
	return count;
}

static ssize_t polling_interval_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *mhost = cls_dev_to_mmc_host(dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);

	if (!host)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%lu milliseconds\n",
			host->clk_scaling.polling_delay_ms);
}

static ssize_t polling_interval_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mmc_host *mhost = cls_dev_to_mmc_host(dev);
	struct sdhci_host *shost = mmc_priv(mhost);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(shost);
	struct sdhci_msm_host *host = sdhci_pltfm_priv(pltfm_host);
	unsigned long value;

	if (!host || kstrtoul(buf, 0, &value))
		return -EINVAL;

	host->clk_scaling.polling_delay_ms = value;

	pr_debug("%s: clkscale_polling_delay_ms set to %lu\n",
			mmc_hostname(mhost), value);
	return count;
}

DEVICE_ATTR_RW(enable);
DEVICE_ATTR_RW(polling_interval);
DEVICE_ATTR_RW(up_threshold);
DEVICE_ATTR_RW(down_threshold);

static struct attribute *clk_scaling_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_up_threshold.attr,
	&dev_attr_down_threshold.attr,
	&dev_attr_polling_interval.attr,
	NULL,
};

static struct attribute_group clk_scaling_attr_grp = {
	.name = "clk_scaling",
	.attrs = clk_scaling_attrs,
};


/**
 * mmc_init_devfreq_clk_scaling() - Initialize clock scaling
 * @host: pointer to mmc host structure
 *
 * Initialize clock scaling for supported hosts. It is assumed that the caller
 * ensure clock is running at maximum possible frequency before calling this
 * function. Shall use struct devfreq_simple_ondemand_data to configure
 * governor.
 */
int _sdhci_msm_mmc_init_clk_scaling(struct sdhci_msm_host *host)
{
	struct mmc_host *mhost = host->mmc;
	int err;
	struct devfreq *devfreq;

	if (!mhost || !mhost->card) {
		pr_err("%s: unexpected host/card parameters\n",
			__func__);
		return -EINVAL;
	}

	if (!sdhci_msm_mmc_can_scale_clk(host)) {
		pr_debug("%s: clock scaling is not supported\n",
			mmc_hostname(mhost));
		return 0;
	}

	pr_debug("registering %s dev (%pK) to devfreq\n",
		mmc_hostname(mhost),
		mmc_classdev(mhost));

	if (host->clk_scaling.devfreq) {
		pr_err("%s: dev is already registered for dev %pK\n",
			mmc_hostname(mhost),
			mmc_dev(mhost));
		return -EPERM;
	}
	spin_lock_init(&host->clk_scaling.lock);
	atomic_set(&host->clk_scaling.devfreq_abort, 0);
	host->clk_scaling.curr_freq = mhost->ios.clock;
	host->clk_scaling.need_freq_change = false;
	host->clk_scaling.is_busy_started = false;

	host->clk_scaling.devfreq_profile.polling_ms =
		host->clk_scaling.polling_delay_ms;
	host->clk_scaling.devfreq_profile.get_dev_status =
		sdhci_msm_mmc_devfreq_get_dev_status;
	host->clk_scaling.devfreq_profile.target = sdhci_msm_mmc_devfreq_set_target;
	host->clk_scaling.devfreq_profile.initial_freq = mhost->ios.clock;
	host->clk_scaling.devfreq_profile.timer = DEVFREQ_TIMER_DELAYED;

	host->clk_scaling.ondemand_gov_data.upthreshold =
		host->clk_scaling.upthreshold;
	host->clk_scaling.ondemand_gov_data.downdifferential =
		host->clk_scaling.upthreshold - host->clk_scaling.downthreshold;

	err = sdhci_msm_mmc_devfreq_create_freq_table(host);
	if (err) {
		pr_err("%s: fail to create devfreq frequency table\n",
			mmc_hostname(mhost));
		return err;
	}

	dev_pm_opp_add(mmc_classdev(mhost),
		host->clk_scaling.devfreq_profile.freq_table[0], 0);
	dev_pm_opp_add(mmc_classdev(mhost),
		host->clk_scaling.devfreq_profile.freq_table[1], 0);

	pr_debug("%s: adding devfreq with: upthreshold=%u downthreshold=%u polling=%u\n",
		mmc_hostname(mhost),
		host->clk_scaling.ondemand_gov_data.upthreshold,
		host->clk_scaling.ondemand_gov_data.downdifferential,
		host->clk_scaling.devfreq_profile.polling_ms);

	devfreq = devfreq_add_device(
		mmc_classdev(mhost),
		&host->clk_scaling.devfreq_profile,
		"simple_ondemand",
		&host->clk_scaling.ondemand_gov_data);

	if (IS_ERR(devfreq)) {
		pr_err("%s: unable to register with devfreq\n",
			mmc_hostname(mhost));
		dev_pm_opp_remove(mmc_classdev(mhost),
			host->clk_scaling.devfreq_profile.freq_table[0]);
		dev_pm_opp_remove(mmc_classdev(mhost),
			host->clk_scaling.devfreq_profile.freq_table[1]);
		return PTR_ERR(devfreq);
	}

	host->clk_scaling.devfreq = devfreq;
	pr_debug("%s: clk scaling is enabled for device %s (%pK) with devfreq %pK (clock = %uHz)\n",
		mmc_hostname(mhost),
		dev_name(mmc_classdev(mhost)),
		mmc_classdev(mhost),
		host->clk_scaling.devfreq,
		mhost->ios.clock);

	host->clk_scaling.enable = true;

	err = sysfs_create_group(&mhost->class_dev.kobj, &clk_scaling_attr_grp);
	if (err)
		pr_err("%s: failed to create clk scale sysfs group with err %d\n",
				__func__, err);

	return err;
}
EXPORT_SYMBOL_GPL(_sdhci_msm_mmc_init_clk_scaling);

/**
 * mmc_suspend_clk_scaling() - suspend clock scaling
 * @host: pointer to mmc host structure
 *
 * This API will suspend devfreq feature for the specific host.
 * The statistics collected by mmc will be cleared.
 * This function is intended to be called by the pm callbacks
 * (e.g. runtime_suspend, suspend) of the mmc device
 */
int _sdhci_msm_mmc_suspend_clk_scaling(struct sdhci_msm_host *host)
{
	struct mmc_host *mhost = host->mmc;
	int err;

	if (!host) {
		WARN(1, "bad host parameter\n");
		return -EINVAL;
	}

	if (!sdhci_msm_mmc_can_scale_clk(host) || !host->clk_scaling.enable ||
			host->clk_scaling.is_suspended)
		return 0;

	if (!host->clk_scaling.devfreq) {
		pr_err("%s: %s: no devfreq is assosiated with this device\n",
			mmc_hostname(mhost), __func__);
		return -EPERM;
	}

	atomic_inc(&host->clk_scaling.devfreq_abort);
	wake_up(&mhost->wq);
	err = devfreq_suspend_device(host->clk_scaling.devfreq);
	if (err) {
		pr_err("%s: %s: failed to suspend devfreq\n",
			mmc_hostname(mhost), __func__);
		return err;
	}
	host->clk_scaling.is_suspended = true;

	host->clk_scaling.total_busy_time_us = 0;

	pr_debug("%s: devfreq was removed\n", mmc_hostname(mhost));

	return 0;
}
EXPORT_SYMBOL_GPL(_sdhci_msm_mmc_suspend_clk_scaling);

/**
 * mmc_resume_clk_scaling() - resume clock scaling
 * @host: pointer to mmc host structure
 *
 * This API will resume devfreq feature for the specific host.
 * This API is intended to be called by the pm callbacks
 * (e.g. runtime_suspend, suspend) of the mmc device
 */
int _sdhci_msm_mmc_resume_clk_scaling(struct sdhci_msm_host *host)
{
	struct mmc_host *mhost = host->mmc;
	int err = 0;
	u32 max_clk_idx = 0;
	u32 devfreq_max_clk = 0;
	u32 devfreq_min_clk = 0;

	if (!host) {
		WARN(1, "bad host parameter\n");
		return -EINVAL;
	}

	if (!sdhci_msm_mmc_can_scale_clk(host))
		return 0;

	/*
	 * If clock scaling is already exited when resume is called, like
	 * during mmc shutdown, it is not an error and should not fail the
	 * API calling this.
	 */
	if (!host->clk_scaling.devfreq) {
		pr_warn("%s: %s: no devfreq is assosiated with this device\n",
			mmc_hostname(mhost), __func__);
		return 0;
	}

	atomic_set(&host->clk_scaling.devfreq_abort, 0);

	max_clk_idx = host->clk_scaling.freq_table_sz - 1;
	devfreq_max_clk = host->clk_scaling.freq_table[max_clk_idx];
	devfreq_min_clk = host->clk_scaling.freq_table[0];

	host->clk_scaling.curr_freq = devfreq_max_clk;
	if (mhost->ios.clock < host->clk_scaling.freq_table[max_clk_idx])
		host->clk_scaling.curr_freq = devfreq_min_clk;
	host->clk_scaling.target_freq = host->clk_scaling.curr_freq;

	err = devfreq_resume_device(host->clk_scaling.devfreq);
	if (err) {
		pr_err("%s: %s: failed to resume devfreq (%d)\n",
			mmc_hostname(mhost), __func__, err);
	} else {
		host->clk_scaling.is_suspended = false;
		pr_debug("%s: devfreq resumed\n", mmc_hostname(mhost));
	}

	return err;
}
EXPORT_SYMBOL_GPL(_sdhci_msm_mmc_resume_clk_scaling);

/**
 * mmc_exit_devfreq_clk_scaling() - Disable clock scaling
 * @host: pointer to mmc host structure
 *
 * Disable clock scaling permanently.
 */
int _sdhci_msm_mmc_exit_clk_scaling(struct sdhci_msm_host *host)
{
	struct mmc_host *mhost = host->mmc;
	int err;

	if (!host) {
		pr_err("%s: bad host parameter\n", __func__);
		WARN_ON(1);
		return -EINVAL;
	}

	if (!sdhci_msm_mmc_can_scale_clk(host))
		return 0;

	if (!host->clk_scaling.devfreq) {
		pr_err("%s: %s: no devfreq is assosiated with this device\n",
			mmc_hostname(mhost), __func__);
		return -EPERM;
	}

	err = _sdhci_msm_mmc_suspend_clk_scaling(host);
	if (err) {
		pr_err("%s: %s: fail to suspend clock scaling (%d)\n",
			mmc_hostname(mhost), __func__,  err);
		return err;
	}

	err = devfreq_remove_device(host->clk_scaling.devfreq);
	if (err) {
		pr_err("%s: remove devfreq failed (%d)\n",
			mmc_hostname(mhost), err);
		return err;
	}

	dev_pm_opp_remove(mmc_classdev(mhost),
		host->clk_scaling.devfreq_profile.freq_table[0]);
	dev_pm_opp_remove(mmc_classdev(mhost),
		host->clk_scaling.devfreq_profile.freq_table[1]);

	kfree(host->clk_scaling.devfreq_profile.freq_table);

	host->clk_scaling.devfreq = NULL;
	atomic_set(&host->clk_scaling.devfreq_abort, 1);

	kfree(host->clk_scaling.freq_table);
	host->clk_scaling.freq_table = NULL;

	pr_debug("%s: devfreq was removed\n", mmc_hostname(mhost));

	return 0;
}
EXPORT_SYMBOL_GPL(_sdhci_msm_mmc_exit_clk_scaling);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. SDHCI Controller Support");
MODULE_LICENSE("GPL");
