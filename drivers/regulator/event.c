// SPDX-License-Identifier: GPL-2.0
/*
 * Regulator event over netlink
 *
 * Author: Naresh Solanki <Naresh.Solanki@9elements.com>
 */

#include <regulator/regulator.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/atomic.h>

#include "regnl.h"

static atomic_t reg_event_seqnum = ATOMIC_INIT(0);

static const struct genl_multicast_group reg_event_mcgrps[] = {
	{ .name = REG_GENL_MCAST_GROUP_NAME, },
};

static struct genl_family reg_event_genl_family __ro_after_init = {
	.module = THIS_MODULE,
	.name = REG_GENL_FAMILY_NAME,
	.version = REG_GENL_VERSION,
	.maxattr = REG_GENL_ATTR_MAX,
	.mcgrps = reg_event_mcgrps,
	.n_mcgrps = ARRAY_SIZE(reg_event_mcgrps),
};

int reg_generate_netlink_event(const char *reg_name, u64 event)
{
	struct sk_buff *skb;
	struct nlattr *attr;
	struct reg_genl_event *edata;
	void *msg_header;
	int size;

	/* allocate memory */
	size = nla_total_size(sizeof(struct reg_genl_event)) +
	    nla_total_size(0);

	skb = genlmsg_new(size, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	/* add the genetlink message header */
	msg_header = genlmsg_put(skb, 0, atomic_inc_return(&reg_event_seqnum),
				 &reg_event_genl_family, 0, REG_GENL_CMD_EVENT);
	if (!msg_header) {
		nlmsg_free(skb);
		return -ENOMEM;
	}

	/* fill the data */
	attr = nla_reserve(skb, REG_GENL_ATTR_EVENT, sizeof(struct reg_genl_event));
	if (!attr) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	edata = nla_data(attr);
	memset(edata, 0, sizeof(struct reg_genl_event));

	strscpy(edata->reg_name, reg_name, sizeof(edata->reg_name));
	edata->event = event;

	/* send multicast genetlink message */
	genlmsg_end(skb, msg_header);
	size = genlmsg_multicast(&reg_event_genl_family, skb, 0, 0, GFP_ATOMIC);

	return size;
}

static int __init reg_event_genetlink_init(void)
{
	return genl_register_family(&reg_event_genl_family);
}

static int __init reg_event_init(void)
{
	int error;

	/* create genetlink for acpi event */
	error = reg_event_genetlink_init();
	if (error)
		pr_warn("Failed to create genetlink family for reg event\n");

	return 0;
}

fs_initcall(reg_event_init);
