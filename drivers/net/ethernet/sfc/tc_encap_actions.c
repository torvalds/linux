// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2023, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "tc_encap_actions.h"
#include "tc.h"
#include "mae.h"
#include <net/flow.h>
#include <net/inet_dscp.h>
#include <net/vxlan.h>
#include <net/geneve.h>
#include <net/netevent.h>
#include <net/arp.h>

static const struct rhashtable_params efx_neigh_ht_params = {
	.key_len	= offsetof(struct efx_neigh_binder, ha),
	.key_offset	= 0,
	.head_offset	= offsetof(struct efx_neigh_binder, linkage),
};

static const struct rhashtable_params efx_tc_encap_ht_params = {
	.key_len	= offsetofend(struct efx_tc_encap_action, key),
	.key_offset	= 0,
	.head_offset	= offsetof(struct efx_tc_encap_action, linkage),
};

static void efx_tc_encap_free(void *ptr, void *__unused)
{
	struct efx_tc_encap_action *enc = ptr;

	WARN_ON(refcount_read(&enc->ref));
	kfree(enc);
}

static void efx_neigh_free(void *ptr, void *__unused)
{
	struct efx_neigh_binder *neigh = ptr;

	WARN_ON(refcount_read(&neigh->ref));
	WARN_ON(!list_empty(&neigh->users));
	put_net_track(neigh->net, &neigh->ns_tracker);
	netdev_put(neigh->egdev, &neigh->dev_tracker);
	kfree(neigh);
}

int efx_tc_init_encap_actions(struct efx_nic *efx)
{
	int rc;

	rc = rhashtable_init(&efx->tc->neigh_ht, &efx_neigh_ht_params);
	if (rc < 0)
		goto fail_neigh_ht;
	rc = rhashtable_init(&efx->tc->encap_ht, &efx_tc_encap_ht_params);
	if (rc < 0)
		goto fail_encap_ht;
	return 0;
fail_encap_ht:
	rhashtable_destroy(&efx->tc->neigh_ht);
fail_neigh_ht:
	return rc;
}

/* Only call this in init failure teardown.
 * Normal exit should fini instead as there may be entries in the table.
 */
void efx_tc_destroy_encap_actions(struct efx_nic *efx)
{
	rhashtable_destroy(&efx->tc->encap_ht);
	rhashtable_destroy(&efx->tc->neigh_ht);
}

void efx_tc_fini_encap_actions(struct efx_nic *efx)
{
	rhashtable_free_and_destroy(&efx->tc->encap_ht, efx_tc_encap_free, NULL);
	rhashtable_free_and_destroy(&efx->tc->neigh_ht, efx_neigh_free, NULL);
}

static void efx_neigh_update(struct work_struct *work);

static int efx_bind_neigh(struct efx_nic *efx,
			  struct efx_tc_encap_action *encap, struct net *net,
			  struct netlink_ext_ack *extack)
{
	struct efx_neigh_binder *neigh, *old;
	struct flowi6 flow6 = {};
	struct flowi4 flow4 = {};
	int rc;

