// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe over Fabrics DH-HMAC-CHAP authentication command handling.
 * Copyright (c) 2020 Hannes Reinecke, SUSE Software Solutions.
 * All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/blkdev.h>
#include <linux/random.h>
#include <linux/nvme-auth.h>
#include <crypto/hash.h>
#include <crypto/kpp.h>
#include "nvmet.h"

static void nvmet_auth_expired_work(struct work_struct *work)
{
	struct nvmet_sq *sq = container_of(to_delayed_work(work),
			struct nvmet_sq, auth_expired_work);

	pr_debug("%s: ctrl %d qid %d transaction %u expired, resetting\n",
		 __func__, sq->ctrl->cntlid, sq->qid, sq->dhchap_tid);
	sq->dhchap_step = NVME_AUTH_DHCHAP_MESSAGE_NEGOTIATE;
	sq->dhchap_tid = -1;
}

void nvmet_auth_sq_init(struct nvmet_sq *sq)
{
	/* Initialize in-band authentication */
	INIT_DELAYED_WORK(&sq->auth_expired_work, nvmet_auth_expired_work);
	sq->authenticated = false;
	sq->dhchap_step = NVME_AUTH_DHCHAP_MESSAGE_NEGOTIATE;
}

static u16 nvmet_auth_negotiate(struct nvmet_req *req, void *d)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmf_auth_dhchap_negotiate_data *data = d;
	int i, hash_id = 0, fallback_hash_id = 0, dhgid, fallback_dhgid;

	pr_debug("%s: ctrl %d qid %d: data sc_d %d napd %d authid %d halen %d dhlen %d\n",
		 __func__, ctrl->cntlid, req->sq->qid,
		 data->sc_c, data->napd, data->auth_protocol[0].dhchap.authid,
		 data->auth_protocol[0].dhchap.halen,
		 data->auth_protocol[0].dhchap.dhlen);
	req->sq->dhchap_tid = le16_to_cpu(data->t_id);
	if (data->sc_c)
		return NVME_AUTH_DHCHAP_FAILURE_CONCAT_MISMATCH;

	if (data->napd != 1)
		return NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;

	if (data->auth_protocol[0].dhchap.authid !=
	    NVME_AUTH_DHCHAP_AUTH_ID)
		return NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;

	for (i = 0; i < data->auth_protocol[0].dhchap.halen; i++) {
		u8 host_hmac_id = data->auth_protocol[0].dhchap.idlist[i];

		if (!fallback_hash_id &&
		    crypto_has_shash(nvme_auth_hmac_name(host_hmac_id), 0, 0))
			fallback_hash_id = host_hmac_id;
		if (ctrl->shash_id != host_hmac_id)
			continue;
		hash_id = ctrl->shash_id;
		break;
	}
	if (hash_id == 0) {
		if (fallback_hash_id == 0) {
			pr_debug("%s: ctrl %d qid %d: no usable hash found\n",
				 __func__, ctrl->cntlid, req->sq->qid);
			return NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		}
		pr_debug("%s: ctrl %d qid %d: no usable hash found, falling back to %s\n",
			 __func__, ctrl->cntlid, req->sq->qid,
			 nvme_auth_hmac_name(fallback_hash_id));
		ctrl->shash_id = fallback_hash_id;
	}

	dhgid = -1;
	fallback_dhgid = -1;
	for (i = 0; i < data->auth_protocol[0].dhchap.dhlen; i++) {
		int tmp_dhgid = data->auth_protocol[0].dhchap.idlist[i + 30];

		if (tmp_dhgid != ctrl->dh_gid) {
			dhgid = tmp_dhgid;
			break;
		}
		if (fallback_dhgid < 0) {
			const char *kpp = nvme_auth_dhgroup_kpp(tmp_dhgid);

			if (crypto_has_kpp(kpp, 0, 0))
				fallback_dhgid = tmp_dhgid;
		}
	}
	if (dhgid < 0) {
		if (fallback_dhgid < 0) {
			pr_debug("%s: ctrl %d qid %d: no usable DH group found\n",
				 __func__, ctrl->cntlid, req->sq->qid);
			return NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
		}
		pr_debug("%s: ctrl %d qid %d: configured DH group %s not found\n",
			 __func__, ctrl->cntlid, req->sq->qid,
			 nvme_auth_dhgroup_name(fallback_dhgid));
		ctrl->dh_gid = fallback_dhgid;
	}
	pr_debug("%s: ctrl %d qid %d: selected DH group %s (%d)\n",
		 __func__, ctrl->cntlid, req->sq->qid,
		 nvme_auth_dhgroup_name(ctrl->dh_gid), ctrl->dh_gid);
	return 0;
}

