// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/unaligned.h>
#include <linux/pci.h>
#include <linux/pldmfw.h>
#include <linux/types.h>
#include <net/devlink.h>

#include "fbnic.h"
#include "fbnic_tlv.h"

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

static bool
fbnic_pldm_match_record(struct pldmfw *context, struct pldmfw_record *record)
{
	struct pldmfw_desc_tlv *desc;
	u32 anti_rollback_ver = 0;
	struct devlink *devlink;
	struct fbnic_dev *fbd;
	struct pci_dev *pdev;

	/* First, use the standard PCI matching function */
	if (!pldmfw_op_pci_match_record(context, record))
		return false;

	pdev = to_pci_dev(context->dev);
	fbd = pci_get_drvdata(pdev);
	devlink = priv_to_devlink(fbd);

	/* If PCI match is successful, check for vendor-specific descriptors */
	list_for_each_entry(desc, &record->descs, entry) {
		if (desc->type != PLDM_DESC_ID_VENDOR_DEFINED)
			continue;

		if (desc->size < 21 || desc->data[0] != 1 ||
		    desc->data[1] != 15)
			continue;

		if (memcmp(desc->data + 2, "AntiRollbackVer", 15) != 0)
			continue;

		anti_rollback_ver = get_unaligned_le32(desc->data + 17);
		break;
	}

	/* Compare versions and return error if they do not match */
	if (anti_rollback_ver < fbd->fw_cap.anti_rollback_version) {
		char buf[128];

		snprintf(buf, sizeof(buf),
			 "New firmware anti-rollback version (0x%x) is older than device version (0x%x)!",
			 anti_rollback_ver, fbd->fw_cap.anti_rollback_version);
		devlink_flash_update_status_notify(devlink, buf,
						   "Anti-Rollback", 0, 0);

		return false;
	}

	return true;
}

static int
fbnic_flash_start(struct fbnic_dev *fbd, struct pldmfw_component *component)
{
	struct fbnic_fw_completion *cmpl;
	int err;

	cmpl = fbnic_fw_alloc_cmpl(FBNIC_TLV_MSG_ID_FW_START_UPGRADE_REQ);
	if (!cmpl)
		return -ENOMEM;

	err = fbnic_fw_xmit_fw_start_upgrade(fbd, cmpl,
					     component->identifier,
					     component->component_size);
	if (err)
		goto cmpl_free;

	/* Wait for firmware to ack firmware upgrade start */
	if (wait_for_completion_timeout(&cmpl->done, 10 * HZ))
		err = cmpl->result;
	else
		err = -ETIMEDOUT;

	fbnic_mbx_clear_cmpl(fbd, cmpl);
cmpl_free:
	fbnic_fw_put_cmpl(cmpl);

	return err;
}

static int
fbnic_flash_component(struct pldmfw *context,
		      struct pldmfw_component *component)
{
	const u8 *data = component->component_data;
	const u32 size = component->component_size;
	struct fbnic_fw_completion *cmpl;
	const char *component_name;
	struct devlink *devlink;
	struct fbnic_dev *fbd;
	struct pci_dev *pdev;
	u32 offset = 0;
	u32 length = 0;
	char buf[32];
	int err;

	pdev = to_pci_dev(context->dev);
	fbd = pci_get_drvdata(pdev);
	devlink = priv_to_devlink(fbd);

	switch (component->identifier) {
	case QSPI_SECTION_CMRT:
		component_name = "boot1";
		break;
	case QSPI_SECTION_CONTROL_FW:
		component_name = "boot2";
		break;
	case QSPI_SECTION_OPTION_ROM:
		component_name = "option-rom";
		break;
	default:
		snprintf(buf, sizeof(buf), "Unknown component ID %u!",
			 component->identifier);
		devlink_flash_update_status_notify(devlink, buf, NULL, 0,
						   size);
		return -EINVAL;
	}

