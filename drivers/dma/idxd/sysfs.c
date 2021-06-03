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

static int idxd_config_bus_match(struct device *dev,
				 struct device_driver *drv)
{
	int matched = 0;

	if (is_idxd_dev(dev)) {
		struct idxd_device *idxd = confdev_to_idxd(dev);

		if (idxd->state != IDXD_DEV_CONF_READY)
			return 0;
		matched = 1;
	} else if (is_idxd_wq_dev(dev)) {
		struct idxd_wq *wq = confdev_to_wq(dev);
		struct idxd_device *idxd = wq->idxd;

		if (idxd->state < IDXD_DEV_CONF_READY)
			return 0;

		if (wq->state != IDXD_WQ_DISABLED) {
			dev_dbg(dev, "%s not disabled\n", dev_name(dev));
			return 0;
		}
		matched = 1;
	}

	if (matched)
		dev_dbg(dev, "%s matched\n", dev_name(dev));

	return matched;
}

static int enable_wq(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	unsigned long flags;
	int rc;

	mutex_lock(&wq->wq_lock);

	if (idxd->state != IDXD_DEV_ENABLED) {
		mutex_unlock(&wq->wq_lock);
		dev_warn(dev, "Enabling while device not enabled.\n");
		return -EPERM;
	}

	if (wq->state != IDXD_WQ_DISABLED) {
		mutex_unlock(&wq->wq_lock);
		dev_warn(dev, "WQ %d already enabled.\n", wq->id);
		return -EBUSY;
	}

	if (!wq->group) {
		mutex_unlock(&wq->wq_lock);
		dev_warn(dev, "WQ not attached to group.\n");
		return -EINVAL;
	}

	if (strlen(wq->name) == 0) {
		mutex_unlock(&wq->wq_lock);
		dev_warn(dev, "WQ name not set.\n");
		return -EINVAL;
	}

	/* Shared WQ checks */
	if (wq_shared(wq)) {
		if (!device_swq_supported(idxd)) {
			dev_warn(dev, "PASID not enabled and shared WQ.\n");
			mutex_unlock(&wq->wq_lock);
			return -ENXIO;
		}
		/*
		 * Shared wq with the threshold set to 0 means the user
		 * did not set the threshold or transitioned from a
		 * dedicated wq but did not set threshold. A value
		 * of 0 would effectively disable the shared wq. The
		 * driver does not allow a value of 0 to be set for
		 * threshold via sysfs.
		 */
		if (wq->threshold == 0) {
			dev_warn(dev, "Shared WQ and threshold 0.\n");
			mutex_unlock(&wq->wq_lock);
			return -EINVAL;
		}
	}

	rc = idxd_wq_alloc_resources(wq);
	if (rc < 0) {
		mutex_unlock(&wq->wq_lock);
		dev_warn(dev, "WQ resource alloc failed\n");
		return rc;
	}

	spin_lock_irqsave(&idxd->dev_lock, flags);
	if (test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		rc = idxd_device_config(idxd);
	spin_unlock_irqrestore(&idxd->dev_lock, flags);
	if (rc < 0) {
		mutex_unlock(&wq->wq_lock);
		dev_warn(dev, "Writing WQ %d config failed: %d\n", wq->id, rc);
		return rc;
	}

	rc = idxd_wq_enable(wq);
	if (rc < 0) {
		mutex_unlock(&wq->wq_lock);
		dev_warn(dev, "WQ %d enabling failed: %d\n", wq->id, rc);
		return rc;
	}

	rc = idxd_wq_map_portal(wq);
	if (rc < 0) {
		dev_warn(dev, "wq portal mapping failed: %d\n", rc);
		rc = idxd_wq_disable(wq, false);
		if (rc < 0)
			dev_warn(dev, "IDXD wq disable failed\n");
		mutex_unlock(&wq->wq_lock);
		return rc;
	}

	wq->client_count = 0;

	if (wq->type == IDXD_WQT_KERNEL) {
		rc = idxd_wq_init_percpu_ref(wq);
		if (rc < 0) {
			dev_dbg(dev, "percpu_ref setup failed\n");
			mutex_unlock(&wq->wq_lock);
			return rc;
		}
	}

	if (is_idxd_wq_dmaengine(wq)) {
		rc = idxd_register_dma_channel(wq);
		if (rc < 0) {
			dev_dbg(dev, "DMA channel register failed\n");
			mutex_unlock(&wq->wq_lock);
			return rc;
		}
	} else if (is_idxd_wq_cdev(wq)) {
		rc = idxd_wq_add_cdev(wq);
		if (rc < 0) {
			dev_dbg(dev, "Cdev creation failed\n");
			mutex_unlock(&wq->wq_lock);
			return rc;
		}
	}

	mutex_unlock(&wq->wq_lock);
	dev_info(dev, "wq %s enabled\n", dev_name(&wq->conf_dev));

	return 0;
}

static int idxd_config_bus_probe(struct device *dev)
{
	int rc = 0;
	unsigned long flags;

	dev_dbg(dev, "%s called\n", __func__);

	if (is_idxd_dev(dev)) {
		struct idxd_device *idxd = confdev_to_idxd(dev);

		if (idxd->state != IDXD_DEV_CONF_READY) {
			dev_warn(dev, "Device not ready for config\n");
			return -EBUSY;
		}

		if (!try_module_get(THIS_MODULE))
			return -ENXIO;

		/* Perform IDXD configuration and enabling */
		spin_lock_irqsave(&idxd->dev_lock, flags);
		if (test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
			rc = idxd_device_config(idxd);
		spin_unlock_irqrestore(&idxd->dev_lock, flags);
		if (rc < 0) {
			module_put(THIS_MODULE);
			dev_warn(dev, "Device config failed: %d\n", rc);
			return rc;
		}

		/* start device */
		rc = idxd_device_enable(idxd);
		if (rc < 0) {
			module_put(THIS_MODULE);
			dev_warn(dev, "Device enable failed: %d\n", rc);
			return rc;
		}

		dev_info(dev, "Device %s enabled\n", dev_name(dev));

		rc = idxd_register_dma_device(idxd);
		if (rc < 0) {
			module_put(THIS_MODULE);
			dev_dbg(dev, "Failed to register dmaengine device\n");
			return rc;
		}
		return 0;
	} else if (is_idxd_wq_dev(dev)) {
		struct idxd_wq *wq = confdev_to_wq(dev);

		return enable_wq(wq);
	}

	return -ENODEV;
}

static void disable_wq(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;

	mutex_lock(&wq->wq_lock);
	dev_dbg(dev, "%s removing WQ %s\n", __func__, dev_name(&wq->conf_dev));
	if (wq->state == IDXD_WQ_DISABLED) {
		mutex_unlock(&wq->wq_lock);
		return;
	}

	if (wq->type == IDXD_WQT_KERNEL)
		idxd_wq_quiesce(wq);

	if (is_idxd_wq_dmaengine(wq))
		idxd_unregister_dma_channel(wq);
	else if (is_idxd_wq_cdev(wq))
		idxd_wq_del_cdev(wq);

	if (idxd_wq_refcount(wq))
		dev_warn(dev, "Clients has claim on wq %d: %d\n",
			 wq->id, idxd_wq_refcount(wq));

	idxd_wq_unmap_portal(wq);

	idxd_wq_drain(wq);
	idxd_wq_reset(wq);

	idxd_wq_free_resources(wq);
	wq->client_count = 0;
	mutex_unlock(&wq->wq_lock);

	dev_info(dev, "wq %s disabled\n", dev_name(&wq->conf_dev));
}

static int idxd_config_bus_remove(struct device *dev)
{
	dev_dbg(dev, "%s called for %s\n", __func__, dev_name(dev));

	/* disable workqueue here */
	if (is_idxd_wq_dev(dev)) {
		struct idxd_wq *wq = confdev_to_wq(dev);

		disable_wq(wq);
	} else if (is_idxd_dev(dev)) {
		struct idxd_device *idxd = confdev_to_idxd(dev);
		int i;

		dev_dbg(dev, "%s removing dev %s\n", __func__,
			dev_name(&idxd->conf_dev));
		for (i = 0; i < idxd->max_wqs; i++) {
			struct idxd_wq *wq = idxd->wqs[i];

			if (wq->state == IDXD_WQ_DISABLED)
				continue;
			dev_warn(dev, "Active wq %d on disable %s.\n", i,
				 dev_name(&idxd->conf_dev));
			device_release_driver(&wq->conf_dev);
		}

		idxd_unregister_dma_device(idxd);
		idxd_device_disable(idxd);
		if (test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
			idxd_device_reset(idxd);
		module_put(THIS_MODULE);

		dev_info(dev, "Device %s disabled\n", dev_name(dev));
	}

	return 0;
}

static void idxd_config_bus_shutdown(struct device *dev)
{
	dev_dbg(dev, "%s called\n", __func__);
}

struct bus_type dsa_bus_type = {
	.name = "dsa",
	.match = idxd_config_bus_match,
	.probe = idxd_config_bus_probe,
	.remove = idxd_config_bus_remove,
	.shutdown = idxd_config_bus_shutdown,
};

static struct idxd_device_driver dsa_drv = {
	.drv = {
		.name = "dsa",
		.bus = &dsa_bus_type,
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
	},
};

/* IDXD generic driver setup */
int idxd_register_driver(void)
{
	return driver_register(&dsa_drv.drv);
}

void idxd_unregister_driver(void)
{
	driver_unregister(&dsa_drv.drv);
}

/* IDXD engine attributes */
static ssize_t engine_group_id_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct idxd_engine *engine =
		container_of(dev, struct idxd_engine, conf_dev);

	if (engine->group)
		return sysfs_emit(buf, "%d\n", engine->group->id);
	else
		return sysfs_emit(buf, "%d\n", -1);
}

static ssize_t engine_group_id_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct idxd_engine *engine =
		container_of(dev, struct idxd_engine, conf_dev);
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
	struct idxd_engine *engine = container_of(dev, struct idxd_engine, conf_dev);

	kfree(engine);
}

struct device_type idxd_engine_device_type = {
	.name = "engine",
	.release = idxd_conf_engine_release,
	.groups = idxd_engine_attribute_groups,
};

/* Group attributes */

static void idxd_set_free_tokens(struct idxd_device *idxd)
{
	int i, tokens;

	for (i = 0, tokens = 0; i < idxd->max_groups; i++) {
		struct idxd_group *g = idxd->groups[i];

		tokens += g->tokens_reserved;
	}

	idxd->nr_tokens = idxd->max_tokens - tokens;
}

static ssize_t group_tokens_reserved_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);

	return sysfs_emit(buf, "%u\n", group->tokens_reserved);
}

