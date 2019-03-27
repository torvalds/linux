/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2009,2011,2014 Kai Wang
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

ELFTC_VCSID("$Id: dwarf_die.c 3039 2014-05-18 15:10:56Z kaiwang27 $");

int
dwarf_child(Dwarf_Die die, Dwarf_Die *ret_die, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Dwarf_Section *ds;
	Dwarf_CU cu;
	int ret;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || ret_die == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (die->die_ab->ab_children == DW_CHILDREN_no)
		return (DW_DLV_NO_ENTRY);

	dbg = die->die_dbg;
	cu = die->die_cu;
	ds = cu->cu_is_info ? dbg->dbg_info_sec : dbg->dbg_types_sec;
	ret = _dwarf_die_parse(die->die_dbg, ds, cu, cu->cu_dwarf_size,
	    die->die_next_off, cu->cu_next_offset, ret_die, 0, error);

	if (ret == DW_DLE_NO_ENTRY) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	} else if (ret != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	return (DW_DLV_OK);
}

int
dwarf_siblingof_b(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Die *ret_die,
    Dwarf_Bool is_info, Dwarf_Error *error)
{
	Dwarf_CU cu;
	Dwarf_Attribute at;
	Dwarf_Section *ds;
	uint64_t offset;
	int ret, search_sibling;

	if (dbg == NULL || ret_die == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	ds = is_info ? dbg->dbg_info_sec : dbg->dbg_types_sec;
	cu = is_info ? dbg->dbg_cu_current : dbg->dbg_tu_current;

	if (cu == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_DIE_NO_CU_CONTEXT);
		return (DW_DLV_ERROR);
	}

	/* Application requests the first DIE in this CU. */
	if (die == NULL)
		return (dwarf_offdie_b(dbg, cu->cu_1st_offset, is_info,
		    ret_die, error));

	/*
	 * Check if the `is_info' flag matches the debug section the
	 * DIE belongs to.
	 */
	if (is_info != die->die_cu->cu_is_info) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	/*
	 * If the DIE doesn't have any children, its sibling sits next
	 * right to it.
	 */
	search_sibling = 0;
	if (die->die_ab->ab_children == DW_CHILDREN_no)
		offset = die->die_next_off;
	else {
		/*
		 * Look for DW_AT_sibling attribute for the offset of
		 * its sibling.
		 */
		if ((at = _dwarf_attr_find(die, DW_AT_sibling)) != NULL) {
			if (at->at_form != DW_FORM_ref_addr)
				offset = at->u[0].u64 + cu->cu_offset;
			else
				offset = at->u[0].u64;
		} else {
			offset = die->die_next_off;
			search_sibling = 1;
		}
	}

	ret = _dwarf_die_parse(die->die_dbg, ds, cu, cu->cu_dwarf_size, offset,
	    cu->cu_next_offset, ret_die, search_sibling, error);
	
	if (ret == DW_DLE_NO_ENTRY) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	} else if (ret != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	return (DW_DLV_OK);
}


int
dwarf_siblingof(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Die *ret_die,
    Dwarf_Error *error)
{

	return (dwarf_siblingof_b(dbg, die, ret_die, 1, error));
}

static int
_dwarf_search_die_within_cu(Dwarf_Debug dbg, Dwarf_Section *s, Dwarf_CU cu,
    Dwarf_Off offset, Dwarf_Die *ret_die, Dwarf_Error *error)
{

	assert(dbg != NULL && cu != NULL && ret_die != NULL);

	return (_dwarf_die_parse(dbg, s, cu, cu->cu_dwarf_size,
	    offset, cu->cu_next_offset, ret_die, 0, error));
}

