// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PTP 1588 clock support - character device implementation.
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */
#include <linux/compat.h>
#include <linux/module.h>
#include <linux/posix-clock.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/debugfs.h>

#include <linux/nospec.h>

#include "ptp_private.h"

static int ptp_disable_pinfunc(struct ptp_clock_info *ops,
			       enum ptp_pin_function func, unsigned int chan)
{
	struct ptp_clock_request rq;
	int err = 0;

	memset(&rq, 0, sizeof(rq));

	switch (func) {
	case PTP_PF_NONE:
		break;
	case PTP_PF_EXTTS:
		rq.type = PTP_CLK_REQ_EXTTS;
		rq.extts.index = chan;
		err = ops->enable(ops, &rq, 0);
		break;
	case PTP_PF_PEROUT:
		rq.type = PTP_CLK_REQ_PEROUT;
		rq.perout.index = chan;
		err = ops->enable(ops, &rq, 0);
		break;
	case PTP_PF_PHYSYNC:
		break;
	default:
		return -EINVAL;
	}

	return err;
}

void ptp_disable_all_events(struct ptp_clock *ptp)
{
	struct ptp_clock_info *info = ptp->info;
	unsigned int i;

	mutex_lock(&ptp->pincfg_mux);
	/* Disable any pins that may raise EXTTS events */
	for (i = 0; i < info->n_pins; i++)
		if (info->pin_config[i].func == PTP_PF_EXTTS)
			ptp_disable_pinfunc(info, info->pin_config[i].func,
					    info->pin_config[i].chan);

	/* Disable the PPS event if the driver has PPS support */
	if (info->pps) {
		struct ptp_clock_request req = { .type = PTP_CLK_REQ_PPS };
		info->enable(info, &req, 0);
	}
	mutex_unlock(&ptp->pincfg_mux);
}

int ptp_set_pinfunc(struct ptp_clock *ptp, unsigned int pin,
		    enum ptp_pin_function func, unsigned int chan)
{
	struct ptp_clock_info *info = ptp->info;
	struct ptp_pin_desc *pin1 = NULL, *pin2 = &info->pin_config[pin];
	unsigned int i;

	/* Check to see if any other pin previously had this function. */
	for (i = 0; i < info->n_pins; i++) {
		if (info->pin_config[i].func == func &&
		    info->pin_config[i].chan == chan) {
			pin1 = &info->pin_config[i];
			break;
		}
	}
	if (pin1 && i == pin)
		return 0;

