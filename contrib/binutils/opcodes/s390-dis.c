/* s390-dis.c -- Disassemble S390 instructions
   Copyright 2000, 2001, 2002, 2003, 2005 Free Software Foundation, Inc.
   Contributed by Martin Schwidefsky (schwidefsky@de.ibm.com).

   This file is part of GDB, GAS and the GNU binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include <stdio.h>
#include "ansidecl.h"
#include "sysdep.h"
#include "dis-asm.h"
#include "opcode/s390.h"

static int init_flag = 0;
static int opc_index[256];
static int current_arch_mask = 0;

/* Set up index table for first opcode byte.  */

static void
init_disasm (struct disassemble_info *info)
{
  const struct s390_opcode *opcode;
  const struct s390_opcode *opcode_end;

  memset (opc_index, 0, sizeof (opc_index));
  opcode_end = s390_opcodes + s390_num_opcodes;
  for (opcode = s390_opcodes; opcode < opcode_end; opcode++)
    {
      opc_index[(int) opcode->opcode[0]] = opcode - s390_opcodes;
      while ((opcode < opcode_end) &&
	     (opcode[1].opcode[0] == opcode->opcode[0]))
	opcode++;
    }
  switch (info->mach)
    {
    case bfd_mach_s390_31:
      current_arch_mask = 1 << S390_OPCODE_ESA;
      break;
    case bfd_mach_s390_64:
      current_arch_mask = 1 << S390_OPCODE_ZARCH;
      break;
    default:
      abort ();
    }
  init_flag = 1;
}

/* Extracts an operand value from an instruction.  */

static inline unsigned int
s390_extract_operand (unsigned char *insn, const struct s390_operand *operand)
{
  unsigned int val;
  int bits;

  /* Extract fragments of the operand byte for byte.  */
  insn += operand->shift / 8;
  bits = (operand->shift & 7) + operand->bits;
  val = 0;
  do
    {
      val <<= 8;
      val |= (unsigned int) *insn++;
      bits -= 8;
    }
  while (bits > 0);
  val >>= -bits;
  val &= ((1U << (operand->bits - 1)) << 1) - 1;

  /* Check for special long displacement case.  */
  if (operand->bits == 20 && operand->shift == 20)
    val = (val & 0xff) << 12 | (val & 0xfff00) >> 8;

  /* Sign extend value if the operand is signed or pc relative.  */
  if ((operand->flags & (S390_OPERAND_SIGNED | S390_OPERAND_PCREL))
      && (val & (1U << (operand->bits - 1))))
    val |= (-1U << (operand->bits - 1)) << 1;

  /* Double value if the operand is pc relative.  */
  if (operand->flags & S390_OPERAND_PCREL)
    val <<= 1;

  /* Length x in an instructions has real length x + 1.  */
  if (operand->flags & S390_OPERAND_LENGTH)
    val++;
  return val;
}

/* Print a S390 instruction.  */

