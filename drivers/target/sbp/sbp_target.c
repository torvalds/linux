/*
 * SBP2 target driver (SCSI over IEEE1394 in target mode)
 *
 * Copyright (C) 2011  Chris Boot <bootc@bootc.net>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define KMSG_COMPONENT "sbp_target"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>
#include <target/target_core_fabric_configfs.h>
#include <target/target_core_configfs.h>
#include <target/configfs_macros.h>
#include <asm/unaligned.h>

#include "sbp_target.h"

/* Local pointer to allocated TCM configfs fabric module */
static struct target_fabric_configfs *sbp_fabric_configfs;

/* FireWire address region for management and command block address handlers */
static const struct fw_address_region sbp_register_region = {
	.start	= CSR_REGISTER_BASE + 0x10000,
	.end	= 0x1000000000000ULL,
};

static const u32 sbp_unit_directory_template[] = {
	0x1200609e, /* unit_specifier_id: NCITS/T10 */
	0x13010483, /* unit_sw_version: 1155D Rev 4 */
	0x3800609e, /* command_set_specifier_id: NCITS/T10 */
	0x390104d8, /* command_set: SPC-2 */
	0x3b000000, /* command_set_revision: 0 */
	0x3c000001, /* firmware_revision: 1 */
};

#define SESSION_MAINTENANCE_INTERVAL HZ

static atomic_t login_id = ATOMIC_INIT(0);

static void session_maintenance_work(struct work_struct *);
static int sbp_run_transaction(struct fw_card *, int, int, int, int,
		unsigned long long, void *, size_t);

static int read_peer_guid(u64 *guid, const struct sbp_management_request *req)
{
	int ret;
	__be32 high, low;

	ret = sbp_run_transaction(req->card, TCODE_READ_QUADLET_REQUEST,
			req->node_addr, req->generation, req->speed,
			(CSR_REGISTER_BASE | CSR_CONFIG_ROM) + 3 * 4,
			&high, sizeof(high));
	if (ret != RCODE_COMPLETE)
		return ret;

	ret = sbp_run_transaction(req->card, TCODE_READ_QUADLET_REQUEST,
			req->node_addr, req->generation, req->speed,
			(CSR_REGISTER_BASE | CSR_CONFIG_ROM) + 4 * 4,
			&low, sizeof(low));
	if (ret != RCODE_COMPLETE)
		return ret;

	*guid = (u64)be32_to_cpu(high) << 32 | be32_to_cpu(low);

	return RCODE_COMPLETE;
}

static struct sbp_session *sbp_session_find_by_guid(
	struct sbp_tpg *tpg, u64 guid)
{
	struct se_session *se_sess;
	struct sbp_session *sess, *found = NULL;

	spin_lock_bh(&tpg->se_tpg.session_lock);
	list_for_each_entry(se_sess, &tpg->se_tpg.tpg_sess_list, sess_list) {
		sess = se_sess->fabric_sess_ptr;
		if (sess->guid == guid)
			found = sess;
	}
	spin_unlock_bh(&tpg->se_tpg.session_lock);

	return found;
}

static struct sbp_login_descriptor *sbp_login_find_by_lun(
		struct sbp_session *session, struct se_lun *lun)
{
	struct sbp_login_descriptor *login, *found = NULL;

	spin_lock_bh(&session->lock);
	list_for_each_entry(login, &session->login_list, link) {
		if (login->lun == lun)
			found = login;
	}
	spin_unlock_bh(&session->lock);

	return found;
}

static int sbp_login_count_all_by_lun(
		struct sbp_tpg *tpg,
		struct se_lun *lun,
		int exclusive)
{
	struct se_session *se_sess;
	struct sbp_session *sess;
	struct sbp_login_descriptor *login;
	int count = 0;

	spin_lock_bh(&tpg->se_tpg.session_lock);
	list_for_each_entry(se_sess, &tpg->se_tpg.tpg_sess_list, sess_list) {
		sess = se_sess->fabric_sess_ptr;

		spin_lock_bh(&sess->lock);
		list_for_each_entry(login, &sess->login_list, link) {
			if (login->lun != lun)
				continue;

			if (!exclusive || login->exclusive)
				count++;
		}
		spin_unlock_bh(&sess->lock);
	}
	spin_unlock_bh(&tpg->se_tpg.session_lock);

	return count;
}

static struct sbp_login_descriptor *sbp_login_find_by_id(
	struct sbp_tpg *tpg, int login_id)
{
	struct se_session *se_sess;
	struct sbp_session *sess;
	struct sbp_login_descriptor *login, *found = NULL;

	spin_lock_bh(&tpg->se_tpg.session_lock);
	list_for_each_entry(se_sess, &tpg->se_tpg.tpg_sess_list, sess_list) {
		sess = se_sess->fabric_sess_ptr;

		spin_lock_bh(&sess->lock);
		list_for_each_entry(login, &sess->login_list, link) {
			if (login->login_id == login_id)
				found = login;
		}
		spin_unlock_bh(&sess->lock);
	}
	spin_unlock_bh(&tpg->se_tpg.session_lock);

	return found;
}

static struct se_lun *sbp_get_lun_from_tpg(struct sbp_tpg *tpg, int lun)
{
	struct se_portal_group *se_tpg = &tpg->se_tpg;
	struct se_lun *se_lun;

	if (lun >= TRANSPORT_MAX_LUNS_PER_TPG)
		return ERR_PTR(-EINVAL);

	spin_lock(&se_tpg->tpg_lun_lock);
	se_lun = se_tpg->tpg_lun_list[lun];

	if (se_lun->lun_status != TRANSPORT_LUN_STATUS_ACTIVE)
		se_lun = ERR_PTR(-ENODEV);

	spin_unlock(&se_tpg->tpg_lun_lock);

	return se_lun;
}

static struct sbp_session *sbp_session_create(
		struct sbp_tpg *tpg,
		u64 guid)
{
	struct sbp_session *sess;
	int ret;
	char guid_str[17];
	struct se_node_acl *se_nacl;

	sess = kmalloc(sizeof(*sess), GFP_KERNEL);
	if (!sess) {
		pr_err("failed to allocate session descriptor\n");
		return ERR_PTR(-ENOMEM);
	}

	sess->se_sess = transport_init_session();
	if (IS_ERR(sess->se_sess)) {
		pr_err("failed to init se_session\n");

		ret = PTR_ERR(sess->se_sess);
		kfree(sess);
		return ERR_PTR(ret);
	}

	snprintf(guid_str, sizeof(guid_str), "%016llx", guid);

	se_nacl = core_tpg_check_initiator_node_acl(&tpg->se_tpg, guid_str);
	if (!se_nacl) {
		pr_warn("Node ACL not found for %s\n", guid_str);

		transport_free_session(sess->se_sess);
		kfree(sess);

		return ERR_PTR(-EPERM);
	}

	sess->se_sess->se_node_acl = se_nacl;

	spin_lock_init(&sess->lock);
	INIT_LIST_HEAD(&sess->login_list);
	INIT_DELAYED_WORK(&sess->maint_work, session_maintenance_work);

	sess->guid = guid;

	transport_register_session(&tpg->se_tpg, se_nacl, sess->se_sess, sess);

	return sess;
}

static void sbp_session_release(struct sbp_session *sess, bool cancel_work)
{
	spin_lock_bh(&sess->lock);
	if (!list_empty(&sess->login_list)) {
		spin_unlock_bh(&sess->lock);
		return;
	}
	spin_unlock_bh(&sess->lock);

	if (cancel_work)
		cancel_delayed_work_sync(&sess->maint_work);

	transport_deregister_session_configfs(sess->se_sess);
	transport_deregister_session(sess->se_sess);

	if (sess->card)
		fw_card_put(sess->card);

	kfree(sess);
}

static void sbp_target_agent_unregister(struct sbp_target_agent *);

static void sbp_login_release(struct sbp_login_descriptor *login,
	bool cancel_work)
{
	struct sbp_session *sess = login->sess;

	/* FIXME: abort/wait on tasks */

	sbp_target_agent_unregister(login->tgt_agt);

	if (sess) {
		spin_lock_bh(&sess->lock);
		list_del(&login->link);
		spin_unlock_bh(&sess->lock);

		sbp_session_release(sess, cancel_work);
	}

	kfree(login);
}

static struct sbp_target_agent *sbp_target_agent_register(
	struct sbp_login_descriptor *);

