// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2009 Luca Tettamanti <kronos.it@gmail.com>
 *
 * See COPYING in the top level directory of the kernel tree.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/hwmon.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/acpi.h>

#define ATK_HID "ATK0110"

static bool new_if;
module_param(new_if, bool, 0);
MODULE_PARM_DESC(new_if, "Override detection heuristic and force the use of the new ATK0110 interface");

static const struct dmi_system_id __initconst atk_force_new_if[] = {
	{
		/* Old interface has broken MCH temp monitoring */
		.ident = "Asus Sabertooth X58",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "SABERTOOTH X58")
		}
	}, {
		/* Old interface reads the same sensor for fan0 and fan1 */
		.ident = "Asus M5A78L",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "M5A78L")
		}
	},
	{ }
};

/*
 * Minimum time between readings, enforced in order to avoid
 * hogging the CPU.
 */
#define CACHE_TIME		HZ

#define BOARD_ID		"MBIF"
#define METHOD_ENUMERATE	"GGRP"
#define METHOD_READ		"GITM"
#define METHOD_WRITE		"SITM"
#define METHOD_OLD_READ_TMP	"RTMP"
#define METHOD_OLD_READ_VLT	"RVLT"
#define METHOD_OLD_READ_FAN	"RFAN"
#define METHOD_OLD_ENUM_TMP	"TSIF"
#define METHOD_OLD_ENUM_VLT	"VSIF"
#define METHOD_OLD_ENUM_FAN	"FSIF"

#define ATK_MUX_HWMON		0x00000006ULL
#define ATK_MUX_MGMT		0x00000011ULL

#define ATK_CLASS_MASK		0xff000000ULL
#define ATK_CLASS_FREQ_CTL	0x03000000ULL
#define ATK_CLASS_FAN_CTL	0x04000000ULL
#define ATK_CLASS_HWMON		0x06000000ULL
#define ATK_CLASS_MGMT		0x11000000ULL

#define ATK_TYPE_MASK		0x00ff0000ULL
#define HWMON_TYPE_VOLT		0x00020000ULL
#define HWMON_TYPE_TEMP		0x00030000ULL
#define HWMON_TYPE_FAN		0x00040000ULL

#define ATK_ELEMENT_ID_MASK	0x0000ffffULL

#define ATK_EC_ID		0x11060004ULL

enum atk_pack_member {
	HWMON_PACK_FLAGS,
	HWMON_PACK_NAME,
	HWMON_PACK_LIMIT1,
	HWMON_PACK_LIMIT2,
	HWMON_PACK_ENABLE
};

/* New package format */
#define _HWMON_NEW_PACK_SIZE	7
#define _HWMON_NEW_PACK_FLAGS	0
#define _HWMON_NEW_PACK_NAME	1
#define _HWMON_NEW_PACK_UNK1	2
#define _HWMON_NEW_PACK_UNK2	3
#define _HWMON_NEW_PACK_LIMIT1	4
#define _HWMON_NEW_PACK_LIMIT2	5
#define _HWMON_NEW_PACK_ENABLE	6

/* Old package format */
#define _HWMON_OLD_PACK_SIZE	5
#define _HWMON_OLD_PACK_FLAGS	0
#define _HWMON_OLD_PACK_NAME	1
#define _HWMON_OLD_PACK_LIMIT1	2
#define _HWMON_OLD_PACK_LIMIT2	3
#define _HWMON_OLD_PACK_ENABLE	4


struct atk_data {
	struct device *hwmon_dev;
	acpi_handle atk_handle;
	struct acpi_device *acpi_dev;

	bool old_interface;

	/* old interface */
	acpi_handle rtmp_handle;
	acpi_handle rvlt_handle;
	acpi_handle rfan_handle;
	/* new interface */
	acpi_handle enumerate_handle;
	acpi_handle read_handle;
	acpi_handle write_handle;

	bool disable_ec;

	int voltage_count;
	int temperature_count;
	int fan_count;
	struct list_head sensor_list;
	struct attribute_group attr_group;
	const struct attribute_group *attr_groups[2];

	struct {
		struct dentry *root;
		u32 id;
	} debugfs;
};


typedef ssize_t (*sysfs_show_func)(struct device *dev,
			struct device_attribute *attr, char *buf);

