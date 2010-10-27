/* cxgb3i_iscsi.c: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 * Copyright (c) 2008 Mike Christie
 * Copyright (c) 2008 Red Hat, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#include <linux/inet.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <linux/if_vlan.h>
#include <net/dst.h>
#include <net/tcp.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/iscsi_proto.h>
#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>

#include "cxgb3i.h"
#include "cxgb3i_pdu.h"

#ifdef __DEBUG_CXGB3I_TAG__
#define cxgb3i_tag_debug	cxgb3i_log_debug
#else
#define cxgb3i_tag_debug(fmt...)
#endif

#ifdef __DEBUG_CXGB3I_API__
#define cxgb3i_api_debug	cxgb3i_log_debug
#else
#define cxgb3i_api_debug(fmt...)
#endif

/*
 * align pdu size to multiple of 512 for better performance
 */
#define align_pdu_size(n) do { n = (n) & (~511); } while (0)

static struct scsi_transport_template *cxgb3i_scsi_transport;
static struct scsi_host_template cxgb3i_host_template;
static struct iscsi_transport cxgb3i_iscsi_transport;
static unsigned char sw_tag_idx_bits;
static unsigned char sw_tag_age_bits;

static LIST_HEAD(cxgb3i_snic_list);
static DEFINE_RWLOCK(cxgb3i_snic_rwlock);

/**
 * cxgb3i_adpater_find_by_tdev - find the cxgb3i_adapter structure via t3cdev
 * @tdev: t3cdev pointer
 */
struct cxgb3i_adapter *cxgb3i_adapter_find_by_tdev(struct t3cdev *tdev)
{
	struct cxgb3i_adapter *snic;

	read_lock(&cxgb3i_snic_rwlock);
	list_for_each_entry(snic, &cxgb3i_snic_list, list_head) {
		if (snic->tdev == tdev) {
			read_unlock(&cxgb3i_snic_rwlock);
			return snic;
		}
	}
	read_unlock(&cxgb3i_snic_rwlock);
	return NULL;
}

static inline int adapter_update(struct cxgb3i_adapter *snic)
{
	cxgb3i_log_info("snic 0x%p, t3dev 0x%p, updating.\n",
			snic, snic->tdev);
	return cxgb3i_adapter_ddp_info(snic->tdev, &snic->tag_format,
					&snic->tx_max_size,
					&snic->rx_max_size);
}

static int adapter_add(struct cxgb3i_adapter *snic)
{
	struct t3cdev *t3dev = snic->tdev;
	struct adapter *adapter = tdev2adap(t3dev);
	int i, err;

	snic->pdev = adapter->pdev;
	snic->tag_format.sw_bits = sw_tag_idx_bits + sw_tag_age_bits;

	err = cxgb3i_adapter_ddp_info(t3dev, &snic->tag_format,
				    &snic->tx_max_size,
				    &snic->rx_max_size);
	if (err < 0)
		return err;

	for_each_port(adapter, i) {
		snic->hba[i] = cxgb3i_hba_host_add(snic, adapter->port[i]);
		if (!snic->hba[i])
			return -EINVAL;
	}
	snic->hba_cnt = adapter->params.nports;

	/* add to the list */
	write_lock(&cxgb3i_snic_rwlock);
	list_add_tail(&snic->list_head, &cxgb3i_snic_list);
	write_unlock(&cxgb3i_snic_rwlock);

	cxgb3i_log_info("t3dev 0x%p open, snic 0x%p, %u scsi hosts added.\n",
			t3dev, snic, snic->hba_cnt);
	return 0;
}

/**
 * cxgb3i_adapter_open - init a s3 adapter structure and any h/w settings
 * @t3dev: t3cdev adapter
 */
void cxgb3i_adapter_open(struct t3cdev *t3dev)
{
	struct cxgb3i_adapter *snic = cxgb3i_adapter_find_by_tdev(t3dev);
	int err;

	if (snic)
		err = adapter_update(snic);
	else {
		snic = kzalloc(sizeof(*snic), GFP_KERNEL);
		if (snic) {
			spin_lock_init(&snic->lock);
			snic->tdev = t3dev;
			err = adapter_add(snic);
		} else
			err = -ENOMEM;
	}

	if (err < 0) {
		cxgb3i_log_info("snic 0x%p, f 0x%x, t3dev 0x%p open, err %d.\n",
				snic, snic ? snic->flags : 0, t3dev, err);
		if (snic) {
			snic->flags &= ~CXGB3I_ADAPTER_FLAG_RESET;
			cxgb3i_adapter_close(t3dev);
		}
	}
}

