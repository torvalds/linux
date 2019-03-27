/* ia64-opc-d.c -- IA-64 `D' opcode table.
   Copyright 1998, 1999, 2000, 2001, 2002 Free Software Foundation, Inc.
   Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

   This file is part of GDB, GAS, and the GNU binutils.

   GDB, GAS, and the GNU binutils are free software; you can redistribute
   them and/or modify them under the terms of the GNU General Public
   License as published by the Free Software Foundation; either version
   2, or (at your option) any later version.

   GDB, GAS, and the GNU binutils are distributed in the hope that they
   will be useful, but WITHOUT ANY WARRANTY; without even the implied
   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this file; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

struct ia64_opcode ia64_opcodes_d[] =
  {
    {"add",   IA64_TYPE_DYN, 1, 0, 0, {IA64_OPND_R1, IA64_OPND_IMM22, IA64_OPND_R3_2}, 0, 0, NULL},
    {"add",   IA64_TYPE_DYN, 1, 0, 0, {IA64_OPND_R1, IA64_OPND_IMM14, IA64_OPND_R3}, 0, 0, NULL},
    {"break", IA64_TYPE_DYN, 0, 0, 0, {IA64_OPND_IMMU21}, 0, 0, NULL},
    {"chk.s", IA64_TYPE_DYN, 0, 0, 0, {IA64_OPND_R2, IA64_OPND_TGT25b}, 0, 0, NULL},
    {"hint",  IA64_TYPE_DYN, 0, 0, 0, {IA64_OPND_IMMU21}, 0, 0, NULL},
    {"mov",   IA64_TYPE_DYN, 1, 0, 0, {IA64_OPND_R1,  IA64_OPND_AR3}, 0, 0, NULL},
    {"mov",   IA64_TYPE_DYN, 1, 0, 0, {IA64_OPND_AR3, IA64_OPND_IMM8}, 0, 0, NULL},
    {"mov",   IA64_TYPE_DYN, 1, 0, 0, {IA64_OPND_AR3, IA64_OPND_R2}, 0, 0, NULL},
    {"nop",   IA64_TYPE_DYN, 0, 0, 0, {IA64_OPND_IMMU21}, 0, 0, NULL},
    {NULL, 0, 0, 0, 0, {0}, 0, 0, NULL}
  };
