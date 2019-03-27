/*-
 * Copyright (c) 2007-2013 Kai Wang
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

#include <sys/param.h>
#include <assert.h>
#include <err.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elfcopy.h"

ELFTC_VCSID("$Id: symbols.c 3520 2017-04-17 01:47:52Z kaiwang27 $");

/* Backwards compatibility for systems with older ELF definitions. */
#ifndef STB_GNU_UNIQUE
#define	STB_GNU_UNIQUE 10
#endif


/* Symbol table buffer structure. */
struct symbuf {
	Elf32_Sym *l32;		/* 32bit local symbol */
	Elf32_Sym *g32;		/* 32bit global symbol */
	Elf64_Sym *l64;		/* 64bit local symbol */
	Elf64_Sym *g64;		/* 64bit global symbol */
	size_t ngs, nls;	/* number of each kind */
	size_t gcap, lcap; 	/* buffer capacities. */
};

struct sthash {
	LIST_ENTRY(sthash) sh_next;
	size_t sh_off;
};
typedef LIST_HEAD(,sthash) hash_head;
#define STHASHSIZE 65536

struct strimpl {
	char *buf;		/* string table */
	size_t sz;		/* entries */
	size_t cap;		/* buffer capacity */
	hash_head hash[STHASHSIZE];
};


/* String table buffer structure. */
struct strbuf {
	struct strimpl l;	/* local symbols */
	struct strimpl g;	/* global symbols */
};

static int	is_debug_symbol(unsigned char st_info);
static int	is_global_symbol(unsigned char st_info);
static int	is_local_symbol(unsigned char st_info);
static int	is_local_label(const char *name);
static int	is_needed_symbol(struct elfcopy *ecp, int i, GElf_Sym *s);
static int	is_remove_symbol(struct elfcopy *ecp, size_t sc, int i,
		    GElf_Sym *s, const char *name);
static int	is_weak_symbol(unsigned char st_info);
static int	lookup_exact_string(hash_head *hash, const char *buf,
		    const char *s);
static int	generate_symbols(struct elfcopy *ecp);
static void	mark_reloc_symbols(struct elfcopy *ecp, size_t sc);
static void	mark_section_group_symbols(struct elfcopy *ecp, size_t sc);
uint32_t	str_hash(const char *s);

/* Convenient bit vector operation macros. */
#define BIT_SET(v, n) (v[(n)>>3] |= 1U << ((n) & 7))
#define BIT_CLR(v, n) (v[(n)>>3] &= ~(1U << ((n) & 7)))
#define BIT_ISSET(v, n) (v[(n)>>3] & (1U << ((n) & 7)))

static int
is_debug_symbol(unsigned char st_info)
{

	if (GELF_ST_TYPE(st_info) == STT_SECTION ||
	    GELF_ST_TYPE(st_info) == STT_FILE)
		return (1);

	return (0);
}

static int
is_global_symbol(unsigned char st_info)
{

	if (GELF_ST_BIND(st_info) == STB_GLOBAL ||
	    GELF_ST_BIND(st_info) == STB_GNU_UNIQUE)
		return (1);

	return (0);
}

static int
is_weak_symbol(unsigned char st_info)
{

	if (GELF_ST_BIND(st_info) == STB_WEAK)
		return (1);

	return (0);
}

static int
is_local_symbol(unsigned char st_info)
{

	if (GELF_ST_BIND(st_info) == STB_LOCAL)
		return (1);

	return (0);
}

static int
is_hidden_symbol(unsigned char st_other)
{

	if (GELF_ST_VISIBILITY(st_other) == STV_HIDDEN ||
	    GELF_ST_VISIBILITY(st_other) == STV_INTERNAL)
		return (1);

	return (0);
}

static int
is_local_label(const char *name)
{

	/* Compiler generated local symbols that start with .L */
	if (name[0] == '.' && name[1] == 'L')
		return (1);

	return (0);
}

/*
 * Symbols related to relocation are needed.
 */
static int
is_needed_symbol(struct elfcopy *ecp, int i, GElf_Sym *s)
{

	/* If symbol involves relocation, it is needed. */
	if (BIT_ISSET(ecp->v_rel, i))
		return (1);

	/* Symbols referred by COMDAT sections are needed. */
	if (BIT_ISSET(ecp->v_grp, i))
		return (1);

	/*
	 * For relocatable files (.o files), global and weak symbols
	 * are needed.
	 */
	if (ecp->flags & RELOCATABLE) {
		if (is_global_symbol(s->st_info) || is_weak_symbol(s->st_info))
			return (1);
	}

	return (0);
}

