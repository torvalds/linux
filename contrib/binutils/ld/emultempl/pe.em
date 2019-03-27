# This shell script emits a C file. -*- C -*-
# It does some substitutions.
test -z "${ENTRY}" && ENTRY="_mainCRTStartup"
if [ -z "$MACHINE" ]; then
  OUTPUT_ARCH=${ARCH}
else
  OUTPUT_ARCH=${ARCH}:${MACHINE}
fi
rm -f e${EMULATION_NAME}.c
(echo;echo;echo;echo;echo)>e${EMULATION_NAME}.c # there, now line numbers match ;-)
cat >>e${EMULATION_NAME}.c <<EOF
/* This file is part of GLD, the Gnu Linker.
   Copyright 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2006, 2007 Free Software Foundation, Inc.

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

/* For WINDOWS_NT */
/* The original file generated returned different default scripts depending
   on whether certain switches were set, but these switches pertain to the
   Linux system and that particular version of coff.  In the NT case, we
   only determine if the subsystem is console or windows in order to select
   the correct entry point by default. */

#define TARGET_IS_${EMULATION_NAME}

/* Do this before including bfd.h, so we prototype the right functions.  */

#if defined(TARGET_IS_armpe) \
    || defined(TARGET_IS_arm_epoc_pe) \
    || defined(TARGET_IS_arm_wince_pe)
#define bfd_arm_allocate_interworking_sections \
	bfd_${EMULATION_NAME}_allocate_interworking_sections
#define bfd_arm_get_bfd_for_interworking \
	bfd_${EMULATION_NAME}_get_bfd_for_interworking
#define bfd_arm_process_before_allocation \
	bfd_${EMULATION_NAME}_process_before_allocation
#endif
 
#include "sysdep.h"
#include "bfd.h"
#include "bfdlink.h"
#include "getopt.h"
#include "libiberty.h"
#include "ld.h"
#include "ldmain.h"
#include "ldexp.h"
#include "ldlang.h"
#include "ldfile.h"
#include "ldemul.h"
#include <ldgram.h>
#include "ldlex.h"
#include "ldmisc.h"
#include "ldctor.h"
#include "coff/internal.h"

/* FIXME: See bfd/peXXigen.c for why we include an architecture specific
   header in generic PE code.  */
#include "coff/i386.h"
#include "coff/pe.h"

/* FIXME: This is a BFD internal header file, and we should not be
   using it here.  */
#include "../bfd/libcoff.h"

#include "deffile.h"
#include "pe-dll.h"
#include "safe-ctype.h"

/* Permit the emulation parameters to override the default section
   alignment by setting OVERRIDE_SECTION_ALIGNMENT.  FIXME: This makes
   it seem that include/coff/internal.h should not define
   PE_DEF_SECTION_ALIGNMENT.  */
#if PE_DEF_SECTION_ALIGNMENT != ${OVERRIDE_SECTION_ALIGNMENT:-PE_DEF_SECTION_ALIGNMENT}
#undef PE_DEF_SECTION_ALIGNMENT
#define PE_DEF_SECTION_ALIGNMENT ${OVERRIDE_SECTION_ALIGNMENT}
#endif

#if defined(TARGET_IS_i386pe) \
    || defined(TARGET_IS_shpe) \
    || defined(TARGET_IS_mipspe) \
    || defined(TARGET_IS_armpe) \
    || defined(TARGET_IS_arm_epoc_pe) \
    || defined(TARGET_IS_arm_wince_pe)
#define DLL_SUPPORT
#endif

#if defined(TARGET_IS_i386pe) || ! defined(DLL_SUPPORT)
#define	PE_DEF_SUBSYSTEM		3
#else
#undef NT_EXE_IMAGE_BASE
#undef PE_DEF_SECTION_ALIGNMENT
#undef PE_DEF_FILE_ALIGNMENT
#define NT_EXE_IMAGE_BASE		0x00010000

#if defined(TARGET_IS_armpe) || defined(TARGET_IS_arm_wince_pe)
#define PE_DEF_SECTION_ALIGNMENT	0x00001000
#define	PE_DEF_SUBSYSTEM		9
#else
#define PE_DEF_SECTION_ALIGNMENT	0x00000400
#define	PE_DEF_SUBSYSTEM		2
#endif
#define PE_DEF_FILE_ALIGNMENT		0x00000200
#endif

#define U(S) ${INITIAL_SYMBOL_CHAR} S

static struct internal_extra_pe_aouthdr pe;
static int dll;
static flagword real_flags = 0;
static int support_old_code = 0;
static char * thumb_entry_symbol = NULL;
static lang_assignment_statement_type *image_base_statement = 0;

#ifdef DLL_SUPPORT
static int pe_enable_stdcall_fixup = -1; /* 0=disable 1=enable.  */
static char *pe_out_def_filename = NULL;
static char *pe_implib_filename = NULL;
static int pe_enable_auto_image_base = 0;
static char *pe_dll_search_prefix = NULL;
#endif

extern const char *output_filename;

static void
gld_${EMULATION_NAME}_before_parse (void)
{
  ldfile_set_output_arch ("${OUTPUT_ARCH}", bfd_arch_`echo ${ARCH} | sed -e 's/:.*//'`);
  output_filename = "${EXECUTABLE_NAME:-a.exe}";
#ifdef DLL_SUPPORT
  config.dynamic_link = TRUE;
  config.has_shared = 1;
  link_info.pei386_auto_import = -1;
  link_info.pei386_runtime_pseudo_reloc = -1;

#if (PE_DEF_SUBSYSTEM == 9) || (PE_DEF_SUBSYSTEM == 2)
#if defined TARGET_IS_mipspe || defined TARGET_IS_armpe || defined TARGET_IS_arm_wince_pe
  lang_default_entry ("WinMainCRTStartup");
#else
  lang_default_entry ("_WinMainCRTStartup");
#endif
#else
  lang_default_entry ("${ENTRY}");
#endif
#endif
}

/* PE format extra command line options.  */

/* Used for setting flags in the PE header.  */
#define OPTION_BASE_FILE		(300  + 1)
#define OPTION_DLL			(OPTION_BASE_FILE + 1)
#define OPTION_FILE_ALIGNMENT		(OPTION_DLL + 1)
#define OPTION_IMAGE_BASE		(OPTION_FILE_ALIGNMENT + 1)
#define OPTION_MAJOR_IMAGE_VERSION	(OPTION_IMAGE_BASE + 1)
#define OPTION_MAJOR_OS_VERSION		(OPTION_MAJOR_IMAGE_VERSION + 1)
#define OPTION_MAJOR_SUBSYSTEM_VERSION	(OPTION_MAJOR_OS_VERSION + 1)
#define OPTION_MINOR_IMAGE_VERSION	(OPTION_MAJOR_SUBSYSTEM_VERSION + 1)
#define OPTION_MINOR_OS_VERSION		(OPTION_MINOR_IMAGE_VERSION + 1)
#define OPTION_MINOR_SUBSYSTEM_VERSION	(OPTION_MINOR_OS_VERSION + 1)
#define OPTION_SECTION_ALIGNMENT	(OPTION_MINOR_SUBSYSTEM_VERSION + 1)
#define OPTION_STACK			(OPTION_SECTION_ALIGNMENT + 1)
#define OPTION_SUBSYSTEM		(OPTION_STACK + 1)
#define OPTION_HEAP			(OPTION_SUBSYSTEM + 1)
#define OPTION_SUPPORT_OLD_CODE		(OPTION_HEAP + 1)
#define OPTION_OUT_DEF			(OPTION_SUPPORT_OLD_CODE + 1)
#define OPTION_EXPORT_ALL		(OPTION_OUT_DEF + 1)
#define OPTION_EXCLUDE_SYMBOLS		(OPTION_EXPORT_ALL + 1)
#define OPTION_KILL_ATS			(OPTION_EXCLUDE_SYMBOLS + 1)
#define OPTION_STDCALL_ALIASES		(OPTION_KILL_ATS + 1)
#define OPTION_ENABLE_STDCALL_FIXUP	(OPTION_STDCALL_ALIASES + 1)
#define OPTION_DISABLE_STDCALL_FIXUP	(OPTION_ENABLE_STDCALL_FIXUP + 1)
#define OPTION_IMPLIB_FILENAME		(OPTION_DISABLE_STDCALL_FIXUP + 1)
#define OPTION_THUMB_ENTRY		(OPTION_IMPLIB_FILENAME + 1)
#define OPTION_WARN_DUPLICATE_EXPORTS	(OPTION_THUMB_ENTRY + 1)
#define OPTION_IMP_COMPAT		(OPTION_WARN_DUPLICATE_EXPORTS + 1)
#define OPTION_ENABLE_AUTO_IMAGE_BASE	(OPTION_IMP_COMPAT + 1)
#define OPTION_DISABLE_AUTO_IMAGE_BASE	(OPTION_ENABLE_AUTO_IMAGE_BASE + 1)
#define OPTION_DLL_SEARCH_PREFIX	(OPTION_DISABLE_AUTO_IMAGE_BASE + 1)
#define OPTION_NO_DEFAULT_EXCLUDES	(OPTION_DLL_SEARCH_PREFIX + 1)
#define OPTION_DLL_ENABLE_AUTO_IMPORT	(OPTION_NO_DEFAULT_EXCLUDES + 1)
#define OPTION_DLL_DISABLE_AUTO_IMPORT	(OPTION_DLL_ENABLE_AUTO_IMPORT + 1)
#define OPTION_ENABLE_EXTRA_PE_DEBUG	(OPTION_DLL_DISABLE_AUTO_IMPORT + 1)
#define OPTION_EXCLUDE_LIBS		(OPTION_ENABLE_EXTRA_PE_DEBUG + 1)
#define OPTION_DLL_ENABLE_RUNTIME_PSEUDO_RELOC	\
					(OPTION_EXCLUDE_LIBS + 1)
