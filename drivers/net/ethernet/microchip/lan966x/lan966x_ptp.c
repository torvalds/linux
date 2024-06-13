// SPDX-License-Identifier: GPL-2.0+

#include <linux/ptp_classify.h>

#include "lan966x_main.h"

#define LAN966X_MAX_PTP_ID	512

/* Represents 1ppm adjustment in 2^59 format with 6.037735849ns as reference
 * The value is calculated as following: (1/1000000)/((2^-59)/6.037735849)
 */
#define LAN966X_1PPM_FORMAT		3480517749723LL

/* Represents 1ppb adjustment in 2^29 format with 6.037735849ns as reference
 * The value is calculated as following: (1/1000000000)/((2^59)/6.037735849)
 */
#define LAN966X_1PPB_FORMAT		3480517749LL

#define TOD_ACC_PIN		0x7

enum {
	PTP_PIN_ACTION_IDLE = 0,
	PTP_PIN_ACTION_LOAD,
	PTP_PIN_ACTION_SAVE,
	PTP_PIN_ACTION_CLOCK,
	PTP_PIN_ACTION_DELTA,
	PTP_PIN_ACTION_TOD
};

static u64 lan966x_ptp_get_nominal_value(void)
{
	/* This is the default value that for each system clock, the time of day
	 * is increased. It has the format 5.59 nanosecond.
	 */
	return 0x304d4873ecade305;
}

