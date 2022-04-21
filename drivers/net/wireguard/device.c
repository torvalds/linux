// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "queueing.h"
#include "socket.h"
#include "timers.h"
#include "device.h"
#include "ratelimiter.h"
#include "peer.h"
#include "messages.h"

#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_arp.h>
#include <linux/icmp.h>
#include <linux/suspend.h>
#include <net/dst_metadata.h>
#include <net/icmp.h>
#include <net/rtnetlink.h>
#include <net/ip_tunnels.h>
#include <net/addrconf.h>

static LIST_HEAD(device_list);

static int wg_open(struct net_device *dev)
{
	struct in_device *dev_v4 = __in_dev_get_rtnl(dev);
	struct inet6_dev *dev_v6 = __in6_dev_get(dev);
	struct wg_device *wg = netdev_priv(dev);
	struct wg_peer *peer;
	int ret;

	if (dev_v4) {
		/* At some point we might put this check near the ip_rt_send_
		 * redirect call of ip_forward in net/ipv4/ip_forward.c, similar
		 * to the current secpath check.
		 */
		IN_DEV_CONF_SET(dev_v4, SEND_REDIRECTS, false);
		IPV4_DEVCONF_ALL(dev_net(dev), SEND_REDIRECTS) = false;
	}
	if (dev_v6)
		dev_v6->cnf.addr_gen_mode = IN6_ADDR_GEN_MODE_NONE;

	mutex_lock(&wg->device_update_lock);
	ret = wg_socket_init(wg, wg->incoming_port);
	if (ret < 0)
		goto out;
	list_for_each_entry(peer, &wg->peer_list, peer_list) {
		wg_packet_send_staged_packets(peer);
		if (peer->persistent_keepalive_interval)
			wg_packet_send_keepalive(peer);
	}
out:
	mutex_unlock(&wg->device_update_lock);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int wg_pm_notification(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct wg_device *wg;
	struct wg_peer *peer;

	/* If the machine is constantly suspending and resuming, as part of
	 * its normal operation rather than as a somewhat rare event, then we
	 * don't actually want to clear keys.
	 */
	if (IS_ENABLED(CONFIG_PM_AUTOSLEEP) || IS_ENABLED(CONFIG_ANDROID))
		return 0;

	if (action != PM_HIBERNATION_PREPARE && action != PM_SUSPEND_PREPARE)
		return 0;

	rtnl_lock();
	list_for_each_entry(wg, &device_list, device_list) {
		mutex_lock(&wg->device_update_lock);
		list_for_each_entry(peer, &wg->peer_list, peer_list) {
			del_timer(&peer->timer_zero_key_material);
			wg_noise_handshake_clear(&peer->handshake);
			wg_noise_keypairs_clear(&peer->keypairs);
		}
		mutex_unlock(&wg->device_update_lock);
	}
	rtnl_unlock();
	rcu_barrier();
	return 0;
}

static struct notifier_block pm_notifier = { .notifier_call = wg_pm_notification };
#endif

static int wg_stop(struct net_device *dev)
{
	struct wg_device *wg = netdev_priv(dev);
	struct wg_peer *peer;
	struct sk_buff *skb;

	mutex_lock(&wg->device_update_lock);
	list_for_each_entry(peer, &wg->peer_list, peer_list) {
		wg_packet_purge_staged_packets(peer);
		wg_timers_stop(peer);
		wg_noise_handshake_clear(&peer->handshake);
		wg_noise_keypairs_clear(&peer->keypairs);
		wg_noise_reset_last_sent_handshake(&peer->last_sent_handshake);
	}
	mutex_unlock(&wg->device_update_lock);
	while ((skb = ptr_ring_consume(&wg->handshake_queue.ring)) != NULL)
		kfree_skb(skb);
	atomic_set(&wg->handshake_queue_len, 0);
	wg_socket_reinit(wg, NULL, NULL);
	return 0;
}

static netdev_tx_t wg_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct wg_device *wg = netdev_priv(dev);
	struct sk_buff_head packets;
	struct wg_peer *peer;
	struct sk_buff *next;
	sa_family_t family;
	u32 mtu;
	int ret;

	if (unlikely(!wg_check_packet_protocol(skb))) {
		ret = -EPROTONOSUPPORT;
		net_dbg_ratelimited("%s: Invalid IP packet\n", dev->name);
		goto err;
	}

	peer = wg_allowedips_lookup_dst(&wg->peer_allowedips, skb);
	if (unlikely(!peer)) {
		ret = -ENOKEY;
		if (skb->protocol == htons(ETH_P_IP))
			net_dbg_ratelimited("%s: No peer has allowed IPs matching %pI4\n",
					    dev->name, &ip_hdr(skb)->daddr);
		else if (skb->protocol == htons(ETH_P_IPV6))
			net_dbg_ratelimited("%s: No peer has allowed IPs matching %pI6\n",
					    dev->name, &ipv6_hdr(skb)->daddr);
		goto err_icmp;
	}

	family = READ_ONCE(peer->endpoint.addr.sa_family);
	if (unlikely(family != AF_INET && family != AF_INET6)) {
		ret = -EDESTADDRREQ;
		net_dbg_ratelimited("%s: No valid endpoint has been configured or discovered for peer %llu\n",
				    dev->name, peer->internal_id);
		goto err_peer;
	}

	mtu = skb_valid_dst(skb) ? dst_mtu(skb_dst(skb)) : dev->mtu;

	__skb_queue_head_init(&packets);
	if (!skb_is_gso(skb)) {
		skb_mark_not_on_list(skb);
	} else {
		struct sk_buff *segs = skb_gso_segment(skb, 0);

		if (IS_ERR(segs)) {
			ret = PTR_ERR(segs);
			goto err_peer;
		}
		dev_kfree_skb(skb);
		skb = segs;
	}

	skb_list_walk_safe(skb, skb, next) {
		skb_mark_not_on_list(skb);

		skb = skb_share_check(skb, GFP_ATOMIC);
		if (unlikely(!skb))
			continue;

		/* We only need to keep the original dst around for icmp,
		 * so at this point we're in a position to drop it.
		 */
		skb_dst_drop(skb);

		PACKET_CB(skb)->mtu = mtu;

		__skb_queue_tail(&packets, skb);
	}

	spin_lock_bh(&peer->staged_packet_queue.lock);
	/* If the queue is getting too big, we start removing the oldest packets
	 * until it's small again. We do this before adding the new packet, so
	 * we don't remove GSO segments that are in excess.
	 */
	while (skb_queue_len(&peer->staged_packet_queue) > MAX_STAGED_PACKETS) {
		dev_kfree_skb(__skb_dequeue(&peer->staged_packet_queue));
		++dev->stats.tx_dropped;
	}
	skb_queue_splice_tail(&packets, &peer->staged_packet_queue);
	spin_unlock_bh(&peer->staged_packet_queue.lock);

	wg_packet_send_staged_packets(peer);

	wg_peer_put(peer);
	return NETDEV_TX_OK;

err_peer:
	wg_peer_put(peer);
err_icmp:
	if (skb->protocol == htons(ETH_P_IP))
		icmp_ndo_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
	else if (skb->protocol == htons(ETH_P_IPV6))
		icmpv6_ndo_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_ADDR_UNREACH, 0);
