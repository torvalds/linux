/* Various diagnostic subroutines for the GNU C language.
   Copyright (C) 2000, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@codesourcery.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "c-tree.h"
#include "tm_p.h"
#include "flags.h"
#include "diagnostic.h"

/* Issue an ISO C99 pedantic warning MSGID.  */

void
pedwarn_c99 (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location,
		       flag_isoc99 ? pedantic_error_kind () : DK_WARNING);
  report_diagnostic (&diagnostic);
  va_end (ap);
}

/* Issue an ISO C90 pedantic warning MSGID.  This function is supposed to
   be used for matters that are allowed in ISO C99 but not supported in
   ISO C90, thus we explicitly don't pedwarn when C99 is specified.
   (There is no flag_c90.)  */

void
pedwarn_c90 (const char *gmsgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;

  va_start (ap, gmsgid);
  diagnostic_set_info (&diagnostic, gmsgid, &ap, input_location,
		       flag_isoc99 ? DK_WARNING : pedantic_error_kind ());
  report_diagnostic (&diagnostic);
  va_end (ap);
}
