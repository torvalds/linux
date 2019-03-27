/*-
 * Copyright (c) 2010,2011 Kai Wang
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
#include <ctype.h>
#include <err.h>
#include <gelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elfcopy.h"

ELFTC_VCSID("$Id: binary.c 3611 2018-04-16 21:35:18Z jkoshy $");

/*
 * Convert ELF object to `binary'. Sections with SHF_ALLOC flag set
 * are copied to the result binary. The relative offsets for each section
 * are retained, so the result binary file might contain "holes".
 */
void
create_binary(int ifd, int ofd)
{
	Elf *e;
	Elf_Scn *scn;
	Elf_Data *d;
	GElf_Shdr sh;
	off_t base, off;
	int elferr;

	if ((e = elf_begin(ifd, ELF_C_READ, NULL)) == NULL)
		errx(EXIT_FAILURE, "elf_begin() failed: %s",
		    elf_errmsg(-1));

	base = 0;
	if (lseek(ofd, base, SEEK_SET) < 0)
		err(EXIT_FAILURE, "lseek failed");

	/*
	 * Find base offset in the first iteration.
	 */
	base = -1;
	scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &sh) == NULL) {
			warnx("gelf_getshdr failed: %s", elf_errmsg(-1));
			(void) elf_errno();
			continue;
		}
		if ((sh.sh_flags & SHF_ALLOC) == 0 ||
		    sh.sh_type == SHT_NOBITS ||
		    sh.sh_size == 0)
			continue;
		if (base == -1 || (off_t) sh.sh_offset < base)
			base = sh.sh_offset;
	}
	elferr = elf_errno();
	if (elferr != 0)
		warnx("elf_nextscn failed: %s", elf_errmsg(elferr));

	if (base == -1)
		return;

	/*
	 * Write out sections in the second iteration.
	 */
	scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &sh) == NULL) {
			warnx("gelf_getshdr failed: %s", elf_errmsg(-1));
			(void) elf_errno();
			continue;
		}
		if ((sh.sh_flags & SHF_ALLOC) == 0 ||
		    sh.sh_type == SHT_NOBITS ||
		    sh.sh_size == 0)
			continue;
		(void) elf_errno();
		if ((d = elf_rawdata(scn, NULL)) == NULL) {
			elferr = elf_errno();
			if (elferr != 0)
				warnx("elf_rawdata failed: %s", elf_errmsg(-1));
			continue;
		}
		if (d->d_buf == NULL || d->d_size == 0)
			continue;

		/* lseek to section offset relative to `base'. */
		off = sh.sh_offset - base;
		if (lseek(ofd, off, SEEK_SET) < 0)
			err(EXIT_FAILURE, "lseek failed");

		/* Write out section contents. */
		if (write(ofd, d->d_buf, d->d_size) != (ssize_t) d->d_size)
			err(EXIT_FAILURE, "write failed");
	}
	elferr = elf_errno();
	if (elferr != 0)
		warnx("elf_nextscn failed: %s", elf_errmsg(elferr));
}

#define	_SYMBOL_NAMSZ	1024

/*
 * Convert `binary' to ELF object. The input `binary' is converted to
 * a relocatable (.o) file, a few symbols will also be created to make
 * it easier to access the binary data in other compilation units.
 */
