// SPDX-License-Identifier: GPL-2.0

/*
 * IPMB driver to receive a request and send a response
 *
 * Copyright (C) 2019 Mellanox Techologies, Ltd.
 *
 * This was inspired by Brendan Higgins' ipmi-bmc-bt-i2c driver.
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
#include <linux/wait.h>

#define MAX_MSG_LEN		240
#define IPMB_REQUEST_LEN_MIN	7
#define NETFN_RSP_BIT_MASK	0x4
#define REQUEST_QUEUE_MAX_LEN	256

#define IPMB_MSG_LEN_IDX	0
#define RQ_SA_8BIT_IDX		1
#define NETFN_LUN_IDX		2

#define GET_7BIT_ADDR(addr_8bit)	(addr_8bit >> 1)
#define GET_8BIT_ADDR(addr_7bit)	((addr_7bit << 1) & 0xff)

#define IPMB_MSG_PAYLOAD_LEN_MAX (MAX_MSG_LEN - IPMB_REQUEST_LEN_MIN - 1)

#define SMBUS_MSG_HEADER_LENGTH	2
#define SMBUS_MSG_IDX_OFFSET	(SMBUS_MSG_HEADER_LENGTH + 1)

struct ipmb_msg {
	u8 len;
	u8 rs_sa;
	u8 netfn_rs_lun;
	u8 checksum1;
	u8 rq_sa;
	u8 rq_seq_rq_lun;
	u8 cmd;
	u8 payload[IPMB_MSG_PAYLOAD_LEN_MAX];
	/* checksum2 is included in payload */
} __packed;

struct ipmb_request_elem {
	struct list_head list;
	struct ipmb_msg request;
};

struct ipmb_dev {
	struct i2c_client *client;
	struct miscdevice miscdev;
	struct ipmb_msg request;
	struct list_head request_queue;
	atomic_t request_queue_len;
	size_t msg_idx;
	spinlock_t lock;
	wait_queue_head_t wait_queue;
	struct mutex file_mutex;
	bool is_i2c_protocol;
};

static inline struct ipmb_dev *to_ipmb_dev(struct file *file)
{
	return container_of(file->private_data, struct ipmb_dev, miscdev);
}

static ssize_t ipmb_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	struct ipmb_dev *ipmb_dev = to_ipmb_dev(file);
	struct ipmb_request_elem *queue_elem;
	struct ipmb_msg msg;
	ssize_t ret = 0;

	memset(&msg, 0, sizeof(msg));

	spin_lock_irq(&ipmb_dev->lock);

	while (list_empty(&ipmb_dev->request_queue)) {
		spin_unlock_irq(&ipmb_dev->lock);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(ipmb_dev->wait_queue,
				!list_empty(&ipmb_dev->request_queue));
		if (ret)
			return ret;

		spin_lock_irq(&ipmb_dev->lock);
	}

	queue_elem = list_first_entry(&ipmb_dev->request_queue,
					struct ipmb_request_elem, list);
	memcpy(&msg, &queue_elem->request, sizeof(msg));
	list_del(&queue_elem->list);
	kfree(queue_elem);
	atomic_dec(&ipmb_dev->request_queue_len);

	spin_unlock_irq(&ipmb_dev->lock);

	count = min_t(size_t, count, msg.len + 1);
	if (copy_to_user(buf, &msg, count))
		ret = -EFAULT;

	return ret < 0 ? ret : count;
}

static int ipmb_i2c_write(struct i2c_client *client, u8 *msg, u8 addr)
{
	struct i2c_msg i2c_msg;

	/*
	 * subtract 1 byte (rq_sa) from the length of the msg passed to
	 * raw i2c_transfer
	 */
	i2c_msg.len = msg[IPMB_MSG_LEN_IDX] - 1;

	/* Assign message to buffer except first 2 bytes (length and address) */
	i2c_msg.buf = msg + 2;

	i2c_msg.addr = addr;
	i2c_msg.flags = client->flags & I2C_CLIENT_PEC;

	return i2c_transfer(client->adapter, &i2c_msg, 1);
}

