/*
 * SVC Greybus driver.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/debugfs.h>
#include <linux/input.h>
#include <linux/workqueue.h>

#include "greybus.h"

#define SVC_KEY_ARA_BUTTON	KEY_A

#define SVC_INTF_EJECT_TIMEOUT	9000

struct gb_svc_deferred_request {
	struct work_struct work;
	struct gb_operation *operation;
};


static ssize_t endo_id_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gb_svc *svc = to_gb_svc(dev);

	return sprintf(buf, "0x%04x\n", svc->endo_id);
}
static DEVICE_ATTR_RO(endo_id);

static ssize_t ap_intf_id_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gb_svc *svc = to_gb_svc(dev);

	return sprintf(buf, "%u\n", svc->ap_intf_id);
}
static DEVICE_ATTR_RO(ap_intf_id);


// FIXME
// This is a hack, we need to do this "right" and clean the interface up
// properly, not just forcibly yank the thing out of the system and hope for the
// best.  But for now, people want their modules to come out without having to
// throw the thing to the ground or get out a screwdriver.
static ssize_t intf_eject_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct gb_svc *svc = to_gb_svc(dev);
	unsigned short intf_id;
	int ret;

	ret = kstrtou16(buf, 10, &intf_id);
	if (ret < 0)
		return ret;

	dev_warn(dev, "Forcibly trying to eject interface %d\n", intf_id);

	ret = gb_svc_intf_eject(svc, intf_id);
	if (ret < 0)
		return ret;

	return len;
}
static DEVICE_ATTR_WO(intf_eject);

static ssize_t watchdog_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct gb_svc *svc = to_gb_svc(dev);

	return sprintf(buf, "%s\n",
		       gb_svc_watchdog_enabled(svc) ? "enabled" : "disabled");
}

static ssize_t watchdog_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t len)
{
	struct gb_svc *svc = to_gb_svc(dev);
	int retval;
	bool user_request;

	retval = strtobool(buf, &user_request);
	if (retval)
		return retval;

	if (user_request)
		retval = gb_svc_watchdog_enable(svc);
	else
		retval = gb_svc_watchdog_disable(svc);
	if (retval)
		return retval;
	return len;
}
static DEVICE_ATTR_RW(watchdog);

static int gb_svc_pwrmon_rail_count_get(struct gb_svc *svc, u8 *value)
{
	struct gb_svc_pwrmon_rail_count_get_response response;
	int ret;

	ret = gb_operation_sync(svc->connection,
				GB_SVC_TYPE_PWRMON_RAIL_COUNT_GET, NULL, 0,
				&response, sizeof(response));
	if (ret) {
		dev_err(&svc->dev, "failed to get rail count: %d\n", ret);
		return ret;
	}

	*value = response.rail_count;

	return 0;
}

static int gb_svc_pwrmon_rail_names_get(struct gb_svc *svc,
		struct gb_svc_pwrmon_rail_names_get_response *response,
		size_t bufsize)
{
	int ret;

	ret = gb_operation_sync(svc->connection,
				GB_SVC_TYPE_PWRMON_RAIL_NAMES_GET, NULL, 0,
				response, bufsize);
	if (ret) {
		dev_err(&svc->dev, "failed to get rail names: %d\n", ret);
		return ret;
	}

	return 0;
}

static int gb_svc_pwrmon_sample_get(struct gb_svc *svc, u8 rail_id,
				    u8 measurement_type, u32 *value)
{
	struct gb_svc_pwrmon_sample_get_request request;
	struct gb_svc_pwrmon_sample_get_response response;
	int ret;

	request.rail_id = rail_id;
	request.measurement_type = measurement_type;

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_PWRMON_SAMPLE_GET,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret) {
		dev_err(&svc->dev, "failed to get rail sample: %d\n", ret);
		return ret;
	}

	if (response.result) {
		dev_err(&svc->dev,
			"UniPro error while getting rail power sample (%d %d): %d\n",
			rail_id, measurement_type, response.result);
		switch (response.result) {
		case GB_SVC_PWRMON_GET_SAMPLE_INVAL:
			return -EINVAL;
		case GB_SVC_PWRMON_GET_SAMPLE_NOSUPP:
			return -ENOMSG;
		default:
			return -EIO;
		}
	}

	*value = le32_to_cpu(response.measurement);

	return 0;
}

int gb_svc_pwrmon_intf_sample_get(struct gb_svc *svc, u8 intf_id,
				  u8 measurement_type, u32 *value)
{
	struct gb_svc_pwrmon_intf_sample_get_request request;
	struct gb_svc_pwrmon_intf_sample_get_response response;
	int ret;

	request.intf_id = intf_id;
	request.measurement_type = measurement_type;

	ret = gb_operation_sync(svc->connection,
				GB_SVC_TYPE_PWRMON_INTF_SAMPLE_GET,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret) {
		dev_err(&svc->dev, "failed to get intf sample: %d\n", ret);
		return ret;
	}

	if (response.result) {
		dev_err(&svc->dev,
			"UniPro error while getting intf power sample (%d %d): %d\n",
			intf_id, measurement_type, response.result);
		switch (response.result) {
		case GB_SVC_PWRMON_GET_SAMPLE_INVAL:
			return -EINVAL;
		case GB_SVC_PWRMON_GET_SAMPLE_NOSUPP:
			return -ENOSYS;
		default:
			return -EIO;
		}
	}

	*value = le32_to_cpu(response.measurement);

	return 0;
}

static struct attribute *svc_attrs[] = {
	&dev_attr_endo_id.attr,
	&dev_attr_ap_intf_id.attr,
	&dev_attr_intf_eject.attr,
	&dev_attr_watchdog.attr,
	NULL,
};
ATTRIBUTE_GROUPS(svc);

int gb_svc_intf_device_id(struct gb_svc *svc, u8 intf_id, u8 device_id)
{
	struct gb_svc_intf_device_id_request request;

	request.intf_id = intf_id;
	request.device_id = device_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_DEVICE_ID,
				 &request, sizeof(request), NULL, 0);
}

int gb_svc_intf_eject(struct gb_svc *svc, u8 intf_id)
{
	struct gb_svc_intf_eject_request request;
	int ret;

	request.intf_id = intf_id;

	/*
	 * The pulse width for module release in svc is long so we need to
	 * increase the timeout so the operation will not return to soon.
	 */
	ret = gb_operation_sync_timeout(svc->connection,
					GB_SVC_TYPE_INTF_EJECT, &request,
					sizeof(request), NULL, 0,
					SVC_INTF_EJECT_TIMEOUT);
	if (ret) {
		dev_err(&svc->dev, "failed to eject interface %u\n", intf_id);
		return ret;
	}

	return 0;
}

