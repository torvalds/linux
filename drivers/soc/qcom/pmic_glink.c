// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"PMIC_GLINK: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/ipc_logging.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/soc/qcom/pmic_glink.h>

#define NUM_LOG_PAGES		10

#define pmic_glink_dbg(pgdev, fmt, ...) \
	do { \
		ipc_log_string(pgdev->ipc_log, fmt, ##__VA_ARGS__); \
		pr_debug(fmt, ##__VA_ARGS__); \
	} while (0)

/**
 * struct pmic_glink_dev - Top level data structure for pmic_glink device
 * @rpdev:		rpmsg device from rpmsg framework
 * @dev:		pmic_glink parent device for all child devices
 * @debugfs_dir:	Debugfs directory handle
 * @channel_name:	Glink channel name used by rpmsg device
 * @ipc_log:		ipc logging handle
 * @client_idr:		idr list for the clients
 * @client_lock:	mutex lock when idr APIs are used on client_idr
 * @rpdev_sem:		read-write semaphore to synchronize glink channel
 *			availability and rpmsg transactions
 * @rx_lock:		spinlock to be used when rx_list is modified
 * @rx_work:		worker for handling rx messages
 * @init_work:		worker to instantiate child devices under pdev
 * @rx_wq:		workqueue for rx messages
 * @rx_list:		list for rx messages
 * @dev_list:		list for pmic_glink_dev_list
 * @state:		indicates when remote subsystem is up/down
 * @prev_state:		previous state of remote subsystem
 * @child_probed:	indicates when the children are probed
 * @log_filter:		message owner filter for logging
 * @log_enable:		enables message logging
 * @client_dev_list:	list of client devices to be notified on state
 *			transition during an SSR or PDR
 * @ssr_nb:		notifier block for subsystem notifier
 * @subsys_name:	subsystem name from which SSR notifications should
 *			be handled and notified to the clients
 * @subsys_handle:	handle to subsystem notifier
 * @pdr_handle:		handle to PDR notifier
 * @pdr_service_name:	protection domain service name
 * @pdr_path_name:	protection domain path name
 * @pdr_state:		protection domain service state
 */
struct pmic_glink_dev {
	struct rpmsg_device	*rpdev;
	struct device		*dev;
	struct dentry		*debugfs_dir;
	const char		*channel_name;
	void			*ipc_log;
	struct idr		client_idr;
	struct mutex		client_lock;
	struct rw_semaphore	rpdev_sem;
	spinlock_t		rx_lock;
	struct work_struct	rx_work;
	struct work_struct	init_work;
	struct workqueue_struct	*rx_wq;
	struct list_head	rx_list;
	struct list_head	dev_list;
	atomic_t		state;
	atomic_t		prev_state;
	bool			child_probed;
	u32			log_filter;
	bool			log_enable;
	struct list_head	client_dev_list;
	struct notifier_block	ssr_nb;
	const char		*subsys_name;
	void			*subsys_handle;
	void			*pdr_handle;
	const char		*pdr_service_name;
	const char		*pdr_path_name;
	atomic_t		pdr_state;
};

/**
 * struct pmic_glink_client - pmic_glink client device
 * @pgdev:	pmic_glink device for the client device
 * @name:	Client name
 * @id:		Unique id for client for communication
 * @lock:	lock for sending data
 * @priv:	private data for client
 * @msg_cb:	callback function for client to receive the messages that
 *		are intended to be delivered to it over PMIC Glink
 * @node:	list node to be added in client_dev_list of pmic_glink device
 * @state_cb:	callback function to notify pmic glink state in the event of
 *		a subsystem restart (SSR) or a protection domain restart (PDR)
 */
struct pmic_glink_client {
	struct pmic_glink_dev	*pgdev;
	const char		*name;
	u32			id;
	struct mutex		lock;
	void			*priv;
	int			(*msg_cb)(void *priv, void *data, size_t len);
	struct list_head	node;
	void			(*state_cb)(void *priv,
					  enum pmic_glink_state state);
};

struct pmic_glink_buf {
	struct list_head	node;
	size_t			len;
	u8			buf[];
};

static LIST_HEAD(pmic_glink_dev_list);
static DEFINE_MUTEX(pmic_glink_dev_lock);

static void pmic_glink_notify_clients(struct pmic_glink_dev *pgdev,
					enum pmic_glink_state state)
{
	struct pmic_glink_client *pos;

	pm_stay_awake(pgdev->dev);

	mutex_lock(&pgdev->client_lock);
	list_for_each_entry(pos, &pgdev->client_dev_list, node)
		pos->state_cb(pos->priv, state);
	mutex_unlock(&pgdev->client_lock);

	pm_relax(pgdev->dev);

	pmic_glink_dbg(pgdev, "state_cb done %d\n", state);
}

static int pmic_glink_ssr_notifier_cb(struct notifier_block *nb,
				unsigned long code, void *data)
{
	struct pmic_glink_dev *pgdev = container_of(nb, struct pmic_glink_dev,
						ssr_nb);

	pmic_glink_dbg(pgdev, "code: %lu\n", code);

	switch (code) {
	case QCOM_SSR_BEFORE_SHUTDOWN:
		atomic_set(&pgdev->prev_state, code);
		pmic_glink_notify_clients(pgdev, PMIC_GLINK_STATE_DOWN);
		break;
	case QCOM_SSR_AFTER_POWERUP:
		/*
		 * Do not notify PMIC Glink clients here but rather from
		 * pmic_glink_init_work which will be run only after rpmsg
		 * driver is probed and Glink communication is up.
		 */
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static void pmic_glink_pdr_notifier_cb(int state, char *service_name,
					void *priv)
{
	struct pmic_glink_dev *pgdev = priv;

	pmic_glink_dbg(pgdev, "PDR state: %x\n", state);

	switch (state) {
	case SERVREG_SERVICE_STATE_DOWN:
		pmic_glink_dbg(pgdev, "PD state down for %s\n",
				pgdev->pdr_service_name);
		pmic_glink_notify_clients(pgdev, PMIC_GLINK_STATE_DOWN);
		atomic_set(&pgdev->pdr_state, state);
		break;
	case SERVREG_SERVICE_STATE_UP:
		/*
		 * Do not notify PMIC Glink clients here but rather from
		 * pmic_glink_init_work which will be run only after rpmsg
		 * driver is probed and Glink communication is up.
		 */
		pmic_glink_dbg(pgdev, "PD state up for %s\n",
				pgdev->pdr_service_name);
		break;
	default:
		break;
	}
}

static struct pmic_glink_dev *get_pmic_glink_from_dev(struct device *dev)
{
	struct pmic_glink_dev *tmp, *pos;

	mutex_lock(&pmic_glink_dev_lock);
	list_for_each_entry_safe(pos, tmp, &pmic_glink_dev_list, dev_list) {
		if (pos->dev == dev) {
			mutex_unlock(&pmic_glink_dev_lock);
			return pos;
		}
	}
	mutex_unlock(&pmic_glink_dev_lock);

	return NULL;
}

static struct pmic_glink_dev *get_pmic_glink_from_rpdev(
						struct rpmsg_device *rpdev)
{
	struct pmic_glink_dev *tmp, *pos;

	mutex_lock(&pmic_glink_dev_lock);
	list_for_each_entry_safe(pos, tmp, &pmic_glink_dev_list, dev_list) {
		if (!strcmp(rpdev->id.name, pos->channel_name)) {
			mutex_unlock(&pmic_glink_dev_lock);
			return pos;
		}
	}
	mutex_unlock(&pmic_glink_dev_lock);

	return NULL;
}

/**
 * pmic_glink_write() - Send data from client to remote subsystem
 *
 * @client: Client device pointer that is registered already
 * @data: Pointer to data that needs to be sent
 * @len: Length of data
 *
 * Return: 0 if success, negative on error.
 */
int pmic_glink_write(struct pmic_glink_client *client, void *data,
			size_t len)
{
	int rc;

	if (!client || !client->pgdev || !client->name)
		return -ENODEV;

	down_read(&client->pgdev->rpdev_sem);

	if (!client->pgdev->rpdev || !atomic_read(&client->pgdev->state)) {
		pr_err("Error in sending data for client %s\n", client->name);
		up_read(&client->pgdev->rpdev_sem);
		return -ENOTCONN;
	}

	mutex_lock(&client->lock);
	rc = rpmsg_send(client->pgdev->rpdev->ept, data, len);
	mutex_unlock(&client->lock);
	up_read(&client->pgdev->rpdev_sem);

	if (rc < 0)
		pr_err("Failed to send data [%*ph] for client %s, rc=%d\n",
			len, data, client->name, rc);

	if (!rc && client->pgdev->log_enable) {
		struct pmic_glink_hdr *hdr = data;

		if (client->pgdev->log_filter == hdr->owner)
			pr_info("Tx data: %*ph\n", len, data);
		else if (client->pgdev->log_filter == 65535)
			pr_info("[%u] Tx data: %*ph\n", hdr->owner, len, data);
	}

	return rc;
}
EXPORT_SYMBOL(pmic_glink_write);

/**
 * pmic_glink_register_client() - Register a PMIC Glink client
 *
 * @dev: Device pointer of child device
 * @client_data: Client device data pointer
 *
 * Return: Valid client pointer upon success or ERR_PTR(-ERRNO)
 *
 * This function should be called by a client with a unique id, name and
 * callback function so that the pmic_glink driver can route the messages
 * to the client.
 */
struct pmic_glink_client *pmic_glink_register_client(struct device *dev,
			const struct pmic_glink_client_data *client_data)
{
	int rc;
	struct pmic_glink_dev *pgdev;
	struct pmic_glink_client *client;

	if (!dev || !dev->parent)
		return ERR_PTR(-ENODEV);

	if (!client_data->id || !client_data->msg_cb || !client_data->name)
		return ERR_PTR(-EINVAL);

	pgdev = get_pmic_glink_from_dev(dev->parent);
	if (!pgdev) {
		pr_err("Failed to get pmic_glink_dev for %s\n",
			client_data->name);
		return ERR_PTR(-ENODEV);
	}

	down_read(&pgdev->rpdev_sem);
	if (!atomic_read(&pgdev->state)) {
		up_read(&pgdev->rpdev_sem);
		pr_err("pmic_glink is not up\n");
		return ERR_PTR(-EPROBE_DEFER);
	}
	up_read(&pgdev->rpdev_sem);

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->name = kstrdup(client_data->name, GFP_KERNEL);
	if (!client->name) {
		kfree(client);
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&client->lock);
	client->id = client_data->id;
	client->msg_cb = client_data->msg_cb;
	client->priv = client_data->priv;
	client->pgdev = pgdev;
	client->state_cb = client_data->state_cb;

	mutex_lock(&pgdev->client_lock);
	rc = idr_alloc(&pgdev->client_idr, client, client->id, client->id + 1,
			GFP_KERNEL);
	if (rc < 0) {
		pr_err("Error in allocating idr for client %s, rc=%d\n",
			client->name, rc);
		mutex_unlock(&pgdev->client_lock);
		kfree(client->name);
		kfree(client);
		return ERR_PTR(rc);
	}

	if (client->state_cb) {
		INIT_LIST_HEAD(&client->node);
		list_add_tail(&client->node, &pgdev->client_dev_list);
	}
	mutex_unlock(&pgdev->client_lock);

	pmic_glink_dbg(pgdev, "Registered client %s\n", client->name);
	return client;
}
EXPORT_SYMBOL(pmic_glink_register_client);

/**
 * pmic_glink_unregister_client() - Unregister a PMIC Glink client
 *
 * @client: Client device pointer that is registered already
 *
 * Return: 0 if success, negative on error.
 *
 * This function should be called by a client when it wants to unregister from
 * pmic_glink driver. Messages will not be routed to client after this is done.
 */
int pmic_glink_unregister_client(struct pmic_glink_client *client)
{
	struct pmic_glink_client *pos, *tmp;

	if (!client || !client->pgdev)
		return -ENODEV;

	mutex_lock(&client->pgdev->client_lock);
	list_for_each_entry_safe(pos, tmp, &client->pgdev->client_dev_list,
				node) {
		if (pos == client)
			list_del(&client->node);
	}
	idr_remove(&client->pgdev->client_idr, client->id);
	mutex_unlock(&client->pgdev->client_lock);

	pmic_glink_dbg(client->pgdev, "Unregistered client %s\n", client->name);
	kfree(client->name);
	kfree(client);
	return 0;
}
EXPORT_SYMBOL(pmic_glink_unregister_client);

static void pmic_glink_rx_callback(struct pmic_glink_dev *pgdev,
					struct pmic_glink_buf *pbuf)
{
	struct pmic_glink_client *client;
	struct pmic_glink_hdr *hdr;

	hdr = (struct pmic_glink_hdr *)pbuf->buf;

	mutex_lock(&pgdev->client_lock);
	client = idr_find(&pgdev->client_idr, hdr->owner);
	mutex_unlock(&pgdev->client_lock);

	if (!client || !client->msg_cb) {
		pr_err("No client present for %u\n", hdr->owner);
		return;
	}

	if (pgdev->log_enable) {
		if (pgdev->log_filter == hdr->owner)
			pr_info("Rx data: %*ph\n", pbuf->len, pbuf->buf);
		else if (pgdev->log_filter == 65535)
			pr_info("[%u] Rx data: %*ph\n", hdr->owner, pbuf->len,
				pbuf->buf);
	}

	client->msg_cb(client->priv, pbuf->buf, pbuf->len);
}

static void pmic_glink_rx_work(struct work_struct *work)
{
	struct pmic_glink_dev *pdev = container_of(work, struct pmic_glink_dev,
						rx_work);
	struct pmic_glink_buf *pbuf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&pdev->rx_lock, flags);
	if (!list_empty(&pdev->rx_list)) {
		list_for_each_entry_safe(pbuf, tmp, &pdev->rx_list, node) {
			spin_unlock_irqrestore(&pdev->rx_lock, flags);
			pmic_glink_rx_callback(pdev, pbuf);
			spin_lock_irqsave(&pdev->rx_lock, flags);
			list_del(&pbuf->node);
			kfree(pbuf);
		}
	}
	spin_unlock_irqrestore(&pdev->rx_lock, flags);
}

static int pmic_glink_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
				int len, void *priv, u32 addr)
{
	struct pmic_glink_dev *pdev = dev_get_drvdata(&rpdev->dev);
	struct pmic_glink_buf *pbuf;
	unsigned long flags;

	if (len < sizeof(struct pmic_glink_hdr)) {
		pr_err("Received length %d less than header size: %zu\n", len,
			sizeof(struct pmic_glink_hdr));
		return -EINVAL;
	}

	pbuf = kzalloc(sizeof(*pbuf) + len, GFP_ATOMIC);
	if (!pbuf)
		return -ENOMEM;

	pbuf->len = len;
	memcpy(pbuf->buf, data, len);

	spin_lock_irqsave(&pdev->rx_lock, flags);
	list_add_tail(&pbuf->node, &pdev->rx_list);
	spin_unlock_irqrestore(&pdev->rx_lock, flags);

	queue_work(pdev->rx_wq, &pdev->rx_work);
	return 0;
}

static void pmic_glink_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct pmic_glink_dev *pgdev = NULL;

	pgdev = get_pmic_glink_from_rpdev(rpdev);
	if (!pgdev) {
		pr_err("Failed to get pmic_glink_dev for %s\n", rpdev->id.name);
		return;
	}

	down_write(&pgdev->rpdev_sem);
	atomic_set(&pgdev->state, 0);
	pgdev->rpdev = NULL;
	up_write(&pgdev->rpdev_sem);
	pmic_glink_dbg(pgdev, "%s removed\n", rpdev->id.name);
}

static int pmic_glink_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct pmic_glink_dev *pgdev = NULL;

	pgdev = get_pmic_glink_from_rpdev(rpdev);
	if (!pgdev) {
		pr_err("Failed to get pmic_glink_dev for %s\n", rpdev->id.name);
		return -EPROBE_DEFER;
	}

	down_write(&pgdev->rpdev_sem);
	dev_set_drvdata(&rpdev->dev, pgdev);
	pgdev->rpdev = rpdev;
	atomic_set(&pgdev->state, 1);
	up_write(&pgdev->rpdev_sem);
	schedule_work(&pgdev->init_work);
	pmic_glink_dbg(pgdev, "%s probed\n", rpdev->id.name);

	return 0;
}