static ssize_t group_tokens_reserved_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);
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

	if (val > idxd->max_tokens)
		return -EINVAL;

	if (val > idxd->nr_tokens + group->tokens_reserved)
		return -EINVAL;

	group->tokens_reserved = val;
	idxd_set_free_tokens(idxd);
	return count;
}

static struct device_attribute dev_attr_group_tokens_reserved =
		__ATTR(tokens_reserved, 0644, group_tokens_reserved_show,
		       group_tokens_reserved_store);

static ssize_t group_tokens_allowed_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);

	return sysfs_emit(buf, "%u\n", group->tokens_allowed);
}

static ssize_t group_tokens_allowed_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);
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
	    val > group->tokens_reserved + idxd->nr_tokens)
		return -EINVAL;

	group->tokens_allowed = val;
	return count;
}

static struct device_attribute dev_attr_group_tokens_allowed =
		__ATTR(tokens_allowed, 0644, group_tokens_allowed_show,
		       group_tokens_allowed_store);

static ssize_t group_use_token_limit_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);

	return sysfs_emit(buf, "%u\n", group->use_token_limit);
}

static ssize_t group_use_token_limit_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);
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

	if (idxd->token_limit == 0)
		return -EPERM;

	group->use_token_limit = !!val;
	return count;
}

