/* Support for GCC using WindISS simulator.
   Copyright (C) 2003 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC. 

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


/* windiss uses wchar_t == unsigned short (UCS2) on all architectures.  */
#undef WCHAR_TYPE
#define WCHAR_TYPE "short unsigned int"
#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16

/* windiss has wint_t == int */
#undef WINT_TYPE
#define WINT_TYPE "int"

/* No profiling.  */
#undef  FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE, LABELNO)     \
{                                            \
  sorry ("profiler support for WindISS");    \
}
