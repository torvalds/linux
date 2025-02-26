// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Microchip Technology

#include "microchip_rds_ptp.h"

static int mchp_rds_phy_read_mmd(struct mchp_rds_ptp_clock *clock,
				 u32 offset, enum mchp_rds_ptp_base base)
{
	struct phy_device *phydev = clock->phydev;
	u32 addr;

	addr = (offset + ((base == MCHP_RDS_PTP_PORT) ? BASE_PORT(clock) :
			  BASE_CLK(clock)));

	return phy_read_mmd(phydev, PTP_MMD(clock), addr);
}

static int mchp_rds_phy_write_mmd(struct mchp_rds_ptp_clock *clock,
				  u32 offset, enum mchp_rds_ptp_base base,
				  u16 val)
{
	struct phy_device *phydev = clock->phydev;
	u32 addr;

	addr = (offset + ((base == MCHP_RDS_PTP_PORT) ? BASE_PORT(clock) :
			  BASE_CLK(clock)));

	return phy_write_mmd(phydev, PTP_MMD(clock), addr, val);
}

static int mchp_rds_phy_modify_mmd(struct mchp_rds_ptp_clock *clock,
				   u32 offset, enum mchp_rds_ptp_base base,
				   u16 mask, u16 val)
{
	struct phy_device *phydev = clock->phydev;
	u32 addr;

	addr = (offset + ((base == MCHP_RDS_PTP_PORT) ? BASE_PORT(clock) :
			  BASE_CLK(clock)));

	return phy_modify_mmd(phydev, PTP_MMD(clock), addr, mask, val);
}

static int mchp_rds_phy_set_bits_mmd(struct mchp_rds_ptp_clock *clock,
				     u32 offset, enum mchp_rds_ptp_base base,
				     u16 val)
{
	struct phy_device *phydev = clock->phydev;
	u32 addr;

	addr = (offset + ((base == MCHP_RDS_PTP_PORT) ? BASE_PORT(clock) :
			  BASE_CLK(clock)));

	return phy_set_bits_mmd(phydev, PTP_MMD(clock), addr, val);
}

static int mchp_get_pulsewidth(struct phy_device *phydev,
			       struct ptp_perout_request *perout_request,
			       int *pulse_width)
{
	struct timespec64 ts_period;
	s64 ts_on_nsec, period_nsec;
	struct timespec64 ts_on;
	static const s64 sup_on_necs[] = {
		100,		/* 100ns */
		500,		/* 500ns */
		1000,		/* 1us */
		5000,		/* 5us */
		10000,		/* 10us */
		50000,		/* 50us */
		100000,		/* 100us */
		500000,		/* 500us */
		1000000,	/* 1ms */
		5000000,	/* 5ms */
		10000000,	/* 10ms */
		50000000,	/* 50ms */
		100000000,	/* 100ms */
		200000000,	/* 200ms */
	};

	ts_period.tv_sec = perout_request->period.sec;
	ts_period.tv_nsec = perout_request->period.nsec;

	ts_on.tv_sec = perout_request->on.sec;
	ts_on.tv_nsec = perout_request->on.nsec;
	ts_on_nsec = timespec64_to_ns(&ts_on);
	period_nsec = timespec64_to_ns(&ts_period);

	if (period_nsec < 200) {
		phydev_warn(phydev, "perout period small, minimum is 200ns\n");
		return -EOPNOTSUPP;
	}

	for (int i = 0; i < ARRAY_SIZE(sup_on_necs); i++) {
		if (ts_on_nsec <= sup_on_necs[i]) {
			*pulse_width = i;
			break;
		}
	}

	phydev_info(phydev, "pulse width is %d\n", *pulse_width);
	return 0;
}

static int mchp_general_event_config(struct mchp_rds_ptp_clock *clock,
				     int pulse_width)
{
	int general_config;

	general_config = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_GEN_CFG,
					       MCHP_RDS_PTP_CLOCK);
	if (general_config < 0)
		return general_config;

	general_config &= ~MCHP_RDS_PTP_GEN_CFG_LTC_EVT_MASK;
	general_config |= MCHP_RDS_PTP_GEN_CFG_LTC_EVT_SET(pulse_width);
	general_config &= ~MCHP_RDS_PTP_GEN_CFG_RELOAD_ADD;
	general_config |= MCHP_RDS_PTP_GEN_CFG_POLARITY;

	return mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_GEN_CFG,
				      MCHP_RDS_PTP_CLOCK, general_config);
}

static int mchp_set_clock_reload(struct mchp_rds_ptp_clock *clock,
				 s64 period_sec, u32 period_nsec)
{
	int rc;

	rc = mchp_rds_phy_write_mmd(clock,
				    MCHP_RDS_PTP_CLK_TRGT_RELOAD_SEC_LO,
				    MCHP_RDS_PTP_CLOCK,
				    lower_16_bits(period_sec));
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock,
				    MCHP_RDS_PTP_CLK_TRGT_RELOAD_SEC_HI,
				    MCHP_RDS_PTP_CLOCK,
				    upper_16_bits(period_sec));
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock,
				    MCHP_RDS_PTP_CLK_TRGT_RELOAD_NS_LO,
				    MCHP_RDS_PTP_CLOCK,
				    lower_16_bits(period_nsec));
	if (rc < 0)
		return rc;

	return mchp_rds_phy_write_mmd(clock,
				      MCHP_RDS_PTP_CLK_TRGT_RELOAD_NS_HI,
				      MCHP_RDS_PTP_CLOCK,
				      upper_16_bits(period_nsec) & 0x3fff);
}

