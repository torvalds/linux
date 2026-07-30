// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pldmfw.h>
#include <linux/vmalloc.h>

#include "core.h"

/* The worst case wait for the install activity is about 25 minutes when
 * installing a new CPLD, which is very seldom.  Normal is about 30-35
 * seconds.  Since the driver can't tell if a CPLD update will happen we
 * set the timeout for the ugly case.
 */
#define PDSC_FW_INSTALL_TIMEOUT	(25 * 60)
#define PDSC_FW_SELECT_TIMEOUT	30

/* Number of periodic log updates during fw file download */
#define PDSC_FW_INTERVAL_FRACTION	32

#define PDSC_FW_COMPONENT_PREFIX		"fw."
#define PDSC_FW_COMPONENT_FULL_NAME_BUFLEN \
	(sizeof(PDSC_FW_COMPONENT_PREFIX) + PDS_CORE_FW_COMPONENT_NAME_BUFLEN)

/* Driver-defined component type to name mapping.
 * PDS_CORE_FW_TYPE_MAIN is NULL - handled specially as "fw" without prefix.
 */
static const char * const pdsc_fw_type_names[] = {
	[PDS_CORE_FW_TYPE_MAIN]      = NULL,
	[PDS_CORE_FW_TYPE_BOOT]      = "bootloader",
	[PDS_CORE_FW_TYPE_CPLD]      = "cpld",
	[PDS_CORE_FW_TYPE_SECURE]    = "secure",
	[PDS_CORE_FW_TYPE_FPGA]      = "fpga",
	[PDS_CORE_FW_TYPE_SUC_MAIN]  = "suc",
	[PDS_CORE_FW_TYPE_SUC_BOOT]  = "suc.bootloader",
	[PDS_CORE_FW_TYPE_UBOOT]     = "uboot",
};

const char *pdsc_fw_type_to_name(u8 type)
{
	if (type < ARRAY_SIZE(pdsc_fw_type_names) && pdsc_fw_type_names[type])
		return pdsc_fw_type_names[type];
	return NULL;
}

void pdsc_fw_components_invalidate(struct pdsc *pdsc)
{
	/* Pairs with READ_ONCE in pdsc_dl_component_info_get() */
	WRITE_ONCE(pdsc->fw_components.num_components, 0);
}

static u8 pdsc_name_to_fw_type(const char *name)
{
	size_t prefix_len;
	int i;

	/* "fw" without suffix maps to main firmware */
	if (!strcmp(name, "fw"))
		return PDS_CORE_FW_TYPE_MAIN;

	prefix_len = str_has_prefix(name, PDSC_FW_COMPONENT_PREFIX);
	if (prefix_len)
		name += prefix_len;

	for (i = 1; i < ARRAY_SIZE(pdsc_fw_type_names); i++) {
		if (pdsc_fw_type_names[i] &&
		    !strcmp(name, pdsc_fw_type_names[i]))
			return i;
	}
	return 0;
}

