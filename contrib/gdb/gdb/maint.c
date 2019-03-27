/* Support for GDB maintenance commands.

   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1999, 2000, 2001,
   2002, 2003, 2004 Free Software Foundation, Inc.

   Written by Fred Fish at Cygnus Support.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */


#include "defs.h"
#include <ctype.h>
#include <signal.h>
#include "command.h"
#include "gdbcmd.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "demangle.h"
#include "gdbcore.h"
#include "expression.h"		/* For language.h */
#include "language.h"
#include "symfile.h"
#include "objfiles.h"
#include "value.h"

#include "cli/cli-decode.h"

extern void _initialize_maint_cmds (void);

static void maintenance_command (char *, int);

static void maintenance_dump_me (char *, int);

static void maintenance_internal_error (char *args, int from_tty);

static void maintenance_demangle (char *, int);

static void maintenance_time_display (char *, int);

static void maintenance_space_display (char *, int);

static void maintenance_info_command (char *, int);

static void maintenance_info_sections (char *, int);

static void maintenance_print_command (char *, int);

static void maintenance_do_deprecate (char *, int);

/* Set this to the maximum number of seconds to wait instead of waiting forever
   in target_wait().  If this timer times out, then it generates an error and
   the command is aborted.  This replaces most of the need for timeouts in the
   GDB test suite, and makes it possible to distinguish between a hung target
   and one with slow communications.  */

int watchdog = 0;

/*

   LOCAL FUNCTION

   maintenance_command -- access the maintenance subcommands

   SYNOPSIS

   void maintenance_command (char *args, int from_tty)

   DESCRIPTION

 */

static void
maintenance_command (char *args, int from_tty)
{
  printf_unfiltered ("\"maintenance\" must be followed by the name of a maintenance command.\n");
  help_list (maintenancelist, "maintenance ", -1, gdb_stdout);
}

#ifndef _WIN32
static void
maintenance_dump_me (char *args, int from_tty)
{
  if (query ("Should GDB dump core? "))
    {
#ifdef __DJGPP__
      /* SIGQUIT by default is ignored, so use SIGABRT instead.  */
      signal (SIGABRT, SIG_DFL);
      kill (getpid (), SIGABRT);
#else
      signal (SIGQUIT, SIG_DFL);
      kill (getpid (), SIGQUIT);
#endif
    }
}
#endif

/* Stimulate the internal error mechanism that GDB uses when an
   internal problem is detected.  Allows testing of the mechanism.
   Also useful when the user wants to drop a core file but not exit
   GDB. */

static void
maintenance_internal_error (char *args, int from_tty)
{
  internal_error (__FILE__, __LINE__, "%s", (args == NULL ? "" : args));
}

/* Stimulate the internal error mechanism that GDB uses when an
   internal problem is detected.  Allows testing of the mechanism.
   Also useful when the user wants to drop a core file but not exit
   GDB. */

static void
maintenance_internal_warning (char *args, int from_tty)
{
  internal_warning (__FILE__, __LINE__, "%s", (args == NULL ? "" : args));
}

/* Someday we should allow demangling for things other than just
   explicit strings.  For example, we might want to be able to specify
   the address of a string in either GDB's process space or the
   debuggee's process space, and have gdb fetch and demangle that
   string.  If we have a char* pointer "ptr" that points to a string,
   we might want to be able to given just the name and have GDB
   demangle and print what it points to, etc.  (FIXME) */

static void
maintenance_demangle (char *args, int from_tty)
{
  char *demangled;

  if (args == NULL || *args == '\0')
    {
      printf_unfiltered ("\"maintenance demangle\" takes an argument to demangle.\n");
    }
  else
    {
      demangled = language_demangle (current_language, args, 
				     DMGL_ANSI | DMGL_PARAMS);
      if (demangled != NULL)
	{
	  printf_unfiltered ("%s\n", demangled);
	  xfree (demangled);
	}
      else
	{
	  printf_unfiltered ("Can't demangle \"%s\"\n", args);
	}
    }
}

