/*
 * USB FTDI client driver for Elan Digital Systems's Uxxx adapters
 *
 * Copyright(C) 2006 Elan Digital Systems Limited
 * http://www.elandigitalsystems.com
 *
 * Author and Maintainer - Tony Olech - Elan Digital Systems
 * tony.olech@elandigitalsystems.com
 *
 * This program is free software;you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 *
 * This driver was written by Tony Olech(tony.olech@elandigitalsystems.com)
 * based on various USB client drivers in the 2.6.15 linux kernel
 * with constant reference to the 3rd Edition of Linux Device Drivers
 * published by O'Reilly
 *
 * The U132 adapter is a USB to CardBus adapter specifically designed
 * for PC cards that contain an OHCI host controller. Typical PC cards
 * are the Orange Mobile 3G Option GlobeTrotter Fusion card.
 *
 * The U132 adapter will *NOT *work with PC cards that do not contain
 * an OHCI controller. A simple way to test whether a PC card has an
 * OHCI controller as an interface is to insert the PC card directly
 * into a laptop(or desktop) with a CardBus slot and if "lspci" shows
 * a new USB controller and "lsusb -v" shows a new OHCI Host Controller
 * then there is a good chance that the U132 adapter will support the
 * PC card.(you also need the specific client driver for the PC card)
 *
 * Please inform the Author and Maintainer about any PC cards that
 * contain OHCI Host Controller and work when directly connected to
 * an embedded CardBus slot but do not work when they are connected
 * via an ELAN U132 adapter.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/ioctl.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
MODULE_AUTHOR("Tony Olech");
MODULE_DESCRIPTION("FTDI ELAN driver");
MODULE_LICENSE("GPL");
#define INT_MODULE_PARM(n, v) static int n = v;module_param(n, int, 0444)
static bool distrust_firmware = 1;
module_param(distrust_firmware, bool, 0);
MODULE_PARM_DESC(distrust_firmware,
		 "true to distrust firmware power/overcurrent setup");
extern struct platform_driver u132_platform_driver;
/*
 * ftdi_module_lock exists to protect access to global variables
 *
 */
static struct mutex ftdi_module_lock;
static int ftdi_instances = 0;
static struct list_head ftdi_static_list;
/*
 * end of the global variables protected by ftdi_module_lock
 */
#include "usb_u132.h"
#include <asm/io.h>
#include <linux/usb/hcd.h>

/* FIXME ohci.h is ONLY for internal use by the OHCI driver.
 * If you're going to try stuff like this, you need to split
 * out shareable stuff (register declarations?) into its own
 * file, maybe name <linux/usb/ohci.h>
 */

#include "../host/ohci.h"
/* Define these values to match your devices*/
#define USB_FTDI_ELAN_VENDOR_ID 0x0403
#define USB_FTDI_ELAN_PRODUCT_ID 0xd6ea
/* table of devices that work with this driver*/
static const struct usb_device_id ftdi_elan_table[] = {
	{USB_DEVICE(USB_FTDI_ELAN_VENDOR_ID, USB_FTDI_ELAN_PRODUCT_ID)},
	{ /* Terminating entry */ }
};

MODULE_DEVICE_TABLE(usb, ftdi_elan_table);
/* only the jtag(firmware upgrade device) interface requires
 * a device file and corresponding minor number, but the
 * interface is created unconditionally - I suppose it could
 * be configured or not according to a module parameter.
 * But since we(now) require one interface per device,
 * and since it unlikely that a normal installation would
 * require more than a couple of elan-ftdi devices, 8 seems
 * like a reasonable limit to have here, and if someone
 * really requires more than 8 devices, then they can frig the
 * code and recompile
 */
#define USB_FTDI_ELAN_MINOR_BASE 192
#define COMMAND_BITS 5
#define COMMAND_SIZE (1<<COMMAND_BITS)
#define COMMAND_MASK (COMMAND_SIZE-1)
struct u132_command {
	u8 header;
	u16 length;
	u8 address;
	u8 width;
	u32 value;
	int follows;
	void *buffer;
};
#define RESPOND_BITS 5
#define RESPOND_SIZE (1<<RESPOND_BITS)
#define RESPOND_MASK (RESPOND_SIZE-1)
struct u132_respond {
	u8 header;
	u8 address;
	u32 *value;
	int *result;
	struct completion wait_completion;
};
struct u132_target {
	void *endp;
	struct urb *urb;
	int toggle_bits;
	int error_count;
	int condition_code;
	int repeat_number;
	int halted;
	int skipped;
	int actual;
	int non_null;
	int active;
	int abandoning;
	void (*callback)(void *endp, struct urb *urb, u8 *buf, int len,
			 int toggle_bits, int error_count, int condition_code,
			 int repeat_number, int halted, int skipped, int actual,
			 int non_null);
};
/* Structure to hold all of our device specific stuff*/
struct usb_ftdi {
	struct list_head ftdi_list;
	struct mutex u132_lock;
	int command_next;
	int command_head;
	struct u132_command command[COMMAND_SIZE];
	int respond_next;
	int respond_head;
	struct u132_respond respond[RESPOND_SIZE];
	struct u132_target target[4];
	char device_name[16];
	unsigned synchronized:1;
	unsigned enumerated:1;
	unsigned registered:1;
	unsigned initialized:1;
	unsigned card_ejected:1;
	int function;
	int sequence_num;
	int disconnected;
	int gone_away;
	int stuck_status;
	int status_queue_delay;
	struct semaphore sw_lock;
	struct usb_device *udev;
	struct usb_interface *interface;
	struct usb_class_driver *class;
	struct delayed_work status_work;
	struct delayed_work command_work;
	struct delayed_work respond_work;
	struct u132_platform_data platform_data;
	struct resource resources[0];
	struct platform_device platform_dev;
	unsigned char *bulk_in_buffer;
	size_t bulk_in_size;
	size_t bulk_in_last;
	size_t bulk_in_left;
	__u8 bulk_in_endpointAddr;
	__u8 bulk_out_endpointAddr;
	struct kref kref;
	u32 controlreg;
	u8 response[4 + 1024];
	int expected;
	int received;
	int ed_found;
};
#define kref_to_usb_ftdi(d) container_of(d, struct usb_ftdi, kref)
#define platform_device_to_usb_ftdi(d) container_of(d, struct usb_ftdi, \
						    platform_dev)
static struct usb_driver ftdi_elan_driver;
static void ftdi_elan_delete(struct kref *kref)
{
	struct usb_ftdi *ftdi = kref_to_usb_ftdi(kref);
	dev_warn(&ftdi->udev->dev, "FREEING ftdi=%p\n", ftdi);
	usb_put_dev(ftdi->udev);
	ftdi->disconnected += 1;
	mutex_lock(&ftdi_module_lock);
	list_del_init(&ftdi->ftdi_list);
	ftdi_instances -= 1;
	mutex_unlock(&ftdi_module_lock);
	kfree(ftdi->bulk_in_buffer);
	ftdi->bulk_in_buffer = NULL;
}

static void ftdi_elan_put_kref(struct usb_ftdi *ftdi)
{
	kref_put(&ftdi->kref, ftdi_elan_delete);
}

static void ftdi_elan_get_kref(struct usb_ftdi *ftdi)
{
	kref_get(&ftdi->kref);
}

static void ftdi_elan_init_kref(struct usb_ftdi *ftdi)
{
	kref_init(&ftdi->kref);
}

static void ftdi_status_requeue_work(struct usb_ftdi *ftdi, unsigned int delta)
{
	if (!schedule_delayed_work(&ftdi->status_work, delta))
		kref_put(&ftdi->kref, ftdi_elan_delete);
}

static void ftdi_status_queue_work(struct usb_ftdi *ftdi, unsigned int delta)
{
	if (schedule_delayed_work(&ftdi->status_work, delta))
		kref_get(&ftdi->kref);
}

static void ftdi_status_cancel_work(struct usb_ftdi *ftdi)
{
	if (cancel_delayed_work_sync(&ftdi->status_work))
		kref_put(&ftdi->kref, ftdi_elan_delete);
}

static void ftdi_command_requeue_work(struct usb_ftdi *ftdi, unsigned int delta)
{
	if (!schedule_delayed_work(&ftdi->command_work, delta))
		kref_put(&ftdi->kref, ftdi_elan_delete);
}

static void ftdi_command_queue_work(struct usb_ftdi *ftdi, unsigned int delta)
{
	if (schedule_delayed_work(&ftdi->command_work, delta))
		kref_get(&ftdi->kref);
}

static void ftdi_command_cancel_work(struct usb_ftdi *ftdi)
{
	if (cancel_delayed_work_sync(&ftdi->command_work))
		kref_put(&ftdi->kref, ftdi_elan_delete);
}

static void ftdi_response_requeue_work(struct usb_ftdi *ftdi,
				       unsigned int delta)
{
	if (!schedule_delayed_work(&ftdi->respond_work, delta))
		kref_put(&ftdi->kref, ftdi_elan_delete);
}

static void ftdi_respond_queue_work(struct usb_ftdi *ftdi, unsigned int delta)
{
	if (schedule_delayed_work(&ftdi->respond_work, delta))
		kref_get(&ftdi->kref);
}

static void ftdi_response_cancel_work(struct usb_ftdi *ftdi)
{
	if (cancel_delayed_work_sync(&ftdi->respond_work))
		kref_put(&ftdi->kref, ftdi_elan_delete);
}

void ftdi_elan_gone_away(struct platform_device *pdev)
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	ftdi->gone_away += 1;
	ftdi_elan_put_kref(ftdi);
}


EXPORT_SYMBOL_GPL(ftdi_elan_gone_away);
static void ftdi_release_platform_dev(struct device *dev)
{
	dev->parent = NULL;
}

static void ftdi_elan_do_callback(struct usb_ftdi *ftdi,
				  struct u132_target *target, u8 *buffer, int length);
static void ftdi_elan_kick_command_queue(struct usb_ftdi *ftdi);
static void ftdi_elan_kick_respond_queue(struct usb_ftdi *ftdi);
static int ftdi_elan_setupOHCI(struct usb_ftdi *ftdi);
static int ftdi_elan_checkingPCI(struct usb_ftdi *ftdi);
static int ftdi_elan_enumeratePCI(struct usb_ftdi *ftdi);
static int ftdi_elan_synchronize(struct usb_ftdi *ftdi);
static int ftdi_elan_stuck_waiting(struct usb_ftdi *ftdi);
static int ftdi_elan_command_engine(struct usb_ftdi *ftdi);
static int ftdi_elan_respond_engine(struct usb_ftdi *ftdi);
static int ftdi_elan_hcd_init(struct usb_ftdi *ftdi)
{
	int result;
	if (ftdi->platform_dev.dev.parent)
		return -EBUSY;
	ftdi_elan_get_kref(ftdi);
	ftdi->platform_data.potpg = 100;
	ftdi->platform_data.reset = NULL;
	ftdi->platform_dev.id = ftdi->sequence_num;
	ftdi->platform_dev.resource = ftdi->resources;
	ftdi->platform_dev.num_resources = ARRAY_SIZE(ftdi->resources);
	ftdi->platform_dev.dev.platform_data = &ftdi->platform_data;
	ftdi->platform_dev.dev.parent = NULL;
	ftdi->platform_dev.dev.release = ftdi_release_platform_dev;
	ftdi->platform_dev.dev.dma_mask = NULL;
	snprintf(ftdi->device_name, sizeof(ftdi->device_name), "u132_hcd");
	ftdi->platform_dev.name = ftdi->device_name;
	dev_info(&ftdi->udev->dev, "requesting module '%s'\n", "u132_hcd");
	request_module("u132_hcd");
	dev_info(&ftdi->udev->dev, "registering '%s'\n",
		 ftdi->platform_dev.name);
	result = platform_device_register(&ftdi->platform_dev);
	return result;
}