static u16 nvmet_auth_reply(struct nvmet_req *req, void *d)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmf_auth_dhchap_reply_data *data = d;
	u16 dhvlen = le16_to_cpu(data->dhvlen);
	u8 *response;

	pr_debug("%s: ctrl %d qid %d: data hl %d cvalid %d dhvlen %u\n",
		 __func__, ctrl->cntlid, req->sq->qid,
		 data->hl, data->cvalid, dhvlen);

	if (dhvlen) {
		if (!ctrl->dh_tfm)
			return NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		if (nvmet_auth_ctrl_sesskey(req, data->rval + 2 * data->hl,
					    dhvlen) < 0)
			return NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
	}

	response = kmalloc(data->hl, GFP_KERNEL);
	if (!response)
		return NVME_AUTH_DHCHAP_FAILURE_FAILED;

	if (!ctrl->host_key) {
		pr_warn("ctrl %d qid %d no host key\n",
			ctrl->cntlid, req->sq->qid);
		kfree(response);
		return NVME_AUTH_DHCHAP_FAILURE_FAILED;
	}
	if (nvmet_auth_host_hash(req, response, data->hl) < 0) {
		pr_debug("ctrl %d qid %d host hash failed\n",
			 ctrl->cntlid, req->sq->qid);
		kfree(response);
		return NVME_AUTH_DHCHAP_FAILURE_FAILED;
	}

	if (memcmp(data->rval, response, data->hl)) {
		pr_info("ctrl %d qid %d host response mismatch\n",
			ctrl->cntlid, req->sq->qid);
		kfree(response);
		return NVME_AUTH_DHCHAP_FAILURE_FAILED;
	}
	kfree(response);
	pr_debug("%s: ctrl %d qid %d host authenticated\n",
		 __func__, ctrl->cntlid, req->sq->qid);
	if (data->cvalid) {
		req->sq->dhchap_c2 = kmemdup(data->rval + data->hl, data->hl,
					     GFP_KERNEL);
		if (!req->sq->dhchap_c2)
			return NVME_AUTH_DHCHAP_FAILURE_FAILED;

		pr_debug("%s: ctrl %d qid %d challenge %*ph\n",
			 __func__, ctrl->cntlid, req->sq->qid, data->hl,
			 req->sq->dhchap_c2);
		req->sq->dhchap_s2 = le32_to_cpu(data->seqnum);
	} else {
		req->sq->authenticated = true;
		req->sq->dhchap_c2 = NULL;
	}

	return 0;
}

static u16 nvmet_auth_failure2(void *d)
{
	struct nvmf_auth_dhchap_failure_data *data = d;

	return data->rescode_exp;
}