static void
maintenance_time_display (char *args, int from_tty)
{
  extern int display_time;

  if (args == NULL || *args == '\0')
    printf_unfiltered ("\"maintenance time\" takes a numeric argument.\n");
  else
    display_time = strtol (args, NULL, 10);
}

static void
maintenance_space_display (char *args, int from_tty)
{
  extern int display_space;

  if (args == NULL || *args == '\0')
    printf_unfiltered ("\"maintenance space\" takes a numeric argument.\n");
  else
    display_space = strtol (args, NULL, 10);
}

/* The "maintenance info" command is defined as a prefix, with
   allow_unknown 0.  Therefore, its own definition is called only for
   "maintenance info" with no args.  */

static void
maintenance_info_command (char *arg, int from_tty)
{
  printf_unfiltered ("\"maintenance info\" must be followed by the name of an info command.\n");
  help_list (maintenanceinfolist, "maintenance info ", -1, gdb_stdout);
}

/* Mini tokenizing lexer for 'maint info sections' command.  */

static int
match_substring (const char *string, const char *substr)
{
  int substr_len = strlen(substr);
  const char *tok;

  while ((tok = strstr (string, substr)) != NULL)
    {
      /* Got a partial match.  Is it a whole word? */
      if (tok == string
	  || tok[-1] == ' '
	  || tok[-1] == '\t')
      {
	/* Token is delimited at the front... */
	if (tok[substr_len] == ' '
	    || tok[substr_len] == '\t'
	    || tok[substr_len] == '\0')
	{
	  /* Token is delimited at the rear.  Got a whole-word match.  */
	  return 1;
	}
      }
      /* Token didn't match as a whole word.  Advance and try again.  */
      string = tok + 1;
    }
  return 0;
}

static int 
match_bfd_flags (char *string, flagword flags)
{
  if (flags & SEC_ALLOC)
    if (match_substring (string, "ALLOC"))
      return 1;
  if (flags & SEC_LOAD)
    if (match_substring (string, "LOAD"))
      return 1;
  if (flags & SEC_RELOC)
    if (match_substring (string, "RELOC"))
      return 1;
  if (flags & SEC_READONLY)
    if (match_substring (string, "READONLY"))
      return 1;
  if (flags & SEC_CODE)
    if (match_substring (string, "CODE"))
      return 1;
  if (flags & SEC_DATA)
    if (match_substring (string, "DATA"))
      return 1;
  if (flags & SEC_ROM)
    if (match_substring (string, "ROM"))
      return 1;
  if (flags & SEC_CONSTRUCTOR)
    if (match_substring (string, "CONSTRUCTOR"))
      return 1;
  if (flags & SEC_HAS_CONTENTS)
    if (match_substring (string, "HAS_CONTENTS"))
      return 1;
  if (flags & SEC_NEVER_LOAD)
    if (match_substring (string, "NEVER_LOAD"))
      return 1;
  if (flags & SEC_COFF_SHARED_LIBRARY)
    if (match_substring (string, "COFF_SHARED_LIBRARY"))
      return 1;
  if (flags & SEC_IS_COMMON)
    if (match_substring (string, "IS_COMMON"))
      return 1;

  return 0;
}

static void
print_bfd_flags (flagword flags)
{
  if (flags & SEC_ALLOC)
    printf_filtered (" ALLOC");
  if (flags & SEC_LOAD)
    printf_filtered (" LOAD");
  if (flags & SEC_RELOC)
    printf_filtered (" RELOC");
  if (flags & SEC_READONLY)
    printf_filtered (" READONLY");
  if (flags & SEC_CODE)
    printf_filtered (" CODE");
  if (flags & SEC_DATA)
    printf_filtered (" DATA");
  if (flags & SEC_ROM)
    printf_filtered (" ROM");
  if (flags & SEC_CONSTRUCTOR)
    printf_filtered (" CONSTRUCTOR");
  if (flags & SEC_HAS_CONTENTS)
    printf_filtered (" HAS_CONTENTS");
  if (flags & SEC_NEVER_LOAD)
    printf_filtered (" NEVER_LOAD");
  if (flags & SEC_COFF_SHARED_LIBRARY)
    printf_filtered (" COFF_SHARED_LIBRARY");
  if (flags & SEC_IS_COMMON)
    printf_filtered (" IS_COMMON");
}

