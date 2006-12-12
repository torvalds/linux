/*
 * devices.c
 * (C) Copyright 1999 Randy Dunlap.
 * (C) Copyright 1999,2000 Thomas Sailer <sailer@ife.ee.ethz.ch>. (proc file per device)
 * (C) Copyright 1999 Deti Fliegl (new USB architecture)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *************************************************************
 *
 * <mountpoint>/devices contains USB topology, device, config, class,
 * interface, & endpoint data.
 *
 * I considered using /proc/bus/usb/devices/device# for each device
 * as it is attached or detached, but I didn't like this for some
 * reason -- maybe it's just too deep of a directory structure.
 * I also don't like looking in multiple places to gather and view
 * the data.  Having only one file for ./devices also prevents race
 * conditions that could arise if a program was reading device info
 * for devices that are being removed (unplugged).  (That is, the
 * program may find a directory for devnum_12 then try to open it,
 * but it was just unplugged, so the directory is now deleted.
 * But programs would just have to be prepared for situations like
 * this in any plug-and-play environment.)
 *
 * 1999-12-16: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *   Converted the whole proc stuff to real
 *   read methods. Now not the whole device list needs to fit
 *   into one page, only the device list for one bus.
 *   Added a poll method to /proc/bus/usb/devices, to wake
 *   up an eventual usbd
 * 2000-01-04: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *   Turned into its own filesystem
 * 2000-07-05: Ashley Montanaro <ashley@compsoc.man.ac.uk>
 *   Converted file reading routine to dump to buffer once
 *   per device, not per bus
 *
 * $Id: devices.c,v 1.5 2000/01/11 13:58:21 tom Exp $
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/usb.h>
#include <linux/smp_lock.h>
#include <linux/usbdevice_fs.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#include "usb.h"
#include "hcd.h"

#define MAX_TOPO_LEVEL		6

/* Define ALLOW_SERIAL_NUMBER if you want to see the serial number of devices */
#define ALLOW_SERIAL_NUMBER

static const char *format_topo =
/* T:  Bus=dd Lev=dd Prnt=dd Port=dd Cnt=dd Dev#=ddd Spd=ddd MxCh=dd */
"\nT:  Bus=%2.2d Lev=%2.2d Prnt=%2.2d Port=%2.2d Cnt=%2.2d Dev#=%3d Spd=%3s MxCh=%2d\n";

static const char *format_string_manufacturer =
/* S:  Manufacturer=xxxx */
  "S:  Manufacturer=%.100s\n";

static const char *format_string_product =
/* S:  Product=xxxx */
  "S:  Product=%.100s\n";

#ifdef ALLOW_SERIAL_NUMBER
static const char *format_string_serialnumber =
/* S:  SerialNumber=xxxx */
  "S:  SerialNumber=%.100s\n";
#endif

static const char *format_bandwidth =
/* B:  Alloc=ddd/ddd us (xx%), #Int=ddd, #Iso=ddd */
  "B:  Alloc=%3d/%3d us (%2d%%), #Int=%3d, #Iso=%3d\n";
  
static const char *format_device1 =
/* D:  Ver=xx.xx Cls=xx(sssss) Sub=xx Prot=xx MxPS=dd #Cfgs=dd */
  "D:  Ver=%2x.%02x Cls=%02x(%-5s) Sub=%02x Prot=%02x MxPS=%2d #Cfgs=%3d\n";

static const char *format_device2 =
/* P:  Vendor=xxxx ProdID=xxxx Rev=xx.xx */
  "P:  Vendor=%04x ProdID=%04x Rev=%2x.%02x\n";

static const char *format_config =
/* C:  #Ifs=dd Cfg#=dd Atr=xx MPwr=dddmA */
  "C:%c #Ifs=%2d Cfg#=%2d Atr=%02x MxPwr=%3dmA\n";
  
static const char *format_iface =
/* I:  If#=dd Alt=dd #EPs=dd Cls=xx(sssss) Sub=xx Prot=xx Driver=xxxx*/
  "I:  If#=%2d Alt=%2d #EPs=%2d Cls=%02x(%-5s) Sub=%02x Prot=%02x Driver=%s\n";

static const char *format_endpt =
/* E:  Ad=xx(s) Atr=xx(ssss) MxPS=dddd Ivl=D?s */
  "E:  Ad=%02x(%c) Atr=%02x(%-4s) MxPS=%4d Ivl=%d%cs\n";


