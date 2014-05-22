/*
 * f_hid.c -- USB HID function driver
 *
 * Copyright (C) 2010 Fabien Chouteau <fabien.chouteau@barco.com>
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
 */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/module.h>
#include <linux/hid.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/usb/g_hid.h>

static int major, minors;
static struct class *hidg_class;

/*-------------------------------------------------------------------------*/
/*                            HID gadget struct                            */

struct f_hidg {
	/* configuration */
	unsigned char bInterfaceSubClass;
	unsigned char bInterfaceProtocol;
	unsigned short report_desc_length;
	char *report_desc;
	unsigned short report_length;

	/* recv report */
	char *set_report_buff;
	unsigned short set_report_length;
	spinlock_t spinlock;
	wait_queue_head_t read_queue;
	struct usb_request *req_out;

	/* send report */
	struct mutex lock;
	bool write_pending;
	wait_queue_head_t write_queue;
	struct usb_request *req;

	int minor;
	struct cdev cdev;
	struct usb_function func;
	struct usb_ep *in_ep;
	struct usb_endpoint_descriptor *fs_in_ep_desc;
	struct usb_endpoint_descriptor *hs_in_ep_desc;
	struct usb_endpoint_descriptor *fs_out_ep_desc;
	struct usb_endpoint_descriptor *hs_out_ep_desc;

	struct usb_composite_dev *u_cdev;

	bool connected;
	/* 4   1 for boot protocol , 0 for report protocol */
	bool boot_protocol;
	bool suspend;
};

bool bypass_input;
static inline struct f_hidg *func_to_hidg(struct usb_function *f)
{
	return container_of(f, struct f_hidg, func);
}

void hidg_disconnect();

/*-------------------------------------------------------------------------*/
/*                           Static descriptors                            */

static struct usb_interface_descriptor hidg_interface_desc = {
	.bLength = sizeof hidg_interface_desc,
	.bDescriptorType = USB_DT_INTERFACE,
	/* .bInterfaceNumber    = DYNAMIC */
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_HID,
	/* .bInterfaceSubClass  = DYNAMIC */
	/* .bInterfaceProtocol  = DYNAMIC */
	/* .iInterface          = DYNAMIC */
};

static struct hid_descriptor hidg_desc = {
	.bLength = sizeof hidg_desc,
	.bDescriptorType = HID_DT_HID,
	.bcdHID = 0x0110,	/* 0x0101, */
	.bCountryCode = 0x00,
	.bNumDescriptors = 0x1,
	/*.desc[0].bDescriptorType      = DYNAMIC */
	/*.desc[0].wDescriptorLenght    = DYNAMIC */
};

/* High-Speed Support */

static struct usb_endpoint_descriptor hidg_hs_in_ep_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = cpu_to_le16(64),
	.bInterval = 4,		/* FIXME: Add this field in the
				 * HID gadget configuration?
				 * (struct hidg_func_descriptor)
				 */
};

static struct usb_descriptor_header *hidg_hs_descriptors[] = {
	(struct usb_descriptor_header *)&hidg_interface_desc,
	(struct usb_descriptor_header *)&hidg_desc,
	(struct usb_descriptor_header *)&hidg_hs_in_ep_desc,
	NULL,
};

/* Full-Speed Support */

static struct usb_endpoint_descriptor hidg_fs_in_ep_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = cpu_to_le16(64),
	.bInterval = 10,	/* FIXME: Add this field in the
				 * HID gadget configuration?
				 * (struct hidg_func_descriptor)
				 */
};

static struct usb_descriptor_header *hidg_fs_descriptors[] = {
	(struct usb_descriptor_header *)&hidg_interface_desc,
	(struct usb_descriptor_header *)&hidg_desc,
	(struct usb_descriptor_header *)&hidg_fs_in_ep_desc,
	NULL,
};

