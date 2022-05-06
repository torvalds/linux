// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"altmode-glink: %s: " fmt, __func__

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/qcom/altmode-glink.h>
#include <linux/soc/qcom/pmic_glink.h>

#define MSG_OWNER_USBC_PAN	32780
#define MSG_TYPE_REQ_RESP	1

#define NOTIFY_PAYLOAD_SIZE	16
#define USBC_WRITE_BUFFER_SIZE	8

#define USBC_CMD_WRITE_REQ	0x15
#define USBC_NOTIFY_IND		0x16

#define ALTMODE_NAME_MAX_LEN	10

struct usbc_notify_ind_msg {
	struct pmic_glink_hdr	hdr;
	u8			payload[NOTIFY_PAYLOAD_SIZE];
	u32			reserved;
};

struct usbc_write_buffer_req_msg {
	struct pmic_glink_hdr	hdr;
	u8			buf[USBC_WRITE_BUFFER_SIZE];
	u32			reserved;
};

/**
 * struct altmode_dev
 *	Definition of an altmode device.
 *
 * @dev:		Altmode parent device for all client devices
 * @name:		Short descriptive name of this altmode device
 * @pgclient:		PMIC GLINK client to talk to remote subsystem
 * @client_idr:		idr list for altmode clients
 * @client_lock:	mutex protecting changes to client_idr
 * @d_node:		Linked list node to string together multiple amdev's
 * @client_list:	Linked list head keeping track of this device's clients
 * @pan_en_sent:	Flag to ensure PAN Enable msg is sent only once
 * @send_pan_en_work:	To schedule the sending of the PAN Enable message
 * @probe_notifier:	Inform clients of altmode probe completion
 */
struct altmode_dev {
	struct device			*dev;
	char				name[ALTMODE_NAME_MAX_LEN];
	struct pmic_glink_client	*pgclient;
	struct idr			client_idr;
	struct mutex			client_lock;
	struct list_head		d_node;
	struct list_head		client_list;
	atomic_t			pan_en_sent;
	struct delayed_work		send_pan_en_work;
	struct raw_notifier_head	probe_notifier;
};

/**
 * struct altmode_client
 *	Definition of a client of an altmode device.
 *
 * @amdev:		Parent altmode device of this client
 * @data:		Supplied by client driver during registration
 * @c_node:		Linked list node for parent altmode device's client list
 * @port_index:		Type-C port index assigned by remote subystem
 */
struct altmode_client {
	struct altmode_dev		*amdev;
	struct altmode_client_data	data;
	struct list_head		c_node;
	u8				port_index;
};

/**
 * struct probe_notify_node
 *	Linked list node to keep track of altmode clients who, by design,
 *	register with the altmode framework before altmode probes.
 *
 * @amdev_name:		Name of the altmode device client wants to bind to
 * @nb:			Client's notifier block
 * @node:		Linked list node for probe_notify_list
 */
struct probe_notify_node {
	char				*amdev_name;
	struct notifier_block		*nb;
	struct list_head		node;
};

/**
 * struct list_head amdev_list
 *	List of altmode devices currently using this driver.
 */
static LIST_HEAD(amdev_list);
static DEFINE_MUTEX(amdev_lock);

/**
 * struct list_head probe_notify_list
 *	List of altmode clients that register to get notified upon probe
 *	completion. This list is traversed and clients are removed from this
 *	list after they have been registered with the altmode device's
 *	raw_notifier_head.
 */
static LIST_HEAD(probe_notify_list);
static DEFINE_MUTEX(notify_lock);

static struct altmode_dev *get_amdev_from_dev(struct device *dev)
{
	struct altmode_dev *pos, *tmp;

	mutex_lock(&amdev_lock);
	list_for_each_entry_safe(pos, tmp, &amdev_list, d_node) {
		if (pos->dev == dev) {
			mutex_unlock(&amdev_lock);
			return pos;
		}
	}
	mutex_unlock(&amdev_lock);

	return NULL;
}

static int __altmode_send_data(struct altmode_dev *amdev, void *data,
			       size_t len)
{
	int rc;
	struct usbc_write_buffer_req_msg msg = { { 0 } };

	if (len > sizeof(msg.buf)) {
		pr_err("len %zu exceeds msg buf's size: %zu\n",
				len, USBC_WRITE_BUFFER_SIZE);
		return -EINVAL;
	}

	msg.hdr.owner = MSG_OWNER_USBC_PAN;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = USBC_CMD_WRITE_REQ;

	memcpy(msg.buf, data, len);

	rc = pmic_glink_write(amdev->pgclient, &msg, sizeof(msg));
	if (rc < 0)
		pr_err("Error in sending message: %d\n", rc);

	return rc;
}

