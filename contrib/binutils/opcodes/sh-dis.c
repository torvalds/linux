/* Disassemble SH instructions.
   Copyright 1993, 1994, 1995, 1997, 1998, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include <stdio.h>
#include "sysdep.h"
#define STATIC_TABLE
#define DEFINE_TABLE

#include "sh-opc.h"
#include "dis-asm.h"

#ifdef ARCH_all
#define INCLUDE_SHMEDIA
#endif

static void
print_movxy (const sh_opcode_info *op,
	     int rn,
	     int rm,
	     fprintf_ftype fprintf_fn,
	     void *stream)
{
  int n;

  fprintf_fn (stream, "%s\t", op->name);
  for (n = 0; n < 2; n++)
    {
      switch (op->arg[n])
	{
	case A_IND_N:
	case AX_IND_N:
	case AXY_IND_N:
	case AY_IND_N:
	case AYX_IND_N:
	  fprintf_fn (stream, "@r%d", rn);
	  break;
	case A_INC_N:
	case AX_INC_N:
	case AXY_INC_N:
	case AY_INC_N:
	case AYX_INC_N:
	  fprintf_fn (stream, "@r%d+", rn);
	  break;
	case AX_PMOD_N:
	case AXY_PMOD_N:
	  fprintf_fn (stream, "@r%d+r8", rn);
	  break;
	case AY_PMOD_N:
	case AYX_PMOD_N:
	  fprintf_fn (stream, "@r%d+r9", rn);
	  break;
	case DSP_REG_A_M:
	  fprintf_fn (stream, "a%c", '0' + rm);
	  break;
	case DSP_REG_X:
	  fprintf_fn (stream, "x%c", '0' + rm);
	  break;
	case DSP_REG_Y:
	  fprintf_fn (stream, "y%c", '0' + rm);
	  break;
	case DSP_REG_AX:
	  fprintf_fn (stream, "%c%c",
		      (rm & 1) ? 'x' : 'a',
		      (rm & 2) ? '1' : '0');
	  break;
	case DSP_REG_XY:
	  fprintf_fn (stream, "%c%c",
		      (rm & 1) ? 'y' : 'x',
		      (rm & 2) ? '1' : '0');
	  break;
	case DSP_REG_AY:
	  fprintf_fn (stream, "%c%c",
		      (rm & 2) ? 'y' : 'a',
		      (rm & 1) ? '1' : '0');
	  break;
	case DSP_REG_YX:
	  fprintf_fn (stream, "%c%c",
		      (rm & 2) ? 'x' : 'y',
		      (rm & 1) ? '1' : '0');
	  break;
	default:
	  abort ();
	}
      if (n == 0)
	fprintf_fn (stream, ",");
    }
}

/* Print a double data transfer insn.  INSN is just the lower three
   nibbles of the insn, i.e. field a and the bit that indicates if
   a parallel processing insn follows.
   Return nonzero if a field b of a parallel processing insns follows.  */

