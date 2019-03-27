/* Parse options for the GNU linker.
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007
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
#include "bfdver.h"
#include "libiberty.h"
#include <stdio.h>
#include <string.h>
#include "safe-ctype.h"
#include "getopt.h"
#include "bfdlink.h"
#include "ld.h"
#include "ldmain.h"
#include "ldmisc.h"
#include "ldexp.h"
#include "ldlang.h"
#include <ldgram.h>
#include "ldlex.h"
#include "ldfile.h"
#include "ldver.h"
#include "ldemul.h"
#include "demangle.h"

#ifndef PATH_SEPARATOR
#if defined (__MSDOS__) || (defined (_WIN32) && ! defined (__CYGWIN32__))
#define PATH_SEPARATOR ';'
#else
#define PATH_SEPARATOR ':'
#endif
#endif

/* Somewhere above, sys/stat.h got included . . . .  */
#if !defined(S_ISDIR) && defined(S_IFDIR)
#define	S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

static void set_default_dirlist (char *);
static void set_section_start (char *, char *);
static void set_segment_start (const char *, char *);
static void help (void);

/* Non-zero if we are processing a --defsym from the command line.  */
int parsing_defsym = 0;

/* Codes used for the long options with no short synonyms.  150 isn't
   special; it's just an arbitrary non-ASCII char value.  */
enum option_values
{
  OPTION_ASSERT = 150,
  OPTION_CALL_SHARED,
  OPTION_CREF,
  OPTION_DEFSYM,
  OPTION_DEMANGLE,
  OPTION_DYNAMIC_LINKER,
  OPTION_SYSROOT,
  OPTION_EB,
  OPTION_EL,
  OPTION_EMBEDDED_RELOCS,
  OPTION_EXPORT_DYNAMIC,
  OPTION_HELP,
  OPTION_IGNORE,
  OPTION_MAP,
  OPTION_NO_DEMANGLE,
  OPTION_NO_KEEP_MEMORY,
  OPTION_NO_WARN_MISMATCH,
  OPTION_NO_WARN_SEARCH_MISMATCH,
  OPTION_NO_WARN_FATAL,
  OPTION_NOINHIBIT_EXEC,
  OPTION_NON_SHARED,
  OPTION_NO_WHOLE_ARCHIVE,
  OPTION_OFORMAT,
  OPTION_RELAX,
  OPTION_RETAIN_SYMBOLS_FILE,
  OPTION_RPATH,
  OPTION_RPATH_LINK,
  OPTION_SHARED,
  OPTION_SONAME,
  OPTION_SORT_COMMON,
  OPTION_SORT_SECTION,
  OPTION_STATS,
  OPTION_SYMBOLIC,
  OPTION_SYMBOLIC_FUNCTIONS,
  OPTION_TASK_LINK,
  OPTION_TBSS,
  OPTION_TDATA,
  OPTION_TTEXT,
  OPTION_TRADITIONAL_FORMAT,
  OPTION_UR,
  OPTION_VERBOSE,
  OPTION_VERSION,
  OPTION_VERSION_SCRIPT,
  OPTION_VERSION_EXPORTS_SECTION,
  OPTION_DYNAMIC_LIST,
  OPTION_DYNAMIC_LIST_CPP_NEW,
  OPTION_DYNAMIC_LIST_CPP_TYPEINFO,
  OPTION_DYNAMIC_LIST_DATA,
  OPTION_WARN_COMMON,
  OPTION_WARN_CONSTRUCTORS,
  OPTION_WARN_FATAL,
  OPTION_WARN_MULTIPLE_GP,
  OPTION_WARN_ONCE,
  OPTION_WARN_SECTION_ALIGN,
  OPTION_SPLIT_BY_RELOC,
  OPTION_SPLIT_BY_FILE ,
  OPTION_WHOLE_ARCHIVE,
  OPTION_ADD_NEEDED,
  OPTION_NO_ADD_NEEDED,
  OPTION_AS_NEEDED,
  OPTION_NO_AS_NEEDED,
  OPTION_WRAP,
  OPTION_FORCE_EXE_SUFFIX,
  OPTION_GC_SECTIONS,
  OPTION_NO_GC_SECTIONS,
  OPTION_PRINT_GC_SECTIONS,
  OPTION_NO_PRINT_GC_SECTIONS,
  OPTION_HASH_SIZE,
  OPTION_CHECK_SECTIONS,
  OPTION_NO_CHECK_SECTIONS,
  OPTION_NO_UNDEFINED,
  OPTION_INIT,
  OPTION_FINI,
  OPTION_SECTION_START,
  OPTION_UNIQUE,
  OPTION_TARGET_HELP,
  OPTION_ALLOW_SHLIB_UNDEFINED,
  OPTION_NO_ALLOW_SHLIB_UNDEFINED,
  OPTION_ALLOW_MULTIPLE_DEFINITION,
  OPTION_NO_UNDEFINED_VERSION,
  OPTION_DEFAULT_SYMVER,
  OPTION_DEFAULT_IMPORTED_SYMVER,
  OPTION_DISCARD_NONE,
  OPTION_SPARE_DYNAMIC_TAGS,
  OPTION_NO_DEFINE_COMMON,
  OPTION_NOSTDLIB,
  OPTION_NO_OMAGIC,
  OPTION_STRIP_DISCARDED,
  OPTION_NO_STRIP_DISCARDED,
  OPTION_ACCEPT_UNKNOWN_INPUT_ARCH,
  OPTION_NO_ACCEPT_UNKNOWN_INPUT_ARCH,
  OPTION_PIE,
  OPTION_UNRESOLVED_SYMBOLS,
  OPTION_WARN_UNRESOLVED_SYMBOLS,
  OPTION_ERROR_UNRESOLVED_SYMBOLS,
  OPTION_WARN_SHARED_TEXTREL,
  OPTION_REDUCE_MEMORY_OVERHEADS,
  OPTION_DEFAULT_SCRIPT
};

/* The long options.  This structure is used for both the option
   parsing and the help text.  */

struct ld_option
{
  /* The long option information.  */
  struct option opt;
  /* The short option with the same meaning ('\0' if none).  */
  char shortopt;
  /* The name of the argument (NULL if none).  */
  const char *arg;
  /* The documentation string.  If this is NULL, this is a synonym for
     the previous option.  */
  const char *doc;
  enum {
    /* Use one dash before long option name.  */
    ONE_DASH,
    /* Use two dashes before long option name.  */
    TWO_DASHES,
    /* Only accept two dashes before the long option name.
       This is an overloading of the use of this enum, since originally it
       was only intended to tell the --help display function how to display
       the long option name.  This feature was added in order to resolve
       the confusion about the -omagic command line switch.  Is it setting
       the output file name to "magic" or is it setting the NMAGIC flag on
       the output ?  It has been decided that it is setting the output file
       name, and that if you want to set the NMAGIC flag you should use -N
       or --omagic.  */
    EXACTLY_TWO_DASHES,
    /* Don't mention this option in --help output.  */
    NO_HELP
  } control;
};

