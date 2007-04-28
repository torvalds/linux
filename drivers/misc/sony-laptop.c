/*
 * ACPI Sony Notebook Control Driver (SNC and SPIC)
 *
 * Copyright (C) 2004-2005 Stelian Pop <stelian@popies.net>
 * Copyright (C) 2007 Mattia Dongili <malattia@linux.it>
 *
 * Parts of this driver inspired from asus_acpi.c and ibm_acpi.c
 * which are copyrighted by their respective authors.
 *
 * The SNY6001 driver part is based on the sonypi driver which includes
 * material from:
 *
 * Copyright (C) 2001-2005 Stelian Pop <stelian@popies.net>
 *
 * Copyright (C) 2005 Narayanan R S <nars@kadamba.org>
 *
 * Copyright (C) 2001-2002 Alcôve <www.alcove.com>
 *
 * Copyright (C) 2001 Michael Ashley <m.ashley@unsw.edu.au>
 *
 * Copyright (C) 2001 Junichi Morita <jun1m@mars.dti.ne.jp>
 *
 * Copyright (C) 2000 Takaya Kinjo <t-kinjo@tc4.so-net.ne.jp>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include <linux/acpi.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <asm/uaccess.h>
#include <linux/sonypi.h>
#include <linux/sony-laptop.h>
#ifdef CONFIG_SONY_LAPTOP_OLD
#include <linux/poll.h>
#include <linux/miscdevice.h>
#endif

#define DRV_PFX			"sony-laptop: "
#define dprintk(msg...)		do {			\
	if (debug) printk(KERN_WARNING DRV_PFX  msg);	\
} while (0)

#define SONY_LAPTOP_DRIVER_VERSION	"0.5"

#define SONY_NC_CLASS		"sony-nc"
#define SONY_NC_HID		"SNY5001"
#define SONY_NC_DRIVER_NAME	"Sony Notebook Control Driver"

#define SONY_PIC_CLASS		"sony-pic"
#define SONY_PIC_HID		"SNY6001"
#define SONY_PIC_DRIVER_NAME	"Sony Programmable IO Control Driver"

MODULE_AUTHOR("Stelian Pop, Mattia Dongili");
MODULE_DESCRIPTION("Sony laptop extras driver (SPIC and SNC ACPI device)");
MODULE_LICENSE("GPL");
MODULE_VERSION(SONY_LAPTOP_DRIVER_VERSION);

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "set this to 1 (and RTFM) if you want to help "
		 "the development of this driver");

static int no_spic;		/* = 0 */
module_param(no_spic, int, 0444);
MODULE_PARM_DESC(no_spic,
		 "set this if you don't want to enable the SPIC device");

static int compat;		/* = 0 */
module_param(compat, int, 0444);
MODULE_PARM_DESC(compat,
		 "set this if you want to enable backward compatibility mode");

static unsigned long mask = 0xffffffff;
module_param(mask, ulong, 0644);
MODULE_PARM_DESC(mask,
		 "set this to the mask of event you want to enable (see doc)");

static int camera;		/* = 0 */
module_param(camera, int, 0444);
MODULE_PARM_DESC(camera,
		 "set this to 1 to enable Motion Eye camera controls "
		 "(only use it if you have a C1VE or C1VN model)");

#ifdef CONFIG_SONY_LAPTOP_OLD
static int minor = -1;
module_param(minor, int, 0);
MODULE_PARM_DESC(minor,
		 "minor number of the misc device for the SPIC compatibility code, "
		 "default is -1 (automatic)");
#endif

/*********** Input Devices ***********/

#define SONY_LAPTOP_BUF_SIZE	128
struct sony_laptop_input_s {
	atomic_t		users;
	struct input_dev	*jog_dev;
	struct input_dev	*key_dev;
	struct kfifo		*fifo;
	spinlock_t		fifo_lock;
	struct workqueue_struct	*wq;
};
static struct sony_laptop_input_s sony_laptop_input = {
	.users = ATOMIC_INIT(0),
};

struct sony_laptop_keypress {
	struct input_dev *dev;
	int key;
};

/* Correspondance table between sonypi events and input layer events */
static struct {
	int sonypiev;
	int inputev;
} sony_laptop_inputkeys[] = {
	{ SONYPI_EVENT_CAPTURE_PRESSED,	 	KEY_CAMERA },
	{ SONYPI_EVENT_FNKEY_ONLY, 		KEY_FN },
	{ SONYPI_EVENT_FNKEY_ESC, 		KEY_FN_ESC },
	{ SONYPI_EVENT_FNKEY_F1, 		KEY_FN_F1 },
	{ SONYPI_EVENT_FNKEY_F2, 		KEY_FN_F2 },
	{ SONYPI_EVENT_FNKEY_F3, 		KEY_FN_F3 },
	{ SONYPI_EVENT_FNKEY_F4, 		KEY_FN_F4 },
	{ SONYPI_EVENT_FNKEY_F5, 		KEY_FN_F5 },
	{ SONYPI_EVENT_FNKEY_F6, 		KEY_FN_F6 },
	{ SONYPI_EVENT_FNKEY_F7, 		KEY_FN_F7 },
	{ SONYPI_EVENT_FNKEY_F8, 		KEY_FN_F8 },
	{ SONYPI_EVENT_FNKEY_F9,		KEY_FN_F9 },
	{ SONYPI_EVENT_FNKEY_F10,		KEY_FN_F10 },
	{ SONYPI_EVENT_FNKEY_F11, 		KEY_FN_F11 },
	{ SONYPI_EVENT_FNKEY_F12,		KEY_FN_F12 },
	{ SONYPI_EVENT_FNKEY_1, 		KEY_FN_1 },
	{ SONYPI_EVENT_FNKEY_2, 		KEY_FN_2 },
	{ SONYPI_EVENT_FNKEY_D,			KEY_FN_D },
	{ SONYPI_EVENT_FNKEY_E,			KEY_FN_E },
	{ SONYPI_EVENT_FNKEY_F,			KEY_FN_F },
	{ SONYPI_EVENT_FNKEY_S,			KEY_FN_S },
	{ SONYPI_EVENT_FNKEY_B,			KEY_FN_B },
	{ SONYPI_EVENT_BLUETOOTH_PRESSED, 	KEY_BLUE },
	{ SONYPI_EVENT_BLUETOOTH_ON, 		KEY_BLUE },
	{ SONYPI_EVENT_PKEY_P1, 		KEY_PROG1 },
	{ SONYPI_EVENT_PKEY_P2, 		KEY_PROG2 },
	{ SONYPI_EVENT_PKEY_P3, 		KEY_PROG3 },
	{ SONYPI_EVENT_BACK_PRESSED, 		KEY_BACK },
	{ SONYPI_EVENT_HELP_PRESSED, 		KEY_HELP },
	{ SONYPI_EVENT_ZOOM_PRESSED, 		KEY_ZOOM },
	{ SONYPI_EVENT_THUMBPHRASE_PRESSED, 	BTN_THUMB },
	{ 0, 0 },
};

/* release buttons after a short delay if pressed */
static void do_sony_laptop_release_key(struct work_struct *work)
{
	struct sony_laptop_keypress kp;

	while (kfifo_get(sony_laptop_input.fifo, (unsigned char *)&kp,
			 sizeof(kp)) == sizeof(kp)) {
		msleep(10);
		input_report_key(kp.dev, kp.key, 0);
		input_sync(kp.dev);
	}
}
static DECLARE_WORK(sony_laptop_release_key_work,
		do_sony_laptop_release_key);

/* forward event to the input subsytem */
static void sony_laptop_report_input_event(u8 event)
{
	struct input_dev *jog_dev = sony_laptop_input.jog_dev;
	struct input_dev *key_dev = sony_laptop_input.key_dev;
	struct sony_laptop_keypress kp = { NULL };
	int i;

	if (event == SONYPI_EVENT_FNKEY_RELEASED) {
		/* Nothing, not all VAIOs generate this event */
		return;
	}

	/* report events */
	switch (event) {
	/* jog_dev events */
	case SONYPI_EVENT_JOGDIAL_UP:
	case SONYPI_EVENT_JOGDIAL_UP_PRESSED:
		input_report_rel(jog_dev, REL_WHEEL, 1);
		input_sync(jog_dev);
		return;

	case SONYPI_EVENT_JOGDIAL_DOWN:
	case SONYPI_EVENT_JOGDIAL_DOWN_PRESSED:
		input_report_rel(jog_dev, REL_WHEEL, -1);
		input_sync(jog_dev);
		return;

	/* key_dev events */
	case SONYPI_EVENT_JOGDIAL_PRESSED:
		kp.key = BTN_MIDDLE;
		kp.dev = jog_dev;
		break;

	default:
		for (i = 0; sony_laptop_inputkeys[i].sonypiev; i++)
			if (event == sony_laptop_inputkeys[i].sonypiev) {
				kp.dev = key_dev;
				kp.key = sony_laptop_inputkeys[i].inputev;
				break;
			}
		break;
	}

	if (kp.dev) {
		input_report_key(kp.dev, kp.key, 1);
		input_sync(kp.dev);
		kfifo_put(sony_laptop_input.fifo,
			  (unsigned char *)&kp, sizeof(kp));

		if (!work_pending(&sony_laptop_release_key_work))
			queue_work(sony_laptop_input.wq,
					&sony_laptop_release_key_work);
	} else
		dprintk("unknown input event %.2x\n", event);
}

