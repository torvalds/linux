/*
 * Bitfield
 * Copyright (c) 2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef BITFIELD_H
#define BITFIELD_H

struct bitfield;

struct bitfield * bitfield_alloc(size_t max_bits);
void bitfield_free(struct bitfield *bf);
void bitfield_set(struct bitfield *bf, size_t bit);
void bitfield_clear(struct bitfield *bf, size_t bit);
int bitfield_is_set(struct bitfield *bf, size_t bit);
int bitfield_get_first_zero(struct bitfield *bf);

#endif /* BITFIELD_H */
