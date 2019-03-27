# This shell script emits a C file. -*- C -*-
#   Copyright 2003, 2004, 2005 Free Software Foundation, Inc.
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

# This file is sourced from elf32.em, and defines extra alpha
# specific routines.
#
cat >>e${EMULATION_NAME}.c <<EOF

#include "elf/internal.h"
#include "elf/alpha.h"
#include "elf-bfd.h"

static bfd_boolean limit_32bit;
static bfd_boolean disable_relaxation;

extern bfd_boolean elf64_alpha_use_secureplt;
extern const bfd_target bfd_elf64_alpha_vec;
extern const bfd_target bfd_elf64_alpha_freebsd_vec;


/* Set the start address as in the Tru64 ld.  */
#define ALPHA_TEXT_START_32BIT 0x12000000

static void
alpha_after_open (void)
{
  if (link_info.hash->creator == &bfd_elf64_alpha_vec
      || link_info.hash->creator == &bfd_elf64_alpha_freebsd_vec)
    {
      unsigned int num_plt;
      lang_output_section_statement_type *os;
      lang_output_section_statement_type *plt_os[2];

      num_plt = 0;
      for (os = &lang_output_section_statement.head->output_section_statement;
	   os != NULL;
	   os = os->next)
	{
	  if (os->constraint == SPECIAL && strcmp (os->name, ".plt") == 0)
	    {
	      if (num_plt < 2)
		plt_os[num_plt] = os;
	      ++num_plt;
	    }
	}

      if (num_plt == 2)
	{
	  plt_os[0]->constraint = elf64_alpha_use_secureplt ? 0 : -1;
	  plt_os[1]->constraint = elf64_alpha_use_secureplt ? -1 : 0;
	}
    }

  gld${EMULATION_NAME}_after_open ();
}

static void
alpha_after_parse (void)
{
  if (limit_32bit && !link_info.shared && !link_info.relocatable)
    lang_section_start (".interp",
			exp_binop ('+',
				   exp_intop (ALPHA_TEXT_START_32BIT),
				   exp_nameop (SIZEOF_HEADERS, NULL)),
			NULL);
}

static void
alpha_before_allocation (void)
{
  /* Call main function; we're just extending it.  */
  gld${EMULATION_NAME}_before_allocation ();

  /* Add -relax if -O, not -r, and not explicitly disabled.  */
  if (link_info.optimize && !link_info.relocatable && !disable_relaxation)
    command_line.relax = TRUE;
}

static void
alpha_finish (void)
{
  if (limit_32bit)
    elf_elfheader (output_bfd)->e_flags |= EF_ALPHA_32BIT;

  gld${EMULATION_NAME}_finish ();
}
EOF

# Define some shell vars to insert bits of code into the standard elf
# parse_args and list_options functions.
#
PARSE_AND_LIST_PROLOGUE='
#define OPTION_TASO		300
#define OPTION_NO_RELAX		(OPTION_TASO + 1)
#define OPTION_SECUREPLT	(OPTION_NO_RELAX + 1)
#define OPTION_NO_SECUREPLT	(OPTION_SECUREPLT + 1)
'

PARSE_AND_LIST_LONGOPTS='
  { "taso", no_argument, NULL, OPTION_TASO },
  { "no-relax", no_argument, NULL, OPTION_NO_RELAX },
  { "secureplt", no_argument, NULL, OPTION_SECUREPLT },
  { "no-secureplt", no_argument, NULL, OPTION_NO_SECUREPLT },
'

PARSE_AND_LIST_OPTIONS='
  fprintf (file, _("\
  --taso		Load executable in the lower 31-bit addressable\n\
			virtual address range.\n\
  --no-relax		Do not relax call and gp sequences.\n\
  --secureplt		Force PLT in text segment.\n\
  --no-secureplt	Force PLT in data segment.\n\
"));
'

PARSE_AND_LIST_ARGS_CASES='
    case OPTION_TASO:
      limit_32bit = 1;
      break;
    case OPTION_NO_RELAX:
      disable_relaxation = TRUE;
      break;
    case OPTION_SECUREPLT:
      elf64_alpha_use_secureplt = TRUE;
      break;
    case OPTION_NO_SECUREPLT:
      elf64_alpha_use_secureplt = FALSE;
      break;
'

# Put these extra alpha routines in ld_${EMULATION_NAME}_emulation
#
LDEMUL_AFTER_OPEN=alpha_after_open
LDEMUL_AFTER_PARSE=alpha_after_parse
LDEMUL_BEFORE_ALLOCATION=alpha_before_allocation
LDEMUL_FINISH=alpha_finish
