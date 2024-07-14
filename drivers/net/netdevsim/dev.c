/*
 * Copyright (c) 2018 Cumulus Networks. All rights reserved.
 * Copyright (c) 2018 David Ahern <dsa@cumulusnetworks.com>
 * Copyright (c) 2019 Mellanox Technologies. All rights reserved.
 *
 * This software is licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree.
 *
 * THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS"
 * WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE
 * OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME
 * THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/inet.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>
#include <net/devlink.h>
#include <net/ip.h>
#include <net/flow_offload.h>
#include <uapi/linux/devlink.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/udp.h>

#include "netdevsim.h"

static unsigned int
nsim_dev_port_index(enum nsim_dev_port_type type, unsigned int port_index)
{
	switch (type) {
	case NSIM_DEV_PORT_TYPE_VF:
		port_index = NSIM_DEV_VF_PORT_INDEX_BASE + port_index;
		break;
	case NSIM_DEV_PORT_TYPE_PF:
		break;
	}

	return port_index;
}

static inline unsigned int nsim_dev_port_index_to_vf_index(unsigned int port_index)
{
	return port_index - NSIM_DEV_VF_PORT_INDEX_BASE;
}

static struct dentry *nsim_dev_ddir;

unsigned int nsim_dev_get_vfs(struct nsim_dev *nsim_dev)
{
	WARN_ON(!lockdep_rtnl_is_held() &&
		!devl_lock_is_held(priv_to_devlink(nsim_dev)));

	return nsim_dev->nsim_bus_dev->num_vfs;
}

static void
nsim_bus_dev_set_vfs(struct nsim_bus_dev *nsim_bus_dev, unsigned int num_vfs)
{
	rtnl_lock();
	nsim_bus_dev->num_vfs = num_vfs;
	rtnl_unlock();
}

#define NSIM_DEV_DUMMY_REGION_SIZE (1024 * 32)

static int
nsim_dev_take_snapshot(struct devlink *devlink,
		       const struct devlink_region_ops *ops,
		       struct netlink_ext_ack *extack,
		       u8 **data)
{
	void *dummy_data;

	dummy_data = kmalloc(NSIM_DEV_DUMMY_REGION_SIZE, GFP_KERNEL);
	if (!dummy_data)
		return -ENOMEM;

	get_random_bytes(dummy_data, NSIM_DEV_DUMMY_REGION_SIZE);

	*data = dummy_data;

	return 0;
}

static ssize_t nsim_dev_take_snapshot_write(struct file *file,
					    const char __user *data,
					    size_t count, loff_t *ppos)
{
	struct nsim_dev *nsim_dev = file->private_data;
	struct devlink *devlink;
	u8 *dummy_data;
	int err;
	u32 id;

	devlink = priv_to_devlink(nsim_dev);

	err = nsim_dev_take_snapshot(devlink, NULL, NULL, &dummy_data);
	if (err)
		return err;

	err = devlink_region_snapshot_id_get(devlink, &id);
	if (err) {
		pr_err("Failed to get snapshot id\n");
		kfree(dummy_data);
		return err;
	}
	err = devlink_region_snapshot_create(nsim_dev->dummy_region,
					     dummy_data, id);
	devlink_region_snapshot_id_put(devlink, id);
	if (err) {
		pr_err("Failed to create region snapshot\n");
		kfree(dummy_data);
		return err;
	}

	return count;
}

static const struct file_operations nsim_dev_take_snapshot_fops = {
	.open = simple_open,
	.write = nsim_dev_take_snapshot_write,
	.llseek = generic_file_llseek,
	.owner = THIS_MODULE,
};

static ssize_t nsim_dev_trap_fa_cookie_read(struct file *file,
					    char __user *data,
					    size_t count, loff_t *ppos)
{
	struct nsim_dev *nsim_dev = file->private_data;
	struct flow_action_cookie *fa_cookie;
	unsigned int buf_len;
	ssize_t ret;
	char *buf;

	spin_lock(&nsim_dev->fa_cookie_lock);
	fa_cookie = nsim_dev->fa_cookie;
	if (!fa_cookie) {
		ret = -EINVAL;
		goto errout;
	}
	buf_len = fa_cookie->cookie_len * 2;
	buf = kmalloc(buf_len, GFP_ATOMIC);
	if (!buf) {
		ret = -ENOMEM;
		goto errout;
	}
	bin2hex(buf, fa_cookie->cookie, fa_cookie->cookie_len);
	spin_unlock(&nsim_dev->fa_cookie_lock);

	ret = simple_read_from_buffer(data, count, ppos, buf, buf_len);

	kfree(buf);
	return ret;

errout:
	spin_unlock(&nsim_dev->fa_cookie_lock);
	return ret;
}

static ssize_t nsim_dev_trap_fa_cookie_write(struct file *file,
					     const char __user *data,
					     size_t count, loff_t *ppos)
{
	struct nsim_dev *nsim_dev = file->private_data;
	struct flow_action_cookie *fa_cookie;
	size_t cookie_len;
	ssize_t ret;
	char *buf;

	if (*ppos != 0)
		return -EINVAL;
	cookie_len = (count - 1) / 2;
	if ((count - 1) % 2)
		return -EINVAL;

	buf = memdup_user(data, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	fa_cookie = kmalloc(sizeof(*fa_cookie) + cookie_len,
			    GFP_KERNEL | __GFP_NOWARN);
	if (!fa_cookie) {
		ret = -ENOMEM;
		goto free_buf;
	}

	fa_cookie->cookie_len = cookie_len;
	ret = hex2bin(fa_cookie->cookie, buf, cookie_len);
	if (ret)
		goto free_fa_cookie;
	kfree(buf);

	spin_lock(&nsim_dev->fa_cookie_lock);
	kfree(nsim_dev->fa_cookie);
	nsim_dev->fa_cookie = fa_cookie;
	spin_unlock(&nsim_dev->fa_cookie_lock);

	return count;

free_fa_cookie:
	kfree(fa_cookie);
free_buf:
	kfree(buf);
	return ret;
}

static const struct file_operations nsim_dev_trap_fa_cookie_fops = {
	.open = simple_open,
	.read = nsim_dev_trap_fa_cookie_read,
	.write = nsim_dev_trap_fa_cookie_write,
	.llseek = generic_file_llseek,
	.owner = THIS_MODULE,
};

static ssize_t nsim_bus_dev_max_vfs_read(struct file *file, char __user *data,
					 size_t count, loff_t *ppos)
{
	struct nsim_dev *nsim_dev = file->private_data;
	char buf[11];
	ssize_t len;

	len = scnprintf(buf, sizeof(buf), "%u\n",
			READ_ONCE(nsim_dev->nsim_bus_dev->max_vfs));

	return simple_read_from_buffer(data, count, ppos, buf, len);
}

static ssize_t nsim_bus_dev_max_vfs_write(struct file *file,
					  const char __user *data,
					  size_t count, loff_t *ppos)
{
	struct nsim_vf_config *vfconfigs;
	struct nsim_dev *nsim_dev;
	char buf[10];
	ssize_t ret;
	u32 val;

	if (*ppos != 0)
		return 0;

	if (count >= sizeof(buf))
		return -ENOSPC;

	ret = copy_from_user(buf, data, count);
	if (ret)
		return -EFAULT;
	buf[count] = '\0';

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return -EINVAL;

	/* max_vfs limited by the maximum number of provided port indexes */
	if (val > NSIM_DEV_VF_PORT_INDEX_MAX - NSIM_DEV_VF_PORT_INDEX_BASE)
		return -ERANGE;

	vfconfigs = kcalloc(val, sizeof(struct nsim_vf_config),
			    GFP_KERNEL | __GFP_NOWARN);
	if (!vfconfigs)
		return -ENOMEM;

	nsim_dev = file->private_data;
	devl_lock(priv_to_devlink(nsim_dev));
	/* Reject if VFs are configured */
	if (nsim_dev_get_vfs(nsim_dev)) {
		ret = -EBUSY;
	} else {
		swap(nsim_dev->vfconfigs, vfconfigs);
		WRITE_ONCE(nsim_dev->nsim_bus_dev->max_vfs, val);
		*ppos += count;
		ret = count;
	}
	devl_unlock(priv_to_devlink(nsim_dev));

	kfree(vfconfigs);
	return ret;
}

