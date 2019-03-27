/* Definitions for Intel x86 running BeOS
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2004
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */


#define TARGET_VERSION fprintf (stderr, " (i386 BeOS/ELF)");

/* Change debugging to Dwarf2.  */
#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

/* The SVR4 ABI for the i386 says that records and unions are returned
   in memory.  */
#undef DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1

#undef ASM_COMMENT_START
#define ASM_COMMENT_START " #"

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n) \
  (TARGET_64BIT ? dbx64_register_map[n] : svr4_dbx_register_map[n])

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */

#undef MCOUNT_NAME
#define MCOUNT_NAME "mcount"

#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"
 
#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"
  
#undef WCHAR_TYPE
#define WCHAR_TYPE "short unsigned int"
   
#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16

#define TARGET_DECLSPEC 1

#define TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
	builtin_define ("__BEOS__");					\
	builtin_define ("__INTEL__");					\
	builtin_define ("_X86_");					\
	builtin_define ("__stdcall=__attribute__((__stdcall__))");	\
	builtin_define ("__cdecl=__attribute__((__cdecl__))");		\
	builtin_assert ("system=beos");					\
    }									\
  while (0)
    
/* BeOS uses lots of multichars, so don't warn about them unless the
   user explicitly asks for the warnings with -Wmultichar.  Note that
   CC1_SPEC is used for both cc1 and cc1plus.  */

#undef CC1_SPEC
#define CC1_SPEC "%{!no-fpic:%{!fno-pic:%{!fno-pie:%{!fpie:%{!fPIC:%{!fPIE:-fpic}}}}}} %{!Wmultichar: -Wno-multichar} %(cc1_cpu) %{profile:-p}"

#undef CC1PLUS_SPEC
#define CC1PLUS_SPEC "%{!Wctor-dtor-privacy:-Wno-ctor-dtor-privacy}"

/* Provide a LINK_SPEC appropriate for BeOS.  Here we provide support
   for the special GCC options -static and -shared, which allow us to
   link things in one of these three modes by applying the appropriate
   combinations of options at link-time.  */

/* If ELF is the default format, we should not use /lib/elf.  */

#undef	LINK_SPEC
#define LINK_SPEC "%{!o*:-o %b} -m elf_i386_be -shared -Bsymbolic %{nostart:-e 0}"

/* Provide start and end file specs appropriate to glibc.  */

/* LIB_SPEC for BeOS */
#undef LIB_SPEC
#define LIB_SPEC "-lnet -lroot"

/* gcc runtime lib is built into libroot.so on BeOS */
/* ??? This is gonna be lovely when the next release of gcc has 
   some new symbol in, so that links start failing.  */
#undef LIBGCC_SPEC
#define LIBGCC_SPEC ""

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "crti.o%s crtbegin.o%s %{!nostart:start_dyn.o%s} init_term_dyn.o%s %{p:i386-mcount.o%s}"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s crtn.o%s"

/* A C statement (sans semicolon) to output to the stdio stream
   FILE the assembler definition of uninitialized global DECL named
   NAME whose size is SIZE bytes and alignment is ALIGN bytes.
   Try to use asm_output_aligned_bss to implement this macro.  */

#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)

/* A C statement to output to the stdio stream FILE an assembler
   command to advance the location counter to a multiple of 1<<LOG
   bytes if it is within MAX_SKIP bytes.

   This is used to align code labels according to Intel recommendations.  */

#ifdef HAVE_GAS_MAX_SKIP_P2ALIGN
#define ASM_OUTPUT_MAX_SKIP_ALIGN(FILE,LOG,MAX_SKIP) \
  if ((LOG)!=0) \
    if ((MAX_SKIP)==0) fprintf ((FILE), "\t.p2align %d\n", (LOG)); \
    else fprintf ((FILE), "\t.p2align %d,,%d\n", (LOG), (MAX_SKIP))
#endif

/* For native compiler, use standard BeOS include file search paths
   rooted in /boot/develop/headers.  For a cross compiler, don't
   expect the host to use the BeOS directory scheme, and instead look
   for the BeOS include files relative to TOOL_INCLUDE_DIR.  Yes, we
   use ANSI string concatenation here (FIXME) */

