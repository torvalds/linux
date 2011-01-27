/*
 * libcxgbi.c: Chelsio common library for T3/T4 iSCSI driver.
 *
 * Copyright (c) 2010 Chelsio Communications, Inc.
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
#include <linux/inetdevice.h>	/* ip_dev_find */
#include <net/tcp.h>

static unsigned int dbg_level;

#include "libcxgbi.h"

#define DRV_MODULE_NAME		"libcxgbi"
#define DRV_MODULE_DESC		"Chelsio iSCSI driver library"
#define DRV_MODULE_VERSION	"0.9.0"
#define DRV_MODULE_RELDATE	"Jun. 2010"

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

int cxgbi_device_portmap_create(struct cxgbi_device *cdev, unsigned int base,
				unsigned int max_conn)
{
	struct cxgbi_ports_map *pmap = &cdev->pmap;

	pmap->port_csk = cxgbi_alloc_big_mem(max_conn *
					     sizeof(struct cxgbi_sock *),
					     GFP_KERNEL);
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
	if (cdev->dev_ddp_cleanup)
		cdev->dev_ddp_cleanup(cdev);
	else
		cxgbi_ddp_cleanup(cdev);
	if (cdev->ddp)
		cxgbi_ddp_cleanup(cdev);
	if (cdev->pmap.max_connect)
		cxgbi_free_big_mem(cdev->pmap.port_csk);
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
	cxgbi_device_destroy(cdev);
}
EXPORT_SYMBOL_GPL(cxgbi_device_unregister);

