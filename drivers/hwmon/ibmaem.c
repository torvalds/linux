// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A hwmon driver for the IBM System Director Active Energy Manager (AEM)
 * temperature/power/energy sensors and capping functionality.
 * Copyright (C) 2008 IBM
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ipmi.h>
#include <linux/module.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/kdev_t.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/math64.h>
#include <linux/time.h>
#include <linux/err.h>

#define REFRESH_INTERVAL	(HZ)
#define IPMI_TIMEOUT		(30 * HZ)
#define DRVNAME			"aem"

#define AEM_NETFN		0x2E

#define AEM_FIND_FW_CMD		0x80
#define AEM_ELEMENT_CMD		0x81
#define AEM_FW_INSTANCE_CMD	0x82

#define AEM_READ_ELEMENT_CFG	0x80
#define AEM_READ_BUFFER		0x81
#define AEM_READ_REGISTER	0x82
#define AEM_WRITE_REGISTER	0x83
#define AEM_SET_REG_MASK	0x84
#define AEM_CLEAR_REG_MASK	0x85
#define AEM_READ_ELEMENT_CFG2	0x86

#define AEM_CONTROL_ELEMENT	0
#define AEM_ENERGY_ELEMENT	1
#define AEM_CLOCK_ELEMENT	4
#define AEM_POWER_CAP_ELEMENT	7
#define AEM_EXHAUST_ELEMENT	9
#define AEM_POWER_ELEMENT	10

#define AEM_MODULE_TYPE_ID	0x0001

#define AEM2_NUM_ENERGY_REGS	2
#define AEM2_NUM_PCAP_REGS	6
#define AEM2_NUM_TEMP_REGS	2
#define AEM2_NUM_SENSORS	14

#define AEM1_NUM_ENERGY_REGS	1
#define AEM1_NUM_SENSORS	3

/* AEM 2.x has more energy registers */
#define AEM_NUM_ENERGY_REGS	AEM2_NUM_ENERGY_REGS
/* AEM 2.x needs more sensor files */
#define AEM_NUM_SENSORS		AEM2_NUM_SENSORS

#define POWER_CAP		0
#define POWER_CAP_MAX_HOTPLUG	1
#define POWER_CAP_MAX		2
#define	POWER_CAP_MIN_WARNING	3
#define POWER_CAP_MIN		4
#define	POWER_AUX		5

#define AEM_DEFAULT_POWER_INTERVAL 1000
#define AEM_MIN_POWER_INTERVAL	200
#define UJ_PER_MJ		1000L

static DEFINE_IDA(aem_ida);

static struct platform_driver aem_driver = {
	.driver = {
		.name = DRVNAME,
		.bus = &platform_bus_type,
	}
};

struct aem_ipmi_data {
	struct completion	read_complete;
	struct ipmi_addr	address;
	struct ipmi_user	*user;
	int			interface;

	struct kernel_ipmi_msg	tx_message;
	long			tx_msgid;

	void			*rx_msg_data;
	unsigned short		rx_msg_len;
	unsigned char		rx_result;
	int			rx_recv_type;

	struct device		*bmc_device;
};

struct aem_ro_sensor_template {
	char *label;
	ssize_t (*show)(struct device *dev,
			struct device_attribute *devattr,
			char *buf);
	int index;
};

struct aem_rw_sensor_template {
	char *label;
	ssize_t (*show)(struct device *dev,
			struct device_attribute *devattr,
			char *buf);
	ssize_t (*set)(struct device *dev,
		       struct device_attribute *devattr,
		       const char *buf, size_t count);
	int index;
};

struct aem_data {
	struct list_head	list;

	struct device		*hwmon_dev;
	struct platform_device	*pdev;
	struct mutex		lock;
	char			valid;
	unsigned long		last_updated;	/* In jiffies */
	u8			ver_major;
	u8			ver_minor;
	u8			module_handle;
	int			id;
	struct aem_ipmi_data	ipmi;

	/* Function and buffer to update sensors */
	void (*update)(struct aem_data *data);
	struct aem_read_sensor_resp *rs_resp;

