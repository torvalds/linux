/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IOBITMAP_H
#define _ASM_X86_IOBITMAP_H

#include <asm/processor.h>

struct io_bitmap {
	u64		sequence;
	/* The maximum number of bytes to copy so all zero bits are covered */
	unsigned int	max;
	unsigned long	bitmap[IO_BITMAP_LONGS];
};

void io_bitmap_exit(void);

void tss_update_io_bitmap(void);

#endif