static ssize_t ipmb_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	struct ipmb_dev *ipmb_dev = to_ipmb_dev(file);
	u8 rq_sa, netf_rq_lun, msg_len;
	struct i2c_client *temp_client;
	u8 msg[MAX_MSG_LEN];
	ssize_t ret;

	if (count > sizeof(msg))
		return -EINVAL;

	if (copy_from_user(&msg, buf, count))
		return -EFAULT;

	if (count < msg[0])
		return -EINVAL;

	rq_sa = GET_7BIT_ADDR(msg[RQ_SA_8BIT_IDX]);
	netf_rq_lun = msg[NETFN_LUN_IDX];

	/* Check i2c block transfer vs smbus */
	if (ipmb_dev->is_i2c_protocol) {
		ret = ipmb_i2c_write(ipmb_dev->client, msg, rq_sa);
		return (ret == 1) ? count : ret;
	}

	/*
	 * subtract rq_sa and netf_rq_lun from the length of the msg. Fill the
	 * temporary client. Note that its use is an exception for IPMI.
	 */
	msg_len = msg[IPMB_MSG_LEN_IDX] - SMBUS_MSG_HEADER_LENGTH;
	temp_client = kmemdup(ipmb_dev->client, sizeof(*temp_client), GFP_KERNEL);
	if (!temp_client)
		return -ENOMEM;

	temp_client->addr = rq_sa;

	ret = i2c_smbus_write_block_data(temp_client, netf_rq_lun, msg_len,
					 msg + SMBUS_MSG_IDX_OFFSET);
	kfree(temp_client);

	return ret < 0 ? ret : count;
}

static __poll_t ipmb_poll(struct file *file, poll_table *wait)
{
	struct ipmb_dev *ipmb_dev = to_ipmb_dev(file);
	__poll_t mask = EPOLLOUT;

	mutex_lock(&ipmb_dev->file_mutex);
	poll_wait(file, &ipmb_dev->wait_queue, wait);

	if (atomic_read(&ipmb_dev->request_queue_len))
		mask |= EPOLLIN;
	mutex_unlock(&ipmb_dev->file_mutex);

	return mask;
}

static const struct file_operations ipmb_fops = {
	.owner	= THIS_MODULE,
	.read	= ipmb_read,
	.write	= ipmb_write,
	.poll	= ipmb_poll,
};

/* Called with ipmb_dev->lock held. */
static void ipmb_handle_request(struct ipmb_dev *ipmb_dev)
{
	struct ipmb_request_elem *queue_elem;

	if (atomic_read(&ipmb_dev->request_queue_len) >=
			REQUEST_QUEUE_MAX_LEN)
		return;

	queue_elem = kmalloc(sizeof(*queue_elem), GFP_ATOMIC);
	if (!queue_elem)
		return;

	memcpy(&queue_elem->request, &ipmb_dev->request,
		sizeof(struct ipmb_msg));
	list_add(&queue_elem->list, &ipmb_dev->request_queue);
	atomic_inc(&ipmb_dev->request_queue_len);
	wake_up_all(&ipmb_dev->wait_queue);
}

static u8 ipmb_verify_checksum1(struct ipmb_dev *ipmb_dev, u8 rs_sa)
{
	/* The 8 lsb of the sum is 0 when the checksum is valid */
	return (rs_sa + ipmb_dev->request.netfn_rs_lun +
		ipmb_dev->request.checksum1);
}

/*
 * Verify if message has proper ipmb header with minimum length
 * and correct checksum byte.
 */
static bool is_ipmb_msg(struct ipmb_dev *ipmb_dev, u8 rs_sa)
{
	if ((ipmb_dev->msg_idx >= IPMB_REQUEST_LEN_MIN) &&
	   (!ipmb_verify_checksum1(ipmb_dev, rs_sa)))
		return true;

	return false;
}

/*
 * The IPMB protocol only supports I2C Writes so there is no need
 * to support I2C_SLAVE_READ* events.
 * This i2c callback function only monitors IPMB request messages
 * and adds them in a queue, so that they can be handled by
 * receive_ipmb_request.
 */
static int ipmb_slave_cb(struct i2c_client *client,
			enum i2c_slave_event event, u8 *val)
{
	struct ipmb_dev *ipmb_dev = i2c_get_clientdata(client);
	u8 *buf = (u8 *)&ipmb_dev->request;
	unsigned long flags;

