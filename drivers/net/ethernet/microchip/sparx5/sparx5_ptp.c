// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */
#include <linux/ptp_classify.h>

#include "sparx5_main_regs.h"
#include "sparx5_main.h"

#define SPARX5_MAX_PTP_ID	512

#define TOD_ACC_PIN		0x4

enum {
	PTP_PIN_ACTION_IDLE = 0,
	PTP_PIN_ACTION_LOAD,
	PTP_PIN_ACTION_SAVE,
	PTP_PIN_ACTION_CLOCK,
	PTP_PIN_ACTION_DELTA,
	PTP_PIN_ACTION_TOD
};

static u64 sparx5_ptp_get_1ppm(struct sparx5 *sparx5)
{
	/* Represents 1ppm adjustment in 2^59 format with 1.59687500000(625)
	 * 1.99609375000(500), 3.99218750000(250) as reference
	 * The value is calculated as following:
	 * (1/1000000)/((2^-59)/X)
	 */

	u64 res = 0;

	switch (sparx5->coreclock) {
	case SPX5_CORE_CLOCK_250MHZ:
		res = 2301339409586;
		break;
	case SPX5_CORE_CLOCK_500MHZ:
		res = 1150669704793;
		break;
	case SPX5_CORE_CLOCK_625MHZ:
		res =  920535763834;
		break;
	default:
		WARN(1, "Invalid core clock");
		break;
	}

	return res;
}

static u64 sparx5_ptp_get_nominal_value(struct sparx5 *sparx5)
{
	u64 res = 0;

	switch (sparx5->coreclock) {
	case SPX5_CORE_CLOCK_250MHZ:
		res = 0x1FF0000000000000;
		break;
	case SPX5_CORE_CLOCK_500MHZ:
		res = 0x0FF8000000000000;
		break;
	case SPX5_CORE_CLOCK_625MHZ:
		res = 0x0CC6666666666666;
		break;
	default:
		WARN(1, "Invalid core clock");
		break;
	}

	return res;
}

