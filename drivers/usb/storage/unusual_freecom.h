/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Unusual Devices File for the Freecom USB/IDE adaptor
 */

#if defined(CONFIG_USB_STORAGE_FREECOM) || \
		defined(CONFIG_USB_STORAGE_FREECOM_MODULE)

UNUSUAL_DEV(  0x07ab, 0xfc01, 0x0000, 0x9999,
		"Freecom",
		"USB-IDE",
		USB_SC_QIC, USB_PR_FREECOM, init_freecom, 0),

#endif /* defined(CONFIG_USB_STORAGE_FREECOM) || ... */
