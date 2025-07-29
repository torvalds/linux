// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   imon.c:	input and display driver for SoundGraph iMON IR/VFD/LCD
 *
 *   Copyright(C) 2010  Jarod Wilson <jarod@wilsonet.com>
 *   Portions based on the original lirc_imon driver,
 *	Copyright(C) 2004  Venky Raju(dev@venky.ws)
 *
 *   Huge thanks to R. Geoff Newbury for invaluable debugging on the
 *   0xffdc iMON devices, and for sending me one to hack on, without
 *   which the support for them wouldn't be nearly as good. Thanks
 *   also to the numerous 0xffdc device owners that tested auto-config
 *   support for me and provided debug dumps from their devices.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ratelimit.h>

#include <linux/input.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

#include <linux/timer.h>

#define MOD_AUTHOR	"Jarod Wilson <jarod@wilsonet.com>"
#define MOD_DESC	"Driver for SoundGraph iMON MultiMedia IR/Display"
#define MOD_NAME	"imon"
#define MOD_VERSION	"0.9.4"

#define DISPLAY_MINOR_BASE	144
#define DEVICE_NAME	"lcd%d"

#define IMON_CLOCK_ENABLE_PACKETS	2

/*** P R O T O T Y P E S ***/

/* USB Callback prototypes */
static int imon_probe(struct usb_interface *interface,
		      const struct usb_device_id *id);
static void imon_disconnect(struct usb_interface *interface);
static void usb_rx_callback_intf0(struct urb *urb);
static void usb_rx_callback_intf1(struct urb *urb);
static void usb_tx_callback(struct urb *urb);

/* suspend/resume support */
static int imon_resume(struct usb_interface *intf);
static int imon_suspend(struct usb_interface *intf, pm_message_t message);

/* Display file_operations function prototypes */
static int display_open(struct inode *inode, struct file *file);
static int display_close(struct inode *inode, struct file *file);

/* VFD write operation */
static ssize_t vfd_write(struct file *file, const char __user *buf,
			 size_t n_bytes, loff_t *pos);

/* LCD file_operations override function prototypes */
static ssize_t lcd_write(struct file *file, const char __user *buf,
			 size_t n_bytes, loff_t *pos);

/*** G L O B A L S ***/

struct imon_panel_key_table {
	u64 hw_code;
	u32 keycode;
};

struct imon_usb_dev_descr {
	__u16 flags;
#define IMON_NO_FLAGS 0
#define IMON_NEED_20MS_PKT_DELAY 1
#define IMON_SUPPRESS_REPEATED_KEYS 2
	struct imon_panel_key_table key_table[];
};

struct imon_context {
	struct device *dev;
	/* Newer devices have two interfaces */
	struct usb_device *usbdev_intf0;
	struct usb_device *usbdev_intf1;

	bool display_supported;		/* not all controllers do */
	bool display_isopen;		/* display port has been opened */
	bool rf_device;			/* true if iMON 2.4G LT/DT RF device */
	bool rf_isassociating;		/* RF remote associating */
	bool dev_present_intf0;		/* USB device presence, interface 0 */
	bool dev_present_intf1;		/* USB device presence, interface 1 */

	struct mutex lock;		/* to lock this object */
	wait_queue_head_t remove_ok;	/* For unexpected USB disconnects */

	struct usb_endpoint_descriptor *rx_endpoint_intf0;
	struct usb_endpoint_descriptor *rx_endpoint_intf1;
	struct usb_endpoint_descriptor *tx_endpoint;
	struct urb *rx_urb_intf0;
	struct urb *rx_urb_intf1;
	struct urb *tx_urb;
	bool tx_control;
	unsigned char usb_rx_buf[8];
	unsigned char usb_tx_buf[8];
	unsigned int send_packet_delay;

	struct tx_t {
		unsigned char data_buf[35];	/* user data buffer */
		struct completion finished;	/* wait for write to finish */
		bool busy;			/* write in progress */
		int status;			/* status of tx completion */
	} tx;

	u16 vendor;			/* usb vendor ID */
	u16 product;			/* usb product ID */

	struct rc_dev *rdev;		/* rc-core device for remote */
	struct input_dev *idev;		/* input device for panel & IR mouse */
	struct input_dev *touch;	/* input device for touchscreen */

	spinlock_t kc_lock;		/* make sure we get keycodes right */
	u32 kc;				/* current input keycode */
	u32 last_keycode;		/* last reported input keycode */
	u32 rc_scancode;		/* the computed remote scancode */
	u8 rc_toggle;			/* the computed remote toggle bit */
	u64 rc_proto;			/* iMON or MCE (RC6) IR protocol? */
	bool release_code;		/* some keys send a release code */

	u8 display_type;		/* store the display type */
	bool pad_mouse;			/* toggle kbd(0)/mouse(1) mode */

	char name_rdev[128];		/* rc input device name */
	char phys_rdev[64];		/* rc input device phys path */

	char name_idev[128];		/* input device name */
	char phys_idev[64];		/* input device phys path */

	char name_touch[128];		/* touch screen name */
	char phys_touch[64];		/* touch screen phys path */
	struct timer_list ttimer;	/* touch screen timer */
	int touch_x;			/* x coordinate on touchscreen */
	int touch_y;			/* y coordinate on touchscreen */
	const struct imon_usb_dev_descr *dev_descr;
					/* device description with key */
					/* table for front panels */
	/*
	 * Fields for deferring free_imon_context().
	 *
	 * Since reference to "struct imon_context" is stored into
	 * "struct file"->private_data, we need to remember
	 * how many file descriptors might access this "struct imon_context".
	 */
	refcount_t users;
	/*
	 * Use a flag for telling display_open()/vfd_write()/lcd_write() that
	 * imon_disconnect() was already called.
	 */
	bool disconnected;
	/*
	 * We need to wait for RCU grace period in order to allow
	 * display_open() to safely check ->disconnected and increment ->users.
	 */
	struct rcu_head rcu;
};

#define TOUCH_TIMEOUT	(HZ/30)

/* vfd character device file operations */
static const struct file_operations vfd_fops = {
	.owner		= THIS_MODULE,
	.open		= display_open,
	.write		= vfd_write,
	.release	= display_close,
	.llseek		= noop_llseek,
};

/* lcd character device file operations */
static const struct file_operations lcd_fops = {
	.owner		= THIS_MODULE,
	.open		= display_open,
	.write		= lcd_write,
	.release	= display_close,
	.llseek		= noop_llseek,
};

enum {
	IMON_DISPLAY_TYPE_AUTO = 0,
	IMON_DISPLAY_TYPE_VFD  = 1,
	IMON_DISPLAY_TYPE_LCD  = 2,
	IMON_DISPLAY_TYPE_VGA  = 3,
	IMON_DISPLAY_TYPE_NONE = 4,
};

enum {
	IMON_KEY_IMON	= 0,
	IMON_KEY_MCE	= 1,
	IMON_KEY_PANEL	= 2,
};

static struct usb_class_driver imon_vfd_class = {
	.name		= DEVICE_NAME,
	.fops		= &vfd_fops,
	.minor_base	= DISPLAY_MINOR_BASE,
};

static struct usb_class_driver imon_lcd_class = {
	.name		= DEVICE_NAME,
	.fops		= &lcd_fops,
	.minor_base	= DISPLAY_MINOR_BASE,
};

/* imon receiver front panel/knob key table */
static const struct imon_usb_dev_descr imon_default_table = {
	.flags = IMON_NO_FLAGS,
	.key_table = {
		{ 0x000000000f00ffeell, KEY_MEDIA }, /* Go */
		{ 0x000000001200ffeell, KEY_UP },
		{ 0x000000001300ffeell, KEY_DOWN },
		{ 0x000000001400ffeell, KEY_LEFT },
		{ 0x000000001500ffeell, KEY_RIGHT },
		{ 0x000000001600ffeell, KEY_ENTER },
		{ 0x000000001700ffeell, KEY_ESC },
		{ 0x000000001f00ffeell, KEY_AUDIO },
		{ 0x000000002000ffeell, KEY_VIDEO },
		{ 0x000000002100ffeell, KEY_CAMERA },
		{ 0x000000002700ffeell, KEY_DVD },
		{ 0x000000002300ffeell, KEY_TV },
		{ 0x000000002b00ffeell, KEY_EXIT },
		{ 0x000000002c00ffeell, KEY_SELECT },
		{ 0x000000002d00ffeell, KEY_MENU },
		{ 0x000000000500ffeell, KEY_PREVIOUS },
		{ 0x000000000700ffeell, KEY_REWIND },
		{ 0x000000000400ffeell, KEY_STOP },
		{ 0x000000003c00ffeell, KEY_PLAYPAUSE },
		{ 0x000000000800ffeell, KEY_FASTFORWARD },
		{ 0x000000000600ffeell, KEY_NEXT },
		{ 0x000000010000ffeell, KEY_RIGHT },
		{ 0x000001000000ffeell, KEY_LEFT },
		{ 0x000000003d00ffeell, KEY_SELECT },
		{ 0x000100000000ffeell, KEY_VOLUMEUP },
		{ 0x010000000000ffeell, KEY_VOLUMEDOWN },
		{ 0x000000000100ffeell, KEY_MUTE },
		/* 0xffdc iMON MCE VFD */
		{ 0x00010000ffffffeell, KEY_VOLUMEUP },
		{ 0x01000000ffffffeell, KEY_VOLUMEDOWN },
		{ 0x00000001ffffffeell, KEY_MUTE },
		{ 0x0000000fffffffeell, KEY_MEDIA },
		{ 0x00000012ffffffeell, KEY_UP },
		{ 0x00000013ffffffeell, KEY_DOWN },
		{ 0x00000014ffffffeell, KEY_LEFT },
		{ 0x00000015ffffffeell, KEY_RIGHT },
		{ 0x00000016ffffffeell, KEY_ENTER },
		{ 0x00000017ffffffeell, KEY_ESC },
		/* iMON Knob values */
		{ 0x000100ffffffffeell, KEY_VOLUMEUP },
		{ 0x010000ffffffffeell, KEY_VOLUMEDOWN },
		{ 0x000008ffffffffeell, KEY_MUTE },
		{ 0, KEY_RESERVED },
	}
};

static const struct imon_usb_dev_descr imon_OEM_VFD = {
	.flags = IMON_NEED_20MS_PKT_DELAY,
	.key_table = {
		{ 0x000000000f00ffeell, KEY_MEDIA }, /* Go */
		{ 0x000000001200ffeell, KEY_UP },
		{ 0x000000001300ffeell, KEY_DOWN },
		{ 0x000000001400ffeell, KEY_LEFT },
		{ 0x000000001500ffeell, KEY_RIGHT },
		{ 0x000000001600ffeell, KEY_ENTER },
		{ 0x000000001700ffeell, KEY_ESC },
		{ 0x000000001f00ffeell, KEY_AUDIO },
		{ 0x000000002b00ffeell, KEY_EXIT },
		{ 0x000000002c00ffeell, KEY_SELECT },
		{ 0x000000002d00ffeell, KEY_MENU },
		{ 0x000000000500ffeell, KEY_PREVIOUS },
		{ 0x000000000700ffeell, KEY_REWIND },
		{ 0x000000000400ffeell, KEY_STOP },
		{ 0x000000003c00ffeell, KEY_PLAYPAUSE },
		{ 0x000000000800ffeell, KEY_FASTFORWARD },
		{ 0x000000000600ffeell, KEY_NEXT },
		{ 0x000000010000ffeell, KEY_RIGHT },
		{ 0x000001000000ffeell, KEY_LEFT },
		{ 0x000000003d00ffeell, KEY_SELECT },
		{ 0x000100000000ffeell, KEY_VOLUMEUP },
		{ 0x010000000000ffeell, KEY_VOLUMEDOWN },
		{ 0x000000000100ffeell, KEY_MUTE },
		/* 0xffdc iMON MCE VFD */
		{ 0x00010000ffffffeell, KEY_VOLUMEUP },
		{ 0x01000000ffffffeell, KEY_VOLUMEDOWN },
		{ 0x00000001ffffffeell, KEY_MUTE },
		{ 0x0000000fffffffeell, KEY_MEDIA },
		{ 0x00000012ffffffeell, KEY_UP },
		{ 0x00000013ffffffeell, KEY_DOWN },
		{ 0x00000014ffffffeell, KEY_LEFT },
		{ 0x00000015ffffffeell, KEY_RIGHT },
		{ 0x00000016ffffffeell, KEY_ENTER },
		{ 0x00000017ffffffeell, KEY_ESC },
		/* iMON Knob values */
		{ 0x000100ffffffffeell, KEY_VOLUMEUP },
		{ 0x010000ffffffffeell, KEY_VOLUMEDOWN },
		{ 0x000008ffffffffeell, KEY_MUTE },
		{ 0, KEY_RESERVED },
	}
};

