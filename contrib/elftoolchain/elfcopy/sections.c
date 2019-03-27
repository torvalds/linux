/*-
 * Copyright (c) 2007-2011,2014 Kai Wang
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
#include <sys/stat.h>
#include <err.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elfcopy.h"

ELFTC_VCSID("$Id: sections.c 3646 2018-10-27 02:25:39Z emaste $");

static void	add_gnu_debuglink(struct elfcopy *ecp);
static uint32_t calc_crc32(const char *p, size_t len, uint32_t crc);
static void	check_section_rename(struct elfcopy *ecp, struct section *s);
static void	filter_reloc(struct elfcopy *ecp, struct section *s);
static int	get_section_flags(struct elfcopy *ecp, const char *name);
static void	insert_sections(struct elfcopy *ecp);
static void	insert_to_strtab(struct section *t, const char *s);
static int	is_append_section(struct elfcopy *ecp, const char *name);
static int	is_compress_section(struct elfcopy *ecp, const char *name);
static int	is_debug_section(const char *name);
static int	is_dwo_section(const char *name);
static int	is_modify_section(struct elfcopy *ecp, const char *name);
static int	is_print_section(struct elfcopy *ecp, const char *name);
static int	lookup_string(struct section *t, const char *s);
static void	modify_section(struct elfcopy *ecp, struct section *s);
static void	pad_section(struct elfcopy *ecp, struct section *s);
static void	print_data(const char *d, size_t sz);
static void	print_section(struct section *s);
static void	*read_section(struct section *s, size_t *size);
static void	update_reloc(struct elfcopy *ecp, struct section *s);
static void	update_section_group(struct elfcopy *ecp, struct section *s);

int
is_remove_section(struct elfcopy *ecp, const char *name)
{

	/* Always keep section name table */
	if (strcmp(name, ".shstrtab") == 0)
		return 0;
	if (strcmp(name, ".symtab") == 0 ||
	    strcmp(name, ".strtab") == 0) {
		if (ecp->strip == STRIP_ALL && lookup_symop_list(
		    ecp, NULL, SYMOP_KEEP) == NULL)
			return (1);
		else
			return (0);
	}

	if (ecp->strip == STRIP_DWO && is_dwo_section(name))
		return (1);
	if (ecp->strip == STRIP_NONDWO && !is_dwo_section(name))
		return (1);

	if (is_debug_section(name)) {
		if (ecp->strip == STRIP_ALL ||
		    ecp->strip == STRIP_DEBUG ||
		    ecp->strip == STRIP_UNNEEDED ||
		    (ecp->flags & DISCARD_LOCAL))
			return (1);
		if (ecp->strip == STRIP_NONDEBUG)
			return (0);
	}

	if ((ecp->flags & SEC_REMOVE) || (ecp->flags & SEC_COPY)) {
		struct sec_action *sac;

		sac = lookup_sec_act(ecp, name, 0);
		if ((ecp->flags & SEC_REMOVE) && sac != NULL && sac->remove)
			return (1);
		if ((ecp->flags & SEC_COPY) && (sac == NULL || !sac->copy))
			return (1);
	}

	return (0);
}

/*
 * Relocation section needs to be removed if the section it applies to
 * will be removed.
 */
int
is_remove_reloc_sec(struct elfcopy *ecp, uint32_t sh_info)
{
	const char	*name;
	GElf_Shdr	 ish;
	Elf_Scn		*is;
	size_t		 indx;
	int		 elferr;

	if (elf_getshstrndx(ecp->ein, &indx) == 0)
		errx(EXIT_FAILURE, "elf_getshstrndx failed: %s",
		    elf_errmsg(-1));

	is = NULL;
	while ((is = elf_nextscn(ecp->ein, is)) != NULL) {
		if (sh_info == elf_ndxscn(is)) {
			if (gelf_getshdr(is, &ish) == NULL)
				errx(EXIT_FAILURE, "gelf_getshdr failed: %s",
				    elf_errmsg(-1));
			if ((name = elf_strptr(ecp->ein, indx, ish.sh_name)) ==
			    NULL)
				errx(EXIT_FAILURE, "elf_strptr failed: %s",
				    elf_errmsg(-1));
			if (is_remove_section(ecp, name))
				return (1);
			else
				return (0);
		}
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));

	/* Remove reloc section if we can't find the target section. */
	return (1);
}

static int
is_append_section(struct elfcopy *ecp, const char *name)
{
	struct sec_action *sac;

	sac = lookup_sec_act(ecp, name, 0);
	if (sac != NULL && sac->append != 0 && sac->string != NULL)
		return (1);

	return (0);
}

static int
is_compress_section(struct elfcopy *ecp, const char *name)
{
	struct sec_action *sac;

	sac = lookup_sec_act(ecp, name, 0);
	if (sac != NULL && sac->compress != 0)
		return (1);

	return (0);
}

static void
check_section_rename(struct elfcopy *ecp, struct section *s)
{
	struct sec_action *sac;
	char *prefix;
	size_t namelen;

	if (s->pseudo)
		return;

	sac = lookup_sec_act(ecp, s->name, 0);
	if (sac != NULL && sac->rename)
		s->name = sac->newname;

	if (!strcmp(s->name, ".symtab") ||
	    !strcmp(s->name, ".strtab") ||
	    !strcmp(s->name, ".shstrtab"))
		return;

	prefix = NULL;
	if (s->loadable && ecp->prefix_alloc != NULL)
		prefix = ecp->prefix_alloc;
	else if (ecp->prefix_sec != NULL)
		prefix = ecp->prefix_sec;

	if (prefix != NULL) {
		namelen = strlen(s->name) + strlen(prefix) + 1;
		if ((s->newname = malloc(namelen)) == NULL)
			err(EXIT_FAILURE, "malloc failed");
		snprintf(s->newname, namelen, "%s%s", prefix, s->name);
		s->name = s->newname;
	}
}

static int
get_section_flags(struct elfcopy *ecp, const char *name)
{
	struct sec_action *sac;

	sac = lookup_sec_act(ecp, name, 0);
	if (sac != NULL && sac->flags)
		return sac->flags;

	return (0);
}

/*
 * Determine whether the section are debugging section.
 * According to libbfd, debugging sections are recognized
 * only by name.
 */
static int
is_debug_section(const char *name)
{
	const char *dbg_sec[] = {
		".apple_",
		".debug",
		".gnu.linkonce.wi.",
		".line",
		".stab",
		NULL
	};
	const char **p;

	for(p = dbg_sec; *p; p++) {
		if (strncmp(name, *p, strlen(*p)) == 0)
			return (1);
	}

	return (0);
}

static int
is_dwo_section(const char *name)
{
	size_t len;

	if ((len = strlen(name)) > 4 && strcmp(name + len - 4, ".dwo") == 0)
		return (1);
	return (0);
}

static int
is_print_section(struct elfcopy *ecp, const char *name)
{
	struct sec_action *sac;

	sac = lookup_sec_act(ecp, name, 0);
	if (sac != NULL && sac->print != 0)
		return (1);

	return (0);
}

static int
is_modify_section(struct elfcopy *ecp, const char *name)
{

	if (is_append_section(ecp, name) ||
	    is_compress_section(ecp, name))
		return (1);

	return (0);
}

