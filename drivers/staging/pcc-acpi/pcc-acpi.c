/*
 *  Panasonic HotKey and lcd brightness control Extra driver
 *  (C) 2004 Hiroshi Miura <miura@da-cha.org>
 *  (C) 2004 NTT DATA Intellilink Co. http://www.intellilink.co.jp/
 *  (C) YOKOTA Hiroshi <yokota (at) netlab. is. tsukuba. ac. jp>
 *  (C) 2004 David Bronaugh <dbronaugh>
 *
 *  derived from toshiba_acpi.c, Copyright (C) 2002-2004 John Belmonte
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publicshed by the Free Software Foundation.
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
 *---------------------------------------------------------------------------
 *
 * ChangeLog:
 *
 * 	Nov.04, 2006	Hiroshi Miura <miura@da-cha.org>
 * 		-v0.9	remove warning about section reference.
 * 			remove acpi_os_free
 * 			add /proc/acpi/pcc/brightness interface to allow HAL to access.
 * 			merge dbronaugh's enhancement
 * 			Aug.17, 2004 David Bronaugh (dbronaugh)
 *  				- Added screen brightness setting interface
 *				  Thanks to the FreeBSD crew (acpi_panasonic.c authors)
 * 				  for the ideas I needed to accomplish it
 *
 *	May.29, 2006	Hiroshi Miura <miura@da-cha.org>
 *		-v0.8.4 follow to change keyinput structure
 *			thanks Fabian Yamaguchi <fabs@cs.tu-berlin.de>,
 *			Jacob Bower <jacob.bower@ic.ac.uk> and
 *			Hiroshi Yokota for providing solutions.
 *
 *	Oct.02, 2004	Hiroshi Miura <miura@da-cha.org>
 *		-v0.8.2	merge code of YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>.
 *			Add sticky key mode interface.
 *			Refactoring acpi_pcc_generete_keyinput().
 *
 *	Sep.15, 2004	Hiroshi Miura <miura@da-cha.org>
 *		-v0.8	Generate key input event on input subsystem.
 *			This is based on yet another driver written by Ryuta Nakanishi.
 *
 *	Sep.10, 2004	Hiroshi Miura <miura@da-cha.org>
 *		-v0.7	Change proc interface functions using seq_file
 *			facility as same as other ACPI drivers.
 *
 *	Aug.28, 2004	Hiroshi Miura <miura@da-cha.org>
 *		-v0.6.4 Fix a silly error with status checking
 *
 *	Aug.25, 2004	Hiroshi Miura <miura@da-cha.org>
 *		-v0.6.3 replace read_acpi_int by standard function acpi_evaluate_integer
 *			some clean up and make smart copyright notice.
 *			fix return value of pcc_acpi_get_key()
 *			fix checking return value of acpi_bus_register_driver()
 *
 *      Aug.22, 2004    David Bronaugh <dbronaugh@linuxboxen.org>
 *              -v0.6.2 Add check on ACPI data (num_sifr)
 *                      Coding style cleanups, better error messages/handling
 *			Fixed an off-by-one error in memory allocation
 *
 *      Aug.21, 2004    David Bronaugh <dbronaugh@linuxboxen.org>
 *              -v0.6.1 Fix a silly error with status checking
 *
 *      Aug.20, 2004    David Bronaugh <dbronaugh@linuxboxen.org>
 *              - v0.6  Correct brightness controls to reflect reality
 *                      based on information gleaned by Hiroshi Miura
 *                      and discussions with Hiroshi Miura
 *
 *	Aug.10, 2004	Hiroshi Miura <miura@da-cha.org>
 *		- v0.5  support LCD brightness control
 *			based on the disclosed information by MEI.
 *
 *	Jul.25, 2004	Hiroshi Miura <miura@da-cha.org>
 *		- v0.4  first post version
 *		        add function to retrive SIFR
 *
 *	Jul.24, 2004	Hiroshi Miura <miura@da-cha.org>
 *		- v0.3  get proper status of hotkey
 *
 *      Jul.22, 2004	Hiroshi Miura <miura@da-cha.org>
 *		- v0.2  add HotKey handler
 *
 *      Jul.17, 2004	Hiroshi Miura <miura@da-cha.org>
 *		- v0.1  start from toshiba_acpi driver written by John Belmonte
 *
 */

#define ACPI_PCC_VERSION	"0.9+hy"

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <asm/uaccess.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>


/*************************************************************************
 * "seq" file template definition.
 */
/* "seq" initializer */
#define SEQ_OPEN_FS(_open_func_name_, _show_func_name_) \
static int _open_func_name_(struct inode *inode, struct file *file) \
{								      \
	return single_open(file, _show_func_name_, PDE(inode)->data);  \
}

/*-------------------------------------------------------------------------
 * "seq" fops template for read-only files.
 */
#define SEQ_FILEOPS_R(_open_func_name_) \
{ \
	.open	 = _open_func_name_,		  \
	.read	 = seq_read,			  \
	.llseek	 = seq_lseek,			  \
	.release = single_release,		  \
}

