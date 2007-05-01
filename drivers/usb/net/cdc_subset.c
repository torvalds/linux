/*
 * Simple "CDC Subset" USB Networking Links
 * Copyright (C) 2000-2005 by David Brownell
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

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/usb.h>

#include "usbnet.h"


/*
 * This supports simple USB network links that don't require any special
 * framing or hardware control operations.  The protocol used here is a
 * strict subset of CDC Ethernet, with three basic differences reflecting
 * the goal that almost any hardware should run it:
 *
 *  - Minimal runtime control:  one interface, no altsettings, and
 *    no vendor or class specific control requests.  If a device is
 *    configured, it is allowed to exchange packets with the host.
 *    Fancier models would mean not working on some hardware.
 *
 *  - Minimal manufacturing control:  no IEEE "Organizationally
 *    Unique ID" required, or an EEPROMs to store one.  Each host uses
 *    one random "locally assigned" Ethernet address instead, which can
 *    of course be overridden using standard tools like "ifconfig".
 *    (With 2^46 such addresses, same-net collisions are quite rare.)
 *
 *  - There is no additional framing data for USB.  Packets are written
 *    exactly as in CDC Ethernet, starting with an Ethernet header and
 *    terminated by a short packet.  However, the host will never send a
 *    zero length packet; some systems can't handle those robustly.
 *
 * Anything that can transmit and receive USB bulk packets can implement
 * this protocol.  That includes both smart peripherals and quite a lot
 * of "host-to-host" USB cables (which embed two devices back-to-back).
 *
 * Note that although Linux may use many of those host-to-host links
 * with this "cdc_subset" framing, that doesn't mean there may not be a
 * better approach.  Handling the "other end unplugs/replugs" scenario
 * well tends to require chip-specific vendor requests.  Also, Windows
 * peers at the other end of host-to-host cables may expect their own
 * framing to be used rather than this "cdc_subset" model.
 */

#if defined(CONFIG_USB_EPSON2888) || defined(CONFIG_USB_ARMLINUX)
/* PDA style devices are always connected if present */
static int always_connected (struct usbnet *dev)
{
	return 0;
}
#endif

#ifdef	CONFIG_USB_ALI_M5632
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * ALi M5632 driver ... does high speed
 *
 * NOTE that the MS-Windows drivers for this chip use some funky and
 * (naturally) undocumented 7-byte prefix to each packet, so this is a
 * case where we don't currently interoperate.  Also, once you unplug
 * one end of the cable, you need to replug the other end too ... since
 * chip docs are unavailable, there's no way to reset the relevant state
 * short of a power cycle.
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	ali_m5632_info = {
	.description =	"ALi M5632",
};

#endif