int gb_svc_dme_peer_get(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 *value)
{
	struct gb_svc_dme_peer_get_request request;
	struct gb_svc_dme_peer_get_response response;
	u16 result;
	int ret;

	request.intf_id = intf_id;
	request.attr = cpu_to_le16(attr);
	request.selector = cpu_to_le16(selector);

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_DME_PEER_GET,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret) {
		dev_err(&svc->dev, "failed to get DME attribute (%u 0x%04x %u): %d\n",
				intf_id, attr, selector, ret);
		return ret;
	}

	result = le16_to_cpu(response.result_code);
	if (result) {
		dev_err(&svc->dev, "UniPro error while getting DME attribute (%u 0x%04x %u): %u\n",
				intf_id, attr, selector, result);
		return -EIO;
	}

	if (value)
		*value = le32_to_cpu(response.attr_value);

	return 0;
}
EXPORT_SYMBOL_GPL(gb_svc_dme_peer_get);

int gb_svc_dme_peer_set(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 value)
{
	struct gb_svc_dme_peer_set_request request;
	struct gb_svc_dme_peer_set_response response;
	u16 result;
	int ret;

	request.intf_id = intf_id;
	request.attr = cpu_to_le16(attr);
	request.selector = cpu_to_le16(selector);
	request.value = cpu_to_le32(value);

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_DME_PEER_SET,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret) {
		dev_err(&svc->dev, "failed to set DME attribute (%u 0x%04x %u %u): %d\n",
				intf_id, attr, selector, value, ret);
		return ret;
	}

	result = le16_to_cpu(response.result_code);
	if (result) {
		dev_err(&svc->dev, "UniPro error while setting DME attribute (%u 0x%04x %u %u): %u\n",
				intf_id, attr, selector, value, result);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gb_svc_dme_peer_set);

