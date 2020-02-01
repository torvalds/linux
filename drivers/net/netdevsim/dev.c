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
#include <uapi/linux/devlink.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/udp.h>

#include "netdevsim.h"

static struct dentry *nsim_dev_ddir;

#define NSIM_DEV_DUMMY_REGION_SIZE (1024 * 32)

static ssize_t nsim_dev_take_snapshot_write(struct file *file,
					    const char __user *data,
					    size_t count, loff_t *ppos)
{
	struct nsim_dev *nsim_dev = file->private_data;
	void *dummy_data;
	int err;
	u32 id;

	dummy_data = kmalloc(NSIM_DEV_DUMMY_REGION_SIZE, GFP_KERNEL);
	if (!dummy_data)
		return -ENOMEM;

	get_random_bytes(dummy_data, NSIM_DEV_DUMMY_REGION_SIZE);

	id = devlink_region_snapshot_id_get(priv_to_devlink(nsim_dev));
	err = devlink_region_snapshot_create(nsim_dev->dummy_region,
					     dummy_data, id, kfree);
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
};

static int nsim_dev_debugfs_init(struct nsim_dev *nsim_dev)
{
	char dev_ddir_name[sizeof(DRV_NAME) + 10];

	sprintf(dev_ddir_name, DRV_NAME "%u", nsim_dev->nsim_bus_dev->dev.id);
	nsim_dev->ddir = debugfs_create_dir(dev_ddir_name, nsim_dev_ddir);
	if (IS_ERR(nsim_dev->ddir))
		return PTR_ERR(nsim_dev->ddir);
	nsim_dev->ports_ddir = debugfs_create_dir("ports", nsim_dev->ddir);
	if (IS_ERR(nsim_dev->ports_ddir))
		return PTR_ERR(nsim_dev->ports_ddir);
	debugfs_create_bool("fw_update_status", 0600, nsim_dev->ddir,
			    &nsim_dev->fw_update_status);
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
	return 0;
}

static void nsim_dev_debugfs_exit(struct nsim_dev *nsim_dev)
{
	debugfs_remove_recursive(nsim_dev->ports_ddir);
	debugfs_remove_recursive(nsim_dev->ddir);
}

static int nsim_dev_port_debugfs_init(struct nsim_dev *nsim_dev,
				      struct nsim_dev_port *nsim_dev_port)
{
	char port_ddir_name[16];
	char dev_link_name[32];

	sprintf(port_ddir_name, "%u", nsim_dev_port->port_index);
	nsim_dev_port->ddir = debugfs_create_dir(port_ddir_name,
						 nsim_dev->ports_ddir);
	if (IS_ERR(nsim_dev_port->ddir))
		return PTR_ERR(nsim_dev_port->ddir);

	sprintf(dev_link_name, "../../../" DRV_NAME "%u",
		nsim_dev->nsim_bus_dev->dev.id);
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
	err = devlink_resource_register(devlink, "IPv4", (u64)-1,
					NSIM_RESOURCE_IPV4,
					DEVLINK_RESOURCE_ID_PARENT_TOP,
					&params);
	if (err) {
		pr_err("Failed to register IPv4 top resource\n");
		goto out;
	}

	err = devlink_resource_register(devlink, "fib", (u64)-1,
					NSIM_RESOURCE_IPV4_FIB,
					NSIM_RESOURCE_IPV4, &params);
	if (err) {
		pr_err("Failed to register IPv4 FIB resource\n");
		return err;
	}

	err = devlink_resource_register(devlink, "fib-rules", (u64)-1,
					NSIM_RESOURCE_IPV4_FIB_RULES,
					NSIM_RESOURCE_IPV4, &params);
	if (err) {
		pr_err("Failed to register IPv4 FIB rules resource\n");
		return err;
	}

	/* Resources for IPv6 */
	err = devlink_resource_register(devlink, "IPv6", (u64)-1,
					NSIM_RESOURCE_IPV6,
					DEVLINK_RESOURCE_ID_PARENT_TOP,
					&params);
	if (err) {
		pr_err("Failed to register IPv6 top resource\n");
		goto out;
	}

	err = devlink_resource_register(devlink, "fib", (u64)-1,
					NSIM_RESOURCE_IPV6_FIB,
					NSIM_RESOURCE_IPV6, &params);
	if (err) {
		pr_err("Failed to register IPv6 FIB resource\n");
		return err;
	}

