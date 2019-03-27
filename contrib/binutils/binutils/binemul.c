/* Binutils emulation layer.
   Copyright 2002, 2003 Free Software Foundation, Inc.
   Written by Tom Rix, Red Hat Inc.

   This file is part of GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "binemul.h"

extern bin_emulation_xfer_type bin_dummy_emulation;

void
ar_emul_usage (FILE *fp)
{
  if (bin_dummy_emulation.ar_usage)
    bin_dummy_emulation.ar_usage (fp);
}

void
ar_emul_default_usage (FILE *fp)
{
  AR_EMUL_USAGE_PRINT_OPTION_HEADER (fp);
  /* xgettext:c-format */
  fprintf (fp, _("  No emulation specific options\n"));
}

bfd_boolean
ar_emul_append (bfd **after_bfd, char *file_name, bfd_boolean verbose)
{
  if (bin_dummy_emulation.ar_append)
    return bin_dummy_emulation.ar_append (after_bfd, file_name, verbose);

  return FALSE;
}

bfd_boolean
ar_emul_default_append (bfd **after_bfd, char *file_name,
			bfd_boolean verbose)
{
  bfd *temp;

  temp = *after_bfd;
  *after_bfd = bfd_openr (file_name, NULL);

  AR_EMUL_ELEMENT_CHECK (*after_bfd, file_name);
  AR_EMUL_APPEND_PRINT_VERBOSE (verbose, file_name);

  (*after_bfd)->archive_next = temp;

  return TRUE;
}

bfd_boolean
ar_emul_replace (bfd **after_bfd, char *file_name, bfd_boolean verbose)
{
  if (bin_dummy_emulation.ar_replace)
    return bin_dummy_emulation.ar_replace (after_bfd, file_name, verbose);

  return FALSE;
}

bfd_boolean
ar_emul_default_replace (bfd **after_bfd, char *file_name,
			 bfd_boolean verbose)
{
  bfd *temp;

  temp = *after_bfd;
  *after_bfd = bfd_openr (file_name, NULL);

  AR_EMUL_ELEMENT_CHECK (*after_bfd, file_name);
  AR_EMUL_REPLACE_PRINT_VERBOSE (verbose, file_name);

  (*after_bfd)->archive_next = temp;

  return TRUE;
}

bfd_boolean
ar_emul_parse_arg (char *arg)
{
  if (bin_dummy_emulation.ar_parse_arg)
    return bin_dummy_emulation.ar_parse_arg (arg);

  return FALSE;
}

bfd_boolean
ar_emul_default_parse_arg (char *arg ATTRIBUTE_UNUSED)
{
  return FALSE;
}
