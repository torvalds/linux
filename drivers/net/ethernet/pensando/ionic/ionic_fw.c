// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 Pensando Systems, Inc */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/firmware.h>

#include "ionic.h"
#include "ionic_dev.h"
#include "ionic_lif.h"
#include "ionic_devlink.h"

/* The worst case wait for the install activity is about 25 minutes when
 * installing a new CPLD, which is very seldom.  Normal is about 30-35
 * seconds.  Since the driver can't tell if a CPLD update will happen we
 * set the timeout for the ugly case.
 */
#define IONIC_FW_INSTALL_TIMEOUT	(25 * 60)
#define IONIC_FW_SELECT_TIMEOUT		30

/* Number of periodic log updates during fw file download */
#define IONIC_FW_INTERVAL_FRACTION	32

static void ionic_dev_cmd_firmware_download(struct ionic_dev *idev, u64 addr,
					    u32 offset, u32 length)
{
	union ionic_dev_cmd cmd = {
		.fw_download.opcode = IONIC_CMD_FW_DOWNLOAD,
		.fw_download.offset = cpu_to_le32(offset),
		.fw_download.addr = cpu_to_le64(addr),
		.fw_download.length = cpu_to_le32(length),
	};

	ionic_dev_cmd_go(idev, &cmd);
}

static void ionic_dev_cmd_firmware_install(struct ionic_dev *idev)
{
	union ionic_dev_cmd cmd = {
		.fw_control.opcode = IONIC_CMD_FW_CONTROL,
		.fw_control.oper = IONIC_FW_INSTALL_ASYNC
	};

	ionic_dev_cmd_go(idev, &cmd);
}

static void ionic_dev_cmd_firmware_activate(struct ionic_dev *idev, u8 slot)
{
	union ionic_dev_cmd cmd = {
		.fw_control.opcode = IONIC_CMD_FW_CONTROL,
		.fw_control.oper = IONIC_FW_ACTIVATE_ASYNC,
		.fw_control.slot = slot
	};

	ionic_dev_cmd_go(idev, &cmd);
}

static int ionic_fw_status_long_wait(struct ionic *ionic,
				     const char *label,
				     unsigned long timeout,
				     u8 fw_cmd,
				     struct netlink_ext_ack *extack)
{
	union ionic_dev_cmd cmd = {
		.fw_control.opcode = IONIC_CMD_FW_CONTROL,
		.fw_control.oper = fw_cmd,
	};
	unsigned long start_time;
	unsigned long end_time;
	int err;

	start_time = jiffies;
	end_time = start_time + (timeout * HZ);
	do {
		mutex_lock(&ionic->dev_cmd_lock);
		ionic_dev_cmd_go(&ionic->idev, &cmd);
		err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
		mutex_unlock(&ionic->dev_cmd_lock);

		msleep(20);
	} while (time_before(jiffies, end_time) && (err == -EAGAIN || err == -ETIMEDOUT));

	if (err == -EAGAIN || err == -ETIMEDOUT) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware wait timed out");
		dev_err(ionic->dev, "DEV_CMD firmware wait %s timed out\n", label);
	} else if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware wait failed");
	}

	return err;
}

int ionic_firmware_update(struct ionic_lif *lif, const char *fw_name,
			  struct netlink_ext_ack *extack)
{
	struct ionic_dev *idev = &lif->ionic->idev;
	struct net_device *netdev = lif->netdev;
	struct ionic *ionic = lif->ionic;
	union ionic_dev_cmd_comp comp;
	u32 buf_sz, copy_sz, offset;
	const struct firmware *fw;
	struct devlink *dl;
	int next_interval;
	int err = 0;
	u8 fw_slot;

	netdev_info(netdev, "Installing firmware %s\n", fw_name);

	dl = priv_to_devlink(ionic);
	devlink_flash_update_begin_notify(dl);
	devlink_flash_update_status_notify(dl, "Preparing to flash", NULL, 0, 0);

	err = request_firmware(&fw, fw_name, ionic->dev);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to find firmware file");
		goto err_out;
	}

	buf_sz = sizeof(idev->dev_cmd_regs->data);

	netdev_dbg(netdev,
		   "downloading firmware - size %d part_sz %d nparts %lu\n",
		   (int)fw->size, buf_sz, DIV_ROUND_UP(fw->size, buf_sz));

	offset = 0;
	next_interval = 0;
	while (offset < fw->size) {
		if (offset >= next_interval) {
			devlink_flash_update_status_notify(dl, "Downloading", NULL,
							   offset, fw->size);
			next_interval = offset + (fw->size / IONIC_FW_INTERVAL_FRACTION);
		}

		copy_sz = min_t(unsigned int, buf_sz, fw->size - offset);
		mutex_lock(&ionic->dev_cmd_lock);
		memcpy_toio(&idev->dev_cmd_regs->data, fw->data + offset, copy_sz);
		ionic_dev_cmd_firmware_download(idev,
						offsetof(union ionic_dev_cmd_regs, data),
						offset, copy_sz);
		err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
		mutex_unlock(&ionic->dev_cmd_lock);
		if (err) {
			netdev_err(netdev,
				   "download failed offset 0x%x addr 0x%lx len 0x%x\n",
				   offset, offsetof(union ionic_dev_cmd_regs, data),
				   copy_sz);
			NL_SET_ERR_MSG_MOD(extack, "Segment download failed");
			goto err_out;
		}
		offset += copy_sz;
	}
	devlink_flash_update_status_notify(dl, "Downloading", NULL,
					   fw->size, fw->size);

	devlink_flash_update_timeout_notify(dl, "Installing", NULL,
					    IONIC_FW_INSTALL_TIMEOUT);

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_firmware_install(idev);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	ionic_dev_cmd_comp(idev, (union ionic_dev_cmd_comp *)&comp);
	fw_slot = comp.fw_control.slot;
	mutex_unlock(&ionic->dev_cmd_lock);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to start firmware install");
		goto err_out;
	}

	err = ionic_fw_status_long_wait(ionic, "Installing",
					IONIC_FW_INSTALL_TIMEOUT,
					IONIC_FW_INSTALL_STATUS,
					extack);
	if (err)
		goto err_out;

	devlink_flash_update_timeout_notify(dl, "Selecting", NULL,
					    IONIC_FW_SELECT_TIMEOUT);

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_firmware_activate(idev, fw_slot);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&ionic->dev_cmd_lock);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to start firmware select");
		goto err_out;
	}

	err = ionic_fw_status_long_wait(ionic, "Selecting",
					IONIC_FW_SELECT_TIMEOUT,
					IONIC_FW_ACTIVATE_STATUS,
					extack);
	if (err)
		goto err_out;

	netdev_info(netdev, "Firmware update completed\n");

err_out:
	if (err)
		devlink_flash_update_status_notify(dl, "Flash failed", NULL, 0, 0);
	else
		devlink_flash_update_status_notify(dl, "Flash done", NULL, 0, 0);
	release_firmware(fw);
	devlink_flash_update_end_notify(dl);
	return err;
}
