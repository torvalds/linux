// SPDX-License-Identifier: GPL-2.0-only
/*
 * hid-sensor-custom.c
 * Copyright (c) 2015, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/bsearch.h>
#include <linux/platform_device.h>
#include <linux/hid-sensor-hub.h>

#define HID_CUSTOM_NAME_LENGTH		64
#define HID_CUSTOM_MAX_CORE_ATTRS	10
#define HID_CUSTOM_TOTAL_ATTRS		(HID_CUSTOM_MAX_CORE_ATTRS + 1)
#define HID_CUSTOM_FIFO_SIZE		4096
#define HID_CUSTOM_MAX_FEATURE_BYTES	64

struct hid_sensor_custom_field {
	int report_id;
	char group_name[HID_CUSTOM_NAME_LENGTH];
	struct hid_sensor_hub_attribute_info attribute;
	struct device_attribute sd_attrs[HID_CUSTOM_MAX_CORE_ATTRS];
	char attr_name[HID_CUSTOM_TOTAL_ATTRS][HID_CUSTOM_NAME_LENGTH];
	struct attribute *attrs[HID_CUSTOM_TOTAL_ATTRS];
	struct attribute_group hid_custom_attribute_group;
};

struct hid_sensor_custom {
	struct mutex mutex;
	struct platform_device *pdev;
	struct hid_sensor_hub_device *hsdev;
	struct hid_sensor_hub_callbacks callbacks;
	int sensor_field_count;
	struct hid_sensor_custom_field *fields;
	int input_field_count;
	int input_report_size;
	int input_report_recd_size;
	bool input_skip_sample;
	bool enable;
	struct hid_sensor_custom_field *power_state;
	struct hid_sensor_custom_field *report_state;
	struct miscdevice custom_dev;
	struct kfifo data_fifo;
	unsigned long misc_opened;
	wait_queue_head_t wait;
};

/* Header for each sample to user space via dev interface */
struct hid_sensor_sample {
	u32 usage_id;
	u64 timestamp;
	u32 raw_len;
} __packed;

static struct attribute hid_custom_attrs[] = {
	{.name = "name", .mode = S_IRUGO},
	{.name = "units", .mode = S_IRUGO},
	{.name = "unit-expo", .mode = S_IRUGO},
	{.name = "minimum", .mode = S_IRUGO},
	{.name = "maximum", .mode = S_IRUGO},
	{.name = "size", .mode = S_IRUGO},
	{.name = "value", .mode = S_IWUSR | S_IRUGO},
	{.name = NULL}
};

