// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/net/bond/bond_netlink.c - Netlink interface for bonding
 * Copyright (c) 2013 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2013 Scott Feldman <sfeldma@cumulusnetworks.com>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <net/netlink.h>
#include <net/rtnetlink.h>
#include <net/bonding.h>

static size_t bond_get_slave_size(const struct net_device *bond_dev,
				  const struct net_device *slave_dev)
{
	return nla_total_size(sizeof(u8)) +	/* IFLA_BOND_SLAVE_STATE */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_SLAVE_MII_STATUS */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_SLAVE_LINK_FAILURE_COUNT */
		nla_total_size(MAX_ADDR_LEN) +	/* IFLA_BOND_SLAVE_PERM_HWADDR */
		nla_total_size(sizeof(u16)) +	/* IFLA_BOND_SLAVE_QUEUE_ID */
		nla_total_size(sizeof(u16)) +	/* IFLA_BOND_SLAVE_AD_AGGREGATOR_ID */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_SLAVE_AD_ACTOR_OPER_PORT_STATE */
		nla_total_size(sizeof(u16)) +	/* IFLA_BOND_SLAVE_AD_PARTNER_OPER_PORT_STATE */
		0;
}

static int bond_fill_slave_info(struct sk_buff *skb,
				const struct net_device *bond_dev,
				const struct net_device *slave_dev)
{
	struct slave *slave = bond_slave_get_rtnl(slave_dev);

	if (nla_put_u8(skb, IFLA_BOND_SLAVE_STATE, bond_slave_state(slave)))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_SLAVE_MII_STATUS, slave->link))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_SLAVE_LINK_FAILURE_COUNT,
			slave->link_failure_count))
		goto nla_put_failure;

	if (nla_put(skb, IFLA_BOND_SLAVE_PERM_HWADDR,
		    slave_dev->addr_len, slave->perm_hwaddr))
		goto nla_put_failure;

	if (nla_put_u16(skb, IFLA_BOND_SLAVE_QUEUE_ID, slave->queue_id))
		goto nla_put_failure;

	if (BOND_MODE(slave->bond) == BOND_MODE_8023AD) {
		const struct aggregator *agg;
		const struct port *ad_port;

		ad_port = &SLAVE_AD_INFO(slave)->port;
		agg = SLAVE_AD_INFO(slave)->port.aggregator;
		if (agg) {
			if (nla_put_u16(skb, IFLA_BOND_SLAVE_AD_AGGREGATOR_ID,
					agg->aggregator_identifier))
				goto nla_put_failure;
			if (nla_put_u8(skb,
				       IFLA_BOND_SLAVE_AD_ACTOR_OPER_PORT_STATE,
				       ad_port->actor_oper_port_state))
				goto nla_put_failure;
			if (nla_put_u16(skb,
					IFLA_BOND_SLAVE_AD_PARTNER_OPER_PORT_STATE,
					ad_port->partner_oper.port_state))
				goto nla_put_failure;
		}
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy bond_policy[IFLA_BOND_MAX + 1] = {
	[IFLA_BOND_MODE]		= { .type = NLA_U8 },
	[IFLA_BOND_ACTIVE_SLAVE]	= { .type = NLA_U32 },
	[IFLA_BOND_MIIMON]		= { .type = NLA_U32 },
	[IFLA_BOND_UPDELAY]		= { .type = NLA_U32 },
	[IFLA_BOND_DOWNDELAY]		= { .type = NLA_U32 },
	[IFLA_BOND_USE_CARRIER]		= { .type = NLA_U8 },
	[IFLA_BOND_ARP_INTERVAL]	= { .type = NLA_U32 },
	[IFLA_BOND_ARP_IP_TARGET]	= { .type = NLA_NESTED },
	[IFLA_BOND_ARP_VALIDATE]	= { .type = NLA_U32 },
	[IFLA_BOND_ARP_ALL_TARGETS]	= { .type = NLA_U32 },
	[IFLA_BOND_PRIMARY]		= { .type = NLA_U32 },
	[IFLA_BOND_PRIMARY_RESELECT]	= { .type = NLA_U8 },
	[IFLA_BOND_FAIL_OVER_MAC]	= { .type = NLA_U8 },
	[IFLA_BOND_XMIT_HASH_POLICY]	= { .type = NLA_U8 },
	[IFLA_BOND_RESEND_IGMP]		= { .type = NLA_U32 },
	[IFLA_BOND_NUM_PEER_NOTIF]	= { .type = NLA_U8 },
	[IFLA_BOND_ALL_SLAVES_ACTIVE]	= { .type = NLA_U8 },
	[IFLA_BOND_MIN_LINKS]		= { .type = NLA_U32 },
	[IFLA_BOND_LP_INTERVAL]		= { .type = NLA_U32 },
	[IFLA_BOND_PACKETS_PER_SLAVE]	= { .type = NLA_U32 },
	[IFLA_BOND_AD_LACP_ACTIVE]	= { .type = NLA_U8 },
	[IFLA_BOND_AD_LACP_RATE]	= { .type = NLA_U8 },
	[IFLA_BOND_AD_SELECT]		= { .type = NLA_U8 },
	[IFLA_BOND_AD_INFO]		= { .type = NLA_NESTED },
	[IFLA_BOND_AD_ACTOR_SYS_PRIO]	= { .type = NLA_U16 },
	[IFLA_BOND_AD_USER_PORT_KEY]	= { .type = NLA_U16 },
	[IFLA_BOND_AD_ACTOR_SYSTEM]	= { .type = NLA_BINARY,
					    .len  = ETH_ALEN },
	[IFLA_BOND_TLB_DYNAMIC_LB]	= { .type = NLA_U8 },
	[IFLA_BOND_PEER_NOTIF_DELAY]    = { .type = NLA_U32 },
};

static const struct nla_policy bond_slave_policy[IFLA_BOND_SLAVE_MAX + 1] = {
	[IFLA_BOND_SLAVE_QUEUE_ID]	= { .type = NLA_U16 },
};

static int bond_validate(struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static int bond_slave_changelink(struct net_device *bond_dev,
				 struct net_device *slave_dev,
				 struct nlattr *tb[], struct nlattr *data[],
				 struct netlink_ext_ack *extack)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct bond_opt_value newval;
	int err;

	if (!data)
		return 0;

	if (data[IFLA_BOND_SLAVE_QUEUE_ID]) {
		u16 queue_id = nla_get_u16(data[IFLA_BOND_SLAVE_QUEUE_ID]);
		char queue_id_str[IFNAMSIZ + 7];

		/* queue_id option setting expects slave_name:queue_id */
		snprintf(queue_id_str, sizeof(queue_id_str), "%s:%u\n",
			 slave_dev->name, queue_id);
		bond_opt_initstr(&newval, queue_id_str);
		err = __bond_opt_set(bond, BOND_OPT_QUEUE_ID, &newval);
		if (err)
			return err;
	}

	return 0;
}

static int bond_changelink(struct net_device *bond_dev, struct nlattr *tb[],
			   struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct bond_opt_value newval;
	int miimon = 0;
	int err;

	if (!data)
		return 0;

	if (data[IFLA_BOND_MODE]) {
		int mode = nla_get_u8(data[IFLA_BOND_MODE]);

		bond_opt_initval(&newval, mode);
		err = __bond_opt_set(bond, BOND_OPT_MODE, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_ACTIVE_SLAVE]) {
		int ifindex = nla_get_u32(data[IFLA_BOND_ACTIVE_SLAVE]);
		struct net_device *slave_dev;
		char *active_slave = "";

		if (ifindex != 0) {
			slave_dev = __dev_get_by_index(dev_net(bond_dev),
						       ifindex);
			if (!slave_dev)
				return -ENODEV;
			active_slave = slave_dev->name;
		}
		bond_opt_initstr(&newval, active_slave);
		err = __bond_opt_set(bond, BOND_OPT_ACTIVE_SLAVE, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_MIIMON]) {
		miimon = nla_get_u32(data[IFLA_BOND_MIIMON]);

		bond_opt_initval(&newval, miimon);
		err = __bond_opt_set(bond, BOND_OPT_MIIMON, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_UPDELAY]) {
		int updelay = nla_get_u32(data[IFLA_BOND_UPDELAY]);

		bond_opt_initval(&newval, updelay);
		err = __bond_opt_set(bond, BOND_OPT_UPDELAY, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_DOWNDELAY]) {
		int downdelay = nla_get_u32(data[IFLA_BOND_DOWNDELAY]);

		bond_opt_initval(&newval, downdelay);
		err = __bond_opt_set(bond, BOND_OPT_DOWNDELAY, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_PEER_NOTIF_DELAY]) {
		int delay = nla_get_u32(data[IFLA_BOND_PEER_NOTIF_DELAY]);

		bond_opt_initval(&newval, delay);
		err = __bond_opt_set(bond, BOND_OPT_PEER_NOTIF_DELAY, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_USE_CARRIER]) {
		int use_carrier = nla_get_u8(data[IFLA_BOND_USE_CARRIER]);

		bond_opt_initval(&newval, use_carrier);
		err = __bond_opt_set(bond, BOND_OPT_USE_CARRIER, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_ARP_INTERVAL]) {
		int arp_interval = nla_get_u32(data[IFLA_BOND_ARP_INTERVAL]);

		if (arp_interval && miimon) {
			netdev_err(bond->dev, "ARP monitoring cannot be used with MII monitoring\n");
			return -EINVAL;
		}

		bond_opt_initval(&newval, arp_interval);
		err = __bond_opt_set(bond, BOND_OPT_ARP_INTERVAL, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_ARP_IP_TARGET]) {
		struct nlattr *attr;
		int i = 0, rem;

		bond_option_arp_ip_targets_clear(bond);
		nla_for_each_nested(attr, data[IFLA_BOND_ARP_IP_TARGET], rem) {
			__be32 target;

			if (nla_len(attr) < sizeof(target))
				return -EINVAL;

			target = nla_get_be32(attr);

			bond_opt_initval(&newval, (__force u64)target);
			err = __bond_opt_set(bond, BOND_OPT_ARP_TARGETS,
					     &newval);
			if (err)
				break;
			i++;
		}
		if (i == 0 && bond->params.arp_interval)
			netdev_warn(bond->dev, "Removing last arp target with arp_interval on\n");
		if (err)
			return err;
	}
	if (data[IFLA_BOND_ARP_VALIDATE]) {
		int arp_validate = nla_get_u32(data[IFLA_BOND_ARP_VALIDATE]);

		if (arp_validate && miimon) {
			netdev_err(bond->dev, "ARP validating cannot be used with MII monitoring\n");
			return -EINVAL;
		}

		bond_opt_initval(&newval, arp_validate);
		err = __bond_opt_set(bond, BOND_OPT_ARP_VALIDATE, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_ARP_ALL_TARGETS]) {
		int arp_all_targets =
			nla_get_u32(data[IFLA_BOND_ARP_ALL_TARGETS]);

		bond_opt_initval(&newval, arp_all_targets);
		err = __bond_opt_set(bond, BOND_OPT_ARP_ALL_TARGETS, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_PRIMARY]) {
		int ifindex = nla_get_u32(data[IFLA_BOND_PRIMARY]);
		struct net_device *dev;
		char *primary = "";

		dev = __dev_get_by_index(dev_net(bond_dev), ifindex);
		if (dev)
			primary = dev->name;

		bond_opt_initstr(&newval, primary);
		err = __bond_opt_set(bond, BOND_OPT_PRIMARY, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_PRIMARY_RESELECT]) {
		int primary_reselect =
			nla_get_u8(data[IFLA_BOND_PRIMARY_RESELECT]);

		bond_opt_initval(&newval, primary_reselect);
		err = __bond_opt_set(bond, BOND_OPT_PRIMARY_RESELECT, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_FAIL_OVER_MAC]) {
		int fail_over_mac =
			nla_get_u8(data[IFLA_BOND_FAIL_OVER_MAC]);

		bond_opt_initval(&newval, fail_over_mac);
		err = __bond_opt_set(bond, BOND_OPT_FAIL_OVER_MAC, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_XMIT_HASH_POLICY]) {
		int xmit_hash_policy =
			nla_get_u8(data[IFLA_BOND_XMIT_HASH_POLICY]);

		bond_opt_initval(&newval, xmit_hash_policy);
		err = __bond_opt_set(bond, BOND_OPT_XMIT_HASH, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_RESEND_IGMP]) {
		int resend_igmp =
			nla_get_u32(data[IFLA_BOND_RESEND_IGMP]);

		bond_opt_initval(&newval, resend_igmp);
		err = __bond_opt_set(bond, BOND_OPT_RESEND_IGMP, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_NUM_PEER_NOTIF]) {
		int num_peer_notif =
			nla_get_u8(data[IFLA_BOND_NUM_PEER_NOTIF]);

		bond_opt_initval(&newval, num_peer_notif);
		err = __bond_opt_set(bond, BOND_OPT_NUM_PEER_NOTIF, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_ALL_SLAVES_ACTIVE]) {
		int all_slaves_active =
			nla_get_u8(data[IFLA_BOND_ALL_SLAVES_ACTIVE]);

		bond_opt_initval(&newval, all_slaves_active);
		err = __bond_opt_set(bond, BOND_OPT_ALL_SLAVES_ACTIVE, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_MIN_LINKS]) {
		int min_links =
			nla_get_u32(data[IFLA_BOND_MIN_LINKS]);

		bond_opt_initval(&newval, min_links);
		err = __bond_opt_set(bond, BOND_OPT_MINLINKS, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_LP_INTERVAL]) {
		int lp_interval =
			nla_get_u32(data[IFLA_BOND_LP_INTERVAL]);

		bond_opt_initval(&newval, lp_interval);
		err = __bond_opt_set(bond, BOND_OPT_LP_INTERVAL, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_PACKETS_PER_SLAVE]) {
		int packets_per_slave =
			nla_get_u32(data[IFLA_BOND_PACKETS_PER_SLAVE]);

		bond_opt_initval(&newval, packets_per_slave);
		err = __bond_opt_set(bond, BOND_OPT_PACKETS_PER_SLAVE, &newval);
		if (err)
			return err;
	}

	if (data[IFLA_BOND_AD_LACP_ACTIVE]) {
		int lacp_active = nla_get_u8(data[IFLA_BOND_AD_LACP_ACTIVE]);

		bond_opt_initval(&newval, lacp_active);
		err = __bond_opt_set(bond, BOND_OPT_LACP_ACTIVE, &newval);
		if (err)
			return err;
	}

	if (data[IFLA_BOND_AD_LACP_RATE]) {
		int lacp_rate =
			nla_get_u8(data[IFLA_BOND_AD_LACP_RATE]);

		bond_opt_initval(&newval, lacp_rate);
		err = __bond_opt_set(bond, BOND_OPT_LACP_RATE, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_AD_SELECT]) {
		int ad_select =
			nla_get_u8(data[IFLA_BOND_AD_SELECT]);

		bond_opt_initval(&newval, ad_select);
		err = __bond_opt_set(bond, BOND_OPT_AD_SELECT, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_AD_ACTOR_SYS_PRIO]) {
		int actor_sys_prio =
			nla_get_u16(data[IFLA_BOND_AD_ACTOR_SYS_PRIO]);

		bond_opt_initval(&newval, actor_sys_prio);
		err = __bond_opt_set(bond, BOND_OPT_AD_ACTOR_SYS_PRIO, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_AD_USER_PORT_KEY]) {
		int port_key =
			nla_get_u16(data[IFLA_BOND_AD_USER_PORT_KEY]);

		bond_opt_initval(&newval, port_key);
		err = __bond_opt_set(bond, BOND_OPT_AD_USER_PORT_KEY, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_AD_ACTOR_SYSTEM]) {
		if (nla_len(data[IFLA_BOND_AD_ACTOR_SYSTEM]) != ETH_ALEN)
			return -EINVAL;

		bond_opt_initval(&newval,
				 nla_get_u64(data[IFLA_BOND_AD_ACTOR_SYSTEM]));
		err = __bond_opt_set(bond, BOND_OPT_AD_ACTOR_SYSTEM, &newval);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_TLB_DYNAMIC_LB]) {
		int dynamic_lb = nla_get_u8(data[IFLA_BOND_TLB_DYNAMIC_LB]);

		bond_opt_initval(&newval, dynamic_lb);
		err = __bond_opt_set(bond, BOND_OPT_TLB_DYNAMIC_LB, &newval);
		if (err)
			return err;
	}

	return 0;
}

