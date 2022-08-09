/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <net/devlink.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_vfr.h"
#include "bnxt_devlink.h"
#include "bnxt_ethtool.h"

static int
bnxt_dl_flash_update(struct devlink *dl,
		     struct devlink_flash_update_params *params,
		     struct netlink_ext_ack *extack)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	int rc;

	if (!BNXT_PF(bp)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "flash update not supported from a VF");
		return -EPERM;
	}

	devlink_flash_update_status_notify(dl, "Preparing to flash", NULL, 0, 0);
	rc = bnxt_flash_package_from_fw_obj(bp->dev, params->fw, 0);
	if (!rc)
		devlink_flash_update_status_notify(dl, "Flashing done", NULL, 0, 0);
	else
		devlink_flash_update_status_notify(dl, "Flashing failed", NULL, 0, 0);
	return rc;
}

static int bnxt_fw_reporter_diagnose(struct devlink_health_reporter *reporter,
				     struct devlink_fmsg *fmsg,
				     struct netlink_ext_ack *extack)
{
	struct bnxt *bp = devlink_health_reporter_priv(reporter);
	u32 val;
	int rc;

	if (test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
		return 0;

	val = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);

	if (BNXT_FW_IS_BOOTING(val)) {
		rc = devlink_fmsg_string_pair_put(fmsg, "Description",
						  "Not yet completed initialization");
		if (rc)
			return rc;
	} else if (BNXT_FW_IS_ERR(val)) {
		rc = devlink_fmsg_string_pair_put(fmsg, "Description",
						  "Encountered fatal error and cannot recover");
		if (rc)
			return rc;
	}

	if (val >> 16) {
		rc = devlink_fmsg_u32_pair_put(fmsg, "Error code", val >> 16);
		if (rc)
			return rc;
	}

	val = bnxt_fw_health_readl(bp, BNXT_FW_RESET_CNT_REG);
	rc = devlink_fmsg_u32_pair_put(fmsg, "Reset count", val);
	if (rc)
		return rc;

	return 0;
}

static const struct devlink_health_reporter_ops bnxt_dl_fw_reporter_ops = {
	.name = "fw",
	.diagnose = bnxt_fw_reporter_diagnose,
};

static int bnxt_fw_reset_recover(struct devlink_health_reporter *reporter,
				 void *priv_ctx,
				 struct netlink_ext_ack *extack)
{
	struct bnxt *bp = devlink_health_reporter_priv(reporter);

	if (!priv_ctx)
		return -EOPNOTSUPP;

	bnxt_fw_reset(bp);
	return -EINPROGRESS;
}

static const
struct devlink_health_reporter_ops bnxt_dl_fw_reset_reporter_ops = {
	.name = "fw_reset",
	.recover = bnxt_fw_reset_recover,
};

static int bnxt_fw_fatal_recover(struct devlink_health_reporter *reporter,
				 void *priv_ctx,
				 struct netlink_ext_ack *extack)
{
	struct bnxt *bp = devlink_health_reporter_priv(reporter);
	struct bnxt_fw_reporter_ctx *fw_reporter_ctx = priv_ctx;
	unsigned long event;

	if (!priv_ctx)
		return -EOPNOTSUPP;

	bp->fw_health->fatal = true;
	event = fw_reporter_ctx->sp_event;
	if (event == BNXT_FW_RESET_NOTIFY_SP_EVENT)
		bnxt_fw_reset(bp);
	else if (event == BNXT_FW_EXCEPTION_SP_EVENT)
		bnxt_fw_exception(bp);

	return -EINPROGRESS;
}

static const
struct devlink_health_reporter_ops bnxt_dl_fw_fatal_reporter_ops = {
	.name = "fw_fatal",
	.recover = bnxt_fw_fatal_recover,
};