/* imon receiver front panel/knob key table for DH102*/
static const struct imon_usb_dev_descr imon_DH102 = {
	.flags = IMON_NO_FLAGS,
	.key_table = {
		{ 0x000100000000ffeell, KEY_VOLUMEUP },
		{ 0x010000000000ffeell, KEY_VOLUMEDOWN },
		{ 0x000000010000ffeell, KEY_MUTE },
		{ 0x0000000f0000ffeell, KEY_MEDIA },
		{ 0x000000120000ffeell, KEY_UP },
		{ 0x000000130000ffeell, KEY_DOWN },
		{ 0x000000140000ffeell, KEY_LEFT },
		{ 0x000000150000ffeell, KEY_RIGHT },
		{ 0x000000160000ffeell, KEY_ENTER },
		{ 0x000000170000ffeell, KEY_ESC },
		{ 0x0000002b0000ffeell, KEY_EXIT },
		{ 0x0000002c0000ffeell, KEY_SELECT },
		{ 0x0000002d0000ffeell, KEY_MENU },
		{ 0, KEY_RESERVED }
	}
};

/* imon ultrabay front panel key table */
static const struct imon_usb_dev_descr ultrabay_table = {
	.flags = IMON_SUPPRESS_REPEATED_KEYS,
	.key_table = {
		{ 0x0000000f0000ffeell, KEY_MEDIA },      /* Go */
		{ 0x000000000100ffeell, KEY_UP },
		{ 0x000000000001ffeell, KEY_DOWN },
		{ 0x000000160000ffeell, KEY_ENTER },
		{ 0x0000001f0000ffeell, KEY_AUDIO },      /* Music */
		{ 0x000000200000ffeell, KEY_VIDEO },      /* Movie */
		{ 0x000000210000ffeell, KEY_CAMERA },     /* Photo */
		{ 0x000000270000ffeell, KEY_DVD },        /* DVD */
		{ 0x000000230000ffeell, KEY_TV },         /* TV */
		{ 0x000000050000ffeell, KEY_PREVIOUS },   /* Previous */
		{ 0x000000070000ffeell, KEY_REWIND },
		{ 0x000000040000ffeell, KEY_STOP },
		{ 0x000000020000ffeell, KEY_PLAYPAUSE },
		{ 0x000000080000ffeell, KEY_FASTFORWARD },
		{ 0x000000060000ffeell, KEY_NEXT },       /* Next */
		{ 0x000100000000ffeell, KEY_VOLUMEUP },
		{ 0x010000000000ffeell, KEY_VOLUMEDOWN },
		{ 0x000000010000ffeell, KEY_MUTE },
		{ 0, KEY_RESERVED },
	}
};

/*
 * USB Device ID for iMON USB Control Boards
 *
 * The Windows drivers contain 6 different inf files, more or less one for
 * each new device until the 0x0034-0x0046 devices, which all use the same
 * driver. Some of the devices in the 34-46 range haven't been definitively
 * identified yet. Early devices have either a TriGem Computer, Inc. or a
 * Samsung vendor ID (0x0aa8 and 0x04e8 respectively), while all later
 * devices use the SoundGraph vendor ID (0x15c2). This driver only supports
 * the ffdc and later devices, which do onboard decoding.
 */
static const struct usb_device_id imon_usb_id_table[] = {
	/*
	 * Several devices with this same device ID, all use iMON_PAD.inf
	 * SoundGraph iMON PAD (IR & VFD)
	 * SoundGraph iMON PAD (IR & LCD)
	 * SoundGraph iMON Knob (IR only)
	 */
	{ USB_DEVICE(0x15c2, 0xffdc),
	  .driver_info = (unsigned long)&imon_default_table },

	/*
	 * Newer devices, all driven by the latest iMON Windows driver, full
	 * list of device IDs extracted via 'strings Setup/data1.hdr |grep 15c2'
	 * Need user input to fill in details on unknown devices.
	 */
	/* SoundGraph iMON OEM Touch LCD (IR & 7" VGA LCD) */
	{ USB_DEVICE(0x15c2, 0x0034),
	  .driver_info = (unsigned long)&imon_DH102 },
	/* SoundGraph iMON OEM Touch LCD (IR & 4.3" VGA LCD) */
	{ USB_DEVICE(0x15c2, 0x0035),
	  .driver_info = (unsigned long)&imon_default_table},
	/* SoundGraph iMON OEM VFD (IR & VFD) */
	{ USB_DEVICE(0x15c2, 0x0036),
	  .driver_info = (unsigned long)&imon_OEM_VFD },
	/* device specifics unknown */
	{ USB_DEVICE(0x15c2, 0x0037),
	  .driver_info = (unsigned long)&imon_default_table},
	/* SoundGraph iMON OEM LCD (IR & LCD) */
	{ USB_DEVICE(0x15c2, 0x0038),
	  .driver_info = (unsigned long)&imon_default_table},
	/* SoundGraph iMON UltraBay (IR & LCD) */
	{ USB_DEVICE(0x15c2, 0x0039),
	  .driver_info = (unsigned long)&imon_default_table},
	/* device specifics unknown */
	{ USB_DEVICE(0x15c2, 0x003a),
	  .driver_info = (unsigned long)&imon_default_table},
	/* device specifics unknown */
	{ USB_DEVICE(0x15c2, 0x003b),
	  .driver_info = (unsigned long)&imon_default_table},
	/* SoundGraph iMON OEM Inside (IR only) */
	{ USB_DEVICE(0x15c2, 0x003c),
	  .driver_info = (unsigned long)&imon_default_table},
	/* device specifics unknown */
	{ USB_DEVICE(0x15c2, 0x003d),
	  .driver_info = (unsigned long)&imon_default_table},
	/* device specifics unknown */
	{ USB_DEVICE(0x15c2, 0x003e),
	  .driver_info = (unsigned long)&imon_default_table},
	/* device specifics unknown */
	{ USB_DEVICE(0x15c2, 0x003f),
	  .driver_info = (unsigned long)&imon_default_table},
	/* device specifics unknown */
	{ USB_DEVICE(0x15c2, 0x0040),
	  .driver_info = (unsigned long)&imon_default_table},
	/* SoundGraph iMON MINI (IR only) */
	{ USB_DEVICE(0x15c2, 0x0041),
	  .driver_info = (unsigned long)&imon_default_table},
	/* Antec Veris Multimedia Station EZ External (IR only) */
	{ USB_DEVICE(0x15c2, 0x0042),
	  .driver_info = (unsigned long)&imon_default_table},
	/* Antec Veris Multimedia Station Basic Internal (IR only) */
	{ USB_DEVICE(0x15c2, 0x0043),
	  .driver_info = (unsigned long)&imon_default_table},
	/* Antec Veris Multimedia Station Elite (IR & VFD) */
	{ USB_DEVICE(0x15c2, 0x0044),
	  .driver_info = (unsigned long)&imon_default_table},
	/* Antec Veris Multimedia Station Premiere (IR & LCD) */
	{ USB_DEVICE(0x15c2, 0x0045),
	  .driver_info = (unsigned long)&imon_default_table},
	/* device specifics unknown */
	{ USB_DEVICE(0x15c2, 0x0046),
	  .driver_info = (unsigned long)&imon_default_table},
	{}
};

/* USB Device data */
static struct usb_driver imon_driver = {
	.name		= MOD_NAME,
	.probe		= imon_probe,
	.disconnect	= imon_disconnect,
	.suspend	= imon_suspend,
	.resume		= imon_resume,
	.id_table	= imon_usb_id_table,
};

/* Module bookkeeping bits */
MODULE_AUTHOR(MOD_AUTHOR);
MODULE_DESCRIPTION(MOD_DESC);
MODULE_VERSION(MOD_VERSION);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, imon_usb_id_table);

static bool debug;
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug messages: 0=no, 1=yes (default: no)");

/* lcd, vfd, vga or none? should be auto-detected, but can be overridden... */
static int display_type;
module_param(display_type, int, S_IRUGO);
MODULE_PARM_DESC(display_type, "Type of attached display. 0=autodetect, 1=vfd, 2=lcd, 3=vga, 4=none (default: autodetect)");

static int pad_stabilize = 1;
module_param(pad_stabilize, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pad_stabilize, "Apply stabilization algorithm to iMON PAD presses in arrow key mode. 0=disable, 1=enable (default).");

/*
 * In certain use cases, mouse mode isn't really helpful, and could actually
 * cause confusion, so allow disabling it when the IR device is open.
 */
static bool nomouse;
module_param(nomouse, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(nomouse, "Disable mouse input device mode when IR device is open. 0=don't disable, 1=disable. (default: don't disable)");

/* threshold at which a pad push registers as an arrow key in kbd mode */
static int pad_thresh;
module_param(pad_thresh, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pad_thresh, "Threshold at which a pad push registers as an arrow key in kbd mode (default: 28)");


static void free_imon_context(struct imon_context *ictx)
{
	struct device *dev = ictx->dev;

	usb_free_urb(ictx->tx_urb);
	WARN_ON(ictx->dev_present_intf0);
	usb_free_urb(ictx->rx_urb_intf0);
	WARN_ON(ictx->dev_present_intf1);
	usb_free_urb(ictx->rx_urb_intf1);
	kfree_rcu(ictx, rcu);

	dev_dbg(dev, "%s: iMON context freed\n", __func__);
}

/*
 * Called when the Display device (e.g. /dev/lcd0)
 * is opened by the application.
 */
static int display_open(struct inode *inode, struct file *file)
{
	struct usb_interface *interface;
	struct imon_context *ictx = NULL;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);
	interface = usb_find_interface(&imon_driver, subminor);
	if (!interface) {
		pr_err("could not find interface for minor %d\n", subminor);
		retval = -ENODEV;
		goto exit;
	}

	rcu_read_lock();
	ictx = usb_get_intfdata(interface);
	if (!ictx || ictx->disconnected || !refcount_inc_not_zero(&ictx->users)) {
		rcu_read_unlock();
		pr_err("no context found for minor %d\n", subminor);
		retval = -ENODEV;
		goto exit;
	}
	rcu_read_unlock();

	mutex_lock(&ictx->lock);

	if (ictx->disconnected) {
		retval = -ENODEV;
	} else if (!ictx->display_supported) {
		pr_err("display not supported by device\n");
		retval = -ENODEV;
	} else if (ictx->display_isopen) {
		pr_err("display port is already open\n");
		retval = -EBUSY;
	} else {
		ictx->display_isopen = true;
		file->private_data = ictx;
		dev_dbg(ictx->dev, "display port opened\n");
	}

	mutex_unlock(&ictx->lock);

	if (retval && refcount_dec_and_test(&ictx->users))
		free_imon_context(ictx);

exit:
	return retval;
}