	/* Check the desired function and channel. */
	switch (func) {
	case PTP_PF_NONE:
		break;
	case PTP_PF_EXTTS:
		if (chan >= info->n_ext_ts)
			return -EINVAL;
		break;
	case PTP_PF_PEROUT:
		if (chan >= info->n_per_out)
			return -EINVAL;
		break;
	case PTP_PF_PHYSYNC:
		if (chan != 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (info->verify(info, pin, func, chan)) {
		pr_err("driver cannot use function %u and channel %u on pin %u\n",
		       func, chan, pin);
		return -EOPNOTSUPP;
	}

	/* Disable whichever pin was previously assigned to this function and
	 * channel.
	 */
	if (pin1) {
		ptp_disable_pinfunc(info, func, chan);
		pin1->func = PTP_PF_NONE;
		pin1->chan = 0;
	}

	/* Disable whatever function was previously assigned to the requested
	 * pin.
	 */
	ptp_disable_pinfunc(info, pin2->func, pin2->chan);
	pin2->func = func;
	pin2->chan = chan;

	return 0;
}

int ptp_open(struct posix_clock_context *pccontext, fmode_t fmode)
{
	struct ptp_clock *ptp = container_of(pccontext->clk, struct ptp_clock, clock);
	struct timestamp_event_queue *queue;
	char debugfsname[32];

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -EINVAL;
	queue->mask = bitmap_alloc(PTP_MAX_CHANNELS, GFP_KERNEL);
	if (!queue->mask) {
		kfree(queue);
		return -EINVAL;
	}
	bitmap_set(queue->mask, 0, PTP_MAX_CHANNELS);
	spin_lock_init(&queue->lock);
	scoped_guard(spinlock_irq, &ptp->tsevqs_lock)
		list_add_tail(&queue->qlist, &ptp->tsevqs);
	pccontext->private_clkdata = queue;

	/* Debugfs contents */
	sprintf(debugfsname, "0x%p", queue);
	queue->debugfs_instance =
		debugfs_create_dir(debugfsname, ptp->debugfs_root);
	queue->dfs_bitmap.array = (u32 *)queue->mask;
	queue->dfs_bitmap.n_elements =
		DIV_ROUND_UP(PTP_MAX_CHANNELS, BITS_PER_BYTE * sizeof(u32));
	debugfs_create_u32_array("mask", 0444, queue->debugfs_instance,
				 &queue->dfs_bitmap);

	return 0;
}

int ptp_release(struct posix_clock_context *pccontext)
{
	struct timestamp_event_queue *queue = pccontext->private_clkdata;
	struct ptp_clock *ptp =
		container_of(pccontext->clk, struct ptp_clock, clock);

	debugfs_remove(queue->debugfs_instance);
	pccontext->private_clkdata = NULL;
	scoped_guard(spinlock_irq, &ptp->tsevqs_lock)
		list_del(&queue->qlist);
	bitmap_free(queue->mask);
	kfree(queue);
	return 0;
}

static long ptp_clock_getcaps(struct ptp_clock *ptp, void __user *arg)
{
	struct ptp_clock_caps caps = {
		.max_adj		= ptp->info->max_adj,
		.n_alarm		= ptp->info->n_alarm,
		.n_ext_ts		= ptp->info->n_ext_ts,
		.n_per_out		= ptp->info->n_per_out,
		.pps			= ptp->info->pps,
		.n_pins			= ptp->info->n_pins,
		.cross_timestamping	= ptp->info->getcrosststamp != NULL,
		.adjust_phase		= ptp->info->adjphase != NULL &&
					  ptp->info->getmaxphase != NULL,
	};

	if (caps.adjust_phase)
		caps.max_phase_adj = ptp->info->getmaxphase(ptp->info);

	return copy_to_user(arg, &caps, sizeof(caps)) ? -EFAULT : 0;
}

static long ptp_extts_request(struct ptp_clock *ptp, unsigned int cmd, void __user *arg)
{
	struct ptp_clock_request req = { .type = PTP_CLK_REQ_EXTTS };
	struct ptp_clock_info *ops = ptp->info;
	unsigned int supported_extts_flags;

	if (copy_from_user(&req.extts, arg, sizeof(req.extts)))
		return -EFAULT;

	if (cmd == PTP_EXTTS_REQUEST2) {
		/* Tell the drivers to check the flags carefully. */
		req.extts.flags |= PTP_STRICT_FLAGS;
		/* Make sure no reserved bit is set. */
		if ((req.extts.flags & ~PTP_EXTTS_VALID_FLAGS) ||
		    req.extts.rsv[0] || req.extts.rsv[1])
			return -EINVAL;

		/* Ensure one of the rising/falling edge bits is set. */
		if ((req.extts.flags & PTP_ENABLE_FEATURE) &&
		    (req.extts.flags & PTP_EXTTS_EDGES) == 0)
			return -EINVAL;
	} else {
		req.extts.flags &= PTP_EXTTS_V1_VALID_FLAGS;
		memset(req.extts.rsv, 0, sizeof(req.extts.rsv));
	}

	if (req.extts.index >= ops->n_ext_ts)
		return -EINVAL;

	supported_extts_flags = ptp->info->supported_extts_flags;
	/* The PTP_ENABLE_FEATURE flag is always supported. */
	supported_extts_flags |= PTP_ENABLE_FEATURE;
	/* If the driver does not support strictly checking flags, the
	 * PTP_RISING_EDGE and PTP_FALLING_EDGE flags are merely hints
	 * which are not enforced.
	 */
	if (!(supported_extts_flags & PTP_STRICT_FLAGS))
		supported_extts_flags |= PTP_EXTTS_EDGES;
	/* Reject unsupported flags */
	if (req.extts.flags & ~supported_extts_flags)
		return -EOPNOTSUPP;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &ptp->pincfg_mux)
		return ops->enable(ops, &req, req.extts.flags & PTP_ENABLE_FEATURE ? 1 : 0);
}

static long ptp_perout_request(struct ptp_clock *ptp, unsigned int cmd, void __user *arg)
{
	struct ptp_clock_request req = { .type = PTP_CLK_REQ_PEROUT };
	struct ptp_perout_request *perout = &req.perout;
	struct ptp_clock_info *ops = ptp->info;

	if (copy_from_user(perout, arg, sizeof(*perout)))
		return -EFAULT;

	if (cmd == PTP_PEROUT_REQUEST2) {
		if (perout->flags & ~PTP_PEROUT_VALID_FLAGS)
			return -EINVAL;

		/*
		 * The "on" field has undefined meaning if
		 * PTP_PEROUT_DUTY_CYCLE isn't set, we must still treat it
		 * as reserved, which must be set to zero.
		 */
		if (!(perout->flags & PTP_PEROUT_DUTY_CYCLE) &&
		    !mem_is_zero(perout->rsv, sizeof(perout->rsv)))
			return -EINVAL;

		if (perout->flags & PTP_PEROUT_DUTY_CYCLE) {
			/* The duty cycle must be subunitary. */
			if (perout->on.sec > perout->period.sec ||
			    (perout->on.sec == perout->period.sec &&
			     perout->on.nsec > perout->period.nsec))
				return -ERANGE;
		}

		if (perout->flags & PTP_PEROUT_PHASE) {
			/*
			 * The phase should be specified modulo the period,
			 * therefore anything equal or larger than 1 period
			 * is invalid.
			 */
			if (perout->phase.sec > perout->period.sec ||
			    (perout->phase.sec == perout->period.sec &&
			     perout->phase.nsec >= perout->period.nsec))
				return -ERANGE;
		}
	} else {
		perout->flags &= PTP_PEROUT_V1_VALID_FLAGS;
		memset(perout->rsv, 0, sizeof(perout->rsv));
	}

	if (perout->index >= ops->n_per_out)
		return -EINVAL;
	if (perout->flags & ~ops->supported_perout_flags)
		return -EOPNOTSUPP;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &ptp->pincfg_mux)
		return ops->enable(ops, &req, perout->period.sec || perout->period.nsec);
}

