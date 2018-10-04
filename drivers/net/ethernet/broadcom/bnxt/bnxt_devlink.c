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
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_vfr.h"
#include "bnxt_devlink.h"

static const struct devlink_ops bnxt_dl_ops = {
#ifdef CONFIG_BNXT_SRIOV
	.eswitch_mode_set = bnxt_dl_eswitch_mode_set,
	.eswitch_mode_get = bnxt_dl_eswitch_mode_get,
#endif /* CONFIG_BNXT_SRIOV */
};

static const struct bnxt_dl_nvm_param nvm_params[] = {
	{DEVLINK_PARAM_GENERIC_ID_ENABLE_SRIOV, NVM_OFF_ENABLE_SRIOV,
	 BNXT_NVM_SHARED_CFG, 1},
};

static int bnxt_hwrm_nvm_req(struct bnxt *bp, u32 param_id, void *msg,
			     int msg_len, union devlink_param_value *val)
{
	struct hwrm_nvm_get_variable_input *req = msg;
	void *data_addr = NULL, *buf = NULL;
	struct bnxt_dl_nvm_param nvm_param;
	int bytesize, idx = 0, rc, i;
	dma_addr_t data_dma_addr;

	/* Get/Set NVM CFG parameter is supported only on PFs */
	if (BNXT_VF(bp))
		return -EPERM;

	for (i = 0; i < ARRAY_SIZE(nvm_params); i++) {
		if (nvm_params[i].id == param_id) {
			nvm_param = nvm_params[i];
			break;
		}
	}

	if (i == ARRAY_SIZE(nvm_params))
		return -EOPNOTSUPP;

	if (nvm_param.dir_type == BNXT_NVM_PORT_CFG)
		idx = bp->pf.port_id;
	else if (nvm_param.dir_type == BNXT_NVM_FUNC_CFG)
		idx = bp->pf.fw_fid - BNXT_FIRST_PF_FID;

	bytesize = roundup(nvm_param.num_bits, BITS_PER_BYTE) / BITS_PER_BYTE;
	if (nvm_param.num_bits == 1)
		buf = &val->vbool;

	data_addr = dma_zalloc_coherent(&bp->pdev->dev, bytesize,
					&data_dma_addr, GFP_KERNEL);
	if (!data_addr)
		return -ENOMEM;

	req->dest_data_addr = cpu_to_le64(data_dma_addr);
	req->data_len = cpu_to_le16(nvm_param.num_bits);
	req->option_num = cpu_to_le16(nvm_param.offset);
	req->index_0 = cpu_to_le16(idx);
	if (idx)
		req->dimensions = cpu_to_le16(1);

	if (req->req_type == cpu_to_le16(HWRM_NVM_SET_VARIABLE))
		memcpy(data_addr, buf, bytesize);

	rc = hwrm_send_message(bp, msg, msg_len, HWRM_CMD_TIMEOUT);
	if (!rc && req->req_type == cpu_to_le16(HWRM_NVM_GET_VARIABLE))
		memcpy(buf, data_addr, bytesize);

	dma_free_coherent(&bp->pdev->dev, bytesize, data_addr, data_dma_addr);
	if (rc == HWRM_ERR_CODE_RESOURCE_ACCESS_DENIED) {
		netdev_err(bp->dev, "PF does not have admin privileges to modify NVM config\n");
		return -EACCES;
	} else if (rc) {
		return -EIO;
	}
	return 0;
}

static int bnxt_dl_nvm_param_get(struct devlink *dl, u32 id,
				 struct devlink_param_gset_ctx *ctx)
{
	struct hwrm_nvm_get_variable_input req = {0};
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_NVM_GET_VARIABLE, -1, -1);
	return bnxt_hwrm_nvm_req(bp, id, &req, sizeof(req), &ctx->val);
}

static int bnxt_dl_nvm_param_set(struct devlink *dl, u32 id,
				 struct devlink_param_gset_ctx *ctx)
{
	struct hwrm_nvm_set_variable_input req = {0};
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_NVM_SET_VARIABLE, -1, -1);
	return bnxt_hwrm_nvm_req(bp, id, &req, sizeof(req), &ctx->val);
}

static const struct devlink_param bnxt_dl_params[] = {
	DEVLINK_PARAM_GENERIC(ENABLE_SRIOV,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      NULL),
};

int bnxt_dl_register(struct bnxt *bp)
{
	struct devlink *dl;
	int rc;

	if (bp->hwrm_spec_code < 0x10600) {
		netdev_warn(bp->dev, "Firmware does not support NVM params");
		return -ENOTSUPP;
	}

	dl = devlink_alloc(&bnxt_dl_ops, sizeof(struct bnxt_dl));
	if (!dl) {
		netdev_warn(bp->dev, "devlink_alloc failed");
		return -ENOMEM;
	}

	bnxt_link_bp_to_dl(bp, dl);

	/* Add switchdev eswitch mode setting, if SRIOV supported */
	if (pci_find_ext_capability(bp->pdev, PCI_EXT_CAP_ID_SRIOV) &&
	    bp->hwrm_spec_code > 0x10803)
		bp->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;

	rc = devlink_register(dl, &bp->pdev->dev);
	if (rc) {
		netdev_warn(bp->dev, "devlink_register failed. rc=%d", rc);
		goto err_dl_free;
	}

	rc = devlink_params_register(dl, bnxt_dl_params,
				     ARRAY_SIZE(bnxt_dl_params));
	if (rc) {
		netdev_warn(bp->dev, "devlink_params_register failed. rc=%d",
			    rc);
		goto err_dl_unreg;
	}

	return 0;

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

	devlink_params_unregister(dl, bnxt_dl_params,
				  ARRAY_SIZE(bnxt_dl_params));
	devlink_unregister(dl);
	devlink_free(dl);
}