int gb_svc_connection_create(struct gb_svc *svc,
				u8 intf1_id, u16 cport1_id,
				u8 intf2_id, u16 cport2_id,
				u8 cport_flags)
{
	struct gb_svc_conn_create_request request;

	request.intf1_id = intf1_id;
	request.cport1_id = cpu_to_le16(cport1_id);
	request.intf2_id = intf2_id;
	request.cport2_id = cpu_to_le16(cport2_id);
	request.tc = 0;		/* TC0 */
	request.flags = cport_flags;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_CONN_CREATE,
				 &request, sizeof(request), NULL, 0);
}
EXPORT_SYMBOL_GPL(gb_svc_connection_create);

void gb_svc_connection_destroy(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
			       u8 intf2_id, u16 cport2_id)
{
	struct gb_svc_conn_destroy_request request;
	struct gb_connection *connection = svc->connection;
	int ret;

	request.intf1_id = intf1_id;
	request.cport1_id = cpu_to_le16(cport1_id);
	request.intf2_id = intf2_id;
	request.cport2_id = cpu_to_le16(cport2_id);

	ret = gb_operation_sync(connection, GB_SVC_TYPE_CONN_DESTROY,
				&request, sizeof(request), NULL, 0);
	if (ret) {
		dev_err(&svc->dev, "failed to destroy connection (%u:%u %u:%u): %d\n",
				intf1_id, cport1_id, intf2_id, cport2_id, ret);
	}
}
EXPORT_SYMBOL_GPL(gb_svc_connection_destroy);

/* Creates bi-directional routes between the devices */
int gb_svc_route_create(struct gb_svc *svc, u8 intf1_id, u8 dev1_id,
			       u8 intf2_id, u8 dev2_id)
{
	struct gb_svc_route_create_request request;

	request.intf1_id = intf1_id;
	request.dev1_id = dev1_id;
	request.intf2_id = intf2_id;
	request.dev2_id = dev2_id;

	return gb_operation_sync(svc->connection, GB_SVC_TYPE_ROUTE_CREATE,
				 &request, sizeof(request), NULL, 0);
}

/* Destroys bi-directional routes between the devices */
void gb_svc_route_destroy(struct gb_svc *svc, u8 intf1_id, u8 intf2_id)
{
	struct gb_svc_route_destroy_request request;
	int ret;

	request.intf1_id = intf1_id;
	request.intf2_id = intf2_id;

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_ROUTE_DESTROY,
				&request, sizeof(request), NULL, 0);
	if (ret) {
		dev_err(&svc->dev, "failed to destroy route (%u %u): %d\n",
				intf1_id, intf2_id, ret);
	}
}

int gb_svc_intf_set_power_mode(struct gb_svc *svc, u8 intf_id, u8 hs_series,
			       u8 tx_mode, u8 tx_gear, u8 tx_nlanes,
			       u8 rx_mode, u8 rx_gear, u8 rx_nlanes,
			       u8 flags, u32 quirks)
{
	struct gb_svc_intf_set_pwrm_request request;
	struct gb_svc_intf_set_pwrm_response response;
	int ret;

	request.intf_id = intf_id;
	request.hs_series = hs_series;
	request.tx_mode = tx_mode;
	request.tx_gear = tx_gear;
	request.tx_nlanes = tx_nlanes;
	request.rx_mode = rx_mode;
	request.rx_gear = rx_gear;
	request.rx_nlanes = rx_nlanes;
	request.flags = flags;
	request.quirks = cpu_to_le32(quirks);

	ret = gb_operation_sync(svc->connection, GB_SVC_TYPE_INTF_SET_PWRM,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret < 0)
		return ret;

	return le16_to_cpu(response.result_code);
}
EXPORT_SYMBOL_GPL(gb_svc_intf_set_power_mode);

int gb_svc_ping(struct gb_svc *svc)
{
	return gb_operation_sync_timeout(svc->connection, GB_SVC_TYPE_PING,
					 NULL, 0, NULL, 0,
					 GB_OPERATION_TIMEOUT_DEFAULT * 2);
}
EXPORT_SYMBOL_GPL(gb_svc_ping);