	/*
	 * AEM 1.x sensors:
	 * Available sensors:
	 * Energy meter
	 * Power meter
	 *
	 * AEM 2.x sensors:
	 * Two energy meters
	 * Two power meters
	 * Two temperature sensors
	 * Six power cap registers
	 */

	/* sysfs attrs */
	struct sensor_device_attribute	sensors[AEM_NUM_SENSORS];

	/* energy use in mJ */
	u64			energy[AEM_NUM_ENERGY_REGS];

	/* power sampling interval in ms */
	unsigned long		power_period[AEM_NUM_ENERGY_REGS];

	/* Everything past here is for AEM2 only */

	/* power caps in dW */
	u16			pcap[AEM2_NUM_PCAP_REGS];

	/* exhaust temperature in C */
	u8			temp[AEM2_NUM_TEMP_REGS];
};

/* Data structures returned by the AEM firmware */
struct aem_iana_id {
	u8			bytes[3];
};
static struct aem_iana_id system_x_id = {
	.bytes = {0x4D, 0x4F, 0x00}
};

/* These are used to find AEM1 instances */
struct aem_find_firmware_req {
	struct aem_iana_id	id;
	u8			rsvd;
	__be16			index;
	__be16			module_type_id;
} __packed;

struct aem_find_firmware_resp {
	struct aem_iana_id	id;
	u8			num_instances;
} __packed;

/* These are used to find AEM2 instances */
struct aem_find_instance_req {
	struct aem_iana_id	id;
	u8			instance_number;
	__be16			module_type_id;
} __packed;

struct aem_find_instance_resp {
	struct aem_iana_id	id;
	u8			num_instances;
	u8			major;
	u8			minor;
	u8			module_handle;
	u16			record_id;
} __packed;

/* These are used to query sensors */
struct aem_read_sensor_req {
	struct aem_iana_id	id;
	u8			module_handle;
	u8			element;
	u8			subcommand;
	u8			reg;
	u8			rx_buf_size;
} __packed;

struct aem_read_sensor_resp {
	struct aem_iana_id	id;
	u8			bytes[0];
} __packed;

/* Data structures to talk to the IPMI layer */
struct aem_driver_data {
	struct list_head	aem_devices;
	struct ipmi_smi_watcher	bmc_events;
	struct ipmi_user_hndl	ipmi_hndlrs;
};

static void aem_register_bmc(int iface, struct device *dev);
static void aem_bmc_gone(int iface);
static void aem_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data);

static void aem_remove_sensors(struct aem_data *data);
static int aem1_find_sensors(struct aem_data *data);
static int aem2_find_sensors(struct aem_data *data);
static void update_aem1_sensors(struct aem_data *data);
static void update_aem2_sensors(struct aem_data *data);

static struct aem_driver_data driver_data = {
	.aem_devices = LIST_HEAD_INIT(driver_data.aem_devices),
	.bmc_events = {
		.owner = THIS_MODULE,
		.new_smi = aem_register_bmc,
		.smi_gone = aem_bmc_gone,
	},
	.ipmi_hndlrs = {
		.ipmi_recv_hndl = aem_msg_handler,
	},
};

/* Functions to talk to the IPMI layer */

/* Initialize IPMI address, message buffers and user data */
static int aem_init_ipmi_data(struct aem_ipmi_data *data, int iface,
			      struct device *bmc)
{
	int err;

	init_completion(&data->read_complete);
	data->bmc_device = bmc;

	/* Initialize IPMI address */
	data->address.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	data->address.channel = IPMI_BMC_CHANNEL;
	data->address.data[0] = 0;
	data->interface = iface;

	/* Initialize message buffers */
	data->tx_msgid = 0;
	data->tx_message.netfn = AEM_NETFN;

	/* Create IPMI messaging interface user */
	err = ipmi_create_user(data->interface, &driver_data.ipmi_hndlrs,
			       data, &data->user);
	if (err < 0) {
		dev_err(bmc,
			"Unable to register user with IPMI interface %d\n",
			data->interface);
		return err;
	}

	return 0;
}

