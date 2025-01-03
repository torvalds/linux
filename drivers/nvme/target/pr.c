// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe over Fabrics Persist Reservation.
 * Copyright (c) 2024 Guixin Liu, Alibaba Group.
 * All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/unaligned.h>
#include "nvmet.h"

#define NVMET_PR_NOTIFI_MASK_ALL \
	(1 << NVME_PR_NOTIFY_BIT_REG_PREEMPTED | \
	 1 << NVME_PR_NOTIFY_BIT_RESV_RELEASED | \
	 1 << NVME_PR_NOTIFY_BIT_RESV_PREEMPTED)

static inline bool nvmet_pr_parse_ignore_key(u32 cdw10)
{
	/* Ignore existing key, bit 03. */
	return (cdw10 >> 3) & 1;
}

static inline struct nvmet_ns *nvmet_pr_to_ns(struct nvmet_pr *pr)
{
	return container_of(pr, struct nvmet_ns, pr);
}

static struct nvmet_pr_registrant *
nvmet_pr_find_registrant(struct nvmet_pr *pr, uuid_t *hostid)
{
	struct nvmet_pr_registrant *reg;

	list_for_each_entry_rcu(reg, &pr->registrant_list, entry) {
		if (uuid_equal(&reg->hostid, hostid))
			return reg;
	}
	return NULL;
}

u16 nvmet_set_feat_resv_notif_mask(struct nvmet_req *req, u32 mask)
{
	u32 nsid = le32_to_cpu(req->cmd->common.nsid);
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_ns *ns;
	unsigned long idx;
	u16 status;

	if (mask & ~(NVMET_PR_NOTIFI_MASK_ALL)) {
		req->error_loc = offsetof(struct nvme_common_command, cdw11);
		return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
	}

	if (nsid != U32_MAX) {
		status = nvmet_req_find_ns(req);
		if (status)
			return status;
		if (!req->ns->pr.enable)
			return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;

		WRITE_ONCE(req->ns->pr.notify_mask, mask);
		goto success;
	}

	xa_for_each(&ctrl->subsys->namespaces, idx, ns) {
		if (ns->pr.enable)
			WRITE_ONCE(ns->pr.notify_mask, mask);
	}

success:
	nvmet_set_result(req, mask);
	return NVME_SC_SUCCESS;
}

u16 nvmet_get_feat_resv_notif_mask(struct nvmet_req *req)
{
	u16 status;

	status = nvmet_req_find_ns(req);
	if (status)
		return status;

	if (!req->ns->pr.enable)
		return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;

	nvmet_set_result(req, READ_ONCE(req->ns->pr.notify_mask));
	return status;
}

void nvmet_execute_get_log_page_resv(struct nvmet_req *req)
{
	struct nvmet_pr_log_mgr *log_mgr = &req->sq->ctrl->pr_log_mgr;
	struct nvme_pr_log next_log = {0};
	struct nvme_pr_log log = {0};
	u16 status = NVME_SC_SUCCESS;
	u64 lost_count;
	u64 cur_count;
	u64 next_count;

	mutex_lock(&log_mgr->lock);
	if (!kfifo_get(&log_mgr->log_queue, &log))
		goto out;

	/*
	 * We can't get the last in kfifo.
	 * Utilize the current count and the count from the next log to
	 * calculate the number of lost logs, while also addressing cases
	 * of overflow. If there is no subsequent log, the number of lost
	 * logs is equal to the lost_count within the nvmet_pr_log_mgr.
	 */
	cur_count = le64_to_cpu(log.count);
	if (kfifo_peek(&log_mgr->log_queue, &next_log)) {
		next_count = le64_to_cpu(next_log.count);
		if (next_count > cur_count)
			lost_count = next_count - cur_count - 1;
		else
			lost_count = U64_MAX - cur_count + next_count - 1;
	} else {
		lost_count = log_mgr->lost_count;
	}

	log.count = cpu_to_le64((cur_count + lost_count) == 0 ?
				1 : (cur_count + lost_count));
	log_mgr->lost_count -= lost_count;

	log.nr_pages = kfifo_len(&log_mgr->log_queue);

out:
	status = nvmet_copy_to_sgl(req, 0, &log, sizeof(log));
	mutex_unlock(&log_mgr->lock);
	nvmet_req_complete(req, status);
}

static void nvmet_pr_add_resv_log(struct nvmet_ctrl *ctrl, u8 log_type,
				  u32 nsid)
{
	struct nvmet_pr_log_mgr *log_mgr = &ctrl->pr_log_mgr;
	struct nvme_pr_log log = {0};

	mutex_lock(&log_mgr->lock);
	log_mgr->counter++;
	if (log_mgr->counter == 0)
		log_mgr->counter = 1;

	log.count = cpu_to_le64(log_mgr->counter);
	log.type = log_type;
	log.nsid = cpu_to_le32(nsid);

	if (!kfifo_put(&log_mgr->log_queue, log)) {
		pr_info("a reservation log lost, cntlid:%d, log_type:%d, nsid:%d\n",
			ctrl->cntlid, log_type, nsid);
		log_mgr->lost_count++;
	}

	mutex_unlock(&log_mgr->lock);
}

