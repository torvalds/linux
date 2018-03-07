// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/klnds/socklnd/socklnd.c
 *
 * Author: Zach Brown <zab@zabbo.net>
 * Author: Peter J. Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 * Author: Eric Barton <eric@bartonsoftware.com>
 */

#include "socklnd.h"

static struct lnet_lnd the_ksocklnd;
struct ksock_nal_data ksocknal_data;

static struct ksock_interface *
ksocknal_ip2iface(struct lnet_ni *ni, __u32 ip)
{
	struct ksock_net *net = ni->ni_data;
	int i;
	struct ksock_interface *iface;

	for (i = 0; i < net->ksnn_ninterfaces; i++) {
		LASSERT(i < LNET_MAX_INTERFACES);
		iface = &net->ksnn_interfaces[i];

		if (iface->ksni_ipaddr == ip)
			return iface;
	}

	return NULL;
}

static struct ksock_route *
ksocknal_create_route(__u32 ipaddr, int port)
{
	struct ksock_route *route;

	route = kzalloc(sizeof(*route), GFP_NOFS);
	if (!route)
		return NULL;

	atomic_set(&route->ksnr_refcount, 1);
	route->ksnr_peer = NULL;
	route->ksnr_retry_interval = 0;	 /* OK to connect at any time */
	route->ksnr_ipaddr = ipaddr;
	route->ksnr_port = port;
	route->ksnr_scheduled = 0;
	route->ksnr_connecting = 0;
	route->ksnr_connected = 0;
	route->ksnr_deleted = 0;
	route->ksnr_conn_count = 0;
	route->ksnr_share_count = 0;

	return route;
}

void
ksocknal_destroy_route(struct ksock_route *route)
{
	LASSERT(!atomic_read(&route->ksnr_refcount));

	if (route->ksnr_peer)
		ksocknal_peer_decref(route->ksnr_peer);

	kfree(route);
}

static int
ksocknal_create_peer(struct ksock_peer **peerp, struct lnet_ni *ni,
		     struct lnet_process_id id)
{
	int cpt = lnet_cpt_of_nid(id.nid);
	struct ksock_net *net = ni->ni_data;
	struct ksock_peer *peer;

	LASSERT(id.nid != LNET_NID_ANY);
	LASSERT(id.pid != LNET_PID_ANY);
	LASSERT(!in_interrupt());

	peer = kzalloc_cpt(sizeof(*peer), GFP_NOFS, cpt);
	if (!peer)
		return -ENOMEM;

	peer->ksnp_ni = ni;
	peer->ksnp_id = id;
	atomic_set(&peer->ksnp_refcount, 1);   /* 1 ref for caller */
	peer->ksnp_closing = 0;
	peer->ksnp_accepting = 0;
	peer->ksnp_proto = NULL;
	peer->ksnp_last_alive = 0;
	peer->ksnp_zc_next_cookie = SOCKNAL_KEEPALIVE_PING + 1;

	INIT_LIST_HEAD(&peer->ksnp_conns);
	INIT_LIST_HEAD(&peer->ksnp_routes);
	INIT_LIST_HEAD(&peer->ksnp_tx_queue);
	INIT_LIST_HEAD(&peer->ksnp_zc_req_list);
	spin_lock_init(&peer->ksnp_lock);

	spin_lock_bh(&net->ksnn_lock);

	if (net->ksnn_shutdown) {
		spin_unlock_bh(&net->ksnn_lock);

		kfree(peer);
		CERROR("Can't create peer: network shutdown\n");
		return -ESHUTDOWN;
	}

	net->ksnn_npeers++;

	spin_unlock_bh(&net->ksnn_lock);

	*peerp = peer;
	return 0;
}

void
ksocknal_destroy_peer(struct ksock_peer *peer)
{
	struct ksock_net *net = peer->ksnp_ni->ni_data;

	CDEBUG(D_NET, "peer %s %p deleted\n",
	       libcfs_id2str(peer->ksnp_id), peer);

	LASSERT(!atomic_read(&peer->ksnp_refcount));
	LASSERT(!peer->ksnp_accepting);
	LASSERT(list_empty(&peer->ksnp_conns));
	LASSERT(list_empty(&peer->ksnp_routes));
	LASSERT(list_empty(&peer->ksnp_tx_queue));
	LASSERT(list_empty(&peer->ksnp_zc_req_list));

	kfree(peer);

	/*
	 * NB a peer's connections and routes keep a reference on their peer
	 * until they are destroyed, so we can be assured that _all_ state to
	 * do with this peer has been cleaned up when its refcount drops to
	 * zero.
	 */
	spin_lock_bh(&net->ksnn_lock);
	net->ksnn_npeers--;
	spin_unlock_bh(&net->ksnn_lock);
}

struct ksock_peer *
ksocknal_find_peer_locked(struct lnet_ni *ni, struct lnet_process_id id)
{
	struct list_head *peer_list = ksocknal_nid2peerlist(id.nid);
	struct ksock_peer *peer;

	list_for_each_entry(peer, peer_list, ksnp_list) {
		LASSERT(!peer->ksnp_closing);

		if (peer->ksnp_ni != ni)
			continue;

		if (peer->ksnp_id.nid != id.nid ||
		    peer->ksnp_id.pid != id.pid)
			continue;

		CDEBUG(D_NET, "got peer [%p] -> %s (%d)\n",
		       peer, libcfs_id2str(id),
		       atomic_read(&peer->ksnp_refcount));
		return peer;
	}
	return NULL;
}

struct ksock_peer *
ksocknal_find_peer(struct lnet_ni *ni, struct lnet_process_id id)
{
	struct ksock_peer *peer;

	read_lock(&ksocknal_data.ksnd_global_lock);
	peer = ksocknal_find_peer_locked(ni, id);
	if (peer)			/* +1 ref for caller? */
		ksocknal_peer_addref(peer);
	read_unlock(&ksocknal_data.ksnd_global_lock);

	return peer;
}

static void
ksocknal_unlink_peer_locked(struct ksock_peer *peer)
{
	int i;
	__u32 ip;
	struct ksock_interface *iface;

	for (i = 0; i < peer->ksnp_n_passive_ips; i++) {
		LASSERT(i < LNET_MAX_INTERFACES);
		ip = peer->ksnp_passive_ips[i];

		iface = ksocknal_ip2iface(peer->ksnp_ni, ip);
		/*
		 * All IPs in peer->ksnp_passive_ips[] come from the
		 * interface list, therefore the call must succeed.
		 */
		LASSERT(iface);

		CDEBUG(D_NET, "peer=%p iface=%p ksni_nroutes=%d\n",
		       peer, iface, iface->ksni_nroutes);
		iface->ksni_npeers--;
	}

	LASSERT(list_empty(&peer->ksnp_conns));
	LASSERT(list_empty(&peer->ksnp_routes));
	LASSERT(!peer->ksnp_closing);
	peer->ksnp_closing = 1;
	list_del(&peer->ksnp_list);
	/* lose peerlist's ref */
	ksocknal_peer_decref(peer);
}

static int
ksocknal_get_peer_info(struct lnet_ni *ni, int index,
		       struct lnet_process_id *id, __u32 *myip, __u32 *peer_ip,
		       int *port, int *conn_count, int *share_count)
{
	struct ksock_peer *peer;
	struct list_head *ptmp;
	struct ksock_route *route;
	struct list_head *rtmp;
	int i;
	int j;
	int rc = -ENOENT;

	read_lock(&ksocknal_data.ksnd_global_lock);

	for (i = 0; i < ksocknal_data.ksnd_peer_hash_size; i++) {
		list_for_each(ptmp, &ksocknal_data.ksnd_peers[i]) {
			peer = list_entry(ptmp, struct ksock_peer, ksnp_list);

			if (peer->ksnp_ni != ni)
				continue;

			if (!peer->ksnp_n_passive_ips &&
			    list_empty(&peer->ksnp_routes)) {
				if (index-- > 0)
					continue;

				*id = peer->ksnp_id;
				*myip = 0;
				*peer_ip = 0;
				*port = 0;
				*conn_count = 0;
				*share_count = 0;
				rc = 0;
				goto out;
			}

			for (j = 0; j < peer->ksnp_n_passive_ips; j++) {
				if (index-- > 0)
					continue;

				*id = peer->ksnp_id;
				*myip = peer->ksnp_passive_ips[j];
				*peer_ip = 0;
				*port = 0;
				*conn_count = 0;
				*share_count = 0;
				rc = 0;
				goto out;
			}

			list_for_each(rtmp, &peer->ksnp_routes) {
				if (index-- > 0)
					continue;

				route = list_entry(rtmp, struct ksock_route,
						   ksnr_list);

				*id = peer->ksnp_id;
				*myip = route->ksnr_myipaddr;
				*peer_ip = route->ksnr_ipaddr;
				*port = route->ksnr_port;
				*conn_count = route->ksnr_conn_count;
				*share_count = route->ksnr_share_count;
				rc = 0;
				goto out;
			}
		}
	}
 out:
	read_unlock(&ksocknal_data.ksnd_global_lock);
	return rc;
}

static void
ksocknal_associate_route_conn_locked(struct ksock_route *route,
				     struct ksock_conn *conn)
{
	struct ksock_peer *peer = route->ksnr_peer;
	int type = conn->ksnc_type;
	struct ksock_interface *iface;

	conn->ksnc_route = route;
	ksocknal_route_addref(route);

	if (route->ksnr_myipaddr != conn->ksnc_myipaddr) {
		if (!route->ksnr_myipaddr) {
			/* route wasn't bound locally yet (the initial route) */
			CDEBUG(D_NET, "Binding %s %pI4h to %pI4h\n",
			       libcfs_id2str(peer->ksnp_id),
			       &route->ksnr_ipaddr,
			       &conn->ksnc_myipaddr);
		} else {
			CDEBUG(D_NET, "Rebinding %s %pI4h from %pI4h to %pI4h\n",
			       libcfs_id2str(peer->ksnp_id),
			       &route->ksnr_ipaddr,
			       &route->ksnr_myipaddr,
			       &conn->ksnc_myipaddr);

			iface = ksocknal_ip2iface(route->ksnr_peer->ksnp_ni,
						  route->ksnr_myipaddr);
			if (iface)
				iface->ksni_nroutes--;
		}
		route->ksnr_myipaddr = conn->ksnc_myipaddr;
		iface = ksocknal_ip2iface(route->ksnr_peer->ksnp_ni,
					  route->ksnr_myipaddr);
		if (iface)
			iface->ksni_nroutes++;
	}

	route->ksnr_connected |= (1 << type);
	route->ksnr_conn_count++;

	/*
	 * Successful connection => further attempts can
	 * proceed immediately
	 */
	route->ksnr_retry_interval = 0;
}

static void
ksocknal_add_route_locked(struct ksock_peer *peer, struct ksock_route *route)
{
	struct list_head *tmp;
	struct ksock_conn *conn;
	struct ksock_route *route2;

	LASSERT(!peer->ksnp_closing);
	LASSERT(!route->ksnr_peer);
	LASSERT(!route->ksnr_scheduled);
	LASSERT(!route->ksnr_connecting);
	LASSERT(!route->ksnr_connected);

	/* LASSERT(unique) */
	list_for_each(tmp, &peer->ksnp_routes) {
		route2 = list_entry(tmp, struct ksock_route, ksnr_list);

		if (route2->ksnr_ipaddr == route->ksnr_ipaddr) {
			CERROR("Duplicate route %s %pI4h\n",
			       libcfs_id2str(peer->ksnp_id),
			       &route->ksnr_ipaddr);
			LBUG();
		}
	}

	route->ksnr_peer = peer;
	ksocknal_peer_addref(peer);
	/* peer's routelist takes over my ref on 'route' */
	list_add_tail(&route->ksnr_list, &peer->ksnp_routes);

	list_for_each(tmp, &peer->ksnp_conns) {
		conn = list_entry(tmp, struct ksock_conn, ksnc_list);

		if (conn->ksnc_ipaddr != route->ksnr_ipaddr)
			continue;

		ksocknal_associate_route_conn_locked(route, conn);
		/* keep going (typed routes) */
	}
}

