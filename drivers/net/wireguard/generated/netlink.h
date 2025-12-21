/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/wireguard.yaml */
/* YNL-GEN kernel header */
/* YNL-ARG --function-prefix wg */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _LINUX_WIREGUARD_GEN_H
#define _LINUX_WIREGUARD_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/wireguard.h>
#include <linux/time_types.h>

/* Common nested types */
extern const struct nla_policy wireguard_wgallowedip_nl_policy[WGALLOWEDIP_A_FLAGS + 1];
extern const struct nla_policy wireguard_wgpeer_nl_policy[WGPEER_A_PROTOCOL_VERSION + 1];

/* Ops table for wireguard */
extern const struct genl_split_ops wireguard_nl_ops[2];

int wg_get_device_start(struct netlink_callback *cb);
int wg_get_device_done(struct netlink_callback *cb);

int wg_get_device_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int wg_set_device_doit(struct sk_buff *skb, struct genl_info *info);

#endif /* _LINUX_WIREGUARD_GEN_H */