int sparx5_ptp_hwtstamp_set(struct sparx5_port *port, struct ifreq *ifr)
{
	struct sparx5 *sparx5 = port->sparx5;
	struct hwtstamp_config cfg;
	struct sparx5_phc *phc;

	/* For now don't allow to run ptp on ports that are part of a bridge,
	 * because in case of transparent clock the HW will still forward the
	 * frames, so there would be duplicate frames
	 */

	if (test_bit(port->portno, sparx5->bridge_mask))
		return -EINVAL;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	switch (cfg.tx_type) {
	case HWTSTAMP_TX_ON:
		port->ptp_cmd = IFH_REW_OP_TWO_STEP_PTP;
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		port->ptp_cmd = IFH_REW_OP_ONE_STEP_PTP;
		break;
	case HWTSTAMP_TX_OFF:
		port->ptp_cmd = IFH_REW_OP_NOOP;
		break;
	default:
		return -ERANGE;
	}

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_NTP_ALL:
		cfg.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	/* Commit back the result & save it */
	mutex_lock(&sparx5->ptp_lock);
	phc = &sparx5->phc[SPARX5_PHC_PORT];
	memcpy(&phc->hwtstamp_config, &cfg, sizeof(cfg));
	mutex_unlock(&sparx5->ptp_lock);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

int sparx5_ptp_hwtstamp_get(struct sparx5_port *port, struct ifreq *ifr)
{
	struct sparx5 *sparx5 = port->sparx5;
	struct sparx5_phc *phc;

	phc = &sparx5->phc[SPARX5_PHC_PORT];
	return copy_to_user(ifr->ifr_data, &phc->hwtstamp_config,
			    sizeof(phc->hwtstamp_config)) ? -EFAULT : 0;
}

static void sparx5_ptp_classify(struct sparx5_port *port, struct sk_buff *skb,
				u8 *rew_op, u8 *pdu_type, u8 *pdu_w16_offset)
{
	struct ptp_header *header;
	u8 msgtype;
	int type;

	if (port->ptp_cmd == IFH_REW_OP_NOOP) {
		*rew_op = IFH_REW_OP_NOOP;
		*pdu_type = IFH_PDU_TYPE_NONE;
		*pdu_w16_offset = 0;
		return;
	}

	type = ptp_classify_raw(skb);
	if (type == PTP_CLASS_NONE) {
		*rew_op = IFH_REW_OP_NOOP;
		*pdu_type = IFH_PDU_TYPE_NONE;
		*pdu_w16_offset = 0;
		return;
	}

	header = ptp_parse_header(skb, type);
	if (!header) {
		*rew_op = IFH_REW_OP_NOOP;
		*pdu_type = IFH_PDU_TYPE_NONE;
		*pdu_w16_offset = 0;
		return;
	}

	*pdu_w16_offset = 7;
	if (type & PTP_CLASS_L2)
		*pdu_type = IFH_PDU_TYPE_PTP;
	if (type & PTP_CLASS_IPV4)
		*pdu_type = IFH_PDU_TYPE_IPV4_UDP_PTP;
	if (type & PTP_CLASS_IPV6)
		*pdu_type = IFH_PDU_TYPE_IPV6_UDP_PTP;

	if (port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP) {
		*rew_op = IFH_REW_OP_TWO_STEP_PTP;
		return;
	}

	/* If it is sync and run 1 step then set the correct operation,
	 * otherwise run as 2 step
	 */
	msgtype = ptp_get_msgtype(header, type);
	if ((msgtype & 0xf) == 0) {
		*rew_op = IFH_REW_OP_ONE_STEP_PTP;
		return;
	}

	*rew_op = IFH_REW_OP_TWO_STEP_PTP;
}

static void sparx5_ptp_txtstamp_old_release(struct sparx5_port *port)
{
	struct sk_buff *skb, *skb_tmp;
	unsigned long flags;

	spin_lock_irqsave(&port->tx_skbs.lock, flags);
	skb_queue_walk_safe(&port->tx_skbs, skb, skb_tmp) {
		if time_after(SPARX5_SKB_CB(skb)->jiffies + SPARX5_PTP_TIMEOUT,
			      jiffies)
			break;

		__skb_unlink(skb, &port->tx_skbs);
		dev_kfree_skb_any(skb);
	}
	spin_unlock_irqrestore(&port->tx_skbs.lock, flags);
}

int sparx5_ptp_txtstamp_request(struct sparx5_port *port,
				struct sk_buff *skb)
{
	struct sparx5 *sparx5 = port->sparx5;
	u8 rew_op, pdu_type, pdu_w16_offset;
	unsigned long flags;

	sparx5_ptp_classify(port, skb, &rew_op, &pdu_type, &pdu_w16_offset);
	SPARX5_SKB_CB(skb)->rew_op = rew_op;
	SPARX5_SKB_CB(skb)->pdu_type = pdu_type;
	SPARX5_SKB_CB(skb)->pdu_w16_offset = pdu_w16_offset;

	if (rew_op != IFH_REW_OP_TWO_STEP_PTP)
		return 0;

	sparx5_ptp_txtstamp_old_release(port);

	spin_lock_irqsave(&sparx5->ptp_ts_id_lock, flags);
	if (sparx5->ptp_skbs == SPARX5_MAX_PTP_ID) {
		spin_unlock_irqrestore(&sparx5->ptp_ts_id_lock, flags);
		return -EBUSY;
	}

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	skb_queue_tail(&port->tx_skbs, skb);
	SPARX5_SKB_CB(skb)->ts_id = port->ts_id;
	SPARX5_SKB_CB(skb)->jiffies = jiffies;

	sparx5->ptp_skbs++;
	port->ts_id++;
	if (port->ts_id == SPARX5_MAX_PTP_ID)
		port->ts_id = 0;

	spin_unlock_irqrestore(&sparx5->ptp_ts_id_lock, flags);

	return 0;
}

void sparx5_ptp_txtstamp_release(struct sparx5_port *port,
				 struct sk_buff *skb)
{
	struct sparx5 *sparx5 = port->sparx5;
	unsigned long flags;

	spin_lock_irqsave(&sparx5->ptp_ts_id_lock, flags);
	port->ts_id--;
	sparx5->ptp_skbs--;
	skb_unlink(skb, &port->tx_skbs);
	spin_unlock_irqrestore(&sparx5->ptp_ts_id_lock, flags);
}

static void sparx5_get_hwtimestamp(struct sparx5 *sparx5,
				   struct timespec64 *ts,
				   u32 nsec)
{
	/* Read current PTP time to get seconds */
	unsigned long flags;
	u32 curr_nsec;

	spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

	spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_SAVE) |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(SPARX5_PHC_PORT) |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
		 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
		 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

	ts->tv_sec = spx5_rd(sparx5, PTP_PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	curr_nsec = spx5_rd(sparx5, PTP_PTP_TOD_NSEC(TOD_ACC_PIN));

	ts->tv_nsec = nsec;

	/* Sec has incremented since the ts was registered */
	if (curr_nsec < nsec)
		ts->tv_sec--;

	spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);
}