static int mchp_set_clock_target(struct mchp_rds_ptp_clock *clock,
				 s64 start_sec, u32 start_nsec)
{
	int rc;

	/* Set the start time */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_CLK_TRGT_SEC_LO,
				    MCHP_RDS_PTP_CLOCK,
				    lower_16_bits(start_sec));
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_CLK_TRGT_SEC_HI,
				    MCHP_RDS_PTP_CLOCK,
				    upper_16_bits(start_sec));
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_CLK_TRGT_NS_LO,
				    MCHP_RDS_PTP_CLOCK,
				    lower_16_bits(start_nsec));
	if (rc < 0)
		return rc;

	return mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_CLK_TRGT_NS_HI,
				      MCHP_RDS_PTP_CLOCK,
				      upper_16_bits(start_nsec) & 0x3fff);
}

static int mchp_rds_ptp_perout_off(struct mchp_rds_ptp_clock *clock)
{
	u16 general_config;
	int rc;

	/* Set target to too far in the future, effectively disabling it */
	rc = mchp_set_clock_target(clock, 0xFFFFFFFF, 0);
	if (rc < 0)
		return rc;

	general_config = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_GEN_CFG,
					       MCHP_RDS_PTP_CLOCK);
	general_config |= MCHP_RDS_PTP_GEN_CFG_RELOAD_ADD;
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_GEN_CFG,
				    MCHP_RDS_PTP_CLOCK, general_config);
	if (rc < 0)
		return rc;

	clock->mchp_rds_ptp_event = -1;

	return 0;
}

static bool mchp_get_event(struct mchp_rds_ptp_clock *clock, int pin)
{
	if (clock->mchp_rds_ptp_event < 0 && pin == clock->event_pin) {
		clock->mchp_rds_ptp_event = pin;
		return true;
	}

	return false;
}

static int mchp_rds_ptp_perout(struct ptp_clock_info *ptpci,
			       struct ptp_perout_request *perout, int on)
{
	struct mchp_rds_ptp_clock *clock = container_of(ptpci,
						      struct mchp_rds_ptp_clock,
						      caps);
	struct phy_device *phydev = clock->phydev;
	int ret, event_pin, pulsewidth;

	/* Reject requests with unsupported flags */
	if (perout->flags & ~PTP_PEROUT_DUTY_CYCLE)
		return -EOPNOTSUPP;

	event_pin = ptp_find_pin(clock->ptp_clock, PTP_PF_PEROUT,
				 perout->index);
	if (event_pin != clock->event_pin)
		return -EINVAL;

	if (!on) {
		ret = mchp_rds_ptp_perout_off(clock);
		return ret;
	}

	if (!mchp_get_event(clock, event_pin))
		return -EINVAL;

	ret = mchp_get_pulsewidth(phydev, perout, &pulsewidth);
	if (ret < 0)
		return ret;

	/* Configure to pulse every period */
	ret = mchp_general_event_config(clock, pulsewidth);
	if (ret < 0)
		return ret;

	ret = mchp_set_clock_target(clock, perout->start.sec,
				    perout->start.nsec);
	if (ret < 0)
		return ret;

	return mchp_set_clock_reload(clock, perout->period.sec,
				     perout->period.nsec);
}

static int mchp_rds_ptpci_enable(struct ptp_clock_info *ptpci,
				 struct ptp_clock_request *request, int on)
{
	switch (request->type) {
	case PTP_CLK_REQ_PEROUT:
		return mchp_rds_ptp_perout(ptpci, &request->perout, on);
	default:
		return -EINVAL;
	}
}

static int mchp_rds_ptpci_verify(struct ptp_clock_info *ptpci, unsigned int pin,
				 enum ptp_pin_function func, unsigned int chan)
{
	struct mchp_rds_ptp_clock *clock = container_of(ptpci,
						      struct mchp_rds_ptp_clock,
						      caps);

	if (!(pin == clock->event_pin && chan == 0))
		return -1;

	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
		break;
	default:
		return -1;
	}

	return 0;
}

static int mchp_rds_ptp_flush_fifo(struct mchp_rds_ptp_clock *clock,
				   enum mchp_rds_ptp_fifo_dir dir)
{
	int rc;

	if (dir == MCHP_RDS_PTP_EGRESS_FIFO)
		skb_queue_purge(&clock->tx_queue);
	else
		skb_queue_purge(&clock->rx_queue);

	for (int i = 0; i < MCHP_RDS_PTP_FIFO_SIZE; ++i) {
		rc = mchp_rds_phy_read_mmd(clock,
					   dir == MCHP_RDS_PTP_EGRESS_FIFO ?
					   MCHP_RDS_PTP_TX_MSG_HDR2 :
					   MCHP_RDS_PTP_RX_MSG_HDR2,
					   MCHP_RDS_PTP_PORT);
		if (rc < 0)
			return rc;
	}
	return mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_INT_STS,
				     MCHP_RDS_PTP_PORT);
}

static int mchp_rds_ptp_config_intr(struct mchp_rds_ptp_clock *clock,
				    bool enable)
{
	/* Enable  or disable ptp interrupts */
	return mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_INT_EN,
				      MCHP_RDS_PTP_PORT,
				      enable ? MCHP_RDS_PTP_INT_ALL_MSK : 0);
}

