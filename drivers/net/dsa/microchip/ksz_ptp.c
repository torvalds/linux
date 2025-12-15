// SPDX-License-Identifier: GPL-2.0
/* Microchip KSZ PTP Implementation
 *
 * Copyright (C) 2020 ARRI Lighting
 * Copyright (C) 2022 Microchip Technology Inc.
 */

#include <linux/dsa/ksz_common.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>

#include "ksz_common.h"
#include "ksz_ptp.h"
#include "ksz_ptp_reg.h"

#define ptp_caps_to_data(d) container_of((d), struct ksz_ptp_data, caps)
#define ptp_data_to_ksz_dev(d) container_of((d), struct ksz_device, ptp_data)
#define work_to_xmit_work(w) \
		container_of((w), struct ksz_deferred_xmit_work, work)

/* Sub-nanoseconds-adj,max * sub-nanoseconds / 40ns * 1ns
 * = (2^30-1) * (2 ^ 32) / 40 ns * 1 ns = 6249999
 */
#define KSZ_MAX_DRIFT_CORR 6249999
#define KSZ_MAX_PULSE_WIDTH 125000000LL

#define KSZ_PTP_INC_NS 40ULL  /* HW clock is incremented every 40 ns (by 40) */
#define KSZ_PTP_SUBNS_BITS 32

#define KSZ_PTP_INT_START 13

static int ksz_ptp_tou_gpio(struct ksz_device *dev)
{
	int ret;

	if (!is_lan937x(dev))
		return 0;

	ret = ksz_rmw32(dev, REG_PTP_CTRL_STAT__4, GPIO_OUT,
			GPIO_OUT);
	if (ret)
		return ret;

	ret = ksz_rmw32(dev, REG_SW_GLOBAL_LED_OVR__4, LED_OVR_1 | LED_OVR_2,
			LED_OVR_1 | LED_OVR_2);
	if (ret)
		return ret;

	return ksz_rmw32(dev, REG_SW_GLOBAL_LED_SRC__4,
			 LED_SRC_PTP_GPIO_1 | LED_SRC_PTP_GPIO_2,
			 LED_SRC_PTP_GPIO_1 | LED_SRC_PTP_GPIO_2);
}

static int ksz_ptp_tou_reset(struct ksz_device *dev, u8 unit)
{
	u32 data;
	int ret;

	/* Reset trigger unit (clears TRIGGER_EN, but not GPIOSTATx) */
	ret = ksz_rmw32(dev, REG_PTP_CTRL_STAT__4, TRIG_RESET, TRIG_RESET);

	data = FIELD_PREP(TRIG_DONE_M, BIT(unit));
	ret = ksz_write32(dev, REG_PTP_TRIG_STATUS__4, data);
	if (ret)
		return ret;

	data = FIELD_PREP(TRIG_INT_M, BIT(unit));
	ret = ksz_write32(dev, REG_PTP_INT_STATUS__4, data);
	if (ret)
		return ret;

	/* Clear reset and set GPIO direction */
	return ksz_rmw32(dev, REG_PTP_CTRL_STAT__4, (TRIG_RESET | TRIG_ENABLE),
			 0);
}

static int ksz_ptp_tou_pulse_verify(u64 pulse_ns)
{
	u32 data;

	if (pulse_ns & 0x3)
		return -EINVAL;

	data = (pulse_ns / 8);
	if (!FIELD_FIT(TRIG_PULSE_WIDTH_M, data))
		return -ERANGE;

	return 0;
}

static int ksz_ptp_tou_target_time_set(struct ksz_device *dev,
				       struct timespec64 const *ts)
{
	int ret;

	/* Hardware has only 32 bit */
	if ((ts->tv_sec & 0xffffffff) != ts->tv_sec)
		return -EINVAL;

	ret = ksz_write32(dev, REG_TRIG_TARGET_NANOSEC, ts->tv_nsec);
	if (ret)
		return ret;

	ret = ksz_write32(dev, REG_TRIG_TARGET_SEC, ts->tv_sec);
	if (ret)
		return ret;

	return 0;
}

static int ksz_ptp_tou_start(struct ksz_device *dev, u8 unit)
{
	u32 data;
	int ret;

	ret = ksz_rmw32(dev, REG_PTP_CTRL_STAT__4, TRIG_ENABLE, TRIG_ENABLE);
	if (ret)
		return ret;

	/* Check error flag:
	 * - the ACTIVE flag is NOT cleared an error!
	 */
	ret = ksz_read32(dev, REG_PTP_TRIG_STATUS__4, &data);
	if (ret)
		return ret;

	if (FIELD_GET(TRIG_ERROR_M, data) & (1 << unit)) {
		dev_err(dev->dev, "%s: Trigger unit%d error!\n", __func__,
			unit);
		ret = -EIO;
		/* Unit will be reset on next access */
		return ret;
	}

	return 0;
}

