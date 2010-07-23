/*

Copyright 1996,2002,2005 Gregory D. Hager, Alfred A. Rizzi, Noah J. Cowan,
				Jason Lapenta, Scott Smedley, Greg Sharp

This file is part of the DT3155 Device Driver.

The DT3155 Device Driver is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The DT3155 Device Driver is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the DT3155 Device Driver; if not, write to the Free
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
MA 02111-1307 USA

   File: dt3155_isr.c
Purpose: Buffer management routines, and other routines for the ISR
		(the actual isr is in dt3155_drv.c)

-- Changes --

  Date       Programmer  Description of changes made
  -------------------------------------------------------------------
  03-Jul-2000 JML       n/a
  02-Apr-2002 SS        Mods to make work with separate allocator
			module; Merged John Roll's mods to make work with
			multiple boards.
  10-Jul-2002 GCS       Complete rewrite of setup_buffers to disallow
			buffers which span a 4MB boundary.
  24-Jul-2002 SS        GPL licence.
  30-Jul-2002 NJC       Added support for buffer loop.
  31-Jul-2002 NJC       Complete rewrite of buffer management
  02-Aug-2002 NJC       Including slab.h instead of malloc.h (no warning).
			Also, allocator_init() now returns allocator_max
			so cleaned up allocate_buffers() accordingly.
  08-Aug-2005 SS        port to 2.6 kernel.

*/

#include <asm/system.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/types.h>

#include "dt3155.h"
#include "dt3155_drv.h"
#include "dt3155_io.h"
#include "dt3155_isr.h"
#include "allocator.h"

#define FOUR_MB         (0x0400000)  /* Can't DMA accross a 4MB boundary!*/
#define UPPER_10_BITS   (0x3FF<<22)  /* Can't DMA accross a 4MB boundary!*/


/******************************************************************************
 * Simple array based que struct
 *
 * Some handy functions using the buffering structure.
 *****************************************************************************/

/***************************
 * are_empty_buffers
 ***************************/
bool are_empty_buffers(struct dt3155_fbuffer *fb)
{
	return fb->empty_len;
}

/**************************
 * push_empty
 *
 * This is slightly confusing.  The number empty_len is the literal #
 * of empty buffers.  After calling, empty_len-1 is the index into the
 * empty buffer stack.  So, if empty_len == 1, there is one empty buffer,
 * given by fb->empty_buffers[0].
 * empty_buffers should never fill up, though this is not checked.
 **************************/
void push_empty(struct dt3155_fbuffer *fb, int index)
{
	fb->empty_buffers[fb->empty_len] = index;
	fb->empty_len++;
}

/**************************
 * pop_empty
 **************************/
int pop_empty(struct dt3155_fbuffer *fb)
{
	fb->empty_len--;
	return fb->empty_buffers[fb->empty_len];
}

/*************************
 * is_ready_buf_empty
 *************************/
bool is_ready_buf_empty(struct dt3155_fbuffer *fb)
{
	return fb->ready_len == 0;
}

/*************************
 * is_ready_buf_full
 *
 * this should *never* be true if there are any active, locked or empty
 * buffers, since it corresponds to nbuffers ready buffers!!
 * 7/31/02: total rewrite. --NJC
 *************************/
bool is_ready_buf_full(struct dt3155_fbuffer *fb)
{
	return fb->ready_len == fb->nbuffers;
}

/*****************************************************
 * push_ready
 *****************************************************/
void push_ready(struct dt3155_fbuffer *fb, int index)
{
	int head = fb->ready_head;

	fb->ready_que[head] = index;
	fb->ready_head = (head + 1) % fb->nbuffers;
	fb->ready_len++;
}

/*****************************************************
 * get_tail
 *
 * Simply comptutes the tail given the head and the length.
 *****************************************************/
static int get_tail(struct dt3155_fbuffer *fb)
{
	return (fb->ready_head - fb->ready_len + fb->nbuffers) % fb->nbuffers;
}

/*****************************************************
 * pop_ready
 *
 * This assumes that there is a ready buffer ready... should
 * be checked (e.g. with is_ready_buf_empty()  prior to call.
 *****************************************************/
int pop_ready(struct dt3155_fbuffer *fb)
{
	int tail = get_tail(fb);

	fb->ready_len--;
	return fb->ready_que[tail];
}

/*****************************************************
 * printques
 *****************************************************/
void printques(struct dt3155_fbuffer *fb)
{
	int i;

	printk(KERN_INFO "\n R:");
	for (i = get_tail(fb); i != fb->ready_head; i++, i %= fb->nbuffers)
		printk(" %d ", fb->ready_que[i]);

	printk(KERN_INFO "\n E:");
	for (i = 0; i < fb->empty_len; i++)
		printk(" %d ", fb->empty_buffers[i]);

	printk(KERN_INFO "\n A: %d", fb->active_buf);

	printk(KERN_INFO "\n L: %d\n", fb->locked_buf);
}