void nvmet_execute_auth_send(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmf_auth_dhchap_success2_data *data;
	void *d;
	u32 tl;
	u16 status = 0;

	if (req->cmd->auth_send.secp != NVME_AUTH_DHCHAP_PROTOCOL_IDENTIFIER) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc =
			offsetof(struct nvmf_auth_send_command, secp);
		goto done;
	}
	if (req->cmd->auth_send.spsp0 != 0x01) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc =
			offsetof(struct nvmf_auth_send_command, spsp0);
		goto done;
	}
	if (req->cmd->auth_send.spsp1 != 0x01) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc =
			offsetof(struct nvmf_auth_send_command, spsp1);
		goto done;
	}
	tl = le32_to_cpu(req->cmd->auth_send.tl);
	if (!tl) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc =
			offsetof(struct nvmf_auth_send_command, tl);
		goto done;
	}
	if (!nvmet_check_transfer_len(req, tl)) {
		pr_debug("%s: transfer length mismatch (%u)\n", __func__, tl);
		return;
	}

	d = kmalloc(tl, GFP_KERNEL);
	if (!d) {
		status = NVME_SC_INTERNAL;
		goto done;
	}

	status = nvmet_copy_from_sgl(req, 0, d, tl);
	if (status)
		goto done_kfree;

	data = d;
	pr_debug("%s: ctrl %d qid %d type %d id %d step %x\n", __func__,
		 ctrl->cntlid, req->sq->qid, data->auth_type, data->auth_id,
		 req->sq->dhchap_step);
	if (data->auth_type != NVME_AUTH_COMMON_MESSAGES &&
	    data->auth_type != NVME_AUTH_DHCHAP_MESSAGES)
		goto done_failure1;
	if (data->auth_type == NVME_AUTH_COMMON_MESSAGES) {
		if (data->auth_id == NVME_AUTH_DHCHAP_MESSAGE_NEGOTIATE) {
			/* Restart negotiation */
			pr_debug("%s: ctrl %d qid %d reset negotiation\n", __func__,
				 ctrl->cntlid, req->sq->qid);
			if (!req->sq->qid) {
				if (nvmet_setup_auth(ctrl) < 0) {
					status = NVME_SC_INTERNAL;
					pr_err("ctrl %d qid 0 failed to setup"
					       "re-authentication",
					       ctrl->cntlid);
					goto done_failure1;
				}
			}
			req->sq->dhchap_step = NVME_AUTH_DHCHAP_MESSAGE_NEGOTIATE;
		} else if (data->auth_id != req->sq->dhchap_step)
			goto done_failure1;
		/* Validate negotiation parameters */
		status = nvmet_auth_negotiate(req, d);
		if (status == 0)
			req->sq->dhchap_step =
				NVME_AUTH_DHCHAP_MESSAGE_CHALLENGE;
		else {
			req->sq->dhchap_step =
				NVME_AUTH_DHCHAP_MESSAGE_FAILURE1;
			req->sq->dhchap_status = status;
			status = 0;
		}
		goto done_kfree;
	}
	if (data->auth_id != req->sq->dhchap_step) {
		pr_debug("%s: ctrl %d qid %d step mismatch (%d != %d)\n",
			 __func__, ctrl->cntlid, req->sq->qid,
			 data->auth_id, req->sq->dhchap_step);
		goto done_failure1;
	}
	if (le16_to_cpu(data->t_id) != req->sq->dhchap_tid) {
		pr_debug("%s: ctrl %d qid %d invalid transaction %d (expected %d)\n",
			 __func__, ctrl->cntlid, req->sq->qid,
			 le16_to_cpu(data->t_id),
			 req->sq->dhchap_tid);
		req->sq->dhchap_step =
			NVME_AUTH_DHCHAP_MESSAGE_FAILURE1;
		req->sq->dhchap_status =
			NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		goto done_kfree;
	}

	switch (data->auth_id) {
	case NVME_AUTH_DHCHAP_MESSAGE_REPLY:
		status = nvmet_auth_reply(req, d);
		if (status == 0)
			req->sq->dhchap_step =
				NVME_AUTH_DHCHAP_MESSAGE_SUCCESS1;
		else {
			req->sq->dhchap_step =
				NVME_AUTH_DHCHAP_MESSAGE_FAILURE1;
			req->sq->dhchap_status = status;
			status = 0;
		}
		goto done_kfree;
		break;
	case NVME_AUTH_DHCHAP_MESSAGE_SUCCESS2:
		req->sq->authenticated = true;
		pr_debug("%s: ctrl %d qid %d ctrl authenticated\n",
			 __func__, ctrl->cntlid, req->sq->qid);
		goto done_kfree;
		break;
	case NVME_AUTH_DHCHAP_MESSAGE_FAILURE2:
		status = nvmet_auth_failure2(d);
		if (status) {
			pr_warn("ctrl %d qid %d: authentication failed (%d)\n",
				ctrl->cntlid, req->sq->qid, status);
			req->sq->dhchap_status = status;
			req->sq->authenticated = false;
			status = 0;
		}
		goto done_kfree;
		break;
	default:
		req->sq->dhchap_status =
			NVME_AUTH_DHCHAP_FAILURE_INCORRECT_MESSAGE;
		req->sq->dhchap_step =
			NVME_AUTH_DHCHAP_MESSAGE_FAILURE2;
		req->sq->authenticated = false;
		goto done_kfree;
		break;
	}
done_failure1:
	req->sq->dhchap_status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_MESSAGE;
	req->sq->dhchap_step = NVME_AUTH_DHCHAP_MESSAGE_FAILURE2;

done_kfree:
	kfree(d);