static int
is_remove_symbol(struct elfcopy *ecp, size_t sc, int i, GElf_Sym *s,
    const char *name)
{
	GElf_Sym sym0 = {
		0, 		/* st_name */
		0,		/* st_value */
		0,		/* st_size */
		0,		/* st_info */
		0,		/* st_other */
		SHN_UNDEF,	/* st_shndx */
	};

	/*
	 * Keep the first symbol if it is the special reserved symbol.
	 * XXX Should we generate one if it's missing?
	 */
	if (i == 0 && !memcmp(s, &sym0, sizeof(GElf_Sym)))
		return (0);

	/* Remove the symbol if the section it refers to was removed. */
	if (s->st_shndx != SHN_UNDEF && s->st_shndx < SHN_LORESERVE &&
	    ecp->secndx[s->st_shndx] == 0)
		return (1);

	/* Keep the symbol if specified by command line option -K. */
	if (lookup_symop_list(ecp, name, SYMOP_KEEP) != NULL)
		return (0);

	if (ecp->strip == STRIP_ALL)
		return (1);

	/* Mark symbols used in relocation. */
	if (ecp->v_rel == NULL)
		mark_reloc_symbols(ecp, sc);

	/* Mark symbols used in section groups. */
	if (ecp->v_grp == NULL)
		mark_section_group_symbols(ecp, sc);

	/*
	 * Strip the symbol if specified by command line option -N,
	 * unless it's used in relocation.
	 */
	if (lookup_symop_list(ecp, name, SYMOP_STRIP) != NULL) {
		if (BIT_ISSET(ecp->v_rel, i)) {
			warnx("not stripping symbol `%s' because it is named"
			    " in a relocation", name);
			return (0);
		}
		return (1);
	}

	if (is_needed_symbol(ecp, i, s))
		return (0);

	if (ecp->strip == STRIP_UNNEEDED)
		return (1);

	if ((ecp->flags & DISCARD_LOCAL) && is_local_symbol(s->st_info) &&
	    !is_debug_symbol(s->st_info))
		return (1);

	if ((ecp->flags & DISCARD_LLABEL) && is_local_symbol(s->st_info) &&
	    !is_debug_symbol(s->st_info) && is_local_label(name))
		return (1);

	if (ecp->strip == STRIP_DEBUG && is_debug_symbol(s->st_info))
		return (1);

	return (0);
}

/*
 * Mark symbols referred by relocation entries.
 */
static void
mark_reloc_symbols(struct elfcopy *ecp, size_t sc)
{
	const char	*name;
	Elf_Data	*d;
	Elf_Scn		*s;
	GElf_Rel	 r;
	GElf_Rela	 ra;
	GElf_Shdr	 sh;
	size_t		 n, indx;
	int		 elferr, i, len;

	ecp->v_rel = calloc((sc + 7) / 8, 1);
	if (ecp->v_rel == NULL)
		err(EXIT_FAILURE, "calloc failed");

	if (elf_getshstrndx(ecp->ein, &indx) == 0)
		errx(EXIT_FAILURE, "elf_getshstrndx failed: %s",
		    elf_errmsg(-1));

	s = NULL;
	while ((s = elf_nextscn(ecp->ein, s)) != NULL) {
		if (gelf_getshdr(s, &sh) != &sh)
			errx(EXIT_FAILURE, "elf_getshdr failed: %s",
			    elf_errmsg(-1));

		if (sh.sh_type != SHT_REL && sh.sh_type != SHT_RELA)
			continue;

		/*
		 * Skip if this reloc section won't appear in the
		 * output object.
		 */
		if ((name = elf_strptr(ecp->ein, indx, sh.sh_name)) == NULL)
			errx(EXIT_FAILURE, "elf_strptr failed: %s",
			    elf_errmsg(-1));
		if (is_remove_section(ecp, name) ||
		    is_remove_reloc_sec(ecp, sh.sh_info))
			continue;

		/* Skip if it's not for .symtab */
		if (sh.sh_link != elf_ndxscn(ecp->symtab->is))
			continue;

		d = NULL;
		n = 0;
		while (n < sh.sh_size && (d = elf_getdata(s, d)) != NULL) {
			len = d->d_size / sh.sh_entsize;
			for (i = 0; i < len; i++) {
				if (sh.sh_type == SHT_REL) {
					if (gelf_getrel(d, i, &r) != &r)
						errx(EXIT_FAILURE,
						    "elf_getrel failed: %s",
						     elf_errmsg(-1));
					n = GELF_R_SYM(r.r_info);
				} else {
					if (gelf_getrela(d, i, &ra) != &ra)
						errx(EXIT_FAILURE,
						    "elf_getrela failed: %s",
						     elf_errmsg(-1));
					n = GELF_R_SYM(ra.r_info);
				}
				if (n > 0 && n < sc)
					BIT_SET(ecp->v_rel, n);
				else if (n != 0)
					warnx("invalid symbox index");
			}
		}
		elferr = elf_errno();
		if (elferr != 0)
			errx(EXIT_FAILURE, "elf_getdata failed: %s",
			    elf_errmsg(elferr));
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));
}

static void
mark_section_group_symbols(struct elfcopy *ecp, size_t sc)
{
	const char	*name;
	Elf_Scn		*s;
	GElf_Shdr	 sh;
	size_t		 indx;
	int		 elferr;

	ecp->v_grp = calloc((sc + 7) / 8, 1);
	if (ecp->v_grp == NULL)
		err(EXIT_FAILURE, "calloc failed");

	if (elf_getshstrndx(ecp->ein, &indx) == 0)
		errx(EXIT_FAILURE, "elf_getshstrndx failed: %s",
		    elf_errmsg(-1));

	s = NULL;
	while ((s = elf_nextscn(ecp->ein, s)) != NULL) {
		if (gelf_getshdr(s, &sh) != &sh)
			errx(EXIT_FAILURE, "elf_getshdr failed: %s",
			    elf_errmsg(-1));

		if (sh.sh_type != SHT_GROUP)
			continue;

		if ((name = elf_strptr(ecp->ein, indx, sh.sh_name)) == NULL)
			errx(EXIT_FAILURE, "elf_strptr failed: %s",
			    elf_errmsg(-1));
		if (is_remove_section(ecp, name))
			continue;

		if (sh.sh_info > 0 && sh.sh_info < sc)
			BIT_SET(ecp->v_grp, sh.sh_info);
		else if (sh.sh_info != 0)
			warnx("invalid symbox index");
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));
}