static int gb_svc_version_request(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_svc *svc = gb_connection_get_data(connection);
	struct gb_protocol_version_request *request;
	struct gb_protocol_version_response *response;

	if (op->request->payload_size < sizeof(*request)) {
		dev_err(&svc->dev, "short version request (%zu < %zu)\n",
				op->request->payload_size,
				sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;

	if (request->major > GB_SVC_VERSION_MAJOR) {
		dev_warn(&svc->dev, "unsupported major version (%u > %u)\n",
				request->major, GB_SVC_VERSION_MAJOR);
		return -ENOTSUPP;
	}

	svc->protocol_major = request->major;
	svc->protocol_minor = request->minor;

	if (!gb_operation_response_alloc(op, sizeof(*response), GFP_KERNEL))
		return -ENOMEM;

	response = op->response->payload;
	response->major = svc->protocol_major;
	response->minor = svc->protocol_minor;

	return 0;
}

static ssize_t pwr_debugfs_voltage_read(struct file *file, char __user *buf,
					size_t len, loff_t *offset)
{
	struct svc_debugfs_pwrmon_rail *pwrmon_rails = file->f_inode->i_private;
	struct gb_svc *svc = pwrmon_rails->svc;
	int ret, desc;
	u32 value;
	char buff[16];

	ret = gb_svc_pwrmon_sample_get(svc, pwrmon_rails->id,
				       GB_SVC_PWRMON_TYPE_VOL, &value);
	if (ret) {
		dev_err(&svc->dev,
			"failed to get voltage sample %u: %d\n",
			pwrmon_rails->id, ret);
		return ret;
	}

	desc = scnprintf(buff, sizeof(buff), "%u\n", value);

	return simple_read_from_buffer(buf, len, offset, buff, desc);
}

static ssize_t pwr_debugfs_current_read(struct file *file, char __user *buf,
					size_t len, loff_t *offset)
{
	struct svc_debugfs_pwrmon_rail *pwrmon_rails = file->f_inode->i_private;
	struct gb_svc *svc = pwrmon_rails->svc;
	int ret, desc;
	u32 value;
	char buff[16];

	ret = gb_svc_pwrmon_sample_get(svc, pwrmon_rails->id,
				       GB_SVC_PWRMON_TYPE_CURR, &value);
	if (ret) {
		dev_err(&svc->dev,
			"failed to get current sample %u: %d\n",
			pwrmon_rails->id, ret);
		return ret;
	}

	desc = scnprintf(buff, sizeof(buff), "%u\n", value);

	return simple_read_from_buffer(buf, len, offset, buff, desc);
}

static ssize_t pwr_debugfs_power_read(struct file *file, char __user *buf,
				      size_t len, loff_t *offset)
{
	struct svc_debugfs_pwrmon_rail *pwrmon_rails = file->f_inode->i_private;
	struct gb_svc *svc = pwrmon_rails->svc;
	int ret, desc;
	u32 value;
	char buff[16];

	ret = gb_svc_pwrmon_sample_get(svc, pwrmon_rails->id,
				       GB_SVC_PWRMON_TYPE_PWR, &value);
	if (ret) {
		dev_err(&svc->dev, "failed to get power sample %u: %d\n",
			pwrmon_rails->id, ret);
		return ret;
	}

	desc = scnprintf(buff, sizeof(buff), "%u\n", value);

	return simple_read_from_buffer(buf, len, offset, buff, desc);
}

static const struct file_operations pwrmon_debugfs_voltage_fops = {
	.read		= pwr_debugfs_voltage_read,
};

static const struct file_operations pwrmon_debugfs_current_fops = {
	.read		= pwr_debugfs_current_read,
};

static const struct file_operations pwrmon_debugfs_power_fops = {
	.read		= pwr_debugfs_power_read,
};

static void svc_pwrmon_debugfs_init(struct gb_svc *svc)
{
	int i;
	size_t bufsize;
	struct dentry *dent;

	dent = debugfs_create_dir("pwrmon", svc->debugfs_dentry);
	if (IS_ERR_OR_NULL(dent))
		return;

	if (gb_svc_pwrmon_rail_count_get(svc, &svc->rail_count))
		goto err_pwrmon_debugfs;

	if (!svc->rail_count || svc->rail_count > GB_SVC_PWRMON_MAX_RAIL_COUNT)
		goto err_pwrmon_debugfs;

	bufsize = GB_SVC_PWRMON_RAIL_NAME_BUFSIZE * svc->rail_count;

	svc->rail_names = kzalloc(bufsize, GFP_KERNEL);
	if (!svc->rail_names)
		goto err_pwrmon_debugfs;

	svc->pwrmon_rails = kcalloc(svc->rail_count, sizeof(*svc->pwrmon_rails),
				    GFP_KERNEL);
	if (!svc->pwrmon_rails)
		goto err_pwrmon_debugfs_free;

	if (gb_svc_pwrmon_rail_names_get(svc, svc->rail_names, bufsize))
		goto err_pwrmon_debugfs_free;

	for (i = 0; i < svc->rail_count; i++) {
		struct dentry *dir;
		struct svc_debugfs_pwrmon_rail *rail = &svc->pwrmon_rails[i];
		char fname[GB_SVC_PWRMON_RAIL_NAME_BUFSIZE];

		snprintf(fname, sizeof(fname), "%s",
			 (char *)&svc->rail_names->name[i]);

		rail->id = i;
		rail->svc = svc;

		dir = debugfs_create_dir(fname, dent);
		debugfs_create_file("voltage_now", S_IRUGO, dir, rail,
				    &pwrmon_debugfs_voltage_fops);
		debugfs_create_file("current_now", S_IRUGO, dir, rail,
				    &pwrmon_debugfs_current_fops);
		debugfs_create_file("power_now", S_IRUGO, dir, rail,
				    &pwrmon_debugfs_power_fops);
	};
	return;

err_pwrmon_debugfs_free:
	kfree(svc->rail_names);
	svc->rail_names = NULL;

	kfree(svc->pwrmon_rails);
	svc->pwrmon_rails = NULL;

err_pwrmon_debugfs:
	debugfs_remove(dent);
}

static void svc_debugfs_init(struct gb_svc *svc)
{
	svc->debugfs_dentry = debugfs_create_dir(dev_name(&svc->dev),
						 gb_debugfs_get());
	svc_pwrmon_debugfs_init(svc);
}

static void svc_debugfs_exit(struct gb_svc *svc)
{
	debugfs_remove_recursive(svc->debugfs_dentry);
	kfree(svc->rail_names);
}

static int gb_svc_hello(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_svc *svc = gb_connection_get_data(connection);
	struct gb_svc_hello_request *hello_request;
	int ret;

	if (op->request->payload_size < sizeof(*hello_request)) {
		dev_warn(&svc->dev, "short hello request (%zu < %zu)\n",
				op->request->payload_size,
				sizeof(*hello_request));
		return -EINVAL;
	}

	hello_request = op->request->payload;
	svc->endo_id = le16_to_cpu(hello_request->endo_id);
	svc->ap_intf_id = hello_request->interface_id;

	ret = device_add(&svc->dev);
	if (ret) {
		dev_err(&svc->dev, "failed to register svc device: %d\n", ret);
		return ret;
	}

	ret = input_register_device(svc->input);
	if (ret) {
		dev_err(&svc->dev, "failed to register input: %d\n", ret);
		device_del(&svc->dev);
		return ret;
	}

	ret = gb_svc_watchdog_create(svc);
	if (ret) {
		dev_err(&svc->dev, "failed to create watchdog: %d\n", ret);
		input_unregister_device(svc->input);
		device_del(&svc->dev);
		return ret;
	}

	svc_debugfs_init(svc);

	return 0;
}

static void gb_svc_process_intf_hotplug(struct gb_operation *operation)
{
	struct gb_svc_intf_hotplug_request *request;
	struct gb_connection *connection = operation->connection;
	struct gb_svc *svc = gb_connection_get_data(connection);
	struct gb_host_device *hd = connection->hd;
	struct gb_interface *intf;
	u8 intf_id;
	int ret;

	/* The request message size has already been verified. */
	request = operation->request->payload;
	intf_id = request->intf_id;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, intf_id);

	intf = gb_interface_find(hd, intf_id);
	if (intf) {
		dev_info(&svc->dev, "mode switch detected on interface %u\n",
				intf_id);

		/* Mark as disconnected to prevent I/O during disable. */
		intf->disconnected = true;
		gb_interface_disable(intf);
		intf->disconnected = false;

		goto enable_interface;
	}

	intf = gb_interface_create(hd, intf_id);
	if (!intf) {
		dev_err(&svc->dev, "failed to create interface %u\n",
				intf_id);
		return;
	}

	ret = gb_interface_activate(intf);
	if (ret) {
		dev_err(&svc->dev, "failed to activate interface %u: %d\n",
				intf_id, ret);
		gb_interface_add(intf);
		return;
	}

	ret = gb_interface_add(intf);
	if (ret)
		goto err_interface_deactivate;

enable_interface:
	ret = gb_interface_enable(intf);
	if (ret) {
		dev_err(&svc->dev, "failed to enable interface %u: %d\n",
				intf_id, ret);
		goto err_interface_deactivate;
	}

	return;

err_interface_deactivate:
	gb_interface_deactivate(intf);
}

static void gb_svc_process_intf_hot_unplug(struct gb_operation *operation)
{
	struct gb_svc *svc = gb_connection_get_data(operation->connection);
	struct gb_svc_intf_hot_unplug_request *request;
	struct gb_host_device *hd = operation->connection->hd;
	struct gb_interface *intf;
	u8 intf_id;

	/* The request message size has already been verified. */
	request = operation->request->payload;
	intf_id = request->intf_id;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, intf_id);

	intf = gb_interface_find(hd, intf_id);
	if (!intf) {
		dev_warn(&svc->dev, "could not find hot-unplug interface %u\n",
				intf_id);
		return;
	}

	/* Mark as disconnected to prevent I/O during disable. */
	intf->disconnected = true;

	gb_interface_disable(intf);
	gb_interface_deactivate(intf);
	gb_interface_remove(intf);
}