static int sony_laptop_setup_input(void)
{
	struct input_dev *jog_dev;
	struct input_dev *key_dev;
	int i;
	int error;

	/* don't run again if already initialized */
	if (atomic_add_return(1, &sony_laptop_input.users) > 1)
		return 0;

	/* kfifo */
	spin_lock_init(&sony_laptop_input.fifo_lock);
	sony_laptop_input.fifo =
		kfifo_alloc(SONY_LAPTOP_BUF_SIZE, GFP_KERNEL,
			    &sony_laptop_input.fifo_lock);
	if (IS_ERR(sony_laptop_input.fifo)) {
		printk(KERN_ERR DRV_PFX "kfifo_alloc failed\n");
		error = PTR_ERR(sony_laptop_input.fifo);
		goto err_dec_users;
	}

	/* init workqueue */
	sony_laptop_input.wq = create_singlethread_workqueue("sony-laptop");
	if (!sony_laptop_input.wq) {
		printk(KERN_ERR DRV_PFX
				"Unabe to create workqueue.\n");
		error = -ENXIO;
		goto err_free_kfifo;
	}

	/* input keys */
	key_dev = input_allocate_device();
	if (!key_dev) {
		error = -ENOMEM;
		goto err_destroy_wq;
	}

	key_dev->name = "Sony Vaio Keys";
	key_dev->id.bustype = BUS_ISA;
	key_dev->id.vendor = PCI_VENDOR_ID_SONY;

	/* Initialize the Input Drivers: special keys */
	key_dev->evbit[0] = BIT(EV_KEY);
	for (i = 0; sony_laptop_inputkeys[i].sonypiev; i++)
		if (sony_laptop_inputkeys[i].inputev)
			set_bit(sony_laptop_inputkeys[i].inputev,
					key_dev->keybit);

	error = input_register_device(key_dev);
	if (error)
		goto err_free_keydev;

	sony_laptop_input.key_dev = key_dev;

	/* jogdial */
	jog_dev = input_allocate_device();
	if (!jog_dev) {
		error = -ENOMEM;
		goto err_unregister_keydev;
	}

	jog_dev->name = "Sony Vaio Jogdial";
	jog_dev->id.bustype = BUS_ISA;
	jog_dev->id.vendor = PCI_VENDOR_ID_SONY;

	jog_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	jog_dev->keybit[LONG(BTN_MOUSE)] = BIT(BTN_MIDDLE);
	jog_dev->relbit[0] = BIT(REL_WHEEL);

	error = input_register_device(jog_dev);
	if (error)
		goto err_free_jogdev;

	sony_laptop_input.jog_dev = jog_dev;

	return 0;

err_free_jogdev:
	input_free_device(jog_dev);

err_unregister_keydev:
	input_unregister_device(key_dev);
	/* to avoid kref underflow below at input_free_device */
	key_dev = NULL;

err_free_keydev:
	input_free_device(key_dev);

err_destroy_wq:
	destroy_workqueue(sony_laptop_input.wq);

err_free_kfifo:
	kfifo_free(sony_laptop_input.fifo);

err_dec_users:
	atomic_dec(&sony_laptop_input.users);
	return error;
}

static void sony_laptop_remove_input(void)
{
	/* cleanup only after the last user has gone */
	if (!atomic_dec_and_test(&sony_laptop_input.users))
		return;

	/* flush workqueue first */
	flush_workqueue(sony_laptop_input.wq);

	/* destroy input devs */
	input_unregister_device(sony_laptop_input.key_dev);
	sony_laptop_input.key_dev = NULL;

	if (sony_laptop_input.jog_dev) {
		input_unregister_device(sony_laptop_input.jog_dev);
		sony_laptop_input.jog_dev = NULL;
	}

	destroy_workqueue(sony_laptop_input.wq);
	kfifo_free(sony_laptop_input.fifo);
}

/*********** Platform Device ***********/

static atomic_t sony_pf_users = ATOMIC_INIT(0);
static struct platform_driver sony_pf_driver = {
	.driver = {
		   .name = "sony-laptop",
		   .owner = THIS_MODULE,
		   }
};
static struct platform_device *sony_pf_device;

static int sony_pf_add(void)
{
	int ret = 0;

	/* don't run again if already initialized */
	if (atomic_add_return(1, &sony_pf_users) > 1)
		return 0;

	ret = platform_driver_register(&sony_pf_driver);
	if (ret)
		goto out;

	sony_pf_device = platform_device_alloc("sony-laptop", -1);
	if (!sony_pf_device) {
		ret = -ENOMEM;
		goto out_platform_registered;
	}

	ret = platform_device_add(sony_pf_device);
	if (ret)
		goto out_platform_alloced;

	return 0;

      out_platform_alloced:
	platform_device_put(sony_pf_device);
	sony_pf_device = NULL;
      out_platform_registered:
	platform_driver_unregister(&sony_pf_driver);
      out:
	atomic_dec(&sony_pf_users);
	return ret;
}

static void sony_pf_remove(void)
{
	/* deregister only after the last user has gone */
	if (!atomic_dec_and_test(&sony_pf_users))
		return;

	platform_device_del(sony_pf_device);
	platform_device_put(sony_pf_device);
	platform_driver_unregister(&sony_pf_driver);
}

/*********** SNC (SNY5001) Device ***********/

/* the device uses 1-based values, while the backlight subsystem uses
   0-based values */
#define SONY_MAX_BRIGHTNESS	8

#define SNC_VALIDATE_IN		0
#define SNC_VALIDATE_OUT	1

static ssize_t sony_nc_sysfs_show(struct device *, struct device_attribute *,
			      char *);
static ssize_t sony_nc_sysfs_store(struct device *, struct device_attribute *,
			       const char *, size_t);
static int boolean_validate(const int, const int);
static int brightness_default_validate(const int, const int);

struct sony_nc_value {
	char *name;		/* name of the entry */
	char **acpiget;		/* names of the ACPI get function */
	char **acpiset;		/* names of the ACPI set function */
	int (*validate)(const int, const int);	/* input/output validation */
	int value;		/* current setting */
	int valid;		/* Has ever been set */
	int debug;		/* active only in debug mode ? */
	struct device_attribute devattr;	/* sysfs atribute */
};

#define SNC_HANDLE_NAMES(_name, _values...) \
	static char *snc_##_name[] = { _values, NULL }

#define SNC_HANDLE(_name, _getters, _setters, _validate, _debug) \
	{ \
		.name		= __stringify(_name), \
		.acpiget	= _getters, \
		.acpiset	= _setters, \
		.validate	= _validate, \
		.debug		= _debug, \
		.devattr	= __ATTR(_name, 0, sony_nc_sysfs_show, sony_nc_sysfs_store), \
	}

#define SNC_HANDLE_NULL	{ .name = NULL }

SNC_HANDLE_NAMES(fnkey_get, "GHKE");

SNC_HANDLE_NAMES(brightness_def_get, "GPBR");
SNC_HANDLE_NAMES(brightness_def_set, "SPBR");

SNC_HANDLE_NAMES(cdpower_get, "GCDP");
SNC_HANDLE_NAMES(cdpower_set, "SCDP", "CDPW");

SNC_HANDLE_NAMES(audiopower_get, "GAZP");
SNC_HANDLE_NAMES(audiopower_set, "AZPW");

SNC_HANDLE_NAMES(lanpower_get, "GLNP");
SNC_HANDLE_NAMES(lanpower_set, "LNPW");

SNC_HANDLE_NAMES(PID_get, "GPID");

SNC_HANDLE_NAMES(CTR_get, "GCTR");
SNC_HANDLE_NAMES(CTR_set, "SCTR");

SNC_HANDLE_NAMES(PCR_get, "GPCR");
SNC_HANDLE_NAMES(PCR_set, "SPCR");

SNC_HANDLE_NAMES(CMI_get, "GCMI");
SNC_HANDLE_NAMES(CMI_set, "SCMI");

static struct sony_nc_value sony_nc_values[] = {
	SNC_HANDLE(brightness_default, snc_brightness_def_get,
			snc_brightness_def_set, brightness_default_validate, 0),
	SNC_HANDLE(fnkey, snc_fnkey_get, NULL, NULL, 0),
	SNC_HANDLE(cdpower, snc_cdpower_get, snc_cdpower_set, boolean_validate, 0),
	SNC_HANDLE(audiopower, snc_audiopower_get, snc_audiopower_set,
			boolean_validate, 0),
	SNC_HANDLE(lanpower, snc_lanpower_get, snc_lanpower_set,
			boolean_validate, 1),
	/* unknown methods */
	SNC_HANDLE(PID, snc_PID_get, NULL, NULL, 1),
	SNC_HANDLE(CTR, snc_CTR_get, snc_CTR_set, NULL, 1),
	SNC_HANDLE(PCR, snc_PCR_get, snc_PCR_set, NULL, 1),
	SNC_HANDLE(CMI, snc_CMI_get, snc_CMI_set, NULL, 1),
	SNC_HANDLE_NULL
};

static acpi_handle sony_nc_acpi_handle;
static struct acpi_device *sony_nc_acpi_device = NULL;

/*
 * acpi_evaluate_object wrappers
 */
