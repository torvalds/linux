/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for retrieving CTF data from a .SUNW_ctf ELF section
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <gelf.h>
#include <strings.h>
#include <sys/types.h>

#include "ctftools.h"
#include "memory.h"
#include "symbol.h"

typedef int read_cb_f(tdata_t *, char *, void *);

/*
 * Return the source types that the object was generated from.
 */
source_types_t
built_source_types(Elf *elf, char const *file)
{
	source_types_t types = SOURCE_NONE;
	symit_data_t *si;

	if ((si = symit_new(elf, file)) == NULL)
		return (SOURCE_NONE);

	while (symit_next(si, STT_FILE) != NULL) {
		char *name = symit_name(si);
		size_t len = strlen(name);
		if (len < 2 || name[len - 2] != '.') {
			types |= SOURCE_UNKNOWN;
			continue;
		}

		switch (name[len - 1]) {
		case 'c':
			types |= SOURCE_C;
			break;
		case 'h':
			/* ignore */
			break;
		case 's':
		case 'S':
			types |= SOURCE_S;
			break;
		default:
			types |= SOURCE_UNKNOWN;
		}
	}

	symit_free(si);
	return (types);
}

static int
read_file(Elf *elf, char *file, char *label, read_cb_f *func, void *arg,
    int require_ctf)
{
	Elf_Scn *ctfscn;
	Elf_Data *ctfdata = NULL;
	symit_data_t *si = NULL;
	int ctfscnidx;
	tdata_t *td;

	if ((ctfscnidx = findelfsecidx(elf, file, ".SUNW_ctf")) < 0) {
		if (require_ctf &&
		    (built_source_types(elf, file) & SOURCE_C)) {
			terminate("Input file %s was partially built from "
			    "C sources, but no CTF data was present\n", file);
		}
		return (0);
	}

	if ((ctfscn = elf_getscn(elf, ctfscnidx)) == NULL ||
	    (ctfdata = elf_getdata(ctfscn, NULL)) == NULL)
		elfterminate(file, "Cannot read CTF section");

	/* Reconstruction of type tree */
	if ((si = symit_new(elf, file)) == NULL) {
		warning("%s has no symbol table - skipping", file);
		return (0);
	}

	td = ctf_load(file, ctfdata->d_buf, ctfdata->d_size, si, label);
	tdata_build_hashes(td);

	symit_free(si);

	if (td != NULL) {
		if (func(td, file, arg) < 0)
			return (-1);
		else
			return (1);
	}
	return (0);
}

static int
read_archive(int fd, Elf *elf, char *file, char *label, read_cb_f *func,
    void *arg, int require_ctf)
{
	Elf *melf;
	Elf_Cmd cmd = ELF_C_READ;
	Elf_Arhdr *arh;
	int secnum = 1, found = 0;

	while ((melf = elf_begin(fd, cmd, elf)) != NULL) {
		int rc = 0;

		if ((arh = elf_getarhdr(melf)) == NULL) {
			elfterminate(file, "Can't get archive header for "
			    "member %d", secnum);
		}

		/* skip special sections - their names begin with "/" */
		if (*arh->ar_name != '/') {
			size_t memlen = strlen(file) + 1 +
			    strlen(arh->ar_name) + 1 + 1;
			char *memname = xmalloc(memlen);

			snprintf(memname, memlen, "%s(%s)", file, arh->ar_name);

			switch (elf_kind(melf)) {
			case ELF_K_AR:
				rc = read_archive(fd, melf, memname, label,
				    func, arg, require_ctf);
				break;
			case ELF_K_ELF:
				rc = read_file(melf, memname, label,
				    func, arg, require_ctf);
				break;
			default:
				terminate("%s: Unknown elf kind %d\n",
				    memname, elf_kind(melf));
			}

			free(memname);
		}

		cmd = elf_next(melf);
		(void) elf_end(melf);
		secnum++;

		if (rc < 0)
			return (rc);
		else
			found += rc;
	}

	return (found);
}

static int
read_ctf_common(char *file, char *label, read_cb_f *func, void *arg,
    int require_ctf)
{
	Elf *elf;
	int found = 0;
	int fd;

	debug(3, "Reading %s (label %s)\n", file, (label ? label : "NONE"));

	(void) elf_version(EV_CURRENT);

	if ((fd = open(file, O_RDONLY)) < 0)
		terminate("%s: Cannot open for reading", file);
	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL)
		elfterminate(file, "Cannot read");

	switch (elf_kind(elf)) {
	case ELF_K_AR:
		found = read_archive(fd, elf, file, label,
		    func, arg, require_ctf);
		break;

	case ELF_K_ELF:
		found = read_file(elf, file, label,
		    func, arg, require_ctf);
		break;

	default:
		terminate("%s: Unknown elf kind %d\n", file, elf_kind(elf));
	}

	(void) elf_end(elf);
	(void) close(fd);

	return (found);
}