static int pdsc_devcmd_fw_download_locked(struct pdsc *pdsc, u64 addr,
					  u32 offset, u32 length)
{
	union pds_core_dev_cmd cmd = {
		.fw_download.opcode = PDS_CORE_CMD_FW_DOWNLOAD,
		.fw_download.offset = cpu_to_le32(offset),
		.fw_download.addr = cpu_to_le64(addr),
		.fw_download.length = cpu_to_le32(length),
	};
	union pds_core_dev_comp comp = {};

	return pdsc_devcmd_locked(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
}

static int pdsc_devcmd_fw_install(struct pdsc *pdsc)
{
	union pds_core_dev_cmd cmd = {
		.fw_control.opcode = PDS_CORE_CMD_FW_CONTROL,
		.fw_control.oper = PDS_CORE_FW_INSTALL_ASYNC
	};
	union pds_core_dev_comp comp;
	int err;

	err = pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
	if (err < 0)
		return err;

	return comp.fw_control.slot;
}

static int pdsc_devcmd_fw_activate(struct pdsc *pdsc,
				   enum pds_core_fw_slot slot)
{
	union pds_core_dev_cmd cmd = {
		.fw_control.opcode = PDS_CORE_CMD_FW_CONTROL,
		.fw_control.oper = PDS_CORE_FW_ACTIVATE_ASYNC,
		.fw_control.slot = slot
	};
	union pds_core_dev_comp comp;

	return pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
}

static int pdsc_fw_status_long_wait(struct pdsc *pdsc,
				    const char *label,
				    unsigned long timeout,
				    u8 fw_cmd,
				    struct netlink_ext_ack *extack)
{
	union pds_core_dev_cmd cmd = {
		.fw_control.opcode = PDS_CORE_CMD_FW_CONTROL,
		.fw_control.oper = fw_cmd,
	};
	union pds_core_dev_comp comp;
	unsigned long start_time;
	unsigned long end_time;
	int err;

	/* Ping on the status of the long running async install
	 * command.  We get EAGAIN while the command is still
	 * running, else we get the final command status.
	 */
	start_time = jiffies;
	end_time = start_time + (timeout * HZ);
	do {
		err = pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
		msleep(20);
	} while (time_before(jiffies, end_time) &&
		 (err == -EAGAIN || err == -ETIMEDOUT));

	if (err == -EAGAIN || err == -ETIMEDOUT) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware wait timed out");
		dev_err(pdsc->dev, "DEV_CMD firmware wait %s timed out\n",
			label);
	} else if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware wait failed");
	}

	return err;
}

static int
pdsc_legacy_firmware_update(struct pdsc *pdsc,
			    struct devlink_flash_update_params *params,
			    struct netlink_ext_ack *extack)
{
	const struct firmware *fw = params->fw;
	u32 buf_sz, copy_sz, offset;
	struct devlink *dl;
	int next_interval;
	u64 data_addr;
	int err = 0;
	int fw_slot;

	if (params->component) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Component update not supported by this device");
		return -EOPNOTSUPP;
	}

	dev_info(pdsc->dev, "Installing firmware\n");

	if (!pdsc->cmd_regs)
		return -ENXIO;

	dl = priv_to_devlink(pdsc);
	devlink_flash_update_status_notify(dl, "Preparing to flash",
					   NULL, 0, 0);

	buf_sz = sizeof(pdsc->cmd_regs->data);

	dev_dbg(pdsc->dev,
		"downloading firmware - size %d part_sz %d nparts %lu\n",
		(int)fw->size, buf_sz, DIV_ROUND_UP(fw->size, buf_sz));

	offset = 0;
	next_interval = 0;
	data_addr = offsetof(struct pds_core_dev_cmd_regs, data);
	while (offset < fw->size) {
		if (offset >= next_interval) {
			devlink_flash_update_status_notify(dl, "Downloading",
							   NULL, offset,
							   fw->size);
			next_interval = offset +
					(fw->size / PDSC_FW_INTERVAL_FRACTION);
		}

		copy_sz = min_t(unsigned int, buf_sz, fw->size - offset);
		mutex_lock(&pdsc->devcmd_lock);
		memcpy_toio(&pdsc->cmd_regs->data, fw->data + offset, copy_sz);
		err = pdsc_devcmd_fw_download_locked(pdsc, data_addr,
						     offset, copy_sz);
		mutex_unlock(&pdsc->devcmd_lock);
		if (err) {
			dev_err(pdsc->dev,
				"download failed offset 0x%x addr 0x%llx len 0x%x: %pe\n",
				offset, data_addr, copy_sz, ERR_PTR(err));
			NL_SET_ERR_MSG_MOD(extack, "Segment download failed");
			goto err_out;
		}
		offset += copy_sz;
	}
	devlink_flash_update_status_notify(dl, "Downloading", NULL,
					   fw->size, fw->size);

	devlink_flash_update_timeout_notify(dl, "Installing", NULL,
					    PDSC_FW_INSTALL_TIMEOUT);

	fw_slot = pdsc_devcmd_fw_install(pdsc);
	if (fw_slot < 0) {
		err = fw_slot;
		dev_err(pdsc->dev, "install failed: %pe\n", ERR_PTR(err));
		NL_SET_ERR_MSG_MOD(extack, "Failed to start firmware install");
		goto err_out;
	}

	err = pdsc_fw_status_long_wait(pdsc, "Installing",
				       PDSC_FW_INSTALL_TIMEOUT,
				       PDS_CORE_FW_INSTALL_STATUS,
				       extack);
	if (err)
		goto err_out;

	devlink_flash_update_timeout_notify(dl, "Selecting", NULL,
					    PDSC_FW_SELECT_TIMEOUT);

	err = pdsc_devcmd_fw_activate(pdsc, fw_slot);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to start firmware select");
		goto err_out;
	}

	err = pdsc_fw_status_long_wait(pdsc, "Selecting",
				       PDSC_FW_SELECT_TIMEOUT,
				       PDS_CORE_FW_ACTIVATE_STATUS,
				       extack);
	if (err)
		goto err_out;

	dev_info(pdsc->dev, "Firmware update completed, slot %d\n", fw_slot);

