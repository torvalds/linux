/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_SEGMENT_H
#define __ASM_CSKY_SEGMENT_H

typedef struct {
	unsigned long seg;
} mm_segment_t;

#define KERNEL_DS		((mm_segment_t) { 0xFFFFFFFF })
#define get_ds()		KERNEL_DS

#define USER_DS			((mm_segment_t) { 0x80000000UL })
#define get_fs()		(current_thread_info()->addr_limit)
#define set_fs(x)		(current_thread_info()->addr_limit = (x))
#define segment_eq(a, b)	((a).seg == (b).seg)

#endif /* __ASM_CSKY_SEGMENT_H */
