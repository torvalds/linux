/*
 * libcxgbi.c: Chelsio common library for T3/T4 iSCSI driver.
 *
 * Copyright (c) 2010-2015 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 * Written by: Rakesh Ranjan (rranjan@chelsio.com)
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/skbuff.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/pci.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <linux/if_vlan.h>
#include <linux/inet.h>
#include <net/dst.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#include <linux/inetdevice.h>	/* ip_dev_find */
#include <linux/module.h>
#include <net/tcp.h>

static unsigned int dbg_level;

#include "libcxgbi.h"

#define DRV_MODULE_NAME		"libcxgbi"
#define DRV_MODULE_DESC		"Chelsio iSCSI driver library"
#define DRV_MODULE_VERSION	"0.9.1-ko"
#define DRV_MODULE_RELDATE	"Apr. 2015"

static char version[] =
	DRV_MODULE_DESC " " DRV_MODULE_NAME
	" v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Chelsio Communications, Inc.");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("GPL");

module_param(dbg_level, uint, 0644);
MODULE_PARM_DESC(dbg_level, "libiscsi debug level (default=0)");


/*
 * cxgbi device management
 * maintains a list of the cxgbi devices
 */
static LIST_HEAD(cdev_list);
static DEFINE_MUTEX(cdev_mutex);

static LIST_HEAD(cdev_rcu_list);
static DEFINE_SPINLOCK(cdev_rcu_lock);

static inline void cxgbi_decode_sw_tag(u32 sw_tag, int *idx, int *age)
{
	if (age)
		*age = sw_tag & 0x7FFF;
	if (idx)
		*idx = (sw_tag >> 16) & 0x7FFF;
}

int cxgbi_device_portmap_create(struct cxgbi_device *cdev, unsigned int base,
				unsigned int max_conn)
{
	struct cxgbi_ports_map *pmap = &cdev->pmap;

	pmap->port_csk = kvzalloc(array_size(max_conn,
					     sizeof(struct cxgbi_sock *)),
				  GFP_KERNEL | __GFP_NOWARN);
	if (!pmap->port_csk) {
		pr_warn("cdev 0x%p, portmap OOM %u.\n", cdev, max_conn);
		return -ENOMEM;
	}

	pmap->max_connect = max_conn;
	pmap->sport_base = base;
	spin_lock_init(&pmap->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(cxgbi_device_portmap_create);

void cxgbi_device_portmap_cleanup(struct cxgbi_device *cdev)
{
	struct cxgbi_ports_map *pmap = &cdev->pmap;
	struct cxgbi_sock *csk;
	int i;

	for (i = 0; i < pmap->max_connect; i++) {
		if (pmap->port_csk[i]) {
			csk = pmap->port_csk[i];
			pmap->port_csk[i] = NULL;
			log_debug(1 << CXGBI_DBG_SOCK,
				"csk 0x%p, cdev 0x%p, offload down.\n",
				csk, cdev);
			spin_lock_bh(&csk->lock);
			cxgbi_sock_set_flag(csk, CTPF_OFFLOAD_DOWN);
			cxgbi_sock_closed(csk);
			spin_unlock_bh(&csk->lock);
			cxgbi_sock_put(csk);
		}
	}
}
EXPORT_SYMBOL_GPL(cxgbi_device_portmap_cleanup);

static inline void cxgbi_device_destroy(struct cxgbi_device *cdev)
{
	log_debug(1 << CXGBI_DBG_DEV,
		"cdev 0x%p, p# %u.\n", cdev, cdev->nports);
	cxgbi_hbas_remove(cdev);
	cxgbi_device_portmap_cleanup(cdev);
	if (cdev->cdev2ppm)
		cxgbi_ppm_release(cdev->cdev2ppm(cdev));
	if (cdev->pmap.max_connect)
		kvfree(cdev->pmap.port_csk);
	kfree(cdev);
}

struct cxgbi_device *cxgbi_device_register(unsigned int extra,
					   unsigned int nports)
{
	struct cxgbi_device *cdev;

	cdev = kzalloc(sizeof(*cdev) + extra + nports *
			(sizeof(struct cxgbi_hba *) +
			 sizeof(struct net_device *)),
			GFP_KERNEL);
	if (!cdev) {
		pr_warn("nport %d, OOM.\n", nports);
		return NULL;
	}
	cdev->ports = (struct net_device **)(cdev + 1);
	cdev->hbas = (struct cxgbi_hba **)(((char*)cdev->ports) + nports *
						sizeof(struct net_device *));
	if (extra)
		cdev->dd_data = ((char *)cdev->hbas) +
				nports * sizeof(struct cxgbi_hba *);
	spin_lock_init(&cdev->pmap.lock);

	mutex_lock(&cdev_mutex);
	list_add_tail(&cdev->list_head, &cdev_list);
	mutex_unlock(&cdev_mutex);

	spin_lock(&cdev_rcu_lock);
	list_add_tail_rcu(&cdev->rcu_node, &cdev_rcu_list);
	spin_unlock(&cdev_rcu_lock);

	log_debug(1 << CXGBI_DBG_DEV,
		"cdev 0x%p, p# %u.\n", cdev, nports);
	return cdev;
}
EXPORT_SYMBOL_GPL(cxgbi_device_register);

void cxgbi_device_unregister(struct cxgbi_device *cdev)
{
	log_debug(1 << CXGBI_DBG_DEV,
		"cdev 0x%p, p# %u,%s.\n",
		cdev, cdev->nports, cdev->nports ? cdev->ports[0]->name : "");

	mutex_lock(&cdev_mutex);
	list_del(&cdev->list_head);
	mutex_unlock(&cdev_mutex);

	spin_lock(&cdev_rcu_lock);
	list_del_rcu(&cdev->rcu_node);
	spin_unlock(&cdev_rcu_lock);
	synchronize_rcu();

	cxgbi_device_destroy(cdev);
}
EXPORT_SYMBOL_GPL(cxgbi_device_unregister);

void cxgbi_device_unregister_all(unsigned int flag)
{
	struct cxgbi_device *cdev, *tmp;

	mutex_lock(&cdev_mutex);
	list_for_each_entry_safe(cdev, tmp, &cdev_list, list_head) {
		if ((cdev->flags & flag) == flag) {
			mutex_unlock(&cdev_mutex);
			cxgbi_device_unregister(cdev);
			mutex_lock(&cdev_mutex);
		}
	}
	mutex_unlock(&cdev_mutex);
}
EXPORT_SYMBOL_GPL(cxgbi_device_unregister_all);

struct cxgbi_device *cxgbi_device_find_by_lldev(void *lldev)
{
	struct cxgbi_device *cdev, *tmp;

	mutex_lock(&cdev_mutex);
	list_for_each_entry_safe(cdev, tmp, &cdev_list, list_head) {
		if (cdev->lldev == lldev) {
			mutex_unlock(&cdev_mutex);
			return cdev;
		}
	}
	mutex_unlock(&cdev_mutex);

	log_debug(1 << CXGBI_DBG_DEV,
		"lldev 0x%p, NO match found.\n", lldev);
	return NULL;
}
EXPORT_SYMBOL_GPL(cxgbi_device_find_by_lldev);

struct cxgbi_device *cxgbi_device_find_by_netdev(struct net_device *ndev,
						 int *port)
{
	struct net_device *vdev = NULL;
	struct cxgbi_device *cdev, *tmp;
	int i;

	if (is_vlan_dev(ndev)) {
		vdev = ndev;
		ndev = vlan_dev_real_dev(ndev);
		log_debug(1 << CXGBI_DBG_DEV,
			"vlan dev %s -> %s.\n", vdev->name, ndev->name);
	}

	mutex_lock(&cdev_mutex);
	list_for_each_entry_safe(cdev, tmp, &cdev_list, list_head) {
		for (i = 0; i < cdev->nports; i++) {
			if (ndev == cdev->ports[i]) {
				cdev->hbas[i]->vdev = vdev;
				mutex_unlock(&cdev_mutex);
				if (port)
					*port = i;
				return cdev;
			}
		}
	}
	mutex_unlock(&cdev_mutex);
	log_debug(1 << CXGBI_DBG_DEV,
		"ndev 0x%p, %s, NO match found.\n", ndev, ndev->name);
	return NULL;
}
EXPORT_SYMBOL_GPL(cxgbi_device_find_by_netdev);

struct cxgbi_device *cxgbi_device_find_by_netdev_rcu(struct net_device *ndev,
						     int *port)
{
	struct net_device *vdev = NULL;
	struct cxgbi_device *cdev;
	int i;

	if (is_vlan_dev(ndev)) {
		vdev = ndev;
		ndev = vlan_dev_real_dev(ndev);
		pr_info("vlan dev %s -> %s.\n", vdev->name, ndev->name);
	}

	rcu_read_lock();
	list_for_each_entry_rcu(cdev, &cdev_rcu_list, rcu_node) {
		for (i = 0; i < cdev->nports; i++) {
			if (ndev == cdev->ports[i]) {
				cdev->hbas[i]->vdev = vdev;
				rcu_read_unlock();
				if (port)
					*port = i;
				return cdev;
			}
		}
	}
	rcu_read_unlock();

	log_debug(1 << CXGBI_DBG_DEV,
		  "ndev 0x%p, %s, NO match found.\n", ndev, ndev->name);
	return NULL;
}
EXPORT_SYMBOL_GPL(cxgbi_device_find_by_netdev_rcu);

static struct cxgbi_device *cxgbi_device_find_by_mac(struct net_device *ndev,
						     int *port)
{
	struct net_device *vdev = NULL;
	struct cxgbi_device *cdev, *tmp;
	int i;

	if (is_vlan_dev(ndev)) {
		vdev = ndev;
		ndev = vlan_dev_real_dev(ndev);
		pr_info("vlan dev %s -> %s.\n", vdev->name, ndev->name);
	}

	mutex_lock(&cdev_mutex);
	list_for_each_entry_safe(cdev, tmp, &cdev_list, list_head) {
		for (i = 0; i < cdev->nports; i++) {
			if (!memcmp(ndev->dev_addr, cdev->ports[i]->dev_addr,
				    MAX_ADDR_LEN)) {
				cdev->hbas[i]->vdev = vdev;
				mutex_unlock(&cdev_mutex);
				if (port)
					*port = i;
				return cdev;
			}
		}
	}
	mutex_unlock(&cdev_mutex);
	log_debug(1 << CXGBI_DBG_DEV,
		  "ndev 0x%p, %s, NO match mac found.\n",
		  ndev, ndev->name);
	return NULL;
}

void cxgbi_hbas_remove(struct cxgbi_device *cdev)
{
	int i;
	struct cxgbi_hba *chba;

	log_debug(1 << CXGBI_DBG_DEV,
		"cdev 0x%p, p#%u.\n", cdev, cdev->nports);

	for (i = 0; i < cdev->nports; i++) {
		chba = cdev->hbas[i];
		if (chba) {
			cdev->hbas[i] = NULL;
			iscsi_host_remove(chba->shost);
			pci_dev_put(cdev->pdev);
			iscsi_host_free(chba->shost);
		}
	}
}
EXPORT_SYMBOL_GPL(cxgbi_hbas_remove);

int cxgbi_hbas_add(struct cxgbi_device *cdev, u64 max_lun,
		unsigned int max_conns, struct scsi_host_template *sht,
		struct scsi_transport_template *stt)
{
	struct cxgbi_hba *chba;
	struct Scsi_Host *shost;
	int i, err;

	log_debug(1 << CXGBI_DBG_DEV, "cdev 0x%p, p#%u.\n", cdev, cdev->nports);

	for (i = 0; i < cdev->nports; i++) {
		shost = iscsi_host_alloc(sht, sizeof(*chba), 1);
		if (!shost) {
			pr_info("0x%p, p%d, %s, host alloc failed.\n",
				cdev, i, cdev->ports[i]->name);
			err = -ENOMEM;
			goto err_out;
		}

		shost->transportt = stt;
		shost->max_lun = max_lun;
		shost->max_id = max_conns - 1;
		shost->max_channel = 0;
		shost->max_cmd_len = SCSI_MAX_VARLEN_CDB_SIZE;

		chba = iscsi_host_priv(shost);
		chba->cdev = cdev;
		chba->ndev = cdev->ports[i];
		chba->shost = shost;

		shost->can_queue = sht->can_queue - ISCSI_MGMT_CMDS_MAX;

		log_debug(1 << CXGBI_DBG_DEV,
			"cdev 0x%p, p#%d %s: chba 0x%p.\n",
			cdev, i, cdev->ports[i]->name, chba);

		pci_dev_get(cdev->pdev);
		err = iscsi_host_add(shost, &cdev->pdev->dev);
		if (err) {
			pr_info("cdev 0x%p, p#%d %s, host add failed.\n",
				cdev, i, cdev->ports[i]->name);
			pci_dev_put(cdev->pdev);
			scsi_host_put(shost);
			goto  err_out;
		}

		cdev->hbas[i] = chba;
	}

	return 0;

err_out:
	cxgbi_hbas_remove(cdev);
	return err;
}
EXPORT_SYMBOL_GPL(cxgbi_hbas_add);

/*
 * iSCSI offload
 *
 * - source port management
 *   To find a free source port in the port allocation map we use a very simple
 *   rotor scheme to look for the next free port.
 *
 *   If a source port has been specified make sure that it doesn't collide with
 *   our normal source port allocation map.  If it's outside the range of our
 *   allocation/deallocation scheme just let them use it.
 *
 *   If the source port is outside our allocation range, the caller is
 *   responsible for keeping track of their port usage.
 */

static struct cxgbi_sock *find_sock_on_port(struct cxgbi_device *cdev,
					    unsigned char port_id)
{
	struct cxgbi_ports_map *pmap = &cdev->pmap;
	unsigned int i;
	unsigned int used;

	if (!pmap->max_connect || !pmap->used)
		return NULL;

	spin_lock_bh(&pmap->lock);
	used = pmap->used;
	for (i = 0; used && i < pmap->max_connect; i++) {
		struct cxgbi_sock *csk = pmap->port_csk[i];

		if (csk) {
			if (csk->port_id == port_id) {
				spin_unlock_bh(&pmap->lock);
				return csk;
			}
			used--;
		}
	}
	spin_unlock_bh(&pmap->lock);

	return NULL;
}

static int sock_get_port(struct cxgbi_sock *csk)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgbi_ports_map *pmap = &cdev->pmap;
	unsigned int start;
	int idx;
	__be16 *port;

	if (!pmap->max_connect) {
		pr_err("cdev 0x%p, p#%u %s, NO port map.\n",
			   cdev, csk->port_id, cdev->ports[csk->port_id]->name);
		return -EADDRNOTAVAIL;
	}

	if (csk->csk_family == AF_INET)
		port = &csk->saddr.sin_port;
	else /* ipv6 */
		port = &csk->saddr6.sin6_port;

	if (*port) {
		pr_err("source port NON-ZERO %u.\n",
			ntohs(*port));
		return -EADDRINUSE;
	}

	spin_lock_bh(&pmap->lock);
	if (pmap->used >= pmap->max_connect) {
		spin_unlock_bh(&pmap->lock);
		pr_info("cdev 0x%p, p#%u %s, ALL ports used.\n",
			cdev, csk->port_id, cdev->ports[csk->port_id]->name);
		return -EADDRNOTAVAIL;
	}

	start = idx = pmap->next;
	do {
		if (++idx >= pmap->max_connect)
			idx = 0;
		if (!pmap->port_csk[idx]) {
			pmap->used++;
			*port = htons(pmap->sport_base + idx);
			pmap->next = idx;
			pmap->port_csk[idx] = csk;
			spin_unlock_bh(&pmap->lock);
			cxgbi_sock_get(csk);
			log_debug(1 << CXGBI_DBG_SOCK,
				"cdev 0x%p, p#%u %s, p %u, %u.\n",
				cdev, csk->port_id,
				cdev->ports[csk->port_id]->name,
				pmap->sport_base + idx, pmap->next);
			return 0;
		}
	} while (idx != start);
	spin_unlock_bh(&pmap->lock);

	/* should not happen */
	pr_warn("cdev 0x%p, p#%u %s, next %u?\n",
		cdev, csk->port_id, cdev->ports[csk->port_id]->name,
		pmap->next);
	return -EADDRNOTAVAIL;
}

static void sock_put_port(struct cxgbi_sock *csk)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgbi_ports_map *pmap = &cdev->pmap;
	__be16 *port;

	if (csk->csk_family == AF_INET)
		port = &csk->saddr.sin_port;
	else /* ipv6 */
		port = &csk->saddr6.sin6_port;

	if (*port) {
		int idx = ntohs(*port) - pmap->sport_base;

		*port = 0;
		if (idx < 0 || idx >= pmap->max_connect) {
			pr_err("cdev 0x%p, p#%u %s, port %u OOR.\n",
				cdev, csk->port_id,
				cdev->ports[csk->port_id]->name,
				ntohs(*port));
			return;
		}

		spin_lock_bh(&pmap->lock);
		pmap->port_csk[idx] = NULL;
		pmap->used--;
		spin_unlock_bh(&pmap->lock);

		log_debug(1 << CXGBI_DBG_SOCK,
			"cdev 0x%p, p#%u %s, release %u.\n",
			cdev, csk->port_id, cdev->ports[csk->port_id]->name,
			pmap->sport_base + idx);

		cxgbi_sock_put(csk);
	}
}