#define OPTION_DLL_DISABLE_RUNTIME_PSEUDO_RELOC	\
					(OPTION_DLL_ENABLE_RUNTIME_PSEUDO_RELOC + 1)
#define OPTION_LARGE_ADDRESS_AWARE \
					(OPTION_DLL_DISABLE_RUNTIME_PSEUDO_RELOC + 1)

static void
gld${EMULATION_NAME}_add_options
  (int ns ATTRIBUTE_UNUSED, char **shortopts ATTRIBUTE_UNUSED, int nl,
    struct option **longopts, int nrl ATTRIBUTE_UNUSED,
    struct option **really_longopts ATTRIBUTE_UNUSED)
{
  static const struct option xtra_long[] = {
    /* PE options */
    {"base-file", required_argument, NULL, OPTION_BASE_FILE},
    {"dll", no_argument, NULL, OPTION_DLL},
    {"file-alignment", required_argument, NULL, OPTION_FILE_ALIGNMENT},
    {"heap", required_argument, NULL, OPTION_HEAP},
    {"image-base", required_argument, NULL, OPTION_IMAGE_BASE},
    {"major-image-version", required_argument, NULL, OPTION_MAJOR_IMAGE_VERSION},
    {"major-os-version", required_argument, NULL, OPTION_MAJOR_OS_VERSION},
    {"major-subsystem-version", required_argument, NULL, OPTION_MAJOR_SUBSYSTEM_VERSION},
    {"minor-image-version", required_argument, NULL, OPTION_MINOR_IMAGE_VERSION},
    {"minor-os-version", required_argument, NULL, OPTION_MINOR_OS_VERSION},
    {"minor-subsystem-version", required_argument, NULL, OPTION_MINOR_SUBSYSTEM_VERSION},
    {"section-alignment", required_argument, NULL, OPTION_SECTION_ALIGNMENT},
    {"stack", required_argument, NULL, OPTION_STACK},
    {"subsystem", required_argument, NULL, OPTION_SUBSYSTEM},
    {"support-old-code", no_argument, NULL, OPTION_SUPPORT_OLD_CODE},
    {"thumb-entry", required_argument, NULL, OPTION_THUMB_ENTRY},
#ifdef DLL_SUPPORT
    /* getopt allows abbreviations, so we do this to stop it from treating -o
       as an abbreviation for this option */
    {"output-def", required_argument, NULL, OPTION_OUT_DEF},
    {"output-def", required_argument, NULL, OPTION_OUT_DEF},
    {"export-all-symbols", no_argument, NULL, OPTION_EXPORT_ALL},
    {"exclude-symbols", required_argument, NULL, OPTION_EXCLUDE_SYMBOLS},
    {"exclude-libs", required_argument, NULL, OPTION_EXCLUDE_LIBS},
    {"kill-at", no_argument, NULL, OPTION_KILL_ATS},
    {"add-stdcall-alias", no_argument, NULL, OPTION_STDCALL_ALIASES},
    {"enable-stdcall-fixup", no_argument, NULL, OPTION_ENABLE_STDCALL_FIXUP},
    {"disable-stdcall-fixup", no_argument, NULL, OPTION_DISABLE_STDCALL_FIXUP},
    {"out-implib", required_argument, NULL, OPTION_IMPLIB_FILENAME},
    {"warn-duplicate-exports", no_argument, NULL, OPTION_WARN_DUPLICATE_EXPORTS},
    /* getopt() allows abbreviations, so we do this to stop it from
       treating -c as an abbreviation for these --compat-implib.  */
    {"compat-implib", no_argument, NULL, OPTION_IMP_COMPAT},
    {"compat-implib", no_argument, NULL, OPTION_IMP_COMPAT},
    {"enable-auto-image-base", no_argument, NULL, OPTION_ENABLE_AUTO_IMAGE_BASE},
    {"disable-auto-image-base", no_argument, NULL, OPTION_DISABLE_AUTO_IMAGE_BASE},
    {"dll-search-prefix", required_argument, NULL, OPTION_DLL_SEARCH_PREFIX},
    {"no-default-excludes", no_argument, NULL, OPTION_NO_DEFAULT_EXCLUDES},
    {"enable-auto-import", no_argument, NULL, OPTION_DLL_ENABLE_AUTO_IMPORT},
    {"disable-auto-import", no_argument, NULL, OPTION_DLL_DISABLE_AUTO_IMPORT},
    {"enable-extra-pe-debug", no_argument, NULL, OPTION_ENABLE_EXTRA_PE_DEBUG},
    {"enable-runtime-pseudo-reloc", no_argument, NULL, OPTION_DLL_ENABLE_RUNTIME_PSEUDO_RELOC},
    {"disable-runtime-pseudo-reloc", no_argument, NULL, OPTION_DLL_DISABLE_RUNTIME_PSEUDO_RELOC},
#endif
    {"large-address-aware", no_argument, NULL, OPTION_LARGE_ADDRESS_AWARE},
    {NULL, no_argument, NULL, 0}
  };

  *longopts = (struct option *)
    xrealloc (*longopts, nl * sizeof (struct option) + sizeof (xtra_long));
  memcpy (*longopts + nl, &xtra_long, sizeof (xtra_long));
}

/* PE/WIN32; added routines to get the subsystem type, heap and/or stack
   parameters which may be input from the command line.  */

typedef struct
{
  void *ptr;
  int size;
  int value;
  char *symbol;
  int inited;
} definfo;

#define D(field,symbol,def)  {&pe.field,sizeof(pe.field), def, symbol,0}

static definfo init[] =
{
  /* imagebase must be first */
#define IMAGEBASEOFF 0
  D(ImageBase,"__image_base__", NT_EXE_IMAGE_BASE),
#define DLLOFF 1
  {&dll, sizeof(dll), 0, "__dll__", 0},
  D(SectionAlignment,"__section_alignment__", PE_DEF_SECTION_ALIGNMENT),
  D(FileAlignment,"__file_alignment__", PE_DEF_FILE_ALIGNMENT),
  D(MajorOperatingSystemVersion,"__major_os_version__", 4),
  D(MinorOperatingSystemVersion,"__minor_os_version__", 0),
  D(MajorImageVersion,"__major_image_version__", 1),
  D(MinorImageVersion,"__minor_image_version__", 0),
#if defined(TARGET_IS_armpe)  || defined(TARGET_IS_arm_wince_pe)
  D(MajorSubsystemVersion,"__major_subsystem_version__", 3),
#else
  D(MajorSubsystemVersion,"__major_subsystem_version__", 4),
#endif
  D(MinorSubsystemVersion,"__minor_subsystem_version__", 0),
  D(Subsystem,"__subsystem__", ${SUBSYSTEM}),
  D(SizeOfStackReserve,"__size_of_stack_reserve__", 0x200000),
  D(SizeOfStackCommit,"__size_of_stack_commit__", 0x1000),
  D(SizeOfHeapReserve,"__size_of_heap_reserve__", 0x100000),
  D(SizeOfHeapCommit,"__size_of_heap_commit__", 0x1000),
  D(LoaderFlags,"__loader_flags__", 0x0),
  { NULL, 0, 0, NULL, 0 }
};


