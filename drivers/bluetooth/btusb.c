// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Generic Bluetooth USB driver
 *
 *  Copyright (C) 2005-2008  Marcel Holtmann <marcel@holtmann.org>
 */

#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/suspend.h>
#include <linux/gpio/consumer.h>
#include <linux/debugfs.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btintel.h"
#include "btbcm.h"
#include "btrtl.h"
#include "btmtk.h"

#define VERSION "0.8"

static bool disable_scofix;
static bool force_scofix;
static bool enable_autosuspend = IS_ENABLED(CONFIG_BT_HCIBTUSB_AUTOSUSPEND);
static bool reset = true;

static struct usb_driver btusb_driver;

#define BTUSB_IGNORE			BIT(0)
#define BTUSB_DIGIANSWER		BIT(1)
#define BTUSB_CSR			BIT(2)
#define BTUSB_SNIFFER			BIT(3)
#define BTUSB_BCM92035			BIT(4)
#define BTUSB_BROKEN_ISOC		BIT(5)
#define BTUSB_WRONG_SCO_MTU		BIT(6)
#define BTUSB_ATH3012			BIT(7)
#define BTUSB_INTEL_COMBINED		BIT(8)
#define BTUSB_INTEL_BOOT		BIT(9)
#define BTUSB_BCM_PATCHRAM		BIT(10)
#define BTUSB_MARVELL			BIT(11)
#define BTUSB_SWAVE			BIT(12)
#define BTUSB_AMP			BIT(13)
#define BTUSB_QCA_ROME			BIT(14)
#define BTUSB_BCM_APPLE			BIT(15)
#define BTUSB_REALTEK			BIT(16)
#define BTUSB_BCM2045			BIT(17)
#define BTUSB_IFNUM_2			BIT(18)
#define BTUSB_CW6622			BIT(19)
#define BTUSB_MEDIATEK			BIT(20)
#define BTUSB_WIDEBAND_SPEECH		BIT(21)
#define BTUSB_VALID_LE_STATES		BIT(22)
#define BTUSB_QCA_WCN6855		BIT(23)
#define BTUSB_INTEL_BROKEN_SHUTDOWN_LED	BIT(24)
#define BTUSB_INTEL_BROKEN_INITIAL_NCMD BIT(25)
#define BTUSB_INTEL_NO_WBS_SUPPORT	BIT(26)
#define BTUSB_ACTIONS_SEMI		BIT(27)

static const struct usb_device_id btusb_table[] = {
	/* Generic Bluetooth USB device */
	{ USB_DEVICE_INFO(0xe0, 0x01, 0x01) },

	/* Generic Bluetooth AMP device */
	{ USB_DEVICE_INFO(0xe0, 0x01, 0x04), .driver_info = BTUSB_AMP },

	/* Generic Bluetooth USB interface */
	{ USB_INTERFACE_INFO(0xe0, 0x01, 0x01) },

	/* Apple-specific (Broadcom) devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x05ac, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_APPLE | BTUSB_IFNUM_2 },

	/* MediaTek MT76x0E */
	{ USB_DEVICE(0x0e8d, 0x763f) },

	/* Broadcom SoftSailing reporting vendor specific */
	{ USB_DEVICE(0x0a5c, 0x21e1) },

	/* Apple MacBookPro 7,1 */
	{ USB_DEVICE(0x05ac, 0x8213) },

	/* Apple iMac11,1 */
	{ USB_DEVICE(0x05ac, 0x8215) },

	/* Apple MacBookPro6,2 */
	{ USB_DEVICE(0x05ac, 0x8218) },

	/* Apple MacBookAir3,1, MacBookAir3,2 */
	{ USB_DEVICE(0x05ac, 0x821b) },

	/* Apple MacBookAir4,1 */
	{ USB_DEVICE(0x05ac, 0x821f) },

	/* Apple MacBookPro8,2 */
	{ USB_DEVICE(0x05ac, 0x821a) },

	/* Apple MacMini5,1 */
	{ USB_DEVICE(0x05ac, 0x8281) },

	/* AVM BlueFRITZ! USB v2.0 */
	{ USB_DEVICE(0x057c, 0x3800), .driver_info = BTUSB_SWAVE },

	/* Bluetooth Ultraport Module from IBM */
	{ USB_DEVICE(0x04bf, 0x030a) },

	/* ALPS Modules with non-standard id */
	{ USB_DEVICE(0x044e, 0x3001) },
	{ USB_DEVICE(0x044e, 0x3002) },

	/* Ericsson with non-standard id */
	{ USB_DEVICE(0x0bdb, 0x1002) },

	/* Canyon CN-BTU1 with HID interfaces */
	{ USB_DEVICE(0x0c10, 0x0000) },

	/* Broadcom BCM20702B0 (Dynex/Insignia) */
	{ USB_DEVICE(0x19ff, 0x0239), .driver_info = BTUSB_BCM_PATCHRAM },

	/* Broadcom BCM43142A0 (Foxconn/Lenovo) */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x105b, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Broadcom BCM920703 (HTC Vive) */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0bb4, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Foxconn - Hon Hai */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0489, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Lite-On Technology - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x04ca, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Broadcom devices with vendor specific id */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0a5c, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* ASUSTek Computer - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0b05, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Belkin F8065bf - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x050d, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* IMC Networks - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x13d3, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Dell Computer - Broadcom based  */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x413c, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Toshiba Corp - Broadcom based */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0930, 0xff, 0x01, 0x01),
	  .driver_info = BTUSB_BCM_PATCHRAM },

	/* Intel Bluetooth USB Bootloader (RAM module) */
	{ USB_DEVICE(0x8087, 0x0a5a),
	  .driver_info = BTUSB_INTEL_BOOT | BTUSB_BROKEN_ISOC },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, btusb_table);

static const struct usb_device_id blacklist_table[] = {
	/* CSR BlueCore devices */
	{ USB_DEVICE(0x0a12, 0x0001), .driver_info = BTUSB_CSR },

	/* Broadcom BCM2033 without firmware */
	{ USB_DEVICE(0x0a5c, 0x2033), .driver_info = BTUSB_IGNORE },

	/* Broadcom BCM2045 devices */
	{ USB_DEVICE(0x0a5c, 0x2045), .driver_info = BTUSB_BCM2045 },

	/* Atheros 3011 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe027), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0489, 0xe03d), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x04f2, 0xaff1), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0930, 0x0215), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0cf3, 0x3002), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0cf3, 0xe019), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x13d3, 0x3304), .driver_info = BTUSB_IGNORE },

	/* Atheros AR9285 Malbec with sflash firmware */
	{ USB_DEVICE(0x03f0, 0x311d), .driver_info = BTUSB_IGNORE },

	/* Atheros 3012 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe04d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe04e), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe056), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe057), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe05f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe076), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe078), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe095), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04c5, 0x1330), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3005), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3006), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3007), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3008), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300b), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x300f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3010), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3014), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3018), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0219), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x021c), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0220), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0227), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0b05, 0x17d0), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x0036), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3008), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311e), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311f), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3121), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x817a), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x817b), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe003), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe005), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe006), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3362), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3375), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3393), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3395), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3402), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3408), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3423), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3432), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3472), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3474), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3487), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3490), .driver_info = BTUSB_ATH3012 },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe02c), .driver_info = BTUSB_IGNORE },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe036), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe03c), .driver_info = BTUSB_ATH3012 },

	/* QCA ROME chipset */
	{ USB_DEVICE(0x0cf3, 0x535b), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cf3, 0xe007), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cf3, 0xe009), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cf3, 0xe010), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cf3, 0xe300), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cf3, 0xe301), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cf3, 0xe360), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cf3, 0xe500), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0489, 0xe092), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0489, 0xe09f), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0489, 0xe0a2), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04ca, 0x3011), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04ca, 0x3015), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04ca, 0x3016), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04ca, 0x301a), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04ca, 0x3021), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3491), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3496), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3501), .driver_info = BTUSB_QCA_ROME |
						     BTUSB_WIDEBAND_SPEECH },

	/* QCA WCN6855 chipset */
	{ USB_DEVICE(0x0cf3, 0xe600), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0cc), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0d6), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0e3), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9309), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9409), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0d0), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9108), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9109), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9208), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9209), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9308), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9408), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9508), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9509), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9608), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9609), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x10ab, 0x9f09), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3022), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0c7), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0c9), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0ca), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0cb), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0ce), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0de), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0df), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0e1), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0ea), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0ec), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3023), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3024), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3a22), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3a24), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3a26), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3a27), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },

	/* QCA WCN785x chipset */
	{ USB_DEVICE(0x0cf3, 0xe700), .driver_info = BTUSB_QCA_WCN6855 |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },

	/* Broadcom BCM2035 */
	{ USB_DEVICE(0x0a5c, 0x2009), .driver_info = BTUSB_BCM92035 },
	{ USB_DEVICE(0x0a5c, 0x200a), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2035), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Broadcom BCM2045 */
	{ USB_DEVICE(0x0a5c, 0x2039), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2101), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* IBM/Lenovo ThinkPad with Broadcom chip */
	{ USB_DEVICE(0x0a5c, 0x201e), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2110), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* HP laptop with Broadcom chip */
	{ USB_DEVICE(0x03f0, 0x171d), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell laptop with Broadcom chip */
	{ USB_DEVICE(0x413c, 0x8126), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell Wireless 370 and 410 devices */
	{ USB_DEVICE(0x413c, 0x8152), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x413c, 0x8156), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Belkin F8T012 and F8T013 devices */
	{ USB_DEVICE(0x050d, 0x0012), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x050d, 0x0013), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Asus WL-BTD202 device */
	{ USB_DEVICE(0x0b05, 0x1715), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Kensington Bluetooth USB adapter */
	{ USB_DEVICE(0x047d, 0x105e), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* RTX Telecom based adapters with buggy SCO support */
	{ USB_DEVICE(0x0400, 0x0807), .driver_info = BTUSB_BROKEN_ISOC },
	{ USB_DEVICE(0x0400, 0x080a), .driver_info = BTUSB_BROKEN_ISOC },

	/* CONWISE Technology based adapters with buggy SCO support */
	{ USB_DEVICE(0x0e5e, 0x6622),
	  .driver_info = BTUSB_BROKEN_ISOC | BTUSB_CW6622},

	/* Roper Class 1 Bluetooth Dongle (Silicon Wave based) */
	{ USB_DEVICE(0x1310, 0x0001), .driver_info = BTUSB_SWAVE },

	/* Digianswer devices */
	{ USB_DEVICE(0x08fd, 0x0001), .driver_info = BTUSB_DIGIANSWER },
	{ USB_DEVICE(0x08fd, 0x0002), .driver_info = BTUSB_IGNORE },

	/* CSR BlueCore Bluetooth Sniffer */
	{ USB_DEVICE(0x0a12, 0x0002),
	  .driver_info = BTUSB_SNIFFER | BTUSB_BROKEN_ISOC },

	/* Frontline ComProbe Bluetooth Sniffer */
	{ USB_DEVICE(0x16d3, 0x0002),
	  .driver_info = BTUSB_SNIFFER | BTUSB_BROKEN_ISOC },

	/* Marvell Bluetooth devices */
	{ USB_DEVICE(0x1286, 0x2044), .driver_info = BTUSB_MARVELL },
	{ USB_DEVICE(0x1286, 0x2046), .driver_info = BTUSB_MARVELL },
	{ USB_DEVICE(0x1286, 0x204e), .driver_info = BTUSB_MARVELL },

	/* Intel Bluetooth devices */
	{ USB_DEVICE(0x8087, 0x0025), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0026), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0029), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0032), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0033), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0035), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x07da), .driver_info = BTUSB_CSR },
	{ USB_DEVICE(0x8087, 0x07dc), .driver_info = BTUSB_INTEL_COMBINED |
						     BTUSB_INTEL_NO_WBS_SUPPORT |
						     BTUSB_INTEL_BROKEN_INITIAL_NCMD |
						     BTUSB_INTEL_BROKEN_SHUTDOWN_LED },
	{ USB_DEVICE(0x8087, 0x0a2a), .driver_info = BTUSB_INTEL_COMBINED |
						     BTUSB_INTEL_NO_WBS_SUPPORT |
						     BTUSB_INTEL_BROKEN_SHUTDOWN_LED },
	{ USB_DEVICE(0x8087, 0x0a2b), .driver_info = BTUSB_INTEL_COMBINED },
	{ USB_DEVICE(0x8087, 0x0aa7), .driver_info = BTUSB_INTEL_COMBINED |
						     BTUSB_INTEL_BROKEN_SHUTDOWN_LED },
	{ USB_DEVICE(0x8087, 0x0aaa), .driver_info = BTUSB_INTEL_COMBINED },

	/* Other Intel Bluetooth devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x8087, 0xe0, 0x01, 0x01),
	  .driver_info = BTUSB_IGNORE },

	/* Realtek 8821CE Bluetooth devices */
	{ USB_DEVICE(0x13d3, 0x3529), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek 8822CE Bluetooth devices */
	{ USB_DEVICE(0x0bda, 0xb00c), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0xc822), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek 8852AE Bluetooth devices */
	{ USB_DEVICE(0x0bda, 0x2852), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0xc852), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0x385a), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0x4852), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04c5, 0x165c), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04ca, 0x4006), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cb8, 0xc549), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek 8852CE Bluetooth devices */
	{ USB_DEVICE(0x04ca, 0x4007), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04c5, 0x1675), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cb8, 0xc558), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3587), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3586), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3592), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Realtek Bluetooth devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0bda, 0xe0, 0x01, 0x01),
	  .driver_info = BTUSB_REALTEK },

	/* MediaTek Bluetooth devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0e8d, 0xe0, 0x01, 0x01),
	  .driver_info = BTUSB_MEDIATEK |
			 BTUSB_WIDEBAND_SPEECH |
			 BTUSB_VALID_LE_STATES },

	/* Additional MediaTek MT7615E Bluetooth devices */
	{ USB_DEVICE(0x13d3, 0x3560), .driver_info = BTUSB_MEDIATEK},

	/* Additional MediaTek MT7663 Bluetooth devices */
	{ USB_DEVICE(0x043e, 0x310c), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3801), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },

	/* Additional MediaTek MT7668 Bluetooth devices */
	{ USB_DEVICE(0x043e, 0x3109), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },

	/* Additional MediaTek MT7921 Bluetooth devices */
	{ USB_DEVICE(0x0489, 0xe0c8), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0e0), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0f2), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x04ca, 0x3802), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x13d3, 0x3563), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x13d3, 0x3564), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x13d3, 0x3567), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x13d3, 0x3578), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x13d3, 0x3583), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0cd), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0e8d, 0x0608), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },

	/* MediaTek MT7922A Bluetooth devices */
	{ USB_DEVICE(0x0489, 0xe0d8), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0d9), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0f5), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x13d3, 0x3568), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },
	{ USB_DEVICE(0x0489, 0xe0e2), .driver_info = BTUSB_MEDIATEK |
						     BTUSB_WIDEBAND_SPEECH |
						     BTUSB_VALID_LE_STATES },

	/* Additional Realtek 8723AE Bluetooth devices */
	{ USB_DEVICE(0x0930, 0x021d), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3394), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723BE Bluetooth devices */
	{ USB_DEVICE(0x0489, 0xe085), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x0489, 0xe08b), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x04f2, 0xb49f), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3410), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3416), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3459), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3494), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723BU Bluetooth devices */
	{ USB_DEVICE(0x7392, 0xa611), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8723DE Bluetooth devices */
	{ USB_DEVICE(0x0bda, 0xb009), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x2ff8, 0xb011), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8761BUV Bluetooth devices */
	{ USB_DEVICE(0x2357, 0x0604), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0b05, 0x190e), .driver_info = BTUSB_REALTEK |
	  					     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x2550, 0x8761), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0x8771), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x7392, 0xc611), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Additional Realtek 8821AE Bluetooth devices */
	{ USB_DEVICE(0x0b05, 0x17dc), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3414), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3458), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3461), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x13d3, 0x3462), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8822BE Bluetooth devices */
	{ USB_DEVICE(0x13d3, 0x3526), .driver_info = BTUSB_REALTEK },
	{ USB_DEVICE(0x0b05, 0x185c), .driver_info = BTUSB_REALTEK },

	/* Additional Realtek 8822CE Bluetooth devices */
	{ USB_DEVICE(0x04ca, 0x4005), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x04c5, 0x161f), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0b05, 0x18ef), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3548), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3549), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3553), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x13d3, 0x3555), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x2ff8, 0x3051), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x1358, 0xc123), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0bda, 0xc123), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },
	{ USB_DEVICE(0x0cb5, 0xc547), .driver_info = BTUSB_REALTEK |
						     BTUSB_WIDEBAND_SPEECH },

	/* Actions Semiconductor ATS2851 based devices */
	{ USB_DEVICE(0x10d7, 0xb012), .driver_info = BTUSB_ACTIONS_SEMI },

	/* Silicon Wave based devices */
	{ USB_DEVICE(0x0c10, 0x0000), .driver_info = BTUSB_SWAVE },

	{ }	/* Terminating entry */
};