static const struct file_operations nsim_dev_max_vfs_fops = {
	.open = simple_open,
	.read = nsim_bus_dev_max_vfs_read,
	.write = nsim_bus_dev_max_vfs_write,
	.llseek = generic_file_llseek,
	.owner = THIS_MODULE,
};

static int nsim_dev_debugfs_init(struct nsim_dev *nsim_dev)
{
	char dev_ddir_name[sizeof(DRV_NAME) + 10];
	int err;

	sprintf(dev_ddir_name, DRV_NAME "%u", nsim_dev->nsim_bus_dev->dev.id);
	nsim_dev->ddir = debugfs_create_dir(dev_ddir_name, nsim_dev_ddir);
	if (IS_ERR(nsim_dev->ddir))
		return PTR_ERR(nsim_dev->ddir);
	nsim_dev->ports_ddir = debugfs_create_dir("ports", nsim_dev->ddir);
	if (IS_ERR(nsim_dev->ports_ddir)) {
		err = PTR_ERR(nsim_dev->ports_ddir);
		goto err_ddir;
	}
	debugfs_create_bool("fw_update_status", 0600, nsim_dev->ddir,
			    &nsim_dev->fw_update_status);
	debugfs_create_u32("fw_update_overwrite_mask", 0600, nsim_dev->ddir,
			    &nsim_dev->fw_update_overwrite_mask);
	debugfs_create_u32("max_macs", 0600, nsim_dev->ddir,
			   &nsim_dev->max_macs);
	debugfs_create_bool("test1", 0600, nsim_dev->ddir,
			    &nsim_dev->test1);
	nsim_dev->take_snapshot = debugfs_create_file("take_snapshot",
						      0200,
						      nsim_dev->ddir,
						      nsim_dev,
						&nsim_dev_take_snapshot_fops);
	debugfs_create_bool("dont_allow_reload", 0600, nsim_dev->ddir,
			    &nsim_dev->dont_allow_reload);
	debugfs_create_bool("fail_reload", 0600, nsim_dev->ddir,
			    &nsim_dev->fail_reload);
	debugfs_create_file("trap_flow_action_cookie", 0600, nsim_dev->ddir,
			    nsim_dev, &nsim_dev_trap_fa_cookie_fops);
	debugfs_create_bool("fail_trap_group_set", 0600,
			    nsim_dev->ddir,
			    &nsim_dev->fail_trap_group_set);
	debugfs_create_bool("fail_trap_policer_set", 0600,
			    nsim_dev->ddir,
			    &nsim_dev->fail_trap_policer_set);
	debugfs_create_bool("fail_trap_policer_counter_get", 0600,
			    nsim_dev->ddir,
			    &nsim_dev->fail_trap_policer_counter_get);
	/* caution, dev_max_vfs write takes devlink lock */
	debugfs_create_file("max_vfs", 0600, nsim_dev->ddir,
			    nsim_dev, &nsim_dev_max_vfs_fops);

	nsim_dev->nodes_ddir = debugfs_create_dir("rate_nodes", nsim_dev->ddir);
	if (IS_ERR(nsim_dev->nodes_ddir)) {
		err = PTR_ERR(nsim_dev->nodes_ddir);
		goto err_ports_ddir;
	}
	debugfs_create_bool("fail_trap_drop_counter_get", 0600,
			    nsim_dev->ddir,
			    &nsim_dev->fail_trap_drop_counter_get);
	nsim_udp_tunnels_debugfs_create(nsim_dev);
	return 0;

err_ports_ddir:
	debugfs_remove_recursive(nsim_dev->ports_ddir);
err_ddir:
	debugfs_remove_recursive(nsim_dev->ddir);
	return err;
}

static void nsim_dev_debugfs_exit(struct nsim_dev *nsim_dev)
{
	debugfs_remove_recursive(nsim_dev->nodes_ddir);
	debugfs_remove_recursive(nsim_dev->ports_ddir);
	debugfs_remove_recursive(nsim_dev->ddir);
}

static ssize_t nsim_dev_rate_parent_read(struct file *file,
					 char __user *data,
					 size_t count, loff_t *ppos)
{
	char **name_ptr = file->private_data;
	size_t len;

	if (!*name_ptr)
		return 0;

	len = strlen(*name_ptr);
	return simple_read_from_buffer(data, count, ppos, *name_ptr, len);
}

static const struct file_operations nsim_dev_rate_parent_fops = {
	.open = simple_open,
	.read = nsim_dev_rate_parent_read,
	.llseek = generic_file_llseek,
	.owner = THIS_MODULE,
};

static int nsim_dev_port_debugfs_init(struct nsim_dev *nsim_dev,
				      struct nsim_dev_port *nsim_dev_port)
{
	struct nsim_bus_dev *nsim_bus_dev = nsim_dev->nsim_bus_dev;
	unsigned int port_index = nsim_dev_port->port_index;
	char port_ddir_name[16];
	char dev_link_name[32];

	sprintf(port_ddir_name, "%u", port_index);
	nsim_dev_port->ddir = debugfs_create_dir(port_ddir_name,
						 nsim_dev->ports_ddir);
	if (IS_ERR(nsim_dev_port->ddir))
		return PTR_ERR(nsim_dev_port->ddir);

	sprintf(dev_link_name, "../../../" DRV_NAME "%u", nsim_bus_dev->dev.id);
	if (nsim_dev_port_is_vf(nsim_dev_port)) {
		unsigned int vf_id = nsim_dev_port_index_to_vf_index(port_index);

		debugfs_create_u16("tx_share", 0400, nsim_dev_port->ddir,
				   &nsim_dev->vfconfigs[vf_id].min_tx_rate);
		debugfs_create_u16("tx_max", 0400, nsim_dev_port->ddir,
				   &nsim_dev->vfconfigs[vf_id].max_tx_rate);
		nsim_dev_port->rate_parent = debugfs_create_file("rate_parent",
								 0400,
								 nsim_dev_port->ddir,
								 &nsim_dev_port->parent_name,
								 &nsim_dev_rate_parent_fops);
	}
	debugfs_create_symlink("dev", nsim_dev_port->ddir, dev_link_name);

	return 0;
}

static void nsim_dev_port_debugfs_exit(struct nsim_dev_port *nsim_dev_port)
{
	debugfs_remove_recursive(nsim_dev_port->ddir);
}

static int nsim_dev_resources_register(struct devlink *devlink)
{
	struct devlink_resource_size_params params = {
		.size_max = (u64)-1,
		.size_granularity = 1,
		.unit = DEVLINK_RESOURCE_UNIT_ENTRY
	};
	int err;

	/* Resources for IPv4 */
	err = devl_resource_register(devlink, "IPv4", (u64)-1,
				     NSIM_RESOURCE_IPV4,
				     DEVLINK_RESOURCE_ID_PARENT_TOP,
				     &params);
	if (err) {
		pr_err("Failed to register IPv4 top resource\n");
		goto err_out;
	}

	err = devl_resource_register(devlink, "fib", (u64)-1,
				     NSIM_RESOURCE_IPV4_FIB,
				     NSIM_RESOURCE_IPV4, &params);
	if (err) {
		pr_err("Failed to register IPv4 FIB resource\n");
		goto err_out;
	}

	err = devl_resource_register(devlink, "fib-rules", (u64)-1,
				     NSIM_RESOURCE_IPV4_FIB_RULES,
				     NSIM_RESOURCE_IPV4, &params);
	if (err) {
		pr_err("Failed to register IPv4 FIB rules resource\n");
		goto err_out;
	}

	/* Resources for IPv6 */
	err = devl_resource_register(devlink, "IPv6", (u64)-1,
				     NSIM_RESOURCE_IPV6,
				     DEVLINK_RESOURCE_ID_PARENT_TOP,
				     &params);
	if (err) {
		pr_err("Failed to register IPv6 top resource\n");
		goto err_out;
	}

	err = devl_resource_register(devlink, "fib", (u64)-1,
				     NSIM_RESOURCE_IPV6_FIB,
				     NSIM_RESOURCE_IPV6, &params);
	if (err) {
		pr_err("Failed to register IPv6 FIB resource\n");
		goto err_out;
	}

	err = devl_resource_register(devlink, "fib-rules", (u64)-1,
				     NSIM_RESOURCE_IPV6_FIB_RULES,
				     NSIM_RESOURCE_IPV6, &params);
	if (err) {
		pr_err("Failed to register IPv6 FIB rules resource\n");
		goto err_out;
	}

	/* Resources for nexthops */
	err = devl_resource_register(devlink, "nexthops", (u64)-1,
				     NSIM_RESOURCE_NEXTHOPS,
				     DEVLINK_RESOURCE_ID_PARENT_TOP,
				     &params);
	if (err) {
		pr_err("Failed to register NEXTHOPS resource\n");
		goto err_out;
	}
	return 0;

err_out:
	devl_resources_unregister(devlink);
	return err;
}

