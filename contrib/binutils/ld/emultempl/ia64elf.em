# This shell script emits a C file. -*- C -*-
#   Copyright 2003 Free Software Foundation, Inc.
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

# This file is sourced from elf32.em, and defines extra ia64-elf
# specific routines.
#
# Define some shell vars to insert bits of code into the standard elf
# parse_args and list_options functions.
#
cat >>e${EMULATION_NAME}.c <<EOF

/* None zero if generating binary for Intel Itanium processor.  */
static int itanium = 0;

static void
gld${EMULATION_NAME}_after_parse (void)
{
  link_info.relax_pass = 2;
  bfd_elf${ELFSIZE}_ia64_after_parse (itanium);
}

EOF

PARSE_AND_LIST_PROLOGUE='
#define OPTION_ITANIUM			300
'

PARSE_AND_LIST_LONGOPTS='
    { "itanium", no_argument, NULL, OPTION_ITANIUM},
'

PARSE_AND_LIST_OPTIONS='
  fprintf (file, _("\
  --itanium             Generate code for Intel Itanium processor\n"
		   ));
'

PARSE_AND_LIST_ARGS_CASES='
    case OPTION_ITANIUM:
      itanium = 1;
      break;
'

LDEMUL_AFTER_PARSE=gld${EMULATION_NAME}_after_parse
. ${srcdir}/emultempl/needrelax.em
