# This shell script emits a C file. -*- C -*-
#   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.
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

# This file is sourced from elf32.em.  It is used by targets for
# which relaxation is not just an optimization, but for correctness.

LDEMUL_BEFORE_ALLOCATION=need_relax_${EMULATION_NAME}_before_allocation

cat >>e${EMULATION_NAME}.c <<EOF

static void
need_relax_${EMULATION_NAME}_before_allocation (void)
{
  /* Call main function; we're just extending it.  */
  gld${EMULATION_NAME}_before_allocation ();

  /* Force -relax on if not doing a relocatable link.  */
  if (! link_info.relocatable)
    command_line.relax = TRUE;
}
EOF