err:
	++dev->stats.tx_errors;
	kfree_skb(skb);
	return ret;
}

static const struct net_device_ops netdev_ops = {
	.ndo_open		= wg_open,
	.ndo_stop		= wg_stop,
	.ndo_start_xmit		= wg_xmit,
	.ndo_get_stats64	= dev_get_tstats64
};

static void wg_destruct(struct net_device *dev)
{
	struct wg_device *wg = netdev_priv(dev);

	rtnl_lock();
	list_del(&wg->device_list);
	rtnl_unlock();
	mutex_lock(&wg->device_update_lock);
	rcu_assign_pointer(wg->creating_net, NULL);
	wg->incoming_port = 0;
	wg_socket_reinit(wg, NULL, NULL);
	/* The final references are cleared in the below calls to destroy_workqueue. */
	wg_peer_remove_all(wg);
	destroy_workqueue(wg->handshake_receive_wq);
	destroy_workqueue(wg->handshake_send_wq);
	destroy_workqueue(wg->packet_crypt_wq);
	wg_packet_queue_free(&wg->handshake_queue, true);
	wg_packet_queue_free(&wg->decrypt_queue, false);
	wg_packet_queue_free(&wg->encrypt_queue, false);
	rcu_barrier(); /* Wait for all the peers to be actually freed. */
	wg_ratelimiter_uninit();
	memzero_explicit(&wg->static_identity, sizeof(wg->static_identity));
	free_percpu(dev->tstats);
	kvfree(wg->index_hashtable);
	kvfree(wg->peer_hashtable);
	mutex_unlock(&wg->device_update_lock);

	pr_debug("%s: Interface destroyed\n", dev->name);
	free_netdev(dev);
}

