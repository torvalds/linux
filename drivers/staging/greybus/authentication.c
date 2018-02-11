// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus Component Authentication Protocol (CAP) Driver.
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 */

#include "greybus.h"

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>

#include "greybus_authentication.h"
#include "firmware.h"

#define CAP_TIMEOUT_MS		1000

/*
 * Number of minor devices this driver supports.
 * There will be exactly one required per Interface.
 */
#define NUM_MINORS		U8_MAX

struct gb_cap {
	struct device		*parent;
	struct gb_connection	*connection;
	struct kref		kref;
	struct list_head	node;
	bool			disabled; /* connection getting disabled */

	struct mutex		mutex;
	struct cdev		cdev;
	struct device		*class_device;
	dev_t			dev_num;
};

static struct class *cap_class;
static dev_t cap_dev_num;
static DEFINE_IDA(cap_minors_map);
static LIST_HEAD(cap_list);
static DEFINE_MUTEX(list_mutex);

static void cap_kref_release(struct kref *kref)
{
	struct gb_cap *cap = container_of(kref, struct gb_cap, kref);

	kfree(cap);
}

/*
 * All users of cap take a reference (from within list_mutex lock), before
 * they get a pointer to play with. And the structure will be freed only after
 * the last user has put the reference to it.
 */
static void put_cap(struct gb_cap *cap)
{
	kref_put(&cap->kref, cap_kref_release);
}

/* Caller must call put_cap() after using struct gb_cap */
static struct gb_cap *get_cap(struct cdev *cdev)
{
	struct gb_cap *cap;

	mutex_lock(&list_mutex);

	list_for_each_entry(cap, &cap_list, node) {
		if (&cap->cdev == cdev) {
			kref_get(&cap->kref);
			goto unlock;
		}
	}

	cap = NULL;

unlock:
	mutex_unlock(&list_mutex);

	return cap;
}

static int cap_get_endpoint_uid(struct gb_cap *cap, u8 *euid)
{
	struct gb_connection *connection = cap->connection;
	struct gb_cap_get_endpoint_uid_response response;
	int ret;

	ret = gb_operation_sync(connection, GB_CAP_TYPE_GET_ENDPOINT_UID, NULL,
				0, &response, sizeof(response));
	if (ret) {
		dev_err(cap->parent, "failed to get endpoint uid (%d)\n", ret);
		return ret;
	}

	memcpy(euid, response.uid, sizeof(response.uid));

	return 0;
}

