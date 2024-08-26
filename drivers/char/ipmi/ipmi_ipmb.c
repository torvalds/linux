// SPDX-License-Identifier: GPL-2.0

/*
 * Driver to talk to a remote management controller on IPMB.
 */

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/ipmi_msgdefs.h>
#include <linux/ipmi_smi.h>

#define DEVICE_NAME "ipmi-ipmb"

static int bmcaddr = 0x20;
module_param(bmcaddr, int, 0644);
MODULE_PARM_DESC(bmcaddr, "Address to use for BMC.");

static unsigned int retry_time_ms = 250;
module_param(retry_time_ms, uint, 0644);
MODULE_PARM_DESC(retry_time_ms, "Timeout time between retries, in milliseconds.");

static unsigned int max_retries = 1;
module_param(max_retries, uint, 0644);
MODULE_PARM_DESC(max_retries, "Max resends of a command before timing out.");

/* Add room for the two slave addresses, two checksums, and rqSeq. */
#define IPMB_MAX_MSG_LEN (IPMI_MAX_MSG_LENGTH + 5)

struct ipmi_ipmb_dev {
	struct ipmi_smi *intf;
	struct i2c_client *client;
	struct i2c_client *slave;

	struct ipmi_smi_handlers handlers;

	bool ready;

	u8 curr_seq;

	u8 bmcaddr;
	u32 retry_time_ms;
	u32 max_retries;

	struct ipmi_smi_msg *next_msg;
	struct ipmi_smi_msg *working_msg;

	/* Transmit thread. */
	struct task_struct *thread;
	struct semaphore wake_thread;
	struct semaphore got_rsp;
	spinlock_t lock;
	bool stopping;

	u8 xmitmsg[IPMB_MAX_MSG_LEN];
	unsigned int xmitlen;

	u8 rcvmsg[IPMB_MAX_MSG_LEN];
	unsigned int rcvlen;
	bool overrun;
};

static bool valid_ipmb(struct ipmi_ipmb_dev *iidev)
{
	u8 *msg = iidev->rcvmsg;
	u8 netfn;

	if (iidev->overrun)
		return false;

	/* Minimum message size. */
	if (iidev->rcvlen < 7)
		return false;

	/* Is it a response? */
	netfn = msg[1] >> 2;
	if (netfn & 1) {
		/* Response messages have an added completion code. */
		if (iidev->rcvlen < 8)
			return false;
	}

	if (ipmb_checksum(msg, 3) != 0)
		return false;
	if (ipmb_checksum(msg + 3, iidev->rcvlen - 3) != 0)
		return false;

	return true;
}

static void ipmi_ipmb_check_msg_done(struct ipmi_ipmb_dev *iidev)
{
	struct ipmi_smi_msg *imsg = NULL;
	u8 *msg = iidev->rcvmsg;
	bool is_cmd;
	unsigned long flags;

	if (iidev->rcvlen == 0)
		return;
	if (!valid_ipmb(iidev))
		goto done;

	is_cmd = ((msg[1] >> 2) & 1) == 0;

	if (is_cmd) {
		/* Ignore commands until we are up. */
		if (!iidev->ready)
			goto done;

		/* It's a command, allocate a message for it. */
		imsg = ipmi_alloc_smi_msg();
		if (!imsg)
			goto done;
		imsg->type = IPMI_SMI_MSG_TYPE_IPMB_DIRECT;
		imsg->data_size = 0;
	} else {
		spin_lock_irqsave(&iidev->lock, flags);
		if (iidev->working_msg) {
			u8 seq = msg[4] >> 2;
			bool xmit_rsp = (iidev->working_msg->data[0] >> 2) & 1;

			/*
			 * Responses should carry the sequence we sent
			 * them with.  If it's a transmitted response,
			 * ignore it.  And if the message hasn't been
			 * transmitted, ignore it.
			 */
			if (!xmit_rsp && seq == iidev->curr_seq) {
				iidev->curr_seq = (iidev->curr_seq + 1) & 0x3f;

				imsg = iidev->working_msg;
				iidev->working_msg = NULL;
			}
		}
		spin_unlock_irqrestore(&iidev->lock, flags);
	}

	if (!imsg)
		goto done;

	if (imsg->type == IPMI_SMI_MSG_TYPE_IPMB_DIRECT) {
		imsg->rsp[0] = msg[1]; /* NetFn/LUN */
		/*
		 * Keep the source address, rqSeq.  Drop the trailing
		 * checksum.
		 */
		memcpy(imsg->rsp + 1, msg + 3, iidev->rcvlen - 4);
		imsg->rsp_size = iidev->rcvlen - 3;
	} else {
		imsg->rsp[0] = msg[1]; /* NetFn/LUN */
		/*
		 * Skip the source address, rqSeq.  Drop the trailing
		 * checksum.
		 */
		memcpy(imsg->rsp + 1, msg + 5, iidev->rcvlen - 6);
		imsg->rsp_size = iidev->rcvlen - 5;
	}
	ipmi_smi_msg_received(iidev->intf, imsg);
	if (!is_cmd)
		up(&iidev->got_rsp);

done:
	iidev->overrun = false;
	iidev->rcvlen = 0;
}

