/*	Sysfs attributes of bond slaves
 *
 *      Copyright (c) 2014 Scott Feldman <sfeldma@cumulusnetworks.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include "bonding.h"

struct slave_attribute {
	struct attribute attr;
	ssize_t (*show)(struct slave *, char *);
};

#define SLAVE_ATTR(_name, _mode, _show)				\
const struct slave_attribute slave_attr_##_name = {		\
	.attr = {.name = __stringify(_name),			\
		 .mode = _mode },				\
	.show	= _show,					\
};
#define SLAVE_ATTR_RO(_name) \
	SLAVE_ATTR(_name, S_IRUGO, _name##_show)

static ssize_t state_show(struct slave *slave, char *buf)
{
	switch (bond_slave_state(slave)) {
	case BOND_STATE_ACTIVE:
		return sprintf(buf, "active\n");
	case BOND_STATE_BACKUP:
		return sprintf(buf, "backup\n");
	default:
		return sprintf(buf, "UNKONWN\n");
	}
}
static SLAVE_ATTR_RO(state);

static ssize_t mii_status_show(struct slave *slave, char *buf)
{
	return sprintf(buf, "%s\n", bond_slave_link_status(slave->link));
}
static SLAVE_ATTR_RO(mii_status);

static ssize_t link_failure_count_show(struct slave *slave, char *buf)
{
	return sprintf(buf, "%d\n", slave->link_failure_count);
}
static SLAVE_ATTR_RO(link_failure_count);

static ssize_t perm_hwaddr_show(struct slave *slave, char *buf)
{
	return sprintf(buf, "%pM\n", slave->perm_hwaddr);
}
static SLAVE_ATTR_RO(perm_hwaddr);

static ssize_t queue_id_show(struct slave *slave, char *buf)
{
	return sprintf(buf, "%d\n", slave->queue_id);
}
static SLAVE_ATTR_RO(queue_id);

static ssize_t ad_aggregator_id_show(struct slave *slave, char *buf)
{
	const struct aggregator *agg;

	if (slave->bond->params.mode == BOND_MODE_8023AD) {
		agg = SLAVE_AD_INFO(slave).port.aggregator;
		if (agg)
			return sprintf(buf, "%d\n",
				       agg->aggregator_identifier);
	}

	return sprintf(buf, "N/A\n");
}
static SLAVE_ATTR_RO(ad_aggregator_id);

static const struct slave_attribute *slave_attrs[] = {
	&slave_attr_state,
	&slave_attr_mii_status,
	&slave_attr_link_failure_count,
	&slave_attr_perm_hwaddr,
	&slave_attr_queue_id,
	&slave_attr_ad_aggregator_id,
	NULL
};

#define to_slave_attr(_at) container_of(_at, struct slave_attribute, attr)
#define to_slave(obj)	container_of(obj, struct slave, kobj)

static ssize_t slave_show(struct kobject *kobj,
			  struct attribute *attr, char *buf)
{
	struct slave_attribute *slave_attr = to_slave_attr(attr);
	struct slave *slave = to_slave(kobj);

	return slave_attr->show(slave, buf);
}

static const struct sysfs_ops slave_sysfs_ops = {
	.show = slave_show,
};

static struct kobj_type slave_ktype = {
#ifdef CONFIG_SYSFS
	.sysfs_ops = &slave_sysfs_ops,
#endif
};

int bond_sysfs_slave_add(struct slave *slave)
{
	const struct slave_attribute **a;
	int err;

	err = kobject_init_and_add(&slave->kobj, &slave_ktype,
				   &(slave->dev->dev.kobj), "slave");
	if (err)
		return err;

	for (a = slave_attrs; *a; ++a) {
		err = sysfs_create_file(&slave->kobj, &((*a)->attr));
		if (err) {
			kobject_del(&slave->kobj);
			return err;
		}
	}

	return 0;
}

void bond_sysfs_slave_del(struct slave *slave)
{
	const struct slave_attribute **a;

	for (a = slave_attrs; *a; ++a)
		sysfs_remove_file(&slave->kobj, &((*a)->attr));

	kobject_del(&slave->kobj);
}
