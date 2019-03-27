/*-
 * Copyright (c) 2006,2008 Joseph Koshy
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

#include <sys/types.h>

#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_align.c 3174 2015-03-27 17:13:41Z emaste $");

struct align {
	unsigned int a32;
	unsigned int a64;
};

#ifdef	__GNUC__
#define	MALIGN(N)	{					\
		.a32 = __alignof__(Elf32_##N),			\
		.a64 = __alignof__(Elf64_##N)			\
	}
#define	MALIGN64(V)	  {					\
		.a32 = 0,					\
		.a64 = __alignof__(Elf64_##V)			\
	}
#define	MALIGN_WORD()	{					\
		.a32 = __alignof__(int32_t),			\
		.a64 = __alignof__(int64_t)			\
	    }
#else
#error	Need the __alignof__ builtin.
#endif
#define	UNSUPPORTED()	{					\
		.a32 = 0,					\
		.a64 = 0					\
	}

static struct align malign[ELF_T_NUM] = {
	[ELF_T_ADDR]	= MALIGN(Addr),
	[ELF_T_BYTE]	= { .a32 = 1, .a64 = 1 },
	[ELF_T_CAP]	= MALIGN(Cap),
	[ELF_T_DYN]	= MALIGN(Dyn),
	[ELF_T_EHDR]	= MALIGN(Ehdr),
	[ELF_T_HALF]	= MALIGN(Half),
	[ELF_T_LWORD]	= MALIGN(Lword),
	[ELF_T_MOVE]	= MALIGN(Move),
	[ELF_T_MOVEP] 	= UNSUPPORTED(),
	[ELF_T_NOTE]	= MALIGN(Nhdr),
	[ELF_T_OFF]	= MALIGN(Off),
	[ELF_T_PHDR]	= MALIGN(Phdr),
	[ELF_T_REL]	= MALIGN(Rel),
	[ELF_T_RELA]	= MALIGN(Rela),
	[ELF_T_SHDR]	= MALIGN(Shdr),
	[ELF_T_SWORD]	= MALIGN(Sword),
	[ELF_T_SXWORD]	= MALIGN64(Sxword),
	[ELF_T_SYM]	= MALIGN(Sym),
	[ELF_T_SYMINFO]	= MALIGN(Syminfo),
	[ELF_T_VDEF]	= MALIGN(Verdef),
	[ELF_T_VNEED]	= MALIGN(Verneed),
	[ELF_T_WORD]	= MALIGN(Word),
	[ELF_T_XWORD]	= MALIGN64(Xword),
	[ELF_T_GNUHASH] = MALIGN_WORD()
};

unsigned int
_libelf_malign(Elf_Type t, int elfclass)
{
	if (t >= ELF_T_NUM || (int) t < 0)
		return (0);

	return (elfclass == ELFCLASS32 ? malign[t].a32 :
	    malign[t].a64);
}

#define	FALIGN(A32,A64)	{ .a32 = (A32), .a64 = (A64) }

static struct align falign[ELF_T_NUM] = {
	[ELF_T_ADDR]	= FALIGN(4,8),
	[ELF_T_BYTE]	= FALIGN(1,1),
	[ELF_T_CAP]	= FALIGN(4,8),
	[ELF_T_DYN]	= FALIGN(4,8),
	[ELF_T_EHDR]	= FALIGN(4,8),
	[ELF_T_HALF]	= FALIGN(2,2),
	[ELF_T_LWORD]	= FALIGN(8,8),
	[ELF_T_MOVE]	= FALIGN(8,8),
	[ELF_T_MOVEP] 	= UNSUPPORTED(),
	[ELF_T_NOTE]	= FALIGN(1,1),
	[ELF_T_OFF]	= FALIGN(4,8),
	[ELF_T_PHDR]	= FALIGN(4,8),
	[ELF_T_REL]	= FALIGN(4,8),
	[ELF_T_RELA]	= FALIGN(4,8),
	[ELF_T_SHDR]	= FALIGN(4,8),
	[ELF_T_SWORD]	= FALIGN(4,4),
	[ELF_T_SXWORD]	= FALIGN(0,8),
	[ELF_T_SYM]	= FALIGN(4,8),
	[ELF_T_SYMINFO]	= FALIGN(2,2),
	[ELF_T_VDEF]	= FALIGN(4,4),
	[ELF_T_VNEED]	= FALIGN(4,4),
	[ELF_T_WORD]	= FALIGN(4,4),
	[ELF_T_XWORD]	= FALIGN(0,8),
	[ELF_T_GNUHASH] = FALIGN(4,8)
};

unsigned int
_libelf_falign(Elf_Type t, int elfclass)
{
	if (t >= ELF_T_NUM || (int) t < 0)
		return (0);

	return (elfclass == ELFCLASS32 ? falign[t].a32 :
	    falign[t].a64);
}
