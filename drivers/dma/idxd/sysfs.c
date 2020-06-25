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

static void idxd_conf_device_release(struct device *dev)
{
	dev_dbg(dev, "%s for %s\n", __func__, dev_name(dev));
}

static struct device_type idxd_group_device_type = {
	.name = "group",
	.release = idxd_conf_device_release,
};

static struct device_type idxd_wq_device_type = {
	.name = "wq",
	.release = idxd_conf_device_release,
};

static struct device_type idxd_engine_device_type = {
	.name = "engine",
	.release = idxd_conf_device_release,
};

static struct device_type dsa_device_type = {
	.name = "dsa",
	.release = idxd_conf_device_release,
};

static inline bool is_dsa_dev(struct device *dev)
{
	return dev ? dev->type == &dsa_device_type : false;
}

static inline bool is_idxd_dev(struct device *dev)
{
	return is_dsa_dev(dev);
}

static inline bool is_idxd_wq_dev(struct device *dev)
{
	return dev ? dev->type == &idxd_wq_device_type : false;
}

static inline bool is_idxd_wq_dmaengine(struct idxd_wq *wq)
{
	if (wq->type == IDXD_WQT_KERNEL &&
	    strcmp(wq->name, "dmaengine") == 0)
		return true;
	return false;
}

static inline bool is_idxd_wq_cdev(struct idxd_wq *wq)
{
	return wq->type == IDXD_WQT_USER;
}

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

static int idxd_config_bus_probe(struct device *dev)
{
	int rc;
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

		spin_lock_irqsave(&idxd->dev_lock, flags);

		/* Perform IDXD configuration and enabling */
		rc = idxd_device_config(idxd);
		if (rc < 0) {
			spin_unlock_irqrestore(&idxd->dev_lock, flags);
			module_put(THIS_MODULE);
			dev_warn(dev, "Device config failed: %d\n", rc);
			return rc;
		}

		/* start device */
		rc = idxd_device_enable(idxd);
		if (rc < 0) {
			spin_unlock_irqrestore(&idxd->dev_lock, flags);
			module_put(THIS_MODULE);
			dev_warn(dev, "Device enable failed: %d\n", rc);
			return rc;
		}

		spin_unlock_irqrestore(&idxd->dev_lock, flags);
		dev_info(dev, "Device %s enabled\n", dev_name(dev));

		rc = idxd_register_dma_device(idxd);
		if (rc < 0) {
			spin_unlock_irqrestore(&idxd->dev_lock, flags);
			module_put(THIS_MODULE);
			dev_dbg(dev, "Failed to register dmaengine device\n");
			return rc;
		}
		return 0;
	} else if (is_idxd_wq_dev(dev)) {
		struct idxd_wq *wq = confdev_to_wq(dev);
		struct idxd_device *idxd = wq->idxd;

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

		rc = idxd_wq_alloc_resources(wq);
		if (rc < 0) {
			mutex_unlock(&wq->wq_lock);
			dev_warn(dev, "WQ resource alloc failed\n");
			return rc;
		}

		spin_lock_irqsave(&idxd->dev_lock, flags);
		rc = idxd_device_config(idxd);
		if (rc < 0) {
			spin_unlock_irqrestore(&idxd->dev_lock, flags);
			mutex_unlock(&wq->wq_lock);
			dev_warn(dev, "Writing WQ %d config failed: %d\n",
				 wq->id, rc);
			return rc;
		}

		rc = idxd_wq_enable(wq);
		if (rc < 0) {
			spin_unlock_irqrestore(&idxd->dev_lock, flags);
			mutex_unlock(&wq->wq_lock);
			dev_warn(dev, "WQ %d enabling failed: %d\n",
				 wq->id, rc);
			return rc;
		}
		spin_unlock_irqrestore(&idxd->dev_lock, flags);

		rc = idxd_wq_map_portal(wq);
		if (rc < 0) {
			dev_warn(dev, "wq portal mapping failed: %d\n", rc);
			rc = idxd_wq_disable(wq);
			if (rc < 0)
				dev_warn(dev, "IDXD wq disable failed\n");
			spin_unlock_irqrestore(&idxd->dev_lock, flags);
			mutex_unlock(&wq->wq_lock);
			return rc;
		}

		wq->client_count = 0;

		dev_info(dev, "wq %s enabled\n", dev_name(&wq->conf_dev));

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
		return 0;
	}

	return -ENODEV;
}

