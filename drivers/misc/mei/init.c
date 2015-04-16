/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "client.h"

const char *mei_dev_state_str(int state)
{
#define MEI_DEV_STATE(state) case MEI_DEV_##state: return #state
	switch (state) {
	MEI_DEV_STATE(INITIALIZING);
	MEI_DEV_STATE(INIT_CLIENTS);
	MEI_DEV_STATE(ENABLED);
	MEI_DEV_STATE(RESETTING);
	MEI_DEV_STATE(DISABLED);
	MEI_DEV_STATE(POWER_DOWN);
	MEI_DEV_STATE(POWER_UP);
	default:
		return "unknown";
	}
#undef MEI_DEV_STATE
}

const char *mei_pg_state_str(enum mei_pg_state state)
{
#define MEI_PG_STATE(state) case MEI_PG_##state: return #state
	switch (state) {
	MEI_PG_STATE(OFF);
	MEI_PG_STATE(ON);
	default:
		return "unknown";
	}
#undef MEI_PG_STATE
}

/**
 * mei_fw_status2str - convert fw status registers to printable string
 *
 * @fw_status:  firmware status
 * @buf: string buffer at minimal size MEI_FW_STATUS_STR_SZ
 * @len: buffer len must be >= MEI_FW_STATUS_STR_SZ
 *
 * Return: number of bytes written or -EINVAL if buffer is to small
 */
ssize_t mei_fw_status2str(struct mei_fw_status *fw_status,
			  char *buf, size_t len)
{
	ssize_t cnt = 0;
	int i;

	buf[0] = '\0';

	if (len < MEI_FW_STATUS_STR_SZ)
		return -EINVAL;

	for (i = 0; i < fw_status->count; i++)
		cnt += scnprintf(buf + cnt, len - cnt, "%08X ",
				fw_status->status[i]);

	/* drop last space */
	buf[cnt] = '\0';
	return cnt;
}
EXPORT_SYMBOL_GPL(mei_fw_status2str);

/**
 * mei_cancel_work - Cancel mei background jobs
 *
 * @dev: the device structure
 */
void mei_cancel_work(struct mei_device *dev)
{
	cancel_work_sync(&dev->init_work);
	cancel_work_sync(&dev->reset_work);

	cancel_delayed_work(&dev->timer_work);
}
EXPORT_SYMBOL_GPL(mei_cancel_work);

/**
 * mei_reset - resets host and fw.
 *
 * @dev: the device structure
 *
 * Return: 0 on success or < 0 if the reset hasn't succeeded
 */
