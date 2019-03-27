/*-
 * Copyright (c) 2006-2011 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <assert.h>
#include <libelf.h>
#include <string.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_convert.m4 3632 2018-10-10 21:12:43Z jkoshy $");

/* WARNING: GENERATED FROM __file__. */

divert(-1)

# Generate conversion routines for converting between in-memory and
# file representations of Elf data structures.
#
# These conversions use the type information defined in `elf_types.m4'.

include(SRCDIR`/elf_types.m4')

# For the purposes of generating conversion code, ELF types may be
# classified according to the following characteristics:
#
# 1. Whether the ELF type can be directly mapped to an integral C
#    language type.  For example, the ELF_T_WORD type maps directly to
#    a 'uint32_t', but ELF_T_GNUHASH lacks a matching C type.
#
# 2. Whether the type has word size dependent variants.  For example,
#    ELT_T_EHDR is represented using C types Elf32_Ehdr and El64_Ehdr,
#    and the ELF_T_ADDR and ELF_T_OFF types have integral C types that
#    can be 32- or 64- bit wide.
#
# 3. Whether the ELF types has a fixed representation or not.  For
#    example, the ELF_T_SYM type has a fixed size file representation,
#    some types like ELF_T_NOTE and ELF_T_GNUHASH use a variable size
#    representation.
#
# We use m4 macros to generate conversion code for ELF types that have
# a fixed size representation.  Conversion functions for the remaining
# types are coded by hand.
#
#* Handling File and Memory Representations
#
# `In-memory' representations of an Elf data structure use natural
# alignments and native byte ordering.  This allows pointer arithmetic
# and casting to work as expected.  On the other hand, the `file'
# representation of an ELF data structure could possibly be packed
# tighter than its `in-memory' representation, and could be of a
# differing byte order.  Reading ELF objects that are members of `ar'
# archives present an additional complication: `ar' pads file data to
# even addresses, so file data structures in an archive member
# residing inside an `ar' archive could be at misaligned memory
# addresses when brought into memory.
#
# In summary, casting the `char *' pointers that point to memory
# representations (i.e., source pointers for the *_tof() functions and
# the destination pointers for the *_tom() functions), is safe, as
# these pointers should be correctly aligned for the memory type
# already.  However, pointers to file representations have to be
# treated as being potentially unaligned and no casting can be done.

# NOCVT(TYPE) -- Do not generate the cvt[] structure entry for TYPE
define(`NOCVT',`define(`NOCVT_'$1,1)')

# NOFUNC(TYPE) -- Do not generate a conversion function for TYPE
define(`NOFUNC',`define(`NOFUNC_'$1,1)')

# IGNORE(TYPE) -- Completely ignore the type.
define(`IGNORE',`NOCVT($1)NOFUNC($1)')

# Mark ELF types that should not be processed by the M4 macros below.

# Types for which we use functions with non-standard names.
IGNORE(`BYTE')			# Uses a wrapper around memcpy().
IGNORE(`NOTE')			# Not a fixed size type.

# Types for which we supply hand-coded functions.
NOFUNC(`GNUHASH')		# A type with complex internal structure.
NOFUNC(`VDEF')			# See MAKE_VERSION_CONVERTERS below.
NOFUNC(`VNEED')			# ..

# Unimplemented types.
IGNORE(`MOVEP')

# ELF types that don't exist in a 32-bit world.
NOFUNC(`XWORD32')
NOFUNC(`SXWORD32')

# `Primitive' ELF types are those that are an alias for an integral
# type.  As they have no internal structure, they can be copied using
# a `memcpy()', and byteswapped in straightforward way.
#
# Mark all ELF types that directly map to integral C types.
define(`PRIM_ADDR',	1)
define(`PRIM_BYTE',	1)
define(`PRIM_HALF',	1)
define(`PRIM_LWORD',	1)
define(`PRIM_OFF',	1)
define(`PRIM_SWORD',	1)
define(`PRIM_SXWORD',	1)
define(`PRIM_WORD',	1)
define(`PRIM_XWORD',	1)

# Note the primitive types that are size-dependent.
define(`SIZEDEP_ADDR',	1)
define(`SIZEDEP_OFF',	1)

# Generate conversion functions for primitive types.
#
# Macro use: MAKEPRIMFUNCS(ELFTYPE,CTYPE,TYPESIZE,SYMSIZE)
# `$1': Name of the ELF type.
# `$2': C structure name suffix.
# `$3': ELF class specifier for types, one of [`32', `64'].
# `$4': Additional ELF class specifier, one of [`', `32', `64'].
#
# Generates a pair of conversion functions.
define(`MAKEPRIMFUNCS',`
static int
_libelf_cvt_$1$4_tof(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf$3_$2 t, *s = (Elf$3_$2 *) (uintptr_t) src;
	size_t c;

	(void) dsz;

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*s));
		return (1);
	}

	for (c = 0; c < count; c++) {
		t = *s++;
		SWAP_$1$4(t);
		WRITE_$1$4(dst,t);
	}

	return (1);
}

static int
_libelf_cvt_$1$4_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf$3_$2 t, *d = (Elf$3_$2 *) (uintptr_t) dst;
	size_t c;

	if (dsz < count * sizeof(Elf$3_$2))
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count * sizeof(*d));
		return (1);
	}

	for (c = 0; c < count; c++) {
		READ_$1$4(src,t);
		SWAP_$1$4(t);
		*d++ = t;
	}

	return (1);
}
')

#
# Handling composite ELF types
#

# SWAP_FIELD(FIELDNAME,ELFTYPE) -- Generate code to swap one field.
define(`SWAP_FIELD',
  `ifdef(`SIZEDEP_'$2,
    `SWAP_$2'SZ()`(t.$1);
			',
    `SWAP_$2(t.$1);
			')')

# SWAP_MEMBERS(STRUCT) -- Iterate over a structure definition.
define(`SWAP_MEMBERS',
  `ifelse($#,1,`/**/',
     `SWAP_FIELD($1)SWAP_MEMBERS(shift($@))')')

# SWAP_STRUCT(CTYPE,SIZE) -- Generate code to swap an ELF structure.
define(`SWAP_STRUCT',
  `pushdef(`SZ',$2)/* Swap an Elf$2_$1 */
			SWAP_MEMBERS(Elf$2_$1_DEF)popdef(`SZ')')

# WRITE_FIELD(ELFTYPE,FIELDNAME) -- Generate code to write one field.
define(`WRITE_FIELD',
  `ifdef(`SIZEDEP_'$2,
    `WRITE_$2'SZ()`(dst,t.$1);
		',
    `WRITE_$2(dst,t.$1);
		')')

# WRITE_MEMBERS(ELFTYPELIST) -- Iterate over a structure definition.
define(`WRITE_MEMBERS',
  `ifelse($#,1,`/**/',
    `WRITE_FIELD($1)WRITE_MEMBERS(shift($@))')')

# WRITE_STRUCT(CTYPE,SIZE) -- Generate code to write out an ELF structure.
define(`WRITE_STRUCT',
  `pushdef(`SZ',$2)/* Write an Elf$2_$1 */
		WRITE_MEMBERS(Elf$2_$1_DEF)popdef(`SZ')')

# READ_FIELD(ELFTYPE,CTYPE) -- Generate code to read one field.
define(`READ_FIELD',
  `ifdef(`SIZEDEP_'$2,
    `READ_$2'SZ()`(s,t.$1);
		',
    `READ_$2(s,t.$1);
		')')

# READ_MEMBERS(ELFTYPELIST) -- Iterate over a structure definition.
define(`READ_MEMBERS',
  `ifelse($#,1,`/**/',
    `READ_FIELD($1)READ_MEMBERS(shift($@))')')

# READ_STRUCT(CTYPE,SIZE) -- Generate code to read an ELF structure.
define(`READ_STRUCT',
  `pushdef(`SZ',$2)/* Read an Elf$2_$1 */
		READ_MEMBERS(Elf$2_$1_DEF)popdef(`SZ')')


# MAKECOMPFUNCS -- Generate converters for composite ELF structures.
#
# When converting data to file representation, the source pointer will
# be naturally aligned for a data structure's in-memory
# representation.  When converting data to memory, the destination
# pointer will be similarly aligned.
#
# For in-place conversions, when converting to file representations,
# the source buffer is large enough to hold `file' data.  When
# converting from file to memory, we need to be careful to work
# `backwards', to avoid overwriting unconverted data.
#
# Macro use:
# `$1': Name of the ELF type.
# `$2': C structure name suffix.
# `$3': ELF class specifier, one of [`', `32', `64']
define(`MAKECOMPFUNCS', `ifdef(`NOFUNC_'$1$3,`',`
static int
_libelf_cvt_$1$3_tof(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf$3_$2	t, *s;
	size_t c;

	(void) dsz;

	s = (Elf$3_$2 *) (uintptr_t) src;
	for (c = 0; c < count; c++) {
		t = *s++;
		if (byteswap) {
			SWAP_STRUCT($2,$3)
		}
		WRITE_STRUCT($2,$3)
	}

	return (1);
}

static int
_libelf_cvt_$1$3_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf$3_$2	t, *d;
	unsigned char	*s,*s0;
	size_t		fsz;

	fsz = elf$3_fsize(ELF_T_$1, (size_t) 1, EV_CURRENT);
	d   = ((Elf$3_$2 *) (uintptr_t) dst) + (count - 1);
	s0  = src + (count - 1) * fsz;

	if (dsz < count * sizeof(Elf$3_$2))
		return (0);

	while (count--) {
		s = s0;
		READ_STRUCT($2,$3)
		if (byteswap) {
			SWAP_STRUCT($2,$3)
		}
		*d-- = t; s0 -= fsz;
	}

	return (1);
}
')')

# MAKE_TYPE_CONVERTER(ELFTYPE,CTYPE)
#
# Make type convertor functions from the type definition
# of the ELF type:
# - Skip convertors marked as `NOFUNC'.
# - Invoke `MAKEPRIMFUNCS' or `MAKECOMPFUNCS' as appropriate.
define(`MAKE_TYPE_CONVERTER',
  `ifdef(`NOFUNC_'$1,`',
    `ifdef(`PRIM_'$1,
      `ifdef(`SIZEDEP_'$1,
	`MAKEPRIMFUNCS($1,$2,32,32)dnl
	 MAKEPRIMFUNCS($1,$2,64,64)',
	`MAKEPRIMFUNCS($1,$2,64)')',
      `MAKECOMPFUNCS($1,$2,32)dnl
       MAKECOMPFUNCS($1,$2,64)')')')

# MAKE_TYPE_CONVERTERS(ELFTYPELIST) -- Generate conversion functions.
define(`MAKE_TYPE_CONVERTERS',
  `ifelse($#,1,`',
    `MAKE_TYPE_CONVERTER($1)MAKE_TYPE_CONVERTERS(shift($@))')')


#
# Macros to generate entries for the table of convertors.
#

# CONV(ELFTYPE,SIZE,DIRECTION)
#
# Generate the name of a convertor function.
define(`CONV',
  `ifdef(`NOFUNC_'$1$2,
    `.$3$2 = NULL',
    `ifdef(`PRIM_'$1,
      `ifdef(`SIZEDEP_'$1,
	`.$3$2 = _libelf_cvt_$1$2_$3',
	`.$3$2 = _libelf_cvt_$1_$3')',
      `.$3$2 = _libelf_cvt_$1$2_$3')')')

# CONVERTER_NAME(ELFTYPE)
#
# Generate the contents of one `struct cvt' instance.
define(`CONVERTER_NAME',
  `ifdef(`NOCVT_'$1,`',
    `	[ELF_T_$1] = {
		CONV($1,32,tof),
		CONV($1,32,tom),
		CONV($1,64,tof),
		CONV($1,64,tom)
	},

')')

# CONVERTER_NAMES(ELFTYPELIST)
#
# Generate the `struct cvt[]' array.
define(`CONVERTER_NAMES',
  `ifelse($#,1,`',
    `CONVERTER_NAME($1)CONVERTER_NAMES(shift($@))')')

#
# Handling ELF version sections.
#

# _FSZ(FIELD,BASETYPE) - return the file size for a field.
define(`_FSZ',
  `ifelse($2,`HALF',2,
     $2,`WORD',4)')

# FSZ(STRUCT) - determine the file size of a structure.
define(`FSZ',
  `ifelse($#,1,0,
    `eval(_FSZ($1) + FSZ(shift($@)))')')

# MAKE_VERSION_CONVERTERS(TYPE,BASE,AUX,PFX) -- Generate conversion
# functions for versioning structures.
define(`MAKE_VERSION_CONVERTERS',
  `MAKE_VERSION_CONVERTER($1,$2,$3,$4,32)
   MAKE_VERSION_CONVERTER($1,$2,$3,$4,64)')

# MAKE_VERSION_CONVERTOR(TYPE,CBASE,CAUX,PFX,SIZE) -- Generate a
# conversion function.
define(`MAKE_VERSION_CONVERTER',`
static int
_libelf_cvt_$1$5_tof(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf$5_$2	t;
	Elf$5_$3	a;
	const size_t	verfsz = FSZ(Elf$5_$2_DEF);
	const size_t	auxfsz = FSZ(Elf$5_$3_DEF);
	const size_t	vermsz = sizeof(Elf$5_$2);
	const size_t	auxmsz = sizeof(Elf$5_$3);
	unsigned char * const dstend = dst + dsz;
	unsigned char * const srcend = src + count;
	unsigned char	*dtmp, *dstaux, *srcaux;
	Elf$5_Word	aux, anext, cnt, vnext;

	for (dtmp = dst, vnext = ~0U;
	     vnext != 0 && dtmp + verfsz <= dstend && src + vermsz <= srcend;
	     dtmp += vnext, src += vnext) {

		/* Read in an Elf$5_$2 structure. */
		t = *((Elf$5_$2 *) (uintptr_t) src);

		aux = t.$4_aux;
		cnt = t.$4_cnt;
		vnext = t.$4_next;

		if (byteswap) {
			SWAP_STRUCT($2, $5)
		}

		dst = dtmp;
		WRITE_STRUCT($2, $5)

		if (aux < verfsz)
			return (0);

		/* Process AUX entries. */
		for (anext = ~0U, dstaux = dtmp + aux, srcaux = src + aux;
		     cnt != 0 && anext != 0 && dstaux + auxfsz <= dstend &&
			srcaux + auxmsz <= srcend;
		     dstaux += anext, srcaux += anext, cnt--) {

			/* Read in an Elf$5_$3 structure. */
			a = *((Elf$5_$3 *) (uintptr_t) srcaux);
			anext = a.$4a_next;

			if (byteswap) {
				pushdef(`t',`a')SWAP_STRUCT($3, $5)popdef(`t')
			}

			dst = dstaux;
			pushdef(`t',`a')WRITE_STRUCT($3, $5)popdef(`t')
		}

		if (anext || cnt)
			return (0);
	}

	if (vnext)
		return (0);

	return (1);
}

static int
_libelf_cvt_$1$5_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	Elf$5_$2	t, *dp;
	Elf$5_$3	a, *ap;
	const size_t	verfsz = FSZ(Elf$5_$2_DEF);
	const size_t	auxfsz = FSZ(Elf$5_$3_DEF);
	const size_t	vermsz = sizeof(Elf$5_$2);
	const size_t	auxmsz = sizeof(Elf$5_$3);
	unsigned char * const dstend = dst + dsz;
	unsigned char * const srcend = src + count;
	unsigned char	*dstaux, *s, *srcaux, *stmp;
	Elf$5_Word	aux, anext, cnt, vnext;

	for (stmp = src, vnext = ~0U;
	     vnext != 0 && stmp + verfsz <= srcend && dst + vermsz <= dstend;
	     stmp += vnext, dst += vnext) {

		/* Read in a $1 structure. */
		s = stmp;
		READ_STRUCT($2, $5)
		if (byteswap) {
			SWAP_STRUCT($2, $5)
		}

		dp = (Elf$5_$2 *) (uintptr_t) dst;
		*dp = t;

		aux = t.$4_aux;
		cnt = t.$4_cnt;
		vnext = t.$4_next;

		if (aux < vermsz)
			return (0);

		/* Process AUX entries. */
		for (anext = ~0U, dstaux = dst + aux, srcaux = stmp + aux;
		     cnt != 0 && anext != 0 && dstaux + auxmsz <= dstend &&
			srcaux + auxfsz <= srcend;
		     dstaux += anext, srcaux += anext, cnt--) {

			s = srcaux;
			pushdef(`t',`a')READ_STRUCT($3, $5)popdef(`t')

			if (byteswap) {
				pushdef(`t',`a')SWAP_STRUCT($3, $5)popdef(`t')
			}

			anext = a.$4a_next;

			ap = ((Elf$5_$3 *) (uintptr_t) dstaux);
			*ap = a;
		}

		if (anext || cnt)
			return (0);
	}

	if (vnext)
		return (0);

	return (1);
}')

divert(0)

/*
 * C macros to byte swap integral quantities.
 */

#define	SWAP_BYTE(X)	do { (void) (X); } while (0)
#define	SWAP_IDENT(X)	do { (void) (X); } while (0)
#define	SWAP_HALF(X)	do {						\
		uint16_t _x = (uint16_t) (X);				\
		uint32_t _t = _x & 0xFFU;				\
		_t <<= 8U; _x >>= 8U; _t |= _x & 0xFFU;			\
		(X) = (uint16_t) _t;					\
	} while (0)
#define	_SWAP_WORD(X, T) do {						\
		uint32_t _x = (uint32_t) (X);				\
		uint32_t _t = _x & 0xFF;				\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		(X) = (T) _t;						\
	} while (0)
#define	SWAP_ADDR32(X)	_SWAP_WORD(X, Elf32_Addr)
#define	SWAP_OFF32(X)	_SWAP_WORD(X, Elf32_Off)
#define	SWAP_SWORD(X)	_SWAP_WORD(X, Elf32_Sword)
#define	SWAP_WORD(X)	_SWAP_WORD(X, Elf32_Word)
#define	_SWAP_WORD64(X, T) do {						\
		uint64_t _x = (uint64_t) (X);				\
		uint64_t _t = _x & 0xFF;				\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		_t <<= 8; _x >>= 8; _t |= _x & 0xFF;			\
		(X) = (T) _t;						\
	} while (0)
#define	SWAP_ADDR64(X)	_SWAP_WORD64(X, Elf64_Addr)
#define	SWAP_LWORD(X)	_SWAP_WORD64(X, Elf64_Lword)
#define	SWAP_OFF64(X)	_SWAP_WORD64(X, Elf64_Off)
#define	SWAP_SXWORD(X)	_SWAP_WORD64(X, Elf64_Sxword)
#define	SWAP_XWORD(X)	_SWAP_WORD64(X, Elf64_Xword)

/*
 * C macros to write out various integral values.
 *
 * Note:
 * - The destination pointer could be unaligned.
 * - Values are written out in native byte order.
 * - The destination pointer is incremented after the write.
 */
#define	WRITE_BYTE(P,X) do {						\
		unsigned char *const _p = (unsigned char *) (P);	\
		_p[0]		= (unsigned char) (X);			\
		(P)		= _p + 1;				\
	} while (0)
#define	WRITE_HALF(P,X)	do {						\
		uint16_t _t	= (X);					\
		unsigned char *const _p	= (unsigned char *) (P);	\
		const unsigned char *const _q = (unsigned char *) &_t;	\
		_p[0]		= _q[0];				\
		_p[1]		= _q[1];				\
		(P)		= _p + 2;				\
	} while (0)
#define	WRITE_WORD(P,X) do {						\
		uint32_t _t	= (uint32_t) (X);			\
		unsigned char *const _p	= (unsigned char *) (P);	\
		const unsigned char *const _q = (unsigned char *) &_t;	\
		_p[0]		= _q[0];				\
		_p[1]		= _q[1];				\
		_p[2]		= _q[2];				\
		_p[3]		= _q[3];				\
		(P)		= _p + 4;				\
	} while (0)
#define	WRITE_ADDR32(P,X)	WRITE_WORD(P,X)
#define	WRITE_OFF32(P,X)	WRITE_WORD(P,X)
#define	WRITE_SWORD(P,X)	WRITE_WORD(P,X)
#define	WRITE_WORD64(P,X)	do {					\
		uint64_t _t	= (uint64_t) (X);			\
		unsigned char *const _p	= (unsigned char *) (P);	\
		const unsigned char *const _q = (unsigned char *) &_t;	\
		_p[0]		= _q[0];				\
		_p[1]		= _q[1];				\
		_p[2]		= _q[2];				\
		_p[3]		= _q[3];				\
		_p[4]		= _q[4];				\
		_p[5]		= _q[5];				\
		_p[6]		= _q[6];				\
		_p[7]		= _q[7];				\
		(P)		= _p + 8;				\
	} while (0)
#define	WRITE_ADDR64(P,X)	WRITE_WORD64(P,X)
#define	WRITE_LWORD(P,X)	WRITE_WORD64(P,X)
#define	WRITE_OFF64(P,X)	WRITE_WORD64(P,X)
#define	WRITE_SXWORD(P,X)	WRITE_WORD64(P,X)
#define	WRITE_XWORD(P,X)	WRITE_WORD64(P,X)
#define	WRITE_IDENT(P,X)	do {					\
		(void) memcpy((P), (X), sizeof((X)));			\
		(P)		= (P) + EI_NIDENT;			\
	} while (0)

/*
 * C macros to read in various integral values.
 *
 * Note:
 * - The source pointer could be unaligned.
 * - Values are read in native byte order.
 * - The source pointer is incremented appropriately.
 */

#define	READ_BYTE(P,X)	do {						\
		const unsigned char *const _p =				\
			(const unsigned char *) (P);			\
		(X)		= _p[0];				\
		(P)		= (P) + 1;				\
	} while (0)
#define	READ_HALF(P,X)	do {						\
		uint16_t _t;						\
		unsigned char *const _q = (unsigned char *) &_t;	\
		const unsigned char *const _p =				\
			(const unsigned char *) (P);			\
		_q[0]		= _p[0];				\
		_q[1]		= _p[1];				\
		(P)		= (P) + 2;				\
		(X)		= _t;					\
	} while (0)
#define	_READ_WORD(P,X,T) do {						\
		uint32_t _t;						\
		unsigned char *const _q = (unsigned char *) &_t;	\
		const unsigned char *const _p =				\
			(const unsigned char *) (P);			\
		_q[0]		= _p[0];				\
		_q[1]		= _p[1];				\
		_q[2]		= _p[2];				\
		_q[3]		= _p[3];				\
		(P)		= (P) + 4;				\
		(X)		= (T) _t;				\
	} while (0)
#define	READ_ADDR32(P,X)	_READ_WORD(P, X, Elf32_Addr)
#define	READ_OFF32(P,X)		_READ_WORD(P, X, Elf32_Off)
#define	READ_SWORD(P,X)		_READ_WORD(P, X, Elf32_Sword)
#define	READ_WORD(P,X)		_READ_WORD(P, X, Elf32_Word)
#define	_READ_WORD64(P,X,T)	do {					\
		uint64_t _t;						\
		unsigned char *const _q = (unsigned char *) &_t;	\
		const unsigned char *const _p =				\
			(const unsigned char *) (P);			\
		_q[0]		= _p[0];				\
		_q[1]		= _p[1];				\
		_q[2]		= _p[2];				\
		_q[3]		= _p[3];				\
		_q[4]		= _p[4];				\
		_q[5]		= _p[5];				\
		_q[6]		= _p[6];				\
		_q[7]		= _p[7];				\
		(P)		= (P) + 8;				\
		(X)		= (T) _t;				\
	} while (0)
#define	READ_ADDR64(P,X)	_READ_WORD64(P, X, Elf64_Addr)
#define	READ_LWORD(P,X)		_READ_WORD64(P, X, Elf64_Lword)
#define	READ_OFF64(P,X)		_READ_WORD64(P, X, Elf64_Off)
#define	READ_SXWORD(P,X)	_READ_WORD64(P, X, Elf64_Sxword)
#define	READ_XWORD(P,X)		_READ_WORD64(P, X, Elf64_Xword)
#define	READ_IDENT(P,X)		do {					\
		(void) memcpy((X), (P), sizeof((X)));			\
		(P)		= (P) + EI_NIDENT;			\
	} while (0)

#define	ROUNDUP2(V,N)	(V) = ((((V) + (N) - 1)) & ~((N) - 1))

/*[*/
MAKE_TYPE_CONVERTERS(ELF_TYPE_LIST)
MAKE_VERSION_CONVERTERS(VDEF,Verdef,Verdaux,vd)
MAKE_VERSION_CONVERTERS(VNEED,Verneed,Vernaux,vn)
/*]*/

/*
 * Sections of type ELF_T_BYTE are never byteswapped, consequently a
 * simple memcpy suffices for both directions of conversion.
 */

static int
_libelf_cvt_BYTE_tox(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	(void) byteswap;
	if (dsz < count)
		return (0);
	if (dst != src)
		(void) memcpy(dst, src, count);
	return (1);
}

/*
 * Sections of type ELF_T_GNUHASH start with a header containing 4 32-bit
 * words.  Bloom filter data comes next, followed by hash buckets and the
 * hash chain.
 *
 * Bloom filter words are 64 bit wide on ELFCLASS64 objects and are 32 bit
 * wide on ELFCLASS32 objects.  The other objects in this section are 32
 * bits wide.
 *
 * Argument `srcsz' denotes the number of bytes to be converted.  In the
 * 32-bit case we need to translate `srcsz' to a count of 32-bit words.
 */

static int
_libelf_cvt_GNUHASH32_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t srcsz, int byteswap)
{
	return (_libelf_cvt_WORD_tom(dst, dsz, src, srcsz / sizeof(uint32_t),
		byteswap));
}

static int
_libelf_cvt_GNUHASH32_tof(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t srcsz, int byteswap)
{
	return (_libelf_cvt_WORD_tof(dst, dsz, src, srcsz / sizeof(uint32_t),
		byteswap));
}

static int
_libelf_cvt_GNUHASH64_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t srcsz, int byteswap)
{
	size_t sz;
	uint64_t t64, *bloom64;
	Elf_GNU_Hash_Header *gh;
	uint32_t n, nbuckets, nchains, maskwords, shift2, symndx, t32;
	uint32_t *buckets, *chains;

	sz = 4 * sizeof(uint32_t);	/* File header is 4 words long. */
	if (dsz < sizeof(Elf_GNU_Hash_Header) || srcsz < sz)
		return (0);

	/* Read in the section header and byteswap if needed. */
	READ_WORD(src, nbuckets);
	READ_WORD(src, symndx);
	READ_WORD(src, maskwords);
	READ_WORD(src, shift2);

	srcsz -= sz;

	if (byteswap) {
		SWAP_WORD(nbuckets);
		SWAP_WORD(symndx);
		SWAP_WORD(maskwords);
		SWAP_WORD(shift2);
	}

	/* Check source buffer and destination buffer sizes. */
	sz = nbuckets * sizeof(uint32_t) + maskwords * sizeof(uint64_t);
	if (srcsz < sz || dsz < sz + sizeof(Elf_GNU_Hash_Header))
		return (0);

	gh = (Elf_GNU_Hash_Header *) (uintptr_t) dst;
	gh->gh_nbuckets  = nbuckets;
	gh->gh_symndx    = symndx;
	gh->gh_maskwords = maskwords;
	gh->gh_shift2    = shift2;

	dsz -= sizeof(Elf_GNU_Hash_Header);
	dst += sizeof(Elf_GNU_Hash_Header);

	bloom64 = (uint64_t *) (uintptr_t) dst;

	/* Copy bloom filter data. */
	for (n = 0; n < maskwords; n++) {
		READ_XWORD(src, t64);
		if (byteswap)
			SWAP_XWORD(t64);
		bloom64[n] = t64;
	}

	/* The hash buckets follows the bloom filter. */
	dst += maskwords * sizeof(uint64_t);
	buckets = (uint32_t *) (uintptr_t) dst;

	for (n = 0; n < nbuckets; n++) {
		READ_WORD(src, t32);
		if (byteswap)
			SWAP_WORD(t32);
		buckets[n] = t32;
	}

	dst += nbuckets * sizeof(uint32_t);

	/* The hash chain follows the hash buckets. */
	dsz -= sz;
	srcsz -= sz;

	if (dsz < srcsz)	/* Destination lacks space. */
		return (0);

	nchains = srcsz / sizeof(uint32_t);
	chains = (uint32_t *) (uintptr_t) dst;

	for (n = 0; n < nchains; n++) {
		READ_WORD(src, t32);
		if (byteswap)
			SWAP_WORD(t32);
		*chains++ = t32;
	}

	return (1);
}

static int
_libelf_cvt_GNUHASH64_tof(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t srcsz, int byteswap)
{
	uint32_t *s32;
	size_t sz, hdrsz;
	uint64_t *s64, t64;
	Elf_GNU_Hash_Header *gh;
	uint32_t maskwords, n, nbuckets, nchains, t0, t1, t2, t3, t32;

	hdrsz = 4 * sizeof(uint32_t);	/* Header is 4x32 bits. */
	if (dsz < hdrsz || srcsz < sizeof(Elf_GNU_Hash_Header))
		return (0);

	gh = (Elf_GNU_Hash_Header *) (uintptr_t) src;

	t0 = nbuckets = gh->gh_nbuckets;
	t1 = gh->gh_symndx;
	t2 = maskwords = gh->gh_maskwords;
	t3 = gh->gh_shift2;

	src   += sizeof(Elf_GNU_Hash_Header);
	srcsz -= sizeof(Elf_GNU_Hash_Header);
	dsz   -= hdrsz;

	sz = gh->gh_nbuckets * sizeof(uint32_t) + gh->gh_maskwords *
	    sizeof(uint64_t);

	if (srcsz < sz || dsz < sz)
		return (0);

	/* Write out the header. */
	if (byteswap) {
		SWAP_WORD(t0);
		SWAP_WORD(t1);
		SWAP_WORD(t2);
		SWAP_WORD(t3);
	}

	WRITE_WORD(dst, t0);
	WRITE_WORD(dst, t1);
	WRITE_WORD(dst, t2);
	WRITE_WORD(dst, t3);

	/* Copy the bloom filter and the hash table. */
	s64 = (uint64_t *) (uintptr_t) src;
	for (n = 0; n < maskwords; n++) {
		t64 = *s64++;
		if (byteswap)
			SWAP_XWORD(t64);
		WRITE_WORD64(dst, t64);
	}

	s32 = (uint32_t *) s64;
	for (n = 0; n < nbuckets; n++) {
		t32 = *s32++;
		if (byteswap)
			SWAP_WORD(t32);
		WRITE_WORD(dst, t32);
	}

	srcsz -= sz;
	dsz   -= sz;

	/* Copy out the hash chains. */
	if (dsz < srcsz)
		return (0);

	nchains = srcsz / sizeof(uint32_t);
	for (n = 0; n < nchains; n++) {
		t32 = *s32++;
		if (byteswap)
			SWAP_WORD(t32);
		WRITE_WORD(dst, t32);
	}

	return (1);
}

/*
 * Elf_Note structures comprise a fixed size header followed by variable
 * length strings.  The fixed size header needs to be byte swapped, but
 * not the strings.
 *
 * Argument `count' denotes the total number of bytes to be converted.
 * The destination buffer needs to be at least `count' bytes in size.
 */
static int
_libelf_cvt_NOTE_tom(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	uint32_t namesz, descsz, type;
	Elf_Note *en;
	size_t sz, hdrsz;

	if (dsz < count)	/* Destination buffer is too small. */
		return (0);

	hdrsz = 3 * sizeof(uint32_t);
	if (count < hdrsz)		/* Source too small. */
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count);
		return (1);
	}

	/* Process all notes in the section. */
	while (count > hdrsz) {
		/* Read the note header. */
		READ_WORD(src, namesz);
		READ_WORD(src, descsz);
		READ_WORD(src, type);

		/* Translate. */
		SWAP_WORD(namesz);
		SWAP_WORD(descsz);
		SWAP_WORD(type);

		/* Copy out the translated note header. */
		en = (Elf_Note *) (uintptr_t) dst;
		en->n_namesz = namesz;
		en->n_descsz = descsz;
		en->n_type = type;

		dsz -= sizeof(Elf_Note);
		dst += sizeof(Elf_Note);
		count -= hdrsz;

		ROUNDUP2(namesz, 4U);
		ROUNDUP2(descsz, 4U);

		sz = namesz + descsz;

		if (count < sz || dsz < sz)	/* Buffers are too small. */
			return (0);

		(void) memcpy(dst, src, sz);

		src += sz;
		dst += sz;

		count -= sz;
		dsz -= sz;
	}

	return (1);
}

static int
_libelf_cvt_NOTE_tof(unsigned char *dst, size_t dsz, unsigned char *src,
    size_t count, int byteswap)
{
	uint32_t namesz, descsz, type;
	Elf_Note *en;
	size_t sz;

	if (dsz < count)
		return (0);

	if (!byteswap) {
		(void) memcpy(dst, src, count);
		return (1);
	}

	while (count > sizeof(Elf_Note)) {

		en = (Elf_Note *) (uintptr_t) src;
		namesz = en->n_namesz;
		descsz = en->n_descsz;
		type = en->n_type;

		sz = namesz;
		ROUNDUP2(sz, 4U);
		sz += descsz;
		ROUNDUP2(sz, 4U);

		SWAP_WORD(namesz);
		SWAP_WORD(descsz);
		SWAP_WORD(type);

		WRITE_WORD(dst, namesz);
		WRITE_WORD(dst, descsz);
		WRITE_WORD(dst, type);

		src += sizeof(Elf_Note);
		count -= sizeof(Elf_Note);

		if (count < sz)
			sz = count;

		(void) memcpy(dst, src, sz);

		src += sz;
		dst += sz;
		count -= sz;
	}

	return (1);
}

struct converters {
	int	(*tof32)(unsigned char *dst, size_t dsz, unsigned char *src,
		    size_t cnt, int byteswap);
	int	(*tom32)(unsigned char *dst, size_t dsz, unsigned char *src,
		    size_t cnt, int byteswap);
	int	(*tof64)(unsigned char *dst, size_t dsz, unsigned char *src,
		    size_t cnt, int byteswap);
	int	(*tom64)(unsigned char *dst, size_t dsz, unsigned char *src,
		    size_t cnt, int byteswap);
};


static struct converters cvt[ELF_T_NUM] = {
	/*[*/
CONVERTER_NAMES(ELF_TYPE_LIST)
	/*]*/

	/*
	 * Types that need hand-coded converters follow.
	 */

	[ELF_T_BYTE] = {
		.tof32 = _libelf_cvt_BYTE_tox,
		.tom32 = _libelf_cvt_BYTE_tox,
		.tof64 = _libelf_cvt_BYTE_tox,
		.tom64 = _libelf_cvt_BYTE_tox
	},

	[ELF_T_NOTE] = {
		.tof32 = _libelf_cvt_NOTE_tof,
		.tom32 = _libelf_cvt_NOTE_tom,
		.tof64 = _libelf_cvt_NOTE_tof,
		.tom64 = _libelf_cvt_NOTE_tom
	}
};

/*
 * Return a translator function for the specified ELF section type, conversion
 * direction, ELF class and ELF machine.
 */
_libelf_translator_function *
_libelf_get_translator(Elf_Type t, int direction, int elfclass, int elfmachine)
{
	assert(elfclass == ELFCLASS32 || elfclass == ELFCLASS64);
#if 0
	assert(elfmachine >= EM_NONE && elfmachine < EM__LAST__);
#endif
	assert(direction == ELF_TOFILE || direction == ELF_TOMEMORY);

	if (t >= ELF_T_NUM ||
	    (elfclass != ELFCLASS32 && elfclass != ELFCLASS64) ||
	    (direction != ELF_TOFILE && direction != ELF_TOMEMORY))
		return (NULL);

	/* TODO: Handle MIPS64 REL{,A} sections (ticket #559). */
	(void) elfmachine;

	return ((elfclass == ELFCLASS32) ?
	    (direction == ELF_TOFILE ? cvt[t].tof32 : cvt[t].tom32) :
	    (direction == ELF_TOFILE ? cvt[t].tof64 : cvt[t].tom64));
}
