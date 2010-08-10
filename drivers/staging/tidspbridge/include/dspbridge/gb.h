/*
 * gb.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Generic bitmap manager.
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

#ifndef GB_
#define GB_

#define GB_NOBITS (~0)
#include <dspbridge/host_os.h>

struct gb_t_map;

/*
 *  ======== gb_clear ========
 *  Clear the bit in position bitn in the bitmap map.  Bit positions are
 *  zero based.
 */

extern void gb_clear(struct gb_t_map *map, u32 bitn);

/*
 *  ======== gb_create ========
 *  Create a bit map with len bits.  Initially all bits are cleared.
 */

extern struct gb_t_map *gb_create(u32 len);

/*
 *  ======== gb_delete ========
 *  Delete previously created bit map
 */

extern void gb_delete(struct gb_t_map *map);

/*
 *  ======== gb_findandset ========
 *  Finds a clear bit, sets it, and returns the position
 */

extern u32 gb_findandset(struct gb_t_map *map);

/*
 *  ======== gb_minclear ========
 *  gb_minclear returns the minimum clear bit position.  If no bit is
 *  clear, gb_minclear returns -1.
 */
extern u32 gb_minclear(struct gb_t_map *map);

/*
 *  ======== gb_set ========
 *  Set the bit in position bitn in the bitmap map.  Bit positions are
 *  zero based.
 */

extern void gb_set(struct gb_t_map *map, u32 bitn);

/*
 *  ======== gb_test ========
 *  Returns TRUE if the bit in position bitn is set in map; otherwise
 *  gb_test returns FALSE.  Bit positions are zero based.
 */

extern bool gb_test(struct gb_t_map *map, u32 bitn);

#endif /*GB_ */
