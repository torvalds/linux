// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/platform_device.h>
#include <linux/qcom_scm.h>
#include <linux/string.h>
#include <net/sock.h>

#include "debug.h"
#include "snoc.h"

#define ATH10K_QMI_CLIENT_ID		0x4b4e454c
#define ATH10K_QMI_TIMEOUT		30

static int ath10k_qmi_map_msa_permission(struct ath10k_qmi *qmi,
					 struct ath10k_msa_mem_info *mem_info)
{
	struct qcom_scm_vmperm dst_perms[3];
	struct ath10k *ar = qmi->ar;
	unsigned int src_perms;
	u32 perm_count;
	int ret;

	src_perms = BIT(QCOM_SCM_VMID_HLOS);

	dst_perms[0].vmid = QCOM_SCM_VMID_MSS_MSA;
	dst_perms[0].perm = QCOM_SCM_PERM_RW;
	dst_perms[1].vmid = QCOM_SCM_VMID_WLAN;
	dst_perms[1].perm = QCOM_SCM_PERM_RW;

	if (mem_info->secure) {
		perm_count = 2;
	} else {
		dst_perms[2].vmid = QCOM_SCM_VMID_WLAN_CE;
		dst_perms[2].perm = QCOM_SCM_PERM_RW;
		perm_count = 3;
	}

	ret = qcom_scm_assign_mem(mem_info->addr, mem_info->size,
				  &src_perms, dst_perms, perm_count);
	if (ret < 0)
		ath10k_err(ar, "failed to assign msa map permissions: %d\n", ret);

	return ret;
}

static int ath10k_qmi_unmap_msa_permission(struct ath10k_qmi *qmi,
					   struct ath10k_msa_mem_info *mem_info)
{
	struct qcom_scm_vmperm dst_perms;
	struct ath10k *ar = qmi->ar;
	unsigned int src_perms;
	int ret;

	src_perms = BIT(QCOM_SCM_VMID_MSS_MSA) | BIT(QCOM_SCM_VMID_WLAN);

	if (!mem_info->secure)
		src_perms |= BIT(QCOM_SCM_VMID_WLAN_CE);

	dst_perms.vmid = QCOM_SCM_VMID_HLOS;
	dst_perms.perm = QCOM_SCM_PERM_RW;

	ret = qcom_scm_assign_mem(mem_info->addr, mem_info->size,
				  &src_perms, &dst_perms, 1);
	if (ret < 0)
		ath10k_err(ar, "failed to unmap msa permissions: %d\n", ret);

	return ret;
}

static int ath10k_qmi_setup_msa_permissions(struct ath10k_qmi *qmi)
{
	int ret;
	int i;

	if (qmi->msa_fixed_perm)
		return 0;

	for (i = 0; i < qmi->nr_mem_region; i++) {
		ret = ath10k_qmi_map_msa_permission(qmi, &qmi->mem_region[i]);
		if (ret)
			goto err_unmap;
	}

	return 0;

err_unmap:
	for (i--; i >= 0; i--)
		ath10k_qmi_unmap_msa_permission(qmi, &qmi->mem_region[i]);
	return ret;
}

static void ath10k_qmi_remove_msa_permission(struct ath10k_qmi *qmi)
{
	int i;

	if (qmi->msa_fixed_perm)
		return;

	for (i = 0; i < qmi->nr_mem_region; i++)
		ath10k_qmi_unmap_msa_permission(qmi, &qmi->mem_region[i]);
}

