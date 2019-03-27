/* size.c -- report size of various sections of an executable file.
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

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

/* Extensions/incompatibilities:
   o - BSD output has filenames at the end.
   o - BSD output can appear in different radicies.
   o - SysV output has less redundant whitespace.  Filename comes at end.
   o - SysV output doesn't show VMA which is always the same as the PMA.
   o - We also handle core files.
   o - We also handle archives.
   If you write shell scripts which manipulate this info then you may be
   out of luck; there's no --compatibility or --pedantic option.  */

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"
#include "getopt.h"
#include "bucomm.h"

#ifndef BSD_DEFAULT
#define BSD_DEFAULT 1
#endif

/* Program options.  */

enum
  {
    decimal, octal, hex
  }
radix = decimal;

/* 0 means use AT&T-style output.  */
static int berkeley_format = BSD_DEFAULT;

int show_version = 0;
int show_help = 0;
int show_totals = 0;

static bfd_size_type total_bsssize;
static bfd_size_type total_datasize;
static bfd_size_type total_textsize;

/* Program exit status.  */
int return_code = 0;

static char *target = NULL;

/* Static declarations.  */

static void usage (FILE *, int);
static void display_file (char *);
static void display_bfd (bfd *);
static void display_archive (bfd *);
static int size_number (bfd_size_type);
static void rprint_number (int, bfd_size_type);
static void print_berkeley_format (bfd *);
static void sysv_internal_sizer (bfd *, asection *, void *);
static void sysv_internal_printer (bfd *, asection *, void *);
static void print_sysv_format (bfd *);
static void print_sizes (bfd * file);
static void berkeley_sum (bfd *, sec_ptr, void *);

static void
usage (FILE *stream, int status)
{
  fprintf (stream, _("Usage: %s [option(s)] [file(s)]\n"), program_name);
  fprintf (stream, _(" Displays the sizes of sections inside binary files\n"));
  fprintf (stream, _(" If no input file(s) are specified, a.out is assumed\n"));
  fprintf (stream, _(" The options are:\n\
  -A|-B     --format={sysv|berkeley}  Select output style (default is %s)\n\
  -o|-d|-x  --radix={8|10|16}         Display numbers in octal, decimal or hex\n\
  -t        --totals                  Display the total sizes (Berkeley only)\n\
            --target=<bfdname>        Set the binary file format\n\
            @<file>                   Read options from <file>\n\
  -h        --help                    Display this information\n\
  -v        --version                 Display the program's version\n\
\n"),
#if BSD_DEFAULT
  "berkeley"
#else
  "sysv"
#endif
);
  list_supported_targets (program_name, stream);
  if (REPORT_BUGS_TO[0] && status == 0)
    fprintf (stream, _("Report bugs to %s\n"), REPORT_BUGS_TO);
  exit (status);
}

static struct option long_options[] =
{
  {"format", required_argument, 0, 200},
  {"radix", required_argument, 0, 201},
  {"target", required_argument, 0, 202},
  {"totals", no_argument, &show_totals, 1},
  {"version", no_argument, &show_version, 1},
  {"help", no_argument, &show_help, 1},
  {0, no_argument, 0, 0}
};

int main (int, char **);

