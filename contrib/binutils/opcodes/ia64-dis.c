/* ia64-dis.c -- Disassemble ia64 instructions
   Copyright 1998, 1999, 2000, 2002 Free Software Foundation, Inc.
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

#include <assert.h>
#include <string.h>

#include "dis-asm.h"
#include "opcode/ia64.h"

#define NELEMS(a)	((int) (sizeof (a) / sizeof (a[0])))

/* Disassemble ia64 instruction.  */

/* Return the instruction type for OPCODE found in unit UNIT. */

static enum ia64_insn_type
unit_to_type (ia64_insn opcode, enum ia64_unit unit)
{
  enum ia64_insn_type type;
  int op;

  op = IA64_OP (opcode);

  if (op >= 8 && (unit == IA64_UNIT_I || unit == IA64_UNIT_M))
    {
      type = IA64_TYPE_A;
    }
  else
    {
      switch (unit)
	{
	case IA64_UNIT_I:
	  type = IA64_TYPE_I; break;
	case IA64_UNIT_M:
	  type = IA64_TYPE_M; break;
	case IA64_UNIT_B:
	  type = IA64_TYPE_B; break;
	case IA64_UNIT_F:
	  type = IA64_TYPE_F; break;
        case IA64_UNIT_L:
	case IA64_UNIT_X:
	  type = IA64_TYPE_X; break;
	default:
	  type = -1;
	}
    }
  return type;
}