static void
gld_${EMULATION_NAME}_list_options (FILE *file)
{
  fprintf (file, _("  --base_file <basefile>             Generate a base file for relocatable DLLs\n"));
  fprintf (file, _("  --dll                              Set image base to the default for DLLs\n"));
  fprintf (file, _("  --file-alignment <size>            Set file alignment\n"));
  fprintf (file, _("  --heap <size>                      Set initial size of the heap\n"));
  fprintf (file, _("  --image-base <address>             Set start address of the executable\n"));
  fprintf (file, _("  --major-image-version <number>     Set version number of the executable\n"));
  fprintf (file, _("  --major-os-version <number>        Set minimum required OS version\n"));
  fprintf (file, _("  --major-subsystem-version <number> Set minimum required OS subsystem version\n"));
  fprintf (file, _("  --minor-image-version <number>     Set revision number of the executable\n"));
  fprintf (file, _("  --minor-os-version <number>        Set minimum required OS revision\n"));
  fprintf (file, _("  --minor-subsystem-version <number> Set minimum required OS subsystem revision\n"));
  fprintf (file, _("  --section-alignment <size>         Set section alignment\n"));
  fprintf (file, _("  --stack <size>                     Set size of the initial stack\n"));
  fprintf (file, _("  --subsystem <name>[:<version>]     Set required OS subsystem [& version]\n"));
  fprintf (file, _("  --support-old-code                 Support interworking with old code\n"));
  fprintf (file, _("  --thumb-entry=<symbol>             Set the entry point to be Thumb <symbol>\n"));
#ifdef DLL_SUPPORT
  fprintf (file, _("  --add-stdcall-alias                Export symbols with and without @nn\n"));
  fprintf (file, _("  --disable-stdcall-fixup            Don't link _sym to _sym@nn\n"));
  fprintf (file, _("  --enable-stdcall-fixup             Link _sym to _sym@nn without warnings\n"));
  fprintf (file, _("  --exclude-symbols sym,sym,...      Exclude symbols from automatic export\n"));
  fprintf (file, _("  --exclude-libs lib,lib,...         Exclude libraries from automatic export\n"));
  fprintf (file, _("  --export-all-symbols               Automatically export all globals to DLL\n"));
  fprintf (file, _("  --kill-at                          Remove @nn from exported symbols\n"));
  fprintf (file, _("  --out-implib <file>                Generate import library\n"));
  fprintf (file, _("  --output-def <file>                Generate a .DEF file for the built DLL\n"));
  fprintf (file, _("  --warn-duplicate-exports           Warn about duplicate exports.\n"));
  fprintf (file, _("  --compat-implib                    Create backward compatible import libs;\n\
                                       create __imp_<SYMBOL> as well.\n"));
  fprintf (file, _("  --enable-auto-image-base           Automatically choose image base for DLLs\n\
                                       unless user specifies one\n"));
  fprintf (file, _("  --disable-auto-image-base          Do not auto-choose image base. (default)\n"));
  fprintf (file, _("  --dll-search-prefix=<string>       When linking dynamically to a dll without\n\
                                       an importlib, use <string><basename>.dll\n\
                                       in preference to lib<basename>.dll \n"));
  fprintf (file, _("  --enable-auto-import               Do sophistcated linking of _sym to\n\
                                       __imp_sym for DATA references\n"));
  fprintf (file, _("  --disable-auto-import              Do not auto-import DATA items from DLLs\n"));
  fprintf (file, _("  --enable-runtime-pseudo-reloc      Work around auto-import limitations by\n\
                                       adding pseudo-relocations resolved at\n\
                                       runtime.\n"));
  fprintf (file, _("  --disable-runtime-pseudo-reloc     Do not add runtime pseudo-relocations for\n\
                                       auto-imported DATA.\n"));
  fprintf (file, _("  --enable-extra-pe-debug            Enable verbose debug output when building\n\
                                       or linking to DLLs (esp. auto-import)\n"));
#endif
  fprintf (file, _("  --large-address-aware              Executable supports virtual addresses\n\
                                       greater than 2 gigabytes\n"));
}


static void
set_pe_name (char *name, long val)
{
  int i;

  /* Find the name and set it.  */
  for (i = 0; init[i].ptr; i++)
    {
      if (strcmp (name, init[i].symbol) == 0)
	{
	  init[i].value = val;
	  init[i].inited = 1;
	  return;
	}
    }
  abort ();
}


static void
set_pe_subsystem (void)
{
  const char *sver;
  const char *entry;
  const char *initial_symbol_char;
  char *end;
  int len;
  int i;
  int subsystem;
  unsigned long temp_subsystem;
  static const struct
    {
      const char *name;
      const int value;
      const char *entry;
    }
  v[] =
    {
      { "native",  1, "NtProcessStartup" },
      { "windows", 2, "WinMainCRTStartup" },
      { "console", 3, "mainCRTStartup" },
      { "posix",   7, "__PosixProcessStartup"},
      { "wince",   9, "WinMainCRTStartup" },
      { "xbox",   14, "mainCRTStartup" },
      { NULL, 0, NULL }
    };
  /* Entry point name for arbitrary subsystem numbers.  */
  static const char default_entry[] = "mainCRTStartup";

  /* Check for the presence of a version number.  */
  sver = strchr (optarg, ':');
  if (sver == NULL)
    len = strlen (optarg);
  else
    {
      len = sver - optarg;
      set_pe_name ("__major_subsystem_version__",
		   strtoul (sver + 1, &end, 0));
      if (*end == '.')
	set_pe_name ("__minor_subsystem_version__",
		     strtoul (end + 1, &end, 0));
      if (*end != '\0')
	einfo (_("%P: warning: bad version number in -subsystem option\n"));
    }

  /* Check for numeric subsystem.  */
  temp_subsystem = strtoul (optarg, & end, 0);
  if ((*end == ':' || *end == '\0') && (temp_subsystem < 65536))
    {
      /* Search list for a numeric match to use its entry point.  */
      for (i = 0; v[i].name; i++)
	if (v[i].value == (int) temp_subsystem)
	  break;

      /* If no match, use the default.  */
      if (v[i].name != NULL)
	entry = v[i].entry;
      else
	entry = default_entry;

      /* Use this subsystem.  */
      subsystem = (int) temp_subsystem;
    }
  else
    {
      /* Search for subsystem by name.  */
      for (i = 0; v[i].name; i++)
	if (strncmp (optarg, v[i].name, len) == 0
	    && v[i].name[len] == '\0')
	  break;

      if (v[i].name == NULL)
	{
	  einfo (_("%P%F: invalid subsystem type %s\n"), optarg);
	  return;
	}

      entry = v[i].entry;
      subsystem = v[i].value;
    }

  set_pe_name ("__subsystem__", subsystem);

  initial_symbol_char = ${INITIAL_SYMBOL_CHAR};
  if (*initial_symbol_char != '\0')
    {
      char *alc_entry;

      /* lang_default_entry expects its argument to be permanently
	 allocated, so we don't free this string.  */
      alc_entry = xmalloc (strlen (initial_symbol_char)
			   + strlen (entry)
			   + 1);
      strcpy (alc_entry, initial_symbol_char);
      strcat (alc_entry, entry);
      entry = alc_entry;
    }

  lang_default_entry (entry);

  return;
}


static void
set_pe_value (char *name)
{
  char *end;

  set_pe_name (name,  strtoul (optarg, &end, 0));

  if (end == optarg)
    einfo (_("%P%F: invalid hex number for PE parameter '%s'\n"), optarg);

  optarg = end;
}


static void
set_pe_stack_heap (char *resname, char *comname)
{
  set_pe_value (resname);

  if (*optarg == ',')
    {
      optarg++;
      set_pe_value (comname);
    }
  else if (*optarg)
    einfo (_("%P%F: strange hex info for PE parameter '%s'\n"), optarg);
}


