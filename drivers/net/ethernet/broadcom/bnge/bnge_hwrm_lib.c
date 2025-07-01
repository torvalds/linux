// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>

#include "bnge.h"
#include "../bnxt/bnxt_hsi.h"
#include "bnge_hwrm.h"
#include "bnge_hwrm_lib.h"

int bnge_hwrm_ver_get(struct bnge_dev *bd)
{
	u32 dev_caps_cfg, hwrm_ver, hwrm_spec_code;
	u16 fw_maj, fw_min, fw_bld, fw_rsv;
	struct hwrm_ver_get_output *resp;
	struct hwrm_ver_get_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VER_GET);
	if (rc)
		return rc;

	bnge_hwrm_req_flags(bd, req, BNGE_HWRM_FULL_WAIT);
	bd->hwrm_max_req_len = HWRM_MAX_REQ_LEN;
	req->hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req->hwrm_intf_min = HWRM_VERSION_MINOR;
	req->hwrm_intf_upd = HWRM_VERSION_UPDATE;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (rc)
		goto hwrm_ver_get_exit;

	memcpy(&bd->ver_resp, resp, sizeof(struct hwrm_ver_get_output));

	hwrm_spec_code = resp->hwrm_intf_maj_8b << 16 |
			 resp->hwrm_intf_min_8b << 8 |
			 resp->hwrm_intf_upd_8b;
	hwrm_ver = HWRM_VERSION_MAJOR << 16 | HWRM_VERSION_MINOR << 8 |
			HWRM_VERSION_UPDATE;

	if (hwrm_spec_code > hwrm_ver)
		snprintf(bd->hwrm_ver_supp, FW_VER_STR_LEN, "%d.%d.%d",
			 HWRM_VERSION_MAJOR, HWRM_VERSION_MINOR,
			 HWRM_VERSION_UPDATE);
	else
		snprintf(bd->hwrm_ver_supp, FW_VER_STR_LEN, "%d.%d.%d",
			 resp->hwrm_intf_maj_8b, resp->hwrm_intf_min_8b,
			 resp->hwrm_intf_upd_8b);

	fw_maj = le16_to_cpu(resp->hwrm_fw_major);
	fw_min = le16_to_cpu(resp->hwrm_fw_minor);
	fw_bld = le16_to_cpu(resp->hwrm_fw_build);
	fw_rsv = le16_to_cpu(resp->hwrm_fw_patch);

	bd->fw_ver_code = BNGE_FW_VER_CODE(fw_maj, fw_min, fw_bld, fw_rsv);
	snprintf(bd->fw_ver_str, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 fw_maj, fw_min, fw_bld, fw_rsv);

	if (strlen(resp->active_pkg_name)) {
		int fw_ver_len = strlen(bd->fw_ver_str);

		snprintf(bd->fw_ver_str + fw_ver_len,
			 FW_VER_STR_LEN - fw_ver_len - 1, "/pkg %s",
			 resp->active_pkg_name);
		bd->fw_cap |= BNGE_FW_CAP_PKG_VER;
	}

	bd->hwrm_cmd_timeout = le16_to_cpu(resp->def_req_timeout);
	if (!bd->hwrm_cmd_timeout)
		bd->hwrm_cmd_timeout = BNGE_DFLT_HWRM_CMD_TIMEOUT;
	bd->hwrm_cmd_max_timeout = le16_to_cpu(resp->max_req_timeout) * 1000;
	if (!bd->hwrm_cmd_max_timeout)
		bd->hwrm_cmd_max_timeout = BNGE_HWRM_CMD_MAX_TIMEOUT;
	else if (bd->hwrm_cmd_max_timeout > BNGE_HWRM_CMD_MAX_TIMEOUT)
		dev_warn(bd->dev, "Default HWRM commands max timeout increased to %d seconds\n",
			 bd->hwrm_cmd_max_timeout / 1000);

	bd->hwrm_max_req_len = le16_to_cpu(resp->max_req_win_len);
	bd->hwrm_max_ext_req_len = le16_to_cpu(resp->max_ext_req_len);

	if (bd->hwrm_max_ext_req_len < HWRM_MAX_REQ_LEN)
		bd->hwrm_max_ext_req_len = HWRM_MAX_REQ_LEN;

	bd->chip_num = le16_to_cpu(resp->chip_num);
	bd->chip_rev = resp->chip_rev;

	dev_caps_cfg = le32_to_cpu(resp->dev_caps_cfg);
	if ((dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_SHORT_CMD_SUPPORTED) &&
	    (dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_SHORT_CMD_REQUIRED))
		bd->fw_cap |= BNGE_FW_CAP_SHORT_CMD;

	if (dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_KONG_MB_CHNL_SUPPORTED)
		bd->fw_cap |= BNGE_FW_CAP_KONG_MB_CHNL;

	if (dev_caps_cfg &
	    VER_GET_RESP_DEV_CAPS_CFG_CFA_ADV_FLOW_MGNT_SUPPORTED)
		bd->fw_cap |= BNGE_FW_CAP_CFA_ADV_FLOW;

