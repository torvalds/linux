// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ipc_logging.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/slab.h>
#include <trace/events/rproc_qcom.h>

#define MODULE_NAME "qsee_ipc_irq_bridge"
#define DEVICE_NAME MODULE_NAME
#define NUM_LOG_PAGES 4

#define QIIB_DBG(x...) do { \
	if (qiib_info->log_ctx) \
		ipc_log_string(qiib_info->log_ctx, x); \
	else \
		pr_debug(x); \
	} while (0)

#define QIIB_ERR(x...) do { \
	pr_err(x); \
	ipc_log_string(qiib_info->log_ctx, x); \
	} while (0)

static void qiib_cleanup(void);

/**
 * qiib_dev - QSEE IPC IRQ bridge device
 * @dev_list:		qiib device list.
 * @i:			Index to this character device.
 * @dev_name:		Device node name used by the clients.
 * @cdev:		structure to the internal character device.
 * @devicep:		Pointer to the qiib class device structure.
 * @poll_wait_queue:	poll thread wait queue.
 * @irq_num:		IRQ number usd for this device.
 * @irq_pending_count:	The number of IRQs pending.
 * @irq_pending_count_lock: Lock to protect @irq_pending_cont.
 * @ssr_name:		Name of the subsystem recognized by the SSR framework.
 * @nb:			SSR Notifier callback.
 * @notifier_handle:	SSR Notifier handle.
 * @in_reset:		Flag to check the SSR state.
 */
struct qiib_dev {
	struct list_head dev_list;
	uint32_t i;

	const char *dev_name;
	struct cdev cdev;
	struct device *devicep;

	wait_queue_head_t poll_wait_queue;

	uint32_t irq_line;
	uint32_t irq_pending_count;
	spinlock_t irq_pending_count_lock;

	const char *ssr_name;
	struct notifier_block nb;
	void *notifier_handle;
	bool in_reset;
};

/**
 * qiib_driver_data - QSEE IPC IRQ bridge driver data
 * @list:		list of all nodes devices.
 * @list_lock:		lock to synchronize the @list access.
 * @nprots:		Number of device nodes.
 * @classp:		Pointer to the device class.
 * @dev_num:		qiib device number.
 * @log_ctx:		pointer to the ipc logging context.
 */
struct qiib_driver_data {
	struct list_head list;
	struct mutex list_lock;

	int nports;
	struct class *classp;
	dev_t dev_num;

	void *log_ctx;
};

static struct qiib_driver_data *qiib_info;

/**
 * qiib_driver_data_init() - Initialize the QIIB driver data.
 *
 * This function used to initialize the driver specific data
 * during the module init.
 *
 * Return:	0 for success, Standard Linux errors
 */
static int qiib_driver_data_init(void)
{
	qiib_info = kzalloc(sizeof(*qiib_info), GFP_KERNEL);
	if (!qiib_info)
		return -ENOMEM;

	INIT_LIST_HEAD(&qiib_info->list);
	mutex_init(&qiib_info->list_lock);

	qiib_info->log_ctx = ipc_log_context_create(NUM_LOG_PAGES,
						"qsee_ipc_irq_bridge", 0);
	if (!qiib_info->log_ctx)
		QIIB_ERR("%s: unable to create logging context\n", __func__);

	return 0;
}

/**
 * qiib_driver_data_deinit() - De-Initialize the QIIB driver data.
 *
 * This function used to de-initialize the driver specific data
 * during the module exit.
 */
static void qiib_driver_data_deinit(void)
{
	if (!qiib_info->log_ctx)
		ipc_log_context_destroy(qiib_info->log_ctx);
	kfree(qiib_info);
	qiib_info = NULL;
}

/**
 * qiib_restart_notifier_cb() - SSR restart notifier callback function
 * @this:	Notifier block used by the SSR framework
 * @code:	The SSR code for which stage of restart is occurring
 * @data:	Structure containing private data - not used here.
 *
 * This function is a callback for the SSR framework. From here we initiate
 * our handling of SSR.
 *
 * Return: Status of SSR handling
 */