	spin_lock_irqsave(&ipmb_dev->lock, flags);
	switch (event) {
	case I2C_SLAVE_WRITE_REQUESTED:
		memset(&ipmb_dev->request, 0, sizeof(ipmb_dev->request));
		ipmb_dev->msg_idx = 0;

		/*
		 * At index 0, ipmb_msg stores the length of msg,
		 * skip it for now.
		 * The len will be populated once the whole
		 * buf is populated.
		 *
		 * The I2C bus driver's responsibility is to pass the
		 * data bytes to the backend driver; it does not
		 * forward the i2c slave address.
		 * Since the first byte in the IPMB message is the
		 * address of the responder, it is the responsibility
		 * of the IPMB driver to format the message properly.
		 * So this driver prepends the address of the responder
		 * to the received i2c data before the request message
		 * is handled in userland.
		 */
		buf[++ipmb_dev->msg_idx] = GET_8BIT_ADDR(client->addr);
		break;

	case I2C_SLAVE_WRITE_RECEIVED:
		if (ipmb_dev->msg_idx >= sizeof(struct ipmb_msg) - 1)
			break;

		buf[++ipmb_dev->msg_idx] = *val;
		break;

	case I2C_SLAVE_STOP:
		ipmb_dev->request.len = ipmb_dev->msg_idx;
		if (is_ipmb_msg(ipmb_dev, GET_8BIT_ADDR(client->addr)))
			ipmb_handle_request(ipmb_dev);
		break;

	default:
		break;
	}
	spin_unlock_irqrestore(&ipmb_dev->lock, flags);

	return 0;
}

static int ipmb_probe(struct i2c_client *client)
{
	struct ipmb_dev *ipmb_dev;
	int ret;

	ipmb_dev = devm_kzalloc(&client->dev, sizeof(*ipmb_dev),
					GFP_KERNEL);
	if (!ipmb_dev)
		return -ENOMEM;

	spin_lock_init(&ipmb_dev->lock);
	init_waitqueue_head(&ipmb_dev->wait_queue);
	atomic_set(&ipmb_dev->request_queue_len, 0);
	INIT_LIST_HEAD(&ipmb_dev->request_queue);

	mutex_init(&ipmb_dev->file_mutex);

	ipmb_dev->miscdev.minor = MISC_DYNAMIC_MINOR;

	ipmb_dev->miscdev.name = devm_kasprintf(&client->dev, GFP_KERNEL,
						"%s%d", "ipmb-",
						client->adapter->nr);
	ipmb_dev->miscdev.fops = &ipmb_fops;
	ipmb_dev->miscdev.parent = &client->dev;
	ret = misc_register(&ipmb_dev->miscdev);
	if (ret)
		return ret;

	ipmb_dev->is_i2c_protocol
		= device_property_read_bool(&client->dev, "i2c-protocol");

	ipmb_dev->client = client;
	i2c_set_clientdata(client, ipmb_dev);
	ret = i2c_slave_register(client, ipmb_slave_cb);
	if (ret) {
		misc_deregister(&ipmb_dev->miscdev);
		return ret;
	}

	return 0;
}

static void ipmb_remove(struct i2c_client *client)
{
	struct ipmb_dev *ipmb_dev = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	misc_deregister(&ipmb_dev->miscdev);
}

static const struct i2c_device_id ipmb_id[] = {
	{ "ipmb-dev", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, ipmb_id);

static const struct acpi_device_id acpi_ipmb_id[] = {
	{ "IPMB0001", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, acpi_ipmb_id);

static struct i2c_driver ipmb_driver = {
	.driver = {
		.name = "ipmb-dev",
		.acpi_match_table = ACPI_PTR(acpi_ipmb_id),
	},
	.probe = ipmb_probe,
	.remove = ipmb_remove,
	.id_table = ipmb_id,
};
module_i2c_driver(ipmb_driver);

MODULE_AUTHOR("Mellanox Technologies");
MODULE_DESCRIPTION("IPMB driver");
MODULE_LICENSE("GPL v2");
