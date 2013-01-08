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

#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "client.h"

const char *mei_dev_state_str(int state)
{
#define MEI_DEV_STATE(state) case MEI_DEV_##state: return #state
	switch (state) {
	MEI_DEV_STATE(INITIALIZING);
	MEI_DEV_STATE(INIT_CLIENTS);
	MEI_DEV_STATE(ENABLED);
	MEI_DEV_STATE(RESETING);
	MEI_DEV_STATE(DISABLED);
	MEI_DEV_STATE(RECOVERING_FROM_RESET);
	MEI_DEV_STATE(POWER_DOWN);
	MEI_DEV_STATE(POWER_UP);
	default:
		return "unkown";
	}
#undef MEI_DEV_STATE
}




/**
 * init_mei_device - allocates and initializes the mei device structure
 *
 * @pdev: The pci device structure
 *
 * returns The mei_device_device pointer on success, NULL on failure.
 */
struct mei_device *mei_device_init(struct pci_dev *pdev)
{
	struct mei_device *dev;

	dev = kzalloc(sizeof(struct mei_device), GFP_KERNEL);
	if (!dev)
		return NULL;

	/* setup our list array */
	INIT_LIST_HEAD(&dev->file_list);
	INIT_LIST_HEAD(&dev->wd_cl.link);
	INIT_LIST_HEAD(&dev->iamthif_cl.link);
	mutex_init(&dev->device_lock);
	init_waitqueue_head(&dev->wait_recvd_msg);
	init_waitqueue_head(&dev->wait_stop_wd);
	dev->dev_state = MEI_DEV_INITIALIZING;

	mei_io_list_init(&dev->read_list);
	mei_io_list_init(&dev->write_list);
	mei_io_list_init(&dev->write_waiting_list);
	mei_io_list_init(&dev->ctrl_wr_list);
	mei_io_list_init(&dev->ctrl_rd_list);
	mei_io_list_init(&dev->amthif_cmd_list);
	mei_io_list_init(&dev->amthif_rd_complete_list);
	dev->pdev = pdev;
	return dev;
}

/**
 * mei_hw_init - initializes host and fw to start work.
 *
 * @dev: the device structure
 *
 * returns 0 on success, <0 on failure.
 */