enum nsim_devlink_param_id {
	NSIM_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	NSIM_DEVLINK_PARAM_ID_TEST1,
};

static const struct devlink_param nsim_devlink_params[] = {
	DEVLINK_PARAM_GENERIC(MAX_MACS,
			      BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, NULL),
	DEVLINK_PARAM_DRIVER(NSIM_DEVLINK_PARAM_ID_TEST1,
			     "test1", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			     NULL, NULL, NULL),
};

static void nsim_devlink_set_params_init_values(struct nsim_dev *nsim_dev,
						struct devlink *devlink)
{
	union devlink_param_value value;

	value.vu32 = nsim_dev->max_macs;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
					   value);
	value.vbool = nsim_dev->test1;
	devlink_param_driverinit_value_set(devlink,
					   NSIM_DEVLINK_PARAM_ID_TEST1,
					   value);
}

static void nsim_devlink_param_load_driverinit_values(struct devlink *devlink)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);
	union devlink_param_value saved_value;
	int err;

	err = devlink_param_driverinit_value_get(devlink,
						 DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
						 &saved_value);
	if (!err)
		nsim_dev->max_macs = saved_value.vu32;
	err = devlink_param_driverinit_value_get(devlink,
						 NSIM_DEVLINK_PARAM_ID_TEST1,
						 &saved_value);
	if (!err)
		nsim_dev->test1 = saved_value.vbool;
}

#define NSIM_DEV_DUMMY_REGION_SNAPSHOT_MAX 16

static const struct devlink_region_ops dummy_region_ops = {
	.name = "dummy",
	.destructor = &kfree,
	.snapshot = nsim_dev_take_snapshot,
};

static int nsim_dev_dummy_region_init(struct nsim_dev *nsim_dev,
				      struct devlink *devlink)
{
	nsim_dev->dummy_region =
		devl_region_create(devlink, &dummy_region_ops,
				   NSIM_DEV_DUMMY_REGION_SNAPSHOT_MAX,
				   NSIM_DEV_DUMMY_REGION_SIZE);
	return PTR_ERR_OR_ZERO(nsim_dev->dummy_region);
}

static void nsim_dev_dummy_region_exit(struct nsim_dev *nsim_dev)
{
	devl_region_destroy(nsim_dev->dummy_region);
}

static int
__nsim_dev_port_add(struct nsim_dev *nsim_dev, enum nsim_dev_port_type type,
		    unsigned int port_index);
static void __nsim_dev_port_del(struct nsim_dev_port *nsim_dev_port);

static int nsim_esw_legacy_enable(struct nsim_dev *nsim_dev,
				  struct netlink_ext_ack *extack)
{
	struct devlink *devlink = priv_to_devlink(nsim_dev);
	struct nsim_dev_port *nsim_dev_port, *tmp;

	devl_rate_nodes_destroy(devlink);
	list_for_each_entry_safe(nsim_dev_port, tmp, &nsim_dev->port_list, list)
		if (nsim_dev_port_is_vf(nsim_dev_port))
			__nsim_dev_port_del(nsim_dev_port);
	nsim_dev->esw_mode = DEVLINK_ESWITCH_MODE_LEGACY;
	return 0;
}

static int nsim_esw_switchdev_enable(struct nsim_dev *nsim_dev,
				     struct netlink_ext_ack *extack)
{
	struct nsim_dev_port *nsim_dev_port, *tmp;
	int i, err;

	for (i = 0; i < nsim_dev_get_vfs(nsim_dev); i++) {
		err = __nsim_dev_port_add(nsim_dev, NSIM_DEV_PORT_TYPE_VF, i);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to initialize VFs' netdevsim ports");
			pr_err("Failed to initialize VF id=%d. %d.\n", i, err);
			goto err_port_add_vfs;
		}
	}
	nsim_dev->esw_mode = DEVLINK_ESWITCH_MODE_SWITCHDEV;
	return 0;

err_port_add_vfs:
	list_for_each_entry_safe(nsim_dev_port, tmp, &nsim_dev->port_list, list)
		if (nsim_dev_port_is_vf(nsim_dev_port))
			__nsim_dev_port_del(nsim_dev_port);
	return err;
}

static int nsim_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode,
					 struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);

	if (mode == nsim_dev->esw_mode)
		return 0;

	if (mode == DEVLINK_ESWITCH_MODE_LEGACY)
		return nsim_esw_legacy_enable(nsim_dev, extack);
	if (mode == DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return nsim_esw_switchdev_enable(nsim_dev, extack);

	return -EINVAL;
}

static int nsim_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);

	*mode = nsim_dev->esw_mode;
	return 0;
}

struct nsim_trap_item {
	void *trap_ctx;
	enum devlink_trap_action action;
};

struct nsim_trap_data {
	struct delayed_work trap_report_dw;
	struct nsim_trap_item *trap_items_arr;
	u64 *trap_policers_cnt_arr;
	u64 trap_pkt_cnt;
	struct nsim_dev *nsim_dev;
	spinlock_t trap_lock;	/* Protects trap_items_arr */
};

/* All driver-specific traps must be documented in
 * Documentation/networking/devlink/netdevsim.rst
 */
enum {
	NSIM_TRAP_ID_BASE = DEVLINK_TRAP_GENERIC_ID_MAX,
	NSIM_TRAP_ID_FID_MISS,
};

#define NSIM_TRAP_NAME_FID_MISS "fid_miss"

#define NSIM_TRAP_METADATA DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT

#define NSIM_TRAP_DROP(_id, _group_id)					      \
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,				      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     NSIM_TRAP_METADATA)
#define NSIM_TRAP_DROP_EXT(_id, _group_id, _metadata)			      \
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,				      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     NSIM_TRAP_METADATA | (_metadata))
#define NSIM_TRAP_EXCEPTION(_id, _group_id)				      \
	DEVLINK_TRAP_GENERIC(EXCEPTION, TRAP, _id,			      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     NSIM_TRAP_METADATA)
#define NSIM_TRAP_CONTROL(_id, _group_id, _action)			      \
	DEVLINK_TRAP_GENERIC(CONTROL, _action, _id,			      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     NSIM_TRAP_METADATA)
#define NSIM_TRAP_DRIVER_EXCEPTION(_id, _group_id)			      \
	DEVLINK_TRAP_DRIVER(EXCEPTION, TRAP, NSIM_TRAP_ID_##_id,	      \
			    NSIM_TRAP_NAME_##_id,			      \
			    DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			    NSIM_TRAP_METADATA)

#define NSIM_DEV_TRAP_POLICER_MIN_RATE	1
#define NSIM_DEV_TRAP_POLICER_MAX_RATE	8000
#define NSIM_DEV_TRAP_POLICER_MIN_BURST	8
#define NSIM_DEV_TRAP_POLICER_MAX_BURST	65536

#define NSIM_TRAP_POLICER(_id, _rate, _burst)				      \
	DEVLINK_TRAP_POLICER(_id, _rate, _burst,			      \
			     NSIM_DEV_TRAP_POLICER_MAX_RATE,		      \
			     NSIM_DEV_TRAP_POLICER_MIN_RATE,		      \
			     NSIM_DEV_TRAP_POLICER_MAX_BURST,		      \
			     NSIM_DEV_TRAP_POLICER_MIN_BURST)