/*
 * Called when the display device (e.g. /dev/lcd0)
 * is closed by the application.
 */
static int display_close(struct inode *inode, struct file *file)
{
	struct imon_context *ictx = file->private_data;
	int retval = 0;

	mutex_lock(&ictx->lock);

	if (!ictx->display_supported) {
		pr_err("display not supported by device\n");
		retval = -ENODEV;
	} else if (!ictx->display_isopen) {
		pr_err("display is not open\n");
		retval = -EIO;
	} else {
		ictx->display_isopen = false;
		dev_dbg(ictx->dev, "display port closed\n");
	}

	mutex_unlock(&ictx->lock);
	if (refcount_dec_and_test(&ictx->users))
		free_imon_context(ictx);
	return retval;
}

/*
 * Sends a packet to the device -- this function must be called with
 * ictx->lock held, or its unlock/lock sequence while waiting for tx
 * to complete can/will lead to a deadlock.
 */
static int send_packet(struct imon_context *ictx)
{
	unsigned int pipe;
	unsigned long timeout;
	int interval = 0;
	int retval = 0;
	struct usb_ctrlrequest *control_req = NULL;

	lockdep_assert_held(&ictx->lock);

	if (ictx->disconnected)
		return -ENODEV;

	/* Check if we need to use control or interrupt urb */
	if (!ictx->tx_control) {
		pipe = usb_sndintpipe(ictx->usbdev_intf0,
				      ictx->tx_endpoint->bEndpointAddress);
		interval = ictx->tx_endpoint->bInterval;

		usb_fill_int_urb(ictx->tx_urb, ictx->usbdev_intf0, pipe,
				 ictx->usb_tx_buf,
				 sizeof(ictx->usb_tx_buf),
				 usb_tx_callback, ictx, interval);

		ictx->tx_urb->actual_length = 0;
	} else {
		/* fill request into kmalloc'ed space: */
		control_req = kmalloc(sizeof(*control_req), GFP_KERNEL);
		if (control_req == NULL)
			return -ENOMEM;

		/* setup packet is '21 09 0200 0001 0008' */
		control_req->bRequestType = 0x21;
		control_req->bRequest = 0x09;
		control_req->wValue = cpu_to_le16(0x0200);
		control_req->wIndex = cpu_to_le16(0x0001);
		control_req->wLength = cpu_to_le16(0x0008);

		/* control pipe is endpoint 0x00 */
		pipe = usb_sndctrlpipe(ictx->usbdev_intf0, 0);

		/* build the control urb */
		usb_fill_control_urb(ictx->tx_urb, ictx->usbdev_intf0,
				     pipe, (unsigned char *)control_req,
				     ictx->usb_tx_buf,
				     sizeof(ictx->usb_tx_buf),
				     usb_tx_callback, ictx);
		ictx->tx_urb->actual_length = 0;
	}

	reinit_completion(&ictx->tx.finished);
	ictx->tx.busy = true;
	smp_rmb(); /* ensure later readers know we're busy */

	retval = usb_submit_urb(ictx->tx_urb, GFP_KERNEL);
	if (retval) {
		ictx->tx.busy = false;
		smp_rmb(); /* ensure later readers know we're not busy */
		pr_err_ratelimited("error submitting urb(%d)\n", retval);
	} else {
		/* Wait for transmission to complete (or abort or timeout) */
		retval = wait_for_completion_interruptible_timeout(&ictx->tx.finished, 10 * HZ);
		if (retval <= 0) {
			usb_kill_urb(ictx->tx_urb);
			pr_err_ratelimited("task interrupted\n");
			if (retval < 0)
				ictx->tx.status = retval;
			else
				ictx->tx.status = -ETIMEDOUT;
		}

		ictx->tx.busy = false;
		retval = ictx->tx.status;
		if (retval)
			pr_err_ratelimited("packet tx failed (%d)\n", retval);
	}

	kfree(control_req);

	/*
	 * Induce a mandatory delay before returning, as otherwise,
	 * send_packet can get called so rapidly as to overwhelm the device,
	 * particularly on faster systems and/or those with quirky usb.
	 */
	timeout = msecs_to_jiffies(ictx->send_packet_delay);
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(timeout);

	return retval;
}

/*
 * Sends an associate packet to the iMON 2.4G.
 *
 * This might not be such a good idea, since it has an id collision with
 * some versions of the "IR & VFD" combo. The only way to determine if it
 * is an RF version is to look at the product description string. (Which
 * we currently do not fetch).
 */
static int send_associate_24g(struct imon_context *ictx)
{
	const unsigned char packet[8] = { 0x01, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x20 };

	if (!ictx) {
		pr_err("no context for device\n");
		return -ENODEV;
	}

	if (!ictx->dev_present_intf0) {
		pr_err("no iMON device present\n");
		return -ENODEV;
	}

	memcpy(ictx->usb_tx_buf, packet, sizeof(packet));

	return send_packet(ictx);
}

/*
 * Sends packets to setup and show clock on iMON display
 *
 * Arguments: year - last 2 digits of year, month - 1..12,
 * day - 1..31, dow - day of the week (0-Sun...6-Sat),
 * hour - 0..23, minute - 0..59, second - 0..59
 */
static int send_set_imon_clock(struct imon_context *ictx,
			       unsigned int year, unsigned int month,
			       unsigned int day, unsigned int dow,
			       unsigned int hour, unsigned int minute,
			       unsigned int second)
{
	unsigned char clock_enable_pkt[IMON_CLOCK_ENABLE_PACKETS][8];
	int retval = 0;
	int i;

	if (!ictx) {
		pr_err("no context for device\n");
		return -ENODEV;
	}

	switch (ictx->display_type) {
	case IMON_DISPLAY_TYPE_LCD:
		clock_enable_pkt[0][0] = 0x80;
		clock_enable_pkt[0][1] = year;
		clock_enable_pkt[0][2] = month-1;
		clock_enable_pkt[0][3] = day;
		clock_enable_pkt[0][4] = hour;
		clock_enable_pkt[0][5] = minute;
		clock_enable_pkt[0][6] = second;

		clock_enable_pkt[1][0] = 0x80;
		clock_enable_pkt[1][1] = 0;
		clock_enable_pkt[1][2] = 0;
		clock_enable_pkt[1][3] = 0;
		clock_enable_pkt[1][4] = 0;
		clock_enable_pkt[1][5] = 0;
		clock_enable_pkt[1][6] = 0;

		if (ictx->product == 0xffdc) {
			clock_enable_pkt[0][7] = 0x50;
			clock_enable_pkt[1][7] = 0x51;
		} else {
			clock_enable_pkt[0][7] = 0x88;
			clock_enable_pkt[1][7] = 0x8a;
		}

		break;

	case IMON_DISPLAY_TYPE_VFD:
		clock_enable_pkt[0][0] = year;
		clock_enable_pkt[0][1] = month-1;
		clock_enable_pkt[0][2] = day;
		clock_enable_pkt[0][3] = dow;
		clock_enable_pkt[0][4] = hour;
		clock_enable_pkt[0][5] = minute;
		clock_enable_pkt[0][6] = second;
		clock_enable_pkt[0][7] = 0x40;

		clock_enable_pkt[1][0] = 0;
		clock_enable_pkt[1][1] = 0;
		clock_enable_pkt[1][2] = 1;
		clock_enable_pkt[1][3] = 0;
		clock_enable_pkt[1][4] = 0;
		clock_enable_pkt[1][5] = 0;
		clock_enable_pkt[1][6] = 0;
		clock_enable_pkt[1][7] = 0x42;

		break;

	default:
		return -ENODEV;
	}

	for (i = 0; i < IMON_CLOCK_ENABLE_PACKETS; i++) {
		memcpy(ictx->usb_tx_buf, clock_enable_pkt[i], 8);
		retval = send_packet(ictx);
		if (retval) {
			pr_err("send_packet failed for packet %d\n", i);
			break;
		}
	}

	return retval;
}

/*
 * These are the sysfs functions to handle the association on the iMON 2.4G LT.
 */
static ssize_t associate_remote_show(struct device *d,
				     struct device_attribute *attr,
				     char *buf)
{
	struct imon_context *ictx = dev_get_drvdata(d);

	if (!ictx)
		return -ENODEV;

	mutex_lock(&ictx->lock);
	if (ictx->rf_isassociating)
		strscpy(buf, "associating\n", PAGE_SIZE);
	else
		strscpy(buf, "closed\n", PAGE_SIZE);

	dev_info(d, "Visit https://www.lirc.org/html/imon-24g.html for instructions on how to associate your iMON 2.4G DT/LT remote\n");
	mutex_unlock(&ictx->lock);
	return strlen(buf);
}

static ssize_t associate_remote_store(struct device *d,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct imon_context *ictx;

	ictx = dev_get_drvdata(d);

	if (!ictx)
		return -ENODEV;

	mutex_lock(&ictx->lock);
	ictx->rf_isassociating = true;
	send_associate_24g(ictx);
	mutex_unlock(&ictx->lock);

	return count;
}

/*
 * sysfs functions to control internal imon clock
 */
static ssize_t imon_clock_show(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct imon_context *ictx = dev_get_drvdata(d);
	size_t len;

	if (!ictx)
		return -ENODEV;

	mutex_lock(&ictx->lock);

	if (!ictx->display_supported) {
		len = sysfs_emit(buf, "Not supported.");
	} else {
		len = sysfs_emit(buf,
				 "To set the clock on your iMON display:\n"
				 "# date \"+%%y %%m %%d %%w %%H %%M %%S\" > imon_clock\n"
				 "%s", ictx->display_isopen ?
				 "\nNOTE: imon device must be closed\n" : "");
	}

	mutex_unlock(&ictx->lock);

	return len;
}

static ssize_t imon_clock_store(struct device *d,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct imon_context *ictx = dev_get_drvdata(d);
	ssize_t retval;
	unsigned int year, month, day, dow, hour, minute, second;

	if (!ictx)
		return -ENODEV;

	mutex_lock(&ictx->lock);

	if (!ictx->display_supported) {
		retval = -ENODEV;
		goto exit;
	} else if (ictx->display_isopen) {
		retval = -EBUSY;
		goto exit;
	}

	if (sscanf(buf, "%u %u %u %u %u %u %u",	&year, &month, &day, &dow,
		   &hour, &minute, &second) != 7) {
		retval = -EINVAL;
		goto exit;
	}

	if ((month < 1 || month > 12) ||
	    (day < 1 || day > 31) || (dow > 6) ||
	    (hour > 23) || (minute > 59) || (second > 59)) {
		retval = -EINVAL;
		goto exit;
	}

	retval = send_set_imon_clock(ictx, year, month, day, dow,
				     hour, minute, second);
	if (retval)
		goto exit;

	retval = count;
exit:
	mutex_unlock(&ictx->lock);

	return retval;
}


static DEVICE_ATTR_RW(imon_clock);
static DEVICE_ATTR_RW(associate_remote);

static struct attribute *imon_display_sysfs_entries[] = {
	&dev_attr_imon_clock.attr,
	NULL
};

