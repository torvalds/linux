/*
 * g_serial.c -- USB gadget serial driver
 *
 * Copyright 2003 (C) Al Borchers (alborchers@steinerpoint.com)
 *
 * This code is based in part on the Gadget Zero driver, which
 * is Copyright (C) 2003 by David Brownell, all rights reserved.
 *
 * This code also borrows from usbserial.c, which is
 * Copyright (C) 1999 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2000 Peter Berger (pberger@brimson.com)
 * Copyright (C) 2000 Al Borchers (alborchers@steinerpoint.com)
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * either version 2 of that License or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/utsname.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/uaccess.h>

#include <linux/usb_ch9.h>
#include <linux/usb_cdc.h>
#include <linux/usb_gadget.h>

#include "gadget_chips.h"


/* Wait Cond */

#define __wait_cond_interruptible(wq, condition, lock, flags, ret)	\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			spin_unlock_irqrestore(lock, flags);		\
			schedule();					\
			spin_lock_irqsave(lock, flags);			\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)
	
#define wait_cond_interruptible(wq, condition, lock, flags)		\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__wait_cond_interruptible(wq, condition, lock, flags,	\
						__ret);			\
	__ret;								\
})

#define __wait_cond_interruptible_timeout(wq, condition, lock, flags, 	\
						timeout, ret)		\
do {									\
	signed long __timeout = timeout;				\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (__timeout == 0)					\
			break;						\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			spin_unlock_irqrestore(lock, flags);		\
			__timeout = schedule_timeout(__timeout);	\
			spin_lock_irqsave(lock, flags);			\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)
	
#define wait_cond_interruptible_timeout(wq, condition, lock, flags,	\
						timeout)		\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__wait_cond_interruptible_timeout(wq, condition, lock,	\
						flags, timeout, __ret);	\
	__ret;								\
})


/* Defines */

#define GS_VERSION_STR			"v2.0"
#define GS_VERSION_NUM			0x0200

#define GS_LONG_NAME			"Gadget Serial"
#define GS_SHORT_NAME			"g_serial"

#define GS_MAJOR			127
#define GS_MINOR_START			0

#define GS_NUM_PORTS			16

#define GS_NUM_CONFIGS			1
#define GS_NO_CONFIG_ID			0
#define GS_BULK_CONFIG_ID		1
#define GS_ACM_CONFIG_ID		2

#define GS_MAX_NUM_INTERFACES		2
#define GS_BULK_INTERFACE_ID		0
#define GS_CONTROL_INTERFACE_ID		0
#define GS_DATA_INTERFACE_ID		1

#define GS_MAX_DESC_LEN			256

#define GS_DEFAULT_READ_Q_SIZE		32
#define GS_DEFAULT_WRITE_Q_SIZE		32

#define GS_DEFAULT_WRITE_BUF_SIZE	8192
#define GS_TMP_BUF_SIZE			8192

#define GS_CLOSE_TIMEOUT		15

#define GS_DEFAULT_USE_ACM		0

#define GS_DEFAULT_DTE_RATE		9600
#define GS_DEFAULT_DATA_BITS		8
#define GS_DEFAULT_PARITY		USB_CDC_NO_PARITY
#define GS_DEFAULT_CHAR_FORMAT		USB_CDC_1_STOP_BITS

/* select highspeed/fullspeed, hiding highspeed if not configured */
#ifdef CONFIG_USB_GADGET_DUALSPEED
#define GS_SPEED_SELECT(is_hs,hs,fs) ((is_hs) ? (hs) : (fs))
#else
#define GS_SPEED_SELECT(is_hs,hs,fs) (fs)
#endif /* CONFIG_USB_GADGET_DUALSPEED */

/* debug settings */
#ifdef GS_DEBUG
static int debug = 1;

