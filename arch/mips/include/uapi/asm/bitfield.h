/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef __UAPI_ASM_BITFIELD_H
#define __UAPI_ASM_BITFIELD_H

/*
 *  * Damn ...  bitfields depend from byteorder :-(
 *   */
#ifdef __MIPSEB__
#define __BITFIELD_FIELD(field, more)					\
	field;								\
	more

#elif defined(__MIPSEL__)

#define __BITFIELD_FIELD(field, more)					\
	more								\
	field;

#else /* !defined (__MIPSEB__) && !defined (__MIPSEL__) */
#error "MIPS but neither __MIPSEL__ nor __MIPSEB__?"
#endif

#endif /* __UAPI_ASM_BITFIELD_H */
