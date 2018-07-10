/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2008 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Resize (grow) malloced vector.
 *
 *  #define magic packed two parameters into one:
 *  sizeof = sizeof_and_shift >> 8
 *  shift  = (sizeof_and_shift) & 0xff
 *
 * Lets say shift = 4. 1 << 4 == 0x10.
 * If idx == 0, 0x10, 0x20 etc, vector[] is resized to next higher
 * idx step, plus one: if idx == 0x20, vector[] is resized to 0x31,
 * thus last usable element is vector[0x30].
 *
 * In other words: after xrealloc_vector(v, 4, idx), with any idx,
 * it's ok to use at least v[idx] and v[idx+1].
 * v[idx+2] etc generally are not ok.
 *
 * New elements are zeroed out, but only if realloc was done
 * (not on every call). You can depend on v[idx] and v[idx+1] being
 * zeroed out if you use it like this:
 *  v = xrealloc_vector(v, 4, idx);
 *  v[idx].some_fields = ...; - the rest stays 0/NULL
 *  idx++;
 * If you do not advance idx like above, you should be more careful.
 * Next call to xrealloc_vector(v, 4, idx) may or may not zero out v[idx].
 */
void* FAST_FUNC xrealloc_vector_helper(void *vector, unsigned sizeof_and_shift, int idx)
{
	int mask = 1 << (uint8_t)sizeof_and_shift;

	if (!(idx & (mask - 1))) {
		sizeof_and_shift >>= 8; /* sizeof(vector[0]) */
		vector = xrealloc(vector, sizeof_and_shift * (idx + mask + 1));
		memset((char*)vector + (sizeof_and_shift * idx), 0, sizeof_and_shift * (mask + 1));
	}
	return vector;
}