static void ftdi_elan_abandon_completions(struct usb_ftdi *ftdi)
{
	mutex_lock(&ftdi->u132_lock);
	while (ftdi->respond_next > ftdi->respond_head) {
		struct u132_respond *respond = &ftdi->respond[RESPOND_MASK &
							      ftdi->respond_head++];
		*respond->result = -ESHUTDOWN;
		*respond->value = 0;
		complete(&respond->wait_completion);
	} mutex_unlock(&ftdi->u132_lock);
}

static void ftdi_elan_abandon_targets(struct usb_ftdi *ftdi)
{
	int ed_number = 4;
	mutex_lock(&ftdi->u132_lock);
	while (ed_number-- > 0) {
		struct u132_target *target = &ftdi->target[ed_number];
		if (target->active == 1) {
			target->condition_code = TD_DEVNOTRESP;
			mutex_unlock(&ftdi->u132_lock);
			ftdi_elan_do_callback(ftdi, target, NULL, 0);
			mutex_lock(&ftdi->u132_lock);
		}
	}
	ftdi->received = 0;
	ftdi->expected = 4;
	ftdi->ed_found = 0;
	mutex_unlock(&ftdi->u132_lock);
}

static void ftdi_elan_flush_targets(struct usb_ftdi *ftdi)
{
	int ed_number = 4;
	mutex_lock(&ftdi->u132_lock);
	while (ed_number-- > 0) {
		struct u132_target *target = &ftdi->target[ed_number];
		target->abandoning = 1;
	wait_1:if (target->active == 1) {
			int command_size = ftdi->command_next -
				ftdi->command_head;
			if (command_size < COMMAND_SIZE) {
				struct u132_command *command = &ftdi->command[
					COMMAND_MASK & ftdi->command_next];
				command->header = 0x80 | (ed_number << 5) | 0x4;
				command->length = 0x00;
				command->address = 0x00;
				command->width = 0x00;
				command->follows = 0;
				command->value = 0;
				command->buffer = &command->value;
				ftdi->command_next += 1;
				ftdi_elan_kick_command_queue(ftdi);
			} else {
				mutex_unlock(&ftdi->u132_lock);
				msleep(100);
				mutex_lock(&ftdi->u132_lock);
				goto wait_1;
			}
		}
	wait_2:if (target->active == 1) {
			int command_size = ftdi->command_next -
				ftdi->command_head;
			if (command_size < COMMAND_SIZE) {
				struct u132_command *command = &ftdi->command[
					COMMAND_MASK & ftdi->command_next];
				command->header = 0x90 | (ed_number << 5);
				command->length = 0x00;
				command->address = 0x00;
				command->width = 0x00;
				command->follows = 0;
				command->value = 0;
				command->buffer = &command->value;
				ftdi->command_next += 1;
				ftdi_elan_kick_command_queue(ftdi);
			} else {
				mutex_unlock(&ftdi->u132_lock);
				msleep(100);
				mutex_lock(&ftdi->u132_lock);
				goto wait_2;
			}
		}
	}
	ftdi->received = 0;
	ftdi->expected = 4;
	ftdi->ed_found = 0;
	mutex_unlock(&ftdi->u132_lock);
}

static void ftdi_elan_cancel_targets(struct usb_ftdi *ftdi)
{
	int ed_number = 4;
	mutex_lock(&ftdi->u132_lock);
	while (ed_number-- > 0) {
		struct u132_target *target = &ftdi->target[ed_number];
		target->abandoning = 1;
	wait:if (target->active == 1) {
			int command_size = ftdi->command_next -
				ftdi->command_head;
			if (command_size < COMMAND_SIZE) {
				struct u132_command *command = &ftdi->command[
					COMMAND_MASK & ftdi->command_next];
				command->header = 0x80 | (ed_number << 5) | 0x4;
				command->length = 0x00;
				command->address = 0x00;
				command->width = 0x00;
				command->follows = 0;
				command->value = 0;
				command->buffer = &command->value;
				ftdi->command_next += 1;
				ftdi_elan_kick_command_queue(ftdi);
			} else {
				mutex_unlock(&ftdi->u132_lock);
				msleep(100);
				mutex_lock(&ftdi->u132_lock);
				goto wait;
			}
		}
	}
	ftdi->received = 0;
	ftdi->expected = 4;
	ftdi->ed_found = 0;
	mutex_unlock(&ftdi->u132_lock);
}

static void ftdi_elan_kick_command_queue(struct usb_ftdi *ftdi)
{
	ftdi_command_queue_work(ftdi, 0);
}

static void ftdi_elan_command_work(struct work_struct *work)
{
	struct usb_ftdi *ftdi =
		container_of(work, struct usb_ftdi, command_work.work);

	if (ftdi->disconnected > 0) {
		ftdi_elan_put_kref(ftdi);
		return;
	} else {
		int retval = ftdi_elan_command_engine(ftdi);
		if (retval == -ESHUTDOWN) {
			ftdi->disconnected += 1;
		} else if (retval == -ENODEV) {
			ftdi->disconnected += 1;
		} else if (retval)
			dev_err(&ftdi->udev->dev, "command error %d\n", retval);
		ftdi_command_requeue_work(ftdi, msecs_to_jiffies(10));
		return;
	}
}

static void ftdi_elan_kick_respond_queue(struct usb_ftdi *ftdi)
{
	ftdi_respond_queue_work(ftdi, 0);
}

static void ftdi_elan_respond_work(struct work_struct *work)
{
	struct usb_ftdi *ftdi =
		container_of(work, struct usb_ftdi, respond_work.work);
	if (ftdi->disconnected > 0) {
		ftdi_elan_put_kref(ftdi);
		return;
	} else {
		int retval = ftdi_elan_respond_engine(ftdi);
		if (retval == 0) {
		} else if (retval == -ESHUTDOWN) {
			ftdi->disconnected += 1;
		} else if (retval == -ENODEV) {
			ftdi->disconnected += 1;
		} else if (retval == -EILSEQ) {
			ftdi->disconnected += 1;
		} else {
			ftdi->disconnected += 1;
			dev_err(&ftdi->udev->dev, "respond error %d\n", retval);
		}
		if (ftdi->disconnected > 0) {
			ftdi_elan_abandon_completions(ftdi);
			ftdi_elan_abandon_targets(ftdi);
		}
		ftdi_response_requeue_work(ftdi, msecs_to_jiffies(10));
		return;
	}
}


/*
 * the sw_lock is initially held and will be freed
 * after the FTDI has been synchronized
 *
 */
static void ftdi_elan_status_work(struct work_struct *work)
{
	struct usb_ftdi *ftdi =
		container_of(work, struct usb_ftdi, status_work.work);
	int work_delay_in_msec = 0;
	if (ftdi->disconnected > 0) {
		ftdi_elan_put_kref(ftdi);
		return;
	} else if (ftdi->synchronized == 0) {
		down(&ftdi->sw_lock);
		if (ftdi_elan_synchronize(ftdi) == 0) {
			ftdi->synchronized = 1;
			ftdi_command_queue_work(ftdi, 1);
			ftdi_respond_queue_work(ftdi, 1);
			up(&ftdi->sw_lock);
			work_delay_in_msec = 100;
		} else {
			dev_err(&ftdi->udev->dev, "synchronize failed\n");
			up(&ftdi->sw_lock);
			work_delay_in_msec = 10 *1000;
		}
	} else if (ftdi->stuck_status > 0) {
		if (ftdi_elan_stuck_waiting(ftdi) == 0) {
			ftdi->stuck_status = 0;
			ftdi->synchronized = 0;
		} else if ((ftdi->stuck_status++ % 60) == 1) {
			dev_err(&ftdi->udev->dev, "WRONG type of card inserted - please remove\n");
		} else
			dev_err(&ftdi->udev->dev, "WRONG type of card inserted - checked %d times\n",
				ftdi->stuck_status);
		work_delay_in_msec = 100;
	} else if (ftdi->enumerated == 0) {
		if (ftdi_elan_enumeratePCI(ftdi) == 0) {
			ftdi->enumerated = 1;
			work_delay_in_msec = 250;
		} else
			work_delay_in_msec = 1000;
	} else if (ftdi->initialized == 0) {
		if (ftdi_elan_setupOHCI(ftdi) == 0) {
			ftdi->initialized = 1;
			work_delay_in_msec = 500;
		} else {
			dev_err(&ftdi->udev->dev, "initialized failed - trying again in 10 seconds\n");
			work_delay_in_msec = 1 *1000;
		}
	} else if (ftdi->registered == 0) {
		work_delay_in_msec = 10;
		if (ftdi_elan_hcd_init(ftdi) == 0) {
			ftdi->registered = 1;
		} else
			dev_err(&ftdi->udev->dev, "register failed\n");
		work_delay_in_msec = 250;
	} else {
		if (ftdi_elan_checkingPCI(ftdi) == 0) {
			work_delay_in_msec = 250;
		} else if (ftdi->controlreg & 0x00400000) {
			if (ftdi->gone_away > 0) {
				dev_err(&ftdi->udev->dev, "PCI device eject confirmed platform_dev.dev.parent=%p platform_dev.dev=%p\n",
					ftdi->platform_dev.dev.parent,
					&ftdi->platform_dev.dev);
				platform_device_unregister(&ftdi->platform_dev);
				ftdi->platform_dev.dev.parent = NULL;
				ftdi->registered = 0;
				ftdi->enumerated = 0;
				ftdi->card_ejected = 0;
				ftdi->initialized = 0;
				ftdi->gone_away = 0;
			} else
				ftdi_elan_flush_targets(ftdi);
			work_delay_in_msec = 250;
		} else {
			dev_err(&ftdi->udev->dev, "PCI device has disappeared\n");
			ftdi_elan_cancel_targets(ftdi);
			work_delay_in_msec = 500;
			ftdi->enumerated = 0;
			ftdi->initialized = 0;
		}
	}
	if (ftdi->disconnected > 0) {
		ftdi_elan_put_kref(ftdi);
		return;
	} else {
		ftdi_status_requeue_work(ftdi,
					 msecs_to_jiffies(work_delay_in_msec));
		return;
	}
}


/*
 * file_operations for the jtag interface
 *
 * the usage count for the device is incremented on open()
 * and decremented on release()
 */
static int ftdi_elan_open(struct inode *inode, struct file *file)
{
	int subminor;
	struct usb_interface *interface;

	subminor = iminor(inode);
	interface = usb_find_interface(&ftdi_elan_driver, subminor);

	if (!interface) {
		pr_err("can't find device for minor %d\n", subminor);
		return -ENODEV;
	} else {
		struct usb_ftdi *ftdi = usb_get_intfdata(interface);
		if (!ftdi) {
			return -ENODEV;
		} else {
			if (down_interruptible(&ftdi->sw_lock)) {
				return -EINTR;
			} else {
				ftdi_elan_get_kref(ftdi);
				file->private_data = ftdi;
				return 0;
			}
		}
	}
}