static void
ksocknal_del_route_locked(struct ksock_route *route)
{
	struct ksock_peer *peer = route->ksnr_peer;
	struct ksock_interface *iface;
	struct ksock_conn *conn;
	struct list_head *ctmp;
	struct list_head *cnxt;

	LASSERT(!route->ksnr_deleted);

	/* Close associated conns */
	list_for_each_safe(ctmp, cnxt, &peer->ksnp_conns) {
		conn = list_entry(ctmp, struct ksock_conn, ksnc_list);

		if (conn->ksnc_route != route)
			continue;

		ksocknal_close_conn_locked(conn, 0);
	}

	if (route->ksnr_myipaddr) {
		iface = ksocknal_ip2iface(route->ksnr_peer->ksnp_ni,
					  route->ksnr_myipaddr);
		if (iface)
			iface->ksni_nroutes--;
	}

	route->ksnr_deleted = 1;
	list_del(&route->ksnr_list);
	ksocknal_route_decref(route);	     /* drop peer's ref */

	if (list_empty(&peer->ksnp_routes) &&
	    list_empty(&peer->ksnp_conns)) {
		/*
		 * I've just removed the last route to a peer with no active
		 * connections
		 */
		ksocknal_unlink_peer_locked(peer);
	}
}

int
ksocknal_add_peer(struct lnet_ni *ni, struct lnet_process_id id, __u32 ipaddr,
		  int port)
{
	struct ksock_peer *peer;
	struct ksock_peer *peer2;
	struct ksock_route *route;
	struct ksock_route *route2;
	int rc;

	if (id.nid == LNET_NID_ANY ||
	    id.pid == LNET_PID_ANY)
		return -EINVAL;

	/* Have a brand new peer ready... */
	rc = ksocknal_create_peer(&peer, ni, id);
	if (rc)
		return rc;

	route = ksocknal_create_route(ipaddr, port);
	if (!route) {
		ksocknal_peer_decref(peer);
		return -ENOMEM;
	}

	write_lock_bh(&ksocknal_data.ksnd_global_lock);

	/* always called with a ref on ni, so shutdown can't have started */
	LASSERT(!((struct ksock_net *)ni->ni_data)->ksnn_shutdown);

	peer2 = ksocknal_find_peer_locked(ni, id);
	if (peer2) {
		ksocknal_peer_decref(peer);
		peer = peer2;
	} else {
		/* peer table takes my ref on peer */
		list_add_tail(&peer->ksnp_list,
			      ksocknal_nid2peerlist(id.nid));
	}

	list_for_each_entry(route2, &peer->ksnp_routes, ksnr_list) {
		if (route2->ksnr_ipaddr == ipaddr) {
			/* Route already exists, use the old one */
			ksocknal_route_decref(route);
			route2->ksnr_share_count++;
			goto out;
		}
	}
	/* Route doesn't already exist, add the new one */
	ksocknal_add_route_locked(peer, route);
	route->ksnr_share_count++;
out:
	write_unlock_bh(&ksocknal_data.ksnd_global_lock);

	return 0;
}

static void
ksocknal_del_peer_locked(struct ksock_peer *peer, __u32 ip)
{
	struct ksock_conn *conn;
	struct ksock_route *route;
	struct list_head *tmp;
	struct list_head *nxt;
	int nshared;

	LASSERT(!peer->ksnp_closing);

	/* Extra ref prevents peer disappearing until I'm done with it */
	ksocknal_peer_addref(peer);

	list_for_each_safe(tmp, nxt, &peer->ksnp_routes) {
		route = list_entry(tmp, struct ksock_route, ksnr_list);

		/* no match */
		if (!(!ip || route->ksnr_ipaddr == ip))
			continue;

		route->ksnr_share_count = 0;
		/* This deletes associated conns too */
		ksocknal_del_route_locked(route);
	}

	nshared = 0;
	list_for_each_safe(tmp, nxt, &peer->ksnp_routes) {
		route = list_entry(tmp, struct ksock_route, ksnr_list);
		nshared += route->ksnr_share_count;
	}

	if (!nshared) {
		/*
		 * remove everything else if there are no explicit entries
		 * left
		 */
		list_for_each_safe(tmp, nxt, &peer->ksnp_routes) {
			route = list_entry(tmp, struct ksock_route, ksnr_list);

			/* we should only be removing auto-entries */
			LASSERT(!route->ksnr_share_count);
			ksocknal_del_route_locked(route);
		}

		list_for_each_safe(tmp, nxt, &peer->ksnp_conns) {
			conn = list_entry(tmp, struct ksock_conn, ksnc_list);

			ksocknal_close_conn_locked(conn, 0);
		}
	}

	ksocknal_peer_decref(peer);
	/* NB peer unlinks itself when last conn/route is removed */
}

static int
ksocknal_del_peer(struct lnet_ni *ni, struct lnet_process_id id, __u32 ip)
{
	LIST_HEAD(zombies);
	struct list_head *ptmp;
	struct list_head *pnxt;
	struct ksock_peer *peer;
	int lo;
	int hi;
	int i;
	int rc = -ENOENT;

	write_lock_bh(&ksocknal_data.ksnd_global_lock);

	if (id.nid != LNET_NID_ANY) {
		lo = (int)(ksocknal_nid2peerlist(id.nid) - ksocknal_data.ksnd_peers);
		hi = (int)(ksocknal_nid2peerlist(id.nid) - ksocknal_data.ksnd_peers);
	} else {
		lo = 0;
		hi = ksocknal_data.ksnd_peer_hash_size - 1;
	}

	for (i = lo; i <= hi; i++) {
		list_for_each_safe(ptmp, pnxt, &ksocknal_data.ksnd_peers[i]) {
			peer = list_entry(ptmp, struct ksock_peer, ksnp_list);

			if (peer->ksnp_ni != ni)
				continue;

			if (!((id.nid == LNET_NID_ANY || peer->ksnp_id.nid == id.nid) &&
			      (id.pid == LNET_PID_ANY || peer->ksnp_id.pid == id.pid)))
				continue;

			ksocknal_peer_addref(peer);     /* a ref for me... */

			ksocknal_del_peer_locked(peer, ip);

			if (peer->ksnp_closing &&
			    !list_empty(&peer->ksnp_tx_queue)) {
				LASSERT(list_empty(&peer->ksnp_conns));
				LASSERT(list_empty(&peer->ksnp_routes));

				list_splice_init(&peer->ksnp_tx_queue,
						 &zombies);
			}

			ksocknal_peer_decref(peer);     /* ...till here */

			rc = 0;		 /* matched! */
		}
	}

	write_unlock_bh(&ksocknal_data.ksnd_global_lock);

	ksocknal_txlist_done(ni, &zombies, 1);

	return rc;
}

static struct ksock_conn *
ksocknal_get_conn_by_idx(struct lnet_ni *ni, int index)
{
	struct ksock_peer *peer;
	struct list_head *ptmp;
	struct ksock_conn *conn;
	struct list_head *ctmp;
	int i;

	read_lock(&ksocknal_data.ksnd_global_lock);

	for (i = 0; i < ksocknal_data.ksnd_peer_hash_size; i++) {
		list_for_each(ptmp, &ksocknal_data.ksnd_peers[i]) {
			peer = list_entry(ptmp, struct ksock_peer, ksnp_list);

			LASSERT(!peer->ksnp_closing);

			if (peer->ksnp_ni != ni)
				continue;

			list_for_each(ctmp, &peer->ksnp_conns) {
				if (index-- > 0)
					continue;

				conn = list_entry(ctmp, struct ksock_conn,
						  ksnc_list);
				ksocknal_conn_addref(conn);
				read_unlock(&ksocknal_data.ksnd_global_lock);
				return conn;
			}
		}
	}

	read_unlock(&ksocknal_data.ksnd_global_lock);
	return NULL;
}

static struct ksock_sched *
ksocknal_choose_scheduler_locked(unsigned int cpt)
{
	struct ksock_sched_info	*info = ksocknal_data.ksnd_sched_info[cpt];
	struct ksock_sched *sched;
	int i;

	LASSERT(info->ksi_nthreads > 0);

	sched = &info->ksi_scheds[0];
	/*
	 * NB: it's safe so far, but info->ksi_nthreads could be changed
	 * at runtime when we have dynamic LNet configuration, then we
	 * need to take care of this.
	 */
	for (i = 1; i < info->ksi_nthreads; i++) {
		if (sched->kss_nconns > info->ksi_scheds[i].kss_nconns)
			sched = &info->ksi_scheds[i];
	}

	return sched;
}

static int
ksocknal_local_ipvec(struct lnet_ni *ni, __u32 *ipaddrs)
{
	struct ksock_net *net = ni->ni_data;
	int i;
	int nip;

	read_lock(&ksocknal_data.ksnd_global_lock);

	nip = net->ksnn_ninterfaces;
	LASSERT(nip <= LNET_MAX_INTERFACES);

	/*
	 * Only offer interfaces for additional connections if I have
	 * more than one.
	 */
	if (nip < 2) {
		read_unlock(&ksocknal_data.ksnd_global_lock);
		return 0;
	}

	for (i = 0; i < nip; i++) {
		ipaddrs[i] = net->ksnn_interfaces[i].ksni_ipaddr;
		LASSERT(ipaddrs[i]);
	}

	read_unlock(&ksocknal_data.ksnd_global_lock);
	return nip;
}

static int
ksocknal_match_peerip(struct ksock_interface *iface, __u32 *ips, int nips)
{
	int best_netmatch = 0;
	int best_xor      = 0;
	int best	  = -1;
	int this_xor;
	int this_netmatch;
	int i;

	for (i = 0; i < nips; i++) {
		if (!ips[i])
			continue;

		this_xor = ips[i] ^ iface->ksni_ipaddr;
		this_netmatch = !(this_xor & iface->ksni_netmask) ? 1 : 0;

		if (!(best < 0 ||
		      best_netmatch < this_netmatch ||
		      (best_netmatch == this_netmatch &&
		       best_xor > this_xor)))
			continue;

		best = i;
		best_netmatch = this_netmatch;
		best_xor = this_xor;
	}

	LASSERT(best >= 0);
	return best;
}

