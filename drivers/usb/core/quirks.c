// SPDX-License-Identifier: GPL-2.0
/*
 * USB device quirk handling logic and table
 *
 * Copyright (c) 2007 Oliver Neukum
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 */

#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include <linux/usb/hcd.h>
#include "usb.h"

struct quirk_entry {
	u16 vid;
	u16 pid;
	u32 flags;
};

static DEFINE_MUTEX(quirk_mutex);

static struct quirk_entry *quirk_list;
static unsigned int quirk_count;

static char quirks_param[128];

static int quirks_param_set(const char *value, const struct kernel_param *kp)
{
	char *val, *p, *field;
	u16 vid, pid;
	u32 flags;
	size_t i;
	int err;

	val = kstrdup(value, GFP_KERNEL);
	if (!val)
		return -ENOMEM;

	err = param_set_copystring(val, kp);
	if (err) {
		kfree(val);
		return err;
	}

	mutex_lock(&quirk_mutex);

	if (!*val) {
		quirk_count = 0;
		kfree(quirk_list);
		quirk_list = NULL;
		goto unlock;
	}

	for (quirk_count = 1, i = 0; val[i]; i++)
		if (val[i] == ',')
			quirk_count++;

	if (quirk_list) {
		kfree(quirk_list);
		quirk_list = NULL;
	}

	quirk_list = kcalloc(quirk_count, sizeof(struct quirk_entry),
			     GFP_KERNEL);
	if (!quirk_list) {
		quirk_count = 0;
		mutex_unlock(&quirk_mutex);
		kfree(val);
		return -ENOMEM;
	}

	for (i = 0, p = val; p && *p;) {
		/* Each entry consists of VID:PID:flags */
		field = strsep(&p, ":");
		if (!field)
			break;

		if (kstrtou16(field, 16, &vid))
			break;

		field = strsep(&p, ":");
		if (!field)
			break;

		if (kstrtou16(field, 16, &pid))
			break;

		field = strsep(&p, ",");
		if (!field || !*field)
			break;

		/* Collect the flags */
		for (flags = 0; *field; field++) {
			switch (*field) {
			case 'a':
				flags |= USB_QUIRK_STRING_FETCH_255;
				break;
			case 'b':
				flags |= USB_QUIRK_RESET_RESUME;
				break;
			case 'c':
				flags |= USB_QUIRK_NO_SET_INTF;
				break;
			case 'd':
				flags |= USB_QUIRK_CONFIG_INTF_STRINGS;
				break;
			case 'e':
				flags |= USB_QUIRK_RESET;
				break;
			case 'f':
				flags |= USB_QUIRK_HONOR_BNUMINTERFACES;
				break;
			case 'g':
				flags |= USB_QUIRK_DELAY_INIT;
				break;
			case 'h':
				flags |= USB_QUIRK_LINEAR_UFRAME_INTR_BINTERVAL;
				break;
			case 'i':
				flags |= USB_QUIRK_DEVICE_QUALIFIER;
				break;
			case 'j':
				flags |= USB_QUIRK_IGNORE_REMOTE_WAKEUP;
				break;
			case 'k':
				flags |= USB_QUIRK_NO_LPM;
				break;
			case 'l':
				flags |= USB_QUIRK_LINEAR_FRAME_INTR_BINTERVAL;
				break;
			case 'm':
				flags |= USB_QUIRK_DISCONNECT_SUSPEND;
				break;
			case 'n':
				flags |= USB_QUIRK_DELAY_CTRL_MSG;
				break;
			case 'o':
				flags |= USB_QUIRK_HUB_SLOW_RESET;
				break;
			/* Ignore unrecognized flag characters */
			}
		}

		quirk_list[i++] = (struct quirk_entry)
			{ .vid = vid, .pid = pid, .flags = flags };
	}

	if (i < quirk_count)
		quirk_count = i;

unlock:
	mutex_unlock(&quirk_mutex);
	kfree(val);

	return 0;
}

static const struct kernel_param_ops quirks_param_ops = {
	.set = quirks_param_set,
	.get = param_get_string,
};

