/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2010,2011,2014 Kai Wang
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

ELFTC_VCSID("$Id: libdwarf_info.c 3136 2014-12-24 16:04:38Z kaiwang27 $");

int
_dwarf_info_first_cu(Dwarf_Debug dbg, Dwarf_Error *error)
{
	Dwarf_CU cu;
	int ret;

	assert(dbg->dbg_cu_current == NULL);
	cu = STAILQ_FIRST(&dbg->dbg_cu);
	if (cu != NULL) {
		dbg->dbg_cu_current = cu;
		return (DW_DLE_NONE);
	}

	if (dbg->dbg_info_loaded)
		return (DW_DLE_NO_ENTRY);

	dbg->dbg_info_off = 0;
	ret = _dwarf_info_load(dbg, 0, 1, error);
	if (ret != DW_DLE_NONE)
		return (ret);

	dbg->dbg_cu_current = STAILQ_FIRST(&dbg->dbg_cu);

	return (DW_DLE_NONE);
}

int
_dwarf_info_first_tu(Dwarf_Debug dbg, Dwarf_Error *error)
{
	Dwarf_CU tu;
	int ret;

	assert(dbg->dbg_tu_current == NULL);
	tu = STAILQ_FIRST(&dbg->dbg_tu);
	if (tu != NULL) {
		dbg->dbg_tu_current = tu;
		return (DW_DLE_NONE);
	}

	if (dbg->dbg_types_loaded)
		return (DW_DLE_NO_ENTRY);

	dbg->dbg_types_off = 0;
	ret = _dwarf_info_load(dbg, 0, 0, error);
	if (ret != DW_DLE_NONE)
		return (ret);

	dbg->dbg_tu_current = STAILQ_FIRST(&dbg->dbg_tu);

	return (DW_DLE_NONE);
}

int
_dwarf_info_next_cu(Dwarf_Debug dbg, Dwarf_Error *error)
{
	Dwarf_CU cu;
	int ret;

	assert(dbg->dbg_cu_current != NULL);
	cu = STAILQ_NEXT(dbg->dbg_cu_current, cu_next);
	if (cu != NULL) {
		dbg->dbg_cu_current = cu;
		return (DW_DLE_NONE);
	}

	if (dbg->dbg_info_loaded) {
		dbg->dbg_cu_current = NULL;
		return (DW_DLE_NO_ENTRY);
	}

	ret = _dwarf_info_load(dbg, 0, 1, error);
	if (ret != DW_DLE_NONE)
		return (ret);

	dbg->dbg_cu_current = STAILQ_NEXT(dbg->dbg_cu_current, cu_next);

	return (DW_DLE_NONE);
}

int
_dwarf_info_next_tu(Dwarf_Debug dbg, Dwarf_Error *error)
{
	Dwarf_CU cu;
	int ret;

	assert(dbg->dbg_tu_current != NULL);
	cu = STAILQ_NEXT(dbg->dbg_tu_current, cu_next);
	if (cu != NULL) {
		dbg->dbg_tu_current = cu;
		return (DW_DLE_NONE);
	}

	if (dbg->dbg_types_loaded) {
		dbg->dbg_tu_current = NULL;
		return (DW_DLE_NO_ENTRY);
	}

	ret = _dwarf_info_load(dbg, 0, 0, error);
	if (ret != DW_DLE_NONE)
		return (ret);

	dbg->dbg_tu_current = STAILQ_NEXT(dbg->dbg_tu_current, cu_next);

	return (DW_DLE_NONE);
}