static const struct attribute_group imon_display_attr_group = {
	.attrs = imon_display_sysfs_entries
};

static struct attribute *imon_rf_sysfs_entries[] = {
	&dev_attr_associate_remote.attr,
	NULL
};

static const struct attribute_group imon_rf_attr_group = {
	.attrs = imon_rf_sysfs_entries
};

/*
 * Writes data to the VFD.  The iMON VFD is 2x16 characters
 * and requires data in 5 consecutive USB interrupt packets,
 * each packet but the last carrying 7 bytes.
 *
 * I don't know if the VFD board supports features such as
 * scrolling, clearing rows, blanking, etc. so at
 * the caller must provide a full screen of data.  If fewer
 * than 32 bytes are provided spaces will be appended to
 * generate a full screen.
 */
static ssize_t vfd_write(struct file *file, const char __user *buf,
			 size_t n_bytes, loff_t *pos)
{
	int i;
	int offset;
	int seq;
	int retval = 0;
	struct imon_context *ictx = file->private_data;
	static const unsigned char vfd_packet6[] = {
		0x01, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF };

	if (mutex_lock_interruptible(&ictx->lock))
		return -ERESTARTSYS;

	if (ictx->disconnected) {
		retval = -ENODEV;
		goto exit;
	}

	if (!ictx->dev_present_intf0) {
		pr_err_ratelimited("no iMON device present\n");
		retval = -ENODEV;
		goto exit;
	}

	if (n_bytes <= 0 || n_bytes > 32) {
		pr_err_ratelimited("invalid payload size\n");
		retval = -EINVAL;
		goto exit;
	}

	if (copy_from_user(ictx->tx.data_buf, buf, n_bytes)) {
		retval = -EFAULT;
		goto exit;
	}

	/* Pad with spaces */
	for (i = n_bytes; i < 32; ++i)
		ictx->tx.data_buf[i] = ' ';

	for (i = 32; i < 35; ++i)
		ictx->tx.data_buf[i] = 0xFF;

	offset = 0;
	seq = 0;

	do {
		memcpy(ictx->usb_tx_buf, ictx->tx.data_buf + offset, 7);
		ictx->usb_tx_buf[7] = (unsigned char) seq;

		retval = send_packet(ictx);
		if (retval) {
			pr_err_ratelimited("send packet #%d failed\n", seq / 2);
			goto exit;
		} else {
			seq += 2;
			offset += 7;
		}

	} while (offset < 35);

	/* Send packet #6 */
	memcpy(ictx->usb_tx_buf, &vfd_packet6, sizeof(vfd_packet6));
	ictx->usb_tx_buf[7] = (unsigned char) seq;
	retval = send_packet(ictx);
	if (retval)
		pr_err_ratelimited("send packet #%d failed\n", seq / 2);

exit:
	mutex_unlock(&ictx->lock);

	return (!retval) ? n_bytes : retval;
}

/*
 * Writes data to the LCD.  The iMON OEM LCD screen expects 8-byte
 * packets. We accept data as 16 hexadecimal digits, followed by a
 * newline (to make it easy to drive the device from a command-line
 * -- even though the actual binary data is a bit complicated).
 *
 * The device itself is not a "traditional" text-mode display. It's
 * actually a 16x96 pixel bitmap display. That means if you want to
 * display text, you've got to have your own "font" and translate the
 * text into bitmaps for display. This is really flexible (you can
 * display whatever diacritics you need, and so on), but it's also
 * a lot more complicated than most LCDs...
 */
static ssize_t lcd_write(struct file *file, const char __user *buf,
			 size_t n_bytes, loff_t *pos)
{
	int retval = 0;
	struct imon_context *ictx = file->private_data;

	mutex_lock(&ictx->lock);

	if (ictx->disconnected) {
		retval = -ENODEV;
		goto exit;
	}

	if (!ictx->display_supported) {
		pr_err_ratelimited("no iMON display present\n");
		retval = -ENODEV;
		goto exit;
	}

	if (n_bytes != 8) {
		pr_err_ratelimited("invalid payload size: %d (expected 8)\n",
				   (int)n_bytes);
		retval = -EINVAL;
		goto exit;
	}

	if (copy_from_user(ictx->usb_tx_buf, buf, 8)) {
		retval = -EFAULT;
		goto exit;
	}

	retval = send_packet(ictx);
	if (retval) {
		pr_err_ratelimited("send packet failed!\n");
		goto exit;
	} else {
		dev_dbg(ictx->dev, "%s: write %d bytes to LCD\n",
			__func__, (int) n_bytes);
	}
exit:
	mutex_unlock(&ictx->lock);
	return (!retval) ? n_bytes : retval;
}

/*
 * Callback function for USB core API: transmit data
 */
static void usb_tx_callback(struct urb *urb)
{
	struct imon_context *ictx;

	if (!urb)
		return;
	ictx = (struct imon_context *)urb->context;
	if (!ictx)
		return;

	ictx->tx.status = urb->status;

	/* notify waiters that write has finished */
	ictx->tx.busy = false;
	smp_rmb(); /* ensure later readers know we're not busy */
	complete(&ictx->tx.finished);
}

/*
 * report touchscreen input
 */
static void imon_touch_display_timeout(struct timer_list *t)
{
	struct imon_context *ictx = timer_container_of(ictx, t, ttimer);

	if (ictx->display_type != IMON_DISPLAY_TYPE_VGA)
		return;

	input_report_abs(ictx->touch, ABS_X, ictx->touch_x);
	input_report_abs(ictx->touch, ABS_Y, ictx->touch_y);
	input_report_key(ictx->touch, BTN_TOUCH, 0x00);
	input_sync(ictx->touch);
}

/*
 * iMON IR receivers support two different signal sets -- those used by
 * the iMON remotes, and those used by the Windows MCE remotes (which is
 * really just RC-6), but only one or the other at a time, as the signals
 * are decoded onboard the receiver.
 *
 * This function gets called two different ways, one way is from
 * rc_register_device, for initial protocol selection/setup, and the other is
 * via a userspace-initiated protocol change request, either by direct sysfs
 * prodding or by something like ir-keytable. In the rc_register_device case,
 * the imon context lock is already held, but when initiated from userspace,
 * it is not, so we must acquire it prior to calling send_packet, which
 * requires that the lock is held.
 */
static int imon_ir_change_protocol(struct rc_dev *rc, u64 *rc_proto)
{
	int retval;
	struct imon_context *ictx = rc->priv;
	struct device *dev = ictx->dev;
	const bool unlock = mutex_trylock(&ictx->lock);
	unsigned char ir_proto_packet[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86 };

	if (*rc_proto && !(*rc_proto & rc->allowed_protocols))
		dev_warn(dev, "Looks like you're trying to use an IR protocol this device does not support\n");

	if (*rc_proto & RC_PROTO_BIT_RC6_MCE) {
		dev_dbg(dev, "Configuring IR receiver for MCE protocol\n");
		ir_proto_packet[0] = 0x01;
		*rc_proto = RC_PROTO_BIT_RC6_MCE;
	} else if (*rc_proto & RC_PROTO_BIT_IMON) {
		dev_dbg(dev, "Configuring IR receiver for iMON protocol\n");
		if (!pad_stabilize)
			dev_dbg(dev, "PAD stabilize functionality disabled\n");
		/* ir_proto_packet[0] = 0x00; // already the default */
		*rc_proto = RC_PROTO_BIT_IMON;
	} else {
		dev_warn(dev, "Unsupported IR protocol specified, overriding to iMON IR protocol\n");
		if (!pad_stabilize)
			dev_dbg(dev, "PAD stabilize functionality disabled\n");
		/* ir_proto_packet[0] = 0x00; // already the default */
		*rc_proto = RC_PROTO_BIT_IMON;
	}

	memcpy(ictx->usb_tx_buf, &ir_proto_packet, sizeof(ir_proto_packet));

	retval = send_packet(ictx);
	if (retval)
		goto out;

	ictx->rc_proto = *rc_proto;
	ictx->pad_mouse = false;

out:
	if (unlock)
		mutex_unlock(&ictx->lock);

	return retval;
}

/*
 * The directional pad behaves a bit differently, depending on whether this is
 * one of the older ffdc devices or a newer device. Newer devices appear to
 * have a higher resolution matrix for more precise mouse movement, but it
 * makes things overly sensitive in keyboard mode, so we do some interesting
 * contortions to make it less touchy. Older devices run through the same
 * routine with shorter timeout and a smaller threshold.
 */
static int stabilize(int a, int b, u16 timeout, u16 threshold)
{
	ktime_t ct;
	static ktime_t prev_time;
	static ktime_t hit_time;
	static int x, y, prev_result, hits;
	int result = 0;
	long msec, msec_hit;

	ct = ktime_get();
	msec = ktime_ms_delta(ct, prev_time);
	msec_hit = ktime_ms_delta(ct, hit_time);

	if (msec > 100) {
		x = 0;
		y = 0;
		hits = 0;
	}

	x += a;
	y += b;

	prev_time = ct;

	if (abs(x) > threshold || abs(y) > threshold) {
		if (abs(y) > abs(x))
			result = (y > 0) ? 0x7F : 0x80;
		else
			result = (x > 0) ? 0x7F00 : 0x8000;

		x = 0;
		y = 0;

		if (result == prev_result) {
			hits++;

			if (hits > 3) {
				switch (result) {
				case 0x7F:
					y = 17 * threshold / 30;
					break;
				case 0x80:
					y -= 17 * threshold / 30;
					break;
				case 0x7F00:
					x = 17 * threshold / 30;
					break;
				case 0x8000:
					x -= 17 * threshold / 30;
					break;
				}
			}

			if (hits == 2 && msec_hit < timeout) {
				result = 0;
				hits = 1;
			}
		} else {
			prev_result = result;
			hits = 1;
			hit_time = ct;
		}
	}

	return result;
}

static u32 imon_remote_key_lookup(struct imon_context *ictx, u32 scancode)
{
	u32 keycode;
	u32 release;
	bool is_release_code = false;

	/* Look for the initial press of a button */
	keycode = rc_g_keycode_from_table(ictx->rdev, scancode);
	ictx->rc_toggle = 0x0;
	ictx->rc_scancode = scancode;

	/* Look for the release of a button */
	if (keycode == KEY_RESERVED) {
		release = scancode & ~0x4000;
		keycode = rc_g_keycode_from_table(ictx->rdev, release);
		if (keycode != KEY_RESERVED)
			is_release_code = true;
	}

	ictx->release_code = is_release_code;

	return keycode;
}

static u32 imon_mce_key_lookup(struct imon_context *ictx, u32 scancode)
{
	u32 keycode;

#define MCE_KEY_MASK 0x7000
#define MCE_TOGGLE_BIT 0x8000

	/*
	 * On some receivers, mce keys decode to 0x8000f04xx and 0x8000f84xx
	 * (the toggle bit flipping between alternating key presses), while
	 * on other receivers, we see 0x8000f74xx and 0x8000ff4xx. To keep
	 * the table trim, we always or in the bits to look up 0x8000ff4xx,
	 * but we can't or them into all codes, as some keys are decoded in
	 * a different way w/o the same use of the toggle bit...
	 */
	if (scancode & 0x80000000)
		scancode = scancode | MCE_KEY_MASK | MCE_TOGGLE_BIT;

	ictx->rc_scancode = scancode;
	keycode = rc_g_keycode_from_table(ictx->rdev, scancode);

	/* not used in mce mode, but make sure we know its false */
	ictx->release_code = false;

	return keycode;
}