static struct device_attribute dev_attr_group_use_token_limit =
		__ATTR(use_token_limit, 0644, group_use_token_limit_show,
		       group_use_token_limit_store);

static ssize_t group_engines_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);
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
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);
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
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);

	return sysfs_emit(buf, "%d\n", group->tc_a);
}

static ssize_t group_traffic_class_a_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);
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
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);

	return sysfs_emit(buf, "%d\n", group->tc_b);
}

static ssize_t group_traffic_class_b_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct idxd_group *group =
		container_of(dev, struct idxd_group, conf_dev);
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

	if (val < 0 || val > 7)
		return -EINVAL;

	group->tc_b = val;
	return count;
}

static struct device_attribute dev_attr_group_traffic_class_b =
		__ATTR(traffic_class_b, 0644, group_traffic_class_b_show,
		       group_traffic_class_b_store);

static struct attribute *idxd_group_attributes[] = {
	&dev_attr_group_work_queues.attr,
	&dev_attr_group_engines.attr,
	&dev_attr_group_use_token_limit.attr,
	&dev_attr_group_tokens_allowed.attr,
	&dev_attr_group_tokens_reserved.attr,
	&dev_attr_group_traffic_class_a.attr,
	&dev_attr_group_traffic_class_b.attr,
	NULL,
};

