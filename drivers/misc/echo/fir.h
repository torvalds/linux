/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SpanDSP - a series of DSP components for telephony
 *
 * fir.h - General telephony FIR routines
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2002 Steve Underwood
 *
 * All rights reserved.
 */

#if !defined(_FIR_H_)
#define _FIR_H_

/*
   Ideas for improvement:

   1/ Rewrite filter for dual MAC inner loop.  The issue here is handling
   history sample offsets that are 16 bit aligned - the dual MAC needs
   32 bit aligmnent.  There are some good examples in libbfdsp.

   2/ Use the hardware circular buffer facility tohalve memory usage.

   3/ Consider using internal memory.

   Using less memory might also improve speed as cache misses will be
   reduced. A drop in MIPs and memory approaching 50% should be
   possible.

   The foreground and background filters currenlty use a total of
   about 10 MIPs/ch as measured with speedtest.c on a 256 TAP echo
   can.
*/

/*
 * 16 bit integer FIR descriptor. This defines the working state for a single
 * instance of an FIR filter using 16 bit integer coefficients.
 */
struct fir16_state_t {
	int taps;
	int curr_pos;
	const int16_t *coeffs;
	int16_t *history;
};

/*
 * 32 bit integer FIR descriptor. This defines the working state for a single
 * instance of an FIR filter using 32 bit integer coefficients, and filtering
 * 16 bit integer data.
 */
struct fir32_state_t {
	int taps;
	int curr_pos;
	const int32_t *coeffs;
	int16_t *history;
};

/*
 * Floating point FIR descriptor. This defines the working state for a single
 * instance of an FIR filter using floating point coefficients and data.
 */
struct fir_float_state_t {
	int taps;
	int curr_pos;
	const float *coeffs;
	float *history;
};

static inline const int16_t *fir16_create(struct fir16_state_t *fir,
					      const int16_t *coeffs, int taps)
{
	fir->taps = taps;
	fir->curr_pos = taps - 1;
	fir->coeffs = coeffs;
	fir->history = kcalloc(taps, sizeof(int16_t), GFP_KERNEL);
	return fir->history;
}

static inline void fir16_flush(struct fir16_state_t *fir)
{
	memset(fir->history, 0, fir->taps * sizeof(int16_t));
}

static inline void fir16_free(struct fir16_state_t *fir)
{
	kfree(fir->history);
}

static inline int16_t fir16(struct fir16_state_t *fir, int16_t sample)
{
	int32_t y;
	int i;
	int offset1;
	int offset2;

	fir->history[fir->curr_pos] = sample;

	offset2 = fir->curr_pos;
	offset1 = fir->taps - offset2;
	y = 0;
	for (i = fir->taps - 1; i >= offset1; i--)
		y += fir->coeffs[i] * fir->history[i - offset1];
	for (; i >= 0; i--)
		y += fir->coeffs[i] * fir->history[i + offset2];
	if (fir->curr_pos <= 0)
		fir->curr_pos = fir->taps;
	fir->curr_pos--;
	return (int16_t) (y >> 15);
}

static inline const int16_t *fir32_create(struct fir32_state_t *fir,
					      const int32_t *coeffs, int taps)
{
	fir->taps = taps;
	fir->curr_pos = taps - 1;
	fir->coeffs = coeffs;
	fir->history = kcalloc(taps, sizeof(int16_t), GFP_KERNEL);
	return fir->history;
}

static inline void fir32_flush(struct fir32_state_t *fir)
{
	memset(fir->history, 0, fir->taps * sizeof(int16_t));
}

static inline void fir32_free(struct fir32_state_t *fir)
{
	kfree(fir->history);
}

static inline int16_t fir32(struct fir32_state_t *fir, int16_t sample)
{
	int i;
	int32_t y;
	int offset1;
	int offset2;

	fir->history[fir->curr_pos] = sample;
	offset2 = fir->curr_pos;
	offset1 = fir->taps - offset2;
	y = 0;
	for (i = fir->taps - 1; i >= offset1; i--)
		y += fir->coeffs[i] * fir->history[i - offset1];
	for (; i >= 0; i--)
		y += fir->coeffs[i] * fir->history[i + offset2];
	if (fir->curr_pos <= 0)
		fir->curr_pos = fir->taps;
	fir->curr_pos--;
	return (int16_t) (y >> 15);
}

#endif
