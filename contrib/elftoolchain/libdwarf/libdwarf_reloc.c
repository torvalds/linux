/*-
 * Copyright (c) 2010 Kai Wang
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

ELFTC_VCSID("$Id: libdwarf_reloc.c 3578 2017-09-14 02:21:28Z emaste $");

Dwarf_Unsigned
_dwarf_get_reloc_type(Dwarf_P_Debug dbg, int is64)
{

	assert(dbg != NULL);

	switch (dbg->dbgp_isa) {
	case DW_ISA_AARCH64:
		return (is64 ? R_AARCH64_ABS64 : R_AARCH64_ABS32);
	case DW_ISA_X86:
		return (R_386_32);
	case DW_ISA_X86_64:
		return (is64 ? R_X86_64_64 : R_X86_64_32);
	case DW_ISA_SPARC:
		return (is64 ? R_SPARC_UA64 : R_SPARC_UA32);
	case DW_ISA_PPC:
		return (R_PPC_ADDR32);
	case DW_ISA_ARM:
		return (R_ARM_ABS32);
	case DW_ISA_MIPS:
		return (is64 ? R_MIPS_64 : R_MIPS_32);
	case DW_ISA_RISCV:
		return (is64 ? R_RISCV_64 : R_RISCV_32);
	case DW_ISA_IA64:
		return (is64 ? R_IA_64_DIR64LSB : R_IA_64_DIR32LSB);
	default:
		break;
	}
	return (0);		/* NOT REACHED */
}

int
_dwarf_get_reloc_size(Dwarf_Debug dbg, Dwarf_Unsigned rel_type)
{

	switch (dbg->dbg_machine) {
	case EM_NONE:
		break;
	case EM_AARCH64:
		if (rel_type == R_AARCH64_ABS32)
			return (4);
		else if (rel_type == R_AARCH64_ABS64)
			return (8);
		break;
	case EM_ARM:
		if (rel_type == R_ARM_ABS32)
			return (4);
		break;
	case EM_386:
	case EM_IAMCU:
		if (rel_type == R_386_32)
			return (4);
		break;
	case EM_X86_64:
		if (rel_type == R_X86_64_32)
			return (4);
		else if (rel_type == R_X86_64_64)
			return (8);
		break;
	case EM_SPARC:
		if (rel_type == R_SPARC_UA32)
			return (4);
		else if (rel_type == R_SPARC_UA64)
			return (8);
		break;
	case EM_PPC:
		if (rel_type == R_PPC_ADDR32)
			return (4);
		break;
	case EM_MIPS:
		if (rel_type == R_MIPS_32)
			return (4);
		else if (rel_type == R_MIPS_64)
			return (8);
		break;
	case EM_RISCV:
		if (rel_type == R_RISCV_32)
			return (4);
		else if (rel_type == R_RISCV_64)
			return (8);
		break;
	case EM_IA_64:
		if (rel_type == R_IA_64_SECREL32LSB)
			return (4);
		else if (rel_type == R_IA_64_DIR64LSB)
			return (8);
		break;
	default:
		break;
	}

	/* unknown relocation. */
	return (0);
}