done:
	pr_debug("%s: ctrl %d qid %d dhchap status %x step %x\n", __func__,
		 ctrl->cntlid, req->sq->qid,
		 req->sq->dhchap_status, req->sq->dhchap_step);
	if (status)
		pr_debug("%s: ctrl %d qid %d nvme status %x error loc %d\n",
			 __func__, ctrl->cntlid, req->sq->qid,
			 status, req->error_loc);
	req->cqe->result.u64 = 0;
	if (req->sq->dhchap_step != NVME_AUTH_DHCHAP_MESSAGE_SUCCESS2 &&
	    req->sq->dhchap_step != NVME_AUTH_DHCHAP_MESSAGE_FAILURE2) {
		unsigned long auth_expire_secs = ctrl->kato ? ctrl->kato : 120;

		mod_delayed_work(system_wq, &req->sq->auth_expired_work,
				 auth_expire_secs * HZ);
		goto complete;
	}
	/* Final states, clear up variables */
	nvmet_auth_sq_free(req->sq);
	if (req->sq->dhchap_step == NVME_AUTH_DHCHAP_MESSAGE_FAILURE2)
		nvmet_ctrl_fatal_error(ctrl);

complete:
	nvmet_req_complete(req, status);
}

static int nvmet_auth_challenge(struct nvmet_req *req, void *d, int al)
{
	struct nvmf_auth_dhchap_challenge_data *data = d;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	int ret = 0;
	int hash_len = nvme_auth_hmac_hash_len(ctrl->shash_id);
	int data_size = sizeof(*d) + hash_len;

	if (ctrl->dh_tfm)
		data_size += ctrl->dh_keysize;
	if (al < data_size) {
		pr_debug("%s: buffer too small (al %d need %d)\n", __func__,
			 al, data_size);
		return -EINVAL;
	}
	memset(data, 0, data_size);
	req->sq->dhchap_s1 = nvme_auth_get_seqnum();
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_CHALLENGE;
	data->t_id = cpu_to_le16(req->sq->dhchap_tid);
	data->hashid = ctrl->shash_id;
	data->hl = hash_len;
	data->seqnum = cpu_to_le32(req->sq->dhchap_s1);
	req->sq->dhchap_c1 = kmalloc(data->hl, GFP_KERNEL);
	if (!req->sq->dhchap_c1)
		return -ENOMEM;
	get_random_bytes(req->sq->dhchap_c1, data->hl);
	memcpy(data->cval, req->sq->dhchap_c1, data->hl);
	if (ctrl->dh_tfm) {
		data->dhgid = ctrl->dh_gid;
		data->dhvlen = cpu_to_le16(ctrl->dh_keysize);
		ret = nvmet_auth_ctrl_exponential(req, data->cval + data->hl,
						  ctrl->dh_keysize);
	}
	pr_debug("%s: ctrl %d qid %d seq %d transaction %d hl %d dhvlen %zu\n",
		 __func__, ctrl->cntlid, req->sq->qid, req->sq->dhchap_s1,
		 req->sq->dhchap_tid, data->hl, ctrl->dh_keysize);
	return ret;
}

static int nvmet_auth_success1(struct nvmet_req *req, void *d, int al)
{
	struct nvmf_auth_dhchap_success1_data *data = d;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	int hash_len = nvme_auth_hmac_hash_len(ctrl->shash_id);

	WARN_ON(al < sizeof(*data));
	memset(data, 0, sizeof(*data));
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_SUCCESS1;
	data->t_id = cpu_to_le16(req->sq->dhchap_tid);
	data->hl = hash_len;
	if (req->sq->dhchap_c2) {
		if (!ctrl->ctrl_key) {
			pr_warn("ctrl %d qid %d no ctrl key\n",
				ctrl->cntlid, req->sq->qid);
			return NVME_AUTH_DHCHAP_FAILURE_FAILED;
		}
		if (nvmet_auth_ctrl_hash(req, data->rval, data->hl))
			return NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		data->rvalid = 1;
		pr_debug("ctrl %d qid %d response %*ph\n",
			 ctrl->cntlid, req->sq->qid, data->hl, data->rval);
	}
	return 0;
}

