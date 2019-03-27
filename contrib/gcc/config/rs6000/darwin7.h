/* Target definitions for Darwin 7.x (Mac OS X) systems.
   Copyright (C) 2004, 2005
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

/* Machine dependent libraries.  Include libmx when compiling for
   Darwin 7.0 and above, but before libSystem, since the functions are
   actually in libSystem but for 7.x compatibility we want them to be
   looked for in libmx first.  Include libmx by default because otherwise
   libstdc++ isn't usable.  */

#undef	LIB_SPEC
#define LIB_SPEC "%{!static:\
  %:version-compare(!< 10.3 mmacosx-version-min= -lmx)\
  -lSystem}"
