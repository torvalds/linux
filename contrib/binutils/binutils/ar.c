/* ar.c - Archive modify and extract.
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

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

/*
   Bugs: should use getopt the way tar does (complete w/optional -) and
   should have long options too. GNU ar used to check file against filesystem
   in quick_update and replace operations (would check mtime). Doesn't warn
   when name truncated. No way to specify pos_end. Error messages should be
   more consistent.  */

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"
#include "progress.h"
#include "aout/ar.h"
#include "libbfd.h"
#include "bucomm.h"
#include "arsup.h"
#include "filenames.h"
#include "binemul.h"
#include <sys/stat.h>

#ifdef __GO32___
#define EXT_NAME_LEN 3		/* bufflen of addition to name if it's MS-DOS */
#else
#define EXT_NAME_LEN 6		/* ditto for *NIX */
#endif

/* We need to open files in binary modes on system where that makes a
   difference.  */
#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Kludge declaration from BFD!  This is ugly!  FIXME!  XXX */

struct ar_hdr *
  bfd_special_undocumented_glue (bfd * abfd, const char *filename);

/* Static declarations */

static void mri_emul (void);
static const char *normalize (const char *, bfd *);
static void remove_output (void);
static void map_over_members (bfd *, void (*)(bfd *), char **, int);
static void print_contents (bfd * member);
static void delete_members (bfd *, char **files_to_delete);

static void move_members (bfd *, char **files_to_move);
static void replace_members
  (bfd *, char **files_to_replace, bfd_boolean quick);
static void print_descr (bfd * abfd);
static void write_archive (bfd *);
static int  ranlib_only (const char *archname);
static int  ranlib_touch (const char *archname);
static void usage (int);

/** Globals and flags */

static int mri_mode;

/* This flag distinguishes between ar and ranlib:
   1 means this is 'ranlib'; 0 means this is 'ar'.
   -1 means if we should use argv[0] to decide.  */
extern int is_ranlib;

/* Nonzero means don't warn about creating the archive file if necessary.  */
int silent_create = 0;

/* Nonzero means describe each action performed.  */
int verbose = 0;

/* Nonzero means preserve dates of members when extracting them.  */
int preserve_dates = 0;

/* Nonzero means don't replace existing members whose dates are more recent
   than the corresponding files.  */
int newer_only = 0;

/* Controls the writing of an archive symbol table (in BSD: a __.SYMDEF
   member).  -1 means we've been explicitly asked to not write a symbol table;
   +1 means we've been explicitly asked to write it;
   0 is the default.
   Traditionally, the default in BSD has been to not write the table.
   However, for POSIX.2 compliance the default is now to write a symbol table
   if any of the members are object files.  */
int write_armap = 0;

/* Nonzero means it's the name of an existing member; position new or moved
   files with respect to this one.  */
char *posname = NULL;

/* Sez how to use `posname': pos_before means position before that member.
   pos_after means position after that member. pos_end means always at end.
   pos_default means default appropriately. For the latter two, `posname'
   should also be zero.  */
enum pos
  {
    pos_default, pos_before, pos_after, pos_end
  } postype = pos_default;

static bfd **
get_pos_bfd (bfd **, enum pos, const char *);

/* For extract/delete only.  If COUNTED_NAME_MODE is TRUE, we only
   extract the COUNTED_NAME_COUNTER instance of that name.  */
static bfd_boolean counted_name_mode = 0;
static int counted_name_counter = 0;

/* Whether to truncate names of files stored in the archive.  */
static bfd_boolean ar_truncate = FALSE;

/* Whether to use a full file name match when searching an archive.
   This is convenient for archives created by the Microsoft lib
   program.  */
static bfd_boolean full_pathname = FALSE;

int interactive = 0;

static void
mri_emul (void)
{
  interactive = isatty (fileno (stdin));
  yyparse ();
}

/* If COUNT is 0, then FUNCTION is called once on each entry.  If nonzero,
   COUNT is the length of the FILES chain; FUNCTION is called on each entry
   whose name matches one in FILES.  */