void bnxt_dl_fw_reporters_create(struct bnxt *bp)
{
	struct bnxt_fw_health *health = bp->fw_health;

	if (!bp->dl || !health)
		return;

	if (!(bp->fw_cap & BNXT_FW_CAP_HOT_RESET) || health->fw_reset_reporter)
		goto err_recovery;

	health->fw_reset_reporter =
		devlink_health_reporter_create(bp->dl,
					       &bnxt_dl_fw_reset_reporter_ops,
					       0, bp);
	if (IS_ERR(health->fw_reset_reporter)) {
		netdev_warn(bp->dev, "Failed to create FW fatal health reporter, rc = %ld\n",
			    PTR_ERR(health->fw_reset_reporter));
		health->fw_reset_reporter = NULL;
		bp->fw_cap &= ~BNXT_FW_CAP_HOT_RESET;
	}

err_recovery:
	if (!(bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY))
		return;

	if (!health->fw_reporter) {
		health->fw_reporter =
			devlink_health_reporter_create(bp->dl,
						       &bnxt_dl_fw_reporter_ops,
						       0, bp);
		if (IS_ERR(health->fw_reporter)) {
			netdev_warn(bp->dev, "Failed to create FW health reporter, rc = %ld\n",
				    PTR_ERR(health->fw_reporter));
			health->fw_reporter = NULL;
			bp->fw_cap &= ~BNXT_FW_CAP_ERROR_RECOVERY;
			return;
		}
	}

	if (health->fw_fatal_reporter)
		return;

	health->fw_fatal_reporter =
		devlink_health_reporter_create(bp->dl,
					       &bnxt_dl_fw_fatal_reporter_ops,
					       0, bp);
	if (IS_ERR(health->fw_fatal_reporter)) {
		netdev_warn(bp->dev, "Failed to create FW fatal health reporter, rc = %ld\n",
			    PTR_ERR(health->fw_fatal_reporter));
		health->fw_fatal_reporter = NULL;
		bp->fw_cap &= ~BNXT_FW_CAP_ERROR_RECOVERY;
	}
}

void bnxt_dl_fw_reporters_destroy(struct bnxt *bp, bool all)
{
	struct bnxt_fw_health *health = bp->fw_health;

	if (!bp->dl || !health)
		return;

	if ((all || !(bp->fw_cap & BNXT_FW_CAP_HOT_RESET)) &&
	    health->fw_reset_reporter) {
		devlink_health_reporter_destroy(health->fw_reset_reporter);
		health->fw_reset_reporter = NULL;
	}

	if ((bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY) && !all)
		return;

	if (health->fw_reporter) {
		devlink_health_reporter_destroy(health->fw_reporter);
		health->fw_reporter = NULL;
	}

	if (health->fw_fatal_reporter) {
		devlink_health_reporter_destroy(health->fw_fatal_reporter);
		health->fw_fatal_reporter = NULL;
	}
}

void bnxt_devlink_health_report(struct bnxt *bp, unsigned long event)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	struct bnxt_fw_reporter_ctx fw_reporter_ctx;

	fw_reporter_ctx.sp_event = event;
	switch (event) {
	case BNXT_FW_RESET_NOTIFY_SP_EVENT:
		if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state)) {
			if (!fw_health->fw_fatal_reporter)
				return;

			devlink_health_report(fw_health->fw_fatal_reporter,
					      "FW fatal async event received",
					      &fw_reporter_ctx);
			return;
		}
		if (!fw_health->fw_reset_reporter)
			return;

		devlink_health_report(fw_health->fw_reset_reporter,
				      "FW non-fatal reset event received",
				      &fw_reporter_ctx);
		return;

	case BNXT_FW_EXCEPTION_SP_EVENT:
		if (!fw_health->fw_fatal_reporter)
			return;

		devlink_health_report(fw_health->fw_fatal_reporter,
				      "FW fatal error reported",
				      &fw_reporter_ctx);
		return;
	}
}

void bnxt_dl_health_status_update(struct bnxt *bp, bool healthy)
{
	struct bnxt_fw_health *health = bp->fw_health;
	u8 state;

	if (healthy)
		state = DEVLINK_HEALTH_REPORTER_STATE_HEALTHY;
	else
		state = DEVLINK_HEALTH_REPORTER_STATE_ERROR;

	if (health->fatal)
		devlink_health_reporter_state_update(health->fw_fatal_reporter,
						     state);
	else
		devlink_health_reporter_state_update(health->fw_reset_reporter,
						     state);

	health->fatal = false;
}

