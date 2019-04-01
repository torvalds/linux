/* ppc-dis.c -- Disassemble PowerPC instructions
   Copyright (C) 1994-2016 Free Software Foundation, Inc.
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
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <asm/cputable.h>
#include <asm/cpu_has_feature.h>
#include "nonstdio.h"
#include "ansidecl.h"
#include "ppc.h"
#include "dis-asm.h"

/* This file provides several disassembler functions, all of which use
   the disassembler interface defined in dis-asm.h.  Several functions
   are provided because this file handles disassembly for the PowerPC
   in both big and little endian mode and also for the POWER (RS/6000)
   chip.  */

/* Extract the operand value from the PowerPC or POWER instruction.  */

static long
operand_value_powerpc (const struct powerpc_operand *operand,
		       unsigned long insn, ppc_cpu_t dialect)
{
  long value;
  int invalid;
  /* Extract the value from the instruction.  */
  if (operand->extract)
    value = (*operand->extract) (insn, dialect, &invalid);
  else
    {
      if (operand->shift >= 0)
	value = (insn >> operand->shift) & operand->bitm;
      else
	value = (insn << -operand->shift) & operand->bitm;
      if ((operand->flags & PPC_OPERAND_SIGNED) != 0)
	{
	  /* BITM is always some number of zeros followed by some
	     number of ones, followed by some number of zeros.  */
	  unsigned long top = operand->bitm;
	  /* top & -top gives the rightmost 1 bit, so this
	     fills in any trailing zeros.  */
	  top |= (top & -top) - 1;
	  top &= ~(top >> 1);
	  value = (value ^ top) - top;
	}
    }

  return value;
}

/* Determine whether the optional operand(s) should be printed.  */

static int
skip_optional_operands (const unsigned char *opindex,
			unsigned long insn, ppc_cpu_t dialect)
{
  const struct powerpc_operand *operand;

  for (; *opindex != 0; opindex++)
    {
      operand = &powerpc_operands[*opindex];
      if ((operand->flags & PPC_OPERAND_NEXT) != 0
	  || ((operand->flags & PPC_OPERAND_OPTIONAL) != 0
	      && operand_value_powerpc (operand, insn, dialect) !=
		 ppc_optional_operand_value (operand)))
	return 0;
    }

  return 1;
}

/* Find a match for INSN in the opcode table, given machine DIALECT.
   A DIALECT of -1 is special, matching all machine opcode variations.  */

static const struct powerpc_opcode *
lookup_powerpc (unsigned long insn, ppc_cpu_t dialect)
{
  const struct powerpc_opcode *opcode;
  const struct powerpc_opcode *opcode_end;

  opcode_end = powerpc_opcodes + powerpc_num_opcodes;
  /* Find the first match in the opcode table for this major opcode.  */
  for (opcode = powerpc_opcodes; opcode < opcode_end; ++opcode)
    {
      const unsigned char *opindex;
      const struct powerpc_operand *operand;
      int invalid;

      if ((insn & opcode->mask) != opcode->opcode
	  || (dialect != (ppc_cpu_t) -1
	      && ((opcode->flags & dialect) == 0
		  || (opcode->deprecated & dialect) != 0)))
	continue;

      /* Check validity of operands.  */
      invalid = 0;
      for (opindex = opcode->operands; *opindex != 0; opindex++)
	{
	  operand = powerpc_operands + *opindex;
	  if (operand->extract)
	    (*operand->extract) (insn, dialect, &invalid);
	}
      if (invalid)
	continue;

      return opcode;
    }

  return NULL;
}

/* Print a PowerPC or POWER instruction.  */

