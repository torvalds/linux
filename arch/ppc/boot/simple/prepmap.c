/*
 * 2004 (C) IBM. This file is licensed under the terms of the GNU General
 * Public License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <nonstdio.h>

void board_isa_init(void)
{
	ISA_init(0x80000000);
}