	/* GCC stupidly thinks that only values explicitly listed in the enum
	 * definition can _possibly_ be sensible case values, so without this
	 * cast it complains about the IPv6 versions.
	 */
	switch ((int)encap->type) {
	case EFX_ENCAP_TYPE_VXLAN:
	case EFX_ENCAP_TYPE_GENEVE:
		flow4.flowi4_proto = IPPROTO_UDP;
		flow4.fl4_dport = encap->key.tp_dst;
		flow4.flowi4_dscp = inet_dsfield_to_dscp(encap->key.tos);
		flow4.daddr = encap->key.u.ipv4.dst;
		flow4.saddr = encap->key.u.ipv4.src;
		break;
	case EFX_ENCAP_TYPE_VXLAN | EFX_ENCAP_FLAG_IPV6:
	case EFX_ENCAP_TYPE_GENEVE | EFX_ENCAP_FLAG_IPV6:
		flow6.flowi6_proto = IPPROTO_UDP;
		flow6.fl6_dport = encap->key.tp_dst;
		flow6.flowlabel = ip6_make_flowinfo(encap->key.tos,
						    encap->key.label);
		flow6.daddr = encap->key.u.ipv6.dst;
		flow6.saddr = encap->key.u.ipv6.src;
		break;
	default:
		NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported encap type %d",
				       (int)encap->type);
		return -EOPNOTSUPP;
	}

	neigh = kzalloc(sizeof(*neigh), GFP_KERNEL_ACCOUNT);
	if (!neigh)
		return -ENOMEM;
	neigh->net = get_net_track(net, &neigh->ns_tracker, GFP_KERNEL_ACCOUNT);
	neigh->dst_ip = flow4.daddr;
	neigh->dst_ip6 = flow6.daddr;

	old = rhashtable_lookup_get_insert_fast(&efx->tc->neigh_ht,
						&neigh->linkage,
						efx_neigh_ht_params);
	if (old) {
		/* don't need our new entry */
		put_net_track(neigh->net, &neigh->ns_tracker);
		kfree(neigh);
		if (IS_ERR(old)) /* oh dear, it's actually an error */
			return PTR_ERR(old);
		if (!refcount_inc_not_zero(&old->ref))
			return -EAGAIN;
		/* existing entry found, ref taken */
		neigh = old;
	} else {
		/* New entry.  We need to initiate a lookup */
		struct neighbour *n;
		struct rtable *rt;

		if (encap->type & EFX_ENCAP_FLAG_IPV6) {
#if IS_ENABLED(CONFIG_IPV6)
			struct dst_entry *dst;

			dst = ipv6_stub->ipv6_dst_lookup_flow(net, NULL, &flow6,
							      NULL);
			rc = PTR_ERR_OR_ZERO(dst);
			if (rc) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to lookup route for IPv6 encap");
				goto out_free;
			}
			neigh->egdev = dst->dev;
			netdev_hold(neigh->egdev, &neigh->dev_tracker,
				    GFP_KERNEL_ACCOUNT);
			neigh->ttl = ip6_dst_hoplimit(dst);
			n = dst_neigh_lookup(dst, &flow6.daddr);
			dst_release(dst);
#else
			/* We shouldn't ever get here, because if IPv6 isn't
			 * enabled how did someone create an IPv6 tunnel_key?
			 */
			rc = -EOPNOTSUPP;
			NL_SET_ERR_MSG_MOD(extack, "No IPv6 support (neigh bind)");
			goto out_free;
#endif
		} else {
			rt = ip_route_output_key(net, &flow4);
			if (IS_ERR_OR_NULL(rt)) {
				rc = PTR_ERR_OR_ZERO(rt);
				if (!rc)
					rc = -EIO;
				NL_SET_ERR_MSG_MOD(extack, "Failed to lookup route for encap");
				goto out_free;
			}
			neigh->egdev = rt->dst.dev;
			netdev_hold(neigh->egdev, &neigh->dev_tracker,
				    GFP_KERNEL_ACCOUNT);
			neigh->ttl = ip4_dst_hoplimit(&rt->dst);
			n = dst_neigh_lookup(&rt->dst, &flow4.daddr);
			ip_rt_put(rt);
		}
		if (!n) {
			rc = -ENETUNREACH;
			NL_SET_ERR_MSG_MOD(extack, "Failed to lookup neighbour for encap");
			netdev_put(neigh->egdev, &neigh->dev_tracker);
			goto out_free;
		}
		refcount_set(&neigh->ref, 1);
		INIT_LIST_HEAD(&neigh->users);
		read_lock_bh(&n->lock);
		ether_addr_copy(neigh->ha, n->ha);
		neigh->n_valid = n->nud_state & NUD_VALID;
		read_unlock_bh(&n->lock);
		rwlock_init(&neigh->lock);
		INIT_WORK(&neigh->work, efx_neigh_update);
		neigh->efx = efx;
		neigh->used = jiffies;
		if (!neigh->n_valid)
			/* Prod ARP to find us a neighbour */
			neigh_event_send(n, NULL);
		neigh_release(n);
	}
	/* Add us to this neigh */
	encap->neigh = neigh;
	list_add_tail(&encap->list, &neigh->users);
	return 0;

out_free:
	/* cleanup common to several error paths */
	rhashtable_remove_fast(&efx->tc->neigh_ht, &neigh->linkage,
			       efx_neigh_ht_params);
	synchronize_rcu();
	put_net_track(net, &neigh->ns_tracker);
	kfree(neigh);
	return rc;
}