static void
print_insn_ddt (int insn, struct disassemble_info *info)
{
  fprintf_ftype fprintf_fn = info->fprintf_func;
  void *stream = info->stream;

  /* If this is just a nop, make sure to emit something.  */
  if (insn == 0x000)
    fprintf_fn (stream, "nopx\tnopy");

  /* If a parallel processing insn was printed before,
     and we got a non-nop, emit a tab.  */
  if ((insn & 0x800) && (insn & 0x3ff))
    fprintf_fn (stream, "\t");

  /* Check if either the x or y part is invalid.  */
  if (((insn & 0xc) == 0 && (insn & 0x2a0))
      || ((insn & 3) == 0 && (insn & 0x150)))
    if (info->mach != bfd_mach_sh_dsp
        && info->mach != bfd_mach_sh3_dsp)
      {
	static const sh_opcode_info *first_movx, *first_movy;
	const sh_opcode_info *op;
	int is_movy;

	if (! first_movx)
	  {
	    for (first_movx = sh_table; first_movx->nibbles[1] != MOVX_NOPY;)
	      first_movx++;
	    for (first_movy = first_movx; first_movy->nibbles[1] != MOVY_NOPX;)
	      first_movy++;
	  }

	is_movy = ((insn & 3) != 0);

	if (is_movy)
	  op = first_movy;
	else
	  op = first_movx;

	while (op->nibbles[2] != (unsigned) ((insn >> 4) & 3)
	       || op->nibbles[3] != (unsigned) (insn & 0xf))
	  op++;
	
	print_movxy (op,
		     (4 * ((insn & (is_movy ? 0x200 : 0x100)) == 0)
		      + 2 * is_movy
		      + 1 * ((insn & (is_movy ? 0x100 : 0x200)) != 0)),
		     (insn >> 6) & 3,
		     fprintf_fn, stream);
      }
    else
      fprintf_fn (stream, ".word 0x%x", insn);
  else
    {
      static const sh_opcode_info *first_movx, *first_movy;
      const sh_opcode_info *opx, *opy;
      unsigned int insn_x, insn_y;

      if (! first_movx)
	{
	  for (first_movx = sh_table; first_movx->nibbles[1] != MOVX;)
	    first_movx++;
	  for (first_movy = first_movx; first_movy->nibbles[1] != MOVY;)
	    first_movy++;
	}
      insn_x = (insn >> 2) & 0xb;
      if (insn_x)
	{
	  for (opx = first_movx; opx->nibbles[2] != insn_x;)
	    opx++;
	  print_movxy (opx, ((insn >> 9) & 1) + 4, (insn >> 7) & 1,
		       fprintf_fn, stream);
	}
      insn_y = (insn & 3) | ((insn >> 1) & 8);
      if (insn_y)
	{
	  if (insn_x)
	    fprintf_fn (stream, "\t");
	  for (opy = first_movy; opy->nibbles[2] != insn_y;)
	    opy++;
	  print_movxy (opy, ((insn >> 8) & 1) + 6, (insn >> 6) & 1,
		       fprintf_fn, stream);
	}
    }
}

static void
print_dsp_reg (int rm, fprintf_ftype fprintf_fn, void *stream)
{
  switch (rm)
    {
    case A_A1_NUM:
      fprintf_fn (stream, "a1");
      break;
    case A_A0_NUM:
      fprintf_fn (stream, "a0");
      break;
    case A_X0_NUM:
      fprintf_fn (stream, "x0");
      break;
    case A_X1_NUM:
      fprintf_fn (stream, "x1");
      break;
    case A_Y0_NUM:
      fprintf_fn (stream, "y0");
      break;
    case A_Y1_NUM:
      fprintf_fn (stream, "y1");
      break;
    case A_M0_NUM:
      fprintf_fn (stream, "m0");
      break;
    case A_A1G_NUM:
      fprintf_fn (stream, "a1g");
      break;
    case A_M1_NUM:
      fprintf_fn (stream, "m1");
      break;
    case A_A0G_NUM:
      fprintf_fn (stream, "a0g");
      break;
    default:
      fprintf_fn (stream, "0x%x", rm);
      break;
    }
}

