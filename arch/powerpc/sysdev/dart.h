/*
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _POWERPC_SYSDEV_DART_H
#define _POWERPC_SYSDEV_DART_H


/* physical base of DART registers */
#define DART_BASE        0xf8033000UL

/* Offset from base to control register */
#define DARTCNTL   0
/* Offset from base to exception register */
#define DARTEXCP   0x10
/* Offset from base to TLB tag registers */
#define DARTTAG    0x1000


/* Control Register fields */

/* base address of table (pfn) */
#define DARTCNTL_BASE_MASK    0xfffff
#define DARTCNTL_BASE_SHIFT   12

#define DARTCNTL_FLUSHTLB     0x400
#define DARTCNTL_ENABLE       0x200

/* size of table in pages */
#define DARTCNTL_SIZE_MASK    0x1ff
#define DARTCNTL_SIZE_SHIFT   0


/* DART table fields */

#define DARTMAP_VALID   0x80000000
#define DARTMAP_RPNMASK 0x00ffffff


#define DART_PAGE_SHIFT		12
#define DART_PAGE_SIZE		(1 << DART_PAGE_SHIFT)
#define DART_PAGE_FACTOR	(PAGE_SHIFT - DART_PAGE_SHIFT)


#endif /* _POWERPC_SYSDEV_DART_H */
