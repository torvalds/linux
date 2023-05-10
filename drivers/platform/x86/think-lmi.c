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
#include <linux/dmi.h>
#include <linux/wmi.h>
#include "firmware_attributes_class.h"
#include "think-lmi.h"

static bool debug_support;
module_param(debug_support, bool, 0444);
MODULE_PARM_DESC(debug_support, "Enable debug command support");

/*
 * Name: BiosSetting
 * Description: Get item name and settings for current LMI instance.
 * Type: Query
 * Returns: "Item,Value"
 * Example: "WakeOnLAN,Enable"
 */
#define LENOVO_BIOS_SETTING_GUID "51F5230E-9677-46CD-A1CF-C0B23EE34DB7"

/*
 * Name: SetBiosSetting
 * Description: Change the BIOS setting to the desired value using the SetBiosSetting
 *  class. To save the settings, use the SaveBiosSetting class.
 *  BIOS settings and values are case sensitive.
 *  After making changes to the BIOS settings, you must reboot the computer
 *  before the changes will take effect.
 * Type: Method
 * Arguments: "Item,Value,Password,Encoding,KbdLang;"
 * Example: "WakeOnLAN,Disable,pa55w0rd,ascii,us;"
 */
#define LENOVO_SET_BIOS_SETTINGS_GUID "98479A64-33F5-4E33-A707-8E251EBBC3A1"

/*
 * Name: SaveBiosSettings
 * Description: Save any pending changes in settings.
 * Type: Method
 * Arguments: "Password,Encoding,KbdLang;"
 * Example: "pa55w0rd,ascii,us;"
 */
#define LENOVO_SAVE_BIOS_SETTINGS_GUID "6A4B54EF-A5ED-4D33-9455-B0D9B48DF4B3"

/*
 * Name: BiosPasswordSettings
 * Description: Return BIOS Password settings
 * Type: Query
 * Returns: PasswordMode, PasswordState, MinLength, MaxLength,
 *  SupportedEncoding, SupportedKeyboard
 */
#define LENOVO_BIOS_PASSWORD_SETTINGS_GUID "8ADB159E-1E32-455C-BC93-308A7ED98246"

/*
 * Name: SetBiosPassword
 * Description: Change a specific password.
 *  - BIOS settings cannot be changed at the same boot as power-on
 *    passwords (POP) and hard disk passwords (HDP). If you want to change
 *    BIOS settings and POP or HDP, you must reboot the system after changing
 *    one of them.
 *  - A password cannot be set using this method when one does not already
 *    exist. Passwords can only be updated or cleared.
 * Type: Method
 * Arguments: "PasswordType,CurrentPassword,NewPassword,Encoding,KbdLang;"
 * Example: "pop,pa55w0rd,newpa55w0rd,ascii,us;”
 */
#define LENOVO_SET_BIOS_PASSWORD_GUID "2651D9FD-911C-4B69-B94E-D0DED5963BD7"

/*
 * Name: GetBiosSelections
 * Description: Return a list of valid settings for a given item.
 * Type: Method
 * Arguments: "Item"
 * Returns: "Value1,Value2,Value3,..."
 * Example:
 *  -> "FlashOverLAN"
 *  <- "Enabled,Disabled"
 */
#define LENOVO_GET_BIOS_SELECTIONS_GUID	"7364651A-132F-4FE7-ADAA-40C6C7EE2E3B"

/*
 * Name: DebugCmd
 * Description: Debug entry method for entering debug commands to the BIOS
 */
#define LENOVO_DEBUG_CMD_GUID "7FF47003-3B6C-4E5E-A227-E979824A85D1"

/*
 * Name: OpcodeIF
 * Description: Opcode interface which provides the ability to set multiple
 *  parameters and then trigger an action with a final command.
 *  This is particularly useful for simplifying setting passwords.
 *  With this support comes the ability to set System, HDD and NVMe
 *  passwords.
 *  This is currently available on ThinkCenter and ThinkStations platforms
 */