static int ath10k_qmi_msa_mem_info_send_sync_msg(struct ath10k_qmi *qmi)
{
	struct wlfw_msa_info_resp_msg_v01 resp = {};
	struct wlfw_msa_info_req_msg_v01 req = {};
	struct ath10k *ar = qmi->ar;
	phys_addr_t max_mapped_addr;
	struct qmi_txn txn;
	int ret;
	int i;

	req.msa_addr = ar->msa.paddr;
	req.size = ar->msa.mem_size;

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn,
			   wlfw_msa_info_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_MSA_INFO_REQ_V01,
			       WLFW_MSA_INFO_REQ_MSG_V01_MAX_MSG_LEN,
			       wlfw_msa_info_req_msg_v01_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send msa mem info req: %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath10k_err(ar, "msa info req rejected: %d\n", resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

	if (resp.mem_region_info_len > QMI_WLFW_MAX_MEM_REG_V01) {
		ath10k_err(ar, "invalid memory region length received: %d\n",
			   resp.mem_region_info_len);
		ret = -EINVAL;
		goto out;
	}

	max_mapped_addr = ar->msa.paddr + ar->msa.mem_size;
	qmi->nr_mem_region = resp.mem_region_info_len;
	for (i = 0; i < resp.mem_region_info_len; i++) {
		if (resp.mem_region_info[i].size > ar->msa.mem_size ||
		    resp.mem_region_info[i].region_addr > max_mapped_addr ||
		    resp.mem_region_info[i].region_addr < ar->msa.paddr ||
		    resp.mem_region_info[i].size +
		    resp.mem_region_info[i].region_addr > max_mapped_addr) {
			ath10k_err(ar, "received out of range memory region address 0x%llx with size 0x%x, aborting\n",
				   resp.mem_region_info[i].region_addr,
				   resp.mem_region_info[i].size);
			ret = -EINVAL;
			goto fail_unwind;
		}
		qmi->mem_region[i].addr = resp.mem_region_info[i].region_addr;
		qmi->mem_region[i].size = resp.mem_region_info[i].size;
		qmi->mem_region[i].secure = resp.mem_region_info[i].secure_flag;
		ath10k_dbg(ar, ATH10K_DBG_QMI,
			   "qmi msa mem region %d addr 0x%pa size 0x%x flag 0x%08x\n",
			   i, &qmi->mem_region[i].addr,
			   qmi->mem_region[i].size,
			   qmi->mem_region[i].secure);
	}

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi msa mem info request completed\n");
	return 0;

fail_unwind:
	memset(&qmi->mem_region[0], 0, sizeof(qmi->mem_region[0]) * i);
out:
	return ret;
}

static int ath10k_qmi_msa_ready_send_sync_msg(struct ath10k_qmi *qmi)
{
	struct wlfw_msa_ready_resp_msg_v01 resp = {};
	struct wlfw_msa_ready_req_msg_v01 req = {};
	struct ath10k *ar = qmi->ar;
	struct qmi_txn txn;
	int ret;

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn,
			   wlfw_msa_ready_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_MSA_READY_REQ_V01,
			       WLFW_MSA_READY_REQ_MSG_V01_MAX_MSG_LEN,
			       wlfw_msa_ready_req_msg_v01_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send msa mem ready request: %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath10k_err(ar, "msa ready request rejected: %d\n", resp.resp.error);
		ret = -EINVAL;
	}

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi msa mem ready request completed\n");
	return 0;

out:
	return ret;
}

static int ath10k_qmi_bdf_dnld_send_sync(struct ath10k_qmi *qmi)
{
	struct wlfw_bdf_download_resp_msg_v01 resp = {};
	struct wlfw_bdf_download_req_msg_v01 *req;
	struct ath10k *ar = qmi->ar;
	unsigned int remaining;
	struct qmi_txn txn;
	const u8 *temp;
	int ret;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	temp = ar->normal_mode_fw.board_data;
	remaining = ar->normal_mode_fw.board_len;

	while (remaining) {
		req->valid = 1;
		req->file_id_valid = 1;
		req->file_id = 0;
		req->total_size_valid = 1;
		req->total_size = ar->normal_mode_fw.board_len;
		req->seg_id_valid = 1;
		req->data_valid = 1;
		req->end_valid = 1;

		if (remaining > QMI_WLFW_MAX_DATA_SIZE_V01) {
			req->data_len = QMI_WLFW_MAX_DATA_SIZE_V01;
		} else {
			req->data_len = remaining;
			req->end = 1;
		}

		memcpy(req->data, temp, req->data_len);

		ret = qmi_txn_init(&qmi->qmi_hdl, &txn,
				   wlfw_bdf_download_resp_msg_v01_ei,
				   &resp);
		if (ret < 0)
			goto out;

		ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
				       QMI_WLFW_BDF_DOWNLOAD_REQ_V01,
				       WLFW_BDF_DOWNLOAD_REQ_MSG_V01_MAX_MSG_LEN,
				       wlfw_bdf_download_req_msg_v01_ei, req);
		if (ret < 0) {
			qmi_txn_cancel(&txn);
			goto out;
		}

		ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);

		if (ret < 0)
			goto out;

		/* end = 1 triggers a CRC check on the BDF.  If this fails, we
		 * get a QMI_ERR_MALFORMED_MSG_V01 error, but the FW is still
		 * willing to use the BDF.  For some platforms, all the valid
		 * released BDFs fail this CRC check, so attempt to detect this
		 * scenario and treat it as non-fatal.
		 */
		if (resp.resp.result != QMI_RESULT_SUCCESS_V01 &&
		    !(req->end == 1 &&
		      resp.resp.result == QMI_ERR_MALFORMED_MSG_V01)) {
			ath10k_err(ar, "failed to download board data file: %d\n",
				   resp.resp.error);
			ret = -EINVAL;
			goto out;
		}

		remaining -= req->data_len;
		temp += req->data_len;
		req->seg_id++;
	}

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi bdf download request completed\n");

	kfree(req);
	return 0;

