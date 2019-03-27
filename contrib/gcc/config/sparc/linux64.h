/* Definitions for 64-bit SPARC running Linux-based GNU systems with ELF.
   Copyright 1996, 1997, 1998, 2000, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by David S. Miller (davem@caip.rutgers.edu)

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

#define TARGET_OS_CPP_BUILTINS()			\
  do							\
    {							\
      builtin_define_std ("unix");			\
      builtin_define_std ("linux");			\
      builtin_define ("_LONGLONG");			\
      builtin_define ("__gnu_linux__");			\
      builtin_assert ("system=linux");			\
      builtin_assert ("system=unix");			\
      builtin_assert ("system=posix");			\
      if (TARGET_ARCH32 && TARGET_LONG_DOUBLE_128)	\
	builtin_define ("__LONG_DOUBLE_128__");		\
    }							\
  while (0)

/* Don't assume anything about the header files.  */
#define NO_IMPLICIT_EXTERN_C

#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX

#if TARGET_CPU_DEFAULT == TARGET_CPU_v9 \
    || TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc \
    || TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc3 \
    || TARGET_CPU_DEFAULT == TARGET_CPU_niagara
/* A 64 bit v9 compiler with stack-bias,
   in a Medium/Low code model environment.  */

#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
  (MASK_V9 + MASK_PTR64 + MASK_64BIT /* + MASK_HARD_QUAD */ \
   + MASK_STACK_BIAS + MASK_APP_REGS + MASK_FPU + MASK_LONG_DOUBLE_128)
#endif

#undef ASM_CPU_DEFAULT_SPEC
#define ASM_CPU_DEFAULT_SPEC "-Av9a"

/* Provide a STARTFILE_SPEC appropriate for GNU/Linux.  Here we add
   the GNU/Linux magical crtbegin.o file (see crtstuff.c) which
   provides part of the support for getting C++ file-scope static
   object constructed before entering `main'.  */
   
#undef  STARTFILE_SPEC

#ifdef HAVE_LD_PIE
#define STARTFILE_SPEC \
  "%{!shared:%{pg|p:gcrt1.o%s;pie:Scrt1.o%s;:crt1.o%s}}\
   crti.o%s %{static:crtbeginT.o%s;shared|pie:crtbeginS.o%s;:crtbeginS.o%s}"
#else
#define STARTFILE_SPEC \
  "%{!shared:%{pg|p:gcrt1.o%s;:crt1.o%s}}\
   crti.o%s %{static:crtbeginT.o%s;shared|pie:crtbeginS.o%s;:crtbeginS.o%s}"
#endif

/* Provide a ENDFILE_SPEC appropriate for GNU/Linux.  Here we tack on
   the GNU/Linux magical crtend.o file (see crtstuff.c) which
   provides part of the support for getting C++ file-scope static
   object constructed before entering `main', followed by a normal
   GNU/Linux "finalizer" file, `crtn.o'.  */

#undef  ENDFILE_SPEC

#define ENDFILE_SPEC \
  "%{shared|pie:crtendS.o%s;:crtend.o%s} crtn.o%s\
   %{ffast-math|funsafe-math-optimizations:crtfastmath.o%s}"

/* The GNU C++ standard library requires that these macros be defined.  */
#undef CPLUSPLUS_CPP_SPEC
#define CPLUSPLUS_CPP_SPEC "-D_GNU_SOURCE %(cpp)"

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (sparc64 GNU/Linux with ELF)");

/* The default code model.  */
#undef SPARC_DEFAULT_CMODEL
#define SPARC_DEFAULT_CMODEL CM_MEDLOW

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* Define for support of TFmode long double.
   SPARC ABI says that long double is 4 words.  */
#undef LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE (TARGET_LONG_DOUBLE_128 ? 128 : 64)

/* Define this to set long double type size to use in libgcc2.c, which can
   not depend on target_flags.  */
#if defined(__arch64__) || defined(__LONG_DOUBLE_128__)
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 128
#else
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 64
#endif

#undef CPP_SUBTARGET_SPEC
#define CPP_SUBTARGET_SPEC "\
%{posix:-D_POSIX_SOURCE} \
%{pthread:-D_REENTRANT} \
"

#undef LIB_SPEC
#define LIB_SPEC \
  "%{pthread:-lpthread} \
   %{shared:-lc} \
   %{!shared: %{mieee-fp:-lieee} %{profile:-lc_p}%{!profile:-lc}}"

/* Provide a LINK_SPEC appropriate for GNU/Linux.  Here we provide support
   for the special GCC options -static and -shared, which allow us to
   link things in one of these three modes by applying the appropriate
   combinations of options at link-time. We like to support here for
   as many of the other GNU linker options as possible. But I don't
   have the time to search for those flags. I am sure how to add
   support for -soname shared_object_name. H.J.

   I took out %{v:%{!V:-V}}. It is too much :-(. They can use
   -Wl,-V.

   When the -shared link option is used a final link is not being
   done.  */

/* If ELF is the default format, we should not use /lib/elf.  */

