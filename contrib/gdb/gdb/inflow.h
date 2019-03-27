/* Low level interface to ptrace, for GDB when running under Unix.

   Copyright 2003 Free Software Foundation, Inc.

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

#ifndef INFLOW_H
#define INFLOW_H

#include "terminal.h"		/* For HAVE_TERMIOS et.al.  */

#ifdef HAVE_TERMIOS
#define PROCESS_GROUP_TYPE pid_t
#endif

#ifdef HAVE_TERMIO
#define PROCESS_GROUP_TYPE int
#endif

#ifdef HAVE_SGTTY
#ifdef SHORT_PGRP
/* This is only used for the ultra.  Does it have pid_t?  */
#define PROCESS_GROUP_TYPE short
#else
#define PROCESS_GROUP_TYPE int
#endif
#endif /* sgtty */

#ifdef PROCESS_GROUP_TYPE
/* Process group for us and the inferior.  Saved and restored just like
   {our,inferior}_ttystate.  */
extern PROCESS_GROUP_TYPE our_process_group;
extern PROCESS_GROUP_TYPE inferior_process_group;
#endif

#endif
