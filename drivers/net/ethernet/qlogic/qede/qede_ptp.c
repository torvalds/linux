/* QLogic qede NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "qede_ptp.h"

struct qede_ptp {
	const struct qed_eth_ptp_ops	*ops;
	struct ptp_clock_info		clock_info;
	struct cyclecounter		cc;
	struct timecounter		tc;
	struct ptp_clock		*clock;
	struct work_struct		work;
	struct qede_dev			*edev;
	struct sk_buff			*tx_skb;

	/* ptp spinlock is used for protecting the cycle/time counter fields
	 * and, also for serializing the qed PTP API invocations.
	 */
	spinlock_t			lock;
	bool				hw_ts_ioctl_called;
	u16				tx_type;
	u16				rx_filter;
};

/**
 * qede_ptp_adjfreq
 * @ptp: the ptp clock structure
 * @ppb: parts per billion adjustment from base
 *
 * Adjust the frequency of the ptp cycle counter by the
 * indicated ppb from the base frequency.
 */
static int qede_ptp_adjfreq(struct ptp_clock_info *info, s32 ppb)
{
	struct qede_ptp *ptp = container_of(info, struct qede_ptp, clock_info);
	struct qede_dev *edev = ptp->edev;
	int rc;

	__qede_lock(edev);
	if (edev->state == QEDE_STATE_OPEN) {
		spin_lock_bh(&ptp->lock);
		rc = ptp->ops->adjfreq(edev->cdev, ppb);
		spin_unlock_bh(&ptp->lock);
	} else {
		DP_ERR(edev, "PTP adjfreq called while interface is down\n");
		rc = -EFAULT;
	}
	__qede_unlock(edev);

	return rc;
}

static int qede_ptp_adjtime(struct ptp_clock_info *info, s64 delta)
{
	struct qede_dev *edev;
	struct qede_ptp *ptp;

	ptp = container_of(info, struct qede_ptp, clock_info);
	edev = ptp->edev;

	DP_VERBOSE(edev, QED_MSG_DEBUG, "PTP adjtime called, delta = %llx\n",
		   delta);

	spin_lock_bh(&ptp->lock);
	timecounter_adjtime(&ptp->tc, delta);
	spin_unlock_bh(&ptp->lock);

	return 0;
}

static int qede_ptp_gettime(struct ptp_clock_info *info, struct timespec64 *ts)
{
	struct qede_dev *edev;
	struct qede_ptp *ptp;
	u64 ns;

	ptp = container_of(info, struct qede_ptp, clock_info);
	edev = ptp->edev;

	spin_lock_bh(&ptp->lock);
	ns = timecounter_read(&ptp->tc);
	spin_unlock_bh(&ptp->lock);

	DP_VERBOSE(edev, QED_MSG_DEBUG, "PTP gettime called, ns = %llu\n", ns);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int qede_ptp_settime(struct ptp_clock_info *info,
			    const struct timespec64 *ts)
{
	struct qede_dev *edev;
	struct qede_ptp *ptp;
	u64 ns;

	ptp = container_of(info, struct qede_ptp, clock_info);
	edev = ptp->edev;

	ns = timespec64_to_ns(ts);

	DP_VERBOSE(edev, QED_MSG_DEBUG, "PTP settime called, ns = %llu\n", ns);

	/* Re-init the timecounter */
	spin_lock_bh(&ptp->lock);
	timecounter_init(&ptp->tc, &ptp->cc, ns);
	spin_unlock_bh(&ptp->lock);

	return 0;
}

/* Enable (or disable) ancillary features of the phc subsystem */
static int qede_ptp_ancillary_feature_enable(struct ptp_clock_info *info,
					     struct ptp_clock_request *rq,
					     int on)
{
	struct qede_dev *edev;
	struct qede_ptp *ptp;

	ptp = container_of(info, struct qede_ptp, clock_info);
	edev = ptp->edev;

	DP_ERR(edev, "PHC ancillary features are not supported\n");

	return -ENOTSUPP;
}

static void qede_ptp_task(struct work_struct *work)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct qede_dev *edev;
	struct qede_ptp *ptp;
	u64 timestamp, ns;
	int rc;

	ptp = container_of(work, struct qede_ptp, work);
	edev = ptp->edev;

	/* Read Tx timestamp registers */
	spin_lock_bh(&ptp->lock);
	rc = ptp->ops->read_tx_ts(edev->cdev, &timestamp);
	spin_unlock_bh(&ptp->lock);
	if (rc) {
		/* Reschedule to keep checking for a valid timestamp value */
		schedule_work(&ptp->work);
		return;
	}

	ns = timecounter_cyc2time(&ptp->tc, timestamp);
	memset(&shhwtstamps, 0, sizeof(shhwtstamps));
	shhwtstamps.hwtstamp = ns_to_ktime(ns);
	skb_tstamp_tx(ptp->tx_skb, &shhwtstamps);
	dev_kfree_skb_any(ptp->tx_skb);
	ptp->tx_skb = NULL;
	clear_bit_unlock(QEDE_FLAGS_PTP_TX_IN_PRORGESS, &edev->flags);

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "Tx timestamp, timestamp cycles = %llu, ns = %llu\n",
		   timestamp, ns);
}