/*****************************************************
 * adjust_4MB
 *
 *  If a buffer intersects the 4MB boundary, push
 *  the start address up to the beginning of the
 *  next 4MB chunk (assuming bufsize < 4MB).
 *****************************************************/
static u32 adjust_4MB(u32 buf_addr, u32 bufsize)
{
    if (((buf_addr+bufsize) & UPPER_10_BITS) != (buf_addr & UPPER_10_BITS))
	return (buf_addr+bufsize) & UPPER_10_BITS;
    else
	return buf_addr;
}


/*****************************************************
 * allocate_buffers
 *
 *  Try to allocate enough memory for all requested
 *  buffers.  If there is not enough free space
 *  try for less memory.
 *****************************************************/
static void allocate_buffers(u32 *buf_addr, u32* total_size_kbs,
		       u32 bufsize)
{
  /* Compute the minimum amount of memory guaranteed to hold all
     MAXBUFFERS such that no buffer crosses the 4MB boundary.
     Store this value in the variable "full_size" */

  u32 allocator_max;
  u32 bufs_per_chunk = (FOUR_MB / bufsize);
  u32 filled_chunks = (MAXBUFFERS-1) / bufs_per_chunk;
  u32 leftover_bufs = MAXBUFFERS - filled_chunks * bufs_per_chunk;

  u32 full_size = bufsize      /* possibly unusable part of 1st chunk */
    + filled_chunks * FOUR_MB   /* max # of completely filled 4mb chunks */
    + leftover_bufs * bufsize;  /* these buffs will be in a partly filled
				   chunk at beginning or end */

  u32 full_size_kbs = 1 + (full_size-1) / 1024;
  u32 min_size_kbs = 2*ndevices*bufsize / 1024;
  u32 size_kbs;

  /* Now, try to allocate full_size.  If this fails, keep trying for
     less & less memory until it succeeds. */
#ifndef STANDALONE_ALLOCATOR
  /* initialize the allocator            */
  allocator_init(&allocator_max);
#endif
  size_kbs = full_size_kbs;
  *buf_addr = 0;
  printk(KERN_INFO "DT3155: We would like to get: %d KB\n", full_size_kbs);
  printk(KERN_INFO "DT3155: ...but need at least: %d KB\n", min_size_kbs);
  printk(KERN_INFO "DT3155: ...the allocator has: %d KB\n", allocator_max);
  size_kbs = (full_size_kbs <= allocator_max ? full_size_kbs : allocator_max);
    if (size_kbs > min_size_kbs) {
		*buf_addr = allocator_allocate_dma(size_kbs, GFP_KERNEL);
	if (*buf_addr != 0) {
		printk(KERN_INFO "DT3155:  Managed to allocate: %d KB\n",
				size_kbs);
		*total_size_kbs = size_kbs;
		return;
	}
    }
  /* If we got here, the allocation failed */
  printk(KERN_INFO "DT3155: Allocator failed!\n");
  *buf_addr = 0;
  *total_size_kbs = 0;
  return;

}


/*****************************************************
 * dt3155_setup_buffers
 *
 *  setup_buffers just puts the buffering system into
 *  a consistent state before the start of interrupts
 *
 * JML : it looks like all the buffers need to be
 * continuous. So I'm going to try and allocate one
 * continuous buffer.
 *
 * GCS : Fix DMA problems when buffer spans
 * 4MB boundary.  Also, add error checking.  This
 * function will return -ENOMEM when not enough memory.
 *****************************************************/
u32 dt3155_setup_buffers(u32 *allocatorAddr)

