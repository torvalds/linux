/* Copyright (c) 2014 - 2015 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 */
#ifndef _ESP_SLAB_H_
#define _ESP_SLAB_H_

#define DEFAULT_PICECS_NUM_SHIFT 11
#define DEFAULT_PICECS_NUM (1<<DEFAULT_PICECS_NUM_SHIFT)

#define PREMALLOC_LARGE_SHIFT 19
#define PREMALLOC_LITTLE_SHIFT 17

#define LARGE_PIECE_SIZE_SHIFT 10
#define LARGE_PIECES_NUM_SHIFT (PREMALLOC_LARGE_SHIFT - LARGE_PIECE_SIZE_SHIFT)

#define LITTLE_PIECE_SIZE_SHIFT 6
#define LITTLE_PIECES_NUM_SHIFT (PREMALLOC_LITTLE_SHIFT - LITTLE_PIECE_SIZE_SHIFT)

#define PREMALLOC_LARGE_SIZE (1<<PREMALLOC_LARGE_SHIFT)
#define PREMALLOC_LITTLE_SIZE (1<<PREMALLOC_LITTLE_SHIFT)
#define PREMALLOC_SIZE (PREMALLOC_LARGE_SIZE + PREMALLOC_LITTLE_SIZE)

#define LARGE_PIECE_SIZE (1<<LARGE_PIECE_SIZE_SHIFT)        /* 1KByte per piece divide 512KByte, then equals 512 pieces. 512K = 1K*512 */
#define LARGE_PIECES_NUM (1<<LARGE_PIECES_NUM_SHIFT)

#define LITTLE_PIECE_SIZE (1<<LITTLE_PIECE_SIZE_SHIFT)        /* 64Byte per piece divide 128KByte, then equals 2K pieces. 128K = 2K*64 */
#define LITTLE_PIECES_NUM (1<<LITTLE_PIECES_NUM_SHIFT)

#define LARGE_BIT_MAP_SIZE (LARGE_PIECES_NUM >> 5)
#define LITTLE_BIT_MAP_SIZE (LITTLE_PIECES_NUM >> 5)

#define LITTLE_LIMIT 512           /* < LITTLE_LIMIT use little mem, or use large mem */

#define DEFAULT_MAX_ACCESSES_PER  128

struct mem_access {
	void *p;
	int pieces;
};

struct esp_mem_mgmt_per {
	void *start_p;
	u32 *bit_map;
	struct mem_access macs[DEFAULT_MAX_ACCESSES_PER];
	int cur_access;
	spinlock_t spin_lock;
	unsigned long lock_flags;
};

struct esp_mem_mgmt {
	struct esp_mem_mgmt_per large_mmp;
	struct esp_mem_mgmt_per little_mmp;
};


int set_gl_mem_p(void *mem_p);
void *get_gl_mem_p(void);
int esp_slab_init(void);
void esp_slab_deinit(void);

#endif /* _ESP_SLAB_H_ */
