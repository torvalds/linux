/* nm.c -- Describe symbol table of a rel file.
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2007
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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "progress.h"
#include "getopt.h"
#include "aout/stab_gnu.h"
#include "aout/ranlib.h"
#include "demangle.h"
#include "libiberty.h"
#include "elf-bfd.h"
#include "elf/common.h"
#include "bucomm.h"

/* When sorting by size, we use this structure to hold the size and a
   pointer to the minisymbol.  */

struct size_sym
{
  const void *minisym;
  bfd_vma size;
};

/* When fetching relocs, we use this structure to pass information to
   get_relocs.  */

struct get_relocs_info
{
  asection **secs;
  arelent ***relocs;
  long *relcount;
  asymbol **syms;
};

struct extended_symbol_info
{
  symbol_info *sinfo;
  bfd_vma ssize;
  elf_symbol_type *elfinfo;
  /* FIXME: We should add more fields for Type, Line, Section.  */
};
#define SYM_NAME(sym)        (sym->sinfo->name)
#define SYM_VALUE(sym)       (sym->sinfo->value)
#define SYM_TYPE(sym)        (sym->sinfo->type)
#define SYM_STAB_NAME(sym)   (sym->sinfo->stab_name)
#define SYM_STAB_DESC(sym)   (sym->sinfo->stab_desc)
#define SYM_STAB_OTHER(sym)  (sym->sinfo->stab_other)
#define SYM_SIZE(sym) \
  (sym->elfinfo ? sym->elfinfo->internal_elf_sym.st_size: sym->ssize)

/* The output formatting functions.  */
static void print_object_filename_bsd (char *);
static void print_object_filename_sysv (char *);
static void print_object_filename_posix (char *);
static void print_archive_filename_bsd (char *);
static void print_archive_filename_sysv (char *);
static void print_archive_filename_posix (char *);
static void print_archive_member_bsd (char *, const char *);
static void print_archive_member_sysv (char *, const char *);
static void print_archive_member_posix (char *, const char *);
static void print_symbol_filename_bsd (bfd *, bfd *);
static void print_symbol_filename_sysv (bfd *, bfd *);
static void print_symbol_filename_posix (bfd *, bfd *);
static void print_value (bfd *, bfd_vma);
static void print_symbol_info_bsd (struct extended_symbol_info *, bfd *);
static void print_symbol_info_sysv (struct extended_symbol_info *, bfd *);
static void print_symbol_info_posix (struct extended_symbol_info *, bfd *);

/* Support for different output formats.  */
struct output_fns
  {
    /* Print the name of an object file given on the command line.  */
    void (*print_object_filename) (char *);

    /* Print the name of an archive file given on the command line.  */
    void (*print_archive_filename) (char *);

    /* Print the name of an archive member file.  */
    void (*print_archive_member) (char *, const char *);

    /* Print the name of the file (and archive, if there is one)
       containing a symbol.  */
    void (*print_symbol_filename) (bfd *, bfd *);

    /* Print a line of information about a symbol.  */
    void (*print_symbol_info) (struct extended_symbol_info *, bfd *);
  };

static struct output_fns formats[] =
{
  {print_object_filename_bsd,
   print_archive_filename_bsd,
   print_archive_member_bsd,
   print_symbol_filename_bsd,
   print_symbol_info_bsd},
  {print_object_filename_sysv,
   print_archive_filename_sysv,
   print_archive_member_sysv,
   print_symbol_filename_sysv,
   print_symbol_info_sysv},
  {print_object_filename_posix,
   print_archive_filename_posix,
   print_archive_member_posix,
   print_symbol_filename_posix,
   print_symbol_info_posix}
};

/* Indices in `formats'.  */
#define FORMAT_BSD 0
#define FORMAT_SYSV 1
#define FORMAT_POSIX 2
#define FORMAT_DEFAULT FORMAT_BSD

/* The output format to use.  */
static struct output_fns *format = &formats[FORMAT_DEFAULT];

/* Command options.  */

static int do_demangle = 0;	/* Pretty print C++ symbol names.  */
static int external_only = 0;	/* Print external symbols only.  */
static int defined_only = 0;	/* Print defined symbols only.  */
static int no_sort = 0;		/* Don't sort; print syms in order found.  */
static int print_debug_syms = 0;/* Print debugger-only symbols too.  */
static int print_armap = 0;	/* Describe __.SYMDEF data in archive files.  */
static int print_size = 0;	/* Print size of defined symbols.  */
static int reverse_sort = 0;	/* Sort in downward(alpha or numeric) order.  */
static int sort_numerically = 0;/* Sort in numeric rather than alpha order.  */
static int sort_by_size = 0;	/* Sort by size of symbol.  */
static int undefined_only = 0;	/* Print undefined symbols only.  */
static int dynamic = 0;		/* Print dynamic symbols.  */
static int show_version = 0;	/* Show the version number.  */
static int show_stats = 0;	/* Show statistics.  */
static int show_synthetic = 0;	/* Display synthesized symbols too.  */
static int line_numbers = 0;	/* Print line numbers for symbols.  */
static int allow_special_symbols = 0;  /* Allow special symbols.  */

/* When to print the names of files.  Not mutually exclusive in SYSV format.  */
static int filename_per_file = 0;	/* Once per file, on its own line.  */
static int filename_per_symbol = 0;	/* Once per symbol, at start of line.  */

/* Print formats for printing a symbol value.  */
static char value_format_32bit[] = "%08lx";
static char value_format_64bit[] = "%016lx";
static int print_width = 0;
static int print_radix = 16;
/* Print formats for printing stab info.  */
static char other_format[] = "%02x";
static char desc_format[] = "%04x";

static char *target = NULL;

/* Used to cache the line numbers for a BFD.  */
static bfd *lineno_cache_bfd;
static bfd *lineno_cache_rel_bfd;

#define OPTION_TARGET 200

