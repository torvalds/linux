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

ELFTC_VCSID("$Id: dwarf_dealloc.c 2073 2011-10-27 03:30:47Z jkoshy $");

void
dwarf_dealloc(Dwarf_Debug dbg, Dwarf_Ptr p, Dwarf_Unsigned alloc_type)
{
	Dwarf_Abbrev ab;
	Dwarf_AttrDef ad, tad;
	Dwarf_Attribute at, tat;
	Dwarf_Die die;

	/*
	 * This libdwarf implementation does not use the SGI/libdwarf
	 * style of memory allocation. In most cases it does not copy
	 * things to return to the client, so the client does not need
	 * to remember to free them.  The remaining cases are handled
	 * below.
	 */

	(void) dbg;

	if (alloc_type == DW_DLA_LIST || alloc_type == DW_DLA_FRAME_BLOCK ||
	    alloc_type == DW_DLA_LOC_BLOCK || alloc_type == DW_DLA_LOCDESC)
		free(p);
	else if (alloc_type == DW_DLA_ABBREV) {
		ab = p;
		STAILQ_FOREACH_SAFE(ad, &ab->ab_attrdef, ad_next, tad) {
			STAILQ_REMOVE(&ab->ab_attrdef, ad, _Dwarf_AttrDef,
			    ad_next);
			free(ad);
		}
		free(ab);
	} else if (alloc_type == DW_DLA_DIE) {
		die = p;
		STAILQ_FOREACH_SAFE(at, &die->die_attr, at_next, tat) {
			STAILQ_REMOVE(&die->die_attr, at,
			    _Dwarf_Attribute, at_next);
			if (at->at_ld != NULL)
				free(at->at_ld);
			free(at);
		}
		if (die->die_attrarray)
			free(die->die_attrarray);
		free(die);
	}
}

void
dwarf_srclines_dealloc(Dwarf_Debug dbg, Dwarf_Line *linebuf,
	Dwarf_Signed count)
{
	/*
	 * In this libdwarf implementation, line information remains
	 * associated with the DIE for a compilation unit for the
	 * lifetime of the DIE.  The client does not need to free
	 * the memory returned by `dwarf_srclines()`.
	 */ 

	(void) dbg; (void) linebuf; (void) count;
}

void
dwarf_ranges_dealloc(Dwarf_Debug dbg, Dwarf_Ranges *ranges,
    Dwarf_Signed range_count)
{
	/*
	 * In this libdwarf implementation, ranges information is
	 * kept by a STAILQ inside Dwarf_Debug object. The client
	 * does not need to free the memory returned by
	 * `dwarf_get_ranges()` or `dwarf_get_ranges_a()`.
	 */

	(void) dbg; (void) ranges; (void) range_count;
}

void
dwarf_fde_cie_list_dealloc(Dwarf_Debug dbg, Dwarf_Cie *cie_list,
    Dwarf_Signed cie_count, Dwarf_Fde *fde_list, Dwarf_Signed fde_count)
{
	/*
	 * In this implementation, FDE and CIE information is managed
	 * as part of the Dwarf_Debug object.  The client does not need
	 * to explicitly free these memory arenas.
	 */
	(void) dbg;
	(void) cie_list;
	(void) cie_count;
	(void) fde_list;
	(void) fde_count;
}
