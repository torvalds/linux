// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Think LMI BIOS configuration driver
 *
 * Copyright(C) 2019-2021 Lenovo
 *
 * Original code from Thinkpad-wmi project https://github.com/iksaif/thinkpad-wmi
 * Copyright(C) 2017 Corentin Chary <corentin.chary@gmail.com>
 * Distributed under the GPL-2.0 license
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wmi.h>
#include "firmware_attributes_class.h"
#include "think-lmi.h"

static bool debug_support;
module_param(debug_support, bool, 0444);
MODULE_PARM_DESC(debug_support, "Enable debug command support");

/*
 * Name:
 *  Lenovo_BiosSetting
 * Description:
 *  Get item name and settings for current LMI instance.
 * Type:
 *  Query
 * Returns:
 *  "Item,Value"
 * Example:
 *  "WakeOnLAN,Enable"
 */
#define LENOVO_BIOS_SETTING_GUID "51F5230E-9677-46CD-A1CF-C0B23EE34DB7"

/*
 * Name:
 *  Lenovo_SetBiosSetting
 * Description:
 *  Change the BIOS setting to the desired value using the Lenovo_SetBiosSetting
 *  class. To save the settings, use the Lenovo_SaveBiosSetting class.
 *  BIOS settings and values are case sensitive.
 *  After making changes to the BIOS settings, you must reboot the computer
 *  before the changes will take effect.
 * Type:
 *  Method
 * Arguments:
 *  "Item,Value,Password,Encoding,KbdLang;"
 * Example:
 *  "WakeOnLAN,Disable,pa55w0rd,ascii,us;"
 */
#define LENOVO_SET_BIOS_SETTINGS_GUID "98479A64-33F5-4E33-A707-8E251EBBC3A1"

/*
 * Name:
 *  Lenovo_SaveBiosSettings
 * Description:
 *  Save any pending changes in settings.
 * Type:
 *  Method
 * Arguments:
 *  "Password,Encoding,KbdLang;"
 * Example:
 * "pa55w0rd,ascii,us;"
 */
#define LENOVO_SAVE_BIOS_SETTINGS_GUID "6A4B54EF-A5ED-4D33-9455-B0D9B48DF4B3"

/*
 * Name:
 *  Lenovo_BiosPasswordSettings
 * Description:
 *  Return BIOS Password settings
 * Type:
 *  Query
 * Returns:
 *  PasswordMode, PasswordState, MinLength, MaxLength,
 *  SupportedEncoding, SupportedKeyboard
 */
#define LENOVO_BIOS_PASSWORD_SETTINGS_GUID "8ADB159E-1E32-455C-BC93-308A7ED98246"

/*
 * Name:
 *  Lenovo_SetBiosPassword
 * Description:
 *  Change a specific password.
 *  - BIOS settings cannot be changed at the same boot as power-on
 *    passwords (POP) and hard disk passwords (HDP). If you want to change
 *    BIOS settings and POP or HDP, you must reboot the system after changing
 *    one of them.
 *  - A password cannot be set using this method when one does not already
 *    exist. Passwords can only be updated or cleared.
 * Type:
 *  Method
 * Arguments:
 *  "PasswordType,CurrentPassword,NewPassword,Encoding,KbdLang;"
 * Example:
 *  "pop,pa55w0rd,newpa55w0rd,ascii,us;”
 */
#define LENOVO_SET_BIOS_PASSWORD_GUID "2651D9FD-911C-4B69-B94E-D0DED5963BD7"

/*
 * Name:
 *  Lenovo_GetBiosSelections
 * Description:
 *  Return a list of valid settings for a given item.
 * Type:
 *  Method
 * Arguments:
 *  "Item"
 * Returns:
 *  "Value1,Value2,Value3,..."
 * Example:
 *  -> "FlashOverLAN"
 *  <- "Enabled,Disabled"
 */
#define LENOVO_GET_BIOS_SELECTIONS_GUID	"7364651A-132F-4FE7-ADAA-40C6C7EE2E3B"