/* Send an IPMI command */
static int aem_send_message(struct aem_ipmi_data *data)
{
	int err;

	err = ipmi_validate_addr(&data->address, sizeof(data->address));
	if (err)
		goto out;

	data->tx_msgid++;
	err = ipmi_request_settime(data->user, &data->address, data->tx_msgid,
				   &data->tx_message, data, 0, 0, 0);
	if (err)
		goto out1;

	return 0;
out1:
	dev_err(data->bmc_device, "request_settime=%x\n", err);
	return err;
out:
	dev_err(data->bmc_device, "validate_addr=%x\n", err);
	return err;
}

/* Dispatch IPMI messages to callers */
static void aem_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data)
{
	unsigned short rx_len;
	struct aem_ipmi_data *data = user_msg_data;

	if (msg->msgid != data->tx_msgid) {
		dev_err(data->bmc_device,
			"Mismatch between received msgid (%02x) and transmitted msgid (%02x)!\n",
			(int)msg->msgid,
			(int)data->tx_msgid);
		ipmi_free_recv_msg(msg);
		return;
	}

	data->rx_recv_type = msg->recv_type;
	if (msg->msg.data_len > 0)
		data->rx_result = msg->msg.data[0];
	else
		data->rx_result = IPMI_UNKNOWN_ERR_COMPLETION_CODE;

	if (msg->msg.data_len > 1) {
		rx_len = msg->msg.data_len - 1;
		if (data->rx_msg_len < rx_len)
			rx_len = data->rx_msg_len;
		data->rx_msg_len = rx_len;
		memcpy(data->rx_msg_data, msg->msg.data + 1, data->rx_msg_len);
	} else
		data->rx_msg_len = 0;

	ipmi_free_recv_msg(msg);
	complete(&data->read_complete);
}

/* Sensor support functions */

/* Read a sensor value; must be called with data->lock held */
static int aem_read_sensor(struct aem_data *data, u8 elt, u8 reg,
			   void *buf, size_t size)
{
	int rs_size, res;
	struct aem_read_sensor_req rs_req;
	/* Use preallocated rx buffer */
	struct aem_read_sensor_resp *rs_resp = data->rs_resp;
	struct aem_ipmi_data *ipmi = &data->ipmi;

	/* AEM registers are 1, 2, 4 or 8 bytes */
	switch (size) {
	case 1:
	case 2:
	case 4:
	case 8:
		break;
	default:
		return -EINVAL;
	}

	rs_req.id = system_x_id;
	rs_req.module_handle = data->module_handle;
	rs_req.element = elt;
	rs_req.subcommand = AEM_READ_REGISTER;
	rs_req.reg = reg;
	rs_req.rx_buf_size = size;

	ipmi->tx_message.cmd = AEM_ELEMENT_CMD;
	ipmi->tx_message.data = (char *)&rs_req;
	ipmi->tx_message.data_len = sizeof(rs_req);

	rs_size = sizeof(*rs_resp) + size;
	ipmi->rx_msg_data = rs_resp;
	ipmi->rx_msg_len = rs_size;

	aem_send_message(ipmi);

	res = wait_for_completion_timeout(&ipmi->read_complete, IPMI_TIMEOUT);
	if (!res) {
		res = -ETIMEDOUT;
		goto out;
	}

	if (ipmi->rx_result || ipmi->rx_msg_len != rs_size ||
	    memcmp(&rs_resp->id, &system_x_id, sizeof(system_x_id))) {
		res = -ENOENT;
		goto out;
	}

	switch (size) {
	case 1: {
		u8 *x = buf;
		*x = rs_resp->bytes[0];
		break;
	}
	case 2: {
		u16 *x = buf;
		*x = be16_to_cpup((__be16 *)rs_resp->bytes);
		break;
	}
	case 4: {
		u32 *x = buf;
		*x = be32_to_cpup((__be32 *)rs_resp->bytes);
		break;
	}
	case 8: {
		u64 *x = buf;
		*x = be64_to_cpup((__be64 *)rs_resp->bytes);
		break;
	}
	}
	res = 0;

out:
	return res;
}