static int ftdi_elan_release(struct inode *inode, struct file *file)
{
	struct usb_ftdi *ftdi = file->private_data;
	if (ftdi == NULL)
		return -ENODEV;
	up(&ftdi->sw_lock);        /* decrement the count on our device */
	ftdi_elan_put_kref(ftdi);
	return 0;
}


/*
 *
 * blocking bulk reads are used to get data from the device
 *
 */
static ssize_t ftdi_elan_read(struct file *file, char __user *buffer,
			      size_t count, loff_t *ppos)
{
	char data[30 *3 + 4];
	char *d = data;
	int m = (sizeof(data) - 1) / 3 - 1;
	int bytes_read = 0;
	int retry_on_empty = 10;
	int retry_on_timeout = 5;
	struct usb_ftdi *ftdi = file->private_data;
	if (ftdi->disconnected > 0) {
		return -ENODEV;
	}
	data[0] = 0;
have:if (ftdi->bulk_in_left > 0) {
		if (count-- > 0) {
			char *p = ++ftdi->bulk_in_last + ftdi->bulk_in_buffer;
			ftdi->bulk_in_left -= 1;
			if (bytes_read < m) {
				d += sprintf(d, " %02X", 0x000000FF & *p);
			} else if (bytes_read > m) {
			} else
				d += sprintf(d, " ..");
			if (copy_to_user(buffer++, p, 1)) {
				return -EFAULT;
			} else {
				bytes_read += 1;
				goto have;
			}
		} else
			return bytes_read;
	}
more:if (count > 0) {
		int packet_bytes = 0;
		int retval = usb_bulk_msg(ftdi->udev,
					  usb_rcvbulkpipe(ftdi->udev, ftdi->bulk_in_endpointAddr),
					  ftdi->bulk_in_buffer, ftdi->bulk_in_size,
					  &packet_bytes, 50);
		if (packet_bytes > 2) {
			ftdi->bulk_in_left = packet_bytes - 2;
			ftdi->bulk_in_last = 1;
			goto have;
		} else if (retval == -ETIMEDOUT) {
			if (retry_on_timeout-- > 0) {
				goto more;
			} else if (bytes_read > 0) {
				return bytes_read;
			} else
				return retval;
		} else if (retval == 0) {
			if (retry_on_empty-- > 0) {
				goto more;
			} else
				return bytes_read;
		} else
			return retval;
	} else
		return bytes_read;
}

static void ftdi_elan_write_bulk_callback(struct urb *urb)
{
	struct usb_ftdi *ftdi = urb->context;
	int status = urb->status;

	if (status && !(status == -ENOENT || status == -ECONNRESET ||
			status == -ESHUTDOWN)) {
		dev_err(&ftdi->udev->dev,
			"urb=%p write bulk status received: %d\n", urb, status);
	}
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
}

static int fill_buffer_with_all_queued_commands(struct usb_ftdi *ftdi,
						char *buf, int command_size, int total_size)
{
	int ed_commands = 0;
	int b = 0;
	int I = command_size;
	int i = ftdi->command_head;
	while (I-- > 0) {
		struct u132_command *command = &ftdi->command[COMMAND_MASK &
							      i++];
		int F = command->follows;
		u8 *f = command->buffer;
		if (command->header & 0x80) {
			ed_commands |= 1 << (0x3 & (command->header >> 5));
		}
		buf[b++] = command->header;
		buf[b++] = (command->length >> 0) & 0x00FF;
		buf[b++] = (command->length >> 8) & 0x00FF;
		buf[b++] = command->address;
		buf[b++] = command->width;
		while (F-- > 0) {
			buf[b++] = *f++;
		}
	}
	return ed_commands;
}

static int ftdi_elan_total_command_size(struct usb_ftdi *ftdi, int command_size)
{
	int total_size = 0;
	int I = command_size;
	int i = ftdi->command_head;
	while (I-- > 0) {
		struct u132_command *command = &ftdi->command[COMMAND_MASK &
							      i++];
		total_size += 5 + command->follows;
	} return total_size;
}

static int ftdi_elan_command_engine(struct usb_ftdi *ftdi)
{
	int retval;
	char *buf;
	int ed_commands;
	int total_size;
	struct urb *urb;
	int command_size = ftdi->command_next - ftdi->command_head;
	if (command_size == 0)
		return 0;
	total_size = ftdi_elan_total_command_size(ftdi, command_size);
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;
	buf = usb_alloc_coherent(ftdi->udev, total_size, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!buf) {
		dev_err(&ftdi->udev->dev, "could not get a buffer to write %d commands totaling %d bytes to the Uxxx\n",
			command_size, total_size);
		usb_free_urb(urb);
		return -ENOMEM;
	}
	ed_commands = fill_buffer_with_all_queued_commands(ftdi, buf,
							   command_size, total_size);
	usb_fill_bulk_urb(urb, ftdi->udev, usb_sndbulkpipe(ftdi->udev,
							   ftdi->bulk_out_endpointAddr), buf, total_size,
			  ftdi_elan_write_bulk_callback, ftdi);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	if (ed_commands) {
		char diag[40 *3 + 4];
		char *d = diag;
		int m = total_size;
		u8 *c = buf;
		int s = (sizeof(diag) - 1) / 3;
		diag[0] = 0;
		while (s-- > 0 && m-- > 0) {
			if (s > 0 || m == 0) {
				d += sprintf(d, " %02X", *c++);
			} else
				d += sprintf(d, " ..");
		}
	}
	retval = usb_submit_urb(urb, GFP_KERNEL);
	if (retval) {
		dev_err(&ftdi->udev->dev, "failed %d to submit urb %p to write %d commands totaling %d bytes to the Uxxx\n",
			retval, urb, command_size, total_size);
		usb_free_coherent(ftdi->udev, total_size, buf, urb->transfer_dma);
		usb_free_urb(urb);
		return retval;
	}
	usb_free_urb(urb);        /* release our reference to this urb,
				     the USB core will eventually free it entirely */
	ftdi->command_head += command_size;
	ftdi_elan_kick_respond_queue(ftdi);
	return 0;
}

static void ftdi_elan_do_callback(struct usb_ftdi *ftdi,
				  struct u132_target *target, u8 *buffer, int length)
{
	struct urb *urb = target->urb;
	int halted = target->halted;
	int skipped = target->skipped;
	int actual = target->actual;
	int non_null = target->non_null;
	int toggle_bits = target->toggle_bits;
	int error_count = target->error_count;
	int condition_code = target->condition_code;
	int repeat_number = target->repeat_number;
	void (*callback) (void *, struct urb *, u8 *, int, int, int, int, int,
			  int, int, int, int) = target->callback;
	target->active -= 1;
	target->callback = NULL;
	(*callback) (target->endp, urb, buffer, length, toggle_bits,
		     error_count, condition_code, repeat_number, halted, skipped,
		     actual, non_null);
}

static char *have_ed_set_response(struct usb_ftdi *ftdi,
				  struct u132_target *target, u16 ed_length, int ed_number, int ed_type,
				  char *b)
{
	int payload = (ed_length >> 0) & 0x07FF;
	mutex_lock(&ftdi->u132_lock);
	target->actual = 0;
	target->non_null = (ed_length >> 15) & 0x0001;
	target->repeat_number = (ed_length >> 11) & 0x000F;
	if (ed_type == 0x02) {
		if (payload == 0 || target->abandoning > 0) {
			target->abandoning = 0;
			mutex_unlock(&ftdi->u132_lock);
			ftdi_elan_do_callback(ftdi, target, 4 + ftdi->response,
					      payload);
			ftdi->received = 0;
			ftdi->expected = 4;
			ftdi->ed_found = 0;
			return ftdi->response;
		} else {
			ftdi->expected = 4 + payload;
			ftdi->ed_found = 1;
			mutex_unlock(&ftdi->u132_lock);
			return b;
		}
	} else if (ed_type == 0x03) {
		if (payload == 0 || target->abandoning > 0) {
			target->abandoning = 0;
			mutex_unlock(&ftdi->u132_lock);
			ftdi_elan_do_callback(ftdi, target, 4 + ftdi->response,
					      payload);
			ftdi->received = 0;
			ftdi->expected = 4;
			ftdi->ed_found = 0;
			return ftdi->response;
		} else {
			ftdi->expected = 4 + payload;
			ftdi->ed_found = 1;
			mutex_unlock(&ftdi->u132_lock);
			return b;
		}
	} else if (ed_type == 0x01) {
		target->abandoning = 0;
		mutex_unlock(&ftdi->u132_lock);
		ftdi_elan_do_callback(ftdi, target, 4 + ftdi->response,
				      payload);
		ftdi->received = 0;
		ftdi->expected = 4;
		ftdi->ed_found = 0;
		return ftdi->response;
	} else {
		target->abandoning = 0;
		mutex_unlock(&ftdi->u132_lock);
		ftdi_elan_do_callback(ftdi, target, 4 + ftdi->response,
				      payload);
		ftdi->received = 0;
		ftdi->expected = 4;
		ftdi->ed_found = 0;
		return ftdi->response;
	}
}

static char *have_ed_get_response(struct usb_ftdi *ftdi,
				  struct u132_target *target, u16 ed_length, int ed_number, int ed_type,
				  char *b)
{
	mutex_lock(&ftdi->u132_lock);
	target->condition_code = TD_DEVNOTRESP;
	target->actual = (ed_length >> 0) & 0x01FF;
	target->non_null = (ed_length >> 15) & 0x0001;
	target->repeat_number = (ed_length >> 11) & 0x000F;
	mutex_unlock(&ftdi->u132_lock);
	if (target->active)
		ftdi_elan_do_callback(ftdi, target, NULL, 0);
	target->abandoning = 0;
	ftdi->received = 0;
	ftdi->expected = 4;
	ftdi->ed_found = 0;
	return ftdi->response;
}


/*
 * The engine tries to empty the FTDI fifo
 *
 * all responses found in the fifo data are dispatched thus
 * the response buffer can only ever hold a maximum sized
 * response from the Uxxx.
 *
 */
