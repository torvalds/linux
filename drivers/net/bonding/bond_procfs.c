// SPDX-License-Identifier: GPL-2.0
#include <linux/proc_fs.h>
#include <linux/ethtool.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/bonding.h>

#include "bonding_priv.h"

static void *bond_info_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct bonding *bond = PDE_DATA(file_inode(seq->file));
	struct list_head *iter;
	struct slave *slave;
	loff_t off = 0;

	rcu_read_lock();

	if (*pos == 0)
		return SEQ_START_TOKEN;

	bond_for_each_slave_rcu(bond, slave, iter)
		if (++off == *pos)
			return slave;

	return NULL;
}

static void *bond_info_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bonding *bond = PDE_DATA(file_inode(seq->file));
	struct list_head *iter;
	struct slave *slave;
	bool found = false;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return bond_first_slave_rcu(bond);

	bond_for_each_slave_rcu(bond, slave, iter) {
		if (found)
			return slave;
		if (slave == v)
			found = true;
	}

	return NULL;
}

static void bond_info_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static void bond_info_show_master(struct seq_file *seq)
{
	struct bonding *bond = PDE_DATA(file_inode(seq->file));
	const struct bond_opt_value *optval;
	struct slave *curr, *primary;
	int i;

	curr = rcu_dereference(bond->curr_active_slave);

	seq_printf(seq, "Bonding Mode: %s",
		   bond_mode_name(BOND_MODE(bond)));

	if (BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP &&
	    bond->params.fail_over_mac) {
		optval = bond_opt_get_val(BOND_OPT_FAIL_OVER_MAC,
					  bond->params.fail_over_mac);
		seq_printf(seq, " (fail_over_mac %s)", optval->string);
	}

	seq_printf(seq, "\n");

	if (bond_mode_uses_xmit_hash(bond)) {
		optval = bond_opt_get_val(BOND_OPT_XMIT_HASH,
					  bond->params.xmit_policy);
		seq_printf(seq, "Transmit Hash Policy: %s (%d)\n",
			   optval->string, bond->params.xmit_policy);
	}

	if (bond_uses_primary(bond)) {
		primary = rcu_dereference(bond->primary_slave);
		seq_printf(seq, "Primary Slave: %s",
			   primary ? primary->dev->name : "None");
		if (primary) {
			optval = bond_opt_get_val(BOND_OPT_PRIMARY_RESELECT,
						  bond->params.primary_reselect);
			seq_printf(seq, " (primary_reselect %s)",
				   optval->string);
		}

		seq_printf(seq, "\nCurrently Active Slave: %s\n",
			   (curr) ? curr->dev->name : "None");
	}

	seq_printf(seq, "MII Status: %s\n", netif_carrier_ok(bond->dev) ?
		   "up" : "down");
	seq_printf(seq, "MII Polling Interval (ms): %d\n", bond->params.miimon);
	seq_printf(seq, "Up Delay (ms): %d\n",
		   bond->params.updelay * bond->params.miimon);
	seq_printf(seq, "Down Delay (ms): %d\n",
		   bond->params.downdelay * bond->params.miimon);
	seq_printf(seq, "Peer Notification Delay (ms): %d\n",
		   bond->params.peer_notif_delay * bond->params.miimon);


	/* ARP information */
	if (bond->params.arp_interval > 0) {
		int printed = 0;

		seq_printf(seq, "ARP Polling Interval (ms): %d\n",
				bond->params.arp_interval);
		seq_printf(seq, "ARP Missed Max: %u\n",
				bond->params.missed_max);

		seq_printf(seq, "ARP IP target/s (n.n.n.n form):");

		for (i = 0; (i < BOND_MAX_ARP_TARGETS); i++) {
			if (!bond->params.arp_targets[i])
				break;
			if (printed)
				seq_printf(seq, ",");
			seq_printf(seq, " %pI4", &bond->params.arp_targets[i]);
			printed = 1;
		}
		seq_printf(seq, "\n");
	}

	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		struct ad_info ad_info;

		seq_puts(seq, "\n802.3ad info\n");
		seq_printf(seq, "LACP active: %s\n",
			   (bond->params.lacp_active) ? "on" : "off");
		seq_printf(seq, "LACP rate: %s\n",
			   (bond->params.lacp_fast) ? "fast" : "slow");
		seq_printf(seq, "Min links: %d\n", bond->params.min_links);
		optval = bond_opt_get_val(BOND_OPT_AD_SELECT,
					  bond->params.ad_select);
		seq_printf(seq, "Aggregator selection policy (ad_select): %s\n",
			   optval->string);
		if (capable(CAP_NET_ADMIN)) {
			seq_printf(seq, "System priority: %d\n",
				   BOND_AD_INFO(bond).system.sys_priority);
			seq_printf(seq, "System MAC address: %pM\n",
				   &BOND_AD_INFO(bond).system.sys_mac_addr);

			if (__bond_3ad_get_active_agg_info(bond, &ad_info)) {
				seq_printf(seq,
					   "bond %s has no active aggregator\n",
					   bond->dev->name);
			} else {
				seq_printf(seq, "Active Aggregator Info:\n");

				seq_printf(seq, "\tAggregator ID: %d\n",
					   ad_info.aggregator_id);
				seq_printf(seq, "\tNumber of ports: %d\n",
					   ad_info.ports);
				seq_printf(seq, "\tActor Key: %d\n",
					   ad_info.actor_key);
				seq_printf(seq, "\tPartner Key: %d\n",
					   ad_info.partner_key);
				seq_printf(seq, "\tPartner Mac Address: %pM\n",
					   ad_info.partner_system);
			}
		}
	}
}