static void
map_over_members (bfd *arch, void (*function)(bfd *), char **files, int count)
{
  bfd *head;
  int match_count;

  if (count == 0)
    {
      for (head = arch->archive_next; head; head = head->archive_next)
	{
	  PROGRESS (1);
	  function (head);
	}
      return;
    }

  /* This may appear to be a baroque way of accomplishing what we want.
     However we have to iterate over the filenames in order to notice where
     a filename is requested but does not exist in the archive.  Ditto
     mapping over each file each time -- we want to hack multiple
     references.  */

  for (; count > 0; files++, count--)
    {
      bfd_boolean found = FALSE;

      match_count = 0;
      for (head = arch->archive_next; head; head = head->archive_next)
	{
	  PROGRESS (1);
	  if (head->filename == NULL)
	    {
	      /* Some archive formats don't get the filenames filled in
		 until the elements are opened.  */
	      struct stat buf;
	      bfd_stat_arch_elt (head, &buf);
	    }
	  if ((head->filename != NULL) &&
	      (!FILENAME_CMP (normalize (*files, arch), head->filename)))
	    {
	      ++match_count;
	      if (counted_name_mode
		  && match_count != counted_name_counter)
		{
		  /* Counting, and didn't match on count; go on to the
                     next one.  */
		  continue;
		}

	      found = TRUE;
	      function (head);
	    }
	}
      if (!found)
	/* xgettext:c-format */
	fprintf (stderr, _("no entry %s in archive\n"), *files);
    }
}

bfd_boolean operation_alters_arch = FALSE;

static void
usage (int help)
{
  FILE *s;

  s = help ? stdout : stderr;

  if (! is_ranlib)
    {
      /* xgettext:c-format */
      fprintf (s, _("Usage: %s [emulation options] [-]{dmpqrstx}[abcfilNoPsSuvV] [member-name] [count] archive-file file...\n"),
	       program_name);
      /* xgettext:c-format */
      fprintf (s, _("       %s -M [<mri-script]\n"), program_name);
      fprintf (s, _(" commands:\n"));
      fprintf (s, _("  d            - delete file(s) from the archive\n"));
      fprintf (s, _("  m[ab]        - move file(s) in the archive\n"));
      fprintf (s, _("  p            - print file(s) found in the archive\n"));
      fprintf (s, _("  q[f]         - quick append file(s) to the archive\n"));
      fprintf (s, _("  r[ab][f][u]  - replace existing or insert new file(s) into the archive\n"));
      fprintf (s, _("  t            - display contents of archive\n"));
      fprintf (s, _("  x[o]         - extract file(s) from the archive\n"));
      fprintf (s, _(" command specific modifiers:\n"));
      fprintf (s, _("  [a]          - put file(s) after [member-name]\n"));
      fprintf (s, _("  [b]          - put file(s) before [member-name] (same as [i])\n"));
      fprintf (s, _("  [N]          - use instance [count] of name\n"));
      fprintf (s, _("  [f]          - truncate inserted file names\n"));
      fprintf (s, _("  [P]          - use full path names when matching\n"));
      fprintf (s, _("  [o]          - preserve original dates\n"));
      fprintf (s, _("  [u]          - only replace files that are newer than current archive contents\n"));
      fprintf (s, _(" generic modifiers:\n"));
      fprintf (s, _("  [c]          - do not warn if the library had to be created\n"));
      fprintf (s, _("  [s]          - create an archive index (cf. ranlib)\n"));
      fprintf (s, _("  [S]          - do not build a symbol table\n"));
      fprintf (s, _("  [v]          - be verbose\n"));
      fprintf (s, _("  [V]          - display the version number\n"));
      fprintf (s, _("  @<file>      - read options from <file>\n"));
 
      ar_emul_usage (s);
    }
  else
    {
      /* xgettext:c-format */
      fprintf (s, _("Usage: %s [options] archive\n"), program_name);
      fprintf (s, _(" Generate an index to speed access to archives\n"));
      fprintf (s, _(" The options are:\n\
  @<file>                      Read options from <file>\n\
  -h --help                    Print this help message\n\
  -V --version                 Print version information\n"));
    }

  list_supported_targets (program_name, s);

  if (REPORT_BUGS_TO[0] && help)
    fprintf (s, _("Report bugs to %s\n"), REPORT_BUGS_TO);

  xexit (help ? 0 : 1);
}

/* Normalize a file name specified on the command line into a file
   name which we will use in an archive.  */