out:
	kfree(req);
	return ret;
}

static int ath10k_qmi_send_cal_report_req(struct ath10k_qmi *qmi)
{
	struct wlfw_cal_report_resp_msg_v01 resp = {};
	struct wlfw_cal_report_req_msg_v01 req = {};
	struct ath10k *ar = qmi->ar;
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct qmi_txn txn;
	int i, j = 0;
	int ret;

	if (ar_snoc->xo_cal_supported) {
		req.xo_cal_data_valid = 1;
		req.xo_cal_data = ar_snoc->xo_cal_data;
	}

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn, wlfw_cal_report_resp_msg_v01_ei,
			   &resp);
	if (ret < 0)
		goto out;

	for (i = 0; i < QMI_WLFW_MAX_NUM_CAL_V01; i++) {
		if (qmi->cal_data[i].total_size &&
		    qmi->cal_data[i].data) {
			req.meta_data[j] = qmi->cal_data[i].cal_id;
			j++;
		}
	}
	req.meta_data_len = j;

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_CAL_REPORT_REQ_V01,
			       WLFW_CAL_REPORT_REQ_MSG_V01_MAX_MSG_LEN,
			       wlfw_cal_report_req_msg_v01_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send calibration request: %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath10k_err(ar, "calibration request rejected: %d\n", resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi cal report request completed\n");
	return 0;

out:
	return ret;
}

static int
ath10k_qmi_mode_send_sync_msg(struct ath10k *ar, enum wlfw_driver_mode_enum_v01 mode)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_qmi *qmi = ar_snoc->qmi;
	struct wlfw_wlan_mode_resp_msg_v01 resp = {};
	struct wlfw_wlan_mode_req_msg_v01 req = {};
	struct qmi_txn txn;
	int ret;

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn,
			   wlfw_wlan_mode_resp_msg_v01_ei,
			   &resp);
	if (ret < 0)
		goto out;

	req.mode = mode;
	req.hw_debug_valid = 1;
	req.hw_debug = 0;

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_WLAN_MODE_REQ_V01,
			       WLFW_WLAN_MODE_REQ_MSG_V01_MAX_MSG_LEN,
			       wlfw_wlan_mode_req_msg_v01_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send wlan mode %d request: %d\n", mode, ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath10k_err(ar, "more request rejected: %d\n", resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi wlan mode req completed: %d\n", mode);
	return 0;

out:
	return ret;
}