static struct kparam_string quirks_param_string = {
	.maxlen = sizeof(quirks_param),
	.string = quirks_param,
};

device_param_cb(quirks, &quirks_param_ops, &quirks_param_string, 0644);
MODULE_PARM_DESC(quirks, "Add/modify USB quirks by specifying quirks=vendorID:productID:quirks");

/* Lists of quirky USB devices, split in device quirks and interface quirks.
 * Device quirks are applied at the very beginning of the enumeration process,
 * right after reading the device descriptor. They can thus only match on device
 * information.
 *
 * Interface quirks are applied after reading all the configuration descriptors.
 * They can match on both device and interface information.
 *
 * Note that the DELAY_INIT and HONOR_BNUMINTERFACES quirks do not make sense as
 * interface quirks, as they only influence the enumeration process which is run
 * before processing the interface quirks.
 *
 * Please keep the lists ordered by:
 * 	1) Vendor ID
 * 	2) Product ID
 * 	3) Class ID
 */
static const struct usb_device_id usb_quirk_list[] = {
	/* CBM - Flash disk */
	{ USB_DEVICE(0x0204, 0x6025), .driver_info = USB_QUIRK_RESET_RESUME },

	/* WORLDE Controller KS49 or Prodipe MIDI 49C USB controller */
	{ USB_DEVICE(0x0218, 0x0201), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* WORLDE easy key (easykey.25) MIDI controller  */
	{ USB_DEVICE(0x0218, 0x0401), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* HP 5300/5370C scanner */
	{ USB_DEVICE(0x03f0, 0x0701), .driver_info =
			USB_QUIRK_STRING_FETCH_255 },

	/* HP v222w 16GB Mini USB Drive */
	{ USB_DEVICE(0x03f0, 0x3f40), .driver_info = USB_QUIRK_DELAY_INIT },

	/* Creative SB Audigy 2 NX */
	{ USB_DEVICE(0x041e, 0x3020), .driver_info = USB_QUIRK_RESET_RESUME },

	/* USB3503 */
	{ USB_DEVICE(0x0424, 0x3503), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Microsoft Wireless Laser Mouse 6000 Receiver */
	{ USB_DEVICE(0x045e, 0x00e1), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Microsoft LifeCam-VX700 v2.0 */
	{ USB_DEVICE(0x045e, 0x0770), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Microsoft Surface Dock Ethernet (RTL8153 GigE) */
	{ USB_DEVICE(0x045e, 0x07c6), .driver_info = USB_QUIRK_NO_LPM },

	/* Cherry Stream G230 2.0 (G85-231) and 3.0 (G85-232) */
	{ USB_DEVICE(0x046a, 0x0023), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech HD Webcam C270 */
	{ USB_DEVICE(0x046d, 0x0825), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech HD Pro Webcams C920, C920-C, C922, C925e and C930e */
	{ USB_DEVICE(0x046d, 0x082d), .driver_info = USB_QUIRK_DELAY_INIT },
	{ USB_DEVICE(0x046d, 0x0841), .driver_info = USB_QUIRK_DELAY_INIT },
	{ USB_DEVICE(0x046d, 0x0843), .driver_info = USB_QUIRK_DELAY_INIT },
	{ USB_DEVICE(0x046d, 0x085b), .driver_info = USB_QUIRK_DELAY_INIT },
	{ USB_DEVICE(0x046d, 0x085c), .driver_info = USB_QUIRK_DELAY_INIT },

	/* Logitech ConferenceCam CC3000e */
	{ USB_DEVICE(0x046d, 0x0847), .driver_info = USB_QUIRK_DELAY_INIT },
	{ USB_DEVICE(0x046d, 0x0848), .driver_info = USB_QUIRK_DELAY_INIT },

	/* Logitech PTZ Pro Camera */
	{ USB_DEVICE(0x046d, 0x0853), .driver_info = USB_QUIRK_DELAY_INIT },

	/* Logitech Screen Share */
	{ USB_DEVICE(0x046d, 0x086c), .driver_info = USB_QUIRK_NO_LPM },

	/* Logitech Quickcam Fusion */
	{ USB_DEVICE(0x046d, 0x08c1), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam Orbit MP */
	{ USB_DEVICE(0x046d, 0x08c2), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam Pro for Notebook */
	{ USB_DEVICE(0x046d, 0x08c3), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam Pro 5000 */
	{ USB_DEVICE(0x046d, 0x08c5), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam OEM Dell Notebook */
	{ USB_DEVICE(0x046d, 0x08c6), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Quickcam OEM Cisco VT Camera II */
	{ USB_DEVICE(0x046d, 0x08c7), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Harmony 700-series */
	{ USB_DEVICE(0x046d, 0xc122), .driver_info = USB_QUIRK_DELAY_INIT },

	/* Philips PSC805 audio device */
	{ USB_DEVICE(0x0471, 0x0155), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Plantronic Audio 655 DSP */
	{ USB_DEVICE(0x047f, 0xc008), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Plantronic Audio 648 USB */
	{ USB_DEVICE(0x047f, 0xc013), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Artisman Watchdog Dongle */
	{ USB_DEVICE(0x04b4, 0x0526), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Microchip Joss Optical infrared touchboard device */
	{ USB_DEVICE(0x04d8, 0x000c), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* CarrolTouch 4000U */
	{ USB_DEVICE(0x04e7, 0x0009), .driver_info = USB_QUIRK_RESET_RESUME },

	/* CarrolTouch 4500U */
	{ USB_DEVICE(0x04e7, 0x0030), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Samsung Android phone modem - ID conflict with SPH-I500 */
	{ USB_DEVICE(0x04e8, 0x6601), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Elan Touchscreen */
	{ USB_DEVICE(0x04f3, 0x0089), .driver_info =
			USB_QUIRK_DEVICE_QUALIFIER },

	{ USB_DEVICE(0x04f3, 0x009b), .driver_info =
			USB_QUIRK_DEVICE_QUALIFIER },

	{ USB_DEVICE(0x04f3, 0x010c), .driver_info =
			USB_QUIRK_DEVICE_QUALIFIER },

	{ USB_DEVICE(0x04f3, 0x0125), .driver_info =
			USB_QUIRK_DEVICE_QUALIFIER },

	{ USB_DEVICE(0x04f3, 0x016f), .driver_info =
			USB_QUIRK_DEVICE_QUALIFIER },

	{ USB_DEVICE(0x04f3, 0x0381), .driver_info =
			USB_QUIRK_NO_LPM },

	{ USB_DEVICE(0x04f3, 0x21b8), .driver_info =
			USB_QUIRK_DEVICE_QUALIFIER },

	/* Roland SC-8820 */
	{ USB_DEVICE(0x0582, 0x0007), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Edirol SD-20 */
	{ USB_DEVICE(0x0582, 0x0027), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Alcor Micro Corp. Hub */
	{ USB_DEVICE(0x058f, 0x9254), .driver_info = USB_QUIRK_RESET_RESUME },

	/* appletouch */
	{ USB_DEVICE(0x05ac, 0x021a), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Genesys Logic hub, internally used by KY-688 USB 3.1 Type-C Hub */
	{ USB_DEVICE(0x05e3, 0x0612), .driver_info = USB_QUIRK_NO_LPM },

	/* ELSA MicroLink 56K */
	{ USB_DEVICE(0x05cc, 0x2267), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Genesys Logic hub, internally used by Moshi USB to Ethernet Adapter */
	{ USB_DEVICE(0x05e3, 0x0616), .driver_info = USB_QUIRK_NO_LPM },

	/* Avision AV600U */
	{ USB_DEVICE(0x0638, 0x0a13), .driver_info =
	  USB_QUIRK_STRING_FETCH_255 },

	/* Saitek Cyborg Gold Joystick */
	{ USB_DEVICE(0x06a3, 0x0006), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Agfa SNAPSCAN 1212U */
	{ USB_DEVICE(0x06bd, 0x0001), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Guillemot Webcam Hercules Dualpix Exchange (2nd ID) */
	{ USB_DEVICE(0x06f8, 0x0804), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Guillemot Webcam Hercules Dualpix Exchange*/
	{ USB_DEVICE(0x06f8, 0x3005), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Guillemot Hercules DJ Console audio card (BZ 208357) */
	{ USB_DEVICE(0x06f8, 0xb000), .driver_info =
			USB_QUIRK_ENDPOINT_IGNORE },

	/* Midiman M-Audio Keystation 88es */
	{ USB_DEVICE(0x0763, 0x0192), .driver_info = USB_QUIRK_RESET_RESUME },

	/* SanDisk Ultra Fit and Ultra Flair */
	{ USB_DEVICE(0x0781, 0x5583), .driver_info = USB_QUIRK_NO_LPM },
	{ USB_DEVICE(0x0781, 0x5591), .driver_info = USB_QUIRK_NO_LPM },

	/* M-Systems Flash Disk Pioneers */
	{ USB_DEVICE(0x08ec, 0x1000), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Baum Vario Ultra */
	{ USB_DEVICE(0x0904, 0x6101), .driver_info =
			USB_QUIRK_LINEAR_FRAME_INTR_BINTERVAL },
	{ USB_DEVICE(0x0904, 0x6102), .driver_info =
			USB_QUIRK_LINEAR_FRAME_INTR_BINTERVAL },
	{ USB_DEVICE(0x0904, 0x6103), .driver_info =
			USB_QUIRK_LINEAR_FRAME_INTR_BINTERVAL },

	/* Sound Devices USBPre2 */
	{ USB_DEVICE(0x0926, 0x0202), .driver_info =
			USB_QUIRK_ENDPOINT_IGNORE },

	/* Sound Devices MixPre-D */
	{ USB_DEVICE(0x0926, 0x0208), .driver_info =
			USB_QUIRK_ENDPOINT_IGNORE },

	/* Keytouch QWERTY Panel keyboard */
	{ USB_DEVICE(0x0926, 0x3333), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Kingston DataTraveler 3.0 */
	{ USB_DEVICE(0x0951, 0x1666), .driver_info = USB_QUIRK_NO_LPM },

	/* X-Rite/Gretag-Macbeth Eye-One Pro display colorimeter */
	{ USB_DEVICE(0x0971, 0x2000), .driver_info = USB_QUIRK_NO_SET_INTF },

	/* ELMO L-12F document camera */
	{ USB_DEVICE(0x09a1, 0x0028), .driver_info = USB_QUIRK_DELAY_CTRL_MSG },

	/* Broadcom BCM92035DGROM BT dongle */
	{ USB_DEVICE(0x0a5c, 0x2021), .driver_info = USB_QUIRK_RESET_RESUME },

	/* MAYA44USB sound device */
	{ USB_DEVICE(0x0a92, 0x0091), .driver_info = USB_QUIRK_RESET_RESUME },

	/* ASUS Base Station(T100) */
	{ USB_DEVICE(0x0b05, 0x17e0), .driver_info =
			USB_QUIRK_IGNORE_REMOTE_WAKEUP },

	/* Realtek Semiconductor Corp. Mass Storage Device (Multicard Reader)*/
	{ USB_DEVICE(0x0bda, 0x0151), .driver_info = USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Realtek hub in Dell WD19 (Type-C) */
	{ USB_DEVICE(0x0bda, 0x0487), .driver_info = USB_QUIRK_NO_LPM },

	/* Generic RTL8153 based ethernet adapters */
	{ USB_DEVICE(0x0bda, 0x8153), .driver_info = USB_QUIRK_NO_LPM },

	/* SONiX USB DEVICE Touchpad */
	{ USB_DEVICE(0x0c45, 0x7056), .driver_info =
			USB_QUIRK_IGNORE_REMOTE_WAKEUP },

	/* Action Semiconductor flash disk */
	{ USB_DEVICE(0x10d6, 0x2200), .driver_info =
			USB_QUIRK_STRING_FETCH_255 },

	/* novation SoundControl XL */
	{ USB_DEVICE(0x1235, 0x0061), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Huawei 4G LTE module */
	{ USB_DEVICE(0x12d1, 0x15bb), .driver_info =
			USB_QUIRK_DISCONNECT_SUSPEND },
	{ USB_DEVICE(0x12d1, 0x15c3), .driver_info =
			USB_QUIRK_DISCONNECT_SUSPEND },

	/* SKYMEDI USB_DRIVE */
	{ USB_DEVICE(0x1516, 0x8628), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Razer - Razer Blade Keyboard */
	{ USB_DEVICE(0x1532, 0x0116), .driver_info =
			USB_QUIRK_LINEAR_UFRAME_INTR_BINTERVAL },

	/* Lenovo ThinkPad OneLink+ Dock twin hub controllers (VIA Labs VL812) */
	{ USB_DEVICE(0x17ef, 0x1018), .driver_info = USB_QUIRK_RESET_RESUME },
	{ USB_DEVICE(0x17ef, 0x1019), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Lenovo USB-C to Ethernet Adapter RTL8153-04 */
	{ USB_DEVICE(0x17ef, 0x720c), .driver_info = USB_QUIRK_NO_LPM },

	/* Lenovo Powered USB-C Travel Hub (4X90S92381, RTL8153 GigE) */
	{ USB_DEVICE(0x17ef, 0x721e), .driver_info = USB_QUIRK_NO_LPM },

	/* Lenovo ThinkCenter A630Z TI024Gen3 usb-audio */
	{ USB_DEVICE(0x17ef, 0xa012), .driver_info =
			USB_QUIRK_DISCONNECT_SUSPEND },

	/* Lenovo ThinkPad USB-C Dock Gen2 Ethernet (RTL8153 GigE) */
	{ USB_DEVICE(0x17ef, 0xa387), .driver_info = USB_QUIRK_NO_LPM },

	/* BUILDWIN Photo Frame */
	{ USB_DEVICE(0x1908, 0x1315), .driver_info =
			USB_QUIRK_HONOR_BNUMINTERFACES },

	/* Protocol and OTG Electrical Test Device */
	{ USB_DEVICE(0x1a0a, 0x0200), .driver_info =
			USB_QUIRK_LINEAR_UFRAME_INTR_BINTERVAL },

	/* Terminus Technology Inc. Hub */
	{ USB_DEVICE(0x1a40, 0x0101), .driver_info = USB_QUIRK_HUB_SLOW_RESET },

	/* Corsair K70 RGB */
	{ USB_DEVICE(0x1b1c, 0x1b13), .driver_info = USB_QUIRK_DELAY_INIT |
	  USB_QUIRK_DELAY_CTRL_MSG },

	/* Corsair Strafe */
	{ USB_DEVICE(0x1b1c, 0x1b15), .driver_info = USB_QUIRK_DELAY_INIT |
	  USB_QUIRK_DELAY_CTRL_MSG },

	/* Corsair Strafe RGB */
	{ USB_DEVICE(0x1b1c, 0x1b20), .driver_info = USB_QUIRK_DELAY_INIT |
	  USB_QUIRK_DELAY_CTRL_MSG },

	/* Corsair K70 LUX RGB */
	{ USB_DEVICE(0x1b1c, 0x1b33), .driver_info = USB_QUIRK_DELAY_INIT },

	/* Corsair K70 LUX */
	{ USB_DEVICE(0x1b1c, 0x1b36), .driver_info = USB_QUIRK_DELAY_INIT },

	/* Corsair K70 RGB RAPDIFIRE */
	{ USB_DEVICE(0x1b1c, 0x1b38), .driver_info = USB_QUIRK_DELAY_INIT |
	  USB_QUIRK_DELAY_CTRL_MSG },

	/* MIDI keyboard WORLDE MINI */
	{ USB_DEVICE(0x1c75, 0x0204), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Acer C120 LED Projector */
	{ USB_DEVICE(0x1de1, 0xc102), .driver_info = USB_QUIRK_NO_LPM },

	/* Blackmagic Design Intensity Shuttle */
	{ USB_DEVICE(0x1edb, 0xbd3b), .driver_info = USB_QUIRK_NO_LPM },

	/* Blackmagic Design UltraStudio SDI */
	{ USB_DEVICE(0x1edb, 0xbd4f), .driver_info = USB_QUIRK_NO_LPM },

	/* Hauppauge HVR-950q */
	{ USB_DEVICE(0x2040, 0x7200), .driver_info =
			USB_QUIRK_CONFIG_INTF_STRINGS },

	/* Raydium Touchscreen */
	{ USB_DEVICE(0x2386, 0x3114), .driver_info = USB_QUIRK_NO_LPM },

	{ USB_DEVICE(0x2386, 0x3119), .driver_info = USB_QUIRK_NO_LPM },

	{ USB_DEVICE(0x2386, 0x350e), .driver_info = USB_QUIRK_NO_LPM },

	/* DJI CineSSD */
	{ USB_DEVICE(0x2ca3, 0x0031), .driver_info = USB_QUIRK_NO_LPM },

	/* DELL USB GEN2 */
	{ USB_DEVICE(0x413c, 0xb062), .driver_info = USB_QUIRK_NO_LPM | USB_QUIRK_RESET_RESUME },

	/* VCOM device */
	{ USB_DEVICE(0x4296, 0x7570), .driver_info = USB_QUIRK_CONFIG_INTF_STRINGS },

	/* INTEL VALUE SSD */
	{ USB_DEVICE(0x8086, 0xf1a5), .driver_info = USB_QUIRK_RESET_RESUME },

	{ }  /* terminating entry must be last */
};

static const struct usb_device_id usb_interface_quirk_list[] = {
	/* Logitech UVC Cameras */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x046d, USB_CLASS_VIDEO, 1, 0),
	  .driver_info = USB_QUIRK_RESET_RESUME },

	{ }  /* terminating entry must be last */
};

static const struct usb_device_id usb_amd_resume_quirk_list[] = {
	/* Lenovo Mouse with Pixart controller */
	{ USB_DEVICE(0x17ef, 0x602e), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Pixart Mouse */
	{ USB_DEVICE(0x093a, 0x2500), .driver_info = USB_QUIRK_RESET_RESUME },
	{ USB_DEVICE(0x093a, 0x2510), .driver_info = USB_QUIRK_RESET_RESUME },
	{ USB_DEVICE(0x093a, 0x2521), .driver_info = USB_QUIRK_RESET_RESUME },
	{ USB_DEVICE(0x03f0, 0x2b4a), .driver_info = USB_QUIRK_RESET_RESUME },

	/* Logitech Optical Mouse M90/M100 */
	{ USB_DEVICE(0x046d, 0xc05a), .driver_info = USB_QUIRK_RESET_RESUME },

	{ }  /* terminating entry must be last */
};

/*
 * Entries for endpoints that should be ignored when parsing configuration
 * descriptors.
 *
 * Matched for devices with USB_QUIRK_ENDPOINT_IGNORE.
 */
static const struct usb_device_id usb_endpoint_ignore[] = {
	{ USB_DEVICE_INTERFACE_NUMBER(0x06f8, 0xb000, 5), .driver_info = 0x01 },
	{ USB_DEVICE_INTERFACE_NUMBER(0x06f8, 0xb000, 5), .driver_info = 0x81 },
	{ USB_DEVICE_INTERFACE_NUMBER(0x0926, 0x0202, 1), .driver_info = 0x85 },
	{ USB_DEVICE_INTERFACE_NUMBER(0x0926, 0x0208, 1), .driver_info = 0x85 },
	{ }
};

bool usb_endpoint_is_ignored(struct usb_device *udev,
			     struct usb_host_interface *intf,
			     struct usb_endpoint_descriptor *epd)
{
	const struct usb_device_id *id;
	unsigned int address;

	for (id = usb_endpoint_ignore; id->match_flags; ++id) {
		if (!usb_match_device(udev, id))
			continue;

		if (!usb_match_one_id_intf(udev, intf, id))
			continue;

		address = id->driver_info;
		if (address == epd->bEndpointAddress)
			return true;
	}

	return false;
}

static bool usb_match_any_interface(struct usb_device *udev,
				    const struct usb_device_id *id)
{
	unsigned int i;

	for (i = 0; i < udev->descriptor.bNumConfigurations; ++i) {
		struct usb_host_config *cfg = &udev->config[i];
		unsigned int j;

		for (j = 0; j < cfg->desc.bNumInterfaces; ++j) {
			struct usb_interface_cache *cache;
			struct usb_host_interface *intf;

			cache = cfg->intf_cache[j];
			if (cache->num_altsetting == 0)
				continue;

			intf = &cache->altsetting[0];
			if (usb_match_one_id_intf(udev, intf, id))
				return true;
		}
	}

	return false;
}

static int usb_amd_resume_quirk(struct usb_device *udev)
{
	struct usb_hcd *hcd;

	hcd = bus_to_hcd(udev->bus);
	/* The device should be attached directly to root hub */
	if (udev->level == 1 && hcd->amd_resume_bug == 1)
		return 1;

	return 0;
}

static u32 usb_detect_static_quirks(struct usb_device *udev,
				    const struct usb_device_id *id)
{
	u32 quirks = 0;

	for (; id->match_flags; id++) {
		if (!usb_match_device(udev, id))
			continue;

		if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_INFO) &&
		    !usb_match_any_interface(udev, id))
			continue;

		quirks |= (u32)(id->driver_info);
	}

	return quirks;
}

static u32 usb_detect_dynamic_quirks(struct usb_device *udev)
{
	u16 vid = le16_to_cpu(udev->descriptor.idVendor);
	u16 pid = le16_to_cpu(udev->descriptor.idProduct);
	int i, flags = 0;

	mutex_lock(&quirk_mutex);

	for (i = 0; i < quirk_count; i++) {
		if (vid == quirk_list[i].vid && pid == quirk_list[i].pid) {
			flags = quirk_list[i].flags;
			break;
		}
	}

	mutex_unlock(&quirk_mutex);

	return flags;
}

/*
 * Detect any quirks the device has, and do any housekeeping for it if needed.
 */
void usb_detect_quirks(struct usb_device *udev)
{
	udev->quirks = usb_detect_static_quirks(udev, usb_quirk_list);

	/*
	 * Pixart-based mice would trigger remote wakeup issue on AMD
	 * Yangtze chipset, so set them as RESET_RESUME flag.
	 */
	if (usb_amd_resume_quirk(udev))
		udev->quirks |= usb_detect_static_quirks(udev,
				usb_amd_resume_quirk_list);

	udev->quirks ^= usb_detect_dynamic_quirks(udev);

	if (udev->quirks)
		dev_dbg(&udev->dev, "USB quirks for this device: %x\n",
			udev->quirks);

#ifdef CONFIG_USB_DEFAULT_PERSIST
	if (!(udev->quirks & USB_QUIRK_RESET))
		udev->persist_enabled = 1;
#else
	/* Hubs are automatically enabled for USB-PERSIST */
	if (udev->descriptor.bDeviceClass == USB_CLASS_HUB)
		udev->persist_enabled = 1;
#endif	/* CONFIG_USB_DEFAULT_PERSIST */
}

void usb_detect_interface_quirks(struct usb_device *udev)
{
	u32 quirks;

	quirks = usb_detect_static_quirks(udev, usb_interface_quirk_list);
	if (quirks == 0)
		return;

	dev_dbg(&udev->dev, "USB interface quirks for this device: %x\n",
		quirks);
	udev->quirks |= quirks;
}

void usb_release_quirk_list(void)
{
	mutex_lock(&quirk_mutex);
	kfree(quirk_list);
	quirk_list = NULL;
	mutex_unlock(&quirk_mutex);
}
