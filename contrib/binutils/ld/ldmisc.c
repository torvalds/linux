/* ldmisc.c
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Written by Steve Chamberlain of Cygnus Support.

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
#include "bfdlink.h"
#include "libiberty.h"
#include "demangle.h"
#include <stdarg.h>
#include "ld.h"
#include "ldmisc.h"
#include "ldexp.h"
#include "ldlang.h"
#include <ldgram.h>
#include "ldlex.h"
#include "ldmain.h"
#include "ldfile.h"
#include "elf-bfd.h"

/*
 %% literal %
 %A section name from a section
 %B filename from a bfd
 %C clever filename:linenumber with function
 %D like %C, but no function name
 %E current bfd error or errno
 %F error is fatal
 %G like %D, but only function name
 %I filename from a lang_input_statement_type
 %P print program name
 %R info about a relent
 %S print script file and linenumber
 %T symbol name
 %V hex bfd_vma
 %W hex bfd_vma with 0x with no leading zeros taking up 8 spaces
 %X no object output, fail return
 %d integer, like printf
 %ld long, like printf
 %lu unsigned long, like printf
 %s arbitrary string, like printf
 %u integer, like printf
 %v hex bfd_vma, no leading zeros
*/

static void
vfinfo (FILE *fp, const char *fmt, va_list arg, bfd_boolean is_warning)
{
  bfd_boolean fatal = FALSE;

  while (*fmt != '\0')
    {
      while (*fmt != '%' && *fmt != '\0')
	{
	  putc (*fmt, fp);
	  fmt++;
	}

      if (*fmt == '%')
	{
	  fmt++;
	  switch (*fmt++)
	    {
	    case '%':
	      /* literal % */
	      putc ('%', fp);
	      break;

	    case 'X':
	      /* no object output, fail return */
	      config.make_executable = FALSE;
	      break;

	    case 'V':
	      /* hex bfd_vma */
	      {
		bfd_vma value = va_arg (arg, bfd_vma);
		fprintf_vma (fp, value);
	      }
	      break;

	    case 'v':
	      /* hex bfd_vma, no leading zeros */
	      {
		char buf[100];
		char *p = buf;
		bfd_vma value = va_arg (arg, bfd_vma);
		sprintf_vma (p, value);
		while (*p == '0')
		  p++;
		if (!*p)
		  p--;
		fputs (p, fp);
	      }
	      break;

	    case 'W':
	      /* hex bfd_vma with 0x with no leading zeroes taking up
		 8 spaces.  */
	      {
		char buf[100];
		bfd_vma value;
		char *p;
		int len;

		value = va_arg (arg, bfd_vma);
		sprintf_vma (buf, value);
		for (p = buf; *p == '0'; ++p)
		  ;
		if (*p == '\0')
		  --p;
		len = strlen (p);
		while (len < 8)
		  {
		    putc (' ', fp);
		    ++len;
		  }
		fprintf (fp, "0x%s", p);
	      }
	      break;

	    case 'T':
	      /* Symbol name.  */
	      {
		const char *name = va_arg (arg, const char *);

		if (name == NULL || *name == 0)
		  {
		    fprintf (fp, _("no symbol"));
		    break;
		  }
		else if (demangling)
		  {
		    char *demangled;

		    demangled = bfd_demangle (output_bfd, name,
					      DMGL_ANSI | DMGL_PARAMS);
		    if (demangled != NULL)
		      {
			fprintf (fp, "%s", demangled);
			free (demangled);
			break;
		      }
		  }
		fprintf (fp, "%s", name);
	      }
	      break;

	    case 'A':
	      /* section name from a section */
	      {
		asection *sec = va_arg (arg, asection *);
		bfd *abfd = sec->owner;
		const char *group = NULL;
		struct coff_comdat_info *ci;

		fprintf (fp, "%s", sec->name);
		if (abfd != NULL
		    && bfd_get_flavour (abfd) == bfd_target_elf_flavour
		    && elf_next_in_group (sec) != NULL
		    && (sec->flags & SEC_GROUP) == 0)
		  group = elf_group_name (sec);
		else if (abfd != NULL
			 && bfd_get_flavour (abfd) == bfd_target_coff_flavour
			 && (ci = bfd_coff_get_comdat_section (sec->owner,
							       sec)) != NULL)
		  group = ci->name;
		if (group != NULL)
		  fprintf (fp, "[%s]", group);
	      }
	      break;

	    case 'B':
	      /* filename from a bfd */
	      {
		bfd *abfd = va_arg (arg, bfd *);

		if (abfd == NULL)
		  fprintf (fp, "%s generated", program_name);
		else if (abfd->my_archive)
		  fprintf (fp, "%s(%s)", abfd->my_archive->filename,
			   abfd->filename);
		else
		  fprintf (fp, "%s", abfd->filename);
	      }
	      break;

	    case 'F':
	      /* Error is fatal.  */
	      fatal = TRUE;
	      break;

	    case 'P':
	      /* Print program name.  */
	      fprintf (fp, "%s", program_name);
	      break;

	    case 'E':
	      /* current bfd error or errno */
	      fprintf (fp, "%s", bfd_errmsg (bfd_get_error ()));
	      break;

	    case 'I':
	      /* filename from a lang_input_statement_type */
	      {
		lang_input_statement_type *i;

		i = va_arg (arg, lang_input_statement_type *);
		if (bfd_my_archive (i->the_bfd) != NULL)
		  fprintf (fp, "(%s)",
			   bfd_get_filename (bfd_my_archive (i->the_bfd)));
		fprintf (fp, "%s", i->local_sym_name);
		if (bfd_my_archive (i->the_bfd) == NULL
		    && strcmp (i->local_sym_name, i->filename) != 0)
		  fprintf (fp, " (%s)", i->filename);
	      }
	      break;

	    case 'S':
	      /* Print script file and linenumber.  */
	      if (parsing_defsym)
		fprintf (fp, "--defsym %s", lex_string);
	      else if (ldfile_input_filename != NULL)
		fprintf (fp, "%s:%u", ldfile_input_filename, lineno);
	      else
		fprintf (fp, _("built in linker script:%u"), lineno);
	      break;

	    case 'R':
	      /* Print all that's interesting about a relent.  */
	      {
		arelent *relent = va_arg (arg, arelent *);

		lfinfo (fp, "%s+0x%v (type %s)",
			(*(relent->sym_ptr_ptr))->name,
			relent->addend,
			relent->howto->name);
	      }
	      break;

	    case 'C':
	    case 'D':
	    case 'G':
	      /* Clever filename:linenumber with function name if possible.
		 The arguments are a BFD, a section, and an offset.  */
	      {
		static bfd *last_bfd;
		static char *last_file = NULL;
		static char *last_function = NULL;
		bfd *abfd;
		asection *section;
		bfd_vma offset;
		lang_input_statement_type *entry;
		asymbol **asymbols;
		const char *filename;
		const char *functionname;
		unsigned int linenumber;
		bfd_boolean discard_last;

		abfd = va_arg (arg, bfd *);
		section = va_arg (arg, asection *);
		offset = va_arg (arg, bfd_vma);

		if (abfd == NULL)
		  {
		    entry = NULL;
		    asymbols = NULL;
		  }
		else
		  {
		    entry = (lang_input_statement_type *) abfd->usrdata;
		    if (entry != (lang_input_statement_type *) NULL
			&& entry->asymbols != (asymbol **) NULL)
		      asymbols = entry->asymbols;
		    else
		      {
			long symsize;
			long sym_count;

			symsize = bfd_get_symtab_upper_bound (abfd);
			if (symsize < 0)
			  einfo (_("%B%F: could not read symbols\n"), abfd);
			asymbols = xmalloc (symsize);
			sym_count = bfd_canonicalize_symtab (abfd, asymbols);
			if (sym_count < 0)
			  einfo (_("%B%F: could not read symbols\n"), abfd);
			if (entry != (lang_input_statement_type *) NULL)
			  {
			    entry->asymbols = asymbols;
			    entry->symbol_count = sym_count;
			  }
		      }
		  }

		/* The GNU Coding Standard requires that error messages
		   be of the form:
		   
		     source-file-name:lineno: message

		   We do not always have a line number available so if
		   we cannot find them we print out the section name and
		   offset instread.  */
		discard_last = TRUE;
		if (abfd != NULL
		    && bfd_find_nearest_line (abfd, section, asymbols, offset,
					      &filename, &functionname,
					      &linenumber))
		  {
		    if (functionname != NULL && fmt[-1] == 'C')
		      {
			/* Detect the case where we are printing out a
			   message for the same function as the last
			   call to vinfo ("%C").  In this situation do
			   not print out the ABFD filename or the
			   function name again.  Note - we do still
			   print out the source filename, as this will
			   allow programs that parse the linker's output
			   (eg emacs) to correctly locate multiple
			   errors in the same source file.  */
			if (last_bfd == NULL
			    || last_file == NULL
			    || last_function == NULL
			    || last_bfd != abfd
			    || (filename != NULL
				&& strcmp (last_file, filename) != 0)
			    || strcmp (last_function, functionname) != 0)
			  {
			    lfinfo (fp, _("%B: In function `%T':\n"),
				    abfd, functionname);

			    last_bfd = abfd;
			    if (last_file != NULL)
			      free (last_file);
			    last_file = NULL;
			    if (filename)
			      last_file = xstrdup (filename);
			    if (last_function != NULL)
			      free (last_function);
			    last_function = xstrdup (functionname);
			  }
			discard_last = FALSE;
		      }
		    else
		      lfinfo (fp, "%B:", abfd);

		    if (filename != NULL)
		      fprintf (fp, "%s:", filename);

		    if (functionname != NULL && fmt[-1] == 'G')
		      lfinfo (fp, "%T", functionname);
		    else if (filename != NULL && linenumber != 0)
		      fprintf (fp, "%u", linenumber);
		    else
		      lfinfo (fp, "(%A+0x%v)", section, offset);
		  }
		else
		  lfinfo (fp, "%B:(%A+0x%v)", abfd, section, offset);

		if (asymbols != NULL && entry == NULL)
		  free (asymbols);

		if (discard_last)
		  {
		    last_bfd = NULL;
		    if (last_file != NULL)
		      {
			free (last_file);
			last_file = NULL;
		      }
		    if (last_function != NULL)
		      {
			free (last_function);
			last_function = NULL;
		      }
		  }
	      }
	      break;

	    case 's':
	      /* arbitrary string, like printf */
	      fprintf (fp, "%s", va_arg (arg, char *));
	      break;

	    case 'd':
	      /* integer, like printf */
	      fprintf (fp, "%d", va_arg (arg, int));
	      break;

	    case 'u':
	      /* unsigned integer, like printf */
	      fprintf (fp, "%u", va_arg (arg, unsigned int));
	      break;

	    case 'l':
	      if (*fmt == 'd')
		{
		  fprintf (fp, "%ld", va_arg (arg, long));
		  ++fmt;
		  break;
		}
	      else if (*fmt == 'u')
		{
		  fprintf (fp, "%lu", va_arg (arg, unsigned long));
		  ++fmt;
		  break;
		}
	      /* Fall thru */

	    default:
	      fprintf (fp, "%%%c", fmt[-1]);
	      break;
	    }
	}
    }

  if (is_warning && config.fatal_warnings)
    config.make_executable = FALSE;

  if (fatal)
    xexit (1);
}