#define gs_debug(format, arg...) \
	do { if (debug) printk(KERN_DEBUG format, ## arg); } while(0)
#define gs_debug_level(level, format, arg...) \
	do { if (debug>=level) printk(KERN_DEBUG format, ## arg); } while(0)

#else

#define gs_debug(format, arg...) \
	do { } while(0)
#define gs_debug_level(level, format, arg...) \
	do { } while(0)

#endif /* GS_DEBUG */

/* Thanks to NetChip Technologies for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define GS_VENDOR_ID			0x0525	/* NetChip */
#define GS_PRODUCT_ID			0xa4a6	/* Linux-USB Serial Gadget */
#define GS_CDC_PRODUCT_ID		0xa4a7	/* ... as CDC-ACM */

#define GS_LOG2_NOTIFY_INTERVAL		5	/* 1 << 5 == 32 msec */
#define GS_NOTIFY_MAXPACKET		8


/* Structures */

struct gs_dev;

/* circular buffer */
struct gs_buf {
	unsigned int		buf_size;
	char			*buf_buf;
	char			*buf_get;
	char			*buf_put;
};

/* list of requests */
struct gs_req_entry {
	struct list_head	re_entry;
	struct usb_request	*re_req;
};

/* the port structure holds info for each port, one for each minor number */
struct gs_port {
	struct gs_dev 		*port_dev;	/* pointer to device struct */
	struct tty_struct	*port_tty;	/* pointer to tty struct */
	spinlock_t		port_lock;
	int 			port_num;
	int			port_open_count;
	int			port_in_use;	/* open/close in progress */
	wait_queue_head_t	port_write_wait;/* waiting to write */
	struct gs_buf		*port_write_buf;
	struct usb_cdc_line_coding	port_line_coding;
};

/* the device structure holds info for the USB device */
struct gs_dev {
	struct usb_gadget	*dev_gadget;	/* gadget device pointer */
	spinlock_t		dev_lock;	/* lock for set/reset config */
	int			dev_config;	/* configuration number */
	struct usb_ep		*dev_notify_ep;	/* address of notify endpoint */
	struct usb_ep		*dev_in_ep;	/* address of in endpoint */
	struct usb_ep		*dev_out_ep;	/* address of out endpoint */
	struct usb_endpoint_descriptor		/* descriptor of notify ep */
				*dev_notify_ep_desc;
	struct usb_endpoint_descriptor		/* descriptor of in endpoint */
				*dev_in_ep_desc;
	struct usb_endpoint_descriptor		/* descriptor of out endpoint */
				*dev_out_ep_desc;
	struct usb_request	*dev_ctrl_req;	/* control request */
	struct list_head	dev_req_list;	/* list of write requests */
	int			dev_sched_port;	/* round robin port scheduled */
	struct gs_port		*dev_port[GS_NUM_PORTS]; /* the ports */
};


/* Functions */

/* module */
static int __init gs_module_init(void);
static void __exit gs_module_exit(void);

/* tty driver */
static int gs_open(struct tty_struct *tty, struct file *file);
static void gs_close(struct tty_struct *tty, struct file *file);
static int gs_write(struct tty_struct *tty, 
	const unsigned char *buf, int count);
static void gs_put_char(struct tty_struct *tty, unsigned char ch);
static void gs_flush_chars(struct tty_struct *tty);
static int gs_write_room(struct tty_struct *tty);
static int gs_chars_in_buffer(struct tty_struct *tty);
static void gs_throttle(struct tty_struct * tty);
static void gs_unthrottle(struct tty_struct * tty);
static void gs_break(struct tty_struct *tty, int break_state);
static int  gs_ioctl(struct tty_struct *tty, struct file *file,
	unsigned int cmd, unsigned long arg);
static void gs_set_termios(struct tty_struct *tty, struct termios *old);

static int gs_send(struct gs_dev *dev);
static int gs_send_packet(struct gs_dev *dev, char *packet,
	unsigned int size);
static int gs_recv_packet(struct gs_dev *dev, char *packet,
	unsigned int size);
static void gs_read_complete(struct usb_ep *ep, struct usb_request *req);
static void gs_write_complete(struct usb_ep *ep, struct usb_request *req);

/* gadget driver */
static int gs_bind(struct usb_gadget *gadget);
static void gs_unbind(struct usb_gadget *gadget);
static int gs_setup(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl);
static int gs_setup_standard(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl);
static int gs_setup_class(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl);
static void gs_setup_complete(struct usb_ep *ep, struct usb_request *req);
static void gs_disconnect(struct usb_gadget *gadget);
static int gs_set_config(struct gs_dev *dev, unsigned config);
static void gs_reset_config(struct gs_dev *dev);
static int gs_build_config_buf(u8 *buf, enum usb_device_speed speed,
		u8 type, unsigned int index, int is_otg);

static struct usb_request *gs_alloc_req(struct usb_ep *ep, unsigned int len,
	gfp_t kmalloc_flags);
static void gs_free_req(struct usb_ep *ep, struct usb_request *req);

static struct gs_req_entry *gs_alloc_req_entry(struct usb_ep *ep, unsigned len,
	gfp_t kmalloc_flags);
static void gs_free_req_entry(struct usb_ep *ep, struct gs_req_entry *req);

static int gs_alloc_ports(struct gs_dev *dev, gfp_t kmalloc_flags);
static void gs_free_ports(struct gs_dev *dev);

/* circular buffer */
static struct gs_buf *gs_buf_alloc(unsigned int size, gfp_t kmalloc_flags);
static void gs_buf_free(struct gs_buf *gb);
static void gs_buf_clear(struct gs_buf *gb);
static unsigned int gs_buf_data_avail(struct gs_buf *gb);
static unsigned int gs_buf_space_avail(struct gs_buf *gb);
static unsigned int gs_buf_put(struct gs_buf *gb, const char *buf,
	unsigned int count);
static unsigned int gs_buf_get(struct gs_buf *gb, char *buf,
	unsigned int count);

/* external functions */
extern int net2280_set_fifo_mode(struct usb_gadget *gadget, int mode);


/* Globals */

static struct gs_dev *gs_device;

static const char *EP_IN_NAME;
static const char *EP_OUT_NAME;
static const char *EP_NOTIFY_NAME;

static struct semaphore	gs_open_close_sem[GS_NUM_PORTS];

static unsigned int read_q_size = GS_DEFAULT_READ_Q_SIZE;
static unsigned int write_q_size = GS_DEFAULT_WRITE_Q_SIZE;

static unsigned int write_buf_size = GS_DEFAULT_WRITE_BUF_SIZE;

static unsigned int use_acm = GS_DEFAULT_USE_ACM;


/* tty driver struct */
static struct tty_operations gs_tty_ops = {
	.open =			gs_open,
	.close =		gs_close,
	.write =		gs_write,
	.put_char =		gs_put_char,
	.flush_chars =		gs_flush_chars,
	.write_room =		gs_write_room,
	.ioctl =		gs_ioctl,
	.set_termios =		gs_set_termios,
	.throttle =		gs_throttle,
	.unthrottle =		gs_unthrottle,
	.break_ctl =		gs_break,
	.chars_in_buffer =	gs_chars_in_buffer,
};
static struct tty_driver *gs_tty_driver;

/* gadget driver struct */
static struct usb_gadget_driver gs_gadget_driver = {
#ifdef CONFIG_USB_GADGET_DUALSPEED
	.speed =		USB_SPEED_HIGH,
#else
	.speed =		USB_SPEED_FULL,
#endif /* CONFIG_USB_GADGET_DUALSPEED */
	.function =		GS_LONG_NAME,
	.bind =			gs_bind,
	.unbind =		__exit_p(gs_unbind),
	.setup =		gs_setup,
	.disconnect =		gs_disconnect,
	.driver = {
		.name =		GS_SHORT_NAME,
	},
};


/* USB descriptors */

#define GS_MANUFACTURER_STR_ID	1
#define GS_PRODUCT_STR_ID	2
#define GS_SERIAL_STR_ID	3
#define GS_BULK_CONFIG_STR_ID	4
#define GS_ACM_CONFIG_STR_ID	5
#define GS_CONTROL_STR_ID	6
#define GS_DATA_STR_ID		7

/* static strings, in UTF-8 */
static char manufacturer[50];
static struct usb_string gs_strings[] = {
	{ GS_MANUFACTURER_STR_ID, manufacturer },
	{ GS_PRODUCT_STR_ID, GS_LONG_NAME },
	{ GS_SERIAL_STR_ID, "0" },
	{ GS_BULK_CONFIG_STR_ID, "Gadget Serial Bulk" },
	{ GS_ACM_CONFIG_STR_ID, "Gadget Serial CDC ACM" },
	{ GS_CONTROL_STR_ID, "Gadget Serial Control" },
	{ GS_DATA_STR_ID, "Gadget Serial Data" },
	{  } /* end of list */
};

static struct usb_gadget_strings gs_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		gs_strings,
};

static struct usb_device_descriptor gs_device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		__constant_cpu_to_le16(0x0200),
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	.idVendor =		__constant_cpu_to_le16(GS_VENDOR_ID),
	.idProduct =		__constant_cpu_to_le16(GS_PRODUCT_ID),
	.iManufacturer =	GS_MANUFACTURER_STR_ID,
	.iProduct =		GS_PRODUCT_STR_ID,
	.iSerialNumber =	GS_SERIAL_STR_ID,
	.bNumConfigurations =	GS_NUM_CONFIGS,
};

static struct usb_otg_descriptor gs_otg_descriptor = {
	.bLength =		sizeof(gs_otg_descriptor),
	.bDescriptorType =	USB_DT_OTG,
	.bmAttributes =		USB_OTG_SRP,
};

static struct usb_config_descriptor gs_bulk_config_desc = {
	.bLength =		USB_DT_CONFIG_SIZE,
	.bDescriptorType =	USB_DT_CONFIG,
	/* .wTotalLength computed dynamically */
	.bNumInterfaces =	1,
	.bConfigurationValue =	GS_BULK_CONFIG_ID,
	.iConfiguration =	GS_BULK_CONFIG_STR_ID,
	.bmAttributes =		USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower =		1,
};

static struct usb_config_descriptor gs_acm_config_desc = {
	.bLength =		USB_DT_CONFIG_SIZE,
	.bDescriptorType =	USB_DT_CONFIG,
	/* .wTotalLength computed dynamically */
	.bNumInterfaces =	2,
	.bConfigurationValue =	GS_ACM_CONFIG_ID,
	.iConfiguration =	GS_ACM_CONFIG_STR_ID,
	.bmAttributes =		USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower =		1,
};

static const struct usb_interface_descriptor gs_bulk_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	GS_BULK_INTERFACE_ID,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	.iInterface =		GS_DATA_STR_ID,
};

static const struct usb_interface_descriptor gs_control_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	GS_CONTROL_INTERFACE_ID,
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol =	USB_CDC_ACM_PROTO_AT_V25TER,
	.iInterface =		GS_CONTROL_STR_ID,
};

static const struct usb_interface_descriptor gs_data_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	GS_DATA_INTERFACE_ID,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	.iInterface =		GS_DATA_STR_ID,
};

static const struct usb_cdc_header_desc gs_header_desc = {
	.bLength =		sizeof(gs_header_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,
	.bcdCDC =		__constant_cpu_to_le16(0x0110),
};

static const struct usb_cdc_call_mgmt_descriptor gs_call_mgmt_descriptor = {
	.bLength =  		sizeof(gs_call_mgmt_descriptor),
	.bDescriptorType = 	USB_DT_CS_INTERFACE,
	.bDescriptorSubType = 	USB_CDC_CALL_MANAGEMENT_TYPE,
	.bmCapabilities = 	0,
	.bDataInterface = 	1,	/* index of data interface */
};

static struct usb_cdc_acm_descriptor gs_acm_descriptor = {
	.bLength =  		sizeof(gs_acm_descriptor),
	.bDescriptorType = 	USB_DT_CS_INTERFACE,
	.bDescriptorSubType = 	USB_CDC_ACM_TYPE,
	.bmCapabilities = 	0,
};

static const struct usb_cdc_union_desc gs_union_desc = {
	.bLength =		sizeof(gs_union_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_UNION_TYPE,
	.bMasterInterface0 =	0,	/* index of control interface */
	.bSlaveInterface0 =	1,	/* index of data interface */
};
 
static struct usb_endpoint_descriptor gs_fullspeed_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	__constant_cpu_to_le16(GS_NOTIFY_MAXPACKET),
	.bInterval =		1 << GS_LOG2_NOTIFY_INTERVAL,
};

static struct usb_endpoint_descriptor gs_fullspeed_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor gs_fullspeed_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static const struct usb_descriptor_header *gs_bulk_fullspeed_function[] = {
	(struct usb_descriptor_header *) &gs_otg_descriptor,
	(struct usb_descriptor_header *) &gs_bulk_interface_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_in_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_out_desc,
	NULL,
};

static const struct usb_descriptor_header *gs_acm_fullspeed_function[] = {
	(struct usb_descriptor_header *) &gs_otg_descriptor,
	(struct usb_descriptor_header *) &gs_control_interface_desc,
	(struct usb_descriptor_header *) &gs_header_desc,
	(struct usb_descriptor_header *) &gs_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &gs_acm_descriptor,
	(struct usb_descriptor_header *) &gs_union_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_notify_desc,
	(struct usb_descriptor_header *) &gs_data_interface_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_in_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_out_desc,
	NULL,
};

#ifdef CONFIG_USB_GADGET_DUALSPEED
static struct usb_endpoint_descriptor gs_highspeed_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	__constant_cpu_to_le16(GS_NOTIFY_MAXPACKET),
	.bInterval =		GS_LOG2_NOTIFY_INTERVAL+4,
};

