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

ELFTC_VCSID("$Id: dwarf_lineno.c 2983 2014-02-09 00:24:31Z kaiwang27 $");

int
dwarf_srclines(Dwarf_Die die, Dwarf_Line **linebuf, Dwarf_Signed *linecount,
    Dwarf_Error *error)
{
	Dwarf_LineInfo li;
	Dwarf_Debug dbg;
	Dwarf_Line ln;
	Dwarf_CU cu;
	Dwarf_Attribute at; 
	int i;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || linebuf == NULL || linecount == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if ((at = _dwarf_attr_find(die, DW_AT_stmt_list)) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	cu = die->die_cu;
	if (cu->cu_lineinfo == NULL) {
		if (_dwarf_lineno_init(die, at->u[0].u64, error) !=
		    DW_DLE_NONE)
			return (DW_DLV_ERROR);
	}
	if (cu->cu_lineinfo == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	li = cu->cu_lineinfo;
	*linecount = (Dwarf_Signed) li->li_lnlen;

	if (*linecount == 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	if (li->li_lnarray != NULL) {
		*linebuf = li->li_lnarray;
		return (DW_DLV_OK);
	}

	if ((li->li_lnarray = malloc(*linecount * sizeof(Dwarf_Line))) ==
	    NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_ERROR);
	}

	for (i = 0, ln = STAILQ_FIRST(&li->li_lnlist);
	     i < *linecount && ln != NULL; i++, ln = STAILQ_NEXT(ln, ln_next))
		li->li_lnarray[i] = ln;

	*linebuf = li->li_lnarray;

	return (DW_DLV_OK);
}

int
dwarf_srcfiles(Dwarf_Die die, char ***srcfiles, Dwarf_Signed *srccount,
    Dwarf_Error *error)
{
	Dwarf_LineInfo li;
	Dwarf_LineFile lf;
	Dwarf_Debug dbg;
	Dwarf_CU cu;
	Dwarf_Attribute at; 
	int i;

	dbg = die != NULL ? die->die_dbg : NULL;

	if (die == NULL || srcfiles == NULL || srccount == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if ((at = _dwarf_attr_find(die, DW_AT_stmt_list)) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	cu = die->die_cu;
	if (cu->cu_lineinfo == NULL) {
		if (_dwarf_lineno_init(die, at->u[0].u64, error) !=
		    DW_DLE_NONE)
			return (DW_DLV_ERROR);
	}
	if (cu->cu_lineinfo == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	li = cu->cu_lineinfo;
	*srccount = (Dwarf_Signed) li->li_lflen;

	if (*srccount == 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_NO_ENTRY);
		return (DW_DLV_NO_ENTRY);
	}

	if (li->li_lfnarray != NULL) {
		*srcfiles = li->li_lfnarray;
		return (DW_DLV_OK);
	}

	if ((li->li_lfnarray = malloc(*srccount * sizeof(char *))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_ERROR);
	}

	for (i = 0, lf = STAILQ_FIRST(&li->li_lflist);
	     i < *srccount && lf != NULL; i++, lf = STAILQ_NEXT(lf, lf_next)) {
		if (lf->lf_fullpath)
			li->li_lfnarray[i] = lf->lf_fullpath;
		else
			li->li_lfnarray[i] = lf->lf_fname;
	}

	*srcfiles = li->li_lfnarray;

	return (DW_DLV_OK);
}

int
dwarf_linebeginstatement(Dwarf_Line ln, Dwarf_Bool *ret_bool,
    Dwarf_Error *error)
{

	if (ln == NULL || ret_bool == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*ret_bool = ln->ln_stmt;

	return (DW_DLV_OK);
}

int
dwarf_lineendsequence(Dwarf_Line ln, Dwarf_Bool *ret_bool, Dwarf_Error *error)
{

	if (ln == NULL || ret_bool == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*ret_bool = ln->ln_endseq;

	return (DW_DLV_OK);
}

int
dwarf_lineno(Dwarf_Line ln, Dwarf_Unsigned *ret_lineno, Dwarf_Error *error)
{

	if (ln == NULL || ret_lineno == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*ret_lineno = ln->ln_lineno;

	return (DW_DLV_OK);
}

int
dwarf_line_srcfileno(Dwarf_Line ln, Dwarf_Unsigned *ret_fileno,
    Dwarf_Error *error)
{

	if (ln == NULL || ret_fileno == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*ret_fileno = ln->ln_fileno;

	return (DW_DLV_OK);
}

int
dwarf_lineaddr(Dwarf_Line ln, Dwarf_Addr *ret_lineaddr, Dwarf_Error *error)
{

	if (ln == NULL || ret_lineaddr == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*ret_lineaddr = ln->ln_addr;

	return (DW_DLV_OK);
}

int
dwarf_lineoff(Dwarf_Line ln, Dwarf_Signed *ret_lineoff, Dwarf_Error *error)
{

	if (ln == NULL || ret_lineoff == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (ln->ln_column == 0)
		*ret_lineoff = -1;
	else
		*ret_lineoff = (Dwarf_Signed) ln->ln_column;

	return (DW_DLV_OK);
}

int
dwarf_linesrc(Dwarf_Line ln, char **ret_linesrc, Dwarf_Error *error)
{
	Dwarf_LineInfo li;
	Dwarf_LineFile lf;
	int i;

	if (ln == NULL || ret_linesrc == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	li = ln->ln_li;
	assert(li != NULL);

	for (i = 1, lf = STAILQ_FIRST(&li->li_lflist);
	     (Dwarf_Unsigned) i < ln->ln_fileno && lf != NULL;
	     i++, lf = STAILQ_NEXT(lf, lf_next))
		;

	if (lf == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_LINE_FILE_NUM_BAD);
		return (DW_DLV_ERROR);
	}

	if (lf->lf_fullpath) {
		*ret_linesrc = (char *) lf->lf_fullpath;
		return (DW_DLV_OK);
	}

	*ret_linesrc = lf->lf_fname;

	return (DW_DLV_OK);
}

int
dwarf_lineblock(Dwarf_Line ln, Dwarf_Bool *ret_bool, Dwarf_Error *error)
{

	if (ln == NULL || ret_bool == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*ret_bool = ln->ln_bblock;

	return (DW_DLV_OK);
}