static const struct acpi_device_id atk_ids[] = {
	{ATK_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, atk_ids);

#define ATTR_NAME_SIZE 16 /* Worst case is "tempN_input" */

struct atk_sensor_data {
	struct list_head list;
	struct atk_data *data;
	struct device_attribute label_attr;
	struct device_attribute input_attr;
	struct device_attribute limit1_attr;
	struct device_attribute limit2_attr;
	char label_attr_name[ATTR_NAME_SIZE];
	char input_attr_name[ATTR_NAME_SIZE];
	char limit1_attr_name[ATTR_NAME_SIZE];
	char limit2_attr_name[ATTR_NAME_SIZE];
	u64 id;
	u64 type;
	u64 limit1;
	u64 limit2;
	u64 cached_value;
	unsigned long last_updated; /* in jiffies */
	bool is_valid;
	char const *acpi_name;
};

/*
 * Return buffer format:
 * [0-3] "value" is valid flag
 * [4-7] value
 * [8- ] unknown stuff on newer mobos
 */
struct atk_acpi_ret_buffer {
	u32 flags;
	u32 value;
	u8 data[];
};

/* Input buffer used for GITM and SITM methods */
struct atk_acpi_input_buf {
	u32 id;
	u32 param1;
	u32 param2;
};

static int atk_add(struct acpi_device *device);
static int atk_remove(struct acpi_device *device);
static void atk_print_sensor(struct atk_data *data, union acpi_object *obj);
static int atk_read_value(struct atk_sensor_data *sensor, u64 *value);

static struct acpi_driver atk_driver = {
	.name	= ATK_HID,
	.class	= "hwmon",
	.ids	= atk_ids,
	.ops	= {
		.add	= atk_add,
		.remove	= atk_remove,
	},
};

#define input_to_atk_sensor(attr) \
	container_of(attr, struct atk_sensor_data, input_attr)

#define label_to_atk_sensor(attr) \
	container_of(attr, struct atk_sensor_data, label_attr)

#define limit1_to_atk_sensor(attr) \
	container_of(attr, struct atk_sensor_data, limit1_attr)

#define limit2_to_atk_sensor(attr) \
	container_of(attr, struct atk_sensor_data, limit2_attr)

static ssize_t atk_input_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct atk_sensor_data *s = input_to_atk_sensor(attr);
	u64 value;
	int err;

	err = atk_read_value(s, &value);
	if (err)
		return err;

	if (s->type == HWMON_TYPE_TEMP)
		/* ACPI returns decidegree */
		value *= 100;

	return sprintf(buf, "%llu\n", value);
}

static ssize_t atk_label_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct atk_sensor_data *s = label_to_atk_sensor(attr);

	return sprintf(buf, "%s\n", s->acpi_name);
}

static ssize_t atk_limit1_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct atk_sensor_data *s = limit1_to_atk_sensor(attr);
	u64 value = s->limit1;

	if (s->type == HWMON_TYPE_TEMP)
		value *= 100;

	return sprintf(buf, "%lld\n", value);
}

static ssize_t atk_limit2_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct atk_sensor_data *s = limit2_to_atk_sensor(attr);
	u64 value = s->limit2;

	if (s->type == HWMON_TYPE_TEMP)
		value *= 100;

	return sprintf(buf, "%lld\n", value);
}

static void atk_init_attribute(struct device_attribute *attr, char *name,
		sysfs_show_func show)
{
	sysfs_attr_init(&attr->attr);
	attr->attr.name = name;
	attr->attr.mode = 0444;
	attr->show = show;
	attr->store = NULL;
}


static union acpi_object *atk_get_pack_member(struct atk_data *data,
						union acpi_object *pack,
						enum atk_pack_member m)
{
	bool old_if = data->old_interface;
	int offset;

	switch (m) {
	case HWMON_PACK_FLAGS:
		offset = old_if ? _HWMON_OLD_PACK_FLAGS : _HWMON_NEW_PACK_FLAGS;
		break;
	case HWMON_PACK_NAME:
		offset = old_if ? _HWMON_OLD_PACK_NAME : _HWMON_NEW_PACK_NAME;
		break;
	case HWMON_PACK_LIMIT1:
		offset = old_if ? _HWMON_OLD_PACK_LIMIT1 :
				  _HWMON_NEW_PACK_LIMIT1;
		break;
	case HWMON_PACK_LIMIT2:
		offset = old_if ? _HWMON_OLD_PACK_LIMIT2 :
				  _HWMON_NEW_PACK_LIMIT2;
		break;
	case HWMON_PACK_ENABLE:
		offset = old_if ? _HWMON_OLD_PACK_ENABLE :
				  _HWMON_NEW_PACK_ENABLE;
		break;
	default:
		return NULL;
	}

	return &pack->package.elements[offset];
}


/*
 * New package format is:
 * - flag (int)
 *	class - used for de-muxing the request to the correct GITn
 *	type (volt, temp, fan)
 *	sensor id |
 *	sensor id - used for de-muxing the request _inside_ the GITn
 * - name (str)
 * - unknown (int)
 * - unknown (int)
 * - limit1 (int)
 * - limit2 (int)
 * - enable (int)
 *
 * The old package has the same format but it's missing the two unknown fields.
 */
