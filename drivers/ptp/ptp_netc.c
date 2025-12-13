// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * NXP NETC V4 Timer driver
 * Copyright 2025 NXP
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/fsl/netc_global.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/ptp_clock_kernel.h>

#define NETC_TMR_PCI_VENDOR_NXP		0x1131

#define NETC_TMR_CTRL			0x0080
#define  TMR_CTRL_CK_SEL		GENMASK(1, 0)
#define  TMR_CTRL_TE			BIT(2)
#define  TMR_ETEP(i)			BIT(8 + (i))
#define  TMR_COMP_MODE			BIT(15)
#define  TMR_CTRL_TCLK_PERIOD		GENMASK(25, 16)
#define  TMR_CTRL_PPL(i)		BIT(27 - (i))
#define  TMR_CTRL_FS			BIT(28)

#define NETC_TMR_TEVENT			0x0084
#define  TMR_TEVNET_PPEN(i)		BIT(7 - (i))
#define  TMR_TEVENT_PPEN_ALL		GENMASK(7, 5)
#define  TMR_TEVENT_ALMEN(i)		BIT(16 + (i))
#define  TMR_TEVENT_ETS_THREN(i)	BIT(20 + (i))
#define  TMR_TEVENT_ETSEN(i)		BIT(24 + (i))
#define  TMR_TEVENT_ETS_OVEN(i)		BIT(28 + (i))
#define  TMR_TEVENT_ETS(i)		(TMR_TEVENT_ETS_THREN(i) | \
					 TMR_TEVENT_ETSEN(i) | \
					 TMR_TEVENT_ETS_OVEN(i))

#define NETC_TMR_TEMASK			0x0088
#define NETC_TMR_STAT			0x0094
#define  TMR_STAT_ETS_VLD(i)		BIT(24 + (i))

#define NETC_TMR_CNT_L			0x0098
#define NETC_TMR_CNT_H			0x009c
#define NETC_TMR_ADD			0x00a0
#define NETC_TMR_PRSC			0x00a8
#define NETC_TMR_ECTRL			0x00ac
#define NETC_TMR_OFF_L			0x00b0
#define NETC_TMR_OFF_H			0x00b4

/* i = 0, 1, i indicates the index of TMR_ALARM */
#define NETC_TMR_ALARM_L(i)		(0x00b8 + (i) * 8)
#define NETC_TMR_ALARM_H(i)		(0x00bc + (i) * 8)

/* i = 0, 1, 2. i indicates the index of TMR_FIPER. */
#define NETC_TMR_FIPER(i)		(0x00d0 + (i) * 4)

#define NETC_TMR_FIPER_CTRL		0x00dc
#define  FIPER_CTRL_DIS(i)		(BIT(7) << (i) * 8)
#define  FIPER_CTRL_PG(i)		(BIT(6) << (i) * 8)
#define  FIPER_CTRL_FS_ALARM(i)		(BIT(5) << (i) * 8)
#define  FIPER_CTRL_PW(i)		(GENMASK(4, 0) << (i) * 8)
#define  FIPER_CTRL_SET_PW(i, v)	(((v) & GENMASK(4, 0)) << 8 * (i))

/* i = 0, 1, i indicates the index of TMR_ETTS */
#define NETC_TMR_ETTS_L(i)		(0x00e0 + (i) * 8)
#define NETC_TMR_ETTS_H(i)		(0x00e4 + (i) * 8)
#define NETC_TMR_CUR_TIME_L		0x00f0
#define NETC_TMR_CUR_TIME_H		0x00f4

#define NETC_TMR_REGS_BAR		0
#define NETC_GLOBAL_OFFSET		0x10000
#define NETC_GLOBAL_IPBRR0		0xbf8
#define  IPBRR0_IP_REV			GENMASK(15, 0)
#define NETC_REV_4_1			0x0401

#define NETC_TMR_FIPER_NUM		3
#define NETC_TMR_INVALID_CHANNEL	NETC_TMR_FIPER_NUM
#define NETC_TMR_DEFAULT_PRSC		2
#define NETC_TMR_DEFAULT_ALARM		GENMASK_ULL(63, 0)
#define NETC_TMR_DEFAULT_FIPER		GENMASK(31, 0)
#define NETC_TMR_FIPER_MAX_PW		GENMASK(4, 0)
#define NETC_TMR_ALARM_NUM		2
#define NETC_TMR_DEFAULT_ETTF_THR	7

/* 1588 timer reference clock source select */
#define NETC_TMR_CCM_TIMER1		0 /* enet_timer1_clk_root, from CCM */
#define NETC_TMR_SYSTEM_CLK		1 /* enet_clk_root/2, from CCM */
#define NETC_TMR_EXT_OSC		2 /* tmr_1588_clk, from IO pins */

