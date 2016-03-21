/* PTP 1588 clock using the Renesas Ethernet AVB
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 * Copyright (C) 2015 Renesas Solutions Corp.
 * Copyright (C) 2015-2016 Cogent Embedded, Inc. <source@cogentembedded.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "ravb.h"

static int ravb_ptp_tcr_request(struct ravb_private *priv, u32 request)
{
	struct net_device *ndev = priv->ndev;
	int error;

	error = ravb_wait(ndev, GCCR, GCCR_TCR, GCCR_TCR_NOREQ);
	if (error)
		return error;

	ravb_modify(ndev, GCCR, request, request);
	return ravb_wait(ndev, GCCR, GCCR_TCR, GCCR_TCR_NOREQ);
}

/* Caller must hold the lock */
static int ravb_ptp_time_read(struct ravb_private *priv, struct timespec64 *ts)
{
	struct net_device *ndev = priv->ndev;
	int error;

	error = ravb_ptp_tcr_request(priv, GCCR_TCR_CAPTURE);
	if (error)
		return error;

	ts->tv_nsec = ravb_read(ndev, GCT0);
	ts->tv_sec  = ravb_read(ndev, GCT1) |
		((s64)ravb_read(ndev, GCT2) << 32);

	return 0;
}

/* Caller must hold the lock */
static int ravb_ptp_time_write(struct ravb_private *priv,
				const struct timespec64 *ts)
{
	struct net_device *ndev = priv->ndev;
	int error;
	u32 gccr;

	error = ravb_ptp_tcr_request(priv, GCCR_TCR_RESET);
	if (error)
		return error;

	gccr = ravb_read(ndev, GCCR);
	if (gccr & GCCR_LTO)
		return -EBUSY;
	ravb_write(ndev, ts->tv_nsec, GTO0);
	ravb_write(ndev, ts->tv_sec,  GTO1);
	ravb_write(ndev, (ts->tv_sec >> 32) & 0xffff, GTO2);
	ravb_write(ndev, gccr | GCCR_LTO, GCCR);

	return 0;
}

/* Caller must hold the lock */
static int ravb_ptp_update_compare(struct ravb_private *priv, u32 ns)
{
	struct net_device *ndev = priv->ndev;
	/* When the comparison value (GPTC.PTCV) is in range of
	 * [x-1 to x+1] (x is the configured increment value in
	 * GTI.TIV), it may happen that a comparison match is
	 * not detected when the timer wraps around.
	 */
	u32 gti_ns_plus_1 = (priv->ptp.current_addend >> 20) + 1;
	u32 gccr;

	if (ns < gti_ns_plus_1)
		ns = gti_ns_plus_1;
	else if (ns > 0 - gti_ns_plus_1)
		ns = 0 - gti_ns_plus_1;

	gccr = ravb_read(ndev, GCCR);
	if (gccr & GCCR_LPTC)
		return -EBUSY;
	ravb_write(ndev, ns, GPTC);
	ravb_write(ndev, gccr | GCCR_LPTC, GCCR);

	return 0;
}

/* PTP clock operations */
static int ravb_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct ravb_private *priv = container_of(ptp, struct ravb_private,
						 ptp.info);
	struct net_device *ndev = priv->ndev;
	unsigned long flags;
	u32 diff, addend;
	bool neg_adj = false;
	u32 gccr;

	if (ppb < 0) {
		neg_adj = true;
		ppb = -ppb;
	}
	addend = priv->ptp.default_addend;
	diff = div_u64((u64)addend * ppb, NSEC_PER_SEC);

	addend = neg_adj ? addend - diff : addend + diff;

	spin_lock_irqsave(&priv->lock, flags);

	priv->ptp.current_addend = addend;

	gccr = ravb_read(ndev, GCCR);
	if (gccr & GCCR_LTI) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return -EBUSY;
	}
	ravb_write(ndev, addend & GTI_TIV, GTI);
	ravb_write(ndev, gccr | GCCR_LTI, GCCR);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int ravb_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ravb_private *priv = container_of(ptp, struct ravb_private,
						 ptp.info);
	struct timespec64 ts;
	unsigned long flags;
	int error;

	spin_lock_irqsave(&priv->lock, flags);
	error = ravb_ptp_time_read(priv, &ts);
	if (!error) {
		u64 now = ktime_to_ns(timespec64_to_ktime(ts));

		ts = ns_to_timespec64(now + delta);
		error = ravb_ptp_time_write(priv, &ts);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return error;
}

static int ravb_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ravb_private *priv = container_of(ptp, struct ravb_private,
						 ptp.info);
	unsigned long flags;
	int error;

	spin_lock_irqsave(&priv->lock, flags);
	error = ravb_ptp_time_read(priv, ts);
	spin_unlock_irqrestore(&priv->lock, flags);

	return error;
}

static int ravb_ptp_settime64(struct ptp_clock_info *ptp,
			      const struct timespec64 *ts)
{
	struct ravb_private *priv = container_of(ptp, struct ravb_private,
						 ptp.info);
	unsigned long flags;
	int error;

	spin_lock_irqsave(&priv->lock, flags);
	error = ravb_ptp_time_write(priv, ts);
	spin_unlock_irqrestore(&priv->lock, flags);

	return error;
}