static struct option long_options[] =
{
  {"debug-syms", no_argument, &print_debug_syms, 1},
  {"demangle", optional_argument, 0, 'C'},
  {"dynamic", no_argument, &dynamic, 1},
  {"extern-only", no_argument, &external_only, 1},
  {"format", required_argument, 0, 'f'},
  {"help", no_argument, 0, 'h'},
  {"line-numbers", no_argument, 0, 'l'},
  {"no-cplus", no_argument, &do_demangle, 0},  /* Linux compatibility.  */
  {"no-demangle", no_argument, &do_demangle, 0},
  {"no-sort", no_argument, &no_sort, 1},
  {"numeric-sort", no_argument, &sort_numerically, 1},
  {"portability", no_argument, 0, 'P'},
  {"print-armap", no_argument, &print_armap, 1},
  {"print-file-name", no_argument, 0, 'o'},
  {"print-size", no_argument, 0, 'S'},
  {"radix", required_argument, 0, 't'},
  {"reverse-sort", no_argument, &reverse_sort, 1},
  {"size-sort", no_argument, &sort_by_size, 1},
  {"special-syms", no_argument, &allow_special_symbols, 1},
  {"stats", no_argument, &show_stats, 1},
  {"synthetic", no_argument, &show_synthetic, 1},
  {"target", required_argument, 0, OPTION_TARGET},
  {"defined-only", no_argument, &defined_only, 1},
  {"undefined-only", no_argument, &undefined_only, 1},
  {"version", no_argument, &show_version, 1},
  {0, no_argument, 0, 0}
};

/* Some error-reporting functions.  */

static void
usage (FILE *stream, int status)
{
  fprintf (stream, _("Usage: %s [option(s)] [file(s)]\n"), program_name);
  fprintf (stream, _(" List symbols in [file(s)] (a.out by default).\n"));
  fprintf (stream, _(" The options are:\n\
  -a, --debug-syms       Display debugger-only symbols\n\
  -A, --print-file-name  Print name of the input file before every symbol\n\
  -B                     Same as --format=bsd\n\
  -C, --demangle[=STYLE] Decode low-level symbol names into user-level names\n\
                          The STYLE, if specified, can be `auto' (the default),\n\
                          `gnu', `lucid', `arm', `hp', `edg', `gnu-v3', `java'\n\
                          or `gnat'\n\
      --no-demangle      Do not demangle low-level symbol names\n\
  -D, --dynamic          Display dynamic symbols instead of normal symbols\n\
      --defined-only     Display only defined symbols\n\
  -e                     (ignored)\n\
  -f, --format=FORMAT    Use the output format FORMAT.  FORMAT can be `bsd',\n\
                           `sysv' or `posix'.  The default is `bsd'\n\
  -g, --extern-only      Display only external symbols\n\
  -l, --line-numbers     Use debugging information to find a filename and\n\
                           line number for each symbol\n\
  -n, --numeric-sort     Sort symbols numerically by address\n\
  -o                     Same as -A\n\
  -p, --no-sort          Do not sort the symbols\n\
  -P, --portability      Same as --format=posix\n\
  -r, --reverse-sort     Reverse the sense of the sort\n\
  -S, --print-size       Print size of defined symbols\n\
  -s, --print-armap      Include index for symbols from archive members\n\
      --size-sort        Sort symbols by size\n\
      --special-syms     Include special symbols in the output\n\
      --synthetic        Display synthetic symbols as well\n\
  -t, --radix=RADIX      Use RADIX for printing symbol values\n\
      --target=BFDNAME   Specify the target object format as BFDNAME\n\
  -u, --undefined-only   Display only undefined symbols\n\
  -X 32_64               (ignored)\n\
  @FILE                  Read options from FILE\n\
  -h, --help             Display this information\n\
  -V, --version          Display this program's version number\n\
\n"));
  list_supported_targets (program_name, stream);
  if (REPORT_BUGS_TO[0] && status == 0)
    fprintf (stream, _("Report bugs to %s.\n"), REPORT_BUGS_TO);
  exit (status);
}

/* Set the radix for the symbol value and size according to RADIX.  */

static void
set_print_radix (char *radix)
{
  switch (*radix)
    {
    case 'x':
      break;
    case 'd':
    case 'o':
      if (*radix == 'd')
	print_radix = 10;
      else
	print_radix = 8;
      value_format_32bit[4] = *radix;
      value_format_64bit[5] = *radix;
      other_format[3] = desc_format[3] = *radix;
      break;
    default:
      fatal (_("%s: invalid radix"), radix);
    }
}

static void
set_output_format (char *f)
{
  int i;

  switch (*f)
    {
    case 'b':
    case 'B':
      i = FORMAT_BSD;
      break;
    case 'p':
    case 'P':
      i = FORMAT_POSIX;
      break;
    case 's':
    case 'S':
      i = FORMAT_SYSV;
      break;
    default:
      fatal (_("%s: invalid output format"), f);
    }
  format = &formats[i];
}

static const char *
get_symbol_type (unsigned int type)
{
  static char buff [32];

  switch (type)
    {
    case STT_NOTYPE:   return "NOTYPE";
    case STT_OBJECT:   return "OBJECT";
    case STT_FUNC:     return "FUNC";
    case STT_SECTION:  return "SECTION";
    case STT_FILE:     return "FILE";
    case STT_COMMON:   return "COMMON";
    case STT_TLS:      return "TLS";
    default:
      if (type >= STT_LOPROC && type <= STT_HIPROC)
	sprintf (buff, _("<processor specific>: %d"), type);
      else if (type >= STT_LOOS && type <= STT_HIOS)
	sprintf (buff, _("<OS specific>: %d"), type);
      else
	sprintf (buff, _("<unknown>: %d"), type);
      return buff;
    }
}

/* Print symbol name NAME, read from ABFD, with printf format FORMAT,
   demangling it if requested.  */

static void
print_symname (const char *format, const char *name, bfd *abfd)
{
  if (do_demangle && *name)
    {
      char *res = bfd_demangle (abfd, name, DMGL_ANSI | DMGL_PARAMS);

      if (res != NULL)
	{
	  printf (format, res);
	  free (res);
	  return;
	}
    }

  printf (format, name);
}

