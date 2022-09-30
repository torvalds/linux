// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <uapi/linux/idxd.h>
#include "registers.h"
#include "idxd.h"

static char *idxd_wq_type_names[] = {
	[IDXD_WQT_NONE]		= "none",
	[IDXD_WQT_KERNEL]	= "kernel",
	[IDXD_WQT_USER]		= "user",
};

/* IDXD engine attributes */
static ssize_t engine_group_id_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct idxd_engine *engine = confdev_to_engine(dev);

	if (engine->group)
		return sysfs_emit(buf, "%d\n", engine->group->id);
	else
		return sysfs_emit(buf, "%d\n", -1);
}

static ssize_t engine_group_id_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct idxd_engine *engine = confdev_to_engine(dev);
	struct idxd_device *idxd = engine->idxd;
	long id;
	int rc;
	struct idxd_group *prevg;

	rc = kstrtol(buf, 10, &id);
	if (rc < 0)
		return -EINVAL;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (id > idxd->max_groups - 1 || id < -1)
		return -EINVAL;

	if (id == -1) {
		if (engine->group) {
			engine->group->num_engines--;
			engine->group = NULL;
		}
		return count;
	}

	prevg = engine->group;

	if (prevg)
		prevg->num_engines--;
	engine->group = idxd->groups[id];
	engine->group->num_engines++;

	return count;
}

static struct device_attribute dev_attr_engine_group =
		__ATTR(group_id, 0644, engine_group_id_show,
		       engine_group_id_store);

static struct attribute *idxd_engine_attributes[] = {
	&dev_attr_engine_group.attr,
	NULL,
};

static const struct attribute_group idxd_engine_attribute_group = {
	.attrs = idxd_engine_attributes,
};

static const struct attribute_group *idxd_engine_attribute_groups[] = {
	&idxd_engine_attribute_group,
	NULL,
};

static void idxd_conf_engine_release(struct device *dev)
{
	struct idxd_engine *engine = confdev_to_engine(dev);

	kfree(engine);
}

struct device_type idxd_engine_device_type = {
	.name = "engine",
	.release = idxd_conf_engine_release,
	.groups = idxd_engine_attribute_groups,
};

/* Group attributes */

static void idxd_set_free_rdbufs(struct idxd_device *idxd)
{
	int i, rdbufs;

	for (i = 0, rdbufs = 0; i < idxd->max_groups; i++) {
		struct idxd_group *g = idxd->groups[i];

		rdbufs += g->rdbufs_reserved;
	}

	idxd->nr_rdbufs = idxd->max_rdbufs - rdbufs;
}

static ssize_t group_read_buffers_reserved_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);

	return sysfs_emit(buf, "%u\n", group->rdbufs_reserved);
}

static ssize_t group_tokens_reserved_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	dev_warn_once(dev, "attribute deprecated, see read_buffers_reserved.\n");
	return group_read_buffers_reserved_show(dev, attr, buf);
}

static ssize_t group_read_buffers_reserved_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct idxd_group *group = confdev_to_group(dev);
	struct idxd_device *idxd = group->idxd;
	unsigned long val;
	int rc;

	rc = kstrtoul(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (idxd->data->type == IDXD_TYPE_IAX)
		return -EOPNOTSUPP;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (idxd->state == IDXD_DEV_ENABLED)
		return -EPERM;

	if (val > idxd->max_rdbufs)
		return -EINVAL;

	if (val > idxd->nr_rdbufs + group->rdbufs_reserved)
		return -EINVAL;

	group->rdbufs_reserved = val;
	idxd_set_free_rdbufs(idxd);
	return count;
}

static ssize_t group_tokens_reserved_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	dev_warn_once(dev, "attribute deprecated, see read_buffers_reserved.\n");
	return group_read_buffers_reserved_store(dev, attr, buf, count);
}

static struct device_attribute dev_attr_group_tokens_reserved =
		__ATTR(tokens_reserved, 0644, group_tokens_reserved_show,
		       group_tokens_reserved_store);

static struct device_attribute dev_attr_group_read_buffers_reserved =
		__ATTR(read_buffers_reserved, 0644, group_read_buffers_reserved_show,
		       group_read_buffers_reserved_store);

static ssize_t group_read_buffers_allowed_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);

	return sysfs_emit(buf, "%u\n", group->rdbufs_allowed);
}

static ssize_t group_tokens_allowed_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	dev_warn_once(dev, "attribute deprecated, see read_buffers_allowed.\n");
	return group_read_buffers_allowed_show(dev, attr, buf);
}

static ssize_t group_read_buffers_allowed_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct idxd_group *group = confdev_to_group(dev);
	struct idxd_device *idxd = group->idxd;
	unsigned long val;
	int rc;

	rc = kstrtoul(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (idxd->data->type == IDXD_TYPE_IAX)
		return -EOPNOTSUPP;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (idxd->state == IDXD_DEV_ENABLED)
		return -EPERM;

	if (val < 4 * group->num_engines ||
	    val > group->rdbufs_reserved + idxd->nr_rdbufs)
		return -EINVAL;

	group->rdbufs_allowed = val;
	return count;
}

