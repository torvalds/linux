// SPDX-License-Identifier: GPL-2.0-only
/*
 * coreboot CFR firmware attributes driver.
 *
 * Parses LB_TAG_CFR_ROOT records from the coreboot table and exposes
 * runtime EFI variable-backed options through the firmware-attributes class.
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/compiler_attributes.h>
#include <linux/container_of.h>
#include <linux/crc32.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/device-id/coreboot.h>
#include <linux/efi.h>
#include <linux/err.h>
#include <linux/firmware_attributes.h>
#include <linux/io.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "coreboot_table.h"

#define DRIVER_NAME "coreboot-cfr"

#define LB_TAG_CFR_ROOT		0x47
#define CFR_VERSION		0

enum cfr_tags {
	CFR_TAG_OPTION_FORM		= 1,
	CFR_TAG_ENUM_VALUE		= 2,
	CFR_TAG_OPTION_ENUM		= 3,
	CFR_TAG_OPTION_NUMBER		= 4,
	CFR_TAG_OPTION_BOOL		= 5,
	CFR_TAG_VARCHAR_OPT_NAME	= 7,
	CFR_TAG_VARCHAR_UI_NAME		= 8,
	CFR_TAG_RUNTIME_APPLY		= 13,
};

enum cfr_option_flags {
	CFR_OPTFLAG_READONLY	= BIT(0),
	CFR_OPTFLAG_INACTIVE	= BIT(1),
	CFR_OPTFLAG_SUPPRESS	= BIT(2),
	CFR_OPTFLAG_VOLATILE	= BIT(3),
	CFR_OPTFLAG_RUNTIME	= BIT(4),
};

enum cfr_runtime_apply_method {
	CFR_RUNTIME_APPLY_NONE		= 0,
	CFR_RUNTIME_APPLY_APM_CNT	= 1,
};

struct lb_cfr {
	u32 tag;
	u32 size;
	u32 version;
	u32 checksum;
} __packed;

struct lb_cfr_varbinary {
	u32 tag;
	u32 size;
	u32 data_length;
} __packed;

struct lb_cfr_enum_value {
	u32 tag;
	u32 size;
	u32 value;
} __packed;

struct lb_cfr_runtime_apply {
	u32 tag;
	u32 size;
	u32 method;
	u32 id;
} __packed;

struct lb_cfr_numeric_option {
	u32 tag;
	u32 size;
	cb_u64 object_id;
	cb_u64 dependency_id;
	u32 flags;
	u32 default_value;
	u32 min;
	u32 max;
	u32 step;
	u32 display_flags;
} __packed;

struct lb_cfr_option_form {
	u32 tag;
	u32 size;
	cb_u64 object_id;
	cb_u64 dependency_id;
	u32 flags;
} __packed;

#define COREBOOT_CFR_OPT_SKIP_FLAGS \
	(CFR_OPTFLAG_SUPPRESS | CFR_OPTFLAG_VOLATILE)

#define COREBOOT_CFR_OPT_READ_ONLY_FLAGS \
	(CFR_OPTFLAG_READONLY | CFR_OPTFLAG_INACTIVE)

#define COREBOOT_CFR_EFI_ATTRS \
	(EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | \
	 EFI_VARIABLE_RUNTIME_ACCESS)

#define COREBOOT_CFR_APM_CNT_PORT	0xb2
#define COREBOOT_CFR_APM_STS_PORT	0xb3
#define COREBOOT_CFR_APM_APPLY_CMD	0xe3
#define COREBOOT_CFR_MAX_FORM_DEPTH	16

static efi_guid_t coreboot_cfr_guid = EFI_GUID(0xceae4c1d, 0x335b, 0x4685,
					       0xa4, 0xa0, 0xfc, 0x4a,
					       0x94, 0xee, 0xa0, 0x85);

enum coreboot_cfr_setting_type {
	COREBOOT_CFR_SETTING_ENUM,
	COREBOOT_CFR_SETTING_NUMBER,
	COREBOOT_CFR_SETTING_BOOL,
};

struct coreboot_cfr_enum {
	char *label;
	u32 value;
};

struct coreboot_cfr_setting {
	struct kobject kobj;
	struct list_head node;
	struct coreboot_cfr_drvdata *drvdata;
	enum coreboot_cfr_setting_type type;
	char *name;
	char *display_name;
	struct coreboot_cfr_enum *values;
	unsigned int n_values;
	u32 default_value;
	u32 min;
	u32 max;
	u32 step;
	u32 runtime_apply_method;
	u32 runtime_apply_id;
	bool read_only;
};

struct coreboot_cfr_drvdata {
	struct device *class_dev;
	struct kset *attrs_kset;
	struct list_head settings;
	/* Serializes EFI variable writes and the matching runtime apply hook. */
	struct mutex lock;
	bool efi_writes_supported;
	bool pending_reboot;
};

