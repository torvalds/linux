/* ldemul.c -- clearing house for ld emulation states
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2005, 2007
   Free Software Foundation, Inc.

This file is part of GLD, the Gnu Linker.

GLD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GLD is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GLD; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "getopt.h"
#include "bfdlink.h"

#include "ld.h"
#include "ldmisc.h"
#include "ldexp.h"
#include "ldlang.h"
#include "ldfile.h"
#include "ldemul.h"
#include "ldmain.h"
#include "ldemul-list.h"

static ld_emulation_xfer_type *ld_emulation;

void
ldemul_hll (char *name)
{
  ld_emulation->hll (name);
}

void
ldemul_syslib (char *name)
{
  ld_emulation->syslib (name);
}

void
ldemul_after_parse (void)
{
  ld_emulation->after_parse ();
}

void
ldemul_before_parse (void)
{
  ld_emulation->before_parse ();
}

void
ldemul_after_open (void)
{
  ld_emulation->after_open ();
}

void
ldemul_after_allocation (void)
{
  ld_emulation->after_allocation ();
}

void
ldemul_before_allocation (void)
{
  ld_emulation->before_allocation ();
}

void
ldemul_set_output_arch (void)
{
  ld_emulation->set_output_arch ();
}

void
ldemul_finish (void)
{
  ld_emulation->finish ();
}

void
ldemul_set_symbols (void)
{
  if (ld_emulation->set_symbols)
    ld_emulation->set_symbols ();
}

void
ldemul_create_output_section_statements (void)
{
  if (ld_emulation->create_output_section_statements)
    ld_emulation->create_output_section_statements ();
}

char *
ldemul_get_script (int *isfile)
{
  return ld_emulation->get_script (isfile);
}

bfd_boolean
ldemul_open_dynamic_archive (const char *arch, search_dirs_type *search,
			     lang_input_statement_type *entry)
{
  if (ld_emulation->open_dynamic_archive)
    return (*ld_emulation->open_dynamic_archive) (arch, search, entry);
  return FALSE;
}

bfd_boolean
ldemul_place_orphan (asection *s)
{
  if (ld_emulation->place_orphan)
    return (*ld_emulation->place_orphan) (s);
  return FALSE;
}

void
ldemul_add_options (int ns, char **shortopts, int nl,
		    struct option **longopts, int nrl,
		    struct option **really_longopts)
{
  if (ld_emulation->add_options)
    (*ld_emulation->add_options) (ns, shortopts, nl, longopts,
				  nrl, really_longopts);
}

bfd_boolean
ldemul_handle_option (int optc)
{
  if (ld_emulation->handle_option)
    return (*ld_emulation->handle_option) (optc);
  return FALSE;
}

bfd_boolean
ldemul_parse_args (int argc, char **argv)
{
  /* Try and use the emulation parser if there is one.  */
  if (ld_emulation->parse_args)
    return (*ld_emulation->parse_args) (argc, argv);
  return FALSE;
}

/* Let the emulation code handle an unrecognized file.  */

bfd_boolean
ldemul_unrecognized_file (lang_input_statement_type *entry)
{
  if (ld_emulation->unrecognized_file)
    return (*ld_emulation->unrecognized_file) (entry);
  return FALSE;
}

/* Let the emulation code handle a recognized file.  */

bfd_boolean
ldemul_recognized_file (lang_input_statement_type *entry)
{
  if (ld_emulation->recognized_file)
    return (*ld_emulation->recognized_file) (entry);
  return FALSE;
}

char *
ldemul_choose_target (int argc, char **argv)
{
  return ld_emulation->choose_target (argc, argv);
}


/* The default choose_target function.  */

char *
ldemul_default_target (int argc ATTRIBUTE_UNUSED, char **argv ATTRIBUTE_UNUSED)
{
  char *from_outside = getenv (TARGET_ENVIRON);
  if (from_outside != (char *) NULL)
    return from_outside;
  return ld_emulation->target_name;
}

void
after_parse_default (void)
{
}

void
after_open_default (void)
{
}

void
after_allocation_default (void)
{
}

void
before_allocation_default (void)
{
  if (!link_info.relocatable)
    strip_excluded_output_sections ();
}

void
finish_default (void)
{
  if (!link_info.relocatable)
    _bfd_fix_excluded_sec_syms (output_bfd, &link_info);
}

void
set_output_arch_default (void)
{
  /* Set the output architecture and machine if possible.  */
  bfd_set_arch_mach (output_bfd,
		     ldfile_output_architecture, ldfile_output_machine);
}

void
syslib_default (char *ignore ATTRIBUTE_UNUSED)
{
  info_msg (_("%S SYSLIB ignored\n"));
}

void
hll_default (char *ignore ATTRIBUTE_UNUSED)
{
  info_msg (_("%S HLL ignored\n"));
}

ld_emulation_xfer_type *ld_emulations[] = { EMULATION_LIST };

void
ldemul_choose_mode (char *target)
{
  ld_emulation_xfer_type **eptr = ld_emulations;
  /* Ignore "gld" prefix.  */
  if (target[0] == 'g' && target[1] == 'l' && target[2] == 'd')
    target += 3;
  for (; *eptr; eptr++)
    {
      if (strcmp (target, (*eptr)->emulation_name) == 0)
	{
	  ld_emulation = *eptr;
	  return;
	}
    }
  einfo (_("%P: unrecognised emulation mode: %s\n"), target);
  einfo (_("Supported emulations: "));
  ldemul_list_emulations (stderr);
  einfo ("%F\n");
}

void
ldemul_list_emulations (FILE *f)
{
  ld_emulation_xfer_type **eptr = ld_emulations;
  bfd_boolean first = TRUE;

  for (; *eptr; eptr++)
    {
      if (first)
	first = FALSE;
      else
	fprintf (f, " ");
      fprintf (f, "%s", (*eptr)->emulation_name);
    }
}

void
ldemul_list_emulation_options (FILE *f)
{
  ld_emulation_xfer_type **eptr;
  int options_found = 0;

  for (eptr = ld_emulations; *eptr; eptr++)
    {
      ld_emulation_xfer_type *emul = *eptr;

      if (emul->list_options)
	{
	  fprintf (f, "%s: \n", emul->emulation_name);

	  emul->list_options (f);

	  options_found = 1;
	}
    }

  if (! options_found)
    fprintf (f, _("  no emulation specific options.\n"));
}

int
ldemul_find_potential_libraries (char *name, lang_input_statement_type *entry)
{
  if (ld_emulation->find_potential_libraries)
    return ld_emulation->find_potential_libraries (name, entry);

  return 0;
}

struct bfd_elf_version_expr *
ldemul_new_vers_pattern (struct bfd_elf_version_expr *entry)
{
  if (ld_emulation->new_vers_pattern)
    entry = (*ld_emulation->new_vers_pattern) (entry);
  return entry;
}