#define LENOVO_OPCODE_IF_GUID "DFDDEF2C-57D4-48ce-B196-0FB787D90836"

/*
 * Name: SetBiosCert
 * Description: Install BIOS certificate.
 * Type: Method
 * Arguments: "Certificate,Password"
 * You must reboot the computer before the changes will take effect.
 */
#define LENOVO_SET_BIOS_CERT_GUID    "26861C9F-47E9-44C4-BD8B-DFE7FA2610FE"

/*
 * Name: UpdateBiosCert
 * Description: Update BIOS certificate.
 * Type: Method
 * Format: "Certificate,Signature"
 * You must reboot the computer before the changes will take effect.
 */
#define LENOVO_UPDATE_BIOS_CERT_GUID "9AA3180A-9750-41F7-B9F7-D5D3B1BAC3CE"

/*
 * Name: ClearBiosCert
 * Description: Uninstall BIOS certificate.
 * Type: Method
 * Format: "Serial,Signature"
 * You must reboot the computer before the changes will take effect.
 */
#define LENOVO_CLEAR_BIOS_CERT_GUID  "B2BC39A7-78DD-4D71-B059-A510DEC44890"
/*
 * Name: CertToPassword
 * Description: Switch from certificate to password authentication.
 * Type: Method
 * Format: "Password,Signature"
 * You must reboot the computer before the changes will take effect.
 */
#define LENOVO_CERT_TO_PASSWORD_GUID "0DE8590D-5510-4044-9621-77C227F5A70D"

/*
 * Name: SetBiosSettingCert
 * Description: Set attribute using certificate authentication.
 * Type: Method
 * Format: "Item,Value,Signature"
 */
#define LENOVO_SET_BIOS_SETTING_CERT_GUID  "34A008CC-D205-4B62-9E67-31DFA8B90003"

/*
 * Name: SaveBiosSettingCert
 * Description: Save any pending changes in settings.
 * Type: Method
 * Format: "Signature"
 */
#define LENOVO_SAVE_BIOS_SETTING_CERT_GUID "C050FB9D-DF5F-4606-B066-9EFC401B2551"

/*
 * Name: CertThumbprint
 * Description: Display Certificate thumbprints
 * Type: Query
 * Returns: MD5, SHA1 & SHA256 thumbprints
 */
#define LENOVO_CERT_THUMBPRINT_GUID "C59119ED-1C0D-4806-A8E9-59AA318176C4"

#define TLMI_POP_PWD (1 << 0)
#define TLMI_PAP_PWD (1 << 1)
#define TLMI_HDD_PWD (1 << 2)
#define TLMI_SYS_PWD (1 << 3)
#define TLMI_CERT    (1 << 7)

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
static const char * const level_options[] = {
	[TLMI_LEVEL_USER] = "user",
	[TLMI_LEVEL_MASTER] = "master",
};
static struct think_lmi tlmi_priv;
static struct class *fw_attr_class;

/* ------ Utility functions ------------*/
/* Strip out CR if one is present */
static void strip_cr(char *str)
{
	char *p = strchrnul(str, '\n');
	*p = '\0';
}

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
	int copy_size;

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
	 * Settings must have at minimum the core fields available
	 */
	if (obj->buffer.length < sizeof(struct tlmi_pwdcfg_core)) {
		pr_warn("Unknown pwdcfg buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return -EIO;
	}

	copy_size = obj->buffer.length < sizeof(struct tlmi_pwdcfg) ?
		obj->buffer.length : sizeof(struct tlmi_pwdcfg);
	memcpy(pwdcfg, obj->buffer.pointer, copy_size);
	kfree(obj);

	if (WARN_ON(pwdcfg->core.max_length >= TLMI_PWD_BUFSIZE))
		pwdcfg->core.max_length = TLMI_PWD_BUFSIZE - 1;
	return 0;
}

