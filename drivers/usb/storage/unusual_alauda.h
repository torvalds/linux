// SPDX-License-Identifier: GPL-2.0+
/*
 * Unusual Devices File for the Alauda-based card readers
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

#if defined(CONFIG_USB_STORAGE_ALAUDA) || \
		defined(CONFIG_USB_STORAGE_ALAUDA_MODULE)

UNUSUAL_DEV(  0x0584, 0x0008, 0x0102, 0x0102,
		"Fujifilm",
		"DPC-R1 (Alauda)",
		USB_SC_SCSI, USB_PR_ALAUDA, init_alauda, 0),

UNUSUAL_DEV(  0x07b4, 0x010a, 0x0102, 0x0102,
		"Olympus",
		"MAUSB-10 (Alauda)",
		USB_SC_SCSI, USB_PR_ALAUDA, init_alauda, 0),

#endif /* defined(CONFIG_USB_STORAGE_ALAUDA) || ... */
