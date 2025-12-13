// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/dpll.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "dpll_nl.h"

#include <uapi/linux/dpll.h>

/* Common nested types */
const struct nla_policy dpll_pin_parent_device_nl_policy[DPLL_A_PIN_PHASE_OFFSET + 1] = {
	[DPLL_A_PIN_PARENT_ID] = { .type = NLA_U32, },
	[DPLL_A_PIN_DIRECTION] = NLA_POLICY_RANGE(NLA_U32, 1, 2),
	[DPLL_A_PIN_PRIO] = { .type = NLA_U32, },
	[DPLL_A_PIN_STATE] = NLA_POLICY_RANGE(NLA_U32, 1, 3),
	[DPLL_A_PIN_PHASE_OFFSET] = { .type = NLA_S64, },
};

const struct nla_policy dpll_pin_parent_pin_nl_policy[DPLL_A_PIN_STATE + 1] = {
	[DPLL_A_PIN_PARENT_ID] = { .type = NLA_U32, },
	[DPLL_A_PIN_STATE] = NLA_POLICY_RANGE(NLA_U32, 1, 3),
};

const struct nla_policy dpll_reference_sync_nl_policy[DPLL_A_PIN_STATE + 1] = {
	[DPLL_A_PIN_ID] = { .type = NLA_U32, },
	[DPLL_A_PIN_STATE] = NLA_POLICY_RANGE(NLA_U32, 1, 3),
};

/* DPLL_CMD_DEVICE_ID_GET - do */
static const struct nla_policy dpll_device_id_get_nl_policy[DPLL_A_TYPE + 1] = {
	[DPLL_A_MODULE_NAME] = { .type = NLA_NUL_STRING, },
	[DPLL_A_CLOCK_ID] = { .type = NLA_U64, },
	[DPLL_A_TYPE] = NLA_POLICY_RANGE(NLA_U32, 1, 2),
};

/* DPLL_CMD_DEVICE_GET - do */
static const struct nla_policy dpll_device_get_nl_policy[DPLL_A_ID + 1] = {
	[DPLL_A_ID] = { .type = NLA_U32, },
};

/* DPLL_CMD_DEVICE_SET - do */
static const struct nla_policy dpll_device_set_nl_policy[DPLL_A_PHASE_OFFSET_AVG_FACTOR + 1] = {
	[DPLL_A_ID] = { .type = NLA_U32, },
	[DPLL_A_PHASE_OFFSET_MONITOR] = NLA_POLICY_MAX(NLA_U32, 1),
	[DPLL_A_PHASE_OFFSET_AVG_FACTOR] = { .type = NLA_U32, },
};

/* DPLL_CMD_PIN_ID_GET - do */
static const struct nla_policy dpll_pin_id_get_nl_policy[DPLL_A_PIN_TYPE + 1] = {
	[DPLL_A_PIN_MODULE_NAME] = { .type = NLA_NUL_STRING, },
	[DPLL_A_PIN_CLOCK_ID] = { .type = NLA_U64, },
	[DPLL_A_PIN_BOARD_LABEL] = { .type = NLA_NUL_STRING, },
	[DPLL_A_PIN_PANEL_LABEL] = { .type = NLA_NUL_STRING, },
	[DPLL_A_PIN_PACKAGE_LABEL] = { .type = NLA_NUL_STRING, },
	[DPLL_A_PIN_TYPE] = NLA_POLICY_RANGE(NLA_U32, 1, 5),
};

/* DPLL_CMD_PIN_GET - do */
static const struct nla_policy dpll_pin_get_do_nl_policy[DPLL_A_PIN_ID + 1] = {
	[DPLL_A_PIN_ID] = { .type = NLA_U32, },
};

/* DPLL_CMD_PIN_GET - dump */
static const struct nla_policy dpll_pin_get_dump_nl_policy[DPLL_A_PIN_ID + 1] = {
	[DPLL_A_PIN_ID] = { .type = NLA_U32, },
};