static void efx_free_neigh(struct efx_neigh_binder *neigh)
{
	struct efx_nic *efx = neigh->efx;

	rhashtable_remove_fast(&efx->tc->neigh_ht, &neigh->linkage,
			       efx_neigh_ht_params);
	synchronize_rcu();
	netdev_put(neigh->egdev, &neigh->dev_tracker);
	put_net_track(neigh->net, &neigh->ns_tracker);
	kfree(neigh);
}

static void efx_release_neigh(struct efx_nic *efx,
			      struct efx_tc_encap_action *encap)
{
	struct efx_neigh_binder *neigh = encap->neigh;

	if (!neigh)
		return;
	list_del(&encap->list);
	encap->neigh = NULL;
	if (!refcount_dec_and_test(&neigh->ref))
		return; /* still in use */
	efx_free_neigh(neigh);
}

static void efx_gen_tun_header_eth(struct efx_tc_encap_action *encap, u16 proto)
{
	struct efx_neigh_binder *neigh = encap->neigh;
	struct ethhdr *eth;

	encap->encap_hdr_len = sizeof(*eth);
	eth = (struct ethhdr *)encap->encap_hdr;

	if (encap->neigh->n_valid)
		ether_addr_copy(eth->h_dest, neigh->ha);
	else
		eth_zero_addr(eth->h_dest);
	ether_addr_copy(eth->h_source, neigh->egdev->dev_addr);
	eth->h_proto = htons(proto);
}

static void efx_gen_tun_header_ipv4(struct efx_tc_encap_action *encap, u8 ipproto, u8 len)
{
	struct efx_neigh_binder *neigh = encap->neigh;
	struct ip_tunnel_key *key = &encap->key;
	struct iphdr *ip;

	ip = (struct iphdr *)(encap->encap_hdr + encap->encap_hdr_len);
	encap->encap_hdr_len += sizeof(*ip);

	ip->daddr = key->u.ipv4.dst;
	ip->saddr = key->u.ipv4.src;
	ip->ttl = neigh->ttl;
	ip->protocol = ipproto;
	ip->version = 0x4;
	ip->ihl = 0x5;
	ip->tot_len = cpu_to_be16(ip->ihl * 4 + len);
	ip_send_check(ip);
}

#ifdef CONFIG_IPV6
static void efx_gen_tun_header_ipv6(struct efx_tc_encap_action *encap, u8 ipproto, u8 len)
{
	struct efx_neigh_binder *neigh = encap->neigh;
	struct ip_tunnel_key *key = &encap->key;
	struct ipv6hdr *ip;

	ip = (struct ipv6hdr *)(encap->encap_hdr + encap->encap_hdr_len);
	encap->encap_hdr_len += sizeof(*ip);

	ip6_flow_hdr(ip, key->tos, key->label);
	ip->daddr = key->u.ipv6.dst;
	ip->saddr = key->u.ipv6.src;
	ip->hop_limit = neigh->ttl;
	ip->nexthdr = ipproto;
	ip->version = 0x6;
	ip->payload_len = cpu_to_be16(len);
}
#endif

static void efx_gen_tun_header_udp(struct efx_tc_encap_action *encap, u8 len)
{
	struct ip_tunnel_key *key = &encap->key;
	struct udphdr *udp;

	udp = (struct udphdr *)(encap->encap_hdr + encap->encap_hdr_len);
	encap->encap_hdr_len += sizeof(*udp);

	udp->dest = key->tp_dst;
	udp->len = cpu_to_be16(sizeof(*udp) + len);
}

static void efx_gen_tun_header_vxlan(struct efx_tc_encap_action *encap)
{
	struct ip_tunnel_key *key = &encap->key;
	struct vxlanhdr *vxlan;

	vxlan = (struct vxlanhdr *)(encap->encap_hdr + encap->encap_hdr_len);
	encap->encap_hdr_len += sizeof(*vxlan);

	vxlan->vx_flags = VXLAN_HF_VNI;
	vxlan->vx_vni = vxlan_vni_field(tunnel_id_to_key32(key->tun_id));
}