static struct usb_endpoint_descriptor gs_highspeed_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor gs_highspeed_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static struct usb_qualifier_descriptor gs_qualifier_desc = {
	.bLength =		sizeof(struct usb_qualifier_descriptor),
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,
	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	/* assumes ep0 uses the same value for both speeds ... */
	.bNumConfigurations =	GS_NUM_CONFIGS,
};

static const struct usb_descriptor_header *gs_bulk_highspeed_function[] = {
	(struct usb_descriptor_header *) &gs_otg_descriptor,
	(struct usb_descriptor_header *) &gs_bulk_interface_desc,
	(struct usb_descriptor_header *) &gs_highspeed_in_desc,
	(struct usb_descriptor_header *) &gs_highspeed_out_desc,
	NULL,
};

static const struct usb_descriptor_header *gs_acm_highspeed_function[] = {
	(struct usb_descriptor_header *) &gs_otg_descriptor,
	(struct usb_descriptor_header *) &gs_control_interface_desc,
	(struct usb_descriptor_header *) &gs_header_desc,
	(struct usb_descriptor_header *) &gs_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &gs_acm_descriptor,
	(struct usb_descriptor_header *) &gs_union_desc,
	(struct usb_descriptor_header *) &gs_highspeed_notify_desc,
	(struct usb_descriptor_header *) &gs_data_interface_desc,
	(struct usb_descriptor_header *) &gs_highspeed_in_desc,
	(struct usb_descriptor_header *) &gs_highspeed_out_desc,
	NULL,
};

#endif /* CONFIG_USB_GADGET_DUALSPEED */


/* Module */
MODULE_DESCRIPTION(GS_LONG_NAME);
MODULE_AUTHOR("Al Borchers");
MODULE_LICENSE("GPL");

#ifdef GS_DEBUG
module_param(debug, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging, 0=off, 1=on");
#endif

module_param(read_q_size, uint, S_IRUGO);
MODULE_PARM_DESC(read_q_size, "Read request queue size, default=32");

module_param(write_q_size, uint, S_IRUGO);
MODULE_PARM_DESC(write_q_size, "Write request queue size, default=32");

module_param(write_buf_size, uint, S_IRUGO);
MODULE_PARM_DESC(write_buf_size, "Write buffer size, default=8192");

module_param(use_acm, uint, S_IRUGO);
MODULE_PARM_DESC(use_acm, "Use CDC ACM, 0=no, 1=yes, default=no");

module_init(gs_module_init);
module_exit(gs_module_exit);

/*
*  gs_module_init
*
*  Register as a USB gadget driver and a tty driver.
*/
static int __init gs_module_init(void)
{
	int i;
	int retval;

	retval = usb_gadget_register_driver(&gs_gadget_driver);
	if (retval) {
		printk(KERN_ERR "gs_module_init: cannot register gadget driver, ret=%d\n", retval);
		return retval;
	}

	gs_tty_driver = alloc_tty_driver(GS_NUM_PORTS);
	if (!gs_tty_driver)
		return -ENOMEM;
	gs_tty_driver->owner = THIS_MODULE;
	gs_tty_driver->driver_name = GS_SHORT_NAME;
	gs_tty_driver->name = "ttygs";
	gs_tty_driver->devfs_name = "usb/ttygs/";
	gs_tty_driver->major = GS_MAJOR;
	gs_tty_driver->minor_start = GS_MINOR_START;
	gs_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	gs_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	gs_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	gs_tty_driver->init_termios = tty_std_termios;
	gs_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_set_operations(gs_tty_driver, &gs_tty_ops);

	for (i=0; i < GS_NUM_PORTS; i++)
		sema_init(&gs_open_close_sem[i], 1);

	retval = tty_register_driver(gs_tty_driver);
	if (retval) {
		usb_gadget_unregister_driver(&gs_gadget_driver);
		put_tty_driver(gs_tty_driver);
		printk(KERN_ERR "gs_module_init: cannot register tty driver, ret=%d\n", retval);
		return retval;
	}

	printk(KERN_INFO "gs_module_init: %s %s loaded\n", GS_LONG_NAME, GS_VERSION_STR);
	return 0;
}

/*
* gs_module_exit
*
* Unregister as a tty driver and a USB gadget driver.
*/
static void __exit gs_module_exit(void)
{
	tty_unregister_driver(gs_tty_driver);
	put_tty_driver(gs_tty_driver);
	usb_gadget_unregister_driver(&gs_gadget_driver);

	printk(KERN_INFO "gs_module_exit: %s %s unloaded\n", GS_LONG_NAME, GS_VERSION_STR);
}

/* TTY Driver */

/*
 * gs_open
 */
static int gs_open(struct tty_struct *tty, struct file *file)
{
	int port_num;
	unsigned long flags;
	struct gs_port *port;
	struct gs_dev *dev;
	struct gs_buf *buf;
	struct semaphore *sem;
	int ret;

	port_num = tty->index;

	gs_debug("gs_open: (%d,%p,%p)\n", port_num, tty, file);

	if (port_num < 0 || port_num >= GS_NUM_PORTS) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) invalid port number\n",
			port_num, tty, file);
		return -ENODEV;
	}

	dev = gs_device;

	if (dev == NULL) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) NULL device pointer\n",
			port_num, tty, file);
		return -ENODEV;
	}

	sem = &gs_open_close_sem[port_num];
	if (down_interruptible(sem)) {
		printk(KERN_ERR
		"gs_open: (%d,%p,%p) interrupted waiting for semaphore\n",
			port_num, tty, file);
		return -ERESTARTSYS;
	}

	spin_lock_irqsave(&dev->dev_lock, flags);

	if (dev->dev_config == GS_NO_CONFIG_ID) {
		printk(KERN_ERR
			"gs_open: (%d,%p,%p) device is not connected\n",
			port_num, tty, file);
		ret = -ENODEV;
		goto exit_unlock_dev;
	}

	port = dev->dev_port[port_num];

	if (port == NULL) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) NULL port pointer\n",
			port_num, tty, file);
		ret = -ENODEV;
		goto exit_unlock_dev;
	}

	spin_lock(&port->port_lock);
	spin_unlock(&dev->dev_lock);

	if (port->port_dev == NULL) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) port disconnected (1)\n",
			port_num, tty, file);
		ret = -EIO;
		goto exit_unlock_port;
	}

	if (port->port_open_count > 0) {
		++port->port_open_count;
		gs_debug("gs_open: (%d,%p,%p) already open\n",
			port_num, tty, file);
		ret = 0;
		goto exit_unlock_port;
	}

	tty->driver_data = NULL;

	/* mark port as in use, we can drop port lock and sleep if necessary */
	port->port_in_use = 1;

	/* allocate write buffer on first open */
	if (port->port_write_buf == NULL) {
		spin_unlock_irqrestore(&port->port_lock, flags);
		buf = gs_buf_alloc(write_buf_size, GFP_KERNEL);
		spin_lock_irqsave(&port->port_lock, flags);

		/* might have been disconnected while asleep, check */
		if (port->port_dev == NULL) {
			printk(KERN_ERR
				"gs_open: (%d,%p,%p) port disconnected (2)\n",
				port_num, tty, file);
			port->port_in_use = 0;
			ret = -EIO;
			goto exit_unlock_port;
		}

		if ((port->port_write_buf=buf) == NULL) {
			printk(KERN_ERR "gs_open: (%d,%p,%p) cannot allocate port write buffer\n",
				port_num, tty, file);
			port->port_in_use = 0;
			ret = -ENOMEM;
			goto exit_unlock_port;
		}

	}

	/* wait for carrier detect (not implemented) */

	/* might have been disconnected while asleep, check */
	if (port->port_dev == NULL) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) port disconnected (3)\n",
			port_num, tty, file);
		port->port_in_use = 0;
		ret = -EIO;
		goto exit_unlock_port;
	}

	tty->driver_data = port;
	port->port_tty = tty;
	port->port_open_count = 1;
	port->port_in_use = 0;

	gs_debug("gs_open: (%d,%p,%p) completed\n", port_num, tty, file);

	ret = 0;

exit_unlock_port:
	spin_unlock_irqrestore(&port->port_lock, flags);
	up(sem);
	return ret;

exit_unlock_dev:
	spin_unlock_irqrestore(&dev->dev_lock, flags);
	up(sem);
	return ret;

}

/*
 * gs_close
 */
