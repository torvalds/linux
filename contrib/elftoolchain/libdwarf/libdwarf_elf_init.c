/*-
 * Copyright (c) 2009 Kai Wang
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

#include "_libdwarf.h"

ELFTC_VCSID("$Id: libdwarf_elf_init.c 3475 2016-05-18 18:11:26Z emaste $");

static const char *debug_name[] = {
	".debug_abbrev",
	".debug_aranges",
	".debug_frame",
	".debug_info",
	".debug_types",
	".debug_line",
	".debug_pubnames",
	".eh_frame",
	".debug_macinfo",
	".debug_str",
	".debug_loc",
	".debug_pubtypes",
	".debug_ranges",
	".debug_static_func",
	".debug_static_vars",
	".debug_typenames",
	".debug_weaknames",
	NULL
};

static void
_dwarf_elf_apply_rel_reloc(Dwarf_Debug dbg, void *buf, uint64_t bufsize,
    Elf_Data *rel_data, Elf_Data *symtab_data, int endian)
{
	Dwarf_Unsigned type;
	GElf_Rel rel;
	GElf_Sym sym;
	size_t symndx;
	uint64_t offset;
	uint64_t addend;
	int size, j;

	j = 0;
	while (gelf_getrel(rel_data, j++, &rel) != NULL) {
		symndx = GELF_R_SYM(rel.r_info);
		type = GELF_R_TYPE(rel.r_info);

		if (gelf_getsym(symtab_data, symndx, &sym) == NULL)
			continue;

		size = _dwarf_get_reloc_size(dbg, type);
		if (size == 0)
			continue; /* Unknown or non-absolute relocation. */

		offset = rel.r_offset;
		if (offset + size >= bufsize)
			continue;

		if (endian == ELFDATA2MSB)
			addend = _dwarf_read_msb(buf, &offset, size);
		else
			addend = _dwarf_read_lsb(buf, &offset, size);

		offset = rel.r_offset;
		if (endian == ELFDATA2MSB)
			_dwarf_write_msb(buf, &offset, sym.st_value + addend,
			    size);
		else
			_dwarf_write_lsb(buf, &offset, sym.st_value + addend,
			    size);
	}
}

static void
_dwarf_elf_apply_rela_reloc(Dwarf_Debug dbg, void *buf, uint64_t bufsize,
    Elf_Data *rel_data, Elf_Data *symtab_data, int endian)
{
	Dwarf_Unsigned type;
	GElf_Rela rela;
	GElf_Sym sym;
	size_t symndx;
	uint64_t offset;
	int size, j;

	j = 0;
	while (gelf_getrela(rel_data, j++, &rela) != NULL) {
		symndx = GELF_R_SYM(rela.r_info);
		type = GELF_R_TYPE(rela.r_info);

		if (gelf_getsym(symtab_data, symndx, &sym) == NULL)
			continue;

		offset = rela.r_offset;
		size = _dwarf_get_reloc_size(dbg, type);
		if (size == 0)
			continue; /* Unknown or non-absolute relocation. */
		if (offset + size >= bufsize)
			continue;

		if (endian == ELFDATA2MSB)
			_dwarf_write_msb(buf, &offset,
			    sym.st_value + rela.r_addend, size);
		else
			_dwarf_write_lsb(buf, &offset,
			    sym.st_value + rela.r_addend, size);
	}
}