void cxgbi_device_unregister_all(unsigned int flag)
{
	struct cxgbi_device *cdev, *tmp;

	mutex_lock(&cdev_mutex);
	list_for_each_entry_safe(cdev, tmp, &cdev_list, list_head) {
		if ((cdev->flags & flag) == flag) {
			log_debug(1 << CXGBI_DBG_DEV,
				"cdev 0x%p, p# %u,%s.\n",
				cdev, cdev->nports, cdev->nports ?
				 cdev->ports[0]->name : "");
			list_del(&cdev->list_head);
			cxgbi_device_destroy(cdev);
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

static struct cxgbi_device *cxgbi_device_find_by_netdev(struct net_device *ndev,
							int *port)
{
	struct net_device *vdev = NULL;
	struct cxgbi_device *cdev, *tmp;
	int i;

	if (ndev->priv_flags & IFF_802_1Q_VLAN) {
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

int cxgbi_hbas_add(struct cxgbi_device *cdev, unsigned int max_lun,
		unsigned int max_id, struct scsi_host_template *sht,
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
		shost->max_id = max_id;
		shost->max_channel = 0;
		shost->max_cmd_len = 16;

		chba = iscsi_host_priv(shost);
		chba->cdev = cdev;
		chba->ndev = cdev->ports[i];
		chba->shost = shost;

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
static int sock_get_port(struct cxgbi_sock *csk)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgbi_ports_map *pmap = &cdev->pmap;
	unsigned int start;
	int idx;

	if (!pmap->max_connect) {
		pr_err("cdev 0x%p, p#%u %s, NO port map.\n",
			   cdev, csk->port_id, cdev->ports[csk->port_id]->name);
		return -EADDRNOTAVAIL;
	}

	if (csk->saddr.sin_port) {
		pr_err("source port NON-ZERO %u.\n",
			ntohs(csk->saddr.sin_port));
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
			csk->saddr.sin_port =
				htons(pmap->sport_base + idx);
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

	if (csk->saddr.sin_port) {
		int idx = ntohs(csk->saddr.sin_port) - pmap->sport_base;

		csk->saddr.sin_port = 0;
		if (idx < 0 || idx >= pmap->max_connect) {
			pr_err("cdev 0x%p, p#%u %s, port %u OOR.\n",
				cdev, csk->port_id,
				cdev->ports[csk->port_id]->name,
				ntohs(csk->saddr.sin_port));
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
	setup_timer(&csk->retry_timer, NULL, (unsigned long)csk);
	rwlock_init(&csk->callback_lock);
	csk->cdev = cdev;
	csk->flags = 0;
	cxgbi_sock_set_state(csk, CTP_CLOSED);

	log_debug(1 << CXGBI_DBG_SOCK, "cdev 0x%p, new csk 0x%p.\n", cdev, csk);

	return csk;
}

static struct rtable *find_route_ipv4(__be32 saddr, __be32 daddr,
					__be16 sport, __be16 dport, u8 tos)
{
	struct rtable *rt;
	struct flowi fl = {
		.oif = 0,
		.nl_u = {
			.ip4_u = {
				.daddr = daddr,
				.saddr = saddr,
				.tos = tos }
			},
		.proto = IPPROTO_TCP,
		.uli_u = {
			.ports = {
				.sport = sport,
				.dport = dport }
			}
	};

	if (ip_route_output_flow(&init_net, &rt, &fl, NULL, 0))
		return NULL;

	return rt;
}

static struct cxgbi_sock *cxgbi_check_route(struct sockaddr *dst_addr)
{
	struct sockaddr_in *daddr = (struct sockaddr_in *)dst_addr;
	struct dst_entry *dst;
	struct net_device *ndev;
	struct cxgbi_device *cdev;
	struct rtable *rt = NULL;
	struct cxgbi_sock *csk = NULL;
	unsigned int mtu = 0;
	int port = 0xFFFF;
	int err = 0;

	if (daddr->sin_family != AF_INET) {
		pr_info("address family 0x%x NOT supported.\n",
			daddr->sin_family);
		err = -EAFNOSUPPORT;
		goto err_out;
	}

	rt = find_route_ipv4(0, daddr->sin_addr.s_addr, 0, daddr->sin_port, 0);
	if (!rt) {
		pr_info("no route to ipv4 0x%x, port %u.\n",
			daddr->sin_addr.s_addr, daddr->sin_port);
		err = -ENETUNREACH;
		goto err_out;
	}
	dst = &rt->dst;
	ndev = dst->neighbour->dev;

	if (rt->rt_flags & (RTCF_MULTICAST | RTCF_BROADCAST)) {
		pr_info("multi-cast route %pI4, port %u, dev %s.\n",
			&daddr->sin_addr.s_addr, ntohs(daddr->sin_port),
			ndev->name);
		err = -ENETUNREACH;
		goto rel_rt;
	}

	if (ndev->flags & IFF_LOOPBACK) {
		ndev = ip_dev_find(&init_net, daddr->sin_addr.s_addr);
		mtu = ndev->mtu;
		pr_info("rt dev %s, loopback -> %s, mtu %u.\n",
			dst->neighbour->dev->name, ndev->name, mtu);
	}

	cdev = cxgbi_device_find_by_netdev(ndev, &port);
	if (!cdev) {
		pr_info("dst %pI4, %s, NOT cxgbi device.\n",
			&daddr->sin_addr.s_addr, ndev->name);
		err = -ENETUNREACH;
		goto rel_rt;
	}
	log_debug(1 << CXGBI_DBG_SOCK,
		"route to %pI4 :%u, ndev p#%d,%s, cdev 0x%p.\n",
		&daddr->sin_addr.s_addr, ntohs(daddr->sin_port),
			   port, ndev->name, cdev);

	csk = cxgbi_sock_create(cdev);
	if (!csk) {
		err = -ENOMEM;
		goto rel_rt;
	}
	csk->cdev = cdev;
	csk->port_id = port;
	csk->mtu = mtu;
	csk->dst = dst;
	csk->daddr.sin_addr.s_addr = daddr->sin_addr.s_addr;
	csk->daddr.sin_port = daddr->sin_port;
	csk->saddr.sin_addr.s_addr = rt->rt_src;

	return csk;

rel_rt:
	ip_rt_put(rt);
	if (csk)
		cxgbi_sock_closed(csk);
err_out:
	return ERR_PTR(err);
}

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
					ISCSI_ERR_CONN_FAILED);
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
		if (data_lost)
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

	log_debug(1 << CXGBI_DBG_SOCK, "csk 0x%p,%u,0x%lx,%u.\n",
		csk, (csk)->state, (csk)->flags, (csk)->tid);
	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);
	if (csk->state == CTP_ACTIVE_OPEN)
		cxgbi_sock_fail_act_open(csk, -EHOSTUNREACH);
	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
	__kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(cxgbi_sock_act_open_req_arp_failure);

void cxgbi_sock_rcv_abort_rpl(struct cxgbi_sock *csk)
{
	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);
	if (cxgbi_sock_flag(csk, CTPF_ABORT_RPL_PENDING)) {
		if (!cxgbi_sock_flag(csk, CTPF_ABORT_RPL_RCVD))
			cxgbi_sock_set_flag(csk, CTPF_ABORT_RPL_RCVD);
		else {
			cxgbi_sock_clear_flag(csk, CTPF_ABORT_RPL_RCVD);
			cxgbi_sock_clear_flag(csk, CTPF_ABORT_RPL_PENDING);
			if (cxgbi_sock_flag(csk, CTPF_ABORT_REQ_RCVD))
				pr_err("csk 0x%p,%u,0x%lx,%u,ABT_RPL_RSS.\n",
					csk, csk->state, csk->flags, csk->tid);
			cxgbi_sock_closed(csk);
		}
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

static int cxgbi_sock_send_pdus(struct cxgbi_sock *csk, struct sk_buff *skb)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct sk_buff *next;
	int err, copied = 0;

	spin_lock_bh(&csk->lock);

	if (csk->state != CTP_ESTABLISHED) {
		log_debug(1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p,%u,0x%lx,%u, EAGAIN.\n",
			csk, csk->state, csk->flags, csk->tid);
		err = -EAGAIN;
		goto out_err;
	}

	if (csk->err) {
		log_debug(1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p,%u,0x%lx,%u, EPIPE %d.\n",
			csk, csk->state, csk->flags, csk->tid, csk->err);
		err = -EPIPE;
		goto out_err;
	}

	if (csk->write_seq - csk->snd_una >= cdev->snd_win) {
		log_debug(1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p,%u,0x%lx,%u, FULL %u-%u >= %u.\n",
			csk, csk->state, csk->flags, csk->tid, csk->write_seq,
			csk->snd_una, cdev->snd_win);
		err = -ENOBUFS;
		goto out_err;
	}

	while (skb) {
		int frags = skb_shinfo(skb)->nr_frags +
				(skb->len != skb->data_len);

		if (unlikely(skb_headroom(skb) < cdev->skb_tx_rsvd)) {
			pr_err("csk 0x%p, skb head %u < %u.\n",
				csk, skb_headroom(skb), cdev->skb_tx_rsvd);
			err = -EINVAL;
			goto out_err;
		}

		if (frags >= SKB_WR_LIST_SIZE) {
			pr_err("csk 0x%p, frags %d, %u,%u >%u.\n",
				csk, skb_shinfo(skb)->nr_frags, skb->len,
				skb->data_len, (uint)(SKB_WR_LIST_SIZE));
			err = -EINVAL;
			goto out_err;
		}

		next = skb->next;
		skb->next = NULL;
		cxgbi_skcb_set_flag(skb, SKCBF_TX_NEED_HDR);
		cxgbi_sock_skb_entail(csk, skb);
		copied += skb->len;
		csk->write_seq += skb->len +
				cxgbi_ulp_extra_len(cxgbi_skcb_ulp_mode(skb));
		skb = next;
	}
done:
	if (likely(skb_queue_len(&csk->write_queue)))
		cdev->csk_push_tx_frames(csk, 1);
	spin_unlock_bh(&csk->lock);
	return copied;

out_err:
	if (copied == 0 && err == -EPIPE)
		copied = csk->err ? csk->err : -EPIPE;
	else
		copied = err;
	goto done;
}

/*
 * Direct Data Placement -
 * Directly place the iSCSI Data-In or Data-Out PDU's payload into pre-posted
 * final destination host-memory buffers based on the Initiator Task Tag (ITT)
 * in Data-In or Target Task Tag (TTT) in Data-Out PDUs.
 * The host memory address is programmed into h/w in the format of pagepod
 * entries.
 * The location of the pagepod entry is encoded into ddp tag which is used as
 * the base for ITT/TTT.
 */

static unsigned char ddp_page_order[DDP_PGIDX_MAX] = {0, 1, 2, 4};
static unsigned char ddp_page_shift[DDP_PGIDX_MAX] = {12, 13, 14, 16};
static unsigned char page_idx = DDP_PGIDX_MAX;

static unsigned char sw_tag_idx_bits;
static unsigned char sw_tag_age_bits;

/*
 * Direct-Data Placement page size adjustment
 */
static int ddp_adjust_page_table(void)
{
	int i;
	unsigned int base_order, order;

	if (PAGE_SIZE < (1UL << ddp_page_shift[0])) {
		pr_info("PAGE_SIZE 0x%lx too small, min 0x%lx\n",
			PAGE_SIZE, 1UL << ddp_page_shift[0]);
		return -EINVAL;
	}

	base_order = get_order(1UL << ddp_page_shift[0]);
	order = get_order(1UL << PAGE_SHIFT);

	for (i = 0; i < DDP_PGIDX_MAX; i++) {
		/* first is the kernel page size, then just doubling */
		ddp_page_order[i] = order - base_order + i;
		ddp_page_shift[i] = PAGE_SHIFT + i;
	}
	return 0;
}

static int ddp_find_page_index(unsigned long pgsz)
{
	int i;

	for (i = 0; i < DDP_PGIDX_MAX; i++) {
		if (pgsz == (1UL << ddp_page_shift[i]))
			return i;
	}
	pr_info("ddp page size %lu not supported.\n", pgsz);
	return DDP_PGIDX_MAX;
}

static void ddp_setup_host_page_size(void)
{
	if (page_idx == DDP_PGIDX_MAX) {
		page_idx = ddp_find_page_index(PAGE_SIZE);

		if (page_idx == DDP_PGIDX_MAX) {
			pr_info("system PAGE %lu, update hw.\n", PAGE_SIZE);
			if (ddp_adjust_page_table() < 0) {
				pr_info("PAGE %lu, disable ddp.\n", PAGE_SIZE);
				return;
			}
			page_idx = ddp_find_page_index(PAGE_SIZE);
		}
		pr_info("system PAGE %lu, ddp idx %u.\n", PAGE_SIZE, page_idx);
	}
}

void cxgbi_ddp_page_size_factor(int *pgsz_factor)
{
	int i;

	for (i = 0; i < DDP_PGIDX_MAX; i++)
		pgsz_factor[i] = ddp_page_order[i];
}
EXPORT_SYMBOL_GPL(cxgbi_ddp_page_size_factor);

/*
 * DDP setup & teardown
 */

void cxgbi_ddp_ppod_set(struct cxgbi_pagepod *ppod,
			struct cxgbi_pagepod_hdr *hdr,
			struct cxgbi_gather_list *gl, unsigned int gidx)
{
	int i;

	memcpy(ppod, hdr, sizeof(*hdr));
	for (i = 0; i < (PPOD_PAGES_MAX + 1); i++, gidx++) {
		ppod->addr[i] = gidx < gl->nelem ?
				cpu_to_be64(gl->phys_addr[gidx]) : 0ULL;
	}
}
EXPORT_SYMBOL_GPL(cxgbi_ddp_ppod_set);

void cxgbi_ddp_ppod_clear(struct cxgbi_pagepod *ppod)
{
	memset(ppod, 0, sizeof(*ppod));
}
EXPORT_SYMBOL_GPL(cxgbi_ddp_ppod_clear);

static inline int ddp_find_unused_entries(struct cxgbi_ddp_info *ddp,
					unsigned int start, unsigned int max,
					unsigned int count,
					struct cxgbi_gather_list *gl)
{
	unsigned int i, j, k;

	/*  not enough entries */
	if ((max - start) < count) {
		log_debug(1 << CXGBI_DBG_DDP,
			"NOT enough entries %u+%u < %u.\n", start, count, max);
		return -EBUSY;
	}

	max -= count;
	spin_lock(&ddp->map_lock);
	for (i = start; i < max;) {
		for (j = 0, k = i; j < count; j++, k++) {
			if (ddp->gl_map[k])
				break;
		}
		if (j == count) {
			for (j = 0, k = i; j < count; j++, k++)
				ddp->gl_map[k] = gl;
			spin_unlock(&ddp->map_lock);
			return i;
		}
		i += j + 1;
	}
	spin_unlock(&ddp->map_lock);
	log_debug(1 << CXGBI_DBG_DDP,
		"NO suitable entries %u available.\n", count);
	return -EBUSY;
}

static inline void ddp_unmark_entries(struct cxgbi_ddp_info *ddp,
						int start, int count)
{
	spin_lock(&ddp->map_lock);
	memset(&ddp->gl_map[start], 0,
		count * sizeof(struct cxgbi_gather_list *));
	spin_unlock(&ddp->map_lock);
}

static inline void ddp_gl_unmap(struct pci_dev *pdev,
					struct cxgbi_gather_list *gl)
{
	int i;

	for (i = 0; i < gl->nelem; i++)
		dma_unmap_page(&pdev->dev, gl->phys_addr[i], PAGE_SIZE,
				PCI_DMA_FROMDEVICE);
}

static inline int ddp_gl_map(struct pci_dev *pdev,
				    struct cxgbi_gather_list *gl)
{
	int i;

	for (i = 0; i < gl->nelem; i++) {
		gl->phys_addr[i] = dma_map_page(&pdev->dev, gl->pages[i], 0,
						PAGE_SIZE,
						PCI_DMA_FROMDEVICE);
		if (unlikely(dma_mapping_error(&pdev->dev, gl->phys_addr[i]))) {
			log_debug(1 << CXGBI_DBG_DDP,
				"page %d 0x%p, 0x%p dma mapping err.\n",
				i, gl->pages[i], pdev);
			goto unmap;
		}
	}
	return i;
unmap:
	if (i) {
		unsigned int nelem = gl->nelem;

		gl->nelem = i;
		ddp_gl_unmap(pdev, gl);
		gl->nelem = nelem;
	}
	return -EINVAL;
}

static void ddp_release_gl(struct cxgbi_gather_list *gl,
				  struct pci_dev *pdev)
{
	ddp_gl_unmap(pdev, gl);
	kfree(gl);
}

static struct cxgbi_gather_list *ddp_make_gl(unsigned int xferlen,
						    struct scatterlist *sgl,
						    unsigned int sgcnt,
						    struct pci_dev *pdev,
						    gfp_t gfp)
{
	struct cxgbi_gather_list *gl;
	struct scatterlist *sg = sgl;
	struct page *sgpage = sg_page(sg);
	unsigned int sglen = sg->length;
	unsigned int sgoffset = sg->offset;
	unsigned int npages = (xferlen + sgoffset + PAGE_SIZE - 1) >>
				PAGE_SHIFT;
	int i = 1, j = 0;

	if (xferlen < DDP_THRESHOLD) {
		log_debug(1 << CXGBI_DBG_DDP,
			"xfer %u < threshold %u, no ddp.\n",
			xferlen, DDP_THRESHOLD);
		return NULL;
	}

	gl = kzalloc(sizeof(struct cxgbi_gather_list) +
		     npages * (sizeof(dma_addr_t) +
		     sizeof(struct page *)), gfp);
	if (!gl) {
		log_debug(1 << CXGBI_DBG_DDP,
			"xfer %u, %u pages, OOM.\n", xferlen, npages);
		return NULL;
	}

	 log_debug(1 << CXGBI_DBG_DDP,
		"xfer %u, sgl %u, gl max %u.\n", xferlen, sgcnt, npages);

	gl->pages = (struct page **)&gl->phys_addr[npages];
	gl->nelem = npages;
	gl->length = xferlen;
	gl->offset = sgoffset;
	gl->pages[0] = sgpage;

	for (i = 1, sg = sg_next(sgl), j = 0; i < sgcnt;
		i++, sg = sg_next(sg)) {
		struct page *page = sg_page(sg);

		if (sgpage == page && sg->offset == sgoffset + sglen)
			sglen += sg->length;
		else {
			/*  make sure the sgl is fit for ddp:
			 *  each has the same page size, and
			 *  all of the middle pages are used completely
			 */
			if ((j && sgoffset) || ((i != sgcnt - 1) &&
			    ((sglen + sgoffset) & ~PAGE_MASK))) {
				log_debug(1 << CXGBI_DBG_DDP,
					"page %d/%u, %u + %u.\n",
					i, sgcnt, sgoffset, sglen);
				goto error_out;
			}

			j++;
			if (j == gl->nelem || sg->offset) {
				log_debug(1 << CXGBI_DBG_DDP,
					"page %d/%u, offset %u.\n",
					j, gl->nelem, sg->offset);
				goto error_out;
			}
			gl->pages[j] = page;
			sglen = sg->length;
			sgoffset = sg->offset;
			sgpage = page;
		}
	}
	gl->nelem = ++j;

	if (ddp_gl_map(pdev, gl) < 0)
		goto error_out;

	return gl;

error_out:
	kfree(gl);
	return NULL;
}

static void ddp_tag_release(struct cxgbi_hba *chba, u32 tag)
{
	struct cxgbi_device *cdev = chba->cdev;
	struct cxgbi_ddp_info *ddp = cdev->ddp;
	u32 idx;

	idx = (tag >> PPOD_IDX_SHIFT) & ddp->idx_mask;
	if (idx < ddp->nppods) {
		struct cxgbi_gather_list *gl = ddp->gl_map[idx];
		unsigned int npods;

		if (!gl || !gl->nelem) {
			pr_warn("tag 0x%x, idx %u, gl 0x%p, %u.\n",
				tag, idx, gl, gl ? gl->nelem : 0);
			return;
		}
		npods = (gl->nelem + PPOD_PAGES_MAX - 1) >> PPOD_PAGES_SHIFT;
		log_debug(1 << CXGBI_DBG_DDP,
			"tag 0x%x, release idx %u, npods %u.\n",
			tag, idx, npods);
		cdev->csk_ddp_clear(chba, tag, idx, npods);
		ddp_unmark_entries(ddp, idx, npods);
		ddp_release_gl(gl, ddp->pdev);
	} else
		pr_warn("tag 0x%x, idx %u > max %u.\n", tag, idx, ddp->nppods);
}

static int ddp_tag_reserve(struct cxgbi_sock *csk, unsigned int tid,
			   u32 sw_tag, u32 *tagp, struct cxgbi_gather_list *gl,
			   gfp_t gfp)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgbi_ddp_info *ddp = cdev->ddp;
	struct cxgbi_tag_format *tformat = &cdev->tag_format;
	struct cxgbi_pagepod_hdr hdr;
	unsigned int npods;
	int idx = -1;
	int err = -ENOMEM;
	u32 tag;

	npods = (gl->nelem + PPOD_PAGES_MAX - 1) >> PPOD_PAGES_SHIFT;
	if (ddp->idx_last == ddp->nppods)
		idx = ddp_find_unused_entries(ddp, 0, ddp->nppods,
							npods, gl);
	else {
		idx = ddp_find_unused_entries(ddp, ddp->idx_last + 1,
							ddp->nppods, npods,
							gl);
		if (idx < 0 && ddp->idx_last >= npods) {
			idx = ddp_find_unused_entries(ddp, 0,
				min(ddp->idx_last + npods, ddp->nppods),
							npods, gl);
		}
	}
	if (idx < 0) {
		log_debug(1 << CXGBI_DBG_DDP,
			"xferlen %u, gl %u, npods %u NO DDP.\n",
			gl->length, gl->nelem, npods);
		return idx;
	}

	if (cdev->csk_ddp_alloc_gl_skb) {
		err = cdev->csk_ddp_alloc_gl_skb(ddp, idx, npods, gfp);
		if (err < 0)
			goto unmark_entries;
	}

	tag = cxgbi_ddp_tag_base(tformat, sw_tag);
	tag |= idx << PPOD_IDX_SHIFT;

	hdr.rsvd = 0;
	hdr.vld_tid = htonl(PPOD_VALID_FLAG | PPOD_TID(tid));
	hdr.pgsz_tag_clr = htonl(tag & ddp->rsvd_tag_mask);
	hdr.max_offset = htonl(gl->length);
	hdr.page_offset = htonl(gl->offset);

	err = cdev->csk_ddp_set(csk, &hdr, idx, npods, gl);
	if (err < 0) {
		if (cdev->csk_ddp_free_gl_skb)
			cdev->csk_ddp_free_gl_skb(ddp, idx, npods);
		goto unmark_entries;
	}

	ddp->idx_last = idx;
	log_debug(1 << CXGBI_DBG_DDP,
		"xfer %u, gl %u,%u, tid 0x%x, tag 0x%x->0x%x(%u,%u).\n",
		gl->length, gl->nelem, gl->offset, tid, sw_tag, tag, idx,
		npods);
	*tagp = tag;
	return 0;

unmark_entries:
	ddp_unmark_entries(ddp, idx, npods);
	return err;
}

int cxgbi_ddp_reserve(struct cxgbi_sock *csk, unsigned int *tagp,
			unsigned int sw_tag, unsigned int xferlen,
			struct scatterlist *sgl, unsigned int sgcnt, gfp_t gfp)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgbi_tag_format *tformat = &cdev->tag_format;
	struct cxgbi_gather_list *gl;
	int err;

	if (page_idx >= DDP_PGIDX_MAX || !cdev->ddp ||
	    xferlen < DDP_THRESHOLD) {
		log_debug(1 << CXGBI_DBG_DDP,
			"pgidx %u, xfer %u, NO ddp.\n", page_idx, xferlen);
		return -EINVAL;
	}

	if (!cxgbi_sw_tag_usable(tformat, sw_tag)) {
		log_debug(1 << CXGBI_DBG_DDP,
			"sw_tag 0x%x NOT usable.\n", sw_tag);
		return -EINVAL;
	}

	gl = ddp_make_gl(xferlen, sgl, sgcnt, cdev->pdev, gfp);
	if (!gl)
		return -ENOMEM;

	err = ddp_tag_reserve(csk, csk->tid, sw_tag, tagp, gl, gfp);
	if (err < 0)
		ddp_release_gl(gl, cdev->pdev);

	return err;
}

static void ddp_destroy(struct kref *kref)
{
	struct cxgbi_ddp_info *ddp = container_of(kref,
						struct cxgbi_ddp_info,
						refcnt);
	struct cxgbi_device *cdev = ddp->cdev;
	int i = 0;

	pr_info("kref 0, destroy ddp 0x%p, cdev 0x%p.\n", ddp, cdev);

	while (i < ddp->nppods) {
		struct cxgbi_gather_list *gl = ddp->gl_map[i];

		if (gl) {
			int npods = (gl->nelem + PPOD_PAGES_MAX - 1)
					>> PPOD_PAGES_SHIFT;
			pr_info("cdev 0x%p, ddp %d + %d.\n", cdev, i, npods);
			kfree(gl);
			if (cdev->csk_ddp_free_gl_skb)
				cdev->csk_ddp_free_gl_skb(ddp, i, npods);
			i += npods;
		} else
			i++;
	}
	cxgbi_free_big_mem(ddp);
}

int cxgbi_ddp_cleanup(struct cxgbi_device *cdev)
{
	struct cxgbi_ddp_info *ddp = cdev->ddp;

	log_debug(1 << CXGBI_DBG_DDP,
		"cdev 0x%p, release ddp 0x%p.\n", cdev, ddp);
	cdev->ddp = NULL;
	if (ddp)
		return kref_put(&ddp->refcnt, ddp_destroy);
	return 0;
}
EXPORT_SYMBOL_GPL(cxgbi_ddp_cleanup);

int cxgbi_ddp_init(struct cxgbi_device *cdev,
		   unsigned int llimit, unsigned int ulimit,
		   unsigned int max_txsz, unsigned int max_rxsz)
{
	struct cxgbi_ddp_info *ddp;
	unsigned int ppmax, bits;

	ppmax = (ulimit - llimit + 1) >> PPOD_SIZE_SHIFT;
	bits = __ilog2_u32(ppmax) + 1;
	if (bits > PPOD_IDX_MAX_SIZE)
		bits = PPOD_IDX_MAX_SIZE;
	ppmax = (1 << (bits - 1)) - 1;

	ddp = cxgbi_alloc_big_mem(sizeof(struct cxgbi_ddp_info) +
				ppmax * (sizeof(struct cxgbi_gather_list *) +
					 sizeof(struct sk_buff *)),
				GFP_KERNEL);
	if (!ddp) {
		pr_warn("cdev 0x%p, ddp ppmax %u OOM.\n", cdev, ppmax);
		return -ENOMEM;
	}
	ddp->gl_map = (struct cxgbi_gather_list **)(ddp + 1);
	ddp->gl_skb = (struct sk_buff **)(((char *)ddp->gl_map) +
				ppmax * sizeof(struct cxgbi_gather_list *));
	cdev->ddp = ddp;

	spin_lock_init(&ddp->map_lock);
	kref_init(&ddp->refcnt);

	ddp->cdev = cdev;
	ddp->pdev = cdev->pdev;
	ddp->llimit = llimit;
	ddp->ulimit = ulimit;
	ddp->max_txsz = min_t(unsigned int, max_txsz, ULP2_MAX_PKT_SIZE);
	ddp->max_rxsz = min_t(unsigned int, max_rxsz, ULP2_MAX_PKT_SIZE);
	ddp->nppods = ppmax;
	ddp->idx_last = ppmax;
	ddp->idx_bits = bits;
	ddp->idx_mask = (1 << bits) - 1;
	ddp->rsvd_tag_mask = (1 << (bits + PPOD_IDX_SHIFT)) - 1;

	cdev->tag_format.sw_bits = sw_tag_idx_bits + sw_tag_age_bits;
	cdev->tag_format.rsvd_bits = ddp->idx_bits;
	cdev->tag_format.rsvd_shift = PPOD_IDX_SHIFT;
	cdev->tag_format.rsvd_mask = (1 << cdev->tag_format.rsvd_bits) - 1;

	pr_info("%s tag format, sw %u, rsvd %u,%u, mask 0x%x.\n",
		cdev->ports[0]->name, cdev->tag_format.sw_bits,
		cdev->tag_format.rsvd_bits, cdev->tag_format.rsvd_shift,
		cdev->tag_format.rsvd_mask);

	cdev->tx_max_size = min_t(unsigned int, ULP2_MAX_PDU_PAYLOAD,
				ddp->max_txsz - ISCSI_PDU_NONPAYLOAD_LEN);
	cdev->rx_max_size = min_t(unsigned int, ULP2_MAX_PDU_PAYLOAD,
				ddp->max_rxsz - ISCSI_PDU_NONPAYLOAD_LEN);

	log_debug(1 << CXGBI_DBG_DDP,
		"%s max payload size: %u/%u, %u/%u.\n",
		cdev->ports[0]->name, cdev->tx_max_size, ddp->max_txsz,
		cdev->rx_max_size, ddp->max_rxsz);
	return 0;
}
EXPORT_SYMBOL_GPL(cxgbi_ddp_init);

/*
 * APIs interacting with open-iscsi libraries
 */

static unsigned char padding[4];

static void task_release_itt(struct iscsi_task *task, itt_t hdr_itt)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_tcp_conn *tcp_conn = task->conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_hba *chba = cconn->chba;
	struct cxgbi_tag_format *tformat = &chba->cdev->tag_format;
	u32 tag = ntohl((__force u32)hdr_itt);

	log_debug(1 << CXGBI_DBG_DDP,
		   "cdev 0x%p, release tag 0x%x.\n", chba->cdev, tag);
	if (sc &&
	    (scsi_bidi_cmnd(sc) || sc->sc_data_direction == DMA_FROM_DEVICE) &&
	    cxgbi_is_ddp_tag(tformat, tag))
		ddp_tag_release(chba, tag);
}

static int task_reserve_itt(struct iscsi_task *task, itt_t *hdr_itt)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *sess = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_hba *chba = cconn->chba;
	struct cxgbi_tag_format *tformat = &chba->cdev->tag_format;
	u32 sw_tag = (sess->age << cconn->task_idx_bits) | task->itt;
	u32 tag = 0;
	int err = -EINVAL;

	if (sc &&
	    (scsi_bidi_cmnd(sc) || sc->sc_data_direction == DMA_FROM_DEVICE)) {
		err = cxgbi_ddp_reserve(cconn->cep->csk, &tag, sw_tag,
					scsi_in(sc)->length,
					scsi_in(sc)->table.sgl,
					scsi_in(sc)->table.nents,
					GFP_ATOMIC);
		if (err < 0)
			log_debug(1 << CXGBI_DBG_DDP,
				"csk 0x%p, R task 0x%p, %u,%u, no ddp.\n",
				cconn->cep->csk, task, scsi_in(sc)->length,
				scsi_in(sc)->table.nents);
	}

	if (err < 0)
		tag = cxgbi_set_non_ddp_tag(tformat, sw_tag);
	/*  the itt need to sent in big-endian order */
	*hdr_itt = (__force itt_t)htonl(tag);

	log_debug(1 << CXGBI_DBG_DDP,
		"cdev 0x%p, task 0x%p, 0x%x(0x%x,0x%x)->0x%x/0x%x.\n",
		chba->cdev, task, sw_tag, task->itt, sess->age, tag, *hdr_itt);
	return 0;
}

void cxgbi_parse_pdu_itt(struct iscsi_conn *conn, itt_t itt, int *idx, int *age)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_device *cdev = cconn->chba->cdev;
	u32 tag = ntohl((__force u32) itt);
	u32 sw_bits;

	sw_bits = cxgbi_tag_nonrsvd_bits(&cdev->tag_format, tag);
	if (idx)
		*idx = sw_bits & ((1 << cconn->task_idx_bits) - 1);
	if (age)
		*age = (sw_bits >> cconn->task_idx_bits) & ISCSI_AGE_MASK;

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

static int skb_read_pdu_bhs(struct iscsi_conn *conn, struct sk_buff *skb)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

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

	return read_pdu_skb(conn, skb, 0, 0);
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
		"csk 0x%p,%u,0x%lu,%u, seq %u, wup %u, thre %u, %u.\n",
		csk, csk->state, csk->flags, csk->tid, csk->copied_seq,
		csk->rcv_wup, cdev->rx_credit_thres,
		cdev->rcv_win);

	if (csk->state != CTP_ESTABLISHED)
		return;

	credits = csk->copied_seq - csk->rcv_wup;
	if (unlikely(!credits))
		return;
	if (unlikely(cdev->rx_credit_thres == 0))
		return;

	must_send = credits + 16384 >= cdev->rcv_win;
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
			err = skb_read_pdu_bhs(conn, skb);
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
			err = skb_read_pdu_bhs(conn, skb);
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

static int sgl_read_to_frags(struct scatterlist *sg, unsigned int sgoffset,
				unsigned int dlen, skb_frag_t *frags,
				int frag_max)
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
			frags[i - 1].page_offset + frags[i - 1].size) {
			frags[i - 1].size += copy;
		} else {
			if (i >= frag_max) {
				pr_warn("too many pages %u, dlen %u.\n",
					frag_max, dlen);
				return -EINVAL;
			}

			frags[i].page = page;
			frags[i].page_offset = sg->offset + sgoffset;
			frags[i].size = copy;
			i++;
		}
		datalen -= copy;
		sgoffset += copy;
		sglen -= copy;
	} while (datalen);

	return i;
}