struct sec_action*
lookup_sec_act(struct elfcopy *ecp, const char *name, int add)
{
	struct sec_action *sac;

	if (name == NULL)
		return NULL;

	STAILQ_FOREACH(sac, &ecp->v_sac, sac_list) {
		if (strcmp(name, sac->name) == 0)
			return sac;
	}

	if (add == 0)
		return NULL;

	if ((sac = malloc(sizeof(*sac))) == NULL)
		errx(EXIT_FAILURE, "not enough memory");
	memset(sac, 0, sizeof(*sac));
	sac->name = name;
	STAILQ_INSERT_TAIL(&ecp->v_sac, sac, sac_list);

	return (sac);
}

void
free_sec_act(struct elfcopy *ecp)
{
	struct sec_action *sac, *sac_temp;

	STAILQ_FOREACH_SAFE(sac, &ecp->v_sac, sac_list, sac_temp) {
		STAILQ_REMOVE(&ecp->v_sac, sac, sec_action, sac_list);
		free(sac);
	}
}

void
insert_to_sec_list(struct elfcopy *ecp, struct section *sec, int tail)
{
	struct section *s;

	if (!tail) {
		TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
			if (sec->off < s->off) {
				TAILQ_INSERT_BEFORE(s, sec, sec_list);
				goto inc_nos;
			}
		}
	}

	TAILQ_INSERT_TAIL(&ecp->v_sec, sec, sec_list);

inc_nos:
	if (sec->pseudo == 0)
		ecp->nos++;
}

/*
 * First step of section creation: create scn and internal section
 * structure, discard sections to be removed.
 */
void
create_scn(struct elfcopy *ecp)
{
	struct section	*s;
	const char	*name;
	Elf_Scn		*is;
	GElf_Shdr	 ish;
	size_t		 indx;
	uint64_t	 oldndx, newndx;
	int		 elferr, sec_flags, reorder;

	/*
	 * Insert a pseudo section that contains the ELF header
	 * and program header. Used as reference for section offset
	 * or load address adjustment.
	 */
	if ((s = calloc(1, sizeof(*s))) == NULL)
		err(EXIT_FAILURE, "calloc failed");
	s->off = 0;
	s->sz = gelf_fsize(ecp->eout, ELF_T_EHDR, 1, EV_CURRENT) +
	    gelf_fsize(ecp->eout, ELF_T_PHDR, ecp->ophnum, EV_CURRENT);
	s->align = 1;
	s->pseudo = 1;
	s->loadable = add_to_inseg_list(ecp, s);
	insert_to_sec_list(ecp, s, 0);

	/* Create internal .shstrtab section. */
	init_shstrtab(ecp);

	if (elf_getshstrndx(ecp->ein, &indx) == 0)
		errx(EXIT_FAILURE, "elf_getshstrndx failed: %s",
		    elf_errmsg(-1));

	reorder = 0;
	is = NULL;
	while ((is = elf_nextscn(ecp->ein, is)) != NULL) {
		if (gelf_getshdr(is, &ish) == NULL)
			errx(EXIT_FAILURE, "gelf_getshdr failed: %s",
			    elf_errmsg(-1));
		if ((name = elf_strptr(ecp->ein, indx, ish.sh_name)) == NULL)
			errx(EXIT_FAILURE, "elf_strptr failed: %s",
			    elf_errmsg(-1));

		/* Skip sections to be removed. */
		if (is_remove_section(ecp, name))
			continue;

		/*
		 * Relocation section need to be remove if the section
		 * it applies will be removed.
		 */
		if (ish.sh_type == SHT_REL || ish.sh_type == SHT_RELA)
			if (ish.sh_info != 0 &&
			    is_remove_reloc_sec(ecp, ish.sh_info))
				continue;

		/*
		 * Section groups should be removed if symbol table will
		 * be removed. (section group's signature stored in symbol
		 * table)
		 */
		if (ish.sh_type == SHT_GROUP && ecp->strip == STRIP_ALL)
			continue;

		/* Get section flags set by user. */
		sec_flags = get_section_flags(ecp, name);

		/* Create internal section object. */
		if (strcmp(name, ".shstrtab") != 0) {
			if ((s = calloc(1, sizeof(*s))) == NULL)
				err(EXIT_FAILURE, "calloc failed");
			s->name		= name;
			s->is		= is;
			s->off		= ish.sh_offset;
			s->sz		= ish.sh_size;
			s->align	= ish.sh_addralign;
			s->type		= ish.sh_type;
			s->flags	= ish.sh_flags;
			s->vma		= ish.sh_addr;

			/*
			 * Search program headers to determine whether section
			 * is loadable, but if user explicitly set section flags
			 * while neither "load" nor "alloc" is set, we make the
			 * section unloadable.
			 *
			 * Sections in relocatable object is loadable if
			 * section flag SHF_ALLOC is set.
			 */
			if (sec_flags &&
			    (sec_flags & (SF_LOAD | SF_ALLOC)) == 0)
				s->loadable = 0;
			else {
				s->loadable = add_to_inseg_list(ecp, s);
				if ((ecp->flags & RELOCATABLE) &&
				    (ish.sh_flags & SHF_ALLOC))
					s->loadable = 1;
			}
		} else {
			/* Assuming .shstrtab is "unloadable". */
			s		= ecp->shstrtab;
			s->off		= ish.sh_offset;
		}

		oldndx = newndx = SHN_UNDEF;
		if (strcmp(name, ".symtab") != 0 &&
		    strcmp(name, ".strtab") != 0) {
			if (!strcmp(name, ".shstrtab")) {
				/*
				 * Add sections specified by --add-section and
				 * gnu debuglink. we want these sections have
				 * smaller index than .shstrtab section.
				 */
				if (ecp->debuglink != NULL)
					add_gnu_debuglink(ecp);
				if (ecp->flags & SEC_ADD)
					insert_sections(ecp);
			}
 			if ((s->os = elf_newscn(ecp->eout)) == NULL)
				errx(EXIT_FAILURE, "elf_newscn failed: %s",
				    elf_errmsg(-1));
			if ((newndx = elf_ndxscn(s->os)) == SHN_UNDEF)
				errx(EXIT_FAILURE, "elf_ndxscn failed: %s",
				    elf_errmsg(-1));
		}
		if ((oldndx = elf_ndxscn(is)) == SHN_UNDEF)
			errx(EXIT_FAILURE, "elf_ndxscn failed: %s",
			    elf_errmsg(-1));
		if (oldndx != SHN_UNDEF && newndx != SHN_UNDEF)
			ecp->secndx[oldndx] = newndx;

		/*
		 * If strip action is STRIP_NONDEBUG(only keep debug),
		 * change sections type of loadable sections and section
		 * groups to SHT_NOBITS, and the content of those sections
		 * will be discarded. However, SHT_NOTE sections should
		 * be kept.
		 */
		if (ecp->strip == STRIP_NONDEBUG) {
			if (((ish.sh_flags & SHF_ALLOC) ||
			    (ish.sh_flags & SHF_GROUP)) &&
			    ish.sh_type != SHT_NOTE)
				s->type = SHT_NOBITS;
		}

		check_section_rename(ecp, s);

		/* create section header based on input object. */
		if (strcmp(name, ".symtab") != 0 &&
		    strcmp(name, ".strtab") != 0 &&
		    strcmp(name, ".shstrtab") != 0) {
			copy_shdr(ecp, s, NULL, 0, sec_flags);
			/*
			 * elfcopy puts .symtab, .strtab and .shstrtab
			 * sections in the end of the output object.
			 * If the input objects have more sections
			 * after any of these 3 sections, the section
			 * table will be reordered. section symbols
			 * should be regenerated for relocations.
			 */
			if (reorder)
				ecp->flags &= ~SYMTAB_INTACT;
		} else
			reorder = 1;

		if (strcmp(name, ".symtab") == 0) {
			ecp->flags |= SYMTAB_EXIST;
			ecp->symtab = s;
		}
		if (strcmp(name, ".strtab") == 0)
			ecp->strtab = s;

		insert_to_sec_list(ecp, s, 0);
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));
}

