/*
 * gb.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Generic bitmap operations.
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <linux/types.h>
/*  ----------------------------------- This */
#include <dspbridge/gs.h>
#include <dspbridge/gb.h>

struct gb_t_map {
	u32 len;
	u32 wcnt;
	u32 *words;
};

/*
 *  ======== gb_clear ========
 *  purpose:
 *      Clears a bit in the bit map.
 */

void gb_clear(struct gb_t_map *map, u32 bitn)
{
	u32 mask;

	mask = 1L << (bitn % BITS_PER_LONG);
	map->words[bitn / BITS_PER_LONG] &= ~mask;
}

/*
 *  ======== gb_create ========
 *  purpose:
 *      Creates a bit map.
 */

struct gb_t_map *gb_create(u32 len)
{
	struct gb_t_map *map;
	u32 i;
	map = (struct gb_t_map *)gs_alloc(sizeof(struct gb_t_map));
	if (map != NULL) {
		map->len = len;
		map->wcnt = len / BITS_PER_LONG + 1;
		map->words = (u32 *) gs_alloc(map->wcnt * sizeof(u32));
		if (map->words != NULL) {
			for (i = 0; i < map->wcnt; i++)
				map->words[i] = 0L;

		} else {
			gs_frees(map, sizeof(struct gb_t_map));
			map = NULL;
		}
	}

	return map;
}

/*
 *  ======== gb_delete ========
 *  purpose:
 *      Frees a bit map.
 */

void gb_delete(struct gb_t_map *map)
{
	gs_frees(map->words, map->wcnt * sizeof(u32));
	gs_frees(map, sizeof(struct gb_t_map));
}

/*
 *  ======== gb_findandset ========
 *  purpose:
 *      Finds a free bit and sets it.
 */
u32 gb_findandset(struct gb_t_map *map)
{
	u32 bitn;

	bitn = gb_minclear(map);

	if (bitn != GB_NOBITS)
		gb_set(map, bitn);

	return bitn;
}

/*
 *  ======== gb_minclear ========
 *  purpose:
 *      returns the location of the first unset bit in the bit map.
 */
u32 gb_minclear(struct gb_t_map *map)
{
	u32 bit_location = 0;
	u32 bit_acc = 0;
	u32 i;
	u32 bit;
	u32 *word;

	for (word = map->words, i = 0; i < map->wcnt; word++, i++) {
		if (~*word) {
			for (bit = 0; bit < BITS_PER_LONG; bit++, bit_acc++) {
				if (bit_acc == map->len)
					return GB_NOBITS;

				if (~*word & (1L << bit)) {
					bit_location = i * BITS_PER_LONG + bit;
					return bit_location;
				}

			}
		} else {
			bit_acc += BITS_PER_LONG;
		}
	}

	return GB_NOBITS;
}

/*
 *  ======== gb_set ========
 *  purpose:
 *      Sets a bit in the bit map.
 */

void gb_set(struct gb_t_map *map, u32 bitn)
{
	u32 mask;

	mask = 1L << (bitn % BITS_PER_LONG);
	map->words[bitn / BITS_PER_LONG] |= mask;
}

/*
 *  ======== gb_test ========
 *  purpose:
 *      Returns true if the bit is set in the specified location.
 */

bool gb_test(struct gb_t_map *map, u32 bitn)
{
	bool state;
	u32 mask;
	u32 word;

	mask = 1L << (bitn % BITS_PER_LONG);
	word = map->words[bitn / BITS_PER_LONG];
	state = word & mask ? true : false;

	return state;
}