int cxgbi_conn_alloc_pdu(struct iscsi_task *task, u8 opcode)
{
	struct iscsi_tcp_conn *tcp_conn = task->conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_device *cdev = cconn->chba->cdev;
	struct iscsi_conn *conn = task->conn;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct scsi_cmnd *sc = task->sc;
	int headroom = SKB_TX_ISCSI_PDU_HEADER_MAX;

	tcp_task->dd_data = tdata;
	task->hdr = NULL;

	if (SKB_MAX_HEAD(cdev->skb_tx_rsvd) > (512 * MAX_SKB_FRAGS) &&
	    (opcode == ISCSI_OP_SCSI_DATA_OUT ||
	     (opcode == ISCSI_OP_SCSI_CMD &&
	      (scsi_bidi_cmnd(sc) || sc->sc_data_direction == DMA_TO_DEVICE))))
		/* data could goes into skb head */
		headroom += min_t(unsigned int,
				SKB_MAX_HEAD(cdev->skb_tx_rsvd),
				conn->max_xmit_dlength);

	tdata->skb = alloc_skb(cdev->skb_tx_rsvd + headroom, GFP_ATOMIC);
	if (!tdata->skb) {
		pr_warn("alloc skb %u+%u, opcode 0x%x failed.\n",
			cdev->skb_tx_rsvd, headroom, opcode);
		return -ENOMEM;
	}

	skb_reserve(tdata->skb, cdev->skb_tx_rsvd);
	task->hdr = (struct iscsi_hdr *)tdata->skb->data;
	task->hdr_max = SKB_TX_ISCSI_PDU_HEADER_MAX; /* BHS + AHS */

	/* data_out uses scsi_cmd's itt */
	if (opcode != ISCSI_OP_SCSI_DATA_OUT)
		task_reserve_itt(task, &task->hdr->itt);

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
		"task 0x%p, op 0x%x, skb 0x%p,%u+%u/%u, itt 0x%x.\n",
		task, opcode, tdata->skb, cdev->skb_tx_rsvd, headroom,
		conn->max_xmit_dlength, ntohl(task->hdr->itt));

	return 0;
}
EXPORT_SYMBOL_GPL(cxgbi_conn_alloc_pdu);

