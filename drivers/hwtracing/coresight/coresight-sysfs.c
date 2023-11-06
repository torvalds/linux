// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/device.h>
#include <linux/kernel.h>

#include "coresight-priv.h"

/*
 * Connections group - links attribute.
 * Count of created links between coresight components in the group.
 */
static ssize_t nr_links_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	return sprintf(buf, "%d\n", csdev->nr_links);
}
static DEVICE_ATTR_RO(nr_links);

static struct attribute *coresight_conns_attrs[] = {
	&dev_attr_nr_links.attr,
	NULL,
};

static struct attribute_group coresight_conns_group = {
	.attrs = coresight_conns_attrs,
	.name = "connections",
};

/*
 * Create connections group for CoreSight devices.
 * This group will then be used to collate the sysfs links between
 * devices.
 */
int coresight_create_conns_sysfs_group(struct coresight_device *csdev)
{
	int ret = 0;

	if (!csdev)
		return -EINVAL;

	ret = sysfs_create_group(&csdev->dev.kobj, &coresight_conns_group);
	if (ret)
		return ret;

	csdev->has_conns_grp = true;
	return ret;
}

void coresight_remove_conns_sysfs_group(struct coresight_device *csdev)
{
	if (!csdev)
		return;

	if (csdev->has_conns_grp) {
		sysfs_remove_group(&csdev->dev.kobj, &coresight_conns_group);
		csdev->has_conns_grp = false;
	}
}

int coresight_add_sysfs_link(struct coresight_sysfs_link *info)
{
	int ret = 0;

	if (!info)
		return -EINVAL;
	if (!info->orig || !info->target ||
	    !info->orig_name || !info->target_name)
		return -EINVAL;
	if (!info->orig->has_conns_grp || !info->target->has_conns_grp)
		return -EINVAL;

	/* first link orig->target */
	ret = sysfs_add_link_to_group(&info->orig->dev.kobj,
				      coresight_conns_group.name,
				      &info->target->dev.kobj,
				      info->orig_name);
	if (ret)
		return ret;

	/* second link target->orig */
	ret = sysfs_add_link_to_group(&info->target->dev.kobj,
				      coresight_conns_group.name,
				      &info->orig->dev.kobj,
				      info->target_name);

	/* error in second link - remove first - otherwise inc counts */
	if (ret) {
		sysfs_remove_link_from_group(&info->orig->dev.kobj,
					     coresight_conns_group.name,
					     info->orig_name);
	} else {
		info->orig->nr_links++;
		info->target->nr_links++;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(coresight_add_sysfs_link);

void coresight_remove_sysfs_link(struct coresight_sysfs_link *info)
{
	if (!info)
		return;
	if (!info->orig || !info->target ||
	    !info->orig_name || !info->target_name)
		return;

	sysfs_remove_link_from_group(&info->orig->dev.kobj,
				     coresight_conns_group.name,
				     info->orig_name);

	sysfs_remove_link_from_group(&info->target->dev.kobj,
				     coresight_conns_group.name,
				     info->target_name);

	info->orig->nr_links--;
	info->target->nr_links--;
}
EXPORT_SYMBOL_GPL(coresight_remove_sysfs_link);

/*
 * coresight_make_links: Make a link for a connection from a @orig
 * device to @target, represented by @conn.
 *
 *   e.g, for devOrig[output_X] -> devTarget[input_Y] is represented
 *   as two symbolic links :
 *
 *	/sys/.../devOrig/out:X	-> /sys/.../devTarget/
 *	/sys/.../devTarget/in:Y	-> /sys/.../devOrig/
 *
 * The link names are allocated for a device where it appears. i.e, the
 * "out" link on the master and "in" link on the slave device.
 * The link info is stored in the connection record for avoiding
 * the reconstruction of names for removal.
 */
int coresight_make_links(struct coresight_device *orig,
			 struct coresight_connection *conn,
			 struct coresight_device *target)
{
	int ret = -ENOMEM;
	char *outs = NULL, *ins = NULL;
	struct coresight_sysfs_link *link = NULL;

	/* Helper devices aren't shown in sysfs */
	if (conn->dest_port == -1 && conn->src_port == -1)
		return 0;

	do {
		outs = devm_kasprintf(&orig->dev, GFP_KERNEL,
				      "out:%d", conn->src_port);
		if (!outs)
			break;
		ins = devm_kasprintf(&target->dev, GFP_KERNEL,
				     "in:%d", conn->dest_port);
		if (!ins)
			break;
		link = devm_kzalloc(&orig->dev,
				    sizeof(struct coresight_sysfs_link),
				    GFP_KERNEL);
		if (!link)
			break;

		link->orig = orig;
		link->target = target;
		link->orig_name = outs;
		link->target_name = ins;

		ret = coresight_add_sysfs_link(link);
		if (ret)
			break;

		conn->link = link;
		return 0;
	} while (0);

	return ret;
}

/*
 * coresight_remove_links: Remove the sysfs links for a given connection @conn,
 * from @orig device to @target device. See coresight_make_links() for more
 * details.
 */
void coresight_remove_links(struct coresight_device *orig,
			    struct coresight_connection *conn)
{
	if (!orig || !conn->link)
		return;

	coresight_remove_sysfs_link(conn->link);

	devm_kfree(&conn->dest_dev->dev, conn->link->target_name);
	devm_kfree(&orig->dev, conn->link->orig_name);
	devm_kfree(&orig->dev, conn->link);
	conn->link = NULL;
}