static int
ath10k_qmi_cfg_send_sync_msg(struct ath10k *ar,
			     struct ath10k_qmi_wlan_enable_cfg *config,
			     const char *version)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_qmi *qmi = ar_snoc->qmi;
	struct wlfw_wlan_cfg_resp_msg_v01 resp = {};
	struct wlfw_wlan_cfg_req_msg_v01 *req;
	struct qmi_txn txn;
	int ret;
	u32 i;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn,
			   wlfw_wlan_cfg_resp_msg_v01_ei,
			   &resp);
	if (ret < 0)
		goto out;

	req->host_version_valid = 0;

	req->tgt_cfg_valid = 1;
	if (config->num_ce_tgt_cfg > QMI_WLFW_MAX_NUM_CE_V01)
		req->tgt_cfg_len = QMI_WLFW_MAX_NUM_CE_V01;
	else
		req->tgt_cfg_len = config->num_ce_tgt_cfg;
	for (i = 0; i < req->tgt_cfg_len; i++) {
		req->tgt_cfg[i].pipe_num = config->ce_tgt_cfg[i].pipe_num;
		req->tgt_cfg[i].pipe_dir = config->ce_tgt_cfg[i].pipe_dir;
		req->tgt_cfg[i].nentries = config->ce_tgt_cfg[i].nentries;
		req->tgt_cfg[i].nbytes_max = config->ce_tgt_cfg[i].nbytes_max;
		req->tgt_cfg[i].flags = config->ce_tgt_cfg[i].flags;
	}

	req->svc_cfg_valid = 1;
	if (config->num_ce_svc_pipe_cfg > QMI_WLFW_MAX_NUM_SVC_V01)
		req->svc_cfg_len = QMI_WLFW_MAX_NUM_SVC_V01;
	else
		req->svc_cfg_len = config->num_ce_svc_pipe_cfg;
	for (i = 0; i < req->svc_cfg_len; i++) {
		req->svc_cfg[i].service_id = config->ce_svc_cfg[i].service_id;
		req->svc_cfg[i].pipe_dir = config->ce_svc_cfg[i].pipe_dir;
		req->svc_cfg[i].pipe_num = config->ce_svc_cfg[i].pipe_num;
	}

	req->shadow_reg_valid = 1;
	if (config->num_shadow_reg_cfg >
	    QMI_WLFW_MAX_NUM_SHADOW_REG_V01)
		req->shadow_reg_len = QMI_WLFW_MAX_NUM_SHADOW_REG_V01;
	else
		req->shadow_reg_len = config->num_shadow_reg_cfg;

	memcpy(req->shadow_reg, config->shadow_reg_cfg,
	       sizeof(struct wlfw_shadow_reg_cfg_s_v01) * req->shadow_reg_len);

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_WLAN_CFG_REQ_V01,
			       WLFW_WLAN_CFG_REQ_MSG_V01_MAX_MSG_LEN,
			       wlfw_wlan_cfg_req_msg_v01_ei, req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send config request: %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath10k_err(ar, "config request rejected: %d\n", resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi config request completed\n");
	kfree(req);
	return 0;

out:
	kfree(req);
	return ret;
}

int ath10k_qmi_wlan_enable(struct ath10k *ar,
			   struct ath10k_qmi_wlan_enable_cfg *config,
			   enum wlfw_driver_mode_enum_v01 mode,
			   const char *version)
{
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi mode %d config %p\n",
		   mode, config);

	ret = ath10k_qmi_cfg_send_sync_msg(ar, config, version);
	if (ret) {
		ath10k_err(ar, "failed to send qmi config: %d\n", ret);
		return ret;
	}

	ret = ath10k_qmi_mode_send_sync_msg(ar, mode);
	if (ret) {
		ath10k_err(ar, "failed to send qmi mode: %d\n", ret);
		return ret;
	}

	return 0;
}

int ath10k_qmi_wlan_disable(struct ath10k *ar)
{
	return ath10k_qmi_mode_send_sync_msg(ar, QMI_WLFW_OFF_V01);
}