struct coreboot_cfr_iterator {
	const u8 *cursor;
	size_t remaining;
};

static struct coreboot_cfr_setting *to_coreboot_cfr_setting(struct kobject *kobj)
{
	return container_of(kobj, struct coreboot_cfr_setting, kobj);
}

static bool coreboot_cfr_string_is_valid_name(const char *name)
{
	return name && name[0] && !strchr(name, '/') &&
		strcmp(name, "pending_reboot") != 0;
}

static bool coreboot_cfr_string_is_valid_label(const char *label)
{
	return label && !strchr(label, ';') && !strchr(label, '\n');
}

static char *coreboot_cfr_string_dup(const struct lb_cfr_varbinary *str)
{
	const char *data = (const char *)(str + 1);
	size_t len = str->data_length;

	if (len && !data[len - 1])
		len--;

	return kmemdup_nul(data, len, GFP_KERNEL);
}

static const struct coreboot_table_entry *
coreboot_cfr_next_entry(struct coreboot_cfr_iterator *iterator)
{
	const struct coreboot_table_entry *entry;

	if (!iterator->remaining)
		return NULL;

	if (iterator->remaining < sizeof(*entry))
		return ERR_PTR(-EINVAL);

	entry = (const struct coreboot_table_entry *)iterator->cursor;
	if (entry->size < sizeof(*entry) || entry->size > iterator->remaining)
		return ERR_PTR(-EINVAL);

	iterator->cursor += entry->size;
	iterator->remaining -= entry->size;

	return entry;
}

static const struct coreboot_table_entry *
coreboot_cfr_child_entry(const void *base, size_t len, u32 tag)
{
	struct coreboot_cfr_iterator iterator = {
		.cursor = base,
		.remaining = len,
	};
	const struct coreboot_table_entry *entry;

	for (;;) {
		entry = coreboot_cfr_next_entry(&iterator);
		if (IS_ERR_OR_NULL(entry))
			return entry;

		if (entry->tag == tag)
			return entry;
	}
}

static const struct lb_cfr_varbinary *
coreboot_cfr_child_string(const void *base, size_t len, u32 tag)
{
	const struct lb_cfr_varbinary *str;
	const struct coreboot_table_entry *entry;

	entry = coreboot_cfr_child_entry(base, len, tag);
	if (IS_ERR(entry))
		return ERR_CAST(entry);
	if (!entry)
		return NULL;

	if (entry->size < sizeof(*str))
		return ERR_PTR(-EINVAL);

	str = (const struct lb_cfr_varbinary *)entry;
	if (str->data_length > entry->size - sizeof(*str))
		return ERR_PTR(-EINVAL);

	return str;
}

static const struct lb_cfr_runtime_apply *
coreboot_cfr_child_runtime_apply(const void *base, size_t len)
{
	const struct lb_cfr_runtime_apply *runtime_apply;
	const struct coreboot_table_entry *entry;

	entry = coreboot_cfr_child_entry(base, len, CFR_TAG_RUNTIME_APPLY);
	if (IS_ERR(entry))
		return ERR_CAST(entry);
	if (!entry)
		return NULL;

	if (entry->size < sizeof(*runtime_apply))
		return ERR_PTR(-EINVAL);

	runtime_apply = (const struct lb_cfr_runtime_apply *)entry;
	if (runtime_apply->method == CFR_RUNTIME_APPLY_APM_CNT &&
	    runtime_apply->id > U8_MAX)
		return ERR_PTR(-EINVAL);

	return runtime_apply;
}

static efi_char16_t *coreboot_cfr_efi_name(const char *name)
{
	size_t len, i;

	len = strlen(name);
	if (len >= EFI_VAR_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	efi_char16_t *efi_name __free(kfree) =
		kcalloc(len + 1, sizeof(*efi_name), GFP_KERNEL);
	if (!efi_name)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < len; i++) {
		if (!isascii(name[i]))
			return ERR_PTR(-EINVAL);
		efi_name[i] = name[i];
	}

	return no_free_ptr(efi_name);
}

