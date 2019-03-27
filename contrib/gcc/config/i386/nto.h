/* Definitions for Intel 386 running QNX/Neutrino.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.

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

#undef  DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1

#undef TARGET_VERSION
#define TARGET_VERSION	fprintf (stderr, " (QNX/Neutrino/i386 ELF)");

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
        builtin_define_std ("__X86__");		\
        builtin_define_std ("__QNXNTO__");	\
        builtin_define_std ("__QNX__");		\
        builtin_define_std ("__ELF__");		\
        builtin_define_std ("__LITTLEENDIAN__");\
        builtin_assert ("system=qnx");		\
        builtin_assert ("system=qnxnto");	\
        builtin_assert ("system=nto");		\
        builtin_assert ("system=unix");		\
    }						\
  while (0)

#undef THREAD_MODEL_SPEC
#define THREAD_MODEL_SPEC "posix"

#ifdef CROSS_DIRECTORY_STRUCTURE
#define SYSROOT_SUFFIX_SPEC "x86"
#endif

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
"%{!shared: \
  %{!symbolic: \
    %{pg:mcrt1.o%s} \
    %{!pg:%{p:mcrt1.o%s} \
    %{!p:crt1.o%s}}}} \
crti.o%s \
%{fexceptions: crtbegin.o%s} \
%{!fexceptions: %R/lib/crtbegin.o}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC \
  "crtend.o%s crtn.o%s"

#undef LINK_SPEC
#define LINK_SPEC \
  "%{h*} %{v:-V} \
   %{b} \
   %{static:-dn -Bstatic} \
   %{shared:-G -dy -z text} \
   %{symbolic:-Bsymbolic -G -dy -z text} \
   %{G:-G} \
   %{YP,*} \
   %{!YP,*:%{p:-Y P,%R/lib} \
    %{!p:-Y P,%R/lib}} \
   %{Qy:} %{!Qn:-Qy} \
   -m i386nto \
   %{!shared: --dynamic-linker /usr/lib/ldqnx.so.2}"


#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "long unsigned int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD

#define NO_IMPLICIT_EXTERN_C 1

