/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2007 John Williams
 *
 * Reasonably optimised generic C-code for memset on Microblaze
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

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/string.h>

#ifdef __HAVE_ARCH_MEMSET
#ifndef CONFIG_OPT_LIB_FUNCTION
void *memset(void *v_src, int c, __kernel_size_t n)
{
	char *src = v_src;

	/* Truncate c to 8 bits */
	c = (c & 0xFF);

	/* Simple, byte oriented memset or the rest of count. */
	while (n--)
		*src++ = c;

	return v_src;
}
#else /* CONFIG_OPT_LIB_FUNCTION */
void *memset(void *v_src, int c, __kernel_size_t n)
{
	char *src = v_src;
	uint32_t *i_src;
	uint32_t w32 = 0;

	/* Truncate c to 8 bits */
	c = (c & 0xFF);

	if (unlikely(c)) {
		/* Make a repeating word out of it */
		w32 = c;
		w32 |= w32 << 8;
		w32 |= w32 << 16;
	}

	if (likely(n >= 4)) {
		/* Align the destination to a word boundary */
		/* This is done in an endian independant manner */
		switch ((unsigned) src & 3) {
		case 1:
			*src++ = c;
			--n;
		case 2:
			*src++ = c;
			--n;
		case 3:
			*src++ = c;
			--n;
		}

		i_src  = (void *)src;

		/* Do as many full-word copies as we can */
		for (; n >= 4; n -= 4)
			*i_src++ = w32;

		src  = (void *)i_src;
	}

	/* Simple, byte oriented memset or the rest of count. */
	while (n--)
		*src++ = c;

	return v_src;
}
#endif /* CONFIG_OPT_LIB_FUNCTION */
EXPORT_SYMBOL(memset);
#endif /* __HAVE_ARCH_MEMSET */