static int ksz_ptp_configure_perout(struct ksz_device *dev,
				    u32 cycle_width_ns, u32 pulse_width_ns,
				    struct timespec64 const *target_time,
				    u8 index)
{
	u32 data;
	int ret;

	data = FIELD_PREP(TRIG_NOTIFY, 1) |
		FIELD_PREP(TRIG_GPO_M, index) |
		FIELD_PREP(TRIG_PATTERN_M, TRIG_POS_PERIOD);
	ret = ksz_write32(dev, REG_TRIG_CTRL__4, data);
	if (ret)
		return ret;

	ret = ksz_write32(dev, REG_TRIG_CYCLE_WIDTH, cycle_width_ns);
	if (ret)
		return ret;

	/* Set cycle count 0 - Infinite */
	ret = ksz_rmw32(dev, REG_TRIG_CYCLE_CNT, TRIG_CYCLE_CNT_M, 0);
	if (ret)
		return ret;

	data = (pulse_width_ns / 8);
	ret = ksz_write32(dev, REG_TRIG_PULSE_WIDTH__4, data);
	if (ret)
		return ret;

	ret = ksz_ptp_tou_target_time_set(dev, target_time);
	if (ret)
		return ret;

	return 0;
}

static int ksz_ptp_enable_perout(struct ksz_device *dev,
				 struct ptp_perout_request const *request,
				 int on)
{
	struct ksz_ptp_data *ptp_data = &dev->ptp_data;
	u64 req_pulse_width_ns;
	u64 cycle_width_ns;
	u64 pulse_width_ns;
	int pin = 0;
	u32 data32;
	int ret;

	if (request->flags & ~PTP_PEROUT_DUTY_CYCLE)
		return -EOPNOTSUPP;

	if (ptp_data->tou_mode != KSZ_PTP_TOU_PEROUT &&
	    ptp_data->tou_mode != KSZ_PTP_TOU_IDLE)
		return -EBUSY;

	pin = ptp_find_pin(ptp_data->clock, PTP_PF_PEROUT, request->index);
	if (pin < 0)
		return -EINVAL;

	data32 = FIELD_PREP(PTP_GPIO_INDEX, pin) |
		 FIELD_PREP(PTP_TOU_INDEX, request->index);
	ret = ksz_rmw32(dev, REG_PTP_UNIT_INDEX__4,
			PTP_GPIO_INDEX | PTP_TOU_INDEX, data32);
	if (ret)
		return ret;

	ret = ksz_ptp_tou_reset(dev, request->index);
	if (ret)
		return ret;

	if (!on) {
		ptp_data->tou_mode = KSZ_PTP_TOU_IDLE;
		return 0;
	}

	ptp_data->perout_target_time_first.tv_sec  = request->start.sec;
	ptp_data->perout_target_time_first.tv_nsec = request->start.nsec;

	ptp_data->perout_period.tv_sec = request->period.sec;
	ptp_data->perout_period.tv_nsec = request->period.nsec;

	cycle_width_ns = timespec64_to_ns(&ptp_data->perout_period);
	if ((cycle_width_ns & TRIG_CYCLE_WIDTH_M) != cycle_width_ns)
		return -EINVAL;

	if (request->flags & PTP_PEROUT_DUTY_CYCLE) {
		pulse_width_ns = request->on.sec * NSEC_PER_SEC +
			request->on.nsec;
	} else {
		/* Use a duty cycle of 50%. Maximum pulse width supported by the
		 * hardware is a little bit more than 125 ms.
		 */
		req_pulse_width_ns = (request->period.sec * NSEC_PER_SEC +
				      request->period.nsec) / 2;
		pulse_width_ns = min_t(u64, req_pulse_width_ns,
				       KSZ_MAX_PULSE_WIDTH);
	}

	ret = ksz_ptp_tou_pulse_verify(pulse_width_ns);
	if (ret)
		return ret;

	ret = ksz_ptp_configure_perout(dev, cycle_width_ns, pulse_width_ns,
				       &ptp_data->perout_target_time_first,
				       pin);
	if (ret)
		return ret;

	ret = ksz_ptp_tou_gpio(dev);
	if (ret)
		return ret;

	ret = ksz_ptp_tou_start(dev, request->index);
	if (ret)
		return ret;

	ptp_data->tou_mode = KSZ_PTP_TOU_PEROUT;

	return 0;
}