static void
maint_print_section_info (const char *name, flagword flags, 
			  CORE_ADDR addr, CORE_ADDR endaddr, 
			  unsigned long filepos)
{
  /* FIXME-32x64: Need print_address_numeric with field width.  */
  printf_filtered ("    0x%s", paddr (addr));
  printf_filtered ("->0x%s", paddr (endaddr));
  printf_filtered (" at %s",
		   local_hex_string_custom ((unsigned long) filepos, "08l"));
  printf_filtered (": %s", name);
  print_bfd_flags (flags);
  printf_filtered ("\n");
}

static void
print_bfd_section_info (bfd *abfd, 
			asection *asect, 
			void *arg)
{
  flagword flags = bfd_get_section_flags (abfd, asect);
  const char *name = bfd_section_name (abfd, asect);

  if (arg == NULL || *((char *) arg) == '\0'
      || match_substring ((char *) arg, name)
      || match_bfd_flags ((char *) arg, flags))
    {
      CORE_ADDR addr, endaddr;

      addr = bfd_section_vma (abfd, asect);
      endaddr = addr + bfd_section_size (abfd, asect);
      maint_print_section_info (name, flags, addr, endaddr, asect->filepos);
    }
}

static void
print_objfile_section_info (bfd *abfd, 
			    struct obj_section *asect, 
			    char *string)
{
  flagword flags = bfd_get_section_flags (abfd, asect->the_bfd_section);
  const char *name = bfd_section_name (abfd, asect->the_bfd_section);

  if (string == NULL || *string == '\0'
      || match_substring (string, name)
      || match_bfd_flags (string, flags))
    {
      maint_print_section_info (name, flags, asect->addr, asect->endaddr, 
			  asect->the_bfd_section->filepos);
    }
}

static void
maintenance_info_sections (char *arg, int from_tty)
{
  if (exec_bfd)
    {
      printf_filtered ("Exec file:\n");
      printf_filtered ("    `%s', ", bfd_get_filename (exec_bfd));
      wrap_here ("        ");
      printf_filtered ("file type %s.\n", bfd_get_target (exec_bfd));
      if (arg && *arg && match_substring (arg, "ALLOBJ"))
	{
	  struct objfile *ofile;
	  struct obj_section *osect;

	  /* Only this function cares about the 'ALLOBJ' argument; 
	     if 'ALLOBJ' is the only argument, discard it rather than
	     passing it down to print_objfile_section_info (which 
	     wouldn't know how to handle it).  */
	  if (strcmp (arg, "ALLOBJ") == 0)
	    arg = NULL;

	  ALL_OBJFILES (ofile)
	    {
	      printf_filtered ("  Object file: %s\n", 
			       bfd_get_filename (ofile->obfd));
	      ALL_OBJFILE_OSECTIONS (ofile, osect)
		{
		  print_objfile_section_info (ofile->obfd, osect, arg);
		}
	    }
	}
      else 
	bfd_map_over_sections (exec_bfd, print_bfd_section_info, arg);
    }

  if (core_bfd)
    {
      printf_filtered ("Core file:\n");
      printf_filtered ("    `%s', ", bfd_get_filename (core_bfd));
      wrap_here ("        ");
      printf_filtered ("file type %s.\n", bfd_get_target (core_bfd));
      bfd_map_over_sections (core_bfd, print_bfd_section_info, arg);
    }
}

