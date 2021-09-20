// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PTP 1588 clock support - sysfs interface.
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 * Copyright 2021 NXP
 */
#include <linux/capability.h>
#include <linux/slab.h>

#include "ptp_private.h"

static ssize_t clock_name_show(struct device *dev,
			       struct device_attribute *attr, char *page)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	return snprintf(page, PAGE_SIZE-1, "%s\n", ptp->info->name);
}
static DEVICE_ATTR_RO(clock_name);

#define PTP_SHOW_INT(name, var)						\
static ssize_t var##_show(struct device *dev,				\
			   struct device_attribute *attr, char *page)	\
{									\
	struct ptp_clock *ptp = dev_get_drvdata(dev);			\
	return snprintf(page, PAGE_SIZE-1, "%d\n", ptp->info->var);	\
}									\
static DEVICE_ATTR(name, 0444, var##_show, NULL);

PTP_SHOW_INT(max_adjustment, max_adj);
PTP_SHOW_INT(n_alarms, n_alarm);
PTP_SHOW_INT(n_external_timestamps, n_ext_ts);
PTP_SHOW_INT(n_periodic_outputs, n_per_out);
PTP_SHOW_INT(n_programmable_pins, n_pins);
PTP_SHOW_INT(pps_available, pps);

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
static DEVICE_ATTR(extts_enable, 0220, NULL, extts_enable_store);

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
static DEVICE_ATTR(fifo, 0444, extts_fifo_show, NULL);

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
static DEVICE_ATTR(period, 0220, NULL, period_store);

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
static DEVICE_ATTR(pps_enable, 0220, NULL, pps_enable_store);

static int unregister_vclock(struct device *dev, void *data)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	struct ptp_clock_info *info = ptp->info;
	struct ptp_vclock *vclock;
	u32 *num = data;

	vclock = info_to_vclock(info);
	dev_info(dev->parent, "delete virtual clock ptp%d\n",
		 vclock->clock->index);

	ptp_vclock_unregister(vclock);
	(*num)--;

	/* For break. Not error. */
	if (*num == 0)
		return -EINVAL;

	return 0;
}

static ssize_t n_vclocks_show(struct device *dev,
			      struct device_attribute *attr, char *page)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	ssize_t size;

	if (mutex_lock_interruptible(&ptp->n_vclocks_mux))
		return -ERESTARTSYS;

	size = snprintf(page, PAGE_SIZE - 1, "%u\n", ptp->n_vclocks);

	mutex_unlock(&ptp->n_vclocks_mux);

	return size;
}

static ssize_t n_vclocks_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	struct ptp_vclock *vclock;
	int err = -EINVAL;
	u32 num, i;

	if (kstrtou32(buf, 0, &num))
		return err;

	if (mutex_lock_interruptible(&ptp->n_vclocks_mux))
		return -ERESTARTSYS;

	if (num > ptp->max_vclocks) {
		dev_err(dev, "max value is %d\n", ptp->max_vclocks);
		goto out;
	}

	/* Need to create more vclocks */
	if (num > ptp->n_vclocks) {
		for (i = 0; i < num - ptp->n_vclocks; i++) {
			vclock = ptp_vclock_register(ptp);
			if (!vclock)
				goto out;

			*(ptp->vclock_index + ptp->n_vclocks + i) =
				vclock->clock->index;

			dev_info(dev, "new virtual clock ptp%d\n",
				 vclock->clock->index);
		}
	}

	/* Need to delete vclocks */
	if (num < ptp->n_vclocks) {
		i = ptp->n_vclocks - num;
		device_for_each_child_reverse(dev, &i,
					      unregister_vclock);

		for (i = 1; i <= ptp->n_vclocks - num; i++)
			*(ptp->vclock_index + ptp->n_vclocks - i) = -1;
	}

	if (num == 0)
		dev_info(dev, "only physical clock in use now\n");
	else
		dev_info(dev, "guarantee physical clock free running\n");

	ptp->n_vclocks = num;
	mutex_unlock(&ptp->n_vclocks_mux);

	return count;
out:
	mutex_unlock(&ptp->n_vclocks_mux);
	return err;
}
static DEVICE_ATTR_RW(n_vclocks);

static ssize_t max_vclocks_show(struct device *dev,
				struct device_attribute *attr, char *page)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	ssize_t size;

	size = snprintf(page, PAGE_SIZE - 1, "%u\n", ptp->max_vclocks);

	return size;
}

static ssize_t max_vclocks_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	unsigned int *vclock_index;
	int err = -EINVAL;
	size_t size;
	u32 max;

	if (kstrtou32(buf, 0, &max) || max == 0)
		return -EINVAL;

	if (max == ptp->max_vclocks)
		return count;

	if (mutex_lock_interruptible(&ptp->n_vclocks_mux))
		return -ERESTARTSYS;

	if (max < ptp->n_vclocks)
		goto out;

	size = sizeof(int) * max;
	vclock_index = kzalloc(size, GFP_KERNEL);
	if (!vclock_index) {
		err = -ENOMEM;
		goto out;
	}

	size = sizeof(int) * ptp->n_vclocks;
	memcpy(vclock_index, ptp->vclock_index, size);

	kfree(ptp->vclock_index);
	ptp->vclock_index = vclock_index;
	ptp->max_vclocks = max;

	mutex_unlock(&ptp->n_vclocks_mux);

	return count;