#define NETC_TMR_SYSCLK_333M		333333333U

enum netc_pp_type {
	NETC_PP_PPS = 1,
	NETC_PP_PEROUT,
};

struct netc_pp {
	enum netc_pp_type type;
	bool enabled;
	int alarm_id;
	u32 period; /* pulse period, ns */
	u64 stime; /* start time, ns */
};

struct netc_timer {
	void __iomem *base;
	struct pci_dev *pdev;
	spinlock_t lock; /* Prevent concurrent access to registers */

	struct ptp_clock *clock;
	struct ptp_clock_info caps;
	u32 clk_select;
	u32 clk_freq;
	u32 oclk_prsc;
	/* High 32-bit is integer part, low 32-bit is fractional part */
	u64 period;

	int irq;
	char irq_name[24];
	int revision;
	u32 tmr_emask;
	u8 pps_channel;
	u8 fs_alarm_num;
	u8 fs_alarm_bitmap;
	struct netc_pp pp[NETC_TMR_FIPER_NUM]; /* periodic pulse */
};

#define netc_timer_rd(p, o)		netc_read((p)->base + (o))
#define netc_timer_wr(p, o, v)		netc_write((p)->base + (o), v)
#define ptp_to_netc_timer(ptp)		container_of((ptp), struct netc_timer, caps)

static const char *const timer_clk_src[] = {
	"ccm",
	"ext"
};

static void netc_timer_cnt_write(struct netc_timer *priv, u64 ns)
{
	u32 tmr_cnt_h = upper_32_bits(ns);
	u32 tmr_cnt_l = lower_32_bits(ns);

	/* Writes to the TMR_CNT_L register copies the written value
	 * into the shadow TMR_CNT_L register. Writes to the TMR_CNT_H
	 * register copies the values written into the shadow TMR_CNT_H
	 * register. Contents of the shadow registers are copied into
	 * the TMR_CNT_L and TMR_CNT_H registers following a write into
	 * the TMR_CNT_H register. So the user must writes to TMR_CNT_L
	 * register first. Other H/L registers should have the same
	 * behavior.
	 */
	netc_timer_wr(priv, NETC_TMR_CNT_L, tmr_cnt_l);
	netc_timer_wr(priv, NETC_TMR_CNT_H, tmr_cnt_h);
}

static u64 netc_timer_offset_read(struct netc_timer *priv)
{
	u32 tmr_off_l, tmr_off_h;
	u64 offset;

	tmr_off_l = netc_timer_rd(priv, NETC_TMR_OFF_L);
	tmr_off_h = netc_timer_rd(priv, NETC_TMR_OFF_H);
	offset = (((u64)tmr_off_h) << 32) | tmr_off_l;

	return offset;
}

static void netc_timer_offset_write(struct netc_timer *priv, u64 offset)
{
	u32 tmr_off_h = upper_32_bits(offset);
	u32 tmr_off_l = lower_32_bits(offset);

	netc_timer_wr(priv, NETC_TMR_OFF_L, tmr_off_l);
	netc_timer_wr(priv, NETC_TMR_OFF_H, tmr_off_h);
}

static u64 netc_timer_cur_time_read(struct netc_timer *priv)
{
	u32 time_h, time_l;
	u64 ns;

	/* The user should read NETC_TMR_CUR_TIME_L first to
	 * get correct current time.
	 */
	time_l = netc_timer_rd(priv, NETC_TMR_CUR_TIME_L);
	time_h = netc_timer_rd(priv, NETC_TMR_CUR_TIME_H);
	ns = (u64)time_h << 32 | time_l;

	return ns;
}

static void netc_timer_alarm_write(struct netc_timer *priv,
				   u64 alarm, int index)
{
	u32 alarm_h = upper_32_bits(alarm);
	u32 alarm_l = lower_32_bits(alarm);

	netc_timer_wr(priv, NETC_TMR_ALARM_L(index), alarm_l);
	netc_timer_wr(priv, NETC_TMR_ALARM_H(index), alarm_h);
}

static u32 netc_timer_get_integral_period(struct netc_timer *priv)
{
	u32 tmr_ctrl, integral_period;

	tmr_ctrl = netc_timer_rd(priv, NETC_TMR_CTRL);
	integral_period = FIELD_GET(TMR_CTRL_TCLK_PERIOD, tmr_ctrl);

	return integral_period;
}

