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
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/types.h>

#include "dt3155.h"
#include "dt3155_drv.h"
#include "dt3155_io.h"
#include "dt3155_isr.h"
#include "allocator.h"

#define FOUR_MB         (0x0400000)  /* Can't DMA accross a 4MB boundary!*/
#define UPPER_10_BITS   (0x3FF<<22)  /* Can't DMA accross a 4MB boundary!*/


/* Pointer into global structure for handling buffers */
struct dt3155_fbuffer_s *dt3155_fbuffer[MAXBOARDS] = {NULL
#if MAXBOARDS == 2
						      , NULL
#endif
};

/******************************************************************************
 * Simple array based que struct
 *
 * Some handy functions using the buffering structure.
 *****************************************************************************/


/***************************
 * are_empty_buffers
 * m is minor # of device
 ***************************/
inline bool are_empty_buffers( int m )
{
  return ( dt3155_fbuffer[ m ]->empty_len );
}

/**************************
 * push_empty
 * m is minor # of device
 *
 * This is slightly confusing.  The number empty_len is the literal #
 * of empty buffers.  After calling, empty_len-1 is the index into the
 * empty buffer stack.  So, if empty_len == 1, there is one empty buffer,
 * given by dt3155_fbuffer[m]->empty_buffers[0].
 * empty_buffers should never fill up, though this is not checked.
 **************************/
inline void push_empty( int index, int m )
{
  dt3155_fbuffer[m]->empty_buffers[ dt3155_fbuffer[m]->empty_len ] = index;
  dt3155_fbuffer[m]->empty_len++;
}

/**************************
 * pop_empty( m )
 * m is minor # of device
 **************************/
inline int pop_empty( int m )
{
  dt3155_fbuffer[m]->empty_len--;
  return dt3155_fbuffer[m]->empty_buffers[ dt3155_fbuffer[m]->empty_len ];
}

/*************************
 * is_ready_buf_empty( m )
 * m is minor # of device
 *************************/
inline bool is_ready_buf_empty( int m )
{
  return ((dt3155_fbuffer[ m ]->ready_len) == 0);
}

/*************************
 * is_ready_buf_full( m )
 * m is minor # of device
 * this should *never* be true if there are any active, locked or empty
 * buffers, since it corresponds to nbuffers ready buffers!!
 * 7/31/02: total rewrite. --NJC
 *************************/
inline bool is_ready_buf_full( int m )
{
  return ( dt3155_fbuffer[ m ]->ready_len == dt3155_fbuffer[ m ]->nbuffers );
}

/*****************************************************
 * push_ready( m, buffer )
 * m is minor # of device
 *
 *****************************************************/
inline void push_ready( int m, int index )
{
  int head = dt3155_fbuffer[m]->ready_head;

  dt3155_fbuffer[ m ]->ready_que[ head ] = index;
  dt3155_fbuffer[ m ]->ready_head = ( (head + 1) %
				      (dt3155_fbuffer[ m ]->nbuffers) );
  dt3155_fbuffer[ m ]->ready_len++;

}

/*****************************************************
 * get_tail()
 * m is minor # of device
 *
 * Simply comptutes the tail given the head and the length.
 *****************************************************/
static inline int get_tail( int m )
{
  return ((dt3155_fbuffer[ m ]->ready_head -
	   dt3155_fbuffer[ m ]->ready_len +
	   dt3155_fbuffer[ m ]->nbuffers)%
	  (dt3155_fbuffer[ m ]->nbuffers));
}



/*****************************************************
 * pop_ready()
 * m is minor # of device
 *
 * This assumes that there is a ready buffer ready... should
 * be checked (e.g. with is_ready_buf_empty()  prior to call.
 *****************************************************/
inline int pop_ready( int m )
{
  int tail;
  tail = get_tail(m);
  dt3155_fbuffer[ m ]->ready_len--;
  return dt3155_fbuffer[ m ]->ready_que[ tail ];
}


/*****************************************************
 * printques
 * m is minor # of device
 *****************************************************/
inline void printques( int m )
{
  int head = dt3155_fbuffer[ m ]->ready_head;
  int tail;
  int num = dt3155_fbuffer[ m ]->nbuffers;
  int frame_index;
  int index;

  tail = get_tail(m);

  printk("\n R:");
  for ( index = tail; index != head; index++, index = index % (num) )
    {
      frame_index = dt3155_fbuffer[ m ]->ready_que[ index ];
      printk(" %d ", frame_index );
    }

  printk("\n E:");
  for ( index = 0; index < dt3155_fbuffer[ m ]->empty_len; index++ )
    {
      frame_index = dt3155_fbuffer[ m ]->empty_buffers[ index ];
      printk(" %d ", frame_index );
    }

  frame_index = dt3155_fbuffer[ m ]->active_buf;
  printk("\n A: %d", frame_index);

  frame_index = dt3155_fbuffer[ m ]->locked_buf;
  printk("\n L: %d \n", frame_index );

}