static void mchp_rds_ptp_txtstamp(struct mii_timestamper *mii_ts,
				  struct sk_buff *skb, int type)
{
	struct mchp_rds_ptp_clock *clock = container_of(mii_ts,
						      struct mchp_rds_ptp_clock,
						      mii_ts);

	switch (clock->hwts_tx_type) {
	case HWTSTAMP_TX_ONESTEP_SYNC:
		if (ptp_msg_is_sync(skb, type)) {
			kfree_skb(skb);
			return;
		}
		fallthrough;
	case HWTSTAMP_TX_ON:
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		skb_queue_tail(&clock->tx_queue, skb);
		break;
	case HWTSTAMP_TX_OFF:
	default:
		kfree_skb(skb);
		break;
	}
}

static bool mchp_rds_ptp_get_sig_rx(struct sk_buff *skb, u16 *sig)
{
	struct ptp_header *ptp_header;
	int type;

	skb_push(skb, ETH_HLEN);
	type = ptp_classify_raw(skb);
	if (type == PTP_CLASS_NONE)
		return false;

	ptp_header = ptp_parse_header(skb, type);
	if (!ptp_header)
		return false;

	skb_pull_inline(skb, ETH_HLEN);

	*sig = (__force u16)(ntohs(ptp_header->sequence_id));

	return true;
}

static bool mchp_rds_ptp_match_skb(struct mchp_rds_ptp_clock *clock,
				   struct mchp_rds_ptp_rx_ts *rx_ts)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct sk_buff *skb, *skb_tmp;
	unsigned long flags;
	bool rc = false;
	u16 skb_sig;

	spin_lock_irqsave(&clock->rx_queue.lock, flags);
	skb_queue_walk_safe(&clock->rx_queue, skb, skb_tmp) {
		if (!mchp_rds_ptp_get_sig_rx(skb, &skb_sig))
			continue;

		if (skb_sig != rx_ts->seq_id)
			continue;

		__skb_unlink(skb, &clock->rx_queue);

		rc = true;
		break;
	}
	spin_unlock_irqrestore(&clock->rx_queue.lock, flags);

	if (rc) {
		shhwtstamps = skb_hwtstamps(skb);
		shhwtstamps->hwtstamp = ktime_set(rx_ts->seconds, rx_ts->nsec);
		netif_rx(skb);
	}

	return rc;
}

static void mchp_rds_ptp_match_rx_ts(struct mchp_rds_ptp_clock *clock,
				     struct mchp_rds_ptp_rx_ts *rx_ts)
{
	unsigned long flags;

	/* If we failed to match the skb add it to the queue for when
	 * the frame will come
	 */
	if (!mchp_rds_ptp_match_skb(clock, rx_ts)) {
		spin_lock_irqsave(&clock->rx_ts_lock, flags);
		list_add(&rx_ts->list, &clock->rx_ts_list);
		spin_unlock_irqrestore(&clock->rx_ts_lock, flags);
	} else {
		kfree(rx_ts);
	}
}

static void mchp_rds_ptp_match_rx_skb(struct mchp_rds_ptp_clock *clock,
				      struct sk_buff *skb)
{
	struct mchp_rds_ptp_rx_ts *rx_ts, *tmp, *rx_ts_var = NULL;
	struct skb_shared_hwtstamps *shhwtstamps;
	unsigned long flags;
	u16 skb_sig;

	if (!mchp_rds_ptp_get_sig_rx(skb, &skb_sig))
		return;

	/* Iterate over all RX timestamps and match it with the received skbs */
	spin_lock_irqsave(&clock->rx_ts_lock, flags);
	list_for_each_entry_safe(rx_ts, tmp, &clock->rx_ts_list, list) {
		/* Check if we found the signature we were looking for. */
		if (skb_sig != rx_ts->seq_id)
			continue;

		shhwtstamps = skb_hwtstamps(skb);
		shhwtstamps->hwtstamp = ktime_set(rx_ts->seconds, rx_ts->nsec);
		netif_rx(skb);

		rx_ts_var = rx_ts;

		break;
	}
	spin_unlock_irqrestore(&clock->rx_ts_lock, flags);

	if (rx_ts_var) {
		list_del(&rx_ts_var->list);
		kfree(rx_ts_var);
	} else {
		skb_queue_tail(&clock->rx_queue, skb);
	}
}

static bool mchp_rds_ptp_rxtstamp(struct mii_timestamper *mii_ts,
				  struct sk_buff *skb, int type)
{
	struct mchp_rds_ptp_clock *clock = container_of(mii_ts,
						      struct mchp_rds_ptp_clock,
						      mii_ts);

	if (clock->rx_filter == HWTSTAMP_FILTER_NONE ||
	    type == PTP_CLASS_NONE)
		return false;

	if ((type & clock->version) == 0 || (type & clock->layer) == 0)
		return false;

	/* Here if match occurs skb is sent to application, If not skb is added
	 * to queue and sending skb to application will get handled when
	 * interrupt occurs i.e., it get handles in interrupt handler. By
	 * any means skb will reach the application so we should not return
	 * false here if skb doesn't matches.
	 */
	mchp_rds_ptp_match_rx_skb(clock, skb);

	return true;
}

static int mchp_rds_ptp_hwtstamp(struct mii_timestamper *mii_ts,
				 struct kernel_hwtstamp_config *config,
				 struct netlink_ext_ack *extack)
{
	struct mchp_rds_ptp_clock *clock =
				container_of(mii_ts, struct mchp_rds_ptp_clock,
					     mii_ts);
	struct mchp_rds_ptp_rx_ts *rx_ts, *tmp;
	int txcfg = 0, rxcfg = 0;
	unsigned long flags;
	int rc;

