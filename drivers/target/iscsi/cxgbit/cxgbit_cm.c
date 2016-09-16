/*
 * Copyright (c) 2016 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_vlan.h>

#include <net/neighbour.h>
#include <net/netevent.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#include <libcxgb_cm.h>
#include "cxgbit.h"
#include "clip_tbl.h"

static void cxgbit_init_wr_wait(struct cxgbit_wr_wait *wr_waitp)
{
	wr_waitp->ret = 0;
	reinit_completion(&wr_waitp->completion);
}

static void
cxgbit_wake_up(struct cxgbit_wr_wait *wr_waitp, const char *func, u8 ret)
{
	if (ret == CPL_ERR_NONE)
		wr_waitp->ret = 0;
	else
		wr_waitp->ret = -EIO;

	if (wr_waitp->ret)
		pr_err("%s: err:%u", func, ret);

	complete(&wr_waitp->completion);
}

static int
cxgbit_wait_for_reply(struct cxgbit_device *cdev,
		      struct cxgbit_wr_wait *wr_waitp, u32 tid, u32 timeout,
		      const char *func)
{
	int ret;

	if (!test_bit(CDEV_STATE_UP, &cdev->flags)) {
		wr_waitp->ret = -EIO;
		goto out;
	}

	ret = wait_for_completion_timeout(&wr_waitp->completion, timeout * HZ);
	if (!ret) {
		pr_info("%s - Device %s not responding tid %u\n",
			func, pci_name(cdev->lldi.pdev), tid);
		wr_waitp->ret = -ETIMEDOUT;
	}
out:
	if (wr_waitp->ret)
		pr_info("%s: FW reply %d tid %u\n",
			pci_name(cdev->lldi.pdev), wr_waitp->ret, tid);
	return wr_waitp->ret;
}

static int cxgbit_np_hashfn(const struct cxgbit_np *cnp)
{
	return ((unsigned long)cnp >> 10) & (NP_INFO_HASH_SIZE - 1);
}

static struct np_info *
cxgbit_np_hash_add(struct cxgbit_device *cdev, struct cxgbit_np *cnp,
		   unsigned int stid)
{
	struct np_info *p = kzalloc(sizeof(*p), GFP_KERNEL);

	if (p) {
		int bucket = cxgbit_np_hashfn(cnp);

		p->cnp = cnp;
		p->stid = stid;
		spin_lock(&cdev->np_lock);
		p->next = cdev->np_hash_tab[bucket];
		cdev->np_hash_tab[bucket] = p;
		spin_unlock(&cdev->np_lock);
	}

	return p;
}

static int
cxgbit_np_hash_find(struct cxgbit_device *cdev, struct cxgbit_np *cnp)
{
	int stid = -1, bucket = cxgbit_np_hashfn(cnp);
	struct np_info *p;

	spin_lock(&cdev->np_lock);
	for (p = cdev->np_hash_tab[bucket]; p; p = p->next) {
		if (p->cnp == cnp) {
			stid = p->stid;
			break;
		}
	}
	spin_unlock(&cdev->np_lock);

	return stid;
}

static int cxgbit_np_hash_del(struct cxgbit_device *cdev, struct cxgbit_np *cnp)
{
	int stid = -1, bucket = cxgbit_np_hashfn(cnp);
	struct np_info *p, **prev = &cdev->np_hash_tab[bucket];

	spin_lock(&cdev->np_lock);
	for (p = *prev; p; prev = &p->next, p = p->next) {
		if (p->cnp == cnp) {
			stid = p->stid;
			*prev = p->next;
			kfree(p);
			break;
		}
	}
	spin_unlock(&cdev->np_lock);

	return stid;
}

void _cxgbit_free_cnp(struct kref *kref)
{
	struct cxgbit_np *cnp;

	cnp = container_of(kref, struct cxgbit_np, kref);
	kfree(cnp);
}

static int
cxgbit_create_server6(struct cxgbit_device *cdev, unsigned int stid,
		      struct cxgbit_np *cnp)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)
				     &cnp->com.local_addr;
	int addr_type;
	int ret;

	pr_debug("%s: dev = %s; stid = %u; sin6_port = %u\n",
		 __func__, cdev->lldi.ports[0]->name, stid, sin6->sin6_port);

	addr_type = ipv6_addr_type((const struct in6_addr *)
				   &sin6->sin6_addr);
	if (addr_type != IPV6_ADDR_ANY) {
		ret = cxgb4_clip_get(cdev->lldi.ports[0],
				     (const u32 *)&sin6->sin6_addr.s6_addr, 1);
		if (ret) {
			pr_err("Unable to find clip table entry. laddr %pI6. Error:%d.\n",
			       sin6->sin6_addr.s6_addr, ret);
			return -ENOMEM;
		}
	}

	cxgbit_get_cnp(cnp);
	cxgbit_init_wr_wait(&cnp->com.wr_wait);

	ret = cxgb4_create_server6(cdev->lldi.ports[0],
				   stid, &sin6->sin6_addr,
				   sin6->sin6_port,
				   cdev->lldi.rxq_ids[0]);
	if (!ret)
		ret = cxgbit_wait_for_reply(cdev, &cnp->com.wr_wait,
					    0, 10, __func__);
	else if (ret > 0)
		ret = net_xmit_errno(ret);
	else
		cxgbit_put_cnp(cnp);

	if (ret) {
		if (ret != -ETIMEDOUT)
			cxgb4_clip_release(cdev->lldi.ports[0],
				   (const u32 *)&sin6->sin6_addr.s6_addr, 1);

		pr_err("create server6 err %d stid %d laddr %pI6 lport %d\n",
		       ret, stid, sin6->sin6_addr.s6_addr,
		       ntohs(sin6->sin6_port));
	}

	return ret;
}

static int
cxgbit_create_server4(struct cxgbit_device *cdev, unsigned int stid,
		      struct cxgbit_np *cnp)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)
				   &cnp->com.local_addr;
	int ret;

	pr_debug("%s: dev = %s; stid = %u; sin_port = %u\n",
		 __func__, cdev->lldi.ports[0]->name, stid, sin->sin_port);

	cxgbit_get_cnp(cnp);
	cxgbit_init_wr_wait(&cnp->com.wr_wait);

	ret = cxgb4_create_server(cdev->lldi.ports[0],
				  stid, sin->sin_addr.s_addr,
				  sin->sin_port, 0,
				  cdev->lldi.rxq_ids[0]);
	if (!ret)
		ret = cxgbit_wait_for_reply(cdev,
					    &cnp->com.wr_wait,
					    0, 10, __func__);
	else if (ret > 0)
		ret = net_xmit_errno(ret);
	else
		cxgbit_put_cnp(cnp);

	if (ret)
		pr_err("create server failed err %d stid %d laddr %pI4 lport %d\n",
		       ret, stid, &sin->sin_addr, ntohs(sin->sin_port));
	return ret;
}

struct cxgbit_device *cxgbit_find_device(struct net_device *ndev, u8 *port_id)
{
	struct cxgbit_device *cdev;
	u8 i;

	list_for_each_entry(cdev, &cdev_list_head, list) {
		struct cxgb4_lld_info *lldi = &cdev->lldi;

		for (i = 0; i < lldi->nports; i++) {
			if (lldi->ports[i] == ndev) {
				if (port_id)
					*port_id = i;
				return cdev;
			}
		}
	}

	return NULL;
}

static struct net_device *cxgbit_get_real_dev(struct net_device *ndev)
{
	if (ndev->priv_flags & IFF_BONDING) {
		pr_err("Bond devices are not supported. Interface:%s\n",
		       ndev->name);
		return NULL;
	}

	if (is_vlan_dev(ndev))
		return vlan_dev_real_dev(ndev);

	return ndev;
}

static struct net_device *cxgbit_ipv4_netdev(__be32 saddr)
{
	struct net_device *ndev;

	ndev = __ip_dev_find(&init_net, saddr, false);
	if (!ndev)
		return NULL;

	return cxgbit_get_real_dev(ndev);
}

static struct net_device *cxgbit_ipv6_netdev(struct in6_addr *addr6)
{
	struct net_device *ndev = NULL;
	bool found = false;

	if (IS_ENABLED(CONFIG_IPV6)) {
		for_each_netdev_rcu(&init_net, ndev)
			if (ipv6_chk_addr(&init_net, addr6, ndev, 1)) {
				found = true;
				break;
			}
	}
	if (!found)
		return NULL;
	return cxgbit_get_real_dev(ndev);
}

static struct cxgbit_device *cxgbit_find_np_cdev(struct cxgbit_np *cnp)
{
	struct sockaddr_storage *sockaddr = &cnp->com.local_addr;
	int ss_family = sockaddr->ss_family;
	struct net_device *ndev = NULL;
	struct cxgbit_device *cdev = NULL;

	rcu_read_lock();
	if (ss_family == AF_INET) {
		struct sockaddr_in *sin;

		sin = (struct sockaddr_in *)sockaddr;
		ndev = cxgbit_ipv4_netdev(sin->sin_addr.s_addr);
	} else if (ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)sockaddr;
		ndev = cxgbit_ipv6_netdev(&sin6->sin6_addr);
	}
	if (!ndev)
		goto out;

	cdev = cxgbit_find_device(ndev, NULL);
out:
	rcu_read_unlock();
	return cdev;
}

static bool cxgbit_inaddr_any(struct cxgbit_np *cnp)
{
	struct sockaddr_storage *sockaddr = &cnp->com.local_addr;
	int ss_family = sockaddr->ss_family;
	int addr_type;

	if (ss_family == AF_INET) {
		struct sockaddr_in *sin;

		sin = (struct sockaddr_in *)sockaddr;
		if (sin->sin_addr.s_addr == htonl(INADDR_ANY))
			return true;
	} else if (ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)sockaddr;
		addr_type = ipv6_addr_type((const struct in6_addr *)
				&sin6->sin6_addr);
		if (addr_type == IPV6_ADDR_ANY)
			return true;
	}
	return false;
}

static int
__cxgbit_setup_cdev_np(struct cxgbit_device *cdev, struct cxgbit_np *cnp)
{
	int stid, ret;
	int ss_family = cnp->com.local_addr.ss_family;

	if (!test_bit(CDEV_STATE_UP, &cdev->flags))
		return -EINVAL;

	stid = cxgb4_alloc_stid(cdev->lldi.tids, ss_family, cnp);
	if (stid < 0)
		return -EINVAL;

	if (!cxgbit_np_hash_add(cdev, cnp, stid)) {
		cxgb4_free_stid(cdev->lldi.tids, stid, ss_family);
		return -EINVAL;
	}

	if (ss_family == AF_INET)
		ret = cxgbit_create_server4(cdev, stid, cnp);
	else
		ret = cxgbit_create_server6(cdev, stid, cnp);

	if (ret) {
		if (ret != -ETIMEDOUT)
			cxgb4_free_stid(cdev->lldi.tids, stid,
					ss_family);
		cxgbit_np_hash_del(cdev, cnp);
		return ret;
	}
	return ret;
}

static int cxgbit_setup_cdev_np(struct cxgbit_np *cnp)
{
	struct cxgbit_device *cdev;
	int ret = -1;

	mutex_lock(&cdev_list_lock);
	cdev = cxgbit_find_np_cdev(cnp);
	if (!cdev)
		goto out;

	if (cxgbit_np_hash_find(cdev, cnp) >= 0)
		goto out;

	if (__cxgbit_setup_cdev_np(cdev, cnp))
		goto out;

	cnp->com.cdev = cdev;
	ret = 0;
out:
	mutex_unlock(&cdev_list_lock);
	return ret;
}

static int cxgbit_setup_all_np(struct cxgbit_np *cnp)
{
	struct cxgbit_device *cdev;
	int ret;
	u32 count = 0;

	mutex_lock(&cdev_list_lock);
	list_for_each_entry(cdev, &cdev_list_head, list) {
		if (cxgbit_np_hash_find(cdev, cnp) >= 0) {
			mutex_unlock(&cdev_list_lock);
			return -1;
		}
	}

	list_for_each_entry(cdev, &cdev_list_head, list) {
		ret = __cxgbit_setup_cdev_np(cdev, cnp);
		if (ret == -ETIMEDOUT)
			break;
		if (ret != 0)
			continue;
		count++;
	}
	mutex_unlock(&cdev_list_lock);

	return count ? 0 : -1;
}

int cxgbit_setup_np(struct iscsi_np *np, struct sockaddr_storage *ksockaddr)
{
	struct cxgbit_np *cnp;
	int ret;

	if ((ksockaddr->ss_family != AF_INET) &&
	    (ksockaddr->ss_family != AF_INET6))
		return -EINVAL;

	cnp = kzalloc(sizeof(*cnp), GFP_KERNEL);
	if (!cnp)
		return -ENOMEM;

	init_waitqueue_head(&cnp->accept_wait);
	init_completion(&cnp->com.wr_wait.completion);
	init_completion(&cnp->accept_comp);
	INIT_LIST_HEAD(&cnp->np_accept_list);
	spin_lock_init(&cnp->np_accept_lock);
	kref_init(&cnp->kref);
	memcpy(&np->np_sockaddr, ksockaddr,
	       sizeof(struct sockaddr_storage));
	memcpy(&cnp->com.local_addr, &np->np_sockaddr,
	       sizeof(cnp->com.local_addr));

	cnp->np = np;
	cnp->com.cdev = NULL;

	if (cxgbit_inaddr_any(cnp))
		ret = cxgbit_setup_all_np(cnp);
	else
		ret = cxgbit_setup_cdev_np(cnp);

	if (ret) {
		cxgbit_put_cnp(cnp);
		return -EINVAL;
	}

	np->np_context = cnp;
	cnp->com.state = CSK_STATE_LISTEN;
	return 0;
}

static void
cxgbit_set_conn_info(struct iscsi_np *np, struct iscsi_conn *conn,
		     struct cxgbit_sock *csk)
{
	conn->login_family = np->np_sockaddr.ss_family;
	conn->login_sockaddr = csk->com.remote_addr;
	conn->local_sockaddr = csk->com.local_addr;
}

int cxgbit_accept_np(struct iscsi_np *np, struct iscsi_conn *conn)
{
	struct cxgbit_np *cnp = np->np_context;
	struct cxgbit_sock *csk;
	int ret = 0;

accept_wait:
	ret = wait_for_completion_interruptible(&cnp->accept_comp);
	if (ret)
		return -ENODEV;

	spin_lock_bh(&np->np_thread_lock);
	if (np->np_thread_state >= ISCSI_NP_THREAD_RESET) {
		spin_unlock_bh(&np->np_thread_lock);
		/**
		 * No point in stalling here when np_thread
		 * is in state RESET/SHUTDOWN/EXIT - bail
		 **/
		return -ENODEV;
	}
	spin_unlock_bh(&np->np_thread_lock);

	spin_lock_bh(&cnp->np_accept_lock);
	if (list_empty(&cnp->np_accept_list)) {
		spin_unlock_bh(&cnp->np_accept_lock);
		goto accept_wait;
	}

	csk = list_first_entry(&cnp->np_accept_list,
			       struct cxgbit_sock,
			       accept_node);

	list_del_init(&csk->accept_node);
	spin_unlock_bh(&cnp->np_accept_lock);
	conn->context = csk;
	csk->conn = conn;

	cxgbit_set_conn_info(np, conn, csk);
	return 0;
}

