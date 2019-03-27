/* as.c - GAS main program.
   Copyright 1987, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Main program for AS; a 32-bit assembler of GNU.
   Understands command arguments.
   Has a few routines that don't fit in other modules because they
   are shared.
  
  			bugs
  
   : initialisers
  	Since no-one else says they will support them in future: I
   don't support them now.  */

#define COMMON

#include "as.h"
#include "subsegs.h"
#include "output-file.h"
#include "sb.h"
#include "macro.h"
#include "dwarf2dbg.h"
#include "dw2gencfi.h"
#include "bfdver.h"

#ifdef HAVE_ITBL_CPU
#include "itbl-ops.h"
#else
#define itbl_parse(itbl_file) 1
#define itbl_init()
#endif

#ifdef HAVE_SBRK
#ifdef NEED_DECLARATION_SBRK
extern PTR sbrk ();
#endif
#endif

#ifdef USING_CGEN
/* Perform any cgen specific initialisation for gas.  */
extern void gas_cgen_begin (void);
#endif

/* Keep a record of the itbl files we read in.  */
struct itbl_file_list
{
  struct itbl_file_list *next;
  char *name;
};

/* We build a list of defsyms as we read the options, and then define
   them after we have initialized everything.  */
struct defsym_list
{
  struct defsym_list *next;
  char *name;
  valueT value;
};


/* True if a listing is wanted.  */
int listing;

/* Type of debugging to generate.  */
enum debug_info_type debug_type = DEBUG_UNSPECIFIED;
int use_gnu_debug_info_extensions = 0;

#ifndef MD_DEBUG_FORMAT_SELECTOR
#define MD_DEBUG_FORMAT_SELECTOR NULL
#endif
static enum debug_info_type (*md_debug_format_selector) (int *) = MD_DEBUG_FORMAT_SELECTOR;

/* Maximum level of macro nesting.  */
int max_macro_nest = 100;

/* argv[0]  */
static char * myname;

/* The default obstack chunk size.  If we set this to zero, the
   obstack code will use whatever will fit in a 4096 byte block.  */
int chunksize = 0;

/* To monitor memory allocation more effectively, make this non-zero.
   Then the chunk sizes for gas and bfd will be reduced.  */
int debug_memory = 0;

/* Enable verbose mode.  */
int verbose = 0;

segT reg_section;
segT expr_section;
segT text_section;
segT data_section;
segT bss_section;

/* Name of listing file.  */
static char *listing_filename = NULL;

static struct defsym_list *defsyms;

static struct itbl_file_list *itbl_files;

static long start_time;

static int flag_macro_alternate;


#ifdef USE_EMULATIONS
#define EMULATION_ENVIRON "AS_EMULATION"

extern struct emulation mipsbelf, mipslelf, mipself;
extern struct emulation mipsbecoff, mipslecoff, mipsecoff;
extern struct emulation i386coff, i386elf, i386aout;
extern struct emulation crisaout, criself;

static struct emulation *const emulations[] = { EMULATIONS };
static const int n_emulations = sizeof (emulations) / sizeof (emulations[0]);

static void
select_emulation_mode (int argc, char **argv)
{
  int i;
  char *p, *em = 0;

  for (i = 1; i < argc; i++)
    if (!strncmp ("--em", argv[i], 4))
      break;

  if (i == argc)
    goto do_default;

  p = strchr (argv[i], '=');
  if (p)
    p++;
  else
    p = argv[i + 1];

  if (!p || !*p)
    as_fatal (_("missing emulation mode name"));
  em = p;

 do_default:
  if (em == 0)
    em = getenv (EMULATION_ENVIRON);
  if (em == 0)
    em = DEFAULT_EMULATION;

  if (em)
    {
      for (i = 0; i < n_emulations; i++)
	if (!strcmp (emulations[i]->name, em))
	  break;
      if (i == n_emulations)
	as_fatal (_("unrecognized emulation name `%s'"), em);
      this_emulation = emulations[i];
    }
  else
    this_emulation = emulations[0];

  this_emulation->init ();
}

const char *
default_emul_bfd_name (void)
{
  abort ();
  return NULL;
}

void
common_emul_init (void)
{
  this_format = this_emulation->format;

  if (this_emulation->leading_underscore == 2)
    this_emulation->leading_underscore = this_format->dfl_leading_underscore;

  if (this_emulation->default_endian != 2)
    target_big_endian = this_emulation->default_endian;

  if (this_emulation->fake_label_name == 0)
    {
      if (this_emulation->leading_underscore)
	this_emulation->fake_label_name = "L0\001";
      else
	/* What other parameters should we test?  */
	this_emulation->fake_label_name = ".L0\001";
    }
}
#endif

void
print_version_id (void)
{
  static int printed;

  if (printed)
    return;
  printed = 1;

  fprintf (stderr, _("GNU assembler version %s (%s) using BFD version %s\n"),
	   VERSION, TARGET_ALIAS, BFD_VERSION_STRING);
}

