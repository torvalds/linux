// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Meta Platforms Inc.
 * Copyright (C) 2022 Jonathan Lemon <jonathan.lemon@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>

#include "bcm-phy-lib.h"

/* IEEE 1588 Expansion registers */
#define SLICE_CTRL		0x0810
#define  SLICE_TX_EN			BIT(0)
#define  SLICE_RX_EN			BIT(8)
#define TX_EVENT_MODE		0x0811
#define  MODE_TX_UPDATE_CF		BIT(0)
#define  MODE_TX_REPLACE_TS_CF		BIT(1)
#define  MODE_TX_REPLACE_TS		GENMASK(1, 0)
#define RX_EVENT_MODE		0x0819
#define  MODE_RX_UPDATE_CF		BIT(0)
#define  MODE_RX_INSERT_TS_48		BIT(1)
#define  MODE_RX_INSERT_TS_64		GENMASK(1, 0)

#define MODE_EVT_SHIFT_SYNC		0
#define MODE_EVT_SHIFT_DELAY_REQ	2
#define MODE_EVT_SHIFT_PDELAY_REQ	4
#define MODE_EVT_SHIFT_PDELAY_RESP	6

#define MODE_SEL_SHIFT_PORT		0
#define MODE_SEL_SHIFT_CPU		8

