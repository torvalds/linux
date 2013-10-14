/*******************************************************************************
 * This file contains main functions related to the iSCSI Target Core Driver.
 *
 * (c) Copyright 2007-2013 Datera, Inc.
 *
 * Author: Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 ******************************************************************************/

#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/crypto.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <asm/unaligned.h>
#include <scsi/scsi_device.h>
#include <scsi/iscsi_proto.h>
#include <scsi/scsi_tcq.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_configfs.h>

#include "iscsi_target_core.h"
#include "iscsi_target_parameters.h"
#include "iscsi_target_seq_pdu_list.h"
#include "iscsi_target_tq.h"
#include "iscsi_target_configfs.h"
#include "iscsi_target_datain_values.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_erl1.h"
#include "iscsi_target_erl2.h"
#include "iscsi_target_login.h"
#include "iscsi_target_tmr.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"
#include "iscsi_target_device.h"
#include "iscsi_target_stat.h"

#include <target/iscsi/iscsi_transport.h>

static LIST_HEAD(g_tiqn_list);
static LIST_HEAD(g_np_list);
static DEFINE_SPINLOCK(tiqn_lock);
static DEFINE_SPINLOCK(np_lock);

static struct idr tiqn_idr;
struct idr sess_idr;
struct mutex auth_id_lock;
spinlock_t sess_idr_lock;

struct iscsit_global *iscsit_global;

struct kmem_cache *lio_qr_cache;
struct kmem_cache *lio_dr_cache;
struct kmem_cache *lio_ooo_cache;
struct kmem_cache *lio_r2t_cache;

static int iscsit_handle_immediate_data(struct iscsi_cmd *,
			struct iscsi_scsi_req *, u32);

struct iscsi_tiqn *iscsit_get_tiqn_for_login(unsigned char *buf)
{
	struct iscsi_tiqn *tiqn = NULL;

	spin_lock(&tiqn_lock);
	list_for_each_entry(tiqn, &g_tiqn_list, tiqn_list) {
		if (!strcmp(tiqn->tiqn, buf)) {

			spin_lock(&tiqn->tiqn_state_lock);
			if (tiqn->tiqn_state == TIQN_STATE_ACTIVE) {
				tiqn->tiqn_access_count++;
				spin_unlock(&tiqn->tiqn_state_lock);
				spin_unlock(&tiqn_lock);
				return tiqn;
			}
			spin_unlock(&tiqn->tiqn_state_lock);
		}
	}
	spin_unlock(&tiqn_lock);

	return NULL;
}

static int iscsit_set_tiqn_shutdown(struct iscsi_tiqn *tiqn)
{
	spin_lock(&tiqn->tiqn_state_lock);
	if (tiqn->tiqn_state == TIQN_STATE_ACTIVE) {
		tiqn->tiqn_state = TIQN_STATE_SHUTDOWN;
		spin_unlock(&tiqn->tiqn_state_lock);
		return 0;
	}
	spin_unlock(&tiqn->tiqn_state_lock);

	return -1;
}

void iscsit_put_tiqn_for_login(struct iscsi_tiqn *tiqn)
{
	spin_lock(&tiqn->tiqn_state_lock);
	tiqn->tiqn_access_count--;
	spin_unlock(&tiqn->tiqn_state_lock);
}

/*
 * Note that IQN formatting is expected to be done in userspace, and
 * no explict IQN format checks are done here.
 */
struct iscsi_tiqn *iscsit_add_tiqn(unsigned char *buf)
{
	struct iscsi_tiqn *tiqn = NULL;
	int ret;

	if (strlen(buf) >= ISCSI_IQN_LEN) {
		pr_err("Target IQN exceeds %d bytes\n",
				ISCSI_IQN_LEN);
		return ERR_PTR(-EINVAL);
	}

	tiqn = kzalloc(sizeof(struct iscsi_tiqn), GFP_KERNEL);
	if (!tiqn) {
		pr_err("Unable to allocate struct iscsi_tiqn\n");
		return ERR_PTR(-ENOMEM);
	}

	sprintf(tiqn->tiqn, "%s", buf);
	INIT_LIST_HEAD(&tiqn->tiqn_list);
	INIT_LIST_HEAD(&tiqn->tiqn_tpg_list);
	spin_lock_init(&tiqn->tiqn_state_lock);
	spin_lock_init(&tiqn->tiqn_tpg_lock);
	spin_lock_init(&tiqn->sess_err_stats.lock);
	spin_lock_init(&tiqn->login_stats.lock);
	spin_lock_init(&tiqn->logout_stats.lock);

	tiqn->tiqn_state = TIQN_STATE_ACTIVE;

	idr_preload(GFP_KERNEL);
	spin_lock(&tiqn_lock);

	ret = idr_alloc(&tiqn_idr, NULL, 0, 0, GFP_NOWAIT);
	if (ret < 0) {
		pr_err("idr_alloc() failed for tiqn->tiqn_index\n");
		spin_unlock(&tiqn_lock);
		idr_preload_end();
		kfree(tiqn);
		return ERR_PTR(ret);
	}
	tiqn->tiqn_index = ret;
	list_add_tail(&tiqn->tiqn_list, &g_tiqn_list);

	spin_unlock(&tiqn_lock);
	idr_preload_end();

	pr_debug("CORE[0] - Added iSCSI Target IQN: %s\n", tiqn->tiqn);

	return tiqn;

}

static void iscsit_wait_for_tiqn(struct iscsi_tiqn *tiqn)
{
	/*
	 * Wait for accesses to said struct iscsi_tiqn to end.
	 */
	spin_lock(&tiqn->tiqn_state_lock);
	while (tiqn->tiqn_access_count != 0) {
		spin_unlock(&tiqn->tiqn_state_lock);
		msleep(10);
		spin_lock(&tiqn->tiqn_state_lock);
	}
	spin_unlock(&tiqn->tiqn_state_lock);
}

void iscsit_del_tiqn(struct iscsi_tiqn *tiqn)
{
	/*
	 * iscsit_set_tiqn_shutdown sets tiqn->tiqn_state = TIQN_STATE_SHUTDOWN
	 * while holding tiqn->tiqn_state_lock.  This means that all subsequent
	 * attempts to access this struct iscsi_tiqn will fail from both transport
	 * fabric and control code paths.
	 */
	if (iscsit_set_tiqn_shutdown(tiqn) < 0) {
		pr_err("iscsit_set_tiqn_shutdown() failed\n");
		return;
	}

	iscsit_wait_for_tiqn(tiqn);

	spin_lock(&tiqn_lock);
	list_del(&tiqn->tiqn_list);
	idr_remove(&tiqn_idr, tiqn->tiqn_index);
	spin_unlock(&tiqn_lock);

	pr_debug("CORE[0] - Deleted iSCSI Target IQN: %s\n",
			tiqn->tiqn);
	kfree(tiqn);
}

int iscsit_access_np(struct iscsi_np *np, struct iscsi_portal_group *tpg)
{
	int ret;
	/*
	 * Determine if the network portal is accepting storage traffic.
	 */
	spin_lock_bh(&np->np_thread_lock);
	if (np->np_thread_state != ISCSI_NP_THREAD_ACTIVE) {
		spin_unlock_bh(&np->np_thread_lock);
		return -1;
	}
	spin_unlock_bh(&np->np_thread_lock);
	/*
	 * Determine if the portal group is accepting storage traffic.
	 */
	spin_lock_bh(&tpg->tpg_state_lock);
	if (tpg->tpg_state != TPG_STATE_ACTIVE) {
		spin_unlock_bh(&tpg->tpg_state_lock);
		return -1;
	}
	spin_unlock_bh(&tpg->tpg_state_lock);

	/*
	 * Here we serialize access across the TIQN+TPG Tuple.
	 */
	ret = down_interruptible(&tpg->np_login_sem);
	if ((ret != 0) || signal_pending(current))
		return -1;

	spin_lock_bh(&tpg->tpg_state_lock);
	if (tpg->tpg_state != TPG_STATE_ACTIVE) {
		spin_unlock_bh(&tpg->tpg_state_lock);
		up(&tpg->np_login_sem);
		return -1;
	}
	spin_unlock_bh(&tpg->tpg_state_lock);

	return 0;
}

void iscsit_login_kref_put(struct kref *kref)
{
	struct iscsi_tpg_np *tpg_np = container_of(kref,
				struct iscsi_tpg_np, tpg_np_kref);

	complete(&tpg_np->tpg_np_comp);
}

int iscsit_deaccess_np(struct iscsi_np *np, struct iscsi_portal_group *tpg,
		       struct iscsi_tpg_np *tpg_np)
{
	struct iscsi_tiqn *tiqn = tpg->tpg_tiqn;

	up(&tpg->np_login_sem);

	if (tpg_np)
		kref_put(&tpg_np->tpg_np_kref, iscsit_login_kref_put);

	if (tiqn)
		iscsit_put_tiqn_for_login(tiqn);

	return 0;
}

bool iscsit_check_np_match(
	struct __kernel_sockaddr_storage *sockaddr,
	struct iscsi_np *np,
	int network_transport)
{
	struct sockaddr_in *sock_in, *sock_in_e;
	struct sockaddr_in6 *sock_in6, *sock_in6_e;
	bool ip_match = false;
	u16 port;

	if (sockaddr->ss_family == AF_INET6) {
		sock_in6 = (struct sockaddr_in6 *)sockaddr;
		sock_in6_e = (struct sockaddr_in6 *)&np->np_sockaddr;

		if (!memcmp(&sock_in6->sin6_addr.in6_u,
			    &sock_in6_e->sin6_addr.in6_u,
			    sizeof(struct in6_addr)))
			ip_match = true;

		port = ntohs(sock_in6->sin6_port);
	} else {
		sock_in = (struct sockaddr_in *)sockaddr;
		sock_in_e = (struct sockaddr_in *)&np->np_sockaddr;

		if (sock_in->sin_addr.s_addr == sock_in_e->sin_addr.s_addr)
			ip_match = true;

		port = ntohs(sock_in->sin_port);
	}

	if ((ip_match == true) && (np->np_port == port) &&
	    (np->np_network_transport == network_transport))
		return true;

	return false;
}

static struct iscsi_np *iscsit_get_np(
	struct __kernel_sockaddr_storage *sockaddr,
	int network_transport)
{
	struct iscsi_np *np;
	bool match;

	spin_lock_bh(&np_lock);
	list_for_each_entry(np, &g_np_list, np_list) {
		spin_lock(&np->np_thread_lock);
		if (np->np_thread_state != ISCSI_NP_THREAD_ACTIVE) {
			spin_unlock(&np->np_thread_lock);
			continue;
		}

		match = iscsit_check_np_match(sockaddr, np, network_transport);
		if (match == true) {
			/*
			 * Increment the np_exports reference count now to
			 * prevent iscsit_del_np() below from being called
			 * while iscsi_tpg_add_network_portal() is called.
			 */
			np->np_exports++;
			spin_unlock(&np->np_thread_lock);
			spin_unlock_bh(&np_lock);
			return np;
		}
		spin_unlock(&np->np_thread_lock);
	}
	spin_unlock_bh(&np_lock);

	return NULL;
}

struct iscsi_np *iscsit_add_np(
	struct __kernel_sockaddr_storage *sockaddr,
	char *ip_str,
	int network_transport)
{
	struct sockaddr_in *sock_in;
	struct sockaddr_in6 *sock_in6;
	struct iscsi_np *np;
	int ret;
	/*
	 * Locate the existing struct iscsi_np if already active..
	 */
	np = iscsit_get_np(sockaddr, network_transport);
	if (np)
		return np;

	np = kzalloc(sizeof(struct iscsi_np), GFP_KERNEL);
	if (!np) {
		pr_err("Unable to allocate memory for struct iscsi_np\n");
		return ERR_PTR(-ENOMEM);
	}

	np->np_flags |= NPF_IP_NETWORK;
	if (sockaddr->ss_family == AF_INET6) {
		sock_in6 = (struct sockaddr_in6 *)sockaddr;
		snprintf(np->np_ip, IPV6_ADDRESS_SPACE, "%s", ip_str);
		np->np_port = ntohs(sock_in6->sin6_port);
	} else {
		sock_in = (struct sockaddr_in *)sockaddr;
		sprintf(np->np_ip, "%s", ip_str);
		np->np_port = ntohs(sock_in->sin_port);
	}

	np->np_network_transport = network_transport;
	spin_lock_init(&np->np_thread_lock);
	init_completion(&np->np_restart_comp);
	INIT_LIST_HEAD(&np->np_list);

	ret = iscsi_target_setup_login_socket(np, sockaddr);
	if (ret != 0) {
		kfree(np);
		return ERR_PTR(ret);
	}

	np->np_thread = kthread_run(iscsi_target_login_thread, np, "iscsi_np");
	if (IS_ERR(np->np_thread)) {
		pr_err("Unable to create kthread: iscsi_np\n");
		ret = PTR_ERR(np->np_thread);
		kfree(np);
		return ERR_PTR(ret);
	}
	/*
	 * Increment the np_exports reference count now to prevent
	 * iscsit_del_np() below from being run while a new call to
	 * iscsi_tpg_add_network_portal() for a matching iscsi_np is
	 * active.  We don't need to hold np->np_thread_lock at this
	 * point because iscsi_np has not been added to g_np_list yet.
	 */
	np->np_exports = 1;

	spin_lock_bh(&np_lock);
	list_add_tail(&np->np_list, &g_np_list);
	spin_unlock_bh(&np_lock);

	pr_debug("CORE[0] - Added Network Portal: %s:%hu on %s\n",
		np->np_ip, np->np_port, np->np_transport->name);

	return np;
}

int iscsit_reset_np_thread(
	struct iscsi_np *np,
	struct iscsi_tpg_np *tpg_np,
	struct iscsi_portal_group *tpg,
	bool shutdown)
{
	spin_lock_bh(&np->np_thread_lock);
	if (np->np_thread_state == ISCSI_NP_THREAD_INACTIVE) {
		spin_unlock_bh(&np->np_thread_lock);
		return 0;
	}
	np->np_thread_state = ISCSI_NP_THREAD_RESET;

	if (np->np_thread) {
		spin_unlock_bh(&np->np_thread_lock);
		send_sig(SIGINT, np->np_thread, 1);
		wait_for_completion(&np->np_restart_comp);
		spin_lock_bh(&np->np_thread_lock);
	}
	spin_unlock_bh(&np->np_thread_lock);

	if (tpg_np && shutdown) {
		kref_put(&tpg_np->tpg_np_kref, iscsit_login_kref_put);

		wait_for_completion(&tpg_np->tpg_np_comp);
	}

	return 0;
}

static void iscsit_free_np(struct iscsi_np *np)
{
	if (np->np_socket)
		sock_release(np->np_socket);
}

int iscsit_del_np(struct iscsi_np *np)
{
	spin_lock_bh(&np->np_thread_lock);
	np->np_exports--;
	if (np->np_exports) {
		spin_unlock_bh(&np->np_thread_lock);
		return 0;
	}
	np->np_thread_state = ISCSI_NP_THREAD_SHUTDOWN;
	spin_unlock_bh(&np->np_thread_lock);

	if (np->np_thread) {
		/*
		 * We need to send the signal to wakeup Linux/Net
		 * which may be sleeping in sock_accept()..
		 */
		send_sig(SIGINT, np->np_thread, 1);
		kthread_stop(np->np_thread);
	}

	np->np_transport->iscsit_free_np(np);

	spin_lock_bh(&np_lock);
	list_del(&np->np_list);
	spin_unlock_bh(&np_lock);

	pr_debug("CORE[0] - Removed Network Portal: %s:%hu on %s\n",
		np->np_ip, np->np_port, np->np_transport->name);

	iscsit_put_transport(np->np_transport);
	kfree(np);
	return 0;
}

static int iscsit_immediate_queue(struct iscsi_conn *, struct iscsi_cmd *, int);
static int iscsit_response_queue(struct iscsi_conn *, struct iscsi_cmd *, int);

static int iscsit_queue_rsp(struct iscsi_conn *conn, struct iscsi_cmd *cmd)
{
	iscsit_add_cmd_to_response_queue(cmd, cmd->conn, cmd->i_state);
	return 0;
}

static struct iscsit_transport iscsi_target_transport = {
	.name			= "iSCSI/TCP",
	.transport_type		= ISCSI_TCP,
	.owner			= NULL,
	.iscsit_setup_np	= iscsit_setup_np,
	.iscsit_accept_np	= iscsit_accept_np,
	.iscsit_free_np		= iscsit_free_np,
	.iscsit_get_login_rx	= iscsit_get_login_rx,
	.iscsit_put_login_tx	= iscsit_put_login_tx,
	.iscsit_get_dataout	= iscsit_build_r2ts_for_cmd,
	.iscsit_immediate_queue	= iscsit_immediate_queue,
	.iscsit_response_queue	= iscsit_response_queue,
	.iscsit_queue_data_in	= iscsit_queue_rsp,
	.iscsit_queue_status	= iscsit_queue_rsp,
};

static int __init iscsi_target_init_module(void)
{
	int ret = 0;

	pr_debug("iSCSI-Target "ISCSIT_VERSION"\n");

	iscsit_global = kzalloc(sizeof(struct iscsit_global), GFP_KERNEL);
	if (!iscsit_global) {
		pr_err("Unable to allocate memory for iscsit_global\n");
		return -1;
	}
	mutex_init(&auth_id_lock);
	spin_lock_init(&sess_idr_lock);
	idr_init(&tiqn_idr);
	idr_init(&sess_idr);

	ret = iscsi_target_register_configfs();
	if (ret < 0)
		goto out;

	ret = iscsi_thread_set_init();
	if (ret < 0)
		goto configfs_out;

	if (iscsi_allocate_thread_sets(TARGET_THREAD_SET_COUNT) !=
			TARGET_THREAD_SET_COUNT) {
		pr_err("iscsi_allocate_thread_sets() returned"
			" unexpected value!\n");
		goto ts_out1;
	}

	lio_qr_cache = kmem_cache_create("lio_qr_cache",
			sizeof(struct iscsi_queue_req),
			__alignof__(struct iscsi_queue_req), 0, NULL);
	if (!lio_qr_cache) {
		pr_err("nable to kmem_cache_create() for"
				" lio_qr_cache\n");
		goto ts_out2;
	}

	lio_dr_cache = kmem_cache_create("lio_dr_cache",
			sizeof(struct iscsi_datain_req),
			__alignof__(struct iscsi_datain_req), 0, NULL);
	if (!lio_dr_cache) {
		pr_err("Unable to kmem_cache_create() for"
				" lio_dr_cache\n");
		goto qr_out;
	}

	lio_ooo_cache = kmem_cache_create("lio_ooo_cache",
			sizeof(struct iscsi_ooo_cmdsn),
			__alignof__(struct iscsi_ooo_cmdsn), 0, NULL);
	if (!lio_ooo_cache) {
		pr_err("Unable to kmem_cache_create() for"
				" lio_ooo_cache\n");
		goto dr_out;
	}

	lio_r2t_cache = kmem_cache_create("lio_r2t_cache",
			sizeof(struct iscsi_r2t), __alignof__(struct iscsi_r2t),
			0, NULL);
	if (!lio_r2t_cache) {
		pr_err("Unable to kmem_cache_create() for"
				" lio_r2t_cache\n");
		goto ooo_out;
	}

	iscsit_register_transport(&iscsi_target_transport);

	if (iscsit_load_discovery_tpg() < 0)
		goto r2t_out;

	return ret;
r2t_out:
	kmem_cache_destroy(lio_r2t_cache);
ooo_out:
	kmem_cache_destroy(lio_ooo_cache);
dr_out:
	kmem_cache_destroy(lio_dr_cache);
qr_out:
	kmem_cache_destroy(lio_qr_cache);
ts_out2:
	iscsi_deallocate_thread_sets();
ts_out1:
	iscsi_thread_set_free();
configfs_out:
	iscsi_target_deregister_configfs();
out:
	kfree(iscsit_global);
	return -ENOMEM;
}

static void __exit iscsi_target_cleanup_module(void)
{
	iscsi_deallocate_thread_sets();
	iscsi_thread_set_free();
	iscsit_release_discovery_tpg();
	iscsit_unregister_transport(&iscsi_target_transport);
	kmem_cache_destroy(lio_qr_cache);
	kmem_cache_destroy(lio_dr_cache);
	kmem_cache_destroy(lio_ooo_cache);
	kmem_cache_destroy(lio_r2t_cache);

	iscsi_target_deregister_configfs();

	kfree(iscsit_global);
}

static int iscsit_add_reject(
	struct iscsi_conn *conn,
	u8 reason,
	unsigned char *buf)
{
	struct iscsi_cmd *cmd;

	cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
	if (!cmd)
		return -1;

	cmd->iscsi_opcode = ISCSI_OP_REJECT;
	cmd->reject_reason = reason;

	cmd->buf_ptr = kmemdup(buf, ISCSI_HDR_LEN, GFP_KERNEL);
	if (!cmd->buf_ptr) {
		pr_err("Unable to allocate memory for cmd->buf_ptr\n");
		iscsit_free_cmd(cmd, false);
		return -1;
	}

	spin_lock_bh(&conn->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
	spin_unlock_bh(&conn->cmd_lock);

	cmd->i_state = ISTATE_SEND_REJECT;
	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);

	return -1;
}

