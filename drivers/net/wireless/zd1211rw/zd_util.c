/* zd_util.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Utility program
 */

#include "zd_def.h"
#include "zd_util.h"

#ifdef DEBUG
static char hex(u8 v)
{
	v &= 0xf;
	return (v < 10 ? '0' : 'a' - 10) + v;
}

static char hex_print(u8 c)
{
	return (0x20 <= c && c < 0x7f) ? c : '.';
}

static void dump_line(const u8 *bytes, size_t size)
{
	char c;
	size_t i;

	size = size <= 8 ? size : 8;
	printk(KERN_DEBUG "zd1211 %p ", bytes);
	for (i = 0; i < 8; i++) {
		switch (i) {
		case 1:
		case 5:
			c = '.';
			break;
		case 3:
			c = ':';
			break;
		default:
			c = ' ';
		}
		if (i < size) {
			printk("%c%c%c", hex(bytes[i] >> 4), hex(bytes[i]), c);
		} else {
			printk("  %c", c);
		}
	}

	for (i = 0; i < size; i++)
		printk("%c", hex_print(bytes[i]));
	printk("\n");
}

void zd_hexdump(const void *bytes, size_t size)
{
	size_t i = 0;

	do {
		dump_line((u8 *)bytes + i, size-i);
		i += 8;
	} while (i < size);
}
#endif /* DEBUG */

void *zd_tail(const void *buffer, size_t buffer_size, size_t tail_size)
{
	if (buffer_size < tail_size)
		return NULL;
	return (u8 *)buffer + (buffer_size - tail_size);
}