static int bond_newlink(struct net *src_net, struct net_device *bond_dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	int err;

	err = bond_changelink(bond_dev, tb, data, extack);
	if (err < 0)
		return err;

	err = register_netdevice(bond_dev);
	if (!err) {
		struct bonding *bond = netdev_priv(bond_dev);

		netif_carrier_off(bond_dev);
		bond_work_init_all(bond);
	}

	return err;
}

static size_t bond_get_size(const struct net_device *bond_dev)
{
	return nla_total_size(sizeof(u8)) +	/* IFLA_BOND_MODE */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_ACTIVE_SLAVE */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_MIIMON */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_UPDELAY */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_DOWNDELAY */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_USE_CARRIER */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_ARP_INTERVAL */
						/* IFLA_BOND_ARP_IP_TARGET */
		nla_total_size(sizeof(struct nlattr)) +
		nla_total_size(sizeof(u32)) * BOND_MAX_ARP_TARGETS +
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_ARP_VALIDATE */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_ARP_ALL_TARGETS */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_PRIMARY */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_PRIMARY_RESELECT */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_FAIL_OVER_MAC */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_XMIT_HASH_POLICY */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_RESEND_IGMP */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_NUM_PEER_NOTIF */
		nla_total_size(sizeof(u8)) +   /* IFLA_BOND_ALL_SLAVES_ACTIVE */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_MIN_LINKS */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_LP_INTERVAL */
		nla_total_size(sizeof(u32)) +  /* IFLA_BOND_PACKETS_PER_SLAVE */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_AD_LACP_ACTIVE */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_AD_LACP_RATE */
		nla_total_size(sizeof(u8)) +	/* IFLA_BOND_AD_SELECT */
		nla_total_size(sizeof(struct nlattr)) + /* IFLA_BOND_AD_INFO */
		nla_total_size(sizeof(u16)) + /* IFLA_BOND_AD_INFO_AGGREGATOR */
		nla_total_size(sizeof(u16)) + /* IFLA_BOND_AD_INFO_NUM_PORTS */
		nla_total_size(sizeof(u16)) + /* IFLA_BOND_AD_INFO_ACTOR_KEY */
		nla_total_size(sizeof(u16)) + /* IFLA_BOND_AD_INFO_PARTNER_KEY*/
		nla_total_size(ETH_ALEN) +    /* IFLA_BOND_AD_INFO_PARTNER_MAC*/
		nla_total_size(sizeof(u16)) + /* IFLA_BOND_AD_ACTOR_SYS_PRIO */
		nla_total_size(sizeof(u16)) + /* IFLA_BOND_AD_USER_PORT_KEY */
		nla_total_size(ETH_ALEN) + /* IFLA_BOND_AD_ACTOR_SYSTEM */
		nla_total_size(sizeof(u8)) + /* IFLA_BOND_TLB_DYNAMIC_LB */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_PEER_NOTIF_DELAY */
		0;
}

