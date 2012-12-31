/*
 *
 * (C) COPYRIGHT 2010 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_mem_linux.h
 * Base kernel memory APIs, Linux implementation.
 */

#ifndef _KBASE_MEM_LINUX_H_
#define _KBASE_MEM_LINUX_H_

struct kbase_va_region *kbase_pmem_alloc(struct kbase_context *kctx, u32 size,
					 u32 flags, u16 *pmem_cookie);
int kbase_mmap(struct file *file, struct vm_area_struct *vma);

#endif /* _KBASE_MEM_LINUX_H_ */