	clock->hwts_tx_type = config->tx_type;
	clock->rx_filter = config->rx_filter;

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		clock->layer = 0;
		clock->version = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		clock->layer = PTP_CLASS_L4;
		clock->version = PTP_CLASS_V2;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		clock->layer = PTP_CLASS_L2;
		clock->version = PTP_CLASS_V2;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		clock->layer = PTP_CLASS_L4 | PTP_CLASS_L2;
		clock->version = PTP_CLASS_V2;
		break;
	default:
		return -ERANGE;
	}

	/* Setup parsing of the frames and enable the timestamping for ptp
	 * frames
	 */
	if (clock->layer & PTP_CLASS_L2) {
		rxcfg = MCHP_RDS_PTP_PARSE_CONFIG_LAYER2_EN;
		txcfg = MCHP_RDS_PTP_PARSE_CONFIG_LAYER2_EN;
	}
	if (clock->layer & PTP_CLASS_L4) {
		rxcfg |= MCHP_RDS_PTP_PARSE_CONFIG_IPV4_EN |
			 MCHP_RDS_PTP_PARSE_CONFIG_IPV6_EN;
		txcfg |= MCHP_RDS_PTP_PARSE_CONFIG_IPV4_EN |
			 MCHP_RDS_PTP_PARSE_CONFIG_IPV6_EN;
	}
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_RX_PARSE_CONFIG,
				    MCHP_RDS_PTP_PORT, rxcfg);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TX_PARSE_CONFIG,
				    MCHP_RDS_PTP_PORT, txcfg);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_RX_TIMESTAMP_EN,
				    MCHP_RDS_PTP_PORT,
				    MCHP_RDS_PTP_TIMESTAMP_EN_ALL);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TX_TIMESTAMP_EN,
				    MCHP_RDS_PTP_PORT,
				    MCHP_RDS_PTP_TIMESTAMP_EN_ALL);
	if (rc < 0)
		return rc;

	if (clock->hwts_tx_type == HWTSTAMP_TX_ONESTEP_SYNC)
		/* Enable / disable of the TX timestamp in the SYNC frames */
		rc = mchp_rds_phy_modify_mmd(clock, MCHP_RDS_PTP_TX_MOD,
					     MCHP_RDS_PTP_PORT,
					     MCHP_RDS_TX_MOD_PTP_SYNC_TS_INSERT,
					    MCHP_RDS_TX_MOD_PTP_SYNC_TS_INSERT);
	else
		rc = mchp_rds_phy_modify_mmd(clock, MCHP_RDS_PTP_TX_MOD,
					     MCHP_RDS_PTP_PORT,
					     MCHP_RDS_TX_MOD_PTP_SYNC_TS_INSERT,
				      (u16)~MCHP_RDS_TX_MOD_PTP_SYNC_TS_INSERT);

	if (rc < 0)
		return rc;

	/* In case of multiple starts and stops, these needs to be cleared */
	spin_lock_irqsave(&clock->rx_ts_lock, flags);
	list_for_each_entry_safe(rx_ts, tmp, &clock->rx_ts_list, list) {
		list_del(&rx_ts->list);
		kfree(rx_ts);
	}
	spin_unlock_irqrestore(&clock->rx_ts_lock, flags);

	rc = mchp_rds_ptp_flush_fifo(clock, MCHP_RDS_PTP_INGRESS_FIFO);
	if (rc < 0)
		return rc;

	rc = mchp_rds_ptp_flush_fifo(clock, MCHP_RDS_PTP_EGRESS_FIFO);
	if (rc < 0)
		return rc;

	/* Now enable the timestamping interrupts */
	rc = mchp_rds_ptp_config_intr(clock,
				      config->rx_filter != HWTSTAMP_FILTER_NONE);

	return rc < 0 ? rc : 0;
}

static int mchp_rds_ptp_ts_info(struct mii_timestamper *mii_ts,
				struct kernel_ethtool_ts_info *info)
{
	struct mchp_rds_ptp_clock *clock = container_of(mii_ts,
						      struct mchp_rds_ptp_clock,
						      mii_ts);

	info->phc_index = ptp_clock_index(clock->ptp_clock);

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON) |
			 BIT(HWTSTAMP_TX_ONESTEP_SYNC);

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

