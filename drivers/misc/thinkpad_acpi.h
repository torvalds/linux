/*
 *  thinkpad_acpi.h - ThinkPad ACPI Extras
 *
 *
 *  Copyright (C) 2004-2005 Borislav Deianov <borislav@users.sf.net>
 *  Copyright (C) 2006-2007 Henrique de Moraes Holschuh <hmh@hmh.eng.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#ifndef __THINKPAD_ACPI_H__
#define __THINKPAD_ACPI_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include <linux/proc_fs.h>
#include <linux/sysfs.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <asm/uaccess.h>

#include <linux/dmi.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>

#include <acpi/acpi_drivers.h>
#include <acpi/acnamesp.h>


/****************************************************************************
 * Main driver
 */

#define IBM_NAME "thinkpad"
#define IBM_DESC "ThinkPad ACPI Extras"
#define IBM_FILE "thinkpad_acpi"
#define IBM_URL "http://ibm-acpi.sf.net/"
#define IBM_MAIL "ibm-acpi-devel@lists.sourceforge.net"

#define IBM_PROC_DIR "ibm"
#define IBM_ACPI_EVENT_PREFIX "ibm"
#define IBM_DRVR_NAME IBM_FILE

#define IBM_LOG IBM_FILE ": "
#define IBM_ERR	   KERN_ERR    IBM_LOG
#define IBM_NOTICE KERN_NOTICE IBM_LOG
#define IBM_INFO   KERN_INFO   IBM_LOG
#define IBM_DEBUG  KERN_DEBUG  IBM_LOG

#define IBM_MAX_ACPI_ARGS 3

/* ThinkPad CMOS commands */
#define TP_CMOS_VOLUME_DOWN	0
#define TP_CMOS_VOLUME_UP	1
#define TP_CMOS_VOLUME_MUTE	2
#define TP_CMOS_BRIGHTNESS_UP	4
#define TP_CMOS_BRIGHTNESS_DOWN	5

#define onoff(status,bit) ((status) & (1 << (bit)) ? "on" : "off")
#define enabled(status,bit) ((status) & (1 << (bit)) ? "enabled" : "disabled")
#define strlencmp(a,b) (strncmp((a), (b), strlen(b)))