static u32 imon_panel_key_lookup(struct imon_context *ictx, u64 code)
{
	const struct imon_panel_key_table *key_table;
	u32 keycode = KEY_RESERVED;
	int i;

	key_table = ictx->dev_descr->key_table;

	for (i = 0; key_table[i].hw_code != 0; i++) {
		if (key_table[i].hw_code == (code | 0xffee)) {
			keycode = key_table[i].keycode;
			break;
		}
	}
	ictx->release_code = false;
	return keycode;
}

static bool imon_mouse_event(struct imon_context *ictx,
			     unsigned char *buf, int len)
{
	signed char rel_x = 0x00, rel_y = 0x00;
	u8 right_shift = 1;
	bool mouse_input = true;
	int dir = 0;
	unsigned long flags;

	spin_lock_irqsave(&ictx->kc_lock, flags);

	/* newer iMON device PAD or mouse button */
	if (ictx->product != 0xffdc && (buf[0] & 0x01) && len == 5) {
		rel_x = buf[2];
		rel_y = buf[3];
		right_shift = 1;
	/* 0xffdc iMON PAD or mouse button input */
	} else if (ictx->product == 0xffdc && (buf[0] & 0x40) &&
			!((buf[1] & 0x01) || ((buf[1] >> 2) & 0x01))) {
		rel_x = (buf[1] & 0x08) | (buf[1] & 0x10) >> 2 |
			(buf[1] & 0x20) >> 4 | (buf[1] & 0x40) >> 6;
		if (buf[0] & 0x02)
			rel_x |= ~0x0f;
		rel_x = rel_x + rel_x / 2;
		rel_y = (buf[2] & 0x08) | (buf[2] & 0x10) >> 2 |
			(buf[2] & 0x20) >> 4 | (buf[2] & 0x40) >> 6;
		if (buf[0] & 0x01)
			rel_y |= ~0x0f;
		rel_y = rel_y + rel_y / 2;
		right_shift = 2;
	/* some ffdc devices decode mouse buttons differently... */
	} else if (ictx->product == 0xffdc && (buf[0] == 0x68)) {
		right_shift = 2;
	/* ch+/- buttons, which we use for an emulated scroll wheel */
	} else if (ictx->kc == KEY_CHANNELUP && (buf[2] & 0x40) != 0x40) {
		dir = 1;
	} else if (ictx->kc == KEY_CHANNELDOWN && (buf[2] & 0x40) != 0x40) {
		dir = -1;
	} else
		mouse_input = false;

	spin_unlock_irqrestore(&ictx->kc_lock, flags);

	if (mouse_input) {
		dev_dbg(ictx->dev, "sending mouse data via input subsystem\n");

		if (dir) {
			input_report_rel(ictx->idev, REL_WHEEL, dir);
		} else if (rel_x || rel_y) {
			input_report_rel(ictx->idev, REL_X, rel_x);
			input_report_rel(ictx->idev, REL_Y, rel_y);
		} else {
			input_report_key(ictx->idev, BTN_LEFT, buf[1] & 0x1);
			input_report_key(ictx->idev, BTN_RIGHT,
					 buf[1] >> right_shift & 0x1);
		}
		input_sync(ictx->idev);
		spin_lock_irqsave(&ictx->kc_lock, flags);
		ictx->last_keycode = ictx->kc;
		spin_unlock_irqrestore(&ictx->kc_lock, flags);
	}

	return mouse_input;
}

static void imon_touch_event(struct imon_context *ictx, unsigned char *buf)
{
	mod_timer(&ictx->ttimer, jiffies + TOUCH_TIMEOUT);
	ictx->touch_x = (buf[0] << 4) | (buf[1] >> 4);
	ictx->touch_y = 0xfff - ((buf[2] << 4) | (buf[1] & 0xf));
	input_report_abs(ictx->touch, ABS_X, ictx->touch_x);
	input_report_abs(ictx->touch, ABS_Y, ictx->touch_y);
	input_report_key(ictx->touch, BTN_TOUCH, 0x01);
	input_sync(ictx->touch);
}

static void imon_pad_to_keys(struct imon_context *ictx, unsigned char *buf)
{
	int dir = 0;
	signed char rel_x = 0x00, rel_y = 0x00;
	u16 timeout, threshold;
	u32 scancode = KEY_RESERVED;
	unsigned long flags;

	/*
	 * The imon directional pad functions more like a touchpad. Bytes 3 & 4
	 * contain a position coordinate (x,y), with each component ranging
	 * from -14 to 14. We want to down-sample this to only 4 discrete values
	 * for up/down/left/right arrow keys. Also, when you get too close to
	 * diagonals, it has a tendency to jump back and forth, so lets try to
	 * ignore when they get too close.
	 */
	if (ictx->product != 0xffdc) {
		/* first, pad to 8 bytes so it conforms with everything else */
		buf[5] = buf[6] = buf[7] = 0;
		timeout = 500;	/* in msecs */
		/* (2*threshold) x (2*threshold) square */
		threshold = pad_thresh ? pad_thresh : 28;
		rel_x = buf[2];
		rel_y = buf[3];

		if (ictx->rc_proto == RC_PROTO_BIT_IMON && pad_stabilize) {
			if ((buf[1] == 0) && ((rel_x != 0) || (rel_y != 0))) {
				dir = stabilize((int)rel_x, (int)rel_y,
						timeout, threshold);
				if (!dir) {
					spin_lock_irqsave(&ictx->kc_lock,
							  flags);
					ictx->kc = KEY_UNKNOWN;
					spin_unlock_irqrestore(&ictx->kc_lock,
							       flags);
					return;
				}
				buf[2] = dir & 0xFF;
				buf[3] = (dir >> 8) & 0xFF;
				scancode = be32_to_cpu(*((__be32 *)buf));
			}
		} else {
			/*
			 * Hack alert: instead of using keycodes, we have
			 * to use hard-coded scancodes here...
			 */
			if (abs(rel_y) > abs(rel_x)) {
				buf[2] = (rel_y > 0) ? 0x7F : 0x80;
				buf[3] = 0;
				if (rel_y > 0)
					scancode = 0x01007f00; /* KEY_DOWN */
				else
					scancode = 0x01008000; /* KEY_UP */
			} else {
				buf[2] = 0;
				buf[3] = (rel_x > 0) ? 0x7F : 0x80;
				if (rel_x > 0)
					scancode = 0x0100007f; /* KEY_RIGHT */
				else
					scancode = 0x01000080; /* KEY_LEFT */
			}
		}

	/*
	 * Handle on-board decoded pad events for e.g. older VFD/iMON-Pad
	 * device (15c2:ffdc). The remote generates various codes from
	 * 0x68nnnnB7 to 0x6AnnnnB7, the left mouse button generates
	 * 0x688301b7 and the right one 0x688481b7. All other keys generate
	 * 0x2nnnnnnn. Position coordinate is encoded in buf[1] and buf[2] with
	 * reversed endianness. Extract direction from buffer, rotate endianness,
	 * adjust sign and feed the values into stabilize(). The resulting codes
	 * will be 0x01008000, 0x01007F00, which match the newer devices.
	 */
	} else {
		timeout = 10;	/* in msecs */
		/* (2*threshold) x (2*threshold) square */
		threshold = pad_thresh ? pad_thresh : 15;

		/* buf[1] is x */
		rel_x = (buf[1] & 0x08) | (buf[1] & 0x10) >> 2 |
			(buf[1] & 0x20) >> 4 | (buf[1] & 0x40) >> 6;
		if (buf[0] & 0x02)
			rel_x |= ~0x10+1;
		/* buf[2] is y */
		rel_y = (buf[2] & 0x08) | (buf[2] & 0x10) >> 2 |
			(buf[2] & 0x20) >> 4 | (buf[2] & 0x40) >> 6;
		if (buf[0] & 0x01)
			rel_y |= ~0x10+1;

		buf[0] = 0x01;
		buf[1] = buf[4] = buf[5] = buf[6] = buf[7] = 0;

		if (ictx->rc_proto == RC_PROTO_BIT_IMON && pad_stabilize) {
			dir = stabilize((int)rel_x, (int)rel_y,
					timeout, threshold);
			if (!dir) {
				spin_lock_irqsave(&ictx->kc_lock, flags);
				ictx->kc = KEY_UNKNOWN;
				spin_unlock_irqrestore(&ictx->kc_lock, flags);
				return;
			}
			buf[2] = dir & 0xFF;
			buf[3] = (dir >> 8) & 0xFF;
			scancode = be32_to_cpu(*((__be32 *)buf));
		} else {
			/*
			 * Hack alert: instead of using keycodes, we have
			 * to use hard-coded scancodes here...
			 */
			if (abs(rel_y) > abs(rel_x)) {
				buf[2] = (rel_y > 0) ? 0x7F : 0x80;
				buf[3] = 0;
				if (rel_y > 0)
					scancode = 0x01007f00; /* KEY_DOWN */
				else
					scancode = 0x01008000; /* KEY_UP */
			} else {
				buf[2] = 0;
				buf[3] = (rel_x > 0) ? 0x7F : 0x80;
				if (rel_x > 0)
					scancode = 0x0100007f; /* KEY_RIGHT */
				else
					scancode = 0x01000080; /* KEY_LEFT */
			}
		}
	}

	if (scancode) {
		spin_lock_irqsave(&ictx->kc_lock, flags);
		ictx->kc = imon_remote_key_lookup(ictx, scancode);
		spin_unlock_irqrestore(&ictx->kc_lock, flags);
	}
}

/*
 * figure out if these is a press or a release. We don't actually
 * care about repeats, as those will be auto-generated within the IR
 * subsystem for repeating scancodes.
 */
static int imon_parse_press_type(struct imon_context *ictx,
				 unsigned char *buf, u8 ktype)
{
	int press_type = 0;
	unsigned long flags;

	spin_lock_irqsave(&ictx->kc_lock, flags);

	/* key release of 0x02XXXXXX key */
	if (ictx->kc == KEY_RESERVED && buf[0] == 0x02 && buf[3] == 0x00)
		ictx->kc = ictx->last_keycode;

	/* mouse button release on (some) 0xffdc devices */
	else if (ictx->kc == KEY_RESERVED && buf[0] == 0x68 && buf[1] == 0x82 &&
		 buf[2] == 0x81 && buf[3] == 0xb7)
		ictx->kc = ictx->last_keycode;

	/* mouse button release on (some other) 0xffdc devices */
	else if (ictx->kc == KEY_RESERVED && buf[0] == 0x01 && buf[1] == 0x00 &&
		 buf[2] == 0x81 && buf[3] == 0xb7)
		ictx->kc = ictx->last_keycode;

	/* mce-specific button handling, no keyup events */
	else if (ktype == IMON_KEY_MCE) {
		ictx->rc_toggle = buf[2];
		press_type = 1;

	/* incoherent or irrelevant data */
	} else if (ictx->kc == KEY_RESERVED)
		press_type = -EINVAL;

	/* key release of 0xXXXXXXb7 key */
	else if (ictx->release_code)
		press_type = 0;

	/* this is a button press */
	else
		press_type = 1;

	spin_unlock_irqrestore(&ictx->kc_lock, flags);

	return press_type;
}

/*
 * Process the incoming packet
 */
static void imon_incoming_packet(struct imon_context *ictx,
				 struct urb *urb, int intf)
{
	int len = urb->actual_length;
	unsigned char *buf = urb->transfer_buffer;
	struct device *dev = ictx->dev;
	unsigned long flags;
	u32 kc;
	u64 scancode;
	int press_type = 0;
	ktime_t t;
	static ktime_t prev_time;
	u8 ktype;

