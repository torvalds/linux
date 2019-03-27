/*-
 * Copyright (c) 2009,2011 Kai Wang
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
 *
 * $Id: dwarf_nametbl.m4 2074 2011-10-27 03:34:33Z jkoshy $
 */

define(`MAKE_NAMETBL_API',`
int
dwarf_get_$1s(Dwarf_Debug dbg, Dwarf_$2 **$1s,
    Dwarf_Signed *ret_count, Dwarf_Error *error)
{
	Dwarf_Section *ds;
	int ret;

	if (dbg == NULL || $1s == NULL || ret_count == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (dbg->dbg_$1s == NULL) {
		if ((ds = _dwarf_find_section(dbg, ".debug_$4")) != NULL) {
			ret = _dwarf_nametbl_init(dbg, &dbg->dbg_$1s, ds,
			    error);
			if (ret != DW_DLE_NONE)
				return (DW_DLV_ERROR);
		}
		if (dbg->dbg_$1s == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
			return (DW_DLV_NO_ENTRY);
		}
	}

	*$1s = dbg->dbg_$1s->ns_array;
	*ret_count = dbg->dbg_$1s->ns_len;

	return (DW_DLV_OK);
}

int
dwarf_$3name(Dwarf_$2 $1, char **ret_name, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = $1 != NULL ? $1->np_nt->nt_cu->cu_dbg : NULL;

	if ($1 == NULL || ret_name == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*ret_name = $1->np_name;

	return (DW_DLV_OK);
}

int
dwarf_$1_die_offset(Dwarf_$2 $1, Dwarf_Off *ret_offset,
    Dwarf_Error *error)
{
	Dwarf_NameTbl nt;
	Dwarf_Debug dbg;

	dbg = $1 != NULL ? $1->np_nt->nt_cu->cu_dbg : NULL;

	if ($1 == NULL || ret_offset == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	nt = $1->np_nt;
	assert(nt != NULL);

	*ret_offset = nt->nt_cu_offset + $1->np_offset;

	return (DW_DLV_OK);
}

int
dwarf_$1_cu_offset(Dwarf_$2 $1, Dwarf_Off *ret_offset,
    Dwarf_Error *error)
{
	Dwarf_NameTbl nt;
	Dwarf_Debug dbg;

	dbg = $1 != NULL ? $1->np_nt->nt_cu->cu_dbg : NULL;

	if ($1 == NULL || ret_offset == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	nt = $1->np_nt;
	assert(nt != NULL);

	*ret_offset = nt->nt_cu_offset;

	return (DW_DLV_OK);
}

int
dwarf_$1_name_offsets(Dwarf_$2 $1, char **ret_name, Dwarf_Off *die_offset,
    Dwarf_Off *cu_offset, Dwarf_Error *error)
{
	Dwarf_CU cu;
	Dwarf_Debug dbg;
	Dwarf_NameTbl nt;

	dbg = $1 != NULL ? $1->np_nt->nt_cu->cu_dbg : NULL;

	if ($1 == NULL || ret_name == NULL || die_offset == NULL ||
	    cu_offset == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	nt = $1->np_nt;
	assert(nt != NULL);

	cu = nt->nt_cu;
	assert(cu != NULL);

	*ret_name = $1->np_name;
	*die_offset = nt->nt_cu_offset + $1->np_offset;
	*cu_offset = cu->cu_1st_offset;

	return (DW_DLV_OK);
}

void
dwarf_$1s_dealloc(Dwarf_Debug dbg, Dwarf_$2 *$1s, Dwarf_Signed count)
{

	(void) dbg;
	(void) $1s;
	(void) count;
}
')