static void sbp_management_request_login(
	struct sbp_management_agent *agent, struct sbp_management_request *req,
	int *status_data_size)
{
	struct sbp_tport *tport = agent->tport;
	struct sbp_tpg *tpg = tport->tpg;
	struct se_lun *se_lun;
	int ret;
	u64 guid;
	struct sbp_session *sess;
	struct sbp_login_descriptor *login;
	struct sbp_login_response_block *response;
	int login_response_len;

	se_lun = sbp_get_lun_from_tpg(tpg,
			LOGIN_ORB_LUN(be32_to_cpu(req->orb.misc)));
	if (IS_ERR(se_lun)) {
		pr_notice("login to unknown LUN: %d\n",
			LOGIN_ORB_LUN(be32_to_cpu(req->orb.misc)));

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_LUN_NOTSUPP));
		return;
	}

	ret = read_peer_guid(&guid, req);
	if (ret != RCODE_COMPLETE) {
		pr_warn("failed to read peer GUID: %d\n", ret);

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_TRANSPORT_FAILURE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_UNSPECIFIED_ERROR));
		return;
	}

	pr_notice("mgt_agent LOGIN to LUN %d from %016llx\n",
		se_lun->unpacked_lun, guid);

	sess = sbp_session_find_by_guid(tpg, guid);
	if (sess) {
		login = sbp_login_find_by_lun(sess, se_lun);
		if (login) {
			pr_notice("initiator already logged-in\n");

			/*
			 * SBP-2 R4 says we should return access denied, but
			 * that can confuse initiators. Instead we need to
			 * treat this like a reconnect, but send the login
			 * response block like a fresh login.
			 *
			 * This is required particularly in the case of Apple
			 * devices booting off the FireWire target, where
			 * the firmware has an active login to the target. When
			 * the OS takes control of the session it issues its own
			 * LOGIN rather than a RECONNECT. To avoid the machine
			 * waiting until the reconnect_hold expires, we can skip
			 * the ACCESS_DENIED errors to speed things up.
			 */

			goto already_logged_in;
		}
	}

	/*
	 * check exclusive bit in login request
	 * reject with access_denied if any logins present
	 */
	if (LOGIN_ORB_EXCLUSIVE(be32_to_cpu(req->orb.misc)) &&
			sbp_login_count_all_by_lun(tpg, se_lun, 0)) {
		pr_warn("refusing exclusive login with other active logins\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_ACCESS_DENIED));
		return;
	}

	/*
	 * check exclusive bit in any existing login descriptor
	 * reject with access_denied if any exclusive logins present
	 */
	if (sbp_login_count_all_by_lun(tpg, se_lun, 1)) {
		pr_warn("refusing login while another exclusive login present\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_ACCESS_DENIED));
		return;
	}

	/*
	 * check we haven't exceeded the number of allowed logins
	 * reject with resources_unavailable if we have
	 */
	if (sbp_login_count_all_by_lun(tpg, se_lun, 0) >=
			tport->max_logins_per_lun) {
		pr_warn("max number of logins reached\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_RESOURCES_UNAVAIL));
		return;
	}

	if (!sess) {
		sess = sbp_session_create(tpg, guid);
		if (IS_ERR(sess)) {
			switch (PTR_ERR(sess)) {
			case -EPERM:
				ret = SBP_STATUS_ACCESS_DENIED;
				break;
			default:
				ret = SBP_STATUS_RESOURCES_UNAVAIL;
				break;
			}

			req->status.status = cpu_to_be32(
				STATUS_BLOCK_RESP(
					STATUS_RESP_REQUEST_COMPLETE) |
				STATUS_BLOCK_SBP_STATUS(ret));
			return;
		}

		sess->node_id = req->node_addr;
		sess->card = fw_card_get(req->card);
		sess->generation = req->generation;
		sess->speed = req->speed;

		schedule_delayed_work(&sess->maint_work,
				SESSION_MAINTENANCE_INTERVAL);
	}

	/* only take the latest reconnect_hold into account */
	sess->reconnect_hold = min(
		1 << LOGIN_ORB_RECONNECT(be32_to_cpu(req->orb.misc)),
		tport->max_reconnect_timeout) - 1;

	login = kmalloc(sizeof(*login), GFP_KERNEL);
	if (!login) {
		pr_err("failed to allocate login descriptor\n");

		sbp_session_release(sess, true);

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_RESOURCES_UNAVAIL));
		return;
	}

	login->sess = sess;
	login->lun = se_lun;
	login->status_fifo_addr = sbp2_pointer_to_addr(&req->orb.status_fifo);
	login->exclusive = LOGIN_ORB_EXCLUSIVE(be32_to_cpu(req->orb.misc));
	login->login_id = atomic_inc_return(&login_id);

	login->tgt_agt = sbp_target_agent_register(login);
	if (IS_ERR(login->tgt_agt)) {
		ret = PTR_ERR(login->tgt_agt);
		pr_err("failed to map command block handler: %d\n", ret);

		sbp_session_release(sess, true);
		kfree(login);

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_RESOURCES_UNAVAIL));
		return;
	}

	spin_lock_bh(&sess->lock);
	list_add_tail(&login->link, &sess->login_list);
	spin_unlock_bh(&sess->lock);

already_logged_in:
	response = kzalloc(sizeof(*response), GFP_KERNEL);
	if (!response) {
		pr_err("failed to allocate login response block\n");

		sbp_login_release(login, true);

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_RESOURCES_UNAVAIL));
		return;
	}

	login_response_len = clamp_val(
			LOGIN_ORB_RESPONSE_LENGTH(be32_to_cpu(req->orb.length)),
			12, sizeof(*response));
	response->misc = cpu_to_be32(
		((login_response_len & 0xffff) << 16) |
		(login->login_id & 0xffff));
	response->reconnect_hold = cpu_to_be32(sess->reconnect_hold & 0xffff);
	addr_to_sbp2_pointer(login->tgt_agt->handler.offset,
		&response->command_block_agent);

	ret = sbp_run_transaction(sess->card, TCODE_WRITE_BLOCK_REQUEST,
		sess->node_id, sess->generation, sess->speed,
		sbp2_pointer_to_addr(&req->orb.ptr2), response,
		login_response_len);
	if (ret != RCODE_COMPLETE) {
		pr_debug("failed to write login response block: %x\n", ret);

		kfree(response);
		sbp_login_release(login, true);

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_TRANSPORT_FAILURE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_UNSPECIFIED_ERROR));
		return;
	}

	kfree(response);

	req->status.status = cpu_to_be32(
		STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
		STATUS_BLOCK_SBP_STATUS(SBP_STATUS_OK));
}

static void sbp_management_request_query_logins(
	struct sbp_management_agent *agent, struct sbp_management_request *req,
	int *status_data_size)
{
	pr_notice("QUERY LOGINS not implemented\n");
	/* FIXME: implement */

	req->status.status = cpu_to_be32(
		STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
		STATUS_BLOCK_SBP_STATUS(SBP_STATUS_REQ_TYPE_NOTSUPP));
}

static void sbp_management_request_reconnect(
	struct sbp_management_agent *agent, struct sbp_management_request *req,
	int *status_data_size)
{
	struct sbp_tport *tport = agent->tport;
	struct sbp_tpg *tpg = tport->tpg;
	int ret;
	u64 guid;
	struct sbp_login_descriptor *login;

	ret = read_peer_guid(&guid, req);
	if (ret != RCODE_COMPLETE) {
		pr_warn("failed to read peer GUID: %d\n", ret);

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_TRANSPORT_FAILURE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_UNSPECIFIED_ERROR));
		return;
	}

	pr_notice("mgt_agent RECONNECT from %016llx\n", guid);

	login = sbp_login_find_by_id(tpg,
		RECONNECT_ORB_LOGIN_ID(be32_to_cpu(req->orb.misc)));

	if (!login) {
		pr_err("mgt_agent RECONNECT unknown login ID\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_ACCESS_DENIED));
		return;
	}

	if (login->sess->guid != guid) {
		pr_err("mgt_agent RECONNECT login GUID doesn't match\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_ACCESS_DENIED));
		return;
	}

	spin_lock_bh(&login->sess->lock);
	if (login->sess->card)
		fw_card_put(login->sess->card);

	/* update the node details */
	login->sess->generation = req->generation;
	login->sess->node_id = req->node_addr;
	login->sess->card = fw_card_get(req->card);
	login->sess->speed = req->speed;
	spin_unlock_bh(&login->sess->lock);

	req->status.status = cpu_to_be32(
		STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
		STATUS_BLOCK_SBP_STATUS(SBP_STATUS_OK));
}

static void sbp_management_request_logout(
	struct sbp_management_agent *agent, struct sbp_management_request *req,
	int *status_data_size)
{
	struct sbp_tport *tport = agent->tport;
	struct sbp_tpg *tpg = tport->tpg;
	int id;
	struct sbp_login_descriptor *login;

	id = LOGOUT_ORB_LOGIN_ID(be32_to_cpu(req->orb.misc));

	login = sbp_login_find_by_id(tpg, id);
	if (!login) {
		pr_warn("cannot find login: %d\n", id);

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_LOGIN_ID_UNKNOWN));
		return;
	}

	pr_info("mgt_agent LOGOUT from LUN %d session %d\n",
		login->lun->unpacked_lun, login->login_id);

	if (req->node_addr != login->sess->node_id) {
		pr_warn("logout from different node ID\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_ACCESS_DENIED));
		return;
	}

	sbp_login_release(login, true);

	req->status.status = cpu_to_be32(
		STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
		STATUS_BLOCK_SBP_STATUS(SBP_STATUS_OK));
}

static void session_check_for_reset(struct sbp_session *sess)
{
	bool card_valid = false;

	spin_lock_bh(&sess->lock);

	if (sess->card) {
		spin_lock_irq(&sess->card->lock);
		card_valid = (sess->card->local_node != NULL);
		spin_unlock_irq(&sess->card->lock);

		if (!card_valid) {
			fw_card_put(sess->card);
			sess->card = NULL;
		}
	}

	if (!card_valid || (sess->generation != sess->card->generation)) {
		pr_info("Waiting for reconnect from node: %016llx\n",
				sess->guid);

		sess->node_id = -1;
		sess->reconnect_expires = get_jiffies_64() +
			((sess->reconnect_hold + 1) * HZ);
	}

	spin_unlock_bh(&sess->lock);
}

static void session_reconnect_expired(struct sbp_session *sess)
{
	struct sbp_login_descriptor *login, *temp;
	LIST_HEAD(login_list);

	pr_info("Reconnect timer expired for node: %016llx\n", sess->guid);

	spin_lock_bh(&sess->lock);
	list_for_each_entry_safe(login, temp, &sess->login_list, link) {
		login->sess = NULL;
		list_move_tail(&login->link, &login_list);
	}
	spin_unlock_bh(&sess->lock);

	list_for_each_entry_safe(login, temp, &login_list, link) {
		list_del(&login->link);
		sbp_login_release(login, false);
	}

	sbp_session_release(sess, false);
}