static int ath10k_qmi_cap_send_sync_msg(struct ath10k_qmi *qmi)
{
	struct wlfw_cap_resp_msg_v01 *resp;
	struct wlfw_cap_req_msg_v01 req = {};
	struct ath10k *ar = qmi->ar;
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct qmi_txn txn;
	int ret;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn, wlfw_cap_resp_msg_v01_ei, resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_CAP_REQ_V01,
			       WLFW_CAP_REQ_MSG_V01_MAX_MSG_LEN,
			       wlfw_cap_req_msg_v01_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send capability request: %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	if (resp->resp.result != QMI_RESULT_SUCCESS_V01) {
		ath10k_err(ar, "capability req rejected: %d\n", resp->resp.error);
		ret = -EINVAL;
		goto out;
	}

	if (resp->chip_info_valid) {
		qmi->chip_info.chip_id = resp->chip_info.chip_id;
		qmi->chip_info.chip_family = resp->chip_info.chip_family;
	} else {
		qmi->chip_info.chip_id = 0xFF;
	}

	if (resp->board_info_valid)
		qmi->board_info.board_id = resp->board_info.board_id;
	else
		qmi->board_info.board_id = 0xFF;

	if (resp->soc_info_valid)
		qmi->soc_info.soc_id = resp->soc_info.soc_id;

	if (resp->fw_version_info_valid) {
		qmi->fw_version = resp->fw_version_info.fw_version;
		strscpy(qmi->fw_build_timestamp, resp->fw_version_info.fw_build_timestamp,
			sizeof(qmi->fw_build_timestamp));
	}

	if (resp->fw_build_id_valid)
		strscpy(qmi->fw_build_id, resp->fw_build_id,
			MAX_BUILD_ID_LEN + 1);

	if (!test_bit(ATH10K_SNOC_FLAG_REGISTERED, &ar_snoc->flags)) {
		ath10k_info(ar, "qmi chip_id 0x%x chip_family 0x%x board_id 0x%x soc_id 0x%x",
			    qmi->chip_info.chip_id, qmi->chip_info.chip_family,
			    qmi->board_info.board_id, qmi->soc_info.soc_id);
		ath10k_info(ar, "qmi fw_version 0x%x fw_build_timestamp %s fw_build_id %s",
			    qmi->fw_version, qmi->fw_build_timestamp, qmi->fw_build_id);
	}

	kfree(resp);
	return 0;

out:
	kfree(resp);
	return ret;
}

static int ath10k_qmi_host_cap_send_sync(struct ath10k_qmi *qmi)
{
	struct wlfw_host_cap_resp_msg_v01 resp = {};
	struct wlfw_host_cap_req_msg_v01 req = {};
	struct qmi_elem_info *req_ei;
	struct ath10k *ar = qmi->ar;
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct qmi_txn txn;
	int ret;

	req.daemon_support_valid = 1;
	req.daemon_support = 0;

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn, wlfw_host_cap_resp_msg_v01_ei,
			   &resp);
	if (ret < 0)
		goto out;

	if (test_bit(ATH10K_SNOC_FLAG_8BIT_HOST_CAP_QUIRK, &ar_snoc->flags))
		req_ei = wlfw_host_cap_8bit_req_msg_v01_ei;
	else
		req_ei = wlfw_host_cap_req_msg_v01_ei;

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_HOST_CAP_REQ_V01,
			       WLFW_HOST_CAP_REQ_MSG_V01_MAX_MSG_LEN,
			       req_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send host capability request: %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	/* older FW didn't support this request, which is not fatal */
	if (resp.resp.result != QMI_RESULT_SUCCESS_V01 &&
	    resp.resp.error != QMI_ERR_NOT_SUPPORTED_V01) {
		ath10k_err(ar, "host capability request rejected: %d\n", resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi host capability request completed\n");
	return 0;

out:
	return ret;
}

int ath10k_qmi_set_fw_log_mode(struct ath10k *ar, u8 fw_log_mode)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct wlfw_ini_resp_msg_v01 resp = {};
	struct ath10k_qmi *qmi = ar_snoc->qmi;
	struct wlfw_ini_req_msg_v01 req = {};
	struct qmi_txn txn;
	int ret;

	req.enablefwlog_valid = 1;
	req.enablefwlog = fw_log_mode;

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn, wlfw_ini_resp_msg_v01_ei,
			   &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_INI_REQ_V01,
			       WLFW_INI_REQ_MSG_V01_MAX_MSG_LEN,
			       wlfw_ini_req_msg_v01_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send fw log request: %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath10k_err(ar, "fw log request rejected: %d\n",
			   resp.resp.error);
		ret = -EINVAL;
		goto out;
	}
	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi fw log request completed, mode: %d\n",
		   fw_log_mode);
	return 0;

out:
	return ret;
}