static int ksz_ptp_enable_mode(struct ksz_device *dev)
{
	struct ksz_tagger_data *tagger_data = ksz_tagger_data(dev->ds);
	struct ksz_ptp_data *ptp_data = &dev->ptp_data;
	struct ksz_port *prt;
	struct dsa_port *dp;
	bool tag_en = false;

	dsa_switch_for_each_user_port(dp, dev->ds) {
		prt = &dev->ports[dp->index];
		if (prt->hwts_tx_en || prt->hwts_rx_en) {
			tag_en = true;
			break;
		}
	}

	if (tag_en) {
		ptp_schedule_worker(ptp_data->clock, 0);
	} else {
		ptp_cancel_worker_sync(ptp_data->clock);
	}

	tagger_data->hwtstamp_set_state(dev->ds, tag_en);

	return ksz_rmw16(dev, REG_PTP_MSG_CONF1, PTP_ENABLE,
			 tag_en ? PTP_ENABLE : 0);
}

/* The function is return back the capability of timestamping feature when
 * requested through ethtool -T <interface> utility
 */
int ksz_get_ts_info(struct dsa_switch *ds, int port, struct kernel_ethtool_ts_info *ts)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_ptp_data *ptp_data;

	ptp_data = &dev->ptp_data;

	if (!ptp_data->clock)
		return -ENODEV;

	ts->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
			      SOF_TIMESTAMPING_RX_HARDWARE |
			      SOF_TIMESTAMPING_RAW_HARDWARE;

	ts->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ONESTEP_P2P);

	if (is_lan937x(dev))
		ts->tx_types |= BIT(HWTSTAMP_TX_ON);

	ts->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	ts->phc_index = ptp_clock_index(ptp_data->clock);

	return 0;
}

int ksz_hwtstamp_get(struct dsa_switch *ds, int port,
		     struct kernel_hwtstamp_config *config)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *prt;

	prt = &dev->ports[port];
	*config = prt->tstamp_config;

	return 0;
}

static int ksz_set_hwtstamp_config(struct ksz_device *dev,
				   struct ksz_port *prt,
				   struct kernel_hwtstamp_config *config)
{
	int ret;

	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		prt->ptpmsg_irq[KSZ_SYNC_MSG].ts_en  = false;
		prt->ptpmsg_irq[KSZ_XDREQ_MSG].ts_en = false;
		prt->ptpmsg_irq[KSZ_PDRES_MSG].ts_en = false;
		prt->hwts_tx_en = false;
		break;
	case HWTSTAMP_TX_ONESTEP_P2P:
		prt->ptpmsg_irq[KSZ_SYNC_MSG].ts_en  = false;
		prt->ptpmsg_irq[KSZ_XDREQ_MSG].ts_en = true;
		prt->ptpmsg_irq[KSZ_PDRES_MSG].ts_en = false;
		prt->hwts_tx_en = true;

		ret = ksz_rmw16(dev, REG_PTP_MSG_CONF1, PTP_1STEP, PTP_1STEP);
		if (ret)
			return ret;

		break;
	case HWTSTAMP_TX_ON:
		if (!is_lan937x(dev))
			return -ERANGE;

		prt->ptpmsg_irq[KSZ_SYNC_MSG].ts_en  = true;
		prt->ptpmsg_irq[KSZ_XDREQ_MSG].ts_en = true;
		prt->ptpmsg_irq[KSZ_PDRES_MSG].ts_en = true;
		prt->hwts_tx_en = true;

		ret = ksz_rmw16(dev, REG_PTP_MSG_CONF1, PTP_1STEP, 0);
		if (ret)
			return ret;

		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		prt->hwts_rx_en = false;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		prt->hwts_rx_en = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		prt->hwts_rx_en = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		prt->hwts_rx_en = true;
		break;
	default:
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	return ksz_ptp_enable_mode(dev);
}

int ksz_hwtstamp_set(struct dsa_switch *ds, int port,
		     struct kernel_hwtstamp_config *config,
		     struct netlink_ext_ack *extack)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *prt;
	int ret;

	prt = &dev->ports[port];

	ret = ksz_set_hwtstamp_config(dev, prt, config);
	if (ret)
		return ret;

	prt->tstamp_config = *config;

	return 0;
}

static ktime_t ksz_tstamp_reconstruct(struct ksz_device *dev, ktime_t tstamp)
{
	struct timespec64 ptp_clock_time;
	struct ksz_ptp_data *ptp_data;
	struct timespec64 diff;
	struct timespec64 ts;

	ptp_data = &dev->ptp_data;
	ts = ktime_to_timespec64(tstamp);

	spin_lock_bh(&ptp_data->clock_lock);
	ptp_clock_time = ptp_data->clock_time;
	spin_unlock_bh(&ptp_data->clock_lock);

	/* calculate full time from partial time stamp */
	ts.tv_sec = (ptp_clock_time.tv_sec & ~3) | ts.tv_sec;

	/* find nearest possible point in time */
	diff = timespec64_sub(ts, ptp_clock_time);
	if (diff.tv_sec > 2)
		ts.tv_sec -= 4;
	else if (diff.tv_sec < -2)
		ts.tv_sec += 4;

	return timespec64_to_ktime(ts);
}

