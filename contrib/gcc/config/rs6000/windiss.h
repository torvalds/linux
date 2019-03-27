/* Support for GCC on PowerPC using WindISS simulator.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC. 

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

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (PowerPC WindISS)");

#undef  LIB_DEFAULT_SPEC
#define LIB_DEFAULT_SPEC "%(lib_windiss)"

#undef  STARTFILE_DEFAULT_SPEC
#define STARTFILE_DEFAULT_SPEC "%(startfile_windiss)"

#undef  ENDFILE_DEFAULT_SPEC
#define ENDFILE_DEFAULT_SPEC "%(endfile_windiss)"

#undef	LINK_START_DEFAULT_SPEC
#define LINK_START_DEFAULT_SPEC "%(link_start_windiss)"

#undef	LINK_OS_DEFAULT_SPEC
#define LINK_OS_DEFAULT_SPEC "%(link_os_windiss)"

#undef  CRTSAVRES_DEFAULT_SPEC
#define CRTSAVRES_DEFAULT_SPEC ""

#undef  WCHAR_TYPE
#define WCHAR_TYPE "short unsigned int"

#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16
