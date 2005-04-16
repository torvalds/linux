/*
 * arch/v850/lib/memcpy.c -- Memory copying
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/types.h>
#include <asm/string.h>

#define CHUNK_SIZE		32 /* bytes */
#define CHUNK_ALIGNED(addr)	(((unsigned long)addr & 0x3) == 0)

/* Note that this macro uses 8 call-clobbered registers (not including
   R1), which are few enough so that the following functions don't need
   to spill anything to memory.  It also uses R1, which is nominally
   reserved for the assembler, but here it should be OK.  */
#define COPY_CHUNK(src, dst)			\
   asm ("mov %0, ep;"				\
	"sld.w 0[ep], r1; sld.w 4[ep], r12;"	\
	"sld.w 8[ep], r13; sld.w 12[ep], r14;"	\
	"sld.w 16[ep], r15; sld.w 20[ep], r17;"	\
	"sld.w 24[ep], r18; sld.w 28[ep], r19;"	\
	"mov %1, ep;"				\
	"sst.w r1, 0[ep]; sst.w r12, 4[ep];"	\
	"sst.w r13, 8[ep]; sst.w r14, 12[ep];"	\
	"sst.w r15, 16[ep]; sst.w r17, 20[ep];"	\
	"sst.w r18, 24[ep]; sst.w r19, 28[ep]"	\
	:: "r" (src), "r" (dst)			\
	: "r1", "r12", "r13", "r14", "r15",	\
	  "r17", "r18", "r19", "ep", "memory");

void *memcpy (void *dst, const void *src, __kernel_size_t size)
{
	char *_dst = dst;
	const char *_src = src;

	if (size >= CHUNK_SIZE && CHUNK_ALIGNED(_src) && CHUNK_ALIGNED(_dst)) {
		/* Copy large blocks efficiently.  */
		unsigned count;
		for (count = size / CHUNK_SIZE; count; count--) {
			COPY_CHUNK (_src, _dst);
			_src += CHUNK_SIZE;
			_dst += CHUNK_SIZE;
		}
		size %= CHUNK_SIZE;
	}

	if (size > 0)
		do
			*_dst++ = *_src++;
		while (--size);

	return dst;
}

void *memmove (void *dst, const void *src, __kernel_size_t size)
{
	if ((unsigned long)dst < (unsigned long)src
	    || (unsigned long)src + size < (unsigned long)dst)
		return memcpy (dst, src, size);
	else {
		char *_dst = dst + size;
		const char *_src = src + size;

		if (size >= CHUNK_SIZE
		    && CHUNK_ALIGNED (_src) && CHUNK_ALIGNED (_dst))
		{
			/* Copy large blocks efficiently.  */
			unsigned count;
			for (count = size / CHUNK_SIZE; count; count--) {
				_src -= CHUNK_SIZE;
				_dst -= CHUNK_SIZE;
				COPY_CHUNK (_src, _dst);
			}
			size %= CHUNK_SIZE;
		}

		if (size > 0)
			do
				*--_dst = *--_src;
			while (--size);

		return _dst;
	}
}
