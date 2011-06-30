/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2011, Intel Corporation.
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include "mei_dev.h"
#include "hw.h"
#include "interface.h"
#include "mei.h"

/*
 * MEI Watchdog Module Parameters
 */
static u16 watchdog_timeout = AMT_WD_VALUE;
module_param(watchdog_timeout, ushort, 0);
MODULE_PARM_DESC(watchdog_timeout,
		"Intel(R) AMT Watchdog timeout value in seconds. (default="
					__MODULE_STRING(AMT_WD_VALUE)
					", disable=0)");

static const u8 mei_start_wd_params[] = { 0x02, 0x12, 0x13, 0x10 };
static const u8 mei_stop_wd_params[] = { 0x02, 0x02, 0x14, 0x10 };

const u8 mei_wd_state_independence_msg[3][4] = {
	{0x05, 0x02, 0x51, 0x10},
	{0x05, 0x02, 0x52, 0x10},
	{0x07, 0x02, 0x01, 0x10}
};

/* UUIDs for AMT F/W clients */
const uuid_le mei_wd_guid = UUID_LE(0x05B79A6F, 0x4628, 0x4D7F, 0x89,
						0x9D, 0xA9, 0x15, 0x14, 0xCB,
						0x32, 0xAB);


void mei_wd_start_setup(struct mei_device *dev)
{
	dev_dbg(&dev->pdev->dev, "dev->wd_timeout=%d.\n", dev->wd_timeout);
	memcpy(dev->wd_data, mei_start_wd_params, MEI_WD_PARAMS_SIZE);
	memcpy(dev->wd_data + MEI_WD_PARAMS_SIZE,
		&dev->wd_timeout, sizeof(u16));
}

/**
 * host_init_wd - mei initialization wd.
 *
 * @dev: the device structure
 */
void mei_wd_host_init(struct mei_device *dev)
{
	mei_init_file_private(&dev->wd_cl, dev);

	/* look for WD client and connect to it */
	dev->wd_cl.state = MEI_FILE_DISCONNECTED;
	dev->wd_timeout = watchdog_timeout;

	if (dev->wd_timeout > 0) {
		mei_wd_start_setup(dev);
		/* find ME WD client */
		mei_find_me_client_update_filext(dev, &dev->wd_cl,
					&mei_wd_guid, MEI_WD_HOST_CLIENT_ID);

		dev_dbg(&dev->pdev->dev, "check wd_cl\n");
		if (MEI_FILE_CONNECTING == dev->wd_cl.state) {
			if (!mei_connect(dev, &dev->wd_cl)) {
				dev_dbg(&dev->pdev->dev, "Failed to connect to WD client\n");
				dev->wd_cl.state = MEI_FILE_DISCONNECTED;
				dev->wd_cl.host_client_id = 0;
				host_init_iamthif(dev) ;
			} else {
				dev->wd_cl.timer_count = CONNECT_TIMEOUT;
			}
		} else {
			dev_dbg(&dev->pdev->dev, "Failed to find WD client\n");
			host_init_iamthif(dev) ;
		}
	} else {
		dev->wd_bypass = true;
		dev_dbg(&dev->pdev->dev, "WD requested to be disabled\n");
		host_init_iamthif(dev) ;
	}
}

/**
 * mei_wd_send - sends watch dog message to fw.
 *
 * @dev: the device structure
 *
 * returns 0 if success,
 *	-EIO when message send fails
 *	-EINVAL when invalid message is to be sent
 */
int mei_wd_send(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr;

	mei_hdr = (struct mei_msg_hdr *) &dev->wr_msg_buf[0];
	mei_hdr->host_addr = dev->wd_cl.host_client_id;
	mei_hdr->me_addr = dev->wd_cl.me_client_id;
	mei_hdr->msg_complete = 1;
	mei_hdr->reserved = 0;

	if (!memcmp(dev->wd_data, mei_start_wd_params, MEI_WD_PARAMS_SIZE))
		mei_hdr->length = MEI_START_WD_DATA_SIZE;
	else if (!memcmp(dev->wd_data, mei_stop_wd_params, MEI_WD_PARAMS_SIZE))
		mei_hdr->length = MEI_WD_PARAMS_SIZE;
	else
		return -EINVAL;

	if (mei_write_message(dev, mei_hdr, dev->wd_data, mei_hdr->length))
		return 0;
	return -EIO;
}

int mei_wd_stop(struct mei_device *dev, bool preserve)
{
	int ret;
	u16 wd_timeout = dev->wd_timeout;

	cancel_delayed_work(&dev->wd_work);
	if (dev->wd_cl.state != MEI_FILE_CONNECTED || !dev->wd_timeout)
		return 0;

	dev->wd_timeout = 0;
	dev->wd_due_counter = 0;
	memcpy(dev->wd_data, mei_stop_wd_params, MEI_WD_PARAMS_SIZE);
	dev->stop = 1;

	ret = mei_flow_ctrl_creds(dev, &dev->wd_cl);
	if (ret < 0)
		goto out;

	if (ret && dev->mei_host_buffer_is_empty) {
		ret = 0;
		dev->mei_host_buffer_is_empty = 0;

		if (!mei_wd_send(dev)) {
			ret = mei_flow_ctrl_reduce(dev, &dev->wd_cl);
			if (ret)
				goto out;
		} else {
			dev_dbg(&dev->pdev->dev, "send stop WD failed\n");
		}

		dev->wd_pending = 0;
	} else {
		dev->wd_pending = 1;
	}
	dev->wd_stopped = 0;
	mutex_unlock(&dev->device_lock);

	ret = wait_event_interruptible_timeout(dev->wait_stop_wd,
					dev->wd_stopped, 10 * HZ);
	mutex_lock(&dev->device_lock);
	if (dev->wd_stopped) {
		dev_dbg(&dev->pdev->dev, "stop wd complete ret=%d.\n", ret);
		ret = 0;
	} else {
		if (!ret)
			ret = -ETIMEDOUT;
		dev_warn(&dev->pdev->dev,
			"stop wd failed to complete ret=%d.\n", ret);
	}

	if (preserve)
		dev->wd_timeout = wd_timeout;

out:
	return ret;
}