int
_dwarf_reloc_section_init(Dwarf_P_Debug dbg, Dwarf_Rel_Section *drsp,
    Dwarf_P_Section ref, Dwarf_Error *error)
{
	Dwarf_Rel_Section drs;
	char name[128];
	int pseudo;

	assert(dbg != NULL && drsp != NULL && ref != NULL);

	if ((drs = calloc(1, sizeof(struct _Dwarf_Rel_Section))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	drs->drs_ref = ref;

	/*
	 * FIXME The logic here is most likely wrong. It should
	 * be the ISA that determines relocation type.
	 */
	if (dbg->dbgp_flags & DW_DLC_SIZE_64)
		drs->drs_addend = 1;
	else
		drs->drs_addend = 0;

	if (dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS)
		pseudo = 1;
	else
		pseudo = 0;

	snprintf(name, sizeof(name), "%s%s",
	    drs->drs_addend ? ".rela" : ".rel", ref->ds_name);
	if (_dwarf_section_init(dbg, &drs->drs_ds, name, pseudo, error) !=
	    DW_DLE_NONE) {
		free(drs);
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	STAILQ_INIT(&drs->drs_dre);
	STAILQ_INSERT_TAIL(&dbg->dbgp_drslist, drs, drs_next);
	dbg->dbgp_drscnt++;
	*drsp = drs;

	return (DW_DLE_NONE);
}

void
_dwarf_reloc_section_free(Dwarf_P_Debug dbg, Dwarf_Rel_Section *drsp)
{
	Dwarf_Rel_Section drs, tdrs;
	Dwarf_Rel_Entry dre, tdre;

	assert(dbg != NULL && drsp != NULL);

	if (*drsp == NULL)
		return;

	STAILQ_FOREACH_SAFE(drs, &dbg->dbgp_drslist, drs_next, tdrs) {
		if (drs != *drsp)
			continue;
		STAILQ_REMOVE(&dbg->dbgp_drslist, drs, _Dwarf_Rel_Section,
		    drs_next);
		STAILQ_FOREACH_SAFE(dre, &drs->drs_dre, dre_next, tdre) {
			STAILQ_REMOVE(&drs->drs_dre, dre, _Dwarf_Rel_Entry,
			    dre_next);
			free(dre);
		}
		if ((dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS) == 0)
			_dwarf_section_free(dbg, &drs->drs_ds);
		else {
			if (drs->drs_ds->ds_name)
				free(drs->drs_ds->ds_name);
			free(drs->drs_ds);
		}
		free(drs);
		*drsp = NULL;
		dbg->dbgp_drscnt--;
		break;
	}
}

int
_dwarf_reloc_entry_add(Dwarf_P_Debug dbg, Dwarf_Rel_Section drs,
    Dwarf_P_Section ds, unsigned char type, unsigned char length,
    Dwarf_Unsigned offset, Dwarf_Unsigned symndx, Dwarf_Unsigned addend,
    const char *secname, Dwarf_Error *error)
{
	Dwarf_Rel_Entry dre;
	Dwarf_Unsigned reloff;
	int ret;

	assert(drs != NULL);
	assert(offset <= ds->ds_size);
	reloff = offset;

	/*
	 * If the DW_DLC_SYMBOLIC_RELOCATIONS flag is set or ElfXX_Rel
	 * is used instead of ELfXX_Rela, we need to write the addend
	 * in the storage unit to be relocated. Otherwise write 0 in the
	 * storage unit and the addend will be written into relocation
	 * section later.
	 */
	if ((dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS) ||
	    drs->drs_addend == 0)
		ret = dbg->write_alloc(&ds->ds_data, &ds->ds_cap, &offset,
		    addend, length, error);
	else
		ret = dbg->write_alloc(&ds->ds_data, &ds->ds_cap, &offset,
		    0, length, error);
	if (ret != DW_DLE_NONE)
		return (ret);
	if (offset > ds->ds_size)
		ds->ds_size = offset;

	if ((dre = calloc(1, sizeof(struct _Dwarf_Rel_Entry))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	STAILQ_INSERT_TAIL(&drs->drs_dre, dre, dre_next);
	dre->dre_type = type;
	dre->dre_length = length;
	dre->dre_offset = reloff;
	dre->dre_symndx = symndx;
	dre->dre_addend = addend;
	dre->dre_secname = secname;
	drs->drs_drecnt++;

	return (DW_DLE_NONE);
}

int
_dwarf_reloc_entry_add_pair(Dwarf_P_Debug dbg, Dwarf_Rel_Section drs,
    Dwarf_P_Section ds, unsigned char length, Dwarf_Unsigned offset,
    Dwarf_Unsigned symndx, Dwarf_Unsigned esymndx, Dwarf_Unsigned symoff,
    Dwarf_Unsigned esymoff, Dwarf_Error *error)
{
	Dwarf_Rel_Entry dre;
	Dwarf_Unsigned reloff;
	int ret;

	assert(drs != NULL);
	assert(offset <= ds->ds_size);
	assert(dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS);
	reloff = offset;

	/* Write net offset into section stream. */
	ret = dbg->write_alloc(&ds->ds_data, &ds->ds_cap, &offset,
	    esymoff - symoff, length, error);
	if (ret != DW_DLE_NONE)
		return (ret);
	if (offset > ds->ds_size)
		ds->ds_size = offset;

	if ((dre = calloc(2, sizeof(struct _Dwarf_Rel_Entry))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	STAILQ_INSERT_TAIL(&drs->drs_dre, &dre[0], dre_next);
	STAILQ_INSERT_TAIL(&drs->drs_dre, &dre[1], dre_next);
	dre[0].dre_type = dwarf_drt_first_of_length_pair;
	dre[0].dre_length = length;
	dre[0].dre_offset = reloff;
	dre[0].dre_symndx = symndx;
	dre[0].dre_addend = 0;
	dre[0].dre_secname = NULL;
	dre[1].dre_type = dwarf_drt_second_of_length_pair;
	dre[1].dre_length = length;
	dre[1].dre_offset = reloff;
	dre[1].dre_symndx = esymndx;
	dre[1].dre_addend = 0;
	dre[1].dre_secname = NULL;
	drs->drs_drecnt += 2;

	return (DW_DLE_NONE);
}

int
_dwarf_reloc_section_finalize(Dwarf_P_Debug dbg, Dwarf_Rel_Section drs,
    Dwarf_Error *error)
{
	Dwarf_P_Section ds;
	Dwarf_Unsigned unit;
	int ret, size;

	assert(dbg != NULL && drs != NULL && drs->drs_ds != NULL &&
	    drs->drs_ref != NULL);

	ds = drs->drs_ds;

	/*
	 * Calculate the size (in bytes) of the relocation section.
	 */
	if (dbg->dbgp_flags & DW_DLC_SIZE_64)
		unit = drs->drs_addend ? sizeof(Elf64_Rela) : sizeof(Elf64_Rel);
	else
		unit = drs->drs_addend ? sizeof(Elf32_Rela) : sizeof(Elf32_Rel);
	assert(ds->ds_size == 0);
	size = drs->drs_drecnt * unit;

	/*
	 * Discard this relocation section if there is no entry in it.
	 */
	if (size == 0) {
		_dwarf_reloc_section_free(dbg, &drs);
		return (DW_DLE_NONE);
	}

	/*
	 * If we are under stream mode, realloc the section data block to
	 * this size.
	 */
	if ((dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS) == 0) {
		ds->ds_cap = size;
		if ((ds->ds_data = realloc(ds->ds_data, (size_t) ds->ds_cap)) ==
		    NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLE_MEMORY);
		}
	}

	/*
	 * Notify the application the creation of this relocation section.
	 * Note that the section link here should point to the .symtab
	 * section, we set it to 0 since we have no way to know .symtab
	 * section index.
	 */
	ret = _dwarf_pro_callback(dbg, ds->ds_name, size,
	    drs->drs_addend ? SHT_RELA : SHT_REL, 0, 0, drs->drs_ref->ds_ndx,
	    &ds->ds_symndx, NULL);
	if (ret < 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ELF_SECT_ERR);
		return (DW_DLE_ELF_SECT_ERR);
	}
	ds->ds_ndx = ret;

	return (DW_DLE_NONE);
}

int
_dwarf_reloc_section_gen(Dwarf_P_Debug dbg, Dwarf_Rel_Section drs,
    Dwarf_Error *error)
{
	Dwarf_Rel_Entry dre;
	Dwarf_P_Section ds;
	Dwarf_Unsigned type;
	int ret;

	assert((dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS) == 0);
	assert(drs->drs_ds != NULL && drs->drs_ds->ds_size == 0);
	assert(!STAILQ_EMPTY(&drs->drs_dre));
	ds = drs->drs_ds;

	STAILQ_FOREACH(dre, &drs->drs_dre, dre_next) {
		assert(dre->dre_length == 4 || dre->dre_length == 8);
		type = _dwarf_get_reloc_type(dbg, dre->dre_length == 8);
		if (dbg->dbgp_flags & DW_DLC_SIZE_64) {
			/* Write r_offset (8 bytes) */
			ret = dbg->write_alloc(&ds->ds_data, &ds->ds_cap,
			    &ds->ds_size, dre->dre_offset, 8, error);
			if (ret != DW_DLE_NONE)
				return (ret);
			/* Write r_info (8 bytes) */
			ret = dbg->write_alloc(&ds->ds_data, &ds->ds_cap,
			    &ds->ds_size, ELF64_R_INFO(dre->dre_symndx, type),
			    8, error);
			if (ret != DW_DLE_NONE)
				return (ret);
			/* Write r_addend (8 bytes) */
			if (drs->drs_addend) {
				ret = dbg->write_alloc(&ds->ds_data,
				    &ds->ds_cap, &ds->ds_size, dre->dre_addend,
				    8, error);
				if (ret != DW_DLE_NONE)
					return (ret);
			}
		} else {
			/* Write r_offset (4 bytes) */
			ret = dbg->write_alloc(&ds->ds_data, &ds->ds_cap,
			    &ds->ds_size, dre->dre_offset, 4, error);
			if (ret != DW_DLE_NONE)
				return (ret);
			/* Write r_info (4 bytes) */
			ret = dbg->write_alloc(&ds->ds_data, &ds->ds_cap,
			    &ds->ds_size, ELF32_R_INFO(dre->dre_symndx, type),
			    4, error);
			if (ret != DW_DLE_NONE)
				return (ret);
			/* Write r_addend (4 bytes) */
			if (drs->drs_addend) {
				ret = dbg->write_alloc(&ds->ds_data,
				    &ds->ds_cap, &ds->ds_size, dre->dre_addend,
				    4, error);
				if (ret != DW_DLE_NONE)
					return (ret);
			}
		}
	}
	assert(ds->ds_size == ds->ds_cap);

	return (DW_DLE_NONE);
}

int
_dwarf_reloc_gen(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	Dwarf_Rel_Section drs;
	Dwarf_Rel_Entry dre;
	Dwarf_P_Section ds;
	int ret;

	STAILQ_FOREACH(drs, &dbg->dbgp_drslist, drs_next) {
		/*
		 * Update relocation entries: translate any section name
		 * reference to section symbol index.
		 */
		STAILQ_FOREACH(dre, &drs->drs_dre, dre_next) {
			if (dre->dre_secname == NULL)
				continue;
			ds = _dwarf_pro_find_section(dbg, dre->dre_secname);
			assert(ds != NULL && ds->ds_symndx != 0);
			dre->dre_symndx = ds->ds_symndx;
		}

		/*
		 * Generate ELF relocation section if we are under stream
		 * mode.
		 */
		if ((dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS) == 0) {
			ret = _dwarf_reloc_section_gen(dbg, drs, error);
			if (ret != DW_DLE_NONE)
				return (ret);
		}
	}

	return (DW_DLE_NONE);
}

void
_dwarf_reloc_cleanup(Dwarf_P_Debug dbg)
{
	Dwarf_Rel_Section drs, tdrs;
	Dwarf_Rel_Entry dre, tdre;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_WRITE);

	STAILQ_FOREACH_SAFE(drs, &dbg->dbgp_drslist, drs_next, tdrs) {
		STAILQ_REMOVE(&dbg->dbgp_drslist, drs, _Dwarf_Rel_Section,
		    drs_next);
		free(drs->drs_drd);
		STAILQ_FOREACH_SAFE(dre, &drs->drs_dre, dre_next, tdre) {
			STAILQ_REMOVE(&drs->drs_dre, dre, _Dwarf_Rel_Entry,
			    dre_next);
			free(dre);
		}
		if (dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS) {
			if (drs->drs_ds) {
				if (drs->drs_ds->ds_name)
					free(drs->drs_ds->ds_name);
				free(drs->drs_ds);
			}
		}
		free(drs);
	}
	dbg->dbgp_drscnt = 0;
	dbg->dbgp_drspos = NULL;
}
