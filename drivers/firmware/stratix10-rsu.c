// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019, Intel Corporation
 * Copyright (C) 2025, Altera Corporation
 */

#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/firmware/intel/stratix10-svc-client.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/delay.h>

#define RSU_ERASE_SIZE_MASK		GENMASK_ULL(63, 32)
#define RSU_DCMF0_MASK			GENMASK_ULL(31, 0)
#define RSU_DCMF1_MASK			GENMASK_ULL(63, 32)
#define RSU_DCMF2_MASK			GENMASK_ULL(31, 0)
#define RSU_DCMF3_MASK			GENMASK_ULL(63, 32)
#define RSU_DCMF0_STATUS_MASK		GENMASK_ULL(15, 0)
#define RSU_DCMF1_STATUS_MASK		GENMASK_ULL(31, 16)
#define RSU_DCMF2_STATUS_MASK		GENMASK_ULL(47, 32)
#define RSU_DCMF3_STATUS_MASK		GENMASK_ULL(63, 48)

#define RSU_TIMEOUT	(msecs_to_jiffies(SVC_RSU_REQUEST_TIMEOUT_MS))

#define INVALID_RETRY_COUNTER		0xFF
#define INVALID_DCMF_VERSION		0xFF
#define INVALID_DCMF_STATUS		0xFFFFFFFF
#define INVALID_SPT_ADDRESS		0x0

#define RSU_RETRY_SLEEP_MS		(1U)
#define RSU_ASYNC_MSG_RETRY		(3U)
#define RSU_GET_SPT_RESP_LEN		(4 * sizeof(unsigned int))

typedef void (*rsu_callback)(struct stratix10_svc_client *client,
			     struct stratix10_svc_cb_data *data);
/**
 * struct stratix10_rsu_priv - rsu data structure
 * @chan: pointer to the allocated service channel
 * @client: active service client
 * @completion: state for callback completion
 * @lock: a mutex to protect callback completion state
 * @status.current_image: address of image currently running in flash
 * @status.fail_image: address of failed image in flash
 * @status.version: the interface version number of RSU firmware
 * @status.state: the state of RSU system
 * @status.error_details: error code
 * @status.error_location: the error offset inside the image that failed
 * @dcmf_version.dcmf0: Quartus dcmf0 version
 * @dcmf_version.dcmf1: Quartus dcmf1 version
 * @dcmf_version.dcmf2: Quartus dcmf2 version
 * @dcmf_version.dcmf3: Quartus dcmf3 version
 * @dcmf_status.dcmf0: dcmf0 status
 * @dcmf_status.dcmf1: dcmf1 status
 * @dcmf_status.dcmf2: dcmf2 status
 * @dcmf_status.dcmf3: dcmf3 status
 * @retry_counter: the current image's retry counter
 * @max_retry: the preset max retry value
 * @spt0_address: address of spt0
 * @spt1_address: address of spt1
 */
struct stratix10_rsu_priv {
	struct stratix10_svc_chan *chan;
	struct stratix10_svc_client client;
	struct completion completion;
	struct mutex lock;
	struct {
		unsigned long current_image;
		unsigned long fail_image;
		unsigned int version;
		unsigned int state;
		unsigned int error_details;
		unsigned int error_location;
	} status;

	struct {
		unsigned int dcmf0;
		unsigned int dcmf1;
		unsigned int dcmf2;
		unsigned int dcmf3;
	} dcmf_version;

	struct {
		unsigned int dcmf0;
		unsigned int dcmf1;
		unsigned int dcmf2;
		unsigned int dcmf3;
	} dcmf_status;

	unsigned int retry_counter;
	unsigned int max_retry;

	unsigned long spt0_address;
	unsigned long spt1_address;
};

typedef void (*rsu_async_callback)(struct device *dev,
	struct stratix10_rsu_priv *priv, struct stratix10_svc_cb_data *data);

