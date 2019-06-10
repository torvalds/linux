// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A hwmon driver for the IBM PowerExecutive temperature/power sensors
 * Copyright (C) 2007 IBM
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */

#include <linux/ipmi.h>
#include <linux/module.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/err.h>

#define REFRESH_INTERVAL	(2 * HZ)
#define DRVNAME			"ibmpex"

#define PEX_GET_VERSION		1
#define PEX_GET_SENSOR_COUNT	2
#define PEX_GET_SENSOR_NAME	3
#define PEX_RESET_HIGH_LOW	4
#define PEX_GET_SENSOR_DATA	6

#define PEX_NET_FUNCTION	0x3A
#define PEX_COMMAND		0x3C

static inline u16 extract_value(const char *data, int offset)
{
	return be16_to_cpup((__be16 *)&data[offset]);
}

#define TEMP_SENSOR		1
#define POWER_SENSOR		2

#define PEX_SENSOR_TYPE_LEN	3
static u8 const power_sensor_sig[] = {0x70, 0x77, 0x72};
static u8 const temp_sensor_sig[]  = {0x74, 0x65, 0x6D};

#define PEX_MULT_LEN		2
static u8 const watt_sensor_sig[]  = {0x41, 0x43};

#define PEX_NUM_SENSOR_FUNCS	3
static const char * const sensor_name_suffixes[] = {
	"",
	"_lowest",
	"_highest"
};

static void ibmpex_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data);
static void ibmpex_register_bmc(int iface, struct device *dev);
static void ibmpex_bmc_gone(int iface);

struct ibmpex_sensor_data {
	int			in_use;
	s16			values[PEX_NUM_SENSOR_FUNCS];
	int			multiplier;

	struct sensor_device_attribute_2	attr[PEX_NUM_SENSOR_FUNCS];
};

struct ibmpex_bmc_data {
	struct list_head	list;
	struct device		*hwmon_dev;
	struct device		*bmc_device;
	struct mutex		lock;
	char			valid;
	unsigned long		last_updated;	/* In jiffies */

	struct ipmi_addr	address;
	struct completion	read_complete;
	struct ipmi_user	*user;
	int			interface;

	struct kernel_ipmi_msg	tx_message;
	unsigned char		tx_msg_data[IPMI_MAX_MSG_LENGTH];
	long			tx_msgid;

	unsigned char		rx_msg_data[IPMI_MAX_MSG_LENGTH];
	unsigned long		rx_msg_len;
	unsigned char		rx_result;
	int			rx_recv_type;

	unsigned char		sensor_major;
	unsigned char		sensor_minor;

	unsigned char		num_sensors;
	struct ibmpex_sensor_data	*sensors;
};

struct ibmpex_driver_data {
	struct list_head	bmc_data;
	struct ipmi_smi_watcher	bmc_events;
	struct ipmi_user_hndl	ipmi_hndlrs;
};

static struct ibmpex_driver_data driver_data = {
	.bmc_data = LIST_HEAD_INIT(driver_data.bmc_data),
	.bmc_events = {
		.owner = THIS_MODULE,
		.new_smi = ibmpex_register_bmc,
		.smi_gone = ibmpex_bmc_gone,
	},
	.ipmi_hndlrs = {
		.ipmi_recv_hndl = ibmpex_msg_handler,
	},
};

static int ibmpex_send_message(struct ibmpex_bmc_data *data)
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

static int ibmpex_ver_check(struct ibmpex_bmc_data *data)
{
	data->tx_msg_data[0] = PEX_GET_VERSION;
	data->tx_message.data_len = 1;
	ibmpex_send_message(data);

	wait_for_completion(&data->read_complete);

	if (data->rx_result || data->rx_msg_len != 6)
		return -ENOENT;

	data->sensor_major = data->rx_msg_data[0];
	data->sensor_minor = data->rx_msg_data[1];

	dev_info(data->bmc_device,
		 "Found BMC with sensor interface v%d.%d %d-%02d-%02d on interface %d\n",
		 data->sensor_major,
		 data->sensor_minor,
		 extract_value(data->rx_msg_data, 2),
		 data->rx_msg_data[4],
		 data->rx_msg_data[5],
		 data->interface);

	return 0;
}