static u32 netc_timer_calculate_fiper_pw(struct netc_timer *priv,
					 u32 fiper)
{
	u64 divisor, pulse_width;

	/* Set the FIPER pulse width to half FIPER interval by default.
	 * pulse_width = (fiper / 2) / TMR_GCLK_period,
	 * TMR_GCLK_period = NSEC_PER_SEC / TMR_GCLK_freq,
	 * TMR_GCLK_freq = (clk_freq / oclk_prsc) Hz,
	 * so pulse_width = fiper * clk_freq / (2 * NSEC_PER_SEC * oclk_prsc).
	 */
	divisor = mul_u32_u32(2 * NSEC_PER_SEC, priv->oclk_prsc);
	pulse_width = div64_u64(mul_u32_u32(fiper, priv->clk_freq), divisor);

	/* The FIPER_PW field only has 5 bits, need to update oclk_prsc */
	if (pulse_width > NETC_TMR_FIPER_MAX_PW)
		pulse_width = NETC_TMR_FIPER_MAX_PW;

	return pulse_width;
}

static void netc_timer_set_pps_alarm(struct netc_timer *priv, int channel,
				     u32 integral_period)
{
	struct netc_pp *pp = &priv->pp[channel];
	u64 alarm;

	/* Get the alarm value */
	alarm = netc_timer_cur_time_read(priv) +  NSEC_PER_MSEC;
	alarm = roundup_u64(alarm, NSEC_PER_SEC);
	alarm = roundup_u64(alarm, integral_period);

	netc_timer_alarm_write(priv, alarm, pp->alarm_id);
}

static void netc_timer_set_perout_alarm(struct netc_timer *priv, int channel,
					u32 integral_period)
{
	u64 cur_time = netc_timer_cur_time_read(priv);
	struct netc_pp *pp = &priv->pp[channel];
	u64 alarm, delta, min_time;
	u32 period = pp->period;
	u64 stime = pp->stime;

	min_time = cur_time + NSEC_PER_MSEC + period;
	if (stime < min_time) {
		delta = min_time - stime;
		stime += roundup_u64(delta, period);
	}

	alarm = roundup_u64(stime - period, integral_period);
	netc_timer_alarm_write(priv, alarm, pp->alarm_id);
}

static int netc_timer_get_alarm_id(struct netc_timer *priv)
{
	int i;

	for (i = 0; i < priv->fs_alarm_num; i++) {
		if (!(priv->fs_alarm_bitmap & BIT(i))) {
			priv->fs_alarm_bitmap |= BIT(i);
			break;
		}
	}

	return i;
}

static u64 netc_timer_get_gclk_period(struct netc_timer *priv)
{
	/* TMR_GCLK_freq = (clk_freq / oclk_prsc) Hz.
	 * TMR_GCLK_period = NSEC_PER_SEC / TMR_GCLK_freq.
	 * TMR_GCLK_period = (NSEC_PER_SEC * oclk_prsc) / clk_freq
	 */

	return div_u64(mul_u32_u32(NSEC_PER_SEC, priv->oclk_prsc),
		       priv->clk_freq);
}

static void netc_timer_enable_periodic_pulse(struct netc_timer *priv,
					     u8 channel)
{
	u32 fiper_pw, fiper, fiper_ctrl, integral_period;
	struct netc_pp *pp = &priv->pp[channel];
	int alarm_id = pp->alarm_id;

	integral_period = netc_timer_get_integral_period(priv);
	/* Set to desired FIPER interval in ns - TCLK_PERIOD */
	fiper = pp->period - integral_period;
	fiper_pw = netc_timer_calculate_fiper_pw(priv, fiper);

	fiper_ctrl = netc_timer_rd(priv, NETC_TMR_FIPER_CTRL);
	fiper_ctrl &= ~(FIPER_CTRL_DIS(channel) | FIPER_CTRL_PW(channel) |
			FIPER_CTRL_FS_ALARM(channel));
	fiper_ctrl |= FIPER_CTRL_SET_PW(channel, fiper_pw);
	fiper_ctrl |= alarm_id ? FIPER_CTRL_FS_ALARM(channel) : 0;

	priv->tmr_emask |= TMR_TEVENT_ALMEN(alarm_id);

	if (pp->type == NETC_PP_PPS) {
		priv->tmr_emask |= TMR_TEVNET_PPEN(channel);
		netc_timer_set_pps_alarm(priv, channel, integral_period);
	} else {
		netc_timer_set_perout_alarm(priv, channel, integral_period);
	}

	netc_timer_wr(priv, NETC_TMR_TEMASK, priv->tmr_emask);
	netc_timer_wr(priv, NETC_TMR_FIPER(channel), fiper);
	netc_timer_wr(priv, NETC_TMR_FIPER_CTRL, fiper_ctrl);
}