void
maintenance_print_statistics (char *args, int from_tty)
{
  print_objfile_statistics ();
  print_symbol_bcache_statistics ();
}

static void
maintenance_print_architecture (char *args, int from_tty)
{
  if (args == NULL)
    gdbarch_dump (current_gdbarch, gdb_stdout);
  else
    {
      struct ui_file *file = gdb_fopen (args, "w");
      if (file == NULL)
	perror_with_name ("maintenance print architecture");
      gdbarch_dump (current_gdbarch, file);    
      ui_file_delete (file);
    }
}

/* The "maintenance print" command is defined as a prefix, with
   allow_unknown 0.  Therefore, its own definition is called only for
   "maintenance print" with no args.  */

static void
maintenance_print_command (char *arg, int from_tty)
{
  printf_unfiltered ("\"maintenance print\" must be followed by the name of a print command.\n");
  help_list (maintenanceprintlist, "maintenance print ", -1, gdb_stdout);
}

/* The "maintenance translate-address" command converts a section and address
   to a symbol.  This can be called in two ways:
   maintenance translate-address <secname> <addr>
   or   maintenance translate-address <addr>
 */

static void
maintenance_translate_address (char *arg, int from_tty)
{
  CORE_ADDR address;
  asection *sect;
  char *p;
  struct minimal_symbol *sym;
  struct objfile *objfile;

  if (arg == NULL || *arg == 0)
    error ("requires argument (address or section + address)");

  sect = NULL;
  p = arg;

  if (!isdigit (*p))
    {				/* See if we have a valid section name */
      while (*p && !isspace (*p))	/* Find end of section name */
	p++;
      if (*p == '\000')		/* End of command? */
	error ("Need to specify <section-name> and <address>");
      *p++ = '\000';
      while (isspace (*p))
	p++;			/* Skip whitespace */

      ALL_OBJFILES (objfile)
      {
	sect = bfd_get_section_by_name (objfile->obfd, arg);
	if (sect != NULL)
	  break;
      }

      if (!sect)
	error ("Unknown section %s.", arg);
    }

  address = parse_and_eval_address (p);

  if (sect)
    sym = lookup_minimal_symbol_by_pc_section (address, sect);
  else
    sym = lookup_minimal_symbol_by_pc (address);

  if (sym)
    printf_filtered ("%s+%s\n",
		     SYMBOL_PRINT_NAME (sym),
		     paddr_u (address - SYMBOL_VALUE_ADDRESS (sym)));
  else if (sect)
    printf_filtered ("no symbol at %s:0x%s\n", sect->name, paddr (address));
  else
    printf_filtered ("no symbol at 0x%s\n", paddr (address));

  return;
}


/* When a command is deprecated the user will be warned the first time
   the command is used.  If possible, a replacement will be
   offered. */

static void
maintenance_deprecate (char *args, int from_tty)
{
  if (args == NULL || *args == '\0')
    {
      printf_unfiltered ("\"maintenance deprecate\" takes an argument, \n\
the command you want to deprecate, and optionally the replacement command \n\
enclosed in quotes.\n");
    }

  maintenance_do_deprecate (args, 1);

}


static void
maintenance_undeprecate (char *args, int from_tty)
{
  if (args == NULL || *args == '\0')
    {
      printf_unfiltered ("\"maintenance undeprecate\" takes an argument, \n\
the command you want to undeprecate.\n");
    }

  maintenance_do_deprecate (args, 0);

}

/* You really shouldn't be using this. It is just for the testsuite.
   Rather, you should use deprecate_cmd() when the command is created
   in _initialize_blah().

   This function deprecates a command and optionally assigns it a
   replacement.  */