static void nvmet_pr_resv_released(struct nvmet_pr *pr, uuid_t *hostid)
{
	struct nvmet_ns *ns = nvmet_pr_to_ns(pr);
	struct nvmet_subsys *subsys = ns->subsys;
	struct nvmet_ctrl *ctrl;

	if (test_bit(NVME_PR_NOTIFY_BIT_RESV_RELEASED, &pr->notify_mask))
		return;

	mutex_lock(&subsys->lock);
	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry) {
		if (!uuid_equal(&ctrl->hostid, hostid) &&
		    nvmet_pr_find_registrant(pr, &ctrl->hostid)) {
			nvmet_pr_add_resv_log(ctrl,
				NVME_PR_LOG_RESERVATION_RELEASED, ns->nsid);
			nvmet_add_async_event(ctrl, NVME_AER_CSS,
				NVME_AEN_RESV_LOG_PAGE_AVALIABLE,
				NVME_LOG_RESERVATION);
		}
	}
	mutex_unlock(&subsys->lock);
}

static void nvmet_pr_send_event_to_host(struct nvmet_pr *pr, uuid_t *hostid,
					  u8 log_type)
{
	struct nvmet_ns *ns = nvmet_pr_to_ns(pr);
	struct nvmet_subsys *subsys = ns->subsys;
	struct nvmet_ctrl *ctrl;

	mutex_lock(&subsys->lock);
	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry) {
		if (uuid_equal(hostid, &ctrl->hostid)) {
			nvmet_pr_add_resv_log(ctrl, log_type, ns->nsid);
			nvmet_add_async_event(ctrl, NVME_AER_CSS,
				NVME_AEN_RESV_LOG_PAGE_AVALIABLE,
				NVME_LOG_RESERVATION);
		}
	}
	mutex_unlock(&subsys->lock);
}

static void nvmet_pr_resv_preempted(struct nvmet_pr *pr, uuid_t *hostid)
{
	if (test_bit(NVME_PR_NOTIFY_BIT_RESV_PREEMPTED, &pr->notify_mask))
		return;

	nvmet_pr_send_event_to_host(pr, hostid,
		NVME_PR_LOG_RESERVATOIN_PREEMPTED);
}

static void nvmet_pr_registration_preempted(struct nvmet_pr *pr,
					    uuid_t *hostid)
{
	if (test_bit(NVME_PR_NOTIFY_BIT_REG_PREEMPTED, &pr->notify_mask))
		return;

	nvmet_pr_send_event_to_host(pr, hostid,
		NVME_PR_LOG_REGISTRATION_PREEMPTED);
}

static inline void nvmet_pr_set_new_holder(struct nvmet_pr *pr, u8 new_rtype,
					   struct nvmet_pr_registrant *reg)
{
	reg->rtype = new_rtype;
	rcu_assign_pointer(pr->holder, reg);
}

static u16 nvmet_pr_register(struct nvmet_req *req,
			     struct nvmet_pr_register_data *d)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_pr_registrant *new, *reg;
	struct nvmet_pr *pr = &req->ns->pr;
	u16 status = NVME_SC_SUCCESS;
	u64 nrkey = le64_to_cpu(d->nrkey);

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NVME_SC_INTERNAL;

	down(&pr->pr_sem);
	reg = nvmet_pr_find_registrant(pr, &ctrl->hostid);
	if (reg) {
		if (reg->rkey != nrkey)
			status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
		kfree(new);
		goto out;
	}

	memset(new, 0, sizeof(*new));
	INIT_LIST_HEAD(&new->entry);
	new->rkey = nrkey;
	uuid_copy(&new->hostid, &ctrl->hostid);
	list_add_tail_rcu(&new->entry, &pr->registrant_list);

out:
	up(&pr->pr_sem);
	return status;
}

static void nvmet_pr_unregister_one(struct nvmet_pr *pr,
				    struct nvmet_pr_registrant *reg)
{
	struct nvmet_pr_registrant *first_reg;
	struct nvmet_pr_registrant *holder;
	u8 original_rtype;

	list_del_rcu(&reg->entry);

	holder = rcu_dereference_protected(pr->holder, 1);
	if (reg != holder)
		goto out;

	original_rtype = holder->rtype;
	if (original_rtype == NVME_PR_WRITE_EXCLUSIVE_ALL_REGS ||
	    original_rtype == NVME_PR_EXCLUSIVE_ACCESS_ALL_REGS) {
		first_reg = list_first_or_null_rcu(&pr->registrant_list,
				struct nvmet_pr_registrant, entry);
		if (first_reg)
			first_reg->rtype = original_rtype;
		rcu_assign_pointer(pr->holder, first_reg);
	} else {
		rcu_assign_pointer(pr->holder, NULL);

		if (original_rtype == NVME_PR_WRITE_EXCLUSIVE_REG_ONLY ||
		    original_rtype == NVME_PR_EXCLUSIVE_ACCESS_REG_ONLY)
			nvmet_pr_resv_released(pr, &reg->hostid);
	}
out:
	kfree_rcu(reg, rcu);
}

