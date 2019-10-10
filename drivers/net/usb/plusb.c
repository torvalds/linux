// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PL-2301/2302 USB host-to-host link cables
 * Copyright (C) 2000-2005 by David Brownell
 */

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/usbnet.h>


/*
 * Prolific PL-2301/PL-2302 driver ... http://www.prolific.com.tw/ 
 *
 * The protocol and handshaking used here should be bug-compatible
 * with the Linux 2.2 "plusb" driver, by Deti Fliegl.
 *
 * HEADS UP:  this handshaking isn't all that robust.  This driver
 * gets confused easily if you unplug one end of the cable then
 * try to connect it again; you'll need to restart both ends. The
 * "naplink" software (used by some PlayStation/2 developers) does
 * the handshaking much better!   Also, sometimes this hardware
 * seems to get wedged under load.  Prolific docs are weak, and
 * don't identify differences between PL2301 and PL2302, much less
 * anything to explain the different PL2302 versions observed.
 *
 * NOTE:  pl2501 has several modes, including pl2301 and pl2302
 * compatibility.   Some docs suggest the difference between 2301
 * and 2302 is only to make MS-Windows use a different driver...
 *
 * pl25a1 glue based on patch from Tony Gibbs.  Prolific "docs" on
 * this chip are as usual incomplete about what control messages
 * are supported.
 */

/*
 * Bits 0-4 can be used for software handshaking; they're set from
 * one end, cleared from the other, "read" with the interrupt byte.
 */
#define	PL_S_EN		(1<<7)		/* (feature only) suspend enable */
/* reserved bit -- rx ready (6) ? */
#define	PL_TX_READY	(1<<5)		/* (interrupt only) transmit ready */
#define	PL_RESET_OUT	(1<<4)		/* reset output pipe */
#define	PL_RESET_IN	(1<<3)		/* reset input pipe */
#define	PL_TX_C		(1<<2)		/* transmission complete */
#define	PL_TX_REQ	(1<<1)		/* transmission received */
#define	PL_PEER_E	(1<<0)		/* peer exists */

static inline int
pl_vendor_req(struct usbnet *dev, u8 req, u8 val, u8 index)
{
	return usbnet_read_cmd(dev, req,
				USB_DIR_IN | USB_TYPE_VENDOR |
				USB_RECIP_DEVICE,
				val, index, NULL, 0);
}

static inline int
pl_clear_QuickLink_features(struct usbnet *dev, int val)
{
	return pl_vendor_req(dev, 1, (u8) val, 0);
}

static inline int
pl_set_QuickLink_features(struct usbnet *dev, int val)
{
	return pl_vendor_req(dev, 3, (u8) val, 0);
}

static int pl_reset(struct usbnet *dev)
{
	int status;

	/* some units seem to need this reset, others reject it utterly.
	 * FIXME be more like "naplink" or windows drivers.
	 */
	status = pl_set_QuickLink_features(dev,
		PL_S_EN|PL_RESET_OUT|PL_RESET_IN|PL_PEER_E);
	if (status != 0 && netif_msg_probe(dev))
		netif_dbg(dev, link, dev->net, "pl_reset --> %d\n", status);
	return 0;
}

static const struct driver_info	prolific_info = {
	.description =	"Prolific PL-2301/PL-2302/PL-25A1/PL-27A1",
	.flags =	FLAG_POINTTOPOINT | FLAG_NO_SETINT,
		/* some PL-2302 versions seem to fail usb_set_interface() */
	.reset =	pl_reset,
};


/*-------------------------------------------------------------------------*/

/*
 * Proilific's name won't normally be on the cables, and
 * may not be on the device.
 */

static const struct usb_device_id	products [] = {

/* full speed cables */
{
	USB_DEVICE(0x067b, 0x0000),	// PL-2301
	.driver_info =	(unsigned long) &prolific_info,
}, {
	USB_DEVICE(0x067b, 0x0001),	// PL-2302
	.driver_info =	(unsigned long) &prolific_info,
},

/* high speed cables */
{
	USB_DEVICE(0x067b, 0x25a1),     /* PL-25A1, no eeprom */
	.driver_info =  (unsigned long) &prolific_info,
}, {
	USB_DEVICE(0x050d, 0x258a),     /* Belkin F5U258/F5U279 (PL-25A1) */
	.driver_info =  (unsigned long) &prolific_info,
}, {
	USB_DEVICE(0x3923, 0x7825),     /* National Instruments USB
					 * Host-to-Host Cable
					 */
	.driver_info =  (unsigned long) &prolific_info,

},

/* super speed cables */
{
	USB_DEVICE(0x067b, 0x27a1),     /* PL-27A1, no eeprom
					 * also: goobay Active USB 3.0
					 * Data Link,
					 * Unitek Y-3501
					 */
	.driver_info =  (unsigned long) &prolific_info,
},

	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver plusb_driver = {
	.name =		"plusb",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(plusb_driver);

MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("Prolific PL-2301/2302/25A1/27A1 USB Host to Host Link Driver");
MODULE_LICENSE("GPL");
