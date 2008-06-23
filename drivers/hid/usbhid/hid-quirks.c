/*
 *  USB HID quirks support for Linux
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2007 Paul Walmsley
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>

#include "../hid-ids.h"

/*
 * Alphabetically sorted blacklist by quirk type.
 */

static const struct hid_blacklist {
	__u16 idVendor;
	__u16 idProduct;
	__u32 quirks;
} hid_blacklist[] = {

	{ USB_VENDOR_ID_A4TECH, USB_DEVICE_ID_A4TECH_WCP32PU, HID_QUIRK_2WHEEL_MOUSE_HACK_7 },
	{ USB_VENDOR_ID_A4TECH, USB_DEVICE_ID_A4TECH_X5_005D, HID_QUIRK_2WHEEL_MOUSE_HACK_B8 },

	{ USB_VENDOR_ID_AASHIMA, USB_DEVICE_ID_AASHIMA_GAMEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_AASHIMA, USB_DEVICE_ID_AASHIMA_PREDATOR, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_ALPS, USB_DEVICE_ID_IBM_GAMEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_CHIC, USB_DEVICE_ID_CHIC_GAMEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_GAMERON, USB_DEVICE_ID_GAMERON_DUAL_PSX_ADAPTOR, HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_DRIVING, HID_QUIRK_BADPAD | HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_FLYING, HID_QUIRK_BADPAD | HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_FIGHTING, HID_QUIRK_BADPAD | HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_NATSU, USB_DEVICE_ID_NATSU_GAMEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_NEC, USB_DEVICE_ID_NEC_USB_GAME_PAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_RUMBLEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_TOPMAX, USB_DEVICE_ID_TOPMAX_COBRAPAD, HID_QUIRK_BADPAD },

	{ USB_VENDOR_ID_AFATECH, USB_DEVICE_ID_AFATECH_AF9016, HID_QUIRK_FULLSPEED_INTERVAL },

	{ USB_VENDOR_ID_BELKIN, USB_DEVICE_ID_FLIP_KVM, HID_QUIRK_HIDDEV },
	{ USB_VENDOR_ID_SAMSUNG, USB_DEVICE_ID_SAMSUNG_IR_REMOTE, HID_QUIRK_HIDDEV | HID_QUIRK_IGNORE_HIDINPUT },

	{ USB_VENDOR_ID_EZKEY, USB_DEVICE_ID_BTC_8193, HID_QUIRK_HWHEEL_WHEEL_INVERT },

	{ USB_VENDOR_ID_PANTHERLORD, USB_DEVICE_ID_PANTHERLORD_TWIN_USB_JOYSTICK, HID_QUIRK_MULTI_INPUT | HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ USB_VENDOR_ID_PLAYDOTCOM, USB_DEVICE_ID_PLAYDOTCOM_EMS_USBII, HID_QUIRK_MULTI_INPUT },

	{ USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER, HID_QUIRK_SONY_PS3_CONTROLLER | HID_QUIRK_HIDDEV },

	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_UC100KM, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_CS124U, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_2PORTKVM, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_4PORTKVM, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_4PORTKVMC, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_DMI, USB_DEVICE_ID_DMI_ENC, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ELO, USB_DEVICE_ID_ELO_TS2700, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_PETALYNX, USB_DEVICE_ID_PETALYNX_MAXTER_REMOTE, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_SUN, USB_DEVICE_ID_RARITAN_KVM_DONGLE, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_TURBOX, USB_DEVICE_ID_TURBOX_KEYBOARD, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_DUAL_USB_JOYPAD, HID_QUIRK_NOGET | HID_QUIRK_MULTI_INPUT | HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_QUAD_USB_JOYPAD, HID_QUIRK_NOGET | HID_QUIRK_MULTI_INPUT },

	{ USB_VENDOR_ID_WISEGROUP_LTD, USB_DEVICE_ID_SMARTJOY_DUAL_PLUS, HID_QUIRK_NOGET | HID_QUIRK_MULTI_INPUT },

	{ USB_VENDOR_ID_DELL, USB_DEVICE_ID_DELL_W7658, HID_QUIRK_RESET_LEDS },

	{ 0, 0 }
};

