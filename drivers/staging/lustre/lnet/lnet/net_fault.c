/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2014, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Seagate, Inc.
 *
 * lnet/lnet/net_fault.c
 *
 * Lustre network fault simulation
 *
 * Author: liang.zhen@intel.com
 */

#define DEBUG_SUBSYSTEM S_LNET

#include "../../include/linux/lnet/lib-lnet.h"
#include "../../include/linux/lnet/lnetctl.h"

#define LNET_MSG_MASK		(LNET_PUT_BIT | LNET_ACK_BIT | \
				 LNET_GET_BIT | LNET_REPLY_BIT)

struct lnet_drop_rule {
	/** link chain on the_lnet.ln_drop_rules */
	struct list_head	dr_link;
	/** attributes of this rule */
	struct lnet_fault_attr	dr_attr;
	/** lock to protect \a dr_drop_at and \a dr_stat */
	spinlock_t		dr_lock;
	/**
	 * the message sequence to drop, which means message is dropped when
	 * dr_stat.drs_count == dr_drop_at
	 */
	unsigned long		dr_drop_at;
	/**
	 * seconds to drop the next message, it's exclusive with dr_drop_at
	 */
	unsigned long		dr_drop_time;
	/** baseline to caculate dr_drop_time */
	unsigned long		dr_time_base;
	/** statistic of dropped messages */
	struct lnet_fault_stat	dr_stat;
};

static bool
lnet_fault_nid_match(lnet_nid_t nid, lnet_nid_t msg_nid)
{
	if (nid == msg_nid || nid == LNET_NID_ANY)
		return true;

	if (LNET_NIDNET(nid) != LNET_NIDNET(msg_nid))
		return false;

	/* 255.255.255.255@net is wildcard for all addresses in a network */
	return LNET_NIDADDR(nid) == LNET_NIDADDR(LNET_NID_ANY);
}

static bool
lnet_fault_attr_match(struct lnet_fault_attr *attr, lnet_nid_t src,
		      lnet_nid_t dst, unsigned int type, unsigned int portal)
{
	if (!lnet_fault_nid_match(attr->fa_src, src) ||
	    !lnet_fault_nid_match(attr->fa_dst, dst))
		return false;

	if (!(attr->fa_msg_mask & (1 << type)))
		return false;

	/**
	 * NB: ACK and REPLY have no portal, but they should have been
	 * rejected by message mask
	 */
	if (attr->fa_ptl_mask && /* has portal filter */
	    !(attr->fa_ptl_mask & (1ULL << portal)))
		return false;

	return true;
}

static int
lnet_fault_attr_validate(struct lnet_fault_attr *attr)
{
	if (!attr->fa_msg_mask)
		attr->fa_msg_mask = LNET_MSG_MASK; /* all message types */

	if (!attr->fa_ptl_mask) /* no portal filter */
		return 0;

	/* NB: only PUT and GET can be filtered if portal filter has been set */
	attr->fa_msg_mask &= LNET_GET_BIT | LNET_PUT_BIT;
	if (!attr->fa_msg_mask) {
		CDEBUG(D_NET, "can't find valid message type bits %x\n",
		       attr->fa_msg_mask);
		return -EINVAL;
	}
	return 0;
}

static void
lnet_fault_stat_inc(struct lnet_fault_stat *stat, unsigned int type)
{
	/* NB: fs_counter is NOT updated by this function */
	switch (type) {
	case LNET_MSG_PUT:
		stat->fs_put++;
		return;
	case LNET_MSG_ACK:
		stat->fs_ack++;
		return;
	case LNET_MSG_GET:
		stat->fs_get++;
		return;
	case LNET_MSG_REPLY:
		stat->fs_reply++;
		return;
	}
}

/**
 * Add a new drop rule to LNet
 * There is no check for duplicated drop rule, all rules will be checked for
 * incoming message.
 */
