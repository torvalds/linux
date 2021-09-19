// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#include "iosm_ipc_chnl_cfg.h"
#include "iosm_ipc_coredump.h"
#include "iosm_ipc_devlink.h"
#include "iosm_ipc_flash.h"

/* Coredump list */
static struct iosm_coredump_file_info list[IOSM_NOF_CD_REGION] = {
	{"report.json", REPORT_JSON_SIZE,},
	{"coredump.fcd", COREDUMP_FCD_SIZE,},
	{"cdd.log", CDD_LOG_SIZE,},
	{"eeprom.bin", EEPROM_BIN_SIZE,},
	{"bootcore_trace.bin", BOOTCORE_TRC_BIN_SIZE,},
	{"bootcore_prev_trace.bin", BOOTCORE_PREV_TRC_BIN_SIZE,},
};

/* Get the param values for the specific param ID's */
static int ipc_devlink_get_param(struct devlink *dl, u32 id,
				 struct devlink_param_gset_ctx *ctx)
{
	struct iosm_devlink *ipc_devlink = devlink_priv(dl);
	int rc = 0;

	switch (id) {
	case IOSM_DEVLINK_PARAM_ID_ERASE_FULL_FLASH:
		ctx->val.vu8 = ipc_devlink->param.erase_full_flash;
		break;

	case IOSM_DEVLINK_PARAM_ID_DOWNLOAD_REGION:
		ctx->val.vu8 = ipc_devlink->param.download_region;
		break;

	case IOSM_DEVLINK_PARAM_ID_ADDRESS:
		ctx->val.vu32 = ipc_devlink->param.address;
		break;

	case IOSM_DEVLINK_PARAM_ID_REGION_COUNT:
		ctx->val.vu8 = ipc_devlink->param.region_count;
		break;

	default:
		rc = -EOPNOTSUPP;
		break;
	}

	return rc;
}

/* Set the param values for the specific param ID's */
static int ipc_devlink_set_param(struct devlink *dl, u32 id,
				 struct devlink_param_gset_ctx *ctx)
{
	struct iosm_devlink *ipc_devlink = devlink_priv(dl);
	int rc = 0;

	switch (id) {
	case IOSM_DEVLINK_PARAM_ID_ERASE_FULL_FLASH:
		ipc_devlink->param.erase_full_flash = ctx->val.vu8;
		break;

	case IOSM_DEVLINK_PARAM_ID_DOWNLOAD_REGION:
		ipc_devlink->param.download_region = ctx->val.vu8;
		break;

	case IOSM_DEVLINK_PARAM_ID_ADDRESS:
		ipc_devlink->param.address = ctx->val.vu32;
		break;

	case IOSM_DEVLINK_PARAM_ID_REGION_COUNT:
		ipc_devlink->param.region_count = ctx->val.vu8;
		break;

	default:
		rc = -EOPNOTSUPP;
		break;
	}

	return rc;
}

/* Devlink param structure array */
static const struct devlink_param iosm_devlink_params[] = {
	DEVLINK_PARAM_DRIVER(IOSM_DEVLINK_PARAM_ID_ERASE_FULL_FLASH,
			     "erase_full_flash", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     ipc_devlink_get_param, ipc_devlink_set_param,
			     NULL),
	DEVLINK_PARAM_DRIVER(IOSM_DEVLINK_PARAM_ID_DOWNLOAD_REGION,
			     "download_region", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     ipc_devlink_get_param, ipc_devlink_set_param,
			     NULL),
	DEVLINK_PARAM_DRIVER(IOSM_DEVLINK_PARAM_ID_ADDRESS,
			     "address", DEVLINK_PARAM_TYPE_U32,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     ipc_devlink_get_param, ipc_devlink_set_param,
			     NULL),
	DEVLINK_PARAM_DRIVER(IOSM_DEVLINK_PARAM_ID_REGION_COUNT,
			     "region_count", DEVLINK_PARAM_TYPE_U8,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     ipc_devlink_get_param, ipc_devlink_set_param,
			     NULL),
};

/* Get devlink flash component type */
static enum iosm_flash_comp_type
ipc_devlink_get_flash_comp_type(const char comp_str[], u32 len)
{
	enum iosm_flash_comp_type fls_type;

	if (!strncmp("PSI", comp_str, len))
		fls_type = FLASH_COMP_TYPE_PSI;
	else if (!strncmp("EBL", comp_str, len))
		fls_type = FLASH_COMP_TYPE_EBL;
	else if (!strncmp("FLS", comp_str, len))
		fls_type = FLASH_COMP_TYPE_FLS;
	else
		fls_type = FLASH_COMP_TYPE_INVAL;

	return fls_type;
}