static int ftdi_elan_respond_engine(struct usb_ftdi *ftdi)
{
	u8 *b = ftdi->response + ftdi->received;
	int bytes_read = 0;
	int retry_on_empty = 1;
	int retry_on_timeout = 3;
	int empty_packets = 0;
read:{
		int packet_bytes = 0;
		int retval = usb_bulk_msg(ftdi->udev,
					  usb_rcvbulkpipe(ftdi->udev, ftdi->bulk_in_endpointAddr),
					  ftdi->bulk_in_buffer, ftdi->bulk_in_size,
					  &packet_bytes, 500);
		char diag[30 *3 + 4];
		char *d = diag;
		int m = packet_bytes;
		u8 *c = ftdi->bulk_in_buffer;
		int s = (sizeof(diag) - 1) / 3;
		diag[0] = 0;
		while (s-- > 0 && m-- > 0) {
			if (s > 0 || m == 0) {
				d += sprintf(d, " %02X", *c++);
			} else
				d += sprintf(d, " ..");
		}
		if (packet_bytes > 2) {
			ftdi->bulk_in_left = packet_bytes - 2;
			ftdi->bulk_in_last = 1;
			goto have;
		} else if (retval == -ETIMEDOUT) {
			if (retry_on_timeout-- > 0) {
				dev_err(&ftdi->udev->dev, "TIMED OUT with packet_bytes = %d with total %d bytes%s\n",
					packet_bytes, bytes_read, diag);
				goto more;
			} else if (bytes_read > 0) {
				dev_err(&ftdi->udev->dev, "ONLY %d bytes%s\n",
					bytes_read, diag);
				return -ENOMEM;
			} else {
				dev_err(&ftdi->udev->dev, "TIMED OUT with packet_bytes = %d with total %d bytes%s\n",
					packet_bytes, bytes_read, diag);
				return -ENOMEM;
			}
		} else if (retval == -EILSEQ) {
			dev_err(&ftdi->udev->dev, "error = %d with packet_bytes = %d with total %d bytes%s\n",
				retval, packet_bytes, bytes_read, diag);
			return retval;
		} else if (retval) {
			dev_err(&ftdi->udev->dev, "error = %d with packet_bytes = %d with total %d bytes%s\n",
				retval, packet_bytes, bytes_read, diag);
			return retval;
		} else if (packet_bytes == 2) {
			unsigned char s0 = ftdi->bulk_in_buffer[0];
			unsigned char s1 = ftdi->bulk_in_buffer[1];
			empty_packets += 1;
			if (s0 == 0x31 && s1 == 0x60) {
				if (retry_on_empty-- > 0) {
					goto more;
				} else
					return 0;
			} else if (s0 == 0x31 && s1 == 0x00) {
				if (retry_on_empty-- > 0) {
					goto more;
				} else
					return 0;
			} else {
				if (retry_on_empty-- > 0) {
					goto more;
				} else
					return 0;
			}
		} else if (packet_bytes == 1) {
			if (retry_on_empty-- > 0) {
				goto more;
			} else
				return 0;
		} else {
			if (retry_on_empty-- > 0) {
				goto more;
			} else
				return 0;
		}
	}
more:{
		goto read;
	}
have:if (ftdi->bulk_in_left > 0) {
		u8 c = ftdi->bulk_in_buffer[++ftdi->bulk_in_last];
		bytes_read += 1;
		ftdi->bulk_in_left -= 1;
		if (ftdi->received == 0 && c == 0xFF) {
			goto have;
		} else
			*b++ = c;
		if (++ftdi->received < ftdi->expected) {
			goto have;
		} else if (ftdi->ed_found) {
			int ed_number = (ftdi->response[0] >> 5) & 0x03;
			u16 ed_length = (ftdi->response[2] << 8) |
				ftdi->response[1];
			struct u132_target *target = &ftdi->target[ed_number];
			int payload = (ed_length >> 0) & 0x07FF;
			char diag[30 *3 + 4];
			char *d = diag;
			int m = payload;
			u8 *c = 4 + ftdi->response;
			int s = (sizeof(diag) - 1) / 3;
			diag[0] = 0;
			while (s-- > 0 && m-- > 0) {
				if (s > 0 || m == 0) {
					d += sprintf(d, " %02X", *c++);
				} else
					d += sprintf(d, " ..");
			}
			ftdi_elan_do_callback(ftdi, target, 4 + ftdi->response,
					      payload);
			ftdi->received = 0;
			ftdi->expected = 4;
			ftdi->ed_found = 0;
			b = ftdi->response;
			goto have;
		} else if (ftdi->expected == 8) {
			u8 buscmd;
			int respond_head = ftdi->respond_head++;
			struct u132_respond *respond = &ftdi->respond[
				RESPOND_MASK & respond_head];
			u32 data = ftdi->response[7];
			data <<= 8;
			data |= ftdi->response[6];
			data <<= 8;
			data |= ftdi->response[5];
			data <<= 8;
			data |= ftdi->response[4];
			*respond->value = data;
			*respond->result = 0;
			complete(&respond->wait_completion);
			ftdi->received = 0;
			ftdi->expected = 4;
			ftdi->ed_found = 0;
			b = ftdi->response;
			buscmd = (ftdi->response[0] >> 0) & 0x0F;
			if (buscmd == 0x00) {
			} else if (buscmd == 0x02) {
			} else if (buscmd == 0x06) {
			} else if (buscmd == 0x0A) {
			} else
				dev_err(&ftdi->udev->dev, "Uxxx unknown(%0X) value = %08X\n",
					buscmd, data);
			goto have;
		} else {
			if ((ftdi->response[0] & 0x80) == 0x00) {
				ftdi->expected = 8;
				goto have;
			} else {
				int ed_number = (ftdi->response[0] >> 5) & 0x03;
				int ed_type = (ftdi->response[0] >> 0) & 0x03;
				u16 ed_length = (ftdi->response[2] << 8) |
					ftdi->response[1];
				struct u132_target *target = &ftdi->target[
					ed_number];
				target->halted = (ftdi->response[0] >> 3) &
					0x01;
				target->skipped = (ftdi->response[0] >> 2) &
					0x01;
				target->toggle_bits = (ftdi->response[3] >> 6)
					& 0x03;
				target->error_count = (ftdi->response[3] >> 4)
					& 0x03;
				target->condition_code = (ftdi->response[
								  3] >> 0) & 0x0F;
				if ((ftdi->response[0] & 0x10) == 0x00) {
					b = have_ed_set_response(ftdi, target,
								 ed_length, ed_number, ed_type,
								 b);
					goto have;
				} else {
					b = have_ed_get_response(ftdi, target,
								 ed_length, ed_number, ed_type,
								 b);
					goto have;
				}
			}
		}
	} else
		goto more;
}


/*
 * create a urb, and a buffer for it, and copy the data to the urb
 *
 */
static ssize_t ftdi_elan_write(struct file *file,
			       const char __user *user_buffer, size_t count,
			       loff_t *ppos)
{
	int retval = 0;
	struct urb *urb;
	char *buf;
	struct usb_ftdi *ftdi = file->private_data;

	if (ftdi->disconnected > 0) {
		return -ENODEV;
	}
	if (count == 0) {
		goto exit;
	}
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		retval = -ENOMEM;
		goto error_1;
	}
	buf = usb_alloc_coherent(ftdi->udev, count, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!buf) {
		retval = -ENOMEM;
		goto error_2;
	}
	if (copy_from_user(buf, user_buffer, count)) {
		retval = -EFAULT;
		goto error_3;
	}
	usb_fill_bulk_urb(urb, ftdi->udev, usb_sndbulkpipe(ftdi->udev,
							   ftdi->bulk_out_endpointAddr), buf, count,
			  ftdi_elan_write_bulk_callback, ftdi);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	retval = usb_submit_urb(urb, GFP_KERNEL);
	if (retval) {
		dev_err(&ftdi->udev->dev,
			"failed submitting write urb, error %d\n", retval);
		goto error_3;
	}
	usb_free_urb(urb);

exit:
	return count;
error_3:
	usb_free_coherent(ftdi->udev, count, buf, urb->transfer_dma);
error_2:
	usb_free_urb(urb);
error_1:
	return retval;
}

static const struct file_operations ftdi_elan_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = ftdi_elan_read,
	.write = ftdi_elan_write,
	.open = ftdi_elan_open,
	.release = ftdi_elan_release,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver ftdi_elan_jtag_class = {
	.name = "ftdi-%d-jtag",
	.fops = &ftdi_elan_fops,
	.minor_base = USB_FTDI_ELAN_MINOR_BASE,
};

/*
 * the following definitions are for the
 * ELAN FPGA state machgine processor that
 * lies on the other side of the FTDI chip
 */
