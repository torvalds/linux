/*
 * Copyright 2004 Digi International (www.digi.com)
 *      Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 *************************************************************************
 *
 * This file is intended to contain all the kernel "differences" between the
 * various kernels that we support.
 *
 *************************************************************************/

#ifndef __DGNC_KCOMPAT_H
#define __DGNC_KCOMPAT_H

#if !defined(TTY_FLIPBUF_SIZE)
# define TTY_FLIPBUF_SIZE 512
#endif


/* Sparse stuff */
# ifndef __user
#  define __user
#  define __kernel
#  define __safe
#  define __force
#  define __chk_user_ptr(x) (void)0
# endif


#endif /* ! __DGNC_KCOMPAT_H */
