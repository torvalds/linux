/* ARC target-dependent stuff. Extension data structures.
   Copyright 1995, 1997, 2000, 2001, 2005 Free Software Foundation, Inc.

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
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef ARCEXT_H
#define ARCEXT_H

enum {EXT_INSTRUCTION   = 0,
      EXT_CORE_REGISTER = 1,
      EXT_AUX_REGISTER  = 2,
      EXT_COND_CODE     = 3};

enum {NUM_EXT_INST = (0x1f-0x10+1) + (0x3f-0x09+1)};
enum {NUM_EXT_CORE = 59-32+1};
enum {NUM_EXT_COND = 0x1f-0x10+1};

struct ExtInstruction 
{
  char flags;
  char *name;
}; 

struct ExtAuxRegister 
{
  long address;
  char *name;
  struct ExtAuxRegister *next; 
};

struct arcExtMap 
{
  struct ExtAuxRegister *auxRegisters;
  struct ExtInstruction *instructions[NUM_EXT_INST];
  char *coreRegisters[NUM_EXT_CORE];
  char *condCodes[NUM_EXT_COND];
};

extern int arcExtMap_add(void*, unsigned long);
extern const char *arcExtMap_coreRegName(int);
extern const char *arcExtMap_auxRegName(long);
extern const char *arcExtMap_condCodeName(int);
extern const char *arcExtMap_instName(int, int, int*);
extern void build_ARC_extmap(bfd *);

#define IGNORE_FIRST_OPD 1

#endif
