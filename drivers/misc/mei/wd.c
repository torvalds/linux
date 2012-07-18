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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/watchdog.h>

#include "mei_dev.h"
#include "hw.h"
#include "interface.h"
#include <linux/mei.h>

static const u8 mei_start_wd_params[] = { 0x02, 0x12, 0x13, 0x10 };
static const u8 mei_stop_wd_params[] = { 0x02, 0x02, 0x14, 0x10 };

const u8 mei_wd_state_independence_msg[3][4] = {
	{0x05, 0x02, 0x51, 0x10},
	{0x05, 0x02, 0x52, 0x10},
	{0x07, 0x02, 0x01, 0x10}
};

/*
 * AMT Watchdog Device
 */
#define INTEL_AMT_WATCHDOG_ID "INTCAMT"

/* UUIDs for AMT F/W clients */
const uuid_le mei_wd_guid = UUID_LE(0x05B79A6F, 0x4628, 0x4D7F, 0x89,
						0x9D, 0xA9, 0x15, 0x14, 0xCB,
						0x32, 0xAB);

static void mei_wd_set_start_timeout(struct mei_device *dev, u16 timeout)
{
	dev_dbg(&dev->pdev->dev, "wd: set timeout=%d.\n", timeout);
	memcpy(dev->wd_data, mei_start_wd_params, MEI_WD_PARAMS_SIZE);
	memcpy(dev->wd_data + MEI_WD_PARAMS_SIZE, &timeout, sizeof(u16));
}

/**
 * host_init_wd - mei initialization wd.
 *
 * @dev: the device structure
 * returns -ENENT if wd client cannot be found
 *         -EIO if write has failed
 */
int mei_wd_host_init(struct mei_device *dev)
{
	mei_cl_init(&dev->wd_cl, dev);

	/* look for WD client and connect to it */
	dev->wd_cl.state = MEI_FILE_DISCONNECTED;
	dev->wd_timeout = AMT_WD_DEFAULT_TIMEOUT;

	/* find ME WD client */
	mei_find_me_client_update_filext(dev, &dev->wd_cl,
				&mei_wd_guid, MEI_WD_HOST_CLIENT_ID);

	dev_dbg(&dev->pdev->dev, "wd: check client\n");
	if (MEI_FILE_CONNECTING != dev->wd_cl.state) {
		dev_info(&dev->pdev->dev, "wd: failed to find the client\n");
		return -ENOENT;
	}

	if (mei_connect(dev, &dev->wd_cl)) {
		dev_err(&dev->pdev->dev, "wd: failed to connect to the client\n");
		dev->wd_cl.state = MEI_FILE_DISCONNECTED;
		dev->wd_cl.host_client_id = 0;
		return -EIO;
	}
	dev->wd_cl.timer_count = CONNECT_TIMEOUT;

	return 0;
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

	return mei_write_message(dev, mei_hdr, dev->wd_data, mei_hdr->length);
}

/**
 * mei_wd_stop - sends watchdog stop message to fw.
 *
 * @dev: the device structure
 * @preserve: indicate if to keep the timeout value
 *
 * returns 0 if success,
 *	-EIO when message send fails
 *	-EINVAL when invalid message is to be sent
 */
int mei_wd_stop(struct mei_device *dev, bool preserve)
{
	int ret;
	u16 wd_timeout = dev->wd_timeout;

	cancel_delayed_work(&dev->timer_work);
	if (dev->wd_cl.state != MEI_FILE_CONNECTED || !dev->wd_timeout)
		return 0;

	dev->wd_timeout = 0;
	dev->wd_due_counter = 0;
	memcpy(dev->wd_data, mei_stop_wd_params, MEI_WD_PARAMS_SIZE);
	dev->stop = true;

	ret = mei_flow_ctrl_creds(dev, &dev->wd_cl);
	if (ret < 0)
		goto out;

	if (ret && dev->mei_host_buffer_is_empty) {
		ret = 0;
		dev->mei_host_buffer_is_empty = false;

		if (!mei_wd_send(dev)) {
			ret = mei_flow_ctrl_reduce(dev, &dev->wd_cl);
			if (ret)
				goto out;
		} else {
			dev_err(&dev->pdev->dev, "wd: send stop failed\n");
		}

		dev->wd_pending = false;
	} else {
		dev->wd_pending = true;
	}
	dev->wd_stopped = false;
	mutex_unlock(&dev->device_lock);

	ret = wait_event_interruptible_timeout(dev->wait_stop_wd,
					dev->wd_stopped, 10 * HZ);
	mutex_lock(&dev->device_lock);
	if (dev->wd_stopped) {
		dev_dbg(&dev->pdev->dev, "wd: stop completed ret=%d.\n", ret);
		ret = 0;
	} else {
		if (!ret)
			ret = -ETIMEDOUT;
		dev_warn(&dev->pdev->dev,
			"wd: stop failed to complete ret=%d.\n", ret);
	}

	if (preserve)
		dev->wd_timeout = wd_timeout;

out:
	return ret;
}

/*
 * mei_wd_ops_start - wd start command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 *
 * returns 0 if success, negative errno code for failure
 */