static const struct rpmsg_device_id pmic_glink_rpmsg_match[] = {
	{ "PMIC_RTR_ADSP_APPS" },
	{ "PMIC_LOGS_ADSP_APPS" },
	{}
};

static struct rpmsg_driver pmic_glink_rpmsg_driver = {
	.id_table = pmic_glink_rpmsg_match,
	.probe = pmic_glink_rpmsg_probe,
	.remove = pmic_glink_rpmsg_remove,
	.callback = pmic_glink_rpmsg_callback,
	.drv = {
		.name = "pmic_glink_rpmsg",
	},
};

#ifdef CONFIG_DEBUG_FS
static void pmic_glink_add_debugfs(struct pmic_glink_dev *pgdev)
{
	struct dentry *dir;

	dir = debugfs_create_dir(dev_name(pgdev->dev), NULL);
	if (IS_ERR(dir)) {
		pr_err("Failed to create pmic_glink debugfs directory rc=%d\n",
			PTR_ERR(dir));
		return;
	}

	pgdev->debugfs_dir = dir;
	debugfs_create_u32("filter", 0600, dir, &pgdev->log_filter);
	debugfs_create_bool("enable", 0600, dir, &pgdev->log_enable);
}
#else
static inline void pmic_glink_add_debugfs(struct pmic_glink_dev *pgdev)
{ }
#endif

