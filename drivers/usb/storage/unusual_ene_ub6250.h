// SPDX-License-Identifier: GPL-2.0+
/*
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

#if defined(CONFIG_USB_STORAGE_ENE_UB6250) || \
		defined(CONFIG_USB_STORAGE_ENE_UB6250_MODULE)

UNUSUAL_DEV(0x0cf2, 0x6250, 0x0000, 0x9999,
		"ENE",
		"ENE UB6250 reader",
		USB_SC_DEVICE, USB_PR_DEVICE, NULL, 0),

#endif /* defined(CONFIG_USB_STORAGE_ENE_UB6250) || ... */
