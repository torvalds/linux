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

ELFTC_VCSID("$Id: dwarf_macinfo.c 2074 2011-10-27 03:34:33Z jkoshy $");

char *
dwarf_find_macro_value_start(char *macro_string)
{
	char *p;

	if (macro_string == NULL)
		return (NULL);

	p = macro_string;
	while (*p != '\0' && *p != ' ')
		p++;
	if (*p == ' ')
		p++;

	return (p);
}

int
dwarf_get_macro_details(Dwarf_Debug dbg, Dwarf_Off offset,
    Dwarf_Unsigned max_count, Dwarf_Signed *entry_cnt,
    Dwarf_Macro_Details **details, Dwarf_Error *error)
{
	Dwarf_MacroSet ms;
	Dwarf_Unsigned cnt;
	int i;

	if (dbg == NULL || entry_cnt == NULL || details == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (STAILQ_EMPTY(&dbg->dbg_mslist)) {
		if (_dwarf_macinfo_init(dbg, error) != DW_DLE_NONE)
			return (DW_DLV_ERROR);
		if (STAILQ_EMPTY(&dbg->dbg_mslist)) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
			return (DW_DLV_NO_ENTRY);
		}
	}

	STAILQ_FOREACH(ms, &dbg->dbg_mslist, ms_next) {
		for (i = 0; (Dwarf_Unsigned) i < ms->ms_cnt; i++)
			if (ms->ms_mdlist[i].dmd_offset == offset) {
				cnt = ms->ms_cnt - i;
				if (max_count != 0 && cnt > max_count)
					cnt = max_count;

				*details = &ms->ms_mdlist[i];
				*entry_cnt = cnt;

				return (DW_DLV_OK);
			}
	}

	DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);

	return (DW_DLV_NO_ENTRY);
}