err_out:
	if (err)
		devlink_flash_update_status_notify(dl, "Flash failed",
						   NULL, 0, 0);
	else
		devlink_flash_update_status_notify(dl, "Flash done",
						   NULL, 0, 0);
	return err;
}

struct pdsc_component_priv {
	u16 component_id;
	bool skip;
	struct list_head list_entry;
};

struct pds_core_fwu_priv {
	struct pldmfw context;
	struct devlink_flash_update_params *params;
	struct netlink_ext_ack *extack;
	struct pdsc *pdsc;
	struct list_head components;
	bool component_found;
};

static void pdsc_free_fwu_priv(struct pds_core_fwu_priv *priv)
{
	struct pdsc_component_priv *component_priv, *tmp;

	list_for_each_entry_safe(component_priv, tmp, &priv->components,
				 list_entry) {
		list_del(&component_priv->list_entry);
		kfree(component_priv);
	}
}

static int pdsc_devcmd_match_record_desc(struct pdsc *pdsc, u16 desc_type,
					 u16 desc_size, const u8 *desc_data,
					 u8 *match)
{
	union pds_core_dev_cmd cmd = {
		.match_record_desc.opcode = PDS_CORE_CMD_MATCH_RECORD_DESC,
		.match_record_desc.ver = 1,
		.match_record_desc.type = cpu_to_le16(desc_type),
		.match_record_desc.size = cpu_to_le16(desc_size),
	};
	union pds_core_dev_comp comp = {};
	int err;

	err = pdsc_devcmd_with_data(pdsc, &cmd, desc_data, desc_size,
				    &comp, pdsc->devcmd_timeout);
	*match = comp.match_record_desc.match;

	return err;
}

static bool pdsc_match_record_descs(struct pldmfw *context,
				    struct pldmfw_record *record)
{
	struct pds_core_fwu_priv *priv =
		container_of(context, struct pds_core_fwu_priv, context);
	struct pdsc *pdsc = priv->pdsc;
	struct pldmfw_desc_tlv *desc;

	if (!pldmfw_op_pci_match_record(context, record))
		return false;

	list_for_each_entry(desc, &record->descs, entry) {
		u8 match;
		int err;

		switch (desc->type) {
		/* skip types checked in pldmfw_op_pci_match_record */
		case PLDM_DESC_ID_PCI_VENDOR_ID:
		case PLDM_DESC_ID_PCI_DEVICE_ID:
		case PLDM_DESC_ID_PCI_SUBVENDOR_ID:
		case PLDM_DESC_ID_PCI_SUBDEV_ID:
			continue;
		}

		if (!desc->size)
			return false;

		err = pdsc_devcmd_match_record_desc(pdsc, desc->type,
						    desc->size, desc->data,
						    &match);
		if (err) {
			dev_err(pdsc->dev,
				"match_record_desc failed type: 0x%04x size: %u, err %d\n",
				desc->type, desc->size, err);
			return false;
		}
		/* all record descriptors must match */
		if (!match)
			return false;
	}

	return true;
}

static int pdsc_devcmd_send_package_data(struct pdsc *pdsc, u64 addr,
					 u16 length, u16 offset, u16 total_len)
{
	union pds_core_dev_cmd cmd = {
		.send_pkg_data.opcode = PDS_CORE_CMD_SEND_PKG_DATA,
		.send_pkg_data.ver = 1,
		.send_pkg_data.data_pa = cpu_to_le64(addr),
		.send_pkg_data.data_len = cpu_to_le16(length),
		.send_pkg_data.offset = cpu_to_le16(offset),
		.send_pkg_data.total_len = cpu_to_le16(total_len),
	};
	union pds_core_dev_comp comp = {};

	return pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
}