static int validate_hwmon_pack(struct atk_data *data, union acpi_object *obj)
{
	struct device *dev = &data->acpi_dev->dev;
	union acpi_object *tmp;
	bool old_if = data->old_interface;
	int const expected_size = old_if ? _HWMON_OLD_PACK_SIZE :
					   _HWMON_NEW_PACK_SIZE;

	if (obj->type != ACPI_TYPE_PACKAGE) {
		dev_warn(dev, "Invalid type: %d\n", obj->type);
		return -EINVAL;
	}

	if (obj->package.count != expected_size) {
		dev_warn(dev, "Invalid package size: %d, expected: %d\n",
				obj->package.count, expected_size);
		return -EINVAL;
	}

	tmp = atk_get_pack_member(data, obj, HWMON_PACK_FLAGS);
	if (tmp->type != ACPI_TYPE_INTEGER) {
		dev_warn(dev, "Invalid type (flag): %d\n", tmp->type);
		return -EINVAL;
	}

	tmp = atk_get_pack_member(data, obj, HWMON_PACK_NAME);
	if (tmp->type != ACPI_TYPE_STRING) {
		dev_warn(dev, "Invalid type (name): %d\n", tmp->type);
		return -EINVAL;
	}

	/* Don't check... we don't know what they're useful for anyway */
#if 0
	tmp = &obj->package.elements[HWMON_PACK_UNK1];
	if (tmp->type != ACPI_TYPE_INTEGER) {
		dev_warn(dev, "Invalid type (unk1): %d\n", tmp->type);
		return -EINVAL;
	}

	tmp = &obj->package.elements[HWMON_PACK_UNK2];
	if (tmp->type != ACPI_TYPE_INTEGER) {
		dev_warn(dev, "Invalid type (unk2): %d\n", tmp->type);
		return -EINVAL;
	}
#endif

	tmp = atk_get_pack_member(data, obj, HWMON_PACK_LIMIT1);
	if (tmp->type != ACPI_TYPE_INTEGER) {
		dev_warn(dev, "Invalid type (limit1): %d\n", tmp->type);
		return -EINVAL;
	}

	tmp = atk_get_pack_member(data, obj, HWMON_PACK_LIMIT2);
	if (tmp->type != ACPI_TYPE_INTEGER) {
		dev_warn(dev, "Invalid type (limit2): %d\n", tmp->type);
		return -EINVAL;
	}

	tmp = atk_get_pack_member(data, obj, HWMON_PACK_ENABLE);
	if (tmp->type != ACPI_TYPE_INTEGER) {
		dev_warn(dev, "Invalid type (enable): %d\n", tmp->type);
		return -EINVAL;
	}

	atk_print_sensor(data, obj);

	return 0;
}

#ifdef DEBUG
static char const *atk_sensor_type(union acpi_object *flags)
{
	u64 type = flags->integer.value & ATK_TYPE_MASK;
	char const *what;

	switch (type) {
	case HWMON_TYPE_VOLT:
		what = "voltage";
		break;
	case HWMON_TYPE_TEMP:
		what = "temperature";
		break;
	case HWMON_TYPE_FAN:
		what = "fan";
		break;
	default:
		what = "unknown";
		break;
	}

	return what;
}
#endif

static void atk_print_sensor(struct atk_data *data, union acpi_object *obj)
{
#ifdef DEBUG
	struct device *dev = &data->acpi_dev->dev;
	union acpi_object *flags;
	union acpi_object *name;
	union acpi_object *limit1;
	union acpi_object *limit2;
	union acpi_object *enable;
	char const *what;

	flags = atk_get_pack_member(data, obj, HWMON_PACK_FLAGS);
	name = atk_get_pack_member(data, obj, HWMON_PACK_NAME);
	limit1 = atk_get_pack_member(data, obj, HWMON_PACK_LIMIT1);
	limit2 = atk_get_pack_member(data, obj, HWMON_PACK_LIMIT2);
	enable = atk_get_pack_member(data, obj, HWMON_PACK_ENABLE);

	what = atk_sensor_type(flags);

	dev_dbg(dev, "%s: %#llx %s [%llu-%llu] %s\n", what,
			flags->integer.value,
			name->string.pointer,
			limit1->integer.value, limit2->integer.value,
			enable->integer.value ? "enabled" : "disabled");
#endif
}

static int atk_read_value_old(struct atk_sensor_data *sensor, u64 *value)
{
	struct atk_data *data = sensor->data;
	struct device *dev = &data->acpi_dev->dev;
	struct acpi_object_list params;
	union acpi_object id;
	acpi_status status;
	acpi_handle method;

	switch (sensor->type) {
	case HWMON_TYPE_VOLT:
		method = data->rvlt_handle;
		break;
	case HWMON_TYPE_TEMP:
		method = data->rtmp_handle;
		break;
	case HWMON_TYPE_FAN:
		method = data->rfan_handle;
		break;
	default:
		return -EINVAL;
	}

	id.type = ACPI_TYPE_INTEGER;
	id.integer.value = sensor->id;

	params.count = 1;
	params.pointer = &id;

	status = acpi_evaluate_integer(method, NULL, &params, value);
	if (status != AE_OK) {
		dev_warn(dev, "%s: ACPI exception: %s\n", __func__,
				acpi_format_exception(status));
		return -EIO;
	}

	return 0;
}

static union acpi_object *atk_ggrp(struct atk_data *data, u16 mux)
{
	struct device *dev = &data->acpi_dev->dev;
	struct acpi_buffer buf;
	acpi_status ret;
	struct acpi_object_list params;
	union acpi_object id;
	union acpi_object *pack;

	id.type = ACPI_TYPE_INTEGER;
	id.integer.value = mux;
	params.count = 1;
	params.pointer = &id;

	buf.length = ACPI_ALLOCATE_BUFFER;
	ret = acpi_evaluate_object(data->enumerate_handle, NULL, &params, &buf);
	if (ret != AE_OK) {
		dev_err(dev, "GGRP[%#x] ACPI exception: %s\n", mux,
				acpi_format_exception(ret));
		return ERR_PTR(-EIO);
	}
	pack = buf.pointer;
	if (pack->type != ACPI_TYPE_PACKAGE) {
		/* Execution was successful, but the id was not found */
		ACPI_FREE(pack);
		return ERR_PTR(-ENOENT);
	}

	if (pack->package.count < 1) {
		dev_err(dev, "GGRP[%#x] package is too small\n", mux);
		ACPI_FREE(pack);
		return ERR_PTR(-EIO);
	}
	return pack;
}