/*
 * Need access to the driver and USB bus lists.
 * extern struct list_head usb_bus_list;
 * However, these will come from functions that return ptrs to each of them.
 */

static DECLARE_WAIT_QUEUE_HEAD(deviceconndiscwq);
static unsigned int conndiscevcnt = 0;

/* this struct stores the poll state for <mountpoint>/devices pollers */
struct usb_device_status {
	unsigned int lastev;
};

struct class_info {
	int class;
	char *class_name;
};

static const struct class_info clas_info[] =
{					/* max. 5 chars. per name string */
	{USB_CLASS_PER_INTERFACE,	">ifc"},
	{USB_CLASS_AUDIO,		"audio"},
	{USB_CLASS_COMM,		"comm."},
	{USB_CLASS_HID,			"HID"},
	{USB_CLASS_HUB,			"hub"},
	{USB_CLASS_PHYSICAL,		"PID"},
	{USB_CLASS_PRINTER,		"print"},
	{USB_CLASS_MASS_STORAGE,	"stor."},
	{USB_CLASS_CDC_DATA,		"data"},
	{USB_CLASS_APP_SPEC,		"app."},
	{USB_CLASS_VENDOR_SPEC,		"vend."},
	{USB_CLASS_STILL_IMAGE,		"still"},
	{USB_CLASS_CSCID,		"scard"},
	{USB_CLASS_CONTENT_SEC,		"c-sec"},
	{-1,				"unk."}		/* leave as last */
};

/*****************************************************************/

void usbfs_conn_disc_event(void)
{
	conndiscevcnt++;
	wake_up(&deviceconndiscwq);
}

static const char *class_decode(const int class)
{
	int ix;

	for (ix = 0; clas_info[ix].class != -1; ix++)
		if (clas_info[ix].class == class)
			break;
	return (clas_info[ix].class_name);
}

static char *usb_dump_endpoint_descriptor (
	int speed,
	char *start,
	char *end,
	const struct usb_endpoint_descriptor *desc
)
{
	char dir, unit, *type;
	unsigned interval, bandwidth = 1;

	if (start > end)
		return start;

	dir = usb_endpoint_dir_in(desc) ? 'I' : 'O';

	if (speed == USB_SPEED_HIGH) {
		switch (le16_to_cpu(desc->wMaxPacketSize) & (0x03 << 11)) {
		case 1 << 11:	bandwidth = 2; break;
		case 2 << 11:	bandwidth = 3; break;
		}
	}

	/* this isn't checking for illegal values */
	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_CONTROL:
		type = "Ctrl";
		if (speed == USB_SPEED_HIGH) 	/* uframes per NAK */
			interval = desc->bInterval;
		else
			interval = 0;
		dir = 'B';			/* ctrl is bidirectional */
		break;
	case USB_ENDPOINT_XFER_ISOC:
		type = "Isoc";
		interval = 1 << (desc->bInterval - 1);
		break;
	case USB_ENDPOINT_XFER_BULK:
		type = "Bulk";
		if (speed == USB_SPEED_HIGH && dir == 'O') /* uframes per NAK */
			interval = desc->bInterval;
		else
			interval = 0;
		break;
	case USB_ENDPOINT_XFER_INT:
		type = "Int.";
		if (speed == USB_SPEED_HIGH) {
			interval = 1 << (desc->bInterval - 1);
		} else
			interval = desc->bInterval;
		break;
	default:	/* "can't happen" */
		return start;
	}
	interval *= (speed == USB_SPEED_HIGH) ? 125 : 1000;
	if (interval % 1000)
		unit = 'u';
	else {
		unit = 'm';
		interval /= 1000;
	}

	start += sprintf(start, format_endpt, desc->bEndpointAddress, dir,
			 desc->bmAttributes, type,
			 (le16_to_cpu(desc->wMaxPacketSize) & 0x07ff) * bandwidth,
			 interval, unit);
	return start;
}