int lan966x_ptp_hwtstamp_set(struct lan966x_port *port, struct ifreq *ifr)
{
	struct lan966x *lan966x = port->lan966x;
	struct hwtstamp_config cfg;
	struct lan966x_phc *phc;

	/* For now don't allow to run ptp on ports that are part of a bridge,
	 * because in case of transparent clock the HW will still forward the
	 * frames, so there would be duplicate frames
	 */
	if (lan966x->bridge_mask & BIT(port->chip_port))
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
	mutex_lock(&lan966x->ptp_lock);
	phc = &lan966x->phc[LAN966X_PHC_PORT];
	memcpy(&phc->hwtstamp_config, &cfg, sizeof(cfg));
	mutex_unlock(&lan966x->ptp_lock);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

int lan966x_ptp_hwtstamp_get(struct lan966x_port *port, struct ifreq *ifr)
{
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_phc *phc;

	phc = &lan966x->phc[LAN966X_PHC_PORT];
	return copy_to_user(ifr->ifr_data, &phc->hwtstamp_config,
			    sizeof(phc->hwtstamp_config)) ? -EFAULT : 0;
}

static int lan966x_ptp_classify(struct lan966x_port *port, struct sk_buff *skb)
{
	struct ptp_header *header;
	u8 msgtype;
	int type;

	if (port->ptp_cmd == IFH_REW_OP_NOOP)
		return IFH_REW_OP_NOOP;

	type = ptp_classify_raw(skb);
	if (type == PTP_CLASS_NONE)
		return IFH_REW_OP_NOOP;

	header = ptp_parse_header(skb, type);
	if (!header)
		return IFH_REW_OP_NOOP;

	if (port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP)
		return IFH_REW_OP_TWO_STEP_PTP;

	/* If it is sync and run 1 step then set the correct operation,
	 * otherwise run as 2 step
	 */
	msgtype = ptp_get_msgtype(header, type);
	if ((msgtype & 0xf) == 0)
		return IFH_REW_OP_ONE_STEP_PTP;

	return IFH_REW_OP_TWO_STEP_PTP;
}

static void lan966x_ptp_txtstamp_old_release(struct lan966x_port *port)
{
	struct sk_buff *skb, *skb_tmp;
	unsigned long flags;

	spin_lock_irqsave(&port->tx_skbs.lock, flags);
	skb_queue_walk_safe(&port->tx_skbs, skb, skb_tmp) {
		if time_after(LAN966X_SKB_CB(skb)->jiffies + LAN966X_PTP_TIMEOUT,
			      jiffies)
			break;

		__skb_unlink(skb, &port->tx_skbs);
		dev_kfree_skb_any(skb);
	}
	spin_unlock_irqrestore(&port->tx_skbs.lock, flags);
}

int lan966x_ptp_txtstamp_request(struct lan966x_port *port,
				 struct sk_buff *skb)
{
	struct lan966x *lan966x = port->lan966x;
	unsigned long flags;
	u8 rew_op;

	rew_op = lan966x_ptp_classify(port, skb);
	LAN966X_SKB_CB(skb)->rew_op = rew_op;

	if (rew_op != IFH_REW_OP_TWO_STEP_PTP)
		return 0;

	lan966x_ptp_txtstamp_old_release(port);

	spin_lock_irqsave(&lan966x->ptp_ts_id_lock, flags);
	if (lan966x->ptp_skbs == LAN966X_MAX_PTP_ID) {
		spin_unlock_irqrestore(&lan966x->ptp_ts_id_lock, flags);
		return -EBUSY;
	}

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	skb_queue_tail(&port->tx_skbs, skb);
	LAN966X_SKB_CB(skb)->ts_id = port->ts_id;
	LAN966X_SKB_CB(skb)->jiffies = jiffies;

	lan966x->ptp_skbs++;
	port->ts_id++;
	if (port->ts_id == LAN966X_MAX_PTP_ID)
		port->ts_id = 0;

	spin_unlock_irqrestore(&lan966x->ptp_ts_id_lock, flags);

	return 0;
}

void lan966x_ptp_txtstamp_release(struct lan966x_port *port,
				  struct sk_buff *skb)
{
	struct lan966x *lan966x = port->lan966x;
	unsigned long flags;

	spin_lock_irqsave(&lan966x->ptp_ts_id_lock, flags);
	port->ts_id--;
	lan966x->ptp_skbs--;
	skb_unlink(skb, &port->tx_skbs);
	spin_unlock_irqrestore(&lan966x->ptp_ts_id_lock, flags);
}

static void lan966x_get_hwtimestamp(struct lan966x *lan966x,
				    struct timespec64 *ts,
				    u32 nsec)
{
	/* Read current PTP time to get seconds */
	unsigned long flags;
	u32 curr_nsec;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_SAVE) |
		PTP_PIN_CFG_PIN_DOM_SET(LAN966X_PHC_PORT) |
		PTP_PIN_CFG_PIN_SYNC_SET(0),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SYNC,
		lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

	ts->tv_sec = lan_rd(lan966x, PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	curr_nsec = lan_rd(lan966x, PTP_TOD_NSEC(TOD_ACC_PIN));

	ts->tv_nsec = nsec;

	/* Sec has incremented since the ts was registered */
	if (curr_nsec < nsec)
		ts->tv_sec--;

	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);
}