static const struct hid_custom_usage_desc {
	int usage_id;
	char *desc;
} hid_custom_usage_desc_table[] = {
	{0x200201,	"event-sensor-state"},
	{0x200202,	"event-sensor-event"},
	{0x200301,	"property-friendly-name"},
	{0x200302,	"property-persistent-unique-id"},
	{0x200303,	"property-sensor-status"},
	{0x200304,	"property-min-report-interval"},
	{0x200305,	"property-sensor-manufacturer"},
	{0x200306,	"property-sensor-model"},
	{0x200307,	"property-sensor-serial-number"},
	{0x200308,	"property-sensor-description"},
	{0x200309,	"property-sensor-connection-type"},
	{0x20030A,	"property-sensor-device-path"},
	{0x20030B,	"property-hardware-revision"},
	{0x20030C,	"property-firmware-version"},
	{0x20030D,	"property-release-date"},
	{0x20030E,	"property-report-interval"},
	{0x20030F,	"property-change-sensitivity-absolute"},
	{0x200310,	"property-change-sensitivity-percent-range"},
	{0x200311,	"property-change-sensitivity-percent-relative"},
	{0x200312,	"property-accuracy"},
	{0x200313,	"property-resolution"},
	{0x200314,	"property-maximum"},
	{0x200315,	"property-minimum"},
	{0x200316,	"property-reporting-state"},
	{0x200317,	"property-sampling-rate"},
	{0x200318,	"property-response-curve"},
	{0x200319,	"property-power-state"},
	{0x200540,	"data-field-custom"},
	{0x200541,	"data-field-custom-usage"},
	{0x200542,	"data-field-custom-boolean-array"},
	{0x200543,	"data-field-custom-value"},
	{0x200544,	"data-field-custom-value_1"},
	{0x200545,	"data-field-custom-value_2"},
	{0x200546,	"data-field-custom-value_3"},
	{0x200547,	"data-field-custom-value_4"},
	{0x200548,	"data-field-custom-value_5"},
	{0x200549,	"data-field-custom-value_6"},
	{0x20054A,	"data-field-custom-value_7"},
	{0x20054B,	"data-field-custom-value_8"},
	{0x20054C,	"data-field-custom-value_9"},
	{0x20054D,	"data-field-custom-value_10"},
	{0x20054E,	"data-field-custom-value_11"},
	{0x20054F,	"data-field-custom-value_12"},
	{0x200550,	"data-field-custom-value_13"},
	{0x200551,	"data-field-custom-value_14"},
	{0x200552,	"data-field-custom-value_15"},
	{0x200553,	"data-field-custom-value_16"},
	{0x200554,	"data-field-custom-value_17"},
	{0x200555,	"data-field-custom-value_18"},
	{0x200556,	"data-field-custom-value_19"},
	{0x200557,	"data-field-custom-value_20"},
	{0x200558,	"data-field-custom-value_21"},
	{0x200559,	"data-field-custom-value_22"},
	{0x20055A,	"data-field-custom-value_23"},
	{0x20055B,	"data-field-custom-value_24"},
	{0x20055C,	"data-field-custom-value_25"},
	{0x20055D,	"data-field-custom-value_26"},
	{0x20055E,	"data-field-custom-value_27"},
	{0x20055F,	"data-field-custom-value_28"},
};

static int usage_id_cmp(const void *p1, const void *p2)
{
	if (*(int *)p1 < *(int *)p2)
		return -1;

	if (*(int *)p1 > *(int *)p2)
		return 1;

	return 0;
}

static ssize_t enable_sensor_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct hid_sensor_custom *sensor_inst = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", sensor_inst->enable);
}

static int set_power_report_state(struct hid_sensor_custom *sensor_inst,
				  bool state)
{
	int power_val = -1;
	int report_val = -1;
	u32 power_state_usage_id;
	u32 report_state_usage_id;
	int ret;

	/*
	 * It is possible that the power/report state ids are not present.
	 * In this case this function will return success. But if the
	 * ids are present, then it will return error if set fails.
	 */
	if (state) {
		power_state_usage_id =
			HID_USAGE_SENSOR_PROP_POWER_STATE_D0_FULL_POWER_ENUM;
		report_state_usage_id =
			HID_USAGE_SENSOR_PROP_REPORTING_STATE_ALL_EVENTS_ENUM;
	} else {
		power_state_usage_id =
			HID_USAGE_SENSOR_PROP_POWER_STATE_D4_POWER_OFF_ENUM;
		report_state_usage_id =
			HID_USAGE_SENSOR_PROP_REPORTING_STATE_NO_EVENTS_ENUM;
	}

	if (sensor_inst->power_state)
		power_val = hid_sensor_get_usage_index(sensor_inst->hsdev,
				sensor_inst->power_state->attribute.report_id,
				sensor_inst->power_state->attribute.index,
				power_state_usage_id);
	if (sensor_inst->report_state)
		report_val = hid_sensor_get_usage_index(sensor_inst->hsdev,
				sensor_inst->report_state->attribute.report_id,
				sensor_inst->report_state->attribute.index,
				report_state_usage_id);

	if (power_val >= 0) {
		power_val +=
			sensor_inst->power_state->attribute.logical_minimum;
		ret = sensor_hub_set_feature(sensor_inst->hsdev,
				sensor_inst->power_state->attribute.report_id,
				sensor_inst->power_state->attribute.index,
				sizeof(power_val),
				&power_val);
		if (ret) {
			hid_err(sensor_inst->hsdev->hdev,
				"Set power state failed\n");
			return ret;
		}
	}

	if (report_val >= 0) {
		report_val +=
			sensor_inst->report_state->attribute.logical_minimum;
		ret = sensor_hub_set_feature(sensor_inst->hsdev,
				sensor_inst->report_state->attribute.report_id,
				sensor_inst->report_state->attribute.index,
				sizeof(report_val),
				&report_val);
		if (ret) {
			hid_err(sensor_inst->hsdev->hdev,
				"Set report state failed\n");
			return ret;
		}
	}

	return 0;
}