static int
__cxgbit_free_cdev_np(struct cxgbit_device *cdev, struct cxgbit_np *cnp)
{
	int stid, ret;
	bool ipv6 = false;

	stid = cxgbit_np_hash_del(cdev, cnp);
	if (stid < 0)
		return -EINVAL;
	if (!test_bit(CDEV_STATE_UP, &cdev->flags))
		return -EINVAL;

	if (cnp->np->np_sockaddr.ss_family == AF_INET6)
		ipv6 = true;

	cxgbit_get_cnp(cnp);
	cxgbit_init_wr_wait(&cnp->com.wr_wait);
	ret = cxgb4_remove_server(cdev->lldi.ports[0], stid,
				  cdev->lldi.rxq_ids[0], ipv6);

	if (ret > 0)
		ret = net_xmit_errno(ret);

	if (ret) {
		cxgbit_put_cnp(cnp);
		return ret;
	}

	ret = cxgbit_wait_for_reply(cdev, &cnp->com.wr_wait,
				    0, 10, __func__);
	if (ret == -ETIMEDOUT)
		return ret;

	if (ipv6 && cnp->com.cdev) {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)&cnp->com.local_addr;
		cxgb4_clip_release(cdev->lldi.ports[0],
				   (const u32 *)&sin6->sin6_addr.s6_addr,
				   1);
	}

	cxgb4_free_stid(cdev->lldi.tids, stid,
			cnp->com.local_addr.ss_family);
	return 0;
}