/* Update AEM energy registers */
static void update_aem_energy_one(struct aem_data *data, int which)
{
	aem_read_sensor(data, AEM_ENERGY_ELEMENT, which,
			&data->energy[which], 8);
}

static void update_aem_energy(struct aem_data *data)
{
	update_aem_energy_one(data, 0);
	if (data->ver_major < 2)
		return;
	update_aem_energy_one(data, 1);
}

/* Update all AEM1 sensors */
static void update_aem1_sensors(struct aem_data *data)
{
	mutex_lock(&data->lock);
	if (time_before(jiffies, data->last_updated + REFRESH_INTERVAL) &&
	    data->valid)
		goto out;

	update_aem_energy(data);
out:
	mutex_unlock(&data->lock);
}

/* Update all AEM2 sensors */
static void update_aem2_sensors(struct aem_data *data)
{
	int i;

	mutex_lock(&data->lock);
	if (time_before(jiffies, data->last_updated + REFRESH_INTERVAL) &&
	    data->valid)
		goto out;

	update_aem_energy(data);
	aem_read_sensor(data, AEM_EXHAUST_ELEMENT, 0, &data->temp[0], 1);
	aem_read_sensor(data, AEM_EXHAUST_ELEMENT, 1, &data->temp[1], 1);

	for (i = POWER_CAP; i <= POWER_AUX; i++)
		aem_read_sensor(data, AEM_POWER_CAP_ELEMENT, i,
				&data->pcap[i], 2);
out:
	mutex_unlock(&data->lock);
}

/* Delete an AEM instance */
static void aem_delete(struct aem_data *data)
{
	list_del(&data->list);
	aem_remove_sensors(data);
	kfree(data->rs_resp);
	hwmon_device_unregister(data->hwmon_dev);
	ipmi_destroy_user(data->ipmi.user);
	platform_set_drvdata(data->pdev, NULL);
	platform_device_unregister(data->pdev);
	ida_simple_remove(&aem_ida, data->id);
	kfree(data);
}

/* Probe functions for AEM1 devices */

/* Retrieve version and module handle for an AEM1 instance */
static int aem_find_aem1_count(struct aem_ipmi_data *data)
{
	int res;
	struct aem_find_firmware_req	ff_req;
	struct aem_find_firmware_resp	ff_resp;

	ff_req.id = system_x_id;
	ff_req.index = 0;
	ff_req.module_type_id = cpu_to_be16(AEM_MODULE_TYPE_ID);

	data->tx_message.cmd = AEM_FIND_FW_CMD;
	data->tx_message.data = (char *)&ff_req;
	data->tx_message.data_len = sizeof(ff_req);

	data->rx_msg_data = &ff_resp;
	data->rx_msg_len = sizeof(ff_resp);

	aem_send_message(data);

	res = wait_for_completion_timeout(&data->read_complete, IPMI_TIMEOUT);
	if (!res)
		return -ETIMEDOUT;

	if (data->rx_result || data->rx_msg_len != sizeof(ff_resp) ||
	    memcmp(&ff_resp.id, &system_x_id, sizeof(system_x_id)))
		return -ENOENT;

	return ff_resp.num_instances;
}