static void
print_symdef_entry (bfd *abfd)
{
  symindex idx = BFD_NO_MORE_SYMBOLS;
  carsym *thesym;
  bfd_boolean everprinted = FALSE;

  for (idx = bfd_get_next_mapent (abfd, idx, &thesym);
       idx != BFD_NO_MORE_SYMBOLS;
       idx = bfd_get_next_mapent (abfd, idx, &thesym))
    {
      bfd *elt;
      if (!everprinted)
	{
	  printf (_("\nArchive index:\n"));
	  everprinted = TRUE;
	}
      elt = bfd_get_elt_at_index (abfd, idx);
      if (elt == NULL)
	bfd_fatal ("bfd_get_elt_at_index");
      if (thesym->name != (char *) NULL)
	{
	  print_symname ("%s", thesym->name, abfd);
	  printf (" in %s\n", bfd_get_filename (elt));
	}
    }
}

/* Choose which symbol entries to print;
   compact them downward to get rid of the rest.
   Return the number of symbols to be printed.  */

static long
filter_symbols (bfd *abfd, bfd_boolean dynamic, void *minisyms,
		long symcount, unsigned int size)
{
  bfd_byte *from, *fromend, *to;
  asymbol *store;

  store = bfd_make_empty_symbol (abfd);
  if (store == NULL)
    bfd_fatal (bfd_get_filename (abfd));

  from = (bfd_byte *) minisyms;
  fromend = from + symcount * size;
  to = (bfd_byte *) minisyms;

  for (; from < fromend; from += size)
    {
      int keep = 0;
      asymbol *sym;

      PROGRESS (1);

      sym = bfd_minisymbol_to_symbol (abfd, dynamic, (const void *) from, store);
      if (sym == NULL)
	bfd_fatal (bfd_get_filename (abfd));

      if (undefined_only)
	keep = bfd_is_und_section (sym->section);
      else if (external_only)
	keep = ((sym->flags & BSF_GLOBAL) != 0
		|| (sym->flags & BSF_WEAK) != 0
		|| bfd_is_und_section (sym->section)
		|| bfd_is_com_section (sym->section));
      else
	keep = 1;

      if (keep
	  && ! print_debug_syms
	  && (sym->flags & BSF_DEBUGGING) != 0)
	keep = 0;

      if (keep
	  && sort_by_size
	  && (bfd_is_abs_section (sym->section)
	      || bfd_is_und_section (sym->section)))
	keep = 0;

      if (keep
	  && defined_only)
	{
	  if (bfd_is_und_section (sym->section))
	    keep = 0;
	}

      if (keep
	  && bfd_is_target_special_symbol (abfd, sym)
	  && ! allow_special_symbols)
	keep = 0;

      if (keep)
	{
	  memcpy (to, from, size);
	  to += size;
	}
    }

  return (to - (bfd_byte *) minisyms) / size;
}

/* These globals are used to pass information into the sorting
   routines.  */
static bfd *sort_bfd;
static bfd_boolean sort_dynamic;
static asymbol *sort_x;
static asymbol *sort_y;

/* Symbol-sorting predicates */
#define valueof(x) ((x)->section->vma + (x)->value)

/* Numeric sorts.  Undefined symbols are always considered "less than"
   defined symbols with zero values.  Common symbols are not treated
   specially -- i.e., their sizes are used as their "values".  */

static int
non_numeric_forward (const void *P_x, const void *P_y)
{
  asymbol *x, *y;
  const char *xn, *yn;

  x = bfd_minisymbol_to_symbol (sort_bfd, sort_dynamic, P_x, sort_x);
  y = bfd_minisymbol_to_symbol (sort_bfd, sort_dynamic, P_y, sort_y);
  if (x == NULL || y == NULL)
    bfd_fatal (bfd_get_filename (sort_bfd));

  xn = bfd_asymbol_name (x);
  yn = bfd_asymbol_name (y);

  if (yn == NULL)
    return xn != NULL;
  if (xn == NULL)
    return -1;

#ifdef HAVE_STRCOLL
  /* Solaris 2.5 has a bug in strcoll.
     strcoll returns invalid values when confronted with empty strings.  */
  if (*yn == '\0')
    return *xn != '\0';
  if (*xn == '\0')
    return -1;

  return strcoll (xn, yn);
#else
  return strcmp (xn, yn);
#endif
}

static int
non_numeric_reverse (const void *x, const void *y)
{
  return - non_numeric_forward (x, y);
}

static int
numeric_forward (const void *P_x, const void *P_y)
{
  asymbol *x, *y;
  asection *xs, *ys;

  x = bfd_minisymbol_to_symbol (sort_bfd, sort_dynamic, P_x, sort_x);
  y =  bfd_minisymbol_to_symbol (sort_bfd, sort_dynamic, P_y, sort_y);
  if (x == NULL || y == NULL)
    bfd_fatal (bfd_get_filename (sort_bfd));

  xs = bfd_get_section (x);
  ys = bfd_get_section (y);

  if (bfd_is_und_section (xs))
    {
      if (! bfd_is_und_section (ys))
	return -1;
    }
  else if (bfd_is_und_section (ys))
    return 1;
  else if (valueof (x) != valueof (y))
    return valueof (x) < valueof (y) ? -1 : 1;

  return non_numeric_forward (P_x, P_y);
}

static int
numeric_reverse (const void *x, const void *y)
{
  return - numeric_forward (x, y);
}

static int (*(sorters[2][2])) (const void *, const void *) =
{
  { non_numeric_forward, non_numeric_reverse },
  { numeric_forward, numeric_reverse }
};

/* This sort routine is used by sort_symbols_by_size.  It is similar
   to numeric_forward, but when symbols have the same value it sorts
   by section VMA.  This simplifies the sort_symbols_by_size code
   which handles symbols at the end of sections.  Also, this routine
   tries to sort file names before other symbols with the same value.
   That will make the file name have a zero size, which will make
   sort_symbols_by_size choose the non file name symbol, leading to
   more meaningful output.  For similar reasons, this code sorts
   gnu_compiled_* and gcc2_compiled before other symbols with the same
   value.  */

