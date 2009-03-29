/* Unusual Devices File for SCM Microsystems (a.k.a. Shuttle) USB-ATAPI cable
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

#if defined(CONFIG_USB_STORAGE_USBAT) || \
		defined(CONFIG_USB_STORAGE_USBAT_MODULE)

UNUSUAL_DEV(  0x03f0, 0x0207, 0x0001, 0x0001,
		"HP",
		"CD-Writer+ 8200e",
		US_SC_8070, US_PR_USBAT, init_usbat_cd, 0),

UNUSUAL_DEV(  0x03f0, 0x0307, 0x0001, 0x0001,
		"HP",
		"CD-Writer+ CD-4e",
		US_SC_8070, US_PR_USBAT, init_usbat_cd, 0),

UNUSUAL_DEV(  0x04e6, 0x1010, 0x0000, 0x9999,
		"Shuttle/SCM",
		"USBAT-02",
		US_SC_SCSI, US_PR_USBAT, init_usbat_flash,
		US_FL_SINGLE_LUN),

UNUSUAL_DEV(  0x0781, 0x0005, 0x0005, 0x0005,
		"Sandisk",
		"ImageMate SDDR-05b",
		US_SC_SCSI, US_PR_USBAT, init_usbat_flash,
		US_FL_SINGLE_LUN),

#endif /* defined(CONFIG_USB_STORAGE_USBAT) || ... */
