/* Definitions for running gdb on a host machine running any flavor of SVR4.
   Copyright 1991, 1992, 1993, 1995, 1998 Free Software Foundation, Inc.
   Written by Fred Fish at Cygnus Support (fnf@cygnus.com).

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

/* SVR4 has termios facilities. */

#undef HAVE_TERMIO
#define HAVE_TERMIOS

/* SVR4 is a derivative of System V Release 3 (USG) */

#define USG