/* The Bluetooth USB module build into some devices needs to be reset on resume,
 * this is a problem with the platform (likely shutting off all power) not with
 * the module itself. So we use a DMI list to match known broken platforms.
 */
static const struct dmi_system_id btusb_needs_reset_resume_table[] = {
	{
		/* Dell OptiPlex 3060 (QCA ROME device 0cf3:e007) */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 3060"),
		},
	},
	{
		/* Dell XPS 9360 (QCA ROME device 0cf3:e300) */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "XPS 13 9360"),
		},
	},
	{
		/* Dell Inspiron 5565 (QCA ROME device 0cf3:e009) */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 5565"),
		},
	},
	{}
};

#define BTUSB_MAX_ISOC_FRAMES	10

#define BTUSB_INTR_RUNNING	0
#define BTUSB_BULK_RUNNING	1
#define BTUSB_ISOC_RUNNING	2
#define BTUSB_SUSPENDING	3
#define BTUSB_DID_ISO_RESUME	4
#define BTUSB_BOOTLOADER	5
#define BTUSB_DOWNLOADING	6
#define BTUSB_FIRMWARE_LOADED	7
#define BTUSB_FIRMWARE_FAILED	8
#define BTUSB_BOOTING		9
#define BTUSB_DIAG_RUNNING	10
#define BTUSB_OOB_WAKE_ENABLED	11
#define BTUSB_HW_RESET_ACTIVE	12
#define BTUSB_TX_WAIT_VND_EVT	13
#define BTUSB_WAKEUP_AUTOSUSPEND	14
#define BTUSB_USE_ALT3_FOR_WBS	15

struct btusb_data {
	struct hci_dev       *hdev;
	struct usb_device    *udev;
	struct usb_interface *intf;
	struct usb_interface *isoc;
	struct usb_interface *diag;
	unsigned isoc_ifnum;

	unsigned long flags;

	bool poll_sync;
	int intr_interval;
	struct work_struct  work;
	struct work_struct  waker;
	struct delayed_work rx_work;

	struct sk_buff_head acl_q;

	struct usb_anchor deferred;
	struct usb_anchor tx_anchor;
	int tx_in_flight;
	spinlock_t txlock;

	struct usb_anchor intr_anchor;
	struct usb_anchor bulk_anchor;
	struct usb_anchor isoc_anchor;
	struct usb_anchor diag_anchor;
	struct usb_anchor ctrl_anchor;
	spinlock_t rxlock;

	struct sk_buff *evt_skb;
	struct sk_buff *acl_skb;
	struct sk_buff *sco_skb;

	struct usb_endpoint_descriptor *intr_ep;
	struct usb_endpoint_descriptor *bulk_tx_ep;
	struct usb_endpoint_descriptor *bulk_rx_ep;
	struct usb_endpoint_descriptor *isoc_tx_ep;
	struct usb_endpoint_descriptor *isoc_rx_ep;
	struct usb_endpoint_descriptor *diag_tx_ep;
	struct usb_endpoint_descriptor *diag_rx_ep;

	struct gpio_desc *reset_gpio;

	__u8 cmdreq_type;
	__u8 cmdreq;

	unsigned int sco_num;
	unsigned int air_mode;
	bool usb_alt6_packet_flow;
	int isoc_altsetting;
	int suspend_count;

	int (*recv_event)(struct hci_dev *hdev, struct sk_buff *skb);
	int (*recv_acl)(struct hci_dev *hdev, struct sk_buff *skb);
	int (*recv_bulk)(struct btusb_data *data, void *buffer, int count);

	int (*setup_on_usb)(struct hci_dev *hdev);

	int oob_wake_irq;   /* irq for out-of-band wake-on-bt */
	unsigned cmd_timeout_cnt;
};

static void btusb_intel_cmd_timeout(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct gpio_desc *reset_gpio = data->reset_gpio;

	if (++data->cmd_timeout_cnt < 5)
		return;

	if (!reset_gpio) {
		bt_dev_err(hdev, "No way to reset. Ignoring and continuing");
		return;
	}

	/*
	 * Toggle the hard reset line if the platform provides one. The reset
	 * is going to yank the device off the USB and then replug. So doing
	 * once is enough. The cleanup is handled correctly on the way out
	 * (standard USB disconnect), and the new device is detected cleanly
	 * and bound to the driver again like it should be.
	 */
	if (test_and_set_bit(BTUSB_HW_RESET_ACTIVE, &data->flags)) {
		bt_dev_err(hdev, "last reset failed? Not resetting again");
		return;
	}

	bt_dev_err(hdev, "Initiating HW reset via gpio");
	gpiod_set_value_cansleep(reset_gpio, 1);
	msleep(100);
	gpiod_set_value_cansleep(reset_gpio, 0);
}

static void btusb_rtl_cmd_timeout(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct gpio_desc *reset_gpio = data->reset_gpio;

	if (++data->cmd_timeout_cnt < 5)
		return;

	if (!reset_gpio) {
		bt_dev_err(hdev, "No gpio to reset Realtek device, ignoring");
		return;
	}

	/* Toggle the hard reset line. The Realtek device is going to
	 * yank itself off the USB and then replug. The cleanup is handled
	 * correctly on the way out (standard USB disconnect), and the new
	 * device is detected cleanly and bound to the driver again like
	 * it should be.
	 */
	if (test_and_set_bit(BTUSB_HW_RESET_ACTIVE, &data->flags)) {
		bt_dev_err(hdev, "last reset failed? Not resetting again");
		return;
	}

	bt_dev_err(hdev, "Reset Realtek device via gpio");
	gpiod_set_value_cansleep(reset_gpio, 1);
	msleep(200);
	gpiod_set_value_cansleep(reset_gpio, 0);
}

static void btusb_qca_cmd_timeout(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct gpio_desc *reset_gpio = data->reset_gpio;
	int err;

	if (++data->cmd_timeout_cnt < 5)
		return;

	if (reset_gpio) {
		bt_dev_err(hdev, "Reset qca device via bt_en gpio");

		/* Toggle the hard reset line. The qca bt device is going to
		 * yank itself off the USB and then replug. The cleanup is handled
		 * correctly on the way out (standard USB disconnect), and the new
		 * device is detected cleanly and bound to the driver again like
		 * it should be.
		 */
		if (test_and_set_bit(BTUSB_HW_RESET_ACTIVE, &data->flags)) {
			bt_dev_err(hdev, "last reset failed? Not resetting again");
			return;
		}

		gpiod_set_value_cansleep(reset_gpio, 0);
		msleep(200);
		gpiod_set_value_cansleep(reset_gpio, 1);

		return;
	}

	bt_dev_err(hdev, "Multiple cmd timeouts seen. Resetting usb device.");
	/* This is not an unbalanced PM reference since the device will reset */
	err = usb_autopm_get_interface(data->intf);
	if (!err)
		usb_queue_reset_device(data->intf);
	else
		bt_dev_err(hdev, "Failed usb_autopm_get_interface with %d", err);
}

static inline void btusb_free_frags(struct btusb_data *data)
{
	unsigned long flags;

	spin_lock_irqsave(&data->rxlock, flags);

	dev_kfree_skb_irq(data->evt_skb);
	data->evt_skb = NULL;

	dev_kfree_skb_irq(data->acl_skb);
	data->acl_skb = NULL;

	dev_kfree_skb_irq(data->sco_skb);
	data->sco_skb = NULL;

	spin_unlock_irqrestore(&data->rxlock, flags);
}

static int btusb_recv_event(struct btusb_data *data, struct sk_buff *skb)
{
	if (data->intr_interval) {
		/* Trigger dequeue immediatelly if an event is received */
		schedule_delayed_work(&data->rx_work, 0);
	}

	return data->recv_event(data->hdev, skb);
}

static int btusb_recv_intr(struct btusb_data *data, void *buffer, int count)
{
	struct sk_buff *skb;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&data->rxlock, flags);
	skb = data->evt_skb;

	while (count) {
		int len;

		if (!skb) {
			skb = bt_skb_alloc(HCI_MAX_EVENT_SIZE, GFP_ATOMIC);
			if (!skb) {
				err = -ENOMEM;
				break;
			}

			hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
			hci_skb_expect(skb) = HCI_EVENT_HDR_SIZE;
		}

		len = min_t(uint, hci_skb_expect(skb), count);
		skb_put_data(skb, buffer, len);

		count -= len;
		buffer += len;
		hci_skb_expect(skb) -= len;

		if (skb->len == HCI_EVENT_HDR_SIZE) {
			/* Complete event header */
			hci_skb_expect(skb) = hci_event_hdr(skb)->plen;

			if (skb_tailroom(skb) < hci_skb_expect(skb)) {
				kfree_skb(skb);
				skb = NULL;

				err = -EILSEQ;
				break;
			}
		}

		if (!hci_skb_expect(skb)) {
			/* Complete frame */
			btusb_recv_event(data, skb);
			skb = NULL;
		}
	}

	data->evt_skb = skb;
	spin_unlock_irqrestore(&data->rxlock, flags);

	return err;
}

static int btusb_recv_acl(struct btusb_data *data, struct sk_buff *skb)
{
	/* Only queue ACL packet if intr_interval is set as it means
	 * force_poll_sync has been enabled.
	 */
	if (!data->intr_interval)
		return data->recv_acl(data->hdev, skb);

	skb_queue_tail(&data->acl_q, skb);
	schedule_delayed_work(&data->rx_work, data->intr_interval);

	return 0;
}

static int btusb_recv_bulk(struct btusb_data *data, void *buffer, int count)
{
	struct sk_buff *skb;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&data->rxlock, flags);
	skb = data->acl_skb;

	while (count) {
		int len;

		if (!skb) {
			skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
			if (!skb) {
				err = -ENOMEM;
				break;
			}

			hci_skb_pkt_type(skb) = HCI_ACLDATA_PKT;
			hci_skb_expect(skb) = HCI_ACL_HDR_SIZE;
		}

		len = min_t(uint, hci_skb_expect(skb), count);
		skb_put_data(skb, buffer, len);

		count -= len;
		buffer += len;
		hci_skb_expect(skb) -= len;

		if (skb->len == HCI_ACL_HDR_SIZE) {
			__le16 dlen = hci_acl_hdr(skb)->dlen;

			/* Complete ACL header */
			hci_skb_expect(skb) = __le16_to_cpu(dlen);

			if (skb_tailroom(skb) < hci_skb_expect(skb)) {
				kfree_skb(skb);
				skb = NULL;

				err = -EILSEQ;
				break;
			}
		}

		if (!hci_skb_expect(skb)) {
			/* Complete frame */
			btusb_recv_acl(data, skb);
			skb = NULL;
		}
	}

	data->acl_skb = skb;
	spin_unlock_irqrestore(&data->rxlock, flags);

	return err;
}

