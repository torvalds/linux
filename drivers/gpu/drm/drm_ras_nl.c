// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/drm_ras.yaml */
/* YNL-GEN kernel source */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "drm_ras_nl.h"

#include <uapi/drm/drm_ras.h>

/* DRM_RAS_CMD_GET_ERROR_COUNTER - do */
static const struct nla_policy drm_ras_get_error_counter_do_nl_policy[DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_ID + 1] = {
	[DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID] = { .type = NLA_U32, },
	[DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_ID] = { .type = NLA_U32, },
};

/* DRM_RAS_CMD_GET_ERROR_COUNTER - dump */
static const struct nla_policy drm_ras_get_error_counter_dump_nl_policy[DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID + 1] = {
	[DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID] = { .type = NLA_U32, },
};

/* Ops table for drm_ras */
static const struct genl_split_ops drm_ras_nl_ops[] = {
	{
		.cmd	= DRM_RAS_CMD_LIST_NODES,
		.dumpit	= drm_ras_nl_list_nodes_dumpit,
		.flags	= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DRM_RAS_CMD_GET_ERROR_COUNTER,
		.doit		= drm_ras_nl_get_error_counter_doit,
		.policy		= drm_ras_get_error_counter_do_nl_policy,
		.maxattr	= DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DRM_RAS_CMD_GET_ERROR_COUNTER,
		.dumpit		= drm_ras_nl_get_error_counter_dumpit,
		.policy		= drm_ras_get_error_counter_dump_nl_policy,
		.maxattr	= DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
};

struct genl_family drm_ras_nl_family __ro_after_init = {
	.name		= DRM_RAS_FAMILY_NAME,
	.version	= DRM_RAS_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= drm_ras_nl_ops,
	.n_split_ops	= ARRAY_SIZE(drm_ras_nl_ops),
};