int mei_hw_init(struct mei_device *dev)
{
	int err = 0;
	int ret;

	mutex_lock(&dev->device_lock);

	dev->host_hw_state = mei_hcsr_read(dev);
	dev->me_hw_state = mei_mecsr_read(dev);
	dev_dbg(&dev->pdev->dev, "host_hw_state = 0x%08x, mestate = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	/* acknowledge interrupt and stop interupts */
	mei_clear_interrupts(dev);

	/* Doesn't change in runtime */
	dev->hbuf_depth = (dev->host_hw_state & H_CBD) >> 24;

	dev->recvd_msg = false;
	dev_dbg(&dev->pdev->dev, "reset in start the mei device.\n");

	mei_reset(dev, 1);

	dev_dbg(&dev->pdev->dev, "host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	/* wait for ME to turn on ME_RDY */
	if (!dev->recvd_msg) {
		mutex_unlock(&dev->device_lock);
		err = wait_event_interruptible_timeout(dev->wait_recvd_msg,
			dev->recvd_msg,
			mei_secs_to_jiffies(MEI_INTEROP_TIMEOUT));
		mutex_lock(&dev->device_lock);
	}

	if (err <= 0 && !dev->recvd_msg) {
		dev->dev_state = MEI_DEV_DISABLED;
		dev_dbg(&dev->pdev->dev,
			"wait_event_interruptible_timeout failed"
			"on wait for ME to turn on ME_RDY.\n");
		ret = -ENODEV;
		goto out;
	}

	if (!(((dev->host_hw_state & H_RDY) == H_RDY) &&
	      ((dev->me_hw_state & ME_RDY_HRA) == ME_RDY_HRA))) {
		dev->dev_state = MEI_DEV_DISABLED;
		dev_dbg(&dev->pdev->dev,
			"host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
			dev->host_hw_state, dev->me_hw_state);

		if (!(dev->host_hw_state & H_RDY))
			dev_dbg(&dev->pdev->dev, "host turn off H_RDY.\n");

		if (!(dev->me_hw_state & ME_RDY_HRA))
			dev_dbg(&dev->pdev->dev, "ME turn off ME_RDY.\n");

		dev_err(&dev->pdev->dev, "link layer initialization failed.\n");
		ret = -ENODEV;
		goto out;
	}

	if (dev->version.major_version != HBM_MAJOR_VERSION ||
	    dev->version.minor_version != HBM_MINOR_VERSION) {
		dev_dbg(&dev->pdev->dev, "MEI start failed.\n");
		ret = -ENODEV;
		goto out;
	}

	dev->recvd_msg = false;
	dev_dbg(&dev->pdev->dev, "host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);
	dev_dbg(&dev->pdev->dev, "ME turn on ME_RDY and host turn on H_RDY.\n");
	dev_dbg(&dev->pdev->dev, "link layer has been established.\n");
	dev_dbg(&dev->pdev->dev, "MEI  start success.\n");
	ret = 0;

out:
	mutex_unlock(&dev->device_lock);
	return ret;
}

/**
 * mei_hw_reset - resets fw via mei csr register.
 *
 * @dev: the device structure
 * @interrupts_enabled: if interrupt should be enabled after reset.
 */
static void mei_hw_reset(struct mei_device *dev, int interrupts_enabled)
{
	dev->host_hw_state |= (H_RST | H_IG);

	if (interrupts_enabled)
		mei_enable_interrupts(dev);
	else
		mei_disable_interrupts(dev);
}

/**
 * mei_reset - resets host and fw.
 *
 * @dev: the device structure
 * @interrupts_enabled: if interrupt should be enabled after reset.
 */
void mei_reset(struct mei_device *dev, int interrupts_enabled)
{
	struct mei_cl *cl_pos = NULL;
	struct mei_cl *cl_next = NULL;
	struct mei_cl_cb *cb_pos = NULL;
	struct mei_cl_cb *cb_next = NULL;
	bool unexpected;

	if (dev->dev_state == MEI_DEV_RECOVERING_FROM_RESET)
		return;

	unexpected = (dev->dev_state != MEI_DEV_INITIALIZING &&
			dev->dev_state != MEI_DEV_DISABLED &&
			dev->dev_state != MEI_DEV_POWER_DOWN &&
			dev->dev_state != MEI_DEV_POWER_UP);

	dev->host_hw_state = mei_hcsr_read(dev);

	dev_dbg(&dev->pdev->dev, "before reset host_hw_state = 0x%08x.\n",
	    dev->host_hw_state);

	mei_hw_reset(dev, interrupts_enabled);

	dev->host_hw_state &= ~H_RST;
	dev->host_hw_state |= H_IG;

	mei_hcsr_set(dev);

	dev_dbg(&dev->pdev->dev, "currently saved host_hw_state = 0x%08x.\n",
	    dev->host_hw_state);

	if (dev->dev_state != MEI_DEV_INITIALIZING) {
		if (dev->dev_state != MEI_DEV_DISABLED &&
		    dev->dev_state != MEI_DEV_POWER_DOWN)
			dev->dev_state = MEI_DEV_RESETING;

		list_for_each_entry_safe(cl_pos,
				cl_next, &dev->file_list, link) {
			cl_pos->state = MEI_FILE_DISCONNECTED;
			cl_pos->mei_flow_ctrl_creds = 0;
			cl_pos->read_cb = NULL;
			cl_pos->timer_count = 0;
		}
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

	/* update the state of the registers after reset */
	dev->host_hw_state = mei_hcsr_read(dev);
	dev->me_hw_state = mei_mecsr_read(dev);

	dev_dbg(&dev->pdev->dev, "after reset host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	if (unexpected)
		dev_warn(&dev->pdev->dev, "unexpected reset: dev_state = %s\n",
			 mei_dev_state_str(dev->dev_state));

	/* Wake up all readings so they can be interrupted */
	list_for_each_entry_safe(cl_pos, cl_next, &dev->file_list, link) {
		if (waitqueue_active(&cl_pos->rx_wait)) {
			dev_dbg(&dev->pdev->dev, "Waking up client!\n");
			wake_up_interruptible(&cl_pos->rx_wait);
		}
	}
	/* remove all waiting requests */
	list_for_each_entry_safe(cb_pos, cb_next, &dev->write_list.list, list) {
		list_del(&cb_pos->list);
		mei_io_cb_free(cb_pos);
	}
}