/* Function triggered on devlink flash command
 * Flash update function which calls multiple functions based on
 * component type specified in the flash command
 */
static int ipc_devlink_flash_update(struct devlink *devlink,
				    struct devlink_flash_update_params *params,
				    struct netlink_ext_ack *extack)
{
	struct iosm_devlink *ipc_devlink = devlink_priv(devlink);
	enum iosm_flash_comp_type fls_type;
	u32 rc = -EINVAL;
	u8 *mdm_rsp;

	if (!params->component)
		return rc;

	mdm_rsp = kzalloc(IOSM_EBL_DW_PACK_SIZE, GFP_KERNEL);
	if (!mdm_rsp)
		return -ENOMEM;

	fls_type = ipc_devlink_get_flash_comp_type(params->component,
						   strlen(params->component));

	switch (fls_type) {
	case FLASH_COMP_TYPE_PSI:
		rc = ipc_flash_boot_psi(ipc_devlink, params->fw);
		break;
	case FLASH_COMP_TYPE_EBL:
		rc = ipc_flash_boot_ebl(ipc_devlink, params->fw);
		if (!rc)
			rc = ipc_flash_boot_set_capabilities(ipc_devlink,
							     mdm_rsp);
		if (!rc)
			rc = ipc_flash_read_swid(ipc_devlink, mdm_rsp);
		break;
	case FLASH_COMP_TYPE_FLS:
		rc = ipc_flash_send_fls(ipc_devlink, params->fw, mdm_rsp);
		break;
	default:
		devlink_flash_update_status_notify(devlink, "Invalid component",
						   params->component, 0, 0);
		break;
	}

	if (!rc)
		devlink_flash_update_status_notify(devlink, "Flashing success",
						   params->component, 0, 0);
	else
		devlink_flash_update_status_notify(devlink, "Flashing failed",
						   params->component, 0, 0);

	kfree(mdm_rsp);
	return rc;
}

/* Call back function for devlink ops */
static const struct devlink_ops devlink_flash_ops = {
	.supported_flash_update_params = DEVLINK_SUPPORT_FLASH_UPDATE_COMPONENT,
	.flash_update = ipc_devlink_flash_update,
};

/* Send command to modem to collect data */
int ipc_devlink_send_cmd(struct iosm_devlink *ipc_devlink, u16 cmd, u32 entry)
{
	struct iosm_rpsi_cmd rpsi_cmd;

	rpsi_cmd.param.dword = cpu_to_le32(entry);
	rpsi_cmd.cmd = cpu_to_le16(cmd);
	rpsi_cmd.crc = rpsi_cmd.param.word[0] ^ rpsi_cmd.param.word[1] ^
		       rpsi_cmd.cmd;

	return ipc_imem_sys_devlink_write(ipc_devlink, (u8 *)&rpsi_cmd,
					  sizeof(rpsi_cmd));
}

static int ipc_devlink_coredump_snapshot(struct devlink *dl,
					 const struct devlink_region_ops *ops,
					 struct netlink_ext_ack *extack,
					 u8 **data)
{
	struct iosm_devlink *ipc_devlink = devlink_priv(dl);
	struct iosm_coredump_file_info *cd_list = ops->priv;
	u32 region_size;
	int rc;

	dev_dbg(ipc_devlink->dev, "Region:%s, ID:%d", ops->name,
		cd_list->entry);
	region_size = cd_list->default_size;
	rc = ipc_coredump_collect(ipc_devlink, data, cd_list->entry,
				  region_size);
	if (rc) {
		dev_err(ipc_devlink->dev, "Fail to create snapshot,err %d", rc);
		goto coredump_collect_err;
	}

	/* Send coredump end cmd indicating end of coredump collection */
	if (cd_list->entry == (IOSM_NOF_CD_REGION - 1))
		ipc_coredump_get_list(ipc_devlink, rpsi_cmd_coredump_end);

	return rc;
coredump_collect_err:
	ipc_coredump_get_list(ipc_devlink, rpsi_cmd_coredump_end);
	return rc;
}

