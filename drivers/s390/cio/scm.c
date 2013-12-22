/*
 * Recognize and maintain s390 storage class memory.
 *
 * Copyright IBM Corp. 2012
 * Author(s): Sebastian Ott <sebott@linux.vnet.ibm.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <asm/eadm.h>
#include "chsc.h"

static struct device *scm_root;

#define to_scm_dev(n) container_of(n, struct scm_device, dev)
#define	to_scm_drv(d) container_of(d, struct scm_driver, drv)

static int scmdev_probe(struct device *dev)
{
	struct scm_device *scmdev = to_scm_dev(dev);
	struct scm_driver *scmdrv = to_scm_drv(dev->driver);

	return scmdrv->probe ? scmdrv->probe(scmdev) : -ENODEV;
}

static int scmdev_remove(struct device *dev)
{
	struct scm_device *scmdev = to_scm_dev(dev);
	struct scm_driver *scmdrv = to_scm_drv(dev->driver);

	return scmdrv->remove ? scmdrv->remove(scmdev) : -ENODEV;
}

static int scmdev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return add_uevent_var(env, "MODALIAS=scm:scmdev");
}

static struct bus_type scm_bus_type = {
	.name  = "scm",
	.probe = scmdev_probe,
	.remove = scmdev_remove,
	.uevent = scmdev_uevent,
};

/**
 * scm_driver_register() - register a scm driver
 * @scmdrv: driver to be registered
 */
int scm_driver_register(struct scm_driver *scmdrv)
{
	struct device_driver *drv = &scmdrv->drv;

	drv->bus = &scm_bus_type;

	return driver_register(drv);
}
EXPORT_SYMBOL_GPL(scm_driver_register);

/**
 * scm_driver_unregister() - deregister a scm driver
 * @scmdrv: driver to be deregistered
 */
void scm_driver_unregister(struct scm_driver *scmdrv)
{
	driver_unregister(&scmdrv->drv);
}
EXPORT_SYMBOL_GPL(scm_driver_unregister);

void scm_irq_handler(struct aob *aob, int error)
{
	struct aob_rq_header *aobrq = (void *) aob->request.data;
	struct scm_device *scmdev = aobrq->scmdev;
	struct scm_driver *scmdrv = to_scm_drv(scmdev->dev.driver);

	scmdrv->handler(scmdev, aobrq->data, error);
}
EXPORT_SYMBOL_GPL(scm_irq_handler);

#define scm_attr(name)							\
static ssize_t show_##name(struct device *dev,				\
	       struct device_attribute *attr, char *buf)		\
{									\
	struct scm_device *scmdev = to_scm_dev(dev);			\
	int ret;							\
									\
	device_lock(dev);						\
	ret = sprintf(buf, "%u\n", scmdev->attrs.name);			\
	device_unlock(dev);						\
									\
	return ret;							\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL);

scm_attr(persistence);
scm_attr(oper_state);
scm_attr(data_state);
scm_attr(rank);
scm_attr(release);
scm_attr(res_id);

static struct attribute *scmdev_attrs[] = {
	&dev_attr_persistence.attr,
	&dev_attr_oper_state.attr,
	&dev_attr_data_state.attr,
	&dev_attr_rank.attr,
	&dev_attr_release.attr,
	&dev_attr_res_id.attr,
	NULL,
};

static struct attribute_group scmdev_attr_group = {
	.attrs = scmdev_attrs,
};

static const struct attribute_group *scmdev_attr_groups[] = {
	&scmdev_attr_group,
	NULL,
};

static void scmdev_release(struct device *dev)
{
	struct scm_device *scmdev = to_scm_dev(dev);

	kfree(scmdev);
}

static void scmdev_setup(struct scm_device *scmdev, struct sale *sale,
			 unsigned int size, unsigned int max_blk_count)
{
	dev_set_name(&scmdev->dev, "%016llx", (unsigned long long) sale->sa);
	scmdev->nr_max_block = max_blk_count;
	scmdev->address = sale->sa;
	scmdev->size = 1UL << size;
	scmdev->attrs.rank = sale->rank;
	scmdev->attrs.persistence = sale->p;
	scmdev->attrs.oper_state = sale->op_state;
	scmdev->attrs.data_state = sale->data_state;
	scmdev->attrs.rank = sale->rank;
	scmdev->attrs.release = sale->r;
	scmdev->attrs.res_id = sale->rid;
	scmdev->dev.parent = scm_root;
	scmdev->dev.bus = &scm_bus_type;
	scmdev->dev.release = scmdev_release;
	scmdev->dev.groups = scmdev_attr_groups;
}