static void disable_wq(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	unsigned long flags;
	int rc;

	mutex_lock(&wq->wq_lock);
	dev_dbg(dev, "%s removing WQ %s\n", __func__, dev_name(&wq->conf_dev));
	if (wq->state == IDXD_WQ_DISABLED) {
		mutex_unlock(&wq->wq_lock);
		return;
	}

	if (is_idxd_wq_dmaengine(wq))
		idxd_unregister_dma_channel(wq);
	else if (is_idxd_wq_cdev(wq))
		idxd_wq_del_cdev(wq);

	if (idxd_wq_refcount(wq))
		dev_warn(dev, "Clients has claim on wq %d: %d\n",
			 wq->id, idxd_wq_refcount(wq));

	idxd_wq_unmap_portal(wq);

	spin_lock_irqsave(&idxd->dev_lock, flags);
	rc = idxd_wq_disable(wq);
	spin_unlock_irqrestore(&idxd->dev_lock, flags);

	idxd_wq_free_resources(wq);
	wq->client_count = 0;
	mutex_unlock(&wq->wq_lock);

	if (rc < 0)
		dev_warn(dev, "Failed to disable %s: %d\n",
			 dev_name(&wq->conf_dev), rc);
	else
		dev_info(dev, "wq %s disabled\n", dev_name(&wq->conf_dev));
}

static int idxd_config_bus_remove(struct device *dev)
{
	int rc;
	unsigned long flags;

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
			struct idxd_wq *wq = &idxd->wqs[i];

			if (wq->state == IDXD_WQ_DISABLED)
				continue;
			dev_warn(dev, "Active wq %d on disable %s.\n", i,
				 dev_name(&idxd->conf_dev));
			device_release_driver(&wq->conf_dev);
		}

		idxd_unregister_dma_device(idxd);
		spin_lock_irqsave(&idxd->dev_lock, flags);
		rc = idxd_device_disable(idxd);
		spin_unlock_irqrestore(&idxd->dev_lock, flags);
		module_put(THIS_MODULE);
		if (rc < 0)
			dev_warn(dev, "Device disable failed\n");
		else
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

static struct bus_type *idxd_bus_types[] = {
	&dsa_bus_type
};

static struct idxd_device_driver dsa_drv = {
	.drv = {
		.name = "dsa",
		.bus = &dsa_bus_type,
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
	},
};

static struct idxd_device_driver *idxd_drvs[] = {
	&dsa_drv
};

struct bus_type *idxd_get_bus_type(struct idxd_device *idxd)
{
	return idxd_bus_types[idxd->type];
}

static struct device_type *idxd_get_device_type(struct idxd_device *idxd)
{
	if (idxd->type == IDXD_TYPE_DSA)
		return &dsa_device_type;
	else
		return NULL;
}

/* IDXD generic driver setup */
int idxd_register_driver(void)
{
	int i, rc;

	for (i = 0; i < IDXD_TYPE_MAX; i++) {
		rc = driver_register(&idxd_drvs[i]->drv);
		if (rc < 0)
			goto drv_fail;
	}

	return 0;

drv_fail:
	for (; i > 0; i--)
		driver_unregister(&idxd_drvs[i]->drv);
	return rc;
}

void idxd_unregister_driver(void)
{
	int i;

	for (i = 0; i < IDXD_TYPE_MAX; i++)
		driver_unregister(&idxd_drvs[i]->drv);
}

/* IDXD engine attributes */
static ssize_t engine_group_id_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct idxd_engine *engine =
		container_of(dev, struct idxd_engine, conf_dev);

	if (engine->group)
		return sprintf(buf, "%d\n", engine->group->id);
	else
		return sprintf(buf, "%d\n", -1);
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
	engine->group = &idxd->groups[id];
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

/* Group attributes */