static void netc_timer_disable_periodic_pulse(struct netc_timer *priv,
					      u8 channel)
{
	struct netc_pp *pp = &priv->pp[channel];
	int alarm_id = pp->alarm_id;
	u32 fiper_ctrl;

	if (!pp->enabled)
		return;

	priv->tmr_emask &= ~(TMR_TEVNET_PPEN(channel) |
			     TMR_TEVENT_ALMEN(alarm_id));

	fiper_ctrl = netc_timer_rd(priv, NETC_TMR_FIPER_CTRL);
	fiper_ctrl |= FIPER_CTRL_DIS(channel);

	netc_timer_alarm_write(priv, NETC_TMR_DEFAULT_ALARM, alarm_id);
	netc_timer_wr(priv, NETC_TMR_TEMASK, priv->tmr_emask);
	netc_timer_wr(priv, NETC_TMR_FIPER(channel), NETC_TMR_DEFAULT_FIPER);
	netc_timer_wr(priv, NETC_TMR_FIPER_CTRL, fiper_ctrl);
}

static u8 netc_timer_select_pps_channel(struct netc_timer *priv)
{
	int i;

	for (i = 0; i < NETC_TMR_FIPER_NUM; i++) {
		if (!priv->pp[i].enabled)
			return i;
	}

	return NETC_TMR_INVALID_CHANNEL;
}

/* Note that users should not use this API to output PPS signal on
 * external pins, because PTP_CLK_REQ_PPS trigger internal PPS event
 * for input into kernel PPS subsystem. See:
 * https://lore.kernel.org/r/20201117213826.18235-1-a.fatoum@pengutronix.de
 */
static int netc_timer_enable_pps(struct netc_timer *priv,
				 struct ptp_clock_request *rq, int on)
{
	struct device *dev = &priv->pdev->dev;
	unsigned long flags;
	struct netc_pp *pp;
	int err = 0;

	spin_lock_irqsave(&priv->lock, flags);

	if (on) {
		int alarm_id;
		u8 channel;

		if (priv->pps_channel < NETC_TMR_FIPER_NUM) {
			channel = priv->pps_channel;
		} else {
			channel = netc_timer_select_pps_channel(priv);
			if (channel == NETC_TMR_INVALID_CHANNEL) {
				dev_err(dev, "No available FIPERs\n");
				err = -EBUSY;
				goto unlock_spinlock;
			}
		}

		pp = &priv->pp[channel];
		if (pp->enabled)
			goto unlock_spinlock;

		alarm_id = netc_timer_get_alarm_id(priv);
		if (alarm_id == priv->fs_alarm_num) {
			dev_err(dev, "No available ALARMs\n");
			err = -EBUSY;
			goto unlock_spinlock;
		}

		pp->enabled = true;
		pp->type = NETC_PP_PPS;
		pp->alarm_id = alarm_id;
		pp->period = NSEC_PER_SEC;
		priv->pps_channel = channel;

		netc_timer_enable_periodic_pulse(priv, channel);
	} else {
		/* pps_channel is invalid if PPS is not enabled, so no
		 * processing is needed.
		 */
		if (priv->pps_channel >= NETC_TMR_FIPER_NUM)
			goto unlock_spinlock;

		netc_timer_disable_periodic_pulse(priv, priv->pps_channel);
		pp = &priv->pp[priv->pps_channel];
		priv->fs_alarm_bitmap &= ~BIT(pp->alarm_id);
		memset(pp, 0, sizeof(*pp));
		priv->pps_channel = NETC_TMR_INVALID_CHANNEL;
	}

unlock_spinlock:
	spin_unlock_irqrestore(&priv->lock, flags);

	return err;
}

static int net_timer_enable_perout(struct netc_timer *priv,
				   struct ptp_clock_request *rq, int on)
{
	struct device *dev = &priv->pdev->dev;
	u32 channel = rq->perout.index;
	unsigned long flags;
	struct netc_pp *pp;
	int err = 0;

	spin_lock_irqsave(&priv->lock, flags);

	pp = &priv->pp[channel];
	if (pp->type == NETC_PP_PPS) {
		dev_err(dev, "FIPER%u is being used for PPS\n", channel);
		err = -EBUSY;
		goto unlock_spinlock;
	}

