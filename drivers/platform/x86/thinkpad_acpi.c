/*
 *  thinkpad_acpi.c - ThinkPad ACPI Extras
 *
 *
 *  Copyright (C) 2004-2005 Borislav Deianov <borislav@users.sf.net>
 *  Copyright (C) 2006-2009 Henrique de Moraes Holschuh <hmh@hmh.eng.br>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define TPACPI_VERSION "0.25"
#define TPACPI_SYSFS_VERSION 0x020700

/*
 *  Changelog:
 *  2007-10-20		changelog trimmed down
 *
 *  2007-03-27  0.14	renamed to thinkpad_acpi and moved to
 *  			drivers/misc.
 *
 *  2006-11-22	0.13	new maintainer
 *  			changelog now lives in git commit history, and will
 *  			not be updated further in-file.
 *
 *  2005-03-17	0.11	support for 600e, 770x
 *			    thanks to Jamie Lentin <lentinj@dial.pipex.com>
 *
 *  2005-01-16	0.9	use MODULE_VERSION
 *			    thanks to Henrik Brix Andersen <brix@gentoo.org>
 *			fix parameter passing on module loading
 *			    thanks to Rusty Russell <rusty@rustcorp.com.au>
 *			    thanks to Jim Radford <radford@blackbean.org>
 *  2004-11-08	0.8	fix init error case, don't return from a macro
 *			    thanks to Chris Wright <chrisw@osdl.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/nvram.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sysfs.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/rfkill.h>
#include <linux/dmi.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/acpi.h>
#include <linux/pci_ids.h>
#include <linux/thinkpad_acpi.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <asm/uaccess.h>

/* ThinkPad CMOS commands */
#define TP_CMOS_VOLUME_DOWN	0
#define TP_CMOS_VOLUME_UP	1
#define TP_CMOS_VOLUME_MUTE	2
#define TP_CMOS_BRIGHTNESS_UP	4
#define TP_CMOS_BRIGHTNESS_DOWN	5
#define TP_CMOS_THINKLIGHT_ON	12
#define TP_CMOS_THINKLIGHT_OFF	13

/* NVRAM Addresses */
enum tp_nvram_addr {
	TP_NVRAM_ADDR_HK2		= 0x57,
	TP_NVRAM_ADDR_THINKLIGHT	= 0x58,
	TP_NVRAM_ADDR_VIDEO		= 0x59,
	TP_NVRAM_ADDR_BRIGHTNESS	= 0x5e,
	TP_NVRAM_ADDR_MIXER		= 0x60,
};

/* NVRAM bit masks */
enum {
	TP_NVRAM_MASK_HKT_THINKPAD	= 0x08,
	TP_NVRAM_MASK_HKT_ZOOM		= 0x20,
	TP_NVRAM_MASK_HKT_DISPLAY	= 0x40,
	TP_NVRAM_MASK_HKT_HIBERNATE	= 0x80,
	TP_NVRAM_MASK_THINKLIGHT	= 0x10,
	TP_NVRAM_MASK_HKT_DISPEXPND	= 0x30,
	TP_NVRAM_MASK_HKT_BRIGHTNESS	= 0x20,
	TP_NVRAM_MASK_LEVEL_BRIGHTNESS	= 0x0f,
	TP_NVRAM_POS_LEVEL_BRIGHTNESS	= 0,
	TP_NVRAM_MASK_MUTE		= 0x40,
	TP_NVRAM_MASK_HKT_VOLUME	= 0x80,
	TP_NVRAM_MASK_LEVEL_VOLUME	= 0x0f,
	TP_NVRAM_POS_LEVEL_VOLUME	= 0,
};

/* Misc NVRAM-related */
enum {
	TP_NVRAM_LEVEL_VOLUME_MAX = 14,
};

/* ACPI HIDs */
#define TPACPI_ACPI_IBM_HKEY_HID	"IBM0068"
#define TPACPI_ACPI_LENOVO_HKEY_HID	"LEN0068"
#define TPACPI_ACPI_EC_HID		"PNP0C09"

/* Input IDs */
#define TPACPI_HKEY_INPUT_PRODUCT	0x5054 /* "TP" */
#define TPACPI_HKEY_INPUT_VERSION	0x4101

/* ACPI \WGSV commands */
enum {
	TP_ACPI_WGSV_GET_STATE		= 0x01, /* Get state information */
	TP_ACPI_WGSV_PWR_ON_ON_RESUME	= 0x02, /* Resume WWAN powered on */
	TP_ACPI_WGSV_PWR_OFF_ON_RESUME	= 0x03,	/* Resume WWAN powered off */
	TP_ACPI_WGSV_SAVE_STATE		= 0x04, /* Save state for S4/S5 */
};

/* TP_ACPI_WGSV_GET_STATE bits */
enum {
	TP_ACPI_WGSV_STATE_WWANEXIST	= 0x0001, /* WWAN hw available */
	TP_ACPI_WGSV_STATE_WWANPWR	= 0x0002, /* WWAN radio enabled */
	TP_ACPI_WGSV_STATE_WWANPWRRES	= 0x0004, /* WWAN state at resume */
	TP_ACPI_WGSV_STATE_WWANBIOSOFF	= 0x0008, /* WWAN disabled in BIOS */
	TP_ACPI_WGSV_STATE_BLTHEXIST	= 0x0001, /* BLTH hw available */
	TP_ACPI_WGSV_STATE_BLTHPWR	= 0x0002, /* BLTH radio enabled */
	TP_ACPI_WGSV_STATE_BLTHPWRRES	= 0x0004, /* BLTH state at resume */
	TP_ACPI_WGSV_STATE_BLTHBIOSOFF	= 0x0008, /* BLTH disabled in BIOS */
	TP_ACPI_WGSV_STATE_UWBEXIST	= 0x0010, /* UWB hw available */
	TP_ACPI_WGSV_STATE_UWBPWR	= 0x0020, /* UWB radio enabled */
};

/* HKEY events */
enum tpacpi_hkey_event_t {
	/* Hotkey-related */
	TP_HKEY_EV_HOTKEY_BASE		= 0x1001, /* first hotkey (FN+F1) */
	TP_HKEY_EV_BRGHT_UP		= 0x1010, /* Brightness up */
	TP_HKEY_EV_BRGHT_DOWN		= 0x1011, /* Brightness down */
	TP_HKEY_EV_VOL_UP		= 0x1015, /* Volume up or unmute */
	TP_HKEY_EV_VOL_DOWN		= 0x1016, /* Volume down or unmute */
	TP_HKEY_EV_VOL_MUTE		= 0x1017, /* Mixer output mute */

	/* Reasons for waking up from S3/S4 */
	TP_HKEY_EV_WKUP_S3_UNDOCK	= 0x2304, /* undock requested, S3 */
	TP_HKEY_EV_WKUP_S4_UNDOCK	= 0x2404, /* undock requested, S4 */
	TP_HKEY_EV_WKUP_S3_BAYEJ	= 0x2305, /* bay ejection req, S3 */
	TP_HKEY_EV_WKUP_S4_BAYEJ	= 0x2405, /* bay ejection req, S4 */
	TP_HKEY_EV_WKUP_S3_BATLOW	= 0x2313, /* battery empty, S3 */
	TP_HKEY_EV_WKUP_S4_BATLOW	= 0x2413, /* battery empty, S4 */

	/* Auto-sleep after eject request */
	TP_HKEY_EV_BAYEJ_ACK		= 0x3003, /* bay ejection complete */
	TP_HKEY_EV_UNDOCK_ACK		= 0x4003, /* undock complete */

	/* Misc bay events */
	TP_HKEY_EV_OPTDRV_EJ		= 0x3006, /* opt. drive tray ejected */
	TP_HKEY_EV_HOTPLUG_DOCK		= 0x4010, /* docked into hotplug dock
						     or port replicator */
	TP_HKEY_EV_HOTPLUG_UNDOCK	= 0x4011, /* undocked from hotplug
						     dock or port replicator */

	/* User-interface events */
	TP_HKEY_EV_LID_CLOSE		= 0x5001, /* laptop lid closed */
	TP_HKEY_EV_LID_OPEN		= 0x5002, /* laptop lid opened */
	TP_HKEY_EV_TABLET_TABLET	= 0x5009, /* tablet swivel up */
	TP_HKEY_EV_TABLET_NOTEBOOK	= 0x500a, /* tablet swivel down */
	TP_HKEY_EV_PEN_INSERTED		= 0x500b, /* tablet pen inserted */
	TP_HKEY_EV_PEN_REMOVED		= 0x500c, /* tablet pen removed */
	TP_HKEY_EV_BRGHT_CHANGED	= 0x5010, /* backlight control event */

	/* Key-related user-interface events */
	TP_HKEY_EV_KEY_NUMLOCK		= 0x6000, /* NumLock key pressed */
	TP_HKEY_EV_KEY_FN		= 0x6005, /* Fn key pressed? E420 */

	/* Thermal events */
	TP_HKEY_EV_ALARM_BAT_HOT	= 0x6011, /* battery too hot */
	TP_HKEY_EV_ALARM_BAT_XHOT	= 0x6012, /* battery critically hot */
	TP_HKEY_EV_ALARM_SENSOR_HOT	= 0x6021, /* sensor too hot */
	TP_HKEY_EV_ALARM_SENSOR_XHOT	= 0x6022, /* sensor critically hot */
	TP_HKEY_EV_THM_TABLE_CHANGED	= 0x6030, /* thermal table changed */

	/* AC-related events */
	TP_HKEY_EV_AC_CHANGED		= 0x6040, /* AC status changed */

	/* Misc */
	TP_HKEY_EV_RFKILL_CHANGED	= 0x7000, /* rfkill switch changed */
};

/****************************************************************************
 * Main driver
 */

#define TPACPI_NAME "thinkpad"
#define TPACPI_DESC "ThinkPad ACPI Extras"
#define TPACPI_FILE TPACPI_NAME "_acpi"
#define TPACPI_URL "http://ibm-acpi.sf.net/"
#define TPACPI_MAIL "ibm-acpi-devel@lists.sourceforge.net"

#define TPACPI_PROC_DIR "ibm"
#define TPACPI_ACPI_EVENT_PREFIX "ibm"
#define TPACPI_DRVR_NAME TPACPI_FILE
#define TPACPI_DRVR_SHORTNAME "tpacpi"
#define TPACPI_HWMON_DRVR_NAME TPACPI_NAME "_hwmon"

#define TPACPI_NVRAM_KTHREAD_NAME "ktpacpi_nvramd"
#define TPACPI_WORKQUEUE_NAME "ktpacpid"

#define TPACPI_MAX_ACPI_ARGS 3

/* Debugging printk groups */
#define TPACPI_DBG_ALL		0xffff
#define TPACPI_DBG_DISCLOSETASK	0x8000
#define TPACPI_DBG_INIT		0x0001
#define TPACPI_DBG_EXIT		0x0002
#define TPACPI_DBG_RFKILL	0x0004
#define TPACPI_DBG_HKEY		0x0008
#define TPACPI_DBG_FAN		0x0010
#define TPACPI_DBG_BRGHT	0x0020
#define TPACPI_DBG_MIXER	0x0040

#define onoff(status, bit) ((status) & (1 << (bit)) ? "on" : "off")
#define enabled(status, bit) ((status) & (1 << (bit)) ? "enabled" : "disabled")
#define strlencmp(a, b) (strncmp((a), (b), strlen(b)))


/****************************************************************************
 * Driver-wide structs and misc. variables
 */

struct ibm_struct;

struct tp_acpi_drv_struct {
	const struct acpi_device_id *hid;
	struct acpi_driver *driver;

	void (*notify) (struct ibm_struct *, u32);
	acpi_handle *handle;
	u32 type;
	struct acpi_device *device;
};

struct ibm_struct {
	char *name;

	int (*read) (struct seq_file *);
	int (*write) (char *);
	void (*exit) (void);
	void (*resume) (void);
	void (*suspend) (void);
	void (*shutdown) (void);

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
	umode_t base_procfs_mode;
	struct ibm_struct *data;
};

static struct {
	u32 bluetooth:1;
	u32 hotkey:1;
	u32 hotkey_mask:1;
	u32 hotkey_wlsw:1;
	u32 hotkey_tablet:1;
	u32 light:1;
	u32 light_status:1;
	u32 bright_acpimode:1;
	u32 bright_unkfw:1;
	u32 wan:1;
	u32 uwb:1;
	u32 fan_ctrl_status_undef:1;
	u32 second_fan:1;
	u32 beep_needs_two_args:1;
	u32 mixer_no_level_control:1;
	u32 input_device_registered:1;
	u32 platform_drv_registered:1;
	u32 platform_drv_attrs_registered:1;
	u32 sensors_pdrv_registered:1;
	u32 sensors_pdrv_attrs_registered:1;
	u32 sensors_pdev_attrs_registered:1;
	u32 hotkey_poll_active:1;
} tp_features;

static struct {
	u16 hotkey_mask_ff:1;
	u16 volume_ctrl_forbidden:1;
} tp_warned;

struct thinkpad_id_data {
	unsigned int vendor;	/* ThinkPad vendor:
				 * PCI_VENDOR_ID_IBM/PCI_VENDOR_ID_LENOVO */

	char *bios_version_str;	/* Something like 1ZET51WW (1.03z) */
	char *ec_version_str;	/* Something like 1ZHT51WW-1.04a */

	u16 bios_model;		/* 1Y = 0x5931, 0 = unknown */
	u16 ec_model;
	u16 bios_release;	/* 1ZETK1WW = 0x314b, 0 = unknown */
	u16 ec_release;

	char *model_str;	/* ThinkPad T43 */
	char *nummodel_str;	/* 9384A9C for a 9384-A9C model */
};
static struct thinkpad_id_data thinkpad_id;

static enum {
	TPACPI_LIFE_INIT = 0,
	TPACPI_LIFE_RUNNING,
	TPACPI_LIFE_EXITING,
} tpacpi_lifecycle;

static int experimental;
static u32 dbg_level;

static struct workqueue_struct *tpacpi_wq;

enum led_status_t {
	TPACPI_LED_OFF = 0,
	TPACPI_LED_ON,
	TPACPI_LED_BLINK,
};

/* Special LED class that can defer work */
struct tpacpi_led_classdev {
	struct led_classdev led_classdev;
	struct work_struct work;
	enum led_status_t new_state;
	int led;
};

/* brightness level capabilities */
static unsigned int bright_maxlvl;	/* 0 = unknown */

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
static int dbg_wlswemul;
static bool tpacpi_wlsw_emulstate;
static int dbg_bluetoothemul;
static bool tpacpi_bluetooth_emulstate;
static int dbg_wwanemul;
static bool tpacpi_wwan_emulstate;
static int dbg_uwbemul;
static bool tpacpi_uwb_emulstate;
#endif


/*************************************************************************
 *  Debugging helpers
 */

#define dbg_printk(a_dbg_level, format, arg...)				\
do {									\
	if (dbg_level & (a_dbg_level))					\
		printk(KERN_DEBUG pr_fmt("%s: " format),		\
		       __func__, ##arg);				\
} while (0)

#ifdef CONFIG_THINKPAD_ACPI_DEBUG
#define vdbg_printk dbg_printk
static const char *str_supported(int is_supported);
#else
static inline const char *str_supported(int is_supported) { return ""; }
#define vdbg_printk(a_dbg_level, format, arg...)	\
	no_printk(format, ##arg)
#endif

static void tpacpi_log_usertask(const char * const what)
{
	printk(KERN_DEBUG pr_fmt("%s: access by process with PID %d\n"),
	       what, task_tgid_vnr(current));
}

#define tpacpi_disclose_usertask(what, format, arg...)			\
do {									\
	if (unlikely((dbg_level & TPACPI_DBG_DISCLOSETASK) &&		\
		     (tpacpi_lifecycle == TPACPI_LIFE_RUNNING))) {	\
		printk(KERN_DEBUG pr_fmt("%s: PID %d: " format),	\
		       what, task_tgid_vnr(current), ## arg);		\
	}								\
} while (0)

/*
 * Quirk handling helpers
 *
 * ThinkPad IDs and versions seen in the field so far
 * are two-characters from the set [0-9A-Z], i.e. base 36.
 *
 * We use values well outside that range as specials.
 */

#define TPACPI_MATCH_ANY		0xffffU
#define TPACPI_MATCH_UNKNOWN		0U

/* TPID('1', 'Y') == 0x5931 */
#define TPID(__c1, __c2) (((__c2) << 8) | (__c1))

#define TPACPI_Q_IBM(__id1, __id2, __quirk)	\
	{ .vendor = PCI_VENDOR_ID_IBM,		\
	  .bios = TPID(__id1, __id2),		\
	  .ec = TPACPI_MATCH_ANY,		\
	  .quirks = (__quirk) }

#define TPACPI_Q_LNV(__id1, __id2, __quirk)	\
	{ .vendor = PCI_VENDOR_ID_LENOVO,	\
	  .bios = TPID(__id1, __id2),		\
	  .ec = TPACPI_MATCH_ANY,		\
	  .quirks = (__quirk) }

#define TPACPI_QEC_LNV(__id1, __id2, __quirk)	\
	{ .vendor = PCI_VENDOR_ID_LENOVO,	\
	  .bios = TPACPI_MATCH_ANY,		\
	  .ec = TPID(__id1, __id2),		\
	  .quirks = (__quirk) }

struct tpacpi_quirk {
	unsigned int vendor;
	u16 bios;
	u16 ec;
	unsigned long quirks;
};

/**
 * tpacpi_check_quirks() - search BIOS/EC version on a list
 * @qlist:		array of &struct tpacpi_quirk
 * @qlist_size:		number of elements in @qlist
 *
 * Iterates over a quirks list until one is found that matches the
 * ThinkPad's vendor, BIOS and EC model.
 *
 * Returns 0 if nothing matches, otherwise returns the quirks field of
 * the matching &struct tpacpi_quirk entry.
 *
 * The match criteria is: vendor, ec and bios much match.
 */
static unsigned long __init tpacpi_check_quirks(
			const struct tpacpi_quirk *qlist,
			unsigned int qlist_size)
{
	while (qlist_size) {
		if ((qlist->vendor == thinkpad_id.vendor ||
				qlist->vendor == TPACPI_MATCH_ANY) &&
		    (qlist->bios == thinkpad_id.bios_model ||
				qlist->bios == TPACPI_MATCH_ANY) &&
		    (qlist->ec == thinkpad_id.ec_model ||
				qlist->ec == TPACPI_MATCH_ANY))
			return qlist->quirks;

		qlist_size--;
		qlist++;
	}
	return 0;
}

static inline bool __pure __init tpacpi_is_lenovo(void)
{
	return thinkpad_id.vendor == PCI_VENDOR_ID_LENOVO;
}

static inline bool __pure __init tpacpi_is_ibm(void)
{
	return thinkpad_id.vendor == PCI_VENDOR_ID_IBM;
}

/****************************************************************************
 ****************************************************************************
 *
 * ACPI Helpers and device model
 *
 ****************************************************************************
 ****************************************************************************/

/*************************************************************************
 * ACPI basic handles
 */

static acpi_handle root_handle;
static acpi_handle ec_handle;

#define TPACPI_HANDLE(object, parent, paths...)			\
	static acpi_handle  object##_handle;			\
	static const acpi_handle * const object##_parent __initconst =	\
						&parent##_handle; \
	static char *object##_paths[] __initdata = { paths }

TPACPI_HANDLE(ecrd, ec, "ECRD");	/* 570 */
TPACPI_HANDLE(ecwr, ec, "ECWR");	/* 570 */

TPACPI_HANDLE(cmos, root, "\\UCMS",	/* R50, R50e, R50p, R51, */
					/* T4x, X31, X40 */
	   "\\CMOS",		/* A3x, G4x, R32, T23, T30, X22-24, X30 */
	   "\\CMS",		/* R40, R40e */
	   );			/* all others */

TPACPI_HANDLE(hkey, ec, "\\_SB.HKEY",	/* 600e/x, 770e, 770x */
	   "^HKEY",		/* R30, R31 */
	   "HKEY",		/* all others */
	   );			/* 570 */

/*************************************************************************
 * ACPI helpers
 */

static int acpi_evalf(acpi_handle handle,
		      int *res, char *method, char *fmt, ...)
{
	char *fmt0 = fmt;
	struct acpi_object_list params;
	union acpi_object in_objs[TPACPI_MAX_ACPI_ARGS];
	struct acpi_buffer result, *resultp;
	union acpi_object out_obj;
	acpi_status status;
	va_list ap;
	char res_type;
	int success;
	int quiet;

	if (!*fmt) {
		pr_err("acpi_evalf() called with empty format\n");
		return 0;
	}

	if (*fmt == 'q') {
		quiet = 1;
		fmt++;
	} else
		quiet = 0;

	res_type = *(fmt++);

	params.count = 0;
	params.pointer = &in_objs[0];

	va_start(ap, fmt);
	while (*fmt) {
		char c = *(fmt++);
		switch (c) {
		case 'd':	/* int */
			in_objs[params.count].integer.value = va_arg(ap, int);
			in_objs[params.count++].type = ACPI_TYPE_INTEGER;
			break;
			/* add more types as needed */
		default:
			pr_err("acpi_evalf() called "
			       "with invalid format character '%c'\n", c);
			va_end(ap);
			return 0;
		}
	}
	va_end(ap);

	if (res_type != 'v') {
		result.length = sizeof(out_obj);
		result.pointer = &out_obj;
		resultp = &result;
	} else
		resultp = NULL;

	status = acpi_evaluate_object(handle, method, &params, resultp);

	switch (res_type) {
	case 'd':		/* int */
		success = (status == AE_OK &&
			   out_obj.type == ACPI_TYPE_INTEGER);
		if (success && res)
			*res = out_obj.integer.value;
		break;
	case 'v':		/* void */
		success = status == AE_OK;
		break;
		/* add more types as needed */
	default:
		pr_err("acpi_evalf() called "
		       "with invalid format character '%c'\n", res_type);
		return 0;
	}

	if (!success && !quiet)
		pr_err("acpi_evalf(%s, %s, ...) failed: %s\n",
		       method, fmt0, acpi_format_exception(status));

	return success;
}

static int acpi_ec_read(int i, u8 *p)
{
	int v;

	if (ecrd_handle) {
		if (!acpi_evalf(ecrd_handle, &v, NULL, "dd", i))
			return 0;
		*p = v;
	} else {
		if (ec_read(i, p) < 0)
			return 0;
	}

	return 1;
}

static int acpi_ec_write(int i, u8 v)
{
	if (ecwr_handle) {
		if (!acpi_evalf(ecwr_handle, NULL, NULL, "vdd", i, v))
			return 0;
	} else {
		if (ec_write(i, v) < 0)
			return 0;
	}

	return 1;
}

static int issue_thinkpad_cmos_command(int cmos_cmd)
{
	if (!cmos_handle)
		return -ENXIO;

	if (!acpi_evalf(cmos_handle, NULL, NULL, "vd", cmos_cmd))
		return -EIO;

	return 0;
}

/*************************************************************************
 * ACPI device model
 */

#define TPACPI_ACPIHANDLE_INIT(object) \
	drv_acpi_handle_init(#object, &object##_handle, *object##_parent, \
		object##_paths, ARRAY_SIZE(object##_paths))

static void __init drv_acpi_handle_init(const char *name,
			   acpi_handle *handle, const acpi_handle parent,
			   char **paths, const int num_paths)
{
	int i;
	acpi_status status;

	vdbg_printk(TPACPI_DBG_INIT, "trying to locate ACPI handle for %s\n",
		name);

	for (i = 0; i < num_paths; i++) {
		status = acpi_get_handle(parent, paths[i], handle);
		if (ACPI_SUCCESS(status)) {
			dbg_printk(TPACPI_DBG_INIT,
				   "Found ACPI handle %s for %s\n",
				   paths[i], name);
			return;
		}
	}

	vdbg_printk(TPACPI_DBG_INIT, "ACPI handle for %s not found\n",
		    name);
	*handle = NULL;
}

static acpi_status __init tpacpi_acpi_handle_locate_callback(acpi_handle handle,
			u32 level, void *context, void **return_value)
{
	struct acpi_device *dev;
	if (!strcmp(context, "video")) {
		if (acpi_bus_get_device(handle, &dev))
			return AE_OK;
		if (strcmp(ACPI_VIDEO_HID, acpi_device_hid(dev)))
			return AE_OK;
	}

	*(acpi_handle *)return_value = handle;

	return AE_CTRL_TERMINATE;
}

static void __init tpacpi_acpi_handle_locate(const char *name,
		const char *hid,
		acpi_handle *handle)
{
	acpi_status status;
	acpi_handle device_found;

	BUG_ON(!name || !handle);
	vdbg_printk(TPACPI_DBG_INIT,
			"trying to locate ACPI handle for %s, using HID %s\n",
			name, hid ? hid : "NULL");

	memset(&device_found, 0, sizeof(device_found));
	status = acpi_get_devices(hid, tpacpi_acpi_handle_locate_callback,
				  (void *)name, &device_found);

	*handle = NULL;

	if (ACPI_SUCCESS(status)) {
		*handle = device_found;
		dbg_printk(TPACPI_DBG_INIT,
			   "Found ACPI handle for %s\n", name);
	} else {
		vdbg_printk(TPACPI_DBG_INIT,
			    "Could not locate an ACPI handle for %s: %s\n",
			    name, acpi_format_exception(status));
	}
}

static void dispatch_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct ibm_struct *ibm = data;

	if (tpacpi_lifecycle != TPACPI_LIFE_RUNNING)
		return;

	if (!ibm || !ibm->acpi || !ibm->acpi->notify)
		return;

	ibm->acpi->notify(ibm, event);
}

static int __init setup_acpi_notify(struct ibm_struct *ibm)
{
	acpi_status status;
	int rc;

	BUG_ON(!ibm->acpi);

	if (!*ibm->acpi->handle)
		return 0;

	vdbg_printk(TPACPI_DBG_INIT,
		"setting up ACPI notify for %s\n", ibm->name);

	rc = acpi_bus_get_device(*ibm->acpi->handle, &ibm->acpi->device);
	if (rc < 0) {
		pr_err("acpi_bus_get_device(%s) failed: %d\n", ibm->name, rc);
		return -ENODEV;
	}

	ibm->acpi->device->driver_data = ibm;
	sprintf(acpi_device_class(ibm->acpi->device), "%s/%s",
		TPACPI_ACPI_EVENT_PREFIX,
		ibm->name);

	status = acpi_install_notify_handler(*ibm->acpi->handle,
			ibm->acpi->type, dispatch_acpi_notify, ibm);
	if (ACPI_FAILURE(status)) {
		if (status == AE_ALREADY_EXISTS) {
			pr_notice("another device driver is already "
				  "handling %s events\n", ibm->name);
		} else {
			pr_err("acpi_install_notify_handler(%s) failed: %s\n",
			       ibm->name, acpi_format_exception(status));
		}
		return -ENODEV;
	}
	ibm->flags.acpi_notify_installed = 1;
	return 0;
}

static int __init tpacpi_device_add(struct acpi_device *device)
{
	return 0;
}

static int __init register_tpacpi_subdriver(struct ibm_struct *ibm)
{
	int rc;

	dbg_printk(TPACPI_DBG_INIT,
		"registering %s as an ACPI driver\n", ibm->name);

	BUG_ON(!ibm->acpi);

	ibm->acpi->driver = kzalloc(sizeof(struct acpi_driver), GFP_KERNEL);
	if (!ibm->acpi->driver) {
		pr_err("failed to allocate memory for ibm->acpi->driver\n");
		return -ENOMEM;
	}

	sprintf(ibm->acpi->driver->name, "%s_%s", TPACPI_NAME, ibm->name);
	ibm->acpi->driver->ids = ibm->acpi->hid;

	ibm->acpi->driver->ops.add = &tpacpi_device_add;

	rc = acpi_bus_register_driver(ibm->acpi->driver);
	if (rc < 0) {
		pr_err("acpi_bus_register_driver(%s) failed: %d\n",
		       ibm->name, rc);
		kfree(ibm->acpi->driver);
		ibm->acpi->driver = NULL;
	} else if (!rc)
		ibm->flags.acpi_driver_registered = 1;

	return rc;
}


/****************************************************************************
 ****************************************************************************
 *
 * Procfs Helpers
 *
 ****************************************************************************
 ****************************************************************************/

static int dispatch_proc_show(struct seq_file *m, void *v)
{
	struct ibm_struct *ibm = m->private;

	if (!ibm || !ibm->read)
		return -EINVAL;
	return ibm->read(m);
}

static int dispatch_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dispatch_proc_show, PDE_DATA(inode));
}

static ssize_t dispatch_proc_write(struct file *file,
			const char __user *userbuf,
			size_t count, loff_t *pos)
{
	struct ibm_struct *ibm = PDE_DATA(file_inode(file));
	char *kernbuf;
	int ret;

	if (!ibm || !ibm->write)
		return -EINVAL;
	if (count > PAGE_SIZE - 2)
		return -EINVAL;

	kernbuf = kmalloc(count + 2, GFP_KERNEL);
	if (!kernbuf)
		return -ENOMEM;

	if (copy_from_user(kernbuf, userbuf, count)) {
		kfree(kernbuf);
		return -EFAULT;
	}

	kernbuf[count] = 0;
	strcat(kernbuf, ",");
	ret = ibm->write(kernbuf);
	if (ret == 0)
		ret = count;

	kfree(kernbuf);

	return ret;
}

static const struct file_operations dispatch_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= dispatch_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= dispatch_proc_write,
};

static char *next_cmd(char **cmds)
{
	char *start = *cmds;
	char *end;

	while ((end = strchr(start, ',')) && end == start)
		start = end + 1;

	if (!end)
		return NULL;

	*end = 0;
	*cmds = end + 1;
	return start;
}


/****************************************************************************
 ****************************************************************************
 *
 * Device model: input, hwmon and platform
 *
 ****************************************************************************
 ****************************************************************************/

static struct platform_device *tpacpi_pdev;
static struct platform_device *tpacpi_sensors_pdev;
static struct device *tpacpi_hwmon;
static struct input_dev *tpacpi_inputdev;
static struct mutex tpacpi_inputdev_send_mutex;
static LIST_HEAD(tpacpi_all_drivers);

#ifdef CONFIG_PM_SLEEP
static int tpacpi_suspend_handler(struct device *dev)
{
	struct ibm_struct *ibm, *itmp;

	list_for_each_entry_safe(ibm, itmp,
				 &tpacpi_all_drivers,
				 all_drivers) {
		if (ibm->suspend)
			(ibm->suspend)();
	}

	return 0;
}