static const struct devlink_trap_policer nsim_trap_policers_arr[] = {
	NSIM_TRAP_POLICER(1, 1000, 128),
	NSIM_TRAP_POLICER(2, 2000, 256),
	NSIM_TRAP_POLICER(3, 3000, 512),
};

static const struct devlink_trap_group nsim_trap_groups_arr[] = {
	DEVLINK_TRAP_GROUP_GENERIC(L2_DROPS, 0),
	DEVLINK_TRAP_GROUP_GENERIC(L3_DROPS, 1),
	DEVLINK_TRAP_GROUP_GENERIC(L3_EXCEPTIONS, 1),
	DEVLINK_TRAP_GROUP_GENERIC(BUFFER_DROPS, 2),
	DEVLINK_TRAP_GROUP_GENERIC(ACL_DROPS, 3),
	DEVLINK_TRAP_GROUP_GENERIC(MC_SNOOPING, 3),
};

static const struct devlink_trap nsim_traps_arr[] = {
	NSIM_TRAP_DROP(SMAC_MC, L2_DROPS),
	NSIM_TRAP_DROP(VLAN_TAG_MISMATCH, L2_DROPS),
	NSIM_TRAP_DROP(INGRESS_VLAN_FILTER, L2_DROPS),
	NSIM_TRAP_DROP(INGRESS_STP_FILTER, L2_DROPS),
	NSIM_TRAP_DROP(EMPTY_TX_LIST, L2_DROPS),
	NSIM_TRAP_DROP(PORT_LOOPBACK_FILTER, L2_DROPS),
	NSIM_TRAP_DRIVER_EXCEPTION(FID_MISS, L2_DROPS),
	NSIM_TRAP_DROP(BLACKHOLE_ROUTE, L3_DROPS),
	NSIM_TRAP_EXCEPTION(TTL_ERROR, L3_EXCEPTIONS),
	NSIM_TRAP_DROP(TAIL_DROP, BUFFER_DROPS),
	NSIM_TRAP_DROP_EXT(INGRESS_FLOW_ACTION_DROP, ACL_DROPS,
			   DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE),
	NSIM_TRAP_DROP_EXT(EGRESS_FLOW_ACTION_DROP, ACL_DROPS,
			   DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE),
	NSIM_TRAP_CONTROL(IGMP_QUERY, MC_SNOOPING, MIRROR),
	NSIM_TRAP_CONTROL(IGMP_V1_REPORT, MC_SNOOPING, TRAP),
};

#define NSIM_TRAP_L4_DATA_LEN 100

static struct sk_buff *nsim_dev_trap_skb_build(void)
{
	int tot_len, data_len = NSIM_TRAP_L4_DATA_LEN;
	struct sk_buff *skb;
	struct udphdr *udph;
	struct ethhdr *eth;
	struct iphdr *iph;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (!skb)
		return NULL;
	tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + data_len;

	skb_reset_mac_header(skb);
	eth = skb_put(skb, sizeof(struct ethhdr));
	eth_random_addr(eth->h_dest);
	eth_random_addr(eth->h_source);
	eth->h_proto = htons(ETH_P_IP);
	skb->protocol = htons(ETH_P_IP);

	skb_set_network_header(skb, skb->len);
	iph = skb_put(skb, sizeof(struct iphdr));
	iph->protocol = IPPROTO_UDP;
	iph->saddr = in_aton("192.0.2.1");
	iph->daddr = in_aton("198.51.100.1");
	iph->version = 0x4;
	iph->frag_off = 0;
	iph->ihl = 0x5;
	iph->tot_len = htons(tot_len);
	iph->ttl = 100;
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

	skb_set_transport_header(skb, skb->len);
	udph = skb_put_zero(skb, sizeof(struct udphdr) + data_len);
	get_random_bytes(&udph->source, sizeof(u16));
	get_random_bytes(&udph->dest, sizeof(u16));
	udph->len = htons(sizeof(struct udphdr) + data_len);

	return skb;
}

static void nsim_dev_trap_report(struct nsim_dev_port *nsim_dev_port)
{
	struct nsim_dev *nsim_dev = nsim_dev_port->ns->nsim_dev;
	struct devlink *devlink = priv_to_devlink(nsim_dev);
	struct nsim_trap_data *nsim_trap_data;
	int i;

	nsim_trap_data = nsim_dev->trap_data;

	spin_lock(&nsim_trap_data->trap_lock);
	for (i = 0; i < ARRAY_SIZE(nsim_traps_arr); i++) {
		struct flow_action_cookie *fa_cookie = NULL;
		struct nsim_trap_item *nsim_trap_item;
		struct sk_buff *skb;
		bool has_fa_cookie;

		has_fa_cookie = nsim_traps_arr[i].metadata_cap &
				DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE;

		nsim_trap_item = &nsim_trap_data->trap_items_arr[i];
		if (nsim_trap_item->action == DEVLINK_TRAP_ACTION_DROP)
			continue;

		skb = nsim_dev_trap_skb_build();
		if (!skb)
			continue;
		skb->dev = nsim_dev_port->ns->netdev;

		/* Trapped packets are usually passed to devlink in softIRQ,
		 * but in this case they are generated in a workqueue. Disable
		 * softIRQs to prevent lockdep from complaining about
		 * "incosistent lock state".
		 */

		spin_lock_bh(&nsim_dev->fa_cookie_lock);
		fa_cookie = has_fa_cookie ? nsim_dev->fa_cookie : NULL;
		devlink_trap_report(devlink, skb, nsim_trap_item->trap_ctx,
				    &nsim_dev_port->devlink_port, fa_cookie);
		spin_unlock_bh(&nsim_dev->fa_cookie_lock);
		consume_skb(skb);
	}
	spin_unlock(&nsim_trap_data->trap_lock);
}

#define NSIM_TRAP_REPORT_INTERVAL_MS	100

static void nsim_dev_trap_report_work(struct work_struct *work)
{
	struct nsim_trap_data *nsim_trap_data;
	struct nsim_dev_port *nsim_dev_port;
	struct nsim_dev *nsim_dev;

	nsim_trap_data = container_of(work, struct nsim_trap_data,
				      trap_report_dw.work);
	nsim_dev = nsim_trap_data->nsim_dev;

	if (!devl_trylock(priv_to_devlink(nsim_dev))) {
		schedule_delayed_work(&nsim_dev->trap_data->trap_report_dw, 1);
		return;
	}

	/* For each running port and enabled packet trap, generate a UDP
	 * packet with a random 5-tuple and report it.
	 */
	list_for_each_entry(nsim_dev_port, &nsim_dev->port_list, list) {
		if (!netif_running(nsim_dev_port->ns->netdev))
			continue;

		nsim_dev_trap_report(nsim_dev_port);
	}
	devl_unlock(priv_to_devlink(nsim_dev));

	schedule_delayed_work(&nsim_dev->trap_data->trap_report_dw,
			      msecs_to_jiffies(NSIM_TRAP_REPORT_INTERVAL_MS));
}