static void
show_usage (FILE * stream)
{
  fprintf (stream, _("Usage: %s [option...] [asmfile...]\n"), myname);

  fprintf (stream, _("\
Options:\n\
  -a[sub-option...]	  turn on listings\n\
                      	  Sub-options [default hls]:\n\
                      	  c      omit false conditionals\n\
                      	  d      omit debugging directives\n\
                      	  h      include high-level source\n\
                      	  l      include assembly\n\
                      	  m      include macro expansions\n\
                      	  n      omit forms processing\n\
                      	  s      include symbols\n\
                      	  =FILE  list to FILE (must be last sub-option)\n"));

  fprintf (stream, _("\
  --alternate             initially turn on alternate macro syntax\n"));
  fprintf (stream, _("\
  -D                      produce assembler debugging messages\n"));
  fprintf (stream, _("\
  --defsym SYM=VAL        define symbol SYM to given value\n"));
#ifdef USE_EMULATIONS
  {
    int i;
    char *def_em;

    fprintf (stream, "\
  --em=[");
    for (i = 0; i < n_emulations - 1; i++)
      fprintf (stream, "%s | ", emulations[i]->name);
    fprintf (stream, "%s]\n", emulations[i]->name);

    def_em = getenv (EMULATION_ENVIRON);
    if (!def_em)
      def_em = DEFAULT_EMULATION;
    fprintf (stream, _("\
                          emulate output (default %s)\n"), def_em);
  }
#endif
#if defined OBJ_ELF || defined OBJ_MAYBE_ELF
  fprintf (stream, _("\
  --execstack             require executable stack for this object\n"));
  fprintf (stream, _("\
  --noexecstack           don't require executable stack for this object\n"));
#endif
  fprintf (stream, _("\
  -f                      skip whitespace and comment preprocessing\n"));
  fprintf (stream, _("\
  -g --gen-debug          generate debugging information\n"));
  fprintf (stream, _("\
  --gstabs                generate STABS debugging information\n"));
  fprintf (stream, _("\
  --gstabs+               generate STABS debug info with GNU extensions\n"));
  fprintf (stream, _("\
  --gdwarf-2              generate DWARF2 debugging information\n"));
  fprintf (stream, _("\
  --hash-size=<value>     set the hash table size close to <value>\n"));
  fprintf (stream, _("\
  --help                  show this message and exit\n"));
  fprintf (stream, _("\
  --target-help           show target specific options\n"));
  fprintf (stream, _("\
  -I DIR                  add DIR to search list for .include directives\n"));
  fprintf (stream, _("\
  -J                      don't warn about signed overflow\n"));
  fprintf (stream, _("\
  -K                      warn when differences altered for long displacements\n"));
  fprintf (stream, _("\
  -L,--keep-locals        keep local symbols (e.g. starting with `L')\n"));
  fprintf (stream, _("\
  -M,--mri                assemble in MRI compatibility mode\n"));
  fprintf (stream, _("\
  --MD FILE               write dependency information in FILE (default none)\n"));
  fprintf (stream, _("\
  -nocpp                  ignored\n"));
  fprintf (stream, _("\
  -o OBJFILE              name the object-file output OBJFILE (default a.out)\n"));
  fprintf (stream, _("\
  -R                      fold data section into text section\n"));
  fprintf (stream, _("\
  --reduce-memory-overheads \n\
                          prefer smaller memory use at the cost of longer\n\
                          assembly times\n"));
  fprintf (stream, _("\
  --statistics            print various measured statistics from execution\n"));
  fprintf (stream, _("\
  --strip-local-absolute  strip local absolute symbols\n"));
  fprintf (stream, _("\
  --traditional-format    Use same format as native assembler when possible\n"));
  fprintf (stream, _("\
  --version               print assembler version number and exit\n"));
  fprintf (stream, _("\
  -W  --no-warn           suppress warnings\n"));
  fprintf (stream, _("\
  --warn                  don't suppress warnings\n"));
  fprintf (stream, _("\
  --fatal-warnings        treat warnings as errors\n"));
  fprintf (stream, _("\
  --itbl INSTTBL          extend instruction set to include instructions\n\
                          matching the specifications defined in file INSTTBL\n"));
  fprintf (stream, _("\
  -w                      ignored\n"));
  fprintf (stream, _("\
  -X                      ignored\n"));
  fprintf (stream, _("\
  -Z                      generate object file even after errors\n"));
  fprintf (stream, _("\
  --listing-lhs-width     set the width in words of the output data column of\n\
                          the listing\n"));
  fprintf (stream, _("\
  --listing-lhs-width2    set the width in words of the continuation lines\n\
                          of the output data column; ignored if smaller than\n\
                          the width of the first line\n"));
  fprintf (stream, _("\
  --listing-rhs-width     set the max width in characters of the lines from\n\
                          the source file\n"));
  fprintf (stream, _("\
  --listing-cont-lines    set the maximum number of continuation lines used\n\
                          for the output data column of the listing\n"));
  fprintf (stream, _("\
  @FILE                   read options from FILE\n")); 

  md_show_usage (stream);

  fputc ('\n', stream);

  if (REPORT_BUGS_TO[0] && stream == stdout)
    fprintf (stream, _("Report bugs to %s\n"), REPORT_BUGS_TO);
}