	err = devlink_resource_register(devlink, "fib-rules", (u64)-1,
					NSIM_RESOURCE_IPV6_FIB_RULES,
					NSIM_RESOURCE_IPV6, &params);
	if (err) {
		pr_err("Failed to register IPv6 FIB rules resource\n");
		return err;
	}

out:
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

static int nsim_dev_dummy_region_init(struct nsim_dev *nsim_dev,
				      struct devlink *devlink)
{
	nsim_dev->dummy_region =
		devlink_region_create(devlink, "dummy",
				      NSIM_DEV_DUMMY_REGION_SNAPSHOT_MAX,
				      NSIM_DEV_DUMMY_REGION_SIZE);
	return PTR_ERR_OR_ZERO(nsim_dev->dummy_region);
}

static void nsim_dev_dummy_region_exit(struct nsim_dev *nsim_dev)
{
	devlink_region_destroy(nsim_dev->dummy_region);
}

struct nsim_trap_item {
	void *trap_ctx;
	enum devlink_trap_action action;
};

struct nsim_trap_data {
	struct delayed_work trap_report_dw;
	struct nsim_trap_item *trap_items_arr;
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
			     DEVLINK_TRAP_GROUP_GENERIC(_group_id),	      \
			     NSIM_TRAP_METADATA)
#define NSIM_TRAP_EXCEPTION(_id, _group_id)				      \
	DEVLINK_TRAP_GENERIC(EXCEPTION, TRAP, _id,			      \
			     DEVLINK_TRAP_GROUP_GENERIC(_group_id),	      \
			     NSIM_TRAP_METADATA)
#define NSIM_TRAP_DRIVER_EXCEPTION(_id, _group_id)			      \
	DEVLINK_TRAP_DRIVER(EXCEPTION, TRAP, NSIM_TRAP_ID_##_id,	      \
			    NSIM_TRAP_NAME_##_id,			      \
			    DEVLINK_TRAP_GROUP_GENERIC(_group_id),	      \
			    NSIM_TRAP_METADATA)

static const struct devlink_trap nsim_traps_arr[] = {
	NSIM_TRAP_DROP(SMAC_MC, L2_DROPS),
	NSIM_TRAP_DROP(VLAN_TAG_MISMATCH, L2_DROPS),
	NSIM_TRAP_DROP(INGRESS_VLAN_FILTER, L2_DROPS),
	NSIM_TRAP_DROP(INGRESS_STP_FILTER, L2_DROPS),
	NSIM_TRAP_DROP(EMPTY_TX_LIST, L2_DROPS),
	NSIM_TRAP_DROP(PORT_LOOPBACK_FILTER, L2_DROPS),
	NSIM_TRAP_DRIVER_EXCEPTION(FID_MISS, L2_DROPS),
	NSIM_TRAP_DROP(BLACKHOLE_ROUTE, L3_DROPS),
	NSIM_TRAP_EXCEPTION(TTL_ERROR, L3_DROPS),
	NSIM_TRAP_DROP(TAIL_DROP, BUFFER_DROPS),
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
		struct nsim_trap_item *nsim_trap_item;
		struct sk_buff *skb;

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
		local_bh_disable();
		devlink_trap_report(devlink, skb, nsim_trap_item->trap_ctx,
				    &nsim_dev_port->devlink_port);
		local_bh_enable();
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

	/* For each running port and enabled packet trap, generate a UDP
	 * packet with a random 5-tuple and report it.
	 */
	mutex_lock(&nsim_dev->port_list_lock);
	list_for_each_entry(nsim_dev_port, &nsim_dev->port_list, list) {
		if (!netif_running(nsim_dev_port->ns->netdev))
			continue;

		nsim_dev_trap_report(nsim_dev_port);
	}
	mutex_unlock(&nsim_dev->port_list_lock);

	schedule_delayed_work(&nsim_dev->trap_data->trap_report_dw,
			      msecs_to_jiffies(NSIM_TRAP_REPORT_INTERVAL_MS));
}

static int nsim_dev_traps_init(struct devlink *devlink)
{
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

	/* The lock is used to protect the action state of the registered
	 * traps. The value is written by user and read in delayed work when
	 * iterating over all the traps.
	 */
	spin_lock_init(&nsim_trap_data->trap_lock);
	nsim_trap_data->nsim_dev = nsim_dev;
	nsim_dev->trap_data = nsim_trap_data;

	err = devlink_traps_register(devlink, nsim_traps_arr,
				     ARRAY_SIZE(nsim_traps_arr), NULL);
	if (err)
		goto err_trap_items_free;

	INIT_DELAYED_WORK(&nsim_dev->trap_data->trap_report_dw,
			  nsim_dev_trap_report_work);
	schedule_delayed_work(&nsim_dev->trap_data->trap_report_dw,
			      msecs_to_jiffies(NSIM_TRAP_REPORT_INTERVAL_MS));

	return 0;

err_trap_items_free:
	kfree(nsim_trap_data->trap_items_arr);
err_trap_data_free:
	kfree(nsim_trap_data);
	return err;
}

static void nsim_dev_traps_exit(struct devlink *devlink)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);

	cancel_delayed_work_sync(&nsim_dev->trap_data->trap_report_dw);
	devlink_traps_unregister(devlink, nsim_traps_arr,
				 ARRAY_SIZE(nsim_traps_arr));
	kfree(nsim_dev->trap_data->trap_items_arr);
	kfree(nsim_dev->trap_data);
}