/* Caller must hold the efivar lock. */
static int coreboot_cfr_read_efi_value(efi_char16_t *efi_name, u32 *value,
				       u32 *attrs)
{
	unsigned long size = sizeof(__le32);
	efi_status_t status;
	__le32 data;
	u32 attr;

	status = efivar_get_variable(efi_name, &coreboot_cfr_guid, &attr,
				     &size, &data);
	if (status != EFI_SUCCESS)
		return efi_status_to_err(status);

	if (size != sizeof(data))
		return -EINVAL;

	if (!(attr & EFI_VARIABLE_RUNTIME_ACCESS))
		return -EOPNOTSUPP;

	*value = le32_to_cpu(data);
	if (attrs)
		*attrs = attr;

	return 0;
}

static int coreboot_cfr_read_value(const struct coreboot_cfr_setting *setting,
				   u32 *value, u32 *attrs)
{
	efi_char16_t *efi_name __free(kfree) =
		coreboot_cfr_efi_name(setting->name);
	int ret;

	if (IS_ERR(efi_name))
		return PTR_ERR(no_free_ptr(efi_name));

	ret = efivar_lock();
	if (ret)
		return ret;

	ret = coreboot_cfr_read_efi_value(efi_name, value, attrs);
	efivar_unlock();

	return ret;
}

/* Caller must hold the efivar lock. */
static int coreboot_cfr_write_efi_value(efi_char16_t *efi_name, u32 value,
					u32 attrs)
{
	efi_status_t status;
	__le32 data;

	if (!efivar_supports_writes())
		return -EROFS;

	data = cpu_to_le32(value);
	status = efivar_set_variable_locked(efi_name, &coreboot_cfr_guid, attrs,
					    sizeof(data), &data, false);
	if (status != EFI_SUCCESS)
		return efi_status_to_err(status);

	return 0;
}

static int coreboot_cfr_apply_runtime(struct coreboot_cfr_setting *setting)
{
	u8 status;

	if (setting->runtime_apply_method != CFR_RUNTIME_APPLY_APM_CNT)
		return -EOPNOTSUPP;

	outb((u8)setting->runtime_apply_id, COREBOOT_CFR_APM_STS_PORT);
	outb(COREBOOT_CFR_APM_APPLY_CMD, COREBOOT_CFR_APM_CNT_PORT);
	status = inb(COREBOOT_CFR_APM_STS_PORT);
	if (status)
		return -EIO;

	return 0;
}

static int coreboot_cfr_write_value(struct coreboot_cfr_setting *setting,
				    u32 value)
{
	efi_char16_t *efi_name;
	u32 attrs;
	u32 old;
	int restore_ret;
	int ret;

	if (setting->read_only)
		return -EACCES;

	efi_name = coreboot_cfr_efi_name(setting->name);
	if (IS_ERR(efi_name))
		return PTR_ERR(efi_name);

	mutex_lock(&setting->drvdata->lock);

	ret = efivar_lock();
	if (ret)
		goto out_unlock_mutex;

	ret = coreboot_cfr_read_efi_value(efi_name, &old, &attrs);
	if (ret)
		goto out_unlock_efi;

	if ((attrs & COREBOOT_CFR_EFI_ATTRS) != COREBOOT_CFR_EFI_ATTRS) {
		ret = -EOPNOTSUPP;
		goto out_unlock_efi;
	}

	if (old == value)
		goto out_unlock_efi;

	ret = coreboot_cfr_write_efi_value(efi_name, value, attrs);
	if (ret)
		goto out_unlock_efi;

	ret = coreboot_cfr_apply_runtime(setting);
	if (ret == -EOPNOTSUPP) {
		/* EFI changed; firmware will consume it after reboot. */
		setting->drvdata->pending_reboot = true;
		ret = 0;
	} else if (ret) {
		restore_ret = coreboot_cfr_write_efi_value(efi_name, old, attrs);
		if (restore_ret) {
			setting->drvdata->pending_reboot = true;
			ret = restore_ret;
		} else {
			goto out_unlock_efi;
		}
	}

	efivar_unlock();
	kobject_uevent(&setting->drvdata->class_dev->kobj, KOBJ_CHANGE);
	mutex_unlock(&setting->drvdata->lock);
	kfree(efi_name);
	return ret;

out_unlock_efi:
	efivar_unlock();
out_unlock_mutex:
	mutex_unlock(&setting->drvdata->lock);
	kfree(efi_name);
	return ret;
}