static int tpacpi_resume_handler(struct device *dev)
{
	struct ibm_struct *ibm, *itmp;

	list_for_each_entry_safe(ibm, itmp,
				 &tpacpi_all_drivers,
				 all_drivers) {
		if (ibm->resume)
			(ibm->resume)();
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tpacpi_pm,
			 tpacpi_suspend_handler, tpacpi_resume_handler);

static void tpacpi_shutdown_handler(struct platform_device *pdev)
{
	struct ibm_struct *ibm, *itmp;

	list_for_each_entry_safe(ibm, itmp,
				 &tpacpi_all_drivers,
				 all_drivers) {
		if (ibm->shutdown)
			(ibm->shutdown)();
	}
}

static struct platform_driver tpacpi_pdriver = {
	.driver = {
		.name = TPACPI_DRVR_NAME,
		.owner = THIS_MODULE,
		.pm = &tpacpi_pm,
	},
	.shutdown = tpacpi_shutdown_handler,
};

static struct platform_driver tpacpi_hwmon_pdriver = {
	.driver = {
		.name = TPACPI_HWMON_DRVR_NAME,
		.owner = THIS_MODULE,
	},
};

/*************************************************************************
 * sysfs support helpers
 */

struct attribute_set {
	unsigned int members, max_members;
	struct attribute_group group;
};

struct attribute_set_obj {
	struct attribute_set s;
	struct attribute *a;
} __attribute__((packed));

static struct attribute_set *create_attr_set(unsigned int max_members,
						const char *name)
{
	struct attribute_set_obj *sobj;

	if (max_members == 0)
		return NULL;

	/* Allocates space for implicit NULL at the end too */
	sobj = kzalloc(sizeof(struct attribute_set_obj) +
		    max_members * sizeof(struct attribute *),
		    GFP_KERNEL);
	if (!sobj)
		return NULL;
	sobj->s.max_members = max_members;
	sobj->s.group.attrs = &sobj->a;
	sobj->s.group.name = name;

	return &sobj->s;
}

#define destroy_attr_set(_set) \
	kfree(_set);

/* not multi-threaded safe, use it in a single thread per set */
static int add_to_attr_set(struct attribute_set *s, struct attribute *attr)
{
	if (!s || !attr)
		return -EINVAL;

	if (s->members >= s->max_members)
		return -ENOMEM;

	s->group.attrs[s->members] = attr;
	s->members++;

	return 0;
}

static int add_many_to_attr_set(struct attribute_set *s,
			struct attribute **attr,
			unsigned int count)
{
	int i, res;

	for (i = 0; i < count; i++) {
		res = add_to_attr_set(s, attr[i]);
		if (res)
			return res;
	}

	return 0;
}

static void delete_attr_set(struct attribute_set *s, struct kobject *kobj)
{
	sysfs_remove_group(kobj, &s->group);
	destroy_attr_set(s);
}

#define register_attr_set_with_sysfs(_attr_set, _kobj) \
	sysfs_create_group(_kobj, &_attr_set->group)

static int parse_strtoul(const char *buf,
		unsigned long max, unsigned long *value)
{
	char *endp;

	*value = simple_strtoul(skip_spaces(buf), &endp, 0);
	endp = skip_spaces(endp);
	if (*endp || *value > max)
		return -EINVAL;

	return 0;
}

static void tpacpi_disable_brightness_delay(void)
{
	if (acpi_evalf(hkey_handle, NULL, "PWMS", "qvd", 0))
		pr_notice("ACPI backlight control delay disabled\n");
}

static void printk_deprecated_attribute(const char * const what,
					const char * const details)
{
	tpacpi_log_usertask("deprecated sysfs attribute");
	pr_warn("WARNING: sysfs attribute %s is deprecated and "
		"will be removed. %s\n",
		what, details);
}

/*************************************************************************
 * rfkill and radio control support helpers
 */

/*
 * ThinkPad-ACPI firmware handling model:
 *
 * WLSW (master wireless switch) is event-driven, and is common to all
 * firmware-controlled radios.  It cannot be controlled, just monitored,
 * as expected.  It overrides all radio state in firmware
 *
 * The kernel, a masked-off hotkey, and WLSW can change the radio state
 * (TODO: verify how WLSW interacts with the returned radio state).
 *
 * The only time there are shadow radio state changes, is when
 * masked-off hotkeys are used.
 */

/*
 * Internal driver API for radio state:
 *
 * int: < 0 = error, otherwise enum tpacpi_rfkill_state
 * bool: true means radio blocked (off)
 */
enum tpacpi_rfkill_state {
	TPACPI_RFK_RADIO_OFF = 0,
	TPACPI_RFK_RADIO_ON
};

/* rfkill switches */
enum tpacpi_rfk_id {
	TPACPI_RFK_BLUETOOTH_SW_ID = 0,
	TPACPI_RFK_WWAN_SW_ID,
	TPACPI_RFK_UWB_SW_ID,
	TPACPI_RFK_SW_MAX
};

static const char *tpacpi_rfkill_names[] = {
	[TPACPI_RFK_BLUETOOTH_SW_ID] = "bluetooth",
	[TPACPI_RFK_WWAN_SW_ID] = "wwan",
	[TPACPI_RFK_UWB_SW_ID] = "uwb",
	[TPACPI_RFK_SW_MAX] = NULL
};

/* ThinkPad-ACPI rfkill subdriver */
struct tpacpi_rfk {
	struct rfkill *rfkill;
	enum tpacpi_rfk_id id;
	const struct tpacpi_rfk_ops *ops;
};

struct tpacpi_rfk_ops {
	/* firmware interface */
	int (*get_status)(void);
	int (*set_status)(const enum tpacpi_rfkill_state);
};

static struct tpacpi_rfk *tpacpi_rfkill_switches[TPACPI_RFK_SW_MAX];

/* Query FW and update rfkill sw state for a given rfkill switch */
static int tpacpi_rfk_update_swstate(const struct tpacpi_rfk *tp_rfk)
{
	int status;

	if (!tp_rfk)
		return -ENODEV;

	status = (tp_rfk->ops->get_status)();
	if (status < 0)
		return status;

	rfkill_set_sw_state(tp_rfk->rfkill,
			    (status == TPACPI_RFK_RADIO_OFF));

	return status;
}

/* Query FW and update rfkill sw state for all rfkill switches */
static void tpacpi_rfk_update_swstate_all(void)
{
	unsigned int i;

	for (i = 0; i < TPACPI_RFK_SW_MAX; i++)
		tpacpi_rfk_update_swstate(tpacpi_rfkill_switches[i]);
}

/*
 * Sync the HW-blocking state of all rfkill switches,
 * do notice it causes the rfkill core to schedule uevents
 */
static void tpacpi_rfk_update_hwblock_state(bool blocked)
{
	unsigned int i;
	struct tpacpi_rfk *tp_rfk;

	for (i = 0; i < TPACPI_RFK_SW_MAX; i++) {
		tp_rfk = tpacpi_rfkill_switches[i];
		if (tp_rfk) {
			if (rfkill_set_hw_state(tp_rfk->rfkill,
						blocked)) {
				/* ignore -- we track sw block */
			}
		}
	}
}

/* Call to get the WLSW state from the firmware */
static int hotkey_get_wlsw(void);

/* Call to query WLSW state and update all rfkill switches */
static bool tpacpi_rfk_check_hwblock_state(void)
{
	int res = hotkey_get_wlsw();
	int hw_blocked;

	/* When unknown or unsupported, we have to assume it is unblocked */
	if (res < 0)
		return false;

	hw_blocked = (res == TPACPI_RFK_RADIO_OFF);
	tpacpi_rfk_update_hwblock_state(hw_blocked);

	return hw_blocked;
}

static int tpacpi_rfk_hook_set_block(void *data, bool blocked)
{
	struct tpacpi_rfk *tp_rfk = data;
	int res;

	dbg_printk(TPACPI_DBG_RFKILL,
		   "request to change radio state to %s\n",
		   blocked ? "blocked" : "unblocked");

	/* try to set radio state */
	res = (tp_rfk->ops->set_status)(blocked ?
				TPACPI_RFK_RADIO_OFF : TPACPI_RFK_RADIO_ON);

	/* and update the rfkill core with whatever the FW really did */
	tpacpi_rfk_update_swstate(tp_rfk);

	return (res < 0) ? res : 0;
}

static const struct rfkill_ops tpacpi_rfk_rfkill_ops = {
	.set_block = tpacpi_rfk_hook_set_block,
};

static int __init tpacpi_new_rfkill(const enum tpacpi_rfk_id id,
			const struct tpacpi_rfk_ops *tp_rfkops,
			const enum rfkill_type rfktype,
			const char *name,
			const bool set_default)
{
	struct tpacpi_rfk *atp_rfk;
	int res;
	bool sw_state = false;
	bool hw_state;
	int sw_status;

	BUG_ON(id >= TPACPI_RFK_SW_MAX || tpacpi_rfkill_switches[id]);

	atp_rfk = kzalloc(sizeof(struct tpacpi_rfk), GFP_KERNEL);
	if (atp_rfk)
		atp_rfk->rfkill = rfkill_alloc(name,
						&tpacpi_pdev->dev,
						rfktype,
						&tpacpi_rfk_rfkill_ops,
						atp_rfk);
	if (!atp_rfk || !atp_rfk->rfkill) {
		pr_err("failed to allocate memory for rfkill class\n");
		kfree(atp_rfk);
		return -ENOMEM;
	}

	atp_rfk->id = id;
	atp_rfk->ops = tp_rfkops;

	sw_status = (tp_rfkops->get_status)();
	if (sw_status < 0) {
		pr_err("failed to read initial state for %s, error %d\n",
		       name, sw_status);
	} else {
		sw_state = (sw_status == TPACPI_RFK_RADIO_OFF);
		if (set_default) {
			/* try to keep the initial state, since we ask the
			 * firmware to preserve it across S5 in NVRAM */
			rfkill_init_sw_state(atp_rfk->rfkill, sw_state);
		}
	}
	hw_state = tpacpi_rfk_check_hwblock_state();
	rfkill_set_hw_state(atp_rfk->rfkill, hw_state);

	res = rfkill_register(atp_rfk->rfkill);
	if (res < 0) {
		pr_err("failed to register %s rfkill switch: %d\n", name, res);
		rfkill_destroy(atp_rfk->rfkill);
		kfree(atp_rfk);
		return res;
	}

	tpacpi_rfkill_switches[id] = atp_rfk;

	pr_info("rfkill switch %s: radio is %sblocked\n",
		name, (sw_state || hw_state) ? "" : "un");
	return 0;
}

static void tpacpi_destroy_rfkill(const enum tpacpi_rfk_id id)
{
	struct tpacpi_rfk *tp_rfk;

	BUG_ON(id >= TPACPI_RFK_SW_MAX);

	tp_rfk = tpacpi_rfkill_switches[id];
	if (tp_rfk) {
		rfkill_unregister(tp_rfk->rfkill);
		rfkill_destroy(tp_rfk->rfkill);
		tpacpi_rfkill_switches[id] = NULL;
		kfree(tp_rfk);
	}
}

static void printk_deprecated_rfkill_attribute(const char * const what)
{
	printk_deprecated_attribute(what,
			"Please switch to generic rfkill before year 2010");
}

/* sysfs <radio> enable ------------------------------------------------ */
static ssize_t tpacpi_rfk_sysfs_enable_show(const enum tpacpi_rfk_id id,
					    struct device_attribute *attr,
					    char *buf)
{
	int status;

	printk_deprecated_rfkill_attribute(attr->attr.name);

	/* This is in the ABI... */
	if (tpacpi_rfk_check_hwblock_state()) {
		status = TPACPI_RFK_RADIO_OFF;
	} else {
		status = tpacpi_rfk_update_swstate(tpacpi_rfkill_switches[id]);
		if (status < 0)
			return status;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n",
			(status == TPACPI_RFK_RADIO_ON) ? 1 : 0);
}

static ssize_t tpacpi_rfk_sysfs_enable_store(const enum tpacpi_rfk_id id,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned long t;
	int res;

	printk_deprecated_rfkill_attribute(attr->attr.name);

	if (parse_strtoul(buf, 1, &t))
		return -EINVAL;

	tpacpi_disclose_usertask(attr->attr.name, "set to %ld\n", t);

	/* This is in the ABI... */
	if (tpacpi_rfk_check_hwblock_state() && !!t)
		return -EPERM;

	res = tpacpi_rfkill_switches[id]->ops->set_status((!!t) ?
				TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF);
	tpacpi_rfk_update_swstate(tpacpi_rfkill_switches[id]);

	return (res < 0) ? res : count;
}

/* procfs -------------------------------------------------------------- */
static int tpacpi_rfk_procfs_read(const enum tpacpi_rfk_id id, struct seq_file *m)
{
	if (id >= TPACPI_RFK_SW_MAX)
		seq_printf(m, "status:\t\tnot supported\n");
	else {
		int status;

		/* This is in the ABI... */
		if (tpacpi_rfk_check_hwblock_state()) {
			status = TPACPI_RFK_RADIO_OFF;
		} else {
			status = tpacpi_rfk_update_swstate(
						tpacpi_rfkill_switches[id]);
			if (status < 0)
				return status;
		}

		seq_printf(m, "status:\t\t%s\n",
				(status == TPACPI_RFK_RADIO_ON) ?
					"enabled" : "disabled");
		seq_printf(m, "commands:\tenable, disable\n");
	}

	return 0;
}

static int tpacpi_rfk_procfs_write(const enum tpacpi_rfk_id id, char *buf)
{
	char *cmd;
	int status = -1;
	int res = 0;

	if (id >= TPACPI_RFK_SW_MAX)
		return -ENODEV;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "enable") == 0)
			status = TPACPI_RFK_RADIO_ON;
		else if (strlencmp(cmd, "disable") == 0)
			status = TPACPI_RFK_RADIO_OFF;
		else
			return -EINVAL;
	}

	if (status != -1) {
		tpacpi_disclose_usertask("procfs", "attempt to %s %s\n",
				(status == TPACPI_RFK_RADIO_ON) ?
						"enable" : "disable",
				tpacpi_rfkill_names[id]);
		res = (tpacpi_rfkill_switches[id]->ops->set_status)(status);
		tpacpi_rfk_update_swstate(tpacpi_rfkill_switches[id]);
	}

	return res;
}

/*************************************************************************
 * thinkpad-acpi driver attributes
 */

/* interface_version --------------------------------------------------- */
static ssize_t tpacpi_driver_interface_version_show(
				struct device_driver *drv,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%08x\n", TPACPI_SYSFS_VERSION);
}

static DRIVER_ATTR(interface_version, S_IRUGO,
		tpacpi_driver_interface_version_show, NULL);

/* debug_level --------------------------------------------------------- */
static ssize_t tpacpi_driver_debug_show(struct device_driver *drv,
						char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%04x\n", dbg_level);
}

static ssize_t tpacpi_driver_debug_store(struct device_driver *drv,
						const char *buf, size_t count)
{
	unsigned long t;

	if (parse_strtoul(buf, 0xffff, &t))
		return -EINVAL;

	dbg_level = t;

	return count;
}

static DRIVER_ATTR(debug_level, S_IWUSR | S_IRUGO,
		tpacpi_driver_debug_show, tpacpi_driver_debug_store);

/* version ------------------------------------------------------------- */
static ssize_t tpacpi_driver_version_show(struct device_driver *drv,
						char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s v%s\n",
			TPACPI_DESC, TPACPI_VERSION);
}

static DRIVER_ATTR(version, S_IRUGO,
		tpacpi_driver_version_show, NULL);

/* --------------------------------------------------------------------- */

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES

/* wlsw_emulstate ------------------------------------------------------ */
static ssize_t tpacpi_driver_wlsw_emulstate_show(struct device_driver *drv,
						char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", !!tpacpi_wlsw_emulstate);
}

static ssize_t tpacpi_driver_wlsw_emulstate_store(struct device_driver *drv,
						const char *buf, size_t count)
{
	unsigned long t;

	if (parse_strtoul(buf, 1, &t))
		return -EINVAL;

	if (tpacpi_wlsw_emulstate != !!t) {
		tpacpi_wlsw_emulstate = !!t;
		tpacpi_rfk_update_hwblock_state(!t);	/* negative logic */
	}

	return count;
}

static DRIVER_ATTR(wlsw_emulstate, S_IWUSR | S_IRUGO,
		tpacpi_driver_wlsw_emulstate_show,
		tpacpi_driver_wlsw_emulstate_store);

/* bluetooth_emulstate ------------------------------------------------- */
static ssize_t tpacpi_driver_bluetooth_emulstate_show(
					struct device_driver *drv,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", !!tpacpi_bluetooth_emulstate);
}

static ssize_t tpacpi_driver_bluetooth_emulstate_store(
					struct device_driver *drv,
					const char *buf, size_t count)
{
	unsigned long t;

	if (parse_strtoul(buf, 1, &t))
		return -EINVAL;

	tpacpi_bluetooth_emulstate = !!t;

	return count;
}

static DRIVER_ATTR(bluetooth_emulstate, S_IWUSR | S_IRUGO,
		tpacpi_driver_bluetooth_emulstate_show,
		tpacpi_driver_bluetooth_emulstate_store);

/* wwan_emulstate ------------------------------------------------- */
static ssize_t tpacpi_driver_wwan_emulstate_show(
					struct device_driver *drv,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", !!tpacpi_wwan_emulstate);
}

static ssize_t tpacpi_driver_wwan_emulstate_store(
					struct device_driver *drv,
					const char *buf, size_t count)
{
	unsigned long t;

	if (parse_strtoul(buf, 1, &t))
		return -EINVAL;

	tpacpi_wwan_emulstate = !!t;

	return count;
}

static DRIVER_ATTR(wwan_emulstate, S_IWUSR | S_IRUGO,
		tpacpi_driver_wwan_emulstate_show,
		tpacpi_driver_wwan_emulstate_store);

/* uwb_emulstate ------------------------------------------------- */
static ssize_t tpacpi_driver_uwb_emulstate_show(
					struct device_driver *drv,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", !!tpacpi_uwb_emulstate);
}

static ssize_t tpacpi_driver_uwb_emulstate_store(
					struct device_driver *drv,
					const char *buf, size_t count)
{
	unsigned long t;

	if (parse_strtoul(buf, 1, &t))
		return -EINVAL;

	tpacpi_uwb_emulstate = !!t;

	return count;
}

static DRIVER_ATTR(uwb_emulstate, S_IWUSR | S_IRUGO,
		tpacpi_driver_uwb_emulstate_show,
		tpacpi_driver_uwb_emulstate_store);
#endif

/* --------------------------------------------------------------------- */

static struct driver_attribute *tpacpi_driver_attributes[] = {
	&driver_attr_debug_level, &driver_attr_version,
	&driver_attr_interface_version,
};

static int __init tpacpi_create_driver_attributes(struct device_driver *drv)
{
	int i, res;

	i = 0;
	res = 0;
	while (!res && i < ARRAY_SIZE(tpacpi_driver_attributes)) {
		res = driver_create_file(drv, tpacpi_driver_attributes[i]);
		i++;
	}

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (!res && dbg_wlswemul)
		res = driver_create_file(drv, &driver_attr_wlsw_emulstate);
	if (!res && dbg_bluetoothemul)
		res = driver_create_file(drv, &driver_attr_bluetooth_emulstate);
	if (!res && dbg_wwanemul)
		res = driver_create_file(drv, &driver_attr_wwan_emulstate);
	if (!res && dbg_uwbemul)
		res = driver_create_file(drv, &driver_attr_uwb_emulstate);
#endif

	return res;
}

static void tpacpi_remove_driver_attributes(struct device_driver *drv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tpacpi_driver_attributes); i++)
		driver_remove_file(drv, tpacpi_driver_attributes[i]);

#ifdef THINKPAD_ACPI_DEBUGFACILITIES
	driver_remove_file(drv, &driver_attr_wlsw_emulstate);
	driver_remove_file(drv, &driver_attr_bluetooth_emulstate);
	driver_remove_file(drv, &driver_attr_wwan_emulstate);
	driver_remove_file(drv, &driver_attr_uwb_emulstate);
#endif
}

/*************************************************************************
 * Firmware Data
 */

/*
 * Table of recommended minimum BIOS versions
 *
 * Reasons for listing:
 *    1. Stable BIOS, listed because the unknown amount of
 *       bugs and bad ACPI behaviour on older versions
 *
 *    2. BIOS or EC fw with known bugs that trigger on Linux
 *
 *    3. BIOS with known reduced functionality in older versions
 *
 *  We recommend the latest BIOS and EC version.
 *  We only support the latest BIOS and EC fw version as a rule.
 *
 *  Sources: IBM ThinkPad Public Web Documents (update changelogs),
 *  Information from users in ThinkWiki
 *
 *  WARNING: we use this table also to detect that the machine is
 *  a ThinkPad in some cases, so don't remove entries lightly.
 */

#define TPV_Q(__v, __id1, __id2, __bv1, __bv2)		\
	{ .vendor	= (__v),			\
	  .bios		= TPID(__id1, __id2),		\
	  .ec		= TPACPI_MATCH_ANY,		\
	  .quirks	= TPACPI_MATCH_ANY << 16	\
			  | (__bv1) << 8 | (__bv2) }

#define TPV_Q_X(__v, __bid1, __bid2, __bv1, __bv2,	\
		__eid, __ev1, __ev2)			\
	{ .vendor	= (__v),			\
	  .bios		= TPID(__bid1, __bid2),		\
	  .ec		= __eid,			\
	  .quirks	= (__ev1) << 24 | (__ev2) << 16 \
			  | (__bv1) << 8 | (__bv2) }

#define TPV_QI0(__id1, __id2, __bv1, __bv2) \
	TPV_Q(PCI_VENDOR_ID_IBM, __id1, __id2, __bv1, __bv2)

/* Outdated IBM BIOSes often lack the EC id string */
#define TPV_QI1(__id1, __id2, __bv1, __bv2, __ev1, __ev2) \
	TPV_Q_X(PCI_VENDOR_ID_IBM, __id1, __id2, 	\
		__bv1, __bv2, TPID(__id1, __id2),	\
		__ev1, __ev2),				\
	TPV_Q_X(PCI_VENDOR_ID_IBM, __id1, __id2, 	\
		__bv1, __bv2, TPACPI_MATCH_UNKNOWN,	\
		__ev1, __ev2)

/* Outdated IBM BIOSes often lack the EC id string */
#define TPV_QI2(__bid1, __bid2, __bv1, __bv2,		\
		__eid1, __eid2, __ev1, __ev2) 		\
	TPV_Q_X(PCI_VENDOR_ID_IBM, __bid1, __bid2, 	\
		__bv1, __bv2, TPID(__eid1, __eid2),	\
		__ev1, __ev2),				\
	TPV_Q_X(PCI_VENDOR_ID_IBM, __bid1, __bid2, 	\
		__bv1, __bv2, TPACPI_MATCH_UNKNOWN,	\
		__ev1, __ev2)

#define TPV_QL0(__id1, __id2, __bv1, __bv2) \
	TPV_Q(PCI_VENDOR_ID_LENOVO, __id1, __id2, __bv1, __bv2)

#define TPV_QL1(__id1, __id2, __bv1, __bv2, __ev1, __ev2) \
	TPV_Q_X(PCI_VENDOR_ID_LENOVO, __id1, __id2, 	\
		__bv1, __bv2, TPID(__id1, __id2),	\
		__ev1, __ev2)

#define TPV_QL2(__bid1, __bid2, __bv1, __bv2,		\
		__eid1, __eid2, __ev1, __ev2) 		\
	TPV_Q_X(PCI_VENDOR_ID_LENOVO, __bid1, __bid2, 	\
		__bv1, __bv2, TPID(__eid1, __eid2),	\
		__ev1, __ev2)

static const struct tpacpi_quirk tpacpi_bios_version_qtable[] __initconst = {
	/*  Numeric models ------------------ */
	/*      FW MODEL   BIOS VERS	      */
	TPV_QI0('I', 'M',  '6', '5'),		 /* 570 */
	TPV_QI0('I', 'U',  '2', '6'),		 /* 570E */
	TPV_QI0('I', 'B',  '5', '4'),		 /* 600 */
	TPV_QI0('I', 'H',  '4', '7'),		 /* 600E */
	TPV_QI0('I', 'N',  '3', '6'),		 /* 600E */
	TPV_QI0('I', 'T',  '5', '5'),		 /* 600X */
	TPV_QI0('I', 'D',  '4', '8'),		 /* 770, 770E, 770ED */
	TPV_QI0('I', 'I',  '4', '2'),		 /* 770X */
	TPV_QI0('I', 'O',  '2', '3'),		 /* 770Z */

	/* A-series ------------------------- */
	/*      FW MODEL   BIOS VERS  EC VERS */
	TPV_QI0('I', 'W',  '5', '9'),		 /* A20m */
	TPV_QI0('I', 'V',  '6', '9'),		 /* A20p */
	TPV_QI0('1', '0',  '2', '6'),		 /* A21e, A22e */
	TPV_QI0('K', 'U',  '3', '6'),		 /* A21e */
	TPV_QI0('K', 'X',  '3', '6'),		 /* A21m, A22m */
	TPV_QI0('K', 'Y',  '3', '8'),		 /* A21p, A22p */
	TPV_QI0('1', 'B',  '1', '7'),		 /* A22e */
	TPV_QI0('1', '3',  '2', '0'),		 /* A22m */
	TPV_QI0('1', 'E',  '7', '3'),		 /* A30/p (0) */
	TPV_QI1('1', 'G',  '4', '1',  '1', '7'), /* A31/p (0) */
	TPV_QI1('1', 'N',  '1', '6',  '0', '7'), /* A31/p (0) */

	/* G-series ------------------------- */
	/*      FW MODEL   BIOS VERS	      */
	TPV_QI0('1', 'T',  'A', '6'),		 /* G40 */
	TPV_QI0('1', 'X',  '5', '7'),		 /* G41 */

	/* R-series, T-series --------------- */
	/*      FW MODEL   BIOS VERS  EC VERS */
	TPV_QI0('1', 'C',  'F', '0'),		 /* R30 */
	TPV_QI0('1', 'F',  'F', '1'),		 /* R31 */
	TPV_QI0('1', 'M',  '9', '7'),		 /* R32 */
	TPV_QI0('1', 'O',  '6', '1'),		 /* R40 */
	TPV_QI0('1', 'P',  '6', '5'),		 /* R40 */
	TPV_QI0('1', 'S',  '7', '0'),		 /* R40e */
	TPV_QI1('1', 'R',  'D', 'R',  '7', '1'), /* R50/p, R51,
						    T40/p, T41/p, T42/p (1) */
	TPV_QI1('1', 'V',  '7', '1',  '2', '8'), /* R50e, R51 (1) */
	TPV_QI1('7', '8',  '7', '1',  '0', '6'), /* R51e (1) */
	TPV_QI1('7', '6',  '6', '9',  '1', '6'), /* R52 (1) */
	TPV_QI1('7', '0',  '6', '9',  '2', '8'), /* R52, T43 (1) */

	TPV_QI0('I', 'Y',  '6', '1'),		 /* T20 */
	TPV_QI0('K', 'Z',  '3', '4'),		 /* T21 */
	TPV_QI0('1', '6',  '3', '2'),		 /* T22 */
	TPV_QI1('1', 'A',  '6', '4',  '2', '3'), /* T23 (0) */
	TPV_QI1('1', 'I',  '7', '1',  '2', '0'), /* T30 (0) */
	TPV_QI1('1', 'Y',  '6', '5',  '2', '9'), /* T43/p (1) */

	TPV_QL1('7', '9',  'E', '3',  '5', '0'), /* T60/p */
	TPV_QL1('7', 'C',  'D', '2',  '2', '2'), /* R60, R60i */
	TPV_QL1('7', 'E',  'D', '0',  '1', '5'), /* R60e, R60i */

	/*      BIOS FW    BIOS VERS  EC FW     EC VERS */
	TPV_QI2('1', 'W',  '9', '0',  '1', 'V', '2', '8'), /* R50e (1) */
	TPV_QL2('7', 'I',  '3', '4',  '7', '9', '5', '0'), /* T60/p wide */

	/* X-series ------------------------- */
	/*      FW MODEL   BIOS VERS  EC VERS */
	TPV_QI0('I', 'Z',  '9', 'D'),		 /* X20, X21 */
	TPV_QI0('1', 'D',  '7', '0'),		 /* X22, X23, X24 */
	TPV_QI1('1', 'K',  '4', '8',  '1', '8'), /* X30 (0) */
	TPV_QI1('1', 'Q',  '9', '7',  '2', '3'), /* X31, X32 (0) */
	TPV_QI1('1', 'U',  'D', '3',  'B', '2'), /* X40 (0) */
	TPV_QI1('7', '4',  '6', '4',  '2', '7'), /* X41 (0) */
	TPV_QI1('7', '5',  '6', '0',  '2', '0'), /* X41t (0) */

	TPV_QL1('7', 'B',  'D', '7',  '4', '0'), /* X60/s */
	TPV_QL1('7', 'J',  '3', '0',  '1', '3'), /* X60t */

	/* (0) - older versions lack DMI EC fw string and functionality */
	/* (1) - older versions known to lack functionality */
};

#undef TPV_QL1
#undef TPV_QL0
#undef TPV_QI2
#undef TPV_QI1
#undef TPV_QI0
#undef TPV_Q_X
#undef TPV_Q

static void __init tpacpi_check_outdated_fw(void)
{
	unsigned long fwvers;
	u16 ec_version, bios_version;

	fwvers = tpacpi_check_quirks(tpacpi_bios_version_qtable,
				ARRAY_SIZE(tpacpi_bios_version_qtable));

	if (!fwvers)
		return;

	bios_version = fwvers & 0xffffU;
	ec_version = (fwvers >> 16) & 0xffffU;

	/* note that unknown versions are set to 0x0000 and we use that */
	if ((bios_version > thinkpad_id.bios_release) ||
	    (ec_version > thinkpad_id.ec_release &&
				ec_version != TPACPI_MATCH_ANY)) {
		/*
		 * The changelogs would let us track down the exact
		 * reason, but it is just too much of a pain to track
		 * it.  We only list BIOSes that are either really
		 * broken, or really stable to begin with, so it is
		 * best if the user upgrades the firmware anyway.
		 */
		pr_warn("WARNING: Outdated ThinkPad BIOS/EC firmware\n");
		pr_warn("WARNING: This firmware may be missing critical bug "
			"fixes and/or important features\n");
	}
}

static bool __init tpacpi_is_fw_known(void)
{
	return tpacpi_check_quirks(tpacpi_bios_version_qtable,
			ARRAY_SIZE(tpacpi_bios_version_qtable)) != 0;
}

/****************************************************************************
 ****************************************************************************
 *
 * Subdrivers
 *
 ****************************************************************************
 ****************************************************************************/

/*************************************************************************
 * thinkpad-acpi metadata subdriver
 */

static int thinkpad_acpi_driver_read(struct seq_file *m)
{
	seq_printf(m, "driver:\t\t%s\n", TPACPI_DESC);
	seq_printf(m, "version:\t%s\n", TPACPI_VERSION);
	return 0;
}

static struct ibm_struct thinkpad_acpi_driver_data = {
	.name = "driver",
	.read = thinkpad_acpi_driver_read,
};

/*************************************************************************
 * Hotkey subdriver
 */

/*
 * ThinkPad firmware event model
 *
 * The ThinkPad firmware has two main event interfaces: normal ACPI
 * notifications (which follow the ACPI standard), and a private event
 * interface.
 *
 * The private event interface also issues events for the hotkeys.  As
 * the driver gained features, the event handling code ended up being
 * built around the hotkey subdriver.  This will need to be refactored
 * to a more formal event API eventually.
 *
 * Some "hotkeys" are actually supposed to be used as event reports,
 * such as "brightness has changed", "volume has changed", depending on
 * the ThinkPad model and how the firmware is operating.
 *
 * Unlike other classes, hotkey-class events have mask/unmask control on
 * non-ancient firmware.  However, how it behaves changes a lot with the
 * firmware model and version.
 */

enum {	/* hot key scan codes (derived from ACPI DSDT) */
	TP_ACPI_HOTKEYSCAN_FNF1		= 0,
	TP_ACPI_HOTKEYSCAN_FNF2,
	TP_ACPI_HOTKEYSCAN_FNF3,
	TP_ACPI_HOTKEYSCAN_FNF4,
	TP_ACPI_HOTKEYSCAN_FNF5,
	TP_ACPI_HOTKEYSCAN_FNF6,
	TP_ACPI_HOTKEYSCAN_FNF7,
	TP_ACPI_HOTKEYSCAN_FNF8,
	TP_ACPI_HOTKEYSCAN_FNF9,
	TP_ACPI_HOTKEYSCAN_FNF10,
	TP_ACPI_HOTKEYSCAN_FNF11,
	TP_ACPI_HOTKEYSCAN_FNF12,
	TP_ACPI_HOTKEYSCAN_FNBACKSPACE,
	TP_ACPI_HOTKEYSCAN_FNINSERT,
	TP_ACPI_HOTKEYSCAN_FNDELETE,
	TP_ACPI_HOTKEYSCAN_FNHOME,
	TP_ACPI_HOTKEYSCAN_FNEND,
	TP_ACPI_HOTKEYSCAN_FNPAGEUP,
	TP_ACPI_HOTKEYSCAN_FNPAGEDOWN,
	TP_ACPI_HOTKEYSCAN_FNSPACE,
	TP_ACPI_HOTKEYSCAN_VOLUMEUP,
	TP_ACPI_HOTKEYSCAN_VOLUMEDOWN,
	TP_ACPI_HOTKEYSCAN_MUTE,
	TP_ACPI_HOTKEYSCAN_THINKPAD,
	TP_ACPI_HOTKEYSCAN_UNK1,
	TP_ACPI_HOTKEYSCAN_UNK2,
	TP_ACPI_HOTKEYSCAN_UNK3,
	TP_ACPI_HOTKEYSCAN_UNK4,
	TP_ACPI_HOTKEYSCAN_UNK5,
	TP_ACPI_HOTKEYSCAN_UNK6,
	TP_ACPI_HOTKEYSCAN_UNK7,
	TP_ACPI_HOTKEYSCAN_UNK8,

	/* Hotkey keymap size */
	TPACPI_HOTKEY_MAP_LEN
};

enum {	/* Keys/events available through NVRAM polling */
	TPACPI_HKEY_NVRAM_KNOWN_MASK = 0x00fb88c0U,
	TPACPI_HKEY_NVRAM_GOOD_MASK  = 0x00fb8000U,
};

enum {	/* Positions of some of the keys in hotkey masks */
	TP_ACPI_HKEY_DISPSWTCH_MASK	= 1 << TP_ACPI_HOTKEYSCAN_FNF7,
	TP_ACPI_HKEY_DISPXPAND_MASK	= 1 << TP_ACPI_HOTKEYSCAN_FNF8,
	TP_ACPI_HKEY_HIBERNATE_MASK	= 1 << TP_ACPI_HOTKEYSCAN_FNF12,
	TP_ACPI_HKEY_BRGHTUP_MASK	= 1 << TP_ACPI_HOTKEYSCAN_FNHOME,
	TP_ACPI_HKEY_BRGHTDWN_MASK	= 1 << TP_ACPI_HOTKEYSCAN_FNEND,
	TP_ACPI_HKEY_THNKLGHT_MASK	= 1 << TP_ACPI_HOTKEYSCAN_FNPAGEUP,
	TP_ACPI_HKEY_ZOOM_MASK		= 1 << TP_ACPI_HOTKEYSCAN_FNSPACE,
	TP_ACPI_HKEY_VOLUP_MASK		= 1 << TP_ACPI_HOTKEYSCAN_VOLUMEUP,
	TP_ACPI_HKEY_VOLDWN_MASK	= 1 << TP_ACPI_HOTKEYSCAN_VOLUMEDOWN,
	TP_ACPI_HKEY_MUTE_MASK		= 1 << TP_ACPI_HOTKEYSCAN_MUTE,
	TP_ACPI_HKEY_THINKPAD_MASK	= 1 << TP_ACPI_HOTKEYSCAN_THINKPAD,
};

enum {	/* NVRAM to ACPI HKEY group map */
	TP_NVRAM_HKEY_GROUP_HK2		= TP_ACPI_HKEY_THINKPAD_MASK |
					  TP_ACPI_HKEY_ZOOM_MASK |
					  TP_ACPI_HKEY_DISPSWTCH_MASK |
					  TP_ACPI_HKEY_HIBERNATE_MASK,
	TP_NVRAM_HKEY_GROUP_BRIGHTNESS	= TP_ACPI_HKEY_BRGHTUP_MASK |
					  TP_ACPI_HKEY_BRGHTDWN_MASK,
	TP_NVRAM_HKEY_GROUP_VOLUME	= TP_ACPI_HKEY_VOLUP_MASK |
					  TP_ACPI_HKEY_VOLDWN_MASK |
					  TP_ACPI_HKEY_MUTE_MASK,
};

#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL
struct tp_nvram_state {
       u16 thinkpad_toggle:1;
       u16 zoom_toggle:1;
       u16 display_toggle:1;
       u16 thinklight_toggle:1;
       u16 hibernate_toggle:1;
       u16 displayexp_toggle:1;
       u16 display_state:1;
       u16 brightness_toggle:1;
       u16 volume_toggle:1;
       u16 mute:1;

       u8 brightness_level;
       u8 volume_level;
};

/* kthread for the hotkey poller */
static struct task_struct *tpacpi_hotkey_task;

/*
 * Acquire mutex to write poller control variables as an
 * atomic block.
 *
 * Increment hotkey_config_change when changing them if you
 * want the kthread to forget old state.
 *
 * See HOTKEY_CONFIG_CRITICAL_START/HOTKEY_CONFIG_CRITICAL_END
 */
static struct mutex hotkey_thread_data_mutex;
static unsigned int hotkey_config_change;

/*
 * hotkey poller control variables
 *
 * Must be atomic or readers will also need to acquire mutex
 *
 * HOTKEY_CONFIG_CRITICAL_START/HOTKEY_CONFIG_CRITICAL_END
 * should be used only when the changes need to be taken as
 * a block, OR when one needs to force the kthread to forget
 * old state.
 */
static u32 hotkey_source_mask;		/* bit mask 0=ACPI,1=NVRAM */
static unsigned int hotkey_poll_freq = 10; /* Hz */

#define HOTKEY_CONFIG_CRITICAL_START \
	do { \
		mutex_lock(&hotkey_thread_data_mutex); \
		hotkey_config_change++; \
	} while (0);
#define HOTKEY_CONFIG_CRITICAL_END \
	mutex_unlock(&hotkey_thread_data_mutex);

#else /* CONFIG_THINKPAD_ACPI_HOTKEY_POLL */

#define hotkey_source_mask 0U
#define HOTKEY_CONFIG_CRITICAL_START
#define HOTKEY_CONFIG_CRITICAL_END

#endif /* CONFIG_THINKPAD_ACPI_HOTKEY_POLL */

static struct mutex hotkey_mutex;

static enum {	/* Reasons for waking up */
	TP_ACPI_WAKEUP_NONE = 0,	/* None or unknown */
	TP_ACPI_WAKEUP_BAYEJ,		/* Bay ejection request */
	TP_ACPI_WAKEUP_UNDOCK,		/* Undock request */
} hotkey_wakeup_reason;

static int hotkey_autosleep_ack;

static u32 hotkey_orig_mask;		/* events the BIOS had enabled */
static u32 hotkey_all_mask;		/* all events supported in fw */
static u32 hotkey_reserved_mask;	/* events better left disabled */
static u32 hotkey_driver_mask;		/* events needed by the driver */
static u32 hotkey_user_mask;		/* events visible to userspace */
static u32 hotkey_acpi_mask;		/* events enabled in firmware */

static u16 *hotkey_keycode_map;

static struct attribute_set *hotkey_dev_attributes;

static void tpacpi_driver_event(const unsigned int hkey_event);
static void hotkey_driver_event(const unsigned int scancode);
static void hotkey_poll_setup(const bool may_warn);

/* HKEY.MHKG() return bits */
#define TP_HOTKEY_TABLET_MASK (1 << 3)

static int hotkey_get_wlsw(void)
{
	int status;

	if (!tp_features.hotkey_wlsw)
		return -ENODEV;

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_wlswemul)
		return (tpacpi_wlsw_emulstate) ?
				TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF;
#endif

	if (!acpi_evalf(hkey_handle, &status, "WLSW", "d"))
		return -EIO;

	return (status) ? TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF;
}

static int hotkey_get_tablet_mode(int *status)
{
	int s;

	if (!acpi_evalf(hkey_handle, &s, "MHKG", "d"))
		return -EIO;

	*status = ((s & TP_HOTKEY_TABLET_MASK) != 0);
	return 0;
}

/*
 * Reads current event mask from firmware, and updates
 * hotkey_acpi_mask accordingly.  Also resets any bits
 * from hotkey_user_mask that are unavailable to be
 * delivered (shadow requirement of the userspace ABI).
 *
 * Call with hotkey_mutex held
 */
static int hotkey_mask_get(void)
{
	if (tp_features.hotkey_mask) {
		u32 m = 0;

		if (!acpi_evalf(hkey_handle, &m, "DHKN", "d"))
			return -EIO;

		hotkey_acpi_mask = m;
	} else {
		/* no mask support doesn't mean no event support... */
		hotkey_acpi_mask = hotkey_all_mask;
	}

	/* sync userspace-visible mask */
	hotkey_user_mask &= (hotkey_acpi_mask | hotkey_source_mask);

	return 0;
}

void static hotkey_mask_warn_incomplete_mask(void)
{
	/* log only what the user can fix... */
	const u32 wantedmask = hotkey_driver_mask &
		~(hotkey_acpi_mask | hotkey_source_mask) &
		(hotkey_all_mask | TPACPI_HKEY_NVRAM_KNOWN_MASK);

	if (wantedmask)
		pr_notice("required events 0x%08x not enabled!\n", wantedmask);
}

/*
 * Set the firmware mask when supported
 *
 * Also calls hotkey_mask_get to update hotkey_acpi_mask.
 *
 * NOTE: does not set bits in hotkey_user_mask, but may reset them.
 *
 * Call with hotkey_mutex held
 */
static int hotkey_mask_set(u32 mask)
{
	int i;
	int rc = 0;

	const u32 fwmask = mask & ~hotkey_source_mask;

	if (tp_features.hotkey_mask) {
		for (i = 0; i < 32; i++) {
			if (!acpi_evalf(hkey_handle,
					NULL, "MHKM", "vdd", i + 1,
					!!(mask & (1 << i)))) {
				rc = -EIO;
				break;
			}
		}
	}

	/*
	 * We *must* make an inconditional call to hotkey_mask_get to
	 * refresh hotkey_acpi_mask and update hotkey_user_mask
	 *
	 * Take the opportunity to also log when we cannot _enable_
	 * a given event.
	 */
	if (!hotkey_mask_get() && !rc && (fwmask & ~hotkey_acpi_mask)) {
		pr_notice("asked for hotkey mask 0x%08x, but "
			  "firmware forced it to 0x%08x\n",
			  fwmask, hotkey_acpi_mask);
	}

	if (tpacpi_lifecycle != TPACPI_LIFE_EXITING)
		hotkey_mask_warn_incomplete_mask();

	return rc;
}

/*
 * Sets hotkey_user_mask and tries to set the firmware mask
 *
 * Call with hotkey_mutex held
 */
static int hotkey_user_mask_set(const u32 mask)
{
	int rc;

	/* Give people a chance to notice they are doing something that
	 * is bound to go boom on their users sooner or later */
	if (!tp_warned.hotkey_mask_ff &&
	    (mask == 0xffff || mask == 0xffffff ||
	     mask == 0xffffffff)) {
		tp_warned.hotkey_mask_ff = 1;
		pr_notice("setting the hotkey mask to 0x%08x is likely "
			  "not the best way to go about it\n", mask);
		pr_notice("please consider using the driver defaults, "
			  "and refer to up-to-date thinkpad-acpi "
			  "documentation\n");
	}

	/* Try to enable what the user asked for, plus whatever we need.
	 * this syncs everything but won't enable bits in hotkey_user_mask */
	rc = hotkey_mask_set((mask | hotkey_driver_mask) & ~hotkey_source_mask);

	/* Enable the available bits in hotkey_user_mask */
	hotkey_user_mask = mask & (hotkey_acpi_mask | hotkey_source_mask);

	return rc;
}

/*
 * Sets the driver hotkey mask.
 *
 * Can be called even if the hotkey subdriver is inactive
 */