/**
 * cxgb3i_adapter_close - release the resources held and cleanup h/w settings
 * @t3dev: t3cdev adapter
 */
void cxgb3i_adapter_close(struct t3cdev *t3dev)
{
	struct cxgb3i_adapter *snic = cxgb3i_adapter_find_by_tdev(t3dev);
	int i;

	if (!snic || snic->flags & CXGB3I_ADAPTER_FLAG_RESET) {
		cxgb3i_log_info("t3dev 0x%p close, snic 0x%p, f 0x%x.\n",
				t3dev, snic, snic ? snic->flags : 0);
		return;
	}

	/* remove from the list */
	write_lock(&cxgb3i_snic_rwlock);
	list_del(&snic->list_head);
	write_unlock(&cxgb3i_snic_rwlock);

	for (i = 0; i < snic->hba_cnt; i++) {
		if (snic->hba[i]) {
			cxgb3i_hba_host_remove(snic->hba[i]);
			snic->hba[i] = NULL;
		}
	}
	cxgb3i_log_info("t3dev 0x%p close, snic 0x%p, %u scsi hosts removed.\n",
			t3dev, snic, snic->hba_cnt);
	kfree(snic);
}

/**
 * cxgb3i_hba_find_by_netdev - find the cxgb3i_hba structure via net_device
 * @t3dev: t3cdev adapter
 */
static struct cxgb3i_hba *cxgb3i_hba_find_by_netdev(struct net_device *ndev)
{
	struct cxgb3i_adapter *snic;
	int i;

	if (ndev->priv_flags & IFF_802_1Q_VLAN)
		ndev = vlan_dev_real_dev(ndev);

	read_lock(&cxgb3i_snic_rwlock);
	list_for_each_entry(snic, &cxgb3i_snic_list, list_head) {
		for (i = 0; i < snic->hba_cnt; i++) {
			if (snic->hba[i]->ndev == ndev) {
				read_unlock(&cxgb3i_snic_rwlock);
				return snic->hba[i];
			}
		}
	}
	read_unlock(&cxgb3i_snic_rwlock);
	return NULL;
}

/**
 * cxgb3i_hba_host_add - register a new host with scsi/iscsi
 * @snic: the cxgb3i adapter
 * @ndev: associated net_device
 */
struct cxgb3i_hba *cxgb3i_hba_host_add(struct cxgb3i_adapter *snic,
				       struct net_device *ndev)
{
	struct cxgb3i_hba *hba;
	struct Scsi_Host *shost;
	int err;

	shost = iscsi_host_alloc(&cxgb3i_host_template,
				 sizeof(struct cxgb3i_hba), 1);
	if (!shost) {
		cxgb3i_log_info("snic 0x%p, ndev 0x%p, host_alloc failed.\n",
				snic, ndev);
		return NULL;
	}

	shost->transportt = cxgb3i_scsi_transport;
	shost->max_lun = CXGB3I_MAX_LUN;
	shost->max_id = CXGB3I_MAX_TARGET;
	shost->max_channel = 0;
	shost->max_cmd_len = 16;

	hba = iscsi_host_priv(shost);
	hba->snic = snic;
	hba->ndev = ndev;
	hba->shost = shost;

	pci_dev_get(snic->pdev);
	err = iscsi_host_add(shost, &snic->pdev->dev);
	if (err) {
		cxgb3i_log_info("snic 0x%p, ndev 0x%p, host_add failed.\n",
				snic, ndev);
		goto pci_dev_put;
	}

	cxgb3i_api_debug("shost 0x%p, hba 0x%p, no %u.\n",
			 shost, hba, shost->host_no);

	return hba;

pci_dev_put:
	pci_dev_put(snic->pdev);
	scsi_host_put(shost);
	return NULL;
}

/**
 * cxgb3i_hba_host_remove - de-register the host with scsi/iscsi
 * @hba: the cxgb3i hba
 */
void cxgb3i_hba_host_remove(struct cxgb3i_hba *hba)
{
	cxgb3i_api_debug("shost 0x%p, hba 0x%p, no %u.\n",
			 hba->shost, hba, hba->shost->host_no);
	iscsi_host_remove(hba->shost);
	pci_dev_put(hba->snic->pdev);
	iscsi_host_free(hba->shost);
}