static void bond_info_show_slave(struct seq_file *seq,
				 const struct slave *slave)
{
	struct bonding *bond = PDE_DATA(file_inode(seq->file));

	seq_printf(seq, "\nSlave Interface: %s\n", slave->dev->name);
	seq_printf(seq, "MII Status: %s\n", bond_slave_link_status(slave->link));
	if (slave->speed == SPEED_UNKNOWN)
		seq_printf(seq, "Speed: %s\n", "Unknown");
	else
		seq_printf(seq, "Speed: %d Mbps\n", slave->speed);

	if (slave->duplex == DUPLEX_UNKNOWN)
		seq_printf(seq, "Duplex: %s\n", "Unknown");
	else
		seq_printf(seq, "Duplex: %s\n", slave->duplex ? "full" : "half");

	seq_printf(seq, "Link Failure Count: %u\n",
		   slave->link_failure_count);

	seq_printf(seq, "Permanent HW addr: %*phC\n",
		   slave->dev->addr_len, slave->perm_hwaddr);
	seq_printf(seq, "Slave queue ID: %d\n", slave->queue_id);

	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		const struct port *port = &SLAVE_AD_INFO(slave)->port;
		const struct aggregator *agg = port->aggregator;

		if (agg) {
			seq_printf(seq, "Aggregator ID: %d\n",
				   agg->aggregator_identifier);
			seq_printf(seq, "Actor Churn State: %s\n",
				   bond_3ad_churn_desc(port->sm_churn_actor_state));
			seq_printf(seq, "Partner Churn State: %s\n",
				   bond_3ad_churn_desc(port->sm_churn_partner_state));
			seq_printf(seq, "Actor Churned Count: %d\n",
				   port->churn_actor_count);
			seq_printf(seq, "Partner Churned Count: %d\n",
				   port->churn_partner_count);

			if (capable(CAP_NET_ADMIN)) {
				seq_puts(seq, "details actor lacp pdu:\n");
				seq_printf(seq, "    system priority: %d\n",
					   port->actor_system_priority);
				seq_printf(seq, "    system mac address: %pM\n",
					   &port->actor_system);
				seq_printf(seq, "    port key: %d\n",
					   port->actor_oper_port_key);
				seq_printf(seq, "    port priority: %d\n",
					   port->actor_port_priority);
				seq_printf(seq, "    port number: %d\n",
					   port->actor_port_number);
				seq_printf(seq, "    port state: %d\n",
					   port->actor_oper_port_state);

				seq_puts(seq, "details partner lacp pdu:\n");
				seq_printf(seq, "    system priority: %d\n",
					   port->partner_oper.system_priority);
				seq_printf(seq, "    system mac address: %pM\n",
					   &port->partner_oper.system);
				seq_printf(seq, "    oper key: %d\n",
					   port->partner_oper.key);
				seq_printf(seq, "    port priority: %d\n",
					   port->partner_oper.port_priority);
				seq_printf(seq, "    port number: %d\n",
					   port->partner_oper.port_number);
				seq_printf(seq, "    port state: %d\n",
					   port->partner_oper.port_state);
			}
		} else {
			seq_puts(seq, "Aggregator ID: N/A\n");
		}
	}
}

static int bond_info_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%s\n", bond_version);
		bond_info_show_master(seq);
	} else
		bond_info_show_slave(seq, v);

	return 0;
}

static const struct seq_operations bond_info_seq_ops = {
	.start = bond_info_seq_start,
	.next  = bond_info_seq_next,
	.stop  = bond_info_seq_stop,
	.show  = bond_info_seq_show,
};

void bond_create_proc_entry(struct bonding *bond)
{
	struct net_device *bond_dev = bond->dev;
	struct bond_net *bn = net_generic(dev_net(bond_dev), bond_net_id);

	if (bn->proc_dir) {
		bond->proc_entry = proc_create_seq_data(bond_dev->name, 0444,
				bn->proc_dir, &bond_info_seq_ops, bond);
		if (bond->proc_entry == NULL)
			netdev_warn(bond_dev, "Cannot create /proc/net/%s/%s\n",
				    DRV_NAME, bond_dev->name);
		else
			memcpy(bond->proc_file_name, bond_dev->name, IFNAMSIZ);
	}
}

void bond_remove_proc_entry(struct bonding *bond)
{
	struct net_device *bond_dev = bond->dev;
	struct bond_net *bn = net_generic(dev_net(bond_dev), bond_net_id);

	if (bn->proc_dir && bond->proc_entry) {
		remove_proc_entry(bond->proc_file_name, bn->proc_dir);
		memset(bond->proc_file_name, 0, IFNAMSIZ);
		bond->proc_entry = NULL;
	}
}

/* Create the bonding directory under /proc/net, if doesn't exist yet.
 * Caller must hold rtnl_lock.
 */
void __net_init bond_create_proc_dir(struct bond_net *bn)
{
	if (!bn->proc_dir) {
		bn->proc_dir = proc_mkdir(DRV_NAME, bn->net->proc_net);
		if (!bn->proc_dir)
			pr_warn("Warning: Cannot create /proc/net/%s\n",
				DRV_NAME);
	}
}

/* Destroy the bonding directory under /proc/net, if empty.
 * Caller must hold rtnl_lock.
 */
void __net_exit bond_destroy_proc_dir(struct bond_net *bn)
{
	if (bn->proc_dir) {
		remove_proc_entry(DRV_NAME, bn->net->proc_net);
		bn->proc_dir = NULL;
	}
}