static int tpacpi_hotkey_driver_mask_set(const u32 mask)
{
	int rc;

	/* Do the right thing if hotkey_init has not been called yet */
	if (!tp_features.hotkey) {
		hotkey_driver_mask = mask;
		return 0;
	}

	mutex_lock(&hotkey_mutex);

	HOTKEY_CONFIG_CRITICAL_START
	hotkey_driver_mask = mask;
#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL
	hotkey_source_mask |= (mask & ~hotkey_all_mask);
#endif
	HOTKEY_CONFIG_CRITICAL_END

	rc = hotkey_mask_set((hotkey_acpi_mask | hotkey_driver_mask) &
							~hotkey_source_mask);
	hotkey_poll_setup(true);

	mutex_unlock(&hotkey_mutex);

	return rc;
}

static int hotkey_status_get(int *status)
{
	if (!acpi_evalf(hkey_handle, status, "DHKC", "d"))
		return -EIO;

	return 0;
}

static int hotkey_status_set(bool enable)
{
	if (!acpi_evalf(hkey_handle, NULL, "MHKC", "vd", enable ? 1 : 0))
		return -EIO;

	return 0;
}

static void tpacpi_input_send_tabletsw(void)
{
	int state;

	if (tp_features.hotkey_tablet &&
	    !hotkey_get_tablet_mode(&state)) {
		mutex_lock(&tpacpi_inputdev_send_mutex);

		input_report_switch(tpacpi_inputdev,
				    SW_TABLET_MODE, !!state);
		input_sync(tpacpi_inputdev);

		mutex_unlock(&tpacpi_inputdev_send_mutex);
	}
}

/* Do NOT call without validating scancode first */
static void tpacpi_input_send_key(const unsigned int scancode)
{
	const unsigned int keycode = hotkey_keycode_map[scancode];

	if (keycode != KEY_RESERVED) {
		mutex_lock(&tpacpi_inputdev_send_mutex);

		input_event(tpacpi_inputdev, EV_MSC, MSC_SCAN, scancode);
		input_report_key(tpacpi_inputdev, keycode, 1);
		input_sync(tpacpi_inputdev);

		input_event(tpacpi_inputdev, EV_MSC, MSC_SCAN, scancode);
		input_report_key(tpacpi_inputdev, keycode, 0);
		input_sync(tpacpi_inputdev);

		mutex_unlock(&tpacpi_inputdev_send_mutex);
	}
}

/* Do NOT call without validating scancode first */
static void tpacpi_input_send_key_masked(const unsigned int scancode)
{
	hotkey_driver_event(scancode);
	if (hotkey_user_mask & (1 << scancode))
		tpacpi_input_send_key(scancode);
}

#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL
static struct tp_acpi_drv_struct ibm_hotkey_acpidriver;

/* Do NOT call without validating scancode first */
static void tpacpi_hotkey_send_key(unsigned int scancode)
{
	tpacpi_input_send_key_masked(scancode);
}

static void hotkey_read_nvram(struct tp_nvram_state *n, const u32 m)
{
	u8 d;

	if (m & TP_NVRAM_HKEY_GROUP_HK2) {
		d = nvram_read_byte(TP_NVRAM_ADDR_HK2);
		n->thinkpad_toggle = !!(d & TP_NVRAM_MASK_HKT_THINKPAD);
		n->zoom_toggle = !!(d & TP_NVRAM_MASK_HKT_ZOOM);
		n->display_toggle = !!(d & TP_NVRAM_MASK_HKT_DISPLAY);
		n->hibernate_toggle = !!(d & TP_NVRAM_MASK_HKT_HIBERNATE);
	}
	if (m & TP_ACPI_HKEY_THNKLGHT_MASK) {
		d = nvram_read_byte(TP_NVRAM_ADDR_THINKLIGHT);
		n->thinklight_toggle = !!(d & TP_NVRAM_MASK_THINKLIGHT);
	}
	if (m & TP_ACPI_HKEY_DISPXPAND_MASK) {
		d = nvram_read_byte(TP_NVRAM_ADDR_VIDEO);
		n->displayexp_toggle =
				!!(d & TP_NVRAM_MASK_HKT_DISPEXPND);
	}
	if (m & TP_NVRAM_HKEY_GROUP_BRIGHTNESS) {
		d = nvram_read_byte(TP_NVRAM_ADDR_BRIGHTNESS);
		n->brightness_level = (d & TP_NVRAM_MASK_LEVEL_BRIGHTNESS)
				>> TP_NVRAM_POS_LEVEL_BRIGHTNESS;
		n->brightness_toggle =
				!!(d & TP_NVRAM_MASK_HKT_BRIGHTNESS);
	}
	if (m & TP_NVRAM_HKEY_GROUP_VOLUME) {
		d = nvram_read_byte(TP_NVRAM_ADDR_MIXER);
		n->volume_level = (d & TP_NVRAM_MASK_LEVEL_VOLUME)
				>> TP_NVRAM_POS_LEVEL_VOLUME;
		n->mute = !!(d & TP_NVRAM_MASK_MUTE);
		n->volume_toggle = !!(d & TP_NVRAM_MASK_HKT_VOLUME);
	}
}

static void hotkey_compare_and_issue_event(struct tp_nvram_state *oldn,
					   struct tp_nvram_state *newn,
					   const u32 event_mask)
{

#define TPACPI_COMPARE_KEY(__scancode, __member) \
	do { \
		if ((event_mask & (1 << __scancode)) && \
		    oldn->__member != newn->__member) \
			tpacpi_hotkey_send_key(__scancode); \
	} while (0)

#define TPACPI_MAY_SEND_KEY(__scancode) \
	do { \
		if (event_mask & (1 << __scancode)) \
			tpacpi_hotkey_send_key(__scancode); \
	} while (0)

	void issue_volchange(const unsigned int oldvol,
			     const unsigned int newvol)
	{
		unsigned int i = oldvol;

		while (i > newvol) {
			TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_VOLUMEDOWN);
			i--;
		}
		while (i < newvol) {
			TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_VOLUMEUP);
			i++;
		}
	}

	void issue_brightnesschange(const unsigned int oldbrt,
				    const unsigned int newbrt)
	{
		unsigned int i = oldbrt;

		while (i > newbrt) {
			TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_FNEND);
			i--;
		}
		while (i < newbrt) {
			TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_FNHOME);
			i++;
		}
	}

	TPACPI_COMPARE_KEY(TP_ACPI_HOTKEYSCAN_THINKPAD, thinkpad_toggle);
	TPACPI_COMPARE_KEY(TP_ACPI_HOTKEYSCAN_FNSPACE, zoom_toggle);
	TPACPI_COMPARE_KEY(TP_ACPI_HOTKEYSCAN_FNF7, display_toggle);
	TPACPI_COMPARE_KEY(TP_ACPI_HOTKEYSCAN_FNF12, hibernate_toggle);

	TPACPI_COMPARE_KEY(TP_ACPI_HOTKEYSCAN_FNPAGEUP, thinklight_toggle);

	TPACPI_COMPARE_KEY(TP_ACPI_HOTKEYSCAN_FNF8, displayexp_toggle);

	/*
	 * Handle volume
	 *
	 * This code is supposed to duplicate the IBM firmware behaviour:
	 * - Pressing MUTE issues mute hotkey message, even when already mute
	 * - Pressing Volume up/down issues volume up/down hotkey messages,
	 *   even when already at maximum or minimum volume
	 * - The act of unmuting issues volume up/down notification,
	 *   depending which key was used to unmute
	 *
	 * We are constrained to what the NVRAM can tell us, which is not much
	 * and certainly not enough if more than one volume hotkey was pressed
	 * since the last poll cycle.
	 *
	 * Just to make our life interesting, some newer Lenovo ThinkPads have
	 * bugs in the BIOS and may fail to update volume_toggle properly.
	 */
	if (newn->mute) {
		/* muted */
		if (!oldn->mute ||
		    oldn->volume_toggle != newn->volume_toggle ||
		    oldn->volume_level != newn->volume_level) {
			/* recently muted, or repeated mute keypress, or
			 * multiple presses ending in mute */
			issue_volchange(oldn->volume_level, newn->volume_level);
			TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_MUTE);
		}
	} else {
		/* unmute */
		if (oldn->mute) {
			/* recently unmuted, issue 'unmute' keypress */
			TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_VOLUMEUP);
		}
		if (oldn->volume_level != newn->volume_level) {
			issue_volchange(oldn->volume_level, newn->volume_level);
		} else if (oldn->volume_toggle != newn->volume_toggle) {
			/* repeated vol up/down keypress at end of scale ? */
			if (newn->volume_level == 0)
				TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_VOLUMEDOWN);
			else if (newn->volume_level >= TP_NVRAM_LEVEL_VOLUME_MAX)
				TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_VOLUMEUP);
		}
	}

	/* handle brightness */
	if (oldn->brightness_level != newn->brightness_level) {
		issue_brightnesschange(oldn->brightness_level,
				       newn->brightness_level);
	} else if (oldn->brightness_toggle != newn->brightness_toggle) {
		/* repeated key presses that didn't change state */
		if (newn->brightness_level == 0)
			TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_FNEND);
		else if (newn->brightness_level >= bright_maxlvl
				&& !tp_features.bright_unkfw)
			TPACPI_MAY_SEND_KEY(TP_ACPI_HOTKEYSCAN_FNHOME);
	}

#undef TPACPI_COMPARE_KEY
#undef TPACPI_MAY_SEND_KEY
}

/*
 * Polling driver
 *
 * We track all events in hotkey_source_mask all the time, since
 * most of them are edge-based.  We only issue those requested by
 * hotkey_user_mask or hotkey_driver_mask, though.
 */
static int hotkey_kthread(void *data)
{
	struct tp_nvram_state s[2];
	u32 poll_mask, event_mask;
	unsigned int si, so;
	unsigned long t;
	unsigned int change_detector;
	unsigned int poll_freq;
	bool was_frozen;

	if (tpacpi_lifecycle == TPACPI_LIFE_EXITING)
		goto exit;

	set_freezable();

	so = 0;
	si = 1;
	t = 0;

	/* Initial state for compares */
	mutex_lock(&hotkey_thread_data_mutex);
	change_detector = hotkey_config_change;
	poll_mask = hotkey_source_mask;
	event_mask = hotkey_source_mask &
			(hotkey_driver_mask | hotkey_user_mask);
	poll_freq = hotkey_poll_freq;
	mutex_unlock(&hotkey_thread_data_mutex);
	hotkey_read_nvram(&s[so], poll_mask);

	while (!kthread_should_stop()) {
		if (t == 0) {
			if (likely(poll_freq))
				t = 1000/poll_freq;
			else
				t = 100;	/* should never happen... */
		}
		t = msleep_interruptible(t);
		if (unlikely(kthread_freezable_should_stop(&was_frozen)))
			break;

		if (t > 0 && !was_frozen)
			continue;

		mutex_lock(&hotkey_thread_data_mutex);
		if (was_frozen || hotkey_config_change != change_detector) {
			/* forget old state on thaw or config change */
			si = so;
			t = 0;
			change_detector = hotkey_config_change;
		}
		poll_mask = hotkey_source_mask;
		event_mask = hotkey_source_mask &
				(hotkey_driver_mask | hotkey_user_mask);
		poll_freq = hotkey_poll_freq;
		mutex_unlock(&hotkey_thread_data_mutex);

		if (likely(poll_mask)) {
			hotkey_read_nvram(&s[si], poll_mask);
			if (likely(si != so)) {
				hotkey_compare_and_issue_event(&s[so], &s[si],
								event_mask);
			}
		}

		so = si;
		si ^= 1;
	}

exit:
	return 0;
}

/* call with hotkey_mutex held */
static void hotkey_poll_stop_sync(void)
{
	if (tpacpi_hotkey_task) {
		kthread_stop(tpacpi_hotkey_task);
		tpacpi_hotkey_task = NULL;
	}
}

/* call with hotkey_mutex held */
static void hotkey_poll_setup(const bool may_warn)
{
	const u32 poll_driver_mask = hotkey_driver_mask & hotkey_source_mask;
	const u32 poll_user_mask = hotkey_user_mask & hotkey_source_mask;

	if (hotkey_poll_freq > 0 &&
	    (poll_driver_mask ||
	     (poll_user_mask && tpacpi_inputdev->users > 0))) {
		if (!tpacpi_hotkey_task) {
			tpacpi_hotkey_task = kthread_run(hotkey_kthread,
					NULL, TPACPI_NVRAM_KTHREAD_NAME);
			if (IS_ERR(tpacpi_hotkey_task)) {
				tpacpi_hotkey_task = NULL;
				pr_err("could not create kernel thread "
				       "for hotkey polling\n");
			}
		}
	} else {
		hotkey_poll_stop_sync();
		if (may_warn && (poll_driver_mask || poll_user_mask) &&
		    hotkey_poll_freq == 0) {
			pr_notice("hot keys 0x%08x and/or events 0x%08x "
				  "require polling, which is currently "
				  "disabled\n",
				  poll_user_mask, poll_driver_mask);
		}
	}
}

static void hotkey_poll_setup_safe(const bool may_warn)
{
	mutex_lock(&hotkey_mutex);
	hotkey_poll_setup(may_warn);
	mutex_unlock(&hotkey_mutex);
}

/* call with hotkey_mutex held */
static void hotkey_poll_set_freq(unsigned int freq)
{
	if (!freq)
		hotkey_poll_stop_sync();

	hotkey_poll_freq = freq;
}

#else /* CONFIG_THINKPAD_ACPI_HOTKEY_POLL */

static void hotkey_poll_setup(const bool __unused)
{
}

static void hotkey_poll_setup_safe(const bool __unused)
{
}

#endif /* CONFIG_THINKPAD_ACPI_HOTKEY_POLL */

static int hotkey_inputdev_open(struct input_dev *dev)
{
	switch (tpacpi_lifecycle) {
	case TPACPI_LIFE_INIT:
	case TPACPI_LIFE_RUNNING:
		hotkey_poll_setup_safe(false);
		return 0;
	case TPACPI_LIFE_EXITING:
		return -EBUSY;
	}

	/* Should only happen if tpacpi_lifecycle is corrupt */
	BUG();
	return -EBUSY;
}

static void hotkey_inputdev_close(struct input_dev *dev)
{
	/* disable hotkey polling when possible */
	if (tpacpi_lifecycle != TPACPI_LIFE_EXITING &&
	    !(hotkey_source_mask & hotkey_driver_mask))
		hotkey_poll_setup_safe(false);
}

/* sysfs hotkey enable ------------------------------------------------- */
static ssize_t hotkey_enable_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int res, status;

	printk_deprecated_attribute("hotkey_enable",
			"Hotkey reporting is always enabled");

	res = hotkey_status_get(&status);
	if (res)
		return res;

	return snprintf(buf, PAGE_SIZE, "%d\n", status);
}

static ssize_t hotkey_enable_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned long t;

	printk_deprecated_attribute("hotkey_enable",
			"Hotkeys can be disabled through hotkey_mask");

	if (parse_strtoul(buf, 1, &t))
		return -EINVAL;

	if (t == 0)
		return -EPERM;

	return count;
}

static struct device_attribute dev_attr_hotkey_enable =
	__ATTR(hotkey_enable, S_IWUSR | S_IRUGO,
		hotkey_enable_show, hotkey_enable_store);

/* sysfs hotkey mask --------------------------------------------------- */
static ssize_t hotkey_mask_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%08x\n", hotkey_user_mask);
}

static ssize_t hotkey_mask_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned long t;
	int res;

	if (parse_strtoul(buf, 0xffffffffUL, &t))
		return -EINVAL;

	if (mutex_lock_killable(&hotkey_mutex))
		return -ERESTARTSYS;

	res = hotkey_user_mask_set(t);

#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL
	hotkey_poll_setup(true);
#endif

	mutex_unlock(&hotkey_mutex);

	tpacpi_disclose_usertask("hotkey_mask", "set to 0x%08lx\n", t);

	return (res) ? res : count;
}

static struct device_attribute dev_attr_hotkey_mask =
	__ATTR(hotkey_mask, S_IWUSR | S_IRUGO,
		hotkey_mask_show, hotkey_mask_store);

/* sysfs hotkey bios_enabled ------------------------------------------- */
static ssize_t hotkey_bios_enabled_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return sprintf(buf, "0\n");
}

static struct device_attribute dev_attr_hotkey_bios_enabled =
	__ATTR(hotkey_bios_enabled, S_IRUGO, hotkey_bios_enabled_show, NULL);

/* sysfs hotkey bios_mask ---------------------------------------------- */
static ssize_t hotkey_bios_mask_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	printk_deprecated_attribute("hotkey_bios_mask",
			"This attribute is useless.");
	return snprintf(buf, PAGE_SIZE, "0x%08x\n", hotkey_orig_mask);
}

static struct device_attribute dev_attr_hotkey_bios_mask =
	__ATTR(hotkey_bios_mask, S_IRUGO, hotkey_bios_mask_show, NULL);

/* sysfs hotkey all_mask ----------------------------------------------- */
static ssize_t hotkey_all_mask_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%08x\n",
				hotkey_all_mask | hotkey_source_mask);
}

static struct device_attribute dev_attr_hotkey_all_mask =
	__ATTR(hotkey_all_mask, S_IRUGO, hotkey_all_mask_show, NULL);

/* sysfs hotkey recommended_mask --------------------------------------- */
static ssize_t hotkey_recommended_mask_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%08x\n",
			(hotkey_all_mask | hotkey_source_mask)
			& ~hotkey_reserved_mask);
}

static struct device_attribute dev_attr_hotkey_recommended_mask =
	__ATTR(hotkey_recommended_mask, S_IRUGO,
		hotkey_recommended_mask_show, NULL);

#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL

/* sysfs hotkey hotkey_source_mask ------------------------------------- */
static ssize_t hotkey_source_mask_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%08x\n", hotkey_source_mask);
}

static ssize_t hotkey_source_mask_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned long t;
	u32 r_ev;
	int rc;

	if (parse_strtoul(buf, 0xffffffffUL, &t) ||
		((t & ~TPACPI_HKEY_NVRAM_KNOWN_MASK) != 0))
		return -EINVAL;

	if (mutex_lock_killable(&hotkey_mutex))
		return -ERESTARTSYS;

	HOTKEY_CONFIG_CRITICAL_START
	hotkey_source_mask = t;
	HOTKEY_CONFIG_CRITICAL_END

	rc = hotkey_mask_set((hotkey_user_mask | hotkey_driver_mask) &
			~hotkey_source_mask);
	hotkey_poll_setup(true);

	/* check if events needed by the driver got disabled */
	r_ev = hotkey_driver_mask & ~(hotkey_acpi_mask & hotkey_all_mask)
		& ~hotkey_source_mask & TPACPI_HKEY_NVRAM_KNOWN_MASK;

	mutex_unlock(&hotkey_mutex);

	if (rc < 0)
		pr_err("hotkey_source_mask: "
		       "failed to update the firmware event mask!\n");

	if (r_ev)
		pr_notice("hotkey_source_mask: "
			  "some important events were disabled: 0x%04x\n",
			  r_ev);

	tpacpi_disclose_usertask("hotkey_source_mask", "set to 0x%08lx\n", t);

	return (rc < 0) ? rc : count;
}

static struct device_attribute dev_attr_hotkey_source_mask =
	__ATTR(hotkey_source_mask, S_IWUSR | S_IRUGO,
		hotkey_source_mask_show, hotkey_source_mask_store);

/* sysfs hotkey hotkey_poll_freq --------------------------------------- */
static ssize_t hotkey_poll_freq_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hotkey_poll_freq);
}

static ssize_t hotkey_poll_freq_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned long t;

	if (parse_strtoul(buf, 25, &t))
		return -EINVAL;

	if (mutex_lock_killable(&hotkey_mutex))
		return -ERESTARTSYS;

	hotkey_poll_set_freq(t);
	hotkey_poll_setup(true);

	mutex_unlock(&hotkey_mutex);

	tpacpi_disclose_usertask("hotkey_poll_freq", "set to %lu\n", t);

	return count;
}

static struct device_attribute dev_attr_hotkey_poll_freq =
	__ATTR(hotkey_poll_freq, S_IWUSR | S_IRUGO,
		hotkey_poll_freq_show, hotkey_poll_freq_store);

#endif /* CONFIG_THINKPAD_ACPI_HOTKEY_POLL */

/* sysfs hotkey radio_sw (pollable) ------------------------------------ */
static ssize_t hotkey_radio_sw_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int res;
	res = hotkey_get_wlsw();
	if (res < 0)
		return res;

	/* Opportunistic update */
	tpacpi_rfk_update_hwblock_state((res == TPACPI_RFK_RADIO_OFF));

	return snprintf(buf, PAGE_SIZE, "%d\n",
			(res == TPACPI_RFK_RADIO_OFF) ? 0 : 1);
}

static struct device_attribute dev_attr_hotkey_radio_sw =
	__ATTR(hotkey_radio_sw, S_IRUGO, hotkey_radio_sw_show, NULL);

static void hotkey_radio_sw_notify_change(void)
{
	if (tp_features.hotkey_wlsw)
		sysfs_notify(&tpacpi_pdev->dev.kobj, NULL,
			     "hotkey_radio_sw");
}

/* sysfs hotkey tablet mode (pollable) --------------------------------- */
static ssize_t hotkey_tablet_mode_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int res, s;
	res = hotkey_get_tablet_mode(&s);
	if (res < 0)
		return res;

	return snprintf(buf, PAGE_SIZE, "%d\n", !!s);
}

static struct device_attribute dev_attr_hotkey_tablet_mode =
	__ATTR(hotkey_tablet_mode, S_IRUGO, hotkey_tablet_mode_show, NULL);

static void hotkey_tablet_mode_notify_change(void)
{
	if (tp_features.hotkey_tablet)
		sysfs_notify(&tpacpi_pdev->dev.kobj, NULL,
			     "hotkey_tablet_mode");
}

/* sysfs wakeup reason (pollable) -------------------------------------- */
static ssize_t hotkey_wakeup_reason_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hotkey_wakeup_reason);
}

static struct device_attribute dev_attr_hotkey_wakeup_reason =
	__ATTR(wakeup_reason, S_IRUGO, hotkey_wakeup_reason_show, NULL);

static void hotkey_wakeup_reason_notify_change(void)
{
	sysfs_notify(&tpacpi_pdev->dev.kobj, NULL,
		     "wakeup_reason");
}

/* sysfs wakeup hotunplug_complete (pollable) -------------------------- */
static ssize_t hotkey_wakeup_hotunplug_complete_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hotkey_autosleep_ack);
}

static struct device_attribute dev_attr_hotkey_wakeup_hotunplug_complete =
	__ATTR(wakeup_hotunplug_complete, S_IRUGO,
	       hotkey_wakeup_hotunplug_complete_show, NULL);

static void hotkey_wakeup_hotunplug_complete_notify_change(void)
{
	sysfs_notify(&tpacpi_pdev->dev.kobj, NULL,
		     "wakeup_hotunplug_complete");
}

/* --------------------------------------------------------------------- */

static struct attribute *hotkey_attributes[] __initdata = {
	&dev_attr_hotkey_enable.attr,
	&dev_attr_hotkey_bios_enabled.attr,
	&dev_attr_hotkey_bios_mask.attr,
	&dev_attr_hotkey_wakeup_reason.attr,
	&dev_attr_hotkey_wakeup_hotunplug_complete.attr,
	&dev_attr_hotkey_mask.attr,
	&dev_attr_hotkey_all_mask.attr,
	&dev_attr_hotkey_recommended_mask.attr,
#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL
	&dev_attr_hotkey_source_mask.attr,
	&dev_attr_hotkey_poll_freq.attr,
#endif
};

/*
 * Sync both the hw and sw blocking state of all switches
 */
static void tpacpi_send_radiosw_update(void)
{
	int wlsw;

	/*
	 * We must sync all rfkill controllers *before* issuing any
	 * rfkill input events, or we will race the rfkill core input
	 * handler.
	 *
	 * tpacpi_inputdev_send_mutex works as a synchronization point
	 * for the above.
	 *
	 * We optimize to avoid numerous calls to hotkey_get_wlsw.
	 */

	wlsw = hotkey_get_wlsw();

	/* Sync hw blocking state first if it is hw-blocked */
	if (wlsw == TPACPI_RFK_RADIO_OFF)
		tpacpi_rfk_update_hwblock_state(true);

	/* Sync sw blocking state */
	tpacpi_rfk_update_swstate_all();

	/* Sync hw blocking state last if it is hw-unblocked */
	if (wlsw == TPACPI_RFK_RADIO_ON)
		tpacpi_rfk_update_hwblock_state(false);

	/* Issue rfkill input event for WLSW switch */
	if (!(wlsw < 0)) {
		mutex_lock(&tpacpi_inputdev_send_mutex);

		input_report_switch(tpacpi_inputdev,
				    SW_RFKILL_ALL, (wlsw > 0));
		input_sync(tpacpi_inputdev);

		mutex_unlock(&tpacpi_inputdev_send_mutex);
	}

	/*
	 * this can be unconditional, as we will poll state again
	 * if userspace uses the notify to read data
	 */
	hotkey_radio_sw_notify_change();
}

static void hotkey_exit(void)
{
#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL
	mutex_lock(&hotkey_mutex);
	hotkey_poll_stop_sync();
	mutex_unlock(&hotkey_mutex);
#endif

	if (hotkey_dev_attributes)
		delete_attr_set(hotkey_dev_attributes, &tpacpi_pdev->dev.kobj);

	dbg_printk(TPACPI_DBG_EXIT | TPACPI_DBG_HKEY,
		   "restoring original HKEY status and mask\n");
	/* yes, there is a bitwise or below, we want the
	 * functions to be called even if one of them fail */
	if (((tp_features.hotkey_mask &&
	      hotkey_mask_set(hotkey_orig_mask)) |
	     hotkey_status_set(false)) != 0)
		pr_err("failed to restore hot key mask "
		       "to BIOS defaults\n");
}

static void __init hotkey_unmap(const unsigned int scancode)
{
	if (hotkey_keycode_map[scancode] != KEY_RESERVED) {
		clear_bit(hotkey_keycode_map[scancode],
			  tpacpi_inputdev->keybit);
		hotkey_keycode_map[scancode] = KEY_RESERVED;
	}
}

/*
 * HKEY quirks:
 *   TPACPI_HK_Q_INIMASK:	Supports FN+F3,FN+F4,FN+F12
 */

#define	TPACPI_HK_Q_INIMASK	0x0001

static const struct tpacpi_quirk tpacpi_hotkey_qtable[] __initconst = {
	TPACPI_Q_IBM('I', 'H', TPACPI_HK_Q_INIMASK), /* 600E */
	TPACPI_Q_IBM('I', 'N', TPACPI_HK_Q_INIMASK), /* 600E */
	TPACPI_Q_IBM('I', 'D', TPACPI_HK_Q_INIMASK), /* 770, 770E, 770ED */
	TPACPI_Q_IBM('I', 'W', TPACPI_HK_Q_INIMASK), /* A20m */
	TPACPI_Q_IBM('I', 'V', TPACPI_HK_Q_INIMASK), /* A20p */
	TPACPI_Q_IBM('1', '0', TPACPI_HK_Q_INIMASK), /* A21e, A22e */
	TPACPI_Q_IBM('K', 'U', TPACPI_HK_Q_INIMASK), /* A21e */
	TPACPI_Q_IBM('K', 'X', TPACPI_HK_Q_INIMASK), /* A21m, A22m */
	TPACPI_Q_IBM('K', 'Y', TPACPI_HK_Q_INIMASK), /* A21p, A22p */
	TPACPI_Q_IBM('1', 'B', TPACPI_HK_Q_INIMASK), /* A22e */
	TPACPI_Q_IBM('1', '3', TPACPI_HK_Q_INIMASK), /* A22m */
	TPACPI_Q_IBM('1', 'E', TPACPI_HK_Q_INIMASK), /* A30/p (0) */
	TPACPI_Q_IBM('1', 'C', TPACPI_HK_Q_INIMASK), /* R30 */
	TPACPI_Q_IBM('1', 'F', TPACPI_HK_Q_INIMASK), /* R31 */
	TPACPI_Q_IBM('I', 'Y', TPACPI_HK_Q_INIMASK), /* T20 */
	TPACPI_Q_IBM('K', 'Z', TPACPI_HK_Q_INIMASK), /* T21 */
	TPACPI_Q_IBM('1', '6', TPACPI_HK_Q_INIMASK), /* T22 */
	TPACPI_Q_IBM('I', 'Z', TPACPI_HK_Q_INIMASK), /* X20, X21 */
	TPACPI_Q_IBM('1', 'D', TPACPI_HK_Q_INIMASK), /* X22, X23, X24 */
};

typedef u16 tpacpi_keymap_entry_t;
typedef tpacpi_keymap_entry_t tpacpi_keymap_t[TPACPI_HOTKEY_MAP_LEN];

static int __init hotkey_init(struct ibm_init_struct *iibm)
{
	/* Requirements for changing the default keymaps:
	 *
	 * 1. Many of the keys are mapped to KEY_RESERVED for very
	 *    good reasons.  Do not change them unless you have deep
	 *    knowledge on the IBM and Lenovo ThinkPad firmware for
	 *    the various ThinkPad models.  The driver behaves
	 *    differently for KEY_RESERVED: such keys have their
	 *    hot key mask *unset* in mask_recommended, and also
	 *    in the initial hot key mask programmed into the
	 *    firmware at driver load time, which means the firm-
	 *    ware may react very differently if you change them to
	 *    something else;
	 *
	 * 2. You must be subscribed to the linux-thinkpad and
	 *    ibm-acpi-devel mailing lists, and you should read the
	 *    list archives since 2007 if you want to change the
	 *    keymaps.  This requirement exists so that you will
	 *    know the past history of problems with the thinkpad-
	 *    acpi driver keymaps, and also that you will be
	 *    listening to any bug reports;
	 *
	 * 3. Do not send thinkpad-acpi specific patches directly to
	 *    for merging, *ever*.  Send them to the linux-acpi
	 *    mailinglist for comments.  Merging is to be done only
	 *    through acpi-test and the ACPI maintainer.
	 *
	 * If the above is too much to ask, don't change the keymap.
	 * Ask the thinkpad-acpi maintainer to do it, instead.
	 */

	enum keymap_index {
		TPACPI_KEYMAP_IBM_GENERIC = 0,
		TPACPI_KEYMAP_LENOVO_GENERIC,
	};

	static const tpacpi_keymap_t tpacpi_keymaps[] __initconst = {
	/* Generic keymap for IBM ThinkPads */
	[TPACPI_KEYMAP_IBM_GENERIC] = {
		/* Scan Codes 0x00 to 0x0B: ACPI HKEY FN+F1..F12 */
		KEY_FN_F1,	KEY_BATTERY,	KEY_COFFEE,	KEY_SLEEP,
		KEY_WLAN,	KEY_FN_F6, KEY_SWITCHVIDEOMODE, KEY_FN_F8,
		KEY_FN_F9,	KEY_FN_F10,	KEY_FN_F11,	KEY_SUSPEND,

		/* Scan codes 0x0C to 0x1F: Other ACPI HKEY hot keys */
		KEY_UNKNOWN,	/* 0x0C: FN+BACKSPACE */
		KEY_UNKNOWN,	/* 0x0D: FN+INSERT */
		KEY_UNKNOWN,	/* 0x0E: FN+DELETE */

		/* brightness: firmware always reacts to them */
		KEY_RESERVED,	/* 0x0F: FN+HOME (brightness up) */
		KEY_RESERVED,	/* 0x10: FN+END (brightness down) */

		/* Thinklight: firmware always react to it */
		KEY_RESERVED,	/* 0x11: FN+PGUP (thinklight toggle) */

		KEY_UNKNOWN,	/* 0x12: FN+PGDOWN */
		KEY_ZOOM,	/* 0x13: FN+SPACE (zoom) */

		/* Volume: firmware always react to it and reprograms
		 * the built-in *extra* mixer.  Never map it to control
		 * another mixer by default. */
		KEY_RESERVED,	/* 0x14: VOLUME UP */
		KEY_RESERVED,	/* 0x15: VOLUME DOWN */
		KEY_RESERVED,	/* 0x16: MUTE */

		KEY_VENDOR,	/* 0x17: Thinkpad/AccessIBM/Lenovo */

		/* (assignments unknown, please report if found) */
		KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN,
		KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN,
		},

	/* Generic keymap for Lenovo ThinkPads */
	[TPACPI_KEYMAP_LENOVO_GENERIC] = {
		/* Scan Codes 0x00 to 0x0B: ACPI HKEY FN+F1..F12 */
		KEY_FN_F1,	KEY_COFFEE,	KEY_BATTERY,	KEY_SLEEP,
		KEY_WLAN,	KEY_CAMERA, KEY_SWITCHVIDEOMODE, KEY_FN_F8,
		KEY_FN_F9,	KEY_FN_F10,	KEY_FN_F11,	KEY_SUSPEND,

		/* Scan codes 0x0C to 0x1F: Other ACPI HKEY hot keys */
		KEY_UNKNOWN,	/* 0x0C: FN+BACKSPACE */
		KEY_UNKNOWN,	/* 0x0D: FN+INSERT */
		KEY_UNKNOWN,	/* 0x0E: FN+DELETE */

		/* These should be enabled --only-- when ACPI video
		 * is disabled (i.e. in "vendor" mode), and are handled
		 * in a special way by the init code */
		KEY_BRIGHTNESSUP,	/* 0x0F: FN+HOME (brightness up) */
		KEY_BRIGHTNESSDOWN,	/* 0x10: FN+END (brightness down) */

		KEY_RESERVED,	/* 0x11: FN+PGUP (thinklight toggle) */

		KEY_UNKNOWN,	/* 0x12: FN+PGDOWN */
		KEY_ZOOM,	/* 0x13: FN+SPACE (zoom) */

		/* Volume: z60/z61, T60 (BIOS version?): firmware always
		 * react to it and reprograms the built-in *extra* mixer.
		 * Never map it to control another mixer by default.
		 *
		 * T60?, T61, R60?, R61: firmware and EC tries to send
		 * these over the regular keyboard, so these are no-ops,
		 * but there are still weird bugs re. MUTE, so do not
		 * change unless you get test reports from all Lenovo
		 * models.  May cause the BIOS to interfere with the
		 * HDA mixer.
		 */
		KEY_RESERVED,	/* 0x14: VOLUME UP */
		KEY_RESERVED,	/* 0x15: VOLUME DOWN */
		KEY_RESERVED,	/* 0x16: MUTE */

		KEY_VENDOR,	/* 0x17: Thinkpad/AccessIBM/Lenovo */

		/* (assignments unknown, please report if found) */
		KEY_UNKNOWN, KEY_UNKNOWN,

		/*
		 * The mic mute button only sends 0x1a.  It does not
		 * automatically mute the mic or change the mute light.
		 */
		KEY_MICMUTE,	/* 0x1a: Mic mute (since ?400 or so) */

		/* (assignments unknown, please report if found) */
		KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN,
		KEY_UNKNOWN,
		},
	};

	static const struct tpacpi_quirk tpacpi_keymap_qtable[] __initconst = {
		/* Generic maps (fallback) */
		{
		  .vendor = PCI_VENDOR_ID_IBM,
		  .bios = TPACPI_MATCH_ANY, .ec = TPACPI_MATCH_ANY,
		  .quirks = TPACPI_KEYMAP_IBM_GENERIC,
		},
		{
		  .vendor = PCI_VENDOR_ID_LENOVO,
		  .bios = TPACPI_MATCH_ANY, .ec = TPACPI_MATCH_ANY,
		  .quirks = TPACPI_KEYMAP_LENOVO_GENERIC,
		},
	};

#define TPACPI_HOTKEY_MAP_SIZE		sizeof(tpacpi_keymap_t)
#define TPACPI_HOTKEY_MAP_TYPESIZE	sizeof(tpacpi_keymap_entry_t)

	int res, i;
	int status;
	int hkeyv;
	bool radiosw_state  = false;
	bool tabletsw_state = false;

	unsigned long quirks;
	unsigned long keymap_id;

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_HKEY,
			"initializing hotkey subdriver\n");

	BUG_ON(!tpacpi_inputdev);
	BUG_ON(tpacpi_inputdev->open != NULL ||
	       tpacpi_inputdev->close != NULL);

	TPACPI_ACPIHANDLE_INIT(hkey);
	mutex_init(&hotkey_mutex);

#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL
	mutex_init(&hotkey_thread_data_mutex);
