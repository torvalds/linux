/*-
 * Copyright (c) 2014 Kai Wang
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

ELFTC_VCSID("$Id: dwarf_sections.c 3226 2015-06-23 13:00:16Z emaste $");

#define	SET(N, V)				\
	do {					\
		if ((N) != NULL)		\
			*(N) = (V);		\
	} while (0)

int
dwarf_get_section_max_offsets_b(Dwarf_Debug dbg, Dwarf_Unsigned *debug_info,
    Dwarf_Unsigned *debug_abbrev, Dwarf_Unsigned *debug_line,
    Dwarf_Unsigned *debug_loc, Dwarf_Unsigned *debug_aranges,
    Dwarf_Unsigned *debug_macinfo, Dwarf_Unsigned *debug_pubnames,
    Dwarf_Unsigned *debug_str, Dwarf_Unsigned *debug_frame,
    Dwarf_Unsigned *debug_ranges, Dwarf_Unsigned *debug_pubtypes,
    Dwarf_Unsigned *debug_types)
{
	const char *n;
	Dwarf_Unsigned sz;
	int i;

	if (dbg == NULL)
		return (DW_DLV_ERROR);

	SET(debug_info, 0);
	SET(debug_abbrev, 0);
	SET(debug_line, 0);
	SET(debug_loc, 0);
	SET(debug_aranges, 0);
	SET(debug_macinfo, 0);
	SET(debug_pubnames, 0);
	SET(debug_str, 0);
	SET(debug_frame, 0);
	SET(debug_ranges, 0);
	SET(debug_pubtypes, 0);
	SET(debug_types, 0);

	for (i = 0; (Dwarf_Unsigned) i < dbg->dbg_seccnt; i++) {
		n = dbg->dbg_section[i].ds_name;
		sz = dbg->dbg_section[i].ds_size;
		if (!strcmp(n, ".debug_info"))
			SET(debug_info, sz);
		else if (!strcmp(n, ".debug_abbrev"))
			SET(debug_abbrev, sz);
		else if (!strcmp(n, ".debug_line"))
			SET(debug_line, sz);
		else if (!strcmp(n, ".debug_loc"))
			SET(debug_loc, sz);
		else if (!strcmp(n, ".debug_aranges"))
			SET(debug_aranges, sz);
		else if (!strcmp(n, ".debug_macinfo"))
			SET(debug_macinfo, sz);
		else if (!strcmp(n, ".debug_pubnames"))
			SET(debug_pubnames, sz);
		else if (!strcmp(n, ".debug_str"))
			SET(debug_str, sz);
		else if (!strcmp(n, ".debug_frame"))
			SET(debug_frame, sz);
		else if (!strcmp(n, ".debug_ranges"))
			SET(debug_ranges, sz);
		else if (!strcmp(n, ".debug_pubtypes"))
			SET(debug_pubtypes, sz);
		else if (!strcmp(n, ".debug_types"))
			SET(debug_types, sz);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_section_max_offsets(Dwarf_Debug dbg, Dwarf_Unsigned *debug_info,
    Dwarf_Unsigned *debug_abbrev, Dwarf_Unsigned *debug_line,
    Dwarf_Unsigned *debug_loc, Dwarf_Unsigned *debug_aranges,
    Dwarf_Unsigned *debug_macinfo, Dwarf_Unsigned *debug_pubnames,
    Dwarf_Unsigned *debug_str, Dwarf_Unsigned *debug_frame,
    Dwarf_Unsigned *debug_ranges, Dwarf_Unsigned *debug_pubtypes)
{

	return (dwarf_get_section_max_offsets_b(dbg, debug_info, debug_abbrev,
	    debug_line, debug_loc, debug_aranges, debug_macinfo,
	    debug_pubnames, debug_str, debug_frame, debug_ranges,
	    debug_pubtypes, NULL));
}