static int nsim_dev_traps_init(struct devlink *devlink)
{
	size_t policers_count = ARRAY_SIZE(nsim_trap_policers_arr);
	struct nsim_dev *nsim_dev = devlink_priv(devlink);
	struct nsim_trap_data *nsim_trap_data;
	int err;

	nsim_trap_data = kzalloc(sizeof(*nsim_trap_data), GFP_KERNEL);
	if (!nsim_trap_data)
		return -ENOMEM;

	nsim_trap_data->trap_items_arr = kcalloc(ARRAY_SIZE(nsim_traps_arr),
						 sizeof(struct nsim_trap_item),
						 GFP_KERNEL);
	if (!nsim_trap_data->trap_items_arr) {
		err = -ENOMEM;
		goto err_trap_data_free;
	}

	nsim_trap_data->trap_policers_cnt_arr = kcalloc(policers_count,
							sizeof(u64),
							GFP_KERNEL);
	if (!nsim_trap_data->trap_policers_cnt_arr) {
		err = -ENOMEM;
		goto err_trap_items_free;
	}

	/* The lock is used to protect the action state of the registered
	 * traps. The value is written by user and read in delayed work when
	 * iterating over all the traps.
	 */
	spin_lock_init(&nsim_trap_data->trap_lock);
	nsim_trap_data->nsim_dev = nsim_dev;
	nsim_dev->trap_data = nsim_trap_data;

	err = devl_trap_policers_register(devlink, nsim_trap_policers_arr,
					  policers_count);
	if (err)
		goto err_trap_policers_cnt_free;

	err = devl_trap_groups_register(devlink, nsim_trap_groups_arr,
					ARRAY_SIZE(nsim_trap_groups_arr));
	if (err)
		goto err_trap_policers_unregister;

	err = devl_traps_register(devlink, nsim_traps_arr,
				  ARRAY_SIZE(nsim_traps_arr), NULL);
	if (err)
		goto err_trap_groups_unregister;

	INIT_DELAYED_WORK(&nsim_dev->trap_data->trap_report_dw,
			  nsim_dev_trap_report_work);
	schedule_delayed_work(&nsim_dev->trap_data->trap_report_dw,
			      msecs_to_jiffies(NSIM_TRAP_REPORT_INTERVAL_MS));

	return 0;

err_trap_groups_unregister:
	devl_trap_groups_unregister(devlink, nsim_trap_groups_arr,
				    ARRAY_SIZE(nsim_trap_groups_arr));
err_trap_policers_unregister:
	devl_trap_policers_unregister(devlink, nsim_trap_policers_arr,
				      ARRAY_SIZE(nsim_trap_policers_arr));
err_trap_policers_cnt_free:
	kfree(nsim_trap_data->trap_policers_cnt_arr);
err_trap_items_free:
	kfree(nsim_trap_data->trap_items_arr);
err_trap_data_free:
	kfree(nsim_trap_data);
	return err;
}

static void nsim_dev_traps_exit(struct devlink *devlink)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);

	/* caution, trap work takes devlink lock */
	cancel_delayed_work_sync(&nsim_dev->trap_data->trap_report_dw);
	devl_traps_unregister(devlink, nsim_traps_arr,
			      ARRAY_SIZE(nsim_traps_arr));
	devl_trap_groups_unregister(devlink, nsim_trap_groups_arr,
				    ARRAY_SIZE(nsim_trap_groups_arr));
	devl_trap_policers_unregister(devlink, nsim_trap_policers_arr,
				      ARRAY_SIZE(nsim_trap_policers_arr));
	kfree(nsim_dev->trap_data->trap_policers_cnt_arr);
	kfree(nsim_dev->trap_data->trap_items_arr);
	kfree(nsim_dev->trap_data);
}

static int nsim_dev_reload_create(struct nsim_dev *nsim_dev,
				  struct netlink_ext_ack *extack);
static void nsim_dev_reload_destroy(struct nsim_dev *nsim_dev);

static int nsim_dev_reload_down(struct devlink *devlink, bool netns_change,
				enum devlink_reload_action action, enum devlink_reload_limit limit,
				struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);

	if (nsim_dev->dont_allow_reload) {
		/* For testing purposes, user set debugfs dont_allow_reload
		 * value to true. So forbid it.
		 */
		NL_SET_ERR_MSG_MOD(extack, "User forbid the reload for testing purposes");
		return -EOPNOTSUPP;
	}

	nsim_dev_reload_destroy(nsim_dev);
	return 0;
}

static int nsim_dev_reload_up(struct devlink *devlink, enum devlink_reload_action action,
			      enum devlink_reload_limit limit, u32 *actions_performed,
			      struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);

	if (nsim_dev->fail_reload) {
		/* For testing purposes, user set debugfs fail_reload
		 * value to true. Fail right away.
		 */
		NL_SET_ERR_MSG_MOD(extack, "User setup the reload to fail for testing purposes");
		return -EINVAL;
	}

	*actions_performed = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);

	return nsim_dev_reload_create(nsim_dev, extack);
}

static int nsim_dev_info_get(struct devlink *devlink,
			     struct devlink_info_req *req,
			     struct netlink_ext_ack *extack)
{
	int err;

	err = devlink_info_driver_name_put(req, DRV_NAME);
	if (err)
		return err;
	err = devlink_info_version_stored_put_ext(req, "fw.mgmt", "10.20.30",
						  DEVLINK_INFO_VERSION_TYPE_COMPONENT);
	if (err)
		return err;
	return devlink_info_version_running_put_ext(req, "fw.mgmt", "10.20.30",
						    DEVLINK_INFO_VERSION_TYPE_COMPONENT);
}

#define NSIM_DEV_FLASH_SIZE 500000
#define NSIM_DEV_FLASH_CHUNK_SIZE 1000
#define NSIM_DEV_FLASH_CHUNK_TIME_MS 10

static int nsim_dev_flash_update(struct devlink *devlink,
				 struct devlink_flash_update_params *params,
				 struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);
	int i;

	if ((params->overwrite_mask & ~nsim_dev->fw_update_overwrite_mask) != 0)
		return -EOPNOTSUPP;

	if (nsim_dev->fw_update_status) {
		devlink_flash_update_status_notify(devlink,
						   "Preparing to flash",
						   params->component, 0, 0);
	}

	for (i = 0; i < NSIM_DEV_FLASH_SIZE / NSIM_DEV_FLASH_CHUNK_SIZE; i++) {
		if (nsim_dev->fw_update_status)
			devlink_flash_update_status_notify(devlink, "Flashing",
							   params->component,
							   i * NSIM_DEV_FLASH_CHUNK_SIZE,
							   NSIM_DEV_FLASH_SIZE);
		msleep(NSIM_DEV_FLASH_CHUNK_TIME_MS);
	}

	if (nsim_dev->fw_update_status) {
		devlink_flash_update_status_notify(devlink, "Flashing",
						   params->component,
						   NSIM_DEV_FLASH_SIZE,
						   NSIM_DEV_FLASH_SIZE);
		devlink_flash_update_timeout_notify(devlink, "Flash select",
						    params->component, 81);
		devlink_flash_update_status_notify(devlink, "Flashing done",
						   params->component, 0, 0);
	}

	return 0;
}

static struct nsim_trap_item *
nsim_dev_trap_item_lookup(struct nsim_dev *nsim_dev, u16 trap_id)
{
	struct nsim_trap_data *nsim_trap_data = nsim_dev->trap_data;
	int i;

	for (i = 0; i < ARRAY_SIZE(nsim_traps_arr); i++) {
		if (nsim_traps_arr[i].id == trap_id)
			return &nsim_trap_data->trap_items_arr[i];
	}

	return NULL;
}

static int nsim_dev_devlink_trap_init(struct devlink *devlink,
				      const struct devlink_trap *trap,
				      void *trap_ctx)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);
	struct nsim_trap_item *nsim_trap_item;

	nsim_trap_item = nsim_dev_trap_item_lookup(nsim_dev, trap->id);
	if (WARN_ON(!nsim_trap_item))
		return -ENOENT;

	nsim_trap_item->trap_ctx = trap_ctx;
	nsim_trap_item->action = trap->init_action;

	return 0;
}

static int
nsim_dev_devlink_trap_action_set(struct devlink *devlink,
				 const struct devlink_trap *trap,
				 enum devlink_trap_action action,
				 struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);
	struct nsim_trap_item *nsim_trap_item;

	nsim_trap_item = nsim_dev_trap_item_lookup(nsim_dev, trap->id);
	if (WARN_ON(!nsim_trap_item))
		return -ENOENT;

	spin_lock(&nsim_dev->trap_data->trap_lock);
	nsim_trap_item->action = action;
	spin_unlock(&nsim_dev->trap_data->trap_lock);

	return 0;
}

static int
nsim_dev_devlink_trap_group_set(struct devlink *devlink,
				const struct devlink_trap_group *group,
				const struct devlink_trap_policer *policer,
				struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);

	if (nsim_dev->fail_trap_group_set)
		return -EINVAL;

	return 0;
}

