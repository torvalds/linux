/* Disassemble support for GDB.
   Copyright 2002 Free Software Foundation, Inc.

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

#ifndef DISASM_H
#define DISASM_H

struct ui_out;
struct ui_file;

extern void gdb_disassembly (struct ui_out *uiout,
			     char *file_string,
			     int line_num,
			     int mixed_source_and_assembly,
			     int how_many, CORE_ADDR low, CORE_ADDR high);

/* Print the instruction at address MEMADDR in debugged memory, on
   STREAM.  Returns length of the instruction, in bytes.  */

extern int gdb_print_insn (CORE_ADDR memaddr, struct ui_file *stream);

#endif