static int pdsc_send_package_data(struct pldmfw *context, const u8 *data,
				  u16 length)
{
	struct pds_core_fwu_priv *priv =
		container_of(context, struct pds_core_fwu_priv, context);
	struct pdsc_deferred_dma *deferred;
	struct device *dev = context->dev;
	struct pdsc *pdsc = priv->pdsc;
	dma_addr_t dma_addr;
	u8 *package_data;
	u32 offset;
	int err;

	if (!length)
		return 0;

	deferred = kmalloc_obj(*deferred, GFP_KERNEL);
	if (!deferred)
		return -ENOMEM;

	package_data = kmemdup(data, length, GFP_KERNEL);
	if (!package_data) {
		kfree(deferred);
		return -ENOMEM;
	}

	dma_addr = dma_map_single(dev, package_data, length, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr)) {
		dev_err(dev, "Failed to dma_map package_data length 0x%x\n",
			length);
		kfree(package_data);
		kfree(deferred);
		return -ENOMEM;
	}

	for (offset = 0; offset < length; offset += PDS_PAGE_SIZE) {
		u32 copy_sz;

		copy_sz = min_t(unsigned int, PDS_PAGE_SIZE, length - offset);
		err = pdsc_devcmd_send_package_data(pdsc, dma_addr + offset,
						    copy_sz, offset, length);
		if (err) {
			NL_SET_ERR_MSG_MOD(priv->extack,
					   "Failed to send package data");
			break;
		}
	}

	if (err == -ETIMEDOUT || err == -EAGAIN) {
		pdsc_deferred_dma_add(pdsc, deferred, dma_addr,
				      package_data, length, DMA_TO_DEVICE);
		return err;
	}

	kfree(deferred);
	dma_unmap_single(dev, dma_addr, length, DMA_TO_DEVICE);
	kfree(package_data);
	return err;
}

static bool pdsc_component_type_exists(struct pdsc *pdsc, u8 type)
{
	int i;

	for (i = 0; i < pdsc->fw_components.num_components; i++) {
		if (pdsc->fw_components.info[i].component_type == type)
			return true;
	}
	return false;
}

static bool pdsc_component_id_matches_type(struct pdsc *pdsc,
					   u8 component_id, u8 type)
{
	int i;

	for (i = 0; i < pdsc->fw_components.num_components; i++) {
		struct pds_core_fw_component_info *info =
			&pdsc->fw_components.info[i];

		if (info->identifier == component_id &&
		    info->component_type == type)
			return true;
	}
	return false;
}

static u8 pdsc_get_component_type_by_id(struct pdsc *pdsc, u16 component_id)
{
	int i;

	for (i = 0; i < pdsc->fw_components.num_components; i++) {
		struct pds_core_fw_component_info *info =
			&pdsc->fw_components.info[i];

		if (info->identifier == component_id)
			return info->component_type;
	}
	return 0;
}

static bool pdsc_skip_component(struct pds_core_fwu_priv *priv,
				u16 component_id)
{
	struct pdsc_component_priv *component_priv;

	list_for_each_entry(component_priv, &priv->components, list_entry) {
		if (component_priv->component_id == component_id)
			return component_priv->skip;
	}

	return false;
}