/**
 * rsu_async_status_callback() - Status callback from rsu_async_send()
 * @dev: pointer to device object
 * @priv: pointer to priv object
 * @data: pointer to callback data structure
 *
 * Callback from rsu_async_send() to get the system rsu error status.
 */
static void rsu_async_status_callback(struct device *dev,
				      struct stratix10_rsu_priv *priv,
				      struct stratix10_svc_cb_data *data)
{
	struct arm_smccc_1_2_regs *res = (struct arm_smccc_1_2_regs *)data->kaddr1;

	priv->status.current_image = res->a2;
	priv->status.fail_image = res->a3;
	priv->status.state = res->a4;
	priv->status.version = res->a5;
	priv->status.error_location = res->a7;
	priv->status.error_details = res->a8;
	priv->retry_counter = res->a9;
}

/**
 * rsu_command_callback() - Update callback from Intel Service Layer
 * @client: pointer to client
 * @data: pointer to callback data structure
 *
 * Callback from Intel service layer for RSU commands.
 */
static void rsu_command_callback(struct stratix10_svc_client *client,
				 struct stratix10_svc_cb_data *data)
{
	struct stratix10_rsu_priv *priv = client->priv;

	if (data->status == BIT(SVC_STATUS_NO_SUPPORT))
		dev_warn(client->dev, "Secure FW doesn't support notify\n");
	else if (data->status == BIT(SVC_STATUS_ERROR))
		dev_err(client->dev, "Failure, returned status is %lu\n",
			BIT(data->status));

	complete(&priv->completion);
}


/**
 * rsu_max_retry_callback() - Callback from Intel service layer for getting
 * the max retry value from the firmware
 * @client: pointer to client
 * @data: pointer to callback data structure
 *
 * Callback from Intel service layer for max retry.
 */
static void rsu_max_retry_callback(struct stratix10_svc_client *client,
				   struct stratix10_svc_cb_data *data)
{
	struct stratix10_rsu_priv *priv = client->priv;
	unsigned int *max_retry = (unsigned int *)data->kaddr1;

	if (data->status == BIT(SVC_STATUS_OK))
		priv->max_retry = *max_retry;
	else if (data->status == BIT(SVC_STATUS_NO_SUPPORT))
		dev_warn(client->dev, "Secure FW doesn't support max retry\n");
	else
		dev_err(client->dev, "Failed to get max retry %lu\n",
			BIT(data->status));

	complete(&priv->completion);
}

/**
 * rsu_dcmf_version_callback() - Callback from Intel service layer for getting
 * the DCMF version
 * @client: pointer to client
 * @data: pointer to callback data structure
 *
 * Callback from Intel service layer for DCMF version number
 */
static void rsu_dcmf_version_callback(struct stratix10_svc_client *client,
				      struct stratix10_svc_cb_data *data)
{
	struct stratix10_rsu_priv *priv = client->priv;
	unsigned long long *value1 = (unsigned long long *)data->kaddr1;
	unsigned long long *value2 = (unsigned long long *)data->kaddr2;

	if (data->status == BIT(SVC_STATUS_OK)) {
		priv->dcmf_version.dcmf0 = FIELD_GET(RSU_DCMF0_MASK, *value1);
		priv->dcmf_version.dcmf1 = FIELD_GET(RSU_DCMF1_MASK, *value1);
		priv->dcmf_version.dcmf2 = FIELD_GET(RSU_DCMF2_MASK, *value2);
		priv->dcmf_version.dcmf3 = FIELD_GET(RSU_DCMF3_MASK, *value2);
	} else
		dev_err(client->dev, "failed to get DCMF version\n");

	complete(&priv->completion);
}

/**
 * rsu_dcmf_status_callback() - Callback from Intel service layer for getting
 * the DCMF status
 * @client: pointer to client
 * @data: pointer to callback data structure
 *
 * Callback from Intel service layer for DCMF status
 */
