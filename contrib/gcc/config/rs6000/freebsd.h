/* Definitions for PowerPC running FreeBSD using the ELF format
   Copyright (C) 2001, 2003 Free Software Foundation, Inc.
   Contributed by David E. O'Brien <obrien@FreeBSD.org> and BSDi.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* Override the defaults, which exist to force the proper definition.  */

#ifdef IN_LIBGCC2
#undef TARGET_64BIT
#ifdef __powerpc64__
#define TARGET_64BIT 1
#else
#define TARGET_64BIT 0
#endif
#endif

/* On 64-bit systems, use the AIX ABI like Linux and NetBSD */

#undef	DEFAULT_ABI
#define	DEFAULT_ABI (TARGET_64BIT ? ABI_AIX : ABI_V4)
#undef	TARGET_AIX
#define	TARGET_AIX TARGET_64BIT

#ifdef HAVE_LD_NO_DOT_SYMS
/* New ABI uses a local sym for the function entry point.  */
extern int dot_symbols;
#undef DOT_SYMBOLS
#define DOT_SYMBOLS dot_symbols
#endif

#undef  FBSD_TARGET_CPU_CPP_BUILTINS
#define FBSD_TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__PPC__");		\
      builtin_define ("__ppc__");		\
      builtin_define ("__PowerPC__");		\
      builtin_define ("__powerpc__");		\
      if (TARGET_64BIT)				\
	{					\
	  builtin_define ("__LP64__");		\
	  builtin_define ("__ppc64__");		\
	  builtin_define ("__powerpc64__");	\
	  builtin_define ("__arch64__");	\
	  builtin_assert ("cpu=powerpc64");	\
	  builtin_assert ("machine=powerpc64");	\
	} else {				\
	  builtin_assert ("cpu=powerpc");	\
	  builtin_assert ("machine=powerpc");	\
	}					\
    }						\
  while (0)

#define INVALID_64BIT "-m%s not supported in this configuration"
#define INVALID_32BIT INVALID_64BIT

#undef	SUBSUBTARGET_OVERRIDE_OPTIONS
#define	SUBSUBTARGET_OVERRIDE_OPTIONS				\
  do								\
    {								\
      if (!rs6000_explicit_options.alignment)			\
	rs6000_alignment_flags = MASK_ALIGN_NATURAL;		\
      if (TARGET_64BIT)						\
	{							\
	  if (DEFAULT_ABI != ABI_AIX)				\
	    {							\
	      rs6000_current_abi = ABI_AIX;			\
	      error (INVALID_64BIT, "call");			\
	    }							\
	  dot_symbols = !strcmp (rs6000_abi_name, "aixdesc");	\
	  if (target_flags & MASK_RELOCATABLE)			\
	    {							\
	      target_flags &= ~MASK_RELOCATABLE;		\
	      error (INVALID_64BIT, "relocatable");		\
	    }							\
	  if (target_flags & MASK_EABI)				\
	    {							\
	      target_flags &= ~MASK_EABI;			\
	      error (INVALID_64BIT, "eabi");			\
	    }							\
	  if (target_flags & MASK_PROTOTYPE)			\
	    {							\
	      target_flags &= ~MASK_PROTOTYPE;			\
	      error (INVALID_64BIT, "prototype");		\
	    }							\
	  if ((target_flags & MASK_POWERPC64) == 0)		\
	    {							\
	      target_flags |= MASK_POWERPC64;			\
	      error ("64 bit CPU required");			\
	    }							\
	}							\
    }								\
  while (0)


#undef	STARTFILE_DEFAULT_SPEC
#define STARTFILE_DEFAULT_SPEC "%(startfile_freebsd)"

#undef	ENDFILE_DEFAULT_SPEC
#define ENDFILE_DEFAULT_SPEC "%(endfile_freebsd)"

#undef	LIB_DEFAULT_SPEC
#define LIB_DEFAULT_SPEC "%(lib_freebsd)"

#undef	LINK_START_DEFAULT_SPEC
#define LINK_START_DEFAULT_SPEC "%(link_start_freebsd)"

#undef	LINK_OS_DEFAULT_SPEC
#define	LINK_OS_DEFAULT_SPEC "%(link_os_freebsd)"

/* XXX: This is wrong for many platforms in sysv4.h.
   We should work on getting that definition fixed.  */
#undef  LINK_SHLIB_SPEC
#define LINK_SHLIB_SPEC "%{shared:-shared} %{!shared: %{static:-static}}"


/************************[  Target stuff  ]***********************************/

/* Define the actual types of some ANSI-mandated types.  
   Needs to agree with <machine/ansi.h>.  GCC defaults come from c-decl.c,
   c-common.c, and config/<arch>/<arch>.h.  */

#undef  SIZE_TYPE
#define SIZE_TYPE	(TARGET_64BIT ? "long unsigned int" : "unsigned int")

#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE	(TARGET_64BIT ? "long int" : "int")

/* rs6000.h gets this wrong for FreeBSD.  We use the GCC defaults instead.  */
#undef WCHAR_TYPE

#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (FreeBSD/PowerPC ELF)");

/* Override rs6000.h definition.  */
#undef  ASM_APP_ON
#define ASM_APP_ON "#APP\n"

/* Override rs6000.h definition.  */
#undef  ASM_APP_OFF
#define ASM_APP_OFF "#NO_APP\n"