static union acpi_object *atk_gitm(struct atk_data *data, u64 id)
{
	struct device *dev = &data->acpi_dev->dev;
	struct atk_acpi_input_buf buf;
	union acpi_object tmp;
	struct acpi_object_list params;
	struct acpi_buffer ret;
	union acpi_object *obj;
	acpi_status status;

	buf.id = id;
	buf.param1 = 0;
	buf.param2 = 0;

	tmp.type = ACPI_TYPE_BUFFER;
	tmp.buffer.pointer = (u8 *)&buf;
	tmp.buffer.length = sizeof(buf);

	params.count = 1;
	params.pointer = (void *)&tmp;

	ret.length = ACPI_ALLOCATE_BUFFER;
	status = acpi_evaluate_object_typed(data->read_handle, NULL, &params,
			&ret, ACPI_TYPE_BUFFER);
	if (status != AE_OK) {
		dev_warn(dev, "GITM[%#llx] ACPI exception: %s\n", id,
				acpi_format_exception(status));
		return ERR_PTR(-EIO);
	}
	obj = ret.pointer;

	/* Sanity check */
	if (obj->buffer.length < 8) {
		dev_warn(dev, "Unexpected ASBF length: %u\n",
				obj->buffer.length);
		ACPI_FREE(obj);
		return ERR_PTR(-EIO);
	}
	return obj;
}

static union acpi_object *atk_sitm(struct atk_data *data,
		struct atk_acpi_input_buf *buf)
{
	struct device *dev = &data->acpi_dev->dev;
	struct acpi_object_list params;
	union acpi_object tmp;
	struct acpi_buffer ret;
	union acpi_object *obj;
	acpi_status status;

	tmp.type = ACPI_TYPE_BUFFER;
	tmp.buffer.pointer = (u8 *)buf;
	tmp.buffer.length = sizeof(*buf);

	params.count = 1;
	params.pointer = &tmp;

	ret.length = ACPI_ALLOCATE_BUFFER;
	status = acpi_evaluate_object_typed(data->write_handle, NULL, &params,
			&ret, ACPI_TYPE_BUFFER);
	if (status != AE_OK) {
		dev_warn(dev, "SITM[%#x] ACPI exception: %s\n", buf->id,
				acpi_format_exception(status));
		return ERR_PTR(-EIO);
	}
	obj = ret.pointer;

	/* Sanity check */
	if (obj->buffer.length < 8) {
		dev_warn(dev, "Unexpected ASBF length: %u\n",
				obj->buffer.length);
		ACPI_FREE(obj);
		return ERR_PTR(-EIO);
	}
	return obj;
}

static int atk_read_value_new(struct atk_sensor_data *sensor, u64 *value)
{
	struct atk_data *data = sensor->data;
	struct device *dev = &data->acpi_dev->dev;
	union acpi_object *obj;
	struct atk_acpi_ret_buffer *buf;
	int err = 0;

	obj = atk_gitm(data, sensor->id);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	buf = (struct atk_acpi_ret_buffer *)obj->buffer.pointer;
	if (buf->flags == 0) {
		/*
		 * The reading is not valid, possible causes:
		 * - sensor failure
		 * - enumeration was FUBAR (and we didn't notice)
		 */
		dev_warn(dev, "Read failed, sensor = %#llx\n", sensor->id);
		err = -EIO;
		goto out;
	}

	*value = buf->value;
out:
	ACPI_FREE(obj);
	return err;
}

static int atk_read_value(struct atk_sensor_data *sensor, u64 *value)
{
	int err;

	if (!sensor->is_valid ||
	    time_after(jiffies, sensor->last_updated + CACHE_TIME)) {
		if (sensor->data->old_interface)
			err = atk_read_value_old(sensor, value);
		else
			err = atk_read_value_new(sensor, value);

		if (err)
			return err;

		sensor->is_valid = true;
		sensor->last_updated = jiffies;
		sensor->cached_value = *value;
	} else {
		*value = sensor->cached_value;
		err = 0;
	}

	return err;
}

#ifdef CONFIG_DEBUG_FS
static int atk_debugfs_gitm_get(void *p, u64 *val)
{
	struct atk_data *data = p;
	union acpi_object *ret;
	struct atk_acpi_ret_buffer *buf;
	int err = 0;

	if (!data->read_handle)
		return -ENODEV;

	if (!data->debugfs.id)
		return -EINVAL;

	ret = atk_gitm(data, data->debugfs.id);
	if (IS_ERR(ret))
		return PTR_ERR(ret);

	buf = (struct atk_acpi_ret_buffer *)ret->buffer.pointer;
	if (buf->flags)
		*val = buf->value;
	else
		err = -EIO;

	ACPI_FREE(ret);
	return err;
}

DEFINE_DEBUGFS_ATTRIBUTE(atk_debugfs_gitm, atk_debugfs_gitm_get, NULL,
			 "0x%08llx\n");

static int atk_acpi_print(char *buf, size_t sz, union acpi_object *obj)
{
	int ret = 0;

	switch (obj->type) {
	case ACPI_TYPE_INTEGER:
		ret = snprintf(buf, sz, "0x%08llx\n", obj->integer.value);
		break;
	case ACPI_TYPE_STRING:
		ret = snprintf(buf, sz, "%s\n", obj->string.pointer);
		break;
	}

	return ret;
}

