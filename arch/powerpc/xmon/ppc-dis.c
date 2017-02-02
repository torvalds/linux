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

#include "sysdep.h"
#include <stdio.h>
#include "dis-asm.h"
#include "elf-bfd.h"
#include "elf/ppc.h"
#include "opintl.h"
#include "opcode/ppc.h"

/* This file provides several disassembler functions, all of which use
   the disassembler interface defined in dis-asm.h.  Several functions
   are provided because this file handles disassembly for the PowerPC
   in both big and little endian mode and also for the POWER (RS/6000)
   chip.  */
static int print_insn_powerpc (bfd_vma, struct disassemble_info *, int,
			       ppc_cpu_t);

struct dis_private
{
  /* Stash the result of parsing disassembler_options here.  */
  ppc_cpu_t dialect;
} private;

#define POWERPC_DIALECT(INFO) \
  (((struct dis_private *) ((INFO)->private_data))->dialect)

struct ppc_mopt {
  const char *opt;
  ppc_cpu_t cpu;
  ppc_cpu_t sticky;
};

struct ppc_mopt ppc_opts[] = {
  { "403",     PPC_OPCODE_PPC | PPC_OPCODE_403,
    0 },
  { "405",     PPC_OPCODE_PPC | PPC_OPCODE_403 | PPC_OPCODE_405,
    0 },
  { "440",     (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_440
		| PPC_OPCODE_ISEL | PPC_OPCODE_RFMCI),
    0 },
  { "464",     (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_440
		| PPC_OPCODE_ISEL | PPC_OPCODE_RFMCI),
    0 },
  { "476",     (PPC_OPCODE_PPC | PPC_OPCODE_ISEL | PPC_OPCODE_440
		| PPC_OPCODE_476 | PPC_OPCODE_POWER4 | PPC_OPCODE_POWER5),
    0 },
  { "601",     PPC_OPCODE_PPC | PPC_OPCODE_601,
    0 },
  { "603",     PPC_OPCODE_PPC,
    0 },
  { "604",     PPC_OPCODE_PPC,
    0 },
  { "620",     PPC_OPCODE_PPC | PPC_OPCODE_64,
    0 },
  { "7400",    PPC_OPCODE_PPC | PPC_OPCODE_ALTIVEC,
    0 },
  { "7410",    PPC_OPCODE_PPC | PPC_OPCODE_ALTIVEC,
    0 },
  { "7450",    PPC_OPCODE_PPC | PPC_OPCODE_7450 | PPC_OPCODE_ALTIVEC,
    0 },
  { "7455",    PPC_OPCODE_PPC | PPC_OPCODE_ALTIVEC,
    0 },
  { "750cl",   PPC_OPCODE_PPC | PPC_OPCODE_750 | PPC_OPCODE_PPCPS
    , 0 },
  { "821",     PPC_OPCODE_PPC | PPC_OPCODE_860,
    0 },
  { "850",     PPC_OPCODE_PPC | PPC_OPCODE_860,
    0 },
  { "860",     PPC_OPCODE_PPC | PPC_OPCODE_860,
    0 },
  { "a2",      (PPC_OPCODE_PPC | PPC_OPCODE_ISEL | PPC_OPCODE_POWER4
		| PPC_OPCODE_POWER5 | PPC_OPCODE_CACHELCK | PPC_OPCODE_64
		| PPC_OPCODE_A2),
    0 },
  { "altivec", PPC_OPCODE_PPC,
    PPC_OPCODE_ALTIVEC | PPC_OPCODE_ALTIVEC2 },
  { "any",     0,
    PPC_OPCODE_ANY },
  { "booke",   PPC_OPCODE_PPC | PPC_OPCODE_BOOKE,
    0 },
  { "booke32", PPC_OPCODE_PPC | PPC_OPCODE_BOOKE,
    0 },
  { "cell",    (PPC_OPCODE_PPC | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		| PPC_OPCODE_CELL | PPC_OPCODE_ALTIVEC),
    0 },
  { "com",     PPC_OPCODE_COMMON,
    0 },
  { "e200z4",  (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE| PPC_OPCODE_SPE
		| PPC_OPCODE_ISEL | PPC_OPCODE_EFS | PPC_OPCODE_BRLOCK
		| PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK | PPC_OPCODE_RFMCI
		| PPC_OPCODE_E500 | PPC_OPCODE_E200Z4),
    PPC_OPCODE_VLE },
  { "e300",    PPC_OPCODE_PPC | PPC_OPCODE_E300,
    0 },
  { "e500",    (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_SPE
		| PPC_OPCODE_ISEL | PPC_OPCODE_EFS | PPC_OPCODE_BRLOCK
		| PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK | PPC_OPCODE_RFMCI
		| PPC_OPCODE_E500),
    0 },
  { "e500mc",  (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_ISEL
		| PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK | PPC_OPCODE_RFMCI
		| PPC_OPCODE_E500MC),
    0 },
  { "e500mc64",  (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_ISEL
		| PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK | PPC_OPCODE_RFMCI
		| PPC_OPCODE_E500MC | PPC_OPCODE_64 | PPC_OPCODE_POWER5
		| PPC_OPCODE_POWER6 | PPC_OPCODE_POWER7),
    0 },
  { "e5500",    (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_ISEL
		| PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK | PPC_OPCODE_RFMCI
		| PPC_OPCODE_E500MC | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		| PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6
		| PPC_OPCODE_POWER7),
    0 },
  { "e6500",   (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_ISEL
		| PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK | PPC_OPCODE_RFMCI
		| PPC_OPCODE_E500MC | PPC_OPCODE_64 | PPC_OPCODE_ALTIVEC
		| PPC_OPCODE_ALTIVEC2 | PPC_OPCODE_E6500 | PPC_OPCODE_POWER4
		| PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6 | PPC_OPCODE_POWER7),
    0 },
  { "e500x2",  (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_SPE
		| PPC_OPCODE_ISEL | PPC_OPCODE_EFS | PPC_OPCODE_BRLOCK
		| PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK | PPC_OPCODE_RFMCI
		| PPC_OPCODE_E500),
    0 },
  { "efs",     PPC_OPCODE_PPC | PPC_OPCODE_EFS,
    0 },
  { "power4",  PPC_OPCODE_PPC | PPC_OPCODE_64 | PPC_OPCODE_POWER4,
    0 },
  { "power5",  (PPC_OPCODE_PPC | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		| PPC_OPCODE_POWER5),
    0 },
  { "power6",  (PPC_OPCODE_PPC | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		| PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6 | PPC_OPCODE_ALTIVEC),
    0 },
  { "power7",  (PPC_OPCODE_PPC | PPC_OPCODE_ISEL | PPC_OPCODE_64
		| PPC_OPCODE_POWER4 | PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6
		| PPC_OPCODE_POWER7 | PPC_OPCODE_ALTIVEC | PPC_OPCODE_VSX),
    0 },
  { "power8",  (PPC_OPCODE_PPC | PPC_OPCODE_ISEL | PPC_OPCODE_64
		| PPC_OPCODE_POWER4 | PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6
		| PPC_OPCODE_POWER7 | PPC_OPCODE_POWER8 | PPC_OPCODE_HTM
		| PPC_OPCODE_ALTIVEC | PPC_OPCODE_ALTIVEC2 | PPC_OPCODE_VSX),
    0 },
  { "power9",  (PPC_OPCODE_PPC | PPC_OPCODE_ISEL | PPC_OPCODE_64
		| PPC_OPCODE_POWER4 | PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6
		| PPC_OPCODE_POWER7 | PPC_OPCODE_POWER8 | PPC_OPCODE_POWER9
		| PPC_OPCODE_HTM | PPC_OPCODE_ALTIVEC | PPC_OPCODE_ALTIVEC2
		| PPC_OPCODE_VSX | PPC_OPCODE_VSX3 ),
    0 },
  { "ppc",     PPC_OPCODE_PPC,
    0 },
  { "ppc32",   PPC_OPCODE_PPC,
    0 },
  { "ppc64",   PPC_OPCODE_PPC | PPC_OPCODE_64,
    0 },
  { "ppc64bridge", PPC_OPCODE_PPC | PPC_OPCODE_64_BRIDGE,
    0 },
  { "ppcps",   PPC_OPCODE_PPC | PPC_OPCODE_PPCPS,
    0 },
  { "pwr",     PPC_OPCODE_POWER,
    0 },
  { "pwr2",    PPC_OPCODE_POWER | PPC_OPCODE_POWER2,
    0 },
  { "pwr4",    PPC_OPCODE_PPC | PPC_OPCODE_64 | PPC_OPCODE_POWER4,
    0 },
  { "pwr5",    (PPC_OPCODE_PPC | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		| PPC_OPCODE_POWER5),
    0 },
  { "pwr5x",   (PPC_OPCODE_PPC | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		| PPC_OPCODE_POWER5),
    0 },
  { "pwr6",    (PPC_OPCODE_PPC | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		| PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6 | PPC_OPCODE_ALTIVEC),
    0 },
  { "pwr7",    (PPC_OPCODE_PPC | PPC_OPCODE_ISEL | PPC_OPCODE_64
		| PPC_OPCODE_POWER4 | PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6
		| PPC_OPCODE_POWER7 | PPC_OPCODE_ALTIVEC | PPC_OPCODE_VSX),
    0 },
  { "pwr8",    (PPC_OPCODE_PPC | PPC_OPCODE_ISEL | PPC_OPCODE_64
		| PPC_OPCODE_POWER4 | PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6
		| PPC_OPCODE_POWER7 | PPC_OPCODE_POWER8 | PPC_OPCODE_HTM
		| PPC_OPCODE_ALTIVEC | PPC_OPCODE_ALTIVEC2 | PPC_OPCODE_VSX),
    0 },
  { "pwr9",    (PPC_OPCODE_PPC | PPC_OPCODE_ISEL | PPC_OPCODE_64
		| PPC_OPCODE_POWER4 | PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6
		| PPC_OPCODE_POWER7 | PPC_OPCODE_POWER8 | PPC_OPCODE_POWER9
		| PPC_OPCODE_HTM | PPC_OPCODE_ALTIVEC | PPC_OPCODE_ALTIVEC2
		| PPC_OPCODE_VSX | PPC_OPCODE_VSX3 ),
    0 },
  { "pwrx",    PPC_OPCODE_POWER | PPC_OPCODE_POWER2,
    0 },
  { "spe",     PPC_OPCODE_PPC | PPC_OPCODE_EFS,
    PPC_OPCODE_SPE },
  { "titan",   (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_PMR
		| PPC_OPCODE_RFMCI | PPC_OPCODE_TITAN),
    0 },
  { "vle",     (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE| PPC_OPCODE_SPE
		| PPC_OPCODE_ISEL | PPC_OPCODE_EFS | PPC_OPCODE_BRLOCK
		| PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK | PPC_OPCODE_RFMCI
		| PPC_OPCODE_E500),
    PPC_OPCODE_VLE },
  { "vsx",     PPC_OPCODE_PPC,
    PPC_OPCODE_VSX | PPC_OPCODE_VSX3 },
  { "htm",     PPC_OPCODE_PPC,
    PPC_OPCODE_HTM },
};

