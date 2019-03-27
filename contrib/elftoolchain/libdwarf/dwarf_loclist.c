/*-
 * Copyright (c) 2009,2014 Kai Wang
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

ELFTC_VCSID("$Id: dwarf_loclist.c 3066 2014-06-06 19:36:06Z kaiwang27 $");

static int
copy_locdesc(Dwarf_Debug dbg, Dwarf_Locdesc *dst, Dwarf_Locdesc *src,
    Dwarf_Error *error)
{

	assert(src != NULL && dst != NULL);

	dst->ld_lopc = src->ld_lopc;
	dst->ld_hipc = src->ld_hipc;
	dst->ld_cents = src->ld_cents;

	if (dst->ld_cents > 0) {
		dst->ld_s = calloc(dst->ld_cents, sizeof(Dwarf_Loc));
		if (dst->ld_s == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLE_MEMORY);
		}
		memcpy(dst->ld_s, src->ld_s, src->ld_cents *
		    sizeof(Dwarf_Loc));
	} else
		dst->ld_s = NULL;

	return (DW_DLE_NONE);
}

int
dwarf_loclist_n(Dwarf_Attribute at, Dwarf_Locdesc ***llbuf,
    Dwarf_Signed *listlen, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	int ret;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || llbuf == NULL || listlen == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	switch (at->at_attrib) {
	case DW_AT_location:
	case DW_AT_string_length:
	case DW_AT_return_addr:
	case DW_AT_data_member_location:
	case DW_AT_frame_base:
	case DW_AT_segment:
	case DW_AT_static_link:
	case DW_AT_use_location:
	case DW_AT_vtable_elem_location:
		switch (at->at_form) {
		case DW_FORM_data4:
		case DW_FORM_data8:
			/*
			 * DW_FORM_data[48] can not be used as section offset
			 * since DWARF4. For DWARF[23], the application needs
			 * to determine if DW_FORM_data[48] is representing
			 * a constant or a section offset.
			 */
			if (at->at_die->die_cu->cu_version >= 4) {
				DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
				return (DW_DLV_NO_ENTRY);
			}
			/* FALLTHROUGH */
		case DW_FORM_sec_offset:
			ret = _dwarf_loclist_find(dbg, at->at_die->die_cu,
			    at->u[0].u64, llbuf, listlen, NULL, error);
			if (ret == DW_DLE_NO_ENTRY) {
				DWARF_SET_ERROR(dbg, error, ret);
				return (DW_DLV_NO_ENTRY);
			}
			if (ret != DW_DLE_NONE)
				return (DW_DLV_ERROR);
			return (DW_DLV_OK);
		case DW_FORM_block:
		case DW_FORM_block1:
		case DW_FORM_block2:
		case DW_FORM_block4:
		case DW_FORM_exprloc:
			if (at->at_ld == NULL) {
				ret = _dwarf_loc_add(at->at_die, at, error);
				if (ret != DW_DLE_NONE)
					return (DW_DLV_ERROR);
			}
			*llbuf = calloc(1, sizeof(Dwarf_Locdesc *));
			if (*llbuf == NULL) {
				DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
				return (DW_DLV_ERROR);
			}
			(*llbuf)[0] = calloc(1, sizeof(Dwarf_Locdesc));
			if ((*llbuf)[0] == NULL) {
				free(*llbuf);
				DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
				return (DW_DLV_ERROR);
			}
			if (copy_locdesc(dbg, (*llbuf)[0], at->at_ld, error) !=
			    DW_DLE_NONE) {
				free((*llbuf)[0]);
				free(*llbuf);
				return (DW_DLV_ERROR);
			}
			*listlen = 1;
			return (DW_DLV_OK);
		default:
			/* Malformed Attr? */
			DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
			return (DW_DLV_NO_ENTRY);
		}
	default:
		/* Wrong attr supplied. */
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}
}

int
dwarf_loclist(Dwarf_Attribute at, Dwarf_Locdesc **llbuf,
    Dwarf_Signed *listlen, Dwarf_Error *error)
{
	Dwarf_Locdesc **_llbuf;
	int i, ret;

	ret = dwarf_loclist_n(at, &_llbuf, listlen, error);
	if (ret != DW_DLV_OK)
		return (ret);

	/* Only return the first location description of the list. */
	*llbuf = _llbuf[0];

	/* Free the rest of the list. */
	for (i = 1; i < *listlen; i++) {
		if (_llbuf[i]->ld_s)
			free(_llbuf[i]->ld_s);
		free(_llbuf[i]);
	}
	free(_llbuf);

	*listlen = 1;

	return (DW_DLV_OK);
}