static int tlmi_save_bios_settings(const char *password)
{
	return tlmi_simple_call(LENOVO_SAVE_BIOS_SETTINGS_GUID,
				password);
}

static int tlmi_opcode_setting(char *setting, const char *value)
{
	char *opcode_str;
	int ret;

	opcode_str = kasprintf(GFP_KERNEL, "%s:%s;", setting, value);
	if (!opcode_str)
		return -ENOMEM;

	ret = tlmi_simple_call(LENOVO_OPCODE_IF_GUID, opcode_str);
	kfree(opcode_str);
	return ret;
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

	pwdlen = strlen(buf);
	/* pwdlen == 0 is allowed to clear the password */
	if (pwdlen && ((pwdlen < setting->minlen) || (pwdlen > setting->maxlen)))
		return -EINVAL;

	strscpy(setting->password, buf, setting->maxlen);
	/* Strip out CR if one is present, setting password won't work if it is present */
	strip_cr(setting->password);
	return count;
}

static struct kobj_attribute auth_current_password = __ATTR_WO(current_password);

static ssize_t new_password_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	char *auth_str, *new_pwd;
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
	strip_cr(new_pwd);

	pwdlen = strlen(new_pwd);
	/* pwdlen == 0 is allowed to clear the password */
	if (pwdlen && ((pwdlen < setting->minlen) || (pwdlen > setting->maxlen))) {
		ret = -EINVAL;
		goto out;
	}

	/* If opcode support is present use that interface */
	if (tlmi_priv.opcode_support) {
		char pwd_type[8];

		/* Special handling required for HDD and NVMe passwords */
		if (setting == tlmi_priv.pwd_hdd) {
			if (setting->level == TLMI_LEVEL_USER)
				sprintf(pwd_type, "uhdp%d", setting->index);
			else
				sprintf(pwd_type, "mhdp%d", setting->index);
		} else if (setting == tlmi_priv.pwd_nvme) {
			if (setting->level == TLMI_LEVEL_USER)
				sprintf(pwd_type, "unvp%d", setting->index);
			else
				sprintf(pwd_type, "mnvp%d", setting->index);
		} else {
			sprintf(pwd_type, "%s", setting->pwd_type);
		}

		ret = tlmi_opcode_setting("WmiOpcodePasswordType", pwd_type);
		if (ret)
			goto out;

		if (tlmi_priv.pwd_admin->valid) {
			ret = tlmi_opcode_setting("WmiOpcodePasswordAdmin",
					tlmi_priv.pwd_admin->password);
			if (ret)
				goto out;
		}
		ret = tlmi_opcode_setting("WmiOpcodePasswordCurrent01", setting->password);
		if (ret)
			goto out;
		ret = tlmi_opcode_setting("WmiOpcodePasswordNew01", new_pwd);
		if (ret)
			goto out;
		ret = tlmi_simple_call(LENOVO_OPCODE_IF_GUID, "WmiOpcodePasswordSetUpdate;");
	} else {
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
	}
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

static ssize_t index_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	return sysfs_emit(buf, "%d\n", setting->index);
}

static ssize_t index_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	int err, val;

	err = kstrtoint(buf, 10, &val);
	if (err < 0)
		return err;

	if (val < 0 || val > TLMI_INDEX_MAX)
		return -EINVAL;

	setting->index = val;
	return count;
}

static struct kobj_attribute auth_index = __ATTR_RW(index);

static ssize_t level_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	return sysfs_emit(buf, "%s\n", level_options[setting->level]);
}

static ssize_t level_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	int i;

	/* Scan for a matching profile */
	i = sysfs_match_string(level_options, buf);
	if (i < 0)
		return -EINVAL;

	setting->level = i;
	return count;
}

static struct kobj_attribute auth_level = __ATTR_RW(level);