static const char *
normalize (const char *file, bfd *abfd)
{
  const char *filename;

  if (full_pathname)
    return file;

  filename = strrchr (file, '/');
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  {
    /* We could have foo/bar\\baz, or foo\\bar, or d:bar.  */
    char *bslash = strrchr (file, '\\');
    if (filename == NULL || (bslash != NULL && bslash > filename))
      filename = bslash;
    if (filename == NULL && file[0] != '\0' && file[1] == ':')
      filename = file + 1;
  }
#endif
  if (filename != (char *) NULL)
    filename++;
  else
    filename = file;

  if (ar_truncate
      && abfd != NULL
      && strlen (filename) > abfd->xvec->ar_max_namelen)
    {
      char *s;

      /* Space leak.  */
      s = (char *) xmalloc (abfd->xvec->ar_max_namelen + 1);
      memcpy (s, filename, abfd->xvec->ar_max_namelen);
      s[abfd->xvec->ar_max_namelen] = '\0';
      filename = s;
    }

  return filename;
}

/* Remove any output file.  This is only called via xatexit.  */

static const char *output_filename = NULL;
static FILE *output_file = NULL;
static bfd *output_bfd = NULL;

static void
remove_output (void)
{
  if (output_filename != NULL)
    {
      if (output_bfd != NULL)
	bfd_cache_close (output_bfd);
      if (output_file != NULL)
	fclose (output_file);
      unlink_if_ordinary (output_filename);
    }
}

/* The option parsing should be in its own function.
   It will be when I have getopt working.  */

int main (int, char **);