void bnxt_dl_health_recovery_done(struct bnxt *bp)
{
	struct bnxt_fw_health *hlth = bp->fw_health;

	if (hlth->fatal)
		devlink_health_reporter_recovery_done(hlth->fw_fatal_reporter);
	else
		devlink_health_reporter_recovery_done(hlth->fw_reset_reporter);
}

static int bnxt_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
			    struct netlink_ext_ack *extack);

static const struct devlink_ops bnxt_dl_ops = {
#ifdef CONFIG_BNXT_SRIOV
	.eswitch_mode_set = bnxt_dl_eswitch_mode_set,
	.eswitch_mode_get = bnxt_dl_eswitch_mode_get,
#endif /* CONFIG_BNXT_SRIOV */
	.info_get	  = bnxt_dl_info_get,
	.flash_update	  = bnxt_dl_flash_update,
};

static const struct devlink_ops bnxt_vf_dl_ops;

enum bnxt_dl_param_id {
	BNXT_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK,
};

static const struct bnxt_dl_nvm_param nvm_params[] = {
	{DEVLINK_PARAM_GENERIC_ID_ENABLE_SRIOV, NVM_OFF_ENABLE_SRIOV,
	 BNXT_NVM_SHARED_CFG, 1, 1},
	{DEVLINK_PARAM_GENERIC_ID_IGNORE_ARI, NVM_OFF_IGNORE_ARI,
	 BNXT_NVM_SHARED_CFG, 1, 1},
	{DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MAX,
	 NVM_OFF_MSIX_VEC_PER_PF_MAX, BNXT_NVM_SHARED_CFG, 10, 4},
	{DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MIN,
	 NVM_OFF_MSIX_VEC_PER_PF_MIN, BNXT_NVM_SHARED_CFG, 7, 4},
	{BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK, NVM_OFF_DIS_GRE_VER_CHECK,
	 BNXT_NVM_SHARED_CFG, 1, 1},
};

union bnxt_nvm_data {
	u8	val8;
	__le32	val32;
};

static void bnxt_copy_to_nvm_data(union bnxt_nvm_data *dst,
				  union devlink_param_value *src,
				  int nvm_num_bits, int dl_num_bytes)
{
	u32 val32 = 0;

	if (nvm_num_bits == 1) {
		dst->val8 = src->vbool;
		return;
	}
	if (dl_num_bytes == 4)
		val32 = src->vu32;
	else if (dl_num_bytes == 2)
		val32 = (u32)src->vu16;
	else if (dl_num_bytes == 1)
		val32 = (u32)src->vu8;
	dst->val32 = cpu_to_le32(val32);
}

static void bnxt_copy_from_nvm_data(union devlink_param_value *dst,
				    union bnxt_nvm_data *src,
				    int nvm_num_bits, int dl_num_bytes)
{
	u32 val32;

	if (nvm_num_bits == 1) {
		dst->vbool = src->val8;
		return;
	}
	val32 = le32_to_cpu(src->val32);
	if (dl_num_bytes == 4)
		dst->vu32 = val32;
	else if (dl_num_bytes == 2)
		dst->vu16 = (u16)val32;
	else if (dl_num_bytes == 1)
		dst->vu8 = (u8)val32;
}