#define cPCIu132rd 0x0
#define cPCIu132wr 0x1
#define cPCIiord 0x2
#define cPCIiowr 0x3
#define cPCImemrd 0x6
#define cPCImemwr 0x7
#define cPCIcfgrd 0xA
#define cPCIcfgwr 0xB
#define cPCInull 0xF
#define cU132cmd_status 0x0
#define cU132flash 0x1
#define cPIDsetup 0x0
#define cPIDout 0x1
#define cPIDin 0x2
#define cPIDinonce 0x3
#define cCCnoerror 0x0
#define cCCcrc 0x1
#define cCCbitstuff 0x2
#define cCCtoggle 0x3
#define cCCstall 0x4
#define cCCnoresp 0x5
#define cCCbadpid1 0x6
#define cCCbadpid2 0x7
#define cCCdataoverrun 0x8
#define cCCdataunderrun 0x9
#define cCCbuffoverrun 0xC
#define cCCbuffunderrun 0xD
#define cCCnotaccessed 0xF
static int ftdi_elan_write_reg(struct usb_ftdi *ftdi, u32 data)
{
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else {
		int command_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		if (command_size < COMMAND_SIZE) {
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			command->header = 0x00 | cPCIu132wr;
			command->length = 0x04;
			command->address = 0x00;
			command->width = 0x00;
			command->follows = 4;
			command->value = data;
			command->buffer = &command->value;
			ftdi->command_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

static int ftdi_elan_write_config(struct usb_ftdi *ftdi, int config_offset,
				  u8 width, u32 data)
{
	u8 addressofs = config_offset / 4;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else {
		int command_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		if (command_size < COMMAND_SIZE) {
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			command->header = 0x00 | (cPCIcfgwr & 0x0F);
			command->length = 0x04;
			command->address = addressofs;
			command->width = 0x00 | (width & 0x0F);
			command->follows = 4;
			command->value = data;
			command->buffer = &command->value;
			ftdi->command_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

static int ftdi_elan_write_pcimem(struct usb_ftdi *ftdi, int mem_offset,
				  u8 width, u32 data)
{
	u8 addressofs = mem_offset / 4;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else {
		int command_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		if (command_size < COMMAND_SIZE) {
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			command->header = 0x00 | (cPCImemwr & 0x0F);
			command->length = 0x04;
			command->address = addressofs;
			command->width = 0x00 | (width & 0x0F);
			command->follows = 4;
			command->value = data;
			command->buffer = &command->value;
			ftdi->command_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

int usb_ftdi_elan_write_pcimem(struct platform_device *pdev, int mem_offset,
			       u8 width, u32 data)
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	return ftdi_elan_write_pcimem(ftdi, mem_offset, width, data);
}


EXPORT_SYMBOL_GPL(usb_ftdi_elan_write_pcimem);
static int ftdi_elan_read_reg(struct usb_ftdi *ftdi, u32 *data)
{
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else {
		int command_size;
		int respond_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		respond_size = ftdi->respond_next - ftdi->respond_head;
		if (command_size < COMMAND_SIZE && respond_size < RESPOND_SIZE)
		{
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			struct u132_respond *respond = &ftdi->respond[
				RESPOND_MASK & ftdi->respond_next];
			int result = -ENODEV;
			respond->result = &result;
			respond->header = command->header = 0x00 | cPCIu132rd;
			command->length = 0x04;
			respond->address = command->address = cU132cmd_status;
			command->width = 0x00;
			command->follows = 0;
			command->value = 0;
			command->buffer = NULL;
			respond->value = data;
			init_completion(&respond->wait_completion);
			ftdi->command_next += 1;
			ftdi->respond_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			wait_for_completion(&respond->wait_completion);
			return result;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

static int ftdi_elan_read_config(struct usb_ftdi *ftdi, int config_offset,
				 u8 width, u32 *data)
{
	u8 addressofs = config_offset / 4;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else {
		int command_size;
		int respond_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		respond_size = ftdi->respond_next - ftdi->respond_head;
		if (command_size < COMMAND_SIZE && respond_size < RESPOND_SIZE)
		{
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			struct u132_respond *respond = &ftdi->respond[
				RESPOND_MASK & ftdi->respond_next];
			int result = -ENODEV;
			respond->result = &result;
			respond->header = command->header = 0x00 | (cPCIcfgrd &
								    0x0F);
			command->length = 0x04;
			respond->address = command->address = addressofs;
			command->width = 0x00 | (width & 0x0F);
			command->follows = 0;
			command->value = 0;
			command->buffer = NULL;
			respond->value = data;
			init_completion(&respond->wait_completion);
			ftdi->command_next += 1;
			ftdi->respond_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			wait_for_completion(&respond->wait_completion);
			return result;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

static int ftdi_elan_read_pcimem(struct usb_ftdi *ftdi, int mem_offset,
				 u8 width, u32 *data)
{
	u8 addressofs = mem_offset / 4;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else {
		int command_size;
		int respond_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		respond_size = ftdi->respond_next - ftdi->respond_head;
		if (command_size < COMMAND_SIZE && respond_size < RESPOND_SIZE)
		{
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			struct u132_respond *respond = &ftdi->respond[
				RESPOND_MASK & ftdi->respond_next];
			int result = -ENODEV;
			respond->result = &result;
			respond->header = command->header = 0x00 | (cPCImemrd &
								    0x0F);
			command->length = 0x04;
			respond->address = command->address = addressofs;
			command->width = 0x00 | (width & 0x0F);
			command->follows = 0;
			command->value = 0;
			command->buffer = NULL;
			respond->value = data;
			init_completion(&respond->wait_completion);
			ftdi->command_next += 1;
			ftdi->respond_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			wait_for_completion(&respond->wait_completion);
			return result;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

int usb_ftdi_elan_read_pcimem(struct platform_device *pdev, int mem_offset,
			      u8 width, u32 *data)
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	if (ftdi->initialized == 0) {
		return -ENODEV;
	} else
		return ftdi_elan_read_pcimem(ftdi, mem_offset, width, data);
}


EXPORT_SYMBOL_GPL(usb_ftdi_elan_read_pcimem);
static int ftdi_elan_edset_setup(struct usb_ftdi *ftdi, u8 ed_number,
				 void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
				 void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						   int toggle_bits, int error_count, int condition_code, int repeat_number,
						   int halted, int skipped, int actual, int non_null))
{
	u8 ed = ed_number - 1;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else if (ftdi->initialized == 0) {
		return -ENODEV;
	} else {
		int command_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		if (command_size < COMMAND_SIZE) {
			struct u132_target *target = &ftdi->target[ed];
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			command->header = 0x80 | (ed << 5);
			command->length = 0x8007;
			command->address = (toggle_bits << 6) | (ep_number << 2)
				| (address << 0);
			command->width = usb_maxpacket(urb->dev, urb->pipe,
						       usb_pipeout(urb->pipe));
			command->follows = 8;
			command->value = 0;
			command->buffer = urb->setup_packet;
			target->callback = callback;
			target->endp = endp;
			target->urb = urb;
			target->active = 1;
			ftdi->command_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

int usb_ftdi_elan_edset_setup(struct platform_device *pdev, u8 ed_number,
			      void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
			      void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						int toggle_bits, int error_count, int condition_code, int repeat_number,
						int halted, int skipped, int actual, int non_null))
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	return ftdi_elan_edset_setup(ftdi, ed_number, endp, urb, address,
				     ep_number, toggle_bits, callback);
}


EXPORT_SYMBOL_GPL(usb_ftdi_elan_edset_setup);
static int ftdi_elan_edset_input(struct usb_ftdi *ftdi, u8 ed_number,
				 void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
				 void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						   int toggle_bits, int error_count, int condition_code, int repeat_number,
						   int halted, int skipped, int actual, int non_null))
{
	u8 ed = ed_number - 1;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else if (ftdi->initialized == 0) {
		return -ENODEV;
	} else {
		int command_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		if (command_size < COMMAND_SIZE) {
			struct u132_target *target = &ftdi->target[ed];
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			u32 remaining_length = urb->transfer_buffer_length -
				urb->actual_length;
			command->header = 0x82 | (ed << 5);
			if (remaining_length == 0) {
				command->length = 0x0000;
			} else if (remaining_length > 1024) {
				command->length = 0x8000 | 1023;
			} else
				command->length = 0x8000 | (remaining_length -
							    1);
			command->address = (toggle_bits << 6) | (ep_number << 2)
				| (address << 0);
			command->width = usb_maxpacket(urb->dev, urb->pipe,
						       usb_pipeout(urb->pipe));
			command->follows = 0;
			command->value = 0;
			command->buffer = NULL;
			target->callback = callback;
			target->endp = endp;
			target->urb = urb;
			target->active = 1;
			ftdi->command_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

int usb_ftdi_elan_edset_input(struct platform_device *pdev, u8 ed_number,
			      void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
			      void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						int toggle_bits, int error_count, int condition_code, int repeat_number,
						int halted, int skipped, int actual, int non_null))
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	return ftdi_elan_edset_input(ftdi, ed_number, endp, urb, address,
				     ep_number, toggle_bits, callback);
}


EXPORT_SYMBOL_GPL(usb_ftdi_elan_edset_input);
static int ftdi_elan_edset_empty(struct usb_ftdi *ftdi, u8 ed_number,
				 void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
				 void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						   int toggle_bits, int error_count, int condition_code, int repeat_number,
						   int halted, int skipped, int actual, int non_null))
{
	u8 ed = ed_number - 1;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else if (ftdi->initialized == 0) {
		return -ENODEV;
	} else {
		int command_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		if (command_size < COMMAND_SIZE) {
			struct u132_target *target = &ftdi->target[ed];
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			command->header = 0x81 | (ed << 5);
			command->length = 0x0000;
			command->address = (toggle_bits << 6) | (ep_number << 2)
				| (address << 0);
			command->width = usb_maxpacket(urb->dev, urb->pipe,
						       usb_pipeout(urb->pipe));
			command->follows = 0;
			command->value = 0;
			command->buffer = NULL;
			target->callback = callback;
			target->endp = endp;
			target->urb = urb;
			target->active = 1;
			ftdi->command_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

int usb_ftdi_elan_edset_empty(struct platform_device *pdev, u8 ed_number,
			      void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
			      void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						int toggle_bits, int error_count, int condition_code, int repeat_number,
						int halted, int skipped, int actual, int non_null))
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	return ftdi_elan_edset_empty(ftdi, ed_number, endp, urb, address,
				     ep_number, toggle_bits, callback);
}


EXPORT_SYMBOL_GPL(usb_ftdi_elan_edset_empty);
static int ftdi_elan_edset_output(struct usb_ftdi *ftdi, u8 ed_number,
				  void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
				  void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						    int toggle_bits, int error_count, int condition_code, int repeat_number,
						    int halted, int skipped, int actual, int non_null))
{
	u8 ed = ed_number - 1;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else if (ftdi->initialized == 0) {
		return -ENODEV;
	} else {
		int command_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		if (command_size < COMMAND_SIZE) {
			u8 *b;
			u16 urb_size;
			int i = 0;
			char data[30 *3 + 4];
			char *d = data;
			int m = (sizeof(data) - 1) / 3 - 1;
			int l = 0;
			struct u132_target *target = &ftdi->target[ed];
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			command->header = 0x81 | (ed << 5);
			command->address = (toggle_bits << 6) | (ep_number << 2)
				| (address << 0);
			command->width = usb_maxpacket(urb->dev, urb->pipe,
						       usb_pipeout(urb->pipe));
			command->follows = min_t(u32, 1024,
						 urb->transfer_buffer_length -
						 urb->actual_length);
			command->value = 0;
			command->buffer = urb->transfer_buffer +
				urb->actual_length;
			command->length = 0x8000 | (command->follows - 1);
			b = command->buffer;
			urb_size = command->follows;
			data[0] = 0;
			while (urb_size-- > 0) {
				if (i > m) {
				} else if (i++ < m) {
					int w = sprintf(d, " %02X", *b++);
					d += w;
					l += w;
				} else
					d += sprintf(d, " ..");
			}
			target->callback = callback;
			target->endp = endp;
			target->urb = urb;
			target->active = 1;
			ftdi->command_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

int usb_ftdi_elan_edset_output(struct platform_device *pdev, u8 ed_number,
			       void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
			       void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						 int toggle_bits, int error_count, int condition_code, int repeat_number,
						 int halted, int skipped, int actual, int non_null))
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	return ftdi_elan_edset_output(ftdi, ed_number, endp, urb, address,
				      ep_number, toggle_bits, callback);
}


EXPORT_SYMBOL_GPL(usb_ftdi_elan_edset_output);
static int ftdi_elan_edset_single(struct usb_ftdi *ftdi, u8 ed_number,
				  void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
				  void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						    int toggle_bits, int error_count, int condition_code, int repeat_number,
						    int halted, int skipped, int actual, int non_null))
{
	u8 ed = ed_number - 1;
wait:if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else if (ftdi->initialized == 0) {
		return -ENODEV;
	} else {
		int command_size;
		mutex_lock(&ftdi->u132_lock);
		command_size = ftdi->command_next - ftdi->command_head;
		if (command_size < COMMAND_SIZE) {
			u32 remaining_length = urb->transfer_buffer_length -
				urb->actual_length;
			struct u132_target *target = &ftdi->target[ed];
			struct u132_command *command = &ftdi->command[
				COMMAND_MASK & ftdi->command_next];
			command->header = 0x83 | (ed << 5);
			if (remaining_length == 0) {
				command->length = 0x0000;
			} else if (remaining_length > 1024) {
				command->length = 0x8000 | 1023;
			} else
				command->length = 0x8000 | (remaining_length -
							    1);
			command->address = (toggle_bits << 6) | (ep_number << 2)
				| (address << 0);
			command->width = usb_maxpacket(urb->dev, urb->pipe,
						       usb_pipeout(urb->pipe));
			command->follows = 0;
			command->value = 0;
			command->buffer = NULL;
			target->callback = callback;
			target->endp = endp;
			target->urb = urb;
			target->active = 1;
			ftdi->command_next += 1;
			ftdi_elan_kick_command_queue(ftdi);
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			mutex_unlock(&ftdi->u132_lock);
			msleep(100);
			goto wait;
		}
	}
}

int usb_ftdi_elan_edset_single(struct platform_device *pdev, u8 ed_number,
			       void *endp, struct urb *urb, u8 address, u8 ep_number, u8 toggle_bits,
			       void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
						 int toggle_bits, int error_count, int condition_code, int repeat_number,
						 int halted, int skipped, int actual, int non_null))
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	return ftdi_elan_edset_single(ftdi, ed_number, endp, urb, address,
				      ep_number, toggle_bits, callback);
}


EXPORT_SYMBOL_GPL(usb_ftdi_elan_edset_single);
static int ftdi_elan_edset_flush(struct usb_ftdi *ftdi, u8 ed_number,
				 void *endp)
{
	u8 ed = ed_number - 1;
	if (ftdi->disconnected > 0) {
		return -ENODEV;
	} else if (ftdi->initialized == 0) {
		return -ENODEV;
	} else {
		struct u132_target *target = &ftdi->target[ed];
		mutex_lock(&ftdi->u132_lock);
		if (target->abandoning > 0) {
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		} else {
			target->abandoning = 1;
		wait_1:if (target->active == 1) {
				int command_size = ftdi->command_next -
					ftdi->command_head;
				if (command_size < COMMAND_SIZE) {
					struct u132_command *command =
						&ftdi->command[COMMAND_MASK &
							       ftdi->command_next];
					command->header = 0x80 | (ed << 5) |
						0x4;
					command->length = 0x00;
					command->address = 0x00;
					command->width = 0x00;
					command->follows = 0;
					command->value = 0;
					command->buffer = &command->value;
					ftdi->command_next += 1;
					ftdi_elan_kick_command_queue(ftdi);
				} else {
					mutex_unlock(&ftdi->u132_lock);
					msleep(100);
					mutex_lock(&ftdi->u132_lock);
					goto wait_1;
				}
			}
			mutex_unlock(&ftdi->u132_lock);
			return 0;
		}
	}
}

int usb_ftdi_elan_edset_flush(struct platform_device *pdev, u8 ed_number,
			      void *endp)
{
	struct usb_ftdi *ftdi = platform_device_to_usb_ftdi(pdev);
	return ftdi_elan_edset_flush(ftdi, ed_number, endp);
}


EXPORT_SYMBOL_GPL(usb_ftdi_elan_edset_flush);
static int ftdi_elan_flush_input_fifo(struct usb_ftdi *ftdi)
{
	int retry_on_empty = 10;
	int retry_on_timeout = 5;
	int retry_on_status = 20;
more:{
		int packet_bytes = 0;
		int retval = usb_bulk_msg(ftdi->udev,
					  usb_rcvbulkpipe(ftdi->udev, ftdi->bulk_in_endpointAddr),
					  ftdi->bulk_in_buffer, ftdi->bulk_in_size,
					  &packet_bytes, 100);
		if (packet_bytes > 2) {
			char diag[30 *3 + 4];
			char *d = diag;
			int m = (sizeof(diag) - 1) / 3 - 1;
			char *b = ftdi->bulk_in_buffer;
			int bytes_read = 0;
			diag[0] = 0;
			while (packet_bytes-- > 0) {
				char c = *b++;
				if (bytes_read < m) {
					d += sprintf(d, " %02X",
						     0x000000FF & c);
				} else if (bytes_read > m) {
				} else
					d += sprintf(d, " ..");
				bytes_read += 1;
				continue;
			}
			goto more;
		} else if (packet_bytes > 1) {
			char s1 = ftdi->bulk_in_buffer[0];
			char s2 = ftdi->bulk_in_buffer[1];
			if (s1 == 0x31 && s2 == 0x60) {
				return 0;
			} else if (retry_on_status-- > 0) {
				goto more;
			} else {
				dev_err(&ftdi->udev->dev, "STATUS ERROR retry limit reached\n");
				return -EFAULT;
			}
		} else if (packet_bytes > 0) {
			char b1 = ftdi->bulk_in_buffer[0];
			dev_err(&ftdi->udev->dev, "only one byte flushed from FTDI = %02X\n",
				b1);
			if (retry_on_status-- > 0) {
				goto more;
			} else {
				dev_err(&ftdi->udev->dev, "STATUS ERROR retry limit reached\n");
				return -EFAULT;
			}
		} else if (retval == -ETIMEDOUT) {
			if (retry_on_timeout-- > 0) {
				goto more;
			} else {
				dev_err(&ftdi->udev->dev, "TIMED OUT retry limit reached\n");
				return -ENOMEM;
			}
		} else if (retval == 0) {
			if (retry_on_empty-- > 0) {
				goto more;
			} else {
				dev_err(&ftdi->udev->dev, "empty packet retry limit reached\n");
				return -ENOMEM;
			}
		} else {
			dev_err(&ftdi->udev->dev, "error = %d\n", retval);
			return retval;
		}
	}
	return -1;
}


/*
 * send the long flush sequence
 *
 */
static int ftdi_elan_synchronize_flush(struct usb_ftdi *ftdi)
{
	int retval;
	struct urb *urb;
	char *buf;
	int I = 257;
	int i = 0;
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;
	buf = usb_alloc_coherent(ftdi->udev, I, GFP_KERNEL, &urb->transfer_dma);
	if (!buf) {
		dev_err(&ftdi->udev->dev, "could not get a buffer for flush sequence\n");
		usb_free_urb(urb);
		return -ENOMEM;
	}
	while (I-- > 0)
		buf[i++] = 0x55;
	usb_fill_bulk_urb(urb, ftdi->udev, usb_sndbulkpipe(ftdi->udev,
							   ftdi->bulk_out_endpointAddr), buf, i,
			  ftdi_elan_write_bulk_callback, ftdi);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	retval = usb_submit_urb(urb, GFP_KERNEL);
	if (retval) {
		dev_err(&ftdi->udev->dev, "failed to submit urb containing the flush sequence\n");
		usb_free_coherent(ftdi->udev, i, buf, urb->transfer_dma);
		usb_free_urb(urb);
		return -ENOMEM;
	}
	usb_free_urb(urb);
	return 0;
}


/*
 * send the reset sequence
 *
 */
static int ftdi_elan_synchronize_reset(struct usb_ftdi *ftdi)
{
	int retval;
	struct urb *urb;
	char *buf;
	int I = 4;
	int i = 0;
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;
	buf = usb_alloc_coherent(ftdi->udev, I, GFP_KERNEL, &urb->transfer_dma);
	if (!buf) {
		dev_err(&ftdi->udev->dev, "could not get a buffer for the reset sequence\n");
		usb_free_urb(urb);
		return -ENOMEM;
	}
	buf[i++] = 0x55;
	buf[i++] = 0xAA;
	buf[i++] = 0x5A;
	buf[i++] = 0xA5;
	usb_fill_bulk_urb(urb, ftdi->udev, usb_sndbulkpipe(ftdi->udev,
							   ftdi->bulk_out_endpointAddr), buf, i,
			  ftdi_elan_write_bulk_callback, ftdi);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	retval = usb_submit_urb(urb, GFP_KERNEL);
	if (retval) {
		dev_err(&ftdi->udev->dev, "failed to submit urb containing the reset sequence\n");
		usb_free_coherent(ftdi->udev, i, buf, urb->transfer_dma);
		usb_free_urb(urb);
		return -ENOMEM;
	}
	usb_free_urb(urb);
	return 0;
}

static int ftdi_elan_synchronize(struct usb_ftdi *ftdi)
{
	int retval;
	int long_stop = 10;
	int retry_on_timeout = 5;
	int retry_on_empty = 10;
	int err_count = 0;
	retval = ftdi_elan_flush_input_fifo(ftdi);
	if (retval)
		return retval;
	ftdi->bulk_in_left = 0;
	ftdi->bulk_in_last = -1;
	while (long_stop-- > 0) {
		int read_stop;
		int read_stuck;
		retval = ftdi_elan_synchronize_flush(ftdi);
		if (retval)
			return retval;
		retval = ftdi_elan_flush_input_fifo(ftdi);
		if (retval)
			return retval;
	reset:retval = ftdi_elan_synchronize_reset(ftdi);
		if (retval)
			return retval;
		read_stop = 100;
		read_stuck = 10;
	read:{
			int packet_bytes = 0;
			retval = usb_bulk_msg(ftdi->udev,
					      usb_rcvbulkpipe(ftdi->udev,
							      ftdi->bulk_in_endpointAddr),
					      ftdi->bulk_in_buffer, ftdi->bulk_in_size,
					      &packet_bytes, 500);
			if (packet_bytes > 2) {
				char diag[30 *3 + 4];
				char *d = diag;
				int m = (sizeof(diag) - 1) / 3 - 1;
				char *b = ftdi->bulk_in_buffer;
				int bytes_read = 0;
				unsigned char c = 0;
				diag[0] = 0;
				while (packet_bytes-- > 0) {
					c = *b++;
					if (bytes_read < m) {
						d += sprintf(d, " %02X", c);
					} else if (bytes_read > m) {
					} else
						d += sprintf(d, " ..");
					bytes_read += 1;
					continue;
				}
				if (c == 0x7E) {
					return 0;
				} else {
					if (c == 0x55) {
						goto read;
					} else if (read_stop-- > 0) {
						goto read;
					} else {
						dev_err(&ftdi->udev->dev, "retry limit reached\n");
						continue;
					}
				}
			} else if (packet_bytes > 1) {
				unsigned char s1 = ftdi->bulk_in_buffer[0];
				unsigned char s2 = ftdi->bulk_in_buffer[1];
				if (s1 == 0x31 && s2 == 0x00) {
					if (read_stuck-- > 0) {
						goto read;
					} else
						goto reset;
				} else if (s1 == 0x31 && s2 == 0x60) {
					if (read_stop-- > 0) {
						goto read;
					} else {
						dev_err(&ftdi->udev->dev, "retry limit reached\n");
						continue;
					}
				} else {
					if (read_stop-- > 0) {
						goto read;
					} else {
						dev_err(&ftdi->udev->dev, "retry limit reached\n");
						continue;
					}
				}
			} else if (packet_bytes > 0) {
				if (read_stop-- > 0) {
					goto read;
				} else {
					dev_err(&ftdi->udev->dev, "retry limit reached\n");
					continue;
				}
			} else if (retval == -ETIMEDOUT) {
				if (retry_on_timeout-- > 0) {
					goto read;
				} else {
					dev_err(&ftdi->udev->dev, "TIMED OUT retry limit reached\n");
					continue;
				}
			} else if (retval == 0) {
				if (retry_on_empty-- > 0) {
					goto read;
				} else {
					dev_err(&ftdi->udev->dev, "empty packet retry limit reached\n");
					continue;
				}
			} else {
				err_count += 1;
				dev_err(&ftdi->udev->dev, "error = %d\n",
					retval);
				if (read_stop-- > 0) {
					goto read;
				} else {
					dev_err(&ftdi->udev->dev, "retry limit reached\n");
					continue;
				}
			}
		}
	}
	dev_err(&ftdi->udev->dev, "failed to synchronize\n");
	return -EFAULT;
}

static int ftdi_elan_stuck_waiting(struct usb_ftdi *ftdi)
{
	int retry_on_empty = 10;
	int retry_on_timeout = 5;
	int retry_on_status = 50;
more:{
		int packet_bytes = 0;
		int retval = usb_bulk_msg(ftdi->udev,
					  usb_rcvbulkpipe(ftdi->udev, ftdi->bulk_in_endpointAddr),
					  ftdi->bulk_in_buffer, ftdi->bulk_in_size,
					  &packet_bytes, 1000);
		if (packet_bytes > 2) {
			char diag[30 *3 + 4];
			char *d = diag;
			int m = (sizeof(diag) - 1) / 3 - 1;
			char *b = ftdi->bulk_in_buffer;
			int bytes_read = 0;
			diag[0] = 0;
			while (packet_bytes-- > 0) {
				char c = *b++;
				if (bytes_read < m) {
					d += sprintf(d, " %02X",
						     0x000000FF & c);
				} else if (bytes_read > m) {
				} else
					d += sprintf(d, " ..");
				bytes_read += 1;
				continue;
			}
			goto more;
		} else if (packet_bytes > 1) {
			char s1 = ftdi->bulk_in_buffer[0];
			char s2 = ftdi->bulk_in_buffer[1];
			if (s1 == 0x31 && s2 == 0x60) {
				return 0;
			} else if (retry_on_status-- > 0) {
				msleep(5);
				goto more;
			} else
				return -EFAULT;
		} else if (packet_bytes > 0) {
			char b1 = ftdi->bulk_in_buffer[0];
			dev_err(&ftdi->udev->dev, "only one byte flushed from FTDI = %02X\n", b1);
			if (retry_on_status-- > 0) {
				msleep(5);
				goto more;
			} else {
				dev_err(&ftdi->udev->dev, "STATUS ERROR retry limit reached\n");
				return -EFAULT;
			}
		} else if (retval == -ETIMEDOUT) {
			if (retry_on_timeout-- > 0) {
				goto more;
			} else {
				dev_err(&ftdi->udev->dev, "TIMED OUT retry limit reached\n");
				return -ENOMEM;
			}
		} else if (retval == 0) {
			if (retry_on_empty-- > 0) {
				goto more;
			} else {
				dev_err(&ftdi->udev->dev, "empty packet retry limit reached\n");
				return -ENOMEM;
			}
		} else {
			dev_err(&ftdi->udev->dev, "error = %d\n", retval);
			return -ENOMEM;
		}
	}
	return -1;
}

static int ftdi_elan_checkingPCI(struct usb_ftdi *ftdi)
{
	int UxxxStatus = ftdi_elan_read_reg(ftdi, &ftdi->controlreg);
	if (UxxxStatus)
		return UxxxStatus;
	if (ftdi->controlreg & 0x00400000) {
		if (ftdi->card_ejected) {
		} else {
			ftdi->card_ejected = 1;
			dev_err(&ftdi->udev->dev, "CARD EJECTED - controlreg = %08X\n",
				ftdi->controlreg);
		}
		return -ENODEV;
	} else {
		u8 fn = ftdi->function - 1;
		int activePCIfn = fn << 8;
		u32 pcidata;
		u32 pciVID;
		u32 pciPID;
		int reg = 0;
		UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
						   &pcidata);
		if (UxxxStatus)
			return UxxxStatus;
		pciVID = pcidata & 0xFFFF;
		pciPID = (pcidata >> 16) & 0xFFFF;
		if (pciVID == ftdi->platform_data.vendor && pciPID ==
		    ftdi->platform_data.device) {
			return 0;
		} else {
			dev_err(&ftdi->udev->dev, "vendor=%04X pciVID=%04X device=%04X pciPID=%04X\n",
				ftdi->platform_data.vendor, pciVID,
				ftdi->platform_data.device, pciPID);
			return -ENODEV;
		}
	}
}


#define ftdi_read_pcimem(ftdi, member, data) ftdi_elan_read_pcimem(ftdi, \
								   offsetof(struct ohci_regs, member), 0, data);
#define ftdi_write_pcimem(ftdi, member, data) ftdi_elan_write_pcimem(ftdi, \
								     offsetof(struct ohci_regs, member), 0, data);

#define OHCI_CONTROL_INIT OHCI_CTRL_CBSR
#define OHCI_INTR_INIT (OHCI_INTR_MIE | OHCI_INTR_UE | OHCI_INTR_RD |	\
			OHCI_INTR_WDH)
static int ftdi_elan_check_controller(struct usb_ftdi *ftdi, int quirk)
{
	int devices = 0;
	int retval;
	u32 hc_control;
	int num_ports;
	u32 control;
	u32 rh_a = -1;
	u32 status;
	u32 fminterval;
	u32 hc_fminterval;
	u32 periodicstart;
	u32 cmdstatus;
	u32 roothub_a;
	int mask = OHCI_INTR_INIT;
	int sleep_time = 0;
	int reset_timeout = 30;        /* ... allow extra time */
	int temp;
	retval = ftdi_write_pcimem(ftdi, intrdisable, OHCI_INTR_MIE);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, control, &control);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, roothub.a, &rh_a);
	if (retval)
		return retval;
	num_ports = rh_a & RH_A_NDP;
	retval = ftdi_read_pcimem(ftdi, fminterval, &hc_fminterval);
	if (retval)
		return retval;
	hc_fminterval &= 0x3fff;
	if (hc_fminterval != FI) {
	}
	hc_fminterval |= FSMP(hc_fminterval) << 16;
	retval = ftdi_read_pcimem(ftdi, control, &hc_control);
	if (retval)
		return retval;
	switch (hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_OPER:
		sleep_time = 0;
		break;
	case OHCI_USB_SUSPEND:
	case OHCI_USB_RESUME:
		hc_control &= OHCI_CTRL_RWC;
		hc_control |= OHCI_USB_RESUME;
		sleep_time = 10;
		break;
	default:
		hc_control &= OHCI_CTRL_RWC;
		hc_control |= OHCI_USB_RESET;
		sleep_time = 50;
		break;
	}
	retval = ftdi_write_pcimem(ftdi, control, hc_control);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, control, &control);
	if (retval)
		return retval;
	msleep(sleep_time);
	retval = ftdi_read_pcimem(ftdi, roothub.a, &roothub_a);
	if (retval)
		return retval;
	if (!(roothub_a & RH_A_NPS)) {        /* power down each port */
		for (temp = 0; temp < num_ports; temp++) {
			retval = ftdi_write_pcimem(ftdi,
						   roothub.portstatus[temp], RH_PS_LSDA);
			if (retval)
				return retval;
		}
	}
	retval = ftdi_read_pcimem(ftdi, control, &control);
	if (retval)
		return retval;
retry:retval = ftdi_read_pcimem(ftdi, cmdstatus, &status);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, cmdstatus, OHCI_HCR);
	if (retval)
		return retval;
extra:{
		retval = ftdi_read_pcimem(ftdi, cmdstatus, &status);
		if (retval)
			return retval;
		if (0 != (status & OHCI_HCR)) {
			if (--reset_timeout == 0) {
				dev_err(&ftdi->udev->dev, "USB HC reset timed out!\n");
				return -ENODEV;
			} else {
				msleep(5);
				goto extra;
			}
		}
	}
	if (quirk & OHCI_QUIRK_INITRESET) {
		retval = ftdi_write_pcimem(ftdi, control, hc_control);
		if (retval)
			return retval;
		retval = ftdi_read_pcimem(ftdi, control, &control);
		if (retval)
			return retval;
	}
	retval = ftdi_write_pcimem(ftdi, ed_controlhead, 0x00000000);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, ed_bulkhead, 0x11000000);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, hcca, 0x00000000);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, fminterval, &fminterval);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, fminterval,
				   ((fminterval & FIT) ^ FIT) | hc_fminterval);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, periodicstart,
				   ((9 *hc_fminterval) / 10) & 0x3fff);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, fminterval, &fminterval);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, periodicstart, &periodicstart);
	if (retval)
		return retval;
	if (0 == (fminterval & 0x3fff0000) || 0 == periodicstart) {
		if (!(quirk & OHCI_QUIRK_INITRESET)) {
			quirk |= OHCI_QUIRK_INITRESET;
			goto retry;
		} else
			dev_err(&ftdi->udev->dev, "init err(%08x %04x)\n",
				fminterval, periodicstart);
	}                        /* start controller operations */
	hc_control &= OHCI_CTRL_RWC;
	hc_control |= OHCI_CONTROL_INIT | OHCI_CTRL_BLE | OHCI_USB_OPER;
	retval = ftdi_write_pcimem(ftdi, control, hc_control);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, cmdstatus, OHCI_BLF);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, cmdstatus, &cmdstatus);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, control, &control);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, roothub.status, RH_HS_DRWE);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, intrstatus, mask);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, intrdisable,
				   OHCI_INTR_MIE | OHCI_INTR_OC | OHCI_INTR_RHSC | OHCI_INTR_FNO |
				   OHCI_INTR_UE | OHCI_INTR_RD | OHCI_INTR_SF | OHCI_INTR_WDH |
				   OHCI_INTR_SO);
	if (retval)
		return retval;        /* handle root hub init quirks ... */
	retval = ftdi_read_pcimem(ftdi, roothub.a, &roothub_a);
	if (retval)
		return retval;
	roothub_a &= ~(RH_A_PSM | RH_A_OCPM);
	if (quirk & OHCI_QUIRK_SUPERIO) {
		roothub_a |= RH_A_NOCP;
		roothub_a &= ~(RH_A_POTPGT | RH_A_NPS);
		retval = ftdi_write_pcimem(ftdi, roothub.a, roothub_a);
		if (retval)
			return retval;
	} else if ((quirk & OHCI_QUIRK_AMD756) || distrust_firmware) {
		roothub_a |= RH_A_NPS;
		retval = ftdi_write_pcimem(ftdi, roothub.a, roothub_a);
		if (retval)
			return retval;
	}
	retval = ftdi_write_pcimem(ftdi, roothub.status, RH_HS_LPSC);
	if (retval)
		return retval;
	retval = ftdi_write_pcimem(ftdi, roothub.b,
				   (roothub_a & RH_A_NPS) ? 0 : RH_B_PPCM);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(ftdi, control, &control);
	if (retval)
		return retval;
	mdelay((roothub_a >> 23) & 0x1fe);
	for (temp = 0; temp < num_ports; temp++) {
		u32 portstatus;
		retval = ftdi_read_pcimem(ftdi, roothub.portstatus[temp],
					  &portstatus);
		if (retval)
			return retval;
		if (1 & portstatus)
			devices += 1;
	}
	return devices;
}