static ssize_t enable_sensor_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hid_sensor_custom *sensor_inst = dev_get_drvdata(dev);
	int value;
	int ret = -EINVAL;

	if (kstrtoint(buf, 0, &value) != 0)
		return -EINVAL;

	mutex_lock(&sensor_inst->mutex);
	if (value && !sensor_inst->enable) {
		ret = sensor_hub_device_open(sensor_inst->hsdev);
		if (ret)
			goto unlock_state;

		ret = set_power_report_state(sensor_inst, true);
		if (ret) {
			sensor_hub_device_close(sensor_inst->hsdev);
			goto unlock_state;
		}
		sensor_inst->enable = true;
	} else if (!value && sensor_inst->enable) {
		ret = set_power_report_state(sensor_inst, false);
		sensor_hub_device_close(sensor_inst->hsdev);
		sensor_inst->enable = false;
	}
unlock_state:
	mutex_unlock(&sensor_inst->mutex);
	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(enable_sensor);

static struct attribute *enable_sensor_attrs[] = {
	&dev_attr_enable_sensor.attr,
	NULL,
};

static const struct attribute_group enable_sensor_attr_group = {
	.attrs = enable_sensor_attrs,
};

static ssize_t show_value(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct hid_sensor_custom *sensor_inst = dev_get_drvdata(dev);
	struct hid_sensor_hub_attribute_info *attribute;
	int index, usage, field_index;
	char name[HID_CUSTOM_NAME_LENGTH];
	bool feature = false;
	bool input = false;
	int value = 0;

	if (sscanf(attr->attr.name, "feature-%x-%x-%s", &index, &usage,
		   name) == 3) {
		feature = true;
		field_index = index + sensor_inst->input_field_count;
	} else if (sscanf(attr->attr.name, "input-%x-%x-%s", &index, &usage,
		   name) == 3) {
		input = true;
		field_index = index;
	} else
		return -EINVAL;

	if (!strncmp(name, "value", strlen("value"))) {
		u32 report_id;
		int ret;

		attribute = &sensor_inst->fields[field_index].attribute;
		report_id = attribute->report_id;
		if (feature) {
			u8 values[HID_CUSTOM_MAX_FEATURE_BYTES];
			int len = 0;
			u64 value = 0;
			int i = 0;

			ret = sensor_hub_get_feature(sensor_inst->hsdev,
						     report_id,
						     index,
						     sizeof(values), values);
			if (ret < 0)
				return ret;

			while (i < ret) {
				if (i + attribute->size > ret) {
					len += snprintf(&buf[len],
							PAGE_SIZE - len,
							"%d ", values[i]);
					break;
				}
				switch (attribute->size) {
				case 2:
					value = (u64) *(u16 *)&values[i];
					i += attribute->size;
					break;
				case 4:
					value = (u64) *(u32 *)&values[i];
					i += attribute->size;
					break;
				case 8:
					value = *(u64 *)&values[i];
					i += attribute->size;
					break;
				default:
					value = (u64) values[i];
					++i;
					break;
				}
				len += snprintf(&buf[len], PAGE_SIZE - len,
						"%lld ", value);
			}
			len += snprintf(&buf[len], PAGE_SIZE - len, "\n");

			return len;
		} else if (input)
			value = sensor_hub_input_attr_get_raw_value(
						sensor_inst->hsdev,
						sensor_inst->hsdev->usage,
						usage, report_id,
						SENSOR_HUB_SYNC, false);
	} else if (!strncmp(name, "units", strlen("units")))
		value = sensor_inst->fields[field_index].attribute.units;
	else if (!strncmp(name, "unit-expo", strlen("unit-expo")))
		value = sensor_inst->fields[field_index].attribute.unit_expo;
	else if (!strncmp(name, "size", strlen("size")))
		value = sensor_inst->fields[field_index].attribute.size;
	else if (!strncmp(name, "minimum", strlen("minimum")))
		value = sensor_inst->fields[field_index].attribute.
							logical_minimum;
	else if (!strncmp(name, "maximum", strlen("maximum")))
		value = sensor_inst->fields[field_index].attribute.
							logical_maximum;
	else if (!strncmp(name, "name", strlen("name"))) {
		struct hid_custom_usage_desc *usage_desc;

		usage_desc = bsearch(&usage, hid_custom_usage_desc_table,
				     ARRAY_SIZE(hid_custom_usage_desc_table),
				     sizeof(struct hid_custom_usage_desc),
				     usage_id_cmp);
		if (usage_desc)
			return snprintf(buf, PAGE_SIZE, "%s\n",
					usage_desc->desc);
		else
			return sprintf(buf, "not-specified\n");
	 } else
		return -EINVAL;

	return sprintf(buf, "%d\n", value);
}