static void idxd_set_free_tokens(struct idxd_device *idxd)
{
	int i, tokens;

	for (i = 0, tokens = 0; i < idxd->max_groups; i++) {
		struct idxd_group *g = &idxd->groups[i];

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

	return sprintf(buf, "%u\n", group->tokens_reserved);
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

	return sprintf(buf, "%u\n", group->tokens_allowed);
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

	return sprintf(buf, "%u\n", group->use_token_limit);
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
	char *tmp = buf;
	struct idxd_device *idxd = group->idxd;

	for (i = 0; i < idxd->max_engines; i++) {
		struct idxd_engine *engine = &idxd->engines[i];

		if (!engine->group)
			continue;

		if (engine->group->id == group->id)
			rc += sprintf(tmp + rc, "engine%d.%d ",
					idxd->id, engine->id);
	}

	rc--;
	rc += sprintf(tmp + rc, "\n");

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
	char *tmp = buf;
	struct idxd_device *idxd = group->idxd;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = &idxd->wqs[i];

		if (!wq->group)
			continue;

		if (wq->group->id == group->id)
			rc += sprintf(tmp + rc, "wq%d.%d ",
					idxd->id, wq->id);
	}

	rc--;
	rc += sprintf(tmp + rc, "\n");

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

	return sprintf(buf, "%d\n", group->tc_a);
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

	return sprintf(buf, "%d\n", group->tc_b);
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

/* IDXD work queue attribs */
static ssize_t wq_clients_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	return sprintf(buf, "%d\n", wq->client_count);
}

static struct device_attribute dev_attr_wq_clients =
		__ATTR(clients, 0444, wq_clients_show, NULL);

static ssize_t wq_state_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	switch (wq->state) {
	case IDXD_WQ_DISABLED:
		return sprintf(buf, "disabled\n");
	case IDXD_WQ_ENABLED:
		return sprintf(buf, "enabled\n");
	}

	return sprintf(buf, "unknown\n");
}

static struct device_attribute dev_attr_wq_state =
		__ATTR(state, 0444, wq_state_show, NULL);

static ssize_t wq_group_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	if (wq->group)
		return sprintf(buf, "%u\n", wq->group->id);
	else
		return sprintf(buf, "-1\n");
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

	group = &idxd->groups[id];
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

	return sprintf(buf, "%s\n",
			wq_dedicated(wq) ? "dedicated" : "shared");
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

	return sprintf(buf, "%u\n", wq->size);
}

static int total_claimed_wq_size(struct idxd_device *idxd)
{
	int i;
	int wq_size = 0;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = &idxd->wqs[i];

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

	if (wq->state != IDXD_WQ_DISABLED)
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

	return sprintf(buf, "%u\n", wq->priority);
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

static ssize_t wq_type_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct idxd_wq *wq = container_of(dev, struct idxd_wq, conf_dev);

	switch (wq->type) {
	case IDXD_WQT_KERNEL:
		return sprintf(buf, "%s\n",
			       idxd_wq_type_names[IDXD_WQT_KERNEL]);
	case IDXD_WQT_USER:
		return sprintf(buf, "%s\n",
			       idxd_wq_type_names[IDXD_WQT_USER]);
	case IDXD_WQT_NONE:
	default:
		return sprintf(buf, "%s\n",
			       idxd_wq_type_names[IDXD_WQT_NONE]);
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

	return sprintf(buf, "%s\n", wq->name);
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

	return sprintf(buf, "%d\n", wq->idxd_cdev.minor);
}

static struct device_attribute dev_attr_wq_cdev_minor =
		__ATTR(cdev_minor, 0444, wq_cdev_minor_show, NULL);

static struct attribute *idxd_wq_attributes[] = {
	&dev_attr_wq_clients.attr,
	&dev_attr_wq_state.attr,
	&dev_attr_wq_group_id.attr,
	&dev_attr_wq_mode.attr,
	&dev_attr_wq_size.attr,
	&dev_attr_wq_priority.attr,
	&dev_attr_wq_type.attr,
	&dev_attr_wq_name.attr,
	&dev_attr_wq_cdev_minor.attr,
	NULL,
};

static const struct attribute_group idxd_wq_attribute_group = {
	.attrs = idxd_wq_attributes,
};

static const struct attribute_group *idxd_wq_attribute_groups[] = {
	&idxd_wq_attribute_group,
	NULL,
};

/* IDXD device attribs */
static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%#x\n", idxd->hw.version);
}
static DEVICE_ATTR_RO(version);

static ssize_t max_work_queues_size_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%u\n", idxd->max_wq_size);
}
static DEVICE_ATTR_RO(max_work_queues_size);