irqreturn_t lan966x_ptp_irq_handler(int irq, void *args)
{
	int budget = LAN966X_MAX_PTP_ID;
	struct lan966x *lan966x = args;

	while (budget--) {
		struct sk_buff *skb, *skb_tmp, *skb_match = NULL;
		struct skb_shared_hwtstamps shhwtstamps;
		struct lan966x_port *port;
		struct timespec64 ts;
		unsigned long flags;
		u32 val, id, txport;
		u32 delay;

		val = lan_rd(lan966x, PTP_TWOSTEP_CTRL);

		/* Check if a timestamp can be retrieved */
		if (!(val & PTP_TWOSTEP_CTRL_VLD))
			break;

		WARN_ON(val & PTP_TWOSTEP_CTRL_OVFL);

		if (!(val & PTP_TWOSTEP_CTRL_STAMP_TX))
			continue;

		/* Retrieve the ts Tx port */
		txport = PTP_TWOSTEP_CTRL_STAMP_PORT_GET(val);

		/* Retrieve its associated skb */
		port = lan966x->ports[txport];

		/* Retrieve the delay */
		delay = lan_rd(lan966x, PTP_TWOSTEP_STAMP);
		delay = PTP_TWOSTEP_STAMP_STAMP_NSEC_GET(delay);

		/* Get next timestamp from fifo, which needs to be the
		 * rx timestamp which represents the id of the frame
		 */
		lan_rmw(PTP_TWOSTEP_CTRL_NXT_SET(1),
			PTP_TWOSTEP_CTRL_NXT,
			lan966x, PTP_TWOSTEP_CTRL);

		val = lan_rd(lan966x, PTP_TWOSTEP_CTRL);

		/* Check if a timestamp can be retried */
		if (!(val & PTP_TWOSTEP_CTRL_VLD))
			break;

		/* Read RX timestamping to get the ID */
		id = lan_rd(lan966x, PTP_TWOSTEP_STAMP);

		spin_lock_irqsave(&port->tx_skbs.lock, flags);
		skb_queue_walk_safe(&port->tx_skbs, skb, skb_tmp) {
			if (LAN966X_SKB_CB(skb)->ts_id != id)
				continue;

			__skb_unlink(skb, &port->tx_skbs);
			skb_match = skb;
			break;
		}
		spin_unlock_irqrestore(&port->tx_skbs.lock, flags);

		/* Next ts */
		lan_rmw(PTP_TWOSTEP_CTRL_NXT_SET(1),
			PTP_TWOSTEP_CTRL_NXT,
			lan966x, PTP_TWOSTEP_CTRL);

		if (WARN_ON(!skb_match))
			continue;

		spin_lock_irqsave(&lan966x->ptp_ts_id_lock, flags);
		lan966x->ptp_skbs--;
		spin_unlock_irqrestore(&lan966x->ptp_ts_id_lock, flags);

		/* Get the h/w timestamp */
		lan966x_get_hwtimestamp(lan966x, &ts, delay);

		/* Set the timestamp into the skb */
		shhwtstamps.hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
		skb_tstamp_tx(skb_match, &shhwtstamps);

		dev_kfree_skb_any(skb_match);
	}

	return IRQ_HANDLED;
}

irqreturn_t lan966x_ptp_ext_irq_handler(int irq, void *args)
{
	struct lan966x *lan966x = args;
	struct lan966x_phc *phc;
	unsigned long flags;
	u64 time = 0;
	time64_t s;
	int pin, i;
	s64 ns;

	if (!(lan_rd(lan966x, PTP_PIN_INTR)))
		return IRQ_NONE;

	/* Go through all domains and see which pin generated the interrupt */
	for (i = 0; i < LAN966X_PHC_COUNT; ++i) {
		struct ptp_clock_event ptp_event = {0};

		phc = &lan966x->phc[i];
		pin = ptp_find_pin_unlocked(phc->clock, PTP_PF_EXTTS, 0);
		if (pin == -1)
			continue;

		if (!(lan_rd(lan966x, PTP_PIN_INTR) & BIT(pin)))
			continue;

		spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

		/* Enable to get the new interrupt.
		 * By writing 1 it clears the bit
		 */
		lan_wr(BIT(pin), lan966x, PTP_PIN_INTR);

		/* Get current time */
		s = lan_rd(lan966x, PTP_TOD_SEC_MSB(pin));
		s <<= 32;
		s |= lan_rd(lan966x, PTP_TOD_SEC_LSB(pin));
		ns = lan_rd(lan966x, PTP_TOD_NSEC(pin));
		ns &= PTP_TOD_NSEC_TOD_NSEC;

		spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

		if ((ns & 0xFFFFFFF0) == 0x3FFFFFF0) {
			s--;
			ns &= 0xf;
			ns += 999999984;
		}
		time = ktime_set(s, ns);

		ptp_event.index = pin;
		ptp_event.timestamp = time;
		ptp_event.type = PTP_CLOCK_EXTTS;
		ptp_clock_event(phc->clock, &ptp_event);
	}

	return IRQ_HANDLED;
}

