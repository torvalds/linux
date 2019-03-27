/*
 * Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *
 *	$Id: bitops.h,v 1.3 2013-11-22 20:51:31 ca Exp $
 */

#ifndef	SM_BITOPS_H
# define SM_BITOPS_H

/*
**  Data structure for bit maps.
**
**	Each bit in this map can be referenced by an ascii character.
**	This is 256 possible bits, or 32 8-bit bytes.
*/

# define BITMAPBITS	256	/* number of bits in a bit map */
# define BYTEBITS	8	/* number of bits in a byte */
# define BITMAPBYTES	(BITMAPBITS / BYTEBITS)	/* number of bytes in bit map */
# define BITMAPMAX	((BITMAPBYTES / sizeof (int)) - 1)

/* internal macros */

/* make sure this index never leaves the allowed range: 0 to BITMAPMAX */
# define _BITWORD(bit)	(((unsigned char)(bit) / (BYTEBITS * sizeof (int))) & BITMAPMAX)
# define _BITBIT(bit)	((unsigned int)1 << ((unsigned char)(bit) % (BYTEBITS * sizeof (int))))

typedef unsigned int	BITMAP256[BITMAPBYTES / sizeof (int)];

/* properly case and truncate bit */
# define bitidx(bit)		((unsigned int) (bit) & 0xff)

/* test bit number N */
# define bitnset(bit, map)	((map)[_BITWORD(bit)] & _BITBIT(bit))

/* set bit number N */
# define setbitn(bit, map)	(map)[_BITWORD(bit)] |= _BITBIT(bit)

/* clear bit number N */
# define clrbitn(bit, map)	(map)[_BITWORD(bit)] &= ~_BITBIT(bit)

/* clear an entire bit map */
# define clrbitmap(map)		memset((char *) map, '\0', BITMAPBYTES)

/* bit hacking */
# define bitset(bit, word)	(((word) & (bit)) != 0)

#endif /* ! SM_BITOPS_H */
