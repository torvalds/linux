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

#include "ap_bus.h"
#include "ap_asm.h"

/*
 * AP card related attributes.
 */
static ssize_t ap_hwtype_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ac->ap_dev.device_type);
}

static DEVICE_ATTR(hwtype, 0444, ap_hwtype_show, NULL);

static ssize_t ap_raw_hwtype_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ac->raw_hwtype);
}

static DEVICE_ATTR(raw_hwtype, 0444, ap_raw_hwtype_show, NULL);

static ssize_t ap_depth_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ac->queue_depth);
}

static DEVICE_ATTR(depth, 0444, ap_depth_show, NULL);

static ssize_t ap_functions_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);

	return snprintf(buf, PAGE_SIZE, "0x%08X\n", ac->functions);
}

static DEVICE_ATTR(ap_functions, 0444, ap_functions_show, NULL);

static ssize_t ap_req_count_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	unsigned int req_cnt;

	req_cnt = 0;
	spin_lock_bh(&ap_list_lock);
	req_cnt = atomic_read(&ac->total_request_count);
	spin_unlock_bh(&ap_list_lock);
	return snprintf(buf, PAGE_SIZE, "%d\n", req_cnt);
}

static ssize_t ap_req_count_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ap_card *ac = to_ap_card(dev);
	struct ap_queue *aq;

	spin_lock_bh(&ap_list_lock);
	for_each_ap_queue(aq, ac)
		aq->total_request_count = 0;
	spin_unlock_bh(&ap_list_lock);
	atomic_set(&ac->total_request_count, 0);

	return count;
}

static DEVICE_ATTR(request_count, 0644, ap_req_count_show, ap_req_count_store);

static ssize_t ap_requestq_count_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	struct ap_queue *aq;
	unsigned int reqq_cnt;

	reqq_cnt = 0;
	spin_lock_bh(&ap_list_lock);
	for_each_ap_queue(aq, ac)
		reqq_cnt += aq->requestq_count;
	spin_unlock_bh(&ap_list_lock);
	return snprintf(buf, PAGE_SIZE, "%d\n", reqq_cnt);
}

static DEVICE_ATTR(requestq_count, 0444, ap_requestq_count_show, NULL);

static ssize_t ap_pendingq_count_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct ap_card *ac = to_ap_card(dev);
	struct ap_queue *aq;
	unsigned int penq_cnt;

	penq_cnt = 0;
	spin_lock_bh(&ap_list_lock);
	for_each_ap_queue(aq, ac)
		penq_cnt += aq->pendingq_count;
	spin_unlock_bh(&ap_list_lock);
	return snprintf(buf, PAGE_SIZE, "%d\n", penq_cnt);
}

static DEVICE_ATTR(pendingq_count, 0444, ap_pendingq_count_show, NULL);

static ssize_t ap_modalias_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "ap:t%02X\n", to_ap_dev(dev)->device_type);
}

static DEVICE_ATTR(modalias, 0444, ap_modalias_show, NULL);

static struct attribute *ap_card_dev_attrs[] = {
	&dev_attr_hwtype.attr,
	&dev_attr_raw_hwtype.attr,
	&dev_attr_depth.attr,
	&dev_attr_ap_functions.attr,
	&dev_attr_request_count.attr,
	&dev_attr_requestq_count.attr,
	&dev_attr_pendingq_count.attr,
	&dev_attr_modalias.attr,
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
	kfree(to_ap_card(dev));
}

struct ap_card *ap_card_create(int id, int queue_depth, int device_type,
			       unsigned int functions)
{
	struct ap_card *ac;

	ac = kzalloc(sizeof(*ac), GFP_KERNEL);
	if (!ac)
		return NULL;
	INIT_LIST_HEAD(&ac->queues);
	ac->ap_dev.device.release = ap_card_device_release;
	ac->ap_dev.device.type = &ap_card_type;
	ac->ap_dev.device_type = device_type;
	/* CEX6 toleration: map to CEX5 */
	if (device_type == AP_DEVICE_TYPE_CEX6)
		ac->ap_dev.device_type = AP_DEVICE_TYPE_CEX5;
	ac->raw_hwtype = device_type;
	ac->queue_depth = queue_depth;
	ac->functions = functions;
	ac->id = id;
	return ac;
}
