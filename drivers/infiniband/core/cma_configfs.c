/*
 * Copyright (c) 2015, Mellanox Technologies inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/configfs.h>
#include <rdma/ib_verbs.h>
#include "core_priv.h"

struct cma_device;

struct cma_dev_group;

struct cma_dev_port_group {
	unsigned int		port_num;
	struct cma_dev_group	*cma_dev_group;
	struct config_group	group;
};

struct cma_dev_group {
	char				name[IB_DEVICE_NAME_MAX];
	struct config_group		device_group;
	struct config_group		ports_group;
	struct cma_dev_port_group	*ports;
};

static struct cma_dev_port_group *to_dev_port_group(struct config_item *item)
{
	struct config_group *group;

	if (!item)
		return NULL;

	group = container_of(item, struct config_group, cg_item);
	return container_of(group, struct cma_dev_port_group, group);
}

static bool filter_by_name(struct ib_device *ib_dev, void *cookie)
{
	return !strcmp(dev_name(&ib_dev->dev), cookie);
}

static int cma_configfs_params_get(struct config_item *item,
				   struct cma_device **pcma_dev,
				   struct cma_dev_port_group **pgroup)
{
	struct cma_dev_port_group *group = to_dev_port_group(item);
	struct cma_device *cma_dev;

	if (!group)
		return -ENODEV;

	cma_dev = cma_enum_devices_by_ibdev(filter_by_name,
					    group->cma_dev_group->name);
	if (!cma_dev)
		return -ENODEV;

	*pcma_dev = cma_dev;
	*pgroup = group;

	return 0;
}

static void cma_configfs_params_put(struct cma_device *cma_dev)
{
	cma_deref_dev(cma_dev);
}

static ssize_t default_roce_mode_show(struct config_item *item,
				      char *buf)
{
	struct cma_device *cma_dev;
	struct cma_dev_port_group *group;
	int gid_type;
	ssize_t ret;

	ret = cma_configfs_params_get(item, &cma_dev, &group);
	if (ret)
		return ret;

	gid_type = cma_get_default_gid_type(cma_dev, group->port_num);
	cma_configfs_params_put(cma_dev);

	if (gid_type < 0)
		return gid_type;

	return sprintf(buf, "%s\n", ib_cache_gid_type_str(gid_type));
}

static ssize_t default_roce_mode_store(struct config_item *item,
				       const char *buf, size_t count)
{
	struct cma_device *cma_dev;
	struct cma_dev_port_group *group;
	int gid_type = ib_cache_gid_parse_type_str(buf);
	ssize_t ret;

	if (gid_type < 0)
		return -EINVAL;

	ret = cma_configfs_params_get(item, &cma_dev, &group);
	if (ret)
		return ret;

	ret = cma_set_default_gid_type(cma_dev, group->port_num, gid_type);

	cma_configfs_params_put(cma_dev);

	return !ret ? strnlen(buf, count) : ret;
}

CONFIGFS_ATTR(, default_roce_mode);

static ssize_t default_roce_tos_show(struct config_item *item, char *buf)
{
	struct cma_device *cma_dev;
	struct cma_dev_port_group *group;
	ssize_t ret;
	u8 tos;

	ret = cma_configfs_params_get(item, &cma_dev, &group);
	if (ret)
		return ret;

	tos = cma_get_default_roce_tos(cma_dev, group->port_num);
	cma_configfs_params_put(cma_dev);

	return sprintf(buf, "%u\n", tos);
}

static ssize_t default_roce_tos_store(struct config_item *item,
				      const char *buf, size_t count)
{
	struct cma_device *cma_dev;
	struct cma_dev_port_group *group;
	ssize_t ret;
	u8 tos;

	ret = kstrtou8(buf, 0, &tos);
	if (ret)
		return ret;

	ret = cma_configfs_params_get(item, &cma_dev, &group);
	if (ret)
		return ret;

	ret = cma_set_default_roce_tos(cma_dev, group->port_num, tos);
	cma_configfs_params_put(cma_dev);

	return ret ? ret : strnlen(buf, count);
}

CONFIGFS_ATTR(, default_roce_tos);

static struct configfs_attribute *cma_configfs_attributes[] = {
	&attr_default_roce_mode,
	&attr_default_roce_tos,
	NULL,
};

static const struct config_item_type cma_port_group_type = {
	.ct_attrs	= cma_configfs_attributes,
	.ct_owner	= THIS_MODULE
};

static int make_cma_ports(struct cma_dev_group *cma_dev_group,
			  struct cma_device *cma_dev)
{
	struct ib_device *ibdev;
	unsigned int i;
	unsigned int ports_num;
	struct cma_dev_port_group *ports;
	int err;

	ibdev = cma_get_ib_dev(cma_dev);

	if (!ibdev)
		return -ENODEV;

	ports_num = ibdev->phys_port_cnt;
	ports = kcalloc(ports_num, sizeof(*cma_dev_group->ports),
			GFP_KERNEL);

	if (!ports) {
		err = -ENOMEM;
		goto free;
	}

	for (i = 0; i < ports_num; i++) {
		char port_str[10];

		ports[i].port_num = i + 1;
		snprintf(port_str, sizeof(port_str), "%u", i + 1);
		ports[i].cma_dev_group = cma_dev_group;
		config_group_init_type_name(&ports[i].group,
					    port_str,
					    &cma_port_group_type);
		configfs_add_default_group(&ports[i].group,
				&cma_dev_group->ports_group);

	}
	cma_dev_group->ports = ports;

	return 0;
free:
	kfree(ports);
	cma_dev_group->ports = NULL;
	return err;
}

static void release_cma_dev(struct config_item  *item)
{
	struct config_group *group = container_of(item, struct config_group,
						  cg_item);
	struct cma_dev_group *cma_dev_group = container_of(group,
							   struct cma_dev_group,
							   device_group);

	kfree(cma_dev_group);
};

static void release_cma_ports_group(struct config_item  *item)
{
	struct config_group *group = container_of(item, struct config_group,
						  cg_item);
	struct cma_dev_group *cma_dev_group = container_of(group,
							   struct cma_dev_group,
							   ports_group);

	kfree(cma_dev_group->ports);
	cma_dev_group->ports = NULL;
};

static struct configfs_item_operations cma_ports_item_ops = {
	.release = release_cma_ports_group
};

static const struct config_item_type cma_ports_group_type = {
	.ct_item_ops	= &cma_ports_item_ops,
	.ct_owner	= THIS_MODULE
};

static struct configfs_item_operations cma_device_item_ops = {
	.release = release_cma_dev
};

static const struct config_item_type cma_device_group_type = {
	.ct_item_ops	= &cma_device_item_ops,
	.ct_owner	= THIS_MODULE
};

static struct config_group *make_cma_dev(struct config_group *group,
					 const char *name)
{
	int err = -ENODEV;
	struct cma_device *cma_dev = cma_enum_devices_by_ibdev(filter_by_name,
							       (void *)name);
	struct cma_dev_group *cma_dev_group = NULL;

	if (!cma_dev)
		goto fail;

	cma_dev_group = kzalloc(sizeof(*cma_dev_group), GFP_KERNEL);

	if (!cma_dev_group) {
		err = -ENOMEM;
		goto fail;
	}

	strlcpy(cma_dev_group->name, name, sizeof(cma_dev_group->name));

	config_group_init_type_name(&cma_dev_group->ports_group, "ports",
				    &cma_ports_group_type);

	err = make_cma_ports(cma_dev_group, cma_dev);
	if (err)
		goto fail;

	config_group_init_type_name(&cma_dev_group->device_group, name,
				    &cma_device_group_type);
	configfs_add_default_group(&cma_dev_group->ports_group,
			&cma_dev_group->device_group);

	cma_deref_dev(cma_dev);
	return &cma_dev_group->device_group;

fail:
	if (cma_dev)
		cma_deref_dev(cma_dev);
	kfree(cma_dev_group);
	return ERR_PTR(err);
}

static struct configfs_group_operations cma_subsys_group_ops = {
	.make_group	= make_cma_dev,
};

static const struct config_item_type cma_subsys_type = {
	.ct_group_ops	= &cma_subsys_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem cma_subsys = {
	.su_group	= {
		.cg_item	= {
			.ci_namebuf	= "rdma_cm",
			.ci_type	= &cma_subsys_type,
		},
	},
};

int __init cma_configfs_init(void)
{
	config_group_init(&cma_subsys.su_group);
	mutex_init(&cma_subsys.su_mutex);
	return configfs_register_subsystem(&cma_subsys);
}

void __exit cma_configfs_exit(void)
{
	configfs_unregister_subsystem(&cma_subsys);
}