static void rsu_dcmf_status_callback(struct stratix10_svc_client *client,
				     struct stratix10_svc_cb_data *data)
{
	struct stratix10_rsu_priv *priv = client->priv;
	unsigned long long *value = (unsigned long long *)data->kaddr1;

	if (data->status == BIT(SVC_STATUS_OK)) {
		priv->dcmf_status.dcmf0 = FIELD_GET(RSU_DCMF0_STATUS_MASK,
						    *value);
		priv->dcmf_status.dcmf1 = FIELD_GET(RSU_DCMF1_STATUS_MASK,
						    *value);
		priv->dcmf_status.dcmf2 = FIELD_GET(RSU_DCMF2_STATUS_MASK,
						    *value);
		priv->dcmf_status.dcmf3 = FIELD_GET(RSU_DCMF3_STATUS_MASK,
						    *value);
	} else
		dev_err(client->dev, "failed to get DCMF status\n");

	complete(&priv->completion);
}

/**
 * rsu_async_get_spt_table_callback() - Callback to be used by the rsu_async_send()
 * to retrieve the SPT table information.
 * @dev: pointer to device object
 * @priv: pointer to priv object
 * @data: pointer to callback data structure
 */
static void rsu_async_get_spt_table_callback(struct device *dev,
					     struct stratix10_rsu_priv *priv,
					     struct stratix10_svc_cb_data *data)
{
	priv->spt0_address = *((unsigned long *)data->kaddr1);
	priv->spt1_address = *((unsigned long *)data->kaddr2);
}

/**
 * rsu_send_msg() - send a message to Intel service layer
 * @priv: pointer to rsu private data
 * @command: RSU status or update command
 * @arg: the request argument, the bitstream address or notify status
 * @callback: function pointer for the callback (status or update)
 *
 * Start an Intel service layer transaction to perform the SMC call that
 * is necessary to get RSU boot log or set the address of bitstream to
 * boot after reboot.
 *
 * Returns 0 on success or -ETIMEDOUT on error.
 */
static int rsu_send_msg(struct stratix10_rsu_priv *priv,
			enum stratix10_svc_command_code command,
			unsigned long arg,
			rsu_callback callback)
{
	struct stratix10_svc_client_msg msg;
	int ret;

	mutex_lock(&priv->lock);
	reinit_completion(&priv->completion);
	priv->client.receive_cb = callback;

	msg.command = command;
	if (arg)
		msg.arg[0] = arg;

	ret = stratix10_svc_send(priv->chan, &msg);
	if (ret < 0)
		goto status_done;

	ret = wait_for_completion_interruptible_timeout(&priv->completion,
							RSU_TIMEOUT);
	if (!ret) {
		dev_err(priv->client.dev,
			"timeout waiting for SMC call\n");
		ret = -ETIMEDOUT;
		goto status_done;
	} else if (ret < 0) {
		dev_err(priv->client.dev,
			"error %d waiting for SMC call\n", ret);
		goto status_done;
	} else {
		ret = 0;
	}

status_done:
	stratix10_svc_done(priv->chan);
	mutex_unlock(&priv->lock);
	return ret;
}

/**
 * soc64_async_callback() - Callback from Intel service layer for async requests
 * @ptr: pointer to the completion object
 */
static void soc64_async_callback(void *ptr)
{
	if (ptr)
		complete(ptr);
}

/**
 * rsu_send_async_msg() - send an async message to Intel service layer
 * @dev: pointer to device object
 * @priv: pointer to rsu private data
 * @command: RSU status or update command
 * @arg: the request argument, notify status
 * @callback: function pointer for the callback (status or update)
 */
