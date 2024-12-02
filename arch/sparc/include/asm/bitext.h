/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bitext.h: Bit string operations on the sparc, specific to architecture.
 *
 * Copyright 2002 Pete Zaitcev <zaitcev@yahoo.com>
 */

#ifndef _SPARC_BITEXT_H
#define _SPARC_BITEXT_H

#include <linux/spinlock.h>

struct bit_map {
	spinlock_t lock;
	unsigned long *map;
	int size;
	int used;
	int last_off;
	int last_size;
	int first_free;
	int num_colors;
};

int bit_map_string_get(struct bit_map *t, int len, int align);
void bit_map_clear(struct bit_map *t, int offset, int len);
void bit_map_init(struct bit_map *t, unsigned long *map, int size);

#endif /* defined(_SPARC_BITEXT_H) */