irqreturn_t sparx5_ptp_irq_handler(int irq, void *args)
{
	int budget = SPARX5_MAX_PTP_ID;
	struct sparx5 *sparx5 = args;

	while (budget--) {
		struct sk_buff *skb, *skb_tmp, *skb_match = NULL;
		struct skb_shared_hwtstamps shhwtstamps;
		struct sparx5_port *port;
		struct timespec64 ts;
		unsigned long flags;
		u32 val, id, txport;
		u32 delay;

		val = spx5_rd(sparx5, REW_PTP_TWOSTEP_CTRL);

		/* Check if a timestamp can be retrieved */
		if (!(val & REW_PTP_TWOSTEP_CTRL_PTP_VLD))
			break;

		WARN_ON(val & REW_PTP_TWOSTEP_CTRL_PTP_OVFL);

		if (!(val & REW_PTP_TWOSTEP_CTRL_STAMP_TX))
			continue;

		/* Retrieve the ts Tx port */
		txport = REW_PTP_TWOSTEP_CTRL_STAMP_PORT_GET(val);

		/* Retrieve its associated skb */
		port = sparx5->ports[txport];

		/* Retrieve the delay */
		delay = spx5_rd(sparx5, REW_PTP_TWOSTEP_STAMP);
		delay = REW_PTP_TWOSTEP_STAMP_STAMP_NSEC_GET(delay);

		/* Get next timestamp from fifo, which needs to be the
		 * rx timestamp which represents the id of the frame
		 */
		spx5_rmw(REW_PTP_TWOSTEP_CTRL_PTP_NXT_SET(1),
			 REW_PTP_TWOSTEP_CTRL_PTP_NXT,
			 sparx5, REW_PTP_TWOSTEP_CTRL);

		val = spx5_rd(sparx5, REW_PTP_TWOSTEP_CTRL);

		/* Check if a timestamp can be retried */
		if (!(val & REW_PTP_TWOSTEP_CTRL_PTP_VLD))
			break;

		/* Read RX timestamping to get the ID */
		id = spx5_rd(sparx5, REW_PTP_TWOSTEP_STAMP);
		id <<= 8;
		id |= spx5_rd(sparx5, REW_PTP_TWOSTEP_STAMP_SUBNS);

		spin_lock_irqsave(&port->tx_skbs.lock, flags);
		skb_queue_walk_safe(&port->tx_skbs, skb, skb_tmp) {
			if (SPARX5_SKB_CB(skb)->ts_id != id)
				continue;

			__skb_unlink(skb, &port->tx_skbs);
			skb_match = skb;
			break;
		}
		spin_unlock_irqrestore(&port->tx_skbs.lock, flags);

		/* Next ts */
		spx5_rmw(REW_PTP_TWOSTEP_CTRL_PTP_NXT_SET(1),
			 REW_PTP_TWOSTEP_CTRL_PTP_NXT,
			 sparx5, REW_PTP_TWOSTEP_CTRL);

		if (WARN_ON(!skb_match))
			continue;

		spin_lock(&sparx5->ptp_ts_id_lock);
		sparx5->ptp_skbs--;
		spin_unlock(&sparx5->ptp_ts_id_lock);

		/* Get the h/w timestamp */
		sparx5_get_hwtimestamp(sparx5, &ts, delay);

		/* Set the timestamp into the skb */
		shhwtstamps.hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
		skb_tstamp_tx(skb_match, &shhwtstamps);

		dev_kfree_skb_any(skb_match);
	}

	return IRQ_HANDLED;
}