static int rsu_send_async_msg(struct device *dev, struct stratix10_rsu_priv *priv,
			      enum stratix10_svc_command_code command,
			      unsigned long arg,
			      rsu_async_callback callback)
{
	struct stratix10_svc_client_msg msg = {0};
	struct stratix10_svc_cb_data data = {0};
	struct completion completion;
	int status, index, ret;
	void *handle = NULL;

	msg.command = command;
	msg.arg[0] = arg;

	init_completion(&completion);

	for (index = 0; index < RSU_ASYNC_MSG_RETRY; index++) {
		status = stratix10_svc_async_send(priv->chan, &msg,
						  &handle, soc64_async_callback,
						  &completion);
		if (status == 0)
			break;
		dev_warn(dev, "Failed to send async message\n");
		msleep(RSU_RETRY_SLEEP_MS);
	}

	if (status && !handle) {
		dev_err(dev, "Failed to send async message\n");
		return -ETIMEDOUT;
	}

	ret = wait_for_completion_io_timeout(&completion, RSU_TIMEOUT);
	if (ret > 0)
		dev_dbg(dev, "Received async interrupt\n");
	else if (ret == 0)
		dev_dbg(dev, "Timeout occurred. Trying to poll the response\n");

	for (index = 0; index < RSU_ASYNC_MSG_RETRY; index++) {
		status = stratix10_svc_async_poll(priv->chan, handle, &data);
		if (status == -EAGAIN) {
			dev_dbg(dev, "Async message is still in progress\n");
		} else if (status < 0) {
			dev_alert(dev, "Failed to poll async message\n");
			ret = -ETIMEDOUT;
		} else if (status == 0) {
			ret = 0;
			break;
		}
		msleep(RSU_RETRY_SLEEP_MS);
	}

	if (ret) {
		dev_err(dev, "Failed to get async response\n");
		goto status_done;
	}

	if (data.status == 0) {
		ret = 0;
		if (callback)
			callback(dev, priv, &data);
	} else {
		dev_err(dev, "%s returned 0x%x from SDM\n", __func__,
			data.status);
		ret = -EFAULT;
	}

status_done:
	stratix10_svc_async_done(priv->chan, handle);
	return ret;
}

/*
 * This driver exposes some optional features of the Intel Stratix 10 SoC FPGA.
 * The sysfs interfaces exposed here are FPGA Remote System Update (RSU)
 * related. They allow user space software to query the configuration system
 * status and to request optional reboot behavior specific to Intel FPGAs.
 */

static ssize_t current_image_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08lx\n", priv->status.current_image);
}

static ssize_t fail_image_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08lx\n", priv->status.fail_image);
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->status.version);
}

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->status.state);
}

static ssize_t error_location_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->status.error_location);
}

static ssize_t error_details_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->status.error_details);
}

static ssize_t retry_counter_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->retry_counter);
}

static ssize_t max_retry_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sysfs_emit(buf, "0x%08x\n", priv->max_retry);
}

static ssize_t dcmf0_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->dcmf_version.dcmf0);
}

static ssize_t dcmf1_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->dcmf_version.dcmf1);
}

static ssize_t dcmf2_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->dcmf_version.dcmf2);
}

static ssize_t dcmf3_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	return sprintf(buf, "0x%08x\n", priv->dcmf_version.dcmf3);
}

static ssize_t dcmf0_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	if (priv->dcmf_status.dcmf0 == INVALID_DCMF_STATUS)
		return -EIO;

	return sprintf(buf, "0x%08x\n", priv->dcmf_status.dcmf0);
}

static ssize_t dcmf1_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	if (priv->dcmf_status.dcmf1 == INVALID_DCMF_STATUS)
		return -EIO;

	return sprintf(buf, "0x%08x\n", priv->dcmf_status.dcmf1);
}

static ssize_t dcmf2_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	if (priv->dcmf_status.dcmf2 == INVALID_DCMF_STATUS)
		return -EIO;

	return sprintf(buf, "0x%08x\n", priv->dcmf_status.dcmf2);
}