/* To create regions for coredump files */
static int ipc_devlink_create_region(struct iosm_devlink *devlink)
{
	struct devlink_region_ops *mdm_coredump;
	int rc = 0;
	int i;

	mdm_coredump = devlink->iosm_devlink_mdm_coredump;
	for (i = 0; i < IOSM_NOF_CD_REGION; i++) {
		mdm_coredump[i].name = list[i].filename;
		mdm_coredump[i].snapshot = ipc_devlink_coredump_snapshot;
		mdm_coredump[i].destructor = vfree;
		devlink->cd_regions[i] =
			devlink_region_create(devlink->devlink_ctx,
					      &mdm_coredump[i], MAX_SNAPSHOTS,
					      list[i].default_size);

		if (IS_ERR(devlink->cd_regions[i])) {
			rc = PTR_ERR(devlink->cd_regions[i]);
			dev_err(devlink->dev, "Devlink region fail,err %d", rc);
			/* Delete previously created regions */
			for ( ; i >= 0; i--)
				devlink_region_destroy(devlink->cd_regions[i]);
			goto region_create_fail;
		}
		list[i].entry = i;
		mdm_coredump[i].priv = list + i;
	}
region_create_fail:
	return rc;
}

/* To Destroy devlink regions */
static void ipc_devlink_destroy_region(struct iosm_devlink *ipc_devlink)
{
	u8 i;

	for (i = 0; i < IOSM_NOF_CD_REGION; i++)
		devlink_region_destroy(ipc_devlink->cd_regions[i]);
}

/* Handle registration to devlink framework */
struct iosm_devlink *ipc_devlink_init(struct iosm_imem *ipc_imem)
{
	struct ipc_chnl_cfg chnl_cfg_flash = { 0 };
	struct iosm_devlink *ipc_devlink;
	struct devlink *devlink_ctx;
	int rc;

	devlink_ctx = devlink_alloc(&devlink_flash_ops,
				    sizeof(struct iosm_devlink),
				    ipc_imem->dev);
	if (!devlink_ctx) {
		dev_err(ipc_imem->dev, "devlink_alloc failed");
		goto devlink_alloc_fail;
	}

	ipc_devlink = devlink_priv(devlink_ctx);
	ipc_devlink->devlink_ctx = devlink_ctx;
	ipc_devlink->pcie = ipc_imem->pcie;
	ipc_devlink->dev = ipc_imem->dev;
	rc = devlink_register(devlink_ctx);
	if (rc) {
		dev_err(ipc_devlink->dev, "devlink_register failed rc %d", rc);
		goto free_dl;
	}

	rc = devlink_params_register(devlink_ctx, iosm_devlink_params,
				     ARRAY_SIZE(iosm_devlink_params));
	if (rc) {
		dev_err(ipc_devlink->dev,
			"devlink_params_register failed. rc %d", rc);
		goto param_reg_fail;
	}

	devlink_params_publish(devlink_ctx);
	ipc_devlink->cd_file_info = list;

	rc = ipc_devlink_create_region(ipc_devlink);
	if (rc) {
		dev_err(ipc_devlink->dev, "Devlink Region create failed, rc %d",
			rc);
		goto region_create_fail;
	}

	if (ipc_chnl_cfg_get(&chnl_cfg_flash, IPC_MEM_CTRL_CHL_ID_7) < 0)
		goto chnl_get_fail;

	ipc_imem_channel_init(ipc_imem, IPC_CTYPE_CTRL,
			      chnl_cfg_flash, IRQ_MOD_OFF);

	init_completion(&ipc_devlink->devlink_sio.read_sem);
	skb_queue_head_init(&ipc_devlink->devlink_sio.rx_list);

	dev_dbg(ipc_devlink->dev, "iosm devlink register success");

	return ipc_devlink;

chnl_get_fail:
	ipc_devlink_destroy_region(ipc_devlink);
region_create_fail:
	devlink_params_unpublish(devlink_ctx);
	devlink_params_unregister(devlink_ctx, iosm_devlink_params,
				  ARRAY_SIZE(iosm_devlink_params));
param_reg_fail:
	devlink_unregister(devlink_ctx);
free_dl:
	devlink_free(devlink_ctx);
devlink_alloc_fail:
	return NULL;
}

/* Handle unregistration of devlink */
void ipc_devlink_deinit(struct iosm_devlink *ipc_devlink)
{
	struct devlink *devlink_ctx = ipc_devlink->devlink_ctx;

	ipc_devlink_destroy_region(ipc_devlink);
	devlink_params_unpublish(devlink_ctx);
	devlink_params_unregister(devlink_ctx, iosm_devlink_params,
				  ARRAY_SIZE(iosm_devlink_params));
	if (ipc_devlink->devlink_sio.devlink_read_pend) {
		complete(&ipc_devlink->devlink_sio.read_sem);
		complete(&ipc_devlink->devlink_sio.channel->ul_sem);
	}
	if (!ipc_devlink->devlink_sio.devlink_read_pend)
		skb_queue_purge(&ipc_devlink->devlink_sio.rx_list);

	ipc_imem_sys_devlink_close(ipc_devlink);
	devlink_unregister(devlink_ctx);
	devlink_free(devlink_ctx);
}
