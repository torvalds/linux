/*
 *	dscore.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/usb.h>

#include "dscore.h"

static struct usb_device_id ds_id_table [] = {
	{ USB_DEVICE(0x04fa, 0x2490) },
	{ },
};
MODULE_DEVICE_TABLE(usb, ds_id_table);

static int ds_probe(struct usb_interface *, const struct usb_device_id *);
static void ds_disconnect(struct usb_interface *);

int ds_touch_bit(struct ds_device *, u8, u8 *);
int ds_read_byte(struct ds_device *, u8 *);
int ds_read_bit(struct ds_device *, u8 *);
int ds_write_byte(struct ds_device *, u8);
int ds_write_bit(struct ds_device *, u8);
static int ds_start_pulse(struct ds_device *, int);
int ds_reset(struct ds_device *, struct ds_status *);
struct ds_device * ds_get_device(void);
void ds_put_device(struct ds_device *);

static inline void ds_dump_status(unsigned char *, unsigned char *, int);
static int ds_send_control(struct ds_device *, u16, u16);
static int ds_send_control_mode(struct ds_device *, u16, u16);
static int ds_send_control_cmd(struct ds_device *, u16, u16);


static struct usb_driver ds_driver = {
	.name =		"DS9490R",
	.probe =	ds_probe,
	.disconnect =	ds_disconnect,
	.id_table =	ds_id_table,
};

static struct ds_device *ds_dev;

struct ds_device * ds_get_device(void)
{
	if (ds_dev)
		atomic_inc(&ds_dev->refcnt);
	return ds_dev;
}

void ds_put_device(struct ds_device *dev)
{
	atomic_dec(&dev->refcnt);
}

static int ds_send_control_cmd(struct ds_device *dev, u16 value, u16 index)
{
	int err;

	err = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, dev->ep[EP_CONTROL]),
			CONTROL_CMD, 0x40, value, index, NULL, 0, 1000);
	if (err < 0) {
		printk(KERN_ERR "Failed to send command control message %x.%x: err=%d.\n",
				value, index, err);
		return err;
	}

	return err;
}

static int ds_send_control_mode(struct ds_device *dev, u16 value, u16 index)
{
	int err;

	err = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, dev->ep[EP_CONTROL]),
			MODE_CMD, 0x40, value, index, NULL, 0, 1000);
	if (err < 0) {
		printk(KERN_ERR "Failed to send mode control message %x.%x: err=%d.\n",
				value, index, err);
		return err;
	}

	return err;
}

static int ds_send_control(struct ds_device *dev, u16 value, u16 index)
{
	int err;

	err = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, dev->ep[EP_CONTROL]),
			COMM_CMD, 0x40, value, index, NULL, 0, 1000);
	if (err < 0) {
		printk(KERN_ERR "Failed to send control message %x.%x: err=%d.\n",
				value, index, err);
		return err;
	}

	return err;
}

static inline void ds_dump_status(unsigned char *buf, unsigned char *str, int off)
{
	printk("%45s: %8x\n", str, buf[off]);
}

static int ds_recv_status_nodump(struct ds_device *dev, struct ds_status *st,
				 unsigned char *buf, int size)
{
	int count, err;

	memset(st, 0, sizeof(st));

	count = 0;
	err = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, dev->ep[EP_STATUS]), buf, size, &count, 100);
	if (err < 0) {
		printk(KERN_ERR "Failed to read 1-wire data from 0x%x: err=%d.\n", dev->ep[EP_STATUS], err);
		return err;
	}

	if (count >= sizeof(*st))
		memcpy(st, buf, sizeof(*st));

	return count;
}

static int ds_recv_status(struct ds_device *dev, struct ds_status *st)
{
	unsigned char buf[64];
	int count, err = 0, i;

	memcpy(st, buf, sizeof(*st));

	count = ds_recv_status_nodump(dev, st, buf, sizeof(buf));
	if (count < 0)
		return err;

	printk("0x%x: count=%d, status: ", dev->ep[EP_STATUS], count);
	for (i=0; i<count; ++i)
		printk("%02x ", buf[i]);
	printk("\n");

	if (count >= 16) {
		ds_dump_status(buf, "enable flag", 0);
		ds_dump_status(buf, "1-wire speed", 1);
		ds_dump_status(buf, "strong pullup duration", 2);
		ds_dump_status(buf, "programming pulse duration", 3);
		ds_dump_status(buf, "pulldown slew rate control", 4);
		ds_dump_status(buf, "write-1 low time", 5);
		ds_dump_status(buf, "data sample offset/write-0 recovery time", 6);
		ds_dump_status(buf, "reserved (test register)", 7);
		ds_dump_status(buf, "device status flags", 8);
		ds_dump_status(buf, "communication command byte 1", 9);
		ds_dump_status(buf, "communication command byte 2", 10);
		ds_dump_status(buf, "communication command buffer status", 11);
		ds_dump_status(buf, "1-wire data output buffer status", 12);
		ds_dump_status(buf, "1-wire data input buffer status", 13);
		ds_dump_status(buf, "reserved", 14);
		ds_dump_status(buf, "reserved", 15);
	}

	memcpy(st, buf, sizeof(*st));

	if (st->status & ST_EPOF) {
		printk(KERN_INFO "Resetting device after ST_EPOF.\n");
		err = ds_send_control_cmd(dev, CTL_RESET_DEVICE, 0);
		if (err)
			return err;
		count = ds_recv_status_nodump(dev, st, buf, sizeof(buf));
		if (count < 0)
			return err;
	}
#if 0
	if (st->status & ST_IDLE) {
		printk(KERN_INFO "Resetting pulse after ST_IDLE.\n");
		err = ds_start_pulse(dev, PULLUP_PULSE_DURATION);
		if (err)
			return err;
	}
#endif

	return err;
}

static int ds_recv_data(struct ds_device *dev, unsigned char *buf, int size)
{
	int count, err;
	struct ds_status st;

	count = 0;
	err = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, dev->ep[EP_DATA_IN]),
				buf, size, &count, 1000);
	if (err < 0) {
		printk(KERN_INFO "Clearing ep0x%x.\n", dev->ep[EP_DATA_IN]);
		usb_clear_halt(dev->udev, usb_rcvbulkpipe(dev->udev, dev->ep[EP_DATA_IN]));
		ds_recv_status(dev, &st);
		return err;
	}

#if 0
	{
		int i;

		printk("%s: count=%d: ", __func__, count);
		for (i=0; i<count; ++i)
			printk("%02x ", buf[i]);
		printk("\n");
	}
#endif
	return count;
}

static int ds_send_data(struct ds_device *dev, unsigned char *buf, int len)
{
	int count, err;

	count = 0;
	err = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, dev->ep[EP_DATA_OUT]), buf, len, &count, 1000);
	if (err < 0) {
		printk(KERN_ERR "Failed to read 1-wire data from 0x02: err=%d.\n", err);
		return err;
	}

	return err;
}

#if 0

int ds_stop_pulse(struct ds_device *dev, int limit)
{
	struct ds_status st;
	int count = 0, err = 0;
	u8 buf[0x20];

	do {
		err = ds_send_control(dev, CTL_HALT_EXE_IDLE, 0);
		if (err)
			break;
		err = ds_send_control(dev, CTL_RESUME_EXE, 0);
		if (err)
			break;
		err = ds_recv_status_nodump(dev, &st, buf, sizeof(buf));
		if (err)
			break;

		if ((st.status & ST_SPUA) == 0) {
			err = ds_send_control_mode(dev, MOD_PULSE_EN, 0);
			if (err)
				break;
		}
	} while(++count < limit);

	return err;
}

int ds_detect(struct ds_device *dev, struct ds_status *st)
{
	int err;

	err = ds_send_control_cmd(dev, CTL_RESET_DEVICE, 0);
	if (err)
		return err;

	err = ds_send_control(dev, COMM_SET_DURATION | COMM_IM, 0);
	if (err)
		return err;

	err = ds_send_control(dev, COMM_SET_DURATION | COMM_IM | COMM_TYPE, 0x40);
	if (err)
		return err;

	err = ds_send_control_mode(dev, MOD_PULSE_EN, PULSE_PROG);
	if (err)
		return err;

	err = ds_recv_status(dev, st);

	return err;
}

#endif  /*  0  */