static int
ksocknal_select_ips(struct ksock_peer *peer, __u32 *peerips, int n_peerips)
{
	rwlock_t *global_lock = &ksocknal_data.ksnd_global_lock;
	struct ksock_net *net = peer->ksnp_ni->ni_data;
	struct ksock_interface *iface;
	struct ksock_interface *best_iface;
	int n_ips;
	int i;
	int j;
	int k;
	__u32 ip;
	__u32 xor;
	int this_netmatch;
	int best_netmatch;
	int best_npeers;

	/*
	 * CAVEAT EMPTOR: We do all our interface matching with an
	 * exclusive hold of global lock at IRQ priority.  We're only
	 * expecting to be dealing with small numbers of interfaces, so the
	 * O(n**3)-ness shouldn't matter
	 */
	/*
	 * Also note that I'm not going to return more than n_peerips
	 * interfaces, even if I have more myself
	 */
	write_lock_bh(global_lock);

	LASSERT(n_peerips <= LNET_MAX_INTERFACES);
	LASSERT(net->ksnn_ninterfaces <= LNET_MAX_INTERFACES);

	/*
	 * Only match interfaces for additional connections
	 * if I have > 1 interface
	 */
	n_ips = (net->ksnn_ninterfaces < 2) ? 0 :
		min(n_peerips, net->ksnn_ninterfaces);

	for (i = 0; peer->ksnp_n_passive_ips < n_ips; i++) {
		/*	      ^ yes really... */

		/*
		 * If we have any new interfaces, first tick off all the
		 * peer IPs that match old interfaces, then choose new
		 * interfaces to match the remaining peer IPS.
		 * We don't forget interfaces we've stopped using; we might
		 * start using them again...
		 */
		if (i < peer->ksnp_n_passive_ips) {
			/* Old interface. */
			ip = peer->ksnp_passive_ips[i];
			best_iface = ksocknal_ip2iface(peer->ksnp_ni, ip);

			/* peer passive ips are kept up to date */
			LASSERT(best_iface);
		} else {
			/* choose a new interface */
			LASSERT(i == peer->ksnp_n_passive_ips);

			best_iface = NULL;
			best_netmatch = 0;
			best_npeers = 0;

			for (j = 0; j < net->ksnn_ninterfaces; j++) {
				iface = &net->ksnn_interfaces[j];
				ip = iface->ksni_ipaddr;

				for (k = 0; k < peer->ksnp_n_passive_ips; k++)
					if (peer->ksnp_passive_ips[k] == ip)
						break;

				if (k < peer->ksnp_n_passive_ips) /* using it already */
					continue;

				k = ksocknal_match_peerip(iface, peerips,
							  n_peerips);
				xor = ip ^ peerips[k];
				this_netmatch = !(xor & iface->ksni_netmask) ? 1 : 0;

				if (!(!best_iface ||
				      best_netmatch < this_netmatch ||
				      (best_netmatch == this_netmatch &&
				       best_npeers > iface->ksni_npeers)))
					continue;

				best_iface = iface;
				best_netmatch = this_netmatch;
				best_npeers = iface->ksni_npeers;
			}

			LASSERT(best_iface);

			best_iface->ksni_npeers++;
			ip = best_iface->ksni_ipaddr;
			peer->ksnp_passive_ips[i] = ip;
			peer->ksnp_n_passive_ips = i + 1;
		}

		/* mark the best matching peer IP used */
		j = ksocknal_match_peerip(best_iface, peerips, n_peerips);
		peerips[j] = 0;
	}

	/* Overwrite input peer IP addresses */
	memcpy(peerips, peer->ksnp_passive_ips, n_ips * sizeof(*peerips));

	write_unlock_bh(global_lock);

	return n_ips;
}

static void
ksocknal_create_routes(struct ksock_peer *peer, int port,
		       __u32 *peer_ipaddrs, int npeer_ipaddrs)
{
	struct ksock_route *newroute = NULL;
	rwlock_t *global_lock = &ksocknal_data.ksnd_global_lock;
	struct lnet_ni *ni = peer->ksnp_ni;
	struct ksock_net *net = ni->ni_data;
	struct list_head *rtmp;
	struct ksock_route *route;
	struct ksock_interface *iface;
	struct ksock_interface *best_iface;
	int best_netmatch;
	int this_netmatch;
	int best_nroutes;
	int i;
	int j;

	/*
	 * CAVEAT EMPTOR: We do all our interface matching with an
	 * exclusive hold of global lock at IRQ priority.  We're only
	 * expecting to be dealing with small numbers of interfaces, so the
	 * O(n**3)-ness here shouldn't matter
	 */
	write_lock_bh(global_lock);

	if (net->ksnn_ninterfaces < 2) {
		/*
		 * Only create additional connections
		 * if I have > 1 interface
		 */
		write_unlock_bh(global_lock);
		return;
	}

	LASSERT(npeer_ipaddrs <= LNET_MAX_INTERFACES);

	for (i = 0; i < npeer_ipaddrs; i++) {
		if (newroute) {
			newroute->ksnr_ipaddr = peer_ipaddrs[i];
		} else {
			write_unlock_bh(global_lock);

			newroute = ksocknal_create_route(peer_ipaddrs[i], port);
			if (!newroute)
				return;

			write_lock_bh(global_lock);
		}

		if (peer->ksnp_closing) {
			/* peer got closed under me */
			break;
		}

		/* Already got a route? */
		route = NULL;
		list_for_each(rtmp, &peer->ksnp_routes) {
			route = list_entry(rtmp, struct ksock_route, ksnr_list);

			if (route->ksnr_ipaddr == newroute->ksnr_ipaddr)
				break;

			route = NULL;
		}
		if (route)
			continue;

		best_iface = NULL;
		best_nroutes = 0;
		best_netmatch = 0;

		LASSERT(net->ksnn_ninterfaces <= LNET_MAX_INTERFACES);

		/* Select interface to connect from */
		for (j = 0; j < net->ksnn_ninterfaces; j++) {
			iface = &net->ksnn_interfaces[j];

			/* Using this interface already? */
			list_for_each(rtmp, &peer->ksnp_routes) {
				route = list_entry(rtmp, struct ksock_route,
						   ksnr_list);

				if (route->ksnr_myipaddr == iface->ksni_ipaddr)
					break;

				route = NULL;
			}
			if (route)
				continue;

			this_netmatch = (!((iface->ksni_ipaddr ^
					   newroute->ksnr_ipaddr) &
					   iface->ksni_netmask)) ? 1 : 0;

			if (!(!best_iface ||
			      best_netmatch < this_netmatch ||
			      (best_netmatch == this_netmatch &&
			       best_nroutes > iface->ksni_nroutes)))
				continue;

			best_iface = iface;
			best_netmatch = this_netmatch;
			best_nroutes = iface->ksni_nroutes;
		}

		if (!best_iface)
			continue;

		newroute->ksnr_myipaddr = best_iface->ksni_ipaddr;
		best_iface->ksni_nroutes++;

		ksocknal_add_route_locked(peer, newroute);
		newroute = NULL;
	}

	write_unlock_bh(global_lock);
	if (newroute)
		ksocknal_route_decref(newroute);
}

int
ksocknal_accept(struct lnet_ni *ni, struct socket *sock)
{
	struct ksock_connreq *cr;
	int rc;
	__u32 peer_ip;
	int peer_port;

	rc = lnet_sock_getaddr(sock, 1, &peer_ip, &peer_port);
	LASSERT(!rc);		      /* we succeeded before */

	cr = kzalloc(sizeof(*cr), GFP_NOFS);
	if (!cr) {
		LCONSOLE_ERROR_MSG(0x12f, "Dropping connection request from %pI4h: memory exhausted\n",
				   &peer_ip);
		return -ENOMEM;
	}

	lnet_ni_addref(ni);
	cr->ksncr_ni   = ni;
	cr->ksncr_sock = sock;

	spin_lock_bh(&ksocknal_data.ksnd_connd_lock);

	list_add_tail(&cr->ksncr_list, &ksocknal_data.ksnd_connd_connreqs);
	wake_up(&ksocknal_data.ksnd_connd_waitq);

	spin_unlock_bh(&ksocknal_data.ksnd_connd_lock);
	return 0;
}

static int
ksocknal_connecting(struct ksock_peer *peer, __u32 ipaddr)
{
	struct ksock_route *route;

	list_for_each_entry(route, &peer->ksnp_routes, ksnr_list) {
		if (route->ksnr_ipaddr == ipaddr)
			return route->ksnr_connecting;
	}
	return 0;
}

int
ksocknal_create_conn(struct lnet_ni *ni, struct ksock_route *route,
		     struct socket *sock, int type)
{
	rwlock_t *global_lock = &ksocknal_data.ksnd_global_lock;
	LIST_HEAD(zombies);
	struct lnet_process_id peerid;
	struct list_head *tmp;
	__u64 incarnation;
	struct ksock_conn *conn;
	struct ksock_conn *conn2;
	struct ksock_peer *peer = NULL;
	struct ksock_peer *peer2;
	struct ksock_sched *sched;
	struct ksock_hello_msg *hello;
	int cpt;
	struct ksock_tx *tx;
	struct ksock_tx *txtmp;
	int rc;
	int active;
	char *warn = NULL;

	active = !!route;

	LASSERT(active == (type != SOCKLND_CONN_NONE));

	conn = kzalloc(sizeof(*conn), GFP_NOFS);
	if (!conn) {
		rc = -ENOMEM;
		goto failed_0;
	}

	conn->ksnc_peer = NULL;
	conn->ksnc_route = NULL;
	conn->ksnc_sock = sock;
	/*
	 * 2 ref, 1 for conn, another extra ref prevents socket
	 * being closed before establishment of connection
	 */
	atomic_set(&conn->ksnc_sock_refcount, 2);
	conn->ksnc_type = type;
	ksocknal_lib_save_callback(sock, conn);
	atomic_set(&conn->ksnc_conn_refcount, 1); /* 1 ref for me */

	conn->ksnc_rx_ready = 0;
	conn->ksnc_rx_scheduled = 0;

	INIT_LIST_HEAD(&conn->ksnc_tx_queue);
	conn->ksnc_tx_ready = 0;
	conn->ksnc_tx_scheduled = 0;
	conn->ksnc_tx_carrier = NULL;
	atomic_set(&conn->ksnc_tx_nob, 0);

	hello = kvzalloc(offsetof(struct ksock_hello_msg,
				  kshm_ips[LNET_MAX_INTERFACES]),
			 GFP_KERNEL);
	if (!hello) {
		rc = -ENOMEM;
		goto failed_1;
	}

	/* stash conn's local and remote addrs */
	rc = ksocknal_lib_get_conn_addrs(conn);
	if (rc)
		goto failed_1;

	/*
	 * Find out/confirm peer's NID and connection type and get the
	 * vector of interfaces she's willing to let me connect to.
	 * Passive connections use the listener timeout since the peer sends
	 * eagerly
	 */
	if (active) {
		peer = route->ksnr_peer;
		LASSERT(ni == peer->ksnp_ni);

		/* Active connection sends HELLO eagerly */
		hello->kshm_nips = ksocknal_local_ipvec(ni, hello->kshm_ips);
		peerid = peer->ksnp_id;

		write_lock_bh(global_lock);
		conn->ksnc_proto = peer->ksnp_proto;
		write_unlock_bh(global_lock);

		if (!conn->ksnc_proto) {
			conn->ksnc_proto = &ksocknal_protocol_v3x;
#if SOCKNAL_VERSION_DEBUG
			if (*ksocknal_tunables.ksnd_protocol == 2)
				conn->ksnc_proto = &ksocknal_protocol_v2x;
			else if (*ksocknal_tunables.ksnd_protocol == 1)
				conn->ksnc_proto = &ksocknal_protocol_v1x;
#endif
		}

		rc = ksocknal_send_hello(ni, conn, peerid.nid, hello);
		if (rc)
			goto failed_1;
	} else {
		peerid.nid = LNET_NID_ANY;
		peerid.pid = LNET_PID_ANY;

		/* Passive, get protocol from peer */
		conn->ksnc_proto = NULL;
	}

	rc = ksocknal_recv_hello(ni, conn, hello, &peerid, &incarnation);
	if (rc < 0)
		goto failed_1;

	LASSERT(!rc || active);
	LASSERT(conn->ksnc_proto);
	LASSERT(peerid.nid != LNET_NID_ANY);

	cpt = lnet_cpt_of_nid(peerid.nid);

	if (active) {
		ksocknal_peer_addref(peer);
		write_lock_bh(global_lock);
	} else {
		rc = ksocknal_create_peer(&peer, ni, peerid);
		if (rc)
			goto failed_1;

		write_lock_bh(global_lock);

		/* called with a ref on ni, so shutdown can't have started */
		LASSERT(!((struct ksock_net *)ni->ni_data)->ksnn_shutdown);

		peer2 = ksocknal_find_peer_locked(ni, peerid);
		if (!peer2) {
			/*
			 * NB this puts an "empty" peer in the peer
			 * table (which takes my ref)
			 */
			list_add_tail(&peer->ksnp_list,
				      ksocknal_nid2peerlist(peerid.nid));
		} else {
			ksocknal_peer_decref(peer);
			peer = peer2;
		}

		/* +1 ref for me */
		ksocknal_peer_addref(peer);
		peer->ksnp_accepting++;

		/*
		 * Am I already connecting to this guy?  Resolve in
		 * favour of higher NID...
		 */
		if (peerid.nid < ni->ni_nid &&
		    ksocknal_connecting(peer, conn->ksnc_ipaddr)) {
			rc = EALREADY;
			warn = "connection race resolution";
			goto failed_2;
		}
	}

