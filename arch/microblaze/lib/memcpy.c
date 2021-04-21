/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2007 John Williams
 *
 * Reasonably optimised generic C-code for memcpy on Microblaze
 * This is generic C code to do efficient, alignment-aware memcpy.
 *
 * It is based on demo code originally Copyright 2001 by Intel Corp, taken from
 * http://www.embedded.com/showArticle.jhtml?articleID=19205567
 *
 * Attempts were made, unsuccessfully, to contact the original
 * author of this code (Michael Morrow, Intel).  Below is the original
 * copyright notice.
 *
 * This software has been developed by Intel Corporation.
 * Intel specifically disclaims all warranties, express or
 * implied, and all liability, including consequential and
 * other indirect damages, for the use of this program, including
 * liability for infringement of any proprietary rights,
 * and including the warranties of merchantability and fitness
 * for a particular purpose. Intel does not assume any
 * responsibility for and errors which may appear in this program
 * not any responsibility to update it.
 */

#include <linux/export.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/compiler.h>

#include <linux/string.h>

#ifdef __HAVE_ARCH_MEMCPY
#ifndef CONFIG_OPT_LIB_FUNCTION
void *memcpy(void *v_dst, const void *v_src, __kernel_size_t c)
{
	const char *src = v_src;
	char *dst = v_dst;

	/* Simple, byte oriented memcpy. */
	while (c--)
		*dst++ = *src++;

	return v_dst;
}
#else /* CONFIG_OPT_LIB_FUNCTION */
void *memcpy(void *v_dst, const void *v_src, __kernel_size_t c)
{
	const char *src = v_src;
	char *dst = v_dst;

	/* The following code tries to optimize the copy by using unsigned
	 * alignment. This will work fine if both source and destination are
	 * aligned on the same boundary. However, if they are aligned on
	 * different boundaries shifts will be necessary. This might result in
	 * bad performance on MicroBlaze systems without a barrel shifter.
	 */
	const uint32_t *i_src;
	uint32_t *i_dst;

	if (likely(c >= 4)) {
		unsigned  value, buf_hold;

		/* Align the destination to a word boundary. */
		/* This is done in an endian independent manner. */
		switch ((unsigned long)dst & 3) {
		case 1:
			*dst++ = *src++;
			--c;
			fallthrough;
		case 2:
			*dst++ = *src++;
			--c;
			fallthrough;
		case 3:
			*dst++ = *src++;
			--c;
		}

		i_dst = (void *)dst;

		/* Choose a copy scheme based on the source */
		/* alignment relative to destination. */
		switch ((unsigned long)src & 3) {
		case 0x0:	/* Both byte offsets are aligned */
			i_src  = (const void *)src;

			for (; c >= 4; c -= 4)
				*i_dst++ = *i_src++;

			src  = (const void *)i_src;
			break;
		case 0x1:	/* Unaligned - Off by 1 */
			/* Word align the source */
			i_src = (const void *) ((unsigned)src & ~3);
#ifndef __MICROBLAZEEL__
			/* Load the holding buffer */
			buf_hold = *i_src++ << 8;

			for (; c >= 4; c -= 4) {
				value = *i_src++;
				*i_dst++ = buf_hold | value >> 24;
				buf_hold = value << 8;
			}
#else
			/* Load the holding buffer */
			buf_hold = (*i_src++ & 0xFFFFFF00) >> 8;

			for (; c >= 4; c -= 4) {
				value = *i_src++;
				*i_dst++ = buf_hold | ((value & 0xFF) << 24);
				buf_hold = (value & 0xFFFFFF00) >> 8;
			}
#endif
			/* Realign the source */
			src = (const void *)i_src;
			src -= 3;
			break;
		case 0x2:	/* Unaligned - Off by 2 */
			/* Word align the source */
			i_src = (const void *) ((unsigned)src & ~3);
#ifndef __MICROBLAZEEL__
			/* Load the holding buffer */
			buf_hold = *i_src++ << 16;

			for (; c >= 4; c -= 4) {
				value = *i_src++;
				*i_dst++ = buf_hold | value >> 16;
				buf_hold = value << 16;
			}
#else
			/* Load the holding buffer */
			buf_hold = (*i_src++ & 0xFFFF0000) >> 16;

			for (; c >= 4; c -= 4) {
				value = *i_src++;
				*i_dst++ = buf_hold | ((value & 0xFFFF) << 16);
				buf_hold = (value & 0xFFFF0000) >> 16;
			}
#endif
			/* Realign the source */
			src = (const void *)i_src;
			src -= 2;
			break;
		case 0x3:	/* Unaligned - Off by 3 */
			/* Word align the source */
			i_src = (const void *) ((unsigned)src & ~3);
#ifndef __MICROBLAZEEL__
			/* Load the holding buffer */
			buf_hold = *i_src++ << 24;

			for (; c >= 4; c -= 4) {
				value = *i_src++;
				*i_dst++ = buf_hold | value >> 8;
				buf_hold = value << 24;
			}
#else
			/* Load the holding buffer */
			buf_hold = (*i_src++ & 0xFF000000) >> 24;

			for (; c >= 4; c -= 4) {
				value = *i_src++;
				*i_dst++ = buf_hold | ((value & 0xFFFFFF) << 8);
				buf_hold = (value & 0xFF000000) >> 24;
			}
#endif
			/* Realign the source */
			src = (const void *)i_src;
			src -= 1;
			break;
		}
		dst = (void *)i_dst;
	}

	/* Finish off any remaining bytes */
	/* simple fast copy, ... unless a cache boundary is crossed */
	switch (c) {
	case 3:
		*dst++ = *src++;
		fallthrough;
	case 2:
		*dst++ = *src++;
		fallthrough;
	case 1:
		*dst++ = *src++;
	}

	return v_dst;
}
#endif /* CONFIG_OPT_LIB_FUNCTION */
EXPORT_SYMBOL(memcpy);
#endif /* __HAVE_ARCH_MEMCPY */
