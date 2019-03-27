/* Disassembler structures definitions for the ARC.
   Copyright 1994, 1995, 1997, 1998, 2000, 2001
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef ARCDIS_H
#define ARCDIS_H

enum 
{
  BR_exec_when_no_jump,
  BR_exec_always,
  BR_exec_when_jump
};

enum Flow 
{
  noflow,
  direct_jump,
  direct_call,
  indirect_jump,
  indirect_call,
  invalid_instr
};

enum { no_reg = 99 };
enum { allOperandsSize = 256 };

struct arcDisState 
{
  void *_this;
  int instructionLen;
  void (*err)(void*, const char*);
  const char *(*coreRegName)(void*, int);
  const char *(*auxRegName)(void*, int);
  const char *(*condCodeName)(void*, int);
  const char *(*instName)(void*, int, int, int*);
  
  unsigned char* instruction;
  unsigned index;
  const char *comm[6]; /* instr name, cond, NOP, 3 operands  */
  int opWidth;
  int targets[4];
  int addresses[4];
  /* Set as a side-effect of calling the disassembler.
     Used only by the debugger.  */
  enum Flow flow;
  int register_for_indirect_jump;
  int ea_reg1, ea_reg2, _offset;
  int _cond, _opcode;
  unsigned long words[2];
  char *commentBuffer;
  char instrBuffer[40];
  char operandBuffer[allOperandsSize];
  char _ea_present;
  char _mem_load;
  char _load_len;
  char nullifyMode;
  unsigned char commNum;
  unsigned char isBranch;
  unsigned char tcnt;
  unsigned char acnt;
};

#define __TRANSLATION_REQUIRED(state) ((state).acnt != 0)

#endif
