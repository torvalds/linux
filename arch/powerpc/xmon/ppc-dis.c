/* ppc-dis.c -- Disassemble PowerPC instructions
   Copyright 1994 Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Support

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
along with this file; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "nonstdio.h"
#include "ansidecl.h"
#include "ppc.h"

extern void print_address (unsigned long memaddr);

/* Print a PowerPC or POWER instruction.  */

int
print_insn_powerpc (unsigned long insn, unsigned long memaddr, int dialect)
{
  const struct powerpc_opcode *opcode;
  const struct powerpc_opcode *opcode_end;
  unsigned long op;

  if (dialect == 0)
    dialect = PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC | PPC_OPCODE_COMMON
	      | PPC_OPCODE_64 | PPC_OPCODE_POWER4 | PPC_OPCODE_ALTIVEC;

  /* Get the major opcode of the instruction.  */
  op = PPC_OP (insn);

  /* Find the first match in the opcode table.  We could speed this up
     a bit by doing a binary search on the major opcode.  */
  opcode_end = powerpc_opcodes + powerpc_num_opcodes;
 again:
  for (opcode = powerpc_opcodes; opcode < opcode_end; opcode++)
    {
      unsigned long table_op;
      const unsigned char *opindex;
      const struct powerpc_operand *operand;
      int invalid;
      int need_comma;
      int need_paren;

      table_op = PPC_OP (opcode->opcode);
      if (op < table_op)
	break;
      if (op > table_op)
	continue;

      if ((insn & opcode->mask) != opcode->opcode
	  || (opcode->flags & dialect) == 0)
	continue;

      /* Make two passes over the operands.  First see if any of them
	 have extraction functions, and, if they do, make sure the
	 instruction is valid.  */
      invalid = 0;
      for (opindex = opcode->operands; *opindex != 0; opindex++)
	{
	  operand = powerpc_operands + *opindex;
	  if (operand->extract)
	    (*operand->extract) (insn, dialect, &invalid);
	}
      if (invalid)
	continue;

      /* The instruction is valid.  */
      printf("%s", opcode->name);
      if (opcode->operands[0] != 0)
	printf("\t");

      /* Now extract and print the operands.  */
      need_comma = 0;
      need_paren = 0;
      for (opindex = opcode->operands; *opindex != 0; opindex++)
	{
	  long value;

	  operand = powerpc_operands + *opindex;

	  /* Operands that are marked FAKE are simply ignored.  We
	     already made sure that the extract function considered
	     the instruction to be valid.  */
	  if ((operand->flags & PPC_OPERAND_FAKE) != 0)
	    continue;

	  /* Extract the value from the instruction.  */
	  if (operand->extract)
	    value = (*operand->extract) (insn, dialect, &invalid);
	  else
	    {
	      value = (insn >> operand->shift) & ((1 << operand->bits) - 1);
	      if ((operand->flags & PPC_OPERAND_SIGNED) != 0
		  && (value & (1 << (operand->bits - 1))) != 0)
		value -= 1 << operand->bits;
	    }

	  /* If the operand is optional, and the value is zero, don't
	     print anything.  */
	  if ((operand->flags & PPC_OPERAND_OPTIONAL) != 0
	      && (operand->flags & PPC_OPERAND_NEXT) == 0
	      && value == 0)
	    continue;

	  if (need_comma)
	    {
	      printf(",");
	      need_comma = 0;
	    }

	  /* Print the operand as directed by the flags.  */
	  if ((operand->flags & PPC_OPERAND_GPR) != 0)
	    printf("r%ld", value);
	  else if ((operand->flags & PPC_OPERAND_FPR) != 0)
	    printf("f%ld", value);
	  else if ((operand->flags & PPC_OPERAND_VR) != 0)
	    printf("v%ld", value);
	  else if ((operand->flags & PPC_OPERAND_RELATIVE) != 0)
	    print_address (memaddr + value);
	  else if ((operand->flags & PPC_OPERAND_ABSOLUTE) != 0)
	    print_address (value & 0xffffffff);
	  else if ((operand->flags & PPC_OPERAND_CR) == 0
		   || (dialect & PPC_OPCODE_PPC) == 0)
	    printf("%ld", value);
	  else
	    {
	      if (operand->bits == 3)
		printf("cr%d", value);
	      else
		{
		  static const char *cbnames[4] = { "lt", "gt", "eq", "so" };
		  int cr;
		  int cc;

		  cr = value >> 2;
		  if (cr != 0)
		    printf("4*cr%d+", cr);
		  cc = value & 3;
		  printf("%s", cbnames[cc]);
		}
	    }

	  if (need_paren)
	    {
	      printf(")");
	      need_paren = 0;
	    }

	  if ((operand->flags & PPC_OPERAND_PARENS) == 0)
	    need_comma = 1;
	  else
	    {
	      printf("(");
	      need_paren = 1;
	    }
	}

      /* We have found and printed an instruction; return.  */
      return 4;
    }

  if ((dialect & PPC_OPCODE_ANY) != 0)
    {
      dialect = ~PPC_OPCODE_ANY;
      goto again;
    }

  /* We could not find a match.  */
  printf(".long 0x%lx", insn);

  return 4;
}
