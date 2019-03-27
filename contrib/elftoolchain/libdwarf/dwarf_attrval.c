/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
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

ELFTC_VCSID("$Id: dwarf_attrval.c 3509 2016-12-29 03:58:41Z emaste $");

int
dwarf_attrval_flag(Dwarf_Die die, Dwarf_Half attr, Dwarf_Bool *valp, Dwarf_Error *err)
{
	Dwarf_Attribute at;
	Dwarf_Debug dbg;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || valp == NULL) {
		DWARF_SET_ERROR(dbg, err, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*valp = 0;

	if ((at = _dwarf_attr_find(die, attr)) == NULL) {
		DWARF_SET_ERROR(dbg, err, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	switch (at->at_form) {
	case DW_FORM_flag:
	case DW_FORM_flag_present:
		*valp = (Dwarf_Bool) (!!at->u[0].u64);
		break;
	default:
		DWARF_SET_ERROR(dbg, err, DW_DLE_ATTR_FORM_BAD);
		return (DW_DLV_ERROR);
	}

	return (DW_DLV_OK);
}

int
dwarf_attrval_string(Dwarf_Die die, Dwarf_Half attr, const char **strp, Dwarf_Error *err)
{
	Dwarf_Attribute at;
	Dwarf_Debug dbg;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || strp == NULL) {
		DWARF_SET_ERROR(dbg, err, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*strp = NULL;

	if ((at = _dwarf_attr_find(die, attr)) == NULL) {
		DWARF_SET_ERROR(dbg, err, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	switch (at->at_form) {
	case DW_FORM_strp:
		*strp = at->u[1].s;
		break;
	case DW_FORM_string:
		*strp = at->u[0].s;
		break;
	default:
		DWARF_SET_ERROR(dbg, err, DW_DLE_ATTR_FORM_BAD);
		return (DW_DLV_ERROR);
	}

	return (DW_DLV_OK);
}

int
dwarf_attrval_signed(Dwarf_Die die, Dwarf_Half attr, Dwarf_Signed *valp, Dwarf_Error *err)
{
	Dwarf_Attribute at;
	Dwarf_Debug dbg;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || valp == NULL) {
		DWARF_SET_ERROR(dbg, err, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*valp = 0;

	if ((at = _dwarf_attr_find(die, attr)) == NULL) {
		DWARF_SET_ERROR(dbg, err, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	switch (at->at_form) {
	case DW_FORM_data1:
		*valp = (int8_t) at->u[0].s64;
		break;
	case DW_FORM_data2:
		*valp = (int16_t) at->u[0].s64;
		break;
	case DW_FORM_data4:
		*valp = (int32_t) at->u[0].s64;
		break;
	case DW_FORM_data8:
	case DW_FORM_sdata:
		*valp = at->u[0].s64;
		break;
	default:
		DWARF_SET_ERROR(dbg, err, DW_DLE_ATTR_FORM_BAD);
		return (DW_DLV_ERROR);
	}

	return (DW_DLV_OK);
}

int
dwarf_attrval_unsigned(Dwarf_Die die, Dwarf_Half attr, Dwarf_Unsigned *valp, Dwarf_Error *err)
{
	Dwarf_Attribute at;
	Dwarf_Die die1;
	Dwarf_Unsigned val;
	Dwarf_Debug dbg;
	int first;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || valp == NULL) {
		DWARF_SET_ERROR(dbg, err, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*valp = 0;

	die1 = NULL;
	for (;;) {
		if ((at = _dwarf_attr_find(die, attr)) != NULL ||
		    attr != DW_AT_type)
			break;
		if ((at = _dwarf_attr_find(die, DW_AT_abstract_origin)) ==
		    NULL &&
		    (at = _dwarf_attr_find(die, DW_AT_specification)) == NULL)
			break;

		switch (at->at_form) {
		case DW_FORM_ref1:
		case DW_FORM_ref2:
		case DW_FORM_ref4:
		case DW_FORM_ref8:
		case DW_FORM_ref_udata:
			val = at->u[0].u64;
			first = (die1 == NULL);
			die1 = _dwarf_die_find(die, val);
			if (!first)
				dwarf_dealloc(dbg, die, DW_DLA_DIE);
			if (die1 == NULL) {
				DWARF_SET_ERROR(dbg, err, DW_DLE_NO_ENTRY);
				return (DW_DLV_NO_ENTRY);
			}
			die = die1;
			break;
		default:
			DWARF_SET_ERROR(dbg, err, DW_DLE_ATTR_FORM_BAD);
			return (DW_DLV_ERROR);
		}
	}

	if (at == NULL) {
		DWARF_SET_ERROR(dbg, err, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	switch (at->at_form) {
	case DW_FORM_addr:
	case DW_FORM_data1:
	case DW_FORM_data2:
	case DW_FORM_data4:
	case DW_FORM_data8:
	case DW_FORM_udata:
	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_ref8:
	case DW_FORM_ref_udata:
		*valp = at->u[0].u64;
		break;
	default:
		if (die1 != NULL)
			dwarf_dealloc(dbg, die1, DW_DLA_DIE);
		DWARF_SET_ERROR(dbg, err, DW_DLE_ATTR_FORM_BAD);
		return (DW_DLV_ERROR);
	}

	if (die1 != NULL)
		dwarf_dealloc(dbg, die1, DW_DLA_DIE);

	return (DW_DLV_OK);
}