struct section *
insert_shtab(struct elfcopy *ecp, int tail)
{
	struct section	*s, *shtab;
	GElf_Ehdr	 ieh;
	int		 nsecs;

	/*
	 * Treat section header table as a "pseudo" section, insert it
	 * into section list, so later it will get sorted and resynced
	 * just as normal sections.
	 */
	if ((shtab = calloc(1, sizeof(*shtab))) == NULL)
		errx(EXIT_FAILURE, "calloc failed");
	if (!tail) {
		/*
		 * "shoff" of input object is used as a hint for section
		 * resync later.
		 */
		if (gelf_getehdr(ecp->ein, &ieh) == NULL)
			errx(EXIT_FAILURE, "gelf_getehdr() failed: %s",
			    elf_errmsg(-1));
		shtab->off = ieh.e_shoff;
	} else
		shtab->off = 0;
	/* Calculate number of sections in the output object. */
	nsecs = 0;
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (!s->pseudo)
			nsecs++;
	}
	/* Remember there is always a null section, so we +1 here. */
	shtab->sz = gelf_fsize(ecp->eout, ELF_T_SHDR, nsecs + 1, EV_CURRENT);
	if (shtab->sz == 0)
		errx(EXIT_FAILURE, "gelf_fsize() failed: %s", elf_errmsg(-1));
	shtab->align = (ecp->oec == ELFCLASS32 ? 4 : 8);
	shtab->loadable = 0;
	shtab->pseudo = 1;
	insert_to_sec_list(ecp, shtab, tail);

	return (shtab);
}

void
copy_content(struct elfcopy *ecp)
{
	struct section *s;

	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		/* Skip pseudo section. */
		if (s->pseudo)
			continue;

		/* Skip special sections. */
		if (strcmp(s->name, ".symtab") == 0 ||
		    strcmp(s->name, ".strtab") == 0 ||
		    strcmp(s->name, ".shstrtab") == 0)
			continue;

		/*
		 * If strip action is STRIP_ALL, relocation info need
		 * to be stripped. Skip filtering otherwisw.
		 */
		if (ecp->strip == STRIP_ALL &&
		    (s->type == SHT_REL || s->type == SHT_RELA))
			filter_reloc(ecp, s);

		/*
		 * The section indices in the SHT_GROUP section needs
		 * to be updated since we might have stripped some
		 * sections and changed section numbering.
		 */
		if (s->type == SHT_GROUP)
			update_section_group(ecp, s);

		if (is_modify_section(ecp, s->name))
			modify_section(ecp, s);

		copy_data(s);

		/*
		 * If symbol table is modified, relocation info might
		 * need update, as symbol index may have changed.
		 */
		if ((ecp->flags & SYMTAB_INTACT) == 0 &&
		    (ecp->flags & SYMTAB_EXIST) &&
		    (s->type == SHT_REL || s->type == SHT_RELA))
			update_reloc(ecp, s);

		if (is_print_section(ecp, s->name))
			print_section(s);
	}
}


/*
 * Update section group section. The section indices in the SHT_GROUP
 * section need update after section numbering changed.
 */
static void
update_section_group(struct elfcopy *ecp, struct section *s)
{
	GElf_Shdr	 ish;
	Elf_Data	*id;
	uint32_t	*ws, *wd;
	uint64_t	 n;
	size_t		 ishnum;
	int		 i, j;

	if (!elf_getshnum(ecp->ein, &ishnum))
		errx(EXIT_FAILURE, "elf_getshnum failed: %s",
		    elf_errmsg(-1));

	if (gelf_getshdr(s->is, &ish) == NULL)
		errx(EXIT_FAILURE, "gelf_getehdr() failed: %s",
		    elf_errmsg(-1));

	if ((id = elf_getdata(s->is, NULL)) == NULL)
		errx(EXIT_FAILURE, "elf_getdata() failed: %s",
		    elf_errmsg(-1));

	if (ish.sh_size == 0)
		return;

	if (ish.sh_entsize == 0)
		ish.sh_entsize = 4;

	ws = id->d_buf;

	/* We only support COMDAT section. */
#ifndef GRP_COMDAT
#define	GRP_COMDAT 0x1
#endif
	if ((*ws & GRP_COMDAT) == 0)
		return;

	if ((s->buf = malloc(ish.sh_size)) == NULL)
		err(EXIT_FAILURE, "malloc failed");

	s->sz = ish.sh_size;

	wd = s->buf;

	/* Copy the flag word as-is. */
	*wd = *ws;

	/* Update the section indices. */
	n = ish.sh_size / ish.sh_entsize;
	for(i = 1, j = 1; (uint64_t)i < n; i++) {
		if (ws[i] != SHN_UNDEF && ws[i] < ishnum &&
		    ecp->secndx[ws[i]] != 0)
			wd[j++] = ecp->secndx[ws[i]];
		else
			s->sz -= 4;
	}

	s->nocopy = 1;
}

/*
 * Filter relocation entries, only keep those entries whose
 * symbol is in the keep list.
 */
