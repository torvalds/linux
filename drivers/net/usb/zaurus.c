/*
 * Copyright (C) 2002 Pavel Machek <pavel@ucw.cz>
 * Copyright (C) 2002-2005 by David Brownell
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

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>


/*
 * All known Zaurii lie about their standards conformance.  At least
 * the earliest SA-1100 models lie by saying they support CDC Ethernet.
 * Some later models (especially PXA-25x and PXA-27x based ones) lie
 * and say they support CDC MDLM (for access to cell phone modems).
 *
 * There are non-Zaurus products that use these same protocols too.
 *
 * The annoying thing is that at the same time Sharp was developing
 * that annoying standards-breaking software, the Linux community had
 * a simple "CDC Subset" working reliably on the same SA-1100 hardware.
 * That is, the same functionality but not violating standards.
 *
 * The CDC Ethernet nonconformance points are troublesome to hosts
 * with a true CDC Ethernet implementation:
 *   - Framing appends a CRC, which the spec says drivers "must not" do;
 *   - Transfers data in altsetting zero, instead of altsetting 1;
 *   - All these peripherals use the same ethernet address.
 *
 * The CDC MDLM nonconformance is less immediately troublesome, since all
 * MDLM implementations are quasi-proprietary anyway.
 */

static struct sk_buff *
zaurus_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	int			padlen;
	struct sk_buff		*skb2;

	padlen = 2;
	if (!skb_cloned(skb)) {
		int	tailroom = skb_tailroom(skb);
		if ((padlen + 4) <= tailroom)
			goto done;
	}
	skb2 = skb_copy_expand(skb, 0, 4 + padlen, flags);
	dev_kfree_skb_any(skb);
	skb = skb2;
	if (skb) {
		u32		fcs;
done:
		fcs = crc32_le(~0, skb->data, skb->len);
		fcs = ~fcs;

		*skb_put (skb, 1) = fcs       & 0xff;
		*skb_put (skb, 1) = (fcs>> 8) & 0xff;
		*skb_put (skb, 1) = (fcs>>16) & 0xff;
		*skb_put (skb, 1) = (fcs>>24) & 0xff;
	}
	return skb;
}

static int zaurus_bind(struct usbnet *dev, struct usb_interface *intf)
{
	/* Belcarra's funky framing has other options; mostly
	 * TRAILERS (!) with 4 bytes CRC, and maybe 2 pad bytes.
	 */
	dev->net->hard_header_len += 6;
	dev->rx_urb_size = dev->net->hard_header_len + dev->net->mtu;
	return usbnet_generic_cdc_bind(dev, intf);
}

/* PDA style devices are always connected if present */
static int always_connected (struct usbnet *dev)
{
	return 0;
}

static const struct driver_info	zaurus_sl5x00_info = {
	.description =	"Sharp Zaurus SL-5x00",
	.flags =	FLAG_POINTTOPOINT | FLAG_FRAMING_Z,
	.check_connect = always_connected,
	.bind =		zaurus_bind,
	.unbind =	usbnet_cdc_unbind,
	.tx_fixup =	zaurus_tx_fixup,
};
#define	ZAURUS_STRONGARM_INFO	((unsigned long)&zaurus_sl5x00_info)

static const struct driver_info	zaurus_pxa_info = {
	.description =	"Sharp Zaurus, PXA-2xx based",
	.flags =	FLAG_POINTTOPOINT | FLAG_FRAMING_Z,
	.check_connect = always_connected,
	.bind =		zaurus_bind,
	.unbind =	usbnet_cdc_unbind,
	.tx_fixup =	zaurus_tx_fixup,
};
#define	ZAURUS_PXA_INFO		((unsigned long)&zaurus_pxa_info)

static const struct driver_info	olympus_mxl_info = {
	.description =	"Olympus R1000",
	.flags =	FLAG_POINTTOPOINT | FLAG_FRAMING_Z,
	.check_connect = always_connected,
	.bind =		zaurus_bind,
	.unbind =	usbnet_cdc_unbind,
	.tx_fixup =	zaurus_tx_fixup,
};
#define	OLYMPUS_MXL_INFO	((unsigned long)&olympus_mxl_info)