/* Find and initialize one AEM1 instance */
static int aem_init_aem1_inst(struct aem_ipmi_data *probe, u8 module_handle)
{
	struct aem_data *data;
	int i;
	int res = -ENOMEM;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return res;
	mutex_init(&data->lock);

	/* Copy instance data */
	data->ver_major = 1;
	data->ver_minor = 0;
	data->module_handle = module_handle;
	for (i = 0; i < AEM1_NUM_ENERGY_REGS; i++)
		data->power_period[i] = AEM_DEFAULT_POWER_INTERVAL;

	/* Create sub-device for this fw instance */
	data->id = ida_simple_get(&aem_ida, 0, 0, GFP_KERNEL);
	if (data->id < 0)
		goto id_err;

	data->pdev = platform_device_alloc(DRVNAME, data->id);
	if (!data->pdev)
		goto dev_err;
	data->pdev->dev.driver = &aem_driver.driver;

	res = platform_device_add(data->pdev);
	if (res)
		goto ipmi_err;

	platform_set_drvdata(data->pdev, data);

	/* Set up IPMI interface */
	res = aem_init_ipmi_data(&data->ipmi, probe->interface,
				 probe->bmc_device);
	if (res)
		goto ipmi_err;

	/* Register with hwmon */
	data->hwmon_dev = hwmon_device_register(&data->pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		dev_err(&data->pdev->dev,
			"Unable to register hwmon device for IPMI interface %d\n",
			probe->interface);
		res = PTR_ERR(data->hwmon_dev);
		goto hwmon_reg_err;
	}

	data->update = update_aem1_sensors;
	data->rs_resp = kzalloc(sizeof(*(data->rs_resp)) + 8, GFP_KERNEL);
	if (!data->rs_resp) {
		res = -ENOMEM;
		goto alloc_resp_err;
	}

	/* Find sensors */
	res = aem1_find_sensors(data);
	if (res)
		goto sensor_err;

	/* Add to our list of AEM devices */
	list_add_tail(&data->list, &driver_data.aem_devices);

	dev_info(data->ipmi.bmc_device, "Found AEM v%d.%d at 0x%X\n",
		 data->ver_major, data->ver_minor,
		 data->module_handle);
	return 0;

sensor_err:
	kfree(data->rs_resp);
alloc_resp_err:
	hwmon_device_unregister(data->hwmon_dev);
hwmon_reg_err:
	ipmi_destroy_user(data->ipmi.user);
ipmi_err:
	platform_set_drvdata(data->pdev, NULL);
	platform_device_unregister(data->pdev);
dev_err:
	ida_simple_remove(&aem_ida, data->id);
id_err:
	kfree(data);

	return res;
}

/* Find and initialize all AEM1 instances */
static void aem_init_aem1(struct aem_ipmi_data *probe)
{
	int num, i, err;

	num = aem_find_aem1_count(probe);
	for (i = 0; i < num; i++) {
		err = aem_init_aem1_inst(probe, i);
		if (err) {
			dev_err(probe->bmc_device,
				"Error %d initializing AEM1 0x%X\n",
				err, i);
		}
	}
}

/* Probe functions for AEM2 devices */

/* Retrieve version and module handle for an AEM2 instance */
static int aem_find_aem2(struct aem_ipmi_data *data,
			    struct aem_find_instance_resp *fi_resp,
			    int instance_num)
{
	int res;
	struct aem_find_instance_req fi_req;

	fi_req.id = system_x_id;
	fi_req.instance_number = instance_num;
	fi_req.module_type_id = cpu_to_be16(AEM_MODULE_TYPE_ID);

	data->tx_message.cmd = AEM_FW_INSTANCE_CMD;
	data->tx_message.data = (char *)&fi_req;
	data->tx_message.data_len = sizeof(fi_req);

	data->rx_msg_data = fi_resp;
	data->rx_msg_len = sizeof(*fi_resp);

	aem_send_message(data);

	res = wait_for_completion_timeout(&data->read_complete, IPMI_TIMEOUT);
	if (!res)
		return -ETIMEDOUT;

	if (data->rx_result || data->rx_msg_len != sizeof(*fi_resp) ||
	    memcmp(&fi_resp->id, &system_x_id, sizeof(system_x_id)) ||
	    fi_resp->num_instances <= instance_num)
		return -ENOENT;

	return 0;
}

/* Find and initialize one AEM2 instance */
static int aem_init_aem2_inst(struct aem_ipmi_data *probe,
			      struct aem_find_instance_resp *fi_resp)
{
	struct aem_data *data;
	int i;
	int res = -ENOMEM;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return res;
	mutex_init(&data->lock);

	/* Copy instance data */
	data->ver_major = fi_resp->major;
	data->ver_minor = fi_resp->minor;
	data->module_handle = fi_resp->module_handle;
	for (i = 0; i < AEM2_NUM_ENERGY_REGS; i++)
		data->power_period[i] = AEM_DEFAULT_POWER_INTERVAL;

	/* Create sub-device for this fw instance */
	data->id = ida_simple_get(&aem_ida, 0, 0, GFP_KERNEL);
	if (data->id < 0)
		goto id_err;

