/* Linker file opening and searching.
   Copyright 1991, 1992, 1993, 1994, 1995, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2007 Free Software Foundation, Inc.

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

/* ldfile.c:  look after all the file stuff.  */

#include "sysdep.h"
#include "bfd.h"
#include "bfdlink.h"
#include "safe-ctype.h"
#include "ld.h"
#include "ldmisc.h"
#include "ldexp.h"
#include "ldlang.h"
#include "ldfile.h"
#include "ldmain.h"
#include <ldgram.h>
#include "ldlex.h"
#include "ldemul.h"
#include "libiberty.h"
#include "filenames.h"

const char * ldfile_input_filename;
bfd_boolean  ldfile_assumed_script = FALSE;
const char * ldfile_output_machine_name = "";
unsigned long ldfile_output_machine;
enum bfd_architecture ldfile_output_architecture;
search_dirs_type * search_head;

#ifdef VMS
static char * slash = "";
#else
#if defined (_WIN32) && ! defined (__CYGWIN32__)
static char * slash = "\\";
#else
static char * slash = "/";
#endif
#endif

typedef struct search_arch
{
  char *name;
  struct search_arch *next;
} search_arch_type;

static search_dirs_type **search_tail_ptr = &search_head;
static search_arch_type *search_arch_head;
static search_arch_type **search_arch_tail_ptr = &search_arch_head;

/* Test whether a pathname, after canonicalization, is the same or a
   sub-directory of the sysroot directory.  */

static bfd_boolean
is_sysrooted_pathname (const char *name, bfd_boolean notsame)
{
  char * realname = ld_canon_sysroot ? lrealpath (name) : NULL;
  int len;
  bfd_boolean result;

  if (! realname)
    return FALSE;

  len = strlen (realname);

  if (((! notsame && len == ld_canon_sysroot_len)
       || (len >= ld_canon_sysroot_len
	   && IS_DIR_SEPARATOR (realname[ld_canon_sysroot_len])
	   && (realname[ld_canon_sysroot_len] = '\0') == '\0'))
      && FILENAME_CMP (ld_canon_sysroot, realname) == 0)
    result = TRUE;
  else
    result = FALSE;

  if (realname)
    free (realname);

  return result;
}

/* Adds NAME to the library search path.
   Makes a copy of NAME using xmalloc().  */

void
ldfile_add_library_path (const char *name, bfd_boolean cmdline)
{
  search_dirs_type *new;

  if (!cmdline && config.only_cmd_line_lib_dirs)
    return;

  new = xmalloc (sizeof (search_dirs_type));
  new->next = NULL;
  new->cmdline = cmdline;
  *search_tail_ptr = new;
  search_tail_ptr = &new->next;

  /* If a directory is marked as honoring sysroot, prepend the sysroot path
     now.  */
  if (name[0] == '=')
    {
      new->name = concat (ld_sysroot, name + 1, NULL);
      new->sysrooted = TRUE;
    }
  else
    {
      new->name = xstrdup (name);
      new->sysrooted = is_sysrooted_pathname (name, FALSE);
    }
}

/* Try to open a BFD for a lang_input_statement.  */

