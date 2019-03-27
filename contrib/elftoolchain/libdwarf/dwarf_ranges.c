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
 */

#include "_libdwarf.h"

ELFTC_VCSID("$Id: dwarf_ranges.c 3029 2014-04-21 23:26:02Z kaiwang27 $");

static int
_dwarf_get_ranges(Dwarf_Debug dbg, Dwarf_CU cu, Dwarf_Off off,
    Dwarf_Ranges **ranges, Dwarf_Signed *ret_cnt, Dwarf_Unsigned *ret_byte_cnt,
    Dwarf_Error *error)
{
	Dwarf_Rangelist rl;
	int ret;

	assert(cu != NULL);
	if (_dwarf_ranges_find(dbg, off, &rl) == DW_DLE_NO_ENTRY) {
		ret = _dwarf_ranges_add(dbg, cu, off, &rl, error);
		if (ret != DW_DLE_NONE)
			return (DW_DLV_ERROR);
	}

	*ranges = rl->rl_rgarray;
	*ret_cnt = rl->rl_rglen;

	if (ret_byte_cnt != NULL)
		*ret_byte_cnt = cu->cu_pointer_size * rl->rl_rglen * 2;

	return (DW_DLV_OK);
}

int
dwarf_get_ranges(Dwarf_Debug dbg, Dwarf_Off offset, Dwarf_Ranges **ranges,
    Dwarf_Signed *ret_cnt, Dwarf_Unsigned *ret_byte_cnt, Dwarf_Error *error)
{

	if (dbg == NULL || ranges == NULL || ret_cnt == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (!dbg->dbg_info_loaded) {
		if (_dwarf_info_load(dbg, 1, 1, error) != DW_DLE_NONE)
			return (DW_DLV_ERROR);
	}

	return (_dwarf_get_ranges(dbg, STAILQ_FIRST(&dbg->dbg_cu), offset,
	    ranges, ret_cnt, ret_byte_cnt, error));
}

int
dwarf_get_ranges_a(Dwarf_Debug dbg, Dwarf_Off offset, Dwarf_Die die,
    Dwarf_Ranges **ranges, Dwarf_Signed *ret_cnt, Dwarf_Unsigned *ret_byte_cnt,
    Dwarf_Error *error)
{

	if (dbg == NULL || die == NULL || ranges == NULL || ret_cnt == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	return (_dwarf_get_ranges(dbg, die->die_cu, offset, ranges, ret_cnt,
	    ret_byte_cnt, error));
}
