/* Target-dependent mdebug code for the ALPHA architecture.
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "frame.h"
#include "frame-unwind.h"
#include "frame-base.h"
#include "symtab.h"
#include "gdbcore.h"
#include "block.h"
#include "gdb_assert.h"

#include "alpha-tdep.h"

/* FIXME: Some of this code should perhaps be merged with mips.  */

/* *INDENT-OFF* */
/* Layout of a stack frame on the alpha:

                |				|
 pdr members:	|  7th ... nth arg,		|
                |  `pushed' by caller.		|
                |				|
----------------|-------------------------------|<--  old_sp == vfp
   ^  ^  ^  ^	|				|
   |  |  |  |	|				|
   |  |localoff	|  Copies of 1st .. 6th		|
   |  |  |  |	|  argument if necessary.	|
   |  |  |  v	|				|
   |  |  |  ---	|-------------------------------|<-- LOCALS_ADDRESS
   |  |  |      |				|
   |  |  |      |  Locals and temporaries.	|
   |  |  |      |				|
   |  |  |      |-------------------------------|
   |  |  |      |				|
   |-fregoffset	|  Saved float registers.	|
   |  |  |      |  F9				|
   |  |  |      |   .				|
   |  |  |      |   .				|
   |  |  |      |  F2				|
   |  |  v      |				|
   |  |  -------|-------------------------------|
   |  |         |				|
   |  |         |  Saved registers.		|
   |  |         |  S6				|
   |-regoffset	|   .				|
   |  |         |   .				|
   |  |         |  S0				|
   |  |         |  pdr.pcreg			|
   |  v         |				|
   |  ----------|-------------------------------|
   |            |				|
 frameoffset    |  Argument build area, gets	|
   |            |  7th ... nth arg for any	|
   |            |  called procedure.		|
   v            |  				|
   -------------|-------------------------------|<-- sp
                |				|
*/
/* *INDENT-ON* */

#define PROC_LOW_ADDR(proc) ((proc)->pdr.adr)
#define PROC_FRAME_OFFSET(proc) ((proc)->pdr.frameoffset)
#define PROC_FRAME_REG(proc) ((proc)->pdr.framereg)
#define PROC_REG_MASK(proc) ((proc)->pdr.regmask)
#define PROC_FREG_MASK(proc) ((proc)->pdr.fregmask)
#define PROC_REG_OFFSET(proc) ((proc)->pdr.regoffset)
#define PROC_FREG_OFFSET(proc) ((proc)->pdr.fregoffset)
#define PROC_PC_REG(proc) ((proc)->pdr.pcreg)
#define PROC_LOCALOFF(proc) ((proc)->pdr.localoff)

/* Locate the mdebug PDR for the given PC.  Return null if one can't
   be found; you'll have to fall back to other methods in that case.  */

static alpha_extra_func_info_t
find_proc_desc (CORE_ADDR pc)
{
  struct block *b = block_for_pc (pc);
  alpha_extra_func_info_t proc_desc = NULL;
  struct symbol *sym = NULL;

  if (b)
    {
      CORE_ADDR startaddr;
      find_pc_partial_function (pc, NULL, &startaddr, NULL);

      if (startaddr > BLOCK_START (b))
	/* This is the "pathological" case referred to in a comment in
	   print_frame_info.  It might be better to move this check into
	   symbol reading.  */
	sym = NULL;
      else
	sym = lookup_symbol (MIPS_EFI_SYMBOL_NAME, b, LABEL_DOMAIN, 0, NULL);
    }

  if (sym)
    {
      proc_desc = (alpha_extra_func_info_t) SYMBOL_VALUE (sym);

      /* If we never found a PDR for this function in symbol reading,
	 then examine prologues to find the information.  */
      if (proc_desc->pdr.framereg == -1)
	proc_desc = NULL;
    }

  return proc_desc;
}

/* This returns the PC of the first inst after the prologue.  If we can't
   find the prologue, then return 0.  */

static CORE_ADDR
alpha_mdebug_after_prologue (CORE_ADDR pc, alpha_extra_func_info_t proc_desc)
{
  if (proc_desc)
    {
      /* If function is frameless, then we need to do it the hard way.  I
         strongly suspect that frameless always means prologueless... */
      if (PROC_FRAME_REG (proc_desc) == ALPHA_SP_REGNUM
	  && PROC_FRAME_OFFSET (proc_desc) == 0)
	return 0;
    }

  return alpha_after_prologue (pc);
}