/* DPLL_CMD_PIN_SET - do */
static const struct nla_policy dpll_pin_set_nl_policy[DPLL_A_PIN_REFERENCE_SYNC + 1] = {
	[DPLL_A_PIN_ID] = { .type = NLA_U32, },
	[DPLL_A_PIN_FREQUENCY] = { .type = NLA_U64, },
	[DPLL_A_PIN_DIRECTION] = NLA_POLICY_RANGE(NLA_U32, 1, 2),
	[DPLL_A_PIN_PRIO] = { .type = NLA_U32, },
	[DPLL_A_PIN_STATE] = NLA_POLICY_RANGE(NLA_U32, 1, 3),
	[DPLL_A_PIN_PARENT_DEVICE] = NLA_POLICY_NESTED(dpll_pin_parent_device_nl_policy),
	[DPLL_A_PIN_PARENT_PIN] = NLA_POLICY_NESTED(dpll_pin_parent_pin_nl_policy),
	[DPLL_A_PIN_PHASE_ADJUST] = { .type = NLA_S32, },
	[DPLL_A_PIN_ESYNC_FREQUENCY] = { .type = NLA_U64, },
	[DPLL_A_PIN_REFERENCE_SYNC] = NLA_POLICY_NESTED(dpll_reference_sync_nl_policy),
};

/* Ops table for dpll */
static const struct genl_split_ops dpll_nl_ops[] = {
	{
		.cmd		= DPLL_CMD_DEVICE_ID_GET,
		.pre_doit	= dpll_lock_doit,
		.doit		= dpll_nl_device_id_get_doit,
		.post_doit	= dpll_unlock_doit,
		.policy		= dpll_device_id_get_nl_policy,
		.maxattr	= DPLL_A_TYPE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DPLL_CMD_DEVICE_GET,
		.pre_doit	= dpll_pre_doit,
		.doit		= dpll_nl_device_get_doit,
		.post_doit	= dpll_post_doit,
		.policy		= dpll_device_get_nl_policy,
		.maxattr	= DPLL_A_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd	= DPLL_CMD_DEVICE_GET,
		.dumpit	= dpll_nl_device_get_dumpit,
		.flags	= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DPLL_CMD_DEVICE_SET,
		.pre_doit	= dpll_pre_doit,
		.doit		= dpll_nl_device_set_doit,
		.post_doit	= dpll_post_doit,
		.policy		= dpll_device_set_nl_policy,
		.maxattr	= DPLL_A_PHASE_OFFSET_AVG_FACTOR,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DPLL_CMD_PIN_ID_GET,
		.pre_doit	= dpll_lock_doit,
		.doit		= dpll_nl_pin_id_get_doit,
		.post_doit	= dpll_unlock_doit,
		.policy		= dpll_pin_id_get_nl_policy,
		.maxattr	= DPLL_A_PIN_TYPE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DPLL_CMD_PIN_GET,
		.pre_doit	= dpll_pin_pre_doit,
		.doit		= dpll_nl_pin_get_doit,
		.post_doit	= dpll_pin_post_doit,
		.policy		= dpll_pin_get_do_nl_policy,
		.maxattr	= DPLL_A_PIN_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= DPLL_CMD_PIN_GET,
		.dumpit		= dpll_nl_pin_get_dumpit,
		.policy		= dpll_pin_get_dump_nl_policy,
		.maxattr	= DPLL_A_PIN_ID,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DPLL_CMD_PIN_SET,
		.pre_doit	= dpll_pin_pre_doit,
		.doit		= dpll_nl_pin_set_doit,
		.post_doit	= dpll_pin_post_doit,
		.policy		= dpll_pin_set_nl_policy,
		.maxattr	= DPLL_A_PIN_REFERENCE_SYNC,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
};

static const struct genl_multicast_group dpll_nl_mcgrps[] = {
	[DPLL_NLGRP_MONITOR] = { "monitor", },
};

struct genl_family dpll_nl_family __ro_after_init = {
	.name		= DPLL_FAMILY_NAME,
	.version	= DPLL_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= dpll_nl_ops,
	.n_split_ops	= ARRAY_SIZE(dpll_nl_ops),
	.mcgrps		= dpll_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(dpll_nl_mcgrps),
};
