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

/* protect update critical side of if_list - but not the content */
static DEFINE_SPINLOCK(if_list_lock);

static void hardif_free_rcu(struct rcu_head *rcu)
{
	struct batman_if *batman_if;

	batman_if = container_of(rcu, struct batman_if, rcu);
	dev_put(batman_if->net_dev);
	kref_put(&batman_if->refcount, hardif_free_ref);
}

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
	if (batman_if)
		kref_get(&batman_if->refcount);

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
	/* if (net_dev->priv_flags & IFF_BRIDGE_PORT)
		return 0; */

	return 1;
}

static struct batman_if *get_active_batman_if(struct net_device *soft_iface)
{
	struct batman_if *batman_if;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if (batman_if->soft_iface != soft_iface)
			continue;

		if (batman_if->if_status == IF_ACTIVE)
			goto out;
	}

	batman_if = NULL;

out:
	if (batman_if)
		kref_get(&batman_if->refcount);

	rcu_read_unlock();
	return batman_if;
}

static void update_primary_addr(struct bat_priv *bat_priv)
{
	struct vis_packet *vis_packet;

	vis_packet = (struct vis_packet *)
				bat_priv->my_vis_info->skb_packet->data;
	memcpy(vis_packet->vis_orig,
	       bat_priv->primary_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(vis_packet->sender_orig,
	       bat_priv->primary_if->net_dev->dev_addr, ETH_ALEN);
}

static void set_primary_if(struct bat_priv *bat_priv,
			   struct batman_if *batman_if)
{
	struct batman_packet *batman_packet;
	struct batman_if *old_if;

	if (batman_if)
		kref_get(&batman_if->refcount);

	old_if = bat_priv->primary_if;
	bat_priv->primary_if = batman_if;

	if (old_if)
		kref_put(&old_if->refcount, hardif_free_ref);

	if (!bat_priv->primary_if)
		return;

	batman_packet = (struct batman_packet *)(batman_if->packet_buff);
	batman_packet->flags = PRIMARIES_FIRST_HOP;
	batman_packet->ttl = TTL;

	update_primary_addr(bat_priv);

	/***
	 * hacky trick to make sure that we send the HNA information via
	 * our new primary interface
	 */
	atomic_set(&bat_priv->hna_local_changed, 1);
}

static bool hardif_is_iface_up(struct batman_if *batman_if)
{
	if (batman_if->net_dev->flags & IFF_UP)
		return true;

	return false;
}

static void update_mac_addresses(struct batman_if *batman_if)
{
	memcpy(((struct batman_packet *)(batman_if->packet_buff))->orig,
	       batman_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(((struct batman_packet *)(batman_if->packet_buff))->prev_sender,
	       batman_if->net_dev->dev_addr, ETH_ALEN);
}

static void check_known_mac_addr(struct net_device *net_dev)
{
	struct batman_if *batman_if;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if ((batman_if->if_status != IF_ACTIVE) &&
		    (batman_if->if_status != IF_TO_BE_ACTIVATED))
			continue;

		if (batman_if->net_dev == net_dev)
			continue;

		if (!compare_orig(batman_if->net_dev->dev_addr,
				  net_dev->dev_addr))
			continue;

		pr_warning("The newly added mac address (%pM) already exists "
			   "on: %s\n", net_dev->dev_addr,
			   batman_if->net_dev->name);
		pr_warning("It is strongly recommended to keep mac addresses "
			   "unique to avoid problems!\n");
	}
	rcu_read_unlock();
}

int hardif_min_mtu(struct net_device *soft_iface)
{
	struct bat_priv *bat_priv = netdev_priv(soft_iface);
	struct batman_if *batman_if;
	/* allow big frames if all devices are capable to do so
	 * (have MTU > 1500 + BAT_HEADER_LEN) */
	int min_mtu = ETH_DATA_LEN;

	if (atomic_read(&bat_priv->frag_enabled))
		goto out;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if ((batman_if->if_status != IF_ACTIVE) &&
		    (batman_if->if_status != IF_TO_BE_ACTIVATED))
			continue;

		if (batman_if->soft_iface != soft_iface)
			continue;

		min_mtu = MIN(batman_if->net_dev->mtu - BAT_HEADER_LEN,
			      min_mtu);
	}
	rcu_read_unlock();
out:
	return min_mtu;
}