static void cxgbit_free_all_np(struct cxgbit_np *cnp)
{
	struct cxgbit_device *cdev;
	int ret;

	mutex_lock(&cdev_list_lock);
	list_for_each_entry(cdev, &cdev_list_head, list) {
		ret = __cxgbit_free_cdev_np(cdev, cnp);
		if (ret == -ETIMEDOUT)
			break;
	}
	mutex_unlock(&cdev_list_lock);
}

static void cxgbit_free_cdev_np(struct cxgbit_np *cnp)
{
	struct cxgbit_device *cdev;
	bool found = false;

	mutex_lock(&cdev_list_lock);
	list_for_each_entry(cdev, &cdev_list_head, list) {
		if (cdev == cnp->com.cdev) {
			found = true;
			break;
		}
	}
	if (!found)
		goto out;

	__cxgbit_free_cdev_np(cdev, cnp);
out:
	mutex_unlock(&cdev_list_lock);
}

void cxgbit_free_np(struct iscsi_np *np)
{
	struct cxgbit_np *cnp = np->np_context;

	cnp->com.state = CSK_STATE_DEAD;
	if (cnp->com.cdev)
		cxgbit_free_cdev_np(cnp);
	else
		cxgbit_free_all_np(cnp);

	np->np_context = NULL;
	cxgbit_put_cnp(cnp);
}

static void cxgbit_send_halfclose(struct cxgbit_sock *csk)
{
	struct sk_buff *skb;
	u32 len = roundup(sizeof(struct cpl_close_con_req), 16);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return;

	cxgb_mk_close_con_req(skb, len, csk->tid, csk->txq_idx,
			      NULL, NULL);

	cxgbit_skcb_flags(skb) |= SKCBF_TX_FLAG_COMPL;
	__skb_queue_tail(&csk->txq, skb);
	cxgbit_push_tx_frames(csk);
}

static void cxgbit_arp_failure_discard(void *handle, struct sk_buff *skb)
{
	pr_debug("%s cxgbit_device %p\n", __func__, handle);
	kfree_skb(skb);
}

static void cxgbit_abort_arp_failure(void *handle, struct sk_buff *skb)
{
	struct cxgbit_device *cdev = handle;
	struct cpl_abort_req *req = cplhdr(skb);

	pr_debug("%s cdev %p\n", __func__, cdev);
	req->cmd = CPL_ABORT_NO_RST;
	cxgbit_ofld_send(cdev, skb);
}

static int cxgbit_send_abort_req(struct cxgbit_sock *csk)
{
	struct sk_buff *skb;
	u32 len = roundup(sizeof(struct cpl_abort_req), 16);

	pr_debug("%s: csk %p tid %u; state %d\n",
		 __func__, csk, csk->tid, csk->com.state);

	__skb_queue_purge(&csk->txq);

	if (!test_and_set_bit(CSK_TX_DATA_SENT, &csk->com.flags))
		cxgbit_send_tx_flowc_wr(csk);

	skb = __skb_dequeue(&csk->skbq);
	cxgb_mk_abort_req(skb, len, csk->tid, csk->txq_idx,
			  csk->com.cdev, cxgbit_abort_arp_failure);

	return cxgbit_l2t_send(csk->com.cdev, skb, csk->l2t);
}

void cxgbit_free_conn(struct iscsi_conn *conn)
{
	struct cxgbit_sock *csk = conn->context;
	bool release = false;

	pr_debug("%s: state %d\n",
		 __func__, csk->com.state);

	spin_lock_bh(&csk->lock);
	switch (csk->com.state) {
	case CSK_STATE_ESTABLISHED:
		if (conn->conn_state == TARG_CONN_STATE_IN_LOGOUT) {
			csk->com.state = CSK_STATE_CLOSING;
			cxgbit_send_halfclose(csk);
		} else {
			csk->com.state = CSK_STATE_ABORTING;
			cxgbit_send_abort_req(csk);
		}
		break;
	case CSK_STATE_CLOSING:
		csk->com.state = CSK_STATE_MORIBUND;
		cxgbit_send_halfclose(csk);
		break;
	case CSK_STATE_DEAD:
		release = true;
		break;
	default:
		pr_err("%s: csk %p; state %d\n",
		       __func__, csk, csk->com.state);
	}
	spin_unlock_bh(&csk->lock);

	if (release)
		cxgbit_put_csk(csk);
}

static void cxgbit_set_emss(struct cxgbit_sock *csk, u16 opt)
{
	csk->emss = csk->com.cdev->lldi.mtus[TCPOPT_MSS_G(opt)] -
			((csk->com.remote_addr.ss_family == AF_INET) ?
			sizeof(struct iphdr) : sizeof(struct ipv6hdr)) -
			sizeof(struct tcphdr);
	csk->mss = csk->emss;
	if (TCPOPT_TSTAMP_G(opt))
		csk->emss -= round_up(TCPOLEN_TIMESTAMP, 4);
	if (csk->emss < 128)
		csk->emss = 128;
	if (csk->emss & 7)
		pr_info("Warning: misaligned mtu idx %u mss %u emss=%u\n",
			TCPOPT_MSS_G(opt), csk->mss, csk->emss);
	pr_debug("%s mss_idx %u mss %u emss=%u\n", __func__, TCPOPT_MSS_G(opt),
		 csk->mss, csk->emss);
}

static void cxgbit_free_skb(struct cxgbit_sock *csk)
{
	struct sk_buff *skb;

	__skb_queue_purge(&csk->txq);
	__skb_queue_purge(&csk->rxq);
	__skb_queue_purge(&csk->backlogq);
	__skb_queue_purge(&csk->ppodq);
	__skb_queue_purge(&csk->skbq);

	while ((skb = cxgbit_sock_dequeue_wr(csk)))
		kfree_skb(skb);

	__kfree_skb(csk->lro_hskb);
}

void _cxgbit_free_csk(struct kref *kref)
{
	struct cxgbit_sock *csk;
	struct cxgbit_device *cdev;

	csk = container_of(kref, struct cxgbit_sock, kref);

	pr_debug("%s csk %p state %d\n", __func__, csk, csk->com.state);

	if (csk->com.local_addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)
					     &csk->com.local_addr;
		cxgb4_clip_release(csk->com.cdev->lldi.ports[0],
				   (const u32 *)
				   &sin6->sin6_addr.s6_addr, 1);
	}

	cxgb4_remove_tid(csk->com.cdev->lldi.tids, 0, csk->tid);
	dst_release(csk->dst);
	cxgb4_l2t_release(csk->l2t);

	cdev = csk->com.cdev;
	spin_lock_bh(&cdev->cskq.lock);
	list_del(&csk->list);
	spin_unlock_bh(&cdev->cskq.lock);

	cxgbit_free_skb(csk);
	cxgbit_put_cdev(cdev);

	kfree(csk);
}

static void cxgbit_set_tcp_window(struct cxgbit_sock *csk, struct port_info *pi)
{
	unsigned int linkspeed;
	u8 scale;

	linkspeed = pi->link_cfg.speed;
	scale = linkspeed / SPEED_10000;

#define CXGBIT_10G_RCV_WIN (256 * 1024)
	csk->rcv_win = CXGBIT_10G_RCV_WIN;
	if (scale)
		csk->rcv_win *= scale;

#define CXGBIT_10G_SND_WIN (256 * 1024)
	csk->snd_win = CXGBIT_10G_SND_WIN;
	if (scale)
		csk->snd_win *= scale;

	pr_debug("%s snd_win %d rcv_win %d\n",
		 __func__, csk->snd_win, csk->rcv_win);
}

#ifdef CONFIG_CHELSIO_T4_DCB
static u8 cxgbit_get_iscsi_dcb_state(struct net_device *ndev)
{
	return ndev->dcbnl_ops->getstate(ndev);
}

