// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus Firmware Management Protocol Driver.
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 */

#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/greybus.h>

#include "firmware.h"
#include "greybus_firmware.h"

#define FW_MGMT_TIMEOUT_MS		1000

struct fw_mgmt {
	struct device		*parent;
	struct gb_connection	*connection;
	struct kref		kref;
	struct list_head	node;

	/* Common id-map for interface and backend firmware requests */
	struct ida		id_map;
	struct mutex		mutex;
	struct completion	completion;
	struct cdev		cdev;
	struct device		*class_device;
	dev_t			dev_num;
	unsigned int		timeout_jiffies;
	bool			disabled; /* connection getting disabled */

	/* Interface Firmware specific fields */
	bool			mode_switch_started;
	bool			intf_fw_loaded;
	u8			intf_fw_request_id;
	u8			intf_fw_status;
	u16			intf_fw_major;
	u16			intf_fw_minor;

	/* Backend Firmware specific fields */
	u8			backend_fw_request_id;
	u8			backend_fw_status;
};

/*
 * Number of minor devices this driver supports.
 * There will be exactly one required per Interface.
 */
#define NUM_MINORS		U8_MAX

static struct class *fw_mgmt_class;
static dev_t fw_mgmt_dev_num;
static DEFINE_IDA(fw_mgmt_minors_map);
static LIST_HEAD(fw_mgmt_list);
static DEFINE_MUTEX(list_mutex);

static void fw_mgmt_kref_release(struct kref *kref)
{
	struct fw_mgmt *fw_mgmt = container_of(kref, struct fw_mgmt, kref);

	ida_destroy(&fw_mgmt->id_map);
	kfree(fw_mgmt);
}

/*
 * All users of fw_mgmt take a reference (from within list_mutex lock), before
 * they get a pointer to play with. And the structure will be freed only after
 * the last user has put the reference to it.
 */
static void put_fw_mgmt(struct fw_mgmt *fw_mgmt)
{
	kref_put(&fw_mgmt->kref, fw_mgmt_kref_release);
}

/* Caller must call put_fw_mgmt() after using struct fw_mgmt */
static struct fw_mgmt *get_fw_mgmt(struct cdev *cdev)
{
	struct fw_mgmt *fw_mgmt;

	mutex_lock(&list_mutex);

	list_for_each_entry(fw_mgmt, &fw_mgmt_list, node) {
		if (&fw_mgmt->cdev == cdev) {
			kref_get(&fw_mgmt->kref);
			goto unlock;
		}
	}

	fw_mgmt = NULL;

unlock:
	mutex_unlock(&list_mutex);

	return fw_mgmt;
}

static int fw_mgmt_interface_fw_version_operation(struct fw_mgmt *fw_mgmt,
		struct fw_mgmt_ioc_get_intf_version *fw_info)
{
	struct gb_connection *connection = fw_mgmt->connection;
	struct gb_fw_mgmt_interface_fw_version_response response;
	int ret;

	ret = gb_operation_sync(connection,
				GB_FW_MGMT_TYPE_INTERFACE_FW_VERSION, NULL, 0,
				&response, sizeof(response));
	if (ret) {
		dev_err(fw_mgmt->parent,
			"failed to get interface firmware version (%d)\n", ret);
		return ret;
	}

	fw_info->major = le16_to_cpu(response.major);
	fw_info->minor = le16_to_cpu(response.minor);

	strncpy(fw_info->firmware_tag, response.firmware_tag,
		GB_FIRMWARE_TAG_MAX_SIZE);

	/*
	 * The firmware-tag should be NULL terminated, otherwise throw error but
	 * don't fail.
	 */
	if (fw_info->firmware_tag[GB_FIRMWARE_TAG_MAX_SIZE - 1] != '\0') {
		dev_err(fw_mgmt->parent,
			"fw-version: firmware-tag is not NULL terminated\n");
		fw_info->firmware_tag[GB_FIRMWARE_TAG_MAX_SIZE - 1] = '\0';
	}

	return 0;
}