/*------------------------------------------------------------------------
 * "seq" fops template for read-write files.
 */
#define SEQ_FILEOPS_RW(_open_func_name_, _write_func_name_) \
{ \
	.open	 = _open_func_name_ ,		  \
	.read	 = seq_read,			  \
	.write	 = _write_func_name_,		  \
	.llseek	 = seq_lseek,			  \
	.release = single_release,		  \
}

/*
 * "seq" file template definition ended.
 ***************************************************************************
 */
#ifndef ACPI_HOTKEY_COMPONENT
#define ACPI_HOTKEY_COMPONENT	0x10000000
#endif

#define _COMPONENT		ACPI_HOTKEY_COMPONENT
ACPI_MODULE_NAME("pcc_acpi");

MODULE_AUTHOR("Hiroshi Miura, Hiroshi Yokota");
MODULE_DESCRIPTION("ACPI HotKey driver for Panasonic Let's Note laptops");
MODULE_LICENSE("GPL");

#define LOGPREFIX "pcc_acpi: "

/****************************************************
 * Define ACPI PATHs
 ****************************************************/
/* Lets note hotkeys */
#define METHOD_HKEY_QUERY	"HINF"
#define METHOD_HKEY_SQTY	"SQTY"
#define METHOD_HKEY_SINF	"SINF"
#define METHOD_HKEY_SSET	"SSET"
#define HKEY_NOTIFY		 0x80

/* for brightness control */
#define LCD_MAX_BRIGHTNESS 255
/* This may be magical -- beware */
#define LCD_BRIGHTNESS_INCREMENT 17
/* Registers of SINF */
#define SINF_LCD_BRIGHTNESS 4

/*******************************************************************
 *
 * definitions for /proc/ interface
 *
 *******************************************************************/
#define ACPI_PCC_DRIVER_NAME	"pcc_acpi"
#define ACPI_PCC_DEVICE_NAME	"PCCExtra"
#define ACPI_PCC_CLASS		"pcc"
#define PROC_PCC		ACPI_PCC_CLASS

#define ACPI_PCC_INPUT_PHYS	"panasonic/hkey0"

/* This is transitional definition */
#ifndef KEY_BATT
# define KEY_BATT 227
#endif

#define PROC_STR_MAX_LEN  8

#define BUS_PCC_HOTKEY BUS_I8042 /*0x1a*/ /* FIXME: BUS_I8042? */

/* Fn+F4/F5 confricts with Shift+F1/F2  */
/* This hack avoids key number confrict */
#define PCC_KEYINPUT_MODE (0)

/* LCD_TYPEs: 0 = Normal, 1 = Semi-transparent
   ENV_STATEs: Normal temp=0x01, High temp=0x81, N/A=0x00
*/
enum SINF_BITS { SINF_NUM_BATTERIES = 0,
                 SINF_LCD_TYPE,      /* 1 */
		 SINF_AC_MAX_BRIGHT, SINF_AC_MIN_BRIGHT, SINF_AC_CUR_BRIGHT,  /* 2, 3, 4 */
		             /* 4 = R1 only handle SINF_AC_CUR_BRIGHT as SINF_CUR_BRIGHT and don't know AC state */
		 SINF_DC_MAX_BRIGHT, SINF_DC_MIN_BRIGHT, SINF_DC_CUR_BRIGHT,  /* 5, 6, 7 */
		 SINF_MUTE,
		 SINF_RESERVED,      SINF_ENV_STATE, /* 9, 10 */
		 SINF_STICKY_KEY = 0x80,
};

static struct acpi_device_id pcc_device_ids[] = {
	{"MAT0012", 0},
	{"MAT0013", 0},
	{"MAT0018", 0},
	{"MAT0019", 0},
	{"",	    0},
};
MODULE_DEVICE_TABLE(acpi, pcc_device_ids);


static int __devinit acpi_pcc_hotkey_add(struct acpi_device *device);
static int __devexit acpi_pcc_hotkey_remove(struct acpi_device *device, int type);
static int acpi_pcc_hotkey_resume(struct acpi_device *device);


static struct acpi_driver acpi_pcc_driver = {
	.name =		ACPI_PCC_DRIVER_NAME,
	.class =	ACPI_PCC_CLASS,
	.ids =		pcc_device_ids,
	.ops =		{
				.add =		acpi_pcc_hotkey_add,
				.remove =	__devexit_p(acpi_pcc_hotkey_remove),
#ifdef CONFIG_PM
				/*.suspend =      acpi_pcc_hotkey_suspend,*/
				.resume =       acpi_pcc_hotkey_resume,
#endif
			},
};

struct acpi_hotkey {
	acpi_handle		handle;
	struct acpi_device	*device;
	struct proc_dir_entry   *proc_dir_entry;
	unsigned long		num_sifr;
	unsigned long		status;
	struct input_dev	*input_dev;
	int			sticky_mode;
};

struct pcc_keyinput {
	struct acpi_hotkey      *hotkey;
	int key_mode;
};

