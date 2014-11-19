/*
 * HID Sensors Driver
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/list.h>
#include <linux/hid-sensor-ids.h>
#include <linux/hid-sensor-hub.h>
#include "hid-ids.h"

#define HID_SENSOR_HUB_ENUM_QUIRK	0x01

/**
 * struct sensor_hub_pending - Synchronous read pending information
 * @status:		Pending status true/false.
 * @ready:		Completion synchronization data.
 * @usage_id:		Usage id for physical device, E.g. Gyro usage id.
 * @attr_usage_id:	Usage Id of a field, E.g. X-AXIS for a gyro.
 * @raw_size:		Response size for a read request.
 * @raw_data:		Place holder for received response.
 */
struct sensor_hub_pending {
	bool status;
	struct completion ready;
	u32 usage_id;
	u32 attr_usage_id;
	int raw_size;
	u8  *raw_data;
};

/**
 * struct sensor_hub_data - Hold a instance data for a HID hub device
 * @hsdev:		Stored hid instance for current hub device.
 * @mutex:		Mutex to serialize synchronous request.
 * @lock:		Spin lock to protect pending request structure.
 * @pending:		Holds information of pending sync read request.
 * @dyn_callback_list:	Holds callback function
 * @dyn_callback_lock:	spin lock to protect callback list
 * @hid_sensor_hub_client_devs:	Stores all MFD cells for a hub instance.
 * @hid_sensor_client_cnt: Number of MFD cells, (no of sensors attached).
 * @ref_cnt:		Number of MFD clients have opened this device
 */
struct sensor_hub_data {
	struct mutex mutex;
	spinlock_t lock;
	struct sensor_hub_pending pending;
	struct list_head dyn_callback_list;
	spinlock_t dyn_callback_lock;
	struct mfd_cell *hid_sensor_hub_client_devs;
	int hid_sensor_client_cnt;
	unsigned long quirks;
	int ref_cnt;
};

/**
 * struct hid_sensor_hub_callbacks_list - Stores callback list
 * @list:		list head.
 * @usage_id:		usage id for a physical device.
 * @usage_callback:	Stores registered callback functions.
 * @priv:		Private data for a physical device.
 */
struct hid_sensor_hub_callbacks_list {
	struct list_head list;
	u32 usage_id;
	struct hid_sensor_hub_device *hsdev;
	struct hid_sensor_hub_callbacks *usage_callback;
	void *priv;
};

static struct hid_report *sensor_hub_report(int id, struct hid_device *hdev,
						int dir)
{
	struct hid_report *report;

	list_for_each_entry(report, &hdev->report_enum[dir].report_list, list) {
		if (report->id == id)
			return report;
	}
	hid_warn(hdev, "No report with id 0x%x found\n", id);

	return NULL;
}

static int sensor_hub_get_physical_device_count(struct hid_device *hdev)
{
	int i;
	int count = 0;

	for (i = 0; i < hdev->maxcollection; ++i) {
		struct hid_collection *collection = &hdev->collection[i];
		if (collection->type == HID_COLLECTION_PHYSICAL)
			++count;
	}

	return count;
}

static void sensor_hub_fill_attr_info(
		struct hid_sensor_hub_attribute_info *info,
		s32 index, s32 report_id, struct hid_field *field)
{
	info->index = index;
	info->report_id = report_id;
	info->units = field->unit;
	info->unit_expo = field->unit_exponent;
	info->size = (field->report_size * field->report_count)/8;
	info->logical_minimum = field->logical_minimum;
	info->logical_maximum = field->logical_maximum;
}

static struct hid_sensor_hub_callbacks *sensor_hub_get_callback(
					struct hid_device *hdev,
					u32 usage_id,
					int collection_index,
					struct hid_sensor_hub_device **hsdev,
					void **priv)
{
	struct hid_sensor_hub_callbacks_list *callback;
	struct sensor_hub_data *pdata = hid_get_drvdata(hdev);

	spin_lock(&pdata->dyn_callback_lock);
	list_for_each_entry(callback, &pdata->dyn_callback_list, list)
		if (callback->usage_id == usage_id &&
			(collection_index >=
				callback->hsdev->start_collection_index) &&
			(collection_index <
				callback->hsdev->end_collection_index)) {
			*priv = callback->priv;
			*hsdev = callback->hsdev;
			spin_unlock(&pdata->dyn_callback_lock);
			return callback->usage_callback;
		}
	spin_unlock(&pdata->dyn_callback_lock);

	return NULL;
}