static u16 nvmet_pr_unregister(struct nvmet_req *req,
			       struct nvmet_pr_register_data *d,
			       bool ignore_key)
{
	u16 status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_pr *pr = &req->ns->pr;
	struct nvmet_pr_registrant *reg;

	down(&pr->pr_sem);
	list_for_each_entry_rcu(reg, &pr->registrant_list, entry) {
		if (uuid_equal(&reg->hostid, &ctrl->hostid)) {
			if (ignore_key || reg->rkey == le64_to_cpu(d->crkey)) {
				status = NVME_SC_SUCCESS;
				nvmet_pr_unregister_one(pr, reg);
			}
			break;
		}
	}
	up(&pr->pr_sem);

	return status;
}

static void nvmet_pr_update_reg_rkey(struct nvmet_pr_registrant *reg,
				     void *attr)
{
	reg->rkey = *(u64 *)attr;
}

static u16 nvmet_pr_update_reg_attr(struct nvmet_pr *pr,
			struct nvmet_pr_registrant *reg,
			void (*change_attr)(struct nvmet_pr_registrant *reg,
			void *attr),
			void *attr)
{
	struct nvmet_pr_registrant *holder;
	struct nvmet_pr_registrant *new;

	holder = rcu_dereference_protected(pr->holder, 1);
	if (reg != holder) {
		change_attr(reg, attr);
		return NVME_SC_SUCCESS;
	}

	new = kmalloc(sizeof(*new), GFP_ATOMIC);
	if (!new)
		return NVME_SC_INTERNAL;

	new->rkey = holder->rkey;
	new->rtype = holder->rtype;
	uuid_copy(&new->hostid, &holder->hostid);
	INIT_LIST_HEAD(&new->entry);

	change_attr(new, attr);
	list_replace_rcu(&holder->entry, &new->entry);
	rcu_assign_pointer(pr->holder, new);
	kfree_rcu(holder, rcu);

	return NVME_SC_SUCCESS;
}

static u16 nvmet_pr_replace(struct nvmet_req *req,
			    struct nvmet_pr_register_data *d,
			    bool ignore_key)
{
	u16 status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_pr *pr = &req->ns->pr;
	struct nvmet_pr_registrant *reg;
	u64 nrkey = le64_to_cpu(d->nrkey);

	down(&pr->pr_sem);
	list_for_each_entry_rcu(reg, &pr->registrant_list, entry) {
		if (uuid_equal(&reg->hostid, &ctrl->hostid)) {
			if (ignore_key || reg->rkey == le64_to_cpu(d->crkey))
				status = nvmet_pr_update_reg_attr(pr, reg,
						nvmet_pr_update_reg_rkey,
						&nrkey);
			break;
		}
	}
	up(&pr->pr_sem);
	return status;
}

static void nvmet_execute_pr_register(struct nvmet_req *req)
{
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10);
	bool ignore_key = nvmet_pr_parse_ignore_key(cdw10);
	struct nvmet_pr_register_data *d;
	u8 reg_act = cdw10 & 0x07; /* Reservation Register Action, bit 02:00 */
	u16 status;

	d = kmalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	status = nvmet_copy_from_sgl(req, 0, d, sizeof(*d));
	if (status)
		goto free_data;

	switch (reg_act) {
	case NVME_PR_REGISTER_ACT_REG:
		status = nvmet_pr_register(req, d);
		break;
	case NVME_PR_REGISTER_ACT_UNREG:
		status = nvmet_pr_unregister(req, d, ignore_key);
		break;
	case NVME_PR_REGISTER_ACT_REPLACE:
		status = nvmet_pr_replace(req, d, ignore_key);
		break;
	default:
		req->error_loc = offsetof(struct nvme_common_command, cdw10);
		status = NVME_SC_INVALID_OPCODE | NVME_STATUS_DNR;
		break;
	}
free_data:
	kfree(d);
out:
	if (!status)
		atomic_inc(&req->ns->pr.generation);
	nvmet_req_complete(req, status);
}

static u16 nvmet_pr_acquire(struct nvmet_req *req,
			    struct nvmet_pr_registrant *reg,
			    u8 rtype)
{
	struct nvmet_pr *pr = &req->ns->pr;
	struct nvmet_pr_registrant *holder;

	holder = rcu_dereference_protected(pr->holder, 1);
	if (holder && reg != holder)
		return  NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
	if (holder && reg == holder) {
		if (holder->rtype == rtype)
			return NVME_SC_SUCCESS;
		return NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
	}

	nvmet_pr_set_new_holder(pr, rtype, reg);
	return NVME_SC_SUCCESS;
}

static void nvmet_pr_confirm_ns_pc_ref(struct percpu_ref *ref)
{
	struct nvmet_pr_per_ctrl_ref *pc_ref =
		container_of(ref, struct nvmet_pr_per_ctrl_ref, ref);

	complete(&pc_ref->confirm_done);
}