static ssize_t max_groups_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%u\n", idxd->max_groups);
}
static DEVICE_ATTR_RO(max_groups);

static ssize_t max_work_queues_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%u\n", idxd->max_wqs);
}
static DEVICE_ATTR_RO(max_work_queues);

static ssize_t max_engines_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%u\n", idxd->max_engines);
}
static DEVICE_ATTR_RO(max_engines);

static ssize_t numa_node_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%d\n", dev_to_node(&idxd->pdev->dev));
}
static DEVICE_ATTR_RO(numa_node);

static ssize_t max_batch_size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%u\n", idxd->max_batch_size);
}
static DEVICE_ATTR_RO(max_batch_size);

static ssize_t max_transfer_size_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%llu\n", idxd->max_xfer_bytes);
}
static DEVICE_ATTR_RO(max_transfer_size);

static ssize_t op_cap_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%#llx\n", idxd->hw.opcap.bits[0]);
}
static DEVICE_ATTR_RO(op_cap);

static ssize_t gen_cap_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%#llx\n", idxd->hw.gen_cap.bits);
}
static DEVICE_ATTR_RO(gen_cap);

static ssize_t configurable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%u\n",
			test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags));
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
		struct idxd_wq *wq = &idxd->wqs[i];

		count += wq->client_count;
	}
	spin_unlock_irqrestore(&idxd->dev_lock, flags);

	return sprintf(buf, "%d\n", count);
}
static DEVICE_ATTR_RO(clients);

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	switch (idxd->state) {
	case IDXD_DEV_DISABLED:
	case IDXD_DEV_CONF_READY:
		return sprintf(buf, "disabled\n");
	case IDXD_DEV_ENABLED:
		return sprintf(buf, "enabled\n");
	case IDXD_DEV_HALTED:
		return sprintf(buf, "halted\n");
	}

	return sprintf(buf, "unknown\n");
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
		out += sprintf(buf + out, "%#018llx ", idxd->sw_err.bits[i]);
	spin_unlock_irqrestore(&idxd->dev_lock, flags);
	out--;
	out += sprintf(buf + out, "\n");
	return out;
}
static DEVICE_ATTR_RO(errors);

static ssize_t max_tokens_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%u\n", idxd->max_tokens);
}
static DEVICE_ATTR_RO(max_tokens);

static ssize_t token_limit_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct idxd_device *idxd =
		container_of(dev, struct idxd_device, conf_dev);

	return sprintf(buf, "%u\n", idxd->token_limit);
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

	return sprintf(buf, "%u\n", idxd->major);
}
static DEVICE_ATTR_RO(cdev_major);

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
	&dev_attr_state.attr,
	&dev_attr_errors.attr,
	&dev_attr_max_tokens.attr,
	&dev_attr_token_limit.attr,
	&dev_attr_cdev_major.attr,
	NULL,
};

static const struct attribute_group idxd_device_attribute_group = {
	.attrs = idxd_device_attributes,
};

static const struct attribute_group *idxd_attribute_groups[] = {
	&idxd_device_attribute_group,
	NULL,
};

static int idxd_setup_engine_sysfs(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int i, rc;

	for (i = 0; i < idxd->max_engines; i++) {
		struct idxd_engine *engine = &idxd->engines[i];

		engine->conf_dev.parent = &idxd->conf_dev;
		dev_set_name(&engine->conf_dev, "engine%d.%d",
			     idxd->id, engine->id);
		engine->conf_dev.bus = idxd_get_bus_type(idxd);
		engine->conf_dev.groups = idxd_engine_attribute_groups;
		engine->conf_dev.type = &idxd_engine_device_type;
		dev_dbg(dev, "Engine device register: %s\n",
			dev_name(&engine->conf_dev));
		rc = device_register(&engine->conf_dev);
		if (rc < 0) {
			put_device(&engine->conf_dev);
			goto cleanup;
		}
	}

	return 0;

cleanup:
	while (i--) {
		struct idxd_engine *engine = &idxd->engines[i];

		device_unregister(&engine->conf_dev);
	}
	return rc;
}