#endif

	/* hotkey not supported on 570 */
	tp_features.hotkey = hkey_handle != NULL;

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_HKEY,
		"hotkeys are %s\n",
		str_supported(tp_features.hotkey));

	if (!tp_features.hotkey)
		return 1;

	quirks = tpacpi_check_quirks(tpacpi_hotkey_qtable,
				     ARRAY_SIZE(tpacpi_hotkey_qtable));

	tpacpi_disable_brightness_delay();

	/* MUST have enough space for all attributes to be added to
	 * hotkey_dev_attributes */
	hotkey_dev_attributes = create_attr_set(
					ARRAY_SIZE(hotkey_attributes) + 2,
					NULL);
	if (!hotkey_dev_attributes)
		return -ENOMEM;
	res = add_many_to_attr_set(hotkey_dev_attributes,
			hotkey_attributes,
			ARRAY_SIZE(hotkey_attributes));
	if (res)
		goto err_exit;

	/* mask not supported on 600e/x, 770e, 770x, A21e, A2xm/p,
	   A30, R30, R31, T20-22, X20-21, X22-24.  Detected by checking
	   for HKEY interface version 0x100 */
	if (acpi_evalf(hkey_handle, &hkeyv, "MHKV", "qd")) {
		if ((hkeyv >> 8) != 1) {
			pr_err("unknown version of the HKEY interface: 0x%x\n",
			       hkeyv);
			pr_err("please report this to %s\n", TPACPI_MAIL);
		} else {
			/*
			 * MHKV 0x100 in A31, R40, R40e,
			 * T4x, X31, and later
			 */
			vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_HKEY,
				"firmware HKEY interface version: 0x%x\n",
				hkeyv);

			/* Paranoia check AND init hotkey_all_mask */
			if (!acpi_evalf(hkey_handle, &hotkey_all_mask,
					"MHKA", "qd")) {
				pr_err("missing MHKA handler, "
				       "please report this to %s\n",
				       TPACPI_MAIL);
				/* Fallback: pre-init for FN+F3,F4,F12 */
				hotkey_all_mask = 0x080cU;
			} else {
				tp_features.hotkey_mask = 1;
			}
		}
	}

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_HKEY,
		"hotkey masks are %s\n",
		str_supported(tp_features.hotkey_mask));

	/* Init hotkey_all_mask if not initialized yet */
	if (!tp_features.hotkey_mask && !hotkey_all_mask &&
	    (quirks & TPACPI_HK_Q_INIMASK))
		hotkey_all_mask = 0x080cU;  /* FN+F12, FN+F4, FN+F3 */

	/* Init hotkey_acpi_mask and hotkey_orig_mask */
	if (tp_features.hotkey_mask) {
		/* hotkey_source_mask *must* be zero for
		 * the first hotkey_mask_get to return hotkey_orig_mask */
		res = hotkey_mask_get();
		if (res)
			goto err_exit;

		hotkey_orig_mask = hotkey_acpi_mask;
	} else {
		hotkey_orig_mask = hotkey_all_mask;
		hotkey_acpi_mask = hotkey_all_mask;
	}

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_wlswemul) {
		tp_features.hotkey_wlsw = 1;
		radiosw_state = !!tpacpi_wlsw_emulstate;
		pr_info("radio switch emulation enabled\n");
	} else
#endif
	/* Not all thinkpads have a hardware radio switch */
	if (acpi_evalf(hkey_handle, &status, "WLSW", "qd")) {
		tp_features.hotkey_wlsw = 1;
		radiosw_state = !!status;
		pr_info("radio switch found; radios are %s\n",
			enabled(status, 0));
	}
	if (tp_features.hotkey_wlsw)
		res = add_to_attr_set(hotkey_dev_attributes,
				&dev_attr_hotkey_radio_sw.attr);

	/* For X41t, X60t, X61t Tablets... */
	if (!res && acpi_evalf(hkey_handle, &status, "MHKG", "qd")) {
		tp_features.hotkey_tablet = 1;
		tabletsw_state = !!(status & TP_HOTKEY_TABLET_MASK);
		pr_info("possible tablet mode switch found; "
			"ThinkPad in %s mode\n",
			(tabletsw_state) ? "tablet" : "laptop");
		res = add_to_attr_set(hotkey_dev_attributes,
				&dev_attr_hotkey_tablet_mode.attr);
	}

	if (!res)
		res = register_attr_set_with_sysfs(
				hotkey_dev_attributes,
				&tpacpi_pdev->dev.kobj);
	if (res)
		goto err_exit;

	/* Set up key map */
	hotkey_keycode_map = kmalloc(TPACPI_HOTKEY_MAP_SIZE,
					GFP_KERNEL);
	if (!hotkey_keycode_map) {
		pr_err("failed to allocate memory for key map\n");
		res = -ENOMEM;
		goto err_exit;
	}

	keymap_id = tpacpi_check_quirks(tpacpi_keymap_qtable,
					ARRAY_SIZE(tpacpi_keymap_qtable));
	BUG_ON(keymap_id >= ARRAY_SIZE(tpacpi_keymaps));
	dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_HKEY,
		   "using keymap number %lu\n", keymap_id);

	memcpy(hotkey_keycode_map, &tpacpi_keymaps[keymap_id],
		TPACPI_HOTKEY_MAP_SIZE);

	input_set_capability(tpacpi_inputdev, EV_MSC, MSC_SCAN);
	tpacpi_inputdev->keycodesize = TPACPI_HOTKEY_MAP_TYPESIZE;
	tpacpi_inputdev->keycodemax = TPACPI_HOTKEY_MAP_LEN;
	tpacpi_inputdev->keycode = hotkey_keycode_map;
	for (i = 0; i < TPACPI_HOTKEY_MAP_LEN; i++) {
		if (hotkey_keycode_map[i] != KEY_RESERVED) {
			input_set_capability(tpacpi_inputdev, EV_KEY,
						hotkey_keycode_map[i]);
		} else {
			if (i < sizeof(hotkey_reserved_mask)*8)
				hotkey_reserved_mask |= 1 << i;
		}
	}

	if (tp_features.hotkey_wlsw) {
		input_set_capability(tpacpi_inputdev, EV_SW, SW_RFKILL_ALL);
		input_report_switch(tpacpi_inputdev,
				    SW_RFKILL_ALL, radiosw_state);
	}
	if (tp_features.hotkey_tablet) {
		input_set_capability(tpacpi_inputdev, EV_SW, SW_TABLET_MODE);
		input_report_switch(tpacpi_inputdev,
				    SW_TABLET_MODE, tabletsw_state);
	}

	/* Do not issue duplicate brightness change events to
	 * userspace. tpacpi_detect_brightness_capabilities() must have
	 * been called before this point  */
	if (acpi_video_backlight_support()) {
		pr_info("This ThinkPad has standard ACPI backlight "
			"brightness control, supported by the ACPI "
			"video driver\n");
		pr_notice("Disabling thinkpad-acpi brightness events "
			  "by default...\n");

		/* Disable brightness up/down on Lenovo thinkpads when
		 * ACPI is handling them, otherwise it is plain impossible
		 * for userspace to do something even remotely sane */
		hotkey_reserved_mask |=
			(1 << TP_ACPI_HOTKEYSCAN_FNHOME)
			| (1 << TP_ACPI_HOTKEYSCAN_FNEND);
		hotkey_unmap(TP_ACPI_HOTKEYSCAN_FNHOME);
		hotkey_unmap(TP_ACPI_HOTKEYSCAN_FNEND);
	}

#ifdef CONFIG_THINKPAD_ACPI_HOTKEY_POLL
	hotkey_source_mask = TPACPI_HKEY_NVRAM_GOOD_MASK
				& ~hotkey_all_mask
				& ~hotkey_reserved_mask;

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_HKEY,
		    "hotkey source mask 0x%08x, polling freq %u\n",
		    hotkey_source_mask, hotkey_poll_freq);
#endif

	dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_HKEY,
			"enabling firmware HKEY event interface...\n");
	res = hotkey_status_set(true);
	if (res) {
		hotkey_exit();
		return res;
	}
	res = hotkey_mask_set(((hotkey_all_mask & ~hotkey_reserved_mask)
			       | hotkey_driver_mask)
			      & ~hotkey_source_mask);
	if (res < 0 && res != -ENXIO) {
		hotkey_exit();
		return res;
	}
	hotkey_user_mask = (hotkey_acpi_mask | hotkey_source_mask)
				& ~hotkey_reserved_mask;
	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_HKEY,
		"initial masks: user=0x%08x, fw=0x%08x, poll=0x%08x\n",
		hotkey_user_mask, hotkey_acpi_mask, hotkey_source_mask);

	tpacpi_inputdev->open = &hotkey_inputdev_open;
	tpacpi_inputdev->close = &hotkey_inputdev_close;

	hotkey_poll_setup_safe(true);

	return 0;

err_exit:
	delete_attr_set(hotkey_dev_attributes, &tpacpi_pdev->dev.kobj);
	hotkey_dev_attributes = NULL;

	return (res < 0)? res : 1;
}

static bool hotkey_notify_hotkey(const u32 hkey,
				 bool *send_acpi_ev,
				 bool *ignore_acpi_ev)
{
	/* 0x1000-0x1FFF: key presses */
	unsigned int scancode = hkey & 0xfff;
	*send_acpi_ev = true;
	*ignore_acpi_ev = false;

	/* HKEY event 0x1001 is scancode 0x00 */
	if (scancode > 0 && scancode <= TPACPI_HOTKEY_MAP_LEN) {
		scancode--;
		if (!(hotkey_source_mask & (1 << scancode))) {
			tpacpi_input_send_key_masked(scancode);
			*send_acpi_ev = false;
		} else {
			*ignore_acpi_ev = true;
		}
		return true;
	}
	return false;
}

static bool hotkey_notify_wakeup(const u32 hkey,
				 bool *send_acpi_ev,
				 bool *ignore_acpi_ev)
{
	/* 0x2000-0x2FFF: Wakeup reason */
	*send_acpi_ev = true;
	*ignore_acpi_ev = false;

	switch (hkey) {
	case TP_HKEY_EV_WKUP_S3_UNDOCK: /* suspend, undock */
	case TP_HKEY_EV_WKUP_S4_UNDOCK: /* hibernation, undock */
		hotkey_wakeup_reason = TP_ACPI_WAKEUP_UNDOCK;
		*ignore_acpi_ev = true;
		break;

	case TP_HKEY_EV_WKUP_S3_BAYEJ: /* suspend, bay eject */
	case TP_HKEY_EV_WKUP_S4_BAYEJ: /* hibernation, bay eject */
		hotkey_wakeup_reason = TP_ACPI_WAKEUP_BAYEJ;
		*ignore_acpi_ev = true;
		break;

	case TP_HKEY_EV_WKUP_S3_BATLOW: /* Battery on critical low level/S3 */
	case TP_HKEY_EV_WKUP_S4_BATLOW: /* Battery on critical low level/S4 */
		pr_alert("EMERGENCY WAKEUP: battery almost empty\n");
		/* how to auto-heal: */
		/* 2313: woke up from S3, go to S4/S5 */
		/* 2413: woke up from S4, go to S5 */
		break;

	default:
		return false;
	}

	if (hotkey_wakeup_reason != TP_ACPI_WAKEUP_NONE) {
		pr_info("woke up due to a hot-unplug request...\n");
		hotkey_wakeup_reason_notify_change();
	}
	return true;
}

static bool hotkey_notify_dockevent(const u32 hkey,
				 bool *send_acpi_ev,
				 bool *ignore_acpi_ev)
{
	/* 0x4000-0x4FFF: dock-related events */
	*send_acpi_ev = true;
	*ignore_acpi_ev = false;

	switch (hkey) {
	case TP_HKEY_EV_UNDOCK_ACK:
		/* ACPI undock operation completed after wakeup */
		hotkey_autosleep_ack = 1;
		pr_info("undocked\n");
		hotkey_wakeup_hotunplug_complete_notify_change();
		return true;

	case TP_HKEY_EV_HOTPLUG_DOCK: /* docked to port replicator */
		pr_info("docked into hotplug port replicator\n");
		return true;
	case TP_HKEY_EV_HOTPLUG_UNDOCK: /* undocked from port replicator */
		pr_info("undocked from hotplug port replicator\n");
		return true;

	default:
		return false;
	}
}

static bool hotkey_notify_usrevent(const u32 hkey,
				 bool *send_acpi_ev,
				 bool *ignore_acpi_ev)
{
	/* 0x5000-0x5FFF: human interface helpers */
	*send_acpi_ev = true;
	*ignore_acpi_ev = false;

	switch (hkey) {
	case TP_HKEY_EV_PEN_INSERTED:  /* X61t: tablet pen inserted into bay */
	case TP_HKEY_EV_PEN_REMOVED:   /* X61t: tablet pen removed from bay */
		return true;

	case TP_HKEY_EV_TABLET_TABLET:   /* X41t-X61t: tablet mode */
	case TP_HKEY_EV_TABLET_NOTEBOOK: /* X41t-X61t: normal mode */
		tpacpi_input_send_tabletsw();
		hotkey_tablet_mode_notify_change();
		*send_acpi_ev = false;
		return true;

	case TP_HKEY_EV_LID_CLOSE:	/* Lid closed */
	case TP_HKEY_EV_LID_OPEN:	/* Lid opened */
	case TP_HKEY_EV_BRGHT_CHANGED:	/* brightness changed */
		/* do not propagate these events */
		*ignore_acpi_ev = true;
		return true;

	default:
		return false;
	}
}

static void thermal_dump_all_sensors(void);

static bool hotkey_notify_6xxx(const u32 hkey,
				 bool *send_acpi_ev,
				 bool *ignore_acpi_ev)
{
	bool known = true;

	/* 0x6000-0x6FFF: thermal alarms/notices and keyboard events */
	*send_acpi_ev = true;
	*ignore_acpi_ev = false;

	switch (hkey) {
	case TP_HKEY_EV_THM_TABLE_CHANGED:
		pr_info("EC reports that Thermal Table has changed\n");
		/* recommended action: do nothing, we don't have
		 * Lenovo ATM information */
		return true;
	case TP_HKEY_EV_ALARM_BAT_HOT:
		pr_crit("THERMAL ALARM: battery is too hot!\n");
		/* recommended action: warn user through gui */
		break;
	case TP_HKEY_EV_ALARM_BAT_XHOT:
		pr_alert("THERMAL EMERGENCY: battery is extremely hot!\n");
		/* recommended action: immediate sleep/hibernate */
		break;
	case TP_HKEY_EV_ALARM_SENSOR_HOT:
		pr_crit("THERMAL ALARM: "
			"a sensor reports something is too hot!\n");
		/* recommended action: warn user through gui, that */
		/* some internal component is too hot */
		break;
	case TP_HKEY_EV_ALARM_SENSOR_XHOT:
		pr_alert("THERMAL EMERGENCY: "
			 "a sensor reports something is extremely hot!\n");
		/* recommended action: immediate sleep/hibernate */
		break;
	case TP_HKEY_EV_AC_CHANGED:
		/* X120e, X121e, X220, X220i, X220t, X230, T420, T420s, W520:
		 * AC status changed; can be triggered by plugging or
		 * unplugging AC adapter, docking or undocking. */

		/* fallthrough */

	case TP_HKEY_EV_KEY_NUMLOCK:
	case TP_HKEY_EV_KEY_FN:
		/* key press events, we just ignore them as long as the EC
		 * is still reporting them in the normal keyboard stream */
		*send_acpi_ev = false;
		*ignore_acpi_ev = true;
		return true;

	default:
		pr_warn("unknown possible thermal alarm or keyboard event received\n");
		known = false;
	}

	thermal_dump_all_sensors();

	return known;
}

static void hotkey_notify(struct ibm_struct *ibm, u32 event)
{
	u32 hkey;
	bool send_acpi_ev;
	bool ignore_acpi_ev;
	bool known_ev;

	if (event != 0x80) {
		pr_err("unknown HKEY notification event %d\n", event);
		/* forward it to userspace, maybe it knows how to handle it */
		acpi_bus_generate_netlink_event(
					ibm->acpi->device->pnp.device_class,
					dev_name(&ibm->acpi->device->dev),
					event, 0);
		return;
	}

	while (1) {
		if (!acpi_evalf(hkey_handle, &hkey, "MHKP", "d")) {
			pr_err("failed to retrieve HKEY event\n");
			return;
		}

		if (hkey == 0) {
			/* queue empty */
			return;
		}

		send_acpi_ev = true;
		ignore_acpi_ev = false;

		switch (hkey >> 12) {
		case 1:
			/* 0x1000-0x1FFF: key presses */
			known_ev = hotkey_notify_hotkey(hkey, &send_acpi_ev,
						 &ignore_acpi_ev);
			break;
		case 2:
			/* 0x2000-0x2FFF: Wakeup reason */
			known_ev = hotkey_notify_wakeup(hkey, &send_acpi_ev,
						 &ignore_acpi_ev);
			break;
		case 3:
			/* 0x3000-0x3FFF: bay-related wakeups */
			switch (hkey) {
			case TP_HKEY_EV_BAYEJ_ACK:
				hotkey_autosleep_ack = 1;
				pr_info("bay ejected\n");
				hotkey_wakeup_hotunplug_complete_notify_change();
				known_ev = true;
				break;
			case TP_HKEY_EV_OPTDRV_EJ:
				/* FIXME: kick libata if SATA link offline */
				known_ev = true;
				break;
			default:
				known_ev = false;
			}
			break;
		case 4:
			/* 0x4000-0x4FFF: dock-related events */
			known_ev = hotkey_notify_dockevent(hkey, &send_acpi_ev,
						&ignore_acpi_ev);
			break;
		case 5:
			/* 0x5000-0x5FFF: human interface helpers */
			known_ev = hotkey_notify_usrevent(hkey, &send_acpi_ev,
						 &ignore_acpi_ev);
			break;
		case 6:
			/* 0x6000-0x6FFF: thermal alarms/notices and
			 *                keyboard events */
			known_ev = hotkey_notify_6xxx(hkey, &send_acpi_ev,
						 &ignore_acpi_ev);
			break;
		case 7:
			/* 0x7000-0x7FFF: misc */
			if (tp_features.hotkey_wlsw &&
					hkey == TP_HKEY_EV_RFKILL_CHANGED) {
				tpacpi_send_radiosw_update();
				send_acpi_ev = 0;
				known_ev = true;
				break;
			}
			/* fallthrough to default */
		default:
			known_ev = false;
		}
		if (!known_ev) {
			pr_notice("unhandled HKEY event 0x%04x\n", hkey);
			pr_notice("please report the conditions when this "
				  "event happened to %s\n", TPACPI_MAIL);
		}

		/* netlink events */
		if (!ignore_acpi_ev && send_acpi_ev) {
			acpi_bus_generate_netlink_event(
					ibm->acpi->device->pnp.device_class,
					dev_name(&ibm->acpi->device->dev),
					event, hkey);
		}
	}
}

static void hotkey_suspend(void)
{
	/* Do these on suspend, we get the events on early resume! */
	hotkey_wakeup_reason = TP_ACPI_WAKEUP_NONE;
	hotkey_autosleep_ack = 0;
}

static void hotkey_resume(void)
{
	tpacpi_disable_brightness_delay();

	if (hotkey_status_set(true) < 0 ||
	    hotkey_mask_set(hotkey_acpi_mask) < 0)
		pr_err("error while attempting to reset the event "
		       "firmware interface\n");

	tpacpi_send_radiosw_update();
	hotkey_tablet_mode_notify_change();
	hotkey_wakeup_reason_notify_change();
	hotkey_wakeup_hotunplug_complete_notify_change();
	hotkey_poll_setup_safe(false);
}

/* procfs -------------------------------------------------------------- */
static int hotkey_read(struct seq_file *m)
{
	int res, status;

	if (!tp_features.hotkey) {
		seq_printf(m, "status:\t\tnot supported\n");
		return 0;
	}

	if (mutex_lock_killable(&hotkey_mutex))
		return -ERESTARTSYS;
	res = hotkey_status_get(&status);
	if (!res)
		res = hotkey_mask_get();
	mutex_unlock(&hotkey_mutex);
	if (res)
		return res;

	seq_printf(m, "status:\t\t%s\n", enabled(status, 0));
	if (hotkey_all_mask) {
		seq_printf(m, "mask:\t\t0x%08x\n", hotkey_user_mask);
		seq_printf(m, "commands:\tenable, disable, reset, <mask>\n");
	} else {
		seq_printf(m, "mask:\t\tnot supported\n");
		seq_printf(m, "commands:\tenable, disable, reset\n");
	}

	return 0;
}

static void hotkey_enabledisable_warn(bool enable)
{
	tpacpi_log_usertask("procfs hotkey enable/disable");
	if (!WARN((tpacpi_lifecycle == TPACPI_LIFE_RUNNING || !enable),
		  pr_fmt("hotkey enable/disable functionality has been "
			 "removed from the driver.  "
			 "Hotkeys are always enabled.\n")))
		pr_err("Please remove the hotkey=enable module "
		       "parameter, it is deprecated.  "
		       "Hotkeys are always enabled.\n");
}

static int hotkey_write(char *buf)
{
	int res;
	u32 mask;
	char *cmd;

	if (!tp_features.hotkey)
		return -ENODEV;

	if (mutex_lock_killable(&hotkey_mutex))
		return -ERESTARTSYS;

	mask = hotkey_user_mask;

	res = 0;
	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "enable") == 0) {
			hotkey_enabledisable_warn(1);
		} else if (strlencmp(cmd, "disable") == 0) {
			hotkey_enabledisable_warn(0);
			res = -EPERM;
		} else if (strlencmp(cmd, "reset") == 0) {
			mask = (hotkey_all_mask | hotkey_source_mask)
				& ~hotkey_reserved_mask;
		} else if (sscanf(cmd, "0x%x", &mask) == 1) {
			/* mask set */
		} else if (sscanf(cmd, "%x", &mask) == 1) {
			/* mask set */
		} else {
			res = -EINVAL;
			goto errexit;
		}
	}

	if (!res) {
		tpacpi_disclose_usertask("procfs hotkey",
			"set mask to 0x%08x\n", mask);
		res = hotkey_user_mask_set(mask);
	}

errexit:
	mutex_unlock(&hotkey_mutex);
	return res;
}

static const struct acpi_device_id ibm_htk_device_ids[] = {
	{TPACPI_ACPI_IBM_HKEY_HID, 0},
	{TPACPI_ACPI_LENOVO_HKEY_HID, 0},
	{"", 0},
};

static struct tp_acpi_drv_struct ibm_hotkey_acpidriver = {
	.hid = ibm_htk_device_ids,
	.notify = hotkey_notify,
	.handle = &hkey_handle,
	.type = ACPI_DEVICE_NOTIFY,
};

static struct ibm_struct hotkey_driver_data = {
	.name = "hotkey",
	.read = hotkey_read,
	.write = hotkey_write,
	.exit = hotkey_exit,
	.resume = hotkey_resume,
	.suspend = hotkey_suspend,
	.acpi = &ibm_hotkey_acpidriver,
};

/*************************************************************************
 * Bluetooth subdriver
 */

enum {
	/* ACPI GBDC/SBDC bits */
	TP_ACPI_BLUETOOTH_HWPRESENT	= 0x01,	/* Bluetooth hw available */
	TP_ACPI_BLUETOOTH_RADIOSSW	= 0x02,	/* Bluetooth radio enabled */
	TP_ACPI_BLUETOOTH_RESUMECTRL	= 0x04,	/* Bluetooth state at resume:
						   0 = disable, 1 = enable */
};

enum {
	/* ACPI \BLTH commands */
	TP_ACPI_BLTH_GET_ULTRAPORT_ID	= 0x00, /* Get Ultraport BT ID */
	TP_ACPI_BLTH_GET_PWR_ON_RESUME	= 0x01, /* Get power-on-resume state */
	TP_ACPI_BLTH_PWR_ON_ON_RESUME	= 0x02, /* Resume powered on */
	TP_ACPI_BLTH_PWR_OFF_ON_RESUME	= 0x03,	/* Resume powered off */
	TP_ACPI_BLTH_SAVE_STATE		= 0x05, /* Save state for S4/S5 */
};

#define TPACPI_RFK_BLUETOOTH_SW_NAME	"tpacpi_bluetooth_sw"

static int bluetooth_get_status(void)
{
	int status;

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_bluetoothemul)
		return (tpacpi_bluetooth_emulstate) ?
		       TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF;
#endif

	if (!acpi_evalf(hkey_handle, &status, "GBDC", "d"))
		return -EIO;

	return ((status & TP_ACPI_BLUETOOTH_RADIOSSW) != 0) ?
			TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF;
}

static int bluetooth_set_status(enum tpacpi_rfkill_state state)
{
	int status;

	vdbg_printk(TPACPI_DBG_RFKILL,
		"will attempt to %s bluetooth\n",
		(state == TPACPI_RFK_RADIO_ON) ? "enable" : "disable");

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_bluetoothemul) {
		tpacpi_bluetooth_emulstate = (state == TPACPI_RFK_RADIO_ON);
		return 0;
	}
#endif

	if (state == TPACPI_RFK_RADIO_ON)
		status = TP_ACPI_BLUETOOTH_RADIOSSW
			  | TP_ACPI_BLUETOOTH_RESUMECTRL;
	else
		status = 0;

	if (!acpi_evalf(hkey_handle, NULL, "SBDC", "vd", status))
		return -EIO;

	return 0;
}

/* sysfs bluetooth enable ---------------------------------------------- */
static ssize_t bluetooth_enable_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return tpacpi_rfk_sysfs_enable_show(TPACPI_RFK_BLUETOOTH_SW_ID,
			attr, buf);
}

static ssize_t bluetooth_enable_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return tpacpi_rfk_sysfs_enable_store(TPACPI_RFK_BLUETOOTH_SW_ID,
				attr, buf, count);
}

static struct device_attribute dev_attr_bluetooth_enable =
	__ATTR(bluetooth_enable, S_IWUSR | S_IRUGO,
		bluetooth_enable_show, bluetooth_enable_store);

/* --------------------------------------------------------------------- */

static struct attribute *bluetooth_attributes[] = {
	&dev_attr_bluetooth_enable.attr,
	NULL
};

static const struct attribute_group bluetooth_attr_group = {
	.attrs = bluetooth_attributes,
};

static const struct tpacpi_rfk_ops bluetooth_tprfk_ops = {
	.get_status = bluetooth_get_status,
	.set_status = bluetooth_set_status,
};

static void bluetooth_shutdown(void)
{
	/* Order firmware to save current state to NVRAM */
	if (!acpi_evalf(NULL, NULL, "\\BLTH", "vd",
			TP_ACPI_BLTH_SAVE_STATE))
		pr_notice("failed to save bluetooth state to NVRAM\n");
	else
		vdbg_printk(TPACPI_DBG_RFKILL,
			"bluetooth state saved to NVRAM\n");
}

static void bluetooth_exit(void)
{
	sysfs_remove_group(&tpacpi_pdev->dev.kobj,
			&bluetooth_attr_group);

	tpacpi_destroy_rfkill(TPACPI_RFK_BLUETOOTH_SW_ID);

	bluetooth_shutdown();
}

static int __init bluetooth_init(struct ibm_init_struct *iibm)
{
	int res;
	int status = 0;

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_RFKILL,
			"initializing bluetooth subdriver\n");

	TPACPI_ACPIHANDLE_INIT(hkey);

	/* bluetooth not supported on 570, 600e/x, 770e, 770x, A21e, A2xm/p,
	   G4x, R30, R31, R40e, R50e, T20-22, X20-21 */
	tp_features.bluetooth = hkey_handle &&
	    acpi_evalf(hkey_handle, &status, "GBDC", "qd");

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_RFKILL,
		"bluetooth is %s, status 0x%02x\n",
		str_supported(tp_features.bluetooth),
		status);

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_bluetoothemul) {
		tp_features.bluetooth = 1;
		pr_info("bluetooth switch emulation enabled\n");
	} else
#endif
	if (tp_features.bluetooth &&
	    !(status & TP_ACPI_BLUETOOTH_HWPRESENT)) {
		/* no bluetooth hardware present in system */
		tp_features.bluetooth = 0;
		dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_RFKILL,
			   "bluetooth hardware not installed\n");
	}

	if (!tp_features.bluetooth)
		return 1;

	res = tpacpi_new_rfkill(TPACPI_RFK_BLUETOOTH_SW_ID,
				&bluetooth_tprfk_ops,
				RFKILL_TYPE_BLUETOOTH,
				TPACPI_RFK_BLUETOOTH_SW_NAME,
				true);
	if (res)
		return res;

	res = sysfs_create_group(&tpacpi_pdev->dev.kobj,
				&bluetooth_attr_group);
	if (res) {
		tpacpi_destroy_rfkill(TPACPI_RFK_BLUETOOTH_SW_ID);
		return res;
	}

	return 0;
}

/* procfs -------------------------------------------------------------- */
static int bluetooth_read(struct seq_file *m)
{
	return tpacpi_rfk_procfs_read(TPACPI_RFK_BLUETOOTH_SW_ID, m);
}

static int bluetooth_write(char *buf)
{
	return tpacpi_rfk_procfs_write(TPACPI_RFK_BLUETOOTH_SW_ID, buf);
}

static struct ibm_struct bluetooth_driver_data = {
	.name = "bluetooth",
	.read = bluetooth_read,
	.write = bluetooth_write,
	.exit = bluetooth_exit,
	.shutdown = bluetooth_shutdown,
};

/*************************************************************************
 * Wan subdriver
 */

enum {
	/* ACPI GWAN/SWAN bits */
	TP_ACPI_WANCARD_HWPRESENT	= 0x01,	/* Wan hw available */
	TP_ACPI_WANCARD_RADIOSSW	= 0x02,	/* Wan radio enabled */
	TP_ACPI_WANCARD_RESUMECTRL	= 0x04,	/* Wan state at resume:
						   0 = disable, 1 = enable */
};

#define TPACPI_RFK_WWAN_SW_NAME		"tpacpi_wwan_sw"

static int wan_get_status(void)
{
	int status;

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_wwanemul)
		return (tpacpi_wwan_emulstate) ?
		       TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF;
#endif

	if (!acpi_evalf(hkey_handle, &status, "GWAN", "d"))
		return -EIO;

	return ((status & TP_ACPI_WANCARD_RADIOSSW) != 0) ?
			TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF;
}

static int wan_set_status(enum tpacpi_rfkill_state state)
{
	int status;

	vdbg_printk(TPACPI_DBG_RFKILL,
		"will attempt to %s wwan\n",
		(state == TPACPI_RFK_RADIO_ON) ? "enable" : "disable");

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_wwanemul) {
		tpacpi_wwan_emulstate = (state == TPACPI_RFK_RADIO_ON);
		return 0;
	}
#endif

	if (state == TPACPI_RFK_RADIO_ON)
		status = TP_ACPI_WANCARD_RADIOSSW
			 | TP_ACPI_WANCARD_RESUMECTRL;
	else
		status = 0;

	if (!acpi_evalf(hkey_handle, NULL, "SWAN", "vd", status))
		return -EIO;

	return 0;
}

/* sysfs wan enable ---------------------------------------------------- */
static ssize_t wan_enable_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return tpacpi_rfk_sysfs_enable_show(TPACPI_RFK_WWAN_SW_ID,
			attr, buf);
}

static ssize_t wan_enable_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return tpacpi_rfk_sysfs_enable_store(TPACPI_RFK_WWAN_SW_ID,
			attr, buf, count);
}

static struct device_attribute dev_attr_wan_enable =
	__ATTR(wwan_enable, S_IWUSR | S_IRUGO,
		wan_enable_show, wan_enable_store);

/* --------------------------------------------------------------------- */

static struct attribute *wan_attributes[] = {
	&dev_attr_wan_enable.attr,
	NULL
};

static const struct attribute_group wan_attr_group = {
	.attrs = wan_attributes,
};

static const struct tpacpi_rfk_ops wan_tprfk_ops = {
	.get_status = wan_get_status,
	.set_status = wan_set_status,
};

static void wan_shutdown(void)
{
	/* Order firmware to save current state to NVRAM */
	if (!acpi_evalf(NULL, NULL, "\\WGSV", "vd",
			TP_ACPI_WGSV_SAVE_STATE))
		pr_notice("failed to save WWAN state to NVRAM\n");
	else
		vdbg_printk(TPACPI_DBG_RFKILL,
			"WWAN state saved to NVRAM\n");
}

static void wan_exit(void)
{
	sysfs_remove_group(&tpacpi_pdev->dev.kobj,
		&wan_attr_group);

	tpacpi_destroy_rfkill(TPACPI_RFK_WWAN_SW_ID);

	wan_shutdown();
}

static int __init wan_init(struct ibm_init_struct *iibm)
{
	int res;
	int status = 0;

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_RFKILL,
			"initializing wan subdriver\n");

	TPACPI_ACPIHANDLE_INIT(hkey);

	tp_features.wan = hkey_handle &&
	    acpi_evalf(hkey_handle, &status, "GWAN", "qd");

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_RFKILL,
		"wan is %s, status 0x%02x\n",
		str_supported(tp_features.wan),
		status);

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_wwanemul) {
		tp_features.wan = 1;
		pr_info("wwan switch emulation enabled\n");
	} else
#endif
	if (tp_features.wan &&
	    !(status & TP_ACPI_WANCARD_HWPRESENT)) {
		/* no wan hardware present in system */
		tp_features.wan = 0;
		dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_RFKILL,
			   "wan hardware not installed\n");
	}

	if (!tp_features.wan)
		return 1;

	res = tpacpi_new_rfkill(TPACPI_RFK_WWAN_SW_ID,
				&wan_tprfk_ops,
				RFKILL_TYPE_WWAN,
				TPACPI_RFK_WWAN_SW_NAME,
				true);
	if (res)
		return res;

	res = sysfs_create_group(&tpacpi_pdev->dev.kobj,
				&wan_attr_group);

	if (res) {
		tpacpi_destroy_rfkill(TPACPI_RFK_WWAN_SW_ID);
		return res;
	}

	return 0;
}

/* procfs -------------------------------------------------------------- */
static int wan_read(struct seq_file *m)
{
	return tpacpi_rfk_procfs_read(TPACPI_RFK_WWAN_SW_ID, m);
}

static int wan_write(char *buf)
{
	return tpacpi_rfk_procfs_write(TPACPI_RFK_WWAN_SW_ID, buf);
}

static struct ibm_struct wan_driver_data = {
	.name = "wan",
	.read = wan_read,
	.write = wan_write,
	.exit = wan_exit,
	.shutdown = wan_shutdown,
};

/*************************************************************************
 * UWB subdriver
 */

enum {
	/* ACPI GUWB/SUWB bits */
	TP_ACPI_UWB_HWPRESENT	= 0x01,	/* UWB hw available */
	TP_ACPI_UWB_RADIOSSW	= 0x02,	/* UWB radio enabled */
};

#define TPACPI_RFK_UWB_SW_NAME	"tpacpi_uwb_sw"

static int uwb_get_status(void)
{
	int status;

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_uwbemul)
		return (tpacpi_uwb_emulstate) ?
		       TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF;
#endif

	if (!acpi_evalf(hkey_handle, &status, "GUWB", "d"))
		return -EIO;

	return ((status & TP_ACPI_UWB_RADIOSSW) != 0) ?
			TPACPI_RFK_RADIO_ON : TPACPI_RFK_RADIO_OFF;
}

static int uwb_set_status(enum tpacpi_rfkill_state state)
{
	int status;

	vdbg_printk(TPACPI_DBG_RFKILL,
		"will attempt to %s UWB\n",
		(state == TPACPI_RFK_RADIO_ON) ? "enable" : "disable");

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_uwbemul) {
		tpacpi_uwb_emulstate = (state == TPACPI_RFK_RADIO_ON);
		return 0;
	}
#endif

	if (state == TPACPI_RFK_RADIO_ON)
		status = TP_ACPI_UWB_RADIOSSW;
	else
		status = 0;

	if (!acpi_evalf(hkey_handle, NULL, "SUWB", "vd", status))
		return -EIO;

	return 0;
}

/* --------------------------------------------------------------------- */

static const struct tpacpi_rfk_ops uwb_tprfk_ops = {
	.get_status = uwb_get_status,
	.set_status = uwb_set_status,
};

static void uwb_exit(void)
{
	tpacpi_destroy_rfkill(TPACPI_RFK_UWB_SW_ID);
}

static int __init uwb_init(struct ibm_init_struct *iibm)
{
	int res;
	int status = 0;

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_RFKILL,
			"initializing uwb subdriver\n");

	TPACPI_ACPIHANDLE_INIT(hkey);

	tp_features.uwb = hkey_handle &&
	    acpi_evalf(hkey_handle, &status, "GUWB", "qd");

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_RFKILL,
		"uwb is %s, status 0x%02x\n",
		str_supported(tp_features.uwb),
		status);

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
	if (dbg_uwbemul) {
		tp_features.uwb = 1;
		pr_info("uwb switch emulation enabled\n");
	} else
#endif
	if (tp_features.uwb &&
	    !(status & TP_ACPI_UWB_HWPRESENT)) {
		/* no uwb hardware present in system */
		tp_features.uwb = 0;
		dbg_printk(TPACPI_DBG_INIT,
			   "uwb hardware not installed\n");
	}

	if (!tp_features.uwb)
		return 1;

	res = tpacpi_new_rfkill(TPACPI_RFK_UWB_SW_ID,
				&uwb_tprfk_ops,
				RFKILL_TYPE_UWB,
				TPACPI_RFK_UWB_SW_NAME,
				false);
	return res;
}

static struct ibm_struct uwb_driver_data = {
	.name = "uwb",
	.exit = uwb_exit,
	.flags.experimental = 1,
};

/*************************************************************************
 * Video subdriver
 */

#ifdef CONFIG_THINKPAD_ACPI_VIDEO

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

static int video_autosw_get(void);
static int video_autosw_set(int enable);

TPACPI_HANDLE(vid, root,
	      "\\_SB.PCI.AGP.VGA",	/* 570 */
	      "\\_SB.PCI0.AGP0.VID0",	/* 600e/x, 770x */
	      "\\_SB.PCI0.VID0",	/* 770e */
	      "\\_SB.PCI0.VID",		/* A21e, G4x, R50e, X30, X40 */
	      "\\_SB.PCI0.AGP.VGA",	/* X100e and a few others */
	      "\\_SB.PCI0.AGP.VID",	/* all others */
	);				/* R30, R31 */

TPACPI_HANDLE(vid2, root, "\\_SB.PCI0.AGPB.VID");	/* G41 */