/* Read the PHC. This API is invoked with ptp_lock held. */
static u64 qede_ptp_read_cc(const struct cyclecounter *cc)
{
	struct qede_dev *edev;
	struct qede_ptp *ptp;
	u64 phc_cycles;
	int rc;

	ptp = container_of(cc, struct qede_ptp, cc);
	edev = ptp->edev;
	rc = ptp->ops->read_cc(edev->cdev, &phc_cycles);
	if (rc)
		WARN_ONCE(1, "PHC read err %d\n", rc);

	DP_VERBOSE(edev, QED_MSG_DEBUG, "PHC read cycles = %llu\n", phc_cycles);

	return phc_cycles;
}

static int qede_ptp_cfg_filters(struct qede_dev *edev)
{
	enum qed_ptp_hwtstamp_tx_type tx_type = QED_PTP_HWTSTAMP_TX_ON;
	enum qed_ptp_filter_type rx_filter = QED_PTP_FILTER_NONE;
	struct qede_ptp *ptp = edev->ptp;

	if (!ptp)
		return -EIO;

	if (!ptp->hw_ts_ioctl_called) {
		DP_INFO(edev, "TS IOCTL not called\n");
		return 0;
	}

	switch (ptp->tx_type) {
	case HWTSTAMP_TX_ON:
		edev->flags |= QEDE_TX_TIMESTAMPING_EN;
		tx_type = QED_PTP_HWTSTAMP_TX_ON;
		break;

	case HWTSTAMP_TX_OFF:
		edev->flags &= ~QEDE_TX_TIMESTAMPING_EN;
		tx_type = QED_PTP_HWTSTAMP_TX_OFF;
		break;

	case HWTSTAMP_TX_ONESTEP_SYNC:
		DP_ERR(edev, "One-step timestamping is not supported\n");
		return -ERANGE;
	}

	spin_lock_bh(&ptp->lock);
	switch (ptp->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		rx_filter = QED_PTP_FILTER_NONE;
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_NTP_ALL:
		ptp->rx_filter = HWTSTAMP_FILTER_NONE;
		rx_filter = QED_PTP_FILTER_ALL;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		rx_filter = QED_PTP_FILTER_V1_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		/* Initialize PTP detection for UDP/IPv4 events */
		rx_filter = QED_PTP_FILTER_V1_L4_GEN;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		rx_filter = QED_PTP_FILTER_V2_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		/* Initialize PTP detection for UDP/IPv4 or UDP/IPv6 events */
		rx_filter = QED_PTP_FILTER_V2_L4_GEN;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		rx_filter = QED_PTP_FILTER_V2_L2_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		/* Initialize PTP detection L2 events */
		rx_filter = QED_PTP_FILTER_V2_L2_GEN;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		rx_filter = QED_PTP_FILTER_V2_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		/* Initialize PTP detection L2, UDP/IPv4 or UDP/IPv6 events */
		rx_filter = QED_PTP_FILTER_V2_GEN;
		break;
	}

	ptp->ops->cfg_filters(edev->cdev, rx_filter, tx_type);

	spin_unlock_bh(&ptp->lock);

	return 0;
}

int qede_ptp_hw_ts(struct qede_dev *edev, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	struct qede_ptp *ptp;
	int rc;

	ptp = edev->ptp;
	if (!ptp)
		return -EIO;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "HWTSTAMP IOCTL: Requested tx_type = %d, requested rx_filters = %d\n",
		   config.tx_type, config.rx_filter);

	if (config.flags) {
		DP_ERR(edev, "config.flags is reserved for future use\n");
		return -EINVAL;
	}

	ptp->hw_ts_ioctl_called = 1;
	ptp->tx_type = config.tx_type;
	ptp->rx_filter = config.rx_filter;

	rc = qede_ptp_cfg_filters(edev);
	if (rc)
		return rc;

	config.rx_filter = ptp->rx_filter;

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

int qede_ptp_get_ts_info(struct qede_dev *edev, struct ethtool_ts_info *info)
{
	struct qede_ptp *ptp = edev->ptp;

	if (!ptp)
		return -EIO;

	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	if (ptp->clock)
		info->phc_index = ptp_clock_index(ptp->clock);
	else
		info->phc_index = -1;

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
			   BIT(HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_SYNC) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_DELAY_REQ);

	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);

	return 0;
}

void qede_ptp_disable(struct qede_dev *edev)
{
	struct qede_ptp *ptp;

	ptp = edev->ptp;
	if (!ptp)
		return;

	if (ptp->clock) {
		ptp_clock_unregister(ptp->clock);
		ptp->clock = NULL;
	}

	/* Cancel PTP work queue. Should be done after the Tx queues are
	 * drained to prevent additional scheduling.
	 */
	cancel_work_sync(&ptp->work);
	if (ptp->tx_skb) {
		dev_kfree_skb_any(ptp->tx_skb);
		ptp->tx_skb = NULL;
	}

	/* Disable PTP in HW */
	spin_lock_bh(&ptp->lock);
	ptp->ops->disable(edev->cdev);
	spin_unlock_bh(&ptp->lock);

	kfree(ptp);
	edev->ptp = NULL;
}

