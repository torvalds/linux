/*
 *  asus_acpi.c - Asus Laptop ACPI Extras
 *
 *
 *  Copyright (C) 2002, 2003, 2004 Julien Lerouge, Karol Kozimor
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  The development page for this driver is located at
 *  http://sourceforge.net/projects/acpi4asus/
 *
 *  Credits:
 *  Pontus Fuchs   - Helper functions, cleanup
 *  Johann Wiesner - Small compile fixes
 *  John Belmonte  - ACPI code for Toshiba laptop was a good starting point.
 *
 *  TODO:
 *  add Fn key status
 *  Add mode selection on module loading (parameter) -> still necessary?
 *  Complete display switching -- may require dirty hacks or calling _DOS?
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <asm/uaccess.h>

#define ASUS_ACPI_VERSION "0.29"

#define PROC_ASUS       "asus"	//the directory
#define PROC_MLED       "mled"
#define PROC_WLED       "wled"
#define PROC_TLED       "tled"
#define PROC_INFO       "info"
#define PROC_LCD        "lcd"
#define PROC_BRN        "brn"
#define PROC_DISP       "disp"

#define ACPI_HOTK_NAME          "Asus Laptop ACPI Extras Driver"
#define ACPI_HOTK_CLASS         "hotkey"
#define ACPI_HOTK_DEVICE_NAME   "Hotkey"
#define ACPI_HOTK_HID           "ATK0100"

/*
 * Some events we use, same for all Asus
 */
#define BR_UP       0x10
#define BR_DOWN     0x20

/*
 * Flags for hotk status
 */
#define MLED_ON     0x01	//is MLED ON ?
#define WLED_ON     0x02
#define TLED_ON     0x04

MODULE_AUTHOR("Julien Lerouge, Karol Kozimor");
MODULE_DESCRIPTION(ACPI_HOTK_NAME);
MODULE_LICENSE("GPL");

static uid_t asus_uid;
static gid_t asus_gid;
module_param(asus_uid, uint, 0);
MODULE_PARM_DESC(uid, "UID for entries in /proc/acpi/asus.\n");
module_param(asus_gid, uint, 0);
MODULE_PARM_DESC(gid, "GID for entries in /proc/acpi/asus.\n");

/* For each model, all features implemented, 
 * those marked with R are relative to HOTK, A for absolute */
struct model_data {
	char *name;		//name of the laptop________________A
	char *mt_mled;		//method to handle mled_____________R
	char *mled_status;	//node to handle mled reading_______A
	char *mt_wled;		//method to handle wled_____________R
	char *wled_status;	//node to handle wled reading_______A
	char *mt_tled;		//method to handle tled_____________R
	char *tled_status;	//node to handle tled reading_______A
	char *mt_lcd_switch;	//method to turn LCD ON/OFF_________A
	char *lcd_status;	//node to read LCD panel state______A
	char *brightness_up;	//method to set brightness up_______A
	char *brightness_down;	//guess what ?______________________A
	char *brightness_set;	//method to set absolute brightness_R
	char *brightness_get;	//method to get absolute brightness_R
	char *brightness_status;	//node to get brightness____________A
	char *display_set;	//method to set video output________R
	char *display_get;	//method to get video output________R
};

/*
 * This is the main structure, we can use it to store anything interesting
 * about the hotk device
 */
struct asus_hotk {
	struct acpi_device *device;	//the device we are in
	acpi_handle handle;	//the handle of the hotk device
	char status;		//status of the hotk, for LEDs, ...
	struct model_data *methods;	//methods available on the laptop
	u8 brightness;		//brightness level
	enum {
		A1x = 0,	//A1340D, A1300F
		A2x,		//A2500H
		D1x,		//D1
		L2D,		//L2000D
		L3C,		//L3800C
		L3D,		//L3400D
		L3H,		//L3H, but also L2000E
		L4R,		//L4500R
		L5x,		//L5800C 
		L8L,		//L8400L
		M1A,		//M1300A
		M2E,		//M2400E, L4400L
		M6N,		//M6800N
		M6R,		//M6700R
		P30,		//Samsung P30
		S1x,		//S1300A, but also L1400B and M2400A (L84F)
		S2x,		//S200 (J1 reported), Victor MP-XP7210
		xxN,		//M2400N, M3700N, M5200N, S1300N, S5200N, W1OOON
		//(Centrino)
		END_MODEL
	} model;		//Models currently supported
	u16 event_count[128];	//count for each event TODO make this better
};

/* Here we go */
#define A1x_PREFIX "\\_SB.PCI0.ISA.EC0."
#define L3C_PREFIX "\\_SB.PCI0.PX40.ECD0."
#define M1A_PREFIX "\\_SB.PCI0.PX40.EC0."
#define P30_PREFIX "\\_SB.PCI0.LPCB.EC0."
#define S1x_PREFIX "\\_SB.PCI0.PX40."
#define S2x_PREFIX A1x_PREFIX
#define xxN_PREFIX "\\_SB.PCI0.SBRG.EC0."

