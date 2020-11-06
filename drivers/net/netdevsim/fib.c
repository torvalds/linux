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

#include <linux/in6.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <net/fib_notifier.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/fib_rules.h>
#include <net/net_namespace.h>
#include <net/nexthop.h>

#include "netdevsim.h"

struct nsim_fib_entry {
	u64 max;
	u64 num;
};

struct nsim_per_fib_data {
	struct nsim_fib_entry fib;
	struct nsim_fib_entry rules;
};

struct nsim_fib_data {
	struct notifier_block fib_nb;
	struct nsim_per_fib_data ipv4;
	struct nsim_per_fib_data ipv6;
	struct nsim_fib_entry nexthops;
	struct rhashtable fib_rt_ht;
	struct list_head fib_rt_list;
	spinlock_t fib_lock;	/* Protects hashtable, list and accounting */
	struct notifier_block nexthop_nb;
	struct rhashtable nexthop_ht;
	struct devlink *devlink;
};

struct nsim_fib_rt_key {
	unsigned char addr[sizeof(struct in6_addr)];
	unsigned char prefix_len;
	int family;
	u32 tb_id;
};

struct nsim_fib_rt {
	struct nsim_fib_rt_key key;
	struct rhash_head ht_node;
	struct list_head list;	/* Member of fib_rt_list */
};

struct nsim_fib4_rt {
	struct nsim_fib_rt common;
	struct fib_info *fi;
	u8 tos;
	u8 type;
};

struct nsim_fib6_rt {
	struct nsim_fib_rt common;
	struct list_head nh_list;
	unsigned int nhs;
};

struct nsim_fib6_rt_nh {
	struct list_head list;	/* Member of nh_list */
	struct fib6_info *rt;
};

static const struct rhashtable_params nsim_fib_rt_ht_params = {
	.key_offset = offsetof(struct nsim_fib_rt, key),
	.head_offset = offsetof(struct nsim_fib_rt, ht_node),
	.key_len = sizeof(struct nsim_fib_rt_key),
	.automatic_shrinking = true,
};

struct nsim_nexthop {
	struct rhash_head ht_node;
	u64 occ;
	u32 id;
};

static const struct rhashtable_params nsim_nexthop_ht_params = {
	.key_offset = offsetof(struct nsim_nexthop, id),
	.head_offset = offsetof(struct nsim_nexthop, ht_node),
	.key_len = sizeof(u32),
	.automatic_shrinking = true,
};

u64 nsim_fib_get_val(struct nsim_fib_data *fib_data,
		     enum nsim_resource_id res_id, bool max)
{
	struct nsim_fib_entry *entry;

	switch (res_id) {
	case NSIM_RESOURCE_IPV4_FIB:
		entry = &fib_data->ipv4.fib;
		break;
	case NSIM_RESOURCE_IPV4_FIB_RULES:
		entry = &fib_data->ipv4.rules;
		break;
	case NSIM_RESOURCE_IPV6_FIB:
		entry = &fib_data->ipv6.fib;
		break;
	case NSIM_RESOURCE_IPV6_FIB_RULES:
		entry = &fib_data->ipv6.rules;
		break;
	case NSIM_RESOURCE_NEXTHOPS:
		entry = &fib_data->nexthops;
		break;
	default:
		return 0;
	}

	return max ? entry->max : entry->num;
}

static void nsim_fib_set_max(struct nsim_fib_data *fib_data,
			     enum nsim_resource_id res_id, u64 val)
{
	struct nsim_fib_entry *entry;

	switch (res_id) {
	case NSIM_RESOURCE_IPV4_FIB:
		entry = &fib_data->ipv4.fib;
		break;
	case NSIM_RESOURCE_IPV4_FIB_RULES:
		entry = &fib_data->ipv4.rules;
		break;
	case NSIM_RESOURCE_IPV6_FIB:
		entry = &fib_data->ipv6.fib;
		break;
	case NSIM_RESOURCE_IPV6_FIB_RULES:
		entry = &fib_data->ipv6.rules;
		break;
	case NSIM_RESOURCE_NEXTHOPS:
		entry = &fib_data->nexthops;
		break;
	default:
		WARN_ON(1);
		return;
	}
	entry->max = val;
}

static int nsim_fib_rule_account(struct nsim_fib_entry *entry, bool add,
				 struct netlink_ext_ack *extack)
{
	int err = 0;

	if (add) {
		if (entry->num < entry->max) {
			entry->num++;
		} else {
			err = -ENOSPC;
			NL_SET_ERR_MSG_MOD(extack, "Exceeded number of supported fib rule entries");
		}
	} else {
		entry->num--;
	}

	return err;
}

static int nsim_fib_rule_event(struct nsim_fib_data *data,
			       struct fib_notifier_info *info, bool add)
{
	struct netlink_ext_ack *extack = info->extack;
	int err = 0;

