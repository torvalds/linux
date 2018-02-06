/*
 * PTP 1588 clock support - character device implementation.
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/module.h>
#include <linux/posix-clock.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

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
	default:
		return -EINVAL;
	}

	if (info->verify(info, pin, func, chan)) {
		pr_err("driver cannot use function %u on pin %u\n", func, chan);
		return -EOPNOTSUPP;
	}

	/* Disable whatever function was previously assigned. */
	if (pin1) {
		ptp_disable_pinfunc(info, func, chan);
		pin1->func = PTP_PF_NONE;
		pin1->chan = 0;
	}
	ptp_disable_pinfunc(info, pin2->func, pin2->chan);
	pin2->func = func;
	pin2->chan = chan;

	return 0;
}

int ptp_open(struct posix_clock *pc, fmode_t fmode)
{
	return 0;
}

long ptp_ioctl(struct posix_clock *pc, unsigned int cmd, unsigned long arg)
{
	struct ptp_clock_caps caps;
	struct ptp_clock_request req;
	struct ptp_sys_offset *sysoff = NULL;
	struct ptp_sys_offset_precise precise_offset;
	struct ptp_pin_desc pd;
	struct ptp_clock *ptp = container_of(pc, struct ptp_clock, clock);
	struct ptp_clock_info *ops = ptp->info;
	struct ptp_clock_time *pct;
	struct timespec64 ts;
	struct system_device_crosststamp xtstamp;
	int enable, err = 0;
	unsigned int i, pin_index;

	switch (cmd) {

	case PTP_CLOCK_GETCAPS:
		memset(&caps, 0, sizeof(caps));
		caps.max_adj = ptp->info->max_adj;
		caps.n_alarm = ptp->info->n_alarm;
		caps.n_ext_ts = ptp->info->n_ext_ts;
		caps.n_per_out = ptp->info->n_per_out;
		caps.pps = ptp->info->pps;
		caps.n_pins = ptp->info->n_pins;
		caps.cross_timestamping = ptp->info->getcrosststamp != NULL;
		if (copy_to_user((void __user *)arg, &caps, sizeof(caps)))
			err = -EFAULT;
		break;

	case PTP_EXTTS_REQUEST:
		if (copy_from_user(&req.extts, (void __user *)arg,
				   sizeof(req.extts))) {
			err = -EFAULT;
			break;
		}
		if (req.extts.index >= ops->n_ext_ts) {
			err = -EINVAL;
			break;
		}
		req.type = PTP_CLK_REQ_EXTTS;
		enable = req.extts.flags & PTP_ENABLE_FEATURE ? 1 : 0;
		err = ops->enable(ops, &req, enable);
		break;

	case PTP_PEROUT_REQUEST:
		if (copy_from_user(&req.perout, (void __user *)arg,
				   sizeof(req.perout))) {
			err = -EFAULT;
			break;
		}
		if (req.perout.index >= ops->n_per_out) {
			err = -EINVAL;
			break;
		}
		req.type = PTP_CLK_REQ_PEROUT;
		enable = req.perout.period.sec || req.perout.period.nsec;
		err = ops->enable(ops, &req, enable);
		break;

	case PTP_ENABLE_PPS:
		if (!capable(CAP_SYS_TIME))
			return -EPERM;
		req.type = PTP_CLK_REQ_PPS;
		enable = arg ? 1 : 0;
		err = ops->enable(ops, &req, enable);
		break;

	case PTP_SYS_OFFSET_PRECISE:
		if (!ptp->info->getcrosststamp) {
			err = -EOPNOTSUPP;
			break;
		}
		err = ptp->info->getcrosststamp(ptp->info, &xtstamp);
		if (err)
			break;

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
		if (copy_to_user((void __user *)arg, &precise_offset,
				 sizeof(precise_offset)))
			err = -EFAULT;
		break;

	case PTP_SYS_OFFSET:
		sysoff = memdup_user((void __user *)arg, sizeof(*sysoff));
		if (IS_ERR(sysoff)) {
			err = PTR_ERR(sysoff);
			sysoff = NULL;
			break;
		}
		if (sysoff->n_samples > PTP_MAX_SAMPLES) {
			err = -EINVAL;
			break;
		}
		pct = &sysoff->ts[0];
		for (i = 0; i < sysoff->n_samples; i++) {
			getnstimeofday64(&ts);
			pct->sec = ts.tv_sec;
			pct->nsec = ts.tv_nsec;
			pct++;
			ptp->info->gettime64(ptp->info, &ts);
			pct->sec = ts.tv_sec;
			pct->nsec = ts.tv_nsec;
			pct++;
		}
		getnstimeofday64(&ts);
		pct->sec = ts.tv_sec;
		pct->nsec = ts.tv_nsec;
		if (copy_to_user((void __user *)arg, sysoff, sizeof(*sysoff)))
			err = -EFAULT;
		break;

	case PTP_PIN_GETFUNC:
		if (copy_from_user(&pd, (void __user *)arg, sizeof(pd))) {
			err = -EFAULT;
			break;
		}
		pin_index = pd.index;
		if (pin_index >= ops->n_pins) {
			err = -EINVAL;
			break;
		}
		if (mutex_lock_interruptible(&ptp->pincfg_mux))
			return -ERESTARTSYS;
		pd = ops->pin_config[pin_index];
		mutex_unlock(&ptp->pincfg_mux);
		if (!err && copy_to_user((void __user *)arg, &pd, sizeof(pd)))
			err = -EFAULT;
		break;

	case PTP_PIN_SETFUNC:
		if (copy_from_user(&pd, (void __user *)arg, sizeof(pd))) {
			err = -EFAULT;
			break;
		}
		pin_index = pd.index;
		if (pin_index >= ops->n_pins) {
			err = -EINVAL;
			break;
		}
		if (mutex_lock_interruptible(&ptp->pincfg_mux))
			return -ERESTARTSYS;
		err = ptp_set_pinfunc(ptp, pin_index, pd.func, pd.chan);
		mutex_unlock(&ptp->pincfg_mux);
		break;

	default:
		err = -ENOTTY;
		break;
	}

	kfree(sysoff);
	return err;
}