static void gs_close(struct tty_struct *tty, struct file *file)
{
	unsigned long flags;
	struct gs_port *port = tty->driver_data;
	struct semaphore *sem;

	if (port == NULL) {
		printk(KERN_ERR "gs_close: NULL port pointer\n");
		return;
	}

	gs_debug("gs_close: (%d,%p,%p)\n", port->port_num, tty, file);

	sem = &gs_open_close_sem[port->port_num];
	down(sem);

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_open_count == 0) {
		printk(KERN_ERR
			"gs_close: (%d,%p,%p) port is already closed\n",
			port->port_num, tty, file);
		goto exit;
	}

	if (port->port_open_count > 1) {
		--port->port_open_count;
		goto exit;
	}

	/* free disconnected port on final close */
	if (port->port_dev == NULL) {
		kfree(port);
		goto exit;
	}

	/* mark port as closed but in use, we can drop port lock */
	/* and sleep if necessary */
	port->port_in_use = 1;
	port->port_open_count = 0;

	/* wait for write buffer to drain, or */
	/* at most GS_CLOSE_TIMEOUT seconds */
	if (gs_buf_data_avail(port->port_write_buf) > 0) {
		spin_unlock_irqrestore(&port->port_lock, flags);
		wait_cond_interruptible_timeout(port->port_write_wait,
		port->port_dev == NULL
		|| gs_buf_data_avail(port->port_write_buf) == 0,
		&port->port_lock, flags, GS_CLOSE_TIMEOUT * HZ);
		spin_lock_irqsave(&port->port_lock, flags);
	}

	/* free disconnected port on final close */
	/* (might have happened during the above sleep) */
	if (port->port_dev == NULL) {
		kfree(port);
		goto exit;
	}

	gs_buf_clear(port->port_write_buf);

	tty->driver_data = NULL;
	port->port_tty = NULL;
	port->port_in_use = 0;

	gs_debug("gs_close: (%d,%p,%p) completed\n",
		port->port_num, tty, file);

exit:
	spin_unlock_irqrestore(&port->port_lock, flags);
	up(sem);
}

/*
 * gs_write
 */
static int gs_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	unsigned long flags;
	struct gs_port *port = tty->driver_data;
	int ret;

	if (port == NULL) {
		printk(KERN_ERR "gs_write: NULL port pointer\n");
		return -EIO;
	}

	gs_debug("gs_write: (%d,%p) writing %d bytes\n", port->port_num, tty,
		count);

	if (count == 0)
		return 0;

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev == NULL) {
		printk(KERN_ERR "gs_write: (%d,%p) port is not connected\n",
			port->port_num, tty);
		ret = -EIO;
		goto exit;
	}

	if (port->port_open_count == 0) {
		printk(KERN_ERR "gs_write: (%d,%p) port is closed\n",
			port->port_num, tty);
		ret = -EBADF;
		goto exit;
	}

	count = gs_buf_put(port->port_write_buf, buf, count);

	spin_unlock_irqrestore(&port->port_lock, flags);

	gs_send(gs_device);

	gs_debug("gs_write: (%d,%p) wrote %d bytes\n", port->port_num, tty,
		count);

	return count;

exit:
	spin_unlock_irqrestore(&port->port_lock, flags);
	return ret;
}

/*
 * gs_put_char
 */
static void gs_put_char(struct tty_struct *tty, unsigned char ch)
{
	unsigned long flags;
	struct gs_port *port = tty->driver_data;

	if (port == NULL) {
		printk(KERN_ERR "gs_put_char: NULL port pointer\n");
		return;
	}

	gs_debug("gs_put_char: (%d,%p) char=0x%x, called from %p, %p, %p\n", port->port_num, tty, ch, __builtin_return_address(0), __builtin_return_address(1), __builtin_return_address(2));

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev == NULL) {
		printk(KERN_ERR "gs_put_char: (%d,%p) port is not connected\n",
			port->port_num, tty);
		goto exit;
	}

	if (port->port_open_count == 0) {
		printk(KERN_ERR "gs_put_char: (%d,%p) port is closed\n",
			port->port_num, tty);
		goto exit;
	}

	gs_buf_put(port->port_write_buf, &ch, 1);

exit:
	spin_unlock_irqrestore(&port->port_lock, flags);
}

/*
 * gs_flush_chars
 */
static void gs_flush_chars(struct tty_struct *tty)
{
	unsigned long flags;
	struct gs_port *port = tty->driver_data;

	if (port == NULL) {
		printk(KERN_ERR "gs_flush_chars: NULL port pointer\n");
		return;
	}

	gs_debug("gs_flush_chars: (%d,%p)\n", port->port_num, tty);

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev == NULL) {
		printk(KERN_ERR
			"gs_flush_chars: (%d,%p) port is not connected\n",
			port->port_num, tty);
		goto exit;
	}

	if (port->port_open_count == 0) {
		printk(KERN_ERR "gs_flush_chars: (%d,%p) port is closed\n",
			port->port_num, tty);
		goto exit;
	}

	spin_unlock_irqrestore(&port->port_lock, flags);

	gs_send(gs_device);

	return;

exit:
	spin_unlock_irqrestore(&port->port_lock, flags);
}

/*
 * gs_write_room
 */
static int gs_write_room(struct tty_struct *tty)
{

	int room = 0;
	unsigned long flags;
	struct gs_port *port = tty->driver_data;


	if (port == NULL)
		return 0;

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev != NULL && port->port_open_count > 0
	&& port->port_write_buf != NULL)
		room = gs_buf_space_avail(port->port_write_buf);

	spin_unlock_irqrestore(&port->port_lock, flags);

	gs_debug("gs_write_room: (%d,%p) room=%d\n",
		port->port_num, tty, room);

	return room;
}

/*
 * gs_chars_in_buffer
 */
static int gs_chars_in_buffer(struct tty_struct *tty)
{
	int chars = 0;
	unsigned long flags;
	struct gs_port *port = tty->driver_data;

	if (port == NULL)
		return 0;

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev != NULL && port->port_open_count > 0
	&& port->port_write_buf != NULL)
		chars = gs_buf_data_avail(port->port_write_buf);

	spin_unlock_irqrestore(&port->port_lock, flags);

	gs_debug("gs_chars_in_buffer: (%d,%p) chars=%d\n",
		port->port_num, tty, chars);

	return chars;
}

/*
 * gs_throttle
 */
static void gs_throttle(struct tty_struct *tty)
{
}

/*
 * gs_unthrottle
 */
static void gs_unthrottle(struct tty_struct *tty)
{
}

/*
 * gs_break
 */
static void gs_break(struct tty_struct *tty, int break_state)
{
}

/*
 * gs_ioctl
 */
static int gs_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct gs_port *port = tty->driver_data;

	if (port == NULL) {
		printk(KERN_ERR "gs_ioctl: NULL port pointer\n");
		return -EIO;
	}

	gs_debug("gs_ioctl: (%d,%p,%p) cmd=0x%4.4x, arg=%lu\n",
		port->port_num, tty, file, cmd, arg);

	/* handle ioctls */

	/* could not handle ioctl */
	return -ENOIOCTLCMD;
}

/*
 * gs_set_termios
 */
static void gs_set_termios(struct tty_struct *tty, struct termios *old)
{
}