static void
filter_reloc(struct elfcopy *ecp, struct section *s)
{
	const char	*name;
	GElf_Shdr	 ish;
	GElf_Rel	 rel;
	GElf_Rela	 rela;
	Elf32_Rel	*rel32;
	Elf64_Rel	*rel64;
	Elf32_Rela	*rela32;
	Elf64_Rela	*rela64;
	Elf_Data	*id;
	uint64_t	 cap, n, nrels, sym;
	int		 elferr, i;

	if (gelf_getshdr(s->is, &ish) == NULL)
		errx(EXIT_FAILURE, "gelf_getehdr() failed: %s",
		    elf_errmsg(-1));

	/* We don't want to touch relocation info for dynamic symbols. */
	if ((ecp->flags & SYMTAB_EXIST) == 0) {
		/*
		 * No symbol table in output.  If sh_link points to a section
		 * that exists in the output object, this relocation section
		 * is for dynamic symbols.  Don't touch it.
		 */
		if (ish.sh_link != 0 && ecp->secndx[ish.sh_link] != 0)
			return;
	} else {
		/* Symbol table exist, check if index equals. */
		if (ish.sh_link != elf_ndxscn(ecp->symtab->is))
			return;
	}

#define	COPYREL(REL, SZ) do {					\
	if (nrels == 0) {					\
		if ((REL##SZ = malloc(cap *			\
		    sizeof(*REL##SZ))) == NULL)			\
			err(EXIT_FAILURE, "malloc failed");	\
	}							\
	if (nrels >= cap) {					\
		cap *= 2;					\
		if ((REL##SZ = realloc(REL##SZ, cap *		\
		    sizeof(*REL##SZ))) == NULL)			\
			err(EXIT_FAILURE, "realloc failed");	\
	}							\
	REL##SZ[nrels].r_offset = REL.r_offset;			\
	REL##SZ[nrels].r_info	= REL.r_info;			\
	if (s->type == SHT_RELA)				\
		rela##SZ[nrels].r_addend = rela.r_addend;	\
	nrels++;						\
} while (0)

	nrels = 0;
	cap = 4;		/* keep list is usually small. */
	rel32 = NULL;
	rel64 = NULL;
	rela32 = NULL;
	rela64 = NULL;
	if ((id = elf_getdata(s->is, NULL)) == NULL)
		errx(EXIT_FAILURE, "elf_getdata() failed: %s",
		    elf_errmsg(-1));
	n = ish.sh_size / ish.sh_entsize;
	for(i = 0; (uint64_t)i < n; i++) {
		if (s->type == SHT_REL) {
			if (gelf_getrel(id, i, &rel) != &rel)
				errx(EXIT_FAILURE, "gelf_getrel failed: %s",
				    elf_errmsg(-1));
			sym = GELF_R_SYM(rel.r_info);
		} else {
			if (gelf_getrela(id, i, &rela) != &rela)
				errx(EXIT_FAILURE, "gelf_getrel failed: %s",
				    elf_errmsg(-1));
			sym = GELF_R_SYM(rela.r_info);
		}
		/*
		 * If a relocation references a symbol and we are omitting
		 * either that symbol or the entire symbol table we cannot
		 * produce valid output, and so just omit the relocation.
		 * Broken output like this is generally not useful, but some
		 * uses of elfcopy/strip rely on it - for example, GCC's build
		 * process uses it to check for build reproducibility by
		 * stripping objects and comparing them.
		 *
		 * Relocations that do not reference a symbol are retained.
		 */
		if (sym != 0) {
			if (ish.sh_link == 0 || ecp->secndx[ish.sh_link] == 0)
				continue;
			name = elf_strptr(ecp->ein, elf_ndxscn(ecp->strtab->is),
			    sym);
			if (name == NULL)
				errx(EXIT_FAILURE, "elf_strptr failed: %s",
				    elf_errmsg(-1));
			if (lookup_symop_list(ecp, name, SYMOP_KEEP) == NULL)
				continue;
		}
		if (ecp->oec == ELFCLASS32) {
			if (s->type == SHT_REL)
				COPYREL(rel, 32);
			else
				COPYREL(rela, 32);
		} else {
			if (s->type == SHT_REL)
				COPYREL(rel, 64);
			else
				COPYREL(rela, 64);
		}
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_getdata() failed: %s",
		    elf_errmsg(elferr));

	if (ecp->oec == ELFCLASS32) {
		if (s->type == SHT_REL)
			s->buf = rel32;
		else
			s->buf = rela32;
	} else {
		if (s->type == SHT_REL)
			s->buf = rel64;
		else
			s->buf = rela64;
	}
	s->sz = gelf_fsize(ecp->eout, (s->type == SHT_REL ? ELF_T_REL :
	    ELF_T_RELA), nrels, EV_CURRENT);
	s->nocopy = 1;
}

static void
update_reloc(struct elfcopy *ecp, struct section *s)
{
	GElf_Shdr	 osh;
	GElf_Rel	 rel;
	GElf_Rela	 rela;
	Elf_Data	*od;
	uint64_t	 n;
	int		 i;

#define UPDATEREL(REL) do {						\
	if (gelf_get##REL(od, i, &REL) != &REL)				\
		errx(EXIT_FAILURE, "gelf_get##REL failed: %s",		\
		    elf_errmsg(-1));					\
	REL.r_info = GELF_R_INFO(ecp->symndx[GELF_R_SYM(REL.r_info)],	\
	    GELF_R_TYPE(REL.r_info));					\
	if (!gelf_update_##REL(od, i, &REL))				\
		errx(EXIT_FAILURE, "gelf_update_##REL failed: %s",	\
		    elf_errmsg(-1));					\
} while(0)

	if (s->sz == 0)
		return;
	if (gelf_getshdr(s->os, &osh) == NULL)
		errx(EXIT_FAILURE, "gelf_getehdr() failed: %s",
		    elf_errmsg(-1));
	/* Only process .symtab reloc info. */
	if (osh.sh_link != elf_ndxscn(ecp->symtab->is))
		return;
	if ((od = elf_getdata(s->os, NULL)) == NULL)
		errx(EXIT_FAILURE, "elf_getdata() failed: %s",
		    elf_errmsg(-1));
	n = osh.sh_size / osh.sh_entsize;
	for(i = 0; (uint64_t)i < n; i++) {
		if (s->type == SHT_REL)
			UPDATEREL(rel);
		else
			UPDATEREL(rela);
	}
}

static void
pad_section(struct elfcopy *ecp, struct section *s)
{
	GElf_Shdr	 osh;
	Elf_Data	*od;

	if (s == NULL || s->pad_sz == 0)
		return;

	if ((s->pad = malloc(s->pad_sz)) == NULL)
		err(EXIT_FAILURE, "malloc failed");
	memset(s->pad, ecp->fill, s->pad_sz);

	/* Create a new Elf_Data to contain the padding bytes. */
	if ((od = elf_newdata(s->os)) == NULL)
		errx(EXIT_FAILURE, "elf_newdata() failed: %s",
		    elf_errmsg(-1));
	od->d_align = 1;
	od->d_off = s->sz;
	od->d_buf = s->pad;
	od->d_type = ELF_T_BYTE;
	od->d_size = s->pad_sz;
	od->d_version = EV_CURRENT;

	/* Update section header. */
	if (gelf_getshdr(s->os, &osh) == NULL)
		errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));
	osh.sh_size = s->sz + s->pad_sz;
	if (!gelf_update_shdr(s->os, &osh))
		errx(EXIT_FAILURE, "elf_update_shdr failed: %s",
		    elf_errmsg(-1));
}

void
resync_sections(struct elfcopy *ecp)
{
	struct section	*s, *ps;
	GElf_Shdr	 osh;
	uint64_t	 off;
	int		 first;

	ps = NULL;
	first = 1;
	off = 0;
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (first) {
			off = s->off;
			first = 0;
		}

		/*
		 * Ignore TLS sections with load address 0 and without
		 * content. We don't need to adjust their file offset or
		 * VMA, only the size matters.
		 */
		if (s->seg_tls != NULL && s->type == SHT_NOBITS &&
		    s->off == 0)
			continue;

		/* Align section offset. */
		if (s->align == 0)
			s->align = 1;
		if (off <= s->off) {
			if (!s->loadable || (ecp->flags & RELOCATABLE))
				s->off = roundup(off, s->align);
		} else {
			if (s->loadable && (ecp->flags & RELOCATABLE) == 0)
				warnx("moving loadable section %s, "
				    "is this intentional?", s->name);
			s->off = roundup(off, s->align);
		}

		/* Calculate next section offset. */
		off = s->off;
		if (s->pseudo || (s->type != SHT_NOBITS && s->type != SHT_NULL))
			off += s->sz;

		if (s->pseudo) {
			ps = NULL;
			continue;
		}

		/* Count padding bytes added through --pad-to. */
		if (s->pad_sz > 0)
			off += s->pad_sz;

		/* Update section header accordingly. */
		if (gelf_getshdr(s->os, &osh) == NULL)
			errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
			    elf_errmsg(-1));
		osh.sh_addr = s->vma;
		osh.sh_offset = s->off;
		osh.sh_size = s->sz;
		if (!gelf_update_shdr(s->os, &osh))
			errx(EXIT_FAILURE, "elf_update_shdr failed: %s",
			    elf_errmsg(-1));

		/* Add padding for previous section, if need. */
		if (ps != NULL) {
			if (ps->pad_sz > 0) {
				/* Apply padding added by --pad-to. */
				pad_section(ecp, ps);
			} else if ((ecp->flags & GAP_FILL) &&
			    (ps->off + ps->sz < s->off)) {
				/*
				 * Fill the gap between sections by padding
				 * the section with lower address.
				 */
				ps->pad_sz = s->off - (ps->off + ps->sz);
				pad_section(ecp, ps);
			}
		}

		ps = s;
	}

	/* Pad the last section, if need. */
	if (ps != NULL && ps->pad_sz > 0)
		pad_section(ecp, ps);
}

static void
modify_section(struct elfcopy *ecp, struct section *s)
{
	struct sec_action	*sac;
	size_t			 srcsz, dstsz, p, len;
	char			*b, *c, *d, *src, *end;
	int			 dupe;

	src = read_section(s, &srcsz);
	if (src == NULL || srcsz == 0) {
		/* For empty section, we proceed if we need to append. */
		if (!is_append_section(ecp, s->name))
			return;
	}

	/* Allocate buffer needed for new section data. */
	dstsz = srcsz;
	if (is_append_section(ecp, s->name)) {
		sac = lookup_sec_act(ecp, s->name, 0);
		dstsz += strlen(sac->string) + 1;
	}
	if ((b = malloc(dstsz)) == NULL)
		err(EXIT_FAILURE, "malloc failed");
	s->buf = b;

	/* Compress section. */
	p = 0;
	if (is_compress_section(ecp, s->name)) {
		end = src + srcsz;
		for(c = src; c < end;) {
			len = 0;
			while(c + len < end && c[len] != '\0')
				len++;
			if (c + len == end) {
				/* XXX should we warn here? */
				strncpy(&b[p], c, len);
				p += len;
				break;
			}
			dupe = 0;
			for (d = b; d < b + p; ) {
				if (strcmp(d, c) == 0) {
					dupe = 1;
					break;
				}
				d += strlen(d) + 1;
			}
			if (!dupe) {
				strncpy(&b[p], c, len);
				b[p + len] = '\0';
				p += len + 1;
			}
			c += len + 1;
		}
	} else {
		memcpy(b, src, srcsz);
		p += srcsz;
	}

	/* Append section. */
	if (is_append_section(ecp, s->name)) {
		sac = lookup_sec_act(ecp, s->name, 0);
		len = strlen(sac->string);
		strncpy(&b[p], sac->string, len);
		b[p + len] = '\0';
		p += len + 1;
	}

	s->sz = p;
	s->nocopy = 1;
}

static void
print_data(const char *d, size_t sz)
{
	const char *c;

	for (c = d; c < d + sz; c++) {
		if (*c == '\0')
			putchar('\n');
		else
			putchar(*c);
	}
}

static void
print_section(struct section *s)
{
	Elf_Data	*id;
	int		 elferr;

	if (s->buf != NULL && s->sz > 0) {
		print_data(s->buf, s->sz);
	} else {
		id = NULL;
		while ((id = elf_getdata(s->is, id)) != NULL ||
		    (id = elf_rawdata(s->is, id)) != NULL) {
			(void) elf_errno();
			print_data(id->d_buf, id->d_size);
		}
		elferr = elf_errno();
		if (elferr != 0)
			errx(EXIT_FAILURE, "elf_getdata() failed: %s",
			    elf_errmsg(elferr));
	}
	putchar('\n');
}

static void *
read_section(struct section *s, size_t *size)
{
	Elf_Data	*id;
	char		*b;
	size_t		 sz;
	int		 elferr;

	sz = 0;
	b = NULL;
	id = NULL;
	while ((id = elf_getdata(s->is, id)) != NULL ||
	    (id = elf_rawdata(s->is, id)) != NULL) {
		(void) elf_errno();
		if (b == NULL)
			b = malloc(id->d_size);
		else
			b = malloc(sz + id->d_size);
		if (b == NULL)
			err(EXIT_FAILURE, "malloc or realloc failed");

		memcpy(&b[sz], id->d_buf, id->d_size);
		sz += id->d_size;
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_getdata() failed: %s",
		    elf_errmsg(elferr));

	*size = sz;

	return (b);
}

void
copy_shdr(struct elfcopy *ecp, struct section *s, const char *name, int copy,
    int sec_flags)
{
	GElf_Shdr ish, osh;

	if (gelf_getshdr(s->is, &ish) == NULL)
		errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));
	if (gelf_getshdr(s->os, &osh) == NULL)
		errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));

	if (copy)
		(void) memcpy(&osh, &ish, sizeof(ish));
	else {
		osh.sh_type		= s->type;
		osh.sh_addr		= s->vma;
		osh.sh_offset		= s->off;
		osh.sh_size		= s->sz;
		osh.sh_link		= ish.sh_link;
		osh.sh_info		= ish.sh_info;
		osh.sh_addralign	= s->align;
		osh.sh_entsize		= ish.sh_entsize;

		if (sec_flags) {
			osh.sh_flags = 0;
			if (sec_flags & SF_ALLOC)
				osh.sh_flags |= SHF_ALLOC;
			if ((sec_flags & SF_READONLY) == 0)
				osh.sh_flags |= SHF_WRITE;
			if (sec_flags & SF_CODE)
				osh.sh_flags |= SHF_EXECINSTR;
			if ((sec_flags & SF_CONTENTS) &&
			    s->type == SHT_NOBITS && s->sz > 0) {
				/*
				 * Convert SHT_NOBITS section to section with
				 * (zero'ed) content on file.
				 */
				osh.sh_type = s->type = SHT_PROGBITS;
				if ((s->buf = calloc(1, s->sz)) == NULL)
					err(EXIT_FAILURE, "malloc failed");
				s->nocopy = 1;
			}
		} else {
			osh.sh_flags = ish.sh_flags;
			/*
			 * Newer binutils as(1) emits the section flag
			 * SHF_INFO_LINK for relocation sections. elfcopy
			 * emits this flag in the output section if it's
			 * missing in the input section, to remain compatible
			 * with binutils.
			 */
			if (ish.sh_type == SHT_REL || ish.sh_type == SHT_RELA)
				osh.sh_flags |= SHF_INFO_LINK;
		}
	}

	if (name == NULL)
		add_to_shstrtab(ecp, s->name);
	else
		add_to_shstrtab(ecp, name);

	if (!gelf_update_shdr(s->os, &osh))
		errx(EXIT_FAILURE, "elf_update_shdr failed: %s",
		    elf_errmsg(-1));
}

