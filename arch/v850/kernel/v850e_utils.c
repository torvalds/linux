/*
 * include/asm-v850/v850e_utils.h -- Utility functions associated with
 *	V850E CPUs
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <asm/v850e_utils.h>

/* Calculate counter clock-divider and count values to attain the
   desired frequency RATE from the base frequency BASE_FREQ.  The
   counter is expected to have a clock-divider, which can divide the
   system cpu clock by a power of two value from MIN_DIVLOG2 to
   MAX_DIV_LOG2, and a word-size of COUNTER_SIZE bits (the counter
   counts up and resets whenever it's equal to the compare register,
   generating an interrupt or whatever when it does so).  The returned
   values are: *DIVLOG2 -- log2 of the desired clock divider and *COUNT
   -- the counter compare value to use.  Returns true if it was possible
   to find a reasonable value, otherwise false (and the other return
   values will be set to be as good as possible).  */
int calc_counter_params (unsigned long base_freq,
			 unsigned long rate,
			 unsigned min_divlog2, unsigned max_divlog2,
			 unsigned counter_size,
			 unsigned *divlog2, unsigned *count)
{
	unsigned _divlog2;
	int ok = 0;

	/* Find the lowest clock divider setting that can represent RATE.  */
	for (_divlog2 = min_divlog2; _divlog2 <= max_divlog2; _divlog2++) {
		/* Minimum interrupt rate possible using this divider.  */
		unsigned min_int_rate
			= (base_freq >> _divlog2) >> counter_size;

		if (min_int_rate <= rate) {
			/* This setting is the highest resolution
			   setting that's slow enough enough to attain
			   RATE interrupts per second, so use it.  */
			ok = 1;
			break;
		}
	}

	if (_divlog2 > max_divlog2)
		/* Can't find correct setting.  */
		_divlog2 = max_divlog2;

	if (divlog2)
		*divlog2 = _divlog2;
	if (count)
		*count = ((base_freq >> _divlog2) + rate/2) / rate;

	return ok;
}