static int __init video_init(struct ibm_init_struct *iibm)
{
	int ivga;

	vdbg_printk(TPACPI_DBG_INIT, "initializing video subdriver\n");

	TPACPI_ACPIHANDLE_INIT(vid);
	if (tpacpi_is_ibm())
		TPACPI_ACPIHANDLE_INIT(vid2);

	if (vid2_handle && acpi_evalf(NULL, &ivga, "\\IVGA", "d") && ivga)
		/* G41, assume IVGA doesn't change */
		vid_handle = vid2_handle;

	if (!vid_handle)
		/* video switching not supported on R30, R31 */
		video_supported = TPACPI_VIDEO_NONE;
	else if (tpacpi_is_ibm() &&
		 acpi_evalf(vid_handle, &video_orig_autosw, "SWIT", "qd"))
		/* 570 */
		video_supported = TPACPI_VIDEO_570;
	else if (tpacpi_is_ibm() &&
		 acpi_evalf(vid_handle, &video_orig_autosw, "^VADL", "qd"))
		/* 600e/x, 770e, 770x */
		video_supported = TPACPI_VIDEO_770;
	else
		/* all others */
		video_supported = TPACPI_VIDEO_NEW;

	vdbg_printk(TPACPI_DBG_INIT, "video is %s, mode %d\n",
		str_supported(video_supported != TPACPI_VIDEO_NONE),
		video_supported);

	return (video_supported != TPACPI_VIDEO_NONE)? 0 : 1;
}

static void video_exit(void)
{
	dbg_printk(TPACPI_DBG_EXIT,
		   "restoring original video autoswitch mode\n");
	if (video_autosw_set(video_orig_autosw))
		pr_err("error while trying to restore original "
			"video autoswitch mode\n");
}

static int video_outputsw_get(void)
{
	int status = 0;
	int i;

	switch (video_supported) {
	case TPACPI_VIDEO_570:
		if (!acpi_evalf(NULL, &i, "\\_SB.PHS", "dd",
				 TP_ACPI_VIDEO_570_PHSCMD))
			return -EIO;
		status = i & TP_ACPI_VIDEO_570_PHSMASK;
		break;
	case TPACPI_VIDEO_770:
		if (!acpi_evalf(NULL, &i, "\\VCDL", "d"))
			return -EIO;
		if (i)
			status |= TP_ACPI_VIDEO_S_LCD;
		if (!acpi_evalf(NULL, &i, "\\VCDC", "d"))
			return -EIO;
		if (i)
			status |= TP_ACPI_VIDEO_S_CRT;
		break;
	case TPACPI_VIDEO_NEW:
		if (!acpi_evalf(NULL, NULL, "\\VUPS", "vd", 1) ||
		    !acpi_evalf(NULL, &i, "\\VCDC", "d"))
			return -EIO;
		if (i)
			status |= TP_ACPI_VIDEO_S_CRT;

		if (!acpi_evalf(NULL, NULL, "\\VUPS", "vd", 0) ||
		    !acpi_evalf(NULL, &i, "\\VCDL", "d"))
			return -EIO;
		if (i)
			status |= TP_ACPI_VIDEO_S_LCD;
		if (!acpi_evalf(NULL, &i, "\\VCDD", "d"))
			return -EIO;
		if (i)
			status |= TP_ACPI_VIDEO_S_DVI;
		break;
	default:
		return -ENOSYS;
	}

	return status;
}

static int video_outputsw_set(int status)
{
	int autosw;
	int res = 0;

	switch (video_supported) {
	case TPACPI_VIDEO_570:
		res = acpi_evalf(NULL, NULL,
				 "\\_SB.PHS2", "vdd",
				 TP_ACPI_VIDEO_570_PHS2CMD,
				 status | TP_ACPI_VIDEO_570_PHS2SET);
		break;
	case TPACPI_VIDEO_770:
		autosw = video_autosw_get();
		if (autosw < 0)
			return autosw;

		res = video_autosw_set(1);
		if (res)
			return res;
		res = acpi_evalf(vid_handle, NULL,
				 "ASWT", "vdd", status * 0x100, 0);
		if (!autosw && video_autosw_set(autosw)) {
			pr_err("video auto-switch left enabled due to error\n");
			return -EIO;
		}
		break;
	case TPACPI_VIDEO_NEW:
		res = acpi_evalf(NULL, NULL, "\\VUPS", "vd", 0x80) &&
		      acpi_evalf(NULL, NULL, "\\VSDS", "vdd", status, 1);
		break;
	default:
		return -ENOSYS;
	}

	return (res)? 0 : -EIO;
}

static int video_autosw_get(void)
{
	int autosw = 0;

	switch (video_supported) {
	case TPACPI_VIDEO_570:
		if (!acpi_evalf(vid_handle, &autosw, "SWIT", "d"))
			return -EIO;
		break;
	case TPACPI_VIDEO_770:
	case TPACPI_VIDEO_NEW:
		if (!acpi_evalf(vid_handle, &autosw, "^VDEE", "d"))
			return -EIO;
		break;
	default:
		return -ENOSYS;
	}

	return autosw & 1;
}

static int video_autosw_set(int enable)
{
	if (!acpi_evalf(vid_handle, NULL, "_DOS", "vd", (enable)? 1 : 0))
		return -EIO;
	return 0;
}

static int video_outputsw_cycle(void)
{
	int autosw = video_autosw_get();
	int res;

	if (autosw < 0)
		return autosw;

	switch (video_supported) {
	case TPACPI_VIDEO_570:
		res = video_autosw_set(1);
		if (res)
			return res;
		res = acpi_evalf(ec_handle, NULL, "_Q16", "v");
		break;
	case TPACPI_VIDEO_770:
	case TPACPI_VIDEO_NEW:
		res = video_autosw_set(1);
		if (res)
			return res;
		res = acpi_evalf(vid_handle, NULL, "VSWT", "v");
		break;
	default:
		return -ENOSYS;
	}
	if (!autosw && video_autosw_set(autosw)) {
		pr_err("video auto-switch left enabled due to error\n");
		return -EIO;
	}

	return (res)? 0 : -EIO;
}

static int video_expand_toggle(void)
{
	switch (video_supported) {
	case TPACPI_VIDEO_570:
		return acpi_evalf(ec_handle, NULL, "_Q17", "v")?
			0 : -EIO;
	case TPACPI_VIDEO_770:
		return acpi_evalf(vid_handle, NULL, "VEXP", "v")?
			0 : -EIO;
	case TPACPI_VIDEO_NEW:
		return acpi_evalf(NULL, NULL, "\\VEXP", "v")?
			0 : -EIO;
	default:
		return -ENOSYS;
	}
	/* not reached */
}

static int video_read(struct seq_file *m)
{
	int status, autosw;

	if (video_supported == TPACPI_VIDEO_NONE) {
		seq_printf(m, "status:\t\tnot supported\n");
		return 0;
	}

	/* Even reads can crash X.org, so... */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	status = video_outputsw_get();
	if (status < 0)
		return status;

	autosw = video_autosw_get();
	if (autosw < 0)
		return autosw;

	seq_printf(m, "status:\t\tsupported\n");
	seq_printf(m, "lcd:\t\t%s\n", enabled(status, 0));
	seq_printf(m, "crt:\t\t%s\n", enabled(status, 1));
	if (video_supported == TPACPI_VIDEO_NEW)
		seq_printf(m, "dvi:\t\t%s\n", enabled(status, 3));
	seq_printf(m, "auto:\t\t%s\n", enabled(autosw, 0));
	seq_printf(m, "commands:\tlcd_enable, lcd_disable\n");
	seq_printf(m, "commands:\tcrt_enable, crt_disable\n");
	if (video_supported == TPACPI_VIDEO_NEW)
		seq_printf(m, "commands:\tdvi_enable, dvi_disable\n");
	seq_printf(m, "commands:\tauto_enable, auto_disable\n");
	seq_printf(m, "commands:\tvideo_switch, expand_toggle\n");

	return 0;
}

static int video_write(char *buf)
{
	char *cmd;
	int enable, disable, status;
	int res;

	if (video_supported == TPACPI_VIDEO_NONE)
		return -ENODEV;

	/* Even reads can crash X.org, let alone writes... */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	enable = 0;
	disable = 0;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "lcd_enable") == 0) {
			enable |= TP_ACPI_VIDEO_S_LCD;
		} else if (strlencmp(cmd, "lcd_disable") == 0) {
			disable |= TP_ACPI_VIDEO_S_LCD;
		} else if (strlencmp(cmd, "crt_enable") == 0) {
			enable |= TP_ACPI_VIDEO_S_CRT;
		} else if (strlencmp(cmd, "crt_disable") == 0) {
			disable |= TP_ACPI_VIDEO_S_CRT;
		} else if (video_supported == TPACPI_VIDEO_NEW &&
			   strlencmp(cmd, "dvi_enable") == 0) {
			enable |= TP_ACPI_VIDEO_S_DVI;
		} else if (video_supported == TPACPI_VIDEO_NEW &&
			   strlencmp(cmd, "dvi_disable") == 0) {
			disable |= TP_ACPI_VIDEO_S_DVI;
		} else if (strlencmp(cmd, "auto_enable") == 0) {
			res = video_autosw_set(1);
			if (res)
				return res;
		} else if (strlencmp(cmd, "auto_disable") == 0) {
			res = video_autosw_set(0);
			if (res)
				return res;
		} else if (strlencmp(cmd, "video_switch") == 0) {
			res = video_outputsw_cycle();
			if (res)
				return res;
		} else if (strlencmp(cmd, "expand_toggle") == 0) {
			res = video_expand_toggle();
			if (res)
				return res;
		} else
			return -EINVAL;
	}

	if (enable || disable) {
		status = video_outputsw_get();
		if (status < 0)
			return status;
		res = video_outputsw_set((status & ~disable) | enable);
		if (res)
			return res;
	}

	return 0;
}

static struct ibm_struct video_driver_data = {
	.name = "video",
	.read = video_read,
	.write = video_write,
	.exit = video_exit,
};

#endif /* CONFIG_THINKPAD_ACPI_VIDEO */

/*************************************************************************
 * Light (thinklight) subdriver
 */

TPACPI_HANDLE(lght, root, "\\LGHT");	/* A21e, A2xm/p, T20-22, X20-21 */
TPACPI_HANDLE(ledb, ec, "LEDB");		/* G4x */

static int light_get_status(void)
{
	int status = 0;

	if (tp_features.light_status) {
		if (!acpi_evalf(ec_handle, &status, "KBLT", "d"))
			return -EIO;
		return (!!status);
	}

	return -ENXIO;
}

static int light_set_status(int status)
{
	int rc;

	if (tp_features.light) {
		if (cmos_handle) {
			rc = acpi_evalf(cmos_handle, NULL, NULL, "vd",
					(status)?
						TP_CMOS_THINKLIGHT_ON :
						TP_CMOS_THINKLIGHT_OFF);
		} else {
			rc = acpi_evalf(lght_handle, NULL, NULL, "vd",
					(status)? 1 : 0);
		}
		return (rc)? 0 : -EIO;
	}

	return -ENXIO;
}

static void light_set_status_worker(struct work_struct *work)
{
	struct tpacpi_led_classdev *data =
			container_of(work, struct tpacpi_led_classdev, work);

	if (likely(tpacpi_lifecycle == TPACPI_LIFE_RUNNING))
		light_set_status((data->new_state != TPACPI_LED_OFF));
}

static void light_sysfs_set(struct led_classdev *led_cdev,
			enum led_brightness brightness)
{
	struct tpacpi_led_classdev *data =
		container_of(led_cdev,
			     struct tpacpi_led_classdev,
			     led_classdev);
	data->new_state = (brightness != LED_OFF) ?
				TPACPI_LED_ON : TPACPI_LED_OFF;
	queue_work(tpacpi_wq, &data->work);
}

static enum led_brightness light_sysfs_get(struct led_classdev *led_cdev)
{
	return (light_get_status() == 1)? LED_FULL : LED_OFF;
}

static struct tpacpi_led_classdev tpacpi_led_thinklight = {
	.led_classdev = {
		.name		= "tpacpi::thinklight",
		.brightness_set	= &light_sysfs_set,
		.brightness_get	= &light_sysfs_get,
	}
};

static int __init light_init(struct ibm_init_struct *iibm)
{
	int rc;

	vdbg_printk(TPACPI_DBG_INIT, "initializing light subdriver\n");

	if (tpacpi_is_ibm()) {
		TPACPI_ACPIHANDLE_INIT(ledb);
		TPACPI_ACPIHANDLE_INIT(lght);
	}
	TPACPI_ACPIHANDLE_INIT(cmos);
	INIT_WORK(&tpacpi_led_thinklight.work, light_set_status_worker);

	/* light not supported on 570, 600e/x, 770e, 770x, G4x, R30, R31 */
	tp_features.light = (cmos_handle || lght_handle) && !ledb_handle;

	if (tp_features.light)
		/* light status not supported on
		   570, 600e/x, 770e, 770x, G4x, R30, R31, R32, X20 */
		tp_features.light_status =
			acpi_evalf(ec_handle, NULL, "KBLT", "qv");

	vdbg_printk(TPACPI_DBG_INIT, "light is %s, light status is %s\n",
		str_supported(tp_features.light),
		str_supported(tp_features.light_status));

	if (!tp_features.light)
		return 1;

	rc = led_classdev_register(&tpacpi_pdev->dev,
				   &tpacpi_led_thinklight.led_classdev);

	if (rc < 0) {
		tp_features.light = 0;
		tp_features.light_status = 0;
	} else  {
		rc = 0;
	}

	return rc;
}

static void light_exit(void)
{
	led_classdev_unregister(&tpacpi_led_thinklight.led_classdev);
	flush_workqueue(tpacpi_wq);
}

static int light_read(struct seq_file *m)
{
	int status;

	if (!tp_features.light) {
		seq_printf(m, "status:\t\tnot supported\n");
	} else if (!tp_features.light_status) {
		seq_printf(m, "status:\t\tunknown\n");
		seq_printf(m, "commands:\ton, off\n");
	} else {
		status = light_get_status();
		if (status < 0)
			return status;
		seq_printf(m, "status:\t\t%s\n", onoff(status, 0));
		seq_printf(m, "commands:\ton, off\n");
	}

	return 0;
}

static int light_write(char *buf)
{
	char *cmd;
	int newstatus = 0;

	if (!tp_features.light)
		return -ENODEV;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "on") == 0) {
			newstatus = 1;
		} else if (strlencmp(cmd, "off") == 0) {
			newstatus = 0;
		} else
			return -EINVAL;
	}

	return light_set_status(newstatus);
}

static struct ibm_struct light_driver_data = {
	.name = "light",
	.read = light_read,
	.write = light_write,
	.exit = light_exit,
};

/*************************************************************************
 * CMOS subdriver
 */

/* sysfs cmos_command -------------------------------------------------- */
static ssize_t cmos_command_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned long cmos_cmd;
	int res;

	if (parse_strtoul(buf, 21, &cmos_cmd))
		return -EINVAL;

	res = issue_thinkpad_cmos_command(cmos_cmd);
	return (res)? res : count;
}

static struct device_attribute dev_attr_cmos_command =
	__ATTR(cmos_command, S_IWUSR, NULL, cmos_command_store);

/* --------------------------------------------------------------------- */

static int __init cmos_init(struct ibm_init_struct *iibm)
{
	int res;

	vdbg_printk(TPACPI_DBG_INIT,
		"initializing cmos commands subdriver\n");

	TPACPI_ACPIHANDLE_INIT(cmos);

	vdbg_printk(TPACPI_DBG_INIT, "cmos commands are %s\n",
		str_supported(cmos_handle != NULL));

	res = device_create_file(&tpacpi_pdev->dev, &dev_attr_cmos_command);
	if (res)
		return res;

	return (cmos_handle)? 0 : 1;
}

static void cmos_exit(void)
{
	device_remove_file(&tpacpi_pdev->dev, &dev_attr_cmos_command);
}

static int cmos_read(struct seq_file *m)
{
	/* cmos not supported on 570, 600e/x, 770e, 770x, A21e, A2xm/p,
	   R30, R31, T20-22, X20-21 */
	if (!cmos_handle)
		seq_printf(m, "status:\t\tnot supported\n");
	else {
		seq_printf(m, "status:\t\tsupported\n");
		seq_printf(m, "commands:\t<cmd> (<cmd> is 0-21)\n");
	}

	return 0;
}

static int cmos_write(char *buf)
{
	char *cmd;
	int cmos_cmd, res;

	while ((cmd = next_cmd(&buf))) {
		if (sscanf(cmd, "%u", &cmos_cmd) == 1 &&
		    cmos_cmd >= 0 && cmos_cmd <= 21) {
			/* cmos_cmd set */
		} else
			return -EINVAL;

		res = issue_thinkpad_cmos_command(cmos_cmd);
		if (res)
			return res;
	}

	return 0;
}

static struct ibm_struct cmos_driver_data = {
	.name = "cmos",
	.read = cmos_read,
	.write = cmos_write,
	.exit = cmos_exit,
};

/*************************************************************************
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

#define TPACPI_LED_NUMLEDS 16
static struct tpacpi_led_classdev *tpacpi_leds;
static enum led_status_t tpacpi_led_state_cache[TPACPI_LED_NUMLEDS];
static const char * const tpacpi_led_names[TPACPI_LED_NUMLEDS] = {
	/* there's a limit of 19 chars + NULL before 2.6.26 */
	"tpacpi::power",
	"tpacpi:orange:batt",
	"tpacpi:green:batt",
	"tpacpi::dock_active",
	"tpacpi::bay_active",
	"tpacpi::dock_batt",
	"tpacpi::unknown_led",
	"tpacpi::standby",
	"tpacpi::dock_status1",
	"tpacpi::dock_status2",
	"tpacpi::unknown_led2",
	"tpacpi::unknown_led3",
	"tpacpi::thinkvantage",
};
#define TPACPI_SAFE_LEDS	0x1081U

static inline bool tpacpi_is_led_restricted(const unsigned int led)
{
#ifdef CONFIG_THINKPAD_ACPI_UNSAFE_LEDS
	return false;
#else
	return (1U & (TPACPI_SAFE_LEDS >> led)) == 0;
#endif
}

static int led_get_status(const unsigned int led)
{
	int status;
	enum led_status_t led_s;

	switch (led_supported) {
	case TPACPI_LED_570:
		if (!acpi_evalf(ec_handle,
				&status, "GLED", "dd", 1 << led))
			return -EIO;
		led_s = (status == 0)?
				TPACPI_LED_OFF :
				((status == 1)?
					TPACPI_LED_ON :
					TPACPI_LED_BLINK);
		tpacpi_led_state_cache[led] = led_s;
		return led_s;
	default:
		return -ENXIO;
	}

	/* not reached */
}

static int led_set_status(const unsigned int led,
			  const enum led_status_t ledstatus)
{
	/* off, on, blink. Index is led_status_t */
	static const unsigned int led_sled_arg1[] = { 0, 1, 3 };
	static const unsigned int led_led_arg1[] = { 0, 0x80, 0xc0 };

	int rc = 0;

	switch (led_supported) {
	case TPACPI_LED_570:
		/* 570 */
		if (unlikely(led > 7))
			return -EINVAL;
		if (unlikely(tpacpi_is_led_restricted(led)))
			return -EPERM;
		if (!acpi_evalf(led_handle, NULL, NULL, "vdd",
				(1 << led), led_sled_arg1[ledstatus]))
			rc = -EIO;
		break;
	case TPACPI_LED_OLD:
		/* 600e/x, 770e, 770x, A21e, A2xm/p, T20-22, X20 */
		if (unlikely(led > 7))
			return -EINVAL;
		if (unlikely(tpacpi_is_led_restricted(led)))
			return -EPERM;
		rc = ec_write(TPACPI_LED_EC_HLMS, (1 << led));
		if (rc >= 0)
			rc = ec_write(TPACPI_LED_EC_HLBL,
				      (ledstatus == TPACPI_LED_BLINK) << led);
		if (rc >= 0)
			rc = ec_write(TPACPI_LED_EC_HLCL,
				      (ledstatus != TPACPI_LED_OFF) << led);
		break;
	case TPACPI_LED_NEW:
		/* all others */
		if (unlikely(led >= TPACPI_LED_NUMLEDS))
			return -EINVAL;
		if (unlikely(tpacpi_is_led_restricted(led)))
			return -EPERM;
		if (!acpi_evalf(led_handle, NULL, NULL, "vdd",
				led, led_led_arg1[ledstatus]))
			rc = -EIO;
		break;
	default:
		rc = -ENXIO;
	}

	if (!rc)
		tpacpi_led_state_cache[led] = ledstatus;

	return rc;
}

static void led_set_status_worker(struct work_struct *work)
{
	struct tpacpi_led_classdev *data =
		container_of(work, struct tpacpi_led_classdev, work);

	if (likely(tpacpi_lifecycle == TPACPI_LIFE_RUNNING))
		led_set_status(data->led, data->new_state);
}

static void led_sysfs_set(struct led_classdev *led_cdev,
			enum led_brightness brightness)
{
	struct tpacpi_led_classdev *data = container_of(led_cdev,
			     struct tpacpi_led_classdev, led_classdev);

	if (brightness == LED_OFF)
		data->new_state = TPACPI_LED_OFF;
	else if (tpacpi_led_state_cache[data->led] != TPACPI_LED_BLINK)
		data->new_state = TPACPI_LED_ON;
	else
		data->new_state = TPACPI_LED_BLINK;

	queue_work(tpacpi_wq, &data->work);
}

static int led_sysfs_blink_set(struct led_classdev *led_cdev,
			unsigned long *delay_on, unsigned long *delay_off)
{
	struct tpacpi_led_classdev *data = container_of(led_cdev,
			     struct tpacpi_led_classdev, led_classdev);

	/* Can we choose the flash rate? */
	if (*delay_on == 0 && *delay_off == 0) {
		/* yes. set them to the hardware blink rate (1 Hz) */
		*delay_on = 500; /* ms */
		*delay_off = 500; /* ms */
	} else if ((*delay_on != 500) || (*delay_off != 500))
		return -EINVAL;

	data->new_state = TPACPI_LED_BLINK;
	queue_work(tpacpi_wq, &data->work);

	return 0;
}

static enum led_brightness led_sysfs_get(struct led_classdev *led_cdev)
{
	int rc;

	struct tpacpi_led_classdev *data = container_of(led_cdev,
			     struct tpacpi_led_classdev, led_classdev);

	rc = led_get_status(data->led);

	if (rc == TPACPI_LED_OFF || rc < 0)
		rc = LED_OFF;	/* no error handling in led class :( */
	else
		rc = LED_FULL;

	return rc;
}

static void led_exit(void)
{
	unsigned int i;

	for (i = 0; i < TPACPI_LED_NUMLEDS; i++) {
		if (tpacpi_leds[i].led_classdev.name)
			led_classdev_unregister(&tpacpi_leds[i].led_classdev);
	}

	flush_workqueue(tpacpi_wq);
	kfree(tpacpi_leds);
}

static int __init tpacpi_init_led(unsigned int led)
{
	int rc;

	tpacpi_leds[led].led = led;

	/* LEDs with no name don't get registered */
	if (!tpacpi_led_names[led])
		return 0;

	tpacpi_leds[led].led_classdev.brightness_set = &led_sysfs_set;
	tpacpi_leds[led].led_classdev.blink_set = &led_sysfs_blink_set;
	if (led_supported == TPACPI_LED_570)
		tpacpi_leds[led].led_classdev.brightness_get =
						&led_sysfs_get;

	tpacpi_leds[led].led_classdev.name = tpacpi_led_names[led];

	INIT_WORK(&tpacpi_leds[led].work, led_set_status_worker);

	rc = led_classdev_register(&tpacpi_pdev->dev,
				&tpacpi_leds[led].led_classdev);
	if (rc < 0)
		tpacpi_leds[led].led_classdev.name = NULL;

	return rc;
}

static const struct tpacpi_quirk led_useful_qtable[] __initconst = {
	TPACPI_Q_IBM('1', 'E', 0x009f), /* A30 */
	TPACPI_Q_IBM('1', 'N', 0x009f), /* A31 */
	TPACPI_Q_IBM('1', 'G', 0x009f), /* A31 */

	TPACPI_Q_IBM('1', 'I', 0x0097), /* T30 */
	TPACPI_Q_IBM('1', 'R', 0x0097), /* T40, T41, T42, R50, R51 */
	TPACPI_Q_IBM('7', '0', 0x0097), /* T43, R52 */
	TPACPI_Q_IBM('1', 'Y', 0x0097), /* T43 */
	TPACPI_Q_IBM('1', 'W', 0x0097), /* R50e */
	TPACPI_Q_IBM('1', 'V', 0x0097), /* R51 */
	TPACPI_Q_IBM('7', '8', 0x0097), /* R51e */
	TPACPI_Q_IBM('7', '6', 0x0097), /* R52 */

	TPACPI_Q_IBM('1', 'K', 0x00bf), /* X30 */
	TPACPI_Q_IBM('1', 'Q', 0x00bf), /* X31, X32 */
	TPACPI_Q_IBM('1', 'U', 0x00bf), /* X40 */
	TPACPI_Q_IBM('7', '4', 0x00bf), /* X41 */
	TPACPI_Q_IBM('7', '5', 0x00bf), /* X41t */

	TPACPI_Q_IBM('7', '9', 0x1f97), /* T60 (1) */
	TPACPI_Q_IBM('7', '7', 0x1f97), /* Z60* (1) */
	TPACPI_Q_IBM('7', 'F', 0x1f97), /* Z61* (1) */
	TPACPI_Q_IBM('7', 'B', 0x1fb7), /* X60 (1) */

	/* (1) - may have excess leds enabled on MSB */

	/* Defaults (order matters, keep last, don't reorder!) */
	{ /* Lenovo */
	  .vendor = PCI_VENDOR_ID_LENOVO,
	  .bios = TPACPI_MATCH_ANY, .ec = TPACPI_MATCH_ANY,
	  .quirks = 0x1fffU,
	},
	{ /* IBM ThinkPads with no EC version string */
	  .vendor = PCI_VENDOR_ID_IBM,
	  .bios = TPACPI_MATCH_ANY, .ec = TPACPI_MATCH_UNKNOWN,
	  .quirks = 0x00ffU,
	},
	{ /* IBM ThinkPads with EC version string */
	  .vendor = PCI_VENDOR_ID_IBM,
	  .bios = TPACPI_MATCH_ANY, .ec = TPACPI_MATCH_ANY,
	  .quirks = 0x00bfU,
	},
};

#undef TPACPI_LEDQ_IBM
#undef TPACPI_LEDQ_LNV

static enum led_access_mode __init led_init_detect_mode(void)
{
	acpi_status status;

	if (tpacpi_is_ibm()) {
		/* 570 */
		status = acpi_get_handle(ec_handle, "SLED", &led_handle);
		if (ACPI_SUCCESS(status))
			return TPACPI_LED_570;

		/* 600e/x, 770e, 770x, A21e, A2xm/p, T20-22, X20-21 */
		status = acpi_get_handle(ec_handle, "SYSL", &led_handle);
		if (ACPI_SUCCESS(status))
			return TPACPI_LED_OLD;
	}

	/* most others */
	status = acpi_get_handle(ec_handle, "LED", &led_handle);
	if (ACPI_SUCCESS(status))
		return TPACPI_LED_NEW;

	/* R30, R31, and unknown firmwares */
	led_handle = NULL;
	return TPACPI_LED_NONE;
}

static int __init led_init(struct ibm_init_struct *iibm)
{
	unsigned int i;
	int rc;
	unsigned long useful_leds;

	vdbg_printk(TPACPI_DBG_INIT, "initializing LED subdriver\n");

	led_supported = led_init_detect_mode();

	if (led_supported != TPACPI_LED_NONE) {
		useful_leds = tpacpi_check_quirks(led_useful_qtable,
				ARRAY_SIZE(led_useful_qtable));

		if (!useful_leds) {
			led_handle = NULL;
			led_supported = TPACPI_LED_NONE;
		}
	}

	vdbg_printk(TPACPI_DBG_INIT, "LED commands are %s, mode %d\n",
		str_supported(led_supported), led_supported);

	if (led_supported == TPACPI_LED_NONE)
		return 1;

	tpacpi_leds = kzalloc(sizeof(*tpacpi_leds) * TPACPI_LED_NUMLEDS,
			      GFP_KERNEL);
	if (!tpacpi_leds) {
		pr_err("Out of memory for LED data\n");
		return -ENOMEM;
	}

	for (i = 0; i < TPACPI_LED_NUMLEDS; i++) {
		tpacpi_leds[i].led = -1;

		if (!tpacpi_is_led_restricted(i) &&
		    test_bit(i, &useful_leds)) {
			rc = tpacpi_init_led(i);
			if (rc < 0) {
				led_exit();
				return rc;
			}
		}
	}

#ifdef CONFIG_THINKPAD_ACPI_UNSAFE_LEDS
	pr_notice("warning: userspace override of important "
		  "firmware LEDs is enabled\n");
#endif
	return 0;
}

#define str_led_status(s) \
	((s) == TPACPI_LED_OFF ? "off" : \
		((s) == TPACPI_LED_ON ? "on" : "blinking"))

static int led_read(struct seq_file *m)
{
	if (!led_supported) {
		seq_printf(m, "status:\t\tnot supported\n");
		return 0;
	}
	seq_printf(m, "status:\t\tsupported\n");

	if (led_supported == TPACPI_LED_570) {
		/* 570 */
		int i, status;
		for (i = 0; i < 8; i++) {
			status = led_get_status(i);
			if (status < 0)
				return -EIO;
			seq_printf(m, "%d:\t\t%s\n",
				       i, str_led_status(status));
		}
	}

	seq_printf(m, "commands:\t"
		       "<led> on, <led> off, <led> blink (<led> is 0-15)\n");

	return 0;
}