/* Some more recent products using Lineo/Belcarra code will wrongly claim
 * CDC MDLM conformance.  They aren't conformant:  data endpoints live
 * in the control interface, there's no data interface, and it's not used
 * to talk to a cell phone radio.  But at least we can detect these two
 * pseudo-classes, rather than growing this product list with entries for
 * each new nonconformant product (sigh).
 */
static const u8 safe_guid[16] = {
	0x5d, 0x34, 0xcf, 0x66, 0x11, 0x18, 0x11, 0xd6,
	0xa2, 0x1a, 0x00, 0x01, 0x02, 0xca, 0x9a, 0x7f,
};
static const u8 blan_guid[16] = {
	0x74, 0xf0, 0x3d, 0xbd, 0x1e, 0xc1, 0x44, 0x70,
	0xa3, 0x67, 0x71, 0x34, 0xc9, 0xf5, 0x54, 0x37,
};

static int blan_mdlm_bind(struct usbnet *dev, struct usb_interface *intf)
{
	u8				*buf = intf->cur_altsetting->extra;
	int				len = intf->cur_altsetting->extralen;
	struct usb_cdc_mdlm_desc	*desc = NULL;
	struct usb_cdc_mdlm_detail_desc	*detail = NULL;

	while (len > 3) {
		if (buf [1] != USB_DT_CS_INTERFACE)
			goto next_desc;

		/* use bDescriptorSubType, and just verify that we get a
		 * "BLAN" (or "SAFE") descriptor.
		 */
		switch (buf [2]) {
		case USB_CDC_MDLM_TYPE:
			if (desc) {
				dev_dbg(&intf->dev, "extra MDLM\n");
				goto bad_desc;
			}
			desc = (void *) buf;
			if (desc->bLength != sizeof *desc) {
				dev_dbg(&intf->dev, "MDLM len %u\n",
					desc->bLength);
				goto bad_desc;
			}
			/* expect bcdVersion 1.0, ignore */
			if (memcmp(&desc->bGUID, blan_guid, 16) &&
			    memcmp(&desc->bGUID, safe_guid, 16)) {
				/* hey, this one might _really_ be MDLM! */
				dev_dbg(&intf->dev, "MDLM guid\n");
				goto bad_desc;
			}
			break;
		case USB_CDC_MDLM_DETAIL_TYPE:
			if (detail) {
				dev_dbg(&intf->dev, "extra MDLM detail\n");
				goto bad_desc;
			}
			detail = (void *) buf;
			switch (detail->bGuidDescriptorType) {
			case 0:			/* "SAFE" */
				if (detail->bLength != (sizeof *detail + 2))
					goto bad_detail;
				break;
			case 1:			/* "BLAN" */
				if (detail->bLength != (sizeof *detail + 3))
					goto bad_detail;
				break;
			default:
				goto bad_detail;
			}

			/* assuming we either noticed BLAN already, or will
			 * find it soon, there are some data bytes here:
			 *  - bmNetworkCapabilities (unused)
			 *  - bmDataCapabilities (bits, see below)
			 *  - bPad (ignored, for PADAFTER -- BLAN-only)
			 * bits are:
			 *  - 0x01 -- Zaurus framing (add CRC)
			 *  - 0x02 -- PADBEFORE (CRC includes some padding)
			 *  - 0x04 -- PADAFTER (some padding after CRC)
			 *  - 0x08 -- "fermat" packet mangling (for hw bugs)
			 * the PADBEFORE appears not to matter; we interop
			 * with devices that use it and those that don't.
			 */
			if ((detail->bDetailData[1] & ~0x02) != 0x01) {
				/* bmDataCapabilities == 0 would be fine too,
				 * but framing is minidriver-coupled for now.
				 */
bad_detail:
				dev_dbg(&intf->dev,
						"bad MDLM detail, %d %d %d\n",
						detail->bLength,
						detail->bDetailData[0],
						detail->bDetailData[2]);
				goto bad_desc;
			}

			/* same extra framing as for non-BLAN mode */
			dev->net->hard_header_len += 6;
			dev->rx_urb_size = dev->net->hard_header_len
					+ dev->net->mtu;
			break;
		}
next_desc:
		len -= buf [0];	/* bLength */
		buf += buf [0];
	}

	if (!desc || !detail) {
		dev_dbg(&intf->dev, "missing cdc mdlm %s%sdescriptor\n",
			desc ? "" : "func ",
			detail ? "" : "detail ");
		goto bad_desc;
	}

	/* There's probably a CDC Ethernet descriptor there, but we can't
	 * rely on the Ethernet address it provides since not all vendors
	 * bother to make it unique.  Likewise there's no point in tracking
	 * of the CDC event notifications.
	 */
	return usbnet_get_endpoints(dev, intf);

bad_desc:
	dev_info(&dev->udev->dev, "unsupported MDLM descriptors\n");
	return -ENODEV;
}