static ssize_t group_tokens_allowed_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	dev_warn_once(dev, "attribute deprecated, see read_buffers_allowed.\n");
	return group_read_buffers_allowed_store(dev, attr, buf, count);
}

static struct device_attribute dev_attr_group_tokens_allowed =
		__ATTR(tokens_allowed, 0644, group_tokens_allowed_show,
		       group_tokens_allowed_store);

static struct device_attribute dev_attr_group_read_buffers_allowed =
		__ATTR(read_buffers_allowed, 0644, group_read_buffers_allowed_show,
		       group_read_buffers_allowed_store);

static ssize_t group_use_read_buffer_limit_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);

	return sysfs_emit(buf, "%u\n", group->use_rdbuf_limit);
}

static ssize_t group_use_token_limit_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	dev_warn_once(dev, "attribute deprecated, see use_read_buffer_limit.\n");
	return group_use_read_buffer_limit_show(dev, attr, buf);
}

static ssize_t group_use_read_buffer_limit_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct idxd_group *group = confdev_to_group(dev);
	struct idxd_device *idxd = group->idxd;
	unsigned long val;
	int rc;

	rc = kstrtoul(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (idxd->data->type == IDXD_TYPE_IAX)
		return -EOPNOTSUPP;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (idxd->state == IDXD_DEV_ENABLED)
		return -EPERM;

	if (idxd->rdbuf_limit == 0)
		return -EPERM;

	group->use_rdbuf_limit = !!val;
	return count;
}

static ssize_t group_use_token_limit_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	dev_warn_once(dev, "attribute deprecated, see use_read_buffer_limit.\n");
	return group_use_read_buffer_limit_store(dev, attr, buf, count);
}

static struct device_attribute dev_attr_group_use_token_limit =
		__ATTR(use_token_limit, 0644, group_use_token_limit_show,
		       group_use_token_limit_store);

static struct device_attribute dev_attr_group_use_read_buffer_limit =
		__ATTR(use_read_buffer_limit, 0644, group_use_read_buffer_limit_show,
		       group_use_read_buffer_limit_store);

static ssize_t group_engines_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);
	int i, rc = 0;
	struct idxd_device *idxd = group->idxd;

	for (i = 0; i < idxd->max_engines; i++) {
		struct idxd_engine *engine = idxd->engines[i];

		if (!engine->group)
			continue;

		if (engine->group->id == group->id)
			rc += sysfs_emit_at(buf, rc, "engine%d.%d ", idxd->id, engine->id);
	}

	if (!rc)
		return 0;
	rc--;
	rc += sysfs_emit_at(buf, rc, "\n");

	return rc;
}

static struct device_attribute dev_attr_group_engines =
		__ATTR(engines, 0444, group_engines_show, NULL);

static ssize_t group_work_queues_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);
	int i, rc = 0;
	struct idxd_device *idxd = group->idxd;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		if (!wq->group)
			continue;

		if (wq->group->id == group->id)
			rc += sysfs_emit_at(buf, rc, "wq%d.%d ", idxd->id, wq->id);
	}

	if (!rc)
		return 0;
	rc--;
	rc += sysfs_emit_at(buf, rc, "\n");

	return rc;
}

static struct device_attribute dev_attr_group_work_queues =
		__ATTR(work_queues, 0444, group_work_queues_show, NULL);

static ssize_t group_traffic_class_a_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);

	return sysfs_emit(buf, "%d\n", group->tc_a);
}

static ssize_t group_traffic_class_a_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct idxd_group *group = confdev_to_group(dev);
	struct idxd_device *idxd = group->idxd;
	long val;
	int rc;

	rc = kstrtol(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (idxd->state == IDXD_DEV_ENABLED)
		return -EPERM;

	if (idxd->hw.version < DEVICE_VERSION_2 && !tc_override)
		return -EPERM;

	if (val < 0 || val > 7)
		return -EINVAL;

	group->tc_a = val;
	return count;
}

static struct device_attribute dev_attr_group_traffic_class_a =
		__ATTR(traffic_class_a, 0644, group_traffic_class_a_show,
		       group_traffic_class_a_store);

static ssize_t group_traffic_class_b_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);

	return sysfs_emit(buf, "%d\n", group->tc_b);
}

static ssize_t group_traffic_class_b_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct idxd_group *group = confdev_to_group(dev);
	struct idxd_device *idxd = group->idxd;
	long val;
	int rc;

	rc = kstrtol(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (idxd->state == IDXD_DEV_ENABLED)
		return -EPERM;

	if (idxd->hw.version < DEVICE_VERSION_2 && !tc_override)
		return -EPERM;

	if (val < 0 || val > 7)
		return -EINVAL;

	group->tc_b = val;
	return count;
}