static int cxgbit_select_priority(int pri_mask)
{
	if (!pri_mask)
		return 0;

	return (ffs(pri_mask) - 1);
}

static u8 cxgbit_get_iscsi_dcb_priority(struct net_device *ndev, u16 local_port)
{
	int ret;
	u8 caps;

	struct dcb_app iscsi_dcb_app = {
		.protocol = local_port
	};

	ret = (int)ndev->dcbnl_ops->getcap(ndev, DCB_CAP_ATTR_DCBX, &caps);

	if (ret)
		return 0;

	if (caps & DCB_CAP_DCBX_VER_IEEE) {
		iscsi_dcb_app.selector = IEEE_8021QAZ_APP_SEL_ANY;

		ret = dcb_ieee_getapp_mask(ndev, &iscsi_dcb_app);

	} else if (caps & DCB_CAP_DCBX_VER_CEE) {
		iscsi_dcb_app.selector = DCB_APP_IDTYPE_PORTNUM;

		ret = dcb_getapp(ndev, &iscsi_dcb_app);
	}

	pr_info("iSCSI priority is set to %u\n", cxgbit_select_priority(ret));

	return cxgbit_select_priority(ret);
}
#endif

static int
cxgbit_offload_init(struct cxgbit_sock *csk, int iptype, __u8 *peer_ip,
		    u16 local_port, struct dst_entry *dst,
		    struct cxgbit_device *cdev)
{
	struct neighbour *n;
	int ret, step;
	struct net_device *ndev;
	u16 rxq_idx, port_id;
#ifdef CONFIG_CHELSIO_T4_DCB
	u8 priority = 0;
#endif

	n = dst_neigh_lookup(dst, peer_ip);
	if (!n)
		return -ENODEV;

	rcu_read_lock();
	ret = -ENOMEM;
	if (n->dev->flags & IFF_LOOPBACK) {
		if (iptype == 4)
			ndev = cxgbit_ipv4_netdev(*(__be32 *)peer_ip);
		else if (IS_ENABLED(CONFIG_IPV6))
			ndev = cxgbit_ipv6_netdev((struct in6_addr *)peer_ip);
		else
			ndev = NULL;

		if (!ndev) {
			ret = -ENODEV;
			goto out;
		}

		csk->l2t = cxgb4_l2t_get(cdev->lldi.l2t,
					 n, ndev, 0);
		if (!csk->l2t)
			goto out;
		csk->mtu = ndev->mtu;
		csk->tx_chan = cxgb4_port_chan(ndev);
		csk->smac_idx = (cxgb4_port_viid(ndev) & 0x7F) << 1;
		step = cdev->lldi.ntxq /
			cdev->lldi.nchan;
		csk->txq_idx = cxgb4_port_idx(ndev) * step;
		step = cdev->lldi.nrxq /
			cdev->lldi.nchan;
		csk->ctrlq_idx = cxgb4_port_idx(ndev);
		csk->rss_qid = cdev->lldi.rxq_ids[
				cxgb4_port_idx(ndev) * step];
		csk->port_id = cxgb4_port_idx(ndev);
		cxgbit_set_tcp_window(csk,
				      (struct port_info *)netdev_priv(ndev));
	} else {
		ndev = cxgbit_get_real_dev(n->dev);
		if (!ndev) {
			ret = -ENODEV;
			goto out;
		}

#ifdef CONFIG_CHELSIO_T4_DCB
		if (cxgbit_get_iscsi_dcb_state(ndev))
			priority = cxgbit_get_iscsi_dcb_priority(ndev,
								 local_port);

		csk->dcb_priority = priority;

		csk->l2t = cxgb4_l2t_get(cdev->lldi.l2t, n, ndev, priority);
#else
		csk->l2t = cxgb4_l2t_get(cdev->lldi.l2t, n, ndev, 0);
#endif
		if (!csk->l2t)
			goto out;
		port_id = cxgb4_port_idx(ndev);
		csk->mtu = dst_mtu(dst);
		csk->tx_chan = cxgb4_port_chan(ndev);
		csk->smac_idx = (cxgb4_port_viid(ndev) & 0x7F) << 1;
		step = cdev->lldi.ntxq /
			cdev->lldi.nports;
		csk->txq_idx = (port_id * step) +
				(cdev->selectq[port_id][0]++ % step);
		csk->ctrlq_idx = cxgb4_port_idx(ndev);
		step = cdev->lldi.nrxq /
			cdev->lldi.nports;
		rxq_idx = (port_id * step) +
				(cdev->selectq[port_id][1]++ % step);
		csk->rss_qid = cdev->lldi.rxq_ids[rxq_idx];
		csk->port_id = port_id;
		cxgbit_set_tcp_window(csk,
				      (struct port_info *)netdev_priv(ndev));
	}
	ret = 0;
out:
	rcu_read_unlock();
	neigh_release(n);
	return ret;
}

int cxgbit_ofld_send(struct cxgbit_device *cdev, struct sk_buff *skb)
{
	int ret = 0;

	if (!test_bit(CDEV_STATE_UP, &cdev->flags)) {
		kfree_skb(skb);
		pr_err("%s - device not up - dropping\n", __func__);
		return -EIO;
	}

	ret = cxgb4_ofld_send(cdev->lldi.ports[0], skb);
	if (ret < 0)
		kfree_skb(skb);
	return ret < 0 ? ret : 0;
}

static void cxgbit_release_tid(struct cxgbit_device *cdev, u32 tid)
{
	u32 len = roundup(sizeof(struct cpl_tid_release), 16);
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return;

	cxgb_mk_tid_release(skb, len, tid, 0);
	cxgbit_ofld_send(cdev, skb);
}

int
cxgbit_l2t_send(struct cxgbit_device *cdev, struct sk_buff *skb,
		struct l2t_entry *l2e)
{
	int ret = 0;

	if (!test_bit(CDEV_STATE_UP, &cdev->flags)) {
		kfree_skb(skb);
		pr_err("%s - device not up - dropping\n", __func__);
		return -EIO;
	}

	ret = cxgb4_l2t_send(cdev->lldi.ports[0], skb, l2e);
	if (ret < 0)
		kfree_skb(skb);
	return ret < 0 ? ret : 0;
}

static void cxgbit_send_rx_credits(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	if (csk->com.state != CSK_STATE_ESTABLISHED) {
		__kfree_skb(skb);
		return;
	}

	cxgbit_ofld_send(csk->com.cdev, skb);
}

/*
 * CPL connection rx data ack: host ->
 * Send RX credits through an RX_DATA_ACK CPL message.
 * Returns the number of credits sent.
 */
int cxgbit_rx_data_ack(struct cxgbit_sock *csk)
{
	struct sk_buff *skb;
	u32 len = roundup(sizeof(struct cpl_rx_data_ack), 16);
	u32 credit_dack;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return -1;

	credit_dack = RX_DACK_CHANGE_F | RX_DACK_MODE_V(1) |
		      RX_CREDITS_V(csk->rx_credits);

	cxgb_mk_rx_data_ack(skb, len, csk->tid, csk->ctrlq_idx,
			    credit_dack);

	csk->rx_credits = 0;

	spin_lock_bh(&csk->lock);
	if (csk->lock_owner) {
		cxgbit_skcb_rx_backlog_fn(skb) = cxgbit_send_rx_credits;
		__skb_queue_tail(&csk->backlogq, skb);
		spin_unlock_bh(&csk->lock);
		return 0;
	}

	cxgbit_send_rx_credits(csk, skb);
	spin_unlock_bh(&csk->lock);

	return 0;
}

#define FLOWC_WR_NPARAMS_MIN    9
#define FLOWC_WR_NPARAMS_MAX	11
static int cxgbit_alloc_csk_skb(struct cxgbit_sock *csk)
{
	struct sk_buff *skb;
	u32 len, flowclen;
	u8 i;

	flowclen = offsetof(struct fw_flowc_wr,
			    mnemval[FLOWC_WR_NPARAMS_MAX]);

	len = max_t(u32, sizeof(struct cpl_abort_req),
		    sizeof(struct cpl_abort_rpl));

	len = max(len, flowclen);
	len = roundup(len, 16);

	for (i = 0; i < 3; i++) {
		skb = alloc_skb(len, GFP_ATOMIC);
		if (!skb)
			goto out;
		__skb_queue_tail(&csk->skbq, skb);
	}

	skb = alloc_skb(LRO_SKB_MIN_HEADROOM, GFP_ATOMIC);
	if (!skb)
		goto out;

	memset(skb->data, 0, LRO_SKB_MIN_HEADROOM);
	csk->lro_hskb = skb;

	return 0;
out:
	__skb_queue_purge(&csk->skbq);
	return -ENOMEM;
}