static int btusb_recv_isoc(struct btusb_data *data, void *buffer, int count)
{
	struct sk_buff *skb;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&data->rxlock, flags);
	skb = data->sco_skb;

	while (count) {
		int len;

		if (!skb) {
			skb = bt_skb_alloc(HCI_MAX_SCO_SIZE, GFP_ATOMIC);
			if (!skb) {
				err = -ENOMEM;
				break;
			}

			hci_skb_pkt_type(skb) = HCI_SCODATA_PKT;
			hci_skb_expect(skb) = HCI_SCO_HDR_SIZE;
		}

		len = min_t(uint, hci_skb_expect(skb), count);
		skb_put_data(skb, buffer, len);

		count -= len;
		buffer += len;
		hci_skb_expect(skb) -= len;

		if (skb->len == HCI_SCO_HDR_SIZE) {
			/* Complete SCO header */
			hci_skb_expect(skb) = hci_sco_hdr(skb)->dlen;

			if (skb_tailroom(skb) < hci_skb_expect(skb)) {
				kfree_skb(skb);
				skb = NULL;

				err = -EILSEQ;
				break;
			}
		}

		if (!hci_skb_expect(skb)) {
			/* Complete frame */
			hci_recv_frame(data->hdev, skb);
			skb = NULL;
		}
	}

	data->sco_skb = skb;
	spin_unlock_irqrestore(&data->rxlock, flags);

	return err;
}

static void btusb_intr_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (btusb_recv_intr(data, urb->transfer_buffer,
				    urb->actual_length) < 0) {
			bt_dev_err(hdev, "corrupted event packet");
			hdev->stat.err_rx++;
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_INTR_RUNNING, &data->flags))
		return;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p failed to resubmit (%d)",
				   urb, -err);
		if (err != -EPERM)
			hci_cmd_sync_cancel(hdev, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!data->intr_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->intr_ep->wMaxPacketSize);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size,
			 btusb_intr_complete, hdev, data->intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p submission failed (%d)",
				   urb, -err);
		if (err != -EPERM)
			hci_cmd_sync_cancel(hdev, -err);
		usb_unanchor_urb(urb);
	}

	/* Only initialize intr_interval if URB poll sync is enabled */
	if (!data->poll_sync)
		goto done;

	/* The units are frames (milliseconds) for full and low speed devices,
	 * and microframes (1/8 millisecond) for highspeed and SuperSpeed
	 * devices.
	 *
	 * This is done once on open/resume so it shouldn't change even if
	 * force_poll_sync changes.
	 */
	switch (urb->dev->speed) {
	case USB_SPEED_SUPER_PLUS:
	case USB_SPEED_SUPER:	/* units are 125us */
		data->intr_interval = usecs_to_jiffies(urb->interval * 125);
		break;
	default:
		data->intr_interval = msecs_to_jiffies(urb->interval);
		break;
	}

done:
	usb_free_urb(urb);

	return err;
}

static void btusb_bulk_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (data->recv_bulk(data, urb->transfer_buffer,
				    urb->actual_length) < 0) {
			bt_dev_err(hdev, "corrupted ACL packet");
			hdev->stat.err_rx++;
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_BULK_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->bulk_anchor);
	usb_mark_last_busy(data->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p failed to resubmit (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_bulk_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	BT_DBG("%s", hdev->name);

	if (!data->bulk_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, data->bulk_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe, buf, size,
			  btusb_bulk_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->bulk_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p submission failed (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_isoc_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int i, err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned int offset = urb->iso_frame_desc[i].offset;
			unsigned int length = urb->iso_frame_desc[i].actual_length;

			if (urb->iso_frame_desc[i].status)
				continue;

			hdev->stat.byte_rx += length;

			if (btusb_recv_isoc(data, urb->transfer_buffer + offset,
					    length) < 0) {
				bt_dev_err(hdev, "corrupted SCO packet");
				hdev->stat.err_rx++;
			}
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_ISOC_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p failed to resubmit (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline void __fill_isoc_descriptor_msbc(struct urb *urb, int len,
					       int mtu, struct btusb_data *data)
{
	int i, offset = 0;
	unsigned int interval;

	BT_DBG("len %d mtu %d", len, mtu);

	/* For mSBC ALT 6 setting the host will send the packet at continuous
	 * flow. As per core spec 5, vol 4, part B, table 2.1. For ALT setting
	 * 6 the HCI PACKET INTERVAL should be 7.5ms for every usb packets.
	 * To maintain the rate we send 63bytes of usb packets alternatively for
	 * 7ms and 8ms to maintain the rate as 7.5ms.
	 */
	if (data->usb_alt6_packet_flow) {
		interval = 7;
		data->usb_alt6_packet_flow = false;
	} else {
		interval = 6;
		data->usb_alt6_packet_flow = true;
	}

	for (i = 0; i < interval; i++) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = offset;
	}

	if (len && i < BTUSB_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		i++;
	}

	urb->number_of_packets = i;
}

static inline void __fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
	int i, offset = 0;

	BT_DBG("len %d mtu %d", len, mtu);

	for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
					i++, offset += mtu, len -= mtu) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = mtu;
	}

	if (len && i < BTUSB_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		i++;
	}

	urb->number_of_packets = i;
}

static int btusb_submit_isoc_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!data->isoc_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize) *
						BTUSB_MAX_ISOC_FRAMES;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvisocpipe(data->udev, data->isoc_rx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size, btusb_isoc_complete,
			 hdev, data->isoc_rx_ep->bInterval);

	urb->transfer_flags = URB_FREE_BUFFER | URB_ISO_ASAP;

	__fill_isoc_descriptor(urb, size,
			       le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize));

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p submission failed (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_diag_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (urb->status == 0) {
		struct sk_buff *skb;

		skb = bt_skb_alloc(urb->actual_length, GFP_ATOMIC);
		if (skb) {
			skb_put_data(skb, urb->transfer_buffer,
				     urb->actual_length);
			hci_recv_diag(hdev, skb);
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (!test_bit(BTUSB_DIAG_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->diag_anchor);
	usb_mark_last_busy(data->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p failed to resubmit (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_diag_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	BT_DBG("%s", hdev->name);

	if (!data->diag_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, data->diag_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe, buf, size,
			  btusb_diag_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->diag_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p submission failed (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;
	struct btusb_data *data = hci_get_drvdata(hdev);
	unsigned long flags;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status) {
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	} else {
		if (hci_skb_pkt_type(skb) == HCI_COMMAND_PKT)
			hci_cmd_sync_cancel(hdev, -urb->status);
		hdev->stat.err_tx++;
	}

done:
	spin_lock_irqsave(&data->txlock, flags);
	data->tx_in_flight--;
	spin_unlock_irqrestore(&data->txlock, flags);

	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static void btusb_isoc_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static int btusb_open(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s", hdev->name);

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return err;

	/* Patching USB firmware files prior to starting any URBs of HCI path
	 * It is more safe to use USB bulk channel for downloading USB patch
	 */
	if (data->setup_on_usb) {
		err = data->setup_on_usb(hdev);
		if (err < 0)
			goto setup_fail;
	}

	data->intf->needs_remote_wakeup = 1;

	if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags))
		goto done;

	err = btusb_submit_intr_urb(hdev, GFP_KERNEL);
	if (err < 0)
		goto failed;

	err = btusb_submit_bulk_urb(hdev, GFP_KERNEL);
	if (err < 0) {
		usb_kill_anchored_urbs(&data->intr_anchor);
		goto failed;
	}

	set_bit(BTUSB_BULK_RUNNING, &data->flags);
	btusb_submit_bulk_urb(hdev, GFP_KERNEL);

	if (data->diag) {
		if (!btusb_submit_diag_urb(hdev, GFP_KERNEL))
			set_bit(BTUSB_DIAG_RUNNING, &data->flags);
	}

done:
	usb_autopm_put_interface(data->intf);
	return 0;

failed:
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);
setup_fail:
	usb_autopm_put_interface(data->intf);
	return err;
}

static void btusb_stop_traffic(struct btusb_data *data)
{
	usb_kill_anchored_urbs(&data->intr_anchor);
	usb_kill_anchored_urbs(&data->bulk_anchor);
	usb_kill_anchored_urbs(&data->isoc_anchor);
	usb_kill_anchored_urbs(&data->diag_anchor);
	usb_kill_anchored_urbs(&data->ctrl_anchor);
}

static int btusb_close(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s", hdev->name);

	cancel_delayed_work(&data->rx_work);
	cancel_work_sync(&data->work);
	cancel_work_sync(&data->waker);

	skb_queue_purge(&data->acl_q);

	clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
	clear_bit(BTUSB_BULK_RUNNING, &data->flags);
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);
	clear_bit(BTUSB_DIAG_RUNNING, &data->flags);

	btusb_stop_traffic(data);
	btusb_free_frags(data);

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		goto failed;

	data->intf->needs_remote_wakeup = 0;

	/* Enable remote wake up for auto-suspend */
	if (test_bit(BTUSB_WAKEUP_AUTOSUSPEND, &data->flags))
		data->intf->needs_remote_wakeup = 1;

	usb_autopm_put_interface(data->intf);

failed:
	usb_scuttle_anchored_urbs(&data->deferred);
	return 0;
}

static int btusb_flush(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s", hdev->name);

	cancel_delayed_work(&data->rx_work);

	skb_queue_purge(&data->acl_q);

	usb_kill_anchored_urbs(&data->tx_anchor);
	btusb_free_frags(data);

	return 0;
}

static struct urb *alloc_ctrl_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		usb_free_urb(urb);
		return ERR_PTR(-ENOMEM);
	}

	dr->bRequestType = data->cmdreq_type;
	dr->bRequest     = data->cmdreq;
	dr->wIndex       = 0;
	dr->wValue       = 0;
	dr->wLength      = __cpu_to_le16(skb->len);

	pipe = usb_sndctrlpipe(data->udev, 0x00);

	usb_fill_control_urb(urb, data->udev, pipe, (void *)dr,
			     skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_bulk_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned int pipe;

	if (!data->bulk_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	pipe = usb_sndbulkpipe(data->udev, data->bulk_tx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe,
			  skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static struct urb *alloc_isoc_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned int pipe;

	if (!data->isoc_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	pipe = usb_sndisocpipe(data->udev, data->isoc_tx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe,
			 skb->data, skb->len, btusb_isoc_tx_complete,
			 skb, data->isoc_tx_ep->bInterval);

	urb->transfer_flags  = URB_ISO_ASAP;

	if (data->isoc_altsetting == 6)
		__fill_isoc_descriptor_msbc(urb, skb->len,
					    le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize),
					    data);
	else
		__fill_isoc_descriptor(urb, skb->len,
				       le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));
	skb->dev = (void *)hdev;

	return urb;
}

static int submit_tx_urb(struct hci_dev *hdev, struct urb *urb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	usb_anchor_urb(urb, &data->tx_anchor);

	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p submission failed (%d)",
				   urb, -err);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	} else {
		usb_mark_last_busy(data->udev);
	}

	usb_free_urb(urb);
	return err;
}

static int submit_or_queue_tx_urb(struct hci_dev *hdev, struct urb *urb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	unsigned long flags;
	bool suspending;

	spin_lock_irqsave(&data->txlock, flags);
	suspending = test_bit(BTUSB_SUSPENDING, &data->flags);
	if (!suspending)
		data->tx_in_flight++;
	spin_unlock_irqrestore(&data->txlock, flags);

	if (!suspending)
		return submit_tx_urb(hdev, urb);

	usb_anchor_urb(urb, &data->deferred);
	schedule_work(&data->waker);

	usb_free_urb(urb);
	return 0;
}

static int btusb_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct urb *urb;

	BT_DBG("%s", hdev->name);

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		urb = alloc_ctrl_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.cmd_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_ACLDATA_PKT:
		urb = alloc_bulk_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.acl_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_SCODATA_PKT:
		if (hci_conn_num(hdev, SCO_LINK) < 1)
			return -ENODEV;

		urb = alloc_isoc_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.sco_tx++;
		return submit_tx_urb(hdev, urb);

	case HCI_ISODATA_PKT:
		urb = alloc_bulk_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		return submit_or_queue_tx_urb(hdev, urb);
	}

	return -EILSEQ;
}

static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s evt %d", hdev->name, evt);

	if (hci_conn_num(hdev, SCO_LINK) != data->sco_num) {
		data->sco_num = hci_conn_num(hdev, SCO_LINK);
		data->air_mode = evt;
		schedule_work(&data->work);
	}
}

static inline int __set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_interface *intf = data->isoc;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;

	if (!data->isoc)
		return -ENODEV;

	err = usb_set_interface(data->udev, data->isoc_ifnum, altsetting);
	if (err < 0) {
		bt_dev_err(hdev, "setting interface failed (%d)", -err);
		return err;
	}

	data->isoc_altsetting = altsetting;

	data->isoc_tx_ep = NULL;
	data->isoc_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
			data->isoc_tx_ep = ep_desc;
			continue;
		}

		if (!data->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
			data->isoc_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->isoc_tx_ep || !data->isoc_rx_ep) {
		bt_dev_err(hdev, "invalid SCO descriptors");
		return -ENODEV;
	}

	return 0;
}

static int btusb_switch_alt_setting(struct hci_dev *hdev, int new_alts)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	if (data->isoc_altsetting != new_alts) {
		unsigned long flags;

		clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		usb_kill_anchored_urbs(&data->isoc_anchor);

		/* When isochronous alternate setting needs to be
		 * changed, because SCO connection has been added
		 * or removed, a packet fragment may be left in the
		 * reassembling state. This could lead to wrongly
		 * assembled fragments.
		 *
		 * Clear outstanding fragment when selecting a new
		 * alternate setting.
		 */
		spin_lock_irqsave(&data->rxlock, flags);
		dev_kfree_skb_irq(data->sco_skb);
		data->sco_skb = NULL;
		spin_unlock_irqrestore(&data->rxlock, flags);

		err = __set_isoc_interface(hdev, new_alts);
		if (err < 0)
			return err;
	}

	if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
		if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		else
			btusb_submit_isoc_urb(hdev, GFP_KERNEL);
	}

	return 0;
}