	if (on) {
		u64 period_ns, gclk_period, max_period, min_period;
		struct timespec64 period, stime;
		u32 integral_period;
		int alarm_id;

		period.tv_sec = rq->perout.period.sec;
		period.tv_nsec = rq->perout.period.nsec;
		period_ns = timespec64_to_ns(&period);

		integral_period = netc_timer_get_integral_period(priv);
		max_period = (u64)NETC_TMR_DEFAULT_FIPER + integral_period;
		gclk_period = netc_timer_get_gclk_period(priv);
		min_period = gclk_period * 4 + integral_period;
		if (period_ns > max_period || period_ns < min_period) {
			dev_err(dev, "The period range is %llu ~ %llu\n",
				min_period, max_period);
			err = -EINVAL;
			goto unlock_spinlock;
		}

		if (pp->enabled) {
			alarm_id = pp->alarm_id;
		} else {
			alarm_id = netc_timer_get_alarm_id(priv);
			if (alarm_id == priv->fs_alarm_num) {
				dev_err(dev, "No available ALARMs\n");
				err = -EBUSY;
				goto unlock_spinlock;
			}

			pp->type = NETC_PP_PEROUT;
			pp->enabled = true;
			pp->alarm_id = alarm_id;
		}

		stime.tv_sec = rq->perout.start.sec;
		stime.tv_nsec = rq->perout.start.nsec;
		pp->stime = timespec64_to_ns(&stime);
		pp->period = period_ns;

		netc_timer_enable_periodic_pulse(priv, channel);
	} else {
		netc_timer_disable_periodic_pulse(priv, channel);
		priv->fs_alarm_bitmap &= ~BIT(pp->alarm_id);
		memset(pp, 0, sizeof(*pp));
	}

unlock_spinlock:
	spin_unlock_irqrestore(&priv->lock, flags);

	return err;
}

static void netc_timer_handle_etts_event(struct netc_timer *priv, int index,
					 bool update_event)
{
	struct ptp_clock_event event;
	u32 etts_l = 0, etts_h = 0;

	while (netc_timer_rd(priv, NETC_TMR_STAT) & TMR_STAT_ETS_VLD(index)) {
		etts_l = netc_timer_rd(priv, NETC_TMR_ETTS_L(index));
		etts_h = netc_timer_rd(priv, NETC_TMR_ETTS_H(index));
	}

	/* Invalid time stamp */
	if (!etts_l && !etts_h)
		return;

	if (update_event) {
		event.type = PTP_CLOCK_EXTTS;
		event.index = index;
		event.timestamp = (u64)etts_h << 32;
		event.timestamp |= etts_l;
		ptp_clock_event(priv->clock, &event);
	}
}

static int netc_timer_enable_extts(struct netc_timer *priv,
				   struct ptp_clock_request *rq, int on)
{
	int index = rq->extts.index;
	unsigned long flags;
	u32 tmr_ctrl;

	/* Reject requests to enable time stamping on both edges */
	if ((rq->extts.flags & PTP_EXTTS_EDGES) == PTP_EXTTS_EDGES)
		return -EOPNOTSUPP;

	spin_lock_irqsave(&priv->lock, flags);

	netc_timer_handle_etts_event(priv, rq->extts.index, false);
	if (on) {
		tmr_ctrl = netc_timer_rd(priv, NETC_TMR_CTRL);
		if (rq->extts.flags & PTP_FALLING_EDGE)
			tmr_ctrl |= TMR_ETEP(index);
		else
			tmr_ctrl &= ~TMR_ETEP(index);

		netc_timer_wr(priv, NETC_TMR_CTRL, tmr_ctrl);
		priv->tmr_emask |= TMR_TEVENT_ETS(index);
	} else {
		priv->tmr_emask &= ~TMR_TEVENT_ETS(index);
	}

	netc_timer_wr(priv, NETC_TMR_TEMASK, priv->tmr_emask);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void netc_timer_disable_fiper(struct netc_timer *priv)
{
	u32 fiper_ctrl = netc_timer_rd(priv, NETC_TMR_FIPER_CTRL);
	int i;

	for (i = 0; i < NETC_TMR_FIPER_NUM; i++) {
		if (!priv->pp[i].enabled)
			continue;

		fiper_ctrl |= FIPER_CTRL_DIS(i);
		netc_timer_wr(priv, NETC_TMR_FIPER(i), NETC_TMR_DEFAULT_FIPER);
	}

	netc_timer_wr(priv, NETC_TMR_FIPER_CTRL, fiper_ctrl);
}

static void netc_timer_enable_fiper(struct netc_timer *priv)
{
	u32 integral_period = netc_timer_get_integral_period(priv);
	u32 fiper_ctrl = netc_timer_rd(priv, NETC_TMR_FIPER_CTRL);
	int i;

	for (i = 0; i < NETC_TMR_FIPER_NUM; i++) {
		struct netc_pp *pp = &priv->pp[i];
		u32 fiper;

		if (!pp->enabled)
			continue;

		fiper_ctrl &= ~FIPER_CTRL_DIS(i);

		if (pp->type == NETC_PP_PPS)
			netc_timer_set_pps_alarm(priv, i, integral_period);
		else if (pp->type == NETC_PP_PEROUT)
			netc_timer_set_perout_alarm(priv, i, integral_period);

		fiper = pp->period - integral_period;
		netc_timer_wr(priv, NETC_TMR_FIPER(i), fiper);
	}

	netc_timer_wr(priv, NETC_TMR_FIPER_CTRL, fiper_ctrl);
}

static int netc_timer_enable(struct ptp_clock_info *ptp,
			     struct ptp_clock_request *rq, int on)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);

	switch (rq->type) {
	case PTP_CLK_REQ_PPS:
		return netc_timer_enable_pps(priv, rq, on);
	case PTP_CLK_REQ_PEROUT:
		return net_timer_enable_perout(priv, rq, on);
	case PTP_CLK_REQ_EXTTS:
		return netc_timer_enable_extts(priv, rq, on);
	default:
		return -EOPNOTSUPP;
	}
}

