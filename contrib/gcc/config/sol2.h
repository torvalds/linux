/* Operating system specific defines to be used when targeting GCC for any
   Solaris 2 system.
   Copyright 2002, 2003, 2004 Free Software Foundation, Inc.

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

/* We use stabs-in-elf for debugging, because that is what the native
   toolchain uses.  */
#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG

/* Solaris 2 (at least as of 2.5.1) uses a 32-bit wchar_t.  */
#undef WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD

/* Solaris 2 uses a wint_t different from the default. This is required
   by the SCD 2.4.1, p. 6-83, Figure 6-66.  */
#undef	WINT_TYPE
#define	WINT_TYPE "long int"

#undef	WINT_TYPE_SIZE
#define	WINT_TYPE_SIZE BITS_PER_WORD

#define TARGET_HANDLE_PRAGMA_REDEFINE_EXTNAME 1

/* ??? Note: in order for -compat-bsd to work fully,
   we must somehow arrange to fixincludes /usr/ucbinclude
   and put the result in $(libsubdir)/ucbinclude.  */

#undef CPP_SUBTARGET_SPEC
#define CPP_SUBTARGET_SPEC "\
%{pthreads|pthread:-D_REENTRANT -D_PTHREADS} \
%{!pthreads:%{!pthread:%{threads:-D_REENTRANT -D_SOLARIS_THREADS}}} \
%{compat-bsd:-iwithprefixbefore ucbinclude -I/usr/ucbinclude} \
"

/* Names to predefine in the preprocessor for this target machine.  */
#define TARGET_SUB_OS_CPP_BUILTINS()
#define TARGET_OS_CPP_BUILTINS()			\
    do {						\
	builtin_define_std ("unix");			\
	builtin_define_std ("sun");			\
	builtin_define ("__svr4__");			\
	builtin_define ("__SVR4");			\
	builtin_assert ("system=unix");			\
	builtin_assert ("system=svr4");			\
	/* For C++ we need to add some additional macro	\
	   definitions required by the C++ standard	\
	   library.  */					\
	if (c_dialect_cxx ())				\
	  {						\
	    builtin_define ("_XOPEN_SOURCE=500");	\
	    builtin_define ("_LARGEFILE_SOURCE=1");	\
	    builtin_define ("_LARGEFILE64_SOURCE=1");	\
	    builtin_define ("__EXTENSIONS__");		\
	  }						\
	TARGET_SUB_OS_CPP_BUILTINS();			\
    } while (0)

/* The system headers under Solaris 2 are C++-aware since 2.0.  */
#define NO_IMPLICIT_EXTERN_C

/* The sun bundled assembler doesn't accept -Yd, (and neither does gas).
   It's safe to pass -s always, even if -g is not used.  */
#undef ASM_SPEC
#define ASM_SPEC "\
%{v:-V} %{Qy:} %{!Qn:-Qy} %{n} %{T} %{Ym,*} %{Wa,*:%*} -s \
%{fpic|fpie|fPIC|fPIE:-K PIC} \
%(asm_cpu) \
"

/* We don't use the standard LIB_SPEC only because we don't yet support c++.  */
#undef LIB_SPEC
#define LIB_SPEC \
  "%{compat-bsd:-lucb -lsocket -lnsl -lelf -laio} \
   %{!shared:\
     %{!symbolic:\
       %{pthreads|pthread:-lpthread} \
       %{!pthreads:%{!pthread:%{threads:-lthread}}} \
       %{p|pg:-ldl} -lc}}"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s crtn.o%s"

/* We don't use the standard svr4 STARTFILE_SPEC because it's wrong for us.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "%{!shared: \
			 %{!symbolic: \
			  %{p:mcrt1.o%s} \
                          %{!p: \
	                    %{pg:gcrt1.o%s gmon.o%s} \
                            %{!pg:crt1.o%s}}}} \
			crti.o%s %(startfile_arch) \
			crtbegin.o%s"

#undef STARTFILE_ARCH32_SPEC
#define STARTFILE_ARCH32_SPEC "%{ansi:values-Xc.o%s} \
			    %{!ansi:values-Xa.o%s}"

#undef STARTFILE_ARCH_SPEC
#define STARTFILE_ARCH_SPEC STARTFILE_ARCH32_SPEC

#undef LINK_ARCH32_SPEC_BASE
#define LINK_ARCH32_SPEC_BASE \
  "%{G:-G} \
   %{YP,*} \
   %{R*} \
   %{compat-bsd: \
     %{!YP,*:%{p|pg:-Y P,/usr/ucblib:/usr/ccs/lib/libp:/usr/lib/libp:/usr/ccs/lib:/usr/lib} \
             %{!p:%{!pg:-Y P,/usr/ucblib:/usr/ccs/lib:/usr/lib}}} \
             -R /usr/ucblib} \
   %{!compat-bsd: \
     %{!YP,*:%{p|pg:-Y P,/usr/ccs/lib/libp:/usr/lib/libp:/usr/ccs/lib:/usr/lib} \
             %{!p:%{!pg:-Y P,/usr/ccs/lib:/usr/lib}}}}"

#undef LINK_ARCH32_SPEC
#define LINK_ARCH32_SPEC LINK_ARCH32_SPEC_BASE

#undef LINK_ARCH_SPEC
#define LINK_ARCH_SPEC LINK_ARCH32_SPEC

/* This should be the same as in svr4.h, except with -R added.  */
#undef  LINK_SPEC
#define LINK_SPEC \
  "%{h*} %{v:-V} \
   %{b} \
   %{static:-dn -Bstatic} \
   %{shared:-G -dy %{!mimpure-text:-z text}} \
   %{symbolic:-Bsymbolic -G -dy -z text} \
   %(link_arch) \
   %{Qy:} %{!Qn:-Qy}"

