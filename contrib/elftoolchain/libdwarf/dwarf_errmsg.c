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

ELFTC_VCSID("$Id: dwarf_errmsg.c 2975 2014-01-21 20:08:04Z kaiwang27 $");

static const char *_libdwarf_errors[] = {
#define	DEFINE_ERROR(N,S)		[DW_DLE_##N] = S
	DEFINE_ERROR(NONE, "No Error"),
	DEFINE_ERROR(ERROR, "An error"),
	DEFINE_ERROR(NO_ENTRY, "No entry found"),
	DEFINE_ERROR(ARGUMENT, "Invalid argument"),
	DEFINE_ERROR(DEBUG_INFO_NULL, "Debug info NULL"),
	DEFINE_ERROR(MEMORY, "Insufficient memory"),
	DEFINE_ERROR(ELF, "ELF error"),
	DEFINE_ERROR(CU_LENGTH_ERROR, "Invalid compilation unit data"),
	DEFINE_ERROR(VERSION_STAMP_ERROR, "Unsupported version"),
	DEFINE_ERROR(DEBUG_ABBREV_NULL, "Abbrev not found"),
	DEFINE_ERROR(DIE_NO_CU_CONTEXT,	"No current compilation unit"),
	DEFINE_ERROR(LOC_EXPR_BAD, "Invalid location expression"),
	DEFINE_ERROR(EXPR_LENGTH_BAD, "Invalid DWARF expression length"),
	DEFINE_ERROR(DEBUG_LOC_SECTION_SHORT, "Loclist section too short"),
	DEFINE_ERROR(ATTR_FORM_BAD, "Invalid attribute form"),
	DEFINE_ERROR(DEBUG_LINE_LENGTH_BAD, "Line info section too short"),
	DEFINE_ERROR(LINE_FILE_NUM_BAD, "Invalid file number."),
	DEFINE_ERROR(DIR_INDEX_BAD, "Invalid dir index."),
	DEFINE_ERROR(DEBUG_FRAME_LENGTH_BAD, "Frame section too short"),
	DEFINE_ERROR(NO_CIE_FOR_FDE, "FDE without corresponding CIE"),
	DEFINE_ERROR(FRAME_AUGMENTATION_UNKNOWN, "Unknown CIE augmentation"),
	DEFINE_ERROR(FRAME_INSTR_EXEC_ERROR, "Frame instruction exec error"),
	DEFINE_ERROR(FRAME_VERSION_BAD, "Unsupported frame section version"),
	DEFINE_ERROR(FRAME_TABLE_COL_BAD, "Invalid table column value"),
	DEFINE_ERROR(DF_REG_NUM_TOO_HIGH, "Register number too large"),
	DEFINE_ERROR(PC_NOT_IN_FDE_RANGE, "PC requested not in the FDE range"),
	DEFINE_ERROR(ARANGE_OFFSET_BAD, "Invalid address range offset"),
	DEFINE_ERROR(DEBUG_MACRO_INCONSISTENT, "Invalid macinfo data"),
	DEFINE_ERROR(ELF_SECT_ERR, "Application callback failed"),
	DEFINE_ERROR(NUM, "Unknown DWARF error")
#undef	DEFINE_ERROR
};

const char *
dwarf_errmsg_(Dwarf_Error *error)
{
	const char *p;

	if (error == NULL)
		return NULL;

	if (error->err_error < 0 || error->err_error >= DW_DLE_NUM)
		return _libdwarf_errors[DW_DLE_NUM];
	else if (error->err_error == DW_DLE_NONE)
		return _libdwarf_errors[DW_DLE_NONE];
	else
		p = _libdwarf_errors[error->err_error];

	if (error->err_error == DW_DLE_ELF)
		snprintf(error->err_msg, sizeof(error->err_msg),
		    "ELF error : %s [%s(%d)]", elf_errmsg(error->err_elferror),
		    error->err_func, error->err_line);
	else
		snprintf(error->err_msg, sizeof(error->err_msg),
		    "%s [%s(%d)]", p, error->err_func, error->err_line);

	return (const char *) error->err_msg;
}