static void
maintenance_do_deprecate (char *text, int deprecate)
{

  struct cmd_list_element *alias = NULL;
  struct cmd_list_element *prefix_cmd = NULL;
  struct cmd_list_element *cmd = NULL;

  char *start_ptr = NULL;
  char *end_ptr = NULL;
  int len;
  char *replacement = NULL;

  if (text == NULL)
    return;

  if (!lookup_cmd_composition (text, &alias, &prefix_cmd, &cmd))
    {
      printf_filtered ("Can't find command '%s' to deprecate.\n", text);
      return;
    }

  if (deprecate)
    {
      /* look for a replacement command */
      start_ptr = strchr (text, '\"');
      if (start_ptr != NULL)
	{
	  start_ptr++;
	  end_ptr = strrchr (start_ptr, '\"');
	  if (end_ptr != NULL)
	    {
	      len = end_ptr - start_ptr;
	      start_ptr[len] = '\0';
	      replacement = xstrdup (start_ptr);
	    }
	}
    }

  if (!start_ptr || !end_ptr)
    replacement = NULL;


  /* If they used an alias, we only want to deprecate the alias.

     Note the MALLOCED_REPLACEMENT test.  If the command's replacement
     string was allocated at compile time we don't want to free the
     memory. */
  if (alias)
    {

      if (alias->flags & MALLOCED_REPLACEMENT)
	xfree (alias->replacement);

      if (deprecate)
	alias->flags |= (DEPRECATED_WARN_USER | CMD_DEPRECATED);
      else
	alias->flags &= ~(DEPRECATED_WARN_USER | CMD_DEPRECATED);
      alias->replacement = replacement;
      alias->flags |= MALLOCED_REPLACEMENT;
      return;
    }
  else if (cmd)
    {
      if (cmd->flags & MALLOCED_REPLACEMENT)
	xfree (cmd->replacement);

      if (deprecate)
	cmd->flags |= (DEPRECATED_WARN_USER | CMD_DEPRECATED);
      else
	cmd->flags &= ~(DEPRECATED_WARN_USER | CMD_DEPRECATED);
      cmd->replacement = replacement;
      cmd->flags |= MALLOCED_REPLACEMENT;
      return;
    }
}

/* Maintenance set/show framework.  */

static struct cmd_list_element *maintenance_set_cmdlist;
static struct cmd_list_element *maintenance_show_cmdlist;

static void
maintenance_set_cmd (char *args, int from_tty)
{
  printf_unfiltered ("\"maintenance set\" must be followed by the name of a set command.\n");
  help_list (maintenance_set_cmdlist, "maintenance set ", -1, gdb_stdout);
}

static void
maintenance_show_cmd (char *args, int from_tty)
{
  cmd_show_list (maintenance_show_cmdlist, from_tty, "");
}

/* Profiling support.  */

static int maintenance_profile_p;

#if defined (HAVE_MONSTARTUP) && defined (HAVE__MCLEANUP)

#ifdef HAVE__ETEXT
extern char _etext;
#define TEXTEND &_etext
#else
extern char etext;
#define TEXTEND &etext
#endif

static int profiling_state;

static void
mcleanup_wrapper (void)
{
  extern void _mcleanup (void);

  if (profiling_state)
    _mcleanup ();
}

static void
maintenance_set_profile_cmd (char *args, int from_tty, struct cmd_list_element *c)
{
  if (maintenance_profile_p == profiling_state)
    return;

  profiling_state = maintenance_profile_p;

  if (maintenance_profile_p)
    {
      static int profiling_initialized;

      extern void monstartup (unsigned long, unsigned long);
      extern int main();

      if (!profiling_initialized)
	{
	  atexit (mcleanup_wrapper);
	  profiling_initialized = 1;
	}

      /* "main" is now always the first function in the text segment, so use
	 its address for monstartup.  */
      monstartup ((unsigned long) &main, (unsigned long) TEXTEND);
    }
  else
    {
      extern void _mcleanup (void);
      _mcleanup ();
    }
}
#else
static void
maintenance_set_profile_cmd (char *args, int from_tty, struct cmd_list_element *c)
{
  error ("Profiling support is not available on this system.");
}
#endif