static int ds_wait_status(struct ds_device *dev, struct ds_status *st)
{
	u8 buf[0x20];
	int err, count = 0;

	do {
		err = ds_recv_status_nodump(dev, st, buf, sizeof(buf));
#if 0
		if (err >= 0) {
			int i;
			printk("0x%x: count=%d, status: ", dev->ep[EP_STATUS], err);
			for (i=0; i<err; ++i)
				printk("%02x ", buf[i]);
			printk("\n");
		}
#endif
	} while(!(buf[0x08] & 0x20) && !(err < 0) && ++count < 100);


	if (((err > 16) && (buf[0x10] & 0x01)) || count >= 100 || err < 0) {
		ds_recv_status(dev, st);
		return -1;
	} else
		return 0;
}

int ds_reset(struct ds_device *dev, struct ds_status *st)
{
	int err;

	//err = ds_send_control(dev, COMM_1_WIRE_RESET | COMM_F | COMM_IM | COMM_SE, SPEED_FLEXIBLE);
	err = ds_send_control(dev, 0x43, SPEED_NORMAL);
	if (err)
		return err;

	ds_wait_status(dev, st);
#if 0
	if (st->command_buffer_status) {
		printk(KERN_INFO "Short circuit.\n");
		return -EIO;
	}
#endif

	return 0;
}

