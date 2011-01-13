/*
 * gs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Memory allocation/release wrappers.  This module allows clients to
 * avoid OS spacific issues related to memory allocation.  It also provides
 * simple diagnostic capabilities to assist in the detection of memory
 * leaks.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef GS_
#define GS_

/*
 *  ======== gs_alloc ========
 *  Alloc size bytes of space.  Returns pointer to space
 *  allocated, otherwise NULL.
 */
extern void *gs_alloc(u32 size);

/*
 *  ======== gs_exit ========
 *  Module exit.  Do not change to "#define gs_init()"; in
 *  some environments this operation must actually do some work!
 */
extern void gs_exit(void);

/*
 *  ======== gs_free ========
 *  Free space allocated by gs_alloc() or GS_calloc().
 */
extern void gs_free(void *ptr);

/*
 *  ======== gs_frees ========
 *  Free space allocated by gs_alloc() or GS_calloc() and assert that
 *  the size of the allocation is size bytes.
 */
extern void gs_frees(void *ptr, u32 size);

/*
 *  ======== gs_init ========
 *  Module initialization.  Do not change to "#define gs_init()"; in
 *  some environments this operation must actually do some work!
 */
extern void gs_init(void);

#endif /*GS_ */