static void pmic_glink_init_work(struct work_struct *work)
{
	struct pmic_glink_dev *pgdev = container_of(work, struct pmic_glink_dev,
					init_work);
	struct device *dev = pgdev->dev;
	int rc;

	if (atomic_read(&pgdev->pdr_state) == SERVREG_SERVICE_STATE_DOWN ||
	    atomic_read(&pgdev->prev_state) == QCOM_SSR_BEFORE_SHUTDOWN) {
		pmic_glink_notify_clients(pgdev, PMIC_GLINK_STATE_UP);
		atomic_set(&pgdev->pdr_state, SERVREG_SERVICE_STATE_UP);
		atomic_set(&pgdev->prev_state, QCOM_SSR_AFTER_POWERUP);
	}

	if (pgdev->child_probed)
		return;

	rc = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (rc < 0)
		pr_err("Failed to create devices rc=%d\n", rc);
	else
		pgdev->child_probed = true;
}

static void pmic_glink_dev_add(struct pmic_glink_dev *pgdev)
{
	mutex_lock(&pmic_glink_dev_lock);
	list_add(&pgdev->dev_list, &pmic_glink_dev_list);
	mutex_unlock(&pmic_glink_dev_lock);
}

static void pmic_glink_dev_remove(struct pmic_glink_dev *pgdev)
{
	struct pmic_glink_dev *pos, *tmp;

	mutex_lock(&pmic_glink_dev_lock);
	list_for_each_entry_safe(pos, tmp, &pmic_glink_dev_list, dev_list) {
		if (pos == pgdev)
			list_del(&pgdev->dev_list);
	}
	mutex_unlock(&pmic_glink_dev_lock);
}