	switch (info->family) {
	case AF_INET:
		err = nsim_fib_rule_account(&data->ipv4.rules, add, extack);
		break;
	case AF_INET6:
		err = nsim_fib_rule_account(&data->ipv6.rules, add, extack);
		break;
	}

	return err;
}

static int nsim_fib_account(struct nsim_fib_entry *entry, bool add,
			    struct netlink_ext_ack *extack)
{
	int err = 0;

	if (add) {
		if (entry->num < entry->max) {
			entry->num++;
		} else {
			err = -ENOSPC;
			NL_SET_ERR_MSG_MOD(extack, "Exceeded number of supported fib entries");
		}
	} else {
		entry->num--;
	}

	return err;
}

static void nsim_fib_rt_init(struct nsim_fib_data *data,
			     struct nsim_fib_rt *fib_rt, const void *addr,
			     size_t addr_len, unsigned int prefix_len,
			     int family, u32 tb_id)
{
	memcpy(fib_rt->key.addr, addr, addr_len);
	fib_rt->key.prefix_len = prefix_len;
	fib_rt->key.family = family;
	fib_rt->key.tb_id = tb_id;
	list_add(&fib_rt->list, &data->fib_rt_list);
}

static void nsim_fib_rt_fini(struct nsim_fib_rt *fib_rt)
{
	list_del(&fib_rt->list);
}

static struct nsim_fib_rt *nsim_fib_rt_lookup(struct rhashtable *fib_rt_ht,
					      const void *addr, size_t addr_len,
					      unsigned int prefix_len,
					      int family, u32 tb_id)
{
	struct nsim_fib_rt_key key;

	memset(&key, 0, sizeof(key));
	memcpy(key.addr, addr, addr_len);
	key.prefix_len = prefix_len;
	key.family = family;
	key.tb_id = tb_id;

	return rhashtable_lookup_fast(fib_rt_ht, &key, nsim_fib_rt_ht_params);
}

static struct nsim_fib4_rt *
nsim_fib4_rt_create(struct nsim_fib_data *data,
		    struct fib_entry_notifier_info *fen_info)
{
	struct nsim_fib4_rt *fib4_rt;

	fib4_rt = kzalloc(sizeof(*fib4_rt), GFP_ATOMIC);
	if (!fib4_rt)
		return NULL;

	nsim_fib_rt_init(data, &fib4_rt->common, &fen_info->dst, sizeof(u32),
			 fen_info->dst_len, AF_INET, fen_info->tb_id);

	fib4_rt->fi = fen_info->fi;
	fib_info_hold(fib4_rt->fi);
	fib4_rt->tos = fen_info->tos;
	fib4_rt->type = fen_info->type;

	return fib4_rt;
}

static void nsim_fib4_rt_destroy(struct nsim_fib4_rt *fib4_rt)
{
	fib_info_put(fib4_rt->fi);
	nsim_fib_rt_fini(&fib4_rt->common);
	kfree(fib4_rt);
}

static struct nsim_fib4_rt *
nsim_fib4_rt_lookup(struct rhashtable *fib_rt_ht,
		    const struct fib_entry_notifier_info *fen_info)
{
	struct nsim_fib_rt *fib_rt;

	fib_rt = nsim_fib_rt_lookup(fib_rt_ht, &fen_info->dst, sizeof(u32),
				    fen_info->dst_len, AF_INET,
				    fen_info->tb_id);
	if (!fib_rt)
		return NULL;

	return container_of(fib_rt, struct nsim_fib4_rt, common);
}

static void nsim_fib4_rt_hw_flags_set(struct net *net,
				      const struct nsim_fib4_rt *fib4_rt,
				      bool trap)
{
	u32 *p_dst = (u32 *) fib4_rt->common.key.addr;
	int dst_len = fib4_rt->common.key.prefix_len;
	struct fib_rt_info fri;

	fri.fi = fib4_rt->fi;
	fri.tb_id = fib4_rt->common.key.tb_id;
	fri.dst = cpu_to_be32(*p_dst);
	fri.dst_len = dst_len;
	fri.tos = fib4_rt->tos;
	fri.type = fib4_rt->type;
	fri.offload = false;
	fri.trap = trap;
	fib_alias_hw_flags_set(net, &fri);
}

static int nsim_fib4_rt_add(struct nsim_fib_data *data,
			    struct nsim_fib4_rt *fib4_rt,
			    struct netlink_ext_ack *extack)
{
	struct net *net = devlink_net(data->devlink);
	int err;

	err = nsim_fib_account(&data->ipv4.fib, true, extack);
	if (err)
		return err;

	err = rhashtable_insert_fast(&data->fib_rt_ht,
				     &fib4_rt->common.ht_node,
				     nsim_fib_rt_ht_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to insert IPv4 route");
		goto err_fib_dismiss;
	}

