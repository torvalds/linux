// SPDX-License-Identifier: GPL-2.0-or-later
/*	Sysfs attributes of bond slaves
 *
 *      Copyright (c) 2014 Scott Feldman <sfeldma@cumulusnetworks.com>
 */

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include <net/bonding.h>

struct slave_attribute {
	struct attribute attr;
	ssize_t (*show)(struct slave *, char *);
};

#define SLAVE_ATTR_RO(_name)					\
const struct slave_attribute slave_attr_##_name = __ATTR_RO(_name)

static ssize_t state_show(struct slave *slave, char *buf)
{
	switch (bond_slave_state(slave)) {
	case BOND_STATE_ACTIVE:
		return sysfs_emit(buf, "active\n");
	case BOND_STATE_BACKUP:
		return sysfs_emit(buf, "backup\n");
	default:
		return sysfs_emit(buf, "UNKNOWN\n");
	}
}
static SLAVE_ATTR_RO(state);

static ssize_t mii_status_show(struct slave *slave, char *buf)
{
	return sysfs_emit(buf, "%s\n", bond_slave_link_status(slave->link));
}
static SLAVE_ATTR_RO(mii_status);

static ssize_t link_failure_count_show(struct slave *slave, char *buf)
{
	return sysfs_emit(buf, "%d\n", slave->link_failure_count);
}
static SLAVE_ATTR_RO(link_failure_count);

static ssize_t perm_hwaddr_show(struct slave *slave, char *buf)
{
	return sysfs_emit(buf, "%*phC\n",
		       slave->dev->addr_len,
		       slave->perm_hwaddr);
}
static SLAVE_ATTR_RO(perm_hwaddr);

static ssize_t queue_id_show(struct slave *slave, char *buf)
{
	return sysfs_emit(buf, "%d\n", slave->queue_id);
}
static SLAVE_ATTR_RO(queue_id);

static ssize_t ad_aggregator_id_show(struct slave *slave, char *buf)
{
	const struct aggregator *agg;

	if (BOND_MODE(slave->bond) == BOND_MODE_8023AD) {
		agg = SLAVE_AD_INFO(slave)->port.aggregator;
		if (agg)
			return sysfs_emit(buf, "%d\n",
					  agg->aggregator_identifier);
	}

	return sysfs_emit(buf, "N/A\n");
}
static SLAVE_ATTR_RO(ad_aggregator_id);

static ssize_t ad_actor_oper_port_state_show(struct slave *slave, char *buf)
{
	const struct port *ad_port;

	if (BOND_MODE(slave->bond) == BOND_MODE_8023AD) {
		ad_port = &SLAVE_AD_INFO(slave)->port;
		if (ad_port->aggregator)
			return sysfs_emit(buf, "%u\n",
				       ad_port->actor_oper_port_state);
	}

	return sysfs_emit(buf, "N/A\n");
}
static SLAVE_ATTR_RO(ad_actor_oper_port_state);

static ssize_t ad_partner_oper_port_state_show(struct slave *slave, char *buf)
{
	const struct port *ad_port;

	if (BOND_MODE(slave->bond) == BOND_MODE_8023AD) {
		ad_port = &SLAVE_AD_INFO(slave)->port;
		if (ad_port->aggregator)
			return sysfs_emit(buf, "%u\n",
				       ad_port->partner_oper.port_state);
	}

	return sysfs_emit(buf, "N/A\n");
}
static SLAVE_ATTR_RO(ad_partner_oper_port_state);

static const struct attribute *slave_attrs[] = {
	&slave_attr_state.attr,
	&slave_attr_mii_status.attr,
	&slave_attr_link_failure_count.attr,
	&slave_attr_perm_hwaddr.attr,
	&slave_attr_queue_id.attr,
	&slave_attr_ad_aggregator_id.attr,
	&slave_attr_ad_actor_oper_port_state.attr,
	&slave_attr_ad_partner_oper_port_state.attr,
	NULL
};

#define to_slave_attr(_at) container_of(_at, struct slave_attribute, attr)

static ssize_t slave_show(struct kobject *kobj,
			  struct attribute *attr, char *buf)
{
	struct slave_attribute *slave_attr = to_slave_attr(attr);
	struct slave *slave = to_slave(kobj);

	return slave_attr->show(slave, buf);
}

const struct sysfs_ops slave_sysfs_ops = {
	.show = slave_show,
};

int bond_sysfs_slave_add(struct slave *slave)
{
	return sysfs_create_files(&slave->kobj, slave_attrs);
}

void bond_sysfs_slave_del(struct slave *slave)
{
	sysfs_remove_files(&slave->kobj, slave_attrs);
}