	if (peer->ksnp_closing ||
	    (active && route->ksnr_deleted)) {
		/* peer/route got closed under me */
		rc = -ESTALE;
		warn = "peer/route removed";
		goto failed_2;
	}

	if (!peer->ksnp_proto) {
		/*
		 * Never connected before.
		 * NB recv_hello may have returned EPROTO to signal my peer
		 * wants a different protocol than the one I asked for.
		 */
		LASSERT(list_empty(&peer->ksnp_conns));

		peer->ksnp_proto = conn->ksnc_proto;
		peer->ksnp_incarnation = incarnation;
	}

	if (peer->ksnp_proto != conn->ksnc_proto ||
	    peer->ksnp_incarnation != incarnation) {
		/* Peer rebooted or I've got the wrong protocol version */
		ksocknal_close_peer_conns_locked(peer, 0, 0);

		peer->ksnp_proto = NULL;
		rc = ESTALE;
		warn = peer->ksnp_incarnation != incarnation ?
		       "peer rebooted" :
		       "wrong proto version";
		goto failed_2;
	}

	switch (rc) {
	default:
		LBUG();
	case 0:
		break;
	case EALREADY:
		warn = "lost conn race";
		goto failed_2;
	case EPROTO:
		warn = "retry with different protocol version";
		goto failed_2;
	}

	/*
	 * Refuse to duplicate an existing connection, unless this is a
	 * loopback connection
	 */
	if (conn->ksnc_ipaddr != conn->ksnc_myipaddr) {
		list_for_each(tmp, &peer->ksnp_conns) {
			conn2 = list_entry(tmp, struct ksock_conn, ksnc_list);

			if (conn2->ksnc_ipaddr != conn->ksnc_ipaddr ||
			    conn2->ksnc_myipaddr != conn->ksnc_myipaddr ||
			    conn2->ksnc_type != conn->ksnc_type)
				continue;

			/*
			 * Reply on a passive connection attempt so the peer
			 * realises we're connected.
			 */
			LASSERT(!rc);
			if (!active)
				rc = EALREADY;

			warn = "duplicate";
			goto failed_2;
		}
	}

	/*
	 * If the connection created by this route didn't bind to the IP
	 * address the route connected to, the connection/route matching
	 * code below probably isn't going to work.
	 */
	if (active &&
	    route->ksnr_ipaddr != conn->ksnc_ipaddr) {
		CERROR("Route %s %pI4h connected to %pI4h\n",
		       libcfs_id2str(peer->ksnp_id),
		       &route->ksnr_ipaddr,
		       &conn->ksnc_ipaddr);
	}

	/*
	 * Search for a route corresponding to the new connection and
	 * create an association.  This allows incoming connections created
	 * by routes in my peer to match my own route entries so I don't
	 * continually create duplicate routes.
	 */
	list_for_each(tmp, &peer->ksnp_routes) {
		route = list_entry(tmp, struct ksock_route, ksnr_list);

		if (route->ksnr_ipaddr != conn->ksnc_ipaddr)
			continue;

		ksocknal_associate_route_conn_locked(route, conn);
		break;
	}

	conn->ksnc_peer = peer;		 /* conn takes my ref on peer */
	peer->ksnp_last_alive = cfs_time_current();
	peer->ksnp_send_keepalive = 0;
	peer->ksnp_error = 0;

	sched = ksocknal_choose_scheduler_locked(cpt);
	sched->kss_nconns++;
	conn->ksnc_scheduler = sched;

	conn->ksnc_tx_last_post = cfs_time_current();
	/* Set the deadline for the outgoing HELLO to drain */
	conn->ksnc_tx_bufnob = sock->sk->sk_wmem_queued;
	conn->ksnc_tx_deadline = cfs_time_shift(*ksocknal_tunables.ksnd_timeout);
	mb();   /* order with adding to peer's conn list */

	list_add(&conn->ksnc_list, &peer->ksnp_conns);
	ksocknal_conn_addref(conn);

	ksocknal_new_packet(conn, 0);

	conn->ksnc_zc_capable = ksocknal_lib_zc_capable(conn);

	/* Take packets blocking for this connection. */
	list_for_each_entry_safe(tx, txtmp, &peer->ksnp_tx_queue, tx_list) {
		int match = conn->ksnc_proto->pro_match_tx(conn, tx,
							   tx->tx_nonblk);

		if (match == SOCKNAL_MATCH_NO)
			continue;

		list_del(&tx->tx_list);
		ksocknal_queue_tx_locked(tx, conn);
	}

	write_unlock_bh(global_lock);

	/*
	 * We've now got a new connection.  Any errors from here on are just
	 * like "normal" comms errors and we close the connection normally.
	 * NB (a) we still have to send the reply HELLO for passive
	 *	connections,
	 *    (b) normal I/O on the conn is blocked until I setup and call the
	 *	socket callbacks.
	 */
	CDEBUG(D_NET, "New conn %s p %d.x %pI4h -> %pI4h/%d incarnation:%lld sched[%d:%d]\n",
	       libcfs_id2str(peerid), conn->ksnc_proto->pro_version,
	       &conn->ksnc_myipaddr, &conn->ksnc_ipaddr,
	       conn->ksnc_port, incarnation, cpt,
	       (int)(sched - &sched->kss_info->ksi_scheds[0]));

	if (active) {
		/* additional routes after interface exchange? */
		ksocknal_create_routes(peer, conn->ksnc_port,
				       hello->kshm_ips, hello->kshm_nips);
	} else {
		hello->kshm_nips = ksocknal_select_ips(peer, hello->kshm_ips,
						       hello->kshm_nips);
		rc = ksocknal_send_hello(ni, conn, peerid.nid, hello);
	}

	kvfree(hello);

	/*
	 * setup the socket AFTER I've received hello (it disables
	 * SO_LINGER).  I might call back to the acceptor who may want
	 * to send a protocol version response and then close the
	 * socket; this ensures the socket only tears down after the
	 * response has been sent.
	 */
	if (!rc)
		rc = ksocknal_lib_setup_sock(sock);

	write_lock_bh(global_lock);

	/* NB my callbacks block while I hold ksnd_global_lock */
	ksocknal_lib_set_callback(sock, conn);

	if (!active)
		peer->ksnp_accepting--;

	write_unlock_bh(global_lock);

	if (rc) {
		write_lock_bh(global_lock);
		if (!conn->ksnc_closing) {
			/* could be closed by another thread */
			ksocknal_close_conn_locked(conn, rc);
		}
		write_unlock_bh(global_lock);
	} else if (!ksocknal_connsock_addref(conn)) {
		/* Allow I/O to proceed. */
		ksocknal_read_callback(conn);
		ksocknal_write_callback(conn);
		ksocknal_connsock_decref(conn);
	}

	ksocknal_connsock_decref(conn);
	ksocknal_conn_decref(conn);
	return rc;

 failed_2:
	if (!peer->ksnp_closing &&
	    list_empty(&peer->ksnp_conns) &&
	    list_empty(&peer->ksnp_routes)) {
		list_add(&zombies, &peer->ksnp_tx_queue);
		list_del_init(&peer->ksnp_tx_queue);
		ksocknal_unlink_peer_locked(peer);
	}

	write_unlock_bh(global_lock);

	if (warn) {
		if (rc < 0)
			CERROR("Not creating conn %s type %d: %s\n",
			       libcfs_id2str(peerid), conn->ksnc_type, warn);
		else
			CDEBUG(D_NET, "Not creating conn %s type %d: %s\n",
			       libcfs_id2str(peerid), conn->ksnc_type, warn);
	}

	if (!active) {
		if (rc > 0) {
			/*
			 * Request retry by replying with CONN_NONE
			 * ksnc_proto has been set already
			 */
			conn->ksnc_type = SOCKLND_CONN_NONE;
			hello->kshm_nips = 0;
			ksocknal_send_hello(ni, conn, peerid.nid, hello);
		}

		write_lock_bh(global_lock);
		peer->ksnp_accepting--;
		write_unlock_bh(global_lock);
	}

	ksocknal_txlist_done(ni, &zombies, 1);
	ksocknal_peer_decref(peer);

failed_1:
	kvfree(hello);

	kfree(conn);

failed_0:
	sock_release(sock);
	return rc;
}

void
ksocknal_close_conn_locked(struct ksock_conn *conn, int error)
{
	/*
	 * This just does the immmediate housekeeping, and queues the
	 * connection for the reaper to terminate.
	 * Caller holds ksnd_global_lock exclusively in irq context
	 */
	struct ksock_peer *peer = conn->ksnc_peer;
	struct ksock_route *route;
	struct ksock_conn *conn2;
	struct list_head *tmp;

	LASSERT(!peer->ksnp_error);
	LASSERT(!conn->ksnc_closing);
	conn->ksnc_closing = 1;

	/* ksnd_deathrow_conns takes over peer's ref */
	list_del(&conn->ksnc_list);

	route = conn->ksnc_route;
	if (route) {
		/* dissociate conn from route... */
		LASSERT(!route->ksnr_deleted);
		LASSERT(route->ksnr_connected & (1 << conn->ksnc_type));

		conn2 = NULL;
		list_for_each(tmp, &peer->ksnp_conns) {
			conn2 = list_entry(tmp, struct ksock_conn, ksnc_list);

			if (conn2->ksnc_route == route &&
			    conn2->ksnc_type == conn->ksnc_type)
				break;

			conn2 = NULL;
		}
		if (!conn2)
			route->ksnr_connected &= ~(1 << conn->ksnc_type);

		conn->ksnc_route = NULL;

		ksocknal_route_decref(route);     /* drop conn's ref on route */
	}

	if (list_empty(&peer->ksnp_conns)) {
		/* No more connections to this peer */

		if (!list_empty(&peer->ksnp_tx_queue)) {
			struct ksock_tx *tx;

			LASSERT(conn->ksnc_proto == &ksocknal_protocol_v3x);

			/*
			 * throw them to the last connection...,
			 * these TXs will be send to /dev/null by scheduler
			 */
			list_for_each_entry(tx, &peer->ksnp_tx_queue,
					    tx_list)
				ksocknal_tx_prep(conn, tx);

			spin_lock_bh(&conn->ksnc_scheduler->kss_lock);
			list_splice_init(&peer->ksnp_tx_queue,
					 &conn->ksnc_tx_queue);
			spin_unlock_bh(&conn->ksnc_scheduler->kss_lock);
		}

		peer->ksnp_proto = NULL;  /* renegotiate protocol version */
		peer->ksnp_error = error; /* stash last conn close reason */

		if (list_empty(&peer->ksnp_routes)) {
			/*
			 * I've just closed last conn belonging to a
			 * peer with no routes to it
			 */
			ksocknal_unlink_peer_locked(peer);
		}
	}

	spin_lock_bh(&ksocknal_data.ksnd_reaper_lock);

	list_add_tail(&conn->ksnc_list,
		      &ksocknal_data.ksnd_deathrow_conns);
	wake_up(&ksocknal_data.ksnd_reaper_waitq);

	spin_unlock_bh(&ksocknal_data.ksnd_reaper_lock);
}