static struct usb_host_interface *btusb_find_altsetting(struct btusb_data *data,
							int alt)
{
	struct usb_interface *intf = data->isoc;
	int i;

	BT_DBG("Looking for Alt no :%d", alt);

	if (!intf)
		return NULL;

	for (i = 0; i < intf->num_altsetting; i++) {
		if (intf->altsetting[i].desc.bAlternateSetting == alt)
			return &intf->altsetting[i];
	}

	return NULL;
}

static void btusb_work(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, work);
	struct hci_dev *hdev = data->hdev;
	int new_alts = 0;
	int err;

	if (data->sco_num > 0) {
		if (!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)) {
			err = usb_autopm_get_interface(data->isoc ? data->isoc : data->intf);
			if (err < 0) {
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
				usb_kill_anchored_urbs(&data->isoc_anchor);
				return;
			}

			set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
		}

		if (data->air_mode == HCI_NOTIFY_ENABLE_SCO_CVSD) {
			if (hdev->voice_setting & 0x0020) {
				static const int alts[3] = { 2, 4, 5 };

				new_alts = alts[data->sco_num - 1];
			} else {
				new_alts = data->sco_num;
			}
		} else if (data->air_mode == HCI_NOTIFY_ENABLE_SCO_TRANSP) {
			/* Bluetooth USB spec recommends alt 6 (63 bytes), but
			 * many adapters do not support it.  Alt 1 appears to
			 * work for all adapters that do not have alt 6, and
			 * which work with WBS at all.  Some devices prefer
			 * alt 3 (HCI payload >= 60 Bytes let air packet
			 * data satisfy 60 bytes), requiring
			 * MTU >= 3 (packets) * 25 (size) - 3 (headers) = 72
			 * see also Core spec 5, vol 4, B 2.1.1 & Table 2.1.
			 */
			if (btusb_find_altsetting(data, 6))
				new_alts = 6;
			else if (btusb_find_altsetting(data, 3) &&
				 hdev->sco_mtu >= 72 &&
				 test_bit(BTUSB_USE_ALT3_FOR_WBS, &data->flags))
				new_alts = 3;
			else
				new_alts = 1;
		}

		if (btusb_switch_alt_setting(hdev, new_alts) < 0)
			bt_dev_err(hdev, "set USB alt:(%d) failed!", new_alts);
	} else {
		clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		usb_kill_anchored_urbs(&data->isoc_anchor);

		__set_isoc_interface(hdev, 0);
		if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &data->flags))
			usb_autopm_put_interface(data->isoc ? data->isoc : data->intf);
	}
}

static void btusb_waker(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, waker);
	int err;

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return;

	usb_autopm_put_interface(data->intf);
}

static void btusb_rx_work(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data,
					       rx_work.work);
	struct sk_buff *skb;

	/* Dequeue ACL data received during the interval */
	while ((skb = skb_dequeue(&data->acl_q)))
		data->recv_acl(data->hdev, skb);
}

static int btusb_setup_bcm92035(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	u8 val = 0x00;

	BT_DBG("%s", hdev->name);

	skb = __hci_cmd_sync(hdev, 0xfc3b, 1, &val, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		bt_dev_err(hdev, "BCM92035 command failed (%ld)", PTR_ERR(skb));
	else
		kfree_skb(skb);

	return 0;
}

static int btusb_setup_csr(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	u16 bcdDevice = le16_to_cpu(data->udev->descriptor.bcdDevice);
	struct hci_rp_read_local_version *rp;
	struct sk_buff *skb;
	bool is_fake = false;
	int ret;

	BT_DBG("%s", hdev->name);

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		bt_dev_err(hdev, "CSR: Local version failed (%d)", err);
		return err;
	}

	if (skb->len != sizeof(struct hci_rp_read_local_version)) {
		bt_dev_err(hdev, "CSR: Local version length mismatch");
		kfree_skb(skb);
		return -EIO;
	}

	rp = (struct hci_rp_read_local_version *)skb->data;

	bt_dev_info(hdev, "CSR: Setting up dongle with HCI ver=%u rev=%04x; LMP ver=%u subver=%04x; manufacturer=%u",
		le16_to_cpu(rp->hci_ver), le16_to_cpu(rp->hci_rev),
		le16_to_cpu(rp->lmp_ver), le16_to_cpu(rp->lmp_subver),
		le16_to_cpu(rp->manufacturer));

	/* Detect a wide host of Chinese controllers that aren't CSR.
	 *
	 * Known fake bcdDevices: 0x0100, 0x0134, 0x1915, 0x2520, 0x7558, 0x8891
	 *
	 * The main thing they have in common is that these are really popular low-cost
	 * options that support newer Bluetooth versions but rely on heavy VID/PID
	 * squatting of this poor old Bluetooth 1.1 device. Even sold as such.
	 *
	 * We detect actual CSR devices by checking that the HCI manufacturer code
	 * is Cambridge Silicon Radio (10) and ensuring that LMP sub-version and
	 * HCI rev values always match. As they both store the firmware number.
	 */
	if (le16_to_cpu(rp->manufacturer) != 10 ||
	    le16_to_cpu(rp->hci_rev) != le16_to_cpu(rp->lmp_subver))
		is_fake = true;

	/* Known legit CSR firmware build numbers and their supported BT versions:
	 * - 1.1 (0x1) -> 0x0073, 0x020d, 0x033c, 0x034e
	 * - 1.2 (0x2) ->                 0x04d9, 0x0529
	 * - 2.0 (0x3) ->         0x07a6, 0x07ad, 0x0c5c
	 * - 2.1 (0x4) ->         0x149c, 0x1735, 0x1899 (0x1899 is a BlueCore4-External)
	 * - 4.0 (0x6) ->         0x1d86, 0x2031, 0x22bb
	 *
	 * e.g. Real CSR dongles with LMP subversion 0x73 are old enough that
	 *      support BT 1.1 only; so it's a dead giveaway when some
	 *      third-party BT 4.0 dongle reuses it.
	 */
	else if (le16_to_cpu(rp->lmp_subver) <= 0x034e &&
		 le16_to_cpu(rp->hci_ver) > BLUETOOTH_VER_1_1)
		is_fake = true;

	else if (le16_to_cpu(rp->lmp_subver) <= 0x0529 &&
		 le16_to_cpu(rp->hci_ver) > BLUETOOTH_VER_1_2)
		is_fake = true;

	else if (le16_to_cpu(rp->lmp_subver) <= 0x0c5c &&
		 le16_to_cpu(rp->hci_ver) > BLUETOOTH_VER_2_0)
		is_fake = true;

	else if (le16_to_cpu(rp->lmp_subver) <= 0x1899 &&
		 le16_to_cpu(rp->hci_ver) > BLUETOOTH_VER_2_1)
		is_fake = true;

	else if (le16_to_cpu(rp->lmp_subver) <= 0x22bb &&
		 le16_to_cpu(rp->hci_ver) > BLUETOOTH_VER_4_0)
		is_fake = true;

	/* Other clones which beat all the above checks */
	else if (bcdDevice == 0x0134 &&
		 le16_to_cpu(rp->lmp_subver) == 0x0c5c &&
		 le16_to_cpu(rp->hci_ver) == BLUETOOTH_VER_2_0)
		is_fake = true;

	if (is_fake) {
		bt_dev_warn(hdev, "CSR: Unbranded CSR clone detected; adding workarounds and force-suspending once...");

		/* Generally these clones have big discrepancies between
		 * advertised features and what's actually supported.
		 * Probably will need to be expanded in the future;
		 * without these the controller will lock up.
		 */
		set_bit(HCI_QUIRK_BROKEN_STORED_LINK_KEY, &hdev->quirks);
		set_bit(HCI_QUIRK_BROKEN_ERR_DATA_REPORTING, &hdev->quirks);
		set_bit(HCI_QUIRK_BROKEN_FILTER_CLEAR_ALL, &hdev->quirks);
		set_bit(HCI_QUIRK_NO_SUSPEND_NOTIFIER, &hdev->quirks);

		/* Clear the reset quirk since this is not an actual
		 * early Bluetooth 1.1 device from CSR.
		 */
		clear_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);
		clear_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);

		/*
		 * Special workaround for these BT 4.0 chip clones, and potentially more:
		 *
		 * - 0x0134: a Barrot 8041a02                 (HCI rev: 0x0810 sub: 0x1012)
		 * - 0x7558: IC markings FR3191AHAL 749H15143 (HCI rev/sub-version: 0x0709)
		 *
		 * These controllers are really messed-up.
		 *
		 * 1. Their bulk RX endpoint will never report any data unless
		 *    the device was suspended at least once (yes, really).
		 * 2. They will not wakeup when autosuspended and receiving data
		 *    on their bulk RX endpoint from e.g. a keyboard or mouse
		 *    (IOW remote-wakeup support is broken for the bulk endpoint).
		 *
		 * To fix 1. enable runtime-suspend, force-suspend the
		 * HCI and then wake-it up by disabling runtime-suspend.
		 *
		 * To fix 2. clear the HCI's can_wake flag, this way the HCI
		 * will still be autosuspended when it is not open.
		 *
		 * --
		 *
		 * Because these are widespread problems we prefer generic solutions; so
		 * apply this initialization quirk to every controller that gets here,
		 * it should be harmless. The alternative is to not work at all.
		 */
		pm_runtime_allow(&data->udev->dev);

		ret = pm_runtime_suspend(&data->udev->dev);
		if (ret >= 0)
			msleep(200);
		else
			bt_dev_warn(hdev, "CSR: Couldn't suspend the device for our Barrot 8041a02 receive-issue workaround");

		pm_runtime_forbid(&data->udev->dev);

		device_set_wakeup_capable(&data->udev->dev, false);

		/* Re-enable autosuspend if this was requested */
		if (enable_autosuspend)
			usb_enable_autosuspend(data->udev);
	}

	kfree_skb(skb);

	return 0;
}

static int inject_cmd_complete(struct hci_dev *hdev, __u16 opcode)
{
	struct sk_buff *skb;
	struct hci_event_hdr *hdr;
	struct hci_ev_cmd_complete *evt;

	skb = bt_skb_alloc(sizeof(*hdr) + sizeof(*evt) + 1, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->evt = HCI_EV_CMD_COMPLETE;
	hdr->plen = sizeof(*evt) + 1;

	evt = skb_put(skb, sizeof(*evt));
	evt->ncmd = 0x01;
	evt->opcode = cpu_to_le16(opcode);

	skb_put_u8(skb, 0x00);

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;

	return hci_recv_frame(hdev, skb);
}

static int btusb_recv_bulk_intel(struct btusb_data *data, void *buffer,
				 int count)
{
	struct hci_dev *hdev = data->hdev;

	/* When the device is in bootloader mode, then it can send
	 * events via the bulk endpoint. These events are treated the
	 * same way as the ones received from the interrupt endpoint.
	 */
	if (btintel_test_flag(hdev, INTEL_BOOTLOADER))
		return btusb_recv_intr(data, buffer, count);

	return btusb_recv_bulk(data, buffer, count);
}

static int btusb_recv_event_intel(struct hci_dev *hdev, struct sk_buff *skb)
{
	if (btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
		struct hci_event_hdr *hdr = (void *)skb->data;

		if (skb->len > HCI_EVENT_HDR_SIZE && hdr->evt == 0xff &&
		    hdr->plen > 0) {
			const void *ptr = skb->data + HCI_EVENT_HDR_SIZE + 1;
			unsigned int len = skb->len - HCI_EVENT_HDR_SIZE - 1;

			switch (skb->data[2]) {
			case 0x02:
				/* When switching to the operational firmware
				 * the device sends a vendor specific event
				 * indicating that the bootup completed.
				 */
				btintel_bootup(hdev, ptr, len);
				break;
			case 0x06:
				/* When the firmware loading completes the
				 * device sends out a vendor specific event
				 * indicating the result of the firmware
				 * loading.
				 */
				btintel_secure_send_result(hdev, ptr, len);
				break;
			}
		}
	}

	return hci_recv_frame(hdev, skb);
}

static int btusb_send_frame_intel(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct urb *urb;

	BT_DBG("%s", hdev->name);

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		if (btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
			struct hci_command_hdr *cmd = (void *)skb->data;
			__u16 opcode = le16_to_cpu(cmd->opcode);

			/* When in bootloader mode and the command 0xfc09
			 * is received, it needs to be send down the
			 * bulk endpoint. So allocate a bulk URB instead.
			 */
			if (opcode == 0xfc09)
				urb = alloc_bulk_urb(hdev, skb);
			else
				urb = alloc_ctrl_urb(hdev, skb);

			/* When the 0xfc01 command is issued to boot into
			 * the operational firmware, it will actually not
			 * send a command complete event. To keep the flow
			 * control working inject that event here.
			 */
			if (opcode == 0xfc01)
				inject_cmd_complete(hdev, opcode);
		} else {
			urb = alloc_ctrl_urb(hdev, skb);
		}
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.cmd_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_ACLDATA_PKT:
		urb = alloc_bulk_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.acl_tx++;
		return submit_or_queue_tx_urb(hdev, urb);

	case HCI_SCODATA_PKT:
		if (hci_conn_num(hdev, SCO_LINK) < 1)
			return -ENODEV;

		urb = alloc_isoc_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		hdev->stat.sco_tx++;
		return submit_tx_urb(hdev, urb);

	case HCI_ISODATA_PKT:
		urb = alloc_bulk_urb(hdev, skb);
		if (IS_ERR(urb))
			return PTR_ERR(urb);

		return submit_or_queue_tx_urb(hdev, urb);
	}

	return -EILSEQ;
}