static char *usb_dump_interface_descriptor(char *start, char *end,
	const struct usb_interface_cache *intfc,
	const struct usb_interface *iface,
	int setno)
{
	const struct usb_interface_descriptor *desc = &intfc->altsetting[setno].desc;
	const char *driver_name = "";

	if (start > end)
		return start;
	down_read(&usb_bus_type.subsys.rwsem);
	if (iface)
		driver_name = (iface->dev.driver
				? iface->dev.driver->name
				: "(none)");
	start += sprintf(start, format_iface,
			 desc->bInterfaceNumber,
			 desc->bAlternateSetting,
			 desc->bNumEndpoints,
			 desc->bInterfaceClass,
			 class_decode(desc->bInterfaceClass),
			 desc->bInterfaceSubClass,
			 desc->bInterfaceProtocol,
			 driver_name);
	up_read(&usb_bus_type.subsys.rwsem);
	return start;
}

static char *usb_dump_interface(
	int speed,
	char *start,
	char *end,
	const struct usb_interface_cache *intfc,
	const struct usb_interface *iface,
	int setno
) {
	const struct usb_host_interface *desc = &intfc->altsetting[setno];
	int i;

	start = usb_dump_interface_descriptor(start, end, intfc, iface, setno);
	for (i = 0; i < desc->desc.bNumEndpoints; i++) {
		if (start > end)
			return start;
		start = usb_dump_endpoint_descriptor(speed,
				start, end, &desc->endpoint[i].desc);
	}
	return start;
}

/* TBD:
 * 0. TBDs
 * 1. marking active interface altsettings (code lists all, but should mark
 *    which ones are active, if any)
 */

static char *usb_dump_config_descriptor(char *start, char *end, const struct usb_config_descriptor *desc, int active)
{
	if (start > end)
		return start;
	start += sprintf(start, format_config,
			 active ? '*' : ' ',	/* mark active/actual/current cfg. */
			 desc->bNumInterfaces,
			 desc->bConfigurationValue,
			 desc->bmAttributes,
			 desc->bMaxPower * 2);
	return start;
}

static char *usb_dump_config (
	int speed,
	char *start,
	char *end,
	const struct usb_host_config *config,
	int active
)
{
	int i, j;
	struct usb_interface_cache *intfc;
	struct usb_interface *interface;

	if (start > end)
		return start;
	if (!config)		/* getting these some in 2.3.7; none in 2.3.6 */
		return start + sprintf(start, "(null Cfg. desc.)\n");
	start = usb_dump_config_descriptor(start, end, &config->desc, active);
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		intfc = config->intf_cache[i];
		interface = config->interface[i];
		for (j = 0; j < intfc->num_altsetting; j++) {
			if (start > end)
				return start;
			start = usb_dump_interface(speed,
				start, end, intfc, interface, j);
		}
	}
	return start;
}

/*
 * Dump the different USB descriptors.
 */
static char *usb_dump_device_descriptor(char *start, char *end, const struct usb_device_descriptor *desc)
{
	u16 bcdUSB = le16_to_cpu(desc->bcdUSB);
	u16 bcdDevice = le16_to_cpu(desc->bcdDevice);

	if (start > end)
		return start;
	start += sprintf (start, format_device1,
			  bcdUSB >> 8, bcdUSB & 0xff,
			  desc->bDeviceClass,
			  class_decode (desc->bDeviceClass),
			  desc->bDeviceSubClass,
			  desc->bDeviceProtocol,
			  desc->bMaxPacketSize0,
			  desc->bNumConfigurations);
	if (start > end)
		return start;
	start += sprintf(start, format_device2,
			 le16_to_cpu(desc->idVendor),
			 le16_to_cpu(desc->idProduct),
			 bcdDevice >> 8, bcdDevice & 0xff);
	return start;
}

/*
 * Dump the different strings that this device holds.
 */
static char *usb_dump_device_strings (char *start, char *end, struct usb_device *dev)
{
	if (start > end)
		return start;
	if (dev->manufacturer)
		start += sprintf(start, format_string_manufacturer, dev->manufacturer);
	if (start > end)
		goto out;
	if (dev->product)
		start += sprintf(start, format_string_product, dev->product);
	if (start > end)
		goto out;
#ifdef ALLOW_SERIAL_NUMBER
	if (dev->serial)
		start += sprintf(start, format_string_serialnumber, dev->serial);
#endif
 out:
	return start;
}