/* Quirks for devices which require report descriptor fixup go here */
static const struct hid_rdesc_blacklist {
	__u16 idVendor;
	__u16 idProduct;
	__u32 quirks;
} hid_rdesc_blacklist[] = {

	{ USB_VENDOR_ID_CHERRY, USB_DEVICE_ID_CHERRY_CYMOTION, HID_QUIRK_RDESC_CYMOTION },

	{ USB_VENDOR_ID_MONTEREY, USB_DEVICE_ID_GENIUS_KB29E, HID_QUIRK_RDESC_BUTTON_CONSUMER },

	{ USB_VENDOR_ID_PETALYNX, USB_DEVICE_ID_PETALYNX_MAXTER_REMOTE, HID_QUIRK_RDESC_PETALYNX },

	{ USB_VENDOR_ID_SAMSUNG, USB_DEVICE_ID_SAMSUNG_IR_REMOTE, HID_QUIRK_RDESC_SAMSUNG_REMOTE },

	{ 0, 0 }
};

/* Dynamic HID quirks list - specified at runtime */
struct quirks_list_struct {
	struct hid_blacklist hid_bl_item;
	struct list_head node;
};

static LIST_HEAD(dquirks_list);
static DECLARE_RWSEM(dquirks_rwsem);

/* Runtime ("dynamic") quirks manipulation functions */

/**
 * usbhid_exists_dquirk: find any dynamic quirks for a USB HID device
 * @idVendor: the 16-bit USB vendor ID, in native byteorder
 * @idProduct: the 16-bit USB product ID, in native byteorder
 *
 * Description:
 *         Scans dquirks_list for a matching dynamic quirk and returns
 *         the pointer to the relevant struct hid_blacklist if found.
 *         Must be called with a read lock held on dquirks_rwsem.
 *
 * Returns: NULL if no quirk found, struct hid_blacklist * if found.
 */
static struct hid_blacklist *usbhid_exists_dquirk(const u16 idVendor,
		const u16 idProduct)
{
	struct quirks_list_struct *q;
	struct hid_blacklist *bl_entry = NULL;

	list_for_each_entry(q, &dquirks_list, node) {
		if (q->hid_bl_item.idVendor == idVendor &&
				q->hid_bl_item.idProduct == idProduct) {
			bl_entry = &q->hid_bl_item;
			break;
		}
	}

	if (bl_entry != NULL)
		dbg_hid("Found dynamic quirk 0x%x for USB HID vendor 0x%hx prod 0x%hx\n",
				bl_entry->quirks, bl_entry->idVendor,
				bl_entry->idProduct);

	return bl_entry;
}


/**
 * usbhid_modify_dquirk: add/replace a HID quirk
 * @idVendor: the 16-bit USB vendor ID, in native byteorder
 * @idProduct: the 16-bit USB product ID, in native byteorder
 * @quirks: the u32 quirks value to add/replace
 *
 * Description:
 *         If an dynamic quirk exists in memory for this (idVendor,
 *         idProduct) pair, replace its quirks value with what was
 *         provided.  Otherwise, add the quirk to the dynamic quirks list.
 *
 * Returns: 0 OK, -error on failure.
 */
static int usbhid_modify_dquirk(const u16 idVendor, const u16 idProduct,
				const u32 quirks)
{
	struct quirks_list_struct *q_new, *q;
	int list_edited = 0;

	if (!idVendor) {
		dbg_hid("Cannot add a quirk with idVendor = 0\n");
		return -EINVAL;
	}

	q_new = kmalloc(sizeof(struct quirks_list_struct), GFP_KERNEL);
	if (!q_new) {
		dbg_hid("Could not allocate quirks_list_struct\n");
		return -ENOMEM;
	}

	q_new->hid_bl_item.idVendor = idVendor;
	q_new->hid_bl_item.idProduct = idProduct;
	q_new->hid_bl_item.quirks = quirks;

	down_write(&dquirks_rwsem);

	list_for_each_entry(q, &dquirks_list, node) {

		if (q->hid_bl_item.idVendor == idVendor &&
				q->hid_bl_item.idProduct == idProduct) {

			list_replace(&q->node, &q_new->node);
			kfree(q);
			list_edited = 1;
			break;

		}

	}

	if (!list_edited)
		list_add_tail(&q_new->node, &dquirks_list);

	up_write(&dquirks_rwsem);

	return 0;
}

/**
 * usbhid_remove_all_dquirks: remove all runtime HID quirks from memory
 *
 * Description:
 *         Free all memory associated with dynamic quirks - called before
 *         module unload.
 *
 */