static int fw_mgmt_load_and_validate_operation(struct fw_mgmt *fw_mgmt,
					       u8 load_method, const char *tag)
{
	struct gb_fw_mgmt_load_and_validate_fw_request request;
	int ret;

	if (load_method != GB_FW_LOAD_METHOD_UNIPRO &&
	    load_method != GB_FW_LOAD_METHOD_INTERNAL) {
		dev_err(fw_mgmt->parent,
			"invalid load-method (%d)\n", load_method);
		return -EINVAL;
	}

	request.load_method = load_method;
	strncpy(request.firmware_tag, tag, GB_FIRMWARE_TAG_MAX_SIZE);

	/*
	 * The firmware-tag should be NULL terminated, otherwise throw error and
	 * fail.
	 */
	if (request.firmware_tag[GB_FIRMWARE_TAG_MAX_SIZE - 1] != '\0') {
		dev_err(fw_mgmt->parent, "load-and-validate: firmware-tag is not NULL terminated\n");
		return -EINVAL;
	}

	/* Allocate ids from 1 to 255 (u8-max), 0 is an invalid id */
	ret = ida_simple_get(&fw_mgmt->id_map, 1, 256, GFP_KERNEL);
	if (ret < 0) {
		dev_err(fw_mgmt->parent, "failed to allocate request id (%d)\n",
			ret);
		return ret;
	}

	fw_mgmt->intf_fw_request_id = ret;
	fw_mgmt->intf_fw_loaded = false;
	request.request_id = ret;

	ret = gb_operation_sync(fw_mgmt->connection,
				GB_FW_MGMT_TYPE_LOAD_AND_VALIDATE_FW, &request,
				sizeof(request), NULL, 0);
	if (ret) {
		ida_simple_remove(&fw_mgmt->id_map,
				  fw_mgmt->intf_fw_request_id);
		fw_mgmt->intf_fw_request_id = 0;
		dev_err(fw_mgmt->parent,
			"load and validate firmware request failed (%d)\n",
			ret);
		return ret;
	}

	return 0;
}