static char *usb_dump_desc(char *start, char *end, struct usb_device *dev)
{
	int i;

	if (start > end)
		return start;
		
	start = usb_dump_device_descriptor(start, end, &dev->descriptor);

	if (start > end)
		return start;
	
	start = usb_dump_device_strings (start, end, dev);

	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		if (start > end)
			return start;
		start = usb_dump_config(dev->speed,
				start, end, dev->config + i,
				/* active ? */
				(dev->config + i) == dev->actconfig);
	}
	return start;
}


#ifdef PROC_EXTRA /* TBD: may want to add this code later */

static char *usb_dump_hub_descriptor(char *start, char *end, const struct usb_hub_descriptor * desc)
{
	int leng = USB_DT_HUB_NONVAR_SIZE;
	unsigned char *ptr = (unsigned char *)desc;

	if (start > end)
		return start;
	start += sprintf(start, "Interface:");
	while (leng && start <= end) {
		start += sprintf(start, " %02x", *ptr);
		ptr++; leng--;
	}
	*start++ = '\n';
	return start;
}

static char *usb_dump_string(char *start, char *end, const struct usb_device *dev, char *id, int index)
{
	if (start > end)
		return start;
	start += sprintf(start, "Interface:");
	if (index <= dev->maxstring && dev->stringindex && dev->stringindex[index])
		start += sprintf(start, "%s: %.100s ", id, dev->stringindex[index]);
	return start;
}

#endif /* PROC_EXTRA */

/*****************************************************************/

/* This is a recursive function. Parameters:
 * buffer - the user-space buffer to write data into
 * nbytes - the maximum number of bytes to write
 * skip_bytes - the number of bytes to skip before writing anything
 * file_offset - the offset into the devices file on completion
 * The caller must own the device lock.
 */
static ssize_t usb_device_dump(char __user **buffer, size_t *nbytes, loff_t *skip_bytes, loff_t *file_offset,
				struct usb_device *usbdev, struct usb_bus *bus, int level, int index, int count)
{
	int chix;
	int ret, cnt = 0;
	int parent_devnum = 0;
	char *pages_start, *data_end, *speed;
	unsigned int length;
	ssize_t total_written = 0;
	
	/* don't bother with anything else if we're not writing any data */
	if (*nbytes <= 0)
		return 0;
	
	if (level > MAX_TOPO_LEVEL)
		return 0;
	/* allocate 2^1 pages = 8K (on i386); should be more than enough for one device */
        if (!(pages_start = (char*) __get_free_pages(GFP_KERNEL,1)))
                return -ENOMEM;
		
	if (usbdev->parent && usbdev->parent->devnum != -1)
		parent_devnum = usbdev->parent->devnum;
	/*
	 * So the root hub's parent is 0 and any device that is
	 * plugged into the root hub has a parent of 0.
	 */
	switch (usbdev->speed) {
	case USB_SPEED_LOW:
		speed = "1.5"; break;
	case USB_SPEED_UNKNOWN:		/* usb 1.1 root hub code */
	case USB_SPEED_FULL:
		speed = "12 "; break;
	case USB_SPEED_HIGH:
		speed = "480"; break;
	default:
		speed = "?? ";
	}
	data_end = pages_start + sprintf(pages_start, format_topo,
			bus->busnum, level, parent_devnum,
			index, count, usbdev->devnum,
			speed, usbdev->maxchild);
	/*
	 * level = topology-tier level;
	 * parent_devnum = parent device number;
	 * index = parent's connector number;
	 * count = device count at this level
	 */
	/* If this is the root hub, display the bandwidth information */
	if (level == 0) {
		int	max;

		/* high speed reserves 80%, full/low reserves 90% */
		if (usbdev->speed == USB_SPEED_HIGH)
			max = 800;
		else
			max = FRAME_TIME_MAX_USECS_ALLOC;

		/* report "average" periodic allocation over a microsecond.
		 * the schedules are actually bursty, HCDs need to deal with
		 * that and just compute/report this average.
		 */
		data_end += sprintf(data_end, format_bandwidth,
				bus->bandwidth_allocated, max,
				(100 * bus->bandwidth_allocated + max / 2)
					/ max,
			         bus->bandwidth_int_reqs,
				 bus->bandwidth_isoc_reqs);
	
	}
	data_end = usb_dump_desc(data_end, pages_start + (2 * PAGE_SIZE) - 256, usbdev);
	