void
_initialize_maint_cmds (void)
{
  struct cmd_list_element *tmpcmd;

  add_prefix_cmd ("maintenance", class_maintenance, maintenance_command,
		  "Commands for use by GDB maintainers.\n\
Includes commands to dump specific internal GDB structures in\n\
a human readable form, to cause GDB to deliberately dump core,\n\
to test internal functions such as the C++/ObjC demangler, etc.",
		  &maintenancelist, "maintenance ", 0,
		  &cmdlist);

  add_com_alias ("mt", "maintenance", class_maintenance, 1);

  add_prefix_cmd ("info", class_maintenance, maintenance_info_command,
     "Commands for showing internal info about the program being debugged.",
		  &maintenanceinfolist, "maintenance info ", 0,
		  &maintenancelist);
  add_alias_cmd ("i", "info", class_maintenance, 1, &maintenancelist);

  add_cmd ("sections", class_maintenance, maintenance_info_sections,
	   "List the BFD sections of the exec and core files. \n\
Arguments may be any combination of:\n\
	[one or more section names]\n\
	ALLOC LOAD RELOC READONLY CODE DATA ROM CONSTRUCTOR\n\
	HAS_CONTENTS NEVER_LOAD COFF_SHARED_LIBRARY IS_COMMON\n\
Sections matching any argument will be listed (no argument\n\
implies all sections).  In addition, the special argument\n\
	ALLOBJ\n\
lists all sections from all object files, including shared libraries.",
	   &maintenanceinfolist);

  add_prefix_cmd ("print", class_maintenance, maintenance_print_command,
		  "Maintenance command for printing GDB internal state.",
		  &maintenanceprintlist, "maintenance print ", 0,
		  &maintenancelist);

  add_prefix_cmd ("set", class_maintenance, maintenance_set_cmd, "\
Set GDB internal variables used by the GDB maintainer.\n\
Configure variables internal to GDB that aid in GDB's maintenance",
		  &maintenance_set_cmdlist, "maintenance set ",
		  0/*allow-unknown*/,
		  &maintenancelist);

  add_prefix_cmd ("show", class_maintenance, maintenance_show_cmd, "\
Show GDB internal variables used by the GDB maintainer.\n\
Configure variables internal to GDB that aid in GDB's maintenance",
		  &maintenance_show_cmdlist, "maintenance show ",
		  0/*allow-unknown*/,
		  &maintenancelist);

#ifndef _WIN32
  add_cmd ("dump-me", class_maintenance, maintenance_dump_me,
	   "Get fatal error; make debugger dump its core.\n\
GDB sets its handling of SIGQUIT back to SIG_DFL and then sends\n\
itself a SIGQUIT signal.",
	   &maintenancelist);
#endif

  add_cmd ("internal-error", class_maintenance, maintenance_internal_error,
	   "Give GDB an internal error.\n\
Cause GDB to behave as if an internal error was detected.",
	   &maintenancelist);

  add_cmd ("internal-warning", class_maintenance, maintenance_internal_warning,
	   "Give GDB an internal warning.\n\
Cause GDB to behave as if an internal warning was reported.",
	   &maintenancelist);

  add_cmd ("demangle", class_maintenance, maintenance_demangle,
	   "Demangle a C++/ObjC mangled name.\n\
Call internal GDB demangler routine to demangle a C++ link name\n\
and prints the result.",
	   &maintenancelist);

  add_cmd ("time", class_maintenance, maintenance_time_display,
	   "Set the display of time usage.\n\
If nonzero, will cause the execution time for each command to be\n\
displayed, following the command's output.",
	   &maintenancelist);

  add_cmd ("space", class_maintenance, maintenance_space_display,
	   "Set the display of space usage.\n\
If nonzero, will cause the execution space for each command to be\n\
displayed, following the command's output.",
	   &maintenancelist);

  add_cmd ("type", class_maintenance, maintenance_print_type,
	   "Print a type chain for a given symbol.\n\
For each node in a type chain, print the raw data for each member of\n\
the type structure, and the interpretation of the data.",
	   &maintenanceprintlist);

  add_cmd ("symbols", class_maintenance, maintenance_print_symbols,
	   "Print dump of current symbol definitions.\n\
Entries in the full symbol table are dumped to file OUTFILE.\n\
If a SOURCE file is specified, dump only that file's symbols.",
	   &maintenanceprintlist);

  add_cmd ("msymbols", class_maintenance, maintenance_print_msymbols,
	   "Print dump of current minimal symbol definitions.\n\
Entries in the minimal symbol table are dumped to file OUTFILE.\n\
If a SOURCE file is specified, dump only that file's minimal symbols.",
	   &maintenanceprintlist);

  add_cmd ("psymbols", class_maintenance, maintenance_print_psymbols,
	   "Print dump of current partial symbol definitions.\n\
Entries in the partial symbol table are dumped to file OUTFILE.\n\
If a SOURCE file is specified, dump only that file's partial symbols.",
	   &maintenanceprintlist);

  add_cmd ("objfiles", class_maintenance, maintenance_print_objfiles,
	   "Print dump of current object file definitions.",
	   &maintenanceprintlist);

  add_cmd ("symtabs", class_maintenance, maintenance_info_symtabs,
	   "List the full symbol tables for all object files.\n\
This does not include information about individual symbols, blocks, or\n\
linetables --- just the symbol table structures themselves.\n\
With an argument REGEXP, list the symbol tables whose names that match that.",
	   &maintenanceinfolist);

  add_cmd ("psymtabs", class_maintenance, maintenance_info_psymtabs,
	   "List the partial symbol tables for all object files.\n\
This does not include information about individual partial symbols,\n\
just the symbol table structures themselves.",
	   &maintenanceinfolist);

  add_cmd ("statistics", class_maintenance, maintenance_print_statistics,
	   "Print statistics about internal gdb state.",
	   &maintenanceprintlist);

  add_cmd ("architecture", class_maintenance, maintenance_print_architecture,
	   "Print the internal architecture configuration.\
Takes an optional file parameter.",
	   &maintenanceprintlist);

  add_cmd ("check-symtabs", class_maintenance, maintenance_check_symtabs,
	   "Check consistency of psymtabs and symtabs.",
	   &maintenancelist);

  add_cmd ("translate-address", class_maintenance, maintenance_translate_address,
	   "Translate a section name and address to a symbol.",
	   &maintenancelist);

  add_cmd ("deprecate", class_maintenance, maintenance_deprecate,
	   "Deprecate a command.  Note that this is just in here so the \n\
testsuite can check the comamnd deprecator. You probably shouldn't use this,\n\
rather you should use the C function deprecate_cmd().  If you decide you \n\
want to use it: maintenance deprecate 'commandname' \"replacement\". The \n\
replacement is optional.", &maintenancelist);

  add_cmd ("undeprecate", class_maintenance, maintenance_undeprecate,
	   "Undeprecate a command.  Note that this is just in here so the \n\
testsuite can check the comamnd deprecator. You probably shouldn't use this,\n\
If you decide you want to use it: maintenance undeprecate 'commandname'",
	   &maintenancelist);

  add_show_from_set (
		      add_set_cmd ("watchdog", class_maintenance, var_zinteger, (char *) &watchdog,
				   "Set watchdog timer.\n\
When non-zero, this timeout is used instead of waiting forever for a target to\n\
finish a low-level step or continue operation.  If the specified amount of time\n\
passes without a response from the target, an error occurs.", &setlist),
		      &showlist);


  add_setshow_boolean_cmd ("profile", class_maintenance,
			   &maintenance_profile_p,
			   "Set internal profiling.\n"
			   "When enabled GDB is profiled.",
			   "Show internal profiling.\n",
			   maintenance_set_profile_cmd, NULL,
			   &maintenance_set_cmdlist,
			   &maintenance_show_cmdlist);
}