static int ftdi_elan_setup_controller(struct usb_ftdi *ftdi, int fn)
{
	u32 latence_timer;
	int UxxxStatus;
	u32 pcidata;
	int reg = 0;
	int activePCIfn = fn << 8;
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x0000025FL | 0x2800);
	if (UxxxStatus)
		return UxxxStatus;
	reg = 16;
	UxxxStatus = ftdi_elan_write_config(ftdi, activePCIfn | reg, 0,
					    0xFFFFFFFF);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &pcidata);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_write_config(ftdi, activePCIfn | reg, 0,
					    0xF0000000);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &pcidata);
	if (UxxxStatus)
		return UxxxStatus;
	reg = 12;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &latence_timer);
	if (UxxxStatus)
		return UxxxStatus;
	latence_timer &= 0xFFFF00FF;
	latence_timer |= 0x00001600;
	UxxxStatus = ftdi_elan_write_config(ftdi, activePCIfn | reg, 0x00,
					    latence_timer);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &pcidata);
	if (UxxxStatus)
		return UxxxStatus;
	reg = 4;
	UxxxStatus = ftdi_elan_write_config(ftdi, activePCIfn | reg, 0x00,
					    0x06);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &pcidata);
	if (UxxxStatus)
		return UxxxStatus;
	for (reg = 0; reg <= 0x54; reg += 4) {
		UxxxStatus = ftdi_elan_read_pcimem(ftdi, reg, 0, &pcidata);
		if (UxxxStatus)
			return UxxxStatus;
	}
	return 0;
}