static const struct device_type device_type = { .name = KBUILD_MODNAME };

static void wg_setup(struct net_device *dev)
{
	struct wg_device *wg = netdev_priv(dev);
	enum { WG_NETDEV_FEATURES = NETIF_F_HW_CSUM | NETIF_F_RXCSUM |
				    NETIF_F_SG | NETIF_F_GSO |
				    NETIF_F_GSO_SOFTWARE | NETIF_F_HIGHDMA };
	const int overhead = MESSAGE_MINIMUM_LENGTH + sizeof(struct udphdr) +
			     max(sizeof(struct ipv6hdr), sizeof(struct iphdr));

	dev->netdev_ops = &netdev_ops;
	dev->header_ops = &ip_tunnel_header_ops;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->needed_headroom = DATA_PACKET_HEAD_ROOM;
	dev->needed_tailroom = noise_encrypted_len(MESSAGE_PADDING_MULTIPLE);
	dev->type = ARPHRD_NONE;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->features |= NETIF_F_LLTX;
	dev->features |= WG_NETDEV_FEATURES;
	dev->hw_features |= WG_NETDEV_FEATURES;
	dev->hw_enc_features |= WG_NETDEV_FEATURES;
	dev->mtu = ETH_DATA_LEN - overhead;
	dev->max_mtu = round_down(INT_MAX, MESSAGE_PADDING_MULTIPLE) - overhead;

	SET_NETDEV_DEVTYPE(dev, &device_type);

	/* We need to keep the dst around in case of icmp replies. */
	netif_keep_dst(dev);

	memset(wg, 0, sizeof(*wg));
	wg->dev = dev;
}

static int wg_newlink(struct net *src_net, struct net_device *dev,
		      struct nlattr *tb[], struct nlattr *data[],
		      struct netlink_ext_ack *extack)
{
	struct wg_device *wg = netdev_priv(dev);
	int ret = -ENOMEM;

	rcu_assign_pointer(wg->creating_net, src_net);
	init_rwsem(&wg->static_identity.lock);
	mutex_init(&wg->socket_update_lock);
	mutex_init(&wg->device_update_lock);
	wg_allowedips_init(&wg->peer_allowedips);
	wg_cookie_checker_init(&wg->cookie_checker, wg);
	INIT_LIST_HEAD(&wg->peer_list);
	wg->device_update_gen = 1;

	wg->peer_hashtable = wg_pubkey_hashtable_alloc();
	if (!wg->peer_hashtable)
		return ret;

	wg->index_hashtable = wg_index_hashtable_alloc();
	if (!wg->index_hashtable)
		goto err_free_peer_hashtable;

	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		goto err_free_index_hashtable;

	wg->handshake_receive_wq = alloc_workqueue("wg-kex-%s",
			WQ_CPU_INTENSIVE | WQ_FREEZABLE, 0, dev->name);
	if (!wg->handshake_receive_wq)
		goto err_free_tstats;

	wg->handshake_send_wq = alloc_workqueue("wg-kex-%s",
			WQ_UNBOUND | WQ_FREEZABLE, 0, dev->name);
	if (!wg->handshake_send_wq)
		goto err_destroy_handshake_receive;

	wg->packet_crypt_wq = alloc_workqueue("wg-crypt-%s",
			WQ_CPU_INTENSIVE | WQ_MEM_RECLAIM, 0, dev->name);
	if (!wg->packet_crypt_wq)
		goto err_destroy_handshake_send;