static int fw_mgmt_interface_fw_loaded_operation(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct fw_mgmt *fw_mgmt = gb_connection_get_data(connection);
	struct gb_fw_mgmt_loaded_fw_request *request;

	/* No pending load and validate request ? */
	if (!fw_mgmt->intf_fw_request_id) {
		dev_err(fw_mgmt->parent,
			"unexpected firmware loaded request received\n");
		return -ENODEV;
	}

	if (op->request->payload_size != sizeof(*request)) {
		dev_err(fw_mgmt->parent, "illegal size of firmware loaded request (%zu != %zu)\n",
			op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;

	/* Invalid request-id ? */
	if (request->request_id != fw_mgmt->intf_fw_request_id) {
		dev_err(fw_mgmt->parent, "invalid request id for firmware loaded request (%02u != %02u)\n",
			fw_mgmt->intf_fw_request_id, request->request_id);
		return -ENODEV;
	}

	ida_simple_remove(&fw_mgmt->id_map, fw_mgmt->intf_fw_request_id);
	fw_mgmt->intf_fw_request_id = 0;
	fw_mgmt->intf_fw_status = request->status;
	fw_mgmt->intf_fw_major = le16_to_cpu(request->major);
	fw_mgmt->intf_fw_minor = le16_to_cpu(request->minor);

	if (fw_mgmt->intf_fw_status == GB_FW_LOAD_STATUS_FAILED)
		dev_err(fw_mgmt->parent,
			"failed to load interface firmware, status:%02x\n",
			fw_mgmt->intf_fw_status);
	else if (fw_mgmt->intf_fw_status == GB_FW_LOAD_STATUS_VALIDATION_FAILED)
		dev_err(fw_mgmt->parent,
			"failed to validate interface firmware, status:%02x\n",
			fw_mgmt->intf_fw_status);
	else
		fw_mgmt->intf_fw_loaded = true;

	complete(&fw_mgmt->completion);

	return 0;
}

static int fw_mgmt_backend_fw_version_operation(struct fw_mgmt *fw_mgmt,
		struct fw_mgmt_ioc_get_backend_version *fw_info)
{
	struct gb_connection *connection = fw_mgmt->connection;
	struct gb_fw_mgmt_backend_fw_version_request request;
	struct gb_fw_mgmt_backend_fw_version_response response;
	int ret;

	strncpy(request.firmware_tag, fw_info->firmware_tag,
		GB_FIRMWARE_TAG_MAX_SIZE);

	/*
	 * The firmware-tag should be NULL terminated, otherwise throw error and
	 * fail.
	 */
	if (request.firmware_tag[GB_FIRMWARE_TAG_MAX_SIZE - 1] != '\0') {
		dev_err(fw_mgmt->parent, "backend-version: firmware-tag is not NULL terminated\n");
		return -EINVAL;
	}

	ret = gb_operation_sync(connection,
				GB_FW_MGMT_TYPE_BACKEND_FW_VERSION, &request,
				sizeof(request), &response, sizeof(response));
	if (ret) {
		dev_err(fw_mgmt->parent, "failed to get version of %s backend firmware (%d)\n",
			fw_info->firmware_tag, ret);
		return ret;
	}

	fw_info->status = response.status;

	/* Reset version as that should be non-zero only for success case */
	fw_info->major = 0;
	fw_info->minor = 0;

	switch (fw_info->status) {
	case GB_FW_BACKEND_VERSION_STATUS_SUCCESS:
		fw_info->major = le16_to_cpu(response.major);
		fw_info->minor = le16_to_cpu(response.minor);
		break;
	case GB_FW_BACKEND_VERSION_STATUS_NOT_AVAILABLE:
	case GB_FW_BACKEND_VERSION_STATUS_RETRY:
		break;
	case GB_FW_BACKEND_VERSION_STATUS_NOT_SUPPORTED:
		dev_err(fw_mgmt->parent,
			"Firmware with tag %s is not supported by Interface\n",
			fw_info->firmware_tag);
		break;
	default:
		dev_err(fw_mgmt->parent, "Invalid status received: %u\n",
			fw_info->status);
	}

	return 0;
}

static int fw_mgmt_backend_fw_update_operation(struct fw_mgmt *fw_mgmt,
					       char *tag)
{
	struct gb_fw_mgmt_backend_fw_update_request request;
	int ret;

	strncpy(request.firmware_tag, tag, GB_FIRMWARE_TAG_MAX_SIZE);

	/*
	 * The firmware-tag should be NULL terminated, otherwise throw error and
	 * fail.
	 */
	if (request.firmware_tag[GB_FIRMWARE_TAG_MAX_SIZE - 1] != '\0') {
		dev_err(fw_mgmt->parent, "backend-update: firmware-tag is not NULL terminated\n");
		return -EINVAL;
	}

	/* Allocate ids from 1 to 255 (u8-max), 0 is an invalid id */
	ret = ida_simple_get(&fw_mgmt->id_map, 1, 256, GFP_KERNEL);
	if (ret < 0) {
		dev_err(fw_mgmt->parent, "failed to allocate request id (%d)\n",
			ret);
		return ret;
	}

	fw_mgmt->backend_fw_request_id = ret;
	request.request_id = ret;

	ret = gb_operation_sync(fw_mgmt->connection,
				GB_FW_MGMT_TYPE_BACKEND_FW_UPDATE, &request,
				sizeof(request), NULL, 0);
	if (ret) {
		ida_simple_remove(&fw_mgmt->id_map,
				  fw_mgmt->backend_fw_request_id);
		fw_mgmt->backend_fw_request_id = 0;
		dev_err(fw_mgmt->parent,
			"backend %s firmware update request failed (%d)\n", tag,
			ret);
		return ret;
	}

	return 0;
}

static int fw_mgmt_backend_fw_updated_operation(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct fw_mgmt *fw_mgmt = gb_connection_get_data(connection);
	struct gb_fw_mgmt_backend_fw_updated_request *request;

	/* No pending load and validate request ? */
	if (!fw_mgmt->backend_fw_request_id) {
		dev_err(fw_mgmt->parent, "unexpected backend firmware updated request received\n");
		return -ENODEV;
	}

	if (op->request->payload_size != sizeof(*request)) {
		dev_err(fw_mgmt->parent, "illegal size of backend firmware updated request (%zu != %zu)\n",
			op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;

	/* Invalid request-id ? */
	if (request->request_id != fw_mgmt->backend_fw_request_id) {
		dev_err(fw_mgmt->parent, "invalid request id for backend firmware updated request (%02u != %02u)\n",
			fw_mgmt->backend_fw_request_id, request->request_id);
		return -ENODEV;
	}

	ida_simple_remove(&fw_mgmt->id_map, fw_mgmt->backend_fw_request_id);
	fw_mgmt->backend_fw_request_id = 0;
	fw_mgmt->backend_fw_status = request->status;

	if ((fw_mgmt->backend_fw_status != GB_FW_BACKEND_FW_STATUS_SUCCESS) &&
	    (fw_mgmt->backend_fw_status != GB_FW_BACKEND_FW_STATUS_RETRY))
		dev_err(fw_mgmt->parent,
			"failed to load backend firmware: %02x\n",
			fw_mgmt->backend_fw_status);

	complete(&fw_mgmt->completion);

	return 0;
}

/* Char device fops */

static int fw_mgmt_open(struct inode *inode, struct file *file)
{
	struct fw_mgmt *fw_mgmt = get_fw_mgmt(inode->i_cdev);

	/* fw_mgmt structure can't get freed until file descriptor is closed */
	if (fw_mgmt) {
		file->private_data = fw_mgmt;
		return 0;
	}

	return -ENODEV;
}

static int fw_mgmt_release(struct inode *inode, struct file *file)
{
	struct fw_mgmt *fw_mgmt = file->private_data;

	put_fw_mgmt(fw_mgmt);
	return 0;
}

static int fw_mgmt_ioctl(struct fw_mgmt *fw_mgmt, unsigned int cmd,
			 void __user *buf)
{
	struct fw_mgmt_ioc_get_intf_version intf_fw_info;
	struct fw_mgmt_ioc_get_backend_version backend_fw_info;
	struct fw_mgmt_ioc_intf_load_and_validate intf_load;
	struct fw_mgmt_ioc_backend_fw_update backend_update;
	unsigned int timeout;
	int ret;

	/* Reject any operations after mode-switch has started */
	if (fw_mgmt->mode_switch_started)
		return -EBUSY;

	switch (cmd) {
	case FW_MGMT_IOC_GET_INTF_FW:
		ret = fw_mgmt_interface_fw_version_operation(fw_mgmt,
							     &intf_fw_info);
		if (ret)
			return ret;

		if (copy_to_user(buf, &intf_fw_info, sizeof(intf_fw_info)))
			return -EFAULT;

		return 0;
	case FW_MGMT_IOC_GET_BACKEND_FW:
		if (copy_from_user(&backend_fw_info, buf,
				   sizeof(backend_fw_info)))
			return -EFAULT;

		ret = fw_mgmt_backend_fw_version_operation(fw_mgmt,
							   &backend_fw_info);
		if (ret)
			return ret;

		if (copy_to_user(buf, &backend_fw_info,
				 sizeof(backend_fw_info)))
			return -EFAULT;

		return 0;
	case FW_MGMT_IOC_INTF_LOAD_AND_VALIDATE:
		if (copy_from_user(&intf_load, buf, sizeof(intf_load)))
			return -EFAULT;

		ret = fw_mgmt_load_and_validate_operation(fw_mgmt,
				intf_load.load_method, intf_load.firmware_tag);
		if (ret)
			return ret;

		if (!wait_for_completion_timeout(&fw_mgmt->completion,
						 fw_mgmt->timeout_jiffies)) {
			dev_err(fw_mgmt->parent, "timed out waiting for firmware load and validation to finish\n");
			return -ETIMEDOUT;
		}

		intf_load.status = fw_mgmt->intf_fw_status;
		intf_load.major = fw_mgmt->intf_fw_major;
		intf_load.minor = fw_mgmt->intf_fw_minor;

		if (copy_to_user(buf, &intf_load, sizeof(intf_load)))
			return -EFAULT;

		return 0;
	case FW_MGMT_IOC_INTF_BACKEND_FW_UPDATE:
		if (copy_from_user(&backend_update, buf,
				   sizeof(backend_update)))
			return -EFAULT;

		ret = fw_mgmt_backend_fw_update_operation(fw_mgmt,
				backend_update.firmware_tag);
		if (ret)
			return ret;

		if (!wait_for_completion_timeout(&fw_mgmt->completion,
						 fw_mgmt->timeout_jiffies)) {
			dev_err(fw_mgmt->parent, "timed out waiting for backend firmware update to finish\n");
			return -ETIMEDOUT;
		}

		backend_update.status = fw_mgmt->backend_fw_status;

		if (copy_to_user(buf, &backend_update, sizeof(backend_update)))
			return -EFAULT;

		return 0;
	case FW_MGMT_IOC_SET_TIMEOUT_MS:
		if (get_user(timeout, (unsigned int __user *)buf))
			return -EFAULT;

		if (!timeout) {
			dev_err(fw_mgmt->parent, "timeout can't be zero\n");
			return -EINVAL;
		}

		fw_mgmt->timeout_jiffies = msecs_to_jiffies(timeout);

		return 0;
	case FW_MGMT_IOC_MODE_SWITCH:
		if (!fw_mgmt->intf_fw_loaded) {
			dev_err(fw_mgmt->parent,
				"Firmware not loaded for mode-switch\n");
			return -EPERM;
		}

		/*
		 * Disallow new ioctls as the fw-core bundle driver is going to
		 * get disconnected soon and the character device will get
		 * removed.
		 */
		fw_mgmt->mode_switch_started = true;

		ret = gb_interface_request_mode_switch(fw_mgmt->connection->intf);
		if (ret) {
			dev_err(fw_mgmt->parent, "Mode-switch failed: %d\n",
				ret);
			fw_mgmt->mode_switch_started = false;
			return ret;
		}

		return 0;
	default:
		return -ENOTTY;
	}
}

static long fw_mgmt_ioctl_unlocked(struct file *file, unsigned int cmd,
				   unsigned long arg)
{
	struct fw_mgmt *fw_mgmt = file->private_data;
	struct gb_bundle *bundle = fw_mgmt->connection->bundle;
	int ret = -ENODEV;

	/*
	 * Serialize ioctls.
	 *
	 * We don't want the user to do few operations in parallel. For example,
	 * updating Interface firmware in parallel for the same Interface. There
	 * is no need to do things in parallel for speed and we can avoid having
	 * complicated code for now.
	 *
	 * This is also used to protect ->disabled, which is used to check if
	 * the connection is getting disconnected, so that we don't start any
	 * new operations.
	 */
	mutex_lock(&fw_mgmt->mutex);
	if (!fw_mgmt->disabled) {
		ret = gb_pm_runtime_get_sync(bundle);
		if (!ret) {
			ret = fw_mgmt_ioctl(fw_mgmt, cmd, (void __user *)arg);
			gb_pm_runtime_put_autosuspend(bundle);
		}
	}
	mutex_unlock(&fw_mgmt->mutex);

	return ret;
}

static const struct file_operations fw_mgmt_fops = {
	.owner		= THIS_MODULE,
	.open		= fw_mgmt_open,
	.release	= fw_mgmt_release,
	.unlocked_ioctl	= fw_mgmt_ioctl_unlocked,
};

int gb_fw_mgmt_request_handler(struct gb_operation *op)
{
	u8 type = op->type;

	switch (type) {
	case GB_FW_MGMT_TYPE_LOADED_FW:
		return fw_mgmt_interface_fw_loaded_operation(op);
	case GB_FW_MGMT_TYPE_BACKEND_FW_UPDATED:
		return fw_mgmt_backend_fw_updated_operation(op);
	default:
		dev_err(&op->connection->bundle->dev,
			"unsupported request: %u\n", type);
		return -EINVAL;
	}
}

int gb_fw_mgmt_connection_init(struct gb_connection *connection)
{
	struct fw_mgmt *fw_mgmt;
	int ret, minor;

	if (!connection)
		return 0;

	fw_mgmt = kzalloc(sizeof(*fw_mgmt), GFP_KERNEL);
	if (!fw_mgmt)
		return -ENOMEM;

	fw_mgmt->parent = &connection->bundle->dev;
	fw_mgmt->timeout_jiffies = msecs_to_jiffies(FW_MGMT_TIMEOUT_MS);
	fw_mgmt->connection = connection;

	gb_connection_set_data(connection, fw_mgmt);
	init_completion(&fw_mgmt->completion);
	ida_init(&fw_mgmt->id_map);
	mutex_init(&fw_mgmt->mutex);
	kref_init(&fw_mgmt->kref);

	mutex_lock(&list_mutex);
	list_add(&fw_mgmt->node, &fw_mgmt_list);
	mutex_unlock(&list_mutex);

	ret = gb_connection_enable(connection);
	if (ret)
		goto err_list_del;

	minor = ida_simple_get(&fw_mgmt_minors_map, 0, NUM_MINORS, GFP_KERNEL);
	if (minor < 0) {
		ret = minor;
		goto err_connection_disable;
	}

	/* Add a char device to allow userspace to interact with fw-mgmt */
	fw_mgmt->dev_num = MKDEV(MAJOR(fw_mgmt_dev_num), minor);
	cdev_init(&fw_mgmt->cdev, &fw_mgmt_fops);

	ret = cdev_add(&fw_mgmt->cdev, fw_mgmt->dev_num, 1);
	if (ret)
		goto err_remove_ida;

	/* Add a soft link to the previously added char-dev within the bundle */
	fw_mgmt->class_device = device_create(fw_mgmt_class, fw_mgmt->parent,
					      fw_mgmt->dev_num, NULL,
					      "gb-fw-mgmt-%d", minor);
	if (IS_ERR(fw_mgmt->class_device)) {
		ret = PTR_ERR(fw_mgmt->class_device);
		goto err_del_cdev;
	}

	return 0;

err_del_cdev:
	cdev_del(&fw_mgmt->cdev);
err_remove_ida:
	ida_simple_remove(&fw_mgmt_minors_map, minor);
err_connection_disable:
	gb_connection_disable(connection);
err_list_del:
	mutex_lock(&list_mutex);
	list_del(&fw_mgmt->node);
	mutex_unlock(&list_mutex);

	put_fw_mgmt(fw_mgmt);

	return ret;
}

void gb_fw_mgmt_connection_exit(struct gb_connection *connection)
{
	struct fw_mgmt *fw_mgmt;

	if (!connection)
		return;

	fw_mgmt = gb_connection_get_data(connection);

	device_destroy(fw_mgmt_class, fw_mgmt->dev_num);
	cdev_del(&fw_mgmt->cdev);
	ida_simple_remove(&fw_mgmt_minors_map, MINOR(fw_mgmt->dev_num));

	/*
	 * Disallow any new ioctl operations on the char device and wait for
	 * existing ones to finish.
	 */
	mutex_lock(&fw_mgmt->mutex);
	fw_mgmt->disabled = true;
	mutex_unlock(&fw_mgmt->mutex);

	/* All pending greybus operations should have finished by now */
	gb_connection_disable(fw_mgmt->connection);

	/* Disallow new users to get access to the fw_mgmt structure */
	mutex_lock(&list_mutex);
	list_del(&fw_mgmt->node);
	mutex_unlock(&list_mutex);

	/*
	 * All current users of fw_mgmt would have taken a reference to it by
	 * now, we can drop our reference and wait the last user will get
	 * fw_mgmt freed.
	 */
	put_fw_mgmt(fw_mgmt);
}

int fw_mgmt_init(void)
{
	int ret;

	fw_mgmt_class = class_create(THIS_MODULE, "gb_fw_mgmt");
	if (IS_ERR(fw_mgmt_class))
		return PTR_ERR(fw_mgmt_class);

	ret = alloc_chrdev_region(&fw_mgmt_dev_num, 0, NUM_MINORS,
				  "gb_fw_mgmt");
	if (ret)
		goto err_remove_class;

	return 0;

err_remove_class:
	class_destroy(fw_mgmt_class);
	return ret;
}

void fw_mgmt_exit(void)
{
	unregister_chrdev_region(fw_mgmt_dev_num, NUM_MINORS);
	class_destroy(fw_mgmt_class);
	ida_destroy(&fw_mgmt_minors_map);
}