static inline void tx_skb_setmode(struct sk_buff *skb, int hcrc, int dcrc)
{
	u8 submode = 0;

	if (hcrc)
		submode |= 1;
	if (dcrc)
		submode |= 2;
	cxgbi_skcb_ulp_mode(skb) = (ULP2_MODE_ISCSI << 4) | submode;
}

int cxgbi_conn_init_pdu(struct iscsi_task *task, unsigned int offset,
			      unsigned int count)
{
	struct iscsi_conn *conn = task->conn;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct sk_buff *skb = tdata->skb;
	unsigned int datalen = count;
	int i, padlen = iscsi_padding(count);
	struct page *pg;

	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
		"task 0x%p,0x%p, skb 0x%p, 0x%x,0x%x,0x%x, %u+%u.\n",
		task, task->sc, skb, (*skb->data) & ISCSI_OPCODE_MASK,
		ntohl(task->cmdsn), ntohl(task->hdr->itt), offset, count);

	skb_put(skb, task->hdr_len);
	tx_skb_setmode(skb, conn->hdrdgst_en, datalen ? conn->datadgst_en : 0);
	if (!count)
		return 0;

	if (task->sc) {
		struct scsi_data_buffer *sdb = scsi_out(task->sc);
		struct scatterlist *sg = NULL;
		int err;

		tdata->offset = offset;
		tdata->count = count;
		err = sgl_seek_offset(
					sdb->table.sgl, sdb->table.nents,
					tdata->offset, &tdata->sgoffset, &sg);
		if (err < 0) {
			pr_warn("tpdu, sgl %u, bad offset %u/%u.\n",
				sdb->table.nents, tdata->offset, sdb->length);
			return err;
		}
		err = sgl_read_to_frags(sg, tdata->sgoffset, tdata->count,
					tdata->frags, MAX_PDU_FRAGS);
		if (err < 0) {
			pr_warn("tpdu, sgl %u, bad offset %u + %u.\n",
				sdb->table.nents, tdata->offset, tdata->count);
			return err;
		}
		tdata->nr_frags = err;

		if (tdata->nr_frags > MAX_SKB_FRAGS ||
		    (padlen && tdata->nr_frags == MAX_SKB_FRAGS)) {
			char *dst = skb->data + task->hdr_len;
			skb_frag_t *frag = tdata->frags;

			/* data fits in the skb's headroom */
			for (i = 0; i < tdata->nr_frags; i++, frag++) {
				char *src = kmap_atomic(frag->page,
							KM_SOFTIRQ0);

				memcpy(dst, src+frag->page_offset, frag->size);
				dst += frag->size;
				kunmap_atomic(src, KM_SOFTIRQ0);
			}
			if (padlen) {
				memset(dst, 0, padlen);
				padlen = 0;
			}
			skb_put(skb, count + padlen);
		} else {
			/* data fit into frag_list */
			for (i = 0; i < tdata->nr_frags; i++)
				get_page(tdata->frags[i].page);

			memcpy(skb_shinfo(skb)->frags, tdata->frags,
				sizeof(skb_frag_t) * tdata->nr_frags);
			skb_shinfo(skb)->nr_frags = tdata->nr_frags;
			skb->len += count;
			skb->data_len += count;
			skb->truesize += count;
		}

	} else {
		pg = virt_to_page(task->data);

		get_page(pg);
		skb_fill_page_desc(skb, 0, pg, offset_in_page(task->data),
					count);
		skb->len += count;
		skb->data_len += count;
		skb->truesize += count;
	}

	if (padlen) {
		i = skb_shinfo(skb)->nr_frags;
		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
				virt_to_page(padding), offset_in_page(padding),
				padlen);

		skb->data_len += padlen;
		skb->truesize += padlen;
		skb->len += padlen;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cxgbi_conn_init_pdu);