static int pdsc_send_component_table(struct pldmfw *context,
				     struct pldmfw_component *component,
				     u8 transfer_flag)
{
	struct pds_core_fwu_priv *priv =
		container_of(context, struct pds_core_fwu_priv, context);
	struct pds_core_component_tbl *component_tbl;
	struct pdsc_component_priv *component_priv;
	struct device *dev = context->dev;
	union pds_core_dev_comp comp = {};
	union pds_core_dev_cmd cmd = {};
	struct pdsc *pdsc = priv->pdsc;
	bool skip_component = false;
	u8 requested_type = 0;
	u16 buf_sz, tbl_sz;
	int err = 0;

	dev_dbg(dev,
		"component name %s classification %u id %u activation_method %u ver_len %d ver_str %.*s index %u size %u transfer_flag 0x%02x\n",
		priv->params->component, component->classification,
		component->identifier, component->activation_method,
		component->version_len, component->version_len,
		component->version_string, component->index,
		component->component_size, transfer_flag);

	component_priv = kzalloc_obj(*component_priv, GFP_KERNEL);
	if (!component_priv)
		return -ENOMEM;

	if (priv->params->component) {
		requested_type = pdsc_name_to_fw_type(priv->params->component);
		if (component->identifier > U8_MAX ||
		    !pdsc_component_id_matches_type(pdsc,
						    component->identifier,
						    requested_type)) {
			skip_component = true;
			goto add_component_priv;
		}
		priv->component_found = true;
	}

	buf_sz = sizeof(pdsc->cmd_regs->data);
	tbl_sz = struct_size(component_tbl, version_str,
			     component->version_len);
	if (tbl_sz > buf_sz) {
		dev_err(dev, "component_tbl size %d too big, max size: %d\n",
			tbl_sz, buf_sz);
		err = -ENOSPC;
		goto free_component_priv;
	}
	component_tbl = kzalloc(tbl_sz, GFP_KERNEL);
	if (!component_tbl) {
		err = -ENOMEM;
		goto free_component_priv;
	}

	component_tbl->comparison_stamp =
		cpu_to_le32(component->comparison_stamp);
	component_tbl->classification = cpu_to_le16(component->classification);
	component_tbl->identifier = cpu_to_le16(component->identifier);
	component_tbl->transfer_flag = transfer_flag;
	component_tbl->version_str_type = component->version_type;
	component_tbl->version_str_len = component->version_len;
	memcpy(component_tbl->version_str, component->version_string,
	       component->version_len);

	cmd.send_component_tbl.opcode = PDS_CORE_CMD_SEND_COMPONENT_TBL;
	cmd.send_component_tbl.ver = 1;
	cmd.send_component_tbl.slot_id = PDS_CORE_FW_SLOT_INVALID;

	err = pdsc_devcmd_with_data(pdsc, &cmd, component_tbl, tbl_sz,
				    &comp, pdsc->devcmd_timeout);
	kfree(component_tbl);
	if (err) {
		dev_err(dev, "Failed sending component table: %pe\n",
			ERR_PTR(err));
		goto free_component_priv;
	}

	skip_component = comp.send_component_tbl.response == 1;

add_component_priv:
	component_priv->skip = skip_component;
	component_priv->component_id = component->identifier;
	list_add(&component_priv->list_entry, &priv->components);

	return 0;

free_component_priv:
	kfree(component_priv);
	return err;
}

int pdsc_get_component_info(struct pdsc *pdsc)
{
	union pds_core_dev_cmd cmd = {
		.get_component_info.opcode = PDS_CORE_CMD_GET_COMPONENT_INFO,
		.get_component_info.ver = 1,
	};
	struct pds_core_component_list_info *list_info;
	struct pdsc_deferred_dma *deferred;
	union pds_core_dev_comp comp = {};
	dma_addr_t dma_addr;
	u8 num_components;
	int err, i;

	deferred = kmalloc_obj(*deferred);
	if (!deferred)
		return -ENOMEM;

	list_info = kzalloc(PDS_PAGE_SIZE, GFP_KERNEL);
	if (!list_info) {
		kfree(deferred);
		return -ENOMEM;
	}

	dma_addr = dma_map_single(pdsc->dev, list_info, PDS_PAGE_SIZE,
				  DMA_FROM_DEVICE);
	if (dma_mapping_error(pdsc->dev, dma_addr)) {
		dev_err(pdsc->dev,
			"Failed to dma_map component_list_info length %d\n",
			PDS_PAGE_SIZE);
		kfree(list_info);
		kfree(deferred);
		return -ENOMEM;
	}

	cmd.get_component_info.data_len = cpu_to_le16(PDS_PAGE_SIZE);
	cmd.get_component_info.data_pa = cpu_to_le64(dma_addr);

	err = pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout * 2);
	if (err == -ETIMEDOUT || err == -EAGAIN) {
		pdsc_deferred_dma_add(pdsc, deferred, dma_addr, list_info,
				      PDS_PAGE_SIZE, DMA_FROM_DEVICE);
		return err;
	}

	kfree(deferred);
	dma_unmap_single(pdsc->dev, dma_addr, PDS_PAGE_SIZE, DMA_FROM_DEVICE);
	if (err)
		goto out;

	if (comp.get_component_info.ver == 0) {
		/* Don't support backward compatibility as version 0 has
		 * alignment issues, so give a hint to users to update
		 * their firmware
		 */
		dev_warn_once(pdsc->dev,
			      "Incompatible get_component_info version %u reported by firmware\n",
			      comp.get_component_info.ver);
		err = 0;
		goto out;
	}

	num_components = list_info->num_components;
	if (num_components > PDS_CORE_FW_COMPONENT_LIST_LEN) {
		err = -ENOMEM;
		goto out;
	}

	pdsc->fw_components.num_components = num_components;
	for (i = 0; i < num_components; i++) {
		struct pds_core_fw_component_info *info =
			&pdsc->fw_components.info[i];

		memcpy(info, &list_info->info[i], sizeof(*info));
		info->version[PDS_CORE_FW_COMPONENT_VER_BUFLEN - 1] = 0;
		info->name[PDS_CORE_FW_COMPONENT_NAME_BUFLEN - 1] = 0;
	}

