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
#include <net/devlink.h>
#include <net/netns/generic.h>

#include "netdevsim.h"

static unsigned int nsim_devlink_id;

/* place holder until devlink and namespaces is sorted out */
static struct net *nsim_devlink_net(struct devlink *devlink)
{
	return &init_net;
}

/* IPv4
 */
static u64 nsim_ipv4_fib_resource_occ_get(struct devlink *devlink)
{
	struct net *net = nsim_devlink_net(devlink);

	return nsim_fib_get_val(net, NSIM_RESOURCE_IPV4_FIB, false);
}

static struct devlink_resource_ops nsim_ipv4_fib_res_ops = {
	.occ_get = nsim_ipv4_fib_resource_occ_get,
};

static u64 nsim_ipv4_fib_rules_res_occ_get(struct devlink *devlink)
{
	struct net *net = nsim_devlink_net(devlink);

	return nsim_fib_get_val(net, NSIM_RESOURCE_IPV4_FIB_RULES, false);
}

static struct devlink_resource_ops nsim_ipv4_fib_rules_res_ops = {
	.occ_get = nsim_ipv4_fib_rules_res_occ_get,
};

/* IPv6
 */
static u64 nsim_ipv6_fib_resource_occ_get(struct devlink *devlink)
{
	struct net *net = nsim_devlink_net(devlink);

	return nsim_fib_get_val(net, NSIM_RESOURCE_IPV6_FIB, false);
}

static struct devlink_resource_ops nsim_ipv6_fib_res_ops = {
	.occ_get = nsim_ipv6_fib_resource_occ_get,
};

static u64 nsim_ipv6_fib_rules_res_occ_get(struct devlink *devlink)
{
	struct net *net = nsim_devlink_net(devlink);

	return nsim_fib_get_val(net, NSIM_RESOURCE_IPV6_FIB_RULES, false);
}

static struct devlink_resource_ops nsim_ipv6_fib_rules_res_ops = {
	.occ_get = nsim_ipv6_fib_rules_res_occ_get,
};

static int devlink_resources_register(struct devlink *devlink)
{
	struct devlink_resource_size_params params = {
		.size_max = (u64)-1,
		.size_granularity = 1,
		.unit = DEVLINK_RESOURCE_UNIT_ENTRY
	};
	struct net *net = nsim_devlink_net(devlink);
	int err;
	u64 n;

	/* Resources for IPv4 */
	err = devlink_resource_register(devlink, "IPv4", (u64)-1,
					NSIM_RESOURCE_IPV4,
					DEVLINK_RESOURCE_ID_PARENT_TOP,
					&params, NULL);
	if (err) {
		pr_err("Failed to register IPv4 top resource\n");
		goto out;
	}

	n = nsim_fib_get_val(net, NSIM_RESOURCE_IPV4_FIB, true);
	err = devlink_resource_register(devlink, "fib", n,
					NSIM_RESOURCE_IPV4_FIB,
					NSIM_RESOURCE_IPV4,
					&params, &nsim_ipv4_fib_res_ops);
	if (err) {
		pr_err("Failed to register IPv4 FIB resource\n");
		return err;
	}

	n = nsim_fib_get_val(net, NSIM_RESOURCE_IPV4_FIB_RULES, true);
	err = devlink_resource_register(devlink, "fib-rules", n,
					NSIM_RESOURCE_IPV4_FIB_RULES,
					NSIM_RESOURCE_IPV4,
					&params, &nsim_ipv4_fib_rules_res_ops);
	if (err) {
		pr_err("Failed to register IPv4 FIB rules resource\n");
		return err;
	}

	/* Resources for IPv6 */
	err = devlink_resource_register(devlink, "IPv6", (u64)-1,
					NSIM_RESOURCE_IPV6,
					DEVLINK_RESOURCE_ID_PARENT_TOP,
					&params, NULL);
	if (err) {
		pr_err("Failed to register IPv6 top resource\n");
		goto out;
	}

	n = nsim_fib_get_val(net, NSIM_RESOURCE_IPV6_FIB, true);
	err = devlink_resource_register(devlink, "fib", n,
					NSIM_RESOURCE_IPV6_FIB,
					NSIM_RESOURCE_IPV6,
					&params, &nsim_ipv6_fib_res_ops);
	if (err) {
		pr_err("Failed to register IPv6 FIB resource\n");
		return err;
	}

	n = nsim_fib_get_val(net, NSIM_RESOURCE_IPV6_FIB_RULES, true);
	err = devlink_resource_register(devlink, "fib-rules", n,
					NSIM_RESOURCE_IPV6_FIB_RULES,
					NSIM_RESOURCE_IPV6,
					&params, &nsim_ipv6_fib_rules_res_ops);
	if (err) {
		pr_err("Failed to register IPv6 FIB rules resource\n");
		return err;
	}
out:
	return err;
}