#if 0
int ds_set_speed(struct ds_device *dev, int speed)
{
	int err;

	if (speed != SPEED_NORMAL && speed != SPEED_FLEXIBLE && speed != SPEED_OVERDRIVE)
		return -EINVAL;

	if (speed != SPEED_OVERDRIVE)
		speed = SPEED_FLEXIBLE;

	speed &= 0xff;

	err = ds_send_control_mode(dev, MOD_1WIRE_SPEED, speed);
	if (err)
		return err;

	return err;
}
#endif  /*  0  */

static int ds_start_pulse(struct ds_device *dev, int delay)
{
	int err;
	u8 del = 1 + (u8)(delay >> 4);
	struct ds_status st;

#if 0
	err = ds_stop_pulse(dev, 10);
	if (err)
		return err;

	err = ds_send_control_mode(dev, MOD_PULSE_EN, PULSE_SPUE);
	if (err)
		return err;
#endif
	err = ds_send_control(dev, COMM_SET_DURATION | COMM_IM, del);
	if (err)
		return err;

	err = ds_send_control(dev, COMM_PULSE | COMM_IM | COMM_F, 0);
	if (err)
		return err;

	mdelay(delay);

	ds_wait_status(dev, &st);

	return err;
}

int ds_touch_bit(struct ds_device *dev, u8 bit, u8 *tbit)
{
	int err, count;
	struct ds_status st;
	u16 value = (COMM_BIT_IO | COMM_IM) | ((bit) ? COMM_D : 0);
	u16 cmd;

	err = ds_send_control(dev, value, 0);
	if (err)
		return err;

	count = 0;
	do {
		err = ds_wait_status(dev, &st);
		if (err)
			return err;

		cmd = st.command0 | (st.command1 << 8);
	} while (cmd != value && ++count < 10);

	if (err < 0 || count >= 10) {
		printk(KERN_ERR "Failed to obtain status.\n");
		return -EINVAL;
	}

	err = ds_recv_data(dev, tbit, sizeof(*tbit));
	if (err < 0)
		return err;

	return 0;
}

int ds_write_bit(struct ds_device *dev, u8 bit)
{
	int err;
	struct ds_status st;

	err = ds_send_control(dev, COMM_BIT_IO | COMM_IM | (bit) ? COMM_D : 0, 0);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	return 0;
}

int ds_write_byte(struct ds_device *dev, u8 byte)
{
	int err;
	struct ds_status st;
	u8 rbyte;

	err = ds_send_control(dev, COMM_BYTE_IO | COMM_IM | COMM_SPU, byte);
	if (err)
		return err;

	err = ds_wait_status(dev, &st);
	if (err)
		return err;

	err = ds_recv_data(dev, &rbyte, sizeof(rbyte));
	if (err < 0)
		return err;

	ds_start_pulse(dev, PULLUP_PULSE_DURATION);

	return !(byte == rbyte);
}

