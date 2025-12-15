// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/wireguard.yaml */
/* YNL-GEN kernel source */
/* YNL-ARG --function-prefix wg */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "netlink.h"

#include <uapi/linux/wireguard.h>
#include <linux/time_types.h>

/* Common nested types */
const struct nla_policy wireguard_wgallowedip_nl_policy[WGALLOWEDIP_A_FLAGS + 1] = {
	[WGALLOWEDIP_A_FAMILY] = { .type = NLA_U16, },
	[WGALLOWEDIP_A_IPADDR] = NLA_POLICY_MIN_LEN(4),
	[WGALLOWEDIP_A_CIDR_MASK] = { .type = NLA_U8, },
	[WGALLOWEDIP_A_FLAGS] = NLA_POLICY_MASK(NLA_U32, 0x1),
};

const struct nla_policy wireguard_wgpeer_nl_policy[WGPEER_A_PROTOCOL_VERSION + 1] = {
	[WGPEER_A_PUBLIC_KEY] = NLA_POLICY_EXACT_LEN(WG_KEY_LEN),
	[WGPEER_A_PRESHARED_KEY] = NLA_POLICY_EXACT_LEN(WG_KEY_LEN),
	[WGPEER_A_FLAGS] = NLA_POLICY_MASK(NLA_U32, 0x7),
	[WGPEER_A_ENDPOINT] = NLA_POLICY_MIN_LEN(16),
	[WGPEER_A_PERSISTENT_KEEPALIVE_INTERVAL] = { .type = NLA_U16, },
	[WGPEER_A_LAST_HANDSHAKE_TIME] = NLA_POLICY_EXACT_LEN(16),
	[WGPEER_A_RX_BYTES] = { .type = NLA_U64, },
	[WGPEER_A_TX_BYTES] = { .type = NLA_U64, },
	[WGPEER_A_ALLOWEDIPS] = NLA_POLICY_NESTED_ARRAY(wireguard_wgallowedip_nl_policy),
	[WGPEER_A_PROTOCOL_VERSION] = { .type = NLA_U32, },
};

/* WG_CMD_GET_DEVICE - dump */
static const struct nla_policy wireguard_get_device_nl_policy[WGDEVICE_A_IFNAME + 1] = {
	[WGDEVICE_A_IFINDEX] = { .type = NLA_U32, },
	[WGDEVICE_A_IFNAME] = { .type = NLA_NUL_STRING, .len = 15, },
};

/* WG_CMD_SET_DEVICE - do */
static const struct nla_policy wireguard_set_device_nl_policy[WGDEVICE_A_PEERS + 1] = {
	[WGDEVICE_A_IFINDEX] = { .type = NLA_U32, },
	[WGDEVICE_A_IFNAME] = { .type = NLA_NUL_STRING, .len = 15, },
	[WGDEVICE_A_PRIVATE_KEY] = NLA_POLICY_EXACT_LEN(WG_KEY_LEN),
	[WGDEVICE_A_PUBLIC_KEY] = NLA_POLICY_EXACT_LEN(WG_KEY_LEN),
	[WGDEVICE_A_FLAGS] = NLA_POLICY_MASK(NLA_U32, 0x1),
	[WGDEVICE_A_LISTEN_PORT] = { .type = NLA_U16, },
	[WGDEVICE_A_FWMARK] = { .type = NLA_U32, },
	[WGDEVICE_A_PEERS] = NLA_POLICY_NESTED_ARRAY(wireguard_wgpeer_nl_policy),
};

/* Ops table for wireguard */
const struct genl_split_ops wireguard_nl_ops[2] = {
	{
		.cmd		= WG_CMD_GET_DEVICE,
		.start		= wg_get_device_start,
		.dumpit		= wg_get_device_dumpit,
		.done		= wg_get_device_done,
		.policy		= wireguard_get_device_nl_policy,
		.maxattr	= WGDEVICE_A_IFNAME,
		.flags		= GENL_UNS_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= WG_CMD_SET_DEVICE,
		.doit		= wg_set_device_doit,
		.policy		= wireguard_set_device_nl_policy,
		.maxattr	= WGDEVICE_A_PEERS,
		.flags		= GENL_UNS_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
};