void
ksocknal_peer_failed(struct ksock_peer *peer)
{
	int notify = 0;
	unsigned long last_alive = 0;

	/*
	 * There has been a connection failure or comms error; but I'll only
	 * tell LNET I think the peer is dead if it's to another kernel and
	 * there are no connections or connection attempts in existence.
	 */
	read_lock(&ksocknal_data.ksnd_global_lock);

	if (!(peer->ksnp_id.pid & LNET_PID_USERFLAG) &&
	    list_empty(&peer->ksnp_conns) &&
	    !peer->ksnp_accepting &&
	    !ksocknal_find_connecting_route_locked(peer)) {
		notify = 1;
		last_alive = peer->ksnp_last_alive;
	}

	read_unlock(&ksocknal_data.ksnd_global_lock);

	if (notify)
		lnet_notify(peer->ksnp_ni, peer->ksnp_id.nid, 0,
			    last_alive);
}

void
ksocknal_finalize_zcreq(struct ksock_conn *conn)
{
	struct ksock_peer *peer = conn->ksnc_peer;
	struct ksock_tx *tx;
	struct ksock_tx *temp;
	struct ksock_tx *tmp;
	LIST_HEAD(zlist);

	/*
	 * NB safe to finalize TXs because closing of socket will
	 * abort all buffered data
	 */
	LASSERT(!conn->ksnc_sock);

	spin_lock(&peer->ksnp_lock);

	list_for_each_entry_safe(tx, tmp, &peer->ksnp_zc_req_list, tx_zc_list) {
		if (tx->tx_conn != conn)
			continue;

		LASSERT(tx->tx_msg.ksm_zc_cookies[0]);

		tx->tx_msg.ksm_zc_cookies[0] = 0;
		tx->tx_zc_aborted = 1; /* mark it as not-acked */
		list_del(&tx->tx_zc_list);
		list_add(&tx->tx_zc_list, &zlist);
	}

	spin_unlock(&peer->ksnp_lock);

	list_for_each_entry_safe(tx, temp, &zlist, tx_zc_list) {
		list_del(&tx->tx_zc_list);
		ksocknal_tx_decref(tx);
	}
}

void
ksocknal_terminate_conn(struct ksock_conn *conn)
{
	/*
	 * This gets called by the reaper (guaranteed thread context) to
	 * disengage the socket from its callbacks and close it.
	 * ksnc_refcount will eventually hit zero, and then the reaper will
	 * destroy it.
	 */
	struct ksock_peer *peer = conn->ksnc_peer;
	struct ksock_sched *sched = conn->ksnc_scheduler;
	int failed = 0;

	LASSERT(conn->ksnc_closing);

	/* wake up the scheduler to "send" all remaining packets to /dev/null */
	spin_lock_bh(&sched->kss_lock);

	/* a closing conn is always ready to tx */
	conn->ksnc_tx_ready = 1;

	if (!conn->ksnc_tx_scheduled &&
	    !list_empty(&conn->ksnc_tx_queue)) {
		list_add_tail(&conn->ksnc_tx_list,
			      &sched->kss_tx_conns);
		conn->ksnc_tx_scheduled = 1;
		/* extra ref for scheduler */
		ksocknal_conn_addref(conn);

		wake_up(&sched->kss_waitq);
	}

	spin_unlock_bh(&sched->kss_lock);

	/* serialise with callbacks */
	write_lock_bh(&ksocknal_data.ksnd_global_lock);

	ksocknal_lib_reset_callback(conn->ksnc_sock, conn);

	/*
	 * OK, so this conn may not be completely disengaged from its
	 * scheduler yet, but it _has_ committed to terminate...
	 */
	conn->ksnc_scheduler->kss_nconns--;

	if (peer->ksnp_error) {
		/* peer's last conn closed in error */
		LASSERT(list_empty(&peer->ksnp_conns));
		failed = 1;
		peer->ksnp_error = 0;     /* avoid multiple notifications */
	}

	write_unlock_bh(&ksocknal_data.ksnd_global_lock);

	if (failed)
		ksocknal_peer_failed(peer);

	/*
	 * The socket is closed on the final put; either here, or in
	 * ksocknal_{send,recv}msg().  Since we set up the linger2 option
	 * when the connection was established, this will close the socket
	 * immediately, aborting anything buffered in it. Any hung
	 * zero-copy transmits will therefore complete in finite time.
	 */
	ksocknal_connsock_decref(conn);
}

void
ksocknal_queue_zombie_conn(struct ksock_conn *conn)
{
	/* Queue the conn for the reaper to destroy */

	LASSERT(!atomic_read(&conn->ksnc_conn_refcount));
	spin_lock_bh(&ksocknal_data.ksnd_reaper_lock);

	list_add_tail(&conn->ksnc_list, &ksocknal_data.ksnd_zombie_conns);
	wake_up(&ksocknal_data.ksnd_reaper_waitq);

	spin_unlock_bh(&ksocknal_data.ksnd_reaper_lock);
}

void
ksocknal_destroy_conn(struct ksock_conn *conn)
{
	unsigned long last_rcv;

	/* Final coup-de-grace of the reaper */
	CDEBUG(D_NET, "connection %p\n", conn);

	LASSERT(!atomic_read(&conn->ksnc_conn_refcount));
	LASSERT(!atomic_read(&conn->ksnc_sock_refcount));
	LASSERT(!conn->ksnc_sock);
	LASSERT(!conn->ksnc_route);
	LASSERT(!conn->ksnc_tx_scheduled);
	LASSERT(!conn->ksnc_rx_scheduled);
	LASSERT(list_empty(&conn->ksnc_tx_queue));

	/* complete current receive if any */
	switch (conn->ksnc_rx_state) {
	case SOCKNAL_RX_LNET_PAYLOAD:
		last_rcv = conn->ksnc_rx_deadline -
			   cfs_time_seconds(*ksocknal_tunables.ksnd_timeout);
		CERROR("Completing partial receive from %s[%d], ip %pI4h:%d, with error, wanted: %zd, left: %d, last alive is %ld secs ago\n",
		       libcfs_id2str(conn->ksnc_peer->ksnp_id), conn->ksnc_type,
		       &conn->ksnc_ipaddr, conn->ksnc_port,
		       iov_iter_count(&conn->ksnc_rx_to), conn->ksnc_rx_nob_left,
		       cfs_duration_sec(cfs_time_sub(cfs_time_current(),
						     last_rcv)));
		lnet_finalize(conn->ksnc_peer->ksnp_ni,
			      conn->ksnc_cookie, -EIO);
		break;
	case SOCKNAL_RX_LNET_HEADER:
		if (conn->ksnc_rx_started)
			CERROR("Incomplete receive of lnet header from %s, ip %pI4h:%d, with error, protocol: %d.x.\n",
			       libcfs_id2str(conn->ksnc_peer->ksnp_id),
			       &conn->ksnc_ipaddr, conn->ksnc_port,
			       conn->ksnc_proto->pro_version);
		break;
	case SOCKNAL_RX_KSM_HEADER:
		if (conn->ksnc_rx_started)
			CERROR("Incomplete receive of ksock message from %s, ip %pI4h:%d, with error, protocol: %d.x.\n",
			       libcfs_id2str(conn->ksnc_peer->ksnp_id),
			       &conn->ksnc_ipaddr, conn->ksnc_port,
			       conn->ksnc_proto->pro_version);
		break;
	case SOCKNAL_RX_SLOP:
		if (conn->ksnc_rx_started)
			CERROR("Incomplete receive of slops from %s, ip %pI4h:%d, with error\n",
			       libcfs_id2str(conn->ksnc_peer->ksnp_id),
			       &conn->ksnc_ipaddr, conn->ksnc_port);
	       break;
	default:
		LBUG();
		break;
	}

	ksocknal_peer_decref(conn->ksnc_peer);

	kfree(conn);
}

int
ksocknal_close_peer_conns_locked(struct ksock_peer *peer, __u32 ipaddr, int why)
{
	struct ksock_conn *conn;
	struct list_head *ctmp;
	struct list_head *cnxt;
	int count = 0;

	list_for_each_safe(ctmp, cnxt, &peer->ksnp_conns) {
		conn = list_entry(ctmp, struct ksock_conn, ksnc_list);

		if (!ipaddr || conn->ksnc_ipaddr == ipaddr) {
			count++;
			ksocknal_close_conn_locked(conn, why);
		}
	}

	return count;
}

int
ksocknal_close_conn_and_siblings(struct ksock_conn *conn, int why)
{
	struct ksock_peer *peer = conn->ksnc_peer;
	__u32 ipaddr = conn->ksnc_ipaddr;
	int count;

	write_lock_bh(&ksocknal_data.ksnd_global_lock);

	count = ksocknal_close_peer_conns_locked(peer, ipaddr, why);

	write_unlock_bh(&ksocknal_data.ksnd_global_lock);

	return count;
}

int
ksocknal_close_matching_conns(struct lnet_process_id id, __u32 ipaddr)
{
	struct ksock_peer *peer;
	struct list_head *ptmp;
	struct list_head *pnxt;
	int lo;
	int hi;
	int i;
	int count = 0;

	write_lock_bh(&ksocknal_data.ksnd_global_lock);

	if (id.nid != LNET_NID_ANY) {
		lo = (int)(ksocknal_nid2peerlist(id.nid) - ksocknal_data.ksnd_peers);
		hi = (int)(ksocknal_nid2peerlist(id.nid) - ksocknal_data.ksnd_peers);
	} else {
		lo = 0;
		hi = ksocknal_data.ksnd_peer_hash_size - 1;
	}

	for (i = lo; i <= hi; i++) {
		list_for_each_safe(ptmp, pnxt,
				   &ksocknal_data.ksnd_peers[i]) {
			peer = list_entry(ptmp, struct ksock_peer, ksnp_list);

			if (!((id.nid == LNET_NID_ANY || id.nid == peer->ksnp_id.nid) &&
			      (id.pid == LNET_PID_ANY || id.pid == peer->ksnp_id.pid)))
				continue;

			count += ksocknal_close_peer_conns_locked(peer, ipaddr,
								  0);
		}
	}

	write_unlock_bh(&ksocknal_data.ksnd_global_lock);

	/* wildcards always succeed */
	if (id.nid == LNET_NID_ANY || id.pid == LNET_PID_ANY || !ipaddr)
		return 0;

	if (!count)
		return -ENOENT;
	else
		return 0;
}

void
ksocknal_notify(struct lnet_ni *ni, lnet_nid_t gw_nid, int alive)
{
	/*
	 * The router is telling me she's been notified of a change in
	 * gateway state....
	 */
	struct lnet_process_id id = {0};

	id.nid = gw_nid;
	id.pid = LNET_PID_ANY;

	CDEBUG(D_NET, "gw %s %s\n", libcfs_nid2str(gw_nid),
	       alive ? "up" : "down");

	if (!alive) {
		/* If the gateway crashed, close all open connections... */
		ksocknal_close_matching_conns(id, 0);
		return;
	}

	/*
	 * ...otherwise do nothing.  We can only establish new connections
	 * if we have autroutes, and these connect on demand.
	 */
}

void
ksocknal_query(struct lnet_ni *ni, lnet_nid_t nid, unsigned long *when)
{
	int connect = 1;
	unsigned long last_alive = 0;
	unsigned long now = cfs_time_current();
	struct ksock_peer *peer = NULL;
	rwlock_t *glock = &ksocknal_data.ksnd_global_lock;
	struct lnet_process_id id = {
		.nid = nid,
		.pid = LNET_PID_LUSTRE,
	};

	read_lock(glock);

	peer = ksocknal_find_peer_locked(ni, id);
	if (peer) {
		struct ksock_conn *conn;
		int bufnob;

		list_for_each_entry(conn, &peer->ksnp_conns, ksnc_list) {
			bufnob = conn->ksnc_sock->sk->sk_wmem_queued;

			if (bufnob < conn->ksnc_tx_bufnob) {
				/* something got ACKed */
				conn->ksnc_tx_deadline =
					cfs_time_shift(*ksocknal_tunables.ksnd_timeout);
				peer->ksnp_last_alive = now;
				conn->ksnc_tx_bufnob = bufnob;
			}
		}

		last_alive = peer->ksnp_last_alive;
		if (!ksocknal_find_connectable_route_locked(peer))
			connect = 0;
	}

	read_unlock(glock);

	if (last_alive)
		*when = last_alive;

	CDEBUG(D_NET, "Peer %s %p, alive %ld secs ago, connect %d\n",
	       libcfs_nid2str(nid), peer,
	       last_alive ? cfs_duration_sec(now - last_alive) : -1,
	       connect);

	if (!connect)
		return;

	ksocknal_add_peer(ni, id, LNET_NIDADDR(nid), lnet_acceptor_port());

	write_lock_bh(glock);

	peer = ksocknal_find_peer_locked(ni, id);
	if (peer)
		ksocknal_launch_all_connections_locked(peer);

	write_unlock_bh(glock);
}