static bfd_boolean
gld${EMULATION_NAME}_handle_option (int optc)
{
  switch (optc)
    {
    default:
      return FALSE;

    case OPTION_BASE_FILE:
      link_info.base_file = fopen (optarg, FOPEN_WB);
      if (link_info.base_file == NULL)
	{
	  /* xgettext:c-format */
	  fprintf (stderr, _("%s: Can't open base file %s\n"),
		   program_name, optarg);
	  xexit (1);
	}
      break;

      /* PE options.  */
    case OPTION_HEAP:
      set_pe_stack_heap ("__size_of_heap_reserve__", "__size_of_heap_commit__");
      break;
    case OPTION_STACK:
      set_pe_stack_heap ("__size_of_stack_reserve__", "__size_of_stack_commit__");
      break;
    case OPTION_SUBSYSTEM:
      set_pe_subsystem ();
      break;
    case OPTION_MAJOR_OS_VERSION:
      set_pe_value ("__major_os_version__");
      break;
    case OPTION_MINOR_OS_VERSION:
      set_pe_value ("__minor_os_version__");
      break;
    case OPTION_MAJOR_SUBSYSTEM_VERSION:
      set_pe_value ("__major_subsystem_version__");
      break;
    case OPTION_MINOR_SUBSYSTEM_VERSION:
      set_pe_value ("__minor_subsystem_version__");
      break;
    case OPTION_MAJOR_IMAGE_VERSION:
      set_pe_value ("__major_image_version__");
      break;
    case OPTION_MINOR_IMAGE_VERSION:
      set_pe_value ("__minor_image_version__");
      break;
    case OPTION_FILE_ALIGNMENT:
      set_pe_value ("__file_alignment__");
      break;
    case OPTION_SECTION_ALIGNMENT:
      set_pe_value ("__section_alignment__");
      break;
    case OPTION_DLL:
      set_pe_name ("__dll__", 1);
      break;
    case OPTION_IMAGE_BASE:
      set_pe_value ("__image_base__");
      break;
    case OPTION_SUPPORT_OLD_CODE:
      support_old_code = 1;
      break;
    case OPTION_THUMB_ENTRY:
      thumb_entry_symbol = optarg;
      break;
#ifdef DLL_SUPPORT
    case OPTION_OUT_DEF:
      pe_out_def_filename = xstrdup (optarg);
      break;
    case OPTION_EXPORT_ALL:
      pe_dll_export_everything = 1;
      break;
    case OPTION_EXCLUDE_SYMBOLS:
      pe_dll_add_excludes (optarg, 0);
      break;
    case OPTION_EXCLUDE_LIBS:
      pe_dll_add_excludes (optarg, 1);
      break;
    case OPTION_KILL_ATS:
      pe_dll_kill_ats = 1;
      break;
    case OPTION_STDCALL_ALIASES:
      pe_dll_stdcall_aliases = 1;
      break;
    case OPTION_ENABLE_STDCALL_FIXUP:
      pe_enable_stdcall_fixup = 1;
      break;
    case OPTION_DISABLE_STDCALL_FIXUP:
      pe_enable_stdcall_fixup = 0;
      break;
    case OPTION_IMPLIB_FILENAME:
      pe_implib_filename = xstrdup (optarg);
      break;
    case OPTION_WARN_DUPLICATE_EXPORTS:
      pe_dll_warn_dup_exports = 1;
      break;
    case OPTION_IMP_COMPAT:
      pe_dll_compat_implib = 1;
      break;
    case OPTION_ENABLE_AUTO_IMAGE_BASE:
      pe_enable_auto_image_base = 1;
      break;
    case OPTION_DISABLE_AUTO_IMAGE_BASE:
      pe_enable_auto_image_base = 0;
      break;
    case OPTION_DLL_SEARCH_PREFIX:
      pe_dll_search_prefix = xstrdup (optarg);
      break;
    case OPTION_NO_DEFAULT_EXCLUDES:
      pe_dll_do_default_excludes = 0;
      break;
    case OPTION_DLL_ENABLE_AUTO_IMPORT:
      link_info.pei386_auto_import = 1;
      break;
    case OPTION_DLL_DISABLE_AUTO_IMPORT:
      link_info.pei386_auto_import = 0;
      break;
    case OPTION_DLL_ENABLE_RUNTIME_PSEUDO_RELOC:
      link_info.pei386_runtime_pseudo_reloc = 1;
      break;
    case OPTION_DLL_DISABLE_RUNTIME_PSEUDO_RELOC:
      link_info.pei386_runtime_pseudo_reloc = 0;
      break;
    case OPTION_ENABLE_EXTRA_PE_DEBUG:
      pe_dll_extra_pe_debug = 1;
      break;
#endif
    case OPTION_LARGE_ADDRESS_AWARE:
      real_flags |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
      break;
    }
  return TRUE;
}


#ifdef DLL_SUPPORT
static unsigned long
strhash (const char *str)
{
  const unsigned char *s;
  unsigned long hash;
  unsigned int c;
  unsigned int len;

  hash = 0;
  len = 0;
  s = (const unsigned char *) str;
  while ((c = *s++) != '\0')
    {
      hash += c + (c << 17);
      hash ^= hash >> 2;
      ++len;
    }
  hash += len + (len << 17);
  hash ^= hash >> 2;

  return hash;
}

/* Use the output file to create a image base for relocatable DLLs.  */

static unsigned long
compute_dll_image_base (const char *ofile)
{
  unsigned long hash = strhash (ofile);
  return 0x61300000 + ((hash << 16) & 0x0FFC0000);
}
#endif

/* Assign values to the special symbols before the linker script is
   read.  */

static void
gld_${EMULATION_NAME}_set_symbols (void)
{
  /* Run through and invent symbols for all the
     names and insert the defaults.  */
  int j;
  lang_statement_list_type *save;

  if (!init[IMAGEBASEOFF].inited)
    {
      if (link_info.relocatable)
	init[IMAGEBASEOFF].value = 0;
      else if (init[DLLOFF].value || (link_info.shared && !link_info.pie))
#ifdef DLL_SUPPORT
	init[IMAGEBASEOFF].value = (pe_enable_auto_image_base) ?
	  compute_dll_image_base (output_filename) : NT_DLL_IMAGE_BASE;
#else
	init[IMAGEBASEOFF].value = NT_DLL_IMAGE_BASE;
#endif
      else
	init[IMAGEBASEOFF].value = NT_EXE_IMAGE_BASE;
    }

  /* Don't do any symbol assignments if this is a relocatable link.  */
  if (link_info.relocatable)
    return;

  /* Glue the assignments into the abs section.  */
  save = stat_ptr;

  stat_ptr = &(abs_output_section->children);

  for (j = 0; init[j].ptr; j++)
    {
      long val = init[j].value;
      lang_assignment_statement_type *rv;
      rv = lang_add_assignment (exp_assop ('=', init[j].symbol,
					   exp_intop (val)));
      if (init[j].size == sizeof (short))
	*(short *) init[j].ptr = val;
      else if (init[j].size == sizeof (int))
	*(int *) init[j].ptr = val;
      else if (init[j].size == sizeof (long))
	*(long *) init[j].ptr = val;
      /* This might be a long long or other special type.  */
      else if (init[j].size == sizeof (bfd_vma))
	*(bfd_vma *) init[j].ptr = val;
      else	abort ();
      if (j == IMAGEBASEOFF)
	image_base_statement = rv;
    }
  /* Restore the pointer.  */
  stat_ptr = save;

  if (pe.FileAlignment >
      pe.SectionAlignment)
    {
      einfo (_("%P: warning, file alignment > section alignment.\n"));
    }
}

/* This is called after the linker script and the command line options
   have been read.  */

static void
gld_${EMULATION_NAME}_after_parse (void)
{
  /* The Windows libraries are designed for the linker to treat the
     entry point as an undefined symbol.  Otherwise, the .obj that
     defines mainCRTStartup is brought in because it is the first
     encountered in libc.lib and it has other symbols in it which will
     be pulled in by the link process.  To avoid this, we act as
     though the user specified -u with the entry point symbol.

     This function is called after the linker script and command line
     options have been read, so at this point we know the right entry
     point.  This function is called before the input files are
     opened, so registering the symbol as undefined will make a
     difference.  */

  if (! link_info.relocatable && entry_symbol.name != NULL)
    ldlang_add_undef (entry_symbol.name);
}

/* pe-dll.c directly accesses pe_data_import_dll,
   so it must be defined outside of #ifdef DLL_SUPPORT.
   Note - this variable is deliberately not initialised.
   This allows it to be treated as a common varaible, and only
   exist in one incarnation in a multiple target enabled linker.  */
char * pe_data_import_dll;

#ifdef DLL_SUPPORT
static struct bfd_link_hash_entry *pe_undef_found_sym;

static bfd_boolean
pe_undef_cdecl_match (struct bfd_link_hash_entry *h, void *inf)
{
  int sl;
  char *string = inf;

  sl = strlen (string);
  if (h->type == bfd_link_hash_defined
      && strncmp (h->root.string, string, sl) == 0
      && h->root.string[sl] == '@')
    {
      pe_undef_found_sym = h;
      return FALSE;
    }
  return TRUE;
}

static void
pe_fixup_stdcalls (void)
{
  static int gave_warning_message = 0;
  struct bfd_link_hash_entry *undef, *sym;

  if (pe_dll_extra_pe_debug)
    printf ("%s\n", __FUNCTION__);

  for (undef = link_info.hash->undefs; undef; undef=undef->u.undef.next)
    if (undef->type == bfd_link_hash_undefined)
      {
	char* at = strchr (undef->root.string, '@');
	int lead_at = (*undef->root.string == '@');
	/* For now, don't try to fixup fastcall symbols.  */

	if (at && !lead_at)
	  {
	    /* The symbol is a stdcall symbol, so let's look for a
	       cdecl symbol with the same name and resolve to that.  */
	    char *cname = xstrdup (undef->root.string /* + lead_at */);
	    at = strchr (cname, '@');
	    *at = 0;
	    sym = bfd_link_hash_lookup (link_info.hash, cname, 0, 0, 1);

	    if (sym && sym->type == bfd_link_hash_defined)
	      {
		undef->type = bfd_link_hash_defined;
		undef->u.def.value = sym->u.def.value;
		undef->u.def.section = sym->u.def.section;

		if (pe_enable_stdcall_fixup == -1)
		  {
		    einfo (_("Warning: resolving %s by linking to %s\n"),
			   undef->root.string, cname);
		    if (! gave_warning_message)
		      {
			gave_warning_message = 1;
			einfo (_("Use --enable-stdcall-fixup to disable these warnings\n"));
			einfo (_("Use --disable-stdcall-fixup to disable these fixups\n"));
		      }
		  }
	      }
	  }
	else
	  {
	    /* The symbol is a cdecl symbol, so we look for stdcall
	       symbols - which means scanning the whole symbol table.  */
	    pe_undef_found_sym = 0;
	    bfd_link_hash_traverse (link_info.hash, pe_undef_cdecl_match,
				    (char *) undef->root.string);
	    sym = pe_undef_found_sym;
	    if (sym)
	      {
		undef->type = bfd_link_hash_defined;
		undef->u.def.value = sym->u.def.value;
		undef->u.def.section = sym->u.def.section;

		if (pe_enable_stdcall_fixup == -1)
		  {
		    einfo (_("Warning: resolving %s by linking to %s\n"),
			   undef->root.string, sym->root.string);
		    if (! gave_warning_message)
		      {
			gave_warning_message = 1;
			einfo (_("Use --enable-stdcall-fixup to disable these warnings\n"));
			einfo (_("Use --disable-stdcall-fixup to disable these fixups\n"));
		      }
		  }
	      }
	  }
      }
}

