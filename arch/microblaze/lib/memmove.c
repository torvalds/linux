/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2007 John Williams
 *
 * Reasonably optimised generic C-code for memcpy on Microblaze
 * This is generic C code to do efficient, alignment-aware memmove.
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

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/string.h>

#ifdef __HAVE_ARCH_MEMMOVE
void *memmove(void *v_dst, const void *v_src, __kernel_size_t c)
{
	const char *src = v_src;
	char *dst = v_dst;

#ifdef CONFIG_OPT_LIB_FUNCTION
	const uint32_t *i_src;
	uint32_t *i_dst;
#endif

	if (!c)
		return v_dst;

	/* Use memcpy when source is higher than dest */
	if (v_dst <= v_src)
		return memcpy(v_dst, v_src, c);

#ifndef CONFIG_OPT_LIB_FUNCTION
	/* copy backwards, from end to beginning */
	src += c;
	dst += c;

	/* Simple, byte oriented memmove. */
	while (c--)
		*--dst = *--src;

	return v_dst;
#else
	/* The following code tries to optimize the copy by using unsigned
	 * alignment. This will work fine if both source and destination are
	 * aligned on the same boundary. However, if they are aligned on
	 * different boundaries shifts will be necessary. This might result in
	 * bad performance on MicroBlaze systems without a barrel shifter.
	 */
	/* FIXME this part needs more test */
	/* Do a descending copy - this is a bit trickier! */
	dst += c;
	src += c;

	if (c >= 4) {
		unsigned  value, buf_hold;

		/* Align the destination to a word boundry. */
		/* This is done in an endian independant manner. */

		switch ((unsigned long)dst & 3) {
		case 3:
			*--dst = *--src;
			--c;
		case 2:
			*--dst = *--src;
			--c;
		case 1:
			*--dst = *--src;
			--c;
		}

		i_dst = (void *)dst;
		/* Choose a copy scheme based on the source */
		/* alignment relative to dstination. */
		switch ((unsigned long)src & 3) {
		case 0x0:	/* Both byte offsets are aligned */

			i_src  = (const void *)src;

			for (; c >= 4; c -= 4)
				*--i_dst = *--i_src;

			src  = (const void *)i_src;
			break;
		case 0x1:	/* Unaligned - Off by 1 */
			/* Word align the source */
			i_src = (const void *) (((unsigned)src + 4) & ~3);

			/* Load the holding buffer */
			buf_hold = *--i_src >> 24;

			for (; c >= 4; c -= 4) {
				value = *--i_src;
				*--i_dst = buf_hold << 8 | value;
				buf_hold = value >> 24;
			}

			/* Realign the source */
			src = (const void *)i_src;
			src += 1;
			break;
		case 0x2:	/* Unaligned - Off by 2 */
			/* Word align the source */
			i_src = (const void *) (((unsigned)src + 4) & ~3);

			/* Load the holding buffer */
			buf_hold = *--i_src >> 16;

			for (; c >= 4; c -= 4) {
				value = *--i_src;
				*--i_dst = buf_hold << 16 | value;
				buf_hold = value >> 16;
			}

			/* Realign the source */
			src = (const void *)i_src;
			src += 2;
			break;
		case 0x3:	/* Unaligned - Off by 3 */
			/* Word align the source */
			i_src = (const void *) (((unsigned)src + 4) & ~3);

			/* Load the holding buffer */
			buf_hold = *--i_src >> 8;

			for (; c >= 4; c -= 4) {
				value = *--i_src;
				*--i_dst = buf_hold << 24 | value;
				buf_hold = value >> 8;
			}

			/* Realign the source */
			src = (const void *)i_src;
			src += 3;
			break;
		}
		dst = (void *)i_dst;
	}

	/* simple fast copy, ... unless a cache boundry is crossed */
	/* Finish off any remaining bytes */
	switch (c) {
	case 4:
		*--dst = *--src;
	case 3:
		*--dst = *--src;
	case 2:
		*--dst = *--src;
	case 1:
		*--dst = *--src;
	}
	return v_dst;
#endif
}
EXPORT_SYMBOL(memmove);
#endif /* __HAVE_ARCH_MEMMOVE */