static ssize_t cert_thumbprint(char *buf, const char *arg, int count)
{
	const struct acpi_buffer input = { strlen(arg), (char *)arg };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	const union acpi_object *obj;
	acpi_status status;

	status = wmi_evaluate_method(LENOVO_CERT_THUMBPRINT_GUID, 0, 0, &input, &output);
	if (ACPI_FAILURE(status)) {
		kfree(output.pointer);
		return -EIO;
	}
	obj = output.pointer;
	if (!obj)
		return -ENOMEM;
	if (obj->type != ACPI_TYPE_STRING || !obj->string.pointer) {
		kfree(output.pointer);
		return -EIO;
	}
	count += sysfs_emit_at(buf, count, "%s : %s\n", arg, (char *)obj->string.pointer);
	kfree(output.pointer);

	return count;
}

static ssize_t certificate_thumbprint_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	int count = 0;

	if (!tlmi_priv.certificate_support || !setting->cert_installed)
		return -EOPNOTSUPP;

	count += cert_thumbprint(buf, "Md5", count);
	count += cert_thumbprint(buf, "Sha1", count);
	count += cert_thumbprint(buf, "Sha256", count);
	return count;
}

static struct kobj_attribute auth_cert_thumb = __ATTR_RO(certificate_thumbprint);

static ssize_t cert_to_password_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	char *auth_str, *passwd;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!tlmi_priv.certificate_support)
		return -EOPNOTSUPP;

	if (!setting->cert_installed)
		return -EINVAL;

	if (!setting->signature || !setting->signature[0])
		return -EACCES;

	passwd = kstrdup(buf, GFP_KERNEL);
	if (!passwd)
		return -ENOMEM;

	/* Strip out CR if one is present */
	strip_cr(passwd);

	/* Format: 'Password,Signature' */
	auth_str = kasprintf(GFP_KERNEL, "%s,%s", passwd, setting->signature);
	if (!auth_str) {
		kfree(passwd);
		return -ENOMEM;
	}
	ret = tlmi_simple_call(LENOVO_CERT_TO_PASSWORD_GUID, auth_str);
	kfree(auth_str);
	kfree(passwd);

	return ret ?: count;
}

static struct kobj_attribute auth_cert_to_password = __ATTR_WO(cert_to_password);

static ssize_t certificate_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	char *auth_str, *new_cert;
	char *guid;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!tlmi_priv.certificate_support)
		return -EOPNOTSUPP;

	/* If empty then clear installed certificate */
	if ((buf[0] == '\0') || (buf[0] == '\n')) { /* Clear installed certificate */
		/* Check that signature is set */
		if (!setting->signature || !setting->signature[0])
			return -EACCES;

		/* Format: 'serial#, signature' */
		auth_str = kasprintf(GFP_KERNEL, "%s,%s",
				dmi_get_system_info(DMI_PRODUCT_SERIAL),
				setting->signature);
		if (!auth_str)
			return -ENOMEM;

		ret = tlmi_simple_call(LENOVO_CLEAR_BIOS_CERT_GUID, auth_str);
		kfree(auth_str);

		return ret ?: count;
	}

	new_cert = kstrdup(buf, GFP_KERNEL);
	if (!new_cert)
		return -ENOMEM;
	/* Strip out CR if one is present */
	strip_cr(new_cert);

	if (setting->cert_installed) {
		/* Certificate is installed so this is an update */
		if (!setting->signature || !setting->signature[0]) {
			kfree(new_cert);
			return -EACCES;
		}
		guid = LENOVO_UPDATE_BIOS_CERT_GUID;
		/* Format: 'Certificate,Signature' */
		auth_str = kasprintf(GFP_KERNEL, "%s,%s",
				new_cert, setting->signature);
	} else {
		/* This is a fresh install */
		if (!setting->valid || !setting->password[0]) {
			kfree(new_cert);
			return -EACCES;
		}
		guid = LENOVO_SET_BIOS_CERT_GUID;
		/* Format: 'Certificate,Admin-password' */
		auth_str = kasprintf(GFP_KERNEL, "%s,%s",
				new_cert, setting->password);
	}
	kfree(new_cert);
	if (!auth_str)
		return -ENOMEM;

	ret = tlmi_simple_call(guid, auth_str);
	kfree(auth_str);

	return ret ?: count;
}

