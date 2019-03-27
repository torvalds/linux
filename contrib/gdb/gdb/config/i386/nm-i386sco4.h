/* Native support for SCO 3.2v4.
   Copyright 1993 Free Software Foundation, Inc.
   Contributed by Cygnus Support.  By Ian Lance Taylor
   <ian@cygnus.com> based on work by Martin Walker <maw@netcom.com>.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* SCO 3.2v4 is actually just like SCO 3.2v2, except that it
   additionally supports attaching to a process.  */

#include "i386/nm-i386sco.h"

#define ATTACH_DETACH

/* SCO, in its wisdom, does not provide <sys/ptrace.h>.  infptrace.c
   does not have defaults for these values.  */
#define PTRACE_ATTACH 10
#define PTRACE_DETACH 11
