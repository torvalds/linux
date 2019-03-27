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

ELFTC_VCSID("$Id: dwarf_pro_lineno.c 2973 2013-12-23 06:46:16Z kaiwang27 $");

Dwarf_Unsigned
dwarf_add_line_entry(Dwarf_P_Debug dbg, Dwarf_Unsigned file,
    Dwarf_Addr off, Dwarf_Unsigned lineno, Dwarf_Signed column,
    Dwarf_Bool is_stmt, Dwarf_Bool basic_block, Dwarf_Error *error)
{
	Dwarf_LineInfo li;
	Dwarf_Line ln;

	if (dbg == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	li = dbg->dbgp_lineinfo;

	ln = STAILQ_LAST(&li->li_lnlist, _Dwarf_Line, ln_next);

	if (ln == NULL || ln->ln_addr > off) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	if ((ln = calloc(1, sizeof(struct _Dwarf_Line))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_NOCOUNT);
	}
	ln->ln_li     = li;
	ln->ln_addr   = off;
	ln->ln_symndx = 0;
	ln->ln_fileno = file;
	ln->ln_lineno = lineno;
	ln->ln_column = column;
	ln->ln_bblock = basic_block != 0;
	ln->ln_stmt   = is_stmt != 0;
	ln->ln_endseq = 0;
	STAILQ_INSERT_TAIL(&li->li_lnlist, ln, ln_next);
	li->li_lnlen++;

	return (DW_DLV_OK);
}

Dwarf_Unsigned
dwarf_lne_set_address(Dwarf_P_Debug dbg, Dwarf_Addr offs, Dwarf_Unsigned symndx,
    Dwarf_Error *error)
{
	Dwarf_LineInfo li;
	Dwarf_Line ln;

	if (dbg == NULL || symndx == 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	li = dbg->dbgp_lineinfo;

	if ((ln = calloc(1, sizeof(struct _Dwarf_Line))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_NOCOUNT);
	}
	ln->ln_li = li;
	ln->ln_addr = offs;
	ln->ln_symndx = symndx;
	STAILQ_INSERT_TAIL(&li->li_lnlist, ln, ln_next);
	li->li_lnlen++;

	return (DW_DLV_OK);
}

Dwarf_Unsigned
dwarf_lne_end_sequence(Dwarf_P_Debug dbg, Dwarf_Addr addr, Dwarf_Error *error)
{
	Dwarf_LineInfo li;
	Dwarf_Line ln;

	if (dbg == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	li = dbg->dbgp_lineinfo;

	ln = STAILQ_LAST(&li->li_lnlist, _Dwarf_Line, ln_next);
	if (ln && ln->ln_addr >= addr) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	if ((ln = calloc(1, sizeof(struct _Dwarf_Line))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_NOCOUNT);
	}
	ln->ln_li = li;
	ln->ln_addr = addr;
	ln->ln_endseq = 1;
	STAILQ_INSERT_TAIL(&li->li_lnlist, ln, ln_next);
	li->li_lnlen++;

	return (DW_DLV_OK);
}

Dwarf_Unsigned
dwarf_add_directory_decl(Dwarf_P_Debug dbg, char *name, Dwarf_Error *error)
{
	Dwarf_LineInfo li;

	if (dbg == NULL || name == NULL || strlen(name) == 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	li = dbg->dbgp_lineinfo;

	li->li_incdirs = realloc(li->li_incdirs, (li->li_inclen + 1) *
	    sizeof(char *));
	if (li->li_incdirs == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_NOCOUNT);
	}
	if ((li->li_incdirs[li->li_inclen] = strdup(name)) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_NOCOUNT);
	}

	return (++li->li_inclen);
}

Dwarf_Unsigned
dwarf_add_file_decl(Dwarf_P_Debug dbg, char *name, Dwarf_Unsigned dirndx,
    Dwarf_Unsigned mtime, Dwarf_Unsigned size, Dwarf_Error *error)
{
	Dwarf_LineInfo li;
	Dwarf_LineFile lf;

	if (dbg == NULL || name == NULL || strlen(name) == 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	li = dbg->dbgp_lineinfo;

	if ((lf = malloc(sizeof(struct _Dwarf_LineFile))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	if ((lf->lf_fname = strdup(name)) == NULL) {
		free(lf);
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	lf->lf_dirndx = dirndx;
	lf->lf_mtime = mtime;
	lf->lf_size = size;
	STAILQ_INSERT_TAIL(&li->li_lflist, lf, lf_next);

	return (++li->li_lflen);
}