static int
_dwarf_elf_relocate(Dwarf_Debug dbg, Elf *elf, Dwarf_Elf_Data *ed, size_t shndx,
    size_t symtab, Elf_Data *symtab_data, Dwarf_Error *error)
{
	GElf_Ehdr eh;
	GElf_Shdr sh;
	Elf_Scn *scn;
	Elf_Data *rel;
	int elferr;

	if (symtab == 0 || symtab_data == NULL)
		return (DW_DLE_NONE);

	if (gelf_getehdr(elf, &eh) == NULL) {
		DWARF_SET_ELF_ERROR(dbg, error);
		return (DW_DLE_ELF);
	}

	scn = NULL;
	(void) elf_errno();
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		if (gelf_getshdr(scn, &sh) == NULL) {
			DWARF_SET_ELF_ERROR(dbg, error);
			return (DW_DLE_ELF);
		}

		if ((sh.sh_type != SHT_REL && sh.sh_type != SHT_RELA) ||
		     sh.sh_size == 0)
			continue;

		if (sh.sh_info == shndx && sh.sh_link == symtab) {
			if ((rel = elf_getdata(scn, NULL)) == NULL) {
				elferr = elf_errno();
				if (elferr != 0) {
					_DWARF_SET_ERROR(NULL, error,
					    DW_DLE_ELF, elferr);
					return (DW_DLE_ELF);
				} else
					return (DW_DLE_NONE);
			}

			ed->ed_alloc = malloc(ed->ed_data->d_size);
			if (ed->ed_alloc == NULL) {
				DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
				return (DW_DLE_MEMORY);
			}
			memcpy(ed->ed_alloc, ed->ed_data->d_buf,
			    ed->ed_data->d_size);
			if (sh.sh_type == SHT_REL)
				_dwarf_elf_apply_rel_reloc(dbg,
				    ed->ed_alloc, ed->ed_data->d_size,
				    rel, symtab_data, eh.e_ident[EI_DATA]);
			else
				_dwarf_elf_apply_rela_reloc(dbg,
				    ed->ed_alloc, ed->ed_data->d_size,
				    rel, symtab_data, eh.e_ident[EI_DATA]);

			return (DW_DLE_NONE);
		}
	}
	elferr = elf_errno();
	if (elferr != 0) {
		DWARF_SET_ELF_ERROR(dbg, error);
		return (DW_DLE_ELF);
	}

	return (DW_DLE_NONE);
}