int
main (int argc, char **argv)
{
  char *arg_ptr;
  char c;
  enum
    {
      none = 0, delete, replace, print_table,
      print_files, extract, move, quick_append
    } operation = none;
  int arg_index;
  char **files;
  int file_count;
  char *inarch_filename;
  int show_version;
  int i;
  int do_posix = 0;

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  program_name = argv[0];
  xmalloc_set_program_name (program_name);

  expandargv (&argc, &argv);

  if (is_ranlib < 0)
    {
      char *temp;

      temp = strrchr (program_name, '/');
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
      {
	/* We could have foo/bar\\baz, or foo\\bar, or d:bar.  */
	char *bslash = strrchr (program_name, '\\');
	if (temp == NULL || (bslash != NULL && bslash > temp))
	  temp = bslash;
	if (temp == NULL && program_name[0] != '\0' && program_name[1] == ':')
	  temp = program_name + 1;
      }
#endif
      if (temp == NULL)
	temp = program_name;
      else
	++temp;
      if (strlen (temp) >= 6
	  && FILENAME_CMP (temp + strlen (temp) - 6, "ranlib") == 0)
	is_ranlib = 1;
      else
	is_ranlib = 0;
    }

  if (argc > 1 && argv[1][0] == '-')
    {
      if (strcmp (argv[1], "--help") == 0)
	usage (1);
      else if (strcmp (argv[1], "--version") == 0)
	{
	  if (is_ranlib)
	    print_version ("ranlib");
	  else
	    print_version ("ar");
	}
    }

  START_PROGRESS (program_name, 0);

  bfd_init ();
  set_default_bfd_target ();

  show_version = 0;

  xatexit (remove_output);

  for (i = 1; i < argc; i++)
    if (! ar_emul_parse_arg (argv[i]))
      break;
  argv += (i - 1);
  argc -= (i - 1);

  if (is_ranlib)
    {
      int status = 0;
      bfd_boolean touch = FALSE;

      if (argc < 2
	  || strcmp (argv[1], "--help") == 0
	  || strcmp (argv[1], "-h") == 0
	  || strcmp (argv[1], "-H") == 0)
	usage (0);
      if (strcmp (argv[1], "-V") == 0
	  || strcmp (argv[1], "-v") == 0
	  || CONST_STRNEQ (argv[1], "--v"))
	print_version ("ranlib");
      arg_index = 1;
      if (strcmp (argv[1], "-t") == 0)
	{
	  ++arg_index;
	  touch = TRUE;
	}
      while (arg_index < argc)
	{
	  if (! touch)
	    status |= ranlib_only (argv[arg_index]);
	  else
	    status |= ranlib_touch (argv[arg_index]);
	  ++arg_index;
	}
      xexit (status);
    }

  if (argc == 2 && strcmp (argv[1], "-M") == 0)
    {
      mri_emul ();
      xexit (0);
    }

  if (argc < 2)
    usage (0);

  arg_index = 1;
  arg_ptr = argv[arg_index];

  if (*arg_ptr == '-')
    {
      /* When the first option starts with '-' we support POSIX-compatible
	 option parsing.  */
      do_posix = 1;
      ++arg_ptr;			/* compatibility */
    }

  do
    {
      while ((c = *arg_ptr++) != '\0')
	{
	  switch (c)
	    {
	    case 'd':
	    case 'm':
	    case 'p':
	    case 'q':
	    case 'r':
	    case 't':
	    case 'x':
	      if (operation != none)
		fatal (_("two different operation options specified"));
	      switch (c)
		{
		case 'd':
		  operation = delete;
		  operation_alters_arch = TRUE;
		  break;
		case 'm':
		  operation = move;
		  operation_alters_arch = TRUE;
		  break;
		case 'p':
		  operation = print_files;
		  break;
		case 'q':
		  operation = quick_append;
		  operation_alters_arch = TRUE;
		  break;
		case 'r':
		  operation = replace;
		  operation_alters_arch = TRUE;
		  break;
		case 't':
		  operation = print_table;
		  break;
		case 'x':
		  operation = extract;
		  break;
		}
	    case 'l':
	      break;
	    case 'c':
	      silent_create = 1;
	      break;
	    case 'o':
	      preserve_dates = 1;
	      break;
	    case 'V':
	      show_version = TRUE;
	      break;
	    case 's':
	      write_armap = 1;
	      break;
	    case 'S':
	      write_armap = -1;
	      break;
	    case 'u':
	      newer_only = 1;
	      break;
	    case 'v':
	      verbose = 1;
	      break;
	    case 'a':
	      postype = pos_after;
	      break;
	    case 'b':
	      postype = pos_before;
	      break;
	    case 'i':
	      postype = pos_before;
	      break;
	    case 'M':
	      mri_mode = 1;
	      break;
	    case 'N':
	      counted_name_mode = TRUE;
	      break;
	    case 'f':
	      ar_truncate = TRUE;
	      break;
	    case 'P':
	      full_pathname = TRUE;
	      break;
	    default:
	      /* xgettext:c-format */
	      non_fatal (_("illegal option -- %c"), c);
	      usage (0);
	    }
	}

      /* With POSIX-compatible option parsing continue with the next
	 argument if it starts with '-'.  */
      if (do_posix && arg_index + 1 < argc && argv[arg_index + 1][0] == '-')
	arg_ptr = argv[++arg_index] + 1;
      else
	do_posix = 0;
    }
  while (do_posix);

  if (show_version)
    print_version ("ar");

  ++arg_index;
  if (arg_index >= argc)
    usage (0);

  if (mri_mode)
    {
      mri_emul ();
    }
  else
    {
      bfd *arch;

      /* We don't use do_quick_append any more.  Too many systems
	 expect ar to always rebuild the symbol table even when q is
	 used.  */

      /* We can't write an armap when using ar q, so just do ar r
         instead.  */
      if (operation == quick_append && write_armap)
	operation = replace;

      if ((operation == none || operation == print_table)
	  && write_armap == 1)
	xexit (ranlib_only (argv[arg_index]));

      if (operation == none)
	fatal (_("no operation specified"));

      if (newer_only && operation != replace)
	fatal (_("`u' is only meaningful with the `r' option."));

      if (postype != pos_default)
	posname = argv[arg_index++];

      if (counted_name_mode)
	{
	  if (operation != extract && operation != delete)
	     fatal (_("`N' is only meaningful with the `x' and `d' options."));
	  counted_name_counter = atoi (argv[arg_index++]);
	  if (counted_name_counter <= 0)
	    fatal (_("Value for `N' must be positive."));
	}

      inarch_filename = argv[arg_index++];

      files = arg_index < argc ? argv + arg_index : NULL;
      file_count = argc - arg_index;

      arch = open_inarch (inarch_filename,
			  files == NULL ? (char *) NULL : files[0]);

      switch (operation)
	{
	case print_table:
	  map_over_members (arch, print_descr, files, file_count);
	  break;

	case print_files:
	  map_over_members (arch, print_contents, files, file_count);
	  break;

	case extract:
	  map_over_members (arch, extract_file, files, file_count);
	  break;

	case delete:
	  if (files != NULL)
	    delete_members (arch, files);
	  else
	    output_filename = NULL;
	  break;

	case move:
	  if (files != NULL)
	    move_members (arch, files);
	  else
	    output_filename = NULL;
	  break;

	case replace:
	case quick_append:
	  if (files != NULL || write_armap > 0)
	    replace_members (arch, files, operation == quick_append);
	  else
	    output_filename = NULL;
	  break;

	  /* Shouldn't happen! */
	default:
	  /* xgettext:c-format */
	  fatal (_("internal error -- this option not implemented"));
	}
    }

  END_PROGRESS (program_name);

  xexit (0);
  return 0;
}