	nsim_fib4_rt_hw_flags_set(net, fib4_rt, true);

	return 0;

err_fib_dismiss:
	nsim_fib_account(&data->ipv4.fib, false, extack);
	return err;
}

static int nsim_fib4_rt_replace(struct nsim_fib_data *data,
				struct nsim_fib4_rt *fib4_rt,
				struct nsim_fib4_rt *fib4_rt_old,
				struct netlink_ext_ack *extack)
{
	struct net *net = devlink_net(data->devlink);
	int err;

	/* We are replacing a route, so no need to change the accounting. */
	err = rhashtable_replace_fast(&data->fib_rt_ht,
				      &fib4_rt_old->common.ht_node,
				      &fib4_rt->common.ht_node,
				      nsim_fib_rt_ht_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to replace IPv4 route");
		return err;
	}

	nsim_fib4_rt_hw_flags_set(net, fib4_rt, true);

	nsim_fib4_rt_hw_flags_set(net, fib4_rt_old, false);
	nsim_fib4_rt_destroy(fib4_rt_old);

	return 0;
}

static int nsim_fib4_rt_insert(struct nsim_fib_data *data,
			       struct fib_entry_notifier_info *fen_info)
{
	struct netlink_ext_ack *extack = fen_info->info.extack;
	struct nsim_fib4_rt *fib4_rt, *fib4_rt_old;
	int err;

	fib4_rt = nsim_fib4_rt_create(data, fen_info);
	if (!fib4_rt)
		return -ENOMEM;

	fib4_rt_old = nsim_fib4_rt_lookup(&data->fib_rt_ht, fen_info);
	if (!fib4_rt_old)
		err = nsim_fib4_rt_add(data, fib4_rt, extack);
	else
		err = nsim_fib4_rt_replace(data, fib4_rt, fib4_rt_old, extack);

	if (err)
		nsim_fib4_rt_destroy(fib4_rt);

	return err;
}

static void nsim_fib4_rt_remove(struct nsim_fib_data *data,
				const struct fib_entry_notifier_info *fen_info)
{
	struct netlink_ext_ack *extack = fen_info->info.extack;
	struct nsim_fib4_rt *fib4_rt;

	fib4_rt = nsim_fib4_rt_lookup(&data->fib_rt_ht, fen_info);
	if (WARN_ON_ONCE(!fib4_rt))
		return;

	rhashtable_remove_fast(&data->fib_rt_ht, &fib4_rt->common.ht_node,
			       nsim_fib_rt_ht_params);
	nsim_fib_account(&data->ipv4.fib, false, extack);
	nsim_fib4_rt_destroy(fib4_rt);
}

static int nsim_fib4_event(struct nsim_fib_data *data,
			   struct fib_notifier_info *info,
			   unsigned long event)
{
	struct fib_entry_notifier_info *fen_info;
	int err = 0;

	fen_info = container_of(info, struct fib_entry_notifier_info, info);

	switch (event) {
	case FIB_EVENT_ENTRY_REPLACE:
		err = nsim_fib4_rt_insert(data, fen_info);
		break;
	case FIB_EVENT_ENTRY_DEL:
		nsim_fib4_rt_remove(data, fen_info);
		break;
	default:
		break;
	}

	return err;
}

static struct nsim_fib6_rt_nh *
nsim_fib6_rt_nh_find(const struct nsim_fib6_rt *fib6_rt,
		     const struct fib6_info *rt)
{
	struct nsim_fib6_rt_nh *fib6_rt_nh;

	list_for_each_entry(fib6_rt_nh, &fib6_rt->nh_list, list) {
		if (fib6_rt_nh->rt == rt)
			return fib6_rt_nh;
	}

	return NULL;
}

static int nsim_fib6_rt_nh_add(struct nsim_fib6_rt *fib6_rt,
			       struct fib6_info *rt)
{
	struct nsim_fib6_rt_nh *fib6_rt_nh;

	fib6_rt_nh = kzalloc(sizeof(*fib6_rt_nh), GFP_ATOMIC);
	if (!fib6_rt_nh)
		return -ENOMEM;

	fib6_info_hold(rt);
	fib6_rt_nh->rt = rt;
	list_add_tail(&fib6_rt_nh->list, &fib6_rt->nh_list);
	fib6_rt->nhs++;

	return 0;
}

static void nsim_fib6_rt_nh_del(struct nsim_fib6_rt *fib6_rt,
				const struct fib6_info *rt)
{
	struct nsim_fib6_rt_nh *fib6_rt_nh;

	fib6_rt_nh = nsim_fib6_rt_nh_find(fib6_rt, rt);
	if (WARN_ON_ONCE(!fib6_rt_nh))
		return;

	fib6_rt->nhs--;
	list_del(&fib6_rt_nh->list);
#if IS_ENABLED(CONFIG_IPV6)
	fib6_info_release(fib6_rt_nh->rt);
#endif
	kfree(fib6_rt_nh);
}