bool ksz_port_rxtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb,
		       unsigned int type)
{
	struct skb_shared_hwtstamps *hwtstamps = skb_hwtstamps(skb);
	struct ksz_device *dev = ds->priv;
	struct ptp_header *ptp_hdr;
	struct ksz_port *prt;
	u8 ptp_msg_type;
	ktime_t tstamp;
	s64 correction;

	prt = &dev->ports[port];

	tstamp = KSZ_SKB_CB(skb)->tstamp;
	memset(hwtstamps, 0, sizeof(*hwtstamps));
	hwtstamps->hwtstamp = ksz_tstamp_reconstruct(dev, tstamp);

	if (prt->tstamp_config.tx_type != HWTSTAMP_TX_ONESTEP_P2P)
		goto out;

	ptp_hdr = ptp_parse_header(skb, type);
	if (!ptp_hdr)
		goto out;

	ptp_msg_type = ptp_get_msgtype(ptp_hdr, type);
	if (ptp_msg_type != PTP_MSGTYPE_PDELAY_REQ)
		goto out;

	/* Only subtract the partial time stamp from the correction field.  When
	 * the hardware adds the egress time stamp to the correction field of
	 * the PDelay_Resp message on tx, also only the partial time stamp will
	 * be added.
	 */
	correction = (s64)get_unaligned_be64(&ptp_hdr->correction);
	correction -= ktime_to_ns(tstamp) << 16;

	ptp_header_update_correction(skb, type, ptp_hdr, correction);

out:
	return false;
}

void ksz_port_txtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb)
{
	struct ksz_device *dev = ds->priv;
	struct ptp_header *hdr;
	struct sk_buff *clone;
	struct ksz_port *prt;
	unsigned int type;
	u8 ptp_msg_type;

	prt = &dev->ports[port];

	if (!prt->hwts_tx_en)
		return;

	type = ptp_classify_raw(skb);
	if (type == PTP_CLASS_NONE)
		return;

	hdr = ptp_parse_header(skb, type);
	if (!hdr)
		return;

	ptp_msg_type = ptp_get_msgtype(hdr, type);

	switch (ptp_msg_type) {
	case PTP_MSGTYPE_SYNC:
		if (prt->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_P2P)
			return;
		break;
	case PTP_MSGTYPE_PDELAY_REQ:
		break;
	case PTP_MSGTYPE_PDELAY_RESP:
		if (prt->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_P2P) {
			KSZ_SKB_CB(skb)->ptp_type = type;
			KSZ_SKB_CB(skb)->update_correction = true;
			return;
		}
		break;

	default:
		return;
	}

	clone = skb_clone_sk(skb);
	if (!clone)
		return;

	/* caching the value to be used in tag_ksz.c */
	KSZ_SKB_CB(skb)->clone = clone;
}

static void ksz_ptp_txtstamp_skb(struct ksz_device *dev,
				 struct ksz_port *prt, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps hwtstamps = {};
	int ret;

	/* timeout must include DSA conduit to transmit data, tstamp latency,
	 * IRQ latency and time for reading the time stamp.
	 */
	ret = wait_for_completion_timeout(&prt->tstamp_msg_comp,
					  msecs_to_jiffies(100));
	if (!ret)
		return;

	hwtstamps.hwtstamp = prt->tstamp_msg;
	skb_complete_tx_timestamp(skb, &hwtstamps);
}

void ksz_port_deferred_xmit(struct kthread_work *work)
{
	struct ksz_deferred_xmit_work *xmit_work = work_to_xmit_work(work);
	struct sk_buff *clone, *skb = xmit_work->skb;
	struct dsa_switch *ds = xmit_work->dp->ds;
	struct ksz_device *dev = ds->priv;
	struct ksz_port *prt;

	prt = &dev->ports[xmit_work->dp->index];

	clone = KSZ_SKB_CB(skb)->clone;

	skb_shinfo(clone)->tx_flags |= SKBTX_IN_PROGRESS;

	reinit_completion(&prt->tstamp_msg_comp);

	dsa_enqueue_skb(skb, skb->dev);

	ksz_ptp_txtstamp_skb(dev, prt, clone);

	kfree(xmit_work);
}