int sensor_hub_register_callback(struct hid_sensor_hub_device *hsdev,
			u32 usage_id,
			struct hid_sensor_hub_callbacks *usage_callback)
{
	struct hid_sensor_hub_callbacks_list *callback;
	struct sensor_hub_data *pdata = hid_get_drvdata(hsdev->hdev);
	unsigned long flags;

	spin_lock_irqsave(&pdata->dyn_callback_lock, flags);
	list_for_each_entry(callback, &pdata->dyn_callback_list, list)
		if (callback->usage_id == usage_id &&
						callback->hsdev == hsdev) {
			spin_unlock_irqrestore(&pdata->dyn_callback_lock, flags);
			return -EINVAL;
		}
	callback = kzalloc(sizeof(*callback), GFP_ATOMIC);
	if (!callback) {
		spin_unlock_irqrestore(&pdata->dyn_callback_lock, flags);
		return -ENOMEM;
	}
	callback->hsdev = hsdev;
	callback->usage_callback = usage_callback;
	callback->usage_id = usage_id;
	callback->priv = NULL;
	list_add_tail(&callback->list, &pdata->dyn_callback_list);
	spin_unlock_irqrestore(&pdata->dyn_callback_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(sensor_hub_register_callback);

int sensor_hub_remove_callback(struct hid_sensor_hub_device *hsdev,
				u32 usage_id)
{
	struct hid_sensor_hub_callbacks_list *callback;
	struct sensor_hub_data *pdata = hid_get_drvdata(hsdev->hdev);
	unsigned long flags;

	spin_lock_irqsave(&pdata->dyn_callback_lock, flags);
	list_for_each_entry(callback, &pdata->dyn_callback_list, list)
		if (callback->usage_id == usage_id &&
						callback->hsdev == hsdev) {
			list_del(&callback->list);
			kfree(callback);
			break;
		}
	spin_unlock_irqrestore(&pdata->dyn_callback_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(sensor_hub_remove_callback);

int sensor_hub_set_feature(struct hid_sensor_hub_device *hsdev, u32 report_id,
				u32 field_index, s32 value)
{
	struct hid_report *report;
	struct sensor_hub_data *data = hid_get_drvdata(hsdev->hdev);
	int ret = 0;

	mutex_lock(&data->mutex);
	report = sensor_hub_report(report_id, hsdev->hdev, HID_FEATURE_REPORT);
	if (!report || (field_index >= report->maxfield)) {
		ret = -EINVAL;
		goto done_proc;
	}
	hid_set_field(report->field[field_index], 0, value);
	hid_hw_request(hsdev->hdev, report, HID_REQ_SET_REPORT);
	hid_hw_wait(hsdev->hdev);

done_proc:
	mutex_unlock(&data->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(sensor_hub_set_feature);

int sensor_hub_get_feature(struct hid_sensor_hub_device *hsdev, u32 report_id,
				u32 field_index, s32 *value)
{
	struct hid_report *report;
	struct sensor_hub_data *data = hid_get_drvdata(hsdev->hdev);
	int ret = 0;

	mutex_lock(&data->mutex);
	report = sensor_hub_report(report_id, hsdev->hdev, HID_FEATURE_REPORT);
	if (!report || (field_index >= report->maxfield) ||
	    report->field[field_index]->report_count < 1) {
		ret = -EINVAL;
		goto done_proc;
	}
	hid_hw_request(hsdev->hdev, report, HID_REQ_GET_REPORT);
	hid_hw_wait(hsdev->hdev);
	*value = report->field[field_index]->value[0];

done_proc:
	mutex_unlock(&data->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(sensor_hub_get_feature);


int sensor_hub_input_attr_get_raw_value(struct hid_sensor_hub_device *hsdev,
					u32 usage_id,
					u32 attr_usage_id, u32 report_id)
{
	struct sensor_hub_data *data = hid_get_drvdata(hsdev->hdev);
	unsigned long flags;
	struct hid_report *report;
	int ret_val = 0;

	mutex_lock(&data->mutex);
	memset(&data->pending, 0, sizeof(data->pending));
	init_completion(&data->pending.ready);
	data->pending.usage_id = usage_id;
	data->pending.attr_usage_id = attr_usage_id;
	data->pending.raw_size = 0;

	spin_lock_irqsave(&data->lock, flags);
	data->pending.status = true;
	spin_unlock_irqrestore(&data->lock, flags);
	report = sensor_hub_report(report_id, hsdev->hdev, HID_INPUT_REPORT);
	if (!report)
		goto err_free;

	hid_hw_request(hsdev->hdev, report, HID_REQ_GET_REPORT);
	wait_for_completion_interruptible_timeout(&data->pending.ready, HZ*5);
	switch (data->pending.raw_size) {
	case 1:
		ret_val = *(u8 *)data->pending.raw_data;
		break;
	case 2:
		ret_val = *(u16 *)data->pending.raw_data;
		break;
	case 4:
		ret_val = *(u32 *)data->pending.raw_data;
		break;
	default:
		ret_val = 0;
	}
	kfree(data->pending.raw_data);

err_free:
	data->pending.status = false;
	mutex_unlock(&data->mutex);

	return ret_val;
}
EXPORT_SYMBOL_GPL(sensor_hub_input_attr_get_raw_value);

int hid_sensor_get_usage_index(struct hid_sensor_hub_device *hsdev,
				u32 report_id, int field_index, u32 usage_id)
{
	struct hid_report *report;
	struct hid_field *field;
	int i;

	report = sensor_hub_report(report_id, hsdev->hdev, HID_FEATURE_REPORT);
	if (!report || (field_index >= report->maxfield))
		goto done_proc;

	field = report->field[field_index];
	for (i = 0; i < field->maxusage; ++i) {
		if (field->usage[i].hid == usage_id)
			return field->usage[i].usage_index;
	}

done_proc:
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hid_sensor_get_usage_index);

int sensor_hub_input_get_attribute_info(struct hid_sensor_hub_device *hsdev,
				u8 type,
				u32 usage_id,
				u32 attr_usage_id,
				struct hid_sensor_hub_attribute_info *info)
{
	int ret = -1;
	int i;
	struct hid_report *report;
	struct hid_field *field;
	struct hid_report_enum *report_enum;
	struct hid_device *hdev = hsdev->hdev;

	/* Initialize with defaults */
	info->usage_id = usage_id;
	info->attrib_id = attr_usage_id;
	info->report_id = -1;
	info->index = -1;
	info->units = -1;
	info->unit_expo = -1;

	report_enum = &hdev->report_enum[type];
	list_for_each_entry(report, &report_enum->report_list, list) {
		for (i = 0; i < report->maxfield; ++i) {
			field = report->field[i];
			if (field->maxusage) {
				if (field->physical == usage_id &&
					(field->logical == attr_usage_id ||
					field->usage[0].hid ==
							attr_usage_id) &&
					(field->usage[0].collection_index >=
					hsdev->start_collection_index) &&
					(field->usage[0].collection_index <
					hsdev->end_collection_index)) {

					sensor_hub_fill_attr_info(info, i,
								report->id,
								field);
					ret = 0;
					break;
				}
			}
		}

	}

	return ret;
}
EXPORT_SYMBOL_GPL(sensor_hub_input_get_attribute_info);

#ifdef CONFIG_PM
static int sensor_hub_suspend(struct hid_device *hdev, pm_message_t message)
{
	struct sensor_hub_data *pdata = hid_get_drvdata(hdev);
	struct hid_sensor_hub_callbacks_list *callback;
	unsigned long flags;

	hid_dbg(hdev, " sensor_hub_suspend\n");
	spin_lock_irqsave(&pdata->dyn_callback_lock, flags);
	list_for_each_entry(callback, &pdata->dyn_callback_list, list) {
		if (callback->usage_callback->suspend)
			callback->usage_callback->suspend(
					callback->hsdev, callback->priv);
	}
	spin_unlock_irqrestore(&pdata->dyn_callback_lock, flags);

	return 0;
}

static int sensor_hub_resume(struct hid_device *hdev)
{
	struct sensor_hub_data *pdata = hid_get_drvdata(hdev);
	struct hid_sensor_hub_callbacks_list *callback;
	unsigned long flags;

	hid_dbg(hdev, " sensor_hub_resume\n");
	spin_lock_irqsave(&pdata->dyn_callback_lock, flags);
	list_for_each_entry(callback, &pdata->dyn_callback_list, list) {
		if (callback->usage_callback->resume)
			callback->usage_callback->resume(
					callback->hsdev, callback->priv);
	}
	spin_unlock_irqrestore(&pdata->dyn_callback_lock, flags);

	return 0;
}

static int sensor_hub_reset_resume(struct hid_device *hdev)
{
	return 0;
}
#endif

/*
 * Handle raw report as sent by device
 */
static int sensor_hub_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *raw_data, int size)
{
	int i;
	u8 *ptr;
	int sz;
	struct sensor_hub_data *pdata = hid_get_drvdata(hdev);
	unsigned long flags;
	struct hid_sensor_hub_callbacks *callback = NULL;
	struct hid_collection *collection = NULL;
	void *priv = NULL;
	struct hid_sensor_hub_device *hsdev = NULL;

	hid_dbg(hdev, "sensor_hub_raw_event report id:0x%x size:%d type:%d\n",
			 report->id, size, report->type);
	hid_dbg(hdev, "maxfield:%d\n", report->maxfield);
	if (report->type != HID_INPUT_REPORT)
		return 1;

	ptr = raw_data;
	ptr++; /* Skip report id */

	spin_lock_irqsave(&pdata->lock, flags);

	for (i = 0; i < report->maxfield; ++i) {
		hid_dbg(hdev, "%d collection_index:%x hid:%x sz:%x\n",
				i, report->field[i]->usage->collection_index,
				report->field[i]->usage->hid,
				(report->field[i]->report_size *
					report->field[i]->report_count)/8);
		sz = (report->field[i]->report_size *
					report->field[i]->report_count)/8;
		if (pdata->pending.status && pdata->pending.attr_usage_id ==
				report->field[i]->usage->hid) {
			hid_dbg(hdev, "data was pending ...\n");
			pdata->pending.raw_data = kmemdup(ptr, sz, GFP_ATOMIC);
			if (pdata->pending.raw_data)
				pdata->pending.raw_size = sz;
			else
				pdata->pending.raw_size = 0;
			complete(&pdata->pending.ready);
		}
		collection = &hdev->collection[
				report->field[i]->usage->collection_index];
		hid_dbg(hdev, "collection->usage %x\n",
					collection->usage);

		callback = sensor_hub_get_callback(hdev,
				report->field[i]->physical,
				report->field[i]->usage[0].collection_index,
				&hsdev, &priv);

		if (callback && callback->capture_sample) {
			if (report->field[i]->logical)
				callback->capture_sample(hsdev,
					report->field[i]->logical, sz, ptr,
					callback->pdev);
			else
				callback->capture_sample(hsdev,
					report->field[i]->usage->hid, sz, ptr,
					callback->pdev);
		}
		ptr += sz;
	}
	if (callback && collection && callback->send_event)
		callback->send_event(hsdev, collection->usage,
				callback->pdev);
	spin_unlock_irqrestore(&pdata->lock, flags);

	return 1;
}

int sensor_hub_device_open(struct hid_sensor_hub_device *hsdev)
{
	int ret = 0;
	struct sensor_hub_data *data =  hid_get_drvdata(hsdev->hdev);

	mutex_lock(&data->mutex);
	if (!data->ref_cnt) {
		ret = hid_hw_open(hsdev->hdev);
		if (ret) {
			hid_err(hsdev->hdev, "failed to open hid device\n");
			mutex_unlock(&data->mutex);
			return ret;
		}
	}
	data->ref_cnt++;
	mutex_unlock(&data->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(sensor_hub_device_open);

void sensor_hub_device_close(struct hid_sensor_hub_device *hsdev)
{
	struct sensor_hub_data *data =  hid_get_drvdata(hsdev->hdev);

	mutex_lock(&data->mutex);
	data->ref_cnt--;
	if (!data->ref_cnt)
		hid_hw_close(hsdev->hdev);
	mutex_unlock(&data->mutex);
}
EXPORT_SYMBOL_GPL(sensor_hub_device_close);

static __u8 *sensor_hub_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	int index;
	struct sensor_hub_data *sd =  hid_get_drvdata(hdev);
	unsigned char report_block[] = {
				0x0a,  0x16, 0x03, 0x15, 0x00, 0x25, 0x05};
	unsigned char power_block[] = {
				0x0a,  0x19, 0x03, 0x15, 0x00, 0x25, 0x05};

	if (!(sd->quirks & HID_SENSOR_HUB_ENUM_QUIRK)) {
		hid_dbg(hdev, "No Enum quirks\n");
		return rdesc;
	}

	/* Looks for power and report state usage id and force to 1 */
	for (index = 0; index < *rsize; ++index) {
		if (((*rsize - index) > sizeof(report_block)) &&
			!memcmp(&rdesc[index], report_block,
						sizeof(report_block))) {
			rdesc[index + 4] = 0x01;
			index += sizeof(report_block);
		}
		if (((*rsize - index) > sizeof(power_block)) &&
			!memcmp(&rdesc[index], power_block,
						sizeof(power_block))) {
			rdesc[index + 4] = 0x01;
			index += sizeof(power_block);
		}
	}

	return rdesc;
}

static int sensor_hub_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	int ret;
	struct sensor_hub_data *sd;
	int i;
	char *name;
	int dev_cnt;
	struct hid_sensor_hub_device *hsdev;
	struct hid_sensor_hub_device *last_hsdev = NULL;

	sd = devm_kzalloc(&hdev->dev, sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		hid_err(hdev, "cannot allocate Sensor data\n");
		return -ENOMEM;
	}

	hid_set_drvdata(hdev, sd);
	sd->quirks = id->driver_data;

	spin_lock_init(&sd->lock);
	spin_lock_init(&sd->dyn_callback_lock);
	mutex_init(&sd->mutex);
	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}
	INIT_LIST_HEAD(&hdev->inputs);

	ret = hid_hw_start(hdev, 0);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}
	INIT_LIST_HEAD(&sd->dyn_callback_list);
	sd->hid_sensor_client_cnt = 0;

	dev_cnt = sensor_hub_get_physical_device_count(hdev);
	if (dev_cnt > HID_MAX_PHY_DEVICES) {
		hid_err(hdev, "Invalid Physical device count\n");
		ret = -EINVAL;
		goto err_stop_hw;
	}
	sd->hid_sensor_hub_client_devs = devm_kzalloc(&hdev->dev, dev_cnt *
						      sizeof(struct mfd_cell),
						      GFP_KERNEL);
	if (sd->hid_sensor_hub_client_devs == NULL) {
		hid_err(hdev, "Failed to allocate memory for mfd cells\n");
			ret = -ENOMEM;
			goto err_stop_hw;
	}

	for (i = 0; i < hdev->maxcollection; ++i) {
		struct hid_collection *collection = &hdev->collection[i];

		if (collection->type == HID_COLLECTION_PHYSICAL) {

			hsdev = devm_kzalloc(&hdev->dev, sizeof(*hsdev),
					     GFP_KERNEL);
			if (!hsdev) {
				hid_err(hdev, "cannot allocate hid_sensor_hub_device\n");
				ret = -ENOMEM;
				goto err_stop_hw;
			}
			hsdev->hdev = hdev;
			hsdev->vendor_id = hdev->vendor;
			hsdev->product_id = hdev->product;
			hsdev->start_collection_index = i;
			if (last_hsdev)
				last_hsdev->end_collection_index = i;
			last_hsdev = hsdev;
			name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
					      "HID-SENSOR-%x",
					      collection->usage);
			if (name == NULL) {
				hid_err(hdev, "Failed MFD device name\n");
					ret = -ENOMEM;
					goto err_stop_hw;
			}
			sd->hid_sensor_hub_client_devs[
				sd->hid_sensor_client_cnt].id =
							PLATFORM_DEVID_AUTO;
			sd->hid_sensor_hub_client_devs[
				sd->hid_sensor_client_cnt].name = name;
			sd->hid_sensor_hub_client_devs[
				sd->hid_sensor_client_cnt].platform_data =
							hsdev;
			sd->hid_sensor_hub_client_devs[
				sd->hid_sensor_client_cnt].pdata_size =
							sizeof(*hsdev);
			hid_dbg(hdev, "Adding %s:%d\n", name,
					hsdev->start_collection_index);
			sd->hid_sensor_client_cnt++;
		}
	}
	if (last_hsdev)
		last_hsdev->end_collection_index = i;

	ret = mfd_add_devices(&hdev->dev, 0, sd->hid_sensor_hub_client_devs,
		sd->hid_sensor_client_cnt, NULL, 0, NULL);
	if (ret < 0)
		goto err_stop_hw;

	return ret;

err_stop_hw:
	hid_hw_stop(hdev);

	return ret;
}

static void sensor_hub_remove(struct hid_device *hdev)
{
	struct sensor_hub_data *data = hid_get_drvdata(hdev);
	unsigned long flags;

	hid_dbg(hdev, " hardware removed\n");
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	spin_lock_irqsave(&data->lock, flags);
	if (data->pending.status)
		complete(&data->pending.ready);
	spin_unlock_irqrestore(&data->lock, flags);
	mfd_remove_devices(&hdev->dev);
	hid_set_drvdata(hdev, NULL);
	mutex_destroy(&data->mutex);
}

static const struct hid_device_id sensor_hub_devices[] = {
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, USB_VENDOR_ID_INTEL_0,
			USB_DEVICE_ID_INTEL_HID_SENSOR_0),
			.driver_data = HID_SENSOR_HUB_ENUM_QUIRK},
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, USB_VENDOR_ID_INTEL_1,
			USB_DEVICE_ID_INTEL_HID_SENSOR_0),
			.driver_data = HID_SENSOR_HUB_ENUM_QUIRK},
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, USB_VENDOR_ID_INTEL_1,
			USB_DEVICE_ID_INTEL_HID_SENSOR_1),
			.driver_data = HID_SENSOR_HUB_ENUM_QUIRK},
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, USB_VENDOR_ID_MICROSOFT,
			USB_DEVICE_ID_MS_SURFACE_PRO_2),
			.driver_data = HID_SENSOR_HUB_ENUM_QUIRK},
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, USB_VENDOR_ID_MICROSOFT,
			USB_DEVICE_ID_MS_TOUCH_COVER_2),
			.driver_data = HID_SENSOR_HUB_ENUM_QUIRK},
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, USB_VENDOR_ID_MICROSOFT,
			USB_DEVICE_ID_MS_TYPE_COVER_2),
			.driver_data = HID_SENSOR_HUB_ENUM_QUIRK},
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, USB_VENDOR_ID_STM_0,
			USB_DEVICE_ID_STM_HID_SENSOR_1),
			.driver_data = HID_SENSOR_HUB_ENUM_QUIRK},
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, USB_VENDOR_ID_TEXAS_INSTRUMENTS,
			USB_DEVICE_ID_TEXAS_INSTRUMENTS_LENOVO_YOGA),
			.driver_data = HID_SENSOR_HUB_ENUM_QUIRK},
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_SENSOR_HUB, HID_ANY_ID,
		     HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, sensor_hub_devices);

static struct hid_driver sensor_hub_driver = {
	.name = "hid-sensor-hub",
	.id_table = sensor_hub_devices,
	.probe = sensor_hub_probe,
	.remove = sensor_hub_remove,
	.raw_event = sensor_hub_raw_event,
	.report_fixup = sensor_hub_report_fixup,
#ifdef CONFIG_PM
	.suspend = sensor_hub_suspend,
	.resume = sensor_hub_resume,
	.reset_resume = sensor_hub_reset_resume,
#endif
};
module_hid_driver(sensor_hub_driver);

MODULE_DESCRIPTION("HID Sensor Hub driver");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_LICENSE("GPL");