int
_dwarf_elf_init(Dwarf_Debug dbg, Elf *elf, Dwarf_Error *error)
{
	Dwarf_Obj_Access_Interface *iface;
	Dwarf_Elf_Object *e;
	const char *name;
	GElf_Shdr sh;
	Elf_Scn *scn;
	Elf_Data *symtab_data;
	size_t symtab_ndx;
	int elferr, i, j, n, ret;

	ret = DW_DLE_NONE;

	if ((iface = calloc(1, sizeof(*iface))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	if ((e = calloc(1, sizeof(*e))) == NULL) {
		free(iface);
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	e->eo_elf = elf;
	e->eo_methods.get_section_info = _dwarf_elf_get_section_info;
	e->eo_methods.get_byte_order = _dwarf_elf_get_byte_order;
	e->eo_methods.get_length_size = _dwarf_elf_get_length_size;
	e->eo_methods.get_pointer_size = _dwarf_elf_get_pointer_size;
	e->eo_methods.get_section_count = _dwarf_elf_get_section_count;
	e->eo_methods.load_section = _dwarf_elf_load_section;

	iface->object = e;
	iface->methods = &e->eo_methods;

	dbg->dbg_iface = iface;

	if (gelf_getehdr(elf, &e->eo_ehdr) == NULL) {
		DWARF_SET_ELF_ERROR(dbg, error);
		ret = DW_DLE_ELF;
		goto fail_cleanup;
	}

	dbg->dbg_machine = e->eo_ehdr.e_machine;

	if (!elf_getshstrndx(elf, &e->eo_strndx)) {
		DWARF_SET_ELF_ERROR(dbg, error);
		ret = DW_DLE_ELF;
		goto fail_cleanup;
	}

	n = 0;
	symtab_ndx = 0;
	symtab_data = NULL;
	scn = NULL;
	(void) elf_errno();
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		if (gelf_getshdr(scn, &sh) == NULL) {
			DWARF_SET_ELF_ERROR(dbg, error);
			ret = DW_DLE_ELF;
			goto fail_cleanup;
		}

		if ((name = elf_strptr(elf, e->eo_strndx, sh.sh_name)) ==
		    NULL) {
			DWARF_SET_ELF_ERROR(dbg, error);
			ret = DW_DLE_ELF;
			goto fail_cleanup;
		}

		if (!strcmp(name, ".symtab")) {
			symtab_ndx = elf_ndxscn(scn);
			if ((symtab_data = elf_getdata(scn, NULL)) == NULL) {
				elferr = elf_errno();
				if (elferr != 0) {
					_DWARF_SET_ERROR(NULL, error,
					    DW_DLE_ELF, elferr);
					ret = DW_DLE_ELF;
					goto fail_cleanup;
				}
			}
			continue;
		}

		for (i = 0; debug_name[i] != NULL; i++) {
			if (!strcmp(name, debug_name[i]))
				n++;
		}
	}
	elferr = elf_errno();
	if (elferr != 0) {
		DWARF_SET_ELF_ERROR(dbg, error);
		return (DW_DLE_ELF);
	}

	e->eo_seccnt = n;

	if (n == 0)
		return (DW_DLE_NONE);

	if ((e->eo_data = calloc(n, sizeof(Dwarf_Elf_Data))) == NULL ||
	    (e->eo_shdr = calloc(n, sizeof(GElf_Shdr))) == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_MEMORY);
		ret = DW_DLE_MEMORY;
		goto fail_cleanup;
	}

	scn = NULL;
	j = 0;
	while ((scn = elf_nextscn(elf, scn)) != NULL && j < n) {
		if (gelf_getshdr(scn, &sh) == NULL) {
			DWARF_SET_ELF_ERROR(dbg, error);
			ret = DW_DLE_ELF;
			goto fail_cleanup;
		}

		memcpy(&e->eo_shdr[j], &sh, sizeof(sh));

		if ((name = elf_strptr(elf, e->eo_strndx, sh.sh_name)) ==
		    NULL) {
			DWARF_SET_ELF_ERROR(dbg, error);
			ret = DW_DLE_ELF;
			goto fail_cleanup;
		}

		for (i = 0; debug_name[i] != NULL; i++) {
			if (strcmp(name, debug_name[i]))
				continue;

			(void) elf_errno();
			if ((e->eo_data[j].ed_data = elf_getdata(scn, NULL)) ==
			    NULL) {
				elferr = elf_errno();
				if (elferr != 0) {
					_DWARF_SET_ERROR(dbg, error,
					    DW_DLE_ELF, elferr);
					ret = DW_DLE_ELF;
					goto fail_cleanup;
				}
			}

			if (_libdwarf.applyreloc) {
				if (_dwarf_elf_relocate(dbg, elf,
				    &e->eo_data[j], elf_ndxscn(scn), symtab_ndx,
				    symtab_data, error) != DW_DLE_NONE)
					goto fail_cleanup;
			}

			j++;
		}
	}

	assert(j == n);

	return (DW_DLE_NONE);

fail_cleanup:

	_dwarf_elf_deinit(dbg);

	return (ret);
}

void
_dwarf_elf_deinit(Dwarf_Debug dbg)
{
	Dwarf_Obj_Access_Interface *iface;
	Dwarf_Elf_Object *e;
	int i;

	iface = dbg->dbg_iface;
	assert(iface != NULL);

	e = iface->object;
	assert(e != NULL);

	if (e->eo_data) {
		for (i = 0; (Dwarf_Unsigned) i < e->eo_seccnt; i++) {
			if (e->eo_data[i].ed_alloc)
				free(e->eo_data[i].ed_alloc);
		}
		free(e->eo_data);
	}
	if (e->eo_shdr)
		free(e->eo_shdr);

	free(e);
	free(iface);

	dbg->dbg_iface = NULL;
}