static int iscsit_add_reject_from_cmd(
	struct iscsi_cmd *cmd,
	u8 reason,
	bool add_to_conn,
	unsigned char *buf)
{
	struct iscsi_conn *conn;

	if (!cmd->conn) {
		pr_err("cmd->conn is NULL for ITT: 0x%08x\n",
				cmd->init_task_tag);
		return -1;
	}
	conn = cmd->conn;

	cmd->iscsi_opcode = ISCSI_OP_REJECT;
	cmd->reject_reason = reason;

	cmd->buf_ptr = kmemdup(buf, ISCSI_HDR_LEN, GFP_KERNEL);
	if (!cmd->buf_ptr) {
		pr_err("Unable to allocate memory for cmd->buf_ptr\n");
		iscsit_free_cmd(cmd, false);
		return -1;
	}

	if (add_to_conn) {
		spin_lock_bh(&conn->cmd_lock);
		list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
		spin_unlock_bh(&conn->cmd_lock);
	}

	cmd->i_state = ISTATE_SEND_REJECT;
	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
	/*
	 * Perform the kref_put now if se_cmd has already been setup by
	 * scsit_setup_scsi_cmd()
	 */
	if (cmd->se_cmd.se_tfo != NULL) {
		pr_debug("iscsi reject: calling target_put_sess_cmd >>>>>>\n");
		target_put_sess_cmd(conn->sess->se_sess, &cmd->se_cmd);
	}
	return -1;
}

static int iscsit_add_reject_cmd(struct iscsi_cmd *cmd, u8 reason,
				 unsigned char *buf)
{
	return iscsit_add_reject_from_cmd(cmd, reason, true, buf);
}

int iscsit_reject_cmd(struct iscsi_cmd *cmd, u8 reason, unsigned char *buf)
{
	return iscsit_add_reject_from_cmd(cmd, reason, false, buf);
}

/*
 * Map some portion of the allocated scatterlist to an iovec, suitable for
 * kernel sockets to copy data in/out.
 */
static int iscsit_map_iovec(
	struct iscsi_cmd *cmd,
	struct kvec *iov,
	u32 data_offset,
	u32 data_length)
{
	u32 i = 0;
	struct scatterlist *sg;
	unsigned int page_off;

	/*
	 * We know each entry in t_data_sg contains a page.
	 */
	sg = &cmd->se_cmd.t_data_sg[data_offset / PAGE_SIZE];
	page_off = (data_offset % PAGE_SIZE);

	cmd->first_data_sg = sg;
	cmd->first_data_sg_off = page_off;

	while (data_length) {
		u32 cur_len = min_t(u32, data_length, sg->length - page_off);

		iov[i].iov_base = kmap(sg_page(sg)) + sg->offset + page_off;
		iov[i].iov_len = cur_len;

		data_length -= cur_len;
		page_off = 0;
		sg = sg_next(sg);
		i++;
	}

	cmd->kmapped_nents = i;

	return i;
}

static void iscsit_unmap_iovec(struct iscsi_cmd *cmd)
{
	u32 i;
	struct scatterlist *sg;

	sg = cmd->first_data_sg;

	for (i = 0; i < cmd->kmapped_nents; i++)
		kunmap(sg_page(&sg[i]));
}

static void iscsit_ack_from_expstatsn(struct iscsi_conn *conn, u32 exp_statsn)
{
	LIST_HEAD(ack_list);
	struct iscsi_cmd *cmd, *cmd_p;

	conn->exp_statsn = exp_statsn;

	if (conn->sess->sess_ops->RDMAExtensions)
		return;

	spin_lock_bh(&conn->cmd_lock);
	list_for_each_entry_safe(cmd, cmd_p, &conn->conn_cmd_list, i_conn_node) {
		spin_lock(&cmd->istate_lock);
		if ((cmd->i_state == ISTATE_SENT_STATUS) &&
		    iscsi_sna_lt(cmd->stat_sn, exp_statsn)) {
			cmd->i_state = ISTATE_REMOVE;
			spin_unlock(&cmd->istate_lock);
			list_move_tail(&cmd->i_conn_node, &ack_list);
			continue;
		}
		spin_unlock(&cmd->istate_lock);
	}
	spin_unlock_bh(&conn->cmd_lock);

	list_for_each_entry_safe(cmd, cmd_p, &ack_list, i_conn_node) {
		list_del(&cmd->i_conn_node);
		iscsit_free_cmd(cmd, false);
	}
}

static int iscsit_allocate_iovecs(struct iscsi_cmd *cmd)
{
	u32 iov_count = max(1UL, DIV_ROUND_UP(cmd->se_cmd.data_length, PAGE_SIZE));

	iov_count += ISCSI_IOV_DATA_BUFFER;

	cmd->iov_data = kzalloc(iov_count * sizeof(struct kvec), GFP_KERNEL);
	if (!cmd->iov_data) {
		pr_err("Unable to allocate cmd->iov_data\n");
		return -ENOMEM;
	}

	cmd->orig_iov_data_count = iov_count;
	return 0;
}

int iscsit_setup_scsi_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			  unsigned char *buf)
{
	int data_direction, payload_length;
	struct iscsi_scsi_req *hdr;
	int iscsi_task_attr;
	int sam_task_attr;

	spin_lock_bh(&conn->sess->session_stats_lock);
	conn->sess->cmd_pdus++;
	if (conn->sess->se_sess->se_node_acl) {
		spin_lock(&conn->sess->se_sess->se_node_acl->stats_lock);
		conn->sess->se_sess->se_node_acl->num_cmds++;
		spin_unlock(&conn->sess->se_sess->se_node_acl->stats_lock);
	}
	spin_unlock_bh(&conn->sess->session_stats_lock);

	hdr			= (struct iscsi_scsi_req *) buf;
	payload_length		= ntoh24(hdr->dlength);

	/* FIXME; Add checks for AdditionalHeaderSegment */

	if (!(hdr->flags & ISCSI_FLAG_CMD_WRITE) &&
	    !(hdr->flags & ISCSI_FLAG_CMD_FINAL)) {
		pr_err("ISCSI_FLAG_CMD_WRITE & ISCSI_FLAG_CMD_FINAL"
				" not set. Bad iSCSI Initiator.\n");
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_BOOKMARK_INVALID, buf);
	}

	if (((hdr->flags & ISCSI_FLAG_CMD_READ) ||
	     (hdr->flags & ISCSI_FLAG_CMD_WRITE)) && !hdr->data_length) {
		/*
		 * Vmware ESX v3.0 uses a modified Cisco Initiator (v3.4.2)
		 * that adds support for RESERVE/RELEASE.  There is a bug
		 * add with this new functionality that sets R/W bits when
		 * neither CDB carries any READ or WRITE datapayloads.
		 */
		if ((hdr->cdb[0] == 0x16) || (hdr->cdb[0] == 0x17)) {
			hdr->flags &= ~ISCSI_FLAG_CMD_READ;
			hdr->flags &= ~ISCSI_FLAG_CMD_WRITE;
			goto done;
		}

		pr_err("ISCSI_FLAG_CMD_READ or ISCSI_FLAG_CMD_WRITE"
			" set when Expected Data Transfer Length is 0 for"
			" CDB: 0x%02x. Bad iSCSI Initiator.\n", hdr->cdb[0]);
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_BOOKMARK_INVALID, buf);
	}
done:

	if (!(hdr->flags & ISCSI_FLAG_CMD_READ) &&
	    !(hdr->flags & ISCSI_FLAG_CMD_WRITE) && (hdr->data_length != 0)) {
		pr_err("ISCSI_FLAG_CMD_READ and/or ISCSI_FLAG_CMD_WRITE"
			" MUST be set if Expected Data Transfer Length is not 0."
			" Bad iSCSI Initiator\n");
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_BOOKMARK_INVALID, buf);
	}

	if ((hdr->flags & ISCSI_FLAG_CMD_READ) &&
	    (hdr->flags & ISCSI_FLAG_CMD_WRITE)) {
		pr_err("Bidirectional operations not supported!\n");
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_BOOKMARK_INVALID, buf);
	}

	if (hdr->opcode & ISCSI_OP_IMMEDIATE) {
		pr_err("Illegally set Immediate Bit in iSCSI Initiator"
				" Scsi Command PDU.\n");
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_BOOKMARK_INVALID, buf);
	}

	if (payload_length && !conn->sess->sess_ops->ImmediateData) {
		pr_err("ImmediateData=No but DataSegmentLength=%u,"
			" protocol error.\n", payload_length);
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_PROTOCOL_ERROR, buf);
	}

	if ((be32_to_cpu(hdr->data_length) == payload_length) &&
	    (!(hdr->flags & ISCSI_FLAG_CMD_FINAL))) {
		pr_err("Expected Data Transfer Length and Length of"
			" Immediate Data are the same, but ISCSI_FLAG_CMD_FINAL"
			" bit is not set protocol error\n");
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_PROTOCOL_ERROR, buf);
	}

	if (payload_length > be32_to_cpu(hdr->data_length)) {
		pr_err("DataSegmentLength: %u is greater than"
			" EDTL: %u, protocol error.\n", payload_length,
				hdr->data_length);
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_PROTOCOL_ERROR, buf);
	}

	if (payload_length > conn->conn_ops->MaxXmitDataSegmentLength) {
		pr_err("DataSegmentLength: %u is greater than"
			" MaxXmitDataSegmentLength: %u, protocol error.\n",
			payload_length, conn->conn_ops->MaxXmitDataSegmentLength);
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_PROTOCOL_ERROR, buf);
	}

	if (payload_length > conn->sess->sess_ops->FirstBurstLength) {
		pr_err("DataSegmentLength: %u is greater than"
			" FirstBurstLength: %u, protocol error.\n",
			payload_length, conn->sess->sess_ops->FirstBurstLength);
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_BOOKMARK_INVALID, buf);
	}

	data_direction = (hdr->flags & ISCSI_FLAG_CMD_WRITE) ? DMA_TO_DEVICE :
			 (hdr->flags & ISCSI_FLAG_CMD_READ) ? DMA_FROM_DEVICE :
			  DMA_NONE;

	cmd->data_direction = data_direction;
	iscsi_task_attr = hdr->flags & ISCSI_FLAG_CMD_ATTR_MASK;
	/*
	 * Figure out the SAM Task Attribute for the incoming SCSI CDB
	 */
	if ((iscsi_task_attr == ISCSI_ATTR_UNTAGGED) ||
	    (iscsi_task_attr == ISCSI_ATTR_SIMPLE))
		sam_task_attr = MSG_SIMPLE_TAG;
	else if (iscsi_task_attr == ISCSI_ATTR_ORDERED)
		sam_task_attr = MSG_ORDERED_TAG;
	else if (iscsi_task_attr == ISCSI_ATTR_HEAD_OF_QUEUE)
		sam_task_attr = MSG_HEAD_TAG;
	else if (iscsi_task_attr == ISCSI_ATTR_ACA)
		sam_task_attr = MSG_ACA_TAG;
	else {
		pr_debug("Unknown iSCSI Task Attribute: 0x%02x, using"
			" MSG_SIMPLE_TAG\n", iscsi_task_attr);
		sam_task_attr = MSG_SIMPLE_TAG;
	}

	cmd->iscsi_opcode	= ISCSI_OP_SCSI_CMD;
	cmd->i_state		= ISTATE_NEW_CMD;
	cmd->immediate_cmd	= ((hdr->opcode & ISCSI_OP_IMMEDIATE) ? 1 : 0);
	cmd->immediate_data	= (payload_length) ? 1 : 0;
	cmd->unsolicited_data	= ((!(hdr->flags & ISCSI_FLAG_CMD_FINAL) &&
				     (hdr->flags & ISCSI_FLAG_CMD_WRITE)) ? 1 : 0);
	if (cmd->unsolicited_data)
		cmd->cmd_flags |= ICF_NON_IMMEDIATE_UNSOLICITED_DATA;

	conn->sess->init_task_tag = cmd->init_task_tag = hdr->itt;
	if (hdr->flags & ISCSI_FLAG_CMD_READ) {
		spin_lock_bh(&conn->sess->ttt_lock);
		cmd->targ_xfer_tag = conn->sess->targ_xfer_tag++;
		if (cmd->targ_xfer_tag == 0xFFFFFFFF)
			cmd->targ_xfer_tag = conn->sess->targ_xfer_tag++;
		spin_unlock_bh(&conn->sess->ttt_lock);
	} else if (hdr->flags & ISCSI_FLAG_CMD_WRITE)
		cmd->targ_xfer_tag = 0xFFFFFFFF;
	cmd->cmd_sn		= be32_to_cpu(hdr->cmdsn);
	cmd->exp_stat_sn	= be32_to_cpu(hdr->exp_statsn);
	cmd->first_burst_len	= payload_length;

	if (!conn->sess->sess_ops->RDMAExtensions &&
	     cmd->data_direction == DMA_FROM_DEVICE) {
		struct iscsi_datain_req *dr;

		dr = iscsit_allocate_datain_req();
		if (!dr)
			return iscsit_add_reject_cmd(cmd,
					ISCSI_REASON_BOOKMARK_NO_RESOURCES, buf);

		iscsit_attach_datain_req(cmd, dr);
	}

	/*
	 * Initialize struct se_cmd descriptor from target_core_mod infrastructure
	 */
	transport_init_se_cmd(&cmd->se_cmd, &lio_target_fabric_configfs->tf_ops,
			conn->sess->se_sess, be32_to_cpu(hdr->data_length),
			cmd->data_direction, sam_task_attr,
			cmd->sense_buffer + 2);

	pr_debug("Got SCSI Command, ITT: 0x%08x, CmdSN: 0x%08x,"
		" ExpXferLen: %u, Length: %u, CID: %hu\n", hdr->itt,
		hdr->cmdsn, be32_to_cpu(hdr->data_length), payload_length,
		conn->cid);

	target_get_sess_cmd(conn->sess->se_sess, &cmd->se_cmd, true);

	cmd->sense_reason = transport_lookup_cmd_lun(&cmd->se_cmd,
						     scsilun_to_int(&hdr->lun));
	if (cmd->sense_reason)
		goto attach_cmd;

	cmd->sense_reason = target_setup_cmd_from_cdb(&cmd->se_cmd, hdr->cdb);
	if (cmd->sense_reason) {
		if (cmd->sense_reason == TCM_OUT_OF_RESOURCES) {
			return iscsit_add_reject_cmd(cmd,
					ISCSI_REASON_BOOKMARK_NO_RESOURCES, buf);
		}

		goto attach_cmd;
	}

	if (iscsit_build_pdu_and_seq_lists(cmd, payload_length) < 0) {
		return iscsit_add_reject_cmd(cmd,
				ISCSI_REASON_BOOKMARK_NO_RESOURCES, buf);
	}

attach_cmd:
	spin_lock_bh(&conn->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
	spin_unlock_bh(&conn->cmd_lock);
	/*
	 * Check if we need to delay processing because of ALUA
	 * Active/NonOptimized primary access state..
	 */
	core_alua_check_nonop_delay(&cmd->se_cmd);

	return 0;
}
EXPORT_SYMBOL(iscsit_setup_scsi_cmd);

void iscsit_set_unsoliticed_dataout(struct iscsi_cmd *cmd)
{
	iscsit_set_dataout_sequence_values(cmd);

	spin_lock_bh(&cmd->dataout_timeout_lock);
	iscsit_start_dataout_timer(cmd, cmd->conn);
	spin_unlock_bh(&cmd->dataout_timeout_lock);
}
EXPORT_SYMBOL(iscsit_set_unsoliticed_dataout);

int iscsit_process_scsi_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			    struct iscsi_scsi_req *hdr)
{
	int cmdsn_ret = 0;
	/*
	 * Check the CmdSN against ExpCmdSN/MaxCmdSN here if
	 * the Immediate Bit is not set, and no Immediate
	 * Data is attached.
	 *
	 * A PDU/CmdSN carrying Immediate Data can only
	 * be processed after the DataCRC has passed.
	 * If the DataCRC fails, the CmdSN MUST NOT
	 * be acknowledged. (See below)
	 */
	if (!cmd->immediate_data) {
		cmdsn_ret = iscsit_sequence_cmd(conn, cmd,
					(unsigned char *)hdr, hdr->cmdsn);
		if (cmdsn_ret == CMDSN_ERROR_CANNOT_RECOVER)
			return -1;
		else if (cmdsn_ret == CMDSN_LOWER_THAN_EXP) {
			target_put_sess_cmd(conn->sess->se_sess, &cmd->se_cmd);
			return 0;
		}
	}

	iscsit_ack_from_expstatsn(conn, be32_to_cpu(hdr->exp_statsn));

	/*
	 * If no Immediate Data is attached, it's OK to return now.
	 */
	if (!cmd->immediate_data) {
		if (!cmd->sense_reason && cmd->unsolicited_data)
			iscsit_set_unsoliticed_dataout(cmd);
		if (!cmd->sense_reason)
			return 0;

		target_put_sess_cmd(conn->sess->se_sess, &cmd->se_cmd);
		return 0;
	}

	/*
	 * Early CHECK_CONDITIONs with ImmediateData never make it to command
	 * execution.  These exceptions are processed in CmdSN order using
	 * iscsit_check_received_cmdsn() in iscsit_get_immediate_data() below.
	 */
	if (cmd->sense_reason) {
		if (cmd->reject_reason)
			return 0;

		return 1;
	}
	/*
	 * Call directly into transport_generic_new_cmd() to perform
	 * the backend memory allocation.
	 */
	cmd->sense_reason = transport_generic_new_cmd(&cmd->se_cmd);
	if (cmd->sense_reason)
		return 1;

	return 0;
}
EXPORT_SYMBOL(iscsit_process_scsi_cmd);

static int
iscsit_get_immediate_data(struct iscsi_cmd *cmd, struct iscsi_scsi_req *hdr,
			  bool dump_payload)
{
	struct iscsi_conn *conn = cmd->conn;
	int cmdsn_ret = 0, immed_ret = IMMEDIATE_DATA_NORMAL_OPERATION;
	/*
	 * Special case for Unsupported SAM WRITE Opcodes and ImmediateData=Yes.
	 */
	if (dump_payload == true)
		goto after_immediate_data;

	immed_ret = iscsit_handle_immediate_data(cmd, hdr,
					cmd->first_burst_len);
after_immediate_data:
	if (immed_ret == IMMEDIATE_DATA_NORMAL_OPERATION) {
		/*
		 * A PDU/CmdSN carrying Immediate Data passed
		 * DataCRC, check against ExpCmdSN/MaxCmdSN if
		 * Immediate Bit is not set.
		 */
		cmdsn_ret = iscsit_sequence_cmd(cmd->conn, cmd,
					(unsigned char *)hdr, hdr->cmdsn);
		if (cmdsn_ret == CMDSN_ERROR_CANNOT_RECOVER)
			return -1;

		if (cmd->sense_reason || cmdsn_ret == CMDSN_LOWER_THAN_EXP) {
			int rc;

			rc = iscsit_dump_data_payload(cmd->conn,
						      cmd->first_burst_len, 1);
			target_put_sess_cmd(conn->sess->se_sess, &cmd->se_cmd);
			return rc;
		} else if (cmd->unsolicited_data)
			iscsit_set_unsoliticed_dataout(cmd);

	} else if (immed_ret == IMMEDIATE_DATA_ERL1_CRC_FAILURE) {
		/*
		 * Immediate Data failed DataCRC and ERL>=1,
		 * silently drop this PDU and let the initiator
		 * plug the CmdSN gap.
		 *
		 * FIXME: Send Unsolicited NOPIN with reserved
		 * TTT here to help the initiator figure out
		 * the missing CmdSN, although they should be
		 * intelligent enough to determine the missing
		 * CmdSN and issue a retry to plug the sequence.
		 */
		cmd->i_state = ISTATE_REMOVE;
		iscsit_add_cmd_to_immediate_queue(cmd, cmd->conn, cmd->i_state);
	} else /* immed_ret == IMMEDIATE_DATA_CANNOT_RECOVER */
		return -1;

	return 0;
}

static int
iscsit_handle_scsi_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			   unsigned char *buf)
{
	struct iscsi_scsi_req *hdr = (struct iscsi_scsi_req *)buf;
	int rc, immed_data;
	bool dump_payload = false;

	rc = iscsit_setup_scsi_cmd(conn, cmd, buf);
	if (rc < 0)
		return 0;
	/*
	 * Allocation iovecs needed for struct socket operations for
	 * traditional iSCSI block I/O.
	 */
	if (iscsit_allocate_iovecs(cmd) < 0) {
		return iscsit_add_reject_cmd(cmd,
				ISCSI_REASON_BOOKMARK_NO_RESOURCES, buf);
	}
	immed_data = cmd->immediate_data;

	rc = iscsit_process_scsi_cmd(conn, cmd, hdr);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		dump_payload = true;

	if (!immed_data)
		return 0;

	return iscsit_get_immediate_data(cmd, hdr, dump_payload);
}