/* Switch between Booke and VLE dialects for interlinked dumps.  */
static ppc_cpu_t
get_powerpc_dialect (struct disassemble_info *info)
{
  ppc_cpu_t dialect = 0;

  dialect = POWERPC_DIALECT (info);

  /* Disassemble according to the section headers flags for VLE-mode.  */
  if (dialect & PPC_OPCODE_VLE
      && info->section->owner != NULL
      && bfd_get_flavour (info->section->owner) == bfd_target_elf_flavour
      && elf_object_id (info->section->owner) == PPC32_ELF_DATA
      && (elf_section_flags (info->section) & SHF_PPC_VLE) != 0)
    return dialect;
  else
    return dialect & ~ PPC_OPCODE_VLE;
}

/* Handle -m and -M options that set cpu type, and .machine arg.  */

ppc_cpu_t
ppc_parse_cpu (ppc_cpu_t ppc_cpu, ppc_cpu_t *sticky, const char *arg)
{
  unsigned int i;

  for (i = 0; i < sizeof (ppc_opts) / sizeof (ppc_opts[0]); i++)
    if (strcmp (ppc_opts[i].opt, arg) == 0)
      {
	if (ppc_opts[i].sticky)
	  {
	    *sticky |= ppc_opts[i].sticky;
	    if ((ppc_cpu & ~*sticky) != 0)
	      break;
	  }
	ppc_cpu = ppc_opts[i].cpu;
	break;
      }
  if (i >= sizeof (ppc_opts) / sizeof (ppc_opts[0]))
    return 0;

  ppc_cpu |= *sticky;
  return ppc_cpu;
}