static struct kobj_attribute auth_certificate = __ATTR_WO(certificate);

static ssize_t signature_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	char *new_signature;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!tlmi_priv.certificate_support)
		return -EOPNOTSUPP;

	new_signature = kstrdup(buf, GFP_KERNEL);
	if (!new_signature)
		return -ENOMEM;

	/* Strip out CR if one is present */
	strip_cr(new_signature);

	/* Free any previous signature */
	kfree(setting->signature);
	setting->signature = new_signature;

	return count;
}

static struct kobj_attribute auth_signature = __ATTR_WO(signature);

static ssize_t save_signature_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);
	char *new_signature;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!tlmi_priv.certificate_support)
		return -EOPNOTSUPP;

	new_signature = kstrdup(buf, GFP_KERNEL);
	if (!new_signature)
		return -ENOMEM;

	/* Strip out CR if one is present */
	strip_cr(new_signature);

	/* Free any previous signature */
	kfree(setting->save_signature);
	setting->save_signature = new_signature;

	return count;
}

static struct kobj_attribute auth_save_signature = __ATTR_WO(save_signature);

static umode_t auth_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int n)
{
	struct tlmi_pwd_setting *setting = to_tlmi_pwd_setting(kobj);

	/* We only want to display level and index settings on HDD/NVMe */
	if ((attr == (struct attribute *)&auth_index) ||
			(attr == (struct attribute *)&auth_level)) {
		if ((setting == tlmi_priv.pwd_hdd) || (setting == tlmi_priv.pwd_nvme))
			return attr->mode;
		return 0;
	}

	/* We only display certificates on Admin account, if supported */
	if ((attr == (struct attribute *)&auth_certificate) ||
			(attr == (struct attribute *)&auth_signature) ||
			(attr == (struct attribute *)&auth_save_signature) ||
			(attr == (struct attribute *)&auth_cert_thumb) ||
			(attr == (struct attribute *)&auth_cert_to_password)) {
		if ((setting == tlmi_priv.pwd_admin) && tlmi_priv.certificate_support)
			return attr->mode;
		return 0;
	}

	return attr->mode;
}

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
	&auth_index.attr,
	&auth_level.attr,
	&auth_certificate.attr,
	&auth_signature.attr,
	&auth_save_signature.attr,
	&auth_cert_thumb.attr,
	&auth_cert_to_password.attr,
	NULL
};

static const struct attribute_group auth_attr_group = {
	.is_visible = auth_attr_is_visible,
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
	char *item, *value, *p;
	int ret;

	ret = tlmi_setting(setting->index, &item, LENOVO_BIOS_SETTING_GUID);
	if (ret)
		return ret;

	/* validate and split from `item,value` -> `value` */
	value = strpbrk(item, ",");
	if (!value || value == item || !strlen(value + 1))
		ret = -EINVAL;
	else {
		/* On Workstations remove the Options part after the value */
		p = strchrnul(value, ';');
		*p = '\0';
		ret = sysfs_emit(buf, "%s\n", value + 1);
	}
	kfree(item);

	return ret;
}

static ssize_t possible_values_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);

	return sysfs_emit(buf, "%s\n", setting->possible_values);
}

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);

	if (setting->possible_values) {
		/* Figure out what setting type is as BIOS does not return this */
		if (strchr(setting->possible_values, ';'))
			return sysfs_emit(buf, "enumeration\n");
	}
	/* Anything else is going to be a string */
	return sysfs_emit(buf, "string\n");
}