static int _ksz_ptp_gettime(struct ksz_device *dev, struct timespec64 *ts)
{
	u32 nanoseconds;
	u32 seconds;
	u8 phase;
	int ret;

	/* Copy current PTP clock into shadow registers and read */
	ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_READ_TIME, PTP_READ_TIME);
	if (ret)
		return ret;

	ret = ksz_read8(dev, REG_PTP_RTC_SUB_NANOSEC__2, &phase);
	if (ret)
		return ret;

	ret = ksz_read32(dev, REG_PTP_RTC_NANOSEC, &nanoseconds);
	if (ret)
		return ret;

	ret = ksz_read32(dev, REG_PTP_RTC_SEC, &seconds);
	if (ret)
		return ret;

	ts->tv_sec = seconds;
	ts->tv_nsec = nanoseconds + phase * 8;

	return 0;
}

static int ksz_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	int ret;

	mutex_lock(&ptp_data->lock);
	ret = _ksz_ptp_gettime(dev, ts);
	mutex_unlock(&ptp_data->lock);

	return ret;
}

static int ksz_ptp_restart_perout(struct ksz_device *dev)
{
	struct ksz_ptp_data *ptp_data = &dev->ptp_data;
	s64 now_ns, first_ns, period_ns, next_ns;
	struct ptp_perout_request request;
	struct timespec64 next;
	struct timespec64 now;
	unsigned int count;
	int ret;

	dev_info(dev->dev, "Restarting periodic output signal\n");

	ret = _ksz_ptp_gettime(dev, &now);
	if (ret)
		return ret;

	now_ns = timespec64_to_ns(&now);
	first_ns = timespec64_to_ns(&ptp_data->perout_target_time_first);

	/* Calculate next perout event based on start time and period */
	period_ns = timespec64_to_ns(&ptp_data->perout_period);

	if (first_ns < now_ns) {
		count = div_u64(now_ns - first_ns, period_ns);
		next_ns = first_ns + count * period_ns;
	} else {
		next_ns = first_ns;
	}

	/* Ensure 100 ms guard time prior next event */
	while (next_ns < now_ns + 100000000)
		next_ns += period_ns;

	/* Restart periodic output signal */
	next = ns_to_timespec64(next_ns);
	request.start.sec  = next.tv_sec;
	request.start.nsec = next.tv_nsec;
	request.period.sec  = ptp_data->perout_period.tv_sec;
	request.period.nsec = ptp_data->perout_period.tv_nsec;
	request.index = 0;
	request.flags = 0;

	return ksz_ptp_enable_perout(dev, &request, 1);
}

static int ksz_ptp_settime(struct ptp_clock_info *ptp,
			   const struct timespec64 *ts)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	int ret;

	mutex_lock(&ptp_data->lock);

	/* Write to shadow registers and Load PTP clock */
	ret = ksz_write16(dev, REG_PTP_RTC_SUB_NANOSEC__2, PTP_RTC_0NS);
	if (ret)
		goto unlock;

	ret = ksz_write32(dev, REG_PTP_RTC_NANOSEC, ts->tv_nsec);
	if (ret)
		goto unlock;

	ret = ksz_write32(dev, REG_PTP_RTC_SEC, ts->tv_sec);
	if (ret)
		goto unlock;

	ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_LOAD_TIME, PTP_LOAD_TIME);
	if (ret)
		goto unlock;

	switch (ptp_data->tou_mode) {
	case KSZ_PTP_TOU_IDLE:
		break;

	case KSZ_PTP_TOU_PEROUT:
		ret = ksz_ptp_restart_perout(dev);
		if (ret)
			goto unlock;

		break;
	}

	spin_lock_bh(&ptp_data->clock_lock);
	ptp_data->clock_time = *ts;
	spin_unlock_bh(&ptp_data->clock_lock);

unlock:
	mutex_unlock(&ptp_data->lock);

	return ret;
}

static int ksz_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	u64 base, adj;
	bool negative;
	u32 data32;
	int ret;

	mutex_lock(&ptp_data->lock);

	if (scaled_ppm) {
		base = KSZ_PTP_INC_NS << KSZ_PTP_SUBNS_BITS;
		negative = diff_by_scaled_ppm(base, scaled_ppm, &adj);

		data32 = (u32)adj;
		data32 &= PTP_SUBNANOSEC_M;
		if (!negative)
			data32 |= PTP_RATE_DIR;

		ret = ksz_write32(dev, REG_PTP_SUBNANOSEC_RATE, data32);
		if (ret)
			goto unlock;

		ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_CLK_ADJ_ENABLE,
				PTP_CLK_ADJ_ENABLE);
		if (ret)
			goto unlock;
	} else {
		ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_CLK_ADJ_ENABLE, 0);
		if (ret)
			goto unlock;
	}