static void nvmet_pr_set_ctrl_to_abort(struct nvmet_req *req, uuid_t *hostid)
{
	struct nvmet_pr_per_ctrl_ref *pc_ref;
	struct nvmet_ns *ns = req->ns;
	unsigned long idx;

	xa_for_each(&ns->pr_per_ctrl_refs, idx, pc_ref) {
		if (uuid_equal(&pc_ref->hostid, hostid)) {
			percpu_ref_kill_and_confirm(&pc_ref->ref,
						nvmet_pr_confirm_ns_pc_ref);
			wait_for_completion(&pc_ref->confirm_done);
		}
	}
}

static u16 nvmet_pr_unreg_all_host_by_prkey(struct nvmet_req *req, u64 prkey,
					    uuid_t *send_hostid,
					    bool abort)
{
	u16 status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
	struct nvmet_pr_registrant *reg, *tmp;
	struct nvmet_pr *pr = &req->ns->pr;
	uuid_t hostid;

	list_for_each_entry_safe(reg, tmp, &pr->registrant_list, entry) {
		if (reg->rkey == prkey) {
			status = NVME_SC_SUCCESS;
			uuid_copy(&hostid, &reg->hostid);
			if (abort)
				nvmet_pr_set_ctrl_to_abort(req, &hostid);
			nvmet_pr_unregister_one(pr, reg);
			if (!uuid_equal(&hostid, send_hostid))
				nvmet_pr_registration_preempted(pr, &hostid);
		}
	}
	return status;
}

static void nvmet_pr_unreg_all_others_by_prkey(struct nvmet_req *req,
					       u64 prkey,
					       uuid_t *send_hostid,
					       bool abort)
{
	struct nvmet_pr_registrant *reg, *tmp;
	struct nvmet_pr *pr = &req->ns->pr;
	uuid_t hostid;

	list_for_each_entry_safe(reg, tmp, &pr->registrant_list, entry) {
		if (reg->rkey == prkey &&
		    !uuid_equal(&reg->hostid, send_hostid)) {
			uuid_copy(&hostid, &reg->hostid);
			if (abort)
				nvmet_pr_set_ctrl_to_abort(req, &hostid);
			nvmet_pr_unregister_one(pr, reg);
			nvmet_pr_registration_preempted(pr, &hostid);
		}
	}
}

static void nvmet_pr_unreg_all_others(struct nvmet_req *req,
				      uuid_t *send_hostid,
				      bool abort)
{
	struct nvmet_pr_registrant *reg, *tmp;
	struct nvmet_pr *pr = &req->ns->pr;
	uuid_t hostid;

	list_for_each_entry_safe(reg, tmp, &pr->registrant_list, entry) {
		if (!uuid_equal(&reg->hostid, send_hostid)) {
			uuid_copy(&hostid, &reg->hostid);
			if (abort)
				nvmet_pr_set_ctrl_to_abort(req, &hostid);
			nvmet_pr_unregister_one(pr, reg);
			nvmet_pr_registration_preempted(pr, &hostid);
		}
	}
}

static void nvmet_pr_update_holder_rtype(struct nvmet_pr_registrant *reg,
					 void *attr)
{
	u8 new_rtype = *(u8 *)attr;

	reg->rtype = new_rtype;
}

static u16 nvmet_pr_preempt(struct nvmet_req *req,
			    struct nvmet_pr_registrant *reg,
			    u8 rtype,
			    struct nvmet_pr_acquire_data *d,
			    bool abort)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_pr *pr = &req->ns->pr;
	struct nvmet_pr_registrant *holder;
	enum nvme_pr_type original_rtype;
	u64 prkey = le64_to_cpu(d->prkey);
	u16 status;

	holder = rcu_dereference_protected(pr->holder, 1);
	if (!holder)
		return nvmet_pr_unreg_all_host_by_prkey(req, prkey,
					&ctrl->hostid, abort);

	original_rtype = holder->rtype;
	if (original_rtype == NVME_PR_WRITE_EXCLUSIVE_ALL_REGS ||
	    original_rtype == NVME_PR_EXCLUSIVE_ACCESS_ALL_REGS) {
		if (!prkey) {
			/*
			 * To prevent possible access from other hosts, and
			 * avoid terminate the holder, set the new holder
			 * first before unregistering.
			 */
			nvmet_pr_set_new_holder(pr, rtype, reg);
			nvmet_pr_unreg_all_others(req, &ctrl->hostid, abort);
			return NVME_SC_SUCCESS;
		}
		return nvmet_pr_unreg_all_host_by_prkey(req, prkey,
				&ctrl->hostid, abort);
	}

	if (holder == reg) {
		status = nvmet_pr_update_reg_attr(pr, holder,
				nvmet_pr_update_holder_rtype, &rtype);
		if (!status && original_rtype != rtype)
			nvmet_pr_resv_released(pr, &reg->hostid);
		return status;
	}

	if (prkey == holder->rkey) {
		/*
		 * Same as before, set the new holder first.
		 */
		nvmet_pr_set_new_holder(pr, rtype, reg);
		nvmet_pr_unreg_all_others_by_prkey(req, prkey, &ctrl->hostid,
						abort);
		if (original_rtype != rtype)
			nvmet_pr_resv_released(pr, &reg->hostid);
		return NVME_SC_SUCCESS;
	}

	if (prkey)
		return nvmet_pr_unreg_all_host_by_prkey(req, prkey,
					&ctrl->hostid, abort);
	return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
}