/*
 * Check for state-changes, notify the driver and userspace.
 */
static void scmdev_update(struct scm_device *scmdev, struct sale *sale)
{
	struct scm_driver *scmdrv;
	bool changed;

	device_lock(&scmdev->dev);
	changed = scmdev->attrs.rank != sale->rank ||
		  scmdev->attrs.oper_state != sale->op_state;
	scmdev->attrs.rank = sale->rank;
	scmdev->attrs.oper_state = sale->op_state;
	if (!scmdev->dev.driver)
		goto out;
	scmdrv = to_scm_drv(scmdev->dev.driver);
	if (changed && scmdrv->notify)
		scmdrv->notify(scmdev, SCM_CHANGE);
out:
	device_unlock(&scmdev->dev);
	if (changed)
		kobject_uevent(&scmdev->dev.kobj, KOBJ_CHANGE);
}

static int check_address(struct device *dev, void *data)
{
	struct scm_device *scmdev = to_scm_dev(dev);
	struct sale *sale = data;

	return scmdev->address == sale->sa;
}

static struct scm_device *scmdev_find(struct sale *sale)
{
	struct device *dev;

	dev = bus_find_device(&scm_bus_type, NULL, sale, check_address);

	return dev ? to_scm_dev(dev) : NULL;
}

static int scm_add(struct chsc_scm_info *scm_info, size_t num)
{
	struct sale *sale, *scmal = scm_info->scmal;
	struct scm_device *scmdev;
	int ret;

	for (sale = scmal; sale < scmal + num; sale++) {
		scmdev = scmdev_find(sale);
		if (scmdev) {
			scmdev_update(scmdev, sale);
			/* Release reference from scm_find(). */
			put_device(&scmdev->dev);
			continue;
		}
		scmdev = kzalloc(sizeof(*scmdev), GFP_KERNEL);
		if (!scmdev)
			return -ENODEV;
		scmdev_setup(scmdev, sale, scm_info->is, scm_info->mbc);
		ret = device_register(&scmdev->dev);
		if (ret) {
			/* Release reference from device_initialize(). */
			put_device(&scmdev->dev);
			return ret;
		}
	}

	return 0;
}

int scm_update_information(void)
{
	struct chsc_scm_info *scm_info;
	u64 token = 0;
	size_t num;
	int ret;

	scm_info = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
	if (!scm_info)
		return -ENOMEM;

	do {
		ret = chsc_scm_info(scm_info, token);
		if (ret)
			break;

		num = (scm_info->response.length -
		       (offsetof(struct chsc_scm_info, scmal) -
			offsetof(struct chsc_scm_info, response))
		      ) / sizeof(struct sale);

		ret = scm_add(scm_info, num);
		if (ret)
			break;

		token = scm_info->restok;
	} while (token);

	free_page((unsigned long)scm_info);

	return ret;
}

static int scm_dev_avail(struct device *dev, void *unused)
{
	struct scm_driver *scmdrv = to_scm_drv(dev->driver);
	struct scm_device *scmdev = to_scm_dev(dev);

	if (dev->driver && scmdrv->notify)
		scmdrv->notify(scmdev, SCM_AVAIL);

	return 0;
}

int scm_process_availability_information(void)
{
	return bus_for_each_dev(&scm_bus_type, NULL, NULL, scm_dev_avail);
}

static int __init scm_init(void)
{
	int ret;

	ret = bus_register(&scm_bus_type);
	if (ret)
		return ret;

	scm_root = root_device_register("scm");
	if (IS_ERR(scm_root)) {
		bus_unregister(&scm_bus_type);
		return PTR_ERR(scm_root);
	}

	scm_update_information();
	return 0;
}
subsys_initcall_sync(scm_init);