/* Since it is easy to do here we interpret the special arg "-"
   to mean "use stdin" and we set that argv[] pointing to "".
   After we have munged argv[], the only things left are source file
   name(s) and ""(s) denoting stdin. These file names are used
   (perhaps more than once) later.

   check for new machine-dep cmdline options in
   md_parse_option definitions in config/tc-*.c.  */

static void
parse_args (int * pargc, char *** pargv)
{
  int old_argc;
  int new_argc;
  char ** old_argv;
  char ** new_argv;
  /* Starting the short option string with '-' is for programs that
     expect options and other ARGV-elements in any order and that care about
     the ordering of the two.  We describe each non-option ARGV-element
     as if it were the argument of an option with character code 1.  */
  char *shortopts;
  extern const char *md_shortopts;
  static const char std_shortopts[] =
  {
    '-', 'J',
#ifndef WORKING_DOT_WORD
    /* -K is not meaningful if .word is not being hacked.  */
    'K',
#endif
    'L', 'M', 'R', 'W', 'Z', 'a', ':', ':', 'D', 'f', 'g', ':',':', 'I', ':', 'o', ':',
#ifndef VMS
    /* -v takes an argument on VMS, so we don't make it a generic
       option.  */
    'v',
#endif
    'w', 'X',
    /* New option for extending instruction set (see also --itbl below).  */
    't', ':',
    '\0'
  };
  struct option *longopts;
  extern struct option md_longopts[];
  extern size_t md_longopts_size;
  /* Codes used for the long options with no short synonyms.  */
  enum option_values
    {
      OPTION_HELP = OPTION_STD_BASE,
      OPTION_NOCPP,
      OPTION_STATISTICS,
      OPTION_VERSION,
      OPTION_DUMPCONFIG,
      OPTION_VERBOSE,
      OPTION_EMULATION,
      OPTION_DEFSYM,
      OPTION_INSTTBL,
      OPTION_LISTING_LHS_WIDTH,
      OPTION_LISTING_LHS_WIDTH2,
      OPTION_LISTING_RHS_WIDTH,
      OPTION_LISTING_CONT_LINES,
      OPTION_DEPFILE,
      OPTION_GSTABS,
      OPTION_GSTABS_PLUS,
      OPTION_GDWARF2,
      OPTION_STRIP_LOCAL_ABSOLUTE,
      OPTION_TRADITIONAL_FORMAT,
      OPTION_WARN,
      OPTION_TARGET_HELP,
      OPTION_EXECSTACK,
      OPTION_NOEXECSTACK,
      OPTION_ALTERNATE,
      OPTION_AL,
      OPTION_HASH_TABLE_SIZE,
      OPTION_REDUCE_MEMORY_OVERHEADS,
      OPTION_WARN_FATAL
    /* When you add options here, check that they do
       not collide with OPTION_MD_BASE.  See as.h.  */
    };
  
  static const struct option std_longopts[] =
  {
    /* Note: commas are placed at the start of the line rather than
       the end of the preceeding line so that it is simpler to
       selectively add and remove lines from this list.  */
    {"alternate", no_argument, NULL, OPTION_ALTERNATE}
    /* The entry for "a" is here to prevent getopt_long_only() from
       considering that -a is an abbreviation for --alternate.  This is
       necessary because -a=<FILE> is a valid switch but getopt would
       normally reject it since --alternate does not take an argument.  */
    ,{"a", optional_argument, NULL, 'a'}
    /* Handle -al=<FILE>.  */
    ,{"al", optional_argument, NULL, OPTION_AL}
    ,{"defsym", required_argument, NULL, OPTION_DEFSYM}
    ,{"dump-config", no_argument, NULL, OPTION_DUMPCONFIG}
    ,{"emulation", required_argument, NULL, OPTION_EMULATION}
#if defined OBJ_ELF || defined OBJ_MAYBE_ELF
    ,{"execstack", no_argument, NULL, OPTION_EXECSTACK}
    ,{"noexecstack", no_argument, NULL, OPTION_NOEXECSTACK}
#endif
    ,{"fatal-warnings", no_argument, NULL, OPTION_WARN_FATAL}
    ,{"gdwarf-2", no_argument, NULL, OPTION_GDWARF2}
    /* GCC uses --gdwarf-2 but GAS uses to use --gdwarf2,
       so we keep it here for backwards compatibility.  */
    ,{"gdwarf2", no_argument, NULL, OPTION_GDWARF2}
    ,{"gen-debug", no_argument, NULL, 'g'}
    ,{"gstabs", no_argument, NULL, OPTION_GSTABS}
    ,{"gstabs+", no_argument, NULL, OPTION_GSTABS_PLUS}
    ,{"hash-size", required_argument, NULL, OPTION_HASH_TABLE_SIZE}
    ,{"help", no_argument, NULL, OPTION_HELP}
    /* New option for extending instruction set (see also -t above).
       The "-t file" or "--itbl file" option extends the basic set of
       valid instructions by reading "file", a text file containing a
       list of instruction formats.  The additional opcodes and their
       formats are added to the built-in set of instructions, and
       mnemonics for new registers may also be defined.  */
    ,{"itbl", required_argument, NULL, OPTION_INSTTBL}
    /* getopt allows abbreviations, so we do this to stop it from
       treating -k as an abbreviation for --keep-locals.  Some
       ports use -k to enable PIC assembly.  */
    ,{"keep-locals", no_argument, NULL, 'L'}
    ,{"keep-locals", no_argument, NULL, 'L'}
    ,{"listing-lhs-width", required_argument, NULL, OPTION_LISTING_LHS_WIDTH}
    ,{"listing-lhs-width2", required_argument, NULL, OPTION_LISTING_LHS_WIDTH2}
    ,{"listing-rhs-width", required_argument, NULL, OPTION_LISTING_RHS_WIDTH}
    ,{"listing-cont-lines", required_argument, NULL, OPTION_LISTING_CONT_LINES}
    ,{"MD", required_argument, NULL, OPTION_DEPFILE}
    ,{"mri", no_argument, NULL, 'M'}
    ,{"nocpp", no_argument, NULL, OPTION_NOCPP}
    ,{"no-warn", no_argument, NULL, 'W'}
    ,{"reduce-memory-overheads", no_argument, NULL, OPTION_REDUCE_MEMORY_OVERHEADS}
    ,{"statistics", no_argument, NULL, OPTION_STATISTICS}
    ,{"strip-local-absolute", no_argument, NULL, OPTION_STRIP_LOCAL_ABSOLUTE}
    ,{"version", no_argument, NULL, OPTION_VERSION}
    ,{"verbose", no_argument, NULL, OPTION_VERBOSE}
    ,{"target-help", no_argument, NULL, OPTION_TARGET_HELP}
    ,{"traditional-format", no_argument, NULL, OPTION_TRADITIONAL_FORMAT}
    ,{"warn", no_argument, NULL, OPTION_WARN}
  };

  /* Construct the option lists from the standard list and the target
     dependent list.  Include space for an extra NULL option and
     always NULL terminate.  */
  shortopts = concat (std_shortopts, md_shortopts, (char *) NULL);
  longopts = xmalloc (sizeof (std_longopts) + md_longopts_size + sizeof (struct option));
  memcpy (longopts, std_longopts, sizeof (std_longopts));
  memcpy (((char *) longopts) + sizeof (std_longopts), md_longopts, md_longopts_size);
  memset (((char *) longopts) + sizeof (std_longopts) + md_longopts_size,
	  0, sizeof (struct option));

  /* Make a local copy of the old argv.  */
  old_argc = *pargc;
  old_argv = *pargv;

  /* Initialize a new argv that contains no options.  */
  new_argv = xmalloc (sizeof (char *) * (old_argc + 1));
  new_argv[0] = old_argv[0];
  new_argc = 1;
  new_argv[new_argc] = NULL;

  while (1)
    {
      /* getopt_long_only is like getopt_long, but '-' as well as '--' can
	 indicate a long option.  */
      int longind;
      int optc = getopt_long_only (old_argc, old_argv, shortopts, longopts,
				   &longind);

      if (optc == -1)
	break;

      switch (optc)
	{
	default:
	  /* md_parse_option should return 1 if it recognizes optc,
	     0 if not.  */
	  if (md_parse_option (optc, optarg) != 0)
	    break;
	  /* `-v' isn't included in the general short_opts list, so check for
	     it explicitly here before deciding we've gotten a bad argument.  */
	  if (optc == 'v')
	    {
#ifdef VMS
	      /* Telling getopt to treat -v's value as optional can result
		 in it picking up a following filename argument here.  The
		 VMS code in md_parse_option can return 0 in that case,
		 but it has no way of pushing the filename argument back.  */
	      if (optarg && *optarg)
		new_argv[new_argc++] = optarg, new_argv[new_argc] = NULL;
	      else
#else
	      case 'v':
#endif
	      case OPTION_VERBOSE:
		print_version_id ();
		verbose = 1;
	      break;
	    }
	  else
	    as_bad (_("unrecognized option -%c%s"), optc, optarg ? optarg : "");
	  /* Fall through.  */

	case '?':
	  exit (EXIT_FAILURE);

	case 1:			/* File name.  */
	  if (!strcmp (optarg, "-"))
	    optarg = "";
	  new_argv[new_argc++] = optarg;
	  new_argv[new_argc] = NULL;
	  break;

	case OPTION_TARGET_HELP:
	  md_show_usage (stdout);
	  exit (EXIT_SUCCESS);

	case OPTION_HELP:
	  show_usage (stdout);
	  exit (EXIT_SUCCESS);

	case OPTION_NOCPP:
	  break;

	case OPTION_STATISTICS:
	  flag_print_statistics = 1;
	  break;

	case OPTION_STRIP_LOCAL_ABSOLUTE:
	  flag_strip_local_absolute = 1;
	  break;

	case OPTION_TRADITIONAL_FORMAT:
	  flag_traditional_format = 1;
	  break;

	case OPTION_VERSION:
	  /* This output is intended to follow the GNU standards document.  */
	  printf (_("GNU assembler %s\n"), BFD_VERSION_STRING);
	  printf (_("Copyright 2007 Free Software Foundation, Inc.\n"));
	  printf (_("\
This program is free software; you may redistribute it under the terms of\n\
the GNU General Public License.  This program has absolutely no warranty.\n"));
	  printf (_("This assembler was configured for a target of `%s'.\n"),
		  TARGET_ALIAS);
	  exit (EXIT_SUCCESS);

	case OPTION_EMULATION:
#ifdef USE_EMULATIONS
	  if (strcmp (optarg, this_emulation->name))
	    as_fatal (_("multiple emulation names specified"));
#else
	  as_fatal (_("emulations not handled in this configuration"));
#endif
	  break;

	case OPTION_DUMPCONFIG:
	  fprintf (stderr, _("alias = %s\n"), TARGET_ALIAS);
	  fprintf (stderr, _("canonical = %s\n"), TARGET_CANONICAL);
	  fprintf (stderr, _("cpu-type = %s\n"), TARGET_CPU);
#ifdef TARGET_OBJ_FORMAT
	  fprintf (stderr, _("format = %s\n"), TARGET_OBJ_FORMAT);
#endif
#ifdef TARGET_FORMAT
	  fprintf (stderr, _("bfd-target = %s\n"), TARGET_FORMAT);
#endif
	  exit (EXIT_SUCCESS);

	case OPTION_DEFSYM:
	  {
	    char *s;
	    valueT i;
	    struct defsym_list *n;

	    for (s = optarg; *s != '\0' && *s != '='; s++)
	      ;
	    if (*s == '\0')
	      as_fatal (_("bad defsym; format is --defsym name=value"));
	    *s++ = '\0';
	    i = bfd_scan_vma (s, (const char **) NULL, 0);
	    n = xmalloc (sizeof *n);
	    n->next = defsyms;
	    n->name = optarg;
	    n->value = i;
	    defsyms = n;
	  }
	  break;

	case OPTION_INSTTBL:
	case 't':
	  {
	    /* optarg is the name of the file containing the instruction
	       formats, opcodes, register names, etc.  */
	    struct itbl_file_list *n;

	    if (optarg == NULL)
	      {
		as_warn (_("no file name following -t option"));
		break;
	      }

	    n = xmalloc (sizeof * n);
	    n->next = itbl_files;
	    n->name = optarg;
	    itbl_files = n;

	    /* Parse the file and add the new instructions to our internal
	       table.  If multiple instruction tables are specified, the
	       information from this table gets appended onto the existing
	       internal table.  */
	    itbl_files->name = xstrdup (optarg);
	    if (itbl_parse (itbl_files->name) != 0)
	      as_fatal (_("failed to read instruction table %s\n"),
			itbl_files->name);
	  }
	  break;

	case OPTION_DEPFILE:
	  start_dependencies (optarg);
	  break;

	case 'g':
	  /* Some backends, eg Alpha and Mips, use the -g switch for their
	     own purposes.  So we check here for an explicit -g and allow
	     the backend to decide if it wants to process it.  */
	  if (   old_argv[optind - 1][1] == 'g'
	      && md_parse_option (optc, optarg))
	    continue;

	  if (md_debug_format_selector)
	    debug_type = md_debug_format_selector (& use_gnu_debug_info_extensions);
	  else if (IS_ELF)
	    debug_type = DEBUG_DWARF2;
	  else
	    debug_type = DEBUG_STABS;
	  break;

	case OPTION_GSTABS_PLUS:
	  use_gnu_debug_info_extensions = 1;
	  /* Fall through.  */
	case OPTION_GSTABS:
	  debug_type = DEBUG_STABS;
	  break;

	case OPTION_GDWARF2:
	  debug_type = DEBUG_DWARF2;
	  break;

	case 'J':
	  flag_signed_overflow_ok = 1;
	  break;

#ifndef WORKING_DOT_WORD
	case 'K':
	  flag_warn_displacement = 1;
	  break;
#endif
	case 'L':
	  flag_keep_locals = 1;
	  break;

	case OPTION_LISTING_LHS_WIDTH:
	  listing_lhs_width = atoi (optarg);
	  if (listing_lhs_width_second < listing_lhs_width)
	    listing_lhs_width_second = listing_lhs_width;
	  break;
	case OPTION_LISTING_LHS_WIDTH2:
	  {
	    int tmp = atoi (optarg);

	    if (tmp > listing_lhs_width)
	      listing_lhs_width_second = tmp;
	  }
	  break;
	case OPTION_LISTING_RHS_WIDTH:
	  listing_rhs_width = atoi (optarg);
	  break;
	case OPTION_LISTING_CONT_LINES:
	  listing_lhs_cont_lines = atoi (optarg);
	  break;

	case 'M':
	  flag_mri = 1;
#ifdef TC_M68K
	  flag_m68k_mri = 1;
#endif
	  break;

	case 'R':
	  flag_readonly_data_in_text = 1;
	  break;

	case 'W':
	  flag_no_warnings = 1;
	  break;

	case OPTION_WARN:
	  flag_no_warnings = 0;
	  flag_fatal_warnings = 0;
	  break;

	case OPTION_WARN_FATAL:
	  flag_no_warnings = 0;
	  flag_fatal_warnings = 1;
	  break;

#if defined OBJ_ELF || defined OBJ_MAYBE_ELF
	case OPTION_EXECSTACK:
	  flag_execstack = 1;
	  flag_noexecstack = 0;
	  break;

	case OPTION_NOEXECSTACK:
	  flag_noexecstack = 1;
	  flag_execstack = 0;
	  break;
#endif
	case 'Z':
	  flag_always_generate_output = 1;
	  break;

 	case OPTION_AL:
	  listing |= LISTING_LISTING;
	  if (optarg)
	    listing_filename = xstrdup (optarg);
	  break;

 	case OPTION_ALTERNATE:
 	  optarg = old_argv [optind - 1];
 	  while (* optarg == '-')
 	    optarg ++;

 	  if (strcmp (optarg, "alternate") == 0)
 	    {
 	      flag_macro_alternate = 1;
 	      break;
 	    }
 	  optarg ++;
 	  /* Fall through.  */

	case 'a':
	  if (optarg)
	    {
	      if (optarg != old_argv[optind] && optarg[-1] == '=')
		--optarg;

	      if (md_parse_option (optc, optarg) != 0)
		break;

	      while (*optarg)
		{
		  switch (*optarg)
		    {
		    case 'c':
		      listing |= LISTING_NOCOND;
		      break;
		    case 'd':
		      listing |= LISTING_NODEBUG;
		      break;
		    case 'h':
		      listing |= LISTING_HLL;
		      break;
		    case 'l':
		      listing |= LISTING_LISTING;
		      break;
		    case 'm':
		      listing |= LISTING_MACEXP;
		      break;
		    case 'n':
		      listing |= LISTING_NOFORM;
		      break;
		    case 's':
		      listing |= LISTING_SYMBOLS;
		      break;
		    case '=':
		      listing_filename = xstrdup (optarg + 1);
		      optarg += strlen (listing_filename);
		      break;
		    default:
		      as_fatal (_("invalid listing option `%c'"), *optarg);
		      break;
		    }
		  optarg++;
		}
	    }
	  if (!listing)
	    listing = LISTING_DEFAULT;
	  break;

	case 'D':
	  /* DEBUG is implemented: it debugs different
	     things from other people's assemblers.  */
	  flag_debug = 1;
	  break;

	case 'f':
	  flag_no_comments = 1;
	  break;

	case 'I':
	  {			/* Include file directory.  */
	    char *temp = xstrdup (optarg);

	    add_include_dir (temp);
	    break;
	  }

	case 'o':
	  out_file_name = xstrdup (optarg);
	  break;

	case 'w':
	  break;

	case 'X':
	  /* -X means treat warnings as errors.  */
	  break;

	case OPTION_REDUCE_MEMORY_OVERHEADS:
	  /* The only change we make at the moment is to reduce
	     the size of the hash tables that we use.  */
	  set_gas_hash_table_size (4051);
	  break;

	case OPTION_HASH_TABLE_SIZE:
	  {
	    unsigned long new_size;

            new_size = strtoul (optarg, NULL, 0);
            if (new_size)
              set_gas_hash_table_size (new_size);
            else
              as_fatal (_("--hash-size needs a numeric argument"));
	    break;
	  }
	}
    }

  free (shortopts);
  free (longopts);

  *pargc = new_argc;
  *pargv = new_argv;

#ifdef md_after_parse_args
  md_after_parse_args ();
#endif
}