/**
 * altmode_register_notifier()
 *	Register to be notified when altmode probes.
 *
 * @amdev_name:		The altmode device being registered with by client
 * @nb:			Notifier block to get notified upon probe completion
 *
 * Returns:		0 upon success, negative upon errors.
 */
int altmode_register_notifier(const char *amdev_name, struct notifier_block *nb)
{
	struct probe_notify_node *notify_node;

	if (!amdev_name || !nb)
		return -EINVAL;

	notify_node = kzalloc(sizeof(*notify_node), GFP_KERNEL);
	if (!notify_node)
		return -ENOMEM;

	notify_node->nb = nb;
	notify_node->amdev_name = kstrdup(amdev_name, GFP_KERNEL);
	if (!notify_node->amdev_name) {
		kfree(notify_node);
		return -ENOMEM;
	}

	mutex_lock(&notify_lock);
	list_add(&notify_node->node, &probe_notify_list);
	mutex_unlock(&notify_lock);

	return 0;
}
EXPORT_SYMBOL(altmode_register_notifier);

/**
 * altmode_deregister_notifier()
 *	Deregister probe completion notifier.
 *
 * @client:		The altmode client obtained during registration
 * @nb:			Notifier block used for registration
 *
 * Returns:		0 upon success, negative upon errors.
 */
int altmode_deregister_notifier(struct altmode_client *client,
				struct notifier_block *nb)
{
	struct altmode_dev *amdev;

	if (!client || !nb)
		return -EINVAL;

	amdev = client->amdev;
	if (!amdev)
		return -ENODEV;

	return raw_notifier_chain_unregister(&amdev->probe_notifier, nb);
}
EXPORT_SYMBOL(altmode_deregister_notifier);

/**
 * altmode_send_data()
 *	Send data from altmode client to remote subsystem.
 *
 * @client:		Parent altmode device of this client
 * @data:		Data to be sent
 * @len:		Length in bytes of the data to be sent
 *
 * Returns:		0 upon success, -EINVAL if len exceeds message buffer's
 *			capacity, and other negative error codes as appropriate.
 */
int altmode_send_data(struct altmode_client *client, void *data,
			     size_t len)
{
	struct altmode_dev *amdev = client->amdev;

	return __altmode_send_data(amdev, data, len);
}
EXPORT_SYMBOL(altmode_send_data);

/**
 * altmode_register_client()
 *	Register with altmode to receive PMIC GLINK messages from remote
 *	subsystem.
 *
 * @dev:		Device of the parent altmode (platform) device
 * @client_data:	Details identifying altmode client uniquely
 *
 * Returns:		Valid altmode client pointer upon success, ERR_PTRs
 *			upon errors.
 *
 * Notes:		client_data should contain a unique SVID.
 */
struct altmode_client *altmode_register_client(struct device *dev,
		const struct altmode_client_data *client_data)
{
	int rc;
	struct altmode_dev *amdev;
	struct altmode_client *amclient;

	if (!dev || !dev->parent)
		return ERR_PTR(-ENODEV);

	if (!client_data->name || !client_data->priv || !client_data->callback
			|| !client_data->svid)
		return ERR_PTR(-EINVAL);

	amdev = get_amdev_from_dev(dev);
	if (!amdev) {
		pr_err("No alt mode device exists for %s\n", client_data->name);
		return ERR_PTR(-ENODEV);
	}

	amclient = kzalloc(sizeof(*amclient), GFP_KERNEL);
	if (!amclient)
		return ERR_PTR(-ENOMEM);

	amclient->amdev = amdev;
	amclient->port_index = U8_MAX; /* invalid */
	amclient->data.svid = client_data->svid;
	amclient->data.priv = client_data->priv;
	amclient->data.callback = client_data->callback;
	amclient->data.name = kstrdup(client_data->name, GFP_KERNEL);
	if (!amclient->data.name) {
		kfree(amclient);
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&amdev->client_lock);
	rc = idr_alloc(&amdev->client_idr, amclient, amclient->data.svid,
			amclient->data.svid + 1, GFP_KERNEL);
	if (rc < 0) {
		pr_err("Error in allocating idr for client %s: %d\n",
				client_data->name, rc);
		mutex_unlock(&amdev->client_lock);
		kfree(amclient->data.name);
		kfree(amclient);
		return ERR_PTR(rc);
	}

	list_add(&amclient->c_node, &amdev->client_list);
	mutex_unlock(&amdev->client_lock);

	if (!atomic_read(&amdev->pan_en_sent))
		schedule_delayed_work(&amdev->send_pan_en_work,
				msecs_to_jiffies(20));

	return amclient;
}
EXPORT_SYMBOL(altmode_register_client);