/* hid descriptor for a keyboard */
const struct hidg_func_descriptor my_hid_data = {
	.subclass = 1,		/* No subclass */
	.protocol = 1,		/* 1-Keyboard,2-mouse */
	.report_length = 64,
	.report_desc_length = 150,
	.report_desc = {

			0x05, 0x01,	/*       USAGE_PAGE (Generic Desktop)         */
			0x09, 0x06,	/*       USAGE (Keyboard)                     */
			0xA1, 0x01,	/*       COLLECTION (Application)             */
			0x85, 0x01,	/*   REPORT ID (0x01)                     */
			0x05, 0x07,	/*   USAGE_PAGE (Keyboard)                */
			0x19, 0xE0,	/*   USAGE_MINIMUM (Keyboard LeftControl) */
			0x29, 0xE7,	/*   USAGE_MAXIMUM (Keyboard Right GUI)   */
			0x15, 0x00,	/*   LOGICAL_MINIMUM (0)                  */
			0x25, 0x01,	/*   LOGICAL_MAXIMUM (1)                  */
			0x75, 0x01,	/*   REPORT_SIZE (1)                      */
			0x95, 0x08,	/*   REPORT_COUNT (8)                     */
			0x81, 0x02,	/*   INPUT (Data,Var,Abs)                 */
			0x95, 0x01,	/*   REPORT_COUNT (1)                     */
			0x75, 0x08,	/*   REPORT_SIZE (8)                      */
			0x81, 0x03,	/*   INPUT (Cnst,Var,Abs)                 */
			0x95, 0x05,	/*   REPORT_COUNT (5)                     */
			0x75, 0x01,	/*   REPORT_SIZE (1)                      */
			0x05, 0x08,	/*   USAGE_PAGE (LEDs)                    */
			0x19, 0x01,	/*   USAGE_MINIMUM (Num Lock)             */
			0x29, 0x05,	/*   USAGE_MAXIMUM (Kana)                 */
			0x91, 0x02,	/*   OUTPUT (Data,Var,Abs)                */
			0x95, 0x01,	/*   REPORT_COUNT (1)                     */
			0x75, 0x03,	/*   REPORT_SIZE (3)                      */
			0x91, 0x03,	/*   OUTPUT (Cnst,Var,Abs)                */
			0x95, 0x06,	/*   REPORT_COUNT (6)                     */
			0x75, 0x08,	/*   REPORT_SIZE (8)                      */
			0x15, 0x00,	/*   LOGICAL_MINIMUM (0)                  */
			0x25, 0x65,	/*   LOGICAL_MAXIMUM (101)                */
			0x05, 0x07,	/*   USAGE_PAGE (Keyboard)                */
			0x19, 0x00,	/*   USAGE_MINIMUM (Reserved)             */
			0x29, 0x65,	/*   USAGE_MAXIMUM (Keyboard Application) */
			0x81, 0x00,	/*   INPUT (Data,Ary,Abs)                 */
			0xC0,	/*   END_COLLECTION                       */

			0x05, 0x0C,	/*   USAGE_PAGE (consumer page)           */
			0x09, 0x01,	/*   USAGE (consumer control)             */
			0xA1, 0x01,	/*   COLLECTION (Application)             */
			0x85, 0x03,	/*   REPORT ID (0x03)                     */
			0x15, 0x00,	/*   LOGICAL_MINIMUM (0)                  */
			0x26, 0xFF, 0x02,	/*  LOGICAL_MAXIMUM (0x2FF)         */
			0x19, 0x00,	/*   USAGE_MINIMUM (00)                   */
			0x2A, 0xFF, 0x02,	/*  USAGE_MAXIMUM (0x2FF)           */
			0x75, 0x10,	/*   REPORT_SIZE (16)                     */
			0x95, 0x01,	/*   REPORT_COUNT (1)                     */
			0x81, 0x00,	/*   INPUT (Data,Ary,Abs)                 */
			0xC0,

			0x05, 0x01,	/*   USAGE_PAGE (Generic Desktop)         */
			0x09, 0x02,	/*   USAGE (Mouse)                        */
			0xA1, 0x01,	/*   COLLECTION (Application)             */
			0x85, 0x02,	/*   REPORT ID (0x02)                 */
			0x09, 0x01,	/*   USAGE (Pointer)                  */

			0xA1, 0x00,	/*   COLLECTION (Application)         */
			0x05, 0x09,	/*   USAGE_PAGE (Button)              */
			0x19, 0x01,	/*   USAGE_MINIMUM (Button 1)         */
			0x29, 0x08,	/*   USAGE_MAXIMUM (Button 8)         */
			0x15, 0x00,	/*   LOGICAL_MINIMUM (0)              */
			0x25, 0x01,	/*   LOGICAL_MAXIMUM (1)              */
			0x75, 0x01,	/*   REPORT_SIZE (1)                  */
			0x95, 0x08,	/*   REPORT_COUNT (8)                 */
			0x81, 0x02,	/*   INPUT (Data,Var,Abs)             */
			0x05, 0x01,	/*   USAGE_PAGE (Generic Desktop)     */
			0x09, 0x30,	/*   USAGE (X)                        */
			0x09, 0x31,	/*   USAGE (Y)                        */
			0x16, 0x01, 0xF8,	/* LOGICAL_MINIMUM (-2047)          */
			0x26, 0xFF, 0x07,	/* LOGICAL_MAXIMUM (2047)           */
			0x75, 0x0C,	/*   REPORT_SIZE (12)                 */
			0x95, 0x02,	/*   REPORT_COUNT (2)                 */
			0x81, 0x06,	/*   INPUT (Data,Var,Rel)             */
			0x09, 0x38,
			0x15, 0x81,	/*   LOGICAL_MINIMUM (-127)           */
			0x25, 0x7F,	/*   LOGICAL_MAXIMUM (127)            */
			0x75, 0x08,	/*   REPORT_SIZE (8)                  */
			0x95, 0x01,	/*   REPORT_COUNT (1)                 */
			0x81, 0x06,	/*   INPUT (Data,Var,Rel)             */
			0xC0,	/*   END_COLLECTION                   */

			0xC0	/*   END_COLLECTION                       */
			},
};