static int cap_get_ims_certificate(struct gb_cap *cap, u32 class, u32 id,
				   u8 *certificate, u32 *size, u8 *result)
{
	struct gb_connection *connection = cap->connection;
	struct gb_cap_get_ims_certificate_request *request;
	struct gb_cap_get_ims_certificate_response *response;
	size_t max_size = gb_operation_get_payload_size_max(connection);
	struct gb_operation *op;
	int ret;

	op = gb_operation_create_flags(connection,
				       GB_CAP_TYPE_GET_IMS_CERTIFICATE,
				       sizeof(*request), max_size,
				       GB_OPERATION_FLAG_SHORT_RESPONSE,
				       GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	request = op->request->payload;
	request->certificate_class = cpu_to_le32(class);
	request->certificate_id = cpu_to_le32(id);

	ret = gb_operation_request_send_sync(op);
	if (ret) {
		dev_err(cap->parent, "failed to get certificate (%d)\n", ret);
		goto done;
	}

	response = op->response->payload;
	*result = response->result_code;
	*size = op->response->payload_size - sizeof(*response);
	memcpy(certificate, response->certificate, *size);

done:
	gb_operation_put(op);
	return ret;
}

static int cap_authenticate(struct gb_cap *cap, u32 auth_type, u8 *uid,
			    u8 *challenge, u8 *result, u8 *auth_response,
			    u32 *signature_size, u8 *signature)
{
	struct gb_connection *connection = cap->connection;
	struct gb_cap_authenticate_request *request;
	struct gb_cap_authenticate_response *response;
	size_t max_size = gb_operation_get_payload_size_max(connection);
	struct gb_operation *op;
	int ret;

	op = gb_operation_create_flags(connection, GB_CAP_TYPE_AUTHENTICATE,
				       sizeof(*request), max_size,
				       GB_OPERATION_FLAG_SHORT_RESPONSE,
				       GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	request = op->request->payload;
	request->auth_type = cpu_to_le32(auth_type);
	memcpy(request->uid, uid, sizeof(request->uid));
	memcpy(request->challenge, challenge, sizeof(request->challenge));

	ret = gb_operation_request_send_sync(op);
	if (ret) {
		dev_err(cap->parent, "failed to authenticate (%d)\n", ret);
		goto done;
	}

	response = op->response->payload;
	*result = response->result_code;
	*signature_size = op->response->payload_size - sizeof(*response);
	memcpy(auth_response, response->response, sizeof(response->response));
	memcpy(signature, response->signature, *signature_size);

done:
	gb_operation_put(op);
	return ret;
}

/* Char device fops */

static int cap_open(struct inode *inode, struct file *file)
{
	struct gb_cap *cap = get_cap(inode->i_cdev);

	/* cap structure can't get freed until file descriptor is closed */
	if (cap) {
		file->private_data = cap;
		return 0;
	}

	return -ENODEV;
}

static int cap_release(struct inode *inode, struct file *file)
{
	struct gb_cap *cap = file->private_data;

	put_cap(cap);
	return 0;
}

static int cap_ioctl(struct gb_cap *cap, unsigned int cmd,
			 void __user *buf)
{
	struct cap_ioc_get_endpoint_uid endpoint_uid;
	struct cap_ioc_get_ims_certificate *ims_cert;
	struct cap_ioc_authenticate *authenticate;
	size_t size;
	int ret;

	switch (cmd) {
	case CAP_IOC_GET_ENDPOINT_UID:
		ret = cap_get_endpoint_uid(cap, endpoint_uid.uid);
		if (ret)
			return ret;

		if (copy_to_user(buf, &endpoint_uid, sizeof(endpoint_uid)))
			return -EFAULT;

		return 0;
	case CAP_IOC_GET_IMS_CERTIFICATE:
		size = sizeof(*ims_cert);
		ims_cert = memdup_user(buf, size);
		if (IS_ERR(ims_cert))
			return PTR_ERR(ims_cert);

		ret = cap_get_ims_certificate(cap, ims_cert->certificate_class,
					      ims_cert->certificate_id,
					      ims_cert->certificate,
					      &ims_cert->cert_size,
					      &ims_cert->result_code);
		if (!ret && copy_to_user(buf, ims_cert, size))
			ret = -EFAULT;
		kfree(ims_cert);

		return ret;
	case CAP_IOC_AUTHENTICATE:
		size = sizeof(*authenticate);
		authenticate = memdup_user(buf, size);
		if (IS_ERR(authenticate))
			return PTR_ERR(authenticate);

		ret = cap_authenticate(cap, authenticate->auth_type,
				       authenticate->uid,
				       authenticate->challenge,
				       &authenticate->result_code,
				       authenticate->response,
				       &authenticate->signature_size,
				       authenticate->signature);
		if (!ret && copy_to_user(buf, authenticate, size))
			ret = -EFAULT;
		kfree(authenticate);

		return ret;
	default:
		return -ENOTTY;
	}
}

static long cap_ioctl_unlocked(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct gb_cap *cap = file->private_data;
	struct gb_bundle *bundle = cap->connection->bundle;
	int ret = -ENODEV;

	/*
	 * Serialize ioctls.
	 *
	 * We don't want the user to do multiple authentication operations in
	 * parallel.
	 *
	 * This is also used to protect ->disabled, which is used to check if
	 * the connection is getting disconnected, so that we don't start any
	 * new operations.
	 */
	mutex_lock(&cap->mutex);
	if (!cap->disabled) {
		ret = gb_pm_runtime_get_sync(bundle);
		if (!ret) {
			ret = cap_ioctl(cap, cmd, (void __user *)arg);
			gb_pm_runtime_put_autosuspend(bundle);
		}
	}
	mutex_unlock(&cap->mutex);

	return ret;
}

static const struct file_operations cap_fops = {
	.owner		= THIS_MODULE,
	.open		= cap_open,
	.release	= cap_release,
	.unlocked_ioctl	= cap_ioctl_unlocked,
};

int gb_cap_connection_init(struct gb_connection *connection)
{
	struct gb_cap *cap;
	int ret, minor;

	if (!connection)
		return 0;

	cap = kzalloc(sizeof(*cap), GFP_KERNEL);
	if (!cap)
		return -ENOMEM;

	cap->parent = &connection->bundle->dev;
	cap->connection = connection;
	mutex_init(&cap->mutex);
	gb_connection_set_data(connection, cap);
	kref_init(&cap->kref);

	mutex_lock(&list_mutex);
	list_add(&cap->node, &cap_list);
	mutex_unlock(&list_mutex);

	ret = gb_connection_enable(connection);
	if (ret)
		goto err_list_del;

	minor = ida_simple_get(&cap_minors_map, 0, NUM_MINORS, GFP_KERNEL);
	if (minor < 0) {
		ret = minor;
		goto err_connection_disable;
	}

	/* Add a char device to allow userspace to interact with cap */
	cap->dev_num = MKDEV(MAJOR(cap_dev_num), minor);
	cdev_init(&cap->cdev, &cap_fops);

	ret = cdev_add(&cap->cdev, cap->dev_num, 1);
	if (ret)
		goto err_remove_ida;

	/* Add a soft link to the previously added char-dev within the bundle */
	cap->class_device = device_create(cap_class, cap->parent, cap->dev_num,
					  NULL, "gb-authenticate-%d", minor);
	if (IS_ERR(cap->class_device)) {
		ret = PTR_ERR(cap->class_device);
		goto err_del_cdev;
	}

	return 0;

err_del_cdev:
	cdev_del(&cap->cdev);
err_remove_ida:
	ida_simple_remove(&cap_minors_map, minor);
err_connection_disable:
	gb_connection_disable(connection);
err_list_del:
	mutex_lock(&list_mutex);
	list_del(&cap->node);
	mutex_unlock(&list_mutex);

	put_cap(cap);

	return ret;
}

void gb_cap_connection_exit(struct gb_connection *connection)
{
	struct gb_cap *cap;

	if (!connection)
		return;

	cap = gb_connection_get_data(connection);

	device_destroy(cap_class, cap->dev_num);
	cdev_del(&cap->cdev);
	ida_simple_remove(&cap_minors_map, MINOR(cap->dev_num));

	/*
	 * Disallow any new ioctl operations on the char device and wait for
	 * existing ones to finish.
	 */
	mutex_lock(&cap->mutex);
	cap->disabled = true;
	mutex_unlock(&cap->mutex);

	/* All pending greybus operations should have finished by now */
	gb_connection_disable(cap->connection);

	/* Disallow new users to get access to the cap structure */
	mutex_lock(&list_mutex);
	list_del(&cap->node);
	mutex_unlock(&list_mutex);

	/*
	 * All current users of cap would have taken a reference to it by
	 * now, we can drop our reference and wait the last user will get
	 * cap freed.
	 */
	put_cap(cap);
}

int cap_init(void)
{
	int ret;

	cap_class = class_create(THIS_MODULE, "gb_authenticate");
	if (IS_ERR(cap_class))
		return PTR_ERR(cap_class);

	ret = alloc_chrdev_region(&cap_dev_num, 0, NUM_MINORS,
				  "gb_authenticate");
	if (ret)
		goto err_remove_class;

	return 0;

err_remove_class:
	class_destroy(cap_class);
	return ret;
}

void cap_exit(void)
{
	unregister_chrdev_region(cap_dev_num, NUM_MINORS);
	class_destroy(cap_class);
	ida_destroy(&cap_minors_map);
}