static int lan966x_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
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

	tod_inc = lan966x_ptp_get_nominal_value();

	/* The multiplication is split in 2 separate additions because of
	 * overflow issues. If scaled_ppm with 16bit fractional part was bigger
	 * than 20ppm then we got overflow.
	 */
	ref = LAN966X_1PPM_FORMAT * (scaled_ppm >> 16);
	ref += (LAN966X_1PPM_FORMAT * (0xffff & scaled_ppm)) >> 16;
	tod_inc = neg_adj ? tod_inc - ref : tod_inc + ref;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

	lan_rmw(PTP_DOM_CFG_CLKCFG_DIS_SET(1 << BIT(phc->index)),
		PTP_DOM_CFG_CLKCFG_DIS,
		lan966x, PTP_DOM_CFG);

	lan_wr((u32)tod_inc & 0xFFFFFFFF, lan966x,
	       PTP_CLK_PER_CFG(phc->index, 0));
	lan_wr((u32)(tod_inc >> 32), lan966x,
	       PTP_CLK_PER_CFG(phc->index, 1));

	lan_rmw(PTP_DOM_CFG_CLKCFG_DIS_SET(0),
		PTP_DOM_CFG_CLKCFG_DIS,
		lan966x, PTP_DOM_CFG);

	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

	return 0;
}

static int lan966x_ptp_settime64(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
	unsigned long flags;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

	/* Must be in IDLE mode before the time can be loaded */
	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
		PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
		PTP_PIN_CFG_PIN_SYNC_SET(0),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SYNC,
		lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

	/* Set new value */
	lan_wr(PTP_TOD_SEC_MSB_TOD_SEC_MSB_SET(upper_32_bits(ts->tv_sec)),
	       lan966x, PTP_TOD_SEC_MSB(TOD_ACC_PIN));
	lan_wr(lower_32_bits(ts->tv_sec),
	       lan966x, PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	lan_wr(ts->tv_nsec, lan966x, PTP_TOD_NSEC(TOD_ACC_PIN));

	/* Apply new values */
	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_LOAD) |
		PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
		PTP_PIN_CFG_PIN_SYNC_SET(0),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SYNC,
		lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

	return 0;
}

int lan966x_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
	unsigned long flags;
	time64_t s;
	s64 ns;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_SAVE) |
		PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
		PTP_PIN_CFG_PIN_SYNC_SET(0),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SYNC,
		lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

	s = lan_rd(lan966x, PTP_TOD_SEC_MSB(TOD_ACC_PIN));
	s <<= 32;
	s |= lan_rd(lan966x, PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	ns = lan_rd(lan966x, PTP_TOD_NSEC(TOD_ACC_PIN));
	ns &= PTP_TOD_NSEC_TOD_NSEC;

	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

	/* Deal with negative values */
	if ((ns & 0xFFFFFFF0) == 0x3FFFFFF0) {
		s--;
		ns &= 0xf;
		ns += 999999984;
	}

	set_normalized_timespec64(ts, s, ns);
	return 0;
}

static int lan966x_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;

	if (delta > -(NSEC_PER_SEC / 2) && delta < (NSEC_PER_SEC / 2)) {
		unsigned long flags;

		spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

		/* Must be in IDLE mode before the time can be loaded */
		lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
			PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
			PTP_PIN_CFG_PIN_SYNC_SET(0),
			PTP_PIN_CFG_PIN_ACTION |
			PTP_PIN_CFG_PIN_DOM |
			PTP_PIN_CFG_PIN_SYNC,
			lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

		lan_wr(PTP_TOD_NSEC_TOD_NSEC_SET(delta),
		       lan966x, PTP_TOD_NSEC(TOD_ACC_PIN));

		/* Adjust time with the value of PTP_TOD_NSEC */
		lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_DELTA) |
			PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
			PTP_PIN_CFG_PIN_SYNC_SET(0),
			PTP_PIN_CFG_PIN_ACTION |
			PTP_PIN_CFG_PIN_DOM |
			PTP_PIN_CFG_PIN_SYNC,
			lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

		spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);
	} else {
		/* Fall back using lan966x_ptp_settime64 which is not exact */
		struct timespec64 ts;
		u64 now;

		lan966x_ptp_gettime64(ptp, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		lan966x_ptp_settime64(ptp, &ts);
	}

	return 0;
}

