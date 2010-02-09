/*

Copyright 1996,2002 Gregory D. Hager, Alfred A. Rizzi, Noah J. Cowan,
		    Scott Smedley

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
*/

#ifndef DT3155_DRV_INC
#define DT3155_DRV_INC

/* kernel logical address of the frame grabbers */
extern u8 *dt3155_lbase[MAXBOARDS];

/* kernel logical address of ram buffer */
extern u8 *dt3155_bbase;

#ifdef __KERNEL__
#include <linux/wait.h>

/* wait queue for reads */
extern wait_queue_head_t dt3155_read_wait_queue[MAXBOARDS];
#endif

/* number of devices */
extern u_int ndevices;

extern int dt3155_errno;

#endif