/*
 * Name:
 *  Lenovo_DebugCmdGUID
 * Description
 *  Debug entry GUID method for entering debug commands to the BIOS
 */
#define LENOVO_DEBUG_CMD_GUID "7FF47003-3B6C-4E5E-A227-E979824A85D1"

#define TLMI_POP_PWD (1 << 0)
#define TLMI_PAP_PWD (1 << 1)
#define to_tlmi_pwd_setting(kobj)  container_of(kobj, struct tlmi_pwd_setting, kobj)
#define to_tlmi_attr_setting(kobj)  container_of(kobj, struct tlmi_attr_setting, kobj)

static const struct tlmi_err_codes tlmi_errs[] = {
	{"Success", 0},
	{"Not Supported", -EOPNOTSUPP},
	{"Invalid Parameter", -EINVAL},
	{"Access Denied", -EACCES},
	{"System Busy", -EBUSY},
};

static const char * const encoding_options[] = {
	[TLMI_ENCODING_ASCII] = "ascii",
	[TLMI_ENCODING_SCANCODE] = "scancode",
};
static struct think_lmi tlmi_priv;
static struct class *fw_attr_class;

/* ------ Utility functions ------------*/
/* Convert BIOS WMI error string to suitable error code */
static int tlmi_errstr_to_err(const char *errstr)
{
	int i;

	for (i = 0; i < sizeof(tlmi_errs)/sizeof(struct tlmi_err_codes); i++) {
		if (!strcmp(tlmi_errs[i].err_str, errstr))
			return tlmi_errs[i].err_code;
	}
	return -EPERM;
}

/* Extract error string from WMI return buffer */
static int tlmi_extract_error(const struct acpi_buffer *output)
{
	const union acpi_object *obj;

	obj = output->pointer;
	if (!obj)
		return -ENOMEM;
	if (obj->type != ACPI_TYPE_STRING || !obj->string.pointer)
		return -EIO;

	return tlmi_errstr_to_err(obj->string.pointer);
}

/* Utility function to execute WMI call to BIOS */
static int tlmi_simple_call(const char *guid, const char *arg)
{
	const struct acpi_buffer input = { strlen(arg), (char *)arg };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	int i, err;

	/*
	 * Duplicated call required to match BIOS workaround for behavior
	 * seen when WMI accessed via scripting on other OS.
	 */
	for (i = 0; i < 2; i++) {
		/* (re)initialize output buffer to default state */
		output.length = ACPI_ALLOCATE_BUFFER;
		output.pointer = NULL;

		status = wmi_evaluate_method(guid, 0, 0, &input, &output);
		if (ACPI_FAILURE(status)) {
			kfree(output.pointer);
			return -EIO;
		}
		err = tlmi_extract_error(&output);
		kfree(output.pointer);
		if (err)
			return err;
	}
	return 0;
}