static void
print_insn_ppi (int field_b, struct disassemble_info *info)
{
  static char *sx_tab[] = { "x0", "x1", "a0", "a1" };
  static char *sy_tab[] = { "y0", "y1", "m0", "m1" };
  fprintf_ftype fprintf_fn = info->fprintf_func;
  void *stream = info->stream;
  unsigned int nib1, nib2, nib3;
  unsigned int altnib1, nib4;
  char *dc = NULL;
  const sh_opcode_info *op;

  if ((field_b & 0xe800) == 0)
    {
      fprintf_fn (stream, "psh%c\t#%d,",
		  field_b & 0x1000 ? 'a' : 'l',
		  (field_b >> 4) & 127);
      print_dsp_reg (field_b & 0xf, fprintf_fn, stream);
      return;
    }
  if ((field_b & 0xc000) == 0x4000 && (field_b & 0x3000) != 0x1000)
    {
      static char *du_tab[] = { "x0", "y0", "a0", "a1" };
      static char *se_tab[] = { "x0", "x1", "y0", "a1" };
      static char *sf_tab[] = { "y0", "y1", "x0", "a1" };
      static char *sg_tab[] = { "m0", "m1", "a0", "a1" };

      if (field_b & 0x2000)
	fprintf_fn (stream, "p%s %s,%s,%s\t",
		    (field_b & 0x1000) ? "add" : "sub",
		    sx_tab[(field_b >> 6) & 3],
		    sy_tab[(field_b >> 4) & 3],
		    du_tab[(field_b >> 0) & 3]);

      else if ((field_b & 0xf0) == 0x10
	       && info->mach != bfd_mach_sh_dsp
	       && info->mach != bfd_mach_sh3_dsp)
	fprintf_fn (stream, "pclr %s \t", du_tab[(field_b >> 0) & 3]);

      else if ((field_b & 0xf3) != 0)
	fprintf_fn (stream, ".word 0x%x\t", field_b);

      fprintf_fn (stream, "pmuls%c%s,%s,%s",
		  field_b & 0x2000 ? ' ' : '\t',
		  se_tab[(field_b >> 10) & 3],
		  sf_tab[(field_b >>  8) & 3],
		  sg_tab[(field_b >>  2) & 3]);
      return;
    }

  nib1 = PPIC;
  nib2 = field_b >> 12 & 0xf;
  nib3 = field_b >> 8 & 0xf;
  nib4 = field_b >> 4 & 0xf;
  switch (nib3 & 0x3)
    {
    case 0:
      dc = "";
      nib1 = PPI3;
      break;
    case 1:
      dc = "";
      break;
    case 2:
      dc = "dct ";
      nib3 -= 1;
      break;
    case 3:
      dc = "dcf ";
      nib3 -= 2;
      break;
    }
  if (nib1 == PPI3)
    altnib1 = PPI3NC;
  else
    altnib1 = nib1;
  for (op = sh_table; op->name; op++)
    {
      if ((op->nibbles[1] == nib1 || op->nibbles[1] == altnib1)
	  && op->nibbles[2] == nib2
	  && op->nibbles[3] == nib3)
	{
	  int n;

	  switch (op->nibbles[4])
	    {
	    case HEX_0:
	      break;
	    case HEX_XX00:
	      if ((nib4 & 3) != 0)
		continue;
	      break;
	    case HEX_1:
	      if ((nib4 & 3) != 1)
		continue;
	      break;
	    case HEX_00YY:
	      if ((nib4 & 0xc) != 0)
		continue;
	      break;
	    case HEX_4:
	      if ((nib4 & 0xc) != 4)
		continue;
	      break;
	    default:
	      abort ();
	    }
	  fprintf_fn (stream, "%s%s\t", dc, op->name);
	  for (n = 0; n < 3 && op->arg[n] != A_END; n++)
	    {
	      if (n && op->arg[1] != A_END)
		fprintf_fn (stream, ",");
	      switch (op->arg[n])
		{
		case DSP_REG_N:
		  print_dsp_reg (field_b & 0xf, fprintf_fn, stream);
		  break;
		case DSP_REG_X:
		  fprintf_fn (stream, sx_tab[(field_b >> 6) & 3]);
		  break;
		case DSP_REG_Y:
		  fprintf_fn (stream, sy_tab[(field_b >> 4) & 3]);
		  break;
		case A_MACH:
		  fprintf_fn (stream, "mach");
		  break;
		case A_MACL:
		  fprintf_fn (stream, "macl");
		  break;
		default:
		  abort ();
		}
	    }
	  return;
	}
    }
  /* Not found.  */
  fprintf_fn (stream, ".word 0x%x", field_b);
}

/* FIXME mvs: movx insns print as ".word 0x%03x", insn & 0xfff
   (ie. the upper nibble is missing).  */