/*
 * The IPMB protocol only supports i2c writes so there is no need to
 * support I2C_SLAVE_READ* events, except to know if the other end has
 * issued a read without going to stop mode.
 */
static int ipmi_ipmb_slave_cb(struct i2c_client *client,
			      enum i2c_slave_event event, u8 *val)
{
	struct ipmi_ipmb_dev *iidev = i2c_get_clientdata(client);

	switch (event) {
	case I2C_SLAVE_WRITE_REQUESTED:
		ipmi_ipmb_check_msg_done(iidev);
		/*
		 * First byte is the slave address, to ease the checksum
		 * calculation.
		 */
		iidev->rcvmsg[0] = client->addr << 1;
		iidev->rcvlen = 1;
		break;

	case I2C_SLAVE_WRITE_RECEIVED:
		if (iidev->rcvlen >= sizeof(iidev->rcvmsg))
			iidev->overrun = true;
		else
			iidev->rcvmsg[iidev->rcvlen++] = *val;
		break;

	case I2C_SLAVE_READ_REQUESTED:
	case I2C_SLAVE_STOP:
		ipmi_ipmb_check_msg_done(iidev);
		break;

	case I2C_SLAVE_READ_PROCESSED:
		break;
	}

	return 0;
}

static void ipmi_ipmb_send_response(struct ipmi_ipmb_dev *iidev,
				    struct ipmi_smi_msg *msg, u8 cc)
{
	if ((msg->data[0] >> 2) & 1) {
		/*
		 * It's a response being sent, we need to return a
		 * response to the response.  Fake a send msg command
		 * response with channel 0.  This will always be ipmb
		 * direct.
		 */
		msg->data[0] = (IPMI_NETFN_APP_REQUEST | 1) << 2;
		msg->data[3] = IPMI_SEND_MSG_CMD;
		msg->data[4] = cc;
		msg->data_size = 5;
	}
	msg->rsp[0] = msg->data[0] | (1 << 2);
	if (msg->type == IPMI_SMI_MSG_TYPE_IPMB_DIRECT) {
		msg->rsp[1] = msg->data[1];
		msg->rsp[2] = msg->data[2];
		msg->rsp[3] = msg->data[3];
		msg->rsp[4] = cc;
		msg->rsp_size = 5;
	} else {
		msg->rsp[1] = msg->data[1];
		msg->rsp[2] = cc;
		msg->rsp_size = 3;
	}
	ipmi_smi_msg_received(iidev->intf, msg);
}

static void ipmi_ipmb_format_for_xmit(struct ipmi_ipmb_dev *iidev,
				      struct ipmi_smi_msg *msg)
{
	if (msg->type == IPMI_SMI_MSG_TYPE_IPMB_DIRECT) {
		iidev->xmitmsg[0] = msg->data[1];
		iidev->xmitmsg[1] = msg->data[0];
		memcpy(iidev->xmitmsg + 4, msg->data + 2, msg->data_size - 2);
		iidev->xmitlen = msg->data_size + 2;
	} else {
		iidev->xmitmsg[0] = iidev->bmcaddr;
		iidev->xmitmsg[1] = msg->data[0];
		iidev->xmitmsg[4] = 0;
		memcpy(iidev->xmitmsg + 5, msg->data + 1, msg->data_size - 1);
		iidev->xmitlen = msg->data_size + 4;
	}
	iidev->xmitmsg[3] = iidev->slave->addr << 1;
	if (((msg->data[0] >> 2) & 1) == 0)
		/* If it's a command, put in our own sequence number. */
		iidev->xmitmsg[4] = ((iidev->xmitmsg[4] & 0x03) |
				     (iidev->curr_seq << 2));

	/* Now add on the final checksums. */
	iidev->xmitmsg[2] = ipmb_checksum(iidev->xmitmsg, 2);
	iidev->xmitmsg[iidev->xmitlen] =
		ipmb_checksum(iidev->xmitmsg + 3, iidev->xmitlen - 3);
	iidev->xmitlen++;
}