/**
 * cxgb3i_ep_connect - establish TCP connection to target portal
 * @shost:		scsi host to use
 * @dst_addr:		target IP address
 * @non_blocking:	blocking or non-blocking call
 *
 * Initiates a TCP/IP connection to the dst_addr
 */
static struct iscsi_endpoint *cxgb3i_ep_connect(struct Scsi_Host *shost,
						struct sockaddr *dst_addr,
						int non_blocking)
{
	struct iscsi_endpoint *ep;
	struct cxgb3i_endpoint *cep;
	struct cxgb3i_hba *hba = NULL;
	struct s3_conn *c3cn = NULL;
	int err = 0;

	if (shost)
		hba = iscsi_host_priv(shost);

	cxgb3i_api_debug("shost 0x%p, hba 0x%p.\n", shost, hba);

	c3cn = cxgb3i_c3cn_create();
	if (!c3cn) {
		cxgb3i_log_info("ep connect OOM.\n");
		err = -ENOMEM;
		goto release_conn;
	}

	err = cxgb3i_c3cn_connect(hba ? hba->ndev : NULL, c3cn,
				 (struct sockaddr_in *)dst_addr);
	if (err < 0) {
		cxgb3i_log_info("ep connect failed.\n");
		goto release_conn;
	}

	hba = cxgb3i_hba_find_by_netdev(c3cn->dst_cache->dev);
	if (!hba) {
		err = -ENOSPC;
		cxgb3i_log_info("NOT going through cxgbi device.\n");
		goto release_conn;
	}

	if (shost && hba != iscsi_host_priv(shost)) {
		err = -ENOSPC;
		cxgb3i_log_info("Could not connect through request host%u\n",
				shost->host_no);
		goto release_conn;
	}

	if (c3cn_is_closing(c3cn)) {
		err = -ENOSPC;
		cxgb3i_log_info("ep connect unable to connect.\n");
		goto release_conn;
	}

	ep = iscsi_create_endpoint(sizeof(*cep));
	if (!ep) {
		err = -ENOMEM;
		cxgb3i_log_info("iscsi alloc ep, OOM.\n");
		goto release_conn;
	}
	cep = ep->dd_data;
	cep->c3cn = c3cn;
	cep->hba = hba;

	cxgb3i_api_debug("ep 0x%p, 0x%p, c3cn 0x%p, hba 0x%p.\n",
			  ep, cep, c3cn, hba);
	return ep;

release_conn:
	cxgb3i_api_debug("conn 0x%p failed, release.\n", c3cn);
	if (c3cn)
		cxgb3i_c3cn_release(c3cn);
	return ERR_PTR(err);
}

/**
 * cxgb3i_ep_poll - polls for TCP connection establishement
 * @ep:		TCP connection (endpoint) handle
 * @timeout_ms:	timeout value in milli secs
 *
 * polls for TCP connect request to complete
 */
static int cxgb3i_ep_poll(struct iscsi_endpoint *ep, int timeout_ms)
{
	struct cxgb3i_endpoint *cep = ep->dd_data;
	struct s3_conn *c3cn = cep->c3cn;

	if (!c3cn_is_established(c3cn))
		return 0;
	cxgb3i_api_debug("ep 0x%p, c3cn 0x%p established.\n", ep, c3cn);
	return 1;
}

/**
 * cxgb3i_ep_disconnect - teardown TCP connection
 * @ep:		TCP connection (endpoint) handle
 *
 * teardown TCP connection
 */
static void cxgb3i_ep_disconnect(struct iscsi_endpoint *ep)
{
	struct cxgb3i_endpoint *cep = ep->dd_data;
	struct cxgb3i_conn *cconn = cep->cconn;

	cxgb3i_api_debug("ep 0x%p, cep 0x%p.\n", ep, cep);

	if (cconn && cconn->conn) {
		/*
		 * stop the xmit path so the xmit_pdu function is
		 * not being called
		 */
		iscsi_suspend_tx(cconn->conn);

		write_lock_bh(&cep->c3cn->callback_lock);
		cep->c3cn->user_data = NULL;
		cconn->cep = NULL;
		write_unlock_bh(&cep->c3cn->callback_lock);
	}

	cxgb3i_api_debug("ep 0x%p, cep 0x%p, release c3cn 0x%p.\n",
			 ep, cep, cep->c3cn);
	cxgb3i_c3cn_release(cep->c3cn);
	iscsi_destroy_endpoint(ep);
}