static int
nsim_dev_devlink_trap_policer_set(struct devlink *devlink,
				  const struct devlink_trap_policer *policer,
				  u64 rate, u64 burst,
				  struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);

	if (nsim_dev->fail_trap_policer_set) {
		NL_SET_ERR_MSG_MOD(extack, "User setup the operation to fail for testing purposes");
		return -EINVAL;
	}

	return 0;
}

static int
nsim_dev_devlink_trap_policer_counter_get(struct devlink *devlink,
					  const struct devlink_trap_policer *policer,
					  u64 *p_drops)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);
	u64 *cnt;

	if (nsim_dev->fail_trap_policer_counter_get)
		return -EINVAL;

	cnt = &nsim_dev->trap_data->trap_policers_cnt_arr[policer->id - 1];
	*p_drops = (*cnt)++;

	return 0;
}

#define NSIM_LINK_SPEED_MAX     5000 /* Mbps */
#define NSIM_LINK_SPEED_UNIT    125000 /* 1 Mbps given in bytes/sec to avoid
					* u64 overflow during conversion from
					* bytes to bits.
					*/

static int nsim_rate_bytes_to_units(char *name, u64 *rate, struct netlink_ext_ack *extack)
{
	u64 val;
	u32 rem;

	val = div_u64_rem(*rate, NSIM_LINK_SPEED_UNIT, &rem);
	if (rem) {
		pr_err("%s rate value %lluBps not in link speed units of 1Mbps.\n",
		       name, *rate);
		NL_SET_ERR_MSG_MOD(extack, "TX rate value not in link speed units of 1Mbps.");
		return -EINVAL;
	}

	if (val > NSIM_LINK_SPEED_MAX) {
		pr_err("%s rate value %lluMbps exceed link maximum speed 5000Mbps.\n",
		       name, val);
		NL_SET_ERR_MSG_MOD(extack, "TX rate value exceed link maximum speed 5000Mbps.");
		return -EINVAL;
	}
	*rate = val;
	return 0;
}

static int nsim_leaf_tx_share_set(struct devlink_rate *devlink_rate, void *priv,
				  u64 tx_share, struct netlink_ext_ack *extack)
{
	struct nsim_dev_port *nsim_dev_port = priv;
	struct nsim_dev *nsim_dev = nsim_dev_port->ns->nsim_dev;
	int vf_id = nsim_dev_port_index_to_vf_index(nsim_dev_port->port_index);
	int err;

	err = nsim_rate_bytes_to_units("tx_share", &tx_share, extack);
	if (err)
		return err;

	nsim_dev->vfconfigs[vf_id].min_tx_rate = tx_share;
	return 0;
}

static int nsim_leaf_tx_max_set(struct devlink_rate *devlink_rate, void *priv,
				u64 tx_max, struct netlink_ext_ack *extack)
{
	struct nsim_dev_port *nsim_dev_port = priv;
	struct nsim_dev *nsim_dev = nsim_dev_port->ns->nsim_dev;
	int vf_id = nsim_dev_port_index_to_vf_index(nsim_dev_port->port_index);
	int err;

	err = nsim_rate_bytes_to_units("tx_max", &tx_max, extack);
	if (err)
		return err;

	nsim_dev->vfconfigs[vf_id].max_tx_rate = tx_max;
	return 0;
}

struct nsim_rate_node {
	struct dentry *ddir;
	struct dentry *rate_parent;
	char *parent_name;
	u16 tx_share;
	u16 tx_max;
};

static int nsim_node_tx_share_set(struct devlink_rate *devlink_rate, void *priv,
				  u64 tx_share, struct netlink_ext_ack *extack)
{
	struct nsim_rate_node *nsim_node = priv;
	int err;

	err = nsim_rate_bytes_to_units("tx_share", &tx_share, extack);
	if (err)
		return err;

	nsim_node->tx_share = tx_share;
	return 0;
}

static int nsim_node_tx_max_set(struct devlink_rate *devlink_rate, void *priv,
				u64 tx_max, struct netlink_ext_ack *extack)
{
	struct nsim_rate_node *nsim_node = priv;
	int err;

	err = nsim_rate_bytes_to_units("tx_max", &tx_max, extack);
	if (err)
		return err;

	nsim_node->tx_max = tx_max;
	return 0;
}

static int nsim_rate_node_new(struct devlink_rate *node, void **priv,
			      struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(node->devlink);
	struct nsim_rate_node *nsim_node;

	if (!nsim_esw_mode_is_switchdev(nsim_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Node creation allowed only in switchdev mode.");
		return -EOPNOTSUPP;
	}

	nsim_node = kzalloc(sizeof(*nsim_node), GFP_KERNEL);
	if (!nsim_node)
		return -ENOMEM;

	nsim_node->ddir = debugfs_create_dir(node->name, nsim_dev->nodes_ddir);

	debugfs_create_u16("tx_share", 0400, nsim_node->ddir, &nsim_node->tx_share);
	debugfs_create_u16("tx_max", 0400, nsim_node->ddir, &nsim_node->tx_max);
	nsim_node->rate_parent = debugfs_create_file("rate_parent", 0400,
						     nsim_node->ddir,
						     &nsim_node->parent_name,
						     &nsim_dev_rate_parent_fops);

	*priv = nsim_node;
	return 0;
}

static int nsim_rate_node_del(struct devlink_rate *node, void *priv,
			      struct netlink_ext_ack *extack)
{
	struct nsim_rate_node *nsim_node = priv;

	debugfs_remove(nsim_node->rate_parent);
	debugfs_remove_recursive(nsim_node->ddir);
	kfree(nsim_node);
	return 0;
}

static int nsim_rate_leaf_parent_set(struct devlink_rate *child,
				     struct devlink_rate *parent,
				     void *priv_child, void *priv_parent,
				     struct netlink_ext_ack *extack)
{
	struct nsim_dev_port *nsim_dev_port = priv_child;

	if (parent)
		nsim_dev_port->parent_name = parent->name;
	else
		nsim_dev_port->parent_name = NULL;
	return 0;
}

static int nsim_rate_node_parent_set(struct devlink_rate *child,
				     struct devlink_rate *parent,
				     void *priv_child, void *priv_parent,
				     struct netlink_ext_ack *extack)
{
	struct nsim_rate_node *nsim_node = priv_child;

	if (parent)
		nsim_node->parent_name = parent->name;
	else
		nsim_node->parent_name = NULL;
	return 0;
}

static int
nsim_dev_devlink_trap_drop_counter_get(struct devlink *devlink,
				       const struct devlink_trap *trap,
				       u64 *p_drops)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);
	u64 *cnt;

	if (nsim_dev->fail_trap_drop_counter_get)
		return -EINVAL;

	cnt = &nsim_dev->trap_data->trap_pkt_cnt;
	*p_drops = (*cnt)++;

	return 0;
}

static const struct devlink_ops nsim_dev_devlink_ops = {
	.eswitch_mode_set = nsim_devlink_eswitch_mode_set,
	.eswitch_mode_get = nsim_devlink_eswitch_mode_get,
	.supported_flash_update_params = DEVLINK_SUPPORT_FLASH_UPDATE_OVERWRITE_MASK,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT),
	.reload_down = nsim_dev_reload_down,
	.reload_up = nsim_dev_reload_up,
	.info_get = nsim_dev_info_get,
	.flash_update = nsim_dev_flash_update,
	.trap_init = nsim_dev_devlink_trap_init,
	.trap_action_set = nsim_dev_devlink_trap_action_set,
	.trap_group_set = nsim_dev_devlink_trap_group_set,
	.trap_policer_set = nsim_dev_devlink_trap_policer_set,
	.trap_policer_counter_get = nsim_dev_devlink_trap_policer_counter_get,
	.rate_leaf_tx_share_set = nsim_leaf_tx_share_set,
	.rate_leaf_tx_max_set = nsim_leaf_tx_max_set,
	.rate_node_tx_share_set = nsim_node_tx_share_set,
	.rate_node_tx_max_set = nsim_node_tx_max_set,
	.rate_node_new = nsim_rate_node_new,
	.rate_node_del = nsim_rate_node_del,
	.rate_leaf_parent_set = nsim_rate_leaf_parent_set,
	.rate_node_parent_set = nsim_rate_node_parent_set,
	.trap_drop_counter_get = nsim_dev_devlink_trap_drop_counter_get,
};

