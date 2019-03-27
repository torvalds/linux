/* Target independent definitions for LynxOS.
   Copyright (C) 1993, 1994, 1995, 1996, 1999, 2000, 2002, 2003, 2004
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

/* In this file we set up defaults that can be chosen by
   <target>/lynx.h files.  A target-specific lynx.h file can decide
   either to define and override these definitions or to use them by
   ensuring they are undefined at this point.  If we were to #undef
   them here we might accidentally disable some target-specific
   defines.  */

#ifndef EXTRA_OS_LYNX_TARGET_SPECS
# define EXTRA_OS_LYNX_TARGET_SPECS
#endif

#ifndef EXTRA_OS_LYNX_SPECS
# define EXTRA_OS_LYNX_SPECS \
  { "cpp_os_lynx", CPP_OS_LYNX_SPEC }, \
  { "lib_os_lynx", LIB_OS_LYNX_SPEC }, \
  { "link_os_lynx", LINK_OS_LYNX_SPEC }, \
  { "startfile_os_lynx", STARTFILE_OS_LYNX_SPEC }, \
  { "endfile_os_lynx", ENDFILE_OS_LYNX_SPEC }, \
  EXTRA_OS_LYNX_TARGET_SPECS
#endif

#ifndef SUBTARGET_EXTRA_SPECS
# define SUBTARGET_EXTRA_SPECS EXTRA_OS_LYNX_SPECS
#endif

#ifndef CPP_SPEC
# define CPP_SPEC "%(cpp_cpu) %(cpp_os_lynx)"
#endif

#ifndef LIB_SPEC
# define LIB_SPEC "%(lib_os_lynx)"
#endif

#ifndef LINK_SPEC
# define LINK_SPEC "%(link_os_lynx)"
#endif

#ifndef STARTFILE_SPEC
# define STARTFILE_SPEC "%(startfile_os_lynx)"
#endif

#ifndef ENDFILE_SPEC
# define ENDFILE_SPEC "%(endfile_os_lynx)"
#endif

#ifndef CPP_OS_LYNX_SPEC
# define CPP_OS_LYNX_SPEC \
"%{mthreads: \
   %{mlegacy-threads: \
     %ecannot use mthreads and mlegacy-threads together}} \
 %{mthreads: -D_MULTITHREADED} \
 %{mlegacy-threads: -D_THREADS_POSIX4ad4} \
 -Asystem=lynx -Asystem=unix -D__Lynx__ -D__unix__"
#endif

#ifndef LIB_OS_LYNX_SPEC
# define LIB_OS_LYNX_SPEC \
"%{mlegacy-threads:-lposix-pre1c} -lm -lc"
#endif

/* We link static executables for LynxOS by default unless -mshared is
   used when linking an executable.  Along the same line, we link to
   shared libraries when linking a shared object by default unless
   -static is used.

   We have to pass in our -L options here otherwise the translated
   startfile directories (%D) will take priority over this.
   Furthermore since we have to pass in -L options here we have to
   make sure that -L options provided by the user take priority over
   everything we specify.  */

#ifndef LINK_OS_LYNX_SPEC
# define LINK_OS_LYNX_SPEC \
"%{shared} %{static} \
 %{mshared: %{static: %ecannot use mshared and static together}} \
 %{!mshared: %{!shared: %{!static: -static}}} \
 %{L*} \
 %{mthreads: \
   %{mshared: -L/lib/thread/shlib -rpath /lib/thread/shlib} \
   %{shared: \
     %{!static: -L/lib/thread/shlib -rpath /lib/thread/shlib} \
   %{!mshared: -L/lib/thread}} \
   %{shared: %{static: -L/lib/thread}}} \
 %{!mthreads: \
   %{mshared: -L/lib/shlib -rpath /lib/shlib} \
   %{shared: -L/lib/shlib -rpath /lib/shlib}} \
 %{mlegacy-threads:-lposix-pre1c} -lm -lc"
#endif

#ifndef STARTFILE_OS_LYNX_SPEC
# define STARTFILE_OS_LYNX_SPEC \
"%{!shared: \
   %{!mthreads: \
     %{p:gcrt1.o%s} %{pg:gcrt1.o%s} \
     %{!p:%{!pg:crt1.o%s}}} \
   %{mthreads: \
     %{p:thread/gcrt1.o%s} %{pg:thread/gcrt1.o%s} \
     %{!p:%{!pg:thread/crt1.o%s }}}}\
 %{mthreads: thread/crti.o%s} %{!mthreads: crti.o%s} \
 %{!shared: crtbegin.o%s} \
 %{shared: crtbeginS.o%s}"
#endif

#ifndef ENDFILE_OS_LYNX_SPEC
# define ENDFILE_OS_LYNX_SPEC \
"%{!shared: crtend.o%s} \
 %{shared: crtendS.o%s} \
 %{mthreads: thread/crtn.o%s} %{!mthreads: crtn.o%s}"
#endif

/* Define the actual types of some ANSI-mandated types.  */

#ifndef SIZE_TYPE
# define SIZE_TYPE "unsigned int"
#endif

#ifndef  PTRDIFF_TYPE
# define PTRDIFF_TYPE "int"
#endif

#ifndef  WCHAR_TYPE
# define WCHAR_TYPE "long int"
#endif

#ifndef  WCHAR_TYPE_SIZE
# define WCHAR_TYPE_SIZE BITS_PER_WORD
#endif

/* Define ASM_OUTPUT_ALIGN to use the .balign directive rather that
   the .align directive with GAS.  */

#ifndef ASM_OUTPUT_ALIGN
# define ASM_OUTPUT_ALIGN(FILE, LOG) 			\
  do							\
    {							\
      if ((LOG) != 0)					\
	fprintf ((FILE), "\t.balign %d\n", 1 << (LOG));	\
    }							\
  while (0)
#endif

/* Keep the *_DEBUGGING_INFO defines from elfos.h except that stabs is
   the default on LynxOS.  */

#ifndef PREFERRED_DEBUGGING_TYPE
# define PREFERRED_DEBUGGING_TYPE DBX_DEBUG
#endif

/* We have C++ support in our system headers.  */

#ifndef NO_IMPLICIT_EXTERN_C
# define NO_IMPLICIT_EXTERN_C
#endif

#ifndef TARGET_POSIX_IO
# define TARGET_POSIX_IO
#endif
