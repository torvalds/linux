/*
 * Greybus Vibrator protocol driver.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include "greybus.h"

struct gb_vibrator_device {
	struct gb_connection	*connection;
	struct device		*dev;
	int			minor;		/* vibrator minor number */
	u8			version_major;
	u8			version_minor;
};

/* Version of the Greybus i2c protocol we support */
#define	GB_VIBRATOR_VERSION_MAJOR		0x00
#define	GB_VIBRATOR_VERSION_MINOR		0x01

/* Greybus Vibrator request types */
#define	GB_VIBRATOR_TYPE_INVALID		0x00
#define	GB_VIBRATOR_TYPE_PROTOCOL_VERSION	0x01
#define	GB_VIBRATOR_TYPE_ON			0x02
#define	GB_VIBRATOR_TYPE_OFF			0x03
#define	GB_VIBRATOR_TYPE_RESPONSE		0x80	/* OR'd with rest */

struct gb_vibrator_proto_version_response {
	__u8	status;
	__u8	major;
	__u8	minor;
};

struct gb_vibrator_on_request {
	__le16	timeout_ms;
};

struct gb_vibrator_simple_response {
	__u8	status;
};

static int request_operation(struct gb_connection *connection, int type,
			     void *response, int response_size)
{
	struct gb_operation *operation;
	struct gb_vibrator_simple_response *fake_request;
	u8 *local_response;
	int ret;

	local_response = kmalloc(response_size, GFP_KERNEL);
	if (!local_response)
		return -ENOMEM;

	operation = gb_operation_create(connection, type, 0, response_size);
	if (!operation) {
		kfree(local_response);
		return -ENOMEM;
	}

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("version operation failed (%d)\n", ret);
		goto out;
	}

	/*
	 * We only want to look at the status, and all requests have the same
	 * layout for where the status is, so cast this to a random request so
	 * we can see the status easier.
	 */
	fake_request = (struct gb_vibrator_simple_response *)local_response;
	if (fake_request->status) {
		gb_connection_err(connection, "response %hhu",
			fake_request->status);
		ret = -EIO;
	} else {
		/* Good request, so copy to the caller's buffer */
		if (response_size && response)
			memcpy(response, local_response, response_size);
	}
out:
	gb_operation_destroy(operation);
	kfree(local_response);

	return ret;
}

/*
 * This request only uses the connection field, and if successful,
 * fills in the major and minor protocol version of the target.
 */
static int get_version(struct gb_vibrator_device *vib)
{
	struct gb_connection *connection = vib->connection;
	struct gb_vibrator_proto_version_response version_request;
	int retval;

	retval = request_operation(connection,
				   GB_VIBRATOR_TYPE_PROTOCOL_VERSION,
				   &version_request, sizeof(version_request));
	if (retval)
		return retval;

	if (version_request.major > GB_VIBRATOR_VERSION_MAJOR) {
		dev_err(&connection->dev,
			"unsupported major version (%hhu > %hhu)\n",
			version_request.major, GB_VIBRATOR_VERSION_MAJOR);
		return -ENOTSUPP;
	}

	vib->version_major = version_request.major;
	vib->version_minor = version_request.minor;
	return 0;
}

static int turn_on(struct gb_vibrator_device *vib, u16 timeout_ms)
{
	struct gb_connection *connection = vib->connection;
	struct gb_operation *operation;
	struct gb_vibrator_on_request *request;
	struct gb_vibrator_simple_response *response;
	int retval;

	operation = gb_operation_create(connection, GB_VIBRATOR_TYPE_ON,
					sizeof(*request), sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request_payload;
	request->timeout_ms = cpu_to_le16(timeout_ms);

	/* Synchronous operation--no callback */
	retval = gb_operation_request_send(operation, NULL);
	if (retval) {
		dev_err(&connection->dev,
			"send data operation failed (%d)\n", retval);
		goto out;
	}

	response = operation->response_payload;
	if (response->status) {
		gb_connection_err(connection, "send data response %hhu",
				  response->status);
		retval = -EIO;
	}
out:
	gb_operation_destroy(operation);

	return retval;
}

static int turn_off(struct gb_vibrator_device *vib)
{
	struct gb_connection *connection = vib->connection;

	return request_operation(connection, GB_VIBRATOR_TYPE_OFF, NULL, 0);
}

static ssize_t timeout_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct gb_vibrator_device *vib = dev_get_drvdata(dev);
	unsigned long val;
	int retval;

	retval = kstrtoul(buf, 10, &val);
	if (retval < 0) {
		dev_err(dev, "could not parse timeout value %d\n", retval);
		return retval;
	}

	if (val < 0)
		return -EINVAL;
	if (val)
		retval = turn_on(vib, (u16)val);
	else
		retval = turn_off(vib);
	if (retval)
		return retval;

	return count;
}
static DEVICE_ATTR_WO(timeout);

static struct attribute *vibrator_attrs[] = {
	&dev_attr_timeout.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vibrator);

static struct class vibrator_class = {
	.name		= "vibrator",
	.owner		= THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
	.dev_groups	= vibrator_groups,
#endif
};

static DEFINE_IDR(minors);

static int gb_vibrator_connection_init(struct gb_connection *connection)
{
	struct gb_vibrator_device *vib;
	struct device *dev;
	int retval;

	vib = kzalloc(sizeof(*vib), GFP_KERNEL);
	if (!vib)
		return -ENOMEM;

	vib->connection = connection;

	retval = get_version(vib);
	if (retval)
		goto error;

	/*
	 * For now we create a device in sysfs for the vibrator, but odds are
	 * there is a "real" device somewhere in the kernel for this, but I
	 * can't find it at the moment...
	 */
	vib->minor = idr_alloc(&minors, vib, 0, 0, GFP_KERNEL);
	if (vib->minor < 0) {
		retval = vib->minor;
		goto error;
	}
	dev = device_create(&vibrator_class, &connection->dev, MKDEV(0, 0), vib,
			    "vibrator%d", vib->minor);
	if (IS_ERR(dev)) {
		retval = -EINVAL;
		goto error;
	}
	vib->dev = dev;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,11,0)
	/*
	 * Newer kernels handle this in a race-free manner, by the dev_groups
	 * field in the struct class up above.  But for older kernels, we need
	 * to "open code this :(
	 */
	retval = sysfs_create_group(&dev->kobj, vibrator_groups[0]);
	if (retval) {
		device_unregister(dev);
		goto error;
	}
#endif

	return 0;

error:
	kfree(vib);
	return retval;
}

static void gb_vibrator_connection_exit(struct gb_connection *connection)
{
	struct gb_vibrator_device *vib = connection->private;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,11,0)
	sysfs_remove_group(&vib->dev->kobj, vibrator_groups[0]);
#endif
	idr_remove(&minors, vib->minor);
	device_unregister(vib->dev);
	kfree(vib);
}

static struct gb_protocol vibrator_protocol = {
	.id			= GREYBUS_PROTOCOL_VIBRATOR,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_vibrator_connection_init,
	.connection_exit	= gb_vibrator_connection_exit,
	.request_recv		= NULL,	/* no incoming requests */
};

bool gb_vibrator_protocol_init(void)
{
	int retval;

	retval = class_register(&vibrator_class);
	if (retval)
		return retval;

	return gb_protocol_register(&vibrator_protocol);
}

void gb_vibrator_protocol_exit(void)
{
	gb_protocol_deregister(&vibrator_protocol);
	class_unregister(&vibrator_class);
}