/* UHW CR mapping */
#define MTK_BT_MISC		0x70002510
#define MTK_BT_SUBSYS_RST	0x70002610
#define MTK_UDMA_INT_STA_BT	0x74000024
#define MTK_UDMA_INT_STA_BT1	0x74000308
#define MTK_BT_WDT_STATUS	0x740003A0
#define MTK_EP_RST_OPT		0x74011890
#define MTK_EP_RST_IN_OUT_OPT	0x00010001
#define MTK_BT_RST_DONE		0x00000100
#define MTK_BT_RESET_WAIT_MS	100
#define MTK_BT_RESET_NUM_TRIES	10

static void btusb_mtk_wmt_recv(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct sk_buff *skb;
	int err;

	if (urb->status == 0 && urb->actual_length > 0) {
		hdev->stat.byte_rx += urb->actual_length;

		/* WMT event shouldn't be fragmented and the size should be
		 * less than HCI_WMT_MAX_EVENT_SIZE.
		 */
		skb = bt_skb_alloc(HCI_WMT_MAX_EVENT_SIZE, GFP_ATOMIC);
		if (!skb) {
			hdev->stat.err_rx++;
			kfree(urb->setup_packet);
			return;
		}

		hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
		skb_put_data(skb, urb->transfer_buffer, urb->actual_length);

		/* When someone waits for the WMT event, the skb is being cloned
		 * and being processed the events from there then.
		 */
		if (test_bit(BTUSB_TX_WAIT_VND_EVT, &data->flags)) {
			data->evt_skb = skb_clone(skb, GFP_ATOMIC);
			if (!data->evt_skb) {
				kfree_skb(skb);
				kfree(urb->setup_packet);
				return;
			}
		}

		err = hci_recv_frame(hdev, skb);
		if (err < 0) {
			kfree_skb(data->evt_skb);
			data->evt_skb = NULL;
			kfree(urb->setup_packet);
			return;
		}

		if (test_and_clear_bit(BTUSB_TX_WAIT_VND_EVT,
				       &data->flags)) {
			/* Barrier to sync with other CPUs */
			smp_mb__after_atomic();
			wake_up_bit(&data->flags,
				    BTUSB_TX_WAIT_VND_EVT);
		}
		kfree(urb->setup_packet);
		return;
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	usb_mark_last_busy(data->udev);

	/* The URB complete handler is still called with urb->actual_length = 0
	 * when the event is not available, so we should keep re-submitting
	 * URB until WMT event returns, Also, It's necessary to wait some time
	 * between the two consecutive control URBs to relax the target device
	 * to generate the event. Otherwise, the WMT event cannot return from
	 * the device successfully.
	 */
	udelay(500);

	usb_anchor_urb(urb, &data->ctrl_anchor);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		kfree(urb->setup_packet);
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p failed to resubmit (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_mtk_submit_wmt_recv_urb(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	unsigned char *buf;
	int err, size = 64;
	unsigned int pipe;
	struct urb *urb;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	dr->bRequestType = USB_TYPE_VENDOR | USB_DIR_IN;
	dr->bRequest     = 1;
	dr->wIndex       = cpu_to_le16(0);
	dr->wValue       = cpu_to_le16(48);
	dr->wLength      = cpu_to_le16(size);

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		kfree(dr);
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvctrlpipe(data->udev, 0);

	usb_fill_control_urb(urb, data->udev, pipe, (void *)dr,
			     buf, size, btusb_mtk_wmt_recv, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->ctrl_anchor);
	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p submission failed (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static int btusb_mtk_hci_wmt_sync(struct hci_dev *hdev,
				  struct btmtk_hci_wmt_params *wmt_params)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_evt_funcc *wmt_evt_funcc;
	u32 hlen, status = BTMTK_WMT_INVALID;
	struct btmtk_hci_wmt_evt *wmt_evt;
	struct btmtk_hci_wmt_cmd *wc;
	struct btmtk_wmt_hdr *hdr;
	int err;

	/* Send the WMT command and wait until the WMT event returns */
	hlen = sizeof(*hdr) + wmt_params->dlen;
	if (hlen > 255)
		return -EINVAL;

	wc = kzalloc(hlen, GFP_KERNEL);
	if (!wc)
		return -ENOMEM;

	hdr = &wc->hdr;
	hdr->dir = 1;
	hdr->op = wmt_params->op;
	hdr->dlen = cpu_to_le16(wmt_params->dlen + 1);
	hdr->flag = wmt_params->flag;
	memcpy(wc->data, wmt_params->data, wmt_params->dlen);

	set_bit(BTUSB_TX_WAIT_VND_EVT, &data->flags);

	/* WMT cmd/event doesn't follow up the generic HCI cmd/event handling,
	 * it needs constantly polling control pipe until the host received the
	 * WMT event, thus, we should require to specifically acquire PM counter
	 * on the USB to prevent the interface from entering auto suspended
	 * while WMT cmd/event in progress.
	 */
	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		goto err_free_wc;

	err = __hci_cmd_send(hdev, 0xfc6f, hlen, wc);

	if (err < 0) {
		clear_bit(BTUSB_TX_WAIT_VND_EVT, &data->flags);
		usb_autopm_put_interface(data->intf);
		goto err_free_wc;
	}

	/* Submit control IN URB on demand to process the WMT event */
	err = btusb_mtk_submit_wmt_recv_urb(hdev);

	usb_autopm_put_interface(data->intf);

	if (err < 0)
		goto err_free_wc;

	/* The vendor specific WMT commands are all answered by a vendor
	 * specific event and will have the Command Status or Command
	 * Complete as with usual HCI command flow control.
	 *
	 * After sending the command, wait for BTUSB_TX_WAIT_VND_EVT
	 * state to be cleared. The driver specific event receive routine
	 * will clear that state and with that indicate completion of the
	 * WMT command.
	 */
	err = wait_on_bit_timeout(&data->flags, BTUSB_TX_WAIT_VND_EVT,
				  TASK_INTERRUPTIBLE, HCI_INIT_TIMEOUT);
	if (err == -EINTR) {
		bt_dev_err(hdev, "Execution of wmt command interrupted");
		clear_bit(BTUSB_TX_WAIT_VND_EVT, &data->flags);
		goto err_free_wc;
	}

	if (err) {
		bt_dev_err(hdev, "Execution of wmt command timed out");
		clear_bit(BTUSB_TX_WAIT_VND_EVT, &data->flags);
		err = -ETIMEDOUT;
		goto err_free_wc;
	}

	/* Parse and handle the return WMT event */
	wmt_evt = (struct btmtk_hci_wmt_evt *)data->evt_skb->data;
	if (wmt_evt->whdr.op != hdr->op) {
		bt_dev_err(hdev, "Wrong op received %d expected %d",
			   wmt_evt->whdr.op, hdr->op);
		err = -EIO;
		goto err_free_skb;
	}

	switch (wmt_evt->whdr.op) {
	case BTMTK_WMT_SEMAPHORE:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_UNDONE;
		else
			status = BTMTK_WMT_PATCH_DONE;
		break;
	case BTMTK_WMT_FUNC_CTRL:
		wmt_evt_funcc = (struct btmtk_hci_wmt_evt_funcc *)wmt_evt;
		if (be16_to_cpu(wmt_evt_funcc->status) == 0x404)
			status = BTMTK_WMT_ON_DONE;
		else if (be16_to_cpu(wmt_evt_funcc->status) == 0x420)
			status = BTMTK_WMT_ON_PROGRESS;
		else
			status = BTMTK_WMT_ON_UNDONE;
		break;
	case BTMTK_WMT_PATCH_DWNLD:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_DONE;
		else if (wmt_evt->whdr.flag == 1)
			status = BTMTK_WMT_PATCH_PROGRESS;
		else
			status = BTMTK_WMT_PATCH_UNDONE;
		break;
	}

	if (wmt_params->status)
		*wmt_params->status = status;

err_free_skb:
	kfree_skb(data->evt_skb);
	data->evt_skb = NULL;
err_free_wc:
	kfree(wc);
	return err;
}

static int btusb_mtk_func_query(struct hci_dev *hdev)
{
	struct btmtk_hci_wmt_params wmt_params;
	int status, err;
	u8 param = 0;

	/* Query whether the function is enabled */
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 4;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = &status;

	err = btusb_mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to query function status (%d)", err);
		return err;
	}

	return status;
}

static int btusb_mtk_uhw_reg_write(struct btusb_data *data, u32 reg, u32 val)
{
	struct hci_dev *hdev = data->hdev;
	int pipe, err;
	void *buf;

	buf = kzalloc(4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	put_unaligned_le32(val, buf);

	pipe = usb_sndctrlpipe(data->udev, 0);
	err = usb_control_msg(data->udev, pipe, 0x02,
			      0x5E,
			      reg >> 16, reg & 0xffff,
			      buf, 4, USB_CTRL_SET_TIMEOUT);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to write uhw reg(%d)", err);
		goto err_free_buf;
	}

err_free_buf:
	kfree(buf);

	return err;
}

static int btusb_mtk_uhw_reg_read(struct btusb_data *data, u32 reg, u32 *val)
{
	struct hci_dev *hdev = data->hdev;
	int pipe, err;
	void *buf;

	buf = kzalloc(4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pipe = usb_rcvctrlpipe(data->udev, 0);
	err = usb_control_msg(data->udev, pipe, 0x01,
			      0xDE,
			      reg >> 16, reg & 0xffff,
			      buf, 4, USB_CTRL_SET_TIMEOUT);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to read uhw reg(%d)", err);
		goto err_free_buf;
	}

	*val = get_unaligned_le32(buf);
	bt_dev_dbg(hdev, "reg=%x, value=0x%08x", reg, *val);

err_free_buf:
	kfree(buf);

	return err;
}

static int btusb_mtk_reg_read(struct btusb_data *data, u32 reg, u32 *val)
{
	int pipe, err, size = sizeof(u32);
	void *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pipe = usb_rcvctrlpipe(data->udev, 0);
	err = usb_control_msg(data->udev, pipe, 0x63,
			      USB_TYPE_VENDOR | USB_DIR_IN,
			      reg >> 16, reg & 0xffff,
			      buf, size, USB_CTRL_SET_TIMEOUT);
	if (err < 0)
		goto err_free_buf;

	*val = get_unaligned_le32(buf);

err_free_buf:
	kfree(buf);

	return err;
}

static int btusb_mtk_id_get(struct btusb_data *data, u32 reg, u32 *id)
{
	return btusb_mtk_reg_read(data, reg, id);
}

static int btusb_mtk_setup(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	ktime_t calltime, delta, rettime;
	struct btmtk_tci_sleep tci_sleep;
	unsigned long long duration;
	struct sk_buff *skb;
	const char *fwname;
	int err, status;
	u32 dev_id;
	char fw_bin_name[64];
	u32 fw_version = 0;
	u8 param;

	calltime = ktime_get();

	err = btusb_mtk_id_get(data, 0x80000008, &dev_id);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to get device id (%d)", err);
		return err;
	}

	if (!dev_id) {
		err = btusb_mtk_id_get(data, 0x70010200, &dev_id);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get device id (%d)", err);
			return err;
		}
		err = btusb_mtk_id_get(data, 0x80021004, &fw_version);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get fw version (%d)", err);
			return err;
		}
	}

	switch (dev_id) {
	case 0x7663:
		fwname = FIRMWARE_MT7663;
		break;
	case 0x7668:
		fwname = FIRMWARE_MT7668;
		break;
	case 0x7922:
	case 0x7961:
		snprintf(fw_bin_name, sizeof(fw_bin_name),
			"mediatek/BT_RAM_CODE_MT%04x_1_%x_hdr.bin",
			 dev_id & 0xffff, (fw_version & 0xff) + 1);
		err = btmtk_setup_firmware_79xx(hdev, fw_bin_name,
						btusb_mtk_hci_wmt_sync);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to set up firmware (%d)", err);
			return err;
		}

		/* It's Device EndPoint Reset Option Register */
		btusb_mtk_uhw_reg_write(data, MTK_EP_RST_OPT, MTK_EP_RST_IN_OUT_OPT);

		/* Enable Bluetooth protocol */
		param = 1;
		wmt_params.op = BTMTK_WMT_FUNC_CTRL;
		wmt_params.flag = 0;
		wmt_params.dlen = sizeof(param);
		wmt_params.data = &param;
		wmt_params.status = NULL;

		err = btusb_mtk_hci_wmt_sync(hdev, &wmt_params);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
			return err;
		}

		hci_set_msft_opcode(hdev, 0xFD30);
		hci_set_aosp_capable(hdev);
		goto done;
	default:
		bt_dev_err(hdev, "Unsupported hardware variant (%08x)",
			   dev_id);
		return -ENODEV;
	}

	/* Query whether the firmware is already download */
	wmt_params.op = BTMTK_WMT_SEMAPHORE;
	wmt_params.flag = 1;
	wmt_params.dlen = 0;
	wmt_params.data = NULL;
	wmt_params.status = &status;

	err = btusb_mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to query firmware status (%d)", err);
		return err;
	}

	if (status == BTMTK_WMT_PATCH_DONE) {
		bt_dev_info(hdev, "firmware already downloaded");
		goto ignore_setup_fw;
	}

	/* Setup a firmware which the device definitely requires */
	err = btmtk_setup_firmware(hdev, fwname,
				   btusb_mtk_hci_wmt_sync);
	if (err < 0)
		return err;

ignore_setup_fw:
	err = readx_poll_timeout(btusb_mtk_func_query, hdev, status,
				 status < 0 || status != BTMTK_WMT_ON_PROGRESS,
				 2000, 5000000);
	/* -ETIMEDOUT happens */
	if (err < 0)
		return err;

	/* The other errors happen in btusb_mtk_func_query */
	if (status < 0)
		return status;

	if (status == BTMTK_WMT_ON_DONE) {
		bt_dev_info(hdev, "function already on");
		goto ignore_func_on;
	}

	/* Enable Bluetooth protocol */
	param = 1;
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = btusb_mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