void
copy_data(struct section *s)
{
	Elf_Data	*id, *od;
	int		 elferr;

	if (s->nocopy && s->buf == NULL)
		return;

	if ((id = elf_getdata(s->is, NULL)) == NULL) {
		(void) elf_errno();
		if ((id = elf_rawdata(s->is, NULL)) == NULL) {
			elferr = elf_errno();
			if (elferr != 0)
				errx(EXIT_FAILURE, "failed to read section:"
				    " %s", s->name);
			return;
		}
	}

	if ((od = elf_newdata(s->os)) == NULL)
		errx(EXIT_FAILURE, "elf_newdata() failed: %s",
		    elf_errmsg(-1));

	if (s->nocopy) {
		/* Use s->buf as content if s->nocopy is set. */
		od->d_align	= id->d_align;
		od->d_off	= 0;
		od->d_buf	= s->buf;
		od->d_type	= id->d_type;
		od->d_size	= s->sz;
		od->d_version	= id->d_version;
	} else {
		od->d_align	= id->d_align;
		od->d_off	= id->d_off;
		od->d_buf	= id->d_buf;
		od->d_type	= id->d_type;
		od->d_size	= id->d_size;
		od->d_version	= id->d_version;
	}

	/*
	 * Alignment Fixup. libelf does not allow the alignment for
	 * Elf_Data descriptor to be set to 0. In this case we workaround
	 * it by setting the alignment to 1.
	 *
	 * According to the ELF ABI, alignment 0 and 1 has the same
	 * meaning: the section has no alignment constraints.
	 */
	if (od->d_align == 0)
		od->d_align = 1;
}