static struct device_attribute dev_attr_group_traffic_class_b =
		__ATTR(traffic_class_b, 0644, group_traffic_class_b_show,
		       group_traffic_class_b_store);

static ssize_t group_desc_progress_limit_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);

	return sysfs_emit(buf, "%d\n", group->desc_progress_limit);
}

static ssize_t group_desc_progress_limit_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct idxd_group *group = confdev_to_group(dev);
	int val, rc;

	rc = kstrtoint(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (val & ~GENMASK(1, 0))
		return -EINVAL;

	group->desc_progress_limit = val;
	return count;
}

static struct device_attribute dev_attr_group_desc_progress_limit =
		__ATTR(desc_progress_limit, 0644, group_desc_progress_limit_show,
		       group_desc_progress_limit_store);

static ssize_t group_batch_progress_limit_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct idxd_group *group = confdev_to_group(dev);

	return sysfs_emit(buf, "%d\n", group->batch_progress_limit);
}

static ssize_t group_batch_progress_limit_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct idxd_group *group = confdev_to_group(dev);
	int val, rc;

	rc = kstrtoint(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (val & ~GENMASK(1, 0))
		return -EINVAL;

	group->batch_progress_limit = val;
	return count;
}

static struct device_attribute dev_attr_group_batch_progress_limit =
		__ATTR(batch_progress_limit, 0644, group_batch_progress_limit_show,
		       group_batch_progress_limit_store);
static struct attribute *idxd_group_attributes[] = {
	&dev_attr_group_work_queues.attr,
	&dev_attr_group_engines.attr,
	&dev_attr_group_use_token_limit.attr,
	&dev_attr_group_use_read_buffer_limit.attr,
	&dev_attr_group_tokens_allowed.attr,
	&dev_attr_group_read_buffers_allowed.attr,
	&dev_attr_group_tokens_reserved.attr,
	&dev_attr_group_read_buffers_reserved.attr,
	&dev_attr_group_traffic_class_a.attr,
	&dev_attr_group_traffic_class_b.attr,
	&dev_attr_group_desc_progress_limit.attr,
	&dev_attr_group_batch_progress_limit.attr,
	NULL,
};

static bool idxd_group_attr_progress_limit_invisible(struct attribute *attr,
						     struct idxd_device *idxd)
{
	return (attr == &dev_attr_group_desc_progress_limit.attr ||
		attr == &dev_attr_group_batch_progress_limit.attr) &&
		!idxd->hw.group_cap.progress_limit;
}

static umode_t idxd_group_attr_visible(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct idxd_group *group = confdev_to_group(dev);
	struct idxd_device *idxd = group->idxd;

	if (idxd_group_attr_progress_limit_invisible(attr, idxd))
		return 0;

	return attr->mode;
}

static const struct attribute_group idxd_group_attribute_group = {
	.attrs = idxd_group_attributes,
	.is_visible = idxd_group_attr_visible,
};

static const struct attribute_group *idxd_group_attribute_groups[] = {
	&idxd_group_attribute_group,
	NULL,
};

static void idxd_conf_group_release(struct device *dev)
{
	struct idxd_group *group = confdev_to_group(dev);

	kfree(group);
}

struct device_type idxd_group_device_type = {
	.name = "group",
	.release = idxd_conf_group_release,
	.groups = idxd_group_attribute_groups,
};

/* IDXD work queue attribs */
static ssize_t wq_clients_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%d\n", wq->client_count);
}

static struct device_attribute dev_attr_wq_clients =
		__ATTR(clients, 0444, wq_clients_show, NULL);

static ssize_t wq_state_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	switch (wq->state) {
	case IDXD_WQ_DISABLED:
		return sysfs_emit(buf, "disabled\n");
	case IDXD_WQ_ENABLED:
		return sysfs_emit(buf, "enabled\n");
	}

	return sysfs_emit(buf, "unknown\n");
}

static struct device_attribute dev_attr_wq_state =
		__ATTR(state, 0444, wq_state_show, NULL);

static ssize_t wq_group_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	if (wq->group)
		return sysfs_emit(buf, "%u\n", wq->group->id);
	else
		return sysfs_emit(buf, "-1\n");
}

static ssize_t wq_group_id_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;
	long id;
	int rc;
	struct idxd_group *prevg, *group;

	rc = kstrtol(buf, 10, &id);
	if (rc < 0)
		return -EINVAL;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	if (id > idxd->max_groups - 1 || id < -1)
		return -EINVAL;

	if (id == -1) {
		if (wq->group) {
			wq->group->num_wqs--;
			wq->group = NULL;
		}
		return count;
	}

	group = idxd->groups[id];
	prevg = wq->group;

	if (prevg)
		prevg->num_wqs--;
	wq->group = group;
	group->num_wqs++;
	return count;
}

static struct device_attribute dev_attr_wq_group_id =
		__ATTR(group_id, 0644, wq_group_id_show, wq_group_id_store);