int cxgbi_conn_xmit_pdu(struct iscsi_task *task)
{
	struct iscsi_tcp_conn *tcp_conn = task->conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);
	struct sk_buff *skb = tdata->skb;
	unsigned int datalen;
	int err;

	if (!skb) {
		log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
			"task 0x%p, skb NULL.\n", task);
		return 0;
	}

	datalen = skb->data_len;
	tdata->skb = NULL;
	err = cxgbi_sock_send_pdus(cconn->cep->csk, skb);
	if (err > 0) {
		int pdulen = err;

		log_debug(1 << CXGBI_DBG_PDU_TX,
			"task 0x%p,0x%p, skb 0x%p, len %u/%u, rv %d.\n",
			task, task->sc, skb, skb->len, skb->data_len, err);

		if (task->conn->hdrdgst_en)
			pdulen += ISCSI_DIGEST_SIZE;

		if (datalen && task->conn->datadgst_en)
			pdulen += ISCSI_DIGEST_SIZE;

		task->conn->txdata_octets += pdulen;
		return 0;
	}

	if (err == -EAGAIN || err == -ENOBUFS) {
		log_debug(1 << CXGBI_DBG_PDU_TX,
			"task 0x%p, skb 0x%p, len %u/%u, %d EAGAIN.\n",
			task, skb, skb->len, skb->data_len, err);
		/* reset skb to send when we are called again */
		tdata->skb = skb;
		return err;
	}

	kfree_skb(skb);
	log_debug(1 << CXGBI_DBG_ISCSI | 1 << CXGBI_DBG_PDU_TX,
		"itt 0x%x, skb 0x%p, len %u/%u, xmit err %d.\n",
		task->itt, skb, skb->len, skb->data_len, err);
	iscsi_conn_printk(KERN_ERR, task->conn, "xmit err %d.\n", err);
	iscsi_conn_failure(task->conn, ISCSI_ERR_XMIT_FAILED);
	return err;
}
EXPORT_SYMBOL_GPL(cxgbi_conn_xmit_pdu);

