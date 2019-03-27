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

ELFTC_VCSID("$Id: dwarf_pro_arange.c 2074 2011-10-27 03:34:33Z jkoshy $");

Dwarf_Unsigned
dwarf_add_arange(Dwarf_P_Debug dbg, Dwarf_Addr start, Dwarf_Unsigned length,
    Dwarf_Signed symbol_index, Dwarf_Error *error)
{

	return (dwarf_add_arange_b(dbg, start, length, symbol_index, 0, 0,
	    error));
}

Dwarf_Unsigned
dwarf_add_arange_b(Dwarf_P_Debug dbg, Dwarf_Addr start, Dwarf_Unsigned length,
    Dwarf_Unsigned symbol_index, Dwarf_Unsigned end_symbol_index,
    Dwarf_Addr offset_from_end_symbol, Dwarf_Error *error)
{
	Dwarf_ArangeSet as;
	Dwarf_Arange ar;

	if (dbg == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (0);
	}
	as = dbg->dbgp_as;

	if (end_symbol_index > 0 &&
	    (dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS) == 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (0);
	}

	if ((ar = calloc(1, sizeof(struct _Dwarf_Arange))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (0);
	}
	ar->ar_as = as;
	ar->ar_address = start;
	ar->ar_range = length;
	ar->ar_symndx = symbol_index;
	ar->ar_esymndx = end_symbol_index;
	ar->ar_eoff = offset_from_end_symbol;
	STAILQ_INSERT_TAIL(&as->as_arlist, ar, ar_next);

	return (1);
}