/**
 * cxgb3i_session_create - create a new iscsi session
 * @cmds_max:		max # of commands
 * @qdepth:		scsi queue depth
 * @initial_cmdsn:	initial iscsi CMDSN for this session
 *
 * Creates a new iSCSI session
 */
static struct iscsi_cls_session *
cxgb3i_session_create(struct iscsi_endpoint *ep, u16 cmds_max, u16 qdepth,
		      u32 initial_cmdsn)
{
	struct cxgb3i_endpoint *cep;
	struct cxgb3i_hba *hba;
	struct Scsi_Host *shost;
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;

	if (!ep) {
		cxgb3i_log_error("%s, missing endpoint.\n", __func__);
		return NULL;
	}

	cep = ep->dd_data;
	hba = cep->hba;
	shost = hba->shost;
	cxgb3i_api_debug("ep 0x%p, cep 0x%p, hba 0x%p.\n", ep, cep, hba);
	BUG_ON(hba != iscsi_host_priv(shost));

	cls_session = iscsi_session_setup(&cxgb3i_iscsi_transport, shost,
					  cmds_max, 0,
					  sizeof(struct iscsi_tcp_task) +
					  sizeof(struct cxgb3i_task_data),
					  initial_cmdsn, ISCSI_MAX_TARGET);
	if (!cls_session)
		return NULL;
	session = cls_session->dd_data;
	if (iscsi_tcp_r2tpool_alloc(session))
		goto remove_session;

	return cls_session;

remove_session:
	iscsi_session_teardown(cls_session);
	return NULL;
}

/**
 * cxgb3i_session_destroy - destroys iscsi session
 * @cls_session:	pointer to iscsi cls session
 *
 * Destroys an iSCSI session instance and releases its all resources held
 */
static void cxgb3i_session_destroy(struct iscsi_cls_session *cls_session)
{
	cxgb3i_api_debug("sess 0x%p.\n", cls_session);
	iscsi_tcp_r2tpool_free(cls_session->dd_data);
	iscsi_session_teardown(cls_session);
}

/**
 * cxgb3i_conn_max_xmit_dlength -- calc the max. xmit pdu segment size
 * @conn: iscsi connection
 * check the max. xmit pdu payload, reduce it if needed
 */
static inline int cxgb3i_conn_max_xmit_dlength(struct iscsi_conn *conn)

{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	unsigned int max = max(512 * MAX_SKB_FRAGS, SKB_TX_HEADROOM);

	max = min(cconn->hba->snic->tx_max_size, max);
	if (conn->max_xmit_dlength)
		conn->max_xmit_dlength = min(conn->max_xmit_dlength, max);
	else
		conn->max_xmit_dlength = max;
	align_pdu_size(conn->max_xmit_dlength);
	cxgb3i_api_debug("conn 0x%p, max xmit %u.\n",
			 conn, conn->max_xmit_dlength);
	return 0;
}

/**
 * cxgb3i_conn_max_recv_dlength -- check the max. recv pdu segment size
 * @conn: iscsi connection
 * return 0 if the value is valid, < 0 otherwise.
 */
static inline int cxgb3i_conn_max_recv_dlength(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	unsigned int max = cconn->hba->snic->rx_max_size;

	align_pdu_size(max);
	if (conn->max_recv_dlength) {
		if (conn->max_recv_dlength > max) {
			cxgb3i_log_error("MaxRecvDataSegmentLength %u too big."
					 " Need to be <= %u.\n",
					 conn->max_recv_dlength, max);
			return -EINVAL;
		}
		conn->max_recv_dlength = min(conn->max_recv_dlength, max);
		align_pdu_size(conn->max_recv_dlength);
	} else
		conn->max_recv_dlength = max;
	cxgb3i_api_debug("conn 0x%p, max recv %u.\n",
			 conn, conn->max_recv_dlength);
	return 0;
}

/**
 * cxgb3i_conn_create - create iscsi connection instance
 * @cls_session:	pointer to iscsi cls session
 * @cid:		iscsi cid
 *
 * Creates a new iSCSI connection instance for a given session
 */