/* Return non-zero if we *might* be in a function prologue.  Return zero
   if we are definitively *not* in a function prologue.  */

static int
alpha_mdebug_in_prologue (CORE_ADDR pc, alpha_extra_func_info_t proc_desc)
{
  CORE_ADDR after_prologue_pc = alpha_mdebug_after_prologue (pc, proc_desc);
  return (after_prologue_pc == 0 || pc < after_prologue_pc);
}


/* Frame unwinder that reads mdebug PDRs.  */

struct alpha_mdebug_unwind_cache
{
  alpha_extra_func_info_t proc_desc;
  CORE_ADDR vfp;
  CORE_ADDR *saved_regs;
};

/* Extract all of the information about the frame from PROC_DESC
   and store the resulting register save locations in the structure.  */

static struct alpha_mdebug_unwind_cache *
alpha_mdebug_frame_unwind_cache (struct frame_info *next_frame, 
				 void **this_prologue_cache)
{
  struct alpha_mdebug_unwind_cache *info;
  alpha_extra_func_info_t proc_desc;
  ULONGEST vfp;
  CORE_ADDR pc, reg_position;
  unsigned long mask;
  int ireg, returnreg;

  if (*this_prologue_cache)
    return *this_prologue_cache;

  info = FRAME_OBSTACK_ZALLOC (struct alpha_mdebug_unwind_cache);
  *this_prologue_cache = info;
  pc = frame_pc_unwind (next_frame);

  /* ??? We don't seem to be able to cache the lookup of the PDR
     from alpha_mdebug_frame_p.  It'd be nice if we could change
     the arguments to that function.  Oh well.  */
  proc_desc = find_proc_desc (pc);
  info->proc_desc = proc_desc;
  gdb_assert (proc_desc != NULL);

  info->saved_regs = frame_obstack_zalloc (SIZEOF_FRAME_SAVED_REGS);

  /* The VFP of the frame is at FRAME_REG+FRAME_OFFSET.  */
  frame_unwind_unsigned_register (next_frame, PROC_FRAME_REG (proc_desc), &vfp);
  vfp += PROC_FRAME_OFFSET (info->proc_desc);
  info->vfp = vfp;

  /* Fill in the offsets for the registers which gen_mask says were saved.  */

  reg_position = vfp + PROC_REG_OFFSET (proc_desc);
  mask = PROC_REG_MASK (proc_desc);
  returnreg = PROC_PC_REG (proc_desc);

  /* Note that RA is always saved first, regardless of its actual
     register number.  */
  if (mask & (1 << returnreg))
    {
      /* Clear bit for RA so we don't save it again later. */
      mask &= ~(1 << returnreg);

      info->saved_regs[returnreg] = reg_position;
      reg_position += 8;
    }

  for (ireg = 0; ireg <= 31; ++ireg)
    if (mask & (1 << ireg))
      {
	info->saved_regs[ireg] = reg_position;
	reg_position += 8;
      }

  reg_position = vfp + PROC_FREG_OFFSET (proc_desc);
  mask = PROC_FREG_MASK (proc_desc);

  for (ireg = 0; ireg <= 31; ++ireg)
    if (mask & (1 << ireg))
      {
	info->saved_regs[ALPHA_FP0_REGNUM + ireg] = reg_position;
	reg_position += 8;
      }

  return info;
}

/* Given a GDB frame, determine the address of the calling function's
   frame.  This will be used to create a new GDB frame struct.  */

static void
alpha_mdebug_frame_this_id (struct frame_info *next_frame,
			    void **this_prologue_cache,
			    struct frame_id *this_id)
{
  struct alpha_mdebug_unwind_cache *info
    = alpha_mdebug_frame_unwind_cache (next_frame, this_prologue_cache);

  *this_id = frame_id_build (info->vfp, frame_func_unwind (next_frame));
}

/* Retrieve the value of REGNUM in FRAME.  Don't give up!  */