static ssize_t dcmf3_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	if (priv->dcmf_status.dcmf3 == INVALID_DCMF_STATUS)
		return -EIO;

	return sprintf(buf, "0x%08x\n", priv->dcmf_status.dcmf3);
}
static ssize_t reboot_image_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);
	unsigned long address;
	int ret;

	if (!priv)
		return -ENODEV;

	ret = kstrtoul(buf, 0, &address);
	if (ret)
		return ret;

	ret = rsu_send_msg(priv, COMMAND_RSU_UPDATE,
			   address, rsu_command_callback);
	if (ret) {
		dev_err(dev, "Error, RSU update returned %i\n", ret);
		return ret;
	}

	return count;
}

static ssize_t notify_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);
	unsigned long status;
	int ret;

	if (!priv)
		return -ENODEV;

	ret = kstrtoul(buf, 0, &status);
	if (ret)
		return ret;

	ret = rsu_send_async_msg(dev, priv, COMMAND_RSU_NOTIFY, status, NULL);
	if (ret) {
		dev_err(dev, "Error, RSU notify returned %i\n", ret);
		return ret;
	}

	/* to get the updated state */
	ret = rsu_send_async_msg(dev, priv, COMMAND_RSU_STATUS, 0,
				 rsu_async_status_callback);
	if (ret) {
		dev_err(dev, "Error, getting RSU status %i\n", ret);
		return ret;
	}

	return count;
}

static ssize_t spt0_address_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	if (priv->spt0_address == INVALID_SPT_ADDRESS)
		return -EIO;

	return sysfs_emit(buf, "0x%08lx\n", priv->spt0_address);
}

static ssize_t spt1_address_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct stratix10_rsu_priv *priv = dev_get_drvdata(dev);

	if (!priv)
		return -ENODEV;

	if (priv->spt1_address == INVALID_SPT_ADDRESS)
		return -EIO;

	return sysfs_emit(buf, "0x%08lx\n", priv->spt1_address);
}

static DEVICE_ATTR_RO(current_image);
static DEVICE_ATTR_RO(fail_image);
static DEVICE_ATTR_RO(state);
static DEVICE_ATTR_RO(version);
static DEVICE_ATTR_RO(error_location);
static DEVICE_ATTR_RO(error_details);
static DEVICE_ATTR_RO(retry_counter);
static DEVICE_ATTR_RO(max_retry);
static DEVICE_ATTR_RO(dcmf0);
static DEVICE_ATTR_RO(dcmf1);
static DEVICE_ATTR_RO(dcmf2);
static DEVICE_ATTR_RO(dcmf3);
static DEVICE_ATTR_RO(dcmf0_status);
static DEVICE_ATTR_RO(dcmf1_status);
static DEVICE_ATTR_RO(dcmf2_status);
static DEVICE_ATTR_RO(dcmf3_status);
static DEVICE_ATTR_WO(reboot_image);
static DEVICE_ATTR_WO(notify);
static DEVICE_ATTR_RO(spt0_address);
static DEVICE_ATTR_RO(spt1_address);

static struct attribute *rsu_attrs[] = {
	&dev_attr_current_image.attr,
	&dev_attr_fail_image.attr,
	&dev_attr_state.attr,
	&dev_attr_version.attr,
	&dev_attr_error_location.attr,
	&dev_attr_error_details.attr,
	&dev_attr_retry_counter.attr,
	&dev_attr_max_retry.attr,
	&dev_attr_dcmf0.attr,
	&dev_attr_dcmf1.attr,
	&dev_attr_dcmf2.attr,
	&dev_attr_dcmf3.attr,
	&dev_attr_dcmf0_status.attr,
	&dev_attr_dcmf1_status.attr,
	&dev_attr_dcmf2_status.attr,
	&dev_attr_dcmf3_status.attr,
	&dev_attr_reboot_image.attr,
	&dev_attr_notify.attr,
	&dev_attr_spt0_address.attr,
	&dev_attr_spt1_address.attr,
	NULL
};

ATTRIBUTE_GROUPS(rsu);