static const struct ld_option ld_options[] =
{
  { {NULL, required_argument, NULL, '\0'},
    'a', N_("KEYWORD"), N_("Shared library control for HP/UX compatibility"),
    ONE_DASH },
  { {"architecture", required_argument, NULL, 'A'},
    'A', N_("ARCH"), N_("Set architecture") , TWO_DASHES },
  { {"format", required_argument, NULL, 'b'},
    'b', N_("TARGET"), N_("Specify target for following input files"),
    TWO_DASHES },
  { {"mri-script", required_argument, NULL, 'c'},
    'c', N_("FILE"), N_("Read MRI format linker script"), TWO_DASHES },
  { {"dc", no_argument, NULL, 'd'},
    'd', NULL, N_("Force common symbols to be defined"), ONE_DASH },
  { {"dp", no_argument, NULL, 'd'},
    '\0', NULL, NULL, ONE_DASH },
  { {"entry", required_argument, NULL, 'e'},
    'e', N_("ADDRESS"), N_("Set start address"), TWO_DASHES },
  { {"export-dynamic", no_argument, NULL, OPTION_EXPORT_DYNAMIC},
    'E', NULL, N_("Export all dynamic symbols"), TWO_DASHES },
  { {"EB", no_argument, NULL, OPTION_EB},
    '\0', NULL, N_("Link big-endian objects"), ONE_DASH },
  { {"EL", no_argument, NULL, OPTION_EL},
    '\0', NULL, N_("Link little-endian objects"), ONE_DASH },
  { {"auxiliary", required_argument, NULL, 'f'},
    'f', N_("SHLIB"), N_("Auxiliary filter for shared object symbol table"),
    TWO_DASHES },
  { {"filter", required_argument, NULL, 'F'},
    'F', N_("SHLIB"), N_("Filter for shared object symbol table"),
    TWO_DASHES },
  { {NULL, no_argument, NULL, '\0'},
    'g', NULL, N_("Ignored"), ONE_DASH },
  { {"gpsize", required_argument, NULL, 'G'},
    'G', N_("SIZE"), N_("Small data size (if no size, same as --shared)"),
    TWO_DASHES },
  { {"soname", required_argument, NULL, OPTION_SONAME},
    'h', N_("FILENAME"), N_("Set internal name of shared library"), ONE_DASH },
  { {"dynamic-linker", required_argument, NULL, OPTION_DYNAMIC_LINKER},
    'I', N_("PROGRAM"), N_("Set PROGRAM as the dynamic linker to use"),
    TWO_DASHES },
  { {"library", required_argument, NULL, 'l'},
    'l', N_("LIBNAME"), N_("Search for library LIBNAME"), TWO_DASHES },
  { {"library-path", required_argument, NULL, 'L'},
    'L', N_("DIRECTORY"), N_("Add DIRECTORY to library search path"),
    TWO_DASHES },
  { {"sysroot=<DIRECTORY>", required_argument, NULL, OPTION_SYSROOT},
    '\0', NULL, N_("Override the default sysroot location"), TWO_DASHES },
  { {NULL, required_argument, NULL, '\0'},
    'm', N_("EMULATION"), N_("Set emulation"), ONE_DASH },
  { {"print-map", no_argument, NULL, 'M'},
    'M', NULL, N_("Print map file on standard output"), TWO_DASHES },
  { {"nmagic", no_argument, NULL, 'n'},
    'n', NULL, N_("Do not page align data"), TWO_DASHES },
  { {"omagic", no_argument, NULL, 'N'},
    'N', NULL, N_("Do not page align data, do not make text readonly"),
    EXACTLY_TWO_DASHES },
  { {"no-omagic", no_argument, NULL, OPTION_NO_OMAGIC},
    '\0', NULL, N_("Page align data, make text readonly"),
    EXACTLY_TWO_DASHES },
  { {"output", required_argument, NULL, 'o'},
    'o', N_("FILE"), N_("Set output file name"), EXACTLY_TWO_DASHES },
  { {NULL, required_argument, NULL, '\0'},
    'O', NULL, N_("Optimize output file"), ONE_DASH },
  { {"Qy", no_argument, NULL, OPTION_IGNORE},
    '\0', NULL, N_("Ignored for SVR4 compatibility"), ONE_DASH },
  { {"emit-relocs", no_argument, NULL, 'q'},
    'q', NULL, "Generate relocations in final output", TWO_DASHES },
  { {"relocatable", no_argument, NULL, 'r'},
    'r', NULL, N_("Generate relocatable output"), TWO_DASHES },
  { {NULL, no_argument, NULL, '\0'},
    'i', NULL, NULL, ONE_DASH },
  { {"just-symbols", required_argument, NULL, 'R'},
    'R', N_("FILE"), N_("Just link symbols (if directory, same as --rpath)"),
    TWO_DASHES },
  { {"strip-all", no_argument, NULL, 's'},
    's', NULL, N_("Strip all symbols"), TWO_DASHES },
  { {"strip-debug", no_argument, NULL, 'S'},
    'S', NULL, N_("Strip debugging symbols"), TWO_DASHES },
  { {"strip-discarded", no_argument, NULL, OPTION_STRIP_DISCARDED},
    '\0', NULL, N_("Strip symbols in discarded sections"), TWO_DASHES },
  { {"no-strip-discarded", no_argument, NULL, OPTION_NO_STRIP_DISCARDED},
    '\0', NULL, N_("Do not strip symbols in discarded sections"), TWO_DASHES },
  { {"trace", no_argument, NULL, 't'},
    't', NULL, N_("Trace file opens"), TWO_DASHES },
  { {"script", required_argument, NULL, 'T'},
    'T', N_("FILE"), N_("Read linker script"), TWO_DASHES },
  { {"default-script", required_argument, NULL, OPTION_DEFAULT_SCRIPT},
    '\0', N_("FILE"), N_("Read default linker script"), TWO_DASHES },
  { {"dT", required_argument, NULL, OPTION_DEFAULT_SCRIPT},
    '\0', NULL, NULL, ONE_DASH },
  { {"undefined", required_argument, NULL, 'u'},
    'u', N_("SYMBOL"), N_("Start with undefined reference to SYMBOL"),
    TWO_DASHES },
  { {"unique", optional_argument, NULL, OPTION_UNIQUE},
    '\0', N_("[=SECTION]"),
    N_("Don't merge input [SECTION | orphan] sections"), TWO_DASHES },
  { {"Ur", no_argument, NULL, OPTION_UR},
    '\0', NULL, N_("Build global constructor/destructor tables"), ONE_DASH },
  { {"version", no_argument, NULL, OPTION_VERSION},
    'v', NULL, N_("Print version information"), TWO_DASHES },
  { {NULL, no_argument, NULL, '\0'},
    'V', NULL, N_("Print version and emulation information"), ONE_DASH },
  { {"discard-all", no_argument, NULL, 'x'},
    'x', NULL, N_("Discard all local symbols"), TWO_DASHES },
  { {"discard-locals", no_argument, NULL, 'X'},
    'X', NULL, N_("Discard temporary local symbols (default)"), TWO_DASHES },
  { {"discard-none", no_argument, NULL, OPTION_DISCARD_NONE},
    '\0', NULL, N_("Don't discard any local symbols"), TWO_DASHES },
  { {"trace-symbol", required_argument, NULL, 'y'},
    'y', N_("SYMBOL"), N_("Trace mentions of SYMBOL"), TWO_DASHES },
  { {NULL, required_argument, NULL, '\0'},
    'Y', N_("PATH"), N_("Default search path for Solaris compatibility"),
    ONE_DASH },
  { {"start-group", no_argument, NULL, '('},
    '(', NULL, N_("Start a group"), TWO_DASHES },
  { {"end-group", no_argument, NULL, ')'},
    ')', NULL, N_("End a group"), TWO_DASHES },
  { {"accept-unknown-input-arch", no_argument, NULL,
     OPTION_ACCEPT_UNKNOWN_INPUT_ARCH},
    '\0', NULL,
    N_("Accept input files whose architecture cannot be determined"),
    TWO_DASHES },
  { {"no-accept-unknown-input-arch", no_argument, NULL,
     OPTION_NO_ACCEPT_UNKNOWN_INPUT_ARCH},
    '\0', NULL, N_("Reject input files whose architecture is unknown"),
    TWO_DASHES },
  { {"add-needed", no_argument, NULL, OPTION_ADD_NEEDED},
    '\0', NULL, N_("Set DT_NEEDED tags for DT_NEEDED entries in\n"
		   "\t\t\t\tfollowing dynamic libs"), TWO_DASHES },
  { {"no-add-needed", no_argument, NULL, OPTION_NO_ADD_NEEDED},
    '\0', NULL, N_("Do not set DT_NEEDED tags for DT_NEEDED entries\n"
		   "\t\t\t\tin following dynamic libs"), TWO_DASHES },
  { {"as-needed", no_argument, NULL, OPTION_AS_NEEDED},
    '\0', NULL, N_("Only set DT_NEEDED for following dynamic libs if used"),
    TWO_DASHES },
  { {"no-as-needed", no_argument, NULL, OPTION_NO_AS_NEEDED},
    '\0', NULL, N_("Always set DT_NEEDED for following dynamic libs"),
    TWO_DASHES },
  { {"assert", required_argument, NULL, OPTION_ASSERT},
    '\0', N_("KEYWORD"), N_("Ignored for SunOS compatibility"), ONE_DASH },
  { {"Bdynamic", no_argument, NULL, OPTION_CALL_SHARED},
    '\0', NULL, N_("Link against shared libraries"), ONE_DASH },
  { {"dy", no_argument, NULL, OPTION_CALL_SHARED},
    '\0', NULL, NULL, ONE_DASH },
  { {"call_shared", no_argument, NULL, OPTION_CALL_SHARED},
    '\0', NULL, NULL, ONE_DASH },
  { {"Bstatic", no_argument, NULL, OPTION_NON_SHARED},
    '\0', NULL, N_("Do not link against shared libraries"), ONE_DASH },
  { {"dn", no_argument, NULL, OPTION_NON_SHARED},
    '\0', NULL, NULL, ONE_DASH },
  { {"non_shared", no_argument, NULL, OPTION_NON_SHARED},
    '\0', NULL, NULL, ONE_DASH },
  { {"static", no_argument, NULL, OPTION_NON_SHARED},
    '\0', NULL, NULL, ONE_DASH },
  { {"Bsymbolic", no_argument, NULL, OPTION_SYMBOLIC},
    '\0', NULL, N_("Bind global references locally"), ONE_DASH },
  { {"Bsymbolic-functions", no_argument, NULL, OPTION_SYMBOLIC_FUNCTIONS},
    '\0', NULL, N_("Bind global function references locally"), ONE_DASH },
  { {"check-sections", no_argument, NULL, OPTION_CHECK_SECTIONS},
    '\0', NULL, N_("Check section addresses for overlaps (default)"),
    TWO_DASHES },
  { {"no-check-sections", no_argument, NULL, OPTION_NO_CHECK_SECTIONS},
    '\0', NULL, N_("Do not check section addresses for overlaps"),
    TWO_DASHES },
  { {"cref", no_argument, NULL, OPTION_CREF},
    '\0', NULL, N_("Output cross reference table"), TWO_DASHES },
  { {"defsym", required_argument, NULL, OPTION_DEFSYM},
    '\0', N_("SYMBOL=EXPRESSION"), N_("Define a symbol"), TWO_DASHES },
  { {"demangle", optional_argument, NULL, OPTION_DEMANGLE},
    '\0', N_("[=STYLE]"), N_("Demangle symbol names [using STYLE]"),
    TWO_DASHES },
  { {"embedded-relocs", no_argument, NULL, OPTION_EMBEDDED_RELOCS},
    '\0', NULL, N_("Generate embedded relocs"), TWO_DASHES},
  { {"fatal-warnings", no_argument, NULL, OPTION_WARN_FATAL},
    '\0', NULL, N_("Treat warnings as errors"),
    TWO_DASHES },
  { {"no-fatal-warnings", no_argument, NULL, OPTION_NO_WARN_FATAL},
    '\0', NULL, N_("Don't treat warnings as errors"),
    TWO_DASHES },
  { {"fini", required_argument, NULL, OPTION_FINI},
    '\0', N_("SYMBOL"), N_("Call SYMBOL at unload-time"), ONE_DASH },
  { {"force-exe-suffix", no_argument, NULL, OPTION_FORCE_EXE_SUFFIX},
    '\0', NULL, N_("Force generation of file with .exe suffix"), TWO_DASHES},
  { {"gc-sections", no_argument, NULL, OPTION_GC_SECTIONS},
    '\0', NULL, N_("Remove unused sections (on some targets)"),
    TWO_DASHES },
  { {"no-gc-sections", no_argument, NULL, OPTION_NO_GC_SECTIONS},
    '\0', NULL, N_("Don't remove unused sections (default)"),
    TWO_DASHES },
  { {"print-gc-sections", no_argument, NULL, OPTION_PRINT_GC_SECTIONS},
    '\0', NULL, N_("List removed unused sections on stderr"),
    TWO_DASHES },
  { {"no-print-gc-sections", no_argument, NULL, OPTION_NO_PRINT_GC_SECTIONS},
    '\0', NULL, N_("Do not list removed unused sections"),
    TWO_DASHES },
  { {"hash-size=<NUMBER>", required_argument, NULL, OPTION_HASH_SIZE},
    '\0', NULL, N_("Set default hash table size close to <NUMBER>"),
    TWO_DASHES },
  { {"help", no_argument, NULL, OPTION_HELP},
    '\0', NULL, N_("Print option help"), TWO_DASHES },
  { {"init", required_argument, NULL, OPTION_INIT},
    '\0', N_("SYMBOL"), N_("Call SYMBOL at load-time"), ONE_DASH },
  { {"Map", required_argument, NULL, OPTION_MAP},
    '\0', N_("FILE"), N_("Write a map file"), ONE_DASH },
  { {"no-define-common", no_argument, NULL, OPTION_NO_DEFINE_COMMON},
    '\0', NULL, N_("Do not define Common storage"), TWO_DASHES },
  { {"no-demangle", no_argument, NULL, OPTION_NO_DEMANGLE },
    '\0', NULL, N_("Do not demangle symbol names"), TWO_DASHES },
  { {"no-keep-memory", no_argument, NULL, OPTION_NO_KEEP_MEMORY},
    '\0', NULL, N_("Use less memory and more disk I/O"), TWO_DASHES },
  { {"no-undefined", no_argument, NULL, OPTION_NO_UNDEFINED},
    '\0', NULL, N_("Do not allow unresolved references in object files"),
    TWO_DASHES },
  { {"allow-shlib-undefined", no_argument, NULL, OPTION_ALLOW_SHLIB_UNDEFINED},
    '\0', NULL, N_("Allow unresolved references in shared libaries"),
    TWO_DASHES },
  { {"no-allow-shlib-undefined", no_argument, NULL,
     OPTION_NO_ALLOW_SHLIB_UNDEFINED},
    '\0', NULL, N_("Do not allow unresolved references in shared libs"),
    TWO_DASHES },
  { {"allow-multiple-definition", no_argument, NULL,
     OPTION_ALLOW_MULTIPLE_DEFINITION},
    '\0', NULL, N_("Allow multiple definitions"), TWO_DASHES },
  { {"no-undefined-version", no_argument, NULL, OPTION_NO_UNDEFINED_VERSION},
    '\0', NULL, N_("Disallow undefined version"), TWO_DASHES },
  { {"default-symver", no_argument, NULL, OPTION_DEFAULT_SYMVER},
    '\0', NULL, N_("Create default symbol version"), TWO_DASHES },
  { {"default-imported-symver", no_argument, NULL,
      OPTION_DEFAULT_IMPORTED_SYMVER},
    '\0', NULL, N_("Create default symbol version for imported symbols"),
    TWO_DASHES },
  { {"no-warn-mismatch", no_argument, NULL, OPTION_NO_WARN_MISMATCH},
    '\0', NULL, N_("Don't warn about mismatched input files"), TWO_DASHES},
  { {"no-warn-search-mismatch", no_argument, NULL,
     OPTION_NO_WARN_SEARCH_MISMATCH},
    '\0', NULL, N_("Don't warn on finding an incompatible library"),
    TWO_DASHES},
  { {"no-whole-archive", no_argument, NULL, OPTION_NO_WHOLE_ARCHIVE},
    '\0', NULL, N_("Turn off --whole-archive"), TWO_DASHES },
  { {"noinhibit-exec", no_argument, NULL, OPTION_NOINHIBIT_EXEC},
    '\0', NULL, N_("Create an output file even if errors occur"),
    TWO_DASHES },
  { {"noinhibit_exec", no_argument, NULL, OPTION_NOINHIBIT_EXEC},
    '\0', NULL, NULL, NO_HELP },
  { {"nostdlib", no_argument, NULL, OPTION_NOSTDLIB},
    '\0', NULL, N_("Only use library directories specified on\n"
		   "\t\t\t\tthe command line"), ONE_DASH },
  { {"oformat", required_argument, NULL, OPTION_OFORMAT},
    '\0', N_("TARGET"), N_("Specify target of output file"),
    EXACTLY_TWO_DASHES },
  { {"qmagic", no_argument, NULL, OPTION_IGNORE},
    '\0', NULL, N_("Ignored for Linux compatibility"), ONE_DASH },
  { {"reduce-memory-overheads", no_argument, NULL,
     OPTION_REDUCE_MEMORY_OVERHEADS},
    '\0', NULL, N_("Reduce memory overheads, possibly taking much longer"),
    TWO_DASHES },
  { {"relax", no_argument, NULL, OPTION_RELAX},
    '\0', NULL, N_("Relax branches on certain targets"), TWO_DASHES },
  { {"retain-symbols-file", required_argument, NULL,
     OPTION_RETAIN_SYMBOLS_FILE},
    '\0', N_("FILE"), N_("Keep only symbols listed in FILE"), TWO_DASHES },
  { {"rpath", required_argument, NULL, OPTION_RPATH},
    '\0', N_("PATH"), N_("Set runtime shared library search path"), ONE_DASH },
  { {"rpath-link", required_argument, NULL, OPTION_RPATH_LINK},
    '\0', N_("PATH"), N_("Set link time shared library search path"),
    ONE_DASH },
  { {"shared", no_argument, NULL, OPTION_SHARED},
    '\0', NULL, N_("Create a shared library"), ONE_DASH },
  { {"Bshareable", no_argument, NULL, OPTION_SHARED }, /* FreeBSD.  */
    '\0', NULL, NULL, ONE_DASH },
  { {"pie", no_argument, NULL, OPTION_PIE},
    '\0', NULL, N_("Create a position independent executable"), ONE_DASH },
  { {"pic-executable", no_argument, NULL, OPTION_PIE},
    '\0', NULL, NULL, TWO_DASHES },
  { {"sort-common", no_argument, NULL, OPTION_SORT_COMMON},
    '\0', NULL, N_("Sort common symbols by size"), TWO_DASHES },
  { {"sort_common", no_argument, NULL, OPTION_SORT_COMMON},
    '\0', NULL, NULL, NO_HELP },
  { {"sort-section", required_argument, NULL, OPTION_SORT_SECTION},
    '\0', N_("name|alignment"), 
    N_("Sort sections by name or maximum alignment"), TWO_DASHES },
  { {"spare-dynamic-tags", required_argument, NULL, OPTION_SPARE_DYNAMIC_TAGS},
    '\0', N_("COUNT"), N_("How many tags to reserve in .dynamic section"),
    TWO_DASHES },
  { {"split-by-file", optional_argument, NULL, OPTION_SPLIT_BY_FILE},
    '\0', N_("[=SIZE]"), N_("Split output sections every SIZE octets"),
    TWO_DASHES },
  { {"split-by-reloc", optional_argument, NULL, OPTION_SPLIT_BY_RELOC},
    '\0', N_("[=COUNT]"), N_("Split output sections every COUNT relocs"),
    TWO_DASHES },
  { {"stats", no_argument, NULL, OPTION_STATS},
    '\0', NULL, N_("Print memory usage statistics"), TWO_DASHES },
  { {"target-help", no_argument, NULL, OPTION_TARGET_HELP},
    '\0', NULL, N_("Display target specific options"), TWO_DASHES },
  { {"task-link", required_argument, NULL, OPTION_TASK_LINK},
    '\0', N_("SYMBOL"), N_("Do task level linking"), TWO_DASHES },
  { {"traditional-format", no_argument, NULL, OPTION_TRADITIONAL_FORMAT},
    '\0', NULL, N_("Use same format as native linker"), TWO_DASHES },
  { {"section-start", required_argument, NULL, OPTION_SECTION_START},
    '\0', N_("SECTION=ADDRESS"), N_("Set address of named section"),
    TWO_DASHES },
  { {"Tbss", required_argument, NULL, OPTION_TBSS},
    '\0', N_("ADDRESS"), N_("Set address of .bss section"), ONE_DASH },
  { {"Tdata", required_argument, NULL, OPTION_TDATA},
    '\0', N_("ADDRESS"), N_("Set address of .data section"), ONE_DASH },
  { {"Ttext", required_argument, NULL, OPTION_TTEXT},
    '\0', N_("ADDRESS"), N_("Set address of .text section"), ONE_DASH },
  { {"unresolved-symbols=<method>", required_argument, NULL,
     OPTION_UNRESOLVED_SYMBOLS},
    '\0', NULL, N_("How to handle unresolved symbols.  <method> is:\n"
		   "\t\t\t\tignore-all, report-all, ignore-in-object-files,\n"
		   "\t\t\t\tignore-in-shared-libs"), TWO_DASHES },
  { {"verbose", no_argument, NULL, OPTION_VERBOSE},
    '\0', NULL, N_("Output lots of information during link"), TWO_DASHES },
  { {"dll-verbose", no_argument, NULL, OPTION_VERBOSE}, /* Linux.  */
    '\0', NULL, NULL, NO_HELP },
  { {"version-script", required_argument, NULL, OPTION_VERSION_SCRIPT },
    '\0', N_("FILE"), N_("Read version information script"), TWO_DASHES },
  { {"version-exports-section", required_argument, NULL,
     OPTION_VERSION_EXPORTS_SECTION },
    '\0', N_("SYMBOL"), N_("Take export symbols list from .exports, using\n"
			   "\t\t\t\tSYMBOL as the version."), TWO_DASHES },
  { {"dynamic-list-data", no_argument, NULL, OPTION_DYNAMIC_LIST_DATA},
    '\0', NULL, N_("Add data symbols to dynamic list"), TWO_DASHES },
  { {"dynamic-list-cpp-new", no_argument, NULL, OPTION_DYNAMIC_LIST_CPP_NEW},
    '\0', NULL, N_("Use C++ operator new/delete dynamic list"), TWO_DASHES },
  { {"dynamic-list-cpp-typeinfo", no_argument, NULL, OPTION_DYNAMIC_LIST_CPP_TYPEINFO},
    '\0', NULL, N_("Use C++ typeinfo dynamic list"), TWO_DASHES },
  { {"dynamic-list", required_argument, NULL, OPTION_DYNAMIC_LIST},
    '\0', N_("FILE"), N_("Read dynamic list"), TWO_DASHES },
  { {"warn-common", no_argument, NULL, OPTION_WARN_COMMON},
    '\0', NULL, N_("Warn about duplicate common symbols"), TWO_DASHES },
  { {"warn-constructors", no_argument, NULL, OPTION_WARN_CONSTRUCTORS},
    '\0', NULL, N_("Warn if global constructors/destructors are seen"),
    TWO_DASHES },
  { {"warn-multiple-gp", no_argument, NULL, OPTION_WARN_MULTIPLE_GP},
    '\0', NULL, N_("Warn if the multiple GP values are used"), TWO_DASHES },
  { {"warn-once", no_argument, NULL, OPTION_WARN_ONCE},
    '\0', NULL, N_("Warn only once per undefined symbol"), TWO_DASHES },
  { {"warn-section-align", no_argument, NULL, OPTION_WARN_SECTION_ALIGN},
    '\0', NULL, N_("Warn if start of section changes due to alignment"),
    TWO_DASHES },
  { {"warn-shared-textrel", no_argument, NULL, OPTION_WARN_SHARED_TEXTREL},
    '\0', NULL, N_("Warn if shared object has DT_TEXTREL"),
    TWO_DASHES },
  { {"warn-unresolved-symbols", no_argument, NULL,
     OPTION_WARN_UNRESOLVED_SYMBOLS},
    '\0', NULL, N_("Report unresolved symbols as warnings"), TWO_DASHES },
  { {"error-unresolved-symbols", no_argument, NULL,
     OPTION_ERROR_UNRESOLVED_SYMBOLS},
    '\0', NULL, N_("Report unresolved symbols as errors"), TWO_DASHES },
  { {"whole-archive", no_argument, NULL, OPTION_WHOLE_ARCHIVE},
    '\0', NULL, N_("Include all objects from following archives"),
    TWO_DASHES },
  { {"wrap", required_argument, NULL, OPTION_WRAP},
    '\0', N_("SYMBOL"), N_("Use wrapper functions for SYMBOL"), TWO_DASHES },
};