/* *************************************************************************
   Hotkey driver core
   ************************************************************************* */
/* -------------------------------------------------------------------------
                           method access functions
   ------------------------------------------------------------------------- */
static int acpi_pcc_write_sset(struct acpi_hotkey *hotkey, int func, int val)
{
	union acpi_object in_objs[] = {
		{ .integer.type  = ACPI_TYPE_INTEGER,
		  .integer.value = func, },
		{ .integer.type  = ACPI_TYPE_INTEGER,
		  .integer.value = val, },
	};
	struct acpi_object_list params = {
		.count   = ARRAY_SIZE(in_objs),
		.pointer = in_objs,
	};
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_pcc_write_sset");

	status = acpi_evaluate_object(hotkey->handle, METHOD_HKEY_SSET, &params, NULL);

	return_VALUE(status == AE_OK ? AE_OK : AE_ERROR);
}

static inline int acpi_pcc_get_sqty(struct acpi_device *device)
{
	unsigned long s;
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_pcc_get_sqty");

	status = acpi_evaluate_integer(device->handle, METHOD_HKEY_SQTY, NULL, &s);
	if (ACPI_SUCCESS(status)) {
		return_VALUE(s);
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "evaluation error HKEY.SQTY\n"));
		return_VALUE(-EINVAL);
	}
}

static int acpi_pcc_retrieve_biosdata(struct acpi_hotkey *hotkey, u32* sinf)
{
	acpi_status status;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *hkey = NULL;
	int i;

	ACPI_FUNCTION_TRACE("acpi_pcc_retrieve_biosdata");

	status = acpi_evaluate_object(hotkey->handle, METHOD_HKEY_SINF, 0 , &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "evaluation error HKEY.SINF\n"));
		status = AE_ERROR;
		return_VALUE(status);
	}

	hkey = buffer.pointer;
	if (!hkey || (hkey->type != ACPI_TYPE_PACKAGE)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid HKEY.SINF\n"));
		goto free_buffer;
	}

	if (hotkey->num_sifr < hkey->package.count) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "SQTY reports bad SINF length\n"));
		status = AE_ERROR;
		goto free_buffer;
	}

	for (i = 0; i < hkey->package.count; i++) {
		union acpi_object *element = &(hkey->package.elements[i]);
		if (likely(element->type == ACPI_TYPE_INTEGER)) {
			sinf[i] = element->integer.value;
		} else {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid HKEY.SINF data\n"));
			status = AE_ERROR;
			break;
		}
	}
	sinf[hkey->package.count] = -1;

 free_buffer:
	kfree(buffer.pointer);
	return_VALUE(status == AE_OK ? AE_OK : AE_ERROR);
}

static int acpi_pcc_read_sinf_field(struct seq_file *seq, int field)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *) seq->private;
	u32 sinf[hotkey->num_sifr + 1];

	ACPI_FUNCTION_TRACE("acpi_pcc_read_sinf_field");

	if (ACPI_SUCCESS(acpi_pcc_retrieve_biosdata(hotkey, sinf))) {
		seq_printf(seq, "%u\n",	sinf[field]);
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Couldn't retrieve BIOS data\n"));
	}

	return_VALUE(AE_OK);
}

/* -------------------------------------------------------------------------
                       user interface functions
   ------------------------------------------------------------------------- */
/* read methods */
/* Sinf read methods */
#define PCC_SINF_READ_F(_name_, FUNC) \
static int _name_  (struct seq_file *seq, void *offset) \
{ \
	return_VALUE(ACPI_SUCCESS(acpi_pcc_read_sinf_field(seq, (FUNC)))  ? 0 : -EINVAL); \
}

PCC_SINF_READ_F(acpi_pcc_numbatteries_show,	 SINF_NUM_BATTERIES);
PCC_SINF_READ_F(acpi_pcc_lcdtype_show,		 SINF_LCD_TYPE);
PCC_SINF_READ_F(acpi_pcc_ac_brightness_max_show, SINF_AC_MAX_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_ac_brightness_min_show, SINF_AC_MIN_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_ac_brightness_show,	 SINF_AC_CUR_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_dc_brightness_max_show, SINF_DC_MAX_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_dc_brightness_min_show, SINF_DC_MIN_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_dc_brightness_show,	 SINF_DC_CUR_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_brightness_show,	 SINF_AC_CUR_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_mute_show,		 SINF_MUTE);

static int acpi_pcc_sticky_key_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey *hotkey = seq->private;

	ACPI_FUNCTION_TRACE("acpi_pcc_sticky_key_show");

	if (!hotkey || !hotkey->device) {
		return_VALUE(-EINVAL);
	}

	seq_printf(seq, "%d\n", hotkey->sticky_mode);

	return_VALUE(0);
}

static int acpi_pcc_keyinput_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey 	*hotkey = (struct acpi_hotkey *) seq->private;
	struct input_dev 	*hotk_input_dev = hotkey->input_dev;
	struct pcc_keyinput 	*keyinput = input_get_drvdata(hotk_input_dev);

	ACPI_FUNCTION_TRACE("acpi_pcc_keyinput_show");

	seq_printf(seq, "%d\n", keyinput->key_mode);

	return_VALUE(0);
}