/* Extract output string from WMI return buffer */
static int tlmi_extract_output_string(const struct acpi_buffer *output,
				      char **string)
{
	const union acpi_object *obj;
	char *s;

	obj = output->pointer;
	if (!obj)
		return -ENOMEM;
	if (obj->type != ACPI_TYPE_STRING || !obj->string.pointer)
		return -EIO;

	s = kstrdup(obj->string.pointer, GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	*string = s;
	return 0;
}

/* ------ Core interface functions ------------*/

/* Get password settings from BIOS */
static int tlmi_get_pwd_settings(struct tlmi_pwdcfg *pwdcfg)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	const union acpi_object *obj;
	acpi_status status;

	if (!tlmi_priv.can_get_password_settings)
		return -EOPNOTSUPP;

	status = wmi_query_block(LENOVO_BIOS_PASSWORD_SETTINGS_GUID, 0,
				 &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = output.pointer;
	if (!obj)
		return -ENOMEM;
	if (obj->type != ACPI_TYPE_BUFFER || !obj->buffer.pointer) {
		kfree(obj);
		return -EIO;
	}
	/*
	 * The size of thinkpad_wmi_pcfg on ThinkStation is larger than ThinkPad.
	 * To make the driver compatible on different brands, we permit it to get
	 * the data in below case.
	 */
	if (obj->buffer.length < sizeof(struct tlmi_pwdcfg)) {
		pr_warn("Unknown pwdcfg buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return -EIO;
	}
	memcpy(pwdcfg, obj->buffer.pointer, sizeof(struct tlmi_pwdcfg));
	kfree(obj);
	return 0;
}

static int tlmi_save_bios_settings(const char *password)
{
	return tlmi_simple_call(LENOVO_SAVE_BIOS_SETTINGS_GUID,
				password);
}

static int tlmi_setting(int item, char **value, const char *guid_string)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	int ret;

	status = wmi_query_block(guid_string, item, &output);
	if (ACPI_FAILURE(status)) {
		kfree(output.pointer);
		return -EIO;
	}

	ret = tlmi_extract_output_string(&output, value);
	kfree(output.pointer);
	return ret;
}

static int tlmi_get_bios_selections(const char *item, char **value)
{
	const struct acpi_buffer input = { strlen(item), (char *)item };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	int ret;

	status = wmi_evaluate_method(LENOVO_GET_BIOS_SELECTIONS_GUID,
				     0, 0, &input, &output);

	if (ACPI_FAILURE(status)) {
		kfree(output.pointer);
		return -EIO;
	}

	ret = tlmi_extract_output_string(&output, value);
	kfree(output.pointer);
	return ret;
}

/* ---- Authentication sysfs --------------------------------------------------------- */
static ssize_t is_enabled_show(struct kobject *kobj, struct kobj_attribute *attr,
					  char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	return sysfs_emit(buf, "%d\n", setting->valid);
}

static struct kobj_attribute auth_is_pass_set = __ATTR_RO(is_enabled);

static ssize_t current_password_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	size_t pwdlen;
	char *p;

	pwdlen = strlen(buf);
	/* pwdlen == 0 is allowed to clear the password */
	if (pwdlen && ((pwdlen < setting->minlen) || (pwdlen > setting->maxlen)))
		return -EINVAL;

	strscpy(setting->password, buf, setting->maxlen);
	/* Strip out CR if one is present, setting password won't work if it is present */
	p = strchrnul(setting->password, '\n');
	*p = '\0';
	return count;
}

static struct kobj_attribute auth_current_password = __ATTR_WO(current_password);

static ssize_t new_password_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	char *auth_str, *new_pwd, *p;
	size_t pwdlen;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!tlmi_priv.can_set_bios_password)
		return -EOPNOTSUPP;

	new_pwd = kstrdup(buf, GFP_KERNEL);
	if (!new_pwd)
		return -ENOMEM;

	/* Strip out CR if one is present, setting password won't work if it is present */
	p = strchrnul(new_pwd, '\n');
	*p = '\0';

	pwdlen = strlen(new_pwd);
	/* pwdlen == 0 is allowed to clear the password */
	if (pwdlen && ((pwdlen < setting->minlen) || (pwdlen > setting->maxlen))) {
		ret = -EINVAL;
		goto out;
	}

	/* Format: 'PasswordType,CurrentPw,NewPw,Encoding,KbdLang;' */
	auth_str = kasprintf(GFP_KERNEL, "%s,%s,%s,%s,%s;",
		 setting->pwd_type, setting->password, new_pwd,
		 encoding_options[setting->encoding], setting->kbdlang);
	if (!auth_str) {
		ret = -ENOMEM;
		goto out;
	}
	ret = tlmi_simple_call(LENOVO_SET_BIOS_PASSWORD_GUID, auth_str);
	kfree(auth_str);
out:
	kfree(new_pwd);
	return ret ?: count;
}

static struct kobj_attribute auth_new_password = __ATTR_WO(new_password);

static ssize_t min_password_length_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	return sysfs_emit(buf, "%d\n", setting->minlen);
}

static struct kobj_attribute auth_min_pass_length = __ATTR_RO(min_password_length);

static ssize_t max_password_length_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	return sysfs_emit(buf, "%d\n", setting->maxlen);
}
static struct kobj_attribute auth_max_pass_length = __ATTR_RO(max_password_length);