static int
generate_symbols(struct elfcopy *ecp)
{
	struct section	*s;
	struct symop	*sp;
	struct symbuf	*sy_buf;
	struct strbuf	*st_buf;
	const char	*name;
	char		*newname;
	unsigned char	*gsym;
	GElf_Shdr	 ish;
	GElf_Sym	 sym;
	Elf_Data*	 id;
	Elf_Scn		*is;
	size_t		 ishstrndx, namelen, ndx, sc, symndx;
	int		 ec, elferr, i;

	if (elf_getshstrndx(ecp->ein, &ishstrndx) == 0)
		errx(EXIT_FAILURE, "elf_getshstrndx failed: %s",
		    elf_errmsg(-1));
	if ((ec = gelf_getclass(ecp->eout)) == ELFCLASSNONE)
		errx(EXIT_FAILURE, "gelf_getclass failed: %s",
		    elf_errmsg(-1));

	/* Create buffers for .symtab and .strtab. */
	if ((sy_buf = calloc(1, sizeof(*sy_buf))) == NULL)
		err(EXIT_FAILURE, "calloc failed");
	if ((st_buf = calloc(1, sizeof(*st_buf))) == NULL)
		err(EXIT_FAILURE, "calloc failed");
	sy_buf->gcap = sy_buf->lcap = 64;
	st_buf->g.cap = 256;
	st_buf->l.cap = 64;
	st_buf->l.sz = 1;	/* '\0' at start. */
	st_buf->g.sz = 0;

	ecp->symtab->sz = 0;
	ecp->strtab->sz = 0;
	ecp->symtab->buf = sy_buf;
	ecp->strtab->buf = st_buf;

	gsym = NULL;

	/*
	 * Create bit vector v_secsym, which is used to mark sections
	 * that already have corresponding STT_SECTION symbols.
	 */
	ecp->v_secsym = calloc((ecp->nos + 7) / 8, 1);
	if (ecp->v_secsym == NULL)
		err(EXIT_FAILURE, "calloc failed");

	/* Locate .strtab of input object. */
	symndx = 0;
	name = NULL;
	is = NULL;
	while ((is = elf_nextscn(ecp->ein, is)) != NULL) {
		if (gelf_getshdr(is, &ish) != &ish)
			errx(EXIT_FAILURE, "elf_getshdr failed: %s",
			    elf_errmsg(-1));
		if ((name = elf_strptr(ecp->ein, ishstrndx, ish.sh_name)) ==
		    NULL)
			errx(EXIT_FAILURE, "elf_strptr failed: %s",
			    elf_errmsg(-1));
		if (strcmp(name, ".strtab") == 0) {
			symndx = elf_ndxscn(is);
			break;
		}
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));

	/* Symbol table should exist if this function is called. */
	if (symndx == 0) {
		warnx("can't find .strtab section");
		goto clean;
	}

	/* Locate .symtab of input object. */
	is = NULL;
	while ((is = elf_nextscn(ecp->ein, is)) != NULL) {
		if (gelf_getshdr(is, &ish) != &ish)
			errx(EXIT_FAILURE, "elf_getshdr failed: %s",
			    elf_errmsg(-1));
		if ((name = elf_strptr(ecp->ein, ishstrndx, ish.sh_name)) ==
		    NULL)
			errx(EXIT_FAILURE, "elf_strptr failed: %s",
			    elf_errmsg(-1));
		if (strcmp(name, ".symtab") == 0)
			break;
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));
	if (is == NULL)
		errx(EXIT_FAILURE, "can't find .strtab section");

	/*
	 * Create bit vector gsym to mark global symbols, and symndx
	 * to keep track of symbol index changes from input object to
	 * output object, it is used by update_reloc() later to update
	 * relocation information.
	 */
	sc = ish.sh_size / ish.sh_entsize;
	if (sc > 0) {
		ecp->symndx = calloc(sc, sizeof(*ecp->symndx));
		if (ecp->symndx == NULL)
			err(EXIT_FAILURE, "calloc failed");
		gsym = calloc((sc + 7) / 8, sizeof(*gsym));
		if (gsym == NULL)
			err(EXIT_FAILURE, "calloc failed");
		if ((id = elf_getdata(is, NULL)) == NULL) {
			elferr = elf_errno();
			if (elferr != 0)
				errx(EXIT_FAILURE, "elf_getdata failed: %s",
				    elf_errmsg(elferr));
			goto clean;
		}
	} else
		return (0);

	/* Copy/Filter each symbol. */
	for (i = 0; (size_t)i < sc; i++) {
		if (gelf_getsym(id, i, &sym) != &sym)
			errx(EXIT_FAILURE, "gelf_getsym failed: %s",
			    elf_errmsg(-1));
		if ((name = elf_strptr(ecp->ein, symndx, sym.st_name)) == NULL)
			errx(EXIT_FAILURE, "elf_strptr failed: %s",
			    elf_errmsg(-1));

		/* Symbol filtering. */
		if (is_remove_symbol(ecp, sc, i, &sym, name) != 0)
			continue;

		/* Check if we need to change the binding of this symbol. */
		if (is_global_symbol(sym.st_info) ||
		    is_weak_symbol(sym.st_info)) {
			/*
			 * XXX Binutils objcopy does not weaken certain
			 * symbols.
			 */
			if (ecp->flags & WEAKEN_ALL ||
			    lookup_symop_list(ecp, name, SYMOP_WEAKEN) != NULL)
				sym.st_info = GELF_ST_INFO(STB_WEAK,
				    GELF_ST_TYPE(sym.st_info));
			/* Do not localize undefined symbols. */
			if (sym.st_shndx != SHN_UNDEF &&
			    lookup_symop_list(ecp, name, SYMOP_LOCALIZE) !=
			    NULL)
				sym.st_info = GELF_ST_INFO(STB_LOCAL,
				    GELF_ST_TYPE(sym.st_info));
			if (ecp->flags & KEEP_GLOBAL &&
			    sym.st_shndx != SHN_UNDEF &&
			    lookup_symop_list(ecp, name, SYMOP_KEEPG) == NULL)
				sym.st_info = GELF_ST_INFO(STB_LOCAL,
				    GELF_ST_TYPE(sym.st_info));
			if (ecp->flags & LOCALIZE_HIDDEN &&
			    sym.st_shndx != SHN_UNDEF &&
			    is_hidden_symbol(sym.st_other))
				sym.st_info = GELF_ST_INFO(STB_LOCAL,
				    GELF_ST_TYPE(sym.st_info));
		} else {
			/* STB_LOCAL binding. */
			if (lookup_symop_list(ecp, name, SYMOP_GLOBALIZE) !=
			    NULL)
				sym.st_info = GELF_ST_INFO(STB_GLOBAL,
				    GELF_ST_TYPE(sym.st_info));
			/* XXX We should globalize weak symbol? */
		}

		/* Check if we need to rename this symbol. */
		if ((sp = lookup_symop_list(ecp, name, SYMOP_REDEF)) != NULL)
			name = sp->newname;

		/* Check if we need to prefix the symbols. */
		newname = NULL;
		if (ecp->prefix_sym != NULL && name != NULL && *name != '\0') {
			namelen = strlen(name) + strlen(ecp->prefix_sym) + 1;
			if ((newname = malloc(namelen)) == NULL)
				err(EXIT_FAILURE, "malloc failed");
			snprintf(newname, namelen, "%s%s", ecp->prefix_sym,
			    name);
			name = newname;
		}

		/* Copy symbol, mark global/weak symbol and add to index map. */
		if (is_global_symbol(sym.st_info) ||
		    is_weak_symbol(sym.st_info)) {
			BIT_SET(gsym, i);
			ecp->symndx[i] = sy_buf->ngs;
		} else
			ecp->symndx[i] = sy_buf->nls;
		add_to_symtab(ecp, name, sym.st_value, sym.st_size,
		    sym.st_shndx, sym.st_info, sym.st_other, 0);

		if (newname != NULL)
			free(newname);

		/*
		 * If the symbol is a STT_SECTION symbol, mark the section
		 * it points to.
		 */
		if (GELF_ST_TYPE(sym.st_info) == STT_SECTION &&
		    sym.st_shndx < SHN_LORESERVE) {
			assert(ecp->secndx[sym.st_shndx] < (uint64_t)ecp->nos);
			BIT_SET(ecp->v_secsym, ecp->secndx[sym.st_shndx]);
		}
	}

	/*
	 * Give up if there is no real symbols inside the table.
	 * XXX The logic here needs to be improved. We need to
	 * check if that only local symbol is the reserved symbol.
	 */
	if (sy_buf->nls <= 1 && sy_buf->ngs == 0)
		goto clean;

	/*
	 * Create STT_SECTION symbols for sections that do not already
	 * got one. However, we do not create STT_SECTION symbol for
	 * .symtab, .strtab, .shstrtab and reloc sec of relocatables.
	 */
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (s->pseudo)
			continue;
		if (strcmp(s->name, ".symtab") == 0 ||
		    strcmp(s->name, ".strtab") == 0 ||
		    strcmp(s->name, ".shstrtab") == 0)
			continue;
		if ((ecp->flags & RELOCATABLE) != 0 &&
		    ((s->type == SHT_REL) || (s->type == SHT_RELA)))
			continue;

		if ((ndx = elf_ndxscn(s->os)) == SHN_UNDEF)
			errx(EXIT_FAILURE, "elf_ndxscn failed: %s",
			    elf_errmsg(-1));

		if (!BIT_ISSET(ecp->v_secsym, ndx)) {
			sym.st_name  = 0;
			sym.st_value = s->vma;
			sym.st_size  = 0;
			sym.st_info  = GELF_ST_INFO(STB_LOCAL, STT_SECTION);
			sym.st_other = STV_DEFAULT;
			/*
			 * Don't let add_to_symtab() touch sym.st_shndx.
			 * In this case, we know the index already.
			 */
			add_to_symtab(ecp, NULL, sym.st_value, sym.st_size,
			    ndx, sym.st_info, sym.st_other, 1);
		}
	}

	/*
	 * Update st_name and index map for global/weak symbols. Note that
	 * global/weak symbols are put after local symbols.
	 */
	if (gsym != NULL) {
		for(i = 0; (size_t) i < sc; i++) {
			if (!BIT_ISSET(gsym, i))
				continue;

			/* Update st_name. */
			if (ec == ELFCLASS32)
				sy_buf->g32[ecp->symndx[i]].st_name +=
				    st_buf->l.sz;
			else
				sy_buf->g64[ecp->symndx[i]].st_name +=
				    st_buf->l.sz;

			/* Update index map. */
			ecp->symndx[i] += sy_buf->nls;
		}
		free(gsym);
	}

	return (1);