/* Determine which set of machines to disassemble for.  */

static void
powerpc_init_dialect (struct disassemble_info *info)
{
  ppc_cpu_t dialect = 0;
  ppc_cpu_t sticky = 0;
  char *arg;
  struct dis_private *priv = calloc (sizeof (*priv), 1);

  if (priv == NULL)
    priv = &private;

  switch (info->mach)
    {
    case bfd_mach_ppc_403:
    case bfd_mach_ppc_403gc:
      dialect = ppc_parse_cpu (dialect, &sticky, "403");
      break;
    case bfd_mach_ppc_405:
      dialect = ppc_parse_cpu (dialect, &sticky, "405");
      break;
    case bfd_mach_ppc_601:
      dialect = ppc_parse_cpu (dialect, &sticky, "601");
      break;
    case bfd_mach_ppc_a35:
    case bfd_mach_ppc_rs64ii:
    case bfd_mach_ppc_rs64iii:
      dialect = ppc_parse_cpu (dialect, &sticky, "pwr2") | PPC_OPCODE_64;
      break;
    case bfd_mach_ppc_e500:
      dialect = ppc_parse_cpu (dialect, &sticky, "e500");
      break;
    case bfd_mach_ppc_e500mc:
      dialect = ppc_parse_cpu (dialect, &sticky, "e500mc");
      break;
    case bfd_mach_ppc_e500mc64:
      dialect = ppc_parse_cpu (dialect, &sticky, "e500mc64");
      break;
    case bfd_mach_ppc_e5500:
      dialect = ppc_parse_cpu (dialect, &sticky, "e5500");
      break;
    case bfd_mach_ppc_e6500:
      dialect = ppc_parse_cpu (dialect, &sticky, "e6500");
      break;
    case bfd_mach_ppc_titan:
      dialect = ppc_parse_cpu (dialect, &sticky, "titan");
      break;
    case bfd_mach_ppc_vle:
      dialect = ppc_parse_cpu (dialect, &sticky, "vle");
      break;
    default:
      dialect = ppc_parse_cpu (dialect, &sticky, "power9") | PPC_OPCODE_ANY;
    }

  arg = info->disassembler_options;
  while (arg != NULL)
    {
      ppc_cpu_t new_cpu = 0;
      char *end = strchr (arg, ',');

      if (end != NULL)
	*end = 0;

      if ((new_cpu = ppc_parse_cpu (dialect, &sticky, arg)) != 0)
	dialect = new_cpu;
      else if (strcmp (arg, "32") == 0)
	dialect &= ~(ppc_cpu_t) PPC_OPCODE_64;
      else if (strcmp (arg, "64") == 0)
	dialect |= PPC_OPCODE_64;
      else
	fprintf (stderr, _("warning: ignoring unknown -M%s option\n"), arg);

      if (end != NULL)
	*end++ = ',';
      arg = end;
    }

  info->private_data = priv;
  POWERPC_DIALECT(info) = dialect;
}