struct f_hidg *g_hidg;

/*-------------------------------------------------------------------------*/
/*                              Char Device                                */

static ssize_t f_hidg_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *ptr)
{
	struct f_hidg *hidg = file->private_data;
	char *tmp_buff = NULL;
	unsigned long flags;

	if (!count)
		return 0;

	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	spin_lock_irqsave(&hidg->spinlock, flags);

#define READ_COND (hidg->set_report_buff != NULL)

	while (!READ_COND) {
		spin_unlock_irqrestore(&hidg->spinlock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(hidg->read_queue, READ_COND))
			return -ERESTARTSYS;

		spin_lock_irqsave(&hidg->spinlock, flags);
	}

	count = min_t(unsigned, count, hidg->set_report_length);
	tmp_buff = hidg->set_report_buff;
	hidg->set_report_buff = NULL;

	spin_unlock_irqrestore(&hidg->spinlock, flags);

	if (tmp_buff != NULL) {
		/* copy to user outside spinlock */
		count -= copy_to_user(buffer, tmp_buff, count);
		kfree(tmp_buff);
	} else
		count = -ENOMEM;

	return count;
}

static void f_hidg_req_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_hidg *hidg = (struct f_hidg *)ep->driver_data;

	if (req->status != 0) {
		;
	}

	wake_up(&hidg->write_queue);
}

#define WRITE_COND (!hidg->write_pending)
static ssize_t f_hidg_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *offp)
{
#if 0
	struct f_hidg *hidg = file->private_data;
	ssize_t status = -ENOMEM;

	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;

	mutex_lock(&hidg->lock);

#define WRITE_COND (!hidg->write_pending)

	/* write queue */
	while (!WRITE_COND) {
		mutex_unlock(&hidg->lock);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible_exclusive
		    (hidg->write_queue, WRITE_COND))
			return -ERESTARTSYS;

		mutex_lock(&hidg->lock);
	}

	count = min_t(unsigned, count, hidg->report_length);
	status = copy_from_user(hidg->req->buf, buffer, count);

	if (status != 0) {
		/* ERROR(hidg->func.config->cdev, */
		/*      "copy_from_user error\n"); */
		mutex_unlock(&hidg->lock);
		return -EINVAL;
	}

	hidg->req->status = 0;
	hidg->req->zero = 0;
	hidg->req->length = count;
	hidg->req->complete = f_hidg_req_complete;
	hidg->req->context = hidg;
	hidg->write_pending = 1;

	status = usb_ep_queue(hidg->in_ep, hidg->req, GFP_ATOMIC);
	if (status < 0) {
		/* ERROR(hidg->func.config->cdev,*/
		/*      "usb_ep_queue error on int endpoint %zd\n", status); */
		hidg->write_pending = 0;
		wake_up(&hidg->write_queue);
	} else {
		status = count;
	}

	mutex_unlock(&hidg->lock);
#endif
	return count;
}

static void f_hid_queue_report(u8 *data, int len)
{
	/* this function will run in interrupt context */
	ssize_t status = -ENOMEM;
	struct f_hidg *hidg = g_hidg;
	/* static char raw_report[8]; */

	if (hidg) {
		if (hidg->connected) {
			/* mutex_lock(&hidg->lock); */
			memcpy(hidg->req->buf, data, len);
			hidg->req->status = 0;
			hidg->req->zero = 0;
			hidg->req->length = len;
			/* hidg->req->buf      = raw_report; */
			hidg->req->complete = f_hidg_req_complete;
			hidg->req->context = hidg;

			status =
			    usb_ep_queue(hidg->in_ep, hidg->req, GFP_ATOMIC);
			if (status < 0) {
				/* printk("usb_ep_queue error on int endpoint %zd\n",
				 * 	  status);*/
			}
		}
		/* mutex_unlock(&hidg->lock); */
	}
}

#define KBD_REPORT_ID (0x01)
#define MOUSE_REPORT_ID (0x02)
#define CONSUMER_REPORT_ID (0x03)