ignore_func_on:
	/* Apply the low power environment setup */
	tci_sleep.mode = 0x5;
	tci_sleep.duration = cpu_to_le16(0x640);
	tci_sleep.host_duration = cpu_to_le16(0x640);
	tci_sleep.host_wakeup_pin = 0;
	tci_sleep.time_compensation = 0;

	skb = __hci_cmd_sync(hdev, 0xfc7a, sizeof(tci_sleep), &tci_sleep,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Failed to apply low power setting (%d)", err);
		return err;
	}
	kfree_skb(skb);

done:
	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long)ktime_to_ns(delta) >> 10;

	bt_dev_info(hdev, "Device setup in %llu usecs", duration);

	return 0;
}

static int btusb_mtk_shutdown(struct hci_dev *hdev)
{
	struct btmtk_hci_wmt_params wmt_params;
	u8 param = 0;
	int err;

	/* Disable the device */
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = btusb_mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	return 0;
}

static void btusb_mtk_cmd_timeout(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	u32 val;
	int err, retry = 0;

	/* It's MediaTek specific bluetooth reset mechanism via USB */
	if (test_and_set_bit(BTUSB_HW_RESET_ACTIVE, &data->flags)) {
		bt_dev_err(hdev, "last reset failed? Not resetting again");
		return;
	}

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return;

	btusb_stop_traffic(data);
	usb_kill_anchored_urbs(&data->tx_anchor);

	/* It's Device EndPoint Reset Option Register */
	bt_dev_dbg(hdev, "Initiating reset mechanism via uhw");
	btusb_mtk_uhw_reg_write(data, MTK_EP_RST_OPT, MTK_EP_RST_IN_OUT_OPT);
	btusb_mtk_uhw_reg_read(data, MTK_BT_WDT_STATUS, &val);

	/* Reset the bluetooth chip via USB interface. */
	btusb_mtk_uhw_reg_write(data, MTK_BT_SUBSYS_RST, 1);
	btusb_mtk_uhw_reg_write(data, MTK_UDMA_INT_STA_BT, 0x000000FF);
	btusb_mtk_uhw_reg_read(data, MTK_UDMA_INT_STA_BT, &val);
	btusb_mtk_uhw_reg_write(data, MTK_UDMA_INT_STA_BT1, 0x000000FF);
	btusb_mtk_uhw_reg_read(data, MTK_UDMA_INT_STA_BT1, &val);
	/* MT7921 need to delay 20ms between toggle reset bit */
	msleep(20);
	btusb_mtk_uhw_reg_write(data, MTK_BT_SUBSYS_RST, 0);
	btusb_mtk_uhw_reg_read(data, MTK_BT_SUBSYS_RST, &val);

	/* Poll the register until reset is completed */
	do {
		btusb_mtk_uhw_reg_read(data, MTK_BT_MISC, &val);
		if (val & MTK_BT_RST_DONE) {
			bt_dev_dbg(hdev, "Bluetooth Reset Successfully");
			break;
		}

		bt_dev_dbg(hdev, "Polling Bluetooth Reset CR");
		retry++;
		msleep(MTK_BT_RESET_WAIT_MS);
	} while (retry < MTK_BT_RESET_NUM_TRIES);

	btusb_mtk_id_get(data, 0x70010200, &val);
	if (!val)
		bt_dev_err(hdev, "Can't get device id, subsys reset fail.");

	usb_queue_reset_device(data->intf);

	clear_bit(BTUSB_HW_RESET_ACTIVE, &data->flags);
}

static int btusb_recv_acl_mtk(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	u16 handle = le16_to_cpu(hci_acl_hdr(skb)->handle);

	switch (handle) {
	case 0xfc6f:		/* Firmware dump from device */
		/* When the firmware hangs, the device can no longer
		 * suspend and thus disable auto-suspend.
		 */
		usb_disable_autosuspend(data->udev);
		fallthrough;
	case 0x05ff:		/* Firmware debug logging 1 */
	case 0x05fe:		/* Firmware debug logging 2 */
		return hci_recv_diag(hdev, skb);
	}

	return hci_recv_frame(hdev, skb);
}

#ifdef CONFIG_PM
/* Configure an out-of-band gpio as wake-up pin, if specified in device tree */
static int marvell_config_oob_wake(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct device *dev = &data->udev->dev;
	u16 pin, gap, opcode;
	int ret;
	u8 cmd[5];

	/* Move on if no wakeup pin specified */
	if (of_property_read_u16(dev->of_node, "marvell,wakeup-pin", &pin) ||
	    of_property_read_u16(dev->of_node, "marvell,wakeup-gap-ms", &gap))
		return 0;

	/* Vendor specific command to configure a GPIO as wake-up pin */
	opcode = hci_opcode_pack(0x3F, 0x59);
	cmd[0] = opcode & 0xFF;
	cmd[1] = opcode >> 8;
	cmd[2] = 2; /* length of parameters that follow */
	cmd[3] = pin;
	cmd[4] = gap; /* time in ms, for which wakeup pin should be asserted */

	skb = bt_skb_alloc(sizeof(cmd), GFP_KERNEL);
	if (!skb) {
		bt_dev_err(hdev, "%s: No memory", __func__);
		return -ENOMEM;
	}

	skb_put_data(skb, cmd, sizeof(cmd));
	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;

	ret = btusb_send_frame(hdev, skb);
	if (ret) {
		bt_dev_err(hdev, "%s: configuration failed", __func__);
		kfree_skb(skb);
		return ret;
	}

	return 0;
}
#endif

static int btusb_set_bdaddr_marvell(struct hci_dev *hdev,
				    const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	u8 buf[8];
	long ret;

	buf[0] = 0xfe;
	buf[1] = sizeof(bdaddr_t);
	memcpy(buf + 2, bdaddr, sizeof(bdaddr_t));

	skb = __hci_cmd_sync(hdev, 0xfc22, sizeof(buf), buf, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		bt_dev_err(hdev, "changing Marvell device address failed (%ld)",
			   ret);
		return ret;
	}
	kfree_skb(skb);

	return 0;
}

static int btusb_set_bdaddr_ath3012(struct hci_dev *hdev,
				    const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	u8 buf[10];
	long ret;

	buf[0] = 0x01;
	buf[1] = 0x01;
	buf[2] = 0x00;
	buf[3] = sizeof(bdaddr_t);
	memcpy(buf + 4, bdaddr, sizeof(bdaddr_t));

	skb = __hci_cmd_sync(hdev, 0xfc0b, sizeof(buf), buf, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		bt_dev_err(hdev, "Change address command failed (%ld)", ret);
		return ret;
	}
	kfree_skb(skb);

	return 0;
}

static int btusb_set_bdaddr_wcn6855(struct hci_dev *hdev,
				const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	u8 buf[6];
	long ret;

	memcpy(buf, bdaddr, sizeof(bdaddr_t));

	skb = __hci_cmd_sync_ev(hdev, 0xfc14, sizeof(buf), buf,
				HCI_EV_CMD_COMPLETE, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		bt_dev_err(hdev, "Change address command failed (%ld)", ret);
		return ret;
	}
	kfree_skb(skb);

	return 0;
}

#define QCA_DFU_PACKET_LEN	4096

#define QCA_GET_TARGET_VERSION	0x09
#define QCA_CHECK_STATUS	0x05
#define QCA_DFU_DOWNLOAD	0x01

#define QCA_SYSCFG_UPDATED	0x40
#define QCA_PATCH_UPDATED	0x80
#define QCA_DFU_TIMEOUT		3000
#define QCA_FLAG_MULTI_NVM      0x80
#define QCA_BT_RESET_WAIT_MS    100

#define WCN6855_2_0_RAM_VERSION_GF 0x400c1200
#define WCN6855_2_1_RAM_VERSION_GF 0x400c1211

struct qca_version {
	__le32	rom_version;
	__le32	patch_version;
	__le32	ram_version;
	__u8	chip_id;
	__u8	platform_id;
	__le16	flag;
	__u8	reserved[4];
} __packed;

struct qca_rampatch_version {
	__le16	rom_version_high;
	__le16  rom_version_low;
	__le16	patch_version;
} __packed;

struct qca_device_info {
	u32	rom_version;
	u8	rampatch_hdr;	/* length of header in rampatch */
	u8	nvm_hdr;	/* length of header in NVM */
	u8	ver_offset;	/* offset of version structure in rampatch */
};

static const struct qca_device_info qca_devices_table[] = {
	{ 0x00000100, 20, 4,  8 }, /* Rome 1.0 */
	{ 0x00000101, 20, 4,  8 }, /* Rome 1.1 */
	{ 0x00000200, 28, 4, 16 }, /* Rome 2.0 */
	{ 0x00000201, 28, 4, 16 }, /* Rome 2.1 */
	{ 0x00000300, 28, 4, 16 }, /* Rome 3.0 */
	{ 0x00000302, 28, 4, 16 }, /* Rome 3.2 */
	{ 0x00130100, 40, 4, 16 }, /* WCN6855 1.0 */
	{ 0x00130200, 40, 4, 16 }, /* WCN6855 2.0 */
	{ 0x00130201, 40, 4, 16 }, /* WCN6855 2.1 */
	{ 0x00190200, 40, 4, 16 }, /* WCN785x 2.0 */
};

static int btusb_qca_send_vendor_req(struct usb_device *udev, u8 request,
				     void *data, u16 size)
{
	int pipe, err;
	u8 *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Found some of USB hosts have IOT issues with ours so that we should
	 * not wait until HCI layer is ready.
	 */
	pipe = usb_rcvctrlpipe(udev, 0);
	err = usb_control_msg(udev, pipe, request, USB_TYPE_VENDOR | USB_DIR_IN,
			      0, 0, buf, size, USB_CTRL_SET_TIMEOUT);
	if (err < 0) {
		dev_err(&udev->dev, "Failed to access otp area (%d)", err);
		goto done;
	}

	memcpy(data, buf, size);

done:
	kfree(buf);

	return err;
}

static int btusb_setup_qca_download_fw(struct hci_dev *hdev,
				       const struct firmware *firmware,
				       size_t hdr_size)
{
	struct btusb_data *btdata = hci_get_drvdata(hdev);
	struct usb_device *udev = btdata->udev;
	size_t count, size, sent = 0;
	int pipe, len, err;
	u8 *buf;

	buf = kmalloc(QCA_DFU_PACKET_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	count = firmware->size;

	size = min_t(size_t, count, hdr_size);
	memcpy(buf, firmware->data, size);

	/* USB patches should go down to controller through USB path
	 * because binary format fits to go down through USB channel.
	 * USB control path is for patching headers and USB bulk is for
	 * patch body.
	 */
	pipe = usb_sndctrlpipe(udev, 0);
	err = usb_control_msg(udev, pipe, QCA_DFU_DOWNLOAD, USB_TYPE_VENDOR,
			      0, 0, buf, size, USB_CTRL_SET_TIMEOUT);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send headers (%d)", err);
		goto done;
	}

	sent += size;
	count -= size;

	/* ep2 need time to switch from function acl to function dfu,
	 * so we add 20ms delay here.
	 */
	msleep(20);

	while (count) {
		size = min_t(size_t, count, QCA_DFU_PACKET_LEN);

		memcpy(buf, firmware->data + sent, size);

		pipe = usb_sndbulkpipe(udev, 0x02);
		err = usb_bulk_msg(udev, pipe, buf, size, &len,
				   QCA_DFU_TIMEOUT);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to send body at %zd of %zd (%d)",
				   sent, firmware->size, err);
			break;
		}

		if (size != len) {
			bt_dev_err(hdev, "Failed to get bulk buffer");
			err = -EILSEQ;
			break;
		}

		sent  += size;
		count -= size;
	}

done:
	kfree(buf);
	return err;
}

static int btusb_setup_qca_load_rampatch(struct hci_dev *hdev,
					 struct qca_version *ver,
					 const struct qca_device_info *info)
{
	struct qca_rampatch_version *rver;
	const struct firmware *fw;
	u32 ver_rom, ver_patch, rver_rom;
	u16 rver_rom_low, rver_rom_high, rver_patch;
	char fwname[64];
	int err;

	ver_rom = le32_to_cpu(ver->rom_version);
	ver_patch = le32_to_cpu(ver->patch_version);

	snprintf(fwname, sizeof(fwname), "qca/rampatch_usb_%08x.bin", ver_rom);

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err) {
		bt_dev_err(hdev, "failed to request rampatch file: %s (%d)",
			   fwname, err);
		return err;
	}

	bt_dev_info(hdev, "using rampatch file: %s", fwname);

	rver = (struct qca_rampatch_version *)(fw->data + info->ver_offset);
	rver_rom_low = le16_to_cpu(rver->rom_version_low);
	rver_patch = le16_to_cpu(rver->patch_version);

	if (ver_rom & ~0xffffU) {
		rver_rom_high = le16_to_cpu(rver->rom_version_high);
		rver_rom = le32_to_cpu(rver_rom_high << 16 | rver_rom_low);
	} else {
		rver_rom = rver_rom_low;
	}

	bt_dev_info(hdev, "QCA: patch rome 0x%x build 0x%x, "
		    "firmware rome 0x%x build 0x%x",
		    rver_rom, rver_patch, ver_rom, ver_patch);

	if (rver_rom != ver_rom || rver_patch <= ver_patch) {
		bt_dev_err(hdev, "rampatch file version did not match with firmware");
		err = -EINVAL;
		goto done;
	}

	err = btusb_setup_qca_download_fw(hdev, fw, info->rampatch_hdr);

done:
	release_firmware(fw);

	return err;
}