/*
 * iscsi tcp connection
 */
void cxgbi_sock_free_cpl_skbs(struct cxgbi_sock *csk)
{
	if (csk->cpl_close) {
		kfree_skb(csk->cpl_close);
		csk->cpl_close = NULL;
	}
	if (csk->cpl_abort_req) {
		kfree_skb(csk->cpl_abort_req);
		csk->cpl_abort_req = NULL;
	}
	if (csk->cpl_abort_rpl) {
		kfree_skb(csk->cpl_abort_rpl);
		csk->cpl_abort_rpl = NULL;
	}
}
EXPORT_SYMBOL_GPL(cxgbi_sock_free_cpl_skbs);

static struct cxgbi_sock *cxgbi_sock_create(struct cxgbi_device *cdev)
{
	struct cxgbi_sock *csk = kzalloc(sizeof(*csk), GFP_NOIO);

	if (!csk) {
		pr_info("alloc csk %zu failed.\n", sizeof(*csk));
		return NULL;
	}

	if (cdev->csk_alloc_cpls(csk) < 0) {
		pr_info("csk 0x%p, alloc cpls failed.\n", csk);
		kfree(csk);
		return NULL;
	}

	spin_lock_init(&csk->lock);
	kref_init(&csk->refcnt);
	skb_queue_head_init(&csk->receive_queue);
	skb_queue_head_init(&csk->write_queue);
	timer_setup(&csk->retry_timer, NULL, 0);
	init_completion(&csk->cmpl);
	rwlock_init(&csk->callback_lock);
	csk->cdev = cdev;
	csk->flags = 0;
	cxgbi_sock_set_state(csk, CTP_CLOSED);

	log_debug(1 << CXGBI_DBG_SOCK, "cdev 0x%p, new csk 0x%p.\n", cdev, csk);

	return csk;
}

static struct rtable *find_route_ipv4(struct flowi4 *fl4,
				      __be32 saddr, __be32 daddr,
				      __be16 sport, __be16 dport, u8 tos,
				      int ifindex)
{
	struct rtable *rt;

	rt = ip_route_output_ports(&init_net, fl4, NULL, daddr, saddr,
				   dport, sport, IPPROTO_TCP, tos, ifindex);
	if (IS_ERR(rt))
		return NULL;

	return rt;
}

static struct cxgbi_sock *
cxgbi_check_route(struct sockaddr *dst_addr, int ifindex)
{
	struct sockaddr_in *daddr = (struct sockaddr_in *)dst_addr;
	struct dst_entry *dst;
	struct net_device *ndev;
	struct cxgbi_device *cdev;
	struct rtable *rt = NULL;
	struct neighbour *n;
	struct flowi4 fl4;
	struct cxgbi_sock *csk = NULL;
	unsigned int mtu = 0;
	int port = 0xFFFF;
	int err = 0;

	rt = find_route_ipv4(&fl4, 0, daddr->sin_addr.s_addr, 0,
			     daddr->sin_port, 0, ifindex);
	if (!rt) {
		pr_info("no route to ipv4 0x%x, port %u.\n",
			be32_to_cpu(daddr->sin_addr.s_addr),
			be16_to_cpu(daddr->sin_port));
		err = -ENETUNREACH;
		goto err_out;
	}
	dst = &rt->dst;
	n = dst_neigh_lookup(dst, &daddr->sin_addr.s_addr);
	if (!n) {
		err = -ENODEV;
		goto rel_rt;
	}
	ndev = n->dev;

	if (rt->rt_flags & (RTCF_MULTICAST | RTCF_BROADCAST)) {
		pr_info("multi-cast route %pI4, port %u, dev %s.\n",
			&daddr->sin_addr.s_addr, ntohs(daddr->sin_port),
			ndev->name);
		err = -ENETUNREACH;
		goto rel_neigh;
	}

	if (ndev->flags & IFF_LOOPBACK) {
		ndev = ip_dev_find(&init_net, daddr->sin_addr.s_addr);
		if (!ndev) {
			err = -ENETUNREACH;
			goto rel_neigh;
		}
		mtu = ndev->mtu;
		pr_info("rt dev %s, loopback -> %s, mtu %u.\n",
			n->dev->name, ndev->name, mtu);
	}

	if (!(ndev->flags & IFF_UP) || !netif_carrier_ok(ndev)) {
		pr_info("%s interface not up.\n", ndev->name);
		err = -ENETDOWN;
		goto rel_neigh;
	}

	cdev = cxgbi_device_find_by_netdev(ndev, &port);
	if (!cdev)
		cdev = cxgbi_device_find_by_mac(ndev, &port);
	if (!cdev) {
		pr_info("dst %pI4, %s, NOT cxgbi device.\n",
			&daddr->sin_addr.s_addr, ndev->name);
		err = -ENETUNREACH;
		goto rel_neigh;
	}
	log_debug(1 << CXGBI_DBG_SOCK,
		"route to %pI4 :%u, ndev p#%d,%s, cdev 0x%p.\n",
		&daddr->sin_addr.s_addr, ntohs(daddr->sin_port),
			   port, ndev->name, cdev);

	csk = cxgbi_sock_create(cdev);
	if (!csk) {
		err = -ENOMEM;
		goto rel_neigh;
	}
	csk->cdev = cdev;
	csk->port_id = port;
	csk->mtu = mtu;
	csk->dst = dst;

	csk->csk_family = AF_INET;
	csk->daddr.sin_addr.s_addr = daddr->sin_addr.s_addr;
	csk->daddr.sin_port = daddr->sin_port;
	csk->daddr.sin_family = daddr->sin_family;
	csk->saddr.sin_family = daddr->sin_family;
	csk->saddr.sin_addr.s_addr = fl4.saddr;
	neigh_release(n);

	return csk;

rel_neigh:
	neigh_release(n);

rel_rt:
	ip_rt_put(rt);
err_out:
	return ERR_PTR(err);
}

#if IS_ENABLED(CONFIG_IPV6)
static struct rt6_info *find_route_ipv6(const struct in6_addr *saddr,
					const struct in6_addr *daddr,
					int ifindex)
{
	struct flowi6 fl;

	memset(&fl, 0, sizeof(fl));
	fl.flowi6_oif = ifindex;
	if (saddr)
		memcpy(&fl.saddr, saddr, sizeof(struct in6_addr));
	if (daddr)
		memcpy(&fl.daddr, daddr, sizeof(struct in6_addr));
	return (struct rt6_info *)ip6_route_output(&init_net, NULL, &fl);
}

static struct cxgbi_sock *
cxgbi_check_route6(struct sockaddr *dst_addr, int ifindex)
{
	struct sockaddr_in6 *daddr6 = (struct sockaddr_in6 *)dst_addr;
	struct dst_entry *dst;
	struct net_device *ndev;
	struct cxgbi_device *cdev;
	struct rt6_info *rt = NULL;
	struct neighbour *n;
	struct in6_addr pref_saddr;
	struct cxgbi_sock *csk = NULL;
	unsigned int mtu = 0;
	int port = 0xFFFF;
	int err = 0;

	rt = find_route_ipv6(NULL, &daddr6->sin6_addr, ifindex);

	if (!rt) {
		pr_info("no route to ipv6 %pI6 port %u\n",
			daddr6->sin6_addr.s6_addr,
			be16_to_cpu(daddr6->sin6_port));
		err = -ENETUNREACH;
		goto err_out;
	}

	dst = &rt->dst;

	n = dst_neigh_lookup(dst, &daddr6->sin6_addr);

	if (!n) {
		pr_info("%pI6, port %u, dst no neighbour.\n",
			daddr6->sin6_addr.s6_addr,
			be16_to_cpu(daddr6->sin6_port));
		err = -ENETUNREACH;
		goto rel_rt;
	}
	ndev = n->dev;

	if (!(ndev->flags & IFF_UP) || !netif_carrier_ok(ndev)) {
		pr_info("%s interface not up.\n", ndev->name);
		err = -ENETDOWN;
		goto rel_rt;
	}

	if (ipv6_addr_is_multicast(&daddr6->sin6_addr)) {
		pr_info("multi-cast route %pI6 port %u, dev %s.\n",
			daddr6->sin6_addr.s6_addr,
			ntohs(daddr6->sin6_port), ndev->name);
		err = -ENETUNREACH;
		goto rel_rt;
	}

	cdev = cxgbi_device_find_by_netdev(ndev, &port);
	if (!cdev)
		cdev = cxgbi_device_find_by_mac(ndev, &port);
	if (!cdev) {
		pr_info("dst %pI6 %s, NOT cxgbi device.\n",
			daddr6->sin6_addr.s6_addr, ndev->name);
		err = -ENETUNREACH;
		goto rel_rt;
	}
	log_debug(1 << CXGBI_DBG_SOCK,
		  "route to %pI6 :%u, ndev p#%d,%s, cdev 0x%p.\n",
		  daddr6->sin6_addr.s6_addr, ntohs(daddr6->sin6_port), port,
		  ndev->name, cdev);

	csk = cxgbi_sock_create(cdev);
	if (!csk) {
		err = -ENOMEM;
		goto rel_rt;
	}
	csk->cdev = cdev;
	csk->port_id = port;
	csk->mtu = mtu;
	csk->dst = dst;

	rt6_get_prefsrc(rt, &pref_saddr);
	if (ipv6_addr_any(&pref_saddr)) {
		struct inet6_dev *idev = ip6_dst_idev((struct dst_entry *)rt);

		err = ipv6_dev_get_saddr(&init_net, idev ? idev->dev : NULL,
					 &daddr6->sin6_addr, 0, &pref_saddr);
		if (err) {
			pr_info("failed to get source address to reach %pI6\n",
				&daddr6->sin6_addr);
			goto rel_rt;
		}
	}

	csk->csk_family = AF_INET6;
	csk->daddr6.sin6_addr = daddr6->sin6_addr;
	csk->daddr6.sin6_port = daddr6->sin6_port;
	csk->daddr6.sin6_family = daddr6->sin6_family;
	csk->saddr6.sin6_family = daddr6->sin6_family;
	csk->saddr6.sin6_addr = pref_saddr;

	neigh_release(n);
	return csk;

rel_rt:
	if (n)
		neigh_release(n);

	ip6_rt_put(rt);
	if (csk)
		cxgbi_sock_closed(csk);
err_out:
	return ERR_PTR(err);
}
#endif /* IS_ENABLED(CONFIG_IPV6) */