static ssize_t wq_mode_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%s\n", wq_dedicated(wq) ? "dedicated" : "shared");
}

static ssize_t wq_mode_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	if (sysfs_streq(buf, "dedicated")) {
		set_bit(WQ_FLAG_DEDICATED, &wq->flags);
		wq->threshold = 0;
	} else if (sysfs_streq(buf, "shared")) {
		clear_bit(WQ_FLAG_DEDICATED, &wq->flags);
	} else {
		return -EINVAL;
	}

	return count;
}

static struct device_attribute dev_attr_wq_mode =
		__ATTR(mode, 0644, wq_mode_show, wq_mode_store);

static ssize_t wq_size_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%u\n", wq->size);
}

static int total_claimed_wq_size(struct idxd_device *idxd)
{
	int i;
	int wq_size = 0;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		wq_size += wq->size;
	}

	return wq_size;
}

static ssize_t wq_size_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	unsigned long size;
	struct idxd_device *idxd = wq->idxd;
	int rc;

	rc = kstrtoul(buf, 10, &size);
	if (rc < 0)
		return -EINVAL;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (idxd->state == IDXD_DEV_ENABLED)
		return -EPERM;

	if (size + total_claimed_wq_size(idxd) - wq->size > idxd->max_wq_size)
		return -EINVAL;

	wq->size = size;
	return count;
}

static struct device_attribute dev_attr_wq_size =
		__ATTR(size, 0644, wq_size_show, wq_size_store);

static ssize_t wq_priority_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%u\n", wq->priority);
}

static ssize_t wq_priority_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	unsigned long prio;
	struct idxd_device *idxd = wq->idxd;
	int rc;

	rc = kstrtoul(buf, 10, &prio);
	if (rc < 0)
		return -EINVAL;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	if (prio > IDXD_MAX_PRIORITY)
		return -EINVAL;

	wq->priority = prio;
	return count;
}

static struct device_attribute dev_attr_wq_priority =
		__ATTR(priority, 0644, wq_priority_show, wq_priority_store);

static ssize_t wq_block_on_fault_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%u\n", test_bit(WQ_FLAG_BLOCK_ON_FAULT, &wq->flags));
}

static ssize_t wq_block_on_fault_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;
	bool bof;
	int rc;

	if (!idxd->hw.gen_cap.block_on_fault)
		return -EOPNOTSUPP;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (wq->state != IDXD_WQ_DISABLED)
		return -ENXIO;

	rc = kstrtobool(buf, &bof);
	if (rc < 0)
		return rc;

	if (bof)
		set_bit(WQ_FLAG_BLOCK_ON_FAULT, &wq->flags);
	else
		clear_bit(WQ_FLAG_BLOCK_ON_FAULT, &wq->flags);

	return count;
}

static struct device_attribute dev_attr_wq_block_on_fault =
		__ATTR(block_on_fault, 0644, wq_block_on_fault_show,
		       wq_block_on_fault_store);

static ssize_t wq_threshold_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%u\n", wq->threshold);
}

static ssize_t wq_threshold_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;
	unsigned int val;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return -EINVAL;

	if (val > wq->size || val <= 0)
		return -EINVAL;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (wq->state != IDXD_WQ_DISABLED)
		return -ENXIO;

	if (test_bit(WQ_FLAG_DEDICATED, &wq->flags))
		return -EINVAL;

	wq->threshold = val;

	return count;
}

static struct device_attribute dev_attr_wq_threshold =
		__ATTR(threshold, 0644, wq_threshold_show, wq_threshold_store);

static ssize_t wq_type_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	switch (wq->type) {
	case IDXD_WQT_KERNEL:
		return sysfs_emit(buf, "%s\n", idxd_wq_type_names[IDXD_WQT_KERNEL]);
	case IDXD_WQT_USER:
		return sysfs_emit(buf, "%s\n", idxd_wq_type_names[IDXD_WQT_USER]);
	case IDXD_WQT_NONE:
	default:
		return sysfs_emit(buf, "%s\n", idxd_wq_type_names[IDXD_WQT_NONE]);
	}

	return -EINVAL;
}

static ssize_t wq_type_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	enum idxd_wq_type old_type;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	old_type = wq->type;
	if (sysfs_streq(buf, idxd_wq_type_names[IDXD_WQT_NONE]))
		wq->type = IDXD_WQT_NONE;
	else if (sysfs_streq(buf, idxd_wq_type_names[IDXD_WQT_KERNEL]))
		wq->type = IDXD_WQT_KERNEL;
	else if (sysfs_streq(buf, idxd_wq_type_names[IDXD_WQT_USER]))
		wq->type = IDXD_WQT_USER;
	else
		return -EINVAL;

	/* If we are changing queue type, clear the name */
	if (wq->type != old_type)
		memset(wq->name, 0, WQ_NAME_SIZE + 1);

	return count;
}

static struct device_attribute dev_attr_wq_type =
		__ATTR(type, 0644, wq_type_show, wq_type_store);