static u32 iscsit_do_crypto_hash_sg(
	struct hash_desc *hash,
	struct iscsi_cmd *cmd,
	u32 data_offset,
	u32 data_length,
	u32 padding,
	u8 *pad_bytes)
{
	u32 data_crc;
	u32 i;
	struct scatterlist *sg;
	unsigned int page_off;

	crypto_hash_init(hash);

	sg = cmd->first_data_sg;
	page_off = cmd->first_data_sg_off;

	i = 0;
	while (data_length) {
		u32 cur_len = min_t(u32, data_length, (sg[i].length - page_off));

		crypto_hash_update(hash, &sg[i], cur_len);

		data_length -= cur_len;
		page_off = 0;
		i++;
	}

	if (padding) {
		struct scatterlist pad_sg;

		sg_init_one(&pad_sg, pad_bytes, padding);
		crypto_hash_update(hash, &pad_sg, padding);
	}
	crypto_hash_final(hash, (u8 *) &data_crc);

	return data_crc;
}

static void iscsit_do_crypto_hash_buf(
	struct hash_desc *hash,
	const void *buf,
	u32 payload_length,
	u32 padding,
	u8 *pad_bytes,
	u8 *data_crc)
{
	struct scatterlist sg;

	crypto_hash_init(hash);

	sg_init_one(&sg, buf, payload_length);
	crypto_hash_update(hash, &sg, payload_length);

	if (padding) {
		sg_init_one(&sg, pad_bytes, padding);
		crypto_hash_update(hash, &sg, padding);
	}
	crypto_hash_final(hash, data_crc);
}

int
iscsit_check_dataout_hdr(struct iscsi_conn *conn, unsigned char *buf,
			  struct iscsi_cmd **out_cmd)
{
	struct iscsi_data *hdr = (struct iscsi_data *)buf;
	struct iscsi_cmd *cmd = NULL;
	struct se_cmd *se_cmd;
	u32 payload_length = ntoh24(hdr->dlength);
	int rc;

	if (!payload_length) {
		pr_err("DataOUT payload is ZERO, protocol error.\n");
		return iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
					 buf);
	}

	/* iSCSI write */
	spin_lock_bh(&conn->sess->session_stats_lock);
	conn->sess->rx_data_octets += payload_length;
	if (conn->sess->se_sess->se_node_acl) {
		spin_lock(&conn->sess->se_sess->se_node_acl->stats_lock);
		conn->sess->se_sess->se_node_acl->write_bytes += payload_length;
		spin_unlock(&conn->sess->se_sess->se_node_acl->stats_lock);
	}
	spin_unlock_bh(&conn->sess->session_stats_lock);

	if (payload_length > conn->conn_ops->MaxXmitDataSegmentLength) {
		pr_err("DataSegmentLength: %u is greater than"
			" MaxXmitDataSegmentLength: %u\n", payload_length,
			conn->conn_ops->MaxXmitDataSegmentLength);
		return iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
					 buf);
	}

	cmd = iscsit_find_cmd_from_itt_or_dump(conn, hdr->itt,
			payload_length);
	if (!cmd)
		return 0;

	pr_debug("Got DataOut ITT: 0x%08x, TTT: 0x%08x,"
		" DataSN: 0x%08x, Offset: %u, Length: %u, CID: %hu\n",
		hdr->itt, hdr->ttt, hdr->datasn, ntohl(hdr->offset),
		payload_length, conn->cid);

	if (cmd->cmd_flags & ICF_GOT_LAST_DATAOUT) {
		pr_err("Command ITT: 0x%08x received DataOUT after"
			" last DataOUT received, dumping payload\n",
			cmd->init_task_tag);
		return iscsit_dump_data_payload(conn, payload_length, 1);
	}

	if (cmd->data_direction != DMA_TO_DEVICE) {
		pr_err("Command ITT: 0x%08x received DataOUT for a"
			" NON-WRITE command.\n", cmd->init_task_tag);
		return iscsit_reject_cmd(cmd, ISCSI_REASON_PROTOCOL_ERROR, buf);
	}
	se_cmd = &cmd->se_cmd;
	iscsit_mod_dataout_timer(cmd);

	if ((be32_to_cpu(hdr->offset) + payload_length) > cmd->se_cmd.data_length) {
		pr_err("DataOut Offset: %u, Length %u greater than"
			" iSCSI Command EDTL %u, protocol error.\n",
			hdr->offset, payload_length, cmd->se_cmd.data_length);
		return iscsit_reject_cmd(cmd, ISCSI_REASON_BOOKMARK_INVALID, buf);
	}

	if (cmd->unsolicited_data) {
		int dump_unsolicited_data = 0;

		if (conn->sess->sess_ops->InitialR2T) {
			pr_err("Received unexpected unsolicited data"
				" while InitialR2T=Yes, protocol error.\n");
			transport_send_check_condition_and_sense(&cmd->se_cmd,
					TCM_UNEXPECTED_UNSOLICITED_DATA, 0);
			return -1;
		}
		/*
		 * Special case for dealing with Unsolicited DataOUT
		 * and Unsupported SAM WRITE Opcodes and SE resource allocation
		 * failures;
		 */

		/* Something's amiss if we're not in WRITE_PENDING state... */
		WARN_ON(se_cmd->t_state != TRANSPORT_WRITE_PENDING);
		if (!(se_cmd->se_cmd_flags & SCF_SUPPORTED_SAM_OPCODE))
			dump_unsolicited_data = 1;

		if (dump_unsolicited_data) {
			/*
			 * Check if a delayed TASK_ABORTED status needs to
			 * be sent now if the ISCSI_FLAG_CMD_FINAL has been
			 * received with the unsolicitied data out.
			 */
			if (hdr->flags & ISCSI_FLAG_CMD_FINAL)
				iscsit_stop_dataout_timer(cmd);

			transport_check_aborted_status(se_cmd,
					(hdr->flags & ISCSI_FLAG_CMD_FINAL));
			return iscsit_dump_data_payload(conn, payload_length, 1);
		}
	} else {
		/*
		 * For the normal solicited data path:
		 *
		 * Check for a delayed TASK_ABORTED status and dump any
		 * incoming data out payload if one exists.  Also, when the
		 * ISCSI_FLAG_CMD_FINAL is set to denote the end of the current
		 * data out sequence, we decrement outstanding_r2ts.  Once
		 * outstanding_r2ts reaches zero, go ahead and send the delayed
		 * TASK_ABORTED status.
		 */
		if (se_cmd->transport_state & CMD_T_ABORTED) {
			if (hdr->flags & ISCSI_FLAG_CMD_FINAL)
				if (--cmd->outstanding_r2ts < 1) {
					iscsit_stop_dataout_timer(cmd);
					transport_check_aborted_status(
							se_cmd, 1);
				}

			return iscsit_dump_data_payload(conn, payload_length, 1);
		}
	}
	/*
	 * Preform DataSN, DataSequenceInOrder, DataPDUInOrder, and
	 * within-command recovery checks before receiving the payload.
	 */
	rc = iscsit_check_pre_dataout(cmd, buf);
	if (rc == DATAOUT_WITHIN_COMMAND_RECOVERY)
		return 0;
	else if (rc == DATAOUT_CANNOT_RECOVER)
		return -1;

	*out_cmd = cmd;
	return 0;
}
EXPORT_SYMBOL(iscsit_check_dataout_hdr);

static int
iscsit_get_dataout(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
		   struct iscsi_data *hdr)
{
	struct kvec *iov;
	u32 checksum, iov_count = 0, padding = 0, rx_got = 0, rx_size = 0;
	u32 payload_length = ntoh24(hdr->dlength);
	int iov_ret, data_crc_failed = 0;

	rx_size += payload_length;
	iov = &cmd->iov_data[0];

	iov_ret = iscsit_map_iovec(cmd, iov, be32_to_cpu(hdr->offset),
				   payload_length);
	if (iov_ret < 0)
		return -1;

	iov_count += iov_ret;

	padding = ((-payload_length) & 3);
	if (padding != 0) {
		iov[iov_count].iov_base	= cmd->pad_bytes;
		iov[iov_count++].iov_len = padding;
		rx_size += padding;
		pr_debug("Receiving %u padding bytes.\n", padding);
	}

	if (conn->conn_ops->DataDigest) {
		iov[iov_count].iov_base = &checksum;
		iov[iov_count++].iov_len = ISCSI_CRC_LEN;
		rx_size += ISCSI_CRC_LEN;
	}

	rx_got = rx_data(conn, &cmd->iov_data[0], iov_count, rx_size);

	iscsit_unmap_iovec(cmd);

	if (rx_got != rx_size)
		return -1;

	if (conn->conn_ops->DataDigest) {
		u32 data_crc;

		data_crc = iscsit_do_crypto_hash_sg(&conn->conn_rx_hash, cmd,
						    be32_to_cpu(hdr->offset),
						    payload_length, padding,
						    cmd->pad_bytes);

		if (checksum != data_crc) {
			pr_err("ITT: 0x%08x, Offset: %u, Length: %u,"
				" DataSN: 0x%08x, CRC32C DataDigest 0x%08x"
				" does not match computed 0x%08x\n",
				hdr->itt, hdr->offset, payload_length,
				hdr->datasn, checksum, data_crc);
			data_crc_failed = 1;
		} else {
			pr_debug("Got CRC32C DataDigest 0x%08x for"
				" %u bytes of Data Out\n", checksum,
				payload_length);
		}
	}

	return data_crc_failed;
}

int
iscsit_check_dataout_payload(struct iscsi_cmd *cmd, struct iscsi_data *hdr,
			     bool data_crc_failed)
{
	struct iscsi_conn *conn = cmd->conn;
	int rc, ooo_cmdsn;
	/*
	 * Increment post receive data and CRC values or perform
	 * within-command recovery.
	 */
	rc = iscsit_check_post_dataout(cmd, (unsigned char *)hdr, data_crc_failed);
	if ((rc == DATAOUT_NORMAL) || (rc == DATAOUT_WITHIN_COMMAND_RECOVERY))
		return 0;
	else if (rc == DATAOUT_SEND_R2T) {
		iscsit_set_dataout_sequence_values(cmd);
		conn->conn_transport->iscsit_get_dataout(conn, cmd, false);
	} else if (rc == DATAOUT_SEND_TO_TRANSPORT) {
		/*
		 * Handle extra special case for out of order
		 * Unsolicited Data Out.
		 */
		spin_lock_bh(&cmd->istate_lock);
		ooo_cmdsn = (cmd->cmd_flags & ICF_OOO_CMDSN);
		cmd->cmd_flags |= ICF_GOT_LAST_DATAOUT;
		cmd->i_state = ISTATE_RECEIVED_LAST_DATAOUT;
		spin_unlock_bh(&cmd->istate_lock);

		iscsit_stop_dataout_timer(cmd);
		if (ooo_cmdsn)
			return 0;
		target_execute_cmd(&cmd->se_cmd);
		return 0;
	} else /* DATAOUT_CANNOT_RECOVER */
		return -1;

	return 0;
}
EXPORT_SYMBOL(iscsit_check_dataout_payload);

static int iscsit_handle_data_out(struct iscsi_conn *conn, unsigned char *buf)
{
	struct iscsi_cmd *cmd;
	struct iscsi_data *hdr = (struct iscsi_data *)buf;
	int rc;
	bool data_crc_failed = false;

	rc = iscsit_check_dataout_hdr(conn, buf, &cmd);
	if (rc < 0)
		return 0;
	else if (!cmd)
		return 0;

	rc = iscsit_get_dataout(conn, cmd, hdr);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		data_crc_failed = true;

	return iscsit_check_dataout_payload(cmd, hdr, data_crc_failed);
}

int iscsit_setup_nop_out(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			 struct iscsi_nopout *hdr)
{
	u32 payload_length = ntoh24(hdr->dlength);

	if (hdr->itt == RESERVED_ITT && !(hdr->opcode & ISCSI_OP_IMMEDIATE)) {
		pr_err("NOPOUT ITT is reserved, but Immediate Bit is"
			" not set, protocol error.\n");
		if (!cmd)
			return iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
						 (unsigned char *)hdr);

		return iscsit_reject_cmd(cmd, ISCSI_REASON_PROTOCOL_ERROR,
					 (unsigned char *)hdr);
	}

	if (payload_length > conn->conn_ops->MaxXmitDataSegmentLength) {
		pr_err("NOPOUT Ping Data DataSegmentLength: %u is"
			" greater than MaxXmitDataSegmentLength: %u, protocol"
			" error.\n", payload_length,
			conn->conn_ops->MaxXmitDataSegmentLength);
		if (!cmd)
			return iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
						 (unsigned char *)hdr);

		return iscsit_reject_cmd(cmd, ISCSI_REASON_PROTOCOL_ERROR,
					 (unsigned char *)hdr);
	}

	pr_debug("Got NOPOUT Ping %s ITT: 0x%08x, TTT: 0x%08x,"
		" CmdSN: 0x%08x, ExpStatSN: 0x%08x, Length: %u\n",
		hdr->itt == RESERVED_ITT ? "Response" : "Request",
		hdr->itt, hdr->ttt, hdr->cmdsn, hdr->exp_statsn,
		payload_length);
	/*
	 * This is not a response to a Unsolicited NopIN, which means
	 * it can either be a NOPOUT ping request (with a valid ITT),
	 * or a NOPOUT not requesting a NOPIN (with a reserved ITT).
	 * Either way, make sure we allocate an struct iscsi_cmd, as both
	 * can contain ping data.
	 */
	if (hdr->ttt == cpu_to_be32(0xFFFFFFFF)) {
		cmd->iscsi_opcode	= ISCSI_OP_NOOP_OUT;
		cmd->i_state		= ISTATE_SEND_NOPIN;
		cmd->immediate_cmd	= ((hdr->opcode & ISCSI_OP_IMMEDIATE) ?
						1 : 0);
		conn->sess->init_task_tag = cmd->init_task_tag = hdr->itt;
		cmd->targ_xfer_tag	= 0xFFFFFFFF;
		cmd->cmd_sn		= be32_to_cpu(hdr->cmdsn);
		cmd->exp_stat_sn	= be32_to_cpu(hdr->exp_statsn);
		cmd->data_direction	= DMA_NONE;
	}

	return 0;
}
EXPORT_SYMBOL(iscsit_setup_nop_out);

int iscsit_process_nop_out(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			   struct iscsi_nopout *hdr)
{
	struct iscsi_cmd *cmd_p = NULL;
	int cmdsn_ret = 0;
	/*
	 * Initiator is expecting a NopIN ping reply..
	 */
	if (hdr->itt != RESERVED_ITT) {
		BUG_ON(!cmd);

		spin_lock_bh(&conn->cmd_lock);
		list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
		spin_unlock_bh(&conn->cmd_lock);

		iscsit_ack_from_expstatsn(conn, be32_to_cpu(hdr->exp_statsn));

		if (hdr->opcode & ISCSI_OP_IMMEDIATE) {
			iscsit_add_cmd_to_response_queue(cmd, conn,
							 cmd->i_state);
			return 0;
		}

		cmdsn_ret = iscsit_sequence_cmd(conn, cmd,
				(unsigned char *)hdr, hdr->cmdsn);
                if (cmdsn_ret == CMDSN_LOWER_THAN_EXP)
			return 0;
		if (cmdsn_ret == CMDSN_ERROR_CANNOT_RECOVER)
			return -1;

		return 0;
	}
	/*
	 * This was a response to a unsolicited NOPIN ping.
	 */
	if (hdr->ttt != cpu_to_be32(0xFFFFFFFF)) {
		cmd_p = iscsit_find_cmd_from_ttt(conn, be32_to_cpu(hdr->ttt));
		if (!cmd_p)
			return -EINVAL;

		iscsit_stop_nopin_response_timer(conn);

		cmd_p->i_state = ISTATE_REMOVE;
		iscsit_add_cmd_to_immediate_queue(cmd_p, conn, cmd_p->i_state);

		iscsit_start_nopin_timer(conn);
		return 0;
	}
	/*
	 * Otherwise, initiator is not expecting a NOPIN is response.
	 * Just ignore for now.
	 */
        return 0;
}
EXPORT_SYMBOL(iscsit_process_nop_out);

static int iscsit_handle_nop_out(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
				 unsigned char *buf)
{
	unsigned char *ping_data = NULL;
	struct iscsi_nopout *hdr = (struct iscsi_nopout *)buf;
	struct kvec *iov = NULL;
	u32 payload_length = ntoh24(hdr->dlength);
	int ret;

	ret = iscsit_setup_nop_out(conn, cmd, hdr);
	if (ret < 0)
		return 0;
	/*
	 * Handle NOP-OUT payload for traditional iSCSI sockets
	 */
	if (payload_length && hdr->ttt == cpu_to_be32(0xFFFFFFFF)) {
		u32 checksum, data_crc, padding = 0;
		int niov = 0, rx_got, rx_size = payload_length;

		ping_data = kzalloc(payload_length + 1, GFP_KERNEL);
		if (!ping_data) {
			pr_err("Unable to allocate memory for"
				" NOPOUT ping data.\n");
			ret = -1;
			goto out;
		}

		iov = &cmd->iov_misc[0];
		iov[niov].iov_base	= ping_data;
		iov[niov++].iov_len	= payload_length;

		padding = ((-payload_length) & 3);
		if (padding != 0) {
			pr_debug("Receiving %u additional bytes"
				" for padding.\n", padding);
			iov[niov].iov_base	= &cmd->pad_bytes;
			iov[niov++].iov_len	= padding;
			rx_size += padding;
		}
		if (conn->conn_ops->DataDigest) {
			iov[niov].iov_base	= &checksum;
			iov[niov++].iov_len	= ISCSI_CRC_LEN;
			rx_size += ISCSI_CRC_LEN;
		}

		rx_got = rx_data(conn, &cmd->iov_misc[0], niov, rx_size);
		if (rx_got != rx_size) {
			ret = -1;
			goto out;
		}

		if (conn->conn_ops->DataDigest) {
			iscsit_do_crypto_hash_buf(&conn->conn_rx_hash,
					ping_data, payload_length,
					padding, cmd->pad_bytes,
					(u8 *)&data_crc);

			if (checksum != data_crc) {
				pr_err("Ping data CRC32C DataDigest"
				" 0x%08x does not match computed 0x%08x\n",
					checksum, data_crc);
				if (!conn->sess->sess_ops->ErrorRecoveryLevel) {
					pr_err("Unable to recover from"
					" NOPOUT Ping DataCRC failure while in"
						" ERL=0.\n");
					ret = -1;
					goto out;
				} else {
					/*
					 * Silently drop this PDU and let the
					 * initiator plug the CmdSN gap.
					 */
					pr_debug("Dropping NOPOUT"
					" Command CmdSN: 0x%08x due to"
					" DataCRC error.\n", hdr->cmdsn);
					ret = 0;
					goto out;
				}
			} else {
				pr_debug("Got CRC32C DataDigest"
				" 0x%08x for %u bytes of ping data.\n",
					checksum, payload_length);
			}
		}

		ping_data[payload_length] = '\0';
		/*
		 * Attach ping data to struct iscsi_cmd->buf_ptr.
		 */
		cmd->buf_ptr = ping_data;
		cmd->buf_ptr_size = payload_length;

		pr_debug("Got %u bytes of NOPOUT ping"
			" data.\n", payload_length);
		pr_debug("Ping Data: \"%s\"\n", ping_data);
	}

	return iscsit_process_nop_out(conn, cmd, hdr);
out:
	if (cmd)
		iscsit_free_cmd(cmd, false);

	kfree(ping_data);
	return ret;
}