void cxgbi_sock_established(struct cxgbi_sock *csk, unsigned int snd_isn,
			unsigned int opt)
{
	csk->write_seq = csk->snd_nxt = csk->snd_una = snd_isn;
	dst_confirm(csk->dst);
	smp_mb();
	cxgbi_sock_set_state(csk, CTP_ESTABLISHED);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_established);

static void cxgbi_inform_iscsi_conn_closing(struct cxgbi_sock *csk)
{
	log_debug(1 << CXGBI_DBG_SOCK,
		"csk 0x%p, state %u, flags 0x%lx, conn 0x%p.\n",
		csk, csk->state, csk->flags, csk->user_data);

	if (csk->state != CTP_ESTABLISHED) {
		read_lock_bh(&csk->callback_lock);
		if (csk->user_data)
			iscsi_conn_failure(csk->user_data,
					ISCSI_ERR_TCP_CONN_CLOSE);
		read_unlock_bh(&csk->callback_lock);
	}
}

void cxgbi_sock_closed(struct cxgbi_sock *csk)
{
	log_debug(1 << CXGBI_DBG_SOCK, "csk 0x%p,%u,0x%lx,%u.\n",
		csk, (csk)->state, (csk)->flags, (csk)->tid);
	cxgbi_sock_set_flag(csk, CTPF_ACTIVE_CLOSE_NEEDED);
	if (csk->state == CTP_ACTIVE_OPEN || csk->state == CTP_CLOSED)
		return;
	if (csk->saddr.sin_port)
		sock_put_port(csk);
	if (csk->dst)
		dst_release(csk->dst);
	csk->cdev->csk_release_offload_resources(csk);
	cxgbi_sock_set_state(csk, CTP_CLOSED);
	cxgbi_inform_iscsi_conn_closing(csk);
	cxgbi_sock_put(csk);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_closed);

static void need_active_close(struct cxgbi_sock *csk)
{
	int data_lost;
	int close_req = 0;

	log_debug(1 << CXGBI_DBG_SOCK, "csk 0x%p,%u,0x%lx,%u.\n",
		csk, (csk)->state, (csk)->flags, (csk)->tid);
	spin_lock_bh(&csk->lock);
	if (csk->dst)
		dst_confirm(csk->dst);
	data_lost = skb_queue_len(&csk->receive_queue);
	__skb_queue_purge(&csk->receive_queue);

	if (csk->state == CTP_ACTIVE_OPEN)
		cxgbi_sock_set_flag(csk, CTPF_ACTIVE_CLOSE_NEEDED);
	else if (csk->state == CTP_ESTABLISHED) {
		close_req = 1;
		cxgbi_sock_set_state(csk, CTP_ACTIVE_CLOSE);
	} else if (csk->state == CTP_PASSIVE_CLOSE) {
		close_req = 1;
		cxgbi_sock_set_state(csk, CTP_CLOSE_WAIT_2);
	}

	if (close_req) {
		if (!cxgbi_sock_flag(csk, CTPF_LOGOUT_RSP_RCVD) ||
		    data_lost)
			csk->cdev->csk_send_abort_req(csk);
		else
			csk->cdev->csk_send_close_req(csk);
	}

	spin_unlock_bh(&csk->lock);
}

void cxgbi_sock_fail_act_open(struct cxgbi_sock *csk, int errno)
{
	pr_info("csk 0x%p,%u,%lx, %pI4:%u-%pI4:%u, err %d.\n",
			csk, csk->state, csk->flags,
			&csk->saddr.sin_addr.s_addr, csk->saddr.sin_port,
			&csk->daddr.sin_addr.s_addr, csk->daddr.sin_port,
			errno);

	cxgbi_sock_set_state(csk, CTP_CONNECTING);
	csk->err = errno;
	cxgbi_sock_closed(csk);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_fail_act_open);

void cxgbi_sock_act_open_req_arp_failure(void *handle, struct sk_buff *skb)
{
	struct cxgbi_sock *csk = (struct cxgbi_sock *)skb->sk;
	struct module *owner = csk->cdev->owner;

	log_debug(1 << CXGBI_DBG_SOCK, "csk 0x%p,%u,0x%lx,%u.\n",
		csk, (csk)->state, (csk)->flags, (csk)->tid);
	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);
	if (csk->state == CTP_ACTIVE_OPEN)
		cxgbi_sock_fail_act_open(csk, -EHOSTUNREACH);
	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
	__kfree_skb(skb);

	module_put(owner);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_act_open_req_arp_failure);

void cxgbi_sock_rcv_abort_rpl(struct cxgbi_sock *csk)
{
	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);

	cxgbi_sock_set_flag(csk, CTPF_ABORT_RPL_RCVD);
	if (cxgbi_sock_flag(csk, CTPF_ABORT_RPL_PENDING)) {
		cxgbi_sock_clear_flag(csk, CTPF_ABORT_RPL_PENDING);
		if (cxgbi_sock_flag(csk, CTPF_ABORT_REQ_RCVD))
			pr_err("csk 0x%p,%u,0x%lx,%u,ABT_RPL_RSS.\n",
			       csk, csk->state, csk->flags, csk->tid);
		cxgbi_sock_closed(csk);
	}

	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_rcv_abort_rpl);

void cxgbi_sock_rcv_peer_close(struct cxgbi_sock *csk)
{
	log_debug(1 << CXGBI_DBG_SOCK, "csk 0x%p,%u,0x%lx,%u.\n",
		csk, (csk)->state, (csk)->flags, (csk)->tid);
	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);

	if (cxgbi_sock_flag(csk, CTPF_ABORT_RPL_PENDING))
		goto done;

	switch (csk->state) {
	case CTP_ESTABLISHED:
		cxgbi_sock_set_state(csk, CTP_PASSIVE_CLOSE);
		break;
	case CTP_ACTIVE_CLOSE:
		cxgbi_sock_set_state(csk, CTP_CLOSE_WAIT_2);
		break;
	case CTP_CLOSE_WAIT_1:
		cxgbi_sock_closed(csk);
		break;
	case CTP_ABORTING:
		break;
	default:
		pr_err("csk 0x%p,%u,0x%lx,%u, bad state.\n",
			csk, csk->state, csk->flags, csk->tid);
	}
	cxgbi_inform_iscsi_conn_closing(csk);
done:
	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_rcv_peer_close);

void cxgbi_sock_rcv_close_conn_rpl(struct cxgbi_sock *csk, u32 snd_nxt)
{
	log_debug(1 << CXGBI_DBG_SOCK, "csk 0x%p,%u,0x%lx,%u.\n",
		csk, (csk)->state, (csk)->flags, (csk)->tid);
	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);

	csk->snd_una = snd_nxt - 1;
	if (cxgbi_sock_flag(csk, CTPF_ABORT_RPL_PENDING))
		goto done;

	switch (csk->state) {
	case CTP_ACTIVE_CLOSE:
		cxgbi_sock_set_state(csk, CTP_CLOSE_WAIT_1);
		break;
	case CTP_CLOSE_WAIT_1:
	case CTP_CLOSE_WAIT_2:
		cxgbi_sock_closed(csk);
		break;
	case CTP_ABORTING:
		break;
	default:
		pr_err("csk 0x%p,%u,0x%lx,%u, bad state.\n",
			csk, csk->state, csk->flags, csk->tid);
	}
done:
	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_rcv_close_conn_rpl);

void cxgbi_sock_rcv_wr_ack(struct cxgbi_sock *csk, unsigned int credits,
			   unsigned int snd_una, int seq_chk)
{
	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
			"csk 0x%p,%u,0x%lx,%u, cr %u,%u+%u, snd_una %u,%d.\n",
			csk, csk->state, csk->flags, csk->tid, credits,
			csk->wr_cred, csk->wr_una_cred, snd_una, seq_chk);

	spin_lock_bh(&csk->lock);

	csk->wr_cred += credits;
	if (csk->wr_una_cred > csk->wr_max_cred - csk->wr_cred)
		csk->wr_una_cred = csk->wr_max_cred - csk->wr_cred;

	while (credits) {
		struct sk_buff *p = cxgbi_sock_peek_wr(csk);

		if (unlikely(!p)) {
			pr_err("csk 0x%p,%u,0x%lx,%u, cr %u,%u+%u, empty.\n",
				csk, csk->state, csk->flags, csk->tid, credits,
				csk->wr_cred, csk->wr_una_cred);
			break;
		}

		if (unlikely(credits < p->csum)) {
			pr_warn("csk 0x%p,%u,0x%lx,%u, cr %u,%u+%u, < %u.\n",
				csk, csk->state, csk->flags, csk->tid,
				credits, csk->wr_cred, csk->wr_una_cred,
				p->csum);
			p->csum -= credits;
			break;
		} else {
			cxgbi_sock_dequeue_wr(csk);
			credits -= p->csum;
			kfree_skb(p);
		}
	}

	cxgbi_sock_check_wr_invariants(csk);

	if (seq_chk) {
		if (unlikely(before(snd_una, csk->snd_una))) {
			pr_warn("csk 0x%p,%u,0x%lx,%u, snd_una %u/%u.",
				csk, csk->state, csk->flags, csk->tid, snd_una,
				csk->snd_una);
			goto done;
		}

		if (csk->snd_una != snd_una) {
			csk->snd_una = snd_una;
			dst_confirm(csk->dst);
		}
	}

	if (skb_queue_len(&csk->write_queue)) {
		if (csk->cdev->csk_push_tx_frames(csk, 0))
			cxgbi_conn_tx_open(csk);
	} else
		cxgbi_conn_tx_open(csk);
done:
	spin_unlock_bh(&csk->lock);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_rcv_wr_ack);

static unsigned int cxgbi_sock_find_best_mtu(struct cxgbi_sock *csk,
					     unsigned short mtu)
{
	int i = 0;

	while (i < csk->cdev->nmtus - 1 && csk->cdev->mtus[i + 1] <= mtu)
		++i;

	return i;
}

unsigned int cxgbi_sock_select_mss(struct cxgbi_sock *csk, unsigned int pmtu)
{
	unsigned int idx;
	struct dst_entry *dst = csk->dst;

	csk->advmss = dst_metric_advmss(dst);

	if (csk->advmss > pmtu - 40)
		csk->advmss = pmtu - 40;
	if (csk->advmss < csk->cdev->mtus[0] - 40)
		csk->advmss = csk->cdev->mtus[0] - 40;
	idx = cxgbi_sock_find_best_mtu(csk, csk->advmss + 40);

	return idx;
}
EXPORT_SYMBOL_GPL(cxgbi_sock_select_mss);

void cxgbi_sock_skb_entail(struct cxgbi_sock *csk, struct sk_buff *skb)
{
	cxgbi_skcb_tcp_seq(skb) = csk->write_seq;
	__skb_queue_tail(&csk->write_queue, skb);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_skb_entail);

void cxgbi_sock_purge_wr_queue(struct cxgbi_sock *csk)
{
	struct sk_buff *skb;

	while ((skb = cxgbi_sock_dequeue_wr(csk)) != NULL)
		kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_purge_wr_queue);

void cxgbi_sock_check_wr_invariants(const struct cxgbi_sock *csk)
{
	int pending = cxgbi_sock_count_pending_wrs(csk);

	if (unlikely(csk->wr_cred + pending != csk->wr_max_cred))
		pr_err("csk 0x%p, tid %u, credit %u + %u != %u.\n",
			csk, csk->tid, csk->wr_cred, pending, csk->wr_max_cred);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_check_wr_invariants);

static inline void
scmd_get_params(struct scsi_cmnd *sc, struct scatterlist **sgl,
		unsigned int *sgcnt, unsigned int *dlen,
		unsigned int prot)
{
	struct scsi_data_buffer *sdb = prot ? scsi_prot(sc) : &sc->sdb;

	*sgl = sdb->table.sgl;
	*sgcnt = sdb->table.nents;
	*dlen = sdb->length;
	/* Caution: for protection sdb, sdb->length is invalid */
}

void cxgbi_ddp_set_one_ppod(struct cxgbi_pagepod *ppod,
			    struct cxgbi_task_tag_info *ttinfo,
			    struct scatterlist **sg_pp, unsigned int *sg_off)
{
	struct scatterlist *sg = sg_pp ? *sg_pp : NULL;
	unsigned int offset = sg_off ? *sg_off : 0;
	dma_addr_t addr = 0UL;
	unsigned int len = 0;
	int i;

	memcpy(ppod, &ttinfo->hdr, sizeof(struct cxgbi_pagepod_hdr));

	if (sg) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);
	}

	for (i = 0; i < PPOD_PAGES_MAX; i++) {
		if (sg) {
			ppod->addr[i] = cpu_to_be64(addr + offset);
			offset += PAGE_SIZE;
			if (offset == (len + sg->offset)) {
				offset = 0;
				sg = sg_next(sg);
				if (sg) {
					addr = sg_dma_address(sg);
					len = sg_dma_len(sg);
				}
			}
		} else {
			ppod->addr[i] = 0ULL;
		}
	}

	/*
	 * the fifth address needs to be repeated in the next ppod, so do
	 * not move sg
	 */
	if (sg_pp) {
		*sg_pp = sg;
		*sg_off = offset;
	}

	if (offset == len) {
		offset = 0;
		sg = sg_next(sg);
		if (sg) {
			addr = sg_dma_address(sg);
			len = sg_dma_len(sg);
		}
	}
	ppod->addr[i] = sg ? cpu_to_be64(addr + offset) : 0ULL;
}
EXPORT_SYMBOL_GPL(cxgbi_ddp_set_one_ppod);

