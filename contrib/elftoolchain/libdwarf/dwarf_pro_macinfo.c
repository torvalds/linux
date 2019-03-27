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

ELFTC_VCSID("$Id: dwarf_pro_macinfo.c 2074 2011-10-27 03:34:33Z jkoshy $");

static int
_dwarf_add_macro(Dwarf_P_Debug dbg, int type, Dwarf_Unsigned lineno,
    Dwarf_Signed fileindex, char *str1, char *str2, Dwarf_Error *error)
{
	Dwarf_Macro_Details *md;
	int len;

	dbg->dbgp_mdlist = realloc(dbg->dbgp_mdlist,
	    (size_t) dbg->dbgp_mdcnt + 1);
	if (dbg->dbgp_mdlist == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_ERROR);
	}

	md = &dbg->dbgp_mdlist[dbg->dbgp_mdcnt];
	dbg->dbgp_mdcnt++;

	md->dmd_offset = 0;
	md->dmd_type = type;
	md->dmd_lineno = lineno;
	md->dmd_fileindex = fileindex;
	md->dmd_macro = NULL;

	if (str1 == NULL)
		return (DW_DLV_OK);
	else if (str2 == NULL) {
		if ((md->dmd_macro = strdup(str1)) == NULL) {
			dbg->dbgp_mdcnt--;
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLV_ERROR);
		}
		return (DW_DLV_OK);
	} else {
		len = strlen(str1) + strlen(str2) + 2;
		if ((md->dmd_macro = malloc(len)) == NULL) {
			dbg->dbgp_mdcnt--;
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLV_ERROR);
		}
		snprintf(md->dmd_macro, len, "%s %s", str1, str2);
		return (DW_DLV_OK);
	}
}

int
dwarf_def_macro(Dwarf_P_Debug dbg, Dwarf_Unsigned lineno, char *name,
    char *value, Dwarf_Error *error)
{

	if (dbg == NULL || name == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	return (_dwarf_add_macro(dbg, DW_MACINFO_define, lineno, -1, name,
	    value, error));
}

int
dwarf_undef_macro(Dwarf_P_Debug dbg, Dwarf_Unsigned lineno, char *name,
    Dwarf_Error *error)
{

	if (dbg == NULL || name == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	return (_dwarf_add_macro(dbg, DW_MACINFO_undef, lineno, -1, name,
	    NULL, error));
}

int
dwarf_start_macro_file(Dwarf_P_Debug dbg, Dwarf_Unsigned lineno,
    Dwarf_Unsigned fileindex, Dwarf_Error *error)
{

	if (dbg == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	return (_dwarf_add_macro(dbg, DW_MACINFO_start_file, lineno, fileindex,
	    NULL, NULL, error));
}

int
dwarf_end_macro_file(Dwarf_P_Debug dbg, Dwarf_Error *error)
{

	if (dbg == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	return (_dwarf_add_macro(dbg, DW_MACINFO_end_file, 0, -1,
	    NULL, NULL, error));
}

int
dwarf_vendor_ext(Dwarf_P_Debug dbg, Dwarf_Unsigned constant, char *string,
    Dwarf_Error *error)
{

	if (dbg == NULL || string == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	return (_dwarf_add_macro(dbg, DW_MACINFO_vendor_ext, constant, -1,
	    string, NULL, error));
}