static long ptp_enable_pps(struct ptp_clock *ptp, bool enable)
{
	struct ptp_clock_request req = { .type = PTP_CLK_REQ_PPS };
	struct ptp_clock_info *ops = ptp->info;

	if (!capable(CAP_SYS_TIME))
		return -EPERM;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &ptp->pincfg_mux)
		return ops->enable(ops, &req, enable);
}

typedef int (*ptp_crosststamp_fn)(struct ptp_clock_info *,
				  struct system_device_crosststamp *);

static long ptp_sys_offset_precise(struct ptp_clock *ptp, void __user *arg,
				   ptp_crosststamp_fn crosststamp_fn)
{
	struct ptp_sys_offset_precise precise_offset;
	struct system_device_crosststamp xtstamp;
	struct timespec64 ts;
	int err;

	if (!crosststamp_fn)
		return -EOPNOTSUPP;

	err = crosststamp_fn(ptp->info, &xtstamp);
	if (err)
		return err;

	memset(&precise_offset, 0, sizeof(precise_offset));
	ts = ktime_to_timespec64(xtstamp.device);
	precise_offset.device.sec = ts.tv_sec;
	precise_offset.device.nsec = ts.tv_nsec;
	ts = ktime_to_timespec64(xtstamp.sys_realtime);
	precise_offset.sys_realtime.sec = ts.tv_sec;
	precise_offset.sys_realtime.nsec = ts.tv_nsec;
	ts = ktime_to_timespec64(xtstamp.sys_monoraw);
	precise_offset.sys_monoraw.sec = ts.tv_sec;
	precise_offset.sys_monoraw.nsec = ts.tv_nsec;

	return copy_to_user(arg, &precise_offset, sizeof(precise_offset)) ? -EFAULT : 0;
}