static int idxd_setup_group_sysfs(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int i, rc;

	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = &idxd->groups[i];

		group->conf_dev.parent = &idxd->conf_dev;
		dev_set_name(&group->conf_dev, "group%d.%d",
			     idxd->id, group->id);
		group->conf_dev.bus = idxd_get_bus_type(idxd);
		group->conf_dev.groups = idxd_group_attribute_groups;
		group->conf_dev.type = &idxd_group_device_type;
		dev_dbg(dev, "Group device register: %s\n",
			dev_name(&group->conf_dev));
		rc = device_register(&group->conf_dev);
		if (rc < 0) {
			put_device(&group->conf_dev);
			goto cleanup;
		}
	}

	return 0;

cleanup:
	while (i--) {
		struct idxd_group *group = &idxd->groups[i];

		device_unregister(&group->conf_dev);
	}
	return rc;
}

static int idxd_setup_wq_sysfs(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int i, rc;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = &idxd->wqs[i];

		wq->conf_dev.parent = &idxd->conf_dev;
		dev_set_name(&wq->conf_dev, "wq%d.%d", idxd->id, wq->id);
		wq->conf_dev.bus = idxd_get_bus_type(idxd);
		wq->conf_dev.groups = idxd_wq_attribute_groups;
		wq->conf_dev.type = &idxd_wq_device_type;
		dev_dbg(dev, "WQ device register: %s\n",
			dev_name(&wq->conf_dev));
		rc = device_register(&wq->conf_dev);
		if (rc < 0) {
			put_device(&wq->conf_dev);
			goto cleanup;
		}
	}

	return 0;

cleanup:
	while (i--) {
		struct idxd_wq *wq = &idxd->wqs[i];

		device_unregister(&wq->conf_dev);
	}
	return rc;
}

static int idxd_setup_device_sysfs(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int rc;
	char devname[IDXD_NAME_SIZE];

	sprintf(devname, "%s%d", idxd_get_dev_name(idxd), idxd->id);
	idxd->conf_dev.parent = dev;
	dev_set_name(&idxd->conf_dev, "%s", devname);
	idxd->conf_dev.bus = idxd_get_bus_type(idxd);
	idxd->conf_dev.groups = idxd_attribute_groups;
	idxd->conf_dev.type = idxd_get_device_type(idxd);

	dev_dbg(dev, "IDXD device register: %s\n", dev_name(&idxd->conf_dev));
	rc = device_register(&idxd->conf_dev);
	if (rc < 0) {
		put_device(&idxd->conf_dev);
		return rc;
	}

	return 0;
}

int idxd_setup_sysfs(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int rc;

	rc = idxd_setup_device_sysfs(idxd);
	if (rc < 0) {
		dev_dbg(dev, "Device sysfs registering failed: %d\n", rc);
		return rc;
	}

	rc = idxd_setup_wq_sysfs(idxd);
	if (rc < 0) {
		/* unregister conf dev */
		dev_dbg(dev, "Work Queue sysfs registering failed: %d\n", rc);
		return rc;
	}

	rc = idxd_setup_group_sysfs(idxd);
	if (rc < 0) {
		/* unregister conf dev */
		dev_dbg(dev, "Group sysfs registering failed: %d\n", rc);
		return rc;
	}

	rc = idxd_setup_engine_sysfs(idxd);
	if (rc < 0) {
		/* unregister conf dev */
		dev_dbg(dev, "Engine sysfs registering failed: %d\n", rc);
		return rc;
	}

	return 0;
}

void idxd_cleanup_sysfs(struct idxd_device *idxd)
{
	int i;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = &idxd->wqs[i];

		device_unregister(&wq->conf_dev);
	}

	for (i = 0; i < idxd->max_engines; i++) {
		struct idxd_engine *engine = &idxd->engines[i];

		device_unregister(&engine->conf_dev);
	}

	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = &idxd->groups[i];

		device_unregister(&group->conf_dev);
	}

	device_unregister(&idxd->conf_dev);
}

int idxd_register_bus_type(void)
{
	int i, rc;

	for (i = 0; i < IDXD_TYPE_MAX; i++) {
		rc = bus_register(idxd_bus_types[i]);
		if (rc < 0)
			goto bus_err;
	}

	return 0;

bus_err:
	for (; i > 0; i--)
		bus_unregister(idxd_bus_types[i]);
	return rc;
}

void idxd_unregister_bus_type(void)
{
	int i;

	for (i = 0; i < IDXD_TYPE_MAX; i++)
		bus_unregister(idxd_bus_types[i]);
}