struct section *
create_external_section(struct elfcopy *ecp, const char *name, char *newname,
    void *buf, uint64_t size, uint64_t off, uint64_t stype, Elf_Type dtype,
    uint64_t flags, uint64_t align, uint64_t vma, int loadable)
{
	struct section	*s;
	Elf_Scn		*os;
	Elf_Data	*od;
	GElf_Shdr	 osh;

	if ((os = elf_newscn(ecp->eout)) == NULL)
		errx(EXIT_FAILURE, "elf_newscn() failed: %s",
		    elf_errmsg(-1));
	if ((s = calloc(1, sizeof(*s))) == NULL)
		err(EXIT_FAILURE, "calloc failed");
	s->name = name;
	s->newname = newname;	/* needs to be free()'ed */
	s->off = off;
	s->sz = size;
	s->vma = vma;
	s->align = align;
	s->loadable = loadable;
	s->is = NULL;
	s->os = os;
	s->type = stype;
	s->nocopy = 1;
	insert_to_sec_list(ecp, s, 1);

	if (gelf_getshdr(os, &osh) == NULL)
		errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));
	osh.sh_flags = flags;
	osh.sh_type = s->type;
	osh.sh_addr = s->vma;
	osh.sh_addralign = s->align;
	if (!gelf_update_shdr(os, &osh))
		errx(EXIT_FAILURE, "gelf_update_shdr() failed: %s",
		    elf_errmsg(-1));
	add_to_shstrtab(ecp, name);

	if (buf != NULL && size != 0) {
		if ((od = elf_newdata(os)) == NULL)
			errx(EXIT_FAILURE, "elf_newdata() failed: %s",
			    elf_errmsg(-1));
		od->d_align = align;
		od->d_off = 0;
		od->d_buf = buf;
		od->d_size = size;
		od->d_type = dtype;
		od->d_version = EV_CURRENT;
	}

	/*
	 * Clear SYMTAB_INTACT, as we probably need to update/add new
	 * STT_SECTION symbols into the symbol table.
	 */
	ecp->flags &= ~SYMTAB_INTACT;

	return (s);
}

/*
 * Insert sections specified by --add-section to the end of section list.
 */
static void
insert_sections(struct elfcopy *ecp)
{
	struct sec_add	*sa;
	struct section	*s;
	size_t		 off;
	uint64_t	 stype;

	/* Put these sections in the end of current list. */
	off = 0;
	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (s->type != SHT_NOBITS && s->type != SHT_NULL)
			off = s->off + s->sz;
		else
			off = s->off;
	}

	STAILQ_FOREACH(sa, &ecp->v_sadd, sadd_list) {

		/* TODO: Add section header vma/lma, flag changes here */

		/*
		 * The default section type for user added section is
		 * SHT_PROGBITS. If the section name match certain patterns,
		 * elfcopy will try to set a more appropriate section type.
		 * However, data type is always set to ELF_T_BYTE and no
		 * translation is performed by libelf.
		 */
		stype = SHT_PROGBITS;
		if (strcmp(sa->name, ".note") == 0 ||
		    strncmp(sa->name, ".note.", strlen(".note.")) == 0)
			stype = SHT_NOTE;

		(void) create_external_section(ecp, sa->name, NULL, sa->content,
		    sa->size, off, stype, ELF_T_BYTE, 0, 1, 0, 0);
	}
}

void
add_to_shstrtab(struct elfcopy *ecp, const char *name)
{
	struct section *s;

	s = ecp->shstrtab;
	insert_to_strtab(s, name);
}

void
update_shdr(struct elfcopy *ecp, int update_link)
{
	struct section	*s;
	GElf_Shdr	 osh;
	int		 elferr;

	TAILQ_FOREACH(s, &ecp->v_sec, sec_list) {
		if (s->pseudo)
			continue;

		if (gelf_getshdr(s->os, &osh) == NULL)
			errx(EXIT_FAILURE, "gelf_getshdr failed: %s",
			    elf_errmsg(-1));

		/* Find section name in string table and set sh_name. */
		osh.sh_name = lookup_string(ecp->shstrtab, s->name);

		/*
		 * sh_link needs to be updated, since the index of the
		 * linked section might have changed.
		 */
		if (update_link && osh.sh_link != 0)
			osh.sh_link = ecp->secndx[osh.sh_link];

		/*
		 * sh_info of relocation section links to the section to which
		 * its relocation info applies. So it may need update as well.
		 */
		if ((s->type == SHT_REL || s->type == SHT_RELA) &&
		    osh.sh_info != 0)
			osh.sh_info = ecp->secndx[osh.sh_info];

		/*
		 * sh_info of SHT_GROUP section needs to point to the correct
		 * string in the symbol table.
		 */
		if (s->type == SHT_GROUP && (ecp->flags & SYMTAB_EXIST) &&
		    (ecp->flags & SYMTAB_INTACT) == 0)
			osh.sh_info = ecp->symndx[osh.sh_info];

		if (!gelf_update_shdr(s->os, &osh))
			errx(EXIT_FAILURE, "gelf_update_shdr() failed: %s",
			    elf_errmsg(-1));
	}
	elferr = elf_errno();
	if (elferr != 0)
		errx(EXIT_FAILURE, "elf_nextscn failed: %s",
		    elf_errmsg(elferr));
}

void
init_shstrtab(struct elfcopy *ecp)
{
	struct section *s;

	if ((ecp->shstrtab = calloc(1, sizeof(*ecp->shstrtab))) == NULL)
		err(EXIT_FAILURE, "calloc failed");
	s = ecp->shstrtab;
	s->name = ".shstrtab";
	s->is = NULL;
	s->sz = 0;
	s->align = 1;
	s->loadable = 0;
	s->type = SHT_STRTAB;
	s->vma = 0;

	insert_to_strtab(s, "");
	insert_to_strtab(s, ".symtab");
	insert_to_strtab(s, ".strtab");
	insert_to_strtab(s, ".shstrtab");
}

