/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IOBITMAP_H
#define _ASM_X86_IOBITMAP_H

#include <linux/refcount.h>
#include <asm/processor.h>

struct io_bitmap {
	u64		sequence;
	refcount_t	refcnt;
	/* The maximum number of bytes to copy so all zero bits are covered */
	unsigned int	max;
	unsigned long	bitmap[IO_BITMAP_LONGS];
};

struct task_struct;

#ifdef CONFIG_X86_IOPL_IOPERM
void io_bitmap_share(struct task_struct *tsk);
void io_bitmap_exit(void);

void tss_update_io_bitmap(void);
#else
static inline void io_bitmap_share(struct task_struct *tsk) { }
static inline void io_bitmap_exit(void) { }
static inline void tss_update_io_bitmap(void) { }
#endif

#endif
