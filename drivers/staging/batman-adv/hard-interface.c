/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "hard-interface.h"
#include "soft-interface.h"
#include "send.h"
#include "translation-table.h"
#include "routing.h"
#include "bat_sysfs.h"
#include "originator.h"
#include "hash.h"

#include <linux/if_arp.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

struct batman_if *get_batman_if_by_netdev(struct net_device *net_dev)
{
	struct batman_if *batman_if;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if (batman_if->net_dev == net_dev)
			goto out;
	}

	batman_if = NULL;

out:
	rcu_read_unlock();
	return batman_if;
}

static int is_valid_iface(struct net_device *net_dev)
{
	if (net_dev->flags & IFF_LOOPBACK)
		return 0;

	if (net_dev->type != ARPHRD_ETHER)
		return 0;

	if (net_dev->addr_len != ETH_ALEN)
		return 0;

	/* no batman over batman */
#ifdef HAVE_NET_DEVICE_OPS
	if (net_dev->netdev_ops->ndo_start_xmit == interface_tx)
		return 0;
#else
	if (net_dev->hard_start_xmit == interface_tx)
		return 0;
#endif

	/* Device is being bridged */
	/* if (net_dev->br_port != NULL)
		return 0; */

	return 1;
}

static struct batman_if *get_active_batman_if(void)
{
	struct batman_if *batman_if;

	/* TODO: should check interfaces belonging to bat_priv */
	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if (batman_if->if_status == IF_ACTIVE)
			goto out;
	}

	batman_if = NULL;

out:
	rcu_read_unlock();
	return batman_if;
}

static void set_primary_if(struct bat_priv *bat_priv,
			   struct batman_if *batman_if)
{
	struct batman_packet *batman_packet;

	bat_priv->primary_if = batman_if;

	if (!bat_priv->primary_if)
		return;

	set_main_if_addr(batman_if->net_dev->dev_addr);

	batman_packet = (struct batman_packet *)(batman_if->packet_buff);
	batman_packet->flags = 0;
	batman_packet->ttl = TTL;

	/***
	 * hacky trick to make sure that we send the HNA information via
	 * our new primary interface
	 */
	atomic_set(&hna_local_changed, 1);
}

static bool hardif_is_iface_up(struct batman_if *batman_if)
{
	if (batman_if->net_dev->flags & IFF_UP)
		return true;

	return false;
}

static void update_mac_addresses(struct batman_if *batman_if)
{
	addr_to_string(batman_if->addr_str, batman_if->net_dev->dev_addr);

	memcpy(((struct batman_packet *)(batman_if->packet_buff))->orig,
	       batman_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(((struct batman_packet *)(batman_if->packet_buff))->prev_sender,
	       batman_if->net_dev->dev_addr, ETH_ALEN);
}

static void check_known_mac_addr(uint8_t *addr)
{
	struct batman_if *batman_if;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if ((batman_if->if_status != IF_ACTIVE) &&
		    (batman_if->if_status != IF_TO_BE_ACTIVATED))
			continue;

		if (!compare_orig(batman_if->net_dev->dev_addr, addr))
			continue;

		printk(KERN_WARNING "batman-adv:"
		    "The newly added mac address (%pM) already exists on: %s\n",
		    addr, batman_if->dev);
		printk(KERN_WARNING "batman-adv:"
		    "It is strongly recommended to keep mac addresses unique"
		    "to avoid problems!\n");
	}
	rcu_read_unlock();
}

int hardif_min_mtu(void)
{
	struct batman_if *batman_if;
	/* allow big frames if all devices are capable to do so
	 * (have MTU > 1500 + BAT_HEADER_LEN) */
	int min_mtu = ETH_DATA_LEN;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if ((batman_if->if_status == IF_ACTIVE) ||
		    (batman_if->if_status == IF_TO_BE_ACTIVATED))
			min_mtu = MIN(batman_if->net_dev->mtu - BAT_HEADER_LEN,
				      min_mtu);
	}
	rcu_read_unlock();

	return min_mtu;
}

/* adjusts the MTU if a new interface with a smaller MTU appeared. */
void update_min_mtu(void)
{
	int min_mtu;

	min_mtu = hardif_min_mtu();
	if (soft_device->mtu != min_mtu)
		soft_device->mtu = min_mtu;
}

static void hardif_activate_interface(struct bat_priv *bat_priv,
				      struct batman_if *batman_if)
{
	if (batman_if->if_status != IF_INACTIVE)
		return;

	dev_hold(batman_if->net_dev);

	update_mac_addresses(batman_if);
	batman_if->if_status = IF_TO_BE_ACTIVATED;

	/**
	 * the first active interface becomes our primary interface or
	 * the next active interface after the old primay interface was removed
	 */
	if (!bat_priv->primary_if)
		set_primary_if(bat_priv, batman_if);

