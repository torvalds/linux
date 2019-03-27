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
 *
 * $Id: dwarf_pro_nametbl.m4 2074 2011-10-27 03:34:33Z jkoshy $
 */

define(`MAKE_NAMETBL_PRO_API',`
Dwarf_Unsigned
dwarf_add_$1name(Dwarf_P_Debug dbg, Dwarf_P_Die die, char *$1_name,
    Dwarf_Error *error)
{
	Dwarf_NameTbl nt;
	Dwarf_NamePair np;

	if (dbg == NULL || die == NULL || $1_name == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (0);
	}

	if (dbg->dbgp_$1s == NULL) {
		dbg->dbgp_$1s = calloc(1, sizeof(struct _Dwarf_NameTbl));
		if (dbg->dbgp_$1s == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (0);
		}
		STAILQ_INIT(&dbg->dbgp_$1s->nt_nplist);
	}

	nt = dbg->dbgp_$1s;

	if ((np = calloc(1, sizeof(struct _Dwarf_NamePair))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (0);
	}

	np->np_nt = nt;
	np->np_die = die;
	if ((np->np_name = strdup($1_name)) == NULL) {
		free(np);
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (0);
	}

	STAILQ_INSERT_TAIL(&nt->nt_nplist, np, np_next);

	return (1);
}
')
