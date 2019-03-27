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

#include <assert.h>
#include <libelf.h>
#include <string.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_msize.m4 3174 2015-03-27 17:13:41Z emaste $");

/* WARNING: GENERATED FROM __file__. */

struct msize {
	size_t	msz32;
	size_t	msz64;
};

divert(-1)
include(SRCDIR`/elf_types.m4')

/*
 * ELF types whose memory representations have a variable size.
 */
define(BYTE_SIZE,	1)
define(GNUHASH_SIZE,	1)
define(NOTE_SIZE,	1)
define(VDEF_SIZE,	1)
define(VNEED_SIZE,	1)

/*
 * Unimplemented types.
 */
define(MOVEP_SIZE,	0)
define(SXWORD_SIZE32,	0)
define(XWORD_SIZE32,	0)

define(`DEFINE_ELF_MSIZE',
  `ifdef($1`_SIZE',
    `define($1_SIZE32,$1_SIZE)
     define($1_SIZE64,$1_SIZE)',
    `ifdef($1`_SIZE32',`',
      `define($1_SIZE32,sizeof(Elf32_$2))')
     ifdef($1`_SIZE64',`',
      `define($1_SIZE64,sizeof(Elf64_$2))')')')
define(`DEFINE_ELF_MSIZES',
  `ifelse($#,1,`',
    `DEFINE_ELF_MSIZE($1)
     DEFINE_ELF_MSIZES(shift($@))')')

DEFINE_ELF_MSIZES(ELF_TYPE_LIST)

define(`MSIZE',
  `[ELF_T_$1] = { .msz32 = $1_SIZE32, .msz64 = $1_SIZE64 },
')
define(`MSIZES',
  `ifelse($#,1,`',
    `MSIZE($1)
MSIZES(shift($@))')')

divert(0)

static struct msize msize[ELF_T_NUM] = {
MSIZES(ELF_TYPE_LIST)
};

size_t
_libelf_msize(Elf_Type t, int elfclass, unsigned int version)
{
	size_t sz;

	assert(elfclass == ELFCLASS32 || elfclass == ELFCLASS64);
	assert((signed) t >= ELF_T_FIRST && t <= ELF_T_LAST);

	if (version != EV_CURRENT) {
		LIBELF_SET_ERROR(VERSION, 0);
		return (0);
	}

	sz = (elfclass == ELFCLASS32) ? msize[t].msz32 : msize[t].msz64;

	return (sz);
}