static int
make_import_fixup (arelent *rel, asection *s)
{
  struct bfd_symbol *sym = *rel->sym_ptr_ptr;
  char addend[4];

  if (pe_dll_extra_pe_debug)
    printf ("arelent: %s@%#lx: add=%li\n", sym->name,
	    (long) rel->address, (long) rel->addend);

  if (! bfd_get_section_contents (s->owner, s, addend, rel->address, sizeof (addend)))
    einfo (_("%C: Cannot get section contents - auto-import exception\n"),
	   s->owner, s, rel->address);

  pe_create_import_fixup (rel, s, bfd_get_32 (s->owner, addend));

  return 1;
}

static void
pe_find_data_imports (void)
{
  struct bfd_link_hash_entry *undef, *sym;

  if (link_info.pei386_auto_import == 0)
    return;

  for (undef = link_info.hash->undefs; undef; undef=undef->u.undef.next)
    {
      if (undef->type == bfd_link_hash_undefined)
	{
	  /* C++ symbols are *long*.  */
	  char buf[4096];

	  if (pe_dll_extra_pe_debug)
	    printf ("%s:%s\n", __FUNCTION__, undef->root.string);

	  sprintf (buf, "__imp_%s", undef->root.string);

	  sym = bfd_link_hash_lookup (link_info.hash, buf, 0, 0, 1);

	  if (sym && sym->type == bfd_link_hash_defined)
	    {
	      bfd *b = sym->u.def.section->owner;
	      asymbol **symbols;
	      int nsyms, symsize, i;

	      if (link_info.pei386_auto_import == -1)
		info_msg (_("Info: resolving %s by linking to %s (auto-import)\n"),
			  undef->root.string, buf);

	      symsize = bfd_get_symtab_upper_bound (b);
	      symbols = (asymbol **) xmalloc (symsize);
	      nsyms = bfd_canonicalize_symtab (b, symbols);

	      for (i = 0; i < nsyms; i++)
		{
		  if (! CONST_STRNEQ (symbols[i]->name, U ("_head_")))
		    continue;

		  if (pe_dll_extra_pe_debug)
		    printf ("->%s\n", symbols[i]->name);

		  pe_data_import_dll = (char*) (symbols[i]->name +
						sizeof (U ("_head_")) - 1);
		  break;
		}

	      pe_walk_relocs_of_symbol (&link_info, undef->root.string,
					make_import_fixup);

	      /* Let's differentiate it somehow from defined.  */
	      undef->type = bfd_link_hash_defweak;
	      /* We replace original name with __imp_ prefixed, this
		 1) may trash memory 2) leads to duplicate symbol generation.
		 Still, IMHO it's better than having name poluted.  */
	      undef->root.string = sym->root.string;
	      undef->u.def.value = sym->u.def.value;
	      undef->u.def.section = sym->u.def.section;
	    }
	}
    }
}

static bfd_boolean
pr_sym (struct bfd_hash_entry *h, void *inf ATTRIBUTE_UNUSED)
{
  if (pe_dll_extra_pe_debug)
    printf ("+%s\n", h->string);

  return TRUE;
}
#endif /* DLL_SUPPORT */