unlock:
	mutex_unlock(&ptp_data->lock);
	return ret;
}

static int ksz_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	struct timespec64 delta64 = ns_to_timespec64(delta);
	s32 sec, nsec;
	u16 data16;
	int ret;

	mutex_lock(&ptp_data->lock);

	/* do not use ns_to_timespec64(),
	 * both sec and nsec are subtracted by hw
	 */
	sec = div_s64_rem(delta, NSEC_PER_SEC, &nsec);

	ret = ksz_write32(dev, REG_PTP_RTC_NANOSEC, abs(nsec));
	if (ret)
		goto unlock;

	ret = ksz_write32(dev, REG_PTP_RTC_SEC, abs(sec));
	if (ret)
		goto unlock;

	ret = ksz_read16(dev, REG_PTP_CLK_CTRL, &data16);
	if (ret)
		goto unlock;

	data16 |= PTP_STEP_ADJ;

	/* PTP_STEP_DIR -- 0: subtract, 1: add */
	if (delta < 0)
		data16 &= ~PTP_STEP_DIR;
	else
		data16 |= PTP_STEP_DIR;

	ret = ksz_write16(dev, REG_PTP_CLK_CTRL, data16);
	if (ret)
		goto unlock;

	switch (ptp_data->tou_mode) {
	case KSZ_PTP_TOU_IDLE:
		break;

	case KSZ_PTP_TOU_PEROUT:
		ret = ksz_ptp_restart_perout(dev);
		if (ret)
			goto unlock;

		break;
	}

	spin_lock_bh(&ptp_data->clock_lock);
	ptp_data->clock_time = timespec64_add(ptp_data->clock_time, delta64);
	spin_unlock_bh(&ptp_data->clock_lock);

unlock:
	mutex_unlock(&ptp_data->lock);
	return ret;
}

static int ksz_ptp_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *req, int on)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	int ret;

	switch (req->type) {
	case PTP_CLK_REQ_PEROUT:
		mutex_lock(&ptp_data->lock);
		ret = ksz_ptp_enable_perout(dev, &req->perout, on);
		mutex_unlock(&ptp_data->lock);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int ksz_ptp_verify_pin(struct ptp_clock_info *ptp, unsigned int pin,
			      enum ptp_pin_function func, unsigned int chan)
{
	int ret = 0;

	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

/*  Function is pointer to the do_aux_work in the ptp_clock capability */
static long ksz_ptp_do_aux_work(struct ptp_clock_info *ptp)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	struct timespec64 ts;
	int ret;

	mutex_lock(&ptp_data->lock);
	ret = _ksz_ptp_gettime(dev, &ts);
	if (ret)
		goto out;

	spin_lock_bh(&ptp_data->clock_lock);
	ptp_data->clock_time = ts;
	spin_unlock_bh(&ptp_data->clock_lock);

out:
	mutex_unlock(&ptp_data->lock);

	return HZ;  /* reschedule in 1 second */
}

static int ksz_ptp_start_clock(struct ksz_device *dev)
{
	struct ksz_ptp_data *ptp_data = &dev->ptp_data;
	int ret;

	ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_CLK_ENABLE, PTP_CLK_ENABLE);
	if (ret)
		return ret;

	ptp_data->clock_time.tv_sec = 0;
	ptp_data->clock_time.tv_nsec = 0;

	return 0;
}

int ksz_ptp_clock_register(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_ptp_data *ptp_data;
	int ret;
	u8 i;

	ptp_data = &dev->ptp_data;
	mutex_init(&ptp_data->lock);
	spin_lock_init(&ptp_data->clock_lock);

	ptp_data->caps.owner		= THIS_MODULE;
	snprintf(ptp_data->caps.name, 16, "Microchip Clock");
	ptp_data->caps.max_adj		= KSZ_MAX_DRIFT_CORR;
	ptp_data->caps.gettime64	= ksz_ptp_gettime;
	ptp_data->caps.settime64	= ksz_ptp_settime;
	ptp_data->caps.adjfine		= ksz_ptp_adjfine;
	ptp_data->caps.adjtime		= ksz_ptp_adjtime;
	ptp_data->caps.do_aux_work	= ksz_ptp_do_aux_work;
	ptp_data->caps.enable		= ksz_ptp_enable;
	ptp_data->caps.verify		= ksz_ptp_verify_pin;
	ptp_data->caps.n_pins		= KSZ_PTP_N_GPIO;
	ptp_data->caps.n_per_out	= 3;

	ret = ksz_ptp_start_clock(dev);
	if (ret)
		return ret;

	for (i = 0; i < KSZ_PTP_N_GPIO; i++) {
		struct ptp_pin_desc *ptp_pin = &ptp_data->pin_config[i];

		snprintf(ptp_pin->name,
			 sizeof(ptp_pin->name), "ksz_ptp_pin_%02d", i);
		ptp_pin->index = i;
		ptp_pin->func = PTP_PF_NONE;
	}

	ptp_data->caps.pin_config = ptp_data->pin_config;

	/* Currently only P2P mode is supported. When 802_1AS bit is set, it
	 * forwards all PTP packets to host port and none to other ports.
	 */
	ret = ksz_rmw16(dev, REG_PTP_MSG_CONF1, PTP_TC_P2P | PTP_802_1AS,
			PTP_TC_P2P | PTP_802_1AS);
	if (ret)
		return ret;

	ptp_data->clock = ptp_clock_register(&ptp_data->caps, dev->dev);
	if (IS_ERR_OR_NULL(ptp_data->clock))
		return PTR_ERR(ptp_data->clock);

	return 0;
}