static void
alpha_mdebug_frame_prev_register (struct frame_info *next_frame,
				  void **this_prologue_cache,
				  int regnum, int *optimizedp,
				  enum lval_type *lvalp, CORE_ADDR *addrp,
				  int *realnump, void *bufferp)
{
  struct alpha_mdebug_unwind_cache *info
    = alpha_mdebug_frame_unwind_cache (next_frame, this_prologue_cache);

  /* The PC of the previous frame is stored in the link register of
     the current frame.  Frob regnum so that we pull the value from
     the correct place.  */
  if (regnum == ALPHA_PC_REGNUM)
    regnum = PROC_PC_REG (info->proc_desc);
  
  /* For all registers known to be saved in the current frame, 
     do the obvious and pull the value out.  */
  if (info->saved_regs[regnum])
    {
      *optimizedp = 0;
      *lvalp = lval_memory;
      *addrp = info->saved_regs[regnum];
      *realnump = -1;
      if (bufferp != NULL)
	get_frame_memory (next_frame, *addrp, bufferp, ALPHA_REGISTER_SIZE);
      return;
    }

  /* The stack pointer of the previous frame is computed by popping
     the current stack frame.  */
  if (regnum == ALPHA_SP_REGNUM)
    {
      *optimizedp = 0;
      *lvalp = not_lval;
      *addrp = 0;
      *realnump = -1;
      if (bufferp != NULL)
	store_unsigned_integer (bufferp, ALPHA_REGISTER_SIZE, info->vfp);
      return;
    }

  /* Otherwise assume the next frame has the same register value.  */
  frame_register (next_frame, regnum, optimizedp, lvalp, addrp,
  		  realnump, bufferp);
}

static const struct frame_unwind alpha_mdebug_frame_unwind = {
  NORMAL_FRAME,
  alpha_mdebug_frame_this_id,
  alpha_mdebug_frame_prev_register
};

const struct frame_unwind *
alpha_mdebug_frame_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  alpha_extra_func_info_t proc_desc;

  /* If this PC does not map to a PDR, then clearly this isn't an
     mdebug frame.  */
  proc_desc = find_proc_desc (pc);
  if (proc_desc == NULL)
    return NULL;

  /* If we're in the prologue, the PDR for this frame is not yet valid.
     Say no here and we'll fall back on the heuristic unwinder.  */
  if (alpha_mdebug_in_prologue (pc, proc_desc))
    return NULL;

  return &alpha_mdebug_frame_unwind;
}

static CORE_ADDR
alpha_mdebug_frame_base_address (struct frame_info *next_frame,
				 void **this_prologue_cache)
{
  struct alpha_mdebug_unwind_cache *info
    = alpha_mdebug_frame_unwind_cache (next_frame, this_prologue_cache);

  return info->vfp;
}

static CORE_ADDR
alpha_mdebug_frame_locals_address (struct frame_info *next_frame,
				   void **this_prologue_cache)
{
  struct alpha_mdebug_unwind_cache *info
    = alpha_mdebug_frame_unwind_cache (next_frame, this_prologue_cache);

  return info->vfp - PROC_LOCALOFF (info->proc_desc);
}

static CORE_ADDR
alpha_mdebug_frame_args_address (struct frame_info *next_frame,
				 void **this_prologue_cache)
{
  struct alpha_mdebug_unwind_cache *info
    = alpha_mdebug_frame_unwind_cache (next_frame, this_prologue_cache);

  return info->vfp - ALPHA_NUM_ARG_REGS * 8;
}

static const struct frame_base alpha_mdebug_frame_base = {
  &alpha_mdebug_frame_unwind,
  alpha_mdebug_frame_base_address,
  alpha_mdebug_frame_locals_address,
  alpha_mdebug_frame_args_address
};

static const struct frame_base *
alpha_mdebug_frame_base_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  alpha_extra_func_info_t proc_desc;

  /* If this PC does not map to a PDR, then clearly this isn't an
     mdebug frame.  */
  proc_desc = find_proc_desc (pc);
  if (proc_desc == NULL)
    return NULL;

  return &alpha_mdebug_frame_base;
}


void
alpha_mdebug_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  frame_unwind_append_sniffer (gdbarch, alpha_mdebug_frame_sniffer);
  frame_base_append_sniffer (gdbarch, alpha_mdebug_frame_base_sniffer);
}
