/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZALLOC_H
#define _OZALLOC_H

#include <linux/slab.h>

#ifdef WANT_DEBUG_KMALLOC

void *oz_alloc_debug(size_t size, gfp_t flags, int line);
void oz_free_debug(void *p);
void oz_trace_leaks(void);
#define oz_alloc(__s, __f)	oz_alloc_debug(__s, __f, __LINE__)
#define oz_free			oz_free_debug

#else


#define oz_alloc	kmalloc
#define oz_free		kfree
#define oz_trace_leaks()

#endif /* #ifdef WANT_DEBUG_KMALLOC */

#endif /* _OZALLOC_H */