static int
ath10k_qmi_ind_register_send_sync_msg(struct ath10k_qmi *qmi)
{
	struct wlfw_ind_register_resp_msg_v01 resp = {};
	struct wlfw_ind_register_req_msg_v01 req = {};
	struct ath10k *ar = qmi->ar;
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct qmi_txn txn;
	int ret;

	req.client_id_valid = 1;
	req.client_id = ATH10K_QMI_CLIENT_ID;
	req.fw_ready_enable_valid = 1;
	req.fw_ready_enable = 1;
	req.msa_ready_enable_valid = 1;
	req.msa_ready_enable = 1;

	if (ar_snoc->xo_cal_supported) {
		req.xo_cal_enable_valid = 1;
		req.xo_cal_enable = 1;
	}

	ret = qmi_txn_init(&qmi->qmi_hdl, &txn,
			   wlfw_ind_register_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&qmi->qmi_hdl, NULL, &txn,
			       QMI_WLFW_IND_REGISTER_REQ_V01,
			       WLFW_IND_REGISTER_REQ_MSG_V01_MAX_MSG_LEN,
			       wlfw_ind_register_req_msg_v01_ei, &req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		ath10k_err(ar, "failed to send indication registered request: %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, ATH10K_QMI_TIMEOUT * HZ);
	if (ret < 0)
		goto out;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath10k_err(ar, "indication request rejected: %d\n", resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

	if (resp.fw_status_valid) {
		if (resp.fw_status & QMI_WLFW_FW_READY_V01)
			qmi->fw_ready = true;
	}
	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi indication register request completed\n");
	return 0;

out:
	return ret;
}

static void ath10k_qmi_event_server_arrive(struct ath10k_qmi *qmi)
{
	struct ath10k *ar = qmi->ar;
	int ret;

	ret = ath10k_qmi_ind_register_send_sync_msg(qmi);
	if (ret)
		return;

	if (qmi->fw_ready) {
		ath10k_snoc_fw_indication(ar, ATH10K_QMI_EVENT_FW_READY_IND);
		return;
	}

	ret = ath10k_qmi_host_cap_send_sync(qmi);
	if (ret)
		return;

	ret = ath10k_qmi_msa_mem_info_send_sync_msg(qmi);
	if (ret)
		return;

	/*
	 * HACK: sleep for a while inbetween receiving the msa info response
	 * and the XPU update to prevent SDM845 from crashing due to a security
	 * violation, when running MPSS.AT.4.0.c2-01184-SDM845_GEN_PACK-1.
	 */
	msleep(20);

	ret = ath10k_qmi_setup_msa_permissions(qmi);
	if (ret)
		return;

	ret = ath10k_qmi_msa_ready_send_sync_msg(qmi);
	if (ret)
		goto err_setup_msa;

	ret = ath10k_qmi_cap_send_sync_msg(qmi);
	if (ret)
		goto err_setup_msa;

	return;

err_setup_msa:
	ath10k_qmi_remove_msa_permission(qmi);
}

static int ath10k_qmi_fetch_board_file(struct ath10k_qmi *qmi)
{
	struct ath10k *ar = qmi->ar;
	int ret;

	ar->hif.bus = ATH10K_BUS_SNOC;
	ar->id.qmi_ids_valid = true;
	ar->id.qmi_board_id = qmi->board_info.board_id;
	ar->id.qmi_chip_id = qmi->chip_info.chip_id;
	ar->hw_params.fw.dir = WCN3990_HW_1_0_FW_DIR;

	ret = ath10k_core_check_dt(ar);
	if (ret)
		ath10k_dbg(ar, ATH10K_DBG_QMI, "DT bdf variant name not set.\n");

	return ath10k_core_fetch_board_file(qmi->ar, ATH10K_BD_IE_BOARD);
}

static int
ath10k_qmi_driver_event_post(struct ath10k_qmi *qmi,
			     enum ath10k_qmi_driver_event_type type,
			     void *data)
{
	struct ath10k_qmi_driver_event *event;

	event = kzalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		return -ENOMEM;

	event->type = type;
	event->data = data;

	spin_lock(&qmi->event_lock);
	list_add_tail(&event->list, &qmi->event_list);
	spin_unlock(&qmi->event_lock);

	queue_work(qmi->event_wq, &qmi->event_work);

	return 0;
}

static void ath10k_qmi_event_server_exit(struct ath10k_qmi *qmi)
{
	struct ath10k *ar = qmi->ar;
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	ath10k_qmi_remove_msa_permission(qmi);
	ath10k_core_free_board_files(ar);
	if (!test_bit(ATH10K_SNOC_FLAG_UNREGISTERING, &ar_snoc->flags) &&
	    !test_bit(ATH10K_SNOC_FLAG_MODEM_STOPPED, &ar_snoc->flags))
		ath10k_snoc_fw_crashed_dump(ar);

	ath10k_snoc_fw_indication(ar, ATH10K_QMI_EVENT_FW_DOWN_IND);
	ath10k_dbg(ar, ATH10K_DBG_QMI, "wifi fw qmi service disconnected\n");
}

static void ath10k_qmi_event_msa_ready(struct ath10k_qmi *qmi)
{
	int ret;

	ret = ath10k_qmi_fetch_board_file(qmi);
	if (ret)
		goto out;

	ret = ath10k_qmi_bdf_dnld_send_sync(qmi);
	if (ret)
		goto out;

	ret = ath10k_qmi_send_cal_report_req(qmi);

out:
	return;
}

static int ath10k_qmi_event_fw_ready_ind(struct ath10k_qmi *qmi)
{
	struct ath10k *ar = qmi->ar;

	ath10k_dbg(ar, ATH10K_DBG_QMI, "wifi fw ready event received\n");
	ath10k_snoc_fw_indication(ar, ATH10K_QMI_EVENT_FW_READY_IND);

	return 0;
}

static void ath10k_qmi_fw_ready_ind(struct qmi_handle *qmi_hdl,
				    struct sockaddr_qrtr *sq,
				    struct qmi_txn *txn, const void *data)
{
	struct ath10k_qmi *qmi = container_of(qmi_hdl, struct ath10k_qmi, qmi_hdl);

	ath10k_qmi_driver_event_post(qmi, ATH10K_QMI_EVENT_FW_READY_IND, NULL);
}

static void ath10k_qmi_msa_ready_ind(struct qmi_handle *qmi_hdl,
				     struct sockaddr_qrtr *sq,
				     struct qmi_txn *txn, const void *data)
{
	struct ath10k_qmi *qmi = container_of(qmi_hdl, struct ath10k_qmi, qmi_hdl);

	ath10k_qmi_driver_event_post(qmi, ATH10K_QMI_EVENT_MSA_READY_IND, NULL);
}

static const struct qmi_msg_handler qmi_msg_handler[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_WLFW_FW_READY_IND_V01,
		.ei = wlfw_fw_ready_ind_msg_v01_ei,
		.decoded_size = sizeof(struct wlfw_fw_ready_ind_msg_v01),
		.fn = ath10k_qmi_fw_ready_ind,
	},
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_WLFW_MSA_READY_IND_V01,
		.ei = wlfw_msa_ready_ind_msg_v01_ei,
		.decoded_size = sizeof(struct wlfw_msa_ready_ind_msg_v01),
		.fn = ath10k_qmi_msa_ready_ind,
	},
	{}
};

static int ath10k_qmi_new_server(struct qmi_handle *qmi_hdl,
				 struct qmi_service *service)
{
	struct ath10k_qmi *qmi = container_of(qmi_hdl, struct ath10k_qmi, qmi_hdl);
	struct sockaddr_qrtr *sq = &qmi->sq;
	struct ath10k *ar = qmi->ar;
	int ret;

	sq->sq_family = AF_QIPCRTR;
	sq->sq_node = service->node;
	sq->sq_port = service->port;

	ath10k_dbg(ar, ATH10K_DBG_QMI, "wifi fw qmi service found\n");

	ret = kernel_connect(qmi_hdl->sock, (struct sockaddr *)&qmi->sq,
			     sizeof(qmi->sq), 0);
	if (ret) {
		ath10k_err(ar, "failed to connect to a remote QMI service port\n");
		return ret;
	}

	ath10k_dbg(ar, ATH10K_DBG_QMI, "qmi wifi fw qmi service connected\n");
	ath10k_qmi_driver_event_post(qmi, ATH10K_QMI_EVENT_SERVER_ARRIVE, NULL);

	return ret;
}

static void ath10k_qmi_del_server(struct qmi_handle *qmi_hdl,
				  struct qmi_service *service)
{
	struct ath10k_qmi *qmi =
		container_of(qmi_hdl, struct ath10k_qmi, qmi_hdl);

	qmi->fw_ready = false;

	/*
	 * The del_server event is to be processed only if coming from
	 * the qmi server. The qmi infrastructure sends del_server, when
	 * any client releases the qmi handle. In this case do not process
	 * this del_server event.
	 */
	if (qmi->state == ATH10K_QMI_STATE_INIT_DONE)
		ath10k_qmi_driver_event_post(qmi, ATH10K_QMI_EVENT_SERVER_EXIT,
					     NULL);
}

static const struct qmi_ops ath10k_qmi_ops = {
	.new_server = ath10k_qmi_new_server,
	.del_server = ath10k_qmi_del_server,
};

static void ath10k_qmi_driver_event_work(struct work_struct *work)
{
	struct ath10k_qmi *qmi = container_of(work, struct ath10k_qmi,
					      event_work);
	struct ath10k_qmi_driver_event *event;
	struct ath10k *ar = qmi->ar;

	spin_lock(&qmi->event_lock);
	while (!list_empty(&qmi->event_list)) {
		event = list_first_entry(&qmi->event_list,
					 struct ath10k_qmi_driver_event, list);
		list_del(&event->list);
		spin_unlock(&qmi->event_lock);

		switch (event->type) {
		case ATH10K_QMI_EVENT_SERVER_ARRIVE:
			ath10k_qmi_event_server_arrive(qmi);
			break;
		case ATH10K_QMI_EVENT_SERVER_EXIT:
			ath10k_qmi_event_server_exit(qmi);
			break;
		case ATH10K_QMI_EVENT_FW_READY_IND:
			ath10k_qmi_event_fw_ready_ind(qmi);
			break;
		case ATH10K_QMI_EVENT_MSA_READY_IND:
			ath10k_qmi_event_msa_ready(qmi);
			break;
		default:
			ath10k_warn(ar, "invalid event type: %d", event->type);
			break;
		}
		kfree(event);
		spin_lock(&qmi->event_lock);
	}
	spin_unlock(&qmi->event_lock);
}

int ath10k_qmi_init(struct ath10k *ar, u32 msa_size)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct device *dev = ar->dev;
	struct ath10k_qmi *qmi;
	int ret;

	qmi = kzalloc(sizeof(*qmi), GFP_KERNEL);
	if (!qmi)
		return -ENOMEM;

	qmi->ar = ar;
	ar_snoc->qmi = qmi;

	if (of_property_read_bool(dev->of_node, "qcom,msa-fixed-perm"))
		qmi->msa_fixed_perm = true;

	ret = qmi_handle_init(&qmi->qmi_hdl,
			      WLFW_BDF_DOWNLOAD_REQ_MSG_V01_MAX_MSG_LEN,
			      &ath10k_qmi_ops, qmi_msg_handler);
	if (ret)
		goto err;

	qmi->event_wq = alloc_workqueue("ath10k_qmi_driver_event",
					WQ_UNBOUND, 1);
	if (!qmi->event_wq) {
		ath10k_err(ar, "failed to allocate workqueue\n");
		ret = -EFAULT;
		goto err_release_qmi_handle;
	}

	INIT_LIST_HEAD(&qmi->event_list);
	spin_lock_init(&qmi->event_lock);
	INIT_WORK(&qmi->event_work, ath10k_qmi_driver_event_work);

	ret = qmi_add_lookup(&qmi->qmi_hdl, WLFW_SERVICE_ID_V01,
			     WLFW_SERVICE_VERS_V01, 0);
	if (ret)
		goto err_qmi_lookup;

	qmi->state = ATH10K_QMI_STATE_INIT_DONE;
	return 0;

err_qmi_lookup:
	destroy_workqueue(qmi->event_wq);

err_release_qmi_handle:
	qmi_handle_release(&qmi->qmi_hdl);

err:
	kfree(qmi);
	return ret;
}

int ath10k_qmi_deinit(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_qmi *qmi = ar_snoc->qmi;

	qmi->state = ATH10K_QMI_STATE_DEINIT;
	qmi_handle_release(&qmi->qmi_hdl);
	cancel_work_sync(&qmi->event_work);
	destroy_workqueue(qmi->event_wq);
	kfree(qmi);
	ar_snoc->qmi = NULL;

	return 0;
}
