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

ELFTC_VCSID("$Id: dwarf_pro_die.c 2074 2011-10-27 03:34:33Z jkoshy $");

Dwarf_Unsigned
dwarf_add_die_to_debug(Dwarf_P_Debug dbg, Dwarf_P_Die first_die,
    Dwarf_Error *error)
{

	if (dbg == NULL || first_die == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	dbg->dbgp_root_die = first_die;

	return (DW_DLV_OK);
}

Dwarf_P_Die
dwarf_new_die(Dwarf_P_Debug dbg, Dwarf_Tag new_tag,
    Dwarf_P_Die parent, Dwarf_P_Die child, Dwarf_P_Die left_sibling,
    Dwarf_P_Die right_sibling, Dwarf_Error *error)
{
	Dwarf_P_Die die;
	int count;

	if (dbg == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	count = _dwarf_die_count_links(parent, child, left_sibling,
	    right_sibling);

	if (count > 1) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if (_dwarf_die_alloc(dbg, &die, error) != DW_DLE_NONE)
		return (DW_DLV_BADADDR);

	die->die_dbg = dbg;
	die->die_tag = new_tag;

	STAILQ_INSERT_TAIL(&dbg->dbgp_dielist, die, die_pro_next);

	if (count == 0)
		return (die);

	_dwarf_die_link(die, parent, child, left_sibling, right_sibling);

	return (die);
}

Dwarf_P_Die
dwarf_die_link(Dwarf_P_Die die, Dwarf_P_Die parent,
    Dwarf_P_Die child, Dwarf_P_Die left_sibling, Dwarf_P_Die right_sibling,
    Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	int count;


	if (die == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	dbg = die->die_dbg;
	count = _dwarf_die_count_links(parent, child, left_sibling,
	    right_sibling);

	if (count > 1) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	} else if (count == 0)
		return (die);

	_dwarf_die_link(die, parent, child, left_sibling, right_sibling);

	return (die);
}