static void efx_gen_tun_header_geneve(struct efx_tc_encap_action *encap)
{
	struct ip_tunnel_key *key = &encap->key;
	struct genevehdr *geneve;
	u32 vni;

	geneve = (struct genevehdr *)(encap->encap_hdr + encap->encap_hdr_len);
	encap->encap_hdr_len += sizeof(*geneve);

	geneve->proto_type = htons(ETH_P_TEB);
	/* convert tun_id to host-endian so we can use host arithmetic to
	 * extract individual bytes.
	 */
	vni = ntohl(tunnel_id_to_key32(key->tun_id));
	geneve->vni[0] = vni >> 16;
	geneve->vni[1] = vni >> 8;
	geneve->vni[2] = vni;
}

#define vxlan_header_l4_len	(sizeof(struct udphdr) + sizeof(struct vxlanhdr))
#define vxlan4_header_len	(sizeof(struct ethhdr) + sizeof(struct iphdr) + vxlan_header_l4_len)
static void efx_gen_vxlan_header_ipv4(struct efx_tc_encap_action *encap)
{
	BUILD_BUG_ON(sizeof(encap->encap_hdr) < vxlan4_header_len);
	efx_gen_tun_header_eth(encap, ETH_P_IP);
	efx_gen_tun_header_ipv4(encap, IPPROTO_UDP, vxlan_header_l4_len);
	efx_gen_tun_header_udp(encap, sizeof(struct vxlanhdr));
	efx_gen_tun_header_vxlan(encap);
}

#define geneve_header_l4_len	(sizeof(struct udphdr) + sizeof(struct genevehdr))
#define geneve4_header_len	(sizeof(struct ethhdr) + sizeof(struct iphdr) + geneve_header_l4_len)
static void efx_gen_geneve_header_ipv4(struct efx_tc_encap_action *encap)
{
	BUILD_BUG_ON(sizeof(encap->encap_hdr) < geneve4_header_len);
	efx_gen_tun_header_eth(encap, ETH_P_IP);
	efx_gen_tun_header_ipv4(encap, IPPROTO_UDP, geneve_header_l4_len);
	efx_gen_tun_header_udp(encap, sizeof(struct genevehdr));
	efx_gen_tun_header_geneve(encap);
}

#ifdef CONFIG_IPV6
#define vxlan6_header_len	(sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + vxlan_header_l4_len)
static void efx_gen_vxlan_header_ipv6(struct efx_tc_encap_action *encap)
{
	BUILD_BUG_ON(sizeof(encap->encap_hdr) < vxlan6_header_len);
	efx_gen_tun_header_eth(encap, ETH_P_IPV6);
	efx_gen_tun_header_ipv6(encap, IPPROTO_UDP, vxlan_header_l4_len);
	efx_gen_tun_header_udp(encap, sizeof(struct vxlanhdr));
	efx_gen_tun_header_vxlan(encap);
}

#define geneve6_header_len	(sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + geneve_header_l4_len)
static void efx_gen_geneve_header_ipv6(struct efx_tc_encap_action *encap)
{
	BUILD_BUG_ON(sizeof(encap->encap_hdr) < geneve6_header_len);
	efx_gen_tun_header_eth(encap, ETH_P_IPV6);
	efx_gen_tun_header_ipv6(encap, IPPROTO_UDP, geneve_header_l4_len);
	efx_gen_tun_header_udp(encap, sizeof(struct genevehdr));
	efx_gen_tun_header_geneve(encap);
}
#endif

static void efx_gen_encap_header(struct efx_nic *efx,
				 struct efx_tc_encap_action *encap)
{
	encap->n_valid = encap->neigh->n_valid;

	/* GCC stupidly thinks that only values explicitly listed in the enum
	 * definition can _possibly_ be sensible case values, so without this
	 * cast it complains about the IPv6 versions.
	 */
	switch ((int)encap->type) {
	case EFX_ENCAP_TYPE_VXLAN:
		efx_gen_vxlan_header_ipv4(encap);
		break;
	case EFX_ENCAP_TYPE_GENEVE:
		efx_gen_geneve_header_ipv4(encap);
		break;
#ifdef CONFIG_IPV6
	case EFX_ENCAP_TYPE_VXLAN | EFX_ENCAP_FLAG_IPV6:
		efx_gen_vxlan_header_ipv6(encap);
		break;
	case EFX_ENCAP_TYPE_GENEVE | EFX_ENCAP_FLAG_IPV6:
		efx_gen_geneve_header_ipv6(encap);
		break;
#endif
	default:
		/* unhandled encap type, can't happen */
		if (net_ratelimit())
			netif_err(efx, drv, efx->net_dev,
				  "Bogus encap type %d, can't generate\n",
				  encap->type);

		/* Use fallback action. */
		encap->n_valid = false;
		break;
	}
}