clean:
	free(gsym);
	free_symtab(ecp);

	return (0);
}

void
create_symtab(struct elfcopy *ecp)
{
	struct section	*s, *sy, *st;
	size_t		 maxndx, ndx;

	sy = ecp->symtab;
	st = ecp->strtab;

	assert(sy != NULL && st != NULL);

	/*
	 * Set section index map for .symtab and .strtab. We need to set
	 * these map because otherwise symbols which refer to .symtab and
	 * .strtab will be removed by symbol filtering unconditionally.
	 * And we have to figure out scn index this way (instead of calling
	 * elf_ndxscn) because we can not create Elf_Scn before we're certain
	 * that .symtab and .strtab will exist in the output object.
	 */
	maxndx = 0;
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (s->os == NULL)
			continue;
		if ((ndx = elf_ndxscn(s->os)) == SHN_UNDEF)
			errx(EXIT_FAILURE, "elf_ndxscn failed: %s",
			    elf_errmsg(-1));
		if (ndx > maxndx)
			maxndx = ndx;
	}
	ecp->secndx[elf_ndxscn(sy->is)] = maxndx + 1;
	ecp->secndx[elf_ndxscn(st->is)] = maxndx + 2;

	/*
	 * Generate symbols for output object if SYMTAB_INTACT is not set.
	 * If there is no symbol in the input object or all the symbols are
	 * stripped, then free all the resouces allotted for symbol table,
	 * and clear SYMTAB_EXIST flag.
	 */
	if (((ecp->flags & SYMTAB_INTACT) == 0) && !generate_symbols(ecp)) {
		TAILQ_REMOVE(&ecp->v_sec, ecp->symtab, sec_list);
		TAILQ_REMOVE(&ecp->v_sec, ecp->strtab, sec_list);
		free(ecp->symtab->buf);
		free(ecp->symtab);
		free(ecp->strtab->buf);
		free(ecp->strtab);
		ecp->symtab = NULL;
		ecp->strtab = NULL;
		ecp->flags &= ~SYMTAB_EXIST;
		return;
	}

	/* Create output Elf_Scn for .symtab and .strtab. */
	if ((sy->os = elf_newscn(ecp->eout)) == NULL ||
	    (st->os = elf_newscn(ecp->eout)) == NULL)
		errx(EXIT_FAILURE, "elf_newscn failed: %s",
		    elf_errmsg(-1));
	/* Update secndx anyway. */
	ecp->secndx[elf_ndxscn(sy->is)] = elf_ndxscn(sy->os);
	ecp->secndx[elf_ndxscn(st->is)] = elf_ndxscn(st->os);

	/*
	 * Copy .symtab and .strtab section headers from input to output
	 * object to start with, these will be overridden later if need.
	 */
	copy_shdr(ecp, sy, ".symtab", 1, 0);
	copy_shdr(ecp, st, ".strtab", 1, 0);

	/* Copy verbatim if symbol table is intact. */
	if (ecp->flags & SYMTAB_INTACT) {
		copy_data(sy);
		copy_data(st);
		return;
	}

	create_symtab_data(ecp);
}

