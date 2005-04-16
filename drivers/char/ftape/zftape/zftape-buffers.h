#ifndef _FTAPE_DYNMEM_H
#define _FTAPE_DYNMEM_H

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
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-buffers.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:59 $
 *
 *   memory allocation routines.
 *
 */

/* we do not allocate all of the really large buffer memory before
 * someone tries to open the drive. ftape_open() may fail with
 * -ENOMEM, but that's better having 200k of vmalloced memory which
 * cannot be swapped out.
 */

extern void  zft_memory_stats(void);
extern int   zft_vmalloc_once(void *new, size_t size);
extern int   zft_vcalloc_once(void *new, size_t size);
extern int   zft_vmalloc_always(void *new, size_t size);
extern void  zft_vfree(void *old, size_t size);
extern void *zft_kmalloc(size_t size);
extern void  zft_kfree(void *old, size_t size);

/* called by cleanup_module() 
 */
extern void zft_uninit_mem(void);

#endif