static int mchp_rds_ptp_ltc_adjtime(struct ptp_clock_info *info, s64 delta)
{
	struct mchp_rds_ptp_clock *clock = container_of(info,
						      struct mchp_rds_ptp_clock,
						      caps);
	struct timespec64 ts;
	bool add = true;
	int rc = 0;
	u32 nsec;
	s32 sec;

	/* The HW allows up to 15 sec to adjust the time, but here we limit to
	 * 10 sec the adjustment. The reason is, in case the adjustment is 14
	 * sec and 999999999 nsec, then we add 8ns to compensate the actual
	 * increment so the value can be bigger than 15 sec. Therefore limit the
	 * possible adjustments so we will not have these corner cases
	 */
	if (delta > 10000000000LL || delta < -10000000000LL) {
		/* The timeadjustment is too big, so fall back using set time */
		u64 now;

		info->gettime64(info, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		info->settime64(info, &ts);
		return 0;
	}
	sec = div_u64_rem(abs(delta), NSEC_PER_SEC, &nsec);
	if (delta < 0 && nsec != 0) {
		/* It is not allowed to adjust low the nsec part, therefore
		 * subtract more from second part and add to nanosecond such
		 * that would roll over, so the second part will increase
		 */
		sec--;
		nsec = NSEC_PER_SEC - nsec;
	}

	/* Calculate the adjustments and the direction */
	if (delta < 0)
		add = false;

	if (nsec > 0) {
		/* add 8 ns to cover the likely normal increment */
		nsec += 8;

		if (nsec >= NSEC_PER_SEC) {
			/* carry into seconds */
			sec++;
			nsec -= NSEC_PER_SEC;
		}
	}

	mutex_lock(&clock->ptp_lock);
	if (sec) {
		sec = abs(sec);

		rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_STEP_ADJ_LO,
					    MCHP_RDS_PTP_CLOCK, sec);
		if (rc < 0)
			goto out_unlock;

		rc = mchp_rds_phy_set_bits_mmd(clock, MCHP_RDS_PTP_STEP_ADJ_HI,
					       MCHP_RDS_PTP_CLOCK,
					       ((add ?
						 MCHP_RDS_PTP_STEP_ADJ_HI_DIR :
						 0) | ((sec >> 16) &
						       GENMASK(13, 0))));
		if (rc < 0)
			goto out_unlock;

		rc = mchp_rds_phy_set_bits_mmd(clock, MCHP_RDS_PTP_CMD_CTL,
					       MCHP_RDS_PTP_CLOCK,
					     MCHP_RDS_PTP_CMD_CTL_LTC_STEP_SEC);
		if (rc < 0)
			goto out_unlock;
	}

	if (nsec) {
		rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_STEP_ADJ_LO,
					    MCHP_RDS_PTP_CLOCK,
					    nsec & GENMASK(15, 0));
		if (rc < 0)
			goto out_unlock;

		rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_STEP_ADJ_HI,
					    MCHP_RDS_PTP_CLOCK,
					    (nsec >> 16) & GENMASK(13, 0));
		if (rc < 0)
			goto out_unlock;

		rc = mchp_rds_phy_set_bits_mmd(clock, MCHP_RDS_PTP_CMD_CTL,
					       MCHP_RDS_PTP_CLOCK,
					    MCHP_RDS_PTP_CMD_CTL_LTC_STEP_NSEC);
	}

	mutex_unlock(&clock->ptp_lock);
	info->gettime64(info, &ts);
	mutex_lock(&clock->ptp_lock);

	/* Target update is required for pulse generation on events that
	 * are enabled
	 */
	if (clock->mchp_rds_ptp_event >= 0)
		mchp_set_clock_target(clock,
				      ts.tv_sec + MCHP_RDS_PTP_BUFFER_TIME, 0);
out_unlock:
	mutex_unlock(&clock->ptp_lock);

	return rc;
}

static int mchp_rds_ptp_ltc_adjfine(struct ptp_clock_info *info,
				    long scaled_ppm)
{
	struct mchp_rds_ptp_clock *clock = container_of(info,
						      struct mchp_rds_ptp_clock,
						      caps);
	u16 rate_lo, rate_hi;
	bool faster = true;
	u32 rate;
	int rc;

	if (!scaled_ppm)
		return 0;

	if (scaled_ppm < 0) {
		scaled_ppm = -scaled_ppm;
		faster = false;
	}

	rate = MCHP_RDS_PTP_1PPM_FORMAT * (upper_16_bits(scaled_ppm));
	rate += (MCHP_RDS_PTP_1PPM_FORMAT * (lower_16_bits(scaled_ppm))) >> 16;

	rate_lo = rate & GENMASK(15, 0);
	rate_hi = (rate >> 16) & GENMASK(13, 0);

	if (faster)
		rate_hi |= MCHP_RDS_PTP_LTC_RATE_ADJ_HI_DIR;

	mutex_lock(&clock->ptp_lock);
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_LTC_RATE_ADJ_HI,
				    MCHP_RDS_PTP_CLOCK, rate_hi);
	if (rc < 0)
		goto error;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_LTC_RATE_ADJ_LO,
				    MCHP_RDS_PTP_CLOCK, rate_lo);
	if (rc > 0)
		rc = 0;
error:
	mutex_unlock(&clock->ptp_lock);

	return rc;
}

static int mchp_rds_ptp_ltc_gettime64(struct ptp_clock_info *info,
				      struct timespec64 *ts)
{
	struct mchp_rds_ptp_clock *clock = container_of(info,
						      struct mchp_rds_ptp_clock,
						      caps);
	time64_t secs;
	int rc = 0;
	s64 nsecs;

	mutex_lock(&clock->ptp_lock);
	/* Set read bit to 1 to save current values of 1588 local time counter
	 * into PTP LTC seconds and nanoseconds registers.
	 */
	rc = mchp_rds_phy_set_bits_mmd(clock, MCHP_RDS_PTP_CMD_CTL,
				       MCHP_RDS_PTP_CLOCK,
				       MCHP_RDS_PTP_CMD_CTL_CLOCK_READ);
	if (rc < 0)
		goto out_unlock;