static const char *
coreboot_cfr_label_from_value(const struct coreboot_cfr_setting *setting,
			      u32 value)
{
	unsigned int i;

	for (i = 0; i < setting->n_values; i++) {
		if (setting->values[i].value == value)
			return setting->values[i].label;
	}

	return NULL;
}

static int coreboot_cfr_parse_value(struct coreboot_cfr_setting *setting,
				    const char *label, u32 *value_out)
{
	unsigned int i;

	for (i = 0; i < setting->n_values; i++) {
		if (!sysfs_streq(label, setting->values[i].label))
			continue;

		*value_out = setting->values[i].value;
		return 0;
	}

	return kstrtou32(label, 0, value_out);
}

static bool coreboot_cfr_value_is_valid(struct coreboot_cfr_setting *setting,
					u32 value)
{
	u32 delta;

	if (setting->type != COREBOOT_CFR_SETTING_NUMBER) {
		/* Enum and bool values are valid only if they have a label. */
		return coreboot_cfr_label_from_value(setting, value);
	}

	if (value < setting->min || value > setting->max)
		return false;

	if (!setting->step)
		return true;

	delta = value - setting->min;
	return delta % setting->step == 0;
}

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);

	if (setting->type == COREBOOT_CFR_SETTING_NUMBER)
		return sysfs_emit(buf, "integer\n");

	return sysfs_emit(buf, "enumeration\n");
}

static ssize_t display_name_language_code_show(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       char *buf)
{
	return sysfs_emit(buf, "en_US.UTF-8\n");
}

static ssize_t display_name_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);

	return sysfs_emit(buf, "%s\n", setting->display_name);
}

static ssize_t possible_values_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);
	ssize_t len = 0;
	unsigned int i;

	for (i = 0; i < setting->n_values; i++) {
		len += sysfs_emit_at(buf, len, "%s%s", i ? ";" : "",
				     setting->values[i].label);
	}

	len += sysfs_emit_at(buf, len, "\n");
	return len;
}

static ssize_t min_value_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);

	return sysfs_emit(buf, "%u\n", setting->min);
}

static ssize_t max_value_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);

	return sysfs_emit(buf, "%u\n", setting->max);
}

static ssize_t scalar_increment_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);

	return sysfs_emit(buf, "%u\n", setting->step);
}

static ssize_t default_value_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);
	const char *label;

	if (setting->type == COREBOOT_CFR_SETTING_NUMBER)
		return sysfs_emit(buf, "%u\n", setting->default_value);

	label = coreboot_cfr_label_from_value(setting, setting->default_value);
	if (!label)
		return sysfs_emit(buf, "%u\n", setting->default_value);

	return sysfs_emit(buf, "%s\n", label);
}

static ssize_t current_value_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);
	const char *label;
	u32 value;
	int ret;

	ret = coreboot_cfr_read_value(setting, &value, NULL);
	if (ret)
		return ret;

	if (setting->type == COREBOOT_CFR_SETTING_NUMBER)
		return sysfs_emit(buf, "%u\n", value);

	label = coreboot_cfr_label_from_value(setting, value);
	if (!label)
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", label);
}

static ssize_t current_value_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);
	u32 value;
	int ret;

	ret = coreboot_cfr_parse_value(setting, buf, &value);
	if (ret)
		return ret;

	if (!coreboot_cfr_value_is_valid(setting, value))
		return -EINVAL;

	ret = coreboot_cfr_write_value(setting, value);
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute type_attr = __ATTR_RO(type);
static struct kobj_attribute display_name_language_code_attr =
	__ATTR_RO(display_name_language_code);
static struct kobj_attribute display_name_attr = __ATTR_RO(display_name);
static struct kobj_attribute possible_values_attr = __ATTR_RO(possible_values);
static struct kobj_attribute min_value_attr = __ATTR_RO(min_value);
static struct kobj_attribute max_value_attr = __ATTR_RO(max_value);
static struct kobj_attribute scalar_increment_attr = __ATTR_RO(scalar_increment);
static struct kobj_attribute default_value_attr = __ATTR_RO(default_value);
static struct kobj_attribute current_value_attr = __ATTR_RW(current_value);