#ifndef CROSS_DIRECTORY_STRUCTURE
#undef INCLUDE_DEFAULTS
#define INCLUDE_DEFAULTS \
    { \
    { GPLUSPLUS_INCLUDE_DIR, "G++", 1, 1 },\
    { GCC_INCLUDE_DIR, "GCC", 0, 0 },\
    { TOOL_INCLUDE_DIR, "BINUTILS", 0, 1}, \
    { "/boot/develop/headers/be/add-ons/graphics", 0, 0, 0 },\
    { "/boot/develop/headers/be/devel", 0, 0, 0 },\
    { "/boot/develop/headers/be/translation", 0, 0, 0 },\
    { "/boot/develop/headers/be/mail", 0, 0, 0 },\
    { "/boot/develop/headers/gnu", 0, 0, 0 },\
    { "/boot/develop/headers/be/drivers", 0, 0, 0 },\
    { "/boot/develop/headers/be/opengl", 0, 0, 0 },\
    { "/boot/develop/headers/be/game", 0, 0, 0 },\
    { "/boot/develop/headers/be/support", 0, 0, 0 },\
    { "/boot/develop/headers/be/storage", 0, 0, 0 },\
    { "/boot/develop/headers/be/kernel", 0, 0, 0 },\
    { "/boot/develop/headers/be/net", 0, 0, 0 },\
    { "/boot/develop/headers/be/midi", 0, 0, 0 },\
    { "/boot/develop/headers/be/midi2", 0, 0, 0 },\
    { "/boot/develop/headers/be/media", 0, 0, 0 },\
    { "/boot/develop/headers/be/interface", 0, 0, 0 },\
    { "/boot/develop/headers/be/device", 0, 0, 0 },\
    { "/boot/develop/headers/be/app", 0, 0, 0 },\
    { "/boot/develop/headers/be/precompiled", 0, 0, 0 },\
    { "/boot/develop/headers/be/add-ons/input_server", 0, 0, 0 },\
    { "/boot/develop/headers/be/add-ons/net_server", 0, 0, 0 },\
    { "/boot/develop/headers/be/add-ons/screen_saver", 0, 0, 0 },\
    { "/boot/develop/headers/be/add-ons/tracker", 0, 0, 0 },\
    { "/boot/develop/headers/be/be_apps/Deskbar", 0, 0, 0 },\
    { "/boot/develop/headers/be/be_apps/NetPositive", 0, 0, 0 },\
    { "/boot/develop/headers/be/be_apps/Tracker", 0, 0, 0 },\
    { "/boot/develop/headers/be/drivers/tty", 0, 0, 0 },\
    { "/boot/develop/headers/be/net/netinet", 0, 0, 0 },\
    { "/boot/develop/headers/be/storage", 0, 0, 0 },\
    { "/boot/develop/headers/be", 0, 0, 0 },\
    { "/boot/develop/headers/cpp", 0, 0, 0 },\
    { "/boot/develop/headers/posix", 0, 0, 0 },\
    { "/boot/develop/headers", 0, 0, 0 }, \
    { 0, 0, 0, 0 } \
    }
#else /* CROSS_DIRECTORY_STRUCTURE */
#undef	INCLUDE_DEFAULTS
#define INCLUDE_DEFAULTS				\
    { \
    { GPLUSPLUS_INCLUDE_DIR, "G++", 1, 1 },\
    { GCC_INCLUDE_DIR, "GCC", 0, 0 },\
    { TOOL_INCLUDE_DIR, "BINUTILS", 0, 1}, \
    { CROSS_INCLUDE_DIR "/be/add-ons/graphics", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/devel", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/translation", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/mail", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/gnu", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/drivers", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/opengl", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/game", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/support", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/storage", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/kernel", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/net", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/midi", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/midi2", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/media", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/interface", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/device", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/app", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/precompiled", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/add-ons/input_server", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/add-ons/net_server", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/add-ons/screen_saver", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/add-ons/tracker", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/be_apps/Deskbar", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/be_apps/NetPositive", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/be_apps/Tracker", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/drivers/tty", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/net/netinet", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be/storage", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/be", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/cpp", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR "/posix", 0, 0, 0 },\
    { CROSS_INCLUDE_DIR , 0, 0, 0 }, \
    { 0, 0, 0, 0 } \
    }
#endif

/* Whee.  LIBRARY_PATH is Be's LD_LIBRARY_PATH, which of course will
   cause nasty problems if we override it.  */
#define LIBRARY_PATH_ENV        "BELIBRARIES"

/* BeOS doesn't have a separate math library.  */
#define MATH_LIBRARY ""

/* BeOS headers are C++-aware (and often use C++).  */
#define NO_IMPLICIT_EXTERN_C

/* BeOS uses explicit import from shared libraries.  */
#define MULTIPLE_SYMBOL_SPACES 1