static void efx_tc_update_encap(struct efx_nic *efx,
				struct efx_tc_encap_action *encap)
{
	struct efx_tc_action_set_list *acts, *fallback;
	struct efx_tc_flow_rule *rule;
	struct efx_tc_action_set *act;
	int rc;

	if (encap->n_valid) {
		/* Make sure no rules are using this encap while we change it */
		list_for_each_entry(act, &encap->users, encap_user) {
			acts = act->user;
			if (WARN_ON(!acts)) /* can't happen */
				continue;
			rule = container_of(acts, struct efx_tc_flow_rule, acts);
			if (rule->fallback)
				fallback = rule->fallback;
			else /* fallback of the fallback: deliver to PF */
				fallback = &efx->tc->facts.pf;
			rc = efx_mae_update_rule(efx, fallback->fw_id,
						 rule->fw_id);
			if (rc)
				netif_err(efx, drv, efx->net_dev,
					  "Failed to update (f) rule %08x rc %d\n",
					  rule->fw_id, rc);
			else
				netif_dbg(efx, drv, efx->net_dev, "Updated (f) rule %08x\n",
					  rule->fw_id);
		}
	}

	/* Make sure we don't leak arbitrary bytes on the wire;
	 * set an all-0s ethernet header.  A successful call to
	 * efx_gen_encap_header() will overwrite this.
	 */
	memset(encap->encap_hdr, 0, sizeof(encap->encap_hdr));
	encap->encap_hdr_len = ETH_HLEN;

	if (encap->neigh) {
		read_lock_bh(&encap->neigh->lock);
		efx_gen_encap_header(efx, encap);
		read_unlock_bh(&encap->neigh->lock);
	} else {
		encap->n_valid = false;
	}

	rc = efx_mae_update_encap_md(efx, encap);
	if (rc) {
		netif_err(efx, drv, efx->net_dev,
			  "Failed to update encap hdr %08x rc %d\n",
			  encap->fw_id, rc);
		return;
	}
	netif_dbg(efx, drv, efx->net_dev, "Updated encap hdr %08x\n",
		  encap->fw_id);
	if (!encap->n_valid)
		return;
	/* Update rule users: use the action if they are now ready */
	list_for_each_entry(act, &encap->users, encap_user) {
		acts = act->user;
		if (WARN_ON(!acts)) /* can't happen */
			continue;
		rule = container_of(acts, struct efx_tc_flow_rule, acts);
		if (!efx_tc_check_ready(efx, rule))
			continue;
		rc = efx_mae_update_rule(efx, acts->fw_id, rule->fw_id);
		if (rc)
			netif_err(efx, drv, efx->net_dev,
				  "Failed to update rule %08x rc %d\n",
				  rule->fw_id, rc);
		else
			netif_dbg(efx, drv, efx->net_dev, "Updated rule %08x\n",
				  rule->fw_id);
	}
}

static void efx_neigh_update(struct work_struct *work)
{
	struct efx_neigh_binder *neigh = container_of(work, struct efx_neigh_binder, work);
	struct efx_tc_encap_action *encap;
	struct efx_nic *efx = neigh->efx;

	mutex_lock(&efx->tc->mutex);
	list_for_each_entry(encap, &neigh->users, list)
		efx_tc_update_encap(neigh->efx, encap);
	/* release ref taken in efx_neigh_event() */
	if (refcount_dec_and_test(&neigh->ref))
		efx_free_neigh(neigh);
	mutex_unlock(&efx->tc->mutex);
}