static int
size_forward1 (const void *P_x, const void *P_y)
{
  asymbol *x, *y;
  asection *xs, *ys;
  const char *xn, *yn;
  size_t xnl, ynl;
  int xf, yf;

  x = bfd_minisymbol_to_symbol (sort_bfd, sort_dynamic, P_x, sort_x);
  y = bfd_minisymbol_to_symbol (sort_bfd, sort_dynamic, P_y, sort_y);
  if (x == NULL || y == NULL)
    bfd_fatal (bfd_get_filename (sort_bfd));

  xs = bfd_get_section (x);
  ys = bfd_get_section (y);

  if (bfd_is_und_section (xs))
    abort ();
  if (bfd_is_und_section (ys))
    abort ();

  if (valueof (x) != valueof (y))
    return valueof (x) < valueof (y) ? -1 : 1;

  if (xs->vma != ys->vma)
    return xs->vma < ys->vma ? -1 : 1;

  xn = bfd_asymbol_name (x);
  yn = bfd_asymbol_name (y);
  xnl = strlen (xn);
  ynl = strlen (yn);

  /* The symbols gnu_compiled and gcc2_compiled convey even less
     information than the file name, so sort them out first.  */

  xf = (strstr (xn, "gnu_compiled") != NULL
	|| strstr (xn, "gcc2_compiled") != NULL);
  yf = (strstr (yn, "gnu_compiled") != NULL
	|| strstr (yn, "gcc2_compiled") != NULL);

  if (xf && ! yf)
    return -1;
  if (! xf && yf)
    return 1;

  /* We use a heuristic for the file name.  It may not work on non
     Unix systems, but it doesn't really matter; the only difference
     is precisely which symbol names get printed.  */

#define file_symbol(s, sn, snl)			\
  (((s)->flags & BSF_FILE) != 0			\
   || ((sn)[(snl) - 2] == '.'			\
       && ((sn)[(snl) - 1] == 'o'		\
	   || (sn)[(snl) - 1] == 'a')))

  xf = file_symbol (x, xn, xnl);
  yf = file_symbol (y, yn, ynl);

  if (xf && ! yf)
    return -1;
  if (! xf && yf)
    return 1;

  return non_numeric_forward (P_x, P_y);
}

/* This sort routine is used by sort_symbols_by_size.  It is sorting
   an array of size_sym structures into size order.  */

static int
size_forward2 (const void *P_x, const void *P_y)
{
  const struct size_sym *x = (const struct size_sym *) P_x;
  const struct size_sym *y = (const struct size_sym *) P_y;

  if (x->size < y->size)
    return reverse_sort ? 1 : -1;
  else if (x->size > y->size)
    return reverse_sort ? -1 : 1;
  else
    return sorters[0][reverse_sort] (x->minisym, y->minisym);
}

/* Sort the symbols by size.  ELF provides a size but for other formats
   we have to make a guess by assuming that the difference between the
   address of a symbol and the address of the next higher symbol is the
   size.  */

static long
sort_symbols_by_size (bfd *abfd, bfd_boolean dynamic, void *minisyms,
		      long symcount, unsigned int size,
		      struct size_sym **symsizesp)
{
  struct size_sym *symsizes;
  bfd_byte *from, *fromend;
  asymbol *sym = NULL;
  asymbol *store_sym, *store_next;

  qsort (minisyms, symcount, size, size_forward1);

  /* We are going to return a special set of symbols and sizes to
     print.  */
  symsizes = xmalloc (symcount * sizeof (struct size_sym));
  *symsizesp = symsizes;

  /* Note that filter_symbols has already removed all absolute and
     undefined symbols.  Here we remove all symbols whose size winds
     up as zero.  */
  from = (bfd_byte *) minisyms;
  fromend = from + symcount * size;

  store_sym = sort_x;
  store_next = sort_y;

  if (from < fromend)
    {
      sym = bfd_minisymbol_to_symbol (abfd, dynamic, (const void *) from,
				      store_sym);
      if (sym == NULL)
	bfd_fatal (bfd_get_filename (abfd));
    }

  for (; from < fromend; from += size)
    {
      asymbol *next;
      asection *sec;
      bfd_vma sz;
      asymbol *temp;

      if (from + size < fromend)
	{
	  next = bfd_minisymbol_to_symbol (abfd,
					   dynamic,
					   (const void *) (from + size),
					   store_next);
	  if (next == NULL)
	    bfd_fatal (bfd_get_filename (abfd));
	}
      else
	next = NULL;

      sec = bfd_get_section (sym);

      if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
	sz = ((elf_symbol_type *) sym)->internal_elf_sym.st_size;
      else if (bfd_is_com_section (sec))
	sz = sym->value;
      else
	{
	  if (from + size < fromend
	      && sec == bfd_get_section (next))
	    sz = valueof (next) - valueof (sym);
	  else
	    sz = (bfd_get_section_vma (abfd, sec)
		  + bfd_section_size (abfd, sec)
		  - valueof (sym));
	}

      if (sz != 0)
	{
	  symsizes->minisym = (const void *) from;
	  symsizes->size = sz;
	  ++symsizes;
	}

      sym = next;

      temp = store_sym;
      store_sym = store_next;
      store_next = temp;
    }

  symcount = symsizes - *symsizesp;

  /* We must now sort again by size.  */
  qsort ((void *) *symsizesp, symcount, sizeof (struct size_sym), size_forward2);

  return symcount;
}

/* This function is used to get the relocs for a particular section.
   It is called via bfd_map_over_sections.  */