static int acpi_callgetfunc(acpi_handle handle, char *name, int *result)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, name, NULL, &output);
	if ((status == AE_OK) && (out_obj.type == ACPI_TYPE_INTEGER)) {
		*result = out_obj.integer.value;
		return 0;
	}

	printk(KERN_WARNING DRV_PFX "acpi_callreadfunc failed\n");

	return -1;
}

static int acpi_callsetfunc(acpi_handle handle, char *name, int value,
			    int *result)
{
	struct acpi_object_list params;
	union acpi_object in_obj;
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = value;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, name, &params, &output);
	if (status == AE_OK) {
		if (result != NULL) {
			if (out_obj.type != ACPI_TYPE_INTEGER) {
				printk(KERN_WARNING DRV_PFX "acpi_evaluate_object bad "
				       "return type\n");
				return -1;
			}
			*result = out_obj.integer.value;
		}
		return 0;
	}

	printk(KERN_WARNING DRV_PFX "acpi_evaluate_object failed\n");

	return -1;
}

/*
 * sony_nc_values input/output validate functions
 */

/* brightness_default_validate:
 *
 * manipulate input output values to keep consistency with the
 * backlight framework for which brightness values are 0-based.
 */
static int brightness_default_validate(const int direction, const int value)
{
	switch (direction) {
		case SNC_VALIDATE_OUT:
			return value - 1;
		case SNC_VALIDATE_IN:
			if (value >= 0 && value < SONY_MAX_BRIGHTNESS)
				return value + 1;
	}
	return -EINVAL;
}

/* boolean_validate:
 *
 * on input validate boolean values 0/1, on output just pass the
 * received value.
 */
static int boolean_validate(const int direction, const int value)
{
	if (direction == SNC_VALIDATE_IN) {
		if (value != 0 && value != 1)
			return -EINVAL;
	}
	return value;
}

/*
 * Sysfs show/store common to all sony_nc_values
 */
static ssize_t sony_nc_sysfs_show(struct device *dev, struct device_attribute *attr,
			      char *buffer)
{
	int value;
	struct sony_nc_value *item =
	    container_of(attr, struct sony_nc_value, devattr);

	if (!*item->acpiget)
		return -EIO;

	if (acpi_callgetfunc(sony_nc_acpi_handle, *item->acpiget, &value) < 0)
		return -EIO;

	if (item->validate)
		value = item->validate(SNC_VALIDATE_OUT, value);

	return snprintf(buffer, PAGE_SIZE, "%d\n", value);
}

static ssize_t sony_nc_sysfs_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buffer, size_t count)
{
	int value;
	struct sony_nc_value *item =
	    container_of(attr, struct sony_nc_value, devattr);

	if (!item->acpiset)
		return -EIO;

	if (count > 31)
		return -EINVAL;

	value = simple_strtoul(buffer, NULL, 10);

	if (item->validate)
		value = item->validate(SNC_VALIDATE_IN, value);

	if (value < 0)
		return value;

	if (acpi_callsetfunc(sony_nc_acpi_handle, *item->acpiset, value, NULL) < 0)
		return -EIO;
	item->value = value;
	item->valid = 1;
	return count;
}


/*
 * Backlight device
 */
static int sony_backlight_update_status(struct backlight_device *bd)
{
	return acpi_callsetfunc(sony_nc_acpi_handle, "SBRT",
				bd->props.brightness + 1, NULL);
}

static int sony_backlight_get_brightness(struct backlight_device *bd)
{
	int value;

	if (acpi_callgetfunc(sony_nc_acpi_handle, "GBRT", &value))
		return 0;
	/* brightness levels are 1-based, while backlight ones are 0-based */
	return value - 1;
}

static struct backlight_device *sony_backlight_device;
static struct backlight_ops sony_backlight_ops = {
	.update_status = sony_backlight_update_status,
	.get_brightness = sony_backlight_get_brightness,
};

/*
 * ACPI callbacks
 */
static void sony_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	dprintk("sony_acpi_notify, event: %d\n", event);
	sony_laptop_report_input_event(event);
	acpi_bus_generate_event(sony_nc_acpi_device, 1, event);
}

static acpi_status sony_walk_callback(acpi_handle handle, u32 level,
				      void *context, void **return_value)
{
	struct acpi_namespace_node *node;
	union acpi_operand_object *operand;

	node = (struct acpi_namespace_node *)handle;
	operand = (union acpi_operand_object *)node->object;

	printk(KERN_WARNING DRV_PFX "method: name: %4.4s, args %X\n", node->name.ascii,
	       (u32) operand->method.param_count);

	return AE_OK;
}

/*
 * ACPI device
 */
static int sony_nc_resume(struct acpi_device *device)
{
	struct sony_nc_value *item;

	for (item = sony_nc_values; item->name; item++) {
		int ret;

		if (!item->valid)
			continue;
		ret = acpi_callsetfunc(sony_nc_acpi_handle, *item->acpiset,
				       item->value, NULL);
		if (ret < 0) {
			printk("%s: %d\n", __FUNCTION__, ret);
			break;
		}
	}
	return 0;
}

static int sony_nc_add(struct acpi_device *device)
{
	acpi_status status;
	int result = 0;
	acpi_handle handle;
	struct sony_nc_value *item;

	printk(KERN_INFO DRV_PFX "%s v%s.\n",
		SONY_NC_DRIVER_NAME, SONY_LAPTOP_DRIVER_VERSION);

	sony_nc_acpi_device = device;
	strcpy(acpi_device_class(device), "sony/hotkey");

	sony_nc_acpi_handle = device->handle;

	if (debug) {
		status = acpi_walk_namespace(ACPI_TYPE_METHOD, sony_nc_acpi_handle,
					     1, sony_walk_callback, NULL, NULL);
		if (ACPI_FAILURE(status)) {
			printk(KERN_WARNING DRV_PFX "unable to walk acpi resources\n");
			result = -ENODEV;
			goto outwalk;
		}
	}

	/* setup input devices and helper fifo */
	result = sony_laptop_setup_input();
	if (result) {
		printk(KERN_ERR DRV_PFX
				"Unabe to create input devices.\n");
		goto outwalk;
	}

	status = acpi_install_notify_handler(sony_nc_acpi_handle,
					     ACPI_DEVICE_NOTIFY,
					     sony_acpi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		printk(KERN_WARNING DRV_PFX "unable to install notify handler\n");
		result = -ENODEV;
		goto outinput;
	}

	if (ACPI_SUCCESS(acpi_get_handle(sony_nc_acpi_handle, "GBRT", &handle))) {
		sony_backlight_device = backlight_device_register("sony", NULL,
								  NULL,
								  &sony_backlight_ops);

		if (IS_ERR(sony_backlight_device)) {
			printk(KERN_WARNING DRV_PFX "unable to register backlight device\n");
			sony_backlight_device = NULL;
		} else {
			sony_backlight_device->props.brightness =
			    sony_backlight_get_brightness
			    (sony_backlight_device);
			sony_backlight_device->props.max_brightness = 
			    SONY_MAX_BRIGHTNESS - 1;
		}

	}

	result = sony_pf_add();
	if (result)
		goto outbacklight;

	/* create sony_pf sysfs attributes related to the SNC device */
	for (item = sony_nc_values; item->name; ++item) {

		if (!debug && item->debug)
			continue;

		/* find the available acpiget as described in the DSDT */
		for (; item->acpiget && *item->acpiget; ++item->acpiget) {
			if (ACPI_SUCCESS(acpi_get_handle(sony_nc_acpi_handle,
							 *item->acpiget,
							 &handle))) {
				dprintk("Found %s getter: %s\n",
						item->name, *item->acpiget);
				item->devattr.attr.mode |= S_IRUGO;
				break;
			}
		}

		/* find the available acpiset as described in the DSDT */
		for (; item->acpiset && *item->acpiset; ++item->acpiset) {
			if (ACPI_SUCCESS(acpi_get_handle(sony_nc_acpi_handle,
							 *item->acpiset,
							 &handle))) {
				dprintk("Found %s setter: %s\n",
						item->name, *item->acpiset);
				item->devattr.attr.mode |= S_IWUSR;
				break;
			}
		}

		if (item->devattr.attr.mode != 0) {
			result =
			    device_create_file(&sony_pf_device->dev,
					       &item->devattr);
			if (result)
				goto out_sysfs;
		}
	}

	return 0;

      out_sysfs:
	for (item = sony_nc_values; item->name; ++item) {
		device_remove_file(&sony_pf_device->dev, &item->devattr);
	}
	sony_pf_remove();

      outbacklight:
	if (sony_backlight_device)
		backlight_device_unregister(sony_backlight_device);

	status = acpi_remove_notify_handler(sony_nc_acpi_handle,
					    ACPI_DEVICE_NOTIFY,
					    sony_acpi_notify);
	if (ACPI_FAILURE(status))
		printk(KERN_WARNING DRV_PFX "unable to remove notify handler\n");

      outinput:
	sony_laptop_remove_input();

      outwalk:
	return result;
}

