/* Definitions for target OS TPF for GNU compiler, for IBM S/390 hardware
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by P.J. Darcy (darcypj@us.ibm.com),
                  Hartmut Penner (hpenner@de.ibm.com), and
                  Ulrich Weigand (uweigand@de.ibm.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef _TPF_H
#define _TPF_H

/* TPF wants the following macros defined/undefined as follows.  */
#undef TARGET_TPF
#define TARGET_TPF 1
#undef ASM_APP_ON
#define ASM_APP_ON "#APP\n"
#undef ASM_APP_OFF
#define ASM_APP_OFF "#NO_APP\n"
#define NO_IMPLICIT_EXTERN_C
#define TARGET_POSIX_IO

#undef  SIZE_TYPE
#define SIZE_TYPE ("long unsigned int")
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE ("long int")
#undef  WCHAR_TYPE
#define WCHAR_TYPE "int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32


/* Basic record keeping for the TPF OS name.  */
#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (TPF: zSeries)");

/* TPF OS specific stack-pointer offset.  */
#undef STACK_POINTER_OFFSET
#define STACK_POINTER_OFFSET 		448

/* When building for TPF, set a generic default target that is 64 bits. Also
   enable TPF profiling support and the standard backchain by default.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_TPF_PROFILING | MASK_64BIT | MASK_ZARCH \
			| MASK_HARD_FLOAT | MASK_BACKCHAIN)

/* Exception handling.  */

/* Select a format to encode pointers in exception handling data.  */
#undef ASM_PREFERRED_EH_DATA_FORMAT
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE, GLOBAL) DW_EH_PE_absptr

/* TPF OS specific compiler settings.  */
#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()                \
  do                                            \
    {                                           \
      builtin_define_std ("tpf");               \
      builtin_assert ("system=tpf");            \
      builtin_define ("__ELF__");               \
    }                                           \
  while (0)


#define EXTRA_SPECS                             \
  { "entry_spec", ENTRY_SPEC }

/* Make TPF specific spec file settings here.  */

#undef STARTFILE_SPEC
#define STARTFILE_SPEC \
  "%{mmain:crt0%O%s} crtbeginS%O%s crt3%O%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "crtendS%O%s"

#undef CC1_SPEC
#define CC1_SPEC "%{!fverbose-asm: -fverbose-asm}"

/* The GNU C++ standard library requires that these macros be defined.  */
#undef CPLUSPLUS_CPP_SPEC
#define CPLUSPLUS_CPP_SPEC "-D_GNU_SOURCE %(cpp)"

#undef ASM_SPEC
#define ASM_SPEC "%{m31&m64}%{mesa&mzarch}%{march=*} \
                  -alshd=%b.lst"

/* It would be nice to get the system linker script define the ones that it
   needed.  */
#undef LIB_SPEC
#define LIB_SPEC "-lCTIS -lCISO -lCLBM -lCTAL -lCFVS -lCTBX -lCTXO \
                  -lCJ00 -lCTDF -lCOMX -lCOMS -lCTHD -lCTAD -lTPFSTUB"

#undef TARGET_C99_FUNCTIONS
#define TARGET_C99_FUNCTIONS 1

#define ENTRY_SPEC "%{mmain:-entry=_start} \
                    %{!mmain:-entry=0}"

/* All linking is done shared on TPF-OS.  */
/* FIXME: When binutils patch for new emulation is committed
   then change emulation to elf64_s390_tpf.  */
#undef LINK_SPEC
#define LINK_SPEC \
  "-m elf64_s390 \
   %{static:%estatic is not supported on TPF-OS} \
   %{shared: -shared} \
   %{!shared:-shared} \
   %(entry_spec)"

#define MD_UNWIND_SUPPORT "config/s390/tpf-unwind.h"

/* IBM copies these libraries over with these names.  */
#define MATH_LIBRARY "-lCLBM"
#define LIBSTDCXX "-lCPP1"
#endif /* ! _TPF_H */