static struct model_data model_conf[END_MODEL] = {
	/*
	 * Those pathnames are relative to the HOTK / ATKD device :
	 *       - mt_mled
	 *       - mt_wled
	 *       - brightness_set
	 *       - brightness_get
	 *       - display_set
	 *       - display_get
	 *
	 * TODO I have seen a SWBX and AIBX method on some models, like L1400B,
	 * it seems to be a kind of switch, but what for ?
	 *
	 */

	{
	 .name = "A1x",
	 .mt_mled = "MLED",
	 .mled_status = "\\MAIL",
	 .mt_lcd_switch = A1x_PREFIX "_Q10",
	 .lcd_status = "\\BKLI",
	 .brightness_up = A1x_PREFIX "_Q0E",
	 .brightness_down = A1x_PREFIX "_Q0F"},

	{
	 .name = "A2x",
	 .mt_mled = "MLED",
	 .mt_wled = "WLED",
	 .wled_status = "\\SG66",
	 .mt_lcd_switch = "\\Q10",
	 .lcd_status = "\\BAOF",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\INFB"},

	{
	 .name = "D1x",
	 .mt_mled = "MLED",
	 .mt_lcd_switch = "\\Q0D",
	 .lcd_status = "\\GP11",
	 .brightness_up = "\\Q0C",
	 .brightness_down = "\\Q0B",
	 .brightness_status = "\\BLVL",
	 .display_set = "SDSP",
	 .display_get = "\\INFB"},

	{
	 .name = "L2D",
	 .mt_mled = "MLED",
	 .mled_status = "\\SGP6",
	 .mt_wled = "WLED",
	 .wled_status = "\\RCP3",
	 .mt_lcd_switch = "\\Q10",
	 .lcd_status = "\\SGP0",
	 .brightness_up = "\\Q0E",
	 .brightness_down = "\\Q0F",
	 .display_set = "SDSP",
	 .display_get = "\\INFB"},

	{
	 .name = "L3C",
	 .mt_mled = "MLED",
	 .mt_wled = "WLED",
	 .mt_lcd_switch = L3C_PREFIX "_Q10",
	 .lcd_status = "\\GL32",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\_SB.PCI0.PCI1.VGAC.NMAP"},

	{
	 .name = "L3D",
	 .mt_mled = "MLED",
	 .mled_status = "\\MALD",
	 .mt_wled = "WLED",
	 .mt_lcd_switch = "\\Q10",
	 .lcd_status = "\\BKLG",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\INFB"},

	{
	 .name = "L3H",
	 .mt_mled = "MLED",
	 .mt_wled = "WLED",
	 .mt_lcd_switch = "EHK",
	 .lcd_status = "\\_SB.PCI0.PM.PBC",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\INFB"},

	{
	 .name = "L4R",
	 .mt_mled = "MLED",
	 .mt_wled = "WLED",
	 .wled_status = "\\_SB.PCI0.SBRG.SG13",
	 .mt_lcd_switch = xxN_PREFIX "_Q10",
	 .lcd_status = "\\_SB.PCI0.SBSM.SEO4",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\_SB.PCI0.P0P1.VGA.GETD"},

	{
	 .name = "L5x",
	 .mt_mled = "MLED",
/* WLED present, but not controlled by ACPI */
	 .mt_tled = "TLED",
	 .mt_lcd_switch = "\\Q0D",
	 .lcd_status = "\\BAOF",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\INFB"},

	{
	 .name = "L8L"
/* No features, but at least support the hotkeys */
	 },

	{
	 .name = "M1A",
	 .mt_mled = "MLED",
	 .mt_lcd_switch = M1A_PREFIX "Q10",
	 .lcd_status = "\\PNOF",
	 .brightness_up = M1A_PREFIX "Q0E",
	 .brightness_down = M1A_PREFIX "Q0F",
	 .brightness_status = "\\BRIT",
	 .display_set = "SDSP",
	 .display_get = "\\INFB"},

	{
	 .name = "M2E",
	 .mt_mled = "MLED",
	 .mt_wled = "WLED",
	 .mt_lcd_switch = "\\Q10",
	 .lcd_status = "\\GP06",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\INFB"},

	{
	 .name = "M6N",
	 .mt_mled = "MLED",
	 .mt_wled = "WLED",
	 .wled_status = "\\_SB.PCI0.SBRG.SG13",
	 .mt_lcd_switch = xxN_PREFIX "_Q10",
	 .lcd_status = "\\_SB.BKLT",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\SSTE"},
	{
	 .name = "M6R",
	 .mt_mled = "MLED",
	 .mt_wled = "WLED",
	 .mt_lcd_switch = xxN_PREFIX "_Q10",
	 .lcd_status = "\\_SB.PCI0.SBSM.SEO4",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\SSTE"},

	{
	 .name = "P30",
	 .mt_wled = "WLED",
	 .mt_lcd_switch = P30_PREFIX "_Q0E",
	 .lcd_status = "\\BKLT",
	 .brightness_up = P30_PREFIX "_Q68",
	 .brightness_down = P30_PREFIX "_Q69",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\DNXT"},

	{
	 .name = "S1x",
	 .mt_mled = "MLED",
	 .mled_status = "\\EMLE",
	 .mt_wled = "WLED",
	 .mt_lcd_switch = S1x_PREFIX "Q10",
	 .lcd_status = "\\PNOF",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV"},

	{
	 .name = "S2x",
	 .mt_mled = "MLED",
	 .mled_status = "\\MAIL",
	 .mt_lcd_switch = S2x_PREFIX "_Q10",
	 .lcd_status = "\\BKLI",
	 .brightness_up = S2x_PREFIX "_Q0B",
	 .brightness_down = S2x_PREFIX "_Q0A"},

	{
	 .name = "xxN",
	 .mt_mled = "MLED",
/* WLED present, but not controlled by ACPI */
	 .mt_lcd_switch = xxN_PREFIX "_Q10",
	 .lcd_status = "\\BKLT",
	 .brightness_set = "SPLV",
	 .brightness_get = "GPLV",
	 .display_set = "SDSP",
	 .display_get = "\\ADVG"}
};

/* procdir we use */
static struct proc_dir_entry *asus_proc_dir;

/*
 * This header is made available to allow proper configuration given model,
 * revision number , ... this info cannot go in struct asus_hotk because it is
 * available before the hotk
 */
static struct acpi_table_header *asus_info;

/* The actual device the driver binds to */
static struct asus_hotk *hotk;

/*
 * The hotkey driver declaration
 */
static int asus_hotk_add(struct acpi_device *device);
static int asus_hotk_remove(struct acpi_device *device, int type);
static struct acpi_driver asus_hotk_driver = {
	.name = ACPI_HOTK_NAME,
	.class = ACPI_HOTK_CLASS,
	.ids = ACPI_HOTK_HID,
	.ops = {
		.add = asus_hotk_add,
		.remove = asus_hotk_remove,
		},
};

/* 
 * This function evaluates an ACPI method, given an int as parameter, the
 * method is searched within the scope of the handle, can be NULL. The output
 * of the method is written is output, which can also be NULL
 *
 * returns 1 if write is successful, 0 else. 
 */
static int write_acpi_int(acpi_handle handle, const char *method, int val,
			  struct acpi_buffer *output)
{
	struct acpi_object_list params;	//list of input parameters (an int here)
	union acpi_object in_obj;	//the only param we use
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = val;

	status = acpi_evaluate_object(handle, (char *)method, &params, output);
	return (status == AE_OK);
}

static int read_acpi_int(acpi_handle handle, const char *method, int *val)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, (char *)method, NULL, &output);
	*val = out_obj.integer.value;
	return (status == AE_OK) && (out_obj.type == ACPI_TYPE_INTEGER);
}

/*
 * We write our info in page, we begin at offset off and cannot write more
 * than count bytes. We set eof to 1 if we handle those 2 values. We return the
 * number of bytes written in page
 */
static int
proc_read_info(char *page, char **start, off_t off, int count, int *eof,
	       void *data)
{
	int len = 0;
	int temp;
	char buf[16];		//enough for all info
	/*
	 * We use the easy way, we don't care of off and count, so we don't set eof
	 * to 1
	 */

	len += sprintf(page, ACPI_HOTK_NAME " " ASUS_ACPI_VERSION "\n");
	len += sprintf(page + len, "Model reference    : %s\n",
		       hotk->methods->name);
	/* 
	 * The SFUN method probably allows the original driver to get the list 
	 * of features supported by a given model. For now, 0x0100 or 0x0800 
	 * bit signifies that the laptop is equipped with a Wi-Fi MiniPCI card.
	 * The significance of others is yet to be found.
	 */
	if (read_acpi_int(hotk->handle, "SFUN", &temp))
		len +=
		    sprintf(page + len, "SFUN value         : 0x%04x\n", temp);
	/*
	 * Another value for userspace: the ASYM method returns 0x02 for
	 * battery low and 0x04 for battery critical, its readings tend to be
	 * more accurate than those provided by _BST. 
	 * Note: since not all the laptops provide this method, errors are
	 * silently ignored.
	 */
	if (read_acpi_int(hotk->handle, "ASYM", &temp))
		len +=
		    sprintf(page + len, "ASYM value         : 0x%04x\n", temp);
	if (asus_info) {
		snprintf(buf, 16, "%d", asus_info->length);
		len += sprintf(page + len, "DSDT length        : %s\n", buf);
		snprintf(buf, 16, "%d", asus_info->checksum);
		len += sprintf(page + len, "DSDT checksum      : %s\n", buf);
		snprintf(buf, 16, "%d", asus_info->revision);
		len += sprintf(page + len, "DSDT revision      : %s\n", buf);
		snprintf(buf, 7, "%s", asus_info->oem_id);
		len += sprintf(page + len, "OEM id             : %s\n", buf);
		snprintf(buf, 9, "%s", asus_info->oem_table_id);
		len += sprintf(page + len, "OEM table id       : %s\n", buf);
		snprintf(buf, 16, "%x", asus_info->oem_revision);
		len += sprintf(page + len, "OEM revision       : 0x%s\n", buf);
		snprintf(buf, 5, "%s", asus_info->asl_compiler_id);
		len += sprintf(page + len, "ASL comp vendor id : %s\n", buf);
		snprintf(buf, 16, "%x", asus_info->asl_compiler_revision);
		len += sprintf(page + len, "ASL comp revision  : 0x%s\n", buf);
	}

	return len;
}

/*
 * /proc handlers
 * We write our info in page, we begin at offset off and cannot write more
 * than count bytes. We set eof to 1 if we handle those 2 values. We return the
 * number of bytes written in page
 */

/* Generic LED functions */
static int read_led(const char *ledname, int ledmask)
{
	if (ledname) {
		int led_status;

		if (read_acpi_int(NULL, ledname, &led_status))
			return led_status;
		else
			printk(KERN_WARNING "Asus ACPI: Error reading LED "
			       "status\n");
	}
	return (hotk->status & ledmask) ? 1 : 0;
}

static int parse_arg(const char __user * buf, unsigned long count, int *val)
{
	char s[32];
	if (!count)
		return 0;
	if (count > 31)
		return -EINVAL;
	if (copy_from_user(s, buf, count))
		return -EFAULT;
	s[count] = 0;
	if (sscanf(s, "%i", val) != 1)
		return -EINVAL;
	return count;
}

/* FIXME: kill extraneous args so it can be called independently */
static int
write_led(const char __user * buffer, unsigned long count,
	  char *ledname, int ledmask, int invert)
{
	int value;
	int led_out = 0;

	count = parse_arg(buffer, count, &value);
	if (count > 0)
		led_out = value ? 1 : 0;

	hotk->status =
	    (led_out) ? (hotk->status | ledmask) : (hotk->status & ~ledmask);

	if (invert)		/* invert target value */
		led_out = !led_out & 0x1;

	if (!write_acpi_int(hotk->handle, ledname, led_out, NULL))
		printk(KERN_WARNING "Asus ACPI: LED (%s) write failed\n",
		       ledname);

	return count;
}

/*
 * Proc handlers for MLED
 */
static int
proc_read_mled(char *page, char **start, off_t off, int count, int *eof,
	       void *data)
{
	return sprintf(page, "%d\n",
		       read_led(hotk->methods->mled_status, MLED_ON));
}

static int
proc_write_mled(struct file *file, const char __user * buffer,
		unsigned long count, void *data)
{
	return write_led(buffer, count, hotk->methods->mt_mled, MLED_ON, 1);
}

/*
 * Proc handlers for WLED
 */
static int
proc_read_wled(char *page, char **start, off_t off, int count, int *eof,
	       void *data)
{
	return sprintf(page, "%d\n",
		       read_led(hotk->methods->wled_status, WLED_ON));
}

static int
proc_write_wled(struct file *file, const char __user * buffer,
		unsigned long count, void *data)
{
	return write_led(buffer, count, hotk->methods->mt_wled, WLED_ON, 0);
}

/*
 * Proc handlers for TLED
 */
static int
proc_read_tled(char *page, char **start, off_t off, int count, int *eof,
	       void *data)
{
	return sprintf(page, "%d\n",
		       read_led(hotk->methods->tled_status, TLED_ON));
}

static int
proc_write_tled(struct file *file, const char __user * buffer,
		unsigned long count, void *data)
{
	return write_led(buffer, count, hotk->methods->mt_tled, TLED_ON, 0);
}

static int get_lcd_state(void)
{
	int lcd = 0;

	if (hotk->model != L3H) {
		/* We don't have to check anything if we are here */
		if (!read_acpi_int(NULL, hotk->methods->lcd_status, &lcd))
			printk(KERN_WARNING
			       "Asus ACPI: Error reading LCD status\n");

		if (hotk->model == L2D)
			lcd = ~lcd;
	} else {		/* L3H and the like have to be handled differently */
		acpi_status status = 0;
		struct acpi_object_list input;
		union acpi_object mt_params[2];
		struct acpi_buffer output;
		union acpi_object out_obj;

		input.count = 2;
		input.pointer = mt_params;
		/* Note: the following values are partly guessed up, but 
		   otherwise they seem to work */
		mt_params[0].type = ACPI_TYPE_INTEGER;
		mt_params[0].integer.value = 0x02;
		mt_params[1].type = ACPI_TYPE_INTEGER;
		mt_params[1].integer.value = 0x02;

		output.length = sizeof(out_obj);
		output.pointer = &out_obj;

		status =
		    acpi_evaluate_object(NULL, hotk->methods->lcd_status,
					 &input, &output);
		if (status != AE_OK)
			return -1;
		if (out_obj.type == ACPI_TYPE_INTEGER)
			/* That's what the AML code does */
			lcd = out_obj.integer.value >> 8;
	}

	return (lcd & 1);
}

static int set_lcd_state(int value)
{
	int lcd = 0;
	acpi_status status = 0;

	lcd = value ? 1 : 0;
	if (lcd != get_lcd_state()) {
		/* switch */
		if (hotk->model != L3H) {
			status =
			    acpi_evaluate_object(NULL,
						 hotk->methods->mt_lcd_switch,
						 NULL, NULL);
		} else {	/* L3H and the like have to be handled differently */
			if (!write_acpi_int
			    (hotk->handle, hotk->methods->mt_lcd_switch, 0x07,
			     NULL))
				status = AE_ERROR;
			/* L3H's AML executes EHK (0x07) upon Fn+F7 keypress, 
			   the exact behaviour is simulated here */
		}
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING "Asus ACPI: Error switching LCD\n");
	}
	return 0;

}

static int
proc_read_lcd(char *page, char **start, off_t off, int count, int *eof,
	      void *data)
{
	return sprintf(page, "%d\n", get_lcd_state());
}

static int
proc_write_lcd(struct file *file, const char __user * buffer,
	       unsigned long count, void *data)
{
	int value;

	count = parse_arg(buffer, count, &value);
	if (count > 0)
		set_lcd_state(value);
	return count;
}

static int read_brightness(void)
{
	int value;

	if (hotk->methods->brightness_get) {	/* SPLV/GPLV laptop */
		if (!read_acpi_int(hotk->handle, hotk->methods->brightness_get,
				   &value))
			printk(KERN_WARNING
			       "Asus ACPI: Error reading brightness\n");
	} else if (hotk->methods->brightness_status) {	/* For D1 for example */
		if (!read_acpi_int(NULL, hotk->methods->brightness_status,
				   &value))
			printk(KERN_WARNING
			       "Asus ACPI: Error reading brightness\n");
	} else			/* No GPLV method */
		value = hotk->brightness;
	return value;
}

/*
 * Change the brightness level
 */
static void set_brightness(int value)
{
	acpi_status status = 0;

	/* SPLV laptop */
	if (hotk->methods->brightness_set) {
		if (!write_acpi_int(hotk->handle, hotk->methods->brightness_set,
				    value, NULL))
			printk(KERN_WARNING
			       "Asus ACPI: Error changing brightness\n");
		return;
	}

	/* No SPLV method if we are here, act as appropriate */
	value -= read_brightness();
	while (value != 0) {
		status = acpi_evaluate_object(NULL, (value > 0) ?
					      hotk->methods->brightness_up :
					      hotk->methods->brightness_down,
					      NULL, NULL);
		(value > 0) ? value-- : value++;
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING
			       "Asus ACPI: Error changing brightness\n");
	}
	return;
}

static int
proc_read_brn(char *page, char **start, off_t off, int count, int *eof,
	      void *data)
{
	return sprintf(page, "%d\n", read_brightness());
}

static int
proc_write_brn(struct file *file, const char __user * buffer,
	       unsigned long count, void *data)
{
	int value;

	count = parse_arg(buffer, count, &value);
	if (count > 0) {
		value = (0 < value) ? ((15 < value) ? 15 : value) : 0;
		/* 0 <= value <= 15 */
		set_brightness(value);
	} else if (count < 0) {
		printk(KERN_WARNING "Asus ACPI: Error reading user input\n");
	}

	return count;
}

static void set_display(int value)
{
	/* no sanity check needed for now */
	if (!write_acpi_int(hotk->handle, hotk->methods->display_set,
			    value, NULL))
		printk(KERN_WARNING "Asus ACPI: Error setting display\n");
	return;
}

/*
 * Now, *this* one could be more user-friendly, but so far, no-one has 
 * complained. The significance of bits is the same as in proc_write_disp()
 */
static int
proc_read_disp(char *page, char **start, off_t off, int count, int *eof,
	       void *data)
{
	int value = 0;

	if (!read_acpi_int(hotk->handle, hotk->methods->display_get, &value))
		printk(KERN_WARNING
		       "Asus ACPI: Error reading display status\n");
	value &= 0x07;		/* needed for some models, shouldn't hurt others */
	return sprintf(page, "%d\n", value);
}

/*
 * Experimental support for display switching. As of now: 1 should activate 
 * the LCD output, 2 should do for CRT, and 4 for TV-Out. Any combination 
 * (bitwise) of these will suffice. I never actually tested 3 displays hooked up 
 * simultaneously, so be warned. See the acpi4asus README for more info.
 */
static int
proc_write_disp(struct file *file, const char __user * buffer,
		unsigned long count, void *data)
{
	int value;

	count = parse_arg(buffer, count, &value);
	if (count > 0)
		set_display(value);
	else if (count < 0)
		printk(KERN_WARNING "Asus ACPI: Error reading user input\n");

	return count;
}

typedef int (proc_readfunc) (char *page, char **start, off_t off, int count,
			     int *eof, void *data);
typedef int (proc_writefunc) (struct file * file, const char __user * buffer,
			      unsigned long count, void *data);

static int
__init asus_proc_add(char *name, proc_writefunc * writefunc,
		     proc_readfunc * readfunc, mode_t mode,
		     struct acpi_device *device)
{
	struct proc_dir_entry *proc =
	    create_proc_entry(name, mode, acpi_device_dir(device));
	if (!proc) {
		printk(KERN_WARNING "  Unable to create %s fs entry\n", name);
		return -1;
	}
	proc->write_proc = writefunc;
	proc->read_proc = readfunc;
	proc->data = acpi_driver_data(device);
	proc->owner = THIS_MODULE;
	proc->uid = asus_uid;
	proc->gid = asus_gid;
	return 0;
}

static int __init asus_hotk_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *proc;
	mode_t mode;

	/*
	 * If parameter uid or gid is not changed, keep the default setting for
	 * our proc entries (-rw-rw-rw-) else, it means we care about security,
	 * and then set to -rw-rw----
	 */

	if ((asus_uid == 0) && (asus_gid == 0)) {
		mode = S_IFREG | S_IRUGO | S_IWUGO;
	} else {
		mode = S_IFREG | S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP;
	}

	acpi_device_dir(device) = asus_proc_dir;
	if (!acpi_device_dir(device))
		return -ENODEV;

	proc = create_proc_entry(PROC_INFO, mode, acpi_device_dir(device));
	if (proc) {
		proc->read_proc = proc_read_info;
		proc->data = acpi_driver_data(device);
		proc->owner = THIS_MODULE;
		proc->uid = asus_uid;
		proc->gid = asus_gid;
	} else {
		printk(KERN_WARNING "  Unable to create " PROC_INFO
		       " fs entry\n");
	}

	if (hotk->methods->mt_wled) {
		asus_proc_add(PROC_WLED, &proc_write_wled, &proc_read_wled,
			      mode, device);
	}

	if (hotk->methods->mt_mled) {
		asus_proc_add(PROC_MLED, &proc_write_mled, &proc_read_mled,
			      mode, device);
	}

	if (hotk->methods->mt_tled) {
		asus_proc_add(PROC_TLED, &proc_write_tled, &proc_read_tled,
			      mode, device);
	}

	/* 
	 * We need both read node and write method as LCD switch is also accessible
	 * from keyboard 
	 */
	if (hotk->methods->mt_lcd_switch && hotk->methods->lcd_status) {
		asus_proc_add(PROC_LCD, &proc_write_lcd, &proc_read_lcd, mode,
			      device);
	}

	if ((hotk->methods->brightness_up && hotk->methods->brightness_down) ||
	    (hotk->methods->brightness_get && hotk->methods->brightness_set)) {
		asus_proc_add(PROC_BRN, &proc_write_brn, &proc_read_brn, mode,
			      device);
	}

	if (hotk->methods->display_set) {
		asus_proc_add(PROC_DISP, &proc_write_disp, &proc_read_disp,
			      mode, device);
	}

	return 0;
}

static int asus_hotk_remove_fs(struct acpi_device *device)
{
	if (acpi_device_dir(device)) {
		remove_proc_entry(PROC_INFO, acpi_device_dir(device));
		if (hotk->methods->mt_wled)
			remove_proc_entry(PROC_WLED, acpi_device_dir(device));
		if (hotk->methods->mt_mled)
			remove_proc_entry(PROC_MLED, acpi_device_dir(device));
		if (hotk->methods->mt_tled)
			remove_proc_entry(PROC_TLED, acpi_device_dir(device));
		if (hotk->methods->mt_lcd_switch && hotk->methods->lcd_status)
			remove_proc_entry(PROC_LCD, acpi_device_dir(device));
		if ((hotk->methods->brightness_up
		     && hotk->methods->brightness_down)
		    || (hotk->methods->brightness_get
			&& hotk->methods->brightness_set))
			remove_proc_entry(PROC_BRN, acpi_device_dir(device));
		if (hotk->methods->display_set)
			remove_proc_entry(PROC_DISP, acpi_device_dir(device));
	}
	return 0;
}

static void asus_hotk_notify(acpi_handle handle, u32 event, void *data)
{
	/* TODO Find a better way to handle events count. */
	if (!hotk)
		return;

	if ((event & ~((u32) BR_UP)) < 16) {
		hotk->brightness = (event & ~((u32) BR_UP));
	} else if ((event & ~((u32) BR_DOWN)) < 16) {
		hotk->brightness = (event & ~((u32) BR_DOWN));
	}

	acpi_bus_generate_event(hotk->device, event,
				hotk->event_count[event % 128]++);

	return;
}

/*
 * This function is used to initialize the hotk with right values. In this
 * method, we can make all the detection we want, and modify the hotk struct
 */
static int __init asus_hotk_get_info(void)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer dsdt = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *model = NULL;
	int bsts_result;
	acpi_status status;

	/*
	 * Get DSDT headers early enough to allow for differentiating between 
	 * models, but late enough to allow acpi_bus_register_driver() to fail 
	 * before doing anything ACPI-specific. Should we encounter a machine,
	 * which needs special handling (i.e. its hotkey device has a different
	 * HID), this bit will be moved. A global variable asus_info contains
	 * the DSDT header.
	 */
	status = acpi_get_table(ACPI_TABLE_DSDT, 1, &dsdt);
	if (ACPI_FAILURE(status))
		printk(KERN_WARNING "  Couldn't get the DSDT table header\n");
	else
		asus_info = (struct acpi_table_header *)dsdt.pointer;

	/* We have to write 0 on init this far for all ASUS models */
	if (!write_acpi_int(hotk->handle, "INIT", 0, &buffer)) {
		printk(KERN_ERR "  Hotkey initialization failed\n");
		return -ENODEV;
	}

	/* This needs to be called for some laptops to init properly */
	if (!read_acpi_int(hotk->handle, "BSTS", &bsts_result))
		printk(KERN_WARNING "  Error calling BSTS\n");
	else if (bsts_result)
		printk(KERN_NOTICE "  BSTS called, 0x%02x returned\n",
		       bsts_result);

	/* Samsung P30 has a device with a valid _HID whose INIT does not 
	 * return anything. Catch this one and any similar here */
	if (buffer.pointer == NULL) {
		if (asus_info &&	/* Samsung P30 */
		    strncmp(asus_info->oem_table_id, "ODEM", 4) == 0) {
			hotk->model = P30;
			printk(KERN_NOTICE
			       "  Samsung P30 detected, supported\n");
		} else {
			hotk->model = M2E;
			printk(KERN_WARNING "  no string returned by INIT\n");
			printk(KERN_WARNING "  trying default values, supply "
			       "the developers with your DSDT\n");
		}
		hotk->methods = &model_conf[hotk->model];
		return AE_OK;
	}

	model = (union acpi_object *)buffer.pointer;
	if (model->type == ACPI_TYPE_STRING) {
		printk(KERN_NOTICE "  %s model detected, ",
		       model->string.pointer);
	}

	hotk->model = END_MODEL;
	if (strncmp(model->string.pointer, "L3D", 3) == 0)
		hotk->model = L3D;
	else if (strncmp(model->string.pointer, "L3H", 3) == 0 ||
		 strncmp(model->string.pointer, "L2E", 3) == 0)
		hotk->model = L3H;
	else if (strncmp(model->string.pointer, "L3", 2) == 0 ||
		 strncmp(model->string.pointer, "L2B", 3) == 0)
		hotk->model = L3C;
	else if (strncmp(model->string.pointer, "L8L", 3) == 0)
		hotk->model = L8L;
	else if (strncmp(model->string.pointer, "L4R", 3) == 0)
		hotk->model = L4R;
	else if (strncmp(model->string.pointer, "M6N", 3) == 0)
		hotk->model = M6N;
	else if (strncmp(model->string.pointer, "M6R", 3) == 0)
		hotk->model = M6R;
	else if (strncmp(model->string.pointer, "M2N", 3) == 0 ||
		 strncmp(model->string.pointer, "M3N", 3) == 0 ||
		 strncmp(model->string.pointer, "M5N", 3) == 0 ||
		 strncmp(model->string.pointer, "M6N", 3) == 0 ||
		 strncmp(model->string.pointer, "S1N", 3) == 0 ||
		 strncmp(model->string.pointer, "S5N", 3) == 0 ||
		 strncmp(model->string.pointer, "W1N", 3) == 0)
		hotk->model = xxN;
	else if (strncmp(model->string.pointer, "M1", 2) == 0)
		hotk->model = M1A;
	else if (strncmp(model->string.pointer, "M2", 2) == 0 ||
		 strncmp(model->string.pointer, "L4E", 3) == 0)
		hotk->model = M2E;
	else if (strncmp(model->string.pointer, "L2", 2) == 0)
		hotk->model = L2D;
	else if (strncmp(model->string.pointer, "L8", 2) == 0)
		hotk->model = S1x;
	else if (strncmp(model->string.pointer, "D1", 2) == 0)
		hotk->model = D1x;
	else if (strncmp(model->string.pointer, "A1", 2) == 0)
		hotk->model = A1x;
	else if (strncmp(model->string.pointer, "A2", 2) == 0)
		hotk->model = A2x;
	else if (strncmp(model->string.pointer, "J1", 2) == 0)
		hotk->model = S2x;
	else if (strncmp(model->string.pointer, "L5", 2) == 0)
		hotk->model = L5x;

	if (hotk->model == END_MODEL) {
		printk("unsupported, trying default values, supply the "
		       "developers with your DSDT\n");
		hotk->model = M2E;
	} else {
		printk("supported\n");
	}

	hotk->methods = &model_conf[hotk->model];

	/* Sort of per-model blacklist */
	if (strncmp(model->string.pointer, "L2B", 3) == 0)
		hotk->methods->lcd_status = NULL;
	/* L2B is similar enough to L3C to use its settings, with this only 
	   exception */
	else if (strncmp(model->string.pointer, "S5N", 3) == 0 ||
		 strncmp(model->string.pointer, "M5N", 3) == 0)
		hotk->methods->mt_mled = NULL;
	/* S5N and M5N have no MLED */
	else if (strncmp(model->string.pointer, "M2N", 3) == 0 ||
		 strncmp(model->string.pointer, "W1N", 3) == 0)
		hotk->methods->mt_wled = "WLED";
	/* M2N and W1N have a usable WLED */
	else if (asus_info) {
		if (strncmp(asus_info->oem_table_id, "L1", 2) == 0)
			hotk->methods->mled_status = NULL;
		/* S1300A reports L84F, but L1400B too, account for that */
	}

	acpi_os_free(model);

	return AE_OK;
}

static int __init asus_hotk_check(void)
{
	int result = 0;

	result = acpi_bus_get_status(hotk->device);
	if (result)
		return result;

	if (hotk->device->status.present) {
		result = asus_hotk_get_info();
	} else {
		printk(KERN_ERR "  Hotkey device not present, aborting\n");
		return -EINVAL;
	}

	return result;
}

static int __init asus_hotk_add(struct acpi_device *device)
{
	acpi_status status = AE_OK;
	int result;

	if (!device)
		return -EINVAL;

	printk(KERN_NOTICE "Asus Laptop ACPI Extras version %s\n",
	       ASUS_ACPI_VERSION);

	hotk =
	    (struct asus_hotk *)kmalloc(sizeof(struct asus_hotk), GFP_KERNEL);
	if (!hotk)
		return -ENOMEM;
	memset(hotk, 0, sizeof(struct asus_hotk));

	hotk->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_HOTK_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_HOTK_CLASS);
	acpi_driver_data(device) = hotk;
	hotk->device = device;

	result = asus_hotk_check();
	if (result)
		goto end;

	result = asus_hotk_add_fs(device);
	if (result)
		goto end;

	/*
	 * We install the handler, it will receive the hotk in parameter, so, we
	 * could add other data to the hotk struct
	 */
	status = acpi_install_notify_handler(hotk->handle, ACPI_SYSTEM_NOTIFY,
					     asus_hotk_notify, hotk);
	if (ACPI_FAILURE(status))
		printk(KERN_ERR "  Error installing notify handler\n");

	/* For laptops without GPLV: init the hotk->brightness value */
	if ((!hotk->methods->brightness_get)
	    && (!hotk->methods->brightness_status)
	    && (hotk->methods->brightness_up
		&& hotk->methods->brightness_down)) {
		status =
		    acpi_evaluate_object(NULL, hotk->methods->brightness_down,
					 NULL, NULL);
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING "  Error changing brightness\n");
		else {
			status =
			    acpi_evaluate_object(NULL,
						 hotk->methods->brightness_up,
						 NULL, NULL);
			if (ACPI_FAILURE(status))
				printk(KERN_WARNING "  Strange, error changing"
				       " brightness\n");
		}
	}

      end:
	if (result) {
		kfree(hotk);
	}

	return result;
}

static int asus_hotk_remove(struct acpi_device *device, int type)
{
	acpi_status status = 0;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	status = acpi_remove_notify_handler(hotk->handle, ACPI_SYSTEM_NOTIFY,
					    asus_hotk_notify);
	if (ACPI_FAILURE(status))
		printk(KERN_ERR "Asus ACPI: Error removing notify handler\n");

	asus_hotk_remove_fs(device);

	kfree(hotk);

	return 0;
}

static int __init asus_acpi_init(void)
{
	int result;

	if (acpi_disabled)
		return -ENODEV;

	if (!acpi_specific_hotkey_enabled) {
		printk(KERN_ERR "Using generic hotkey driver\n");
		return -ENODEV;
	}
	asus_proc_dir = proc_mkdir(PROC_ASUS, acpi_root_dir);
	if (!asus_proc_dir) {
		printk(KERN_ERR "Asus ACPI: Unable to create /proc entry\n");
		return -ENODEV;
	}
	asus_proc_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&asus_hotk_driver);
	if (result < 1) {
		acpi_bus_unregister_driver(&asus_hotk_driver);
		remove_proc_entry(PROC_ASUS, acpi_root_dir);
		return -ENODEV;
	}

	return 0;
}

static void __exit asus_acpi_exit(void)
{
	acpi_bus_unregister_driver(&asus_hotk_driver);
	remove_proc_entry(PROC_ASUS, acpi_root_dir);

	acpi_os_free(asus_info);

	return;
}

module_init(asus_acpi_init);
module_exit(asus_acpi_exit);