#define OPTION_COUNT ARRAY_SIZE (ld_options)

void
parse_args (unsigned argc, char **argv)
{
  unsigned i;
  int is, il, irl;
  int ingroup = 0;
  char *default_dirlist = NULL;
  char *shortopts;
  struct option *longopts;
  struct option *really_longopts;
  int last_optind;
  enum report_method how_to_report_unresolved_symbols = RM_GENERATE_ERROR;
  int no_fatal_warnings = FALSE;

  shortopts = xmalloc (OPTION_COUNT * 3 + 2);
  longopts = xmalloc (sizeof (*longopts) * (OPTION_COUNT + 1));
  really_longopts = xmalloc (sizeof (*really_longopts) * (OPTION_COUNT + 1));

  /* Starting the short option string with '-' is for programs that
     expect options and other ARGV-elements in any order and that care about
     the ordering of the two.  We describe each non-option ARGV-element
     as if it were the argument of an option with character code 1.  */
  shortopts[0] = '-';
  is = 1;
  il = 0;
  irl = 0;
  for (i = 0; i < OPTION_COUNT; i++)
    {
      if (ld_options[i].shortopt != '\0')
	{
	  shortopts[is] = ld_options[i].shortopt;
	  ++is;
	  if (ld_options[i].opt.has_arg == required_argument
	      || ld_options[i].opt.has_arg == optional_argument)
	    {
	      shortopts[is] = ':';
	      ++is;
	      if (ld_options[i].opt.has_arg == optional_argument)
		{
		  shortopts[is] = ':';
		  ++is;
		}
	    }
	}
      if (ld_options[i].opt.name != NULL)
	{
	  if (ld_options[i].control == EXACTLY_TWO_DASHES)
	    {
	      really_longopts[irl] = ld_options[i].opt;
	      ++irl;
	    }
	  else
	    {
	      longopts[il] = ld_options[i].opt;
	      ++il;
	    }
	}
    }
  shortopts[is] = '\0';
  longopts[il].name = NULL;
  really_longopts[irl].name = NULL;

  ldemul_add_options (is, &shortopts, il, &longopts, irl, &really_longopts);

  /* The -G option is ambiguous on different platforms.  Sometimes it
     specifies the largest data size to put into the small data
     section.  Sometimes it is equivalent to --shared.  Unfortunately,
     the first form takes an argument, while the second does not.

     We need to permit the --shared form because on some platforms,
     such as Solaris, gcc -shared will pass -G to the linker.

     To permit either usage, we look through the argument list.  If we
     find -G not followed by a number, we change it into --shared.
     This will work for most normal cases.  */
  for (i = 1; i < argc; i++)
    if (strcmp (argv[i], "-G") == 0
	&& (i + 1 >= argc
	    || ! ISDIGIT (argv[i + 1][0])))
      argv[i] = (char *) "--shared";

  /* Because we permit long options to start with a single dash, and
     we have a --library option, and the -l option is conventionally
     used with an immediately following argument, we can have bad
     results if somebody tries to use -l with a library whose name
     happens to start with "ibrary", as in -li.  We avoid problems by
     simply turning -l into --library.  This means that users will
     have to use two dashes in order to use --library, which is OK
     since that's how it is documented.

     FIXME: It's possible that this problem can arise for other short
     options as well, although the user does always have the recourse
     of adding a space between the option and the argument.  */
  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-'
	  && argv[i][1] == 'l'
	  && argv[i][2] != '\0')
	{
	  char *n;

	  n = xmalloc (strlen (argv[i]) + 20);
	  sprintf (n, "--library=%s", argv[i] + 2);
	  argv[i] = n;
	}
    }

  last_optind = -1;
  while (1)
    {
      int longind;
      int optc;

      /* Using last_optind lets us avoid calling ldemul_parse_args
	 multiple times on a single option, which would lead to
	 confusion in the internal static variables maintained by
	 getopt.  This could otherwise happen for an argument like
	 -nx, in which the -n is parsed as a single option, and we
	 loop around to pick up the -x.  */
      if (optind != last_optind)
	if (ldemul_parse_args (argc, argv))
	  continue;

      /* getopt_long_only is like getopt_long, but '-' as well as '--'
	 can indicate a long option.  */
      opterr = 0;
      last_optind = optind;
      optc = getopt_long_only (argc, argv, shortopts, longopts, &longind);
      if (optc == '?')
	{
	  optind = last_optind;
	  optc = getopt_long (argc, argv, "-", really_longopts, &longind);
	}

      if (ldemul_handle_option (optc))
	continue;

      if (optc == -1)
	break;

      switch (optc)
	{
	case '?':
	  einfo (_("%P: unrecognized option '%s'\n"), argv[last_optind]);
	  /* Fall through.  */

	default:
	  einfo (_("%P%F: use the --help option for usage information\n"));

	case 1:			/* File name.  */
	  lang_add_input_file (optarg, lang_input_file_is_file_enum, NULL);
	  break;

	case OPTION_IGNORE:
	  break;
	case 'a':
	  /* For HP/UX compatibility.  Actually -a shared should mean
	     ``use only shared libraries'' but, then, we don't
	     currently support shared libraries on HP/UX anyhow.  */
	  if (strcmp (optarg, "archive") == 0)
	    config.dynamic_link = FALSE;
	  else if (strcmp (optarg, "shared") == 0
		   || strcmp (optarg, "default") == 0)
	    config.dynamic_link = TRUE;
	  else
	    einfo (_("%P%F: unrecognized -a option `%s'\n"), optarg);
	  break;
	case OPTION_ASSERT:
	  /* FIXME: We just ignore these, but we should handle them.  */
	  if (strcmp (optarg, "definitions") == 0)
	    ;
	  else if (strcmp (optarg, "nodefinitions") == 0)
	    ;
	  else if (strcmp (optarg, "nosymbolic") == 0)
	    ;
	  else if (strcmp (optarg, "pure-text") == 0)
	    ;
	  else
	    einfo (_("%P%F: unrecognized -assert option `%s'\n"), optarg);
	  break;
	case 'A':
	  ldfile_add_arch (optarg);
	  break;
	case 'b':
	  lang_add_target (optarg);
	  break;
	case 'c':
	  ldfile_open_command_file (optarg);
	  parser_input = input_mri_script;
	  yyparse ();
	  break;
	case OPTION_CALL_SHARED:
	  config.dynamic_link = TRUE;
	  break;
	case OPTION_NON_SHARED:
	  config.dynamic_link = FALSE;
	  break;
	case OPTION_CREF:
	  command_line.cref = TRUE;
	  link_info.notice_all = TRUE;
	  break;
	case 'd':
	  command_line.force_common_definition = TRUE;
	  break;
	case OPTION_DEFSYM:
	  lex_string = optarg;
	  lex_redirect (optarg);
	  parser_input = input_defsym;
	  parsing_defsym = 1;
	  yyparse ();
	  parsing_defsym = 0;
	  lex_string = NULL;
	  break;
	case OPTION_DEMANGLE:
	  demangling = TRUE;
	  if (optarg != NULL)
	    {
	      enum demangling_styles style;

	      style = cplus_demangle_name_to_style (optarg);
	      if (style == unknown_demangling)
		einfo (_("%F%P: unknown demangling style `%s'"),
		       optarg);

	      cplus_demangle_set_style (style);
	    }
	  break;
	case 'I':		/* Used on Solaris.  */
	case OPTION_DYNAMIC_LINKER:
	  command_line.interpreter = optarg;
	  break;
	case OPTION_SYSROOT:
	  /* Already handled in ldmain.c.  */
	  break;
	case OPTION_EB:
	  command_line.endian = ENDIAN_BIG;
	  break;
	case OPTION_EL:
	  command_line.endian = ENDIAN_LITTLE;
	  break;
	case OPTION_EMBEDDED_RELOCS:
	  command_line.embedded_relocs = TRUE;
	  break;
	case OPTION_EXPORT_DYNAMIC:
	case 'E': /* HP/UX compatibility.  */
	  link_info.export_dynamic = TRUE;
	  break;
	case 'e':
	  lang_add_entry (optarg, TRUE);
	  break;
	case 'f':
	  if (command_line.auxiliary_filters == NULL)
	    {
	      command_line.auxiliary_filters = xmalloc (2 * sizeof (char *));
	      command_line.auxiliary_filters[0] = optarg;
	      command_line.auxiliary_filters[1] = NULL;
	    }
	  else
	    {
	      int c;
	      char **p;

	      c = 0;
	      for (p = command_line.auxiliary_filters; *p != NULL; p++)
		++c;
	      command_line.auxiliary_filters
		= xrealloc (command_line.auxiliary_filters,
			    (c + 2) * sizeof (char *));
	      command_line.auxiliary_filters[c] = optarg;
	      command_line.auxiliary_filters[c + 1] = NULL;
	    }
	  break;
	case 'F':
	  command_line.filter_shlib = optarg;
	  break;
	case OPTION_FORCE_EXE_SUFFIX:
	  command_line.force_exe_suffix = TRUE;
	  break;
	case 'G':
	  {
	    char *end;
	    g_switch_value = strtoul (optarg, &end, 0);
	    if (*end)
	      einfo (_("%P%F: invalid number `%s'\n"), optarg);
	  }
	  break;
	case 'g':
	  /* Ignore.  */
	  break;
	case OPTION_GC_SECTIONS:
	  link_info.gc_sections = TRUE;
	  break;
	case OPTION_PRINT_GC_SECTIONS:
	  link_info.print_gc_sections = TRUE;
	  break;
	case OPTION_HELP:
	  help ();
	  xexit (0);
	  break;
	case 'L':
	  ldfile_add_library_path (optarg, TRUE);
	  break;
	case 'l':
	  lang_add_input_file (optarg, lang_input_file_is_l_enum, NULL);
	  break;
	case 'M':
	  config.map_filename = "-";
	  break;
	case 'm':
	  /* Ignore.  Was handled in a pre-parse.   */
	  break;
	case OPTION_MAP:
	  config.map_filename = optarg;
	  break;
	case 'N':
	  config.text_read_only = FALSE;
	  config.magic_demand_paged = FALSE;
	  config.dynamic_link = FALSE;
	  break;
	case OPTION_NO_OMAGIC:
	  config.text_read_only = TRUE;
	  config.magic_demand_paged = TRUE;
	  /* NB/ Does not set dynamic_link to TRUE.
	     Use --call-shared or -Bdynamic for this.  */
	  break;
	case 'n':
	  config.magic_demand_paged = FALSE;
	  config.dynamic_link = FALSE;
	  break;
	case OPTION_NO_DEFINE_COMMON:
	  command_line.inhibit_common_definition = TRUE;
	  break;
	case OPTION_NO_DEMANGLE:
	  demangling = FALSE;
	  break;
	case OPTION_NO_GC_SECTIONS:
	  link_info.gc_sections = FALSE;
	  break;
	case OPTION_NO_PRINT_GC_SECTIONS:
	  link_info.print_gc_sections = FALSE;
	  break;
	case OPTION_NO_KEEP_MEMORY:
	  link_info.keep_memory = FALSE;
	  break;
	case OPTION_NO_UNDEFINED:
	  link_info.unresolved_syms_in_objects
	    = how_to_report_unresolved_symbols;
	  break;
	case OPTION_ALLOW_SHLIB_UNDEFINED:
	  link_info.unresolved_syms_in_shared_libs = RM_IGNORE;
	  break;
	case OPTION_NO_ALLOW_SHLIB_UNDEFINED:
	  link_info.unresolved_syms_in_shared_libs
	    = how_to_report_unresolved_symbols;
	  break;
	case OPTION_UNRESOLVED_SYMBOLS:
	  if (strcmp (optarg, "ignore-all") == 0)
	    {
	      link_info.unresolved_syms_in_objects = RM_IGNORE;
	      link_info.unresolved_syms_in_shared_libs = RM_IGNORE;
	    }
	  else if (strcmp (optarg, "report-all") == 0)
	    {
	      link_info.unresolved_syms_in_objects
		= how_to_report_unresolved_symbols;
	      link_info.unresolved_syms_in_shared_libs
		= how_to_report_unresolved_symbols;
	    }
	  else if (strcmp (optarg, "ignore-in-object-files") == 0)
	    {
	      link_info.unresolved_syms_in_objects = RM_IGNORE;
	      link_info.unresolved_syms_in_shared_libs
		= how_to_report_unresolved_symbols;
	    }
      	  else if (strcmp (optarg, "ignore-in-shared-libs") == 0)
	    {
	      link_info.unresolved_syms_in_objects
		= how_to_report_unresolved_symbols;
	      link_info.unresolved_syms_in_shared_libs = RM_IGNORE;
	    }
	  else
	    einfo (_("%P%F: bad --unresolved-symbols option: %s\n"), optarg);
	  break;
	case OPTION_WARN_UNRESOLVED_SYMBOLS:
	  how_to_report_unresolved_symbols = RM_GENERATE_WARNING;
	  if (link_info.unresolved_syms_in_objects == RM_GENERATE_ERROR)
	    link_info.unresolved_syms_in_objects = RM_GENERATE_WARNING;
	  if (link_info.unresolved_syms_in_shared_libs == RM_GENERATE_ERROR)
	    link_info.unresolved_syms_in_shared_libs = RM_GENERATE_WARNING;
	  break;

	case OPTION_ERROR_UNRESOLVED_SYMBOLS:
	  how_to_report_unresolved_symbols = RM_GENERATE_ERROR;
	  if (link_info.unresolved_syms_in_objects == RM_GENERATE_WARNING)
	    link_info.unresolved_syms_in_objects = RM_GENERATE_ERROR;
	  if (link_info.unresolved_syms_in_shared_libs == RM_GENERATE_WARNING)
	    link_info.unresolved_syms_in_shared_libs = RM_GENERATE_ERROR;
	  break;
	case OPTION_ALLOW_MULTIPLE_DEFINITION:
	  link_info.allow_multiple_definition = TRUE;
	  break;
	case OPTION_NO_UNDEFINED_VERSION:
	  link_info.allow_undefined_version = FALSE;
	  break;
	case OPTION_DEFAULT_SYMVER:
	  link_info.create_default_symver = TRUE;
	  break;
	case OPTION_DEFAULT_IMPORTED_SYMVER:
	  link_info.default_imported_symver = TRUE;
	  break;
	case OPTION_NO_WARN_MISMATCH:
	  command_line.warn_mismatch = FALSE;
	  break;
	case OPTION_NO_WARN_SEARCH_MISMATCH:
	  command_line.warn_search_mismatch = FALSE;
	  break;
	case OPTION_NOINHIBIT_EXEC:
	  force_make_executable = TRUE;
	  break;
	case OPTION_NOSTDLIB:
	  config.only_cmd_line_lib_dirs = TRUE;
	  break;
	case OPTION_NO_WHOLE_ARCHIVE:
	  whole_archive = FALSE;
	  break;
	case 'O':
	  /* FIXME "-O<non-digits> <value>" used to set the address of
	     section <non-digits>.  Was this for compatibility with
	     something, or can we create a new option to do that
	     (with a syntax similar to -defsym)?
	     getopt can't handle two args to an option without kludges.  */

	  /* Enable optimizations of output files.  */
	  link_info.optimize = strtoul (optarg, NULL, 0) ? TRUE : FALSE;
	  break;
	case 'o':
	  lang_add_output (optarg, 0);
	  break;
	case OPTION_OFORMAT:
	  lang_add_output_format (optarg, NULL, NULL, 0);
	  break;
	case 'q':
	  link_info.emitrelocations = TRUE;
	  break;
	case 'i':
	case 'r':
	  if (optind == last_optind)
	    /* This can happen if the user put "-rpath,a" on the command
	       line.  (Or something similar.  The comma is important).
	       Getopt becomes confused and thinks that this is a -r option
	       but it cannot parse the text after the -r so it refuses to
	       increment the optind counter.  Detect this case and issue
	       an error message here.  We cannot just make this a warning,
	       increment optind, and continue because getopt is too confused
	       and will seg-fault the next time around.  */
	    einfo(_("%P%F: bad -rpath option\n"));

	  link_info.relocatable = TRUE;
	  config.build_constructors = FALSE;
	  config.magic_demand_paged = FALSE;
	  config.text_read_only = FALSE;
	  config.dynamic_link = FALSE;
	  break;
	case 'R':
	  /* The GNU linker traditionally uses -R to mean to include
	     only the symbols from a file.  The Solaris linker uses -R
	     to set the path used by the runtime linker to find
	     libraries.  This is the GNU linker -rpath argument.  We
	     try to support both simultaneously by checking the file
	     named.  If it is a directory, rather than a regular file,
	     we assume -rpath was meant.  */
	  {
	    struct stat s;

	    if (stat (optarg, &s) >= 0
		&& ! S_ISDIR (s.st_mode))
	      {
		lang_add_input_file (optarg,
				     lang_input_file_is_symbols_only_enum,
				     NULL);
		break;
	      }
	  }
	  /* Fall through.  */
	case OPTION_RPATH:
	  if (command_line.rpath == NULL)
	    command_line.rpath = xstrdup (optarg);
	  else
	    {
	      size_t rpath_len = strlen (command_line.rpath);
	      size_t optarg_len = strlen (optarg);
	      char *buf;
	      char *cp = command_line.rpath;

	      /* First see whether OPTARG is already in the path.  */
	      do
		{
		  if (strncmp (optarg, cp, optarg_len) == 0
		      && (cp[optarg_len] == 0
			  || cp[optarg_len] == config.rpath_separator))
		    /* We found it.  */
		    break;

		  /* Not yet found.  */
		  cp = strchr (cp, config.rpath_separator);
		  if (cp != NULL)
		    ++cp;
		}
	      while (cp != NULL);

	      if (cp == NULL)
		{
		  buf = xmalloc (rpath_len + optarg_len + 2);
		  sprintf (buf, "%s%c%s", command_line.rpath,
			   config.rpath_separator, optarg);
		  free (command_line.rpath);
		  command_line.rpath = buf;
		}
	    }
	  break;
	case OPTION_RPATH_LINK:
	  if (command_line.rpath_link == NULL)
	    command_line.rpath_link = xstrdup (optarg);
	  else
	    {
	      char *buf;

	      buf = xmalloc (strlen (command_line.rpath_link)
			     + strlen (optarg)
			     + 2);
	      sprintf (buf, "%s%c%s", command_line.rpath_link,
		       config.rpath_separator, optarg);
	      free (command_line.rpath_link);
	      command_line.rpath_link = buf;
	    }
	  break;
	case OPTION_RELAX:
	  command_line.relax = TRUE;
	  break;
	case OPTION_RETAIN_SYMBOLS_FILE:
	  add_keepsyms_file (optarg);
	  break;
	case 'S':
	  link_info.strip = strip_debugger;
	  break;
	case 's':
	  link_info.strip = strip_all;
	  break;
	case OPTION_STRIP_DISCARDED:
	  link_info.strip_discarded = TRUE;
	  break;
	case OPTION_NO_STRIP_DISCARDED:
	  link_info.strip_discarded = FALSE;
	  break;
	case OPTION_SHARED:
	  if (config.has_shared)
	    {
	      link_info.shared = TRUE;
	      /* When creating a shared library, the default
		 behaviour is to ignore any unresolved references.  */
	      if (link_info.unresolved_syms_in_objects == RM_NOT_YET_SET)
		link_info.unresolved_syms_in_objects = RM_IGNORE;
	      if (link_info.unresolved_syms_in_shared_libs == RM_NOT_YET_SET)
		link_info.unresolved_syms_in_shared_libs = RM_IGNORE;
	    }
	  else
	    einfo (_("%P%F: -shared not supported\n"));
	  break;
	case OPTION_PIE:
	  if (config.has_shared)
	    {
	      link_info.shared = TRUE;
	      link_info.pie = TRUE;
	    }
	  else
	    einfo (_("%P%F: -pie not supported\n"));
	  break;
	case 'h':		/* Used on Solaris.  */
	case OPTION_SONAME:
	  command_line.soname = optarg;
	  break;
	case OPTION_SORT_COMMON:
	  config.sort_common = TRUE;
	  break;
	case OPTION_SORT_SECTION:
	  if (strcmp (optarg, N_("name")) == 0)
	    sort_section = by_name;
	  else if (strcmp (optarg, N_("alignment")) == 0)
	    sort_section = by_alignment;
	  else
	    einfo (_("%P%F: invalid section sorting option: %s\n"),
		   optarg);
	  break;
	case OPTION_STATS:
	  config.stats = TRUE;
	  break;
	case OPTION_SYMBOLIC:
	  command_line.symbolic = symbolic;
	  break;
	case OPTION_SYMBOLIC_FUNCTIONS:
	  command_line.symbolic = symbolic_functions;
	  break;
	case 't':
	  trace_files = TRUE;
	  break;
	case 'T':
	  ldfile_open_command_file (optarg);
	  parser_input = input_script;
	  yyparse ();
	  break;
	case OPTION_DEFAULT_SCRIPT:
	  command_line.default_script = optarg;
	  break;
	case OPTION_SECTION_START:
	  {
	    char *optarg2;
	    char *sec_name;
	    int len;

	    /* Check for <something>=<somthing>...  */
	    optarg2 = strchr (optarg, '=');
	    if (optarg2 == NULL)
	      einfo (_("%P%F: invalid argument to option"
		       " \"--section-start\"\n"));

	    optarg2++;

	    /* So far so good.  Are all the args present?  */
	    if ((*optarg == '\0') || (*optarg2 == '\0'))
	      einfo (_("%P%F: missing argument(s) to option"
		       " \"--section-start\"\n"));

	    /* We must copy the section name as set_section_start
	       doesn't do it for us.  */
	    len = optarg2 - optarg;
	    sec_name = xmalloc (len);
	    memcpy (sec_name, optarg, len - 1);
	    sec_name[len - 1] = 0;

	    /* Then set it...  */
	    set_section_start (sec_name, optarg2);
	  }
	  break;
	case OPTION_TARGET_HELP:
	  /* Mention any target specific options.  */
	  ldemul_list_emulation_options (stdout);
	  exit (0);
	case OPTION_TBSS:
	  set_segment_start (".bss", optarg);
	  break;
	case OPTION_TDATA:
	  set_segment_start (".data", optarg);
	  break;
	case OPTION_TTEXT:
	  set_segment_start (".text", optarg);
	  break;
	case OPTION_TRADITIONAL_FORMAT:
	  link_info.traditional_format = TRUE;
	  break;
	case OPTION_TASK_LINK:
	  link_info.task_link = TRUE;
	  /* Fall through - do an implied -r option.  */
	case OPTION_UR:
	  link_info.relocatable = TRUE;
	  config.build_constructors = TRUE;
	  config.magic_demand_paged = FALSE;
	  config.text_read_only = FALSE;
	  config.dynamic_link = FALSE;
	  break;
	case 'u':
	  ldlang_add_undef (optarg);
	  break;
	case OPTION_UNIQUE:
	  if (optarg != NULL)
	    lang_add_unique (optarg);
	  else
	    config.unique_orphan_sections = TRUE;
	  break;
	case OPTION_VERBOSE:
	  ldversion (1);
	  version_printed = TRUE;
	  trace_file_tries = TRUE;
	  overflow_cutoff_limit = -2;
	  break;
	case 'v':
	  ldversion (0);
	  version_printed = TRUE;
	  break;
	case 'V':
	  ldversion (1);
	  version_printed = TRUE;
	  break;
	case OPTION_VERSION:
	  ldversion (2);
	  xexit (0);
	  break;
	case OPTION_VERSION_SCRIPT:
	  /* This option indicates a small script that only specifies
	     version information.  Read it, but don't assume that
	     we've seen a linker script.  */
	  {
	    FILE *hold_script_handle;

	    hold_script_handle = saved_script_handle;
	    ldfile_open_command_file (optarg);
	    saved_script_handle = hold_script_handle;
	    parser_input = input_version_script;
	    yyparse ();
	  }
	  break;
	case OPTION_VERSION_EXPORTS_SECTION:
	  /* This option records a version symbol to be applied to the
	     symbols listed for export to be found in the object files
	     .exports sections.  */
	  command_line.version_exports_section = optarg;
	  break;
	case OPTION_DYNAMIC_LIST_DATA:
	  command_line.dynamic_list = dynamic_list_data;
	  if (command_line.symbolic == symbolic)
	    command_line.symbolic = symbolic_unset;
	  break;
	case OPTION_DYNAMIC_LIST_CPP_TYPEINFO:
	  lang_append_dynamic_list_cpp_typeinfo ();
	  if (command_line.dynamic_list != dynamic_list_data)
	    command_line.dynamic_list = dynamic_list;
	  if (command_line.symbolic == symbolic)
	    command_line.symbolic = symbolic_unset;
	  break;
	case OPTION_DYNAMIC_LIST_CPP_NEW:
	  lang_append_dynamic_list_cpp_new ();
	  if (command_line.dynamic_list != dynamic_list_data)
	    command_line.dynamic_list = dynamic_list;
	  if (command_line.symbolic == symbolic)
	    command_line.symbolic = symbolic_unset;
	  break;
	case OPTION_DYNAMIC_LIST:
	  /* This option indicates a small script that only specifies
	     a dynamic list.  Read it, but don't assume that we've
	     seen a linker script.  */
	  {
	    FILE *hold_script_handle;

	    hold_script_handle = saved_script_handle;
	    ldfile_open_command_file (optarg);
	    saved_script_handle = hold_script_handle;
	    parser_input = input_dynamic_list;
	    yyparse ();
	  }
	  if (command_line.dynamic_list != dynamic_list_data)
	    command_line.dynamic_list = dynamic_list;
	  if (command_line.symbolic == symbolic)
	    command_line.symbolic = symbolic_unset;
	  break;
	case OPTION_WARN_COMMON:
	  config.warn_common = TRUE;
	  break;
	case OPTION_WARN_CONSTRUCTORS:
	  config.warn_constructors = TRUE;
	  break;
	case OPTION_WARN_FATAL:
	  config.fatal_warnings = TRUE;
	  break;
	case OPTION_NO_WARN_FATAL:
	  no_fatal_warnings = TRUE;
	  break;
	case OPTION_WARN_MULTIPLE_GP:
	  config.warn_multiple_gp = TRUE;
	  break;
	case OPTION_WARN_ONCE:
	  config.warn_once = TRUE;
	  break;
	case OPTION_WARN_SECTION_ALIGN:
	  config.warn_section_align = TRUE;
	  break;
	case OPTION_WARN_SHARED_TEXTREL:
	  link_info.warn_shared_textrel = TRUE;
	  break;
	case OPTION_WHOLE_ARCHIVE:
	  whole_archive = TRUE;
	  break;
	case OPTION_ADD_NEEDED:
	  add_needed = TRUE;
	  break;
	case OPTION_NO_ADD_NEEDED:
	  add_needed = FALSE;
	  break;
	case OPTION_AS_NEEDED:
/* XXX: --as-needed is broken on powerpc64 */
#ifndef __powerpc64__
	  as_needed = TRUE;
	  break;
#endif
	case OPTION_NO_AS_NEEDED:
	  as_needed = FALSE;
	  break;
	case OPTION_WRAP:
	  add_wrap (optarg);
	  break;
	case OPTION_DISCARD_NONE:
	  link_info.discard = discard_none;
	  break;
	case 'X':
	  link_info.discard = discard_l;
	  break;
	case 'x':
	  link_info.discard = discard_all;
	  break;
	case 'Y':
	  if (CONST_STRNEQ (optarg, "P,"))
	    optarg += 2;
	  if (default_dirlist != NULL)
	    free (default_dirlist);
	  default_dirlist = xstrdup (optarg);
	  break;
	case 'y':
	  add_ysym (optarg);
	  break;
	case OPTION_SPARE_DYNAMIC_TAGS:
	  link_info.spare_dynamic_tags = strtoul (optarg, NULL, 0);
	  break;
	case OPTION_SPLIT_BY_RELOC:
	  if (optarg != NULL)
	    config.split_by_reloc = strtoul (optarg, NULL, 0);
	  else
	    config.split_by_reloc = 32768;
	  break;
	case OPTION_SPLIT_BY_FILE:
	  if (optarg != NULL)
	    config.split_by_file = bfd_scan_vma (optarg, NULL, 0);
	  else
	    config.split_by_file = 1;
	  break;
	case OPTION_CHECK_SECTIONS:
	  command_line.check_section_addresses = TRUE;
	  break;
	case OPTION_NO_CHECK_SECTIONS:
	  command_line.check_section_addresses = FALSE;
	  break;
	case OPTION_ACCEPT_UNKNOWN_INPUT_ARCH:
	  command_line.accept_unknown_input_arch = TRUE;
	  break;
	case OPTION_NO_ACCEPT_UNKNOWN_INPUT_ARCH:
	  command_line.accept_unknown_input_arch = FALSE;
	  break;
	case '(':
	  if (ingroup)
	    einfo (_("%P%F: may not nest groups (--help for usage)\n"));

	  lang_enter_group ();
	  ingroup = 1;
	  break;
	case ')':
	  if (! ingroup)
	    einfo (_("%P%F: group ended before it began (--help for usage)\n"));

	  lang_leave_group ();
	  ingroup = 0;
	  break;

	case OPTION_INIT:
	  link_info.init_function = optarg;
	  break;

	case OPTION_FINI:
	  link_info.fini_function = optarg;
	  break;

	case OPTION_REDUCE_MEMORY_OVERHEADS:
	  link_info.reduce_memory_overheads = TRUE;
	  if (config.hash_table_size == 0)
	    config.hash_table_size = 1021;
	  break;

        case OPTION_HASH_SIZE:
	  {
	    bfd_size_type new_size;

            new_size = strtoul (optarg, NULL, 0);
            if (new_size)
              config.hash_table_size = new_size;
            else
              einfo (_("%P%X: --hash-size needs a numeric argument\n"));
          }
          break;
	}
    }
  if (no_fatal_warnings)
    config.fatal_warnings = FALSE;

  if (ingroup)
    lang_leave_group ();

  if (default_dirlist != NULL)
    {
      set_default_dirlist (default_dirlist);
      free (default_dirlist);
    }

  if (link_info.unresolved_syms_in_objects == RM_NOT_YET_SET)
    /* FIXME: Should we allow emulations a chance to set this ?  */
    link_info.unresolved_syms_in_objects = how_to_report_unresolved_symbols;

  if (link_info.unresolved_syms_in_shared_libs == RM_NOT_YET_SET)
    /* FIXME: Should we allow emulations a chance to set this ?  */
    link_info.unresolved_syms_in_shared_libs = how_to_report_unresolved_symbols;
}