void ksz_ptp_clock_unregister(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_ptp_data *ptp_data;

	ptp_data = &dev->ptp_data;

	if (ptp_data->clock)
		ptp_clock_unregister(ptp_data->clock);
}

static irqreturn_t ksz_ptp_msg_thread_fn(int irq, void *dev_id)
{
	struct ksz_ptp_irq *ptpmsg_irq = dev_id;
	struct ksz_device *dev;
	struct ksz_port *port;
	u32 tstamp_raw;
	ktime_t tstamp;
	int ret;

	port = ptpmsg_irq->port;
	dev = port->ksz_dev;

	if (ptpmsg_irq->ts_en) {
		ret = ksz_read32(dev, ptpmsg_irq->ts_reg, &tstamp_raw);
		if (ret)
			return IRQ_NONE;

		tstamp = ksz_decode_tstamp(tstamp_raw);

		port->tstamp_msg = ksz_tstamp_reconstruct(dev, tstamp);

		complete(&port->tstamp_msg_comp);
	}

	return IRQ_HANDLED;
}

static irqreturn_t ksz_ptp_irq_thread_fn(int irq, void *dev_id)
{
	struct ksz_irq *ptpirq = dev_id;
	unsigned int nhandled = 0;
	struct ksz_device *dev;
	unsigned int sub_irq;
	u16 data;
	int ret;
	u8 n;

	dev = ptpirq->dev;

	ret = ksz_read16(dev, ptpirq->reg_status, &data);
	if (ret)
		goto out;

	/* Clear the interrupts W1C */
	ret = ksz_write16(dev, ptpirq->reg_status, data);
	if (ret)
		return IRQ_NONE;

	for (n = 0; n < ptpirq->nirqs; ++n) {
		if (data & BIT(n + KSZ_PTP_INT_START)) {
			sub_irq = irq_find_mapping(ptpirq->domain, n);
			handle_nested_irq(sub_irq);
			++nhandled;
		}
	}

out:
	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
}

static void ksz_ptp_irq_mask(struct irq_data *d)
{
	struct ksz_irq *kirq = irq_data_get_irq_chip_data(d);

	kirq->masked &= ~BIT(d->hwirq + KSZ_PTP_INT_START);
}

static void ksz_ptp_irq_unmask(struct irq_data *d)
{
	struct ksz_irq *kirq = irq_data_get_irq_chip_data(d);

	kirq->masked |= BIT(d->hwirq + KSZ_PTP_INT_START);
}

static void ksz_ptp_irq_bus_lock(struct irq_data *d)
{
	struct ksz_irq *kirq  = irq_data_get_irq_chip_data(d);

	mutex_lock(&kirq->dev->lock_irq);
}

static void ksz_ptp_irq_bus_sync_unlock(struct irq_data *d)
{
	struct ksz_irq *kirq  = irq_data_get_irq_chip_data(d);
	struct ksz_device *dev = kirq->dev;
	int ret;

	ret = ksz_write16(dev, kirq->reg_mask, kirq->masked);
	if (ret)
		dev_err(dev->dev, "failed to change IRQ mask\n");

	mutex_unlock(&dev->lock_irq);
}

static const struct irq_chip ksz_ptp_irq_chip = {
	.name			= "ksz-irq",
	.irq_mask		= ksz_ptp_irq_mask,
	.irq_unmask		= ksz_ptp_irq_unmask,
	.irq_bus_lock		= ksz_ptp_irq_bus_lock,
	.irq_bus_sync_unlock	= ksz_ptp_irq_bus_sync_unlock,
};

static int ksz_ptp_irq_domain_map(struct irq_domain *d,
				  unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, d->host_data);
	irq_set_chip_and_handler(irq, &ksz_ptp_irq_chip, handle_level_irq);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops ksz_ptp_irq_domain_ops = {
	.map	= ksz_ptp_irq_domain_map,
	.xlate	= irq_domain_xlate_twocell,
};