bfd_boolean
ldfile_try_open_bfd (const char *attempt,
		     lang_input_statement_type *entry)
{
  entry->the_bfd = bfd_openr (attempt, entry->target);

  if (trace_file_tries)
    {
      if (entry->the_bfd == NULL)
	info_msg (_("attempt to open %s failed\n"), attempt);
      else
	info_msg (_("attempt to open %s succeeded\n"), attempt);
    }

  if (entry->the_bfd == NULL)
    {
      if (bfd_get_error () == bfd_error_invalid_target)
	einfo (_("%F%P: invalid BFD target `%s'\n"), entry->target);
      return FALSE;
    }

  /* If we are searching for this file, see if the architecture is
     compatible with the output file.  If it isn't, keep searching.
     If we can't open the file as an object file, stop the search
     here.  If we are statically linking, ensure that we don't link
     a dynamic object.  */

  if (entry->search_dirs_flag || !entry->dynamic)
    {
      bfd *check;

      if (bfd_check_format (entry->the_bfd, bfd_archive))
	check = bfd_openr_next_archived_file (entry->the_bfd, NULL);
      else
	check = entry->the_bfd;

      if (check != NULL)
	{
	  if (! bfd_check_format (check, bfd_object))
	    {
	      if (check == entry->the_bfd
		  && entry->search_dirs_flag
		  && bfd_get_error () == bfd_error_file_not_recognized
		  && ! ldemul_unrecognized_file (entry))
		{
		  int token, skip = 0;
		  char *arg, *arg1, *arg2, *arg3;
		  extern FILE *yyin;

		  /* Try to interpret the file as a linker script.  */
		  ldfile_open_command_file (attempt);

		  ldfile_assumed_script = TRUE;
		  parser_input = input_selected;
		  ldlex_both ();
		  token = INPUT_SCRIPT;
		  while (token != 0)
		    {
		      switch (token)
			{
			case OUTPUT_FORMAT:
			  if ((token = yylex ()) != '(')
			    continue;
			  if ((token = yylex ()) != NAME)
			    continue;
			  arg1 = yylval.name;
			  arg2 = NULL;
			  arg3 = NULL;
			  token = yylex ();
			  if (token == ',')
			    {
			      if ((token = yylex ()) != NAME)
				{
				  free (arg1);
				  continue;
				}
			      arg2 = yylval.name;
			      if ((token = yylex ()) != ','
				  || (token = yylex ()) != NAME)
				{
				  free (arg1);
				  free (arg2);
				  continue;
				}
			      arg3 = yylval.name;
			      token = yylex ();
			    }
			  if (token == ')')
			    {
			      switch (command_line.endian)
				{
				default:
				case ENDIAN_UNSET:
				  arg = arg1; break;
				case ENDIAN_BIG:
				  arg = arg2 ? arg2 : arg1; break;
				case ENDIAN_LITTLE:
				  arg = arg3 ? arg3 : arg1; break;
				}
			      if (strcmp (arg, lang_get_output_target ()) != 0)
				skip = 1;
			    }
			  free (arg1);
			  if (arg2) free (arg2);
			  if (arg3) free (arg3);
			  break;
			case NAME:
			case LNAME:
			case VERS_IDENTIFIER:
			case VERS_TAG:
			  free (yylval.name);
			  break;
			case INT:
			  if (yylval.bigint.str)
			    free (yylval.bigint.str);
			  break;
			}
		      token = yylex ();
		    }
		  ldlex_popstate ();
		  ldfile_assumed_script = FALSE;
		  fclose (yyin);
		  yyin = NULL;
		  if (skip)
		    {
		      if (command_line.warn_search_mismatch)
			einfo (_("%P: skipping incompatible %s "
				 "when searching for %s\n"),
			       attempt, entry->local_sym_name);
		      bfd_close (entry->the_bfd);
		      entry->the_bfd = NULL;
		      return FALSE;
		    }
		}
	      return TRUE;
	    }

	  if (!entry->dynamic && (entry->the_bfd->flags & DYNAMIC) != 0)
	    {
	      einfo (_("%F%P: attempted static link of dynamic object `%s'\n"),
		     attempt);
	      bfd_close (entry->the_bfd);
	      entry->the_bfd = NULL;
	      return FALSE;
	    }

	  if (entry->search_dirs_flag
	      && !bfd_arch_get_compatible (check, output_bfd,
					   command_line.accept_unknown_input_arch)
	      /* XCOFF archives can have 32 and 64 bit objects.  */
	      && ! (bfd_get_flavour (check) == bfd_target_xcoff_flavour
		    && bfd_get_flavour (output_bfd) == bfd_target_xcoff_flavour
		    && bfd_check_format (entry->the_bfd, bfd_archive)))
	    {
	      if (command_line.warn_search_mismatch)
		einfo (_("%P: skipping incompatible %s "
			 "when searching for %s\n"),
		       attempt, entry->local_sym_name);
	      bfd_close (entry->the_bfd);
	      entry->the_bfd = NULL;
	      return FALSE;
	    }
	}
    }

  return TRUE;
}

/* Search for and open the file specified by ENTRY.  If it is an
   archive, use ARCH, LIB and SUFFIX to modify the file name.  */

bfd_boolean
ldfile_open_file_search (const char *arch,
			 lang_input_statement_type *entry,
			 const char *lib,
			 const char *suffix)
{
  search_dirs_type *search;

  /* If this is not an archive, try to open it in the current
     directory first.  */
  if (! entry->is_archive)
    {
      if (entry->sysrooted && IS_ABSOLUTE_PATH (entry->filename))
	{
	  char *name = concat (ld_sysroot, entry->filename,
			       (const char *) NULL);
	  if (ldfile_try_open_bfd (name, entry))
	    {
	      entry->filename = name;
	      return TRUE;
	    }
	  free (name);
	}
      else if (ldfile_try_open_bfd (entry->filename, entry))
	{
	  entry->sysrooted = IS_ABSOLUTE_PATH (entry->filename)
	    && is_sysrooted_pathname (entry->filename, TRUE);
	  return TRUE;
	}

      if (IS_ABSOLUTE_PATH (entry->filename))
	return FALSE;
    }

  for (search = search_head; search != NULL; search = search->next)
    {
      char *string;

      if (entry->dynamic && ! link_info.relocatable)
	{
	  if (ldemul_open_dynamic_archive (arch, search, entry))
	    {
	      entry->sysrooted = search->sysrooted;
	      return TRUE;
	    }
	}

      string = xmalloc (strlen (search->name)
			+ strlen (slash)
			+ strlen (lib)
			+ strlen (entry->filename)
			+ strlen (arch)
			+ strlen (suffix)
			+ 1);

      if (entry->is_archive)
	sprintf (string, "%s%s%s%s%s%s", search->name, slash,
		 lib, entry->filename, arch, suffix);
      else
	sprintf (string, "%s%s%s", search->name, slash, entry->filename);

      if (ldfile_try_open_bfd (string, entry))
	{
	  entry->filename = string;
	  entry->sysrooted = search->sysrooted;
	  return TRUE;
	}

      free (string);
    }

  return FALSE;
}

