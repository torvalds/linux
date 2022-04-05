// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2018 Google LLC.

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>

#define EC_MSG_TIMEOUT_MS	200
#define HOST_COMMAND_MARK	1
#define HOST_EVENT_MARK		2

/**
 * struct cros_ec_rpmsg_response - rpmsg message format from from EC.
 *
 * @type:	The type of message, should be either HOST_COMMAND_MARK or
 *		HOST_EVENT_MARK, representing that the message is a response to
 *		host command, or a host event.
 * @data:	ec_host_response for host command.
 */
struct cros_ec_rpmsg_response {
	u8 type;
	u8 data[] __aligned(4);
};

/**
 * struct cros_ec_rpmsg - information about a EC over rpmsg.
 *
 * @rpdev:	rpmsg device we are connected to
 * @xfer_ack:	completion for host command transfer.
 * @host_event_work:	Work struct for pending host event.
 */
struct cros_ec_rpmsg {
	struct rpmsg_device *rpdev;
	struct completion xfer_ack;
	struct work_struct host_event_work;
	struct rpmsg_endpoint *ept;
};

/**
 * cros_ec_cmd_xfer_rpmsg - Transfer a message over rpmsg and receive the reply
 *
 * @ec_dev: ChromeOS EC device
 * @ec_msg: Message to transfer
 *
 * This is only used for old EC proto version, and is not supported for this
 * driver.
 *
 * Return: -EINVAL
 */
static int cros_ec_cmd_xfer_rpmsg(struct cros_ec_device *ec_dev,
				  struct cros_ec_command *ec_msg)
{
	return -EINVAL;
}

/**
 * cros_ec_pkt_xfer_rpmsg - Transfer a packet over rpmsg and receive the reply
 *
 * @ec_dev: ChromeOS EC device
 * @ec_msg: Message to transfer
 *
 * Return: number of bytes of the reply on success or negative error code.
 */
static int cros_ec_pkt_xfer_rpmsg(struct cros_ec_device *ec_dev,
				  struct cros_ec_command *ec_msg)
{
	struct cros_ec_rpmsg *ec_rpmsg = ec_dev->priv;
	struct ec_host_response *response;
	unsigned long timeout;
	int len;
	int ret;
	u8 sum;
	int i;

	ec_msg->result = 0;
	len = cros_ec_prepare_tx(ec_dev, ec_msg);
	dev_dbg(ec_dev->dev, "prepared, len=%d\n", len);

	reinit_completion(&ec_rpmsg->xfer_ack);
	ret = rpmsg_send(ec_rpmsg->ept, ec_dev->dout, len);
	if (ret) {
		dev_err(ec_dev->dev, "rpmsg send failed\n");
		return ret;
	}

	timeout = msecs_to_jiffies(EC_MSG_TIMEOUT_MS);
	ret = wait_for_completion_timeout(&ec_rpmsg->xfer_ack, timeout);
	if (!ret) {
		dev_err(ec_dev->dev, "rpmsg send timeout\n");
		return -EIO;
	}

	/* check response error code */
	response = (struct ec_host_response *)ec_dev->din;
	ec_msg->result = response->result;

	ret = cros_ec_check_result(ec_dev, ec_msg);
	if (ret)
		goto exit;

	if (response->data_len > ec_msg->insize) {
		dev_err(ec_dev->dev, "packet too long (%d bytes, expected %d)",
			response->data_len, ec_msg->insize);
		ret = -EMSGSIZE;
		goto exit;
	}

	/* copy response packet payload and compute checksum */
	memcpy(ec_msg->data, ec_dev->din + sizeof(*response),
	       response->data_len);

	sum = 0;
	for (i = 0; i < sizeof(*response) + response->data_len; i++)
		sum += ec_dev->din[i];

	if (sum) {
		dev_err(ec_dev->dev, "bad packet checksum, calculated %x\n",
			sum);
		ret = -EBADMSG;
		goto exit;
	}

	ret = response->data_len;
exit:
	if (ec_msg->command == EC_CMD_REBOOT_EC)
		msleep(EC_REBOOT_DELAY_MS);

	return ret;
}

static void
cros_ec_rpmsg_host_event_function(struct work_struct *host_event_work)
{
	struct cros_ec_rpmsg *ec_rpmsg = container_of(host_event_work,
						      struct cros_ec_rpmsg,
						      host_event_work);
	struct cros_ec_device *ec_dev = dev_get_drvdata(&ec_rpmsg->rpdev->dev);
	bool wake_event = true;
	int ret;

	ret = cros_ec_get_next_event(ec_dev, &wake_event);

	/*
	 * Signal only if wake host events or any interrupt if
	 * cros_ec_get_next_event() returned an error (default value for
	 * wake_event is true)
	 */
	if (wake_event && device_may_wakeup(ec_dev->dev))
		pm_wakeup_event(ec_dev->dev, 0);

	if (ret > 0)
		blocking_notifier_call_chain(&ec_dev->event_notifier,
					     0, ec_dev);
}