int
iscsit_handle_task_mgt_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			   unsigned char *buf)
{
	struct se_tmr_req *se_tmr;
	struct iscsi_tmr_req *tmr_req;
	struct iscsi_tm *hdr;
	int out_of_order_cmdsn = 0, ret;
	bool sess_ref = false;
	u8 function;

	hdr			= (struct iscsi_tm *) buf;
	hdr->flags &= ~ISCSI_FLAG_CMD_FINAL;
	function = hdr->flags;

	pr_debug("Got Task Management Request ITT: 0x%08x, CmdSN:"
		" 0x%08x, Function: 0x%02x, RefTaskTag: 0x%08x, RefCmdSN:"
		" 0x%08x, CID: %hu\n", hdr->itt, hdr->cmdsn, function,
		hdr->rtt, hdr->refcmdsn, conn->cid);

	if ((function != ISCSI_TM_FUNC_ABORT_TASK) &&
	    ((function != ISCSI_TM_FUNC_TASK_REASSIGN) &&
	     hdr->rtt != RESERVED_ITT)) {
		pr_err("RefTaskTag should be set to 0xFFFFFFFF.\n");
		hdr->rtt = RESERVED_ITT;
	}

	if ((function == ISCSI_TM_FUNC_TASK_REASSIGN) &&
			!(hdr->opcode & ISCSI_OP_IMMEDIATE)) {
		pr_err("Task Management Request TASK_REASSIGN not"
			" issued as immediate command, bad iSCSI Initiator"
				"implementation\n");
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_PROTOCOL_ERROR, buf);
	}
	if ((function != ISCSI_TM_FUNC_ABORT_TASK) &&
	    be32_to_cpu(hdr->refcmdsn) != ISCSI_RESERVED_TAG)
		hdr->refcmdsn = cpu_to_be32(ISCSI_RESERVED_TAG);

	cmd->data_direction = DMA_NONE;

	cmd->tmr_req = kzalloc(sizeof(struct iscsi_tmr_req), GFP_KERNEL);
	if (!cmd->tmr_req) {
		pr_err("Unable to allocate memory for"
			" Task Management command!\n");
		return iscsit_add_reject_cmd(cmd,
					     ISCSI_REASON_BOOKMARK_NO_RESOURCES,
					     buf);
	}

	/*
	 * TASK_REASSIGN for ERL=2 / connection stays inside of
	 * LIO-Target $FABRIC_MOD
	 */
	if (function != ISCSI_TM_FUNC_TASK_REASSIGN) {

		u8 tcm_function;
		int ret;

		transport_init_se_cmd(&cmd->se_cmd,
				      &lio_target_fabric_configfs->tf_ops,
				      conn->sess->se_sess, 0, DMA_NONE,
				      MSG_SIMPLE_TAG, cmd->sense_buffer + 2);

		target_get_sess_cmd(conn->sess->se_sess, &cmd->se_cmd, true);
		sess_ref = true;

		switch (function) {
		case ISCSI_TM_FUNC_ABORT_TASK:
			tcm_function = TMR_ABORT_TASK;
			break;
		case ISCSI_TM_FUNC_ABORT_TASK_SET:
			tcm_function = TMR_ABORT_TASK_SET;
			break;
		case ISCSI_TM_FUNC_CLEAR_ACA:
			tcm_function = TMR_CLEAR_ACA;
			break;
		case ISCSI_TM_FUNC_CLEAR_TASK_SET:
			tcm_function = TMR_CLEAR_TASK_SET;
			break;
		case ISCSI_TM_FUNC_LOGICAL_UNIT_RESET:
			tcm_function = TMR_LUN_RESET;
			break;
		case ISCSI_TM_FUNC_TARGET_WARM_RESET:
			tcm_function = TMR_TARGET_WARM_RESET;
			break;
		case ISCSI_TM_FUNC_TARGET_COLD_RESET:
			tcm_function = TMR_TARGET_COLD_RESET;
			break;
		default:
			pr_err("Unknown iSCSI TMR Function:"
			       " 0x%02x\n", function);
			return iscsit_add_reject_cmd(cmd,
				ISCSI_REASON_BOOKMARK_NO_RESOURCES, buf);
		}

		ret = core_tmr_alloc_req(&cmd->se_cmd, cmd->tmr_req,
					 tcm_function, GFP_KERNEL);
		if (ret < 0)
			return iscsit_add_reject_cmd(cmd,
				ISCSI_REASON_BOOKMARK_NO_RESOURCES, buf);

		cmd->tmr_req->se_tmr_req = cmd->se_cmd.se_tmr_req;
	}

	cmd->iscsi_opcode	= ISCSI_OP_SCSI_TMFUNC;
	cmd->i_state		= ISTATE_SEND_TASKMGTRSP;
	cmd->immediate_cmd	= ((hdr->opcode & ISCSI_OP_IMMEDIATE) ? 1 : 0);
	cmd->init_task_tag	= hdr->itt;
	cmd->targ_xfer_tag	= 0xFFFFFFFF;
	cmd->cmd_sn		= be32_to_cpu(hdr->cmdsn);
	cmd->exp_stat_sn	= be32_to_cpu(hdr->exp_statsn);
	se_tmr			= cmd->se_cmd.se_tmr_req;
	tmr_req			= cmd->tmr_req;
	/*
	 * Locate the struct se_lun for all TMRs not related to ERL=2 TASK_REASSIGN
	 */
	if (function != ISCSI_TM_FUNC_TASK_REASSIGN) {
		ret = transport_lookup_tmr_lun(&cmd->se_cmd,
					       scsilun_to_int(&hdr->lun));
		if (ret < 0) {
			se_tmr->response = ISCSI_TMF_RSP_NO_LUN;
			goto attach;
		}
	}

	switch (function) {
	case ISCSI_TM_FUNC_ABORT_TASK:
		se_tmr->response = iscsit_tmr_abort_task(cmd, buf);
		if (se_tmr->response)
			goto attach;
		break;
	case ISCSI_TM_FUNC_ABORT_TASK_SET:
	case ISCSI_TM_FUNC_CLEAR_ACA:
	case ISCSI_TM_FUNC_CLEAR_TASK_SET:
	case ISCSI_TM_FUNC_LOGICAL_UNIT_RESET:
		break;
	case ISCSI_TM_FUNC_TARGET_WARM_RESET:
		if (iscsit_tmr_task_warm_reset(conn, tmr_req, buf) < 0) {
			se_tmr->response = ISCSI_TMF_RSP_AUTH_FAILED;
			goto attach;
		}
		break;
	case ISCSI_TM_FUNC_TARGET_COLD_RESET:
		if (iscsit_tmr_task_cold_reset(conn, tmr_req, buf) < 0) {
			se_tmr->response = ISCSI_TMF_RSP_AUTH_FAILED;
			goto attach;
		}
		break;
	case ISCSI_TM_FUNC_TASK_REASSIGN:
		se_tmr->response = iscsit_tmr_task_reassign(cmd, buf);
		/*
		 * Perform sanity checks on the ExpDataSN only if the
		 * TASK_REASSIGN was successful.
		 */
		if (se_tmr->response)
			break;

		if (iscsit_check_task_reassign_expdatasn(tmr_req, conn) < 0)
			return iscsit_add_reject_cmd(cmd,
					ISCSI_REASON_BOOKMARK_INVALID, buf);
		break;
	default:
		pr_err("Unknown TMR function: 0x%02x, protocol"
			" error.\n", function);
		se_tmr->response = ISCSI_TMF_RSP_NOT_SUPPORTED;
		goto attach;
	}

	if ((function != ISCSI_TM_FUNC_TASK_REASSIGN) &&
	    (se_tmr->response == ISCSI_TMF_RSP_COMPLETE))
		se_tmr->call_transport = 1;
attach:
	spin_lock_bh(&conn->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
	spin_unlock_bh(&conn->cmd_lock);

	if (!(hdr->opcode & ISCSI_OP_IMMEDIATE)) {
		int cmdsn_ret = iscsit_sequence_cmd(conn, cmd, buf, hdr->cmdsn);
		if (cmdsn_ret == CMDSN_HIGHER_THAN_EXP)
			out_of_order_cmdsn = 1;
		else if (cmdsn_ret == CMDSN_LOWER_THAN_EXP)
			return 0;
		else if (cmdsn_ret == CMDSN_ERROR_CANNOT_RECOVER)
			return -1;
	}
	iscsit_ack_from_expstatsn(conn, be32_to_cpu(hdr->exp_statsn));

	if (out_of_order_cmdsn || !(hdr->opcode & ISCSI_OP_IMMEDIATE))
		return 0;
	/*
	 * Found the referenced task, send to transport for processing.
	 */
	if (se_tmr->call_transport)
		return transport_generic_handle_tmr(&cmd->se_cmd);

	/*
	 * Could not find the referenced LUN, task, or Task Management
	 * command not authorized or supported.  Change state and
	 * let the tx_thread send the response.
	 *
	 * For connection recovery, this is also the default action for
	 * TMR TASK_REASSIGN.
	 */
	if (sess_ref) {
		pr_debug("Handle TMR, using sess_ref=true check\n");
		target_put_sess_cmd(conn->sess->se_sess, &cmd->se_cmd);
	}

	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
	return 0;
}
EXPORT_SYMBOL(iscsit_handle_task_mgt_cmd);

/* #warning FIXME: Support Text Command parameters besides SendTargets */
int
iscsit_setup_text_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
		      struct iscsi_text *hdr)
{
	u32 payload_length = ntoh24(hdr->dlength);

	if (payload_length > conn->conn_ops->MaxXmitDataSegmentLength) {
		pr_err("Unable to accept text parameter length: %u"
			"greater than MaxXmitDataSegmentLength %u.\n",
		       payload_length, conn->conn_ops->MaxXmitDataSegmentLength);
		return iscsit_reject_cmd(cmd, ISCSI_REASON_PROTOCOL_ERROR,
					 (unsigned char *)hdr);
	}

	pr_debug("Got Text Request: ITT: 0x%08x, CmdSN: 0x%08x,"
		" ExpStatSN: 0x%08x, Length: %u\n", hdr->itt, hdr->cmdsn,
		hdr->exp_statsn, payload_length);

	cmd->iscsi_opcode	= ISCSI_OP_TEXT;
	cmd->i_state		= ISTATE_SEND_TEXTRSP;
	cmd->immediate_cmd	= ((hdr->opcode & ISCSI_OP_IMMEDIATE) ? 1 : 0);
	conn->sess->init_task_tag = cmd->init_task_tag  = hdr->itt;
	cmd->targ_xfer_tag	= 0xFFFFFFFF;
	cmd->cmd_sn		= be32_to_cpu(hdr->cmdsn);
	cmd->exp_stat_sn	= be32_to_cpu(hdr->exp_statsn);
	cmd->data_direction	= DMA_NONE;

	return 0;
}
EXPORT_SYMBOL(iscsit_setup_text_cmd);

int
iscsit_process_text_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			struct iscsi_text *hdr)
{
	unsigned char *text_in = cmd->text_in_ptr, *text_ptr;
	int cmdsn_ret;

	if (!text_in) {
		pr_err("Unable to locate text_in buffer for sendtargets"
		       " discovery\n");
		goto reject;
	}
	if (strncmp("SendTargets", text_in, 11) != 0) {
		pr_err("Received Text Data that is not"
			" SendTargets, cannot continue.\n");
		goto reject;
	}
	text_ptr = strchr(text_in, '=');
	if (!text_ptr) {
		pr_err("No \"=\" separator found in Text Data,"
			"  cannot continue.\n");
		goto reject;
	}
	if (!strncmp("=All", text_ptr, 4)) {
		cmd->cmd_flags |= IFC_SENDTARGETS_ALL;
	} else if (!strncmp("=iqn.", text_ptr, 5) ||
		   !strncmp("=eui.", text_ptr, 5)) {
		cmd->cmd_flags |= IFC_SENDTARGETS_SINGLE;
	} else {
		pr_err("Unable to locate valid SendTargets=%s value\n", text_ptr);
		goto reject;
	}

	spin_lock_bh(&conn->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
	spin_unlock_bh(&conn->cmd_lock);

	iscsit_ack_from_expstatsn(conn, be32_to_cpu(hdr->exp_statsn));

	if (!(hdr->opcode & ISCSI_OP_IMMEDIATE)) {
		cmdsn_ret = iscsit_sequence_cmd(conn, cmd,
				(unsigned char *)hdr, hdr->cmdsn);
		if (cmdsn_ret == CMDSN_ERROR_CANNOT_RECOVER)
			return -1;

		return 0;
	}

	return iscsit_execute_cmd(cmd, 0);

reject:
	return iscsit_reject_cmd(cmd, ISCSI_REASON_PROTOCOL_ERROR,
				 (unsigned char *)hdr);
}
EXPORT_SYMBOL(iscsit_process_text_cmd);

static int
iscsit_handle_text_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
		       unsigned char *buf)
{
	struct iscsi_text *hdr = (struct iscsi_text *)buf;
	char *text_in = NULL;
	u32 payload_length = ntoh24(hdr->dlength);
	int rx_size, rc;

	rc = iscsit_setup_text_cmd(conn, cmd, hdr);
	if (rc < 0)
		return 0;

	rx_size = payload_length;
	if (payload_length) {
		u32 checksum = 0, data_crc = 0;
		u32 padding = 0, pad_bytes = 0;
		int niov = 0, rx_got;
		struct kvec iov[3];

		text_in = kzalloc(payload_length, GFP_KERNEL);
		if (!text_in) {
			pr_err("Unable to allocate memory for"
				" incoming text parameters\n");
			goto reject;
		}
		cmd->text_in_ptr = text_in;

		memset(iov, 0, 3 * sizeof(struct kvec));
		iov[niov].iov_base	= text_in;
		iov[niov++].iov_len	= payload_length;

		padding = ((-payload_length) & 3);
		if (padding != 0) {
			iov[niov].iov_base = &pad_bytes;
			iov[niov++].iov_len  = padding;
			rx_size += padding;
			pr_debug("Receiving %u additional bytes"
					" for padding.\n", padding);
		}
		if (conn->conn_ops->DataDigest) {
			iov[niov].iov_base	= &checksum;
			iov[niov++].iov_len	= ISCSI_CRC_LEN;
			rx_size += ISCSI_CRC_LEN;
		}

		rx_got = rx_data(conn, &iov[0], niov, rx_size);
		if (rx_got != rx_size)
			goto reject;

		if (conn->conn_ops->DataDigest) {
			iscsit_do_crypto_hash_buf(&conn->conn_rx_hash,
					text_in, payload_length,
					padding, (u8 *)&pad_bytes,
					(u8 *)&data_crc);

			if (checksum != data_crc) {
				pr_err("Text data CRC32C DataDigest"
					" 0x%08x does not match computed"
					" 0x%08x\n", checksum, data_crc);
				if (!conn->sess->sess_ops->ErrorRecoveryLevel) {
					pr_err("Unable to recover from"
					" Text Data digest failure while in"
						" ERL=0.\n");
					goto reject;
				} else {
					/*
					 * Silently drop this PDU and let the
					 * initiator plug the CmdSN gap.
					 */
					pr_debug("Dropping Text"
					" Command CmdSN: 0x%08x due to"
					" DataCRC error.\n", hdr->cmdsn);
					kfree(text_in);
					return 0;
				}
			} else {
				pr_debug("Got CRC32C DataDigest"
					" 0x%08x for %u bytes of text data.\n",
						checksum, payload_length);
			}
		}
		text_in[payload_length - 1] = '\0';
		pr_debug("Successfully read %d bytes of text"
				" data.\n", payload_length);
	}

	return iscsit_process_text_cmd(conn, cmd, hdr);

reject:
	kfree(cmd->text_in_ptr);
	cmd->text_in_ptr = NULL;
	return iscsit_reject_cmd(cmd, ISCSI_REASON_PROTOCOL_ERROR, buf);
}
EXPORT_SYMBOL(iscsit_handle_text_cmd);

int iscsit_logout_closesession(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct iscsi_conn *conn_p;
	struct iscsi_session *sess = conn->sess;

	pr_debug("Received logout request CLOSESESSION on CID: %hu"
		" for SID: %u.\n", conn->cid, conn->sess->sid);

	atomic_set(&sess->session_logout, 1);
	atomic_set(&conn->conn_logout_remove, 1);
	conn->conn_logout_reason = ISCSI_LOGOUT_REASON_CLOSE_SESSION;

	iscsit_inc_conn_usage_count(conn);
	iscsit_inc_session_usage_count(sess);

	spin_lock_bh(&sess->conn_lock);
	list_for_each_entry(conn_p, &sess->sess_conn_list, conn_list) {
		if (conn_p->conn_state != TARG_CONN_STATE_LOGGED_IN)
			continue;

		pr_debug("Moving to TARG_CONN_STATE_IN_LOGOUT.\n");
		conn_p->conn_state = TARG_CONN_STATE_IN_LOGOUT;
	}
	spin_unlock_bh(&sess->conn_lock);

	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);

	return 0;
}

int iscsit_logout_closeconnection(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct iscsi_conn *l_conn;
	struct iscsi_session *sess = conn->sess;

	pr_debug("Received logout request CLOSECONNECTION for CID:"
		" %hu on CID: %hu.\n", cmd->logout_cid, conn->cid);

	/*
	 * A Logout Request with a CLOSECONNECTION reason code for a CID
	 * can arrive on a connection with a differing CID.
	 */
	if (conn->cid == cmd->logout_cid) {
		spin_lock_bh(&conn->state_lock);
		pr_debug("Moving to TARG_CONN_STATE_IN_LOGOUT.\n");
		conn->conn_state = TARG_CONN_STATE_IN_LOGOUT;

		atomic_set(&conn->conn_logout_remove, 1);
		conn->conn_logout_reason = ISCSI_LOGOUT_REASON_CLOSE_CONNECTION;
		iscsit_inc_conn_usage_count(conn);

		spin_unlock_bh(&conn->state_lock);
	} else {
		/*
		 * Handle all different cid CLOSECONNECTION requests in
		 * iscsit_logout_post_handler_diffcid() as to give enough
		 * time for any non immediate command's CmdSN to be
		 * acknowledged on the connection in question.
		 *
		 * Here we simply make sure the CID is still around.
		 */
		l_conn = iscsit_get_conn_from_cid(sess,
				cmd->logout_cid);
		if (!l_conn) {
			cmd->logout_response = ISCSI_LOGOUT_CID_NOT_FOUND;
			iscsit_add_cmd_to_response_queue(cmd, conn,
					cmd->i_state);
			return 0;
		}

		iscsit_dec_conn_usage_count(l_conn);
	}

	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);

	return 0;
}

int iscsit_logout_removeconnforrecovery(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct iscsi_session *sess = conn->sess;

	pr_debug("Received explicit REMOVECONNFORRECOVERY logout for"
		" CID: %hu on CID: %hu.\n", cmd->logout_cid, conn->cid);

	if (sess->sess_ops->ErrorRecoveryLevel != 2) {
		pr_err("Received Logout Request REMOVECONNFORRECOVERY"
			" while ERL!=2.\n");
		cmd->logout_response = ISCSI_LOGOUT_RECOVERY_UNSUPPORTED;
		iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
		return 0;
	}

	if (conn->cid == cmd->logout_cid) {
		pr_err("Received Logout Request REMOVECONNFORRECOVERY"
			" with CID: %hu on CID: %hu, implementation error.\n",
				cmd->logout_cid, conn->cid);
		cmd->logout_response = ISCSI_LOGOUT_CLEANUP_FAILED;
		iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
		return 0;
	}

	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);

	return 0;
}

int
iscsit_handle_logout_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			unsigned char *buf)
{
	int cmdsn_ret, logout_remove = 0;
	u8 reason_code = 0;
	struct iscsi_logout *hdr;
	struct iscsi_tiqn *tiqn = iscsit_snmp_get_tiqn(conn);

	hdr			= (struct iscsi_logout *) buf;
	reason_code		= (hdr->flags & 0x7f);

	if (tiqn) {
		spin_lock(&tiqn->logout_stats.lock);
		if (reason_code == ISCSI_LOGOUT_REASON_CLOSE_SESSION)
			tiqn->logout_stats.normal_logouts++;
		else
			tiqn->logout_stats.abnormal_logouts++;
		spin_unlock(&tiqn->logout_stats.lock);
	}

	pr_debug("Got Logout Request ITT: 0x%08x CmdSN: 0x%08x"
		" ExpStatSN: 0x%08x Reason: 0x%02x CID: %hu on CID: %hu\n",
		hdr->itt, hdr->cmdsn, hdr->exp_statsn, reason_code,
		hdr->cid, conn->cid);

	if (conn->conn_state != TARG_CONN_STATE_LOGGED_IN) {
		pr_err("Received logout request on connection that"
			" is not in logged in state, ignoring request.\n");
		iscsit_free_cmd(cmd, false);
		return 0;
	}

	cmd->iscsi_opcode       = ISCSI_OP_LOGOUT;
	cmd->i_state            = ISTATE_SEND_LOGOUTRSP;
	cmd->immediate_cmd      = ((hdr->opcode & ISCSI_OP_IMMEDIATE) ? 1 : 0);
	conn->sess->init_task_tag = cmd->init_task_tag  = hdr->itt;
	cmd->targ_xfer_tag      = 0xFFFFFFFF;
	cmd->cmd_sn             = be32_to_cpu(hdr->cmdsn);
	cmd->exp_stat_sn        = be32_to_cpu(hdr->exp_statsn);
	cmd->logout_cid         = be16_to_cpu(hdr->cid);
	cmd->logout_reason      = reason_code;
	cmd->data_direction     = DMA_NONE;

	/*
	 * We need to sleep in these cases (by returning 1) until the Logout
	 * Response gets sent in the tx thread.
	 */
	if ((reason_code == ISCSI_LOGOUT_REASON_CLOSE_SESSION) ||
	   ((reason_code == ISCSI_LOGOUT_REASON_CLOSE_CONNECTION) &&
	    be16_to_cpu(hdr->cid) == conn->cid))
		logout_remove = 1;

	spin_lock_bh(&conn->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
	spin_unlock_bh(&conn->cmd_lock);

	if (reason_code != ISCSI_LOGOUT_REASON_RECOVERY)
		iscsit_ack_from_expstatsn(conn, be32_to_cpu(hdr->exp_statsn));

	/*
	 * Immediate commands are executed, well, immediately.
	 * Non-Immediate Logout Commands are executed in CmdSN order.
	 */
	if (cmd->immediate_cmd) {
		int ret = iscsit_execute_cmd(cmd, 0);

		if (ret < 0)
			return ret;
	} else {
		cmdsn_ret = iscsit_sequence_cmd(conn, cmd, buf, hdr->cmdsn);
		if (cmdsn_ret == CMDSN_LOWER_THAN_EXP)
			logout_remove = 0;
		else if (cmdsn_ret == CMDSN_ERROR_CANNOT_RECOVER)
			return -1;
	}

	return logout_remove;
}
EXPORT_SYMBOL(iscsit_handle_logout_cmd);

static int iscsit_handle_snack(
	struct iscsi_conn *conn,
	unsigned char *buf)
{
	struct iscsi_snack *hdr;

	hdr			= (struct iscsi_snack *) buf;
	hdr->flags		&= ~ISCSI_FLAG_CMD_FINAL;

	pr_debug("Got ISCSI_INIT_SNACK, ITT: 0x%08x, ExpStatSN:"
		" 0x%08x, Type: 0x%02x, BegRun: 0x%08x, RunLength: 0x%08x,"
		" CID: %hu\n", hdr->itt, hdr->exp_statsn, hdr->flags,
			hdr->begrun, hdr->runlength, conn->cid);

	if (!conn->sess->sess_ops->ErrorRecoveryLevel) {
		pr_err("Initiator sent SNACK request while in"
			" ErrorRecoveryLevel=0.\n");
		return iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
					 buf);
	}
	/*
	 * SNACK_DATA and SNACK_R2T are both 0,  so check which function to
	 * call from inside iscsi_send_recovery_datain_or_r2t().
	 */
	switch (hdr->flags & ISCSI_FLAG_SNACK_TYPE_MASK) {
	case 0:
		return iscsit_handle_recovery_datain_or_r2t(conn, buf,
			hdr->itt,
			be32_to_cpu(hdr->ttt),
			be32_to_cpu(hdr->begrun),
			be32_to_cpu(hdr->runlength));
	case ISCSI_FLAG_SNACK_TYPE_STATUS:
		return iscsit_handle_status_snack(conn, hdr->itt,
			be32_to_cpu(hdr->ttt),
			be32_to_cpu(hdr->begrun), be32_to_cpu(hdr->runlength));
	case ISCSI_FLAG_SNACK_TYPE_DATA_ACK:
		return iscsit_handle_data_ack(conn, be32_to_cpu(hdr->ttt),
			be32_to_cpu(hdr->begrun),
			be32_to_cpu(hdr->runlength));
	case ISCSI_FLAG_SNACK_TYPE_RDATA:
		/* FIXME: Support R-Data SNACK */
		pr_err("R-Data SNACK Not Supported.\n");
		return iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
					 buf);
	default:
		pr_err("Unknown SNACK type 0x%02x, protocol"
			" error.\n", hdr->flags & 0x0f);
		return iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
					 buf);
	}

	return 0;
}