static void
get_relocs (bfd *abfd, asection *sec, void *dataarg)
{
  struct get_relocs_info *data = (struct get_relocs_info *) dataarg;

  *data->secs = sec;

  if ((sec->flags & SEC_RELOC) == 0)
    {
      *data->relocs = NULL;
      *data->relcount = 0;
    }
  else
    {
      long relsize;

      relsize = bfd_get_reloc_upper_bound (abfd, sec);
      if (relsize < 0)
	bfd_fatal (bfd_get_filename (abfd));

      *data->relocs = xmalloc (relsize);
      *data->relcount = bfd_canonicalize_reloc (abfd, sec, *data->relocs,
						data->syms);
      if (*data->relcount < 0)
	bfd_fatal (bfd_get_filename (abfd));
    }

  ++data->secs;
  ++data->relocs;
  ++data->relcount;
}

/* Print a single symbol.  */

static void
print_symbol (bfd *abfd, asymbol *sym, bfd_vma ssize, bfd *archive_bfd)
{
  symbol_info syminfo;
  struct extended_symbol_info info;

  PROGRESS (1);

  format->print_symbol_filename (archive_bfd, abfd);

  bfd_get_symbol_info (abfd, sym, &syminfo);
  info.sinfo = &syminfo;
  info.ssize = ssize;
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
    info.elfinfo = (elf_symbol_type *) sym;
  else
    info.elfinfo = NULL;
  format->print_symbol_info (&info, abfd);

  if (line_numbers)
    {
      static asymbol **syms;
      static long symcount;
      const char *filename, *functionname;
      unsigned int lineno;

      /* We need to get the canonical symbols in order to call
         bfd_find_nearest_line.  This is inefficient, but, then, you
         don't have to use --line-numbers.  */
      if (abfd != lineno_cache_bfd && syms != NULL)
	{
	  free (syms);
	  syms = NULL;
	}
      if (syms == NULL)
	{
	  long symsize;

	  symsize = bfd_get_symtab_upper_bound (abfd);
	  if (symsize < 0)
	    bfd_fatal (bfd_get_filename (abfd));
	  syms = xmalloc (symsize);
	  symcount = bfd_canonicalize_symtab (abfd, syms);
	  if (symcount < 0)
	    bfd_fatal (bfd_get_filename (abfd));
	  lineno_cache_bfd = abfd;
	}

      if (bfd_is_und_section (bfd_get_section (sym)))
	{
	  static asection **secs;
	  static arelent ***relocs;
	  static long *relcount;
	  static unsigned int seccount;
	  unsigned int i;
	  const char *symname;

	  /* For an undefined symbol, we try to find a reloc for the
             symbol, and print the line number of the reloc.  */
	  if (abfd != lineno_cache_rel_bfd && relocs != NULL)
	    {
	      for (i = 0; i < seccount; i++)
		if (relocs[i] != NULL)
		  free (relocs[i]);
	      free (secs);
	      free (relocs);
	      free (relcount);
	      secs = NULL;
	      relocs = NULL;
	      relcount = NULL;
	    }

	  if (relocs == NULL)
	    {
	      struct get_relocs_info info;

	      seccount = bfd_count_sections (abfd);

	      secs = xmalloc (seccount * sizeof *secs);
	      relocs = xmalloc (seccount * sizeof *relocs);
	      relcount = xmalloc (seccount * sizeof *relcount);

	      info.secs = secs;
	      info.relocs = relocs;
	      info.relcount = relcount;
	      info.syms = syms;
	      bfd_map_over_sections (abfd, get_relocs, (void *) &info);
	      lineno_cache_rel_bfd = abfd;
	    }

	  symname = bfd_asymbol_name (sym);
	  for (i = 0; i < seccount; i++)
	    {
	      long j;

	      for (j = 0; j < relcount[i]; j++)
		{
		  arelent *r;

		  r = relocs[i][j];
		  if (r->sym_ptr_ptr != NULL
		      && (*r->sym_ptr_ptr)->section == sym->section
		      && (*r->sym_ptr_ptr)->value == sym->value
		      && strcmp (symname,
				 bfd_asymbol_name (*r->sym_ptr_ptr)) == 0
		      && bfd_find_nearest_line (abfd, secs[i], syms,
						r->address, &filename,
						&functionname, &lineno)
		      && filename != NULL)
		    {
		      /* We only print the first one we find.  */
		      printf ("\t%s:%u", filename, lineno);
		      i = seccount;
		      break;
		    }
		}
	    }
	}
      else if (bfd_get_section (sym)->owner == abfd)
	{
	  if ((bfd_find_line (abfd, syms, sym, &filename, &lineno)
	       || bfd_find_nearest_line (abfd, bfd_get_section (sym),
					 syms, sym->value, &filename,
					 &functionname, &lineno))
	      && filename != NULL
	      && lineno != 0)
	    printf ("\t%s:%u", filename, lineno);
	}
    }

  putchar ('\n');
}

/* Print the symbols when sorting by size.  */

static void
print_size_symbols (bfd *abfd, bfd_boolean dynamic,
		    struct size_sym *symsizes, long symcount,
		    bfd *archive_bfd)
{
  asymbol *store;
  struct size_sym *from, *fromend;

  store = bfd_make_empty_symbol (abfd);
  if (store == NULL)
    bfd_fatal (bfd_get_filename (abfd));

  from = symsizes;
  fromend = from + symcount;
  for (; from < fromend; from++)
    {
      asymbol *sym;
      bfd_vma ssize;

      sym = bfd_minisymbol_to_symbol (abfd, dynamic, from->minisym, store);
      if (sym == NULL)
	bfd_fatal (bfd_get_filename (abfd));

      /* For elf we have already computed the correct symbol size.  */
      if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
	ssize = from->size;
      else
	ssize = from->size - bfd_section_vma (abfd, bfd_get_section (sym));

      print_symbol (abfd, sym, ssize, archive_bfd);
    }
}


/* Print the symbols.  If ARCHIVE_BFD is non-NULL, it is the archive
   containing ABFD.  */