int
_dwarf_info_load(Dwarf_Debug dbg, Dwarf_Bool load_all, Dwarf_Bool is_info,
    Dwarf_Error *error)
{
	Dwarf_CU cu;
	Dwarf_Section *ds;
	int dwarf_size, ret;
	uint64_t length;
	uint64_t next_offset;
	uint64_t offset;

	ret = DW_DLE_NONE;

	if (is_info) {
		if (dbg->dbg_info_loaded)
			return (ret);
		offset = dbg->dbg_info_off;
		ds = dbg->dbg_info_sec;
		if (ds == NULL)
			return (DW_DLE_NO_ENTRY);
	} else {
		if (dbg->dbg_types_loaded)
			return (ret);
		offset = dbg->dbg_types_off;
		ds = dbg->dbg_types_sec;
		if (ds == NULL)
			return (DW_DLE_NO_ENTRY);
	}

	while (offset < ds->ds_size) {
		if ((cu = calloc(1, sizeof(struct _Dwarf_CU))) == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLE_MEMORY);
		}

		cu->cu_dbg = dbg;
		cu->cu_is_info = is_info;
		cu->cu_offset = offset;

		length = dbg->read(ds->ds_data, &offset, 4);
		if (length == 0xffffffff) {
			length = dbg->read(ds->ds_data, &offset, 8);
			dwarf_size = 8;
		} else
			dwarf_size = 4;
		cu->cu_dwarf_size = dwarf_size;

		/*
		 * Check if there is enough ELF data for this CU. This assumes
		 * that libelf gives us the entire section in one Elf_Data
		 * object.
		 */
		if (length > ds->ds_size - offset) {
			free(cu);
			DWARF_SET_ERROR(dbg, error, DW_DLE_CU_LENGTH_ERROR);
			return (DW_DLE_CU_LENGTH_ERROR);
		}

		/* Compute the offset to the next compilation unit: */
		next_offset = offset + length;
		if (is_info)
			dbg->dbg_info_off = next_offset;
		else
			dbg->dbg_types_off = next_offset;

		/* Initialise the compilation unit. */
		cu->cu_length		 = length;
		cu->cu_length_size	 = (dwarf_size == 4 ? 4 : 12);
		cu->cu_version		 = dbg->read(ds->ds_data, &offset, 2);
		cu->cu_abbrev_offset	 = dbg->read(ds->ds_data, &offset,
		    dwarf_size);
		cu->cu_abbrev_offset_cur = cu->cu_abbrev_offset;
		cu->cu_pointer_size	 = dbg->read(ds->ds_data, &offset, 1);
		cu->cu_next_offset	 = next_offset;

		/* .debug_types extra fields. */
		if (!is_info) {
			memcpy(cu->cu_type_sig.signature,
			    (char *) ds->ds_data + offset, 8);
			offset += 8;
			cu->cu_type_offset = dbg->read(ds->ds_data, &offset,
			    dwarf_size);
		}

		/* Add the compilation unit to the list. */
		if (is_info)
			STAILQ_INSERT_TAIL(&dbg->dbg_cu, cu, cu_next);
		else
			STAILQ_INSERT_TAIL(&dbg->dbg_tu, cu, cu_next);

		if (cu->cu_version < 2 || cu->cu_version > 4) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_VERSION_STAMP_ERROR);
			ret = DW_DLE_VERSION_STAMP_ERROR;
			break;
		}

		cu->cu_1st_offset = offset;

		offset = next_offset;

		if (!load_all)
			break;
	}

	if (is_info) {
		if ((Dwarf_Unsigned) dbg->dbg_info_off >= ds->ds_size)
			dbg->dbg_info_loaded = 1;
	} else {
		if ((Dwarf_Unsigned) dbg->dbg_types_off >= ds->ds_size)
			dbg->dbg_types_loaded = 1;
	}

	return (ret);
}

void
_dwarf_info_cleanup(Dwarf_Debug dbg)
{
	Dwarf_CU cu, tcu;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_READ);

	STAILQ_FOREACH_SAFE(cu, &dbg->dbg_cu, cu_next, tcu) {
		STAILQ_REMOVE(&dbg->dbg_cu, cu, _Dwarf_CU, cu_next);
		_dwarf_abbrev_cleanup(cu);
		if (cu->cu_lineinfo != NULL) {
			_dwarf_lineno_cleanup(cu->cu_lineinfo);
			cu->cu_lineinfo = NULL;
		}
		free(cu);
	}

	_dwarf_type_unit_cleanup(dbg);
}

