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

#include <net/fib_notifier.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/fib_rules.h>

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
	default:
		return 0;
	}

	return max ? entry->max : entry->num;
}

int nsim_fib_set_max(struct nsim_fib_data *fib_data,
		     enum nsim_resource_id res_id, u64 val,
		     struct netlink_ext_ack *extack)
{
	struct nsim_fib_entry *entry;
	int err = 0;

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
	default:
		return 0;
	}

	/* not allowing a new max to be less than curren occupancy
	 * --> no means of evicting entries
	 */
	if (val < entry->num) {
		NL_SET_ERR_MSG_MOD(extack, "New size is less than current occupancy");
		err = -EINVAL;
	} else {
		entry->max = val;
	}

	return err;
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

static int nsim_fib_event(struct nsim_fib_data *data,
			  struct fib_notifier_info *info, bool add)
{
	struct netlink_ext_ack *extack = info->extack;
	int err = 0;

	switch (info->family) {
	case AF_INET:
		err = nsim_fib_account(&data->ipv4.fib, add, extack);
		break;
	case AF_INET6:
		err = nsim_fib_account(&data->ipv6.fib, add, extack);
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

	switch (event) {
	case FIB_EVENT_RULE_ADD: /* fall through */
	case FIB_EVENT_RULE_DEL:
		err = nsim_fib_rule_event(data, info,
					  event == FIB_EVENT_RULE_ADD);
		break;

	case FIB_EVENT_ENTRY_ADD:  /* fall through */
	case FIB_EVENT_ENTRY_DEL:
		err = nsim_fib_event(data, info,
				     event == FIB_EVENT_ENTRY_ADD);
		break;
	}

	return notifier_from_errno(err);
}

/* inconsistent dump, trying again */
static void nsim_fib_dump_inconsistent(struct notifier_block *nb)
{
	struct nsim_fib_data *data = container_of(nb, struct nsim_fib_data,
						  fib_nb);

	data->ipv4.fib.num = 0ULL;
	data->ipv4.rules.num = 0ULL;
	data->ipv6.fib.num = 0ULL;
	data->ipv6.rules.num = 0ULL;
}

struct nsim_fib_data *nsim_fib_create(void)
{
	struct nsim_fib_data *data;
	int err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	data->ipv4.fib.max = (u64)-1;
	data->ipv4.rules.max = (u64)-1;

	data->ipv6.fib.max = (u64)-1;
	data->ipv6.rules.max = (u64)-1;

	data->fib_nb.notifier_call = nsim_fib_event_nb;
	err = register_fib_notifier(&data->fib_nb, nsim_fib_dump_inconsistent);
	if (err) {
		pr_err("Failed to register fib notifier\n");
		goto err_out;
	}

	return data;

err_out:
	kfree(data);
	return ERR_PTR(err);
}

void nsim_fib_destroy(struct nsim_fib_data *data)
{
	unregister_fib_notifier(&data->fib_nb);
	kfree(data);
}