void cxgbi_cleanup_task(struct iscsi_task *task)
{
	struct cxgbi_task_data *tdata = iscsi_task_cxgbi_data(task);

	log_debug(1 << CXGBI_DBG_ISCSI,
		"task 0x%p, skb 0x%p, itt 0x%x.\n",
		task, tdata->skb, task->hdr_itt);

	/*  never reached the xmit task callout */
	if (tdata->skb)
		__kfree_skb(tdata->skb);
	memset(tdata, 0, sizeof(*tdata));

	task_release_itt(task, task->hdr_itt);
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
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgbi_conn *cconn = tcp_conn->dd_data;
	struct cxgbi_sock *csk = cconn->cep->csk;
	int value, err = 0;

	log_debug(1 << CXGBI_DBG_ISCSI,
		"cls_conn 0x%p, param %d, buf(%d) %s.\n",
		cls_conn, param, buflen, buf);

	switch (param) {
	case ISCSI_PARAM_HDRDGST_EN:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && conn->hdrdgst_en)
			err = csk->cdev->csk_ddp_setup_digest(csk, csk->tid,
							conn->hdrdgst_en,
							conn->datadgst_en, 0);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && conn->datadgst_en)
			err = csk->cdev->csk_ddp_setup_digest(csk, csk->tid,
							conn->hdrdgst_en,
							conn->datadgst_en, 0);
		break;
	case ISCSI_PARAM_MAX_R2T:
		sscanf(buf, "%d", &value);
		if (value <= 0 || !is_power_of_2(value))
			return -EINVAL;
		if (session->max_r2t == value)
			break;
		iscsi_tcp_r2tpool_free(session);
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && iscsi_tcp_r2tpool_alloc(session))
			return -ENOMEM;
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