static void
ksocknal_push_peer(struct ksock_peer *peer)
{
	int index;
	int i;
	struct list_head *tmp;
	struct ksock_conn *conn;

	for (index = 0; ; index++) {
		read_lock(&ksocknal_data.ksnd_global_lock);

		i = 0;
		conn = NULL;

		list_for_each(tmp, &peer->ksnp_conns) {
			if (i++ == index) {
				conn = list_entry(tmp, struct ksock_conn,
						  ksnc_list);
				ksocknal_conn_addref(conn);
				break;
			}
		}

		read_unlock(&ksocknal_data.ksnd_global_lock);

		if (!conn)
			break;

		ksocknal_lib_push_conn(conn);
		ksocknal_conn_decref(conn);
	}
}

static int ksocknal_push(struct lnet_ni *ni, struct lnet_process_id id)
{
	struct list_head *start;
	struct list_head *end;
	struct list_head *tmp;
	int rc = -ENOENT;
	unsigned int hsize = ksocknal_data.ksnd_peer_hash_size;

	if (id.nid == LNET_NID_ANY) {
		start = &ksocknal_data.ksnd_peers[0];
		end = &ksocknal_data.ksnd_peers[hsize - 1];
	} else {
		start = ksocknal_nid2peerlist(id.nid);
		end = ksocknal_nid2peerlist(id.nid);
	}

	for (tmp = start; tmp <= end; tmp++) {
		int peer_off; /* searching offset in peer hash table */

		for (peer_off = 0; ; peer_off++) {
			struct ksock_peer *peer;
			int i = 0;

			read_lock(&ksocknal_data.ksnd_global_lock);
			list_for_each_entry(peer, tmp, ksnp_list) {
				if (!((id.nid == LNET_NID_ANY ||
				       id.nid == peer->ksnp_id.nid) &&
				      (id.pid == LNET_PID_ANY ||
				       id.pid == peer->ksnp_id.pid)))
					continue;

				if (i++ == peer_off) {
					ksocknal_peer_addref(peer);
					break;
				}
			}
			read_unlock(&ksocknal_data.ksnd_global_lock);

			if (!i) /* no match */
				break;

			rc = 0;
			ksocknal_push_peer(peer);
			ksocknal_peer_decref(peer);
		}
	}
	return rc;
}

static int
ksocknal_add_interface(struct lnet_ni *ni, __u32 ipaddress, __u32 netmask)
{
	struct ksock_net *net = ni->ni_data;
	struct ksock_interface *iface;
	int rc;
	int i;
	int j;
	struct list_head *ptmp;
	struct ksock_peer *peer;
	struct list_head *rtmp;
	struct ksock_route *route;

	if (!ipaddress || !netmask)
		return -EINVAL;

	write_lock_bh(&ksocknal_data.ksnd_global_lock);

	iface = ksocknal_ip2iface(ni, ipaddress);
	if (iface) {
		/* silently ignore dups */
		rc = 0;
	} else if (net->ksnn_ninterfaces == LNET_MAX_INTERFACES) {
		rc = -ENOSPC;
	} else {
		iface = &net->ksnn_interfaces[net->ksnn_ninterfaces++];

		iface->ksni_ipaddr = ipaddress;
		iface->ksni_netmask = netmask;
		iface->ksni_nroutes = 0;
		iface->ksni_npeers = 0;

		for (i = 0; i < ksocknal_data.ksnd_peer_hash_size; i++) {
			list_for_each(ptmp, &ksocknal_data.ksnd_peers[i]) {
				peer = list_entry(ptmp, struct ksock_peer,
						  ksnp_list);

				for (j = 0; j < peer->ksnp_n_passive_ips; j++)
					if (peer->ksnp_passive_ips[j] == ipaddress)
						iface->ksni_npeers++;

				list_for_each(rtmp, &peer->ksnp_routes) {
					route = list_entry(rtmp, struct ksock_route,
							   ksnr_list);

					if (route->ksnr_myipaddr == ipaddress)
						iface->ksni_nroutes++;
				}
			}
		}

		rc = 0;
		/*
		 * NB only new connections will pay attention to the
		 * new interface!
		 */
	}

	write_unlock_bh(&ksocknal_data.ksnd_global_lock);

	return rc;
}

static void
ksocknal_peer_del_interface_locked(struct ksock_peer *peer, __u32 ipaddr)
{
	struct list_head *tmp;
	struct list_head *nxt;
	struct ksock_route *route;
	struct ksock_conn *conn;
	int i;
	int j;

	for (i = 0; i < peer->ksnp_n_passive_ips; i++)
		if (peer->ksnp_passive_ips[i] == ipaddr) {
			for (j = i + 1; j < peer->ksnp_n_passive_ips; j++)
				peer->ksnp_passive_ips[j - 1] =
					peer->ksnp_passive_ips[j];
			peer->ksnp_n_passive_ips--;
			break;
		}

	list_for_each_safe(tmp, nxt, &peer->ksnp_routes) {
		route = list_entry(tmp, struct ksock_route, ksnr_list);

		if (route->ksnr_myipaddr != ipaddr)
			continue;

		if (route->ksnr_share_count) {
			/* Manually created; keep, but unbind */
			route->ksnr_myipaddr = 0;
		} else {
			ksocknal_del_route_locked(route);
		}
	}

	list_for_each_safe(tmp, nxt, &peer->ksnp_conns) {
		conn = list_entry(tmp, struct ksock_conn, ksnc_list);

		if (conn->ksnc_myipaddr == ipaddr)
			ksocknal_close_conn_locked(conn, 0);
	}
}

static int
ksocknal_del_interface(struct lnet_ni *ni, __u32 ipaddress)
{
	struct ksock_net *net = ni->ni_data;
	int rc = -ENOENT;
	struct list_head *tmp;
	struct list_head *nxt;
	struct ksock_peer *peer;
	__u32 this_ip;
	int i;
	int j;

	write_lock_bh(&ksocknal_data.ksnd_global_lock);

	for (i = 0; i < net->ksnn_ninterfaces; i++) {
		this_ip = net->ksnn_interfaces[i].ksni_ipaddr;

		if (!(!ipaddress || ipaddress == this_ip))
			continue;

		rc = 0;

		for (j = i + 1; j < net->ksnn_ninterfaces; j++)
			net->ksnn_interfaces[j - 1] =
				net->ksnn_interfaces[j];

		net->ksnn_ninterfaces--;

		for (j = 0; j < ksocknal_data.ksnd_peer_hash_size; j++) {
			list_for_each_safe(tmp, nxt,
					   &ksocknal_data.ksnd_peers[j]) {
				peer = list_entry(tmp, struct ksock_peer, ksnp_list);

				if (peer->ksnp_ni != ni)
					continue;

				ksocknal_peer_del_interface_locked(peer, this_ip);
			}
		}
	}

	write_unlock_bh(&ksocknal_data.ksnd_global_lock);

	return rc;
}

int
ksocknal_ctl(struct lnet_ni *ni, unsigned int cmd, void *arg)
{
	struct lnet_process_id id = {0};
	struct libcfs_ioctl_data *data = arg;
	int rc;

	switch (cmd) {
	case IOC_LIBCFS_GET_INTERFACE: {
		struct ksock_net       *net = ni->ni_data;
		struct ksock_interface *iface;

		read_lock(&ksocknal_data.ksnd_global_lock);

		if (data->ioc_count >= (__u32)net->ksnn_ninterfaces) {
			rc = -ENOENT;
		} else {
			rc = 0;
			iface = &net->ksnn_interfaces[data->ioc_count];

			data->ioc_u32[0] = iface->ksni_ipaddr;
			data->ioc_u32[1] = iface->ksni_netmask;
			data->ioc_u32[2] = iface->ksni_npeers;
			data->ioc_u32[3] = iface->ksni_nroutes;
		}

		read_unlock(&ksocknal_data.ksnd_global_lock);
		return rc;
	}

	case IOC_LIBCFS_ADD_INTERFACE:
		return ksocknal_add_interface(ni,
					      data->ioc_u32[0], /* IP address */
					      data->ioc_u32[1]); /* net mask */

	case IOC_LIBCFS_DEL_INTERFACE:
		return ksocknal_del_interface(ni,
					      data->ioc_u32[0]); /* IP address */

	case IOC_LIBCFS_GET_PEER: {
		__u32 myip = 0;
		__u32 ip = 0;
		int port = 0;
		int conn_count = 0;
		int share_count = 0;

		rc = ksocknal_get_peer_info(ni, data->ioc_count,
					    &id, &myip, &ip, &port,
					    &conn_count,  &share_count);
		if (rc)
			return rc;

		data->ioc_nid    = id.nid;
		data->ioc_count  = share_count;
		data->ioc_u32[0] = ip;
		data->ioc_u32[1] = port;
		data->ioc_u32[2] = myip;
		data->ioc_u32[3] = conn_count;
		data->ioc_u32[4] = id.pid;
		return 0;
	}

	case IOC_LIBCFS_ADD_PEER:
		id.nid = data->ioc_nid;
		id.pid = LNET_PID_LUSTRE;
		return ksocknal_add_peer(ni, id,
					  data->ioc_u32[0], /* IP */
					  data->ioc_u32[1]); /* port */

	case IOC_LIBCFS_DEL_PEER:
		id.nid = data->ioc_nid;
		id.pid = LNET_PID_ANY;
		return ksocknal_del_peer(ni, id,
					  data->ioc_u32[0]); /* IP */

	case IOC_LIBCFS_GET_CONN: {
		int txmem;
		int rxmem;
		int nagle;
		struct ksock_conn *conn;

		conn = ksocknal_get_conn_by_idx(ni, data->ioc_count);
		if (!conn)
			return -ENOENT;

		ksocknal_lib_get_conn_tunables(conn, &txmem, &rxmem, &nagle);

		data->ioc_count  = txmem;
		data->ioc_nid    = conn->ksnc_peer->ksnp_id.nid;
		data->ioc_flags  = nagle;
		data->ioc_u32[0] = conn->ksnc_ipaddr;
		data->ioc_u32[1] = conn->ksnc_port;
		data->ioc_u32[2] = conn->ksnc_myipaddr;
		data->ioc_u32[3] = conn->ksnc_type;
		data->ioc_u32[4] = conn->ksnc_scheduler->kss_info->ksi_cpt;
		data->ioc_u32[5] = rxmem;
		data->ioc_u32[6] = conn->ksnc_peer->ksnp_id.pid;
		ksocknal_conn_decref(conn);
		return 0;
	}

	case IOC_LIBCFS_CLOSE_CONNECTION:
		id.nid = data->ioc_nid;
		id.pid = LNET_PID_ANY;
		return ksocknal_close_matching_conns(id,
						      data->ioc_u32[0]);

	case IOC_LIBCFS_REGISTER_MYNID:
		/* Ignore if this is a noop */
		if (data->ioc_nid == ni->ni_nid)
			return 0;

		CERROR("obsolete IOC_LIBCFS_REGISTER_MYNID: %s(%s)\n",
		       libcfs_nid2str(data->ioc_nid),
		       libcfs_nid2str(ni->ni_nid));
		return -EINVAL;

	case IOC_LIBCFS_PUSH_CONNECTION:
		id.nid = data->ioc_nid;
		id.pid = LNET_PID_ANY;
		return ksocknal_push(ni, id);

	default:
		return -EINVAL;
	}
	/* not reached */
}