static int
lnet_drop_rule_add(struct lnet_fault_attr *attr)
{
	struct lnet_drop_rule *rule;

	if (!attr->u.drop.da_rate == !attr->u.drop.da_interval) {
		CDEBUG(D_NET, "invalid rate %d or interval %d\n",
		       attr->u.drop.da_rate, attr->u.drop.da_interval);
		return -EINVAL;
	}

	if (lnet_fault_attr_validate(attr))
		return -EINVAL;

	CFS_ALLOC_PTR(rule);
	if (!rule)
		return -ENOMEM;

	spin_lock_init(&rule->dr_lock);

	rule->dr_attr = *attr;
	if (attr->u.drop.da_interval) {
		rule->dr_time_base = cfs_time_shift(attr->u.drop.da_interval);
		rule->dr_drop_time = cfs_time_shift(cfs_rand() %
						    attr->u.drop.da_interval);
	} else {
		rule->dr_drop_at = cfs_rand() % attr->u.drop.da_rate;
	}

	lnet_net_lock(LNET_LOCK_EX);
	list_add(&rule->dr_link, &the_lnet.ln_drop_rules);
	lnet_net_unlock(LNET_LOCK_EX);

	CDEBUG(D_NET, "Added drop rule: src %s, dst %s, rate %d, interval %d\n",
	       libcfs_nid2str(attr->fa_src), libcfs_nid2str(attr->fa_src),
	       attr->u.drop.da_rate, attr->u.drop.da_interval);
	return 0;
}

/**
 * Remove matched drop rules from lnet, all rules that can match \a src and
 * \a dst will be removed.
 * If \a src is zero, then all rules have \a dst as destination will be remove
 * If \a dst is zero, then all rules have \a src as source will be removed
 * If both of them are zero, all rules will be removed
 */
static int
lnet_drop_rule_del(lnet_nid_t src, lnet_nid_t dst)
{
	struct lnet_drop_rule *rule;
	struct lnet_drop_rule *tmp;
	struct list_head zombies;
	int n = 0;

	INIT_LIST_HEAD(&zombies);

	lnet_net_lock(LNET_LOCK_EX);
	list_for_each_entry_safe(rule, tmp, &the_lnet.ln_drop_rules, dr_link) {
		if (rule->dr_attr.fa_src != src && src)
			continue;

		if (rule->dr_attr.fa_dst != dst && dst)
			continue;

		list_move(&rule->dr_link, &zombies);
	}
	lnet_net_unlock(LNET_LOCK_EX);

	list_for_each_entry_safe(rule, tmp, &zombies, dr_link) {
		CDEBUG(D_NET, "Remove drop rule: src %s->dst: %s (1/%d, %d)\n",
		       libcfs_nid2str(rule->dr_attr.fa_src),
		       libcfs_nid2str(rule->dr_attr.fa_dst),
		       rule->dr_attr.u.drop.da_rate,
		       rule->dr_attr.u.drop.da_interval);

		list_del(&rule->dr_link);
		CFS_FREE_PTR(rule);
		n++;
	}

	return n;
}

/**
 * List drop rule at position of \a pos
 */
static int
lnet_drop_rule_list(int pos, struct lnet_fault_attr *attr,
		    struct lnet_fault_stat *stat)
{
	struct lnet_drop_rule *rule;
	int cpt;
	int i = 0;
	int rc = -ENOENT;

	cpt = lnet_net_lock_current();
	list_for_each_entry(rule, &the_lnet.ln_drop_rules, dr_link) {
		if (i++ < pos)
			continue;

		spin_lock(&rule->dr_lock);
		*attr = rule->dr_attr;
		*stat = rule->dr_stat;
		spin_unlock(&rule->dr_lock);
		rc = 0;
		break;
	}

	lnet_net_unlock(cpt);
	return rc;
}

/**
 * reset counters for all drop rules
 */
static void
lnet_drop_rule_reset(void)
{
	struct lnet_drop_rule *rule;
	int cpt;

	cpt = lnet_net_lock_current();

	list_for_each_entry(rule, &the_lnet.ln_drop_rules, dr_link) {
		struct lnet_fault_attr *attr = &rule->dr_attr;

		spin_lock(&rule->dr_lock);

		memset(&rule->dr_stat, 0, sizeof(rule->dr_stat));
		if (attr->u.drop.da_rate) {
			rule->dr_drop_at = cfs_rand() % attr->u.drop.da_rate;
		} else {
			rule->dr_drop_time = cfs_time_shift(cfs_rand() %
						attr->u.drop.da_interval);
			rule->dr_time_base = cfs_time_shift(attr->u.drop.da_interval);
		}
		spin_unlock(&rule->dr_lock);
	}

	lnet_net_unlock(cpt);
}

/**
 * check source/destination NID, portal, message type and drop rate,
 * decide whether should drop this message or not
 */