/*
* gs_send
*
* This function finds available write requests, calls
* gs_send_packet to fill these packets with data, and
* continues until either there are no more write requests
* available or no more data to send.  This function is
* run whenever data arrives or write requests are available.
*/
static int gs_send(struct gs_dev *dev)
{
	int ret,len;
	unsigned long flags;
	struct usb_ep *ep;
	struct usb_request *req;
	struct gs_req_entry *req_entry;

	if (dev == NULL) {
		printk(KERN_ERR "gs_send: NULL device pointer\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&dev->dev_lock, flags);

	ep = dev->dev_in_ep;

	while(!list_empty(&dev->dev_req_list)) {

		req_entry = list_entry(dev->dev_req_list.next,
			struct gs_req_entry, re_entry);

		req = req_entry->re_req;

		len = gs_send_packet(dev, req->buf, ep->maxpacket);

		if (len > 0) {
gs_debug_level(3, "gs_send: len=%d, 0x%2.2x 0x%2.2x 0x%2.2x ...\n", len, *((unsigned char *)req->buf), *((unsigned char *)req->buf+1), *((unsigned char *)req->buf+2));
			list_del(&req_entry->re_entry);
			req->length = len;
			if ((ret=usb_ep_queue(ep, req, GFP_ATOMIC))) {
				printk(KERN_ERR
				"gs_send: cannot queue read request, ret=%d\n",
					ret);
				break;
			}
		} else {
			break;
		}

	}

	spin_unlock_irqrestore(&dev->dev_lock, flags);

	return 0;
}

/*
 * gs_send_packet
 *
 * If there is data to send, a packet is built in the given
 * buffer and the size is returned.  If there is no data to
 * send, 0 is returned.  If there is any error a negative
 * error number is returned.
 *
 * Called during USB completion routine, on interrupt time.
 *
 * We assume that disconnect will not happen until all completion
 * routines have completed, so we can assume that the dev_port
 * array does not change during the lifetime of this function.
 */
static int gs_send_packet(struct gs_dev *dev, char *packet, unsigned int size)
{
	unsigned int len;
	struct gs_port *port;

	/* TEMPORARY -- only port 0 is supported right now */
	port = dev->dev_port[0];

	if (port == NULL) {
		printk(KERN_ERR
			"gs_send_packet: port=%d, NULL port pointer\n",
			0);
		return -EIO;
	}

	spin_lock(&port->port_lock);

	len = gs_buf_data_avail(port->port_write_buf);
	if (len < size)
		size = len;

	if (size == 0)
		goto exit;

	size = gs_buf_get(port->port_write_buf, packet, size);

	if (port->port_tty)
		wake_up_interruptible(&port->port_tty->write_wait);

exit:
	spin_unlock(&port->port_lock);
	return size;
}

/*
 * gs_recv_packet
 *
 * Called for each USB packet received.  Reads the packet
 * header and stuffs the data in the appropriate tty buffer.
 * Returns 0 if successful, or a negative error number.
 *
 * Called during USB completion routine, on interrupt time.
 *
 * We assume that disconnect will not happen until all completion
 * routines have completed, so we can assume that the dev_port
 * array does not change during the lifetime of this function.
 */
static int gs_recv_packet(struct gs_dev *dev, char *packet, unsigned int size)
{
	unsigned int len;
	struct gs_port *port;
	int ret;
	struct tty_struct *tty;

	/* TEMPORARY -- only port 0 is supported right now */
	port = dev->dev_port[0];

	if (port == NULL) {
		printk(KERN_ERR "gs_recv_packet: port=%d, NULL port pointer\n",
			port->port_num);
		return -EIO;
	}

	spin_lock(&port->port_lock);

	if (port->port_open_count == 0) {
		printk(KERN_ERR "gs_recv_packet: port=%d, port is closed\n",
			port->port_num);
		ret = -EIO;
		goto exit;
	}


	tty = port->port_tty;

	if (tty == NULL) {
		printk(KERN_ERR "gs_recv_packet: port=%d, NULL tty pointer\n",
			port->port_num);
		ret = -EIO;
		goto exit;
	}

	if (port->port_tty->magic != TTY_MAGIC) {
		printk(KERN_ERR "gs_recv_packet: port=%d, bad tty magic\n",
			port->port_num);
		ret = -EIO;
		goto exit;
	}

	len = tty_buffer_request_room(tty, size);
	if (len > 0) {
		tty_insert_flip_string(tty, packet, len);
		tty_flip_buffer_push(port->port_tty);
		wake_up_interruptible(&port->port_tty->read_wait);
	}
	ret = 0;
exit:
	spin_unlock(&port->port_lock);
	return ret;
}

/*
* gs_read_complete
*/
static void gs_read_complete(struct usb_ep *ep, struct usb_request *req)
{
	int ret;
	struct gs_dev *dev = ep->driver_data;

	if (dev == NULL) {
		printk(KERN_ERR "gs_read_complete: NULL device pointer\n");
		return;
	}

	switch(req->status) {
	case 0:
 		/* normal completion */
		gs_recv_packet(dev, req->buf, req->actual);
requeue:
		req->length = ep->maxpacket;
		if ((ret=usb_ep_queue(ep, req, GFP_ATOMIC))) {
			printk(KERN_ERR
			"gs_read_complete: cannot queue read request, ret=%d\n",
				ret);
		}
		break;

	case -ESHUTDOWN:
		/* disconnect */
		gs_debug("gs_read_complete: shutdown\n");
		gs_free_req(ep, req);
		break;

	default:
		/* unexpected */
		printk(KERN_ERR
		"gs_read_complete: unexpected status error, status=%d\n",
			req->status);
		goto requeue;
		break;
	}
}

/*
* gs_write_complete
*/
static void gs_write_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gs_dev *dev = ep->driver_data;
	struct gs_req_entry *gs_req = req->context;

	if (dev == NULL) {
		printk(KERN_ERR "gs_write_complete: NULL device pointer\n");
		return;
	}

	switch(req->status) {
	case 0:
		/* normal completion */
requeue:
		if (gs_req == NULL) {
			printk(KERN_ERR
				"gs_write_complete: NULL request pointer\n");
			return;
		}

		spin_lock(&dev->dev_lock);
		list_add(&gs_req->re_entry, &dev->dev_req_list);
		spin_unlock(&dev->dev_lock);

		gs_send(dev);

		break;

	case -ESHUTDOWN:
		/* disconnect */
		gs_debug("gs_write_complete: shutdown\n");
		gs_free_req(ep, req);
		break;

	default:
		printk(KERN_ERR
		"gs_write_complete: unexpected status error, status=%d\n",
			req->status);
		goto requeue;
		break;
	}
}

/* Gadget Driver */

/*
 * gs_bind
 *
 * Called on module load.  Allocates and initializes the device
 * structure and a control request.
 */
static int __init gs_bind(struct usb_gadget *gadget)
{
	int ret;
	struct usb_ep *ep;
	struct gs_dev *dev;
	int gcnum;

	/* Some controllers can't support CDC ACM:
	 * - sh doesn't support multiple interfaces or configs;
	 * - sa1100 doesn't have a third interrupt endpoint
	 */
	if (gadget_is_sh(gadget) || gadget_is_sa1100(gadget))
		use_acm = 0;

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		gs_device_desc.bcdDevice =
				cpu_to_le16(GS_VERSION_NUM | gcnum);
	else {
		printk(KERN_WARNING "gs_bind: controller '%s' not recognized\n",
			gadget->name);
		/* unrecognized, but safe unless bulk is REALLY quirky */
		gs_device_desc.bcdDevice =
			__constant_cpu_to_le16(GS_VERSION_NUM|0x0099);
	}

	usb_ep_autoconfig_reset(gadget);

	ep = usb_ep_autoconfig(gadget, &gs_fullspeed_in_desc);
	if (!ep)
		goto autoconf_fail;
	EP_IN_NAME = ep->name;
	ep->driver_data = ep;	/* claim the endpoint */

	ep = usb_ep_autoconfig(gadget, &gs_fullspeed_out_desc);
	if (!ep)
		goto autoconf_fail;
	EP_OUT_NAME = ep->name;
	ep->driver_data = ep;	/* claim the endpoint */

	if (use_acm) {
		ep = usb_ep_autoconfig(gadget, &gs_fullspeed_notify_desc);
		if (!ep) {
			printk(KERN_ERR "gs_bind: cannot run ACM on %s\n", gadget->name);
			goto autoconf_fail;
		}
		gs_device_desc.idProduct = __constant_cpu_to_le16(
						GS_CDC_PRODUCT_ID),
		EP_NOTIFY_NAME = ep->name;
		ep->driver_data = ep;	/* claim the endpoint */
	}

	gs_device_desc.bDeviceClass = use_acm
		? USB_CLASS_COMM : USB_CLASS_VENDOR_SPEC;
	gs_device_desc.bMaxPacketSize0 = gadget->ep0->maxpacket;

#ifdef CONFIG_USB_GADGET_DUALSPEED
	gs_qualifier_desc.bDeviceClass = use_acm
		? USB_CLASS_COMM : USB_CLASS_VENDOR_SPEC;
	/* assume ep0 uses the same packet size for both speeds */
	gs_qualifier_desc.bMaxPacketSize0 = gs_device_desc.bMaxPacketSize0;
	/* assume endpoints are dual-speed */
	gs_highspeed_notify_desc.bEndpointAddress =
		gs_fullspeed_notify_desc.bEndpointAddress;
	gs_highspeed_in_desc.bEndpointAddress =
		gs_fullspeed_in_desc.bEndpointAddress;
	gs_highspeed_out_desc.bEndpointAddress =
		gs_fullspeed_out_desc.bEndpointAddress;
#endif /* CONFIG_USB_GADGET_DUALSPEED */

	usb_gadget_set_selfpowered(gadget);

	if (gadget->is_otg) {
		gs_otg_descriptor.bmAttributes |= USB_OTG_HNP,
		gs_bulk_config_desc.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
		gs_acm_config_desc.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	gs_device = dev = kmalloc(sizeof(struct gs_dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	snprintf(manufacturer, sizeof(manufacturer), "%s %s with %s",
		system_utsname.sysname, system_utsname.release,
		gadget->name);

	memset(dev, 0, sizeof(struct gs_dev));
	dev->dev_gadget = gadget;
	spin_lock_init(&dev->dev_lock);
	INIT_LIST_HEAD(&dev->dev_req_list);
	set_gadget_data(gadget, dev);

	if ((ret=gs_alloc_ports(dev, GFP_KERNEL)) != 0) {
		printk(KERN_ERR "gs_bind: cannot allocate ports\n");
		gs_unbind(gadget);
		return ret;
	}

	/* preallocate control response and buffer */
	dev->dev_ctrl_req = gs_alloc_req(gadget->ep0, GS_MAX_DESC_LEN,
		GFP_KERNEL);
	if (dev->dev_ctrl_req == NULL) {
		gs_unbind(gadget);
		return -ENOMEM;
	}
	dev->dev_ctrl_req->complete = gs_setup_complete;

	gadget->ep0->driver_data = dev;

	printk(KERN_INFO "gs_bind: %s %s bound\n",
		GS_LONG_NAME, GS_VERSION_STR);

	return 0;

autoconf_fail:
	printk(KERN_ERR "gs_bind: cannot autoconfigure on %s\n", gadget->name);
	return -ENODEV;
}

/*
 * gs_unbind
 *
 * Called on module unload.  Frees the control request and device
 * structure.
 */
static void __exit gs_unbind(struct usb_gadget *gadget)
{
	struct gs_dev *dev = get_gadget_data(gadget);

	gs_device = NULL;

	/* read/write requests already freed, only control request remains */
	if (dev != NULL) {
		if (dev->dev_ctrl_req != NULL) {
			gs_free_req(gadget->ep0, dev->dev_ctrl_req);
			dev->dev_ctrl_req = NULL;
		}
		gs_free_ports(dev);
		kfree(dev);
		set_gadget_data(gadget, NULL);
	}

	printk(KERN_INFO "gs_unbind: %s %s unbound\n", GS_LONG_NAME,
		GS_VERSION_STR);
}

/*
 * gs_setup
 *
 * Implements all the control endpoint functionality that's not
 * handled in hardware or the hardware driver.
 *
 * Returns the size of the data sent to the host, or a negative
 * error number.
 */
static int gs_setup(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl)
{
	int ret = -EOPNOTSUPP;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->dev_ctrl_req;
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wLength = le16_to_cpu(ctrl->wLength);

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		ret = gs_setup_standard(gadget,ctrl);
		break;

	case USB_TYPE_CLASS:
		ret = gs_setup_class(gadget,ctrl);
		break;

	default:
		printk(KERN_ERR "gs_setup: unknown request, type=%02x, request=%02x, value=%04x, index=%04x, length=%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
		break;
	}

	/* respond with data transfer before status phase? */
	if (ret >= 0) {
		req->length = ret;
		req->zero = ret < wLength
				&& (ret % gadget->ep0->maxpacket) == 0;
		ret = usb_ep_queue(gadget->ep0, req, GFP_ATOMIC);
		if (ret < 0) {
			printk(KERN_ERR "gs_setup: cannot queue response, ret=%d\n",
				ret);
			req->status = 0;
			gs_setup_complete(gadget->ep0, req);
		}
	}

	/* device either stalls (ret < 0) or reports success */
	return ret;
}

static int gs_setup_standard(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl)
{
	int ret = -EOPNOTSUPP;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->dev_ctrl_req;
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wLength = le16_to_cpu(ctrl->wLength);

	switch (ctrl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != USB_DIR_IN)
			break;

		switch (wValue >> 8) {
		case USB_DT_DEVICE:
			ret = min(wLength,
				(u16)sizeof(struct usb_device_descriptor));
			memcpy(req->buf, &gs_device_desc, ret);
			break;

#ifdef CONFIG_USB_GADGET_DUALSPEED
		case USB_DT_DEVICE_QUALIFIER:
			if (!gadget->is_dualspeed)
				break;
			ret = min(wLength,
				(u16)sizeof(struct usb_qualifier_descriptor));
			memcpy(req->buf, &gs_qualifier_desc, ret);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
			if (!gadget->is_dualspeed)
				break;
			/* fall through */
#endif /* CONFIG_USB_GADGET_DUALSPEED */
		case USB_DT_CONFIG:
			ret = gs_build_config_buf(req->buf, gadget->speed,
				wValue >> 8, wValue & 0xff,
				gadget->is_otg);
			if (ret >= 0)
				ret = min(wLength, (u16)ret);
			break;

		case USB_DT_STRING:
			/* wIndex == language code. */
			ret = usb_gadget_get_string(&gs_string_table,
				wValue & 0xff, req->buf);
			if (ret >= 0)
				ret = min(wLength, (u16)ret);
			break;
		}
		break;

	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0)
			break;
		spin_lock(&dev->dev_lock);
		ret = gs_set_config(dev, wValue);
		spin_unlock(&dev->dev_lock);
		break;

	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != USB_DIR_IN)
			break;
		*(u8 *)req->buf = dev->dev_config;
		ret = min(wLength, (u16)1);
		break;

	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType != USB_RECIP_INTERFACE
				|| !dev->dev_config
				|| wIndex >= GS_MAX_NUM_INTERFACES)
			break;
		if (dev->dev_config == GS_BULK_CONFIG_ID
				&& wIndex != GS_BULK_INTERFACE_ID)
			break;
		/* no alternate interface settings */
		if (wValue != 0)
			break;
		spin_lock(&dev->dev_lock);
		/* PXA hardware partially handles SET_INTERFACE;
		 * we need to kluge around that interference.  */
		if (gadget_is_pxa(gadget)) {
			ret = gs_set_config(dev, use_acm ?
				GS_ACM_CONFIG_ID : GS_BULK_CONFIG_ID);
			goto set_interface_done;
		}
		if (dev->dev_config != GS_BULK_CONFIG_ID
				&& wIndex == GS_CONTROL_INTERFACE_ID) {
			if (dev->dev_notify_ep) {
				usb_ep_disable(dev->dev_notify_ep);
				usb_ep_enable(dev->dev_notify_ep, dev->dev_notify_ep_desc);
			}
		} else {
			usb_ep_disable(dev->dev_in_ep);
			usb_ep_disable(dev->dev_out_ep);
			usb_ep_enable(dev->dev_in_ep, dev->dev_in_ep_desc);
			usb_ep_enable(dev->dev_out_ep, dev->dev_out_ep_desc);
		}
		ret = 0;
set_interface_done:
		spin_unlock(&dev->dev_lock);
		break;

	case USB_REQ_GET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE)
		|| dev->dev_config == GS_NO_CONFIG_ID)
			break;
		if (wIndex >= GS_MAX_NUM_INTERFACES
				|| (dev->dev_config == GS_BULK_CONFIG_ID
				&& wIndex != GS_BULK_INTERFACE_ID)) {
			ret = -EDOM;
			break;
		}
		/* no alternate interface settings */
		*(u8 *)req->buf = 0;
		ret = min(wLength, (u16)1);
		break;

	default:
		printk(KERN_ERR "gs_setup: unknown standard request, type=%02x, request=%02x, value=%04x, index=%04x, length=%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
		break;
	}

	return ret;
}