void
_dwarf_type_unit_cleanup(Dwarf_Debug dbg)
{
	Dwarf_CU cu, tcu;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_READ);

	STAILQ_FOREACH_SAFE(cu, &dbg->dbg_tu, cu_next, tcu) {
		STAILQ_REMOVE(&dbg->dbg_tu, cu, _Dwarf_CU, cu_next);
		_dwarf_abbrev_cleanup(cu);
		free(cu);
	}
}

int
_dwarf_info_gen(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	Dwarf_P_Section ds;
	Dwarf_Rel_Section drs;
	Dwarf_Unsigned offset;
	Dwarf_CU cu;
	int ret;

	assert(dbg != NULL && dbg->write_alloc != NULL);

	if (dbg->dbgp_root_die == NULL)
		return (DW_DLE_NONE);

	/* Create the single CU for this debugging object. */
	if ((cu = calloc(1, sizeof(struct _Dwarf_CU))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	cu->cu_dbg = dbg;
	cu->cu_version = 2;	/* DWARF2 */
	cu->cu_pointer_size = dbg->dbg_pointer_size;
	STAILQ_INSERT_TAIL(&dbg->dbg_cu, cu, cu_next);

	/* Create .debug_info section. */
	if ((ret = _dwarf_section_init(dbg, &dbg->dbgp_info, ".debug_info", 0,
	    error)) != DW_DLE_NONE)
		goto gen_fail1;
	ds = dbg->dbgp_info;

	/* Create relocation section for .debug_init */
	if ((ret = _dwarf_reloc_section_init(dbg, &drs, ds, error)) !=
	    DW_DLE_NONE)
		goto gen_fail0;

	/* Length placeholder. (We only use 32-bit DWARF format) */
	RCHECK(WRITE_VALUE(cu->cu_length, 4));

	/* Write CU version */
	RCHECK(WRITE_VALUE(cu->cu_version, 2));

	/*
	 * Write abbrev offset. (always 0, we only support single CU)
	 * Also generate a relocation entry for this offset.
	 */
	RCHECK(_dwarf_reloc_entry_add(dbg, drs, ds, dwarf_drt_data_reloc, 4,
	    ds->ds_size, 0, cu->cu_abbrev_offset, ".debug_abbrev", error));

	/* Pointer size. */
	RCHECK(WRITE_VALUE(cu->cu_pointer_size, 1));

	/* Transform the DIE(s) of this CU. */
	RCHECK(_dwarf_die_gen(dbg, cu, drs, error));

	/* Now we can fill in the length of this CU. */
	cu->cu_length = ds->ds_size - 4;
	offset = 0;
	dbg->write(ds->ds_data, &offset, cu->cu_length, 4);

	/* Inform application the creation of .debug_info ELF section. */
	RCHECK(_dwarf_section_callback(dbg, ds, SHT_PROGBITS, 0, 0, 0, error));

	/*
	 * Inform application the creation of relocation section for
	 * .debug_info.
	 */
	RCHECK(_dwarf_reloc_section_finalize(dbg, drs, error));

	return (DW_DLE_NONE);

gen_fail:
	_dwarf_reloc_section_free(dbg, &drs);

gen_fail0:
	_dwarf_section_free(dbg, &dbg->dbgp_info);

gen_fail1:
	STAILQ_REMOVE(&dbg->dbg_cu, cu, _Dwarf_CU, cu_next);
	free(cu);

	return (ret);
}

void
_dwarf_info_pro_cleanup(Dwarf_P_Debug dbg)
{
	Dwarf_CU cu;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_WRITE);

	cu = STAILQ_FIRST(&dbg->dbg_cu);
	if (cu != NULL) {
		STAILQ_REMOVE(&dbg->dbg_cu, cu, _Dwarf_CU, cu_next);
		_dwarf_abbrev_cleanup(cu);
		free(cu);
	}
}
