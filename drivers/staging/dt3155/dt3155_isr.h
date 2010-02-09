/*

Copyright 1996,2002 Gregory D. Hager, Alfred A. Rizzi, Noah J. Cowan,
		    Jason Lapenta, Scott Smedley

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


-- Changes --

  Date     Programmer   Description of changes made
  -------------------------------------------------------------------
  03-Jul-2000 JML       n/a
  24-Jul-2002 SS        GPL licence.
  26-Oct-2009 SS	Porting to 2.6.30 kernel.

-- notes --

*/

#ifndef DT3155_ISR_H
#define DT3155_ISR_H

extern struct dt3155_fbuffer_s *dt3155_fbuffer[MAXBOARDS];

/* User functions for buffering */
/* Initialize the buffering system.  This should */
/* be called prior to enabling interrupts */

u64 dt3155_setup_buffers(u64 *allocatorAddr);

/* Get the next frame of data if it is ready.  Returns */
/* zero if no data is ready.  If there is data but */
/* the user has a locked buffer, it will unlock that */
/* buffer and return it to the free list. */

int dt3155_get_ready_buffer(int minor);

/* Return a locked buffer to the free list */

void dt3155_release_locked_buffer(int minor);

/* Flush the buffer system */
int dt3155_flush(int minor);

/**********************************
 * Simple array based que struct
 **********************************/

bool are_empty_buffers(int minor);
void push_empty(int index, int minor);

int  pop_empty(int minor);

bool is_ready_buf_empty(int minor);
bool is_ready_buf_full(int minor);

void push_ready(int minor, int index);
int  pop_ready(int minor);


#endif