/* Open the input file specified by ENTRY.  */

void
ldfile_open_file (lang_input_statement_type *entry)
{
  if (entry->the_bfd != NULL)
    return;

  if (! entry->search_dirs_flag)
    {
      if (ldfile_try_open_bfd (entry->filename, entry))
	return;
      if (strcmp (entry->filename, entry->local_sym_name) != 0)
	einfo (_("%F%P: %s (%s): No such file: %E\n"),
	       entry->filename, entry->local_sym_name);
      else
	einfo (_("%F%P: %s: No such file: %E\n"), entry->local_sym_name);
    }
  else
    {
      search_arch_type *arch;
      bfd_boolean found = FALSE;

      /* Try to open <filename><suffix> or lib<filename><suffix>.a */
      for (arch = search_arch_head; arch != NULL; arch = arch->next)
	{
	  found = ldfile_open_file_search (arch->name, entry, "lib", ".a");
	  if (found)
	    break;
#ifdef VMS
	  found = ldfile_open_file_search (arch->name, entry, ":lib", ".a");
	  if (found)
	    break;
#endif
	  found = ldemul_find_potential_libraries (arch->name, entry);
	  if (found)
	    break;
	}

      /* If we have found the file, we don't need to search directories
	 again.  */
      if (found)
	entry->search_dirs_flag = FALSE;
      else if (entry->sysrooted
	       && ld_sysroot
	       && IS_ABSOLUTE_PATH (entry->local_sym_name))
	einfo (_("%F%P: cannot find %s inside %s\n"),
	       entry->local_sym_name, ld_sysroot);
      else
	einfo (_("%F%P: cannot find %s\n"), entry->local_sym_name);
    }
}

/* Try to open NAME; if that fails, try NAME with EXTEN appended to it.  */

static FILE *
try_open (const char *name, const char *exten)
{
  FILE *result;
  char buff[1000];

  result = fopen (name, "r");

  if (trace_file_tries)
    {
      if (result == NULL)
	info_msg (_("cannot find script file %s\n"), name);
      else
	info_msg (_("opened script file %s\n"), name);
    }

  if (result != NULL)
    return result;

  if (*exten)
    {
      sprintf (buff, "%s%s", name, exten);
      result = fopen (buff, "r");

      if (trace_file_tries)
	{
	  if (result == NULL)
	    info_msg (_("cannot find script file %s\n"), buff);
	  else
	    info_msg (_("opened script file %s\n"), buff);
	}
    }

  return result;
}

/* Try to open NAME; if that fails, look for it in any directories
   specified with -L, without and with EXTEND appended.  */

static FILE *
ldfile_find_command_file (const char *name, const char *extend)
{
  search_dirs_type *search;
  FILE *result;
  char buffer[1000];

  /* First try raw name.  */
  result = try_open (name, "");
  if (result == NULL)
    {
      /* Try now prefixes.  */
      for (search = search_head; search != NULL; search = search->next)
	{
	  sprintf (buffer, "%s%s%s", search->name, slash, name);

	  result = try_open (buffer, extend);
	  if (result)
	    break;
	}
    }

  return result;
}

void
ldfile_open_command_file (const char *name)
{
  FILE *ldlex_input_stack;
  ldlex_input_stack = ldfile_find_command_file (name, "");

  if (ldlex_input_stack == NULL)
    {
      bfd_set_error (bfd_error_system_call);
      einfo (_("%P%F: cannot open linker script file %s: %E\n"), name);
    }

  lex_push_file (ldlex_input_stack, name);

  ldfile_input_filename = name;
  lineno = 1;

  saved_script_handle = ldlex_input_stack;
}

void
ldfile_add_arch (const char *in_name)
{
  char *name = xstrdup (in_name);
  search_arch_type *new = xmalloc (sizeof (search_arch_type));

  ldfile_output_machine_name = in_name;

  new->name = name;
  new->next = NULL;
  while (*name)
    {
      *name = TOLOWER (*name);
      name++;
    }
  *search_arch_tail_ptr = new;
  search_arch_tail_ptr = &new->next;

}

/* Set the output architecture.  */

void
ldfile_set_output_arch (const char *string, enum bfd_architecture defarch)
{
  const bfd_arch_info_type *arch = bfd_scan_arch (string);

  if (arch)
    {
      ldfile_output_architecture = arch->arch;
      ldfile_output_machine = arch->mach;
      ldfile_output_machine_name = arch->printable_name;
    }
  else if (defarch != bfd_arch_unknown)
    ldfile_output_architecture = defarch;
  else
    einfo (_("%P%F: cannot represent machine `%s'\n"), string);
}
