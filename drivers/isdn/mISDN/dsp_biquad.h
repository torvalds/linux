/*
 * SpanDSP - a series of DSP components for telephony
 *
 * biquad.h - General telephony bi-quad section routines (currently this just
 *            handles canonic/type 2 form)
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2001 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

struct biquad2_state {
	int32_t gain;
	int32_t a1;
	int32_t a2;
	int32_t b1;
	int32_t b2;

	int32_t z1;
	int32_t z2;
};

static inline void biquad2_init(struct biquad2_state *bq,
				int32_t gain, int32_t a1, int32_t a2, int32_t b1, int32_t b2)
{
	bq->gain = gain;
	bq->a1 = a1;
	bq->a2 = a2;
	bq->b1 = b1;
	bq->b2 = b2;

	bq->z1 = 0;
	bq->z2 = 0;
}

static inline int16_t biquad2(struct biquad2_state *bq, int16_t sample)
{
	int32_t y;
	int32_t z0;

	z0 = sample * bq->gain + bq->z1 * bq->a1 + bq->z2 * bq->a2;
	y = z0 + bq->z1 * bq->b1 + bq->z2 * bq->b2;

	bq->z2 = bq->z1;
	bq->z1 = z0 >> 15;
	y >>= 15;
	return  y;
}