unsigned int f_hid_bypass_input_get()
{
	if (!g_hidg)
		return 0;
	else
		return bypass_input;
}

EXPORT_SYMBOL(f_hid_bypass_input_get);

unsigned char kbd_idle[] = { KBD_REPORT_ID, 0, 0, 0, 0, 0, 0, 0, 0 };
unsigned char mouse_idle[] = { MOUSE_REPORT_ID, 0, 0, 0, 0, 0 };
unsigned char consumer_idle[] = { CONSUMER_REPORT_ID, 0, 0 };

static void f_hid_send_idle_report(void)
{
	if (g_hidg) {
		mdelay(2);
		f_hid_queue_report(kbd_idle, sizeof(kbd_idle));
		mdelay(2);
		f_hid_queue_report(mouse_idle, sizeof(mouse_idle));
		mdelay(2);
		f_hid_queue_report(consumer_idle, sizeof(consumer_idle));
	}
}

static void f_hid_bypass_input_set(u8 bypass)
{
	if (g_hidg) {

		u8 current_state = f_hid_bypass_input_get();

		if (bypass && (!current_state)) {
			bypass_input = 1;
		}
		if (!bypass && (current_state)) {
			f_hid_send_idle_report();
			bypass_input = 0;
		}
	}
}

void f_hid_wakeup()
{
	g_hidg->u_cdev->gadget->ops->wakeup(g_hidg->u_cdev->gadget);
}

struct kbd_report {
	u8 id;
	u8 command;
	u8 reserved;
	u8 key_array[6];
} __attribute__ ((packed));

struct consumer_report {
	u8 id;
	u16 data;
} __attribute__ ((packed));

void f_hid_kbd_translate_report(struct hid_report *report, u8 *data)
{
	if (f_hid_bypass_input_get()) {
		int i, j;
		struct kbd_report k = { 0 };
		struct consumer_report c = { 0 };

		struct hid_field *field;

		k.id = KBD_REPORT_ID;	/* report id */
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			/* VARIABLE REPORT */
			if (HID_MAIN_ITEM_VARIABLE & field->flags) {
				for (j = 0; j < field->report_count; j++) {
					if ((field->usage[j].type == EV_KEY)
					    && (field->usage[j].code ==
						KEY_LEFTCTRL))
						k.command |=
						    field->value[j] ? 1 << 0 :
						    0;
					if ((field->usage[j].type == EV_KEY)
					    && (field->usage[j].code ==
						KEY_LEFTSHIFT))
						k.command |=
						    field->value[j] ? 1 << 1 :
						    0;
					if ((field->usage[j].type == EV_KEY)
					    && (field->usage[j].code ==
						KEY_LEFTALT))
						k.command |=
						    field->value[j] ? 1 << 2 :
						    0;
					if ((field->usage[j].type == EV_KEY)
					    && (field->usage[j].code ==
						KEY_LEFTMETA))
						k.command |=
						    field->value[j] ? 1 << 3 :
						    0;
					if ((field->usage[j].type == EV_KEY)
					    && (field->usage[j].code ==
						KEY_RIGHTCTRL))
						k.command |=
						    field->value[j] ? 1 << 4 :
						    0;
					if ((field->usage[j].type == EV_KEY)
					    && (field->usage[j].code ==
						KEY_RIGHTSHIFT))
						k.command |=
						    field->value[j] ? 1 << 5 :
						    0;
					if ((field->usage[j].type == EV_KEY)
					    && (field->usage[j].code ==
						KEY_RIGHTALT))
						k.command |=
						    field->value[j] ? 1 << 6 :
						    0;
					if ((field->usage[j].type == EV_KEY)
					    && (field->usage[j].code ==
						KEY_RIGHTMETA))
						k.command |=
						    field->value[j] ? 1 << 7 :
						    0;
				}
			} else {
				/* ARRAY REPORT */
				if (field->application == HID_GD_KEYBOARD) {
					for (j = 0;
					     j < (min(6, field->report_count));
					     j++) {
						k.key_array[j] =
						    field->value[j];
					}
				}
				if (field->application == 0x000c0001) {
					/* CONSUMER PAGE */
					for (j = 0; j < (field->report_count);
					     j++) {
						c.id = CONSUMER_REPORT_ID;
						c.data = field->value[j];
						f_hid_queue_report((u8 *)&c,
								   sizeof(c));
						return;
					}
				}
			}
		}
		if (g_hidg->boot_protocol)
			f_hid_queue_report((u8 *)&k + 1, sizeof(k) - 1);
		else {
			f_hid_wakeup();
			f_hid_queue_report((u8 *)&k, sizeof(k));
		}
	}
}

EXPORT_SYMBOL(f_hid_kbd_translate_report);