static int cros_ec_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
				  int len, void *priv, u32 src)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(&rpdev->dev);
	struct cros_ec_rpmsg *ec_rpmsg = ec_dev->priv;
	struct cros_ec_rpmsg_response *resp;

	if (!len) {
		dev_warn(ec_dev->dev, "rpmsg received empty response");
		return -EINVAL;
	}

	resp = data;
	len -= offsetof(struct cros_ec_rpmsg_response, data);
	if (resp->type == HOST_COMMAND_MARK) {
		if (len > ec_dev->din_size) {
			dev_warn(ec_dev->dev,
				 "received length %d > din_size %d, truncating",
				 len, ec_dev->din_size);
			len = ec_dev->din_size;
		}

		memcpy(ec_dev->din, resp->data, len);
		complete(&ec_rpmsg->xfer_ack);
	} else if (resp->type == HOST_EVENT_MARK) {
		schedule_work(&ec_rpmsg->host_event_work);
	} else {
		dev_warn(ec_dev->dev, "rpmsg received invalid type = %d",
			 resp->type);
		return -EINVAL;
	}

	return 0;
}

static struct rpmsg_endpoint *
cros_ec_rpmsg_create_ept(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo = {};

	strscpy(chinfo.name, rpdev->id.name, RPMSG_NAME_SIZE);
	chinfo.src = rpdev->src;
	chinfo.dst = RPMSG_ADDR_ANY;

	return rpmsg_create_ept(rpdev, cros_ec_rpmsg_callback, NULL, chinfo);
}

static int cros_ec_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct cros_ec_rpmsg *ec_rpmsg;
	struct cros_ec_device *ec_dev;
	int ret;

	ec_dev = devm_kzalloc(dev, sizeof(*ec_dev), GFP_KERNEL);
	if (!ec_dev)
		return -ENOMEM;

	ec_rpmsg = devm_kzalloc(dev, sizeof(*ec_rpmsg), GFP_KERNEL);
	if (!ec_rpmsg)
		return -ENOMEM;

	ec_dev->dev = dev;
	ec_dev->priv = ec_rpmsg;
	ec_dev->cmd_xfer = cros_ec_cmd_xfer_rpmsg;
	ec_dev->pkt_xfer = cros_ec_pkt_xfer_rpmsg;
	ec_dev->phys_name = dev_name(&rpdev->dev);
	ec_dev->din_size = sizeof(struct ec_host_response) +
			   sizeof(struct ec_response_get_protocol_info);
	ec_dev->dout_size = sizeof(struct ec_host_request);
	dev_set_drvdata(dev, ec_dev);

	ec_rpmsg->rpdev = rpdev;
	init_completion(&ec_rpmsg->xfer_ack);
	INIT_WORK(&ec_rpmsg->host_event_work,
		  cros_ec_rpmsg_host_event_function);

	ec_rpmsg->ept = cros_ec_rpmsg_create_ept(rpdev);
	if (!ec_rpmsg->ept)
		return -ENOMEM;

	ret = cros_ec_register(ec_dev);
	if (ret < 0) {
		rpmsg_destroy_ept(ec_rpmsg->ept);
		cancel_work_sync(&ec_rpmsg->host_event_work);
		return ret;
	}

	return 0;
}

static void cros_ec_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(&rpdev->dev);
	struct cros_ec_rpmsg *ec_rpmsg = ec_dev->priv;

	cros_ec_unregister(ec_dev);
	rpmsg_destroy_ept(ec_rpmsg->ept);
	cancel_work_sync(&ec_rpmsg->host_event_work);
}

#ifdef CONFIG_PM_SLEEP
static int cros_ec_rpmsg_suspend(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_suspend(ec_dev);
}

static int cros_ec_rpmsg_resume(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);

	return cros_ec_resume(ec_dev);
}
#endif

static SIMPLE_DEV_PM_OPS(cros_ec_rpmsg_pm_ops, cros_ec_rpmsg_suspend,
			 cros_ec_rpmsg_resume);

static const struct of_device_id cros_ec_rpmsg_of_match[] = {
	{ .compatible = "google,cros-ec-rpmsg", },
	{ }
};
MODULE_DEVICE_TABLE(of, cros_ec_rpmsg_of_match);

static struct rpmsg_driver cros_ec_driver_rpmsg = {
	.drv = {
		.name   = "cros-ec-rpmsg",
		.of_match_table = cros_ec_rpmsg_of_match,
		.pm	= &cros_ec_rpmsg_pm_ops,
	},
	.probe		= cros_ec_rpmsg_probe,
	.remove		= cros_ec_rpmsg_remove,
};

module_rpmsg_driver(cros_ec_driver_rpmsg);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS EC multi function device (rpmsg)");