hwrm_ver_get_exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int
bnge_hwrm_nvm_dev_info(struct bnge_dev *bd,
		       struct hwrm_nvm_get_dev_info_output *nvm_info)
{
	struct hwrm_nvm_get_dev_info_output *resp;
	struct hwrm_nvm_get_dev_info_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_NVM_GET_DEV_INFO);
	if (rc)
		return rc;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc)
		memcpy(nvm_info, resp, sizeof(*resp));
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_func_reset(struct bnge_dev *bd)
{
	struct hwrm_func_reset_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_RESET);
	if (rc)
		return rc;

	req->enables = 0;
	bnge_hwrm_req_timeout(bd, req, BNGE_HWRM_RESET_TIMEOUT);
	return bnge_hwrm_req_send(bd, req);
}

int bnge_hwrm_fw_set_time(struct bnge_dev *bd)
{
	struct hwrm_fw_set_time_input *req;
	struct tm tm;
	int rc;

	time64_to_tm(ktime_get_real_seconds(), 0, &tm);

	rc = bnge_hwrm_req_init(bd, req, HWRM_FW_SET_TIME);
	if (rc)
		return rc;

	req->year = cpu_to_le16(1900 + tm.tm_year);
	req->month = 1 + tm.tm_mon;
	req->day = tm.tm_mday;
	req->hour = tm.tm_hour;
	req->minute = tm.tm_min;
	req->second = tm.tm_sec;
	return bnge_hwrm_req_send(bd, req);
}

int bnge_hwrm_func_drv_rgtr(struct bnge_dev *bd)
{
	struct hwrm_func_drv_rgtr_output *resp;
	struct hwrm_func_drv_rgtr_input *req;
	u32 flags;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_DRV_RGTR);
	if (rc)
		return rc;

	req->enables = cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_OS_TYPE |
				   FUNC_DRV_RGTR_REQ_ENABLES_VER |
				   FUNC_DRV_RGTR_REQ_ENABLES_ASYNC_EVENT_FWD);

	req->os_type = cpu_to_le16(FUNC_DRV_RGTR_REQ_OS_TYPE_LINUX);
	flags = FUNC_DRV_RGTR_REQ_FLAGS_16BIT_VER_MODE;

	req->flags = cpu_to_le32(flags);
	req->ver_maj_8b = DRV_VER_MAJ;
	req->ver_min_8b = DRV_VER_MIN;
	req->ver_upd_8b = DRV_VER_UPD;
	req->ver_maj = cpu_to_le16(DRV_VER_MAJ);
	req->ver_min = cpu_to_le16(DRV_VER_MIN);
	req->ver_upd = cpu_to_le16(DRV_VER_UPD);

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc) {
		set_bit(BNGE_STATE_DRV_REGISTERED, &bd->state);
		if (resp->flags &
		    cpu_to_le32(FUNC_DRV_RGTR_RESP_FLAGS_IF_CHANGE_SUPPORTED))
			bd->fw_cap |= BNGE_FW_CAP_IF_CHANGE;
	}
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_func_drv_unrgtr(struct bnge_dev *bd)
{
	struct hwrm_func_drv_unrgtr_input *req;
	int rc;

	if (!test_and_clear_bit(BNGE_STATE_DRV_REGISTERED, &bd->state))
		return 0;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_DRV_UNRGTR);
	if (rc)
		return rc;
	return bnge_hwrm_req_send(bd, req);
}
