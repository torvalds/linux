// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"altmode-glink: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/ipc_logging.h>
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

#define MAX_NUM_PORTS		4

#define NUM_LOG_PAGES		10

#define IDR_KEY_GEN(svid, ind)	(((svid) << 8) | (ind))
#define IDR_KEY(client)		\
	IDR_KEY_GEN((client)->data.svid, (client)->port_index)

#define altmode_dbg(fmt, ...) \
	do { \
		ipc_log_string(altmode_ipc_log, fmt, ##__VA_ARGS__); \
		pr_debug(fmt, ##__VA_ARGS__); \
	} while (0)

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
 * @pgclient:		PMIC GLINK client to talk to remote subsystem
 * @client_idr:		idr list for altmode clients
 * @client_lock:	mutex protecting changes to client_idr
 * @d_node:		Linked list node to string together multiple amdev's
 * @client_list:	Linked list head keeping track of this device's clients
 * @pan_en_sent:	Flag to ensure PAN Enable msg is sent only once
 * @send_pan_en_work:	To schedule the sending of the PAN Enable message
 * @send_pan_ack_work:	To schedule the sending of the PAN Ack message
 * @debugfs_dir:	Dentry for debugfs directory "altmode"
 * @response_received:	To detect remote subsystem response failures
 * @ack_port_index:	Port index to ack for a nonexistent client
 */
struct altmode_dev {
	struct device			*dev;
	struct pmic_glink_client	*pgclient;
	struct idr			client_idr;
	struct mutex			client_lock;
	struct list_head		d_node;
	struct list_head		client_list;
	atomic_t			pan_en_sent;
	struct delayed_work		send_pan_en_work;
	struct delayed_work		send_pan_ack_work;
	struct dentry			*debugfs_dir;
	struct completion		response_received;
	u8				ack_port_index;
};

/**
 * struct altmode_client
 *	Definition of a client of an altmode device.
 *
 * @amdev:		Parent altmode device of this client
 * @data:		Supplied by client driver during registration
 * @c_node:		Linked list node for parent altmode device's client list
 * @port_index:		Type-C port index assigned by remote subystem
 *
 * Note: The following members are for internal use only, clients should not
 *	 use them.
 *
 * @client_cb_work:	Work to run client callback
 * @msg:		Latest notify msg stashed to be retrieved in cb_work
 */
struct altmode_client {
	struct altmode_dev		*amdev;
	struct altmode_client_data	data;
	struct list_head		c_node;
	u8				port_index;
	struct work_struct		client_cb_work;
	u8				msg[NOTIFY_PAYLOAD_SIZE];
};

/**
 * struct probe_notify_node
 *	Linked list node to keep track of altmode clients who, by design,
 *	register with the altmode framework before altmode probes.
 *
 * @node:		Linked list node for probe_notify_list
 * @amdev_node:		device_node of the altmode device of the client
 * @cb:			Client's probe completion callback function
 * @priv:		Pointer to client's driver data struct
 */
struct probe_notify_node {
	struct list_head		node;
	struct device_node		*amdev_node;
	void				(*cb)(void *priv);
	void				*priv;
};

/**
 * struct list_head probe_notify_list
 *	List of altmode clients that register to get notified upon probe
 *	completion.
 */
static LIST_HEAD(probe_notify_list);
static DEFINE_MUTEX(notify_lock);

static void altmode_send_pan_ack(struct work_struct *work);
static void *altmode_ipc_log;

static struct altmode_dev *to_altmode_device(struct device_node *amdev_node)
{
	struct platform_device *altmode_pdev;

	altmode_pdev = of_find_device_by_node(amdev_node);
	if (altmode_pdev)
		return platform_get_drvdata(altmode_pdev);

	return NULL;
}

#define ALTMODE_WAIT_MS	1000
static int altmode_write(struct altmode_dev *amdev, void *data, size_t len)
{
	int rc;

	reinit_completion(&amdev->response_received);
	rc = pmic_glink_write(amdev->pgclient, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&amdev->response_received,
				msecs_to_jiffies(ALTMODE_WAIT_MS));
		rc = rc ? 0 : -ETIMEDOUT;
	}

	if (rc)
		pr_err("Error in sending message: %d\n", rc);

	return rc;
}

static int __altmode_send_data(struct altmode_dev *amdev, void *data,
			       size_t len)
{
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

	return altmode_write(amdev, &msg, sizeof(msg));
}

/**
 * altmode_register_notifier()
 *	Register to be notified when altmode device probes.
 *
 * @client_dev:		Client device pointer
 * @cb:			Callback function to execute when altmode device of
 *			interest probes successfully
 * @priv:		Client's private data which is passed back when cb() is
 *			called
 *
 * Returns:		0 upon success, negative upon errors.
 */
int altmode_register_notifier(struct device *client_dev, void (*cb)(void *),
			      void *priv)
{
	int rc;
	struct probe_notify_node *notify_node;
	struct device_node *amdev_node;
	struct of_phandle_args pargs;
	struct altmode_dev *amdev;

	if (!client_dev || !cb || !priv)
		return -EINVAL;

	rc = of_parse_phandle_with_args(client_dev->of_node,
			"qcom,altmode-dev", "#altmode-cells", 0, &pargs);
	if (rc) {
		dev_err(client_dev, "Error parsing qcom,altmode-dev property: %d\n",
				rc);
		return rc;
	}

	amdev_node = pargs.np;

	mutex_lock(&notify_lock);
	amdev = to_altmode_device(amdev_node);
	if (amdev) {
		/*
		 * If altmode device has probed already, notify
		 * immediately.
		 */
		of_node_put(amdev_node);
		cb(priv);
	} else {
		notify_node = kzalloc(sizeof(*notify_node), GFP_KERNEL);
		if (!notify_node) {
			mutex_unlock(&notify_lock);
			return -ENOMEM;
		}

		notify_node->cb = cb;
		notify_node->priv = priv;
		notify_node->amdev_node = amdev_node;

		list_add(&notify_node->node, &probe_notify_list);
	}
	mutex_unlock(&notify_lock);

	return 0;
}
EXPORT_SYMBOL(altmode_register_notifier);

/**
 * altmode_deregister_notifier()
 *	Deregister probe completion notifier.
 *
 * @client_dev:		Client device pointer
 * @priv:		Client's private data
 *
 * Returns:		0 upon success, negative upon errors.
 */
int altmode_deregister_notifier(struct device *client_dev, void *priv)
{
	int rc;
	struct device_node *amdev_node;
	struct probe_notify_node *pos, *tmp;
	struct of_phandle_args pargs;

	if (!client_dev)
		return -EINVAL;

	rc = of_parse_phandle_with_args(client_dev->of_node,
			"qcom,altmode-dev", "#altmode-cells", 0, &pargs);
	if (rc) {
		dev_err(client_dev, "Error parsing qcom,altmode-dev property: %d\n",
				rc);
		return rc;
	}

	amdev_node = pargs.np;

	mutex_lock(&notify_lock);
	list_for_each_entry_safe(pos, tmp, &probe_notify_list, node) {
		if (pos->amdev_node == amdev_node && pos->priv == priv) {
			of_node_put(pos->amdev_node);
			list_del(&pos->node);
			kfree(pos);
			break;
		}
	}
	mutex_unlock(&notify_lock);

	of_node_put(amdev_node);

	return 0;
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

static void client_cb_work(struct work_struct *work)
{
	struct altmode_client *amclient = container_of(work,
			struct altmode_client, client_cb_work);

	amclient->data.callback(amclient->data.priv, amclient->msg,
			sizeof(amclient->msg));
}

/**
 * altmode_register_client()
 *	Register with altmode to receive PMIC GLINK messages from remote
 *	subsystem.
 *
 * @client_dev:		Client device pointer
 * @client_data:	Details identifying altmode client uniquely
 *
 * Returns:		Valid altmode client pointer upon success, ERR_PTRs
 *			upon errors.
 *
 * Notes:		client_data should contain a unique SVID.
 */
struct altmode_client *altmode_register_client(struct device *client_dev,
		const struct altmode_client_data *client_data)
{
	int rc, key;
	struct altmode_dev *amdev;
	struct of_phandle_args pargs;
	struct device_node *amdev_node;
	struct altmode_client *amclient;

	if (!client_dev || !client_data)
		return ERR_PTR(-EINVAL);

	if (!client_data->name || !client_data->priv || !client_data->callback
			|| !client_data->svid)
		return ERR_PTR(-EINVAL);

	rc = of_parse_phandle_with_args(client_dev->of_node,
			"qcom,altmode-dev", "#altmode-cells", 0, &pargs);
	if (rc) {
		dev_err(client_dev, "Error parsing qcom,altmode-dev property: %d\n",
				rc);
		return ERR_PTR(rc);
	}

	if (pargs.args_count != 1) {
		dev_err(client_dev, "Error in port_index specification\n");
		return ERR_PTR(-EINVAL);
	}

	if (pargs.args[0] >= MAX_NUM_PORTS) {
		dev_err(client_dev, "Invalid port_index: %d, max is %d\n",
				pargs.args[0], MAX_NUM_PORTS - 1);
		return ERR_PTR(-EINVAL);
	}

	amdev_node = pargs.np;

	amdev = to_altmode_device(amdev_node);
	of_node_put(amdev_node);
	if (!amdev)
		return ERR_PTR(-EPROBE_DEFER);

	amclient = kzalloc(sizeof(*amclient), GFP_KERNEL);
	if (!amclient)
		return ERR_PTR(-ENOMEM);

	amclient->amdev = amdev;
	amclient->port_index = pargs.args[0];
	amclient->data.svid = client_data->svid;
	amclient->data.priv = client_data->priv;
	amclient->data.callback = client_data->callback;
	INIT_WORK(&amclient->client_cb_work, client_cb_work);
	amclient->data.name = kstrdup(client_data->name, GFP_KERNEL);
	if (!amclient->data.name) {
		kfree(amclient);
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&amdev->client_lock);
	key = IDR_KEY(amclient);
	rc = idr_alloc(&amdev->client_idr, amclient, key, key + 1, GFP_KERNEL);
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

	cancel_work_sync(&client->client_cb_work);

	mutex_lock(&amdev->client_lock);
	idr_remove(&amdev->client_idr, IDR_KEY(client));

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
	altmode_dbg("Sent PAN EN\n");
}

static int altmode_send_ack(struct altmode_dev *amdev, u8 port_index)
{
	int rc;
	struct altmode_pan_ack_msg ack;

	ack.cmd_type = ALTMODE_PAN_ACK;
	ack.port_index = port_index;

	rc = __altmode_send_data(amdev, &ack, sizeof(ack));
	if (rc < 0) {
		pr_err("port %u: Failed to send PAN ACK: %d\n", port_index, rc);
		return rc;
	}

	altmode_dbg("port %u: Sent PAN ACK\n", port_index);

	return rc;
}

static void altmode_state_cb(void *priv, enum pmic_glink_state state)
{
	struct altmode_dev *amdev = priv;

	altmode_dbg("state: %d\n", state);

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

static void altmode_send_pan_ack(struct work_struct *work)
{
	struct altmode_dev *amdev = container_of(work, struct altmode_dev,
			send_pan_ack_work.work);

	altmode_send_ack(amdev, amdev->ack_port_index);
}

#define USBC_NOTIFY_IND_MASK	GENMASK(7, 0)
#define GET_OP(opcode)		(opcode & USBC_NOTIFY_IND_MASK)
#define GET_SVID(opcode)	(opcode >> 16)

static int altmode_callback(void *priv, void *data, size_t len)
{
	u16 svid, op;
	struct usbc_notify_ind_msg *notify_msg;
	struct pmic_glink_hdr *hdr = data;
	struct altmode_dev *amdev = priv;
	struct altmode_client *amclient;
	u8 port_index;

	altmode_dbg("len: %zu owner: %u type: %u opcode %04x\n", len, hdr->owner,
			hdr->type, hdr->opcode);

	/*
	 * For DisplayPort alt mode, the hdr->opcode is designed as follows:
	 *
	 *	hdr->opcode = (svid << 16) | USBC_NOTIFY_IND
	 */
	op = GET_OP(hdr->opcode);
	svid = GET_SVID(hdr->opcode);

	switch (op) {
	case USBC_CMD_WRITE_REQ:
		complete(&amdev->response_received);
		break;
	case USBC_NOTIFY_IND:
		if (len != sizeof(*notify_msg)) {
			altmode_dbg("Expected length %u, got: %zu\n",
					sizeof(*notify_msg), len);
			return -EINVAL;
		}

		notify_msg = data;
		port_index = notify_msg->payload[0];

		mutex_lock(&amdev->client_lock);
		amclient = idr_find(&amdev->client_idr, IDR_KEY_GEN(svid,
					port_index));
		mutex_unlock(&amdev->client_lock);

		if (!amclient) {
			altmode_dbg("No client associated with SVID %#x port %u\n",
					svid, port_index);
			amdev->ack_port_index = port_index;
			schedule_delayed_work(&amdev->send_pan_ack_work,
					msecs_to_jiffies(20));
			return 0;
		}

		altmode_dbg("Payload: %*ph\n", NOTIFY_PAYLOAD_SIZE,
				notify_msg->payload);

		cancel_work_sync(&amclient->client_cb_work);
		memcpy(&amclient->msg, notify_msg->payload,
				sizeof(amclient->msg));
		schedule_work(&amclient->client_cb_work);
		break;
	default:
		break;
	}

	return 0;
}

static void altmode_notify_clients(struct altmode_dev *amdev)
{
	struct altmode_dev *pos_amdev;
	struct probe_notify_node *pos, *tmp;

	mutex_lock(&notify_lock);
	list_for_each_entry_safe(pos, tmp, &probe_notify_list, node) {
		pos_amdev = to_altmode_device(pos->amdev_node);
		if (!pos_amdev)
			continue;

		if (pos_amdev == amdev) {
			pos->cb(pos->priv);
			of_node_put(pos->amdev_node);
			list_del(&pos->node);
			kfree(pos);
		}
	}
	mutex_unlock(&notify_lock);
}

#ifdef CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG
static int pan_en_write(void *data, u64 val)
{
	struct altmode_dev *amdev = data;

	schedule_delayed_work(&amdev->send_pan_en_work,
				msecs_to_jiffies(20));
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(pan_en_fops, NULL, pan_en_write, "%llu\n");

static int send_ack_write(void *data, u64 val)
{
	int rc;
	struct altmode_dev *amdev = data;
	struct altmode_pan_ack_msg ack;

	if (val >= MAX_NUM_PORTS)
		return -EINVAL;

	ack.cmd_type = ALTMODE_PAN_ACK;
	ack.port_index = val;

	rc = __altmode_send_data(amdev, &ack, sizeof(ack));
	if (rc < 0) {
		dev_err(amdev->dev, "port %d: Failed sending PAN ACK: %llu\n",
				val, rc);
		return rc;
	}

	dev_dbg(amdev->dev, "port %llu: Sent PAN ACK via debugfs\n", val);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(send_ack_fops, NULL, send_ack_write, "%llu\n");

static int altmode_setup_debugfs(struct altmode_dev *amdev)
{
	int rc;
	struct dentry *am_dir, *file;

	am_dir = debugfs_create_dir("altmode", NULL);
	if (IS_ERR(am_dir)) {
		rc = PTR_ERR(am_dir);
		dev_err(amdev->dev, "Failed to create altmode directory: %d\n",
				rc);
		return rc;
	}

	file = debugfs_create_file_unsafe("send_pan_en", 0200, am_dir, amdev,
					  &pan_en_fops);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(amdev->dev, "Failed to create send_pan_en: %d\n", rc);
		goto error;
	}

	file = debugfs_create_file_unsafe("send_pan_ack", 0200, am_dir,
					  amdev, &send_ack_fops);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(amdev->dev, "Failed to create send_pan_ack: %d\n", rc);
		goto error;
	}

	amdev->debugfs_dir = am_dir;

	return 0;

error:
	debugfs_remove_recursive(am_dir);
	return rc;
}
#else
static int altmode_setup_debugfs(struct altmode_dev *amdev)
{
	return 0;
}
#endif

static int altmode_probe(struct platform_device *pdev)
{
	int rc;
	struct altmode_dev *amdev;
	struct pmic_glink_client_data pgclient_data = { };
	struct device *dev = &pdev->dev;

	amdev = devm_kzalloc(&pdev->dev, sizeof(*amdev), GFP_KERNEL);
	if (!amdev)
		return -ENOMEM;

	amdev->dev = dev;

	mutex_init(&amdev->client_lock);
	idr_init(&amdev->client_idr);
	init_completion(&amdev->response_received);
	INIT_DELAYED_WORK(&amdev->send_pan_en_work, altmode_send_pan_en);
	INIT_DELAYED_WORK(&amdev->send_pan_ack_work, altmode_send_pan_ack);
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

	rc = altmode_setup_debugfs(amdev);
	if (rc < 0) {
		dev_err(amdev->dev, "Failed to create debugfs: %d\n", rc);
		goto unreg_pmic_glink;
	}

	altmode_ipc_log = ipc_log_context_create(NUM_LOG_PAGES, "altmode", 0);
	if (!altmode_ipc_log)
		dev_warn(dev, "Error in creating ipc_log_context\n");

	altmode_notify_clients(amdev);

	return 0;

unreg_pmic_glink:
	pmic_glink_unregister_client(amdev->pgclient);
error_register:
	idr_destroy(&amdev->client_idr);
	return rc;
}

static int altmode_remove(struct platform_device *pdev)
{
	int rc;
	struct altmode_dev *amdev = platform_get_drvdata(pdev);
	struct altmode_client *client, *tmp;
	struct probe_notify_node *npos, *ntmp;

	debugfs_remove_recursive(amdev->debugfs_dir);

	cancel_delayed_work_sync(&amdev->send_pan_en_work);
	cancel_delayed_work_sync(&amdev->send_pan_ack_work);
	idr_destroy(&amdev->client_idr);
	atomic_set(&amdev->pan_en_sent, 0);

	mutex_lock(&notify_lock);
	list_for_each_entry_safe(npos, ntmp, &probe_notify_list, node) {
		of_node_put(npos->amdev_node);
		list_del(&npos->node);
		kfree(npos);
	}
	mutex_unlock(&notify_lock);

	mutex_lock(&amdev->client_lock);
	list_for_each_entry_safe(client, tmp, &amdev->client_list, c_node)
		list_del(&client->c_node);
	mutex_unlock(&amdev->client_lock);

	ipc_log_context_destroy(altmode_ipc_log);
	altmode_ipc_log = NULL;

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