/* adjusts the MTU if a new interface with a smaller MTU appeared. */
void update_min_mtu(struct net_device *soft_iface)
{
	int min_mtu;

	min_mtu = hardif_min_mtu(soft_iface);
	if (soft_iface->mtu != min_mtu)
		soft_iface->mtu = min_mtu;
}

static void hardif_activate_interface(struct batman_if *batman_if)
{
	struct bat_priv *bat_priv;

	if (batman_if->if_status != IF_INACTIVE)
		return;

	bat_priv = netdev_priv(batman_if->soft_iface);

	update_mac_addresses(batman_if);
	batman_if->if_status = IF_TO_BE_ACTIVATED;

	/**
	 * the first active interface becomes our primary interface or
	 * the next active interface after the old primay interface was removed
	 */
	if (!bat_priv->primary_if)
		set_primary_if(bat_priv, batman_if);

	bat_info(batman_if->soft_iface, "Interface activated: %s\n",
		 batman_if->net_dev->name);

	update_min_mtu(batman_if->soft_iface);
	return;
}

static void hardif_deactivate_interface(struct batman_if *batman_if)
{
	if ((batman_if->if_status != IF_ACTIVE) &&
	   (batman_if->if_status != IF_TO_BE_ACTIVATED))
		return;

	batman_if->if_status = IF_INACTIVE;

	bat_info(batman_if->soft_iface, "Interface deactivated: %s\n",
		 batman_if->net_dev->name);

	update_min_mtu(batman_if->soft_iface);
}