static int acpi_pcc_version_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *) seq->private;

	ACPI_FUNCTION_TRACE("acpi_pcc_version_show");

	if (!hotkey || !hotkey->device) {
		return_VALUE(-EINVAL);
	}

	seq_printf(seq, "%s version %s\n", ACPI_PCC_DRIVER_NAME, ACPI_PCC_VERSION);
	seq_printf(seq, "%li functions\n", hotkey->num_sifr);

	return_VALUE(0);
}

/* write methods */
static ssize_t acpi_pcc_write_single_flag (struct file *file,
					   const char __user *buffer,
					   size_t count,
					   int sinf_func)
{
	struct seq_file		*seq = file->private_data;
	struct acpi_hotkey	*hotkey = seq->private;
	char			write_string[PROC_STR_MAX_LEN];
	u32			val;

	ACPI_FUNCTION_TRACE("acpi_pcc_write_single_flag");

	if (!hotkey || (count > sizeof(write_string) - 1)) {
		return_VALUE(-EINVAL);
	}

	if (copy_from_user(write_string, buffer, count)) {
		return_VALUE(-EFAULT);
	}
	write_string[count] = '\0';

	if ((sscanf(write_string, "%3i", &val) == 1) &&
	    (val == 0 || val == 1)) {
		acpi_pcc_write_sset(hotkey, sinf_func, val);
	}

	return_VALUE(count);
}

static unsigned long acpi_pcc_write_brightness(struct file *file, const char __user *buffer,
					       size_t count,
					       int min_index, int max_index,
					       int cur_index)
{
	struct seq_file		*seq = (struct seq_file *)file->private_data;
	struct acpi_hotkey	*hotkey = (struct acpi_hotkey *)seq->private;
	char			write_string[PROC_STR_MAX_LEN];
	u32 bright;
	u32 sinf[hotkey->num_sifr + 1];

	ACPI_FUNCTION_TRACE("acpi_pcc_write_brightness");

	if (!hotkey || (count > sizeof(write_string) - 1)) {
		return_VALUE(-EINVAL);
	}

	if (copy_from_user(write_string, buffer, count)) {
		return_VALUE(-EFAULT);
	}

	write_string[count] = '\0';

	if (ACPI_FAILURE(acpi_pcc_retrieve_biosdata(hotkey, sinf))) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Couldn't retrieve BIOS data\n"));
		goto end;
	}

	if ((sscanf(write_string, "%4i", &bright) == 1) &&
	    (bright >= sinf[min_index]                ) &&
	    (bright <= sinf[max_index]                )) {
		acpi_pcc_write_sset(hotkey, cur_index, bright);
	}

end:
	return_VALUE(count);
}

static ssize_t acpi_pcc_write_ac_brightness(struct file *file, const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	return_VALUE(acpi_pcc_write_brightness(file, buffer, count,
					       SINF_AC_MIN_BRIGHT,
					       SINF_AC_MAX_BRIGHT,
					       SINF_AC_CUR_BRIGHT));
}

static ssize_t acpi_pcc_write_dc_brightness(struct file *file, const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	return_VALUE(acpi_pcc_write_brightness(file, buffer, count,
					       SINF_DC_MIN_BRIGHT,
					       SINF_DC_MAX_BRIGHT,
					       SINF_DC_CUR_BRIGHT));
}

static ssize_t acpi_pcc_write_no_brightness(struct file *file, const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	return acpi_pcc_write_brightness(file, buffer, count, SINF_AC_MIN_BRIGHT,
					 SINF_AC_MAX_BRIGHT,
					 SINF_AC_CUR_BRIGHT);
}

static ssize_t acpi_pcc_write_mute (struct file *file,
				    const char __user *buffer,
				    size_t count, loff_t *ppos)
{
	return_VALUE(acpi_pcc_write_single_flag(file, buffer, count, SINF_MUTE));
}

static ssize_t acpi_pcc_write_sticky_key (struct file *file,
					  const char __user *buffer,
					  size_t count, loff_t *ppos)
{
	struct seq_file     *seq = (struct seq_file *)file->private_data;
	struct acpi_hotkey  *hotkey = (struct acpi_hotkey *)seq->private;
	char                 write_string[PROC_STR_MAX_LEN];
	int                  mode;

	ACPI_FUNCTION_TRACE("acpi_pcc_write_sticky_key");

	if (!hotkey || (count > sizeof(write_string) - 1)) {
		return_VALUE(-EINVAL);
	}

	if (copy_from_user(write_string, buffer, count)) {
		return_VALUE(-EFAULT);
	}
	write_string[count] = '\0';

	if ((sscanf(write_string, "%3i", &mode) == 1) &&
	    (mode == 0 || mode == 1)) {
		acpi_pcc_write_sset(hotkey, SINF_STICKY_KEY, mode);
		hotkey->sticky_mode = mode;
	}

	return_VALUE(count);
}