/*
 * APIs interacting with open-iscsi libraries
 */

int cxgbi_ddp_ppm_setup(void **ppm_pp, struct cxgbi_device *cdev,
			struct cxgbi_tag_format *tformat,
			unsigned int iscsi_size, unsigned int llimit,
			unsigned int start, unsigned int rsvd_factor,
			unsigned int edram_start, unsigned int edram_size)
{
	int err = cxgbi_ppm_init(ppm_pp, cdev->ports[0], cdev->pdev,
				cdev->lldev, tformat, iscsi_size, llimit, start,
				rsvd_factor, edram_start, edram_size);

	if (err >= 0) {
		struct cxgbi_ppm *ppm = (struct cxgbi_ppm *)(*ppm_pp);

		if (ppm->ppmax < 1024 ||
		    ppm->tformat.pgsz_idx_dflt >= DDP_PGIDX_MAX)
			cdev->flags |= CXGBI_FLAG_DDP_OFF;
		err = 0;
	} else {
		cdev->flags |= CXGBI_FLAG_DDP_OFF;
	}

	return err;
}
EXPORT_SYMBOL_GPL(cxgbi_ddp_ppm_setup);

static int cxgbi_ddp_sgl_check(struct scatterlist *sgl, int nents)
{
	int i;
	int last_sgidx = nents - 1;
	struct scatterlist *sg = sgl;

	for (i = 0; i < nents; i++, sg = sg_next(sg)) {
		unsigned int len = sg->length + sg->offset;

		if ((sg->offset & 0x3) || (i && sg->offset) ||
		    ((i != last_sgidx) && len != PAGE_SIZE)) {
			log_debug(1 << CXGBI_DBG_DDP,
				  "sg %u/%u, %u,%u, not aligned.\n",
				  i, nents, sg->offset, sg->length);
			goto err_out;
		}
	}
	return 0;
err_out:
	return -EINVAL;
}

static int cxgbi_ddp_reserve(struct cxgbi_conn *cconn,
			     struct cxgbi_task_data *tdata, u32 sw_tag,
			     unsigned int xferlen)
{
	struct cxgbi_sock *csk = cconn->cep->csk;
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgbi_ppm *ppm = cdev->cdev2ppm(cdev);
	struct cxgbi_task_tag_info *ttinfo = &tdata->ttinfo;
	struct scatterlist *sgl = ttinfo->sgl;
	unsigned int sgcnt = ttinfo->nents;
	unsigned int sg_offset = sgl->offset;
	int err;

	if (cdev->flags & CXGBI_FLAG_DDP_OFF) {
		log_debug(1 << CXGBI_DBG_DDP,
			  "cdev 0x%p DDP off.\n", cdev);
		return -EINVAL;
	}

	if (!ppm || xferlen < DDP_THRESHOLD || !sgcnt ||
	    ppm->tformat.pgsz_idx_dflt >= DDP_PGIDX_MAX) {
		log_debug(1 << CXGBI_DBG_DDP,
			  "ppm 0x%p, pgidx %u, xfer %u, sgcnt %u, NO ddp.\n",
			  ppm, ppm ? ppm->tformat.pgsz_idx_dflt : DDP_PGIDX_MAX,
			  xferlen, ttinfo->nents);
		return -EINVAL;
	}

	/* make sure the buffer is suitable for ddp */
	if (cxgbi_ddp_sgl_check(sgl, sgcnt) < 0)
		return -EINVAL;

	ttinfo->nr_pages = (xferlen + sgl->offset + (1 << PAGE_SHIFT) - 1) >>
			    PAGE_SHIFT;

	/*
	 * the ddp tag will be used for the itt in the outgoing pdu,
	 * the itt genrated by libiscsi is saved in the ppm and can be
	 * retrieved via the ddp tag
	 */
	err = cxgbi_ppm_ppods_reserve(ppm, ttinfo->nr_pages, 0, &ttinfo->idx,
				      &ttinfo->tag, (unsigned long)sw_tag);
	if (err < 0) {
		cconn->ddp_full++;
		return err;
	}
	ttinfo->npods = err;

	 /* setup dma from scsi command sgl */
	sgl->offset = 0;
	err = dma_map_sg(&ppm->pdev->dev, sgl, sgcnt, DMA_FROM_DEVICE);
	sgl->offset = sg_offset;
	if (err == 0) {
		pr_info("%s: 0x%x, xfer %u, sgl %u dma mapping err.\n",
			__func__, sw_tag, xferlen, sgcnt);
		goto rel_ppods;
	}
	if (err != ttinfo->nr_pages) {
		log_debug(1 << CXGBI_DBG_DDP,
			  "%s: sw tag 0x%x, xfer %u, sgl %u, dma count %d.\n",
			  __func__, sw_tag, xferlen, sgcnt, err);
	}

	ttinfo->flags |= CXGBI_PPOD_INFO_FLAG_MAPPED;
	ttinfo->cid = csk->port_id;

	cxgbi_ppm_make_ppod_hdr(ppm, ttinfo->tag, csk->tid, sgl->offset,
				xferlen, &ttinfo->hdr);

	if (cdev->flags & CXGBI_FLAG_USE_PPOD_OFLDQ) {
		/* write ppod from xmit_pdu (of iscsi_scsi_command pdu) */
		ttinfo->flags |= CXGBI_PPOD_INFO_FLAG_VALID;
	} else {
		/* write ppod from control queue now */
		err = cdev->csk_ddp_set_map(ppm, csk, ttinfo);
		if (err < 0)
			goto rel_ppods;
	}

	return 0;

rel_ppods:
	cxgbi_ppm_ppod_release(ppm, ttinfo->idx);

	if (ttinfo->flags & CXGBI_PPOD_INFO_FLAG_MAPPED) {
		ttinfo->flags &= ~CXGBI_PPOD_INFO_FLAG_MAPPED;
		dma_unmap_sg(&ppm->pdev->dev, sgl, sgcnt, DMA_FROM_DEVICE);
	}
	return -EINVAL;
}

static void task_release_itt(struct iscsi_task *task, itt_t hdr_itt)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_tcp_conn *tcp_conn = task->conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_device *cdev = cconn->chba->cdev;
	struct cxgbi_ppm *ppm = cdev->cdev2ppm(cdev);
	u32 tag = ntohl((__force u32)hdr_itt);

	log_debug(1 << CXGBI_DBG_DDP,
		  "cdev 0x%p, task 0x%p, release tag 0x%x.\n",
		  cdev, task, tag);
	if (sc && sc->sc_data_direction == DMA_FROM_DEVICE &&
	    cxgbi_ppm_is_ddp_tag(ppm, tag)) {
		struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
		struct cxgbi_task_tag_info *ttinfo = &tdata->ttinfo;

		if (!(cdev->flags & CXGBI_FLAG_USE_PPOD_OFLDQ))
			cdev->csk_ddp_clear_map(cdev, ppm, ttinfo);
		cxgbi_ppm_ppod_release(ppm, ttinfo->idx);
		dma_unmap_sg(&ppm->pdev->dev, ttinfo->sgl, ttinfo->nents,
			     DMA_FROM_DEVICE);
	}
}

static inline u32 cxgbi_build_sw_tag(u32 idx, u32 age)
{
	/* assume idx and age both are < 0x7FFF (32767) */
	return (idx << 16) | age;
}

static int task_reserve_itt(struct iscsi_task *task, itt_t *hdr_itt)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *sess = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_device *cdev = cconn->chba->cdev;
	struct cxgbi_ppm *ppm = cdev->cdev2ppm(cdev);
	u32 sw_tag = cxgbi_build_sw_tag(task->itt, sess->age);
	u32 tag = 0;
	int err = -EINVAL;

	if (sc && sc->sc_data_direction == DMA_FROM_DEVICE) {
		struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
		struct cxgbi_task_tag_info *ttinfo = &tdata->ttinfo;

		scmd_get_params(sc, &ttinfo->sgl, &ttinfo->nents,
				&tdata->dlen, 0);
		err = cxgbi_ddp_reserve(cconn, tdata, sw_tag, tdata->dlen);
		if (!err)
			tag = ttinfo->tag;
		else
			 log_debug(1 << CXGBI_DBG_DDP,
				   "csk 0x%p, R task 0x%p, %u,%u, no ddp.\n",
				   cconn->cep->csk, task, tdata->dlen,
				   ttinfo->nents);
	}

	if (err < 0) {
		err = cxgbi_ppm_make_non_ddp_tag(ppm, sw_tag, &tag);
		if (err < 0)
			return err;
	}
	/*  the itt need to sent in big-endian order */
	*hdr_itt = (__force itt_t)htonl(tag);

	log_debug(1 << CXGBI_DBG_DDP,
		  "cdev 0x%p, task 0x%p, 0x%x(0x%x,0x%x)->0x%x/0x%x.\n",
		  cdev, task, sw_tag, task->itt, sess->age, tag, *hdr_itt);
	return 0;
}

void cxgbi_parse_pdu_itt(struct iscsi_conn *conn, itt_t itt, int *idx, int *age)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_device *cdev = cconn->chba->cdev;
	struct cxgbi_ppm *ppm = cdev->cdev2ppm(cdev);
	u32 tag = ntohl((__force u32)itt);
	u32 sw_bits;

	if (ppm) {
		if (cxgbi_ppm_is_ddp_tag(ppm, tag))
			sw_bits = cxgbi_ppm_get_tag_caller_data(ppm, tag);
		else
			sw_bits = cxgbi_ppm_decode_non_ddp_tag(ppm, tag);
	} else {
		sw_bits = tag;
	}

	cxgbi_decode_sw_tag(sw_bits, idx, age);
	log_debug(1 << CXGBI_DBG_DDP,
		  "cdev 0x%p, tag 0x%x/0x%x, -> 0x%x(0x%x,0x%x).\n",
		  cdev, tag, itt, sw_bits, idx ? *idx : 0xFFFFF,
		  age ? *age : 0xFF);
}
EXPORT_SYMBOL_GPL(cxgbi_parse_pdu_itt);

void cxgbi_conn_tx_open(struct cxgbi_sock *csk)
{
	struct iscsi_conn *conn = csk->user_data;

	if (conn) {
		log_debug(1 << CXGBI_DBG_SOCK,
			"csk 0x%p, cid %d.\n", csk, conn->id);
		iscsi_conn_queue_work(conn);
	}
}
EXPORT_SYMBOL_GPL(cxgbi_conn_tx_open);

/*
 * pdu receive, interact with libiscsi_tcp
 */
static inline int read_pdu_skb(struct iscsi_conn *conn,
			       struct sk_buff *skb,
			       unsigned int offset,
			       int offloaded)
{
	int status = 0;
	int bytes_read;

	bytes_read = iscsi_tcp_recv_skb(conn, skb, offset, offloaded, &status);
	switch (status) {
	case ISCSI_TCP_CONN_ERR:
		pr_info("skb 0x%p, off %u, %d, TCP_ERR.\n",
			  skb, offset, offloaded);
		return -EIO;
	case ISCSI_TCP_SUSPENDED:
		log_debug(1 << CXGBI_DBG_PDU_RX,
			"skb 0x%p, off %u, %d, TCP_SUSPEND, rc %d.\n",
			skb, offset, offloaded, bytes_read);
		/* no transfer - just have caller flush queue */
		return bytes_read;
	case ISCSI_TCP_SKB_DONE:
		pr_info("skb 0x%p, off %u, %d, TCP_SKB_DONE.\n",
			skb, offset, offloaded);
		/*
		 * pdus should always fit in the skb and we should get
		 * segment done notifcation.
		 */
		iscsi_conn_printk(KERN_ERR, conn, "Invalid pdu or skb.");
		return -EFAULT;
	case ISCSI_TCP_SEGMENT_DONE:
		log_debug(1 << CXGBI_DBG_PDU_RX,
			"skb 0x%p, off %u, %d, TCP_SEG_DONE, rc %d.\n",
			skb, offset, offloaded, bytes_read);
		return bytes_read;
	default:
		pr_info("skb 0x%p, off %u, %d, invalid status %d.\n",
			skb, offset, offloaded, status);
		return -EINVAL;
	}
}