static int ftdi_elan_close_controller(struct usb_ftdi *ftdi, int fn)
{
	u32 latence_timer;
	int UxxxStatus;
	u32 pcidata;
	int reg = 0;
	int activePCIfn = fn << 8;
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x0000025FL | 0x2800);
	if (UxxxStatus)
		return UxxxStatus;
	reg = 16;
	UxxxStatus = ftdi_elan_write_config(ftdi, activePCIfn | reg, 0,
					    0xFFFFFFFF);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &pcidata);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_write_config(ftdi, activePCIfn | reg, 0,
					    0x00000000);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &pcidata);
	if (UxxxStatus)
		return UxxxStatus;
	reg = 12;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &latence_timer);
	if (UxxxStatus)
		return UxxxStatus;
	latence_timer &= 0xFFFF00FF;
	latence_timer |= 0x00001600;
	UxxxStatus = ftdi_elan_write_config(ftdi, activePCIfn | reg, 0x00,
					    latence_timer);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
					   &pcidata);
	if (UxxxStatus)
		return UxxxStatus;
	reg = 4;
	UxxxStatus = ftdi_elan_write_config(ftdi, activePCIfn | reg, 0x00,
					    0x00);
	if (UxxxStatus)
		return UxxxStatus;
	return ftdi_elan_read_config(ftdi, activePCIfn | reg, 0, &pcidata);
}