static ssize_t wq_name_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%s\n", wq->name);
}

static ssize_t wq_name_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	char *input, *pos;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	if (strlen(buf) > WQ_NAME_SIZE || strlen(buf) == 0)
		return -EINVAL;

	/*
	 * This is temporarily placed here until we have SVM support for
	 * dmaengine.
	 */
	if (wq->type == IDXD_WQT_KERNEL && device_pasid_enabled(wq->idxd))
		return -EOPNOTSUPP;

	input = kstrndup(buf, count, GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	pos = strim(input);
	memset(wq->name, 0, WQ_NAME_SIZE + 1);
	sprintf(wq->name, "%s", pos);
	kfree(input);
	return count;
}

static struct device_attribute dev_attr_wq_name =
		__ATTR(name, 0644, wq_name_show, wq_name_store);

static ssize_t wq_cdev_minor_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	int minor = -1;

	mutex_lock(&wq->wq_lock);
	if (wq->idxd_cdev)
		minor = wq->idxd_cdev->minor;
	mutex_unlock(&wq->wq_lock);

	if (minor == -1)
		return -ENXIO;
	return sysfs_emit(buf, "%d\n", minor);
}

static struct device_attribute dev_attr_wq_cdev_minor =
		__ATTR(cdev_minor, 0444, wq_cdev_minor_show, NULL);

static int __get_sysfs_u64(const char *buf, u64 *val)
{
	int rc;

	rc = kstrtou64(buf, 0, val);
	if (rc < 0)
		return -EINVAL;

	if (*val == 0)
		return -EINVAL;

	*val = roundup_pow_of_two(*val);
	return 0;
}

static ssize_t wq_max_transfer_size_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%llu\n", wq->max_xfer_bytes);
}

static ssize_t wq_max_transfer_size_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;
	u64 xfer_size;
	int rc;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	rc = __get_sysfs_u64(buf, &xfer_size);
	if (rc < 0)
		return rc;

	if (xfer_size > idxd->max_xfer_bytes)
		return -EINVAL;

	wq->max_xfer_bytes = xfer_size;

	return count;
}

static struct device_attribute dev_attr_wq_max_transfer_size =
		__ATTR(max_transfer_size, 0644,
		       wq_max_transfer_size_show, wq_max_transfer_size_store);

static ssize_t wq_max_batch_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%u\n", wq->max_batch_size);
}

static ssize_t wq_max_batch_size_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;
	u64 batch_size;
	int rc;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	rc = __get_sysfs_u64(buf, &batch_size);
	if (rc < 0)
		return rc;

	if (batch_size > idxd->max_batch_size)
		return -EINVAL;

	idxd_wq_set_max_batch_size(idxd->data->type, wq, (u32)batch_size);

	return count;
}

static struct device_attribute dev_attr_wq_max_batch_size =
		__ATTR(max_batch_size, 0644, wq_max_batch_size_show, wq_max_batch_size_store);

static ssize_t wq_ats_disable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%u\n", test_bit(WQ_FLAG_ATS_DISABLE, &wq->flags));
}

static ssize_t wq_ats_disable_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;
	bool ats_dis;
	int rc;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	if (!idxd->hw.wq_cap.wq_ats_support)
		return -EOPNOTSUPP;

	rc = kstrtobool(buf, &ats_dis);
	if (rc < 0)
		return rc;

	if (ats_dis)
		set_bit(WQ_FLAG_ATS_DISABLE, &wq->flags);
	else
		clear_bit(WQ_FLAG_ATS_DISABLE, &wq->flags);

	return count;
}

static struct device_attribute dev_attr_wq_ats_disable =
		__ATTR(ats_disable, 0644, wq_ats_disable_show, wq_ats_disable_store);

static ssize_t wq_occupancy_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;
	u32 occup, offset;

	if (!idxd->hw.wq_cap.occupancy)
		return -EOPNOTSUPP;

	offset = WQCFG_OFFSET(idxd, wq->id, WQCFG_OCCUP_IDX);
	occup = ioread32(idxd->reg_base + offset) & WQCFG_OCCUP_MASK;

	return sysfs_emit(buf, "%u\n", occup);
}

static struct device_attribute dev_attr_wq_occupancy =
		__ATTR(occupancy, 0444, wq_occupancy_show, NULL);

static ssize_t wq_enqcmds_retries_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	if (wq_dedicated(wq))
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n", wq->enqcmds_retries);
}

static ssize_t wq_enqcmds_retries_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	int rc;
	unsigned int retries;

	if (wq_dedicated(wq))
		return -EOPNOTSUPP;

	rc = kstrtouint(buf, 10, &retries);
	if (rc < 0)
		return rc;

	if (retries > IDXD_ENQCMDS_MAX_RETRIES)
		retries = IDXD_ENQCMDS_MAX_RETRIES;

	wq->enqcmds_retries = retries;
	return count;
}

static struct device_attribute dev_attr_wq_enqcmds_retries =
		__ATTR(enqcmds_retries, 0644, wq_enqcmds_retries_show, wq_enqcmds_retries_store);