static int
skb_read_pdu_bhs(struct cxgbi_sock *csk, struct iscsi_conn *conn,
		 struct sk_buff *skb)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int err;

	log_debug(1 << CXGBI_DBG_PDU_RX,
		"conn 0x%p, skb 0x%p, len %u, flag 0x%lx.\n",
		conn, skb, skb->len, cxgbi_skcb_flags(skb));

	if (!iscsi_tcp_recv_segment_is_hdr(tcp_conn)) {
		pr_info("conn 0x%p, skb 0x%p, not hdr.\n", conn, skb);
		iscsi_conn_failure(conn, ISCSI_ERR_PROTO);
		return -EIO;
	}

	if (conn->hdrdgst_en &&
	    cxgbi_skcb_test_flag(skb, SKCBF_RX_HCRC_ERR)) {
		pr_info("conn 0x%p, skb 0x%p, hcrc.\n", conn, skb);
		iscsi_conn_failure(conn, ISCSI_ERR_HDR_DGST);
		return -EIO;
	}

	if (cxgbi_skcb_test_flag(skb, SKCBF_RX_ISCSI_COMPL) &&
	    cxgbi_skcb_test_flag(skb, SKCBF_RX_DATA_DDPD)) {
		/* If completion flag is set and data is directly
		 * placed in to the host memory then update
		 * task->exp_datasn to the datasn in completion
		 * iSCSI hdr as T6 adapter generates completion only
		 * for the last pdu of a sequence.
		 */
		itt_t itt = ((struct iscsi_data *)skb->data)->itt;
		struct iscsi_task *task = iscsi_itt_to_ctask(conn, itt);
		u32 data_sn = be32_to_cpu(((struct iscsi_data *)
							skb->data)->datasn);
		if (task && task->sc) {
			struct iscsi_tcp_task *tcp_task = task->dd_data;

			tcp_task->exp_datasn = data_sn;
		}
	}

	err = read_pdu_skb(conn, skb, 0, 0);
	if (likely(err >= 0)) {
		struct iscsi_hdr *hdr = (struct iscsi_hdr *)skb->data;
		u8 opcode = hdr->opcode & ISCSI_OPCODE_MASK;

		if (unlikely(opcode == ISCSI_OP_LOGOUT_RSP))
			cxgbi_sock_set_flag(csk, CTPF_LOGOUT_RSP_RCVD);
	}

	return err;
}

static int skb_read_pdu_data(struct iscsi_conn *conn, struct sk_buff *lskb,
			     struct sk_buff *skb, unsigned int offset)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	bool offloaded = 0;
	int opcode = tcp_conn->in.hdr->opcode & ISCSI_OPCODE_MASK;

	log_debug(1 << CXGBI_DBG_PDU_RX,
		"conn 0x%p, skb 0x%p, len %u, flag 0x%lx.\n",
		conn, skb, skb->len, cxgbi_skcb_flags(skb));

	if (conn->datadgst_en &&
	    cxgbi_skcb_test_flag(lskb, SKCBF_RX_DCRC_ERR)) {
		pr_info("conn 0x%p, skb 0x%p, dcrc 0x%lx.\n",
			conn, lskb, cxgbi_skcb_flags(lskb));
		iscsi_conn_failure(conn, ISCSI_ERR_DATA_DGST);
		return -EIO;
	}

	if (iscsi_tcp_recv_segment_is_hdr(tcp_conn))
		return 0;

	/* coalesced, add header digest length */
	if (lskb == skb && conn->hdrdgst_en)
		offset += ISCSI_DIGEST_SIZE;

	if (cxgbi_skcb_test_flag(lskb, SKCBF_RX_DATA_DDPD))
		offloaded = 1;

	if (opcode == ISCSI_OP_SCSI_DATA_IN)
		log_debug(1 << CXGBI_DBG_PDU_RX,
			"skb 0x%p, op 0x%x, itt 0x%x, %u %s ddp'ed.\n",
			skb, opcode, ntohl(tcp_conn->in.hdr->itt),
			tcp_conn->in.datalen, offloaded ? "is" : "not");

	return read_pdu_skb(conn, skb, offset, offloaded);
}

static void csk_return_rx_credits(struct cxgbi_sock *csk, int copied)
{
	struct cxgbi_device *cdev = csk->cdev;
	int must_send;
	u32 credits;

	log_debug(1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p,%u,0x%lx,%u, seq %u, wup %u, thre %u, %u.\n",
		csk, csk->state, csk->flags, csk->tid, csk->copied_seq,
		csk->rcv_wup, cdev->rx_credit_thres,
		csk->rcv_win);

	if (!cdev->rx_credit_thres)
		return;

	if (csk->state != CTP_ESTABLISHED)
		return;

	credits = csk->copied_seq - csk->rcv_wup;
	if (unlikely(!credits))
		return;
	must_send = credits + 16384 >= csk->rcv_win;
	if (must_send || credits >= cdev->rx_credit_thres)
		csk->rcv_wup += cdev->csk_send_rx_credits(csk, credits);
}

void cxgbi_conn_pdu_ready(struct cxgbi_sock *csk)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct iscsi_conn *conn = csk->user_data;
	struct sk_buff *skb;
	unsigned int read = 0;
	int err = 0;

	log_debug(1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p, conn 0x%p.\n", csk, conn);

	if (unlikely(!conn || conn->suspend_rx)) {
		log_debug(1 << CXGBI_DBG_PDU_RX,
			"csk 0x%p, conn 0x%p, id %d, suspend_rx %lu!\n",
			csk, conn, conn ? conn->id : 0xFF,
			conn ? conn->suspend_rx : 0xFF);
		return;
	}

	while (!err) {
		skb = skb_peek(&csk->receive_queue);
		if (!skb ||
		    !(cxgbi_skcb_test_flag(skb, SKCBF_RX_STATUS))) {
			if (skb)
				log_debug(1 << CXGBI_DBG_PDU_RX,
					"skb 0x%p, NOT ready 0x%lx.\n",
					skb, cxgbi_skcb_flags(skb));
			break;
		}
		__skb_unlink(skb, &csk->receive_queue);

		read += cxgbi_skcb_rx_pdulen(skb);
		log_debug(1 << CXGBI_DBG_PDU_RX,
			"csk 0x%p, skb 0x%p,%u,f 0x%lx, pdu len %u.\n",
			csk, skb, skb->len, cxgbi_skcb_flags(skb),
			cxgbi_skcb_rx_pdulen(skb));

		if (cxgbi_skcb_test_flag(skb, SKCBF_RX_COALESCED)) {
			err = skb_read_pdu_bhs(csk, conn, skb);
			if (err < 0) {
				pr_err("coalesced bhs, csk 0x%p, skb 0x%p,%u, "
					"f 0x%lx, plen %u.\n",
					csk, skb, skb->len,
					cxgbi_skcb_flags(skb),
					cxgbi_skcb_rx_pdulen(skb));
				goto skb_done;
			}
			err = skb_read_pdu_data(conn, skb, skb,
						err + cdev->skb_rx_extra);
			if (err < 0)
				pr_err("coalesced data, csk 0x%p, skb 0x%p,%u, "
					"f 0x%lx, plen %u.\n",
					csk, skb, skb->len,
					cxgbi_skcb_flags(skb),
					cxgbi_skcb_rx_pdulen(skb));
		} else {
			err = skb_read_pdu_bhs(csk, conn, skb);
			if (err < 0) {
				pr_err("bhs, csk 0x%p, skb 0x%p,%u, "
					"f 0x%lx, plen %u.\n",
					csk, skb, skb->len,
					cxgbi_skcb_flags(skb),
					cxgbi_skcb_rx_pdulen(skb));
				goto skb_done;
			}

			if (cxgbi_skcb_test_flag(skb, SKCBF_RX_DATA)) {
				struct sk_buff *dskb;

				dskb = skb_peek(&csk->receive_queue);
				if (!dskb) {
					pr_err("csk 0x%p, skb 0x%p,%u, f 0x%lx,"
						" plen %u, NO data.\n",
						csk, skb, skb->len,
						cxgbi_skcb_flags(skb),
						cxgbi_skcb_rx_pdulen(skb));
					err = -EIO;
					goto skb_done;
				}
				__skb_unlink(dskb, &csk->receive_queue);

				err = skb_read_pdu_data(conn, skb, dskb, 0);
				if (err < 0)
					pr_err("data, csk 0x%p, skb 0x%p,%u, "
						"f 0x%lx, plen %u, dskb 0x%p,"
						"%u.\n",
						csk, skb, skb->len,
						cxgbi_skcb_flags(skb),
						cxgbi_skcb_rx_pdulen(skb),
						dskb, dskb->len);
				__kfree_skb(dskb);
			} else
				err = skb_read_pdu_data(conn, skb, skb, 0);
		}
skb_done:
		__kfree_skb(skb);

		if (err < 0)
			break;
	}

	log_debug(1 << CXGBI_DBG_PDU_RX, "csk 0x%p, read %u.\n", csk, read);
	if (read) {
		csk->copied_seq += read;
		csk_return_rx_credits(csk, read);
		conn->rxdata_octets += read;
	}

	if (err < 0) {
		pr_info("csk 0x%p, 0x%p, rx failed %d, read %u.\n",
			csk, conn, err, read);
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	}
}
EXPORT_SYMBOL_GPL(cxgbi_conn_pdu_ready);

static int sgl_seek_offset(struct scatterlist *sgl, unsigned int sgcnt,
				unsigned int offset, unsigned int *off,
				struct scatterlist **sgp)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, sgcnt, i) {
		if (offset < sg->length) {
			*off = offset;
			*sgp = sg;
			return 0;
		}
		offset -= sg->length;
	}
	return -EFAULT;
}

static int
sgl_read_to_frags(struct scatterlist *sg, unsigned int sgoffset,
		  unsigned int dlen, struct page_frag *frags,
		  int frag_max, u32 *dlimit)
{
	unsigned int datalen = dlen;
	unsigned int sglen = sg->length - sgoffset;
	struct page *page = sg_page(sg);
	int i;

	i = 0;
	do {
		unsigned int copy;

		if (!sglen) {
			sg = sg_next(sg);
			if (!sg) {
				pr_warn("sg %d NULL, len %u/%u.\n",
					i, datalen, dlen);
				return -EINVAL;
			}
			sgoffset = 0;
			sglen = sg->length;
			page = sg_page(sg);

		}
		copy = min(datalen, sglen);
		if (i && page == frags[i - 1].page &&
		    sgoffset + sg->offset ==
			frags[i - 1].offset + frags[i - 1].size) {
			frags[i - 1].size += copy;
		} else {
			if (i >= frag_max) {
				pr_warn("too many pages %u, dlen %u.\n",
					frag_max, dlen);
				*dlimit = dlen - datalen;
				return -EINVAL;
			}

			frags[i].page = page;
			frags[i].offset = sg->offset + sgoffset;
			frags[i].size = copy;
			i++;
		}
		datalen -= copy;
		sgoffset += copy;
		sglen -= copy;
	} while (datalen);

	return i;
}

static void cxgbi_task_data_sgl_check(struct iscsi_task *task)
{
	struct scsi_cmnd *sc = task->sc;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct scatterlist *sg, *sgl = NULL;
	u32 sgcnt = 0;
	int i;

	tdata->flags = CXGBI_TASK_SGL_CHECKED;
	if (!sc)
		return;

	scmd_get_params(sc, &sgl, &sgcnt, &tdata->dlen, 0);
	if (!sgl || !sgcnt) {
		tdata->flags |= CXGBI_TASK_SGL_COPY;
		return;
	}

	for_each_sg(sgl, sg, sgcnt, i) {
		if (page_count(sg_page(sg)) < 1) {
			tdata->flags |= CXGBI_TASK_SGL_COPY;
			return;
		}
	}
}

static int
cxgbi_task_data_sgl_read(struct iscsi_task *task, u32 offset, u32 count,
			 u32 *dlimit)
{
	struct scsi_cmnd *sc = task->sc;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct scatterlist *sgl = NULL;
	struct scatterlist *sg;
	u32 dlen = 0;
	u32 sgcnt;
	int err;

	if (!sc)
		return 0;

	scmd_get_params(sc, &sgl, &sgcnt, &dlen, 0);
	if (!sgl || !sgcnt)
		return 0;

	err = sgl_seek_offset(sgl, sgcnt, offset, &tdata->sgoffset, &sg);
	if (err < 0) {
		pr_warn("tpdu max, sgl %u, bad offset %u/%u.\n",
			sgcnt, offset, tdata->dlen);
		return err;
	}
	err = sgl_read_to_frags(sg, tdata->sgoffset, count,
				tdata->frags, MAX_SKB_FRAGS, dlimit);
	if (err < 0) {
		log_debug(1 << CXGBI_DBG_ISCSI,
			  "sgl max limit, sgl %u, offset %u, %u/%u, dlimit %u.\n",
			  sgcnt, offset, count, tdata->dlen, *dlimit);
		return err;
	}
	tdata->offset = offset;
	tdata->count = count;
	tdata->nr_frags = err;
	tdata->total_count = count;
	tdata->total_offset = offset;

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
		  "%s: offset %u, count %u,\n"
		  "err %u, total_count %u, total_offset %u\n",
		  __func__, offset, count, err,  tdata->total_count, tdata->total_offset);

	return 0;
}