int mei_reset(struct mei_device *dev)
{
	enum mei_dev_state state = dev->dev_state;
	bool interrupts_enabled;
	int ret;

	if (state != MEI_DEV_INITIALIZING &&
	    state != MEI_DEV_DISABLED &&
	    state != MEI_DEV_POWER_DOWN &&
	    state != MEI_DEV_POWER_UP) {
		char fw_sts_str[MEI_FW_STATUS_STR_SZ];

		mei_fw_status_str(dev, fw_sts_str, MEI_FW_STATUS_STR_SZ);
		dev_warn(dev->dev, "unexpected reset: dev_state = %s fw status = %s\n",
			 mei_dev_state_str(state), fw_sts_str);
	}

	/* we're already in reset, cancel the init timer
	 * if the reset was called due the hbm protocol error
	 * we need to call it before hw start
	 * so the hbm watchdog won't kick in
	 */
	mei_hbm_idle(dev);

	/* enter reset flow */
	interrupts_enabled = state != MEI_DEV_POWER_DOWN;
	dev->dev_state = MEI_DEV_RESETTING;

	dev->reset_count++;
	if (dev->reset_count > MEI_MAX_CONSEC_RESET) {
		dev_err(dev->dev, "reset: reached maximal consecutive resets: disabling the device\n");
		dev->dev_state = MEI_DEV_DISABLED;
		return -ENODEV;
	}

	ret = mei_hw_reset(dev, interrupts_enabled);
	/* fall through and remove the sw state even if hw reset has failed */

	/* no need to clean up software state in case of power up */
	if (state != MEI_DEV_INITIALIZING &&
	    state != MEI_DEV_POWER_UP) {

		/* remove all waiting requests */
		mei_cl_all_write_clear(dev);

		mei_cl_all_disconnect(dev);

		/* wake up all readers and writers so they can be interrupted */
		mei_cl_all_wakeup(dev);

		/* remove entry if already in list */
		dev_dbg(dev->dev, "remove iamthif and wd from the file list.\n");
		mei_cl_unlink(&dev->wd_cl);
		mei_cl_unlink(&dev->iamthif_cl);
		mei_amthif_reset_params(dev);
	}

	mei_hbm_reset(dev);

	dev->rd_msg_hdr = 0;
	dev->wd_pending = false;

	if (ret) {
		dev_err(dev->dev, "hw_reset failed ret = %d\n", ret);
		return ret;
	}

	if (state == MEI_DEV_POWER_DOWN) {
		dev_dbg(dev->dev, "powering down: end of reset\n");
		dev->dev_state = MEI_DEV_DISABLED;
		return 0;
	}

	ret = mei_hw_start(dev);
	if (ret) {
		dev_err(dev->dev, "hw_start failed ret = %d\n", ret);
		return ret;
	}

	dev_dbg(dev->dev, "link is established start sending messages.\n");

	dev->dev_state = MEI_DEV_INIT_CLIENTS;
	ret = mei_hbm_start_req(dev);
	if (ret) {
		dev_err(dev->dev, "hbm_start failed ret = %d\n", ret);
		dev->dev_state = MEI_DEV_RESETTING;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mei_reset);

/**
 * mei_start - initializes host and fw to start work.
 *
 * @dev: the device structure
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_start(struct mei_device *dev)
{
	int ret;

	mutex_lock(&dev->device_lock);

	/* acknowledge interrupt and stop interrupts */
	mei_clear_interrupts(dev);

	mei_hw_config(dev);

	dev_dbg(dev->dev, "reset in start the mei device.\n");

	dev->reset_count = 0;
	do {
		dev->dev_state = MEI_DEV_INITIALIZING;
		ret = mei_reset(dev);

		if (ret == -ENODEV || dev->dev_state == MEI_DEV_DISABLED) {
			dev_err(dev->dev, "reset failed ret = %d", ret);
			goto err;
		}
	} while (ret);

	/* we cannot start the device w/o hbm start message completed */
	if (dev->dev_state == MEI_DEV_DISABLED) {
		dev_err(dev->dev, "reset failed");
		goto err;
	}

	if (mei_hbm_start_wait(dev)) {
		dev_err(dev->dev, "HBM haven't started");
		goto err;
	}

	if (!mei_host_is_ready(dev)) {
		dev_err(dev->dev, "host is not ready.\n");
		goto err;
	}

	if (!mei_hw_is_ready(dev)) {
		dev_err(dev->dev, "ME is not ready.\n");
		goto err;
	}

	if (!mei_hbm_version_is_supported(dev)) {
		dev_dbg(dev->dev, "MEI start failed.\n");
		goto err;
	}

	dev_dbg(dev->dev, "link layer has been established.\n");

	mutex_unlock(&dev->device_lock);
	return 0;
err:
	dev_err(dev->dev, "link layer initialization failed.\n");
	dev->dev_state = MEI_DEV_DISABLED;
	mutex_unlock(&dev->device_lock);
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(mei_start);

/**
 * mei_restart - restart device after suspend
 *
 * @dev: the device structure
 *
 * Return: 0 on success or -ENODEV if the restart hasn't succeeded
 */
int mei_restart(struct mei_device *dev)
{
	int err;

	mutex_lock(&dev->device_lock);

	mei_clear_interrupts(dev);

	dev->dev_state = MEI_DEV_POWER_UP;
	dev->reset_count = 0;

	err = mei_reset(dev);

	mutex_unlock(&dev->device_lock);

	if (err == -ENODEV || dev->dev_state == MEI_DEV_DISABLED) {
		dev_err(dev->dev, "device disabled = %d\n", err);
		return -ENODEV;
	}

	/* try to start again */
	if (err)
		schedule_work(&dev->reset_work);


	return 0;
}
EXPORT_SYMBOL_GPL(mei_restart);

static void mei_reset_work(struct work_struct *work)
{
	struct mei_device *dev =
		container_of(work, struct mei_device,  reset_work);
	int ret;

	mutex_lock(&dev->device_lock);

	ret = mei_reset(dev);

	mutex_unlock(&dev->device_lock);

	if (dev->dev_state == MEI_DEV_DISABLED) {
		dev_err(dev->dev, "device disabled = %d\n", ret);
		return;
	}

	/* retry reset in case of failure */
	if (ret)
		schedule_work(&dev->reset_work);
}

void mei_stop(struct mei_device *dev)
{
	dev_dbg(dev->dev, "stopping the device.\n");

	mei_cancel_work(dev);

	mei_nfc_host_exit(dev);

	mei_cl_bus_remove_devices(dev);

	mutex_lock(&dev->device_lock);

	mei_wd_stop(dev);

	dev->dev_state = MEI_DEV_POWER_DOWN;
	mei_reset(dev);
	/* move device to disabled state unconditionally */
	dev->dev_state = MEI_DEV_DISABLED;

	mutex_unlock(&dev->device_lock);

	mei_watchdog_unregister(dev);
}
EXPORT_SYMBOL_GPL(mei_stop);

/**
 * mei_write_is_idle - check if the write queues are idle
 *
 * @dev: the device structure
 *
 * Return: true of there is no pending write
 */
bool mei_write_is_idle(struct mei_device *dev)
{
	bool idle = (dev->dev_state == MEI_DEV_ENABLED &&
		list_empty(&dev->ctrl_wr_list.list) &&
		list_empty(&dev->write_list.list));

	dev_dbg(dev->dev, "write pg: is idle[%d] state=%s ctrl=%d write=%d\n",
		idle,
		mei_dev_state_str(dev->dev_state),
		list_empty(&dev->ctrl_wr_list.list),
		list_empty(&dev->write_list.list));

	return idle;
}
EXPORT_SYMBOL_GPL(mei_write_is_idle);

/**
 * mei_device_init  -- initialize mei_device structure
 *
 * @dev: the mei device
 * @device: the device structure
 * @hw_ops: hw operations
 */
void mei_device_init(struct mei_device *dev,
		     struct device *device,
		     const struct mei_hw_ops *hw_ops)
{
	/* setup our list array */
	INIT_LIST_HEAD(&dev->file_list);
	INIT_LIST_HEAD(&dev->device_list);
	INIT_LIST_HEAD(&dev->me_clients);
	mutex_init(&dev->device_lock);
	init_waitqueue_head(&dev->wait_hw_ready);
	init_waitqueue_head(&dev->wait_pg);
	init_waitqueue_head(&dev->wait_hbm_start);
	init_waitqueue_head(&dev->wait_stop_wd);
	dev->dev_state = MEI_DEV_INITIALIZING;
	dev->reset_count = 0;

	mei_io_list_init(&dev->read_list);
	mei_io_list_init(&dev->write_list);
	mei_io_list_init(&dev->write_waiting_list);
	mei_io_list_init(&dev->ctrl_wr_list);
	mei_io_list_init(&dev->ctrl_rd_list);

	INIT_DELAYED_WORK(&dev->timer_work, mei_timer);
	INIT_WORK(&dev->init_work, mei_host_client_init);
	INIT_WORK(&dev->reset_work, mei_reset_work);

	INIT_LIST_HEAD(&dev->wd_cl.link);
	INIT_LIST_HEAD(&dev->iamthif_cl.link);
	mei_io_list_init(&dev->amthif_cmd_list);
	mei_io_list_init(&dev->amthif_rd_complete_list);

	bitmap_zero(dev->host_clients_map, MEI_CLIENTS_MAX);
	dev->open_handle_count = 0;

	/*
	 * Reserving the first client ID
	 * 0: Reserved for MEI Bus Message communications
	 */
	bitmap_set(dev->host_clients_map, 0, 1);

	dev->pg_event = MEI_PG_EVENT_IDLE;
	dev->ops      = hw_ops;
	dev->dev      = device;
}
EXPORT_SYMBOL_GPL(mei_device_init);