static int sony_nc_remove(struct acpi_device *device, int type)
{
	acpi_status status;
	struct sony_nc_value *item;

	if (sony_backlight_device)
		backlight_device_unregister(sony_backlight_device);

	sony_nc_acpi_device = NULL;

	status = acpi_remove_notify_handler(sony_nc_acpi_handle,
					    ACPI_DEVICE_NOTIFY,
					    sony_acpi_notify);
	if (ACPI_FAILURE(status))
		printk(KERN_WARNING DRV_PFX "unable to remove notify handler\n");

	for (item = sony_nc_values; item->name; ++item) {
		device_remove_file(&sony_pf_device->dev, &item->devattr);
	}

	sony_pf_remove();
	sony_laptop_remove_input();
	dprintk(SONY_NC_DRIVER_NAME " removed.\n");

	return 0;
}

static struct acpi_driver sony_nc_driver = {
	.name = SONY_NC_DRIVER_NAME,
	.class = SONY_NC_CLASS,
	.ids = SONY_NC_HID,
	.owner = THIS_MODULE,
	.ops = {
		.add = sony_nc_add,
		.remove = sony_nc_remove,
		.resume = sony_nc_resume,
		},
};

/*********** SPIC (SNY6001) Device ***********/

#define SONYPI_DEVICE_TYPE1	0x00000001
#define SONYPI_DEVICE_TYPE2	0x00000002
#define SONYPI_DEVICE_TYPE3	0x00000004

#define SONY_PIC_EV_MASK	0xff

struct sony_pic_ioport {
	struct acpi_resource_io	io;
	struct list_head	list;
};

struct sony_pic_irq {
	struct acpi_resource_irq	irq;
	struct list_head		list;
};

struct sony_pic_dev {
	int			model;
	u8			camera_power;
	u8			bluetooth_power;
	u8			wwan_power;
	struct acpi_device	*acpi_dev;
	struct sony_pic_irq	*cur_irq;
	struct sony_pic_ioport	*cur_ioport;
	struct list_head	interrupts;
	struct list_head	ioports;
	struct mutex		lock;
};

static struct sony_pic_dev spic_dev = {
	.interrupts	= LIST_HEAD_INIT(spic_dev.interrupts),
	.ioports	= LIST_HEAD_INIT(spic_dev.ioports),
};

/* Event masks */
#define SONYPI_JOGGER_MASK			0x00000001
#define SONYPI_CAPTURE_MASK			0x00000002
#define SONYPI_FNKEY_MASK			0x00000004
#define SONYPI_BLUETOOTH_MASK			0x00000008
#define SONYPI_PKEY_MASK			0x00000010
#define SONYPI_BACK_MASK			0x00000020
#define SONYPI_HELP_MASK			0x00000040
#define SONYPI_LID_MASK				0x00000080
#define SONYPI_ZOOM_MASK			0x00000100
#define SONYPI_THUMBPHRASE_MASK			0x00000200
#define SONYPI_MEYE_MASK			0x00000400
#define SONYPI_MEMORYSTICK_MASK			0x00000800
#define SONYPI_BATTERY_MASK			0x00001000
#define SONYPI_WIRELESS_MASK			0x00002000

struct sonypi_event {
	u8	data;
	u8	event;
};

/* The set of possible button release events */
static struct sonypi_event sonypi_releaseev[] = {
	{ 0x00, SONYPI_EVENT_ANYBUTTON_RELEASED },
	{ 0, 0 }
};

/* The set of possible jogger events  */
static struct sonypi_event sonypi_joggerev[] = {
	{ 0x1f, SONYPI_EVENT_JOGDIAL_UP },
	{ 0x01, SONYPI_EVENT_JOGDIAL_DOWN },
	{ 0x5f, SONYPI_EVENT_JOGDIAL_UP_PRESSED },
	{ 0x41, SONYPI_EVENT_JOGDIAL_DOWN_PRESSED },
	{ 0x1e, SONYPI_EVENT_JOGDIAL_FAST_UP },
	{ 0x02, SONYPI_EVENT_JOGDIAL_FAST_DOWN },
	{ 0x5e, SONYPI_EVENT_JOGDIAL_FAST_UP_PRESSED },
	{ 0x42, SONYPI_EVENT_JOGDIAL_FAST_DOWN_PRESSED },
	{ 0x1d, SONYPI_EVENT_JOGDIAL_VFAST_UP },
	{ 0x03, SONYPI_EVENT_JOGDIAL_VFAST_DOWN },
	{ 0x5d, SONYPI_EVENT_JOGDIAL_VFAST_UP_PRESSED },
	{ 0x43, SONYPI_EVENT_JOGDIAL_VFAST_DOWN_PRESSED },
	{ 0x40, SONYPI_EVENT_JOGDIAL_PRESSED },
	{ 0, 0 }
};

/* The set of possible capture button events */
static struct sonypi_event sonypi_captureev[] = {
	{ 0x05, SONYPI_EVENT_CAPTURE_PARTIALPRESSED },
	{ 0x07, SONYPI_EVENT_CAPTURE_PRESSED },
	{ 0x01, SONYPI_EVENT_CAPTURE_PARTIALRELEASED },
	{ 0, 0 }
};

/* The set of possible fnkeys events */
static struct sonypi_event sonypi_fnkeyev[] = {
	{ 0x10, SONYPI_EVENT_FNKEY_ESC },
	{ 0x11, SONYPI_EVENT_FNKEY_F1 },
	{ 0x12, SONYPI_EVENT_FNKEY_F2 },
	{ 0x13, SONYPI_EVENT_FNKEY_F3 },
	{ 0x14, SONYPI_EVENT_FNKEY_F4 },
	{ 0x15, SONYPI_EVENT_FNKEY_F5 },
	{ 0x16, SONYPI_EVENT_FNKEY_F6 },
	{ 0x17, SONYPI_EVENT_FNKEY_F7 },
	{ 0x18, SONYPI_EVENT_FNKEY_F8 },
	{ 0x19, SONYPI_EVENT_FNKEY_F9 },
	{ 0x1a, SONYPI_EVENT_FNKEY_F10 },
	{ 0x1b, SONYPI_EVENT_FNKEY_F11 },
	{ 0x1c, SONYPI_EVENT_FNKEY_F12 },
	{ 0x1f, SONYPI_EVENT_FNKEY_RELEASED },
	{ 0x21, SONYPI_EVENT_FNKEY_1 },
	{ 0x22, SONYPI_EVENT_FNKEY_2 },
	{ 0x31, SONYPI_EVENT_FNKEY_D },
	{ 0x32, SONYPI_EVENT_FNKEY_E },
	{ 0x33, SONYPI_EVENT_FNKEY_F },
	{ 0x34, SONYPI_EVENT_FNKEY_S },
	{ 0x35, SONYPI_EVENT_FNKEY_B },
	{ 0x36, SONYPI_EVENT_FNKEY_ONLY },
	{ 0, 0 }
};

/* The set of possible program key events */
static struct sonypi_event sonypi_pkeyev[] = {
	{ 0x01, SONYPI_EVENT_PKEY_P1 },
	{ 0x02, SONYPI_EVENT_PKEY_P2 },
	{ 0x04, SONYPI_EVENT_PKEY_P3 },
	{ 0x5c, SONYPI_EVENT_PKEY_P1 },
	{ 0, 0 }
};

/* The set of possible bluetooth events */
static struct sonypi_event sonypi_blueev[] = {
	{ 0x55, SONYPI_EVENT_BLUETOOTH_PRESSED },
	{ 0x59, SONYPI_EVENT_BLUETOOTH_ON },
	{ 0x5a, SONYPI_EVENT_BLUETOOTH_OFF },
	{ 0, 0 }
};

/* The set of possible wireless events */
static struct sonypi_event sonypi_wlessev[] = {
	{ 0x59, SONYPI_EVENT_WIRELESS_ON },
	{ 0x5a, SONYPI_EVENT_WIRELESS_OFF },
	{ 0, 0 }
};

/* The set of possible back button events */
static struct sonypi_event sonypi_backev[] = {
	{ 0x20, SONYPI_EVENT_BACK_PRESSED },
	{ 0, 0 }
};

/* The set of possible help button events */
static struct sonypi_event sonypi_helpev[] = {
	{ 0x3b, SONYPI_EVENT_HELP_PRESSED },
	{ 0, 0 }
};


/* The set of possible lid events */
static struct sonypi_event sonypi_lidev[] = {
	{ 0x51, SONYPI_EVENT_LID_CLOSED },
	{ 0x50, SONYPI_EVENT_LID_OPENED },
	{ 0, 0 }
};

/* The set of possible zoom events */
static struct sonypi_event sonypi_zoomev[] = {
	{ 0x39, SONYPI_EVENT_ZOOM_PRESSED },
	{ 0, 0 }
};

/* The set of possible thumbphrase events */
static struct sonypi_event sonypi_thumbphraseev[] = {
	{ 0x3a, SONYPI_EVENT_THUMBPHRASE_PRESSED },
	{ 0, 0 }
};

/* The set of possible motioneye camera events */
static struct sonypi_event sonypi_meyeev[] = {
	{ 0x00, SONYPI_EVENT_MEYE_FACE },
	{ 0x01, SONYPI_EVENT_MEYE_OPPOSITE },
	{ 0, 0 }
};

/* The set of possible memorystick events */
static struct sonypi_event sonypi_memorystickev[] = {
	{ 0x53, SONYPI_EVENT_MEMORYSTICK_INSERT },
	{ 0x54, SONYPI_EVENT_MEMORYSTICK_EJECT },
	{ 0, 0 }
};