	printk(KERN_INFO "batman-adv:Interface activated: %s\n",
	       batman_if->dev);

	if (atomic_read(&module_state) == MODULE_INACTIVE)
		activate_module();

	update_min_mtu();
	return;
}

static void hardif_deactivate_interface(struct batman_if *batman_if)
{
	if ((batman_if->if_status != IF_ACTIVE) &&
	   (batman_if->if_status != IF_TO_BE_ACTIVATED))
		return;

	dev_put(batman_if->net_dev);

	batman_if->if_status = IF_INACTIVE;

	printk(KERN_INFO "batman-adv:Interface deactivated: %s\n",
	       batman_if->dev);

	update_min_mtu();
}

int hardif_enable_interface(struct batman_if *batman_if)
{
	/* FIXME: each batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);
	struct batman_packet *batman_packet;

	if (batman_if->if_status != IF_NOT_IN_USE)
		goto out;

	batman_if->packet_len = BAT_PACKET_LEN;
	batman_if->packet_buff = kmalloc(batman_if->packet_len, GFP_ATOMIC);

	if (!batman_if->packet_buff) {
		printk(KERN_ERR "batman-adv:"
		       "Can't add interface packet (%s): out of memory\n",
		       batman_if->dev);
		goto err;
	}

	batman_packet = (struct batman_packet *)(batman_if->packet_buff);
	batman_packet->packet_type = BAT_PACKET;
	batman_packet->version = COMPAT_VERSION;
	batman_packet->flags = 0;
	batman_packet->ttl = 2;
	batman_packet->tq = TQ_MAX_VALUE;
	batman_packet->num_hna = 0;

	batman_if->if_num = bat_priv->num_ifaces;
	bat_priv->num_ifaces++;
	batman_if->if_status = IF_INACTIVE;
	orig_hash_add_if(batman_if, bat_priv->num_ifaces);

	atomic_set(&batman_if->seqno, 1);
	printk(KERN_INFO "batman-adv:Adding interface: %s\n", batman_if->dev);

	if (hardif_is_iface_up(batman_if))
		hardif_activate_interface(bat_priv, batman_if);
	else
		printk(KERN_ERR "batman-adv:"
		       "Not using interface %s "
		       "(retrying later): interface not active\n",
		       batman_if->dev);

	/* begin scheduling originator messages on that interface */
	schedule_own_packet(batman_if);

out:
	return 0;

err:
	return -ENOMEM;
}

void hardif_disable_interface(struct batman_if *batman_if)
{
	/* FIXME: each batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);

	if (batman_if->if_status == IF_ACTIVE)
		hardif_deactivate_interface(batman_if);

	if (batman_if->if_status != IF_INACTIVE)
		return;

	printk(KERN_INFO "batman-adv:Removing interface: %s\n", batman_if->dev);
	bat_priv->num_ifaces--;
	orig_hash_del_if(batman_if, bat_priv->num_ifaces);

	if (batman_if == bat_priv->primary_if)
		set_primary_if(bat_priv, get_active_batman_if());

	kfree(batman_if->packet_buff);
	batman_if->packet_buff = NULL;
	batman_if->if_status = IF_NOT_IN_USE;

	if ((atomic_read(&module_state) == MODULE_ACTIVE) &&
	    (bat_priv->num_ifaces == 0))
		deactivate_module();
}

static struct batman_if *hardif_add_interface(struct net_device *net_dev)
{
	struct batman_if *batman_if;
	int ret;

	ret = is_valid_iface(net_dev);
	if (ret != 1)
		goto out;

	batman_if = kmalloc(sizeof(struct batman_if), GFP_ATOMIC);
	if (!batman_if) {
		printk(KERN_ERR "batman-adv:"
		       "Can't add interface (%s): out of memory\n",
		       net_dev->name);
		goto out;
	}

	batman_if->dev = kstrdup(net_dev->name, GFP_ATOMIC);
	if (!batman_if->dev)
		goto free_if;

	ret = sysfs_add_hardif(&batman_if->hardif_obj, net_dev);
	if (ret)
		goto free_dev;

	batman_if->if_num = -1;
	batman_if->net_dev = net_dev;
	batman_if->if_status = IF_NOT_IN_USE;
	INIT_LIST_HEAD(&batman_if->list);

	check_known_mac_addr(batman_if->net_dev->dev_addr);
	list_add_tail_rcu(&batman_if->list, &if_list);
	return batman_if;

free_dev:
	kfree(batman_if->dev);
free_if:
	kfree(batman_if);
out:
	return NULL;
}

static void hardif_free_interface(struct rcu_head *rcu)
{
	struct batman_if *batman_if = container_of(rcu, struct batman_if, rcu);

	/* delete all references to this batman_if */
	purge_orig(NULL);
	purge_outstanding_packets(batman_if);

	kfree(batman_if->dev);
	kfree(batman_if);
}