static void
print_symbols (bfd *abfd, bfd_boolean dynamic, void *minisyms, long symcount,
	       unsigned int size, bfd *archive_bfd)
{
  asymbol *store;
  bfd_byte *from, *fromend;

  store = bfd_make_empty_symbol (abfd);
  if (store == NULL)
    bfd_fatal (bfd_get_filename (abfd));

  from = (bfd_byte *) minisyms;
  fromend = from + symcount * size;
  for (; from < fromend; from += size)
    {
      asymbol *sym;

      sym = bfd_minisymbol_to_symbol (abfd, dynamic, from, store);
      if (sym == NULL)
	bfd_fatal (bfd_get_filename (abfd));

      print_symbol (abfd, sym, (bfd_vma) 0, archive_bfd);
    }
}

/* If ARCHIVE_BFD is non-NULL, it is the archive containing ABFD.  */

static void
display_rel_file (bfd *abfd, bfd *archive_bfd)
{
  long symcount;
  void *minisyms;
  unsigned int size;
  struct size_sym *symsizes;

  if (! dynamic)
    {
      if (!(bfd_get_file_flags (abfd) & HAS_SYMS))
	{
	  non_fatal (_("%s: no symbols"), bfd_get_filename (abfd));
	  return;
	}
    }

  symcount = bfd_read_minisymbols (abfd, dynamic, &minisyms, &size);
  if (symcount < 0)
    bfd_fatal (bfd_get_filename (abfd));

  if (symcount == 0)
    {
      non_fatal (_("%s: no symbols"), bfd_get_filename (abfd));
      return;
    }

  if (show_synthetic && size == sizeof (asymbol *))
    {
      asymbol *synthsyms;
      long synth_count;
      asymbol **static_syms = NULL;
      asymbol **dyn_syms = NULL;
      long static_count = 0;
      long dyn_count = 0;

      if (dynamic)
	{
	  dyn_count = symcount;
	  dyn_syms = minisyms;
	}
      else
	{
	  long storage = bfd_get_dynamic_symtab_upper_bound (abfd);

	  static_count = symcount;
	  static_syms = minisyms;

	  if (storage > 0)
	    {
	      dyn_syms = xmalloc (storage);
	      dyn_count = bfd_canonicalize_dynamic_symtab (abfd, dyn_syms);
	      if (dyn_count < 0)
		bfd_fatal (bfd_get_filename (abfd));
	    }
	}
      synth_count = bfd_get_synthetic_symtab (abfd, static_count, static_syms,
					      dyn_count, dyn_syms, &synthsyms);
      if (synth_count > 0)
	{
	  asymbol **symp;
	  void *new_mini;
	  long i;

	  new_mini = xmalloc ((symcount + synth_count + 1) * sizeof (*symp));
	  symp = new_mini;
	  memcpy (symp, minisyms, symcount * sizeof (*symp));
	  symp += symcount;
	  for (i = 0; i < synth_count; i++)
	    *symp++ = synthsyms + i;
	  *symp = 0;
	  minisyms = new_mini;
	  symcount += synth_count;
	}
    }

  /* Discard the symbols we don't want to print.
     It's OK to do this in place; we'll free the storage anyway
     (after printing).  */

  symcount = filter_symbols (abfd, dynamic, minisyms, symcount, size);

  symsizes = NULL;
  if (! no_sort)
    {
      sort_bfd = abfd;
      sort_dynamic = dynamic;
      sort_x = bfd_make_empty_symbol (abfd);
      sort_y = bfd_make_empty_symbol (abfd);
      if (sort_x == NULL || sort_y == NULL)
	bfd_fatal (bfd_get_filename (abfd));

      if (! sort_by_size)
	qsort (minisyms, symcount, size,
	       sorters[sort_numerically][reverse_sort]);
      else
	symcount = sort_symbols_by_size (abfd, dynamic, minisyms, symcount,
					 size, &symsizes);
    }

  if (! sort_by_size)
    print_symbols (abfd, dynamic, minisyms, symcount, size, archive_bfd);
  else
    print_size_symbols (abfd, dynamic, symsizes, symcount, archive_bfd);

  free (minisyms);
}

static void
set_print_width (bfd *file)
{
  print_width = bfd_get_arch_size (file);

  if (print_width == -1)
    {
      /* PR binutils/4292
	 Guess the target's bitsize based on its name.
	 We assume here than any 64-bit format will include
	 "64" somewhere in its name.  The only known exception
	 is the MMO object file format.  */
      if (strstr (bfd_get_target (file), "64") != NULL
	  || strcmp (bfd_get_target (file), "mmo") == 0)
	print_width = 64;
      else
	print_width = 32;
    }
}

static void
display_archive (bfd *file)
{
  bfd *arfile = NULL;
  bfd *last_arfile = NULL;
  char **matching;

  format->print_archive_filename (bfd_get_filename (file));

  if (print_armap)
    print_symdef_entry (file);

  for (;;)
    {
      PROGRESS (1);

      arfile = bfd_openr_next_archived_file (file, arfile);

      if (arfile == NULL)
	{
	  if (bfd_get_error () != bfd_error_no_more_archived_files)
	    bfd_fatal (bfd_get_filename (file));
	  break;
	}

      if (bfd_check_format_matches (arfile, bfd_object, &matching))
	{
	  set_print_width (arfile);
	  format->print_archive_member (bfd_get_filename (file),
					bfd_get_filename (arfile));
	  display_rel_file (arfile, file);
	}
      else
	{
	  bfd_nonfatal (bfd_get_filename (arfile));
	  if (bfd_get_error () == bfd_error_file_ambiguously_recognized)
	    {
	      list_matching_formats (matching);
	      free (matching);
	    }
	}

      if (last_arfile != NULL)
	{
	  bfd_close (last_arfile);
	  lineno_cache_bfd = NULL;
	  lineno_cache_rel_bfd = NULL;
	}
      last_arfile = arfile;
    }

  if (last_arfile != NULL)
    {
      bfd_close (last_arfile);
      lineno_cache_bfd = NULL;
      lineno_cache_rel_bfd = NULL;
    }
}