static void nvmet_pr_do_abort(struct work_struct *w)
{
	struct nvmet_req *req = container_of(w, struct nvmet_req, r.abort_work);
	struct nvmet_pr_per_ctrl_ref *pc_ref;
	struct nvmet_ns *ns = req->ns;
	unsigned long idx;

	/*
	 * The target does not support abort, just wait per-controller ref to 0.
	 */
	xa_for_each(&ns->pr_per_ctrl_refs, idx, pc_ref) {
		if (percpu_ref_is_dying(&pc_ref->ref)) {
			wait_for_completion(&pc_ref->free_done);
			reinit_completion(&pc_ref->confirm_done);
			reinit_completion(&pc_ref->free_done);
			percpu_ref_resurrect(&pc_ref->ref);
		}
	}

	up(&ns->pr.pr_sem);
	nvmet_req_complete(req, NVME_SC_SUCCESS);
}

static u16 __nvmet_execute_pr_acquire(struct nvmet_req *req,
				      struct nvmet_pr_registrant *reg,
				      u8 acquire_act,
				      u8 rtype,
				      struct nvmet_pr_acquire_data *d)
{
	u16 status;

	switch (acquire_act) {
	case NVME_PR_ACQUIRE_ACT_ACQUIRE:
		status = nvmet_pr_acquire(req, reg, rtype);
		goto out;
	case NVME_PR_ACQUIRE_ACT_PREEMPT:
		status = nvmet_pr_preempt(req, reg, rtype, d, false);
		goto inc_gen;
	case NVME_PR_ACQUIRE_ACT_PREEMPT_AND_ABORT:
		status = nvmet_pr_preempt(req, reg, rtype, d, true);
		goto inc_gen;
	default:
		req->error_loc = offsetof(struct nvme_common_command, cdw10);
		status = NVME_SC_INVALID_OPCODE | NVME_STATUS_DNR;
		goto out;
	}
inc_gen:
	if (!status)
		atomic_inc(&req->ns->pr.generation);
out:
	return status;
}

static void nvmet_execute_pr_acquire(struct nvmet_req *req)
{
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10);
	bool ignore_key = nvmet_pr_parse_ignore_key(cdw10);
	/* Reservation type, bit 15:08 */
	u8 rtype = (u8)((cdw10 >> 8) & 0xff);
	/* Reservation acquire action, bit 02:00 */
	u8 acquire_act = cdw10 & 0x07;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_pr_acquire_data *d = NULL;
	struct nvmet_pr *pr = &req->ns->pr;
	struct nvmet_pr_registrant *reg;
	u16 status = NVME_SC_SUCCESS;

	if (ignore_key ||
	    rtype < NVME_PR_WRITE_EXCLUSIVE ||
	    rtype > NVME_PR_EXCLUSIVE_ACCESS_ALL_REGS) {
		status = NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
		goto out;
	}

	d = kmalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	status = nvmet_copy_from_sgl(req, 0, d, sizeof(*d));
	if (status)
		goto free_data;

	status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
	down(&pr->pr_sem);
	list_for_each_entry_rcu(reg, &pr->registrant_list, entry) {
		if (uuid_equal(&reg->hostid, &ctrl->hostid) &&
		    reg->rkey == le64_to_cpu(d->crkey)) {
			status = __nvmet_execute_pr_acquire(req, reg,
					acquire_act, rtype, d);
			break;
		}
	}

	if (!status && acquire_act == NVME_PR_ACQUIRE_ACT_PREEMPT_AND_ABORT) {
		kfree(d);
		INIT_WORK(&req->r.abort_work, nvmet_pr_do_abort);
		queue_work(nvmet_wq, &req->r.abort_work);
		return;
	}

	up(&pr->pr_sem);

free_data:
	kfree(d);
out:
	nvmet_req_complete(req, status);
}

static u16 nvmet_pr_release(struct nvmet_req *req,
			    struct nvmet_pr_registrant *reg,
			    u8 rtype)
{
	struct nvmet_pr *pr = &req->ns->pr;
	struct nvmet_pr_registrant *holder;
	u8 original_rtype;

	holder = rcu_dereference_protected(pr->holder, 1);
	if (!holder || reg != holder)
		return NVME_SC_SUCCESS;

	original_rtype = holder->rtype;
	if (original_rtype != rtype)
		return NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;

	rcu_assign_pointer(pr->holder, NULL);

	if (original_rtype != NVME_PR_WRITE_EXCLUSIVE &&
	    original_rtype != NVME_PR_EXCLUSIVE_ACCESS)
		nvmet_pr_resv_released(pr, &reg->hostid);

	return NVME_SC_SUCCESS;
}