static void btusb_generate_qca_nvm_name(char *fwname, size_t max_size,
					const struct qca_version *ver)
{
	u32 rom_version = le32_to_cpu(ver->rom_version);
	u16 flag = le16_to_cpu(ver->flag);

	if (((flag >> 8) & 0xff) == QCA_FLAG_MULTI_NVM) {
		/* The board_id should be split into two bytes
		 * The 1st byte is chip ID, and the 2nd byte is platform ID
		 * For example, board ID 0x010A, 0x01 is platform ID. 0x0A is chip ID
		 * we have several platforms, and platform IDs are continuously added
		 * Platform ID:
		 * 0x00 is for Mobile
		 * 0x01 is for X86
		 * 0x02 is for Automotive
		 * 0x03 is for Consumer electronic
		 */
		u16 board_id = (ver->chip_id << 8) + ver->platform_id;
		const char *variant;

		switch (le32_to_cpu(ver->ram_version)) {
		case WCN6855_2_0_RAM_VERSION_GF:
		case WCN6855_2_1_RAM_VERSION_GF:
			variant = "_gf";
			break;
		default:
			variant = "";
			break;
		}

		if (board_id == 0) {
			snprintf(fwname, max_size, "qca/nvm_usb_%08x%s.bin",
				rom_version, variant);
		} else {
			snprintf(fwname, max_size, "qca/nvm_usb_%08x%s_%04x.bin",
				rom_version, variant, board_id);
		}
	} else {
		snprintf(fwname, max_size, "qca/nvm_usb_%08x.bin",
			rom_version);
	}

}

static int btusb_setup_qca_load_nvm(struct hci_dev *hdev,
				    struct qca_version *ver,
				    const struct qca_device_info *info)
{
	const struct firmware *fw;
	char fwname[64];
	int err;

	btusb_generate_qca_nvm_name(fwname, sizeof(fwname), ver);

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err) {
		bt_dev_err(hdev, "failed to request NVM file: %s (%d)",
			   fwname, err);
		return err;
	}

	bt_dev_info(hdev, "using NVM file: %s", fwname);

	err = btusb_setup_qca_download_fw(hdev, fw, info->nvm_hdr);

	release_firmware(fw);

	return err;
}

/* identify the ROM version and check whether patches are needed */
static bool btusb_qca_need_patch(struct usb_device *udev)
{
	struct qca_version ver;

	if (btusb_qca_send_vendor_req(udev, QCA_GET_TARGET_VERSION, &ver,
				      sizeof(ver)) < 0)
		return false;
	/* only low ROM versions need patches */
	return !(le32_to_cpu(ver.rom_version) & ~0xffffU);
}

static int btusb_setup_qca(struct hci_dev *hdev)
{
	struct btusb_data *btdata = hci_get_drvdata(hdev);
	struct usb_device *udev = btdata->udev;
	const struct qca_device_info *info = NULL;
	struct qca_version ver;
	u32 ver_rom;
	u8 status;
	int i, err;

	err = btusb_qca_send_vendor_req(udev, QCA_GET_TARGET_VERSION, &ver,
					sizeof(ver));
	if (err < 0)
		return err;

	ver_rom = le32_to_cpu(ver.rom_version);

	for (i = 0; i < ARRAY_SIZE(qca_devices_table); i++) {
		if (ver_rom == qca_devices_table[i].rom_version)
			info = &qca_devices_table[i];
	}
	if (!info) {
		/* If the rom_version is not matched in the qca_devices_table
		 * and the high ROM version is not zero, we assume this chip no
		 * need to load the rampatch and nvm.
		 */
		if (ver_rom & ~0xffffU)
			return 0;

		bt_dev_err(hdev, "don't support firmware rome 0x%x", ver_rom);
		return -ENODEV;
	}

	err = btusb_qca_send_vendor_req(udev, QCA_CHECK_STATUS, &status,
					sizeof(status));
	if (err < 0)
		return err;

	if (!(status & QCA_PATCH_UPDATED)) {
		err = btusb_setup_qca_load_rampatch(hdev, &ver, info);
		if (err < 0)
			return err;
	}

	err = btusb_qca_send_vendor_req(udev, QCA_GET_TARGET_VERSION, &ver,
					sizeof(ver));
	if (err < 0)
		return err;

	if (!(status & QCA_SYSCFG_UPDATED)) {
		err = btusb_setup_qca_load_nvm(hdev, &ver, info);
		if (err < 0)
			return err;

		/* WCN6855 2.1 and later will reset to apply firmware downloaded here, so
		 * wait ~100ms for reset Done then go ahead, otherwise, it maybe
		 * cause potential enable failure.
		 */
		if (info->rom_version >= 0x00130201)
			msleep(QCA_BT_RESET_WAIT_MS);
	}

	/* Mark HCI_OP_ENHANCED_SETUP_SYNC_CONN as broken as it doesn't seem to
	 * work with the likes of HSP/HFP mSBC.
	 */
	set_bit(HCI_QUIRK_BROKEN_ENHANCED_SETUP_SYNC_CONN, &hdev->quirks);

	return 0;
}

static inline int __set_diag_interface(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_interface *intf = data->diag;
	int i;

	if (!data->diag)
		return -ENODEV;

	data->diag_tx_ep = NULL;
	data->diag_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep_desc;

		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->diag_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			data->diag_tx_ep = ep_desc;
			continue;
		}

		if (!data->diag_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->diag_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->diag_tx_ep || !data->diag_rx_ep) {
		bt_dev_err(hdev, "invalid diagnostic descriptors");
		return -ENODEV;
	}

	return 0;
}

static struct urb *alloc_diag_urb(struct hci_dev *hdev, bool enable)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct sk_buff *skb;
	struct urb *urb;
	unsigned int pipe;

	if (!data->diag_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	skb = bt_skb_alloc(2, GFP_KERNEL);
	if (!skb) {
		usb_free_urb(urb);
		return ERR_PTR(-ENOMEM);
	}

	skb_put_u8(skb, 0xf0);
	skb_put_u8(skb, enable);

	pipe = usb_sndbulkpipe(data->udev, data->diag_tx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe,
			  skb->data, skb->len, btusb_tx_complete, skb);

	skb->dev = (void *)hdev;

	return urb;
}

static int btusb_bcm_set_diag(struct hci_dev *hdev, bool enable)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;

	if (!data->diag)
		return -ENODEV;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -ENETDOWN;

	urb = alloc_diag_urb(hdev, enable);
	if (IS_ERR(urb))
		return PTR_ERR(urb);

	return submit_or_queue_tx_urb(hdev, urb);
}

#ifdef CONFIG_PM
static irqreturn_t btusb_oob_wake_handler(int irq, void *priv)
{
	struct btusb_data *data = priv;

	pm_wakeup_event(&data->udev->dev, 0);
	pm_system_wakeup();

	/* Disable only if not already disabled (keep it balanced) */
	if (test_and_clear_bit(BTUSB_OOB_WAKE_ENABLED, &data->flags)) {
		disable_irq_nosync(irq);
		disable_irq_wake(irq);
	}
	return IRQ_HANDLED;
}

static const struct of_device_id btusb_match_table[] = {
	{ .compatible = "usb1286,204e" },
	{ .compatible = "usbcf3,e300" }, /* QCA6174A */
	{ .compatible = "usb4ca,301a" }, /* QCA6174A (Lite-On) */
	{ }
};
MODULE_DEVICE_TABLE(of, btusb_match_table);

/* Use an oob wakeup pin? */
static int btusb_config_oob_wake(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct device *dev = &data->udev->dev;
	int irq, ret;

	clear_bit(BTUSB_OOB_WAKE_ENABLED, &data->flags);

	if (!of_match_device(btusb_match_table, dev))
		return 0;

	/* Move on if no IRQ specified */
	irq = of_irq_get_byname(dev->of_node, "wakeup");
	if (irq <= 0) {
		bt_dev_dbg(hdev, "%s: no OOB Wakeup IRQ in DT", __func__);
		return 0;
	}

	irq_set_status_flags(irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(&hdev->dev, irq, btusb_oob_wake_handler,
			       0, "OOB Wake-on-BT", data);
	if (ret) {
		bt_dev_err(hdev, "%s: IRQ request failed", __func__);
		return ret;
	}

	ret = device_init_wakeup(dev, true);
	if (ret) {
		bt_dev_err(hdev, "%s: failed to init_wakeup", __func__);
		return ret;
	}

	data->oob_wake_irq = irq;
	bt_dev_info(hdev, "OOB Wake-on-BT configured at IRQ %u", irq);
	return 0;
}
#endif

static void btusb_check_needs_reset_resume(struct usb_interface *intf)
{
	if (dmi_check_system(btusb_needs_reset_resume_table))
		interface_to_usbdev(intf)->quirks |= USB_QUIRK_RESET_RESUME;
}

static bool btusb_wakeup(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	return device_may_wakeup(&data->udev->dev);
}

static int btusb_shutdown_qca(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "HCI reset during shutdown failed");
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	return 0;
}

static ssize_t force_poll_sync_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct btusb_data *data = file->private_data;
	char buf[3];

	buf[0] = data->poll_sync ? 'Y' : 'N';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t force_poll_sync_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct btusb_data *data = file->private_data;
	bool enable;
	int err;

	err = kstrtobool_from_user(user_buf, count, &enable);
	if (err)
		return err;

	/* Only allow changes while the adapter is down */
	if (test_bit(HCI_UP, &data->hdev->flags))
		return -EPERM;

	if (data->poll_sync == enable)
		return -EALREADY;

	data->poll_sync = enable;

	return count;
}

static const struct file_operations force_poll_sync_fops = {
	.open		= simple_open,
	.read		= force_poll_sync_read,
	.write		= force_poll_sync_write,
	.llseek		= default_llseek,
};

static int btusb_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct gpio_desc *reset_gpio;
	struct btusb_data *data;
	struct hci_dev *hdev;
	unsigned ifnum_base;
	int i, err, priv_size;

	BT_DBG("intf %p id %p", intf, id);

	/* interface numbers are hardcoded in the spec */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 0) {
		if (!(id->driver_info & BTUSB_IFNUM_2))
			return -ENODEV;
		if (intf->cur_altsetting->desc.bInterfaceNumber != 2)
			return -ENODEV;
	}

	ifnum_base = intf->cur_altsetting->desc.bInterfaceNumber;

	if (!id->driver_info) {
		const struct usb_device_id *match;

		match = usb_match_id(intf, blacklist_table);
		if (match)
			id = match;
	}

	if (id->driver_info == BTUSB_IGNORE)
		return -ENODEV;

	if (id->driver_info & BTUSB_ATH3012) {
		struct usb_device *udev = interface_to_usbdev(intf);

		/* Old firmware would otherwise let ath3k driver load
		 * patch and sysconfig files
		 */
		if (le16_to_cpu(udev->descriptor.bcdDevice) <= 0x0001 &&
		    !btusb_qca_need_patch(udev))
			return -ENODEV;
	}

	data = devm_kzalloc(&intf->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
			data->intr_ep = ep_desc;
			continue;
		}

		if (!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			data->bulk_tx_ep = ep_desc;
			continue;
		}

		if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->bulk_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep)
		return -ENODEV;

	if (id->driver_info & BTUSB_AMP) {
		data->cmdreq_type = USB_TYPE_CLASS | 0x01;
		data->cmdreq = 0x2b;
	} else {
		data->cmdreq_type = USB_TYPE_CLASS;
		data->cmdreq = 0x00;
	}

	data->udev = interface_to_usbdev(intf);
	data->intf = intf;

	INIT_WORK(&data->work, btusb_work);
	INIT_WORK(&data->waker, btusb_waker);
	INIT_DELAYED_WORK(&data->rx_work, btusb_rx_work);

	skb_queue_head_init(&data->acl_q);

	init_usb_anchor(&data->deferred);
	init_usb_anchor(&data->tx_anchor);
	spin_lock_init(&data->txlock);

	init_usb_anchor(&data->intr_anchor);
	init_usb_anchor(&data->bulk_anchor);
	init_usb_anchor(&data->isoc_anchor);
	init_usb_anchor(&data->diag_anchor);
	init_usb_anchor(&data->ctrl_anchor);
	spin_lock_init(&data->rxlock);

	priv_size = 0;

	data->recv_event = hci_recv_frame;
	data->recv_bulk = btusb_recv_bulk;

	if (id->driver_info & BTUSB_INTEL_COMBINED) {
		/* Allocate extra space for Intel device */
		priv_size += sizeof(struct btintel_data);

		/* Override the rx handlers */
		data->recv_event = btusb_recv_event_intel;
		data->recv_bulk = btusb_recv_bulk_intel;
	}

	data->recv_acl = hci_recv_frame;

	hdev = hci_alloc_dev_priv(priv_size);
	if (!hdev)
		return -ENOMEM;

	hdev->bus = HCI_USB;
	hci_set_drvdata(hdev, data);

	if (id->driver_info & BTUSB_AMP)
		hdev->dev_type = HCI_AMP;
	else
		hdev->dev_type = HCI_PRIMARY;

	data->hdev = hdev;

	SET_HCIDEV_DEV(hdev, &intf->dev);

	reset_gpio = gpiod_get_optional(&data->udev->dev, "reset",
					GPIOD_OUT_LOW);
	if (IS_ERR(reset_gpio)) {
		err = PTR_ERR(reset_gpio);
		goto out_free_dev;
	} else if (reset_gpio) {
		data->reset_gpio = reset_gpio;
	}

	hdev->open   = btusb_open;
	hdev->close  = btusb_close;
	hdev->flush  = btusb_flush;
	hdev->send   = btusb_send_frame;
	hdev->notify = btusb_notify;
	hdev->wakeup = btusb_wakeup;

#ifdef CONFIG_PM
	err = btusb_config_oob_wake(hdev);
	if (err)
		goto out_free_dev;

	/* Marvell devices may need a specific chip configuration */
	if (id->driver_info & BTUSB_MARVELL && data->oob_wake_irq) {
		err = marvell_config_oob_wake(hdev);
		if (err)
			goto out_free_dev;
	}