out:
	kfree(list_info);
	return err;
}

static int pdsc_devcmd_send_component(struct pdsc *pdsc,
				      struct pds_core_flash_component *info,
				      u16 info_sz, dma_addr_t addr, u32 length,
				      u32 offset, u16 slot_id,
				      union pds_core_dev_comp *comp)
{
	union pds_core_dev_cmd cmd = {
		.send_component.opcode = PDS_CORE_CMD_SEND_COMPONENT,
		.send_component.ver = 1,
		.send_component.operation = PDS_CORE_SEND_COMPONENT_START,
		.send_component.data_pa = cpu_to_le64(addr),
		.send_component.data_len = cpu_to_le32(length),
		.send_component.offset = cpu_to_le32(offset),
		.send_component.slot_id = slot_id,
	};
	unsigned long timeout = 300 * HZ;
	unsigned long start_time;
	unsigned long end_time;
	int err;

	start_time = jiffies;
	end_time = start_time + timeout;
	do {
		/* prevent noisy/benign devcmd failures */
		err = pdsc_devcmd_with_data_nomsg(pdsc, &cmd, info, info_sz,
						  comp, 60);
		if (err != -EAGAIN)
			break;

		/* if required, subsequent commands check status of
		 * PDS_CORE_CMD_SEND_COMPONENT command, which returns
		 * EAGAIN while the command is still running,
		 * else we get the final command status.
		 */
		cmd.send_component.operation = PDS_CORE_SEND_COMPONENT_STATUS;
		msleep(20);
	} while (time_before(jiffies, end_time));

	if (err == -EAGAIN || err == -ETIMEDOUT)
		dev_err(pdsc->dev, "PDS_CORE_CMD_SEND_COMPONENT timed out\n");

	return err;
}

static int pdsc_flash_component_chunk(struct pdsc *pdsc, struct device *dev,
				      struct pds_core_flash_component *info,
				      u16 info_sz, const u8 *data, u16 copy_sz,
				      u32 offset, u8 slot_id,
				      union pds_core_dev_comp *comp)
{
	struct pdsc_deferred_dma *deferred;
	dma_addr_t dma_addr;
	u8 *component_data;
	int err;

	deferred = kmalloc_obj(*deferred, GFP_KERNEL);
	if (!deferred)
		return -ENOMEM;

	component_data = kmemdup(data, copy_sz, GFP_KERNEL);
	if (!component_data) {
		kfree(deferred);
		return -ENOMEM;
	}