static void usbhid_remove_all_dquirks(void)
{
	struct quirks_list_struct *q, *temp;

	down_write(&dquirks_rwsem);
	list_for_each_entry_safe(q, temp, &dquirks_list, node) {
		list_del(&q->node);
		kfree(q);
	}
	up_write(&dquirks_rwsem);

}

/** 
 * usbhid_quirks_init: apply USB HID quirks specified at module load time
 */
int usbhid_quirks_init(char **quirks_param)
{
	u16 idVendor, idProduct;
	u32 quirks;
	int n = 0, m;

	for (; quirks_param[n] && n < MAX_USBHID_BOOT_QUIRKS; n++) {

		m = sscanf(quirks_param[n], "0x%hx:0x%hx:0x%x",
				&idVendor, &idProduct, &quirks);

		if (m != 3 ||
				usbhid_modify_dquirk(idVendor, idProduct, quirks) != 0) {
			printk(KERN_WARNING
					"Could not parse HID quirk module param %s\n",
					quirks_param[n]);
		}
	}

	return 0;
}

/**
 * usbhid_quirks_exit: release memory associated with dynamic_quirks
 *
 * Description:
 *     Release all memory associated with dynamic quirks.  Called upon
 *     module unload.
 *
 * Returns: nothing
 */
void usbhid_quirks_exit(void)
{
	usbhid_remove_all_dquirks();
}

/**
 * usbhid_exists_squirk: return any static quirks for a USB HID device
 * @idVendor: the 16-bit USB vendor ID, in native byteorder
 * @idProduct: the 16-bit USB product ID, in native byteorder
 *
 * Description:
 *     Given a USB vendor ID and product ID, return a pointer to
 *     the hid_blacklist entry associated with that device.
 *
 * Returns: pointer if quirk found, or NULL if no quirks found.
 */
static const struct hid_blacklist *usbhid_exists_squirk(const u16 idVendor,
		const u16 idProduct)
{
	const struct hid_blacklist *bl_entry = NULL;
	int n = 0;

	for (; hid_blacklist[n].idVendor; n++)
		if (hid_blacklist[n].idVendor == idVendor &&
				hid_blacklist[n].idProduct == idProduct)
			bl_entry = &hid_blacklist[n];

	if (bl_entry != NULL)
		dbg_hid("Found squirk 0x%x for USB HID vendor 0x%hx prod 0x%hx\n",
				bl_entry->quirks, bl_entry->idVendor, 
				bl_entry->idProduct);
	return bl_entry;
}

/**
 * usbhid_lookup_quirk: return any quirks associated with a USB HID device
 * @idVendor: the 16-bit USB vendor ID, in native byteorder
 * @idProduct: the 16-bit USB product ID, in native byteorder
 *
 * Description:
 *     Given a USB vendor ID and product ID, return any quirks associated
 *     with that device.
 *
 * Returns: a u32 quirks value.
 */
u32 usbhid_lookup_quirk(const u16 idVendor, const u16 idProduct)
{
	u32 quirks = 0;
	const struct hid_blacklist *bl_entry = NULL;

	/* NCR devices must not be queried for reports */
	if (idVendor == USB_VENDOR_ID_NCR &&
			idProduct >= USB_DEVICE_ID_NCR_FIRST &&
			idProduct <= USB_DEVICE_ID_NCR_LAST)
			return HID_QUIRK_NOGET;

	down_read(&dquirks_rwsem);
	bl_entry = usbhid_exists_dquirk(idVendor, idProduct);
	if (!bl_entry)
		bl_entry = usbhid_exists_squirk(idVendor, idProduct);
	if (bl_entry)
		quirks = bl_entry->quirks;
	up_read(&dquirks_rwsem);

	return quirks;
}

EXPORT_SYMBOL_GPL(usbhid_lookup_quirk);

/*
 * Cherry Cymotion keyboard have an invalid HID report descriptor,
 * that needs fixing before we can parse it.
 */
static void usbhid_fixup_cymotion_descriptor(char *rdesc, int rsize)
{
	if (rsize >= 17 && rdesc[11] == 0x3c && rdesc[12] == 0x02) {
		printk(KERN_INFO "Fixing up Cherry Cymotion report descriptor\n");
		rdesc[11] = rdesc[16] = 0xff;
		rdesc[12] = rdesc[17] = 0x03;
	}
}

