// SPDX-License-Identifier: GPL-2.0-or-later
/* Disassemble SPU instructions

   Copyright 2006 Free Software Foundation, Inc.

   This file is part of GDB, GAS, and the GNU binutils.

 */

#include <linux/string.h>
#include "nonstdio.h"
#include "ansidecl.h"
#include "spu.h"
#include "dis-asm.h"

/* This file provides a disassembler function which uses
   the disassembler interface defined in dis-asm.h.   */

extern const struct spu_opcode spu_opcodes[];
extern const int spu_num_opcodes;

#define SPU_DISASM_TBL_SIZE (1 << 11)
static const struct spu_opcode *spu_disassemble_table[SPU_DISASM_TBL_SIZE];

static void
init_spu_disassemble (void)
{
  int i;

  /* If two instructions have the same opcode then we prefer the first
   * one.  In most cases it is just an alternate mnemonic. */
  for (i = 0; i < spu_num_opcodes; i++)
    {
      int o = spu_opcodes[i].opcode;
      if (o >= SPU_DISASM_TBL_SIZE)
	continue; /* abort (); */
      if (spu_disassemble_table[o] == 0)
	spu_disassemble_table[o] = &spu_opcodes[i];
    }
}

/* Determine the instruction from the 10 least significant bits. */
static const struct spu_opcode *
get_index_for_opcode (unsigned int insn)
{
  const struct spu_opcode *index;
  unsigned int opcode = insn >> (32-11);

  /* Init the table.  This assumes that element 0/opcode 0 (currently
   * NOP) is always used */
  if (spu_disassemble_table[0] == 0)
    init_spu_disassemble ();

  if ((index = spu_disassemble_table[opcode & 0x780]) != 0
      && index->insn_type == RRR)
    return index;

  if ((index = spu_disassemble_table[opcode & 0x7f0]) != 0
      && (index->insn_type == RI18 || index->insn_type == LBT))
    return index;

  if ((index = spu_disassemble_table[opcode & 0x7f8]) != 0
      && index->insn_type == RI10)
    return index;

  if ((index = spu_disassemble_table[opcode & 0x7fc]) != 0
      && (index->insn_type == RI16))
    return index;

  if ((index = spu_disassemble_table[opcode & 0x7fe]) != 0
      && (index->insn_type == RI8))
    return index;

  if ((index = spu_disassemble_table[opcode & 0x7ff]) != 0)
    return index;

  return NULL;
}

/* Print a Spu instruction.  */

int
print_insn_spu (unsigned long insn, unsigned long memaddr)
{
  int value;
  int hex_value;
  const struct spu_opcode *index;
  enum spu_insns tag;

  index = get_index_for_opcode (insn);

  if (index == 0)
    {
      printf(".long 0x%lx", insn);
    }
  else
    {
      int i;
      int paren = 0;
      tag = (enum spu_insns)(index - spu_opcodes);
      printf("%s", index->mnemonic);
      if (tag == M_BI || tag == M_BISL || tag == M_IRET || tag == M_BISLED
	  || tag == M_BIHNZ || tag == M_BIHZ || tag == M_BINZ || tag == M_BIZ
          || tag == M_SYNC || tag == M_HBR)
	{
	  int fb = (insn >> (32-18)) & 0x7f;
	  if (fb & 0x40)
	    printf(tag == M_SYNC ? "c" : "p");
	  if (fb & 0x20)
	    printf("d");
	  if (fb & 0x10)
	    printf("e");
	}
      if (index->arg[0] != 0)
	printf("\t");
      hex_value = 0;
      for (i = 1;  i <= index->arg[0]; i++)
	{
	  int arg = index->arg[i];
	  if (arg != A_P && !paren && i > 1)
	    printf(",");

	  switch (arg)
	    {
	    case A_T:
	      printf("$%lu",
				     DECODE_INSN_RT (insn));
	      break;
	    case A_A:
	      printf("$%lu",
				     DECODE_INSN_RA (insn));
	      break;
	    case A_B:
	      printf("$%lu",
				     DECODE_INSN_RB (insn));
	      break;
	    case A_C:
	      printf("$%lu",
				     DECODE_INSN_RC (insn));
	      break;
	    case A_S:
	      printf("$sp%lu",
				     DECODE_INSN_RA (insn));
	      break;
	    case A_H:
	      printf("$ch%lu",
				     DECODE_INSN_RA (insn));
	      break;
	    case A_P:
	      paren++;
	      printf("(");
	      break;
	    case A_U7A:
	      printf("%lu",
				     173 - DECODE_INSN_U8 (insn));
	      break;
	    case A_U7B:
	      printf("%lu",
				     155 - DECODE_INSN_U8 (insn));
	      break;
	    case A_S3:
	    case A_S6:
	    case A_S7:
	    case A_S7N:
	    case A_U3:
	    case A_U5:
	    case A_U6:
	    case A_U7:
	      hex_value = DECODE_INSN_I7 (insn);
	      printf("%d", hex_value);
	      break;
	    case A_S11:
	      print_address(memaddr + DECODE_INSN_I9a (insn) * 4);
	      break;
	    case A_S11I:
	      print_address(memaddr + DECODE_INSN_I9b (insn) * 4);
	      break;
	    case A_S10:
	    case A_S10B:
	      hex_value = DECODE_INSN_I10 (insn);
	      printf("%d", hex_value);
	      break;
	    case A_S14:
	      hex_value = DECODE_INSN_I10 (insn) * 16;
	      printf("%d", hex_value);
	      break;
	    case A_S16:
	      hex_value = DECODE_INSN_I16 (insn);
	      printf("%d", hex_value);
	      break;
	    case A_X16:
	      hex_value = DECODE_INSN_U16 (insn);
	      printf("%u", hex_value);
	      break;
	    case A_R18:
	      value = DECODE_INSN_I16 (insn) * 4;
	      if (value == 0)
		printf("%d", value);
	      else
		{
		  hex_value = memaddr + value;
		  print_address(hex_value & 0x3ffff);
		}
	      break;
	    case A_S18:
	      value = DECODE_INSN_U16 (insn) * 4;
	      if (value == 0)
		printf("%d", value);
	      else
		print_address(value);
	      break;
	    case A_U18:
	      value = DECODE_INSN_U18 (insn);
	      if (value == 0 || 1)
		{
		  hex_value = value;
		  printf("%u", value);
		}
	      else
		print_address(value);
	      break;
	    case A_U14:
	      hex_value = DECODE_INSN_U14 (insn);
	      printf("%u", hex_value);
	      break;
	    }
	  if (arg != A_P && paren)
	    {
	      printf(")");
	      paren--;
	    }
	}
      if (hex_value > 16)
	printf("\t# %x", hex_value);
    }
  return 4;
}