	dma_addr = dma_map_single(dev, component_data, copy_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr)) {
		dev_err(dev,
			"Failed to dma_map component_data at offset 0x%x copy_sz 0x%x\n",
			offset, copy_sz);
		kfree(component_data);
		kfree(deferred);
		return -ENOMEM;
	}

	err = pdsc_devcmd_send_component(pdsc, info, info_sz, dma_addr,
					 copy_sz, offset, slot_id, comp);
	if (err == -ETIMEDOUT || err == -EAGAIN) {
		pdsc_deferred_dma_add(pdsc, deferred, dma_addr,
				      component_data, copy_sz, DMA_TO_DEVICE);
		return err;
	}

	kfree(deferred);
	dma_unmap_single(dev, dma_addr, copy_sz, DMA_TO_DEVICE);
	kfree(component_data);

	return err;
}

static int pdsc_flash_component(struct pldmfw *context,
				struct pldmfw_component *component)
{
	char component_name_buf[PDSC_FW_COMPONENT_FULL_NAME_BUFLEN];
	struct pds_core_fwu_priv *priv =
		container_of(context, struct pds_core_fwu_priv, context);
	struct pds_core_flash_component *component_info;
	const char *component_name = NULL;
	struct device *dev = context->dev;
	struct pdsc *pdsc = priv->pdsc;
	u16 buf_sz, info_sz;
	struct devlink *dl;
	u8 component_type;
	u32 total_len;
	u32 offset;
	int err;

	component_type = pdsc_get_component_type_by_id(pdsc,
						       component->identifier);
	if (component_type) {
		const char *type_name = pdsc_fw_type_to_name(component_type);

		if (component_type == PDS_CORE_FW_TYPE_MAIN) {
			component_name = "fw";
		} else if (type_name) {
			snprintf(component_name_buf, sizeof(component_name_buf),
				 "%s%s", PDSC_FW_COMPONENT_PREFIX, type_name);
			component_name = component_name_buf;
		}
	}

	dl = priv_to_devlink(pdsc);

	if (pdsc_skip_component(priv, component->identifier)) {
		devlink_flash_update_status_notify(dl, "Skipped",
						   component_name, 0, 0);
		return 0;
	}

	total_len = component->component_size;
	dev_dbg(dev,
		"component name %s class %u id %u act_meth %u ver_str %.*s index %u size %u\n",
		component_name ?: "(unknown)", component->classification,
		component->identifier, component->activation_method,
		component->version_len, component->version_string,
		component->index, component->component_size);

	buf_sz = sizeof(pdsc->cmd_regs->data);
	info_sz = struct_size(component_info, version_str,
			      component->version_len);
	if (info_sz > buf_sz) {
		dev_err(dev, "component_info size %d too big, max size: %d\n",
			info_sz, buf_sz);
		return -ENOSPC;
	}
	component_info = vzalloc(info_sz);
	if (!component_info)
		return -ENOMEM;

	component_info->comparison_stamp =
		cpu_to_le32(component->comparison_stamp);
	component_info->image_size = cpu_to_le32(total_len);
	component_info->classification = cpu_to_le16(component->classification);
	component_info->identifier = cpu_to_le16(component->identifier);
	component_info->options = cpu_to_le16(component->options);
	component_info->version_str_type = component->version_type;
	component_info->version_str_len = component->version_len;
	memcpy(component_info->version_str, component->version_string,
	       component->version_len);

	offset = 0;
	while (offset < total_len) {
		union pds_core_dev_comp comp = {};
		u16 copy_sz;

		copy_sz = min_t(unsigned int, PDS_PAGE_SIZE,
				total_len - offset);

		err = pdsc_flash_component_chunk(pdsc, dev, component_info,
						 info_sz,
						 component->component_data +
						 offset, copy_sz, offset,
						 PDS_CORE_FW_SLOT_INVALID,
						 &comp);
		if (err &&
		    comp.send_component.compat_response &&
		    (comp.send_component.compat_response_code ==
		     PDS_CORE_COMPONENT_STAMP_IDENTICAL ||
		     comp.send_component.compat_response_code ==
		     PDS_CORE_COMPONENT_STAMP_LOWER)) {
			err = 0;
			devlink_flash_update_status_notify(dl, "Skipped",
							   component_name,
							   0, 0);
			goto skip_component;
		}

		if (err) {
			NL_SET_ERR_MSG_MOD(priv->extack,
					   "Failed to flash component");
			goto err_out;
		}

		offset += copy_sz;
		devlink_flash_update_status_notify(dl,
						   "Erasing/Flashing",
						   component_name, offset,
						   total_len);
	}