	if (data_end > (pages_start + (2 * PAGE_SIZE) - 256))
		data_end += sprintf(data_end, "(truncated)\n");
	
	length = data_end - pages_start;
	/* if we can start copying some data to the user */
	if (length > *skip_bytes) {
		length -= *skip_bytes;
		if (length > *nbytes)
			length = *nbytes;
		if (copy_to_user(*buffer, pages_start + *skip_bytes, length)) {
			free_pages((unsigned long)pages_start, 1);
			return -EFAULT;
		}
		*nbytes -= length;
		*file_offset += length;
		total_written += length;
		*buffer += length;
		*skip_bytes = 0;
	} else
		*skip_bytes -= length;
	
	free_pages((unsigned long)pages_start, 1);
	
	/* Now look at all of this device's children. */
	for (chix = 0; chix < usbdev->maxchild; chix++) {
		struct usb_device *childdev = usbdev->children[chix];

		if (childdev) {
			usb_lock_device(childdev);
			ret = usb_device_dump(buffer, nbytes, skip_bytes, file_offset, childdev,
					bus, level + 1, chix, ++cnt);
			usb_unlock_device(childdev);
			if (ret == -EFAULT)
				return total_written;
			total_written += ret;
		}
	}
	return total_written;
}

static ssize_t usb_device_read(struct file *file, char __user *buf, size_t nbytes, loff_t *ppos)
{
	struct usb_bus *bus;
	ssize_t ret, total_written = 0;
	loff_t skip_bytes = *ppos;

	if (*ppos < 0)
		return -EINVAL;
	if (nbytes <= 0)
		return 0;
	if (!access_ok(VERIFY_WRITE, buf, nbytes))
		return -EFAULT;

	mutex_lock(&usb_bus_list_lock);
	/* print devices for all busses */
	list_for_each_entry(bus, &usb_bus_list, bus_list) {
		/* recurse through all children of the root hub */
		if (!bus->root_hub)
			continue;
		usb_lock_device(bus->root_hub);
		ret = usb_device_dump(&buf, &nbytes, &skip_bytes, ppos, bus->root_hub, bus, 0, 0, 0);
		usb_unlock_device(bus->root_hub);
		if (ret < 0) {
			mutex_unlock(&usb_bus_list_lock);
			return ret;
		}
		total_written += ret;
	}
	mutex_unlock(&usb_bus_list_lock);
	return total_written;
}

/* Kernel lock for "lastev" protection */
static unsigned int usb_device_poll(struct file *file, struct poll_table_struct *wait)
{
	struct usb_device_status *st = file->private_data;
	unsigned int mask = 0;

	lock_kernel();
	if (!st) {
		st = kmalloc(sizeof(struct usb_device_status), GFP_KERNEL);
		if (!st) {
			unlock_kernel();
			return POLLIN;
		}

		/* we may have dropped BKL - need to check for having lost the race */
		if (file->private_data) {
			kfree(st);
			st = file->private_data;
			goto lost_race;
		}

		/*
		 * need to prevent the module from being unloaded, since
		 * proc_unregister does not call the release method and
		 * we would have a memory leak
		 */
		st->lastev = conndiscevcnt;
		file->private_data = st;
		mask = POLLIN;
	}
lost_race:
	if (file->f_mode & FMODE_READ)
                poll_wait(file, &deviceconndiscwq, wait);
	if (st->lastev != conndiscevcnt)
		mask |= POLLIN;
	st->lastev = conndiscevcnt;
	unlock_kernel();
	return mask;
}

static int usb_device_open(struct inode *inode, struct file *file)
{
        file->private_data = NULL;
        return 0;
}

static int usb_device_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;
        return 0;
}

static loff_t usb_device_lseek(struct file * file, loff_t offset, int orig)
{
	loff_t ret;

	lock_kernel();

	switch (orig) {
	case 0:
		file->f_pos = offset;
		ret = file->f_pos;
		break;
	case 1:
		file->f_pos += offset;
		ret = file->f_pos;
		break;
	case 2:
	default:
		ret = -EINVAL;
	}

	unlock_kernel();
	return ret;
}

const struct file_operations usbfs_devices_fops = {
	.llseek =	usb_device_lseek,
	.read =		usb_device_read,
	.poll =		usb_device_poll,
	.open =		usb_device_open,
	.release =	usb_device_release,
};