static int bnxt_hwrm_get_nvm_cfg_ver(struct bnxt *bp, u32 *nvm_cfg_ver)
{
	struct hwrm_nvm_get_variable_input *req;
	u16 bytes = BNXT_NVM_CFG_VER_BYTES;
	u16 bits = BNXT_NVM_CFG_VER_BITS;
	union devlink_param_value ver;
	union bnxt_nvm_data *data;
	dma_addr_t data_dma_addr;
	int rc, i = 2;
	u16 dim = 1;

	rc = hwrm_req_init(bp, req, HWRM_NVM_GET_VARIABLE);
	if (rc)
		return rc;

	data = hwrm_req_dma_slice(bp, req, sizeof(*data), &data_dma_addr);
	if (!data) {
		rc = -ENOMEM;
		goto exit;
	}

	/* earlier devices present as an array of raw bytes */
	if (!BNXT_CHIP_P5(bp)) {
		dim = 0;
		i = 0;
		bits *= 3;  /* array of 3 version components */
		bytes *= 4; /* copy whole word */
	}

	hwrm_req_hold(bp, req);
	req->dest_data_addr = cpu_to_le64(data_dma_addr);
	req->data_len = cpu_to_le16(bits);
	req->option_num = cpu_to_le16(NVM_OFF_NVM_CFG_VER);
	req->dimensions = cpu_to_le16(dim);

	while (i >= 0) {
		req->index_0 = cpu_to_le16(i--);
		rc = hwrm_req_send_silent(bp, req);
		if (rc)
			goto exit;
		bnxt_copy_from_nvm_data(&ver, data, bits, bytes);

		if (BNXT_CHIP_P5(bp)) {
			*nvm_cfg_ver <<= 8;
			*nvm_cfg_ver |= ver.vu8;
		} else {
			*nvm_cfg_ver = ver.vu32;
		}
	}

exit:
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_dl_info_put(struct bnxt *bp, struct devlink_info_req *req,
			    enum bnxt_dl_version_type type, const char *key,
			    char *buf)
{
	if (!strlen(buf))
		return 0;

	if ((bp->flags & BNXT_FLAG_CHIP_P5) &&
	    (!strcmp(key, DEVLINK_INFO_VERSION_GENERIC_FW_NCSI) ||
	     !strcmp(key, DEVLINK_INFO_VERSION_GENERIC_FW_ROCE)))
		return 0;

	switch (type) {
	case BNXT_VERSION_FIXED:
		return devlink_info_version_fixed_put(req, key, buf);
	case BNXT_VERSION_RUNNING:
		return devlink_info_version_running_put(req, key, buf);
	case BNXT_VERSION_STORED:
		return devlink_info_version_stored_put(req, key, buf);
	}
	return 0;
}

#define HWRM_FW_VER_STR_LEN	16

static int bnxt_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
			    struct netlink_ext_ack *extack)
{
	struct hwrm_nvm_get_dev_info_output nvm_dev_info;
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	struct hwrm_ver_get_output *ver_resp;
	char mgmt_ver[FW_VER_STR_LEN];
	char roce_ver[FW_VER_STR_LEN];
	char ncsi_ver[FW_VER_STR_LEN];
	char buf[32];
	u32 ver = 0;
	int rc;

	rc = devlink_info_driver_name_put(req, DRV_MODULE_NAME);
	if (rc)
		return rc;

	if (BNXT_PF(bp) && (bp->flags & BNXT_FLAG_DSN_VALID)) {
		sprintf(buf, "%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
			bp->dsn[7], bp->dsn[6], bp->dsn[5], bp->dsn[4],
			bp->dsn[3], bp->dsn[2], bp->dsn[1], bp->dsn[0]);
		rc = devlink_info_serial_number_put(req, buf);
		if (rc)
			return rc;
	}