static void atk_pack_print(char *buf, size_t sz, union acpi_object *pack)
{
	int ret;
	int i;

	for (i = 0; i < pack->package.count; i++) {
		union acpi_object *obj = &pack->package.elements[i];

		ret = atk_acpi_print(buf, sz, obj);
		if (ret >= sz)
			break;
		buf += ret;
		sz -= ret;
	}
}

static int atk_debugfs_ggrp_open(struct inode *inode, struct file *file)
{
	struct atk_data *data = inode->i_private;
	char *buf = NULL;
	union acpi_object *ret;
	u8 cls;
	int i;

	if (!data->enumerate_handle)
		return -ENODEV;
	if (!data->debugfs.id)
		return -EINVAL;

	cls = (data->debugfs.id & 0xff000000) >> 24;
	ret = atk_ggrp(data, cls);
	if (IS_ERR(ret))
		return PTR_ERR(ret);

	for (i = 0; i < ret->package.count; i++) {
		union acpi_object *pack = &ret->package.elements[i];
		union acpi_object *id;

		if (pack->type != ACPI_TYPE_PACKAGE)
			continue;
		if (!pack->package.count)
			continue;
		id = &pack->package.elements[0];
		if (id->integer.value == data->debugfs.id) {
			/* Print the package */
			buf = kzalloc(512, GFP_KERNEL);
			if (!buf) {
				ACPI_FREE(ret);
				return -ENOMEM;
			}
			atk_pack_print(buf, 512, pack);
			break;
		}
	}
	ACPI_FREE(ret);

	if (!buf)
		return -EINVAL;

	file->private_data = buf;

	return nonseekable_open(inode, file);
}

static ssize_t atk_debugfs_ggrp_read(struct file *file, char __user *buf,
		size_t count, loff_t *pos)
{
	char *str = file->private_data;
	size_t len = strlen(str);

	return simple_read_from_buffer(buf, count, pos, str, len);
}

static int atk_debugfs_ggrp_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static const struct file_operations atk_debugfs_ggrp_fops = {
	.read		= atk_debugfs_ggrp_read,
	.open		= atk_debugfs_ggrp_open,
	.release	= atk_debugfs_ggrp_release,
	.llseek		= no_llseek,
};

static void atk_debugfs_init(struct atk_data *data)
{
	struct dentry *d;
	struct dentry *f;

	data->debugfs.id = 0;

	d = debugfs_create_dir("asus_atk0110", NULL);
	if (!d || IS_ERR(d))
		return;

	f = debugfs_create_x32("id", 0600, d, &data->debugfs.id);
	if (!f || IS_ERR(f))
		goto cleanup;

	f = debugfs_create_file_unsafe("gitm", 0400, d, data,
				       &atk_debugfs_gitm);
	if (!f || IS_ERR(f))
		goto cleanup;

	f = debugfs_create_file("ggrp", 0400, d, data,
				&atk_debugfs_ggrp_fops);
	if (!f || IS_ERR(f))
		goto cleanup;

	data->debugfs.root = d;

	return;
cleanup:
	debugfs_remove_recursive(d);
}

static void atk_debugfs_cleanup(struct atk_data *data)
{
	debugfs_remove_recursive(data->debugfs.root);
}

#else /* CONFIG_DEBUG_FS */

static void atk_debugfs_init(struct atk_data *data)
{
}

static void atk_debugfs_cleanup(struct atk_data *data)
{
}
#endif

