/* Binutils emulation layer.
   Copyright 2002, 2003, 2007 Free Software Foundation, Inc.
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

#ifndef BINEMUL_H
#define BINEMUL_H

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"

extern void ar_emul_usage (FILE *);
extern void ar_emul_default_usage (FILE *);
extern bfd_boolean ar_emul_append (bfd **, char *, bfd_boolean);
extern bfd_boolean ar_emul_default_append (bfd **, char *, bfd_boolean);
extern bfd_boolean ar_emul_replace (bfd **, char *, bfd_boolean);
extern bfd_boolean ar_emul_default_replace (bfd **, char *, bfd_boolean);
extern bfd_boolean ar_emul_parse_arg (char *);
extern bfd_boolean ar_emul_default_parse_arg (char *);

/* Macros for common output.  */

#define AR_EMUL_USAGE_PRINT_OPTION_HEADER(fp) \
  /* xgettext:c-format */                     \
  fprintf (fp, _(" emulation options: \n"))

#define AR_EMUL_ELEMENT_CHECK(abfd, file_name) \
  do { if ((abfd) == NULL) bfd_fatal (file_name); } while (0)

#define AR_EMUL_APPEND_PRINT_VERBOSE(verbose, file_name) \
  do { if (verbose) printf ("a - %s\n", file_name); } while (0)

#define AR_EMUL_REPLACE_PRINT_VERBOSE(verbose, file_name) \
  do { if (verbose) printf ("r - %s\n", file_name); } while (0)

typedef struct bin_emulation_xfer_struct
{
  /* Print out the extra options.  */
  void (* ar_usage) (FILE *fp);
  bfd_boolean (* ar_append) (bfd **, char *, bfd_boolean);
  bfd_boolean (* ar_replace) (bfd **, char *, bfd_boolean);
  bfd_boolean (* ar_parse_arg) (char *);
}
bin_emulation_xfer_type;

#endif
