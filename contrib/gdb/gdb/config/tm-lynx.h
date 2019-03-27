/* Macro definitions for LynxOS targets.
   Copyright 1993, 1995 Free Software Foundation, Inc.

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

#ifndef TM_LYNX_H
#define TM_LYNX_H

#include "coff-solib.h"		/* COFF shared library support */

/* Lynx's signal.h doesn't seem to have any macros for what signal numbers
   the real-time events are.  */
#define REALTIME_LO 33
/* One more than the last one.  */
#define REALTIME_HI 64

#endif /* TM_LYNX_H */