struct mouse_report {
	u8 id:8;
	bool button_left:1;
	bool button_right:1;
	bool button_middle:1;
	bool button_side:1;
	bool button_extra:1;
	bool button_forward:1;
	bool button_back:1;
	bool button_task:1;

	signed x : 12;
	signed y : 12;
	s8 wheel:8;
} __attribute__ ((packed));

void f_hid_mouse_translate_report(struct hid_report *report, u8 *data)
{

	if (f_hid_bypass_input_get() && !g_hidg->boot_protocol) {
		struct mouse_report m = { 0 };
		struct hid_field *field;

		int i, j;
		m.id = MOUSE_REPORT_ID;
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			for (j = 0; j < field->report_count; j++) {
				if ((field->usage[j].type == EV_KEY)
				    && (field->usage[j].code == BTN_LEFT))
					if (field->value[j])
						m.button_left = 1;
				if ((field->usage[j].type == EV_KEY)
				    && (field->usage[j].code == BTN_RIGHT))
					if (field->value[j])
						m.button_right = 1;
				if ((field->usage[j].type == EV_KEY)
				    && (field->usage[j].code == BTN_MIDDLE))
					if (field->value[j])
						m.button_middle = 1;
				if ((field->usage[j].type == EV_KEY)
				    && (field->usage[j].code == BTN_SIDE))
					if (field->value[j])
						m.button_side = 1;
				if ((field->usage[j].type == EV_KEY)
				    && (field->usage[j].code == BTN_EXTRA))
					if (field->value[j])
						m.button_extra = 1;
				if ((field->usage[j].type == EV_KEY)
				    && (field->usage[j].code == BTN_FORWARD))
					if (field->value[j])
						m.button_forward = 1;
				if ((field->usage[j].type == EV_KEY)
				    && (field->usage[j].code == BTN_BACK))
					if (field->value[j])
						m.button_back = 1;
				if ((field->usage[j].type == EV_KEY)
				    && (field->usage[j].code == BTN_TASK))
					if (field->value[j])
						m.button_task = 1;

				if ((field->usage[j].type == EV_REL)
				    && (field->usage[j].code == REL_X))
					m.x = field->value[j];
				if ((field->usage[j].type == EV_REL)
				    && (field->usage[j].code == REL_Y))
					m.y = field->value[j];
				if ((field->usage[j].type == EV_REL)
				    && (field->usage[j].code == REL_WHEEL))
					m.wheel = field->value[j];
			}
		}
		if (m.button_right || m.button_left)
			f_hid_wakeup();
		f_hid_queue_report((u8 *)&m, sizeof(m));
	}
}

EXPORT_SYMBOL(f_hid_mouse_translate_report);

#undef KBD_REPORT_ID
#undef MOUSE_REPORT_ID
#undef CONSUMER_REPORT_ID

static unsigned int f_hidg_poll(struct file *file, poll_table *wait)
{
	return 0;
}

#undef WRITE_COND
#undef READ_COND

static int f_hidg_release(struct inode *inode, struct file *fd)
{
	fd->private_data = NULL;
	return 0;
}

static int f_hidg_open(struct inode *inode, struct file *fd)
{
	struct f_hidg *hidg = container_of(inode->i_cdev, struct f_hidg, cdev);

	fd->private_data = hidg;

	return 0;
}

/*-------------------------------------------------------------------------*/
/*                                usb_function                             */

void hidg_connect()
{
	if (g_hidg)
		g_hidg->connected = 1;
}

void hidg_disconnect()
{
	if (g_hidg) {
		g_hidg->connected = 0;
	}
}

EXPORT_SYMBOL(hidg_disconnect);
int hidg_start(struct usb_composite_dev *cdev);

static void hidg_set_report_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_hidg *hidg = (struct f_hidg *)req->context;
	printk("hidg_set_report_complete ,req->status = %d len = %d\n",
	       req->status, req->actual);
	if (req->status != 0 || req->buf == NULL || req->actual == 0) {
		return;
	}

	spin_lock(&hidg->spinlock);

	if (!hidg->connected)
		hidg_connect();

	hidg->set_report_buff = krealloc(hidg->set_report_buff,
					 req->actual, GFP_ATOMIC);

	if (hidg->set_report_buff == NULL) {
		spin_unlock(&hidg->spinlock);
		return;
	}
	hidg->set_report_length = req->actual;
	memcpy(hidg->set_report_buff, req->buf, req->actual);

	spin_unlock(&hidg->spinlock);

	wake_up(&hidg->read_queue);
}