static ssize_t store_value(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct hid_sensor_custom *sensor_inst = dev_get_drvdata(dev);
	int index, field_index, usage;
	char name[HID_CUSTOM_NAME_LENGTH];
	int value;

	if (sscanf(attr->attr.name, "feature-%x-%x-%s", &index, &usage,
		   name) == 3) {
		field_index = index + sensor_inst->input_field_count;
	} else
		return -EINVAL;

	if (!strncmp(name, "value", strlen("value"))) {
		u32 report_id;
		int ret;

		if (kstrtoint(buf, 0, &value) != 0)
			return -EINVAL;

		report_id = sensor_inst->fields[field_index].attribute.
								report_id;
		ret = sensor_hub_set_feature(sensor_inst->hsdev, report_id,
					     index, sizeof(value), &value);
	} else
		return -EINVAL;

	return count;
}

static int hid_sensor_capture_sample(struct hid_sensor_hub_device *hsdev,
				  unsigned usage_id, size_t raw_len,
				  char *raw_data, void *priv)
{
	struct hid_sensor_custom *sensor_inst = platform_get_drvdata(priv);
	struct hid_sensor_sample header;

	/* If any error occurs in a sample, rest of the fields are ignored */
	if (sensor_inst->input_skip_sample) {
		hid_err(sensor_inst->hsdev->hdev, "Skipped remaining data\n");
		return 0;
	}

	hid_dbg(sensor_inst->hsdev->hdev, "%s received %d of %d\n", __func__,
		(int) (sensor_inst->input_report_recd_size + raw_len),
		sensor_inst->input_report_size);

	if (!test_bit(0, &sensor_inst->misc_opened))
		return 0;

	if (!sensor_inst->input_report_recd_size) {
		int required_size = sizeof(struct hid_sensor_sample) +
						sensor_inst->input_report_size;
		header.usage_id = hsdev->usage;
		header.raw_len = sensor_inst->input_report_size;
		header.timestamp = ktime_get_real_ns();
		if (kfifo_avail(&sensor_inst->data_fifo) >= required_size) {
			kfifo_in(&sensor_inst->data_fifo,
				 (unsigned char *)&header,
				 sizeof(header));
		} else
			sensor_inst->input_skip_sample = true;
	}
	if (kfifo_avail(&sensor_inst->data_fifo) >= raw_len)
		kfifo_in(&sensor_inst->data_fifo, (unsigned char *)raw_data,
			 raw_len);

	sensor_inst->input_report_recd_size += raw_len;

	return 0;
}

static int hid_sensor_send_event(struct hid_sensor_hub_device *hsdev,
				 unsigned usage_id, void *priv)
{
	struct hid_sensor_custom *sensor_inst = platform_get_drvdata(priv);

	if (!test_bit(0, &sensor_inst->misc_opened))
		return 0;

	sensor_inst->input_report_recd_size = 0;
	sensor_inst->input_skip_sample = false;

	wake_up(&sensor_inst->wait);

	return 0;
}

