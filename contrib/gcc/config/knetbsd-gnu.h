/* Definitions for kNetBSD-based GNU systems with ELF format
   Copyright (C) 2004, 2006
   Free Software Foundation, Inc.
   Contributed by Robert Millan.

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

#undef LINUX_TARGET_OS_CPP_BUILTINS    
#define LINUX_TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define ("__NetBSD_kernel__");	\
	builtin_define ("__GLIBC__");		\
	builtin_define_std ("unix");		\
	builtin_assert ("system=unix");		\
	builtin_assert ("system=posix");	\
    }						\
  while (0)


#ifdef GLIBC_DYNAMIC_LINKER
#undef GLIBC_DYNAMIC_LINKER
#define GLIBC_DYNAMIC_LINKER "/lib/ld.so.1"
#endif