static void iscsit_rx_thread_wait_for_tcp(struct iscsi_conn *conn)
{
	if ((conn->sock->sk->sk_shutdown & SEND_SHUTDOWN) ||
	    (conn->sock->sk->sk_shutdown & RCV_SHUTDOWN)) {
		wait_for_completion_interruptible_timeout(
					&conn->rx_half_close_comp,
					ISCSI_RX_THREAD_TCP_TIMEOUT * HZ);
	}
}

static int iscsit_handle_immediate_data(
	struct iscsi_cmd *cmd,
	struct iscsi_scsi_req *hdr,
	u32 length)
{
	int iov_ret, rx_got = 0, rx_size = 0;
	u32 checksum, iov_count = 0, padding = 0;
	struct iscsi_conn *conn = cmd->conn;
	struct kvec *iov;

	iov_ret = iscsit_map_iovec(cmd, cmd->iov_data, cmd->write_data_done, length);
	if (iov_ret < 0)
		return IMMEDIATE_DATA_CANNOT_RECOVER;

	rx_size = length;
	iov_count = iov_ret;
	iov = &cmd->iov_data[0];

	padding = ((-length) & 3);
	if (padding != 0) {
		iov[iov_count].iov_base	= cmd->pad_bytes;
		iov[iov_count++].iov_len = padding;
		rx_size += padding;
	}

	if (conn->conn_ops->DataDigest) {
		iov[iov_count].iov_base		= &checksum;
		iov[iov_count++].iov_len	= ISCSI_CRC_LEN;
		rx_size += ISCSI_CRC_LEN;
	}

	rx_got = rx_data(conn, &cmd->iov_data[0], iov_count, rx_size);

	iscsit_unmap_iovec(cmd);

	if (rx_got != rx_size) {
		iscsit_rx_thread_wait_for_tcp(conn);
		return IMMEDIATE_DATA_CANNOT_RECOVER;
	}

	if (conn->conn_ops->DataDigest) {
		u32 data_crc;

		data_crc = iscsit_do_crypto_hash_sg(&conn->conn_rx_hash, cmd,
						    cmd->write_data_done, length, padding,
						    cmd->pad_bytes);

		if (checksum != data_crc) {
			pr_err("ImmediateData CRC32C DataDigest 0x%08x"
				" does not match computed 0x%08x\n", checksum,
				data_crc);

			if (!conn->sess->sess_ops->ErrorRecoveryLevel) {
				pr_err("Unable to recover from"
					" Immediate Data digest failure while"
					" in ERL=0.\n");
				iscsit_reject_cmd(cmd,
						ISCSI_REASON_DATA_DIGEST_ERROR,
						(unsigned char *)hdr);
				return IMMEDIATE_DATA_CANNOT_RECOVER;
			} else {
				iscsit_reject_cmd(cmd,
						ISCSI_REASON_DATA_DIGEST_ERROR,
						(unsigned char *)hdr);
				return IMMEDIATE_DATA_ERL1_CRC_FAILURE;
			}
		} else {
			pr_debug("Got CRC32C DataDigest 0x%08x for"
				" %u bytes of Immediate Data\n", checksum,
				length);
		}
	}

	cmd->write_data_done += length;

	if (cmd->write_data_done == cmd->se_cmd.data_length) {
		spin_lock_bh(&cmd->istate_lock);
		cmd->cmd_flags |= ICF_GOT_LAST_DATAOUT;
		cmd->i_state = ISTATE_RECEIVED_LAST_DATAOUT;
		spin_unlock_bh(&cmd->istate_lock);
	}

	return IMMEDIATE_DATA_NORMAL_OPERATION;
}

/*
 *	Called with sess->conn_lock held.
 */
/* #warning iscsi_build_conn_drop_async_message() only sends out on connections
	with active network interface */
static void iscsit_build_conn_drop_async_message(struct iscsi_conn *conn)
{
	struct iscsi_cmd *cmd;
	struct iscsi_conn *conn_p;

	/*
	 * Only send a Asynchronous Message on connections whos network
	 * interface is still functional.
	 */
	list_for_each_entry(conn_p, &conn->sess->sess_conn_list, conn_list) {
		if (conn_p->conn_state == TARG_CONN_STATE_LOGGED_IN) {
			iscsit_inc_conn_usage_count(conn_p);
			break;
		}
	}

	if (!conn_p)
		return;

	cmd = iscsit_allocate_cmd(conn_p, GFP_ATOMIC);
	if (!cmd) {
		iscsit_dec_conn_usage_count(conn_p);
		return;
	}

	cmd->logout_cid = conn->cid;
	cmd->iscsi_opcode = ISCSI_OP_ASYNC_EVENT;
	cmd->i_state = ISTATE_SEND_ASYNCMSG;

	spin_lock_bh(&conn_p->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn_p->conn_cmd_list);
	spin_unlock_bh(&conn_p->cmd_lock);

	iscsit_add_cmd_to_response_queue(cmd, conn_p, cmd->i_state);
	iscsit_dec_conn_usage_count(conn_p);
}

static int iscsit_send_conn_drop_async_message(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	struct iscsi_async *hdr;

	cmd->tx_size = ISCSI_HDR_LEN;
	cmd->iscsi_opcode = ISCSI_OP_ASYNC_EVENT;

	hdr			= (struct iscsi_async *) cmd->pdu;
	hdr->opcode		= ISCSI_OP_ASYNC_EVENT;
	hdr->flags		= ISCSI_FLAG_CMD_FINAL;
	cmd->init_task_tag	= RESERVED_ITT;
	cmd->targ_xfer_tag	= 0xFFFFFFFF;
	put_unaligned_be64(0xFFFFFFFFFFFFFFFFULL, &hdr->rsvd4[0]);
	cmd->stat_sn		= conn->stat_sn++;
	hdr->statsn		= cpu_to_be32(cmd->stat_sn);
	hdr->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);
	hdr->async_event	= ISCSI_ASYNC_MSG_DROPPING_CONNECTION;
	hdr->param1		= cpu_to_be16(cmd->logout_cid);
	hdr->param2		= cpu_to_be16(conn->sess->sess_ops->DefaultTime2Wait);
	hdr->param3		= cpu_to_be16(conn->sess->sess_ops->DefaultTime2Retain);

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, hdr,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		cmd->tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32C HeaderDigest to"
			" Async Message 0x%08x\n", *header_digest);
	}

	cmd->iov_misc[0].iov_base	= cmd->pdu;
	cmd->iov_misc[0].iov_len	= cmd->tx_size;
	cmd->iov_misc_count		= 1;

	pr_debug("Sending Connection Dropped Async Message StatSN:"
		" 0x%08x, for CID: %hu on CID: %hu\n", cmd->stat_sn,
			cmd->logout_cid, conn->cid);
	return 0;
}

static void iscsit_tx_thread_wait_for_tcp(struct iscsi_conn *conn)
{
	if ((conn->sock->sk->sk_shutdown & SEND_SHUTDOWN) ||
	    (conn->sock->sk->sk_shutdown & RCV_SHUTDOWN)) {
		wait_for_completion_interruptible_timeout(
					&conn->tx_half_close_comp,
					ISCSI_TX_THREAD_TCP_TIMEOUT * HZ);
	}
}

static void
iscsit_build_datain_pdu(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
			struct iscsi_datain *datain, struct iscsi_data_rsp *hdr,
			bool set_statsn)
{
	hdr->opcode		= ISCSI_OP_SCSI_DATA_IN;
	hdr->flags		= datain->flags;
	if (hdr->flags & ISCSI_FLAG_DATA_STATUS) {
		if (cmd->se_cmd.se_cmd_flags & SCF_OVERFLOW_BIT) {
			hdr->flags |= ISCSI_FLAG_DATA_OVERFLOW;
			hdr->residual_count = cpu_to_be32(cmd->se_cmd.residual_count);
		} else if (cmd->se_cmd.se_cmd_flags & SCF_UNDERFLOW_BIT) {
			hdr->flags |= ISCSI_FLAG_DATA_UNDERFLOW;
			hdr->residual_count = cpu_to_be32(cmd->se_cmd.residual_count);
		}
	}
	hton24(hdr->dlength, datain->length);
	if (hdr->flags & ISCSI_FLAG_DATA_ACK)
		int_to_scsilun(cmd->se_cmd.orig_fe_lun,
				(struct scsi_lun *)&hdr->lun);
	else
		put_unaligned_le64(0xFFFFFFFFFFFFFFFFULL, &hdr->lun);

	hdr->itt		= cmd->init_task_tag;

	if (hdr->flags & ISCSI_FLAG_DATA_ACK)
		hdr->ttt		= cpu_to_be32(cmd->targ_xfer_tag);
	else
		hdr->ttt		= cpu_to_be32(0xFFFFFFFF);
	if (set_statsn)
		hdr->statsn		= cpu_to_be32(cmd->stat_sn);
	else
		hdr->statsn		= cpu_to_be32(0xFFFFFFFF);

	hdr->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);
	hdr->datasn		= cpu_to_be32(datain->data_sn);
	hdr->offset		= cpu_to_be32(datain->offset);

	pr_debug("Built DataIN ITT: 0x%08x, StatSN: 0x%08x,"
		" DataSN: 0x%08x, Offset: %u, Length: %u, CID: %hu\n",
		cmd->init_task_tag, ntohl(hdr->statsn), ntohl(hdr->datasn),
		ntohl(hdr->offset), datain->length, conn->cid);
}

static int iscsit_send_datain(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct iscsi_data_rsp *hdr = (struct iscsi_data_rsp *)&cmd->pdu[0];
	struct iscsi_datain datain;
	struct iscsi_datain_req *dr;
	struct kvec *iov;
	u32 iov_count = 0, tx_size = 0;
	int eodr = 0, ret, iov_ret;
	bool set_statsn = false;

	memset(&datain, 0, sizeof(struct iscsi_datain));
	dr = iscsit_get_datain_values(cmd, &datain);
	if (!dr) {
		pr_err("iscsit_get_datain_values failed for ITT: 0x%08x\n",
				cmd->init_task_tag);
		return -1;
	}
	/*
	 * Be paranoid and double check the logic for now.
	 */
	if ((datain.offset + datain.length) > cmd->se_cmd.data_length) {
		pr_err("Command ITT: 0x%08x, datain.offset: %u and"
			" datain.length: %u exceeds cmd->data_length: %u\n",
			cmd->init_task_tag, datain.offset, datain.length,
			cmd->se_cmd.data_length);
		return -1;
	}

	spin_lock_bh(&conn->sess->session_stats_lock);
	conn->sess->tx_data_octets += datain.length;
	if (conn->sess->se_sess->se_node_acl) {
		spin_lock(&conn->sess->se_sess->se_node_acl->stats_lock);
		conn->sess->se_sess->se_node_acl->read_bytes += datain.length;
		spin_unlock(&conn->sess->se_sess->se_node_acl->stats_lock);
	}
	spin_unlock_bh(&conn->sess->session_stats_lock);
	/*
	 * Special case for successfully execution w/ both DATAIN
	 * and Sense Data.
	 */
	if ((datain.flags & ISCSI_FLAG_DATA_STATUS) &&
	    (cmd->se_cmd.se_cmd_flags & SCF_TRANSPORT_TASK_SENSE))
		datain.flags &= ~ISCSI_FLAG_DATA_STATUS;
	else {
		if ((dr->dr_complete == DATAIN_COMPLETE_NORMAL) ||
		    (dr->dr_complete == DATAIN_COMPLETE_CONNECTION_RECOVERY)) {
			iscsit_increment_maxcmdsn(cmd, conn->sess);
			cmd->stat_sn = conn->stat_sn++;
			set_statsn = true;
		} else if (dr->dr_complete ==
			   DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY)
			set_statsn = true;
	}

	iscsit_build_datain_pdu(cmd, conn, &datain, hdr, set_statsn);

	iov = &cmd->iov_data[0];
	iov[iov_count].iov_base	= cmd->pdu;
	iov[iov_count++].iov_len	= ISCSI_HDR_LEN;
	tx_size += ISCSI_HDR_LEN;

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, cmd->pdu,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		iov[0].iov_len += ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;

		pr_debug("Attaching CRC32 HeaderDigest"
			" for DataIN PDU 0x%08x\n", *header_digest);
	}

	iov_ret = iscsit_map_iovec(cmd, &cmd->iov_data[1],
				datain.offset, datain.length);
	if (iov_ret < 0)
		return -1;

	iov_count += iov_ret;
	tx_size += datain.length;

	cmd->padding = ((-datain.length) & 3);
	if (cmd->padding) {
		iov[iov_count].iov_base		= cmd->pad_bytes;
		iov[iov_count++].iov_len	= cmd->padding;
		tx_size += cmd->padding;

		pr_debug("Attaching %u padding bytes\n",
				cmd->padding);
	}
	if (conn->conn_ops->DataDigest) {
		cmd->data_crc = iscsit_do_crypto_hash_sg(&conn->conn_tx_hash, cmd,
			 datain.offset, datain.length, cmd->padding, cmd->pad_bytes);

		iov[iov_count].iov_base	= &cmd->data_crc;
		iov[iov_count++].iov_len = ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;

		pr_debug("Attached CRC32C DataDigest %d bytes, crc"
			" 0x%08x\n", datain.length+cmd->padding, cmd->data_crc);
	}

	cmd->iov_data_count = iov_count;
	cmd->tx_size = tx_size;

	/* sendpage is preferred but can't insert markers */
	if (!conn->conn_ops->IFMarker)
		ret = iscsit_fe_sendpage_sg(cmd, conn);
	else
		ret = iscsit_send_tx_data(cmd, conn, 0);

	iscsit_unmap_iovec(cmd);

	if (ret < 0) {
		iscsit_tx_thread_wait_for_tcp(conn);
		return ret;
	}

	if (dr->dr_complete) {
		eodr = (cmd->se_cmd.se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) ?
				2 : 1;
		iscsit_free_datain_req(cmd, dr);
	}

	return eodr;
}

int
iscsit_build_logout_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
			struct iscsi_logout_rsp *hdr)
{
	struct iscsi_conn *logout_conn = NULL;
	struct iscsi_conn_recovery *cr = NULL;
	struct iscsi_session *sess = conn->sess;
	/*
	 * The actual shutting down of Sessions and/or Connections
	 * for CLOSESESSION and CLOSECONNECTION Logout Requests
	 * is done in scsi_logout_post_handler().
	 */
	switch (cmd->logout_reason) {
	case ISCSI_LOGOUT_REASON_CLOSE_SESSION:
		pr_debug("iSCSI session logout successful, setting"
			" logout response to ISCSI_LOGOUT_SUCCESS.\n");
		cmd->logout_response = ISCSI_LOGOUT_SUCCESS;
		break;
	case ISCSI_LOGOUT_REASON_CLOSE_CONNECTION:
		if (cmd->logout_response == ISCSI_LOGOUT_CID_NOT_FOUND)
			break;
		/*
		 * For CLOSECONNECTION logout requests carrying
		 * a matching logout CID -> local CID, the reference
		 * for the local CID will have been incremented in
		 * iscsi_logout_closeconnection().
		 *
		 * For CLOSECONNECTION logout requests carrying
		 * a different CID than the connection it arrived
		 * on, the connection responding to cmd->logout_cid
		 * is stopped in iscsit_logout_post_handler_diffcid().
		 */

		pr_debug("iSCSI CID: %hu logout on CID: %hu"
			" successful.\n", cmd->logout_cid, conn->cid);
		cmd->logout_response = ISCSI_LOGOUT_SUCCESS;
		break;
	case ISCSI_LOGOUT_REASON_RECOVERY:
		if ((cmd->logout_response == ISCSI_LOGOUT_RECOVERY_UNSUPPORTED) ||
		    (cmd->logout_response == ISCSI_LOGOUT_CLEANUP_FAILED))
			break;
		/*
		 * If the connection is still active from our point of view
		 * force connection recovery to occur.
		 */
		logout_conn = iscsit_get_conn_from_cid_rcfr(sess,
				cmd->logout_cid);
		if (logout_conn) {
			iscsit_connection_reinstatement_rcfr(logout_conn);
			iscsit_dec_conn_usage_count(logout_conn);
		}

		cr = iscsit_get_inactive_connection_recovery_entry(
				conn->sess, cmd->logout_cid);
		if (!cr) {
			pr_err("Unable to locate CID: %hu for"
			" REMOVECONNFORRECOVERY Logout Request.\n",
				cmd->logout_cid);
			cmd->logout_response = ISCSI_LOGOUT_CID_NOT_FOUND;
			break;
		}

		iscsit_discard_cr_cmds_by_expstatsn(cr, cmd->exp_stat_sn);

		pr_debug("iSCSI REMOVECONNFORRECOVERY logout"
			" for recovery for CID: %hu on CID: %hu successful.\n",
				cmd->logout_cid, conn->cid);
		cmd->logout_response = ISCSI_LOGOUT_SUCCESS;
		break;
	default:
		pr_err("Unknown cmd->logout_reason: 0x%02x\n",
				cmd->logout_reason);
		return -1;
	}

	hdr->opcode		= ISCSI_OP_LOGOUT_RSP;
	hdr->flags		|= ISCSI_FLAG_CMD_FINAL;
	hdr->response		= cmd->logout_response;
	hdr->itt		= cmd->init_task_tag;
	cmd->stat_sn		= conn->stat_sn++;
	hdr->statsn		= cpu_to_be32(cmd->stat_sn);

	iscsit_increment_maxcmdsn(cmd, conn->sess);
	hdr->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);

	pr_debug("Built Logout Response ITT: 0x%08x StatSN:"
		" 0x%08x Response: 0x%02x CID: %hu on CID: %hu\n",
		cmd->init_task_tag, cmd->stat_sn, hdr->response,
		cmd->logout_cid, conn->cid);

	return 0;
}
EXPORT_SYMBOL(iscsit_build_logout_rsp);

static int
iscsit_send_logout(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct kvec *iov;
	int niov = 0, tx_size, rc;

	rc = iscsit_build_logout_rsp(cmd, conn,
			(struct iscsi_logout_rsp *)&cmd->pdu[0]);
	if (rc < 0)
		return rc;

	tx_size = ISCSI_HDR_LEN;
	iov = &cmd->iov_misc[0];
	iov[niov].iov_base	= cmd->pdu;
	iov[niov++].iov_len	= ISCSI_HDR_LEN;

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, &cmd->pdu[0],
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		iov[0].iov_len += ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32C HeaderDigest to"
			" Logout Response 0x%08x\n", *header_digest);
	}
	cmd->iov_misc_count = niov;
	cmd->tx_size = tx_size;

	return 0;
}