static void session_maintenance_work(struct work_struct *work)
{
	struct sbp_session *sess = container_of(work, struct sbp_session,
			maint_work.work);

	/* could be called while tearing down the session */
	spin_lock_bh(&sess->lock);
	if (list_empty(&sess->login_list)) {
		spin_unlock_bh(&sess->lock);
		return;
	}
	spin_unlock_bh(&sess->lock);

	if (sess->node_id != -1) {
		/* check for bus reset and make node_id invalid */
		session_check_for_reset(sess);

		schedule_delayed_work(&sess->maint_work,
				SESSION_MAINTENANCE_INTERVAL);
	} else if (!time_after64(get_jiffies_64(), sess->reconnect_expires)) {
		/* still waiting for reconnect */
		schedule_delayed_work(&sess->maint_work,
				SESSION_MAINTENANCE_INTERVAL);
	} else {
		/* reconnect timeout has expired */
		session_reconnect_expired(sess);
	}
}

static int tgt_agent_rw_agent_state(struct fw_card *card, int tcode, void *data,
		struct sbp_target_agent *agent)
{
	int state;

	switch (tcode) {
	case TCODE_READ_QUADLET_REQUEST:
		pr_debug("tgt_agent AGENT_STATE READ\n");

		spin_lock_bh(&agent->lock);
		state = agent->state;
		spin_unlock_bh(&agent->lock);

		*(__be32 *)data = cpu_to_be32(state);

		return RCODE_COMPLETE;

	case TCODE_WRITE_QUADLET_REQUEST:
		/* ignored */
		return RCODE_COMPLETE;

	default:
		return RCODE_TYPE_ERROR;
	}
}

static int tgt_agent_rw_agent_reset(struct fw_card *card, int tcode, void *data,
		struct sbp_target_agent *agent)
{
	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
		pr_debug("tgt_agent AGENT_RESET\n");
		spin_lock_bh(&agent->lock);
		agent->state = AGENT_STATE_RESET;
		spin_unlock_bh(&agent->lock);
		return RCODE_COMPLETE;

	default:
		return RCODE_TYPE_ERROR;
	}
}

static int tgt_agent_rw_orb_pointer(struct fw_card *card, int tcode, void *data,
		struct sbp_target_agent *agent)
{
	struct sbp2_pointer *ptr = data;

	switch (tcode) {
	case TCODE_WRITE_BLOCK_REQUEST:
		spin_lock_bh(&agent->lock);
		if (agent->state != AGENT_STATE_SUSPENDED &&
				agent->state != AGENT_STATE_RESET) {
			spin_unlock_bh(&agent->lock);
			pr_notice("Ignoring ORB_POINTER write while active.\n");
			return RCODE_CONFLICT_ERROR;
		}
		agent->state = AGENT_STATE_ACTIVE;
		spin_unlock_bh(&agent->lock);

		agent->orb_pointer = sbp2_pointer_to_addr(ptr);
		agent->doorbell = false;

		pr_debug("tgt_agent ORB_POINTER write: 0x%llx\n",
				agent->orb_pointer);

		queue_work(system_unbound_wq, &agent->work);

		return RCODE_COMPLETE;

	case TCODE_READ_BLOCK_REQUEST:
		pr_debug("tgt_agent ORB_POINTER READ\n");
		spin_lock_bh(&agent->lock);
		addr_to_sbp2_pointer(agent->orb_pointer, ptr);
		spin_unlock_bh(&agent->lock);
		return RCODE_COMPLETE;

	default:
		return RCODE_TYPE_ERROR;
	}
}

static int tgt_agent_rw_doorbell(struct fw_card *card, int tcode, void *data,
		struct sbp_target_agent *agent)
{
	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
		spin_lock_bh(&agent->lock);
		if (agent->state != AGENT_STATE_SUSPENDED) {
			spin_unlock_bh(&agent->lock);
			pr_debug("Ignoring DOORBELL while active.\n");
			return RCODE_CONFLICT_ERROR;
		}
		agent->state = AGENT_STATE_ACTIVE;
		spin_unlock_bh(&agent->lock);

		agent->doorbell = true;

		pr_debug("tgt_agent DOORBELL\n");

		queue_work(system_unbound_wq, &agent->work);

		return RCODE_COMPLETE;

	case TCODE_READ_QUADLET_REQUEST:
		return RCODE_COMPLETE;

	default:
		return RCODE_TYPE_ERROR;
	}
}

static int tgt_agent_rw_unsolicited_status_enable(struct fw_card *card,
		int tcode, void *data, struct sbp_target_agent *agent)
{
	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
		pr_debug("tgt_agent UNSOLICITED_STATUS_ENABLE\n");
		/* ignored as we don't send unsolicited status */
		return RCODE_COMPLETE;

	case TCODE_READ_QUADLET_REQUEST:
		return RCODE_COMPLETE;

	default:
		return RCODE_TYPE_ERROR;
	}
}

static void tgt_agent_rw(struct fw_card *card, struct fw_request *request,
		int tcode, int destination, int source, int generation,
		unsigned long long offset, void *data, size_t length,
		void *callback_data)
{
	struct sbp_target_agent *agent = callback_data;
	struct sbp_session *sess = agent->login->sess;
	int sess_gen, sess_node, rcode;

	spin_lock_bh(&sess->lock);
	sess_gen = sess->generation;
	sess_node = sess->node_id;
	spin_unlock_bh(&sess->lock);

	if (generation != sess_gen) {
		pr_notice("ignoring request with wrong generation\n");
		rcode = RCODE_TYPE_ERROR;
		goto out;
	}

	if (source != sess_node) {
		pr_notice("ignoring request from foreign node (%x != %x)\n",
				source, sess_node);
		rcode = RCODE_TYPE_ERROR;
		goto out;
	}

	/* turn offset into the offset from the start of the block */
	offset -= agent->handler.offset;

	if (offset == 0x00 && length == 4) {
		/* AGENT_STATE */
		rcode = tgt_agent_rw_agent_state(card, tcode, data, agent);
	} else if (offset == 0x04 && length == 4) {
		/* AGENT_RESET */
		rcode = tgt_agent_rw_agent_reset(card, tcode, data, agent);
	} else if (offset == 0x08 && length == 8) {
		/* ORB_POINTER */
		rcode = tgt_agent_rw_orb_pointer(card, tcode, data, agent);
	} else if (offset == 0x10 && length == 4) {
		/* DOORBELL */
		rcode = tgt_agent_rw_doorbell(card, tcode, data, agent);
	} else if (offset == 0x14 && length == 4) {
		/* UNSOLICITED_STATUS_ENABLE */
		rcode = tgt_agent_rw_unsolicited_status_enable(card, tcode,
				data, agent);
	} else {
		rcode = RCODE_ADDRESS_ERROR;
	}

out:
	fw_send_response(card, request, rcode);
}

static void sbp_handle_command(struct sbp_target_request *);
static int sbp_send_status(struct sbp_target_request *);
static void sbp_free_request(struct sbp_target_request *);

static void tgt_agent_process_work(struct work_struct *work)
{
	struct sbp_target_request *req =
		container_of(work, struct sbp_target_request, work);

	pr_debug("tgt_orb ptr:0x%llx next_ORB:0x%llx data_descriptor:0x%llx misc:0x%x\n",
			req->orb_pointer,
			sbp2_pointer_to_addr(&req->orb.next_orb),
			sbp2_pointer_to_addr(&req->orb.data_descriptor),
			be32_to_cpu(req->orb.misc));

	if (req->orb_pointer >> 32)
		pr_debug("ORB with high bits set\n");

	switch (ORB_REQUEST_FORMAT(be32_to_cpu(req->orb.misc))) {
		case 0:/* Format specified by this standard */
			sbp_handle_command(req);
			return;
		case 1: /* Reserved for future standardization */
		case 2: /* Vendor-dependent */
			req->status.status |= cpu_to_be32(
					STATUS_BLOCK_RESP(
						STATUS_RESP_REQUEST_COMPLETE) |
					STATUS_BLOCK_DEAD(0) |
					STATUS_BLOCK_LEN(1) |
					STATUS_BLOCK_SBP_STATUS(
						SBP_STATUS_REQ_TYPE_NOTSUPP));
			sbp_send_status(req);
			sbp_free_request(req);
			return;
		case 3: /* Dummy ORB */
			req->status.status |= cpu_to_be32(
					STATUS_BLOCK_RESP(
						STATUS_RESP_REQUEST_COMPLETE) |
					STATUS_BLOCK_DEAD(0) |
					STATUS_BLOCK_LEN(1) |
					STATUS_BLOCK_SBP_STATUS(
						SBP_STATUS_DUMMY_ORB_COMPLETE));
			sbp_send_status(req);
			sbp_free_request(req);
			return;
		default:
			BUG();
	}
}

/* used to double-check we haven't been issued an AGENT_RESET */
static inline bool tgt_agent_check_active(struct sbp_target_agent *agent)
{
	bool active;

	spin_lock_bh(&agent->lock);
	active = (agent->state == AGENT_STATE_ACTIVE);
	spin_unlock_bh(&agent->lock);

	return active;
}

