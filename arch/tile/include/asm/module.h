/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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

#ifndef _ASM_TILE_MODULE_H
#define _ASM_TILE_MODULE_H

#include <arch/chip.h>

#include <asm-generic/module.h>

/* We can't use modules built with different page sizes. */
#if defined(CONFIG_PAGE_SIZE_16KB)
# define MODULE_PGSZ " 16KB"
#elif defined(CONFIG_PAGE_SIZE_64KB)
# define MODULE_PGSZ " 64KB"
#else
# define MODULE_PGSZ ""
#endif

/* We don't really support no-SMP so tag if someone tries. */
#ifdef CONFIG_SMP
#define MODULE_NOSMP ""
#else
#define MODULE_NOSMP " nosmp"
#endif

#define MODULE_ARCH_VERMAGIC CHIP_ARCH_NAME MODULE_PGSZ MODULE_NOSMP

#endif /* _ASM_TILE_MODULE_H */