static int lan966x_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
			      enum ptp_pin_function func, unsigned int chan)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
	struct ptp_clock_info *info;
	int i;

	/* Currently support only 1 channel */
	if (chan != 0)
		return -1;

	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
	case PTP_PF_EXTTS:
		break;
	default:
		return -1;
	}

	/* The PTP pins are shared by all the PHC. So it is required to see if
	 * the pin is connected to another PHC. The pin is connected to another
	 * PHC if that pin already has a function on that PHC.
	 */
	for (i = 0; i < LAN966X_PHC_COUNT; ++i) {
		info = &lan966x->phc[i].info;

		/* Ignore the check with ourself */
		if (ptp == info)
			continue;

		if (info->pin_config[pin].func == PTP_PF_PEROUT ||
		    info->pin_config[pin].func == PTP_PF_EXTTS)
			return -1;
	}

	return 0;
}

static int lan966x_ptp_perout(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *rq, int on)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
	struct timespec64 ts_phase, ts_period;
	unsigned long flags;
	s64 wf_high, wf_low;
	bool pps = false;
	int pin;

	if (rq->perout.flags & ~(PTP_PEROUT_DUTY_CYCLE |
				 PTP_PEROUT_PHASE))
		return -EOPNOTSUPP;

	pin = ptp_find_pin(phc->clock, PTP_PF_PEROUT, rq->perout.index);
	if (pin == -1 || pin >= LAN966X_PHC_PINS_NUM)
		return -EINVAL;

	if (!on) {
		spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);
		lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
			PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
			PTP_PIN_CFG_PIN_SYNC_SET(0),
			PTP_PIN_CFG_PIN_ACTION |
			PTP_PIN_CFG_PIN_DOM |
			PTP_PIN_CFG_PIN_SYNC,
			lan966x, PTP_PIN_CFG(pin));
		spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);
		return 0;
	}

	if (rq->perout.period.sec == 1 &&
	    rq->perout.period.nsec == 0)
		pps = true;

	if (rq->perout.flags & PTP_PEROUT_PHASE) {
		ts_phase.tv_sec = rq->perout.phase.sec;
		ts_phase.tv_nsec = rq->perout.phase.nsec;
	} else {
		ts_phase.tv_sec = rq->perout.start.sec;
		ts_phase.tv_nsec = rq->perout.start.nsec;
	}

	if (ts_phase.tv_sec || (ts_phase.tv_nsec && !pps)) {
		dev_warn(lan966x->dev,
			 "Absolute time not supported!\n");
		return -EINVAL;
	}

	if (rq->perout.flags & PTP_PEROUT_DUTY_CYCLE) {
		struct timespec64 ts_on;

		ts_on.tv_sec = rq->perout.on.sec;
		ts_on.tv_nsec = rq->perout.on.nsec;

		wf_high = timespec64_to_ns(&ts_on);
	} else {
		wf_high = 5000;
	}

	if (pps) {
		spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);
		lan_wr(PTP_WF_LOW_PERIOD_PIN_WFL(ts_phase.tv_nsec),
		       lan966x, PTP_WF_LOW_PERIOD(pin));
		lan_wr(PTP_WF_HIGH_PERIOD_PIN_WFH(wf_high),
		       lan966x, PTP_WF_HIGH_PERIOD(pin));
		lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_CLOCK) |
			PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
			PTP_PIN_CFG_PIN_SYNC_SET(3),
			PTP_PIN_CFG_PIN_ACTION |
			PTP_PIN_CFG_PIN_DOM |
			PTP_PIN_CFG_PIN_SYNC,
			lan966x, PTP_PIN_CFG(pin));
		spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);
		return 0;
	}

	ts_period.tv_sec = rq->perout.period.sec;
	ts_period.tv_nsec = rq->perout.period.nsec;

	wf_low = timespec64_to_ns(&ts_period);
	wf_low -= wf_high;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);
	lan_wr(PTP_WF_LOW_PERIOD_PIN_WFL(wf_low),
	       lan966x, PTP_WF_LOW_PERIOD(pin));
	lan_wr(PTP_WF_HIGH_PERIOD_PIN_WFH(wf_high),
	       lan966x, PTP_WF_HIGH_PERIOD(pin));
	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_CLOCK) |
		PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
		PTP_PIN_CFG_PIN_SYNC_SET(0),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SYNC,
		lan966x, PTP_PIN_CFG(pin));
	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

	return 0;
}