static void nvmet_pr_clear(struct nvmet_req *req)
{
	struct nvmet_pr_registrant *reg, *tmp;
	struct nvmet_pr *pr = &req->ns->pr;

	rcu_assign_pointer(pr->holder, NULL);

	list_for_each_entry_safe(reg, tmp, &pr->registrant_list, entry) {
		list_del_rcu(&reg->entry);
		if (!uuid_equal(&req->sq->ctrl->hostid, &reg->hostid))
			nvmet_pr_resv_preempted(pr, &reg->hostid);
		kfree_rcu(reg, rcu);
	}

	atomic_inc(&pr->generation);
}

static u16 __nvmet_execute_pr_release(struct nvmet_req *req,
				      struct nvmet_pr_registrant *reg,
				      u8 release_act, u8 rtype)
{
	switch (release_act) {
	case NVME_PR_RELEASE_ACT_RELEASE:
		return nvmet_pr_release(req, reg, rtype);
	case NVME_PR_RELEASE_ACT_CLEAR:
		nvmet_pr_clear(req);
		return NVME_SC_SUCCESS;
	default:
		req->error_loc = offsetof(struct nvme_common_command, cdw10);
		return NVME_SC_INVALID_OPCODE | NVME_STATUS_DNR;
	}
}

static void nvmet_execute_pr_release(struct nvmet_req *req)
{
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10);
	bool ignore_key = nvmet_pr_parse_ignore_key(cdw10);
	u8 rtype = (u8)((cdw10 >> 8) & 0xff); /* Reservation type, bit 15:08 */
	u8 release_act = cdw10 & 0x07; /* Reservation release action, bit 02:00 */
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_pr *pr = &req->ns->pr;
	struct nvmet_pr_release_data *d;
	struct nvmet_pr_registrant *reg;
	u16 status;

	if (ignore_key) {
		status = NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
		goto out;
	}

	d = kmalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	status = nvmet_copy_from_sgl(req, 0, d, sizeof(*d));
	if (status)
		goto free_data;

	status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
	down(&pr->pr_sem);
	list_for_each_entry_rcu(reg, &pr->registrant_list, entry) {
		if (uuid_equal(&reg->hostid, &ctrl->hostid) &&
		    reg->rkey == le64_to_cpu(d->crkey)) {
			status = __nvmet_execute_pr_release(req, reg,
					release_act, rtype);
			break;
		}
	}
	up(&pr->pr_sem);
free_data:
	kfree(d);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_pr_report(struct nvmet_req *req)
{
	u32 cdw11 = le32_to_cpu(req->cmd->common.cdw11);
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10);
	u32 num_bytes = 4 * (cdw10 + 1); /* cdw10 is number of dwords */
	u8 eds = cdw11 & 1; /* Extended data structure, bit 00 */
	struct nvme_registered_ctrl_ext *ctrl_eds;
	struct nvme_reservation_status_ext *data;
	struct nvmet_pr *pr = &req->ns->pr;
	struct nvmet_pr_registrant *holder;
	struct nvmet_pr_registrant *reg;
	u16 num_ctrls = 0;
	u16 status;
	u8 rtype;

	/* nvmet hostid(uuid_t) is 128 bit. */
	if (!eds) {
		req->error_loc = offsetof(struct nvme_common_command, cdw11);
		status = NVME_SC_HOST_ID_INCONSIST | NVME_STATUS_DNR;
		goto out;
	}

	if (num_bytes < sizeof(struct nvme_reservation_status_ext)) {
		req->error_loc = offsetof(struct nvme_common_command, cdw10);
		status = NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
		goto out;
	}

	data = kmalloc(num_bytes, GFP_KERNEL);
	if (!data) {
		status = NVME_SC_INTERNAL;
		goto out;
	}
	memset(data, 0, num_bytes);
	data->gen = cpu_to_le32(atomic_read(&pr->generation));
	data->ptpls = 0;
	ctrl_eds = data->regctl_eds;

	rcu_read_lock();
	holder = rcu_dereference(pr->holder);
	rtype = holder ? holder->rtype : 0;
	data->rtype = rtype;

	list_for_each_entry_rcu(reg, &pr->registrant_list, entry) {
		num_ctrls++;
		/*
		 * continue to get the number of all registrans.
		 */
		if (((void *)ctrl_eds + sizeof(*ctrl_eds)) >
		    ((void *)data + num_bytes))
			continue;
		/*
		 * Dynamic controller, set cntlid to 0xffff.
		 */
		ctrl_eds->cntlid = cpu_to_le16(NVME_CNTLID_DYNAMIC);
		if (rtype == NVME_PR_WRITE_EXCLUSIVE_ALL_REGS ||
		    rtype == NVME_PR_EXCLUSIVE_ACCESS_ALL_REGS)
			ctrl_eds->rcsts = 1;
		if (reg == holder)
			ctrl_eds->rcsts = 1;
		uuid_copy((uuid_t *)&ctrl_eds->hostid, &reg->hostid);
		ctrl_eds->rkey = cpu_to_le64(reg->rkey);
		ctrl_eds++;
	}
	rcu_read_unlock();

	put_unaligned_le16(num_ctrls, data->regctl);
	status = nvmet_copy_to_sgl(req, 0, data, num_bytes);
	kfree(data);