static int atk_add_sensor(struct atk_data *data, union acpi_object *obj)
{
	struct device *dev = &data->acpi_dev->dev;
	union acpi_object *flags;
	union acpi_object *name;
	union acpi_object *limit1;
	union acpi_object *limit2;
	union acpi_object *enable;
	struct atk_sensor_data *sensor;
	char const *base_name;
	char const *limit1_name;
	char const *limit2_name;
	u64 type;
	int err;
	int *num;
	int start;

	if (obj->type != ACPI_TYPE_PACKAGE) {
		/* wft is this? */
		dev_warn(dev, "Unknown type for ACPI object: (%d)\n",
				obj->type);
		return -EINVAL;
	}

	err = validate_hwmon_pack(data, obj);
	if (err)
		return err;

	/* Ok, we have a valid hwmon package */
	type = atk_get_pack_member(data, obj, HWMON_PACK_FLAGS)->integer.value
	       & ATK_TYPE_MASK;

	switch (type) {
	case HWMON_TYPE_VOLT:
		base_name = "in";
		limit1_name = "min";
		limit2_name = "max";
		num = &data->voltage_count;
		start = 0;
		break;
	case HWMON_TYPE_TEMP:
		base_name = "temp";
		limit1_name = "max";
		limit2_name = "crit";
		num = &data->temperature_count;
		start = 1;
		break;
	case HWMON_TYPE_FAN:
		base_name = "fan";
		limit1_name = "min";
		limit2_name = "max";
		num = &data->fan_count;
		start = 1;
		break;
	default:
		dev_warn(dev, "Unknown sensor type: %#llx\n", type);
		return -EINVAL;
	}

	enable = atk_get_pack_member(data, obj, HWMON_PACK_ENABLE);
	if (!enable->integer.value)
		/* sensor is disabled */
		return 0;

	flags = atk_get_pack_member(data, obj, HWMON_PACK_FLAGS);
	name = atk_get_pack_member(data, obj, HWMON_PACK_NAME);
	limit1 = atk_get_pack_member(data, obj, HWMON_PACK_LIMIT1);
	limit2 = atk_get_pack_member(data, obj, HWMON_PACK_LIMIT2);

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->acpi_name = devm_kstrdup(dev, name->string.pointer, GFP_KERNEL);
	if (!sensor->acpi_name)
		return -ENOMEM;

	INIT_LIST_HEAD(&sensor->list);
	sensor->type = type;
	sensor->data = data;
	sensor->id = flags->integer.value;
	sensor->limit1 = limit1->integer.value;
	if (data->old_interface)
		sensor->limit2 = limit2->integer.value;
	else
		/* The upper limit is expressed as delta from lower limit */
		sensor->limit2 = sensor->limit1 + limit2->integer.value;

	snprintf(sensor->input_attr_name, ATTR_NAME_SIZE,
			"%s%d_input", base_name, start + *num);
	atk_init_attribute(&sensor->input_attr,
			sensor->input_attr_name,
			atk_input_show);

	snprintf(sensor->label_attr_name, ATTR_NAME_SIZE,
			"%s%d_label", base_name, start + *num);
	atk_init_attribute(&sensor->label_attr,
			sensor->label_attr_name,
			atk_label_show);

	snprintf(sensor->limit1_attr_name, ATTR_NAME_SIZE,
			"%s%d_%s", base_name, start + *num, limit1_name);
	atk_init_attribute(&sensor->limit1_attr,
			sensor->limit1_attr_name,
			atk_limit1_show);

	snprintf(sensor->limit2_attr_name, ATTR_NAME_SIZE,
			"%s%d_%s", base_name, start + *num, limit2_name);
	atk_init_attribute(&sensor->limit2_attr,
			sensor->limit2_attr_name,
			atk_limit2_show);

	list_add(&sensor->list, &data->sensor_list);
	(*num)++;

	return 1;
}

static int atk_enumerate_old_hwmon(struct atk_data *data)
{
	struct device *dev = &data->acpi_dev->dev;
	struct acpi_buffer buf;
	union acpi_object *pack;
	acpi_status status;
	int i, ret;
	int count = 0;

	/* Voltages */
	buf.length = ACPI_ALLOCATE_BUFFER;
	status = acpi_evaluate_object_typed(data->atk_handle,
			METHOD_OLD_ENUM_VLT, NULL, &buf, ACPI_TYPE_PACKAGE);
	if (status != AE_OK) {
		dev_warn(dev, METHOD_OLD_ENUM_VLT ": ACPI exception: %s\n",
				acpi_format_exception(status));

		return -ENODEV;
	}

	pack = buf.pointer;
	for (i = 1; i < pack->package.count; i++) {
		union acpi_object *obj = &pack->package.elements[i];

		ret = atk_add_sensor(data, obj);
		if (ret > 0)
			count++;
	}
	ACPI_FREE(buf.pointer);

	/* Temperatures */
	buf.length = ACPI_ALLOCATE_BUFFER;
	status = acpi_evaluate_object_typed(data->atk_handle,
			METHOD_OLD_ENUM_TMP, NULL, &buf, ACPI_TYPE_PACKAGE);
	if (status != AE_OK) {
		dev_warn(dev, METHOD_OLD_ENUM_TMP ": ACPI exception: %s\n",
				acpi_format_exception(status));

		return -ENODEV;
	}

	pack = buf.pointer;
	for (i = 1; i < pack->package.count; i++) {
		union acpi_object *obj = &pack->package.elements[i];

		ret = atk_add_sensor(data, obj);
		if (ret > 0)
			count++;
	}
	ACPI_FREE(buf.pointer);

	/* Fans */
	buf.length = ACPI_ALLOCATE_BUFFER;
	status = acpi_evaluate_object_typed(data->atk_handle,
			METHOD_OLD_ENUM_FAN, NULL, &buf, ACPI_TYPE_PACKAGE);
	if (status != AE_OK) {
		dev_warn(dev, METHOD_OLD_ENUM_FAN ": ACPI exception: %s\n",
				acpi_format_exception(status));

		return -ENODEV;
	}

	pack = buf.pointer;
	for (i = 1; i < pack->package.count; i++) {
		union acpi_object *obj = &pack->package.elements[i];

		ret = atk_add_sensor(data, obj);
		if (ret > 0)
			count++;
	}
	ACPI_FREE(buf.pointer);

	return count;
}

static int atk_ec_present(struct atk_data *data)
{
	struct device *dev = &data->acpi_dev->dev;
	union acpi_object *pack;
	union acpi_object *ec;
	int ret;
	int i;

	pack = atk_ggrp(data, ATK_MUX_MGMT);
	if (IS_ERR(pack)) {
		if (PTR_ERR(pack) == -ENOENT) {
			/* The MGMT class does not exists - that's ok */
			dev_dbg(dev, "Class %#llx not found\n", ATK_MUX_MGMT);
			return 0;
		}
		return PTR_ERR(pack);
	}

	/* Search the EC */
	ec = NULL;
	for (i = 0; i < pack->package.count; i++) {
		union acpi_object *obj = &pack->package.elements[i];
		union acpi_object *id;

		if (obj->type != ACPI_TYPE_PACKAGE)
			continue;

		id = &obj->package.elements[0];
		if (id->type != ACPI_TYPE_INTEGER)
			continue;

		if (id->integer.value == ATK_EC_ID) {
			ec = obj;
			break;
		}
	}

	ret = (ec != NULL);
	if (!ret)
		/* The system has no EC */
		dev_dbg(dev, "EC not found\n");

	ACPI_FREE(pack);
	return ret;
}