/* Add the (colon-separated) elements of DIRLIST_PTR to the
   library search path.  */

static void
set_default_dirlist (char *dirlist_ptr)
{
  char *p;

  while (1)
    {
      p = strchr (dirlist_ptr, PATH_SEPARATOR);
      if (p != NULL)
	*p = '\0';
      if (*dirlist_ptr != '\0')
	ldfile_add_library_path (dirlist_ptr, TRUE);
      if (p == NULL)
	break;
      dirlist_ptr = p + 1;
    }
}

static void
set_section_start (char *sect, char *valstr)
{
  const char *end;
  bfd_vma val = bfd_scan_vma (valstr, &end, 16);
  if (*end)
    einfo (_("%P%F: invalid hex number `%s'\n"), valstr);
  lang_section_start (sect, exp_intop (val), NULL);
}

static void
set_segment_start (const char *section, char *valstr)
{
  const char *name;
  const char *end;
  segment_type *seg;

  bfd_vma val = bfd_scan_vma (valstr, &end, 16);
  if (*end)
    einfo (_("%P%F: invalid hex number `%s'\n"), valstr);
  /* If we already have an entry for this segment, update the existing
     value.  */
  name = section + 1;
  for (seg = segments; seg; seg = seg->next)
    if (strcmp (seg->name, name) == 0)
      {
	seg->value = val;
	return;
      }
  /* There was no existing value so we must create a new segment
     entry.  */
  seg = stat_alloc (sizeof (*seg));
  seg->name = name;
  seg->value = val;
  seg->used = FALSE;
  /* Add it to the linked list of segments.  */
  seg->next = segments;
  segments = seg;
  /* Historically, -Ttext and friends set the base address of a
     particular section.  For backwards compatibility, we still do
     that.  If a SEGMENT_START directive is seen, the section address
     assignment will be disabled.  */
  lang_section_start (section, exp_intop (val), seg);
}