static void
cxgbit_pass_accept_rpl(struct cxgbit_sock *csk, struct cpl_pass_accept_req *req)
{
	struct sk_buff *skb;
	const struct tcphdr *tcph;
	struct cpl_t5_pass_accept_rpl *rpl5;
	unsigned int len = roundup(sizeof(*rpl5), 16);
	unsigned int mtu_idx;
	u64 opt0;
	u32 opt2, hlen;
	u32 wscale;
	u32 win;

	pr_debug("%s csk %p tid %u\n", __func__, csk, csk->tid);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		cxgbit_put_csk(csk);
		return;
	}

	rpl5 = (struct cpl_t5_pass_accept_rpl *)__skb_put(skb, len);
	memset(rpl5, 0, len);

	INIT_TP_WR(rpl5, csk->tid);
	OPCODE_TID(rpl5) = cpu_to_be32(MK_OPCODE_TID(CPL_PASS_ACCEPT_RPL,
						     csk->tid));
	cxgb_best_mtu(csk->com.cdev->lldi.mtus, csk->mtu, &mtu_idx,
		      req->tcpopt.tstamp,
		      (csk->com.remote_addr.ss_family == AF_INET) ? 0 : 1);
	wscale = cxgb_compute_wscale(csk->rcv_win);
	/*
	 * Specify the largest window that will fit in opt0. The
	 * remainder will be specified in the rx_data_ack.
	 */
	win = csk->rcv_win >> 10;
	if (win > RCV_BUFSIZ_M)
		win = RCV_BUFSIZ_M;
	opt0 =  TCAM_BYPASS_F |
		WND_SCALE_V(wscale) |
		MSS_IDX_V(mtu_idx) |
		L2T_IDX_V(csk->l2t->idx) |
		TX_CHAN_V(csk->tx_chan) |
		SMAC_SEL_V(csk->smac_idx) |
		DSCP_V(csk->tos >> 2) |
		ULP_MODE_V(ULP_MODE_ISCSI) |
		RCV_BUFSIZ_V(win);

	opt2 = RX_CHANNEL_V(0) |
		RSS_QUEUE_VALID_F | RSS_QUEUE_V(csk->rss_qid);

	if (req->tcpopt.tstamp)
		opt2 |= TSTAMPS_EN_F;
	if (req->tcpopt.sack)
		opt2 |= SACK_EN_F;
	if (wscale)
		opt2 |= WND_SCALE_EN_F;

	hlen = ntohl(req->hdr_len);
	tcph = (const void *)(req + 1) + ETH_HDR_LEN_G(hlen) +
		IP_HDR_LEN_G(hlen);

	if (tcph->ece && tcph->cwr)
		opt2 |= CCTRL_ECN_V(1);

	opt2 |= RX_COALESCE_V(3);
	opt2 |= CONG_CNTRL_V(CONG_ALG_NEWRENO);

	opt2 |= T5_ISS_F;
	rpl5->iss = cpu_to_be32((prandom_u32() & ~7UL) - 1);

	opt2 |= T5_OPT_2_VALID_F;

	rpl5->opt0 = cpu_to_be64(opt0);
	rpl5->opt2 = cpu_to_be32(opt2);
	set_wr_txq(skb, CPL_PRIORITY_SETUP, csk->ctrlq_idx);
	t4_set_arp_err_handler(skb, NULL, cxgbit_arp_failure_discard);
	cxgbit_l2t_send(csk->com.cdev, skb, csk->l2t);
}

static void
cxgbit_pass_accept_req(struct cxgbit_device *cdev, struct sk_buff *skb)
{
	struct cxgbit_sock *csk = NULL;
	struct cxgbit_np *cnp;
	struct cpl_pass_accept_req *req = cplhdr(skb);
	unsigned int stid = PASS_OPEN_TID_G(ntohl(req->tos_stid));
	struct tid_info *t = cdev->lldi.tids;
	unsigned int tid = GET_TID(req);
	u16 peer_mss = ntohs(req->tcpopt.mss);
	unsigned short hdrs;

	struct dst_entry *dst;
	__u8 local_ip[16], peer_ip[16];
	__be16 local_port, peer_port;
	int ret;
	int iptype;

	pr_debug("%s: cdev = %p; stid = %u; tid = %u\n",
		 __func__, cdev, stid, tid);

	cnp = lookup_stid(t, stid);
	if (!cnp) {
		pr_err("%s connect request on invalid stid %d\n",
		       __func__, stid);
		goto rel_skb;
	}

	if (cnp->com.state != CSK_STATE_LISTEN) {
		pr_err("%s - listening parent not in CSK_STATE_LISTEN\n",
		       __func__);
		goto reject;
	}

	csk = lookup_tid(t, tid);
	if (csk) {
		pr_err("%s csk not null tid %u\n",
		       __func__, tid);
		goto rel_skb;
	}

	cxgb_get_4tuple(req, cdev->lldi.adapter_type, &iptype, local_ip,
			peer_ip, &local_port, &peer_port);

	/* Find output route */
	if (iptype == 4)  {
		pr_debug("%s parent sock %p tid %u laddr %pI4 raddr %pI4 "
			 "lport %d rport %d peer_mss %d\n"
			 , __func__, cnp, tid,
			 local_ip, peer_ip, ntohs(local_port),
			 ntohs(peer_port), peer_mss);
		dst = cxgb_find_route(&cdev->lldi, cxgbit_get_real_dev,
				      *(__be32 *)local_ip,
				      *(__be32 *)peer_ip,
				      local_port, peer_port,
				      PASS_OPEN_TOS_G(ntohl(req->tos_stid)));
	} else {
		pr_debug("%s parent sock %p tid %u laddr %pI6 raddr %pI6 "
			 "lport %d rport %d peer_mss %d\n"
			 , __func__, cnp, tid,
			 local_ip, peer_ip, ntohs(local_port),
			 ntohs(peer_port), peer_mss);
		dst = cxgb_find_route6(&cdev->lldi, cxgbit_get_real_dev,
				       local_ip, peer_ip,
				       local_port, peer_port,
				       PASS_OPEN_TOS_G(ntohl(req->tos_stid)),
				       ((struct sockaddr_in6 *)
					&cnp->com.local_addr)->sin6_scope_id);
	}
	if (!dst) {
		pr_err("%s - failed to find dst entry!\n",
		       __func__);
		goto reject;
	}

	csk = kzalloc(sizeof(*csk), GFP_ATOMIC);
	if (!csk) {
		dst_release(dst);
		goto rel_skb;
	}

	ret = cxgbit_offload_init(csk, iptype, peer_ip, ntohs(local_port),
				  dst, cdev);
	if (ret) {
		pr_err("%s - failed to allocate l2t entry!\n",
		       __func__);
		dst_release(dst);
		kfree(csk);
		goto reject;
	}

	kref_init(&csk->kref);
	init_completion(&csk->com.wr_wait.completion);

	INIT_LIST_HEAD(&csk->accept_node);

	hdrs = (iptype == 4 ? sizeof(struct iphdr) : sizeof(struct ipv6hdr)) +
		sizeof(struct tcphdr) +	(req->tcpopt.tstamp ? 12 : 0);
	if (peer_mss && csk->mtu > (peer_mss + hdrs))
		csk->mtu = peer_mss + hdrs;

	csk->com.state = CSK_STATE_CONNECTING;
	csk->com.cdev = cdev;
	csk->cnp = cnp;
	csk->tos = PASS_OPEN_TOS_G(ntohl(req->tos_stid));
	csk->dst = dst;
	csk->tid = tid;
	csk->wr_cred = cdev->lldi.wr_cred -
			DIV_ROUND_UP(sizeof(struct cpl_abort_req), 16);
	csk->wr_max_cred = csk->wr_cred;
	csk->wr_una_cred = 0;

	if (iptype == 4) {
		struct sockaddr_in *sin = (struct sockaddr_in *)
					  &csk->com.local_addr;
		sin->sin_family = AF_INET;
		sin->sin_port = local_port;
		sin->sin_addr.s_addr = *(__be32 *)local_ip;

		sin = (struct sockaddr_in *)&csk->com.remote_addr;
		sin->sin_family = AF_INET;
		sin->sin_port = peer_port;
		sin->sin_addr.s_addr = *(__be32 *)peer_ip;
	} else {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)
					    &csk->com.local_addr;

		sin6->sin6_family = PF_INET6;
		sin6->sin6_port = local_port;
		memcpy(sin6->sin6_addr.s6_addr, local_ip, 16);
		cxgb4_clip_get(cdev->lldi.ports[0],
			       (const u32 *)&sin6->sin6_addr.s6_addr,
			       1);

		sin6 = (struct sockaddr_in6 *)&csk->com.remote_addr;
		sin6->sin6_family = PF_INET6;
		sin6->sin6_port = peer_port;
		memcpy(sin6->sin6_addr.s6_addr, peer_ip, 16);
	}

	skb_queue_head_init(&csk->rxq);
	skb_queue_head_init(&csk->txq);
	skb_queue_head_init(&csk->ppodq);
	skb_queue_head_init(&csk->backlogq);
	skb_queue_head_init(&csk->skbq);
	cxgbit_sock_reset_wr_list(csk);
	spin_lock_init(&csk->lock);
	init_waitqueue_head(&csk->waitq);
	init_waitqueue_head(&csk->ack_waitq);
	csk->lock_owner = false;

	if (cxgbit_alloc_csk_skb(csk)) {
		dst_release(dst);
		kfree(csk);
		goto rel_skb;
	}

	cxgbit_get_cdev(cdev);

	spin_lock(&cdev->cskq.lock);
	list_add_tail(&csk->list, &cdev->cskq.list);
	spin_unlock(&cdev->cskq.lock);

	cxgb4_insert_tid(t, csk, tid);
	cxgbit_pass_accept_rpl(csk, req);
	goto rel_skb;