static int atk_ec_enabled(struct atk_data *data)
{
	struct device *dev = &data->acpi_dev->dev;
	union acpi_object *obj;
	struct atk_acpi_ret_buffer *buf;
	int err;

	obj = atk_gitm(data, ATK_EC_ID);
	if (IS_ERR(obj)) {
		dev_err(dev, "Unable to query EC status\n");
		return PTR_ERR(obj);
	}
	buf = (struct atk_acpi_ret_buffer *)obj->buffer.pointer;

	if (buf->flags == 0) {
		dev_err(dev, "Unable to query EC status\n");
		err = -EIO;
	} else {
		err = (buf->value != 0);
		dev_dbg(dev, "EC is %sabled\n",
				err ? "en" : "dis");
	}

	ACPI_FREE(obj);
	return err;
}

static int atk_ec_ctl(struct atk_data *data, int enable)
{
	struct device *dev = &data->acpi_dev->dev;
	union acpi_object *obj;
	struct atk_acpi_input_buf sitm;
	struct atk_acpi_ret_buffer *ec_ret;
	int err = 0;

	sitm.id = ATK_EC_ID;
	sitm.param1 = enable;
	sitm.param2 = 0;

	obj = atk_sitm(data, &sitm);
	if (IS_ERR(obj)) {
		dev_err(dev, "Failed to %sable the EC\n",
				enable ? "en" : "dis");
		return PTR_ERR(obj);
	}
	ec_ret = (struct atk_acpi_ret_buffer *)obj->buffer.pointer;
	if (ec_ret->flags == 0) {
		dev_err(dev, "Failed to %sable the EC\n",
				enable ? "en" : "dis");
		err = -EIO;
	} else {
		dev_info(dev, "EC %sabled\n",
				enable ? "en" : "dis");
	}

	ACPI_FREE(obj);
	return err;
}

static int atk_enumerate_new_hwmon(struct atk_data *data)
{
	struct device *dev = &data->acpi_dev->dev;
	union acpi_object *pack;
	int err;
	int i;

	err = atk_ec_present(data);
	if (err < 0)
		return err;
	if (err) {
		err = atk_ec_enabled(data);
		if (err < 0)
			return err;
		/* If the EC was disabled we will disable it again on unload */
		data->disable_ec = err;

		err = atk_ec_ctl(data, 1);
		if (err) {
			data->disable_ec = false;
			return err;
		}
	}

	dev_dbg(dev, "Enumerating hwmon sensors\n");

	pack = atk_ggrp(data, ATK_MUX_HWMON);
	if (IS_ERR(pack))
		return PTR_ERR(pack);

	for (i = 0; i < pack->package.count; i++) {
		union acpi_object *obj = &pack->package.elements[i];

		atk_add_sensor(data, obj);
	}

	err = data->voltage_count + data->temperature_count + data->fan_count;

	ACPI_FREE(pack);
	return err;
}