static int gs_setup_class(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl)
{
	int ret = -EOPNOTSUPP;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct gs_port *port = dev->dev_port[0];	/* ACM only has one port */
	struct usb_request *req = dev->dev_ctrl_req;
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wLength = le16_to_cpu(ctrl->wLength);

	switch (ctrl->bRequest) {
	case USB_CDC_REQ_SET_LINE_CODING:
		ret = min(wLength,
			(u16)sizeof(struct usb_cdc_line_coding));
		if (port) {
			spin_lock(&port->port_lock);
			memcpy(&port->port_line_coding, req->buf, ret);
			spin_unlock(&port->port_lock);
		}
		break;

	case USB_CDC_REQ_GET_LINE_CODING:
		port = dev->dev_port[0];	/* ACM only has one port */
		ret = min(wLength,
			(u16)sizeof(struct usb_cdc_line_coding));
		if (port) {
			spin_lock(&port->port_lock);
			memcpy(req->buf, &port->port_line_coding, ret);
			spin_unlock(&port->port_lock);
		}
		break;

	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		ret = 0;
		break;

	default:
		printk(KERN_ERR "gs_setup: unknown class request, type=%02x, request=%02x, value=%04x, index=%04x, length=%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
		break;
	}

	return ret;
}

/*
 * gs_setup_complete
 */
static void gs_setup_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (req->status || req->actual != req->length) {
		printk(KERN_ERR "gs_setup_complete: status error, status=%d, actual=%d, length=%d\n",
			req->status, req->actual, req->length);
	}
}