static void hardif_remove_interface(struct batman_if *batman_if)
{
	/* first deactivate interface */
	if (batman_if->if_status != IF_NOT_IN_USE)
		hardif_disable_interface(batman_if);

	if (batman_if->if_status != IF_NOT_IN_USE)
		return;

	batman_if->if_status = IF_TO_BE_REMOVED;
	list_del_rcu(&batman_if->list);
	sysfs_del_hardif(&batman_if->hardif_obj);
	call_rcu(&batman_if->rcu, hardif_free_interface);
}

void hardif_remove_interfaces(void)
{
	struct batman_if *batman_if, *batman_if_tmp;

	list_for_each_entry_safe(batman_if, batman_if_tmp, &if_list, list)
		hardif_remove_interface(batman_if);
}

static int hard_if_event(struct notifier_block *this,
			 unsigned long event, void *ptr)
{
	struct net_device *net_dev = (struct net_device *)ptr;
	struct batman_if *batman_if = get_batman_if_by_netdev(net_dev);
	/* FIXME: each batman_if will be attached to a softif */
	struct bat_priv *bat_priv = netdev_priv(soft_device);

	if (!batman_if)
		batman_if = hardif_add_interface(net_dev);

	if (!batman_if)
		goto out;

	switch (event) {
	case NETDEV_REGISTER:
		break;
	case NETDEV_UP:
		hardif_activate_interface(bat_priv, batman_if);
		break;
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
		hardif_deactivate_interface(batman_if);
		break;
	case NETDEV_UNREGISTER:
		hardif_remove_interface(batman_if);
		break;
	case NETDEV_CHANGENAME:
		break;
	case NETDEV_CHANGEADDR:
		check_known_mac_addr(batman_if->net_dev->dev_addr);
		update_mac_addresses(batman_if);
		if (batman_if == bat_priv->primary_if)
			set_primary_if(bat_priv, batman_if);
		break;
	default:
		break;
	};

out:
	return NOTIFY_DONE;
}

/* receive a packet with the batman ethertype coming on a hard
 * interface */
int batman_skb_recv(struct sk_buff *skb, struct net_device *dev,
	struct packet_type *ptype, struct net_device *orig_dev)
{
	struct batman_packet *batman_packet;
	struct batman_if *batman_if;
	struct net_device_stats *stats;
	int ret;

	skb = skb_share_check(skb, GFP_ATOMIC);

	/* skb was released by skb_share_check() */
	if (!skb)
		goto err_out;

	if (atomic_read(&module_state) != MODULE_ACTIVE)
		goto err_free;

	/* packet should hold at least type and version */
	if (unlikely(skb_headlen(skb) < 2))
		goto err_free;

	/* expect a valid ethernet header here. */
	if (unlikely(skb->mac_len != sizeof(struct ethhdr)
				|| !skb_mac_header(skb)))
		goto err_free;

	batman_if = get_batman_if_by_netdev(skb->dev);
	if (!batman_if)
		goto err_free;

	/* discard frames on not active interfaces */
	if (batman_if->if_status != IF_ACTIVE)
		goto err_free;

	stats = (struct net_device_stats *)dev_get_stats(skb->dev);
	if (stats) {
		stats->rx_packets++;
		stats->rx_bytes += skb->len;
	}

	batman_packet = (struct batman_packet *)skb->data;

	if (batman_packet->version != COMPAT_VERSION) {
		bat_dbg(DBG_BATMAN,
			"Drop packet: incompatible batman version (%i)\n",
			batman_packet->version);
		goto err_free;
	}

	/* all receive handlers return whether they received or reused
	 * the supplied skb. if not, we have to free the skb. */

	switch (batman_packet->packet_type) {
		/* batman originator packet */
	case BAT_PACKET:
		ret = recv_bat_packet(skb, batman_if);
		break;

		/* batman icmp packet */
	case BAT_ICMP:
		ret = recv_icmp_packet(skb);
		break;

		/* unicast packet */
	case BAT_UNICAST:
		ret = recv_unicast_packet(skb);
		break;

		/* broadcast packet */
	case BAT_BCAST:
		ret = recv_bcast_packet(skb);
		break;

		/* vis packet */
	case BAT_VIS:
		ret = recv_vis_packet(skb);
		break;
	default:
		ret = NET_RX_DROP;
	}

	if (ret == NET_RX_DROP)
		kfree_skb(skb);

	/* return NET_RX_SUCCESS in any case as we
	 * most probably dropped the packet for
	 * routing-logical reasons. */

	return NET_RX_SUCCESS;

err_free:
	kfree_skb(skb);
err_out:
	return NET_RX_DROP;
}


struct notifier_block hard_if_notifier = {
	.notifier_call = hard_if_event,
};