static const struct attribute_group idxd_group_attribute_group = {
	.attrs = idxd_group_attributes,
};

static const struct attribute_group *idxd_group_attribute_groups[] = {
	&idxd_group_attribute_group,
	NULL,
};

static void idxd_conf_group_release(struct device *dev)
{
	struct idxd_group *group = container_of(dev, struct idxd_group, conf_dev);

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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%d\n", wq->client_count);
}

static struct device_attribute dev_attr_wq_clients =
		__ATTR(clients, 0444, wq_clients_show, NULL);

static ssize_t wq_state_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	if (wq->group)
		return sysfs_emit(buf, "%u\n", wq->group->id);
	else
		return sysfs_emit(buf, "-1\n");
}

static ssize_t wq_group_id_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%s\n", wq_dedicated(wq) ? "dedicated" : "shared");
}

static ssize_t wq_mode_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
	struct idxd_device *idxd = wq->idxd;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	if (sysfs_streq(buf, "dedicated")) {
		set_bit(WQ_FLAG_DEDICATED, &wq->flags);
		wq->threshold = 0;
	} else if (sysfs_streq(buf, "shared") && device_swq_supported(idxd)) {
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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%u\n", wq->priority);
}

static ssize_t wq_priority_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%u\n", test_bit(WQ_FLAG_BLOCK_ON_FAULT, &wq->flags));
}

static ssize_t wq_block_on_fault_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
	struct idxd_device *idxd = wq->idxd;
	bool bof;
	int rc;

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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%u\n", wq->threshold);
}

static ssize_t wq_threshold_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%s\n", wq->name);
}

static ssize_t wq_name_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

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

	memset(wq->name, 0, WQ_NAME_SIZE + 1);
	strncpy(wq->name, buf, WQ_NAME_SIZE);
	strreplace(wq->name, '\n', '\0');
	return count;
}

static struct device_attribute dev_attr_wq_name =
		__ATTR(name, 0644, wq_name_show, wq_name_store);

static ssize_t wq_cdev_minor_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%llu\n", wq->max_xfer_bytes);
}

static ssize_t wq_max_transfer_size_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
	struct idxd_device *idxd = wq->idxd;
	u64 xfer_size;
	int rc;

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
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%u\n", wq->max_batch_size);
}