int
main (int argc, char **argv)
{
  int temp;
  int c;

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  program_name = *argv;
  xmalloc_set_program_name (program_name);

  expandargv (&argc, &argv);

  bfd_init ();
  set_default_bfd_target ();

  while ((c = getopt_long (argc, argv, "ABHhVvdfotx", long_options,
			   (int *) 0)) != EOF)
    switch (c)
      {
      case 200:		/* --format */
	switch (*optarg)
	  {
	  case 'B':
	  case 'b':
	    berkeley_format = 1;
	    break;
	  case 'S':
	  case 's':
	    berkeley_format = 0;
	    break;
	  default:
	    non_fatal (_("invalid argument to --format: %s"), optarg);
	    usage (stderr, 1);
	  }
	break;

      case 202:		/* --target */
	target = optarg;
	break;

      case 201:		/* --radix */
#ifdef ANSI_LIBRARIES
	temp = strtol (optarg, NULL, 10);
#else
	temp = atol (optarg);
#endif
	switch (temp)
	  {
	  case 10:
	    radix = decimal;
	    break;
	  case 8:
	    radix = octal;
	    break;
	  case 16:
	    radix = hex;
	    break;
	  default:
	    non_fatal (_("Invalid radix: %s\n"), optarg);
	    usage (stderr, 1);
	  }
	break;

      case 'A':
	berkeley_format = 0;
	break;
      case 'B':
	berkeley_format = 1;
	break;
      case 'v':
      case 'V':
	show_version = 1;
	break;
      case 'd':
	radix = decimal;
	break;
      case 'x':
	radix = hex;
	break;
      case 'o':
	radix = octal;
	break;
      case 't':
	show_totals = 1;
	break;
      case 'f': /* FIXME : For sysv68, `-f' means `full format', i.e.
		   `[fname:] M(.text) + N(.data) + O(.bss) + P(.comment) = Q'
		   where `fname: ' appears only if there are >= 2 input files,
		   and M, N, O, P, Q are expressed in decimal by default,
		   hexa or octal if requested by `-x' or `-o'.
		   Just to make things interesting, Solaris also accepts -f,
		   which prints out the size of each allocatable section, the
		   name of the section, and the total of the section sizes.  */
		/* For the moment, accept `-f' silently, and ignore it.  */
	break;
      case 0:
	break;
      case 'h':
      case 'H':
      case '?':
	usage (stderr, 1);
      }

  if (show_version)
    print_version ("size");
  if (show_help)
    usage (stdout, 0);

  if (optind == argc)
    display_file ("a.out");
  else
    for (; optind < argc;)
      display_file (argv[optind++]);

  if (show_totals && berkeley_format)
    {
      bfd_size_type total = total_textsize + total_datasize + total_bsssize;

      rprint_number (7, total_textsize);
      putchar('\t');
      rprint_number (7, total_datasize);
      putchar('\t');
      rprint_number (7, total_bsssize);
      printf (((radix == octal) ? "\t%7lo\t%7lx\t" : "\t%7lu\t%7lx\t"),
	      (unsigned long) total, (unsigned long) total);
      fputs ("(TOTALS)\n", stdout);
    }

  return return_code;
}

/* Display stats on file or archive member ABFD.  */

static void
display_bfd (bfd *abfd)
{
  char **matching;

  if (bfd_check_format (abfd, bfd_archive))
    /* An archive within an archive.  */
    return;

  if (bfd_check_format_matches (abfd, bfd_object, &matching))
    {
      print_sizes (abfd);
      printf ("\n");
      return;
    }

  if (bfd_get_error () == bfd_error_file_ambiguously_recognized)
    {
      bfd_nonfatal (bfd_get_filename (abfd));
      list_matching_formats (matching);
      free (matching);
      return_code = 3;
      return;
    }

  if (bfd_check_format_matches (abfd, bfd_core, &matching))
    {
      const char *core_cmd;

      print_sizes (abfd);
      fputs (" (core file", stdout);

      core_cmd = bfd_core_file_failing_command (abfd);
      if (core_cmd)
	printf (" invoked as %s", core_cmd);

      puts (")\n");
      return;
    }

  bfd_nonfatal (bfd_get_filename (abfd));

  if (bfd_get_error () == bfd_error_file_ambiguously_recognized)
    {
      list_matching_formats (matching);
      free (matching);
    }

  return_code = 3;
}

static void
display_archive (bfd *file)
{
  bfd *arfile = (bfd *) NULL;
  bfd *last_arfile = (bfd *) NULL;

  for (;;)
    {
      bfd_set_error (bfd_error_no_error);

      arfile = bfd_openr_next_archived_file (file, arfile);
      if (arfile == NULL)
	{
	  if (bfd_get_error () != bfd_error_no_more_archived_files)
	    {
	      bfd_nonfatal (bfd_get_filename (file));
	      return_code = 2;
	    }
	  break;
	}

      display_bfd (arfile);

      if (last_arfile != NULL)
	bfd_close (last_arfile);
      last_arfile = arfile;
    }

  if (last_arfile != NULL)
    bfd_close (last_arfile);
}

static void
display_file (char *filename)
{
  bfd *file;

  if (get_file_size (filename) < 1)
    {
      return_code = 1;
      return;
    }

  file = bfd_openr (filename, target);
  if (file == NULL)
    {
      bfd_nonfatal (filename);
      return_code = 1;
      return;
    }

  if (bfd_check_format (file, bfd_archive))
    display_archive (file);
  else
    display_bfd (file);

  if (!bfd_close (file))
    {
      bfd_nonfatal (filename);
      return_code = 1;
      return;
    }
}

/* This is what lexical functions are for.  */

static int
size_number (bfd_size_type num)
{
  char buffer[40];

  sprintf (buffer,
	   (radix == decimal ? "%lu" :
	   ((radix == octal) ? "0%lo" : "0x%lx")),
	   (unsigned long) num);

  return strlen (buffer);
}

static void
rprint_number (int width, bfd_size_type num)
{
  char buffer[40];

  sprintf (buffer,
	   (radix == decimal ? "%lu" :
	   ((radix == octal) ? "0%lo" : "0x%lx")),
	   (unsigned long) num);

  printf ("%*s", width, buffer);
}

static bfd_size_type bsssize;
static bfd_size_type datasize;
static bfd_size_type textsize;

