#ifndef _FDC_ISR_H
#define _FDC_ISR_H

/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1996-1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/fdc-isr.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:07 $
 *
 *      This file declares the global variables necessary to
 *      synchronize the interrupt service routine (isr) with the
 *      remainder of the QIC-40/80/3010/3020 floppy-tape driver
 *      "ftape" for Linux.
 */

/*
 *      fdc-isr.c defined public variables
 */
extern volatile int ft_expected_stray_interrupts; /* masks stray interrupts */
extern volatile int ft_seek_completed;	          /* flag set by isr */
extern volatile int ft_interrupt_seen;	          /* flag set by isr */
extern volatile int ft_hide_interrupt;            /* flag set by isr */

/*
 *      fdc-io.c defined public functions
 */
extern void fdc_isr(void);

/*
 *      A kernel hook that steals one interrupt from the floppy
 *      driver (Should be fixed when the new fdc driver gets ready)
 *      See the linux kernel source files:
 *          drivers/block/floppy.c & drivers/block/blk.h
 *      for the details.
 */
extern void (*do_floppy) (void);

#endif