static int netc_timer_perout_loopback(struct ptp_clock_info *ptp,
				      unsigned int index, int on)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	unsigned long flags;
	u32 tmr_ctrl;

	spin_lock_irqsave(&priv->lock, flags);

	tmr_ctrl = netc_timer_rd(priv, NETC_TMR_CTRL);
	if (on)
		tmr_ctrl |= TMR_CTRL_PPL(index);
	else
		tmr_ctrl &= ~TMR_CTRL_PPL(index);

	netc_timer_wr(priv, NETC_TMR_CTRL, tmr_ctrl);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void netc_timer_adjust_period(struct netc_timer *priv, u64 period)
{
	u32 fractional_period = lower_32_bits(period);
	u32 integral_period = upper_32_bits(period);
	u32 tmr_ctrl, old_tmr_ctrl;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	old_tmr_ctrl = netc_timer_rd(priv, NETC_TMR_CTRL);
	tmr_ctrl = u32_replace_bits(old_tmr_ctrl, integral_period,
				    TMR_CTRL_TCLK_PERIOD);
	if (tmr_ctrl != old_tmr_ctrl) {
		netc_timer_disable_fiper(priv);
		netc_timer_wr(priv, NETC_TMR_CTRL, tmr_ctrl);
		netc_timer_enable_fiper(priv);
	}

	netc_timer_wr(priv, NETC_TMR_ADD, fractional_period);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int netc_timer_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	u64 new_period;

	new_period = adjust_by_scaled_ppm(priv->period, scaled_ppm);
	netc_timer_adjust_period(priv, new_period);

	return 0;
}

