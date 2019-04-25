/*
 * Copyright (c) 2018 Cumulus Networks. All rights reserved.
 * Copyright (c) 2018 David Ahern <dsa@cumulusnetworks.com>
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

#include <linux/device.h>
#include <linux/rtnetlink.h>
#include <net/devlink.h>

#include "netdevsim.h"

struct nsim_devlink {
	struct nsim_fib_data *fib_data;
};

/* IPv4
 */
static u64 nsim_ipv4_fib_resource_occ_get(void *priv)
{
	struct nsim_devlink *nsim_devlink = priv;

	return nsim_fib_get_val(nsim_devlink->fib_data,
				NSIM_RESOURCE_IPV4_FIB, false);
}

static u64 nsim_ipv4_fib_rules_res_occ_get(void *priv)
{
	struct nsim_devlink *nsim_devlink = priv;

	return nsim_fib_get_val(nsim_devlink->fib_data,
				NSIM_RESOURCE_IPV4_FIB_RULES, false);
}

/* IPv6
 */
static u64 nsim_ipv6_fib_resource_occ_get(void *priv)
{
	struct nsim_devlink *nsim_devlink = priv;

	return nsim_fib_get_val(nsim_devlink->fib_data,
				NSIM_RESOURCE_IPV6_FIB, false);
}

static u64 nsim_ipv6_fib_rules_res_occ_get(void *priv)
{
	struct nsim_devlink *nsim_devlink = priv;

	return nsim_fib_get_val(nsim_devlink->fib_data,
				NSIM_RESOURCE_IPV6_FIB_RULES, false);
}

static int devlink_resources_register(struct devlink *devlink)
{
	struct nsim_devlink *nsim_devlink = devlink_priv(devlink);
	struct devlink_resource_size_params params = {
		.size_max = (u64)-1,
		.size_granularity = 1,
		.unit = DEVLINK_RESOURCE_UNIT_ENTRY
	};
	int err;
	u64 n;

	/* Resources for IPv4 */
	err = devlink_resource_register(devlink, "IPv4", (u64)-1,
					NSIM_RESOURCE_IPV4,
					DEVLINK_RESOURCE_ID_PARENT_TOP,
					&params);
	if (err) {
		pr_err("Failed to register IPv4 top resource\n");
		goto out;
	}

	n = nsim_fib_get_val(nsim_devlink->fib_data,
			     NSIM_RESOURCE_IPV4_FIB, true);
	err = devlink_resource_register(devlink, "fib", n,
					NSIM_RESOURCE_IPV4_FIB,
					NSIM_RESOURCE_IPV4, &params);
	if (err) {
		pr_err("Failed to register IPv4 FIB resource\n");
		return err;
	}

	n = nsim_fib_get_val(nsim_devlink->fib_data,
			     NSIM_RESOURCE_IPV4_FIB_RULES, true);
	err = devlink_resource_register(devlink, "fib-rules", n,
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

	n = nsim_fib_get_val(nsim_devlink->fib_data,
			     NSIM_RESOURCE_IPV6_FIB, true);
	err = devlink_resource_register(devlink, "fib", n,
					NSIM_RESOURCE_IPV6_FIB,
					NSIM_RESOURCE_IPV6, &params);
	if (err) {
		pr_err("Failed to register IPv6 FIB resource\n");
		return err;
	}

	n = nsim_fib_get_val(nsim_devlink->fib_data,
			     NSIM_RESOURCE_IPV6_FIB_RULES, true);
	err = devlink_resource_register(devlink, "fib-rules", n,
					NSIM_RESOURCE_IPV6_FIB_RULES,
					NSIM_RESOURCE_IPV6, &params);
	if (err) {
		pr_err("Failed to register IPv6 FIB rules resource\n");
		return err;
	}

	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_IPV4_FIB,
					  nsim_ipv4_fib_resource_occ_get,
					  nsim_devlink);
	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_IPV4_FIB_RULES,
					  nsim_ipv4_fib_rules_res_occ_get,
					  nsim_devlink);
	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_IPV6_FIB,
					  nsim_ipv6_fib_resource_occ_get,
					  nsim_devlink);
	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_IPV6_FIB_RULES,
					  nsim_ipv6_fib_rules_res_occ_get,
					  nsim_devlink);
out:
	return err;
}

static int nsim_devlink_reload(struct devlink *devlink,
			       struct netlink_ext_ack *extack)
{
	struct nsim_devlink *nsim_devlink = devlink_priv(devlink);
	enum nsim_resource_id res_ids[] = {
		NSIM_RESOURCE_IPV4_FIB, NSIM_RESOURCE_IPV4_FIB_RULES,
		NSIM_RESOURCE_IPV6_FIB, NSIM_RESOURCE_IPV6_FIB_RULES
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(res_ids); ++i) {
		int err;
		u64 val;

		err = devlink_resource_size_get(devlink, res_ids[i], &val);
		if (!err) {
			err = nsim_fib_set_max(nsim_devlink->fib_data,
					       res_ids[i], val, extack);
			if (err)
				return err;
		}
	}

	return 0;
}

static const struct devlink_ops nsim_devlink_ops = {
	.reload = nsim_devlink_reload,
};

static int __nsim_devlink_init(struct netdevsim *ns)
{
	struct nsim_devlink *nsim_devlink;
	struct devlink *devlink;
	int err;

	devlink = devlink_alloc(&nsim_devlink_ops, sizeof(*nsim_devlink));
	if (!devlink)
		return -ENOMEM;
	nsim_devlink = devlink_priv(devlink);

	nsim_devlink->fib_data = nsim_fib_create();
	if (IS_ERR(nsim_devlink->fib_data)) {
		err = PTR_ERR(nsim_devlink->fib_data);
		goto err_devlink_free;
	}

	err = devlink_resources_register(devlink);
	if (err)
		goto err_fib_destroy;

	err = devlink_register(devlink, &ns->dev);
	if (err)
		goto err_resources_unregister;

	ns->devlink = devlink;

	return 0;

err_resources_unregister:
	devlink_resources_unregister(devlink, NULL);
err_fib_destroy:
	nsim_fib_destroy(nsim_devlink->fib_data);
err_devlink_free:
	devlink_free(devlink);

	return err;
}

int nsim_devlink_init(struct netdevsim *ns)
{
	int err;

	dev_hold(ns->netdev);
	rtnl_unlock();
	err = __nsim_devlink_init(ns);
	rtnl_lock();
	dev_put(ns->netdev);
	return err;
}

void nsim_devlink_exit(struct netdevsim *ns)
{
	struct devlink *devlink = ns->devlink;
	struct nsim_devlink *nsim_devlink = devlink_priv(devlink);

	devlink_unregister(devlink);
	devlink_resources_unregister(devlink, NULL);
	nsim_fib_destroy(nsim_devlink->fib_data);
	devlink_free(devlink);
}
