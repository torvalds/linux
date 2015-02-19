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
#include <linux/sched.h>
#include <linux/watchdog.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "client.h"

static const u8 mei_start_wd_params[] = { 0x02, 0x12, 0x13, 0x10 };
static const u8 mei_stop_wd_params[] = { 0x02, 0x02, 0x14, 0x10 };

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
	dev_dbg(dev->dev, "wd: set timeout=%d.\n", timeout);
	memcpy(dev->wd_data, mei_start_wd_params, MEI_WD_HDR_SIZE);
	memcpy(dev->wd_data + MEI_WD_HDR_SIZE, &timeout, sizeof(u16));
}

/**
 * mei_wd_host_init - connect to the watchdog client
 *
 * @dev: the device structure
 *
 * Return: -ENOTTY if wd client cannot be found
 *         -EIO if write has failed
 *         0 on success
 */
int mei_wd_host_init(struct mei_device *dev)
{
	struct mei_cl *cl = &dev->wd_cl;
	struct mei_me_client *me_cl;
	int ret;

	mei_cl_init(cl, dev);

	dev->wd_timeout = MEI_WD_DEFAULT_TIMEOUT;
	dev->wd_state = MEI_WD_IDLE;


	/* check for valid client id */
	me_cl = mei_me_cl_by_uuid(dev, &mei_wd_guid);
	if (!me_cl) {
		dev_info(dev->dev, "wd: failed to find the client\n");
		return -ENOTTY;
	}

	cl->me_client_id = me_cl->client_id;
	cl->cl_uuid = me_cl->props.protocol_name;
	mei_me_cl_put(me_cl);

	ret = mei_cl_link(cl, MEI_WD_HOST_CLIENT_ID);

	if (ret < 0) {
		dev_info(dev->dev, "wd: failed link client\n");
		return ret;
	}

	ret = mei_cl_connect(cl, NULL);

	if (ret) {
		dev_err(dev->dev, "wd: failed to connect = %d\n", ret);
		mei_cl_unlink(cl);
		return ret;
	}

	ret = mei_watchdog_register(dev);
	if (ret) {
		mei_cl_disconnect(cl);
		mei_cl_unlink(cl);
	}
	return ret;
}

/**
 * mei_wd_send - sends watch dog message to fw.
 *
 * @dev: the device structure
 *
 * Return: 0 if success,
 *	-EIO when message send fails
 *	-EINVAL when invalid message is to be sent
 *	-ENODEV on flow control failure
 */
int mei_wd_send(struct mei_device *dev)
{
	struct mei_cl *cl = &dev->wd_cl;
	struct mei_msg_hdr hdr;
	int ret;

	hdr.host_addr = cl->host_client_id;
	hdr.me_addr = cl->me_client_id;
	hdr.msg_complete = 1;
	hdr.reserved = 0;
	hdr.internal = 0;

	if (!memcmp(dev->wd_data, mei_start_wd_params, MEI_WD_HDR_SIZE))
		hdr.length = MEI_WD_START_MSG_SIZE;
	else if (!memcmp(dev->wd_data, mei_stop_wd_params, MEI_WD_HDR_SIZE))
		hdr.length = MEI_WD_STOP_MSG_SIZE;
	else {
		dev_err(dev->dev, "wd: invalid message is to be sent, aborting\n");
		return -EINVAL;
	}

	ret = mei_write_message(dev, &hdr, dev->wd_data);
	if (ret) {
		dev_err(dev->dev, "wd: write message failed\n");
		return ret;
	}

	ret = mei_cl_flow_ctrl_reduce(cl);
	if (ret) {
		dev_err(dev->dev, "wd: flow_ctrl_reduce failed.\n");
		return ret;
	}

	return 0;
}

/**
 * mei_wd_stop - sends watchdog stop message to fw.
 *
 * @dev: the device structure
 *
 * Return: 0 if success
 * on error:
 *	-EIO    when message send fails
 *	-EINVAL when invalid message is to be sent
 *	-ETIME  on message timeout
 */