/* Format info message and print on stdout.  */

/* (You would think this should be called just "info", but then you
   would be hosed by LynxOS, which defines that name in its libc.)  */

void
info_msg (const char *fmt, ...)
{
  va_list arg;

  va_start (arg, fmt);
  vfinfo (stdout, fmt, arg, FALSE);
  va_end (arg);
}

/* ('e' for error.) Format info message and print on stderr.  */

void
einfo (const char *fmt, ...)
{
  va_list arg;

  va_start (arg, fmt);
  vfinfo (stderr, fmt, arg, TRUE);
  va_end (arg);
}

void
info_assert (const char *file, unsigned int line)
{
  einfo (_("%F%P: internal error %s %d\n"), file, line);
}

/* ('m' for map) Format info message and print on map.  */

void
minfo (const char *fmt, ...)
{
  if (config.map_file != NULL)
    {
      va_list arg;

      va_start (arg, fmt);
      vfinfo (config.map_file, fmt, arg, FALSE);
      va_end (arg);
    }
}

void
lfinfo (FILE *file, const char *fmt, ...)
{
  va_list arg;

  va_start (arg, fmt);
  vfinfo (file, fmt, arg, FALSE);
  va_end (arg);
}

/* Functions to print the link map.  */

void
print_space (void)
{
  fprintf (config.map_file, " ");
}

void
print_nl (void)
{
  fprintf (config.map_file, "\n");
}

/* A more or less friendly abort message.  In ld.h abort is defined to
   call this function.  */

void
ld_abort (const char *file, int line, const char *fn)
{
  if (fn != NULL)
    einfo (_("%P: internal error: aborting at %s line %d in %s\n"),
	   file, line, fn);
  else
    einfo (_("%P: internal error: aborting at %s line %d\n"),
	   file, line);
  einfo (_("%P%F: please report this bug\n"));
  xexit (1);
}