static ssize_t acpi_pcc_write_keyinput(struct file *file, const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	struct seq_file		*seq = (struct seq_file *)file->private_data;
	struct acpi_hotkey	*hotkey = (struct acpi_hotkey *)seq->private;
	struct pcc_keyinput 	*keyinput;
	char			write_string[PROC_STR_MAX_LEN];
	int			key_mode;

	ACPI_FUNCTION_TRACE("acpi_pcc_write_keyinput");

	if (!hotkey || (count > (sizeof(write_string) - 1))) {
		return_VALUE(-EINVAL);
	}

	if (copy_from_user(write_string, buffer, count)) {
		return_VALUE(-EFAULT);
	}

	write_string[count] = '\0';

	if ((sscanf(write_string, "%4i", &key_mode) == 1) &&
	    (key_mode == 0 || key_mode == 1)) {
		keyinput = input_get_drvdata(hotkey->input_dev);
		keyinput->key_mode = key_mode;
	}

	return_VALUE(count);
}

/* -------------------------------------------------------------------------
                            hotkey driver
   ------------------------------------------------------------------------- */
static void acpi_pcc_generete_keyinput(struct acpi_hotkey *hotkey)
{
	struct input_dev    *hotk_input_dev = hotkey->input_dev;
	struct pcc_keyinput *keyinput = input_get_drvdata(hotk_input_dev);
	int hinf = hotkey->status;
	int key_code, hkey_num;
	const int key_map[] = {
		/*  0 */ -1,
		/*  1 */ KEY_BRIGHTNESSDOWN,
		/*  2 */ KEY_BRIGHTNESSUP,
		/*  3 */ -1, /* vga/lcd switch event is not occur on hotkey driver. */
		/*  4 */ KEY_MUTE,
		/*  5 */ KEY_VOLUMEDOWN,
		/*  6 */ KEY_VOLUMEUP,
		/*  7 */ KEY_SLEEP,
		/*  8 */ -1, /* Change CPU boost: do nothing */
		/*  9 */ KEY_BATT,
		/* 10 */ KEY_SUSPEND,
	};

	ACPI_FUNCTION_TRACE("acpi_pcc_generete_keyinput");

	if (keyinput->key_mode == 0) { return_VOID; }

	hkey_num = hinf & 0xf;

	if ((       0 > hkey_num	   ) ||
	    (hkey_num > ARRAY_SIZE(key_map))) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "hotkey number out of range: %d\n",
				  hkey_num));
		return_VOID;
	}

	key_code = key_map[hkey_num];

	if (key_code != -1) {
		int pushed = (hinf & 0x80) ? TRUE : FALSE;

		input_report_key(hotk_input_dev, key_code, pushed);
		input_sync(hotk_input_dev);
	}
}

static int acpi_pcc_hotkey_get_key(struct acpi_hotkey *hotkey)
{
	unsigned long result;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_pcc_hotkey_get_key");

	status = acpi_evaluate_integer(hotkey->handle, METHOD_HKEY_QUERY, NULL, &result);
	if (likely(ACPI_SUCCESS(status))) {
		hotkey->status = result;
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "error getting hotkey status\n"));
	}

	return_VALUE(status == AE_OK);
}

void acpi_pcc_hotkey_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *) data;

	ACPI_FUNCTION_TRACE("acpi_pcc_hotkey_notify");

	switch(event) {
	case HKEY_NOTIFY:
		if (acpi_pcc_hotkey_get_key(hotkey)) {
			/* generate event like '"pcc HKEY 00000080 00000084"' when Fn+F4 pressed */
			acpi_bus_generate_proc_event(hotkey->device, event, hotkey->status);
		}
		acpi_pcc_generete_keyinput(hotkey);
		break;
	default:
		/* nothing to do */
		break;
	}
	return_VOID;
}

/* *************************************************************************
   FS Interface (/proc)
   ************************************************************************* */
/* oepn proc file fs*/
SEQ_OPEN_FS(acpi_pcc_dc_brightness_open_fs,	acpi_pcc_dc_brightness_show);
SEQ_OPEN_FS(acpi_pcc_numbatteries_open_fs,	acpi_pcc_numbatteries_show);
SEQ_OPEN_FS(acpi_pcc_lcdtype_open_fs,		acpi_pcc_lcdtype_show);
SEQ_OPEN_FS(acpi_pcc_ac_brightness_max_open_fs, acpi_pcc_ac_brightness_max_show);
SEQ_OPEN_FS(acpi_pcc_ac_brightness_min_open_fs, acpi_pcc_ac_brightness_min_show);
SEQ_OPEN_FS(acpi_pcc_ac_brightness_open_fs,	acpi_pcc_ac_brightness_show);
SEQ_OPEN_FS(acpi_pcc_dc_brightness_max_open_fs, acpi_pcc_dc_brightness_max_show);
SEQ_OPEN_FS(acpi_pcc_dc_brightness_min_open_fs, acpi_pcc_dc_brightness_min_show);
SEQ_OPEN_FS(acpi_pcc_brightness_open_fs,	acpi_pcc_brightness_show);
SEQ_OPEN_FS(acpi_pcc_mute_open_fs,		acpi_pcc_mute_show);
SEQ_OPEN_FS(acpi_pcc_version_open_fs,		acpi_pcc_version_show);
SEQ_OPEN_FS(acpi_pcc_keyinput_open_fs,		acpi_pcc_keyinput_show);
SEQ_OPEN_FS(acpi_pcc_sticky_key_open_fs,	acpi_pcc_sticky_key_show);