static int ibmpex_query_sensor_count(struct ibmpex_bmc_data *data)
{
	data->tx_msg_data[0] = PEX_GET_SENSOR_COUNT;
	data->tx_message.data_len = 1;
	ibmpex_send_message(data);

	wait_for_completion(&data->read_complete);

	if (data->rx_result || data->rx_msg_len != 1)
		return -ENOENT;

	return data->rx_msg_data[0];
}

static int ibmpex_query_sensor_name(struct ibmpex_bmc_data *data, int sensor)
{
	data->tx_msg_data[0] = PEX_GET_SENSOR_NAME;
	data->tx_msg_data[1] = sensor;
	data->tx_message.data_len = 2;
	ibmpex_send_message(data);

	wait_for_completion(&data->read_complete);

	if (data->rx_result || data->rx_msg_len < 1)
		return -ENOENT;

	return 0;
}

static int ibmpex_query_sensor_data(struct ibmpex_bmc_data *data, int sensor)
{
	data->tx_msg_data[0] = PEX_GET_SENSOR_DATA;
	data->tx_msg_data[1] = sensor;
	data->tx_message.data_len = 2;
	ibmpex_send_message(data);

	wait_for_completion(&data->read_complete);

	if (data->rx_result || data->rx_msg_len < 26) {
		dev_err(data->bmc_device, "Error reading sensor %d.\n",
			sensor);
		return -ENOENT;
	}

	return 0;
}

static int ibmpex_reset_high_low_data(struct ibmpex_bmc_data *data)
{
	data->tx_msg_data[0] = PEX_RESET_HIGH_LOW;
	data->tx_message.data_len = 1;
	ibmpex_send_message(data);

	wait_for_completion(&data->read_complete);

	return 0;
}

static void ibmpex_update_device(struct ibmpex_bmc_data *data)
{
	int i, err;

	mutex_lock(&data->lock);
	if (time_before(jiffies, data->last_updated + REFRESH_INTERVAL) &&
	    data->valid)
		goto out;

	for (i = 0; i < data->num_sensors; i++) {
		if (!data->sensors[i].in_use)
			continue;
		err = ibmpex_query_sensor_data(data, i);
		if (err)
			continue;
		data->sensors[i].values[0] =
			extract_value(data->rx_msg_data, 16);
		data->sensors[i].values[1] =
			extract_value(data->rx_msg_data, 18);
		data->sensors[i].values[2] =
			extract_value(data->rx_msg_data, 20);
	}

	data->last_updated = jiffies;
	data->valid = 1;

out:
	mutex_unlock(&data->lock);
}

static struct ibmpex_bmc_data *get_bmc_data(int iface)
{
	struct ibmpex_bmc_data *p, *next;

	list_for_each_entry_safe(p, next, &driver_data.bmc_data, list)
		if (p->interface == iface)
			return p;

	return NULL;
}

static ssize_t name_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	return sprintf(buf, "%s\n", DRVNAME);
}
static SENSOR_DEVICE_ATTR_RO(name, name, 0);

static ssize_t ibmpex_show_sensor(struct device *dev,
				  struct device_attribute *devattr,
				  char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct ibmpex_bmc_data *data = dev_get_drvdata(dev);
	int mult = data->sensors[attr->index].multiplier;
	ibmpex_update_device(data);

	return sprintf(buf, "%d\n",
		       data->sensors[attr->index].values[attr->nr] * mult);
}

static ssize_t ibmpex_high_low_store(struct device *dev,
				     struct device_attribute *devattr,
				     const char *buf, size_t count)
{
	struct ibmpex_bmc_data *data = dev_get_drvdata(dev);

	ibmpex_reset_high_low_data(data);

	return count;
}

static SENSOR_DEVICE_ATTR_WO(reset_high_low, ibmpex_high_low, 0);

static int is_power_sensor(const char *sensor_id, int len)
{
	if (len < PEX_SENSOR_TYPE_LEN)
		return 0;

	if (!memcmp(sensor_id, power_sensor_sig, PEX_SENSOR_TYPE_LEN))
		return 1;
	return 0;
}

static int is_temp_sensor(const char *sensor_id, int len)
{
	if (len < PEX_SENSOR_TYPE_LEN)
		return 0;

	if (!memcmp(sensor_id, temp_sensor_sig, PEX_SENSOR_TYPE_LEN))
		return 1;
	return 0;
}