static void
dump_statistics (void)
{
#ifdef HAVE_SBRK
  char *lim = (char *) sbrk (0);
#endif
  long run_time = get_run_time () - start_time;

  fprintf (stderr, _("%s: total time in assembly: %ld.%06ld\n"),
	   myname, run_time / 1000000, run_time % 1000000);
#ifdef HAVE_SBRK
  fprintf (stderr, _("%s: data size %ld\n"),
	   myname, (long) (lim - (char *) &environ));
#endif

  subsegs_print_statistics (stderr);
  write_print_statistics (stderr);
  symbol_print_statistics (stderr);
  read_print_statistics (stderr);

#ifdef tc_print_statistics
  tc_print_statistics (stderr);
#endif

#ifdef obj_print_statistics
  obj_print_statistics (stderr);
#endif
}

#ifndef OBJ_VMS
static void
close_output_file (void)
{
  output_file_close (out_file_name);
}
#endif

/* The interface between the macro code and gas expression handling.  */

static int
macro_expr (const char *emsg, int idx, sb *in, int *val)
{
  char *hold;
  expressionS ex;

  sb_terminate (in);

  hold = input_line_pointer;
  input_line_pointer = in->ptr + idx;
  expression_and_evaluate (&ex);
  idx = input_line_pointer - in->ptr;
  input_line_pointer = hold;

  if (ex.X_op != O_constant)
    as_bad ("%s", emsg);

  *val = (int) ex.X_add_number;

  return idx;
}

