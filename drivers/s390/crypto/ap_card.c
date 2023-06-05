// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *
 * Adjunct processor bus, card related code.
 */

#define KMSG_COMPONENT "ap"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/slab.h>
#include <asm/facility.h>
#include <asm/sclp.h>

#include "ap_bus.h"

/*
 * AP card related attributes.
 */
static ssize_t hwtype_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return sysfs_emit(buf, "%d\n", ac->ap_dev.device_type);
}

static DEVICE_ATTR_RO(hwtype);

static ssize_t raw_hwtype_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return sysfs_emit(buf, "%d\n", ac->raw_hwtype);
}

static DEVICE_ATTR_RO(raw_hwtype);

static ssize_t depth_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return sysfs_emit(buf, "%d\n", ac->queue_depth);
}

static DEVICE_ATTR_RO(depth);

static ssize_t ap_functions_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return sysfs_emit(buf, "0x%08X\n", ac->functions);
}

static DEVICE_ATTR_RO(ap_functions);

static ssize_t request_count_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	u64 req_cnt;

	req_cnt = 0;
	spin_lock_bh(&ap_queues_lock);
	req_cnt = atomic64_read(&ac->total_request_count);
	spin_unlock_bh(&ap_queues_lock);
	return sysfs_emit(buf, "%llu\n", req_cnt);
}

static ssize_t request_count_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int bkt;
	struct ap_queue *aq;
	struct ap_card *ac = to_ap_card(dev);

	spin_lock_bh(&ap_queues_lock);
	hash_for_each(ap_queues, bkt, aq, hnode)
		if (ac == aq->card)
			aq->total_request_count = 0;
	spin_unlock_bh(&ap_queues_lock);
	atomic64_set(&ac->total_request_count, 0);

	return count;
}

static DEVICE_ATTR_RW(request_count);

static ssize_t requestq_count_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int bkt;
	struct ap_queue *aq;
	unsigned int reqq_cnt;
	struct ap_card *ac = to_ap_card(dev);

	reqq_cnt = 0;
	spin_lock_bh(&ap_queues_lock);
	hash_for_each(ap_queues, bkt, aq, hnode)
		if (ac == aq->card)
			reqq_cnt += aq->requestq_count;
	spin_unlock_bh(&ap_queues_lock);
	return sysfs_emit(buf, "%d\n", reqq_cnt);
}

static DEVICE_ATTR_RO(requestq_count);

static ssize_t pendingq_count_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int bkt;
	struct ap_queue *aq;
	unsigned int penq_cnt;
	struct ap_card *ac = to_ap_card(dev);

	penq_cnt = 0;
	spin_lock_bh(&ap_queues_lock);
	hash_for_each(ap_queues, bkt, aq, hnode)
		if (ac == aq->card)
			penq_cnt += aq->pendingq_count;
	spin_unlock_bh(&ap_queues_lock);
	return sysfs_emit(buf, "%d\n", penq_cnt);
}

static DEVICE_ATTR_RO(pendingq_count);

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "ap:t%02X\n", to_ap_dev(dev)->device_type);
}

static DEVICE_ATTR_RO(modalias);

static ssize_t config_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return sysfs_emit(buf, "%d\n", ac->config ? 1 : 0);
}

static ssize_t config_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int rc = 0, cfg;
	struct ap_card *ac = to_ap_card(dev);

	if (sscanf(buf, "%d\n", &cfg) != 1 || cfg < 0 || cfg > 1)
		return -EINVAL;

	if (cfg && !ac->config)
		rc = sclp_ap_configure(ac->id);
	else if (!cfg && ac->config)
		rc = sclp_ap_deconfigure(ac->id);
	if (rc)
		return rc;

	ac->config = cfg ? true : false;

	ap_send_config_uevent(&ac->ap_dev, ac->config);

	return count;
}

static DEVICE_ATTR_RW(config);

static ssize_t chkstop_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return sysfs_emit(buf, "%d\n", ac->chkstop ? 1 : 0);
}

static DEVICE_ATTR_RO(chkstop);

static ssize_t max_msg_size_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return sysfs_emit(buf, "%u\n", ac->maxmsgsize);
}

static DEVICE_ATTR_RO(max_msg_size);

static struct attribute *ap_card_dev_attrs[] = {
	&dev_attr_hwtype.attr,
	&dev_attr_raw_hwtype.attr,
	&dev_attr_depth.attr,
	&dev_attr_ap_functions.attr,
	&dev_attr_request_count.attr,
	&dev_attr_requestq_count.attr,
	&dev_attr_pendingq_count.attr,
	&dev_attr_modalias.attr,
	&dev_attr_config.attr,
	&dev_attr_chkstop.attr,
	&dev_attr_max_msg_size.attr,
	NULL
};

static struct attribute_group ap_card_dev_attr_group = {
	.attrs = ap_card_dev_attrs
};

static const struct attribute_group *ap_card_dev_attr_groups[] = {
	&ap_card_dev_attr_group,
	NULL
};

static struct device_type ap_card_type = {
	.name = "ap_card",
	.groups = ap_card_dev_attr_groups,
};

static void ap_card_device_release(struct device *dev)
{
	struct ap_card *ac = to_ap_card(dev);

	kfree(ac);
}

struct ap_card *ap_card_create(int id, int queue_depth, int raw_type,
			       int comp_type, unsigned int functions, int ml)
{
	struct ap_card *ac;

	ac = kzalloc(sizeof(*ac), GFP_KERNEL);
	if (!ac)
		return NULL;
	ac->ap_dev.device.release = ap_card_device_release;
	ac->ap_dev.device.type = &ap_card_type;
	ac->ap_dev.device_type = comp_type;
	ac->raw_hwtype = raw_type;
	ac->queue_depth = queue_depth;
	ac->functions = functions;
	ac->id = id;
	ac->maxmsgsize = ml > 0 ?
		ml * AP_TAPQ_ML_FIELD_CHUNK_SIZE : AP_DEFAULT_MAX_MSG_SIZE;

	return ac;
}