static struct attribute *coreboot_cfr_setting_attrs[] = {
	&type_attr.attr,
	&display_name_language_code_attr.attr,
	&display_name_attr.attr,
	&possible_values_attr.attr,
	&min_value_attr.attr,
	&max_value_attr.attr,
	&scalar_increment_attr.attr,
	&default_value_attr.attr,
	&current_value_attr.attr,
	NULL,
};

static umode_t coreboot_cfr_attr_is_visible(struct kobject *kobj,
					    struct attribute *attr, int n)
{
	struct coreboot_cfr_setting *setting = to_coreboot_cfr_setting(kobj);

	if (setting->type == COREBOOT_CFR_SETTING_NUMBER &&
	    attr == &possible_values_attr.attr)
		return 0;

	if (setting->type != COREBOOT_CFR_SETTING_NUMBER &&
	    (attr == &min_value_attr.attr || attr == &max_value_attr.attr ||
	     attr == &scalar_increment_attr.attr))
		return 0;

	if (setting->read_only && attr == &current_value_attr.attr)
		return 0444;

	return attr->mode;
}

static const struct attribute_group coreboot_cfr_setting_group = {
	.attrs = coreboot_cfr_setting_attrs,
	.is_visible = coreboot_cfr_attr_is_visible,
};

static void coreboot_cfr_free_setting(struct coreboot_cfr_setting *setting)
{
	unsigned int i;

	for (i = 0; i < setting->n_values; i++)
		kfree(setting->values[i].label);

	kfree(setting->values);
	kfree(setting->display_name);
	kfree(setting->name);
	kfree(setting);
}

static void coreboot_cfr_setting_release(struct kobject *kobj)
{
	coreboot_cfr_free_setting(to_coreboot_cfr_setting(kobj));
}

static const struct kobj_type coreboot_cfr_setting_ktype = {
	.release = coreboot_cfr_setting_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static ssize_t pending_reboot_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct device *dev = kobj_to_dev(kobj->parent);
	struct coreboot_cfr_drvdata *data;

	data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", data->pending_reboot);
}

static struct kobj_attribute pending_reboot_attr = __ATTR_RO(pending_reboot);

static int coreboot_cfr_copy_bool_values(struct coreboot_cfr_setting *setting)
{
	static const struct coreboot_cfr_enum bool_values[] = {
		{ .label = "Disabled", .value = 0 },
		{ .label = "Enabled", .value = 1 },
	};
	unsigned int i;

	setting->values = kcalloc(ARRAY_SIZE(bool_values), sizeof(*setting->values),
				  GFP_KERNEL);
	if (!setting->values)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(bool_values); i++) {
		setting->values[i].label = kstrdup(bool_values[i].label,
						   GFP_KERNEL);
		if (!setting->values[i].label)
			return -ENOMEM;
		setting->values[i].value = bool_values[i].value;
		setting->n_values++;
	}

	return 0;
}

static int coreboot_cfr_count_enum_values(const void *base, size_t len)
{
	struct coreboot_cfr_iterator iterator = {
		.cursor = base,
		.remaining = len,
	};
	const struct coreboot_table_entry *entry;
	int count = 0;

	for (;;) {
		entry = coreboot_cfr_next_entry(&iterator);
		if (IS_ERR(entry))
			return PTR_ERR(entry);
		if (!entry)
			return count;

		if (entry->tag == CFR_TAG_ENUM_VALUE)
			count++;
	}
}