static bfd_boolean
display_file (char *filename)
{
  bfd_boolean retval = TRUE;
  bfd *file;
  char **matching;

  if (get_file_size (filename) < 1)
    return FALSE;

  file = bfd_openr (filename, target);
  if (file == NULL)
    {
      bfd_nonfatal (filename);
      return FALSE;
    }

  if (bfd_check_format (file, bfd_archive))
    {
      display_archive (file);
    }
  else if (bfd_check_format_matches (file, bfd_object, &matching))
    {
      set_print_width (file);
      format->print_object_filename (filename);
      display_rel_file (file, NULL);
    }
  else
    {
      bfd_nonfatal (filename);
      if (bfd_get_error () == bfd_error_file_ambiguously_recognized)
	{
	  list_matching_formats (matching);
	  free (matching);
	}
      retval = FALSE;
    }

  if (!bfd_close (file))
    bfd_fatal (filename);

  lineno_cache_bfd = NULL;
  lineno_cache_rel_bfd = NULL;

  return retval;
}

/* The following 3 groups of functions are called unconditionally,
   once at the start of processing each file of the appropriate type.
   They should check `filename_per_file' and `filename_per_symbol',
   as appropriate for their output format, to determine whether to
   print anything.  */

/* Print the name of an object file given on the command line.  */

static void
print_object_filename_bsd (char *filename)
{
  if (filename_per_file && !filename_per_symbol)
    printf ("\n%s:\n", filename);
}

static void
print_object_filename_sysv (char *filename)
{
  if (undefined_only)
    printf (_("\n\nUndefined symbols from %s:\n\n"), filename);
  else
    printf (_("\n\nSymbols from %s:\n\n"), filename);
  if (print_width == 32)
    printf (_("\
Name                  Value   Class        Type         Size     Line  Section\n\n"));
  else
    printf (_("\
Name                  Value           Class        Type         Size             Line  Section\n\n"));
}

static void
print_object_filename_posix (char *filename)
{
  if (filename_per_file && !filename_per_symbol)
    printf ("%s:\n", filename);
}

/* Print the name of an archive file given on the command line.  */

static void
print_archive_filename_bsd (char *filename)
{
  if (filename_per_file)
    printf ("\n%s:\n", filename);
}

static void
print_archive_filename_sysv (char *filename ATTRIBUTE_UNUSED)
{
}

static void
print_archive_filename_posix (char *filename ATTRIBUTE_UNUSED)
{
}

/* Print the name of an archive member file.  */

static void
print_archive_member_bsd (char *archive ATTRIBUTE_UNUSED,
			  const char *filename)
{
  if (!filename_per_symbol)
    printf ("\n%s:\n", filename);
}

static void
print_archive_member_sysv (char *archive, const char *filename)
{
  if (undefined_only)
    printf (_("\n\nUndefined symbols from %s[%s]:\n\n"), archive, filename);
  else
    printf (_("\n\nSymbols from %s[%s]:\n\n"), archive, filename);
  if (print_width == 32)
    printf (_("\
Name                  Value   Class        Type         Size     Line  Section\n\n"));
  else
    printf (_("\
Name                  Value           Class        Type         Size             Line  Section\n\n"));
}

static void
print_archive_member_posix (char *archive, const char *filename)
{
  if (!filename_per_symbol)
    printf ("%s[%s]:\n", archive, filename);
}

/* Print the name of the file (and archive, if there is one)
   containing a symbol.  */

static void
print_symbol_filename_bsd (bfd *archive_bfd, bfd *abfd)
{
  if (filename_per_symbol)
    {
      if (archive_bfd)
	printf ("%s:", bfd_get_filename (archive_bfd));
      printf ("%s:", bfd_get_filename (abfd));
    }
}

static void
print_symbol_filename_sysv (bfd *archive_bfd, bfd *abfd)
{
  if (filename_per_symbol)
    {
      if (archive_bfd)
	printf ("%s:", bfd_get_filename (archive_bfd));
      printf ("%s:", bfd_get_filename (abfd));
    }
}

static void
print_symbol_filename_posix (bfd *archive_bfd, bfd *abfd)
{
  if (filename_per_symbol)
    {
      if (archive_bfd)
	printf ("%s[%s]: ", bfd_get_filename (archive_bfd),
		bfd_get_filename (abfd));
      else
	printf ("%s: ", bfd_get_filename (abfd));
    }
}

/* Print a symbol value.  */

static void
print_value (bfd *abfd ATTRIBUTE_UNUSED, bfd_vma val)
{
  switch (print_width)
    {
    case 32:
      printf (value_format_32bit, (unsigned long) val);
      break;

    case 64:
#if BFD_HOST_64BIT_LONG
      printf (value_format_64bit, val);
#else
      /* We have a 64 bit value to print, but the host is only 32 bit.  */
      if (print_radix == 16)
	bfd_fprintf_vma (abfd, stdout, val);
      else
	{
	  char buf[30];
	  char *s;

	  s = buf + sizeof buf;
	  *--s = '\0';
	  while (val > 0)
	    {
	      *--s = (val % print_radix) + '0';
	      val /= print_radix;
	    }
	  while ((buf + sizeof buf - 1) - s < 16)
	    *--s = '0';
	  printf ("%s", s);
	}
#endif
      break;

    default:
      fatal (_("Print width has not been initialized (%d)"), print_width);
      break;
    }
}

/* Print a line of information about a symbol.  */

static void
print_symbol_info_bsd (struct extended_symbol_info *info, bfd *abfd)
{
  if (bfd_is_undefined_symclass (SYM_TYPE (info)))
    {
      if (print_width == 64)
	printf ("        ");
      printf ("        ");
    }
  else
    {
      /* Normally we print the value of the symbol.  If we are printing the
	 size or sorting by size then we print its size, except for the
	 (weird) special case where both flags are defined, in which case we
	 print both values.  This conforms to documented behaviour.  */
      if (sort_by_size && !print_size)
	print_value (abfd, SYM_SIZE (info));
      else
	print_value (abfd, SYM_VALUE (info));

      if (print_size && SYM_SIZE (info))
	{
	  printf (" ");
	  print_value (abfd, SYM_SIZE (info));
	}
    }

  printf (" %c", SYM_TYPE (info));

  if (SYM_TYPE (info) == '-')
    {
      /* A stab.  */
      printf (" ");
      printf (other_format, SYM_STAB_OTHER (info));
      printf (" ");
      printf (desc_format, SYM_STAB_DESC (info));
      printf (" %5s", SYM_STAB_NAME (info));
    }
  print_symname (" %s", SYM_NAME (info), abfd);
}