static void
gld_${EMULATION_NAME}_after_open (void)
{
#ifdef DLL_SUPPORT
  if (pe_dll_extra_pe_debug)
    {
      bfd *a;
      struct bfd_link_hash_entry *sym;

      printf ("%s()\n", __FUNCTION__);

      for (sym = link_info.hash->undefs; sym; sym=sym->u.undef.next)
	printf ("-%s\n", sym->root.string);
      bfd_hash_traverse (&link_info.hash->table, pr_sym, NULL);

      for (a = link_info.input_bfds; a; a = a->link_next)
	printf ("*%s\n",a->filename);
    }
#endif

  /* Pass the wacky PE command line options into the output bfd.
     FIXME: This should be done via a function, rather than by
     including an internal BFD header.  */

  if (coff_data (output_bfd) == NULL || coff_data (output_bfd)->pe == 0)
    einfo (_("%F%P: cannot perform PE operations on non PE output file '%B'.\n"), output_bfd);

  pe_data (output_bfd)->pe_opthdr = pe;
  pe_data (output_bfd)->dll = init[DLLOFF].value;
  pe_data (output_bfd)->real_flags |= real_flags;

#ifdef DLL_SUPPORT
  if (pe_enable_stdcall_fixup) /* -1=warn or 1=disable */
    pe_fixup_stdcalls ();

  pe_process_import_defs (output_bfd, & link_info);

  pe_find_data_imports ();

#if defined (TARGET_IS_i386pe) \
    || defined (TARGET_IS_armpe) \
    || defined (TARGET_IS_arm_epoc_pe) \
    || defined (TARGET_IS_arm_wince_pe)
  if (!link_info.relocatable)
    pe_dll_build_sections (output_bfd, &link_info);
  else
    pe_exe_build_sections (output_bfd, &link_info);
#else
  if (link_info.shared)
    pe_dll_build_sections (output_bfd, &link_info);
#endif
#endif /* DLL_SUPPORT */

#if defined(TARGET_IS_armpe) || defined(TARGET_IS_arm_epoc_pe) || defined(TARGET_IS_arm_wince_pe)
  if (strstr (bfd_get_target (output_bfd), "arm") == NULL)
    {
      /* The arm backend needs special fields in the output hash structure.
	 These will only be created if the output format is an arm format,
	 hence we do not support linking and changing output formats at the
	 same time.  Use a link followed by objcopy to change output formats.  */
      einfo ("%F%X%P: error: cannot change output format whilst linking ARM binaries\n");
      return;
    }
  {
    /* Find a BFD that can hold the interworking stubs.  */
    LANG_FOR_EACH_INPUT_STATEMENT (is)
      {
	if (bfd_arm_get_bfd_for_interworking (is->the_bfd, & link_info))
	  break;
      }
  }
#endif

  {
    /* This next chunk of code tries to detect the case where you have
       two import libraries for the same DLL (specifically,
       symbolically linking libm.a and libc.a in cygwin to
       libcygwin.a).  In those cases, it's possible for function
       thunks from the second implib to be used but without the
       head/tail objects, causing an improper import table.  We detect
       those cases and rename the "other" import libraries to match
       the one the head/tail come from, so that the linker will sort
       things nicely and produce a valid import table.  */

    LANG_FOR_EACH_INPUT_STATEMENT (is)
      {
	if (is->the_bfd->my_archive)
	  {
	    int idata2 = 0, reloc_count=0, is_imp = 0;
	    asection *sec;

	    /* See if this is an import library thunk.  */
	    for (sec = is->the_bfd->sections; sec; sec = sec->next)
	      {
		if (strcmp (sec->name, ".idata\$2") == 0)
		  idata2 = 1;
		if (CONST_STRNEQ (sec->name, ".idata\$"))
		  is_imp = 1;
		reloc_count += sec->reloc_count;
	      }

	    if (is_imp && !idata2 && reloc_count)
	      {
		/* It is, look for the reference to head and see if it's
		   from our own library.  */
		for (sec = is->the_bfd->sections; sec; sec = sec->next)
		  {
		    int i;
		    long symsize;
		    long relsize;
		    asymbol **symbols;
		    arelent **relocs;
		    int nrelocs;

		    symsize = bfd_get_symtab_upper_bound (is->the_bfd);
		    if (symsize < 1)
		      break;
		    relsize = bfd_get_reloc_upper_bound (is->the_bfd, sec);
		    if (relsize < 1)
		      break;

		    symbols = (asymbol **) xmalloc (symsize);
		    symsize = bfd_canonicalize_symtab (is->the_bfd, symbols);
		    if (symsize < 0)
		      {
			einfo ("%X%P: unable to process symbols: %E");
			return;
		      }

		    relocs = (arelent **) xmalloc ((size_t) relsize);
		    nrelocs = bfd_canonicalize_reloc (is->the_bfd, sec,
						      relocs, symbols);
		    if (nrelocs < 0)
		      {
			free (relocs);
			einfo ("%X%P: unable to process relocs: %E");
			return;
		      }

		    for (i = 0; i < nrelocs; i++)
		      {
			struct bfd_symbol *s;
			struct bfd_link_hash_entry * blhe;
			char *other_bfd_filename;
			char *n;

			s = (relocs[i]->sym_ptr_ptr)[0];

			if (s->flags & BSF_LOCAL)
			  continue;

			/* Thunk section with reloc to another bfd.  */
			blhe = bfd_link_hash_lookup (link_info.hash,
						     s->name,
						     FALSE, FALSE, TRUE);

			if (blhe == NULL
			    || blhe->type != bfd_link_hash_defined)
			  continue;

			other_bfd_filename
			  = blhe->u.def.section->owner->my_archive
			    ? bfd_get_filename (blhe->u.def.section->owner->my_archive)
			    : bfd_get_filename (blhe->u.def.section->owner);

			if (strcmp (bfd_get_filename (is->the_bfd->my_archive),
				    other_bfd_filename) == 0)
			  continue;

			/* Rename this implib to match the other one.  */
			n = xmalloc (strlen (other_bfd_filename) + 1);
			strcpy (n, other_bfd_filename);
			is->the_bfd->my_archive->filename = n;
		      }

		    free (relocs);
		    /* Note - we do not free the symbols,
		       they are now cached in the BFD.  */
		  }
	      }
	  }
      }
  }

  {
    int is_ms_arch = 0;
    bfd *cur_arch = 0;
    lang_input_statement_type *is2;
    lang_input_statement_type *is3;

    /* Careful - this is a shell script.  Watch those dollar signs! */
    /* Microsoft import libraries have every member named the same,
       and not in the right order for us to link them correctly.  We
       must detect these and rename the members so that they'll link
       correctly.  There are three types of objects: the head, the
       thunks, and the sentinel(s).  The head is easy; it's the one
       with idata2.  We assume that the sentinels won't have relocs,
       and the thunks will.  It's easier than checking the symbol
       table for external references.  */
    LANG_FOR_EACH_INPUT_STATEMENT (is)
      {
	if (is->the_bfd->my_archive)
	  {
	    char *pnt;
	    bfd *arch = is->the_bfd->my_archive;

	    if (cur_arch != arch)
	      {
		cur_arch = arch;
		is_ms_arch = 1;

		for (is3 = is;
		     is3 && is3->the_bfd->my_archive == arch;
		     is3 = (lang_input_statement_type *) is3->next)
		  {
		    /* A MS dynamic import library can also contain static
		       members, so look for the first element with a .dll
		       extension, and use that for the remainder of the
		       comparisons.  */
		    pnt = strrchr (is3->the_bfd->filename, '.');
		    if (pnt != NULL && strcmp (pnt, ".dll") == 0)
		      break;
		  }

		if (is3 == NULL)
		  is_ms_arch = 0;
		else
		  {
		    /* OK, found one.  Now look to see if the remaining
		       (dynamic import) members use the same name.  */
		    for (is2 = is;
			 is2 && is2->the_bfd->my_archive == arch;
			 is2 = (lang_input_statement_type *) is2->next)
		      {
			/* Skip static members, ie anything with a .obj
			   extension.  */
			pnt = strrchr (is2->the_bfd->filename, '.');
			if (pnt != NULL && strcmp (pnt, ".obj") == 0)
			  continue;

			if (strcmp (is3->the_bfd->filename,
				    is2->the_bfd->filename))
			  {
			    is_ms_arch = 0;
			    break;
			  }
		      }
		  }
	      }

	    /* This fragment might have come from an .obj file in a Microsoft
	       import, and not an actual import record. If this is the case,
	       then leave the filename alone.  */
	    pnt = strrchr (is->the_bfd->filename, '.');

	    if (is_ms_arch && (strcmp (pnt, ".dll") == 0))
	      {
		int idata2 = 0, reloc_count=0;
		asection *sec;
		char *new_name, seq;

		for (sec = is->the_bfd->sections; sec; sec = sec->next)
		  {
		    if (strcmp (sec->name, ".idata\$2") == 0)
		      idata2 = 1;
		    reloc_count += sec->reloc_count;
		  }

		if (idata2) /* .idata2 is the TOC */
		  seq = 'a';
		else if (reloc_count > 0) /* thunks */
		  seq = 'b';
		else /* sentinel */
		  seq = 'c';

		new_name = xmalloc (strlen (is->the_bfd->filename) + 3);
		sprintf (new_name, "%s.%c", is->the_bfd->filename, seq);
		is->the_bfd->filename = new_name;

		new_name = xmalloc (strlen (is->filename) + 3);
		sprintf (new_name, "%s.%c", is->filename, seq);
		is->filename = new_name;
	      }
	  }
      }
  }

  {
    /* The following chunk of code tries to identify jump stubs in
       import libraries which are dead code and eliminates them
       from the final link. For each exported symbol <sym>, there
       is a object file in the import library with a .text section
       and several .idata$* sections. The .text section contains the
       symbol definition for <sym> which is a jump stub of the form
       jmp *__imp_<sym>. The .idata$5 contains the symbol definition
       for __imp_<sym> which is the address of the slot for <sym> in
       the import address table. When a symbol is imported explicitly
       using __declspec(dllimport) declaration, the compiler generates
       a reference to __imp_<sym> which directly resolves to the
       symbol in .idata$5, in which case the jump stub code is not
       needed. The following code tries to identify jump stub sections
       in import libraries which are not referred to by anyone and
       marks them for exclusion from the final link.  */
    LANG_FOR_EACH_INPUT_STATEMENT (is)
      {
	if (is->the_bfd->my_archive)
	  {
	    int is_imp = 0;
	    asection *sec, *stub_sec = NULL;

	    /* See if this is an import library thunk.  */
	    for (sec = is->the_bfd->sections; sec; sec = sec->next)
	      {
		if (strncmp (sec->name, ".idata\$", 7) == 0)
		  is_imp = 1;
		/* The section containing the jmp stub has code
		   and has a reloc.  */
		if ((sec->flags & SEC_CODE) && sec->reloc_count)
		  stub_sec = sec;
	      }
   
	    if (is_imp && stub_sec)
	      {
		long symsize;
		asymbol **symbols;
		long src_count;
		struct bfd_link_hash_entry * blhe;

		symsize = bfd_get_symtab_upper_bound (is->the_bfd);
		symbols = xmalloc (symsize);
		symsize = bfd_canonicalize_symtab (is->the_bfd, symbols);

		for (src_count = 0; src_count < symsize; src_count++)
		  {
		    if (symbols[src_count]->section->id == stub_sec->id)
		      {
			/* This symbol belongs to the section containing
			   the stub.  */
			blhe = bfd_link_hash_lookup (link_info.hash,
						     symbols[src_count]->name,
						     FALSE, FALSE, TRUE);
			/* If the symbol in the stub section has no other
			   undefined references, exclude the stub section
			   from the final link.  */
			if (blhe && (blhe->type == bfd_link_hash_defined)
			    && (blhe->u.undef.next == NULL))
			  stub_sec->flags |= SEC_EXCLUDE;
		      }
		  }
		free (symbols);
	      }
	  }
      }
  }
}

static void
gld_${EMULATION_NAME}_before_allocation (void)
{
#ifdef TARGET_IS_ppcpe
  /* Here we rummage through the found bfds to collect toc information.  */
  {
    LANG_FOR_EACH_INPUT_STATEMENT (is)
      {
	if (!ppc_process_before_allocation (is->the_bfd, &link_info))
	  {
	    /* xgettext:c-format */
	    einfo (_("Errors encountered processing file %s\n"), is->filename);
	  }
      }
  }

  /* We have seen it all. Allocate it, and carry on.  */
  ppc_allocate_toc_section (&link_info);
#endif /* TARGET_IS_ppcpe */

#if defined(TARGET_IS_armpe) || defined(TARGET_IS_arm_epoc_pe) || defined(TARGET_IS_arm_wince_pe)
  /* FIXME: we should be able to set the size of the interworking stub
     section.

     Here we rummage through the found bfds to collect glue
     information.  FIXME: should this be based on a command line
     option?  krk@cygnus.com.  */
  {
    LANG_FOR_EACH_INPUT_STATEMENT (is)
      {
	if (! bfd_arm_process_before_allocation
	    (is->the_bfd, & link_info, support_old_code))
	  {
	    /* xgettext:c-format */
	    einfo (_("Errors encountered processing file %s for interworking"),
		   is->filename);
	  }
      }
  }

  /* We have seen it all. Allocate it, and carry on.  */
  bfd_arm_allocate_interworking_sections (& link_info);
#endif /* TARGET_IS_armpe || TARGET_IS_arm_epoc_pe || TARGET_IS_arm_wince_pe */

  before_allocation_default ();
}

#ifdef DLL_SUPPORT
/* This is called when an input file isn't recognized as a BFD.  We
   check here for .DEF files and pull them in automatically.  */

static int
saw_option (char *option)
{
  int i;

  for (i = 0; init[i].ptr; i++)
    if (strcmp (init[i].symbol, option) == 0)
      return init[i].inited;
  return 0;
}
#endif /* DLL_SUPPORT */