static void gb_svc_process_deferred_request(struct work_struct *work)
{
	struct gb_svc_deferred_request *dr;
	struct gb_operation *operation;
	struct gb_svc *svc;
	u8 type;

	dr = container_of(work, struct gb_svc_deferred_request, work);
	operation = dr->operation;
	svc = gb_connection_get_data(operation->connection);
	type = operation->request->header->type;

	switch (type) {
	case GB_SVC_TYPE_INTF_HOTPLUG:
		gb_svc_process_intf_hotplug(operation);
		break;
	case GB_SVC_TYPE_INTF_HOT_UNPLUG:
		gb_svc_process_intf_hot_unplug(operation);
		break;
	default:
		dev_err(&svc->dev, "bad deferred request type: 0x%02x\n", type);
	}

	gb_operation_put(operation);
	kfree(dr);
}

static int gb_svc_queue_deferred_request(struct gb_operation *operation)
{
	struct gb_svc *svc = gb_connection_get_data(operation->connection);
	struct gb_svc_deferred_request *dr;

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	gb_operation_get(operation);

	dr->operation = operation;
	INIT_WORK(&dr->work, gb_svc_process_deferred_request);

	queue_work(svc->wq, &dr->work);

	return 0;
}

/*
 * Bringing up a module can be time consuming, as that may require lots of
 * initialization on the module side. Over that, we may also need to download
 * the firmware first and flash that on the module.
 *
 * In order not to make other svc events wait for all this to finish,
 * handle most of module hotplug stuff outside of the hotplug callback, with
 * help of a workqueue.
 */