static struct iscsi_cls_conn *cxgb3i_conn_create(struct iscsi_cls_session
						 *cls_session, u32 cid)
{
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_conn *conn;
	struct iscsi_tcp_conn *tcp_conn;
	struct cxgb3i_conn *cconn;

	cxgb3i_api_debug("sess 0x%p, cid %u.\n", cls_session, cid);

	cls_conn = iscsi_tcp_conn_setup(cls_session, sizeof(*cconn), cid);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;
	tcp_conn = conn->dd_data;
	cconn = tcp_conn->dd_data;

	cconn->conn = conn;
	return cls_conn;
}

/**
 * cxgb3i_conn_bind - binds iscsi sess, conn and endpoint together
 * @cls_session:	pointer to iscsi cls session
 * @cls_conn:		pointer to iscsi cls conn
 * @transport_eph:	64-bit EP handle
 * @is_leading:		leading connection on this session?
 *
 * Binds together an iSCSI session, an iSCSI connection and a
 *	TCP connection. This routine returns error code if the TCP
 *	connection does not belong on the device iSCSI sess/conn is bound
 */

static int cxgb3i_conn_bind(struct iscsi_cls_session *cls_session,
			    struct iscsi_cls_conn *cls_conn,
			    u64 transport_eph, int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	struct cxgb3i_adapter *snic;
	struct iscsi_endpoint *ep;
	struct cxgb3i_endpoint *cep;
	struct s3_conn *c3cn;
	int err;

	ep = iscsi_lookup_endpoint(transport_eph);
	if (!ep)
		return -EINVAL;

	/* setup ddp pagesize */
	cep = ep->dd_data;
	c3cn = cep->c3cn;
	snic = cep->hba->snic;
	err = cxgb3i_setup_conn_host_pagesize(snic->tdev, c3cn->tid, 0);
	if (err < 0)
		return err;

	cxgb3i_api_debug("ep 0x%p, cls sess 0x%p, cls conn 0x%p.\n",
			 ep, cls_session, cls_conn);

	err = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (err)
		return -EINVAL;

	/* calculate the tag idx bits needed for this conn based on cmds_max */
	cconn->task_idx_bits = (__ilog2_u32(conn->session->cmds_max - 1)) + 1;
	cxgb3i_api_debug("session cmds_max 0x%x, bits %u.\n",
			 conn->session->cmds_max, cconn->task_idx_bits);

	read_lock(&c3cn->callback_lock);
	c3cn->user_data = conn;
	cconn->hba = cep->hba;
	cconn->cep = cep;
	cep->cconn = cconn;
	read_unlock(&c3cn->callback_lock);

	cxgb3i_conn_max_xmit_dlength(conn);
	cxgb3i_conn_max_recv_dlength(conn);

	spin_lock_bh(&conn->session->lock);
	sprintf(conn->portal_address, "%pI4", &c3cn->daddr.sin_addr.s_addr);
	conn->portal_port = ntohs(c3cn->daddr.sin_port);
	spin_unlock_bh(&conn->session->lock);

	/* init recv engine */
	iscsi_tcp_hdr_recv_prep(tcp_conn);

	return 0;
}

/**
 * cxgb3i_conn_get_param - return iscsi connection parameter to caller
 * @cls_conn:	pointer to iscsi cls conn
 * @param:	parameter type identifier
 * @buf:	buffer pointer
 *
 * returns iSCSI connection parameters
 */
static int cxgb3i_conn_get_param(struct iscsi_cls_conn *cls_conn,
				 enum iscsi_param param, char *buf)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	int len;

	cxgb3i_api_debug("cls_conn 0x%p, param %d.\n", cls_conn, param);

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
		spin_lock_bh(&conn->session->lock);
		len = sprintf(buf, "%hu\n", conn->portal_port);
		spin_unlock_bh(&conn->session->lock);
		break;
	case ISCSI_PARAM_CONN_ADDRESS:
		spin_lock_bh(&conn->session->lock);
		len = sprintf(buf, "%s\n", conn->portal_address);
		spin_unlock_bh(&conn->session->lock);
		break;
	default:
		return iscsi_conn_get_param(cls_conn, param, buf);
	}

	return len;
}

/**
 * cxgb3i_conn_set_param - set iscsi connection parameter
 * @cls_conn:	pointer to iscsi cls conn
 * @param:	parameter type identifier
 * @buf:	buffer pointer
 * @buflen:	buffer length
 *
 * set iSCSI connection parameters
 */