int
dwarf_get_loclist_entry(Dwarf_Debug dbg, Dwarf_Unsigned offset,
    Dwarf_Addr *hipc, Dwarf_Addr *lopc, Dwarf_Ptr *data,
    Dwarf_Unsigned *entry_len, Dwarf_Unsigned *next_entry,
    Dwarf_Error *error)
{
	Dwarf_Locdesc *ld, **llbuf;
	Dwarf_Section *ds;
	Dwarf_Signed listlen;
	int i, ret;

	/*
	 * Note that this API sometimes will not work correctly because
	 * it assumes that all units have the same pointer size and offset
	 * size.
	 */

	if (dbg == NULL || hipc == NULL || lopc == NULL || data == NULL ||
	    entry_len == NULL || next_entry == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	ret = _dwarf_loclist_find(dbg, STAILQ_FIRST(&dbg->dbg_cu), offset,
	    &llbuf, &listlen, entry_len, error);
	if (ret == DW_DLE_NO_ENTRY) {
		DWARF_SET_ERROR(dbg, error, DW_DLV_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	} else if (ret != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	*hipc = *lopc = 0;
	for (i = 0; i < listlen; i++) {
		ld = llbuf[i];
		if (i == 0) {
			*hipc = ld->ld_hipc;
			*lopc = ld->ld_lopc;
		} else {
			if (ld->ld_lopc < *lopc)
				*lopc = ld->ld_lopc;
			if (ld->ld_hipc > *hipc)
				*hipc = ld->ld_hipc;
		}
	}

	ds = _dwarf_find_section(dbg, ".debug_loc");
	assert(ds != NULL);
	*data = (uint8_t *) ds->ds_data + offset;
	*next_entry = offset + *entry_len;

	return (DW_DLV_OK);
}

int
dwarf_loclist_from_expr(Dwarf_Debug dbg, Dwarf_Ptr bytes_in,
    Dwarf_Unsigned bytes_len, Dwarf_Locdesc **llbuf, Dwarf_Signed *listlen,
    Dwarf_Error *error)
{

	return (dwarf_loclist_from_expr_a(dbg, bytes_in, bytes_len,
	    dbg->dbg_pointer_size, llbuf, listlen, error));
}

int
dwarf_loclist_from_expr_a(Dwarf_Debug dbg, Dwarf_Ptr bytes_in,
    Dwarf_Unsigned bytes_len, Dwarf_Half addr_size, Dwarf_Locdesc **llbuf,
    Dwarf_Signed *listlen, Dwarf_Error *error)
{
	Dwarf_Half offset_size;
	Dwarf_Small version;

	/*
	 * Obtain offset size and DWARF version from the current
	 * Compilation Unit or Type Unit. These values are needed
	 * for correctly parsing DW_OP_GNU_implicit_pointer operator.
	 *
	 * Note that dwarf_loclist_from_expr_b() should be used instead
	 * if the application knows correct values for offset size
	 * and DWARF version.
	 */
	if (dbg->dbg_cu_current) {
		offset_size = dbg->dbg_cu_current->cu_length_size == 4 ? 4 : 8;
		version = dbg->dbg_cu_current->cu_version;
	} else if (dbg->dbg_tu_current) {
		offset_size = dbg->dbg_tu_current->cu_length_size == 4 ? 4 : 8;
		version = dbg->dbg_tu_current->cu_version;
	} else {
		/* Default values if no CU/TU context. */
		offset_size = 4;
		version = 2;	/* DWARF2 */
	}

	return (dwarf_loclist_from_expr_b(dbg, bytes_in, bytes_len, addr_size,
	    offset_size, version, llbuf, listlen, error));
}

int
dwarf_loclist_from_expr_b(Dwarf_Debug dbg, Dwarf_Ptr bytes_in,
    Dwarf_Unsigned bytes_len, Dwarf_Half addr_size, Dwarf_Half offset_size,
    Dwarf_Small version, Dwarf_Locdesc **llbuf, Dwarf_Signed *listlen,
    Dwarf_Error *error)
{
	Dwarf_Locdesc *ld;
	int ret;

	if (dbg == NULL || bytes_in == NULL || bytes_len == 0 ||
	    llbuf == NULL || listlen == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (addr_size != 4 && addr_size != 8) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (offset_size != 4 && offset_size != 8) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	ret = _dwarf_loc_fill_locexpr(dbg, &ld, bytes_in, bytes_len, addr_size,
	    offset_size, version, error);
	if (ret != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	*llbuf = ld;
	*listlen = 1;

	return (DW_DLV_OK);
}