static int power_sensor_multiplier(struct ibmpex_bmc_data *data,
				   const char *sensor_id, int len)
{
	int i;

	if (data->sensor_major == 2)
		return 1000000;

	for (i = PEX_SENSOR_TYPE_LEN; i < len - 1; i++)
		if (!memcmp(&sensor_id[i], watt_sensor_sig, PEX_MULT_LEN))
			return 1000000;

	return 100000;
}

static int create_sensor(struct ibmpex_bmc_data *data, int type,
			 int counter, int sensor, int func)
{
	int err;
	char *n;

	n = kmalloc(32, GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	if (type == TEMP_SENSOR)
		sprintf(n, "temp%d_input%s",
			counter, sensor_name_suffixes[func]);
	else if (type == POWER_SENSOR)
		sprintf(n, "power%d_average%s",
			counter, sensor_name_suffixes[func]);

	sysfs_attr_init(&data->sensors[sensor].attr[func].dev_attr.attr);
	data->sensors[sensor].attr[func].dev_attr.attr.name = n;
	data->sensors[sensor].attr[func].dev_attr.attr.mode = 0444;
	data->sensors[sensor].attr[func].dev_attr.show = ibmpex_show_sensor;
	data->sensors[sensor].attr[func].index = sensor;
	data->sensors[sensor].attr[func].nr = func;

	err = device_create_file(data->bmc_device,
				 &data->sensors[sensor].attr[func].dev_attr);
	if (err) {
		data->sensors[sensor].attr[func].dev_attr.attr.name = NULL;
		kfree(n);
		return err;
	}

	return 0;
}

static int ibmpex_find_sensors(struct ibmpex_bmc_data *data)
{
	int i, j, err;
	int sensor_type;
	int sensor_counter;
	int num_power = 0;
	int num_temp = 0;

	err = ibmpex_query_sensor_count(data);
	if (err <= 0)
		return -ENOENT;
	data->num_sensors = err;

	data->sensors = kcalloc(data->num_sensors, sizeof(*data->sensors),
				GFP_KERNEL);
	if (!data->sensors)
		return -ENOMEM;

	for (i = 0; i < data->num_sensors; i++) {
		err = ibmpex_query_sensor_name(data, i);
		if (err)
			continue;

		if (is_power_sensor(data->rx_msg_data, data->rx_msg_len)) {
			sensor_type = POWER_SENSOR;
			num_power++;
			sensor_counter = num_power;
			data->sensors[i].multiplier =
				power_sensor_multiplier(data,
							data->rx_msg_data,
							data->rx_msg_len);
		} else if (is_temp_sensor(data->rx_msg_data,
					  data->rx_msg_len)) {
			sensor_type = TEMP_SENSOR;
			num_temp++;
			sensor_counter = num_temp;
			data->sensors[i].multiplier = 1000;
		} else
			continue;

		data->sensors[i].in_use = 1;

		/* Create attributes */
		for (j = 0; j < PEX_NUM_SENSOR_FUNCS; j++) {
			err = create_sensor(data, sensor_type, sensor_counter,
					    i, j);
			if (err)
				goto exit_remove;
		}
	}

	err = device_create_file(data->bmc_device,
			&sensor_dev_attr_reset_high_low.dev_attr);
	if (err)
		goto exit_remove;

	err = device_create_file(data->bmc_device,
			&sensor_dev_attr_name.dev_attr);
	if (err)
		goto exit_remove;

	return 0;

exit_remove:
	device_remove_file(data->bmc_device,
			   &sensor_dev_attr_reset_high_low.dev_attr);
	device_remove_file(data->bmc_device, &sensor_dev_attr_name.dev_attr);
	for (i = 0; i < data->num_sensors; i++)
		for (j = 0; j < PEX_NUM_SENSOR_FUNCS; j++) {
			if (!data->sensors[i].attr[j].dev_attr.attr.name)
				continue;
			device_remove_file(data->bmc_device,
				&data->sensors[i].attr[j].dev_attr);
			kfree(data->sensors[i].attr[j].dev_attr.attr.name);
		}

	kfree(data->sensors);
	return err;
}

static void ibmpex_register_bmc(int iface, struct device *dev)
{
	struct ibmpex_bmc_data *data;
	int err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	data->address.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	data->address.channel = IPMI_BMC_CHANNEL;
	data->address.data[0] = 0;
	data->interface = iface;
	data->bmc_device = dev;

	/* Create IPMI messaging interface user */
	err = ipmi_create_user(data->interface, &driver_data.ipmi_hndlrs,
			       data, &data->user);
	if (err < 0) {
		dev_err(dev,
			"Unable to register user with IPMI interface %d\n",
			data->interface);
		goto out;
	}

	mutex_init(&data->lock);

	/* Initialize message */
	data->tx_msgid = 0;
	init_completion(&data->read_complete);
	data->tx_message.netfn = PEX_NET_FUNCTION;
	data->tx_message.cmd = PEX_COMMAND;
	data->tx_message.data = data->tx_msg_data;

	/* Does this BMC support PowerExecutive? */
	err = ibmpex_ver_check(data);
	if (err)
		goto out_user;

	/* Register the BMC as a HWMON class device */
	data->hwmon_dev = hwmon_device_register(data->bmc_device);

	if (IS_ERR(data->hwmon_dev)) {
		dev_err(data->bmc_device,
			"Unable to register hwmon device for IPMI interface %d\n",
			data->interface);
		goto out_user;
	}

	/* finally add the new bmc data to the bmc data list */
	dev_set_drvdata(dev, data);
	list_add_tail(&data->list, &driver_data.bmc_data);

	/* Now go find all the sensors */
	err = ibmpex_find_sensors(data);
	if (err) {
		dev_err(data->bmc_device, "Error %d finding sensors\n", err);
		goto out_register;
	}

	return;

out_register:
	hwmon_device_unregister(data->hwmon_dev);
out_user:
	ipmi_destroy_user(data->user);
out:
	kfree(data);
}

static void ibmpex_bmc_delete(struct ibmpex_bmc_data *data)
{
	int i, j;

	device_remove_file(data->bmc_device,
			   &sensor_dev_attr_reset_high_low.dev_attr);
	device_remove_file(data->bmc_device, &sensor_dev_attr_name.dev_attr);
	for (i = 0; i < data->num_sensors; i++)
		for (j = 0; j < PEX_NUM_SENSOR_FUNCS; j++) {
			if (!data->sensors[i].attr[j].dev_attr.attr.name)
				continue;
			device_remove_file(data->bmc_device,
				&data->sensors[i].attr[j].dev_attr);
			kfree(data->sensors[i].attr[j].dev_attr.attr.name);
		}

	list_del(&data->list);
	dev_set_drvdata(data->bmc_device, NULL);
	hwmon_device_unregister(data->hwmon_dev);
	ipmi_destroy_user(data->user);
	kfree(data->sensors);
	kfree(data);
}

static void ibmpex_bmc_gone(int iface)
{
	struct ibmpex_bmc_data *data = get_bmc_data(iface);

	if (!data)
		return;

	ibmpex_bmc_delete(data);
}

static void ibmpex_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data)
{
	struct ibmpex_bmc_data *data = (struct ibmpex_bmc_data *)user_msg_data;

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
		data->rx_msg_len = msg->msg.data_len - 1;
		memcpy(data->rx_msg_data, msg->msg.data + 1, data->rx_msg_len);
	} else
		data->rx_msg_len = 0;

	ipmi_free_recv_msg(msg);
	complete(&data->read_complete);
}

static int __init ibmpex_init(void)
{
	return ipmi_smi_watcher_register(&driver_data.bmc_events);
}

static void __exit ibmpex_exit(void)
{
	struct ibmpex_bmc_data *p, *next;

	ipmi_smi_watcher_unregister(&driver_data.bmc_events);
	list_for_each_entry_safe(p, next, &driver_data.bmc_data, list)
		ibmpex_bmc_delete(p);
}

MODULE_AUTHOR("Darrick J. Wong <darrick.wong@oracle.com>");
MODULE_DESCRIPTION("IBM PowerExecutive power/temperature sensor driver");
MODULE_LICENSE("GPL");

module_init(ibmpex_init);
module_exit(ibmpex_exit);

MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3350-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3550-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3650-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3655-*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3755-*");