static void
berkeley_sum (bfd *abfd ATTRIBUTE_UNUSED, sec_ptr sec,
	      void *ignore ATTRIBUTE_UNUSED)
{
  flagword flags;
  bfd_size_type size;

  flags = bfd_get_section_flags (abfd, sec);
  if ((flags & SEC_ALLOC) == 0)
    return;

  size = bfd_get_section_size (sec);
  if ((flags & SEC_CODE) != 0 || (flags & SEC_READONLY) != 0)
    textsize += size;
  else if ((flags & SEC_HAS_CONTENTS) != 0)
    datasize += size;
  else
    bsssize += size;
}

static void
print_berkeley_format (bfd *abfd)
{
  static int files_seen = 0;
  bfd_size_type total;

  bsssize = 0;
  datasize = 0;
  textsize = 0;

  bfd_map_over_sections (abfd, berkeley_sum, NULL);

  if (files_seen++ == 0)
    puts ((radix == octal) ? "   text\t   data\t    bss\t    oct\t    hex\tfilename" :
	  "   text\t   data\t    bss\t    dec\t    hex\tfilename");

  total = textsize + datasize + bsssize;

  if (show_totals)
    {
      total_textsize += textsize;
      total_datasize += datasize;
      total_bsssize  += bsssize;
    }

  rprint_number (7, textsize);
  putchar ('\t');
  rprint_number (7, datasize);
  putchar ('\t');
  rprint_number (7, bsssize);
  printf (((radix == octal) ? "\t%7lo\t%7lx\t" : "\t%7lu\t%7lx\t"),
	  (unsigned long) total, (unsigned long) total);

  fputs (bfd_get_filename (abfd), stdout);

  if (bfd_my_archive (abfd))
    printf (" (ex %s)", bfd_get_filename (bfd_my_archive (abfd)));
}

/* I REALLY miss lexical functions! */
bfd_size_type svi_total = 0;
bfd_vma svi_maxvma = 0;
int svi_namelen = 0;
int svi_vmalen = 0;
int svi_sizelen = 0;

static void
sysv_internal_sizer (bfd *file ATTRIBUTE_UNUSED, sec_ptr sec,
		     void *ignore ATTRIBUTE_UNUSED)
{
  bfd_size_type size = bfd_section_size (file, sec);

  if (   ! bfd_is_abs_section (sec)
      && ! bfd_is_com_section (sec)
      && ! bfd_is_und_section (sec))
    {
      int namelen = strlen (bfd_section_name (file, sec));

      if (namelen > svi_namelen)
	svi_namelen = namelen;

      svi_total += size;

      if (bfd_section_vma (file, sec) > svi_maxvma)
	svi_maxvma = bfd_section_vma (file, sec);
    }
}

static void
sysv_internal_printer (bfd *file ATTRIBUTE_UNUSED, sec_ptr sec,
		       void *ignore ATTRIBUTE_UNUSED)
{
  bfd_size_type size = bfd_section_size (file, sec);

  if (   ! bfd_is_abs_section (sec)
      && ! bfd_is_com_section (sec)
      && ! bfd_is_und_section (sec))
    {
      svi_total += size;

      printf ("%-*s   ", svi_namelen, bfd_section_name (file, sec));
      rprint_number (svi_sizelen, size);
      printf ("   ");
      rprint_number (svi_vmalen, bfd_section_vma (file, sec));
      printf ("\n");
    }
}

static void
print_sysv_format (bfd *file)
{
  /* Size all of the columns.  */
  svi_total = 0;
  svi_maxvma = 0;
  svi_namelen = 0;
  bfd_map_over_sections (file, sysv_internal_sizer, NULL);
  svi_vmalen = size_number ((bfd_size_type)svi_maxvma);

  if ((size_t) svi_vmalen < sizeof ("addr") - 1)
    svi_vmalen = sizeof ("addr")-1;

  svi_sizelen = size_number (svi_total);
  if ((size_t) svi_sizelen < sizeof ("size") - 1)
    svi_sizelen = sizeof ("size")-1;

  svi_total = 0;
  printf ("%s  ", bfd_get_filename (file));

  if (bfd_my_archive (file))
    printf (" (ex %s)", bfd_get_filename (bfd_my_archive (file)));

  printf (":\n%-*s   %*s   %*s\n", svi_namelen, "section",
	  svi_sizelen, "size", svi_vmalen, "addr");

  bfd_map_over_sections (file, sysv_internal_printer, NULL);

  printf ("%-*s   ", svi_namelen, "Total");
  rprint_number (svi_sizelen, svi_total);
  printf ("\n\n");
}

static void
print_sizes (bfd *file)
{
  if (berkeley_format)
    print_berkeley_format (file);
  else
    print_sysv_format (file);
}
