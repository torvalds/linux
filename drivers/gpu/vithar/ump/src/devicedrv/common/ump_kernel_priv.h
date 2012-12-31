/*
 *
 * (C) COPYRIGHT 2008-2010, 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _UMP_KERNEL_PRIV_H_
#define _UMP_KERNEL_PRIV_H_

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/list.h>
#include <asm/cacheflush.h>
#endif


#define UMP_EXPECTED_IDS 64
#define UMP_MAX_IDS 32768

#ifdef __KERNEL__
/*TODO: for Linux Kernel Mainline Release to remove OSK define and dependencies below*/
#if defined(MALI_UNIT_TEST) && (MALI_UNIT_TEST)
#include <osk/mali_osk.h>
#define UMP_ASSERT OSK_ASSERT
#else
#define UMP_ASSERT(expr) \
		if (!(expr)) { \
			printk(KERN_ERR "UMP: Assertion failed! %s,%s,%s,line=%d\n",\
					#expr,__FILE__,__func__,__LINE__); \
					BUG(); \
		}
#endif

static inline void ump_sync_to_memory(uint64_t paddr, void* vaddr, size_t sz)
{
#ifdef CONFIG_ARM
	dmac_flush_range(vaddr, vaddr+sz-1);
	outer_flush_range(paddr, paddr+sz-1);
#elif defined(CONFIG_X86)
	struct scatterlist scl = {0, };
	sg_set_page(&scl, pfn_to_page(PFN_DOWN(paddr)), sz,
			paddr & (PAGE_SIZE -1 ));
	dma_sync_sg_for_cpu(NULL, &scl, 1, DMA_TO_DEVICE);
	mb(); /* for outer_sync (if needed) */
#else
#error Implement cache maintenance for your architecture here
#endif
}

static inline void ump_sync_to_cpu(uint64_t paddr, void* vaddr, size_t sz)
{
#ifdef CONFIG_ARM
	dmac_flush_range(vaddr, vaddr+sz-1);
	outer_flush_range(paddr, paddr+sz-1);
#elif defined(CONFIG_X86)
	struct scatterlist scl = {0, };
	sg_set_page(&scl, pfn_to_page(PFN_DOWN(paddr)), sz,
			paddr & (PAGE_SIZE -1 ));
	dma_sync_sg_for_cpu(NULL, &scl, 1, DMA_FROM_DEVICE);
#else
#error Implement cache maintenance for your architecture here
#endif
}
#endif /* __KERNEL__*/
#endif /* _UMP_KERNEL_PRIV_H_ */