static int sparx5_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct sparx5_phc *phc = container_of(ptp, struct sparx5_phc, info);
	struct sparx5 *sparx5 = phc->sparx5;
	unsigned long flags;
	bool neg_adj = 0;
	u64 tod_inc;
	u64 ref;

	if (!scaled_ppm)
		return 0;

	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}

	tod_inc = sparx5_ptp_get_nominal_value(sparx5);

	/* The multiplication is split in 2 separate additions because of
	 * overflow issues. If scaled_ppm with 16bit fractional part was bigger
	 * than 20ppm then we got overflow.
	 */
	ref = sparx5_ptp_get_1ppm(sparx5) * (scaled_ppm >> 16);
	ref += (sparx5_ptp_get_1ppm(sparx5) * (0xffff & scaled_ppm)) >> 16;
	tod_inc = neg_adj ? tod_inc - ref : tod_inc + ref;

	spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

	spx5_rmw(PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS_SET(1 << BIT(phc->index)),
		 PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS,
		 sparx5, PTP_PTP_DOM_CFG);

	spx5_wr((u32)tod_inc & 0xFFFFFFFF, sparx5,
		PTP_CLK_PER_CFG(phc->index, 0));
	spx5_wr((u32)(tod_inc >> 32), sparx5,
		PTP_CLK_PER_CFG(phc->index, 1));

	spx5_rmw(PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS_SET(0),
		 PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS, sparx5,
		 PTP_PTP_DOM_CFG);

	spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);

	return 0;
}

static int sparx5_ptp_settime64(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	struct sparx5_phc *phc = container_of(ptp, struct sparx5_phc, info);
	struct sparx5 *sparx5 = phc->sparx5;
	unsigned long flags;

	spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

	/* Must be in IDLE mode before the time can be loaded */
	spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
		 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
		 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

	/* Set new value */
	spx5_wr(PTP_PTP_TOD_SEC_MSB_PTP_TOD_SEC_MSB_SET(upper_32_bits(ts->tv_sec)),
		sparx5, PTP_PTP_TOD_SEC_MSB(TOD_ACC_PIN));
	spx5_wr(lower_32_bits(ts->tv_sec),
		sparx5, PTP_PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	spx5_wr(ts->tv_nsec, sparx5, PTP_PTP_TOD_NSEC(TOD_ACC_PIN));

	/* Apply new values */
	spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_LOAD) |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
		 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
		 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

	spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);

	return 0;
}

int sparx5_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct sparx5_phc *phc = container_of(ptp, struct sparx5_phc, info);
	struct sparx5 *sparx5 = phc->sparx5;
	unsigned long flags;
	time64_t s;
	s64 ns;

	spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

	spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_SAVE) |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
		 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
		 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

	s = spx5_rd(sparx5, PTP_PTP_TOD_SEC_MSB(TOD_ACC_PIN));
	s <<= 32;
	s |= spx5_rd(sparx5, PTP_PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	ns = spx5_rd(sparx5, PTP_PTP_TOD_NSEC(TOD_ACC_PIN));
	ns &= PTP_PTP_TOD_NSEC_PTP_TOD_NSEC;

	spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);

	/* Deal with negative values */
	if ((ns & 0xFFFFFFF0) == 0x3FFFFFF0) {
		s--;
		ns &= 0xf;
		ns += 999999984;
	}

	set_normalized_timespec64(ts, s, ns);
	return 0;
}

static int sparx5_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct sparx5_phc *phc = container_of(ptp, struct sparx5_phc, info);
	struct sparx5 *sparx5 = phc->sparx5;

	if (delta > -(NSEC_PER_SEC / 2) && delta < (NSEC_PER_SEC / 2)) {
		unsigned long flags;

		spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

		/* Must be in IDLE mode before the time can be loaded */
		spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
			 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
			 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
			 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
			 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
			 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
			 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

		spx5_wr(PTP_PTP_TOD_NSEC_PTP_TOD_NSEC_SET(delta),
			sparx5, PTP_PTP_TOD_NSEC(TOD_ACC_PIN));

		/* Adjust time with the value of PTP_TOD_NSEC */
		spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_DELTA) |
			 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
			 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
			 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
			 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
			 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
			 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

		spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);
	} else {
		/* Fall back using sparx5_ptp_settime64 which is not exact */
		struct timespec64 ts;
		u64 now;

		sparx5_ptp_gettime64(ptp, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		sparx5_ptp_settime64(ptp, &ts);
	}

	return 0;
}