static int led_write(char *buf)
{
	char *cmd;
	int led, rc;
	enum led_status_t s;

	if (!led_supported)
		return -ENODEV;

	while ((cmd = next_cmd(&buf))) {
		if (sscanf(cmd, "%d", &led) != 1)
			return -EINVAL;

		if (led < 0 || led > (TPACPI_LED_NUMLEDS - 1) ||
				tpacpi_leds[led].led < 0)
			return -ENODEV;

		if (strstr(cmd, "off")) {
			s = TPACPI_LED_OFF;
		} else if (strstr(cmd, "on")) {
			s = TPACPI_LED_ON;
		} else if (strstr(cmd, "blink")) {
			s = TPACPI_LED_BLINK;
		} else {
			return -EINVAL;
		}

		rc = led_set_status(led, s);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static struct ibm_struct led_driver_data = {
	.name = "led",
	.read = led_read,
	.write = led_write,
	.exit = led_exit,
};

/*************************************************************************
 * Beep subdriver
 */

TPACPI_HANDLE(beep, ec, "BEEP");	/* all except R30, R31 */

#define TPACPI_BEEP_Q1 0x0001

static const struct tpacpi_quirk beep_quirk_table[] __initconst = {
	TPACPI_Q_IBM('I', 'M', TPACPI_BEEP_Q1), /* 570 */
	TPACPI_Q_IBM('I', 'U', TPACPI_BEEP_Q1), /* 570E - unverified */
};

static int __init beep_init(struct ibm_init_struct *iibm)
{
	unsigned long quirks;

	vdbg_printk(TPACPI_DBG_INIT, "initializing beep subdriver\n");

	TPACPI_ACPIHANDLE_INIT(beep);

	vdbg_printk(TPACPI_DBG_INIT, "beep is %s\n",
		str_supported(beep_handle != NULL));

	quirks = tpacpi_check_quirks(beep_quirk_table,
				     ARRAY_SIZE(beep_quirk_table));

	tp_features.beep_needs_two_args = !!(quirks & TPACPI_BEEP_Q1);

	return (beep_handle)? 0 : 1;
}

static int beep_read(struct seq_file *m)
{
	if (!beep_handle)
		seq_printf(m, "status:\t\tnot supported\n");
	else {
		seq_printf(m, "status:\t\tsupported\n");
		seq_printf(m, "commands:\t<cmd> (<cmd> is 0-17)\n");
	}

	return 0;
}

static int beep_write(char *buf)
{
	char *cmd;
	int beep_cmd;

	if (!beep_handle)
		return -ENODEV;

	while ((cmd = next_cmd(&buf))) {
		if (sscanf(cmd, "%u", &beep_cmd) == 1 &&
		    beep_cmd >= 0 && beep_cmd <= 17) {
			/* beep_cmd set */
		} else
			return -EINVAL;
		if (tp_features.beep_needs_two_args) {
			if (!acpi_evalf(beep_handle, NULL, NULL, "vdd",
					beep_cmd, 0))
				return -EIO;
		} else {
			if (!acpi_evalf(beep_handle, NULL, NULL, "vd",
					beep_cmd))
				return -EIO;
		}
	}

	return 0;
}

static struct ibm_struct beep_driver_data = {
	.name = "beep",
	.read = beep_read,
	.write = beep_write,
};

/*************************************************************************
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

	TPACPI_THERMAL_SENSOR_NA = -128000, /* Sensor not available */
};


#define TPACPI_MAX_THERMAL_SENSORS 16	/* Max thermal sensors supported */
struct ibm_thermal_sensors_struct {
	s32 temp[TPACPI_MAX_THERMAL_SENSORS];
};

static enum thermal_access_mode thermal_read_mode;

/* idx is zero-based */
static int thermal_get_sensor(int idx, s32 *value)
{
	int t;
	s8 tmp;
	char tmpi[5];

	t = TP_EC_THERMAL_TMP0;

	switch (thermal_read_mode) {
#if TPACPI_MAX_THERMAL_SENSORS >= 16
	case TPACPI_THERMAL_TPEC_16:
		if (idx >= 8 && idx <= 15) {
			t = TP_EC_THERMAL_TMP8;
			idx -= 8;
		}
		/* fallthrough */
#endif
	case TPACPI_THERMAL_TPEC_8:
		if (idx <= 7) {
			if (!acpi_ec_read(t + idx, &tmp))
				return -EIO;
			*value = tmp * 1000;
			return 0;
		}
		break;

	case TPACPI_THERMAL_ACPI_UPDT:
		if (idx <= 7) {
			snprintf(tmpi, sizeof(tmpi), "TMP%c", '0' + idx);
			if (!acpi_evalf(ec_handle, NULL, "UPDT", "v"))
				return -EIO;
			if (!acpi_evalf(ec_handle, &t, tmpi, "d"))
				return -EIO;
			*value = (t - 2732) * 100;
			return 0;
		}
		break;

	case TPACPI_THERMAL_ACPI_TMP07:
		if (idx <= 7) {
			snprintf(tmpi, sizeof(tmpi), "TMP%c", '0' + idx);
			if (!acpi_evalf(ec_handle, &t, tmpi, "d"))
				return -EIO;
			if (t > 127 || t < -127)
				t = TP_EC_THERMAL_TMP_NA;
			*value = t * 1000;
			return 0;
		}
		break;

	case TPACPI_THERMAL_NONE:
	default:
		return -ENOSYS;
	}

	return -EINVAL;
}

static int thermal_get_sensors(struct ibm_thermal_sensors_struct *s)
{
	int res, i;
	int n;

	n = 8;
	i = 0;

	if (!s)
		return -EINVAL;

	if (thermal_read_mode == TPACPI_THERMAL_TPEC_16)
		n = 16;

	for (i = 0 ; i < n; i++) {
		res = thermal_get_sensor(i, &s->temp[i]);
		if (res)
			return res;
	}

	return n;
}

static void thermal_dump_all_sensors(void)
{
	int n, i;
	struct ibm_thermal_sensors_struct t;

	n = thermal_get_sensors(&t);
	if (n <= 0)
		return;

	pr_notice("temperatures (Celsius):");

	for (i = 0; i < n; i++) {
		if (t.temp[i] != TPACPI_THERMAL_SENSOR_NA)
			pr_cont(" %d", (int)(t.temp[i] / 1000));
		else
			pr_cont(" N/A");
	}

	pr_cont("\n");
}

/* sysfs temp##_input -------------------------------------------------- */

static ssize_t thermal_temp_input_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct sensor_device_attribute *sensor_attr =
					to_sensor_dev_attr(attr);
	int idx = sensor_attr->index;
	s32 value;
	int res;

	res = thermal_get_sensor(idx, &value);
	if (res)
		return res;
	if (value == TPACPI_THERMAL_SENSOR_NA)
		return -ENXIO;

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

#define THERMAL_SENSOR_ATTR_TEMP(_idxA, _idxB) \
	 SENSOR_ATTR(temp##_idxA##_input, S_IRUGO, \
		     thermal_temp_input_show, NULL, _idxB)

static struct sensor_device_attribute sensor_dev_attr_thermal_temp_input[] = {
	THERMAL_SENSOR_ATTR_TEMP(1, 0),
	THERMAL_SENSOR_ATTR_TEMP(2, 1),
	THERMAL_SENSOR_ATTR_TEMP(3, 2),
	THERMAL_SENSOR_ATTR_TEMP(4, 3),
	THERMAL_SENSOR_ATTR_TEMP(5, 4),
	THERMAL_SENSOR_ATTR_TEMP(6, 5),
	THERMAL_SENSOR_ATTR_TEMP(7, 6),
	THERMAL_SENSOR_ATTR_TEMP(8, 7),
	THERMAL_SENSOR_ATTR_TEMP(9, 8),
	THERMAL_SENSOR_ATTR_TEMP(10, 9),
	THERMAL_SENSOR_ATTR_TEMP(11, 10),
	THERMAL_SENSOR_ATTR_TEMP(12, 11),
	THERMAL_SENSOR_ATTR_TEMP(13, 12),
	THERMAL_SENSOR_ATTR_TEMP(14, 13),
	THERMAL_SENSOR_ATTR_TEMP(15, 14),
	THERMAL_SENSOR_ATTR_TEMP(16, 15),
};

#define THERMAL_ATTRS(X) \
	&sensor_dev_attr_thermal_temp_input[X].dev_attr.attr

static struct attribute *thermal_temp_input_attr[] = {
	THERMAL_ATTRS(8),
	THERMAL_ATTRS(9),
	THERMAL_ATTRS(10),
	THERMAL_ATTRS(11),
	THERMAL_ATTRS(12),
	THERMAL_ATTRS(13),
	THERMAL_ATTRS(14),
	THERMAL_ATTRS(15),
	THERMAL_ATTRS(0),
	THERMAL_ATTRS(1),
	THERMAL_ATTRS(2),
	THERMAL_ATTRS(3),
	THERMAL_ATTRS(4),
	THERMAL_ATTRS(5),
	THERMAL_ATTRS(6),
	THERMAL_ATTRS(7),
	NULL
};

static const struct attribute_group thermal_temp_input16_group = {
	.attrs = thermal_temp_input_attr
};

static const struct attribute_group thermal_temp_input8_group = {
	.attrs = &thermal_temp_input_attr[8]
};

#undef THERMAL_SENSOR_ATTR_TEMP
#undef THERMAL_ATTRS

/* --------------------------------------------------------------------- */

static int __init thermal_init(struct ibm_init_struct *iibm)
{
	u8 t, ta1, ta2;
	int i;
	int acpi_tmp7;
	int res;

	vdbg_printk(TPACPI_DBG_INIT, "initializing thermal subdriver\n");

	acpi_tmp7 = acpi_evalf(ec_handle, NULL, "TMP7", "qv");

	if (thinkpad_id.ec_model) {
		/*
		 * Direct EC access mode: sensors at registers
		 * 0x78-0x7F, 0xC0-0xC7.  Registers return 0x00 for
		 * non-implemented, thermal sensors return 0x80 when
		 * not available
		 */

		ta1 = ta2 = 0;
		for (i = 0; i < 8; i++) {
			if (acpi_ec_read(TP_EC_THERMAL_TMP0 + i, &t)) {
				ta1 |= t;
			} else {
				ta1 = 0;
				break;
			}
			if (acpi_ec_read(TP_EC_THERMAL_TMP8 + i, &t)) {
				ta2 |= t;
			} else {
				ta1 = 0;
				break;
			}
		}
		if (ta1 == 0) {
			/* This is sheer paranoia, but we handle it anyway */
			if (acpi_tmp7) {
				pr_err("ThinkPad ACPI EC access misbehaving, "
				       "falling back to ACPI TMPx access "
				       "mode\n");
				thermal_read_mode = TPACPI_THERMAL_ACPI_TMP07;
			} else {
				pr_err("ThinkPad ACPI EC access misbehaving, "
				       "disabling thermal sensors access\n");
				thermal_read_mode = TPACPI_THERMAL_NONE;
			}
		} else {
			thermal_read_mode =
			    (ta2 != 0) ?
			    TPACPI_THERMAL_TPEC_16 : TPACPI_THERMAL_TPEC_8;
		}
	} else if (acpi_tmp7) {
		if (tpacpi_is_ibm() &&
		    acpi_evalf(ec_handle, NULL, "UPDT", "qv")) {
			/* 600e/x, 770e, 770x */
			thermal_read_mode = TPACPI_THERMAL_ACPI_UPDT;
		} else {
			/* IBM/LENOVO DSDT EC.TMPx access, max 8 sensors */
			thermal_read_mode = TPACPI_THERMAL_ACPI_TMP07;
		}
	} else {
		/* temperatures not supported on 570, G4x, R30, R31, R32 */
		thermal_read_mode = TPACPI_THERMAL_NONE;
	}

	vdbg_printk(TPACPI_DBG_INIT, "thermal is %s, mode %d\n",
		str_supported(thermal_read_mode != TPACPI_THERMAL_NONE),
		thermal_read_mode);

	switch (thermal_read_mode) {
	case TPACPI_THERMAL_TPEC_16:
		res = sysfs_create_group(&tpacpi_sensors_pdev->dev.kobj,
				&thermal_temp_input16_group);
		if (res)
			return res;
		break;
	case TPACPI_THERMAL_TPEC_8:
	case TPACPI_THERMAL_ACPI_TMP07:
	case TPACPI_THERMAL_ACPI_UPDT:
		res = sysfs_create_group(&tpacpi_sensors_pdev->dev.kobj,
				&thermal_temp_input8_group);
		if (res)
			return res;
		break;
	case TPACPI_THERMAL_NONE:
	default:
		return 1;
	}

	return 0;
}

static void thermal_exit(void)
{
	switch (thermal_read_mode) {
	case TPACPI_THERMAL_TPEC_16:
		sysfs_remove_group(&tpacpi_sensors_pdev->dev.kobj,
				   &thermal_temp_input16_group);
		break;
	case TPACPI_THERMAL_TPEC_8:
	case TPACPI_THERMAL_ACPI_TMP07:
	case TPACPI_THERMAL_ACPI_UPDT:
		sysfs_remove_group(&tpacpi_sensors_pdev->dev.kobj,
				   &thermal_temp_input8_group);
		break;
	case TPACPI_THERMAL_NONE:
	default:
		break;
	}
}

static int thermal_read(struct seq_file *m)
{
	int n, i;
	struct ibm_thermal_sensors_struct t;

	n = thermal_get_sensors(&t);
	if (unlikely(n < 0))
		return n;

	seq_printf(m, "temperatures:\t");

	if (n > 0) {
		for (i = 0; i < (n - 1); i++)
			seq_printf(m, "%d ", t.temp[i] / 1000);
		seq_printf(m, "%d\n", t.temp[i] / 1000);
	} else
		seq_printf(m, "not supported\n");

	return 0;
}

static struct ibm_struct thermal_driver_data = {
	.name = "thermal",
	.read = thermal_read,
	.exit = thermal_exit,
};

/*************************************************************************
 * Backlight/brightness subdriver
 */

#define TPACPI_BACKLIGHT_DEV_NAME "thinkpad_screen"

/*
 * ThinkPads can read brightness from two places: EC HBRV (0x31), or
 * CMOS NVRAM byte 0x5E, bits 0-3.
 *
 * EC HBRV (0x31) has the following layout
 *   Bit 7: unknown function
 *   Bit 6: unknown function
 *   Bit 5: Z: honour scale changes, NZ: ignore scale changes
 *   Bit 4: must be set to zero to avoid problems
 *   Bit 3-0: backlight brightness level
 *
 * brightness_get_raw returns status data in the HBRV layout
 *
 * WARNING: The X61 has been verified to use HBRV for something else, so
 * this should be used _only_ on IBM ThinkPads, and maybe with some careful
 * testing on the very early *60 Lenovo models...
 */

enum {
	TP_EC_BACKLIGHT = 0x31,

	/* TP_EC_BACKLIGHT bitmasks */
	TP_EC_BACKLIGHT_LVLMSK = 0x1F,
	TP_EC_BACKLIGHT_CMDMSK = 0xE0,
	TP_EC_BACKLIGHT_MAPSW = 0x20,
};

enum tpacpi_brightness_access_mode {
	TPACPI_BRGHT_MODE_AUTO = 0,	/* Not implemented yet */
	TPACPI_BRGHT_MODE_EC,		/* EC control */
	TPACPI_BRGHT_MODE_UCMS_STEP,	/* UCMS step-based control */
	TPACPI_BRGHT_MODE_ECNVRAM,	/* EC control w/ NVRAM store */
	TPACPI_BRGHT_MODE_MAX
};

static struct backlight_device *ibm_backlight_device;

static enum tpacpi_brightness_access_mode brightness_mode =
		TPACPI_BRGHT_MODE_MAX;

static unsigned int brightness_enable = 2; /* 2 = auto, 0 = no, 1 = yes */

static struct mutex brightness_mutex;

/* NVRAM brightness access,
 * call with brightness_mutex held! */
static unsigned int tpacpi_brightness_nvram_get(void)
{
	u8 lnvram;

	lnvram = (nvram_read_byte(TP_NVRAM_ADDR_BRIGHTNESS)
		  & TP_NVRAM_MASK_LEVEL_BRIGHTNESS)
		  >> TP_NVRAM_POS_LEVEL_BRIGHTNESS;
	lnvram &= bright_maxlvl;

	return lnvram;
}

static void tpacpi_brightness_checkpoint_nvram(void)
{
	u8 lec = 0;
	u8 b_nvram;

	if (brightness_mode != TPACPI_BRGHT_MODE_ECNVRAM)
		return;

	vdbg_printk(TPACPI_DBG_BRGHT,
		"trying to checkpoint backlight level to NVRAM...\n");

	if (mutex_lock_killable(&brightness_mutex) < 0)
		return;

	if (unlikely(!acpi_ec_read(TP_EC_BACKLIGHT, &lec)))
		goto unlock;
	lec &= TP_EC_BACKLIGHT_LVLMSK;
	b_nvram = nvram_read_byte(TP_NVRAM_ADDR_BRIGHTNESS);

	if (lec != ((b_nvram & TP_NVRAM_MASK_LEVEL_BRIGHTNESS)
			     >> TP_NVRAM_POS_LEVEL_BRIGHTNESS)) {
		/* NVRAM needs update */
		b_nvram &= ~(TP_NVRAM_MASK_LEVEL_BRIGHTNESS <<
				TP_NVRAM_POS_LEVEL_BRIGHTNESS);
		b_nvram |= lec;
		nvram_write_byte(b_nvram, TP_NVRAM_ADDR_BRIGHTNESS);
		dbg_printk(TPACPI_DBG_BRGHT,
			   "updated NVRAM backlight level to %u (0x%02x)\n",
			   (unsigned int) lec, (unsigned int) b_nvram);
	} else
		vdbg_printk(TPACPI_DBG_BRGHT,
			   "NVRAM backlight level already is %u (0x%02x)\n",
			   (unsigned int) lec, (unsigned int) b_nvram);

unlock:
	mutex_unlock(&brightness_mutex);
}


/* call with brightness_mutex held! */
static int tpacpi_brightness_get_raw(int *status)
{
	u8 lec = 0;

	switch (brightness_mode) {
	case TPACPI_BRGHT_MODE_UCMS_STEP:
		*status = tpacpi_brightness_nvram_get();
		return 0;
	case TPACPI_BRGHT_MODE_EC:
	case TPACPI_BRGHT_MODE_ECNVRAM:
		if (unlikely(!acpi_ec_read(TP_EC_BACKLIGHT, &lec)))
			return -EIO;
		*status = lec;
		return 0;
	default:
		return -ENXIO;
	}
}

/* call with brightness_mutex held! */
/* do NOT call with illegal backlight level value */
static int tpacpi_brightness_set_ec(unsigned int value)
{
	u8 lec = 0;

	if (unlikely(!acpi_ec_read(TP_EC_BACKLIGHT, &lec)))
		return -EIO;

	if (unlikely(!acpi_ec_write(TP_EC_BACKLIGHT,
				(lec & TP_EC_BACKLIGHT_CMDMSK) |
				(value & TP_EC_BACKLIGHT_LVLMSK))))
		return -EIO;

	return 0;
}

/* call with brightness_mutex held! */
static int tpacpi_brightness_set_ucmsstep(unsigned int value)
{
	int cmos_cmd, inc;
	unsigned int current_value, i;

	current_value = tpacpi_brightness_nvram_get();

	if (value == current_value)
		return 0;

	cmos_cmd = (value > current_value) ?
			TP_CMOS_BRIGHTNESS_UP :
			TP_CMOS_BRIGHTNESS_DOWN;
	inc = (value > current_value) ? 1 : -1;

	for (i = current_value; i != value; i += inc)
		if (issue_thinkpad_cmos_command(cmos_cmd))
			return -EIO;

	return 0;
}

/* May return EINTR which can always be mapped to ERESTARTSYS */
static int brightness_set(unsigned int value)
{
	int res;

	if (value > bright_maxlvl || value < 0)
		return -EINVAL;

	vdbg_printk(TPACPI_DBG_BRGHT,
			"set backlight level to %d\n", value);

	res = mutex_lock_killable(&brightness_mutex);
	if (res < 0)
		return res;

	switch (brightness_mode) {
	case TPACPI_BRGHT_MODE_EC:
	case TPACPI_BRGHT_MODE_ECNVRAM:
		res = tpacpi_brightness_set_ec(value);
		break;
	case TPACPI_BRGHT_MODE_UCMS_STEP:
		res = tpacpi_brightness_set_ucmsstep(value);
		break;
	default:
		res = -ENXIO;
	}

	mutex_unlock(&brightness_mutex);
	return res;
}

/* sysfs backlight class ----------------------------------------------- */

static int brightness_update_status(struct backlight_device *bd)
{
	unsigned int level =
		(bd->props.fb_blank == FB_BLANK_UNBLANK &&
		 bd->props.power == FB_BLANK_UNBLANK) ?
				bd->props.brightness : 0;

	dbg_printk(TPACPI_DBG_BRGHT,
			"backlight: attempt to set level to %d\n",
			level);

	/* it is the backlight class's job (caller) to handle
	 * EINTR and other errors properly */
	return brightness_set(level);
}

static int brightness_get(struct backlight_device *bd)
{
	int status, res;

	res = mutex_lock_killable(&brightness_mutex);
	if (res < 0)
		return 0;

	res = tpacpi_brightness_get_raw(&status);

	mutex_unlock(&brightness_mutex);

	if (res < 0)
		return 0;

	return status & TP_EC_BACKLIGHT_LVLMSK;
}

static void tpacpi_brightness_notify_change(void)
{
	backlight_force_update(ibm_backlight_device,
			       BACKLIGHT_UPDATE_HOTKEY);
}

static const struct backlight_ops ibm_backlight_data = {
	.get_brightness = brightness_get,
	.update_status  = brightness_update_status,
};

/* --------------------------------------------------------------------- */

/*
 * Call _BCL method of video device.  On some ThinkPads this will
 * switch the firmware to the ACPI brightness control mode.
 */

static int __init tpacpi_query_bcl_levels(acpi_handle handle)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct acpi_device *device, *child;
	int rc;

	if (acpi_bus_get_device(handle, &device))
		return 0;

	rc = 0;
	list_for_each_entry(child, &device->children, node) {
		acpi_status status = acpi_evaluate_object(child->handle, "_BCL",
							  NULL, &buffer);
		if (ACPI_FAILURE(status))
			continue;

		obj = (union acpi_object *)buffer.pointer;
		if (!obj || (obj->type != ACPI_TYPE_PACKAGE)) {
			pr_err("Unknown _BCL data, please report this to %s\n",
				TPACPI_MAIL);
			rc = 0;
		} else {
			rc = obj->package.count;
		}
		break;
	}

	kfree(buffer.pointer);
	return rc;
}


/*
 * Returns 0 (no ACPI _BCL or _BCL invalid), or size of brightness map
 */
static unsigned int __init tpacpi_check_std_acpi_brightness_support(void)
{
	acpi_handle video_device;
	int bcl_levels = 0;

	tpacpi_acpi_handle_locate("video", NULL, &video_device);
	if (video_device)
		bcl_levels = tpacpi_query_bcl_levels(video_device);

	tp_features.bright_acpimode = (bcl_levels > 0);

	return (bcl_levels > 2) ? (bcl_levels - 2) : 0;
}

/*
 * These are only useful for models that have only one possibility
 * of GPU.  If the BIOS model handles both ATI and Intel, don't use
 * these quirks.
 */
#define TPACPI_BRGHT_Q_NOEC	0x0001	/* Must NOT use EC HBRV */
#define TPACPI_BRGHT_Q_EC	0x0002  /* Should or must use EC HBRV */
#define TPACPI_BRGHT_Q_ASK	0x8000	/* Ask for user report */

static const struct tpacpi_quirk brightness_quirk_table[] __initconst = {
	/* Models with ATI GPUs known to require ECNVRAM mode */
	TPACPI_Q_IBM('1', 'Y', TPACPI_BRGHT_Q_EC),	/* T43/p ATI */

	/* Models with ATI GPUs that can use ECNVRAM */
	TPACPI_Q_IBM('1', 'R', TPACPI_BRGHT_Q_EC),	/* R50,51 T40-42 */
	TPACPI_Q_IBM('1', 'Q', TPACPI_BRGHT_Q_ASK|TPACPI_BRGHT_Q_EC),
	TPACPI_Q_IBM('7', '6', TPACPI_BRGHT_Q_EC),	/* R52 */
	TPACPI_Q_IBM('7', '8', TPACPI_BRGHT_Q_ASK|TPACPI_BRGHT_Q_EC),

	/* Models with Intel Extreme Graphics 2 */
	TPACPI_Q_IBM('1', 'U', TPACPI_BRGHT_Q_NOEC),	/* X40 */
	TPACPI_Q_IBM('1', 'V', TPACPI_BRGHT_Q_ASK|TPACPI_BRGHT_Q_EC),
	TPACPI_Q_IBM('1', 'W', TPACPI_BRGHT_Q_ASK|TPACPI_BRGHT_Q_EC),

	/* Models with Intel GMA900 */
	TPACPI_Q_IBM('7', '0', TPACPI_BRGHT_Q_NOEC),	/* T43, R52 */
	TPACPI_Q_IBM('7', '4', TPACPI_BRGHT_Q_NOEC),	/* X41 */
	TPACPI_Q_IBM('7', '5', TPACPI_BRGHT_Q_NOEC),	/* X41 Tablet */
};

/*
 * Returns < 0 for error, otherwise sets tp_features.bright_*
 * and bright_maxlvl.
 */
static void __init tpacpi_detect_brightness_capabilities(void)
{
	unsigned int b;

	vdbg_printk(TPACPI_DBG_INIT,
		    "detecting firmware brightness interface capabilities\n");

	/* we could run a quirks check here (same table used by
	 * brightness_init) if needed */

	/*
	 * We always attempt to detect acpi support, so as to switch
	 * Lenovo Vista BIOS to ACPI brightness mode even if we are not
	 * going to publish a backlight interface
	 */
	b = tpacpi_check_std_acpi_brightness_support();
	switch (b) {
	case 16:
		bright_maxlvl = 15;
		pr_info("detected a 16-level brightness capable ThinkPad\n");
		break;
	case 8:
	case 0:
		bright_maxlvl = 7;
		pr_info("detected a 8-level brightness capable ThinkPad\n");
		break;
	default:
		pr_err("Unsupported brightness interface, "
		       "please contact %s\n", TPACPI_MAIL);
		tp_features.bright_unkfw = 1;
		bright_maxlvl = b - 1;
	}
}

static int __init brightness_init(struct ibm_init_struct *iibm)
{
	struct backlight_properties props;
	int b;
	unsigned long quirks;

	vdbg_printk(TPACPI_DBG_INIT, "initializing brightness subdriver\n");

	mutex_init(&brightness_mutex);

	quirks = tpacpi_check_quirks(brightness_quirk_table,
				ARRAY_SIZE(brightness_quirk_table));

	/* tpacpi_detect_brightness_capabilities() must have run already */

	/* if it is unknown, we don't handle it: it wouldn't be safe */
	if (tp_features.bright_unkfw)
		return 1;

	if (!brightness_enable) {
		dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_BRGHT,
			   "brightness support disabled by "
			   "module parameter\n");
		return 1;
	}

	if (acpi_video_backlight_support()) {
		if (brightness_enable > 1) {
			pr_info("Standard ACPI backlight interface "
				"available, not loading native one\n");
			return 1;
		} else if (brightness_enable == 1) {
			pr_warn("Cannot enable backlight brightness support, "
				"ACPI is already handling it.  Refer to the "
				"acpi_backlight kernel parameter.\n");
			return 1;
		}
	} else if (tp_features.bright_acpimode && brightness_enable > 1) {
		pr_notice("Standard ACPI backlight interface not "
			  "available, thinkpad_acpi native "
			  "brightness control enabled\n");
	}

	/*
	 * Check for module parameter bogosity, note that we
	 * init brightness_mode to TPACPI_BRGHT_MODE_MAX in order to be
	 * able to detect "unspecified"
	 */
	if (brightness_mode > TPACPI_BRGHT_MODE_MAX)
		return -EINVAL;

	/* TPACPI_BRGHT_MODE_AUTO not implemented yet, just use default */
	if (brightness_mode == TPACPI_BRGHT_MODE_AUTO ||
	    brightness_mode == TPACPI_BRGHT_MODE_MAX) {
		if (quirks & TPACPI_BRGHT_Q_EC)
			brightness_mode = TPACPI_BRGHT_MODE_ECNVRAM;
		else
			brightness_mode = TPACPI_BRGHT_MODE_UCMS_STEP;

		dbg_printk(TPACPI_DBG_BRGHT,
			   "driver auto-selected brightness_mode=%d\n",
			   brightness_mode);
	}

	/* Safety */
	if (!tpacpi_is_ibm() &&
	    (brightness_mode == TPACPI_BRGHT_MODE_ECNVRAM ||
	     brightness_mode == TPACPI_BRGHT_MODE_EC))
		return -EINVAL;

	if (tpacpi_brightness_get_raw(&b) < 0)
		return 1;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = bright_maxlvl;
	props.brightness = b & TP_EC_BACKLIGHT_LVLMSK;
	ibm_backlight_device = backlight_device_register(TPACPI_BACKLIGHT_DEV_NAME,
							 NULL, NULL,
							 &ibm_backlight_data,
							 &props);
	if (IS_ERR(ibm_backlight_device)) {
		int rc = PTR_ERR(ibm_backlight_device);
		ibm_backlight_device = NULL;
		pr_err("Could not register backlight device\n");
		return rc;
	}
	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_BRGHT,
			"brightness is supported\n");

	if (quirks & TPACPI_BRGHT_Q_ASK) {
		pr_notice("brightness: will use unverified default: "
			  "brightness_mode=%d\n", brightness_mode);
		pr_notice("brightness: please report to %s whether it works well "
			  "or not on your ThinkPad\n", TPACPI_MAIL);
	}

	/* Added by mistake in early 2007.  Probably useless, but it could
	 * be working around some unknown firmware problem where the value
	 * read at startup doesn't match the real hardware state... so leave
	 * it in place just in case */
	backlight_update_status(ibm_backlight_device);

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_BRGHT,
			"brightness: registering brightness hotkeys "
			"as change notification\n");
	tpacpi_hotkey_driver_mask_set(hotkey_driver_mask
				| TP_ACPI_HKEY_BRGHTUP_MASK
				| TP_ACPI_HKEY_BRGHTDWN_MASK);
	return 0;
}

static void brightness_suspend(void)
{
	tpacpi_brightness_checkpoint_nvram();
}

static void brightness_shutdown(void)
{
	tpacpi_brightness_checkpoint_nvram();
}

static void brightness_exit(void)
{
	if (ibm_backlight_device) {
		vdbg_printk(TPACPI_DBG_EXIT | TPACPI_DBG_BRGHT,
			    "calling backlight_device_unregister()\n");
		backlight_device_unregister(ibm_backlight_device);
	}

	tpacpi_brightness_checkpoint_nvram();
}

static int brightness_read(struct seq_file *m)
{
	int level;

	level = brightness_get(NULL);
	if (level < 0) {
		seq_printf(m, "level:\t\tunreadable\n");
	} else {
		seq_printf(m, "level:\t\t%d\n", level);
		seq_printf(m, "commands:\tup, down\n");
		seq_printf(m, "commands:\tlevel <level> (<level> is 0-%d)\n",
			       bright_maxlvl);
	}

	return 0;
}

static int brightness_write(char *buf)
{
	int level;
	int rc;
	char *cmd;

	level = brightness_get(NULL);
	if (level < 0)
		return level;

	while ((cmd = next_cmd(&buf))) {
		if (strlencmp(cmd, "up") == 0) {
			if (level < bright_maxlvl)
				level++;
		} else if (strlencmp(cmd, "down") == 0) {
			if (level > 0)
				level--;
		} else if (sscanf(cmd, "level %d", &level) == 1 &&
			   level >= 0 && level <= bright_maxlvl) {
			/* new level set */
		} else
			return -EINVAL;
	}

	tpacpi_disclose_usertask("procfs brightness",
			"set level to %d\n", level);

	/*
	 * Now we know what the final level should be, so we try to set it.
	 * Doing it this way makes the syscall restartable in case of EINTR
	 */
	rc = brightness_set(level);
	if (!rc && ibm_backlight_device)
		backlight_force_update(ibm_backlight_device,
					BACKLIGHT_UPDATE_SYSFS);
	return (rc == -EINTR)? -ERESTARTSYS : rc;
}

static struct ibm_struct brightness_driver_data = {
	.name = "brightness",
	.read = brightness_read,
	.write = brightness_write,
	.exit = brightness_exit,
	.suspend = brightness_suspend,
	.shutdown = brightness_shutdown,
};

/*************************************************************************
 * Volume subdriver
 */

/*
 * IBM ThinkPads have a simple volume controller with MUTE gating.
 * Very early Lenovo ThinkPads follow the IBM ThinkPad spec.
 *
 * Since the *61 series (and probably also the later *60 series), Lenovo
 * ThinkPads only implement the MUTE gate.
 *
 * EC register 0x30
 *   Bit 6: MUTE (1 mutes sound)
 *   Bit 3-0: Volume
 *   Other bits should be zero as far as we know.
 *
 * This is also stored in CMOS NVRAM, byte 0x60, bit 6 (MUTE), and
 * bits 3-0 (volume).  Other bits in NVRAM may have other functions,
 * such as bit 7 which is used to detect repeated presses of MUTE,
 * and we leave them unchanged.
 */

#ifdef CONFIG_THINKPAD_ACPI_ALSA_SUPPORT

#define TPACPI_ALSA_DRVNAME  "ThinkPad EC"
#define TPACPI_ALSA_SHRTNAME "ThinkPad Console Audio Control"
#define TPACPI_ALSA_MIXERNAME TPACPI_ALSA_SHRTNAME

#if SNDRV_CARDS <= 32
#define DEFAULT_ALSA_IDX		~((1 << (SNDRV_CARDS - 3)) - 1)
#else
#define DEFAULT_ALSA_IDX		~((1 << (32 - 3)) - 1)
#endif
static int alsa_index = DEFAULT_ALSA_IDX; /* last three slots */
static char *alsa_id = "ThinkPadEC";
static bool alsa_enable = SNDRV_DEFAULT_ENABLE1;

struct tpacpi_alsa_data {
	struct snd_card *card;
	struct snd_ctl_elem_id *ctl_mute_id;
	struct snd_ctl_elem_id *ctl_vol_id;
};

static struct snd_card *alsa_card;

enum {
	TP_EC_AUDIO = 0x30,

	/* TP_EC_AUDIO bits */
	TP_EC_AUDIO_MUTESW = 6,

	/* TP_EC_AUDIO bitmasks */
	TP_EC_AUDIO_LVL_MSK = 0x0F,
	TP_EC_AUDIO_MUTESW_MSK = (1 << TP_EC_AUDIO_MUTESW),

	/* Maximum volume */
	TP_EC_VOLUME_MAX = 14,
};

enum tpacpi_volume_access_mode {
	TPACPI_VOL_MODE_AUTO = 0,	/* Not implemented yet */
	TPACPI_VOL_MODE_EC,		/* Pure EC control */
	TPACPI_VOL_MODE_UCMS_STEP,	/* UCMS step-based control: N/A */
	TPACPI_VOL_MODE_ECNVRAM,	/* EC control w/ NVRAM store */
	TPACPI_VOL_MODE_MAX
};

enum tpacpi_volume_capabilities {
	TPACPI_VOL_CAP_AUTO = 0,	/* Use white/blacklist */
	TPACPI_VOL_CAP_VOLMUTE,		/* Output vol and mute */
	TPACPI_VOL_CAP_MUTEONLY,	/* Output mute only */
	TPACPI_VOL_CAP_MAX
};

static enum tpacpi_volume_access_mode volume_mode =
	TPACPI_VOL_MODE_MAX;

static enum tpacpi_volume_capabilities volume_capabilities;
static bool volume_control_allowed;

/*
 * Used to syncronize writers to TP_EC_AUDIO and
 * TP_NVRAM_ADDR_MIXER, as we need to do read-modify-write
 */
static struct mutex volume_mutex;

static void tpacpi_volume_checkpoint_nvram(void)
{
	u8 lec = 0;
	u8 b_nvram;
	u8 ec_mask;

	if (volume_mode != TPACPI_VOL_MODE_ECNVRAM)
		return;
	if (!volume_control_allowed)
		return;

	vdbg_printk(TPACPI_DBG_MIXER,
		"trying to checkpoint mixer state to NVRAM...\n");

	if (tp_features.mixer_no_level_control)
		ec_mask = TP_EC_AUDIO_MUTESW_MSK;
	else
		ec_mask = TP_EC_AUDIO_MUTESW_MSK | TP_EC_AUDIO_LVL_MSK;

	if (mutex_lock_killable(&volume_mutex) < 0)
		return;

	if (unlikely(!acpi_ec_read(TP_EC_AUDIO, &lec)))
		goto unlock;
	lec &= ec_mask;
	b_nvram = nvram_read_byte(TP_NVRAM_ADDR_MIXER);

	if (lec != (b_nvram & ec_mask)) {
		/* NVRAM needs update */
		b_nvram &= ~ec_mask;
		b_nvram |= lec;
		nvram_write_byte(b_nvram, TP_NVRAM_ADDR_MIXER);
		dbg_printk(TPACPI_DBG_MIXER,
			   "updated NVRAM mixer status to 0x%02x (0x%02x)\n",
			   (unsigned int) lec, (unsigned int) b_nvram);
	} else {
		vdbg_printk(TPACPI_DBG_MIXER,
			   "NVRAM mixer status already is 0x%02x (0x%02x)\n",
			   (unsigned int) lec, (unsigned int) b_nvram);
	}

unlock:
	mutex_unlock(&volume_mutex);
}

static int volume_get_status_ec(u8 *status)
{
	u8 s;

	if (!acpi_ec_read(TP_EC_AUDIO, &s))
		return -EIO;

	*status = s;

	dbg_printk(TPACPI_DBG_MIXER, "status 0x%02x\n", s);

	return 0;
}

static int volume_get_status(u8 *status)
{
	return volume_get_status_ec(status);
}

static int volume_set_status_ec(const u8 status)
{
	if (!acpi_ec_write(TP_EC_AUDIO, status))
		return -EIO;

	dbg_printk(TPACPI_DBG_MIXER, "set EC mixer to 0x%02x\n", status);

	return 0;
}

static int volume_set_status(const u8 status)
{
	return volume_set_status_ec(status);
}

/* returns < 0 on error, 0 on no change, 1 on change */
static int __volume_set_mute_ec(const bool mute)
{
	int rc;
	u8 s, n;

	if (mutex_lock_killable(&volume_mutex) < 0)
		return -EINTR;

	rc = volume_get_status_ec(&s);
	if (rc)
		goto unlock;

	n = (mute) ? s | TP_EC_AUDIO_MUTESW_MSK :
		     s & ~TP_EC_AUDIO_MUTESW_MSK;

	if (n != s) {
		rc = volume_set_status_ec(n);
		if (!rc)
			rc = 1;
	}

unlock:
	mutex_unlock(&volume_mutex);
	return rc;
}

static int volume_alsa_set_mute(const bool mute)
{
	dbg_printk(TPACPI_DBG_MIXER, "ALSA: trying to %smute\n",
		   (mute) ? "" : "un");
	return __volume_set_mute_ec(mute);
}

static int volume_set_mute(const bool mute)
{
	int rc;

	dbg_printk(TPACPI_DBG_MIXER, "trying to %smute\n",
		   (mute) ? "" : "un");

	rc = __volume_set_mute_ec(mute);
	return (rc < 0) ? rc : 0;
}

/* returns < 0 on error, 0 on no change, 1 on change */
static int __volume_set_volume_ec(const u8 vol)
{
	int rc;
	u8 s, n;

	if (vol > TP_EC_VOLUME_MAX)
		return -EINVAL;

	if (mutex_lock_killable(&volume_mutex) < 0)
		return -EINTR;

	rc = volume_get_status_ec(&s);
	if (rc)
		goto unlock;

	n = (s & ~TP_EC_AUDIO_LVL_MSK) | vol;

	if (n != s) {
		rc = volume_set_status_ec(n);
		if (!rc)
			rc = 1;
	}

unlock:
	mutex_unlock(&volume_mutex);
	return rc;
}

static int volume_alsa_set_volume(const u8 vol)
{
	dbg_printk(TPACPI_DBG_MIXER,
		   "ALSA: trying to set volume level to %hu\n", vol);
	return __volume_set_volume_ec(vol);
}

static void volume_alsa_notify_change(void)
{
	struct tpacpi_alsa_data *d;

	if (alsa_card && alsa_card->private_data) {
		d = alsa_card->private_data;
		if (d->ctl_mute_id)
			snd_ctl_notify(alsa_card,
					SNDRV_CTL_EVENT_MASK_VALUE,
					d->ctl_mute_id);
		if (d->ctl_vol_id)
			snd_ctl_notify(alsa_card,
					SNDRV_CTL_EVENT_MASK_VALUE,
					d->ctl_vol_id);
	}
}

static int volume_alsa_vol_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = TP_EC_VOLUME_MAX;
	return 0;
}

static int volume_alsa_vol_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u8 s;
	int rc;

	rc = volume_get_status(&s);
	if (rc < 0)
		return rc;

	ucontrol->value.integer.value[0] = s & TP_EC_AUDIO_LVL_MSK;
	return 0;
}

static int volume_alsa_vol_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	tpacpi_disclose_usertask("ALSA", "set volume to %ld\n",
				 ucontrol->value.integer.value[0]);
	return volume_alsa_set_volume(ucontrol->value.integer.value[0]);
}

#define volume_alsa_mute_info snd_ctl_boolean_mono_info

static int volume_alsa_mute_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u8 s;
	int rc;

	rc = volume_get_status(&s);
	if (rc < 0)
		return rc;

	ucontrol->value.integer.value[0] =
				(s & TP_EC_AUDIO_MUTESW_MSK) ? 0 : 1;
	return 0;
}

static int volume_alsa_mute_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	tpacpi_disclose_usertask("ALSA", "%smute\n",
				 ucontrol->value.integer.value[0] ?
					"un" : "");
	return volume_alsa_set_mute(!ucontrol->value.integer.value[0]);
}

static struct snd_kcontrol_new volume_alsa_control_vol = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Console Playback Volume",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = volume_alsa_vol_info,
	.get = volume_alsa_vol_get,
};

static struct snd_kcontrol_new volume_alsa_control_mute = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Console Playback Switch",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = volume_alsa_mute_info,
	.get = volume_alsa_mute_get,
};

static void volume_suspend(void)
{
	tpacpi_volume_checkpoint_nvram();
}

static void volume_resume(void)
{
	volume_alsa_notify_change();
}

static void volume_shutdown(void)
{
	tpacpi_volume_checkpoint_nvram();
}

static void volume_exit(void)
{
	if (alsa_card) {
		snd_card_free(alsa_card);
		alsa_card = NULL;
	}

	tpacpi_volume_checkpoint_nvram();
}

static int __init volume_create_alsa_mixer(void)
{
	struct snd_card *card;
	struct tpacpi_alsa_data *data;
	struct snd_kcontrol *ctl_vol;
	struct snd_kcontrol *ctl_mute;
	int rc;

	rc = snd_card_create(alsa_index, alsa_id, THIS_MODULE,
			    sizeof(struct tpacpi_alsa_data), &card);
	if (rc < 0 || !card) {
		pr_err("Failed to create ALSA card structures: %d\n", rc);
		return 1;
	}

	BUG_ON(!card->private_data);
	data = card->private_data;
	data->card = card;

	strlcpy(card->driver, TPACPI_ALSA_DRVNAME,
		sizeof(card->driver));
	strlcpy(card->shortname, TPACPI_ALSA_SHRTNAME,
		sizeof(card->shortname));
	snprintf(card->mixername, sizeof(card->mixername), "ThinkPad EC %s",
		 (thinkpad_id.ec_version_str) ?
			thinkpad_id.ec_version_str : "(unknown)");
	snprintf(card->longname, sizeof(card->longname),
		 "%s at EC reg 0x%02x, fw %s", card->shortname, TP_EC_AUDIO,
		 (thinkpad_id.ec_version_str) ?
			thinkpad_id.ec_version_str : "unknown");

	if (volume_control_allowed) {
		volume_alsa_control_vol.put = volume_alsa_vol_put;
		volume_alsa_control_vol.access =
				SNDRV_CTL_ELEM_ACCESS_READWRITE;

		volume_alsa_control_mute.put = volume_alsa_mute_put;
		volume_alsa_control_mute.access =
				SNDRV_CTL_ELEM_ACCESS_READWRITE;
	}

	if (!tp_features.mixer_no_level_control) {
		ctl_vol = snd_ctl_new1(&volume_alsa_control_vol, NULL);
		rc = snd_ctl_add(card, ctl_vol);
		if (rc < 0) {
			pr_err("Failed to create ALSA volume control: %d\n",
			       rc);
			goto err_exit;
		}
		data->ctl_vol_id = &ctl_vol->id;
	}

	ctl_mute = snd_ctl_new1(&volume_alsa_control_mute, NULL);
	rc = snd_ctl_add(card, ctl_mute);
	if (rc < 0) {
		pr_err("Failed to create ALSA mute control: %d\n", rc);
		goto err_exit;
	}
	data->ctl_mute_id = &ctl_mute->id;

	snd_card_set_dev(card, &tpacpi_pdev->dev);
	rc = snd_card_register(card);
	if (rc < 0) {
		pr_err("Failed to register ALSA card: %d\n", rc);
		goto err_exit;
	}

	alsa_card = card;
	return 0;

err_exit:
	snd_card_free(card);
	return 1;
}

