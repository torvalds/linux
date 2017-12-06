/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _UAPI__ARCH_SPR_DEF_H__
#define _UAPI__ARCH_SPR_DEF_H__

/* Include the proper base SPR definition file. */
#ifdef __tilegx__
#include <arch/spr_def_64.h>
#else
#include <arch/spr_def_32.h>
#endif


#endif /* _UAPI__ARCH_SPR_DEF_H__ */