/*****************************************************
 * adjust_4MB
 *
 *  If a buffer intersects the 4MB boundary, push
 *  the start address up to the beginning of the
 *  next 4MB chunk (assuming bufsize < 4MB).
 *****************************************************/
u_long adjust_4MB (u_long buf_addr, u_long bufsize) {
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
void allocate_buffers (u_long *buf_addr, u_long* total_size_kbs,
		       u_long bufsize)
{
  /* Compute the minimum amount of memory guaranteed to hold all
     MAXBUFFERS such that no buffer crosses the 4MB boundary.
     Store this value in the variable "full_size" */

  u_long allocator_max;
  u_long bufs_per_chunk = (FOUR_MB / bufsize);
  u_long filled_chunks = (MAXBUFFERS-1) / bufs_per_chunk;
  u_long leftover_bufs = MAXBUFFERS - filled_chunks * bufs_per_chunk;

  u_long full_size = bufsize      /* possibly unusable part of 1st chunk */
    + filled_chunks * FOUR_MB   /* max # of completely filled 4mb chunks */
    + leftover_bufs * bufsize;  /* these buffs will be in a partly filled
				   chunk at beginning or end */

  u_long full_size_kbs = 1 + (full_size-1) / 1024;
  u_long min_size_kbs = 2*ndevices*bufsize / 1024;
  u_long size_kbs;

  /* Now, try to allocate full_size.  If this fails, keep trying for
     less & less memory until it succeeds. */
#ifndef STANDALONE_ALLOCATOR
  /* initialize the allocator            */
  allocator_init(&allocator_max);
#endif
  size_kbs = full_size_kbs;
  *buf_addr = 0;
  printk ("DT3155: We would like to get: %d KB\n", (u_int)(full_size_kbs));
  printk ("DT3155: ...but need at least: %d KB\n", (u_int)(min_size_kbs));
  printk ("DT3155: ...the allocator has: %d KB\n", (u_int)(allocator_max));
  size_kbs = (full_size_kbs <= allocator_max ? full_size_kbs : allocator_max);
  if (size_kbs > min_size_kbs) {
    if ((*buf_addr = allocator_allocate_dma (size_kbs, GFP_KERNEL)) != 0) {
      printk ("DT3155:  Managed to allocate: %d KB\n", (u_int)size_kbs);
      *total_size_kbs = size_kbs;
      return;
    }
  }
  /* If we got here, the allocation failed */
  printk ("DT3155: Allocator failed!\n");
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
u_long dt3155_setup_buffers(u_long *allocatorAddr)

{
  u_long index;
  u_long rambuff_addr; /* start of allocation */
  u_long rambuff_size; /* total size allocated to driver */
  u_long rambuff_acm;  /* accumlator, keep track of how much
			  is left after being split up*/
  u_long rambuff_end;  /* end of rambuff */
  u_long numbufs;      /* number of useful buffers allocated (per device) */
  u_long bufsize      = DT3155_MAX_ROWS * DT3155_MAX_COLS;
  int m;               /* minor # of device, looped for all devs */

  /* zero the fbuffer status and address structure */
  for ( m = 0; m < ndevices; m++)
    {
      dt3155_fbuffer[ m ] = &(dt3155_status[ m ].fbuffer);

      /* Make sure the buffering variables are consistent */
      {
	u_char *ptr = (u_char *) dt3155_fbuffer[ m ];
	for( index = 0; index < sizeof(struct dt3155_fbuffer_s); index++)
	  *(ptr++)=0;
      }
    }

  /* allocate a large contiguous chunk of RAM */
  allocate_buffers (&rambuff_addr, &rambuff_size, bufsize);
  printk( "DT3155: mem info\n" );
  printk( "  - rambuf_addr = 0x%x \n", (u_int)rambuff_addr );
  printk( "  - length (kb) = %u \n",  (u_int)rambuff_size );
  if( rambuff_addr == 0 )
    {
      printk( KERN_INFO
	      "DT3155: Error setup_buffers() allocator dma failed \n" );
      return -ENOMEM;
    }
  *allocatorAddr = rambuff_addr;
  rambuff_end = rambuff_addr + 1024 * rambuff_size;

  /* after allocation, we need to count how many useful buffers there
     are so we can give an equal number to each device */
  rambuff_acm = rambuff_addr;
  for ( index = 0; index < MAXBUFFERS; index++) {
    rambuff_acm = adjust_4MB (rambuff_acm, bufsize);/*avoid spanning 4MB bdry*/
    if (rambuff_acm + bufsize > rambuff_end)
      break;
    rambuff_acm += bufsize;
  }
  /* Following line is OK, will waste buffers if index
   * not evenly divisible by ndevices -NJC*/
  numbufs = index / ndevices;
  printk ("  - numbufs = %u\n", (u_int) numbufs);
  if (numbufs < 2) {
    printk( KERN_INFO
	    "DT3155: Error setup_buffers() couldn't allocate 2 bufs/board\n" );
    return -ENOMEM;
  }

  /* now that we have board memory we spit it up */
  /* between the boards and the buffers          */
  rambuff_acm = rambuff_addr;
  for ( m = 0; m < ndevices; m ++)
    {
      rambuff_acm = adjust_4MB (rambuff_acm, bufsize);

      /* Save the start of this boards buffer space (for mmap).  */
      dt3155_status[ m ].mem_addr = rambuff_acm;

      for (index = 0; index < numbufs; index++)
	{
	  rambuff_acm = adjust_4MB (rambuff_acm, bufsize);
	  if (rambuff_acm + bufsize > rambuff_end) {
	    /* Should never happen */
	    printk ("DT3155 PROGRAM ERROR (GCS)\n"
		    "Error distributing allocated buffers\n");
	    return -ENOMEM;
	  }

	  dt3155_fbuffer[ m ]->frame_info[ index ].addr = rambuff_acm;
	  push_empty( index, m );
	  /* printk("  - Buffer : %lx\n",
	   * dt3155_fbuffer[ m ]->frame_info[ index ].addr );
	   */
	  dt3155_fbuffer[ m ]->nbuffers += 1;
	  rambuff_acm += bufsize;
	}

      /* Make sure there is an active buffer there. */
      dt3155_fbuffer[ m ]->active_buf    = pop_empty( m );
      dt3155_fbuffer[ m ]->even_happened = 0;
      dt3155_fbuffer[ m ]->even_stopped  = 0;

      /* make sure there is no locked_buf JML 2/28/00 */
      dt3155_fbuffer[ m ]->locked_buf = -1;

      dt3155_status[ m ].mem_size =
	rambuff_acm - dt3155_status[ m ].mem_addr;

      /* setup the ready queue */
      dt3155_fbuffer[ m ]->ready_head = 0;
      dt3155_fbuffer[ m ]->ready_len = 0;
      printk("Available buffers for device %d: %d\n",
	     m, dt3155_fbuffer[ m ]->nbuffers);
    }

  return 1;
}

/*****************************************************
 * internal_release_locked_buffer
 *
 * The internal function for releasing a locked buffer.
 * It assumes interrupts are turned off.
 *
 * m is minor number of device
 *****************************************************/
static inline void internal_release_locked_buffer( int m )
{
  /* Pointer into global structure for handling buffers */
  if ( dt3155_fbuffer[ m ]->locked_buf >= 0 )
    {
      push_empty( dt3155_fbuffer[ m ]->locked_buf, m );
      dt3155_fbuffer[ m ]->locked_buf = -1;
    }
}


/*****************************************************
 * dt3155_release_locked_buffer()
 * m is minor # of device
 *
 * The user function of the above.
 *
 *****************************************************/
inline void dt3155_release_locked_buffer( int m )
{
	unsigned long int flags;
	local_save_flags(flags);
	local_irq_disable();
	internal_release_locked_buffer(m);
	local_irq_restore(flags);
}


/*****************************************************
 * dt3155_flush()
 * m is minor # of device
 *
 *****************************************************/
inline int dt3155_flush( int m )
{
  int index;
  unsigned long int flags;
  local_save_flags(flags);
  local_irq_disable();

  internal_release_locked_buffer( m );
  dt3155_fbuffer[ m ]->empty_len = 0;

  for ( index = 0; index < dt3155_fbuffer[ m ]->nbuffers; index++ )
    push_empty( index,  m );

  /* Make sure there is an active buffer there. */
  dt3155_fbuffer[ m ]->active_buf = pop_empty( m );

  dt3155_fbuffer[ m ]->even_happened = 0;
  dt3155_fbuffer[ m ]->even_stopped  = 0;

  /* setup the ready queue  */
  dt3155_fbuffer[ m ]->ready_head = 0;
  dt3155_fbuffer[ m ]->ready_len = 0;

  local_irq_restore(flags);

  return 0;
}

/*****************************************************
 * dt3155_get_ready_buffer()
 * m is minor # of device
 *
 * get_ready_buffer will grab the next chunk of data
 * if it is already there, otherwise it returns 0.
 * If the user has a buffer locked it will unlock
 * that buffer before returning the new one.
 *****************************************************/
inline int dt3155_get_ready_buffer( int m )
{
  int frame_index;
  unsigned long int flags;
  local_save_flags(flags);
  local_irq_disable();

#ifdef DEBUG_QUES_A
  printques( m );
#endif

  internal_release_locked_buffer( m );

  if (is_ready_buf_empty( m ))
    frame_index = -1;
  else
    {
      frame_index = pop_ready( m );
      dt3155_fbuffer[ m ]->locked_buf = frame_index;
    }

#ifdef DEBUG_QUES_B
  printques( m );
#endif

  local_irq_restore(flags);

  return frame_index;
}
