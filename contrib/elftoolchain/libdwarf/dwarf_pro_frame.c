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

ELFTC_VCSID("$Id: dwarf_pro_frame.c 2074 2011-10-27 03:34:33Z jkoshy $");

Dwarf_P_Fde
dwarf_new_fde(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	Dwarf_P_Fde fde;

	if (dbg == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if ((fde = calloc(1, sizeof(struct _Dwarf_Fde))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLV_BADADDR);
	}

	fde->fde_dbg = dbg;

	return (fde);
}

Dwarf_Unsigned
dwarf_add_frame_cie(Dwarf_P_Debug dbg, char *augmenter, Dwarf_Small caf,
    Dwarf_Small daf, Dwarf_Small ra, Dwarf_Ptr initinst,
    Dwarf_Unsigned inst_len, Dwarf_Error *error)
{
	Dwarf_P_Cie cie;

	if (dbg == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	if ((cie = calloc(1, sizeof(struct _Dwarf_Cie))) == NULL) {
		DWARF_SET_ERROR(dbg, error,DW_DLE_MEMORY);
		return (DW_DLV_NOCOUNT);
	}
	STAILQ_INSERT_TAIL(&dbg->dbgp_cielist, cie, cie_next);

	cie->cie_index = dbg->dbgp_cielen++;

	if (augmenter != NULL) {
		cie->cie_augment = (uint8_t *) strdup(augmenter);
		if (cie->cie_augment == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLV_NOCOUNT);
		}
	}

	cie->cie_caf = caf;
	cie->cie_daf = (int8_t) daf; /* daf is signed. */
	cie->cie_ra = ra;
	if (initinst != NULL && inst_len > 0) {
		cie->cie_initinst = malloc((size_t) inst_len);
		if (cie->cie_initinst == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLV_NOCOUNT);
		}
		memcpy(cie->cie_initinst, initinst, inst_len);
		cie->cie_instlen = inst_len;
	}

	return (cie->cie_index);
}

Dwarf_Unsigned
dwarf_add_frame_fde(Dwarf_P_Debug dbg, Dwarf_P_Fde fde, Dwarf_P_Die die,
    Dwarf_Unsigned cie, Dwarf_Addr virt_addr, Dwarf_Unsigned code_len,
    Dwarf_Unsigned symbol_index, Dwarf_Error *error)
{

	return (dwarf_add_frame_fde_b(dbg, fde, die, cie, virt_addr, code_len,
	    symbol_index, 0, 0, error));
}

Dwarf_Unsigned
dwarf_add_frame_fde_b(Dwarf_P_Debug dbg, Dwarf_P_Fde fde, Dwarf_P_Die die,
    Dwarf_Unsigned cie, Dwarf_Addr virt_addr, Dwarf_Unsigned code_len,
    Dwarf_Unsigned symbol_index, Dwarf_Unsigned end_symbol_index,
    Dwarf_Addr offset_from_end_sym, Dwarf_Error *error)
{
	Dwarf_P_Cie ciep;
	int i;

	/*
	 * XXX SGI libdwarf need the DIE arg because later it will insert a
	 * DW_AT_MIPS_fde attribute, which points to the offset the
	 * correspoding FDE, into this DIE. Do we need this?
	 */
	(void) die;

	if (dbg == NULL || fde == NULL || fde->fde_dbg != dbg) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	ciep = STAILQ_FIRST(&dbg->dbgp_cielist);
	for (i = 0; (Dwarf_Unsigned) i < cie; i++) {
		ciep = STAILQ_NEXT(ciep, cie_next);
		if (ciep == NULL)
			break;
	}
	if (ciep == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	if (end_symbol_index > 0 &&
	    (dbg->dbgp_flags & DW_DLC_SYMBOLIC_RELOCATIONS) == 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_NOCOUNT);
	}

	fde->fde_cie = ciep;
	fde->fde_initloc = virt_addr;
	fde->fde_adrange = code_len;
	fde->fde_symndx = symbol_index;
	fde->fde_esymndx = end_symbol_index;
	fde->fde_eoff = offset_from_end_sym;

	STAILQ_INSERT_TAIL(&dbg->dbgp_fdelist, fde, fde_next);

	return (dbg->dbgp_fdelen++);
}

Dwarf_P_Fde
dwarf_fde_cfa_offset(Dwarf_P_Fde fde, Dwarf_Unsigned reg, Dwarf_Signed offset,
    Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;

	dbg = fde != NULL ? fde->fde_dbg : NULL;

	if (fde == NULL || reg > 0x3f) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	ret = _dwarf_frame_fde_add_inst(fde, DW_CFA_offset | (reg & 0x3f),
	    offset, 0, error);

	if (ret != DW_DLE_NONE)
		return (DW_DLV_BADADDR);

	return (fde);
}

Dwarf_P_Fde
dwarf_add_fde_inst(Dwarf_P_Fde fde, Dwarf_Small op, Dwarf_Unsigned val1,
    Dwarf_Unsigned val2, Dwarf_Error *error)
{
	int ret;

	if (fde == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	ret = _dwarf_frame_fde_add_inst(fde, op, val1, val2, error);

	if (ret != DW_DLE_NONE)
		return (DW_DLV_BADADDR);

	return (fde);
}