int
print_insn_sh (bfd_vma memaddr, struct disassemble_info *info)
{
  fprintf_ftype fprintf_fn = info->fprintf_func;
  void *stream = info->stream;
  unsigned char insn[4];
  unsigned char nibs[8];
  int status;
  bfd_vma relmask = ~(bfd_vma) 0;
  const sh_opcode_info *op;
  unsigned int target_arch;
  int allow_op32;

  switch (info->mach)
    {
    case bfd_mach_sh:
      target_arch = arch_sh1;
      /* SH coff object files lack information about the machine type, so
         we end up with bfd_mach_sh unless it was set explicitly (which
	 could have happended if this is a call from gdb or the simulator.)  */
      if (info->symbols
	  && bfd_asymbol_flavour(*info->symbols) == bfd_target_coff_flavour)
	target_arch = arch_sh4;
      break;
    case bfd_mach_sh5:
#ifdef INCLUDE_SHMEDIA
      status = print_insn_sh64 (memaddr, info);
      if (status != -2)
	return status;
#endif
      /* When we get here for sh64, it's because we want to disassemble
	 SHcompact, i.e. arch_sh4.  */
      target_arch = arch_sh4;
      break;
    default:
      target_arch = sh_get_arch_from_bfd_mach (info->mach);
    }

  status = info->read_memory_func (memaddr, insn, 2, info);

  if (status != 0)
    {
      info->memory_error_func (status, memaddr, info);
      return -1;
    }

  if (info->endian == BFD_ENDIAN_LITTLE)
    {
      nibs[0] = (insn[1] >> 4) & 0xf;
      nibs[1] = insn[1] & 0xf;

      nibs[2] = (insn[0] >> 4) & 0xf;
      nibs[3] = insn[0] & 0xf;
    }
  else
    {
      nibs[0] = (insn[0] >> 4) & 0xf;
      nibs[1] = insn[0] & 0xf;

      nibs[2] = (insn[1] >> 4) & 0xf;
      nibs[3] = insn[1] & 0xf;
    }
  status = info->read_memory_func (memaddr + 2, insn + 2, 2, info);
  if (status != 0)
    allow_op32 = 0;
  else
    {
      allow_op32 = 1;

      if (info->endian == BFD_ENDIAN_LITTLE)
	{
	  nibs[4] = (insn[3] >> 4) & 0xf;
	  nibs[5] = insn[3] & 0xf;

	  nibs[6] = (insn[2] >> 4) & 0xf;
	  nibs[7] = insn[2] & 0xf;
	}
      else
	{
	  nibs[4] = (insn[2] >> 4) & 0xf;
	  nibs[5] = insn[2] & 0xf;

	  nibs[6] = (insn[3] >> 4) & 0xf;
	  nibs[7] = insn[3] & 0xf;
	}
    }

  if (nibs[0] == 0xf && (nibs[1] & 4) == 0
      && SH_MERGE_ARCH_SET_VALID (target_arch, arch_sh_dsp_up))
    {
      if (nibs[1] & 8)
	{
	  int field_b;

	  status = info->read_memory_func (memaddr + 2, insn, 2, info);

	  if (status != 0)
	    {
	      info->memory_error_func (status, memaddr + 2, info);
	      return -1;
	    }

	  if (info->endian == BFD_ENDIAN_LITTLE)
	    field_b = insn[1] << 8 | insn[0];
	  else
	    field_b = insn[0] << 8 | insn[1];

	  print_insn_ppi (field_b, info);
	  print_insn_ddt ((nibs[1] << 8) | (nibs[2] << 4) | nibs[3], info);
	  return 4;
	}
      print_insn_ddt ((nibs[1] << 8) | (nibs[2] << 4) | nibs[3], info);
      return 2;
    }
  for (op = sh_table; op->name; op++)
    {
      int n;
      int imm = 0;
      int rn = 0;
      int rm = 0;
      int rb = 0;
      int disp_pc;
      bfd_vma disp_pc_addr = 0;
      int disp = 0;
      int has_disp = 0;
      int max_n = SH_MERGE_ARCH_SET (op->arch, arch_op32) ? 8 : 4;

      if (!allow_op32
	  && SH_MERGE_ARCH_SET (op->arch, arch_op32))
	goto fail;

      if (!SH_MERGE_ARCH_SET_VALID (op->arch, target_arch))
	goto fail;
      for (n = 0; n < max_n; n++)
	{
	  int i = op->nibbles[n];

	  if (i < 16)
	    {
	      if (nibs[n] == i)
		continue;
	      goto fail;
	    }
	  switch (i)
	    {
	    case BRANCH_8:
	      imm = (nibs[2] << 4) | (nibs[3]);
	      if (imm & 0x80)
		imm |= ~0xff;
	      imm = ((char) imm) * 2 + 4;
	      goto ok;
	    case BRANCH_12:
	      imm = ((nibs[1]) << 8) | (nibs[2] << 4) | (nibs[3]);
	      if (imm & 0x800)
		imm |= ~0xfff;
	      imm = imm * 2 + 4;
	      goto ok;
	    case IMM0_3c:
	      if (nibs[3] & 0x8)
		goto fail;
	      imm = nibs[3] & 0x7;
	      break;
	    case IMM0_3s:
	      if (!(nibs[3] & 0x8))
		goto fail;
	      imm = nibs[3] & 0x7;
	      break;
	    case IMM0_3Uc:
	      if (nibs[2] & 0x8)
		goto fail;
	      imm = nibs[2] & 0x7;
	      break;
	    case IMM0_3Us:
	      if (!(nibs[2] & 0x8))
		goto fail;
	      imm = nibs[2] & 0x7;
	      break;
	    case DISP0_12:
	    case DISP1_12:
	      disp = (nibs[5] << 8) | (nibs[6] << 4) | nibs[7];
	      has_disp = 1;
	      goto ok;
	    case DISP0_12BY2:
	    case DISP1_12BY2:
	      disp = ((nibs[5] << 8) | (nibs[6] << 4) | nibs[7]) << 1;
	      relmask = ~(bfd_vma) 1;
	      has_disp = 1;
	      goto ok;
	    case DISP0_12BY4:
	    case DISP1_12BY4:
	      disp = ((nibs[5] << 8) | (nibs[6] << 4) | nibs[7]) << 2;
	      relmask = ~(bfd_vma) 3;
	      has_disp = 1;
	      goto ok;
	    case DISP0_12BY8:
	    case DISP1_12BY8:
	      disp = ((nibs[5] << 8) | (nibs[6] << 4) | nibs[7]) << 3;
	      relmask = ~(bfd_vma) 7;
	      has_disp = 1;
	      goto ok;
	    case IMM0_20_4:
	      break;
	    case IMM0_20:
	      imm = ((nibs[2] << 16) | (nibs[4] << 12) | (nibs[5] << 8)
		     | (nibs[6] << 4) | nibs[7]);
	      if (imm & 0x80000)
		imm -= 0x100000;
	      goto ok;
	    case IMM0_20BY8:
	      imm = ((nibs[2] << 16) | (nibs[4] << 12) | (nibs[5] << 8)
		     | (nibs[6] << 4) | nibs[7]);
	      imm <<= 8;
	      if (imm & 0x8000000)
		imm -= 0x10000000;
	      goto ok;
	    case IMM0_4:
	    case IMM1_4:
	      imm = nibs[3];
	      goto ok;
	    case IMM0_4BY2:
	    case IMM1_4BY2:
	      imm = nibs[3] << 1;
	      goto ok;
	    case IMM0_4BY4:
	    case IMM1_4BY4:
	      imm = nibs[3] << 2;
	      goto ok;
	    case IMM0_8:
	    case IMM1_8:
	      imm = (nibs[2] << 4) | nibs[3];
	      disp = imm;
	      has_disp = 1;
	      if (imm & 0x80)
		imm -= 0x100;
	      goto ok;
	    case PCRELIMM_8BY2:
	      imm = ((nibs[2] << 4) | nibs[3]) << 1;
	      relmask = ~(bfd_vma) 1;
	      goto ok;
	    case PCRELIMM_8BY4:
	      imm = ((nibs[2] << 4) | nibs[3]) << 2;
	      relmask = ~(bfd_vma) 3;
	      goto ok;
	    case IMM0_8BY2:
	    case IMM1_8BY2:
	      imm = ((nibs[2] << 4) | nibs[3]) << 1;
	      goto ok;
	    case IMM0_8BY4:
	    case IMM1_8BY4:
	      imm = ((nibs[2] << 4) | nibs[3]) << 2;
	      goto ok;
	    case REG_N_D:
	      if ((nibs[n] & 1) != 0)
		goto fail;
	      /* Fall through.  */
	    case REG_N:
	      rn = nibs[n];
	      break;
	    case REG_M:
	      rm = nibs[n];
	      break;
	    case REG_N_B01:
	      if ((nibs[n] & 0x3) != 1 /* binary 01 */)
		goto fail;
	      rn = (nibs[n] & 0xc) >> 2;
	      break;
	    case REG_NM:
	      rn = (nibs[n] & 0xc) >> 2;
	      rm = (nibs[n] & 0x3);
	      break;
	    case REG_B:
	      rb = nibs[n] & 0x07;
	      break;
	    case SDT_REG_N:
	      /* sh-dsp: single data transfer.  */
	      rn = nibs[n];
	      if ((rn & 0xc) != 4)
		goto fail;
	      rn = rn & 0x3;
	      rn |= (!(rn & 2)) << 2;
	      break;
	    case PPI:
	    case REPEAT:
	      goto fail;
	    default:
	      abort ();
	    }
	}

    ok:
      /* sh2a has D_REG but not X_REG.  We don't know the pattern
	 doesn't match unless we check the output args to see if they
	 make sense.  */
      if (target_arch == arch_sh2a
	  && ((op->arg[0] == DX_REG_M && (rm & 1) != 0)
	      || (op->arg[1] == DX_REG_N && (rn & 1) != 0)))
	goto fail;

      fprintf_fn (stream, "%s\t", op->name);
      disp_pc = 0;
      for (n = 0; n < 3 && op->arg[n] != A_END; n++)
	{
	  if (n && op->arg[1] != A_END)
	    fprintf_fn (stream, ",");
	  switch (op->arg[n])
	    {
	    case A_IMM:
	      fprintf_fn (stream, "#%d", imm);
	      break;
	    case A_R0:
	      fprintf_fn (stream, "r0");
	      break;
	    case A_REG_N:
	      fprintf_fn (stream, "r%d", rn);
	      break;
	    case A_INC_N:
	    case AS_INC_N:
	      fprintf_fn (stream, "@r%d+", rn);
	      break;
	    case A_DEC_N:
	    case AS_DEC_N:
	      fprintf_fn (stream, "@-r%d", rn);
	      break;
	    case A_IND_N:
	    case AS_IND_N:
	      fprintf_fn (stream, "@r%d", rn);
	      break;
	    case A_DISP_REG_N:
	      fprintf_fn (stream, "@(%d,r%d)", has_disp?disp:imm, rn);
	      break;
	    case AS_PMOD_N:
	      fprintf_fn (stream, "@r%d+r8", rn);
	      break;
	    case A_REG_M:
	      fprintf_fn (stream, "r%d", rm);
	      break;
	    case A_INC_M:
	      fprintf_fn (stream, "@r%d+", rm);
	      break;
	    case A_DEC_M:
	      fprintf_fn (stream, "@-r%d", rm);
	      break;
	    case A_IND_M:
	      fprintf_fn (stream, "@r%d", rm);
	      break;
	    case A_DISP_REG_M:
	      fprintf_fn (stream, "@(%d,r%d)", has_disp?disp:imm, rm);
	      break;
	    case A_REG_B:
	      fprintf_fn (stream, "r%d_bank", rb);
	      break;
	    case A_DISP_PC:
	      disp_pc = 1;
	      disp_pc_addr = imm + 4 + (memaddr & relmask);
	      (*info->print_address_func) (disp_pc_addr, info);
	      break;
	    case A_IND_R0_REG_N:
	      fprintf_fn (stream, "@(r0,r%d)", rn);
	      break;
	    case A_IND_R0_REG_M:
	      fprintf_fn (stream, "@(r0,r%d)", rm);
	      break;
	    case A_DISP_GBR:
	      fprintf_fn (stream, "@(%d,gbr)", has_disp?disp:imm);
	      break;
	    case A_TBR:
	      fprintf_fn (stream, "tbr");
	      break;
	    case A_DISP2_TBR:
	      fprintf_fn (stream, "@@(%d,tbr)", has_disp?disp:imm);
	      break;
	    case A_INC_R15:
	      fprintf_fn (stream, "@r15+");
	      break;
	    case A_DEC_R15:
	      fprintf_fn (stream, "@-r15");
	      break;
	    case A_R0_GBR:
	      fprintf_fn (stream, "@(r0,gbr)");
	      break;
	    case A_BDISP12:
	    case A_BDISP8:
	      (*info->print_address_func) (imm + memaddr, info);
	      break;
	    case A_SR:
	      fprintf_fn (stream, "sr");
	      break;
	    case A_GBR:
	      fprintf_fn (stream, "gbr");
	      break;
	    case A_VBR:
	      fprintf_fn (stream, "vbr");
	      break;
	    case A_DSR:
	      fprintf_fn (stream, "dsr");
	      break;
	    case A_MOD:
	      fprintf_fn (stream, "mod");
	      break;
	    case A_RE:
	      fprintf_fn (stream, "re");
	      break;
	    case A_RS:
	      fprintf_fn (stream, "rs");
	      break;
	    case A_A0:
	      fprintf_fn (stream, "a0");
	      break;
	    case A_X0:
	      fprintf_fn (stream, "x0");
	      break;
	    case A_X1:
	      fprintf_fn (stream, "x1");
	      break;
	    case A_Y0:
	      fprintf_fn (stream, "y0");
	      break;
	    case A_Y1:
	      fprintf_fn (stream, "y1");
	      break;
	    case DSP_REG_M:
	      print_dsp_reg (rm, fprintf_fn, stream);
	      break;
	    case A_SSR:
	      fprintf_fn (stream, "ssr");
	      break;
	    case A_SPC:
	      fprintf_fn (stream, "spc");
	      break;
	    case A_MACH:
	      fprintf_fn (stream, "mach");
	      break;
	    case A_MACL:
	      fprintf_fn (stream, "macl");
	      break;
	    case A_PR:
	      fprintf_fn (stream, "pr");
	      break;
	    case A_SGR:
	      fprintf_fn (stream, "sgr");
	      break;
	    case A_DBR:
	      fprintf_fn (stream, "dbr");
	      break;
	    case F_REG_N:
	      fprintf_fn (stream, "fr%d", rn);
	      break;
	    case F_REG_M:
	      fprintf_fn (stream, "fr%d", rm);
	      break;
	    case DX_REG_N:
	      if (rn & 1)
		{
		  fprintf_fn (stream, "xd%d", rn & ~1);
		  break;
		}
	    case D_REG_N:
	      fprintf_fn (stream, "dr%d", rn);
	      break;
	    case DX_REG_M:
	      if (rm & 1)
		{
		  fprintf_fn (stream, "xd%d", rm & ~1);
		  break;
		}
	    case D_REG_M:
	      fprintf_fn (stream, "dr%d", rm);
	      break;
	    case FPSCR_M:
	    case FPSCR_N:
	      fprintf_fn (stream, "fpscr");
	      break;
	    case FPUL_M:
	    case FPUL_N:
	      fprintf_fn (stream, "fpul");
	      break;
	    case F_FR0:
	      fprintf_fn (stream, "fr0");
	      break;
	    case V_REG_N:
	      fprintf_fn (stream, "fv%d", rn * 4);
	      break;
	    case V_REG_M:
	      fprintf_fn (stream, "fv%d", rm * 4);
	      break;
	    case XMTRX_M4:
	      fprintf_fn (stream, "xmtrx");
	      break;
	    default:
	      abort ();
	    }
	}

#if 0
      /* This code prints instructions in delay slots on the same line
         as the instruction which needs the delay slots.  This can be
         confusing, since other disassembler don't work this way, and
         it means that the instructions are not all in a line.  So I
         disabled it.  Ian.  */
      if (!(info->flags & 1)
	  && (op->name[0] == 'j'
	      || (op->name[0] == 'b'
		  && (op->name[1] == 'r'
		      || op->name[1] == 's'))
	      || (op->name[0] == 'r' && op->name[1] == 't')
	      || (op->name[0] == 'b' && op->name[2] == '.')))
	{
	  info->flags |= 1;
	  fprintf_fn (stream, "\t(slot ");
	  print_insn_sh (memaddr + 2, info);
	  info->flags &= ~1;
	  fprintf_fn (stream, ")");
	  return 4;
	}
#endif

      if (disp_pc && strcmp (op->name, "mova") != 0)
	{
	  int size;
	  bfd_byte bytes[4];

	  if (relmask == ~(bfd_vma) 1)
	    size = 2;
	  else
	    size = 4;
	  status = info->read_memory_func (disp_pc_addr, bytes, size, info);
	  if (status == 0)
	    {
	      unsigned int val;

	      if (size == 2)
		{
		  if (info->endian == BFD_ENDIAN_LITTLE)
		    val = bfd_getl16 (bytes);
		  else
		    val = bfd_getb16 (bytes);
		}
	      else
		{
		  if (info->endian == BFD_ENDIAN_LITTLE)
		    val = bfd_getl32 (bytes);
		  else
		    val = bfd_getb32 (bytes);
		}
	      if ((*info->symbol_at_address_func) (val, info))
		{
		  fprintf_fn (stream, "\t! ");
		  (*info->print_address_func) (val, info);
		}
	      else
		fprintf_fn (stream, "\t! %x", val);
	    }
	}

      return SH_MERGE_ARCH_SET (op->arch, arch_op32) ? 4 : 2;
    fail:
      ;

    }
  fprintf_fn (stream, ".word 0x%x%x%x%x", nibs[0], nibs[1], nibs[2], nibs[3]);
  return 2;
}