/*
 * Samsung IrDA remote controller (reports as Cypress USB Mouse).
 *
 * Vendor specific report #4 has a size of 48 bit,
 * and therefore is not accepted when inspecting the descriptors.
 * As a workaround we reinterpret the report as:
 *   Variable type, count 6, size 8 bit, log. maximum 255
 * The burden to reconstruct the data is moved into user space.
 */
static void usbhid_fixup_samsung_irda_descriptor(unsigned char *rdesc,
						  int rsize)
{
	if (rsize >= 182 && rdesc[175] == 0x25
			 && rdesc[176] == 0x40
			 && rdesc[177] == 0x75
			 && rdesc[178] == 0x30
			 && rdesc[179] == 0x95
			 && rdesc[180] == 0x01
			 && rdesc[182] == 0x40) {
		printk(KERN_INFO "Fixing up Samsung IrDA report descriptor\n");
		rdesc[176] = 0xff;
		rdesc[178] = 0x08;
		rdesc[180] = 0x06;
		rdesc[182] = 0x42;
	}
}

/* Petalynx Maxter Remote has maximum for consumer page set too low */
static void usbhid_fixup_petalynx_descriptor(unsigned char *rdesc, int rsize)
{
	if (rsize >= 60 && rdesc[39] == 0x2a
			&& rdesc[40] == 0xf5
			&& rdesc[41] == 0x00
			&& rdesc[59] == 0x26
			&& rdesc[60] == 0xf9
			&& rdesc[61] == 0x00) {
		printk(KERN_INFO "Fixing up Petalynx Maxter Remote report descriptor\n");
		rdesc[60] = 0xfa;
		rdesc[40] = 0xfa;
	}
}

static void usbhid_fixup_button_consumer_descriptor(unsigned char *rdesc, int rsize)
{
	if (rsize >= 30 && rdesc[29] == 0x05
			&& rdesc[30] == 0x09) {
		printk(KERN_INFO "Fixing up button/consumer in HID report descriptor\n");
		rdesc[30] = 0x0c;
	}
}

static void __usbhid_fixup_report_descriptor(__u32 quirks, char *rdesc, unsigned rsize)
{
	if ((quirks & HID_QUIRK_RDESC_CYMOTION))
		usbhid_fixup_cymotion_descriptor(rdesc, rsize);

	if (quirks & HID_QUIRK_RDESC_PETALYNX)
		usbhid_fixup_petalynx_descriptor(rdesc, rsize);

	if (quirks & HID_QUIRK_RDESC_BUTTON_CONSUMER)
		usbhid_fixup_button_consumer_descriptor(rdesc, rsize);

	if (quirks & HID_QUIRK_RDESC_SAMSUNG_REMOTE)
		usbhid_fixup_samsung_irda_descriptor(rdesc, rsize);
}

/**
 * usbhid_fixup_report_descriptor: check if report descriptor needs fixup
 *
 * Description:
 *	Walks the hid_rdesc_blacklist[] array and checks whether the device
 *	is known to have broken report descriptor that needs to be fixed up
 *	prior to entering the HID parser
 *
 * Returns: nothing
 */
void usbhid_fixup_report_descriptor(const u16 idVendor, const u16 idProduct,
				    char *rdesc, unsigned rsize, char **quirks_param)
{
	int n, m;
	u16 paramVendor, paramProduct;
	u32 quirks;

	/* static rdesc quirk entries */
	for (n = 0; hid_rdesc_blacklist[n].idVendor; n++)
		if (hid_rdesc_blacklist[n].idVendor == idVendor &&
				hid_rdesc_blacklist[n].idProduct == idProduct)
			__usbhid_fixup_report_descriptor(hid_rdesc_blacklist[n].quirks,
					rdesc, rsize);

	/* runtime rdesc quirk entries handling */
	for (n = 0; quirks_param[n] && n < MAX_USBHID_BOOT_QUIRKS; n++) {
		m = sscanf(quirks_param[n], "0x%hx:0x%hx:0x%x",
				&paramVendor, &paramProduct, &quirks);

		if (m != 3)
			printk(KERN_WARNING
				"Could not parse HID quirk module param %s\n",
				quirks_param[n]);
		else if (paramVendor == idVendor && paramProduct == idProduct)
			__usbhid_fixup_report_descriptor(quirks, rdesc, rsize);
	}
}