void
free_symtab(struct elfcopy *ecp)
{
	struct symbuf	*sy_buf;
	struct strbuf	*st_buf;
	struct sthash	*sh, *shtmp;
	int i;

	if (ecp->symtab != NULL && ecp->symtab->buf != NULL) {
		sy_buf = ecp->symtab->buf;
		if (sy_buf->l32 != NULL)
			free(sy_buf->l32);
		if (sy_buf->g32 != NULL)
			free(sy_buf->g32);
		if (sy_buf->l64 != NULL)
			free(sy_buf->l64);
		if (sy_buf->g64 != NULL)
			free(sy_buf->g64);
	}

	if (ecp->strtab != NULL && ecp->strtab->buf != NULL) {
		st_buf = ecp->strtab->buf;
		if (st_buf->l.buf != NULL)
			free(st_buf->l.buf);
		if (st_buf->g.buf != NULL)
			free(st_buf->g.buf);
		for (i = 0; i < STHASHSIZE; i++) {
			LIST_FOREACH_SAFE(sh, &st_buf->l.hash[i], sh_next,
			    shtmp) {
				LIST_REMOVE(sh, sh_next);
				free(sh);
			}
			LIST_FOREACH_SAFE(sh, &st_buf->g.hash[i], sh_next,
			    shtmp) {
				LIST_REMOVE(sh, sh_next);
				free(sh);
			}
		}
	}

	if (ecp->symndx != NULL) {
		free(ecp->symndx);
		ecp->symndx = NULL;
	}
	if (ecp->v_rel != NULL) {
		free(ecp->v_rel);
		ecp->v_rel = NULL;
	}
	if (ecp->v_grp != NULL) {
		free(ecp->v_grp);
		ecp->v_grp = NULL;
	}
	if (ecp->v_secsym != NULL) {
		free(ecp->v_secsym);
		ecp->v_secsym = NULL;
	}
}