#define NSIM_DEV_MAX_MACS_DEFAULT 32
#define NSIM_DEV_TEST1_DEFAULT true

static int __nsim_dev_port_add(struct nsim_dev *nsim_dev, enum nsim_dev_port_type type,
			       unsigned int port_index)
{
	struct devlink_port_attrs attrs = {};
	struct nsim_dev_port *nsim_dev_port;
	struct devlink_port *devlink_port;
	int err;

	if (type == NSIM_DEV_PORT_TYPE_VF && !nsim_dev_get_vfs(nsim_dev))
		return -EINVAL;

	nsim_dev_port = kzalloc(sizeof(*nsim_dev_port), GFP_KERNEL);
	if (!nsim_dev_port)
		return -ENOMEM;
	nsim_dev_port->port_index = nsim_dev_port_index(type, port_index);
	nsim_dev_port->port_type = type;

	devlink_port = &nsim_dev_port->devlink_port;
	if (nsim_dev_port_is_pf(nsim_dev_port)) {
		attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
		attrs.phys.port_number = port_index + 1;
	} else {
		attrs.flavour = DEVLINK_PORT_FLAVOUR_PCI_VF;
		attrs.pci_vf.pf = 0;
		attrs.pci_vf.vf = port_index;
	}
	memcpy(attrs.switch_id.id, nsim_dev->switch_id.id, nsim_dev->switch_id.id_len);
	attrs.switch_id.id_len = nsim_dev->switch_id.id_len;
	devlink_port_attrs_set(devlink_port, &attrs);
	err = devl_port_register(priv_to_devlink(nsim_dev), devlink_port,
				 nsim_dev_port->port_index);
	if (err)
		goto err_port_free;

	err = nsim_dev_port_debugfs_init(nsim_dev, nsim_dev_port);
	if (err)
		goto err_dl_port_unregister;

	nsim_dev_port->ns = nsim_create(nsim_dev, nsim_dev_port);
	if (IS_ERR(nsim_dev_port->ns)) {
		err = PTR_ERR(nsim_dev_port->ns);
		goto err_port_debugfs_exit;
	}

	if (nsim_dev_port_is_vf(nsim_dev_port)) {
		err = devl_rate_leaf_create(&nsim_dev_port->devlink_port,
					    nsim_dev_port);
		if (err)
			goto err_nsim_destroy;
	}

	devlink_port_type_eth_set(devlink_port, nsim_dev_port->ns->netdev);
	list_add(&nsim_dev_port->list, &nsim_dev->port_list);

	return 0;

err_nsim_destroy:
	nsim_destroy(nsim_dev_port->ns);
err_port_debugfs_exit:
	nsim_dev_port_debugfs_exit(nsim_dev_port);
err_dl_port_unregister:
	devl_port_unregister(devlink_port);
err_port_free:
	kfree(nsim_dev_port);
	return err;
}

static void __nsim_dev_port_del(struct nsim_dev_port *nsim_dev_port)
{
	struct devlink_port *devlink_port = &nsim_dev_port->devlink_port;

	list_del(&nsim_dev_port->list);
	if (nsim_dev_port_is_vf(nsim_dev_port))
		devl_rate_leaf_destroy(&nsim_dev_port->devlink_port);
	devlink_port_type_clear(devlink_port);
	nsim_destroy(nsim_dev_port->ns);
	nsim_dev_port_debugfs_exit(nsim_dev_port);
	devl_port_unregister(devlink_port);
	kfree(nsim_dev_port);
}

static void nsim_dev_port_del_all(struct nsim_dev *nsim_dev)
{
	struct nsim_dev_port *nsim_dev_port, *tmp;

	list_for_each_entry_safe(nsim_dev_port, tmp,
				 &nsim_dev->port_list, list)
		__nsim_dev_port_del(nsim_dev_port);
}

static int nsim_dev_port_add_all(struct nsim_dev *nsim_dev,
				 unsigned int port_count)
{
	int i, err;

	for (i = 0; i < port_count; i++) {
		err = __nsim_dev_port_add(nsim_dev, NSIM_DEV_PORT_TYPE_PF, i);
		if (err)
			goto err_port_del_all;
	}
	return 0;

err_port_del_all:
	nsim_dev_port_del_all(nsim_dev);
	return err;
}

static int nsim_dev_reload_create(struct nsim_dev *nsim_dev,
				  struct netlink_ext_ack *extack)
{
	struct nsim_bus_dev *nsim_bus_dev = nsim_dev->nsim_bus_dev;
	struct devlink *devlink;
	int err;

	devlink = priv_to_devlink(nsim_dev);
	nsim_dev = devlink_priv(devlink);
	INIT_LIST_HEAD(&nsim_dev->port_list);
	nsim_dev->fw_update_status = true;
	nsim_dev->fw_update_overwrite_mask = 0;

	nsim_devlink_param_load_driverinit_values(devlink);

	err = nsim_dev_dummy_region_init(nsim_dev, devlink);
	if (err)
		return err;

	err = nsim_dev_traps_init(devlink);
	if (err)
		goto err_dummy_region_exit;

	nsim_dev->fib_data = nsim_fib_create(devlink, extack);
	if (IS_ERR(nsim_dev->fib_data)) {
		err = PTR_ERR(nsim_dev->fib_data);
		goto err_traps_exit;
	}

	err = nsim_dev_health_init(nsim_dev, devlink);
	if (err)
		goto err_fib_destroy;

	err = nsim_dev_psample_init(nsim_dev);
	if (err)
		goto err_health_exit;

	err = nsim_dev_hwstats_init(nsim_dev);
	if (err)
		goto err_psample_exit;

	err = nsim_dev_port_add_all(nsim_dev, nsim_bus_dev->port_count);
	if (err)
		goto err_hwstats_exit;

	nsim_dev->take_snapshot = debugfs_create_file("take_snapshot",
						      0200,
						      nsim_dev->ddir,
						      nsim_dev,
						&nsim_dev_take_snapshot_fops);
	return 0;

err_hwstats_exit:
	nsim_dev_hwstats_exit(nsim_dev);
err_psample_exit:
	nsim_dev_psample_exit(nsim_dev);
err_health_exit:
	nsim_dev_health_exit(nsim_dev);
err_fib_destroy:
	nsim_fib_destroy(devlink, nsim_dev->fib_data);
err_traps_exit:
	nsim_dev_traps_exit(devlink);
err_dummy_region_exit:
	nsim_dev_dummy_region_exit(nsim_dev);
	return err;
}