static ssize_t mechanism_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "password\n");
}
static struct kobj_attribute auth_mechanism = __ATTR_RO(mechanism);

static ssize_t encoding_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	return sysfs_emit(buf, "%s\n", encoding_options[setting->encoding]);
}

static ssize_t encoding_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	int i;

	/* Scan for a matching profile */
	i = sysfs_match_string(encoding_options, buf);
	if (i < 0)
		return -EINVAL;

	setting->encoding = i;
	return count;
}

static struct kobj_attribute auth_encoding = __ATTR_RW(encoding);

static ssize_t kbdlang_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	return sysfs_emit(buf, "%s\n", setting->kbdlang);
}

static ssize_t kbdlang_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	int length;

	/* Calculate length till '\n' or terminating 0 */
	length = strchrnul(buf, '\n') - buf;
	if (!length || length >= TLMI_LANG_MAXLEN)
		return -EINVAL;

	memcpy(setting->kbdlang, buf, length);
	setting->kbdlang[length] = '\0';
	return count;
}

static struct kobj_attribute auth_kbdlang = __ATTR_RW(kbdlang);

static ssize_t role_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	return sysfs_emit(buf, "%s\n", setting->role);
}
static struct kobj_attribute auth_role = __ATTR_RO(role);

static struct attribute *auth_attrs[] = {
	&auth_is_pass_set.attr,
	&auth_min_pass_length.attr,
	&auth_max_pass_length.attr,
	&auth_current_password.attr,
	&auth_new_password.attr,
	&auth_role.attr,
	&auth_mechanism.attr,
	&auth_encoding.attr,
	&auth_kbdlang.attr,
	NULL
};

static const struct attribute_group auth_attr_group = {
	.attrs = auth_attrs,
};

/* ---- Attributes sysfs --------------------------------------------------------- */
static ssize_t display_name_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);

	return sysfs_emit(buf, "%s\n", setting->display_name);
}

static ssize_t current_value_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);
	char *item, *value;
	int ret;

	ret = tlmi_setting(setting->index, &item, LENOVO_BIOS_SETTING_GUID);
	if (ret)
		return ret;

	/* validate and split from `item,value` -> `value` */
	value = strpbrk(item, ",");
	if (!value || value == item || !strlen(value + 1))
		return -EINVAL;

	ret = sysfs_emit(buf, "%s\n", value + 1);
	kfree(item);
	return ret;
}

static ssize_t possible_values_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);

	if (!tlmi_priv.can_get_bios_selections)
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%s\n", setting->possible_values);
}

static ssize_t current_value_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);
	char *set_str = NULL, *new_setting = NULL;
	char *auth_str = NULL;
	char *p;
	int ret;

	if (!tlmi_priv.can_set_bios_settings)
		return -EOPNOTSUPP;

	new_setting = kstrdup(buf, GFP_KERNEL);
	if (!new_setting)
		return -ENOMEM;

	/* Strip out CR if one is present */
	p = strchrnul(new_setting, '\n');
	*p = '\0';

	if (tlmi_priv.pwd_admin->valid && tlmi_priv.pwd_admin->password[0]) {
		auth_str = kasprintf(GFP_KERNEL, "%s,%s,%s;",
				tlmi_priv.pwd_admin->password,
				encoding_options[tlmi_priv.pwd_admin->encoding],
				tlmi_priv.pwd_admin->kbdlang);
		if (!auth_str) {
			ret = -ENOMEM;
			goto out;
		}
	}

	if (auth_str)
		set_str = kasprintf(GFP_KERNEL, "%s,%s,%s", setting->display_name,
				new_setting, auth_str);
	else
		set_str = kasprintf(GFP_KERNEL, "%s,%s;", setting->display_name,
				new_setting);
	if (!set_str) {
		ret = -ENOMEM;
		goto out;
	}

	ret = tlmi_simple_call(LENOVO_SET_BIOS_SETTINGS_GUID, set_str);
	if (ret)
		goto out;

	if (auth_str)
		ret = tlmi_save_bios_settings(auth_str);
	else
		ret = tlmi_save_bios_settings("");

	if (!ret && !tlmi_priv.pending_changes) {
		tlmi_priv.pending_changes = true;
		/* let userland know it may need to check reboot pending again */
		kobject_uevent(&tlmi_priv.class_dev->kobj, KOBJ_CHANGE);
	}