reject:
	cxgbit_release_tid(cdev, tid);
rel_skb:
	__kfree_skb(skb);
}

static u32
cxgbit_tx_flowc_wr_credits(struct cxgbit_sock *csk, u32 *nparamsp,
			   u32 *flowclenp)
{
	u32 nparams, flowclen16, flowclen;

	nparams = FLOWC_WR_NPARAMS_MIN;

	if (csk->snd_wscale)
		nparams++;

#ifdef CONFIG_CHELSIO_T4_DCB
	nparams++;
#endif
	flowclen = offsetof(struct fw_flowc_wr, mnemval[nparams]);
	flowclen16 = DIV_ROUND_UP(flowclen, 16);
	flowclen = flowclen16 * 16;
	/*
	 * Return the number of 16-byte credits used by the flowc request.
	 * Pass back the nparams and actual flowc length if requested.
	 */
	if (nparamsp)
		*nparamsp = nparams;
	if (flowclenp)
		*flowclenp = flowclen;
	return flowclen16;
}

u32 cxgbit_send_tx_flowc_wr(struct cxgbit_sock *csk)
{
	struct cxgbit_device *cdev = csk->com.cdev;
	struct fw_flowc_wr *flowc;
	u32 nparams, flowclen16, flowclen;
	struct sk_buff *skb;
	u8 index;

#ifdef CONFIG_CHELSIO_T4_DCB
	u16 vlan = ((struct l2t_entry *)csk->l2t)->vlan;
#endif

	flowclen16 = cxgbit_tx_flowc_wr_credits(csk, &nparams, &flowclen);

	skb = __skb_dequeue(&csk->skbq);
	flowc = (struct fw_flowc_wr *)__skb_put(skb, flowclen);
	memset(flowc, 0, flowclen);

	flowc->op_to_nparams = cpu_to_be32(FW_WR_OP_V(FW_FLOWC_WR) |
					   FW_FLOWC_WR_NPARAMS_V(nparams));
	flowc->flowid_len16 = cpu_to_be32(FW_WR_LEN16_V(flowclen16) |
					  FW_WR_FLOWID_V(csk->tid));
	flowc->mnemval[0].mnemonic = FW_FLOWC_MNEM_PFNVFN;
	flowc->mnemval[0].val = cpu_to_be32(FW_PFVF_CMD_PFN_V
					    (csk->com.cdev->lldi.pf));
	flowc->mnemval[1].mnemonic = FW_FLOWC_MNEM_CH;
	flowc->mnemval[1].val = cpu_to_be32(csk->tx_chan);
	flowc->mnemval[2].mnemonic = FW_FLOWC_MNEM_PORT;
	flowc->mnemval[2].val = cpu_to_be32(csk->tx_chan);
	flowc->mnemval[3].mnemonic = FW_FLOWC_MNEM_IQID;
	flowc->mnemval[3].val = cpu_to_be32(csk->rss_qid);
	flowc->mnemval[4].mnemonic = FW_FLOWC_MNEM_SNDNXT;
	flowc->mnemval[4].val = cpu_to_be32(csk->snd_nxt);
	flowc->mnemval[5].mnemonic = FW_FLOWC_MNEM_RCVNXT;
	flowc->mnemval[5].val = cpu_to_be32(csk->rcv_nxt);
	flowc->mnemval[6].mnemonic = FW_FLOWC_MNEM_SNDBUF;
	flowc->mnemval[6].val = cpu_to_be32(csk->snd_win);
	flowc->mnemval[7].mnemonic = FW_FLOWC_MNEM_MSS;
	flowc->mnemval[7].val = cpu_to_be32(csk->emss);

	flowc->mnemval[8].mnemonic = FW_FLOWC_MNEM_TXDATAPLEN_MAX;
	if (test_bit(CDEV_ISO_ENABLE, &cdev->flags))
		flowc->mnemval[8].val = cpu_to_be32(CXGBIT_MAX_ISO_PAYLOAD);
	else
		flowc->mnemval[8].val = cpu_to_be32(16384);

	index = 9;

	if (csk->snd_wscale) {
		flowc->mnemval[index].mnemonic = FW_FLOWC_MNEM_RCV_SCALE;
		flowc->mnemval[index].val = cpu_to_be32(csk->snd_wscale);
		index++;
	}

#ifdef CONFIG_CHELSIO_T4_DCB
	flowc->mnemval[index].mnemonic = FW_FLOWC_MNEM_DCBPRIO;
	if (vlan == VLAN_NONE) {
		pr_warn("csk %u without VLAN Tag on DCB Link\n", csk->tid);
		flowc->mnemval[index].val = cpu_to_be32(0);
	} else
		flowc->mnemval[index].val = cpu_to_be32(
				(vlan & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT);
#endif

	pr_debug("%s: csk %p; tx_chan = %u; rss_qid = %u; snd_seq = %u;"
		 " rcv_seq = %u; snd_win = %u; emss = %u\n",
		 __func__, csk, csk->tx_chan, csk->rss_qid, csk->snd_nxt,
		 csk->rcv_nxt, csk->snd_win, csk->emss);
	set_wr_txq(skb, CPL_PRIORITY_DATA, csk->txq_idx);
	cxgbit_ofld_send(csk->com.cdev, skb);
	return flowclen16;
}

int cxgbit_setup_conn_digest(struct cxgbit_sock *csk)
{
	struct sk_buff *skb;
	struct cpl_set_tcb_field *req;
	u8 hcrc = csk->submode & CXGBIT_SUBMODE_HCRC;
	u8 dcrc = csk->submode & CXGBIT_SUBMODE_DCRC;
	unsigned int len = roundup(sizeof(*req), 16);
	int ret;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	/*  set up ulp submode */
	req = (struct cpl_set_tcb_field *)__skb_put(skb, len);
	memset(req, 0, len);

	INIT_TP_WR(req, csk->tid);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, csk->tid));
	req->reply_ctrl = htons(NO_REPLY_V(0) | QUEUENO_V(csk->rss_qid));
	req->word_cookie = htons(0);
	req->mask = cpu_to_be64(0x3 << 4);
	req->val = cpu_to_be64(((hcrc ? ULP_CRC_HEADER : 0) |
				(dcrc ? ULP_CRC_DATA : 0)) << 4);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, csk->ctrlq_idx);

	cxgbit_get_csk(csk);
	cxgbit_init_wr_wait(&csk->com.wr_wait);

	cxgbit_ofld_send(csk->com.cdev, skb);

	ret = cxgbit_wait_for_reply(csk->com.cdev,
				    &csk->com.wr_wait,
				    csk->tid, 5, __func__);
	if (ret)
		return -1;

	return 0;
}

