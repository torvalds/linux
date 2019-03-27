/* Operating system specific defines to be used when targeting GCC for some
   generic System V Release 4 system.
   Copyright (C) 1991, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001 Free Software Foundation, Inc.
   Contributed by Ron Guilmette (rfg@monkeys.com).

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
Boston, MA 02110-1301, USA.

   To use this file, make up a line like that in config.gcc:

	tm_file="$tm_file elfos.h svr4.h MACHINE/svr4.h"

   where MACHINE is replaced by the name of the basic hardware that you
   are targeting for.  Then, in the file MACHINE/svr4.h, put any really
   system-specific defines (or overrides of defines) which you find that
   you need.
*/

/* Define a symbol indicating that we are using svr4.h.  */
#define USING_SVR4_H

/* Cpp, assembler, linker, library, and startfile spec's.  */

/* This defines which switch letters take arguments.  On svr4, most of
   the normal cases (defined in gcc.c) apply, and we also have -h* and
   -z* options (for the linker).  Note however that there is no such
   thing as a -T option for svr4.  */

#undef  SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR)		\
  (DEFAULT_SWITCH_TAKES_ARG (CHAR)	\
   || (CHAR) == 'h'			\
   || (CHAR) == 'x'			\
   || (CHAR) == 'z')

/* This defines which multi-letter switches take arguments.  On svr4,
   there are no such switches except those implemented by GCC itself.  */

#define WORD_SWITCH_TAKES_ARG(STR)			\
 (DEFAULT_WORD_SWITCH_TAKES_ARG (STR)			\
  && strcmp (STR, "Tdata") && strcmp (STR, "Ttext")	\
  && strcmp (STR, "Tbss"))

/* Provide an ASM_SPEC appropriate for svr4.  Here we try to support as
   many of the specialized svr4 assembler options as seems reasonable,
   given that there are certain options which we can't (or shouldn't)
   support directly due to the fact that they conflict with other options
   for other svr4 tools (e.g. ld) or with other options for GCC itself.
   For example, we don't support the -o (output file) or -R (remove
   input file) options because GCC already handles these things.  We
   also don't support the -m (run m4) option for the assembler because
   that conflicts with the -m (produce load map) option of the svr4
   linker.  We do however allow passing arbitrary options to the svr4
   assembler via the -Wa, option.

   Note that gcc doesn't allow a space to follow -Y in a -Ym,* or -Yd,*
   option.

   The svr4 assembler wants '-' on the command line if it's expected to
   read its stdin.
*/

#undef  ASM_SPEC
#define ASM_SPEC \
  "%{v:-V} %{Qy:} %{!Qn:-Qy} %{n} %{T} %{Ym,*} %{Yd,*} %{Wa,*:%*}"

#define AS_NEEDS_DASH_FOR_PIPED_INPUT

/* Under svr4, the normal location of the `ld' and `as' programs is the
   /usr/ccs/bin directory.  */

#ifndef CROSS_DIRECTORY_STRUCTURE
#undef  MD_EXEC_PREFIX
#define MD_EXEC_PREFIX "/usr/ccs/bin/"
#endif

/* Under svr4, the normal location of the various *crt*.o files is the
   /usr/ccs/lib directory.  */

#ifndef CROSS_DIRECTORY_STRUCTURE
#undef  MD_STARTFILE_PREFIX
#define MD_STARTFILE_PREFIX "/usr/ccs/lib/"
#endif

/* Provide a LIB_SPEC appropriate for svr4.  Here we tack on the default
   standard C library (unless we are building a shared library).  */

#undef	LIB_SPEC
#define LIB_SPEC "%{!shared:%{!symbolic:-lc}}"

/* Provide an ENDFILE_SPEC appropriate for svr4.  Here we tack on our own
   magical crtend.o file (see crtstuff.c) which provides part of the
   support for getting C++ file-scope static object constructed before
   entering `main', followed by the normal svr3/svr4 "finalizer" file,
   which is either `gcrtn.o' or `crtn.o'.  */

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s %{pg:gcrtn.o%s}%{!pg:crtn.o%s}"