__poll_t ptp_poll(struct posix_clock *pc, struct file *fp, poll_table *wait)
{
	struct ptp_clock *ptp = container_of(pc, struct ptp_clock, clock);

	poll_wait(fp, &ptp->tsev_wq, wait);

	return queue_cnt(&ptp->tsevq) ? POLLIN : 0;
}

#define EXTTS_BUFSIZE (PTP_BUF_TIMESTAMPS * sizeof(struct ptp_extts_event))

ssize_t ptp_read(struct posix_clock *pc,
		 uint rdflags, char __user *buf, size_t cnt)
{
	struct ptp_clock *ptp = container_of(pc, struct ptp_clock, clock);
	struct timestamp_event_queue *queue = &ptp->tsevq;
	struct ptp_extts_event *event;
	unsigned long flags;
	size_t qcnt, i;
	int result;

	if (cnt % sizeof(struct ptp_extts_event) != 0)
		return -EINVAL;

	if (cnt > EXTTS_BUFSIZE)
		cnt = EXTTS_BUFSIZE;

	cnt = cnt / sizeof(struct ptp_extts_event);

	if (mutex_lock_interruptible(&ptp->tsevq_mux))
		return -ERESTARTSYS;

	if (wait_event_interruptible(ptp->tsev_wq,
				     ptp->defunct || queue_cnt(queue))) {
		mutex_unlock(&ptp->tsevq_mux);
		return -ERESTARTSYS;
	}

	if (ptp->defunct) {
		mutex_unlock(&ptp->tsevq_mux);
		return -ENODEV;
	}

	event = kmalloc(EXTTS_BUFSIZE, GFP_KERNEL);
	if (!event) {
		mutex_unlock(&ptp->tsevq_mux);
		return -ENOMEM;
	}

	spin_lock_irqsave(&queue->lock, flags);

	qcnt = queue_cnt(queue);

	if (cnt > qcnt)
		cnt = qcnt;

	for (i = 0; i < cnt; i++) {
		event[i] = queue->buf[queue->head];
		queue->head = (queue->head + 1) % PTP_MAX_TIMESTAMPS;
	}

	spin_unlock_irqrestore(&queue->lock, flags);

	cnt = cnt * sizeof(struct ptp_extts_event);

	mutex_unlock(&ptp->tsevq_mux);

	result = cnt;
	if (copy_to_user(buf, event, cnt))
		result = -EFAULT;

	kfree(event);
	return result;
}