static int qiib_restart_notifier_cb(struct notifier_block *this,
				  unsigned long code,
				  void *data)
{
	struct qiib_dev *devp = container_of(this, struct qiib_dev, nb);

	if (code == QCOM_SSR_BEFORE_SHUTDOWN) {
		trace_rproc_qcom_event(devp->ssr_name,
				"QCOM_SSR_BEFORE_POWERUP", "qiib_restart_notifier-enter");
		QIIB_DBG("%s: %s: subsystem restart for %s\n", __func__,
				"QCOM_SSR_BEFORE_SHUTDOWN",
				devp->ssr_name);
		devp->in_reset = true;
		wake_up_interruptible(&devp->poll_wait_queue);
	} else if (code == QCOM_SSR_AFTER_POWERUP) {
		trace_rproc_qcom_event(devp->ssr_name,
				"QCOM_SSR_AFTER_SHUTDOWN", "qiib_restart_notifier-enter");
		QIIB_DBG("%s: %s: subsystem restart for %s\n", __func__,
				"QCOM_SSR_AFTER_POWERUP",
				devp->ssr_name);
		devp->in_reset = false;
	}

	trace_rproc_qcom_event(devp->ssr_name, "qiib_restart_notifier", "exit");
	return NOTIFY_DONE;
}

/**
 * qiib_poll() - poll() syscall for the qiib device
 * @file:	Pointer to the file structure.
 * @wait:	pointer to Poll table.
 *
 * This function is used to poll on the qiib device when
 * userspace client do a poll() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 *
 * Return: POLLIN for interrupt intercepted case and POLLRDHUP for SSR.
 */
static __poll_t qiib_poll(struct file *file, poll_table *wait)
{
	struct qiib_dev *devp = file->private_data;
	__poll_t mask = 0;
	unsigned long flags;

	if (!devp) {
		QIIB_ERR("%s on NULL device\n", __func__);
		return POLLERR;
	}

	if (devp->in_reset)
		return POLLRDHUP;

	poll_wait(file, &devp->poll_wait_queue, wait);
	spin_lock_irqsave(&devp->irq_pending_count_lock, flags);
	if (devp->irq_pending_count) {
		mask |= POLLIN;
		QIIB_DBG("%s set POLLIN on [%s] count[%d]\n",
					__func__, devp->dev_name,
					devp->irq_pending_count);
		devp->irq_pending_count = 0;
	}
	spin_unlock_irqrestore(&devp->irq_pending_count_lock, flags);

	if (devp->in_reset) {
		mask |= POLLRDHUP;
		QIIB_DBG("%s set POLLRDHUP on [%s] count[%d]\n",
					__func__, devp->dev_name,
					devp->irq_pending_count);
	}
	return mask;
}

/**
 * qiib_open() - open() syscall for the qiib device
 * @inode:	Pointer to the inode structure.
 * @file:	Pointer to the file structure.
 *
 * This function is used to open the qiib device when
 * userspace client do a open() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 *
 * Return:	0 for success, Standard Linux errors
 */
static int qiib_open(struct inode *inode, struct file *file)
{
	struct qiib_dev *devp = NULL;

	devp = container_of(inode->i_cdev, struct qiib_dev, cdev);
	if (!devp) {
		QIIB_ERR("%s on NULL device\n", __func__);
		return -EINVAL;
	}
	file->private_data = devp;
	QIIB_DBG("%s on [%s]\n", __func__, devp->dev_name);
	return 0;
}

/**
 * qiib_release() - release operation on qiibdevice
 * @inode:	Pointer to the inode structure.
 * @file:	Pointer to the file structure.
 *
 * This function is used to release the qiib device when
 * userspace client do a close() system call. All input arguments are
 * validated by the virtual file system before calling this function.
 */
static int qiib_release(struct inode *inode, struct file *file)
{
	struct qiib_dev *devp = file->private_data;

	if (!devp) {
		QIIB_ERR("%s on NULL device\n", __func__);
		return -EINVAL;
	}

	QIIB_DBG("%s on [%s]\n", __func__, devp->dev_name);
	return 0;
}

static const struct file_operations qiib_fops = {
	.owner = THIS_MODULE,
	.open = qiib_open,
	.release = qiib_release,
	.poll = qiib_poll,
};

/**
 * qiib_add_device() - Initialize qiib device and add cdev
 * @devp:	pointer to the qiib device.
 * @i:		index of the qiib device.
 *
 * Return:	0 for success, Standard Linux errors
 */