static int coreboot_cfr_copy_enum_values(struct coreboot_cfr_setting *setting,
					 const void *base, size_t len)
{
	struct coreboot_cfr_iterator iterator = {
		.cursor = base,
		.remaining = len,
	};
	const struct lb_cfr_enum_value *enum_value;
	const struct lb_cfr_varbinary *label;
	const struct coreboot_table_entry *entry;
	struct coreboot_cfr_enum *value;
	int count;

	count = coreboot_cfr_count_enum_values(base, len);
	if (count <= 0)
		return count ?: -EINVAL;

	setting->values = kcalloc(count, sizeof(*setting->values), GFP_KERNEL);
	if (!setting->values)
		return -ENOMEM;

	for (;;) {
		entry = coreboot_cfr_next_entry(&iterator);
		if (IS_ERR(entry))
			return PTR_ERR(entry);
		if (!entry)
			return 0;

		if (entry->tag != CFR_TAG_ENUM_VALUE)
			continue;

		if (entry->size < sizeof(*enum_value))
			return -EINVAL;

		enum_value = (const struct lb_cfr_enum_value *)entry;
		label = coreboot_cfr_child_string(enum_value + 1,
						  enum_value->size - sizeof(*enum_value),
						  CFR_TAG_VARCHAR_UI_NAME);
		if (IS_ERR(label))
			return PTR_ERR(label);
		if (!label)
			return -EINVAL;

		value = &setting->values[setting->n_values];
		value->label = coreboot_cfr_string_dup(label);
		if (!value->label)
			return -ENOMEM;
		if (!coreboot_cfr_string_is_valid_label(value->label)) {
			kfree(value->label);
			value->label = NULL;
			return -EINVAL;
		}

		value->value = enum_value->value;
		setting->n_values++;
	}
}

static bool
coreboot_cfr_possible_values_fit(const struct coreboot_cfr_setting *setting)
{
	size_t len = 1;		/* Trailing newline. */
	size_t label_len;
	unsigned int i;

	for (i = 0; i < setting->n_values; i++) {
		if (len >= PAGE_SIZE)
			return false;

		if (i)
			len++;

		label_len = strlen(setting->values[i].label);
		if (label_len >= PAGE_SIZE - len)
			return false;

		len += label_len;
	}

	return true;
}

static int coreboot_cfr_setting_is_usable(struct coreboot_cfr_setting *setting)
{
	u32 value;
	int ret;

	ret = coreboot_cfr_read_value(setting, &value, NULL);
	if (ret)
		return ret;

	if (!coreboot_cfr_value_is_valid(setting, value))
		return -EINVAL;

	return 0;
}

static int coreboot_cfr_register_setting(struct coreboot_cfr_drvdata *data,
					 struct coreboot_cfr_setting *setting)
{
	int ret;

	ret = kobject_init_and_add(&setting->kobj, &coreboot_cfr_setting_ktype,
				   &data->attrs_kset->kobj, "%s", setting->name);
	if (ret)
		goto err_put_kobj;

	ret = sysfs_create_group(&setting->kobj, &coreboot_cfr_setting_group);
	if (ret)
		goto err_put_kobj;

	list_add_tail(&setting->node, &data->settings);
	return 0;

err_put_kobj:
	kobject_put(&setting->kobj);
	return ret;
}