typedef int (*ptp_gettimex_fn)(struct ptp_clock_info *,
			       struct timespec64 *,
			       struct ptp_system_timestamp *);

static long ptp_sys_offset_extended(struct ptp_clock *ptp, void __user *arg,
				    ptp_gettimex_fn gettimex_fn)
{
	struct ptp_sys_offset_extended *extoff __free(kfree) = NULL;
	struct ptp_system_timestamp sts;

	if (!gettimex_fn)
		return -EOPNOTSUPP;

	extoff = memdup_user(arg, sizeof(*extoff));
	if (IS_ERR(extoff))
		return PTR_ERR(extoff);

	if (extoff->n_samples > PTP_MAX_SAMPLES || extoff->rsv[0] || extoff->rsv[1])
		return -EINVAL;

	switch (extoff->clockid) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		break;
	case CLOCK_AUX ... CLOCK_AUX_LAST:
		if (IS_ENABLED(CONFIG_POSIX_AUX_CLOCKS))
			break;
		fallthrough;
	default:
		return -EINVAL;
	}

	sts.clockid = extoff->clockid;
	for (unsigned int i = 0; i < extoff->n_samples; i++) {
		struct timespec64 ts;
		int err;

		err = gettimex_fn(ptp->info, &ts, &sts);
		if (err)
			return err;

		/* Filter out disabled or unavailable clocks */
		if (sts.pre_ts.tv_sec < 0 || sts.post_ts.tv_sec < 0)
			return -EINVAL;

		extoff->ts[i][0].sec = sts.pre_ts.tv_sec;
		extoff->ts[i][0].nsec = sts.pre_ts.tv_nsec;
		extoff->ts[i][1].sec = ts.tv_sec;
		extoff->ts[i][1].nsec = ts.tv_nsec;
		extoff->ts[i][2].sec = sts.post_ts.tv_sec;
		extoff->ts[i][2].nsec = sts.post_ts.tv_nsec;
	}

	return copy_to_user(arg, extoff, sizeof(*extoff)) ? -EFAULT : 0;
}

static long ptp_sys_offset(struct ptp_clock *ptp, void __user *arg)
{
	struct ptp_sys_offset *sysoff __free(kfree) = NULL;
	struct ptp_clock_time *pct;
	struct timespec64 ts;

	sysoff = memdup_user(arg, sizeof(*sysoff));
	if (IS_ERR(sysoff))
		return PTR_ERR(sysoff);

	if (sysoff->n_samples > PTP_MAX_SAMPLES)
		return -EINVAL;

	pct = &sysoff->ts[0];
	for (unsigned int i = 0; i < sysoff->n_samples; i++) {
		struct ptp_clock_info *ops = ptp->info;
		int err;

		ktime_get_real_ts64(&ts);
		pct->sec = ts.tv_sec;
		pct->nsec = ts.tv_nsec;
		pct++;
		if (ops->gettimex64)
			err = ops->gettimex64(ops, &ts, NULL);
		else
			err = ops->gettime64(ops, &ts);
		if (err)
			return err;
		pct->sec = ts.tv_sec;
		pct->nsec = ts.tv_nsec;
		pct++;
	}
	ktime_get_real_ts64(&ts);
	pct->sec = ts.tv_sec;
	pct->nsec = ts.tv_nsec;

	return copy_to_user(arg, sysoff, sizeof(*sysoff)) ? -EFAULT : 0;
}