int print_insn_powerpc (unsigned long insn, unsigned long memaddr)
{
  const struct powerpc_opcode *opcode;
  bool insn_is_short;
  ppc_cpu_t dialect;

  dialect = PPC_OPCODE_PPC | PPC_OPCODE_COMMON
            | PPC_OPCODE_64 | PPC_OPCODE_POWER4 | PPC_OPCODE_ALTIVEC;

  if (cpu_has_feature(CPU_FTRS_POWER5))
    dialect |= PPC_OPCODE_POWER5;

  if (cpu_has_feature(CPU_FTRS_CELL))
    dialect |= (PPC_OPCODE_CELL | PPC_OPCODE_ALTIVEC);

  if (cpu_has_feature(CPU_FTRS_POWER6))
    dialect |= (PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6 | PPC_OPCODE_ALTIVEC);

  if (cpu_has_feature(CPU_FTRS_POWER7))
    dialect |= (PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6 | PPC_OPCODE_POWER7
                | PPC_OPCODE_ALTIVEC | PPC_OPCODE_VSX);

  if (cpu_has_feature(CPU_FTRS_POWER8))
    dialect |= (PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6 | PPC_OPCODE_POWER7
		| PPC_OPCODE_POWER8 | PPC_OPCODE_HTM
		| PPC_OPCODE_ALTIVEC | PPC_OPCODE_ALTIVEC2 | PPC_OPCODE_VSX);

  if (cpu_has_feature(CPU_FTRS_POWER9))
    dialect |= (PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6 | PPC_OPCODE_POWER7
		| PPC_OPCODE_POWER8 | PPC_OPCODE_POWER9 | PPC_OPCODE_HTM
		| PPC_OPCODE_ALTIVEC | PPC_OPCODE_ALTIVEC2
		| PPC_OPCODE_VSX | PPC_OPCODE_VSX3);

  /* Get the major opcode of the insn.  */
  opcode = NULL;
  insn_is_short = false;

  if (opcode == NULL)
    opcode = lookup_powerpc (insn, dialect);
  if (opcode == NULL && (dialect & PPC_OPCODE_ANY) != 0)
    opcode = lookup_powerpc (insn, (ppc_cpu_t) -1);

  if (opcode != NULL)
    {
      const unsigned char *opindex;
      const struct powerpc_operand *operand;
      int need_comma;
      int need_paren;
      int skip_optional;

      if (opcode->operands[0] != 0)
	printf("%-7s ", opcode->name);
      else
	printf("%s", opcode->name);

      if (insn_is_short)
        /* The operands will be fetched out of the 16-bit instruction.  */
        insn >>= 16;

      /* Now extract and print the operands.  */
      need_comma = 0;
      need_paren = 0;
      skip_optional = -1;
      for (opindex = opcode->operands; *opindex != 0; opindex++)
	{
	  long value;

	  operand = powerpc_operands + *opindex;

	  /* Operands that are marked FAKE are simply ignored.  We
	     already made sure that the extract function considered
	     the instruction to be valid.  */
	  if ((operand->flags & PPC_OPERAND_FAKE) != 0)
	    continue;

	  /* If all of the optional operands have the value zero,
	     then don't print any of them.  */
	  if ((operand->flags & PPC_OPERAND_OPTIONAL) != 0)
	    {
	      if (skip_optional < 0)
		skip_optional = skip_optional_operands (opindex, insn,
							dialect);
	      if (skip_optional)
		continue;
	    }

	  value = operand_value_powerpc (operand, insn, dialect);

	  if (need_comma)
	    {
	      printf(",");
	      need_comma = 0;
	    }

	  /* Print the operand as directed by the flags.  */
	  if ((operand->flags & PPC_OPERAND_GPR) != 0
	      || ((operand->flags & PPC_OPERAND_GPR_0) != 0 && value != 0))
	    printf("r%ld", value);
	  else if ((operand->flags & PPC_OPERAND_FPR) != 0)
	    printf("f%ld", value);
	  else if ((operand->flags & PPC_OPERAND_VR) != 0)
	    printf("v%ld", value);
	  else if ((operand->flags & PPC_OPERAND_VSR) != 0)
	    printf("vs%ld", value);
	  else if ((operand->flags & PPC_OPERAND_RELATIVE) != 0)
	    print_address(memaddr + value);
	  else if ((operand->flags & PPC_OPERAND_ABSOLUTE) != 0)
	    print_address(value & 0xffffffff);
	  else if ((operand->flags & PPC_OPERAND_FSL) != 0)
	    printf("fsl%ld", value);
	  else if ((operand->flags & PPC_OPERAND_FCR) != 0)
	    printf("fcr%ld", value);
	  else if ((operand->flags & PPC_OPERAND_UDI) != 0)
	    printf("%ld", value);
	  else if ((operand->flags & PPC_OPERAND_CR_REG) != 0
		   && (((dialect & PPC_OPCODE_PPC) != 0)
		       || ((dialect & PPC_OPCODE_VLE) != 0)))
	    printf("cr%ld", value);
	  else if (((operand->flags & PPC_OPERAND_CR_BIT) != 0)
		   && (((dialect & PPC_OPCODE_PPC) != 0)
		       || ((dialect & PPC_OPCODE_VLE) != 0)))
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
	  else
	    printf("%d", (int) value);

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

      /* We have found and printed an instruction.
         If it was a short VLE instruction we have more to do.  */
      if (insn_is_short)
        {
          memaddr += 2;
          return 2;
        }
      else
        /* Otherwise, return.  */
        return 4;
    }

  /* We could not find a match.  */
  printf(".long 0x%lx", insn);

  return 4;
}