static void nvmet_auth_failure1(struct nvmet_req *req, void *d, int al)
{
	struct nvmf_auth_dhchap_failure_data *data = d;

	WARN_ON(al < sizeof(*data));
	data->auth_type = NVME_AUTH_COMMON_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_FAILURE1;
	data->t_id = cpu_to_le16(req->sq->dhchap_tid);
	data->rescode = NVME_AUTH_DHCHAP_FAILURE_REASON_FAILED;
	data->rescode_exp = req->sq->dhchap_status;
}

void nvmet_execute_auth_receive(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	void *d;
	u32 al;
	u16 status = 0;

	if (req->cmd->auth_receive.secp != NVME_AUTH_DHCHAP_PROTOCOL_IDENTIFIER) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc =
			offsetof(struct nvmf_auth_receive_command, secp);
		goto done;
	}
	if (req->cmd->auth_receive.spsp0 != 0x01) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc =
			offsetof(struct nvmf_auth_receive_command, spsp0);
		goto done;
	}
	if (req->cmd->auth_receive.spsp1 != 0x01) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc =
			offsetof(struct nvmf_auth_receive_command, spsp1);
		goto done;
	}
	al = le32_to_cpu(req->cmd->auth_receive.al);
	if (!al) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		req->error_loc =
			offsetof(struct nvmf_auth_receive_command, al);
		goto done;
	}
	if (!nvmet_check_transfer_len(req, al)) {
		pr_debug("%s: transfer length mismatch (%u)\n", __func__, al);
		return;
	}

	d = kmalloc(al, GFP_KERNEL);
	if (!d) {
		status = NVME_SC_INTERNAL;
		goto done;
	}
	pr_debug("%s: ctrl %d qid %d step %x\n", __func__,
		 ctrl->cntlid, req->sq->qid, req->sq->dhchap_step);
	switch (req->sq->dhchap_step) {
	case NVME_AUTH_DHCHAP_MESSAGE_CHALLENGE:
		if (nvmet_auth_challenge(req, d, al) < 0) {
			pr_warn("ctrl %d qid %d: challenge error (%d)\n",
				ctrl->cntlid, req->sq->qid, status);
			status = NVME_SC_INTERNAL;
			break;
		}
		if (status) {
			req->sq->dhchap_status = status;
			nvmet_auth_failure1(req, d, al);
			pr_warn("ctrl %d qid %d: challenge status (%x)\n",
				ctrl->cntlid, req->sq->qid,
				req->sq->dhchap_status);
			status = 0;
			break;
		}
		req->sq->dhchap_step = NVME_AUTH_DHCHAP_MESSAGE_REPLY;
		break;
	case NVME_AUTH_DHCHAP_MESSAGE_SUCCESS1:
		status = nvmet_auth_success1(req, d, al);
		if (status) {
			req->sq->dhchap_status = status;
			req->sq->authenticated = false;
			nvmet_auth_failure1(req, d, al);
			pr_warn("ctrl %d qid %d: success1 status (%x)\n",
				ctrl->cntlid, req->sq->qid,
				req->sq->dhchap_status);
			break;
		}
		req->sq->dhchap_step = NVME_AUTH_DHCHAP_MESSAGE_SUCCESS2;
		break;
	case NVME_AUTH_DHCHAP_MESSAGE_FAILURE1:
		req->sq->authenticated = false;
		nvmet_auth_failure1(req, d, al);
		pr_warn("ctrl %d qid %d failure1 (%x)\n",
			ctrl->cntlid, req->sq->qid, req->sq->dhchap_status);
		break;
	default:
		pr_warn("ctrl %d qid %d unhandled step (%d)\n",
			ctrl->cntlid, req->sq->qid, req->sq->dhchap_step);
		req->sq->dhchap_step = NVME_AUTH_DHCHAP_MESSAGE_FAILURE1;
		req->sq->dhchap_status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		nvmet_auth_failure1(req, d, al);
		status = 0;
		break;
	}

	status = nvmet_copy_to_sgl(req, 0, d, al);
	kfree(d);
done:
	req->cqe->result.u64 = 0;

	if (req->sq->dhchap_step == NVME_AUTH_DHCHAP_MESSAGE_SUCCESS2)
		nvmet_auth_sq_free(req->sq);
	else if (req->sq->dhchap_step == NVME_AUTH_DHCHAP_MESSAGE_FAILURE1) {
		nvmet_auth_sq_free(req->sq);
		nvmet_ctrl_fatal_error(ctrl);
	}
	nvmet_req_complete(req, status);
}