	/* Once firmware receives the request to start upgrading it responds
	 * with two messages:
	 * 1. An ACK that it received the message and possible error code
	 *    indicating that an upgrade is not currently possible.
	 * 2. A request for the first chunk of data
	 *
	 * Setup completions for write before issuing the start message so
	 * the driver can catch both messages.
	 */
	cmpl = fbnic_fw_alloc_cmpl(FBNIC_TLV_MSG_ID_FW_WRITE_CHUNK_REQ);
	if (!cmpl)
		return -ENOMEM;

	err = fbnic_mbx_set_cmpl(fbd, cmpl);
	if (err)
		goto cmpl_free;

	devlink_flash_update_timeout_notify(devlink, "Initializing",
					    component_name, 15);
	err = fbnic_flash_start(fbd, component);
	if (err)
		goto err_no_msg;

	while (offset < size) {
		if (!wait_for_completion_timeout(&cmpl->done, 15 * HZ)) {
			err = -ETIMEDOUT;
			break;
		}

		err = cmpl->result;
		if (err)
			break;

		/* Verify firmware is requesting the next chunk in the seq. */
		if (cmpl->u.fw_update.offset != offset + length) {
			err = -EFAULT;
			break;
		}

		offset = cmpl->u.fw_update.offset;
		length = cmpl->u.fw_update.length;

		if (length > TLV_MAX_DATA || offset + length > size) {
			err = -EFAULT;
			break;
		}

		devlink_flash_update_status_notify(devlink, "Flashing",
						   component_name,
						   offset, size);

		/* Mailbox will set length to 0 once it receives the finish
		 * message.
		 */
		if (!length)
			continue;

		reinit_completion(&cmpl->done);
		err = fbnic_fw_xmit_fw_write_chunk(fbd, data, offset, length,
						   0);
		if (err)
			break;
	}

	if (err) {
		fbnic_fw_xmit_fw_write_chunk(fbd, NULL, 0, 0, err);
err_no_msg:
		snprintf(buf, sizeof(buf), "Mailbox encountered error %d!",
			 err);
		devlink_flash_update_status_notify(devlink, buf,
						   component_name, 0, 0);
	}

	fbnic_mbx_clear_cmpl(fbd, cmpl);
cmpl_free:
	fbnic_fw_put_cmpl(cmpl);

	return err;
}

static const struct pldmfw_ops fbnic_pldmfw_ops = {
	.match_record = fbnic_pldm_match_record,
	.flash_component = fbnic_flash_component,
};

static int
fbnic_devlink_flash_update(struct devlink *devlink,
			   struct devlink_flash_update_params *params,
			   struct netlink_ext_ack *extack)
{
	struct fbnic_dev *fbd = devlink_priv(devlink);
	const struct firmware *fw = params->fw;
	struct device *dev = fbd->dev;
	struct pldmfw context;
	char *err_msg;
	int err;

	context.ops = &fbnic_pldmfw_ops;
	context.dev = dev;

	err = pldmfw_flash_image(&context, fw);
	if (err) {
		switch (err) {
		case -EINVAL:
			err_msg = "Invalid image";
			break;
		case -EOPNOTSUPP:
			err_msg = "Unsupported image";
			break;
		case -ENOMEM:
			err_msg = "Out of memory";
			break;
		case -EFAULT:
			err_msg = "Invalid header";
			break;
		case -ENOENT:
			err_msg = "No matching record";
			break;
		case -ENODEV:
			err_msg = "No matching device";
			break;
		case -ETIMEDOUT:
			err_msg = "Timed out waiting for reply";
			break;
		default:
			err_msg = "Unknown error";
			break;
		}

		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Failed to flash PLDM Image: %s (error: %d)",
				       err_msg, err);
	}

	return err;
}

static const struct devlink_ops fbnic_devlink_ops = {
	.info_get	= fbnic_devlink_info_get,
	.flash_update	= fbnic_devlink_flash_update,
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