static int hid_sensor_custom_add_field(struct hid_sensor_custom *sensor_inst,
				       int index, int report_type,
				       struct hid_report *report,
				       struct hid_field *field)
{
	struct hid_sensor_custom_field *sensor_field;
	void *fields;

	fields = krealloc(sensor_inst->fields,
			  (sensor_inst->sensor_field_count + 1) *
			   sizeof(struct hid_sensor_custom_field), GFP_KERNEL);
	if (!fields) {
		kfree(sensor_inst->fields);
		return -ENOMEM;
	}
	sensor_inst->fields = fields;
	sensor_field = &sensor_inst->fields[sensor_inst->sensor_field_count];
	sensor_field->attribute.usage_id = sensor_inst->hsdev->usage;
	if (field->logical)
		sensor_field->attribute.attrib_id = field->logical;
	else
		sensor_field->attribute.attrib_id = field->usage[0].hid;

	sensor_field->attribute.index = index;
	sensor_field->attribute.report_id = report->id;
	sensor_field->attribute.units = field->unit;
	sensor_field->attribute.unit_expo = field->unit_exponent;
	sensor_field->attribute.size = (field->report_size / 8);
	sensor_field->attribute.logical_minimum = field->logical_minimum;
	sensor_field->attribute.logical_maximum = field->logical_maximum;

	if (report_type == HID_FEATURE_REPORT)
		snprintf(sensor_field->group_name,
			 sizeof(sensor_field->group_name), "feature-%x-%x",
			 sensor_field->attribute.index,
			 sensor_field->attribute.attrib_id);
	else if (report_type == HID_INPUT_REPORT) {
		snprintf(sensor_field->group_name,
			 sizeof(sensor_field->group_name),
			 "input-%x-%x", sensor_field->attribute.index,
			 sensor_field->attribute.attrib_id);
		sensor_inst->input_field_count++;
		sensor_inst->input_report_size += (field->report_size *
						   field->report_count) / 8;
	}

	memset(&sensor_field->hid_custom_attribute_group, 0,
	       sizeof(struct attribute_group));
	sensor_inst->sensor_field_count++;

	return 0;
}

static int hid_sensor_custom_add_fields(struct hid_sensor_custom *sensor_inst,
					struct hid_report_enum *report_enum,
					int report_type)
{
	int i;
	int ret;
	struct hid_report *report;
	struct hid_field *field;
	struct hid_sensor_hub_device *hsdev = sensor_inst->hsdev;

	list_for_each_entry(report, &report_enum->report_list, list) {
		for (i = 0; i < report->maxfield; ++i) {
			field = report->field[i];
			if (field->maxusage &&
			    ((field->usage[0].collection_index >=
			      hsdev->start_collection_index) &&
			      (field->usage[0].collection_index <
			       hsdev->end_collection_index))) {

				ret = hid_sensor_custom_add_field(sensor_inst,
								  i,
								  report_type,
								  report,
								  field);
				if (ret)
					return ret;

			}
		}
	}

	return 0;
}