static int qiib_add_device(struct qiib_dev *devp, int i)
{
	int ret = 0;

	devp->i = i;
	init_waitqueue_head(&devp->poll_wait_queue);
	spin_lock_init(&devp->irq_pending_count_lock);

	cdev_init(&devp->cdev, &qiib_fops);
	devp->cdev.owner = THIS_MODULE;

	ret = cdev_add(&devp->cdev, qiib_info->dev_num + i, 1);
	if (IS_ERR_VALUE((unsigned long)ret)) {
		QIIB_ERR("%s: cdev_add() failed for dev [%s] ret:%i\n",
			__func__, devp->dev_name, ret);
		return ret;
	}

	devp->devicep = device_create(qiib_info->classp,
			      NULL,
			      (qiib_info->dev_num + i),
			      NULL,
			      devp->dev_name);

	if (IS_ERR_OR_NULL(devp->devicep)) {
		QIIB_ERR("%s: device_create() failed for dev [%s]\n",
			__func__, devp->dev_name);
		ret = -ENOMEM;
		cdev_del(&devp->cdev);
		return ret;
	}

	mutex_lock(&qiib_info->list_lock);
	list_add(&devp->dev_list, &qiib_info->list);
	mutex_unlock(&qiib_info->list_lock);

	return ret;
}

static irqreturn_t qiib_irq_handler(int irq, void *priv)
{
	struct qiib_dev *devp = priv;
	unsigned long flags;

	spin_lock_irqsave(&devp->irq_pending_count_lock, flags);
	devp->irq_pending_count++;
	spin_unlock_irqrestore(&devp->irq_pending_count_lock, flags);
	wake_up_interruptible(&devp->poll_wait_queue);

	QIIB_DBG("%s name[%s] pend_count[%d]\n", __func__,
				devp->dev_name, devp->irq_pending_count);

	return IRQ_HANDLED;
}

/**
 * qiib_parse_node() - parse node from device tree binding
 * @node:	pointer to device tree node
 * @devp:	pointer to the qiib device
 *
 * Return:	0 on success, -ENODEV on failure.
 */
static int qiib_parse_node(struct device_node *node, struct qiib_dev *devp)
{
	const char *subsys_name;
	const char *dev_name;
	char *key;
	int ret;

	key = "qcom,dev-name";
	ret = of_property_read_string(node, key, &dev_name);
	if (ret) {
		QIIB_ERR("%s: missing key: %s\n", __func__, key);
		return ret;
	}
	QIIB_DBG("%s: %s = %s\n", __func__, key, dev_name);

	key = "label";
	ret = of_property_read_string(node, key, &subsys_name);
	if (ret) {
		QIIB_ERR("%s: missing key: %s\n", __func__, key);
		return ret;
	}
	QIIB_DBG("%s: %s = %s\n", __func__, key, subsys_name);

	devp->dev_name = dev_name;
	devp->ssr_name = subsys_name;

	return ret;
}

static int qiib_init_notifs(struct device_node *node, struct qiib_dev *devp)
{
	struct irq_data *irqtype_data;
	uint32_t irqtype;
	char *key;
	int ret = -ENODEV;

	key = "interrupts";
	devp->irq_line = irq_of_parse_and_map(node, 0);
	if (!devp->irq_line) {
		QIIB_ERR("%s: missing key: %s\n", __func__, key);
		goto missing_key;
	}
	QIIB_DBG("%s: %s = %d\n", __func__, key, devp->irq_line);

	irqtype_data = irq_get_irq_data(devp->irq_line);
	if (!irqtype_data) {
		QIIB_ERR("%s: get irqdata fail:%d\n", __func__, devp->irq_line);
		goto missing_key;
	}
	irqtype = irqd_get_trigger_type(irqtype_data);
	QIIB_DBG("%s: irqtype = %d\n", __func__, irqtype);

	devp->nb.notifier_call = qiib_restart_notifier_cb;
	devp->notifier_handle = qcom_register_ssr_notifier(devp->ssr_name,
								&devp->nb);
	if (IS_ERR_OR_NULL(devp->notifier_handle)) {
		QIIB_ERR("%s: Could not register SSR notifier cb\n", __func__);
		ret = -EINVAL;
		goto missing_key;
	}

	ret = request_irq(devp->irq_line, qiib_irq_handler, irqtype,
			devp->dev_name, devp);
	if (ret < 0) {
		QIIB_ERR("%s: request_irq() failed on %d\n", __func__,
				devp->irq_line);
		goto req_irq_fail;
	}

	return ret;

req_irq_fail:
	qcom_unregister_ssr_notifier(devp->notifier_handle,	&devp->nb);
missing_key:
	return ret;
}

/**
 * qiib_cleanup - cleanup all the resources
 *
 * This function remove all the memory and unregister
 * the char device region.
 */