int cxgbi_conn_alloc_pdu(struct iscsi_task *task, u8 op)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *session = task->conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_device *cdev = cconn->chba->cdev;
	struct cxgbi_sock *csk = cconn->cep ? cconn->cep->csk : NULL;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct scsi_cmnd *sc = task->sc;
	u32 headroom = SKB_TX_ISCSI_PDU_HEADER_MAX;
	u32 max_txdata_len = conn->max_xmit_dlength;
	u32 iso_tx_rsvd = 0, local_iso_info = 0;
	u32 last_tdata_offset, last_tdata_count;
	int err = 0;

	if (!tcp_task) {
		pr_err("task 0x%p, tcp_task 0x%p, tdata 0x%p.\n",
		       task, tcp_task, tdata);
		return -ENOMEM;
	}
	if (!csk) {
		pr_err("task 0x%p, csk gone.\n", task);
		return -EPIPE;
	}

	op &= ISCSI_OPCODE_MASK;

	tcp_task->dd_data = tdata;
	task->hdr = NULL;

	last_tdata_count = tdata->count;
	last_tdata_offset = tdata->offset;

	if ((op == ISCSI_OP_SCSI_DATA_OUT) ||
	    ((op == ISCSI_OP_SCSI_CMD) &&
	     (sc->sc_data_direction == DMA_TO_DEVICE))) {
		u32 remaining_data_tosend, dlimit = 0;
		u32 max_pdu_size, max_num_pdu, num_pdu;
		u32 count;

		/* Preserve conn->max_xmit_dlength because it can get updated to
		 * ISO data size.
		 */
		if (task->state == ISCSI_TASK_PENDING)
			tdata->max_xmit_dlength = conn->max_xmit_dlength;

		if (!tdata->offset)
			cxgbi_task_data_sgl_check(task);

		remaining_data_tosend =
			tdata->dlen - tdata->offset - tdata->count;

recalculate_sgl:
		max_txdata_len = tdata->max_xmit_dlength;
		log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
			  "tdata->dlen %u, remaining to send %u "
			  "conn->max_xmit_dlength %u, "
			  "tdata->max_xmit_dlength %u\n",
			  tdata->dlen, remaining_data_tosend,
			  conn->max_xmit_dlength, tdata->max_xmit_dlength);

		if (cdev->skb_iso_txhdr && !csk->disable_iso &&
		    (remaining_data_tosend > tdata->max_xmit_dlength) &&
		    !(remaining_data_tosend % 4)) {
			u32 max_iso_data;

			if ((op == ISCSI_OP_SCSI_CMD) &&
			    session->initial_r2t_en)
				goto no_iso;

			max_pdu_size = tdata->max_xmit_dlength +
				       ISCSI_PDU_NONPAYLOAD_LEN;
			max_iso_data = rounddown(CXGBI_MAX_ISO_DATA_IN_SKB,
						 csk->advmss);
			max_num_pdu = max_iso_data / max_pdu_size;

			num_pdu = (remaining_data_tosend +
				   tdata->max_xmit_dlength - 1) /
				  tdata->max_xmit_dlength;

			if (num_pdu > max_num_pdu)
				num_pdu = max_num_pdu;

			conn->max_xmit_dlength = tdata->max_xmit_dlength * num_pdu;
			max_txdata_len = conn->max_xmit_dlength;
			iso_tx_rsvd = cdev->skb_iso_txhdr;
			local_iso_info = sizeof(struct cxgbi_iso_info);

			log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
				  "max_pdu_size %u, max_num_pdu %u, "
				  "max_txdata %u, num_pdu %u\n",
				  max_pdu_size, max_num_pdu,
				  max_txdata_len, num_pdu);
		}
no_iso:
		count  = min_t(u32, max_txdata_len, remaining_data_tosend);
		err = cxgbi_task_data_sgl_read(task,
					       tdata->offset + tdata->count,
					       count, &dlimit);
		if (unlikely(err < 0)) {
			log_debug(1 << CXGBI_DBG_ISCSI,
				  "task 0x%p, tcp_task 0x%p, tdata 0x%p, "
				  "sgl err %d, count %u, dlimit %u\n",
				  task, tcp_task, tdata, err, count, dlimit);
			if (dlimit) {
				remaining_data_tosend =
					rounddown(dlimit,
						  tdata->max_xmit_dlength);
				if (!remaining_data_tosend)
					remaining_data_tosend = dlimit;

				dlimit = 0;

				conn->max_xmit_dlength = remaining_data_tosend;
				goto recalculate_sgl;
			}

			pr_err("task 0x%p, tcp_task 0x%p, tdata 0x%p, "
				"sgl err %d\n",
				task, tcp_task, tdata, err);
			goto ret_err;
		}

		if ((tdata->flags & CXGBI_TASK_SGL_COPY) ||
		    (tdata->nr_frags > MAX_SKB_FRAGS))
			headroom += conn->max_xmit_dlength;
	}

	tdata->skb = alloc_skb(local_iso_info + cdev->skb_tx_rsvd +
			       iso_tx_rsvd + headroom, GFP_ATOMIC);
	if (!tdata->skb) {
		tdata->count = last_tdata_count;
		tdata->offset = last_tdata_offset;
		err = -ENOMEM;
		goto ret_err;
	}

	skb_reserve(tdata->skb, local_iso_info + cdev->skb_tx_rsvd +
		    iso_tx_rsvd);

	if (task->sc) {
		task->hdr = (struct iscsi_hdr *)tdata->skb->data;
	} else {
		task->hdr = kzalloc(SKB_TX_ISCSI_PDU_HEADER_MAX, GFP_ATOMIC);
		if (!task->hdr) {
			__kfree_skb(tdata->skb);
			tdata->skb = NULL;
			return -ENOMEM;
		}
	}

	task->hdr_max = SKB_TX_ISCSI_PDU_HEADER_MAX;

	if (iso_tx_rsvd)
		cxgbi_skcb_set_flag(tdata->skb, SKCBF_TX_ISO);

	/* data_out uses scsi_cmd's itt */
	if (op != ISCSI_OP_SCSI_DATA_OUT)
		task_reserve_itt(task, &task->hdr->itt);

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
		  "task 0x%p, op 0x%x, skb 0x%p,%u+%u/%u, itt 0x%x.\n",
		  task, op, tdata->skb, cdev->skb_tx_rsvd, headroom,
		  conn->max_xmit_dlength, be32_to_cpu(task->hdr->itt));

	return 0;

ret_err:
	conn->max_xmit_dlength = tdata->max_xmit_dlength;
	return err;
}
EXPORT_SYMBOL_GPL(cxgbi_conn_alloc_pdu);

static int
cxgbi_prep_iso_info(struct iscsi_task *task, struct sk_buff *skb,
		    u32 count)
{
	struct cxgbi_iso_info *iso_info = (struct cxgbi_iso_info *)skb->head;
	struct iscsi_r2t_info *r2t;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	u32 burst_size = 0, r2t_dlength = 0, dlength;
	u32 max_pdu_len = tdata->max_xmit_dlength;
	u32 segment_offset = 0;
	u32 num_pdu;

	if (unlikely(!cxgbi_skcb_test_flag(skb, SKCBF_TX_ISO)))
		return 0;

	memset(iso_info, 0, sizeof(struct cxgbi_iso_info));

	if (task->hdr->opcode == ISCSI_OP_SCSI_CMD && session->imm_data_en) {
		iso_info->flags |= CXGBI_ISO_INFO_IMM_ENABLE;
		burst_size = count;
	}

	dlength = ntoh24(task->hdr->dlength);
	dlength = min(dlength, max_pdu_len);
	hton24(task->hdr->dlength, dlength);

	num_pdu = (count + max_pdu_len - 1) / max_pdu_len;

	if (iscsi_task_has_unsol_data(task))
		r2t = &task->unsol_r2t;
	else
		r2t = tcp_task->r2t;

	if (r2t) {
		log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
			  "count %u, tdata->count %u, num_pdu %u,"
			  "task->hdr_len %u, r2t->data_length %u, r2t->sent %u\n",
			  count, tdata->count, num_pdu, task->hdr_len,
			  r2t->data_length, r2t->sent);

		r2t_dlength = r2t->data_length - r2t->sent;
		segment_offset = r2t->sent;
		r2t->datasn += num_pdu - 1;
	}

	if (!r2t || !r2t->sent)
		iso_info->flags |= CXGBI_ISO_INFO_FSLICE;

	if (task->hdr->flags & ISCSI_FLAG_CMD_FINAL)
		iso_info->flags |= CXGBI_ISO_INFO_LSLICE;

	task->hdr->flags &= ~ISCSI_FLAG_CMD_FINAL;

	iso_info->op = task->hdr->opcode;
	iso_info->ahs = task->hdr->hlength;
	iso_info->num_pdu = num_pdu;
	iso_info->mpdu = max_pdu_len;
	iso_info->burst_size = (burst_size + r2t_dlength) >> 2;
	iso_info->len = count + task->hdr_len;
	iso_info->segment_offset = segment_offset;

	cxgbi_skcb_tx_iscsi_hdrlen(skb) = task->hdr_len;
	return 0;
}

static inline void tx_skb_setmode(struct sk_buff *skb, int hcrc, int dcrc)
{
	if (hcrc || dcrc) {
		u8 submode = 0;

		if (hcrc)
			submode |= 1;
		if (dcrc)
			submode |= 2;
		cxgbi_skcb_tx_ulp_mode(skb) = (ULP2_MODE_ISCSI << 4) | submode;
	} else
		cxgbi_skcb_tx_ulp_mode(skb) = 0;
}

static struct page *rsvd_page;

int cxgbi_conn_init_pdu(struct iscsi_task *task, unsigned int offset,
			      unsigned int count)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct sk_buff *skb;
	struct scsi_cmnd *sc = task->sc;
	u32 expected_count, expected_offset;
	u32 datalen = count, dlimit = 0;
	u32 i, padlen = iscsi_padding(count);
	struct page *pg;
	int err;

	if (!tcp_task || (tcp_task->dd_data != tdata)) {
		pr_err("task 0x%p,0x%p, tcp_task 0x%p, tdata 0x%p/0x%p.\n",
		       task, task->sc, tcp_task,
		       tcp_task ? tcp_task->dd_data : NULL, tdata);
		return -EINVAL;
	}
	skb = tdata->skb;

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
		  "task 0x%p,0x%p, skb 0x%p, 0x%x,0x%x,0x%x, %u+%u.\n",
		  task, task->sc, skb, (*skb->data) & ISCSI_OPCODE_MASK,
		  be32_to_cpu(task->cmdsn), be32_to_cpu(task->hdr->itt), offset, count);

	skb_put(skb, task->hdr_len);
	tx_skb_setmode(skb, conn->hdrdgst_en, datalen ? conn->datadgst_en : 0);
	if (!count) {
		tdata->count = count;
		tdata->offset = offset;
		tdata->nr_frags = 0;
		tdata->total_offset = 0;
		tdata->total_count = 0;
		if (tdata->max_xmit_dlength)
			conn->max_xmit_dlength = tdata->max_xmit_dlength;
		cxgbi_skcb_clear_flag(skb, SKCBF_TX_ISO);
		return 0;
	}

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
		  "data->total_count %u, tdata->total_offset %u\n",
		  tdata->total_count, tdata->total_offset);

	expected_count = tdata->total_count;
	expected_offset = tdata->total_offset;

	if ((count != expected_count) ||
	    (offset != expected_offset)) {
		err = cxgbi_task_data_sgl_read(task, offset, count, &dlimit);
		if (err < 0) {
			pr_err("task 0x%p,0x%p, tcp_task 0x%p, tdata 0x%p/0x%p "
			       "dlimit %u, sgl err %d.\n", task, task->sc,
			       tcp_task, tcp_task ? tcp_task->dd_data : NULL,
			       tdata, dlimit, err);
			return err;
		}
	}

	/* Restore original value of conn->max_xmit_dlength because
	 * it can get updated to ISO data size.
	 */
	conn->max_xmit_dlength = tdata->max_xmit_dlength;

	if (sc) {
		struct page_frag *frag = tdata->frags;

		if ((tdata->flags & CXGBI_TASK_SGL_COPY) ||
		    (tdata->nr_frags > MAX_SKB_FRAGS) ||
		    (padlen && (tdata->nr_frags ==
					MAX_SKB_FRAGS))) {
			char *dst = skb->data + task->hdr_len;

			/* data fits in the skb's headroom */
			for (i = 0; i < tdata->nr_frags; i++, frag++) {
				char *src = kmap_atomic(frag->page);

				memcpy(dst, src + frag->offset, frag->size);
				dst += frag->size;
				kunmap_atomic(src);
			}

			if (padlen) {
				memset(dst, 0, padlen);
				padlen = 0;
			}
			skb_put(skb, count + padlen);
		} else {
			for (i = 0; i < tdata->nr_frags; i++, frag++) {
				get_page(frag->page);
				skb_fill_page_desc(skb, i, frag->page,
						   frag->offset, frag->size);
			}

			skb->len += count;
			skb->data_len += count;
			skb->truesize += count;
		}
	} else {
		pg = virt_to_head_page(task->data);
		get_page(pg);
		skb_fill_page_desc(skb, 0, pg,
				   task->data - (char *)page_address(pg),
				   count);
		skb->len += count;
		skb->data_len += count;
		skb->truesize += count;
	}

	if (padlen) {
		get_page(rsvd_page);
		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
				   rsvd_page, 0, padlen);

		skb->data_len += padlen;
		skb->truesize += padlen;
		skb->len += padlen;
	}

	if (likely(count > tdata->max_xmit_dlength))
		cxgbi_prep_iso_info(task, skb, count);
	else
		cxgbi_skcb_clear_flag(skb, SKCBF_TX_ISO);

	return 0;
}
EXPORT_SYMBOL_GPL(cxgbi_conn_init_pdu);

