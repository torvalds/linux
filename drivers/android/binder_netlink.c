// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/binder.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "binder_netlink.h"

#include <uapi/linux/android/binder_netlink.h>

/* Ops table for binder */
static const struct genl_split_ops binder_nl_ops[] = {
};

static const struct genl_multicast_group binder_nl_mcgrps[] = {
	[BINDER_NLGRP_REPORT] = { "report", },
};

struct genl_family binder_nl_family __ro_after_init = {
	.name		= BINDER_FAMILY_NAME,
	.version	= BINDER_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= binder_nl_ops,
	.n_split_ops	= ARRAY_SIZE(binder_nl_ops),
	.mcgrps		= binder_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(binder_nl_mcgrps),
};