typedef struct file_operations fops_t;
static fops_t acpi_pcc_numbatteries_fops      = SEQ_FILEOPS_R (acpi_pcc_numbatteries_open_fs);
static fops_t acpi_pcc_lcdtype_fops           = SEQ_FILEOPS_R (acpi_pcc_lcdtype_open_fs);
static fops_t acpi_pcc_mute_fops              = SEQ_FILEOPS_RW(acpi_pcc_mute_open_fs, acpi_pcc_write_mute);
static fops_t acpi_pcc_ac_brightness_fops     = SEQ_FILEOPS_RW(acpi_pcc_ac_brightness_open_fs, acpi_pcc_write_ac_brightness);
static fops_t acpi_pcc_ac_brightness_max_fops = SEQ_FILEOPS_R (acpi_pcc_ac_brightness_max_open_fs);
static fops_t acpi_pcc_ac_brightness_min_fops = SEQ_FILEOPS_R (acpi_pcc_ac_brightness_min_open_fs);
static fops_t acpi_pcc_dc_brightness_fops     = SEQ_FILEOPS_RW(acpi_pcc_dc_brightness_open_fs, acpi_pcc_write_dc_brightness);
static fops_t acpi_pcc_dc_brightness_max_fops = SEQ_FILEOPS_R (acpi_pcc_dc_brightness_max_open_fs);
static fops_t acpi_pcc_dc_brightness_min_fops = SEQ_FILEOPS_R (acpi_pcc_dc_brightness_min_open_fs);
static fops_t acpi_pcc_brightness_fops        = SEQ_FILEOPS_RW(acpi_pcc_brightness_open_fs, acpi_pcc_write_no_brightness);
static fops_t acpi_pcc_sticky_key_fops        = SEQ_FILEOPS_RW(acpi_pcc_sticky_key_open_fs, acpi_pcc_write_sticky_key);
static fops_t acpi_pcc_keyinput_fops          = SEQ_FILEOPS_RW(acpi_pcc_keyinput_open_fs, acpi_pcc_write_keyinput);
static fops_t acpi_pcc_version_fops           = SEQ_FILEOPS_R (acpi_pcc_version_open_fs);

typedef struct _ProcItem
{
	const char* name;
	struct file_operations *fops;
	mode_t flag;
} ProcItem;

/* Note: These functions map *exactly* to the SINF/SSET functions */
ProcItem acpi_pcc_proc_items_sifr[] = {
	{ "num_batteries",	&acpi_pcc_numbatteries_fops,	 S_IRUGO },
	{ "lcd_type",		&acpi_pcc_lcdtype_fops,		 S_IRUGO },
	{ "ac_brightness_max" , &acpi_pcc_ac_brightness_max_fops,S_IRUGO },
	{ "ac_brightness_min" , &acpi_pcc_ac_brightness_min_fops,S_IRUGO },
	{ "ac_brightness" ,	&acpi_pcc_ac_brightness_fops,	 S_IFREG | S_IRUGO | S_IWUSR },
	{ "dc_brightness_max" , &acpi_pcc_dc_brightness_max_fops,S_IRUGO },
	{ "dc_brightness_min" , &acpi_pcc_dc_brightness_min_fops,S_IRUGO },
	{ "dc_brightness" ,	&acpi_pcc_dc_brightness_fops,	 S_IFREG | S_IRUGO | S_IWUSR },
	{ "brightness" ,	&acpi_pcc_brightness_fops,    S_IFREG | S_IRUGO | S_IWUSR },
	{ "mute",		&acpi_pcc_mute_fops,		 S_IFREG | S_IRUGO | S_IWUSR },
	{ NULL, NULL, 0 },
};

ProcItem acpi_pcc_proc_items[] = {
	{ "sticky_key",		&acpi_pcc_sticky_key_fops,	 S_IFREG | S_IRUGO | S_IWUSR },
	{ "keyinput",		&acpi_pcc_keyinput_fops,	 S_IFREG | S_IRUGO | S_IWUSR },
	{ "version",		&acpi_pcc_version_fops,		 S_IRUGO },
	{ NULL, NULL, 0 },
};