	data->pdev = platform_device_alloc(DRVNAME, data->id);
	if (!data->pdev)
		goto dev_err;
	data->pdev->dev.driver = &aem_driver.driver;

	res = platform_device_add(data->pdev);
	if (res)
		goto ipmi_err;

	platform_set_drvdata(data->pdev, data);

	/* Set up IPMI interface */
	res = aem_init_ipmi_data(&data->ipmi, probe->interface,
				 probe->bmc_device);
	if (res)
		goto ipmi_err;

	/* Register with hwmon */
	data->hwmon_dev = hwmon_device_register(&data->pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		dev_err(&data->pdev->dev,
			"Unable to register hwmon device for IPMI interface %d\n",
			probe->interface);
		res = PTR_ERR(data->hwmon_dev);
		goto hwmon_reg_err;
	}

	data->update = update_aem2_sensors;
	data->rs_resp = kzalloc(sizeof(*(data->rs_resp)) + 8, GFP_KERNEL);
	if (!data->rs_resp) {
		res = -ENOMEM;
		goto alloc_resp_err;
	}

	/* Find sensors */
	res = aem2_find_sensors(data);
	if (res)
		goto sensor_err;

	/* Add to our list of AEM devices */
	list_add_tail(&data->list, &driver_data.aem_devices);

	dev_info(data->ipmi.bmc_device, "Found AEM v%d.%d at 0x%X\n",
		 data->ver_major, data->ver_minor,
		 data->module_handle);
	return 0;

sensor_err:
	kfree(data->rs_resp);
alloc_resp_err:
	hwmon_device_unregister(data->hwmon_dev);
hwmon_reg_err:
	ipmi_destroy_user(data->ipmi.user);
ipmi_err:
	platform_set_drvdata(data->pdev, NULL);
	platform_device_unregister(data->pdev);
dev_err:
	ida_simple_remove(&aem_ida, data->id);
id_err:
	kfree(data);

	return res;
}

/* Find and initialize all AEM2 instances */
static void aem_init_aem2(struct aem_ipmi_data *probe)
{
	struct aem_find_instance_resp fi_resp;
	int err;
	int i = 0;

	while (!aem_find_aem2(probe, &fi_resp, i)) {
		if (fi_resp.major != 2) {
			dev_err(probe->bmc_device,
				"Unknown AEM v%d; please report this to the maintainer.\n",
				fi_resp.major);
			i++;
			continue;
		}
		err = aem_init_aem2_inst(probe, &fi_resp);
		if (err) {
			dev_err(probe->bmc_device,
				"Error %d initializing AEM2 0x%X\n",
				err, fi_resp.module_handle);
		}
		i++;
	}
}

/* Probe a BMC for AEM firmware instances */
static void aem_register_bmc(int iface, struct device *dev)
{
	struct aem_ipmi_data probe;

	if (aem_init_ipmi_data(&probe, iface, dev))
		return;

	/* Ignore probe errors; they won't cause problems */
	aem_init_aem1(&probe);
	aem_init_aem2(&probe);

	ipmi_destroy_user(probe.user);
}

/* Handle BMC deletion */
static void aem_bmc_gone(int iface)
{
	struct aem_data *p1, *next1;

	list_for_each_entry_safe(p1, next1, &driver_data.aem_devices, list)
		if (p1->ipmi.interface == iface)
			aem_delete(p1);
}

/* sysfs support functions */

/* AEM device name */
static ssize_t name_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct aem_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s%d\n", DRVNAME, data->ver_major);
}
static SENSOR_DEVICE_ATTR_RO(name, name, 0);

/* AEM device version */
static ssize_t version_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct aem_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d.%d\n", data->ver_major, data->ver_minor);
}
static SENSOR_DEVICE_ATTR_RO(version, version, 0);