int
print_insn_s390 (bfd_vma memaddr, struct disassemble_info *info)
{
  bfd_byte buffer[6];
  const struct s390_opcode *opcode;
  const struct s390_opcode *opcode_end;
  unsigned int value;
  int status, opsize, bufsize;
  char separator;

  if (init_flag == 0)
    init_disasm (info);

  /* The output looks better if we put 6 bytes on a line.  */
  info->bytes_per_line = 6;

  /* Every S390 instruction is max 6 bytes long.  */
  memset (buffer, 0, 6);
  status = (*info->read_memory_func) (memaddr, buffer, 6, info);
  if (status != 0)
    {
      for (bufsize = 0; bufsize < 6; bufsize++)
	if ((*info->read_memory_func) (memaddr, buffer, bufsize + 1, info) != 0)
	  break;
      if (bufsize <= 0)
	{
	  (*info->memory_error_func) (status, memaddr, info);
	  return -1;
	}
      /* Opsize calculation looks strange but it works
	 00xxxxxx -> 2 bytes, 01xxxxxx/10xxxxxx -> 4 bytes,
	 11xxxxxx -> 6 bytes.  */
      opsize = ((((buffer[0] >> 6) + 1) >> 1) + 1) << 1;
      status = opsize > bufsize;
    }
  else
    {
      bufsize = 6;
      opsize = ((((buffer[0] >> 6) + 1) >> 1) + 1) << 1;
    }

  if (status == 0)
    {
      /* Find the first match in the opcode table.  */
      opcode_end = s390_opcodes + s390_num_opcodes;
      for (opcode = s390_opcodes + opc_index[(int) buffer[0]];
	   (opcode < opcode_end) && (buffer[0] == opcode->opcode[0]);
	   opcode++)
	{
	  const struct s390_operand *operand;
	  const unsigned char *opindex;

	  /* Check architecture.  */
	  if (!(opcode->modes & current_arch_mask))
	    continue;
	  /* Check signature of the opcode.  */
	  if ((buffer[1] & opcode->mask[1]) != opcode->opcode[1]
	      || (buffer[2] & opcode->mask[2]) != opcode->opcode[2]
	      || (buffer[3] & opcode->mask[3]) != opcode->opcode[3]
	      || (buffer[4] & opcode->mask[4]) != opcode->opcode[4]
	      || (buffer[5] & opcode->mask[5]) != opcode->opcode[5])
	    continue;

	  /* The instruction is valid.  */
	  if (opcode->operands[0] != 0)
	    (*info->fprintf_func) (info->stream, "%s\t", opcode->name);
	  else
	    (*info->fprintf_func) (info->stream, "%s", opcode->name);

	  /* Extract the operands.  */
	  separator = 0;
	  for (opindex = opcode->operands; *opindex != 0; opindex++)
	    {
	      unsigned int value;

	      operand = s390_operands + *opindex;
	      value = s390_extract_operand (buffer, operand);

	      if ((operand->flags & S390_OPERAND_INDEX) && value == 0)
		continue;
	      if ((operand->flags & S390_OPERAND_BASE) &&
		  value == 0 && separator == '(')
		{
		  separator = ',';
		  continue;
		}

	      if (separator)
		(*info->fprintf_func) (info->stream, "%c", separator);

	      if (operand->flags & S390_OPERAND_GPR)
		(*info->fprintf_func) (info->stream, "%%r%i", value);
	      else if (operand->flags & S390_OPERAND_FPR)
		(*info->fprintf_func) (info->stream, "%%f%i", value);
	      else if (operand->flags & S390_OPERAND_AR)
		(*info->fprintf_func) (info->stream, "%%a%i", value);
	      else if (operand->flags & S390_OPERAND_CR)
		(*info->fprintf_func) (info->stream, "%%c%i", value);
	      else if (operand->flags & S390_OPERAND_PCREL)
		(*info->print_address_func) (memaddr + (int) value, info);
	      else if (operand->flags & S390_OPERAND_SIGNED)
		(*info->fprintf_func) (info->stream, "%i", (int) value);
	      else
		(*info->fprintf_func) (info->stream, "%u", value);

	      if (operand->flags & S390_OPERAND_DISP)
		{
		  separator = '(';
		}
	      else if (operand->flags & S390_OPERAND_BASE)
		{
		  (*info->fprintf_func) (info->stream, ")");
		  separator = ',';
		}
	      else
		separator = ',';
	    }

	  /* Found instruction, printed it, return its size.  */
	  return opsize;
	}
      /* No matching instruction found, fall through to hex print.  */
    }

  if (bufsize >= 4)
    {
      value = (unsigned int) buffer[0];
      value = (value << 8) + (unsigned int) buffer[1];
      value = (value << 8) + (unsigned int) buffer[2];
      value = (value << 8) + (unsigned int) buffer[3];
      (*info->fprintf_func) (info->stream, ".long\t0x%08x", value);
      return 4;
    }
  else if (bufsize >= 2)
    {
      value = (unsigned int) buffer[0];
      value = (value << 8) + (unsigned int) buffer[1];
      (*info->fprintf_func) (info->stream, ".short\t0x%04x", value);
      return 2;
    }
  else
    {
      value = (unsigned int) buffer[0];
      (*info->fprintf_func) (info->stream, ".byte\t0x%02x", value);
      return 1;
    }
}