static int cxgb3i_conn_set_param(struct iscsi_cls_conn *cls_conn,
				 enum iscsi_param param, char *buf, int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	struct cxgb3i_adapter *snic = cconn->hba->snic;
	struct s3_conn *c3cn = cconn->cep->c3cn;
	int value, err = 0;

	switch (param) {
	case ISCSI_PARAM_HDRDGST_EN:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && conn->hdrdgst_en)
			err = cxgb3i_setup_conn_digest(snic->tdev, c3cn->tid,
							conn->hdrdgst_en,
							conn->datadgst_en, 0);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err && conn->datadgst_en)
			err = cxgb3i_setup_conn_digest(snic->tdev, c3cn->tid,
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
			err = cxgb3i_conn_max_recv_dlength(conn);
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		err = iscsi_set_param(cls_conn, param, buf, buflen);
		if (!err)
			err = cxgb3i_conn_max_xmit_dlength(conn);
		break;
	default:
		return iscsi_set_param(cls_conn, param, buf, buflen);
	}
	return err;
}

/**
 * cxgb3i_host_set_param - configure host (adapter) related parameters
 * @shost:	scsi host pointer
 * @param:	parameter type identifier
 * @buf:	buffer pointer
 */
static int cxgb3i_host_set_param(struct Scsi_Host *shost,
				 enum iscsi_host_param param,
				 char *buf, int buflen)
{
	struct cxgb3i_hba *hba = iscsi_host_priv(shost);

	if (!hba->ndev) {
		shost_printk(KERN_ERR, shost, "Could not set host param. "
			     "Netdev for host not set.\n");
		return -ENODEV;
	}

	cxgb3i_api_debug("param %d, buf %s.\n", param, buf);

	switch (param) {
	case ISCSI_HOST_PARAM_IPADDRESS:
	{
		__be32 addr = in_aton(buf);
		cxgb3i_set_private_ipv4addr(hba->ndev, addr);
		return 0;
	}
	case ISCSI_HOST_PARAM_HWADDRESS:
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		/* ignore */
		return 0;
	default:
		return iscsi_host_set_param(shost, param, buf, buflen);
	}
}

/**
 * cxgb3i_host_get_param - returns host (adapter) related parameters
 * @shost:	scsi host pointer
 * @param:	parameter type identifier
 * @buf:	buffer pointer
 */
static int cxgb3i_host_get_param(struct Scsi_Host *shost,
				 enum iscsi_host_param param, char *buf)
{
	struct cxgb3i_hba *hba = iscsi_host_priv(shost);
	int len = 0;

	if (!hba->ndev) {
		shost_printk(KERN_ERR, shost, "Could not set host param. "
			     "Netdev for host not set.\n");
		return -ENODEV;
	}

	cxgb3i_api_debug("hba %s, param %d.\n", hba->ndev->name, param);

	switch (param) {
	case ISCSI_HOST_PARAM_HWADDRESS:
		len = sysfs_format_mac(buf, hba->ndev->dev_addr, 6);
		break;
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		len = sprintf(buf, "%s\n", hba->ndev->name);
		break;
	case ISCSI_HOST_PARAM_IPADDRESS:
	{
		__be32 addr;

		addr = cxgb3i_get_private_ipv4addr(hba->ndev);
		len = sprintf(buf, "%pI4", &addr);
		break;
	}
	default:
		return iscsi_host_get_param(shost, param, buf);
	}
	return len;
}

/**
 * cxgb3i_conn_get_stats - returns iSCSI stats
 * @cls_conn:	pointer to iscsi cls conn
 * @stats:	pointer to iscsi statistic struct
 */
static void cxgb3i_conn_get_stats(struct iscsi_cls_conn *cls_conn,
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

/**
 * cxgb3i_parse_itt - get the idx and age bits from a given tag
 * @conn:	iscsi connection
 * @itt:	itt tag
 * @idx:	task index, filled in by this function
 * @age:	session age, filled in by this function
 */
static void cxgb3i_parse_itt(struct iscsi_conn *conn, itt_t itt,
			     int *idx, int *age)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	struct cxgb3i_adapter *snic = cconn->hba->snic;
	u32 tag = ntohl((__force u32) itt);
	u32 sw_bits;

	sw_bits = cxgb3i_tag_nonrsvd_bits(&snic->tag_format, tag);
	if (idx)
		*idx = sw_bits & ((1 << cconn->task_idx_bits) - 1);
	if (age)
		*age = (sw_bits >> cconn->task_idx_bits) & ISCSI_AGE_MASK;

	cxgb3i_tag_debug("parse tag 0x%x/0x%x, sw 0x%x, itt 0x%x, age 0x%x.\n",
			 tag, itt, sw_bits, idx ? *idx : 0xFFFFF,
			 age ? *age : 0xFF);
}

