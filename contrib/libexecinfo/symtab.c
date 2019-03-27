/*	$NetBSD: symtab.c,v 1.2 2013/08/29 15:01:57 christos Exp $	*/

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: symtab.c,v 1.2 2013/08/29 15:01:57 christos Exp $");

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <err.h>
#include <dlfcn.h>

#include <libelf.h>
#include <gelf.h>
#ifndef ELF_ST_BIND
#define ELF_ST_BIND(x)          ((x) >> 4)
#endif
#ifndef ELF_ST_TYPE
#define ELF_ST_TYPE(x)          (((unsigned int)x) & 0xf)
#endif


#include "symtab.h"

struct symbol {
	char *st_name;
	uintptr_t st_value;
	uintptr_t st_info;
};

struct symtab {
	size_t nsymbols;
	struct symbol *symbols;
};

static int
address_compare(const void *a, const void *b)
{
	const struct symbol *sa = a;
	const struct symbol *sb = b;
	return (int)(intmax_t)(sa->st_value - sb->st_value);
}

void
symtab_destroy(symtab_t *s)
{
	if (s == NULL)
		return;
	for (size_t i = 0; i < s->nsymbols; i++)
		free(s->symbols[i].st_name);
	free(s->symbols);
	free(s);
}

symtab_t *
symtab_create(int fd, int bind, int type)
{
	Elf *elf;
	symtab_t *st;
	Elf_Scn *scn = NULL;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		warnx("Elf Library is out of date.");
		return NULL;
	}

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (elf == NULL) {
		warnx("Error opening elf file: %s", elf_errmsg(elf_errno()));
		return NULL;
	}
	st = calloc(1, sizeof(*st));
	if (st == NULL) {
		warnx("Error allocating symbol table");
		elf_end(elf);
		return NULL;
	}

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		GElf_Shdr shdr;
		Elf_Data *edata;
		size_t ns;
		struct symbol *s;

		gelf_getshdr(scn, &shdr);
		if(shdr.sh_type != SHT_SYMTAB)
			continue;

		edata = elf_getdata(scn, NULL);
		ns = shdr.sh_size / shdr.sh_entsize;
		s = calloc(ns, sizeof(*s));
		if (s == NULL) {
			warn("Cannot allocate %zu symbols", ns);
			goto out;
		}
		st->symbols = s;

		for (size_t i = 0; i < ns; i++) {
			GElf_Sym sym;
                        gelf_getsym(edata, (int)i, &sym);

			if (bind != -1 &&
			    (unsigned)bind != ELF_ST_BIND(sym.st_info))
				continue;

			if (type != -1 &&
			    (unsigned)type != ELF_ST_TYPE(sym.st_info))
				continue;

			s->st_value = sym.st_value;
			s->st_info = sym.st_info;
			s->st_name = strdup(
			    elf_strptr(elf, shdr.sh_link, sym.st_name));
			if (s->st_name == NULL)
				goto out;
			s++;
                }
		st->nsymbols = s - st->symbols;
		if (st->nsymbols == 0) {
			warnx("No symbols found");
			goto out;
		}
		qsort(st->symbols, st->nsymbols, sizeof(*st->symbols),
		    address_compare);
		elf_end(elf);
		return st;
	}
out:
	symtab_destroy(st);
	elf_end(elf);
	return NULL;
}

	
int
symtab_find(const symtab_t *st, const void *p, Dl_info *dli)
{
	struct symbol *s = st->symbols;
	size_t ns = st->nsymbols;
	size_t hi = ns;
	size_t lo = 0;
	size_t mid = ns / 2;
	uintptr_t dd, sd, me = (uintptr_t)p;

	for (;;) {
		if (s[mid].st_value < me)
			lo = mid;
		else if (s[mid].st_value > me)
			hi = mid;
		else
			break;
		if (hi - lo == 1) {
			mid = lo;
			break;
		}
		mid = (hi + lo) / 2;
	}
	dd = me - (uintptr_t)dli->dli_saddr;
	sd = me - s[mid].st_value;
	if (dd > sd) {
		dli->dli_saddr = (void *)s[mid].st_value;
		dli->dli_sname = s[mid].st_name;
	}
	return 1;
}