static long ptp_pin_getfunc(struct ptp_clock *ptp, unsigned int cmd, void __user *arg)
{
	struct ptp_clock_info *ops = ptp->info;
	struct ptp_pin_desc pd;

	if (copy_from_user(&pd, arg, sizeof(pd)))
		return -EFAULT;

	if (cmd == PTP_PIN_GETFUNC2 && !mem_is_zero(pd.rsv, sizeof(pd.rsv)))
		return -EINVAL;

	if (pd.index >= ops->n_pins)
		return -EINVAL;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &ptp->pincfg_mux)
		pd = ops->pin_config[array_index_nospec(pd.index, ops->n_pins)];

	return copy_to_user(arg, &pd, sizeof(pd)) ? -EFAULT : 0;
}

static long ptp_pin_setfunc(struct ptp_clock *ptp, unsigned int cmd, void __user *arg)
{
	struct ptp_clock_info *ops = ptp->info;
	struct ptp_pin_desc pd;
	unsigned int pin_index;

	if (copy_from_user(&pd, arg, sizeof(pd)))
		return -EFAULT;

	if (cmd == PTP_PIN_SETFUNC2 && !mem_is_zero(pd.rsv, sizeof(pd.rsv)))
		return -EINVAL;

	if (pd.index >= ops->n_pins)
		return -EINVAL;

	pin_index = array_index_nospec(pd.index, ops->n_pins);
	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &ptp->pincfg_mux)
		return ptp_set_pinfunc(ptp, pin_index, pd.func, pd.chan);
}

static long ptp_mask_clear_all(struct timestamp_event_queue *tsevq)
{
	bitmap_clear(tsevq->mask, 0, PTP_MAX_CHANNELS);
	return 0;
}

static long ptp_mask_en_single(struct timestamp_event_queue *tsevq, void __user *arg)
{
	unsigned int channel;

	if (copy_from_user(&channel, arg, sizeof(channel)))
		return -EFAULT;
	if (channel >= PTP_MAX_CHANNELS)
		return -EFAULT;
	set_bit(channel, tsevq->mask);
	return 0;
}

