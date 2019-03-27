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

ELFTC_VCSID("$Id: libdwarf_ranges.c 2972 2013-12-23 06:46:04Z kaiwang27 $");

static int
_dwarf_ranges_parse(Dwarf_Debug dbg, Dwarf_CU cu, Dwarf_Section *ds,
    uint64_t off, Dwarf_Ranges *rg, Dwarf_Unsigned *cnt)
{
	Dwarf_Unsigned start, end;
	int i;

	i = 0;
	while (off < ds->ds_size) {

		start = dbg->read(ds->ds_data, &off, cu->cu_pointer_size);
		end = dbg->read(ds->ds_data, &off, cu->cu_pointer_size);

		if (rg != NULL) {
			rg[i].dwr_addr1 = start;
			rg[i].dwr_addr2 = end;
			if (start == 0 && end == 0)
				rg[i].dwr_type = DW_RANGES_END;
			else if ((start == ~0U && cu->cu_pointer_size == 4) ||
			    (start == ~0ULL && cu->cu_pointer_size == 8))
				rg[i].dwr_type = DW_RANGES_ADDRESS_SELECTION;
			else
				rg[i].dwr_type = DW_RANGES_ENTRY;
		}

		i++;

		if (start == 0 && end == 0)
			break;
	}

	if (cnt != NULL)
		*cnt = i;

	return (DW_DLE_NONE);
}

int
_dwarf_ranges_find(Dwarf_Debug dbg, uint64_t off, Dwarf_Rangelist *ret_rl)
{
	Dwarf_Rangelist rl;

	STAILQ_FOREACH(rl, &dbg->dbg_rllist, rl_next)
		if (rl->rl_offset == off)
			break;

	if (rl == NULL)
		return (DW_DLE_NO_ENTRY);

	if (ret_rl != NULL)
		*ret_rl = rl;

	return (DW_DLE_NONE);
}

void
_dwarf_ranges_cleanup(Dwarf_Debug dbg)
{
	Dwarf_Rangelist rl, trl;

	if (STAILQ_EMPTY(&dbg->dbg_rllist))
		return;

	STAILQ_FOREACH_SAFE(rl, &dbg->dbg_rllist, rl_next, trl) {
		STAILQ_REMOVE(&dbg->dbg_rllist, rl, _Dwarf_Rangelist, rl_next);
		if (rl->rl_rgarray)
			free(rl->rl_rgarray);
		free(rl);
	}
}

int
_dwarf_ranges_add(Dwarf_Debug dbg, Dwarf_CU cu, uint64_t off,
    Dwarf_Rangelist *ret_rl, Dwarf_Error *error)
{
	Dwarf_Section *ds;
	Dwarf_Rangelist rl;
	Dwarf_Unsigned cnt;
	int ret;

	if ((ds = _dwarf_find_section(dbg, ".debug_ranges")) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLE_NO_ENTRY);
	}

	if ((rl = malloc(sizeof(struct _Dwarf_Rangelist))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	rl->rl_offset = off;

	ret = _dwarf_ranges_parse(dbg, cu, ds, off, NULL, &cnt);
	if (ret != DW_DLE_NONE) {
		free(rl);
		return (ret);
	}

	rl->rl_rglen = cnt;
	if (cnt != 0) {
		if ((rl->rl_rgarray = calloc(cnt, sizeof(Dwarf_Ranges))) ==
		    NULL) {
			free(rl);
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLE_MEMORY);
		}

		ret = _dwarf_ranges_parse(dbg, cu, ds, off, rl->rl_rgarray,
		    NULL);
		if (ret != DW_DLE_NONE) {
			free(rl->rl_rgarray);
			free(rl);
			return (ret);
		}
	} else
		rl->rl_rgarray = NULL;

	STAILQ_INSERT_TAIL(&dbg->dbg_rllist, rl, rl_next);
	*ret_rl = rl;

	return (DW_DLE_NONE);
}