	/* Get LTC clock values */
	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_LTC_READ_SEC_HI,
				   MCHP_RDS_PTP_CLOCK);
	if (rc < 0)
		goto out_unlock;
	secs = rc << 16;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_LTC_READ_SEC_MID,
				   MCHP_RDS_PTP_CLOCK);
	if (rc < 0)
		goto out_unlock;
	secs |= rc;
	secs <<= 16;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_LTC_READ_SEC_LO,
				   MCHP_RDS_PTP_CLOCK);
	if (rc < 0)
		goto out_unlock;
	secs |= rc;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_LTC_READ_NS_HI,
				   MCHP_RDS_PTP_CLOCK);
	if (rc < 0)
		goto out_unlock;
	nsecs = (rc & GENMASK(13, 0));
	nsecs <<= 16;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_LTC_READ_NS_LO,
				   MCHP_RDS_PTP_CLOCK);
	if (rc < 0)
		goto out_unlock;
	nsecs |= rc;

	set_normalized_timespec64(ts, secs, nsecs);

	if (rc > 0)
		rc = 0;
out_unlock:
	mutex_unlock(&clock->ptp_lock);

	return rc;
}

static int mchp_rds_ptp_ltc_settime64(struct ptp_clock_info *info,
				      const struct timespec64 *ts)
{
	struct mchp_rds_ptp_clock *clock = container_of(info,
						      struct mchp_rds_ptp_clock,
						      caps);
	int rc;

	mutex_lock(&clock->ptp_lock);
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_LTC_SEC_LO,
				    MCHP_RDS_PTP_CLOCK,
				    lower_16_bits(ts->tv_sec));
	if (rc < 0)
		goto out_unlock;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_LTC_SEC_MID,
				    MCHP_RDS_PTP_CLOCK,
				    upper_16_bits(ts->tv_sec));
	if (rc < 0)
		goto out_unlock;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_LTC_SEC_HI,
				    MCHP_RDS_PTP_CLOCK,
				    upper_32_bits(ts->tv_sec) & GENMASK(15, 0));
	if (rc < 0)
		goto out_unlock;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_LTC_NS_LO,
				    MCHP_RDS_PTP_CLOCK,
				    lower_16_bits(ts->tv_nsec));
	if (rc < 0)
		goto out_unlock;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_LTC_NS_HI,
				    MCHP_RDS_PTP_CLOCK,
				   upper_16_bits(ts->tv_nsec) & GENMASK(13, 0));
	if (rc < 0)
		goto out_unlock;

	/* Set load bit to 1 to write PTP LTC seconds and nanoseconds
	 * registers to 1588 local time counter.
	 */
	rc = mchp_rds_phy_set_bits_mmd(clock, MCHP_RDS_PTP_CMD_CTL,
				       MCHP_RDS_PTP_CLOCK,
				       MCHP_RDS_PTP_CMD_CTL_CLOCK_LOAD);
	if (rc > 0)
		rc = 0;
out_unlock:
	mutex_unlock(&clock->ptp_lock);

	return rc;
}

static bool mchp_rds_ptp_get_sig_tx(struct sk_buff *skb, u16 *sig)
{
	struct ptp_header *ptp_header;
	int type;

	type = ptp_classify_raw(skb);
	if (type == PTP_CLASS_NONE)
		return false;

	ptp_header = ptp_parse_header(skb, type);
	if (!ptp_header)
		return false;

	*sig = (__force u16)(ntohs(ptp_header->sequence_id));

	return true;
}

static void mchp_rds_ptp_match_tx_skb(struct mchp_rds_ptp_clock *clock,
				      u32 seconds, u32 nsec, u16 seq_id)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *skb, *skb_tmp;
	unsigned long flags;
	bool rc = false;
	u16 skb_sig;

	spin_lock_irqsave(&clock->tx_queue.lock, flags);
	skb_queue_walk_safe(&clock->tx_queue, skb, skb_tmp) {
		if (!mchp_rds_ptp_get_sig_tx(skb, &skb_sig))
			continue;

		if (skb_sig != seq_id)
			continue;

		__skb_unlink(skb, &clock->tx_queue);
		rc = true;
		break;
	}
	spin_unlock_irqrestore(&clock->tx_queue.lock, flags);

	if (rc) {
		shhwtstamps.hwtstamp = ktime_set(seconds, nsec);
		skb_complete_tx_timestamp(skb, &shhwtstamps);
	}
}

static struct mchp_rds_ptp_rx_ts
		       *mchp_rds_ptp_get_rx_ts(struct mchp_rds_ptp_clock *clock)
{
	struct phy_device *phydev = clock->phydev;
	struct mchp_rds_ptp_rx_ts *rx_ts = NULL;
	u32 sec, nsec;
	int rc;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_RX_INGRESS_NS_HI,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		goto error;
	if (!(rc & MCHP_RDS_PTP_RX_INGRESS_NS_HI_TS_VALID)) {
		phydev_err(phydev, "RX Timestamp is not valid!\n");
		goto error;
	}
	nsec = (rc & GENMASK(13, 0)) << 16;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_RX_INGRESS_NS_LO,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		goto error;
	nsec |= rc;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_RX_INGRESS_SEC_HI,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		goto error;
	sec = rc << 16;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_RX_INGRESS_SEC_LO,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		goto error;
	sec |= rc;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_RX_MSG_HDR2,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		goto error;

	rx_ts = kmalloc(sizeof(*rx_ts), GFP_KERNEL);
	if (!rx_ts)
		return NULL;

	rx_ts->seconds = sec;
	rx_ts->nsec = nsec;
	rx_ts->seq_id = rc;

error:
	return rx_ts;
}