/* Here to attempt 1 pass over each input file.
   We scan argv[*] looking for filenames or exactly "" which is
   shorthand for stdin. Any argv that is NULL is not a file-name.
   We set need_pass_2 TRUE if, after this, we still have unresolved
   expressions of the form (unknown value)+-(unknown value).

   Note the un*x semantics: there is only 1 logical input file, but it
   may be a catenation of many 'physical' input files.  */

static void
perform_an_assembly_pass (int argc, char ** argv)
{
  int saw_a_file = 0;
  flagword applicable;

  need_pass_2 = 0;

  /* Create the standard sections, and those the assembler uses
     internally.  */
  text_section = subseg_new (TEXT_SECTION_NAME, 0);
  data_section = subseg_new (DATA_SECTION_NAME, 0);
  bss_section = subseg_new (BSS_SECTION_NAME, 0);
  /* @@ FIXME -- we're setting the RELOC flag so that sections are assumed
     to have relocs, otherwise we don't find out in time.  */
  applicable = bfd_applicable_section_flags (stdoutput);
  bfd_set_section_flags (stdoutput, text_section,
			 applicable & (SEC_ALLOC | SEC_LOAD | SEC_RELOC
				       | SEC_CODE | SEC_READONLY));
  bfd_set_section_flags (stdoutput, data_section,
			 applicable & (SEC_ALLOC | SEC_LOAD | SEC_RELOC
				       | SEC_DATA));
  bfd_set_section_flags (stdoutput, bss_section, applicable & SEC_ALLOC);
  seg_info (bss_section)->bss = 1;
  subseg_new (BFD_ABS_SECTION_NAME, 0);
  subseg_new (BFD_UND_SECTION_NAME, 0);
  reg_section = subseg_new ("*GAS `reg' section*", 0);
  expr_section = subseg_new ("*GAS `expr' section*", 0);

  subseg_set (text_section, 0);

  /* This may add symbol table entries, which requires having an open BFD,
     and sections already created.  */
  md_begin ();

#ifdef USING_CGEN
  gas_cgen_begin ();
#endif
#ifdef obj_begin
  obj_begin ();
#endif

  /* Skip argv[0].  */
  argv++;
  argc--;

  while (argc--)
    {
      if (*argv)
	{			/* Is it a file-name argument?  */
	  PROGRESS (1);
	  saw_a_file++;
	  /* argv->"" if stdin desired, else->filename.  */
	  read_a_source_file (*argv);
	}
      argv++;			/* Completed that argv.  */
    }
  if (!saw_a_file)
    read_a_source_file ("");
}

