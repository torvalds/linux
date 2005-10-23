/*
 * libusual
 *
 * The libusual contains the table of devices common for ub and usb-storage.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/vmalloc.h>

/*
 */
#define USU_MOD_FL_THREAD   1	/* Thread is running */
#define USU_MOD_FL_PRESENT  2	/* The module is loaded */

struct mod_status {
	unsigned long fls;
};

static struct mod_status stat[3];
static DEFINE_SPINLOCK(usu_lock);

/*
 */
#define USB_US_DEFAULT_BIAS	USB_US_TYPE_STOR

#define BIAS_NAME_SIZE  (sizeof("usb-storage"))
static char bias[BIAS_NAME_SIZE];
static int usb_usual_bias;
static const char *bias_names[3] = { "none", "usb-storage", "ub" };

static DECLARE_MUTEX_LOCKED(usu_init_notify);
static DECLARE_COMPLETION(usu_end_notify);
static atomic_t total_threads = ATOMIC_INIT(0);

static int usu_probe_thread(void *arg);
static int parse_bias(const char *bias_s);

/*
 * The table.
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName,useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin,bcdDeviceMax), \
  .driver_info = (flags)|(USB_US_TYPE_STOR<<24) }

#define USUAL_DEV(useProto, useTrans, useType) \
{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, useProto, useTrans), \
  .driver_info = ((useType)<<24) }

struct usb_device_id storage_usb_ids [] = {
#	include "unusual_devs.h"
	{ } /* Terminating entry */
};

#undef USUAL_DEV
#undef UNUSUAL_DEV

MODULE_DEVICE_TABLE(usb, storage_usb_ids);
EXPORT_SYMBOL_GPL(storage_usb_ids);

/*
 * @type: the module type as an integer
 */
void usb_usual_set_present(int type)
{
	struct mod_status *st;
	unsigned long flags;

	if (type <= 0 || type >= 3)
		return;
	st = &stat[type];
	spin_lock_irqsave(&usu_lock, flags);
	st->fls |= USU_MOD_FL_PRESENT;
	spin_unlock_irqrestore(&usu_lock, flags);
}
EXPORT_SYMBOL_GPL(usb_usual_set_present);

void usb_usual_clear_present(int type)
{
	struct mod_status *st;
	unsigned long flags;

	if (type <= 0 || type >= 3)
		return;
	st = &stat[type];
	spin_lock_irqsave(&usu_lock, flags);
	st->fls &= ~USU_MOD_FL_PRESENT;
	spin_unlock_irqrestore(&usu_lock, flags);
}
EXPORT_SYMBOL_GPL(usb_usual_clear_present);

/*
 * Match the calling driver type against the table.
 * Returns: 0 if the device matches.
 */
int usb_usual_check_type(const struct usb_device_id *id, int caller_type)
{
	int id_type = USB_US_TYPE(id->driver_info);

	if (caller_type <= 0 || caller_type >= 3)
		return -EINVAL;

	/* Drivers grab fixed assignment devices */
	if (id_type == caller_type)
		return 0;
	/* Drivers grab devices biased to them */
	if (id_type == USB_US_TYPE_NONE && caller_type == usb_usual_bias)
		return 0;
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(usb_usual_check_type);

/*
 */
static int usu_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	int type;
	int rc;
	unsigned long flags;

	type = USB_US_TYPE(id->driver_info);
	if (type == 0)
		type = usb_usual_bias;

	spin_lock_irqsave(&usu_lock, flags);
	if ((stat[type].fls & (USU_MOD_FL_THREAD|USU_MOD_FL_PRESENT)) != 0) {
		spin_unlock_irqrestore(&usu_lock, flags);
		return -ENXIO;
	}
	stat[type].fls |= USU_MOD_FL_THREAD;
	spin_unlock_irqrestore(&usu_lock, flags);

	rc = kernel_thread(usu_probe_thread, (void*)type, CLONE_VM);
	if (rc < 0) {
		printk(KERN_WARNING "libusual: "
		    "Unable to start the thread for %s: %d\n",
		    bias_names[type], rc);
		spin_lock_irqsave(&usu_lock, flags);
		stat[type].fls &= ~USU_MOD_FL_THREAD;
		spin_unlock_irqrestore(&usu_lock, flags);
		return rc;	/* Not being -ENXIO causes a message printed */
	}
	atomic_inc(&total_threads);

	return -ENXIO;
}