/* The set of possible battery events */
static struct sonypi_event sonypi_batteryev[] = {
	{ 0x20, SONYPI_EVENT_BATTERY_INSERT },
	{ 0x30, SONYPI_EVENT_BATTERY_REMOVE },
	{ 0, 0 }
};

static struct sonypi_eventtypes {
	int			model;
	u8			data;
	unsigned long		mask;
	struct sonypi_event *	events;
} sony_pic_eventtypes[] = {
	{ SONYPI_DEVICE_TYPE1, 0, 0xffffffff, sonypi_releaseev },
	{ SONYPI_DEVICE_TYPE1, 0x70, SONYPI_MEYE_MASK, sonypi_meyeev },
	{ SONYPI_DEVICE_TYPE1, 0x30, SONYPI_LID_MASK, sonypi_lidev },
	{ SONYPI_DEVICE_TYPE1, 0x60, SONYPI_CAPTURE_MASK, sonypi_captureev },
	{ SONYPI_DEVICE_TYPE1, 0x10, SONYPI_JOGGER_MASK, sonypi_joggerev },
	{ SONYPI_DEVICE_TYPE1, 0x20, SONYPI_FNKEY_MASK, sonypi_fnkeyev },
	{ SONYPI_DEVICE_TYPE1, 0x30, SONYPI_BLUETOOTH_MASK, sonypi_blueev },
	{ SONYPI_DEVICE_TYPE1, 0x40, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ SONYPI_DEVICE_TYPE1, 0x30, SONYPI_MEMORYSTICK_MASK, sonypi_memorystickev },
	{ SONYPI_DEVICE_TYPE1, 0x40, SONYPI_BATTERY_MASK, sonypi_batteryev },

	{ SONYPI_DEVICE_TYPE2, 0, 0xffffffff, sonypi_releaseev },
	{ SONYPI_DEVICE_TYPE2, 0x38, SONYPI_LID_MASK, sonypi_lidev },
	{ SONYPI_DEVICE_TYPE2, 0x11, SONYPI_JOGGER_MASK, sonypi_joggerev },
	{ SONYPI_DEVICE_TYPE2, 0x61, SONYPI_CAPTURE_MASK, sonypi_captureev },
	{ SONYPI_DEVICE_TYPE2, 0x21, SONYPI_FNKEY_MASK, sonypi_fnkeyev },
	{ SONYPI_DEVICE_TYPE2, 0x31, SONYPI_BLUETOOTH_MASK, sonypi_blueev },
	{ SONYPI_DEVICE_TYPE2, 0x08, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ SONYPI_DEVICE_TYPE2, 0x11, SONYPI_BACK_MASK, sonypi_backev },
	{ SONYPI_DEVICE_TYPE2, 0x21, SONYPI_HELP_MASK, sonypi_helpev },
	{ SONYPI_DEVICE_TYPE2, 0x21, SONYPI_ZOOM_MASK, sonypi_zoomev },
	{ SONYPI_DEVICE_TYPE2, 0x20, SONYPI_THUMBPHRASE_MASK, sonypi_thumbphraseev },
	{ SONYPI_DEVICE_TYPE2, 0x31, SONYPI_MEMORYSTICK_MASK, sonypi_memorystickev },
	{ SONYPI_DEVICE_TYPE2, 0x41, SONYPI_BATTERY_MASK, sonypi_batteryev },
	{ SONYPI_DEVICE_TYPE2, 0x31, SONYPI_PKEY_MASK, sonypi_pkeyev },

	{ SONYPI_DEVICE_TYPE3, 0, 0xffffffff, sonypi_releaseev },
	{ SONYPI_DEVICE_TYPE3, 0x21, SONYPI_FNKEY_MASK, sonypi_fnkeyev },
	{ SONYPI_DEVICE_TYPE3, 0x31, SONYPI_WIRELESS_MASK, sonypi_wlessev },
	{ SONYPI_DEVICE_TYPE3, 0x31, SONYPI_MEMORYSTICK_MASK, sonypi_memorystickev },
	{ SONYPI_DEVICE_TYPE3, 0x41, SONYPI_BATTERY_MASK, sonypi_batteryev },
	{ SONYPI_DEVICE_TYPE3, 0x31, SONYPI_PKEY_MASK, sonypi_pkeyev },
	{ 0 }
};

static int sony_pic_detect_device_type(void)
{
	struct pci_dev *pcidev;
	int model = 0;

	if ((pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
				     PCI_DEVICE_ID_INTEL_82371AB_3, NULL)))
		model = SONYPI_DEVICE_TYPE1;

	else if ((pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
					  PCI_DEVICE_ID_INTEL_ICH6_1, NULL)))
		model = SONYPI_DEVICE_TYPE3;

	else if ((pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
					  PCI_DEVICE_ID_INTEL_ICH7_1, NULL)))
		model = SONYPI_DEVICE_TYPE3;

	else
		model = SONYPI_DEVICE_TYPE2;

	if (pcidev)
		pci_dev_put(pcidev);

	printk(KERN_INFO DRV_PFX "detected Type%d model\n",
			model == SONYPI_DEVICE_TYPE1 ? 1 :
			model == SONYPI_DEVICE_TYPE2 ? 2 : 3);
	return model;
}

#define ITERATIONS_LONG		10000
#define ITERATIONS_SHORT	10
#define wait_on_command(command, iterations) {				\
	unsigned int n = iterations;					\
	while (--n && (command))					\
		udelay(1);						\
	if (!n)								\
		dprintk("command failed at %s : %s (line %d)\n",	\
				__FILE__, __FUNCTION__, __LINE__);	\
}

static u8 sony_pic_call1(u8 dev)
{
	u8 v1, v2;

	wait_on_command(inb_p(spic_dev.cur_ioport->io.minimum + 4) & 2,
			ITERATIONS_LONG);
	outb(dev, spic_dev.cur_ioport->io.minimum + 4);
	v1 = inb_p(spic_dev.cur_ioport->io.minimum + 4);
	v2 = inb_p(spic_dev.cur_ioport->io.minimum);
	dprintk("sony_pic_call1: 0x%.4x\n", (v2 << 8) | v1);
	return v2;
}

static u8 sony_pic_call2(u8 dev, u8 fn)
{
	u8 v1;

	wait_on_command(inb_p(spic_dev.cur_ioport->io.minimum + 4) & 2,
			ITERATIONS_LONG);
	outb(dev, spic_dev.cur_ioport->io.minimum + 4);
	wait_on_command(inb_p(spic_dev.cur_ioport->io.minimum + 4) & 2,
			ITERATIONS_LONG);
	outb(fn, spic_dev.cur_ioport->io.minimum);
	v1 = inb_p(spic_dev.cur_ioport->io.minimum);
	dprintk("sony_pic_call2: 0x%.4x\n", v1);
	return v1;
}

static u8 sony_pic_call3(u8 dev, u8 fn, u8 v)
{
	u8 v1;

	wait_on_command(inb_p(spic_dev.cur_ioport->io.minimum + 4) & 2, ITERATIONS_LONG);
	outb(dev, spic_dev.cur_ioport->io.minimum + 4);
	wait_on_command(inb_p(spic_dev.cur_ioport->io.minimum + 4) & 2, ITERATIONS_LONG);
	outb(fn, spic_dev.cur_ioport->io.minimum);
	wait_on_command(inb_p(spic_dev.cur_ioport->io.minimum + 4) & 2, ITERATIONS_LONG);
	outb(v, spic_dev.cur_ioport->io.minimum);
	v1 = inb_p(spic_dev.cur_ioport->io.minimum);
	dprintk("sony_pic_call3: 0x%.4x\n", v1);
	return v1;
}

/* camera tests and poweron/poweroff */
#define SONYPI_CAMERA_PICTURE		5
#define SONYPI_CAMERA_CONTROL		0x10

#define SONYPI_CAMERA_BRIGHTNESS		0
#define SONYPI_CAMERA_CONTRAST			1
#define SONYPI_CAMERA_HUE			2
#define SONYPI_CAMERA_COLOR			3
#define SONYPI_CAMERA_SHARPNESS			4

#define SONYPI_CAMERA_EXPOSURE_MASK		0xC
#define SONYPI_CAMERA_WHITE_BALANCE_MASK	0x3
#define SONYPI_CAMERA_PICTURE_MODE_MASK		0x30
#define SONYPI_CAMERA_MUTE_MASK			0x40

/* the rest don't need a loop until not 0xff */
#define SONYPI_CAMERA_AGC			6
#define SONYPI_CAMERA_AGC_MASK			0x30
#define SONYPI_CAMERA_SHUTTER_MASK 		0x7

#define SONYPI_CAMERA_SHUTDOWN_REQUEST		7
#define SONYPI_CAMERA_CONTROL			0x10

#define SONYPI_CAMERA_STATUS 			7
#define SONYPI_CAMERA_STATUS_READY 		0x2
#define SONYPI_CAMERA_STATUS_POSITION		0x4

#define SONYPI_DIRECTION_BACKWARDS 		0x4

#define SONYPI_CAMERA_REVISION 			8
#define SONYPI_CAMERA_ROMVERSION 		9

static int __sony_pic_camera_ready(void)
{
	u8 v;

	v = sony_pic_call2(0x8f, SONYPI_CAMERA_STATUS);
	return (v != 0xff && (v & SONYPI_CAMERA_STATUS_READY));
}