static int hidg_setup(struct usb_function *f,
		      const struct usb_ctrlrequest *ctrl)
{
	struct f_hidg *hidg = func_to_hidg(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = cdev->req;
	int status = 0;
	__u16 value, length;

	value = __le16_to_cpu(ctrl->wValue);
	length = __le16_to_cpu(ctrl->wLength);

	VDBG(cdev, "hid_setup crtl_request : bRequestType:0x%x bRequest:0x%x "
	     "Value:0x%x\n", ctrl->bRequestType, ctrl->bRequest, value);

	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_GET_REPORT):
		VDBG(cdev,
		     "get_report\n");

		/* send an empty report */
		length = min_t(unsigned, length, hidg->report_length);
		memset(req->buf, 0x0, length);

		goto respond;
		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_GET_PROTOCOL):
		VDBG(cdev,
		     "get_protocol\n");
		goto stall;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_SET_REPORT):
		VDBG(cdev, "set_report | wLenght=%d\n",
		     ctrl->wLength);
		req->context = hidg;
		req->complete = hidg_set_report_complete;
		goto respond;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_SET_PROTOCOL):
		VDBG(cdev,
		     "set_protocol\n");
		goto stall;
		break;

	case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8 | USB_REQ_GET_DESCRIPTOR):
		switch (value >> 8) {
		case HID_DT_REPORT:
			VDBG(cdev, "USB_REQ_GET_DESCRIPTOR: REPORT\n");
			length = min_t(unsigned short, length,
				       hidg->report_desc_length);
			memcpy(req->buf, hidg->report_desc, length);
			goto respond;
			break;

		default:
			VDBG(cdev, "Unknown decriptor request 0x%x\n",
			     value >> 8);
			goto stall;
			break;
		}
		break;

	default:
		VDBG(cdev, "Unknown request 0x%x\n", ctrl->bRequest);
		goto stall;
		break;
	}

stall:
	return -EOPNOTSUPP;

respond:
	req->zero = 0;
	req->length = length;
	status = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
	/* if (status < 0) ; */
	/* ERROR(cdev, "usb_ep_queue error on ep0 %d\n", value);*/
	return status;
}

static int hidg_ctrlrequest(struct usb_composite_dev *cdev,
			    const struct usb_ctrlrequest *ctrl)
{
	struct f_hidg *hidg = g_hidg;
	struct usb_request *req = cdev->req;
	int status = 0;
	__u16 value, length;

	value = __le16_to_cpu(ctrl->wValue);
	length = __le16_to_cpu(ctrl->wLength);

	/*
	   printk("hid_setup crtl_request : bRequestType:0x%x bRequest:0x%x "
	   "Value:0x%x\n", ctrl->bRequestType, ctrl->bRequest, value);
	 */
	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_GET_REPORT):
		VDBG(cdev,
		     "get_report\n");
		return -EOPNOTSUPP;	/* this command bypass to rndis */
		/* send an empty report */
		length = min_t(unsigned, length, hidg->report_length);
		memset(req->buf, 0x0, length);
		goto respond;
		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_GET_PROTOCOL):
		VDBG(cdev,
		     "get_protocol\n");
		goto stall;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_SET_REPORT):
		VDBG(cdev, "set_report | wLenght=%d\n",
		     ctrl->wLength);
		req->context = hidg;
		req->complete = hidg_set_report_complete;
		goto respond;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_SET_IDLE):
		VDBG(cdev, "set_report | wLenght=%d\n",
		     ctrl->wLength);
		req->context = hidg;
		req->complete = hidg_set_report_complete;
		goto respond;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8 | HID_REQ_SET_PROTOCOL):
		VDBG(cdev,
		     "set_protocol\n");
		req->context = hidg;
		req->complete = hidg_set_report_complete;
		hidg->boot_protocol = 1;
		hidg_start(cdev);
		goto respond;
		break;

	case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8 | USB_REQ_GET_DESCRIPTOR):
		switch (value >> 8) {
		case HID_DT_REPORT:
			VDBG(cdev, "USB_REQ_GET_DESCRIPTOR: REPORT\n");
			length = min_t(unsigned short, length,
				       hidg->report_desc_length);
			memcpy(req->buf, hidg->report_desc, length);
			hidg->boot_protocol = 0;
			goto respond;
			break;

		default:
			VDBG(cdev, "Unknown decriptor request 0x%x\n",
			     value >> 8);
			goto stall;
			break;
		}
		break;

	default:
		VDBG(cdev, "Unknown request 0x%x\n", ctrl->bRequest);
		goto stall;
		break;
	}

stall:
	return -EOPNOTSUPP;

respond:
	req->zero = 0;
	req->length = length;
	status = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
	/* if (status < 0) ; */
	/* ERROR(cdev, "usb_ep_queue error on ep0 %d\n", value);*/
	return status;
}