void
iscsit_build_nopin_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
		       struct iscsi_nopin *hdr, bool nopout_response)
{
	hdr->opcode		= ISCSI_OP_NOOP_IN;
	hdr->flags		|= ISCSI_FLAG_CMD_FINAL;
        hton24(hdr->dlength, cmd->buf_ptr_size);
	if (nopout_response)
		put_unaligned_le64(0xFFFFFFFFFFFFFFFFULL, &hdr->lun);
	hdr->itt		= cmd->init_task_tag;
	hdr->ttt		= cpu_to_be32(cmd->targ_xfer_tag);
	cmd->stat_sn		= (nopout_response) ? conn->stat_sn++ :
				  conn->stat_sn;
	hdr->statsn		= cpu_to_be32(cmd->stat_sn);

	if (nopout_response)
		iscsit_increment_maxcmdsn(cmd, conn->sess);

	hdr->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);

	pr_debug("Built NOPIN %s Response ITT: 0x%08x, TTT: 0x%08x,"
		" StatSN: 0x%08x, Length %u\n", (nopout_response) ?
		"Solicitied" : "Unsolicitied", cmd->init_task_tag,
		cmd->targ_xfer_tag, cmd->stat_sn, cmd->buf_ptr_size);
}
EXPORT_SYMBOL(iscsit_build_nopin_rsp);

/*
 *	Unsolicited NOPIN, either requesting a response or not.
 */
static int iscsit_send_unsolicited_nopin(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn,
	int want_response)
{
	struct iscsi_nopin *hdr = (struct iscsi_nopin *)&cmd->pdu[0];
	int tx_size = ISCSI_HDR_LEN, ret;

	iscsit_build_nopin_rsp(cmd, conn, hdr, false);

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, hdr,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32C HeaderDigest to"
			" NopIN 0x%08x\n", *header_digest);
	}

	cmd->iov_misc[0].iov_base	= cmd->pdu;
	cmd->iov_misc[0].iov_len	= tx_size;
	cmd->iov_misc_count	= 1;
	cmd->tx_size		= tx_size;

	pr_debug("Sending Unsolicited NOPIN TTT: 0x%08x StatSN:"
		" 0x%08x CID: %hu\n", hdr->ttt, cmd->stat_sn, conn->cid);

	ret = iscsit_send_tx_data(cmd, conn, 1);
	if (ret < 0) {
		iscsit_tx_thread_wait_for_tcp(conn);
		return ret;
	}

	spin_lock_bh(&cmd->istate_lock);
	cmd->i_state = want_response ?
		ISTATE_SENT_NOPIN_WANT_RESPONSE : ISTATE_SENT_STATUS;
	spin_unlock_bh(&cmd->istate_lock);

	return 0;
}

static int
iscsit_send_nopin(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct iscsi_nopin *hdr = (struct iscsi_nopin *)&cmd->pdu[0];
	struct kvec *iov;
	u32 padding = 0;
	int niov = 0, tx_size;

	iscsit_build_nopin_rsp(cmd, conn, hdr, true);

	tx_size = ISCSI_HDR_LEN;
	iov = &cmd->iov_misc[0];
	iov[niov].iov_base	= cmd->pdu;
	iov[niov++].iov_len	= ISCSI_HDR_LEN;

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, hdr,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		iov[0].iov_len += ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32C HeaderDigest"
			" to NopIn 0x%08x\n", *header_digest);
	}

	/*
	 * NOPOUT Ping Data is attached to struct iscsi_cmd->buf_ptr.
	 * NOPOUT DataSegmentLength is at struct iscsi_cmd->buf_ptr_size.
	 */
	if (cmd->buf_ptr_size) {
		iov[niov].iov_base	= cmd->buf_ptr;
		iov[niov++].iov_len	= cmd->buf_ptr_size;
		tx_size += cmd->buf_ptr_size;

		pr_debug("Echoing back %u bytes of ping"
			" data.\n", cmd->buf_ptr_size);

		padding = ((-cmd->buf_ptr_size) & 3);
		if (padding != 0) {
			iov[niov].iov_base = &cmd->pad_bytes;
			iov[niov++].iov_len = padding;
			tx_size += padding;
			pr_debug("Attaching %u additional"
				" padding bytes.\n", padding);
		}
		if (conn->conn_ops->DataDigest) {
			iscsit_do_crypto_hash_buf(&conn->conn_tx_hash,
				cmd->buf_ptr, cmd->buf_ptr_size,
				padding, (u8 *)&cmd->pad_bytes,
				(u8 *)&cmd->data_crc);

			iov[niov].iov_base = &cmd->data_crc;
			iov[niov++].iov_len = ISCSI_CRC_LEN;
			tx_size += ISCSI_CRC_LEN;
			pr_debug("Attached DataDigest for %u"
				" bytes of ping data, CRC 0x%08x\n",
				cmd->buf_ptr_size, cmd->data_crc);
		}
	}

	cmd->iov_misc_count = niov;
	cmd->tx_size = tx_size;

	return 0;
}

static int iscsit_send_r2t(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	int tx_size = 0;
	struct iscsi_r2t *r2t;
	struct iscsi_r2t_rsp *hdr;
	int ret;

	r2t = iscsit_get_r2t_from_list(cmd);
	if (!r2t)
		return -1;

	hdr			= (struct iscsi_r2t_rsp *) cmd->pdu;
	memset(hdr, 0, ISCSI_HDR_LEN);
	hdr->opcode		= ISCSI_OP_R2T;
	hdr->flags		|= ISCSI_FLAG_CMD_FINAL;
	int_to_scsilun(cmd->se_cmd.orig_fe_lun,
			(struct scsi_lun *)&hdr->lun);
	hdr->itt		= cmd->init_task_tag;
	spin_lock_bh(&conn->sess->ttt_lock);
	r2t->targ_xfer_tag	= conn->sess->targ_xfer_tag++;
	if (r2t->targ_xfer_tag == 0xFFFFFFFF)
		r2t->targ_xfer_tag = conn->sess->targ_xfer_tag++;
	spin_unlock_bh(&conn->sess->ttt_lock);
	hdr->ttt		= cpu_to_be32(r2t->targ_xfer_tag);
	hdr->statsn		= cpu_to_be32(conn->stat_sn);
	hdr->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);
	hdr->r2tsn		= cpu_to_be32(r2t->r2t_sn);
	hdr->data_offset	= cpu_to_be32(r2t->offset);
	hdr->data_length	= cpu_to_be32(r2t->xfer_len);

	cmd->iov_misc[0].iov_base	= cmd->pdu;
	cmd->iov_misc[0].iov_len	= ISCSI_HDR_LEN;
	tx_size += ISCSI_HDR_LEN;

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, hdr,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		cmd->iov_misc[0].iov_len += ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32 HeaderDigest for R2T"
			" PDU 0x%08x\n", *header_digest);
	}

	pr_debug("Built %sR2T, ITT: 0x%08x, TTT: 0x%08x, StatSN:"
		" 0x%08x, R2TSN: 0x%08x, Offset: %u, DDTL: %u, CID: %hu\n",
		(!r2t->recovery_r2t) ? "" : "Recovery ", cmd->init_task_tag,
		r2t->targ_xfer_tag, ntohl(hdr->statsn), r2t->r2t_sn,
			r2t->offset, r2t->xfer_len, conn->cid);

	cmd->iov_misc_count = 1;
	cmd->tx_size = tx_size;

	spin_lock_bh(&cmd->r2t_lock);
	r2t->sent_r2t = 1;
	spin_unlock_bh(&cmd->r2t_lock);

	ret = iscsit_send_tx_data(cmd, conn, 1);
	if (ret < 0) {
		iscsit_tx_thread_wait_for_tcp(conn);
		return ret;
	}

	spin_lock_bh(&cmd->dataout_timeout_lock);
	iscsit_start_dataout_timer(cmd, conn);
	spin_unlock_bh(&cmd->dataout_timeout_lock);

	return 0;
}

/*
 *	@recovery: If called from iscsi_task_reassign_complete_write() for
 *		connection recovery.
 */
int iscsit_build_r2ts_for_cmd(
	struct iscsi_conn *conn,
	struct iscsi_cmd *cmd,
	bool recovery)
{
	int first_r2t = 1;
	u32 offset = 0, xfer_len = 0;

	spin_lock_bh(&cmd->r2t_lock);
	if (cmd->cmd_flags & ICF_SENT_LAST_R2T) {
		spin_unlock_bh(&cmd->r2t_lock);
		return 0;
	}

	if (conn->sess->sess_ops->DataSequenceInOrder &&
	    !recovery)
		cmd->r2t_offset = max(cmd->r2t_offset, cmd->write_data_done);

	while (cmd->outstanding_r2ts < conn->sess->sess_ops->MaxOutstandingR2T) {
		if (conn->sess->sess_ops->DataSequenceInOrder) {
			offset = cmd->r2t_offset;

			if (first_r2t && recovery) {
				int new_data_end = offset +
					conn->sess->sess_ops->MaxBurstLength -
					cmd->next_burst_len;

				if (new_data_end > cmd->se_cmd.data_length)
					xfer_len = cmd->se_cmd.data_length - offset;
				else
					xfer_len =
						conn->sess->sess_ops->MaxBurstLength -
						cmd->next_burst_len;
			} else {
				int new_data_end = offset +
					conn->sess->sess_ops->MaxBurstLength;

				if (new_data_end > cmd->se_cmd.data_length)
					xfer_len = cmd->se_cmd.data_length - offset;
				else
					xfer_len = conn->sess->sess_ops->MaxBurstLength;
			}
			cmd->r2t_offset += xfer_len;

			if (cmd->r2t_offset == cmd->se_cmd.data_length)
				cmd->cmd_flags |= ICF_SENT_LAST_R2T;
		} else {
			struct iscsi_seq *seq;

			seq = iscsit_get_seq_holder_for_r2t(cmd);
			if (!seq) {
				spin_unlock_bh(&cmd->r2t_lock);
				return -1;
			}

			offset = seq->offset;
			xfer_len = seq->xfer_len;

			if (cmd->seq_send_order == cmd->seq_count)
				cmd->cmd_flags |= ICF_SENT_LAST_R2T;
		}
		cmd->outstanding_r2ts++;
		first_r2t = 0;

		if (iscsit_add_r2t_to_list(cmd, offset, xfer_len, 0, 0) < 0) {
			spin_unlock_bh(&cmd->r2t_lock);
			return -1;
		}

		if (cmd->cmd_flags & ICF_SENT_LAST_R2T)
			break;
	}
	spin_unlock_bh(&cmd->r2t_lock);

	return 0;
}

void iscsit_build_rsp_pdu(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
			bool inc_stat_sn, struct iscsi_scsi_rsp *hdr)
{
	if (inc_stat_sn)
		cmd->stat_sn = conn->stat_sn++;

	spin_lock_bh(&conn->sess->session_stats_lock);
	conn->sess->rsp_pdus++;
	spin_unlock_bh(&conn->sess->session_stats_lock);

	memset(hdr, 0, ISCSI_HDR_LEN);
	hdr->opcode		= ISCSI_OP_SCSI_CMD_RSP;
	hdr->flags		|= ISCSI_FLAG_CMD_FINAL;
	if (cmd->se_cmd.se_cmd_flags & SCF_OVERFLOW_BIT) {
		hdr->flags |= ISCSI_FLAG_CMD_OVERFLOW;
		hdr->residual_count = cpu_to_be32(cmd->se_cmd.residual_count);
	} else if (cmd->se_cmd.se_cmd_flags & SCF_UNDERFLOW_BIT) {
		hdr->flags |= ISCSI_FLAG_CMD_UNDERFLOW;
		hdr->residual_count = cpu_to_be32(cmd->se_cmd.residual_count);
	}
	hdr->response		= cmd->iscsi_response;
	hdr->cmd_status		= cmd->se_cmd.scsi_status;
	hdr->itt		= cmd->init_task_tag;
	hdr->statsn		= cpu_to_be32(cmd->stat_sn);

	iscsit_increment_maxcmdsn(cmd, conn->sess);
	hdr->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);

	pr_debug("Built SCSI Response, ITT: 0x%08x, StatSN: 0x%08x,"
		" Response: 0x%02x, SAM Status: 0x%02x, CID: %hu\n",
		cmd->init_task_tag, cmd->stat_sn, cmd->se_cmd.scsi_status,
		cmd->se_cmd.scsi_status, conn->cid);
}
EXPORT_SYMBOL(iscsit_build_rsp_pdu);

static int iscsit_send_response(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct iscsi_scsi_rsp *hdr = (struct iscsi_scsi_rsp *)&cmd->pdu[0];
	struct kvec *iov;
	u32 padding = 0, tx_size = 0;
	int iov_count = 0;
	bool inc_stat_sn = (cmd->i_state == ISTATE_SEND_STATUS);

	iscsit_build_rsp_pdu(cmd, conn, inc_stat_sn, hdr);

	iov = &cmd->iov_misc[0];
	iov[iov_count].iov_base	= cmd->pdu;
	iov[iov_count++].iov_len = ISCSI_HDR_LEN;
	tx_size += ISCSI_HDR_LEN;

	/*
	 * Attach SENSE DATA payload to iSCSI Response PDU
	 */
	if (cmd->se_cmd.sense_buffer &&
	   ((cmd->se_cmd.se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) ||
	    (cmd->se_cmd.se_cmd_flags & SCF_EMULATED_TASK_SENSE))) {
		put_unaligned_be16(cmd->se_cmd.scsi_sense_length, cmd->sense_buffer);
		cmd->se_cmd.scsi_sense_length += sizeof (__be16);

		padding		= -(cmd->se_cmd.scsi_sense_length) & 3;
		hton24(hdr->dlength, (u32)cmd->se_cmd.scsi_sense_length);
		iov[iov_count].iov_base	= cmd->sense_buffer;
		iov[iov_count++].iov_len =
				(cmd->se_cmd.scsi_sense_length + padding);
		tx_size += cmd->se_cmd.scsi_sense_length;

		if (padding) {
			memset(cmd->sense_buffer +
				cmd->se_cmd.scsi_sense_length, 0, padding);
			tx_size += padding;
			pr_debug("Adding %u bytes of padding to"
				" SENSE.\n", padding);
		}

		if (conn->conn_ops->DataDigest) {
			iscsit_do_crypto_hash_buf(&conn->conn_tx_hash,
				cmd->sense_buffer,
				(cmd->se_cmd.scsi_sense_length + padding),
				0, NULL, (u8 *)&cmd->data_crc);

			iov[iov_count].iov_base    = &cmd->data_crc;
			iov[iov_count++].iov_len     = ISCSI_CRC_LEN;
			tx_size += ISCSI_CRC_LEN;

			pr_debug("Attaching CRC32 DataDigest for"
				" SENSE, %u bytes CRC 0x%08x\n",
				(cmd->se_cmd.scsi_sense_length + padding),
				cmd->data_crc);
		}

		pr_debug("Attaching SENSE DATA: %u bytes to iSCSI"
				" Response PDU\n",
				cmd->se_cmd.scsi_sense_length);
	}

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, cmd->pdu,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		iov[0].iov_len += ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32 HeaderDigest for Response"
				" PDU 0x%08x\n", *header_digest);
	}

	cmd->iov_misc_count = iov_count;
	cmd->tx_size = tx_size;

	return 0;
}

static u8 iscsit_convert_tcm_tmr_rsp(struct se_tmr_req *se_tmr)
{
	switch (se_tmr->response) {
	case TMR_FUNCTION_COMPLETE:
		return ISCSI_TMF_RSP_COMPLETE;
	case TMR_TASK_DOES_NOT_EXIST:
		return ISCSI_TMF_RSP_NO_TASK;
	case TMR_LUN_DOES_NOT_EXIST:
		return ISCSI_TMF_RSP_NO_LUN;
	case TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED:
		return ISCSI_TMF_RSP_NOT_SUPPORTED;
	case TMR_FUNCTION_REJECTED:
	default:
		return ISCSI_TMF_RSP_REJECTED;
	}
}

void
iscsit_build_task_mgt_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
			  struct iscsi_tm_rsp *hdr)
{
	struct se_tmr_req *se_tmr = cmd->se_cmd.se_tmr_req;

	hdr->opcode		= ISCSI_OP_SCSI_TMFUNC_RSP;
	hdr->flags		= ISCSI_FLAG_CMD_FINAL;
	hdr->response		= iscsit_convert_tcm_tmr_rsp(se_tmr);
	hdr->itt		= cmd->init_task_tag;
	cmd->stat_sn		= conn->stat_sn++;
	hdr->statsn		= cpu_to_be32(cmd->stat_sn);

	iscsit_increment_maxcmdsn(cmd, conn->sess);
	hdr->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);

	pr_debug("Built Task Management Response ITT: 0x%08x,"
		" StatSN: 0x%08x, Response: 0x%02x, CID: %hu\n",
		cmd->init_task_tag, cmd->stat_sn, hdr->response, conn->cid);
}
EXPORT_SYMBOL(iscsit_build_task_mgt_rsp);

static int
iscsit_send_task_mgt_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct iscsi_tm_rsp *hdr = (struct iscsi_tm_rsp *)&cmd->pdu[0];
	u32 tx_size = 0;

	iscsit_build_task_mgt_rsp(cmd, conn, hdr);

	cmd->iov_misc[0].iov_base	= cmd->pdu;
	cmd->iov_misc[0].iov_len	= ISCSI_HDR_LEN;
	tx_size += ISCSI_HDR_LEN;

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, hdr,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		cmd->iov_misc[0].iov_len += ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32 HeaderDigest for Task"
			" Mgmt Response PDU 0x%08x\n", *header_digest);
	}

	cmd->iov_misc_count = 1;
	cmd->tx_size = tx_size;

	return 0;
}

static bool iscsit_check_inaddr_any(struct iscsi_np *np)
{
	bool ret = false;

	if (np->np_sockaddr.ss_family == AF_INET6) {
		const struct sockaddr_in6 sin6 = {
			.sin6_addr = IN6ADDR_ANY_INIT };
		struct sockaddr_in6 *sock_in6 =
			 (struct sockaddr_in6 *)&np->np_sockaddr;

		if (!memcmp(sock_in6->sin6_addr.s6_addr,
				sin6.sin6_addr.s6_addr, 16))
			ret = true;
	} else {
		struct sockaddr_in * sock_in =
			(struct sockaddr_in *)&np->np_sockaddr;

		if (sock_in->sin_addr.s_addr == htonl(INADDR_ANY))
			ret = true;
	}

	return ret;
}

#define SENDTARGETS_BUF_LIMIT 32768U

static int iscsit_build_sendtargets_response(struct iscsi_cmd *cmd)
{
	char *payload = NULL;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_portal_group *tpg;
	struct iscsi_tiqn *tiqn;
	struct iscsi_tpg_np *tpg_np;
	int buffer_len, end_of_buf = 0, len = 0, payload_len = 0;
	unsigned char buf[ISCSI_IQN_LEN+12]; /* iqn + "TargetName=" + \0 */
	unsigned char *text_in = cmd->text_in_ptr, *text_ptr = NULL;

	buffer_len = max(conn->conn_ops->MaxRecvDataSegmentLength,
			 SENDTARGETS_BUF_LIMIT);

	payload = kzalloc(buffer_len, GFP_KERNEL);
	if (!payload) {
		pr_err("Unable to allocate memory for sendtargets"
				" response.\n");
		return -ENOMEM;
	}
	/*
	 * Locate pointer to iqn./eui. string for IFC_SENDTARGETS_SINGLE
	 * explicit case..
	 */
	if (cmd->cmd_flags & IFC_SENDTARGETS_SINGLE) {
		text_ptr = strchr(text_in, '=');
		if (!text_ptr) {
			pr_err("Unable to locate '=' string in text_in:"
			       " %s\n", text_in);
			kfree(payload);
			return -EINVAL;
		}
		/*
		 * Skip over '=' character..
		 */
		text_ptr += 1;
	}

	spin_lock(&tiqn_lock);
	list_for_each_entry(tiqn, &g_tiqn_list, tiqn_list) {
		if ((cmd->cmd_flags & IFC_SENDTARGETS_SINGLE) &&
		     strcmp(tiqn->tiqn, text_ptr)) {
			continue;
		}

		len = sprintf(buf, "TargetName=%s", tiqn->tiqn);
		len += 1;

		if ((len + payload_len) > buffer_len) {
			end_of_buf = 1;
			goto eob;
		}
		memcpy(payload + payload_len, buf, len);
		payload_len += len;

		spin_lock(&tiqn->tiqn_tpg_lock);
		list_for_each_entry(tpg, &tiqn->tiqn_tpg_list, tpg_list) {

			spin_lock(&tpg->tpg_state_lock);
			if ((tpg->tpg_state == TPG_STATE_FREE) ||
			    (tpg->tpg_state == TPG_STATE_INACTIVE)) {
				spin_unlock(&tpg->tpg_state_lock);
				continue;
			}
			spin_unlock(&tpg->tpg_state_lock);

			spin_lock(&tpg->tpg_np_lock);
			list_for_each_entry(tpg_np, &tpg->tpg_gnp_list,
						tpg_np_list) {
				struct iscsi_np *np = tpg_np->tpg_np;
				bool inaddr_any = iscsit_check_inaddr_any(np);

				len = sprintf(buf, "TargetAddress="
					"%s:%hu,%hu",
					(inaddr_any == false) ?
						np->np_ip : conn->local_ip,
					(inaddr_any == false) ?
						np->np_port : conn->local_port,
					tpg->tpgt);
				len += 1;

				if ((len + payload_len) > buffer_len) {
					spin_unlock(&tpg->tpg_np_lock);
					spin_unlock(&tiqn->tiqn_tpg_lock);
					end_of_buf = 1;
					goto eob;
				}
				memcpy(payload + payload_len, buf, len);
				payload_len += len;
			}
			spin_unlock(&tpg->tpg_np_lock);
		}
		spin_unlock(&tiqn->tiqn_tpg_lock);
eob:
		if (end_of_buf)
			break;

		if (cmd->cmd_flags & IFC_SENDTARGETS_SINGLE)
			break;
	}
	spin_unlock(&tiqn_lock);

	cmd->buf_ptr = payload;

	return payload_len;
}