static ssize_t current_value_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);
	char *set_str = NULL, *new_setting = NULL;
	char *auth_str = NULL;
	int ret;

	if (!tlmi_priv.can_set_bios_settings)
		return -EOPNOTSUPP;

	new_setting = kstrdup(buf, GFP_KERNEL);
	if (!new_setting)
		return -ENOMEM;

	/* Strip out CR if one is present */
	strip_cr(new_setting);

	/* Check if certificate authentication is enabled and active */
	if (tlmi_priv.certificate_support && tlmi_priv.pwd_admin->cert_installed) {
		if (!tlmi_priv.pwd_admin->signature || !tlmi_priv.pwd_admin->save_signature) {
			ret = -EINVAL;
			goto out;
		}
		set_str = kasprintf(GFP_KERNEL, "%s,%s,%s", setting->display_name,
					new_setting, tlmi_priv.pwd_admin->signature);
		if (!set_str) {
			ret = -ENOMEM;
			goto out;
		}

		ret = tlmi_simple_call(LENOVO_SET_BIOS_SETTING_CERT_GUID, set_str);
		if (ret)
			goto out;
		ret = tlmi_simple_call(LENOVO_SAVE_BIOS_SETTING_CERT_GUID,
				tlmi_priv.pwd_admin->save_signature);
		if (ret)
			goto out;
	} else { /* Non certiifcate based authentication */
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
	}
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

static struct kobj_attribute attr_type = __ATTR_RO(type);

static umode_t attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int n)
{
	struct tlmi_attr_setting *setting = to_tlmi_attr_setting(kobj);

	/* We don't want to display possible_values attributes if not available */
	if ((attr == &attr_possible_values.attr) && (!setting->possible_values))
		return 0;

	return attr->mode;
}

static struct attribute *tlmi_attrs[] = {
	&attr_displ_name.attr,
	&attr_current_val.attr,
	&attr_possible_values.attr,
	&attr_type.attr,
	NULL
};

