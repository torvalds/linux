// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/team.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "team_nl.h"

#include <uapi/linux/if_team.h>

/* Common nested types */
const struct nla_policy team_attr_option_nl_policy[TEAM_ATTR_OPTION_ARRAY_INDEX + 1] = {
	[TEAM_ATTR_OPTION_NAME] = { .type = NLA_STRING, .len = TEAM_STRING_MAX_LEN, },
	[TEAM_ATTR_OPTION_CHANGED] = { .type = NLA_FLAG, },
	[TEAM_ATTR_OPTION_TYPE] = { .type = NLA_U8, },
	[TEAM_ATTR_OPTION_DATA] = { .type = NLA_BINARY, },
	[TEAM_ATTR_OPTION_REMOVED] = { .type = NLA_FLAG, },
	[TEAM_ATTR_OPTION_PORT_IFINDEX] = { .type = NLA_U32, },
	[TEAM_ATTR_OPTION_ARRAY_INDEX] = { .type = NLA_U32, },
};

const struct nla_policy team_item_option_nl_policy[TEAM_ATTR_ITEM_OPTION + 1] = {
	[TEAM_ATTR_ITEM_OPTION] = NLA_POLICY_NESTED(team_attr_option_nl_policy),
};

/* Global operation policy for team */
const struct nla_policy team_nl_policy[TEAM_ATTR_LIST_OPTION + 1] = {
	[TEAM_ATTR_TEAM_IFINDEX] = { .type = NLA_U32, },
	[TEAM_ATTR_LIST_OPTION] = NLA_POLICY_NESTED(team_item_option_nl_policy),
};

/* Ops table for team */
const struct genl_small_ops team_nl_ops[4] = {
	{
		.cmd		= TEAM_CMD_NOOP,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= team_nl_noop_doit,
	},
	{
		.cmd		= TEAM_CMD_OPTIONS_SET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= team_nl_options_set_doit,
		.flags		= GENL_ADMIN_PERM,
	},
	{
		.cmd		= TEAM_CMD_OPTIONS_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= team_nl_options_get_doit,
		.flags		= GENL_ADMIN_PERM,
	},
	{
		.cmd		= TEAM_CMD_PORT_LIST_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT,
		.doit		= team_nl_port_list_get_doit,
		.flags		= GENL_ADMIN_PERM,
	},
};