static ssize_t wq_op_config_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	return sysfs_emit(buf, "%*pb\n", IDXD_MAX_OPCAP_BITS, wq->opcap_bmap);
}

static int idxd_verify_supported_opcap(struct idxd_device *idxd, unsigned long *opmask)
{
	int bit;

	/*
	 * The OPCAP is defined as 256 bits that represents each operation the device
	 * supports per bit. Iterate through all the bits and check if the input mask
	 * is set for bits that are not set in the OPCAP for the device. If no OPCAP
	 * bit is set and input mask has the bit set, then return error.
	 */
	for_each_set_bit(bit, opmask, IDXD_MAX_OPCAP_BITS) {
		if (!test_bit(bit, idxd->opcap_bmap))
			return -EINVAL;
	}

	return 0;
}

static ssize_t wq_op_config_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;
	unsigned long *opmask;
	int rc;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	opmask = bitmap_zalloc(IDXD_MAX_OPCAP_BITS, GFP_KERNEL);
	if (!opmask)
		return -ENOMEM;

	rc = bitmap_parse(buf, count, opmask, IDXD_MAX_OPCAP_BITS);
	if (rc < 0)
		goto err;

	rc = idxd_verify_supported_opcap(idxd, opmask);
	if (rc < 0)
		goto err;

	bitmap_copy(wq->opcap_bmap, opmask, IDXD_MAX_OPCAP_BITS);

	bitmap_free(opmask);
	return count;

err:
	bitmap_free(opmask);
	return rc;
}

static struct device_attribute dev_attr_wq_op_config =
		__ATTR(op_config, 0644, wq_op_config_show, wq_op_config_store);

static struct attribute *idxd_wq_attributes[] = {
	&dev_attr_wq_clients.attr,
	&dev_attr_wq_state.attr,
	&dev_attr_wq_group_id.attr,
	&dev_attr_wq_mode.attr,
	&dev_attr_wq_size.attr,
	&dev_attr_wq_priority.attr,
	&dev_attr_wq_block_on_fault.attr,
	&dev_attr_wq_threshold.attr,
	&dev_attr_wq_type.attr,
	&dev_attr_wq_name.attr,
	&dev_attr_wq_cdev_minor.attr,
	&dev_attr_wq_max_transfer_size.attr,
	&dev_attr_wq_max_batch_size.attr,
	&dev_attr_wq_ats_disable.attr,
	&dev_attr_wq_occupancy.attr,
	&dev_attr_wq_enqcmds_retries.attr,
	&dev_attr_wq_op_config.attr,
	NULL,
};

static bool idxd_wq_attr_op_config_invisible(struct attribute *attr,
					     struct idxd_device *idxd)
{
	return attr == &dev_attr_wq_op_config.attr &&
	       !idxd->hw.wq_cap.op_config;
}

static bool idxd_wq_attr_max_batch_size_invisible(struct attribute *attr,
						  struct idxd_device *idxd)
{
	/* Intel IAA does not support batch processing, make it invisible */
	return attr == &dev_attr_wq_max_batch_size.attr &&
	       idxd->data->type == IDXD_TYPE_IAX;
}

static umode_t idxd_wq_attr_visible(struct kobject *kobj,
				    struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct idxd_wq *wq = confdev_to_wq(dev);
	struct idxd_device *idxd = wq->idxd;

	if (idxd_wq_attr_op_config_invisible(attr, idxd))
		return 0;

	if (idxd_wq_attr_max_batch_size_invisible(attr, idxd))
		return 0;

	return attr->mode;
}

static const struct attribute_group idxd_wq_attribute_group = {
	.attrs = idxd_wq_attributes,
	.is_visible = idxd_wq_attr_visible,
};

static const struct attribute_group *idxd_wq_attribute_groups[] = {
	&idxd_wq_attribute_group,
	NULL,
};

static void idxd_conf_wq_release(struct device *dev)
{
	struct idxd_wq *wq = confdev_to_wq(dev);

	bitmap_free(wq->opcap_bmap);
	kfree(wq->wqcfg);
	kfree(wq);
}

struct device_type idxd_wq_device_type = {
	.name = "wq",
	.release = idxd_conf_wq_release,
	.groups = idxd_wq_attribute_groups,
};

/* IDXD device attribs */
static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%#x\n", idxd->hw.version);
}
static DEVICE_ATTR_RO(version);

static ssize_t max_work_queues_size_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", idxd->max_wq_size);
}
static DEVICE_ATTR_RO(max_work_queues_size);

static ssize_t max_groups_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", idxd->max_groups);
}
static DEVICE_ATTR_RO(max_groups);

static ssize_t max_work_queues_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", idxd->max_wqs);
}
static DEVICE_ATTR_RO(max_work_queues);

static ssize_t max_engines_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", idxd->max_engines);
}
static DEVICE_ATTR_RO(max_engines);