static int nsim_dev_reload_create(struct nsim_dev *nsim_dev,
				  struct netlink_ext_ack *extack);
static void nsim_dev_reload_destroy(struct nsim_dev *nsim_dev);

static int nsim_dev_reload_down(struct devlink *devlink, bool netns_change,
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

static int nsim_dev_reload_up(struct devlink *devlink,
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

	return nsim_dev_reload_create(nsim_dev, extack);
}

static int nsim_dev_info_get(struct devlink *devlink,
			     struct devlink_info_req *req,
			     struct netlink_ext_ack *extack)
{
	return devlink_info_driver_name_put(req, DRV_NAME);
}

#define NSIM_DEV_FLASH_SIZE 500000
#define NSIM_DEV_FLASH_CHUNK_SIZE 1000
#define NSIM_DEV_FLASH_CHUNK_TIME_MS 10

static int nsim_dev_flash_update(struct devlink *devlink, const char *file_name,
				 const char *component,
				 struct netlink_ext_ack *extack)
{
	struct nsim_dev *nsim_dev = devlink_priv(devlink);
	int i;

	if (nsim_dev->fw_update_status) {
		devlink_flash_update_begin_notify(devlink);
		devlink_flash_update_status_notify(devlink,
						   "Preparing to flash",
						   component, 0, 0);
	}

	for (i = 0; i < NSIM_DEV_FLASH_SIZE / NSIM_DEV_FLASH_CHUNK_SIZE; i++) {
		if (nsim_dev->fw_update_status)
			devlink_flash_update_status_notify(devlink, "Flashing",
							   component,
							   i * NSIM_DEV_FLASH_CHUNK_SIZE,
							   NSIM_DEV_FLASH_SIZE);
		msleep(NSIM_DEV_FLASH_CHUNK_TIME_MS);
	}

	if (nsim_dev->fw_update_status) {
		devlink_flash_update_status_notify(devlink, "Flashing",
						   component,
						   NSIM_DEV_FLASH_SIZE,
						   NSIM_DEV_FLASH_SIZE);
		devlink_flash_update_status_notify(devlink, "Flashing done",
						   component, 0, 0);
		devlink_flash_update_end_notify(devlink);
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
				 enum devlink_trap_action action)
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

static const struct devlink_ops nsim_dev_devlink_ops = {
	.reload_down = nsim_dev_reload_down,
	.reload_up = nsim_dev_reload_up,
	.info_get = nsim_dev_info_get,
	.flash_update = nsim_dev_flash_update,
	.trap_init = nsim_dev_devlink_trap_init,
	.trap_action_set = nsim_dev_devlink_trap_action_set,
};

#define NSIM_DEV_MAX_MACS_DEFAULT 32
#define NSIM_DEV_TEST1_DEFAULT true

static int __nsim_dev_port_add(struct nsim_dev *nsim_dev,
			       unsigned int port_index)
{
	struct nsim_dev_port *nsim_dev_port;
	struct devlink_port *devlink_port;
	int err;

	nsim_dev_port = kzalloc(sizeof(*nsim_dev_port), GFP_KERNEL);
	if (!nsim_dev_port)
		return -ENOMEM;
	nsim_dev_port->port_index = port_index;

	devlink_port = &nsim_dev_port->devlink_port;
	devlink_port_attrs_set(devlink_port, DEVLINK_PORT_FLAVOUR_PHYSICAL,
			       port_index + 1, 0, 0,
			       nsim_dev->switch_id.id,
			       nsim_dev->switch_id.id_len);
	err = devlink_port_register(priv_to_devlink(nsim_dev), devlink_port,
				    port_index);
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

	devlink_port_type_eth_set(devlink_port, nsim_dev_port->ns->netdev);
	list_add(&nsim_dev_port->list, &nsim_dev->port_list);

	return 0;

err_port_debugfs_exit:
	nsim_dev_port_debugfs_exit(nsim_dev_port);
err_dl_port_unregister:
	devlink_port_unregister(devlink_port);
err_port_free:
	kfree(nsim_dev_port);
	return err;
}

static void __nsim_dev_port_del(struct nsim_dev_port *nsim_dev_port)
{
	struct devlink_port *devlink_port = &nsim_dev_port->devlink_port;

	list_del(&nsim_dev_port->list);
	devlink_port_type_clear(devlink_port);
	nsim_destroy(nsim_dev_port->ns);
	nsim_dev_port_debugfs_exit(nsim_dev_port);
	devlink_port_unregister(devlink_port);
	kfree(nsim_dev_port);
}

static void nsim_dev_port_del_all(struct nsim_dev *nsim_dev)
{
	struct nsim_dev_port *nsim_dev_port, *tmp;

	mutex_lock(&nsim_dev->port_list_lock);
	list_for_each_entry_safe(nsim_dev_port, tmp,
				 &nsim_dev->port_list, list)
		__nsim_dev_port_del(nsim_dev_port);
	mutex_unlock(&nsim_dev->port_list_lock);
}

static int nsim_dev_port_add_all(struct nsim_dev *nsim_dev,
				 unsigned int port_count)
{
	int i, err;

	for (i = 0; i < port_count; i++) {
		err = __nsim_dev_port_add(nsim_dev, i);
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
	mutex_init(&nsim_dev->port_list_lock);
	nsim_dev->fw_update_status = true;

	nsim_dev->fib_data = nsim_fib_create(devlink, extack);
	if (IS_ERR(nsim_dev->fib_data))
		return PTR_ERR(nsim_dev->fib_data);

	nsim_devlink_param_load_driverinit_values(devlink);

	err = nsim_dev_dummy_region_init(nsim_dev, devlink);
	if (err)
		goto err_fib_destroy;

	err = nsim_dev_traps_init(devlink);
	if (err)
		goto err_dummy_region_exit;

	err = nsim_dev_health_init(nsim_dev, devlink);
	if (err)
		goto err_traps_exit;

	err = nsim_dev_port_add_all(nsim_dev, nsim_bus_dev->port_count);
	if (err)
		goto err_health_exit;

	nsim_dev->take_snapshot = debugfs_create_file("take_snapshot",
						      0200,
						      nsim_dev->ddir,
						      nsim_dev,
						&nsim_dev_take_snapshot_fops);
	return 0;

err_health_exit:
	nsim_dev_health_exit(nsim_dev);
err_traps_exit:
	nsim_dev_traps_exit(devlink);
err_dummy_region_exit:
	nsim_dev_dummy_region_exit(nsim_dev);
err_fib_destroy:
	nsim_fib_destroy(devlink, nsim_dev->fib_data);
	return err;
}

int nsim_dev_probe(struct nsim_bus_dev *nsim_bus_dev)
{
	struct nsim_dev *nsim_dev;
	struct devlink *devlink;
	int err;

	devlink = devlink_alloc(&nsim_dev_devlink_ops, sizeof(*nsim_dev));
	if (!devlink)
		return -ENOMEM;
	devlink_net_set(devlink, nsim_bus_dev->initial_net);
	nsim_dev = devlink_priv(devlink);
	nsim_dev->nsim_bus_dev = nsim_bus_dev;
	nsim_dev->switch_id.id_len = sizeof(nsim_dev->switch_id.id);
	get_random_bytes(nsim_dev->switch_id.id, nsim_dev->switch_id.id_len);
	INIT_LIST_HEAD(&nsim_dev->port_list);
	mutex_init(&nsim_dev->port_list_lock);
	nsim_dev->fw_update_status = true;
	nsim_dev->max_macs = NSIM_DEV_MAX_MACS_DEFAULT;
	nsim_dev->test1 = NSIM_DEV_TEST1_DEFAULT;

	dev_set_drvdata(&nsim_bus_dev->dev, nsim_dev);

	err = nsim_dev_resources_register(devlink);
	if (err)
		goto err_devlink_free;

	nsim_dev->fib_data = nsim_fib_create(devlink, NULL);
	if (IS_ERR(nsim_dev->fib_data)) {
		err = PTR_ERR(nsim_dev->fib_data);
		goto err_resources_unregister;
	}

	err = devlink_register(devlink, &nsim_bus_dev->dev);
	if (err)
		goto err_fib_destroy;

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

	err = nsim_dev_health_init(nsim_dev, devlink);
	if (err)
		goto err_debugfs_exit;

	err = nsim_bpf_dev_init(nsim_dev);
	if (err)
		goto err_health_exit;

	err = nsim_dev_port_add_all(nsim_dev, nsim_bus_dev->port_count);
	if (err)
		goto err_bpf_dev_exit;

	devlink_params_publish(devlink);
	devlink_reload_enable(devlink);
	return 0;

err_bpf_dev_exit:
	nsim_bpf_dev_exit(nsim_dev);
err_health_exit:
	nsim_dev_health_exit(nsim_dev);
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
	devlink_unregister(devlink);
err_fib_destroy:
	nsim_fib_destroy(devlink, nsim_dev->fib_data);
err_resources_unregister:
	devlink_resources_unregister(devlink, NULL);
err_devlink_free:
	devlink_free(devlink);
	return err;
}

static void nsim_dev_reload_destroy(struct nsim_dev *nsim_dev)
{
	struct devlink *devlink = priv_to_devlink(nsim_dev);

	if (devlink_is_reload_failed(devlink))
		return;
	debugfs_remove(nsim_dev->take_snapshot);
	nsim_dev_port_del_all(nsim_dev);
	nsim_dev_health_exit(nsim_dev);
	nsim_dev_traps_exit(devlink);
	nsim_dev_dummy_region_exit(nsim_dev);
	mutex_destroy(&nsim_dev->port_list_lock);
	nsim_fib_destroy(devlink, nsim_dev->fib_data);
}

void nsim_dev_remove(struct nsim_bus_dev *nsim_bus_dev)
{
	struct nsim_dev *nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	struct devlink *devlink = priv_to_devlink(nsim_dev);

	devlink_reload_disable(devlink);

	nsim_dev_reload_destroy(nsim_dev);

	nsim_bpf_dev_exit(nsim_dev);
	nsim_dev_debugfs_exit(nsim_dev);
	devlink_params_unregister(devlink, nsim_devlink_params,
				  ARRAY_SIZE(nsim_devlink_params));
	devlink_unregister(devlink);
	devlink_resources_unregister(devlink, NULL);
	devlink_free(devlink);
}

static struct nsim_dev_port *
__nsim_dev_port_lookup(struct nsim_dev *nsim_dev, unsigned int port_index)
{
	struct nsim_dev_port *nsim_dev_port;

	list_for_each_entry(nsim_dev_port, &nsim_dev->port_list, list)
		if (nsim_dev_port->port_index == port_index)
			return nsim_dev_port;
	return NULL;
}

int nsim_dev_port_add(struct nsim_bus_dev *nsim_bus_dev,
		      unsigned int port_index)
{
	struct nsim_dev *nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	int err;

	mutex_lock(&nsim_dev->port_list_lock);
	if (__nsim_dev_port_lookup(nsim_dev, port_index))
		err = -EEXIST;
	else
		err = __nsim_dev_port_add(nsim_dev, port_index);
	mutex_unlock(&nsim_dev->port_list_lock);
	return err;
}

int nsim_dev_port_del(struct nsim_bus_dev *nsim_bus_dev,
		      unsigned int port_index)
{
	struct nsim_dev *nsim_dev = dev_get_drvdata(&nsim_bus_dev->dev);
	struct nsim_dev_port *nsim_dev_port;
	int err = 0;

	mutex_lock(&nsim_dev->port_list_lock);
	nsim_dev_port = __nsim_dev_port_lookup(nsim_dev, port_index);
	if (!nsim_dev_port)
		err = -ENOENT;
	else
		__nsim_dev_port_del(nsim_dev_port);
	mutex_unlock(&nsim_dev->port_list_lock);
	return err;
}

int nsim_dev_init(void)
{
	nsim_dev_ddir = debugfs_create_dir(DRV_NAME, NULL);
	if (IS_ERR(nsim_dev_ddir))
		return PTR_ERR(nsim_dev_ddir);
	return 0;
}

void nsim_dev_exit(void)
{
	debugfs_remove_recursive(nsim_dev_ddir);
}