static int __devinit acpi_pcc_add_device(struct acpi_device *device,
					 ProcItem *proc_items,
					 int num)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey*)acpi_driver_data(device);
	struct proc_dir_entry* proc;
	ProcItem* item;
	int i;

	for (item = proc_items, i = 0; item->name && i < num; ++item, ++i) {
		proc = create_proc_entry(item->name, item->flag, hotkey->proc_dir_entry);
		if (likely(proc)) {
			proc->proc_fops = item->fops;
			proc->data = hotkey;
			proc->owner = THIS_MODULE;
		} else {
			while (i-- > 0) {
				item--;
				remove_proc_entry(item->name, hotkey->proc_dir_entry);
			}
			return_VALUE(-ENODEV);
		}
	}
	return_VALUE(0);
}

static int __devinit acpi_pcc_proc_init(struct acpi_device *device)
{
	struct proc_dir_entry *acpi_pcc_dir;
	struct acpi_hotkey    *hotkey = (struct acpi_hotkey*)acpi_driver_data(device);
	acpi_status status;

	ACPI_FUNCTION_TRACE("acpi_pcc_proc_init");

	acpi_pcc_dir = proc_mkdir(PROC_PCC, acpi_root_dir);

	if (unlikely(!acpi_pcc_dir)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Couldn't create dir in /proc\n"));
		return_VALUE(-ENODEV);
	}

	acpi_pcc_dir->owner = THIS_MODULE;
	hotkey->proc_dir_entry = acpi_pcc_dir;

	status =  acpi_pcc_add_device(device, acpi_pcc_proc_items_sifr, hotkey->num_sifr);
	status |= acpi_pcc_add_device(device, acpi_pcc_proc_items,      ARRAY_SIZE(acpi_pcc_proc_items));
	if (unlikely(status)) {
		remove_proc_entry(PROC_PCC, acpi_root_dir);
		hotkey->proc_dir_entry = NULL;
		return_VALUE(-ENODEV);
	}

	return_VALUE(status);
}

static void __devexit acpi_pcc_remove_device(struct acpi_device *device,
					     ProcItem *proc_items,
					     int num)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey*)acpi_driver_data(device);
	ProcItem* item;
	int i;

	for (item = proc_items, i = 0; item->name != NULL && i < num; ++item, ++i) {
		remove_proc_entry(item->name, hotkey->proc_dir_entry);
	}

	return_VOID;
}

/* *************************************************************************
   Power Management
   ************************************************************************* */
#ifdef CONFIG_PM
static int acpi_pcc_hotkey_resume(struct acpi_device *device)
{
	struct acpi_hotkey *hotkey = acpi_driver_data(device);
	acpi_status	    status = AE_OK;

	ACPI_FUNCTION_TRACE("acpi_pcc_hotkey_resume");

	if (device == NULL || hotkey == NULL) { return_VALUE(-EINVAL); }

	if (hotkey->num_sifr != 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Sticky mode restore: %d\n", hotkey->sticky_mode));

		status = acpi_pcc_write_sset(hotkey, SINF_STICKY_KEY, hotkey->sticky_mode);
	}
	if (status != AE_OK) { return_VALUE(-EINVAL); }

	return_VALUE(0);
}
#endif

/* *************************************************************************
   Module init/remove
   ************************************************************************* */
/* -------------------------------------------------------------------------
   input
   ------------------------------------------------------------------------- */
static int __devinit acpi_pcc_init_input(struct acpi_hotkey *hotkey)
{
	struct input_dev    *hotk_input_dev;
	struct pcc_keyinput *pcc_keyinput;
	int error;

	ACPI_FUNCTION_TRACE("acpi_pcc_init_input");

	hotk_input_dev = input_allocate_device();
	if (hotk_input_dev == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Couldn't allocate input device for hotkey"));
		goto err_input;
	}

	pcc_keyinput = kcalloc(1, sizeof(struct pcc_keyinput), GFP_KERNEL);

	if (pcc_keyinput == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Couldn't allocate mem for private data"));
		goto err_pcc;
	}

	hotk_input_dev->evbit[0] = BIT(EV_KEY);

	set_bit(KEY_BRIGHTNESSDOWN, hotk_input_dev->keybit);
	set_bit(KEY_BRIGHTNESSUP,   hotk_input_dev->keybit);
	set_bit(KEY_MUTE,	    hotk_input_dev->keybit);
	set_bit(KEY_VOLUMEDOWN,	    hotk_input_dev->keybit);
	set_bit(KEY_VOLUMEUP,	    hotk_input_dev->keybit);
	set_bit(KEY_SLEEP,	    hotk_input_dev->keybit);
	set_bit(KEY_BATT,	    hotk_input_dev->keybit);
	set_bit(KEY_SUSPEND,	    hotk_input_dev->keybit);

	hotk_input_dev->name       = ACPI_PCC_DRIVER_NAME;
	hotk_input_dev->phys       = ACPI_PCC_INPUT_PHYS;
	hotk_input_dev->id.bustype = BUS_PCC_HOTKEY;
	hotk_input_dev->id.vendor  = 0x0001;
	hotk_input_dev->id.product = 0x0001;
	hotk_input_dev->id.version = 0x0100;

	pcc_keyinput->key_mode = PCC_KEYINPUT_MODE;
	pcc_keyinput->hotkey   = hotkey;

	input_set_drvdata(hotk_input_dev, pcc_keyinput);

	hotkey->input_dev = hotk_input_dev;

	error = input_register_device(hotk_input_dev);

	if (error) {
		goto err_pcc;
	}

	return_VALUE(0);

 err_pcc:
	input_unregister_device(hotk_input_dev);
 err_input:
	return_VALUE(-ENOMEM);
}