	/* filter out junk data on the older 0xffdc imon devices */
	if ((buf[0] == 0xff) && (buf[1] == 0xff) && (buf[2] == 0xff))
		return;

	/* Figure out what key was pressed */
	if (len == 8 && buf[7] == 0xee) {
		scancode = be64_to_cpu(*((__be64 *)buf));
		ktype = IMON_KEY_PANEL;
		kc = imon_panel_key_lookup(ictx, scancode);
		ictx->release_code = false;
	} else {
		scancode = be32_to_cpu(*((__be32 *)buf));
		if (ictx->rc_proto == RC_PROTO_BIT_RC6_MCE) {
			ktype = IMON_KEY_IMON;
			if (buf[0] == 0x80)
				ktype = IMON_KEY_MCE;
			kc = imon_mce_key_lookup(ictx, scancode);
		} else {
			ktype = IMON_KEY_IMON;
			kc = imon_remote_key_lookup(ictx, scancode);
		}
	}

	spin_lock_irqsave(&ictx->kc_lock, flags);
	/* keyboard/mouse mode toggle button */
	if (kc == KEY_KEYBOARD && !ictx->release_code) {
		ictx->last_keycode = kc;
		if (!nomouse) {
			ictx->pad_mouse = !ictx->pad_mouse;
			dev_dbg(dev, "toggling to %s mode\n",
				ictx->pad_mouse ? "mouse" : "keyboard");
			spin_unlock_irqrestore(&ictx->kc_lock, flags);
			return;
		} else {
			ictx->pad_mouse = false;
			dev_dbg(dev, "mouse mode disabled, passing key value\n");
		}
	}

	ictx->kc = kc;
	spin_unlock_irqrestore(&ictx->kc_lock, flags);

	/* send touchscreen events through input subsystem if touchpad data */
	if (ictx->touch && len == 8 && buf[7] == 0x86) {
		imon_touch_event(ictx, buf);
		return;

	/* look for mouse events with pad in mouse mode */
	} else if (ictx->pad_mouse) {
		if (imon_mouse_event(ictx, buf, len))
			return;
	}

	/* Now for some special handling to convert pad input to arrow keys */
	if (((len == 5) && (buf[0] == 0x01) && (buf[4] == 0x00)) ||
	    ((len == 8) && (buf[0] & 0x40) &&
	     !(buf[1] & 0x1 || buf[1] >> 2 & 0x1))) {
		len = 8;
		imon_pad_to_keys(ictx, buf);
	}

	if (debug) {
		printk(KERN_INFO "intf%d decoded packet: %*ph\n",
		       intf, len, buf);
	}

	press_type = imon_parse_press_type(ictx, buf, ktype);
	if (press_type < 0)
		goto not_input_data;

	if (ktype != IMON_KEY_PANEL) {
		if (press_type == 0)
			rc_keyup(ictx->rdev);
		else {
			enum rc_proto proto;

			if (ictx->rc_proto == RC_PROTO_BIT_RC6_MCE)
				proto = RC_PROTO_RC6_MCE;
			else if (ictx->rc_proto == RC_PROTO_BIT_IMON)
				proto = RC_PROTO_IMON;
			else
				return;

			rc_keydown(ictx->rdev, proto, ictx->rc_scancode,
				   ictx->rc_toggle);

			spin_lock_irqsave(&ictx->kc_lock, flags);
			ictx->last_keycode = ictx->kc;
			spin_unlock_irqrestore(&ictx->kc_lock, flags);
		}
		return;
	}

	/* Only panel type events left to process now */
	spin_lock_irqsave(&ictx->kc_lock, flags);

	t = ktime_get();
	/* KEY repeats from knob and panel that need to be suppressed */
	if (ictx->kc == KEY_MUTE ||
	    ictx->dev_descr->flags & IMON_SUPPRESS_REPEATED_KEYS) {
		if (ictx->kc == ictx->last_keycode &&
		    ktime_ms_delta(t, prev_time) < ictx->idev->rep[REP_DELAY]) {
			spin_unlock_irqrestore(&ictx->kc_lock, flags);
			return;
		}
	}

	prev_time = t;
	kc = ictx->kc;

	spin_unlock_irqrestore(&ictx->kc_lock, flags);

	input_report_key(ictx->idev, kc, press_type);
	input_sync(ictx->idev);

	/* panel keys don't generate a release */
	input_report_key(ictx->idev, kc, 0);
	input_sync(ictx->idev);

	spin_lock_irqsave(&ictx->kc_lock, flags);
	ictx->last_keycode = kc;
	spin_unlock_irqrestore(&ictx->kc_lock, flags);

	return;

not_input_data:
	if (len != 8) {
		dev_warn(dev, "imon %s: invalid incoming packet size (len = %d, intf%d)\n",
			 __func__, len, intf);
		return;
	}

	/* iMON 2.4G associate frame */
	if (buf[0] == 0x00 &&
	    buf[2] == 0xFF &&				/* REFID */
	    buf[3] == 0xFF &&
	    buf[4] == 0xFF &&
	    buf[5] == 0xFF &&				/* iMON 2.4G */
	   ((buf[6] == 0x4E && buf[7] == 0xDF) ||	/* LT */
	    (buf[6] == 0x5E && buf[7] == 0xDF))) {	/* DT */
		dev_warn(dev, "%s: remote associated refid=%02X\n",
			 __func__, buf[1]);
		ictx->rf_isassociating = false;
	}
}

/*
 * Callback function for USB core API: receive data
 */
static void usb_rx_callback_intf0(struct urb *urb)
{
	struct imon_context *ictx;
	int intfnum = 0;

	if (!urb)
		return;

	ictx = (struct imon_context *)urb->context;
	if (!ictx)
		return;

	switch (urb->status) {
	case -ENOENT:		/* usbcore unlink successful! */
		return;

	case -ESHUTDOWN:	/* transport endpoint was shut down */
		break;

	case 0:
		/*
		 * if we get a callback before we're done configuring the hardware, we
		 * can't yet process the data, as there's nowhere to send it, but we
		 * still need to submit a new rx URB to avoid wedging the hardware
		 */
		if (ictx->dev_present_intf0)
			imon_incoming_packet(ictx, urb, intfnum);
		break;

	case -ECONNRESET:
	case -EILSEQ:
	case -EPROTO:
	case -EPIPE:
		dev_warn(ictx->dev, "imon %s: status(%d)\n",
			 __func__, urb->status);
		return;

	default:
		dev_warn(ictx->dev, "imon %s: status(%d): ignored\n",
			 __func__, urb->status);
		break;
	}

	usb_submit_urb(ictx->rx_urb_intf0, GFP_ATOMIC);
}

static void usb_rx_callback_intf1(struct urb *urb)
{
	struct imon_context *ictx;
	int intfnum = 1;

	if (!urb)
		return;

	ictx = (struct imon_context *)urb->context;
	if (!ictx)
		return;

	switch (urb->status) {
	case -ENOENT:		/* usbcore unlink successful! */
		return;

	case -ESHUTDOWN:	/* transport endpoint was shut down */
		break;

	case 0:
		/*
		 * if we get a callback before we're done configuring the hardware, we
		 * can't yet process the data, as there's nowhere to send it, but we
		 * still need to submit a new rx URB to avoid wedging the hardware
		 */
		if (ictx->dev_present_intf1)
			imon_incoming_packet(ictx, urb, intfnum);
		break;

	case -ECONNRESET:
	case -EILSEQ:
	case -EPROTO:
	case -EPIPE:
		dev_warn(ictx->dev, "imon %s: status(%d)\n",
			 __func__, urb->status);
		return;

	default:
		dev_warn(ictx->dev, "imon %s: status(%d): ignored\n",
			 __func__, urb->status);
		break;
	}

	usb_submit_urb(ictx->rx_urb_intf1, GFP_ATOMIC);
}

/*
 * The 0x15c2:0xffdc device ID was used for umpteen different imon
 * devices, and all of them constantly spew interrupts, even when there
 * is no actual data to report. However, byte 6 of this buffer looks like
 * its unique across device variants, so we're trying to key off that to
 * figure out which display type (if any) and what IR protocol the device
 * actually supports. These devices have their IR protocol hard-coded into
 * their firmware, they can't be changed on the fly like the newer hardware.
 */
static void imon_get_ffdc_type(struct imon_context *ictx)
{
	u8 ffdc_cfg_byte = ictx->usb_rx_buf[6];
	u8 detected_display_type = IMON_DISPLAY_TYPE_NONE;
	u64 allowed_protos = RC_PROTO_BIT_IMON;

	switch (ffdc_cfg_byte) {
	/* iMON Knob, no display, iMON IR + vol knob */
	case 0x21:
		dev_info(ictx->dev, "0xffdc iMON Knob, iMON IR");
		ictx->display_supported = false;
		break;
	/* iMON 2.4G LT (usb stick), no display, iMON RF */
	case 0x4e:
		dev_info(ictx->dev, "0xffdc iMON 2.4G LT, iMON RF");
		ictx->display_supported = false;
		ictx->rf_device = true;
		break;
	/* iMON VFD, no IR (does have vol knob tho) */
	case 0x35:
		dev_info(ictx->dev, "0xffdc iMON VFD + knob, no IR");
		detected_display_type = IMON_DISPLAY_TYPE_VFD;
		break;
	/* iMON VFD, iMON IR */
	case 0x24:
	case 0x30:
	case 0x85:
		dev_info(ictx->dev, "0xffdc iMON VFD, iMON IR");
		detected_display_type = IMON_DISPLAY_TYPE_VFD;
		break;
	/* iMON VFD, MCE IR */
	case 0x46:
	case 0x9e:
		dev_info(ictx->dev, "0xffdc iMON VFD, MCE IR");
		detected_display_type = IMON_DISPLAY_TYPE_VFD;
		allowed_protos = RC_PROTO_BIT_RC6_MCE;
		break;
	/* iMON VFD, iMON or MCE IR */
	case 0x7e:
		dev_info(ictx->dev, "0xffdc iMON VFD, iMON or MCE IR");
		detected_display_type = IMON_DISPLAY_TYPE_VFD;
		allowed_protos |= RC_PROTO_BIT_RC6_MCE;
		break;
	/* iMON LCD, MCE IR */
	case 0x9f:
		dev_info(ictx->dev, "0xffdc iMON LCD, MCE IR");
		detected_display_type = IMON_DISPLAY_TYPE_LCD;
		allowed_protos = RC_PROTO_BIT_RC6_MCE;
		break;
	/* no display, iMON IR */
	case 0x26:
		dev_info(ictx->dev, "0xffdc iMON Inside, iMON IR");
		ictx->display_supported = false;
		break;
	/* Soundgraph iMON UltraBay */
	case 0x98:
		dev_info(ictx->dev, "0xffdc iMON UltraBay, LCD + IR");
		detected_display_type = IMON_DISPLAY_TYPE_LCD;
		allowed_protos = RC_PROTO_BIT_IMON | RC_PROTO_BIT_RC6_MCE;
		ictx->dev_descr = &ultrabay_table;
		break;

	default:
		dev_info(ictx->dev, "Unknown 0xffdc device, defaulting to VFD and iMON IR");
		detected_display_type = IMON_DISPLAY_TYPE_VFD;
		/*
		 * We don't know which one it is, allow user to set the
		 * RC6 one from userspace if IMON wasn't correct.
		 */
		allowed_protos |= RC_PROTO_BIT_RC6_MCE;
		break;
	}

	printk(KERN_CONT " (id 0x%02x)\n", ffdc_cfg_byte);

	ictx->display_type = detected_display_type;
	ictx->rc_proto = allowed_protos;
}