#define TPACPI_VOL_Q_MUTEONLY	0x0001	/* Mute-only control available */
#define TPACPI_VOL_Q_LEVEL	0x0002  /* Volume control available */

static const struct tpacpi_quirk volume_quirk_table[] __initconst = {
	/* Whitelist volume level on all IBM by default */
	{ .vendor = PCI_VENDOR_ID_IBM,
	  .bios   = TPACPI_MATCH_ANY,
	  .ec     = TPACPI_MATCH_ANY,
	  .quirks = TPACPI_VOL_Q_LEVEL },

	/* Lenovo models with volume control (needs confirmation) */
	TPACPI_QEC_LNV('7', 'C', TPACPI_VOL_Q_LEVEL), /* R60/i */
	TPACPI_QEC_LNV('7', 'E', TPACPI_VOL_Q_LEVEL), /* R60e/i */
	TPACPI_QEC_LNV('7', '9', TPACPI_VOL_Q_LEVEL), /* T60/p */
	TPACPI_QEC_LNV('7', 'B', TPACPI_VOL_Q_LEVEL), /* X60/s */
	TPACPI_QEC_LNV('7', 'J', TPACPI_VOL_Q_LEVEL), /* X60t */
	TPACPI_QEC_LNV('7', '7', TPACPI_VOL_Q_LEVEL), /* Z60 */
	TPACPI_QEC_LNV('7', 'F', TPACPI_VOL_Q_LEVEL), /* Z61 */

	/* Whitelist mute-only on all Lenovo by default */
	{ .vendor = PCI_VENDOR_ID_LENOVO,
	  .bios   = TPACPI_MATCH_ANY,
	  .ec	  = TPACPI_MATCH_ANY,
	  .quirks = TPACPI_VOL_Q_MUTEONLY }
};

static int __init volume_init(struct ibm_init_struct *iibm)
{
	unsigned long quirks;
	int rc;

	vdbg_printk(TPACPI_DBG_INIT, "initializing volume subdriver\n");

	mutex_init(&volume_mutex);

	/*
	 * Check for module parameter bogosity, note that we
	 * init volume_mode to TPACPI_VOL_MODE_MAX in order to be
	 * able to detect "unspecified"
	 */
	if (volume_mode > TPACPI_VOL_MODE_MAX)
		return -EINVAL;

	if (volume_mode == TPACPI_VOL_MODE_UCMS_STEP) {
		pr_err("UCMS step volume mode not implemented, "
		       "please contact %s\n", TPACPI_MAIL);
		return 1;
	}

	if (volume_capabilities >= TPACPI_VOL_CAP_MAX)
		return -EINVAL;

	/*
	 * The ALSA mixer is our primary interface.
	 * When disabled, don't install the subdriver at all
	 */
	if (!alsa_enable) {
		vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_MIXER,
			    "ALSA mixer disabled by parameter, "
			    "not loading volume subdriver...\n");
		return 1;
	}

	quirks = tpacpi_check_quirks(volume_quirk_table,
				     ARRAY_SIZE(volume_quirk_table));

	switch (volume_capabilities) {
	case TPACPI_VOL_CAP_AUTO:
		if (quirks & TPACPI_VOL_Q_MUTEONLY)
			tp_features.mixer_no_level_control = 1;
		else if (quirks & TPACPI_VOL_Q_LEVEL)
			tp_features.mixer_no_level_control = 0;
		else
			return 1; /* no mixer */
		break;
	case TPACPI_VOL_CAP_VOLMUTE:
		tp_features.mixer_no_level_control = 0;
		break;
	case TPACPI_VOL_CAP_MUTEONLY:
		tp_features.mixer_no_level_control = 1;
		break;
	default:
		return 1;
	}

	if (volume_capabilities != TPACPI_VOL_CAP_AUTO)
		dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_MIXER,
				"using user-supplied volume_capabilities=%d\n",
				volume_capabilities);

	if (volume_mode == TPACPI_VOL_MODE_AUTO ||
	    volume_mode == TPACPI_VOL_MODE_MAX) {
		volume_mode = TPACPI_VOL_MODE_ECNVRAM;

		dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_MIXER,
				"driver auto-selected volume_mode=%d\n",
				volume_mode);
	} else {
		dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_MIXER,
				"using user-supplied volume_mode=%d\n",
				volume_mode);
	}

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_MIXER,
			"mute is supported, volume control is %s\n",
			str_supported(!tp_features.mixer_no_level_control));

	rc = volume_create_alsa_mixer();
	if (rc) {
		pr_err("Could not create the ALSA mixer interface\n");
		return rc;
	}

	pr_info("Console audio control enabled, mode: %s\n",
		(volume_control_allowed) ?
			"override (read/write)" :
			"monitor (read only)");

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_MIXER,
		"registering volume hotkeys as change notification\n");
	tpacpi_hotkey_driver_mask_set(hotkey_driver_mask
			| TP_ACPI_HKEY_VOLUP_MASK
			| TP_ACPI_HKEY_VOLDWN_MASK
			| TP_ACPI_HKEY_MUTE_MASK);

	return 0;
}

static int volume_read(struct seq_file *m)
{
	u8 status;

	if (volume_get_status(&status) < 0) {
		seq_printf(m, "level:\t\tunreadable\n");
	} else {
		if (tp_features.mixer_no_level_control)
			seq_printf(m, "level:\t\tunsupported\n");
		else
			seq_printf(m, "level:\t\t%d\n",
					status & TP_EC_AUDIO_LVL_MSK);

		seq_printf(m, "mute:\t\t%s\n",
				onoff(status, TP_EC_AUDIO_MUTESW));

		if (volume_control_allowed) {
			seq_printf(m, "commands:\tunmute, mute\n");
			if (!tp_features.mixer_no_level_control) {
				seq_printf(m,
					       "commands:\tup, down\n");
				seq_printf(m,
					       "commands:\tlevel <level>"
					       " (<level> is 0-%d)\n",
					       TP_EC_VOLUME_MAX);
			}
		}
	}

	return 0;
}

static int volume_write(char *buf)
{
	u8 s;
	u8 new_level, new_mute;
	int l;
	char *cmd;
	int rc;

	/*
	 * We do allow volume control at driver startup, so that the
	 * user can set initial state through the volume=... parameter hack.
	 */
	if (!volume_control_allowed && tpacpi_lifecycle != TPACPI_LIFE_INIT) {
		if (unlikely(!tp_warned.volume_ctrl_forbidden)) {
			tp_warned.volume_ctrl_forbidden = 1;
			pr_notice("Console audio control in monitor mode, "
				  "changes are not allowed\n");
			pr_notice("Use the volume_control=1 module parameter "
				  "to enable volume control\n");
		}
		return -EPERM;
	}

	rc = volume_get_status(&s);
	if (rc < 0)
		return rc;

	new_level = s & TP_EC_AUDIO_LVL_MSK;
	new_mute  = s & TP_EC_AUDIO_MUTESW_MSK;

	while ((cmd = next_cmd(&buf))) {
		if (!tp_features.mixer_no_level_control) {
			if (strlencmp(cmd, "up") == 0) {
				if (new_mute)
					new_mute = 0;
				else if (new_level < TP_EC_VOLUME_MAX)
					new_level++;
				continue;
			} else if (strlencmp(cmd, "down") == 0) {
				if (new_mute)
					new_mute = 0;
				else if (new_level > 0)
					new_level--;
				continue;
			} else if (sscanf(cmd, "level %u", &l) == 1 &&
				   l >= 0 && l <= TP_EC_VOLUME_MAX) {
					new_level = l;
				continue;
			}
		}
		if (strlencmp(cmd, "mute") == 0)
			new_mute = TP_EC_AUDIO_MUTESW_MSK;
		else if (strlencmp(cmd, "unmute") == 0)
			new_mute = 0;
		else
			return -EINVAL;
	}

	if (tp_features.mixer_no_level_control) {
		tpacpi_disclose_usertask("procfs volume", "%smute\n",
					new_mute ? "" : "un");
		rc = volume_set_mute(!!new_mute);
	} else {
		tpacpi_disclose_usertask("procfs volume",
					"%smute and set level to %d\n",
					new_mute ? "" : "un", new_level);
		rc = volume_set_status(new_mute | new_level);
	}
	volume_alsa_notify_change();

	return (rc == -EINTR) ? -ERESTARTSYS : rc;
}

static struct ibm_struct volume_driver_data = {
	.name = "volume",
	.read = volume_read,
	.write = volume_write,
	.exit = volume_exit,
	.suspend = volume_suspend,
	.resume = volume_resume,
	.shutdown = volume_shutdown,
};

#else /* !CONFIG_THINKPAD_ACPI_ALSA_SUPPORT */

#define alsa_card NULL

static void inline volume_alsa_notify_change(void)
{
}

static int __init volume_init(struct ibm_init_struct *iibm)
{
	pr_info("volume: disabled as there is no ALSA support in this kernel\n");

	return 1;
}

static struct ibm_struct volume_driver_data = {
	.name = "volume",
};

#endif /* CONFIG_THINKPAD_ACPI_ALSA_SUPPORT */

/*************************************************************************
 * Fan subdriver
 */

/*
 * FAN ACCESS MODES
 *
 * TPACPI_FAN_RD_ACPI_GFAN:
 * 	ACPI GFAN method: returns fan level
 *
 * 	see TPACPI_FAN_WR_ACPI_SFAN
 * 	EC 0x2f (HFSP) not available if GFAN exists
 *
 * TPACPI_FAN_WR_ACPI_SFAN:
 * 	ACPI SFAN method: sets fan level, 0 (stop) to 7 (max)
 *
 * 	EC 0x2f (HFSP) might be available *for reading*, but do not use
 * 	it for writing.
 *
 * TPACPI_FAN_WR_TPEC:
 * 	ThinkPad EC register 0x2f (HFSP): fan control loop mode
 * 	Supported on almost all ThinkPads
 *
 * 	Fan speed changes of any sort (including those caused by the
 * 	disengaged mode) are usually done slowly by the firmware as the
 * 	maximum amount of fan duty cycle change per second seems to be
 * 	limited.
 *
 * 	Reading is not available if GFAN exists.
 * 	Writing is not available if SFAN exists.
 *
 * 	Bits
 *	 7	automatic mode engaged;
 *  		(default operation mode of the ThinkPad)
 * 		fan level is ignored in this mode.
 *	 6	full speed mode (takes precedence over bit 7);
 *		not available on all thinkpads.  May disable
 *		the tachometer while the fan controller ramps up
 *		the speed (which can take up to a few *minutes*).
 *		Speeds up fan to 100% duty-cycle, which is far above
 *		the standard RPM levels.  It is not impossible that
 *		it could cause hardware damage.
 *	5-3	unused in some models.  Extra bits for fan level
 *		in others, but still useless as all values above
 *		7 map to the same speed as level 7 in these models.
 *	2-0	fan level (0..7 usually)
 *			0x00 = stop
 * 			0x07 = max (set when temperatures critical)
 * 		Some ThinkPads may have other levels, see
 * 		TPACPI_FAN_WR_ACPI_FANS (X31/X40/X41)
 *
 *	FIRMWARE BUG: on some models, EC 0x2f might not be initialized at
 *	boot. Apparently the EC does not initialize it, so unless ACPI DSDT
 *	does so, its initial value is meaningless (0x07).
 *
 *	For firmware bugs, refer to:
 *	http://thinkwiki.org/wiki/Embedded_Controller_Firmware#Firmware_Issues
 *
 * 	----
 *
 *	ThinkPad EC register 0x84 (LSB), 0x85 (MSB):
 *	Main fan tachometer reading (in RPM)
 *
 *	This register is present on all ThinkPads with a new-style EC, and
 *	it is known not to be present on the A21m/e, and T22, as there is
 *	something else in offset 0x84 according to the ACPI DSDT.  Other
 *	ThinkPads from this same time period (and earlier) probably lack the
 *	tachometer as well.
 *
 *	Unfortunately a lot of ThinkPads with new-style ECs but whose firmware
 *	was never fixed by IBM to report the EC firmware version string
 *	probably support the tachometer (like the early X models), so
 *	detecting it is quite hard.  We need more data to know for sure.
 *
 *	FIRMWARE BUG: always read 0x84 first, otherwise incorrect readings
 *	might result.
 *
 *	FIRMWARE BUG: may go stale while the EC is switching to full speed
 *	mode.
 *
 *	For firmware bugs, refer to:
 *	http://thinkwiki.org/wiki/Embedded_Controller_Firmware#Firmware_Issues
 *
 *	----
 *
 *	ThinkPad EC register 0x31 bit 0 (only on select models)
 *
 *	When bit 0 of EC register 0x31 is zero, the tachometer registers
 *	show the speed of the main fan.  When bit 0 of EC register 0x31
 *	is one, the tachometer registers show the speed of the auxiliary
 *	fan.
 *
 *	Fan control seems to affect both fans, regardless of the state
 *	of this bit.
 *
 *	So far, only the firmware for the X60/X61 non-tablet versions
 *	seem to support this (firmware TP-7M).
 *
 * TPACPI_FAN_WR_ACPI_FANS:
 *	ThinkPad X31, X40, X41.  Not available in the X60.
 *
 *	FANS ACPI handle: takes three arguments: low speed, medium speed,
 *	high speed.  ACPI DSDT seems to map these three speeds to levels
 *	as follows: STOP LOW LOW MED MED HIGH HIGH HIGH HIGH
 *	(this map is stored on FAN0..FAN8 as "0,1,1,2,2,3,3,3,3")
 *
 * 	The speeds are stored on handles
 * 	(FANA:FAN9), (FANC:FANB), (FANE:FAND).
 *
 * 	There are three default speed sets, accessible as handles:
 * 	FS1L,FS1M,FS1H; FS2L,FS2M,FS2H; FS3L,FS3M,FS3H
 *
 * 	ACPI DSDT switches which set is in use depending on various
 * 	factors.
 *
 * 	TPACPI_FAN_WR_TPEC is also available and should be used to
 * 	command the fan.  The X31/X40/X41 seems to have 8 fan levels,
 * 	but the ACPI tables just mention level 7.
 */

enum {					/* Fan control constants */
	fan_status_offset = 0x2f,	/* EC register 0x2f */
	fan_rpm_offset = 0x84,		/* EC register 0x84: LSB, 0x85 MSB (RPM)
					 * 0x84 must be read before 0x85 */
	fan_select_offset = 0x31,	/* EC register 0x31 (Firmware 7M)
					   bit 0 selects which fan is active */

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

static bool fan_control_allowed;

static enum fan_status_access_mode fan_status_access_mode;
static enum fan_control_access_mode fan_control_access_mode;
static enum fan_control_commands fan_control_commands;

static u8 fan_control_initial_status;
static u8 fan_control_desired_level;
static u8 fan_control_resume_level;
static int fan_watchdog_maxinterval;

static struct mutex fan_mutex;

static void fan_watchdog_fire(struct work_struct *ignored);
static DECLARE_DELAYED_WORK(fan_watchdog_task, fan_watchdog_fire);

TPACPI_HANDLE(fans, ec, "FANS");	/* X31, X40, X41 */
TPACPI_HANDLE(gfan, ec, "GFAN",	/* 570 */
	   "\\FSPD",		/* 600e/x, 770e, 770x */
	   );			/* all others */
TPACPI_HANDLE(sfan, ec, "SFAN",	/* 570 */
	   "JFNS",		/* 770x-JL */
	   );			/* all others */

/*
 * Unitialized HFSP quirk: ACPI DSDT and EC fail to initialize the
 * HFSP register at boot, so it contains 0x07 but the Thinkpad could
 * be in auto mode (0x80).
 *
 * This is corrected by any write to HFSP either by the driver, or
 * by the firmware.
 *
 * We assume 0x07 really means auto mode while this quirk is active,
 * as this is far more likely than the ThinkPad being in level 7,
 * which is only used by the firmware during thermal emergencies.
 *
 * Enable for TP-1Y (T43), TP-78 (R51e), TP-76 (R52),
 * TP-70 (T43, R52), which are known to be buggy.
 */

static void fan_quirk1_setup(void)
{
	if (fan_control_initial_status == 0x07) {
		pr_notice("fan_init: initial fan status is unknown, "
			  "assuming it is in auto mode\n");
		tp_features.fan_ctrl_status_undef = 1;
	}
}

static void fan_quirk1_handle(u8 *fan_status)
{
	if (unlikely(tp_features.fan_ctrl_status_undef)) {
		if (*fan_status != fan_control_initial_status) {
			/* something changed the HFSP regisnter since
			 * driver init time, so it is not undefined
			 * anymore */
			tp_features.fan_ctrl_status_undef = 0;
		} else {
			/* Return most likely status. In fact, it
			 * might be the only possible status */
			*fan_status = TP_EC_FAN_AUTO;
		}
	}
}

/* Select main fan on X60/X61, NOOP on others */
static bool fan_select_fan1(void)
{
	if (tp_features.second_fan) {
		u8 val;

		if (ec_read(fan_select_offset, &val) < 0)
			return false;
		val &= 0xFEU;
		if (ec_write(fan_select_offset, val) < 0)
			return false;
	}
	return true;
}

/* Select secondary fan on X60/X61 */
static bool fan_select_fan2(void)
{
	u8 val;

	if (!tp_features.second_fan)
		return false;

	if (ec_read(fan_select_offset, &val) < 0)
		return false;
	val |= 0x01U;
	if (ec_write(fan_select_offset, val) < 0)
		return false;

	return true;
}

/*
 * Call with fan_mutex held
 */
static void fan_update_desired_level(u8 status)
{
	if ((status &
	     (TP_EC_FAN_AUTO | TP_EC_FAN_FULLSPEED)) == 0) {
		if (status > 7)
			fan_control_desired_level = 7;
		else
			fan_control_desired_level = status;
	}
}

static int fan_get_status(u8 *status)
{
	u8 s;

	/* TODO:
	 * Add TPACPI_FAN_RD_ACPI_FANS ? */

	switch (fan_status_access_mode) {
	case TPACPI_FAN_RD_ACPI_GFAN: {
		/* 570, 600e/x, 770e, 770x */
		int res;

		if (unlikely(!acpi_evalf(gfan_handle, &res, NULL, "d")))
			return -EIO;

		if (likely(status))
			*status = res & 0x07;

		break;
	}
	case TPACPI_FAN_RD_TPEC:
		/* all except 570, 600e/x, 770e, 770x */
		if (unlikely(!acpi_ec_read(fan_status_offset, &s)))
			return -EIO;

		if (likely(status)) {
			*status = s;
			fan_quirk1_handle(status);
		}

		break;

	default:
		return -ENXIO;
	}

	return 0;
}

static int fan_get_status_safe(u8 *status)
{
	int rc;
	u8 s;

	if (mutex_lock_killable(&fan_mutex))
		return -ERESTARTSYS;
	rc = fan_get_status(&s);
	if (!rc)
		fan_update_desired_level(s);
	mutex_unlock(&fan_mutex);

	if (status)
		*status = s;

	return rc;
}

static int fan_get_speed(unsigned int *speed)
{
	u8 hi, lo;

	switch (fan_status_access_mode) {
	case TPACPI_FAN_RD_TPEC:
		/* all except 570, 600e/x, 770e, 770x */
		if (unlikely(!fan_select_fan1()))
			return -EIO;
		if (unlikely(!acpi_ec_read(fan_rpm_offset, &lo) ||
			     !acpi_ec_read(fan_rpm_offset + 1, &hi)))
			return -EIO;

		if (likely(speed))
			*speed = (hi << 8) | lo;

		break;

	default:
		return -ENXIO;
	}

	return 0;
}

static int fan2_get_speed(unsigned int *speed)
{
	u8 hi, lo;
	bool rc;

	switch (fan_status_access_mode) {
	case TPACPI_FAN_RD_TPEC:
		/* all except 570, 600e/x, 770e, 770x */
		if (unlikely(!fan_select_fan2()))
			return -EIO;
		rc = !acpi_ec_read(fan_rpm_offset, &lo) ||
			     !acpi_ec_read(fan_rpm_offset + 1, &hi);
		fan_select_fan1(); /* play it safe */
		if (rc)
			return -EIO;

		if (likely(speed))
			*speed = (hi << 8) | lo;

		break;

	default:
		return -ENXIO;
	}

	return 0;
}

static int fan_set_level(int level)
{
	if (!fan_control_allowed)
		return -EPERM;

	switch (fan_control_access_mode) {
	case TPACPI_FAN_WR_ACPI_SFAN:
		if (level >= 0 && level <= 7) {
			if (!acpi_evalf(sfan_handle, NULL, NULL, "vd", level))
				return -EIO;
		} else
			return -EINVAL;
		break;

	case TPACPI_FAN_WR_ACPI_FANS:
	case TPACPI_FAN_WR_TPEC:
		if (!(level & TP_EC_FAN_AUTO) &&
		    !(level & TP_EC_FAN_FULLSPEED) &&
		    ((level < 0) || (level > 7)))
			return -EINVAL;

		/* safety net should the EC not support AUTO
		 * or FULLSPEED mode bits and just ignore them */
		if (level & TP_EC_FAN_FULLSPEED)
			level |= 7;	/* safety min speed 7 */
		else if (level & TP_EC_FAN_AUTO)
			level |= 4;	/* safety min speed 4 */

		if (!acpi_ec_write(fan_status_offset, level))
			return -EIO;
		else
			tp_features.fan_ctrl_status_undef = 0;
		break;

	default:
		return -ENXIO;
	}

	vdbg_printk(TPACPI_DBG_FAN,
		"fan control: set fan control register to 0x%02x\n", level);
	return 0;
}

static int fan_set_level_safe(int level)
{
	int rc;

	if (!fan_control_allowed)
		return -EPERM;

	if (mutex_lock_killable(&fan_mutex))
		return -ERESTARTSYS;

	if (level == TPACPI_FAN_LAST_LEVEL)
		level = fan_control_desired_level;

	rc = fan_set_level(level);
	if (!rc)
		fan_update_desired_level(level);

	mutex_unlock(&fan_mutex);
	return rc;
}

static int fan_set_enable(void)
{
	u8 s;
	int rc;

	if (!fan_control_allowed)
		return -EPERM;

	if (mutex_lock_killable(&fan_mutex))
		return -ERESTARTSYS;

	switch (fan_control_access_mode) {
	case TPACPI_FAN_WR_ACPI_FANS:
	case TPACPI_FAN_WR_TPEC:
		rc = fan_get_status(&s);
		if (rc < 0)
			break;

		/* Don't go out of emergency fan mode */
		if (s != 7) {
			s &= 0x07;
			s |= TP_EC_FAN_AUTO | 4; /* min fan speed 4 */
		}

		if (!acpi_ec_write(fan_status_offset, s))
			rc = -EIO;
		else {
			tp_features.fan_ctrl_status_undef = 0;
			rc = 0;
		}
		break;

	case TPACPI_FAN_WR_ACPI_SFAN:
		rc = fan_get_status(&s);
		if (rc < 0)
			break;

		s &= 0x07;

		/* Set fan to at least level 4 */
		s |= 4;

		if (!acpi_evalf(sfan_handle, NULL, NULL, "vd", s))
			rc = -EIO;
		else
			rc = 0;
		break;

	default:
		rc = -ENXIO;
	}

	mutex_unlock(&fan_mutex);

	if (!rc)
		vdbg_printk(TPACPI_DBG_FAN,
			"fan control: set fan control register to 0x%02x\n",
			s);
	return rc;
}

static int fan_set_disable(void)
{
	int rc;

	if (!fan_control_allowed)
		return -EPERM;

	if (mutex_lock_killable(&fan_mutex))
		return -ERESTARTSYS;

	rc = 0;
	switch (fan_control_access_mode) {
	case TPACPI_FAN_WR_ACPI_FANS:
	case TPACPI_FAN_WR_TPEC:
		if (!acpi_ec_write(fan_status_offset, 0x00))
			rc = -EIO;
		else {
			fan_control_desired_level = 0;
			tp_features.fan_ctrl_status_undef = 0;
		}
		break;

	case TPACPI_FAN_WR_ACPI_SFAN:
		if (!acpi_evalf(sfan_handle, NULL, NULL, "vd", 0x00))
			rc = -EIO;
		else
			fan_control_desired_level = 0;
		break;

	default:
		rc = -ENXIO;
	}

	if (!rc)
		vdbg_printk(TPACPI_DBG_FAN,
			"fan control: set fan control register to 0\n");

	mutex_unlock(&fan_mutex);
	return rc;
}

static int fan_set_speed(int speed)
{
	int rc;

	if (!fan_control_allowed)
		return -EPERM;

	if (mutex_lock_killable(&fan_mutex))
		return -ERESTARTSYS;

	rc = 0;
	switch (fan_control_access_mode) {
	case TPACPI_FAN_WR_ACPI_FANS:
		if (speed >= 0 && speed <= 65535) {
			if (!acpi_evalf(fans_handle, NULL, NULL, "vddd",
					speed, speed, speed))
				rc = -EIO;
		} else
			rc = -EINVAL;
		break;

	default:
		rc = -ENXIO;
	}

	mutex_unlock(&fan_mutex);
	return rc;
}

static void fan_watchdog_reset(void)
{
	if (fan_control_access_mode == TPACPI_FAN_WR_NONE)
		return;

	if (fan_watchdog_maxinterval > 0 &&
	    tpacpi_lifecycle != TPACPI_LIFE_EXITING)
		mod_delayed_work(tpacpi_wq, &fan_watchdog_task,
			msecs_to_jiffies(fan_watchdog_maxinterval * 1000));
	else
		cancel_delayed_work(&fan_watchdog_task);
}

static void fan_watchdog_fire(struct work_struct *ignored)
{
	int rc;

	if (tpacpi_lifecycle != TPACPI_LIFE_RUNNING)
		return;

	pr_notice("fan watchdog: enabling fan\n");
	rc = fan_set_enable();
	if (rc < 0) {
		pr_err("fan watchdog: error %d while enabling fan, "
		       "will try again later...\n", -rc);
		/* reschedule for later */
		fan_watchdog_reset();
	}
}

/*
 * SYSFS fan layout: hwmon compatible (device)
 *
 * pwm*_enable:
 * 	0: "disengaged" mode
 * 	1: manual mode
 * 	2: native EC "auto" mode (recommended, hardware default)
 *
 * pwm*: set speed in manual mode, ignored otherwise.
 * 	0 is level 0; 255 is level 7. Intermediate points done with linear
 * 	interpolation.
 *
 * fan*_input: tachometer reading, RPM
 *
 *
 * SYSFS fan layout: extensions
 *
 * fan_watchdog (driver):
 * 	fan watchdog interval in seconds, 0 disables (default), max 120
 */

/* sysfs fan pwm1_enable ----------------------------------------------- */
static ssize_t fan_pwm1_enable_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	int res, mode;
	u8 status;

	res = fan_get_status_safe(&status);
	if (res)
		return res;

	if (status & TP_EC_FAN_FULLSPEED) {
		mode = 0;
	} else if (status & TP_EC_FAN_AUTO) {
		mode = 2;
	} else
		mode = 1;

	return snprintf(buf, PAGE_SIZE, "%d\n", mode);
}

static ssize_t fan_pwm1_enable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long t;
	int res, level;

	if (parse_strtoul(buf, 2, &t))
		return -EINVAL;

	tpacpi_disclose_usertask("hwmon pwm1_enable",
			"set fan mode to %lu\n", t);

	switch (t) {
	case 0:
		level = TP_EC_FAN_FULLSPEED;
		break;
	case 1:
		level = TPACPI_FAN_LAST_LEVEL;
		break;
	case 2:
		level = TP_EC_FAN_AUTO;
		break;
	case 3:
		/* reserved for software-controlled auto mode */
		return -ENOSYS;
	default:
		return -EINVAL;
	}

	res = fan_set_level_safe(level);
	if (res == -ENXIO)
		return -EINVAL;
	else if (res < 0)
		return res;

	fan_watchdog_reset();

	return count;
}

static struct device_attribute dev_attr_fan_pwm1_enable =
	__ATTR(pwm1_enable, S_IWUSR | S_IRUGO,
		fan_pwm1_enable_show, fan_pwm1_enable_store);

/* sysfs fan pwm1 ------------------------------------------------------ */
static ssize_t fan_pwm1_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int res;
	u8 status;

	res = fan_get_status_safe(&status);
	if (res)
		return res;

	if ((status &
	     (TP_EC_FAN_AUTO | TP_EC_FAN_FULLSPEED)) != 0)
		status = fan_control_desired_level;

	if (status > 7)
		status = 7;

	return snprintf(buf, PAGE_SIZE, "%u\n", (status * 255) / 7);
}

static ssize_t fan_pwm1_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	unsigned long s;
	int rc;
	u8 status, newlevel;

	if (parse_strtoul(buf, 255, &s))
		return -EINVAL;

	tpacpi_disclose_usertask("hwmon pwm1",
			"set fan speed to %lu\n", s);

	/* scale down from 0-255 to 0-7 */
	newlevel = (s >> 5) & 0x07;

	if (mutex_lock_killable(&fan_mutex))
		return -ERESTARTSYS;

	rc = fan_get_status(&status);
	if (!rc && (status &
		    (TP_EC_FAN_AUTO | TP_EC_FAN_FULLSPEED)) == 0) {
		rc = fan_set_level(newlevel);
		if (rc == -ENXIO)
			rc = -EINVAL;
		else if (!rc) {
			fan_update_desired_level(newlevel);
			fan_watchdog_reset();
		}
	}

	mutex_unlock(&fan_mutex);
	return (rc)? rc : count;
}

static struct device_attribute dev_attr_fan_pwm1 =
	__ATTR(pwm1, S_IWUSR | S_IRUGO,
		fan_pwm1_show, fan_pwm1_store);

/* sysfs fan fan1_input ------------------------------------------------ */
static ssize_t fan_fan1_input_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int res;
	unsigned int speed;

	res = fan_get_speed(&speed);
	if (res < 0)
		return res;

	return snprintf(buf, PAGE_SIZE, "%u\n", speed);
}

static struct device_attribute dev_attr_fan_fan1_input =
	__ATTR(fan1_input, S_IRUGO,
		fan_fan1_input_show, NULL);

/* sysfs fan fan2_input ------------------------------------------------ */
static ssize_t fan_fan2_input_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int res;
	unsigned int speed;

	res = fan2_get_speed(&speed);
	if (res < 0)
		return res;

	return snprintf(buf, PAGE_SIZE, "%u\n", speed);
}

static struct device_attribute dev_attr_fan_fan2_input =
	__ATTR(fan2_input, S_IRUGO,
		fan_fan2_input_show, NULL);

/* sysfs fan fan_watchdog (hwmon driver) ------------------------------- */
static ssize_t fan_fan_watchdog_show(struct device_driver *drv,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fan_watchdog_maxinterval);
}

static ssize_t fan_fan_watchdog_store(struct device_driver *drv,
				      const char *buf, size_t count)
{
	unsigned long t;

	if (parse_strtoul(buf, 120, &t))
		return -EINVAL;

	if (!fan_control_allowed)
		return -EPERM;

	fan_watchdog_maxinterval = t;
	fan_watchdog_reset();

	tpacpi_disclose_usertask("fan_watchdog", "set to %lu\n", t);

	return count;
}

static DRIVER_ATTR(fan_watchdog, S_IWUSR | S_IRUGO,
		fan_fan_watchdog_show, fan_fan_watchdog_store);

/* --------------------------------------------------------------------- */
static struct attribute *fan_attributes[] = {
	&dev_attr_fan_pwm1_enable.attr, &dev_attr_fan_pwm1.attr,
	&dev_attr_fan_fan1_input.attr,
	NULL, /* for fan2_input */
	NULL
};

static const struct attribute_group fan_attr_group = {
	.attrs = fan_attributes,
};

#define	TPACPI_FAN_Q1	0x0001		/* Unitialized HFSP */
#define TPACPI_FAN_2FAN	0x0002		/* EC 0x31 bit 0 selects fan2 */

#define TPACPI_FAN_QI(__id1, __id2, __quirks)	\
	{ .vendor = PCI_VENDOR_ID_IBM,		\
	  .bios = TPACPI_MATCH_ANY,		\
	  .ec = TPID(__id1, __id2),		\
	  .quirks = __quirks }

#define TPACPI_FAN_QL(__id1, __id2, __quirks)	\
	{ .vendor = PCI_VENDOR_ID_LENOVO,	\
	  .bios = TPACPI_MATCH_ANY,		\
	  .ec = TPID(__id1, __id2),		\
	  .quirks = __quirks }

static const struct tpacpi_quirk fan_quirk_table[] __initconst = {
	TPACPI_FAN_QI('1', 'Y', TPACPI_FAN_Q1),
	TPACPI_FAN_QI('7', '8', TPACPI_FAN_Q1),
	TPACPI_FAN_QI('7', '6', TPACPI_FAN_Q1),
	TPACPI_FAN_QI('7', '0', TPACPI_FAN_Q1),
	TPACPI_FAN_QL('7', 'M', TPACPI_FAN_2FAN),
};

#undef TPACPI_FAN_QL
#undef TPACPI_FAN_QI

static int __init fan_init(struct ibm_init_struct *iibm)
{
	int rc;
	unsigned long quirks;

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_FAN,
			"initializing fan subdriver\n");

	mutex_init(&fan_mutex);
	fan_status_access_mode = TPACPI_FAN_NONE;
	fan_control_access_mode = TPACPI_FAN_WR_NONE;
	fan_control_commands = 0;
	fan_watchdog_maxinterval = 0;
	tp_features.fan_ctrl_status_undef = 0;
	tp_features.second_fan = 0;
	fan_control_desired_level = 7;

	if (tpacpi_is_ibm()) {
		TPACPI_ACPIHANDLE_INIT(fans);
		TPACPI_ACPIHANDLE_INIT(gfan);
		TPACPI_ACPIHANDLE_INIT(sfan);
	}

	quirks = tpacpi_check_quirks(fan_quirk_table,
				     ARRAY_SIZE(fan_quirk_table));

	if (gfan_handle) {
		/* 570, 600e/x, 770e, 770x */
		fan_status_access_mode = TPACPI_FAN_RD_ACPI_GFAN;
	} else {
		/* all other ThinkPads: note that even old-style
		 * ThinkPad ECs supports the fan control register */
		if (likely(acpi_ec_read(fan_status_offset,
					&fan_control_initial_status))) {
			fan_status_access_mode = TPACPI_FAN_RD_TPEC;
			if (quirks & TPACPI_FAN_Q1)
				fan_quirk1_setup();
			if (quirks & TPACPI_FAN_2FAN) {
				tp_features.second_fan = 1;
				dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_FAN,
					"secondary fan support enabled\n");
			}
		} else {
			pr_err("ThinkPad ACPI EC access misbehaving, "
			       "fan status and control unavailable\n");
			return 1;
		}
	}

	if (sfan_handle) {
		/* 570, 770x-JL */
		fan_control_access_mode = TPACPI_FAN_WR_ACPI_SFAN;
		fan_control_commands |=
		    TPACPI_FAN_CMD_LEVEL | TPACPI_FAN_CMD_ENABLE;
	} else {
		if (!gfan_handle) {
			/* gfan without sfan means no fan control */
			/* all other models implement TP EC 0x2f control */

			if (fans_handle) {
				/* X31, X40, X41 */
				fan_control_access_mode =
				    TPACPI_FAN_WR_ACPI_FANS;
				fan_control_commands |=
				    TPACPI_FAN_CMD_SPEED |
				    TPACPI_FAN_CMD_LEVEL |
				    TPACPI_FAN_CMD_ENABLE;
			} else {
				fan_control_access_mode = TPACPI_FAN_WR_TPEC;
				fan_control_commands |=
				    TPACPI_FAN_CMD_LEVEL |
				    TPACPI_FAN_CMD_ENABLE;
			}
		}
	}

	vdbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_FAN,
		"fan is %s, modes %d, %d\n",
		str_supported(fan_status_access_mode != TPACPI_FAN_NONE ||
		  fan_control_access_mode != TPACPI_FAN_WR_NONE),
		fan_status_access_mode, fan_control_access_mode);

	/* fan control master switch */
	if (!fan_control_allowed) {
		fan_control_access_mode = TPACPI_FAN_WR_NONE;
		fan_control_commands = 0;
		dbg_printk(TPACPI_DBG_INIT | TPACPI_DBG_FAN,
			   "fan control features disabled by parameter\n");
	}

	/* update fan_control_desired_level */
	if (fan_status_access_mode != TPACPI_FAN_NONE)
		fan_get_status_safe(NULL);

	if (fan_status_access_mode != TPACPI_FAN_NONE ||
	    fan_control_access_mode != TPACPI_FAN_WR_NONE) {
		if (tp_features.second_fan) {
			/* attach second fan tachometer */
			fan_attributes[ARRAY_SIZE(fan_attributes)-2] =
					&dev_attr_fan_fan2_input.attr;
		}
		rc = sysfs_create_group(&tpacpi_sensors_pdev->dev.kobj,
					 &fan_attr_group);
		if (rc < 0)
			return rc;

		rc = driver_create_file(&tpacpi_hwmon_pdriver.driver,
					&driver_attr_fan_watchdog);
		if (rc < 0) {
			sysfs_remove_group(&tpacpi_sensors_pdev->dev.kobj,
					&fan_attr_group);
			return rc;
		}
		return 0;
	} else
		return 1;
}