void
create_external_symtab(struct elfcopy *ecp)
{
	struct section *s;
	struct symbuf *sy_buf;
	struct strbuf *st_buf;
	GElf_Shdr sh;
	size_t ndx;

	if (ecp->oec == ELFCLASS32)
		ecp->symtab = create_external_section(ecp, ".symtab", NULL,
		    NULL, 0, 0, SHT_SYMTAB, ELF_T_SYM, 0, 4, 0, 0);
	else
		ecp->symtab = create_external_section(ecp, ".symtab", NULL,
		    NULL, 0, 0, SHT_SYMTAB, ELF_T_SYM, 0, 8, 0, 0);

	ecp->strtab = create_external_section(ecp, ".strtab", NULL, NULL, 0, 0,
	    SHT_STRTAB, ELF_T_BYTE, 0, 1, 0, 0);

	/* Let sh_link field of .symtab section point to .strtab section. */
	if (gelf_getshdr(ecp->symtab->os, &sh) == NULL)
		errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));
	sh.sh_link = elf_ndxscn(ecp->strtab->os);
	if (!gelf_update_shdr(ecp->symtab->os, &sh))
		errx(EXIT_FAILURE, "gelf_update_shdr() failed: %s",
		    elf_errmsg(-1));

	/* Create buffers for .symtab and .strtab. */
	if ((sy_buf = calloc(1, sizeof(*sy_buf))) == NULL)
		err(EXIT_FAILURE, "calloc failed");
	if ((st_buf = calloc(1, sizeof(*st_buf))) == NULL)
		err(EXIT_FAILURE, "calloc failed");
	sy_buf->gcap = sy_buf->lcap = 64;
	st_buf->g.cap = 256;
	st_buf->l.cap = 64;
	st_buf->l.sz = 1;	/* '\0' at start. */
	st_buf->g.sz = 0;

	ecp->symtab->sz = 0;
	ecp->strtab->sz = 0;
	ecp->symtab->buf = sy_buf;
	ecp->strtab->buf = st_buf;

	/* Always create the special symbol at the symtab beginning. */
	add_to_symtab(ecp, NULL, 0, 0, SHN_UNDEF,
	    ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE), 0, 1);

	/* Create STT_SECTION symbols. */
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (s->pseudo)
			continue;
		if (strcmp(s->name, ".symtab") == 0 ||
		    strcmp(s->name, ".strtab") == 0 ||
		    strcmp(s->name, ".shstrtab") == 0)
			continue;
		(void) elf_errno();
		if ((ndx = elf_ndxscn(s->os)) == SHN_UNDEF) {
			warnx("elf_ndxscn failed: %s",
			    elf_errmsg(-1));
			continue;
		}
		add_to_symtab(ecp, NULL, 0, 0, ndx,
		    GELF_ST_INFO(STB_LOCAL, STT_SECTION), 0, 1);
	}
}

