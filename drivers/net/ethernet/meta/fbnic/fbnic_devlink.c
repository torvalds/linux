// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/unaligned.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <net/devlink.h>

#include "fbnic.h"

#define FBNIC_SN_STR_LEN	24

static int fbnic_version_running_put(struct devlink_info_req *req,
				     struct fbnic_fw_ver *fw_ver,
				     char *ver_name)
{
	char running_ver[FBNIC_FW_VER_MAX_SIZE];
	int err;

	fbnic_mk_fw_ver_str(fw_ver->version, running_ver);
	err = devlink_info_version_running_put(req, ver_name, running_ver);
	if (err)
		return err;

	if (strlen(fw_ver->commit) > 0) {
		char commit_name[FBNIC_SN_STR_LEN];

		snprintf(commit_name, FBNIC_SN_STR_LEN, "%s.commit", ver_name);
		err = devlink_info_version_running_put(req, commit_name,
						       fw_ver->commit);
		if (err)
			return err;
	}

	return 0;
}

static int fbnic_version_stored_put(struct devlink_info_req *req,
				    struct fbnic_fw_ver *fw_ver,
				    char *ver_name)
{
	char stored_ver[FBNIC_FW_VER_MAX_SIZE];
	int err;

	fbnic_mk_fw_ver_str(fw_ver->version, stored_ver);
	err = devlink_info_version_stored_put(req, ver_name, stored_ver);
	if (err)
		return err;

	if (strlen(fw_ver->commit) > 0) {
		char commit_name[FBNIC_SN_STR_LEN];

		snprintf(commit_name, FBNIC_SN_STR_LEN, "%s.commit", ver_name);
		err = devlink_info_version_stored_put(req, commit_name,
						      fw_ver->commit);
		if (err)
			return err;
	}

	return 0;
}

static int fbnic_devlink_info_get(struct devlink *devlink,
				  struct devlink_info_req *req,
				  struct netlink_ext_ack *extack)
{
	struct fbnic_dev *fbd = devlink_priv(devlink);
	int err;

	err = fbnic_version_running_put(req, &fbd->fw_cap.running.mgmt,
					DEVLINK_INFO_VERSION_GENERIC_FW);
	if (err)
		return err;

	err = fbnic_version_running_put(req, &fbd->fw_cap.running.bootloader,
					DEVLINK_INFO_VERSION_GENERIC_FW_BOOTLOADER);
	if (err)
		return err;

	err = fbnic_version_stored_put(req, &fbd->fw_cap.stored.mgmt,
				       DEVLINK_INFO_VERSION_GENERIC_FW);
	if (err)
		return err;

	err = fbnic_version_stored_put(req, &fbd->fw_cap.stored.bootloader,
				       DEVLINK_INFO_VERSION_GENERIC_FW_BOOTLOADER);
	if (err)
		return err;

	err = fbnic_version_stored_put(req, &fbd->fw_cap.stored.undi,
				       DEVLINK_INFO_VERSION_GENERIC_FW_UNDI);
	if (err)
		return err;

	if (fbd->dsn) {
		unsigned char serial[FBNIC_SN_STR_LEN];
		u8 dsn[8];

		put_unaligned_be64(fbd->dsn, dsn);
		err = snprintf(serial, FBNIC_SN_STR_LEN, "%8phD", dsn);
		if (err < 0)
			return err;

		err = devlink_info_serial_number_put(req, serial);
		if (err)
			return err;
	}

	return 0;
}

static const struct devlink_ops fbnic_devlink_ops = {
	.info_get = fbnic_devlink_info_get,
};

void fbnic_devlink_free(struct fbnic_dev *fbd)
{
	struct devlink *devlink = priv_to_devlink(fbd);

	devlink_free(devlink);
}

struct fbnic_dev *fbnic_devlink_alloc(struct pci_dev *pdev)
{
	void __iomem * const *iomap_table;
	struct devlink *devlink;
	struct fbnic_dev *fbd;

	devlink = devlink_alloc(&fbnic_devlink_ops, sizeof(struct fbnic_dev),
				&pdev->dev);
	if (!devlink)
		return NULL;

	fbd = devlink_priv(devlink);
	pci_set_drvdata(pdev, fbd);
	fbd->dev = &pdev->dev;

	iomap_table = pcim_iomap_table(pdev);
	fbd->uc_addr0 = iomap_table[0];
	fbd->uc_addr4 = iomap_table[4];

	fbd->dsn = pci_get_dsn(pdev);
	fbd->mps = pcie_get_mps(pdev);
	fbd->readrq = pcie_get_readrq(pdev);

	fbd->mac_addr_boundary = FBNIC_RPC_TCAM_MACDA_DEFAULT_BOUNDARY;

	return fbd;
}

void fbnic_devlink_register(struct fbnic_dev *fbd)
{
	struct devlink *devlink = priv_to_devlink(fbd);

	devlink_register(devlink);
}

void fbnic_devlink_unregister(struct fbnic_dev *fbd)
{
	struct devlink *devlink = priv_to_devlink(fbd);

	devlink_unregister(devlink);
}