void
create_elf_from_binary(struct elfcopy *ecp, int ifd, const char *ifn)
{
	char name[_SYMBOL_NAMSZ];
	struct section *sec, *sec_temp, *shtab;
	struct stat sb;
	GElf_Ehdr oeh;
	GElf_Shdr sh;
	void *content;
	uint64_t off, data_start, data_end, data_size;
	char *sym_basename, *p;

	/* Reset internal section list. */
	if (!TAILQ_EMPTY(&ecp->v_sec))
		TAILQ_FOREACH_SAFE(sec, &ecp->v_sec, sec_list, sec_temp) {
			TAILQ_REMOVE(&ecp->v_sec, sec, sec_list);
			free(sec);
		}

	if (fstat(ifd, &sb) == -1)
		err(EXIT_FAILURE, "fstat failed");

	/* Read the input binary file to a internal buffer. */
	if ((content = malloc(sb.st_size)) == NULL)
		err(EXIT_FAILURE, "malloc failed");
	if (read(ifd, content, sb.st_size) != sb.st_size)
		err(EXIT_FAILURE, "read failed");

	/*
	 * TODO: copy the input binary to output binary verbatim if -O is not
	 * specified.
	 */

	/* Create EHDR for output .o file. */
	if (gelf_newehdr(ecp->eout, ecp->oec) == NULL)
		errx(EXIT_FAILURE, "gelf_newehdr failed: %s",
		    elf_errmsg(-1));
	if (gelf_getehdr(ecp->eout, &oeh) == NULL)
		errx(EXIT_FAILURE, "gelf_getehdr() failed: %s",
		    elf_errmsg(-1));

	/* Initialise e_ident fields. */
	oeh.e_ident[EI_CLASS] = ecp->oec;
	oeh.e_ident[EI_DATA] = ecp->oed;
	/*
	 * TODO: Set OSABI according to the OS platform where elfcopy(1)
	 * was build. (probably)
	 */
	oeh.e_ident[EI_OSABI] = ELFOSABI_NONE;
	oeh.e_machine = ecp->oem;
	oeh.e_type = ET_REL;
	oeh.e_entry = 0;

	ecp->flags |= RELOCATABLE;

	/* Create .shstrtab section */
	init_shstrtab(ecp);
	ecp->shstrtab->off = 0;

	/*
	 * Create `.data' section which contains the binary data. The
	 * section is inserted immediately after EHDR.
	 */
	off = gelf_fsize(ecp->eout, ELF_T_EHDR, 1, EV_CURRENT);
	if (off == 0)
		errx(EXIT_FAILURE, "gelf_fsize() failed: %s", elf_errmsg(-1));
	(void) create_external_section(ecp, ".data", NULL, content, sb.st_size,
	    off, SHT_PROGBITS, ELF_T_BYTE, SHF_ALLOC | SHF_WRITE, 1, 0, 1);

	/* Insert .shstrtab after .data section. */
	if ((ecp->shstrtab->os = elf_newscn(ecp->eout)) == NULL)
		errx(EXIT_FAILURE, "elf_newscn failed: %s",
		    elf_errmsg(-1));
	insert_to_sec_list(ecp, ecp->shstrtab, 1);

	/* Insert section header table here. */
	shtab = insert_shtab(ecp, 1);

	/* Count in .symtab and .strtab section headers.  */
	shtab->sz += gelf_fsize(ecp->eout, ELF_T_SHDR, 2, EV_CURRENT);

	if ((sym_basename = strdup(ifn)) == NULL)
		err(1, "strdup");
	for (p = sym_basename; *p != '\0'; p++)
		if (!isalnum(*p & 0xFF))
			*p = '_';
#define	_GEN_SYMNAME(S) do {						\
	snprintf(name, sizeof(name), "%s%s%s", "_binary_", sym_basename, S); \
} while (0)

	/*
	 * Create symbol table.
	 */
	create_external_symtab(ecp);
	data_start = 0;
	data_end = data_start + sb.st_size;
	data_size = sb.st_size;
	_GEN_SYMNAME("_start");
	add_to_symtab(ecp, name, data_start, 0, 1,
	    ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, 1);
	_GEN_SYMNAME("_end");
	add_to_symtab(ecp, name, data_end, 0, 1,
	    ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, 1);
	_GEN_SYMNAME("_size");
	add_to_symtab(ecp, name, data_size, 0, SHN_ABS,
	    ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, 1);
	finalize_external_symtab(ecp);
	create_symtab_data(ecp);
#undef	_GEN_SYMNAME
	free(sym_basename);

	/*
	 * Write the underlying ehdr. Note that it should be called
	 * before elf_setshstrndx() since it will overwrite e->e_shstrndx.
	 */
	if (gelf_update_ehdr(ecp->eout, &oeh) == 0)
		errx(EXIT_FAILURE, "gelf_update_ehdr() failed: %s",
		    elf_errmsg(-1));

	/* Generate section name string table (.shstrtab). */
	ecp->flags |= SYMTAB_EXIST;
	set_shstrtab(ecp);

	/* Update sh_name pointer for each section header entry. */
	update_shdr(ecp, 0);

	/* Properly set sh_link field of .symtab section. */
	if (gelf_getshdr(ecp->symtab->os, &sh) == NULL)
		errx(EXIT_FAILURE, "692 gelf_getshdr() failed: %s",
		    elf_errmsg(-1));
	sh.sh_link = elf_ndxscn(ecp->strtab->os);
	if (!gelf_update_shdr(ecp->symtab->os, &sh))
		errx(EXIT_FAILURE, "gelf_update_shdr() failed: %s",
		    elf_errmsg(-1));

	/* Renew oeh to get the updated e_shstrndx. */
	if (gelf_getehdr(ecp->eout, &oeh) == NULL)
		errx(EXIT_FAILURE, "gelf_getehdr() failed: %s",
		    elf_errmsg(-1));

	/* Resync section offsets. */
	resync_sections(ecp);

	/* Store SHDR offset in EHDR. */
	oeh.e_shoff = shtab->off;

	/* Update ehdr since we modified e_shoff. */
	if (gelf_update_ehdr(ecp->eout, &oeh) == 0)
		errx(EXIT_FAILURE, "gelf_update_ehdr() failed: %s",
		    elf_errmsg(-1));

	/* Write out the output elf object. */
	if (elf_update(ecp->eout, ELF_C_WRITE) < 0)
		errx(EXIT_FAILURE, "elf_update() failed: %s",
		    elf_errmsg(-1));

	/* Release allocated resource. */
	free(content);
	free_elf(ecp);
}
