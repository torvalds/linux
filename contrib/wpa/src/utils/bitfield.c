/*
 * Bitfield
 * Copyright (c) 2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "bitfield.h"


struct bitfield {
	u8 *bits;
	size_t max_bits;
};


struct bitfield * bitfield_alloc(size_t max_bits)
{
	struct bitfield *bf;

	bf = os_zalloc(sizeof(*bf) + (max_bits + 7) / 8);
	if (bf == NULL)
		return NULL;
	bf->bits = (u8 *) (bf + 1);
	bf->max_bits = max_bits;
	return bf;
}


void bitfield_free(struct bitfield *bf)
{
	os_free(bf);
}


void bitfield_set(struct bitfield *bf, size_t bit)
{
	if (bit >= bf->max_bits)
		return;
	bf->bits[bit / 8] |= BIT(bit % 8);
}


void bitfield_clear(struct bitfield *bf, size_t bit)
{
	if (bit >= bf->max_bits)
		return;
	bf->bits[bit / 8] &= ~BIT(bit % 8);
}


int bitfield_is_set(struct bitfield *bf, size_t bit)
{
	if (bit >= bf->max_bits)
		return 0;
	return !!(bf->bits[bit / 8] & BIT(bit % 8));
}


static int first_zero(u8 val)
{
	int i;
	for (i = 0; i < 8; i++) {
		if (!(val & 0x01))
			return i;
		val >>= 1;
	}
	return -1;
}


int bitfield_get_first_zero(struct bitfield *bf)
{
	size_t i;
	for (i = 0; i < (bf->max_bits + 7) / 8; i++) {
		if (bf->bits[i] != 0xff)
			break;
	}
	if (i == (bf->max_bits + 7) / 8)
		return -1;
	i = i * 8 + first_zero(bf->bits[i]);
	if (i >= bf->max_bits)
		return -1;
	return i;
}