static int gb_svc_intf_hotplug_recv(struct gb_operation *op)
{
	struct gb_svc *svc = gb_connection_get_data(op->connection);
	struct gb_svc_intf_hotplug_request *request;

	if (op->request->payload_size < sizeof(*request)) {
		dev_warn(&svc->dev, "short hotplug request received (%zu < %zu)\n",
				op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, request->intf_id);

	return gb_svc_queue_deferred_request(op);
}

static int gb_svc_intf_hot_unplug_recv(struct gb_operation *op)
{
	struct gb_svc *svc = gb_connection_get_data(op->connection);
	struct gb_svc_intf_hot_unplug_request *request;

	if (op->request->payload_size < sizeof(*request)) {
		dev_warn(&svc->dev, "short hot unplug request received (%zu < %zu)\n",
				op->request->payload_size, sizeof(*request));
		return -EINVAL;
	}

	request = op->request->payload;

	dev_dbg(&svc->dev, "%s - id = %u\n", __func__, request->intf_id);

	return gb_svc_queue_deferred_request(op);
}

static int gb_svc_intf_reset_recv(struct gb_operation *op)
{
	struct gb_svc *svc = gb_connection_get_data(op->connection);
	struct gb_message *request = op->request;
	struct gb_svc_intf_reset_request *reset;
	u8 intf_id;

	if (request->payload_size < sizeof(*reset)) {
		dev_warn(&svc->dev, "short reset request received (%zu < %zu)\n",
				request->payload_size, sizeof(*reset));
		return -EINVAL;
	}
	reset = request->payload;

	intf_id = reset->intf_id;

	/* FIXME Reset the interface here */

	return 0;
}

static int gb_svc_key_code_map(struct gb_svc *svc, u16 key_code, u16 *code)
{
	switch (key_code) {
	case GB_KEYCODE_ARA:
		*code = SVC_KEY_ARA_BUTTON;
		break;
	default:
		dev_warn(&svc->dev, "unknown keycode received: %u\n", key_code);
		return -EINVAL;
	}

	return 0;
}

static int gb_svc_key_event_recv(struct gb_operation *op)
{
	struct gb_svc *svc = gb_connection_get_data(op->connection);
	struct gb_message *request = op->request;
	struct gb_svc_key_event_request *key;
	u16 code;
	u8 event;
	int ret;

	if (request->payload_size < sizeof(*key)) {
		dev_warn(&svc->dev, "short key request received (%zu < %zu)\n",
			 request->payload_size, sizeof(*key));
		return -EINVAL;
	}

	key = request->payload;

	ret = gb_svc_key_code_map(svc, le16_to_cpu(key->key_code), &code);
	if (ret < 0)
		return ret;

	event = key->key_event;
	if ((event != GB_SVC_KEY_PRESSED) && (event != GB_SVC_KEY_RELEASED)) {
		dev_warn(&svc->dev, "unknown key event received: %u\n", event);
		return -EINVAL;
	}

	input_report_key(svc->input, code, (event == GB_SVC_KEY_PRESSED));
	input_sync(svc->input);

	return 0;
}

static int gb_svc_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_svc *svc = gb_connection_get_data(connection);
	u8 type = op->type;
	int ret = 0;

	/*
	 * SVC requests need to follow a specific order (at least initially) and
	 * below code takes care of enforcing that. The expected order is:
	 * - PROTOCOL_VERSION
	 * - SVC_HELLO
	 * - Any other request, but the earlier two.
	 *
	 * Incoming requests are guaranteed to be serialized and so we don't
	 * need to protect 'state' for any races.
	 */
	switch (type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		if (svc->state != GB_SVC_STATE_RESET)
			ret = -EINVAL;
		break;
	case GB_SVC_TYPE_SVC_HELLO:
		if (svc->state != GB_SVC_STATE_PROTOCOL_VERSION)
			ret = -EINVAL;
		break;
	default:
		if (svc->state != GB_SVC_STATE_SVC_HELLO)
			ret = -EINVAL;
		break;
	}

	if (ret) {
		dev_warn(&svc->dev, "unexpected request 0x%02x received (state %u)\n",
				type, svc->state);
		return ret;
	}

	switch (type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		ret = gb_svc_version_request(op);
		if (!ret)
			svc->state = GB_SVC_STATE_PROTOCOL_VERSION;
		return ret;
	case GB_SVC_TYPE_SVC_HELLO:
		ret = gb_svc_hello(op);
		if (!ret)
			svc->state = GB_SVC_STATE_SVC_HELLO;
		return ret;
	case GB_SVC_TYPE_INTF_HOTPLUG:
		return gb_svc_intf_hotplug_recv(op);
	case GB_SVC_TYPE_INTF_HOT_UNPLUG:
		return gb_svc_intf_hot_unplug_recv(op);
	case GB_SVC_TYPE_INTF_RESET:
		return gb_svc_intf_reset_recv(op);
	case GB_SVC_TYPE_KEY_EVENT:
		return gb_svc_key_event_recv(op);
	default:
		dev_warn(&svc->dev, "unsupported request 0x%02x\n", type);
		return -EINVAL;
	}
}