static bfd_boolean
gld_${EMULATION_NAME}_unrecognized_file (lang_input_statement_type *entry ATTRIBUTE_UNUSED)
{
#ifdef DLL_SUPPORT
  const char *ext = entry->filename + strlen (entry->filename) - 4;

  if (strcmp (ext, ".def") == 0 || strcmp (ext, ".DEF") == 0)
    {
      pe_def_file = def_file_parse (entry->filename, pe_def_file);

      if (pe_def_file)
	{
	  int i, buflen=0, len;
	  char *buf;

	  for (i = 0; i < pe_def_file->num_exports; i++)
	    {
	      len = strlen (pe_def_file->exports[i].internal_name);
	      if (buflen < len + 2)
		buflen = len + 2;
	    }

	  buf = (char *) xmalloc (buflen);

	  for (i = 0; i < pe_def_file->num_exports; i++)
	    {
	      struct bfd_link_hash_entry *h;

	      sprintf (buf, "%s%s", U (""), pe_def_file->exports[i].internal_name);

	      h = bfd_link_hash_lookup (link_info.hash, buf, TRUE, TRUE, TRUE);
	      if (h == (struct bfd_link_hash_entry *) NULL)
		einfo (_("%P%F: bfd_link_hash_lookup failed: %E\n"));
	      if (h->type == bfd_link_hash_new)
		{
		  h->type = bfd_link_hash_undefined;
		  h->u.undef.abfd = NULL;
		  bfd_link_add_undef (link_info.hash, h);
		}
	    }
	  free (buf);

	  /* def_file_print (stdout, pe_def_file); */
	  if (pe_def_file->is_dll == 1)
	    link_info.shared = 1;

	  if (pe_def_file->base_address != (bfd_vma)(-1))
	    {
	      pe.ImageBase =
		pe_data (output_bfd)->pe_opthdr.ImageBase =
		init[IMAGEBASEOFF].value = pe_def_file->base_address;
	      init[IMAGEBASEOFF].inited = 1;
	      if (image_base_statement)
		image_base_statement->exp =
		  exp_assop ('=', "__image_base__", exp_intop (pe.ImageBase));
	    }

	  if (pe_def_file->stack_reserve != -1
	      && ! saw_option ("__size_of_stack_reserve__"))
	    {
	      pe.SizeOfStackReserve = pe_def_file->stack_reserve;
	      if (pe_def_file->stack_commit != -1)
		pe.SizeOfStackCommit = pe_def_file->stack_commit;
	    }
	  if (pe_def_file->heap_reserve != -1
	      && ! saw_option ("__size_of_heap_reserve__"))
	    {
	      pe.SizeOfHeapReserve = pe_def_file->heap_reserve;
	      if (pe_def_file->heap_commit != -1)
		pe.SizeOfHeapCommit = pe_def_file->heap_commit;
	    }
	  return TRUE;
	}
    }
#endif
  return FALSE;
}

static bfd_boolean
gld_${EMULATION_NAME}_recognized_file (lang_input_statement_type *entry ATTRIBUTE_UNUSED)
{
#ifdef DLL_SUPPORT
#ifdef TARGET_IS_i386pe
  pe_dll_id_target ("pei-i386");
#endif
#ifdef TARGET_IS_shpe
  pe_dll_id_target ("pei-shl");
#endif
#ifdef TARGET_IS_mipspe
  pe_dll_id_target ("pei-mips");
#endif
#ifdef TARGET_IS_armpe
  pe_dll_id_target ("pei-arm-little");
#endif
#ifdef TARGET_IS_arm_epoc_pe
  pe_dll_id_target ("epoc-pei-arm-little");
#endif
#ifdef TARGET_IS_arm_wince_pe
  pe_dll_id_target ("pei-arm-wince-little");
#endif
  if (pe_bfd_is_dll (entry->the_bfd))
    return pe_implied_import_dll (entry->filename);
#endif
  return FALSE;
}

static void
gld_${EMULATION_NAME}_finish (void)
{
#if defined(TARGET_IS_armpe) || defined(TARGET_IS_arm_epoc_pe) || defined(TARGET_IS_arm_wince_pe)
  struct bfd_link_hash_entry * h;

  if (thumb_entry_symbol != NULL)
    {
      h = bfd_link_hash_lookup (link_info.hash, thumb_entry_symbol,
				FALSE, FALSE, TRUE);

      if (h != (struct bfd_link_hash_entry *) NULL
	  && (h->type == bfd_link_hash_defined
	      || h->type == bfd_link_hash_defweak)
	  && h->u.def.section->output_section != NULL)
	{
	  static char buffer[32];
	  bfd_vma val;

	  /* Special procesing is required for a Thumb entry symbol.  The
	     bottom bit of its address must be set.  */
	  val = (h->u.def.value
		 + bfd_get_section_vma (output_bfd,
					h->u.def.section->output_section)
		 + h->u.def.section->output_offset);

	  val |= 1;

	  /* Now convert this value into a string and store it in entry_symbol
	     where the lang_finish() function will pick it up.  */
	  buffer[0] = '0';
	  buffer[1] = 'x';

	  sprintf_vma (buffer + 2, val);

	  if (entry_symbol.name != NULL && entry_from_cmdline)
	    einfo (_("%P: warning: '--thumb-entry %s' is overriding '-e %s'\n"),
		   thumb_entry_symbol, entry_symbol.name);
	  entry_symbol.name = buffer;
	}
      else
	einfo (_("%P: warning: connot find thumb start symbol %s\n"), thumb_entry_symbol);
    }
#endif /* defined(TARGET_IS_armpe) || defined(TARGET_IS_arm_epoc_pe) || defined(TARGET_IS_arm_wince_pe) */

  finish_default ();

#ifdef DLL_SUPPORT
  if (link_info.shared
#if !defined(TARGET_IS_shpe) && !defined(TARGET_IS_mipspe)
    || (!link_info.relocatable && pe_def_file->num_exports != 0)
#endif
    )
    {
      pe_dll_fill_sections (output_bfd, &link_info);
      if (pe_implib_filename)
	pe_dll_generate_implib (pe_def_file, pe_implib_filename);
    }
#if defined(TARGET_IS_shpe) || defined(TARGET_IS_mipspe)
  /* ARM doesn't need relocs.  */
  else
    {
      pe_exe_fill_sections (output_bfd, &link_info);
    }
#endif

  if (pe_out_def_filename)
    pe_dll_generate_def_file (pe_out_def_filename);
#endif /* DLL_SUPPORT */

  /* I don't know where .idata gets set as code, but it shouldn't be.  */
  {
    asection *asec = bfd_get_section_by_name (output_bfd, ".idata");

    if (asec)
      {
	asec->flags &= ~SEC_CODE;
	asec->flags |= SEC_DATA;
      }
  }
}


/* Place an orphan section.

   We use this to put sections in a reasonable place in the file, and
   to ensure that they are aligned as required.

   We handle grouped sections here as well.  A section named .foo$nn
   goes into the output section .foo.  All grouped sections are sorted
   by name.

   Grouped sections for the default sections are handled by the
   default linker script using wildcards, and are sorted by
   sort_sections.  */