#endif
	if (id->driver_info & BTUSB_CW6622)
		set_bit(HCI_QUIRK_BROKEN_STORED_LINK_KEY, &hdev->quirks);

	if (id->driver_info & BTUSB_BCM2045)
		set_bit(HCI_QUIRK_BROKEN_STORED_LINK_KEY, &hdev->quirks);

	if (id->driver_info & BTUSB_BCM92035)
		hdev->setup = btusb_setup_bcm92035;

	if (IS_ENABLED(CONFIG_BT_HCIBTUSB_BCM) &&
	    (id->driver_info & BTUSB_BCM_PATCHRAM)) {
		hdev->manufacturer = 15;
		hdev->setup = btbcm_setup_patchram;
		hdev->set_diag = btusb_bcm_set_diag;
		hdev->set_bdaddr = btbcm_set_bdaddr;

		/* Broadcom LM_DIAG Interface numbers are hardcoded */
		data->diag = usb_ifnum_to_if(data->udev, ifnum_base + 2);
	}

	if (IS_ENABLED(CONFIG_BT_HCIBTUSB_BCM) &&
	    (id->driver_info & BTUSB_BCM_APPLE)) {
		hdev->manufacturer = 15;
		hdev->setup = btbcm_setup_apple;
		hdev->set_diag = btusb_bcm_set_diag;

		/* Broadcom LM_DIAG Interface numbers are hardcoded */
		data->diag = usb_ifnum_to_if(data->udev, ifnum_base + 2);
	}

	/* Combined Intel Device setup to support multiple setup routine */
	if (id->driver_info & BTUSB_INTEL_COMBINED) {
		err = btintel_configure_setup(hdev);
		if (err)
			goto out_free_dev;

		/* Transport specific configuration */
		hdev->send = btusb_send_frame_intel;
		hdev->cmd_timeout = btusb_intel_cmd_timeout;

		if (id->driver_info & BTUSB_INTEL_NO_WBS_SUPPORT)
			btintel_set_flag(hdev, INTEL_ROM_LEGACY_NO_WBS_SUPPORT);

		if (id->driver_info & BTUSB_INTEL_BROKEN_INITIAL_NCMD)
			btintel_set_flag(hdev, INTEL_BROKEN_INITIAL_NCMD);

		if (id->driver_info & BTUSB_INTEL_BROKEN_SHUTDOWN_LED)
			btintel_set_flag(hdev, INTEL_BROKEN_SHUTDOWN_LED);
	}

	if (id->driver_info & BTUSB_MARVELL)
		hdev->set_bdaddr = btusb_set_bdaddr_marvell;

	if (IS_ENABLED(CONFIG_BT_HCIBTUSB_MTK) &&
	    (id->driver_info & BTUSB_MEDIATEK)) {
		hdev->setup = btusb_mtk_setup;
		hdev->shutdown = btusb_mtk_shutdown;
		hdev->manufacturer = 70;
		hdev->cmd_timeout = btusb_mtk_cmd_timeout;
		hdev->set_bdaddr = btmtk_set_bdaddr;
		set_bit(HCI_QUIRK_BROKEN_ENHANCED_SETUP_SYNC_CONN, &hdev->quirks);
		set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);
		data->recv_acl = btusb_recv_acl_mtk;
	}

	if (id->driver_info & BTUSB_SWAVE) {
		set_bit(HCI_QUIRK_FIXUP_INQUIRY_MODE, &hdev->quirks);
		set_bit(HCI_QUIRK_BROKEN_LOCAL_COMMANDS, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_INTEL_BOOT) {
		hdev->manufacturer = 2;
		set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_ATH3012) {
		data->setup_on_usb = btusb_setup_qca;
		hdev->set_bdaddr = btusb_set_bdaddr_ath3012;
		set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);
		set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_QCA_ROME) {
		data->setup_on_usb = btusb_setup_qca;
		hdev->shutdown = btusb_shutdown_qca;
		hdev->set_bdaddr = btusb_set_bdaddr_ath3012;
		hdev->cmd_timeout = btusb_qca_cmd_timeout;
		set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);
		btusb_check_needs_reset_resume(intf);
	}

	if (id->driver_info & BTUSB_QCA_WCN6855) {
		data->setup_on_usb = btusb_setup_qca;
		hdev->shutdown = btusb_shutdown_qca;
		hdev->set_bdaddr = btusb_set_bdaddr_wcn6855;
		hdev->cmd_timeout = btusb_qca_cmd_timeout;
		set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);
		hci_set_msft_opcode(hdev, 0xFD70);
	}

	if (id->driver_info & BTUSB_AMP) {
		/* AMP controllers do not support SCO packets */
		data->isoc = NULL;
	} else {
		/* Interface orders are hardcoded in the specification */
		data->isoc = usb_ifnum_to_if(data->udev, ifnum_base + 1);
		data->isoc_ifnum = ifnum_base + 1;
	}

	if (IS_ENABLED(CONFIG_BT_HCIBTUSB_RTL) &&
	    (id->driver_info & BTUSB_REALTEK)) {
		hdev->setup = btrtl_setup_realtek;
		hdev->shutdown = btrtl_shutdown_realtek;
		hdev->cmd_timeout = btusb_rtl_cmd_timeout;

		/* Realtek devices need to set remote wakeup on auto-suspend */
		set_bit(BTUSB_WAKEUP_AUTOSUSPEND, &data->flags);
		set_bit(BTUSB_USE_ALT3_FOR_WBS, &data->flags);
	}

	if (id->driver_info & BTUSB_ACTIONS_SEMI) {
		/* Support is advertised, but not implemented */
		set_bit(HCI_QUIRK_BROKEN_ERR_DATA_REPORTING, &hdev->quirks);
		set_bit(HCI_QUIRK_BROKEN_READ_TRANSMIT_POWER, &hdev->quirks);
		set_bit(HCI_QUIRK_BROKEN_SET_RPA_TIMEOUT, &hdev->quirks);
		set_bit(HCI_QUIRK_BROKEN_EXT_SCAN, &hdev->quirks);
	}

	if (!reset)
		set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

	if (force_scofix || id->driver_info & BTUSB_WRONG_SCO_MTU) {
		if (!disable_scofix)
			set_bit(HCI_QUIRK_FIXUP_BUFFER_SIZE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_BROKEN_ISOC)
		data->isoc = NULL;

	if (id->driver_info & BTUSB_WIDEBAND_SPEECH)
		set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED, &hdev->quirks);

	if (id->driver_info & BTUSB_VALID_LE_STATES)
		set_bit(HCI_QUIRK_VALID_LE_STATES, &hdev->quirks);

	if (id->driver_info & BTUSB_DIGIANSWER) {
		data->cmdreq_type = USB_TYPE_VENDOR;
		set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_CSR) {
		struct usb_device *udev = data->udev;
		u16 bcdDevice = le16_to_cpu(udev->descriptor.bcdDevice);

		/* Old firmware would otherwise execute USB reset */
		if (bcdDevice < 0x117)
			set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

		/* This must be set first in case we disable it for fakes */
		set_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks);

		/* Fake CSR devices with broken commands */
		if (le16_to_cpu(udev->descriptor.idVendor)  == 0x0a12 &&
		    le16_to_cpu(udev->descriptor.idProduct) == 0x0001)
			hdev->setup = btusb_setup_csr;
	}

	if (id->driver_info & BTUSB_SNIFFER) {
		struct usb_device *udev = data->udev;

		/* New sniffer firmware has crippled HCI interface */
		if (le16_to_cpu(udev->descriptor.bcdDevice) > 0x997)
			set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_INTEL_BOOT) {
		/* A bug in the bootloader causes that interrupt interface is
		 * only enabled after receiving SetInterface(0, AltSetting=0).
		 */
		err = usb_set_interface(data->udev, 0, 0);
		if (err < 0) {
			BT_ERR("failed to set interface 0, alt 0 %d", err);
			goto out_free_dev;
		}
	}

	if (data->isoc) {
		err = usb_driver_claim_interface(&btusb_driver,
						 data->isoc, data);
		if (err < 0)
			goto out_free_dev;
	}

	if (IS_ENABLED(CONFIG_BT_HCIBTUSB_BCM) && data->diag) {
		if (!usb_driver_claim_interface(&btusb_driver,
						data->diag, data))
			__set_diag_interface(hdev);
		else
			data->diag = NULL;
	}

	if (enable_autosuspend)
		usb_enable_autosuspend(data->udev);

	err = hci_register_dev(hdev);
	if (err < 0)
		goto out_free_dev;

	usb_set_intfdata(intf, data);

	debugfs_create_file("force_poll_sync", 0644, hdev->debugfs, data,
			    &force_poll_sync_fops);

	return 0;

out_free_dev:
	if (data->reset_gpio)
		gpiod_put(data->reset_gpio);
	hci_free_dev(hdev);
	return err;
}

static void btusb_disconnect(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev;

	BT_DBG("intf %p", intf);

	if (!data)
		return;

	hdev = data->hdev;
	usb_set_intfdata(data->intf, NULL);

	if (data->isoc)
		usb_set_intfdata(data->isoc, NULL);

	if (data->diag)
		usb_set_intfdata(data->diag, NULL);

	hci_unregister_dev(hdev);

	if (intf == data->intf) {
		if (data->isoc)
			usb_driver_release_interface(&btusb_driver, data->isoc);
		if (data->diag)
			usb_driver_release_interface(&btusb_driver, data->diag);
	} else if (intf == data->isoc) {
		if (data->diag)
			usb_driver_release_interface(&btusb_driver, data->diag);
		usb_driver_release_interface(&btusb_driver, data->intf);
	} else if (intf == data->diag) {
		usb_driver_release_interface(&btusb_driver, data->intf);
		if (data->isoc)
			usb_driver_release_interface(&btusb_driver, data->isoc);
	}

	if (data->oob_wake_irq)
		device_init_wakeup(&data->udev->dev, false);

	if (data->reset_gpio)
		gpiod_put(data->reset_gpio);

	hci_free_dev(hdev);
}

#ifdef CONFIG_PM
static int btusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct btusb_data *data = usb_get_intfdata(intf);

	BT_DBG("intf %p", intf);

	if (data->suspend_count++)
		return 0;

	spin_lock_irq(&data->txlock);
	if (!(PMSG_IS_AUTO(message) && data->tx_in_flight)) {
		set_bit(BTUSB_SUSPENDING, &data->flags);
		spin_unlock_irq(&data->txlock);
	} else {
		spin_unlock_irq(&data->txlock);
		data->suspend_count--;
		return -EBUSY;
	}

	cancel_work_sync(&data->work);

	btusb_stop_traffic(data);
	usb_kill_anchored_urbs(&data->tx_anchor);

	if (data->oob_wake_irq && device_may_wakeup(&data->udev->dev)) {
		set_bit(BTUSB_OOB_WAKE_ENABLED, &data->flags);
		enable_irq_wake(data->oob_wake_irq);
		enable_irq(data->oob_wake_irq);
	}

	/* For global suspend, Realtek devices lose the loaded fw
	 * in them. But for autosuspend, firmware should remain.
	 * Actually, it depends on whether the usb host sends
	 * set feature (enable wakeup) or not.
	 */
	if (test_bit(BTUSB_WAKEUP_AUTOSUSPEND, &data->flags)) {
		if (PMSG_IS_AUTO(message) &&
		    device_can_wakeup(&data->udev->dev))
			data->udev->do_remote_wakeup = 1;
		else if (!PMSG_IS_AUTO(message) &&
			 !device_may_wakeup(&data->udev->dev)) {
			data->udev->do_remote_wakeup = 0;
			data->udev->reset_resume = 1;
		}
	}

	return 0;
}

static void play_deferred(struct btusb_data *data)
{
	struct urb *urb;
	int err;

	while ((urb = usb_get_from_anchor(&data->deferred))) {
		usb_anchor_urb(urb, &data->tx_anchor);

		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0) {
			if (err != -EPERM && err != -ENODEV)
				BT_ERR("%s urb %p submission failed (%d)",
				       data->hdev->name, urb, -err);
			kfree(urb->setup_packet);
			usb_unanchor_urb(urb);
			usb_free_urb(urb);
			break;
		}

		data->tx_in_flight++;
		usb_free_urb(urb);
	}

	/* Cleanup the rest deferred urbs. */
	while ((urb = usb_get_from_anchor(&data->deferred))) {
		kfree(urb->setup_packet);
		usb_free_urb(urb);
	}
}

static int btusb_resume(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev = data->hdev;
	int err = 0;

	BT_DBG("intf %p", intf);

	if (--data->suspend_count)
		return 0;

	/* Disable only if not already disabled (keep it balanced) */
	if (test_and_clear_bit(BTUSB_OOB_WAKE_ENABLED, &data->flags)) {
		disable_irq(data->oob_wake_irq);
		disable_irq_wake(data->oob_wake_irq);
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (test_bit(BTUSB_INTR_RUNNING, &data->flags)) {
		err = btusb_submit_intr_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_INTR_RUNNING, &data->flags);
			goto failed;
		}
	}

	if (test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
		err = btusb_submit_bulk_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_BULK_RUNNING, &data->flags);
			goto failed;
		}

		btusb_submit_bulk_urb(hdev, GFP_NOIO);
	}

	if (test_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
		if (btusb_submit_isoc_urb(hdev, GFP_NOIO) < 0)
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		else
			btusb_submit_isoc_urb(hdev, GFP_NOIO);
	}

	spin_lock_irq(&data->txlock);
	play_deferred(data);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);
	schedule_work(&data->work);

	return 0;

failed:
	usb_scuttle_anchored_urbs(&data->deferred);
done:
	spin_lock_irq(&data->txlock);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);

	return err;
}
#endif

static struct usb_driver btusb_driver = {
	.name		= "btusb",
	.probe		= btusb_probe,
	.disconnect	= btusb_disconnect,
#ifdef CONFIG_PM
	.suspend	= btusb_suspend,
	.resume		= btusb_resume,
#endif
	.id_table	= btusb_table,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(btusb_driver);

module_param(disable_scofix, bool, 0644);
MODULE_PARM_DESC(disable_scofix, "Disable fixup of wrong SCO buffer size");

module_param(force_scofix, bool, 0644);
MODULE_PARM_DESC(force_scofix, "Force fixup of wrong SCO buffers size");

module_param(enable_autosuspend, bool, 0644);
MODULE_PARM_DESC(enable_autosuspend, "Enable USB autosuspend by default");

module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "Send HCI reset command on initialization");

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Generic Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
