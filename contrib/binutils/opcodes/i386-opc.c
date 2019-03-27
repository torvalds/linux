/* Intel 80386 opcode table
   Copyright 2007
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler, and GDB, the GNU Debugger.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "libiberty.h"
#include "i386-opc.h"
#include "i386-tbl.h"

/* Segment stuff.  */
const seg_entry cs = { "cs", 0x2e };
const seg_entry ds = { "ds", 0x3e };
const seg_entry ss = { "ss", 0x36 };
const seg_entry es = { "es", 0x26 };
const seg_entry fs = { "fs", 0x64 };
const seg_entry gs = { "gs", 0x65 };