#define PPC_OPCD_SEGS 64
static unsigned short powerpc_opcd_indices[PPC_OPCD_SEGS+1];
#define VLE_OPCD_SEGS 32
static unsigned short vle_opcd_indices[VLE_OPCD_SEGS+1];

/* Calculate opcode table indices to speed up disassembly,
   and init dialect.  */

void
disassemble_init_powerpc (struct disassemble_info *info)
{
  int i;
  unsigned short last;

  if (powerpc_opcd_indices[PPC_OPCD_SEGS] == 0)
    {

      i = powerpc_num_opcodes;
      while (--i >= 0)
        {
          unsigned op = PPC_OP (powerpc_opcodes[i].opcode);

          powerpc_opcd_indices[op] = i;
        }

      last = powerpc_num_opcodes;
      for (i = PPC_OPCD_SEGS; i > 0; --i)
        {
          if (powerpc_opcd_indices[i] == 0)
	    powerpc_opcd_indices[i] = last;
          last = powerpc_opcd_indices[i];
        }

      i = vle_num_opcodes;
      while (--i >= 0)
        {
          unsigned op = VLE_OP (vle_opcodes[i].opcode, vle_opcodes[i].mask);
          unsigned seg = VLE_OP_TO_SEG (op);

          vle_opcd_indices[seg] = i;
        }

      last = vle_num_opcodes;
      for (i = VLE_OPCD_SEGS; i > 0; --i)
        {
          if (vle_opcd_indices[i] == 0)
	    vle_opcd_indices[i] = last;
          last = vle_opcd_indices[i];
        }
    }

  if (info->arch == bfd_arch_powerpc)
    powerpc_init_dialect (info);
}

