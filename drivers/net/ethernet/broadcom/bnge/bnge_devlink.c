// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/unaligned.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <net/devlink.h>

#include "bnge.h"
#include "bnge_devlink.h"

static int bnge_dl_info_put(struct bnge_dev *bd, struct devlink_info_req *req,
			    enum bnge_dl_version_type type, const char *key,
			    char *buf)
{
	if (!strlen(buf))
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

static int bnge_devlink_info_get(struct devlink *devlink,
				 struct devlink_info_req *req,
				 struct netlink_ext_ack *extack)
{
	struct bnge_dev *bd = devlink_priv(devlink);
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
