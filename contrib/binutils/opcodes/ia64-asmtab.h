/* ia64-asmtab.h -- Header for compacted IA-64 opcode tables.
   Copyright 1999, 2000 Free Software Foundation, Inc.
   Contributed by Bob Manson of Cygnus Support <manson@cygnus.com>

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

#ifndef IA64_ASMTAB_H
#define IA64_ASMTAB_H

#include "opcode/ia64.h"

/* The primary opcode table is made up of the following: */
struct ia64_main_table
{
  /* The entry in the string table that corresponds to the name of this
     opcode. */
  unsigned short name_index;

  /* The type of opcode; corresponds to the TYPE field in 
     struct ia64_opcode. */
  unsigned char opcode_type;

  /* The number of outputs for this opcode. */
  unsigned char num_outputs;

  /* The base insn value for this opcode.  It may be modified by completers. */
  ia64_insn opcode;

  /* The mask of valid bits in OPCODE. Zeros indicate operand fields. */
  ia64_insn mask;

  /* The operands of this instruction.  Corresponds to the OPERANDS field
     in struct ia64_opcode. */
  unsigned char operands[5];

  /* The flags for this instruction.  Corresponds to the FLAGS field in
     struct ia64_opcode. */
  short flags;

  /* The tree of completers for this instruction; this is an offset into
     completer_table. */
  short completers;
};

/* Each instruction has a set of possible "completers", or additional
   suffixes that can alter the instruction's behavior, and which has
   potentially different dependencies.

   The completer entries modify certain bits in the instruction opcode.
   Which bits are to be modified are marked by the BITS, MASK and
   OFFSET fields.  The completer entry may also note dependencies for the
   opcode. 

   These completers are arranged in a DAG; the pointers are indexes
   into the completer_table array.  The completer DAG is searched by
   find_completer () and ia64_find_matching_opcode ().

   Note that each completer needs to be applied in turn, so that if we
   have the instruction
   	cmp.lt.unc
   the completer entries for both "lt" and "unc" would need to be applied
   to the opcode's value.

   Some instructions do not require any completers; these contain an
   empty completer entry.  Instructions that require a completer do
   not contain an empty entry.

   Terminal completers (those completers that validly complete an
   instruction) are marked by having the TERMINAL_COMPLETER flag set. 

   Only dependencies listed in the terminal completer for an opcode are
   considered to apply to that opcode instance. */

struct ia64_completer_table
{
  /* The bit value that this completer sets. */
  unsigned int bits;

  /* And its mask. 1s are bits that are to be modified in the 
     instruction. */
  unsigned int mask;

  /* The entry in the string table that corresponds to the name of this
     completer. */
  unsigned short name_index;

  /* An alternative completer, or -1 if this is the end of the chain. */
  short alternative;

  /* A pointer to the DAG of completers that can potentially follow
     this one, or -1. */
  short subentries;

  /* The bit offset in the instruction where BITS and MASK should be
     applied. */
  unsigned char offset : 7;

  unsigned char terminal_completer : 1;

  /* Index into the dependency list table */
  short dependencies;
};

/* This contains sufficient information for the disassembler to resolve
   the complete name of the original instruction.  */
struct ia64_dis_names 
{
  /* COMPLETER_INDEX represents the tree of completers that make up
     the instruction.  The LSB represents the top of the tree for the
     specified instruction. 

     A 0 bit indicates to go to the next alternate completer via the
     alternative field; a 1 bit indicates that the current completer
     is part of the instruction, and to go down the subentries index.
     We know we've reached the final completer when we run out of 1
     bits.

     There is always at least one 1 bit. */
  unsigned int completer_index : 20;

  /* The index in the main_table[] array for the instruction. */
  unsigned short insn_index : 11;

  /* If set, the next entry in this table is an alternate possibility
     for this instruction encoding.  Which one to use is determined by
     the instruction type and other factors (see opcode_verify ()).  */
  unsigned int next_flag : 1;

  /* The disassembly priority of this entry among instructions. */
  unsigned short priority;
};

#endif