static struct input_dev *gb_svc_input_create(struct gb_svc *svc)
{
	struct input_dev *input_dev;

	input_dev = input_allocate_device();
	if (!input_dev)
		return ERR_PTR(-ENOMEM);

	input_dev->name = dev_name(&svc->dev);
	svc->input_phys = kasprintf(GFP_KERNEL, "greybus-%s/input0",
				    input_dev->name);
	if (!svc->input_phys)
		goto err_free_input;

	input_dev->phys = svc->input_phys;
	input_dev->dev.parent = &svc->dev;

	input_set_drvdata(input_dev, svc);

	input_set_capability(input_dev, EV_KEY, SVC_KEY_ARA_BUTTON);

	return input_dev;

err_free_input:
	input_free_device(svc->input);
	return ERR_PTR(-ENOMEM);
}

static void gb_svc_release(struct device *dev)
{
	struct gb_svc *svc = to_gb_svc(dev);

	if (svc->connection)
		gb_connection_destroy(svc->connection);
	ida_destroy(&svc->device_id_map);
	destroy_workqueue(svc->wq);
	kfree(svc->input_phys);
	kfree(svc);
}

struct device_type greybus_svc_type = {
	.name		= "greybus_svc",
	.release	= gb_svc_release,
};

struct gb_svc *gb_svc_create(struct gb_host_device *hd)
{
	struct gb_svc *svc;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return NULL;