static void
ksocknal_free_buffers(void)
{
	LASSERT(!atomic_read(&ksocknal_data.ksnd_nactive_txs));

	if (ksocknal_data.ksnd_sched_info) {
		struct ksock_sched_info *info;
		int i;

		cfs_percpt_for_each(info, i, ksocknal_data.ksnd_sched_info)
			kfree(info->ksi_scheds);
		cfs_percpt_free(ksocknal_data.ksnd_sched_info);
	}

	kvfree(ksocknal_data.ksnd_peers);

	spin_lock(&ksocknal_data.ksnd_tx_lock);

	if (!list_empty(&ksocknal_data.ksnd_idle_noop_txs)) {
		struct list_head zlist;
		struct ksock_tx *tx;
		struct ksock_tx *temp;

		list_add(&zlist, &ksocknal_data.ksnd_idle_noop_txs);
		list_del_init(&ksocknal_data.ksnd_idle_noop_txs);
		spin_unlock(&ksocknal_data.ksnd_tx_lock);

		list_for_each_entry_safe(tx, temp, &zlist, tx_list) {
			list_del(&tx->tx_list);
			kfree(tx);
		}
	} else {
		spin_unlock(&ksocknal_data.ksnd_tx_lock);
	}
}

static void
ksocknal_base_shutdown(void)
{
	struct ksock_sched_info *info;
	struct ksock_sched *sched;
	int i;
	int j;

	LASSERT(!ksocknal_data.ksnd_nnets);

	switch (ksocknal_data.ksnd_init) {
	default:
		LASSERT(0);
		/* fall through */
	case SOCKNAL_INIT_ALL:
	case SOCKNAL_INIT_DATA:
		LASSERT(ksocknal_data.ksnd_peers);
		for (i = 0; i < ksocknal_data.ksnd_peer_hash_size; i++)
			LASSERT(list_empty(&ksocknal_data.ksnd_peers[i]));

		LASSERT(list_empty(&ksocknal_data.ksnd_nets));
		LASSERT(list_empty(&ksocknal_data.ksnd_enomem_conns));
		LASSERT(list_empty(&ksocknal_data.ksnd_zombie_conns));
		LASSERT(list_empty(&ksocknal_data.ksnd_connd_connreqs));
		LASSERT(list_empty(&ksocknal_data.ksnd_connd_routes));

		if (ksocknal_data.ksnd_sched_info) {
			cfs_percpt_for_each(info, i,
					    ksocknal_data.ksnd_sched_info) {
				if (!info->ksi_scheds)
					continue;

				for (j = 0; j < info->ksi_nthreads_max; j++) {
					sched = &info->ksi_scheds[j];
					LASSERT(list_empty(
						&sched->kss_tx_conns));
					LASSERT(list_empty(
						&sched->kss_rx_conns));
					LASSERT(list_empty(
						&sched->kss_zombie_noop_txs));
					LASSERT(!sched->kss_nconns);
				}
			}
		}

		/* flag threads to terminate; wake and wait for them to die */
		ksocknal_data.ksnd_shuttingdown = 1;
		wake_up_all(&ksocknal_data.ksnd_connd_waitq);
		wake_up_all(&ksocknal_data.ksnd_reaper_waitq);

		if (ksocknal_data.ksnd_sched_info) {
			cfs_percpt_for_each(info, i,
					    ksocknal_data.ksnd_sched_info) {
				if (!info->ksi_scheds)
					continue;

				for (j = 0; j < info->ksi_nthreads_max; j++) {
					sched = &info->ksi_scheds[j];
					wake_up_all(&sched->kss_waitq);
				}
			}
		}

		i = 4;
		read_lock(&ksocknal_data.ksnd_global_lock);
		while (ksocknal_data.ksnd_nthreads) {
			i++;
			CDEBUG(((i & (-i)) == i) ? D_WARNING : D_NET, /* power of 2? */
			       "waiting for %d threads to terminate\n",
				ksocknal_data.ksnd_nthreads);
			read_unlock(&ksocknal_data.ksnd_global_lock);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(cfs_time_seconds(1));
			read_lock(&ksocknal_data.ksnd_global_lock);
		}
		read_unlock(&ksocknal_data.ksnd_global_lock);

		ksocknal_free_buffers();

		ksocknal_data.ksnd_init = SOCKNAL_INIT_NOTHING;
		break;
	}

	module_put(THIS_MODULE);
}

static __u64
ksocknal_new_incarnation(void)
{
	/* The incarnation number is the time this module loaded and it
	 * identifies this particular instance of the socknal.
	 */
	return ktime_get_ns();
}

static int
ksocknal_base_startup(void)
{
	struct ksock_sched_info	*info;
	int rc;
	int i;

	LASSERT(ksocknal_data.ksnd_init == SOCKNAL_INIT_NOTHING);
	LASSERT(!ksocknal_data.ksnd_nnets);

	memset(&ksocknal_data, 0, sizeof(ksocknal_data)); /* zero pointers */

	ksocknal_data.ksnd_peer_hash_size = SOCKNAL_PEER_HASH_SIZE;
	ksocknal_data.ksnd_peers = kvmalloc_array(ksocknal_data.ksnd_peer_hash_size,
						  sizeof(struct list_head),
						  GFP_KERNEL);
	if (!ksocknal_data.ksnd_peers)
		return -ENOMEM;

	for (i = 0; i < ksocknal_data.ksnd_peer_hash_size; i++)
		INIT_LIST_HEAD(&ksocknal_data.ksnd_peers[i]);

	rwlock_init(&ksocknal_data.ksnd_global_lock);
	INIT_LIST_HEAD(&ksocknal_data.ksnd_nets);

	spin_lock_init(&ksocknal_data.ksnd_reaper_lock);
	INIT_LIST_HEAD(&ksocknal_data.ksnd_enomem_conns);
	INIT_LIST_HEAD(&ksocknal_data.ksnd_zombie_conns);
	INIT_LIST_HEAD(&ksocknal_data.ksnd_deathrow_conns);
	init_waitqueue_head(&ksocknal_data.ksnd_reaper_waitq);

	spin_lock_init(&ksocknal_data.ksnd_connd_lock);
	INIT_LIST_HEAD(&ksocknal_data.ksnd_connd_connreqs);
	INIT_LIST_HEAD(&ksocknal_data.ksnd_connd_routes);
	init_waitqueue_head(&ksocknal_data.ksnd_connd_waitq);

	spin_lock_init(&ksocknal_data.ksnd_tx_lock);
	INIT_LIST_HEAD(&ksocknal_data.ksnd_idle_noop_txs);

	/* NB memset above zeros whole of ksocknal_data */

	/* flag lists/ptrs/locks initialised */
	ksocknal_data.ksnd_init = SOCKNAL_INIT_DATA;
	try_module_get(THIS_MODULE);

	ksocknal_data.ksnd_sched_info = cfs_percpt_alloc(lnet_cpt_table(),
							 sizeof(*info));
	if (!ksocknal_data.ksnd_sched_info)
		goto failed;

	cfs_percpt_for_each(info, i, ksocknal_data.ksnd_sched_info) {
		struct ksock_sched *sched;
		int nthrs;

		nthrs = cfs_cpt_weight(lnet_cpt_table(), i);
		if (*ksocknal_tunables.ksnd_nscheds > 0) {
			nthrs = min(nthrs, *ksocknal_tunables.ksnd_nscheds);
		} else {
			/*
			 * max to half of CPUs, assume another half should be
			 * reserved for upper layer modules
			 */
			nthrs = min(max(SOCKNAL_NSCHEDS, nthrs >> 1), nthrs);
		}

		info->ksi_nthreads_max = nthrs;
		info->ksi_cpt = i;

		info->ksi_scheds = kzalloc_cpt(info->ksi_nthreads_max * sizeof(*sched),
					       GFP_NOFS, i);
		if (!info->ksi_scheds)
			goto failed;

		for (; nthrs > 0; nthrs--) {
			sched = &info->ksi_scheds[nthrs - 1];

			sched->kss_info = info;
			spin_lock_init(&sched->kss_lock);
			INIT_LIST_HEAD(&sched->kss_rx_conns);
			INIT_LIST_HEAD(&sched->kss_tx_conns);
			INIT_LIST_HEAD(&sched->kss_zombie_noop_txs);
			init_waitqueue_head(&sched->kss_waitq);
		}
	}

	ksocknal_data.ksnd_connd_starting       = 0;
	ksocknal_data.ksnd_connd_failed_stamp   = 0;
	ksocknal_data.ksnd_connd_starting_stamp = ktime_get_real_seconds();
	/*
	 * must have at least 2 connds to remain responsive to accepts while
	 * connecting
	 */
	if (*ksocknal_tunables.ksnd_nconnds < SOCKNAL_CONND_RESV + 1)
		*ksocknal_tunables.ksnd_nconnds = SOCKNAL_CONND_RESV + 1;

	if (*ksocknal_tunables.ksnd_nconnds_max <
	    *ksocknal_tunables.ksnd_nconnds) {
		ksocknal_tunables.ksnd_nconnds_max =
			ksocknal_tunables.ksnd_nconnds;
	}

	for (i = 0; i < *ksocknal_tunables.ksnd_nconnds; i++) {
		char name[16];

		spin_lock_bh(&ksocknal_data.ksnd_connd_lock);
		ksocknal_data.ksnd_connd_starting++;
		spin_unlock_bh(&ksocknal_data.ksnd_connd_lock);

		snprintf(name, sizeof(name), "socknal_cd%02d", i);
		rc = ksocknal_thread_start(ksocknal_connd,
					   (void *)((uintptr_t)i), name);
		if (rc) {
			spin_lock_bh(&ksocknal_data.ksnd_connd_lock);
			ksocknal_data.ksnd_connd_starting--;
			spin_unlock_bh(&ksocknal_data.ksnd_connd_lock);
			CERROR("Can't spawn socknal connd: %d\n", rc);
			goto failed;
		}
	}

	rc = ksocknal_thread_start(ksocknal_reaper, NULL, "socknal_reaper");
	if (rc) {
		CERROR("Can't spawn socknal reaper: %d\n", rc);
		goto failed;
	}

	/* flag everything initialised */
	ksocknal_data.ksnd_init = SOCKNAL_INIT_ALL;

	return 0;

 failed:
	ksocknal_base_shutdown();
	return -ENETDOWN;
}

static void
ksocknal_debug_peerhash(struct lnet_ni *ni)
{
	struct ksock_peer *peer = NULL;
	struct list_head *tmp;
	int i;

	read_lock(&ksocknal_data.ksnd_global_lock);

	for (i = 0; i < ksocknal_data.ksnd_peer_hash_size; i++) {
		list_for_each(tmp, &ksocknal_data.ksnd_peers[i]) {
			peer = list_entry(tmp, struct ksock_peer, ksnp_list);

			if (peer->ksnp_ni == ni)
				break;

			peer = NULL;
		}
	}

	if (peer) {
		struct ksock_route *route;
		struct ksock_conn  *conn;

		CWARN("Active peer on shutdown: %s, ref %d, scnt %d, closing %d, accepting %d, err %d, zcookie %llu, txq %d, zc_req %d\n",
		      libcfs_id2str(peer->ksnp_id),
		      atomic_read(&peer->ksnp_refcount),
		      peer->ksnp_sharecount, peer->ksnp_closing,
		      peer->ksnp_accepting, peer->ksnp_error,
		      peer->ksnp_zc_next_cookie,
		      !list_empty(&peer->ksnp_tx_queue),
		      !list_empty(&peer->ksnp_zc_req_list));

		list_for_each(tmp, &peer->ksnp_routes) {
			route = list_entry(tmp, struct ksock_route, ksnr_list);
			CWARN("Route: ref %d, schd %d, conn %d, cnted %d, del %d\n",
			      atomic_read(&route->ksnr_refcount),
			      route->ksnr_scheduled, route->ksnr_connecting,
			      route->ksnr_connected, route->ksnr_deleted);
		}

		list_for_each(tmp, &peer->ksnp_conns) {
			conn = list_entry(tmp, struct ksock_conn, ksnc_list);
			CWARN("Conn: ref %d, sref %d, t %d, c %d\n",
			      atomic_read(&conn->ksnc_conn_refcount),
			      atomic_read(&conn->ksnc_sock_refcount),
			      conn->ksnc_type, conn->ksnc_closing);
		}
	}

	read_unlock(&ksocknal_data.ksnd_global_lock);
}

