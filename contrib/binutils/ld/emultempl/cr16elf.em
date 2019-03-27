# This shell script emits a C file. -*- C -*-
# Copyright 2007 Free Software Foundation, Inc.
# Contributed by M R Swami Reddy <MR.Swami.Reddy@nsc.com>
#
# This file is part of GLD, the Gnu Linker.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
#

# This file is sourced from elf32.em, and defines extra cr16-elf
# specific routines.
#
cat >>e${EMULATION_NAME}.c <<EOF

#include "ldctor.h"

/* Flag for the emulation-specific "--no-relax" option.  */
static bfd_boolean disable_relaxation = FALSE;

static void
cr16elf_after_parse (void)
{
  /* Always behave as if called with --sort-common command line
     option.
     This is to emulate the CRTools' method of keeping variables
     of different alignment in separate sections.  */
  config.sort_common = TRUE;

  /* Don't create a demand-paged executable, since this feature isn't
     meaninful in CR16 embedded systems. Moreover, when magic_demand_paged
     is true the link sometimes fails.  */
  config.magic_demand_paged = FALSE;
}

/* This is called after the sections have been attached to output
   sections, but before any sizes or addresses have been set.  */

static void
cr16elf_before_allocation (void)
{
  /* Call the default first.  */
  gld${EMULATION_NAME}_before_allocation ();

  /* Enable relaxation by default if the "--no-relax" option was not
     specified.  This is done here instead of in the before_parse hook
     because there is a check in main() to prohibit use of --relax and
     -r together.  */

  if (!disable_relaxation)
    command_line.relax = TRUE;
}

EOF

# Define some shell vars to insert bits of code into the standard elf
# parse_args and list_options functions.
#
PARSE_AND_LIST_PROLOGUE='
#define OPTION_NO_RELAX			301
'

PARSE_AND_LIST_LONGOPTS='
  { "no-relax", no_argument, NULL, OPTION_NO_RELAX},
'

PARSE_AND_LIST_OPTIONS='
  fprintf (file, _("  --no-relax                  Do not relax branches\n"));
'

PARSE_AND_LIST_ARGS_CASES='
    case OPTION_NO_RELAX:
      disable_relaxation = TRUE;
      break;
'

# Put these extra cr16-elf routines in ld_${EMULATION_NAME}_emulation
#
LDEMUL_AFTER_PARSE=cr16elf_after_parse
LDEMUL_BEFORE_ALLOCATION=cr16elf_before_allocation