static int mei_wd_ops_start(struct watchdog_device *wd_dev)
{
	int err = -ENODEV;
	struct mei_device *dev;

	dev = pci_get_drvdata(mei_device);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (dev->mei_state != MEI_ENABLED) {
		dev_dbg(&dev->pdev->dev,
			"wd: mei_state != MEI_ENABLED  mei_state = %d\n",
			dev->mei_state);
		goto end_unlock;
	}

	if (dev->wd_cl.state != MEI_FILE_CONNECTED)	{
		dev_dbg(&dev->pdev->dev,
			"MEI Driver is not connected to Watchdog Client\n");
		goto end_unlock;
	}

	mei_wd_set_start_timeout(dev, dev->wd_timeout);

	err = 0;
end_unlock:
	mutex_unlock(&dev->device_lock);
	return err;
}

/*
 * mei_wd_ops_stop -  wd stop command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 *
 * returns 0 if success, negative errno code for failure
 */
static int mei_wd_ops_stop(struct watchdog_device *wd_dev)
{
	struct mei_device *dev;
	dev = pci_get_drvdata(mei_device);

	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);
	mei_wd_stop(dev, false);
	mutex_unlock(&dev->device_lock);

	return 0;
}

/*
 * mei_wd_ops_ping - wd ping command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 *
 * returns 0 if success, negative errno code for failure
 */
static int mei_wd_ops_ping(struct watchdog_device *wd_dev)
{
	int ret = 0;
	struct mei_device *dev;
	dev = pci_get_drvdata(mei_device);

	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (dev->wd_cl.state != MEI_FILE_CONNECTED) {
		dev_err(&dev->pdev->dev, "wd: not connected.\n");
		ret = -ENODEV;
		goto end;
	}

	/* Check if we can send the ping to HW*/
	if (dev->mei_host_buffer_is_empty &&
		mei_flow_ctrl_creds(dev, &dev->wd_cl) > 0) {

		dev->mei_host_buffer_is_empty = false;
		dev_dbg(&dev->pdev->dev, "wd: sending ping\n");

		if (mei_wd_send(dev)) {
			dev_err(&dev->pdev->dev, "wd: send failed.\n");
			ret = -EIO;
			goto end;
		}

		if (mei_flow_ctrl_reduce(dev, &dev->wd_cl)) {
			dev_err(&dev->pdev->dev,
				"wd: mei_flow_ctrl_reduce() failed.\n");
			ret = -EIO;
			goto end;
		}

	} else {
		dev->wd_pending = true;
	}

end:
	mutex_unlock(&dev->device_lock);
	return ret;
}

/*
 * mei_wd_ops_set_timeout - wd set timeout command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 * @timeout - timeout value to set
 *
 * returns 0 if success, negative errno code for failure
 */
static int mei_wd_ops_set_timeout(struct watchdog_device *wd_dev, unsigned int timeout)
{
	struct mei_device *dev;
	dev = pci_get_drvdata(mei_device);

	if (!dev)
		return -ENODEV;

	/* Check Timeout value */
	if (timeout < AMT_WD_MIN_TIMEOUT || timeout > AMT_WD_MAX_TIMEOUT)
		return -EINVAL;

	mutex_lock(&dev->device_lock);

	dev->wd_timeout = timeout;
	wd_dev->timeout = timeout;
	mei_wd_set_start_timeout(dev, dev->wd_timeout);

	mutex_unlock(&dev->device_lock);

	return 0;
}

/*
 * Watchdog Device structs
 */
static const struct watchdog_ops wd_ops = {
		.owner = THIS_MODULE,
		.start = mei_wd_ops_start,
		.stop = mei_wd_ops_stop,
		.ping = mei_wd_ops_ping,
		.set_timeout = mei_wd_ops_set_timeout,
};
static const struct watchdog_info wd_info = {
		.identity = INTEL_AMT_WATCHDOG_ID,
		.options = WDIOF_KEEPALIVEPING | WDIOF_ALARMONLY,
};

static struct watchdog_device amt_wd_dev = {
		.info = &wd_info,
		.ops = &wd_ops,
		.timeout = AMT_WD_DEFAULT_TIMEOUT,
		.min_timeout = AMT_WD_MIN_TIMEOUT,
		.max_timeout = AMT_WD_MAX_TIMEOUT,
};


void  mei_watchdog_register(struct mei_device *dev)
{
	dev_dbg(&dev->pdev->dev, "dev->wd_timeout =%d.\n", dev->wd_timeout);

	dev->wd_due_counter = !!dev->wd_timeout;

	if (watchdog_register_device(&amt_wd_dev)) {
		dev_err(&dev->pdev->dev,
			"wd: unable to register watchdog device.\n");
		dev->wd_interface_reg = false;
	} else {
		dev_dbg(&dev->pdev->dev,
			"wd: successfully register watchdog interface.\n");
		dev->wd_interface_reg = true;
	}
}

void mei_watchdog_unregister(struct mei_device *dev)
{
	if (dev->wd_interface_reg)
		watchdog_unregister_device(&amt_wd_dev);
	dev->wd_interface_reg = false;
}

