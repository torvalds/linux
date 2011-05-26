/*
 * PTP 1588 clock support - sysfs interface.
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
#include <linux/capability.h>

#include "ptp_private.h"

static ssize_t clock_name_show(struct device *dev,
			       struct device_attribute *attr, char *page)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	return snprintf(page, PAGE_SIZE-1, "%s\n", ptp->info->name);
}

#define PTP_SHOW_INT(name)						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *page)	\
{									\
	struct ptp_clock *ptp = dev_get_drvdata(dev);			\
	return snprintf(page, PAGE_SIZE-1, "%d\n", ptp->info->name);	\
}

PTP_SHOW_INT(max_adj);
PTP_SHOW_INT(n_alarm);
PTP_SHOW_INT(n_ext_ts);
PTP_SHOW_INT(n_per_out);
PTP_SHOW_INT(pps);

#define PTP_RO_ATTR(_var, _name) {				\
	.attr	= { .name = __stringify(_name), .mode = 0444 },	\
	.show	= _var##_show,					\
}

struct device_attribute ptp_dev_attrs[] = {
	PTP_RO_ATTR(clock_name,	clock_name),
	PTP_RO_ATTR(max_adj,	max_adjustment),
	PTP_RO_ATTR(n_alarm,	n_alarms),
	PTP_RO_ATTR(n_ext_ts,	n_external_timestamps),
	PTP_RO_ATTR(n_per_out,	n_periodic_outputs),
	PTP_RO_ATTR(pps,	pps_available),
	__ATTR_NULL,
};

static ssize_t extts_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	struct ptp_clock_info *ops = ptp->info;
	struct ptp_clock_request req = { .type = PTP_CLK_REQ_EXTTS };
	int cnt, enable;
	int err = -EINVAL;

	cnt = sscanf(buf, "%u %d", &req.extts.index, &enable);
	if (cnt != 2)
		goto out;
	if (req.extts.index >= ops->n_ext_ts)
		goto out;

	err = ops->enable(ops, &req, enable ? 1 : 0);
	if (err)
		goto out;

	return count;
out:
	return err;
}

static ssize_t extts_fifo_show(struct device *dev,
			       struct device_attribute *attr, char *page)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	struct timestamp_event_queue *queue = &ptp->tsevq;
	struct ptp_extts_event event;
	unsigned long flags;
	size_t qcnt;
	int cnt = 0;

	memset(&event, 0, sizeof(event));

	if (mutex_lock_interruptible(&ptp->tsevq_mux))
		return -ERESTARTSYS;

	spin_lock_irqsave(&queue->lock, flags);
	qcnt = queue_cnt(queue);
	if (qcnt) {
		event = queue->buf[queue->head];
		queue->head = (queue->head + 1) % PTP_MAX_TIMESTAMPS;
	}
	spin_unlock_irqrestore(&queue->lock, flags);

	if (!qcnt)
		goto out;

	cnt = snprintf(page, PAGE_SIZE, "%u %lld %u\n",
		       event.index, event.t.sec, event.t.nsec);
out:
	mutex_unlock(&ptp->tsevq_mux);
	return cnt;
}

static ssize_t period_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	struct ptp_clock_info *ops = ptp->info;
	struct ptp_clock_request req = { .type = PTP_CLK_REQ_PEROUT };
	int cnt, enable, err = -EINVAL;

	cnt = sscanf(buf, "%u %lld %u %lld %u", &req.perout.index,
		     &req.perout.start.sec, &req.perout.start.nsec,
		     &req.perout.period.sec, &req.perout.period.nsec);
	if (cnt != 5)
		goto out;
	if (req.perout.index >= ops->n_per_out)
		goto out;

	enable = req.perout.period.sec || req.perout.period.nsec;
	err = ops->enable(ops, &req, enable);
	if (err)
		goto out;

	return count;
out:
	return err;
}

static ssize_t pps_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	struct ptp_clock_info *ops = ptp->info;
	struct ptp_clock_request req = { .type = PTP_CLK_REQ_PPS };
	int cnt, enable;
	int err = -EINVAL;

	if (!capable(CAP_SYS_TIME))
		return -EPERM;

	cnt = sscanf(buf, "%d", &enable);
	if (cnt != 1)
		goto out;

	err = ops->enable(ops, &req, enable ? 1 : 0);
	if (err)
		goto out;

	return count;
out:
	return err;
}

static DEVICE_ATTR(extts_enable, 0220, NULL, extts_enable_store);
static DEVICE_ATTR(fifo,         0444, extts_fifo_show, NULL);
static DEVICE_ATTR(period,       0220, NULL, period_store);
static DEVICE_ATTR(pps_enable,   0220, NULL, pps_enable_store);

int ptp_cleanup_sysfs(struct ptp_clock *ptp)
{
	struct device *dev = ptp->dev;
	struct ptp_clock_info *info = ptp->info;

	if (info->n_ext_ts) {
		device_remove_file(dev, &dev_attr_extts_enable);
		device_remove_file(dev, &dev_attr_fifo);
	}
	if (info->n_per_out)
		device_remove_file(dev, &dev_attr_period);

	if (info->pps)
		device_remove_file(dev, &dev_attr_pps_enable);

	return 0;
}

int ptp_populate_sysfs(struct ptp_clock *ptp)
{
	struct device *dev = ptp->dev;
	struct ptp_clock_info *info = ptp->info;
	int err;

	if (info->n_ext_ts) {
		err = device_create_file(dev, &dev_attr_extts_enable);
		if (err)
			goto out1;
		err = device_create_file(dev, &dev_attr_fifo);
		if (err)
			goto out2;
	}
	if (info->n_per_out) {
		err = device_create_file(dev, &dev_attr_period);
		if (err)
			goto out3;
	}
	if (info->pps) {
		err = device_create_file(dev, &dev_attr_pps_enable);
		if (err)
			goto out4;
	}
	return 0;
out4:
	if (info->n_per_out)
		device_remove_file(dev, &dev_attr_period);
out3:
	if (info->n_ext_ts)
		device_remove_file(dev, &dev_attr_fifo);
out2:
	if (info->n_ext_ts)
		device_remove_file(dev, &dev_attr_extts_enable);
out1:
	return err;
}