#ifdef	CONFIG_USB_AN2720
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * AnchorChips 2720 driver ... http://www.cypress.com
 *
 * This doesn't seem to have a way to detect whether the peer is
 * connected, or need any reset handshaking.  It's got pretty big
 * internal buffers (handles most of a frame's worth of data).
 * Chip data sheets don't describe any vendor control messages.
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	an2720_info = {
	.description =	"AnchorChips/Cypress 2720",
	// no reset available!
	// no check_connect available!

	.in = 2, .out = 2,		// direction distinguishes these
};

#endif	/* CONFIG_USB_AN2720 */


#ifdef	CONFIG_USB_BELKIN
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * Belkin F5U104 ... two NetChip 2280 devices + Atmel AVR microcontroller
 *
 * ... also two eTEK designs, including one sold as "Advance USBNET"
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	belkin_info = {
	.description =	"Belkin, eTEK, or compatible",
};

#endif	/* CONFIG_USB_BELKIN */



#ifdef	CONFIG_USB_EPSON2888
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * EPSON USB clients
 *
 * This is the same idea as Linux PDAs (below) except the firmware in the
 * device might not be Tux-powered.  Epson provides reference firmware that
 * implements this interface.  Product developers can reuse or modify that
 * code, such as by using their own product and vendor codes.
 *
 * Support was from Juro Bystricky <bystricky.juro@erd.epson.com>
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	epson2888_info = {
	.description =	"Epson USB Device",
	.check_connect = always_connected,

	.in = 4, .out = 3,
};

#endif	/* CONFIG_USB_EPSON2888 */


/*-------------------------------------------------------------------------
 *
 * info from Jonathan McDowell <noodles@earth.li>
 *
 *-------------------------------------------------------------------------*/
#ifdef CONFIG_USB_KC2190
#define HAVE_HARDWARE
static const struct driver_info kc2190_info = {
	.description =  "KC Technology KC-190",
};
#endif /* CONFIG_USB_KC2190 */


#ifdef	CONFIG_USB_ARMLINUX
#define	HAVE_HARDWARE

/*-------------------------------------------------------------------------
 *
 * Intel's SA-1100 chip integrates basic USB support, and is used
 * in PDAs like some iPaqs, the Yopy, some Zaurus models, and more.
 * When they run Linux, arch/arm/mach-sa1100/usb-eth.c may be used to
 * network using minimal USB framing data.
 *
 * This describes the driver currently in standard ARM Linux kernels.
 * The Zaurus uses a different driver (see later).
 *
 * PXA25x and PXA210 use XScale cores (ARM v5TE) with better USB support
 * and different USB endpoint numbering than the SA1100 devices.  The
 * mach-pxa/usb-eth.c driver re-uses the device ids from mach-sa1100
 * so we rely on the endpoint descriptors.
 *
 *-------------------------------------------------------------------------*/

static const struct driver_info	linuxdev_info = {
	.description =	"Linux Device",
	.check_connect = always_connected,
};

static const struct driver_info	yopy_info = {
	.description =	"Yopy",
	.check_connect = always_connected,
};

static const struct driver_info	blob_info = {
	.description =	"Boot Loader OBject",
	.check_connect = always_connected,
};

#endif	/* CONFIG_USB_ARMLINUX */


/*-------------------------------------------------------------------------*/

#ifndef	HAVE_HARDWARE
#error You need to configure some hardware for this driver
#endif

/*
 * chip vendor names won't normally be on the cables, and
 * may not be on the device.
 */

static const struct usb_device_id	products [] = {

#ifdef	CONFIG_USB_ALI_M5632
{
	USB_DEVICE (0x0402, 0x5632),	// ALi defaults
	.driver_info =	(unsigned long) &ali_m5632_info,
},
{
	USB_DEVICE (0x182d,0x207c),	// SiteCom CN-124
	.driver_info =	(unsigned long) &ali_m5632_info,
},
#endif

#ifdef	CONFIG_USB_AN2720
{
	USB_DEVICE (0x0547, 0x2720),	// AnchorChips defaults
	.driver_info =	(unsigned long) &an2720_info,
}, {
	USB_DEVICE (0x0547, 0x2727),	// Xircom PGUNET
	.driver_info =	(unsigned long) &an2720_info,
},
#endif

#ifdef	CONFIG_USB_BELKIN
{
	USB_DEVICE (0x050d, 0x0004),	// Belkin
	.driver_info =	(unsigned long) &belkin_info,
}, {
	USB_DEVICE (0x056c, 0x8100),	// eTEK
	.driver_info =	(unsigned long) &belkin_info,
}, {
	USB_DEVICE (0x0525, 0x9901),	// Advance USBNET (eTEK)
	.driver_info =	(unsigned long) &belkin_info,
},
#endif

#ifdef	CONFIG_USB_EPSON2888
{
	USB_DEVICE (0x0525, 0x2888),	// EPSON USB client
	.driver_info	= (unsigned long) &epson2888_info,
},
#endif

#ifdef CONFIG_USB_KC2190
{
	USB_DEVICE (0x050f, 0x0190),	// KC-190
	.driver_info =	(unsigned long) &kc2190_info,
},
#endif

#ifdef	CONFIG_USB_ARMLINUX
/*
 * SA-1100 using standard ARM Linux kernels, or compatible.
 * Often used when talking to Linux PDAs (iPaq, Yopy, etc).
 * The sa-1100 "usb-eth" driver handles the basic framing.
 *
 * PXA25x or PXA210 ...  these use a "usb-eth" driver much like
 * the sa1100 one, but hardware uses different endpoint numbers.
 *
 * Or the Linux "Ethernet" gadget on hardware that can't talk
 * CDC Ethernet (e.g., no altsettings), in either of two modes:
 *  - acting just like the old "usb-eth" firmware, though
 *    the implementation is different
 *  - supporting RNDIS as the first/default configuration for
 *    MS-Windows interop; Linux needs to use the other config
 */
{
	// 1183 = 0x049F, both used as hex values?
	// Compaq "Itsy" vendor/product id
	USB_DEVICE (0x049F, 0x505A),	// usb-eth, or compatible
	.driver_info =	(unsigned long) &linuxdev_info,
}, {
	USB_DEVICE (0x0E7E, 0x1001),	// G.Mate "Yopy"
	.driver_info =	(unsigned long) &yopy_info,
}, {
	USB_DEVICE (0x8086, 0x07d3),	// "blob" bootloader
	.driver_info =	(unsigned long) &blob_info,
}, {
	// Linux Ethernet/RNDIS gadget on pxa210/25x/26x, second config
	// e.g. Gumstix, current OpenZaurus, ...
	USB_DEVICE_VER (0x0525, 0xa4a2, 0x0203, 0x0203),
	.driver_info =	(unsigned long) &linuxdev_info,
},
#endif

	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

/*-------------------------------------------------------------------------*/

static struct usb_driver cdc_subset_driver = {
	.name =		"cdc_subset",
	.probe =	usbnet_probe,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
	.disconnect =	usbnet_disconnect,
	.id_table =	products,
};

static int __init cdc_subset_init(void)
{
	return usb_register(&cdc_subset_driver);
}
module_init(cdc_subset_init);

static void __exit cdc_subset_exit(void)
{
	usb_deregister(&cdc_subset_driver);
}
module_exit(cdc_subset_exit);

MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("Simple 'CDC Subset' USB networking links");
MODULE_LICENSE("GPL");