/* Display power use */
static ssize_t aem_show_power(struct device *dev,
			      struct device_attribute *devattr,
			      char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct aem_data *data = dev_get_drvdata(dev);
	u64 before, after, delta, time;
	signed long leftover;

	mutex_lock(&data->lock);
	update_aem_energy_one(data, attr->index);
	time = ktime_get_ns();
	before = data->energy[attr->index];

	leftover = schedule_timeout_interruptible(
			msecs_to_jiffies(data->power_period[attr->index])
		   );
	if (leftover) {
		mutex_unlock(&data->lock);
		return 0;
	}

	update_aem_energy_one(data, attr->index);
	time = ktime_get_ns() - time;
	after = data->energy[attr->index];
	mutex_unlock(&data->lock);

	delta = (after - before) * UJ_PER_MJ;

	return sprintf(buf, "%llu\n",
		(unsigned long long)div64_u64(delta * NSEC_PER_SEC, time));
}

/* Display energy use */
static ssize_t aem_show_energy(struct device *dev,
			       struct device_attribute *devattr,
			       char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct aem_data *a = dev_get_drvdata(dev);
	mutex_lock(&a->lock);
	update_aem_energy_one(a, attr->index);
	mutex_unlock(&a->lock);

	return sprintf(buf, "%llu\n",
			(unsigned long long)a->energy[attr->index] * 1000);
}

/* Display power interval registers */
static ssize_t aem_show_power_period(struct device *dev,
				     struct device_attribute *devattr,
				     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct aem_data *a = dev_get_drvdata(dev);
	a->update(a);

	return sprintf(buf, "%lu\n", a->power_period[attr->index]);
}

/* Set power interval registers */
static ssize_t aem_set_power_period(struct device *dev,
				    struct device_attribute *devattr,
				    const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct aem_data *a = dev_get_drvdata(dev);
	unsigned long temp;
	int res;

	res = kstrtoul(buf, 10, &temp);
	if (res)
		return res;

	if (temp < AEM_MIN_POWER_INTERVAL)
		return -EINVAL;

	mutex_lock(&a->lock);
	a->power_period[attr->index] = temp;
	mutex_unlock(&a->lock);

	return count;
}

/* Discover sensors on an AEM device */
static int aem_register_sensors(struct aem_data *data,
				const struct aem_ro_sensor_template *ro,
				const struct aem_rw_sensor_template *rw)
{
	struct device *dev = &data->pdev->dev;
	struct sensor_device_attribute *sensors = data->sensors;
	int err;

	/* Set up read-only sensors */
	while (ro->label) {
		sysfs_attr_init(&sensors->dev_attr.attr);
		sensors->dev_attr.attr.name = ro->label;
		sensors->dev_attr.attr.mode = 0444;
		sensors->dev_attr.show = ro->show;
		sensors->index = ro->index;

		err = device_create_file(dev, &sensors->dev_attr);
		if (err) {
			sensors->dev_attr.attr.name = NULL;
			goto error;
		}
		sensors++;
		ro++;
	}

	/* Set up read-write sensors */
	while (rw->label) {
		sysfs_attr_init(&sensors->dev_attr.attr);
		sensors->dev_attr.attr.name = rw->label;
		sensors->dev_attr.attr.mode = 0644;
		sensors->dev_attr.show = rw->show;
		sensors->dev_attr.store = rw->set;
		sensors->index = rw->index;

		err = device_create_file(dev, &sensors->dev_attr);
		if (err) {
			sensors->dev_attr.attr.name = NULL;
			goto error;
		}
		sensors++;
		rw++;
	}

	err = device_create_file(dev, &sensor_dev_attr_name.dev_attr);
	if (err)
		goto error;
	err = device_create_file(dev, &sensor_dev_attr_version.dev_attr);
	return err;

error:
	aem_remove_sensors(data);
	return err;
}

/* sysfs support functions for AEM2 sensors */

/* Display temperature use */
static ssize_t aem2_show_temp(struct device *dev,
			      struct device_attribute *devattr,
			      char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct aem_data *a = dev_get_drvdata(dev);
	a->update(a);

	return sprintf(buf, "%u\n", a->temp[attr->index] * 1000);
}

/* Display power-capping registers */
static ssize_t aem2_show_pcap_value(struct device *dev,
				    struct device_attribute *devattr,
				    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct aem_data *a = dev_get_drvdata(dev);
	a->update(a);

	return sprintf(buf, "%u\n", a->pcap[attr->index] * 100000);
}