static int qede_ptp_init(struct qede_dev *edev, bool init_tc)
{
	struct qede_ptp *ptp;
	int rc;

	ptp = edev->ptp;
	if (!ptp)
		return -EINVAL;

	spin_lock_init(&ptp->lock);

	/* Configure PTP in HW */
	rc = ptp->ops->enable(edev->cdev);
	if (rc) {
		DP_INFO(edev, "PTP HW enable failed\n");
		return rc;
	}

	/* Init work queue for Tx timestamping */
	INIT_WORK(&ptp->work, qede_ptp_task);

	/* Init cyclecounter and timecounter. This is done only in the first
	 * load. If done in every load, PTP application will fail when doing
	 * unload / load (e.g. MTU change) while it is running.
	 */
	if (init_tc) {
		memset(&ptp->cc, 0, sizeof(ptp->cc));
		ptp->cc.read = qede_ptp_read_cc;
		ptp->cc.mask = CYCLECOUNTER_MASK(64);
		ptp->cc.shift = 0;
		ptp->cc.mult = 1;

		timecounter_init(&ptp->tc, &ptp->cc,
				 ktime_to_ns(ktime_get_real()));
	}

	return rc;
}

int qede_ptp_enable(struct qede_dev *edev, bool init_tc)
{
	struct qede_ptp *ptp;
	int rc;

	ptp = kzalloc(sizeof(*ptp), GFP_KERNEL);
	if (!ptp) {
		DP_INFO(edev, "Failed to allocate struct for PTP\n");
		return -ENOMEM;
	}

	ptp->edev = edev;
	ptp->ops = edev->ops->ptp;
	if (!ptp->ops) {
		DP_INFO(edev, "PTP enable failed\n");
		rc = -EIO;
		goto err1;
	}

	edev->ptp = ptp;

	rc = qede_ptp_init(edev, init_tc);
	if (rc)
		goto err1;

	qede_ptp_cfg_filters(edev);

	/* Fill the ptp_clock_info struct and register PTP clock */
	ptp->clock_info.owner = THIS_MODULE;
	snprintf(ptp->clock_info.name, 16, "%s", edev->ndev->name);
	ptp->clock_info.max_adj = QED_MAX_PHC_DRIFT_PPB;
	ptp->clock_info.n_alarm = 0;
	ptp->clock_info.n_ext_ts = 0;
	ptp->clock_info.n_per_out = 0;
	ptp->clock_info.pps = 0;
	ptp->clock_info.adjfreq = qede_ptp_adjfreq;
	ptp->clock_info.adjtime = qede_ptp_adjtime;
	ptp->clock_info.gettime64 = qede_ptp_gettime;
	ptp->clock_info.settime64 = qede_ptp_settime;
	ptp->clock_info.enable = qede_ptp_ancillary_feature_enable;

	ptp->clock = ptp_clock_register(&ptp->clock_info, &edev->pdev->dev);
	if (IS_ERR(ptp->clock)) {
		rc = -EINVAL;
		DP_ERR(edev, "PTP clock registration failed\n");
		goto err2;
	}

	return 0;

err2:
	qede_ptp_disable(edev);
	ptp->clock = NULL;
err1:
	kfree(ptp);
	edev->ptp = NULL;

	return rc;
}

void qede_ptp_tx_ts(struct qede_dev *edev, struct sk_buff *skb)
{
	struct qede_ptp *ptp;

	ptp = edev->ptp;
	if (!ptp)
		return;

	if (test_and_set_bit_lock(QEDE_FLAGS_PTP_TX_IN_PRORGESS, &edev->flags))
		return;

	if (unlikely(!(edev->flags & QEDE_TX_TIMESTAMPING_EN))) {
		DP_NOTICE(edev,
			  "Tx timestamping was not enabled, this packet will not be timestamped\n");
	} else if (unlikely(ptp->tx_skb)) {
		DP_NOTICE(edev,
			  "The device supports only a single outstanding packet to timestamp, this packet will not be timestamped\n");
	} else {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		/* schedule check for Tx timestamp */
		ptp->tx_skb = skb_get(skb);
		schedule_work(&ptp->work);
	}
}

void qede_ptp_rx_ts(struct qede_dev *edev, struct sk_buff *skb)
{
	struct qede_ptp *ptp;
	u64 timestamp, ns;
	int rc;

	ptp = edev->ptp;
	if (!ptp)
		return;

	spin_lock_bh(&ptp->lock);
	rc = ptp->ops->read_rx_ts(edev->cdev, &timestamp);
	if (rc) {
		spin_unlock_bh(&ptp->lock);
		DP_INFO(edev, "Invalid Rx timestamp\n");
		return;
	}

	ns = timecounter_cyc2time(&ptp->tc, timestamp);
	spin_unlock_bh(&ptp->lock);
	skb_hwtstamps(skb)->hwtstamp = ns_to_ktime(ns);
	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "Rx timestamp, timestamp cycles = %llu, ns = %llu\n",
		   timestamp, ns);
}