static struct nsim_fib6_rt *
nsim_fib6_rt_create(struct nsim_fib_data *data,
		    struct fib6_entry_notifier_info *fen6_info)
{
	struct fib6_info *iter, *rt = fen6_info->rt;
	struct nsim_fib6_rt *fib6_rt;
	int i = 0;
	int err;

	fib6_rt = kzalloc(sizeof(*fib6_rt), GFP_ATOMIC);
	if (!fib6_rt)
		return ERR_PTR(-ENOMEM);

	nsim_fib_rt_init(data, &fib6_rt->common, &rt->fib6_dst.addr,
			 sizeof(rt->fib6_dst.addr), rt->fib6_dst.plen, AF_INET6,
			 rt->fib6_table->tb6_id);

	/* We consider a multipath IPv6 route as one entry, but it can be made
	 * up from several fib6_info structs (one for each nexthop), so we
	 * add them all to the same list under the entry.
	 */
	INIT_LIST_HEAD(&fib6_rt->nh_list);

	err = nsim_fib6_rt_nh_add(fib6_rt, rt);
	if (err)
		goto err_fib_rt_fini;

	if (!fen6_info->nsiblings)
		return fib6_rt;

	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings) {
		if (i == fen6_info->nsiblings)
			break;

		err = nsim_fib6_rt_nh_add(fib6_rt, iter);
		if (err)
			goto err_fib6_rt_nh_del;
		i++;
	}
	WARN_ON_ONCE(i != fen6_info->nsiblings);

	return fib6_rt;

err_fib6_rt_nh_del:
	list_for_each_entry_continue_reverse(iter, &rt->fib6_siblings,
					     fib6_siblings)
		nsim_fib6_rt_nh_del(fib6_rt, iter);
	nsim_fib6_rt_nh_del(fib6_rt, rt);
err_fib_rt_fini:
	nsim_fib_rt_fini(&fib6_rt->common);
	kfree(fib6_rt);
	return ERR_PTR(err);
}

static void nsim_fib6_rt_destroy(struct nsim_fib6_rt *fib6_rt)
{
	struct nsim_fib6_rt_nh *iter, *tmp;

	list_for_each_entry_safe(iter, tmp, &fib6_rt->nh_list, list)
		nsim_fib6_rt_nh_del(fib6_rt, iter->rt);
	WARN_ON_ONCE(!list_empty(&fib6_rt->nh_list));
	nsim_fib_rt_fini(&fib6_rt->common);
	kfree(fib6_rt);
}

static struct nsim_fib6_rt *
nsim_fib6_rt_lookup(struct rhashtable *fib_rt_ht, const struct fib6_info *rt)
{
	struct nsim_fib_rt *fib_rt;

	fib_rt = nsim_fib_rt_lookup(fib_rt_ht, &rt->fib6_dst.addr,
				    sizeof(rt->fib6_dst.addr),
				    rt->fib6_dst.plen, AF_INET6,
				    rt->fib6_table->tb6_id);
	if (!fib_rt)
		return NULL;

	return container_of(fib_rt, struct nsim_fib6_rt, common);
}

static int nsim_fib6_rt_append(struct nsim_fib_data *data,
			       struct fib6_entry_notifier_info *fen6_info)
{
	struct fib6_info *iter, *rt = fen6_info->rt;
	struct nsim_fib6_rt *fib6_rt;
	int i = 0;
	int err;

	fib6_rt = nsim_fib6_rt_lookup(&data->fib_rt_ht, rt);
	if (WARN_ON_ONCE(!fib6_rt))
		return -EINVAL;

	err = nsim_fib6_rt_nh_add(fib6_rt, rt);
	if (err)
		return err;
	rt->trap = true;

	if (!fen6_info->nsiblings)
		return 0;

	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings) {
		if (i == fen6_info->nsiblings)
			break;

		err = nsim_fib6_rt_nh_add(fib6_rt, iter);
		if (err)
			goto err_fib6_rt_nh_del;
		iter->trap = true;
		i++;
	}
	WARN_ON_ONCE(i != fen6_info->nsiblings);

	return 0;

err_fib6_rt_nh_del:
	list_for_each_entry_continue_reverse(iter, &rt->fib6_siblings,
					     fib6_siblings) {
		iter->trap = false;
		nsim_fib6_rt_nh_del(fib6_rt, iter);
	}
	rt->trap = false;
	nsim_fib6_rt_nh_del(fib6_rt, rt);
	return err;
}

static void nsim_fib6_rt_hw_flags_set(const struct nsim_fib6_rt *fib6_rt,
				      bool trap)
{
	struct nsim_fib6_rt_nh *fib6_rt_nh;

	list_for_each_entry(fib6_rt_nh, &fib6_rt->nh_list, list)
		fib6_info_hw_flags_set(fib6_rt_nh->rt, false, trap);
}