static int lan966x_ptp_extts(struct ptp_clock_info *ptp,
			     struct ptp_clock_request *rq, int on)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
	unsigned long flags;
	int pin;
	u32 val;

	if (lan966x->ptp_ext_irq <= 0)
		return -EOPNOTSUPP;

	/* Reject requests with unsupported flags */
	if (rq->extts.flags & ~(PTP_ENABLE_FEATURE |
				PTP_RISING_EDGE |
				PTP_STRICT_FLAGS))
		return -EOPNOTSUPP;

	pin = ptp_find_pin(phc->clock, PTP_PF_EXTTS, rq->extts.index);
	if (pin == -1 || pin >= LAN966X_PHC_PINS_NUM)
		return -EINVAL;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);
	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_SAVE) |
		PTP_PIN_CFG_PIN_SYNC_SET(on ? 3 : 0) |
		PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
		PTP_PIN_CFG_PIN_SELECT_SET(pin),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_SYNC |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SELECT,
		lan966x, PTP_PIN_CFG(pin));

	val = lan_rd(lan966x, PTP_PIN_INTR_ENA);
	if (on)
		val |= BIT(pin);
	else
		val &= ~BIT(pin);
	lan_wr(val, lan966x, PTP_PIN_INTR_ENA);

	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

	return 0;
}

static int lan966x_ptp_enable(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *rq, int on)
{
	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		return lan966x_ptp_perout(ptp, rq, on);
	case PTP_CLK_REQ_EXTTS:
		return lan966x_ptp_extts(ptp, rq, on);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct ptp_clock_info lan966x_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "lan966x ptp",
	.max_adj	= 200000,
	.gettime64	= lan966x_ptp_gettime64,
	.settime64	= lan966x_ptp_settime64,
	.adjtime	= lan966x_ptp_adjtime,
	.adjfine	= lan966x_ptp_adjfine,
	.verify		= lan966x_ptp_verify,
	.enable		= lan966x_ptp_enable,
	.n_per_out	= LAN966X_PHC_PINS_NUM,
	.n_ext_ts	= LAN966X_PHC_PINS_NUM,
	.n_pins		= LAN966X_PHC_PINS_NUM,
};

static int lan966x_ptp_phc_init(struct lan966x *lan966x,
				int index,
				struct ptp_clock_info *clock_info)
{
	struct lan966x_phc *phc = &lan966x->phc[index];
	struct ptp_pin_desc *p;
	int i;

	for (i = 0; i < LAN966X_PHC_PINS_NUM; i++) {
		p = &phc->pins[i];

		snprintf(p->name, sizeof(p->name), "pin%d", i);
		p->index = i;
		p->func = PTP_PF_NONE;
	}