int cxgbit_setup_conn_pgidx(struct cxgbit_sock *csk, u32 pg_idx)
{
	struct sk_buff *skb;
	struct cpl_set_tcb_field *req;
	unsigned int len = roundup(sizeof(*req), 16);
	int ret;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	req = (struct cpl_set_tcb_field *)__skb_put(skb, len);
	memset(req, 0, len);

	INIT_TP_WR(req, csk->tid);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, csk->tid));
	req->reply_ctrl = htons(NO_REPLY_V(0) | QUEUENO_V(csk->rss_qid));
	req->word_cookie = htons(0);
	req->mask = cpu_to_be64(0x3 << 8);
	req->val = cpu_to_be64(pg_idx << 8);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, csk->ctrlq_idx);

	cxgbit_get_csk(csk);
	cxgbit_init_wr_wait(&csk->com.wr_wait);

	cxgbit_ofld_send(csk->com.cdev, skb);

	ret = cxgbit_wait_for_reply(csk->com.cdev,
				    &csk->com.wr_wait,
				    csk->tid, 5, __func__);
	if (ret)
		return -1;

	return 0;
}

static void
cxgbit_pass_open_rpl(struct cxgbit_device *cdev, struct sk_buff *skb)
{
	struct cpl_pass_open_rpl *rpl = cplhdr(skb);
	struct tid_info *t = cdev->lldi.tids;
	unsigned int stid = GET_TID(rpl);
	struct cxgbit_np *cnp = lookup_stid(t, stid);

	pr_debug("%s: cnp = %p; stid = %u; status = %d\n",
		 __func__, cnp, stid, rpl->status);

	if (!cnp) {
		pr_info("%s stid %d lookup failure\n", __func__, stid);
		return;
	}

	cxgbit_wake_up(&cnp->com.wr_wait, __func__, rpl->status);
	cxgbit_put_cnp(cnp);
}

static void
cxgbit_close_listsrv_rpl(struct cxgbit_device *cdev, struct sk_buff *skb)
{
	struct cpl_close_listsvr_rpl *rpl = cplhdr(skb);
	struct tid_info *t = cdev->lldi.tids;
	unsigned int stid = GET_TID(rpl);
	struct cxgbit_np *cnp = lookup_stid(t, stid);

	pr_debug("%s: cnp = %p; stid = %u; status = %d\n",
		 __func__, cnp, stid, rpl->status);

	if (!cnp) {
		pr_info("%s stid %d lookup failure\n", __func__, stid);
		return;
	}

	cxgbit_wake_up(&cnp->com.wr_wait, __func__, rpl->status);
	cxgbit_put_cnp(cnp);
}

static void
cxgbit_pass_establish(struct cxgbit_device *cdev, struct sk_buff *skb)
{
	struct cpl_pass_establish *req = cplhdr(skb);
	struct tid_info *t = cdev->lldi.tids;
	unsigned int tid = GET_TID(req);
	struct cxgbit_sock *csk;
	struct cxgbit_np *cnp;
	u16 tcp_opt = be16_to_cpu(req->tcp_opt);
	u32 snd_isn = be32_to_cpu(req->snd_isn);
	u32 rcv_isn = be32_to_cpu(req->rcv_isn);

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find connection for tid %u.\n", tid);
		goto rel_skb;
	}
	cnp = csk->cnp;

	pr_debug("%s: csk %p; tid %u; cnp %p\n",
		 __func__, csk, tid, cnp);

	csk->write_seq = snd_isn;
	csk->snd_una = snd_isn;
	csk->snd_nxt = snd_isn;

	csk->rcv_nxt = rcv_isn;

	if (csk->rcv_win > (RCV_BUFSIZ_M << 10))
		csk->rx_credits = (csk->rcv_win - (RCV_BUFSIZ_M << 10));

	csk->snd_wscale = TCPOPT_SND_WSCALE_G(tcp_opt);
	cxgbit_set_emss(csk, tcp_opt);
	dst_confirm(csk->dst);
	csk->com.state = CSK_STATE_ESTABLISHED;
	spin_lock_bh(&cnp->np_accept_lock);
	list_add_tail(&csk->accept_node, &cnp->np_accept_list);
	spin_unlock_bh(&cnp->np_accept_lock);
	complete(&cnp->accept_comp);
rel_skb:
	__kfree_skb(skb);
}

static void cxgbit_queue_rx_skb(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	cxgbit_skcb_flags(skb) = 0;
	spin_lock_bh(&csk->rxq.lock);
	__skb_queue_tail(&csk->rxq, skb);
	spin_unlock_bh(&csk->rxq.lock);
	wake_up(&csk->waitq);
}

static void cxgbit_peer_close(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	pr_debug("%s: csk %p; tid %u; state %d\n",
		 __func__, csk, csk->tid, csk->com.state);

	switch (csk->com.state) {
	case CSK_STATE_ESTABLISHED:
		csk->com.state = CSK_STATE_CLOSING;
		cxgbit_queue_rx_skb(csk, skb);
		return;
	case CSK_STATE_CLOSING:
		/* simultaneous close */
		csk->com.state = CSK_STATE_MORIBUND;
		break;
	case CSK_STATE_MORIBUND:
		csk->com.state = CSK_STATE_DEAD;
		cxgbit_put_csk(csk);
		break;
	case CSK_STATE_ABORTING:
		break;
	default:
		pr_info("%s: cpl_peer_close in bad state %d\n",
			__func__, csk->com.state);
	}

	__kfree_skb(skb);
}

static void cxgbit_close_con_rpl(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	pr_debug("%s: csk %p; tid %u; state %d\n",
		 __func__, csk, csk->tid, csk->com.state);

	switch (csk->com.state) {
	case CSK_STATE_CLOSING:
		csk->com.state = CSK_STATE_MORIBUND;
		break;
	case CSK_STATE_MORIBUND:
		csk->com.state = CSK_STATE_DEAD;
		cxgbit_put_csk(csk);
		break;
	case CSK_STATE_ABORTING:
	case CSK_STATE_DEAD:
		break;
	default:
		pr_info("%s: cpl_close_con_rpl in bad state %d\n",
			__func__, csk->com.state);
	}

	__kfree_skb(skb);
}

static void cxgbit_abort_req_rss(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	struct cpl_abort_req_rss *hdr = cplhdr(skb);
	unsigned int tid = GET_TID(hdr);
	struct sk_buff *rpl_skb;
	bool release = false;
	bool wakeup_thread = false;
	u32 len = roundup(sizeof(struct cpl_abort_rpl), 16);

	pr_debug("%s: csk %p; tid %u; state %d\n",
		 __func__, csk, tid, csk->com.state);

	if (cxgb_is_neg_adv(hdr->status)) {
		pr_err("%s: got neg advise %d on tid %u\n",
		       __func__, hdr->status, tid);
		goto rel_skb;
	}

	switch (csk->com.state) {
	case CSK_STATE_CONNECTING:
	case CSK_STATE_MORIBUND:
		csk->com.state = CSK_STATE_DEAD;
		release = true;
		break;
	case CSK_STATE_ESTABLISHED:
		csk->com.state = CSK_STATE_DEAD;
		wakeup_thread = true;
		break;
	case CSK_STATE_CLOSING:
		csk->com.state = CSK_STATE_DEAD;
		if (!csk->conn)
			release = true;
		break;
	case CSK_STATE_ABORTING:
		break;
	default:
		pr_info("%s: cpl_abort_req_rss in bad state %d\n",
			__func__, csk->com.state);
		csk->com.state = CSK_STATE_DEAD;
	}

	__skb_queue_purge(&csk->txq);

	if (!test_and_set_bit(CSK_TX_DATA_SENT, &csk->com.flags))
		cxgbit_send_tx_flowc_wr(csk);

	rpl_skb = __skb_dequeue(&csk->skbq);

	cxgb_mk_abort_rpl(rpl_skb, len, csk->tid, csk->txq_idx);
	cxgbit_ofld_send(csk->com.cdev, rpl_skb);

	if (wakeup_thread) {
		cxgbit_queue_rx_skb(csk, skb);
		return;
	}

	if (release)
		cxgbit_put_csk(csk);
rel_skb:
	__kfree_skb(skb);
}