static ssize_t numa_node_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%d\n", dev_to_node(&idxd->pdev->dev));
}
static DEVICE_ATTR_RO(numa_node);

static ssize_t max_batch_size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", idxd->max_batch_size);
}
static DEVICE_ATTR_RO(max_batch_size);

static ssize_t max_transfer_size_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%llu\n", idxd->max_xfer_bytes);
}
static DEVICE_ATTR_RO(max_transfer_size);

static ssize_t op_cap_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%*pb\n", IDXD_MAX_OPCAP_BITS, idxd->opcap_bmap);
}
static DEVICE_ATTR_RO(op_cap);

static ssize_t gen_cap_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%#llx\n", idxd->hw.gen_cap.bits);
}
static DEVICE_ATTR_RO(gen_cap);

static ssize_t configurable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags));
}
static DEVICE_ATTR_RO(configurable);

static ssize_t clients_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);
	int count = 0, i;

	spin_lock(&idxd->dev_lock);
	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		count += wq->client_count;
	}
	spin_unlock(&idxd->dev_lock);

	return sysfs_emit(buf, "%d\n", count);
}
static DEVICE_ATTR_RO(clients);

static ssize_t pasid_enabled_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", device_pasid_enabled(idxd));
}
static DEVICE_ATTR_RO(pasid_enabled);

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	switch (idxd->state) {
	case IDXD_DEV_DISABLED:
		return sysfs_emit(buf, "disabled\n");
	case IDXD_DEV_ENABLED:
		return sysfs_emit(buf, "enabled\n");
	case IDXD_DEV_HALTED:
		return sysfs_emit(buf, "halted\n");
	}

	return sysfs_emit(buf, "unknown\n");
}
static DEVICE_ATTR_RO(state);

static ssize_t errors_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);
	int i, out = 0;

	spin_lock(&idxd->dev_lock);
	for (i = 0; i < 4; i++)
		out += sysfs_emit_at(buf, out, "%#018llx ", idxd->sw_err.bits[i]);
	spin_unlock(&idxd->dev_lock);
	out--;
	out += sysfs_emit_at(buf, out, "\n");
	return out;
}
static DEVICE_ATTR_RO(errors);

static ssize_t max_read_buffers_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", idxd->max_rdbufs);
}

static ssize_t max_tokens_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	dev_warn_once(dev, "attribute deprecated, see max_read_buffers.\n");
	return max_read_buffers_show(dev, attr, buf);
}

static DEVICE_ATTR_RO(max_tokens);	/* deprecated */
static DEVICE_ATTR_RO(max_read_buffers);

static ssize_t read_buffer_limit_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", idxd->rdbuf_limit);
}

static ssize_t token_limit_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	dev_warn_once(dev, "attribute deprecated, see read_buffer_limit.\n");
	return read_buffer_limit_show(dev, attr, buf);
}

static ssize_t read_buffer_limit_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);
	unsigned long val;
	int rc;

	rc = kstrtoul(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (idxd->state == IDXD_DEV_ENABLED)
		return -EPERM;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (!idxd->hw.group_cap.rdbuf_limit)
		return -EPERM;

	if (val > idxd->hw.group_cap.total_rdbufs)
		return -EINVAL;

	idxd->rdbuf_limit = val;
	return count;
}

static ssize_t token_limit_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	dev_warn_once(dev, "attribute deprecated, see read_buffer_limit\n");
	return read_buffer_limit_store(dev, attr, buf, count);
}

static DEVICE_ATTR_RW(token_limit);	/* deprecated */
static DEVICE_ATTR_RW(read_buffer_limit);

static ssize_t cdev_major_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%u\n", idxd->major);
}
static DEVICE_ATTR_RO(cdev_major);

static ssize_t cmd_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	return sysfs_emit(buf, "%#x\n", idxd->cmd_status);
}

static ssize_t cmd_status_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	idxd->cmd_status = 0;
	return count;
}
static DEVICE_ATTR_RW(cmd_status);

static bool idxd_device_attr_max_batch_size_invisible(struct attribute *attr,
						      struct idxd_device *idxd)
{
	/* Intel IAA does not support batch processing, make it invisible */
	return attr == &dev_attr_max_batch_size.attr &&
	       idxd->data->type == IDXD_TYPE_IAX;
}

static umode_t idxd_device_attr_visible(struct kobject *kobj,
					struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct idxd_device *idxd = confdev_to_idxd(dev);

	if (idxd_device_attr_max_batch_size_invisible(attr, idxd))
		return 0;

	return attr->mode;
}

