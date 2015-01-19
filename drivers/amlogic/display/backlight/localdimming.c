/*
 * local dimming interface
 *
 * Author: Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2012 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/slab.h>

#include <linux/panel/localdimming.h>


struct backlight_block {
	unsigned int id;
	unsigned int luma;
};

enum blacklight_sequence {
	BL_BLOCK_SEQ_LTRT,
	BL_BLOCK_SEQ_LTRB,
	BL_BLOCK_SEQ_LBRT,
	BL_BLOCK_SEQ_LBRB,
	BL_BLOCK_SEQ_TLBL,
	BL_BLOCK_SEQ_TLBR,
	BL_BLOCK_SEQ_TRBL,
	BL_BLOCK_SEQ_TRBR,
};

static struct backlight_block *block_data;
static unsigned int block_size;
static enum blacklight_sequence block_seq;

int local_dimming_init(unsigned int size, enum blacklight_sequence seq)
{
	block_size = size;
	block_data = kmalloc(sizeof(struct backlight_block) * size, GFP_KERNEL);
	block_seq = seq;
	return 0;
}

EXPORT_SYMBOL(local_dimming_init);

int local_dimming_update(unsigned int *lumas, unsigned int size)
{
	int i;
	for (i = 0; i < size; i++) {
		//block_data[i].id = lumas[i].id;
		//block_data[i].luma = blocks[i].luma;
	}
	return 0;
}