static bfd_boolean
gld_${EMULATION_NAME}_place_orphan (asection *s)
{
  const char *secname;
  const char *orig_secname;
  char *dollar = NULL;
  lang_output_section_statement_type *os;
  lang_statement_list_type add_child;
  lang_statement_union_type **pl;

  secname = bfd_get_section_name (s->owner, s);

  /* Look through the script to see where to place this section.  */
  orig_secname = secname;
  if (!link_info.relocatable
      && (dollar = strchr (secname, '$')) != NULL)
    {
      size_t len = dollar - orig_secname;
      char *newname = xmalloc (len + 1);
      memcpy (newname, orig_secname, len);
      newname[len] = '\0';
      secname = newname;
    }

  os = lang_output_section_find (secname);

  lang_list_init (&add_child);

  if (os != NULL
      && (os->bfd_section == NULL
	  || os->bfd_section->flags == 0
	  || ((s->flags ^ os->bfd_section->flags)
	      & (SEC_LOAD | SEC_ALLOC)) == 0))
    {
      /* We already have an output section statement with this
	 name, and its bfd section, if any, has compatible flags.
	 If the section already exists but does not have any flags set,
	 then it has been created by the linker, probably as a result of
	 a --section-start command line switch.  */
      lang_add_section (&add_child, s, os);
    }
  else
    {
      static struct orphan_save hold[] =
	{
	  { ".text",
	    SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_READONLY | SEC_CODE,
	    0, 0, 0, 0 },
	  { ".rdata",
	    SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_READONLY | SEC_DATA,
	    0, 0, 0, 0 },
	  { ".data",
	    SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_DATA,
	    0, 0, 0, 0 },
	  { ".bss",
	    SEC_ALLOC,
	    0, 0, 0, 0 }
	};
      enum orphan_save_index
	{
	  orphan_text = 0,
	  orphan_rodata,
	  orphan_data,
	  orphan_bss
	};
      static int orphan_init_done = 0;
      struct orphan_save *place;
      lang_output_section_statement_type *after;
      etree_type *address;

      if (!orphan_init_done)
	{
	  struct orphan_save *ho;
	  for (ho = hold; ho < hold + sizeof (hold) / sizeof (hold[0]); ++ho)
	    if (ho->name != NULL)
	      {
		ho->os = lang_output_section_find (ho->name);
		if (ho->os != NULL && ho->os->flags == 0)
		  ho->os->flags = ho->flags;
	      }
	  orphan_init_done = 1;
	}

      /* Try to put the new output section in a reasonable place based
	 on the section name and section flags.  */

      place = NULL;
      if ((s->flags & SEC_ALLOC) == 0)
	;
      else if ((s->flags & (SEC_LOAD | SEC_HAS_CONTENTS)) == 0)
	place = &hold[orphan_bss];
      else if ((s->flags & SEC_READONLY) == 0)
	place = &hold[orphan_data];
      else if ((s->flags & SEC_CODE) == 0)
	place = &hold[orphan_rodata];
      else
	place = &hold[orphan_text];

      after = NULL;
      if (place != NULL)
	{
	  if (place->os == NULL)
	    place->os = lang_output_section_find (place->name);
	  after = place->os;
	  if (after == NULL)
	    after = lang_output_section_find_by_flags (s, &place->os, NULL);
	  if (after == NULL)
	    /* *ABS* is always the first output section statement.  */
	    after = (&lang_output_section_statement.head
		     ->output_section_statement);
	}

      /* Choose a unique name for the section.  This will be needed if the
	 same section name appears in the input file with different
	 loadable or allocatable characteristics.  */
      if (bfd_get_section_by_name (output_bfd, secname) != NULL)
	{
	  static int count = 1;
	  secname = bfd_get_unique_section_name (output_bfd, secname, &count);
	  if (secname == NULL)
	    einfo ("%F%P: place_orphan failed: %E\n");
	}

      /* All sections in an executable must be aligned to a page boundary.  */
      address = exp_unop (ALIGN_K, exp_nameop (NAME, "__section_alignment__"));
      os = lang_insert_orphan (s, secname, after, place, address, &add_child);
    }

  /* If the section name has a '\$', sort it with the other '\$'
     sections.  */
  for (pl = &os->children.head; *pl != NULL; pl = &(*pl)->header.next)
    {
      lang_input_section_type *ls;
      const char *lname;

      if ((*pl)->header.type != lang_input_section_enum)
	continue;

      ls = &(*pl)->input_section;

      lname = bfd_get_section_name (ls->section->owner, ls->section);
      if (strchr (lname, '\$') != NULL
	  && (dollar == NULL || strcmp (orig_secname, lname) < 0))
	break;
    }

  if (add_child.head != NULL)
    {
      *add_child.tail = *pl;
      *pl = add_child.head;
    }

  return TRUE;
}

static bfd_boolean
gld_${EMULATION_NAME}_open_dynamic_archive
  (const char *arch ATTRIBUTE_UNUSED, search_dirs_type *search,
   lang_input_statement_type *entry)
{
  static const struct
    {
      const char * format;
      bfd_boolean use_prefix;
    }
  libname_fmt [] =
    {
      /* Preferred explicit import library for dll's.  */
      { "lib%s.dll.a", FALSE },
      /* Alternate explicit import library for dll's.  */
      { "%s.dll.a", FALSE },
      /* "libfoo.a" could be either an import lib or a static lib.
          For backwards compatibility, libfoo.a needs to precede
          libfoo.dll and foo.dll in the search.  */
      { "lib%s.a", FALSE },
      /* The 'native' spelling of an import lib name is "foo.lib".  */  	
      { "%s.lib", FALSE },
#ifdef DLL_SUPPORT
      /* Try "<prefix>foo.dll" (preferred dll name, if specified).  */
      {	"%s%s.dll", TRUE },
#endif
      /* Try "libfoo.dll" (default preferred dll name).  */
      {	"lib%s.dll", FALSE },
      /* Finally try 'native' dll name "foo.dll".  */
      {  "%s.dll", FALSE },
      /* Note: If adding more formats to this table, make sure to check to
	 see if their length is longer than libname_fmt[0].format, and if
	 so, update the call to xmalloc() below.  */
      { NULL, FALSE }
    };
  static unsigned int format_max_len = 0;
  const char * filename;
  char * full_string;
  char * base_string;
  unsigned int i;


  if (! entry->is_archive)
    return FALSE;

  filename = entry->filename;

  if (format_max_len == 0)
    /* We need to allow space in the memory that we are going to allocate
       for the characters in the format string.  Since the format array is
       static we only need to calculate this information once.  In theory
       this value could also be computed statically, but this introduces
       the possibility for a discrepancy and hence a possible memory
       corruption.  The lengths we compute here will be too long because
       they will include any formating characters (%s) in the strings, but
       this will not matter.  */
    for (i = 0; libname_fmt[i].format; i++)
      if (format_max_len < strlen (libname_fmt[i].format))
	format_max_len = strlen (libname_fmt[i].format);

  full_string = xmalloc (strlen (search->name)
			 + strlen (filename)
			 + format_max_len
#ifdef DLL_SUPPORT
			 + (pe_dll_search_prefix
			    ? strlen (pe_dll_search_prefix) : 0)
#endif
			 /* Allow for the terminating NUL and for the path
			    separator character that is inserted between
			    search->name and the start of the format string.  */
			 + 2);

  sprintf (full_string, "%s/", search->name);
  base_string = full_string + strlen (full_string);

  for (i = 0; libname_fmt[i].format; i++)
    {
#ifdef DLL_SUPPORT 
      if (libname_fmt[i].use_prefix)
	{
	  if (!pe_dll_search_prefix)
	    continue;
	  sprintf (base_string, libname_fmt[i].format, pe_dll_search_prefix, filename);
	}
      else
#endif
	sprintf (base_string, libname_fmt[i].format, filename);

      if (ldfile_try_open_bfd (full_string, entry))
	break;
    }

  if (!libname_fmt[i].format)
    {
      free (full_string);
      return FALSE;
    }

  entry->filename = full_string;

  return TRUE;
}

static int
gld_${EMULATION_NAME}_find_potential_libraries
  (char *name, lang_input_statement_type *entry)
{
  return ldfile_open_file_search (name, entry, "", ".lib");
}

static char *
gld_${EMULATION_NAME}_get_script (int *isfile)
EOF
# Scripts compiled in.
# sed commands to quote an ld script as a C string.
sc="-f stringify.sed"

cat >>e${EMULATION_NAME}.c <<EOF
{
  *isfile = 0;

  if (link_info.relocatable && config.build_constructors)
    return
EOF
sed $sc ldscripts/${EMULATION_NAME}.xu			>> e${EMULATION_NAME}.c
echo '  ; else if (link_info.relocatable) return'	>> e${EMULATION_NAME}.c
sed $sc ldscripts/${EMULATION_NAME}.xr			>> e${EMULATION_NAME}.c
echo '  ; else if (!config.text_read_only) return'	>> e${EMULATION_NAME}.c
sed $sc ldscripts/${EMULATION_NAME}.xbn			>> e${EMULATION_NAME}.c
echo '  ; else if (!config.magic_demand_paged) return'	>> e${EMULATION_NAME}.c
sed $sc ldscripts/${EMULATION_NAME}.xn			>> e${EMULATION_NAME}.c
echo '  ; else return'					>> e${EMULATION_NAME}.c
sed $sc ldscripts/${EMULATION_NAME}.x			>> e${EMULATION_NAME}.c
echo '; }'						>> e${EMULATION_NAME}.c

cat >>e${EMULATION_NAME}.c <<EOF


struct ld_emulation_xfer_struct ld_${EMULATION_NAME}_emulation =
{
  gld_${EMULATION_NAME}_before_parse,
  syslib_default,
  hll_default,
  gld_${EMULATION_NAME}_after_parse,
  gld_${EMULATION_NAME}_after_open,
  after_allocation_default,
  set_output_arch_default,
  ldemul_default_target,
  gld_${EMULATION_NAME}_before_allocation,
  gld_${EMULATION_NAME}_get_script,
  "${EMULATION_NAME}",
  "${OUTPUT_FORMAT}",
  gld_${EMULATION_NAME}_finish,
  NULL, /* Create output section statements.  */
  gld_${EMULATION_NAME}_open_dynamic_archive,
  gld_${EMULATION_NAME}_place_orphan,
  gld_${EMULATION_NAME}_set_symbols,
  NULL, /* parse_args */
  gld${EMULATION_NAME}_add_options,
  gld${EMULATION_NAME}_handle_option,
  gld_${EMULATION_NAME}_unrecognized_file,
  gld_${EMULATION_NAME}_list_options,
  gld_${EMULATION_NAME}_recognized_file,
  gld_${EMULATION_NAME}_find_potential_libraries,
  NULL	/* new_vers_pattern.  */
};
EOF