static void tgt_agent_fetch_work(struct work_struct *work)
{
	struct sbp_target_agent *agent =
		container_of(work, struct sbp_target_agent, work);
	struct sbp_session *sess = agent->login->sess;
	struct sbp_target_request *req;
	int ret;
	bool doorbell = agent->doorbell;
	u64 next_orb = agent->orb_pointer;

	while (next_orb && tgt_agent_check_active(agent)) {
		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req) {
			spin_lock_bh(&agent->lock);
			agent->state = AGENT_STATE_DEAD;
			spin_unlock_bh(&agent->lock);
			return;
		}

		req->login = agent->login;
		req->orb_pointer = next_orb;

		req->status.status = cpu_to_be32(STATUS_BLOCK_ORB_OFFSET_HIGH(
					req->orb_pointer >> 32));
		req->status.orb_low = cpu_to_be32(
				req->orb_pointer & 0xfffffffc);

		/* read in the ORB */
		ret = sbp_run_transaction(sess->card, TCODE_READ_BLOCK_REQUEST,
				sess->node_id, sess->generation, sess->speed,
				req->orb_pointer, &req->orb, sizeof(req->orb));
		if (ret != RCODE_COMPLETE) {
			pr_debug("tgt_orb fetch failed: %x\n", ret);
			req->status.status |= cpu_to_be32(
					STATUS_BLOCK_SRC(
						STATUS_SRC_ORB_FINISHED) |
					STATUS_BLOCK_RESP(
						STATUS_RESP_TRANSPORT_FAILURE) |
					STATUS_BLOCK_DEAD(1) |
					STATUS_BLOCK_LEN(1) |
					STATUS_BLOCK_SBP_STATUS(
						SBP_STATUS_UNSPECIFIED_ERROR));
			spin_lock_bh(&agent->lock);
			agent->state = AGENT_STATE_DEAD;
			spin_unlock_bh(&agent->lock);

			sbp_send_status(req);
			sbp_free_request(req);
			return;
		}

		/* check the next_ORB field */
		if (be32_to_cpu(req->orb.next_orb.high) & 0x80000000) {
			next_orb = 0;
			req->status.status |= cpu_to_be32(STATUS_BLOCK_SRC(
						STATUS_SRC_ORB_FINISHED));
		} else {
			next_orb = sbp2_pointer_to_addr(&req->orb.next_orb);
			req->status.status |= cpu_to_be32(STATUS_BLOCK_SRC(
						STATUS_SRC_ORB_CONTINUING));
		}

		if (tgt_agent_check_active(agent) && !doorbell) {
			INIT_WORK(&req->work, tgt_agent_process_work);
			queue_work(system_unbound_wq, &req->work);
		} else {
			/* don't process this request, just check next_ORB */
			sbp_free_request(req);
		}

		spin_lock_bh(&agent->lock);
		doorbell = agent->doorbell = false;

		/* check if we should carry on processing */
		if (next_orb)
			agent->orb_pointer = next_orb;
		else
			agent->state = AGENT_STATE_SUSPENDED;

		spin_unlock_bh(&agent->lock);
	};
}

static struct sbp_target_agent *sbp_target_agent_register(
		struct sbp_login_descriptor *login)
{
	struct sbp_target_agent *agent;
	int ret;

	agent = kmalloc(sizeof(*agent), GFP_KERNEL);
	if (!agent)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&agent->lock);

	agent->handler.length = 0x20;
	agent->handler.address_callback = tgt_agent_rw;
	agent->handler.callback_data = agent;

	agent->login = login;
	agent->state = AGENT_STATE_RESET;
	INIT_WORK(&agent->work, tgt_agent_fetch_work);
	agent->orb_pointer = 0;
	agent->doorbell = false;

	ret = fw_core_add_address_handler(&agent->handler,
			&sbp_register_region);
	if (ret < 0) {
		kfree(agent);
		return ERR_PTR(ret);
	}

	return agent;
}

static void sbp_target_agent_unregister(struct sbp_target_agent *agent)
{
	fw_core_remove_address_handler(&agent->handler);
	cancel_work_sync(&agent->work);
	kfree(agent);
}

/*
 * Simple wrapper around fw_run_transaction that retries the transaction several
 * times in case of failure, with an exponential backoff.
 */
static int sbp_run_transaction(struct fw_card *card, int tcode, int destination_id,
		int generation, int speed, unsigned long long offset,
		void *payload, size_t length)
{
	int attempt, ret, delay;

	for (attempt = 1; attempt <= 5; attempt++) {
		ret = fw_run_transaction(card, tcode, destination_id,
				generation, speed, offset, payload, length);

		switch (ret) {
		case RCODE_COMPLETE:
		case RCODE_TYPE_ERROR:
		case RCODE_ADDRESS_ERROR:
		case RCODE_GENERATION:
			return ret;

		default:
			delay = 5 * attempt * attempt;
			usleep_range(delay, delay * 2);
		}
	}

	return ret;
}

/*
 * Wrapper around sbp_run_transaction that gets the card, destination,
 * generation and speed out of the request's session.
 */
static int sbp_run_request_transaction(struct sbp_target_request *req,
		int tcode, unsigned long long offset, void *payload,
		size_t length)
{
	struct sbp_login_descriptor *login = req->login;
	struct sbp_session *sess = login->sess;
	struct fw_card *card;
	int node_id, generation, speed, ret;

	spin_lock_bh(&sess->lock);
	card = fw_card_get(sess->card);
	node_id = sess->node_id;
	generation = sess->generation;
	speed = sess->speed;
	spin_unlock_bh(&sess->lock);

	ret = sbp_run_transaction(card, tcode, node_id, generation, speed,
			offset, payload, length);

	fw_card_put(card);

	return ret;
}

static int sbp_fetch_command(struct sbp_target_request *req)
{
	int ret, cmd_len, copy_len;

	cmd_len = scsi_command_size(req->orb.command_block);

	req->cmd_buf = kmalloc(cmd_len, GFP_KERNEL);
	if (!req->cmd_buf)
		return -ENOMEM;

	memcpy(req->cmd_buf, req->orb.command_block,
		min_t(int, cmd_len, sizeof(req->orb.command_block)));

	if (cmd_len > sizeof(req->orb.command_block)) {
		pr_debug("sbp_fetch_command: filling in long command\n");
		copy_len = cmd_len - sizeof(req->orb.command_block);

		ret = sbp_run_request_transaction(req,
				TCODE_READ_BLOCK_REQUEST,
				req->orb_pointer + sizeof(req->orb),
				req->cmd_buf + sizeof(req->orb.command_block),
				copy_len);
		if (ret != RCODE_COMPLETE)
			return -EIO;
	}

	return 0;
}

static int sbp_fetch_page_table(struct sbp_target_request *req)
{
	int pg_tbl_sz, ret;
	struct sbp_page_table_entry *pg_tbl;

	if (!CMDBLK_ORB_PG_TBL_PRESENT(be32_to_cpu(req->orb.misc)))
		return 0;

	pg_tbl_sz = CMDBLK_ORB_DATA_SIZE(be32_to_cpu(req->orb.misc)) *
		sizeof(struct sbp_page_table_entry);

	pg_tbl = kmalloc(pg_tbl_sz, GFP_KERNEL);
	if (!pg_tbl)
		return -ENOMEM;

	ret = sbp_run_request_transaction(req, TCODE_READ_BLOCK_REQUEST,
			sbp2_pointer_to_addr(&req->orb.data_descriptor),
			pg_tbl, pg_tbl_sz);
	if (ret != RCODE_COMPLETE) {
		kfree(pg_tbl);
		return -EIO;
	}

	req->pg_tbl = pg_tbl;
	return 0;
}

static void sbp_calc_data_length_direction(struct sbp_target_request *req,
	u32 *data_len, enum dma_data_direction *data_dir)
{
	int data_size, direction, idx;

	data_size = CMDBLK_ORB_DATA_SIZE(be32_to_cpu(req->orb.misc));
	direction = CMDBLK_ORB_DIRECTION(be32_to_cpu(req->orb.misc));

	if (!data_size) {
		*data_len = 0;
		*data_dir = DMA_NONE;
		return;
	}

	*data_dir = direction ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	if (req->pg_tbl) {
		*data_len = 0;
		for (idx = 0; idx < data_size; idx++) {
			*data_len += be16_to_cpu(
					req->pg_tbl[idx].segment_length);
		}
	} else {
		*data_len = data_size;
	}
}

static void sbp_handle_command(struct sbp_target_request *req)
{
	struct sbp_login_descriptor *login = req->login;
	struct sbp_session *sess = login->sess;
	int ret, unpacked_lun;
	u32 data_length;
	enum dma_data_direction data_dir;

	ret = sbp_fetch_command(req);
	if (ret) {
		pr_debug("sbp_handle_command: fetch command failed: %d\n", ret);
		goto err;
	}

	ret = sbp_fetch_page_table(req);
	if (ret) {
		pr_debug("sbp_handle_command: fetch page table failed: %d\n",
			ret);
		goto err;
	}

	unpacked_lun = req->login->lun->unpacked_lun;
	sbp_calc_data_length_direction(req, &data_length, &data_dir);

	pr_debug("sbp_handle_command ORB:0x%llx unpacked_lun:%d data_len:%d data_dir:%d\n",
			req->orb_pointer, unpacked_lun, data_length, data_dir);

	if (target_submit_cmd(&req->se_cmd, sess->se_sess, req->cmd_buf,
			      req->sense_buf, unpacked_lun, data_length,
			      MSG_SIMPLE_TAG, data_dir, 0))
		goto err;

	return;

err:
	req->status.status |= cpu_to_be32(
		STATUS_BLOCK_RESP(STATUS_RESP_TRANSPORT_FAILURE) |
		STATUS_BLOCK_DEAD(0) |
		STATUS_BLOCK_LEN(1) |
		STATUS_BLOCK_SBP_STATUS(SBP_STATUS_UNSPECIFIED_ERROR));
	sbp_send_status(req);
	sbp_free_request(req);
}

/*
 * DMA_TO_DEVICE = read from initiator (SCSI WRITE)
 * DMA_FROM_DEVICE = write to initiator (SCSI READ)
 */
