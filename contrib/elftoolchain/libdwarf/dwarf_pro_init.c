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

ELFTC_VCSID("$Id: dwarf_pro_init.c 2074 2011-10-27 03:34:33Z jkoshy $");

Dwarf_P_Debug
dwarf_producer_init(Dwarf_Unsigned flags, Dwarf_Callback_Func func,
    Dwarf_Handler errhand, Dwarf_Ptr errarg, Dwarf_Error *error)
{
	Dwarf_P_Debug dbg;
	int mode;

	if (flags & DW_DLC_READ || flags & DW_DLC_RDWR) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if (flags & DW_DLC_WRITE)
		mode = DW_DLC_WRITE;
	else {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if (func == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if (_dwarf_alloc(&dbg, DW_DLC_WRITE, error) != DW_DLE_NONE)
		return (DW_DLV_BADADDR);

	dbg->dbg_mode = mode;

	if (_dwarf_init(dbg, flags, errhand, errarg, error) != DW_DLE_NONE) {
		free(dbg);
		return (DW_DLV_BADADDR);
	}

	dbg->dbgp_func = func;

	return (dbg);
}

Dwarf_P_Debug
dwarf_producer_init_b(Dwarf_Unsigned flags, Dwarf_Callback_Func_b func,
    Dwarf_Handler errhand, Dwarf_Ptr errarg, Dwarf_Error *error)
{
	Dwarf_P_Debug dbg;
	int mode;

	if (flags & DW_DLC_READ || flags & DW_DLC_RDWR) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if (flags & DW_DLC_WRITE)
		mode = DW_DLC_WRITE;
	else {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if (func == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_BADADDR);
	}

	if (_dwarf_alloc(&dbg, DW_DLC_WRITE, error) != DW_DLE_NONE)
		return (DW_DLV_BADADDR);

	dbg->dbg_mode = mode;

	if (_dwarf_init(dbg, flags, errhand, errarg, error) != DW_DLE_NONE) {
		free(dbg);
		return (DW_DLV_BADADDR);
	}

	dbg->dbgp_func_b = func;

	return (dbg);
}

int
dwarf_producer_set_isa(Dwarf_P_Debug dbg, enum Dwarf_ISA isa,
    Dwarf_Error *error)
{

	if (dbg == NULL || isa >= DW_ISA_MAX) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	dbg->dbgp_isa = isa;

	return (DW_DLV_OK);
}
