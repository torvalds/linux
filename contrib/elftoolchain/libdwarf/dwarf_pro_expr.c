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

ELFTC_VCSID("$Id: dwarf_pro_expr.c 2074 2011-10-27 03:34:33Z jkoshy $");

static struct _Dwarf_P_Expr_Entry *
_dwarf_add_expr(Dwarf_P_Expr expr, Dwarf_Small opcode, Dwarf_Unsigned val1,
    Dwarf_Unsigned val2, Dwarf_Error *error)
{
	struct _Dwarf_P_Expr_Entry *ee;
	Dwarf_Debug dbg;
	int len;

	dbg = expr != NULL ? expr->pe_dbg : NULL;

	if (_dwarf_loc_expr_add_atom(expr->pe_dbg, NULL, NULL, opcode, val1,
	    val2, &len, error) != DW_DLE_NONE)
		return (NULL);
	assert(len > 0);

	if ((ee = calloc(1, sizeof(*ee))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (NULL);
	}

	STAILQ_INSERT_TAIL(&expr->pe_eelist, ee, ee_next);

	ee->ee_loc.lr_atom = opcode;
	ee->ee_loc.lr_number = val1;
	ee->ee_loc.lr_number2 = val2;
	ee->ee_loc.lr_offset = expr->pe_length;
	expr->pe_length += len;
	expr->pe_invalid = 1;

	return (ee);
}

int
_dwarf_expr_into_block(Dwarf_P_Expr expr, Dwarf_Error *error)
{
	struct _Dwarf_P_Expr_Entry *ee;
	Dwarf_Debug dbg;
	int len, pos, ret;

	dbg = expr != NULL ? expr->pe_dbg : NULL;

	if (expr->pe_block != NULL) {
		free(expr->pe_block);
		expr->pe_block = NULL;
	}

	if (expr->pe_length <= 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_EXPR_LENGTH_BAD);
		return (DW_DLE_EXPR_LENGTH_BAD);
	}


	if ((expr->pe_block = calloc((size_t) expr->pe_length, 1)) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	pos = 0;
	STAILQ_FOREACH(ee, &expr->pe_eelist, ee_next) {
		assert((Dwarf_Unsigned) pos < expr->pe_length);
		ret = _dwarf_loc_expr_add_atom(expr->pe_dbg,
		    &expr->pe_block[pos], &expr->pe_block[expr->pe_length],
		    ee->ee_loc.lr_atom, ee->ee_loc.lr_number,
		    ee->ee_loc.lr_number2, &len, error);
		assert(ret == DW_DLE_NONE);
		assert(len > 0);
		pos += len;
	}

	expr->pe_invalid = 0;

	return (DW_DLE_NONE);
}

void
_dwarf_expr_cleanup(Dwarf_P_Debug dbg)
{
	Dwarf_P_Expr pe, tpe;
	struct _Dwarf_P_Expr_Entry *ee, *tee;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_WRITE);

	STAILQ_FOREACH_SAFE(pe, &dbg->dbgp_pelist, pe_next, tpe) {
		STAILQ_REMOVE(&dbg->dbgp_pelist, pe, _Dwarf_P_Expr, pe_next);
		STAILQ_FOREACH_SAFE(ee, &pe->pe_eelist, ee_next, tee) {
			STAILQ_REMOVE(&pe->pe_eelist, ee, _Dwarf_P_Expr_Entry,
			    ee_next);
			free(ee);
		}
		if (pe->pe_block)
			free(pe->pe_block);
		free(pe);
	}
}

Dwarf_P_Expr
dwarf_new_expr(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	Dwarf_P_Expr pe;

	if (dbg == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if ((pe = calloc(1, sizeof(struct _Dwarf_P_Expr))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_BADADDR);
	}
	STAILQ_INIT(&pe->pe_eelist);

	STAILQ_INSERT_TAIL(&dbg->dbgp_pelist, pe, pe_next);
	pe->pe_dbg = dbg;

	return (pe);
}

Dwarf_Unsigned
dwarf_add_expr_gen(Dwarf_P_Expr expr, Dwarf_Small opcode, Dwarf_Unsigned val1,
    Dwarf_Unsigned val2, Dwarf_Error *error)
{

	if (expr == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	if (_dwarf_add_expr(expr, opcode, val1, val2, error) == NULL)
		return (DW_DLV_NOCOUNT);

	return (expr->pe_length);
}

Dwarf_Unsigned
dwarf_add_expr_addr(Dwarf_P_Expr expr, Dwarf_Unsigned address,
    Dwarf_Signed sym_index, Dwarf_Error *error)
{

	return (dwarf_add_expr_addr_b(expr, address, sym_index, error));
}

Dwarf_Unsigned
dwarf_add_expr_addr_b(Dwarf_P_Expr expr, Dwarf_Unsigned address,
    Dwarf_Unsigned sym_index, Dwarf_Error *error)
{
	struct _Dwarf_P_Expr_Entry *ee;

	if (expr == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	if ((ee = _dwarf_add_expr(expr, DW_OP_addr, address, 0, error)) == NULL)
		return (DW_DLV_NOCOUNT);

	ee->ee_sym = sym_index;

	return (expr->pe_length);
}

Dwarf_Unsigned
dwarf_expr_current_offset(Dwarf_P_Expr expr, Dwarf_Error *error)
{

	if (expr == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	return (expr->pe_length);
}

Dwarf_Addr
dwarf_expr_into_block(Dwarf_P_Expr expr, Dwarf_Unsigned *length,
    Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = expr != NULL ? expr->pe_dbg : NULL;

	if (expr == NULL || length == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return ((Dwarf_Addr) (uintptr_t) DW_DLV_BADADDR);
	}

	if (expr->pe_block == NULL || expr->pe_invalid)
		if (_dwarf_expr_into_block(expr, error) != DW_DLE_NONE)
			return ((Dwarf_Addr) (uintptr_t) DW_DLV_BADADDR);

	*length = expr->pe_length;

	return ((Dwarf_Addr) (uintptr_t) expr->pe_block);
}