out:
	mutex_unlock(&ptp->n_vclocks_mux);
	return err;
}
static DEVICE_ATTR_RW(max_vclocks);

static struct attribute *ptp_attrs[] = {
	&dev_attr_clock_name.attr,

	&dev_attr_max_adjustment.attr,
	&dev_attr_n_alarms.attr,
	&dev_attr_n_external_timestamps.attr,
	&dev_attr_n_periodic_outputs.attr,
	&dev_attr_n_programmable_pins.attr,
	&dev_attr_pps_available.attr,

	&dev_attr_extts_enable.attr,
	&dev_attr_fifo.attr,
	&dev_attr_period.attr,
	&dev_attr_pps_enable.attr,
	&dev_attr_n_vclocks.attr,
	&dev_attr_max_vclocks.attr,
	NULL
};

static umode_t ptp_is_attribute_visible(struct kobject *kobj,
					struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	struct ptp_clock_info *info = ptp->info;
	umode_t mode = attr->mode;

	if (attr == &dev_attr_extts_enable.attr ||
	    attr == &dev_attr_fifo.attr) {
		if (!info->n_ext_ts)
			mode = 0;
	} else if (attr == &dev_attr_period.attr) {
		if (!info->n_per_out)
			mode = 0;
	} else if (attr == &dev_attr_pps_enable.attr) {
		if (!info->pps)
			mode = 0;
	} else if (attr == &dev_attr_n_vclocks.attr ||
		   attr == &dev_attr_max_vclocks.attr) {
		if (ptp->is_virtual_clock)
			mode = 0;
	}

	return mode;
}

static const struct attribute_group ptp_group = {
	.is_visible	= ptp_is_attribute_visible,
	.attrs		= ptp_attrs,
};

const struct attribute_group *ptp_groups[] = {
	&ptp_group,
	NULL
};

static int ptp_pin_name2index(struct ptp_clock *ptp, const char *name)
{
	int i;
	for (i = 0; i < ptp->info->n_pins; i++) {
		if (!strcmp(ptp->info->pin_config[i].name, name))
			return i;
	}
	return -1;
}

static ssize_t ptp_pin_show(struct device *dev, struct device_attribute *attr,
			    char *page)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	unsigned int func, chan;
	int index;

	index = ptp_pin_name2index(ptp, attr->attr.name);
	if (index < 0)
		return -EINVAL;

	if (mutex_lock_interruptible(&ptp->pincfg_mux))
		return -ERESTARTSYS;

	func = ptp->info->pin_config[index].func;
	chan = ptp->info->pin_config[index].chan;

	mutex_unlock(&ptp->pincfg_mux);

	return snprintf(page, PAGE_SIZE, "%u %u\n", func, chan);
}

static ssize_t ptp_pin_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	unsigned int func, chan;
	int cnt, err, index;

	cnt = sscanf(buf, "%u %u", &func, &chan);
	if (cnt != 2)
		return -EINVAL;

	index = ptp_pin_name2index(ptp, attr->attr.name);
	if (index < 0)
		return -EINVAL;

	if (mutex_lock_interruptible(&ptp->pincfg_mux))
		return -ERESTARTSYS;
	err = ptp_set_pinfunc(ptp, index, func, chan);
	mutex_unlock(&ptp->pincfg_mux);
	if (err)
		return err;

	return count;
}

int ptp_populate_pin_groups(struct ptp_clock *ptp)
{
	struct ptp_clock_info *info = ptp->info;
	int err = -ENOMEM, i, n_pins = info->n_pins;

	if (!n_pins)
		return 0;

	ptp->pin_dev_attr = kcalloc(n_pins, sizeof(*ptp->pin_dev_attr),
				    GFP_KERNEL);
	if (!ptp->pin_dev_attr)
		goto no_dev_attr;

	ptp->pin_attr = kcalloc(1 + n_pins, sizeof(*ptp->pin_attr), GFP_KERNEL);
	if (!ptp->pin_attr)
		goto no_pin_attr;

	for (i = 0; i < n_pins; i++) {
		struct device_attribute *da = &ptp->pin_dev_attr[i];
		sysfs_attr_init(&da->attr);
		da->attr.name = info->pin_config[i].name;
		da->attr.mode = 0644;
		da->show = ptp_pin_show;
		da->store = ptp_pin_store;
		ptp->pin_attr[i] = &da->attr;
	}

	ptp->pin_attr_group.name = "pins";
	ptp->pin_attr_group.attrs = ptp->pin_attr;

	ptp->pin_attr_groups[0] = &ptp->pin_attr_group;

	return 0;

no_pin_attr:
	kfree(ptp->pin_dev_attr);
no_dev_attr:
	return err;
}

void ptp_cleanup_pin_groups(struct ptp_clock *ptp)
{
	kfree(ptp->pin_attr);
	kfree(ptp->pin_dev_attr);
}
