// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2021 Hisilicon Limited.

#include <linux/skbuff.h>
#include "hclge_main.h"
#include "hnae3.h"

static int hclge_ptp_get_cycle(struct hclge_dev *hdev)
{
	struct hclge_ptp *ptp = hdev->ptp;

	ptp->cycle.quo = readl(hdev->ptp->io_base + HCLGE_PTP_CYCLE_QUO_REG) &
			 HCLGE_PTP_CYCLE_QUO_MASK;
	ptp->cycle.numer = readl(hdev->ptp->io_base + HCLGE_PTP_CYCLE_NUM_REG);
	ptp->cycle.den = readl(hdev->ptp->io_base + HCLGE_PTP_CYCLE_DEN_REG);

	if (ptp->cycle.den == 0) {
		dev_err(&hdev->pdev->dev, "invalid ptp cycle denominator!\n");
		return -EINVAL;
	}

	return 0;
}

static int hclge_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct hclge_dev *hdev = hclge_ptp_get_hdev(ptp);
	struct hclge_ptp_cycle *cycle = &hdev->ptp->cycle;
	u64 adj_val, adj_base, diff;
	unsigned long flags;
	bool is_neg = false;
	u32 quo, numerator;

	if (ppb < 0) {
		ppb = -ppb;
		is_neg = true;
	}

	adj_base = (u64)cycle->quo * (u64)cycle->den + (u64)cycle->numer;
	adj_val = adj_base * ppb;
	diff = div_u64(adj_val, 1000000000ULL);

	if (is_neg)
		adj_val = adj_base - diff;
	else
		adj_val = adj_base + diff;

	/* This clock cycle is defined by three part: quotient, numerator
	 * and denominator. For example, 2.5ns, the quotient is 2,
	 * denominator is fixed to ptp->cycle.den, and numerator
	 * is 0.5 * ptp->cycle.den.
	 */
	quo = div_u64_rem(adj_val, cycle->den, &numerator);

	spin_lock_irqsave(&hdev->ptp->lock, flags);
	writel(quo & HCLGE_PTP_CYCLE_QUO_MASK,
	       hdev->ptp->io_base + HCLGE_PTP_CYCLE_QUO_REG);
	writel(numerator, hdev->ptp->io_base + HCLGE_PTP_CYCLE_NUM_REG);
	writel(cycle->den, hdev->ptp->io_base + HCLGE_PTP_CYCLE_DEN_REG);
	writel(HCLGE_PTP_CYCLE_ADJ_EN,
	       hdev->ptp->io_base + HCLGE_PTP_CYCLE_CFG_REG);
	spin_unlock_irqrestore(&hdev->ptp->lock, flags);

	return 0;
}

bool hclge_ptp_set_tx_info(struct hnae3_handle *handle, struct sk_buff *skb)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_ptp *ptp = hdev->ptp;

	if (!test_bit(HCLGE_PTP_FLAG_TX_EN, &ptp->flags) ||
	    test_and_set_bit(HCLGE_STATE_PTP_TX_HANDLING, &hdev->state)) {
		ptp->tx_skipped++;
		return false;
	}

	ptp->tx_start = jiffies;
	ptp->tx_skb = skb_get(skb);
	ptp->tx_cnt++;

	return true;
}

void hclge_ptp_clean_tx_hwts(struct hclge_dev *hdev)
{
	struct sk_buff *skb = hdev->ptp->tx_skb;
	struct skb_shared_hwtstamps hwts;
	u32 hi, lo;
	u64 ns;

	ns = readl(hdev->ptp->io_base + HCLGE_PTP_TX_TS_NSEC_REG) &
	     HCLGE_PTP_TX_TS_NSEC_MASK;
	lo = readl(hdev->ptp->io_base + HCLGE_PTP_TX_TS_SEC_L_REG);
	hi = readl(hdev->ptp->io_base + HCLGE_PTP_TX_TS_SEC_H_REG) &
	     HCLGE_PTP_TX_TS_SEC_H_MASK;
	hdev->ptp->last_tx_seqid = readl(hdev->ptp->io_base +
		HCLGE_PTP_TX_TS_SEQID_REG);

	if (skb) {
		hdev->ptp->tx_skb = NULL;
		hdev->ptp->tx_cleaned++;

		ns += (((u64)hi) << 32 | lo) * NSEC_PER_SEC;
		hwts.hwtstamp = ns_to_ktime(ns);
		skb_tstamp_tx(skb, &hwts);
		dev_kfree_skb_any(skb);
	}

	clear_bit(HCLGE_STATE_PTP_TX_HANDLING, &hdev->state);
}

