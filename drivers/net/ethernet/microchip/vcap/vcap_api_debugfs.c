// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API debug file system support
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 *
 */

#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/netdevice.h>

#include "vcap_api_debugfs.h"

struct vcap_admin_debugfs_info {
	struct vcap_control *vctrl;
	struct vcap_admin *admin;
};

struct vcap_port_debugfs_info {
	struct vcap_control *vctrl;
	struct net_device *ndev;
};

/* Show the port configuration and status */
static int vcap_port_debugfs_show(struct seq_file *m, void *unused)
{
	struct vcap_port_debugfs_info *info = m->private;
	struct vcap_admin *admin;
	struct vcap_output_print out = {
		.prf = (void *)seq_printf,
		.dst = m,
	};

	list_for_each_entry(admin, &info->vctrl->list, list) {
		if (admin->vinst)
			continue;
		info->vctrl->ops->port_info(info->ndev, admin, &out);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vcap_port_debugfs);

void vcap_port_debugfs(struct device *dev, struct dentry *parent,
		       struct vcap_control *vctrl,
		       struct net_device *ndev)
{
	struct vcap_port_debugfs_info *info;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return;

	info->vctrl = vctrl;
	info->ndev = ndev;
	debugfs_create_file(netdev_name(ndev), 0444, parent, info,
			    &vcap_port_debugfs_fops);
}
EXPORT_SYMBOL_GPL(vcap_port_debugfs);

/* Show the raw VCAP instance data (rules with address info) */
static int vcap_raw_debugfs_show(struct seq_file *m, void *unused)
{
	/* The output will be added later */
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vcap_raw_debugfs);

struct dentry *vcap_debugfs(struct device *dev, struct dentry *parent,
			    struct vcap_control *vctrl)
{
	struct vcap_admin_debugfs_info *info;
	struct vcap_admin *admin;
	struct dentry *dir;
	char name[50];

	dir = debugfs_create_dir("vcaps", parent);
	if (PTR_ERR_OR_ZERO(dir))
		return NULL;
	list_for_each_entry(admin, &vctrl->list, list) {
		sprintf(name, "raw_%s_%d", vctrl->vcaps[admin->vtype].name,
			admin->vinst);
		info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
		if (!info)
			return NULL;
		info->vctrl = vctrl;
		info->admin = admin;
		debugfs_create_file(name, 0444, dir, info,
				    &vcap_raw_debugfs_fops);
	}
	return dir;
}
EXPORT_SYMBOL_GPL(vcap_debugfs);