/**
 * cxgb3i_reserve_itt - generate tag for a give task
 * @task: iscsi task
 * @hdr_itt: tag, filled in by this function
 * Set up ddp for scsi read tasks if possible.
 */
int cxgb3i_reserve_itt(struct iscsi_task *task, itt_t *hdr_itt)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *sess = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	struct cxgb3i_adapter *snic = cconn->hba->snic;
	struct cxgb3i_tag_format *tformat = &snic->tag_format;
	u32 sw_tag = (sess->age << cconn->task_idx_bits) | task->itt;
	u32 tag;
	int err = -EINVAL;

	if (sc &&
	    (scsi_bidi_cmnd(sc) || sc->sc_data_direction == DMA_FROM_DEVICE) &&
	    cxgb3i_sw_tag_usable(tformat, sw_tag)) {
		struct s3_conn *c3cn = cconn->cep->c3cn;
		struct cxgb3i_gather_list *gl;

		gl = cxgb3i_ddp_make_gl(scsi_in(sc)->length,
					scsi_in(sc)->table.sgl,
					scsi_in(sc)->table.nents,
					snic->pdev,
					GFP_ATOMIC);
		if (gl) {
			tag = sw_tag;
			err = cxgb3i_ddp_tag_reserve(snic->tdev, c3cn->tid,
						     tformat, &tag,
						     gl, GFP_ATOMIC);
			if (err < 0)
				cxgb3i_ddp_release_gl(gl, snic->pdev);
		}
	}

	if (err < 0)
		tag = cxgb3i_set_non_ddp_tag(tformat, sw_tag);
	/* the itt need to sent in big-endian order */
	*hdr_itt = (__force itt_t)htonl(tag);

	cxgb3i_tag_debug("new tag 0x%x/0x%x (itt 0x%x, age 0x%x).\n",
			 tag, *hdr_itt, task->itt, sess->age);
	return 0;
}

/**
 * cxgb3i_release_itt - release the tag for a given task
 * @task:	iscsi task
 * @hdr_itt:	tag
 * If the tag is a ddp tag, release the ddp setup
 */
void cxgb3i_release_itt(struct iscsi_task *task, itt_t hdr_itt)
{
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_tcp_conn *tcp_conn = task->conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	struct cxgb3i_adapter *snic = cconn->hba->snic;
	struct cxgb3i_tag_format *tformat = &snic->tag_format;
	u32 tag = ntohl((__force u32)hdr_itt);

	cxgb3i_tag_debug("release tag 0x%x.\n", tag);

	if (sc &&
	    (scsi_bidi_cmnd(sc) || sc->sc_data_direction == DMA_FROM_DEVICE) &&
	    cxgb3i_is_ddp_tag(tformat, tag))
		cxgb3i_ddp_tag_release(snic->tdev, tag);
}

/**
 * cxgb3i_host_template -- Scsi_Host_Template structure
 *	used when registering with the scsi mid layer
 */
static struct scsi_host_template cxgb3i_host_template = {
	.module			= THIS_MODULE,
	.name			= "Chelsio S3xx iSCSI Initiator",
	.proc_name		= "cxgb3i",
	.queuecommand		= iscsi_queuecommand,
	.change_queue_depth	= iscsi_change_queue_depth,
	.can_queue		= CXGB3I_SCSI_HOST_QDEPTH,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= 0xFFFF,
	.cmd_per_lun		= ISCSI_DEF_CMD_PER_LUN,
	.eh_abort_handler	= iscsi_eh_abort,
	.eh_device_reset_handler = iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_recover_target,
	.target_alloc		= iscsi_target_alloc,
	.use_clustering		= DISABLE_CLUSTERING,
	.this_id		= -1,
};

