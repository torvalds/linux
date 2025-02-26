// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include "core.h"
#include <linux/pds/pds_auxbus.h>

static struct
pdsc_viftype *pdsc_dl_find_viftype_by_id(struct pdsc *pdsc,
					 enum devlink_param_type dl_id)
{
	int vt;

	if (!pdsc->viftype_status)
		return NULL;

	for (vt = 0; vt < PDS_DEV_TYPE_MAX; vt++) {
		if (pdsc->viftype_status[vt].dl_id == dl_id)
			return &pdsc->viftype_status[vt];
	}

	return NULL;
}

int pdsc_dl_enable_get(struct devlink *dl, u32 id,
		       struct devlink_param_gset_ctx *ctx)
{
	struct pdsc *pdsc = devlink_priv(dl);
	struct pdsc_viftype *vt_entry;

	vt_entry = pdsc_dl_find_viftype_by_id(pdsc, id);
	if (!vt_entry)
		return -ENOENT;

	ctx->val.vbool = vt_entry->enabled;

	return 0;
}

int pdsc_dl_enable_set(struct devlink *dl, u32 id,
		       struct devlink_param_gset_ctx *ctx,
		       struct netlink_ext_ack *extack)
{
	struct pdsc *pdsc = devlink_priv(dl);
	struct pdsc_viftype *vt_entry;
	int err = 0;
	int vf_id;

	vt_entry = pdsc_dl_find_viftype_by_id(pdsc, id);
	if (!vt_entry || !vt_entry->supported)
		return -EOPNOTSUPP;

	if (vt_entry->enabled == ctx->val.vbool)
		return 0;

	vt_entry->enabled = ctx->val.vbool;
	for (vf_id = 0; vf_id < pdsc->num_vfs; vf_id++) {
		struct pdsc *vf = pdsc->vfs[vf_id].vf;

		err = ctx->val.vbool ? pdsc_auxbus_dev_add(vf, pdsc) :
				       pdsc_auxbus_dev_del(vf, pdsc);
	}

	return err;
}

int pdsc_dl_enable_validate(struct devlink *dl, u32 id,
			    union devlink_param_value val,
			    struct netlink_ext_ack *extack)
{
	struct pdsc *pdsc = devlink_priv(dl);
	struct pdsc_viftype *vt_entry;

	vt_entry = pdsc_dl_find_viftype_by_id(pdsc, id);
	if (!vt_entry || !vt_entry->supported)
		return -EOPNOTSUPP;

	if (!pdsc->viftype_status[vt_entry->vif_id].supported)
		return -ENODEV;

	return 0;
}

int pdsc_dl_flash_update(struct devlink *dl,
			 struct devlink_flash_update_params *params,
			 struct netlink_ext_ack *extack)
{
	struct pdsc *pdsc = devlink_priv(dl);

	return pdsc_firmware_update(pdsc, params->fw, extack);
}

static char *fw_slotnames[] = {
	"fw.goldfw",
	"fw.mainfwa",
	"fw.mainfwb",
};

int pdsc_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
		     struct netlink_ext_ack *extack)
{
	union pds_core_dev_cmd cmd = {
		.fw_control.opcode = PDS_CORE_CMD_FW_CONTROL,
		.fw_control.oper = PDS_CORE_FW_GET_LIST,
	};
	struct pds_core_fw_list_info fw_list;
	struct pdsc *pdsc = devlink_priv(dl);
	union pds_core_dev_comp comp;
	char buf[32];
	int listlen;
	int err;
	int i;

	mutex_lock(&pdsc->devcmd_lock);
	err = pdsc_devcmd_locked(pdsc, &cmd, &comp, pdsc->devcmd_timeout * 2);
	if (!err)
		memcpy_fromio(&fw_list, pdsc->cmd_regs->data, sizeof(fw_list));
	mutex_unlock(&pdsc->devcmd_lock);
	if (err && err != -EIO)
		return err;

	listlen = min(fw_list.num_fw_slots, ARRAY_SIZE(fw_list.fw_names));
	for (i = 0; i < listlen; i++) {
		if (i < ARRAY_SIZE(fw_slotnames))
			strscpy(buf, fw_slotnames[i], sizeof(buf));
		else
			snprintf(buf, sizeof(buf), "fw.slot_%d", i);
		err = devlink_info_version_stored_put(req, buf,
						      fw_list.fw_names[i].fw_version);
		if (err)
			return err;
	}

	err = devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW,
					       pdsc->dev_info.fw_version);
	if (err)
		return err;

	snprintf(buf, sizeof(buf), "0x%x", pdsc->dev_info.asic_type);
	err = devlink_info_version_fixed_put(req,
					     DEVLINK_INFO_VERSION_GENERIC_ASIC_ID,
					     buf);
	if (err)
		return err;

	snprintf(buf, sizeof(buf), "0x%x", pdsc->dev_info.asic_rev);
	err = devlink_info_version_fixed_put(req,
					     DEVLINK_INFO_VERSION_GENERIC_ASIC_REV,
					     buf);
	if (err)
		return err;

	return devlink_info_serial_number_put(req, pdsc->dev_info.serial_num);
}

int pdsc_fw_reporter_diagnose(struct devlink_health_reporter *reporter,
			      struct devlink_fmsg *fmsg,
			      struct netlink_ext_ack *extack)
{
	struct pdsc *pdsc = devlink_health_reporter_priv(reporter);

	mutex_lock(&pdsc->config_lock);
	if (test_bit(PDSC_S_FW_DEAD, &pdsc->state))
		devlink_fmsg_string_pair_put(fmsg, "Status", "dead");
	else if (!pdsc_is_fw_good(pdsc))
		devlink_fmsg_string_pair_put(fmsg, "Status", "unhealthy");
	else
		devlink_fmsg_string_pair_put(fmsg, "Status", "healthy");
	mutex_unlock(&pdsc->config_lock);

	devlink_fmsg_u32_pair_put(fmsg, "State",
				  pdsc->fw_status & ~PDS_CORE_FW_STS_F_GENERATION);
	devlink_fmsg_u32_pair_put(fmsg, "Generation", pdsc->fw_generation >> 4);
	devlink_fmsg_u32_pair_put(fmsg, "Recoveries", pdsc->fw_recoveries);

	return 0;
}