int
iscsit_build_text_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
		      struct iscsi_text_rsp *hdr)
{
	int text_length, padding;

	text_length = iscsit_build_sendtargets_response(cmd);
	if (text_length < 0)
		return text_length;

	hdr->opcode = ISCSI_OP_TEXT_RSP;
	hdr->flags |= ISCSI_FLAG_CMD_FINAL;
	padding = ((-text_length) & 3);
	hton24(hdr->dlength, text_length);
	hdr->itt = cmd->init_task_tag;
	hdr->ttt = cpu_to_be32(cmd->targ_xfer_tag);
	cmd->stat_sn = conn->stat_sn++;
	hdr->statsn = cpu_to_be32(cmd->stat_sn);

	iscsit_increment_maxcmdsn(cmd, conn->sess);
	hdr->exp_cmdsn = cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn = cpu_to_be32(conn->sess->max_cmd_sn);

	pr_debug("Built Text Response: ITT: 0x%08x, StatSN: 0x%08x,"
		" Length: %u, CID: %hu\n", cmd->init_task_tag, cmd->stat_sn,
		text_length, conn->cid);

	return text_length + padding;
}
EXPORT_SYMBOL(iscsit_build_text_rsp);

/*
 *	FIXME: Add support for F_BIT and C_BIT when the length is longer than
 *	MaxRecvDataSegmentLength.
 */
static int iscsit_send_text_rsp(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	struct iscsi_text_rsp *hdr = (struct iscsi_text_rsp *)cmd->pdu;
	struct kvec *iov;
	u32 tx_size = 0;
	int text_length, iov_count = 0, rc;

	rc = iscsit_build_text_rsp(cmd, conn, hdr);
	if (rc < 0)
		return rc;

	text_length = rc;
	iov = &cmd->iov_misc[0];
	iov[iov_count].iov_base = cmd->pdu;
	iov[iov_count++].iov_len = ISCSI_HDR_LEN;
	iov[iov_count].iov_base	= cmd->buf_ptr;
	iov[iov_count++].iov_len = text_length;

	tx_size += (ISCSI_HDR_LEN + text_length);

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, hdr,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		iov[0].iov_len += ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32 HeaderDigest for"
			" Text Response PDU 0x%08x\n", *header_digest);
	}

	if (conn->conn_ops->DataDigest) {
		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash,
				cmd->buf_ptr, text_length,
				0, NULL, (u8 *)&cmd->data_crc);

		iov[iov_count].iov_base	= &cmd->data_crc;
		iov[iov_count++].iov_len = ISCSI_CRC_LEN;
		tx_size	+= ISCSI_CRC_LEN;

		pr_debug("Attaching DataDigest for %u bytes of text"
			" data, CRC 0x%08x\n", text_length,
			cmd->data_crc);
	}

	cmd->iov_misc_count = iov_count;
	cmd->tx_size = tx_size;

	return 0;
}

void
iscsit_build_reject(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
		    struct iscsi_reject *hdr)
{
	hdr->opcode		= ISCSI_OP_REJECT;
	hdr->reason		= cmd->reject_reason;
	hdr->flags		|= ISCSI_FLAG_CMD_FINAL;
	hton24(hdr->dlength, ISCSI_HDR_LEN);
	hdr->ffffffff		= cpu_to_be32(0xffffffff);
	cmd->stat_sn		= conn->stat_sn++;
	hdr->statsn		= cpu_to_be32(cmd->stat_sn);
	hdr->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	hdr->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);

}
EXPORT_SYMBOL(iscsit_build_reject);

static int iscsit_send_reject(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	struct iscsi_reject *hdr = (struct iscsi_reject *)&cmd->pdu[0];
	struct kvec *iov;
	u32 iov_count = 0, tx_size;

	iscsit_build_reject(cmd, conn, hdr);

	iov = &cmd->iov_misc[0];
	iov[iov_count].iov_base = cmd->pdu;
	iov[iov_count++].iov_len = ISCSI_HDR_LEN;
	iov[iov_count].iov_base = cmd->buf_ptr;
	iov[iov_count++].iov_len = ISCSI_HDR_LEN;

	tx_size = (ISCSI_HDR_LEN + ISCSI_HDR_LEN);

	if (conn->conn_ops->HeaderDigest) {
		u32 *header_digest = (u32 *)&cmd->pdu[ISCSI_HDR_LEN];

		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, hdr,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)header_digest);

		iov[0].iov_len += ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32 HeaderDigest for"
			" REJECT PDU 0x%08x\n", *header_digest);
	}

	if (conn->conn_ops->DataDigest) {
		iscsit_do_crypto_hash_buf(&conn->conn_tx_hash, cmd->buf_ptr,
				ISCSI_HDR_LEN, 0, NULL, (u8 *)&cmd->data_crc);

		iov[iov_count].iov_base = &cmd->data_crc;
		iov[iov_count++].iov_len  = ISCSI_CRC_LEN;
		tx_size += ISCSI_CRC_LEN;
		pr_debug("Attaching CRC32 DataDigest for REJECT"
				" PDU 0x%08x\n", cmd->data_crc);
	}

	cmd->iov_misc_count = iov_count;
	cmd->tx_size = tx_size;

	pr_debug("Built Reject PDU StatSN: 0x%08x, Reason: 0x%02x,"
		" CID: %hu\n", ntohl(hdr->statsn), hdr->reason, conn->cid);

	return 0;
}

void iscsit_thread_get_cpumask(struct iscsi_conn *conn)
{
	struct iscsi_thread_set *ts = conn->thread_set;
	int ord, cpu;
	/*
	 * thread_id is assigned from iscsit_global->ts_bitmap from
	 * within iscsi_thread_set.c:iscsi_allocate_thread_sets()
	 *
	 * Here we use thread_id to determine which CPU that this
	 * iSCSI connection's iscsi_thread_set will be scheduled to
	 * execute upon.
	 */
	ord = ts->thread_id % cpumask_weight(cpu_online_mask);
	for_each_online_cpu(cpu) {
		if (ord-- == 0) {
			cpumask_set_cpu(cpu, conn->conn_cpumask);
			return;
		}
	}
	/*
	 * This should never be reached..
	 */
	dump_stack();
	cpumask_setall(conn->conn_cpumask);
}

static inline void iscsit_thread_check_cpumask(
	struct iscsi_conn *conn,
	struct task_struct *p,
	int mode)
{
	char buf[128];
	/*
	 * mode == 1 signals iscsi_target_tx_thread() usage.
	 * mode == 0 signals iscsi_target_rx_thread() usage.
	 */
	if (mode == 1) {
		if (!conn->conn_tx_reset_cpumask)
			return;
		conn->conn_tx_reset_cpumask = 0;
	} else {
		if (!conn->conn_rx_reset_cpumask)
			return;
		conn->conn_rx_reset_cpumask = 0;
	}
	/*
	 * Update the CPU mask for this single kthread so that
	 * both TX and RX kthreads are scheduled to run on the
	 * same CPU.
	 */
	memset(buf, 0, 128);
	cpumask_scnprintf(buf, 128, conn->conn_cpumask);
	set_cpus_allowed_ptr(p, conn->conn_cpumask);
}