int ds_read_bit(struct ds_device *dev, u8 *bit)
{
	int err;

	err = ds_send_control_mode(dev, MOD_PULSE_EN, PULSE_SPUE);
	if (err)
		return err;

	err = ds_send_control(dev, COMM_BIT_IO | COMM_IM | COMM_SPU | COMM_D, 0);
	if (err)
		return err;

	err = ds_recv_data(dev, bit, sizeof(*bit));
	if (err < 0)
		return err;

	return 0;
}

int ds_read_byte(struct ds_device *dev, u8 *byte)
{
	int err;
	struct ds_status st;

	err = ds_send_control(dev, COMM_BYTE_IO | COMM_IM , 0xff);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	err = ds_recv_data(dev, byte, sizeof(*byte));
	if (err < 0)
		return err;

	return 0;
}

int ds_read_block(struct ds_device *dev, u8 *buf, int len)
{
	struct ds_status st;
	int err;

	if (len > 64*1024)
		return -E2BIG;

	memset(buf, 0xFF, len);

	err = ds_send_data(dev, buf, len);
	if (err < 0)
		return err;

	err = ds_send_control(dev, COMM_BLOCK_IO | COMM_IM | COMM_SPU, len);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	memset(buf, 0x00, len);
	err = ds_recv_data(dev, buf, len);

	return err;
}

int ds_write_block(struct ds_device *dev, u8 *buf, int len)
{
	int err;
	struct ds_status st;

	err = ds_send_data(dev, buf, len);
	if (err < 0)
		return err;

	ds_wait_status(dev, &st);

	err = ds_send_control(dev, COMM_BLOCK_IO | COMM_IM | COMM_SPU, len);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	err = ds_recv_data(dev, buf, len);
	if (err < 0)
		return err;

	ds_start_pulse(dev, PULLUP_PULSE_DURATION);

	return !(err == len);
}

#if 0

int ds_search(struct ds_device *dev, u64 init, u64 *buf, u8 id_number, int conditional_search)
{
	int err;
	u16 value, index;
	struct ds_status st;

	memset(buf, 0, sizeof(buf));

	err = ds_send_data(ds_dev, (unsigned char *)&init, 8);
	if (err)
		return err;

	ds_wait_status(ds_dev, &st);

	value = COMM_SEARCH_ACCESS | COMM_IM | COMM_SM | COMM_F | COMM_RTS;
	index = (conditional_search ? 0xEC : 0xF0) | (id_number << 8);
	err = ds_send_control(ds_dev, value, index);
	if (err)
		return err;

	ds_wait_status(ds_dev, &st);

	err = ds_recv_data(ds_dev, (unsigned char *)buf, 8*id_number);
	if (err < 0)
		return err;

	return err/8;
}

int ds_match_access(struct ds_device *dev, u64 init)
{
	int err;
	struct ds_status st;

	err = ds_send_data(dev, (unsigned char *)&init, sizeof(init));
	if (err)
		return err;

	ds_wait_status(dev, &st);

	err = ds_send_control(dev, COMM_MATCH_ACCESS | COMM_IM | COMM_RST, 0x0055);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	return 0;
}

int ds_set_path(struct ds_device *dev, u64 init)
{
	int err;
	struct ds_status st;
	u8 buf[9];

	memcpy(buf, &init, 8);
	buf[8] = BRANCH_MAIN;

	err = ds_send_data(dev, buf, sizeof(buf));
	if (err)
		return err;

	ds_wait_status(dev, &st);

	err = ds_send_control(dev, COMM_SET_PATH | COMM_IM | COMM_RST, 0);
	if (err)
		return err;

	ds_wait_status(dev, &st);

	return 0;
}

#endif  /*  0  */