static int sbp_rw_data(struct sbp_target_request *req)
{
	struct sbp_session *sess = req->login->sess;
	int tcode, sg_miter_flags, max_payload, pg_size, speed, node_id,
		generation, num_pte, length, tfr_length,
		rcode = RCODE_COMPLETE;
	struct sbp_page_table_entry *pte;
	unsigned long long offset;
	struct fw_card *card;
	struct sg_mapping_iter iter;

	if (req->se_cmd.data_direction == DMA_FROM_DEVICE) {
		tcode = TCODE_WRITE_BLOCK_REQUEST;
		sg_miter_flags = SG_MITER_FROM_SG;
	} else {
		tcode = TCODE_READ_BLOCK_REQUEST;
		sg_miter_flags = SG_MITER_TO_SG;
	}

	max_payload = 4 << CMDBLK_ORB_MAX_PAYLOAD(be32_to_cpu(req->orb.misc));
	speed = CMDBLK_ORB_SPEED(be32_to_cpu(req->orb.misc));

	pg_size = CMDBLK_ORB_PG_SIZE(be32_to_cpu(req->orb.misc));
	if (pg_size) {
		pr_err("sbp_run_transaction: page size ignored\n");
		pg_size = 0x100 << pg_size;
	}

	spin_lock_bh(&sess->lock);
	card = fw_card_get(sess->card);
	node_id = sess->node_id;
	generation = sess->generation;
	spin_unlock_bh(&sess->lock);

	if (req->pg_tbl) {
		pte = req->pg_tbl;
		num_pte = CMDBLK_ORB_DATA_SIZE(be32_to_cpu(req->orb.misc));

		offset = 0;
		length = 0;
	} else {
		pte = NULL;
		num_pte = 0;

		offset = sbp2_pointer_to_addr(&req->orb.data_descriptor);
		length = req->se_cmd.data_length;
	}

	sg_miter_start(&iter, req->se_cmd.t_data_sg, req->se_cmd.t_data_nents,
		sg_miter_flags);

	while (length || num_pte) {
		if (!length) {
			offset = (u64)be16_to_cpu(pte->segment_base_hi) << 32 |
				be32_to_cpu(pte->segment_base_lo);
			length = be16_to_cpu(pte->segment_length);

			pte++;
			num_pte--;
		}

		sg_miter_next(&iter);

		tfr_length = min3(length, max_payload, (int)iter.length);

		/* FIXME: take page_size into account */

		rcode = sbp_run_transaction(card, tcode, node_id,
				generation, speed,
				offset, iter.addr, tfr_length);

		if (rcode != RCODE_COMPLETE)
			break;

		length -= tfr_length;
		offset += tfr_length;
		iter.consumed = tfr_length;
	}

	sg_miter_stop(&iter);
	fw_card_put(card);

	if (rcode == RCODE_COMPLETE) {
		WARN_ON(length != 0);
		return 0;
	} else {
		return -EIO;
	}
}

static int sbp_send_status(struct sbp_target_request *req)
{
	int ret, length;
	struct sbp_login_descriptor *login = req->login;

	length = (((be32_to_cpu(req->status.status) >> 24) & 0x07) + 1) * 4;

	ret = sbp_run_request_transaction(req, TCODE_WRITE_BLOCK_REQUEST,
			login->status_fifo_addr, &req->status, length);
	if (ret != RCODE_COMPLETE) {
		pr_debug("sbp_send_status: write failed: 0x%x\n", ret);
		return -EIO;
	}

	pr_debug("sbp_send_status: status write complete for ORB: 0x%llx\n",
			req->orb_pointer);

	return 0;
}

static void sbp_sense_mangle(struct sbp_target_request *req)
{
	struct se_cmd *se_cmd = &req->se_cmd;
	u8 *sense = req->sense_buf;
	u8 *status = req->status.data;

	WARN_ON(se_cmd->scsi_sense_length < 18);

	switch (sense[0] & 0x7f) { 		/* sfmt */
	case 0x70: /* current, fixed */
		status[0] = 0 << 6;
		break;
	case 0x71: /* deferred, fixed */
		status[0] = 1 << 6;
		break;
	case 0x72: /* current, descriptor */
	case 0x73: /* deferred, descriptor */
	default:
		/*
		 * TODO: SBP-3 specifies what we should do with descriptor
		 * format sense data
		 */
		pr_err("sbp_send_sense: unknown sense format: 0x%x\n",
			sense[0]);
		req->status.status |= cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_DEAD(0) |
			STATUS_BLOCK_LEN(1) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_REQUEST_ABORTED));
		return;
	}

	status[0] |= se_cmd->scsi_status & 0x3f;/* status */
	status[1] =
		(sense[0] & 0x80) |		/* valid */
		((sense[2] & 0xe0) >> 1) |	/* mark, eom, ili */
		(sense[2] & 0x0f);		/* sense_key */
	status[2] = se_cmd->scsi_asc;		/* sense_code */
	status[3] = se_cmd->scsi_ascq;		/* sense_qualifier */

	/* information */
	status[4] = sense[3];
	status[5] = sense[4];
	status[6] = sense[5];
	status[7] = sense[6];

	/* CDB-dependent */
	status[8] = sense[8];
	status[9] = sense[9];
	status[10] = sense[10];
	status[11] = sense[11];

	/* fru */
	status[12] = sense[14];

	/* sense_key-dependent */
	status[13] = sense[15];
	status[14] = sense[16];
	status[15] = sense[17];

	req->status.status |= cpu_to_be32(
		STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
		STATUS_BLOCK_DEAD(0) |
		STATUS_BLOCK_LEN(5) |
		STATUS_BLOCK_SBP_STATUS(SBP_STATUS_OK));
}

static int sbp_send_sense(struct sbp_target_request *req)
{
	struct se_cmd *se_cmd = &req->se_cmd;

	if (se_cmd->scsi_sense_length) {
		sbp_sense_mangle(req);
	} else {
		req->status.status |= cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_DEAD(0) |
			STATUS_BLOCK_LEN(1) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_OK));
	}

	return sbp_send_status(req);
}

static void sbp_free_request(struct sbp_target_request *req)
{
	kfree(req->pg_tbl);
	kfree(req->cmd_buf);
	kfree(req);
}

static void sbp_mgt_agent_process(struct work_struct *work)
{
	struct sbp_management_agent *agent =
		container_of(work, struct sbp_management_agent, work);
	struct sbp_management_request *req = agent->request;
	int ret;
	int status_data_len = 0;

	/* fetch the ORB from the initiator */
	ret = sbp_run_transaction(req->card, TCODE_READ_BLOCK_REQUEST,
		req->node_addr, req->generation, req->speed,
		agent->orb_offset, &req->orb, sizeof(req->orb));
	if (ret != RCODE_COMPLETE) {
		pr_debug("mgt_orb fetch failed: %x\n", ret);
		goto out;
	}

	pr_debug("mgt_orb ptr1:0x%llx ptr2:0x%llx misc:0x%x len:0x%x status_fifo:0x%llx\n",
		sbp2_pointer_to_addr(&req->orb.ptr1),
		sbp2_pointer_to_addr(&req->orb.ptr2),
		be32_to_cpu(req->orb.misc), be32_to_cpu(req->orb.length),
		sbp2_pointer_to_addr(&req->orb.status_fifo));

	if (!ORB_NOTIFY(be32_to_cpu(req->orb.misc)) ||
		ORB_REQUEST_FORMAT(be32_to_cpu(req->orb.misc)) != 0) {
		pr_err("mgt_orb bad request\n");
		goto out;
	}

	switch (MANAGEMENT_ORB_FUNCTION(be32_to_cpu(req->orb.misc))) {
	case MANAGEMENT_ORB_FUNCTION_LOGIN:
		sbp_management_request_login(agent, req, &status_data_len);
		break;

	case MANAGEMENT_ORB_FUNCTION_QUERY_LOGINS:
		sbp_management_request_query_logins(agent, req,
				&status_data_len);
		break;

	case MANAGEMENT_ORB_FUNCTION_RECONNECT:
		sbp_management_request_reconnect(agent, req, &status_data_len);
		break;

	case MANAGEMENT_ORB_FUNCTION_SET_PASSWORD:
		pr_notice("SET PASSWORD not implemented\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_REQ_TYPE_NOTSUPP));

		break;

	case MANAGEMENT_ORB_FUNCTION_LOGOUT:
		sbp_management_request_logout(agent, req, &status_data_len);
		break;

	case MANAGEMENT_ORB_FUNCTION_ABORT_TASK:
		pr_notice("ABORT TASK not implemented\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_REQ_TYPE_NOTSUPP));

		break;

	case MANAGEMENT_ORB_FUNCTION_ABORT_TASK_SET:
		pr_notice("ABORT TASK SET not implemented\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_REQ_TYPE_NOTSUPP));

		break;

	case MANAGEMENT_ORB_FUNCTION_LOGICAL_UNIT_RESET:
		pr_notice("LOGICAL UNIT RESET not implemented\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_REQ_TYPE_NOTSUPP));

		break;

	case MANAGEMENT_ORB_FUNCTION_TARGET_RESET:
		pr_notice("TARGET RESET not implemented\n");

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_REQ_TYPE_NOTSUPP));

		break;

	default:
		pr_notice("unknown management function 0x%x\n",
			MANAGEMENT_ORB_FUNCTION(be32_to_cpu(req->orb.misc)));

		req->status.status = cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_REQUEST_COMPLETE) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_REQ_TYPE_NOTSUPP));

		break;
	}

	req->status.status |= cpu_to_be32(
		STATUS_BLOCK_SRC(1) | /* Response to ORB, next_ORB absent */
		STATUS_BLOCK_LEN(DIV_ROUND_UP(status_data_len, 4) + 1) |
		STATUS_BLOCK_ORB_OFFSET_HIGH(agent->orb_offset >> 32));
	req->status.orb_low = cpu_to_be32(agent->orb_offset);

	/* write the status block back to the initiator */
	ret = sbp_run_transaction(req->card, TCODE_WRITE_BLOCK_REQUEST,
		req->node_addr, req->generation, req->speed,
		sbp2_pointer_to_addr(&req->orb.status_fifo),
		&req->status, 8 + status_data_len);
	if (ret != RCODE_COMPLETE) {
		pr_debug("mgt_orb status write failed: %x\n", ret);
		goto out;
	}

out:
	fw_card_put(req->card);
	kfree(req);

	spin_lock_bh(&agent->lock);
	agent->state = MANAGEMENT_AGENT_STATE_IDLE;
	spin_unlock_bh(&agent->lock);
}