static void hidg_disable(struct usb_function *f)
{
	struct f_hidg *hidg = func_to_hidg(f);

	usb_ep_disable(hidg->in_ep);
	hidg->in_ep->driver_data = NULL;
}

static int hidg_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct f_hidg *hidg = func_to_hidg(f);
	const struct usb_endpoint_descriptor *ep_desc;
	int status = 0;
	VDBG(cdev, "hidg_set_alt intf:%d alt:%d\n", intf, alt);

	printk("^^^^^hidg_set_alt\n");

	if (hidg->in_ep != NULL) {
		/* restart endpoint */
		if (hidg->in_ep->driver_data != NULL)
			usb_ep_disable(hidg->in_ep);

		ep_desc = ep_choose(f->config->cdev->gadget,
				    hidg->hs_in_ep_desc, hidg->fs_in_ep_desc);
		status = usb_ep_enable(hidg->in_ep, ep_desc);
		if (status < 0) {
			/* ERROR(cdev, "Enable endpoint FAILED!\n"); */
			goto fail;
		}
		hidg->in_ep->driver_data = hidg;
	}
fail:
	return status;
}

int hidg_start(struct usb_composite_dev *cdev)
{
	printk("^^^^^hidg_start\n");
	struct f_hidg *hidg = g_hidg;
	const struct usb_endpoint_descriptor *ep_desc;
	int status = 0;

	if (hidg->in_ep != NULL) {
		/* restart endpoint */
		if (hidg->in_ep->driver_data != NULL)
			usb_ep_disable(hidg->in_ep);

		ep_desc = ep_choose(cdev->gadget,
				    hidg->hs_in_ep_desc, hidg->fs_in_ep_desc);
		status = usb_ep_enable(hidg->in_ep, ep_desc);
		if (status < 0) {
			printk("Enable endpoint FAILED!\n");
			goto fail;
		}
		hidg->in_ep->driver_data = hidg;
	}
fail:
	return status;
}

const struct file_operations f_hidg_fops = {
	.owner = THIS_MODULE,
	.open = f_hidg_open,
	.release = f_hidg_release,
	.write = NULL,		/* f_hidg_write,disable write to /dev/hidg0 */
	.read = f_hidg_read,
	.poll = NULL,		/* f_hidg_poll, */
	.llseek = noop_llseek,
};

static int hidg_bind(struct usb_configuration *c, struct usb_function *f)
{

	struct usb_ep *ep_in;
	struct f_hidg *hidg = func_to_hidg(f);
	int status;
	dev_t dev;
	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	hidg_interface_desc.bInterfaceNumber = status;

	/* allocate instance-specific endpoints */
	status = -ENODEV;
	ep_in = usb_ep_autoconfig(c->cdev->gadget, &hidg_fs_in_ep_desc);
	if (!ep_in)
		goto fail;
	ep_in->driver_data = c->cdev;	/* claim */
	hidg->in_ep = ep_in;

	/* preallocate request and buffer */
	status = -ENOMEM;
	hidg->req = usb_ep_alloc_request(hidg->in_ep, GFP_KERNEL);
	if (!hidg->req)
		goto fail;

	hidg->req->buf = kmalloc(hidg->report_length, GFP_KERNEL);
	if (!hidg->req->buf)
		goto fail;

	/* set descriptor dynamic values */
	hidg_interface_desc.bInterfaceSubClass = hidg->bInterfaceSubClass;
	hidg_interface_desc.bInterfaceProtocol = hidg->bInterfaceProtocol;
	/* hidg_hs_in_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length); */
	/* hidg_fs_in_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length); */
	/* hidg_hs_out_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length); */
	/* hidg_fs_out_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length); */
	hidg_desc.desc[0].bDescriptorType = HID_DT_REPORT;
	hidg_desc.desc[0].wDescriptorLength =
		cpu_to_le16(hidg->report_desc_length);

	hidg->set_report_buff = NULL;

	/* copy descriptors */
	f->descriptors = usb_copy_descriptors(hidg_fs_descriptors);
	if (!f->descriptors)
		goto fail;

	hidg->fs_in_ep_desc = usb_find_endpoint(hidg_fs_descriptors,
						f->descriptors,
						&hidg_fs_in_ep_desc);

	if (gadget_is_dualspeed(c->cdev->gadget)) {
		hidg_hs_in_ep_desc.bEndpointAddress =
		    hidg_fs_in_ep_desc.bEndpointAddress;
		f->hs_descriptors = usb_copy_descriptors(hidg_hs_descriptors);
		if (!f->hs_descriptors)
			goto fail;
		hidg->hs_in_ep_desc = usb_find_endpoint(hidg_hs_descriptors,
							f->hs_descriptors,
							&hidg_hs_in_ep_desc);
	} else {
		hidg->hs_in_ep_desc = NULL;
	}

	hidg->connected = 0;

	mutex_init(&hidg->lock);
	spin_lock_init(&hidg->spinlock);
	init_waitqueue_head(&hidg->write_queue);
	init_waitqueue_head(&hidg->read_queue);

	/* create char device */
	cdev_init(&hidg->cdev, &f_hidg_fops);
	dev = MKDEV(major, hidg->minor);
	status = cdev_add(&hidg->cdev, dev, 1);
	if (status)
		goto fail;

	device_create(hidg_class, NULL, dev, NULL, "%s%d", "hidg", hidg->minor);

	return 0;

fail:
	/* ERROR(f->config->cdev, "hidg_bind FAILED\n"); */
	if (hidg->req != NULL) {
		kfree(hidg->req->buf);
		if (hidg->in_ep != NULL)
			usb_ep_free_request(hidg->in_ep, hidg->req);
	}
	g_hidg = NULL;
	usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);

	return status;
}