static void mchp_rds_ptp_process_rx_ts(struct mchp_rds_ptp_clock *clock)
{
	int caps;

	do {
		struct mchp_rds_ptp_rx_ts *rx_ts;

		rx_ts = mchp_rds_ptp_get_rx_ts(clock);
		if (rx_ts)
			mchp_rds_ptp_match_rx_ts(clock, rx_ts);

		caps = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_CAP_INFO,
					     MCHP_RDS_PTP_PORT);
		if (caps < 0)
			return;
	} while (MCHP_RDS_PTP_RX_TS_CNT(caps) > 0);
}

static bool mchp_rds_ptp_get_tx_ts(struct mchp_rds_ptp_clock *clock,
				   u32 *sec, u32 *nsec, u16 *seq)
{
	int rc;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_TX_EGRESS_NS_HI,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		return false;
	if (!(rc & MCHP_RDS_PTP_TX_EGRESS_NS_HI_TS_VALID))
		return false;
	*nsec = (rc & GENMASK(13, 0)) << 16;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_TX_EGRESS_NS_LO,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		return false;
	*nsec = *nsec | rc;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_TX_EGRESS_SEC_HI,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		return false;
	*sec = rc << 16;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_TX_EGRESS_SEC_LO,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		return false;
	*sec = *sec | rc;

	rc = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_TX_MSG_HDR2,
				   MCHP_RDS_PTP_PORT);
	if (rc < 0)
		return false;

	*seq = rc;

	return true;
}

static void mchp_rds_ptp_process_tx_ts(struct mchp_rds_ptp_clock *clock)
{
	int caps;

	do {
		u32 sec, nsec;
		u16 seq;

		if (mchp_rds_ptp_get_tx_ts(clock, &sec, &nsec, &seq))
			mchp_rds_ptp_match_tx_skb(clock, sec, nsec, seq);

		caps = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_CAP_INFO,
					     MCHP_RDS_PTP_PORT);
		if (caps < 0)
			return;
	} while (MCHP_RDS_PTP_TX_TS_CNT(caps) > 0);
}

int mchp_rds_ptp_top_config_intr(struct mchp_rds_ptp_clock *clock,
				 u16 reg, u16 val, bool clear)
{
	if (clear)
		return phy_clear_bits_mmd(clock->phydev, PTP_MMD(clock), reg,
					  val);
	else
		return phy_set_bits_mmd(clock->phydev, PTP_MMD(clock), reg,
					val);
}
EXPORT_SYMBOL_GPL(mchp_rds_ptp_top_config_intr);

irqreturn_t mchp_rds_ptp_handle_interrupt(struct mchp_rds_ptp_clock *clock)
{
	int irq_sts;

	/* To handle rogue interrupt scenarios */
	if (!clock)
		return IRQ_NONE;

	do {
		irq_sts = mchp_rds_phy_read_mmd(clock, MCHP_RDS_PTP_INT_STS,
						MCHP_RDS_PTP_PORT);
		if (irq_sts < 0)
			return IRQ_NONE;

		if (irq_sts & MCHP_RDS_PTP_INT_RX_TS_EN)
			mchp_rds_ptp_process_rx_ts(clock);

		if (irq_sts & MCHP_RDS_PTP_INT_TX_TS_EN)
			mchp_rds_ptp_process_tx_ts(clock);

		if (irq_sts & MCHP_RDS_PTP_INT_TX_TS_OVRFL_EN)
			mchp_rds_ptp_flush_fifo(clock,
						MCHP_RDS_PTP_EGRESS_FIFO);

		if (irq_sts & MCHP_RDS_PTP_INT_RX_TS_OVRFL_EN)
			mchp_rds_ptp_flush_fifo(clock,
						MCHP_RDS_PTP_INGRESS_FIFO);
	} while (irq_sts & (MCHP_RDS_PTP_INT_RX_TS_EN |
			    MCHP_RDS_PTP_INT_TX_TS_EN |
			    MCHP_RDS_PTP_INT_TX_TS_OVRFL_EN |
			    MCHP_RDS_PTP_INT_RX_TS_OVRFL_EN));

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(mchp_rds_ptp_handle_interrupt);

static int mchp_rds_ptp_init(struct mchp_rds_ptp_clock *clock)
{
	int rc;

	/* Disable PTP */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_CMD_CTL,
				    MCHP_RDS_PTP_CLOCK,
				    MCHP_RDS_PTP_CMD_CTL_DIS);
	if (rc < 0)
		return rc;

	/* Disable TSU */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TSU_GEN_CONFIG,
				    MCHP_RDS_PTP_PORT, 0);
	if (rc < 0)
		return rc;

	/* Clear PTP interrupt status registers */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TSU_HARD_RESET,
				    MCHP_RDS_PTP_PORT,
				    MCHP_RDS_PTP_TSU_HARDRESET);
	if (rc < 0)
		return rc;

	/* Predictor enable */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_LATENCY_CORRECTION_CTL,
				    MCHP_RDS_PTP_CLOCK,
				    MCHP_RDS_PTP_LATENCY_SETTING);
	if (rc < 0)
		return rc;

	/* Configure PTP operational mode */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_OP_MODE,
				    MCHP_RDS_PTP_CLOCK,
				    MCHP_RDS_PTP_OP_MODE_STANDALONE);
	if (rc < 0)
		return rc;

	/* Reference clock configuration */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_REF_CLK_CFG,
				    MCHP_RDS_PTP_CLOCK,
				    MCHP_RDS_PTP_REF_CLK_CFG_SET);
	if (rc < 0)
		return rc;

	/* Classifier configurations */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_RX_PARSE_CONFIG,
				    MCHP_RDS_PTP_PORT, 0);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TX_PARSE_CONFIG,
				    MCHP_RDS_PTP_PORT, 0);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TX_PARSE_L2_ADDR_EN,
				    MCHP_RDS_PTP_PORT, 0);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_RX_PARSE_L2_ADDR_EN,
				    MCHP_RDS_PTP_PORT, 0);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_RX_PARSE_IPV4_ADDR_EN,
				    MCHP_RDS_PTP_PORT, 0);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TX_PARSE_IPV4_ADDR_EN,
				    MCHP_RDS_PTP_PORT, 0);
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_RX_VERSION,
				    MCHP_RDS_PTP_PORT,
				    MCHP_RDS_PTP_MAX_VERSION(0xff) |
				    MCHP_RDS_PTP_MIN_VERSION(0x0));
	if (rc < 0)
		return rc;

	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TX_VERSION,
				    MCHP_RDS_PTP_PORT,
				    MCHP_RDS_PTP_MAX_VERSION(0xff) |
				    MCHP_RDS_PTP_MIN_VERSION(0x0));
	if (rc < 0)
		return rc;

	/* Enable TSU */
	rc = mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_TSU_GEN_CONFIG,
				    MCHP_RDS_PTP_PORT,
				    MCHP_RDS_PTP_TSU_GEN_CFG_TSU_EN);
	if (rc < 0)
		return rc;

	/* Enable PTP */
	return mchp_rds_phy_write_mmd(clock, MCHP_RDS_PTP_CMD_CTL,
				      MCHP_RDS_PTP_CLOCK,
				      MCHP_RDS_PTP_CMD_CTL_EN);
}