static void fan_exit(void)
{
	vdbg_printk(TPACPI_DBG_EXIT | TPACPI_DBG_FAN,
		    "cancelling any pending fan watchdog tasks\n");

	/* FIXME: can we really do this unconditionally? */
	sysfs_remove_group(&tpacpi_sensors_pdev->dev.kobj, &fan_attr_group);
	driver_remove_file(&tpacpi_hwmon_pdriver.driver,
			   &driver_attr_fan_watchdog);

	cancel_delayed_work(&fan_watchdog_task);
	flush_workqueue(tpacpi_wq);
}

static void fan_suspend(void)
{
	int rc;

	if (!fan_control_allowed)
		return;

	/* Store fan status in cache */
	fan_control_resume_level = 0;
	rc = fan_get_status_safe(&fan_control_resume_level);
	if (rc < 0)
		pr_notice("failed to read fan level for later "
			  "restore during resume: %d\n", rc);

	/* if it is undefined, don't attempt to restore it.
	 * KEEP THIS LAST */
	if (tp_features.fan_ctrl_status_undef)
		fan_control_resume_level = 0;
}

static void fan_resume(void)
{
	u8 current_level = 7;
	bool do_set = false;
	int rc;

	/* DSDT *always* updates status on resume */
	tp_features.fan_ctrl_status_undef = 0;

	if (!fan_control_allowed ||
	    !fan_control_resume_level ||
	    (fan_get_status_safe(&current_level) < 0))
		return;

	switch (fan_control_access_mode) {
	case TPACPI_FAN_WR_ACPI_SFAN:
		/* never decrease fan level */
		do_set = (fan_control_resume_level > current_level);
		break;
	case TPACPI_FAN_WR_ACPI_FANS:
	case TPACPI_FAN_WR_TPEC:
		/* never decrease fan level, scale is:
		 * TP_EC_FAN_FULLSPEED > 7 >= TP_EC_FAN_AUTO
		 *
		 * We expect the firmware to set either 7 or AUTO, but we
		 * handle FULLSPEED out of paranoia.
		 *
		 * So, we can safely only restore FULLSPEED or 7, anything
		 * else could slow the fan.  Restoring AUTO is useless, at
		 * best that's exactly what the DSDT already set (it is the
		 * slower it uses).
		 *
		 * Always keep in mind that the DSDT *will* have set the
		 * fans to what the vendor supposes is the best level.  We
		 * muck with it only to speed the fan up.
		 */
		if (fan_control_resume_level != 7 &&
		    !(fan_control_resume_level & TP_EC_FAN_FULLSPEED))
			return;
		else
			do_set = !(current_level & TP_EC_FAN_FULLSPEED) &&
				 (current_level != fan_control_resume_level);
		break;
	default:
		return;
	}
	if (do_set) {
		pr_notice("restoring fan level to 0x%02x\n",
			  fan_control_resume_level);
		rc = fan_set_level_safe(fan_control_resume_level);
		if (rc < 0)
			pr_notice("failed to restore fan level: %d\n", rc);
	}
}

static int fan_read(struct seq_file *m)
{
	int rc;
	u8 status;
	unsigned int speed = 0;

	switch (fan_status_access_mode) {
	case TPACPI_FAN_RD_ACPI_GFAN:
		/* 570, 600e/x, 770e, 770x */
		rc = fan_get_status_safe(&status);
		if (rc < 0)
			return rc;

		seq_printf(m, "status:\t\t%s\n"
			       "level:\t\t%d\n",
			       (status != 0) ? "enabled" : "disabled", status);
		break;

	case TPACPI_FAN_RD_TPEC:
		/* all except 570, 600e/x, 770e, 770x */
		rc = fan_get_status_safe(&status);
		if (rc < 0)
			return rc;

		seq_printf(m, "status:\t\t%s\n",
			       (status != 0) ? "enabled" : "disabled");

		rc = fan_get_speed(&speed);
		if (rc < 0)
			return rc;

		seq_printf(m, "speed:\t\t%d\n", speed);

		if (status & TP_EC_FAN_FULLSPEED)
			/* Disengaged mode takes precedence */
			seq_printf(m, "level:\t\tdisengaged\n");
		else if (status & TP_EC_FAN_AUTO)
			seq_printf(m, "level:\t\tauto\n");
		else
			seq_printf(m, "level:\t\t%d\n", status);
		break;

	case TPACPI_FAN_NONE:
	default:
		seq_printf(m, "status:\t\tnot supported\n");
	}

	if (fan_control_commands & TPACPI_FAN_CMD_LEVEL) {
		seq_printf(m, "commands:\tlevel <level>");

		switch (fan_control_access_mode) {
		case TPACPI_FAN_WR_ACPI_SFAN:
			seq_printf(m, " (<level> is 0-7)\n");
			break;

		default:
			seq_printf(m, " (<level> is 0-7, "
				       "auto, disengaged, full-speed)\n");
			break;
		}
	}

	if (fan_control_commands & TPACPI_FAN_CMD_ENABLE)
		seq_printf(m, "commands:\tenable, disable\n"
			       "commands:\twatchdog <timeout> (<timeout> "
			       "is 0 (off), 1-120 (seconds))\n");

	if (fan_control_commands & TPACPI_FAN_CMD_SPEED)
		seq_printf(m, "commands:\tspeed <speed>"
			       " (<speed> is 0-65535)\n");

	return 0;
}

static int fan_write_cmd_level(const char *cmd, int *rc)
{
	int level;

	if (strlencmp(cmd, "level auto") == 0)
		level = TP_EC_FAN_AUTO;
	else if ((strlencmp(cmd, "level disengaged") == 0) |
			(strlencmp(cmd, "level full-speed") == 0))
		level = TP_EC_FAN_FULLSPEED;
	else if (sscanf(cmd, "level %d", &level) != 1)
		return 0;

	*rc = fan_set_level_safe(level);
	if (*rc == -ENXIO)
		pr_err("level command accepted for unsupported access mode %d\n",
		       fan_control_access_mode);
	else if (!*rc)
		tpacpi_disclose_usertask("procfs fan",
			"set level to %d\n", level);

	return 1;
}

static int fan_write_cmd_enable(const char *cmd, int *rc)
{
	if (strlencmp(cmd, "enable") != 0)
		return 0;

	*rc = fan_set_enable();
	if (*rc == -ENXIO)
		pr_err("enable command accepted for unsupported access mode %d\n",
		       fan_control_access_mode);
	else if (!*rc)
		tpacpi_disclose_usertask("procfs fan", "enable\n");

	return 1;
}

static int fan_write_cmd_disable(const char *cmd, int *rc)
{
	if (strlencmp(cmd, "disable") != 0)
		return 0;

	*rc = fan_set_disable();
	if (*rc == -ENXIO)
		pr_err("disable command accepted for unsupported access mode %d\n",
		       fan_control_access_mode);
	else if (!*rc)
		tpacpi_disclose_usertask("procfs fan", "disable\n");

	return 1;
}

static int fan_write_cmd_speed(const char *cmd, int *rc)
{
	int speed;

	/* TODO:
	 * Support speed <low> <medium> <high> ? */

	if (sscanf(cmd, "speed %d", &speed) != 1)
		return 0;

	*rc = fan_set_speed(speed);
	if (*rc == -ENXIO)
		pr_err("speed command accepted for unsupported access mode %d\n",
		       fan_control_access_mode);
	else if (!*rc)
		tpacpi_disclose_usertask("procfs fan",
			"set speed to %d\n", speed);

	return 1;
}

static int fan_write_cmd_watchdog(const char *cmd, int *rc)
{
	int interval;

	if (sscanf(cmd, "watchdog %d", &interval) != 1)
		return 0;

	if (interval < 0 || interval > 120)
		*rc = -EINVAL;
	else {
		fan_watchdog_maxinterval = interval;
		tpacpi_disclose_usertask("procfs fan",
			"set watchdog timer to %d\n",
			interval);
	}

	return 1;
}

static int fan_write(char *buf)
{
	char *cmd;
	int rc = 0;

	while (!rc && (cmd = next_cmd(&buf))) {
		if (!((fan_control_commands & TPACPI_FAN_CMD_LEVEL) &&
		      fan_write_cmd_level(cmd, &rc)) &&
		    !((fan_control_commands & TPACPI_FAN_CMD_ENABLE) &&
		      (fan_write_cmd_enable(cmd, &rc) ||
		       fan_write_cmd_disable(cmd, &rc) ||
		       fan_write_cmd_watchdog(cmd, &rc))) &&
		    !((fan_control_commands & TPACPI_FAN_CMD_SPEED) &&
		      fan_write_cmd_speed(cmd, &rc))
		    )
			rc = -EINVAL;
		else if (!rc)
			fan_watchdog_reset();
	}

	return rc;
}

static struct ibm_struct fan_driver_data = {
	.name = "fan",
	.read = fan_read,
	.write = fan_write,
	.exit = fan_exit,
	.suspend = fan_suspend,
	.resume = fan_resume,
};

/*************************************************************************
 * Mute LED subdriver
 */


struct tp_led_table {
	acpi_string name;
	int on_value;
	int off_value;
	int state;
};

static struct tp_led_table led_tables[] = {
	[TPACPI_LED_MUTE] = {
		.name = "SSMS",
		.on_value = 1,
		.off_value = 0,
	},
	[TPACPI_LED_MICMUTE] = {
		.name = "MMTS",
		.on_value = 2,
		.off_value = 0,
	},
};

static int mute_led_on_off(struct tp_led_table *t, bool state)
{
	acpi_handle temp;
	int output;

	if (!ACPI_SUCCESS(acpi_get_handle(hkey_handle, t->name, &temp))) {
		pr_warn("Thinkpad ACPI has no %s interface.\n", t->name);
		return -EIO;
	}

	if (!acpi_evalf(hkey_handle, &output, t->name, "dd",
			state ? t->on_value : t->off_value))
		return -EIO;

	t->state = state;
	return state;
}

int tpacpi_led_set(int whichled, bool on)
{
	struct tp_led_table *t;

	if (whichled < 0 || whichled >= TPACPI_LED_MAX)
		return -EINVAL;

	t = &led_tables[whichled];
	if (t->state < 0 || t->state == on)
		return t->state;
	return mute_led_on_off(t, on);
}
EXPORT_SYMBOL_GPL(tpacpi_led_set);

static int mute_led_init(struct ibm_init_struct *iibm)
{
	acpi_handle temp;
	int i;

	for (i = 0; i < TPACPI_LED_MAX; i++) {
		struct tp_led_table *t = &led_tables[i];
		if (ACPI_SUCCESS(acpi_get_handle(hkey_handle, t->name, &temp)))
			mute_led_on_off(t, false);
		else
			t->state = -ENODEV;
	}
	return 0;
}

static void mute_led_exit(void)
{
	int i;

	for (i = 0; i < TPACPI_LED_MAX; i++)
		tpacpi_led_set(i, false);
}

static struct ibm_struct mute_led_driver_data = {
	.name = "mute_led",
	.exit = mute_led_exit,
};

/****************************************************************************
 ****************************************************************************
 *
 * Infrastructure
 *
 ****************************************************************************
 ****************************************************************************/

/*
 * HKEY event callout for other subdrivers go here
 * (yes, it is ugly, but it is quick, safe, and gets the job done
 */
static void tpacpi_driver_event(const unsigned int hkey_event)
{
	if (ibm_backlight_device) {
		switch (hkey_event) {
		case TP_HKEY_EV_BRGHT_UP:
		case TP_HKEY_EV_BRGHT_DOWN:
			tpacpi_brightness_notify_change();
		}
	}
	if (alsa_card) {
		switch (hkey_event) {
		case TP_HKEY_EV_VOL_UP:
		case TP_HKEY_EV_VOL_DOWN:
		case TP_HKEY_EV_VOL_MUTE:
			volume_alsa_notify_change();
		}
	}
}

static void hotkey_driver_event(const unsigned int scancode)
{
	tpacpi_driver_event(TP_HKEY_EV_HOTKEY_BASE + scancode);
}

/* sysfs name ---------------------------------------------------------- */
static ssize_t thinkpad_acpi_pdev_name_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", TPACPI_NAME);
}

static struct device_attribute dev_attr_thinkpad_acpi_pdev_name =
	__ATTR(name, S_IRUGO, thinkpad_acpi_pdev_name_show, NULL);

/* --------------------------------------------------------------------- */

/* /proc support */
static struct proc_dir_entry *proc_dir;

/*
 * Module and infrastructure proble, init and exit handling
 */

static bool force_load;

#ifdef CONFIG_THINKPAD_ACPI_DEBUG
static const char * __init str_supported(int is_supported)
{
	static char text_unsupported[] __initdata = "not supported";

	return (is_supported)? &text_unsupported[4] : &text_unsupported[0];
}
#endif /* CONFIG_THINKPAD_ACPI_DEBUG */

static void ibm_exit(struct ibm_struct *ibm)
{
	dbg_printk(TPACPI_DBG_EXIT, "removing %s\n", ibm->name);

	list_del_init(&ibm->all_drivers);

	if (ibm->flags.acpi_notify_installed) {
		dbg_printk(TPACPI_DBG_EXIT,
			"%s: acpi_remove_notify_handler\n", ibm->name);
		BUG_ON(!ibm->acpi);
		acpi_remove_notify_handler(*ibm->acpi->handle,
					   ibm->acpi->type,
					   dispatch_acpi_notify);
		ibm->flags.acpi_notify_installed = 0;
	}

	if (ibm->flags.proc_created) {
		dbg_printk(TPACPI_DBG_EXIT,
			"%s: remove_proc_entry\n", ibm->name);
		remove_proc_entry(ibm->name, proc_dir);
		ibm->flags.proc_created = 0;
	}

	if (ibm->flags.acpi_driver_registered) {
		dbg_printk(TPACPI_DBG_EXIT,
			"%s: acpi_bus_unregister_driver\n", ibm->name);
		BUG_ON(!ibm->acpi);
		acpi_bus_unregister_driver(ibm->acpi->driver);
		kfree(ibm->acpi->driver);
		ibm->acpi->driver = NULL;
		ibm->flags.acpi_driver_registered = 0;
	}

	if (ibm->flags.init_called && ibm->exit) {
		ibm->exit();
		ibm->flags.init_called = 0;
	}

	dbg_printk(TPACPI_DBG_INIT, "finished removing %s\n", ibm->name);
}

static int __init ibm_init(struct ibm_init_struct *iibm)
{
	int ret;
	struct ibm_struct *ibm = iibm->data;
	struct proc_dir_entry *entry;

	BUG_ON(ibm == NULL);

	INIT_LIST_HEAD(&ibm->all_drivers);

	if (ibm->flags.experimental && !experimental)
		return 0;

	dbg_printk(TPACPI_DBG_INIT,
		"probing for %s\n", ibm->name);

	if (iibm->init) {
		ret = iibm->init(iibm);
		if (ret > 0)
			return 0;	/* probe failed */
		if (ret)
			return ret;

		ibm->flags.init_called = 1;
	}

	if (ibm->acpi) {
		if (ibm->acpi->hid) {
			ret = register_tpacpi_subdriver(ibm);
			if (ret)
				goto err_out;
		}

		if (ibm->acpi->notify) {
			ret = setup_acpi_notify(ibm);
			if (ret == -ENODEV) {
				pr_notice("disabling subdriver %s\n",
					  ibm->name);
				ret = 0;
				goto err_out;
			}
			if (ret < 0)
				goto err_out;
		}
	}

	dbg_printk(TPACPI_DBG_INIT,
		"%s installed\n", ibm->name);

	if (ibm->read) {
		umode_t mode = iibm->base_procfs_mode;

		if (!mode)
			mode = S_IRUGO;
		if (ibm->write)
			mode |= S_IWUSR;
		entry = proc_create_data(ibm->name, mode, proc_dir,
					 &dispatch_proc_fops, ibm);
		if (!entry) {
			pr_err("unable to create proc entry %s\n", ibm->name);
			ret = -ENODEV;
			goto err_out;
		}
		ibm->flags.proc_created = 1;
	}

	list_add_tail(&ibm->all_drivers, &tpacpi_all_drivers);

	return 0;

err_out:
	dbg_printk(TPACPI_DBG_INIT,
		"%s: at error exit path with result %d\n",
		ibm->name, ret);

	ibm_exit(ibm);
	return (ret < 0)? ret : 0;
}

/* Probing */

static bool __pure __init tpacpi_is_fw_digit(const char c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

/* Most models: xxyTkkWW (#.##c); Ancient 570/600 and -SL lacks (#.##c) */
static bool __pure __init tpacpi_is_valid_fw_id(const char* const s,
						const char t)
{
	return s && strlen(s) >= 8 &&
		tpacpi_is_fw_digit(s[0]) &&
		tpacpi_is_fw_digit(s[1]) &&
		s[2] == t &&
		(s[3] == 'T' || s[3] == 'N') &&
		tpacpi_is_fw_digit(s[4]) &&
		tpacpi_is_fw_digit(s[5]);
}

/* returns 0 - probe ok, or < 0 - probe error.
 * Probe ok doesn't mean thinkpad found.
 * On error, kfree() cleanup on tp->* is not performed, caller must do it */
static int __must_check __init get_thinkpad_model_data(
						struct thinkpad_id_data *tp)
{
	const struct dmi_device *dev = NULL;
	char ec_fw_string[18];
	char const *s;

	if (!tp)
		return -EINVAL;

	memset(tp, 0, sizeof(*tp));

	if (dmi_name_in_vendors("IBM"))
		tp->vendor = PCI_VENDOR_ID_IBM;
	else if (dmi_name_in_vendors("LENOVO"))
		tp->vendor = PCI_VENDOR_ID_LENOVO;
	else
		return 0;

	s = dmi_get_system_info(DMI_BIOS_VERSION);
	tp->bios_version_str = kstrdup(s, GFP_KERNEL);
	if (s && !tp->bios_version_str)
		return -ENOMEM;

	/* Really ancient ThinkPad 240X will fail this, which is fine */
	if (!(tpacpi_is_valid_fw_id(tp->bios_version_str, 'E') ||
	      tpacpi_is_valid_fw_id(tp->bios_version_str, 'C')))
		return 0;

	tp->bios_model = tp->bios_version_str[0]
			 | (tp->bios_version_str[1] << 8);
	tp->bios_release = (tp->bios_version_str[4] << 8)
			 | tp->bios_version_str[5];

	/*
	 * ThinkPad T23 or newer, A31 or newer, R50e or newer,
	 * X32 or newer, all Z series;  Some models must have an
	 * up-to-date BIOS or they will not be detected.
	 *
	 * See http://thinkwiki.org/wiki/List_of_DMI_IDs
	 */
	while ((dev = dmi_find_device(DMI_DEV_TYPE_OEM_STRING, NULL, dev))) {
		if (sscanf(dev->name,
			   "IBM ThinkPad Embedded Controller -[%17c",
			   ec_fw_string) == 1) {
			ec_fw_string[sizeof(ec_fw_string) - 1] = 0;
			ec_fw_string[strcspn(ec_fw_string, " ]")] = 0;

			tp->ec_version_str = kstrdup(ec_fw_string, GFP_KERNEL);
			if (!tp->ec_version_str)
				return -ENOMEM;

			if (tpacpi_is_valid_fw_id(ec_fw_string, 'H')) {
				tp->ec_model = ec_fw_string[0]
						| (ec_fw_string[1] << 8);
				tp->ec_release = (ec_fw_string[4] << 8)
						| ec_fw_string[5];
			} else {
				pr_notice("ThinkPad firmware release %s "
					  "doesn't match the known patterns\n",
					  ec_fw_string);
				pr_notice("please report this to %s\n",
					  TPACPI_MAIL);
			}
			break;
		}
	}

	s = dmi_get_system_info(DMI_PRODUCT_VERSION);
	if (s && !(strnicmp(s, "ThinkPad", 8) && strnicmp(s, "Lenovo", 6))) {
		tp->model_str = kstrdup(s, GFP_KERNEL);
		if (!tp->model_str)
			return -ENOMEM;
	} else {
		s = dmi_get_system_info(DMI_BIOS_VENDOR);
		if (s && !(strnicmp(s, "Lenovo", 6))) {
			tp->model_str = kstrdup(s, GFP_KERNEL);
			if (!tp->model_str)
				return -ENOMEM;
		}
	}

	s = dmi_get_system_info(DMI_PRODUCT_NAME);
	tp->nummodel_str = kstrdup(s, GFP_KERNEL);
	if (s && !tp->nummodel_str)
		return -ENOMEM;

	return 0;
}

static int __init probe_for_thinkpad(void)
{
	int is_thinkpad;

	if (acpi_disabled)
		return -ENODEV;

	/* It would be dangerous to run the driver in this case */
	if (!tpacpi_is_ibm() && !tpacpi_is_lenovo())
		return -ENODEV;

	/*
	 * Non-ancient models have better DMI tagging, but very old models
	 * don't.  tpacpi_is_fw_known() is a cheat to help in that case.
	 */
	is_thinkpad = (thinkpad_id.model_str != NULL) ||
		      (thinkpad_id.ec_model != 0) ||
		      tpacpi_is_fw_known();

	/* The EC handler is required */
	tpacpi_acpi_handle_locate("ec", TPACPI_ACPI_EC_HID, &ec_handle);
	if (!ec_handle) {
		if (is_thinkpad)
			pr_err("Not yet supported ThinkPad detected!\n");
		return -ENODEV;
	}

	if (!is_thinkpad && !force_load)
		return -ENODEV;

	return 0;
}

static void __init thinkpad_acpi_init_banner(void)
{
	pr_info("%s v%s\n", TPACPI_DESC, TPACPI_VERSION);
	pr_info("%s\n", TPACPI_URL);

	pr_info("ThinkPad BIOS %s, EC %s\n",
		(thinkpad_id.bios_version_str) ?
			thinkpad_id.bios_version_str : "unknown",
		(thinkpad_id.ec_version_str) ?
			thinkpad_id.ec_version_str : "unknown");

	BUG_ON(!thinkpad_id.vendor);

	if (thinkpad_id.model_str)
		pr_info("%s %s, model %s\n",
			(thinkpad_id.vendor == PCI_VENDOR_ID_IBM) ?
				"IBM" : ((thinkpad_id.vendor ==
						PCI_VENDOR_ID_LENOVO) ?
					"Lenovo" : "Unknown vendor"),
			thinkpad_id.model_str,
			(thinkpad_id.nummodel_str) ?
				thinkpad_id.nummodel_str : "unknown");
}

/* Module init, exit, parameters */

static struct ibm_init_struct ibms_init[] __initdata = {
	{
		.data = &thinkpad_acpi_driver_data,
	},
	{
		.init = hotkey_init,
		.data = &hotkey_driver_data,
	},
	{
		.init = bluetooth_init,
		.data = &bluetooth_driver_data,
	},
	{
		.init = wan_init,
		.data = &wan_driver_data,
	},
	{
		.init = uwb_init,
		.data = &uwb_driver_data,
	},
#ifdef CONFIG_THINKPAD_ACPI_VIDEO
	{
		.init = video_init,
		.base_procfs_mode = S_IRUSR,
		.data = &video_driver_data,
	},
#endif
	{
		.init = light_init,
		.data = &light_driver_data,
	},
	{
		.init = cmos_init,
		.data = &cmos_driver_data,
	},
	{
		.init = led_init,
		.data = &led_driver_data,
	},
	{
		.init = beep_init,
		.data = &beep_driver_data,
	},
	{
		.init = thermal_init,
		.data = &thermal_driver_data,
	},
	{
		.init = brightness_init,
		.data = &brightness_driver_data,
	},
	{
		.init = volume_init,
		.data = &volume_driver_data,
	},
	{
		.init = fan_init,
		.data = &fan_driver_data,
	},
	{
		.init = mute_led_init,
		.data = &mute_led_driver_data,
	},
};

static int __init set_ibm_param(const char *val, struct kernel_param *kp)
{
	unsigned int i;
	struct ibm_struct *ibm;

	if (!kp || !kp->name || !val)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ibms_init); i++) {
		ibm = ibms_init[i].data;
		WARN_ON(ibm == NULL);

		if (!ibm || !ibm->name)
			continue;

		if (strcmp(ibm->name, kp->name) == 0 && ibm->write) {
			if (strlen(val) > sizeof(ibms_init[i].param) - 2)
				return -ENOSPC;
			strcpy(ibms_init[i].param, val);
			strcat(ibms_init[i].param, ",");
			return 0;
		}
	}

	return -EINVAL;
}

module_param(experimental, int, 0444);
MODULE_PARM_DESC(experimental,
		 "Enables experimental features when non-zero");

module_param_named(debug, dbg_level, uint, 0);
MODULE_PARM_DESC(debug, "Sets debug level bit-mask");

module_param(force_load, bool, 0444);
MODULE_PARM_DESC(force_load,
		 "Attempts to load the driver even on a "
		 "mis-identified ThinkPad when true");

module_param_named(fan_control, fan_control_allowed, bool, 0444);
MODULE_PARM_DESC(fan_control,
		 "Enables setting fan parameters features when true");

module_param_named(brightness_mode, brightness_mode, uint, 0444);
MODULE_PARM_DESC(brightness_mode,
		 "Selects brightness control strategy: "
		 "0=auto, 1=EC, 2=UCMS, 3=EC+NVRAM");

module_param(brightness_enable, uint, 0444);
MODULE_PARM_DESC(brightness_enable,
		 "Enables backlight control when 1, disables when 0");

#ifdef CONFIG_THINKPAD_ACPI_ALSA_SUPPORT
module_param_named(volume_mode, volume_mode, uint, 0444);
MODULE_PARM_DESC(volume_mode,
		 "Selects volume control strategy: "
		 "0=auto, 1=EC, 2=N/A, 3=EC+NVRAM");

module_param_named(volume_capabilities, volume_capabilities, uint, 0444);
MODULE_PARM_DESC(volume_capabilities,
		 "Selects the mixer capabilites: "
		 "0=auto, 1=volume and mute, 2=mute only");

module_param_named(volume_control, volume_control_allowed, bool, 0444);
MODULE_PARM_DESC(volume_control,
		 "Enables software override for the console audio "
		 "control when true");

/* ALSA module API parameters */
module_param_named(index, alsa_index, int, 0444);
MODULE_PARM_DESC(index, "ALSA index for the ACPI EC Mixer");
module_param_named(id, alsa_id, charp, 0444);
MODULE_PARM_DESC(id, "ALSA id for the ACPI EC Mixer");
module_param_named(enable, alsa_enable, bool, 0444);
MODULE_PARM_DESC(enable, "Enable the ALSA interface for the ACPI EC Mixer");
#endif /* CONFIG_THINKPAD_ACPI_ALSA_SUPPORT */

#define TPACPI_PARAM(feature) \
	module_param_call(feature, set_ibm_param, NULL, NULL, 0); \
	MODULE_PARM_DESC(feature, "Simulates thinkpad-acpi procfs command " \
			 "at module load, see documentation")

TPACPI_PARAM(hotkey);
TPACPI_PARAM(bluetooth);
TPACPI_PARAM(video);
TPACPI_PARAM(light);
TPACPI_PARAM(cmos);
TPACPI_PARAM(led);
TPACPI_PARAM(beep);
TPACPI_PARAM(brightness);
TPACPI_PARAM(volume);
TPACPI_PARAM(fan);

#ifdef CONFIG_THINKPAD_ACPI_DEBUGFACILITIES
module_param(dbg_wlswemul, uint, 0444);
MODULE_PARM_DESC(dbg_wlswemul, "Enables WLSW emulation");
module_param_named(wlsw_state, tpacpi_wlsw_emulstate, bool, 0);
MODULE_PARM_DESC(wlsw_state,
		 "Initial state of the emulated WLSW switch");

module_param(dbg_bluetoothemul, uint, 0444);
MODULE_PARM_DESC(dbg_bluetoothemul, "Enables bluetooth switch emulation");
module_param_named(bluetooth_state, tpacpi_bluetooth_emulstate, bool, 0);
MODULE_PARM_DESC(bluetooth_state,
		 "Initial state of the emulated bluetooth switch");

module_param(dbg_wwanemul, uint, 0444);
MODULE_PARM_DESC(dbg_wwanemul, "Enables WWAN switch emulation");
module_param_named(wwan_state, tpacpi_wwan_emulstate, bool, 0);
MODULE_PARM_DESC(wwan_state,
		 "Initial state of the emulated WWAN switch");

module_param(dbg_uwbemul, uint, 0444);
MODULE_PARM_DESC(dbg_uwbemul, "Enables UWB switch emulation");
module_param_named(uwb_state, tpacpi_uwb_emulstate, bool, 0);
MODULE_PARM_DESC(uwb_state,
		 "Initial state of the emulated UWB switch");
#endif

static void thinkpad_acpi_module_exit(void)
{
	struct ibm_struct *ibm, *itmp;

	tpacpi_lifecycle = TPACPI_LIFE_EXITING;

	list_for_each_entry_safe_reverse(ibm, itmp,
					 &tpacpi_all_drivers,
					 all_drivers) {
		ibm_exit(ibm);
	}

	dbg_printk(TPACPI_DBG_INIT, "finished subdriver exit path...\n");

	if (tpacpi_inputdev) {
		if (tp_features.input_device_registered)
			input_unregister_device(tpacpi_inputdev);
		else
			input_free_device(tpacpi_inputdev);
		kfree(hotkey_keycode_map);
	}

	if (tpacpi_hwmon)
		hwmon_device_unregister(tpacpi_hwmon);

	if (tp_features.sensors_pdev_attrs_registered)
		device_remove_file(&tpacpi_sensors_pdev->dev,
				   &dev_attr_thinkpad_acpi_pdev_name);
	if (tpacpi_sensors_pdev)
		platform_device_unregister(tpacpi_sensors_pdev);
	if (tpacpi_pdev)
		platform_device_unregister(tpacpi_pdev);

	if (tp_features.sensors_pdrv_attrs_registered)
		tpacpi_remove_driver_attributes(&tpacpi_hwmon_pdriver.driver);
	if (tp_features.platform_drv_attrs_registered)
		tpacpi_remove_driver_attributes(&tpacpi_pdriver.driver);

	if (tp_features.sensors_pdrv_registered)
		platform_driver_unregister(&tpacpi_hwmon_pdriver);

	if (tp_features.platform_drv_registered)
		platform_driver_unregister(&tpacpi_pdriver);

	if (proc_dir)
		remove_proc_entry(TPACPI_PROC_DIR, acpi_root_dir);

	if (tpacpi_wq)
		destroy_workqueue(tpacpi_wq);

	kfree(thinkpad_id.bios_version_str);
	kfree(thinkpad_id.ec_version_str);
	kfree(thinkpad_id.model_str);
	kfree(thinkpad_id.nummodel_str);
}


static int __init thinkpad_acpi_module_init(void)
{
	int ret, i;

	tpacpi_lifecycle = TPACPI_LIFE_INIT;

	/* Driver-level probe */

	ret = get_thinkpad_model_data(&thinkpad_id);
	if (ret) {
		pr_err("unable to get DMI data: %d\n", ret);
		thinkpad_acpi_module_exit();
		return ret;
	}
	ret = probe_for_thinkpad();
	if (ret) {
		thinkpad_acpi_module_exit();
		return ret;
	}

	/* Driver initialization */

	thinkpad_acpi_init_banner();
	tpacpi_check_outdated_fw();

	TPACPI_ACPIHANDLE_INIT(ecrd);
	TPACPI_ACPIHANDLE_INIT(ecwr);

	tpacpi_wq = create_singlethread_workqueue(TPACPI_WORKQUEUE_NAME);
	if (!tpacpi_wq) {
		thinkpad_acpi_module_exit();
		return -ENOMEM;
	}

	proc_dir = proc_mkdir(TPACPI_PROC_DIR, acpi_root_dir);
	if (!proc_dir) {
		pr_err("unable to create proc dir " TPACPI_PROC_DIR "\n");
		thinkpad_acpi_module_exit();
		return -ENODEV;
	}

	ret = platform_driver_register(&tpacpi_pdriver);
	if (ret) {
		pr_err("unable to register main platform driver\n");
		thinkpad_acpi_module_exit();
		return ret;
	}
	tp_features.platform_drv_registered = 1;

	ret = platform_driver_register(&tpacpi_hwmon_pdriver);
	if (ret) {
		pr_err("unable to register hwmon platform driver\n");
		thinkpad_acpi_module_exit();
		return ret;
	}
	tp_features.sensors_pdrv_registered = 1;

	ret = tpacpi_create_driver_attributes(&tpacpi_pdriver.driver);
	if (!ret) {
		tp_features.platform_drv_attrs_registered = 1;
		ret = tpacpi_create_driver_attributes(
					&tpacpi_hwmon_pdriver.driver);
	}
	if (ret) {
		pr_err("unable to create sysfs driver attributes\n");
		thinkpad_acpi_module_exit();
		return ret;
	}
	tp_features.sensors_pdrv_attrs_registered = 1;


	/* Device initialization */
	tpacpi_pdev = platform_device_register_simple(TPACPI_DRVR_NAME, -1,
							NULL, 0);
	if (IS_ERR(tpacpi_pdev)) {
		ret = PTR_ERR(tpacpi_pdev);
		tpacpi_pdev = NULL;
		pr_err("unable to register platform device\n");
		thinkpad_acpi_module_exit();
		return ret;
	}
	tpacpi_sensors_pdev = platform_device_register_simple(
						TPACPI_HWMON_DRVR_NAME,
						-1, NULL, 0);
	if (IS_ERR(tpacpi_sensors_pdev)) {
		ret = PTR_ERR(tpacpi_sensors_pdev);
		tpacpi_sensors_pdev = NULL;
		pr_err("unable to register hwmon platform device\n");
		thinkpad_acpi_module_exit();
		return ret;
	}
	ret = device_create_file(&tpacpi_sensors_pdev->dev,
				 &dev_attr_thinkpad_acpi_pdev_name);
	if (ret) {
		pr_err("unable to create sysfs hwmon device attributes\n");
		thinkpad_acpi_module_exit();
		return ret;
	}
	tp_features.sensors_pdev_attrs_registered = 1;
	tpacpi_hwmon = hwmon_device_register(&tpacpi_sensors_pdev->dev);
	if (IS_ERR(tpacpi_hwmon)) {
		ret = PTR_ERR(tpacpi_hwmon);
		tpacpi_hwmon = NULL;
		pr_err("unable to register hwmon device\n");
		thinkpad_acpi_module_exit();
		return ret;
	}
	mutex_init(&tpacpi_inputdev_send_mutex);
	tpacpi_inputdev = input_allocate_device();
	if (!tpacpi_inputdev) {
		thinkpad_acpi_module_exit();
		return -ENOMEM;
	} else {
		/* Prepare input device, but don't register */
		tpacpi_inputdev->name = "ThinkPad Extra Buttons";
		tpacpi_inputdev->phys = TPACPI_DRVR_NAME "/input0";
		tpacpi_inputdev->id.bustype = BUS_HOST;
		tpacpi_inputdev->id.vendor = thinkpad_id.vendor;
		tpacpi_inputdev->id.product = TPACPI_HKEY_INPUT_PRODUCT;
		tpacpi_inputdev->id.version = TPACPI_HKEY_INPUT_VERSION;
		tpacpi_inputdev->dev.parent = &tpacpi_pdev->dev;
	}

	/* Init subdriver dependencies */
	tpacpi_detect_brightness_capabilities();

	/* Init subdrivers */
	for (i = 0; i < ARRAY_SIZE(ibms_init); i++) {
		ret = ibm_init(&ibms_init[i]);
		if (ret >= 0 && *ibms_init[i].param)
			ret = ibms_init[i].data->write(ibms_init[i].param);
		if (ret < 0) {
			thinkpad_acpi_module_exit();
			return ret;
		}
	}

	tpacpi_lifecycle = TPACPI_LIFE_RUNNING;

	ret = input_register_device(tpacpi_inputdev);
	if (ret < 0) {
		pr_err("unable to register input device\n");
		thinkpad_acpi_module_exit();
		return ret;
	} else {
		tp_features.input_device_registered = 1;
	}

	return 0;
}

MODULE_ALIAS(TPACPI_DRVR_SHORTNAME);

/*
 * This will autoload the driver in almost every ThinkPad
 * in widespread use.
 *
 * Only _VERY_ old models, like the 240, 240x and 570 lack
 * the HKEY event interface.
 */
MODULE_DEVICE_TABLE(acpi, ibm_htk_device_ids);

/*
 * DMI matching for module autoloading
 *
 * See http://thinkwiki.org/wiki/List_of_DMI_IDs
 * See http://thinkwiki.org/wiki/BIOS_Upgrade_Downloads
 *
 * Only models listed in thinkwiki will be supported, so add yours
 * if it is not there yet.
 */
#define IBM_BIOS_MODULE_ALIAS(__type) \
	MODULE_ALIAS("dmi:bvnIBM:bvr" __type "ET??WW*")

/* Ancient thinkpad BIOSes have to be identified by
 * BIOS type or model number, and there are far less
 * BIOS types than model numbers... */
IBM_BIOS_MODULE_ALIAS("I[MU]");		/* 570, 570e */

MODULE_AUTHOR("Borislav Deianov <borislav@users.sf.net>");
MODULE_AUTHOR("Henrique de Moraes Holschuh <hmh@hmh.eng.br>");
MODULE_DESCRIPTION(TPACPI_DESC);
MODULE_VERSION(TPACPI_VERSION);
MODULE_LICENSE("GPL");

module_init(thinkpad_acpi_module_init);
module_exit(thinkpad_acpi_module_exit);