static int ipmi_ipmb_thread(void *data)
{
	struct ipmi_ipmb_dev *iidev = data;

	while (!kthread_should_stop()) {
		long ret;
		struct i2c_msg i2c_msg;
		struct ipmi_smi_msg *msg = NULL;
		unsigned long flags;
		unsigned int retries = 0;

		/* Wait for a message to send */
		ret = down_interruptible(&iidev->wake_thread);
		if (iidev->stopping)
			break;
		if (ret)
			continue;

		spin_lock_irqsave(&iidev->lock, flags);
		if (iidev->next_msg) {
			msg = iidev->next_msg;
			iidev->next_msg = NULL;
		}
		spin_unlock_irqrestore(&iidev->lock, flags);
		if (!msg)
			continue;

		ipmi_ipmb_format_for_xmit(iidev, msg);

retry:
		i2c_msg.len = iidev->xmitlen - 1;
		if (i2c_msg.len > 32) {
			ipmi_ipmb_send_response(iidev, msg,
						IPMI_REQ_LEN_EXCEEDED_ERR);
			continue;
		}

		i2c_msg.addr = iidev->xmitmsg[0] >> 1;
		i2c_msg.flags = 0;
		i2c_msg.buf = iidev->xmitmsg + 1;

		/* Rely on i2c_transfer for a barrier. */
		iidev->working_msg = msg;

		ret = i2c_transfer(iidev->client->adapter, &i2c_msg, 1);

		if ((msg->data[0] >> 2) & 1) {
			/*
			 * It's a response, nothing will be returned
			 * by the other end.
			 */

			iidev->working_msg = NULL;
			ipmi_ipmb_send_response(iidev, msg,
						ret < 0 ? IPMI_BUS_ERR : 0);
			continue;
		}
		if (ret < 0) {
			iidev->working_msg = NULL;
			ipmi_ipmb_send_response(iidev, msg, IPMI_BUS_ERR);
			continue;
		}

		/* A command was sent, wait for its response. */
		ret = down_timeout(&iidev->got_rsp,
				   msecs_to_jiffies(iidev->retry_time_ms));

		/*
		 * Grab the message if we can.  If the handler hasn't
		 * already handled it, the message will still be there.
		 */
		spin_lock_irqsave(&iidev->lock, flags);
		msg = iidev->working_msg;
		iidev->working_msg = NULL;
		spin_unlock_irqrestore(&iidev->lock, flags);

		if (!msg && ret) {
			/*
			 * If working_msg is not set and we timed out,
			 * that means the message grabbed by
			 * check_msg_done before we could grab it
			 * here.  Wait again for check_msg_done to up
			 * the semaphore.
			 */
			down(&iidev->got_rsp);
		} else if (msg && ++retries <= iidev->max_retries) {
			spin_lock_irqsave(&iidev->lock, flags);
			iidev->working_msg = msg;
			spin_unlock_irqrestore(&iidev->lock, flags);
			goto retry;
		}

		if (msg)
			ipmi_ipmb_send_response(iidev, msg, IPMI_TIMEOUT_ERR);
	}

	if (iidev->next_msg)
		/* Return an unspecified error. */
		ipmi_ipmb_send_response(iidev, iidev->next_msg, 0xff);

	return 0;
}

static int ipmi_ipmb_start_processing(void *send_info,
				      struct ipmi_smi *new_intf)
{
	struct ipmi_ipmb_dev *iidev = send_info;

	iidev->intf = new_intf;
	iidev->ready = true;
	return 0;
}

static void ipmi_ipmb_stop_thread(struct ipmi_ipmb_dev *iidev)
{
	if (iidev->thread) {
		struct task_struct *t = iidev->thread;

		iidev->thread = NULL;
		iidev->stopping = true;
		up(&iidev->wake_thread);
		up(&iidev->got_rsp);
		kthread_stop(t);
	}
}

static void ipmi_ipmb_shutdown(void *send_info)
{
	struct ipmi_ipmb_dev *iidev = send_info;

	ipmi_ipmb_stop_thread(iidev);
}

static void ipmi_ipmb_sender(void *send_info,
			     struct ipmi_smi_msg *msg)
{
	struct ipmi_ipmb_dev *iidev = send_info;
	unsigned long flags;

	spin_lock_irqsave(&iidev->lock, flags);
	BUG_ON(iidev->next_msg);

	iidev->next_msg = msg;
	spin_unlock_irqrestore(&iidev->lock, flags);

	up(&iidev->wake_thread);
}

static void ipmi_ipmb_request_events(void *send_info)
{
	/* We don't fetch events here. */
}

static void ipmi_ipmb_cleanup(struct ipmi_ipmb_dev *iidev)
{
	if (iidev->slave) {
		i2c_slave_unregister(iidev->slave);
		if (iidev->slave != iidev->client)
			i2c_unregister_device(iidev->slave);
	}
	iidev->slave = NULL;
	iidev->client = NULL;
	ipmi_ipmb_stop_thread(iidev);
}

static void ipmi_ipmb_remove(struct i2c_client *client)
{
	struct ipmi_ipmb_dev *iidev = i2c_get_clientdata(client);

	ipmi_ipmb_cleanup(iidev);
	ipmi_unregister_smi(iidev->intf);
}