static int nsim_fib6_rt_add(struct nsim_fib_data *data,
			    struct nsim_fib6_rt *fib6_rt,
			    struct netlink_ext_ack *extack)
{
	int err;

	err = nsim_fib_account(&data->ipv6.fib, true, extack);
	if (err)
		return err;

	err = rhashtable_insert_fast(&data->fib_rt_ht,
				     &fib6_rt->common.ht_node,
				     nsim_fib_rt_ht_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to insert IPv6 route");
		goto err_fib_dismiss;
	}

	nsim_fib6_rt_hw_flags_set(fib6_rt, true);

	return 0;

err_fib_dismiss:
	nsim_fib_account(&data->ipv6.fib, false, extack);
	return err;
}

static int nsim_fib6_rt_replace(struct nsim_fib_data *data,
				struct nsim_fib6_rt *fib6_rt,
				struct nsim_fib6_rt *fib6_rt_old,
				struct netlink_ext_ack *extack)
{
	int err;

	/* We are replacing a route, so no need to change the accounting. */
	err = rhashtable_replace_fast(&data->fib_rt_ht,
				      &fib6_rt_old->common.ht_node,
				      &fib6_rt->common.ht_node,
				      nsim_fib_rt_ht_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to replace IPv6 route");
		return err;
	}

	nsim_fib6_rt_hw_flags_set(fib6_rt, true);

	nsim_fib6_rt_hw_flags_set(fib6_rt_old, false);
	nsim_fib6_rt_destroy(fib6_rt_old);

	return 0;
}

static int nsim_fib6_rt_insert(struct nsim_fib_data *data,
			       struct fib6_entry_notifier_info *fen6_info)
{
	struct netlink_ext_ack *extack = fen6_info->info.extack;
	struct nsim_fib6_rt *fib6_rt, *fib6_rt_old;
	int err;

	fib6_rt = nsim_fib6_rt_create(data, fen6_info);
	if (IS_ERR(fib6_rt))
		return PTR_ERR(fib6_rt);

	fib6_rt_old = nsim_fib6_rt_lookup(&data->fib_rt_ht, fen6_info->rt);
	if (!fib6_rt_old)
		err = nsim_fib6_rt_add(data, fib6_rt, extack);
	else
		err = nsim_fib6_rt_replace(data, fib6_rt, fib6_rt_old, extack);

	if (err)
		nsim_fib6_rt_destroy(fib6_rt);

	return err;
}

static void
nsim_fib6_rt_remove(struct nsim_fib_data *data,
		    const struct fib6_entry_notifier_info *fen6_info)
{
	struct netlink_ext_ack *extack = fen6_info->info.extack;
	struct nsim_fib6_rt *fib6_rt;

	/* Multipath routes are first added to the FIB trie and only then
	 * notified. If we vetoed the addition, we will get a delete
	 * notification for a route we do not have. Therefore, do not warn if
	 * route was not found.
	 */
	fib6_rt = nsim_fib6_rt_lookup(&data->fib_rt_ht, fen6_info->rt);
	if (!fib6_rt)
		return;

	/* If not all the nexthops are deleted, then only reduce the nexthop
	 * group.
	 */
	if (fen6_info->nsiblings + 1 != fib6_rt->nhs) {
		nsim_fib6_rt_nh_del(fib6_rt, fen6_info->rt);
		return;
	}

	rhashtable_remove_fast(&data->fib_rt_ht, &fib6_rt->common.ht_node,
			       nsim_fib_rt_ht_params);
	nsim_fib_account(&data->ipv6.fib, false, extack);
	nsim_fib6_rt_destroy(fib6_rt);
}

static int nsim_fib6_event(struct nsim_fib_data *data,
			   struct fib_notifier_info *info,
			   unsigned long event)
{
	struct fib6_entry_notifier_info *fen6_info;
	int err = 0;

	fen6_info = container_of(info, struct fib6_entry_notifier_info, info);

	if (fen6_info->rt->fib6_src.plen) {
		NL_SET_ERR_MSG_MOD(info->extack, "IPv6 source-specific route is not supported");
		return 0;
	}

	switch (event) {
	case FIB_EVENT_ENTRY_REPLACE:
		err = nsim_fib6_rt_insert(data, fen6_info);
		break;
	case FIB_EVENT_ENTRY_APPEND:
		err = nsim_fib6_rt_append(data, fen6_info);
		break;
	case FIB_EVENT_ENTRY_DEL:
		nsim_fib6_rt_remove(data, fen6_info);
		break;
	default:
		break;
	}

	return err;
}

static int nsim_fib_event(struct nsim_fib_data *data,
			  struct fib_notifier_info *info, unsigned long event)
{
	int err = 0;