#define GLIBC_DYNAMIC_LINKER32 "/lib/ld-linux.so.2"
#define GLIBC_DYNAMIC_LINKER64 "/lib64/ld-linux.so.2"
#define UCLIBC_DYNAMIC_LINKER32 "/lib/ld-uClibc.so.0"
#define UCLIBC_DYNAMIC_LINKER64 "/lib/ld64-uClibc.so.0"
#if UCLIBC_DEFAULT
#define CHOOSE_DYNAMIC_LINKER(G, U) "%{mglibc:%{muclibc:%e-mglibc and -muclibc used together}" G ";:" U "}"
#else
#define CHOOSE_DYNAMIC_LINKER(G, U) "%{muclibc:%{mglibc:%e-mglibc and -muclibc used together}" U ";:" G "}"
#endif
#define LINUX_DYNAMIC_LINKER32 \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER32, UCLIBC_DYNAMIC_LINKER32)
#define LINUX_DYNAMIC_LINKER64 \
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER64, UCLIBC_DYNAMIC_LINKER64)

#ifdef SPARC_BI_ARCH

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
  { "link_arch32",       LINK_ARCH32_SPEC },              \
  { "link_arch64",       LINK_ARCH64_SPEC },              \
  { "link_arch_default", LINK_ARCH_DEFAULT_SPEC },	  \
  { "link_arch",	 LINK_ARCH_SPEC },