static void imon_set_display_type(struct imon_context *ictx)
{
	u8 configured_display_type = IMON_DISPLAY_TYPE_VFD;

	/*
	 * Try to auto-detect the type of display if the user hasn't set
	 * it by hand via the display_type modparam. Default is VFD.
	 */

	if (display_type == IMON_DISPLAY_TYPE_AUTO) {
		switch (ictx->product) {
		case 0xffdc:
			/* set in imon_get_ffdc_type() */
			configured_display_type = ictx->display_type;
			break;
		case 0x0034:
		case 0x0035:
			configured_display_type = IMON_DISPLAY_TYPE_VGA;
			break;
		case 0x0038:
		case 0x0039:
		case 0x0045:
			configured_display_type = IMON_DISPLAY_TYPE_LCD;
			break;
		case 0x003c:
		case 0x0041:
		case 0x0042:
		case 0x0043:
			configured_display_type = IMON_DISPLAY_TYPE_NONE;
			ictx->display_supported = false;
			break;
		case 0x0036:
		case 0x0044:
		default:
			configured_display_type = IMON_DISPLAY_TYPE_VFD;
			break;
		}
	} else {
		configured_display_type = display_type;
		if (display_type == IMON_DISPLAY_TYPE_NONE)
			ictx->display_supported = false;
		else
			ictx->display_supported = true;
		dev_info(ictx->dev, "%s: overriding display type to %d via modparam\n",
			 __func__, display_type);
	}

	ictx->display_type = configured_display_type;
}

static struct rc_dev *imon_init_rdev(struct imon_context *ictx)
{
	struct rc_dev *rdev;
	int ret;
	static const unsigned char fp_packet[] = {
		0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88 };

	rdev = rc_allocate_device(RC_DRIVER_SCANCODE);
	if (!rdev) {
		dev_err(ictx->dev, "remote control dev allocation failed\n");
		goto out;
	}

	snprintf(ictx->name_rdev, sizeof(ictx->name_rdev),
		 "iMON Remote (%04x:%04x)", ictx->vendor, ictx->product);
	usb_make_path(ictx->usbdev_intf0, ictx->phys_rdev,
		      sizeof(ictx->phys_rdev));
	strlcat(ictx->phys_rdev, "/input0", sizeof(ictx->phys_rdev));

	rdev->device_name = ictx->name_rdev;
	rdev->input_phys = ictx->phys_rdev;
	usb_to_input_id(ictx->usbdev_intf0, &rdev->input_id);
	rdev->dev.parent = ictx->dev;

	rdev->priv = ictx;
	/* iMON PAD or MCE */
	rdev->allowed_protocols = RC_PROTO_BIT_IMON | RC_PROTO_BIT_RC6_MCE;
	rdev->change_protocol = imon_ir_change_protocol;
	rdev->driver_name = MOD_NAME;

	/* Enable front-panel buttons and/or knobs */
	memcpy(ictx->usb_tx_buf, &fp_packet, sizeof(fp_packet));
	ret = send_packet(ictx);
	/* Not fatal, but warn about it */
	if (ret)
		dev_info(ictx->dev, "panel buttons/knobs setup failed\n");

	if (ictx->product == 0xffdc) {
		imon_get_ffdc_type(ictx);
		rdev->allowed_protocols = ictx->rc_proto;
	}

	imon_set_display_type(ictx);

	if (ictx->rc_proto == RC_PROTO_BIT_RC6_MCE)
		rdev->map_name = RC_MAP_IMON_MCE;
	else
		rdev->map_name = RC_MAP_IMON_PAD;

	ret = rc_register_device(rdev);
	if (ret < 0) {
		dev_err(ictx->dev, "remote input dev register failed\n");
		goto out;
	}

	return rdev;

out:
	rc_free_device(rdev);
	return NULL;
}

static struct input_dev *imon_init_idev(struct imon_context *ictx)
{
	const struct imon_panel_key_table *key_table;
	struct input_dev *idev;
	int ret, i;

	key_table = ictx->dev_descr->key_table;

	idev = input_allocate_device();
	if (!idev)
		goto out;

	snprintf(ictx->name_idev, sizeof(ictx->name_idev),
		 "iMON Panel, Knob and Mouse(%04x:%04x)",
		 ictx->vendor, ictx->product);
	idev->name = ictx->name_idev;

	usb_make_path(ictx->usbdev_intf0, ictx->phys_idev,
		      sizeof(ictx->phys_idev));
	strlcat(ictx->phys_idev, "/input1", sizeof(ictx->phys_idev));
	idev->phys = ictx->phys_idev;

	idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP) | BIT_MASK(EV_REL);

	idev->keybit[BIT_WORD(BTN_MOUSE)] =
		BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);
	idev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y) |
		BIT_MASK(REL_WHEEL);

	/* panel and/or knob code support */
	for (i = 0; key_table[i].hw_code != 0; i++) {
		u32 kc = key_table[i].keycode;
		__set_bit(kc, idev->keybit);
	}

	usb_to_input_id(ictx->usbdev_intf0, &idev->id);
	idev->dev.parent = ictx->dev;
	input_set_drvdata(idev, ictx);

	ret = input_register_device(idev);
	if (ret < 0) {
		dev_err(ictx->dev, "input dev register failed\n");
		goto out;
	}

	return idev;

out:
	input_free_device(idev);
	return NULL;
}

static struct input_dev *imon_init_touch(struct imon_context *ictx)
{
	struct input_dev *touch;
	int ret;

	touch = input_allocate_device();
	if (!touch)
		goto touch_alloc_failed;

	snprintf(ictx->name_touch, sizeof(ictx->name_touch),
		 "iMON USB Touchscreen (%04x:%04x)",
		 ictx->vendor, ictx->product);
	touch->name = ictx->name_touch;

	usb_make_path(ictx->usbdev_intf1, ictx->phys_touch,
		      sizeof(ictx->phys_touch));
	strlcat(ictx->phys_touch, "/input2", sizeof(ictx->phys_touch));
	touch->phys = ictx->phys_touch;

	touch->evbit[0] =
		BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	touch->keybit[BIT_WORD(BTN_TOUCH)] =
		BIT_MASK(BTN_TOUCH);
	input_set_abs_params(touch, ABS_X,
			     0x00, 0xfff, 0, 0);
	input_set_abs_params(touch, ABS_Y,
			     0x00, 0xfff, 0, 0);

	input_set_drvdata(touch, ictx);

	usb_to_input_id(ictx->usbdev_intf1, &touch->id);
	touch->dev.parent = ictx->dev;
	ret = input_register_device(touch);
	if (ret <  0) {
		dev_info(ictx->dev, "touchscreen input dev register failed\n");
		goto touch_register_failed;
	}

	return touch;

touch_register_failed:
	input_free_device(touch);

touch_alloc_failed:
	return NULL;
}

static bool imon_find_endpoints(struct imon_context *ictx,
				struct usb_host_interface *iface_desc)
{
	struct usb_endpoint_descriptor *ep;
	struct usb_endpoint_descriptor *rx_endpoint = NULL;
	struct usb_endpoint_descriptor *tx_endpoint = NULL;
	int ifnum = iface_desc->desc.bInterfaceNumber;
	int num_endpts = iface_desc->desc.bNumEndpoints;
	int i, ep_dir, ep_type;
	bool ir_ep_found = false;
	bool display_ep_found = false;
	bool tx_control = false;

	/*
	 * Scan the endpoint list and set:
	 *	first input endpoint = IR endpoint
	 *	first output endpoint = display endpoint
	 */
	for (i = 0; i < num_endpts && !(ir_ep_found && display_ep_found); ++i) {
		ep = &iface_desc->endpoint[i].desc;
		ep_dir = ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK;
		ep_type = usb_endpoint_type(ep);

		if (!ir_ep_found && ep_dir == USB_DIR_IN &&
		    ep_type == USB_ENDPOINT_XFER_INT) {

			rx_endpoint = ep;
			ir_ep_found = true;
			dev_dbg(ictx->dev, "%s: found IR endpoint\n", __func__);

		} else if (!display_ep_found && ep_dir == USB_DIR_OUT &&
			   ep_type == USB_ENDPOINT_XFER_INT) {
			tx_endpoint = ep;
			display_ep_found = true;
			dev_dbg(ictx->dev, "%s: found display endpoint\n", __func__);
		}
	}

	if (ifnum == 0) {
		ictx->rx_endpoint_intf0 = rx_endpoint;
		/*
		 * tx is used to send characters to lcd/vfd, associate RF
		 * remotes, set IR protocol, and maybe more...
		 */
		ictx->tx_endpoint = tx_endpoint;
	} else {
		ictx->rx_endpoint_intf1 = rx_endpoint;
	}

	/*
	 * If we didn't find a display endpoint, this is probably one of the
	 * newer iMON devices that use control urb instead of interrupt
	 */
	if (!display_ep_found) {
		tx_control = true;
		display_ep_found = true;
		dev_dbg(ictx->dev, "%s: device uses control endpoint, not interface OUT endpoint\n",
			__func__);
	}

	/*
	 * Some iMON receivers have no display. Unfortunately, it seems
	 * that SoundGraph recycles device IDs between devices both with
	 * and without... :\
	 */
	if (ictx->display_type == IMON_DISPLAY_TYPE_NONE) {
		display_ep_found = false;
		dev_dbg(ictx->dev, "%s: device has no display\n", __func__);
	}

	/*
	 * iMON Touch devices have a VGA touchscreen, but no "display", as
	 * that refers to e.g. /dev/lcd0 (a character device LCD or VFD).
	 */
	if (ictx->display_type == IMON_DISPLAY_TYPE_VGA) {
		display_ep_found = false;
		dev_dbg(ictx->dev, "%s: iMON Touch device found\n", __func__);
	}

	/* Input endpoint is mandatory */
	if (!ir_ep_found)
		pr_err("no valid input (IR) endpoint found\n");

	ictx->tx_control = tx_control;

	if (display_ep_found)
		ictx->display_supported = true;

	return ir_ep_found;

}

static struct imon_context *imon_init_intf0(struct usb_interface *intf,
					    const struct usb_device_id *id)
{
	struct imon_context *ictx;
	struct urb *rx_urb;
	struct urb *tx_urb;
	struct device *dev = &intf->dev;
	struct usb_host_interface *iface_desc;
	int ret = -ENOMEM;

	ictx = kzalloc(sizeof(*ictx), GFP_KERNEL);
	if (!ictx)
		goto exit;

	rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!rx_urb)
		goto rx_urb_alloc_failed;
	tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!tx_urb)
		goto tx_urb_alloc_failed;

	mutex_init(&ictx->lock);
	spin_lock_init(&ictx->kc_lock);

	mutex_lock(&ictx->lock);

	ictx->dev = dev;
	ictx->usbdev_intf0 = usb_get_dev(interface_to_usbdev(intf));
	ictx->rx_urb_intf0 = rx_urb;
	ictx->tx_urb = tx_urb;
	ictx->rf_device = false;

	init_completion(&ictx->tx.finished);

	ictx->vendor  = le16_to_cpu(ictx->usbdev_intf0->descriptor.idVendor);
	ictx->product = le16_to_cpu(ictx->usbdev_intf0->descriptor.idProduct);

	/* save drive info for later accessing the panel/knob key table */
	ictx->dev_descr = (struct imon_usb_dev_descr *)id->driver_info;
	/* default send_packet delay is 5ms but some devices need more */
	ictx->send_packet_delay = ictx->dev_descr->flags &
				  IMON_NEED_20MS_PKT_DELAY ? 20 : 5;

	ret = -ENODEV;
	iface_desc = intf->cur_altsetting;
	if (!imon_find_endpoints(ictx, iface_desc)) {
		goto find_endpoint_failed;
	}

	usb_fill_int_urb(ictx->rx_urb_intf0, ictx->usbdev_intf0,
		usb_rcvintpipe(ictx->usbdev_intf0,
			ictx->rx_endpoint_intf0->bEndpointAddress),
		ictx->usb_rx_buf, sizeof(ictx->usb_rx_buf),
		usb_rx_callback_intf0, ictx,
		ictx->rx_endpoint_intf0->bInterval);

	ret = usb_submit_urb(ictx->rx_urb_intf0, GFP_KERNEL);
	if (ret) {
		pr_err("usb_submit_urb failed for intf0 (%d)\n", ret);
		goto urb_submit_failed;
	}

	ictx->idev = imon_init_idev(ictx);
	if (!ictx->idev) {
		dev_err(dev, "%s: input device setup failed\n", __func__);
		goto idev_setup_failed;
	}

	ictx->rdev = imon_init_rdev(ictx);
	if (!ictx->rdev) {
		dev_err(dev, "%s: rc device setup failed\n", __func__);
		goto rdev_setup_failed;
	}

	ictx->dev_present_intf0 = true;

	mutex_unlock(&ictx->lock);
	return ictx;