/*ARGSUSED*/
int
read_ctf_save_cb(tdata_t *td, char *name __unused, void *retp)
{
	tdata_t **tdp = retp;

	*tdp = td;

	return (1);
}

int
read_ctf(char **files, int n, char *label, read_cb_f *func, void *private,
    int require_ctf)
{
	int found;
	int i, rc;

	for (i = 0, found = 0; i < n; i++) {
		if ((rc = read_ctf_common(files[i], label, func,
		    private, require_ctf)) < 0)
			return (rc);
		found += rc;
	}

	return (found);
}

static int
count_archive(int fd, Elf *elf, char *file)
{
	Elf *melf;
	Elf_Cmd cmd = ELF_C_READ;
	Elf_Arhdr *arh;
	int nfiles = 0, err = 0;

	while ((melf = elf_begin(fd, cmd, elf)) != NULL) {
		if ((arh = elf_getarhdr(melf)) == NULL) {
			warning("Can't process input archive %s\n",
			    file);
			err++;
		}

		if (*arh->ar_name != '/')
			nfiles++;

		cmd = elf_next(melf);
		(void) elf_end(melf);
	}

	if (err > 0)
		return (-1);

	return (nfiles);
}

int
count_files(char **files, int n)
{
	int nfiles = 0, err = 0;
	Elf *elf;
	int fd, rc, i;

	(void) elf_version(EV_CURRENT);

	for (i = 0; i < n; i++) {
		char *file = files[i];

		if ((fd = open(file, O_RDONLY)) < 0) {
			warning("Can't read input file %s", file);
			err++;
			continue;
		}

		if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
			warning("Can't open input file %s: %s\n", file,
			    elf_errmsg(-1));
			err++;
			(void) close(fd);
			continue;
		}

		switch (elf_kind(elf)) {
		case ELF_K_AR:
			if ((rc = count_archive(fd, elf, file)) < 0)
				err++;
			else
				nfiles += rc;
			break;
		case ELF_K_ELF:
			nfiles++;
			break;
		default:
			warning("Input file %s is corrupt\n", file);
			err++;
		}

		(void) elf_end(elf);
		(void) close(fd);
	}

	if (err > 0)
		return (-1);

	debug(2, "Found %d files in %d input files\n", nfiles, n);

	return (nfiles);
}

struct symit_data {
	GElf_Shdr si_shdr;
	Elf_Data *si_symd;
	Elf_Data *si_strd;
	GElf_Sym si_cursym;
	char *si_curname;
	char *si_curfile;
	int si_nument;
	int si_next;
};

symit_data_t *
symit_new(Elf *elf, const char *file)
{
	symit_data_t *si;
	Elf_Scn *scn;
	int symtabidx;

	if ((symtabidx = findelfsecidx(elf, file, ".symtab")) < 0)
		return (NULL);

	si = xcalloc(sizeof (symit_data_t));

	if ((scn = elf_getscn(elf, symtabidx)) == NULL ||
	    gelf_getshdr(scn, &si->si_shdr) == NULL ||
	    (si->si_symd = elf_getdata(scn, NULL)) == NULL)
		elfterminate(file, "Cannot read .symtab");

	if ((scn = elf_getscn(elf, si->si_shdr.sh_link)) == NULL ||
	    (si->si_strd = elf_getdata(scn, NULL)) == NULL)
		elfterminate(file, "Cannot read strings for .symtab");

	si->si_nument = si->si_shdr.sh_size / si->si_shdr.sh_entsize;

	return (si);
}

void
symit_free(symit_data_t *si)
{
	free(si);
}

void
symit_reset(symit_data_t *si)
{
	si->si_next = 0;
}

char *
symit_curfile(symit_data_t *si)
{
	return (si->si_curfile);
}

GElf_Sym *
symit_next(symit_data_t *si, int type)
{
	GElf_Sym sym;
	char *bname;
	int check_sym = (type == STT_OBJECT || type == STT_FUNC);

	for (; si->si_next < si->si_nument; si->si_next++) {
		gelf_getsym(si->si_symd, si->si_next, &si->si_cursym);
		gelf_getsym(si->si_symd, si->si_next, &sym);
		si->si_curname = (caddr_t)si->si_strd->d_buf + sym.st_name;

		if (GELF_ST_TYPE(sym.st_info) == STT_FILE) {
			bname = strrchr(si->si_curname, '/');
			si->si_curfile = bname == NULL ? si->si_curname : bname + 1;
		}

		if (GELF_ST_TYPE(sym.st_info) != type ||
		    sym.st_shndx == SHN_UNDEF)
			continue;

		if (check_sym && ignore_symbol(&sym, si->si_curname))
			continue;

		si->si_next++;

		return (&si->si_cursym);
	}

	return (NULL);
}

char *
symit_name(symit_data_t *si)
{
	return (si->si_curname);
}