out:
	kfree(auth_str);
	kfree(set_str);
	kfree(new_setting);
	return ret ?: count;
}

static struct kobj_attribute attr_displ_name = __ATTR_RO(display_name);

static struct kobj_attribute attr_possible_values = __ATTR_RO(possible_values);

static struct kobj_attribute attr_current_val = __ATTR_RW_MODE(current_value, 0600);

static struct attribute *tlmi_attrs[] = {
	&attr_displ_name.attr,
	&attr_current_val.attr,
	&attr_possible_values.attr,
	NULL
};

static const struct attribute_group tlmi_attr_group = {
	.attrs = tlmi_attrs,
};

static ssize_t tlmi_attr_show(struct kobject *kobj, struct attribute *attr,
				    char *buf)
{
	struct kobj_attribute *kattr;

	kattr = container_of(attr, struct kobj_attribute, attr);
	if (kattr->show)
		return kattr->show(kobj, kattr, buf);
	return -EIO;
}

static ssize_t tlmi_attr_store(struct kobject *kobj, struct attribute *attr,
				     const char *buf, size_t count)
{
	struct kobj_attribute *kattr;

	kattr = container_of(attr, struct kobj_attribute, attr);
	if (kattr->store)
		return kattr->store(kobj, kattr, buf, count);
	return -EIO;
}

static const struct sysfs_ops tlmi_kobj_sysfs_ops = {
	.show	= tlmi_attr_show,
	.store	= tlmi_attr_store,
};

static void tlmi_attr_setting_release(struct kobject *kobj)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);

	kfree(setting->possible_values);
	kfree(setting);
}

static void tlmi_pwd_setting_release(struct kobject *kobj)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	kfree(setting);
}

static struct kobj_type tlmi_attr_setting_ktype = {
	.release        = &tlmi_attr_setting_release,
	.sysfs_ops	= &tlmi_kobj_sysfs_ops,
};

static struct kobj_type tlmi_pwd_setting_ktype = {
	.release        = &tlmi_pwd_setting_release,
	.sysfs_ops	= &tlmi_kobj_sysfs_ops,
};

static ssize_t pending_reboot_show(struct kobject *kobj, struct kobj_attribute *attr,
				   char *buf)
{
	return sprintf(buf, "%d\n", tlmi_priv.pending_changes);
}

static struct kobj_attribute pending_reboot = __ATTR_RO(pending_reboot);

/* ---- Debug interface--------------------------------------------------------- */
static ssize_t debug_cmd_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	char *set_str = NULL, *new_setting = NULL;
	char *auth_str = NULL;
	char *p;
	int ret;

	if (!tlmi_priv.can_debug_cmd)
		return -EOPNOTSUPP;

	new_setting = kstrdup(buf, GFP_KERNEL);
	if (!new_setting)
		return -ENOMEM;

	/* Strip out CR if one is present */
	p = strchrnul(new_setting, '\n');
	*p = '\0';

	if (tlmi_priv.pwd_admin->valid && tlmi_priv.pwd_admin->password[0]) {
		auth_str = kasprintf(GFP_KERNEL, "%s,%s,%s;",
				tlmi_priv.pwd_admin->password,
				encoding_options[tlmi_priv.pwd_admin->encoding],
				tlmi_priv.pwd_admin->kbdlang);
		if (!auth_str) {
			ret = -ENOMEM;
			goto out;
		}
	}

	if (auth_str)
		set_str = kasprintf(GFP_KERNEL, "%s,%s", new_setting, auth_str);
	else
		set_str = kasprintf(GFP_KERNEL, "%s;", new_setting);
	if (!set_str) {
		ret = -ENOMEM;
		goto out;
	}

	ret = tlmi_simple_call(LENOVO_DEBUG_CMD_GUID, set_str);
	if (ret)
		goto out;

	if (!ret && !tlmi_priv.pending_changes) {
		tlmi_priv.pending_changes = true;
		/* let userland know it may need to check reboot pending again */
		kobject_uevent(&tlmi_priv.class_dev->kobj, KOBJ_CHANGE);
	}