/* Tell the assembler we want 32/64-bit binaries if -m32 or -m64 is passed */
#if (TARGET_DEFAULT & MASK_64BIT)
#define	SVR4_ASM_SPEC "%(asm_cpu) \
%{.s: %{mregnames} %{mno-regnames}} %{.S: %{mregnames} %{mno-regnames}} \
%{v:-V} %{Qy:} %{!Qn:-Qy} %{n} %{T} %{Ym,*} %{Yd,*} %{Wa,*:%*} \
%{mrelocatable} %{mrelocatable-lib} %{fpic|fpie|fPIC|fPIE:-K PIC} \
%{memb|msdata|msdata=eabi: -memb} \
%{mlittle|mlittle-endian:-mlittle; \
  mbig|mbig-endian      :-mbig;    \
  mcall-aixdesc |		   \
  mcall-freebsd |		   \
  mcall-netbsd  |		   \
  mcall-openbsd |		   \
  mcall-linux   |		   \
  mcall-gnu             :-mbig;    \
  mcall-i960-old        :-mlittle}"
#define LINK_OS_FREEBSD_SPEC_DEF "\
  %{p:%nconsider using `-pg' instead of `-p' with gprof(1)} \
  %{v:-V} \
  %{assert*} %{R*} %{rpath*} %{defsym*} \
  %{shared:-Bshareable %{h*} %{soname*}} \
  %{!static:--enable-new-dtags}	\
  %{!shared: \
    %{!static: \
      %{rdynamic: -export-dynamic} \
      %{!dynamic-linker:-dynamic-linker %(fbsd_dynamic_linker) }} \
    %{static:-Bstatic}} \
  %{symbolic:-Bsymbolic}"


#undef	ASM_DEFAULT_SPEC
#undef	ASM_SPEC
#undef	LINK_OS_FREEBSD_SPEC
#define	ASM_DEFAULT_SPEC	"-mppc%{!m32:64}"
#define	ASM_SPEC		"%{m32:-a32}%{!m32:-a64} " SVR4_ASM_SPEC
#define	LINK_OS_FREEBSD_SPEC	"%{m32:-melf32ppc_fbsd}%{!m32:-melf64ppc_fbsd} " LINK_OS_FREEBSD_SPEC_DEF
#endif

/* _init and _fini functions are built from bits spread across many
   object files, each potentially with a different TOC pointer.  For
   that reason, place a nop after the call so that the linker can
   restore the TOC pointer if a TOC adjusting call stub is needed.  */
#ifdef __powerpc64__
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
  asm (SECTION_OP "\n"					\
"	bl " #FUNC "\n"					\
"	nop\n"						\
"	.previous");
#endif

/* __throw will restore its own return address to be the same as the
   return address of the function that the throw is being made to.
   This is unfortunate, because we want to check the original
   return address to see if we need to restore the TOC.
   So we have to squirrel it away with this.  */
#define SETUP_FRAME_ADDRESSES() \
  do { if (TARGET_64BIT) rs6000_aix_emit_builtin_unwind_init (); } while (0)

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.  */
#undef	ASM_PREFERRED_EH_DATA_FORMAT
#define	ASM_PREFERRED_EH_DATA_FORMAT(CODE, GLOBAL) \
  ((TARGET_64BIT || flag_pic || TARGET_RELOCATABLE)			\
   ? (((GLOBAL) ? DW_EH_PE_indirect : 0) | DW_EH_PE_pcrel		\
      | (TARGET_64BIT ? DW_EH_PE_udata8 : DW_EH_PE_sdata4))		\
   : DW_EH_PE_absptr)

#ifdef __powerpc64__
#define MD_FROB_UPDATE_CONTEXT(CTX, FS)					\
    if ((FS)->regs.reg[2].how == REG_UNSAVED)				\
      {									\
	unsigned int *insn = (unsigned int *)				\
	    _Unwind_GetGR ((CTX), LINK_REGISTER_REGNUM);		\
	if (insn != NULL && *insn == 0xE8410028)			\
	  _Unwind_SetGRPtr ((CTX), 2, (CTX)->cfa + 40);			\
      }
#endif

#define TARGET_ASM_FILE_END rs6000_elf_end_indicate_exec_stack

/* FreeBSD doesn't support saving and restoring 64-bit regs with a 32-bit
   kernel. This is supported when running on a 64-bit kernel with
   COMPAT_FREEBSD32, but tell GCC it isn't so that our 32-bit binaries
   are compatible. */
#define OS_MISSING_POWERPC64 !TARGET_64BIT

/* Function profiling bits */
#undef  RS6000_MCOUNT
#define RS6000_MCOUNT ((TARGET_64BIT) ? "._mcount" : "_mcount")
#define PROFILE_HOOK(LABEL) \
  do { if (TARGET_64BIT) output_profile_hook (LABEL); } while (0)

#undef NEED_INDICATE_EXEC_STACK
#define NEED_INDICATE_EXEC_STACK 1

/* This is how to declare the size of a function.  */
#undef  ASM_DECLARE_FUNCTION_SIZE
#define ASM_DECLARE_FUNCTION_SIZE(FILE, FNAME, DECL)                    \
  do                                                                    \
    {                                                                   \
      if (!flag_inhibit_size_directive)                                 \
        {                                                               \
          fputs ("\t.size\t", (FILE));                                  \
          if (TARGET_64BIT && DOT_SYMBOLS)                              \
            putc ('.', (FILE));                                         \
          assemble_name ((FILE), (FNAME));                              \
          fputs (",.-", (FILE));                                        \
          rs6000_output_function_entry (FILE, FNAME);                   \
          putc ('\n', (FILE));                                          \
        }                                                               \
    }                                                                   \
  while (0)