static void sbp_mgt_agent_rw(struct fw_card *card,
	struct fw_request *request, int tcode, int destination, int source,
	int generation, unsigned long long offset, void *data, size_t length,
	void *callback_data)
{
	struct sbp_management_agent *agent = callback_data;
	struct sbp2_pointer *ptr = data;
	int rcode = RCODE_ADDRESS_ERROR;

	if (!agent->tport->enable)
		goto out;

	if ((offset != agent->handler.offset) || (length != 8))
		goto out;

	if (tcode == TCODE_WRITE_BLOCK_REQUEST) {
		struct sbp_management_request *req;
		int prev_state;

		spin_lock_bh(&agent->lock);
		prev_state = agent->state;
		agent->state = MANAGEMENT_AGENT_STATE_BUSY;
		spin_unlock_bh(&agent->lock);

		if (prev_state == MANAGEMENT_AGENT_STATE_BUSY) {
			pr_notice("ignoring management request while busy\n");
			rcode = RCODE_CONFLICT_ERROR;
			goto out;
		}

		req = kzalloc(sizeof(*req), GFP_ATOMIC);
		if (!req) {
			rcode = RCODE_CONFLICT_ERROR;
			goto out;
		}

		req->card = fw_card_get(card);
		req->generation = generation;
		req->node_addr = source;
		req->speed = fw_get_request_speed(request);

		agent->orb_offset = sbp2_pointer_to_addr(ptr);
		agent->request = req;

		queue_work(system_unbound_wq, &agent->work);
		rcode = RCODE_COMPLETE;
	} else if (tcode == TCODE_READ_BLOCK_REQUEST) {
		addr_to_sbp2_pointer(agent->orb_offset, ptr);
		rcode = RCODE_COMPLETE;
	} else {
		rcode = RCODE_TYPE_ERROR;
	}

out:
	fw_send_response(card, request, rcode);
}

static struct sbp_management_agent *sbp_management_agent_register(
		struct sbp_tport *tport)
{
	int ret;
	struct sbp_management_agent *agent;

	agent = kmalloc(sizeof(*agent), GFP_KERNEL);
	if (!agent)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&agent->lock);
	agent->tport = tport;
	agent->handler.length = 0x08;
	agent->handler.address_callback = sbp_mgt_agent_rw;
	agent->handler.callback_data = agent;
	agent->state = MANAGEMENT_AGENT_STATE_IDLE;
	INIT_WORK(&agent->work, sbp_mgt_agent_process);
	agent->orb_offset = 0;
	agent->request = NULL;

	ret = fw_core_add_address_handler(&agent->handler,
			&sbp_register_region);
	if (ret < 0) {
		kfree(agent);
		return ERR_PTR(ret);
	}

	return agent;
}

static void sbp_management_agent_unregister(struct sbp_management_agent *agent)
{
	fw_core_remove_address_handler(&agent->handler);
	cancel_work_sync(&agent->work);
	kfree(agent);
}

static int sbp_check_true(struct se_portal_group *se_tpg)
{
	return 1;
}

static int sbp_check_false(struct se_portal_group *se_tpg)
{
	return 0;
}

static char *sbp_get_fabric_name(void)
{
	return "sbp";
}

static char *sbp_get_fabric_wwn(struct se_portal_group *se_tpg)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;

	return &tport->tport_name[0];
}

static u16 sbp_get_tag(struct se_portal_group *se_tpg)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	return tpg->tport_tpgt;
}

static u32 sbp_get_default_depth(struct se_portal_group *se_tpg)
{
	return 1;
}

static struct se_node_acl *sbp_alloc_fabric_acl(struct se_portal_group *se_tpg)
{
	struct sbp_nacl *nacl;

	nacl = kzalloc(sizeof(struct sbp_nacl), GFP_KERNEL);
	if (!nacl) {
		pr_err("Unable to allocate struct sbp_nacl\n");
		return NULL;
	}

	return &nacl->se_node_acl;
}

static void sbp_release_fabric_acl(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl)
{
	struct sbp_nacl *nacl =
		container_of(se_nacl, struct sbp_nacl, se_node_acl);
	kfree(nacl);
}

static u32 sbp_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static void sbp_release_cmd(struct se_cmd *se_cmd)
{
	struct sbp_target_request *req = container_of(se_cmd,
			struct sbp_target_request, se_cmd);

	sbp_free_request(req);
}

static int sbp_shutdown_session(struct se_session *se_sess)
{
	return 0;
}

static void sbp_close_session(struct se_session *se_sess)
{
	return;
}

static u32 sbp_sess_get_index(struct se_session *se_sess)
{
	return 0;
}

static int sbp_write_pending(struct se_cmd *se_cmd)
{
	struct sbp_target_request *req = container_of(se_cmd,
			struct sbp_target_request, se_cmd);
	int ret;

	ret = sbp_rw_data(req);
	if (ret) {
		req->status.status |= cpu_to_be32(
			STATUS_BLOCK_RESP(
				STATUS_RESP_TRANSPORT_FAILURE) |
			STATUS_BLOCK_DEAD(0) |
			STATUS_BLOCK_LEN(1) |
			STATUS_BLOCK_SBP_STATUS(
				SBP_STATUS_UNSPECIFIED_ERROR));
		sbp_send_status(req);
		return ret;
	}

	target_execute_cmd(se_cmd);
	return 0;
}

static int sbp_write_pending_status(struct se_cmd *se_cmd)
{
	return 0;
}

static void sbp_set_default_node_attrs(struct se_node_acl *nacl)
{
	return;
}

static u32 sbp_get_task_tag(struct se_cmd *se_cmd)
{
	struct sbp_target_request *req = container_of(se_cmd,
			struct sbp_target_request, se_cmd);

	/* only used for printk until we do TMRs */
	return (u32)req->orb_pointer;
}

static int sbp_get_cmd_state(struct se_cmd *se_cmd)
{
	return 0;
}

static int sbp_queue_data_in(struct se_cmd *se_cmd)
{
	struct sbp_target_request *req = container_of(se_cmd,
			struct sbp_target_request, se_cmd);
	int ret;

	ret = sbp_rw_data(req);
	if (ret) {
		req->status.status |= cpu_to_be32(
			STATUS_BLOCK_RESP(STATUS_RESP_TRANSPORT_FAILURE) |
			STATUS_BLOCK_DEAD(0) |
			STATUS_BLOCK_LEN(1) |
			STATUS_BLOCK_SBP_STATUS(SBP_STATUS_UNSPECIFIED_ERROR));
		sbp_send_status(req);
		return ret;
	}

	return sbp_send_sense(req);
}

/*
 * Called after command (no data transfer) or after the write (to device)
 * operation is completed
 */
static int sbp_queue_status(struct se_cmd *se_cmd)
{
	struct sbp_target_request *req = container_of(se_cmd,
			struct sbp_target_request, se_cmd);

	return sbp_send_sense(req);
}

static int sbp_queue_tm_rsp(struct se_cmd *se_cmd)
{
	return 0;
}

static int sbp_check_stop_free(struct se_cmd *se_cmd)
{
	struct sbp_target_request *req = container_of(se_cmd,
			struct sbp_target_request, se_cmd);

	transport_generic_free_cmd(&req->se_cmd, 0);
	return 1;
}

/*
 * Handlers for Serial Bus Protocol 2/3 (SBP-2 / SBP-3)
 */
static u8 sbp_get_fabric_proto_ident(struct se_portal_group *se_tpg)
{
	/*
	 * Return a IEEE 1394 SCSI Protocol identifier for loopback operations
	 * This is defined in section 7.5.1 Table 362 in spc4r17
	 */
	return SCSI_PROTOCOL_SBP;
}

static u32 sbp_get_pr_transport_id(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int *format_code,
	unsigned char *buf)
{
	int ret;

	/*
	 * Set PROTOCOL IDENTIFIER to 3h for SBP
	 */
	buf[0] = SCSI_PROTOCOL_SBP;
	/*
	 * From spc4r17, 7.5.4.4 TransportID for initiator ports using SCSI
	 * over IEEE 1394
	 */
	ret = hex2bin(&buf[8], se_nacl->initiatorname, 8);
	if (ret < 0)
		pr_debug("sbp transport_id: invalid hex string\n");

	/*
	 * The IEEE 1394 Transport ID is a hardcoded 24-byte length
	 */
	return 24;
}

static u32 sbp_get_pr_transport_id_len(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int *format_code)
{
	*format_code = 0;
	/*
	 * From spc4r17, 7.5.4.4 TransportID for initiator ports using SCSI
	 * over IEEE 1394
	 *
	 * The SBP Transport ID is a hardcoded 24-byte length
	 */
	return 24;
}

/*
 * Used for handling SCSI fabric dependent TransportIDs in SPC-3 and above
 * Persistent Reservation SPEC_I_PT=1 and PROUT REGISTER_AND_MOVE operations.
 */
static char *sbp_parse_pr_out_transport_id(
	struct se_portal_group *se_tpg,
	const char *buf,
	u32 *out_tid_len,
	char **port_nexus_ptr)
{
	/*
	 * Assume the FORMAT CODE 00b from spc4r17, 7.5.4.4 TransportID
	 * for initiator ports using SCSI over SBP Serial SCSI Protocol
	 *
	 * The TransportID for a IEEE 1394 Initiator Port is of fixed size of
	 * 24 bytes, and IEEE 1394 does not contain a I_T nexus identifier,
	 * so we return the **port_nexus_ptr set to NULL.
	 */
	*port_nexus_ptr = NULL;
	*out_tid_len = 24;

	return (char *)&buf[8];
}

static int sbp_count_se_tpg_luns(struct se_portal_group *tpg)
{
	int i, count = 0;

	spin_lock(&tpg->tpg_lun_lock);
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		struct se_lun *se_lun = tpg->tpg_lun_list[i];

		if (se_lun->lun_status == TRANSPORT_LUN_STATUS_FREE)
			continue;

		count++;
	}
	spin_unlock(&tpg->tpg_lun_lock);

	return count;
}