	switch (info->family) {
	case AF_INET:
		err = nsim_fib4_event(data, info, event);
		break;
	case AF_INET6:
		err = nsim_fib6_event(data, info, event);
		break;
	}

	return err;
}

static int nsim_fib_event_nb(struct notifier_block *nb, unsigned long event,
			     void *ptr)
{
	struct nsim_fib_data *data = container_of(nb, struct nsim_fib_data,
						  fib_nb);
	struct fib_notifier_info *info = ptr;
	int err = 0;

	/* IPv6 routes can be added via RAs from softIRQ. */
	spin_lock_bh(&data->fib_lock);

	switch (event) {
	case FIB_EVENT_RULE_ADD:
	case FIB_EVENT_RULE_DEL:
		err = nsim_fib_rule_event(data, info,
					  event == FIB_EVENT_RULE_ADD);
		break;

	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_APPEND:
	case FIB_EVENT_ENTRY_DEL:
		err = nsim_fib_event(data, info, event);
		break;
	}

	spin_unlock_bh(&data->fib_lock);

	return notifier_from_errno(err);
}

static void nsim_fib4_rt_free(struct nsim_fib_rt *fib_rt,
			      struct nsim_fib_data *data)
{
	struct devlink *devlink = data->devlink;
	struct nsim_fib4_rt *fib4_rt;

	fib4_rt = container_of(fib_rt, struct nsim_fib4_rt, common);
	nsim_fib4_rt_hw_flags_set(devlink_net(devlink), fib4_rt, false);
	nsim_fib_account(&data->ipv4.fib, false, NULL);
	nsim_fib4_rt_destroy(fib4_rt);
}

static void nsim_fib6_rt_free(struct nsim_fib_rt *fib_rt,
			      struct nsim_fib_data *data)
{
	struct nsim_fib6_rt *fib6_rt;

	fib6_rt = container_of(fib_rt, struct nsim_fib6_rt, common);
	nsim_fib6_rt_hw_flags_set(fib6_rt, false);
	nsim_fib_account(&data->ipv6.fib, false, NULL);
	nsim_fib6_rt_destroy(fib6_rt);
}

