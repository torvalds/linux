/* RTL specific diagnostic subroutines for GCC
   Copyright (C) 2001, 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@codesourcery.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#undef FLOAT /* This is for hpux. They should change hpux.  */
#undef FFS  /* Some systems define this in param.h.  */
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "insn-attr.h"
#include "insn-config.h"
#include "input.h"
#include "toplev.h"
#include "intl.h"
#include "diagnostic.h"

static location_t location_for_asm (rtx);
static void diagnostic_for_asm (rtx, const char *, va_list *, diagnostic_t) ATTRIBUTE_GCC_DIAG(2,0);

/* Figure the location of the given INSN.  */
static location_t
location_for_asm (rtx insn)
{
  rtx body = PATTERN (insn);
  rtx asmop;
  location_t loc;

  /* Find the (or one of the) ASM_OPERANDS in the insn.  */
  if (GET_CODE (body) == SET && GET_CODE (SET_SRC (body)) == ASM_OPERANDS)
    asmop = SET_SRC (body);
  else if (GET_CODE (body) == ASM_OPERANDS)
    asmop = body;
  else if (GET_CODE (body) == PARALLEL
	   && GET_CODE (XVECEXP (body, 0, 0)) == SET)
    asmop = SET_SRC (XVECEXP (body, 0, 0));
  else if (GET_CODE (body) == PARALLEL
	   && GET_CODE (XVECEXP (body, 0, 0)) == ASM_OPERANDS)
    asmop = XVECEXP (body, 0, 0);
  else
    asmop = NULL;

  if (asmop)
#ifdef USE_MAPPED_LOCATION
    loc = ASM_OPERANDS_SOURCE_LOCATION (asmop);
#else
    {
      loc.file = ASM_OPERANDS_SOURCE_FILE (asmop);
      loc.line = ASM_OPERANDS_SOURCE_LINE (asmop);
    }
#endif
  else
    loc = input_location;
  return loc;
}

/* Report a diagnostic MESSAGE (an errror or a WARNING) at the line number
   of the insn INSN.  This is used only when INSN is an `asm' with operands,
   and each ASM_OPERANDS records its own source file and line.  */
static void
diagnostic_for_asm (rtx insn, const char *msg, va_list *args_ptr,
		    diagnostic_t kind)
{
  diagnostic_info diagnostic;

  diagnostic_set_info (&diagnostic, msg, args_ptr,
		       location_for_asm (insn), kind);
  report_diagnostic (&diagnostic);
}

void
error_for_asm (rtx insn, const char *gmsgid, ...)
{
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_for_asm (insn, gmsgid, &ap, DK_ERROR);
  va_end (ap);
}

void
warning_for_asm (rtx insn, const char *gmsgid, ...)
{
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_for_asm (insn, gmsgid, &ap, DK_WARNING);
  va_end (ap);
}

void
_fatal_insn (const char *msgid, rtx insn, const char *file, int line,
	     const char *function)
{
  error ("%s", _(msgid));

  /* The above incremented error_count, but isn't an error that we want to
     count, so reset it here.  */
  errorcount--;

  debug_rtx (insn);
  fancy_abort (file, line, function);
}

void
_fatal_insn_not_found (rtx insn, const char *file, int line,
		       const char *function)
{
  if (INSN_CODE (insn) < 0)
    _fatal_insn ("unrecognizable insn:", insn, file, line, function);
  else
    _fatal_insn ("insn does not satisfy its constraints:",
		insn, file, line, function);
}
