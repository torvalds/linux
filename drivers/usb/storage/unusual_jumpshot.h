/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Unusual Devices File for the Lexar "Jumpshot" Compact Flash reader
 */

#if defined(CONFIG_USB_STORAGE_JUMPSHOT) || \
		defined(CONFIG_USB_STORAGE_JUMPSHOT_MODULE)

UNUSUAL_DEV(  0x05dc, 0x0001, 0x0000, 0x0001,
		"Lexar",
		"Jumpshot USB CF Reader",
		USB_SC_SCSI, USB_PR_JUMPSHOT, NULL,
		US_FL_NEED_OVERRIDE),

#endif /* defined(CONFIG_USB_STORAGE_JUMPSHOT) || ... */
