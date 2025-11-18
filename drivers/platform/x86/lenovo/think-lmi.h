/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _THINK_LMI_H_
#define _THINK_LMI_H_

#include <linux/types.h>
#include <linux/wmi.h>

#define TLMI_SETTINGS_COUNT  256
#define TLMI_SETTINGS_MAXLEN 512
#define TLMI_PWD_BUFSIZE     129
#define TLMI_LANG_MAXLEN       4
#define TLMI_INDEX_MAX        32

/* Possible error values */
struct tlmi_err_codes {
	const char *err_str;
	int err_code;
};

enum encoding_option {
	TLMI_ENCODING_ASCII,
	TLMI_ENCODING_SCANCODE,
};

enum level_option {
	TLMI_LEVEL_USER,
	TLMI_LEVEL_MASTER,
};

/*
 * There are a limit on the number of WMI operations you can do if you use
 * the default implementation of saving on every set. This is due to a
 * limitation in EFI variable space used.
 * Have a 'bulk save' mode where you can manually trigger the save, and can
 * therefore set unlimited variables - for users that need it.
 */
enum save_mode {
	TLMI_SAVE_SINGLE,
	TLMI_SAVE_BULK,
	TLMI_SAVE_SAVE,
};

/* GUIDs can differ between platforms */
struct tlmi_cert_guids {
	const char *thumbprint;
	const char *set_bios_setting;
	const char *save_bios_setting;
	const char *cert_to_password;
	const char *clear_bios_cert;
	const char *update_bios_cert;
	const char *set_bios_cert;
};

/* password configuration details */
#define TLMI_PWDCFG_MODE_LEGACY    0
#define TLMI_PWDCFG_MODE_PASSWORD  1
#define TLMI_PWDCFG_MODE_MULTICERT 3

struct tlmi_pwdcfg_core {
	uint32_t password_mode;
	uint32_t password_state;
	uint32_t min_length;
	uint32_t max_length;
	uint32_t supported_encodings;
	uint32_t supported_keyboard;
};

struct tlmi_pwdcfg_ext {
	uint32_t hdd_user_password;
	uint32_t hdd_master_password;
	uint32_t nvme_user_password;
	uint32_t nvme_master_password;
};

struct tlmi_pwdcfg {
	struct tlmi_pwdcfg_core core;
	struct tlmi_pwdcfg_ext ext;
};

/* password setting details */
struct tlmi_pwd_setting {
	struct kobject kobj;
	bool pwd_enabled;
	char password[TLMI_PWD_BUFSIZE];
	const char *pwd_type;
	const char *role;
	int minlen;
	int maxlen;
	enum encoding_option encoding;
	char kbdlang[TLMI_LANG_MAXLEN];
	int index; /*Used for HDD and NVME auth */
	enum level_option level;
	bool cert_installed;
	char *signature;
	char *save_signature;
};

/* Attribute setting details */
struct tlmi_attr_setting {
	struct kobject kobj;
	struct wmi_device *wdev;
	int index;
	char name[TLMI_SETTINGS_MAXLEN];
	char display_name[TLMI_SETTINGS_MAXLEN];
	char *possible_values;
};

struct think_lmi {
	struct wmi_device *wmi_device;

	bool can_set_bios_settings;
	bool can_get_bios_selections;
	bool can_set_bios_password;
	bool can_get_password_settings;
	bool pending_changes;
	bool can_debug_cmd;
	bool opcode_support;
	bool certificate_support;
	enum save_mode save_mode;
	bool save_required;
	bool reboot_required;
	bool thinkcenter_mode;

	struct tlmi_attr_setting *setting[TLMI_SETTINGS_COUNT];
	struct device *class_dev;
	struct kset *attribute_kset;
	struct kset *authentication_kset;

	struct tlmi_pwdcfg pwdcfg;
	struct tlmi_pwd_setting *pwd_admin;
	struct tlmi_pwd_setting *pwd_power;
	struct tlmi_pwd_setting *pwd_system;
	struct tlmi_pwd_setting *pwd_hdd;
	struct tlmi_pwd_setting *pwd_nvme;

	const struct tlmi_cert_guids *cert_guid;
};

#endif /* !_THINK_LMI_H_ */