rdev_setup_failed:
	input_unregister_device(ictx->idev);
idev_setup_failed:
	usb_kill_urb(ictx->rx_urb_intf0);
urb_submit_failed:
find_endpoint_failed:
	usb_put_dev(ictx->usbdev_intf0);
	mutex_unlock(&ictx->lock);
	usb_free_urb(tx_urb);
tx_urb_alloc_failed:
	usb_free_urb(rx_urb);
rx_urb_alloc_failed:
	kfree(ictx);
exit:
	dev_err(dev, "unable to initialize intf0, err %d\n", ret);

	return NULL;
}

static struct imon_context *imon_init_intf1(struct usb_interface *intf,
					    struct imon_context *ictx)
{
	struct urb *rx_urb;
	struct usb_host_interface *iface_desc;
	int ret = -ENOMEM;

	rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!rx_urb)
		goto rx_urb_alloc_failed;

	mutex_lock(&ictx->lock);

	if (ictx->display_type == IMON_DISPLAY_TYPE_VGA) {
		timer_setup(&ictx->ttimer, imon_touch_display_timeout, 0);
	}

	ictx->usbdev_intf1 = usb_get_dev(interface_to_usbdev(intf));
	ictx->rx_urb_intf1 = rx_urb;

	ret = -ENODEV;
	iface_desc = intf->cur_altsetting;
	if (!imon_find_endpoints(ictx, iface_desc))
		goto find_endpoint_failed;

	if (ictx->display_type == IMON_DISPLAY_TYPE_VGA) {
		ictx->touch = imon_init_touch(ictx);
		if (!ictx->touch)
			goto touch_setup_failed;
	} else
		ictx->touch = NULL;

	usb_fill_int_urb(ictx->rx_urb_intf1, ictx->usbdev_intf1,
		usb_rcvintpipe(ictx->usbdev_intf1,
			ictx->rx_endpoint_intf1->bEndpointAddress),
		ictx->usb_rx_buf, sizeof(ictx->usb_rx_buf),
		usb_rx_callback_intf1, ictx,
		ictx->rx_endpoint_intf1->bInterval);

	ret = usb_submit_urb(ictx->rx_urb_intf1, GFP_KERNEL);

	if (ret) {
		pr_err("usb_submit_urb failed for intf1 (%d)\n", ret);
		goto urb_submit_failed;
	}

	ictx->dev_present_intf1 = true;

	mutex_unlock(&ictx->lock);
	return ictx;

urb_submit_failed:
	if (ictx->touch)
		input_unregister_device(ictx->touch);
touch_setup_failed:
find_endpoint_failed:
	usb_put_dev(ictx->usbdev_intf1);
	ictx->usbdev_intf1 = NULL;
	mutex_unlock(&ictx->lock);
	usb_free_urb(rx_urb);
	ictx->rx_urb_intf1 = NULL;
rx_urb_alloc_failed:
	dev_err(ictx->dev, "unable to initialize intf1, err %d\n", ret);

	return NULL;
}

static void imon_init_display(struct imon_context *ictx,
			      struct usb_interface *intf)
{
	int ret;

	dev_dbg(ictx->dev, "Registering iMON display with sysfs\n");

	/* set up sysfs entry for built-in clock */
	ret = sysfs_create_group(&intf->dev.kobj, &imon_display_attr_group);
	if (ret)
		dev_err(ictx->dev, "Could not create display sysfs entries(%d)",
			ret);

	if (ictx->display_type == IMON_DISPLAY_TYPE_LCD)
		ret = usb_register_dev(intf, &imon_lcd_class);
	else
		ret = usb_register_dev(intf, &imon_vfd_class);
	if (ret)
		/* Not a fatal error, so ignore */
		dev_info(ictx->dev, "could not get a minor number for display\n");

}

/*
 * Callback function for USB core API: Probe
 */
static int imon_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct usb_device *usbdev = NULL;
	struct usb_host_interface *iface_desc = NULL;
	struct usb_interface *first_if;
	struct device *dev = &interface->dev;
	int ifnum, sysfs_err;
	int ret = 0;
	struct imon_context *ictx = NULL;
	u16 vendor, product;

	usbdev     = usb_get_dev(interface_to_usbdev(interface));
	iface_desc = interface->cur_altsetting;
	ifnum      = iface_desc->desc.bInterfaceNumber;
	vendor     = le16_to_cpu(usbdev->descriptor.idVendor);
	product    = le16_to_cpu(usbdev->descriptor.idProduct);

	dev_dbg(dev, "%s: found iMON device (%04x:%04x, intf%d)\n",
		__func__, vendor, product, ifnum);

	first_if = usb_ifnum_to_if(usbdev, 0);
	if (!first_if) {
		ret = -ENODEV;
		goto fail;
	}

	if (first_if->dev.driver != interface->dev.driver) {
		dev_err(&interface->dev, "inconsistent driver matching\n");
		ret = -EINVAL;
		goto fail;
	}

	if (ifnum == 0) {
		ictx = imon_init_intf0(interface, id);
		if (!ictx) {
			pr_err("failed to initialize context!\n");
			ret = -ENODEV;
			goto fail;
		}
		refcount_set(&ictx->users, 1);

	} else {
		/* this is the secondary interface on the device */
		struct imon_context *first_if_ctx = usb_get_intfdata(first_if);

		/* fail early if first intf failed to register */
		if (!first_if_ctx) {
			ret = -ENODEV;
			goto fail;
		}

		ictx = imon_init_intf1(interface, first_if_ctx);
		if (!ictx) {
			pr_err("failed to attach to context!\n");
			ret = -ENODEV;
			goto fail;
		}
		refcount_inc(&ictx->users);

	}

	usb_set_intfdata(interface, ictx);

	if (ifnum == 0) {
		if (product == 0xffdc && ictx->rf_device) {
			sysfs_err = sysfs_create_group(&interface->dev.kobj,
						       &imon_rf_attr_group);
			if (sysfs_err)
				pr_err("Could not create RF sysfs entries(%d)\n",
				       sysfs_err);
		}

		if (ictx->display_supported)
			imon_init_display(ictx, interface);
	}

	dev_info(dev, "iMON device (%04x:%04x, intf%d) on usb<%d:%d> initialized\n",
		 vendor, product, ifnum,
		 usbdev->bus->busnum, usbdev->devnum);

	usb_put_dev(usbdev);

	return 0;

fail:
	usb_put_dev(usbdev);
	dev_err(dev, "unable to register, err %d\n", ret);

	return ret;
}

/*
 * Callback function for USB core API: disconnect
 */
static void imon_disconnect(struct usb_interface *interface)
{
	struct imon_context *ictx;
	struct device *dev;
	int ifnum;

	ictx = usb_get_intfdata(interface);

	mutex_lock(&ictx->lock);
	ictx->disconnected = true;
	mutex_unlock(&ictx->lock);

	dev = ictx->dev;
	ifnum = interface->cur_altsetting->desc.bInterfaceNumber;

	/*
	 * sysfs_remove_group is safe to call even if sysfs_create_group
	 * hasn't been called
	 */
	sysfs_remove_group(&interface->dev.kobj, &imon_display_attr_group);
	sysfs_remove_group(&interface->dev.kobj, &imon_rf_attr_group);

	usb_set_intfdata(interface, NULL);

	/* Abort ongoing write */
	if (ictx->tx.busy) {
		usb_kill_urb(ictx->tx_urb);
		complete(&ictx->tx.finished);
	}

	if (ifnum == 0) {
		ictx->dev_present_intf0 = false;
		usb_kill_urb(ictx->rx_urb_intf0);
		input_unregister_device(ictx->idev);
		rc_unregister_device(ictx->rdev);
		if (ictx->display_supported) {
			if (ictx->display_type == IMON_DISPLAY_TYPE_LCD)
				usb_deregister_dev(interface, &imon_lcd_class);
			else if (ictx->display_type == IMON_DISPLAY_TYPE_VFD)
				usb_deregister_dev(interface, &imon_vfd_class);
		}
		usb_put_dev(ictx->usbdev_intf0);
	} else {
		ictx->dev_present_intf1 = false;
		usb_kill_urb(ictx->rx_urb_intf1);
		if (ictx->display_type == IMON_DISPLAY_TYPE_VGA) {
			timer_delete_sync(&ictx->ttimer);
			input_unregister_device(ictx->touch);
		}
		usb_put_dev(ictx->usbdev_intf1);
	}

	if (refcount_dec_and_test(&ictx->users))
		free_imon_context(ictx);

	dev_dbg(dev, "%s: iMON device (intf%d) disconnected\n",
		__func__, ifnum);
}

static int imon_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct imon_context *ictx = usb_get_intfdata(intf);
	int ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

	if (ifnum == 0)
		usb_kill_urb(ictx->rx_urb_intf0);
	else
		usb_kill_urb(ictx->rx_urb_intf1);

	return 0;
}

static int imon_resume(struct usb_interface *intf)
{
	int rc = 0;
	struct imon_context *ictx = usb_get_intfdata(intf);
	int ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

	if (ifnum == 0) {
		usb_fill_int_urb(ictx->rx_urb_intf0, ictx->usbdev_intf0,
			usb_rcvintpipe(ictx->usbdev_intf0,
				ictx->rx_endpoint_intf0->bEndpointAddress),
			ictx->usb_rx_buf, sizeof(ictx->usb_rx_buf),
			usb_rx_callback_intf0, ictx,
			ictx->rx_endpoint_intf0->bInterval);

		rc = usb_submit_urb(ictx->rx_urb_intf0, GFP_NOIO);

	} else {
		usb_fill_int_urb(ictx->rx_urb_intf1, ictx->usbdev_intf1,
			usb_rcvintpipe(ictx->usbdev_intf1,
				ictx->rx_endpoint_intf1->bEndpointAddress),
			ictx->usb_rx_buf, sizeof(ictx->usb_rx_buf),
			usb_rx_callback_intf1, ictx,
			ictx->rx_endpoint_intf1->bInterval);

		rc = usb_submit_urb(ictx->rx_urb_intf1, GFP_NOIO);
	}

	return rc;
}

module_usb_driver(imon_driver);