static int sbp_update_unit_directory(struct sbp_tport *tport)
{
	int num_luns, num_entries, idx = 0, mgt_agt_addr, ret, i;
	u32 *data;

	if (tport->unit_directory.data) {
		fw_core_remove_descriptor(&tport->unit_directory);
		kfree(tport->unit_directory.data);
		tport->unit_directory.data = NULL;
	}

	if (!tport->enable || !tport->tpg)
		return 0;

	num_luns = sbp_count_se_tpg_luns(&tport->tpg->se_tpg);

	/*
	 * Number of entries in the final unit directory:
	 *  - all of those in the template
	 *  - management_agent
	 *  - unit_characteristics
	 *  - reconnect_timeout
	 *  - unit unique ID
	 *  - one for each LUN
	 *
	 *  MUST NOT include leaf or sub-directory entries
	 */
	num_entries = ARRAY_SIZE(sbp_unit_directory_template) + 4 + num_luns;

	if (tport->directory_id != -1)
		num_entries++;

	/* allocate num_entries + 4 for the header and unique ID leaf */
	data = kcalloc((num_entries + 4), sizeof(u32), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* directory_length */
	data[idx++] = num_entries << 16;

	/* directory_id */
	if (tport->directory_id != -1)
		data[idx++] = (CSR_DIRECTORY_ID << 24) | tport->directory_id;

	/* unit directory template */
	memcpy(&data[idx], sbp_unit_directory_template,
			sizeof(sbp_unit_directory_template));
	idx += ARRAY_SIZE(sbp_unit_directory_template);

	/* management_agent */
	mgt_agt_addr = (tport->mgt_agt->handler.offset - CSR_REGISTER_BASE) / 4;
	data[idx++] = 0x54000000 | (mgt_agt_addr & 0x00ffffff);

	/* unit_characteristics */
	data[idx++] = 0x3a000000 |
		(((tport->mgt_orb_timeout * 2) << 8) & 0xff00) |
		SBP_ORB_FETCH_SIZE;

	/* reconnect_timeout */
	data[idx++] = 0x3d000000 | (tport->max_reconnect_timeout & 0xffff);

	/* unit unique ID (leaf is just after LUNs) */
	data[idx++] = 0x8d000000 | (num_luns + 1);

	spin_lock(&tport->tpg->se_tpg.tpg_lun_lock);
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		struct se_lun *se_lun = tport->tpg->se_tpg.tpg_lun_list[i];
		struct se_device *dev;
		int type;

		if (se_lun->lun_status == TRANSPORT_LUN_STATUS_FREE)
			continue;

		spin_unlock(&tport->tpg->se_tpg.tpg_lun_lock);

		dev = se_lun->lun_se_dev;
		type = dev->transport->get_device_type(dev);

		/* logical_unit_number */
		data[idx++] = 0x14000000 |
			((type << 16) & 0x1f0000) |
			(se_lun->unpacked_lun & 0xffff);

		spin_lock(&tport->tpg->se_tpg.tpg_lun_lock);
	}
	spin_unlock(&tport->tpg->se_tpg.tpg_lun_lock);

	/* unit unique ID leaf */
	data[idx++] = 2 << 16;
	data[idx++] = tport->guid >> 32;
	data[idx++] = tport->guid;

	tport->unit_directory.length = idx;
	tport->unit_directory.key = (CSR_DIRECTORY | CSR_UNIT) << 24;
	tport->unit_directory.data = data;

	ret = fw_core_add_descriptor(&tport->unit_directory);
	if (ret < 0) {
		kfree(tport->unit_directory.data);
		tport->unit_directory.data = NULL;
	}

	return ret;
}

static ssize_t sbp_parse_wwn(const char *name, u64 *wwn)
{
	const char *cp;
	char c, nibble;
	int pos = 0, err;

	*wwn = 0;
	for (cp = name; cp < &name[SBP_NAMELEN - 1]; cp++) {
		c = *cp;
		if (c == '\n' && cp[1] == '\0')
			continue;
		if (c == '\0') {
			err = 2;
			if (pos != 16)
				goto fail;
			return cp - name;
		}
		err = 3;
		if (isdigit(c))
			nibble = c - '0';
		else if (isxdigit(c))
			nibble = tolower(c) - 'a' + 10;
		else
			goto fail;
		*wwn = (*wwn << 4) | nibble;
		pos++;
	}
	err = 4;
fail:
	printk(KERN_INFO "err %u len %zu pos %u\n",
			err, cp - name, pos);
	return -1;
}

static ssize_t sbp_format_wwn(char *buf, size_t len, u64 wwn)
{
	return snprintf(buf, len, "%016llx", wwn);
}

static struct se_node_acl *sbp_make_nodeacl(
		struct se_portal_group *se_tpg,
		struct config_group *group,
		const char *name)
{
	struct se_node_acl *se_nacl, *se_nacl_new;
	struct sbp_nacl *nacl;
	u64 guid = 0;
	u32 nexus_depth = 1;

	if (sbp_parse_wwn(name, &guid) < 0)
		return ERR_PTR(-EINVAL);

	se_nacl_new = sbp_alloc_fabric_acl(se_tpg);
	if (!se_nacl_new)
		return ERR_PTR(-ENOMEM);

	/*
	 * se_nacl_new may be released by core_tpg_add_initiator_node_acl()
	 * when converting a NodeACL from demo mode -> explict
	 */
	se_nacl = core_tpg_add_initiator_node_acl(se_tpg, se_nacl_new,
			name, nexus_depth);
	if (IS_ERR(se_nacl)) {
		sbp_release_fabric_acl(se_tpg, se_nacl_new);
		return se_nacl;
	}

	nacl = container_of(se_nacl, struct sbp_nacl, se_node_acl);
	nacl->guid = guid;
	sbp_format_wwn(nacl->iport_name, SBP_NAMELEN, guid);

	return se_nacl;
}

static void sbp_drop_nodeacl(struct se_node_acl *se_acl)
{
	struct sbp_nacl *nacl =
		container_of(se_acl, struct sbp_nacl, se_node_acl);

	core_tpg_del_initiator_node_acl(se_acl->se_tpg, se_acl, 1);
	kfree(nacl);
}

static int sbp_post_link_lun(
		struct se_portal_group *se_tpg,
		struct se_lun *se_lun)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);

	return sbp_update_unit_directory(tpg->tport);
}

static void sbp_pre_unlink_lun(
		struct se_portal_group *se_tpg,
		struct se_lun *se_lun)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	int ret;

	if (sbp_count_se_tpg_luns(&tpg->se_tpg) == 0)
		tport->enable = 0;

	ret = sbp_update_unit_directory(tport);
	if (ret < 0)
		pr_err("unlink LUN: failed to update unit directory\n");
}

static struct se_portal_group *sbp_make_tpg(
		struct se_wwn *wwn,
		struct config_group *group,
		const char *name)
{
	struct sbp_tport *tport =
		container_of(wwn, struct sbp_tport, tport_wwn);

	struct sbp_tpg *tpg;
	unsigned long tpgt;
	int ret;

	if (strstr(name, "tpgt_") != name)
		return ERR_PTR(-EINVAL);
	if (kstrtoul(name + 5, 10, &tpgt) || tpgt > UINT_MAX)
		return ERR_PTR(-EINVAL);

	if (tport->tpg) {
		pr_err("Only one TPG per Unit is possible.\n");
		return ERR_PTR(-EBUSY);
	}

	tpg = kzalloc(sizeof(*tpg), GFP_KERNEL);
	if (!tpg) {
		pr_err("Unable to allocate struct sbp_tpg\n");
		return ERR_PTR(-ENOMEM);
	}

	tpg->tport = tport;
	tpg->tport_tpgt = tpgt;
	tport->tpg = tpg;

	/* default attribute values */
	tport->enable = 0;
	tport->directory_id = -1;
	tport->mgt_orb_timeout = 15;
	tport->max_reconnect_timeout = 5;
	tport->max_logins_per_lun = 1;

	tport->mgt_agt = sbp_management_agent_register(tport);
	if (IS_ERR(tport->mgt_agt)) {
		ret = PTR_ERR(tport->mgt_agt);
		goto out_free_tpg;
	}

	ret = core_tpg_register(&sbp_fabric_configfs->tf_ops, wwn,
			&tpg->se_tpg, (void *)tpg,
			TRANSPORT_TPG_TYPE_NORMAL);
	if (ret < 0)
		goto out_unreg_mgt_agt;

	return &tpg->se_tpg;

out_unreg_mgt_agt:
	sbp_management_agent_unregister(tport->mgt_agt);
out_free_tpg:
	tport->tpg = NULL;
	kfree(tpg);
	return ERR_PTR(ret);
}

static void sbp_drop_tpg(struct se_portal_group *se_tpg)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;

	core_tpg_deregister(se_tpg);
	sbp_management_agent_unregister(tport->mgt_agt);
	tport->tpg = NULL;
	kfree(tpg);
}

static struct se_wwn *sbp_make_tport(
		struct target_fabric_configfs *tf,
		struct config_group *group,
		const char *name)
{
	struct sbp_tport *tport;
	u64 guid = 0;

	if (sbp_parse_wwn(name, &guid) < 0)
		return ERR_PTR(-EINVAL);

	tport = kzalloc(sizeof(*tport), GFP_KERNEL);
	if (!tport) {
		pr_err("Unable to allocate struct sbp_tport\n");
		return ERR_PTR(-ENOMEM);
	}

	tport->guid = guid;
	sbp_format_wwn(tport->tport_name, SBP_NAMELEN, guid);

	return &tport->tport_wwn;
}

static void sbp_drop_tport(struct se_wwn *wwn)
{
	struct sbp_tport *tport =
		container_of(wwn, struct sbp_tport, tport_wwn);

	kfree(tport);
}