/* Print help messages for the options.  */

static void
help (void)
{
  unsigned i;
  const char **targets, **pp;
  int len;

  printf (_("Usage: %s [options] file...\n"), program_name);

  printf (_("Options:\n"));
  for (i = 0; i < OPTION_COUNT; i++)
    {
      if (ld_options[i].doc != NULL)
	{
	  bfd_boolean comma;
	  unsigned j;

	  printf ("  ");

	  comma = FALSE;
	  len = 2;

	  j = i;
	  do
	    {
	      if (ld_options[j].shortopt != '\0'
		  && ld_options[j].control != NO_HELP)
		{
		  printf ("%s-%c", comma ? ", " : "", ld_options[j].shortopt);
		  len += (comma ? 2 : 0) + 2;
		  if (ld_options[j].arg != NULL)
		    {
		      if (ld_options[j].opt.has_arg != optional_argument)
			{
			  printf (" ");
			  ++len;
			}
		      printf ("%s", _(ld_options[j].arg));
		      len += strlen (_(ld_options[j].arg));
		    }
		  comma = TRUE;
		}
	      ++j;
	    }
	  while (j < OPTION_COUNT && ld_options[j].doc == NULL);

	  j = i;
	  do
	    {
	      if (ld_options[j].opt.name != NULL
		  && ld_options[j].control != NO_HELP)
		{
		  int two_dashes =
		    (ld_options[j].control == TWO_DASHES
		     || ld_options[j].control == EXACTLY_TWO_DASHES);

		  printf ("%s-%s%s",
			  comma ? ", " : "",
			  two_dashes ? "-" : "",
			  ld_options[j].opt.name);
		  len += ((comma ? 2 : 0)
			  + 1
			  + (two_dashes ? 1 : 0)
			  + strlen (ld_options[j].opt.name));
		  if (ld_options[j].arg != NULL)
		    {
		      printf (" %s", _(ld_options[j].arg));
		      len += 1 + strlen (_(ld_options[j].arg));
		    }
		  comma = TRUE;
		}
	      ++j;
	    }
	  while (j < OPTION_COUNT && ld_options[j].doc == NULL);

	  if (len >= 30)
	    {
	      printf ("\n");
	      len = 0;
	    }

	  for (; len < 30; len++)
	    putchar (' ');

	  printf ("%s\n", _(ld_options[i].doc));
	}
    }
  printf (_("  @FILE"));
  for (len = strlen ("  @FILE"); len < 30; len++)
    putchar (' ');
  printf (_("Read options from FILE\n"));

  /* Note: Various tools (such as libtool) depend upon the
     format of the listings below - do not change them.  */
  /* xgettext:c-format */
  printf (_("%s: supported targets:"), program_name);
  targets = bfd_target_list ();
  for (pp = targets; *pp != NULL; pp++)
    printf (" %s", *pp);
  free (targets);
  printf ("\n");

  /* xgettext:c-format */
  printf (_("%s: supported emulations: "), program_name);
  ldemul_list_emulations (stdout);
  printf ("\n");

  /* xgettext:c-format */
  printf (_("%s: emulation specific options:\n"), program_name);
  ldemul_list_emulation_options (stdout);
  printf ("\n");

  if (REPORT_BUGS_TO[0])
    printf (_("Report bugs to %s\n"), REPORT_BUGS_TO);
}