static void ksz_ptp_msg_irq_free(struct ksz_port *port, u8 n)
{
	struct ksz_ptp_irq *ptpmsg_irq;

	ptpmsg_irq = &port->ptpmsg_irq[n];

	free_irq(ptpmsg_irq->num, ptpmsg_irq);
	irq_dispose_mapping(ptpmsg_irq->num);
}

static int ksz_ptp_msg_irq_setup(struct ksz_port *port, u8 n)
{
	u16 ts_reg[] = {REG_PTP_PORT_PDRESP_TS, REG_PTP_PORT_XDELAY_TS,
			REG_PTP_PORT_SYNC_TS};
	static const char * const name[] = {"pdresp-msg", "xdreq-msg",
					    "sync-msg"};
	const struct ksz_dev_ops *ops = port->ksz_dev->dev_ops;
	struct ksz_irq *ptpirq = &port->ptpirq;
	struct ksz_ptp_irq *ptpmsg_irq;

	ptpmsg_irq = &port->ptpmsg_irq[n];
	ptpmsg_irq->num = irq_create_mapping(ptpirq->domain, n);
	if (!ptpmsg_irq->num)
		return -EINVAL;

	ptpmsg_irq->port = port;
	ptpmsg_irq->ts_reg = ops->get_port_addr(port->num, ts_reg[n]);

	strscpy(ptpmsg_irq->name, name[n]);

	return request_threaded_irq(ptpmsg_irq->num, NULL,
				    ksz_ptp_msg_thread_fn, IRQF_ONESHOT,
				    ptpmsg_irq->name, ptpmsg_irq);
}

int ksz_ptp_irq_setup(struct dsa_switch *ds, u8 p)
{
	struct ksz_device *dev = ds->priv;
	const struct ksz_dev_ops *ops = dev->dev_ops;
	struct ksz_port *port = &dev->ports[p];
	struct ksz_irq *ptpirq = &port->ptpirq;
	int irq;
	int ret;

	ptpirq->dev = dev;
	ptpirq->masked = 0;
	ptpirq->nirqs = 3;
	ptpirq->reg_mask = ops->get_port_addr(p, REG_PTP_PORT_TX_INT_ENABLE__2);
	ptpirq->reg_status = ops->get_port_addr(p,
						REG_PTP_PORT_TX_INT_STATUS__2);
	snprintf(ptpirq->name, sizeof(ptpirq->name), "ptp-irq-%d", p);

	init_completion(&port->tstamp_msg_comp);

	ptpirq->domain = irq_domain_create_linear(dev_fwnode(dev->dev), ptpirq->nirqs,
						  &ksz_ptp_irq_domain_ops, ptpirq);
	if (!ptpirq->domain)
		return -ENOMEM;

	ptpirq->irq_num = irq_find_mapping(port->pirq.domain, PORT_SRC_PTP_INT);
	if (!ptpirq->irq_num) {
		ret = -EINVAL;
		goto out;
	}

	ret = request_threaded_irq(ptpirq->irq_num, NULL, ksz_ptp_irq_thread_fn,
				   IRQF_ONESHOT, ptpirq->name, ptpirq);
	if (ret)
		goto out;

	for (irq = 0; irq < ptpirq->nirqs; irq++) {
		ret = ksz_ptp_msg_irq_setup(port, irq);
		if (ret)
			goto out_ptp_msg;
	}

	return 0;

out_ptp_msg:
	free_irq(ptpirq->irq_num, ptpirq);
	while (irq--) {
		free_irq(port->ptpmsg_irq[irq].num, &port->ptpmsg_irq[irq]);
		irq_dispose_mapping(port->ptpmsg_irq[irq].num);
	}
out:
	irq_domain_remove(ptpirq->domain);

	return ret;
}

void ksz_ptp_irq_free(struct dsa_switch *ds, u8 p)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *port = &dev->ports[p];
	struct ksz_irq *ptpirq = &port->ptpirq;
	u8 n;

	for (n = 0; n < ptpirq->nirqs; n++)
		ksz_ptp_msg_irq_free(port, n);

	free_irq(ptpirq->irq_num, ptpirq);
	irq_dispose_mapping(ptpirq->irq_num);

	irq_domain_remove(ptpirq->domain);
}

MODULE_AUTHOR("Christian Eggers <ceggers@arri.de>");
MODULE_AUTHOR("Arun Ramadoss <arun.ramadoss@microchip.com>");
MODULE_DESCRIPTION("PTP support for KSZ switch");
MODULE_LICENSE("GPL");