out:
	kfree(auth_str);
	kfree(set_str);
	kfree(new_setting);
	return ret ?: count;
}

static struct kobj_attribute debug_cmd = __ATTR_WO(debug_cmd);

/* ---- Initialisation --------------------------------------------------------- */
static void tlmi_release_attr(void)
{
	int i;

	/* Attribute structures */
	for (i = 0; i < TLMI_SETTINGS_COUNT; i++) {
		if (tlmi_priv.setting[i]) {
			sysfs_remove_group(&tlmi_priv.setting[i]->kobj, &tlmi_attr_group);
			kobject_put(&tlmi_priv.setting[i]->kobj);
		}
	}
	sysfs_remove_file(&tlmi_priv.attribute_kset->kobj, &pending_reboot.attr);
	if (tlmi_priv.can_debug_cmd && debug_support)
		sysfs_remove_file(&tlmi_priv.attribute_kset->kobj, &debug_cmd.attr);
	kset_unregister(tlmi_priv.attribute_kset);

	/* Authentication structures */
	sysfs_remove_group(&tlmi_priv.pwd_admin->kobj, &auth_attr_group);
	kobject_put(&tlmi_priv.pwd_admin->kobj);
	sysfs_remove_group(&tlmi_priv.pwd_power->kobj, &auth_attr_group);
	kobject_put(&tlmi_priv.pwd_power->kobj);
	kset_unregister(tlmi_priv.authentication_kset);
}

static int tlmi_sysfs_init(void)
{
	int i, ret;

	ret = fw_attributes_class_get(&fw_attr_class);
	if (ret)
		return ret;

	tlmi_priv.class_dev = device_create(fw_attr_class, NULL, MKDEV(0, 0),
			NULL, "%s", "thinklmi");
	if (IS_ERR(tlmi_priv.class_dev)) {
		ret = PTR_ERR(tlmi_priv.class_dev);
		goto fail_class_created;
	}

	tlmi_priv.attribute_kset = kset_create_and_add("attributes", NULL,
			&tlmi_priv.class_dev->kobj);
	if (!tlmi_priv.attribute_kset) {
		ret = -ENOMEM;
		goto fail_device_created;
	}

	for (i = 0; i < TLMI_SETTINGS_COUNT; i++) {
		/* Check if index is a valid setting - skip if it isn't */
		if (!tlmi_priv.setting[i])
			continue;

		/* check for duplicate or reserved values */
		if (kset_find_obj(tlmi_priv.attribute_kset, tlmi_priv.setting[i]->display_name) ||
		    !strcmp(tlmi_priv.setting[i]->display_name, "Reserved")) {
			pr_debug("duplicate or reserved attribute name found - %s\n",
				tlmi_priv.setting[i]->display_name);
			kfree(tlmi_priv.setting[i]->possible_values);
			kfree(tlmi_priv.setting[i]);
			tlmi_priv.setting[i] = NULL;
			continue;
		}

		/* Build attribute */
		tlmi_priv.setting[i]->kobj.kset = tlmi_priv.attribute_kset;
		ret = kobject_add(&tlmi_priv.setting[i]->kobj, NULL,
				  "%s", tlmi_priv.setting[i]->display_name);
		if (ret)
			goto fail_create_attr;

		ret = sysfs_create_group(&tlmi_priv.setting[i]->kobj, &tlmi_attr_group);
		if (ret)
			goto fail_create_attr;
	}

	ret = sysfs_create_file(&tlmi_priv.attribute_kset->kobj, &pending_reboot.attr);
	if (ret)
		goto fail_create_attr;

	if (tlmi_priv.can_debug_cmd && debug_support) {
		ret = sysfs_create_file(&tlmi_priv.attribute_kset->kobj, &debug_cmd.attr);
		if (ret)
			goto fail_create_attr;
	}
	/* Create authentication entries */
	tlmi_priv.authentication_kset = kset_create_and_add("authentication", NULL,
								&tlmi_priv.class_dev->kobj);
	if (!tlmi_priv.authentication_kset) {
		ret = -ENOMEM;
		goto fail_create_attr;
	}
	tlmi_priv.pwd_admin->kobj.kset = tlmi_priv.authentication_kset;
	ret = kobject_add(&tlmi_priv.pwd_admin->kobj, NULL, "%s", "Admin");
	if (ret)
		goto fail_create_attr;

	ret = sysfs_create_group(&tlmi_priv.pwd_admin->kobj, &auth_attr_group);
	if (ret)
		goto fail_create_attr;

	tlmi_priv.pwd_power->kobj.kset = tlmi_priv.authentication_kset;
	ret = kobject_add(&tlmi_priv.pwd_power->kobj, NULL, "%s", "System");
	if (ret)
		goto fail_create_attr;

	ret = sysfs_create_group(&tlmi_priv.pwd_power->kobj, &auth_attr_group);
	if (ret)
		goto fail_create_attr;

	return ret;

fail_create_attr:
	tlmi_release_attr();
fail_device_created:
	device_destroy(fw_attr_class, MKDEV(0, 0));
fail_class_created:
	fw_attributes_class_put();
	return ret;
}

