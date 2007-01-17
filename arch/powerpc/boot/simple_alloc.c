/*
 * Implement primitive realloc(3) functionality.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2006 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <stddef.h>
#include "types.h"
#include "page.h"
#include "string.h"
#include "ops.h"

#define	ENTRY_BEEN_USED	0x01
#define	ENTRY_IN_USE	0x02

static struct alloc_info {
	u32	flags;
	u32	base;
	u32	size;
} *alloc_tbl;

static u32 tbl_entries;
static u32 alloc_min;
static u32 next_base;
static u32 space_left;

/*
 * First time an entry is used, its base and size are set.
 * An entry can be freed and re-malloc'd but its base & size don't change.
 * Should be smart enough for needs of bootwrapper.
 */
static void *simple_malloc(u32 size)
{
	u32 i;
	struct alloc_info *p = alloc_tbl;

	if (size == 0)
		goto err_out;

	size = _ALIGN_UP(size, alloc_min);

	for (i=0; i<tbl_entries; i++, p++)
		if (!(p->flags & ENTRY_BEEN_USED)) { /* never been used */
			if (size <= space_left) {
				p->base = next_base;
				p->size = size;
				p->flags = ENTRY_BEEN_USED | ENTRY_IN_USE;
				next_base += size;
				space_left -= size;
				return (void *)p->base;
			}
			goto err_out; /* not enough space left */
		}
		/* reuse an entry keeping same base & size */
		else if (!(p->flags & ENTRY_IN_USE) && (size <= p->size)) {
			p->flags |= ENTRY_IN_USE;
			return (void *)p->base;
		}
err_out:
	return NULL;
}

static struct alloc_info *simple_find_entry(void *ptr)
{
	u32 i;
	struct alloc_info *p = alloc_tbl;

	for (i=0; i<tbl_entries; i++,p++) {
		if (!(p->flags & ENTRY_BEEN_USED))
			break;
		if ((p->flags & ENTRY_IN_USE) && (p->base == (u32)ptr))
			return p;
	}
	return NULL;
}

static void simple_free(void *ptr)
{
	struct alloc_info *p = simple_find_entry(ptr);

	if (p != NULL)
		p->flags &= ~ENTRY_IN_USE;
}

/*
 * Change size of area pointed to by 'ptr' to 'size'.
 * If 'ptr' is NULL, then its a malloc().  If 'size' is 0, then its a free().
 * 'ptr' must be NULL or a pointer to a non-freed area previously returned by
 * simple_realloc() or simple_malloc().
 */
static void *simple_realloc(void *ptr, unsigned long size)
{
	struct alloc_info *p;
	void *new;

	if (size == 0) {
		simple_free(ptr);
		return NULL;
	}

	if (ptr == NULL)
		return simple_malloc(size);

	p = simple_find_entry(ptr);
	if (p == NULL) /* ptr not from simple_malloc/simple_realloc */
		return NULL;
	if (size <= p->size) /* fits in current block */
		return ptr;

	new = simple_malloc(size);
	memcpy(new, ptr, p->size);
	simple_free(ptr);
	return new;
}

/*
 * Returns addr of first byte after heap so caller can see if it took
 * too much space.  If so, change args & try again.
 */
void *simple_alloc_init(char *base, u32 heap_size, u32 granularity,
		u32 max_allocs)
{
	u32 heap_base, tbl_size;

	heap_size = _ALIGN_UP(heap_size, granularity);
	alloc_min = granularity;
	tbl_entries = max_allocs;

	tbl_size = tbl_entries * sizeof(struct alloc_info);

	alloc_tbl = (struct alloc_info *)_ALIGN_UP((unsigned long)base, 8);
	memset(alloc_tbl, 0, tbl_size);

	heap_base = _ALIGN_UP((u32)alloc_tbl + tbl_size, alloc_min);

	next_base = heap_base;
	space_left = heap_size;

	platform_ops.malloc = simple_malloc;
	platform_ops.free = simple_free;
	platform_ops.realloc = simple_realloc;

	return (void *)(heap_base + heap_size);
}
