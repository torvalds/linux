/*
 *      Copyright (C) 1995-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-buffers.c,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:59 $
 *
 *      This file contains the dynamic buffer allocation routines 
 *      of zftape
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/zftape.h>

#include <linux/vmalloc.h>

#include "../zftape/zftape-init.h"
#include "../zftape/zftape-eof.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-write.h"
#include "../zftape/zftape-read.h"
#include "../zftape/zftape-rw.h"
#include "../zftape/zftape-vtbl.h"

/*  global variables
 */

/*  local varibales
 */
static unsigned int used_memory;
static unsigned int peak_memory;

void zft_memory_stats(void)
{
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Memory usage (vmalloc allocations):\n"
	      KERN_INFO "total allocated: %d\n"
	      KERN_INFO "peak allocation: %d",
	      used_memory, peak_memory);
	peak_memory = used_memory;
	TRACE_EXIT;
}

int zft_vcalloc_once(void *new, size_t size)
{
	TRACE_FUN(ft_t_flow);
	if (zft_vmalloc_once(new, size) < 0) {
		TRACE_EXIT -ENOMEM;
	}
	memset(*(void **)new, '\0', size);
	TRACE_EXIT 0;
}
int zft_vmalloc_once(void *new, size_t size)
{
	TRACE_FUN(ft_t_flow);

	if (*(void **)new != NULL || size == 0) {
		TRACE_EXIT 0;
	}
	if ((*(void **)new = vmalloc(size)) == NULL) {
		TRACE_EXIT -ENOMEM;
	}
	used_memory += size;
	if (peak_memory < used_memory) {
		peak_memory = used_memory;
	}
	TRACE_ABORT(0, ft_t_noise,
		    "allocated buffer @ %p, %d bytes", *(void **)new, size);
}
int zft_vmalloc_always(void *new, size_t size)
{
	TRACE_FUN(ft_t_flow);

	zft_vfree(new, size);
	TRACE_EXIT zft_vmalloc_once(new, size);
}
void zft_vfree(void *old, size_t size)
{
	TRACE_FUN(ft_t_flow);

	if (*(void **)old) {
		vfree(*(void **)old);
		used_memory -= size;
		TRACE(ft_t_noise, "released buffer @ %p, %d bytes",
		      *(void **)old, size);
		*(void **)old = NULL;
	}
	TRACE_EXIT;
}

void *zft_kmalloc(size_t size)
{
	void *new;

	while ((new = kmalloc(size, GFP_KERNEL)) == NULL) {
		msleep_interruptible(100);
	}
	memset(new, 0, size);
	used_memory += size;
	if (peak_memory < used_memory) {
		peak_memory = used_memory;
	}
	return new;
}

void zft_kfree(void *old, size_t size)
{
	kfree(old);
	used_memory -= size;
}

/* there are some more buffers that are allocated on demand.
 * cleanup_module() calles this function to be sure to have released
 * them 
 */
void zft_uninit_mem(void)
{
	TRACE_FUN(ft_t_flow);

	zft_vfree(&zft_hseg_buf, FT_SEGMENT_SIZE);
	zft_vfree(&zft_deblock_buf, FT_SEGMENT_SIZE); zft_deblock_segment = -1;
	zft_free_vtbl();
	if (zft_cmpr_lock(0 /* don't load */) == 0) {
		(*zft_cmpr_ops->cleanup)();
		(*zft_cmpr_ops->reset)(); /* unlock it again */
	}
	zft_memory_stats();
	TRACE_EXIT;
}