static int cxgbi_sock_tx_queue_up(struct cxgbi_sock *csk, struct sk_buff *skb)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgbi_iso_info *iso_cpl;
	u32 frags = skb_shinfo(skb)->nr_frags;
	u32 extra_len, num_pdu, hdr_len;
	u32 iso_tx_rsvd = 0;

	if (csk->state != CTP_ESTABLISHED) {
		log_debug(1 << CXGBI_DBG_PDU_TX,
			  "csk 0x%p,%u,0x%lx,%u, EAGAIN.\n",
			  csk, csk->state, csk->flags, csk->tid);
		return -EPIPE;
	}

	if (csk->err) {
		log_debug(1 << CXGBI_DBG_PDU_TX,
			  "csk 0x%p,%u,0x%lx,%u, EPIPE %d.\n",
			  csk, csk->state, csk->flags, csk->tid, csk->err);
		return -EPIPE;
	}

	if ((cdev->flags & CXGBI_FLAG_DEV_T3) &&
	    before((csk->snd_win + csk->snd_una), csk->write_seq)) {
		log_debug(1 << CXGBI_DBG_PDU_TX,
			  "csk 0x%p,%u,0x%lx,%u, FULL %u-%u >= %u.\n",
			  csk, csk->state, csk->flags, csk->tid, csk->write_seq,
			  csk->snd_una, csk->snd_win);
		return -ENOBUFS;
	}

	if (cxgbi_skcb_test_flag(skb, SKCBF_TX_ISO))
		iso_tx_rsvd = cdev->skb_iso_txhdr;

	if (unlikely(skb_headroom(skb) < (cdev->skb_tx_rsvd + iso_tx_rsvd))) {
		pr_err("csk 0x%p, skb head %u < %u.\n",
		       csk, skb_headroom(skb), cdev->skb_tx_rsvd);
		return -EINVAL;
	}

	if (skb->len != skb->data_len)
		frags++;

	if (frags >= SKB_WR_LIST_SIZE) {
		pr_err("csk 0x%p, frags %u, %u,%u >%lu.\n",
		       csk, skb_shinfo(skb)->nr_frags, skb->len,
		       skb->data_len, SKB_WR_LIST_SIZE);
		return -EINVAL;
	}

	cxgbi_skcb_set_flag(skb, SKCBF_TX_NEED_HDR);
	skb_reset_transport_header(skb);
	cxgbi_sock_skb_entail(csk, skb);

	extra_len = cxgbi_ulp_extra_len(cxgbi_skcb_tx_ulp_mode(skb));

	if (likely(cxgbi_skcb_test_flag(skb, SKCBF_TX_ISO))) {
		iso_cpl = (struct cxgbi_iso_info *)skb->head;
		num_pdu = iso_cpl->num_pdu;
		hdr_len = cxgbi_skcb_tx_iscsi_hdrlen(skb);
		extra_len = (cxgbi_ulp_extra_len(cxgbi_skcb_tx_ulp_mode(skb)) *
			     num_pdu) +	(hdr_len * (num_pdu - 1));
	}

	csk->write_seq += (skb->len + extra_len);

	return 0;
}

static int cxgbi_sock_send_skb(struct cxgbi_sock *csk, struct sk_buff *skb)
{
	struct cxgbi_device *cdev = csk->cdev;
	int len = skb->len;
	int err;

	spin_lock_bh(&csk->lock);
	err = cxgbi_sock_tx_queue_up(csk, skb);
	if (err < 0) {
		spin_unlock_bh(&csk->lock);
		return err;
	}

	if (likely(skb_queue_len(&csk->write_queue)))
		cdev->csk_push_tx_frames(csk, 0);
	spin_unlock_bh(&csk->lock);
	return len;
}

int cxgbi_conn_xmit_pdu(struct iscsi_task *task)
{
	struct iscsi_tcp_conn *tcp_conn = task->conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct cxgbi_task_tag_info *ttinfo = &tdata->ttinfo;
	struct sk_buff *skb;
	struct cxgbi_sock *csk = NULL;
	u32 pdulen = 0;
	u32 datalen;
	int err;

	if (!tcp_task || (tcp_task->dd_data != tdata)) {
		pr_err("task 0x%p,0x%p, tcp_task 0x%p, tdata 0x%p/0x%p.\n",
		       task, task->sc, tcp_task,
		       tcp_task ? tcp_task->dd_data : NULL, tdata);
		return -EINVAL;
	}

	skb = tdata->skb;
	if (!skb) {
		log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
			  "task 0x%p, skb NULL.\n", task);
		return 0;
	}

	if (cconn && cconn->cep)
		csk = cconn->cep->csk;

	if (!csk) {
		log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
			  "task 0x%p, csk gone.\n", task);
		return -EPIPE;
	}

	tdata->skb = NULL;
	datalen = skb->data_len;

	/* write ppod first if using ofldq to write ppod */
	if (ttinfo->flags & CXGBI_PPOD_INFO_FLAG_VALID) {
		struct cxgbi_ppm *ppm = csk->cdev->cdev2ppm(csk->cdev);

		ttinfo->flags &= ~CXGBI_PPOD_INFO_FLAG_VALID;
		if (csk->cdev->csk_ddp_set_map(ppm, csk, ttinfo) < 0)
			pr_err("task 0x%p, ppod writing using ofldq failed.\n",
			       task);
			/* continue. Let fl get the data */
	}

	if (!task->sc)
		memcpy(skb->data, task->hdr, SKB_TX_ISCSI_PDU_HEADER_MAX);

	err = cxgbi_sock_send_skb(csk, skb);
	if (err > 0) {
		pdulen += err;

		log_debug(1 << CXGBI_DBG_PDU_TX, "task 0x%p,0x%p, rv %d.\n",
			  task, task->sc, err);

		if (task->conn->hdrdgst_en)
			pdulen += ISCSI_DIGEST_SIZE;

		if (datalen && task->conn->datadgst_en)
			pdulen += ISCSI_DIGEST_SIZE;

		task->conn->txdata_octets += pdulen;

		if (unlikely(cxgbi_is_iso_config(csk) && cxgbi_is_iso_disabled(csk))) {
			if (time_after(jiffies, csk->prev_iso_ts + HZ)) {
				csk->disable_iso = false;
				csk->prev_iso_ts = 0;
				log_debug(1 << CXGBI_DBG_PDU_TX,
					  "enable iso: csk 0x%p\n", csk);
			}
		}

		return 0;
	}

	if (err == -EAGAIN || err == -ENOBUFS) {
		log_debug(1 << CXGBI_DBG_PDU_TX,
			  "task 0x%p, skb 0x%p, len %u/%u, %d EAGAIN.\n",
			  task, skb, skb->len, skb->data_len, err);
		/* reset skb to send when we are called again */
		tdata->skb = skb;

		if (cxgbi_is_iso_config(csk) && !cxgbi_is_iso_disabled(csk) &&
		    (csk->no_tx_credits++ >= 2)) {
			csk->disable_iso = true;
			csk->prev_iso_ts = jiffies;
			log_debug(1 << CXGBI_DBG_PDU_TX,
				  "disable iso:csk 0x%p, ts:%lu\n",
				  csk, csk->prev_iso_ts);
		}

		return err;
	}

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
		  "itt 0x%x, skb 0x%p, len %u/%u, xmit err %d.\n",
		  task->itt, skb, skb->len, skb->data_len, err);
	__kfree_skb(skb);
	iscsi_conn_printk(KERN_ERR, task->conn, "xmit err %d.\n", err);
	iscsi_conn_failure(task->conn, ISCSI_ERR_XMIT_FAILED);
	return err;
}
EXPORT_SYMBOL_GPL(cxgbi_conn_xmit_pdu);

void cxgbi_cleanup_task(struct iscsi_task *task)
{
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);

	if (!tcp_task || (tcp_task->dd_data != tdata)) {
		pr_info("task 0x%p,0x%p, tcp_task 0x%p, tdata 0x%p/0x%p.\n",
			task, task->sc, tcp_task,
			tcp_task ? tcp_task->dd_data : NULL, tdata);
		return;
	}

	log_debug(1 << CXGBI_DBG_ISCSI,
		"task 0x%p, skb 0x%p, itt 0x%x.\n",
		task, tdata->skb, task->hdr_itt);

	tcp_task->dd_data = NULL;

	if (!task->sc)
		kfree(task->hdr);
	task->hdr = NULL;

	/*  never reached the xmit task callout */
	if (tdata->skb) {
		__kfree_skb(tdata->skb);
		tdata->skb = NULL;
	}

	task_release_itt(task, task->hdr_itt);
	memset(tdata, 0, sizeof(*tdata));

	iscsi_tcp_cleanup_task(task);
}
EXPORT_SYMBOL_GPL(cxgbi_cleanup_task);

void cxgbi_get_conn_stats(struct iscsi_cls_conn *cls_conn,
				struct iscsi_stats *stats)
{
	struct iscsi_conn *conn = cls_conn->dd_data;

	stats->txdata_octets = conn->txdata_octets;
	stats->rxdata_octets = conn->rxdata_octets;
	stats->scsicmd_pdus = conn->scsicmd_pdus_cnt;
	stats->dataout_pdus = conn->dataout_pdus_cnt;
	stats->scsirsp_pdus = conn->scsirsp_pdus_cnt;
	stats->datain_pdus = conn->datain_pdus_cnt;
	stats->r2t_pdus = conn->r2t_pdus_cnt;
	stats->tmfcmd_pdus = conn->tmfcmd_pdus_cnt;
	stats->tmfrsp_pdus = conn->tmfrsp_pdus_cnt;
	stats->digest_err = 0;
	stats->timeout_err = 0;
	stats->custom_length = 1;
	strcpy(stats->custom[0].desc, "eh_abort_cnt");
	stats->custom[0].value = conn->eh_abort_cnt;
}
EXPORT_SYMBOL_GPL(cxgbi_get_conn_stats);

static int cxgbi_conn_max_xmit_dlength(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_device *cdev = cconn->chba->cdev;
	unsigned int headroom = SKB_MAX_HEAD(cdev->skb_tx_rsvd);
	unsigned int max_def = 512 * MAX_SKB_FRAGS;
	unsigned int max = max(max_def, headroom);

	max = min(cconn->chba->cdev->tx_max_size, max);
	if (conn->max_xmit_dlength)
		conn->max_xmit_dlength = min(conn->max_xmit_dlength, max);
	else
		conn->max_xmit_dlength = max;
	cxgbi_align_pdu_size(conn->max_xmit_dlength);

	return 0;
}

static int cxgbi_conn_max_recv_dlength(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	unsigned int max = cconn->chba->cdev->rx_max_size;

	cxgbi_align_pdu_size(max);

	if (conn->max_recv_dlength) {
		if (conn->max_recv_dlength > max) {
			pr_err("MaxRecvDataSegmentLength %u > %u.\n",
				conn->max_recv_dlength, max);
			return -EINVAL;
		}
		conn->max_recv_dlength = min(conn->max_recv_dlength, max);
		cxgbi_align_pdu_size(conn->max_recv_dlength);
	} else
		conn->max_recv_dlength = max;

	return 0;
}

int cxgbi_set_conn_param(struct iscsi_cls_conn *cls_conn,
			enum iscsi_param param, char *buf, int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_sock *csk = cconn->cep->csk;
	int err;

	log_debug(1 << CXGBI_DBG_ISCSI,
		"cls_conn 0x%p, param %d, buf(%d) %s.\n",
		cls_conn, param, buflen, buf);

	switch (param) {
	case ISCSI_PARAM_HDRDGST_EN:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && conn->hdrdgst_en)
			err = csk->cdev->csk_ddp_setup_digest(csk, csk->tid,
							conn->hdrdgst_en,
							conn->datadgst_en);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && conn->datadgst_en)
			err = csk->cdev->csk_ddp_setup_digest(csk, csk->tid,
							conn->hdrdgst_en,
							conn->datadgst_en);
		break;
	case ISCSI_PARAM_MAX_R2T:
		return iscsi_tcp_set_max_r2t(conn, buf);
	case ISCSI_PARAM_MAX_RECV_DLENGTH:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err)
			err = cxgbi_conn_max_recv_dlength(conn);
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err)
			err = cxgbi_conn_max_xmit_dlength(conn);
		break;
	default:
		return iscsi_set_param(cls_conn, param, buf, buflen);
	}
	return err;
}
EXPORT_SYMBOL_GPL(cxgbi_set_conn_param);

int cxgbi_get_ep_param(struct iscsi_endpoint *ep, enum iscsi_param param,
		       char *buf)
{
	struct cxgbi_endpoint *cep = ep->dd_data;
	struct cxgbi_sock *csk;

	log_debug(1 << CXGBI_DBG_ISCSI,
		"cls_conn 0x%p, param %d.\n", ep, param);

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
	case ISCSI_PARAM_CONN_ADDRESS:
		if (!cep)
			return -ENOTCONN;

		csk = cep->csk;
		if (!csk)
			return -ENOTCONN;

		return iscsi_conn_get_addr_param((struct sockaddr_storage *)
						 &csk->daddr, param, buf);
	default:
		break;
	}
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(cxgbi_get_ep_param);

struct iscsi_cls_conn *
cxgbi_create_conn(struct iscsi_cls_session *cls_session, u32 cid)
{
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_conn *conn;
	struct iscsi_tcp_conn *tcp_conn;
	struct cxgbi_conn *cconn;

	cls_conn = iscsi_tcp_conn_setup(cls_session, sizeof(*cconn), cid);
	if (!cls_conn)
		return NULL;

	conn = cls_conn->dd_data;
	tcp_conn = conn->dd_data;
	cconn = tcp_conn->dd_data;
	cconn->iconn = conn;

	log_debug(1 << CXGBI_DBG_ISCSI,
		"cid %u(0x%x), cls 0x%p,0x%p, conn 0x%p,0x%p,0x%p.\n",
		cid, cid, cls_session, cls_conn, conn, tcp_conn, cconn);

	return cls_conn;
}
EXPORT_SYMBOL_GPL(cxgbi_create_conn);

