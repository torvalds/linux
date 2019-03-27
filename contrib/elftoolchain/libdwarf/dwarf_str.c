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

ELFTC_VCSID("$Id: dwarf_str.c 3295 2016-01-08 22:08:10Z jkoshy $");

int
dwarf_get_str(Dwarf_Debug dbg, Dwarf_Off offset, char **string,
    Dwarf_Signed *ret_strlen, Dwarf_Error *error)
{
	Dwarf_Section *ds;

	if (dbg == NULL || string == NULL || ret_strlen == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	ds = _dwarf_find_section(dbg, ".debug_str");
	if (ds == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	if (offset > ds->ds_size) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (offset == ds->ds_size) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	*string = (char *) ds->ds_data + offset;
	*ret_strlen = strlen(*string);

	return (DW_DLV_OK);
}