int
print_insn_ia64 (bfd_vma memaddr, struct disassemble_info *info)
{
  ia64_insn t0, t1, slot[3], template, s_bit, insn;
  int slotnum, j, status, need_comma, retval, slot_multiplier;
  const struct ia64_operand *odesc;
  const struct ia64_opcode *idesc;
  const char *err, *str, *tname;
  BFD_HOST_U_64_BIT value;
  bfd_byte bundle[16];
  enum ia64_unit unit;
  char regname[16];

  if (info->bytes_per_line == 0)
    info->bytes_per_line = 6;
  info->display_endian = info->endian;

  slot_multiplier = info->bytes_per_line;
  retval = slot_multiplier;

  slotnum = (((long) memaddr) & 0xf) / slot_multiplier;
  if (slotnum > 2)
    return -1;

  memaddr -= (memaddr & 0xf);
  status = (*info->read_memory_func) (memaddr, bundle, sizeof (bundle), info);
  if (status != 0)
    {
      (*info->memory_error_func) (status, memaddr, info);
      return -1;
    }
  /* bundles are always in little-endian byte order */
  t0 = bfd_getl64 (bundle);
  t1 = bfd_getl64 (bundle + 8);
  s_bit = t0 & 1;
  template = (t0 >> 1) & 0xf;
  slot[0] = (t0 >>  5) & 0x1ffffffffffLL;
  slot[1] = ((t0 >> 46) & 0x3ffff) | ((t1 & 0x7fffff) << 18);
  slot[2] = (t1 >> 23) & 0x1ffffffffffLL;

  tname = ia64_templ_desc[template].name;
  if (slotnum == 0)
    (*info->fprintf_func) (info->stream, "[%s] ", tname);
  else
    (*info->fprintf_func) (info->stream, "      ");

  unit = ia64_templ_desc[template].exec_unit[slotnum];

  if (template == 2 && slotnum == 1)
    {
      /* skip L slot in MLI template: */
      slotnum = 2;
      retval += slot_multiplier;
    }

  insn = slot[slotnum];

  if (unit == IA64_UNIT_NIL)
    goto decoding_failed;

  idesc = ia64_dis_opcode (insn, unit_to_type (insn, unit));
  if (idesc == NULL)
    goto decoding_failed;

  /* print predicate, if any: */

  if ((idesc->flags & IA64_OPCODE_NO_PRED)
      || (insn & 0x3f) == 0)
    (*info->fprintf_func) (info->stream, "      ");
  else
    (*info->fprintf_func) (info->stream, "(p%02d) ", (int)(insn & 0x3f));

  /* now the actual instruction: */

  (*info->fprintf_func) (info->stream, "%s", idesc->name);
  if (idesc->operands[0])
    (*info->fprintf_func) (info->stream, " ");

  need_comma = 0;
  for (j = 0; j < NELEMS (idesc->operands) && idesc->operands[j]; ++j)
    {
      odesc = elf64_ia64_operands + idesc->operands[j];

      if (need_comma)
	(*info->fprintf_func) (info->stream, ",");

      if (odesc - elf64_ia64_operands == IA64_OPND_IMMU64)
	{
	  /* special case of 64 bit immediate load: */
	  value = ((insn >> 13) & 0x7f) | (((insn >> 27) & 0x1ff) << 7)
	    | (((insn >> 22) & 0x1f) << 16) | (((insn >> 21) & 0x1) << 21)
	    | (slot[1] << 22) | (((insn >> 36) & 0x1) << 63);
	}
      else if (odesc - elf64_ia64_operands == IA64_OPND_IMMU62)
        {
          /* 62-bit immediate for nop.x/break.x */
          value = ((slot[1] & 0x1ffffffffffLL) << 21)
            | (((insn >> 36) & 0x1) << 20)
            | ((insn >> 6) & 0xfffff);
        }
      else if (odesc - elf64_ia64_operands == IA64_OPND_TGT64)
	{
	  /* 60-bit immediate for long branches. */
	  value = (((insn >> 13) & 0xfffff)
		   | (((insn >> 36) & 1) << 59)
		   | (((slot[1] >> 2) & 0x7fffffffffLL) << 20)) << 4;
	}
      else
	{
	  err = (*odesc->extract) (odesc, insn, &value);
	  if (err)
	    {
	      (*info->fprintf_func) (info->stream, "%s", err);
	      goto done;
	    }
	}

	switch (odesc->class)
	  {
	  case IA64_OPND_CLASS_CST:
	    (*info->fprintf_func) (info->stream, "%s", odesc->str);
	    break;

	  case IA64_OPND_CLASS_REG:
	    if (odesc->str[0] == 'a' && odesc->str[1] == 'r')
	      {
		switch (value)
		  {
		  case 0: case 1: case 2: case 3:
		  case 4: case 5: case 6: case 7:
		    sprintf (regname, "ar.k%u", (unsigned int) value);
		    break;
		  case 16:	strcpy (regname, "ar.rsc"); break;
		  case 17:	strcpy (regname, "ar.bsp"); break;
		  case 18:	strcpy (regname, "ar.bspstore"); break;
		  case 19:	strcpy (regname, "ar.rnat"); break;
		  case 32:	strcpy (regname, "ar.ccv"); break;
		  case 36:	strcpy (regname, "ar.unat"); break;
		  case 40:	strcpy (regname, "ar.fpsr"); break;
		  case 44:	strcpy (regname, "ar.itc"); break;
		  case 64:	strcpy (regname, "ar.pfs"); break;
		  case 65:	strcpy (regname, "ar.lc"); break;
		  case 66:	strcpy (regname, "ar.ec"); break;
		  default:
		    sprintf (regname, "ar%u", (unsigned int) value);
		    break;
		  }
		(*info->fprintf_func) (info->stream, "%s", regname);
	      }
	    else
	      (*info->fprintf_func) (info->stream, "%s%d", odesc->str, (int)value);
	    break;

	  case IA64_OPND_CLASS_IND:
	    (*info->fprintf_func) (info->stream, "%s[r%d]", odesc->str, (int)value);
	    break;

	  case IA64_OPND_CLASS_ABS:
	    str = 0;
	    if (odesc - elf64_ia64_operands == IA64_OPND_MBTYPE4)
	      switch (value)
		{
		case 0x0: str = "@brcst"; break;
		case 0x8: str = "@mix"; break;
		case 0x9: str = "@shuf"; break;
		case 0xa: str = "@alt"; break;
		case 0xb: str = "@rev"; break;
		}

	    if (str)
	      (*info->fprintf_func) (info->stream, "%s", str);
	    else if (odesc->flags & IA64_OPND_FLAG_DECIMAL_SIGNED)
	      (*info->fprintf_func) (info->stream, "%lld", (long long) value);
	    else if (odesc->flags & IA64_OPND_FLAG_DECIMAL_UNSIGNED)
	      (*info->fprintf_func) (info->stream, "%llu", (long long) value);
	    else
	      (*info->fprintf_func) (info->stream, "0x%llx", (long long) value);
	    break;

	  case IA64_OPND_CLASS_REL:
	    (*info->print_address_func) (memaddr + value, info);
	    break;
	  }

      need_comma = 1;
      if (j + 1 == idesc->num_outputs)
	{
	  (*info->fprintf_func) (info->stream, "=");
	  need_comma = 0;
	}
    }
  if (slotnum + 1 == ia64_templ_desc[template].group_boundary 
      || ((slotnum == 2) && s_bit))
    (*info->fprintf_func) (info->stream, ";;");

 done:
  ia64_free_opcode ((struct ia64_opcode *)idesc);
 failed:
  if (slotnum == 2)
    retval += 16 - 3*slot_multiplier;
  return retval;

 decoding_failed:
  (*info->fprintf_func) (info->stream, "      data8 %#011llx", (long long) insn);
  goto failed;
}