static void qiib_cleanup(void)
{
	struct qiib_dev *devp;
	struct qiib_dev *index;

	mutex_lock(&qiib_info->list_lock);
	list_for_each_entry_safe(devp, index, &qiib_info->list, dev_list) {
		cdev_del(&devp->cdev);
		list_del(&devp->dev_list);
		device_destroy(qiib_info->classp,
			       MKDEV(MAJOR(qiib_info->dev_num), devp->i));
		if (devp->notifier_handle)
			qcom_unregister_ssr_notifier(devp->notifier_handle,
								&devp->nb);
		kfree(devp);
	}
	mutex_unlock(&qiib_info->list_lock);

	if (!IS_ERR_OR_NULL(qiib_info->classp)) {
		class_destroy(qiib_info->classp);
		qiib_info->classp = NULL;
	}

	unregister_chrdev_region(MAJOR(qiib_info->dev_num), qiib_info->nports);
}

/**
 * qiib_alloc_chrdev_region() - allocate the char device region
 *
 * This function allocate memory for qiib character-device region and
 * create the class.
 */
static int qiib_alloc_chrdev_region(void)
{
	int ret;

	ret = alloc_chrdev_region(&qiib_info->dev_num,
			       0,
			       qiib_info->nports,
			       DEVICE_NAME);
	if (IS_ERR_VALUE((unsigned long)ret)) {
		QIIB_ERR("%s: alloc_chrdev_region() failed ret:%i\n",
			__func__, ret);
		return ret;
	}

	qiib_info->classp = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(qiib_info->classp)) {
		QIIB_ERR("%s: class_create() failed ENOMEM\n", __func__);
		ret = -ENOMEM;
		unregister_chrdev_region(MAJOR(qiib_info->dev_num),
						qiib_info->nports);
		return ret;
	}

	return 0;
}

static int qsee_ipc_irq_bridge_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node;
	struct qiib_dev *devp;
	int i = 0;

	qiib_info->nports = of_get_available_child_count(pdev->dev.of_node);
	if (!qiib_info->nports) {
		QIIB_ERR("%s:Fail nports = %d\n", __func__, qiib_info->nports);
		return -EINVAL;
	}

	ret = qiib_alloc_chrdev_region();
	if (ret) {
		QIIB_ERR("%s: chrdev_region allocation failed ret:%i\n",
			__func__, ret);
		return ret;
	}

	for_each_available_child_of_node(pdev->dev.of_node, node) {
		devp = kzalloc(sizeof(*devp), GFP_KERNEL);
		if (IS_ERR_OR_NULL(devp)) {
			QIIB_ERR("%s:Allocation failed id:%d\n", __func__, i);
			ret = -ENOMEM;
			goto error;
		}

		ret = qiib_parse_node(node, devp);
		if (ret) {
			QIIB_ERR("%s:qiib_parse_node failed %d\n", __func__, i);
			goto error;
		}

		ret = qiib_add_device(devp, i);
		if (ret < 0) {
			QIIB_ERR("%s: add [%s] device failed ret=%d\n",
					__func__, devp->dev_name, ret);
			goto error;
		}

		ret = qiib_init_notifs(node, devp);
		if (ret < 0) {
			QIIB_ERR("%s: qiib_init_notifs failed ret=%d\n",
					__func__, ret);
			goto error;
		}

		i++;
	}

	QIIB_DBG("%s: Driver Initialized.\n", __func__);
	return 0;

error:
	qiib_cleanup();
	return ret;
}

static int qsee_ipc_irq_bridge_remove(struct platform_device *pdev)
{
	qiib_cleanup();
	return 0;
}

static const struct of_device_id qsee_ipc_irq_bridge_match_table[] = {
	{ .compatible = "qcom,qsee-ipc-irq-bridge" },
	{},
};

static struct platform_driver qsee_ipc_irq_bridge_driver = {
	.probe = qsee_ipc_irq_bridge_probe,
	.remove = qsee_ipc_irq_bridge_remove,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = qsee_ipc_irq_bridge_match_table,
	 },
};

static int __init qsee_ipc_irq_bridge_init(void)
{
	int ret;

	ret = qiib_driver_data_init();
	if (ret) {
		QIIB_ERR("%s: driver data init failed %d\n",
			__func__, ret);
		return ret;
	}

	ret = platform_driver_register(&qsee_ipc_irq_bridge_driver);
	if (ret) {
		QIIB_ERR("%s: platform driver register failed %d\n",
			__func__, ret);
		return ret;
	}

	return 0;
}
module_init(qsee_ipc_irq_bridge_init);

static void __exit qsee_ipc_irq_bridge_exit(void)
{
	platform_driver_unregister(&qsee_ipc_irq_bridge_driver);
	qiib_driver_data_deinit();
}
module_exit(qsee_ipc_irq_bridge_exit);
MODULE_SOFTDEP("pre: qcom_ipcc");
MODULE_DESCRIPTION("QSEE IPC interrupt bridge");
MODULE_LICENSE("GPL");