void
add_to_symtab(struct elfcopy *ecp, const char *name, uint64_t st_value,
    uint64_t st_size, uint16_t st_shndx, unsigned char st_info,
    unsigned char st_other, int ndx_known)
{
	struct symbuf *sy_buf;
	struct strbuf *st_buf;
	struct sthash *sh;
	uint32_t hash;
	int pos;

	/*
	 * Convenient macro for copying global/local 32/64 bit symbols
	 * from input object to the buffer created for output object.
	 * It handles buffer growing, st_name calculating and st_shndx
	 * updating for symbols with non-special section index.
	 */
#define	_ST_NAME_EMPTY_l 0
#define	_ST_NAME_EMPTY_g -1
#define	_ADDSYM(B, SZ) do {						\
	if (sy_buf->B##SZ == NULL) {					\
		sy_buf->B##SZ = malloc(sy_buf->B##cap *			\
		    sizeof(Elf##SZ##_Sym));				\
		if (sy_buf->B##SZ == NULL)				\
			err(EXIT_FAILURE, "malloc failed");		\
	} else if (sy_buf->n##B##s >= sy_buf->B##cap) {			\
		sy_buf->B##cap *= 2;					\
		sy_buf->B##SZ = realloc(sy_buf->B##SZ, sy_buf->B##cap *	\
		    sizeof(Elf##SZ##_Sym));				\
		if (sy_buf->B##SZ == NULL)				\
			err(EXIT_FAILURE, "realloc failed");		\
	}								\
	sy_buf->B##SZ[sy_buf->n##B##s].st_info	= st_info;		\
	sy_buf->B##SZ[sy_buf->n##B##s].st_other	= st_other;		\
	sy_buf->B##SZ[sy_buf->n##B##s].st_value	= st_value;		\
	sy_buf->B##SZ[sy_buf->n##B##s].st_size	= st_size;		\
	if (ndx_known)							\
		sy_buf->B##SZ[sy_buf->n##B##s].st_shndx = st_shndx;	\
	else if (st_shndx == SHN_UNDEF || st_shndx >= SHN_LORESERVE)	\
		sy_buf->B##SZ[sy_buf->n##B##s].st_shndx = st_shndx;	\
	else								\
		sy_buf->B##SZ[sy_buf->n##B##s].st_shndx	=		\
			ecp->secndx[st_shndx];				\
	if (st_buf->B.buf == NULL) {					\
		st_buf->B.buf = calloc(st_buf->B.cap,			\
		    sizeof(*st_buf->B.buf));				\
		if (st_buf->B.buf == NULL)				\
			err(EXIT_FAILURE, "malloc failed");		\
	}								\
	if (name != NULL && *name != '\0') {				\
		pos = lookup_exact_string(st_buf->B.hash, st_buf->B.buf,\
		    name);						\
		if (pos != -1)						\
			sy_buf->B##SZ[sy_buf->n##B##s].st_name = pos;	\
		else {							\
			sy_buf->B##SZ[sy_buf->n##B##s].st_name =	\
			    st_buf->B.sz;				\
			while (st_buf->B.sz + strlen(name) >=		\
			    st_buf->B.cap - 1) {			\
				st_buf->B.cap *= 2;			\
				st_buf->B.buf = realloc(st_buf->B.buf,	\
				    st_buf->B.cap);			\
				if (st_buf->B.buf == NULL)		\
					err(EXIT_FAILURE,		\
					    "realloc failed");		\
			}						\
			if ((sh = malloc(sizeof(*sh))) == NULL)		\
				err(EXIT_FAILURE, "malloc failed");	\
			sh->sh_off = st_buf->B.sz;			\
			hash = str_hash(name);				\
			LIST_INSERT_HEAD(&st_buf->B.hash[hash], sh,	\
			    sh_next);					\
			strncpy(&st_buf->B.buf[st_buf->B.sz], name,	\
			    strlen(name));				\
			st_buf->B.buf[st_buf->B.sz + strlen(name)] = '\0'; \
			st_buf->B.sz += strlen(name) + 1;		\
		}							\
	} else								\
		sy_buf->B##SZ[sy_buf->n##B##s].st_name = 		\
		    (Elf##SZ##_Word)_ST_NAME_EMPTY_##B;			\
	sy_buf->n##B##s++;						\
} while (0)

	sy_buf = ecp->symtab->buf;
	st_buf = ecp->strtab->buf;

	if (ecp->oec == ELFCLASS32) {
		if (is_local_symbol(st_info))
			_ADDSYM(l, 32);
		else
			_ADDSYM(g, 32);
	} else {
		if (is_local_symbol(st_info))
			_ADDSYM(l, 64);
		else
			_ADDSYM(g, 64);
	}

	/* Update section size. */
	ecp->symtab->sz = (sy_buf->nls + sy_buf->ngs) *
	    (ecp->oec == ELFCLASS32 ? sizeof(Elf32_Sym) : sizeof(Elf64_Sym));
	ecp->strtab->sz = st_buf->l.sz + st_buf->g.sz;

#undef	_ADDSYM
#undef	_ST_NAME_EMPTY_l
#undef	_ST_NAME_EMPTY_g
}

void
finalize_external_symtab(struct elfcopy *ecp)
{
	struct symbuf *sy_buf;
	struct strbuf *st_buf;
	int i;

	/*
	 * Update st_name for global/weak symbols. (global/weak symbols
	 * are put after local symbols)
	 */
	sy_buf = ecp->symtab->buf;
	st_buf = ecp->strtab->buf;
	for (i = 0; (size_t) i < sy_buf->ngs; i++) {
		if (ecp->oec == ELFCLASS32) {
			if (sy_buf->g32[i].st_name == (Elf32_Word)-1)
				sy_buf->g32[i].st_name = 0;
			else
				sy_buf->g32[i].st_name += st_buf->l.sz;
		} else {
			if (sy_buf->g64[i].st_name == (Elf64_Word)-1)
				sy_buf->g64[i].st_name = 0;
			else
				sy_buf->g64[i].st_name += st_buf->l.sz;
		}
	}
}

void
create_symtab_data(struct elfcopy *ecp)
{
	struct section	*sy, *st;
	struct symbuf	*sy_buf;
	struct strbuf	*st_buf;
	Elf_Data	*gsydata, *lsydata, *gstdata, *lstdata;
	GElf_Shdr	 shy, sht;

	sy = ecp->symtab;
	st = ecp->strtab;

	if (gelf_getshdr(sy->os, &shy) == NULL)
		errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));
	if (gelf_getshdr(st->os, &sht) == NULL)
		errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));

	/*
	 * Create two Elf_Data for .symtab section of output object, one
	 * for local symbols and another for global symbols. Note that
	 * local symbols appear first in the .symtab.
	 */
	sy_buf = sy->buf;
	if (sy_buf->nls > 0) {
		if ((lsydata = elf_newdata(sy->os)) == NULL)
			errx(EXIT_FAILURE, "elf_newdata() failed: %s.",
			     elf_errmsg(-1));
		if (ecp->oec == ELFCLASS32) {
			lsydata->d_align	= 4;
			lsydata->d_off		= 0;
			lsydata->d_buf		= sy_buf->l32;
			lsydata->d_size		= sy_buf->nls *
				sizeof(Elf32_Sym);
			lsydata->d_type		= ELF_T_SYM;
			lsydata->d_version	= EV_CURRENT;
		} else {
			lsydata->d_align	= 8;
			lsydata->d_off		= 0;
			lsydata->d_buf		= sy_buf->l64;
			lsydata->d_size		= sy_buf->nls *
				sizeof(Elf64_Sym);
			lsydata->d_type		= ELF_T_SYM;
			lsydata->d_version	= EV_CURRENT;
		}
	}
	if (sy_buf->ngs > 0) {
		if ((gsydata = elf_newdata(sy->os)) == NULL)
			errx(EXIT_FAILURE, "elf_newdata() failed: %s.",
			     elf_errmsg(-1));
		if (ecp->oec == ELFCLASS32) {
			gsydata->d_align	= 4;
			gsydata->d_off		= sy_buf->nls *
				sizeof(Elf32_Sym);
			gsydata->d_buf		= sy_buf->g32;
			gsydata->d_size		= sy_buf->ngs *
				sizeof(Elf32_Sym);
			gsydata->d_type		= ELF_T_SYM;
			gsydata->d_version	= EV_CURRENT;
		} else {
			gsydata->d_align	= 8;
			gsydata->d_off		= sy_buf->nls *
				sizeof(Elf64_Sym);
			gsydata->d_buf		= sy_buf->g64;
			gsydata->d_size		= sy_buf->ngs *
				sizeof(Elf64_Sym);
			gsydata->d_type		= ELF_T_SYM;
			gsydata->d_version	= EV_CURRENT;
		}
	}

	/*
	 * Create two Elf_Data for .strtab, one for local symbol name
	 * and another for globals. Same as .symtab, local symbol names
	 * appear first.
	 */
	st_buf = st->buf;
	if ((lstdata = elf_newdata(st->os)) == NULL)
		errx(EXIT_FAILURE, "elf_newdata() failed: %s.",
		    elf_errmsg(-1));
	lstdata->d_align	= 1;
	lstdata->d_off		= 0;
	lstdata->d_buf		= st_buf->l.buf;
	lstdata->d_size		= st_buf->l.sz;
	lstdata->d_type		= ELF_T_BYTE;
	lstdata->d_version	= EV_CURRENT;

	if (st_buf->g.sz > 0) {
		if ((gstdata = elf_newdata(st->os)) == NULL)
			errx(EXIT_FAILURE, "elf_newdata() failed: %s.",
			    elf_errmsg(-1));
		gstdata->d_align	= 1;
		gstdata->d_off		= lstdata->d_size;
		gstdata->d_buf		= st_buf->g.buf;
		gstdata->d_size		= st_buf->g.sz;
		gstdata->d_type		= ELF_T_BYTE;
		gstdata->d_version	= EV_CURRENT;
	}

	shy.sh_addr		= 0;
	shy.sh_addralign	= (ecp->oec == ELFCLASS32 ? 4 : 8);
	shy.sh_size		= sy->sz;
	shy.sh_type		= SHT_SYMTAB;
	shy.sh_flags		= 0;
	shy.sh_entsize		= gelf_fsize(ecp->eout, ELF_T_SYM, 1,
	    EV_CURRENT);
	/*
	 * According to SYSV abi, here sh_info is one greater than
	 * the symbol table index of the last local symbol(binding
	 * STB_LOCAL).
	 */
	shy.sh_info		= sy_buf->nls;

	sht.sh_addr		= 0;
	sht.sh_addralign	= 1;
	sht.sh_size		= st->sz;
	sht.sh_type		= SHT_STRTAB;
	sht.sh_flags		= 0;
	sht.sh_entsize		= 0;
	sht.sh_info		= 0;
	sht.sh_link		= 0;

	if (!gelf_update_shdr(sy->os, &shy))
		errx(EXIT_FAILURE, "gelf_update_shdr() failed: %s",
		    elf_errmsg(-1));
	if (!gelf_update_shdr(st->os, &sht))
		errx(EXIT_FAILURE, "gelf_update_shdr() failed: %s",
		    elf_errmsg(-1));
}