/**
 * altmode_deregister_client()
 *	Deregister altmode client to stop receiving PMIC GLINK messages
 *	specific to its SVID from remote subsystem.
 *
 * @client:		Client returned by altmode_register_client()
 *
 * Returns:		0 upon success, negative upon errors.
 *
 * Notes:		This does not stop the transmission of the messages by
 *			the remote subsystem - only the reception of them by
 *			the client.
 */
int altmode_deregister_client(struct altmode_client *client)
{
	struct altmode_dev *amdev;
	struct altmode_client *pos, *tmp;

	if (!client || !client->amdev)
		return -ENODEV;

	amdev = client->amdev;

	mutex_lock(&amdev->client_lock);
	idr_remove(&amdev->client_idr, client->data.svid);

	list_for_each_entry_safe(pos, tmp, &amdev->client_list, c_node) {
		if (pos == client)
			list_del(&pos->c_node);
	}
	mutex_unlock(&amdev->client_lock);

	kfree(client->data.name);
	kfree(client);

	return 0;
}
EXPORT_SYMBOL(altmode_deregister_client);

static void altmode_send_pan_en(struct work_struct *work)
{
	int rc;
	u32 enable_msg = ALTMODE_PAN_EN;
	struct altmode_dev *amdev = container_of(work, struct altmode_dev,
			send_pan_en_work.work);

	rc = __altmode_send_data(amdev, &enable_msg, sizeof(enable_msg));
	if (rc < 0) {
		pr_err("Failed to send PAN EN: %d\n", rc);
		return;
	}

	atomic_set(&amdev->pan_en_sent, 1);
	pr_debug("Sent PAN EN\n");
}

static int altmode_send_ack(struct altmode_dev *amdev, u8 port_index)
{
	int rc;
	struct altmode_pan_ack_msg ack;

	ack.cmd_type = ALTMODE_PAN_ACK;
	ack.port_index = port_index;

	rc = __altmode_send_data(amdev, &ack, sizeof(ack));
	if (rc < 0) {
		pr_err("port %u: Failed to send PAN ACK\n", port_index);
		return rc;
	}

	pr_debug("port %d: Sent PAN ACK\n", port_index);

	return rc;
}

static void altmode_state_cb(void *priv, enum pmic_glink_state state)
{
	struct altmode_dev *amdev = priv;

	pr_debug("state: %d\n", state);

	switch (state) {
	case PMIC_GLINK_STATE_DOWN:
		/* As of now, nothing to do */
		break;
	case PMIC_GLINK_STATE_UP:
		mutex_lock(&amdev->client_lock);
		if (!list_empty(&amdev->client_list))
			schedule_delayed_work(&amdev->send_pan_en_work,
						msecs_to_jiffies(20));
		mutex_unlock(&amdev->client_lock);
		break;
	default:
		return;
	}
}

#define USBC_NOTIFY_IND_MASK	GENMASK(7, 0)
#define GET_OP(opcode)		(opcode & USBC_NOTIFY_IND_MASK)
#define GET_SVID(opcode)	(opcode >> 16)

static int altmode_callback(void *priv, void *data, size_t len)
{
	u16 svid, op;
	struct usbc_notify_ind_msg *notify_msg = data;
	struct pmic_glink_hdr *hdr = &notify_msg->hdr;
	struct altmode_dev *amdev = priv;
	struct altmode_client *amclient;
	u8 port_index;

	pr_debug("len: %zu owner: %u type: %u opcode %04x\n", len, hdr->owner,
			hdr->type, hdr->opcode);

	/*
	 * For DisplayPort alt mode, the hdr->opcode is designed as follows:
	 *
	 *	hdr->opcode = (svid << 16) | USBC_NOTIFY_IND
	 */
	op = GET_OP(hdr->opcode);
	svid = GET_SVID(hdr->opcode);
	port_index = notify_msg->payload[0];

	mutex_lock(&amdev->client_lock);
	amclient = idr_find(&amdev->client_idr, svid);
	mutex_unlock(&amdev->client_lock);

	if (op == USBC_NOTIFY_IND) {
		if (!amclient) {
			pr_debug("No client associated with SVID %#x\n", svid);
			altmode_send_ack(amdev, port_index);
			return 0;
		}

		if (amclient->port_index == U8_MAX)
			amclient->port_index = port_index;

		pr_debug("Payload: %*ph\n", NOTIFY_PAYLOAD_SIZE,
				notify_msg->payload);
		amclient->data.callback(amclient->data.priv,
					notify_msg->payload, len);

	}

	return 0;
}