static void cxgbit_abort_rpl_rss(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	pr_debug("%s: csk %p; tid %u; state %d\n",
		 __func__, csk, csk->tid, csk->com.state);

	switch (csk->com.state) {
	case CSK_STATE_ABORTING:
		csk->com.state = CSK_STATE_DEAD;
		cxgbit_put_csk(csk);
		break;
	default:
		pr_info("%s: cpl_abort_rpl_rss in state %d\n",
			__func__, csk->com.state);
	}

	__kfree_skb(skb);
}

static bool cxgbit_credit_err(const struct cxgbit_sock *csk)
{
	const struct sk_buff *skb = csk->wr_pending_head;
	u32 credit = 0;

	if (unlikely(csk->wr_cred > csk->wr_max_cred)) {
		pr_err("csk 0x%p, tid %u, credit %u > %u\n",
		       csk, csk->tid, csk->wr_cred, csk->wr_max_cred);
		return true;
	}

	while (skb) {
		credit += skb->csum;
		skb = cxgbit_skcb_tx_wr_next(skb);
	}

	if (unlikely((csk->wr_cred + credit) != csk->wr_max_cred)) {
		pr_err("csk 0x%p, tid %u, credit %u + %u != %u.\n",
		       csk, csk->tid, csk->wr_cred,
		       credit, csk->wr_max_cred);

		return true;
	}

	return false;
}

static void cxgbit_fw4_ack(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	struct cpl_fw4_ack *rpl = (struct cpl_fw4_ack *)cplhdr(skb);
	u32 credits = rpl->credits;
	u32 snd_una = ntohl(rpl->snd_una);

	csk->wr_cred += credits;
	if (csk->wr_una_cred > (csk->wr_max_cred - csk->wr_cred))
		csk->wr_una_cred = csk->wr_max_cred - csk->wr_cred;

	while (credits) {
		struct sk_buff *p = cxgbit_sock_peek_wr(csk);

		if (unlikely(!p)) {
			pr_err("csk 0x%p,%u, cr %u,%u+%u, empty.\n",
			       csk, csk->tid, credits,
			       csk->wr_cred, csk->wr_una_cred);
			break;
		}

		if (unlikely(credits < p->csum)) {
			pr_warn("csk 0x%p,%u, cr %u,%u+%u, < %u.\n",
				csk,  csk->tid,
				credits, csk->wr_cred, csk->wr_una_cred,
				p->csum);
			p->csum -= credits;
			break;
		}

		cxgbit_sock_dequeue_wr(csk);
		credits -= p->csum;
		kfree_skb(p);
	}

	if (unlikely(cxgbit_credit_err(csk))) {
		cxgbit_queue_rx_skb(csk, skb);
		return;
	}

	if (rpl->seq_vld & CPL_FW4_ACK_FLAGS_SEQVAL) {
		if (unlikely(before(snd_una, csk->snd_una))) {
			pr_warn("csk 0x%p,%u, snd_una %u/%u.",
				csk, csk->tid, snd_una,
				csk->snd_una);
			goto rel_skb;
		}

		if (csk->snd_una != snd_una) {
			csk->snd_una = snd_una;
			dst_confirm(csk->dst);
			wake_up(&csk->ack_waitq);
		}
	}

	if (skb_queue_len(&csk->txq))
		cxgbit_push_tx_frames(csk);

rel_skb:
	__kfree_skb(skb);
}

static void cxgbit_set_tcb_rpl(struct cxgbit_device *cdev, struct sk_buff *skb)
{
	struct cxgbit_sock *csk;
	struct cpl_set_tcb_rpl *rpl = (struct cpl_set_tcb_rpl *)skb->data;
	unsigned int tid = GET_TID(rpl);
	struct cxgb4_lld_info *lldi = &cdev->lldi;
	struct tid_info *t = lldi->tids;

	csk = lookup_tid(t, tid);
	if (unlikely(!csk))
		pr_err("can't find connection for tid %u.\n", tid);
	else
		cxgbit_wake_up(&csk->com.wr_wait, __func__, rpl->status);

	cxgbit_put_csk(csk);
}

static void cxgbit_rx_data(struct cxgbit_device *cdev, struct sk_buff *skb)
{
	struct cxgbit_sock *csk;
	struct cpl_rx_data *cpl = cplhdr(skb);
	unsigned int tid = GET_TID(cpl);
	struct cxgb4_lld_info *lldi = &cdev->lldi;
	struct tid_info *t = lldi->tids;

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find conn. for tid %u.\n", tid);
		goto rel_skb;
	}

	cxgbit_queue_rx_skb(csk, skb);
	return;
rel_skb:
	__kfree_skb(skb);
}

static void
__cxgbit_process_rx_cpl(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	spin_lock(&csk->lock);
	if (csk->lock_owner) {
		__skb_queue_tail(&csk->backlogq, skb);
		spin_unlock(&csk->lock);
		return;
	}

	cxgbit_skcb_rx_backlog_fn(skb)(csk, skb);
	spin_unlock(&csk->lock);
}

static void cxgbit_process_rx_cpl(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	cxgbit_get_csk(csk);
	__cxgbit_process_rx_cpl(csk, skb);
	cxgbit_put_csk(csk);
}

static void cxgbit_rx_cpl(struct cxgbit_device *cdev, struct sk_buff *skb)
{
	struct cxgbit_sock *csk;
	struct cpl_tx_data *cpl = cplhdr(skb);
	struct cxgb4_lld_info *lldi = &cdev->lldi;
	struct tid_info *t = lldi->tids;
	unsigned int tid = GET_TID(cpl);
	u8 opcode = cxgbit_skcb_rx_opcode(skb);
	bool ref = true;

	switch (opcode) {
	case CPL_FW4_ACK:
			cxgbit_skcb_rx_backlog_fn(skb) = cxgbit_fw4_ack;
			ref = false;
			break;
	case CPL_PEER_CLOSE:
			cxgbit_skcb_rx_backlog_fn(skb) = cxgbit_peer_close;
			break;
	case CPL_CLOSE_CON_RPL:
			cxgbit_skcb_rx_backlog_fn(skb) = cxgbit_close_con_rpl;
			break;
	case CPL_ABORT_REQ_RSS:
			cxgbit_skcb_rx_backlog_fn(skb) = cxgbit_abort_req_rss;
			break;
	case CPL_ABORT_RPL_RSS:
			cxgbit_skcb_rx_backlog_fn(skb) = cxgbit_abort_rpl_rss;
			break;
	default:
		goto rel_skb;
	}

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find conn. for tid %u.\n", tid);
		goto rel_skb;
	}

	if (ref)
		cxgbit_process_rx_cpl(csk, skb);
	else
		__cxgbit_process_rx_cpl(csk, skb);

	return;
rel_skb:
	__kfree_skb(skb);
}

cxgbit_cplhandler_func cxgbit_cplhandlers[NUM_CPL_CMDS] = {
	[CPL_PASS_OPEN_RPL]	= cxgbit_pass_open_rpl,
	[CPL_CLOSE_LISTSRV_RPL] = cxgbit_close_listsrv_rpl,
	[CPL_PASS_ACCEPT_REQ]	= cxgbit_pass_accept_req,
	[CPL_PASS_ESTABLISH]	= cxgbit_pass_establish,
	[CPL_SET_TCB_RPL]	= cxgbit_set_tcb_rpl,
	[CPL_RX_DATA]		= cxgbit_rx_data,
	[CPL_FW4_ACK]		= cxgbit_rx_cpl,
	[CPL_PEER_CLOSE]	= cxgbit_rx_cpl,
	[CPL_CLOSE_CON_RPL]	= cxgbit_rx_cpl,
	[CPL_ABORT_REQ_RSS]	= cxgbit_rx_cpl,
	[CPL_ABORT_RPL_RSS]	= cxgbit_rx_cpl,
};