static int efx_neigh_event(struct efx_nic *efx, struct neighbour *n)
{
	struct efx_neigh_binder keys = {NULL}, *neigh;
	bool n_valid, ipv6 = false;
	char ha[ETH_ALEN];
	size_t keysize;

	if (WARN_ON(!efx->tc))
		return NOTIFY_DONE;

	if (n->tbl == &arp_tbl) {
		keysize = sizeof(keys.dst_ip);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (n->tbl == ipv6_stub->nd_tbl) {
		ipv6 = true;
		keysize = sizeof(keys.dst_ip6);
#endif
	} else {
		return NOTIFY_DONE;
	}
	if (!n->parms) {
		netif_warn(efx, drv, efx->net_dev, "neigh_event with no parms!\n");
		return NOTIFY_DONE;
	}
	keys.net = read_pnet(&n->parms->net);
	if (n->tbl->key_len != keysize) {
		netif_warn(efx, drv, efx->net_dev, "neigh_event with bad key_len %u\n",
			   n->tbl->key_len);
		return NOTIFY_DONE;
	}
	read_lock_bh(&n->lock); /* Get a consistent view */
	memcpy(ha, n->ha, ETH_ALEN);
	n_valid = (n->nud_state & NUD_VALID) && !n->dead;
	read_unlock_bh(&n->lock);
	if (ipv6)
		memcpy(&keys.dst_ip6, n->primary_key, n->tbl->key_len);
	else
		memcpy(&keys.dst_ip, n->primary_key, n->tbl->key_len);
	rcu_read_lock();
	neigh = rhashtable_lookup_fast(&efx->tc->neigh_ht, &keys,
				       efx_neigh_ht_params);
	if (!neigh || neigh->dying)
		/* We're not interested in this neighbour */
		goto done;
	write_lock_bh(&neigh->lock);
	if (n_valid == neigh->n_valid && !memcmp(ha, neigh->ha, ETH_ALEN)) {
		write_unlock_bh(&neigh->lock);
		/* Nothing has changed; no work to do */
		goto done;
	}
	neigh->n_valid = n_valid;
	memcpy(neigh->ha, ha, ETH_ALEN);
	write_unlock_bh(&neigh->lock);
	if (refcount_inc_not_zero(&neigh->ref)) {
		rcu_read_unlock();
		if (!schedule_work(&neigh->work))
			/* failed to schedule, release the ref we just took */
			if (refcount_dec_and_test(&neigh->ref))
				efx_free_neigh(neigh);
	} else {
done:
		rcu_read_unlock();
	}
	return NOTIFY_DONE;
}

bool efx_tc_check_ready(struct efx_nic *efx, struct efx_tc_flow_rule *rule)
{
	struct efx_tc_action_set *act;

	/* Encap actions can only be offloaded if they have valid
	 * neighbour info for the outer Ethernet header.
	 */
	list_for_each_entry(act, &rule->acts.list, list)
		if (act->encap_md && !act->encap_md->n_valid)
			return false;
	return true;
}

struct efx_tc_encap_action *efx_tc_flower_create_encap_md(
			struct efx_nic *efx, const struct ip_tunnel_info *info,
			struct net_device *egdev, struct netlink_ext_ack *extack)
{
	enum efx_encap_type type = efx_tc_indr_netdev_type(egdev);
	struct efx_tc_encap_action *encap, *old;
	struct efx_rep *to_efv;
	s64 rc;

	if (type == EFX_ENCAP_TYPE_NONE) {
		/* dest is not an encap device */
		NL_SET_ERR_MSG_MOD(extack, "Not a (supported) tunnel device but tunnel_key is set");
		return ERR_PTR(-EOPNOTSUPP);
	}
	rc = efx_mae_check_encap_type_supported(efx, type);
	if (rc < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware reports no support for this tunnel type");
		return ERR_PTR(rc);
	}
	/* No support yet for Geneve options */
	if (info->options_len) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported tunnel options");
		return ERR_PTR(-EOPNOTSUPP);
	}
	switch (info->mode) {
	case IP_TUNNEL_INFO_TX:
		break;
	case IP_TUNNEL_INFO_TX | IP_TUNNEL_INFO_IPV6:
		type |= EFX_ENCAP_FLAG_IPV6;
		break;
	default:
		NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported tunnel mode %u",
				       info->mode);
		return ERR_PTR(-EOPNOTSUPP);
	}
	encap = kzalloc(sizeof(*encap), GFP_KERNEL_ACCOUNT);
	if (!encap)
		return ERR_PTR(-ENOMEM);
	encap->type = type;
	encap->key = info->key;
	INIT_LIST_HEAD(&encap->users);
	old = rhashtable_lookup_get_insert_fast(&efx->tc->encap_ht,
						&encap->linkage,
						efx_tc_encap_ht_params);
	if (old) {
		/* don't need our new entry */
		kfree(encap);
		if (IS_ERR(old)) /* oh dear, it's actually an error */
			return ERR_CAST(old);
		if (!refcount_inc_not_zero(&old->ref))
			return ERR_PTR(-EAGAIN);
		/* existing entry found, ref taken */
		return old;
	}

	rc = efx_bind_neigh(efx, encap, dev_net(egdev), extack);
	if (rc < 0)
		goto out_remove;
	to_efv = efx_tc_flower_lookup_efv(efx, encap->neigh->egdev);
	if (IS_ERR(to_efv)) {
		/* neigh->egdev isn't ours */
		NL_SET_ERR_MSG_MOD(extack, "Tunnel egress device not on switch");
		rc = PTR_ERR(to_efv);
		goto out_release;
	}
	rc = efx_tc_flower_external_mport(efx, to_efv);
	if (rc < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to identify tunnel egress m-port");
		goto out_release;
	}
	encap->dest_mport = rc;
	read_lock_bh(&encap->neigh->lock);
	efx_gen_encap_header(efx, encap);
	read_unlock_bh(&encap->neigh->lock);

	rc = efx_mae_allocate_encap_md(efx, encap);
	if (rc < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to write tunnel header to hw");
		goto out_release;
	}

	/* ref and return */
	refcount_set(&encap->ref, 1);
	return encap;