void
add_to_symop_list(struct elfcopy *ecp, const char *name, const char *newname,
    unsigned int op)
{
	struct symop *s;

	assert (name != NULL);
	STAILQ_FOREACH(s, &ecp->v_symop, symop_list)
		if (!strcmp(name, s->name))
			goto found;

	if ((s = calloc(1, sizeof(*s))) == NULL)
		errx(EXIT_FAILURE, "not enough memory");
	STAILQ_INSERT_TAIL(&ecp->v_symop, s, symop_list);
	s->name = name;
found:
	if (op == SYMOP_REDEF)
		s->newname = newname;
	s->op |= op;
}

struct symop *
lookup_symop_list(struct elfcopy *ecp, const char *name, unsigned int op)
{
	struct symop *s, *ret;
	const char *pattern;

	STAILQ_FOREACH(s, &ecp->v_symop, symop_list) {
		if ((s->op & op) == 0)
			continue;
		if (name == NULL || !strcmp(name, s->name))
			return (s);
		if ((ecp->flags & WILDCARD) == 0)
			continue;

		/* Handle wildcards. */
		pattern = s->name;
		if (pattern[0] == '!') {
			/* Negative match. */
			pattern++;
			ret = NULL;
		} else {
			/* Regular wildcard match. */
			ret = s;
		}
		if (!fnmatch(pattern, name, 0))
			return (ret);
	}

	return (NULL);
}

static int
lookup_exact_string(hash_head *buckets, const char *buf, const char *s)
{
	struct sthash	*sh;
	uint32_t	 hash;

	hash = str_hash(s);
	LIST_FOREACH(sh, &buckets[hash], sh_next)
		if (strcmp(buf + sh->sh_off, s) == 0)
			return sh->sh_off;
	return (-1);
}

uint32_t
str_hash(const char *s)
{
	uint32_t hash;

	for (hash = 2166136261UL; *s; s++)
		hash = (hash ^ *s) * 16777619;

	return (hash & (STHASHSIZE - 1));
}