static const struct attribute_group tlmi_attr_group = {
	.is_visible = attr_is_visible,
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
	int ret;

	if (!tlmi_priv.can_debug_cmd)
		return -EOPNOTSUPP;

	new_setting = kstrdup(buf, GFP_KERNEL);
	if (!new_setting)
		return -ENOMEM;

	/* Strip out CR if one is present */
	strip_cr(new_setting);

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

	/* Free up any saved signatures */
	kfree(tlmi_priv.pwd_admin->signature);
	kfree(tlmi_priv.pwd_admin->save_signature);

	/* Authentication structures */
	sysfs_remove_group(&tlmi_priv.pwd_admin->kobj, &auth_attr_group);
	kobject_put(&tlmi_priv.pwd_admin->kobj);
	sysfs_remove_group(&tlmi_priv.pwd_power->kobj, &auth_attr_group);
	kobject_put(&tlmi_priv.pwd_power->kobj);

	if (tlmi_priv.opcode_support) {
		sysfs_remove_group(&tlmi_priv.pwd_system->kobj, &auth_attr_group);
		kobject_put(&tlmi_priv.pwd_system->kobj);
		sysfs_remove_group(&tlmi_priv.pwd_hdd->kobj, &auth_attr_group);
		kobject_put(&tlmi_priv.pwd_hdd->kobj);
		sysfs_remove_group(&tlmi_priv.pwd_nvme->kobj, &auth_attr_group);
		kobject_put(&tlmi_priv.pwd_nvme->kobj);
	}

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
	ret = kobject_add(&tlmi_priv.pwd_power->kobj, NULL, "%s", "Power-on");
	if (ret)
		goto fail_create_attr;

	ret = sysfs_create_group(&tlmi_priv.pwd_power->kobj, &auth_attr_group);
	if (ret)
		goto fail_create_attr;

	if (tlmi_priv.opcode_support) {
		tlmi_priv.pwd_system->kobj.kset = tlmi_priv.authentication_kset;
		ret = kobject_add(&tlmi_priv.pwd_system->kobj, NULL, "%s", "System");
		if (ret)
			goto fail_create_attr;

		ret = sysfs_create_group(&tlmi_priv.pwd_system->kobj, &auth_attr_group);
		if (ret)
			goto fail_create_attr;

		tlmi_priv.pwd_hdd->kobj.kset = tlmi_priv.authentication_kset;
		ret = kobject_add(&tlmi_priv.pwd_hdd->kobj, NULL, "%s", "HDD");
		if (ret)
			goto fail_create_attr;

		ret = sysfs_create_group(&tlmi_priv.pwd_hdd->kobj, &auth_attr_group);
		if (ret)
			goto fail_create_attr;

		tlmi_priv.pwd_nvme->kobj.kset = tlmi_priv.authentication_kset;
		ret = kobject_add(&tlmi_priv.pwd_nvme->kobj, NULL, "%s", "NVMe");
		if (ret)
			goto fail_create_attr;

		ret = sysfs_create_group(&tlmi_priv.pwd_nvme->kobj, &auth_attr_group);
		if (ret)
			goto fail_create_attr;
	}

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
static struct tlmi_pwd_setting *tlmi_create_auth(const char *pwd_type,
			    const char *pwd_role)
{
	struct tlmi_pwd_setting *new_pwd;

	new_pwd = kzalloc(sizeof(struct tlmi_pwd_setting), GFP_KERNEL);
	if (!new_pwd)
		return NULL;

	strscpy(new_pwd->kbdlang, "us", TLMI_LANG_MAXLEN);
	new_pwd->encoding = TLMI_ENCODING_ASCII;
	new_pwd->pwd_type = pwd_type;
	new_pwd->role = pwd_role;
	new_pwd->minlen = tlmi_priv.pwdcfg.core.min_length;
	new_pwd->maxlen = tlmi_priv.pwdcfg.core.max_length;
	new_pwd->index = 0;

	kobject_init(&new_pwd->kobj, &tlmi_pwd_setting_ktype);

	return new_pwd;
}

static int tlmi_analyze(void)
{
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

	if (wmi_has_guid(LENOVO_OPCODE_IF_GUID))
		tlmi_priv.opcode_support = true;

	if (wmi_has_guid(LENOVO_SET_BIOS_CERT_GUID) &&
		wmi_has_guid(LENOVO_SET_BIOS_SETTING_CERT_GUID) &&
		wmi_has_guid(LENOVO_SAVE_BIOS_SETTING_CERT_GUID))
		tlmi_priv.certificate_support = true;

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
		if (!*item) {
			kfree(item);
			continue;
		}

		/* It is not allowed to have '/' for file name. Convert it into '\'. */
		strreplace(item, '/', '\\');

		/* Remove the value part */
		p = strchrnul(item, ',');
		*p = '\0';

		/* Create a setting entry */
		setting = kzalloc(sizeof(*setting), GFP_KERNEL);
		if (!setting) {
			ret = -ENOMEM;
			kfree(item);
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
		} else {
			/*
			 * Older Thinkstations don't support the bios_selections API.
			 * Instead they store this as a [Optional:Option1,Option2] section of the
			 * name string.
			 * Try and pull that out if it's available.
			 */
			char *optitem, *optstart, *optend;

			if (!tlmi_setting(setting->index, &optitem, LENOVO_BIOS_SETTING_GUID)) {
				optstart = strstr(optitem, "[Optional:");
				if (optstart) {
					optstart += strlen("[Optional:");
					optend = strstr(optstart, "]");
					if (optend)
						setting->possible_values =
							kstrndup(optstart, optend - optstart,
									GFP_KERNEL);
				}
				kfree(optitem);
			}
		}
		/*
		 * firmware-attributes requires that possible_values are separated by ';' but
		 * Lenovo FW uses ','. Replace appropriately.
		 */
		if (setting->possible_values)
			strreplace(setting->possible_values, ',', ';');

		kobject_init(&setting->kobj, &tlmi_attr_setting_ktype);
		tlmi_priv.setting[i] = setting;
		kfree(item);
	}

	/* Create password setting structure */
	ret = tlmi_get_pwd_settings(&tlmi_priv.pwdcfg);
	if (ret)
		goto fail_clear_attr;

	/* All failures below boil down to kmalloc failures */
	ret = -ENOMEM;

	tlmi_priv.pwd_admin = tlmi_create_auth("pap", "bios-admin");
	if (!tlmi_priv.pwd_admin)
		goto fail_clear_attr;

	if (tlmi_priv.pwdcfg.core.password_state & TLMI_PAP_PWD)
		tlmi_priv.pwd_admin->valid = true;

	tlmi_priv.pwd_power = tlmi_create_auth("pop", "power-on");
	if (!tlmi_priv.pwd_power)
		goto fail_clear_attr;

	if (tlmi_priv.pwdcfg.core.password_state & TLMI_POP_PWD)
		tlmi_priv.pwd_power->valid = true;

	if (tlmi_priv.opcode_support) {
		tlmi_priv.pwd_system = tlmi_create_auth("sys", "system");
		if (!tlmi_priv.pwd_system)
			goto fail_clear_attr;

		if (tlmi_priv.pwdcfg.core.password_state & TLMI_SYS_PWD)
			tlmi_priv.pwd_system->valid = true;

		tlmi_priv.pwd_hdd = tlmi_create_auth("hdd", "hdd");
		if (!tlmi_priv.pwd_hdd)
			goto fail_clear_attr;

		tlmi_priv.pwd_nvme = tlmi_create_auth("nvm", "nvme");
		if (!tlmi_priv.pwd_nvme)
			goto fail_clear_attr;

		if (tlmi_priv.pwdcfg.core.password_state & TLMI_HDD_PWD) {
			/* Check if PWD is configured and set index to first drive found */
			if (tlmi_priv.pwdcfg.ext.hdd_user_password ||
					tlmi_priv.pwdcfg.ext.hdd_master_password) {
				tlmi_priv.pwd_hdd->valid = true;
				if (tlmi_priv.pwdcfg.ext.hdd_master_password)
					tlmi_priv.pwd_hdd->index =
						ffs(tlmi_priv.pwdcfg.ext.hdd_master_password) - 1;
				else
					tlmi_priv.pwd_hdd->index =
						ffs(tlmi_priv.pwdcfg.ext.hdd_user_password) - 1;
			}
			if (tlmi_priv.pwdcfg.ext.nvme_user_password ||
					tlmi_priv.pwdcfg.ext.nvme_master_password) {
				tlmi_priv.pwd_nvme->valid = true;
				if (tlmi_priv.pwdcfg.ext.nvme_master_password)
					tlmi_priv.pwd_nvme->index =
						ffs(tlmi_priv.pwdcfg.ext.nvme_master_password) - 1;
				else
					tlmi_priv.pwd_nvme->index =
						ffs(tlmi_priv.pwdcfg.ext.nvme_user_password) - 1;
			}
		}
	}

	if (tlmi_priv.certificate_support &&
		(tlmi_priv.pwdcfg.core.password_state & TLMI_CERT))
		tlmi_priv.pwd_admin->cert_installed = true;

	return 0;

fail_clear_attr:
	for (i = 0; i < TLMI_SETTINGS_COUNT; ++i) {
		if (tlmi_priv.setting[i]) {
			kfree(tlmi_priv.setting[i]->possible_values);
			kfree(tlmi_priv.setting[i]);
		}
	}
	kfree(tlmi_priv.pwd_admin);
	kfree(tlmi_priv.pwd_power);
	kfree(tlmi_priv.pwd_system);
	kfree(tlmi_priv.pwd_hdd);
	kfree(tlmi_priv.pwd_nvme);
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
	int ret;

	ret = tlmi_analyze();
	if (ret)
		return ret;

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