void hclge_ptp_get_rx_hwts(struct hnae3_handle *handle, struct sk_buff *skb,
			   u32 nsec, u32 sec)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	unsigned long flags;
	u64 ns = nsec;
	u32 sec_h;

	if (!test_bit(HCLGE_PTP_FLAG_RX_EN, &hdev->ptp->flags))
		return;

	/* Since the BD does not have enough space for the higher 16 bits of
	 * second, and this part will not change frequently, so read it
	 * from register.
	 */
	spin_lock_irqsave(&hdev->ptp->lock, flags);
	sec_h = readl(hdev->ptp->io_base + HCLGE_PTP_CUR_TIME_SEC_H_REG);
	spin_unlock_irqrestore(&hdev->ptp->lock, flags);

	ns += (((u64)sec_h) << HCLGE_PTP_SEC_H_OFFSET | sec) * NSEC_PER_SEC;
	skb_hwtstamps(skb)->hwtstamp = ns_to_ktime(ns);
	hdev->ptp->last_rx = jiffies;
	hdev->ptp->rx_cnt++;
}

static int hclge_ptp_gettimex(struct ptp_clock_info *ptp, struct timespec64 *ts,
			      struct ptp_system_timestamp *sts)
{
	struct hclge_dev *hdev = hclge_ptp_get_hdev(ptp);
	unsigned long flags;
	u32 hi, lo;
	u64 ns;

	spin_lock_irqsave(&hdev->ptp->lock, flags);
	ns = readl(hdev->ptp->io_base + HCLGE_PTP_CUR_TIME_NSEC_REG);
	hi = readl(hdev->ptp->io_base + HCLGE_PTP_CUR_TIME_SEC_H_REG);
	lo = readl(hdev->ptp->io_base + HCLGE_PTP_CUR_TIME_SEC_L_REG);
	spin_unlock_irqrestore(&hdev->ptp->lock, flags);

	ns += (((u64)hi) << HCLGE_PTP_SEC_H_OFFSET | lo) * NSEC_PER_SEC;
	*ts = ns_to_timespec64(ns);

	return 0;
}

static int hclge_ptp_settime(struct ptp_clock_info *ptp,
			     const struct timespec64 *ts)
{
	struct hclge_dev *hdev = hclge_ptp_get_hdev(ptp);
	unsigned long flags;

	spin_lock_irqsave(&hdev->ptp->lock, flags);
	writel(ts->tv_nsec, hdev->ptp->io_base + HCLGE_PTP_TIME_NSEC_REG);
	writel(ts->tv_sec >> HCLGE_PTP_SEC_H_OFFSET,
	       hdev->ptp->io_base + HCLGE_PTP_TIME_SEC_H_REG);
	writel(ts->tv_sec & HCLGE_PTP_SEC_L_MASK,
	       hdev->ptp->io_base + HCLGE_PTP_TIME_SEC_L_REG);
	/* synchronize the time of phc */
	writel(HCLGE_PTP_TIME_SYNC_EN,
	       hdev->ptp->io_base + HCLGE_PTP_TIME_SYNC_REG);
	spin_unlock_irqrestore(&hdev->ptp->lock, flags);

	return 0;
}

static int hclge_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct hclge_dev *hdev = hclge_ptp_get_hdev(ptp);
	unsigned long flags;
	bool is_neg = false;
	u32 adj_val = 0;

	if (delta < 0) {
		adj_val |= HCLGE_PTP_TIME_NSEC_NEG;
		delta = -delta;
		is_neg = true;
	}

	if (delta > HCLGE_PTP_TIME_NSEC_MASK) {
		struct timespec64 ts;
		s64 ns;

		hclge_ptp_gettimex(ptp, &ts, NULL);
		ns = timespec64_to_ns(&ts);
		ns = is_neg ? ns - delta : ns + delta;
		ts = ns_to_timespec64(ns);
		return hclge_ptp_settime(ptp, &ts);
	}

	adj_val |= delta & HCLGE_PTP_TIME_NSEC_MASK;

	spin_lock_irqsave(&hdev->ptp->lock, flags);
	writel(adj_val, hdev->ptp->io_base + HCLGE_PTP_TIME_NSEC_REG);
	writel(HCLGE_PTP_TIME_ADJ_EN,
	       hdev->ptp->io_base + HCLGE_PTP_TIME_ADJ_REG);
	spin_unlock_irqrestore(&hdev->ptp->lock, flags);

	return 0;
}

int hclge_ptp_get_cfg(struct hclge_dev *hdev, struct ifreq *ifr)
{
	if (!test_bit(HCLGE_STATE_PTP_EN, &hdev->state))
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, &hdev->ptp->ts_cfg,
		sizeof(struct hwtstamp_config)) ? -EFAULT : 0;
}

static int hclge_ptp_int_en(struct hclge_dev *hdev, bool en)
{
	struct hclge_ptp_int_cmd *req;
	struct hclge_desc desc;
	int ret;

	req = (struct hclge_ptp_int_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_PTP_INT_EN, false);
	req->int_en = en ? 1 : 0;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"failed to %s ptp interrupt, ret = %d\n",
			en ? "enable" : "disable", ret);

	return ret;
}