static ssize_t wq_max_batch_size_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
	struct idxd_device *idxd = wq->idxd;
	u64 batch_size;
	int rc;

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	rc = __get_sysfs_u64(buf, &batch_size);
	if (rc < 0)
		return rc;

	if (batch_size > idxd->max_batch_size)
		return -EINVAL;

	wq->max_batch_size = (u32)batch_size;

	return count;
}

static struct device_attribute dev_attr_wq_max_batch_size =
		__ATTR(max_batch_size, 0644, wq_max_batch_size_show, wq_max_batch_size_store);

static ssize_t wq_ats_disable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sysfs_emit(buf, "%u\n", wq->ats_dis);
}

static ssize_t wq_ats_disable_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);
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

	wq->ats_dis = ats_dis;

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
	NULL,
};

static const struct attribute_group idxd_wq_attribute_group = {
	.attrs = idxd_wq_attributes,
};

static const struct attribute_group *idxd_wq_attribute_groups[] = {
	&idxd_wq_attribute_group,
	NULL,
};

static void idxd_conf_wq_release(struct device *dev)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

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
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%#x\n", idxd->hw.version);
}
static DEVICE_ATTR_RO(version);

static ssize_t max_work_queues_size_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", idxd->max_wq_size);
}
static DEVICE_ATTR_RO(max_work_queues_size);

static ssize_t max_groups_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", idxd->max_groups);
}
static DEVICE_ATTR_RO(max_groups);

static ssize_t max_work_queues_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", idxd->max_wqs);
}
static DEVICE_ATTR_RO(max_work_queues);

static ssize_t max_engines_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", idxd->max_engines);
}
static DEVICE_ATTR_RO(max_engines);

static ssize_t numa_node_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%d\n", dev_to_node(&idxd->pdev->dev));
}
static DEVICE_ATTR_RO(numa_node);

static ssize_t max_batch_size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", idxd->max_batch_size);
}
static DEVICE_ATTR_RO(max_batch_size);

static ssize_t max_transfer_size_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%llu\n", idxd->max_xfer_bytes);
}
static DEVICE_ATTR_RO(max_transfer_size);

static ssize_t op_cap_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);
	int i, rc = 0;

	for (i = 0; i < 4; i++)
		rc += sysfs_emit_at(buf, rc, "%#llx ", idxd->hw.opcap.bits[i]);

	rc--;
	rc += sysfs_emit_at(buf, rc, "\n");
	return rc;
}
static DEVICE_ATTR_RO(op_cap);

static ssize_t gen_cap_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%#llx\n", idxd->hw.gen_cap.bits);
}
static DEVICE_ATTR_RO(gen_cap);

static ssize_t configurable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags));
}
static DEVICE_ATTR_RO(configurable);

static ssize_t clients_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);
	unsigned long flags;
	int count = 0, i;

	spin_lock_irqsave(&idxd->dev_lock, flags);
	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		count += wq->client_count;
	}
	spin_unlock_irqrestore(&idxd->dev_lock, flags);

	return sysfs_emit(buf, "%d\n", count);
}
static DEVICE_ATTR_RO(clients);

static ssize_t pasid_enabled_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", device_pasid_enabled(idxd));
}
static DEVICE_ATTR_RO(pasid_enabled);

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	switch (idxd->state) {
	case IDXD_DEV_DISABLED:
	case IDXD_DEV_CONF_READY:
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
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);
	int i, out = 0;
	unsigned long flags;

	spin_lock_irqsave(&idxd->dev_lock, flags);
	for (i = 0; i < 4; i++)
		out += sysfs_emit_at(buf, out, "%#018llx ", idxd->sw_err.bits[i]);
	spin_unlock_irqrestore(&idxd->dev_lock, flags);
	out--;
	out += sysfs_emit_at(buf, out, "\n");
	return out;
}
static DEVICE_ATTR_RO(errors);

static ssize_t max_tokens_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", idxd->max_tokens);
}
static DEVICE_ATTR_RO(max_tokens);

static ssize_t token_limit_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", idxd->token_limit);
}

