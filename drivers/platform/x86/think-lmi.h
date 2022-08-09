/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _THINK_LMI_H_
#define _THINK_LMI_H_

#include <linux/types.h>

#define TLMI_SETTINGS_COUNT  256
#define TLMI_SETTINGS_MAXLEN 512
#define TLMI_PWD_BUFSIZE     129
#define TLMI_LANG_MAXLEN       4

/* Possible error values */
struct tlmi_err_codes {
	const char *err_str;
	int err_code;
};

enum encoding_option {
	TLMI_ENCODING_ASCII,
	TLMI_ENCODING_SCANCODE,
};

/* password configuration details */
struct tlmi_pwdcfg {
	uint32_t password_mode;
	uint32_t password_state;
	uint32_t min_length;
	uint32_t max_length;
	uint32_t supported_encodings;
	uint32_t supported_keyboard;
};

/* password setting details */
struct tlmi_pwd_setting {
	struct kobject kobj;
	bool valid;
	char password[TLMI_PWD_BUFSIZE];
	const char *pwd_type;
	const char *role;
	int minlen;
	int maxlen;
	enum encoding_option encoding;
	char kbdlang[TLMI_LANG_MAXLEN];
};

/* Attribute setting details */
struct tlmi_attr_setting {
	struct kobject kobj;
	int index;
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

	struct tlmi_attr_setting *setting[TLMI_SETTINGS_COUNT];
	struct device *class_dev;
	struct kset *attribute_kset;
	struct kset *authentication_kset;
	struct tlmi_pwd_setting *pwd_admin;
	struct tlmi_pwd_setting *pwd_power;
};

#endif /* !_THINK_LMI_H_ */