out:
	nvmet_req_complete(req, status);
}

u16 nvmet_parse_pr_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	switch (cmd->common.opcode) {
	case nvme_cmd_resv_register:
		req->execute = nvmet_execute_pr_register;
		break;
	case nvme_cmd_resv_acquire:
		req->execute = nvmet_execute_pr_acquire;
		break;
	case nvme_cmd_resv_release:
		req->execute = nvmet_execute_pr_release;
		break;
	case nvme_cmd_resv_report:
		req->execute = nvmet_execute_pr_report;
		break;
	default:
		return 1;
	}
	return NVME_SC_SUCCESS;
}

static bool nvmet_is_req_write_cmd_group(struct nvmet_req *req)
{
	u8 opcode = req->cmd->common.opcode;

	if (req->sq->qid) {
		switch (opcode) {
		case nvme_cmd_flush:
		case nvme_cmd_write:
		case nvme_cmd_write_zeroes:
		case nvme_cmd_dsm:
		case nvme_cmd_zone_append:
		case nvme_cmd_zone_mgmt_send:
			return true;
		default:
			return false;
		}
	}
	return false;
}

static bool nvmet_is_req_read_cmd_group(struct nvmet_req *req)
{
	u8 opcode = req->cmd->common.opcode;

	if (req->sq->qid) {
		switch (opcode) {
		case nvme_cmd_read:
		case nvme_cmd_zone_mgmt_recv:
			return true;
		default:
			return false;
		}
	}
	return false;
}

u16 nvmet_pr_check_cmd_access(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_pr_registrant *holder;
	struct nvmet_ns *ns = req->ns;
	struct nvmet_pr *pr = &ns->pr;
	u16 status = NVME_SC_SUCCESS;

	rcu_read_lock();
	holder = rcu_dereference(pr->holder);
	if (!holder)
		goto unlock;
	if (uuid_equal(&ctrl->hostid, &holder->hostid))
		goto unlock;

	/*
	 * The Reservation command group is checked in executing,
	 * allow it here.
	 */
	switch (holder->rtype) {
	case NVME_PR_WRITE_EXCLUSIVE:
		if (nvmet_is_req_write_cmd_group(req))
			status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
		break;
	case NVME_PR_EXCLUSIVE_ACCESS:
		if (nvmet_is_req_read_cmd_group(req) ||
		    nvmet_is_req_write_cmd_group(req))
			status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
		break;
	case NVME_PR_WRITE_EXCLUSIVE_REG_ONLY:
	case NVME_PR_WRITE_EXCLUSIVE_ALL_REGS:
		if ((nvmet_is_req_write_cmd_group(req)) &&
		    !nvmet_pr_find_registrant(pr, &ctrl->hostid))
			status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
		break;
	case NVME_PR_EXCLUSIVE_ACCESS_REG_ONLY:
	case NVME_PR_EXCLUSIVE_ACCESS_ALL_REGS:
		if ((nvmet_is_req_read_cmd_group(req) ||
		    nvmet_is_req_write_cmd_group(req)) &&
		    !nvmet_pr_find_registrant(pr, &ctrl->hostid))
			status = NVME_SC_RESERVATION_CONFLICT | NVME_STATUS_DNR;
		break;
	default:
		pr_warn("the reservation type is set wrong, type:%d\n",
			holder->rtype);
		break;
	}

unlock:
	rcu_read_unlock();
	if (status)
		req->error_loc = offsetof(struct nvme_common_command, opcode);
	return status;
}

u16 nvmet_pr_get_ns_pc_ref(struct nvmet_req *req)
{
	struct nvmet_pr_per_ctrl_ref *pc_ref;

	pc_ref = xa_load(&req->ns->pr_per_ctrl_refs,
			req->sq->ctrl->cntlid);
	if (unlikely(!percpu_ref_tryget_live(&pc_ref->ref)))
		return NVME_SC_INTERNAL;
	req->pc_ref = pc_ref;
	return NVME_SC_SUCCESS;
}

static void nvmet_pr_ctrl_ns_all_cmds_done(struct percpu_ref *ref)
{
	struct nvmet_pr_per_ctrl_ref *pc_ref =
		container_of(ref, struct nvmet_pr_per_ctrl_ref, ref);

	complete(&pc_ref->free_done);
}