int hclge_ptp_cfg_qry(struct hclge_dev *hdev, u32 *cfg)
{
	struct hclge_ptp_cfg_cmd *req;
	struct hclge_desc desc;
	int ret;

	req = (struct hclge_ptp_cfg_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_PTP_MODE_CFG, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to query ptp config, ret = %d\n", ret);
		return ret;
	}

	*cfg = le32_to_cpu(req->cfg);

	return 0;
}

static int hclge_ptp_cfg(struct hclge_dev *hdev, u32 cfg)
{
	struct hclge_ptp_cfg_cmd *req;
	struct hclge_desc desc;
	int ret;

	req = (struct hclge_ptp_cfg_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_PTP_MODE_CFG, false);
	req->cfg = cpu_to_le32(cfg);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"failed to config ptp, ret = %d\n", ret);

	return ret;
}

static int hclge_ptp_set_tx_mode(struct hwtstamp_config *cfg,
				 unsigned long *flags, u32 *ptp_cfg)
{
	switch (cfg->tx_type) {
	case HWTSTAMP_TX_OFF:
		clear_bit(HCLGE_PTP_FLAG_TX_EN, flags);
		break;
	case HWTSTAMP_TX_ON:
		set_bit(HCLGE_PTP_FLAG_TX_EN, flags);
		*ptp_cfg |= HCLGE_PTP_TX_EN_B;
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

static int hclge_ptp_set_rx_mode(struct hwtstamp_config *cfg,
				 unsigned long *flags, u32 *ptp_cfg)
{
	int rx_filter = cfg->rx_filter;

	switch (cfg->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		clear_bit(HCLGE_PTP_FLAG_RX_EN, flags);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		set_bit(HCLGE_PTP_FLAG_RX_EN, flags);
		*ptp_cfg |= HCLGE_PTP_RX_EN_B;
		*ptp_cfg |= HCLGE_PTP_UDP_FULL_TYPE << HCLGE_PTP_UDP_EN_SHIFT;
		rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		set_bit(HCLGE_PTP_FLAG_RX_EN, flags);
		*ptp_cfg |= HCLGE_PTP_RX_EN_B;
		*ptp_cfg |= HCLGE_PTP_UDP_FULL_TYPE << HCLGE_PTP_UDP_EN_SHIFT;
		*ptp_cfg |= HCLGE_PTP_MSG1_V2_DEFAULT << HCLGE_PTP_MSG1_SHIFT;
		*ptp_cfg |= HCLGE_PTP_MSG0_V2_EVENT << HCLGE_PTP_MSG0_SHIFT;
		*ptp_cfg |= HCLGE_PTP_MSG_TYPE_V2 << HCLGE_PTP_MSG_TYPE_SHIFT;
		rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	case HWTSTAMP_FILTER_ALL:
	default:
		return -ERANGE;
	}

	cfg->rx_filter = rx_filter;

	return 0;
}

static int hclge_ptp_set_ts_mode(struct hclge_dev *hdev,
				 struct hwtstamp_config *cfg)
{
	unsigned long flags = hdev->ptp->flags;
	u32 ptp_cfg = 0;
	int ret;

	if (test_bit(HCLGE_PTP_FLAG_EN, &hdev->ptp->flags))
		ptp_cfg |= HCLGE_PTP_EN_B;

	ret = hclge_ptp_set_tx_mode(cfg, &flags, &ptp_cfg);
	if (ret)
		return ret;

	ret = hclge_ptp_set_rx_mode(cfg, &flags, &ptp_cfg);
	if (ret)
		return ret;

	ret = hclge_ptp_cfg(hdev, ptp_cfg);
	if (ret)
		return ret;

	hdev->ptp->flags = flags;
	hdev->ptp->ptp_cfg = ptp_cfg;

	return 0;
}

int hclge_ptp_set_cfg(struct hclge_dev *hdev, struct ifreq *ifr)
{
	struct hwtstamp_config cfg;
	int ret;

	if (!test_bit(HCLGE_STATE_PTP_EN, &hdev->state)) {
		dev_err(&hdev->pdev->dev, "phc is unsupported\n");
		return -EOPNOTSUPP;
	}

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	ret = hclge_ptp_set_ts_mode(hdev, &cfg);
	if (ret)
		return ret;

	hdev->ptp->ts_cfg = cfg;

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

int hclge_ptp_get_ts_info(struct hnae3_handle *handle,
			  struct ethtool_ts_info *info)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	if (!test_bit(HCLGE_STATE_PTP_EN, &hdev->state)) {
		dev_err(&hdev->pdev->dev, "phc is unsupported\n");
		return -EOPNOTSUPP;
	}

	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	if (hdev->ptp->clock)
		info->phc_index = ptp_clock_index(hdev->ptp->clock);
	else
		info->phc_index = -1;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ);

	info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
			    BIT(HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_SYNC) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_DELAY_REQ) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ);

	return 0;
}