	ret = wg_packet_queue_init(&wg->encrypt_queue, wg_packet_encrypt_worker,
				   MAX_QUEUED_PACKETS);
	if (ret < 0)
		goto err_destroy_packet_crypt;

	ret = wg_packet_queue_init(&wg->decrypt_queue, wg_packet_decrypt_worker,
				   MAX_QUEUED_PACKETS);
	if (ret < 0)
		goto err_free_encrypt_queue;

	ret = wg_packet_queue_init(&wg->handshake_queue, wg_packet_handshake_receive_worker,
				   MAX_QUEUED_INCOMING_HANDSHAKES);
	if (ret < 0)
		goto err_free_decrypt_queue;

	ret = wg_ratelimiter_init();
	if (ret < 0)
		goto err_free_handshake_queue;

	ret = register_netdevice(dev);
	if (ret < 0)
		goto err_uninit_ratelimiter;

	list_add(&wg->device_list, &device_list);

	/* We wait until the end to assign priv_destructor, so that
	 * register_netdevice doesn't call it for us if it fails.
	 */
	dev->priv_destructor = wg_destruct;

	pr_debug("%s: Interface created\n", dev->name);
	return ret;

err_uninit_ratelimiter:
	wg_ratelimiter_uninit();
err_free_handshake_queue:
	wg_packet_queue_free(&wg->handshake_queue, false);
err_free_decrypt_queue:
	wg_packet_queue_free(&wg->decrypt_queue, false);
err_free_encrypt_queue:
	wg_packet_queue_free(&wg->encrypt_queue, false);
err_destroy_packet_crypt:
	destroy_workqueue(wg->packet_crypt_wq);
err_destroy_handshake_send:
	destroy_workqueue(wg->handshake_send_wq);
err_destroy_handshake_receive:
	destroy_workqueue(wg->handshake_receive_wq);
err_free_tstats:
	free_percpu(dev->tstats);
err_free_index_hashtable:
	kvfree(wg->index_hashtable);
err_free_peer_hashtable:
	kvfree(wg->peer_hashtable);
	return ret;
}

static struct rtnl_link_ops link_ops __read_mostly = {
	.kind			= KBUILD_MODNAME,
	.priv_size		= sizeof(struct wg_device),
	.setup			= wg_setup,
	.newlink		= wg_newlink,
};

static void wg_netns_pre_exit(struct net *net)
{
	struct wg_device *wg;
	struct wg_peer *peer;

	rtnl_lock();
	list_for_each_entry(wg, &device_list, device_list) {
		if (rcu_access_pointer(wg->creating_net) == net) {
			pr_debug("%s: Creating namespace exiting\n", wg->dev->name);
			netif_carrier_off(wg->dev);
			mutex_lock(&wg->device_update_lock);
			rcu_assign_pointer(wg->creating_net, NULL);
			wg_socket_reinit(wg, NULL, NULL);
			list_for_each_entry(peer, &wg->peer_list, peer_list)
				wg_socket_clear_peer_endpoint_src(peer);
			mutex_unlock(&wg->device_update_lock);
		}
	}
	rtnl_unlock();
}

static struct pernet_operations pernet_ops = {
	.pre_exit = wg_netns_pre_exit
};

int __init wg_device_init(void)
{
	int ret;

#ifdef CONFIG_PM_SLEEP
	ret = register_pm_notifier(&pm_notifier);
	if (ret)
		return ret;
#endif

	ret = register_pernet_device(&pernet_ops);
	if (ret)
		goto error_pm;

	ret = rtnl_link_register(&link_ops);
	if (ret)
		goto error_pernet;

	return 0;

error_pernet:
	unregister_pernet_device(&pernet_ops);
error_pm:
#ifdef CONFIG_PM_SLEEP
	unregister_pm_notifier(&pm_notifier);
#endif
	return ret;
}

void wg_device_uninit(void)
{
	rtnl_link_unregister(&link_ops);
	unregister_pernet_device(&pernet_ops);
#ifdef CONFIG_PM_SLEEP
	unregister_pm_notifier(&pm_notifier);
#endif
	rcu_barrier();
}