static int atk_init_attribute_groups(struct atk_data *data)
{
	struct device *dev = &data->acpi_dev->dev;
	struct atk_sensor_data *s;
	struct attribute **attrs;
	int i = 0;
	int len = (data->voltage_count + data->temperature_count
			+ data->fan_count) * 4 + 1;

	attrs = devm_kcalloc(dev, len, sizeof(struct attribute *), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	list_for_each_entry(s, &data->sensor_list, list) {
		attrs[i++] = &s->input_attr.attr;
		attrs[i++] = &s->label_attr.attr;
		attrs[i++] = &s->limit1_attr.attr;
		attrs[i++] = &s->limit2_attr.attr;
	}

	data->attr_group.attrs = attrs;
	data->attr_groups[0] = &data->attr_group;

	return 0;
}

static int atk_register_hwmon(struct atk_data *data)
{
	struct device *dev = &data->acpi_dev->dev;

	dev_dbg(dev, "registering hwmon device\n");
	data->hwmon_dev = hwmon_device_register_with_groups(dev, "atk0110",
							    data,
							    data->attr_groups);

	return PTR_ERR_OR_ZERO(data->hwmon_dev);
}

static int atk_probe_if(struct atk_data *data)
{
	struct device *dev = &data->acpi_dev->dev;
	acpi_handle ret;
	acpi_status status;
	int err = 0;

	/* RTMP: read temperature */
	status = acpi_get_handle(data->atk_handle, METHOD_OLD_READ_TMP, &ret);
	if (ACPI_SUCCESS(status))
		data->rtmp_handle = ret;
	else
		dev_dbg(dev, "method " METHOD_OLD_READ_TMP " not found: %s\n",
				acpi_format_exception(status));

	/* RVLT: read voltage */
	status = acpi_get_handle(data->atk_handle, METHOD_OLD_READ_VLT, &ret);
	if (ACPI_SUCCESS(status))
		data->rvlt_handle = ret;
	else
		dev_dbg(dev, "method " METHOD_OLD_READ_VLT " not found: %s\n",
				acpi_format_exception(status));

	/* RFAN: read fan status */
	status = acpi_get_handle(data->atk_handle, METHOD_OLD_READ_FAN, &ret);
	if (ACPI_SUCCESS(status))
		data->rfan_handle = ret;
	else
		dev_dbg(dev, "method " METHOD_OLD_READ_FAN " not found: %s\n",
				acpi_format_exception(status));

	/* Enumeration */
	status = acpi_get_handle(data->atk_handle, METHOD_ENUMERATE, &ret);
	if (ACPI_SUCCESS(status))
		data->enumerate_handle = ret;
	else
		dev_dbg(dev, "method " METHOD_ENUMERATE " not found: %s\n",
				acpi_format_exception(status));

	/* De-multiplexer (read) */
	status = acpi_get_handle(data->atk_handle, METHOD_READ, &ret);
	if (ACPI_SUCCESS(status))
		data->read_handle = ret;
	else
		dev_dbg(dev, "method " METHOD_READ " not found: %s\n",
				acpi_format_exception(status));

	/* De-multiplexer (write) */
	status = acpi_get_handle(data->atk_handle, METHOD_WRITE, &ret);
	if (ACPI_SUCCESS(status))
		data->write_handle = ret;
	else
		dev_dbg(dev, "method " METHOD_WRITE " not found: %s\n",
				 acpi_format_exception(status));

	/*
	 * Check for hwmon methods: first check "old" style methods; note that
	 * both may be present: in this case we stick to the old interface;
	 * analysis of multiple DSDTs indicates that when both interfaces
	 * are present the new one (GGRP/GITM) is not functional.
	 */
	if (new_if)
		dev_info(dev, "Overriding interface detection\n");
	if (data->rtmp_handle &&
			data->rvlt_handle && data->rfan_handle && !new_if)
		data->old_interface = true;
	else if (data->enumerate_handle && data->read_handle &&
			data->write_handle)
		data->old_interface = false;
	else
		err = -ENODEV;

	return err;
}

static int atk_add(struct acpi_device *device)
{
	acpi_status ret;
	int err;
	struct acpi_buffer buf;
	union acpi_object *obj;
	struct atk_data *data;

	dev_dbg(&device->dev, "adding...\n");

	data = devm_kzalloc(&device->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->acpi_dev = device;
	data->atk_handle = device->handle;
	INIT_LIST_HEAD(&data->sensor_list);
	data->disable_ec = false;

	buf.length = ACPI_ALLOCATE_BUFFER;
	ret = acpi_evaluate_object_typed(data->atk_handle, BOARD_ID, NULL,
			&buf, ACPI_TYPE_PACKAGE);
	if (ret != AE_OK) {
		dev_dbg(&device->dev, "atk: method MBIF not found\n");
	} else {
		obj = buf.pointer;
		if (obj->package.count >= 2) {
			union acpi_object *id = &obj->package.elements[1];
			if (id->type == ACPI_TYPE_STRING)
				dev_dbg(&device->dev, "board ID = %s\n",
					id->string.pointer);
		}
		ACPI_FREE(buf.pointer);
	}

	err = atk_probe_if(data);
	if (err) {
		dev_err(&device->dev, "No usable hwmon interface detected\n");
		goto out;
	}

	if (data->old_interface) {
		dev_dbg(&device->dev, "Using old hwmon interface\n");
		err = atk_enumerate_old_hwmon(data);
	} else {
		dev_dbg(&device->dev, "Using new hwmon interface\n");
		err = atk_enumerate_new_hwmon(data);
	}
	if (err < 0)
		goto out;
	if (err == 0) {
		dev_info(&device->dev,
			 "No usable sensor detected, bailing out\n");
		err = -ENODEV;
		goto out;
	}

	err = atk_init_attribute_groups(data);
	if (err)
		goto out;
	err = atk_register_hwmon(data);
	if (err)
		goto out;

	atk_debugfs_init(data);

	device->driver_data = data;
	return 0;
out:
	if (data->disable_ec)
		atk_ec_ctl(data, 0);
	return err;
}

static int atk_remove(struct acpi_device *device)
{
	struct atk_data *data = device->driver_data;
	dev_dbg(&device->dev, "removing...\n");

	device->driver_data = NULL;

	atk_debugfs_cleanup(data);

	hwmon_device_unregister(data->hwmon_dev);

	if (data->disable_ec) {
		if (atk_ec_ctl(data, 0))
			dev_err(&device->dev, "Failed to disable EC\n");
	}

	return 0;
}

static int __init atk0110_init(void)
{
	int ret;

	/* Make sure it's safe to access the device through ACPI */
	if (!acpi_resources_are_enforced()) {
		pr_err("Resources not safely usable due to acpi_enforce_resources kernel parameter\n");
		return -EBUSY;
	}

	if (dmi_check_system(atk_force_new_if))
		new_if = true;

	ret = acpi_bus_register_driver(&atk_driver);
	if (ret)
		pr_info("acpi_bus_register_driver failed: %d\n", ret);

	return ret;
}

static void __exit atk0110_exit(void)
{
	acpi_bus_unregister_driver(&atk_driver);
}

module_init(atk0110_init);
module_exit(atk0110_exit);

MODULE_LICENSE("GPL");