static int __sony_pic_camera_off(void)
{
	if (!camera) {
		printk(KERN_WARNING DRV_PFX "camera control not enabled\n");
		return -ENODEV;
	}

	wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_PICTURE,
				SONYPI_CAMERA_MUTE_MASK),
			ITERATIONS_SHORT);

	if (spic_dev.camera_power) {
		sony_pic_call2(0x91, 0);
		spic_dev.camera_power = 0;
	}
	return 0;
}

static int __sony_pic_camera_on(void)
{
	int i, j, x;

	if (!camera) {
		printk(KERN_WARNING DRV_PFX "camera control not enabled\n");
		return -ENODEV;
	}

	if (spic_dev.camera_power)
		return 0;

	for (j = 5; j > 0; j--) {

		for (x = 0; x < 100 && sony_pic_call2(0x91, 0x1); x++)
			msleep(10);
		sony_pic_call1(0x93);

		for (i = 400; i > 0; i--) {
			if (__sony_pic_camera_ready())
				break;
			msleep(10);
		}
		if (i)
			break;
	}

	if (j == 0) {
		printk(KERN_WARNING DRV_PFX "failed to power on camera\n");
		return -ENODEV;
	}

	wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_CONTROL,
				0x5a),
			ITERATIONS_SHORT);

	spic_dev.camera_power = 1;
	return 0;
}

/* External camera command (exported to the motion eye v4l driver) */
int sony_pic_camera_command(int command, u8 value)
{
	if (!camera)
		return -EIO;

	mutex_lock(&spic_dev.lock);

	switch (command) {
	case SONY_PIC_COMMAND_SETCAMERA:
		if (value)
			__sony_pic_camera_on();
		else
			__sony_pic_camera_off();
		break;
	case SONY_PIC_COMMAND_SETCAMERABRIGHTNESS:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_BRIGHTNESS, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERACONTRAST:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_CONTRAST, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERAHUE:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_HUE, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERACOLOR:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_COLOR, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERASHARPNESS:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_SHARPNESS, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERAPICTURE:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_PICTURE, value),
				ITERATIONS_SHORT);
		break;
	case SONY_PIC_COMMAND_SETCAMERAAGC:
		wait_on_command(sony_pic_call3(0x90, SONYPI_CAMERA_AGC, value),
				ITERATIONS_SHORT);
		break;
	default:
		printk(KERN_ERR DRV_PFX "sony_pic_camera_command invalid: %d\n",
		       command);
		break;
	}
	mutex_unlock(&spic_dev.lock);
	return 0;
}
EXPORT_SYMBOL(sony_pic_camera_command);

/* gprs/edge modem (SZ460N and SZ210P), thanks to Joshua Wise */
static void sony_pic_set_wwanpower(u8 state)
{
	state = !!state;
	mutex_lock(&spic_dev.lock);
	if (spic_dev.wwan_power == state) {
		mutex_unlock(&spic_dev.lock);
		return;
	}
	sony_pic_call2(0xB0, state);
	spic_dev.wwan_power = state;
	mutex_unlock(&spic_dev.lock);
}

static ssize_t sony_pic_wwanpower_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned long value;
	if (count > 31)
		return -EINVAL;

	value = simple_strtoul(buffer, NULL, 10);
	sony_pic_set_wwanpower(value);

	return count;
}

static ssize_t sony_pic_wwanpower_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t count;
	mutex_lock(&spic_dev.lock);
	count = snprintf(buffer, PAGE_SIZE, "%d\n", spic_dev.wwan_power);
	mutex_unlock(&spic_dev.lock);
	return count;
}

/* bluetooth subsystem power state */
static void __sony_pic_set_bluetoothpower(u8 state)
{
	state = !!state;
	if (spic_dev.bluetooth_power == state)
		return;
	sony_pic_call2(0x96, state);
	sony_pic_call1(0x82);
	spic_dev.bluetooth_power = state;
}

static ssize_t sony_pic_bluetoothpower_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned long value;
	if (count > 31)
		return -EINVAL;

	value = simple_strtoul(buffer, NULL, 10);
	mutex_lock(&spic_dev.lock);
	__sony_pic_set_bluetoothpower(value);
	mutex_unlock(&spic_dev.lock);

	return count;
}

static ssize_t sony_pic_bluetoothpower_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	ssize_t count = 0;
	mutex_lock(&spic_dev.lock);
	count = snprintf(buffer, PAGE_SIZE, "%d\n", spic_dev.bluetooth_power);
	mutex_unlock(&spic_dev.lock);
	return count;
}

/* fan speed */
/* FAN0 information (reverse engineered from ACPI tables) */
#define SONY_PIC_FAN0_STATUS	0x93
static int sony_pic_set_fanspeed(unsigned long value)
{
	return ec_write(SONY_PIC_FAN0_STATUS, value);
}

static int sony_pic_get_fanspeed(u8 *value)
{
	return ec_read(SONY_PIC_FAN0_STATUS, value);
}

static ssize_t sony_pic_fanspeed_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t count)
{
	unsigned long value;
	if (count > 31)
		return -EINVAL;

	value = simple_strtoul(buffer, NULL, 10);
	if (sony_pic_set_fanspeed(value))
		return -EIO;

	return count;
}

static ssize_t sony_pic_fanspeed_show(struct device *dev,
		struct device_attribute *attr, char *buffer)
{
	u8 value = 0;
	if (sony_pic_get_fanspeed(&value))
		return -EIO;

	return snprintf(buffer, PAGE_SIZE, "%d\n", value);
}

#define SPIC_ATTR(_name, _mode)					\
struct device_attribute spic_attr_##_name = __ATTR(_name,	\
		_mode, sony_pic_## _name ##_show,		\
		sony_pic_## _name ##_store)

static SPIC_ATTR(bluetoothpower, 0644);
static SPIC_ATTR(wwanpower, 0644);
static SPIC_ATTR(fanspeed, 0644);

static struct attribute *spic_attributes[] = {
	&spic_attr_bluetoothpower.attr,
	&spic_attr_wwanpower.attr,
	&spic_attr_fanspeed.attr,
	NULL
};

static struct attribute_group spic_attribute_group = {
	.attrs = spic_attributes
};

/******** SONYPI compatibility **********/
#ifdef CONFIG_SONY_LAPTOP_OLD

/* battery / brightness / temperature  addresses */
#define SONYPI_BAT_FLAGS	0x81
#define SONYPI_LCD_LIGHT	0x96
#define SONYPI_BAT1_PCTRM	0xa0
#define SONYPI_BAT1_LEFT	0xa2
#define SONYPI_BAT1_MAXRT	0xa4
#define SONYPI_BAT2_PCTRM	0xa8
#define SONYPI_BAT2_LEFT	0xaa
#define SONYPI_BAT2_MAXRT	0xac
#define SONYPI_BAT1_MAXTK	0xb0
#define SONYPI_BAT1_FULL	0xb2
#define SONYPI_BAT2_MAXTK	0xb8
#define SONYPI_BAT2_FULL	0xba
#define SONYPI_TEMP_STATUS	0xC1

struct sonypi_compat_s {
	struct fasync_struct	*fifo_async;
	struct kfifo		*fifo;
	spinlock_t		fifo_lock;
	wait_queue_head_t	fifo_proc_list;
	atomic_t		open_count;
};
static struct sonypi_compat_s sonypi_compat = {
	.open_count = ATOMIC_INIT(0),
};

static int sonypi_misc_fasync(int fd, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(fd, filp, on, &sonypi_compat.fifo_async);
	if (retval < 0)
		return retval;
	return 0;
}

static int sonypi_misc_release(struct inode *inode, struct file *file)
{
	sonypi_misc_fasync(-1, file, 0);
	atomic_dec(&sonypi_compat.open_count);
	return 0;
}

static int sonypi_misc_open(struct inode *inode, struct file *file)
{
	/* Flush input queue on first open */
	if (atomic_inc_return(&sonypi_compat.open_count) == 1)
		kfifo_reset(sonypi_compat.fifo);
	return 0;
}

static ssize_t sonypi_misc_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	ssize_t ret;
	unsigned char c;

	if ((kfifo_len(sonypi_compat.fifo) == 0) &&
	    (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	ret = wait_event_interruptible(sonypi_compat.fifo_proc_list,
				       kfifo_len(sonypi_compat.fifo) != 0);
	if (ret)
		return ret;

	while (ret < count &&
	       (kfifo_get(sonypi_compat.fifo, &c, sizeof(c)) == sizeof(c))) {
		if (put_user(c, buf++))
			return -EFAULT;
		ret++;
	}

	if (ret > 0) {
		struct inode *inode = file->f_path.dentry->d_inode;
		inode->i_atime = current_fs_time(inode->i_sb);
	}

	return ret;
}

static unsigned int sonypi_misc_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &sonypi_compat.fifo_proc_list, wait);
	if (kfifo_len(sonypi_compat.fifo))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int ec_read16(u8 addr, u16 *value)
{
	u8 val_lb, val_hb;
	if (ec_read(addr, &val_lb))
		return -1;
	if (ec_read(addr + 1, &val_hb))
		return -1;
	*value = val_lb | (val_hb << 8);
	return 0;
}