static void altmode_gather_clients(struct altmode_dev *amdev)
{
	struct probe_notify_node *pos, *tmp;

	mutex_lock(&notify_lock);
	list_for_each_entry_safe(pos, tmp, &probe_notify_list, node) {
		if (!strcmp(pos->amdev_name, amdev->name)) {
			raw_notifier_chain_register(&amdev->probe_notifier,
					pos->nb);
			/*
			 * Client's nb has been added to amdev's notifier, so
			 * it may be removed from the global list.
			 */
			list_del(&pos->node);
			kfree(pos->amdev_name);
			kfree(pos);
		}
	}
	mutex_unlock(&notify_lock);
}

static void altmode_notify_clients(struct altmode_dev *amdev,
					  struct platform_device *pdev)
{
	altmode_gather_clients(amdev);
	raw_notifier_call_chain(&amdev->probe_notifier, 0, pdev);
}

static void altmode_device_add(struct altmode_dev *amdev)
{
	mutex_lock(&amdev_lock);
	list_add(&amdev->d_node, &amdev_list);
	mutex_unlock(&amdev_lock);
}

static int altmode_probe(struct platform_device *pdev)
{
	int rc;
	const char *str = NULL;
	struct altmode_dev *amdev;
	struct pmic_glink_client_data pgclient_data = { };
	struct device *dev = &pdev->dev;

	amdev = devm_kzalloc(&pdev->dev, sizeof(*amdev), GFP_KERNEL);
	if (!amdev)
		return -ENOMEM;

	rc = of_property_read_string(dev->of_node, "qcom,altmode-name",
				     &str);
	if (rc < 0) {
		dev_err(dev, "No altmode device name specified: %d\n", rc);
		return rc;
	}

	if (!str || (strlen(str) >= ALTMODE_NAME_MAX_LEN) ||
			!str_has_prefix(str, "altmode_")) {
		dev_err(dev, "Incorrect altmode device name format\n");
		return -EINVAL;
	}

	amdev->dev = dev;
	strlcpy(amdev->name, str, ALTMODE_NAME_MAX_LEN);

	RAW_INIT_NOTIFIER_HEAD(&amdev->probe_notifier);

	mutex_init(&amdev->client_lock);
	idr_init(&amdev->client_idr);
	INIT_DELAYED_WORK(&amdev->send_pan_en_work, altmode_send_pan_en);
	INIT_LIST_HEAD(&amdev->d_node);
	INIT_LIST_HEAD(&amdev->client_list);

	pgclient_data.id = MSG_OWNER_USBC_PAN;
	pgclient_data.name = "altmode";
	pgclient_data.msg_cb = altmode_callback;
	pgclient_data.priv = amdev;
	pgclient_data.state_cb = altmode_state_cb;

	amdev->pgclient = pmic_glink_register_client(amdev->dev,
			&pgclient_data);
	if (IS_ERR(amdev->pgclient)) {
		rc = PTR_ERR(amdev->pgclient);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in pmic_glink registration: %d\n",
				rc);
		goto error_register;
	}

	platform_set_drvdata(pdev, amdev);

	altmode_device_add(amdev);
	altmode_notify_clients(amdev, pdev);

	return 0;

error_register:
	idr_destroy(&amdev->client_idr);
	return rc;
}

static void altmode_device_remove(struct altmode_dev *amdev)
{
	struct altmode_dev *pos, *tmp;

	atomic_set(&amdev->pan_en_sent, 0);

	mutex_lock(&amdev_lock);
	list_for_each_entry_safe(pos, tmp, &amdev_list, d_node) {
		if (pos == amdev)
			list_del(&pos->d_node);
	}
	mutex_unlock(&amdev_lock);
}

static int altmode_remove(struct platform_device *pdev)
{
	int rc;
	struct altmode_dev *amdev = platform_get_drvdata(pdev);
	struct altmode_client *client, *tmp;

	cancel_delayed_work_sync(&amdev->send_pan_en_work);
	idr_destroy(&amdev->client_idr);

	mutex_lock(&amdev->client_lock);
	list_for_each_entry_safe(client, tmp, &amdev->client_list, c_node)
		list_del(&client->c_node);
	mutex_unlock(&amdev->client_lock);

	altmode_device_remove(amdev);

	rc = pmic_glink_unregister_client(amdev->pgclient);
	if (rc < 0)
		dev_err(amdev->dev, "Error in pmic_glink de-registration: %d\n",
				rc);

	return rc;
}

static const struct of_device_id altmode_match_table[] = {
	{ .compatible = "qcom,altmode-glink" },
	{},
};

static struct platform_driver altmode_driver = {
	.driver	= {
		.name = "altmode-glink",
		.of_match_table = altmode_match_table,
	},
	.probe	= altmode_probe,
	.remove	= altmode_remove,
};
module_platform_driver(altmode_driver);

MODULE_DESCRIPTION("QTI Type-C Alt Mode over GLINK");
MODULE_LICENSE("GPL v2");