static bool
drop_rule_match(struct lnet_drop_rule *rule, lnet_nid_t src,
		lnet_nid_t dst, unsigned int type, unsigned int portal)
{
	struct lnet_fault_attr *attr = &rule->dr_attr;
	bool drop;

	if (!lnet_fault_attr_match(attr, src, dst, type, portal))
		return false;

	/* match this rule, check drop rate now */
	spin_lock(&rule->dr_lock);
	if (rule->dr_drop_time) { /* time based drop */
		unsigned long now = cfs_time_current();

		rule->dr_stat.fs_count++;
		drop = cfs_time_aftereq(now, rule->dr_drop_time);
		if (drop) {
			if (cfs_time_after(now, rule->dr_time_base))
				rule->dr_time_base = now;

			rule->dr_drop_time = rule->dr_time_base +
					     cfs_time_seconds(cfs_rand() %
						attr->u.drop.da_interval);
			rule->dr_time_base += cfs_time_seconds(attr->u.drop.da_interval);

			CDEBUG(D_NET, "Drop Rule %s->%s: next drop : %lu\n",
			       libcfs_nid2str(attr->fa_src),
			       libcfs_nid2str(attr->fa_dst),
			       rule->dr_drop_time);
		}

	} else { /* rate based drop */
		drop = rule->dr_stat.fs_count++ == rule->dr_drop_at;

		if (!do_div(rule->dr_stat.fs_count, attr->u.drop.da_rate)) {
			rule->dr_drop_at = rule->dr_stat.fs_count +
					   cfs_rand() % attr->u.drop.da_rate;
			CDEBUG(D_NET, "Drop Rule %s->%s: next drop: %lu\n",
			       libcfs_nid2str(attr->fa_src),
			       libcfs_nid2str(attr->fa_dst), rule->dr_drop_at);
		}
	}

	if (drop) { /* drop this message, update counters */
		lnet_fault_stat_inc(&rule->dr_stat, type);
		rule->dr_stat.u.drop.ds_dropped++;
	}

	spin_unlock(&rule->dr_lock);
	return drop;
}

/**
 * Check if message from \a src to \a dst can match any existed drop rule
 */
bool
lnet_drop_rule_match(lnet_hdr_t *hdr)
{
	struct lnet_drop_rule *rule;
	lnet_nid_t src = le64_to_cpu(hdr->src_nid);
	lnet_nid_t dst = le64_to_cpu(hdr->dest_nid);
	unsigned int typ = le32_to_cpu(hdr->type);
	unsigned int ptl = -1;
	bool drop = false;
	int cpt;

	/**
	 * NB: if Portal is specified, then only PUT and GET will be
	 * filtered by drop rule
	 */
	if (typ == LNET_MSG_PUT)
		ptl = le32_to_cpu(hdr->msg.put.ptl_index);
	else if (typ == LNET_MSG_GET)
		ptl = le32_to_cpu(hdr->msg.get.ptl_index);

	cpt = lnet_net_lock_current();
	list_for_each_entry(rule, &the_lnet.ln_drop_rules, dr_link) {
		drop = drop_rule_match(rule, src, dst, typ, ptl);
		if (drop)
			break;
	}

	lnet_net_unlock(cpt);
	return drop;
}

int
lnet_fault_ctl(int opc, struct libcfs_ioctl_data *data)
{
	struct lnet_fault_attr *attr;
	struct lnet_fault_stat *stat;

	attr = (struct lnet_fault_attr *)data->ioc_inlbuf1;

	switch (opc) {
	default:
		return -EINVAL;

	case LNET_CTL_DROP_ADD:
		if (!attr)
			return -EINVAL;

		return lnet_drop_rule_add(attr);

	case LNET_CTL_DROP_DEL:
		if (!attr)
			return -EINVAL;

		data->ioc_count = lnet_drop_rule_del(attr->fa_src,
						     attr->fa_dst);
		return 0;

	case LNET_CTL_DROP_RESET:
		lnet_drop_rule_reset();
		return 0;

	case LNET_CTL_DROP_LIST:
		stat = (struct lnet_fault_stat *)data->ioc_inlbuf2;
		if (!attr || !stat)
			return -EINVAL;

		return lnet_drop_rule_list(data->ioc_count, attr, stat);
	}
}

int
lnet_fault_init(void)
{
	CLASSERT(LNET_PUT_BIT == 1 << LNET_MSG_PUT);
	CLASSERT(LNET_ACK_BIT == 1 << LNET_MSG_ACK);
	CLASSERT(LNET_GET_BIT == 1 << LNET_MSG_GET);
	CLASSERT(LNET_REPLY_BIT == 1 << LNET_MSG_REPLY);

	return 0;
}

void
lnet_fault_fini(void)
{
	lnet_drop_rule_del(0, 0);

	LASSERT(list_empty(&the_lnet.ln_drop_rules));
}
