/* Traditional MIPS targets
   Copyright 2000, 2003 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This file is te-tmips.h and is intended to provide support for
   traditional mips targets like mips-dde-sysv4.2MP (Supermax ) ,
   mips-sni-sysv4* (Sinix) etc. The base for this file is te-generic.h.
   Created by Koundinya.K < kk@ddeorg.soft.net > with the help of
   Ian Lance Taylor, Cygnus Support, <ian@cygnus.com>.  */

/* Added these, because if we don't know what we're targeting we may
   need an assembler version of libgcc, and that will use local
   labels.  */

#define TE_TMIPS 1
#define LOCAL_LABELS_DOLLAR 1
#define LOCAL_LABELS_FB 1

/* these define interfaces */
#ifdef OBJ_HEADER
#include OBJ_HEADER
#else
#include "obj-format.h"
#endif