{
  struct dt3155_fbuffer *fb;
  u32 index;
  u32 rambuff_addr; /* start of allocation */
  u32 rambuff_size; /* total size allocated to driver */
  u32 rambuff_acm;  /* accumlator, keep track of how much
			  is left after being split up*/
  u32 rambuff_end;  /* end of rambuff */
  u32 numbufs;      /* number of useful buffers allocated (per device) */
  u32 bufsize      = DT3155_MAX_ROWS * DT3155_MAX_COLS;
  int minor;

  /* zero the fbuffer status and address structure */
    for (minor = 0; minor < ndevices; minor++) {
	fb = &dt3155_status[minor].fbuffer;
	memset(fb, 0, sizeof(*fb));
    }

  /* allocate a large contiguous chunk of RAM */
  allocate_buffers(&rambuff_addr, &rambuff_size, bufsize);
  printk(KERN_INFO "DT3155: mem info\n");
  printk(KERN_INFO "  - rambuf_addr = 0x%x\n", rambuff_addr);
  printk(KERN_INFO "  - length (kb) = %u\n", rambuff_size);
    if (rambuff_addr == 0) {
	printk(KERN_INFO
	    "DT3155: Error setup_buffers() allocator dma failed\n");
	return -ENOMEM;
    }
  *allocatorAddr = rambuff_addr;
  rambuff_end = rambuff_addr + 1024 * rambuff_size;

  /* after allocation, we need to count how many useful buffers there
     are so we can give an equal number to each device */
  rambuff_acm = rambuff_addr;
    for (index = 0; index < MAXBUFFERS; index++) {
	/*avoid spanning 4MB bdry*/
	rambuff_acm = adjust_4MB(rambuff_acm, bufsize);
	if (rambuff_acm + bufsize > rambuff_end)
		break;
	rambuff_acm += bufsize;
    }
  /* Following line is OK, will waste buffers if index
   * not evenly divisible by ndevices -NJC*/
  numbufs = index / ndevices;
  printk(KERN_INFO "  - numbufs = %u\n", numbufs);
    if (numbufs < 2) {
	printk(KERN_INFO
	"DT3155: Error setup_buffers() couldn't allocate 2 bufs/board\n");
	return -ENOMEM;
    }

  /* now that we have board memory we spit it up */
  /* between the boards and the buffers          */
    rambuff_acm = rambuff_addr;
    for (minor = 0; minor < ndevices; minor++) {
	fb = &dt3155_status[minor].fbuffer;
	rambuff_acm = adjust_4MB(rambuff_acm, bufsize);

	/* Save the start of this boards buffer space (for mmap).  */
	dt3155_status[minor].mem_addr = rambuff_acm;

	for (index = 0; index < numbufs; index++) {
		rambuff_acm = adjust_4MB(rambuff_acm, bufsize);
		if (rambuff_acm + bufsize > rambuff_end) {
			/* Should never happen */
			printk(KERN_INFO "DT3155 PROGRAM ERROR (GCS)\n"
			"Error distributing allocated buffers\n");
			return -ENOMEM;
		}

		fb->frame_info[index].addr = rambuff_acm;
		push_empty(fb, index);
		/* printk("  - Buffer : %lx\n", fb->frame_info[index].addr); */
		fb->nbuffers += 1;
		rambuff_acm += bufsize;
	}

	/* Make sure there is an active buffer there. */
	fb->active_buf    = pop_empty(fb);
	fb->even_happened = 0;
	fb->even_stopped  = 0;

	/* make sure there is no locked_buf JML 2/28/00 */
	fb->locked_buf = -1;

	dt3155_status[minor].mem_size = rambuff_acm -
					dt3155_status[minor].mem_addr;

	/* setup the ready queue */
	fb->ready_head = 0;
	fb->ready_len = 0;
	printk(KERN_INFO "Available buffers for device %d: %d\n",
	    minor, fb->nbuffers);
    }

    return 1;
}

/*****************************************************
 * internal_release_locked_buffer
 *
 * The internal function for releasing a locked buffer.
 * It assumes interrupts are turned off.
 *****************************************************/
static void internal_release_locked_buffer(struct dt3155_fbuffer *fb)
{
	if (fb->locked_buf >= 0) {
		push_empty(fb, fb->locked_buf);
		fb->locked_buf = -1;
	}
}

/*****************************************************
 * dt3155_release_locked_buffer
 *
 * The user function of the above.
 *****************************************************/
void dt3155_release_locked_buffer(struct dt3155_fbuffer *fb)
{
	unsigned long int flags;

	local_save_flags(flags);
	local_irq_disable();
	internal_release_locked_buffer(fb);
	local_irq_restore(flags);
}

/*****************************************************
 * dt3155_flush
 *****************************************************/
int dt3155_flush(struct dt3155_fbuffer *fb)
{
	unsigned long int flags;
	int index;

	local_save_flags(flags);
	local_irq_disable();

	internal_release_locked_buffer(fb);
	fb->empty_len = 0;

	for (index = 0; index < fb->nbuffers; index++)
		push_empty(fb, index);

	/* Make sure there is an active buffer there. */
	fb->active_buf = pop_empty(fb);

	fb->even_happened = 0;
	fb->even_stopped  = 0;

	/* setup the ready queue  */
	fb->ready_head = 0;
	fb->ready_len = 0;

	local_irq_restore(flags);

	return 0;
}

/*****************************************************
 * dt3155_get_ready_buffer
 *
 * get_ready_buffer will grab the next chunk of data
 * if it is already there, otherwise it returns 0.
 * If the user has a buffer locked it will unlock
 * that buffer before returning the new one.
 *****************************************************/
int dt3155_get_ready_buffer(struct dt3155_fbuffer *fb)
{
	unsigned long int flags;
	int frame_index;

	local_save_flags(flags);
	local_irq_disable();

#ifdef DEBUG_QUES_A
	printques(fb);
#endif

	internal_release_locked_buffer(fb);

	if (is_ready_buf_empty(fb)) {
		frame_index = -1;
	} else {
		frame_index = pop_ready(fb);
		fb->locked_buf = frame_index;
	}

#ifdef DEBUG_QUES_B
	printques(fb);
#endif

	local_irq_restore(flags);

	return frame_index;
}