static int
iscsit_immediate_queue(struct iscsi_conn *conn, struct iscsi_cmd *cmd, int state)
{
	int ret;

	switch (state) {
	case ISTATE_SEND_R2T:
		ret = iscsit_send_r2t(cmd, conn);
		if (ret < 0)
			goto err;
		break;
	case ISTATE_REMOVE:
		spin_lock_bh(&conn->cmd_lock);
		list_del(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		iscsit_free_cmd(cmd, false);
		break;
	case ISTATE_SEND_NOPIN_WANT_RESPONSE:
		iscsit_mod_nopin_response_timer(conn);
		ret = iscsit_send_unsolicited_nopin(cmd, conn, 1);
		if (ret < 0)
			goto err;
		break;
	case ISTATE_SEND_NOPIN_NO_RESPONSE:
		ret = iscsit_send_unsolicited_nopin(cmd, conn, 0);
		if (ret < 0)
			goto err;
		break;
	default:
		pr_err("Unknown Opcode: 0x%02x ITT:"
		       " 0x%08x, i_state: %d on CID: %hu\n",
		       cmd->iscsi_opcode, cmd->init_task_tag, state,
		       conn->cid);
		goto err;
	}

	return 0;

err:
	return -1;
}

static int
iscsit_handle_immediate_queue(struct iscsi_conn *conn)
{
	struct iscsit_transport *t = conn->conn_transport;
	struct iscsi_queue_req *qr;
	struct iscsi_cmd *cmd;
	u8 state;
	int ret;

	while ((qr = iscsit_get_cmd_from_immediate_queue(conn))) {
		atomic_set(&conn->check_immediate_queue, 0);
		cmd = qr->cmd;
		state = qr->state;
		kmem_cache_free(lio_qr_cache, qr);

		ret = t->iscsit_immediate_queue(conn, cmd, state);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
iscsit_response_queue(struct iscsi_conn *conn, struct iscsi_cmd *cmd, int state)
{
	int ret;

check_rsp_state:
	switch (state) {
	case ISTATE_SEND_DATAIN:
		ret = iscsit_send_datain(cmd, conn);
		if (ret < 0)
			goto err;
		else if (!ret)
			/* more drs */
			goto check_rsp_state;
		else if (ret == 1) {
			/* all done */
			spin_lock_bh(&cmd->istate_lock);
			cmd->i_state = ISTATE_SENT_STATUS;
			spin_unlock_bh(&cmd->istate_lock);

			if (atomic_read(&conn->check_immediate_queue))
				return 1;

			return 0;
		} else if (ret == 2) {
			/* Still must send status,
			   SCF_TRANSPORT_TASK_SENSE was set */
			spin_lock_bh(&cmd->istate_lock);
			cmd->i_state = ISTATE_SEND_STATUS;
			spin_unlock_bh(&cmd->istate_lock);
			state = ISTATE_SEND_STATUS;
			goto check_rsp_state;
		}

		break;
	case ISTATE_SEND_STATUS:
	case ISTATE_SEND_STATUS_RECOVERY:
		ret = iscsit_send_response(cmd, conn);
		break;
	case ISTATE_SEND_LOGOUTRSP:
		ret = iscsit_send_logout(cmd, conn);
		break;
	case ISTATE_SEND_ASYNCMSG:
		ret = iscsit_send_conn_drop_async_message(
			cmd, conn);
		break;
	case ISTATE_SEND_NOPIN:
		ret = iscsit_send_nopin(cmd, conn);
		break;
	case ISTATE_SEND_REJECT:
		ret = iscsit_send_reject(cmd, conn);
		break;
	case ISTATE_SEND_TASKMGTRSP:
		ret = iscsit_send_task_mgt_rsp(cmd, conn);
		if (ret != 0)
			break;
		ret = iscsit_tmr_post_handler(cmd, conn);
		if (ret != 0)
			iscsit_fall_back_to_erl0(conn->sess);
		break;
	case ISTATE_SEND_TEXTRSP:
		ret = iscsit_send_text_rsp(cmd, conn);
		break;
	default:
		pr_err("Unknown Opcode: 0x%02x ITT:"
		       " 0x%08x, i_state: %d on CID: %hu\n",
		       cmd->iscsi_opcode, cmd->init_task_tag,
		       state, conn->cid);
		goto err;
	}
	if (ret < 0)
		goto err;

	if (iscsit_send_tx_data(cmd, conn, 1) < 0) {
		iscsit_tx_thread_wait_for_tcp(conn);
		iscsit_unmap_iovec(cmd);
		goto err;
	}
	iscsit_unmap_iovec(cmd);

	switch (state) {
	case ISTATE_SEND_LOGOUTRSP:
		if (!iscsit_logout_post_handler(cmd, conn))
			goto restart;
		/* fall through */
	case ISTATE_SEND_STATUS:
	case ISTATE_SEND_ASYNCMSG:
	case ISTATE_SEND_NOPIN:
	case ISTATE_SEND_STATUS_RECOVERY:
	case ISTATE_SEND_TEXTRSP:
	case ISTATE_SEND_TASKMGTRSP:
	case ISTATE_SEND_REJECT:
		spin_lock_bh(&cmd->istate_lock);
		cmd->i_state = ISTATE_SENT_STATUS;
		spin_unlock_bh(&cmd->istate_lock);
		break;
	default:
		pr_err("Unknown Opcode: 0x%02x ITT:"
		       " 0x%08x, i_state: %d on CID: %hu\n",
		       cmd->iscsi_opcode, cmd->init_task_tag,
		       cmd->i_state, conn->cid);
		goto err;
	}

	if (atomic_read(&conn->check_immediate_queue))
		return 1;

	return 0;

err:
	return -1;
restart:
	return -EAGAIN;
}

static int iscsit_handle_response_queue(struct iscsi_conn *conn)
{
	struct iscsit_transport *t = conn->conn_transport;
	struct iscsi_queue_req *qr;
	struct iscsi_cmd *cmd;
	u8 state;
	int ret;

	while ((qr = iscsit_get_cmd_from_response_queue(conn))) {
		cmd = qr->cmd;
		state = qr->state;
		kmem_cache_free(lio_qr_cache, qr);

		ret = t->iscsit_response_queue(conn, cmd, state);
		if (ret == 1 || ret < 0)
			return ret;
	}

	return 0;
}

int iscsi_target_tx_thread(void *arg)
{
	int ret = 0;
	struct iscsi_conn *conn;
	struct iscsi_thread_set *ts = arg;
	/*
	 * Allow ourselves to be interrupted by SIGINT so that a
	 * connection recovery / failure event can be triggered externally.
	 */
	allow_signal(SIGINT);

restart:
	conn = iscsi_tx_thread_pre_handler(ts);
	if (!conn)
		goto out;

	ret = 0;

	while (!kthread_should_stop()) {
		/*
		 * Ensure that both TX and RX per connection kthreads
		 * are scheduled to run on the same CPU.
		 */
		iscsit_thread_check_cpumask(conn, current, 1);

		wait_event_interruptible(conn->queues_wq,
					 !iscsit_conn_all_queues_empty(conn) ||
					 ts->status == ISCSI_THREAD_SET_RESET);

		if ((ts->status == ISCSI_THREAD_SET_RESET) ||
		     signal_pending(current))
			goto transport_err;

get_immediate:
		ret = iscsit_handle_immediate_queue(conn);
		if (ret < 0)
			goto transport_err;

		ret = iscsit_handle_response_queue(conn);
		if (ret == 1)
			goto get_immediate;
		else if (ret == -EAGAIN)
			goto restart;
		else if (ret < 0)
			goto transport_err;
	}

transport_err:
	iscsit_take_action_for_connection_exit(conn);
	goto restart;
out:
	return 0;
}

static int iscsi_target_rx_opcode(struct iscsi_conn *conn, unsigned char *buf)
{
	struct iscsi_hdr *hdr = (struct iscsi_hdr *)buf;
	struct iscsi_cmd *cmd;
	int ret = 0;

	switch (hdr->opcode & ISCSI_OPCODE_MASK) {
	case ISCSI_OP_SCSI_CMD:
		cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			goto reject;

		ret = iscsit_handle_scsi_cmd(conn, cmd, buf);
		break;
	case ISCSI_OP_SCSI_DATA_OUT:
		ret = iscsit_handle_data_out(conn, buf);
		break;
	case ISCSI_OP_NOOP_OUT:
		cmd = NULL;
		if (hdr->ttt == cpu_to_be32(0xFFFFFFFF)) {
			cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
			if (!cmd)
				goto reject;
		}
		ret = iscsit_handle_nop_out(conn, cmd, buf);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			goto reject;

		ret = iscsit_handle_task_mgt_cmd(conn, cmd, buf);
		break;
	case ISCSI_OP_TEXT:
		cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			goto reject;

		ret = iscsit_handle_text_cmd(conn, cmd, buf);
		break;
	case ISCSI_OP_LOGOUT:
		cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			goto reject;

		ret = iscsit_handle_logout_cmd(conn, cmd, buf);
		if (ret > 0)
			wait_for_completion_timeout(&conn->conn_logout_comp,
					SECONDS_FOR_LOGOUT_COMP * HZ);
		break;
	case ISCSI_OP_SNACK:
		ret = iscsit_handle_snack(conn, buf);
		break;
	default:
		pr_err("Got unknown iSCSI OpCode: 0x%02x\n", hdr->opcode);
		if (!conn->sess->sess_ops->ErrorRecoveryLevel) {
			pr_err("Cannot recover from unknown"
			" opcode while ERL=0, closing iSCSI connection.\n");
			return -1;
		}
		if (!conn->conn_ops->OFMarker) {
			pr_err("Unable to recover from unknown"
			" opcode while OFMarker=No, closing iSCSI"
				" connection.\n");
			return -1;
		}
		if (iscsit_recover_from_unknown_opcode(conn) < 0) {
			pr_err("Unable to recover from unknown"
				" opcode, closing iSCSI connection.\n");
			return -1;
		}
		break;
	}

	return ret;
reject:
	return iscsit_add_reject(conn, ISCSI_REASON_BOOKMARK_NO_RESOURCES, buf);
}

int iscsi_target_rx_thread(void *arg)
{
	int ret;
	u8 buffer[ISCSI_HDR_LEN], opcode;
	u32 checksum = 0, digest = 0;
	struct iscsi_conn *conn = NULL;
	struct iscsi_thread_set *ts = arg;
	struct kvec iov;
	/*
	 * Allow ourselves to be interrupted by SIGINT so that a
	 * connection recovery / failure event can be triggered externally.
	 */
	allow_signal(SIGINT);

restart:
	conn = iscsi_rx_thread_pre_handler(ts);
	if (!conn)
		goto out;

	if (conn->conn_transport->transport_type == ISCSI_INFINIBAND) {
		struct completion comp;
		int rc;

		init_completion(&comp);
		rc = wait_for_completion_interruptible(&comp);
		if (rc < 0)
			goto transport_err;

		goto out;
	}

	while (!kthread_should_stop()) {
		/*
		 * Ensure that both TX and RX per connection kthreads
		 * are scheduled to run on the same CPU.
		 */
		iscsit_thread_check_cpumask(conn, current, 0);

		memset(buffer, 0, ISCSI_HDR_LEN);
		memset(&iov, 0, sizeof(struct kvec));

		iov.iov_base	= buffer;
		iov.iov_len	= ISCSI_HDR_LEN;

		ret = rx_data(conn, &iov, 1, ISCSI_HDR_LEN);
		if (ret != ISCSI_HDR_LEN) {
			iscsit_rx_thread_wait_for_tcp(conn);
			goto transport_err;
		}

		if (conn->conn_ops->HeaderDigest) {
			iov.iov_base	= &digest;
			iov.iov_len	= ISCSI_CRC_LEN;

			ret = rx_data(conn, &iov, 1, ISCSI_CRC_LEN);
			if (ret != ISCSI_CRC_LEN) {
				iscsit_rx_thread_wait_for_tcp(conn);
				goto transport_err;
			}

			iscsit_do_crypto_hash_buf(&conn->conn_rx_hash,
					buffer, ISCSI_HDR_LEN,
					0, NULL, (u8 *)&checksum);

			if (digest != checksum) {
				pr_err("HeaderDigest CRC32C failed,"
					" received 0x%08x, computed 0x%08x\n",
					digest, checksum);
				/*
				 * Set the PDU to 0xff so it will intentionally
				 * hit default in the switch below.
				 */
				memset(buffer, 0xff, ISCSI_HDR_LEN);
				spin_lock_bh(&conn->sess->session_stats_lock);
				conn->sess->conn_digest_errors++;
				spin_unlock_bh(&conn->sess->session_stats_lock);
			} else {
				pr_debug("Got HeaderDigest CRC32C"
						" 0x%08x\n", checksum);
			}
		}

		if (conn->conn_state == TARG_CONN_STATE_IN_LOGOUT)
			goto transport_err;

		opcode = buffer[0] & ISCSI_OPCODE_MASK;

		if (conn->sess->sess_ops->SessionType &&
		   ((!(opcode & ISCSI_OP_TEXT)) ||
		    (!(opcode & ISCSI_OP_LOGOUT)))) {
			pr_err("Received illegal iSCSI Opcode: 0x%02x"
			" while in Discovery Session, rejecting.\n", opcode);
			iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
					  buffer);
			goto transport_err;
		}

		ret = iscsi_target_rx_opcode(conn, buffer);
		if (ret < 0)
			goto transport_err;
	}

transport_err:
	if (!signal_pending(current))
		atomic_set(&conn->transport_failed, 1);
	iscsit_take_action_for_connection_exit(conn);
	goto restart;
out:
	return 0;
}

static void iscsit_release_commands_from_conn(struct iscsi_conn *conn)
{
	struct iscsi_cmd *cmd = NULL, *cmd_tmp = NULL;
	struct iscsi_session *sess = conn->sess;
	/*
	 * We expect this function to only ever be called from either RX or TX
	 * thread context via iscsit_close_connection() once the other context
	 * has been reset -> returned sleeping pre-handler state.
	 */
	spin_lock_bh(&conn->cmd_lock);
	list_for_each_entry_safe(cmd, cmd_tmp, &conn->conn_cmd_list, i_conn_node) {

		list_del(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		iscsit_increment_maxcmdsn(cmd, sess);

		iscsit_free_cmd(cmd, true);

		spin_lock_bh(&conn->cmd_lock);
	}
	spin_unlock_bh(&conn->cmd_lock);
}

static void iscsit_stop_timers_for_cmds(
	struct iscsi_conn *conn)
{
	struct iscsi_cmd *cmd;

	spin_lock_bh(&conn->cmd_lock);
	list_for_each_entry(cmd, &conn->conn_cmd_list, i_conn_node) {
		if (cmd->data_direction == DMA_TO_DEVICE)
			iscsit_stop_dataout_timer(cmd);
	}
	spin_unlock_bh(&conn->cmd_lock);
}

int iscsit_close_connection(
	struct iscsi_conn *conn)
{
	int conn_logout = (conn->conn_state == TARG_CONN_STATE_IN_LOGOUT);
	struct iscsi_session	*sess = conn->sess;

	pr_debug("Closing iSCSI connection CID %hu on SID:"
		" %u\n", conn->cid, sess->sid);
	/*
	 * Always up conn_logout_comp just in case the RX Thread is sleeping
	 * and the logout response never got sent because the connection
	 * failed.
	 */
	complete(&conn->conn_logout_comp);

	iscsi_release_thread_set(conn);

	iscsit_stop_timers_for_cmds(conn);
	iscsit_stop_nopin_response_timer(conn);
	iscsit_stop_nopin_timer(conn);
	iscsit_free_queue_reqs_for_conn(conn);

	/*
	 * During Connection recovery drop unacknowledged out of order
	 * commands for this connection, and prepare the other commands
	 * for realligence.
	 *
	 * During normal operation clear the out of order commands (but
	 * do not free the struct iscsi_ooo_cmdsn's) and release all
	 * struct iscsi_cmds.
	 */
	if (atomic_read(&conn->connection_recovery)) {
		iscsit_discard_unacknowledged_ooo_cmdsns_for_conn(conn);
		iscsit_prepare_cmds_for_realligance(conn);
	} else {
		iscsit_clear_ooo_cmdsns_for_conn(conn);
		iscsit_release_commands_from_conn(conn);
	}

	/*
	 * Handle decrementing session or connection usage count if
	 * a logout response was not able to be sent because the
	 * connection failed.  Fall back to Session Recovery here.
	 */
	if (atomic_read(&conn->conn_logout_remove)) {
		if (conn->conn_logout_reason == ISCSI_LOGOUT_REASON_CLOSE_SESSION) {
			iscsit_dec_conn_usage_count(conn);
			iscsit_dec_session_usage_count(sess);
		}
		if (conn->conn_logout_reason == ISCSI_LOGOUT_REASON_CLOSE_CONNECTION)
			iscsit_dec_conn_usage_count(conn);

		atomic_set(&conn->conn_logout_remove, 0);
		atomic_set(&sess->session_reinstatement, 0);
		atomic_set(&sess->session_fall_back_to_erl0, 1);
	}

	spin_lock_bh(&sess->conn_lock);
	list_del(&conn->conn_list);

	/*
	 * Attempt to let the Initiator know this connection failed by
	 * sending an Connection Dropped Async Message on another
	 * active connection.
	 */
	if (atomic_read(&conn->connection_recovery))
		iscsit_build_conn_drop_async_message(conn);

	spin_unlock_bh(&sess->conn_lock);

	/*
	 * If connection reinstatement is being performed on this connection,
	 * up the connection reinstatement semaphore that is being blocked on
	 * in iscsit_cause_connection_reinstatement().
	 */
	spin_lock_bh(&conn->state_lock);
	if (atomic_read(&conn->sleep_on_conn_wait_comp)) {
		spin_unlock_bh(&conn->state_lock);
		complete(&conn->conn_wait_comp);
		wait_for_completion(&conn->conn_post_wait_comp);
		spin_lock_bh(&conn->state_lock);
	}

	/*
	 * If connection reinstatement is being performed on this connection
	 * by receiving a REMOVECONNFORRECOVERY logout request, up the
	 * connection wait rcfr semaphore that is being blocked on
	 * an iscsit_connection_reinstatement_rcfr().
	 */
	if (atomic_read(&conn->connection_wait_rcfr)) {
		spin_unlock_bh(&conn->state_lock);
		complete(&conn->conn_wait_rcfr_comp);
		wait_for_completion(&conn->conn_post_wait_comp);
		spin_lock_bh(&conn->state_lock);
	}
	atomic_set(&conn->connection_reinstatement, 1);
	spin_unlock_bh(&conn->state_lock);

	/*
	 * If any other processes are accessing this connection pointer we
	 * must wait until they have completed.
	 */
	iscsit_check_conn_usage_count(conn);

	if (conn->conn_rx_hash.tfm)
		crypto_free_hash(conn->conn_rx_hash.tfm);
	if (conn->conn_tx_hash.tfm)
		crypto_free_hash(conn->conn_tx_hash.tfm);

	if (conn->conn_cpumask)
		free_cpumask_var(conn->conn_cpumask);

	kfree(conn->conn_ops);
	conn->conn_ops = NULL;

	if (conn->sock)
		sock_release(conn->sock);

	if (conn->conn_transport->iscsit_free_conn)
		conn->conn_transport->iscsit_free_conn(conn);

	iscsit_put_transport(conn->conn_transport);

	conn->thread_set = NULL;

	pr_debug("Moving to TARG_CONN_STATE_FREE.\n");
	conn->conn_state = TARG_CONN_STATE_FREE;
	kfree(conn);

	spin_lock_bh(&sess->conn_lock);
	atomic_dec(&sess->nconn);
	pr_debug("Decremented iSCSI connection count to %hu from node:"
		" %s\n", atomic_read(&sess->nconn),
		sess->sess_ops->InitiatorName);
	/*
	 * Make sure that if one connection fails in an non ERL=2 iSCSI
	 * Session that they all fail.
	 */
	if ((sess->sess_ops->ErrorRecoveryLevel != 2) && !conn_logout &&
	     !atomic_read(&sess->session_logout))
		atomic_set(&sess->session_fall_back_to_erl0, 1);

	/*
	 * If this was not the last connection in the session, and we are
	 * performing session reinstatement or falling back to ERL=0, call
	 * iscsit_stop_session() without sleeping to shutdown the other
	 * active connections.
	 */
	if (atomic_read(&sess->nconn)) {
		if (!atomic_read(&sess->session_reinstatement) &&
		    !atomic_read(&sess->session_fall_back_to_erl0)) {
			spin_unlock_bh(&sess->conn_lock);
			return 0;
		}
		if (!atomic_read(&sess->session_stop_active)) {
			atomic_set(&sess->session_stop_active, 1);
			spin_unlock_bh(&sess->conn_lock);
			iscsit_stop_session(sess, 0, 0);
			return 0;
		}
		spin_unlock_bh(&sess->conn_lock);
		return 0;
	}

	/*
	 * If this was the last connection in the session and one of the
	 * following is occurring:
	 *
	 * Session Reinstatement is not being performed, and are falling back
	 * to ERL=0 call iscsit_close_session().
	 *
	 * Session Logout was requested.  iscsit_close_session() will be called
	 * elsewhere.
	 *
	 * Session Continuation is not being performed, start the Time2Retain
	 * handler and check if sleep_on_sess_wait_sem is active.
	 */
	if (!atomic_read(&sess->session_reinstatement) &&
	     atomic_read(&sess->session_fall_back_to_erl0)) {
		spin_unlock_bh(&sess->conn_lock);
		target_put_session(sess->se_sess);

		return 0;
	} else if (atomic_read(&sess->session_logout)) {
		pr_debug("Moving to TARG_SESS_STATE_FREE.\n");
		sess->session_state = TARG_SESS_STATE_FREE;
		spin_unlock_bh(&sess->conn_lock);

		if (atomic_read(&sess->sleep_on_sess_wait_comp))
			complete(&sess->session_wait_comp);

		return 0;
	} else {
		pr_debug("Moving to TARG_SESS_STATE_FAILED.\n");
		sess->session_state = TARG_SESS_STATE_FAILED;

		if (!atomic_read(&sess->session_continuation)) {
			spin_unlock_bh(&sess->conn_lock);
			iscsit_start_time2retain_handler(sess);
		} else
			spin_unlock_bh(&sess->conn_lock);

		if (atomic_read(&sess->sleep_on_sess_wait_comp))
			complete(&sess->session_wait_comp);

		return 0;
	}
	spin_unlock_bh(&sess->conn_lock);

	return 0;
}

int iscsit_close_session(struct iscsi_session *sess)
{
	struct iscsi_portal_group *tpg = ISCSI_TPG_S(sess);
	struct se_portal_group *se_tpg = &tpg->tpg_se_tpg;

	if (atomic_read(&sess->nconn)) {
		pr_err("%d connection(s) still exist for iSCSI session"
			" to %s\n", atomic_read(&sess->nconn),
			sess->sess_ops->InitiatorName);
		BUG();
	}

	spin_lock_bh(&se_tpg->session_lock);
	atomic_set(&sess->session_logout, 1);
	atomic_set(&sess->session_reinstatement, 1);
	iscsit_stop_time2retain_timer(sess);
	spin_unlock_bh(&se_tpg->session_lock);

	/*
	 * transport_deregister_session_configfs() will clear the
	 * struct se_node_acl->nacl_sess pointer now as a iscsi_np process context
	 * can be setting it again with __transport_register_session() in
	 * iscsi_post_login_handler() again after the iscsit_stop_session()
	 * completes in iscsi_np context.
	 */
	transport_deregister_session_configfs(sess->se_sess);

	/*
	 * If any other processes are accessing this session pointer we must
	 * wait until they have completed.  If we are in an interrupt (the
	 * time2retain handler) and contain and active session usage count we
	 * restart the timer and exit.
	 */
	if (!in_interrupt()) {
		if (iscsit_check_session_usage_count(sess) == 1)
			iscsit_stop_session(sess, 1, 1);
	} else {
		if (iscsit_check_session_usage_count(sess) == 2) {
			atomic_set(&sess->session_logout, 0);
			iscsit_start_time2retain_handler(sess);
			return 0;
		}
	}

	transport_deregister_session(sess->se_sess);

	if (sess->sess_ops->ErrorRecoveryLevel == 2)
		iscsit_free_connection_recovery_entires(sess);

	iscsit_free_all_ooo_cmdsns(sess);

	spin_lock_bh(&se_tpg->session_lock);
	pr_debug("Moving to TARG_SESS_STATE_FREE.\n");
	sess->session_state = TARG_SESS_STATE_FREE;
	pr_debug("Released iSCSI session from node: %s\n",
			sess->sess_ops->InitiatorName);
	tpg->nsessions--;
	if (tpg->tpg_tiqn)
		tpg->tpg_tiqn->tiqn_nsessions--;

	pr_debug("Decremented number of active iSCSI Sessions on"
		" iSCSI TPG: %hu to %u\n", tpg->tpgt, tpg->nsessions);

	spin_lock(&sess_idr_lock);
	idr_remove(&sess_idr, sess->session_index);
	spin_unlock(&sess_idr_lock);

	kfree(sess->sess_ops);
	sess->sess_ops = NULL;
	spin_unlock_bh(&se_tpg->session_lock);

	kfree(sess);
	return 0;
}

static void iscsit_logout_post_handler_closesession(
	struct iscsi_conn *conn)
{
	struct iscsi_session *sess = conn->sess;

	iscsi_set_thread_clear(conn, ISCSI_CLEAR_TX_THREAD);
	iscsi_set_thread_set_signal(conn, ISCSI_SIGNAL_TX_THREAD);

	atomic_set(&conn->conn_logout_remove, 0);
	complete(&conn->conn_logout_comp);

	iscsit_dec_conn_usage_count(conn);
	iscsit_stop_session(sess, 1, 1);
	iscsit_dec_session_usage_count(sess);
	target_put_session(sess->se_sess);
}

static void iscsit_logout_post_handler_samecid(
	struct iscsi_conn *conn)
{
	iscsi_set_thread_clear(conn, ISCSI_CLEAR_TX_THREAD);
	iscsi_set_thread_set_signal(conn, ISCSI_SIGNAL_TX_THREAD);

	atomic_set(&conn->conn_logout_remove, 0);
	complete(&conn->conn_logout_comp);

	iscsit_cause_connection_reinstatement(conn, 1);
	iscsit_dec_conn_usage_count(conn);
}

static void iscsit_logout_post_handler_diffcid(
	struct iscsi_conn *conn,
	u16 cid)
{
	struct iscsi_conn *l_conn;
	struct iscsi_session *sess = conn->sess;

	if (!sess)
		return;

	spin_lock_bh(&sess->conn_lock);
	list_for_each_entry(l_conn, &sess->sess_conn_list, conn_list) {
		if (l_conn->cid == cid) {
			iscsit_inc_conn_usage_count(l_conn);
			break;
		}
	}
	spin_unlock_bh(&sess->conn_lock);

	if (!l_conn)
		return;

	if (l_conn->sock)
		l_conn->sock->ops->shutdown(l_conn->sock, RCV_SHUTDOWN);

	spin_lock_bh(&l_conn->state_lock);
	pr_debug("Moving to TARG_CONN_STATE_IN_LOGOUT.\n");
	l_conn->conn_state = TARG_CONN_STATE_IN_LOGOUT;
	spin_unlock_bh(&l_conn->state_lock);

	iscsit_cause_connection_reinstatement(l_conn, 1);
	iscsit_dec_conn_usage_count(l_conn);
}

/*
 *	Return of 0 causes the TX thread to restart.
 */
int iscsit_logout_post_handler(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	int ret = 0;

	switch (cmd->logout_reason) {
	case ISCSI_LOGOUT_REASON_CLOSE_SESSION:
		switch (cmd->logout_response) {
		case ISCSI_LOGOUT_SUCCESS:
		case ISCSI_LOGOUT_CLEANUP_FAILED:
		default:
			iscsit_logout_post_handler_closesession(conn);
			break;
		}
		ret = 0;
		break;
	case ISCSI_LOGOUT_REASON_CLOSE_CONNECTION:
		if (conn->cid == cmd->logout_cid) {
			switch (cmd->logout_response) {
			case ISCSI_LOGOUT_SUCCESS:
			case ISCSI_LOGOUT_CLEANUP_FAILED:
			default:
				iscsit_logout_post_handler_samecid(conn);
				break;
			}
			ret = 0;
		} else {
			switch (cmd->logout_response) {
			case ISCSI_LOGOUT_SUCCESS:
				iscsit_logout_post_handler_diffcid(conn,
					cmd->logout_cid);
				break;
			case ISCSI_LOGOUT_CID_NOT_FOUND:
			case ISCSI_LOGOUT_CLEANUP_FAILED:
			default:
				break;
			}
			ret = 1;
		}
		break;
	case ISCSI_LOGOUT_REASON_RECOVERY:
		switch (cmd->logout_response) {
		case ISCSI_LOGOUT_SUCCESS:
		case ISCSI_LOGOUT_CID_NOT_FOUND:
		case ISCSI_LOGOUT_RECOVERY_UNSUPPORTED:
		case ISCSI_LOGOUT_CLEANUP_FAILED:
		default:
			break;
		}
		ret = 1;
		break;
	default:
		break;

	}
	return ret;
}
EXPORT_SYMBOL(iscsit_logout_post_handler);

void iscsit_fail_session(struct iscsi_session *sess)
{
	struct iscsi_conn *conn;

	spin_lock_bh(&sess->conn_lock);
	list_for_each_entry(conn, &sess->sess_conn_list, conn_list) {
		pr_debug("Moving to TARG_CONN_STATE_CLEANUP_WAIT.\n");
		conn->conn_state = TARG_CONN_STATE_CLEANUP_WAIT;
	}
	spin_unlock_bh(&sess->conn_lock);

	pr_debug("Moving to TARG_SESS_STATE_FAILED.\n");
	sess->session_state = TARG_SESS_STATE_FAILED;
}

int iscsit_free_session(struct iscsi_session *sess)
{
	u16 conn_count = atomic_read(&sess->nconn);
	struct iscsi_conn *conn, *conn_tmp = NULL;
	int is_last;

	spin_lock_bh(&sess->conn_lock);
	atomic_set(&sess->sleep_on_sess_wait_comp, 1);

	list_for_each_entry_safe(conn, conn_tmp, &sess->sess_conn_list,
			conn_list) {
		if (conn_count == 0)
			break;

		if (list_is_last(&conn->conn_list, &sess->sess_conn_list)) {
			is_last = 1;
		} else {
			iscsit_inc_conn_usage_count(conn_tmp);
			is_last = 0;
		}
		iscsit_inc_conn_usage_count(conn);

		spin_unlock_bh(&sess->conn_lock);
		iscsit_cause_connection_reinstatement(conn, 1);
		spin_lock_bh(&sess->conn_lock);

		iscsit_dec_conn_usage_count(conn);
		if (is_last == 0)
			iscsit_dec_conn_usage_count(conn_tmp);

		conn_count--;
	}

	if (atomic_read(&sess->nconn)) {
		spin_unlock_bh(&sess->conn_lock);
		wait_for_completion(&sess->session_wait_comp);
	} else
		spin_unlock_bh(&sess->conn_lock);

	target_put_session(sess->se_sess);
	return 0;
}

void iscsit_stop_session(
	struct iscsi_session *sess,
	int session_sleep,
	int connection_sleep)
{
	u16 conn_count = atomic_read(&sess->nconn);
	struct iscsi_conn *conn, *conn_tmp = NULL;
	int is_last;

	spin_lock_bh(&sess->conn_lock);
	if (session_sleep)
		atomic_set(&sess->sleep_on_sess_wait_comp, 1);

	if (connection_sleep) {
		list_for_each_entry_safe(conn, conn_tmp, &sess->sess_conn_list,
				conn_list) {
			if (conn_count == 0)
				break;

			if (list_is_last(&conn->conn_list, &sess->sess_conn_list)) {
				is_last = 1;
			} else {
				iscsit_inc_conn_usage_count(conn_tmp);
				is_last = 0;
			}
			iscsit_inc_conn_usage_count(conn);

			spin_unlock_bh(&sess->conn_lock);
			iscsit_cause_connection_reinstatement(conn, 1);
			spin_lock_bh(&sess->conn_lock);

			iscsit_dec_conn_usage_count(conn);
			if (is_last == 0)
				iscsit_dec_conn_usage_count(conn_tmp);
			conn_count--;
		}
	} else {
		list_for_each_entry(conn, &sess->sess_conn_list, conn_list)
			iscsit_cause_connection_reinstatement(conn, 0);
	}

	if (session_sleep && atomic_read(&sess->nconn)) {
		spin_unlock_bh(&sess->conn_lock);
		wait_for_completion(&sess->session_wait_comp);
	} else
		spin_unlock_bh(&sess->conn_lock);
}

int iscsit_release_sessions_for_tpg(struct iscsi_portal_group *tpg, int force)
{
	struct iscsi_session *sess;
	struct se_portal_group *se_tpg = &tpg->tpg_se_tpg;
	struct se_session *se_sess, *se_sess_tmp;
	int session_count = 0;

	spin_lock_bh(&se_tpg->session_lock);
	if (tpg->nsessions && !force) {
		spin_unlock_bh(&se_tpg->session_lock);
		return -1;
	}

	list_for_each_entry_safe(se_sess, se_sess_tmp, &se_tpg->tpg_sess_list,
			sess_list) {
		sess = (struct iscsi_session *)se_sess->fabric_sess_ptr;

		spin_lock(&sess->conn_lock);
		if (atomic_read(&sess->session_fall_back_to_erl0) ||
		    atomic_read(&sess->session_logout) ||
		    (sess->time2retain_timer_flags & ISCSI_TF_EXPIRED)) {
			spin_unlock(&sess->conn_lock);
			continue;
		}
		atomic_set(&sess->session_reinstatement, 1);
		spin_unlock(&sess->conn_lock);
		spin_unlock_bh(&se_tpg->session_lock);

		iscsit_free_session(sess);
		spin_lock_bh(&se_tpg->session_lock);

		session_count++;
	}
	spin_unlock_bh(&se_tpg->session_lock);

	pr_debug("Released %d iSCSI Session(s) from Target Portal"
			" Group: %hu\n", session_count, tpg->tpgt);
	return 0;
}

MODULE_DESCRIPTION("iSCSI-Target Driver for mainline target infrastructure");
MODULE_VERSION("4.1.x");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(iscsi_target_init_module);
module_exit(iscsi_target_cleanup_module);