bfd *
open_inarch (const char *archive_filename, const char *file)
{
  const char *target;
  bfd **last_one;
  bfd *next_one;
  struct stat sbuf;
  bfd *arch;
  char **matching;

  bfd_set_error (bfd_error_no_error);

  target = NULL;

  if (stat (archive_filename, &sbuf) != 0)
    {
#if !defined(__GO32__) || defined(__DJGPP__)

      /* FIXME: I don't understand why this fragment was ifndef'ed
	 away for __GO32__; perhaps it was in the days of DJGPP v1.x.
	 stat() works just fine in v2.x, so I think this should be
	 removed.  For now, I enable it for DJGPP v2. -- EZ.  */

/* KLUDGE ALERT! Temporary fix until I figger why
   stat() is wrong ... think it's buried in GO32's IDT - Jax */
      if (errno != ENOENT)
	bfd_fatal (archive_filename);
#endif

      if (!operation_alters_arch)
	{
	  fprintf (stderr, "%s: ", program_name);
	  perror (archive_filename);
	  maybequit ();
	  return NULL;
	}

      /* Try to figure out the target to use for the archive from the
         first object on the list.  */
      if (file != NULL)
	{
	  bfd *obj;

	  obj = bfd_openr (file, NULL);
	  if (obj != NULL)
	    {
	      if (bfd_check_format (obj, bfd_object))
		target = bfd_get_target (obj);
	      (void) bfd_close (obj);
	    }
	}

      /* Create an empty archive.  */
      arch = bfd_openw (archive_filename, target);
      if (arch == NULL
	  || ! bfd_set_format (arch, bfd_archive)
	  || ! bfd_close (arch))
	bfd_fatal (archive_filename);
      else if (!silent_create)
        non_fatal (_("creating %s"), archive_filename);

      /* If we die creating a new archive, don't leave it around.  */
      output_filename = archive_filename;
    }

  arch = bfd_openr (archive_filename, target);
  if (arch == NULL)
    {
    bloser:
      bfd_fatal (archive_filename);
    }

  if (! bfd_check_format_matches (arch, bfd_archive, &matching))
    {
      bfd_nonfatal (archive_filename);
      if (bfd_get_error () == bfd_error_file_ambiguously_recognized)
	{
	  list_matching_formats (matching);
	  free (matching);
	}
      xexit (1);
    }

  last_one = &(arch->archive_next);
  /* Read all the contents right away, regardless.  */
  for (next_one = bfd_openr_next_archived_file (arch, NULL);
       next_one;
       next_one = bfd_openr_next_archived_file (arch, next_one))
    {
      PROGRESS (1);
      *last_one = next_one;
      last_one = &next_one->archive_next;
    }
  *last_one = (bfd *) NULL;
  if (bfd_get_error () != bfd_error_no_more_archived_files)
    goto bloser;
  return arch;
}

static void
print_contents (bfd *abfd)
{
  size_t ncopied = 0;
  char *cbuf = xmalloc (BUFSIZE);
  struct stat buf;
  size_t size;
  if (bfd_stat_arch_elt (abfd, &buf) != 0)
    /* xgettext:c-format */
    fatal (_("internal stat error on %s"), bfd_get_filename (abfd));

  if (verbose)
    /* xgettext:c-format */
    printf (_("\n<%s>\n\n"), bfd_get_filename (abfd));

  bfd_seek (abfd, (file_ptr) 0, SEEK_SET);

  size = buf.st_size;
  while (ncopied < size)
    {

      size_t nread;
      size_t tocopy = size - ncopied;
      if (tocopy > BUFSIZE)
	tocopy = BUFSIZE;

      nread = bfd_bread (cbuf, (bfd_size_type) tocopy, abfd);
      if (nread != tocopy)
	/* xgettext:c-format */
	fatal (_("%s is not a valid archive"),
	       bfd_get_filename (bfd_my_archive (abfd)));

      /* fwrite in mingw32 may return int instead of size_t. Cast the
	 return value to size_t to avoid comparison between signed and
	 unsigned values.  */
      if ((size_t) fwrite (cbuf, 1, nread, stdout) != nread)
	fatal ("stdout: %s", strerror (errno));
      ncopied += tocopy;
    }
  free (cbuf);
}

