/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2014 Kai Wang
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

ELFTC_VCSID("$Id: dwarf_cu.c 3041 2014-05-18 15:11:03Z kaiwang27 $");

int
dwarf_next_cu_header_c(Dwarf_Debug dbg, Dwarf_Bool is_info,
    Dwarf_Unsigned *cu_length, Dwarf_Half *cu_version,
    Dwarf_Off *cu_abbrev_offset, Dwarf_Half *cu_pointer_size,
    Dwarf_Half *cu_offset_size, Dwarf_Half *cu_extension_size,
    Dwarf_Sig8 *type_signature, Dwarf_Unsigned *type_offset,
    Dwarf_Unsigned *cu_next_offset, Dwarf_Error *error)
{
	Dwarf_CU cu;
	int ret;

	if (dbg == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (is_info) {
		if (dbg->dbg_cu_current == NULL)
			ret = _dwarf_info_first_cu(dbg, error);
		else
			ret = _dwarf_info_next_cu(dbg, error);
	} else {
		if (dbg->dbg_tu_current == NULL)
			ret = _dwarf_info_first_tu(dbg, error);
		else
			ret = _dwarf_info_next_tu(dbg, error);
	}

	if (ret == DW_DLE_NO_ENTRY) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	} else if (ret != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	if (is_info) {
		if (dbg->dbg_cu_current == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
			return (DW_DLV_NO_ENTRY);
		}
		cu = dbg->dbg_cu_current;
	} else {
		if (dbg->dbg_tu_current == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
			return (DW_DLV_NO_ENTRY);
		}
		cu = dbg->dbg_tu_current;
	}

	if (cu_length)
		*cu_length = cu->cu_length;
	if (cu_version)
		*cu_version = cu->cu_version;
	if (cu_abbrev_offset)
		*cu_abbrev_offset = (Dwarf_Off) cu->cu_abbrev_offset;
	if (cu_pointer_size)
		*cu_pointer_size = cu->cu_pointer_size;
	if (cu_offset_size) {
		if (cu->cu_length_size == 4)
			*cu_offset_size = 4;
		else
			*cu_offset_size = 8;
	}
	if (cu_extension_size) {
		if (cu->cu_length_size == 4)
			*cu_extension_size = 0;
		else
			*cu_extension_size = 4;
	}
	if (cu_next_offset)
		*cu_next_offset	= cu->cu_next_offset;

	if (!is_info) {
		if (type_signature)
			*type_signature = cu->cu_type_sig;
		if (type_offset)
			*type_offset = cu->cu_type_offset;
	}

	return (DW_DLV_OK);
}


int
dwarf_next_cu_header_b(Dwarf_Debug dbg, Dwarf_Unsigned *cu_length,
    Dwarf_Half *cu_version, Dwarf_Off *cu_abbrev_offset,
    Dwarf_Half *cu_pointer_size, Dwarf_Half *cu_offset_size,
    Dwarf_Half *cu_extension_size, Dwarf_Unsigned *cu_next_offset,
    Dwarf_Error *error)
{

	return (dwarf_next_cu_header_c(dbg, 1, cu_length, cu_version,
	    cu_abbrev_offset, cu_pointer_size, cu_offset_size,
	    cu_extension_size, NULL, NULL, cu_next_offset, error));
}

int
dwarf_next_cu_header(Dwarf_Debug dbg, Dwarf_Unsigned *cu_length,
    Dwarf_Half *cu_version, Dwarf_Off *cu_abbrev_offset,
    Dwarf_Half *cu_pointer_size, Dwarf_Unsigned *cu_next_offset,
    Dwarf_Error *error)
{

	return (dwarf_next_cu_header_b(dbg, cu_length, cu_version,
	    cu_abbrev_offset, cu_pointer_size, NULL, NULL, cu_next_offset,
	    error));
}

int
dwarf_next_types_section(Dwarf_Debug dbg, Dwarf_Error *error)
{

	/* Free resource allocated for current .debug_types section. */
	_dwarf_type_unit_cleanup(dbg);
	dbg->dbg_types_loaded = 0;
	dbg->dbg_types_off = 0;

	/* Reset type unit pointer. */
	dbg->dbg_tu_current = NULL;

	/* Search for the next .debug_types section. */
	dbg->dbg_types_sec = _dwarf_find_next_types_section(dbg,
	    dbg->dbg_types_sec);

	if (dbg->dbg_types_sec == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}