/* Remove sensors attached to an AEM device */
static void aem_remove_sensors(struct aem_data *data)
{
	int i;

	for (i = 0; i < AEM_NUM_SENSORS; i++) {
		if (!data->sensors[i].dev_attr.attr.name)
			continue;
		device_remove_file(&data->pdev->dev,
				   &data->sensors[i].dev_attr);
	}

	device_remove_file(&data->pdev->dev,
			   &sensor_dev_attr_name.dev_attr);
	device_remove_file(&data->pdev->dev,
			   &sensor_dev_attr_version.dev_attr);
}

/* Sensor probe functions */

/* Description of AEM1 sensors */
static const struct aem_ro_sensor_template aem1_ro_sensors[] = {
{"energy1_input",  aem_show_energy, 0},
{"power1_average", aem_show_power,  0},
{NULL,		   NULL,	    0},
};

static const struct aem_rw_sensor_template aem1_rw_sensors[] = {
{"power1_average_interval", aem_show_power_period, aem_set_power_period, 0},
{NULL,			    NULL,                  NULL,                 0},
};

/* Description of AEM2 sensors */
static const struct aem_ro_sensor_template aem2_ro_sensors[] = {
{"energy1_input",	  aem_show_energy,	0},
{"energy2_input",	  aem_show_energy,	1},
{"power1_average",	  aem_show_power,	0},
{"power2_average",	  aem_show_power,	1},
{"temp1_input",		  aem2_show_temp,	0},
{"temp2_input",		  aem2_show_temp,	1},

{"power4_average",	  aem2_show_pcap_value,	POWER_CAP_MAX_HOTPLUG},
{"power5_average",	  aem2_show_pcap_value,	POWER_CAP_MAX},
{"power6_average",	  aem2_show_pcap_value,	POWER_CAP_MIN_WARNING},
{"power7_average",	  aem2_show_pcap_value,	POWER_CAP_MIN},

{"power3_average",	  aem2_show_pcap_value,	POWER_AUX},
{"power_cap",		  aem2_show_pcap_value,	POWER_CAP},
{NULL,                    NULL,                 0},
};

static const struct aem_rw_sensor_template aem2_rw_sensors[] = {
{"power1_average_interval", aem_show_power_period, aem_set_power_period, 0},
{"power2_average_interval", aem_show_power_period, aem_set_power_period, 1},
{NULL,			    NULL,                  NULL,                 0},
};

/* Set up AEM1 sensor attrs */
static int aem1_find_sensors(struct aem_data *data)
{
	return aem_register_sensors(data, aem1_ro_sensors, aem1_rw_sensors);
}

/* Set up AEM2 sensor attrs */
static int aem2_find_sensors(struct aem_data *data)
{
	return aem_register_sensors(data, aem2_ro_sensors, aem2_rw_sensors);
}

/* Module init/exit routines */

static int __init aem_init(void)
{
	int res;

	res = driver_register(&aem_driver.driver);
	if (res) {
		pr_err("Can't register aem driver\n");
		return res;
	}

	res = ipmi_smi_watcher_register(&driver_data.bmc_events);
	if (res)
		goto ipmi_reg_err;
	return 0;

ipmi_reg_err:
	driver_unregister(&aem_driver.driver);
	return res;

}

static void __exit aem_exit(void)
{
	struct aem_data *p1, *next1;

	ipmi_smi_watcher_unregister(&driver_data.bmc_events);
	driver_unregister(&aem_driver.driver);
	list_for_each_entry_safe(p1, next1, &driver_data.aem_devices, list)
		aem_delete(p1);
}

MODULE_AUTHOR("Darrick J. Wong <darrick.wong@oracle.com>");
MODULE_DESCRIPTION("IBM AEM power/temp/energy sensor driver");
MODULE_LICENSE("GPL");

module_init(aem_init);
module_exit(aem_exit);

MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3350-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3550-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3650-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3655-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3755-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBM3850M2/x3950M2-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMBladeHC10-*");