static int bond_option_active_slave_get_ifindex(struct bonding *bond)
{
	const struct net_device *slave;
	int ifindex;

	rcu_read_lock();
	slave = bond_option_active_slave_get_rcu(bond);
	ifindex = slave ? slave->ifindex : 0;
	rcu_read_unlock();
	return ifindex;
}

static int bond_fill_info(struct sk_buff *skb,
			  const struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	unsigned int packets_per_slave;
	int ifindex, i, targets_added;
	struct nlattr *targets;
	struct slave *primary;

	if (nla_put_u8(skb, IFLA_BOND_MODE, BOND_MODE(bond)))
		goto nla_put_failure;

	ifindex = bond_option_active_slave_get_ifindex(bond);
	if (ifindex && nla_put_u32(skb, IFLA_BOND_ACTIVE_SLAVE, ifindex))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_MIIMON, bond->params.miimon))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_UPDELAY,
			bond->params.updelay * bond->params.miimon))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_DOWNDELAY,
			bond->params.downdelay * bond->params.miimon))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_PEER_NOTIF_DELAY,
			bond->params.peer_notif_delay * bond->params.miimon))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_USE_CARRIER, bond->params.use_carrier))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_ARP_INTERVAL, bond->params.arp_interval))
		goto nla_put_failure;

	targets = nla_nest_start_noflag(skb, IFLA_BOND_ARP_IP_TARGET);
	if (!targets)
		goto nla_put_failure;

	targets_added = 0;
	for (i = 0; i < BOND_MAX_ARP_TARGETS; i++) {
		if (bond->params.arp_targets[i]) {
			if (nla_put_be32(skb, i, bond->params.arp_targets[i]))
				goto nla_put_failure;
			targets_added = 1;
		}
	}

	if (targets_added)
		nla_nest_end(skb, targets);
	else
		nla_nest_cancel(skb, targets);

	if (nla_put_u32(skb, IFLA_BOND_ARP_VALIDATE, bond->params.arp_validate))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_ARP_ALL_TARGETS,
			bond->params.arp_all_targets))
		goto nla_put_failure;

	primary = rtnl_dereference(bond->primary_slave);
	if (primary &&
	    nla_put_u32(skb, IFLA_BOND_PRIMARY, primary->dev->ifindex))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_PRIMARY_RESELECT,
		       bond->params.primary_reselect))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_FAIL_OVER_MAC,
		       bond->params.fail_over_mac))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_XMIT_HASH_POLICY,
		       bond->params.xmit_policy))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_RESEND_IGMP,
			bond->params.resend_igmp))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_NUM_PEER_NOTIF,
		       bond->params.num_peer_notif))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_ALL_SLAVES_ACTIVE,
		       bond->params.all_slaves_active))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_MIN_LINKS,
			bond->params.min_links))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_LP_INTERVAL,
			bond->params.lp_interval))
		goto nla_put_failure;

	packets_per_slave = bond->params.packets_per_slave;
	if (nla_put_u32(skb, IFLA_BOND_PACKETS_PER_SLAVE,
			packets_per_slave))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_AD_LACP_ACTIVE,
		       bond->params.lacp_active))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_AD_LACP_RATE,
		       bond->params.lacp_fast))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_AD_SELECT,
		       bond->params.ad_select))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_BOND_TLB_DYNAMIC_LB,
		       bond->params.tlb_dynamic_lb))
		goto nla_put_failure;

	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		struct ad_info info;

		if (capable(CAP_NET_ADMIN)) {
			if (nla_put_u16(skb, IFLA_BOND_AD_ACTOR_SYS_PRIO,
					bond->params.ad_actor_sys_prio))
				goto nla_put_failure;

			if (nla_put_u16(skb, IFLA_BOND_AD_USER_PORT_KEY,
					bond->params.ad_user_port_key))
				goto nla_put_failure;

			if (nla_put(skb, IFLA_BOND_AD_ACTOR_SYSTEM,
				    ETH_ALEN, &bond->params.ad_actor_system))
				goto nla_put_failure;
		}
		if (!bond_3ad_get_active_agg_info(bond, &info)) {
			struct nlattr *nest;

			nest = nla_nest_start_noflag(skb, IFLA_BOND_AD_INFO);
			if (!nest)
				goto nla_put_failure;

			if (nla_put_u16(skb, IFLA_BOND_AD_INFO_AGGREGATOR,
					info.aggregator_id))
				goto nla_put_failure;
			if (nla_put_u16(skb, IFLA_BOND_AD_INFO_NUM_PORTS,
					info.ports))
				goto nla_put_failure;
			if (nla_put_u16(skb, IFLA_BOND_AD_INFO_ACTOR_KEY,
					info.actor_key))
				goto nla_put_failure;
			if (nla_put_u16(skb, IFLA_BOND_AD_INFO_PARTNER_KEY,
					info.partner_key))
				goto nla_put_failure;
			if (nla_put(skb, IFLA_BOND_AD_INFO_PARTNER_MAC,
				    sizeof(info.partner_system),
				    &info.partner_system))
				goto nla_put_failure;

			nla_nest_end(skb, nest);
		}
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static size_t bond_get_linkxstats_size(const struct net_device *dev, int attr)
{
	switch (attr) {
	case IFLA_STATS_LINK_XSTATS:
	case IFLA_STATS_LINK_XSTATS_SLAVE:
		break;
	default:
		return 0;
	}

	return bond_3ad_stats_size() + nla_total_size(0);
}