#ifdef OBJ_ELF
static void
create_obj_attrs_section (void)
{
  segT s;
  char *p;
  addressT addr;
  offsetT size;
  const char *name;

  size = bfd_elf_obj_attr_size (stdoutput);
  if (size)
    {
      name = get_elf_backend_data (stdoutput)->obj_attrs_section;
      if (!name)
	name = ".gnu.attributes";
      s = subseg_new (name, 0);
      elf_section_type (s)
	= get_elf_backend_data (stdoutput)->obj_attrs_section_type;
      bfd_set_section_flags (stdoutput, s, SEC_READONLY | SEC_DATA);
      addr = frag_now_fix ();
      p = frag_more (size);
      bfd_elf_set_obj_attr_contents (stdoutput, (bfd_byte *)p, size);
    }
}
#endif


int
main (int argc, char ** argv)
{
  int macro_strip_at;
  int keep_it;

  start_time = get_run_time ();

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  if (debug_memory)
    chunksize = 64;

#ifdef HOST_SPECIAL_INIT
  HOST_SPECIAL_INIT (argc, argv);
#endif

  myname = argv[0];
  xmalloc_set_program_name (myname);

  expandargv (&argc, &argv);

  START_PROGRESS (myname, 0);

#ifndef OBJ_DEFAULT_OUTPUT_FILE_NAME
#define OBJ_DEFAULT_OUTPUT_FILE_NAME "a.out"
#endif

  out_file_name = OBJ_DEFAULT_OUTPUT_FILE_NAME;

  hex_init ();
  bfd_init ();
  bfd_set_error_program_name (myname);

#ifdef USE_EMULATIONS
  select_emulation_mode (argc, argv);
#endif

  PROGRESS (1);
  /* Call parse_args before any of the init/begin functions
     so that switches like --hash-size can be honored.  */
  parse_args (&argc, &argv);
  symbol_begin ();
  frag_init ();
  subsegs_begin ();
  read_begin ();
  input_scrub_begin ();
  expr_begin ();

#ifndef OBJ_VMS /* Does its own file handling.  */
  /* It has to be called after dump_statistics ().  */
  xatexit (close_output_file);
#endif

  if (flag_print_statistics)
    xatexit (dump_statistics);

  macro_strip_at = 0;
#ifdef TC_I960
  macro_strip_at = flag_mri;
#endif

  macro_init (flag_macro_alternate, flag_mri, macro_strip_at, macro_expr);

  PROGRESS (1);

  output_file_create (out_file_name);
  assert (stdoutput != 0);

#ifdef tc_init_after_args
  tc_init_after_args ();
#endif

  itbl_init ();

  /* Now that we have fully initialized, and have created the output
     file, define any symbols requested by --defsym command line
     arguments.  */
  while (defsyms != NULL)
    {
      symbolS *sym;
      struct defsym_list *next;

      sym = symbol_new (defsyms->name, absolute_section, defsyms->value,
			&zero_address_frag);
      /* Make symbols defined on the command line volatile, so that they
	 can be redefined inside a source file.  This makes this assembler's
	 behaviour compatible with earlier versions, but it may not be
	 completely intuitive.  */
      S_SET_VOLATILE (sym);
      symbol_table_insert (sym);
      next = defsyms->next;
      free (defsyms);
      defsyms = next;
    }

  PROGRESS (1);

  /* Assemble it.  */
  perform_an_assembly_pass (argc, argv);

  cond_finish_check (-1);

#ifdef md_end
  md_end ();
#endif

#ifdef OBJ_ELF
  if (IS_ELF)
    create_obj_attrs_section ();
#endif

#if defined OBJ_ELF || defined OBJ_MAYBE_ELF
  if ((flag_execstack || flag_noexecstack)
      && OUTPUT_FLAVOR == bfd_target_elf_flavour)
    {
      segT gnustack;

      gnustack = subseg_new (".note.GNU-stack", 0);
      bfd_set_section_flags (stdoutput, gnustack,
			     SEC_READONLY | (flag_execstack ? SEC_CODE : 0));
                                                                             
    }
#endif

  /* If we've been collecting dwarf2 .debug_line info, either for
     assembly debugging or on behalf of the compiler, emit it now.  */
  dwarf2_finish ();

  /* If we constructed dwarf2 .eh_frame info, either via .cfi 
     directives from the user or by the backend, emit it now.  */
  cfi_finish ();

  if (seen_at_least_1_file ()
      && (flag_always_generate_output || had_errors () == 0))
    keep_it = 1;
  else
    keep_it = 0;

  /* This used to be done at the start of write_object_file in
     write.c, but that caused problems when doing listings when
     keep_it was zero.  This could probably be moved above md_end, but
     I didn't want to risk the change.  */
  subsegs_finish ();

  if (keep_it)
    write_object_file ();

#ifndef NO_LISTING
  listing_print (listing_filename);
#endif

  if (flag_fatal_warnings && had_warnings () > 0 && had_errors () == 0)
    as_bad (_("%d warnings, treating warnings as errors"), had_warnings ());

  if (had_errors () > 0 && ! flag_always_generate_output)
    keep_it = 0;

  if (!keep_it)
    unlink_if_ordinary (out_file_name);

  input_scrub_end ();

  END_PROGRESS (myname);

  /* Use xexit instead of return, because under VMS environments they
     may not place the same interpretation on the value given.  */
  if (had_errors () > 0)
    xexit (EXIT_FAILURE);

  /* Only generate dependency file if assembler was successful.  */
  print_dependencies ();

  xexit (EXIT_SUCCESS);
}
