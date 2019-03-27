# This shell script emits a C file. -*- C -*-
#   Copyright 2006 Free Software Foundation, Inc.
#   Contributed by: 
#   Mei Ligang (ligang@sunnorth.com.cn)
#   Pei-Lin Tsai (pltsai@sunplus.com)

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
# Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
# 02110-1301, USA.
#

# This file is sourced from elf32.em, and defines extra score-elf
# specific routines.
#
cat >>e${EMULATION_NAME}.c <<EOF

static void
gld${EMULATION_NAME}_before_parse ()
{
#ifndef TARGET_			/* I.e., if not generic.  */
  ldfile_set_output_arch ("`echo ${ARCH}`");
#endif /* not TARGET_ */
  config.dynamic_link = ${DYNAMIC_LINK-true};
  config.has_shared = `if test -n "$GENERATE_SHLIB_SCRIPT" ; then echo true ; else echo false ; fi`;
}

static void
score_elf_after_open (void)
{
  if (strstr (bfd_get_target (output_bfd), "score") == NULL)
    {
      /* The score backend needs special fields in the output hash structure.
	 These will only be created if the output format is an score format,
	 hence we do not support linking and changing output formats at the
	 same time.  Use a link followed by objcopy to change output formats.  */
      einfo ("%F%X%P: error: cannot change output format whilst linking S+core binaries\n");
      return;
    }

  /* Call the standard elf routine.  */
  gld${EMULATION_NAME}_after_open ();
}

EOF

# Define some shell vars to insert bits of code into the standard elf
# parse_args and list_options functions.
#
PARSE_AND_LIST_PROLOGUE=''
PARSE_AND_LIST_SHORTOPTS=
PARSE_AND_LIST_LONGOPTS=''
PARSE_AND_LIST_OPTIONS=''
PARSE_AND_LIST_ARGS_CASES=''

# We have our own after_open and before_allocation functions, but they call
# the standard routines, so give them a different name.
LDEMUL_AFTER_OPEN=score_elf_after_open

# Replace the elf before_parse function with our own.
LDEMUL_BEFORE_PARSE=gld"${EMULATION_NAME}"_before_parse