long ptp_ioctl(struct posix_clock_context *pccontext, unsigned int cmd,
	       unsigned long arg)
{
	struct ptp_clock *ptp = container_of(pccontext->clk, struct ptp_clock, clock);
	void __user *argptr;

	if (in_compat_syscall() && cmd != PTP_ENABLE_PPS && cmd != PTP_ENABLE_PPS2)
		arg = (unsigned long)compat_ptr(arg);
	argptr = (void __force __user *)arg;

	switch (cmd) {
	case PTP_CLOCK_GETCAPS:
	case PTP_CLOCK_GETCAPS2:
		return ptp_clock_getcaps(ptp, argptr);

	case PTP_EXTTS_REQUEST:
	case PTP_EXTTS_REQUEST2:
		if ((pccontext->fp->f_mode & FMODE_WRITE) == 0)
			return -EACCES;
		return ptp_extts_request(ptp, cmd, argptr);

	case PTP_PEROUT_REQUEST:
	case PTP_PEROUT_REQUEST2:
		if ((pccontext->fp->f_mode & FMODE_WRITE) == 0)
			return -EACCES;
		return ptp_perout_request(ptp, cmd, argptr);

	case PTP_ENABLE_PPS:
	case PTP_ENABLE_PPS2:
		if ((pccontext->fp->f_mode & FMODE_WRITE) == 0)
			return -EACCES;
		return ptp_enable_pps(ptp, !!arg);

	case PTP_SYS_OFFSET_PRECISE:
	case PTP_SYS_OFFSET_PRECISE2:
		return ptp_sys_offset_precise(ptp, argptr,
					      ptp->info->getcrosststamp);

	case PTP_SYS_OFFSET_EXTENDED:
	case PTP_SYS_OFFSET_EXTENDED2:
		return ptp_sys_offset_extended(ptp, argptr,
					       ptp->info->gettimex64);

	case PTP_SYS_OFFSET:
	case PTP_SYS_OFFSET2:
		return ptp_sys_offset(ptp, argptr);

	case PTP_PIN_GETFUNC:
	case PTP_PIN_GETFUNC2:
		return ptp_pin_getfunc(ptp, cmd, argptr);

	case PTP_PIN_SETFUNC:
	case PTP_PIN_SETFUNC2:
		if ((pccontext->fp->f_mode & FMODE_WRITE) == 0)
			return -EACCES;
		return ptp_pin_setfunc(ptp, cmd, argptr);

	case PTP_MASK_CLEAR_ALL:
		return ptp_mask_clear_all(pccontext->private_clkdata);

	case PTP_MASK_EN_SINGLE:
		return ptp_mask_en_single(pccontext->private_clkdata, argptr);

	case PTP_SYS_OFFSET_PRECISE_CYCLES:
		if (!ptp->has_cycles)
			return -EOPNOTSUPP;
		return ptp_sys_offset_precise(ptp, argptr,
					      ptp->info->getcrosscycles);

	case PTP_SYS_OFFSET_EXTENDED_CYCLES:
		if (!ptp->has_cycles)
			return -EOPNOTSUPP;
		return ptp_sys_offset_extended(ptp, argptr,
					       ptp->info->getcyclesx64);
	default:
		return -ENOTTY;
	}
}

__poll_t ptp_poll(struct posix_clock_context *pccontext, struct file *fp,
		  poll_table *wait)
{
	struct ptp_clock *ptp =
		container_of(pccontext->clk, struct ptp_clock, clock);
	struct timestamp_event_queue *queue;

	queue = pccontext->private_clkdata;
	if (!queue)
		return EPOLLERR;

	poll_wait(fp, &ptp->tsev_wq, wait);

	return queue_cnt(queue) ? EPOLLIN : 0;
}

#define EXTTS_BUFSIZE (PTP_BUF_TIMESTAMPS * sizeof(struct ptp_extts_event))

ssize_t ptp_read(struct posix_clock_context *pccontext, uint rdflags,
		 char __user *buf, size_t cnt)
{
	struct ptp_clock *ptp =	container_of(pccontext->clk, struct ptp_clock, clock);
	struct timestamp_event_queue *queue;
	struct ptp_extts_event *event;
	ssize_t result;

	queue = pccontext->private_clkdata;
	if (!queue)
		return -EINVAL;

	if (cnt % sizeof(*event) != 0)
		return -EINVAL;

	if (cnt > EXTTS_BUFSIZE)
		cnt = EXTTS_BUFSIZE;

	if (wait_event_interruptible(ptp->tsev_wq, ptp->defunct || queue_cnt(queue)))
		return -ERESTARTSYS;

	if (ptp->defunct)
		return -ENODEV;

	event = kmalloc(EXTTS_BUFSIZE, GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	scoped_guard(spinlock_irq, &queue->lock) {
		size_t qcnt = min((size_t)queue_cnt(queue), cnt / sizeof(*event));

		for (size_t i = 0; i < qcnt; i++) {
			event[i] = queue->buf[queue->head];
			/* Paired with READ_ONCE() in queue_cnt() */
			WRITE_ONCE(queue->head, (queue->head + 1) % PTP_MAX_TIMESTAMPS);
		}
		cnt = qcnt * sizeof(*event);
	}

	result = cnt;
	if (copy_to_user(buf, event, cnt))
		result = -EFAULT;

	kfree(event);
	return result;
}