/* Extract a member of the archive into its own file.

   We defer opening the new file until after we have read a BUFSIZ chunk of the
   old one, since we know we have just read the archive header for the old
   one.  Since most members are shorter than BUFSIZ, this means we will read
   the old header, read the old data, write a new inode for the new file, and
   write the new data, and be done. This 'optimization' is what comes from
   sitting next to a bare disk and hearing it every time it seeks.  -- Gnu
   Gilmore  */

void
extract_file (bfd *abfd)
{
  FILE *ostream;
  char *cbuf = xmalloc (BUFSIZE);
  size_t nread, tocopy;
  size_t ncopied = 0;
  size_t size;
  struct stat buf;

  if (bfd_stat_arch_elt (abfd, &buf) != 0)
    /* xgettext:c-format */
    fatal (_("internal stat error on %s"), bfd_get_filename (abfd));
  size = buf.st_size;

  if (verbose)
    printf ("x - %s\n", bfd_get_filename (abfd));

  bfd_seek (abfd, (file_ptr) 0, SEEK_SET);

  ostream = NULL;
  if (size == 0)
    {
      /* Seems like an abstraction violation, eh?  Well it's OK! */
      output_filename = bfd_get_filename (abfd);

      ostream = fopen (bfd_get_filename (abfd), FOPEN_WB);
      if (ostream == NULL)
	{
	  perror (bfd_get_filename (abfd));
	  xexit (1);
	}

      output_file = ostream;
    }
  else
    while (ncopied < size)
      {
	tocopy = size - ncopied;
	if (tocopy > BUFSIZE)
	  tocopy = BUFSIZE;

	nread = bfd_bread (cbuf, (bfd_size_type) tocopy, abfd);
	if (nread != tocopy)
	  /* xgettext:c-format */
	  fatal (_("%s is not a valid archive"),
		 bfd_get_filename (bfd_my_archive (abfd)));

	/* See comment above; this saves disk arm motion */
	if (ostream == NULL)
	  {
	    /* Seems like an abstraction violation, eh?  Well it's OK! */
	    output_filename = bfd_get_filename (abfd);

	    ostream = fopen (bfd_get_filename (abfd), FOPEN_WB);
	    if (ostream == NULL)
	      {
		perror (bfd_get_filename (abfd));
		xexit (1);
	      }

	    output_file = ostream;
	  }

	/* fwrite in mingw32 may return int instead of size_t. Cast
	   the return value to size_t to avoid comparison between
	   signed and unsigned values.  */
	if ((size_t) fwrite (cbuf, 1, nread, ostream) != nread)
	  fatal ("%s: %s", output_filename, strerror (errno));
	ncopied += tocopy;
      }

  if (ostream != NULL)
    fclose (ostream);

  output_file = NULL;
  output_filename = NULL;

  chmod (bfd_get_filename (abfd), buf.st_mode);

  if (preserve_dates)
    {
      /* Set access time to modification time.  Only st_mtime is
	 initialized by bfd_stat_arch_elt.  */
      buf.st_atime = buf.st_mtime;
      set_times (bfd_get_filename (abfd), &buf);
    }

  free (cbuf);
}

static void
write_archive (bfd *iarch)
{
  bfd *obfd;
  char *old_name, *new_name;
  bfd *contents_head = iarch->archive_next;

  old_name = xmalloc (strlen (bfd_get_filename (iarch)) + 1);
  strcpy (old_name, bfd_get_filename (iarch));
  new_name = make_tempname (old_name);

  if (new_name == NULL)
    bfd_fatal ("could not create temporary file whilst writing archive");
  
  output_filename = new_name;

  obfd = bfd_openw (new_name, bfd_get_target (iarch));

  if (obfd == NULL)
    bfd_fatal (old_name);

  output_bfd = obfd;

  bfd_set_format (obfd, bfd_archive);

  /* Request writing the archive symbol table unless we've
     been explicitly requested not to.  */
  obfd->has_armap = write_armap >= 0;

  if (ar_truncate)
    {
      /* This should really use bfd_set_file_flags, but that rejects
         archives.  */
      obfd->flags |= BFD_TRADITIONAL_FORMAT;
    }

  if (!bfd_set_archive_head (obfd, contents_head))
    bfd_fatal (old_name);

  if (!bfd_close (obfd))
    bfd_fatal (old_name);

  output_bfd = NULL;
  output_filename = NULL;

  /* We don't care if this fails; we might be creating the archive.  */
  bfd_close (iarch);

  if (smart_rename (new_name, old_name, 0) != 0)
    xexit (1);
}