static int ipmi_ipmb_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ipmi_ipmb_dev *iidev;
	struct device_node *slave_np;
	struct i2c_adapter *slave_adap = NULL;
	struct i2c_client *slave = NULL;
	int rv;

	iidev = devm_kzalloc(&client->dev, sizeof(*iidev), GFP_KERNEL);
	if (!iidev)
		return -ENOMEM;

	if (of_property_read_u8(dev->of_node, "bmcaddr", &iidev->bmcaddr) != 0)
		iidev->bmcaddr = bmcaddr;
	if (iidev->bmcaddr == 0 || iidev->bmcaddr & 1) {
		/* Can't have the write bit set. */
		dev_notice(&client->dev,
			   "Invalid bmc address value %2.2x\n", iidev->bmcaddr);
		return -EINVAL;
	}

	if (of_property_read_u32(dev->of_node, "retry-time",
				 &iidev->retry_time_ms) != 0)
		iidev->retry_time_ms = retry_time_ms;

	if (of_property_read_u32(dev->of_node, "max-retries",
				 &iidev->max_retries) != 0)
		iidev->max_retries = max_retries;

	slave_np = of_parse_phandle(dev->of_node, "slave-dev", 0);
	if (slave_np) {
		slave_adap = of_get_i2c_adapter_by_node(slave_np);
		of_node_put(slave_np);
		if (!slave_adap) {
			dev_notice(&client->dev,
				   "Could not find slave adapter\n");
			return -EINVAL;
		}
	}

	iidev->client = client;

	if (slave_adap) {
		struct i2c_board_info binfo;

		memset(&binfo, 0, sizeof(binfo));
		strscpy(binfo.type, "ipmb-slave", I2C_NAME_SIZE);
		binfo.addr = client->addr;
		binfo.flags = I2C_CLIENT_SLAVE;
		slave = i2c_new_client_device(slave_adap, &binfo);
		i2c_put_adapter(slave_adap);
		if (IS_ERR(slave)) {
			rv = PTR_ERR(slave);
			dev_notice(&client->dev,
				   "Could not allocate slave device: %d\n", rv);
			return rv;
		}
		i2c_set_clientdata(slave, iidev);
	} else {
		slave = client;
	}
	i2c_set_clientdata(client, iidev);
	slave->flags |= I2C_CLIENT_SLAVE;

	rv = i2c_slave_register(slave, ipmi_ipmb_slave_cb);
	if (rv)
		goto out_err;
	iidev->slave = slave;
	slave = NULL;

	iidev->handlers.flags = IPMI_SMI_CAN_HANDLE_IPMB_DIRECT;
	iidev->handlers.start_processing = ipmi_ipmb_start_processing;
	iidev->handlers.shutdown = ipmi_ipmb_shutdown;
	iidev->handlers.sender = ipmi_ipmb_sender;
	iidev->handlers.request_events = ipmi_ipmb_request_events;

	spin_lock_init(&iidev->lock);
	sema_init(&iidev->wake_thread, 0);
	sema_init(&iidev->got_rsp, 0);

	iidev->thread = kthread_run(ipmi_ipmb_thread, iidev,
				    "kipmb%4.4x", client->addr);
	if (IS_ERR(iidev->thread)) {
		rv = PTR_ERR(iidev->thread);
		dev_notice(&client->dev,
			   "Could not start kernel thread: error %d\n", rv);
		goto out_err;
	}

	rv = ipmi_register_smi(&iidev->handlers,
			       iidev,
			       &client->dev,
			       iidev->bmcaddr);
	if (rv)
		goto out_err;

	return 0;

out_err:
	if (slave && slave != client)
		i2c_unregister_device(slave);
	ipmi_ipmb_cleanup(iidev);
	return rv;
}

#ifdef CONFIG_OF
static const struct of_device_id of_ipmi_ipmb_match[] = {
	{ .type = "ipmi", .compatible = DEVICE_NAME },
	{},
};
MODULE_DEVICE_TABLE(of, of_ipmi_ipmb_match);
#else
#define of_ipmi_ipmb_match NULL
#endif

static const struct i2c_device_id ipmi_ipmb_id[] = {
	{ DEVICE_NAME },
	{}
};
MODULE_DEVICE_TABLE(i2c, ipmi_ipmb_id);

static struct i2c_driver ipmi_ipmb_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = of_ipmi_ipmb_match,
	},
	.probe		= ipmi_ipmb_probe,
	.remove		= ipmi_ipmb_remove,
	.id_table	= ipmi_ipmb_id,
};
module_i2c_driver(ipmi_ipmb_driver);

MODULE_AUTHOR("Corey Minyard");
MODULE_DESCRIPTION("IPMI IPMB driver");
MODULE_LICENSE("GPL v2");