static int ftdi_elan_found_controller(struct usb_ftdi *ftdi, int fn, int quirk)
{
	int result;
	int UxxxStatus;
	UxxxStatus = ftdi_elan_setup_controller(ftdi, fn);
	if (UxxxStatus)
		return UxxxStatus;
	result = ftdi_elan_check_controller(ftdi, quirk);
	UxxxStatus = ftdi_elan_close_controller(ftdi, fn);
	if (UxxxStatus)
		return UxxxStatus;
	return result;
}

static int ftdi_elan_enumeratePCI(struct usb_ftdi *ftdi)
{
	u32 controlreg;
	u8 sensebits;
	int UxxxStatus;
	UxxxStatus = ftdi_elan_read_reg(ftdi, &controlreg);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x00000000L);
	if (UxxxStatus)
		return UxxxStatus;
	msleep(750);
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x00000200L | 0x100);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x00000200L | 0x500);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_reg(ftdi, &controlreg);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x0000020CL | 0x000);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x0000020DL | 0x000);
	if (UxxxStatus)
		return UxxxStatus;
	msleep(250);
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x0000020FL | 0x000);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_reg(ftdi, &controlreg);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_write_reg(ftdi, 0x0000025FL | 0x800);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_reg(ftdi, &controlreg);
	if (UxxxStatus)
		return UxxxStatus;
	UxxxStatus = ftdi_elan_read_reg(ftdi, &controlreg);
	if (UxxxStatus)
		return UxxxStatus;
	msleep(1000);
	sensebits = (controlreg >> 16) & 0x000F;
	if (0x0D == sensebits)
		return 0;
	else
		return - ENXIO;
}

static int ftdi_elan_setupOHCI(struct usb_ftdi *ftdi)
{
	int UxxxStatus;
	u32 pcidata;
	int reg = 0;
	u8 fn;
	int activePCIfn = 0;
	int max_devices = 0;
	int controllers = 0;
	int unrecognized = 0;
	ftdi->function = 0;
	for (fn = 0; (fn < 4); fn++) {
		u32 pciVID = 0;
		u32 pciPID = 0;
		int devices = 0;
		activePCIfn = fn << 8;
		UxxxStatus = ftdi_elan_read_config(ftdi, activePCIfn | reg, 0,
						   &pcidata);
		if (UxxxStatus)
			return UxxxStatus;
		pciVID = pcidata & 0xFFFF;
		pciPID = (pcidata >> 16) & 0xFFFF;
		if ((pciVID == PCI_VENDOR_ID_OPTI) && (pciPID == 0xc861)) {
			devices = ftdi_elan_found_controller(ftdi, fn, 0);
			controllers += 1;
		} else if ((pciVID == PCI_VENDOR_ID_NEC) && (pciPID == 0x0035))
		{
			devices = ftdi_elan_found_controller(ftdi, fn, 0);
			controllers += 1;
		} else if ((pciVID == PCI_VENDOR_ID_AL) && (pciPID == 0x5237)) {
			devices = ftdi_elan_found_controller(ftdi, fn, 0);
			controllers += 1;
		} else if ((pciVID == PCI_VENDOR_ID_ATT) && (pciPID == 0x5802))
		{
			devices = ftdi_elan_found_controller(ftdi, fn, 0);
			controllers += 1;
		} else if (pciVID == PCI_VENDOR_ID_AMD && pciPID == 0x740c) {
			devices = ftdi_elan_found_controller(ftdi, fn,
							     OHCI_QUIRK_AMD756);
			controllers += 1;
		} else if (pciVID == PCI_VENDOR_ID_COMPAQ && pciPID == 0xa0f8) {
			devices = ftdi_elan_found_controller(ftdi, fn,
							     OHCI_QUIRK_ZFMICRO);
			controllers += 1;
		} else if (0 == pcidata) {
		} else
			unrecognized += 1;
		if (devices > max_devices) {
			max_devices = devices;
			ftdi->function = fn + 1;
			ftdi->platform_data.vendor = pciVID;
			ftdi->platform_data.device = pciPID;
		}
	}
	if (ftdi->function > 0) {
		return ftdi_elan_setup_controller(ftdi,	ftdi->function - 1);
	} else if (controllers > 0) {
		return -ENXIO;
	} else if (unrecognized > 0) {
		return -ENXIO;
	} else {
		ftdi->enumerated = 0;
		return -ENXIO;
	}
}


/*
 * we use only the first bulk-in and bulk-out endpoints
 */
static int ftdi_elan_probe(struct usb_interface *interface,
			   const struct usb_device_id *id)
{
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *bulk_in, *bulk_out;
	int retval;
	struct usb_ftdi *ftdi;

	ftdi = kzalloc(sizeof(struct usb_ftdi), GFP_KERNEL);
	if (!ftdi)
		return -ENOMEM;

	mutex_lock(&ftdi_module_lock);
	list_add_tail(&ftdi->ftdi_list, &ftdi_static_list);
	ftdi->sequence_num = ++ftdi_instances;
	mutex_unlock(&ftdi_module_lock);
	ftdi_elan_init_kref(ftdi);
	sema_init(&ftdi->sw_lock, 1);
	ftdi->udev = usb_get_dev(interface_to_usbdev(interface));
	ftdi->interface = interface;
	mutex_init(&ftdi->u132_lock);
	ftdi->expected = 4;

	iface_desc = interface->cur_altsetting;
	retval = usb_find_common_endpoints(iface_desc,
			&bulk_in, &bulk_out, NULL, NULL);
	if (retval) {
		dev_err(&ftdi->udev->dev, "Could not find both bulk-in and bulk-out endpoints\n");
		goto error;
	}

	ftdi->bulk_in_size = usb_endpoint_maxp(bulk_in);
	ftdi->bulk_in_endpointAddr = bulk_in->bEndpointAddress;
	ftdi->bulk_in_buffer = kmalloc(ftdi->bulk_in_size, GFP_KERNEL);
	if (!ftdi->bulk_in_buffer) {
		retval = -ENOMEM;
		goto error;
	}

	ftdi->bulk_out_endpointAddr = bulk_out->bEndpointAddress;

	dev_info(&ftdi->udev->dev, "interface %d has I=%02X O=%02X\n",
		 iface_desc->desc.bInterfaceNumber, ftdi->bulk_in_endpointAddr,
		 ftdi->bulk_out_endpointAddr);
	usb_set_intfdata(interface, ftdi);
	if (iface_desc->desc.bInterfaceNumber == 0 &&
	    ftdi->bulk_in_endpointAddr == 0x81 &&
	    ftdi->bulk_out_endpointAddr == 0x02) {
		retval = usb_register_dev(interface, &ftdi_elan_jtag_class);
		if (retval) {
			dev_err(&ftdi->udev->dev, "Not able to get a minor for this device\n");
			usb_set_intfdata(interface, NULL);
			retval = -ENOMEM;
			goto error;
		} else {
			ftdi->class = &ftdi_elan_jtag_class;
			dev_info(&ftdi->udev->dev, "USB FDTI=%p JTAG interface %d now attached to ftdi%d\n",
				 ftdi, iface_desc->desc.bInterfaceNumber,
				 interface->minor);
			return 0;
		}
	} else if (iface_desc->desc.bInterfaceNumber == 1 &&
		   ftdi->bulk_in_endpointAddr == 0x83 &&
		   ftdi->bulk_out_endpointAddr == 0x04) {
		ftdi->class = NULL;
		dev_info(&ftdi->udev->dev, "USB FDTI=%p ELAN interface %d now activated\n",
			 ftdi, iface_desc->desc.bInterfaceNumber);
		INIT_DELAYED_WORK(&ftdi->status_work, ftdi_elan_status_work);
		INIT_DELAYED_WORK(&ftdi->command_work, ftdi_elan_command_work);
		INIT_DELAYED_WORK(&ftdi->respond_work, ftdi_elan_respond_work);
		ftdi_status_queue_work(ftdi, msecs_to_jiffies(3 *1000));
		return 0;
	} else {
		dev_err(&ftdi->udev->dev,
			"Could not find ELAN's U132 device\n");
		retval = -ENODEV;
		goto error;
	}
error:if (ftdi) {
		ftdi_elan_put_kref(ftdi);
	}
	return retval;
}

static void ftdi_elan_disconnect(struct usb_interface *interface)
{
	struct usb_ftdi *ftdi = usb_get_intfdata(interface);
	ftdi->disconnected += 1;
	if (ftdi->class) {
		int minor = interface->minor;
		struct usb_class_driver *class = ftdi->class;
		usb_set_intfdata(interface, NULL);
		usb_deregister_dev(interface, class);
		dev_info(&ftdi->udev->dev, "USB FTDI U132 jtag interface on minor %d now disconnected\n",
			 minor);
	} else {
		ftdi_status_cancel_work(ftdi);
		ftdi_command_cancel_work(ftdi);
		ftdi_response_cancel_work(ftdi);
		ftdi_elan_abandon_completions(ftdi);
		ftdi_elan_abandon_targets(ftdi);
		if (ftdi->registered) {
			platform_device_unregister(&ftdi->platform_dev);
			ftdi->synchronized = 0;
			ftdi->enumerated = 0;
			ftdi->initialized = 0;
			ftdi->registered = 0;
		}
		ftdi->disconnected += 1;
		usb_set_intfdata(interface, NULL);
		dev_info(&ftdi->udev->dev, "USB FTDI U132 host controller interface now disconnected\n");
	}
	ftdi_elan_put_kref(ftdi);
}

static struct usb_driver ftdi_elan_driver = {
	.name = "ftdi-elan",
	.probe = ftdi_elan_probe,
	.disconnect = ftdi_elan_disconnect,
	.id_table = ftdi_elan_table,
};
static int __init ftdi_elan_init(void)
{
	int result;
	pr_info("driver %s\n", ftdi_elan_driver.name);
	mutex_init(&ftdi_module_lock);
	INIT_LIST_HEAD(&ftdi_static_list);
	result = usb_register(&ftdi_elan_driver);
	if (result) {
		pr_err("usb_register failed. Error number %d\n", result);
	}
	return result;

}

static void __exit ftdi_elan_exit(void)
{
	struct usb_ftdi *ftdi;
	struct usb_ftdi *temp;
	usb_deregister(&ftdi_elan_driver);
	pr_info("ftdi_u132 driver deregistered\n");
	list_for_each_entry_safe(ftdi, temp, &ftdi_static_list, ftdi_list) {
		ftdi_status_cancel_work(ftdi);
		ftdi_command_cancel_work(ftdi);
		ftdi_response_cancel_work(ftdi);
	}
}


module_init(ftdi_elan_init);
module_exit(ftdi_elan_exit);