	if (strlen(bp->board_serialno)) {
		rc = devlink_info_board_serial_number_put(req, bp->board_serialno);
		if (rc)
			return rc;
	}

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_BOARD_ID,
			      bp->board_partno);
	if (rc)
		return rc;

	sprintf(buf, "%X", bp->chip_num);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_ASIC_ID, buf);
	if (rc)
		return rc;

	ver_resp = &bp->ver_resp;
	sprintf(buf, "%c%d", 'A' + ver_resp->chip_rev, ver_resp->chip_metal);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_ASIC_REV, buf);
	if (rc)
		return rc;

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_PSID,
			      bp->nvm_cfg_ver);
	if (rc)
		return rc;

	buf[0] = 0;
	strncat(buf, ver_resp->active_pkg_name, HWRM_FW_VER_STR_LEN);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW, buf);
	if (rc)
		return rc;

	if (BNXT_PF(bp) && !bnxt_hwrm_get_nvm_cfg_ver(bp, &ver)) {
		sprintf(buf, "%d.%d.%d", (ver >> 16) & 0xff, (ver >> 8) & 0xff,
			ver & 0xff);
		rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
				      DEVLINK_INFO_VERSION_GENERIC_FW_PSID,
				      buf);
		if (rc)
			return rc;
	}

	if (ver_resp->flags & VER_GET_RESP_FLAGS_EXT_VER_AVAIL) {
		snprintf(mgmt_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->hwrm_fw_major, ver_resp->hwrm_fw_minor,
			 ver_resp->hwrm_fw_build, ver_resp->hwrm_fw_patch);

		snprintf(ncsi_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->mgmt_fw_major, ver_resp->mgmt_fw_minor,
			 ver_resp->mgmt_fw_build, ver_resp->mgmt_fw_patch);

		snprintf(roce_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->roce_fw_major, ver_resp->roce_fw_minor,
			 ver_resp->roce_fw_build, ver_resp->roce_fw_patch);
	} else {
		snprintf(mgmt_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->hwrm_fw_maj_8b, ver_resp->hwrm_fw_min_8b,
			 ver_resp->hwrm_fw_bld_8b, ver_resp->hwrm_fw_rsvd_8b);

		snprintf(ncsi_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->mgmt_fw_maj_8b, ver_resp->mgmt_fw_min_8b,
			 ver_resp->mgmt_fw_bld_8b, ver_resp->mgmt_fw_rsvd_8b);

		snprintf(roce_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->roce_fw_maj_8b, ver_resp->roce_fw_min_8b,
			 ver_resp->roce_fw_bld_8b, ver_resp->roce_fw_rsvd_8b);
	}
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, mgmt_ver);
	if (rc)
		return rc;

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API,
			      bp->hwrm_ver_supp);
	if (rc)
		return rc;

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_NCSI, ncsi_ver);
	if (rc)
		return rc;

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_ROCE, roce_ver);
	if (rc)
		return rc;

	rc = bnxt_hwrm_nvm_get_dev_info(bp, &nvm_dev_info);
	if (rc ||
	    !(nvm_dev_info.flags & NVM_GET_DEV_INFO_RESP_FLAGS_FW_VER_VALID))
		return 0;

	buf[0] = 0;
	strncat(buf, nvm_dev_info.pkg_name, HWRM_FW_VER_STR_LEN);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW, buf);
	if (rc)
		return rc;

	snprintf(mgmt_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.hwrm_fw_major, nvm_dev_info.hwrm_fw_minor,
		 nvm_dev_info.hwrm_fw_build, nvm_dev_info.hwrm_fw_patch);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, mgmt_ver);
	if (rc)
		return rc;

	snprintf(ncsi_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.mgmt_fw_major, nvm_dev_info.mgmt_fw_minor,
		 nvm_dev_info.mgmt_fw_build, nvm_dev_info.mgmt_fw_patch);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW_NCSI, ncsi_ver);
	if (rc)
		return rc;

	snprintf(roce_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.roce_fw_major, nvm_dev_info.roce_fw_minor,
		 nvm_dev_info.roce_fw_build, nvm_dev_info.roce_fw_patch);
	return bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
				DEVLINK_INFO_VERSION_GENERIC_FW_ROCE, roce_ver);
}

static int bnxt_hwrm_nvm_req(struct bnxt *bp, u32 param_id, void *msg,
			     union devlink_param_value *val)
{
	struct hwrm_nvm_get_variable_input *req = msg;
	struct bnxt_dl_nvm_param nvm_param;
	struct hwrm_err_output *resp;
	union bnxt_nvm_data *data;
	dma_addr_t data_dma_addr;
	int idx = 0, rc, i;

