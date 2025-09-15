/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/ovpn.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_OVPN_GEN_H
#define _LINUX_OVPN_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/ovpn.h>

/* Common nested types */
extern const struct nla_policy ovpn_keyconf_nl_policy[OVPN_A_KEYCONF_DECRYPT_DIR + 1];
extern const struct nla_policy ovpn_keyconf_del_input_nl_policy[OVPN_A_KEYCONF_SLOT + 1];
extern const struct nla_policy ovpn_keyconf_get_nl_policy[OVPN_A_KEYCONF_CIPHER_ALG + 1];
extern const struct nla_policy ovpn_keyconf_swap_input_nl_policy[OVPN_A_KEYCONF_PEER_ID + 1];
extern const struct nla_policy ovpn_keydir_nl_policy[OVPN_A_KEYDIR_NONCE_TAIL + 1];
extern const struct nla_policy ovpn_peer_nl_policy[OVPN_A_PEER_LINK_TX_PACKETS + 1];
extern const struct nla_policy ovpn_peer_del_input_nl_policy[OVPN_A_PEER_ID + 1];
extern const struct nla_policy ovpn_peer_new_input_nl_policy[OVPN_A_PEER_KEEPALIVE_TIMEOUT + 1];
extern const struct nla_policy ovpn_peer_set_input_nl_policy[OVPN_A_PEER_KEEPALIVE_TIMEOUT + 1];

int ovpn_nl_pre_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		     struct genl_info *info);
void
ovpn_nl_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		  struct genl_info *info);

int ovpn_nl_peer_new_doit(struct sk_buff *skb, struct genl_info *info);
int ovpn_nl_peer_set_doit(struct sk_buff *skb, struct genl_info *info);
int ovpn_nl_peer_get_doit(struct sk_buff *skb, struct genl_info *info);
int ovpn_nl_peer_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int ovpn_nl_peer_del_doit(struct sk_buff *skb, struct genl_info *info);
int ovpn_nl_key_new_doit(struct sk_buff *skb, struct genl_info *info);
int ovpn_nl_key_get_doit(struct sk_buff *skb, struct genl_info *info);
int ovpn_nl_key_swap_doit(struct sk_buff *skb, struct genl_info *info);
int ovpn_nl_key_del_doit(struct sk_buff *skb, struct genl_info *info);

enum {
	OVPN_NLGRP_PEERS,
};

extern struct genl_family ovpn_nl_family;

#endif /* _LINUX_OVPN_GEN_H */