void
set_shstrtab(struct elfcopy *ecp)
{
	struct section	*s;
	Elf_Data	*data;
	GElf_Shdr	 sh;

	s = ecp->shstrtab;

	if (s->os == NULL) {
		/* Input object does not contain .shstrtab section */
		if ((s->os = elf_newscn(ecp->eout)) == NULL)
			errx(EXIT_FAILURE, "elf_newscn failed: %s",
			    elf_errmsg(-1));
		insert_to_sec_list(ecp, s, 1);
	}

	if (gelf_getshdr(s->os, &sh) == NULL)
		errx(EXIT_FAILURE, "gelf_getshdr() failed: %s",
		    elf_errmsg(-1));
	sh.sh_addr	= 0;
	sh.sh_addralign	= 1;
	sh.sh_offset	= s->off;
	sh.sh_type	= SHT_STRTAB;
	sh.sh_flags	= 0;
	sh.sh_entsize	= 0;
	sh.sh_info	= 0;
	sh.sh_link	= 0;

	if ((data = elf_newdata(s->os)) == NULL)
		errx(EXIT_FAILURE, "elf_newdata() failed: %s",
		    elf_errmsg(-1));

	/*
	 * If we don't have a symbol table, skip those a few bytes
	 * which are reserved for this in the beginning of shstrtab.
	 */
	if (!(ecp->flags & SYMTAB_EXIST)) {
		s->sz -= sizeof(".symtab\0.strtab");
		memmove(s->buf, (char *)s->buf + sizeof(".symtab\0.strtab"),
		    s->sz);
	}

	sh.sh_size	= s->sz;
	if (!gelf_update_shdr(s->os, &sh))
		errx(EXIT_FAILURE, "gelf_update_shdr() failed: %s",
		    elf_errmsg(-1));

	data->d_align	= 1;
	data->d_buf	= s->buf;
	data->d_size	= s->sz;
	data->d_off	= 0;
	data->d_type	= ELF_T_BYTE;
	data->d_version	= EV_CURRENT;

	if (!elf_setshstrndx(ecp->eout, elf_ndxscn(s->os)))
		errx(EXIT_FAILURE, "elf_setshstrndx() failed: %s",
		     elf_errmsg(-1));
}

void
add_section(struct elfcopy *ecp, const char *arg)
{
	struct sec_add	*sa;
	struct stat	 sb;
	const char	*s, *fn;
	FILE		*fp;
	int		 len;

	if ((s = strchr(arg, '=')) == NULL)
		errx(EXIT_FAILURE,
		    "illegal format for --add-section option");
	if ((sa = malloc(sizeof(*sa))) == NULL)
		err(EXIT_FAILURE, "malloc failed");

	len = s - arg;
	if ((sa->name = malloc(len + 1)) == NULL)
		err(EXIT_FAILURE, "malloc failed");
	strncpy(sa->name, arg, len);
	sa->name[len] = '\0';

	fn = s + 1;
	if (stat(fn, &sb) == -1)
		err(EXIT_FAILURE, "stat failed");
	sa->size = sb.st_size;
	if (sa->size > 0) {
		if ((sa->content = malloc(sa->size)) == NULL)
			err(EXIT_FAILURE, "malloc failed");
		if ((fp = fopen(fn, "r")) == NULL)
			err(EXIT_FAILURE, "can not open %s", fn);
		if (fread(sa->content, 1, sa->size, fp) == 0 ||
		    ferror(fp))
			err(EXIT_FAILURE, "fread failed");
		fclose(fp);
	} else
		sa->content = NULL;

	STAILQ_INSERT_TAIL(&ecp->v_sadd, sa, sadd_list);
	ecp->flags |= SEC_ADD;
}

void
free_sec_add(struct elfcopy *ecp)
{
	struct sec_add *sa, *sa_temp;

	STAILQ_FOREACH_SAFE(sa, &ecp->v_sadd, sadd_list, sa_temp) {
		STAILQ_REMOVE(&ecp->v_sadd, sa, sec_add, sadd_list);
		free(sa->name);
		free(sa->content);
		free(sa);
	}
}

static void
add_gnu_debuglink(struct elfcopy *ecp)
{
	struct sec_add	*sa;
	struct stat	 sb;
	FILE		*fp;
	char		*fnbase, *buf;
	int		 crc_off;
	int		 crc;

	if (ecp->debuglink == NULL)
		return;

	/* Read debug file content. */
	if ((sa = malloc(sizeof(*sa))) == NULL)
		err(EXIT_FAILURE, "malloc failed");
	if ((sa->name = strdup(".gnu_debuglink")) == NULL)
		err(EXIT_FAILURE, "strdup failed");
	if (stat(ecp->debuglink, &sb) == -1)
		err(EXIT_FAILURE, "stat failed");
	if (sb.st_size == 0)
		errx(EXIT_FAILURE, "empty debug link target %s",
		    ecp->debuglink);
	if ((buf = malloc(sb.st_size)) == NULL)
		err(EXIT_FAILURE, "malloc failed");
	if ((fp = fopen(ecp->debuglink, "r")) == NULL)
		err(EXIT_FAILURE, "can not open %s", ecp->debuglink);
	if (fread(buf, 1, sb.st_size, fp) == 0 ||
	    ferror(fp))
		err(EXIT_FAILURE, "fread failed");
	fclose(fp);

	/* Calculate crc checksum.  */
	crc = calc_crc32(buf, sb.st_size, 0xFFFFFFFF);
	free(buf);

	/* Calculate section size and the offset to store crc checksum. */
	if ((fnbase = basename(ecp->debuglink)) == NULL)
		err(EXIT_FAILURE, "basename failed");
	crc_off = roundup(strlen(fnbase) + 1, 4);
	sa->size = crc_off + 4;

	/* Section content. */
	if ((sa->content = calloc(1, sa->size)) == NULL)
		err(EXIT_FAILURE, "malloc failed");
	strncpy(sa->content, fnbase, strlen(fnbase));
	if (ecp->oed == ELFDATA2LSB) {
		sa->content[crc_off] = crc & 0xFF;
		sa->content[crc_off + 1] = (crc >> 8) & 0xFF;
		sa->content[crc_off + 2] = (crc >> 16) & 0xFF;
		sa->content[crc_off + 3] = crc >> 24;
	} else {
		sa->content[crc_off] = crc >> 24;
		sa->content[crc_off + 1] = (crc >> 16) & 0xFF;
		sa->content[crc_off + 2] = (crc >> 8) & 0xFF;
		sa->content[crc_off + 3] = crc & 0xFF;
	}

	STAILQ_INSERT_TAIL(&ecp->v_sadd, sa, sadd_list);
	ecp->flags |= SEC_ADD;
}

static void
insert_to_strtab(struct section *t, const char *s)
{
	const char	*r;
	char		*b, *c;
	size_t		 len, slen;
	int		 append;

	if (t->sz == 0) {
		t->cap = 512;
		if ((t->buf = malloc(t->cap)) == NULL)
			err(EXIT_FAILURE, "malloc failed");
	}

	slen = strlen(s);
	append = 0;
	b = t->buf;
	for (c = b; c < b + t->sz;) {
		len = strlen(c);
		if (!append && len >= slen) {
			r = c + (len - slen);
			if (strcmp(r, s) == 0)
				return;
		} else if (len < slen && len != 0) {
			r = s + (slen - len);
			if (strcmp(c, r) == 0) {
				t->sz -= len + 1;
				memmove(c, c + len + 1, t->sz - (c - b));
				append = 1;
				continue;
			}
		}
		c += len + 1;
	}

	while (t->sz + slen + 1 >= t->cap) {
		t->cap *= 2;
		if ((t->buf = realloc(t->buf, t->cap)) == NULL)
			err(EXIT_FAILURE, "realloc failed");
	}
	b = t->buf;
	strncpy(&b[t->sz], s, slen);
	b[t->sz + slen] = '\0';
	t->sz += slen + 1;
}

static int
lookup_string(struct section *t, const char *s)
{
	const char	*b, *c, *r;
	size_t		 len, slen;

	slen = strlen(s);
	b = t->buf;
	for (c = b; c < b + t->sz;) {
		len = strlen(c);
		if (len >= slen) {
			r = c + (len - slen);
			if (strcmp(r, s) == 0)
				return (r - b);
		}
		c += len + 1;
	}

	return (-1);
}