struct mchp_rds_ptp_clock *mchp_rds_ptp_probe(struct phy_device *phydev, u8 mmd,
					      u16 clk_base_addr,
					      u16 port_base_addr)
{
	struct mchp_rds_ptp_clock *clock;
	int rc;

	clock = devm_kzalloc(&phydev->mdio.dev, sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	clock->port_base_addr	= port_base_addr;
	clock->clk_base_addr	= clk_base_addr;
	clock->mmd		= mmd;

	mutex_init(&clock->ptp_lock);
	clock->pin_config = devm_kmalloc_array(&phydev->mdio.dev,
					       MCHP_RDS_PTP_N_PIN,
					       sizeof(*clock->pin_config),
					       GFP_KERNEL);
	if (!clock->pin_config)
		return ERR_PTR(-ENOMEM);

	for (int i = 0; i < MCHP_RDS_PTP_N_PIN; ++i) {
		struct ptp_pin_desc *p = &clock->pin_config[i];

		memset(p, 0, sizeof(*p));
		snprintf(p->name, sizeof(p->name), "pin%d", i);
		p->index = i;
		p->func = PTP_PF_NONE;
	}
	/* Register PTP clock */
	clock->caps.owner          = THIS_MODULE;
	snprintf(clock->caps.name, 30, "%s", phydev->drv->name);
	clock->caps.max_adj        = MCHP_RDS_PTP_MAX_ADJ;
	clock->caps.n_ext_ts       = 0;
	clock->caps.pps            = 0;
	clock->caps.n_pins         = MCHP_RDS_PTP_N_PIN;
	clock->caps.n_per_out      = MCHP_RDS_PTP_N_PEROUT;
	clock->caps.pin_config     = clock->pin_config;
	clock->caps.adjfine        = mchp_rds_ptp_ltc_adjfine;
	clock->caps.adjtime        = mchp_rds_ptp_ltc_adjtime;
	clock->caps.gettime64      = mchp_rds_ptp_ltc_gettime64;
	clock->caps.settime64      = mchp_rds_ptp_ltc_settime64;
	clock->caps.enable         = mchp_rds_ptpci_enable;
	clock->caps.verify         = mchp_rds_ptpci_verify;
	clock->caps.getcrosststamp = NULL;
	clock->ptp_clock = ptp_clock_register(&clock->caps,
					      &phydev->mdio.dev);
	if (IS_ERR(clock->ptp_clock))
		return ERR_PTR(-EINVAL);

	/* Check if PHC support is missing at the configuration level */
	if (!clock->ptp_clock)
		return NULL;

	/* Initialize the SW */
	skb_queue_head_init(&clock->tx_queue);
	skb_queue_head_init(&clock->rx_queue);
	INIT_LIST_HEAD(&clock->rx_ts_list);
	spin_lock_init(&clock->rx_ts_lock);

	clock->mii_ts.rxtstamp = mchp_rds_ptp_rxtstamp;
	clock->mii_ts.txtstamp = mchp_rds_ptp_txtstamp;
	clock->mii_ts.hwtstamp = mchp_rds_ptp_hwtstamp;
	clock->mii_ts.ts_info = mchp_rds_ptp_ts_info;

	phydev->mii_ts = &clock->mii_ts;

	clock->mchp_rds_ptp_event = -1;

	/* Timestamp selected by default to keep legacy API */
	phydev->default_timestamp = true;

	clock->phydev = phydev;

	rc = mchp_rds_ptp_init(clock);
	if (rc < 0)
		return ERR_PTR(rc);

	return clock;
}
EXPORT_SYMBOL_GPL(mchp_rds_ptp_probe);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MICROCHIP PHY RDS PTP driver");
MODULE_AUTHOR("Divya Koppera");