static int hid_sensor_custom_add_attributes(struct hid_sensor_custom
								*sensor_inst)
{
	struct hid_sensor_hub_device *hsdev = sensor_inst->hsdev;
	struct hid_device *hdev = hsdev->hdev;
	int ret = -1;
	int i, j;

	for (j = 0; j < HID_REPORT_TYPES; ++j) {
		if (j == HID_OUTPUT_REPORT)
			continue;

		ret = hid_sensor_custom_add_fields(sensor_inst,
						   &hdev->report_enum[j], j);
		if (ret)
			return ret;

	}

	/* Create sysfs attributes */
	for (i = 0; i < sensor_inst->sensor_field_count; ++i) {
		j = 0;
		while (j < HID_CUSTOM_TOTAL_ATTRS &&
		       hid_custom_attrs[j].name) {
			struct device_attribute *device_attr;

			device_attr = &sensor_inst->fields[i].sd_attrs[j];

			snprintf((char *)&sensor_inst->fields[i].attr_name[j],
				 HID_CUSTOM_NAME_LENGTH, "%s-%s",
				 sensor_inst->fields[i].group_name,
				 hid_custom_attrs[j].name);
			sysfs_attr_init(&device_attr->attr);
			device_attr->attr.name =
				(char *)&sensor_inst->fields[i].attr_name[j];
			device_attr->attr.mode = hid_custom_attrs[j].mode;
			device_attr->show = show_value;
			if (hid_custom_attrs[j].mode & S_IWUSR)
				device_attr->store = store_value;
			sensor_inst->fields[i].attrs[j] = &device_attr->attr;
			++j;
		}
		sensor_inst->fields[i].attrs[j] = NULL;
		sensor_inst->fields[i].hid_custom_attribute_group.attrs =
						sensor_inst->fields[i].attrs;
		sensor_inst->fields[i].hid_custom_attribute_group.name =
					sensor_inst->fields[i].group_name;
		ret = sysfs_create_group(&sensor_inst->pdev->dev.kobj,
					 &sensor_inst->fields[i].
					 hid_custom_attribute_group);
		if (ret)
			break;

		/* For power or report field store indexes */
		if (sensor_inst->fields[i].attribute.attrib_id ==
					HID_USAGE_SENSOR_PROY_POWER_STATE)
			sensor_inst->power_state = &sensor_inst->fields[i];
		else if (sensor_inst->fields[i].attribute.attrib_id ==
					HID_USAGE_SENSOR_PROP_REPORT_STATE)
			sensor_inst->report_state = &sensor_inst->fields[i];
	}

	return ret;
}

static void hid_sensor_custom_remove_attributes(struct hid_sensor_custom *
								sensor_inst)
{
	int i;

	for (i = 0; i < sensor_inst->sensor_field_count; ++i)
		sysfs_remove_group(&sensor_inst->pdev->dev.kobj,
				   &sensor_inst->fields[i].
				   hid_custom_attribute_group);

	kfree(sensor_inst->fields);
}

static ssize_t hid_sensor_custom_read(struct file *file, char __user *buf,
				      size_t count, loff_t *f_ps)
{
	struct hid_sensor_custom *sensor_inst;
	unsigned int copied;
	int ret;

	sensor_inst = container_of(file->private_data,
				   struct hid_sensor_custom, custom_dev);

	if (count < sizeof(struct hid_sensor_sample))
		return -EINVAL;

	do {
		if (kfifo_is_empty(&sensor_inst->data_fifo)) {
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			ret = wait_event_interruptible(sensor_inst->wait,
				!kfifo_is_empty(&sensor_inst->data_fifo));
			if (ret)
				return ret;
		}
		ret = kfifo_to_user(&sensor_inst->data_fifo, buf, count,
				    &copied);
		if (ret)
			return ret;

	} while (copied == 0);

	return copied;
}

static int hid_sensor_custom_release(struct inode *inode, struct file *file)
{
	struct hid_sensor_custom *sensor_inst;

	sensor_inst = container_of(file->private_data,
				   struct hid_sensor_custom, custom_dev);

	clear_bit(0, &sensor_inst->misc_opened);

	return 0;
}

static int hid_sensor_custom_open(struct inode *inode, struct file *file)
{
	struct hid_sensor_custom *sensor_inst;

	sensor_inst = container_of(file->private_data,
				   struct hid_sensor_custom, custom_dev);
	/* We essentially have single reader and writer */
	if (test_and_set_bit(0, &sensor_inst->misc_opened))
		return -EBUSY;

	return stream_open(inode, file);
}

static __poll_t hid_sensor_custom_poll(struct file *file,
					   struct poll_table_struct *wait)
{
	struct hid_sensor_custom *sensor_inst;
	__poll_t mask = 0;

	sensor_inst = container_of(file->private_data,
				   struct hid_sensor_custom, custom_dev);

	poll_wait(file, &sensor_inst->wait, wait);

	if (!kfifo_is_empty(&sensor_inst->data_fifo))
		mask = EPOLLIN | EPOLLRDNORM;

	return mask;
}