	svc->wq = alloc_workqueue("%s:svc", WQ_UNBOUND, 1, dev_name(&hd->dev));
	if (!svc->wq) {
		kfree(svc);
		return NULL;
	}

	svc->dev.parent = &hd->dev;
	svc->dev.bus = &greybus_bus_type;
	svc->dev.type = &greybus_svc_type;
	svc->dev.groups = svc_groups;
	svc->dev.dma_mask = svc->dev.parent->dma_mask;
	device_initialize(&svc->dev);

	dev_set_name(&svc->dev, "%d-svc", hd->bus_id);

	ida_init(&svc->device_id_map);
	svc->state = GB_SVC_STATE_RESET;
	svc->hd = hd;

	svc->input = gb_svc_input_create(svc);
	if (IS_ERR(svc->input)) {
		dev_err(&svc->dev, "failed to create input device: %ld\n",
			PTR_ERR(svc->input));
		goto err_put_device;
	}

	svc->connection = gb_connection_create_static(hd, GB_SVC_CPORT_ID,
						gb_svc_request_handler);
	if (IS_ERR(svc->connection)) {
		dev_err(&svc->dev, "failed to create connection: %ld\n",
				PTR_ERR(svc->connection));
		goto err_free_input;
	}

	gb_connection_set_data(svc->connection, svc);

	return svc;

err_free_input:
	input_free_device(svc->input);
err_put_device:
	put_device(&svc->dev);
	return NULL;
}

int gb_svc_add(struct gb_svc *svc)
{
	int ret;

	/*
	 * The SVC protocol is currently driven by the SVC, so the SVC device
	 * is added from the connection request handler when enough
	 * information has been received.
	 */
	ret = gb_connection_enable(svc->connection);
	if (ret)
		return ret;

	return 0;
}

static void gb_svc_remove_interfaces(struct gb_svc *svc)
{
	struct gb_interface *intf, *tmp;

	list_for_each_entry_safe(intf, tmp, &svc->hd->interfaces, links) {
		gb_interface_disable(intf);
		gb_interface_deactivate(intf);
		gb_interface_remove(intf);
	}
}

void gb_svc_del(struct gb_svc *svc)
{
	gb_connection_disable(svc->connection);

	/*
	 * The SVC device and input device may have been registered
	 * from the request handler.
	 */
	if (device_is_registered(&svc->dev)) {
		svc_debugfs_exit(svc);
		gb_svc_watchdog_destroy(svc);
		input_unregister_device(svc->input);
		device_del(&svc->dev);
	}

	flush_workqueue(svc->wq);

	gb_svc_remove_interfaces(svc);
}

void gb_svc_put(struct gb_svc *svc)
{
	put_device(&svc->dev);
}