	vfree(component_info);
	return 0;

err_out:
	devlink_flash_update_status_notify(dl,
					   "Erasing/Flashing Component Failed",
					   component_name, 0, 0);
skip_component:
	vfree(component_info);
	return err;
}

static int pdsc_devcmd_finalize_update(struct pdsc *pdsc)
{
	union pds_core_dev_cmd cmd = {
		.finalize_update.opcode = PDS_CORE_CMD_FINALIZE_UPDATE,
		.finalize_update.ver = 1,
	};
	union pds_core_dev_comp comp = {};

	return pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
}

static int pdsc_finalize_update(struct pldmfw *context)
{
	struct pds_core_fwu_priv *priv =
		container_of(context, struct pds_core_fwu_priv, context);
	const char *component_name = priv->params->component;
	unsigned long start_time, end_time;
	struct device *dev = context->dev;
	struct pdsc *pdsc = priv->pdsc;
	struct devlink *dl;
	int err;

	dl = priv_to_devlink(pdsc);

	start_time = jiffies;
	end_time = start_time + (PDSC_FW_INSTALL_TIMEOUT * HZ);
	do {
		err = pdsc_devcmd_finalize_update(pdsc);
		if (err != -EAGAIN)
			break;

		dev_dbg(dev, "retrying finalize_update: %pe\n", ERR_PTR(err));
		msleep(20);
	} while (time_before(jiffies, end_time) && err == -EAGAIN);

	if (err) {
		devlink_flash_update_status_notify(dl, "Finalize Update Failed",
						   component_name, 0, 0);
		NL_SET_ERR_MSG_MOD(priv->extack, "Finalize update failed");
		return err;
	}

	devlink_flash_update_status_notify(dl, "Finalized Update",
					   component_name, 0, 0);
	return 0;
}

static const struct pldmfw_ops pdsc_pldmfw_ops = {
	.match_record = pdsc_match_record_descs,
	.send_package_data = pdsc_send_package_data,
	.send_component_table = pdsc_send_component_table,
	.flash_component = pdsc_flash_component,
	.finalize_update = pdsc_finalize_update
};

static int pdsc_pldm_firmware_update(struct pdsc *pdsc,
				     struct devlink_flash_update_params *params,
				     struct netlink_ext_ack *extack,
				     const struct firmware *fw)
{
	struct pds_core_fwu_priv priv = {};
	int err;

	if (!pdsc->fw_components.num_components) {
		err = pdsc_get_component_info(pdsc);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed to get component info");
			return err;
		}
	}

	if (params->component) {
		u8 type = pdsc_name_to_fw_type(params->component);

		if (!type || !pdsc_component_type_exists(pdsc, type)) {
			NL_SET_ERR_MSG_MOD(extack, "Unknown component name");
			return -ENOENT;
		}
	}

	INIT_LIST_HEAD(&priv.components);
	priv.context.ops = &pdsc_pldmfw_ops;
	priv.context.dev = pdsc->dev;
	priv.params = params;
	priv.extack = extack;
	priv.pdsc = pdsc;

	err = pldmfw_flash_image(&priv.context, fw);
	if (!err && params->component && !priv.component_found) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Requested component not present in firmware package");
		err = -ENOENT;
	}
	pdsc_free_fwu_priv(&priv);

	return err;
}

int pdsc_firmware_update(struct pdsc *pdsc,
			 struct devlink_flash_update_params *params,
			 struct netlink_ext_ack *extack)
{
	int err;

	if (pdsc->dev_ident.version >= PDS_CORE_IDENTITY_VERSION_2 &&
	    pdsc->dev_ident.capabilities &
		cpu_to_le64(PDS_CORE_DEV_CAP_PLDM_FW_UPDATE))
		err = pdsc_pldm_firmware_update(pdsc, params, extack,
						params->fw);
	else
		err = pdsc_legacy_firmware_update(pdsc, params, extack);

	/* Invalidate cached component info so next info_get refreshes */
	pdsc_fw_components_invalidate(pdsc);

	return err;
}
