/* Unusual Devices File for SanDisk SDDR-09 SmartMedia reader
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

#if defined(CONFIG_USB_STORAGE_SDDR09) || \
		defined(CONFIG_USB_STORAGE_SDDR09_MODULE)

UNUSUAL_DEV(  0x0436, 0x0005, 0x0100, 0x0100,
		"Microtech",
		"CameraMate (DPCM_USB)",
		USB_SC_SCSI, USB_PR_DPCM_USB, NULL, 0),

UNUSUAL_DEV(  0x04e6, 0x0003, 0x0000, 0x9999,
		"Sandisk",
		"ImageMate SDDR09",
		USB_SC_SCSI, USB_PR_EUSB_SDDR09, usb_stor_sddr09_init,
		0),

/* This entry is from Andries.Brouwer@cwi.nl */
UNUSUAL_DEV(  0x04e6, 0x0005, 0x0100, 0x0208,
		"SCM Microsystems",
		"eUSB SmartMedia / CompactFlash Adapter",
		USB_SC_SCSI, USB_PR_DPCM_USB, usb_stor_sddr09_dpcm_init,
		0),

UNUSUAL_DEV(  0x066b, 0x0105, 0x0100, 0x0100,
		"Olympus",
		"Camedia MAUSB-2",
		USB_SC_SCSI, USB_PR_EUSB_SDDR09, usb_stor_sddr09_init,
		0),

UNUSUAL_DEV(  0x0781, 0x0200, 0x0000, 0x9999,
		"Sandisk",
		"ImageMate SDDR-09",
		USB_SC_SCSI, USB_PR_EUSB_SDDR09, usb_stor_sddr09_init,
		0),

UNUSUAL_DEV(  0x07af, 0x0006, 0x0100, 0x0100,
		"Microtech",
		"CameraMate (DPCM_USB)",
		USB_SC_SCSI, USB_PR_DPCM_USB, NULL, 0),

#endif /* defined(CONFIG_USB_STORAGE_SDDR09) || ... */