static int coreboot_cfr_add_numeric_option(struct coreboot_cfr_drvdata *data,
					   const struct lb_cfr_numeric_option *option,
					   bool parent_read_only)
{
	const struct lb_cfr_varbinary *name;
	const struct lb_cfr_varbinary *display_name;
	const struct lb_cfr_runtime_apply *runtime_apply;
	const void *child_base = option + 1;
	struct coreboot_cfr_setting *setting;
	size_t child_len = option->size - sizeof(*option);
	int ret;

	if (!(option->flags & CFR_OPTFLAG_RUNTIME))
		return 0;

	if (option->flags & COREBOOT_CFR_OPT_SKIP_FLAGS)
		return 0;

	if (option->dependency_id)
		return 0;

	setting = kzalloc_obj(*setting, GFP_KERNEL);
	if (!setting)
		return -ENOMEM;

	INIT_LIST_HEAD(&setting->node);
	setting->drvdata = data;
	setting->default_value = option->default_value;
	setting->min = option->min;
	setting->max = option->max;
	setting->step = option->step ?: 1;
	setting->read_only =
		(option->flags & COREBOOT_CFR_OPT_READ_ONLY_FLAGS) ||
		!data->efi_writes_supported || parent_read_only;

	runtime_apply = coreboot_cfr_child_runtime_apply(child_base, child_len);
	if (IS_ERR(runtime_apply)) {
		ret = PTR_ERR(runtime_apply);
		goto err_put_setting;
	}

	if (runtime_apply && runtime_apply->method == CFR_RUNTIME_APPLY_APM_CNT) {
		setting->runtime_apply_method = runtime_apply->method;
		setting->runtime_apply_id = runtime_apply->id;
	}

	name = coreboot_cfr_child_string(child_base, child_len,
					 CFR_TAG_VARCHAR_OPT_NAME);
	if (IS_ERR(name)) {
		ret = PTR_ERR(name);
		goto err_put_setting;
	}
	if (!name) {
		ret = -EINVAL;
		goto err_put_setting;
	}

	setting->name = coreboot_cfr_string_dup(name);
	if (!setting->name) {
		ret = -ENOMEM;
		goto err_put_setting;
	}

	if (!coreboot_cfr_string_is_valid_name(setting->name)) {
		ret = -EINVAL;
		goto err_put_setting;
	}

	display_name = coreboot_cfr_child_string(child_base, child_len,
						 CFR_TAG_VARCHAR_UI_NAME);
	if (IS_ERR(display_name)) {
		ret = PTR_ERR(display_name);
		goto err_put_setting;
	}
	if (display_name)
		setting->display_name = coreboot_cfr_string_dup(display_name);
	else
		setting->display_name = kstrdup(setting->name, GFP_KERNEL);
	if (!setting->display_name) {
		ret = -ENOMEM;
		goto err_put_setting;
	}

	switch (option->tag) {
	case CFR_TAG_OPTION_BOOL:
		setting->type = COREBOOT_CFR_SETTING_BOOL;
		setting->min = 0;
		setting->max = 1;
		setting->step = 1;
		ret = coreboot_cfr_copy_bool_values(setting);
		break;
	case CFR_TAG_OPTION_ENUM:
		setting->type = COREBOOT_CFR_SETTING_ENUM;
		ret = coreboot_cfr_copy_enum_values(setting, child_base,
						    child_len);
		break;
	case CFR_TAG_OPTION_NUMBER:
		setting->type = COREBOOT_CFR_SETTING_NUMBER;
		if (setting->max < setting->min)
			ret = -EINVAL;
		else
			ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto err_put_setting;
	/* possible_values must be returned completely in one sysfs read. */
	if (setting->type != COREBOOT_CFR_SETTING_NUMBER &&
	    !coreboot_cfr_possible_values_fit(setting)) {
		ret = 0;
		goto err_put_setting;
	}

	ret = coreboot_cfr_setting_is_usable(setting);
	if (ret) {
		/*
		 * CFR may describe options without a matching, readable runtime
		 * EFI variable. Skip those, but propagate transient EFI failures.
		 */
		if (ret == -ENOENT || ret == -EINVAL || ret == -EOPNOTSUPP ||
		    ret == -ENAMETOOLONG)
			ret = 0;
		goto err_put_setting;
	}

	/* The kobject release callback owns setting after this call. */
	return coreboot_cfr_register_setting(data, setting);

err_put_setting:
	coreboot_cfr_free_setting(setting);
	return ret;
}

static int coreboot_cfr_parse_records(struct coreboot_cfr_drvdata *data,
				      const void *base, size_t len,
				      unsigned int depth,
				      bool parent_read_only)
{
	struct coreboot_cfr_iterator iterator = {
		.cursor = base,
		.remaining = len,
	};
	const struct lb_cfr_option_form *form;
	const struct lb_cfr_numeric_option *option;
	const struct coreboot_table_entry *entry;
	const void *child_base;
	size_t child_len;
	int ret;

	for (;;) {
		entry = coreboot_cfr_next_entry(&iterator);
		if (IS_ERR(entry))
			return PTR_ERR(entry);
		if (!entry)
			return 0;

		switch (entry->tag) {
		case CFR_TAG_OPTION_FORM:
			if (entry->size < sizeof(struct lb_cfr_option_form))
				return -EINVAL;

			form = (const struct lb_cfr_option_form *)entry;
			if (form->dependency_id ||
			    form->flags & CFR_OPTFLAG_SUPPRESS)
				break;

			if (depth >= COREBOOT_CFR_MAX_FORM_DEPTH)
				return -E2BIG;

			child_base = form + 1;
			child_len = entry->size - sizeof(struct lb_cfr_option_form);
			ret = coreboot_cfr_parse_records(data, child_base,
							 child_len, depth + 1,
							 parent_read_only ||
							 (form->flags &
							  COREBOOT_CFR_OPT_READ_ONLY_FLAGS));
			if (ret)
				return ret;
			break;
		case CFR_TAG_OPTION_ENUM:
		case CFR_TAG_OPTION_NUMBER:
		case CFR_TAG_OPTION_BOOL:
			option = (const struct lb_cfr_numeric_option *)entry;
			if (entry->size < sizeof(*option))
				return -EINVAL;
			ret = coreboot_cfr_add_numeric_option(data, option,
							      parent_read_only);
			if (ret)
				return ret;
			break;
		default:
			/* Ignore unsupported, child-only and future record types. */
			break;
		}
	}
}

static void coreboot_cfr_unregister_settings(struct coreboot_cfr_drvdata *data)
{
	struct coreboot_cfr_setting *setting, *tmp;

	list_for_each_entry_safe(setting, tmp, &data->settings, node) {
		sysfs_remove_group(&setting->kobj, &coreboot_cfr_setting_group);
		list_del(&setting->node);
		kobject_put(&setting->kobj);
	}
}

static int coreboot_cfr_probe(struct coreboot_device *dev)
{
	const struct lb_cfr *root = (const struct lb_cfr *)dev->raw;
	struct coreboot_cfr_drvdata *data;
	size_t payload_len;
	int ret;

	if (dev->entry.size < sizeof(*root))
		return -EINVAL;

	if (root->tag != LB_TAG_CFR_ROOT || root->version != CFR_VERSION)
		return -EINVAL;

	if (root->size < sizeof(*root) || root->size > dev->entry.size)
		return -EINVAL;

	payload_len = root->size - sizeof(*root);
	if (crc32_be(0, root + 1, payload_len) != root->checksum)
		return -EBADMSG;

	if (!efivar_is_available())
		return -EPROBE_DEFER;

	data = devm_kzalloc(&dev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = efivar_lock();
	if (ret)
		return ret;
	data->efi_writes_supported = efivar_supports_writes();
	efivar_unlock();

	INIT_LIST_HEAD(&data->settings);
	ret = devm_mutex_init(&dev->dev, &data->lock);
	if (ret)
		return ret;

	dev_set_drvdata(&dev->dev, data);

	data->class_dev = device_create(&firmware_attributes_class, NULL,
					MKDEV(0, 0), NULL, DRIVER_NAME);
	if (IS_ERR(data->class_dev))
		return PTR_ERR(data->class_dev);
	dev_set_drvdata(data->class_dev, data);

	data->attrs_kset = kset_create_and_add("attributes", NULL,
					       &data->class_dev->kobj);
	if (!data->attrs_kset) {
		ret = -ENOMEM;
		goto err_unregister_dev;
	}

	ret = sysfs_create_file(&data->attrs_kset->kobj,
				&pending_reboot_attr.attr);
	if (ret)
		goto err_unregister_attrs;

	ret = coreboot_cfr_parse_records(data, root + 1, payload_len, 0, false);
	if (ret)
		goto err_unregister_settings;

	if (list_empty(&data->settings)) {
		ret = -ENODEV;
		goto err_unregister_settings;
	}

	return 0;

err_unregister_settings:
	coreboot_cfr_unregister_settings(data);
	sysfs_remove_file(&data->attrs_kset->kobj, &pending_reboot_attr.attr);
err_unregister_attrs:
	kset_unregister(data->attrs_kset);
err_unregister_dev:
	device_unregister(data->class_dev);
	return ret;
}

static void coreboot_cfr_remove(struct coreboot_device *dev)
{
	struct coreboot_cfr_drvdata *data = dev_get_drvdata(&dev->dev);

	coreboot_cfr_unregister_settings(data);
	sysfs_remove_file(&data->attrs_kset->kobj, &pending_reboot_attr.attr);
	kset_unregister(data->attrs_kset);
	device_unregister(data->class_dev);
}

static const struct coreboot_device_id coreboot_cfr_ids[] = {
	{ .tag = LB_TAG_CFR_ROOT },
	{ }
};
MODULE_DEVICE_TABLE(coreboot, coreboot_cfr_ids);

static struct coreboot_driver coreboot_cfr_driver = {
	.probe = coreboot_cfr_probe,
	.remove = coreboot_cfr_remove,
	.drv = {
		.name = DRIVER_NAME,
	},
	.id_table = coreboot_cfr_ids,
};
module_coreboot_driver(coreboot_cfr_driver);

MODULE_AUTHOR("Sean Rhodes <sean@starlabs.systems>");
MODULE_DESCRIPTION("coreboot CFR firmware attributes driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EFIVAR");
