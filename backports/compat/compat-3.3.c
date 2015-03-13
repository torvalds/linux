/*
 * Copyright 2012  Luis R. Rodriguez <mcgrof@frijolero.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Backport functionality introduced in Linux 3.3.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <net/dst.h>
#include <net/xfrm.h>

static void __copy_skb_header(struct sk_buff *new, const struct sk_buff *old)
{
	new->tstamp		= old->tstamp;
	new->dev		= old->dev;
	new->transport_header	= old->transport_header;
	new->network_header	= old->network_header;
	new->mac_header		= old->mac_header;
	skb_dst_copy(new, old);
	new->rxhash		= old->rxhash;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
	new->ooo_okay		= old->ooo_okay;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	new->l4_rxhash		= old->l4_rxhash;
#endif
#ifdef CONFIG_XFRM
	new->sp			= secpath_get(old->sp);
#endif
	memcpy(new->cb, old->cb, sizeof(old->cb));
	new->csum		= old->csum;
	new->local_df		= old->local_df;
	new->pkt_type		= old->pkt_type;
	new->ip_summed		= old->ip_summed;
	skb_copy_queue_mapping(new, old);
	new->priority		= old->priority;
#if IS_ENABLED(CONFIG_IP_VS)
	new->ipvs_property	= old->ipvs_property;
#endif
	new->protocol		= old->protocol;
	new->mark		= old->mark;
	new->skb_iif		= old->skb_iif;
	__nf_copy(new, old);
#if IS_ENABLED(CONFIG_NETFILTER_XT_TARGET_TRACE)
	new->nf_trace		= old->nf_trace;
#endif
#ifdef CONFIG_NET_SCHED
	new->tc_index		= old->tc_index;
#ifdef CONFIG_NET_CLS_ACT
	new->tc_verd		= old->tc_verd;
#endif
#endif
	new->vlan_tci		= old->vlan_tci;

	skb_copy_secmark(new, old);
}

static void copy_skb_header(struct sk_buff *new, const struct sk_buff *old)
{
#ifndef NET_SKBUFF_DATA_USES_OFFSET
	/*
	 *	Shift between the two data areas in bytes
	 */
	unsigned long offset = new->data - old->data;
#endif

	__copy_skb_header(new, old);

#ifndef NET_SKBUFF_DATA_USES_OFFSET
	/* {transport,network,mac}_header are relative to skb->head */
	new->transport_header += offset;
	new->network_header   += offset;
	if (skb_mac_header_was_set(new))
		new->mac_header	      += offset;
#endif
	skb_shinfo(new)->gso_size = skb_shinfo(old)->gso_size;
	skb_shinfo(new)->gso_segs = skb_shinfo(old)->gso_segs;
	skb_shinfo(new)->gso_type = skb_shinfo(old)->gso_type;
}

static void skb_clone_fraglist(struct sk_buff *skb)
{
	struct sk_buff *list;

	skb_walk_frags(skb, list)
		skb_get(list);
}


/**
 *	__pskb_copy	-	create copy of an sk_buff with private head.
 *	@skb: buffer to copy
 *	@headroom: headroom of new skb
 *	@gfp_mask: allocation priority
 *
 *	Make a copy of both an &sk_buff and part of its data, located
 *	in header. Fragmented data remain shared. This is used when
 *	the caller wishes to modify only header of &sk_buff and needs
 *	private copy of the header to alter. Returns %NULL on failure
 *	or the pointer to the buffer on success.
 *	The returned buffer has a reference count of 1.
 */

struct sk_buff *__pskb_copy(struct sk_buff *skb, int headroom, gfp_t gfp_mask)
{
	unsigned int size = skb_headlen(skb) + headroom;
	struct sk_buff *n = alloc_skb(size, gfp_mask);

	if (!n)
		goto out;

	/* Set the data pointer */
	skb_reserve(n, headroom);
	/* Set the tail pointer and length */
	skb_put(n, skb_headlen(skb));
	/* Copy the bytes */
	skb_copy_from_linear_data(skb, n->data, n->len);

	n->truesize += skb->data_len;
	n->data_len  = skb->data_len;
	n->len	     = skb->len;

	if (skb_shinfo(skb)->nr_frags) {
		int i;

/*
 * SKBTX_DEV_ZEROCOPY was added on 3.1 as well but requires ubuf
 * stuff added to the skb which we do not have
 */
#if 0
		if (skb_shinfo(skb)->tx_flags & SKBTX_DEV_ZEROCOPY) {
			if (skb_copy_ubufs(skb, gfp_mask)) {
				kfree_skb(n);
				n = NULL;
				goto out;
			}
		}
#endif
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_shinfo(n)->frags[i] = skb_shinfo(skb)->frags[i];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
			skb_frag_ref(skb, i);
#else
			get_page(skb_shinfo(skb)->frags[i].page);
#endif
		}
		skb_shinfo(n)->nr_frags = i;
	}

	if (skb_has_frag_list(skb)) {
		skb_shinfo(n)->frag_list = skb_shinfo(skb)->frag_list;
		skb_clone_fraglist(n);
	}

	copy_skb_header(n, skb);
out:
	return n;
}
EXPORT_SYMBOL_GPL(__pskb_copy);

static DEFINE_SPINLOCK(wq_name_lock);
static LIST_HEAD(wq_name_list);

struct wq_name {
	struct list_head list;
	struct workqueue_struct *wq;
	char name[24];
};

struct workqueue_struct *
backport_alloc_workqueue(const char *fmt, unsigned int flags,
			 int max_active, struct lock_class_key *key,
			 const char *lock_name, ...)
{
	struct workqueue_struct *wq;
	struct wq_name *n = kzalloc(sizeof(*n), GFP_KERNEL);
	va_list args;

	if (!n)
		return NULL;

	va_start(args, lock_name);
	vsnprintf(n->name, sizeof(n->name), fmt, args);
	va_end(args);

	wq = __alloc_workqueue_key(n->name, flags, max_active, key, lock_name);
	if (!wq) {
		kfree(n);
		return NULL;
	}

	n->wq = wq;
	spin_lock(&wq_name_lock);
	list_add(&n->list, &wq_name_list);
	spin_unlock(&wq_name_lock);

	return wq;
}
EXPORT_SYMBOL_GPL(backport_alloc_workqueue);

void backport_destroy_workqueue(struct workqueue_struct *wq)
{
	struct wq_name *n, *tmp;

	/* call original */
#undef destroy_workqueue
	destroy_workqueue(wq);

	spin_lock(&wq_name_lock);
	list_for_each_entry_safe(n, tmp, &wq_name_list, list) {
		if (n->wq == wq) {
			list_del(&n->list);
			kfree(n);
			break;
		}
	}
	spin_unlock(&wq_name_lock);
}
EXPORT_SYMBOL_GPL(backport_destroy_workqueue);

void genl_notify(struct sk_buff *skb, struct net *net, u32 pid, u32 group,
		 struct nlmsghdr *nlh, gfp_t flags)
{
	struct sock *sk = net->genl_sock;
	int report = 0;

	if (nlh)
		report = nlmsg_report(nlh);

	nlmsg_notify(sk, skb, pid, group, report, flags);
}
EXPORT_SYMBOL_GPL(genl_notify);