static void nsim_fib_rt_free(void *ptr, void *arg)
{
	struct nsim_fib_rt *fib_rt = ptr;
	struct nsim_fib_data *data = arg;

	switch (fib_rt->key.family) {
	case AF_INET:
		nsim_fib4_rt_free(fib_rt, data);
		break;
	case AF_INET6:
		nsim_fib6_rt_free(fib_rt, data);
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

/* inconsistent dump, trying again */
static void nsim_fib_dump_inconsistent(struct notifier_block *nb)
{
	struct nsim_fib_data *data = container_of(nb, struct nsim_fib_data,
						  fib_nb);
	struct nsim_fib_rt *fib_rt, *fib_rt_tmp;

	/* The notifier block is still not registered, so we do not need to
	 * take any locks here.
	 */
	list_for_each_entry_safe(fib_rt, fib_rt_tmp, &data->fib_rt_list, list) {
		rhashtable_remove_fast(&data->fib_rt_ht, &fib_rt->ht_node,
				       nsim_fib_rt_ht_params);
		nsim_fib_rt_free(fib_rt, data);
	}

	data->ipv4.rules.num = 0ULL;
	data->ipv6.rules.num = 0ULL;
}

static struct nsim_nexthop *nsim_nexthop_create(struct nsim_fib_data *data,
						struct nh_notifier_info *info)
{
	struct nsim_nexthop *nexthop;
	u64 occ = 0;
	int i;

	nexthop = kzalloc(sizeof(*nexthop), GFP_KERNEL);
	if (!nexthop)
		return NULL;

	nexthop->id = info->id;

	/* Determine the number of nexthop entries the new nexthop will
	 * occupy.
	 */

	if (!info->is_grp) {
		occ = 1;
		goto out;
	}

	for (i = 0; i < info->nh_grp->num_nh; i++)
		occ += info->nh_grp->nh_entries[i].weight;

out:
	nexthop->occ = occ;
	return nexthop;
}

static void nsim_nexthop_destroy(struct nsim_nexthop *nexthop)
{
	kfree(nexthop);
}

static int nsim_nexthop_account(struct nsim_fib_data *data, u64 occ,
				bool add, struct netlink_ext_ack *extack)
{
	int err = 0;

	if (add) {
		if (data->nexthops.num + occ <= data->nexthops.max) {
			data->nexthops.num += occ;
		} else {
			err = -ENOSPC;
			NL_SET_ERR_MSG_MOD(extack, "Exceeded number of supported nexthops");
		}
	} else {
		if (WARN_ON(occ > data->nexthops.num))
			return -EINVAL;
		data->nexthops.num -= occ;
	}

	return err;
}

static int nsim_nexthop_add(struct nsim_fib_data *data,
			    struct nsim_nexthop *nexthop,
			    struct netlink_ext_ack *extack)
{
	struct net *net = devlink_net(data->devlink);
	int err;

	err = nsim_nexthop_account(data, nexthop->occ, true, extack);
	if (err)
		return err;

	err = rhashtable_insert_fast(&data->nexthop_ht, &nexthop->ht_node,
				     nsim_nexthop_ht_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to insert nexthop");
		goto err_nexthop_dismiss;
	}

	nexthop_set_hw_flags(net, nexthop->id, false, true);

	return 0;

err_nexthop_dismiss:
	nsim_nexthop_account(data, nexthop->occ, false, extack);
	return err;
}

static int nsim_nexthop_replace(struct nsim_fib_data *data,
				struct nsim_nexthop *nexthop,
				struct nsim_nexthop *nexthop_old,
				struct netlink_ext_ack *extack)
{
	struct net *net = devlink_net(data->devlink);
	int err;

	err = nsim_nexthop_account(data, nexthop->occ, true, extack);
	if (err)
		return err;

	err = rhashtable_replace_fast(&data->nexthop_ht,
				      &nexthop_old->ht_node, &nexthop->ht_node,
				      nsim_nexthop_ht_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to replace nexthop");
		goto err_nexthop_dismiss;
	}

	nexthop_set_hw_flags(net, nexthop->id, false, true);
	nsim_nexthop_account(data, nexthop_old->occ, false, extack);
	nsim_nexthop_destroy(nexthop_old);

	return 0;

err_nexthop_dismiss:
	nsim_nexthop_account(data, nexthop->occ, false, extack);
	return err;
}

static int nsim_nexthop_insert(struct nsim_fib_data *data,
			       struct nh_notifier_info *info)
{
	struct nsim_nexthop *nexthop, *nexthop_old;
	int err;

	nexthop = nsim_nexthop_create(data, info);
	if (!nexthop)
		return -ENOMEM;

	nexthop_old = rhashtable_lookup_fast(&data->nexthop_ht, &info->id,
					     nsim_nexthop_ht_params);
	if (!nexthop_old)
		err = nsim_nexthop_add(data, nexthop, info->extack);
	else
		err = nsim_nexthop_replace(data, nexthop, nexthop_old,
					   info->extack);

	if (err)
		nsim_nexthop_destroy(nexthop);

	return err;
}

static void nsim_nexthop_remove(struct nsim_fib_data *data,
				struct nh_notifier_info *info)
{
	struct nsim_nexthop *nexthop;

	nexthop = rhashtable_lookup_fast(&data->nexthop_ht, &info->id,
					 nsim_nexthop_ht_params);
	if (!nexthop)
		return;

	rhashtable_remove_fast(&data->nexthop_ht, &nexthop->ht_node,
			       nsim_nexthop_ht_params);
	nsim_nexthop_account(data, nexthop->occ, false, info->extack);
	nsim_nexthop_destroy(nexthop);
}

static int nsim_nexthop_event_nb(struct notifier_block *nb, unsigned long event,
				 void *ptr)
{
	struct nsim_fib_data *data = container_of(nb, struct nsim_fib_data,
						  nexthop_nb);
	struct nh_notifier_info *info = ptr;
	int err = 0;

	ASSERT_RTNL();

	switch (event) {
	case NEXTHOP_EVENT_REPLACE:
		err = nsim_nexthop_insert(data, info);
		break;
	case NEXTHOP_EVENT_DEL:
		nsim_nexthop_remove(data, info);
		break;
	default:
		break;
	}

	return notifier_from_errno(err);
}

static void nsim_nexthop_free(void *ptr, void *arg)
{
	struct nsim_nexthop *nexthop = ptr;
	struct nsim_fib_data *data = arg;
	struct net *net;

	net = devlink_net(data->devlink);
	nexthop_set_hw_flags(net, nexthop->id, false, false);
	nsim_nexthop_account(data, nexthop->occ, false, NULL);
	nsim_nexthop_destroy(nexthop);
}

static u64 nsim_fib_ipv4_resource_occ_get(void *priv)
{
	struct nsim_fib_data *data = priv;

	return nsim_fib_get_val(data, NSIM_RESOURCE_IPV4_FIB, false);
}

static u64 nsim_fib_ipv4_rules_res_occ_get(void *priv)
{
	struct nsim_fib_data *data = priv;

	return nsim_fib_get_val(data, NSIM_RESOURCE_IPV4_FIB_RULES, false);
}

static u64 nsim_fib_ipv6_resource_occ_get(void *priv)
{
	struct nsim_fib_data *data = priv;

	return nsim_fib_get_val(data, NSIM_RESOURCE_IPV6_FIB, false);
}

static u64 nsim_fib_ipv6_rules_res_occ_get(void *priv)
{
	struct nsim_fib_data *data = priv;

	return nsim_fib_get_val(data, NSIM_RESOURCE_IPV6_FIB_RULES, false);
}

static u64 nsim_fib_nexthops_res_occ_get(void *priv)
{
	struct nsim_fib_data *data = priv;

	return nsim_fib_get_val(data, NSIM_RESOURCE_NEXTHOPS, false);
}

static void nsim_fib_set_max_all(struct nsim_fib_data *data,
				 struct devlink *devlink)
{
	enum nsim_resource_id res_ids[] = {
		NSIM_RESOURCE_IPV4_FIB, NSIM_RESOURCE_IPV4_FIB_RULES,
		NSIM_RESOURCE_IPV6_FIB, NSIM_RESOURCE_IPV6_FIB_RULES,
		NSIM_RESOURCE_NEXTHOPS,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(res_ids); i++) {
		int err;
		u64 val;

		err = devlink_resource_size_get(devlink, res_ids[i], &val);
		if (err)
			val = (u64) -1;
		nsim_fib_set_max(data, res_ids[i], val);
	}
}

struct nsim_fib_data *nsim_fib_create(struct devlink *devlink,
				      struct netlink_ext_ack *extack)
{
	struct nsim_fib_data *data;
	int err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);
	data->devlink = devlink;

	err = rhashtable_init(&data->nexthop_ht, &nsim_nexthop_ht_params);
	if (err)
		goto err_data_free;

	spin_lock_init(&data->fib_lock);
	INIT_LIST_HEAD(&data->fib_rt_list);
	err = rhashtable_init(&data->fib_rt_ht, &nsim_fib_rt_ht_params);
	if (err)
		goto err_rhashtable_nexthop_destroy;

	nsim_fib_set_max_all(data, devlink);

	data->nexthop_nb.notifier_call = nsim_nexthop_event_nb;
	err = register_nexthop_notifier(devlink_net(devlink), &data->nexthop_nb,
					extack);
	if (err) {
		pr_err("Failed to register nexthop notifier\n");
		goto err_rhashtable_fib_destroy;
	}

	data->fib_nb.notifier_call = nsim_fib_event_nb;
	err = register_fib_notifier(devlink_net(devlink), &data->fib_nb,
				    nsim_fib_dump_inconsistent, extack);
	if (err) {
		pr_err("Failed to register fib notifier\n");
		goto err_nexthop_nb_unregister;
	}

	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_IPV4_FIB,
					  nsim_fib_ipv4_resource_occ_get,
					  data);
	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_IPV4_FIB_RULES,
					  nsim_fib_ipv4_rules_res_occ_get,
					  data);
	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_IPV6_FIB,
					  nsim_fib_ipv6_resource_occ_get,
					  data);
	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_IPV6_FIB_RULES,
					  nsim_fib_ipv6_rules_res_occ_get,
					  data);
	devlink_resource_occ_get_register(devlink,
					  NSIM_RESOURCE_NEXTHOPS,
					  nsim_fib_nexthops_res_occ_get,
					  data);
	return data;