static struct iscsi_transport cxgb3i_iscsi_transport = {
	.owner			= THIS_MODULE,
	.name			= "cxgb3i",
	.caps			= CAP_RECOVERY_L0 | CAP_MULTI_R2T | CAP_HDRDGST
				| CAP_DATADGST | CAP_DIGEST_OFFLOAD |
				CAP_PADDING_OFFLOAD,
	.param_mask		= ISCSI_MAX_RECV_DLENGTH |
				ISCSI_MAX_XMIT_DLENGTH |
				ISCSI_HDRDGST_EN |
				ISCSI_DATADGST_EN |
				ISCSI_INITIAL_R2T_EN |
				ISCSI_MAX_R2T |
				ISCSI_IMM_DATA_EN |
				ISCSI_FIRST_BURST |
				ISCSI_MAX_BURST |
				ISCSI_PDU_INORDER_EN |
				ISCSI_DATASEQ_INORDER_EN |
				ISCSI_ERL |
				ISCSI_CONN_PORT |
				ISCSI_CONN_ADDRESS |
				ISCSI_EXP_STATSN |
				ISCSI_PERSISTENT_PORT |
				ISCSI_PERSISTENT_ADDRESS |
				ISCSI_TARGET_NAME | ISCSI_TPGT |
				ISCSI_USERNAME | ISCSI_PASSWORD |
				ISCSI_USERNAME_IN | ISCSI_PASSWORD_IN |
				ISCSI_FAST_ABORT | ISCSI_ABORT_TMO |
				ISCSI_LU_RESET_TMO | ISCSI_TGT_RESET_TMO |
				ISCSI_PING_TMO | ISCSI_RECV_TMO |
				ISCSI_IFACE_NAME | ISCSI_INITIATOR_NAME,
	.host_param_mask	= ISCSI_HOST_HWADDRESS | ISCSI_HOST_IPADDRESS |
			ISCSI_HOST_INITIATOR_NAME | ISCSI_HOST_NETDEV_NAME,
	.get_host_param		= cxgb3i_host_get_param,
	.set_host_param		= cxgb3i_host_set_param,
	/* session management */
	.create_session		= cxgb3i_session_create,
	.destroy_session	= cxgb3i_session_destroy,
	.get_session_param	= iscsi_session_get_param,
	/* connection management */
	.create_conn		= cxgb3i_conn_create,
	.bind_conn		= cxgb3i_conn_bind,
	.destroy_conn		= iscsi_tcp_conn_teardown,
	.start_conn		= iscsi_conn_start,
	.stop_conn		= iscsi_conn_stop,
	.get_conn_param		= cxgb3i_conn_get_param,
	.set_param		= cxgb3i_conn_set_param,
	.get_stats		= cxgb3i_conn_get_stats,
	/* pdu xmit req. from user space */
	.send_pdu		= iscsi_conn_send_pdu,
	/* task */
	.init_task		= iscsi_tcp_task_init,
	.xmit_task		= iscsi_tcp_task_xmit,
	.cleanup_task		= cxgb3i_conn_cleanup_task,

	/* pdu */
	.alloc_pdu		= cxgb3i_conn_alloc_pdu,
	.init_pdu		= cxgb3i_conn_init_pdu,
	.xmit_pdu		= cxgb3i_conn_xmit_pdu,
	.parse_pdu_itt		= cxgb3i_parse_itt,

	/* TCP connect/disconnect */
	.ep_connect		= cxgb3i_ep_connect,
	.ep_poll		= cxgb3i_ep_poll,
	.ep_disconnect		= cxgb3i_ep_disconnect,
	/* Error recovery timeout call */
	.session_recovery_timedout = iscsi_session_recovery_timedout,
};

int cxgb3i_iscsi_init(void)
{
	sw_tag_idx_bits = (__ilog2_u32(ISCSI_ITT_MASK)) + 1;
	sw_tag_age_bits = (__ilog2_u32(ISCSI_AGE_MASK)) + 1;
	cxgb3i_log_info("tag itt 0x%x, %u bits, age 0x%x, %u bits.\n",
			ISCSI_ITT_MASK, sw_tag_idx_bits,
			ISCSI_AGE_MASK, sw_tag_age_bits);

	cxgb3i_scsi_transport =
	    iscsi_register_transport(&cxgb3i_iscsi_transport);
	if (!cxgb3i_scsi_transport) {
		cxgb3i_log_error("Could not register cxgb3i transport.\n");
		return -ENODEV;
	}
	cxgb3i_api_debug("cxgb3i transport 0x%p.\n", cxgb3i_scsi_transport);
	return 0;
}

void cxgb3i_iscsi_cleanup(void)
{
	if (cxgb3i_scsi_transport) {
		cxgb3i_api_debug("cxgb3i transport 0x%p.\n",
				 cxgb3i_scsi_transport);
		iscsi_unregister_transport(&cxgb3i_iscsi_transport);
	}
}