static int nsim_devlink_reload(struct devlink *devlink)
{
	enum nsim_resource_id res_ids[] = {
		NSIM_RESOURCE_IPV4_FIB, NSIM_RESOURCE_IPV4_FIB_RULES,
		NSIM_RESOURCE_IPV6_FIB, NSIM_RESOURCE_IPV6_FIB_RULES
	};
	struct net *net = nsim_devlink_net(devlink);
	int i;

	for (i = 0; i < ARRAY_SIZE(res_ids); ++i) {
		int err;
		u64 val;

		err = devlink_resource_size_get(devlink, res_ids[i], &val);
		if (!err) {
			err = nsim_fib_set_max(net, res_ids[i], val);
			if (err)
				return err;
		}
	}

	return 0;
}

static void nsim_devlink_net_reset(struct net *net)
{
	enum nsim_resource_id res_ids[] = {
		NSIM_RESOURCE_IPV4_FIB, NSIM_RESOURCE_IPV4_FIB_RULES,
		NSIM_RESOURCE_IPV6_FIB, NSIM_RESOURCE_IPV6_FIB_RULES
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(res_ids); ++i) {
		if (nsim_fib_set_max(net, res_ids[i], (u64)-1)) {
			pr_err("Failed to reset limit for resource %u\n",
			       res_ids[i]);
		}
	}
}

static const struct devlink_ops nsim_devlink_ops = {
	.reload = nsim_devlink_reload,
};

/* once devlink / namespace issues are sorted out
 * this needs to be net in which a devlink instance
 * is to be created. e.g., dev_net(ns->netdev)
 */
static struct net *nsim_to_net(struct netdevsim *ns)
{
	return &init_net;
}

void nsim_devlink_teardown(struct netdevsim *ns)
{
	if (ns->devlink) {
		struct net *net = nsim_to_net(ns);
		bool *reg_devlink = net_generic(net, nsim_devlink_id);

		devlink_unregister(ns->devlink);
		devlink_free(ns->devlink);
		ns->devlink = NULL;

		nsim_devlink_net_reset(net);
		*reg_devlink = true;
	}
}

void nsim_devlink_setup(struct netdevsim *ns)
{
	struct net *net = nsim_to_net(ns);
	bool *reg_devlink = net_generic(net, nsim_devlink_id);
	struct devlink *devlink;
	int err = -ENOMEM;

	/* only one device per namespace controls devlink */
	if (!*reg_devlink) {
		ns->devlink = NULL;
		return;
	}

	devlink = devlink_alloc(&nsim_devlink_ops, 0);
	if (!devlink)
		return;

	err = devlink_register(devlink, &ns->dev);
	if (err)
		goto err_devlink_free;

	err = devlink_resources_register(devlink);
	if (err)
		goto err_dl_unregister;

	ns->devlink = devlink;

	*reg_devlink = false;

	return;

err_dl_unregister:
	devlink_unregister(devlink);
err_devlink_free:
	devlink_free(devlink);
}

/* Initialize per network namespace state */
static int __net_init nsim_devlink_netns_init(struct net *net)
{
	bool *reg_devlink = net_generic(net, nsim_devlink_id);

	*reg_devlink = true;

	return 0;
}

static struct pernet_operations nsim_devlink_net_ops __net_initdata = {
	.init = nsim_devlink_netns_init,
	.id   = &nsim_devlink_id,
	.size = sizeof(bool),
};

void nsim_devlink_exit(void)
{
	unregister_pernet_subsys(&nsim_devlink_net_ops);
	nsim_fib_exit();
}

int nsim_devlink_init(void)
{
	int err;

	err = nsim_fib_init();
	if (err)
		goto err_out;

	err = register_pernet_subsys(&nsim_devlink_net_ops);
	if (err)
		nsim_fib_exit();

err_out:
	return err;
}