static uint32_t crctable[256] =
{
	0x00000000L, 0x77073096L, 0xEE0E612CL, 0x990951BAL,
	0x076DC419L, 0x706AF48FL, 0xE963A535L, 0x9E6495A3L,
	0x0EDB8832L, 0x79DCB8A4L, 0xE0D5E91EL, 0x97D2D988L,
	0x09B64C2BL, 0x7EB17CBDL, 0xE7B82D07L, 0x90BF1D91L,
	0x1DB71064L, 0x6AB020F2L, 0xF3B97148L, 0x84BE41DEL,
	0x1ADAD47DL, 0x6DDDE4EBL, 0xF4D4B551L, 0x83D385C7L,
	0x136C9856L, 0x646BA8C0L, 0xFD62F97AL, 0x8A65C9ECL,
	0x14015C4FL, 0x63066CD9L, 0xFA0F3D63L, 0x8D080DF5L,
	0x3B6E20C8L, 0x4C69105EL, 0xD56041E4L, 0xA2677172L,
	0x3C03E4D1L, 0x4B04D447L, 0xD20D85FDL, 0xA50AB56BL,
	0x35B5A8FAL, 0x42B2986CL, 0xDBBBC9D6L, 0xACBCF940L,
	0x32D86CE3L, 0x45DF5C75L, 0xDCD60DCFL, 0xABD13D59L,
	0x26D930ACL, 0x51DE003AL, 0xC8D75180L, 0xBFD06116L,
	0x21B4F4B5L, 0x56B3C423L, 0xCFBA9599L, 0xB8BDA50FL,
	0x2802B89EL, 0x5F058808L, 0xC60CD9B2L, 0xB10BE924L,
	0x2F6F7C87L, 0x58684C11L, 0xC1611DABL, 0xB6662D3DL,
	0x76DC4190L, 0x01DB7106L, 0x98D220BCL, 0xEFD5102AL,
	0x71B18589L, 0x06B6B51FL, 0x9FBFE4A5L, 0xE8B8D433L,
	0x7807C9A2L, 0x0F00F934L, 0x9609A88EL, 0xE10E9818L,
	0x7F6A0DBBL, 0x086D3D2DL, 0x91646C97L, 0xE6635C01L,
	0x6B6B51F4L, 0x1C6C6162L, 0x856530D8L, 0xF262004EL,
	0x6C0695EDL, 0x1B01A57BL, 0x8208F4C1L, 0xF50FC457L,
	0x65B0D9C6L, 0x12B7E950L, 0x8BBEB8EAL, 0xFCB9887CL,
	0x62DD1DDFL, 0x15DA2D49L, 0x8CD37CF3L, 0xFBD44C65L,
	0x4DB26158L, 0x3AB551CEL, 0xA3BC0074L, 0xD4BB30E2L,
	0x4ADFA541L, 0x3DD895D7L, 0xA4D1C46DL, 0xD3D6F4FBL,
	0x4369E96AL, 0x346ED9FCL, 0xAD678846L, 0xDA60B8D0L,
	0x44042D73L, 0x33031DE5L, 0xAA0A4C5FL, 0xDD0D7CC9L,
	0x5005713CL, 0x270241AAL, 0xBE0B1010L, 0xC90C2086L,
	0x5768B525L, 0x206F85B3L, 0xB966D409L, 0xCE61E49FL,
	0x5EDEF90EL, 0x29D9C998L, 0xB0D09822L, 0xC7D7A8B4L,
	0x59B33D17L, 0x2EB40D81L, 0xB7BD5C3BL, 0xC0BA6CADL,
	0xEDB88320L, 0x9ABFB3B6L, 0x03B6E20CL, 0x74B1D29AL,
	0xEAD54739L, 0x9DD277AFL, 0x04DB2615L, 0x73DC1683L,
	0xE3630B12L, 0x94643B84L, 0x0D6D6A3EL, 0x7A6A5AA8L,
	0xE40ECF0BL, 0x9309FF9DL, 0x0A00AE27L, 0x7D079EB1L,
	0xF00F9344L, 0x8708A3D2L, 0x1E01F268L, 0x6906C2FEL,
	0xF762575DL, 0x806567CBL, 0x196C3671L, 0x6E6B06E7L,
	0xFED41B76L, 0x89D32BE0L, 0x10DA7A5AL, 0x67DD4ACCL,
	0xF9B9DF6FL, 0x8EBEEFF9L, 0x17B7BE43L, 0x60B08ED5L,
	0xD6D6A3E8L, 0xA1D1937EL, 0x38D8C2C4L, 0x4FDFF252L,
	0xD1BB67F1L, 0xA6BC5767L, 0x3FB506DDL, 0x48B2364BL,
	0xD80D2BDAL, 0xAF0A1B4CL, 0x36034AF6L, 0x41047A60L,
	0xDF60EFC3L, 0xA867DF55L, 0x316E8EEFL, 0x4669BE79L,
	0xCB61B38CL, 0xBC66831AL, 0x256FD2A0L, 0x5268E236L,
	0xCC0C7795L, 0xBB0B4703L, 0x220216B9L, 0x5505262FL,
	0xC5BA3BBEL, 0xB2BD0B28L, 0x2BB45A92L, 0x5CB36A04L,
	0xC2D7FFA7L, 0xB5D0CF31L, 0x2CD99E8BL, 0x5BDEAE1DL,
	0x9B64C2B0L, 0xEC63F226L, 0x756AA39CL, 0x026D930AL,
	0x9C0906A9L, 0xEB0E363FL, 0x72076785L, 0x05005713L,
	0x95BF4A82L, 0xE2B87A14L, 0x7BB12BAEL, 0x0CB61B38L,
	0x92D28E9BL, 0xE5D5BE0DL, 0x7CDCEFB7L, 0x0BDBDF21L,
	0x86D3D2D4L, 0xF1D4E242L, 0x68DDB3F8L, 0x1FDA836EL,
	0x81BE16CDL, 0xF6B9265BL, 0x6FB077E1L, 0x18B74777L,
	0x88085AE6L, 0xFF0F6A70L, 0x66063BCAL, 0x11010B5CL,
	0x8F659EFFL, 0xF862AE69L, 0x616BFFD3L, 0x166CCF45L,
	0xA00AE278L, 0xD70DD2EEL, 0x4E048354L, 0x3903B3C2L,
	0xA7672661L, 0xD06016F7L, 0x4969474DL, 0x3E6E77DBL,
	0xAED16A4AL, 0xD9D65ADCL, 0x40DF0B66L, 0x37D83BF0L,
	0xA9BCAE53L, 0xDEBB9EC5L, 0x47B2CF7FL, 0x30B5FFE9L,
	0xBDBDF21CL, 0xCABAC28AL, 0x53B39330L, 0x24B4A3A6L,
	0xBAD03605L, 0xCDD70693L, 0x54DE5729L, 0x23D967BFL,
	0xB3667A2EL, 0xC4614AB8L, 0x5D681B02L, 0x2A6F2B94L,
	0xB40BBE37L, 0xC30C8EA1L, 0x5A05DF1BL, 0x2D02EF8DL
};

static uint32_t
calc_crc32(const char *p, size_t len, uint32_t crc)
{
	uint32_t i;

	for (i = 0; i < len; i++) {
		crc = crctable[(crc ^ *p++) & 0xFFL] ^ (crc >> 8);
	}

	return (crc ^ 0xFFFFFFFF);
}
