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
#include <linux/pci.h>
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
		return "unkown";
	}
#undef MEI_DEV_STATE
}

void mei_device_init(struct mei_device *dev)
{
	/* setup our list array */
	INIT_LIST_HEAD(&dev->file_list);
	INIT_LIST_HEAD(&dev->device_list);
	mutex_init(&dev->device_lock);
	init_waitqueue_head(&dev->wait_hw_ready);
	init_waitqueue_head(&dev->wait_recvd_msg);
	init_waitqueue_head(&dev->wait_stop_wd);
	dev->dev_state = MEI_DEV_INITIALIZING;

	mei_io_list_init(&dev->read_list);
	mei_io_list_init(&dev->write_list);
	mei_io_list_init(&dev->write_waiting_list);
	mei_io_list_init(&dev->ctrl_wr_list);
	mei_io_list_init(&dev->ctrl_rd_list);

	INIT_DELAYED_WORK(&dev->timer_work, mei_timer);
	INIT_WORK(&dev->init_work, mei_host_client_init);

	INIT_LIST_HEAD(&dev->wd_cl.link);
	INIT_LIST_HEAD(&dev->iamthif_cl.link);
	mei_io_list_init(&dev->amthif_cmd_list);
	mei_io_list_init(&dev->amthif_rd_complete_list);

}
EXPORT_SYMBOL_GPL(mei_device_init);

/**
 * mei_start - initializes host and fw to start work.
 *
 * @dev: the device structure
 *
 * returns 0 on success, <0 on failure.
 */
int mei_start(struct mei_device *dev)
{
	mutex_lock(&dev->device_lock);

	/* acknowledge interrupt and stop interupts */
	mei_clear_interrupts(dev);

	mei_hw_config(dev);

	dev_dbg(&dev->pdev->dev, "reset in start the mei device.\n");

	mei_reset(dev, 1);

	if (mei_hbm_start_wait(dev)) {
		dev_err(&dev->pdev->dev, "HBM haven't started");
		goto err;
	}

	if (!mei_host_is_ready(dev)) {
		dev_err(&dev->pdev->dev, "host is not ready.\n");
		goto err;
	}

	if (!mei_hw_is_ready(dev)) {
		dev_err(&dev->pdev->dev, "ME is not ready.\n");
		goto err;
	}

	if (!mei_hbm_version_is_supported(dev)) {
		dev_dbg(&dev->pdev->dev, "MEI start failed.\n");
		goto err;
	}

	dev_dbg(&dev->pdev->dev, "link layer has been established.\n");

	mutex_unlock(&dev->device_lock);
	return 0;
err:
	dev_err(&dev->pdev->dev, "link layer initialization failed.\n");
	dev->dev_state = MEI_DEV_DISABLED;
	mutex_unlock(&dev->device_lock);
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(mei_start);

/**
 * mei_reset - resets host and fw.
 *
 * @dev: the device structure
 * @interrupts_enabled: if interrupt should be enabled after reset.
 */
void mei_reset(struct mei_device *dev, int interrupts_enabled)
{
	bool unexpected;
	int ret;

	unexpected = (dev->dev_state != MEI_DEV_INITIALIZING &&
			dev->dev_state != MEI_DEV_DISABLED &&
			dev->dev_state != MEI_DEV_POWER_DOWN &&
			dev->dev_state != MEI_DEV_POWER_UP);

	ret = mei_hw_reset(dev, interrupts_enabled);
	if (ret) {
		dev_err(&dev->pdev->dev, "hw reset failed disabling the device\n");
		interrupts_enabled = false;
		dev->dev_state = MEI_DEV_DISABLED;
	}

	dev->hbm_state = MEI_HBM_IDLE;

	if (dev->dev_state != MEI_DEV_INITIALIZING) {
		if (dev->dev_state != MEI_DEV_DISABLED &&
		    dev->dev_state != MEI_DEV_POWER_DOWN)
			dev->dev_state = MEI_DEV_RESETTING;

		mei_cl_all_disconnect(dev);

		/* remove entry if already in list */
		dev_dbg(&dev->pdev->dev, "remove iamthif and wd from the file list.\n");
		mei_cl_unlink(&dev->wd_cl);
		if (dev->open_handle_count > 0)
			dev->open_handle_count--;
		mei_cl_unlink(&dev->iamthif_cl);
		if (dev->open_handle_count > 0)
			dev->open_handle_count--;

		mei_amthif_reset_params(dev);
		memset(&dev->wr_ext_msg, 0, sizeof(dev->wr_ext_msg));
	}

	dev->me_clients_num = 0;
	dev->rd_msg_hdr = 0;
	dev->wd_pending = false;

	if (unexpected)
		dev_warn(&dev->pdev->dev, "unexpected reset: dev_state = %s\n",
			 mei_dev_state_str(dev->dev_state));

	if (!interrupts_enabled) {
		dev_dbg(&dev->pdev->dev, "intr not enabled end of reset\n");
		return;
	}

	mei_hw_start(dev);

	dev_dbg(&dev->pdev->dev, "link is established start sending messages.\n");
	/* link is established * start sending messages.  */

	dev->dev_state = MEI_DEV_INIT_CLIENTS;

	mei_hbm_start_req(dev);

	/* wake up all readings so they can be interrupted */
	mei_cl_all_read_wakeup(dev);

	/* remove all waiting requests */
	mei_cl_all_write_clear(dev);
}
EXPORT_SYMBOL_GPL(mei_reset);

void mei_stop(struct mei_device *dev)
{
	dev_dbg(&dev->pdev->dev, "stopping the device.\n");

	flush_scheduled_work();

	mutex_lock(&dev->device_lock);

	cancel_delayed_work(&dev->timer_work);

	mei_wd_stop(dev);

	mei_nfc_host_exit();

	dev->dev_state = MEI_DEV_POWER_DOWN;
	mei_reset(dev, 0);

	mutex_unlock(&dev->device_lock);

	mei_watchdog_unregister(dev);
}
EXPORT_SYMBOL_GPL(mei_stop);