static int netc_timer_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	unsigned long flags;
	s64 tmr_off;

	spin_lock_irqsave(&priv->lock, flags);

	netc_timer_disable_fiper(priv);

	/* Adjusting TMROFF instead of TMR_CNT is that the timer
	 * counter keeps increasing during reading and writing
	 * TMR_CNT, which will cause latency.
	 */
	tmr_off = netc_timer_offset_read(priv);
	tmr_off += delta;
	netc_timer_offset_write(priv, tmr_off);

	netc_timer_enable_fiper(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int netc_timer_gettimex64(struct ptp_clock_info *ptp,
				 struct timespec64 *ts,
				 struct ptp_system_timestamp *sts)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&priv->lock, flags);

	ptp_read_system_prets(sts);
	ns = netc_timer_cur_time_read(priv);
	ptp_read_system_postts(sts);

	spin_unlock_irqrestore(&priv->lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int netc_timer_settime64(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	struct netc_timer *priv = ptp_to_netc_timer(ptp);
	u64 ns = timespec64_to_ns(ts);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	netc_timer_disable_fiper(priv);
	netc_timer_offset_write(priv, 0);
	netc_timer_cnt_write(priv, ns);
	netc_timer_enable_fiper(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static const struct ptp_clock_info netc_timer_ptp_caps = {
	.owner		= THIS_MODULE,
	.name		= "NETC Timer PTP clock",
	.max_adj	= 500000000,
	.n_pins		= 0,
	.n_alarm	= 2,
	.pps		= 1,
	.n_per_out	= 3,
	.n_ext_ts	= 2,
	.n_per_lp	= 2,
	.supported_extts_flags = PTP_RISING_EDGE | PTP_FALLING_EDGE |
				 PTP_STRICT_FLAGS,
	.adjfine	= netc_timer_adjfine,
	.adjtime	= netc_timer_adjtime,
	.gettimex64	= netc_timer_gettimex64,
	.settime64	= netc_timer_settime64,
	.enable		= netc_timer_enable,
	.perout_loopback = netc_timer_perout_loopback,
};

static void netc_timer_init(struct netc_timer *priv)
{
	u32 fractional_period = lower_32_bits(priv->period);
	u32 integral_period = upper_32_bits(priv->period);
	u32 tmr_ctrl, fiper_ctrl;
	struct timespec64 now;
	u64 ns;
	int i;

	/* Software must enable timer first and the clock selected must be
	 * active, otherwise, the registers which are in the timer clock
	 * domain are not accessible.
	 */
	tmr_ctrl = FIELD_PREP(TMR_CTRL_CK_SEL, priv->clk_select) |
		   TMR_CTRL_TE | TMR_CTRL_FS;
	netc_timer_wr(priv, NETC_TMR_CTRL, tmr_ctrl);
	netc_timer_wr(priv, NETC_TMR_PRSC, priv->oclk_prsc);

	/* Disable FIPER by default */
	fiper_ctrl = netc_timer_rd(priv, NETC_TMR_FIPER_CTRL);
	for (i = 0; i < NETC_TMR_FIPER_NUM; i++) {
		fiper_ctrl |= FIPER_CTRL_DIS(i);
		fiper_ctrl &= ~FIPER_CTRL_PG(i);
	}
	netc_timer_wr(priv, NETC_TMR_FIPER_CTRL, fiper_ctrl);
	netc_timer_wr(priv, NETC_TMR_ECTRL, NETC_TMR_DEFAULT_ETTF_THR);

	ktime_get_real_ts64(&now);
	ns = timespec64_to_ns(&now);
	netc_timer_cnt_write(priv, ns);

	/* Allow atomic writes to TCLK_PERIOD and TMR_ADD, An update to
	 * TCLK_PERIOD does not take effect until TMR_ADD is written.
	 */
	tmr_ctrl |= FIELD_PREP(TMR_CTRL_TCLK_PERIOD, integral_period) |
		    TMR_COMP_MODE;
	netc_timer_wr(priv, NETC_TMR_CTRL, tmr_ctrl);
	netc_timer_wr(priv, NETC_TMR_ADD, fractional_period);
}

static int netc_timer_pci_probe(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct netc_timer *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	pcie_flr(pdev);
	err = pci_enable_device_mem(pdev);
	if (err)
		return dev_err_probe(dev, err, "Failed to enable device\n");

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	err = pci_request_mem_regions(pdev, KBUILD_MODNAME);
	if (err) {
		dev_err(dev, "pci_request_regions() failed, err:%pe\n",
			ERR_PTR(err));
		goto disable_dev;
	}

	pci_set_master(pdev);

	priv->pdev = pdev;
	priv->base = pci_ioremap_bar(pdev, NETC_TMR_REGS_BAR);
	if (!priv->base) {
		err = -ENOMEM;
		goto release_mem_regions;
	}

	pci_set_drvdata(pdev, priv);

	return 0;

release_mem_regions:
	pci_release_mem_regions(pdev);
disable_dev:
	pci_disable_device(pdev);

	return err;
}

static void netc_timer_pci_remove(struct pci_dev *pdev)
{
	struct netc_timer *priv = pci_get_drvdata(pdev);

	iounmap(priv->base);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

static int netc_timer_get_reference_clk_source(struct netc_timer *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct clk *clk;
	int i;

	/* Select NETC system clock as the reference clock by default */
	priv->clk_select = NETC_TMR_SYSTEM_CLK;
	priv->clk_freq = NETC_TMR_SYSCLK_333M;

	/* Update the clock source of the reference clock if the clock
	 * is specified in DT node.
	 */
	for (i = 0; i < ARRAY_SIZE(timer_clk_src); i++) {
		clk = devm_clk_get_optional_enabled(dev, timer_clk_src[i]);
		if (IS_ERR(clk))
			return dev_err_probe(dev, PTR_ERR(clk),
					     "Failed to enable clock\n");

		if (clk) {
			priv->clk_freq = clk_get_rate(clk);
			priv->clk_select = i ? NETC_TMR_EXT_OSC :
					       NETC_TMR_CCM_TIMER1;
			break;
		}
	}

	/* The period is a 64-bit number, the high 32-bit is the integer
	 * part of the period, the low 32-bit is the fractional part of
	 * the period. In order to get the desired 32-bit fixed-point
	 * format, multiply the numerator of the fraction by 2^32.
	 */
	priv->period = div_u64((u64)NSEC_PER_SEC << 32, priv->clk_freq);

	return 0;
}

static int netc_timer_parse_dt(struct netc_timer *priv)
{
	return netc_timer_get_reference_clk_source(priv);
}

static irqreturn_t netc_timer_isr(int irq, void *data)
{
	struct netc_timer *priv = data;
	struct ptp_clock_event event;
	u32 tmr_event;

	spin_lock(&priv->lock);

	tmr_event = netc_timer_rd(priv, NETC_TMR_TEVENT);
	tmr_event &= priv->tmr_emask;
	/* Clear interrupts status */
	netc_timer_wr(priv, NETC_TMR_TEVENT, tmr_event);

	if (tmr_event & TMR_TEVENT_ALMEN(0))
		netc_timer_alarm_write(priv, NETC_TMR_DEFAULT_ALARM, 0);

	if (tmr_event & TMR_TEVENT_ALMEN(1))
		netc_timer_alarm_write(priv, NETC_TMR_DEFAULT_ALARM, 1);

	if (tmr_event & TMR_TEVENT_PPEN_ALL) {
		event.type = PTP_CLOCK_PPS;
		ptp_clock_event(priv->clock, &event);
	}

	if (tmr_event & TMR_TEVENT_ETS(0))
		netc_timer_handle_etts_event(priv, 0, true);

	if (tmr_event & TMR_TEVENT_ETS(1))
		netc_timer_handle_etts_event(priv, 1, true);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static int netc_timer_init_msix_irq(struct netc_timer *priv)
{
	struct pci_dev *pdev = priv->pdev;
	int err, n;

	n = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX);
	if (n != 1) {
		err = (n < 0) ? n : -EPERM;
		dev_err(&pdev->dev, "pci_alloc_irq_vectors() failed\n");
		return err;
	}

	priv->irq = pci_irq_vector(pdev, 0);
	err = request_irq(priv->irq, netc_timer_isr, 0, priv->irq_name, priv);
	if (err) {
		dev_err(&pdev->dev, "request_irq() failed\n");
		pci_free_irq_vectors(pdev);

		return err;
	}

	return 0;
}

static void netc_timer_free_msix_irq(struct netc_timer *priv)
{
	struct pci_dev *pdev = priv->pdev;

	disable_irq(priv->irq);
	free_irq(priv->irq, priv);
	pci_free_irq_vectors(pdev);
}

static int netc_timer_get_global_ip_rev(struct netc_timer *priv)
{
	u32 val;

	val = netc_timer_rd(priv, NETC_GLOBAL_OFFSET + NETC_GLOBAL_IPBRR0);

	return val & IPBRR0_IP_REV;
}

static int netc_timer_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct netc_timer *priv;
	int err;

	err = netc_timer_pci_probe(pdev);
	if (err)
		return err;

	priv = pci_get_drvdata(pdev);
	priv->revision = netc_timer_get_global_ip_rev(priv);
	if (priv->revision == NETC_REV_4_1)
		priv->fs_alarm_num = 1;
	else
		priv->fs_alarm_num = NETC_TMR_ALARM_NUM;

	err = netc_timer_parse_dt(priv);
	if (err)
		goto timer_pci_remove;

	priv->caps = netc_timer_ptp_caps;
	priv->oclk_prsc = NETC_TMR_DEFAULT_PRSC;
	priv->pps_channel = NETC_TMR_INVALID_CHANNEL;
	spin_lock_init(&priv->lock);
	snprintf(priv->irq_name, sizeof(priv->irq_name), "ptp-netc %s",
		 pci_name(pdev));

	err = netc_timer_init_msix_irq(priv);
	if (err)
		goto timer_pci_remove;

	netc_timer_init(priv);
	priv->clock = ptp_clock_register(&priv->caps, dev);
	if (IS_ERR(priv->clock)) {
		err = PTR_ERR(priv->clock);
		goto free_msix_irq;
	}

	return 0;

free_msix_irq:
	netc_timer_free_msix_irq(priv);
timer_pci_remove:
	netc_timer_pci_remove(pdev);

	return err;
}

static void netc_timer_remove(struct pci_dev *pdev)
{
	struct netc_timer *priv = pci_get_drvdata(pdev);

	netc_timer_wr(priv, NETC_TMR_TEMASK, 0);
	netc_timer_wr(priv, NETC_TMR_CTRL, 0);
	ptp_clock_unregister(priv->clock);
	netc_timer_free_msix_irq(priv);
	netc_timer_pci_remove(pdev);
}

static const struct pci_device_id netc_timer_id_table[] = {
	{ PCI_DEVICE(NETC_TMR_PCI_VENDOR_NXP, 0xee02) },
	{ }
};
MODULE_DEVICE_TABLE(pci, netc_timer_id_table);

static struct pci_driver netc_timer_driver = {
	.name = KBUILD_MODNAME,
	.id_table = netc_timer_id_table,
	.probe = netc_timer_probe,
	.remove = netc_timer_remove,
};
module_pci_driver(netc_timer_driver);

MODULE_DESCRIPTION("NXP NETC Timer PTP Driver");
MODULE_LICENSE("Dual BSD/GPL");