int hardif_enable_interface(struct batman_if *batman_if, char *iface_name)
{
	struct bat_priv *bat_priv;
	struct batman_packet *batman_packet;

	if (batman_if->if_status != IF_NOT_IN_USE)
		goto out;

	batman_if->soft_iface = dev_get_by_name(&init_net, iface_name);

	if (!batman_if->soft_iface) {
		batman_if->soft_iface = softif_create(iface_name);

		if (!batman_if->soft_iface)
			goto err;

		/* dev_get_by_name() increases the reference counter for us */
		dev_hold(batman_if->soft_iface);
	}

	bat_priv = netdev_priv(batman_if->soft_iface);
	batman_if->packet_len = BAT_PACKET_LEN;
	batman_if->packet_buff = kmalloc(batman_if->packet_len, GFP_ATOMIC);

	if (!batman_if->packet_buff) {
		bat_err(batman_if->soft_iface, "Can't add interface packet "
			"(%s): out of memory\n", batman_if->net_dev->name);
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

	batman_if->batman_adv_ptype.type = __constant_htons(ETH_P_BATMAN);
	batman_if->batman_adv_ptype.func = batman_skb_recv;
	batman_if->batman_adv_ptype.dev = batman_if->net_dev;
	kref_get(&batman_if->refcount);
	dev_add_pack(&batman_if->batman_adv_ptype);

	atomic_set(&batman_if->seqno, 1);
	atomic_set(&batman_if->frag_seqno, 1);
	bat_info(batman_if->soft_iface, "Adding interface: %s\n",
		 batman_if->net_dev->name);

	if (atomic_read(&bat_priv->frag_enabled) && batman_if->net_dev->mtu <
		ETH_DATA_LEN + BAT_HEADER_LEN)
		bat_info(batman_if->soft_iface,
			"The MTU of interface %s is too small (%i) to handle "
			"the transport of batman-adv packets. Packets going "
			"over this interface will be fragmented on layer2 "
			"which could impact the performance. Setting the MTU "
			"to %zi would solve the problem.\n",
			batman_if->net_dev->name, batman_if->net_dev->mtu,
			ETH_DATA_LEN + BAT_HEADER_LEN);

	if (!atomic_read(&bat_priv->frag_enabled) && batman_if->net_dev->mtu <
		ETH_DATA_LEN + BAT_HEADER_LEN)
		bat_info(batman_if->soft_iface,
			"The MTU of interface %s is too small (%i) to handle "
			"the transport of batman-adv packets. If you experience"
			" problems getting traffic through try increasing the "
			"MTU to %zi.\n",
			batman_if->net_dev->name, batman_if->net_dev->mtu,
			ETH_DATA_LEN + BAT_HEADER_LEN);

	if (hardif_is_iface_up(batman_if))
		hardif_activate_interface(batman_if);
	else
		bat_err(batman_if->soft_iface, "Not using interface %s "
			"(retrying later): interface not active\n",
			batman_if->net_dev->name);

	/* begin scheduling originator messages on that interface */
	schedule_own_packet(batman_if);

out:
	return 0;

err:
	return -ENOMEM;
}

void hardif_disable_interface(struct batman_if *batman_if)
{
	struct bat_priv *bat_priv = netdev_priv(batman_if->soft_iface);

	if (batman_if->if_status == IF_ACTIVE)
		hardif_deactivate_interface(batman_if);

	if (batman_if->if_status != IF_INACTIVE)
		return;

	bat_info(batman_if->soft_iface, "Removing interface: %s\n",
		 batman_if->net_dev->name);
	dev_remove_pack(&batman_if->batman_adv_ptype);
	kref_put(&batman_if->refcount, hardif_free_ref);

	bat_priv->num_ifaces--;
	orig_hash_del_if(batman_if, bat_priv->num_ifaces);

	if (batman_if == bat_priv->primary_if) {
		struct batman_if *new_if;

		new_if = get_active_batman_if(batman_if->soft_iface);
		set_primary_if(bat_priv, new_if);

		if (new_if)
			kref_put(&new_if->refcount, hardif_free_ref);
	}

	kfree(batman_if->packet_buff);
	batman_if->packet_buff = NULL;
	batman_if->if_status = IF_NOT_IN_USE;

	/* delete all references to this batman_if */
	purge_orig_ref(bat_priv);
	purge_outstanding_packets(bat_priv, batman_if);
	dev_put(batman_if->soft_iface);

	/* nobody uses this interface anymore */
	if (!bat_priv->num_ifaces)
		softif_destroy(batman_if->soft_iface);

	batman_if->soft_iface = NULL;
}

static struct batman_if *hardif_add_interface(struct net_device *net_dev)
{
	struct batman_if *batman_if;
	int ret;

	ret = is_valid_iface(net_dev);
	if (ret != 1)
		goto out;

	dev_hold(net_dev);

	batman_if = kmalloc(sizeof(struct batman_if), GFP_ATOMIC);
	if (!batman_if) {
		pr_err("Can't add interface (%s): out of memory\n",
		       net_dev->name);
		goto release_dev;
	}

	ret = sysfs_add_hardif(&batman_if->hardif_obj, net_dev);
	if (ret)
		goto free_if;

	batman_if->if_num = -1;
	batman_if->net_dev = net_dev;
	batman_if->soft_iface = NULL;
	batman_if->if_status = IF_NOT_IN_USE;
	INIT_LIST_HEAD(&batman_if->list);
	kref_init(&batman_if->refcount);

	check_known_mac_addr(batman_if->net_dev);

	spin_lock(&if_list_lock);
	list_add_tail_rcu(&batman_if->list, &if_list);
	spin_unlock(&if_list_lock);

	/* extra reference for return */
	kref_get(&batman_if->refcount);
	return batman_if;

free_if:
	kfree(batman_if);
release_dev:
	dev_put(net_dev);
out:
	return NULL;
}

static void hardif_remove_interface(struct batman_if *batman_if)
{
	/* first deactivate interface */
	if (batman_if->if_status != IF_NOT_IN_USE)
		hardif_disable_interface(batman_if);

	if (batman_if->if_status != IF_NOT_IN_USE)
		return;

	batman_if->if_status = IF_TO_BE_REMOVED;
	sysfs_del_hardif(&batman_if->hardif_obj);
	call_rcu(&batman_if->rcu, hardif_free_rcu);
}

void hardif_remove_interfaces(void)
{
	struct batman_if *batman_if, *batman_if_tmp;
	struct list_head if_queue;

	INIT_LIST_HEAD(&if_queue);

	spin_lock(&if_list_lock);
	list_for_each_entry_safe(batman_if, batman_if_tmp, &if_list, list) {
		list_del_rcu(&batman_if->list);
		list_add_tail(&batman_if->list, &if_queue);
	}
	spin_unlock(&if_list_lock);

	rtnl_lock();
	list_for_each_entry_safe(batman_if, batman_if_tmp, &if_queue, list) {
		hardif_remove_interface(batman_if);
	}
	rtnl_unlock();
}

static int hard_if_event(struct notifier_block *this,
			 unsigned long event, void *ptr)
{
	struct net_device *net_dev = (struct net_device *)ptr;
	struct batman_if *batman_if = get_batman_if_by_netdev(net_dev);
	struct bat_priv *bat_priv;

	if (!batman_if && event == NETDEV_REGISTER)
		batman_if = hardif_add_interface(net_dev);

	if (!batman_if)
		goto out;

	switch (event) {
	case NETDEV_UP:
		hardif_activate_interface(batman_if);
		break;
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
		hardif_deactivate_interface(batman_if);
		break;
	case NETDEV_UNREGISTER:
		spin_lock(&if_list_lock);
		list_del_rcu(&batman_if->list);
		spin_unlock(&if_list_lock);

		hardif_remove_interface(batman_if);
		break;
	case NETDEV_CHANGEMTU:
		if (batman_if->soft_iface)
			update_min_mtu(batman_if->soft_iface);
		break;
	case NETDEV_CHANGEADDR:
		if (batman_if->if_status == IF_NOT_IN_USE)
			goto hardif_put;

		check_known_mac_addr(batman_if->net_dev);
		update_mac_addresses(batman_if);

		bat_priv = netdev_priv(batman_if->soft_iface);
		if (batman_if == bat_priv->primary_if)
			update_primary_addr(bat_priv);
		break;
	default:
		break;
	};

hardif_put:
	kref_put(&batman_if->refcount, hardif_free_ref);
out:
	return NOTIFY_DONE;
}

/* receive a packet with the batman ethertype coming on a hard
 * interface */
int batman_skb_recv(struct sk_buff *skb, struct net_device *dev,
	struct packet_type *ptype, struct net_device *orig_dev)
{
	struct bat_priv *bat_priv;
	struct batman_packet *batman_packet;
	struct batman_if *batman_if;
	int ret;

	batman_if = container_of(ptype, struct batman_if, batman_adv_ptype);
	skb = skb_share_check(skb, GFP_ATOMIC);

	/* skb was released by skb_share_check() */
	if (!skb)
		goto err_out;

	/* packet should hold at least type and version */
	if (unlikely(!pskb_may_pull(skb, 2)))
		goto err_free;

	/* expect a valid ethernet header here. */
	if (unlikely(skb->mac_len != sizeof(struct ethhdr)
				|| !skb_mac_header(skb)))
		goto err_free;

	if (!batman_if->soft_iface)
		goto err_free;

	bat_priv = netdev_priv(batman_if->soft_iface);

	if (atomic_read(&bat_priv->mesh_state) != MESH_ACTIVE)
		goto err_free;

	/* discard frames on not active interfaces */
	if (batman_if->if_status != IF_ACTIVE)
		goto err_free;

	batman_packet = (struct batman_packet *)skb->data;

	if (batman_packet->version != COMPAT_VERSION) {
		bat_dbg(DBG_BATMAN, bat_priv,
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
		ret = recv_icmp_packet(skb, batman_if);
		break;

		/* unicast packet */
	case BAT_UNICAST:
		ret = recv_unicast_packet(skb, batman_if);
		break;

		/* fragmented unicast packet */
	case BAT_UNICAST_FRAG:
		ret = recv_ucast_frag_packet(skb, batman_if);
		break;

		/* broadcast packet */
	case BAT_BCAST:
		ret = recv_bcast_packet(skb, batman_if);
		break;

		/* vis packet */
	case BAT_VIS:
		ret = recv_vis_packet(skb, batman_if);
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