	phc->info = *clock_info;
	phc->info.pin_config = &phc->pins[0];
	phc->clock = ptp_clock_register(&phc->info, lan966x->dev);
	if (IS_ERR(phc->clock))
		return PTR_ERR(phc->clock);

	phc->index = index;
	phc->lan966x = lan966x;

	/* PTP Rx stamping is always enabled.  */
	phc->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;

	return 0;
}

int lan966x_ptp_init(struct lan966x *lan966x)
{
	u64 tod_adj = lan966x_ptp_get_nominal_value();
	struct lan966x_port *port;
	int err, i;

	if (!lan966x->ptp)
		return 0;

	for (i = 0; i < LAN966X_PHC_COUNT; ++i) {
		err = lan966x_ptp_phc_init(lan966x, i, &lan966x_ptp_clock_info);
		if (err)
			return err;
	}

	spin_lock_init(&lan966x->ptp_clock_lock);
	spin_lock_init(&lan966x->ptp_ts_id_lock);
	mutex_init(&lan966x->ptp_lock);

	/* Disable master counters */
	lan_wr(PTP_DOM_CFG_ENA_SET(0), lan966x, PTP_DOM_CFG);

	/* Configure the nominal TOD increment per clock cycle */
	lan_rmw(PTP_DOM_CFG_CLKCFG_DIS_SET(0x7),
		PTP_DOM_CFG_CLKCFG_DIS,
		lan966x, PTP_DOM_CFG);

	for (i = 0; i < LAN966X_PHC_COUNT; ++i) {
		lan_wr((u32)tod_adj & 0xFFFFFFFF, lan966x,
		       PTP_CLK_PER_CFG(i, 0));
		lan_wr((u32)(tod_adj >> 32), lan966x,
		       PTP_CLK_PER_CFG(i, 1));
	}

	lan_rmw(PTP_DOM_CFG_CLKCFG_DIS_SET(0),
		PTP_DOM_CFG_CLKCFG_DIS,
		lan966x, PTP_DOM_CFG);

	/* Enable master counters */
	lan_wr(PTP_DOM_CFG_ENA_SET(0x7), lan966x, PTP_DOM_CFG);

	for (i = 0; i < lan966x->num_phys_ports; i++) {
		port = lan966x->ports[i];
		if (!port)
			continue;

		skb_queue_head_init(&port->tx_skbs);
	}

	return 0;
}

void lan966x_ptp_deinit(struct lan966x *lan966x)
{
	struct lan966x_port *port;
	int i;

	if (!lan966x->ptp)
		return;

	for (i = 0; i < lan966x->num_phys_ports; i++) {
		port = lan966x->ports[i];
		if (!port)
			continue;

		skb_queue_purge(&port->tx_skbs);
	}

	for (i = 0; i < LAN966X_PHC_COUNT; ++i)
		ptp_clock_unregister(lan966x->phc[i].clock);
}

void lan966x_ptp_rxtstamp(struct lan966x *lan966x, struct sk_buff *skb,
			  u64 timestamp)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct lan966x_phc *phc;
	struct timespec64 ts;
	u64 full_ts_in_ns;

	if (!lan966x->ptp)
		return;

	phc = &lan966x->phc[LAN966X_PHC_PORT];
	lan966x_ptp_gettime64(&phc->info, &ts);

	/* Drop the sub-ns precision */
	timestamp = timestamp >> 2;
	if (ts.tv_nsec < timestamp)
		ts.tv_sec--;
	ts.tv_nsec = timestamp;
	full_ts_in_ns = ktime_set(ts.tv_sec, ts.tv_nsec);

	shhwtstamps = skb_hwtstamps(skb);
	shhwtstamps->hwtstamp = full_ts_in_ns;
}

u32 lan966x_ptp_get_period_ps(void)
{
	/* This represents the system clock period in picoseconds */
	return 15125;
}