static int nvmet_pr_alloc_and_insert_pc_ref(struct nvmet_ns *ns,
					    unsigned long idx,
					    uuid_t *hostid)
{
	struct nvmet_pr_per_ctrl_ref *pc_ref;
	int ret;

	pc_ref = kmalloc(sizeof(*pc_ref), GFP_ATOMIC);
	if (!pc_ref)
		return  -ENOMEM;

	ret = percpu_ref_init(&pc_ref->ref, nvmet_pr_ctrl_ns_all_cmds_done,
			PERCPU_REF_ALLOW_REINIT, GFP_KERNEL);
	if (ret)
		goto free;

	init_completion(&pc_ref->free_done);
	init_completion(&pc_ref->confirm_done);
	uuid_copy(&pc_ref->hostid, hostid);

	ret = xa_insert(&ns->pr_per_ctrl_refs, idx, pc_ref, GFP_KERNEL);
	if (ret)
		goto exit;
	return ret;
exit:
	percpu_ref_exit(&pc_ref->ref);
free:
	kfree(pc_ref);
	return ret;
}

int nvmet_ctrl_init_pr(struct nvmet_ctrl *ctrl)
{
	struct nvmet_subsys *subsys = ctrl->subsys;
	struct nvmet_pr_per_ctrl_ref *pc_ref;
	struct nvmet_ns *ns = NULL;
	unsigned long idx;
	int ret;

	ctrl->pr_log_mgr.counter = 0;
	ctrl->pr_log_mgr.lost_count = 0;
	mutex_init(&ctrl->pr_log_mgr.lock);
	INIT_KFIFO(ctrl->pr_log_mgr.log_queue);

	/*
	 * Here we are under subsys lock, if an ns not in subsys->namespaces,
	 * we can make sure that ns is not enabled, and not call
	 * nvmet_pr_init_ns(), see more details in nvmet_ns_enable().
	 * So just check ns->pr.enable.
	 */
	xa_for_each(&subsys->namespaces, idx, ns) {
		if (ns->pr.enable) {
			ret = nvmet_pr_alloc_and_insert_pc_ref(ns, ctrl->cntlid,
							&ctrl->hostid);
			if (ret)
				goto free_per_ctrl_refs;
		}
	}
	return 0;

free_per_ctrl_refs:
	xa_for_each(&subsys->namespaces, idx, ns) {
		if (ns->pr.enable) {
			pc_ref = xa_erase(&ns->pr_per_ctrl_refs, ctrl->cntlid);
			if (pc_ref)
				percpu_ref_exit(&pc_ref->ref);
			kfree(pc_ref);
		}
	}
	return ret;
}

void nvmet_ctrl_destroy_pr(struct nvmet_ctrl *ctrl)
{
	struct nvmet_pr_per_ctrl_ref *pc_ref;
	struct nvmet_ns *ns;
	unsigned long idx;

	kfifo_free(&ctrl->pr_log_mgr.log_queue);
	mutex_destroy(&ctrl->pr_log_mgr.lock);

	xa_for_each(&ctrl->subsys->namespaces, idx, ns) {
		if (ns->pr.enable) {
			pc_ref = xa_erase(&ns->pr_per_ctrl_refs, ctrl->cntlid);
			if (pc_ref)
				percpu_ref_exit(&pc_ref->ref);
			kfree(pc_ref);
		}
	}
}

int nvmet_pr_init_ns(struct nvmet_ns *ns)
{
	struct nvmet_subsys *subsys = ns->subsys;
	struct nvmet_pr_per_ctrl_ref *pc_ref;
	struct nvmet_ctrl *ctrl = NULL;
	unsigned long idx;
	int ret;

	ns->pr.holder = NULL;
	atomic_set(&ns->pr.generation, 0);
	sema_init(&ns->pr.pr_sem, 1);
	INIT_LIST_HEAD(&ns->pr.registrant_list);
	ns->pr.notify_mask = 0;

	xa_init(&ns->pr_per_ctrl_refs);

	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry) {
		ret = nvmet_pr_alloc_and_insert_pc_ref(ns, ctrl->cntlid,
						&ctrl->hostid);
		if (ret)
			goto free_per_ctrl_refs;
	}
	return 0;

free_per_ctrl_refs:
	xa_for_each(&ns->pr_per_ctrl_refs, idx, pc_ref) {
		xa_erase(&ns->pr_per_ctrl_refs, idx);
		percpu_ref_exit(&pc_ref->ref);
		kfree(pc_ref);
	}
	return ret;
}

void nvmet_pr_exit_ns(struct nvmet_ns *ns)
{
	struct nvmet_pr_registrant *reg, *tmp;
	struct nvmet_pr_per_ctrl_ref *pc_ref;
	struct nvmet_pr *pr = &ns->pr;
	unsigned long idx;

	list_for_each_entry_safe(reg, tmp, &pr->registrant_list, entry) {
		list_del(&reg->entry);
		kfree(reg);
	}

	xa_for_each(&ns->pr_per_ctrl_refs, idx, pc_ref) {
		/*
		 * No command on ns here, we can safely free pc_ref.
		 */
		pc_ref = xa_erase(&ns->pr_per_ctrl_refs, idx);
		percpu_ref_exit(&pc_ref->ref);
		kfree(pc_ref);
	}

	xa_destroy(&ns->pr_per_ctrl_refs);
}