err_nexthop_nb_unregister:
	unregister_nexthop_notifier(devlink_net(devlink), &data->nexthop_nb);
err_rhashtable_fib_destroy:
	rhashtable_free_and_destroy(&data->fib_rt_ht, nsim_fib_rt_free,
				    data);
err_rhashtable_nexthop_destroy:
	rhashtable_free_and_destroy(&data->nexthop_ht, nsim_nexthop_free,
				    data);
err_data_free:
	kfree(data);
	return ERR_PTR(err);
}

void nsim_fib_destroy(struct devlink *devlink, struct nsim_fib_data *data)
{
	devlink_resource_occ_get_unregister(devlink,
					    NSIM_RESOURCE_NEXTHOPS);
	devlink_resource_occ_get_unregister(devlink,
					    NSIM_RESOURCE_IPV6_FIB_RULES);
	devlink_resource_occ_get_unregister(devlink,
					    NSIM_RESOURCE_IPV6_FIB);
	devlink_resource_occ_get_unregister(devlink,
					    NSIM_RESOURCE_IPV4_FIB_RULES);
	devlink_resource_occ_get_unregister(devlink,
					    NSIM_RESOURCE_IPV4_FIB);
	unregister_fib_notifier(devlink_net(devlink), &data->fib_nb);
	unregister_nexthop_notifier(devlink_net(devlink), &data->nexthop_nb);
	rhashtable_free_and_destroy(&data->fib_rt_ht, nsim_fib_rt_free,
				    data);
	rhashtable_free_and_destroy(&data->nexthop_ht, nsim_nexthop_free,
				    data);
	WARN_ON_ONCE(!list_empty(&data->fib_rt_list));
	kfree(data);
}