static int sonypi_misc_ioctl(struct inode *ip, struct file *fp,
			     unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	u8 val8;
	u16 val16;
	int value;

	mutex_lock(&spic_dev.lock);
	switch (cmd) {
	case SONYPI_IOCGBRT:
		if (sony_backlight_device == NULL) {
			ret = -EIO;
			break;
		}
		if (acpi_callgetfunc(sony_nc_acpi_handle, "GBRT", &value)) {
			ret = -EIO;
			break;
		}
		val8 = ((value & 0xff) - 1) << 5;
		if (copy_to_user(argp, &val8, sizeof(val8)))
				ret = -EFAULT;
		break;
	case SONYPI_IOCSBRT:
		if (sony_backlight_device == NULL) {
			ret = -EIO;
			break;
		}
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		if (acpi_callsetfunc(sony_nc_acpi_handle, "SBRT",
				(val8 >> 5) + 1, NULL)) {
			ret = -EIO;
			break;
		}
		/* sync the backlight device status */
		sony_backlight_device->props.brightness =
		    sony_backlight_get_brightness(sony_backlight_device);
		break;
	case SONYPI_IOCGBAT1CAP:
		if (ec_read16(SONYPI_BAT1_FULL, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT1REM:
		if (ec_read16(SONYPI_BAT1_LEFT, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT2CAP:
		if (ec_read16(SONYPI_BAT2_FULL, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT2REM:
		if (ec_read16(SONYPI_BAT2_LEFT, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBATFLAGS:
		if (ec_read(SONYPI_BAT_FLAGS, &val8)) {
			ret = -EIO;
			break;
		}
		val8 &= 0x07;
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBLUE:
		val8 = spic_dev.bluetooth_power;
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSBLUE:
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		__sony_pic_set_bluetoothpower(val8);
		break;
	/* FAN Controls */
	case SONYPI_IOCGFAN:
		if (sony_pic_get_fanspeed(&val8)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSFAN:
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		if (sony_pic_set_fanspeed(val8))
			ret = -EIO;
		break;
	/* GET Temperature (useful under APM) */
	case SONYPI_IOCGTEMP:
		if (ec_read(SONYPI_TEMP_STATUS, &val8)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&spic_dev.lock);
	return ret;
}

static const struct file_operations sonypi_misc_fops = {
	.owner		= THIS_MODULE,
	.read		= sonypi_misc_read,
	.poll		= sonypi_misc_poll,
	.open		= sonypi_misc_open,
	.release	= sonypi_misc_release,
	.fasync		= sonypi_misc_fasync,
	.ioctl		= sonypi_misc_ioctl,
};

static struct miscdevice sonypi_misc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "sonypi",
	.fops		= &sonypi_misc_fops,
};

static void sonypi_compat_report_event(u8 event)
{
	kfifo_put(sonypi_compat.fifo, (unsigned char *)&event, sizeof(event));
	kill_fasync(&sonypi_compat.fifo_async, SIGIO, POLL_IN);
	wake_up_interruptible(&sonypi_compat.fifo_proc_list);
}

static int sonypi_compat_init(void)
{
	int error;

	spin_lock_init(&sonypi_compat.fifo_lock);
	sonypi_compat.fifo = kfifo_alloc(SONY_LAPTOP_BUF_SIZE, GFP_KERNEL,
					 &sonypi_compat.fifo_lock);
	if (IS_ERR(sonypi_compat.fifo)) {
		printk(KERN_ERR DRV_PFX "kfifo_alloc failed\n");
		return PTR_ERR(sonypi_compat.fifo);
	}

	init_waitqueue_head(&sonypi_compat.fifo_proc_list);

	if (minor != -1)
		sonypi_misc_device.minor = minor;
	error = misc_register(&sonypi_misc_device);
	if (error) {
		printk(KERN_ERR DRV_PFX "misc_register failed\n");
		goto err_free_kfifo;
	}
	if (minor == -1)
		printk(KERN_INFO DRV_PFX "device allocated minor is %d\n",
		       sonypi_misc_device.minor);

	return 0;

err_free_kfifo:
	kfifo_free(sonypi_compat.fifo);
	return error;
}

static void sonypi_compat_exit(void)
{
	misc_deregister(&sonypi_misc_device);
	kfifo_free(sonypi_compat.fifo);
}
#else
static int sonypi_compat_init(void) { return 0; }
static void sonypi_compat_exit(void) { }
static void sonypi_compat_report_event(u8 event) { }
#endif /* CONFIG_SONY_LAPTOP_OLD */

/*
 * ACPI callbacks
 */
static acpi_status
sony_pic_read_possible_resource(struct acpi_resource *resource, void *context)
{
	u32 i;
	struct sony_pic_dev *dev = (struct sony_pic_dev *)context;

	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
	case ACPI_RESOURCE_TYPE_END_DEPENDENT:
		return AE_OK;

	case ACPI_RESOURCE_TYPE_IRQ:
		{
			struct acpi_resource_irq *p = &resource->data.irq;
			struct sony_pic_irq *interrupt = NULL;
			if (!p || !p->interrupt_count) {
				/*
				 * IRQ descriptors may have no IRQ# bits set,
				 * particularly those those w/ _STA disabled
				 */
				dprintk("Blank IRQ resource\n");
				return AE_OK;
			}
			for (i = 0; i < p->interrupt_count; i++) {
				if (!p->interrupts[i]) {
					printk(KERN_WARNING DRV_PFX
							"Invalid IRQ %d\n",
							p->interrupts[i]);
					continue;
				}
				interrupt = kzalloc(sizeof(*interrupt),
						GFP_KERNEL);
				if (!interrupt)
					return AE_ERROR;

				list_add_tail(&interrupt->list, &dev->interrupts);
				interrupt->irq.triggering = p->triggering;
				interrupt->irq.polarity = p->polarity;
				interrupt->irq.sharable = p->sharable;
				interrupt->irq.interrupt_count = 1;
				interrupt->irq.interrupts[0] = p->interrupts[i];
			}
			return AE_OK;
		}
	case ACPI_RESOURCE_TYPE_IO:
		{
			struct acpi_resource_io *io = &resource->data.io;
			struct sony_pic_ioport *ioport = NULL;
			if (!io) {
				dprintk("Blank IO resource\n");
				return AE_OK;
			}

			ioport = kzalloc(sizeof(*ioport), GFP_KERNEL);
			if (!ioport)
				return AE_ERROR;

			list_add_tail(&ioport->list, &dev->ioports);
			memcpy(&ioport->io, io, sizeof(*io));
			return AE_OK;
		}
	default:
		dprintk("Resource %d isn't an IRQ nor an IO port\n",
				resource->type);

	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	}
	return AE_CTRL_TERMINATE;
}

static int sony_pic_possible_resources(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = AE_OK;

	if (!device)
		return -EINVAL;

	/* get device status */
	/* see acpi_pci_link_get_current acpi_pci_link_get_possible */
	dprintk("Evaluating _STA\n");
	result = acpi_bus_get_status(device);
	if (result) {
		printk(KERN_WARNING DRV_PFX "Unable to read status\n");
		goto end;
	}

	if (!device->status.enabled)
		dprintk("Device disabled\n");
	else
		dprintk("Device enabled\n");

	/*
	 * Query and parse 'method'
	 */
	dprintk("Evaluating %s\n", METHOD_NAME__PRS);
	status = acpi_walk_resources(device->handle, METHOD_NAME__PRS,
			sony_pic_read_possible_resource, &spic_dev);
	if (ACPI_FAILURE(status)) {
		printk(KERN_WARNING DRV_PFX
				"Failure evaluating %s\n",
				METHOD_NAME__PRS);
		result = -ENODEV;
	}
end:
	return result;
}

/*
 *  Disable the spic device by calling its _DIS method
 */
static int sony_pic_disable(struct acpi_device *device)
{
	if (ACPI_FAILURE(acpi_evaluate_object(device->handle, "_DIS", 0, NULL)))
		return -ENXIO;

	dprintk("Device disabled\n");
	return 0;
}


/*
 *  Based on drivers/acpi/pci_link.c:acpi_pci_link_set
 *
 *  Call _SRS to set current resources
 */
static int sony_pic_enable(struct acpi_device *device,
		struct sony_pic_ioport *ioport, struct sony_pic_irq *irq)
{
	acpi_status status;
	int result = 0;
	struct {
		struct acpi_resource io_res;
		struct acpi_resource irq_res;
		struct acpi_resource end;
	} *resource;
	struct acpi_buffer buffer = { 0, NULL };

	if (!ioport || !irq)
		return -EINVAL;

	/* init acpi_buffer */
	resource = kzalloc(sizeof(*resource) + 1, GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	buffer.length = sizeof(*resource) + 1;
	buffer.pointer = resource;

	/* setup io resource */
	resource->io_res.type = ACPI_RESOURCE_TYPE_IO;
	resource->io_res.length = sizeof(struct acpi_resource);
	memcpy(&resource->io_res.data.io, &ioport->io,
			sizeof(struct acpi_resource_io));

	/* setup irq resource */
	resource->irq_res.type = ACPI_RESOURCE_TYPE_IRQ;
	resource->irq_res.length = sizeof(struct acpi_resource);
	memcpy(&resource->irq_res.data.irq, &irq->irq,
			sizeof(struct acpi_resource_irq));
	/* we requested a shared irq */
	resource->irq_res.data.irq.sharable = ACPI_SHARED;

	resource->end.type = ACPI_RESOURCE_TYPE_END_TAG;

	/* Attempt to set the resource */
	dprintk("Evaluating _SRS\n");
	status = acpi_set_current_resources(device->handle, &buffer);

	/* check for total failure */
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR DRV_PFX "Error evaluating _SRS");
		result = -ENODEV;
		goto end;
	}

	/* Necessary device initializations calls (from sonypi) */
	sony_pic_call1(0x82);
	sony_pic_call2(0x81, 0xff);
	sony_pic_call1(compat ? 0x92 : 0x82);

end:
	kfree(resource);
	return result;
}

/*****************
 *
 * ISR: some event is available
 *
 *****************/
static irqreturn_t sony_pic_irq(int irq, void *dev_id)
{
	int i, j;
	u32 port_val = 0;
	u8 ev = 0;
	u8 data_mask = 0;
	u8 device_event = 0;

	struct sony_pic_dev *dev = (struct sony_pic_dev *) dev_id;

	acpi_os_read_port(dev->cur_ioport->io.minimum, &port_val,
			dev->cur_ioport->io.address_length);
	ev = port_val & SONY_PIC_EV_MASK;
	data_mask = 0xff & (port_val >> (dev->cur_ioport->io.address_length - 8));

	dprintk("event (0x%.8x [%.2x] [%.2x]) at port 0x%.4x\n",
			port_val, ev, data_mask, dev->cur_ioport->io.minimum);

	if (ev == 0x00 || ev == 0xff)
		return IRQ_HANDLED;

	for (i = 0; sony_pic_eventtypes[i].model; i++) {

		if (spic_dev.model != sony_pic_eventtypes[i].model)
			continue;

		if ((data_mask & sony_pic_eventtypes[i].data) !=
		    sony_pic_eventtypes[i].data)
			continue;

		if (!(mask & sony_pic_eventtypes[i].mask))
			continue;

		for (j = 0; sony_pic_eventtypes[i].events[j].event; j++) {
			if (ev == sony_pic_eventtypes[i].events[j].data) {
				device_event =
					sony_pic_eventtypes[i].events[j].event;
				goto found;
			}
		}
	}
	return IRQ_HANDLED;

found:
	sony_laptop_report_input_event(device_event);
	acpi_bus_generate_event(spic_dev.acpi_dev, 1, device_event);
	sonypi_compat_report_event(device_event);

	return IRQ_HANDLED;
}

/*****************
 *
 *  ACPI driver
 *
 *****************/
static int sony_pic_remove(struct acpi_device *device, int type)
{
	struct sony_pic_ioport *io, *tmp_io;
	struct sony_pic_irq *irq, *tmp_irq;

	sonypi_compat_exit();

	if (sony_pic_disable(device)) {
		printk(KERN_ERR DRV_PFX "Couldn't disable device.\n");
		return -ENXIO;
	}

	free_irq(spic_dev.cur_irq->irq.interrupts[0], &spic_dev);
	release_region(spic_dev.cur_ioport->io.minimum,
			spic_dev.cur_ioport->io.address_length);

	sony_laptop_remove_input();

	/* pf attrs */
	sysfs_remove_group(&sony_pf_device->dev.kobj, &spic_attribute_group);
	sony_pf_remove();

	list_for_each_entry_safe(io, tmp_io, &spic_dev.ioports, list) {
		list_del(&io->list);
		kfree(io);
	}
	list_for_each_entry_safe(irq, tmp_irq, &spic_dev.interrupts, list) {
		list_del(&irq->list);
		kfree(irq);
	}
	spic_dev.cur_ioport = NULL;
	spic_dev.cur_irq = NULL;

	dprintk(SONY_PIC_DRIVER_NAME " removed.\n");
	return 0;
}

static int sony_pic_add(struct acpi_device *device)
{
	int result;
	struct sony_pic_ioport *io, *tmp_io;
	struct sony_pic_irq *irq, *tmp_irq;

	printk(KERN_INFO DRV_PFX "%s v%s.\n",
		SONY_PIC_DRIVER_NAME, SONY_LAPTOP_DRIVER_VERSION);

	spic_dev.acpi_dev = device;
	strcpy(acpi_device_class(device), "sony/hotkey");
	spic_dev.model = sony_pic_detect_device_type();
	mutex_init(&spic_dev.lock);

	/* read _PRS resources */
	result = sony_pic_possible_resources(device);
	if (result) {
		printk(KERN_ERR DRV_PFX
				"Unabe to read possible resources.\n");
		goto err_free_resources;
	}

	/* setup input devices and helper fifo */
	result = sony_laptop_setup_input();
	if (result) {
		printk(KERN_ERR DRV_PFX
				"Unabe to create input devices.\n");
		goto err_free_resources;
	}

	/* request io port */
	list_for_each_entry(io, &spic_dev.ioports, list) {
		if (request_region(io->io.minimum, io->io.address_length,
					"Sony Programable I/O Device")) {
			dprintk("I/O port: 0x%.4x (0x%.4x) + 0x%.2x\n",
					io->io.minimum, io->io.maximum,
					io->io.address_length);
			spic_dev.cur_ioport = io;
			break;
		}
	}
	if (!spic_dev.cur_ioport) {
		printk(KERN_ERR DRV_PFX "Failed to request_region.\n");
		result = -ENODEV;
		goto err_remove_input;
	}

	/* request IRQ */
	list_for_each_entry(irq, &spic_dev.interrupts, list) {
		if (!request_irq(irq->irq.interrupts[0], sony_pic_irq,
					IRQF_SHARED, "sony-laptop", &spic_dev)) {
			dprintk("IRQ: %d - triggering: %d - "
					"polarity: %d - shr: %d\n",
					irq->irq.interrupts[0],
					irq->irq.triggering,
					irq->irq.polarity,
					irq->irq.sharable);
			spic_dev.cur_irq = irq;
			break;
		}
	}
	if (!spic_dev.cur_irq) {
		printk(KERN_ERR DRV_PFX "Failed to request_irq.\n");
		result = -ENODEV;
		goto err_release_region;
	}

	/* set resource status _SRS */
	result = sony_pic_enable(device, spic_dev.cur_ioport, spic_dev.cur_irq);
	if (result) {
		printk(KERN_ERR DRV_PFX "Couldn't enable device.\n");
		goto err_free_irq;
	}

	spic_dev.bluetooth_power = -1;
	/* create device attributes */
	result = sony_pf_add();
	if (result)
		goto err_disable_device;

	result = sysfs_create_group(&sony_pf_device->dev.kobj, &spic_attribute_group);
	if (result)
		goto err_remove_pf;

	if (sonypi_compat_init())
		goto err_remove_pf;

	return 0;

err_remove_pf:
	sony_pf_remove();

err_disable_device:
	sony_pic_disable(device);

err_free_irq:
	free_irq(spic_dev.cur_irq->irq.interrupts[0], &spic_dev);

err_release_region:
	release_region(spic_dev.cur_ioport->io.minimum,
			spic_dev.cur_ioport->io.address_length);

err_remove_input:
	sony_laptop_remove_input();

err_free_resources:
	list_for_each_entry_safe(io, tmp_io, &spic_dev.ioports, list) {
		list_del(&io->list);
		kfree(io);
	}
	list_for_each_entry_safe(irq, tmp_irq, &spic_dev.interrupts, list) {
		list_del(&irq->list);
		kfree(irq);
	}
	spic_dev.cur_ioport = NULL;
	spic_dev.cur_irq = NULL;

	return result;
}

static int sony_pic_suspend(struct acpi_device *device, pm_message_t state)
{
	if (sony_pic_disable(device))
		return -ENXIO;
	return 0;
}

static int sony_pic_resume(struct acpi_device *device)
{
	sony_pic_enable(device, spic_dev.cur_ioport, spic_dev.cur_irq);
	return 0;
}

static struct acpi_driver sony_pic_driver = {
	.name = SONY_PIC_DRIVER_NAME,
	.class = SONY_PIC_CLASS,
	.ids = SONY_PIC_HID,
	.owner = THIS_MODULE,
	.ops = {
		.add = sony_pic_add,
		.remove = sony_pic_remove,
		.suspend = sony_pic_suspend,
		.resume = sony_pic_resume,
		},
};

static struct dmi_system_id __initdata sonypi_dmi_table[] = {
	{
		.ident = "Sony Vaio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PCG-"),
		},
	},
	{
		.ident = "Sony Vaio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-"),
		},
	},
	{ }
};

static int __init sony_laptop_init(void)
{
	int result;

	if (!no_spic && dmi_check_system(sonypi_dmi_table)) {
		result = acpi_bus_register_driver(&sony_pic_driver);
		if (result) {
			printk(KERN_ERR DRV_PFX
					"Unable to register SPIC driver.");
			goto out;
		}
	}

	result = acpi_bus_register_driver(&sony_nc_driver);
	if (result) {
		printk(KERN_ERR DRV_PFX "Unable to register SNC driver.");
		goto out_unregister_pic;
	}

	return 0;

out_unregister_pic:
	if (!no_spic)
		acpi_bus_unregister_driver(&sony_pic_driver);
out:
	return result;
}

static void __exit sony_laptop_exit(void)
{
	acpi_bus_unregister_driver(&sony_nc_driver);
	if (!no_spic)
		acpi_bus_unregister_driver(&sony_pic_driver);
}

module_init(sony_laptop_init);
module_exit(sony_laptop_exit);
