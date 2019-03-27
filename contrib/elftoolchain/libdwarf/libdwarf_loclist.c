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

ELFTC_VCSID("$Id: libdwarf_loclist.c 3061 2014-06-02 00:42:41Z kaiwang27 $");

static int
_dwarf_loclist_add_locdesc(Dwarf_Debug dbg, Dwarf_CU cu, Dwarf_Section *ds,
    Dwarf_Unsigned *off, Dwarf_Locdesc **ld, Dwarf_Signed *ldlen,
    Dwarf_Unsigned *total_len, Dwarf_Error *error)
{
	uint64_t start, end;
	int i, len, ret;

	if (total_len != NULL)
		*total_len = 0;

	for (i = 0; *off < ds->ds_size; i++) {
		start = dbg->read(ds->ds_data, off, cu->cu_pointer_size);
		end = dbg->read(ds->ds_data, off, cu->cu_pointer_size);
		if (ld != NULL) {
			ld[i]->ld_lopc = start;
			ld[i]->ld_hipc = end;
		}

		if (total_len != NULL)
			*total_len += 2 * cu->cu_pointer_size;

		/* Check if it is the end entry. */
		if (start == 0 && end ==0) {
			i++;
			break;
		}

		/* Check if it is base-select entry. */
		if ((cu->cu_pointer_size == 4 && start == ~0U) ||
		    (cu->cu_pointer_size == 8 && start == ~0ULL))
			continue;

		/* Otherwise it's normal entry. */
		len = dbg->read(ds->ds_data, off, 2);
		if (*off + len > ds->ds_size) {
			DWARF_SET_ERROR(dbg, error,
			    DW_DLE_DEBUG_LOC_SECTION_SHORT);
			return (DW_DLE_DEBUG_LOC_SECTION_SHORT);
		}

		if (total_len != NULL)
			*total_len += len;

		if (ld != NULL) {
			ret = _dwarf_loc_fill_locdesc(dbg, ld[i],
			    ds->ds_data + *off, len, cu->cu_pointer_size,
			    cu->cu_length_size == 4 ? 4 : 8, cu->cu_version,
			    error);
			if (ret != DW_DLE_NONE)
				return (ret);
		}

		*off += len;
	}

	if (ldlen != NULL)
		*ldlen = i;

	return (DW_DLE_NONE);
}

int
_dwarf_loclist_find(Dwarf_Debug dbg, Dwarf_CU cu, uint64_t lloff,
    Dwarf_Locdesc ***ret_llbuf, Dwarf_Signed *listlen,
    Dwarf_Unsigned *entry_len, Dwarf_Error *error)
{
	Dwarf_Locdesc **llbuf;
	Dwarf_Section *ds;
	Dwarf_Signed ldlen;
	Dwarf_Unsigned off;
	int i, ret;

	if ((ds = _dwarf_find_section(dbg, ".debug_loc")) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLE_NO_ENTRY);
	}

	if (lloff >= ds->ds_size) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLE_NO_ENTRY);
	}

	/* Get the number of locdesc the first round. */
	off = lloff;
	ret = _dwarf_loclist_add_locdesc(dbg, cu, ds, &off, NULL, &ldlen,
	    NULL, error);
	if (ret != DW_DLE_NONE)
		return (ret);

	if (ldlen == 0)
		return (DW_DLE_NO_ENTRY);

	/*
	 * Dwarf_Locdesc list memory is allocated in this way (one more level
	 * of indirect) to make the loclist API be compatible with SGI libdwarf.
	 */
	if ((llbuf = calloc(ldlen, sizeof(Dwarf_Locdesc *))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	for (i = 0; i < ldlen; i++) {
		if ((llbuf[i] = calloc(1, sizeof(Dwarf_Locdesc))) == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			ret = DW_DLE_MEMORY;
			goto fail_cleanup;
		}
	}

	off = lloff;

	/* Fill in locdesc. */
	ret = _dwarf_loclist_add_locdesc(dbg, cu, ds, &off, llbuf, NULL,
	    entry_len, error);
	if (ret != DW_DLE_NONE)
		goto fail_cleanup;

	*ret_llbuf = llbuf;
	*listlen = ldlen;

	return (DW_DLE_NONE);

fail_cleanup:

	if (llbuf != NULL) {
		for (i = 0; i < ldlen; i++) {
			if (llbuf[i]->ld_s)
				free(llbuf[i]->ld_s);
			free(llbuf[i]);
		}
		free(llbuf);
	}

	return (ret);
}
