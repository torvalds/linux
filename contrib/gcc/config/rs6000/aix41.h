/* Definitions of target machine for GNU compiler,
   for IBM RS/6000 POWER running AIX version 4.1.
   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2003, 2004,
   2005
   Free Software Foundation, Inc.
   Contributed by David Edelsohn (edelsohn@gnu.org).

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

#undef ASM_SPEC
#define ASM_SPEC "-u %(asm_cpu)"

#undef	ASM_DEFAULT_SPEC
#define ASM_DEFAULT_SPEC "-mcom"

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()     \
  do                                 \
    {                                \
      TARGET_OS_AIX_CPP_BUILTINS (); \
    }                                \
  while (0)

#undef CPP_SPEC
#define CPP_SPEC "%{posix: -D_POSIX_SOURCE}\
   %{ansi: -D_ANSI_C_SOURCE}\
   %{mpe: -I/usr/lpp/ppe.poe/include}\
   %{pthread: -D_THREAD_SAFE}"

#undef TARGET_DEFAULT
#define TARGET_DEFAULT MASK_NEW_MNEMONICS

#undef PROCESSOR_DEFAULT
#define PROCESSOR_DEFAULT PROCESSOR_PPC601

/* Define this macro as a C expression for the initializer of an
   array of string to tell the driver program which options are
   defaults for this target and thus do not need to be handled
   specially when using `MULTILIB_OPTIONS'.

   Do not define this macro if `MULTILIB_OPTIONS' is not defined in
   the target makefile fragment or if none of the options listed in
   `MULTILIB_OPTIONS' are set by default.  *Note Target Fragment::.  */

#undef	MULTILIB_DEFAULTS
#define	MULTILIB_DEFAULTS { "mcpu=common" }

#undef LIB_SPEC
#define LIB_SPEC "%{pg:-L/lib/profiled -L/usr/lib/profiled}\
   %{p:-L/lib/profiled -L/usr/lib/profiled} %{!shared:%{g*:-lg}}\
   %{mpe:-L/usr/lpp/ppe.poe/lib -lmpi -lvtd}\
   %{pthread: -L/usr/lib/threads -lpthreads -lc_r /usr/lib/libc.a}\
   %{!pthread: -lc}"

#undef LINK_SPEC
#define LINK_SPEC "-bpT:0x10000000 -bpD:0x20000000 %{!r:-btextro} -bnodelcsect\
   %{static:-bnso %(link_syscalls) } %{!shared: %{g*: %(link_libg) }}\
   %{shared:-bM:SRE %{!e:-bnoentry}}"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "%{!shared:\
   %{mpe:%{pg:/usr/lpp/ppe.poe/lib/gcrt0.o}\
         %{!pg:%{p:/usr/lpp/ppe.poe/lib/mcrt0.o}\
               %{!p:/usr/lpp/ppe.poe/lib/crt0.o}}}\
   %{!mpe:\
     %{pthread:%{pg:gcrt0_r%O%s}%{!pg:%{p:mcrt0_r%O%s}%{!p:crt0_r%O%s}}}\
     %{!pthread:%{pg:gcrt0%O%s}%{!pg:%{p:mcrt0%O%s}%{!p:crt0%O%s}}}}}"

/* AIX 4 uses PowerPC nop (ori 0,0,0) instruction as call glue for PowerPC
   and "cror 31,31,31" for POWER architecture.  */

#undef RS6000_CALL_GLUE
#define RS6000_CALL_GLUE "{cror 31,31,31|nop}"

/* The IBM AIX 4.x assembler doesn't support forward references in
   .set directives.  We handle this by deferring the output of .set
   directives to the end of the compilation unit.  */
#define TARGET_DEFERRED_OUTPUT_DEFS(DECL,TARGET) true

#undef TARGET_64BIT
#define TARGET_64BIT 0
