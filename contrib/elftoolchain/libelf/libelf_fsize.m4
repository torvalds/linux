/*-
 * Copyright (c) 2006,2008-2011 Joseph Koshy
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

#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_fsize.m4 2225 2011-11-26 18:55:54Z jkoshy $");

/* WARNING: GENERATED FROM __file__. */

/*
 * Create an array of file sizes from the elf_type definitions
 */

divert(-1)
include(SRCDIR`/elf_types.m4')

/*
 * Translations from structure definitions to the size of their file
 * representations.
 */

/* `Basic' types. */
define(`BYTE_SIZE',	1)
define(`IDENT_SIZE',	`EI_NIDENT')

/* Types that have variable length. */
define(`GNUHASH_SIZE',	1)
define(`NOTE_SIZE',	1)
define(`VDEF_SIZE',	1)
define(`VNEED_SIZE',	1)

/* Currently unimplemented types. */
define(`MOVEP_SIZE',	0)

/* Overrides for 32 bit types that do not exist. */
define(`XWORD_SIZE32',	0)
define(`SXWORD_SIZE32',	0)

/*
 * FSZ{32,64} define the sizes of 32 and 64 bit file structures respectively.
 */

define(`FSZ32',`_FSZ32($1_DEF)')
define(`_FSZ32',
  `ifelse($#,1,0,
    `_BSZ32($1)+_FSZ32(shift($@))')')
define(`_BSZ32',`$2_SIZE32')

define(`FSZ64',`_FSZ64($1_DEF)')
define(`_FSZ64',
  `ifelse($#,1,0,
    `_BSZ64($1)+_FSZ64(shift($@))')')
define(`_BSZ64',`$2_SIZE64')

/*
 * DEFINE_ELF_FSIZES(TYPE,NAME)
 *
 * Shorthand for defining  for 32 and 64 versions
 * of elf type TYPE.
 *
 * If TYPE`'_SIZE is defined, use its value for both 32 bit and 64 bit
 * sizes.
 *
 * Otherwise, look for a explicit 32/64 bit size definition for TYPE,
 * TYPE`'_SIZE32 or TYPE`'_SIZE64. If this definition is present, there
 * is nothing further to do.
 *
 * Otherwise, if an Elf{32,64}_`'NAME structure definition is known,
 * compute an expression that adds up the sizes of the structure's
 * constituents.
 *
 * If such a structure definition is not known, treat TYPE as a primitive
 * (i.e., integral) type and use sizeof(Elf{32,64}_`'NAME) to get its
 * file representation size.
 */

define(`DEFINE_ELF_FSIZE',
  `ifdef($1`_SIZE',
    `define($1_SIZE32,$1_SIZE)
     define($1_SIZE64,$1_SIZE)',
    `ifdef($1`_SIZE32',`',
      `ifdef(`Elf32_'$2`_DEF',
        `define($1_SIZE32,FSZ32(Elf32_$2))',
        `define($1_SIZE32,`sizeof(Elf32_'$2`)')')')
     ifdef($1`_SIZE64',`',
      `ifdef(`Elf64_'$2`_DEF',
        `define($1_SIZE64,FSZ64(Elf64_$2))',
        `define($1_SIZE64,`sizeof(Elf64_'$2`)')')')')')

define(`DEFINE_ELF_FSIZES',
  `ifelse($#,1,`',
    `DEFINE_ELF_FSIZE($1)
     DEFINE_ELF_FSIZES(shift($@))')')

DEFINE_ELF_FSIZES(ELF_TYPE_LIST)
DEFINE_ELF_FSIZE(`IDENT',`')	# `IDENT' is a pseudo type

define(`FSIZE',
  `[ELF_T_$1] = { .fsz32 = $1_SIZE32, .fsz64 = $1_SIZE64 },
')
define(`FSIZES',
  `ifelse($#,1,`',
    `FSIZE($1)
FSIZES(shift($@))')')

divert(0)

struct fsize {
	size_t fsz32;
	size_t fsz64;
};

static struct fsize fsize[ELF_T_NUM] = {
FSIZES(ELF_TYPE_LIST)
};

size_t
_libelf_fsize(Elf_Type t, int ec, unsigned int v, size_t c)
{
	size_t sz;

	sz = 0;
	if (v != EV_CURRENT)
		LIBELF_SET_ERROR(VERSION, 0);
	else if ((int) t < ELF_T_FIRST || t > ELF_T_LAST)
		LIBELF_SET_ERROR(ARGUMENT, 0);
	else {
		sz = ec == ELFCLASS64 ? fsize[t].fsz64 : fsize[t].fsz32;
		if (sz == 0)
			LIBELF_SET_ERROR(UNIMPL, 0);
	}

	return (sz*c);
}
