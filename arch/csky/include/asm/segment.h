/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_SEGMENT_H
#define __ASM_CSKY_SEGMENT_H

typedef struct {
	unsigned long seg;
} mm_segment_t;

#define KERNEL_DS		((mm_segment_t) { 0xFFFFFFFF })

#define USER_DS			((mm_segment_t) { PAGE_OFFSET })
#define get_fs()		(current_thread_info()->addr_limit)
#define set_fs(x)		(current_thread_info()->addr_limit = (x))
#define uaccess_kernel()	(get_fs().seg == KERNEL_DS.seg)

#endif /* __ASM_CSKY_SEGMENT_H */
