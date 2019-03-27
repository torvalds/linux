/* ARC target-dependent stuff. Extension structure access functions
   Copyright 1995, 1997, 2000, 2001, 2004, 2005
   Free Software Foundation, Inc.

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

#include "sysdep.h"
#include <stdlib.h>
#include <stdio.h>
#include "bfd.h"
#include "arc-ext.h"
#include "libiberty.h"

/* Extension structure  */
static struct arcExtMap arc_extension_map;

/* Get the name of an extension instruction.  */

const char *
arcExtMap_instName(int opcode, int minor, int *flags)
{
    if (opcode == 3)
      {
	/* FIXME: ??? need to also check 0/1/2 in bit0 for (3f) brk/sleep/swi  */
	if (minor < 0x09 || minor == 0x3f)
	  return 0;
	else
	  opcode = 0x1f - 0x10 + minor - 0x09 + 1;
      }
    else
      if (opcode < 0x10)
	return 0;
    else
      opcode -= 0x10;
    if (!arc_extension_map.instructions[opcode])
      return 0;
    *flags = arc_extension_map.instructions[opcode]->flags;
    return arc_extension_map.instructions[opcode]->name;
}

/* Get the name of an extension core register.  */

const char *
arcExtMap_coreRegName(int value)
{
  if (value < 32)
    return 0;
  return arc_extension_map.coreRegisters[value-32];
}

/* Get the name of an extension condition code.  */

const char *
arcExtMap_condCodeName(int value)
{
  if (value < 16)
    return 0;
  return arc_extension_map.condCodes[value-16];
}

/* Get the name of an extension aux register.  */

const char *
arcExtMap_auxRegName(long address)
{
  /* walk the list of aux reg names and find the name  */
  struct ExtAuxRegister *r;

  for (r = arc_extension_map.auxRegisters; r; r = r->next) {
    if (r->address == address)
      return (const char *) r->name;
  }
  return 0;
}

/* Recursively free auxilliary register strcture pointers until
   the list is empty.  */

static void
clean_aux_registers(struct ExtAuxRegister *r)
{
  if (r -> next)
    {
      clean_aux_registers( r->next);
      free(r -> name);
      free(r -> next);
      r ->next = NULL;
    }
  else
    free(r -> name);
}

/* Free memory that has been allocated for the extensions.  */

static void
cleanup_ext_map(void)
{
  struct ExtAuxRegister *r;
  struct ExtInstruction *insn;
  int i;

  /* clean aux reg structure  */
  r = arc_extension_map.auxRegisters;
  if (r)
    {
      (clean_aux_registers(r));
      free(r);
    }

  /* clean instructions  */
  for (i = 0; i < NUM_EXT_INST; i++)
    {
      insn = arc_extension_map.instructions[i];
      if (insn)
	free(insn->name);
    }

  /* clean core reg struct  */
  for (i = 0; i < NUM_EXT_CORE; i++)
    {
      if (arc_extension_map.coreRegisters[i])
	free(arc_extension_map.coreRegisters[i]);
    }

  for (i = 0; i < NUM_EXT_COND; i++) {
    if (arc_extension_map.condCodes[i])
      free(arc_extension_map.condCodes[i]);
  }

  memset(&arc_extension_map, 0, sizeof(struct arcExtMap));
}

int
arcExtMap_add(void *base, unsigned long length)
{
  unsigned char *block = base;
  unsigned char *p = block;

  /* Clean up and reset everything if needed.  */
  cleanup_ext_map();

  while (p && p < (block + length))
    {
      /* p[0] == length of record
	 p[1] == type of record
	 For instructions:
	   p[2]  = opcode
	   p[3]  = minor opcode (if opcode == 3)
	   p[4]  = flags
	   p[5]+ = name
	 For core regs and condition codes:
	   p[2]  = value
	   p[3]+ = name
	 For aux regs:
	   p[2..5] = value
	   p[6]+   = name
	 (value is p[2]<<24|p[3]<<16|p[4]<<8|p[5])  */

      if (p[0] == 0)
	return -1;

      switch (p[1])
	{
	case EXT_INSTRUCTION:
	  {
	    char opcode = p[2];
	    char minor  = p[3];
	    char * insn_name = (char *) xmalloc(( (int)*p-5) * sizeof(char));
	    struct ExtInstruction * insn =
	      (struct ExtInstruction *) xmalloc(sizeof(struct ExtInstruction));

	    if (opcode==3)
	      opcode = 0x1f - 0x10 + minor - 0x09 + 1;
	    else
	      opcode -= 0x10;
	    insn -> flags = (char) *(p+4);
	    strcpy (insn_name, (char *) (p+5));
	    insn -> name = insn_name;
	    arc_extension_map.instructions[(int) opcode] = insn;
	  }
	  break;

	case EXT_CORE_REGISTER:
	  {
	    char * core_name = (char *) xmalloc(((int)*p-3) * sizeof(char));

	    strcpy(core_name, (char *) (p+3));
	    arc_extension_map.coreRegisters[p[2]-32] = core_name;
	  }
	  break;

	case EXT_COND_CODE:
	  {
	    char * cc_name = (char *) xmalloc( ((int)*p-3) * sizeof(char));
	    strcpy(cc_name, (char *) (p+3));
	    arc_extension_map.condCodes[p[2]-16] = cc_name;
	  }
	  break;

	case EXT_AUX_REGISTER:
	  {
	    /* trickier -- need to store linked list to these  */
	    struct ExtAuxRegister *newAuxRegister =
	      (struct ExtAuxRegister *)malloc(sizeof(struct ExtAuxRegister));
	    char * aux_name = (char *) xmalloc ( ((int)*p-6) * sizeof(char));

	    strcpy (aux_name, (char *) (p+6));
	    newAuxRegister->name = aux_name;
	    newAuxRegister->address = p[2]<<24 | p[3]<<16 | p[4]<<8  | p[5];
	    newAuxRegister->next = arc_extension_map.auxRegisters;
	    arc_extension_map.auxRegisters = newAuxRegister;
	  }
	  break;

	default:
	  return -1;

	}
      p += p[0]; /* move to next record  */
    }

  return 0;
}

/* Load hw extension descibed in .extArcMap ELF section.  */

void
build_ARC_extmap (text_bfd)
  bfd *text_bfd;
{
  char *arcExtMap;
  bfd_size_type count;
  asection *p;

  for (p = text_bfd->sections; p != NULL; p = p->next)
    if (!strcmp (p->name, ".arcextmap"))
      {
        count = bfd_get_section_size (p);
        arcExtMap = (char *) xmalloc (count);
        if (bfd_get_section_contents (text_bfd, p, (PTR) arcExtMap, 0, count))
          {
            arcExtMap_add ((PTR) arcExtMap, count);
            break;
          }
        free ((PTR) arcExtMap);
      }
}