static void
print_symbol_info_sysv (struct extended_symbol_info *info, bfd *abfd)
{
  print_symname ("%-20s|", SYM_NAME (info), abfd);

  if (bfd_is_undefined_symclass (SYM_TYPE (info)))
    {
      if (print_width == 32)
	printf ("        ");
      else
	printf ("                ");
    }
  else
    print_value (abfd, SYM_VALUE (info));

  printf ("|   %c  |", SYM_TYPE (info));

  if (SYM_TYPE (info) == '-')
    {
      /* A stab.  */
      printf ("%18s|  ", SYM_STAB_NAME (info));		/* (C) Type.  */
      printf (desc_format, SYM_STAB_DESC (info));	/* Size.  */
      printf ("|     |");				/* Line, Section.  */
    }
  else
    {
      /* Type, Size, Line, Section */
      if (info->elfinfo)
	printf ("%18s|",
		get_symbol_type (ELF_ST_TYPE (info->elfinfo->internal_elf_sym.st_info)));
      else
	printf ("                  |");

      if (SYM_SIZE (info))
	print_value (abfd, SYM_SIZE (info));
      else
	{
	  if (print_width == 32)
	    printf ("        ");
	  else
	    printf ("                ");
	}

      if (info->elfinfo)
	printf("|     |%s", info->elfinfo->symbol.section->name);
      else
	printf("|     |");
    }
}

static void
print_symbol_info_posix (struct extended_symbol_info *info, bfd *abfd)
{
  print_symname ("%s ", SYM_NAME (info), abfd);
  printf ("%c ", SYM_TYPE (info));

  if (bfd_is_undefined_symclass (SYM_TYPE (info)))
    printf ("        ");
  else
    {
      print_value (abfd, SYM_VALUE (info));
      printf (" ");
      if (SYM_SIZE (info))
	print_value (abfd, SYM_SIZE (info));
    }
}

int
main (int argc, char **argv)
{
  int c;
  int retval;

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
  setlocale (LC_COLLATE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  program_name = *argv;
  xmalloc_set_program_name (program_name);

  START_PROGRESS (program_name, 0);

  expandargv (&argc, &argv);

  bfd_init ();
  set_default_bfd_target ();

  while ((c = getopt_long (argc, argv, "aABCDef:gHhlnopPrSst:uvVvX:",
			   long_options, (int *) 0)) != EOF)
    {
      switch (c)
	{
	case 'a':
	  print_debug_syms = 1;
	  break;
	case 'A':
	case 'o':
	  filename_per_symbol = 1;
	  break;
	case 'B':		/* For MIPS compatibility.  */
	  set_output_format ("bsd");
	  break;
	case 'C':
	  do_demangle = 1;
	  if (optarg != NULL)
	    {
	      enum demangling_styles style;

	      style = cplus_demangle_name_to_style (optarg);
	      if (style == unknown_demangling)
		fatal (_("unknown demangling style `%s'"),
		       optarg);

	      cplus_demangle_set_style (style);
	    }
	  break;
	case 'D':
	  dynamic = 1;
	  break;
	case 'e':
	  /* Ignored for HP/UX compatibility.  */
	  break;
	case 'f':
	  set_output_format (optarg);
	  break;
	case 'g':
	  external_only = 1;
	  break;
	case 'H':
	case 'h':
	  usage (stdout, 0);
	case 'l':
	  line_numbers = 1;
	  break;
	case 'n':
	case 'v':
	  sort_numerically = 1;
	  break;
	case 'p':
	  no_sort = 1;
	  break;
	case 'P':
	  set_output_format ("posix");
	  break;
	case 'r':
	  reverse_sort = 1;
	  break;
	case 's':
	  print_armap = 1;
	  break;
	case 'S':
	  print_size = 1;
	  break;
	case 't':
	  set_print_radix (optarg);
	  break;
	case 'u':
	  undefined_only = 1;
	  break;
	case 'V':
	  show_version = 1;
	  break;
	case 'X':
	  /* Ignored for (partial) AIX compatibility.  On AIX, the
	     argument has values 32, 64, or 32_64, and specifies that
	     only 32-bit, only 64-bit, or both kinds of objects should
	     be examined.  The default is 32.  So plain AIX nm on a
	     library archive with both kinds of objects will ignore
	     the 64-bit ones.  For GNU nm, the default is and always
	     has been -X 32_64, and other options are not supported.  */
	  if (strcmp (optarg, "32_64") != 0)
	    fatal (_("Only -X 32_64 is supported"));
	  break;

	case OPTION_TARGET:	/* --target */
	  target = optarg;
	  break;

	case 0:		/* A long option that just sets a flag.  */
	  break;

	default:
	  usage (stderr, 1);
	}
    }

  if (show_version)
    print_version ("nm");

  if (sort_by_size && undefined_only)
    {
      non_fatal (_("Using the --size-sort and --undefined-only options together"));
      non_fatal (_("will produce no output, since undefined symbols have no size."));
      return 0;
    }

  /* OK, all options now parsed.  If no filename specified, do a.out.  */
  if (optind == argc)
    return !display_file ("a.out");

  retval = 0;

  if (argc - optind > 1)
    filename_per_file = 1;

  /* We were given several filenames to do.  */
  while (optind < argc)
    {
      PROGRESS (1);
      if (!display_file (argv[optind++]))
	retval++;
    }

  END_PROGRESS (program_name);

#ifdef HAVE_SBRK
  if (show_stats)
    {
      char *lim = (char *) sbrk (0);

      non_fatal (_("data size %ld"), (long) (lim - (char *) &environ));
    }
#endif

  exit (retval);
  return retval;
}