static ssize_t token_limit_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);
	unsigned long val;
	int rc;

	rc = kstrtoul(buf, 10, &val);
	if (rc < 0)
		return -EINVAL;

	if (idxd->state == IDXD_DEV_ENABLED)
		return -EPERM;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return -EPERM;

	if (!idxd->hw.group_cap.token_limit)
		return -EPERM;

	if (val > idxd->hw.group_cap.total_tokens)
		return -EINVAL;

	idxd->token_limit = val;
	return count;
}
static DEVICE_ATTR_RW(token_limit);

static ssize_t cdev_major_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%u\n", idxd->major);
}
static DEVICE_ATTR_RO(cdev_major);

static ssize_t cmd_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd = container_of(dev, struct idxd_device, conf_dev);

	return sysfs_emit(buf, "%#x\n", idxd->cmd_status);
}
static DEVICE_ATTR_RO(cmd_status);

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
	&dev_attr_token_limit.attr,
	&dev_attr_cdev_major.attr,
	&dev_attr_cmd_status.attr,
	NULL,
};

static const struct attribute_group idxd_device_attribute_group = {
	.attrs = idxd_device_attributes,
};

static const struct attribute_group *idxd_attribute_groups[] = {
	&idxd_device_attribute_group,
	NULL,
};

static void idxd_conf_device_release(struct device *dev)
{
	struct idxd_device *idxd = container_of(dev, struct idxd_device, conf_dev);

	kfree(idxd->groups);
	kfree(idxd->wqs);
	kfree(idxd->engines);
	kfree(idxd->irq_entries);
	kfree(idxd->int_handles);
	ida_free(&idxd_ida, idxd->id);
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
	int i, j, rc;

	for (i = 0; i < idxd->max_engines; i++) {
		struct idxd_engine *engine = idxd->engines[i];

		rc = device_add(&engine->conf_dev);
		if (rc < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	j = i - 1;
	for (; i < idxd->max_engines; i++)
		put_device(&idxd->engines[i]->conf_dev);

	while (j--)
		device_unregister(&idxd->engines[j]->conf_dev);
	return rc;
}

static int idxd_register_group_devices(struct idxd_device *idxd)
{
	int i, j, rc;

	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = idxd->groups[i];

		rc = device_add(&group->conf_dev);
		if (rc < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	j = i - 1;
	for (; i < idxd->max_groups; i++)
		put_device(&idxd->groups[i]->conf_dev);

	while (j--)
		device_unregister(&idxd->groups[j]->conf_dev);
	return rc;
}

static int idxd_register_wq_devices(struct idxd_device *idxd)
{
	int i, rc, j;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		rc = device_add(&wq->conf_dev);
		if (rc < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	j = i - 1;
	for (; i < idxd->max_wqs; i++)
		put_device(&idxd->wqs[i]->conf_dev);

	while (j--)
		device_unregister(&idxd->wqs[j]->conf_dev);
	return rc;
}

int idxd_register_devices(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int rc, i;

	rc = device_add(&idxd->conf_dev);
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
		device_unregister(&idxd->engines[i]->conf_dev);
 err_engine:
	for (i = 0; i < idxd->max_wqs; i++)
		device_unregister(&idxd->wqs[i]->conf_dev);
 err_wq:
	device_del(&idxd->conf_dev);
	return rc;
}

void idxd_unregister_devices(struct idxd_device *idxd)
{
	int i;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		device_unregister(&wq->conf_dev);
	}

	for (i = 0; i < idxd->max_engines; i++) {
		struct idxd_engine *engine = idxd->engines[i];

		device_unregister(&engine->conf_dev);
	}

	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = idxd->groups[i];

		device_unregister(&group->conf_dev);
	}

	device_unregister(&idxd->conf_dev);
}

int idxd_register_bus_type(void)
{
	return bus_register(&dsa_bus_type);
}

void idxd_unregister_bus_type(void)
{
	bus_unregister(&dsa_bus_type);
}