static void hidg_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_hidg *hidg = func_to_hidg(f);
	hidg_disconnect();

	device_destroy(hidg_class, MKDEV(major, hidg->minor));
	cdev_del(&hidg->cdev);

	/* disable/free request and end point */
	usb_ep_disable(hidg->in_ep);
	usb_ep_dequeue(hidg->in_ep, hidg->req);
	if (hidg->req->buf)
		kfree(hidg->req->buf);
	usb_ep_free_request(hidg->in_ep, hidg->req);

	/* free descriptors copies */
	usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);

	kfree(hidg->report_desc);
	kfree(hidg->set_report_buff);
	kfree(hidg);

	g_hidg = NULL;
}

/*-------------------------------------------------------------------------*/
/*                                 Strings                                 */

#define CT_FUNC_HID_IDX	0

static struct usb_string ct_func_string_defs[] = {
	[CT_FUNC_HID_IDX].s = "HID Interface",
	{},			/* end of list */
};

static struct usb_gadget_strings ct_func_string_table = {
	.language = 0x0409,	/* en-US */
	.strings = ct_func_string_defs,
};

static struct usb_gadget_strings *ct_func_strings[] = {
	&ct_func_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/
/*                             usb_configuration                           */

int hidg_bind_config(struct usb_configuration *c,
		     const struct hidg_func_descriptor *fdesc, int index)
{
	struct f_hidg *hidg;
	int status;
	if (index >= minors)
		return -ENOENT;

	/* maybe allocate device-global string IDs, and patch descriptors */
	if (ct_func_string_defs[CT_FUNC_HID_IDX].id == 0) {
		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		ct_func_string_defs[CT_FUNC_HID_IDX].id = status;
		hidg_interface_desc.iInterface = status;
	}

	/* allocate and initialize one new instance */
	hidg = kzalloc(sizeof *hidg, GFP_KERNEL);
	if (!hidg)
		return -ENOMEM;
	g_hidg = hidg;
	hidg->boot_protocol = 1;
	hidg->connected = 0;
	hidg->minor = index;
	hidg->bInterfaceSubClass = fdesc->subclass;
	hidg->bInterfaceProtocol = fdesc->protocol;
	hidg->report_length = fdesc->report_length;
	hidg->report_desc_length = fdesc->report_desc_length;
	hidg->report_desc = kmemdup(fdesc->report_desc,
				    fdesc->report_desc_length, GFP_KERNEL);
	if (!hidg->report_desc) {
		kfree(hidg);
		return -ENOMEM;
	}

	hidg->func.name = "hid";
	hidg->func.strings = ct_func_strings;
	hidg->func.bind = hidg_bind;
	hidg->func.unbind = hidg_unbind;
	hidg->func.set_alt = hidg_set_alt;
	hidg->func.disable = hidg_disable;
	hidg->func.setup = hidg_setup;

	status = usb_add_function(c, &hidg->func);
	if (status)
		kfree(hidg);
	else
		g_hidg = hidg;
	g_hidg->u_cdev = c->cdev;
	return status;
}

int ghid_setup(struct usb_gadget *g, int count)
{
	int status;
	dev_t dev;

	hidg_class = class_create(THIS_MODULE, "hidg");

	status = alloc_chrdev_region(&dev, 0, count, "hidg");
	if (!status) {
		major = MAJOR(dev);
		minors = count;
	}

	return status;
}

void ghid_cleanup(void)
{
	if (major) {
		unregister_chrdev_region(MKDEV(major, 0), minors);
		major = minors = 0;
	}

	class_destroy(hidg_class);
}