void
ksocknal_shutdown(struct lnet_ni *ni)
{
	struct ksock_net *net = ni->ni_data;
	int i;
	struct lnet_process_id anyid = {0};

	anyid.nid = LNET_NID_ANY;
	anyid.pid = LNET_PID_ANY;

	LASSERT(ksocknal_data.ksnd_init == SOCKNAL_INIT_ALL);
	LASSERT(ksocknal_data.ksnd_nnets > 0);

	spin_lock_bh(&net->ksnn_lock);
	net->ksnn_shutdown = 1;		 /* prevent new peers */
	spin_unlock_bh(&net->ksnn_lock);

	/* Delete all peers */
	ksocknal_del_peer(ni, anyid, 0);

	/* Wait for all peer state to clean up */
	i = 2;
	spin_lock_bh(&net->ksnn_lock);
	while (net->ksnn_npeers) {
		spin_unlock_bh(&net->ksnn_lock);

		i++;
		CDEBUG(((i & (-i)) == i) ? D_WARNING : D_NET, /* power of 2? */
		       "waiting for %d peers to disconnect\n",
		       net->ksnn_npeers);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(cfs_time_seconds(1));

		ksocknal_debug_peerhash(ni);

		spin_lock_bh(&net->ksnn_lock);
	}
	spin_unlock_bh(&net->ksnn_lock);

	for (i = 0; i < net->ksnn_ninterfaces; i++) {
		LASSERT(!net->ksnn_interfaces[i].ksni_npeers);
		LASSERT(!net->ksnn_interfaces[i].ksni_nroutes);
	}

	list_del(&net->ksnn_list);
	kfree(net);

	ksocknal_data.ksnd_nnets--;
	if (!ksocknal_data.ksnd_nnets)
		ksocknal_base_shutdown();
}

static int
ksocknal_enumerate_interfaces(struct ksock_net *net)
{
	char **names;
	int i;
	int j;
	int rc;
	int n;

	n = lnet_ipif_enumerate(&names);
	if (n <= 0) {
		CERROR("Can't enumerate interfaces: %d\n", n);
		return n;
	}

	for (i = j = 0; i < n; i++) {
		int up;
		__u32 ip;
		__u32 mask;

		if (!strcmp(names[i], "lo")) /* skip the loopback IF */
			continue;

		rc = lnet_ipif_query(names[i], &up, &ip, &mask);
		if (rc) {
			CWARN("Can't get interface %s info: %d\n",
			      names[i], rc);
			continue;
		}

		if (!up) {
			CWARN("Ignoring interface %s (down)\n",
			      names[i]);
			continue;
		}

		if (j == LNET_MAX_INTERFACES) {
			CWARN("Ignoring interface %s (too many interfaces)\n",
			      names[i]);
			continue;
		}

		net->ksnn_interfaces[j].ksni_ipaddr = ip;
		net->ksnn_interfaces[j].ksni_netmask = mask;
		strlcpy(net->ksnn_interfaces[j].ksni_name,
			names[i], sizeof(net->ksnn_interfaces[j].ksni_name));
		j++;
	}

	lnet_ipif_free_enumeration(names, n);

	if (!j)
		CERROR("Can't find any usable interfaces\n");

	return j;
}

static int
ksocknal_search_new_ipif(struct ksock_net *net)
{
	int new_ipif = 0;
	int i;

	for (i = 0; i < net->ksnn_ninterfaces; i++) {
		char *ifnam = &net->ksnn_interfaces[i].ksni_name[0];
		char *colon = strchr(ifnam, ':');
		int found  = 0;
		struct ksock_net *tmp;
		int j;

		if (colon) /* ignore alias device */
			*colon = 0;

		list_for_each_entry(tmp, &ksocknal_data.ksnd_nets, ksnn_list) {
			for (j = 0; !found && j < tmp->ksnn_ninterfaces; j++) {
				char *ifnam2 =
					&tmp->ksnn_interfaces[j].ksni_name[0];
				char *colon2 = strchr(ifnam2, ':');

				if (colon2)
					*colon2 = 0;

				found = !strcmp(ifnam, ifnam2);
				if (colon2)
					*colon2 = ':';
			}
			if (found)
				break;
		}

		new_ipif += !found;
		if (colon)
			*colon = ':';
	}

	return new_ipif;
}

static int
ksocknal_start_schedulers(struct ksock_sched_info *info)
{
	int nthrs;
	int rc = 0;
	int i;

	if (!info->ksi_nthreads) {
		if (*ksocknal_tunables.ksnd_nscheds > 0) {
			nthrs = info->ksi_nthreads_max;
		} else {
			nthrs = cfs_cpt_weight(lnet_cpt_table(),
					       info->ksi_cpt);
			nthrs = min(max(SOCKNAL_NSCHEDS, nthrs >> 1), nthrs);
			nthrs = min(SOCKNAL_NSCHEDS_HIGH, nthrs);
		}
		nthrs = min(nthrs, info->ksi_nthreads_max);
	} else {
		LASSERT(info->ksi_nthreads <= info->ksi_nthreads_max);
		/* increase two threads if there is new interface */
		nthrs = min(2, info->ksi_nthreads_max - info->ksi_nthreads);
	}

	for (i = 0; i < nthrs; i++) {
		long id;
		char name[20];
		struct ksock_sched *sched;

		id = KSOCK_THREAD_ID(info->ksi_cpt, info->ksi_nthreads + i);
		sched = &info->ksi_scheds[KSOCK_THREAD_SID(id)];
		snprintf(name, sizeof(name), "socknal_sd%02d_%02d",
			 info->ksi_cpt, (int)(sched - &info->ksi_scheds[0]));

		rc = ksocknal_thread_start(ksocknal_scheduler,
					   (void *)id, name);
		if (!rc)
			continue;

		CERROR("Can't spawn thread %d for scheduler[%d]: %d\n",
		       info->ksi_cpt, info->ksi_nthreads + i, rc);
		break;
	}

	info->ksi_nthreads += i;
	return rc;
}

static int
ksocknal_net_start_threads(struct ksock_net *net, __u32 *cpts, int ncpts)
{
	int newif = ksocknal_search_new_ipif(net);
	int rc;
	int i;

	LASSERT(ncpts > 0 && ncpts <= cfs_cpt_number(lnet_cpt_table()));

	for (i = 0; i < ncpts; i++) {
		struct ksock_sched_info *info;
		int cpt = !cpts ? i : cpts[i];

		LASSERT(cpt < cfs_cpt_number(lnet_cpt_table()));
		info = ksocknal_data.ksnd_sched_info[cpt];

		if (!newif && info->ksi_nthreads > 0)
			continue;

		rc = ksocknal_start_schedulers(info);
		if (rc)
			return rc;
	}
	return 0;
}

int
ksocknal_startup(struct lnet_ni *ni)
{
	struct ksock_net *net;
	int rc;
	int i;

	LASSERT(ni->ni_lnd == &the_ksocklnd);

	if (ksocknal_data.ksnd_init == SOCKNAL_INIT_NOTHING) {
		rc = ksocknal_base_startup();
		if (rc)
			return rc;
	}

	net = kzalloc(sizeof(*net), GFP_NOFS);
	if (!net)
		goto fail_0;

	spin_lock_init(&net->ksnn_lock);
	net->ksnn_incarnation = ksocknal_new_incarnation();
	ni->ni_data = net;
	ni->ni_peertimeout    = *ksocknal_tunables.ksnd_peertimeout;
	ni->ni_maxtxcredits   = *ksocknal_tunables.ksnd_credits;
	ni->ni_peertxcredits  = *ksocknal_tunables.ksnd_peertxcredits;
	ni->ni_peerrtrcredits = *ksocknal_tunables.ksnd_peerrtrcredits;

	if (!ni->ni_interfaces[0]) {
		rc = ksocknal_enumerate_interfaces(net);
		if (rc <= 0)
			goto fail_1;

		net->ksnn_ninterfaces = 1;
	} else {
		for (i = 0; i < LNET_MAX_INTERFACES; i++) {
			int up;

			if (!ni->ni_interfaces[i])
				break;

			rc = lnet_ipif_query(ni->ni_interfaces[i], &up,
					     &net->ksnn_interfaces[i].ksni_ipaddr,
					     &net->ksnn_interfaces[i].ksni_netmask);

			if (rc) {
				CERROR("Can't get interface %s info: %d\n",
				       ni->ni_interfaces[i], rc);
				goto fail_1;
			}

			if (!up) {
				CERROR("Interface %s is down\n",
				       ni->ni_interfaces[i]);
				goto fail_1;
			}

			strlcpy(net->ksnn_interfaces[i].ksni_name,
				ni->ni_interfaces[i],
				sizeof(net->ksnn_interfaces[i].ksni_name));
		}
		net->ksnn_ninterfaces = i;
	}

	/* call it before add it to ksocknal_data.ksnd_nets */
	rc = ksocknal_net_start_threads(net, ni->ni_cpts, ni->ni_ncpts);
	if (rc)
		goto fail_1;

	ni->ni_nid = LNET_MKNID(LNET_NIDNET(ni->ni_nid),
				net->ksnn_interfaces[0].ksni_ipaddr);
	list_add(&net->ksnn_list, &ksocknal_data.ksnd_nets);

	ksocknal_data.ksnd_nnets++;

	return 0;

 fail_1:
	kfree(net);
 fail_0:
	if (!ksocknal_data.ksnd_nnets)
		ksocknal_base_shutdown();

	return -ENETDOWN;
}

static void __exit ksocklnd_exit(void)
{
	lnet_unregister_lnd(&the_ksocklnd);
}

static int __init ksocklnd_init(void)
{
	int rc;

	/* check ksnr_connected/connecting field large enough */
	BUILD_BUG_ON(SOCKLND_CONN_NTYPES > 4);
	BUILD_BUG_ON(SOCKLND_CONN_ACK != SOCKLND_CONN_BULK_IN);

	/* initialize the_ksocklnd */
	the_ksocklnd.lnd_type     = SOCKLND;
	the_ksocklnd.lnd_startup  = ksocknal_startup;
	the_ksocklnd.lnd_shutdown = ksocknal_shutdown;
	the_ksocklnd.lnd_ctl      = ksocknal_ctl;
	the_ksocklnd.lnd_send     = ksocknal_send;
	the_ksocklnd.lnd_recv     = ksocknal_recv;
	the_ksocklnd.lnd_notify   = ksocknal_notify;
	the_ksocklnd.lnd_query    = ksocknal_query;
	the_ksocklnd.lnd_accept   = ksocknal_accept;

	rc = ksocknal_tunables_init();
	if (rc)
		return rc;

	lnet_register_lnd(&the_ksocklnd);

	return 0;
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("TCP Socket LNet Network Driver");
MODULE_VERSION("2.7.0");
MODULE_LICENSE("GPL");

module_init(ksocklnd_init);
module_exit(ksocklnd_exit);
