/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * This file contains debug allocation and free functions. These are turned on
 * by the configuration switch WANT_DEBUG_KMALLOC. This flags should be turned
 * off in the release version but facilitate memory leak and corruption during
 * development.
 * -----------------------------------------------------------------------------
 */
#include <linux/module.h>
#include "ozconfig.h"
#include "ozalloc.h"
#include "oztrace.h"
#ifdef WANT_DEBUG_KMALLOC
/*------------------------------------------------------------------------------
 */
#define MAGIC_1	0x12848796
#define MAGIC_2	0x87465920
#define MAGIC_3	0x80288264
/*------------------------------------------------------------------------------
 */
struct oz_alloc_hdr {
	int size;
	int line;
	unsigned magic;
	struct list_head link;
};
/*------------------------------------------------------------------------------
 */
static unsigned long g_total_alloc_size;
static int g_alloc_count;
static DEFINE_SPINLOCK(g_alloc_lock);
static LIST_HEAD(g_alloc_list);
/*------------------------------------------------------------------------------
 * Context: any
 */
void *oz_alloc_debug(size_t size, gfp_t flags, int line)
{
	struct oz_alloc_hdr *hdr = (struct oz_alloc_hdr *)
		kmalloc(size + sizeof(struct oz_alloc_hdr) +
			sizeof(unsigned), flags);
	if (hdr) {
		unsigned long irq_state;
		hdr->size = size;
		hdr->line = line;
		hdr->magic = MAGIC_1;
		*(unsigned *)(((u8 *)(hdr + 1)) + size) = MAGIC_2;
		spin_lock_irqsave(&g_alloc_lock, irq_state);
		g_total_alloc_size += size;
		g_alloc_count++;
		list_add_tail(&hdr->link, &g_alloc_list);
		spin_unlock_irqrestore(&g_alloc_lock, irq_state);
		return hdr + 1;
	}
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: any
 */
void oz_free_debug(void *p)
{
	if (p) {
		struct oz_alloc_hdr *hdr = (struct oz_alloc_hdr *)
			(((unsigned char *)p) - sizeof(struct oz_alloc_hdr));
		if (hdr->magic == MAGIC_1) {
			unsigned long irq_state;
			if (*(unsigned *)(((u8 *)(hdr + 1)) + hdr->size)
				!= MAGIC_2) {
				oz_trace("oz_free_debug: Corrupted beyond"
					" %p size %d\n", hdr+1, hdr->size);
				return;
			}
			spin_lock_irqsave(&g_alloc_lock, irq_state);
			g_total_alloc_size -= hdr->size;
			g_alloc_count--;
			list_del(&hdr->link);
			spin_unlock_irqrestore(&g_alloc_lock, irq_state);
			hdr->magic = MAGIC_3;
			kfree(hdr);
		} else {
			oz_trace("oz_free_debug: Invalid magic number %u\n",
				hdr->magic);
		}
	}
}
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_trace_leaks(void)
{
#ifdef WANT_TRACE
	struct list_head *e;
	oz_trace("Total alloc size:%ld  Alloc count:%d\n",
			g_total_alloc_size, g_alloc_count);
	if (g_alloc_count)
		oz_trace("Trace of leaks.\n");
	else
		oz_trace("No memory leaks.\n");
	list_for_each(e, &g_alloc_list) {
		struct oz_alloc_hdr *hdr =
			container_of(e, struct oz_alloc_hdr, link);
		oz_trace("LEAK size %d line %d\n", hdr->size, hdr->line);
	}
#endif /* #ifdef WANT_TRACE */
}
#endif /* #ifdef WANT_DEBUG_KMALLOC */