static void __devexit acpi_pcc_remove_input(struct acpi_hotkey *hotkey)
{
	struct input_dev    *hotk_input_dev;
	struct pcc_keyinput *pcc_keyinput;

	ACPI_FUNCTION_TRACE("acpi_pcc_remove_input");

	if (hotkey == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Can't free memory"));
		return_VOID;
	}

	hotk_input_dev = hotkey->input_dev;
	pcc_keyinput   = input_get_drvdata(hotk_input_dev);

	input_unregister_device(hotk_input_dev);

	kfree(pcc_keyinput);
}

/* -------------------------------------------------------------------------
   ACPI
   ------------------------------------------------------------------------- */
static int __devinit acpi_pcc_hotkey_add (struct acpi_device *device)
{
	acpi_status		status = AE_OK;
	struct acpi_hotkey	*hotkey = NULL;
	int sifr_status, num_sifr, result;

	ACPI_FUNCTION_TRACE("acpi_pcc_hotkey_add");

	if (device == NULL) {
		return_VALUE(-EINVAL);
	}

	sifr_status = acpi_pcc_get_sqty(device);

	if (sifr_status > 255) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "num_sifr too large"));
		return_VALUE(-ENODEV);
	}

	if (sifr_status < 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "not support SQTY"));
		num_sifr = 0;
	} else {
		num_sifr = sifr_status;
	}

	hotkey = kcalloc(1, sizeof(struct acpi_hotkey), GFP_KERNEL);
	if (hotkey == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Couldn't allocate mem for hotkey"));
		return_VALUE(-ENOMEM);
	}

	hotkey->device   = device;
	hotkey->handle   = device->handle;
	hotkey->num_sifr = num_sifr;
	acpi_driver_data(device) = hotkey;
	strcpy(acpi_device_name(device),  ACPI_PCC_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PCC_CLASS);

	status = acpi_install_notify_handler(hotkey->handle,
					     ACPI_DEVICE_NOTIFY,
					     acpi_pcc_hotkey_notify,
					     hotkey);

	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error installing notify handler\n"));
		kfree(hotkey);
		return_VALUE(-ENODEV);
	}

	result = acpi_pcc_init_input(hotkey);
	if (result != 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error installing keyinput handler\n"));
		kfree(hotkey);
		return_VALUE(result);
	}

	return_VALUE(acpi_pcc_proc_init(device));
}

static int __devexit acpi_pcc_hotkey_remove(struct acpi_device *device, int type)
{
	acpi_status		status = AE_OK;
	struct acpi_hotkey	*hotkey = acpi_driver_data(device);

	ACPI_FUNCTION_TRACE("acpi_pcc_hotkey_remove");

	if (!device || !hotkey) {
		return_VALUE(-EINVAL);
	}

	if (hotkey->proc_dir_entry) {
		acpi_pcc_remove_device(device, acpi_pcc_proc_items_sifr, hotkey->num_sifr);
		acpi_pcc_remove_device(device, acpi_pcc_proc_items,      ARRAY_SIZE(acpi_pcc_proc_items));
		remove_proc_entry(PROC_PCC, acpi_root_dir);
	}

	status = acpi_remove_notify_handler(hotkey->handle,
		    ACPI_DEVICE_NOTIFY, acpi_pcc_hotkey_notify);

	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error removing notify handler\n"));
	}

	acpi_pcc_remove_input(hotkey);
	if (hotkey != NULL) {
		kfree(hotkey);
	}
	return_VALUE(status == AE_OK);
}

/* *********************************************************************
   Module entry point
   ********************************************************************* */
static int __init acpi_pcc_init(void)
{
	int result;

	ACPI_FUNCTION_TRACE("acpi_pcc_init");

	printk(KERN_INFO LOGPREFIX "loading...\n");

	if (acpi_disabled) {
		printk(KERN_INFO LOGPREFIX "ACPI disabled.\n");
		return_VALUE(-ENODEV);
	}

	result = acpi_bus_register_driver(&acpi_pcc_driver);
	if (result < 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error registering hotkey driver\n"));
		return_VALUE(-ENODEV);
	}

	return_VALUE(result);
}

static void __exit acpi_pcc_exit(void)
{
	ACPI_FUNCTION_TRACE("acpi_pcc_exit");

	printk(KERN_INFO LOGPREFIX "unloading...\n");

	acpi_bus_unregister_driver(&acpi_pcc_driver);

	return_VOID;
}

module_init(acpi_pcc_init);
module_exit(acpi_pcc_exit);
