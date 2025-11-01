// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/unaligned.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <net/devlink.h>

#include "bnge.h"
#include "bnge_devlink.h"
#include "bnge_hwrm_lib.h"

static int bnge_dl_info_put(struct bnge_dev *bd, struct devlink_info_req *req,
			    enum bnge_dl_version_type type, const char *key,
			    char *buf)
{
	if (!strlen(buf))
		return 0;

	if (!strcmp(key, DEVLINK_INFO_VERSION_GENERIC_FW_NCSI) ||
	    !strcmp(key, DEVLINK_INFO_VERSION_GENERIC_FW_ROCE))
		return 0;

	switch (type) {
	case BNGE_VERSION_FIXED:
		return devlink_info_version_fixed_put(req, key, buf);
	case BNGE_VERSION_RUNNING:
		return devlink_info_version_running_put(req, key, buf);
	case BNGE_VERSION_STORED:
		return devlink_info_version_stored_put(req, key, buf);
	}

	return 0;
}

static void bnge_vpd_read_info(struct bnge_dev *bd)
{
	struct pci_dev *pdev = bd->pdev;
	unsigned int vpd_size, kw_len;
	int pos, size;
	u8 *vpd_data;

	vpd_data = pci_vpd_alloc(pdev, &vpd_size);
	if (IS_ERR(vpd_data)) {
		pci_warn(pdev, "Unable to read VPD\n");
		return;
	}

	pos = pci_vpd_find_ro_info_keyword(vpd_data, vpd_size,
					   PCI_VPD_RO_KEYWORD_PARTNO, &kw_len);
	if (pos < 0)
		goto read_sn;

	size = min_t(int, kw_len, BNGE_VPD_FLD_LEN - 1);
	memcpy(bd->board_partno, &vpd_data[pos], size);

read_sn:
	pos = pci_vpd_find_ro_info_keyword(vpd_data, vpd_size,
					   PCI_VPD_RO_KEYWORD_SERIALNO,
					   &kw_len);
	if (pos < 0)
		goto exit;

	size = min_t(int, kw_len, BNGE_VPD_FLD_LEN - 1);
	memcpy(bd->board_serialno, &vpd_data[pos], size);

exit:
	kfree(vpd_data);
}

#define HWRM_FW_VER_STR_LEN	16

static int bnge_devlink_info_get(struct devlink *devlink,
				 struct devlink_info_req *req,
				 struct netlink_ext_ack *extack)
{
	struct hwrm_nvm_get_dev_info_output nvm_dev_info;
	struct bnge_dev *bd = devlink_priv(devlink);
	struct hwrm_ver_get_output *ver_resp;
	char mgmt_ver[FW_VER_STR_LEN];
	char roce_ver[FW_VER_STR_LEN];
	char ncsi_ver[FW_VER_STR_LEN];
	char buf[32];

	int rc;

	if (bd->dsn) {
		char buf[32];
		u8 dsn[8];
		int rc;

		put_unaligned_le64(bd->dsn, dsn);
		sprintf(buf, "%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
			dsn[7], dsn[6], dsn[5], dsn[4],
			dsn[3], dsn[2], dsn[1], dsn[0]);
		rc = devlink_info_serial_number_put(req, buf);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to set dsn");
			return rc;
		}
	}

	if (strlen(bd->board_serialno)) {
		rc = devlink_info_board_serial_number_put(req,
							  bd->board_serialno);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed to set board serial number");
			return rc;
		}
	}

	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_BOARD_ID,
			      bd->board_partno);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to set board part number");
		return rc;
	}

	/* More information from HWRM ver get command */
	sprintf(buf, "%X", bd->chip_num);
	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_ASIC_ID, buf);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to set asic id");
		return rc;
	}

	ver_resp = &bd->ver_resp;
	sprintf(buf, "%c%d", 'A' + ver_resp->chip_rev, ver_resp->chip_metal);
	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_ASIC_REV, buf);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to set asic info");
		return rc;
	}

	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_PSID,
			      bd->nvm_cfg_ver);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to set firmware version");
		return rc;
	}

	buf[0] = 0;
	strncat(buf, ver_resp->active_pkg_name, HWRM_FW_VER_STR_LEN);
	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW, buf);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to set firmware generic version");
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
	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, mgmt_ver);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to set firmware mgmt version");
		return rc;
	}

	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API,
			      bd->hwrm_ver_supp);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to set firmware mgmt api version");
		return rc;
	}

	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_NCSI, ncsi_ver);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to set ncsi firmware version");
		return rc;
	}

	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_ROCE, roce_ver);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to set roce firmware version");
		return rc;
	}

	rc = bnge_hwrm_nvm_dev_info(bd, &nvm_dev_info);
	if (!(nvm_dev_info.flags & NVM_GET_DEV_INFO_RESP_FLAGS_FW_VER_VALID))
		return 0;

	buf[0] = 0;
	strncat(buf, nvm_dev_info.pkg_name, HWRM_FW_VER_STR_LEN);
	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW, buf);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to set roce firmware version");
		return rc;
	}

	snprintf(mgmt_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.hwrm_fw_major, nvm_dev_info.hwrm_fw_minor,
		 nvm_dev_info.hwrm_fw_build, nvm_dev_info.hwrm_fw_patch);
	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, mgmt_ver);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to set stored firmware version");
		return rc;
	}

	snprintf(ncsi_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.mgmt_fw_major, nvm_dev_info.mgmt_fw_minor,
		 nvm_dev_info.mgmt_fw_build, nvm_dev_info.mgmt_fw_patch);
	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW_NCSI, ncsi_ver);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to set stored ncsi firmware version");
		return rc;
	}

	snprintf(roce_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.roce_fw_major, nvm_dev_info.roce_fw_minor,
		 nvm_dev_info.roce_fw_build, nvm_dev_info.roce_fw_patch);
	rc = bnge_dl_info_put(bd, req, BNGE_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW_ROCE, roce_ver);
	if (rc)
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to set stored roce firmware version");

	return rc;
}

static const struct devlink_ops bnge_devlink_ops = {
	.info_get = bnge_devlink_info_get,
};

void bnge_devlink_free(struct bnge_dev *bd)
{
	struct devlink *devlink = priv_to_devlink(bd);

	devlink_free(devlink);
}

struct bnge_dev *bnge_devlink_alloc(struct pci_dev *pdev)
{
	struct devlink *devlink;
	struct bnge_dev *bd;

	devlink = devlink_alloc(&bnge_devlink_ops, sizeof(*bd), &pdev->dev);
	if (!devlink)
		return NULL;

	bd = devlink_priv(devlink);
	pci_set_drvdata(pdev, bd);
	bd->dev = &pdev->dev;
	bd->pdev = pdev;

	bd->dsn = pci_get_dsn(pdev);
	if (!bd->dsn)
		pci_warn(pdev, "Failed to get DSN\n");

	bnge_vpd_read_info(bd);

	return bd;
}

void bnge_devlink_register(struct bnge_dev *bd)
{
	struct devlink *devlink = priv_to_devlink(bd);
	devlink_register(devlink);
}

void bnge_devlink_unregister(struct bnge_dev *bd)
{
	struct devlink *devlink = priv_to_devlink(bd);
	devlink_unregister(devlink);
}