/* Print a big endian PowerPC instruction.  */

int
print_insn_big_powerpc (bfd_vma memaddr, struct disassemble_info *info)
{
  return print_insn_powerpc (memaddr, info, 1, get_powerpc_dialect (info));
}

/* Print a little endian PowerPC instruction.  */

int
print_insn_little_powerpc (bfd_vma memaddr, struct disassemble_info *info)
{
  return print_insn_powerpc (memaddr, info, 0, get_powerpc_dialect (info));
}

/* Print a POWER (RS/6000) instruction.  */

int
print_insn_rs6000 (bfd_vma memaddr, struct disassemble_info *info)
{
  return print_insn_powerpc (memaddr, info, 1, PPC_OPCODE_POWER);
}

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
  unsigned long op;

  /* Get the major opcode of the instruction.  */
  op = PPC_OP (insn);

  /* Find the first match in the opcode table for this major opcode.  */
  opcode_end = powerpc_opcodes + powerpc_opcd_indices[op + 1];
  for (opcode = powerpc_opcodes + powerpc_opcd_indices[op];
       opcode < opcode_end;
       ++opcode)
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

/* Find a match for INSN in the VLE opcode table.  */

static const struct powerpc_opcode *
lookup_vle (unsigned long insn)
{
  const struct powerpc_opcode *opcode;
  const struct powerpc_opcode *opcode_end;
  unsigned op, seg;

  op = PPC_OP (insn);
  if (op >= 0x20 && op <= 0x37)
    {
      /* This insn has a 4-bit opcode.  */
      op &= 0x3c;
    }
  seg = VLE_OP_TO_SEG (op);

  /* Find the first match in the opcode table for this major opcode.  */
  opcode_end = vle_opcodes + vle_opcd_indices[seg + 1];
  for (opcode = vle_opcodes + vle_opcd_indices[seg];
       opcode < opcode_end;
       ++opcode)
    {
      unsigned long table_opcd = opcode->opcode;
      unsigned long table_mask = opcode->mask;
      bfd_boolean table_op_is_short = PPC_OP_SE_VLE(table_mask);
      unsigned long insn2;
      const unsigned char *opindex;
      const struct powerpc_operand *operand;
      int invalid;

      insn2 = insn;
      if (table_op_is_short)
	insn2 >>= 16;
      if ((insn2 & table_mask) != table_opcd)
	continue;

      /* Check validity of operands.  */
      invalid = 0;
      for (opindex = opcode->operands; *opindex != 0; ++opindex)
	{
	  operand = powerpc_operands + *opindex;
	  if (operand->extract)
	    (*operand->extract) (insn, (ppc_cpu_t)0, &invalid);
	}
      if (invalid)
	continue;

      return opcode;
    }

  return NULL;
}

/* Print a PowerPC or POWER instruction.  */