static int hclge_ptp_create_clock(struct hclge_dev *hdev)
{
	struct hclge_ptp *ptp;

	ptp = devm_kzalloc(&hdev->pdev->dev, sizeof(*ptp), GFP_KERNEL);
	if (!ptp)
		return -ENOMEM;

	ptp->hdev = hdev;
	snprintf(ptp->info.name, sizeof(ptp->info.name), "%s",
		 HCLGE_DRIVER_NAME);
	ptp->info.owner = THIS_MODULE;
	ptp->info.max_adj = HCLGE_PTP_CYCLE_ADJ_MAX;
	ptp->info.n_ext_ts = 0;
	ptp->info.pps = 0;
	ptp->info.adjfreq = hclge_ptp_adjfreq;
	ptp->info.adjtime = hclge_ptp_adjtime;
	ptp->info.gettimex64 = hclge_ptp_gettimex;
	ptp->info.settime64 = hclge_ptp_settime;

	ptp->info.n_alarm = 0;
	ptp->clock = ptp_clock_register(&ptp->info, &hdev->pdev->dev);
	if (IS_ERR(ptp->clock)) {
		dev_err(&hdev->pdev->dev,
			"%d failed to register ptp clock, ret = %ld\n",
			ptp->info.n_alarm, PTR_ERR(ptp->clock));
		return -ENODEV;
	} else if (!ptp->clock) {
		dev_err(&hdev->pdev->dev, "failed to register ptp clock\n");
		return -ENODEV;
	}

	spin_lock_init(&ptp->lock);
	ptp->io_base = hdev->hw.io_base + HCLGE_PTP_REG_OFFSET;
	ptp->ts_cfg.rx_filter = HWTSTAMP_FILTER_NONE;
	ptp->ts_cfg.tx_type = HWTSTAMP_TX_OFF;
	hdev->ptp = ptp;

	return 0;
}

static void hclge_ptp_destroy_clock(struct hclge_dev *hdev)
{
	ptp_clock_unregister(hdev->ptp->clock);
	hdev->ptp->clock = NULL;
	devm_kfree(&hdev->pdev->dev, hdev->ptp);
	hdev->ptp = NULL;
}

int hclge_ptp_init(struct hclge_dev *hdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	struct timespec64 ts;
	int ret;

	if (!test_bit(HNAE3_DEV_SUPPORT_PTP_B, ae_dev->caps))
		return 0;

	if (!hdev->ptp) {
		ret = hclge_ptp_create_clock(hdev);
		if (ret)
			return ret;

		ret = hclge_ptp_get_cycle(hdev);
		if (ret)
			return ret;
	}

	ret = hclge_ptp_int_en(hdev, true);
	if (ret)
		goto out;

	set_bit(HCLGE_PTP_FLAG_EN, &hdev->ptp->flags);
	ret = hclge_ptp_adjfreq(&hdev->ptp->info, 0);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to init freq, ret = %d\n", ret);
		goto out;
	}

	ret = hclge_ptp_set_ts_mode(hdev, &hdev->ptp->ts_cfg);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to init ts mode, ret = %d\n", ret);
		goto out;
	}

	ktime_get_real_ts64(&ts);
	ret = hclge_ptp_settime(&hdev->ptp->info, &ts);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to init ts time, ret = %d\n", ret);
		goto out;
	}

	set_bit(HCLGE_STATE_PTP_EN, &hdev->state);
	dev_info(&hdev->pdev->dev, "phc initializes ok!\n");

	return 0;

out:
	hclge_ptp_destroy_clock(hdev);

	return ret;
}

void hclge_ptp_uninit(struct hclge_dev *hdev)
{
	struct hclge_ptp *ptp = hdev->ptp;

	if (!ptp)
		return;

	hclge_ptp_int_en(hdev, false);
	clear_bit(HCLGE_STATE_PTP_EN, &hdev->state);
	clear_bit(HCLGE_PTP_FLAG_EN, &ptp->flags);
	ptp->ts_cfg.rx_filter = HWTSTAMP_FILTER_NONE;
	ptp->ts_cfg.tx_type = HWTSTAMP_TX_OFF;

	if (hclge_ptp_set_ts_mode(hdev, &ptp->ts_cfg))
		dev_err(&hdev->pdev->dev, "failed to disable phc\n");

	if (ptp->tx_skb) {
		struct sk_buff *skb = ptp->tx_skb;

		ptp->tx_skb = NULL;
		dev_kfree_skb_any(skb);
	}

	hclge_ptp_destroy_clock(hdev);
}