static int pmic_glink_probe(struct platform_device *pdev)
{
	struct pmic_glink_dev *pgdev;
	struct device *dev = &pdev->dev;
	struct pdr_service *service;
	int rc;

	pgdev = devm_kzalloc(dev, sizeof(*pgdev), GFP_KERNEL);
	if (!pgdev)
		return -ENOMEM;

	rc = of_property_read_string(dev->of_node, "qcom,pmic-glink-channel",
					&pgdev->channel_name);
	if (rc < 0) {
		pr_err("Error in reading qcom,pmic-glink-channel rc=%d\n", rc);
		return rc;
	}

	if (strlen(pgdev->channel_name) > RPMSG_NAME_SIZE) {
		pr_err("pmic glink channel name %s exceeds length\n",
			pgdev->channel_name);
		return -EINVAL;
	}

	of_property_read_string(dev->of_node, "qcom,subsys-name",
				&pgdev->subsys_name);

	if (of_find_property(dev->of_node, "qcom,protection-domain", NULL)) {
		rc = of_property_read_string_index(dev->of_node,
				"qcom,protection-domain", 0,
				&pgdev->pdr_service_name);
		if (rc) {
			pr_err("Failed to get PDR service name rc=%d\n", rc);
			return rc;
		} else if (strlen(pgdev->pdr_service_name) >
				SERVREG_NAME_LENGTH) {
			pr_err("PDR service name %s is too long\n",
				pgdev->pdr_service_name);
			return -EINVAL;
		}

		rc = of_property_read_string_index(dev->of_node,
				"qcom,protection-domain", 1,
				&pgdev->pdr_path_name);
		if (rc) {
			pr_err("Failed to get PDR path name rc=%d\n", rc);
			return rc;
		} else if (strlen(pgdev->pdr_path_name) >
				SERVREG_NAME_LENGTH) {
			pr_err("PDR path name %s is too long\n",
				pgdev->pdr_path_name);
			return -EINVAL;
		}
	}

	pgdev->rx_wq = create_singlethread_workqueue("pmic_glink_rx");
	if (!pgdev->rx_wq) {
		pr_err("Failed to create pmic_glink_rx wq\n");
		return -ENOMEM;
	}

	init_rwsem(&pgdev->rpdev_sem);
	INIT_WORK(&pgdev->rx_work, pmic_glink_rx_work);
	INIT_WORK(&pgdev->init_work, pmic_glink_init_work);
	INIT_LIST_HEAD(&pgdev->client_dev_list);
	INIT_LIST_HEAD(&pgdev->rx_list);
	INIT_LIST_HEAD(&pgdev->dev_list);
	spin_lock_init(&pgdev->rx_lock);
	mutex_init(&pgdev->client_lock);
	idr_init(&pgdev->client_idr);
	atomic_set(&pgdev->prev_state, QCOM_SSR_BEFORE_POWERUP);
	atomic_set(&pgdev->pdr_state, SERVREG_SERVICE_STATE_UNINIT);

	pgdev->ipc_log = ipc_log_context_create(NUM_LOG_PAGES,
						pgdev->channel_name, 0);
	if (!pgdev->ipc_log)
		pr_warn("Error in creating ipc_log\n");

	if (pgdev->subsys_name) {
		pgdev->ssr_nb.notifier_call = pmic_glink_ssr_notifier_cb;
		pgdev->subsys_handle = qcom_register_ssr_notifier(
							pgdev->subsys_name,
							&pgdev->ssr_nb);
		if (IS_ERR(pgdev->subsys_handle)) {
			rc = PTR_ERR(pgdev->subsys_handle);
			pr_err("Failed in qcom_register_ssr_notifier rc=%d\n",
				rc);
			goto error_subsys;
		}
	}

	if (pgdev->pdr_service_name) {
		pgdev->pdr_handle = pdr_handle_alloc(pmic_glink_pdr_notifier_cb,
						pgdev);
		if (IS_ERR(pgdev->pdr_handle)) {
			rc = PTR_ERR(pgdev->pdr_handle);
			if (rc != -EPROBE_DEFER)
				pr_err("Failed in pdr_handle_alloc rc=%d\n",
					rc);
			goto error_service;
		}

		service = pdr_add_lookup(pgdev->pdr_handle,
				pgdev->pdr_service_name, pgdev->pdr_path_name);
		if (IS_ERR(service) && PTR_ERR(service) != -EALREADY) {
			rc = PTR_ERR(service);
			pr_err("Failed in pdr_add_lookup rc=%d\n", rc);
			goto error_pdr;
		}

		pmic_glink_dbg(pgdev, "Registering PDR for path_name: %s service_name: %s\n",
			pgdev->pdr_path_name, pgdev->pdr_service_name);
	}

	dev_set_drvdata(dev, pgdev);
	pgdev->dev = dev;

	pmic_glink_dev_add(pgdev);
	pmic_glink_add_debugfs(pgdev);
	device_init_wakeup(pgdev->dev, true);

	pmic_glink_dbg(pgdev, "%s probed successfully\n", pgdev->channel_name);
	return 0;

error_pdr:
	pdr_handle_release(pgdev->pdr_handle);
error_service:
	qcom_unregister_ssr_notifier(pgdev->subsys_handle, &pgdev->ssr_nb);
error_subsys:
	ipc_log_context_destroy(pgdev->ipc_log);
	idr_destroy(&pgdev->client_idr);
	destroy_workqueue(pgdev->rx_wq);
	return rc;
}