static ssize_t sbp_wwn_show_attr_version(
		struct target_fabric_configfs *tf,
		char *page)
{
	return sprintf(page, "FireWire SBP fabric module %s\n", SBP_VERSION);
}

TF_WWN_ATTR_RO(sbp, version);

static struct configfs_attribute *sbp_wwn_attrs[] = {
	&sbp_wwn_version.attr,
	NULL,
};

static ssize_t sbp_tpg_show_directory_id(
		struct se_portal_group *se_tpg,
		char *page)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;

	if (tport->directory_id == -1)
		return sprintf(page, "implicit\n");
	else
		return sprintf(page, "%06x\n", tport->directory_id);
}

static ssize_t sbp_tpg_store_directory_id(
		struct se_portal_group *se_tpg,
		const char *page,
		size_t count)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	unsigned long val;

	if (tport->enable) {
		pr_err("Cannot change the directory_id on an active target.\n");
		return -EBUSY;
	}

	if (strstr(page, "implicit") == page) {
		tport->directory_id = -1;
	} else {
		if (kstrtoul(page, 16, &val) < 0)
			return -EINVAL;
		if (val > 0xffffff)
			return -EINVAL;

		tport->directory_id = val;
	}

	return count;
}

static ssize_t sbp_tpg_show_enable(
		struct se_portal_group *se_tpg,
		char *page)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	return sprintf(page, "%d\n", tport->enable);
}

static ssize_t sbp_tpg_store_enable(
		struct se_portal_group *se_tpg,
		const char *page,
		size_t count)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	unsigned long val;
	int ret;

	if (kstrtoul(page, 0, &val) < 0)
		return -EINVAL;
	if ((val != 0) && (val != 1))
		return -EINVAL;

	if (tport->enable == val)
		return count;

	if (val) {
		if (sbp_count_se_tpg_luns(&tpg->se_tpg) == 0) {
			pr_err("Cannot enable a target with no LUNs!\n");
			return -EINVAL;
		}
	} else {
		/* XXX: force-shutdown sessions instead? */
		spin_lock_bh(&se_tpg->session_lock);
		if (!list_empty(&se_tpg->tpg_sess_list)) {
			spin_unlock_bh(&se_tpg->session_lock);
			return -EBUSY;
		}
		spin_unlock_bh(&se_tpg->session_lock);
	}

	tport->enable = val;

	ret = sbp_update_unit_directory(tport);
	if (ret < 0) {
		pr_err("Could not update Config ROM\n");
		return ret;
	}

	return count;
}

TF_TPG_BASE_ATTR(sbp, directory_id, S_IRUGO | S_IWUSR);
TF_TPG_BASE_ATTR(sbp, enable, S_IRUGO | S_IWUSR);

static struct configfs_attribute *sbp_tpg_base_attrs[] = {
	&sbp_tpg_directory_id.attr,
	&sbp_tpg_enable.attr,
	NULL,
};

static ssize_t sbp_tpg_attrib_show_mgt_orb_timeout(
		struct se_portal_group *se_tpg,
		char *page)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	return sprintf(page, "%d\n", tport->mgt_orb_timeout);
}

static ssize_t sbp_tpg_attrib_store_mgt_orb_timeout(
		struct se_portal_group *se_tpg,
		const char *page,
		size_t count)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	unsigned long val;
	int ret;

	if (kstrtoul(page, 0, &val) < 0)
		return -EINVAL;
	if ((val < 1) || (val > 127))
		return -EINVAL;

	if (tport->mgt_orb_timeout == val)
		return count;

	tport->mgt_orb_timeout = val;

	ret = sbp_update_unit_directory(tport);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t sbp_tpg_attrib_show_max_reconnect_timeout(
		struct se_portal_group *se_tpg,
		char *page)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	return sprintf(page, "%d\n", tport->max_reconnect_timeout);
}

static ssize_t sbp_tpg_attrib_store_max_reconnect_timeout(
		struct se_portal_group *se_tpg,
		const char *page,
		size_t count)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	unsigned long val;
	int ret;

	if (kstrtoul(page, 0, &val) < 0)
		return -EINVAL;
	if ((val < 1) || (val > 32767))
		return -EINVAL;

	if (tport->max_reconnect_timeout == val)
		return count;

	tport->max_reconnect_timeout = val;

	ret = sbp_update_unit_directory(tport);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t sbp_tpg_attrib_show_max_logins_per_lun(
		struct se_portal_group *se_tpg,
		char *page)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	return sprintf(page, "%d\n", tport->max_logins_per_lun);
}

static ssize_t sbp_tpg_attrib_store_max_logins_per_lun(
		struct se_portal_group *se_tpg,
		const char *page,
		size_t count)
{
	struct sbp_tpg *tpg = container_of(se_tpg, struct sbp_tpg, se_tpg);
	struct sbp_tport *tport = tpg->tport;
	unsigned long val;

	if (kstrtoul(page, 0, &val) < 0)
		return -EINVAL;
	if ((val < 1) || (val > 127))
		return -EINVAL;

	/* XXX: also check against current count? */

	tport->max_logins_per_lun = val;

	return count;
}

TF_TPG_ATTRIB_ATTR(sbp, mgt_orb_timeout, S_IRUGO | S_IWUSR);
TF_TPG_ATTRIB_ATTR(sbp, max_reconnect_timeout, S_IRUGO | S_IWUSR);
TF_TPG_ATTRIB_ATTR(sbp, max_logins_per_lun, S_IRUGO | S_IWUSR);

static struct configfs_attribute *sbp_tpg_attrib_attrs[] = {
	&sbp_tpg_attrib_mgt_orb_timeout.attr,
	&sbp_tpg_attrib_max_reconnect_timeout.attr,
	&sbp_tpg_attrib_max_logins_per_lun.attr,
	NULL,
};

static struct target_core_fabric_ops sbp_ops = {
	.get_fabric_name		= sbp_get_fabric_name,
	.get_fabric_proto_ident		= sbp_get_fabric_proto_ident,
	.tpg_get_wwn			= sbp_get_fabric_wwn,
	.tpg_get_tag			= sbp_get_tag,
	.tpg_get_default_depth		= sbp_get_default_depth,
	.tpg_get_pr_transport_id	= sbp_get_pr_transport_id,
	.tpg_get_pr_transport_id_len	= sbp_get_pr_transport_id_len,
	.tpg_parse_pr_out_transport_id	= sbp_parse_pr_out_transport_id,
	.tpg_check_demo_mode		= sbp_check_true,
	.tpg_check_demo_mode_cache	= sbp_check_true,
	.tpg_check_demo_mode_write_protect = sbp_check_false,
	.tpg_check_prod_mode_write_protect = sbp_check_false,
	.tpg_alloc_fabric_acl		= sbp_alloc_fabric_acl,
	.tpg_release_fabric_acl		= sbp_release_fabric_acl,
	.tpg_get_inst_index		= sbp_tpg_get_inst_index,
	.release_cmd			= sbp_release_cmd,
	.shutdown_session		= sbp_shutdown_session,
	.close_session			= sbp_close_session,
	.sess_get_index			= sbp_sess_get_index,
	.write_pending			= sbp_write_pending,
	.write_pending_status		= sbp_write_pending_status,
	.set_default_node_attributes	= sbp_set_default_node_attrs,
	.get_task_tag			= sbp_get_task_tag,
	.get_cmd_state			= sbp_get_cmd_state,
	.queue_data_in			= sbp_queue_data_in,
	.queue_status			= sbp_queue_status,
	.queue_tm_rsp			= sbp_queue_tm_rsp,
	.check_stop_free		= sbp_check_stop_free,

	.fabric_make_wwn		= sbp_make_tport,
	.fabric_drop_wwn		= sbp_drop_tport,
	.fabric_make_tpg		= sbp_make_tpg,
	.fabric_drop_tpg		= sbp_drop_tpg,
	.fabric_post_link		= sbp_post_link_lun,
	.fabric_pre_unlink		= sbp_pre_unlink_lun,
	.fabric_make_np			= NULL,
	.fabric_drop_np			= NULL,
	.fabric_make_nodeacl		= sbp_make_nodeacl,
	.fabric_drop_nodeacl		= sbp_drop_nodeacl,
};

static int sbp_register_configfs(void)
{
	struct target_fabric_configfs *fabric;
	int ret;

	fabric = target_fabric_configfs_init(THIS_MODULE, "sbp");
	if (IS_ERR(fabric)) {
		pr_err("target_fabric_configfs_init() failed\n");
		return PTR_ERR(fabric);
	}

	fabric->tf_ops = sbp_ops;

	/*
	 * Setup default attribute lists for various fabric->tf_cit_tmpl
	 */
	TF_CIT_TMPL(fabric)->tfc_wwn_cit.ct_attrs = sbp_wwn_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_base_cit.ct_attrs = sbp_tpg_base_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_attrib_cit.ct_attrs = sbp_tpg_attrib_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_param_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_np_base_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_base_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_attrib_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_auth_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_param_cit.ct_attrs = NULL;

	ret = target_fabric_configfs_register(fabric);
	if (ret < 0) {
		pr_err("target_fabric_configfs_register() failed for SBP\n");
		return ret;
	}

	sbp_fabric_configfs = fabric;

	return 0;
};

static void sbp_deregister_configfs(void)
{
	if (!sbp_fabric_configfs)
		return;

	target_fabric_configfs_deregister(sbp_fabric_configfs);
	sbp_fabric_configfs = NULL;
};

static int __init sbp_init(void)
{
	int ret;

	ret = sbp_register_configfs();
	if (ret < 0)
		return ret;

	return 0;
};

static void __exit sbp_exit(void)
{
	sbp_deregister_configfs();
};

MODULE_DESCRIPTION("FireWire SBP fabric driver");
MODULE_LICENSE("GPL");
module_init(sbp_init);
module_exit(sbp_exit);