	/* Get/Set NVM CFG parameter is supported only on PFs */
	if (BNXT_VF(bp)) {
		hwrm_req_drop(bp, req);
		return -EPERM;
	}

	for (i = 0; i < ARRAY_SIZE(nvm_params); i++) {
		if (nvm_params[i].id == param_id) {
			nvm_param = nvm_params[i];
			break;
		}
	}

	if (i == ARRAY_SIZE(nvm_params)) {
		hwrm_req_drop(bp, req);
		return -EOPNOTSUPP;
	}

	if (nvm_param.dir_type == BNXT_NVM_PORT_CFG)
		idx = bp->pf.port_id;
	else if (nvm_param.dir_type == BNXT_NVM_FUNC_CFG)
		idx = bp->pf.fw_fid - BNXT_FIRST_PF_FID;

	data = hwrm_req_dma_slice(bp, req, sizeof(*data), &data_dma_addr);

	if (!data) {
		hwrm_req_drop(bp, req);
		return -ENOMEM;
	}

	req->dest_data_addr = cpu_to_le64(data_dma_addr);
	req->data_len = cpu_to_le16(nvm_param.nvm_num_bits);
	req->option_num = cpu_to_le16(nvm_param.offset);
	req->index_0 = cpu_to_le16(idx);
	if (idx)
		req->dimensions = cpu_to_le16(1);

	resp = hwrm_req_hold(bp, req);
	if (req->req_type == cpu_to_le16(HWRM_NVM_SET_VARIABLE)) {
		bnxt_copy_to_nvm_data(data, val, nvm_param.nvm_num_bits,
				      nvm_param.dl_num_bytes);
		rc = hwrm_req_send(bp, msg);
	} else {
		rc = hwrm_req_send_silent(bp, msg);
		if (!rc) {
			bnxt_copy_from_nvm_data(val, data,
						nvm_param.nvm_num_bits,
						nvm_param.dl_num_bytes);
		} else {
			if (resp->cmd_err ==
				NVM_GET_VARIABLE_CMD_ERR_CODE_VAR_NOT_EXIST)
				rc = -EOPNOTSUPP;
		}
	}
	hwrm_req_drop(bp, req);
	if (rc == -EACCES)
		netdev_err(bp->dev, "PF does not have admin privileges to modify NVM config\n");
	return rc;
}

static int bnxt_dl_nvm_param_get(struct devlink *dl, u32 id,
				 struct devlink_param_gset_ctx *ctx)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	struct hwrm_nvm_get_variable_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_NVM_GET_VARIABLE);
	if (rc)
		return rc;

	rc = bnxt_hwrm_nvm_req(bp, id, req, &ctx->val);
	if (!rc && id == BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK)
		ctx->val.vbool = !ctx->val.vbool;

	return rc;
}

static int bnxt_dl_nvm_param_set(struct devlink *dl, u32 id,
				 struct devlink_param_gset_ctx *ctx)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	struct hwrm_nvm_set_variable_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_NVM_SET_VARIABLE);
	if (rc)
		return rc;

	if (id == BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK)
		ctx->val.vbool = !ctx->val.vbool;

	return bnxt_hwrm_nvm_req(bp, id, req, &ctx->val);
}

static int bnxt_dl_msix_validate(struct devlink *dl, u32 id,
				 union devlink_param_value val,
				 struct netlink_ext_ack *extack)
{
	int max_val = -1;

	if (id == DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MAX)
		max_val = BNXT_MSIX_VEC_MAX;

	if (id == DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MIN)
		max_val = BNXT_MSIX_VEC_MIN_MAX;