static const struct driver_info	bogus_mdlm_info = {
	.description =	"pseudo-MDLM (BLAN) device",
	.flags =	FLAG_POINTTOPOINT | FLAG_FRAMING_Z,
	.check_connect = always_connected,
	.tx_fixup =	zaurus_tx_fixup,
	.bind =		blan_mdlm_bind,
};

static const struct usb_device_id	products [] = {
#define	ZAURUS_MASTER_INTERFACE \
	.bInterfaceClass	= USB_CLASS_COMM, \
	.bInterfaceSubClass	= USB_CDC_SUBCLASS_ETHERNET, \
	.bInterfaceProtocol	= USB_CDC_PROTO_NONE

/* SA-1100 based Sharp Zaurus ("collie"), or compatible. */
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8004,
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_STRONGARM_INFO,
},

/* PXA-2xx based models are also lying-about-cdc.  If you add any
 * more devices that claim to be CDC Ethernet, make sure they get
 * added to the blacklist in cdc_ether too.
 *
 * NOTE:  OpenZaurus versions with 2.6 kernels won't use these entries,
 * unlike the older ones with 2.4 "embedix" kernels.
 */
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8005,	/* A-300 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8006,	/* B-500/SL-5600 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
	          | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x04DD,
	.idProduct		= 0x8007,	/* C-700 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	.idProduct              = 0x9031,	/* C-750 C-760 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	.idProduct              = 0x9032,	/* SL-6000 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
}, {
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x04DD,
	/* reported with some C860 units */
	.idProduct              = 0x9050,	/* C-860 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = ZAURUS_PXA_INFO,
},
{
	/* Motorola MOTOMAGX phones */
	USB_DEVICE_AND_INTERFACE_INFO(0x22b8, 0x6425, USB_CLASS_COMM,
			USB_CDC_SUBCLASS_MDLM, USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long) &bogus_mdlm_info,
},

/* Olympus has some models with a Zaurus-compatible option.
 * R-1000 uses a FreeScale i.MXL cpu (ARMv4T)
 */
{
	.match_flags    =   USB_DEVICE_ID_MATCH_INT_INFO
		 | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor               = 0x07B4,
	.idProduct              = 0x0F02,	/* R-1000 */
	ZAURUS_MASTER_INTERFACE,
	.driver_info = OLYMPUS_MXL_INFO,
},
	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver zaurus_driver = {
	.name =		"zaurus",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
};

static int __init zaurus_init(void)
{
	return usb_register(&zaurus_driver);
}
module_init(zaurus_init);

static void __exit zaurus_exit(void)
{
	usb_deregister(&zaurus_driver);
}
module_exit(zaurus_exit);

MODULE_AUTHOR("Pavel Machek, David Brownell");
MODULE_DESCRIPTION("Sharp Zaurus PDA, and compatible products");
MODULE_LICENSE("GPL");
