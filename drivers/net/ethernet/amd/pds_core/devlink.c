// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include "core.h"

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
	char buf[16];
	int listlen;
	int err;
	int i;

	mutex_lock(&pdsc->devcmd_lock);
	err = pdsc_devcmd_locked(pdsc, &cmd, &comp, pdsc->devcmd_timeout * 2);
	memcpy_fromio(&fw_list, pdsc->cmd_regs->data, sizeof(fw_list));
	mutex_unlock(&pdsc->devcmd_lock);
	if (err && err != -EIO)
		return err;

	listlen = fw_list.num_fw_slots;
	for (i = 0; i < listlen; i++) {
		if (i < ARRAY_SIZE(fw_slotnames))
			strscpy(buf, fw_slotnames[i], sizeof(buf));
		else
			snprintf(buf, sizeof(buf), "fw.slot_%d", i);
		err = devlink_info_version_stored_put(req, buf,
						      fw_list.fw_names[i].fw_version);
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
	int err;

	mutex_lock(&pdsc->config_lock);

	if (test_bit(PDSC_S_FW_DEAD, &pdsc->state))
		err = devlink_fmsg_string_pair_put(fmsg, "Status", "dead");
	else if (!pdsc_is_fw_good(pdsc))
		err = devlink_fmsg_string_pair_put(fmsg, "Status", "unhealthy");
	else
		err = devlink_fmsg_string_pair_put(fmsg, "Status", "healthy");

	mutex_unlock(&pdsc->config_lock);

	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "State",
					pdsc->fw_status &
						~PDS_CORE_FW_STS_F_GENERATION);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "Generation",
					pdsc->fw_generation >> 4);
	if (err)
		return err;

	return devlink_fmsg_u32_pair_put(fmsg, "Recoveries",
					 pdsc->fw_recoveries);
}