static struct ptp_clock_info sparx5_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "sparx5 ptp",
	.max_adj	= 200000,
	.gettime64	= sparx5_ptp_gettime64,
	.settime64	= sparx5_ptp_settime64,
	.adjtime	= sparx5_ptp_adjtime,
	.adjfine	= sparx5_ptp_adjfine,
};

static int sparx5_ptp_phc_init(struct sparx5 *sparx5,
			       int index,
			       struct ptp_clock_info *clock_info)
{
	struct sparx5_phc *phc = &sparx5->phc[index];

	phc->info = *clock_info;
	phc->clock = ptp_clock_register(&phc->info, sparx5->dev);
	if (IS_ERR(phc->clock))
		return PTR_ERR(phc->clock);

	phc->index = index;
	phc->sparx5 = sparx5;

	/* PTP Rx stamping is always enabled.  */
	phc->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;

	return 0;
}

int sparx5_ptp_init(struct sparx5 *sparx5)
{
	u64 tod_adj = sparx5_ptp_get_nominal_value(sparx5);
	struct sparx5_port *port;
	int err, i;

	if (!sparx5->ptp)
		return 0;

	for (i = 0; i < SPARX5_PHC_COUNT; ++i) {
		err = sparx5_ptp_phc_init(sparx5, i, &sparx5_ptp_clock_info);
		if (err)
			return err;
	}

	spin_lock_init(&sparx5->ptp_clock_lock);
	spin_lock_init(&sparx5->ptp_ts_id_lock);
	mutex_init(&sparx5->ptp_lock);

	/* Disable master counters */
	spx5_wr(PTP_PTP_DOM_CFG_PTP_ENA_SET(0), sparx5, PTP_PTP_DOM_CFG);

	/* Configure the nominal TOD increment per clock cycle */
	spx5_rmw(PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS_SET(0x7),
		 PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS,
		 sparx5, PTP_PTP_DOM_CFG);

	for (i = 0; i < SPARX5_PHC_COUNT; ++i) {
		spx5_wr((u32)tod_adj & 0xFFFFFFFF, sparx5,
			PTP_CLK_PER_CFG(i, 0));
		spx5_wr((u32)(tod_adj >> 32), sparx5,
			PTP_CLK_PER_CFG(i, 1));
	}

	spx5_rmw(PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS_SET(0),
		 PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS,
		 sparx5, PTP_PTP_DOM_CFG);

	/* Enable master counters */
	spx5_wr(PTP_PTP_DOM_CFG_PTP_ENA_SET(0x7), sparx5, PTP_PTP_DOM_CFG);

	for (i = 0; i < SPX5_PORTS; i++) {
		port = sparx5->ports[i];
		if (!port)
			continue;

		skb_queue_head_init(&port->tx_skbs);
	}

	return 0;
}

void sparx5_ptp_deinit(struct sparx5 *sparx5)
{
	struct sparx5_port *port;
	int i;

	for (i = 0; i < SPX5_PORTS; i++) {
		port = sparx5->ports[i];
		if (!port)
			continue;

		skb_queue_purge(&port->tx_skbs);
	}

	for (i = 0; i < SPARX5_PHC_COUNT; ++i)
		ptp_clock_unregister(sparx5->phc[i].clock);
}

void sparx5_ptp_rxtstamp(struct sparx5 *sparx5, struct sk_buff *skb,
			 u64 timestamp)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct sparx5_phc *phc;
	struct timespec64 ts;
	u64 full_ts_in_ns;

	if (!sparx5->ptp)
		return;

	phc = &sparx5->phc[SPARX5_PHC_PORT];
	sparx5_ptp_gettime64(&phc->info, &ts);

	if (ts.tv_nsec < timestamp)
		ts.tv_sec--;
	ts.tv_nsec = timestamp;
	full_ts_in_ns = ktime_set(ts.tv_sec, ts.tv_nsec);

	shhwtstamps = skb_hwtstamps(skb);
	shhwtstamps->hwtstamp = full_ts_in_ns;
}