static int pmic_glink_remove(struct platform_device *pdev)
{
	struct pmic_glink_dev *pgdev = dev_get_drvdata(&pdev->dev);

	ipc_log_context_destroy(pgdev->ipc_log);
	pdr_handle_release(pgdev->pdr_handle);
	qcom_unregister_ssr_notifier(pgdev->subsys_handle, &pgdev->ssr_nb);
	device_init_wakeup(pgdev->dev, false);
	debugfs_remove_recursive(pgdev->debugfs_dir);
	flush_workqueue(pgdev->rx_wq);
	destroy_workqueue(pgdev->rx_wq);
	idr_destroy(&pgdev->client_idr);
	of_platform_depopulate(&pdev->dev);
	pgdev->child_probed = false;
	pmic_glink_dev_remove(pgdev);

	return 0;
}

static const struct of_device_id pmic_glink_of_match[] = {
	{ .compatible = "qcom,pmic-glink" },
	{}
};
MODULE_DEVICE_TABLE(of, pmic_glink_of_match);

static struct platform_driver pmic_glink_driver = {
	.probe = pmic_glink_probe,
	.remove = pmic_glink_remove,
	.driver = {
		.name = "pmic_glink",
		.of_match_table = pmic_glink_of_match,
	},
};

static int __init pmic_glink_init(void)
{
	int rc;

	rc = platform_driver_register(&pmic_glink_driver);
	if (rc < 0)
		return rc;

	return register_rpmsg_driver(&pmic_glink_rpmsg_driver);
}
module_init(pmic_glink_init);

static void __exit pmic_glink_exit(void)
{
	unregister_rpmsg_driver(&pmic_glink_rpmsg_driver);
	platform_driver_unregister(&pmic_glink_driver);
}
module_exit(pmic_glink_exit);

MODULE_DESCRIPTION("QTI PMIC Glink driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("qcom,pmic-glink");
