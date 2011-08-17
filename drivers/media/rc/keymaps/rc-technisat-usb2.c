/* rc-technisat-usb2.c - Keytable for SkyStar HD USB
 *
 * Copyright (C) 2010 Patrick Boettcher,
 *                    Kernel Labs Inc. PO Box 745, St James, NY 11780
 *
 * Development was sponsored by Technisat Digital UK Limited, whose
 * registered office is Witan Gate House 500 - 600 Witan Gate West,
 * Milton Keynes, MK9 1SH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * THIS PROGRAM IS PROVIDED "AS IS" AND BOTH THE COPYRIGHT HOLDER AND
 * TECHNISAT DIGITAL UK LTD DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS PROGRAM INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY OR
 * FITNESS FOR A PARTICULAR PURPOSE.  NEITHER THE COPYRIGHT HOLDER
 * NOR TECHNISAT DIGITAL UK LIMITED SHALL BE LIABLE FOR ANY SPECIAL,
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS PROGRAM. See the
 * GNU General Public License for more details.
 */

#include <media/rc-map.h>

static struct rc_map_table technisat_usb2[] = {
	{0x0a0c, KEY_POWER},
	{0x0a01, KEY_1},
	{0x0a02, KEY_2},
	{0x0a03, KEY_3},
	{0x0a0d, KEY_MUTE},
	{0x0a04, KEY_4},
	{0x0a05, KEY_5},
	{0x0a06, KEY_6},
	{0x0a38, KEY_VIDEO}, /* EXT */
	{0x0a07, KEY_7},
	{0x0a08, KEY_8},
	{0x0a09, KEY_9},
	{0x0a00, KEY_0},
	{0x0a4f, KEY_INFO},
	{0x0a20, KEY_CHANNELUP},
	{0x0a52, KEY_MENU},
	{0x0a11, KEY_VOLUMEUP},
	{0x0a57, KEY_OK},
	{0x0a10, KEY_VOLUMEDOWN},
	{0x0a2f, KEY_EPG},
	{0x0a21, KEY_CHANNELDOWN},
	{0x0a22, KEY_REFRESH},
	{0x0a3c, KEY_TEXT},
	{0x0a76, KEY_ENTER}, /* HOOK */
	{0x0a0f, KEY_HELP},
	{0x0a6b, KEY_RED},
	{0x0a6c, KEY_GREEN},
	{0x0a6d, KEY_YELLOW},
	{0x0a6e, KEY_BLUE},
	{0x0a29, KEY_STOP},
	{0x0a23, KEY_LANGUAGE},
	{0x0a53, KEY_TV},
	{0x0a0a, KEY_PROGRAM},
};

static struct rc_map_list technisat_usb2_map = {
	.map = {
		.scan    = technisat_usb2,
		.size    = ARRAY_SIZE(technisat_usb2),
		.rc_type = RC_TYPE_RC5,
		.name    = RC_MAP_TECHNISAT_USB2,
	}
};

static int __init init_rc_map(void)
{
	return rc_map_register(&technisat_usb2_map);
}

static void __exit exit_rc_map(void)
{
	rc_map_unregister(&technisat_usb2_map);
}

module_init(init_rc_map)
module_exit(exit_rc_map)

MODULE_AUTHOR("Patrick Boettcher <pboettcher@kernellabs.com>");
MODULE_LICENSE("GPL");