/* Provide a LINK_SPEC appropriate for svr4.  Here we provide support
   for the special GCC options -static, -shared, and -symbolic which
   allow us to link things in one of these three modes by applying the
   appropriate combinations of options at link-time.  We also provide
   support here for as many of the other svr4 linker options as seems
   reasonable, given that some of them conflict with options for other
   svr4 tools (e.g. the assembler).  In particular, we do support the
   -z*, -V, -b, -t, -Qy, -Qn, and -YP* options here, and the -e*, -l*,
   -o*, -r, -s, -u*, and -L* options are directly supported by gcc.c
   itself.  We don't directly support the -m (generate load map)
   option because that conflicts with the -m (run m4) option of the
   svr4 assembler.  We also don't directly support the svr4 linker's
   -I* or -M* options because these conflict with existing GCC
   options.  We do however allow passing arbitrary options to the svr4
   linker via the -Wl, option, in gcc.c.  We don't support the svr4
   linker's -a option at all because it is totally useless and because
   it conflicts with GCC's own -a option.

   Note that gcc doesn't allow a space to follow -Y in a -YP,* option.

   When the -G link option is used (-shared and -symbolic) a final link is
   not being done.  */

#undef	LINK_SPEC
#ifdef CROSS_DIRECTORY_STRUCTURE
#define LINK_SPEC "%{h*} %{v:-V} \
		   %{b} \
		   %{static:-dn -Bstatic} \
		   %{shared:-G -dy -z text} \
		   %{symbolic:-Bsymbolic -G -dy -z text} \
		   %{G:-G} \
		   %{YP,*} \
		   %{Qy:} %{!Qn:-Qy}"
#else
#define LINK_SPEC "%{h*} %{v:-V} \
		   %{b} \
		   %{static:-dn -Bstatic} \
		   %{shared:-G -dy -z text} \
		   %{symbolic:-Bsymbolic -G -dy -z text} \
		   %{G:-G} \
		   %{YP,*} \
		   %{!YP,*:%{p:-Y P,/usr/ccs/lib/libp:/usr/lib/libp:/usr/ccs/lib:/usr/lib} \
		    %{!p:-Y P,/usr/ccs/lib:/usr/lib}} \
		   %{Qy:} %{!Qn:-Qy}"
#endif

/* Gcc automatically adds in one of the files /usr/ccs/lib/values-Xc.o
   or /usr/ccs/lib/values-Xa.o for each final link step (depending
   upon the other gcc options selected, such as -ansi).  These files
   each contain one (initialized) copy of a special variable called
   `_lib_version'.  Each one of these files has `_lib_version' initialized
   to a different (enum) value.  The SVR4 library routines query the
   value of `_lib_version' at run to decide how they should behave.
   Specifically, they decide (based upon the value of `_lib_version')
   if they will act in a strictly ANSI conforming manner or not.  */

#undef	STARTFILE_SPEC
#define STARTFILE_SPEC "%{!shared: \
			 %{!symbolic: \
			  %{pg:gcrt1.o%s}%{!pg:%{p:mcrt1.o%s}%{!p:crt1.o%s}}}}\
			%{pg:gcrti.o%s}%{!pg:crti.o%s} \
			%{ansi:values-Xc.o%s} \
			%{!ansi:values-Xa.o%s} \
 			crtbegin.o%s"

/* The numbers used to denote specific machine registers in the System V
   Release 4 DWARF debugging information are quite likely to be totally
   different from the numbers used in BSD stabs debugging information
   for the same kind of target machine.  Thus, we undefine the macro
   DBX_REGISTER_NUMBER here as an extra inducement to get people to
   provide proper machine-specific definitions of DBX_REGISTER_NUMBER
   (which is also used to provide DWARF registers numbers in dwarfout.c)
   in their tm.h files which include this file.  */

#undef DBX_REGISTER_NUMBER

/* Define the actual types of some ANSI-mandated types.  (These
   definitions should work for most SVR4 systems).  */

#undef  SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef  WCHAR_TYPE
#define WCHAR_TYPE "long int"

#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD

#define TARGET_POSIX_IO
