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

#include <assert.h>
#include <gelf.h>
#include <libelf.h>
#include <stdlib.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_ehdr.c 3632 2018-10-10 21:12:43Z jkoshy $");

/*
 * Retrieve counts for sections, phdrs and the section string table index
 * from section header #0 of the ELF object.
 */
static int
_libelf_load_extended(Elf *e, int ec, uint64_t shoff, uint16_t phnum,
    uint16_t strndx)
{
	size_t fsz;
	Elf_Scn *scn;
	uint32_t shtype;
	_libelf_translator_function *xlator;

	assert(STAILQ_EMPTY(&e->e_u.e_elf.e_scn));

	fsz = _libelf_fsize(ELF_T_SHDR, ec, e->e_version, 1);
	assert(fsz > 0);

	if (e->e_rawsize < shoff + fsz) { /* raw file too small */
		LIBELF_SET_ERROR(HEADER, 0);
		return (0);
	}

	if ((scn = _libelf_allocate_scn(e, (size_t) 0)) == NULL)
		return (0);

	xlator = _libelf_get_translator(ELF_T_SHDR, ELF_TOMEMORY, ec,
	    _libelf_elfmachine(e));
	(*xlator)((unsigned char *) &scn->s_shdr, sizeof(scn->s_shdr),
	    (unsigned char *) e->e_rawfile + shoff, (size_t) 1,
	    e->e_byteorder != LIBELF_PRIVATE(byteorder));

#define	GET_SHDR_MEMBER(M) ((ec == ELFCLASS32) ? scn->s_shdr.s_shdr32.M : \
		scn->s_shdr.s_shdr64.M)

	if ((shtype = GET_SHDR_MEMBER(sh_type)) != SHT_NULL) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (0);
	}

	e->e_u.e_elf.e_nscn = (size_t) GET_SHDR_MEMBER(sh_size);
	e->e_u.e_elf.e_nphdr = (phnum != PN_XNUM) ? phnum :
	    GET_SHDR_MEMBER(sh_info);
	e->e_u.e_elf.e_strndx = (strndx != SHN_XINDEX) ? strndx :
	    GET_SHDR_MEMBER(sh_link);
#undef	GET_SHDR_MEMBER

	return (1);
}

#define	EHDR_INIT(E,SZ)	 do {						\
		Elf##SZ##_Ehdr *eh = (E);				\
		eh->e_ident[EI_MAG0] = ELFMAG0;				\
		eh->e_ident[EI_MAG1] = ELFMAG1;				\
		eh->e_ident[EI_MAG2] = ELFMAG2;				\
		eh->e_ident[EI_MAG3] = ELFMAG3;				\
		eh->e_ident[EI_CLASS] = ELFCLASS##SZ;			\
		eh->e_ident[EI_DATA]  = ELFDATANONE;			\
		eh->e_ident[EI_VERSION] = LIBELF_PRIVATE(version) & 0xFFU; \
		eh->e_machine = EM_NONE;				\
		eh->e_type    = ELF_K_NONE;				\
		eh->e_version = LIBELF_PRIVATE(version);		\
	} while (0)

void *
_libelf_ehdr(Elf *e, int ec, int allocate)
{
	void *ehdr;
	size_t fsz, msz;
	uint16_t phnum, shnum, strndx;
	uint64_t shoff;
	int (*xlator)(unsigned char *_d, size_t _dsz, unsigned char *_s,
	    size_t _c, int _swap);

	assert(ec == ELFCLASS32 || ec == ELFCLASS64);

	if (e == NULL || e->e_kind != ELF_K_ELF) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if (e->e_class != ELFCLASSNONE && e->e_class != ec) {
		LIBELF_SET_ERROR(CLASS, 0);
		return (NULL);
	}

	if (e->e_version != EV_CURRENT) {
		LIBELF_SET_ERROR(VERSION, 0);
		return (NULL);
	}

	if (e->e_class == ELFCLASSNONE)
		e->e_class = ec;

	if (ec == ELFCLASS32)
		ehdr = (void *) e->e_u.e_elf.e_ehdr.e_ehdr32;
	else
		ehdr = (void *) e->e_u.e_elf.e_ehdr.e_ehdr64;

	if (ehdr != NULL)	/* already have a translated ehdr */
		return (ehdr);

	fsz = _libelf_fsize(ELF_T_EHDR, ec, e->e_version, (size_t) 1);
	assert(fsz > 0);

	if (e->e_cmd != ELF_C_WRITE && e->e_rawsize < fsz) {
		LIBELF_SET_ERROR(HEADER, 0);
		return (NULL);
	}

	msz = _libelf_msize(ELF_T_EHDR, ec, EV_CURRENT);

	assert(msz > 0);

	if ((ehdr = calloc((size_t) 1, msz)) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	if (ec == ELFCLASS32) {
		e->e_u.e_elf.e_ehdr.e_ehdr32 = ehdr;
		EHDR_INIT(ehdr,32);
	} else {
		e->e_u.e_elf.e_ehdr.e_ehdr64 = ehdr;
		EHDR_INIT(ehdr,64);
	}

	if (allocate)
		e->e_flags |= ELF_F_DIRTY;

	if (e->e_cmd == ELF_C_WRITE)
		return (ehdr);

	xlator = _libelf_get_translator(ELF_T_EHDR, ELF_TOMEMORY, ec,
	    _libelf_elfmachine(e));
	(*xlator)((unsigned char*) ehdr, msz, e->e_rawfile, (size_t) 1,
	    e->e_byteorder != LIBELF_PRIVATE(byteorder));

	if (ec == ELFCLASS32) {
		phnum = ((Elf32_Ehdr *) ehdr)->e_phnum;
		shnum = ((Elf32_Ehdr *) ehdr)->e_shnum;
		shoff = ((Elf32_Ehdr *) ehdr)->e_shoff;
		strndx = ((Elf32_Ehdr *) ehdr)->e_shstrndx;
	} else {
		phnum = ((Elf64_Ehdr *) ehdr)->e_phnum;
		shnum = ((Elf64_Ehdr *) ehdr)->e_shnum;
		shoff = ((Elf64_Ehdr *) ehdr)->e_shoff;
		strndx = ((Elf64_Ehdr *) ehdr)->e_shstrndx;
	}

	if (shnum >= SHN_LORESERVE ||
	    (shoff == 0LL && (shnum != 0 || phnum == PN_XNUM ||
		strndx == SHN_XINDEX))) {
		LIBELF_SET_ERROR(HEADER, 0);
		return (NULL);
	}

	/*
	 * If extended numbering is being used, read the correct
	 * number of sections and program header entries.
	 */
	if ((shnum == 0 && shoff != 0) || phnum == PN_XNUM || strndx == SHN_XINDEX) {
		if (_libelf_load_extended(e, ec, shoff, phnum, strndx) == 0)
			return (NULL);
	} else {
		/* not using extended numbering */
		e->e_u.e_elf.e_nphdr = phnum;
		e->e_u.e_elf.e_nscn = shnum;
		e->e_u.e_elf.e_strndx = strndx;
	}

	return (ehdr);
}