static int ravb_ptp_extts(struct ptp_clock_info *ptp,
			  struct ptp_extts_request *req, int on)
{
	struct ravb_private *priv = container_of(ptp, struct ravb_private,
						 ptp.info);
	struct net_device *ndev = priv->ndev;
	unsigned long flags;

	if (req->index)
		return -EINVAL;

	if (priv->ptp.extts[req->index] == on)
		return 0;
	priv->ptp.extts[req->index] = on;

	spin_lock_irqsave(&priv->lock, flags);
	ravb_modify(ndev, GIC, GIC_PTCE, on ? GIC_PTCE : 0);
	mmiowb();
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int ravb_ptp_perout(struct ptp_clock_info *ptp,
			   struct ptp_perout_request *req, int on)
{
	struct ravb_private *priv = container_of(ptp, struct ravb_private,
						 ptp.info);
	struct net_device *ndev = priv->ndev;
	struct ravb_ptp_perout *perout;
	unsigned long flags;
	int error = 0;

	if (req->index)
		return -EINVAL;

	if (on) {
		u64 start_ns;
		u64 period_ns;

		start_ns = req->start.sec * NSEC_PER_SEC + req->start.nsec;
		period_ns = req->period.sec * NSEC_PER_SEC + req->period.nsec;

		if (start_ns > U32_MAX) {
			netdev_warn(ndev,
				    "ptp: start value (nsec) is over limit. Maximum size of start is only 32 bits\n");
			return -ERANGE;
		}

		if (period_ns > U32_MAX) {
			netdev_warn(ndev,
				    "ptp: period value (nsec) is over limit. Maximum size of period is only 32 bits\n");
			return -ERANGE;
		}

		spin_lock_irqsave(&priv->lock, flags);

		perout = &priv->ptp.perout[req->index];
		perout->target = (u32)start_ns;
		perout->period = (u32)period_ns;
		error = ravb_ptp_update_compare(priv, (u32)start_ns);
		if (!error) {
			/* Unmask interrupt */
			ravb_modify(ndev, GIC, GIC_PTME, GIC_PTME);
		}
	} else	{
		spin_lock_irqsave(&priv->lock, flags);

		perout = &priv->ptp.perout[req->index];
		perout->period = 0;

		/* Mask interrupt */
		ravb_modify(ndev, GIC, GIC_PTME, 0);
	}
	mmiowb();
	spin_unlock_irqrestore(&priv->lock, flags);

	return error;
}

static int ravb_ptp_enable(struct ptp_clock_info *ptp,
			   struct ptp_clock_request *req, int on)
{
	switch (req->type) {
	case PTP_CLK_REQ_EXTTS:
		return ravb_ptp_extts(ptp, &req->extts, on);
	case PTP_CLK_REQ_PEROUT:
		return ravb_ptp_perout(ptp, &req->perout, on);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ptp_clock_info ravb_ptp_info = {
	.owner		= THIS_MODULE,
	.name		= "ravb clock",
	.max_adj	= 50000000,
	.n_ext_ts	= N_EXT_TS,
	.n_per_out	= N_PER_OUT,
	.adjfreq	= ravb_ptp_adjfreq,
	.adjtime	= ravb_ptp_adjtime,
	.gettime64	= ravb_ptp_gettime64,
	.settime64	= ravb_ptp_settime64,
	.enable		= ravb_ptp_enable,
};

/* Caller must hold the lock */
irqreturn_t ravb_ptp_interrupt(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 gis = ravb_read(ndev, GIS);

	gis &= ravb_read(ndev, GIC);
	if (gis & GIS_PTCF) {
		struct ptp_clock_event event;

		event.type = PTP_CLOCK_EXTTS;
		event.index = 0;
		event.timestamp = ravb_read(ndev, GCPT);
		ptp_clock_event(priv->ptp.clock, &event);
	}
	if (gis & GIS_PTMF) {
		struct ravb_ptp_perout *perout = priv->ptp.perout;

		if (perout->period) {
			perout->target += perout->period;
			ravb_ptp_update_compare(priv, perout->target);
		}
	}

	if (gis) {
		ravb_write(ndev, ~gis, GIS);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

void ravb_ptp_init(struct net_device *ndev, struct platform_device *pdev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned long flags;

	priv->ptp.info = ravb_ptp_info;

	priv->ptp.default_addend = ravb_read(ndev, GTI);
	priv->ptp.current_addend = priv->ptp.default_addend;

	spin_lock_irqsave(&priv->lock, flags);
	ravb_wait(ndev, GCCR, GCCR_TCR, GCCR_TCR_NOREQ);
	ravb_modify(ndev, GCCR, GCCR_TCSS, GCCR_TCSS_ADJGPTP);
	mmiowb();
	spin_unlock_irqrestore(&priv->lock, flags);

	priv->ptp.clock = ptp_clock_register(&priv->ptp.info, &pdev->dev);
}

void ravb_ptp_stop(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);

	ravb_write(ndev, 0, GIC);
	ravb_write(ndev, 0, GIS);

	ptp_clock_unregister(priv->ptp.clock);
}