static const struct file_operations hid_sensor_custom_fops = {
	.open =  hid_sensor_custom_open,
	.read =  hid_sensor_custom_read,
	.release = hid_sensor_custom_release,
	.poll = hid_sensor_custom_poll,
	.llseek = noop_llseek,
};

static int hid_sensor_custom_dev_if_add(struct hid_sensor_custom *sensor_inst)
{
	int ret;

	ret = kfifo_alloc(&sensor_inst->data_fifo, HID_CUSTOM_FIFO_SIZE,
			  GFP_KERNEL);
	if (ret)
		return ret;

	init_waitqueue_head(&sensor_inst->wait);

	sensor_inst->custom_dev.minor = MISC_DYNAMIC_MINOR;
	sensor_inst->custom_dev.name = dev_name(&sensor_inst->pdev->dev);
	sensor_inst->custom_dev.fops = &hid_sensor_custom_fops,
	ret = misc_register(&sensor_inst->custom_dev);
	if (ret) {
		kfifo_free(&sensor_inst->data_fifo);
		return ret;
	}
	return 0;
}

static void hid_sensor_custom_dev_if_remove(struct hid_sensor_custom
								*sensor_inst)
{
	wake_up(&sensor_inst->wait);
	misc_deregister(&sensor_inst->custom_dev);
	kfifo_free(&sensor_inst->data_fifo);

}

static int hid_sensor_custom_probe(struct platform_device *pdev)
{
	struct hid_sensor_custom *sensor_inst;
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
	int ret;

	sensor_inst = devm_kzalloc(&pdev->dev, sizeof(*sensor_inst),
				   GFP_KERNEL);
	if (!sensor_inst)
		return -ENOMEM;

	sensor_inst->callbacks.capture_sample = hid_sensor_capture_sample;
	sensor_inst->callbacks.send_event = hid_sensor_send_event;
	sensor_inst->callbacks.pdev = pdev;
	sensor_inst->hsdev = hsdev;
	sensor_inst->pdev = pdev;
	mutex_init(&sensor_inst->mutex);
	platform_set_drvdata(pdev, sensor_inst);
	ret = sensor_hub_register_callback(hsdev, hsdev->usage,
					   &sensor_inst->callbacks);
	if (ret < 0) {
		dev_err(&pdev->dev, "callback reg failed\n");
		return ret;
	}

	ret = sysfs_create_group(&sensor_inst->pdev->dev.kobj,
				 &enable_sensor_attr_group);
	if (ret)
		goto err_remove_callback;

	ret = hid_sensor_custom_add_attributes(sensor_inst);
	if (ret)
		goto err_remove_group;

	ret = hid_sensor_custom_dev_if_add(sensor_inst);
	if (ret)
		goto err_remove_attributes;

	return 0;

err_remove_attributes:
	hid_sensor_custom_remove_attributes(sensor_inst);
err_remove_group:
	sysfs_remove_group(&sensor_inst->pdev->dev.kobj,
			   &enable_sensor_attr_group);
err_remove_callback:
	sensor_hub_remove_callback(hsdev, hsdev->usage);

	return ret;
}

static int hid_sensor_custom_remove(struct platform_device *pdev)
{
	struct hid_sensor_custom *sensor_inst = platform_get_drvdata(pdev);
	struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;

	hid_sensor_custom_dev_if_remove(sensor_inst);
	hid_sensor_custom_remove_attributes(sensor_inst);
	sysfs_remove_group(&sensor_inst->pdev->dev.kobj,
			   &enable_sensor_attr_group);
	sensor_hub_remove_callback(hsdev, hsdev->usage);

	return 0;
}

static const struct platform_device_id hid_sensor_custom_ids[] = {
	{
		.name = "HID-SENSOR-2000e1",
	},
	{
		.name = "HID-SENSOR-2000e2",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_sensor_custom_ids);

static struct platform_driver hid_sensor_custom_platform_driver = {
	.id_table = hid_sensor_custom_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe		= hid_sensor_custom_probe,
	.remove		= hid_sensor_custom_remove,
};
module_platform_driver(hid_sensor_custom_platform_driver);

MODULE_DESCRIPTION("HID Sensor Custom and Generic sensor Driver");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL");