int cxgbi_get_conn_param(struct iscsi_cls_conn *cls_conn,
			enum iscsi_param param, char *buf)
{
	struct iscsi_conn *iconn = cls_conn->dd_data;
	int len;

	log_debug(1 << CXGBI_DBG_ISCSI,
		"cls_conn 0x%p, param %d.\n", cls_conn, param);

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
		spin_lock_bh(&iconn->session->lock);
		len = sprintf(buf, "%hu\n", iconn->portal_port);
		spin_unlock_bh(&iconn->session->lock);
		break;
	case ISCSI_PARAM_CONN_ADDRESS:
		spin_lock_bh(&iconn->session->lock);
		len = sprintf(buf, "%s\n", iconn->portal_address);
		spin_unlock_bh(&iconn->session->lock);
		break;
	default:
		return iscsi_conn_get_param(cls_conn, param, buf);
	}
	return len;
}
EXPORT_SYMBOL_GPL(cxgbi_get_conn_param);

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
	err = csk->cdev->csk_ddp_setup_pgidx(csk, csk->tid, page_idx, 0);
	if (err < 0)
		return err;

	err = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (err)
		return -EINVAL;

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

	spin_lock_bh(&conn->session->lock);
	sprintf(conn->portal_address, "%pI4", &csk->daddr.sin_addr.s_addr);
	conn->portal_port = ntohs(csk->daddr.sin_port);
	spin_unlock_bh(&conn->session->lock);

	log_debug(1 << CXGBI_DBG_ISCSI,
		"cls 0x%p,0x%p, ep 0x%p, cconn 0x%p, csk 0x%p.\n",
		cls_session, cls_conn, ep, cconn, csk);
	/*  init recv engine */
	iscsi_tcp_hdr_recv_prep(tcp_conn);

	return 0;
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
		__be32 addr;

		addr = cxgbi_get_iscsi_ipv4(chba);
		len = sprintf(buf, "%pI4", &addr);
		log_debug(1 << CXGBI_DBG_ISCSI,
			"hba %s, ipv4 %pI4.\n", chba->ndev->name, &addr);
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

	csk = cxgbi_check_route(dst_addr);
	if (IS_ERR(csk))
		return (struct iscsi_endpoint *)csk;
	cxgbi_sock_get(csk);

	if (!hba)
		hba = csk->cdev->hbas[csk->port_id];
	else if (hba != csk->cdev->hbas[csk->port_id]) {
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
		iscsi_suspend_tx(cconn->iconn);
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

static int __init libcxgbi_init_module(void)
{
	sw_tag_idx_bits = (__ilog2_u32(ISCSI_ITT_MASK)) + 1;
	sw_tag_age_bits = (__ilog2_u32(ISCSI_AGE_MASK)) + 1;

	pr_info("tag itt 0x%x, %u bits, age 0x%x, %u bits.\n",
		ISCSI_ITT_MASK, sw_tag_idx_bits,
		ISCSI_AGE_MASK, sw_tag_age_bits);

	ddp_setup_host_page_size();
	return 0;
}

static void __exit libcxgbi_exit_module(void)
{
	cxgbi_device_unregister_all(0xFF);
	return;
}

module_init(libcxgbi_init_module);
module_exit(libcxgbi_exit_module);