static int ds_probe(struct usb_interface *intf,
		    const struct usb_device_id *udev_id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct usb_host_interface *iface_desc;
	int i, err;

	ds_dev = kmalloc(sizeof(struct ds_device), GFP_KERNEL);
	if (!ds_dev) {
		printk(KERN_INFO "Failed to allocate new DS9490R structure.\n");
		return -ENOMEM;
	}

	ds_dev->udev = usb_get_dev(udev);
	usb_set_intfdata(intf, ds_dev);

	err = usb_set_interface(ds_dev->udev, intf->altsetting[0].desc.bInterfaceNumber, 3);
	if (err) {
		printk(KERN_ERR "Failed to set alternative setting 3 for %d interface: err=%d.\n",
				intf->altsetting[0].desc.bInterfaceNumber, err);
		return err;
	}

	err = usb_reset_configuration(ds_dev->udev);
	if (err) {
		printk(KERN_ERR "Failed to reset configuration: err=%d.\n", err);
		return err;
	}

	iface_desc = &intf->altsetting[0];
	if (iface_desc->desc.bNumEndpoints != NUM_EP-1) {
		printk(KERN_INFO "Num endpoints=%d. It is not DS9490R.\n", iface_desc->desc.bNumEndpoints);
		return -ENODEV;
	}

	atomic_set(&ds_dev->refcnt, 0);
	memset(ds_dev->ep, 0, sizeof(ds_dev->ep));

	/*
	 * This loop doesn'd show control 0 endpoint,
	 * so we will fill only 1-3 endpoints entry.
	 */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		ds_dev->ep[i+1] = endpoint->bEndpointAddress;

		printk("%d: addr=%x, size=%d, dir=%s, type=%x\n",
			i, endpoint->bEndpointAddress, le16_to_cpu(endpoint->wMaxPacketSize),
			(endpoint->bEndpointAddress & USB_DIR_IN)?"IN":"OUT",
			endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
	}

#if 0
	{
		int err, i;
		u64 buf[3];
		u64 init=0xb30000002078ee81ull;
		struct ds_status st;

		ds_reset(ds_dev, &st);
		err = ds_search(ds_dev, init, buf, 3, 0);
		if (err < 0)
			return err;
		for (i=0; i<err; ++i)
			printk("%d: %llx\n", i, buf[i]);

		printk("Resetting...\n");
		ds_reset(ds_dev, &st);
		printk("Setting path for %llx.\n", init);
		err = ds_set_path(ds_dev, init);
		if (err)
			return err;
		printk("Calling MATCH_ACCESS.\n");
		err = ds_match_access(ds_dev, init);
		if (err)
			return err;

		printk("Searching the bus...\n");
		err = ds_search(ds_dev, init, buf, 3, 0);

		printk("ds_search() returned %d\n", err);

		if (err < 0)
			return err;
		for (i=0; i<err; ++i)
			printk("%d: %llx\n", i, buf[i]);

		return 0;
	}
#endif

	return 0;
}

static void ds_disconnect(struct usb_interface *intf)
{
	struct ds_device *dev;

	dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);

	while (atomic_read(&dev->refcnt)) {
		printk(KERN_INFO "Waiting for DS to become free: refcnt=%d.\n",
				atomic_read(&dev->refcnt));

		if (msleep_interruptible(1000))
			flush_signals(current);
	}

	usb_put_dev(dev->udev);
	kfree(dev);
	ds_dev = NULL;
}

static int ds_init(void)
{
	int err;

	err = usb_register(&ds_driver);
	if (err) {
		printk(KERN_INFO "Failed to register DS9490R USB device: err=%d.\n", err);
		return err;
	}

	return 0;
}

static void ds_fini(void)
{
	usb_deregister(&ds_driver);
}

module_init(ds_init);
module_exit(ds_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");

EXPORT_SYMBOL(ds_touch_bit);
EXPORT_SYMBOL(ds_read_byte);
EXPORT_SYMBOL(ds_read_bit);
EXPORT_SYMBOL(ds_read_block);
EXPORT_SYMBOL(ds_write_byte);
EXPORT_SYMBOL(ds_write_bit);
EXPORT_SYMBOL(ds_write_block);
EXPORT_SYMBOL(ds_reset);
EXPORT_SYMBOL(ds_get_device);
EXPORT_SYMBOL(ds_put_device);

/*
 * This functions can be used for EEPROM programming,
 * when driver will be included into mainline this will
 * require uncommenting.
 */
#if 0
EXPORT_SYMBOL(ds_start_pulse);
EXPORT_SYMBOL(ds_set_speed);
EXPORT_SYMBOL(ds_detect);
EXPORT_SYMBOL(ds_stop_pulse);
EXPORT_SYMBOL(ds_search);
#endif
