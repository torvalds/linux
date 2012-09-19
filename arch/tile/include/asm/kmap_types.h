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

#ifndef _ASM_TILE_KMAP_TYPES_H
#define _ASM_TILE_KMAP_TYPES_H

/*
 * In 32-bit TILE Linux we have to balance the desire to have a lot of
 * nested atomic mappings with the fact that large page sizes and many
 * processors chew up address space quickly.  In a typical
 * 64-processor, 64KB-page layout build, making KM_TYPE_NR one larger
 * adds 4MB of required address-space.  For now we leave KM_TYPE_NR
 * set to depth 8.
 */
#define	KM_TYPE_NR 8

#endif /* _ASM_TILE_KMAP_TYPES_H */