static int stratix10_rsu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stratix10_rsu_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client.dev = dev;
	priv->client.receive_cb = NULL;
	priv->client.priv = priv;
	priv->status.current_image = 0;
	priv->status.fail_image = 0;
	priv->status.error_location = 0;
	priv->status.error_details = 0;
	priv->status.version = 0;
	priv->status.state = 0;
	priv->retry_counter = INVALID_RETRY_COUNTER;
	priv->dcmf_version.dcmf0 = INVALID_DCMF_VERSION;
	priv->dcmf_version.dcmf1 = INVALID_DCMF_VERSION;
	priv->dcmf_version.dcmf2 = INVALID_DCMF_VERSION;
	priv->dcmf_version.dcmf3 = INVALID_DCMF_VERSION;
	priv->dcmf_status.dcmf0 = INVALID_DCMF_STATUS;
	priv->dcmf_status.dcmf1 = INVALID_DCMF_STATUS;
	priv->dcmf_status.dcmf2 = INVALID_DCMF_STATUS;
	priv->dcmf_status.dcmf3 = INVALID_DCMF_STATUS;
	priv->max_retry = INVALID_RETRY_COUNTER;
	priv->spt0_address = INVALID_SPT_ADDRESS;
	priv->spt1_address = INVALID_SPT_ADDRESS;

	mutex_init(&priv->lock);
	priv->chan = stratix10_svc_request_channel_byname(&priv->client,
							  SVC_CLIENT_RSU);
	if (IS_ERR(priv->chan)) {
		dev_err(dev, "couldn't get service channel %s\n",
			SVC_CLIENT_RSU);
		return PTR_ERR(priv->chan);
	}

	ret = stratix10_svc_add_async_client(priv->chan, false);
	if (ret) {
		dev_err(dev, "failed to add async client\n");
		stratix10_svc_free_channel(priv->chan);
		return ret;
	}

	init_completion(&priv->completion);
	platform_set_drvdata(pdev, priv);

	/* get the initial state from firmware */
	ret = rsu_send_async_msg(dev, priv, COMMAND_RSU_STATUS, 0,
				 rsu_async_status_callback);
	if (ret) {
		dev_err(dev, "Error, getting RSU status %i\n", ret);
		stratix10_svc_free_channel(priv->chan);
	}

	/* get DCMF version from firmware */
	ret = rsu_send_msg(priv, COMMAND_RSU_DCMF_VERSION,
			   0, rsu_dcmf_version_callback);
	if (ret) {
		dev_err(dev, "Error, getting DCMF version %i\n", ret);
		stratix10_svc_free_channel(priv->chan);
	}

	ret = rsu_send_msg(priv, COMMAND_RSU_DCMF_STATUS,
			   0, rsu_dcmf_status_callback);
	if (ret) {
		dev_err(dev, "Error, getting DCMF status %i\n", ret);
		stratix10_svc_free_channel(priv->chan);
	}

	ret = rsu_send_msg(priv, COMMAND_RSU_MAX_RETRY, 0,
			   rsu_max_retry_callback);
	if (ret) {
		dev_err(dev, "Error, getting RSU max retry %i\n", ret);
		stratix10_svc_free_channel(priv->chan);
	}


	ret = rsu_send_async_msg(dev, priv, COMMAND_RSU_GET_SPT_TABLE, 0,
				 rsu_async_get_spt_table_callback);
	if (ret) {
		dev_err(dev, "Error, getting SPT table %i\n", ret);
		stratix10_svc_free_channel(priv->chan);
	}

	return ret;
}

static void stratix10_rsu_remove(struct platform_device *pdev)
{
	struct stratix10_rsu_priv *priv = platform_get_drvdata(pdev);

	stratix10_svc_free_channel(priv->chan);
}

static struct platform_driver stratix10_rsu_driver = {
	.probe = stratix10_rsu_probe,
	.remove = stratix10_rsu_remove,
	.driver = {
		.name = "stratix10-rsu",
		.dev_groups = rsu_groups,
	},
};

module_platform_driver(stratix10_rsu_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Remote System Update Driver");
MODULE_AUTHOR("Richard Gong <richard.gong@intel.com>");