/* ---- Base Driver -------------------------------------------------------- */
static int tlmi_analyze(void)
{
	struct tlmi_pwdcfg pwdcfg;
	acpi_status status;
	int i, ret;

	if (wmi_has_guid(LENOVO_SET_BIOS_SETTINGS_GUID) &&
	    wmi_has_guid(LENOVO_SAVE_BIOS_SETTINGS_GUID))
		tlmi_priv.can_set_bios_settings = true;

	if (wmi_has_guid(LENOVO_GET_BIOS_SELECTIONS_GUID))
		tlmi_priv.can_get_bios_selections = true;

	if (wmi_has_guid(LENOVO_SET_BIOS_PASSWORD_GUID))
		tlmi_priv.can_set_bios_password = true;

	if (wmi_has_guid(LENOVO_BIOS_PASSWORD_SETTINGS_GUID))
		tlmi_priv.can_get_password_settings = true;

	if (wmi_has_guid(LENOVO_DEBUG_CMD_GUID))
		tlmi_priv.can_debug_cmd = true;

	/*
	 * Try to find the number of valid settings of this machine
	 * and use it to create sysfs attributes.
	 */
	for (i = 0; i < TLMI_SETTINGS_COUNT; ++i) {
		struct tlmi_attr_setting *setting;
		char *item = NULL;
		char *p;

		tlmi_priv.setting[i] = NULL;
		status = tlmi_setting(i, &item, LENOVO_BIOS_SETTING_GUID);
		if (ACPI_FAILURE(status))
			break;
		if (!item)
			break;
		if (!*item)
			continue;

		/* It is not allowed to have '/' for file name. Convert it into '\'. */
		strreplace(item, '/', '\\');

		/* Remove the value part */
		p = strchrnul(item, ',');
		*p = '\0';

		/* Create a setting entry */
		setting = kzalloc(sizeof(*setting), GFP_KERNEL);
		if (!setting) {
			ret = -ENOMEM;
			goto fail_clear_attr;
		}
		setting->index = i;
		strscpy(setting->display_name, item, TLMI_SETTINGS_MAXLEN);
		/* If BIOS selections supported, load those */
		if (tlmi_priv.can_get_bios_selections) {
			ret = tlmi_get_bios_selections(setting->display_name,
					&setting->possible_values);
			if (ret || !setting->possible_values)
				pr_info("Error retrieving possible values for %d : %s\n",
						i, setting->display_name);
		}
		kobject_init(&setting->kobj, &tlmi_attr_setting_ktype);
		tlmi_priv.setting[i] = setting;
		tlmi_priv.settings_count++;
		kfree(item);
	}

	/* Create password setting structure */
	ret = tlmi_get_pwd_settings(&pwdcfg);
	if (ret)
		goto fail_clear_attr;

	tlmi_priv.pwd_admin = kzalloc(sizeof(struct tlmi_pwd_setting), GFP_KERNEL);
	if (!tlmi_priv.pwd_admin) {
		ret = -ENOMEM;
		goto fail_clear_attr;
	}
	strscpy(tlmi_priv.pwd_admin->kbdlang, "us", TLMI_LANG_MAXLEN);
	tlmi_priv.pwd_admin->encoding = TLMI_ENCODING_ASCII;
	tlmi_priv.pwd_admin->pwd_type = "pap";
	tlmi_priv.pwd_admin->role = "bios-admin";
	tlmi_priv.pwd_admin->minlen = pwdcfg.min_length;
	if (WARN_ON(pwdcfg.max_length >= TLMI_PWD_BUFSIZE))
		pwdcfg.max_length = TLMI_PWD_BUFSIZE - 1;
	tlmi_priv.pwd_admin->maxlen = pwdcfg.max_length;
	if (pwdcfg.password_state & TLMI_PAP_PWD)
		tlmi_priv.pwd_admin->valid = true;

	kobject_init(&tlmi_priv.pwd_admin->kobj, &tlmi_pwd_setting_ktype);

	tlmi_priv.pwd_power = kzalloc(sizeof(struct tlmi_pwd_setting), GFP_KERNEL);
	if (!tlmi_priv.pwd_power) {
		ret = -ENOMEM;
		goto fail_free_pwd_admin;
	}
	strscpy(tlmi_priv.pwd_power->kbdlang, "us", TLMI_LANG_MAXLEN);
	tlmi_priv.pwd_power->encoding = TLMI_ENCODING_ASCII;
	tlmi_priv.pwd_power->pwd_type = "pop";
	tlmi_priv.pwd_power->role = "power-on";
	tlmi_priv.pwd_power->minlen = pwdcfg.min_length;
	tlmi_priv.pwd_power->maxlen = pwdcfg.max_length;

	if (pwdcfg.password_state & TLMI_POP_PWD)
		tlmi_priv.pwd_power->valid = true;

	kobject_init(&tlmi_priv.pwd_power->kobj, &tlmi_pwd_setting_ktype);

	return 0;

fail_free_pwd_admin:
	kfree(tlmi_priv.pwd_admin);
fail_clear_attr:
	for (i = 0; i < TLMI_SETTINGS_COUNT; ++i) {
		if (tlmi_priv.setting[i]) {
			kfree(tlmi_priv.setting[i]->possible_values);
			kfree(tlmi_priv.setting[i]);
		}
	}
	return ret;
}

static void tlmi_remove(struct wmi_device *wdev)
{
	tlmi_release_attr();
	device_destroy(fw_attr_class, MKDEV(0, 0));
	fw_attributes_class_put();
}

static int tlmi_probe(struct wmi_device *wdev, const void *context)
{
	tlmi_analyze();
	return tlmi_sysfs_init();
}

static const struct wmi_device_id tlmi_id_table[] = {
	{ .guid_string = LENOVO_BIOS_SETTING_GUID },
	{ }
};
MODULE_DEVICE_TABLE(wmi, tlmi_id_table);

static struct wmi_driver tlmi_driver = {
	.driver = {
		.name = "think-lmi",
	},
	.id_table = tlmi_id_table,
	.probe = tlmi_probe,
	.remove = tlmi_remove,
};

MODULE_AUTHOR("Sugumaran L <slacshiminar@lenovo.com>");
MODULE_AUTHOR("Mark Pearson <markpearson@lenovo.com>");
MODULE_AUTHOR("Corentin Chary <corentin.chary@gmail.com>");
MODULE_DESCRIPTION("ThinkLMI Driver");
MODULE_LICENSE("GPL");

module_wmi_driver(tlmi_driver);