#define RX_MODE_SEL(sel, evt, act) \
	(((MODE_RX_##act) << (MODE_EVT_SHIFT_##evt)) << (MODE_SEL_SHIFT_##sel))

#define TX_MODE_SEL(sel, evt, act) \
	(((MODE_TX_##act) << (MODE_EVT_SHIFT_##evt)) << (MODE_SEL_SHIFT_##sel))

/* needs global TS capture first */
#define TX_TS_CAPTURE		0x0821
#define  TX_TS_CAP_EN			BIT(0)
#define RX_TS_CAPTURE		0x0822
#define  RX_TS_CAP_EN			BIT(0)

#define TIME_CODE_0		0x0854
#define TIME_CODE_1		0x0855
#define TIME_CODE_2		0x0856
#define TIME_CODE_3		0x0857
#define TIME_CODE_4		0x0858

#define DPLL_SELECT		0x085b
#define  DPLL_HB_MODE2			BIT(6)

#define SHADOW_CTRL		0x085c
#define SHADOW_LOAD		0x085d
#define  TIME_CODE_LOAD			BIT(10)
#define  SYNC_OUT_LOAD			BIT(9)
#define  NCO_TIME_LOAD			BIT(7)
#define  FREQ_LOAD			BIT(6)
#define INTR_MASK		0x085e
#define INTR_STATUS		0x085f
#define  INTC_FSYNC			BIT(0)
#define  INTC_SOP			BIT(1)

#define NCO_FREQ_LSB		0x0873
#define NCO_FREQ_MSB		0x0874

#define NCO_TIME_0		0x0875
#define NCO_TIME_1		0x0876
#define NCO_TIME_2_CTRL		0x0877
#define  FREQ_MDIO_SEL			BIT(14)

#define SYNC_OUT_0		0x0878
#define SYNC_OUT_1		0x0879
#define SYNC_OUT_2		0x087a

#define SYNC_IN_DIVIDER		0x087b

#define SYNOUT_TS_0		0x087c
#define SYNOUT_TS_1		0x087d
#define SYNOUT_TS_2		0x087e

#define NSE_CTRL		0x087f
#define  NSE_GMODE_EN			GENMASK(15, 14)
#define  NSE_CAPTURE_EN			BIT(13)
#define  NSE_INIT			BIT(12)
#define  NSE_CPU_FRAMESYNC		BIT(5)
#define  NSE_SYNC1_FRAMESYNC		BIT(3)
#define  NSE_FRAMESYNC_MASK		GENMASK(5, 2)
#define  NSE_PEROUT_EN			BIT(1)
#define  NSE_ONESHOT_EN			BIT(0)
#define  NSE_SYNC_OUT_MASK		GENMASK(1, 0)

#define TS_READ_CTRL		0x0885
#define  TS_READ_START			BIT(0)
#define  TS_READ_END			BIT(1)

#define HB_REG_0		0x0886
#define HB_REG_1		0x0887
#define HB_REG_2		0x0888
#define HB_REG_3		0x08ec
#define HB_REG_4		0x08ed
#define HB_STAT_CTRL		0x088e
#define  HB_READ_START			BIT(10)
#define  HB_READ_END			BIT(11)
#define  HB_READ_MASK			GENMASK(11, 10)

#define TS_REG_0		0x0889
#define TS_REG_1		0x088a
#define TS_REG_2		0x088b
#define TS_REG_3		0x08c4

#define TS_INFO_0		0x088c
#define TS_INFO_1		0x088d

#define TIMECODE_CTRL		0x08c3
#define  TX_TIMECODE_SEL		GENMASK(7, 0)
#define  RX_TIMECODE_SEL		GENMASK(15, 8)

#define TIME_SYNC		0x0ff5
#define  TIME_SYNC_EN			BIT(0)

struct bcm_ptp_private {
	struct phy_device *phydev;
	struct mii_timestamper mii_ts;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_info;
	struct ptp_pin_desc pin;
	struct mutex mutex;
	struct sk_buff_head tx_queue;
	int tx_type;
	bool hwts_rx;
	u16 nse_ctrl;
	bool pin_active;
	struct delayed_work pin_work;
};

struct bcm_ptp_skb_cb {
	unsigned long timeout;
	u16 seq_id;
	u8 msgtype;
	bool discard;
};

struct bcm_ptp_capture {
	ktime_t	hwtstamp;
	u16 seq_id;
	u8 msgtype;
	bool tx_dir;
};

#define BCM_SKB_CB(skb)		((struct bcm_ptp_skb_cb *)(skb)->cb)
#define SKB_TS_TIMEOUT		10			/* jiffies */

#define BCM_MAX_PULSE_8NS	((1U << 9) - 1)
#define BCM_MAX_PERIOD_8NS	((1U << 30) - 1)

#define BRCM_PHY_MODEL(phydev) \
	((phydev)->drv->phy_id & (phydev)->drv->phy_id_mask)

static struct bcm_ptp_private *mii2priv(struct mii_timestamper *mii_ts)
{
	return container_of(mii_ts, struct bcm_ptp_private, mii_ts);
}

static struct bcm_ptp_private *ptp2priv(struct ptp_clock_info *info)
{
	return container_of(info, struct bcm_ptp_private, ptp_info);
}

static void bcm_ptp_get_framesync_ts(struct phy_device *phydev,
				     struct timespec64 *ts)
{
	u16 hb[4];

	bcm_phy_write_exp(phydev, HB_STAT_CTRL, HB_READ_START);

	hb[0] = bcm_phy_read_exp(phydev, HB_REG_0);
	hb[1] = bcm_phy_read_exp(phydev, HB_REG_1);
	hb[2] = bcm_phy_read_exp(phydev, HB_REG_2);
	hb[3] = bcm_phy_read_exp(phydev, HB_REG_3);

	bcm_phy_write_exp(phydev, HB_STAT_CTRL, HB_READ_END);
	bcm_phy_write_exp(phydev, HB_STAT_CTRL, 0);

	ts->tv_sec = (hb[3] << 16) | hb[2];
	ts->tv_nsec = (hb[1] << 16) | hb[0];
}

static u16 bcm_ptp_framesync_disable(struct phy_device *phydev, u16 orig_ctrl)
{
	u16 ctrl = orig_ctrl & ~(NSE_FRAMESYNC_MASK | NSE_CAPTURE_EN);

	bcm_phy_write_exp(phydev, NSE_CTRL, ctrl);

	return ctrl;
}

static void bcm_ptp_framesync_restore(struct phy_device *phydev, u16 orig_ctrl)
{
	if (orig_ctrl & NSE_FRAMESYNC_MASK)
		bcm_phy_write_exp(phydev, NSE_CTRL, orig_ctrl);
}

static void bcm_ptp_framesync(struct phy_device *phydev, u16 ctrl)
{
	/* trigger framesync - must have 0->1 transition. */
	bcm_phy_write_exp(phydev, NSE_CTRL, ctrl | NSE_CPU_FRAMESYNC);
}

static int bcm_ptp_framesync_ts(struct phy_device *phydev,
				struct ptp_system_timestamp *sts,
				struct timespec64 *ts,
				u16 orig_ctrl)
{
	u16 ctrl, reg;
	int i;

	ctrl = bcm_ptp_framesync_disable(phydev, orig_ctrl);

	ptp_read_system_prets(sts);

	/* trigger framesync + capture */
	bcm_ptp_framesync(phydev, ctrl | NSE_CAPTURE_EN);

	ptp_read_system_postts(sts);

	/* poll for FSYNC interrupt from TS capture */
	for (i = 0; i < 10; i++) {
		reg = bcm_phy_read_exp(phydev, INTR_STATUS);
		if (reg & INTC_FSYNC) {
			bcm_ptp_get_framesync_ts(phydev, ts);
			break;
		}
	}

	bcm_ptp_framesync_restore(phydev, orig_ctrl);

	return reg & INTC_FSYNC ? 0 : -ETIMEDOUT;
}

static int bcm_ptp_gettimex(struct ptp_clock_info *info,
			    struct timespec64 *ts,
			    struct ptp_system_timestamp *sts)
{
	struct bcm_ptp_private *priv = ptp2priv(info);
	int err;

	mutex_lock(&priv->mutex);
	err = bcm_ptp_framesync_ts(priv->phydev, sts, ts, priv->nse_ctrl);
	mutex_unlock(&priv->mutex);

	return err;
}

static int bcm_ptp_settime_locked(struct bcm_ptp_private *priv,
				  const struct timespec64 *ts)
{
	struct phy_device *phydev = priv->phydev;
	u16 ctrl;
	u64 ns;

	ctrl = bcm_ptp_framesync_disable(phydev, priv->nse_ctrl);

	/* set up time code */
	bcm_phy_write_exp(phydev, TIME_CODE_0, ts->tv_nsec);
	bcm_phy_write_exp(phydev, TIME_CODE_1, ts->tv_nsec >> 16);
	bcm_phy_write_exp(phydev, TIME_CODE_2, ts->tv_sec);
	bcm_phy_write_exp(phydev, TIME_CODE_3, ts->tv_sec >> 16);
	bcm_phy_write_exp(phydev, TIME_CODE_4, ts->tv_sec >> 32);

	/* set NCO counter to match */
	ns = timespec64_to_ns(ts);
	bcm_phy_write_exp(phydev, NCO_TIME_0, ns >> 4);
	bcm_phy_write_exp(phydev, NCO_TIME_1, ns >> 20);
	bcm_phy_write_exp(phydev, NCO_TIME_2_CTRL, (ns >> 36) & 0xfff);

	/* set up load on next frame sync (auto-clears due to NSE_INIT) */
	bcm_phy_write_exp(phydev, SHADOW_LOAD, TIME_CODE_LOAD | NCO_TIME_LOAD);

	/* must have NSE_INIT in order to write time code */
	bcm_ptp_framesync(phydev, ctrl | NSE_INIT);

	bcm_ptp_framesync_restore(phydev, priv->nse_ctrl);

	return 0;
}

static int bcm_ptp_settime(struct ptp_clock_info *info,
			   const struct timespec64 *ts)
{
	struct bcm_ptp_private *priv = ptp2priv(info);
	int err;

	mutex_lock(&priv->mutex);
	err = bcm_ptp_settime_locked(priv, ts);
	mutex_unlock(&priv->mutex);

	return err;
}

static int bcm_ptp_adjtime_locked(struct bcm_ptp_private *priv,
				  s64 delta_ns)
{
	struct timespec64 ts;
	int err;
	s64 ns;

	err = bcm_ptp_framesync_ts(priv->phydev, NULL, &ts, priv->nse_ctrl);
	if (!err) {
		ns = timespec64_to_ns(&ts) + delta_ns;
		ts = ns_to_timespec64(ns);
		err = bcm_ptp_settime_locked(priv, &ts);
	}
	return err;
}

static int bcm_ptp_adjtime(struct ptp_clock_info *info, s64 delta_ns)
{
	struct bcm_ptp_private *priv = ptp2priv(info);
	int err;

	mutex_lock(&priv->mutex);
	err = bcm_ptp_adjtime_locked(priv, delta_ns);
	mutex_unlock(&priv->mutex);

	return err;
}

/* A 125Mhz clock should adjust 8ns per pulse.
 * The frequency adjustment base is 0x8000 0000, or 8*2^28.
 *
 * Frequency adjustment is
 * adj = scaled_ppm * 8*2^28 / (10^6 * 2^16)
 *   which simplifies to:
 * adj = scaled_ppm * 2^9 / 5^6
 */
static int bcm_ptp_adjfine(struct ptp_clock_info *info, long scaled_ppm)
{
	struct bcm_ptp_private *priv = ptp2priv(info);
	int neg_adj = 0;
	u32 diff, freq;
	u16 ctrl;
	u64 adj;

	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}

	adj = scaled_ppm << 9;
	diff = div_u64(adj, 15625);
	freq = (8 << 28) + (neg_adj ? -diff : diff);

	mutex_lock(&priv->mutex);

	ctrl = bcm_ptp_framesync_disable(priv->phydev, priv->nse_ctrl);

	bcm_phy_write_exp(priv->phydev, NCO_FREQ_LSB, freq);
	bcm_phy_write_exp(priv->phydev, NCO_FREQ_MSB, freq >> 16);

	bcm_phy_write_exp(priv->phydev, NCO_TIME_2_CTRL, FREQ_MDIO_SEL);

	/* load on next framesync */
	bcm_phy_write_exp(priv->phydev, SHADOW_LOAD, FREQ_LOAD);

	bcm_ptp_framesync(priv->phydev, ctrl);

	/* clear load */
	bcm_phy_write_exp(priv->phydev, SHADOW_LOAD, 0);

	bcm_ptp_framesync_restore(priv->phydev, priv->nse_ctrl);

	mutex_unlock(&priv->mutex);

	return 0;
}

static bool bcm_ptp_rxtstamp(struct mii_timestamper *mii_ts,
			     struct sk_buff *skb, int type)
{
	struct bcm_ptp_private *priv = mii2priv(mii_ts);
	struct skb_shared_hwtstamps *hwts;
	struct ptp_header *header;
	u32 sec, nsec;
	u8 *data;
	int off;

	if (!priv->hwts_rx)
		return false;

	header = ptp_parse_header(skb, type);
	if (!header)
		return false;

	data = (u8 *)(header + 1);
	sec = get_unaligned_be32(data);
	nsec = get_unaligned_be32(data + 4);

	hwts = skb_hwtstamps(skb);
	hwts->hwtstamp = ktime_set(sec, nsec);

	off = data - skb->data + 8;
	if (off < skb->len) {
		memmove(data, data + 8, skb->len - off);
		__pskb_trim(skb, skb->len - 8);
	}

	return false;
}

static bool bcm_ptp_get_tstamp(struct bcm_ptp_private *priv,
			       struct bcm_ptp_capture *capts)
{
	struct phy_device *phydev = priv->phydev;
	u16 ts[4], reg;
	u32 sec, nsec;

	mutex_lock(&priv->mutex);

	reg = bcm_phy_read_exp(phydev, INTR_STATUS);
	if ((reg & INTC_SOP) == 0) {
		mutex_unlock(&priv->mutex);
		return false;
	}

	bcm_phy_write_exp(phydev, TS_READ_CTRL, TS_READ_START);

	ts[0] = bcm_phy_read_exp(phydev, TS_REG_0);
	ts[1] = bcm_phy_read_exp(phydev, TS_REG_1);
	ts[2] = bcm_phy_read_exp(phydev, TS_REG_2);
	ts[3] = bcm_phy_read_exp(phydev, TS_REG_3);

	/* not in be32 format for some reason */
	capts->seq_id = bcm_phy_read_exp(priv->phydev, TS_INFO_0);

	reg = bcm_phy_read_exp(phydev, TS_INFO_1);
	capts->msgtype = reg >> 12;
	capts->tx_dir = !!(reg & BIT(11));

	bcm_phy_write_exp(phydev, TS_READ_CTRL, TS_READ_END);
	bcm_phy_write_exp(phydev, TS_READ_CTRL, 0);

	mutex_unlock(&priv->mutex);

	sec = (ts[3] << 16) | ts[2];
	nsec = (ts[1] << 16) | ts[0];
	capts->hwtstamp = ktime_set(sec, nsec);

	return true;
}

static void bcm_ptp_match_tstamp(struct bcm_ptp_private *priv,
				 struct bcm_ptp_capture *capts)
{
	struct skb_shared_hwtstamps hwts;
	struct sk_buff *skb, *ts_skb;
	unsigned long flags;
	bool first = false;

	ts_skb = NULL;
	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	skb_queue_walk(&priv->tx_queue, skb) {
		if (BCM_SKB_CB(skb)->seq_id == capts->seq_id &&
		    BCM_SKB_CB(skb)->msgtype == capts->msgtype) {
			first = skb_queue_is_first(&priv->tx_queue, skb);
			__skb_unlink(skb, &priv->tx_queue);
			ts_skb = skb;
			break;
		}
	}
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);

	/* TX captures one-step packets, discard them if needed. */
	if (ts_skb) {
		if (BCM_SKB_CB(ts_skb)->discard) {
			kfree_skb(ts_skb);
		} else {
			memset(&hwts, 0, sizeof(hwts));
			hwts.hwtstamp = capts->hwtstamp;
			skb_complete_tx_timestamp(ts_skb, &hwts);
		}
	}

	/* not first match, try and expire entries */
	if (!first) {
		while ((skb = skb_dequeue(&priv->tx_queue))) {
			if (!time_after(jiffies, BCM_SKB_CB(skb)->timeout)) {
				skb_queue_head(&priv->tx_queue, skb);
				break;
			}
			kfree_skb(skb);
		}
	}
}

static long bcm_ptp_do_aux_work(struct ptp_clock_info *info)
{
	struct bcm_ptp_private *priv = ptp2priv(info);
	struct bcm_ptp_capture capts;
	bool reschedule = false;

	while (!skb_queue_empty_lockless(&priv->tx_queue)) {
		if (!bcm_ptp_get_tstamp(priv, &capts)) {
			reschedule = true;
			break;
		}
		bcm_ptp_match_tstamp(priv, &capts);
	}

	return reschedule ? 1 : -1;
}

static int bcm_ptp_cancel_func(struct bcm_ptp_private *priv)
{
	if (!priv->pin_active)
		return 0;

	priv->pin_active = false;

	priv->nse_ctrl &= ~(NSE_SYNC_OUT_MASK | NSE_SYNC1_FRAMESYNC |
			    NSE_CAPTURE_EN);
	bcm_phy_write_exp(priv->phydev, NSE_CTRL, priv->nse_ctrl);

	cancel_delayed_work_sync(&priv->pin_work);

	return 0;
}

static void bcm_ptp_perout_work(struct work_struct *pin_work)
{
	struct bcm_ptp_private *priv =
		container_of(pin_work, struct bcm_ptp_private, pin_work.work);
	struct phy_device *phydev = priv->phydev;
	struct timespec64 ts;
	u64 ns, next;
	u16 ctrl;

	mutex_lock(&priv->mutex);

	/* no longer running */
	if (!priv->pin_active) {
		mutex_unlock(&priv->mutex);
		return;
	}

	bcm_ptp_framesync_ts(phydev, NULL, &ts, priv->nse_ctrl);

	/* this is 1PPS only */
	next = NSEC_PER_SEC - ts.tv_nsec;
	ts.tv_sec += next < NSEC_PER_MSEC ? 2 : 1;
	ts.tv_nsec = 0;

	ns = timespec64_to_ns(&ts);

	/* force 0->1 transition for ONESHOT */
	ctrl = bcm_ptp_framesync_disable(phydev,
					 priv->nse_ctrl & ~NSE_ONESHOT_EN);

	bcm_phy_write_exp(phydev, SYNOUT_TS_0, ns & 0xfff0);
	bcm_phy_write_exp(phydev, SYNOUT_TS_1, ns >> 16);
	bcm_phy_write_exp(phydev, SYNOUT_TS_2, ns >> 32);

	/* load values on next framesync */
	bcm_phy_write_exp(phydev, SHADOW_LOAD, SYNC_OUT_LOAD);

	bcm_ptp_framesync(phydev, ctrl | NSE_ONESHOT_EN | NSE_INIT);

	priv->nse_ctrl |= NSE_ONESHOT_EN;
	bcm_ptp_framesync_restore(phydev, priv->nse_ctrl);

	mutex_unlock(&priv->mutex);

	next = next + NSEC_PER_MSEC;
	schedule_delayed_work(&priv->pin_work, nsecs_to_jiffies(next));
}

static int bcm_ptp_perout_locked(struct bcm_ptp_private *priv,
				 struct ptp_perout_request *req, int on)
{
	struct phy_device *phydev = priv->phydev;
	u64 period, pulse;
	u16 val;

	if (!on)
		return bcm_ptp_cancel_func(priv);

	/* 1PPS */
	if (req->period.sec != 1 || req->period.nsec != 0)
		return -EINVAL;

	period = BCM_MAX_PERIOD_8NS;	/* write nonzero value */

	if (req->flags & PTP_PEROUT_PHASE)
		return -EOPNOTSUPP;

	if (req->flags & PTP_PEROUT_DUTY_CYCLE)
		pulse = ktime_to_ns(ktime_set(req->on.sec, req->on.nsec));
	else
		pulse = (u64)BCM_MAX_PULSE_8NS << 3;

	/* convert to 8ns units */
	pulse >>= 3;

	if (!pulse || pulse > period || pulse > BCM_MAX_PULSE_8NS)
		return -EINVAL;

	bcm_phy_write_exp(phydev, SYNC_OUT_0, period);

	val = ((pulse & 0x3) << 14) | ((period >> 16) & 0x3fff);
	bcm_phy_write_exp(phydev, SYNC_OUT_1, val);

	val = ((pulse >> 2) & 0x7f) | (pulse << 7);
	bcm_phy_write_exp(phydev, SYNC_OUT_2, val);

	if (priv->pin_active)
		cancel_delayed_work_sync(&priv->pin_work);

	priv->pin_active = true;
	INIT_DELAYED_WORK(&priv->pin_work, bcm_ptp_perout_work);
	schedule_delayed_work(&priv->pin_work, 0);

	return 0;
}

static void bcm_ptp_extts_work(struct work_struct *pin_work)
{
	struct bcm_ptp_private *priv =
		container_of(pin_work, struct bcm_ptp_private, pin_work.work);
	struct phy_device *phydev = priv->phydev;
	struct ptp_clock_event event;
	struct timespec64 ts;
	u16 reg;

	mutex_lock(&priv->mutex);

	/* no longer running */
	if (!priv->pin_active) {
		mutex_unlock(&priv->mutex);
		return;
	}

	reg = bcm_phy_read_exp(phydev, INTR_STATUS);
	if ((reg & INTC_FSYNC) == 0)
		goto out;

	bcm_ptp_get_framesync_ts(phydev, &ts);

	event.index = 0;
	event.type = PTP_CLOCK_EXTTS;
	event.timestamp = timespec64_to_ns(&ts);
	ptp_clock_event(priv->ptp_clock, &event);

out:
	mutex_unlock(&priv->mutex);
	schedule_delayed_work(&priv->pin_work, HZ / 4);
}

static int bcm_ptp_extts_locked(struct bcm_ptp_private *priv, int on)
{
	struct phy_device *phydev = priv->phydev;

	if (!on)
		return bcm_ptp_cancel_func(priv);

	if (priv->pin_active)
		cancel_delayed_work_sync(&priv->pin_work);

	bcm_ptp_framesync_disable(phydev, priv->nse_ctrl);

	priv->nse_ctrl |= NSE_SYNC1_FRAMESYNC | NSE_CAPTURE_EN;

	bcm_ptp_framesync_restore(phydev, priv->nse_ctrl);

	priv->pin_active = true;
	INIT_DELAYED_WORK(&priv->pin_work, bcm_ptp_extts_work);
	schedule_delayed_work(&priv->pin_work, 0);

	return 0;
}

static int bcm_ptp_enable(struct ptp_clock_info *info,
			  struct ptp_clock_request *rq, int on)
{
	struct bcm_ptp_private *priv = ptp2priv(info);
	int err = -EBUSY;

	mutex_lock(&priv->mutex);

	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		if (priv->pin.func == PTP_PF_PEROUT)
			err = bcm_ptp_perout_locked(priv, &rq->perout, on);
		break;
	case PTP_CLK_REQ_EXTTS:
		if (priv->pin.func == PTP_PF_EXTTS)
			err = bcm_ptp_extts_locked(priv, on);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&priv->mutex);

	return err;
}

static int bcm_ptp_verify(struct ptp_clock_info *info, unsigned int pin,
			  enum ptp_pin_function func, unsigned int chan)
{
	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_EXTTS:
	case PTP_PF_PEROUT:
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static const struct ptp_clock_info bcm_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= KBUILD_MODNAME,
	.max_adj	= 100000000,
	.gettimex64	= bcm_ptp_gettimex,
	.settime64	= bcm_ptp_settime,
	.adjtime	= bcm_ptp_adjtime,
	.adjfine	= bcm_ptp_adjfine,
	.enable		= bcm_ptp_enable,
	.verify		= bcm_ptp_verify,
	.do_aux_work	= bcm_ptp_do_aux_work,
	.n_pins		= 1,
	.n_per_out	= 1,
	.n_ext_ts	= 1,
};

static void bcm_ptp_txtstamp(struct mii_timestamper *mii_ts,
			     struct sk_buff *skb, int type)
{
	struct bcm_ptp_private *priv = mii2priv(mii_ts);
	struct ptp_header *hdr;
	bool discard = false;
	int msgtype;

	hdr = ptp_parse_header(skb, type);
	if (!hdr)
		goto out;
	msgtype = ptp_get_msgtype(hdr, type);

	switch (priv->tx_type) {
	case HWTSTAMP_TX_ONESTEP_P2P:
		if (msgtype == PTP_MSGTYPE_PDELAY_RESP)
			discard = true;
		fallthrough;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		if (msgtype == PTP_MSGTYPE_SYNC)
			discard = true;
		fallthrough;
	case HWTSTAMP_TX_ON:
		BCM_SKB_CB(skb)->timeout = jiffies + SKB_TS_TIMEOUT;
		BCM_SKB_CB(skb)->seq_id = be16_to_cpu(hdr->sequence_id);
		BCM_SKB_CB(skb)->msgtype = msgtype;
		BCM_SKB_CB(skb)->discard = discard;
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		skb_queue_tail(&priv->tx_queue, skb);
		ptp_schedule_worker(priv->ptp_clock, 0);
		return;
	default:
		break;
	}

out:
	kfree_skb(skb);
}

static int bcm_ptp_hwtstamp(struct mii_timestamper *mii_ts,
			    struct kernel_hwtstamp_config *cfg,
			    struct netlink_ext_ack *extack)
{
	struct bcm_ptp_private *priv = mii2priv(mii_ts);
	u16 mode, ctrl;

	switch (cfg->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		priv->hwts_rx = false;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		cfg->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		priv->hwts_rx = true;
		break;
	default:
		return -ERANGE;
	}

	priv->tx_type = cfg->tx_type;

	ctrl  = priv->hwts_rx ? SLICE_RX_EN : 0;
	ctrl |= priv->tx_type != HWTSTAMP_TX_OFF ? SLICE_TX_EN : 0;

	mode = TX_MODE_SEL(PORT, SYNC, REPLACE_TS) |
	       TX_MODE_SEL(PORT, DELAY_REQ, REPLACE_TS) |
	       TX_MODE_SEL(PORT, PDELAY_REQ, REPLACE_TS) |
	       TX_MODE_SEL(PORT, PDELAY_RESP, REPLACE_TS);

	bcm_phy_write_exp(priv->phydev, TX_EVENT_MODE, mode);

	mode = RX_MODE_SEL(PORT, SYNC, INSERT_TS_64) |
	       RX_MODE_SEL(PORT, DELAY_REQ, INSERT_TS_64) |
	       RX_MODE_SEL(PORT, PDELAY_REQ, INSERT_TS_64) |
	       RX_MODE_SEL(PORT, PDELAY_RESP, INSERT_TS_64);

	bcm_phy_write_exp(priv->phydev, RX_EVENT_MODE, mode);

	bcm_phy_write_exp(priv->phydev, SLICE_CTRL, ctrl);

	if (ctrl & SLICE_TX_EN)
		bcm_phy_write_exp(priv->phydev, TX_TS_CAPTURE, TX_TS_CAP_EN);
	else
		ptp_cancel_worker_sync(priv->ptp_clock);

	/* purge existing data */
	skb_queue_purge(&priv->tx_queue);

	return 0;
}

static int bcm_ptp_ts_info(struct mii_timestamper *mii_ts,
			   struct ethtool_ts_info *ts_info)
{
	struct bcm_ptp_private *priv = mii2priv(mii_ts);

	ts_info->phc_index = ptp_clock_index(priv->ptp_clock);
	ts_info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	ts_info->tx_types =
		BIT(HWTSTAMP_TX_ON) |
		BIT(HWTSTAMP_TX_OFF) |
		BIT(HWTSTAMP_TX_ONESTEP_SYNC) |
		BIT(HWTSTAMP_TX_ONESTEP_P2P);
	ts_info->rx_filters =
		BIT(HWTSTAMP_FILTER_NONE) |
		BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

void bcm_ptp_stop(struct bcm_ptp_private *priv)
{
	ptp_cancel_worker_sync(priv->ptp_clock);
	bcm_ptp_cancel_func(priv);
}
EXPORT_SYMBOL_GPL(bcm_ptp_stop);

void bcm_ptp_config_init(struct phy_device *phydev)
{
	/* init network sync engine */
	bcm_phy_write_exp(phydev, NSE_CTRL, NSE_GMODE_EN | NSE_INIT);

	/* enable time sync (TX/RX SOP capture) */
	bcm_phy_write_exp(phydev, TIME_SYNC, TIME_SYNC_EN);

	/* use sec.nsec heartbeat capture */
	bcm_phy_write_exp(phydev, DPLL_SELECT, DPLL_HB_MODE2);

	/* use 64 bit timecode for TX */
	bcm_phy_write_exp(phydev, TIMECODE_CTRL, TX_TIMECODE_SEL);

	/* always allow FREQ_LOAD on framesync */
	bcm_phy_write_exp(phydev, SHADOW_CTRL, FREQ_LOAD);

	bcm_phy_write_exp(phydev, SYNC_IN_DIVIDER, 1);
}
EXPORT_SYMBOL_GPL(bcm_ptp_config_init);

static void bcm_ptp_init(struct bcm_ptp_private *priv)
{
	priv->nse_ctrl = NSE_GMODE_EN;

	mutex_init(&priv->mutex);
	skb_queue_head_init(&priv->tx_queue);

	priv->mii_ts.rxtstamp = bcm_ptp_rxtstamp;
	priv->mii_ts.txtstamp = bcm_ptp_txtstamp;
	priv->mii_ts.hwtstamp = bcm_ptp_hwtstamp;
	priv->mii_ts.ts_info = bcm_ptp_ts_info;

	priv->phydev->mii_ts = &priv->mii_ts;
}

struct bcm_ptp_private *bcm_ptp_probe(struct phy_device *phydev)
{
	struct bcm_ptp_private *priv;
	struct ptp_clock *clock;

	switch (BRCM_PHY_MODEL(phydev)) {
	case PHY_ID_BCM54210E:
		break;
	default:
		return NULL;
	}

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->ptp_info = bcm_ptp_clock_info;

	snprintf(priv->pin.name, sizeof(priv->pin.name), "SYNC_OUT");
	priv->ptp_info.pin_config = &priv->pin;

	clock = ptp_clock_register(&priv->ptp_info, &phydev->mdio.dev);
	if (IS_ERR(clock))
		return ERR_CAST(clock);
	priv->ptp_clock = clock;

	priv->phydev = phydev;
	bcm_ptp_init(priv);

	return priv;
}
EXPORT_SYMBOL_GPL(bcm_ptp_probe);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Broadcom PHY PTP driver");
