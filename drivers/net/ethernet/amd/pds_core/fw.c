// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

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

static int pdsc_devcmd_fw_download_locked(struct pdsc *pdsc, u64 addr,
					  u32 offset, u32 length)
{
	union pds_core_dev_cmd cmd = {
		.fw_download.opcode = PDS_CORE_CMD_FW_DOWNLOAD,
		.fw_download.offset = cpu_to_le32(offset),
		.fw_download.addr = cpu_to_le64(addr),
		.fw_download.length = cpu_to_le32(length),
	};
	union pds_core_dev_comp comp;

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

int pdsc_firmware_update(struct pdsc *pdsc, const struct firmware *fw,
			 struct netlink_ext_ack *extack)
{
	u32 buf_sz, copy_sz, offset;
	struct devlink *dl;
	int next_interval;
	u64 data_addr;
	int err = 0;
	int fw_slot;

	dev_info(pdsc->dev, "Installing firmware\n");

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