/*
 * gs_disconnect
 *
 * Called when the device is disconnected.  Frees the closed
 * ports and disconnects open ports.  Open ports will be freed
 * on close.  Then reallocates the ports for the next connection.
 */
static void gs_disconnect(struct usb_gadget *gadget)
{
	unsigned long flags;
	struct gs_dev *dev = get_gadget_data(gadget);

	spin_lock_irqsave(&dev->dev_lock, flags);

	gs_reset_config(dev);

	/* free closed ports and disconnect open ports */
	/* (open ports will be freed when closed) */
	gs_free_ports(dev);

	/* re-allocate ports for the next connection */
	if (gs_alloc_ports(dev, GFP_ATOMIC) != 0)
		printk(KERN_ERR "gs_disconnect: cannot re-allocate ports\n");

	spin_unlock_irqrestore(&dev->dev_lock, flags);

	printk(KERN_INFO "gs_disconnect: %s disconnected\n", GS_LONG_NAME);
}

/*
 * gs_set_config
 *
 * Configures the device by enabling device specific
 * optimizations, setting up the endpoints, allocating
 * read and write requests and queuing read requests.
 *
 * The device lock must be held when calling this function.
 */
static int gs_set_config(struct gs_dev *dev, unsigned config)
{
	int i;
	int ret = 0;
	struct usb_gadget *gadget = dev->dev_gadget;
	struct usb_ep *ep;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_request *req;
	struct gs_req_entry *req_entry;

	if (dev == NULL) {
		printk(KERN_ERR "gs_set_config: NULL device pointer\n");
		return 0;
	}

	if (config == dev->dev_config)
		return 0;

	gs_reset_config(dev);

	switch (config) {
	case GS_NO_CONFIG_ID:
		return 0;
	case GS_BULK_CONFIG_ID:
		if (use_acm)
			return -EINVAL;
		/* device specific optimizations */
		if (gadget_is_net2280(gadget))
			net2280_set_fifo_mode(gadget, 1);
		break;
	case GS_ACM_CONFIG_ID:
		if (!use_acm)
			return -EINVAL;
		/* device specific optimizations */
		if (gadget_is_net2280(gadget))
			net2280_set_fifo_mode(gadget, 1);
		break;
	default:
		return -EINVAL;
	}

	dev->dev_config = config;

	gadget_for_each_ep(ep, gadget) {

		if (EP_NOTIFY_NAME
		&& strcmp(ep->name, EP_NOTIFY_NAME) == 0) {
			ep_desc = GS_SPEED_SELECT(
				gadget->speed == USB_SPEED_HIGH,
				&gs_highspeed_notify_desc,
				&gs_fullspeed_notify_desc);
			ret = usb_ep_enable(ep,ep_desc);
			if (ret == 0) {
				ep->driver_data = dev;
				dev->dev_notify_ep = ep;
				dev->dev_notify_ep_desc = ep_desc;
			} else {
				printk(KERN_ERR "gs_set_config: cannot enable notify endpoint %s, ret=%d\n",
					ep->name, ret);
				goto exit_reset_config;
			}
		}

		else if (strcmp(ep->name, EP_IN_NAME) == 0) {
			ep_desc = GS_SPEED_SELECT(
				gadget->speed == USB_SPEED_HIGH,
 				&gs_highspeed_in_desc,
				&gs_fullspeed_in_desc);
			ret = usb_ep_enable(ep,ep_desc);
			if (ret == 0) {
				ep->driver_data = dev;
				dev->dev_in_ep = ep;
				dev->dev_in_ep_desc = ep_desc;
			} else {
				printk(KERN_ERR "gs_set_config: cannot enable in endpoint %s, ret=%d\n",
					ep->name, ret);
				goto exit_reset_config;
			}
		}

		else if (strcmp(ep->name, EP_OUT_NAME) == 0) {
			ep_desc = GS_SPEED_SELECT(
				gadget->speed == USB_SPEED_HIGH,
				&gs_highspeed_out_desc,
				&gs_fullspeed_out_desc);
			ret = usb_ep_enable(ep,ep_desc);
			if (ret == 0) {
				ep->driver_data = dev;
				dev->dev_out_ep = ep;
				dev->dev_out_ep_desc = ep_desc;
			} else {
				printk(KERN_ERR "gs_set_config: cannot enable out endpoint %s, ret=%d\n",
					ep->name, ret);
				goto exit_reset_config;
			}
		}

	}

	if (dev->dev_in_ep == NULL || dev->dev_out_ep == NULL
	|| (config != GS_BULK_CONFIG_ID && dev->dev_notify_ep == NULL)) {
		printk(KERN_ERR "gs_set_config: cannot find endpoints\n");
		ret = -ENODEV;
		goto exit_reset_config;
	}

	/* allocate and queue read requests */
	ep = dev->dev_out_ep;
	for (i=0; i<read_q_size && ret == 0; i++) {
		if ((req=gs_alloc_req(ep, ep->maxpacket, GFP_ATOMIC))) {
			req->complete = gs_read_complete;
			if ((ret=usb_ep_queue(ep, req, GFP_ATOMIC))) {
				printk(KERN_ERR "gs_set_config: cannot queue read request, ret=%d\n",
					ret);
			}
		} else {
			printk(KERN_ERR "gs_set_config: cannot allocate read requests\n");
			ret = -ENOMEM;
			goto exit_reset_config;
		}
	}

	/* allocate write requests, and put on free list */
	ep = dev->dev_in_ep;
	for (i=0; i<write_q_size; i++) {
		if ((req_entry=gs_alloc_req_entry(ep, ep->maxpacket, GFP_ATOMIC))) {
			req_entry->re_req->complete = gs_write_complete;
			list_add(&req_entry->re_entry, &dev->dev_req_list);
		} else {
			printk(KERN_ERR "gs_set_config: cannot allocate write requests\n");
			ret = -ENOMEM;
			goto exit_reset_config;
		}
	}

	printk(KERN_INFO "gs_set_config: %s configured, %s speed %s config\n",
		GS_LONG_NAME,
		gadget->speed == USB_SPEED_HIGH ? "high" : "full",
		config == GS_BULK_CONFIG_ID ? "BULK" : "CDC-ACM");

	return 0;

exit_reset_config:
	gs_reset_config(dev);
	return ret;
}

/*
 * gs_reset_config
 *
 * Mark the device as not configured, disable all endpoints,
 * which forces completion of pending I/O and frees queued
 * requests, and free the remaining write requests on the
 * free list.
 *
 * The device lock must be held when calling this function.
 */
static void gs_reset_config(struct gs_dev *dev)
{
	struct gs_req_entry *req_entry;

	if (dev == NULL) {
		printk(KERN_ERR "gs_reset_config: NULL device pointer\n");
		return;
	}

	if (dev->dev_config == GS_NO_CONFIG_ID)
		return;

	dev->dev_config = GS_NO_CONFIG_ID;

	/* free write requests on the free list */
	while(!list_empty(&dev->dev_req_list)) {
		req_entry = list_entry(dev->dev_req_list.next,
			struct gs_req_entry, re_entry);
		list_del(&req_entry->re_entry);
		gs_free_req_entry(dev->dev_in_ep, req_entry);
	}

	/* disable endpoints, forcing completion of pending i/o; */
	/* completion handlers free their requests in this case */
	if (dev->dev_notify_ep) {
		usb_ep_disable(dev->dev_notify_ep);
		dev->dev_notify_ep = NULL;
	}
	if (dev->dev_in_ep) {
		usb_ep_disable(dev->dev_in_ep);
		dev->dev_in_ep = NULL;
	}
	if (dev->dev_out_ep) {
		usb_ep_disable(dev->dev_out_ep);
		dev->dev_out_ep = NULL;
	}
}

/*
 * gs_build_config_buf
 *
 * Builds the config descriptors in the given buffer and returns the
 * length, or a negative error number.
 */