int cxgbi_bind_conn(struct iscsi_cls_session *cls_session,
				struct iscsi_cls_conn *cls_conn,
				u64 transport_eph, int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_ppm *ppm;
	struct iscsi_endpoint *ep;
	struct cxgbi_endpoint *cep;
	struct cxgbi_sock *csk;
	int err;

	ep = iscsi_lookup_endpoint(transport_eph);
	if (!ep)
		return -EINVAL;

	/*  setup ddp pagesize */
	cep = ep->dd_data;
	csk = cep->csk;

	ppm = csk->cdev->cdev2ppm(csk->cdev);
	err = csk->cdev->csk_ddp_setup_pgidx(csk, csk->tid,
					     ppm->tformat.pgsz_idx_dflt);
	if (err < 0)
		goto put_ep;

	err = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (err) {
		err = -EINVAL;
		goto put_ep;
	}

	/*  calculate the tag idx bits needed for this conn based on cmds_max */
	cconn->task_idx_bits = (__ilog2_u32(conn->session->cmds_max - 1)) + 1;

	write_lock_bh(&csk->callback_lock);
	csk->user_data = conn;
	cconn->chba = cep->chba;
	cconn->cep = cep;
	cep->cconn = cconn;
	write_unlock_bh(&csk->callback_lock);

	cxgbi_conn_max_xmit_dlength(conn);
	cxgbi_conn_max_recv_dlength(conn);

	log_debug(1 << CXGBI_DBG_ISCSI,
		"cls 0x%p,0x%p, ep 0x%p, cconn 0x%p, csk 0x%p.\n",
		cls_session, cls_conn, ep, cconn, csk);
	/*  init recv engine */
	iscsi_tcp_hdr_recv_prep(tcp_conn);

put_ep:
	iscsi_put_endpoint(ep);
	return err;
}
EXPORT_SYMBOL_GPL(cxgbi_bind_conn);

struct iscsi_cls_session *cxgbi_create_session(struct iscsi_endpoint *ep,
						u16 cmds_max, u16 qdepth,
						u32 initial_cmdsn)
{
	struct cxgbi_endpoint *cep;
	struct cxgbi_hba *chba;
	struct Scsi_Host *shost;
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;

	if (!ep) {
		pr_err("missing endpoint.\n");
		return NULL;
	}

	cep = ep->dd_data;
	chba = cep->chba;
	shost = chba->shost;

	BUG_ON(chba != iscsi_host_priv(shost));

	cls_session = iscsi_session_setup(chba->cdev->itp, shost,
					cmds_max, 0,
					sizeof(struct iscsi_tcp_task) +
					sizeof(struct cxgbi_task_data),
					initial_cmdsn, ISCSI_MAX_TARGET);
	if (!cls_session)
		return NULL;

	session = cls_session->dd_data;
	if (iscsi_tcp_r2tpool_alloc(session))
		goto remove_session;

	log_debug(1 << CXGBI_DBG_ISCSI,
		"ep 0x%p, cls sess 0x%p.\n", ep, cls_session);
	return cls_session;

remove_session:
	iscsi_session_teardown(cls_session);
	return NULL;
}
EXPORT_SYMBOL_GPL(cxgbi_create_session);

void cxgbi_destroy_session(struct iscsi_cls_session *cls_session)
{
	log_debug(1 << CXGBI_DBG_ISCSI,
		"cls sess 0x%p.\n", cls_session);

	iscsi_tcp_r2tpool_free(cls_session->dd_data);
	iscsi_session_teardown(cls_session);
}
EXPORT_SYMBOL_GPL(cxgbi_destroy_session);

int cxgbi_set_host_param(struct Scsi_Host *shost, enum iscsi_host_param param,
			char *buf, int buflen)
{
	struct cxgbi_hba *chba = iscsi_host_priv(shost);

	if (!chba->ndev) {
		shost_printk(KERN_ERR, shost, "Could not get host param. "
				"netdev for host not set.\n");
		return -ENODEV;
	}

	log_debug(1 << CXGBI_DBG_ISCSI,
		"shost 0x%p, hba 0x%p,%s, param %d, buf(%d) %s.\n",
		shost, chba, chba->ndev->name, param, buflen, buf);

	switch (param) {
	case ISCSI_HOST_PARAM_IPADDRESS:
	{
		__be32 addr = in_aton(buf);
		log_debug(1 << CXGBI_DBG_ISCSI,
			"hba %s, req. ipv4 %pI4.\n", chba->ndev->name, &addr);
		cxgbi_set_iscsi_ipv4(chba, addr);
		return 0;
	}
	case ISCSI_HOST_PARAM_HWADDRESS:
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		return 0;
	default:
		return iscsi_host_set_param(shost, param, buf, buflen);
	}
}
EXPORT_SYMBOL_GPL(cxgbi_set_host_param);

int cxgbi_get_host_param(struct Scsi_Host *shost, enum iscsi_host_param param,
			char *buf)
{
	struct cxgbi_hba *chba = iscsi_host_priv(shost);
	int len = 0;

	if (!chba->ndev) {
		shost_printk(KERN_ERR, shost, "Could not get host param. "
				"netdev for host not set.\n");
		return -ENODEV;
	}

	log_debug(1 << CXGBI_DBG_ISCSI,
		"shost 0x%p, hba 0x%p,%s, param %d.\n",
		shost, chba, chba->ndev->name, param);

	switch (param) {
	case ISCSI_HOST_PARAM_HWADDRESS:
		len = sysfs_format_mac(buf, chba->ndev->dev_addr, 6);
		break;
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		len = sprintf(buf, "%s\n", chba->ndev->name);
		break;
	case ISCSI_HOST_PARAM_IPADDRESS:
	{
		struct cxgbi_sock *csk = find_sock_on_port(chba->cdev,
							   chba->port_id);
		if (csk) {
			len = sprintf(buf, "%pIS",
				      (struct sockaddr *)&csk->saddr);
		}
		log_debug(1 << CXGBI_DBG_ISCSI,
			  "hba %s, addr %s.\n", chba->ndev->name, buf);
		break;
	}
	default:
		return iscsi_host_get_param(shost, param, buf);
	}

	return len;
}
EXPORT_SYMBOL_GPL(cxgbi_get_host_param);

struct iscsi_endpoint *cxgbi_ep_connect(struct Scsi_Host *shost,
					struct sockaddr *dst_addr,
					int non_blocking)
{
	struct iscsi_endpoint *ep;
	struct cxgbi_endpoint *cep;
	struct cxgbi_hba *hba = NULL;
	struct cxgbi_sock *csk;
	int ifindex = 0;
	int err = -EINVAL;

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_SOCK,
		"shost 0x%p, non_blocking %d, dst_addr 0x%p.\n",
		shost, non_blocking, dst_addr);

	if (shost) {
		hba = iscsi_host_priv(shost);
		if (!hba) {
			pr_info("shost 0x%p, priv NULL.\n", shost);
			goto err_out;
		}
	}

check_route:
	if (dst_addr->sa_family == AF_INET) {
		csk = cxgbi_check_route(dst_addr, ifindex);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (dst_addr->sa_family == AF_INET6) {
		csk = cxgbi_check_route6(dst_addr, ifindex);
#endif
	} else {
		pr_info("address family 0x%x NOT supported.\n",
			dst_addr->sa_family);
		err = -EAFNOSUPPORT;
		return (struct iscsi_endpoint *)ERR_PTR(err);
	}

	if (IS_ERR(csk))
		return (struct iscsi_endpoint *)csk;
	cxgbi_sock_get(csk);

	if (!hba)
		hba = csk->cdev->hbas[csk->port_id];
	else if (hba != csk->cdev->hbas[csk->port_id]) {
		if (ifindex != hba->ndev->ifindex) {
			cxgbi_sock_put(csk);
			cxgbi_sock_closed(csk);
			ifindex = hba->ndev->ifindex;
			goto check_route;
		}

		pr_info("Could not connect through requested host %u"
			"hba 0x%p != 0x%p (%u).\n",
			shost->host_no, hba,
			csk->cdev->hbas[csk->port_id], csk->port_id);
		err = -ENOSPC;
		goto release_conn;
	}

	err = sock_get_port(csk);
	if (err)
		goto release_conn;

	cxgbi_sock_set_state(csk, CTP_CONNECTING);
	err = csk->cdev->csk_init_act_open(csk);
	if (err)
		goto release_conn;

	if (cxgbi_sock_is_closing(csk)) {
		err = -ENOSPC;
		pr_info("csk 0x%p is closing.\n", csk);
		goto release_conn;
	}

	ep = iscsi_create_endpoint(sizeof(*cep));
	if (!ep) {
		err = -ENOMEM;
		pr_info("iscsi alloc ep, OOM.\n");
		goto release_conn;
	}

	cep = ep->dd_data;
	cep->csk = csk;
	cep->chba = hba;

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_SOCK,
		"ep 0x%p, cep 0x%p, csk 0x%p, hba 0x%p,%s.\n",
		ep, cep, csk, hba, hba->ndev->name);
	return ep;

release_conn:
	cxgbi_sock_put(csk);
	cxgbi_sock_closed(csk);
err_out:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(cxgbi_ep_connect);

int cxgbi_ep_poll(struct iscsi_endpoint *ep, int timeout_ms)
{
	struct cxgbi_endpoint *cep = ep->dd_data;
	struct cxgbi_sock *csk = cep->csk;

	if (!cxgbi_sock_is_established(csk))
		return 0;
	return 1;
}
EXPORT_SYMBOL_GPL(cxgbi_ep_poll);

void cxgbi_ep_disconnect(struct iscsi_endpoint *ep)
{
	struct cxgbi_endpoint *cep = ep->dd_data;
	struct cxgbi_conn *cconn = cep->cconn;
	struct cxgbi_sock *csk = cep->csk;

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_SOCK,
		"ep 0x%p, cep 0x%p, cconn 0x%p, csk 0x%p,%u,0x%lx.\n",
		ep, cep, cconn, csk, csk->state, csk->flags);

	if (cconn && cconn->iconn) {
		write_lock_bh(&csk->callback_lock);
		cep->csk->user_data = NULL;
		cconn->cep = NULL;
		write_unlock_bh(&csk->callback_lock);
	}
	iscsi_destroy_endpoint(ep);

	if (likely(csk->state >= CTP_ESTABLISHED))
		need_active_close(csk);
	else
		cxgbi_sock_closed(csk);

	cxgbi_sock_put(csk);
}
EXPORT_SYMBOL_GPL(cxgbi_ep_disconnect);

int cxgbi_iscsi_init(struct iscsi_transport *itp,
			struct scsi_transport_template **stt)
{
	*stt = iscsi_register_transport(itp);
	if (*stt == NULL) {
		pr_err("unable to register %s transport 0x%p.\n",
			itp->name, itp);
		return -ENODEV;
	}
	log_debug(1 << CXGBI_DBG_ISCSI,
		"%s, registered iscsi transport 0x%p.\n",
		itp->name, stt);
	return 0;
}
EXPORT_SYMBOL_GPL(cxgbi_iscsi_init);

void cxgbi_iscsi_cleanup(struct iscsi_transport *itp,
			struct scsi_transport_template **stt)
{
	if (*stt) {
		log_debug(1 << CXGBI_DBG_ISCSI,
			"de-register transport 0x%p, %s, stt 0x%p.\n",
			itp, itp->name, *stt);
		*stt = NULL;
		iscsi_unregister_transport(itp);
	}
}
EXPORT_SYMBOL_GPL(cxgbi_iscsi_cleanup);

umode_t cxgbi_attr_is_visible(int param_type, int param)
{
	switch (param_type) {
	case ISCSI_HOST_PARAM:
		switch (param) {
		case ISCSI_HOST_PARAM_NETDEV_NAME:
		case ISCSI_HOST_PARAM_HWADDRESS:
		case ISCSI_HOST_PARAM_IPADDRESS:
		case ISCSI_HOST_PARAM_INITIATOR_NAME:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_PARAM:
		switch (param) {
		case ISCSI_PARAM_MAX_RECV_DLENGTH:
		case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		case ISCSI_PARAM_HDRDGST_EN:
		case ISCSI_PARAM_DATADGST_EN:
		case ISCSI_PARAM_CONN_ADDRESS:
		case ISCSI_PARAM_CONN_PORT:
		case ISCSI_PARAM_EXP_STATSN:
		case ISCSI_PARAM_PERSISTENT_ADDRESS:
		case ISCSI_PARAM_PERSISTENT_PORT:
		case ISCSI_PARAM_PING_TMO:
		case ISCSI_PARAM_RECV_TMO:
		case ISCSI_PARAM_INITIAL_R2T_EN:
		case ISCSI_PARAM_MAX_R2T:
		case ISCSI_PARAM_IMM_DATA_EN:
		case ISCSI_PARAM_FIRST_BURST:
		case ISCSI_PARAM_MAX_BURST:
		case ISCSI_PARAM_PDU_INORDER_EN:
		case ISCSI_PARAM_DATASEQ_INORDER_EN:
		case ISCSI_PARAM_ERL:
		case ISCSI_PARAM_TARGET_NAME:
		case ISCSI_PARAM_TPGT:
		case ISCSI_PARAM_USERNAME:
		case ISCSI_PARAM_PASSWORD:
		case ISCSI_PARAM_USERNAME_IN:
		case ISCSI_PARAM_PASSWORD_IN:
		case ISCSI_PARAM_FAST_ABORT:
		case ISCSI_PARAM_ABORT_TMO:
		case ISCSI_PARAM_LU_RESET_TMO:
		case ISCSI_PARAM_TGT_RESET_TMO:
		case ISCSI_PARAM_IFACE_NAME:
		case ISCSI_PARAM_INITIATOR_NAME:
			return S_IRUGO;
		default:
			return 0;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cxgbi_attr_is_visible);

static int __init libcxgbi_init_module(void)
{
	pr_info("%s", version);

	BUILD_BUG_ON(sizeof_field(struct sk_buff, cb) <
		     sizeof(struct cxgbi_skb_cb));
	rsvd_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!rsvd_page)
		return -ENOMEM;

	return 0;
}

static void __exit libcxgbi_exit_module(void)
{
	cxgbi_device_unregister_all(0xFF);
	put_page(rsvd_page);
	return;
}

module_init(libcxgbi_init_module);
module_exit(libcxgbi_exit_module);
