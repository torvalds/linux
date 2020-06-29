/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Unusual Devices File for the Datafab USB Compact Flash reader
 */

#if defined(CONFIG_USB_STORAGE_DATAFAB) || \
		defined(CONFIG_USB_STORAGE_DATAFAB_MODULE)

UNUSUAL_DEV(  0x07c4, 0xa000, 0x0000, 0x0015,
		"Datafab",
		"MDCFE-B USB CF Reader",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		0),

/*
 * The following Datafab-based devices may or may not work
 * using the current driver...the 0xffff is arbitrary since I
 * don't know what device versions exist for these guys.
 *
 * The 0xa003 and 0xa004 devices in particular I'm curious about.
 * I'm told they exist but so far nobody has come forward to say that
 * they work with this driver.  Given the success we've had getting
 * other Datafab-based cards operational with this driver, I've decided
 * to leave these two devices in the list.
 */
UNUSUAL_DEV( 0x07c4, 0xa001, 0x0000, 0xffff,
		"SIIG/Datafab",
		"SIIG/Datafab Memory Stick+CF Reader/Writer",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		0),

/* Reported by Josef Reisinger <josef.reisinger@netcologne.de> */
UNUSUAL_DEV( 0x07c4, 0xa002, 0x0000, 0xffff,
		"Datafab/Unknown",
		"MD2/MD3 Disk enclosure",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		US_FL_SINGLE_LUN),

UNUSUAL_DEV( 0x07c4, 0xa003, 0x0000, 0xffff,
		"Datafab/Unknown",
		"Datafab-based Reader",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		0),

UNUSUAL_DEV( 0x07c4, 0xa004, 0x0000, 0xffff,
		"Datafab/Unknown",
		"Datafab-based Reader",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		0),

UNUSUAL_DEV( 0x07c4, 0xa005, 0x0000, 0xffff,
		"PNY/Datafab",
		"PNY/Datafab CF+SM Reader",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		0),

UNUSUAL_DEV( 0x07c4, 0xa006, 0x0000, 0xffff,
		"Simple Tech/Datafab",
		"Simple Tech/Datafab CF+SM Reader",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		0),

/* Submitted by Olaf Hering <olh@suse.de> */
UNUSUAL_DEV(  0x07c4, 0xa109, 0x0000, 0xffff,
		"Datafab Systems, Inc.",
		"USB to CF + SM Combo (LC1)",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		0),

/*
 * Reported by Felix Moeller <felix@derklecks.de>
 * in Germany this is sold by Hama with the productnumber 46952
 * as "DualSlot CompactFlash(TM) & MStick Drive USB"
 */
UNUSUAL_DEV(  0x07c4, 0xa10b, 0x0000, 0xffff,
		"DataFab Systems Inc.",
		"USB CF+MS",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		0),

UNUSUAL_DEV( 0x0c0b, 0xa109, 0x0000, 0xffff,
		"Acomdata",
		"CF",
		USB_SC_SCSI, USB_PR_DATAFAB, NULL,
		US_FL_SINGLE_LUN),

#endif /* defined(CONFIG_USB_STORAGE_DATAFAB) || ... */