static int
print_insn_powerpc (bfd_vma memaddr,
		    struct disassemble_info *info,
		    int bigendian,
		    ppc_cpu_t dialect)
{
  bfd_byte buffer[4];
  int status;
  unsigned long insn;
  const struct powerpc_opcode *opcode;
  bfd_boolean insn_is_short;

  status = (*info->read_memory_func) (memaddr, buffer, 4, info);
  if (status != 0)
    {
      /* The final instruction may be a 2-byte VLE insn.  */
      if ((dialect & PPC_OPCODE_VLE) != 0)
        {
          /* Clear buffer so unused bytes will not have garbage in them.  */
          buffer[0] = buffer[1] = buffer[2] = buffer[3] = 0;
          status = (*info->read_memory_func) (memaddr, buffer, 2, info);
          if (status != 0)
            {
              (*info->memory_error_func) (status, memaddr, info);
              return -1;
            }
        }
      else
        {
          (*info->memory_error_func) (status, memaddr, info);
          return -1;
        }
    }

  if (bigendian)
    insn = bfd_getb32 (buffer);
  else
    insn = bfd_getl32 (buffer);

  /* Get the major opcode of the insn.  */
  opcode = NULL;
  insn_is_short = FALSE;
  if ((dialect & PPC_OPCODE_VLE) != 0)
    {
      opcode = lookup_vle (insn);
      if (opcode != NULL)
	insn_is_short = PPC_OP_SE_VLE(opcode->mask);
    }
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
	(*info->fprintf_func) (info->stream, "%-7s ", opcode->name);
      else
	(*info->fprintf_func) (info->stream, "%s", opcode->name);

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
	      (*info->fprintf_func) (info->stream, ",");
	      need_comma = 0;
	    }

	  /* Print the operand as directed by the flags.  */
	  if ((operand->flags & PPC_OPERAND_GPR) != 0
	      || ((operand->flags & PPC_OPERAND_GPR_0) != 0 && value != 0))
	    (*info->fprintf_func) (info->stream, "r%ld", value);
	  else if ((operand->flags & PPC_OPERAND_FPR) != 0)
	    (*info->fprintf_func) (info->stream, "f%ld", value);
	  else if ((operand->flags & PPC_OPERAND_VR) != 0)
	    (*info->fprintf_func) (info->stream, "v%ld", value);
	  else if ((operand->flags & PPC_OPERAND_VSR) != 0)
	    (*info->fprintf_func) (info->stream, "vs%ld", value);
	  else if ((operand->flags & PPC_OPERAND_RELATIVE) != 0)
	    (*info->print_address_func) (memaddr + value, info);
	  else if ((operand->flags & PPC_OPERAND_ABSOLUTE) != 0)
	    (*info->print_address_func) ((bfd_vma) value & 0xffffffff, info);
	  else if ((operand->flags & PPC_OPERAND_FSL) != 0)
	    (*info->fprintf_func) (info->stream, "fsl%ld", value);
	  else if ((operand->flags & PPC_OPERAND_FCR) != 0)
	    (*info->fprintf_func) (info->stream, "fcr%ld", value);
	  else if ((operand->flags & PPC_OPERAND_UDI) != 0)
	    (*info->fprintf_func) (info->stream, "%ld", value);
	  else if ((operand->flags & PPC_OPERAND_CR_REG) != 0
		   && (((dialect & PPC_OPCODE_PPC) != 0)
		       || ((dialect & PPC_OPCODE_VLE) != 0)))
	    (*info->fprintf_func) (info->stream, "cr%ld", value);
	  else if (((operand->flags & PPC_OPERAND_CR_BIT) != 0)
		   && (((dialect & PPC_OPCODE_PPC) != 0)
		       || ((dialect & PPC_OPCODE_VLE) != 0)))
	    {
	      static const char *cbnames[4] = { "lt", "gt", "eq", "so" };
	      int cr;
	      int cc;

	      cr = value >> 2;
	      if (cr != 0)
		(*info->fprintf_func) (info->stream, "4*cr%d+", cr);
	      cc = value & 3;
	      (*info->fprintf_func) (info->stream, "%s", cbnames[cc]);
	    }
	  else
	    (*info->fprintf_func) (info->stream, "%d", (int) value);

	  if (need_paren)
	    {
	      (*info->fprintf_func) (info->stream, ")");
	      need_paren = 0;
	    }

	  if ((operand->flags & PPC_OPERAND_PARENS) == 0)
	    need_comma = 1;
	  else
	    {
	      (*info->fprintf_func) (info->stream, "(");
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
  (*info->fprintf_func) (info->stream, ".long 0x%lx", insn);

  return 4;
}

void
print_ppc_disassembler_options (FILE *stream)
{
  unsigned int i, col;

  fprintf (stream, _("\n\
The following PPC specific disassembler options are supported for use with\n\
the -M switch:\n"));

  for (col = 0, i = 0; i < sizeof (ppc_opts) / sizeof (ppc_opts[0]); i++)
    {
      col += fprintf (stream, " %s,", ppc_opts[i].opt);
      if (col > 66)
	{
	  fprintf (stream, "\n");
	  col = 0;
	}
    }
  fprintf (stream, " 32, 64\n");
}