	if (val.vu32 > max_val) {
		NL_SET_ERR_MSG_MOD(extack, "MSIX value is exceeding the range");
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_param bnxt_dl_params[] = {
	DEVLINK_PARAM_GENERIC(ENABLE_SRIOV,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      NULL),
	DEVLINK_PARAM_GENERIC(IGNORE_ARI,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      NULL),
	DEVLINK_PARAM_GENERIC(MSIX_VEC_PER_PF_MAX,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      bnxt_dl_msix_validate),
	DEVLINK_PARAM_GENERIC(MSIX_VEC_PER_PF_MIN,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      bnxt_dl_msix_validate),
	DEVLINK_PARAM_DRIVER(BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK,
			     "gre_ver_check", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			     bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			     NULL),
};

static const struct devlink_param bnxt_dl_port_params[] = {
};

static int bnxt_dl_params_register(struct bnxt *bp)
{
	int rc;

	if (bp->hwrm_spec_code < 0x10600)
		return 0;

	rc = devlink_params_register(bp->dl, bnxt_dl_params,
				     ARRAY_SIZE(bnxt_dl_params));
	if (rc) {
		netdev_warn(bp->dev, "devlink_params_register failed. rc=%d\n",
			    rc);
		return rc;
	}
	rc = devlink_port_params_register(&bp->dl_port, bnxt_dl_port_params,
					  ARRAY_SIZE(bnxt_dl_port_params));
	if (rc) {
		netdev_err(bp->dev, "devlink_port_params_register failed\n");
		devlink_params_unregister(bp->dl, bnxt_dl_params,
					  ARRAY_SIZE(bnxt_dl_params));
		return rc;
	}
	devlink_params_publish(bp->dl);

	return 0;
}

static void bnxt_dl_params_unregister(struct bnxt *bp)
{
	if (bp->hwrm_spec_code < 0x10600)
		return;

	devlink_params_unregister(bp->dl, bnxt_dl_params,
				  ARRAY_SIZE(bnxt_dl_params));
	devlink_port_params_unregister(&bp->dl_port, bnxt_dl_port_params,
				       ARRAY_SIZE(bnxt_dl_port_params));
}

int bnxt_dl_register(struct bnxt *bp)
{
	const struct devlink_ops *devlink_ops;
	struct devlink_port_attrs attrs = {};
	struct devlink *dl;
	int rc;

	if (BNXT_PF(bp))
		devlink_ops = &bnxt_dl_ops;
	else
		devlink_ops = &bnxt_vf_dl_ops;

	dl = devlink_alloc(devlink_ops, sizeof(struct bnxt_dl), &bp->pdev->dev);
	if (!dl) {
		netdev_warn(bp->dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}

	bnxt_link_bp_to_dl(bp, dl);

	/* Add switchdev eswitch mode setting, if SRIOV supported */
	if (pci_find_ext_capability(bp->pdev, PCI_EXT_CAP_ID_SRIOV) &&
	    bp->hwrm_spec_code > 0x10803)
		bp->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;

	rc = devlink_register(dl);
	if (rc) {
		netdev_warn(bp->dev, "devlink_register failed. rc=%d\n", rc);
		goto err_dl_free;
	}

	if (!BNXT_PF(bp))
		return 0;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = bp->pf.port_id;
	memcpy(attrs.switch_id.id, bp->dsn, sizeof(bp->dsn));
	attrs.switch_id.id_len = sizeof(bp->dsn);
	devlink_port_attrs_set(&bp->dl_port, &attrs);
	rc = devlink_port_register(dl, &bp->dl_port, bp->pf.port_id);
	if (rc) {
		netdev_err(bp->dev, "devlink_port_register failed\n");
		goto err_dl_unreg;
	}

	rc = bnxt_dl_params_register(bp);
	if (rc)
		goto err_dl_port_unreg;

	return 0;

err_dl_port_unreg:
	devlink_port_unregister(&bp->dl_port);
err_dl_unreg:
	devlink_unregister(dl);
err_dl_free:
	bnxt_link_bp_to_dl(bp, NULL);
	devlink_free(dl);
	return rc;
}

void bnxt_dl_unregister(struct bnxt *bp)
{
	struct devlink *dl = bp->dl;

	if (!dl)
		return;

	if (BNXT_PF(bp)) {
		bnxt_dl_params_unregister(bp);
		devlink_port_unregister(&bp->dl_port);
	}
	devlink_unregister(dl);
	devlink_free(dl);
}
