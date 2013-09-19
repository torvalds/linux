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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 *
 *************************************************************************
 *
 * This file is intended to contain all the kernel "differences" between the
 * various kernels that we support.
 *
 *************************************************************************/

#ifndef __DGNC_KCOMPAT_H
#define __DGNC_KCOMPAT_H

#include <linux/version.h>

# ifndef KERNEL_VERSION
#  define KERNEL_VERSION(a,b,c)  (((a) << 16) + ((b) << 8) + (c))
# endif


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


#  define PARM_STR(VAR, INIT, PERM, DESC) \
		static char *VAR = INIT; \
		char *dgnc_##VAR; \
		module_param(VAR, charp, PERM); \
		MODULE_PARM_DESC(VAR, DESC);

#  define PARM_INT(VAR, INIT, PERM, DESC) \
		static int VAR = INIT; \
		int dgnc_##VAR; \
		module_param(VAR, int, PERM); \
		MODULE_PARM_DESC(VAR, DESC);

#  define PARM_ULONG(VAR, INIT, PERM, DESC) \
		static ulong VAR = INIT; \
		ulong dgnc_##VAR; \
		module_param(VAR, long, PERM); \
		MODULE_PARM_DESC(VAR, DESC);





#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)



/* NOTHING YET */



# else



# error "this driver does not support anything below the 2.6.27 kernel series."



# endif

#endif /* ! __DGNC_KCOMPAT_H */