int mei_wd_stop(struct mei_device *dev)
{
	int ret;

	if (dev->wd_cl.state != MEI_FILE_CONNECTED ||
	    dev->wd_state != MEI_WD_RUNNING)
		return 0;

	memcpy(dev->wd_data, mei_stop_wd_params, MEI_WD_STOP_MSG_SIZE);

	dev->wd_state = MEI_WD_STOPPING;

	ret = mei_cl_flow_ctrl_creds(&dev->wd_cl);
	if (ret < 0)
		goto err;

	if (ret && mei_hbuf_acquire(dev)) {
		ret = mei_wd_send(dev);
		if (ret)
			goto err;
		dev->wd_pending = false;
	} else {
		dev->wd_pending = true;
	}

	mutex_unlock(&dev->device_lock);

	ret = wait_event_timeout(dev->wait_stop_wd,
				dev->wd_state == MEI_WD_IDLE,
				msecs_to_jiffies(MEI_WD_STOP_TIMEOUT));
	mutex_lock(&dev->device_lock);
	if (dev->wd_state != MEI_WD_IDLE) {
		/* timeout */
		ret = -ETIME;
		dev_warn(dev->dev, "wd: stop failed to complete ret=%d\n", ret);
		goto err;
	}
	dev_dbg(dev->dev, "wd: stop completed after %u msec\n",
			MEI_WD_STOP_TIMEOUT - jiffies_to_msecs(ret));
	return 0;
err:
	return ret;
}

/*
 * mei_wd_ops_start - wd start command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 *
 * Return: 0 if success, negative errno code for failure
 */
static int mei_wd_ops_start(struct watchdog_device *wd_dev)
{
	int err = -ENODEV;
	struct mei_device *dev;

	dev = watchdog_get_drvdata(wd_dev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state != MEI_DEV_ENABLED) {
		dev_dbg(dev->dev, "wd: dev_state != MEI_DEV_ENABLED  dev_state = %s\n",
			mei_dev_state_str(dev->dev_state));
		goto end_unlock;
	}

	if (dev->wd_cl.state != MEI_FILE_CONNECTED)	{
		dev_dbg(dev->dev, "MEI Driver is not connected to Watchdog Client\n");
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
 * Return: 0 if success, negative errno code for failure
 */
static int mei_wd_ops_stop(struct watchdog_device *wd_dev)
{
	struct mei_device *dev;

	dev = watchdog_get_drvdata(wd_dev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);
	mei_wd_stop(dev);
	mutex_unlock(&dev->device_lock);

	return 0;
}

/*
 * mei_wd_ops_ping - wd ping command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 *
 * Return: 0 if success, negative errno code for failure
 */
static int mei_wd_ops_ping(struct watchdog_device *wd_dev)
{
	struct mei_device *dev;
	struct mei_cl *cl;
	int ret;

	dev = watchdog_get_drvdata(wd_dev);
	if (!dev)
		return -ENODEV;

	cl = &dev->wd_cl;

	mutex_lock(&dev->device_lock);

	if (cl->state != MEI_FILE_CONNECTED) {
		dev_err(dev->dev, "wd: not connected.\n");
		ret = -ENODEV;
		goto end;
	}

	dev->wd_state = MEI_WD_RUNNING;

	ret = mei_cl_flow_ctrl_creds(cl);
	if (ret < 0)
		goto end;

	/* Check if we can send the ping to HW*/
	if (ret && mei_hbuf_acquire(dev)) {
		dev_dbg(dev->dev, "wd: sending ping\n");

		ret = mei_wd_send(dev);
		if (ret)
			goto end;
		dev->wd_pending = false;
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
 * Return: 0 if success, negative errno code for failure
 */
static int mei_wd_ops_set_timeout(struct watchdog_device *wd_dev,
		unsigned int timeout)
{
	struct mei_device *dev;

	dev = watchdog_get_drvdata(wd_dev);
	if (!dev)
		return -ENODEV;

	/* Check Timeout value */
	if (timeout < MEI_WD_MIN_TIMEOUT || timeout > MEI_WD_MAX_TIMEOUT)
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
		.options = WDIOF_KEEPALIVEPING |
			   WDIOF_SETTIMEOUT |
			   WDIOF_ALARMONLY,
};

static struct watchdog_device amt_wd_dev = {
		.info = &wd_info,
		.ops = &wd_ops,
		.timeout = MEI_WD_DEFAULT_TIMEOUT,
		.min_timeout = MEI_WD_MIN_TIMEOUT,
		.max_timeout = MEI_WD_MAX_TIMEOUT,
};


int mei_watchdog_register(struct mei_device *dev)
{

	int ret;

	/* unlock to perserve correct locking order */
	mutex_unlock(&dev->device_lock);
	ret = watchdog_register_device(&amt_wd_dev);
	mutex_lock(&dev->device_lock);
	if (ret) {
		dev_err(dev->dev, "wd: unable to register watchdog device = %d.\n",
			ret);
		return ret;
	}

	dev_dbg(dev->dev, "wd: successfully register watchdog interface.\n");
	watchdog_set_drvdata(&amt_wd_dev, dev);
	return 0;
}

void mei_watchdog_unregister(struct mei_device *dev)
{
	if (watchdog_get_drvdata(&amt_wd_dev) == NULL)
		return;

	watchdog_set_drvdata(&amt_wd_dev, NULL);
	watchdog_unregister_device(&amt_wd_dev);
}

