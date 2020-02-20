/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/page.h>

OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
	. = PAGE_OFFSET;

	.payload : {
		*(.payload)
		. = ALIGN(8);
	}
}