#define LINK_ARCH32_SPEC "-m elf32_sparc -Y P,/usr/lib %{shared:-shared} \
  %{!shared: \
    %{!ibcs: \
      %{!static: \
        %{rdynamic:-export-dynamic} \
        %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER32 "}} \
        %{static:-static}}} \
"

#define LINK_ARCH64_SPEC "-m elf64_sparc -Y P,/usr/lib64 %{shared:-shared} \
  %{!shared: \
    %{!ibcs: \
      %{!static: \
        %{rdynamic:-export-dynamic} \
        %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER64 "}} \
        %{static:-static}}} \
"

#define LINK_ARCH_SPEC "\
%{m32:%(link_arch32)} \
%{m64:%(link_arch64)} \
%{!m32:%{!m64:%(link_arch_default)}} \
"

#define LINK_ARCH_DEFAULT_SPEC \
(DEFAULT_ARCH32_P ? LINK_ARCH32_SPEC : LINK_ARCH64_SPEC)

#undef  LINK_SPEC
#define LINK_SPEC "\
%(link_arch) \
%{mlittle-endian:-EL} \
%{!mno-relax:%{!r:-relax}} \
"

#undef	CC1_SPEC
#if DEFAULT_ARCH32_P
#define CC1_SPEC "\
%{sun4:} %{target:} \
%{mcypress:-mcpu=cypress} \
%{msparclite:-mcpu=sparclite} %{mf930:-mcpu=f930} %{mf934:-mcpu=f934} \
%{mv8:-mcpu=v8} %{msupersparc:-mcpu=supersparc} \
%{m32:%{m64:%emay not use both -m32 and -m64}} \
%{m64:-mptr64 -mstack-bias -mlong-double-128 \
  %{!mcpu*:%{!mcypress:%{!msparclite:%{!mf930:%{!mf934:%{!mv8:%{!msupersparc:-mcpu=ultrasparc}}}}}}} \
  %{!mno-vis:%{!mcpu=v9:-mvis}}} \
"
#else
#define CC1_SPEC "\
%{sun4:} %{target:} \
%{mcypress:-mcpu=cypress} \
%{msparclite:-mcpu=sparclite} %{mf930:-mcpu=f930} %{mf934:-mcpu=f934} \
%{mv8:-mcpu=v8} %{msupersparc:-mcpu=supersparc} \
%{m32:%{m64:%emay not use both -m32 and -m64}} \
%{m32:-mptr32 -mno-stack-bias %{!mlong-double-128:-mlong-double-64} \
  %{!mcpu*:%{!mcypress:%{!msparclite:%{!mf930:%{!mf934:%{!mv8:%{!msupersparc:-mcpu=cypress}}}}}}}} \
%{!m32:%{!mcpu*:-mcpu=ultrasparc}} \
%{!mno-vis:%{!m32:%{!mcpu=v9:-mvis}}} \
"
#endif

/* Support for a compile-time default CPU, et cetera.  The rules are:
   --with-cpu is ignored if -mcpu is specified.
   --with-tune is ignored if -mtune is specified.
   --with-float is ignored if -mhard-float, -msoft-float, -mfpu, or -mno-fpu
     are specified.
   In the SPARC_BI_ARCH compiler we cannot pass %{!mcpu=*:-mcpu=%(VALUE)}
   here, otherwise say -mcpu=v7 would be passed even when -m64.
   CC1_SPEC above takes care of this instead.  */
#undef OPTION_DEFAULT_SPECS
#if DEFAULT_ARCH32_P
#define OPTION_DEFAULT_SPECS \
  {"cpu", "%{!m64:%{!mcpu=*:-mcpu=%(VALUE)}}" }, \
  {"tune", "%{!mtune=*:-mtune=%(VALUE)}" }, \
  {"float", "%{!msoft-float:%{!mhard-float:%{!fpu:%{!no-fpu:-m%(VALUE)-float}}}}" }
#else
#define OPTION_DEFAULT_SPECS \
  {"cpu", "%{!m32:%{!mcpu=*:-mcpu=%(VALUE)}}" }, \
  {"tune", "%{!mtune=*:-mtune=%(VALUE)}" }, \
  {"float", "%{!msoft-float:%{!mhard-float:%{!fpu:%{!no-fpu:-m%(VALUE)-float}}}}" }
#endif

#if DEFAULT_ARCH32_P
#define MULTILIB_DEFAULTS { "m32" }
#else
#define MULTILIB_DEFAULTS { "m64" }
#endif

#else /* !SPARC_BI_ARCH */

#undef LINK_SPEC
#define LINK_SPEC "-m elf64_sparc -Y P,/usr/lib64 %{shared:-shared} \
  %{!shared: \
    %{!ibcs: \
      %{!static: \
        %{rdynamic:-export-dynamic} \
        %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER64 "}} \
        %{static:-static}}} \
%{mlittle-endian:-EL} \
%{!mno-relax:%{!r:-relax}} \
"

#endif /* !SPARC_BI_ARCH */

/* The sun bundled assembler doesn't accept -Yd, (and neither does gas).
   It's safe to pass -s always, even if -g is not used.  */
#undef ASM_SPEC
#define ASM_SPEC "\
%{V} \
%{v:%{!V:-V}} \
%{!Qn:-Qy} \
%{n} \
%{T} \
%{Ym,*} \
%{Wa,*:%*} \
-s %{fpic|fPIC|fpie|fPIE:-K PIC} \
%{mlittle-endian:-EL} \
%(asm_cpu) %(asm_arch) %(asm_relax)"

/* Same as sparc.h */
#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(REGNO) (REGNO)

#undef ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(FILE, NAME, SIZE, ALIGN)		\
do {									\
  fputs ("\t.local\t", (FILE));		\
  assemble_name ((FILE), (NAME));					\
  putc ('\n', (FILE));							\
  ASM_OUTPUT_ALIGNED_COMMON (FILE, NAME, SIZE, ALIGN);			\
} while (0)

#undef COMMON_ASM_OP
#define COMMON_ASM_OP "\t.common\t"

#undef  LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX  "."

/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#undef  ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)	\
  sprintf (LABEL, "*.L%s%ld", PREFIX, (long)(NUM))

/* DWARF bits.  */

/* Follow Irix 6 and not the Dwarf2 draft in using 64-bit offsets. 
   Obviously the Dwarf2 folks haven't tried to actually build systems
   with their spec.  On a 64-bit system, only 64-bit relocs become
   RELATIVE relocations.  */

/* #define DWARF_OFFSET_SIZE PTR_SIZE */

#undef DITF_CONVERSION_LIBFUNCS
#define DITF_CONVERSION_LIBFUNCS 1

#if defined(HAVE_LD_EH_FRAME_HDR)
#define LINK_EH_SPEC "%{!static:--eh-frame-hdr} "
#endif

#ifdef HAVE_AS_TLS
#undef TARGET_SUN_TLS
#undef TARGET_GNU_TLS
#define TARGET_SUN_TLS 0
#define TARGET_GNU_TLS 1
#endif

/* Don't be different from other Linux platforms in this regard.  */
#define HANDLE_PRAGMA_PACK_PUSH_POP

/* We use GNU ld so undefine this so that attribute((init_priority)) works.  */
#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP

/* Determine whether the entire c99 runtime is present in the
   runtime library.  */
#define TARGET_C99_FUNCTIONS (OPTION_GLIBC)

#define TARGET_POSIX_IO

#undef LINK_GCC_C_SEQUENCE_SPEC
#define LINK_GCC_C_SEQUENCE_SPEC \
  "%{static:--start-group} %G %L %{static:--end-group}%{!static:%G}"

/* Use --as-needed -lgcc_s for eh support.  */
#ifdef HAVE_LD_AS_NEEDED
#define USE_LD_AS_NEEDED 1
#endif

#define MD_UNWIND_SUPPORT "config/sparc/linux-unwind.h"

/* Linux currently uses RMO in uniprocessor mode, which is equivalent to
   TMO, and TMO in multiprocessor mode.  But they reserve the right to
   change their minds.  */
#undef SPARC_RELAXED_ORDERING
#define SPARC_RELAXED_ORDERING true

#undef NEED_INDICATE_EXEC_STACK
#define NEED_INDICATE_EXEC_STACK 1

#ifdef TARGET_LIBC_PROVIDES_SSP
/* sparc glibc provides __stack_chk_guard in [%g7 + 0x14],
   sparc64 glibc provides it at [%g7 + 0x28].  */
#define TARGET_THREAD_SSP_OFFSET	(TARGET_ARCH64 ? 0x28 : 0x14)
#endif

/* Define if long doubles should be mangled as 'g'.  */
#define TARGET_ALTERNATE_LONG_DOUBLE_MANGLING