int nsim_drv_probe(struct nsim_bus_dev *nsim_bus_dev)
{
	struct nsim_dev *nsim_dev;
	struct devlink *devlink;
	int err;

	devlink = devlink_alloc_ns(&nsim_dev_devlink_ops, sizeof(*nsim_dev),
				 nsim_bus_dev->initial_net, &nsim_bus_dev->dev);
	if (!devlink)
		return -ENOMEM;
	devl_lock(devlink);
	nsim_dev = devlink_priv(devlink);
	nsim_dev->nsim_bus_dev = nsim_bus_dev;
	nsim_dev->switch_id.id_len = sizeof(nsim_dev->switch_id.id);
	get_random_bytes(nsim_dev->switch_id.id, nsim_dev->switch_id.id_len);
	INIT_LIST_HEAD(&nsim_dev->port_list);
	nsim_dev->fw_update_status = true;
	nsim_dev->fw_update_overwrite_mask = 0;
	nsim_dev->max_macs = NSIM_DEV_MAX_MACS_DEFAULT;
	nsim_dev->test1 = NSIM_DEV_TEST1_DEFAULT;
	spin_lock_init(&nsim_dev->fa_cookie_lock);

	dev_set_drvdata(&nsim_bus_dev->dev, nsim_dev);

	nsim_dev->vfconfigs = kcalloc(nsim_bus_dev->max_vfs,
				      sizeof(struct nsim_vf_config),
				      GFP_KERNEL | __GFP_NOWARN);
	if (!nsim_dev->vfconfigs) {
		err = -ENOMEM;
		goto err_devlink_unlock;
	}

	err = nsim_dev_resources_register(devlink);
	if (err)
		goto err_vfc_free;

	err = devlink_params_register(devlink, nsim_devlink_params,
				      ARRAY_SIZE(nsim_devlink_params));
	if (err)
		goto err_dl_unregister;
	nsim_devlink_set_params_init_values(nsim_dev, devlink);

	err = nsim_dev_dummy_region_init(nsim_dev, devlink);
	if (err)
		goto err_params_unregister;

	err = nsim_dev_traps_init(devlink);
	if (err)
		goto err_dummy_region_exit;

	err = nsim_dev_debugfs_init(nsim_dev);
	if (err)
		goto err_traps_exit;

	nsim_dev->fib_data = nsim_fib_create(devlink, NULL);
	if (IS_ERR(nsim_dev->fib_data)) {
		err = PTR_ERR(nsim_dev->fib_data);
		goto err_debugfs_exit;
	}

	err = nsim_dev_health_init(nsim_dev, devlink);
	if (err)
		goto err_fib_destroy;

	err = nsim_bpf_dev_init(nsim_dev);
	if (err)
		goto err_health_exit;

	err = nsim_dev_psample_init(nsim_dev);
	if (err)
		goto err_bpf_dev_exit;

	err = nsim_dev_hwstats_init(nsim_dev);
	if (err)
		goto err_psample_exit;

	err = nsim_dev_port_add_all(nsim_dev, nsim_bus_dev->port_count);
	if (err)
		goto err_hwstats_exit;

	nsim_dev->esw_mode = DEVLINK_ESWITCH_MODE_LEGACY;
	devlink_set_features(devlink, DEVLINK_F_RELOAD);
	devl_unlock(devlink);
	devlink_register(devlink);
	return 0;

err_hwstats_exit:
	nsim_dev_hwstats_exit(nsim_dev);
err_psample_exit:
	nsim_dev_psample_exit(nsim_dev);
err_bpf_dev_exit:
	nsim_bpf_dev_exit(nsim_dev);
err_health_exit:
	nsim_dev_health_exit(nsim_dev);
err_fib_destroy:
	nsim_fib_destroy(devlink, nsim_dev->fib_data);
err_debugfs_exit:
	nsim_dev_debugfs_exit(nsim_dev);
err_traps_exit:
	nsim_dev_traps_exit(devlink);
err_dummy_region_exit:
	nsim_dev_dummy_region_exit(nsim_dev);
err_params_unregister:
	devlink_params_unregister(devlink, nsim_devlink_params,
				  ARRAY_SIZE(nsim_devlink_params));
err_dl_unregister:
	devl_resources_unregister(devlink);
err_vfc_free:
	kfree(nsim_dev->vfconfigs);
err_devlink_unlock:
	devl_unlock(devlink);
	devlink_free(devlink);
	dev_set_drvdata(&nsim_bus_dev->dev, NULL);
	return err;
}

static void nsim_dev_reload_destroy(struct nsim_dev *nsim_dev)
{
	struct devlink *devlink = priv_to_devlink(nsim_dev);

	if (devlink_is_reload_failed(devlink))
		return;
	debugfs_remove(nsim_dev->take_snapshot);

	if (nsim_dev_get_vfs(nsim_dev)) {
		nsim_bus_dev_set_vfs(nsim_dev->nsim_bus_dev, 0);
		if (nsim_esw_mode_is_switchdev(nsim_dev))
			nsim_esw_legacy_enable(nsim_dev, NULL);
	}

	nsim_dev_port_del_all(nsim_dev);
	nsim_dev_hwstats_exit(nsim_dev);
	nsim_dev_psample_exit(nsim_dev);
	nsim_dev_health_exit(nsim_dev);
	nsim_fib_destroy(devlink, nsim_dev->fib_data);
	nsim_dev_traps_exit(devlink);
	nsim_dev_dummy_region_exit(nsim_dev);
}

void nsim_drv_remove(struct nsim_bus_dev *nsim_bus_dev)
{
	struct nsim_dev *nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	struct devlink *devlink = priv_to_devlink(nsim_dev);

	devlink_unregister(devlink);
	devl_lock(devlink);
	nsim_dev_reload_destroy(nsim_dev);

	nsim_bpf_dev_exit(nsim_dev);
	nsim_dev_debugfs_exit(nsim_dev);
	devlink_params_unregister(devlink, nsim_devlink_params,
				  ARRAY_SIZE(nsim_devlink_params));
	devl_resources_unregister(devlink);
	kfree(nsim_dev->vfconfigs);
	kfree(nsim_dev->fa_cookie);
	devl_unlock(devlink);
	devlink_free(devlink);
	dev_set_drvdata(&nsim_bus_dev->dev, NULL);
}

static struct nsim_dev_port *
__nsim_dev_port_lookup(struct nsim_dev *nsim_dev, enum nsim_dev_port_type type,
		       unsigned int port_index)
{
	struct nsim_dev_port *nsim_dev_port;

	port_index = nsim_dev_port_index(type, port_index);
	list_for_each_entry(nsim_dev_port, &nsim_dev->port_list, list)
		if (nsim_dev_port->port_index == port_index)
			return nsim_dev_port;
	return NULL;
}

int nsim_drv_port_add(struct nsim_bus_dev *nsim_bus_dev, enum nsim_dev_port_type type,
		      unsigned int port_index)
{
	struct nsim_dev *nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	int err;

	devl_lock(priv_to_devlink(nsim_dev));
	if (__nsim_dev_port_lookup(nsim_dev, type, port_index))
		err = -EEXIST;
	else
		err = __nsim_dev_port_add(nsim_dev, type, port_index);
	devl_unlock(priv_to_devlink(nsim_dev));
	return err;
}

int nsim_drv_port_del(struct nsim_bus_dev *nsim_bus_dev, enum nsim_dev_port_type type,
		      unsigned int port_index)
{
	struct nsim_dev *nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	struct nsim_dev_port *nsim_dev_port;
	int err = 0;

	devl_lock(priv_to_devlink(nsim_dev));
	nsim_dev_port = __nsim_dev_port_lookup(nsim_dev, type, port_index);
	if (!nsim_dev_port)
		err = -ENOENT;
	else
		__nsim_dev_port_del(nsim_dev_port);
	devl_unlock(priv_to_devlink(nsim_dev));
	return err;
}

int nsim_drv_configure_vfs(struct nsim_bus_dev *nsim_bus_dev,
			   unsigned int num_vfs)
{
	struct nsim_dev *nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	struct devlink *devlink = priv_to_devlink(nsim_dev);
	int ret = 0;

	devl_lock(devlink);
	if (nsim_bus_dev->num_vfs == num_vfs)
		goto exit_unlock;
	if (nsim_bus_dev->num_vfs && num_vfs) {
		ret = -EBUSY;
		goto exit_unlock;
	}
	if (nsim_bus_dev->max_vfs < num_vfs) {
		ret = -ENOMEM;
		goto exit_unlock;
	}

	nsim_bus_dev_set_vfs(nsim_bus_dev, num_vfs);
	if (nsim_esw_mode_is_switchdev(nsim_dev)) {
		if (num_vfs) {
			ret = nsim_esw_switchdev_enable(nsim_dev, NULL);
			if (ret) {
				nsim_bus_dev_set_vfs(nsim_bus_dev, 0);
				goto exit_unlock;
			}
		} else {
			nsim_esw_legacy_enable(nsim_dev, NULL);
		}
	}

exit_unlock:
	devl_unlock(devlink);

	return ret;
}

int nsim_dev_init(void)
{
	nsim_dev_ddir = debugfs_create_dir(DRV_NAME, NULL);
	return PTR_ERR_OR_ZERO(nsim_dev_ddir);
}

void nsim_dev_exit(void)
{
	debugfs_remove_recursive(nsim_dev_ddir);
}