out_release:
	efx_release_neigh(efx, encap);
out_remove:
	rhashtable_remove_fast(&efx->tc->encap_ht, &encap->linkage,
			       efx_tc_encap_ht_params);
	kfree(encap);
	return ERR_PTR(rc);
}

void efx_tc_flower_release_encap_md(struct efx_nic *efx,
				    struct efx_tc_encap_action *encap)
{
	if (!refcount_dec_and_test(&encap->ref))
		return; /* still in use */
	efx_release_neigh(efx, encap);
	rhashtable_remove_fast(&efx->tc->encap_ht, &encap->linkage,
			       efx_tc_encap_ht_params);
	efx_mae_free_encap_md(efx, encap);
	kfree(encap);
}

static void efx_tc_remove_neigh_users(struct efx_nic *efx, struct efx_neigh_binder *neigh)
{
	struct efx_tc_encap_action *encap, *next;

	list_for_each_entry_safe(encap, next, &neigh->users, list) {
		/* Should cause neigh usage count to fall to zero, freeing it */
		efx_release_neigh(efx, encap);
		/* The encap has lost its neigh, so it's now unready */
		efx_tc_update_encap(efx, encap);
	}
}

void efx_tc_unregister_egdev(struct efx_nic *efx, struct net_device *net_dev)
{
	struct efx_neigh_binder *neigh;
	struct rhashtable_iter walk;

	mutex_lock(&efx->tc->mutex);
	rhashtable_walk_enter(&efx->tc->neigh_ht, &walk);
	rhashtable_walk_start(&walk);
	while ((neigh = rhashtable_walk_next(&walk)) != NULL) {
		if (IS_ERR(neigh))
			continue;
		if (neigh->egdev != net_dev)
			continue;
		neigh->dying = true;
		rhashtable_walk_stop(&walk);
		synchronize_rcu(); /* Make sure any updates see dying flag */
		efx_tc_remove_neigh_users(efx, neigh); /* might sleep */
		rhashtable_walk_start(&walk);
	}
	rhashtable_walk_stop(&walk);
	rhashtable_walk_exit(&walk);
	mutex_unlock(&efx->tc->mutex);
}

int efx_tc_netevent_event(struct efx_nic *efx, unsigned long event,
			  void *ptr)
{
	if (efx->type->is_vf)
		return NOTIFY_DONE;

	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		return efx_neigh_event(efx, ptr);
	default:
		return NOTIFY_DONE;
	}
}