static void usu_disconnect(struct usb_interface *intf)
{
	;	/* We should not be here. */
}

static struct usb_driver usu_driver = {
	.owner =	THIS_MODULE,
	.name =		"libusual",
	.probe =	usu_probe,
	.disconnect =	usu_disconnect,
	.id_table =	storage_usb_ids,
};

/*
 * A whole new thread for a purpose of request_module seems quite stupid.
 * The request_module forks once inside again. However, if we attempt
 * to load a storage module from our own modprobe thread, that module
 * references our symbols, which cannot be resolved until our module is
 * initialized. I wish there was a way to wait for the end of initialization.
 * The module notifier reports MODULE_STATE_COMING only.
 * So, we wait until module->init ends as the next best thing.
 */
static int usu_probe_thread(void *arg)
{
	int type = (unsigned long) arg;
	struct mod_status *st = &stat[type];
	int rc;
	unsigned long flags;

	daemonize("libusual_%d", type);	/* "usb-storage" is kinda too long */

	/* A completion does not work here because it's counted. */
	down(&usu_init_notify);
	up(&usu_init_notify);

	rc = request_module(bias_names[type]);
	spin_lock_irqsave(&usu_lock, flags);
	if (rc == 0 && (st->fls & USU_MOD_FL_PRESENT) == 0) {
		/*
		 * This should not happen, but let us keep tabs on it.
		 */
		printk(KERN_NOTICE "libusual: "
		    "modprobe for %s succeeded, but module is not present\n",
		    bias_names[type]);
	}
	st->fls &= ~USU_MOD_FL_THREAD;
	spin_unlock_irqrestore(&usu_lock, flags);

	complete_and_exit(&usu_end_notify, 0);
}

/*
 */
static int __init usb_usual_init(void)
{
	int rc;

	bias[BIAS_NAME_SIZE-1] = 0;
	usb_usual_bias = parse_bias(bias);

	rc = usb_register(&usu_driver);
	up(&usu_init_notify);
	return rc;
}

static void __exit usb_usual_exit(void)
{
	/*
	 * We do not check for any drivers present, because
	 * they keep us pinned with symbol references.
	 */

	usb_deregister(&usu_driver);

	while (atomic_read(&total_threads) > 0) {
		wait_for_completion(&usu_end_notify);
		atomic_dec(&total_threads);
	}
}

/*
 * Validate and accept the bias parameter.
 * Maybe make an sysfs method later. XXX
 */
static int parse_bias(const char *bias_s)
{
	int i;
	int bias_n = 0;

	if (bias_s[0] == 0 || bias_s[0] == ' ') {
		bias_n = USB_US_DEFAULT_BIAS;
	} else {
		for (i = 1; i < 3; i++) {
			if (strcmp(bias_s, bias_names[i]) == 0) {
				bias_n = i;
				break;
			}
		}
		if (bias_n == 0) {
			bias_n = USB_US_DEFAULT_BIAS;
			printk(KERN_INFO
			    "libusual: unknown bias \"%s\", using \"%s\"\n",
			    bias_s, bias_names[bias_n]);
		}
	}
	return bias_n;
}

module_init(usb_usual_init);
module_exit(usb_usual_exit);

module_param_string(bias, bias, BIAS_NAME_SIZE,  S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(bias, "Bias to usb-storage or ub");

MODULE_LICENSE("GPL");