static int gs_build_config_buf(u8 *buf, enum usb_device_speed speed,
	u8 type, unsigned int index, int is_otg)
{
	int len;
	int high_speed;
	const struct usb_config_descriptor *config_desc;
	const struct usb_descriptor_header **function;

	if (index >= gs_device_desc.bNumConfigurations)
		return -EINVAL;

	/* other speed switches high and full speed */
	high_speed = (speed == USB_SPEED_HIGH);
	if (type == USB_DT_OTHER_SPEED_CONFIG)
		high_speed = !high_speed;

	if (use_acm) {
		config_desc = &gs_acm_config_desc;
		function = GS_SPEED_SELECT(high_speed,
			gs_acm_highspeed_function,
			gs_acm_fullspeed_function);
	} else {
		config_desc = &gs_bulk_config_desc;
		function = GS_SPEED_SELECT(high_speed,
			gs_bulk_highspeed_function,
			gs_bulk_fullspeed_function);
	}

	/* for now, don't advertise srp-only devices */
	if (!is_otg)
		function++;

	len = usb_gadget_config_buf(config_desc, buf, GS_MAX_DESC_LEN, function);
	if (len < 0)
		return len;

	((struct usb_config_descriptor *)buf)->bDescriptorType = type;

	return len;
}

/*
 * gs_alloc_req
 *
 * Allocate a usb_request and its buffer.  Returns a pointer to the
 * usb_request or NULL if there is an error.
 */
static struct usb_request *
gs_alloc_req(struct usb_ep *ep, unsigned int len, gfp_t kmalloc_flags)
{
	struct usb_request *req;

	if (ep == NULL)
		return NULL;

	req = usb_ep_alloc_request(ep, kmalloc_flags);

	if (req != NULL) {
		req->length = len;
		req->buf = kmalloc(len, kmalloc_flags);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			return NULL;
		}
	}

	return req;
}

/*
 * gs_free_req
 *
 * Free a usb_request and its buffer.
 */
static void gs_free_req(struct usb_ep *ep, struct usb_request *req)
{
	if (ep != NULL && req != NULL) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

/*
 * gs_alloc_req_entry
 *
 * Allocates a request and its buffer, using the given
 * endpoint, buffer len, and kmalloc flags.
 */
static struct gs_req_entry *
gs_alloc_req_entry(struct usb_ep *ep, unsigned len, gfp_t kmalloc_flags)
{
	struct gs_req_entry	*req;

	req = kmalloc(sizeof(struct gs_req_entry), kmalloc_flags);
	if (req == NULL)
		return NULL;

	req->re_req = gs_alloc_req(ep, len, kmalloc_flags);
	if (req->re_req == NULL) {
		kfree(req);
		return NULL;
	}

	req->re_req->context = req;

	return req;
}

/*
 * gs_free_req_entry
 *
 * Frees a request and its buffer.
 */
static void gs_free_req_entry(struct usb_ep *ep, struct gs_req_entry *req)
{
	if (ep != NULL && req != NULL) {
		if (req->re_req != NULL)
			gs_free_req(ep, req->re_req);
		kfree(req);
	}
}

/*
 * gs_alloc_ports
 *
 * Allocate all ports and set the gs_dev struct to point to them.
 * Return 0 if successful, or a negative error number.
 *
 * The device lock is normally held when calling this function.
 */
static int gs_alloc_ports(struct gs_dev *dev, gfp_t kmalloc_flags)
{
	int i;
	struct gs_port *port;

	if (dev == NULL)
		return -EIO;

	for (i=0; i<GS_NUM_PORTS; i++) {
		if ((port=kzalloc(sizeof(struct gs_port), kmalloc_flags)) == NULL)
			return -ENOMEM;

		port->port_dev = dev;
		port->port_num = i;
		port->port_line_coding.dwDTERate = cpu_to_le32(GS_DEFAULT_DTE_RATE);
		port->port_line_coding.bCharFormat = GS_DEFAULT_CHAR_FORMAT;
		port->port_line_coding.bParityType = GS_DEFAULT_PARITY;
		port->port_line_coding.bDataBits = GS_DEFAULT_DATA_BITS;
		spin_lock_init(&port->port_lock);
		init_waitqueue_head(&port->port_write_wait);

		dev->dev_port[i] = port;
	}

	return 0;
}

/*
 * gs_free_ports
 *
 * Free all closed ports.  Open ports are disconnected by
 * freeing their write buffers, setting their device pointers
 * and the pointers to them in the device to NULL.  These
 * ports will be freed when closed.
 *
 * The device lock is normally held when calling this function.
 */
static void gs_free_ports(struct gs_dev *dev)
{
	int i;
	unsigned long flags;
	struct gs_port *port;

	if (dev == NULL)
		return;

	for (i=0; i<GS_NUM_PORTS; i++) {
		if ((port=dev->dev_port[i]) != NULL) {
			dev->dev_port[i] = NULL;

			spin_lock_irqsave(&port->port_lock, flags);

			if (port->port_write_buf != NULL) {
				gs_buf_free(port->port_write_buf);
				port->port_write_buf = NULL;
			}

			if (port->port_open_count > 0 || port->port_in_use) {
				port->port_dev = NULL;
				wake_up_interruptible(&port->port_write_wait);
				if (port->port_tty) {
					wake_up_interruptible(&port->port_tty->read_wait);
					wake_up_interruptible(&port->port_tty->write_wait);
				}
				spin_unlock_irqrestore(&port->port_lock, flags);
			} else {
				spin_unlock_irqrestore(&port->port_lock, flags);
				kfree(port);
			}

		}
	}
}

/* Circular Buffer */

/*
 * gs_buf_alloc
 *
 * Allocate a circular buffer and all associated memory.
 */
static struct gs_buf *gs_buf_alloc(unsigned int size, gfp_t kmalloc_flags)
{
	struct gs_buf *gb;

	if (size == 0)
		return NULL;

	gb = (struct gs_buf *)kmalloc(sizeof(struct gs_buf), kmalloc_flags);
	if (gb == NULL)
		return NULL;

	gb->buf_buf = kmalloc(size, kmalloc_flags);
	if (gb->buf_buf == NULL) {
		kfree(gb);
		return NULL;
	}

	gb->buf_size = size;
	gb->buf_get = gb->buf_put = gb->buf_buf;

	return gb;
}

/*
 * gs_buf_free
 *
 * Free the buffer and all associated memory.
 */
void gs_buf_free(struct gs_buf *gb)
{
	if (gb) {
		kfree(gb->buf_buf);
		kfree(gb);
	}
}

/*
 * gs_buf_clear
 *
 * Clear out all data in the circular buffer.
 */
void gs_buf_clear(struct gs_buf *gb)
{
	if (gb != NULL)
		gb->buf_get = gb->buf_put;
		/* equivalent to a get of all data available */
}

/*
 * gs_buf_data_avail
 *
 * Return the number of bytes of data available in the circular
 * buffer.
 */
unsigned int gs_buf_data_avail(struct gs_buf *gb)
{
	if (gb != NULL)
		return (gb->buf_size + gb->buf_put - gb->buf_get) % gb->buf_size;
	else
		return 0;
}

/*
 * gs_buf_space_avail
 *
 * Return the number of bytes of space available in the circular
 * buffer.
 */
unsigned int gs_buf_space_avail(struct gs_buf *gb)
{
	if (gb != NULL)
		return (gb->buf_size + gb->buf_get - gb->buf_put - 1) % gb->buf_size;
	else
		return 0;
}

/*
 * gs_buf_put
 *
 * Copy data data from a user buffer and put it into the circular buffer.
 * Restrict to the amount of space available.
 *
 * Return the number of bytes copied.
 */
unsigned int gs_buf_put(struct gs_buf *gb, const char *buf, unsigned int count)
{
	unsigned int len;

	if (gb == NULL)
		return 0;

	len  = gs_buf_space_avail(gb);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = gb->buf_buf + gb->buf_size - gb->buf_put;
	if (count > len) {
		memcpy(gb->buf_put, buf, len);
		memcpy(gb->buf_buf, buf+len, count - len);
		gb->buf_put = gb->buf_buf + count - len;
	} else {
		memcpy(gb->buf_put, buf, count);
		if (count < len)
			gb->buf_put += count;
		else /* count == len */
			gb->buf_put = gb->buf_buf;
	}

	return count;
}

/*
 * gs_buf_get
 *
 * Get data from the circular buffer and copy to the given buffer.
 * Restrict to the amount of data available.
 *
 * Return the number of bytes copied.
 */
unsigned int gs_buf_get(struct gs_buf *gb, char *buf, unsigned int count)
{
	unsigned int len;

	if (gb == NULL)
		return 0;

	len = gs_buf_data_avail(gb);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = gb->buf_buf + gb->buf_size - gb->buf_get;
	if (count > len) {
		memcpy(buf, gb->buf_get, len);
		memcpy(buf+len, gb->buf_buf, count - len);
		gb->buf_get = gb->buf_buf + count - len;
	} else {
		memcpy(buf, gb->buf_get, count);
		if (count < len)
			gb->buf_get += count;
		else /* count == len */
			gb->buf_get = gb->buf_buf;
	}

	return count;
}