/* The Solaris linker doesn't understand constructor priorities.  (The
   GNU linker does support constructor priorities, so GNU ld
   configuration files for Solaris override this setting.)  */
#undef SUPPORTS_INIT_PRIORITY
#define SUPPORTS_INIT_PRIORITY 0

/* This defines which switch letters take arguments.
   It is as in svr4.h but with -R added.  */
#undef SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR) \
  (DEFAULT_SWITCH_TAKES_ARG(CHAR) \
   || (CHAR) == 'R' \
   || (CHAR) == 'h' \
   || (CHAR) == 'z')

#define STDC_0_IN_SYSTEM_HEADERS 1

/*
 * Attempt to turn on access permissions for the stack.
 *
 * _SC_STACK_PROT is only defined for post 2.6, but we want this code
 * to run always.  2.6 can change the stack protection but has no way to
 * query it.
 *
 */

/* sys/mman.h is not present on some non-Solaris configurations
   that use sol2.h, so ENABLE_EXECUTE_STACK must use a magic
   number instead of the appropriate PROT_* flags.  */

#define ENABLE_EXECUTE_STACK					\
									\
/* #define STACK_PROT_RWX (PROT_READ | PROT_WRITE | PROT_EXEC) */	\
									\
static int need_enable_exec_stack;					\
									\
static void check_enabling(void) __attribute__ ((constructor));		\
static void check_enabling(void)					\
{									\
  extern long sysconf(int);						\
									\
  int prot = (int) sysconf(515 /* _SC_STACK_PROT */);			\
  if (prot != 7 /* STACK_PROT_RWX */)					\
    need_enable_exec_stack = 1;						\
}									\
									\
extern void __enable_execute_stack (void *);				\
void									\
__enable_execute_stack (void *addr)					\
{									\
  extern int mprotect(void *, size_t, int);				\
  if (!need_enable_exec_stack)						\
    return;								\
  else {								\
    long size = getpagesize ();						\
    long mask = ~(size-1);						\
    char *page = (char *) (((long) addr) & mask); 			\
    char *end  = (char *) ((((long) (addr + TRAMPOLINE_SIZE)) & mask) + size); \
									\
    if (mprotect (page, end - page, 7 /* STACK_PROT_RWX */) < 0)	\
      perror ("mprotect of trampoline code");				\
  }									\
}

/* Support Solaris-specific format checking for cmn_err.  */
#define TARGET_N_FORMAT_TYPES 1
#define TARGET_FORMAT_TYPES solaris_format_types

/* #pragma init and #pragma fini are implemented on top of init and
   fini attributes.  */
#define SOLARIS_ATTRIBUTE_TABLE						\
  { "init",      0, 0, true,  false,  false, NULL },			\
  { "fini",      0, 0, true,  false,  false, NULL }

/* This is how to declare the size of a function.  For Solaris, we output
   any .init or .fini entries here.  */
#undef ASM_DECLARE_FUNCTION_SIZE
#define ASM_DECLARE_FUNCTION_SIZE(FILE, FNAME, DECL)		\
  do								\
    {								\
      if (!flag_inhibit_size_directive)				\
	ASM_OUTPUT_MEASURED_SIZE (FILE, FNAME);			\
      solaris_output_init_fini (FILE, DECL);			\
    }								\
  while (0)

/* Register the Solaris-specific #pragma directives.  */
#define REGISTER_TARGET_PRAGMAS() solaris_register_pragmas ()

extern GTY(()) tree solaris_pending_aligns;
extern GTY(()) tree solaris_pending_inits;
extern GTY(()) tree solaris_pending_finis;

/* Allow macro expansion in #pragma pack.  */
#define HANDLE_PRAGMA_PACK_WITH_EXPANSION