/* Return a pointer to the pointer to the entry which should be rplacd'd
   into when altering.  DEFAULT_POS should be how to interpret pos_default,
   and should be a pos value.  */

static bfd **
get_pos_bfd (bfd **contents, enum pos default_pos, const char *default_posname)
{
  bfd **after_bfd = contents;
  enum pos realpos;
  const char *realposname;

  if (postype == pos_default)
    {
      realpos = default_pos;
      realposname = default_posname;
    }
  else
    {
      realpos = postype;
      realposname = posname;
    }

  if (realpos == pos_end)
    {
      while (*after_bfd)
	after_bfd = &((*after_bfd)->archive_next);
    }
  else
    {
      for (; *after_bfd; after_bfd = &(*after_bfd)->archive_next)
	if (FILENAME_CMP ((*after_bfd)->filename, realposname) == 0)
	  {
	    if (realpos == pos_after)
	      after_bfd = &(*after_bfd)->archive_next;
	    break;
	  }
    }
  return after_bfd;
}

static void
delete_members (bfd *arch, char **files_to_delete)
{
  bfd **current_ptr_ptr;
  bfd_boolean found;
  bfd_boolean something_changed = FALSE;
  int match_count;

  for (; *files_to_delete != NULL; ++files_to_delete)
    {
      /* In a.out systems, the armap is optional.  It's also called
	 __.SYMDEF.  So if the user asked to delete it, we should remember
	 that fact. This isn't quite right for COFF systems (where
	 __.SYMDEF might be regular member), but it's very unlikely
	 to be a problem.  FIXME */

      if (!strcmp (*files_to_delete, "__.SYMDEF"))
	{
	  arch->has_armap = FALSE;
	  write_armap = -1;
	  continue;
	}

      found = FALSE;
      match_count = 0;
      current_ptr_ptr = &(arch->archive_next);
      while (*current_ptr_ptr)
	{
	  if (FILENAME_CMP (normalize (*files_to_delete, arch),
			    (*current_ptr_ptr)->filename) == 0)
	    {
	      ++match_count;
	      if (counted_name_mode
		  && match_count != counted_name_counter)
		{
		  /* Counting, and didn't match on count; go on to the
                     next one.  */
		}
	      else
		{
		  found = TRUE;
		  something_changed = TRUE;
		  if (verbose)
		    printf ("d - %s\n",
			    *files_to_delete);
		  *current_ptr_ptr = ((*current_ptr_ptr)->archive_next);
		  goto next_file;
		}
	    }

	  current_ptr_ptr = &((*current_ptr_ptr)->archive_next);
	}

      if (verbose && !found)
	{
	  /* xgettext:c-format */
	  printf (_("No member named `%s'\n"), *files_to_delete);
	}
    next_file:
      ;
    }

  if (something_changed)
    write_archive (arch);
  else
    output_filename = NULL;
}


/* Reposition existing members within an archive */

static void
move_members (bfd *arch, char **files_to_move)
{
  bfd **after_bfd;		/* New entries go after this one */
  bfd **current_ptr_ptr;	/* cdr pointer into contents */

  for (; *files_to_move; ++files_to_move)
    {
      current_ptr_ptr = &(arch->archive_next);
      while (*current_ptr_ptr)
	{
	  bfd *current_ptr = *current_ptr_ptr;
	  if (FILENAME_CMP (normalize (*files_to_move, arch),
			    current_ptr->filename) == 0)
	    {
	      /* Move this file to the end of the list - first cut from
		 where it is.  */
	      bfd *link;
	      *current_ptr_ptr = current_ptr->archive_next;

	      /* Now glue to end */
	      after_bfd = get_pos_bfd (&arch->archive_next, pos_end, NULL);
	      link = *after_bfd;
	      *after_bfd = current_ptr;
	      current_ptr->archive_next = link;

	      if (verbose)
		printf ("m - %s\n", *files_to_move);

	      goto next_file;
	    }

	  current_ptr_ptr = &((*current_ptr_ptr)->archive_next);
	}
      /* xgettext:c-format */
      fatal (_("no entry %s in archive %s!"), *files_to_move, arch->filename);

    next_file:;
    }

  write_archive (arch);
}