/* Debugging */
#define TPACPI_DBG_ALL		0xffff
#define TPACPI_DBG_ALL		0xffff
#define TPACPI_DBG_INIT		0x0001
#define TPACPI_DBG_EXIT		0x0002
#define dbg_printk(a_dbg_level, format, arg...) \
	do { if (dbg_level & a_dbg_level) \
		printk(IBM_DEBUG "%s: " format, __func__ , ## arg); } while (0)
#ifdef CONFIG_THINKPAD_ACPI_DEBUG
#define vdbg_printk(a_dbg_level, format, arg...) \
	dbg_printk(a_dbg_level, format, ## arg)
static const char *str_supported(int is_supported);
#else
#define vdbg_printk(a_dbg_level, format, arg...)
#endif

/* ACPI HIDs */
#define IBM_HKEY_HID    "IBM0068"
#define IBM_PCI_HID     "PNP0A03"

/* ACPI helpers */
static int __must_check acpi_evalf(acpi_handle handle,
		      void *res, char *method, char *fmt, ...);
static int __must_check acpi_ec_read(int i, u8 * p);
static int __must_check acpi_ec_write(int i, u8 v);
static int __must_check _sta(acpi_handle handle);

/* ACPI handles */
static acpi_handle root_handle;			/* root namespace */
static acpi_handle ec_handle;			/* EC */
static acpi_handle ecrd_handle, ecwr_handle;	/* 570 EC access */
static acpi_handle cmos_handle, hkey_handle;	/* basic thinkpad handles */

static void drv_acpi_handle_init(char *name,
		   acpi_handle *handle, acpi_handle parent,
		   char **paths, int num_paths, char **path);
#define IBM_ACPIHANDLE_INIT(object)						\
	drv_acpi_handle_init(#object, &object##_handle, *object##_parent,	\
		object##_paths, ARRAY_SIZE(object##_paths), &object##_path)

/* ThinkPad ACPI helpers */
static int issue_thinkpad_cmos_command(int cmos_cmd);

/* procfs support */
static struct proc_dir_entry *proc_dir;

/* procfs helpers */
static int dispatch_procfs_read(char *page, char **start, off_t off,
		int count, int *eof, void *data);
static int dispatch_procfs_write(struct file *file,
		const char __user * userbuf,
		unsigned long count, void *data);
static char *next_cmd(char **cmds);

/* sysfs support */
struct attribute_set {
	unsigned int members, max_members;
	struct attribute_group group;
};

static struct attribute_set *create_attr_set(unsigned int max_members,
						const char* name);
#define destroy_attr_set(_set) \
	kfree(_set);
static int add_to_attr_set(struct attribute_set* s, struct attribute *attr);
static int add_many_to_attr_set(struct attribute_set* s,
			struct attribute **attr,
			unsigned int count);
#define register_attr_set_with_sysfs(_attr_set, _kobj) \
	sysfs_create_group(_kobj, &_attr_set->group)
static void delete_attr_set(struct attribute_set* s, struct kobject *kobj);

static int parse_strtoul(const char *buf, unsigned long max,
			unsigned long *value);

/* Device model */
static struct platform_device *tpacpi_pdev;
static struct class_device *tpacpi_hwmon;
static struct platform_driver tpacpi_pdriver;
static int tpacpi_create_driver_attributes(struct device_driver *drv);
static void tpacpi_remove_driver_attributes(struct device_driver *drv);

/* Module */
static int experimental;
static u32 dbg_level;
static int force_load;
static char *ibm_thinkpad_ec_found;

static char* check_dmi_for_ec(void);
static int thinkpad_acpi_module_init(void);
static void thinkpad_acpi_module_exit(void);


/****************************************************************************
 * Subdrivers
 */

struct ibm_struct;

struct tp_acpi_drv_struct {
	char *hid;
	struct acpi_driver *driver;

	void (*notify) (struct ibm_struct *, u32);
	acpi_handle *handle;
	u32 type;
	struct acpi_device *device;
};

struct ibm_struct {
	char *name;

	int (*read) (char *);
	int (*write) (char *);
	void (*exit) (void);

	struct list_head all_drivers;

	struct tp_acpi_drv_struct *acpi;

	struct {
		u8 acpi_driver_registered:1;
		u8 acpi_notify_installed:1;
		u8 proc_created:1;
		u8 init_called:1;
		u8 experimental:1;
	} flags;
};

struct ibm_init_struct {
	char param[32];

	int (*init) (struct ibm_init_struct *);
	struct ibm_struct *data;
};

static struct {
#ifdef CONFIG_THINKPAD_ACPI_BAY
	u16 bay_status:1;
	u16 bay_eject:1;
	u16 bay_status2:1;
	u16 bay_eject2:1;
#endif
	u16 bluetooth:1;
	u16 hotkey:1;
	u16 hotkey_mask:1;
	u16 light:1;
	u16 light_status:1;
	u16 wan:1;
	u16 fan_ctrl_status_undef:1;
} tp_features;

static struct list_head tpacpi_all_drivers;

static struct ibm_init_struct ibms_init[];
static int set_ibm_param(const char *val, struct kernel_param *kp);
static int ibm_init(struct ibm_init_struct *iibm);
static void ibm_exit(struct ibm_struct *ibm);


/*
 * procfs master subdriver
 */
static int thinkpad_acpi_driver_init(struct ibm_init_struct *iibm);
static int thinkpad_acpi_driver_read(char *p);


/*
 * Bay subdriver
 */

#ifdef CONFIG_THINKPAD_ACPI_BAY
static acpi_handle bay_handle, bay_ej_handle;
static acpi_handle bay2_handle, bay2_ej_handle;

static int bay_init(struct ibm_init_struct *iibm);
static void bay_notify(struct ibm_struct *ibm, u32 event);
static int bay_read(char *p);
static int bay_write(char *buf);
#endif /* CONFIG_THINKPAD_ACPI_BAY */


/*
 * Beep subdriver
 */

static acpi_handle beep_handle;

static int beep_read(char *p);
static int beep_write(char *buf);


/*
 * Bluetooth subdriver
 */

#define TPACPI_BLUETH_SYSFS_GROUP "bluetooth"

enum {
	/* ACPI GBDC/SBDC bits */
	TP_ACPI_BLUETOOTH_HWPRESENT	= 0x01,	/* Bluetooth hw available */
	TP_ACPI_BLUETOOTH_RADIOSSW	= 0x02,	/* Bluetooth radio enabled */
	TP_ACPI_BLUETOOTH_UNK		= 0x04,	/* unknown function */
};

static int bluetooth_init(struct ibm_init_struct *iibm);
static int bluetooth_get_radiosw(void);
static int bluetooth_set_radiosw(int radio_on);
static int bluetooth_read(char *p);
static int bluetooth_write(char *buf);


/*
 * Brightness (backlight) subdriver
 */

#define TPACPI_BACKLIGHT_DEV_NAME "thinkpad_screen"

static struct backlight_device *ibm_backlight_device;
static int brightness_offset = 0x31;

static int brightness_init(struct ibm_init_struct *iibm);
static void brightness_exit(void);
static int brightness_get(struct backlight_device *bd);
static int brightness_set(int value);
static int brightness_update_status(struct backlight_device *bd);
static int brightness_read(char *p);
static int brightness_write(char *buf);


/*
 * CMOS subdriver
 */

static int cmos_read(char *p);
static int cmos_write(char *buf);


/*
 * Dock subdriver
 */

#ifdef CONFIG_THINKPAD_ACPI_DOCK
static acpi_handle pci_handle;
static acpi_handle dock_handle;

static void dock_notify(struct ibm_struct *ibm, u32 event);
static int dock_read(char *p);
static int dock_write(char *buf);
#endif /* CONFIG_THINKPAD_ACPI_DOCK */


/*
 * EC dump subdriver
 */

static int ecdump_read(char *p) ;
static int ecdump_write(char *buf);


/*
 * Fan subdriver
 */

enum {					/* Fan control constants */
	fan_status_offset = 0x2f,	/* EC register 0x2f */
	fan_rpm_offset = 0x84,		/* EC register 0x84: LSB, 0x85 MSB (RPM)
					 * 0x84 must be read before 0x85 */

	TP_EC_FAN_FULLSPEED = 0x40,	/* EC fan mode: full speed */
	TP_EC_FAN_AUTO	    = 0x80,	/* EC fan mode: auto fan control */

	TPACPI_FAN_LAST_LEVEL = 0x100,	/* Use cached last-seen fan level */
};

enum fan_status_access_mode {
	TPACPI_FAN_NONE = 0,		/* No fan status or control */
	TPACPI_FAN_RD_ACPI_GFAN,	/* Use ACPI GFAN */
	TPACPI_FAN_RD_TPEC,		/* Use ACPI EC regs 0x2f, 0x84-0x85 */
};

enum fan_control_access_mode {
	TPACPI_FAN_WR_NONE = 0,		/* No fan control */
	TPACPI_FAN_WR_ACPI_SFAN,	/* Use ACPI SFAN */
	TPACPI_FAN_WR_TPEC,		/* Use ACPI EC reg 0x2f */
	TPACPI_FAN_WR_ACPI_FANS,	/* Use ACPI FANS and EC reg 0x2f */
};

enum fan_control_commands {
	TPACPI_FAN_CMD_SPEED 	= 0x0001,	/* speed command */
	TPACPI_FAN_CMD_LEVEL 	= 0x0002,	/* level command  */
	TPACPI_FAN_CMD_ENABLE	= 0x0004,	/* enable/disable cmd,
						 * and also watchdog cmd */
};

static int fan_control_allowed;

static enum fan_status_access_mode fan_status_access_mode;
static enum fan_control_access_mode fan_control_access_mode;
static enum fan_control_commands fan_control_commands;
static u8 fan_control_initial_status;
static u8 fan_control_desired_level;
static int fan_watchdog_maxinterval;

static struct mutex fan_mutex;

static acpi_handle fans_handle, gfan_handle, sfan_handle;

static int fan_init(struct ibm_init_struct *iibm);
static void fan_exit(void);
static int fan_get_status(u8 *status);
static int fan_get_status_safe(u8 *status);
static int fan_get_speed(unsigned int *speed);
static void fan_update_desired_level(u8 status);
static void fan_watchdog_fire(struct work_struct *ignored);
static void fan_watchdog_reset(void);
static int fan_set_level(int level);
static int fan_set_level_safe(int level);
static int fan_set_enable(void);
static int fan_set_disable(void);
static int fan_set_speed(int speed);
static int fan_read(char *p);
static int fan_write(char *buf);
static int fan_write_cmd_level(const char *cmd, int *rc);
static int fan_write_cmd_enable(const char *cmd, int *rc);
static int fan_write_cmd_disable(const char *cmd, int *rc);
static int fan_write_cmd_speed(const char *cmd, int *rc);
static int fan_write_cmd_watchdog(const char *cmd, int *rc);


/*
 * Hotkey subdriver
 */

#define TPACPI_HOTKEY_SYSFS_GROUP "hotkey"

static int hotkey_orig_status;
static int hotkey_orig_mask;

static struct mutex hotkey_mutex;

static int hotkey_init(struct ibm_init_struct *iibm);
static void hotkey_exit(void);
static int hotkey_get(int *status, int *mask);
static int hotkey_set(int status, int mask);
static void hotkey_notify(struct ibm_struct *ibm, u32 event);
static int hotkey_read(char *p);
static int hotkey_write(char *buf);


/*
 * LED subdriver
 */

enum led_access_mode {
	TPACPI_LED_NONE = 0,
	TPACPI_LED_570,	/* 570 */
	TPACPI_LED_OLD,	/* 600e/x, 770e, 770x, A21e, A2xm/p, T20-22, X20-21 */
	TPACPI_LED_NEW,	/* all others */
};

enum {	/* For TPACPI_LED_OLD */
	TPACPI_LED_EC_HLCL = 0x0c,	/* EC reg to get led to power on */
	TPACPI_LED_EC_HLBL = 0x0d,	/* EC reg to blink a lit led */
	TPACPI_LED_EC_HLMS = 0x0e,	/* EC reg to select led to command */
};

static enum led_access_mode led_supported;
static acpi_handle led_handle;

static int led_init(struct ibm_init_struct *iibm);
static int led_read(char *p);
static int led_write(char *buf);

/*
 * Light (thinklight) subdriver
 */

static acpi_handle lght_handle, ledb_handle;

static int light_init(struct ibm_init_struct *iibm);
static int light_read(char *p);
static int light_write(char *buf);


/*
 * Thermal subdriver
 */

enum thermal_access_mode {
	TPACPI_THERMAL_NONE = 0,	/* No thermal support */
	TPACPI_THERMAL_ACPI_TMP07,	/* Use ACPI TMP0-7 */
	TPACPI_THERMAL_ACPI_UPDT,	/* Use ACPI TMP0-7 with UPDT */
	TPACPI_THERMAL_TPEC_8,		/* Use ACPI EC regs, 8 sensors */
	TPACPI_THERMAL_TPEC_16,		/* Use ACPI EC regs, 16 sensors */
};

enum { /* TPACPI_THERMAL_TPEC_* */
	TP_EC_THERMAL_TMP0 = 0x78,	/* ACPI EC regs TMP 0..7 */
	TP_EC_THERMAL_TMP8 = 0xC0,	/* ACPI EC regs TMP 8..15 */
	TP_EC_THERMAL_TMP_NA = -128,	/* ACPI EC sensor not available */
};

#define TPACPI_MAX_THERMAL_SENSORS 16	/* Max thermal sensors supported */
struct ibm_thermal_sensors_struct {
	s32 temp[TPACPI_MAX_THERMAL_SENSORS];
};

static enum thermal_access_mode thermal_read_mode;

static int thermal_init(struct ibm_init_struct *iibm);
static int thermal_get_sensor(int idx, s32 *value);
static int thermal_get_sensors(struct ibm_thermal_sensors_struct *s);
static int thermal_read(char *p);


/*
 * Video subdriver
 */

enum video_access_mode {
	TPACPI_VIDEO_NONE = 0,
	TPACPI_VIDEO_570,	/* 570 */
	TPACPI_VIDEO_770,	/* 600e/x, 770e, 770x */
	TPACPI_VIDEO_NEW,	/* all others */
};

enum {	/* video status flags, based on VIDEO_570 */
	TP_ACPI_VIDEO_S_LCD = 0x01,	/* LCD output enabled */
	TP_ACPI_VIDEO_S_CRT = 0x02,	/* CRT output enabled */
	TP_ACPI_VIDEO_S_DVI = 0x08,	/* DVI output enabled */
};

enum {  /* TPACPI_VIDEO_570 constants */
	TP_ACPI_VIDEO_570_PHSCMD = 0x87,	/* unknown magic constant :( */
	TP_ACPI_VIDEO_570_PHSMASK = 0x03,	/* PHS bits that map to
						 * video_status_flags */
	TP_ACPI_VIDEO_570_PHS2CMD = 0x8b,	/* unknown magic constant :( */
	TP_ACPI_VIDEO_570_PHS2SET = 0x80,	/* unknown magic constant :( */
};

static enum video_access_mode video_supported;
static int video_orig_autosw;
static acpi_handle vid_handle, vid2_handle;

static int video_init(struct ibm_init_struct *iibm);
static void video_exit(void);
static int video_outputsw_get(void);
static int video_outputsw_set(int status);
static int video_autosw_get(void);
static int video_autosw_set(int enable);
static int video_outputsw_cycle(void);
static int video_expand_toggle(void);
static int video_read(char *p);
static int video_write(char *buf);


/*
 * Volume subdriver
 */

static int volume_offset = 0x30;

static int volume_read(char *p);
static int volume_write(char *buf);


/*
 * Wan subdriver
 */

#define TPACPI_WAN_SYSFS_GROUP "wwan"

enum {
	/* ACPI GWAN/SWAN bits */
	TP_ACPI_WANCARD_HWPRESENT	= 0x01,	/* Wan hw available */
	TP_ACPI_WANCARD_RADIOSSW	= 0x02,	/* Wan radio enabled */
	TP_ACPI_WANCARD_UNK		= 0x04,	/* unknown function */
};

static int wan_init(struct ibm_init_struct *iibm);
static int wan_get_radiosw(void);
static int wan_set_radiosw(int radio_on);
static int wan_read(char *p);
static int wan_write(char *buf);


#endif /* __THINKPAD_ACPI_H */
