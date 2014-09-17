/* Driver for USB Attached SCSI devices - Unusual Devices File
 *
 *   (c) 2013 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on the same file for the usb-storage driver, which is:
 *   (c) 2000-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *   (c) 2000 Adam J. Richter (adam@yggdrasil.com), Yggdrasil Computing, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * IMPORTANT NOTE: This file must be included in another file which defines
 * a UNUSUAL_DEV macro before this file is included.
 */

/*
 * If you edit this file, please try to keep it sorted first by VendorID,
 * then by ProductID.
 *
 * If you want to add an entry for this file, be sure to include the
 * following information:
 *	- a patch that adds the entry for your device, including your
 *	  email address right above the entry (plus maybe a brief
 *	  explanation of the reason for the entry),
 *	- lsusb -v output for the device
 * Send your submission to Hans de Goede <hdegoede@redhat.com>
 * and don't forget to CC: the USB development list <linux-usb@vger.kernel.org>
 */

/* https://bugzilla.kernel.org/show_bug.cgi?id=79511 */
UNUSUAL_DEV(0x0bc2, 0x2312, 0x0000, 0x9999,
		"Seagate",
		"Expansion Desk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_ATA_1X),

/* https://bbs.archlinux.org/viewtopic.php?id=183190 */
UNUSUAL_DEV(0x0bc2, 0x3312, 0x0000, 0x9999,
		"Seagate",
		"Expansion Desk",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_ATA_1X),

/* https://bbs.archlinux.org/viewtopic.php?id=183190 */
UNUSUAL_DEV(0x0bc2, 0xab20, 0x0000, 0x9999,
		"Seagate",
		"Backup+ BK",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_ATA_1X),

/* Reported-by: Claudio Bizzarri <claudio.bizzarri@gmail.com> */
UNUSUAL_DEV(0x152d, 0x0567, 0x0000, 0x9999,
		"JMicron",
		"JMS567",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL,
		US_FL_NO_REPORT_OPCODES),