/* Ought to default to replacing in place, but this is existing practice!  */

static void
replace_members (bfd *arch, char **files_to_move, bfd_boolean quick)
{
  bfd_boolean changed = FALSE;
  bfd **after_bfd;		/* New entries go after this one.  */
  bfd *current;
  bfd **current_ptr;

  while (files_to_move && *files_to_move)
    {
      if (! quick)
	{
	  current_ptr = &arch->archive_next;
	  while (*current_ptr)
	    {
	      current = *current_ptr;

	      /* For compatibility with existing ar programs, we
		 permit the same file to be added multiple times.  */
	      if (FILENAME_CMP (normalize (*files_to_move, arch),
				normalize (current->filename, arch)) == 0
		  && current->arelt_data != NULL)
		{
		  if (newer_only)
		    {
		      struct stat fsbuf, asbuf;

		      if (stat (*files_to_move, &fsbuf) != 0)
			{
			  if (errno != ENOENT)
			    bfd_fatal (*files_to_move);
			  goto next_file;
			}
		      if (bfd_stat_arch_elt (current, &asbuf) != 0)
			/* xgettext:c-format */
			fatal (_("internal stat error on %s"),
			       current->filename);

		      if (fsbuf.st_mtime <= asbuf.st_mtime)
			goto next_file;
		    }

		  after_bfd = get_pos_bfd (&arch->archive_next, pos_after,
					   current->filename);
		  if (ar_emul_replace (after_bfd, *files_to_move,
				       verbose))
		    {
		      /* Snip out this entry from the chain.  */
		      *current_ptr = (*current_ptr)->archive_next;
		      changed = TRUE;
		    }

		  goto next_file;
		}
	      current_ptr = &(current->archive_next);
	    }
	}

      /* Add to the end of the archive.  */
      after_bfd = get_pos_bfd (&arch->archive_next, pos_end, NULL);

      if (ar_emul_append (after_bfd, *files_to_move, verbose))
	changed = TRUE;

    next_file:;

      files_to_move++;
    }

  if (changed)
    write_archive (arch);
  else
    output_filename = NULL;
}

static int
ranlib_only (const char *archname)
{
  bfd *arch;

  if (get_file_size (archname) < 1)
    return 1;
  write_armap = 1;
  arch = open_inarch (archname, (char *) NULL);
  if (arch == NULL)
    xexit (1);
  write_archive (arch);
  return 0;
}

/* Update the timestamp of the symbol map of an archive.  */

static int
ranlib_touch (const char *archname)
{
#ifdef __GO32__
  /* I don't think updating works on go32.  */
  ranlib_only (archname);
#else
  int f;
  bfd *arch;
  char **matching;

  if (get_file_size (archname) < 1)
    return 1;
  f = open (archname, O_RDWR | O_BINARY, 0);
  if (f < 0)
    {
      bfd_set_error (bfd_error_system_call);
      bfd_fatal (archname);
    }

  arch = bfd_fdopenr (archname, (const char *) NULL, f);
  if (arch == NULL)
    bfd_fatal (archname);
  if (! bfd_check_format_matches (arch, bfd_archive, &matching))
    {
      bfd_nonfatal (archname);
      if (bfd_get_error () == bfd_error_file_ambiguously_recognized)
	{
	  list_matching_formats (matching);
	  free (matching);
	}
      xexit (1);
    }

  if (! bfd_has_map (arch))
    /* xgettext:c-format */
    fatal (_("%s: no archive map to update"), archname);

  bfd_update_armap_timestamp (arch);

  if (! bfd_close (arch))
    bfd_fatal (archname);
#endif
  return 0;
}

/* Things which are interesting to map over all or some of the files: */

static void
print_descr (bfd *abfd)
{
  print_arelt_descr (stdout, abfd, verbose);
}