int
dwarf_offdie_b(Dwarf_Debug dbg, Dwarf_Off offset, Dwarf_Bool is_info,
    Dwarf_Die *ret_die, Dwarf_Error *error)
{
	Dwarf_Section *ds;
	Dwarf_CU cu;
	int ret;

	if (dbg == NULL || ret_die == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	ds = is_info ? dbg->dbg_info_sec : dbg->dbg_types_sec;
	cu = is_info ? dbg->dbg_cu_current : dbg->dbg_tu_current;

	/* First search the current CU. */
	if (cu != NULL) {
		if (offset > cu->cu_offset && offset < cu->cu_next_offset) {
			ret = _dwarf_search_die_within_cu(dbg, ds, cu, offset,
			    ret_die, error);
			if (ret == DW_DLE_NO_ENTRY) {
				DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
				return (DW_DLV_NO_ENTRY);
			} else if (ret != DW_DLE_NONE)
				return (DW_DLV_ERROR);
			return (DW_DLV_OK);
		}
	}

	/* Search other CUs. */
	ret = _dwarf_info_load(dbg, 1, is_info, error);
	if (ret != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	if (is_info) {
		STAILQ_FOREACH(cu, &dbg->dbg_cu, cu_next) {
			if (offset < cu->cu_offset ||
			    offset > cu->cu_next_offset)
				continue;
			ret = _dwarf_search_die_within_cu(dbg, ds, cu, offset,
			    ret_die, error);
			if (ret == DW_DLE_NO_ENTRY) {
				DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
				return (DW_DLV_NO_ENTRY);
			} else if (ret != DW_DLE_NONE)
				return (DW_DLV_ERROR);
			return (DW_DLV_OK);
		}
	} else {
		STAILQ_FOREACH(cu, &dbg->dbg_tu, cu_next) {
			if (offset < cu->cu_offset ||
			    offset > cu->cu_next_offset)
				continue;
			ret = _dwarf_search_die_within_cu(dbg, ds, cu, offset,
			    ret_die, error);
			if (ret == DW_DLE_NO_ENTRY) {
				DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
				return (DW_DLV_NO_ENTRY);
			} else if (ret != DW_DLE_NONE)
				return (DW_DLV_ERROR);
			return (DW_DLV_OK);
		}
	}

	DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
	return (DW_DLV_NO_ENTRY);
}

int
dwarf_offdie(Dwarf_Debug dbg, Dwarf_Off offset, Dwarf_Die *ret_die,
    Dwarf_Error *error)
{

	return (dwarf_offdie_b(dbg, offset, 1, ret_die, error));
}

int
dwarf_tag(Dwarf_Die die, Dwarf_Half *tag, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || tag == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	assert(die->die_ab != NULL);

	*tag = (Dwarf_Half) die->die_ab->ab_tag;

	return (DW_DLV_OK);
}

int
dwarf_dieoffset(Dwarf_Die die, Dwarf_Off *ret_offset, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || ret_offset == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*ret_offset = die->die_offset;

	return (DW_DLV_OK);
}

int
dwarf_die_CU_offset(Dwarf_Die die, Dwarf_Off *ret_offset, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Dwarf_CU cu;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || ret_offset == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	cu = die->die_cu;
	assert(cu != NULL);

	*ret_offset = die->die_offset - cu->cu_offset;

	return (DW_DLV_OK);
}

int
dwarf_die_CU_offset_range(Dwarf_Die die, Dwarf_Off *cu_offset,
    Dwarf_Off *cu_length, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Dwarf_CU cu;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || cu_offset == NULL || cu_length == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	cu = die->die_cu;
	assert(cu != NULL);

	*cu_offset = cu->cu_offset;
	*cu_length = cu->cu_length + cu->cu_length_size;

	return (DW_DLV_OK);
}

int
dwarf_diename(Dwarf_Die die, char **ret_name, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || ret_name == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (die->die_name == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	*ret_name = die->die_name;

	return (DW_DLV_OK);
}

int
dwarf_die_abbrev_code(Dwarf_Die die)
{

	assert(die != NULL);

	return (die->die_abnum);
}

int
dwarf_get_cu_die_offset_given_cu_header_offset_b(Dwarf_Debug dbg,
    Dwarf_Off in_cu_header_offset, Dwarf_Bool is_info,
    Dwarf_Off *out_cu_die_offset, Dwarf_Error *error)
{
	Dwarf_CU cu;

	if (dbg == NULL || out_cu_die_offset == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (is_info) {
		STAILQ_FOREACH(cu, &dbg->dbg_cu, cu_next) {
			if (cu->cu_offset == in_cu_header_offset) {
				*out_cu_die_offset = cu->cu_1st_offset;
				break;
			}
		}
	} else {
		STAILQ_FOREACH(cu, &dbg->dbg_tu, cu_next) {
			if (cu->cu_offset == in_cu_header_offset) {
				*out_cu_die_offset = cu->cu_1st_offset;
				break;
			}
		}
	}

	if (cu == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_cu_die_offset_given_cu_header_offset(Dwarf_Debug dbg,
    Dwarf_Off in_cu_header_offset, Dwarf_Off *out_cu_die_offset,
    Dwarf_Error *error)
{

	return (dwarf_get_cu_die_offset_given_cu_header_offset_b(dbg,
	    in_cu_header_offset, 1, out_cu_die_offset, error));
}

int
dwarf_get_address_size(Dwarf_Debug dbg, Dwarf_Half *addr_size,
    Dwarf_Error *error)
{

	if (dbg == NULL || addr_size == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*addr_size = dbg->dbg_pointer_size;

	return (DW_DLV_OK);
}

Dwarf_Bool
dwarf_get_die_infotypes_flag(Dwarf_Die die)
{

	assert(die != NULL);

	return (die->die_cu->cu_is_info);
}