static int bond_fill_linkxstats(struct sk_buff *skb,
				const struct net_device *dev,
				int *prividx, int attr)
{
	struct nlattr *nla __maybe_unused;
	struct slave *slave = NULL;
	struct nlattr *nest, *nest2;
	struct bonding *bond;

	switch (attr) {
	case IFLA_STATS_LINK_XSTATS:
		bond = netdev_priv(dev);
		break;
	case IFLA_STATS_LINK_XSTATS_SLAVE:
		slave = bond_slave_get_rtnl(dev);
		if (!slave)
			return 0;
		bond = slave->bond;
		break;
	default:
		return -EINVAL;
	}

	nest = nla_nest_start_noflag(skb, LINK_XSTATS_TYPE_BOND);
	if (!nest)
		return -EMSGSIZE;
	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		struct bond_3ad_stats *stats;

		if (slave)
			stats = &SLAVE_AD_INFO(slave)->stats;
		else
			stats = &BOND_AD_INFO(bond).stats;

		nest2 = nla_nest_start_noflag(skb, BOND_XSTATS_3AD);
		if (!nest2) {
			nla_nest_end(skb, nest);
			return -EMSGSIZE;
		}

		if (bond_3ad_stats_fill(skb, stats)) {
			nla_nest_cancel(skb, nest2);
			nla_nest_end(skb, nest);
			return -EMSGSIZE;
		}
		nla_nest_end(skb, nest2);
	}
	nla_nest_end(skb, nest);

	return 0;
}