static struct attribute *idxd_device_attributes[] = {
	&dev_attr_version.attr,
	&dev_attr_max_groups.attr,
	&dev_attr_max_work_queues.attr,
	&dev_attr_max_work_queues_size.attr,
	&dev_attr_max_engines.attr,
	&dev_attr_numa_node.attr,
	&dev_attr_max_batch_size.attr,
	&dev_attr_max_transfer_size.attr,
	&dev_attr_op_cap.attr,
	&dev_attr_gen_cap.attr,
	&dev_attr_configurable.attr,
	&dev_attr_clients.attr,
	&dev_attr_pasid_enabled.attr,
	&dev_attr_state.attr,
	&dev_attr_errors.attr,
	&dev_attr_max_tokens.attr,
	&dev_attr_max_read_buffers.attr,
	&dev_attr_token_limit.attr,
	&dev_attr_read_buffer_limit.attr,
	&dev_attr_cdev_major.attr,
	&dev_attr_cmd_status.attr,
	NULL,
};

static const struct attribute_group idxd_device_attribute_group = {
	.attrs = idxd_device_attributes,
	.is_visible = idxd_device_attr_visible,
};

static const struct attribute_group *idxd_attribute_groups[] = {
	&idxd_device_attribute_group,
	NULL,
};

static void idxd_conf_device_release(struct device *dev)
{
	struct idxd_device *idxd = confdev_to_idxd(dev);

	kfree(idxd->groups);
	bitmap_free(idxd->wq_enable_map);
	kfree(idxd->wqs);
	kfree(idxd->engines);
	ida_free(&idxd_ida, idxd->id);
	bitmap_free(idxd->opcap_bmap);
	kfree(idxd);
}

struct device_type dsa_device_type = {
	.name = "dsa",
	.release = idxd_conf_device_release,
	.groups = idxd_attribute_groups,
};

struct device_type iax_device_type = {
	.name = "iax",
	.release = idxd_conf_device_release,
	.groups = idxd_attribute_groups,
};

static int idxd_register_engine_devices(struct idxd_device *idxd)
{
	struct idxd_engine *engine;
	int i, j, rc;

	for (i = 0; i < idxd->max_engines; i++) {
		engine = idxd->engines[i];
		rc = device_add(engine_confdev(engine));
		if (rc < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	j = i - 1;
	for (; i < idxd->max_engines; i++) {
		engine = idxd->engines[i];
		put_device(engine_confdev(engine));
	}

	while (j--) {
		engine = idxd->engines[j];
		device_unregister(engine_confdev(engine));
	}
	return rc;
}

static int idxd_register_group_devices(struct idxd_device *idxd)
{
	struct idxd_group *group;
	int i, j, rc;

	for (i = 0; i < idxd->max_groups; i++) {
		group = idxd->groups[i];
		rc = device_add(group_confdev(group));
		if (rc < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	j = i - 1;
	for (; i < idxd->max_groups; i++) {
		group = idxd->groups[i];
		put_device(group_confdev(group));
	}

	while (j--) {
		group = idxd->groups[j];
		device_unregister(group_confdev(group));
	}
	return rc;
}

static int idxd_register_wq_devices(struct idxd_device *idxd)
{
	struct idxd_wq *wq;
	int i, rc, j;

	for (i = 0; i < idxd->max_wqs; i++) {
		wq = idxd->wqs[i];
		rc = device_add(wq_confdev(wq));
		if (rc < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	j = i - 1;
	for (; i < idxd->max_wqs; i++) {
		wq = idxd->wqs[i];
		put_device(wq_confdev(wq));
	}

	while (j--) {
		wq = idxd->wqs[j];
		device_unregister(wq_confdev(wq));
	}
	return rc;
}

int idxd_register_devices(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int rc, i;

	rc = device_add(idxd_confdev(idxd));
	if (rc < 0)
		return rc;

	rc = idxd_register_wq_devices(idxd);
	if (rc < 0) {
		dev_dbg(dev, "WQ devices registering failed: %d\n", rc);
		goto err_wq;
	}

	rc = idxd_register_engine_devices(idxd);
	if (rc < 0) {
		dev_dbg(dev, "Engine devices registering failed: %d\n", rc);
		goto err_engine;
	}

	rc = idxd_register_group_devices(idxd);
	if (rc < 0) {
		dev_dbg(dev, "Group device registering failed: %d\n", rc);
		goto err_group;
	}

	return 0;

 err_group:
	for (i = 0; i < idxd->max_engines; i++)
		device_unregister(engine_confdev(idxd->engines[i]));
 err_engine:
	for (i = 0; i < idxd->max_wqs; i++)
		device_unregister(wq_confdev(idxd->wqs[i]));
 err_wq:
	device_del(idxd_confdev(idxd));
	return rc;
}

void idxd_unregister_devices(struct idxd_device *idxd)
{
	int i;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		device_unregister(wq_confdev(wq));
	}

	for (i = 0; i < idxd->max_engines; i++) {
		struct idxd_engine *engine = idxd->engines[i];

		device_unregister(engine_confdev(engine));
	}

	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = idxd->groups[i];

		device_unregister(group_confdev(group));
	}
}

int idxd_register_bus_type(void)
{
	return bus_register(&dsa_bus_type);
}

void idxd_unregister_bus_type(void)
{
	bus_unregister(&dsa_bus_type);
}