struct rtnl_link_ops bond_link_ops __read_mostly = {
	.kind			= "bond",
	.priv_size		= sizeof(struct bonding),
	.setup			= bond_setup,
	.maxtype		= IFLA_BOND_MAX,
	.policy			= bond_policy,
	.validate		= bond_validate,
	.newlink		= bond_newlink,
	.changelink		= bond_changelink,
	.get_size		= bond_get_size,
	.fill_info		= bond_fill_info,
	.get_num_tx_queues	= bond_get_num_tx_queues,
	.get_num_rx_queues	= bond_get_num_tx_queues, /* Use the same number
							     as for TX queues */
	.fill_linkxstats        = bond_fill_linkxstats,
	.get_linkxstats_size    = bond_get_linkxstats_size,
	.slave_maxtype		= IFLA_BOND_SLAVE_MAX,
	.slave_policy		= bond_slave_policy,
	.slave_changelink	= bond_slave_changelink,
	.get_slave_size		= bond_get_slave_size,
	.fill_slave_info	= bond_fill_slave_info,
};

int __init bond_netlink_init(void)
{
	return rtnl_link_register(&bond_link_ops);
}

void bond_netlink_fini(void)
{
	rtnl_link_unregister(&bond_link_ops);
}

MODULE_ALIAS_RTNL_LINK("bond");
