/* Common target dependent code for GDB on ARM systems.
   Copyright 1988, 1989, 1991, 1992, 1993, 1995, 1996, 1998, 1999, 2000,
   2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

#include <ctype.h>		/* XXX for isupper () */

#include "defs.h"
#include "frame.h"
#include "inferior.h"
#include "gdbcmd.h"
#include "gdbcore.h"
#include "gdb_string.h"
#include "dis-asm.h"		/* For register styles. */
#include "regcache.h"
#include "doublest.h"
#include "value.h"
#include "arch-utils.h"
#include "osabi.h"
#include "frame-unwind.h"
#include "frame-base.h"
#include "trad-frame.h"

#include "arm-tdep.h"
#include "gdb/sim-arm.h"

#include "elf-bfd.h"
#include "coff/internal.h"
#include "elf/arm.h"

#include "gdb_assert.h"

static int arm_debug;

/* Each OS has a different mechanism for accessing the various
   registers stored in the sigcontext structure.

   SIGCONTEXT_REGISTER_ADDRESS should be defined to the name (or
   function pointer) which may be used to determine the addresses
   of the various saved registers in the sigcontext structure.

   For the ARM target, there are three parameters to this function. 
   The first is the pc value of the frame under consideration, the
   second the stack pointer of this frame, and the last is the
   register number to fetch.  

   If the tm.h file does not define this macro, then it's assumed that
   no mechanism is needed and we define SIGCONTEXT_REGISTER_ADDRESS to
   be 0. 
   
   When it comes time to multi-arching this code, see the identically
   named machinery in ia64-tdep.c for an example of how it could be
   done.  It should not be necessary to modify the code below where
   this macro is used.  */

#ifdef SIGCONTEXT_REGISTER_ADDRESS
#ifndef SIGCONTEXT_REGISTER_ADDRESS_P
#define SIGCONTEXT_REGISTER_ADDRESS_P() 1
#endif
#else
#define SIGCONTEXT_REGISTER_ADDRESS(SP,PC,REG) 0
#define SIGCONTEXT_REGISTER_ADDRESS_P() 0
#endif

/* Macros for setting and testing a bit in a minimal symbol that marks
   it as Thumb function.  The MSB of the minimal symbol's "info" field
   is used for this purpose.

   MSYMBOL_SET_SPECIAL	Actually sets the "special" bit.
   MSYMBOL_IS_SPECIAL   Tests the "special" bit in a minimal symbol.  */

#define MSYMBOL_SET_SPECIAL(msym)					\
	MSYMBOL_INFO (msym) = (char *) (((long) MSYMBOL_INFO (msym))	\
					| 0x80000000)

#define MSYMBOL_IS_SPECIAL(msym)				\
	(((long) MSYMBOL_INFO (msym) & 0x80000000) != 0)

/* The list of available "set arm ..." and "show arm ..." commands.  */
static struct cmd_list_element *setarmcmdlist = NULL;
static struct cmd_list_element *showarmcmdlist = NULL;

/* The type of floating-point to use.  Keep this in sync with enum
   arm_float_model, and the help string in _initialize_arm_tdep.  */
static const char *fp_model_strings[] =
{
  "auto",
  "softfpa",
  "fpa",
  "softvfp",
  "vfp"
};

/* A variable that can be configured by the user.  */
static enum arm_float_model arm_fp_model = ARM_FLOAT_AUTO;
static const char *current_fp_model = "auto";

/* Number of different reg name sets (options).  */
static int num_disassembly_options;

/* We have more registers than the disassembler as gdb can print the value
   of special registers as well.
   The general register names are overwritten by whatever is being used by
   the disassembler at the moment. We also adjust the case of cpsr and fps.  */

/* Initial value: Register names used in ARM's ISA documentation.  */
static char * arm_register_name_strings[] =
{"r0",  "r1",  "r2",  "r3",	/*  0  1  2  3 */
 "r4",  "r5",  "r6",  "r7",	/*  4  5  6  7 */
 "r8",  "r9",  "r10", "r11",	/*  8  9 10 11 */
 "r12", "sp",  "lr",  "pc",	/* 12 13 14 15 */
 "f0",  "f1",  "f2",  "f3",	/* 16 17 18 19 */
 "f4",  "f5",  "f6",  "f7",	/* 20 21 22 23 */
 "fps", "cpsr" };		/* 24 25       */
static char **arm_register_names = arm_register_name_strings;

/* Valid register name styles.  */
static const char **valid_disassembly_styles;

/* Disassembly style to use. Default to "std" register names.  */
static const char *disassembly_style;
/* Index to that option in the opcodes table.  */
static int current_option;

/* This is used to keep the bfd arch_info in sync with the disassembly
   style.  */
static void set_disassembly_style_sfunc(char *, int,
					 struct cmd_list_element *);
static void set_disassembly_style (void);

static void convert_from_extended (const struct floatformat *, const void *,
				   void *);
static void convert_to_extended (const struct floatformat *, void *,
				 const void *);

struct arm_prologue_cache
{
  /* The stack pointer at the time this frame was created; i.e. the
     caller's stack pointer when this function was called.  It is used
     to identify this frame.  */
  CORE_ADDR prev_sp;

  /* The frame base for this frame is just prev_sp + frame offset -
     frame size.  FRAMESIZE is the size of this stack frame, and
     FRAMEOFFSET if the initial offset from the stack pointer (this
     frame's stack pointer, not PREV_SP) to the frame base.  */

  int framesize;
  int frameoffset;

  /* The register used to hold the frame pointer for this frame.  */
  int framereg;

  /* Saved register offsets.  */
  struct trad_frame_saved_reg *saved_regs;
};

/* Addresses for calling Thumb functions have the bit 0 set.
   Here are some macros to test, set, or clear bit 0 of addresses.  */
#define IS_THUMB_ADDR(addr)	((addr) & 1)
#define MAKE_THUMB_ADDR(addr)	((addr) | 1)
#define UNMAKE_THUMB_ADDR(addr) ((addr) & ~1)

/* Set to true if the 32-bit mode is in use.  */

int arm_apcs_32 = 1;

/* Determine if the program counter specified in MEMADDR is in a Thumb
   function.  */

int
arm_pc_is_thumb (CORE_ADDR memaddr)
{
  struct minimal_symbol *sym;

  /* If bit 0 of the address is set, assume this is a Thumb address.  */
  if (IS_THUMB_ADDR (memaddr))
    return 1;

  /* Thumb functions have a "special" bit set in minimal symbols.  */
  sym = lookup_minimal_symbol_by_pc (memaddr);
  if (sym)
    {
      return (MSYMBOL_IS_SPECIAL (sym));
    }
  else
    {
      return 0;
    }
}

/* Remove useless bits from addresses in a running program.  */
static CORE_ADDR
arm_addr_bits_remove (CORE_ADDR val)
{
  if (arm_apcs_32)
    return (val & (arm_pc_is_thumb (val) ? 0xfffffffe : 0xfffffffc));
  else
    return (val & 0x03fffffc);
}

/* When reading symbols, we need to zap the low bit of the address,
   which may be set to 1 for Thumb functions.  */
static CORE_ADDR
arm_smash_text_address (CORE_ADDR val)
{
  return val & ~1;
}

/* Immediately after a function call, return the saved pc.  Can't
   always go through the frames for this because on some machines the
   new frame is not set up until the new function executes some
   instructions.  */

static CORE_ADDR
arm_saved_pc_after_call (struct frame_info *frame)
{
  return ADDR_BITS_REMOVE (read_register (ARM_LR_REGNUM));
}

/* Determine whether the function invocation represented by FI has a
   frame on the stack associated with it.  If it does return zero,
   otherwise return 1.  */

static int
arm_frameless_function_invocation (struct frame_info *fi)
{
  CORE_ADDR func_start, after_prologue;
  int frameless;

  /* Sometimes we have functions that do a little setup (like saving the
     vN registers with the stmdb instruction, but DO NOT set up a frame.
     The symbol table will report this as a prologue.  However, it is
     important not to try to parse these partial frames as frames, or we
     will get really confused.

     So I will demand 3 instructions between the start & end of the
     prologue before I call it a real prologue, i.e. at least
	mov ip, sp,
	stmdb sp!, {}
	sub sp, ip, #4.  */

  func_start = (get_frame_func (fi) + FUNCTION_START_OFFSET);
  after_prologue = SKIP_PROLOGUE (func_start);

  /* There are some frameless functions whose first two instructions
     follow the standard APCS form, in which case after_prologue will
     be func_start + 8.  */

  frameless = (after_prologue < func_start + 12);
  return frameless;
}

/* A typical Thumb prologue looks like this:
   push    {r7, lr}
   add     sp, sp, #-28
   add     r7, sp, #12
   Sometimes the latter instruction may be replaced by:
   mov     r7, sp
   
   or like this:
   push    {r7, lr}
   mov     r7, sp
   sub	   sp, #12
   
   or, on tpcs, like this:
   sub     sp,#16
   push    {r7, lr}
   (many instructions)
   mov     r7, sp
   sub	   sp, #12

   There is always one instruction of three classes:
   1 - push
   2 - setting of r7
   3 - adjusting of sp
   
   When we have found at least one of each class we are done with the prolog.
   Note that the "sub sp, #NN" before the push does not count.
   */

static CORE_ADDR
thumb_skip_prologue (CORE_ADDR pc, CORE_ADDR func_end)
{
  CORE_ADDR current_pc;
  /* findmask:
     bit 0 - push { rlist }
     bit 1 - mov r7, sp  OR  add r7, sp, #imm  (setting of r7)
     bit 2 - sub sp, #simm  OR  add sp, #simm  (adjusting of sp)
  */
  int findmask = 0;

  for (current_pc = pc;
       current_pc + 2 < func_end && current_pc < pc + 40;
       current_pc += 2)
    {
      unsigned short insn = read_memory_unsigned_integer (current_pc, 2);

      if ((insn & 0xfe00) == 0xb400)		/* push { rlist } */
	{
	  findmask |= 1;			/* push found */
	}
      else if ((insn & 0xff00) == 0xb000)	/* add sp, #simm  OR  
						   sub sp, #simm */
	{
	  if ((findmask & 1) == 0)		/* before push ? */
	    continue;
	  else
	    findmask |= 4;			/* add/sub sp found */
	}
      else if ((insn & 0xff00) == 0xaf00)	/* add r7, sp, #imm */
	{
	  findmask |= 2;			/* setting of r7 found */
	}
      else if (insn == 0x466f)			/* mov r7, sp */
	{
	  findmask |= 2;			/* setting of r7 found */
	}
      else if (findmask == (4+2+1))
	{
	  /* We have found one of each type of prologue instruction */
	  break;
	}
      else
	/* Something in the prolog that we don't care about or some
	   instruction from outside the prolog scheduled here for
	   optimization.  */
	continue;
    }

  return current_pc;
}

/* Advance the PC across any function entry prologue instructions to
   reach some "real" code.

   The APCS (ARM Procedure Call Standard) defines the following
   prologue:

   mov          ip, sp
   [stmfd       sp!, {a1,a2,a3,a4}]
   stmfd        sp!, {...,fp,ip,lr,pc}
   [stfe        f7, [sp, #-12]!]
   [stfe        f6, [sp, #-12]!]
   [stfe        f5, [sp, #-12]!]
   [stfe        f4, [sp, #-12]!]
   sub fp, ip, #nn @@ nn == 20 or 4 depending on second insn */

static CORE_ADDR
arm_skip_prologue (CORE_ADDR pc)
{
  unsigned long inst;
  CORE_ADDR skip_pc;
  CORE_ADDR func_addr, func_end = 0;
  char *func_name;
  struct symtab_and_line sal;

  /* If we're in a dummy frame, don't even try to skip the prologue.  */
  if (DEPRECATED_PC_IN_CALL_DUMMY (pc, 0, 0))
    return pc;

  /* See what the symbol table says.  */

  if (find_pc_partial_function (pc, &func_name, &func_addr, &func_end))
    {
      struct symbol *sym;

      /* Found a function.  */
      sym = lookup_symbol (func_name, NULL, VAR_DOMAIN, NULL, NULL);
      if (sym && SYMBOL_LANGUAGE (sym) != language_asm)
        {
	  /* Don't use this trick for assembly source files.  */
	  sal = find_pc_line (func_addr, 0);
	  if ((sal.line != 0) && (sal.end < func_end))
	    return sal.end;
        }
    }

  /* Check if this is Thumb code.  */
  if (arm_pc_is_thumb (pc))
    return thumb_skip_prologue (pc, func_end);

  /* Can't find the prologue end in the symbol table, try it the hard way
     by disassembling the instructions.  */

  /* Like arm_scan_prologue, stop no later than pc + 64. */
  if (func_end == 0 || func_end > pc + 64)
    func_end = pc + 64;

  for (skip_pc = pc; skip_pc < func_end; skip_pc += 4)
    {
      inst = read_memory_integer (skip_pc, 4);

      /* "mov ip, sp" is no longer a required part of the prologue.  */
      if (inst == 0xe1a0c00d)			/* mov ip, sp */
	continue;

      if ((inst & 0xfffff000) == 0xe28dc000)    /* add ip, sp #n */
	continue;

      if ((inst & 0xfffff000) == 0xe24dc000)    /* sub ip, sp #n */
	continue;

      /* Some prologues begin with "str lr, [sp, #-4]!".  */
      if (inst == 0xe52de004)			/* str lr, [sp, #-4]! */
	continue;

      if ((inst & 0xfffffff0) == 0xe92d0000)	/* stmfd sp!,{a1,a2,a3,a4} */
	continue;

      if ((inst & 0xfffff800) == 0xe92dd800)	/* stmfd sp!,{fp,ip,lr,pc} */
	continue;

      /* Any insns after this point may float into the code, if it makes
	 for better instruction scheduling, so we skip them only if we
	 find them, but still consider the function to be frame-ful.  */

      /* We may have either one sfmfd instruction here, or several stfe
	 insns, depending on the version of floating point code we
	 support.  */
      if ((inst & 0xffbf0fff) == 0xec2d0200)	/* sfmfd fn, <cnt>, [sp]! */
	continue;

      if ((inst & 0xffff8fff) == 0xed6d0103)	/* stfe fn, [sp, #-12]! */
	continue;

      if ((inst & 0xfffff000) == 0xe24cb000)	/* sub fp, ip, #nn */
	continue;

      if ((inst & 0xfffff000) == 0xe24dd000)	/* sub sp, sp, #nn */
	continue;

      if ((inst & 0xffffc000) == 0xe54b0000 ||	/* strb r(0123),[r11,#-nn] */
	  (inst & 0xffffc0f0) == 0xe14b00b0 ||	/* strh r(0123),[r11,#-nn] */
	  (inst & 0xffffc000) == 0xe50b0000)	/* str  r(0123),[r11,#-nn] */
	continue;

      if ((inst & 0xffffc000) == 0xe5cd0000 ||	/* strb r(0123),[sp,#nn] */
	  (inst & 0xffffc0f0) == 0xe1cd00b0 ||	/* strh r(0123),[sp,#nn] */
	  (inst & 0xffffc000) == 0xe58d0000)	/* str  r(0123),[sp,#nn] */
	continue;

      /* Un-recognized instruction; stop scanning.  */
      break;
    }

  return skip_pc;		/* End of prologue */
}

/* *INDENT-OFF* */
/* Function: thumb_scan_prologue (helper function for arm_scan_prologue)
   This function decodes a Thumb function prologue to determine:
     1) the size of the stack frame
     2) which registers are saved on it
     3) the offsets of saved regs
     4) the offset from the stack pointer to the frame pointer

   A typical Thumb function prologue would create this stack frame
   (offsets relative to FP)
     old SP ->	24  stack parameters
		20  LR
		16  R7
     R7 ->       0  local variables (16 bytes)
     SP ->     -12  additional stack space (12 bytes)
   The frame size would thus be 36 bytes, and the frame offset would be
   12 bytes.  The frame register is R7. 
   
   The comments for thumb_skip_prolog() describe the algorithm we use
   to detect the end of the prolog.  */
/* *INDENT-ON* */

static void
thumb_scan_prologue (CORE_ADDR prev_pc, struct arm_prologue_cache *cache)
{
  CORE_ADDR prologue_start;
  CORE_ADDR prologue_end;
  CORE_ADDR current_pc;
  /* Which register has been copied to register n?  */
  int saved_reg[16];
  /* findmask:
     bit 0 - push { rlist }
     bit 1 - mov r7, sp  OR  add r7, sp, #imm  (setting of r7)
     bit 2 - sub sp, #simm  OR  add sp, #simm  (adjusting of sp)
  */
  int findmask = 0;
  int i;

  if (find_pc_partial_function (prev_pc, NULL, &prologue_start, &prologue_end))
    {
      struct symtab_and_line sal = find_pc_line (prologue_start, 0);

      if (sal.line == 0)		/* no line info, use current PC  */
	prologue_end = prev_pc;
      else if (sal.end < prologue_end)	/* next line begins after fn end */
	prologue_end = sal.end;		/* (probably means no prologue)  */
    }
  else
    /* We're in the boondocks: allow for 
       16 pushes, an add, and "mv fp,sp".  */
    prologue_end = prologue_start + 40;

  prologue_end = min (prologue_end, prev_pc);

  /* Initialize the saved register map.  When register H is copied to
     register L, we will put H in saved_reg[L].  */
  for (i = 0; i < 16; i++)
    saved_reg[i] = i;

  /* Search the prologue looking for instructions that set up the
     frame pointer, adjust the stack pointer, and save registers.
     Do this until all basic prolog instructions are found.  */

  cache->framesize = 0;
  for (current_pc = prologue_start;
       (current_pc < prologue_end) && ((findmask & 7) != 7);
       current_pc += 2)
    {
      unsigned short insn;
      int regno;
      int offset;

      insn = read_memory_unsigned_integer (current_pc, 2);

      if ((insn & 0xfe00) == 0xb400)	/* push { rlist } */
	{
	  int mask;
	  findmask |= 1;		/* push found */
	  /* Bits 0-7 contain a mask for registers R0-R7.  Bit 8 says
	     whether to save LR (R14).  */
	  mask = (insn & 0xff) | ((insn & 0x100) << 6);

	  /* Calculate offsets of saved R0-R7 and LR.  */
	  for (regno = ARM_LR_REGNUM; regno >= 0; regno--)
	    if (mask & (1 << regno))
	      {
		cache->framesize += 4;
		cache->saved_regs[saved_reg[regno]].addr = -cache->framesize;
		/* Reset saved register map.  */
		saved_reg[regno] = regno;
	      }
	}
      else if ((insn & 0xff00) == 0xb000)	/* add sp, #simm  OR  
						   sub sp, #simm */
	{
	  if ((findmask & 1) == 0)		/* before push?  */
	    continue;
	  else
	    findmask |= 4;			/* add/sub sp found */
	  
	  offset = (insn & 0x7f) << 2;		/* get scaled offset */
	  if (insn & 0x80)		/* is it signed? (==subtracting) */
	    {
	      cache->frameoffset += offset;
	      offset = -offset;
	    }
	  cache->framesize -= offset;
	}
      else if ((insn & 0xff00) == 0xaf00)	/* add r7, sp, #imm */
	{
	  findmask |= 2;			/* setting of r7 found */
	  cache->framereg = THUMB_FP_REGNUM;
	  /* get scaled offset */
	  cache->frameoffset = (insn & 0xff) << 2;
	}
      else if (insn == 0x466f)			/* mov r7, sp */
	{
	  findmask |= 2;			/* setting of r7 found */
	  cache->framereg = THUMB_FP_REGNUM;
	  cache->frameoffset = 0;
	  saved_reg[THUMB_FP_REGNUM] = ARM_SP_REGNUM;
	}
      else if ((insn & 0xffc0) == 0x4640)	/* mov r0-r7, r8-r15 */
	{
	  int lo_reg = insn & 7;		/* dest.  register (r0-r7) */
	  int hi_reg = ((insn >> 3) & 7) + 8;	/* source register (r8-15) */
	  saved_reg[lo_reg] = hi_reg;		/* remember hi reg was saved */
	}
      else
	/* Something in the prolog that we don't care about or some
	   instruction from outside the prolog scheduled here for
	   optimization.  */ 
	continue;
    }
}

/* This function decodes an ARM function prologue to determine:
   1) the size of the stack frame
   2) which registers are saved on it
   3) the offsets of saved regs
   4) the offset from the stack pointer to the frame pointer
   This information is stored in the "extra" fields of the frame_info.

   There are two basic forms for the ARM prologue.  The fixed argument
   function call will look like:

   mov    ip, sp
   stmfd  sp!, {fp, ip, lr, pc}
   sub    fp, ip, #4
   [sub sp, sp, #4]

   Which would create this stack frame (offsets relative to FP):
   IP ->   4    (caller's stack)
   FP ->   0    PC (points to address of stmfd instruction + 8 in callee)
   -4   LR (return address in caller)
   -8   IP (copy of caller's SP)
   -12  FP (caller's FP)
   SP -> -28    Local variables

   The frame size would thus be 32 bytes, and the frame offset would be
   28 bytes.  The stmfd call can also save any of the vN registers it
   plans to use, which increases the frame size accordingly.

   Note: The stored PC is 8 off of the STMFD instruction that stored it
   because the ARM Store instructions always store PC + 8 when you read
   the PC register.

   A variable argument function call will look like:

   mov    ip, sp
   stmfd  sp!, {a1, a2, a3, a4}
   stmfd  sp!, {fp, ip, lr, pc}
   sub    fp, ip, #20

   Which would create this stack frame (offsets relative to FP):
   IP ->  20    (caller's stack)
   16  A4
   12  A3
   8  A2
   4  A1
   FP ->   0    PC (points to address of stmfd instruction + 8 in callee)
   -4   LR (return address in caller)
   -8   IP (copy of caller's SP)
   -12  FP (caller's FP)
   SP -> -28    Local variables

   The frame size would thus be 48 bytes, and the frame offset would be
   28 bytes.

   There is another potential complication, which is that the optimizer
   will try to separate the store of fp in the "stmfd" instruction from
   the "sub fp, ip, #NN" instruction.  Almost anything can be there, so
   we just key on the stmfd, and then scan for the "sub fp, ip, #NN"...

   Also, note, the original version of the ARM toolchain claimed that there
   should be an

   instruction at the end of the prologue.  I have never seen GCC produce
   this, and the ARM docs don't mention it.  We still test for it below in
   case it happens...

 */

static void
arm_scan_prologue (struct frame_info *next_frame, struct arm_prologue_cache *cache)
{
  int regno, sp_offset, fp_offset, ip_offset;
  CORE_ADDR prologue_start, prologue_end, current_pc;
  CORE_ADDR prev_pc = frame_pc_unwind (next_frame);

  /* Assume there is no frame until proven otherwise.  */
  cache->framereg = ARM_SP_REGNUM;
  cache->framesize = 0;
  cache->frameoffset = 0;

  if (frame_tdep_pc_fixup)
  	frame_tdep_pc_fixup(&prev_pc);

  /* Check for Thumb prologue.  */
  if (arm_pc_is_thumb (prev_pc))
    {
      thumb_scan_prologue (prev_pc, cache);
      return;
    }

  /* Find the function prologue.  If we can't find the function in
     the symbol table, peek in the stack frame to find the PC.  */
  if (find_pc_partial_function (prev_pc, NULL, &prologue_start, &prologue_end))
    {
      /* One way to find the end of the prologue (which works well
         for unoptimized code) is to do the following:

	    struct symtab_and_line sal = find_pc_line (prologue_start, 0);

	    if (sal.line == 0)
	      prologue_end = prev_pc;
	    else if (sal.end < prologue_end)
	      prologue_end = sal.end;

	 This mechanism is very accurate so long as the optimizer
	 doesn't move any instructions from the function body into the
	 prologue.  If this happens, sal.end will be the last
	 instruction in the first hunk of prologue code just before
	 the first instruction that the scheduler has moved from
	 the body to the prologue.

	 In order to make sure that we scan all of the prologue
	 instructions, we use a slightly less accurate mechanism which
	 may scan more than necessary.  To help compensate for this
	 lack of accuracy, the prologue scanning loop below contains
	 several clauses which'll cause the loop to terminate early if
	 an implausible prologue instruction is encountered.  
	 
	 The expression
	 
	      prologue_start + 64
	    
	 is a suitable endpoint since it accounts for the largest
	 possible prologue plus up to five instructions inserted by
	 the scheduler.  */
         
      if (prologue_end > prologue_start + 64)
	{
	  prologue_end = prologue_start + 64;	/* See above.  */
	}
    }
  else
    {
      /* We have no symbol information.  Our only option is to assume this
	 function has a standard stack frame and the normal frame register.
	 Then, we can find the value of our frame pointer on entrance to
	 the callee (or at the present moment if this is the innermost frame).
	 The value stored there should be the address of the stmfd + 8.  */
      CORE_ADDR frame_loc;
      LONGEST return_value;

      frame_loc = frame_unwind_register_unsigned (next_frame, ARM_FP_REGNUM);
      if (!safe_read_memory_integer (frame_loc, 4, &return_value))
        return;
      else
        {
          prologue_start = ADDR_BITS_REMOVE (return_value) - 8;
          prologue_end = prologue_start + 64;	/* See above.  */
        }
    }

  if (prev_pc < prologue_end)
    prologue_end = prev_pc;

  /* Now search the prologue looking for instructions that set up the
     frame pointer, adjust the stack pointer, and save registers.

     Be careful, however, and if it doesn't look like a prologue,
     don't try to scan it.  If, for instance, a frameless function
     begins with stmfd sp!, then we will tell ourselves there is
     a frame, which will confuse stack traceback, as well as "finish" 
     and other operations that rely on a knowledge of the stack
     traceback.

     In the APCS, the prologue should start with  "mov ip, sp" so
     if we don't see this as the first insn, we will stop.  

     [Note: This doesn't seem to be true any longer, so it's now an
     optional part of the prologue.  - Kevin Buettner, 2001-11-20]

     [Note further: The "mov ip,sp" only seems to be missing in
     frameless functions at optimization level "-O2" or above,
     in which case it is often (but not always) replaced by
     "str lr, [sp, #-4]!".  - Michael Snyder, 2002-04-23]  */

  sp_offset = fp_offset = ip_offset = 0;

  for (current_pc = prologue_start;
       current_pc < prologue_end;
       current_pc += 4)
    {
      unsigned int insn = read_memory_unsigned_integer (current_pc, 4);

      if (insn == 0xe1a0c00d)		/* mov ip, sp */
	{
	  ip_offset = 0;
	  continue;
	}
      else if ((insn & 0xfffff000) == 0xe28dc000) /* add ip, sp #n */
	{
	  unsigned imm = insn & 0xff;                   /* immediate value */
	  unsigned rot = (insn & 0xf00) >> 7;           /* rotate amount */
	  imm = (imm >> rot) | (imm << (32 - rot));
	  ip_offset = imm;
	  continue;
	}
      else if ((insn & 0xfffff000) == 0xe24dc000) /* sub ip, sp #n */
	{
	  unsigned imm = insn & 0xff;                   /* immediate value */
	  unsigned rot = (insn & 0xf00) >> 7;           /* rotate amount */
	  imm = (imm >> rot) | (imm << (32 - rot));
	  ip_offset = -imm;
	  continue;
	}
      else if (insn == 0xe52de004)	/* str lr, [sp, #-4]! */
	{
	  sp_offset -= 4;
	  cache->saved_regs[ARM_LR_REGNUM].addr = sp_offset;
	  continue;
	}
      else if ((insn & 0xffff0000) == 0xe92d0000)
	/* stmfd sp!, {..., fp, ip, lr, pc}
	   or
	   stmfd sp!, {a1, a2, a3, a4}  */
	{
	  int mask = insn & 0xffff;

	  /* Calculate offsets of saved registers.  */
	  for (regno = ARM_PC_REGNUM; regno >= 0; regno--)
	    if (mask & (1 << regno))
	      {
		sp_offset -= 4;
		cache->saved_regs[regno].addr = sp_offset;
	      }
	}
      else if ((insn & 0xffffc000) == 0xe54b0000 ||	/* strb rx,[r11,#-n] */
	       (insn & 0xffffc0f0) == 0xe14b00b0 ||	/* strh rx,[r11,#-n] */
	       (insn & 0xffffc000) == 0xe50b0000)	/* str  rx,[r11,#-n] */
	{
	  /* No need to add this to saved_regs -- it's just an arg reg.  */
	  continue;
	}
      else if ((insn & 0xffffc000) == 0xe5cd0000 ||	/* strb rx,[sp,#n] */
	       (insn & 0xffffc0f0) == 0xe1cd00b0 ||	/* strh rx,[sp,#n] */
	       (insn & 0xffffc000) == 0xe58d0000)	/* str  rx,[sp,#n] */
	{
	  /* No need to add this to saved_regs -- it's just an arg reg.  */
	  continue;
	}
      else if ((insn & 0xfffff000) == 0xe24cb000)	/* sub fp, ip #n */
	{
	  unsigned imm = insn & 0xff;			/* immediate value */
	  unsigned rot = (insn & 0xf00) >> 7;		/* rotate amount */
	  imm = (imm >> rot) | (imm << (32 - rot));
	  fp_offset = -imm + ip_offset;
	  cache->framereg = ARM_FP_REGNUM;
	}
      else if ((insn & 0xfffff000) == 0xe24dd000)	/* sub sp, sp #n */
	{
	  unsigned imm = insn & 0xff;			/* immediate value */
	  unsigned rot = (insn & 0xf00) >> 7;		/* rotate amount */
	  imm = (imm >> rot) | (imm << (32 - rot));
	  sp_offset -= imm;
	}
      else if ((insn & 0xffff7fff) == 0xed6d0103)	/* stfe f?, [sp, -#c]! */
	{
	  sp_offset -= 12;
	  regno = ARM_F0_REGNUM + ((insn >> 12) & 0x07);
	  cache->saved_regs[regno].addr = sp_offset;
	}
      else if ((insn & 0xffbf0fff) == 0xec2d0200)	/* sfmfd f0, 4, [sp!] */
	{
	  int n_saved_fp_regs;
	  unsigned int fp_start_reg, fp_bound_reg;

	  if ((insn & 0x800) == 0x800)		/* N0 is set */
	    {
	      if ((insn & 0x40000) == 0x40000)	/* N1 is set */
		n_saved_fp_regs = 3;
	      else
		n_saved_fp_regs = 1;
	    }
	  else
	    {
	      if ((insn & 0x40000) == 0x40000)	/* N1 is set */
		n_saved_fp_regs = 2;
	      else
		n_saved_fp_regs = 4;
	    }

	  fp_start_reg = ARM_F0_REGNUM + ((insn >> 12) & 0x7);
	  fp_bound_reg = fp_start_reg + n_saved_fp_regs;
	  for (; fp_start_reg < fp_bound_reg; fp_start_reg++)
	    {
	      sp_offset -= 12;
	      cache->saved_regs[fp_start_reg++].addr = sp_offset;
	    }
	}
      else if ((insn & 0xf0000000) != 0xe0000000)
	break;			/* Condition not true, exit early */
      else if ((insn & 0xfe200000) == 0xe8200000)	/* ldm? */
	break;			/* Don't scan past a block load */
      else
	/* The optimizer might shove anything into the prologue,
	   so we just skip what we don't recognize.  */
	continue;
    }

  /* The frame size is just the negative of the offset (from the
     original SP) of the last thing thing we pushed on the stack. 
     The frame offset is [new FP] - [new SP].  */
  cache->framesize = -sp_offset;
  if (cache->framereg == ARM_FP_REGNUM)
    cache->frameoffset = fp_offset - sp_offset;
  else
    cache->frameoffset = 0;
}

static struct arm_prologue_cache *
arm_make_prologue_cache (struct frame_info *next_frame)
{
  int reg;
  struct arm_prologue_cache *cache;
  CORE_ADDR unwound_fp;

  cache = frame_obstack_zalloc (sizeof (struct arm_prologue_cache));
  cache->saved_regs = trad_frame_alloc_saved_regs (next_frame);

  arm_scan_prologue (next_frame, cache);
  unwound_fp = frame_unwind_register_unsigned (next_frame, cache->framereg);
  if (unwound_fp == 0)
    return cache;

  cache->prev_sp = unwound_fp + cache->framesize - cache->frameoffset;

  /* Calculate actual addresses of saved registers using offsets
     determined by arm_scan_prologue.  */
  for (reg = 0; reg < NUM_REGS; reg++)
    if (trad_frame_addr_p (cache->saved_regs, reg))
      cache->saved_regs[reg].addr += cache->prev_sp;

  return cache;
}

/* Our frame ID for a normal frame is the current function's starting PC
   and the caller's SP when we were called.  */

static void
arm_prologue_this_id (struct frame_info *next_frame,
		      void **this_cache,
		      struct frame_id *this_id)
{
  struct arm_prologue_cache *cache;
  struct frame_id id;
  CORE_ADDR func;

  if (*this_cache == NULL)
    *this_cache = arm_make_prologue_cache (next_frame);
  cache = *this_cache;

  func = frame_func_unwind (next_frame);

  /* This is meant to halt the backtrace at "_start".  Make sure we
     don't halt it at a generic dummy frame. */
  if (func <= LOWEST_PC)
    return;

  /* If we've hit a wall, stop.  */
  if (cache->prev_sp == 0)
    return;

  id = frame_id_build (cache->prev_sp, func);

  /* Check that we're not going round in circles with the same frame
     ID (but avoid applying the test to sentinel frames which do go
     round in circles).  */
  if (frame_relative_level (next_frame) >= 0
      && get_frame_type (next_frame) == NORMAL_FRAME
      && frame_id_eq (get_frame_id (next_frame), id))
    return;

  *this_id = id;
}

static void
arm_prologue_prev_register (struct frame_info *next_frame,
			    void **this_cache,
			    int prev_regnum,
			    int *optimized,
			    enum lval_type *lvalp,
			    CORE_ADDR *addrp,
			    int *realnump,
			    void *valuep)
{
  struct arm_prologue_cache *cache;

  if (*this_cache == NULL)
    *this_cache = arm_make_prologue_cache (next_frame);
  cache = *this_cache;

  /* If we are asked to unwind the PC, then we need to return the LR
     instead.  The saved value of PC points into this frame's
     prologue, not the next frame's resume location.  */
  if (prev_regnum == ARM_PC_REGNUM)
    prev_regnum = ARM_LR_REGNUM;

  /* SP is generally not saved to the stack, but this frame is
     identified by NEXT_FRAME's stack pointer at the time of the call.
     The value was already reconstructed into PREV_SP.  */
  if (prev_regnum == ARM_SP_REGNUM)
    {
      *lvalp = not_lval;
      if (valuep)
	store_unsigned_integer (valuep, 4, cache->prev_sp);
      return;
    }

  trad_frame_prev_register (next_frame, cache->saved_regs, prev_regnum,
			    optimized, lvalp, addrp, realnump, valuep);
}

struct frame_unwind arm_prologue_unwind = {
  NORMAL_FRAME,
  arm_prologue_this_id,
  arm_prologue_prev_register
};

static const struct frame_unwind *
arm_prologue_unwind_sniffer (struct frame_info *next_frame)
{
  return &arm_prologue_unwind;
}

static CORE_ADDR
arm_normal_frame_base (struct frame_info *next_frame, void **this_cache)
{
  struct arm_prologue_cache *cache;

  if (*this_cache == NULL)
    *this_cache = arm_make_prologue_cache (next_frame);
  cache = *this_cache;

  return cache->prev_sp + cache->frameoffset - cache->framesize;
}

struct frame_base arm_normal_base = {
  &arm_prologue_unwind,
  arm_normal_frame_base,
  arm_normal_frame_base,
  arm_normal_frame_base
};

static struct arm_prologue_cache *
arm_make_sigtramp_cache (struct frame_info *next_frame)
{
  struct arm_prologue_cache *cache;
  int reg;

  cache = frame_obstack_zalloc (sizeof (struct arm_prologue_cache));

  cache->prev_sp = frame_unwind_register_unsigned (next_frame, ARM_SP_REGNUM);

  cache->saved_regs = trad_frame_alloc_saved_regs (next_frame);

  for (reg = 0; reg < NUM_REGS; reg++)
    cache->saved_regs[reg].addr
      = SIGCONTEXT_REGISTER_ADDRESS (cache->prev_sp,
				     frame_pc_unwind (next_frame), reg);

  /* FIXME: What about thumb mode?  */
  cache->framereg = ARM_SP_REGNUM;
  cache->prev_sp
    = read_memory_integer (cache->saved_regs[cache->framereg].addr,
			   register_size (current_gdbarch, cache->framereg));

  return cache;
}

static void
arm_sigtramp_this_id (struct frame_info *next_frame,
		      void **this_cache,
		      struct frame_id *this_id)
{
  struct arm_prologue_cache *cache;

  if (*this_cache == NULL)
    *this_cache = arm_make_sigtramp_cache (next_frame);
  cache = *this_cache;

  /* FIXME drow/2003-07-07: This isn't right if we single-step within
     the sigtramp frame; the PC should be the beginning of the trampoline.  */
  *this_id = frame_id_build (cache->prev_sp, frame_pc_unwind (next_frame));
}

static void
arm_sigtramp_prev_register (struct frame_info *next_frame,
			    void **this_cache,
			    int prev_regnum,
			    int *optimized,
			    enum lval_type *lvalp,
			    CORE_ADDR *addrp,
			    int *realnump,
			    void *valuep)
{
  struct arm_prologue_cache *cache;

  if (*this_cache == NULL)
    *this_cache = arm_make_sigtramp_cache (next_frame);
  cache = *this_cache;

  trad_frame_prev_register (next_frame, cache->saved_regs, prev_regnum,
			    optimized, lvalp, addrp, realnump, valuep);
}

struct frame_unwind arm_sigtramp_unwind = {
  SIGTRAMP_FRAME,
  arm_sigtramp_this_id,
  arm_sigtramp_prev_register
};

static const struct frame_unwind *
arm_sigtramp_unwind_sniffer (struct frame_info *next_frame)
{
  /* Note: If an ARM PC_IN_SIGTRAMP method ever needs to compare
     against the name of the function, the code below will have to be
     changed to first fetch the name of the function and then pass
     this name to PC_IN_SIGTRAMP.  */

  if (SIGCONTEXT_REGISTER_ADDRESS_P ()
      && PC_IN_SIGTRAMP (frame_pc_unwind (next_frame), (char *) 0))
    return &arm_sigtramp_unwind;

  return NULL;
}

/* Assuming NEXT_FRAME->prev is a dummy, return the frame ID of that
   dummy frame.  The frame ID's base needs to match the TOS value
   saved by save_dummy_frame_tos() and returned from
   arm_push_dummy_call, and the PC needs to match the dummy frame's
   breakpoint.  */

static struct frame_id
arm_unwind_dummy_id (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  return frame_id_build (frame_unwind_register_unsigned (next_frame, ARM_SP_REGNUM),
			 frame_pc_unwind (next_frame));
}

/* Given THIS_FRAME, find the previous frame's resume PC (which will
   be used to construct the previous frame's ID, after looking up the
   containing function).  */

static CORE_ADDR
arm_unwind_pc (struct gdbarch *gdbarch, struct frame_info *this_frame)
{
  CORE_ADDR pc;
  pc = frame_unwind_register_unsigned (this_frame, ARM_PC_REGNUM);
  return IS_THUMB_ADDR (pc) ? UNMAKE_THUMB_ADDR (pc) : pc;
}

static CORE_ADDR
arm_unwind_sp (struct gdbarch *gdbarch, struct frame_info *this_frame)
{
  return frame_unwind_register_unsigned (this_frame, ARM_SP_REGNUM);
}

/* DEPRECATED_CALL_DUMMY_WORDS:
   This sequence of words is the instructions

   mov  lr,pc
   mov  pc,r4
   illegal

   Note this is 12 bytes.  */

static LONGEST arm_call_dummy_words[] =
{
  0xe1a0e00f, 0xe1a0f004, 0xe7ffdefe
};

/* When arguments must be pushed onto the stack, they go on in reverse
   order.  The code below implements a FILO (stack) to do this.  */

struct stack_item
{
  int len;
  struct stack_item *prev;
  void *data;
};

static struct stack_item *
push_stack_item (struct stack_item *prev, void *contents, int len)
{
  struct stack_item *si;
  si = xmalloc (sizeof (struct stack_item));
  si->data = xmalloc (len);
  si->len = len;
  si->prev = prev;
  memcpy (si->data, contents, len);
  return si;
}

static struct stack_item *
pop_stack_item (struct stack_item *si)
{
  struct stack_item *dead = si;
  si = si->prev;
  xfree (dead->data);
  xfree (dead);
  return si;
}

/* We currently only support passing parameters in integer registers.  This
   conforms with GCC's default model.  Several other variants exist and
   we should probably support some of them based on the selected ABI.  */

static CORE_ADDR
arm_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
		     struct regcache *regcache, CORE_ADDR bp_addr, int nargs,
		     struct value **args, CORE_ADDR sp, int struct_return,
		     CORE_ADDR struct_addr)
{
  int argnum;
  int argreg;
  int nstack;
  struct stack_item *si = NULL;

  /* Set the return address.  For the ARM, the return breakpoint is
     always at BP_ADDR.  */
  /* XXX Fix for Thumb.  */
  regcache_cooked_write_unsigned (regcache, ARM_LR_REGNUM, bp_addr);

  /* Walk through the list of args and determine how large a temporary
     stack is required.  Need to take care here as structs may be
     passed on the stack, and we have to to push them.  */
  nstack = 0;

  argreg = ARM_A1_REGNUM;
  nstack = 0;

  /* Some platforms require a double-word aligned stack.  Make sure sp
     is correctly aligned before we start.  We always do this even if
     it isn't really needed -- it can never hurt things.  */
  sp &= ~(CORE_ADDR)(2 * DEPRECATED_REGISTER_SIZE - 1);

  /* The struct_return pointer occupies the first parameter
     passing register.  */
  if (struct_return)
    {
      if (arm_debug)
	fprintf_unfiltered (gdb_stdlog, "struct return in %s = 0x%s\n",
			    REGISTER_NAME (argreg), paddr (struct_addr));
      regcache_cooked_write_unsigned (regcache, argreg, struct_addr);
      argreg++;
    }

  for (argnum = 0; argnum < nargs; argnum++)
    {
      int len;
      struct type *arg_type;
      struct type *target_type;
      enum type_code typecode;
      char *val;

      arg_type = check_typedef (VALUE_TYPE (args[argnum]));
      len = TYPE_LENGTH (arg_type);
      target_type = TYPE_TARGET_TYPE (arg_type);
      typecode = TYPE_CODE (arg_type);
      val = VALUE_CONTENTS (args[argnum]);

      /* If the argument is a pointer to a function, and it is a
	 Thumb function, create a LOCAL copy of the value and set
	 the THUMB bit in it.  */
      if (TYPE_CODE_PTR == typecode
	  && target_type != NULL
	  && TYPE_CODE_FUNC == TYPE_CODE (target_type))
	{
	  CORE_ADDR regval = extract_unsigned_integer (val, len);
	  if (arm_pc_is_thumb (regval))
	    {
	      val = alloca (len);
	      store_unsigned_integer (val, len, MAKE_THUMB_ADDR (regval));
	    }
	}

      /* Copy the argument to general registers or the stack in
	 register-sized pieces.  Large arguments are split between
	 registers and stack.  */
      while (len > 0)
	{
	  int partial_len = len < DEPRECATED_REGISTER_SIZE ? len : DEPRECATED_REGISTER_SIZE;

	  if (argreg <= ARM_LAST_ARG_REGNUM)
	    {
	      /* The argument is being passed in a general purpose
		 register.  */
	      CORE_ADDR regval = extract_unsigned_integer (val, partial_len);
	      if (arm_debug)
		fprintf_unfiltered (gdb_stdlog, "arg %d in %s = 0x%s\n",
				    argnum, REGISTER_NAME (argreg),
				    phex (regval, DEPRECATED_REGISTER_SIZE));
	      regcache_cooked_write_unsigned (regcache, argreg, regval);
	      argreg++;
	    }
	  else
	    {
	      /* Push the arguments onto the stack.  */
	      if (arm_debug)
		fprintf_unfiltered (gdb_stdlog, "arg %d @ sp + %d\n",
				    argnum, nstack);
	      si = push_stack_item (si, val, DEPRECATED_REGISTER_SIZE);
	      nstack += DEPRECATED_REGISTER_SIZE;
	    }
	      
	  len -= partial_len;
	  val += partial_len;
	}
    }
  /* If we have an odd number of words to push, then decrement the stack
     by one word now, so first stack argument will be dword aligned.  */
  if (nstack & 4)
    sp -= 4;

  while (si)
    {
      sp -= si->len;
      write_memory (sp, si->data, si->len);
      si = pop_stack_item (si);
    }

  /* Finally, update teh SP register.  */
  regcache_cooked_write_unsigned (regcache, ARM_SP_REGNUM, sp);

  return sp;
}

static void
print_fpu_flags (int flags)
{
  if (flags & (1 << 0))
    fputs ("IVO ", stdout);
  if (flags & (1 << 1))
    fputs ("DVZ ", stdout);
  if (flags & (1 << 2))
    fputs ("OFL ", stdout);
  if (flags & (1 << 3))
    fputs ("UFL ", stdout);
  if (flags & (1 << 4))
    fputs ("INX ", stdout);
  putchar ('\n');
}

/* Print interesting information about the floating point processor
   (if present) or emulator.  */
static void
arm_print_float_info (struct gdbarch *gdbarch, struct ui_file *file,
		      struct frame_info *frame, const char *args)
{
  unsigned long status = read_register (ARM_FPS_REGNUM);
  int type;

  type = (status >> 24) & 127;
  printf ("%s FPU type %d\n",
	  (status & (1 << 31)) ? "Hardware" : "Software",
	  type);
  fputs ("mask: ", stdout);
  print_fpu_flags (status >> 16);
  fputs ("flags: ", stdout);
  print_fpu_flags (status);
}

/* Return the GDB type object for the "standard" data type of data in
   register N.  */

static struct type *
arm_register_type (struct gdbarch *gdbarch, int regnum)
{
  if (regnum >= ARM_F0_REGNUM && regnum < ARM_F0_REGNUM + NUM_FREGS)
    {
      if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
	return builtin_type_arm_ext_big;
      else
	return builtin_type_arm_ext_littlebyte_bigword;
    }
  else
    return builtin_type_int32;
}

/* Index within `registers' of the first byte of the space for
   register N.  */

static int
arm_register_byte (int regnum)
{
  if (regnum < ARM_F0_REGNUM)
    return regnum * INT_REGISTER_SIZE;
  else if (regnum < ARM_PS_REGNUM)
    return (NUM_GREGS * INT_REGISTER_SIZE
	    + (regnum - ARM_F0_REGNUM) * FP_REGISTER_SIZE);
  else
    return (NUM_GREGS * INT_REGISTER_SIZE
	    + NUM_FREGS * FP_REGISTER_SIZE
	    + (regnum - ARM_FPS_REGNUM) * STATUS_REGISTER_SIZE);
}

/* Map GDB internal REGNUM onto the Arm simulator register numbers.  */
static int
arm_register_sim_regno (int regnum)
{
  int reg = regnum;
  gdb_assert (reg >= 0 && reg < NUM_REGS);

  if (reg < NUM_GREGS)
    return SIM_ARM_R0_REGNUM + reg;
  reg -= NUM_GREGS;

  if (reg < NUM_FREGS)
    return SIM_ARM_FP0_REGNUM + reg;
  reg -= NUM_FREGS;

  if (reg < NUM_SREGS)
    return SIM_ARM_FPS_REGNUM + reg;
  reg -= NUM_SREGS;

  internal_error (__FILE__, __LINE__, "Bad REGNUM %d", regnum);
}

/* NOTE: cagney/2001-08-20: Both convert_from_extended() and
   convert_to_extended() use floatformat_arm_ext_littlebyte_bigword.
   It is thought that this is is the floating-point register format on
   little-endian systems.  */

static void
convert_from_extended (const struct floatformat *fmt, const void *ptr,
		       void *dbl)
{
  DOUBLEST d;
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    floatformat_to_doublest (&floatformat_arm_ext_big, ptr, &d);
  else
    floatformat_to_doublest (&floatformat_arm_ext_littlebyte_bigword,
			     ptr, &d);
  floatformat_from_doublest (fmt, &d, dbl);
}

static void
convert_to_extended (const struct floatformat *fmt, void *dbl, const void *ptr)
{
  DOUBLEST d;
  floatformat_to_doublest (fmt, ptr, &d);
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    floatformat_from_doublest (&floatformat_arm_ext_big, &d, dbl);
  else
    floatformat_from_doublest (&floatformat_arm_ext_littlebyte_bigword,
			       &d, dbl);
}

static int
condition_true (unsigned long cond, unsigned long status_reg)
{
  if (cond == INST_AL || cond == INST_NV)
    return 1;

  switch (cond)
    {
    case INST_EQ:
      return ((status_reg & FLAG_Z) != 0);
    case INST_NE:
      return ((status_reg & FLAG_Z) == 0);
    case INST_CS:
      return ((status_reg & FLAG_C) != 0);
    case INST_CC:
      return ((status_reg & FLAG_C) == 0);
    case INST_MI:
      return ((status_reg & FLAG_N) != 0);
    case INST_PL:
      return ((status_reg & FLAG_N) == 0);
    case INST_VS:
      return ((status_reg & FLAG_V) != 0);
    case INST_VC:
      return ((status_reg & FLAG_V) == 0);
    case INST_HI:
      return ((status_reg & (FLAG_C | FLAG_Z)) == FLAG_C);
    case INST_LS:
      return ((status_reg & (FLAG_C | FLAG_Z)) != FLAG_C);
    case INST_GE:
      return (((status_reg & FLAG_N) == 0) == ((status_reg & FLAG_V) == 0));
    case INST_LT:
      return (((status_reg & FLAG_N) == 0) != ((status_reg & FLAG_V) == 0));
    case INST_GT:
      return (((status_reg & FLAG_Z) == 0) &&
	      (((status_reg & FLAG_N) == 0) == ((status_reg & FLAG_V) == 0)));
    case INST_LE:
      return (((status_reg & FLAG_Z) != 0) ||
	      (((status_reg & FLAG_N) == 0) != ((status_reg & FLAG_V) == 0)));
    }
  return 1;
}

/* Support routines for single stepping.  Calculate the next PC value.  */
#define submask(x) ((1L << ((x) + 1)) - 1)
#define bit(obj,st) (((obj) >> (st)) & 1)
#define bits(obj,st,fn) (((obj) >> (st)) & submask ((fn) - (st)))
#define sbits(obj,st,fn) \
  ((long) (bits(obj,st,fn) | ((long) bit(obj,fn) * ~ submask (fn - st))))
#define BranchDest(addr,instr) \
  ((CORE_ADDR) (((long) (addr)) + 8 + (sbits (instr, 0, 23) << 2)))
#define ARM_PC_32 1

static unsigned long
shifted_reg_val (unsigned long inst, int carry, unsigned long pc_val,
		 unsigned long status_reg)
{
  unsigned long res, shift;
  int rm = bits (inst, 0, 3);
  unsigned long shifttype = bits (inst, 5, 6);

  if (bit (inst, 4))
    {
      int rs = bits (inst, 8, 11);
      shift = (rs == 15 ? pc_val + 8 : read_register (rs)) & 0xFF;
    }
  else
    shift = bits (inst, 7, 11);

  res = (rm == 15
	 ? ((pc_val | (ARM_PC_32 ? 0 : status_reg))
	    + (bit (inst, 4) ? 12 : 8))
	 : read_register (rm));

  switch (shifttype)
    {
    case 0:			/* LSL */
      res = shift >= 32 ? 0 : res << shift;
      break;

    case 1:			/* LSR */
      res = shift >= 32 ? 0 : res >> shift;
      break;

    case 2:			/* ASR */
      if (shift >= 32)
	shift = 31;
      res = ((res & 0x80000000L)
	     ? ~((~res) >> shift) : res >> shift);
      break;

    case 3:			/* ROR/RRX */
      shift &= 31;
      if (shift == 0)
	res = (res >> 1) | (carry ? 0x80000000L : 0);
      else
	res = (res >> shift) | (res << (32 - shift));
      break;
    }

  return res & 0xffffffff;
}

/* Return number of 1-bits in VAL.  */

static int
bitcount (unsigned long val)
{
  int nbits;
  for (nbits = 0; val != 0; nbits++)
    val &= val - 1;		/* delete rightmost 1-bit in val */
  return nbits;
}

CORE_ADDR
thumb_get_next_pc (CORE_ADDR pc)
{
  unsigned long pc_val = ((unsigned long) pc) + 4;	/* PC after prefetch */
  unsigned short inst1 = read_memory_integer (pc, 2);
  CORE_ADDR nextpc = pc + 2;		/* default is next instruction */
  unsigned long offset;

  if ((inst1 & 0xff00) == 0xbd00)	/* pop {rlist, pc} */
    {
      CORE_ADDR sp;

      /* Fetch the saved PC from the stack.  It's stored above
         all of the other registers.  */
      offset = bitcount (bits (inst1, 0, 7)) * DEPRECATED_REGISTER_SIZE;
      sp = read_register (ARM_SP_REGNUM);
      nextpc = (CORE_ADDR) read_memory_integer (sp + offset, 4);
      nextpc = ADDR_BITS_REMOVE (nextpc);
      if (nextpc == pc)
	error ("Infinite loop detected");
    }
  else if ((inst1 & 0xf000) == 0xd000)	/* conditional branch */
    {
      unsigned long status = read_register (ARM_PS_REGNUM);
      unsigned long cond = bits (inst1, 8, 11);
      if (cond != 0x0f && condition_true (cond, status))    /* 0x0f = SWI */
	nextpc = pc_val + (sbits (inst1, 0, 7) << 1);
    }
  else if ((inst1 & 0xf800) == 0xe000)	/* unconditional branch */
    {
      nextpc = pc_val + (sbits (inst1, 0, 10) << 1);
    }
  else if ((inst1 & 0xf800) == 0xf000)	/* long branch with link, and blx */
    {
      unsigned short inst2 = read_memory_integer (pc + 2, 2);
      offset = (sbits (inst1, 0, 10) << 12) + (bits (inst2, 0, 10) << 1);
      nextpc = pc_val + offset;
      /* For BLX make sure to clear the low bits.  */
      if (bits (inst2, 11, 12) == 1)
	nextpc = nextpc & 0xfffffffc;
    }
  else if ((inst1 & 0xff00) == 0x4700)	/* bx REG, blx REG */
    {
      if (bits (inst1, 3, 6) == 0x0f)
	nextpc = pc_val;
      else
	nextpc = read_register (bits (inst1, 3, 6));

      nextpc = ADDR_BITS_REMOVE (nextpc);
      if (nextpc == pc)
	error ("Infinite loop detected");
    }

  return nextpc;
}

CORE_ADDR
arm_get_next_pc (CORE_ADDR pc)
{
  unsigned long pc_val;
  unsigned long this_instr;
  unsigned long status;
  CORE_ADDR nextpc;

  if (arm_pc_is_thumb (pc))
    return thumb_get_next_pc (pc);

  pc_val = (unsigned long) pc;
  this_instr = read_memory_integer (pc, 4);
  status = read_register (ARM_PS_REGNUM);
  nextpc = (CORE_ADDR) (pc_val + 4);	/* Default case */

  if (condition_true (bits (this_instr, 28, 31), status))
    {
      switch (bits (this_instr, 24, 27))
	{
	case 0x0:
	case 0x1:			/* data processing */
	case 0x2:
	case 0x3:
	  {
	    unsigned long operand1, operand2, result = 0;
	    unsigned long rn;
	    int c;

	    if (bits (this_instr, 12, 15) != 15)
	      break;

	    if (bits (this_instr, 22, 25) == 0
		&& bits (this_instr, 4, 7) == 9)	/* multiply */
	      error ("Illegal update to pc in instruction");

	    /* BX <reg>, BLX <reg> */
	    if (bits (this_instr, 4, 28) == 0x12fff1
		|| bits (this_instr, 4, 28) == 0x12fff3)
	      {
		rn = bits (this_instr, 0, 3);
		result = (rn == 15) ? pc_val + 8 : read_register (rn);
		nextpc = (CORE_ADDR) ADDR_BITS_REMOVE (result);

		if (nextpc == pc)
		  error ("Infinite loop detected");

		return nextpc;
	      }

	    /* Multiply into PC */
	    c = (status & FLAG_C) ? 1 : 0;
	    rn = bits (this_instr, 16, 19);
	    operand1 = (rn == 15) ? pc_val + 8 : read_register (rn);

	    if (bit (this_instr, 25))
	      {
		unsigned long immval = bits (this_instr, 0, 7);
		unsigned long rotate = 2 * bits (this_instr, 8, 11);
		operand2 = ((immval >> rotate) | (immval << (32 - rotate)))
		  & 0xffffffff;
	      }
	    else		/* operand 2 is a shifted register */
	      operand2 = shifted_reg_val (this_instr, c, pc_val, status);

	    switch (bits (this_instr, 21, 24))
	      {
	      case 0x0:	/*and */
		result = operand1 & operand2;
		break;

	      case 0x1:	/*eor */
		result = operand1 ^ operand2;
		break;

	      case 0x2:	/*sub */
		result = operand1 - operand2;
		break;

	      case 0x3:	/*rsb */
		result = operand2 - operand1;
		break;

	      case 0x4:	/*add */
		result = operand1 + operand2;
		break;

	      case 0x5:	/*adc */
		result = operand1 + operand2 + c;
		break;

	      case 0x6:	/*sbc */
		result = operand1 - operand2 + c;
		break;

	      case 0x7:	/*rsc */
		result = operand2 - operand1 + c;
		break;

	      case 0x8:
	      case 0x9:
	      case 0xa:
	      case 0xb:	/* tst, teq, cmp, cmn */
		result = (unsigned long) nextpc;
		break;

	      case 0xc:	/*orr */
		result = operand1 | operand2;
		break;

	      case 0xd:	/*mov */
		/* Always step into a function.  */
		result = operand2;
		break;

	      case 0xe:	/*bic */
		result = operand1 & ~operand2;
		break;

	      case 0xf:	/*mvn */
		result = ~operand2;
		break;
	      }
	    nextpc = (CORE_ADDR) ADDR_BITS_REMOVE (result);

	    if (nextpc == pc)
	      error ("Infinite loop detected");
	    break;
	  }

	case 0x4:
	case 0x5:		/* data transfer */
	case 0x6:
	case 0x7:
	  if (bit (this_instr, 20))
	    {
	      /* load */
	      if (bits (this_instr, 12, 15) == 15)
		{
		  /* rd == pc */
		  unsigned long rn;
		  unsigned long base;

		  if (bit (this_instr, 22))
		    error ("Illegal update to pc in instruction");

		  /* byte write to PC */
		  rn = bits (this_instr, 16, 19);
		  base = (rn == 15) ? pc_val + 8 : read_register (rn);
		  if (bit (this_instr, 24))
		    {
		      /* pre-indexed */
		      int c = (status & FLAG_C) ? 1 : 0;
		      unsigned long offset =
		      (bit (this_instr, 25)
		       ? shifted_reg_val (this_instr, c, pc_val, status)
		       : bits (this_instr, 0, 11));

		      if (bit (this_instr, 23))
			base += offset;
		      else
			base -= offset;
		    }
		  nextpc = (CORE_ADDR) read_memory_integer ((CORE_ADDR) base,
							    4);

		  nextpc = ADDR_BITS_REMOVE (nextpc);

		  if (nextpc == pc)
		    error ("Infinite loop detected");
		}
	    }
	  break;

	case 0x8:
	case 0x9:		/* block transfer */
	  if (bit (this_instr, 20))
	    {
	      /* LDM */
	      if (bit (this_instr, 15))
		{
		  /* loading pc */
		  int offset = 0;

		  if (bit (this_instr, 23))
		    {
		      /* up */
		      unsigned long reglist = bits (this_instr, 0, 14);
		      offset = bitcount (reglist) * 4;
		      if (bit (this_instr, 24))		/* pre */
			offset += 4;
		    }
		  else if (bit (this_instr, 24))
		    offset = -4;

		  {
		    unsigned long rn_val =
		    read_register (bits (this_instr, 16, 19));
		    nextpc =
		      (CORE_ADDR) read_memory_integer ((CORE_ADDR) (rn_val
								  + offset),
						       4);
		  }
		  nextpc = ADDR_BITS_REMOVE (nextpc);
		  if (nextpc == pc)
		    error ("Infinite loop detected");
		}
	    }
	  break;

	case 0xb:		/* branch & link */
	case 0xa:		/* branch */
	  {
	    nextpc = BranchDest (pc, this_instr);

	    /* BLX */
	    if (bits (this_instr, 28, 31) == INST_NV)
	      nextpc |= bit (this_instr, 24) << 1;

	    nextpc = ADDR_BITS_REMOVE (nextpc);
	    if (nextpc == pc)
	      error ("Infinite loop detected");
	    break;
	  }

	case 0xc:
	case 0xd:
	case 0xe:		/* coproc ops */
	case 0xf:		/* SWI */
	  break;

	default:
	  fprintf_filtered (gdb_stderr, "Bad bit-field extraction\n");
	  return (pc);
	}
    }

  return nextpc;
}

/* single_step() is called just before we want to resume the inferior,
   if we want to single-step it but there is no hardware or kernel
   single-step support.  We find the target of the coming instruction
   and breakpoint it.

   single_step() is also called just after the inferior stops.  If we
   had set up a simulated single-step, we undo our damage.  */

static void
arm_software_single_step (enum target_signal sig, int insert_bpt)
{
  static int next_pc;		 /* State between setting and unsetting.  */
  static char break_mem[BREAKPOINT_MAX]; /* Temporary storage for mem@bpt */

  if (insert_bpt)
    {
      next_pc = arm_get_next_pc (read_register (ARM_PC_REGNUM));
      target_insert_breakpoint (next_pc, break_mem);
    }
  else
    target_remove_breakpoint (next_pc, break_mem);
}

#include "bfd-in2.h"
#include "libcoff.h"

static int
gdb_print_insn_arm (bfd_vma memaddr, disassemble_info *info)
{
  if (arm_pc_is_thumb (memaddr))
    {
      static asymbol *asym;
      static combined_entry_type ce;
      static struct coff_symbol_struct csym;
      static struct bfd fake_bfd;
      static bfd_target fake_target;

      if (csym.native == NULL)
	{
	  /* Create a fake symbol vector containing a Thumb symbol.
	     This is solely so that the code in print_insn_little_arm() 
	     and print_insn_big_arm() in opcodes/arm-dis.c will detect
	     the presence of a Thumb symbol and switch to decoding
	     Thumb instructions.  */

	  fake_target.flavour = bfd_target_coff_flavour;
	  fake_bfd.xvec = &fake_target;
	  ce.u.syment.n_sclass = C_THUMBEXTFUNC;
	  csym.native = &ce;
	  csym.symbol.the_bfd = &fake_bfd;
	  csym.symbol.name = "fake";
	  asym = (asymbol *) & csym;
	}

      memaddr = UNMAKE_THUMB_ADDR (memaddr);
      info->symbols = &asym;
    }
  else
    info->symbols = NULL;

  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    return print_insn_big_arm (memaddr, info);
  else
    return print_insn_little_arm (memaddr, info);
}

/* The following define instruction sequences that will cause ARM
   cpu's to take an undefined instruction trap.  These are used to
   signal a breakpoint to GDB.
   
   The newer ARMv4T cpu's are capable of operating in ARM or Thumb
   modes.  A different instruction is required for each mode.  The ARM
   cpu's can also be big or little endian.  Thus four different
   instructions are needed to support all cases.
   
   Note: ARMv4 defines several new instructions that will take the
   undefined instruction trap.  ARM7TDMI is nominally ARMv4T, but does
   not in fact add the new instructions.  The new undefined
   instructions in ARMv4 are all instructions that had no defined
   behaviour in earlier chips.  There is no guarantee that they will
   raise an exception, but may be treated as NOP's.  In practice, it
   may only safe to rely on instructions matching:
   
   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 
   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
   C C C C 0 1 1 x x x x x x x x x x x x x x x x x x x x 1 x x x x
   
   Even this may only true if the condition predicate is true. The
   following use a condition predicate of ALWAYS so it is always TRUE.
   
   There are other ways of forcing a breakpoint.  GNU/Linux, RISC iX,
   and NetBSD all use a software interrupt rather than an undefined
   instruction to force a trap.  This can be handled by by the
   abi-specific code during establishment of the gdbarch vector.  */


/* NOTE rearnsha 2002-02-18: for now we allow a non-multi-arch gdb to
   override these definitions.  */
#ifndef ARM_LE_BREAKPOINT
#define ARM_LE_BREAKPOINT {0xFE,0xDE,0xFF,0xE7}
#endif
#ifndef ARM_BE_BREAKPOINT
#define ARM_BE_BREAKPOINT {0xE7,0xFF,0xDE,0xFE}
#endif
#ifndef THUMB_LE_BREAKPOINT
#define THUMB_LE_BREAKPOINT {0xfe,0xdf}
#endif
#ifndef THUMB_BE_BREAKPOINT
#define THUMB_BE_BREAKPOINT {0xdf,0xfe}
#endif

static const char arm_default_arm_le_breakpoint[] = ARM_LE_BREAKPOINT;
static const char arm_default_arm_be_breakpoint[] = ARM_BE_BREAKPOINT;
static const char arm_default_thumb_le_breakpoint[] = THUMB_LE_BREAKPOINT;
static const char arm_default_thumb_be_breakpoint[] = THUMB_BE_BREAKPOINT;

/* Determine the type and size of breakpoint to insert at PCPTR.  Uses
   the program counter value to determine whether a 16-bit or 32-bit
   breakpoint should be used.  It returns a pointer to a string of
   bytes that encode a breakpoint instruction, stores the length of
   the string to *lenptr, and adjusts the program counter (if
   necessary) to point to the actual memory location where the
   breakpoint should be inserted.  */

/* XXX ??? from old tm-arm.h: if we're using RDP, then we're inserting
   breakpoints and storing their handles instread of what was in
   memory.  It is nice that this is the same size as a handle -
   otherwise remote-rdp will have to change.  */

static const unsigned char *
arm_breakpoint_from_pc (CORE_ADDR *pcptr, int *lenptr)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (arm_pc_is_thumb (*pcptr))
    {
      *pcptr = UNMAKE_THUMB_ADDR (*pcptr);
      *lenptr = tdep->thumb_breakpoint_size;
      return tdep->thumb_breakpoint;
    }
  else
    {
      *lenptr = tdep->arm_breakpoint_size;
      return tdep->arm_breakpoint;
    }
}

/* Extract from an array REGBUF containing the (raw) register state a
   function return value of type TYPE, and copy that, in virtual
   format, into VALBUF.  */

static void
arm_extract_return_value (struct type *type,
			  struct regcache *regs,
			  void *dst)
{
  bfd_byte *valbuf = dst;

  if (TYPE_CODE_FLT == TYPE_CODE (type))
    {
      switch (arm_get_fp_model (current_gdbarch))
	{
	case ARM_FLOAT_FPA:
	  {
	    /* The value is in register F0 in internal format.  We need to
	       extract the raw value and then convert it to the desired
	       internal type.  */
	    bfd_byte tmpbuf[FP_REGISTER_SIZE];

	    regcache_cooked_read (regs, ARM_F0_REGNUM, tmpbuf);
	    convert_from_extended (floatformat_from_type (type), tmpbuf,
				   valbuf);
	  }
	  break;

	case ARM_FLOAT_SOFT_FPA:
	case ARM_FLOAT_SOFT_VFP:
	  regcache_cooked_read (regs, ARM_A1_REGNUM, valbuf);
	  if (TYPE_LENGTH (type) > 4)
	    regcache_cooked_read (regs, ARM_A1_REGNUM + 1,
				  valbuf + INT_REGISTER_SIZE);
	  break;

	default:
	  internal_error
	    (__FILE__, __LINE__,
	     "arm_extract_return_value: Floating point model not supported");
	  break;
	}
    }
  else if (TYPE_CODE (type) == TYPE_CODE_INT
	   || TYPE_CODE (type) == TYPE_CODE_CHAR
	   || TYPE_CODE (type) == TYPE_CODE_BOOL
	   || TYPE_CODE (type) == TYPE_CODE_PTR
	   || TYPE_CODE (type) == TYPE_CODE_REF
	   || TYPE_CODE (type) == TYPE_CODE_ENUM)
    {
      /* If the the type is a plain integer, then the access is
	 straight-forward.  Otherwise we have to play around a bit more.  */
      int len = TYPE_LENGTH (type);
      int regno = ARM_A1_REGNUM;
      ULONGEST tmp;

      while (len > 0)
	{
	  /* By using store_unsigned_integer we avoid having to do
	     anything special for small big-endian values.  */
	  regcache_cooked_read_unsigned (regs, regno++, &tmp);
	  store_unsigned_integer (valbuf, 
				  (len > INT_REGISTER_SIZE
				   ? INT_REGISTER_SIZE : len),
				  tmp);
	  len -= INT_REGISTER_SIZE;
	  valbuf += INT_REGISTER_SIZE;
	}
    }
  else
    {
      /* For a structure or union the behaviour is as if the value had
         been stored to word-aligned memory and then loaded into 
         registers with 32-bit load instruction(s).  */
      int len = TYPE_LENGTH (type);
      int regno = ARM_A1_REGNUM;
      bfd_byte tmpbuf[INT_REGISTER_SIZE];

      while (len > 0)
	{
	  regcache_cooked_read (regs, regno++, tmpbuf);
	  memcpy (valbuf, tmpbuf,
		  len > INT_REGISTER_SIZE ? INT_REGISTER_SIZE : len);
	  len -= INT_REGISTER_SIZE;
	  valbuf += INT_REGISTER_SIZE;
	}
    }
}

/* Extract from an array REGBUF containing the (raw) register state
   the address in which a function should return its structure value.  */

static CORE_ADDR
arm_extract_struct_value_address (struct regcache *regcache)
{
  ULONGEST ret;

  regcache_cooked_read_unsigned (regcache, ARM_A1_REGNUM, &ret);
  return ret;
}

/* Will a function return an aggregate type in memory or in a
   register?  Return 0 if an aggregate type can be returned in a
   register, 1 if it must be returned in memory.  */

static int
arm_use_struct_convention (int gcc_p, struct type *type)
{
  int nRc;
  enum type_code code;

  CHECK_TYPEDEF (type);

  /* In the ARM ABI, "integer" like aggregate types are returned in
     registers.  For an aggregate type to be integer like, its size
     must be less than or equal to DEPRECATED_REGISTER_SIZE and the
     offset of each addressable subfield must be zero.  Note that bit
     fields are not addressable, and all addressable subfields of
     unions always start at offset zero.

     This function is based on the behaviour of GCC 2.95.1.
     See: gcc/arm.c: arm_return_in_memory() for details.

     Note: All versions of GCC before GCC 2.95.2 do not set up the
     parameters correctly for a function returning the following
     structure: struct { float f;}; This should be returned in memory,
     not a register.  Richard Earnshaw sent me a patch, but I do not
     know of any way to detect if a function like the above has been
     compiled with the correct calling convention.  */

  /* All aggregate types that won't fit in a register must be returned
     in memory.  */
  if (TYPE_LENGTH (type) > DEPRECATED_REGISTER_SIZE)
    {
      return 1;
    }

  /* The only aggregate types that can be returned in a register are
     structs and unions.  Arrays must be returned in memory.  */
  code = TYPE_CODE (type);
  if ((TYPE_CODE_STRUCT != code) && (TYPE_CODE_UNION != code))
    {
      return 1;
    }

  /* Assume all other aggregate types can be returned in a register.
     Run a check for structures, unions and arrays.  */
  nRc = 0;

  if ((TYPE_CODE_STRUCT == code) || (TYPE_CODE_UNION == code))
    {
      int i;
      /* Need to check if this struct/union is "integer" like.  For
         this to be true, its size must be less than or equal to
         DEPRECATED_REGISTER_SIZE and the offset of each addressable
         subfield must be zero.  Note that bit fields are not
         addressable, and unions always start at offset zero.  If any
         of the subfields is a floating point type, the struct/union
         cannot be an integer type.  */

      /* For each field in the object, check:
         1) Is it FP? --> yes, nRc = 1;
         2) Is it addressable (bitpos != 0) and
         not packed (bitsize == 0)?
         --> yes, nRc = 1  
       */

      for (i = 0; i < TYPE_NFIELDS (type); i++)
	{
	  enum type_code field_type_code;
	  field_type_code = TYPE_CODE (check_typedef (TYPE_FIELD_TYPE (type, i)));

	  /* Is it a floating point type field?  */
	  if (field_type_code == TYPE_CODE_FLT)
	    {
	      nRc = 1;
	      break;
	    }

	  /* If bitpos != 0, then we have to care about it.  */
	  if (TYPE_FIELD_BITPOS (type, i) != 0)
	    {
	      /* Bitfields are not addressable.  If the field bitsize is 
	         zero, then the field is not packed.  Hence it cannot be
	         a bitfield or any other packed type.  */
	      if (TYPE_FIELD_BITSIZE (type, i) == 0)
		{
		  nRc = 1;
		  break;
		}
	    }
	}
    }

  return nRc;
}

/* Write into appropriate registers a function return value of type
   TYPE, given in virtual format.  */

static void
arm_store_return_value (struct type *type, struct regcache *regs,
			const void *src)
{
  const bfd_byte *valbuf = src;

  if (TYPE_CODE (type) == TYPE_CODE_FLT)
    {
      char buf[MAX_REGISTER_SIZE];

      switch (arm_get_fp_model (current_gdbarch))
	{
	case ARM_FLOAT_FPA:

	  convert_to_extended (floatformat_from_type (type), buf, valbuf);
	  regcache_cooked_write (regs, ARM_F0_REGNUM, buf);
	  break;

	case ARM_FLOAT_SOFT_FPA:
	case ARM_FLOAT_SOFT_VFP:
	  regcache_cooked_write (regs, ARM_A1_REGNUM, valbuf);
	  if (TYPE_LENGTH (type) > 4)
	    regcache_cooked_write (regs, ARM_A1_REGNUM + 1, 
				   valbuf + INT_REGISTER_SIZE);
	  break;

	default:
	  internal_error
	    (__FILE__, __LINE__,
	     "arm_store_return_value: Floating point model not supported");
	  break;
	}
    }
  else if (TYPE_CODE (type) == TYPE_CODE_INT
	   || TYPE_CODE (type) == TYPE_CODE_CHAR
	   || TYPE_CODE (type) == TYPE_CODE_BOOL
	   || TYPE_CODE (type) == TYPE_CODE_PTR
	   || TYPE_CODE (type) == TYPE_CODE_REF
	   || TYPE_CODE (type) == TYPE_CODE_ENUM)
    {
      if (TYPE_LENGTH (type) <= 4)
	{
	  /* Values of one word or less are zero/sign-extended and
	     returned in r0.  */
	  bfd_byte tmpbuf[INT_REGISTER_SIZE];
	  LONGEST val = unpack_long (type, valbuf);

	  store_signed_integer (tmpbuf, INT_REGISTER_SIZE, val);
	  regcache_cooked_write (regs, ARM_A1_REGNUM, tmpbuf);
	}
      else
	{
	  /* Integral values greater than one word are stored in consecutive
	     registers starting with r0.  This will always be a multiple of
	     the regiser size.  */
	  int len = TYPE_LENGTH (type);
	  int regno = ARM_A1_REGNUM;

	  while (len > 0)
	    {
	      regcache_cooked_write (regs, regno++, valbuf);
	      len -= INT_REGISTER_SIZE;
	      valbuf += INT_REGISTER_SIZE;
	    }
	}
    }
  else
    {
      /* For a structure or union the behaviour is as if the value had
         been stored to word-aligned memory and then loaded into 
         registers with 32-bit load instruction(s).  */
      int len = TYPE_LENGTH (type);
      int regno = ARM_A1_REGNUM;
      bfd_byte tmpbuf[INT_REGISTER_SIZE];

      while (len > 0)
	{
	  memcpy (tmpbuf, valbuf,
		  len > INT_REGISTER_SIZE ? INT_REGISTER_SIZE : len);
	  regcache_cooked_write (regs, regno++, tmpbuf);
	  len -= INT_REGISTER_SIZE;
	  valbuf += INT_REGISTER_SIZE;
	}
    }
}

static int
arm_get_longjmp_target (CORE_ADDR *pc)
{
  CORE_ADDR jb_addr;
  char buf[INT_REGISTER_SIZE];
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  
  jb_addr = read_register (ARM_A1_REGNUM);

  if (target_read_memory (jb_addr + tdep->jb_pc * tdep->jb_elt_size, buf,
			  INT_REGISTER_SIZE))
    return 0;

  *pc = extract_unsigned_integer (buf, INT_REGISTER_SIZE);
  return 1;
}

/* Return non-zero if the PC is inside a thumb call thunk.  */

int
arm_in_call_stub (CORE_ADDR pc, char *name)
{
  CORE_ADDR start_addr;

  /* Find the starting address of the function containing the PC.  If
     the caller didn't give us a name, look it up at the same time.  */
  if (0 == find_pc_partial_function (pc, name ? NULL : &name, 
				     &start_addr, NULL))
    return 0;

  return strncmp (name, "_call_via_r", 11) == 0;
}

/* If PC is in a Thumb call or return stub, return the address of the
   target PC, which is in a register.  The thunk functions are called
   _called_via_xx, where x is the register name.  The possible names
   are r0-r9, sl, fp, ip, sp, and lr.  */

CORE_ADDR
arm_skip_stub (CORE_ADDR pc)
{
  char *name;
  CORE_ADDR start_addr;

  /* Find the starting address and name of the function containing the PC.  */
  if (find_pc_partial_function (pc, &name, &start_addr, NULL) == 0)
    return 0;

  /* Call thunks always start with "_call_via_".  */
  if (strncmp (name, "_call_via_", 10) == 0)
    {
      /* Use the name suffix to determine which register contains the
         target PC.  */
      static char *table[15] =
      {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
       "r8", "r9", "sl", "fp", "ip", "sp", "lr"
      };
      int regno;

      for (regno = 0; regno <= 14; regno++)
	if (strcmp (&name[10], table[regno]) == 0)
	  return read_register (regno);
    }

  return 0;			/* not a stub */
}

static void
set_arm_command (char *args, int from_tty)
{
  printf_unfiltered ("\"set arm\" must be followed by an apporpriate subcommand.\n");
  help_list (setarmcmdlist, "set arm ", all_commands, gdb_stdout);
}

static void
show_arm_command (char *args, int from_tty)
{
  cmd_show_list (showarmcmdlist, from_tty, "");
}

enum arm_float_model
arm_get_fp_model (struct gdbarch *gdbarch)
{
  if (arm_fp_model == ARM_FLOAT_AUTO)
    return gdbarch_tdep (gdbarch)->fp_model;

  return arm_fp_model;
}

static void
arm_set_fp (struct gdbarch *gdbarch)
{
  enum arm_float_model fp_model = arm_get_fp_model (gdbarch);

  if (gdbarch_byte_order (gdbarch) == BFD_ENDIAN_LITTLE 
      && (fp_model == ARM_FLOAT_SOFT_FPA || fp_model == ARM_FLOAT_FPA))
    {
      set_gdbarch_double_format	(gdbarch,
				 &floatformat_ieee_double_littlebyte_bigword);
      set_gdbarch_long_double_format
	(gdbarch, &floatformat_ieee_double_littlebyte_bigword);
    }
  else
    {
      set_gdbarch_double_format (gdbarch, &floatformat_ieee_double_little);
      set_gdbarch_long_double_format (gdbarch,
				      &floatformat_ieee_double_little);
    }
}

static void
set_fp_model_sfunc (char *args, int from_tty,
		    struct cmd_list_element *c)
{
  enum arm_float_model fp_model;

  for (fp_model = ARM_FLOAT_AUTO; fp_model != ARM_FLOAT_LAST; fp_model++)
    if (strcmp (current_fp_model, fp_model_strings[fp_model]) == 0)
      {
	arm_fp_model = fp_model;
	break;
      }

  if (fp_model == ARM_FLOAT_LAST)
    internal_error (__FILE__, __LINE__, "Invalid fp model accepted: %s.",
		    current_fp_model);

  if (gdbarch_bfd_arch_info (current_gdbarch)->arch == bfd_arch_arm)
    arm_set_fp (current_gdbarch);
}

static void
show_fp_model (char *args, int from_tty,
	       struct cmd_list_element *c)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (arm_fp_model == ARM_FLOAT_AUTO 
      && gdbarch_bfd_arch_info (current_gdbarch)->arch == bfd_arch_arm)
    printf_filtered ("  - the default for the current ABI is \"%s\".\n",
		     fp_model_strings[tdep->fp_model]);
}

/* If the user changes the register disassembly style used for info
   register and other commands, we have to also switch the style used
   in opcodes for disassembly output.  This function is run in the "set
   arm disassembly" command, and does that.  */

static void
set_disassembly_style_sfunc (char *args, int from_tty,
			      struct cmd_list_element *c)
{
  set_disassembly_style ();
}

/* Return the ARM register name corresponding to register I.  */
static const char *
arm_register_name (int i)
{
  return arm_register_names[i];
}

static void
set_disassembly_style (void)
{
  const char *setname, *setdesc, **regnames;
  int numregs, j;

  /* Find the style that the user wants in the opcodes table.  */
  int current = 0;
  numregs = get_arm_regnames (current, &setname, &setdesc, &regnames);
  while ((disassembly_style != setname)
	 && (current < num_disassembly_options))
    get_arm_regnames (++current, &setname, &setdesc, &regnames);
  current_option = current;

  /* Fill our copy.  */
  for (j = 0; j < numregs; j++)
    arm_register_names[j] = (char *) regnames[j];

  /* Adjust case.  */
  if (isupper (*regnames[ARM_PC_REGNUM]))
    {
      arm_register_names[ARM_FPS_REGNUM] = "FPS";
      arm_register_names[ARM_PS_REGNUM] = "CPSR";
    }
  else
    {
      arm_register_names[ARM_FPS_REGNUM] = "fps";
      arm_register_names[ARM_PS_REGNUM] = "cpsr";
    }

  /* Synchronize the disassembler.  */
  set_arm_regname_option (current);
}

/* arm_othernames implements the "othernames" command.  This is deprecated
   by the "set arm disassembly" command.  */

static void
arm_othernames (char *names, int n)
{
  /* Circle through the various flavors.  */
  current_option = (current_option + 1) % num_disassembly_options;

  disassembly_style = valid_disassembly_styles[current_option];
  set_disassembly_style ();
}

/* Test whether the coff symbol specific value corresponds to a Thumb
   function.  */

static int
coff_sym_is_thumb (int val)
{
  return (val == C_THUMBEXT ||
	  val == C_THUMBSTAT ||
	  val == C_THUMBEXTFUNC ||
	  val == C_THUMBSTATFUNC ||
	  val == C_THUMBLABEL);
}

/* arm_coff_make_msymbol_special()
   arm_elf_make_msymbol_special()
   
   These functions test whether the COFF or ELF symbol corresponds to
   an address in thumb code, and set a "special" bit in a minimal
   symbol to indicate that it does.  */
   
static void
arm_elf_make_msymbol_special(asymbol *sym, struct minimal_symbol *msym)
{
  /* Thumb symbols are of type STT_LOPROC, (synonymous with
     STT_ARM_TFUNC).  */
  if (ELF_ST_TYPE (((elf_symbol_type *)sym)->internal_elf_sym.st_info)
      == STT_LOPROC)
    MSYMBOL_SET_SPECIAL (msym);
}

static void
arm_coff_make_msymbol_special(int val, struct minimal_symbol *msym)
{
  if (coff_sym_is_thumb (val))
    MSYMBOL_SET_SPECIAL (msym);
}

static void
arm_write_pc (CORE_ADDR pc, ptid_t ptid)
{
  write_register_pid (ARM_PC_REGNUM, pc, ptid);

  /* If necessary, set the T bit.  */
  if (arm_apcs_32)
    {
      CORE_ADDR val = read_register_pid (ARM_PS_REGNUM, ptid);
      if (arm_pc_is_thumb (pc))
	write_register_pid (ARM_PS_REGNUM, val | 0x20, ptid);
      else
	write_register_pid (ARM_PS_REGNUM, val & ~(CORE_ADDR) 0x20, ptid);
    }
}

static enum gdb_osabi
arm_elf_osabi_sniffer (bfd *abfd)
{
  unsigned int elfosabi, eflags;
  enum gdb_osabi osabi = GDB_OSABI_UNKNOWN;

  elfosabi = elf_elfheader (abfd)->e_ident[EI_OSABI];

  switch (elfosabi)
    {
    case ELFOSABI_NONE:  
      /* When elfosabi is ELFOSABI_NONE (0), then the ELF structures in the
	 file are conforming to the base specification for that machine 
	 (there are no OS-specific extensions).  In order to determine the 
	 real OS in use we must look for OS notes that have been added.  */
      bfd_map_over_sections (abfd,
			     generic_elf_osabi_sniff_abi_tag_sections,  
			     &osabi);
      if (osabi == GDB_OSABI_UNKNOWN)
	{
	  /* Existing ARM tools don't set this field, so look at the EI_FLAGS
	     field for more information.  */
	  eflags = EF_ARM_EABI_VERSION(elf_elfheader(abfd)->e_flags);
	  switch (eflags)
	    {
	    case EF_ARM_EABI_VER1:
	      osabi = GDB_OSABI_ARM_EABI_V1;
	      break;

	    case EF_ARM_EABI_VER2:
	      osabi = GDB_OSABI_ARM_EABI_V2;
	      break;

	    case EF_ARM_EABI_VER3:
	    case EF_ARM_EABI_VER4:
	    case EF_ARM_EABI_VER5:
	      /*
	       * GDB does not support these EABI versions. Fallback
	       * to the highest known to make the KGDB working with
	       * kernel ELF image.
	       */
	      osabi = GDB_OSABI_ARM_EABI_V2;
	      printf ("\n%s:%d "
	              "arm_elf_osabi_sniffer: Unsupported ARM EABI "
	              "version 0x%x, falling back to 0x%x\n",
	              __FILE__, __LINE__, eflags, EF_ARM_EABI_VER2);
	      break;

	    case EF_ARM_EABI_UNKNOWN:
	      /* Assume GNU tools.  */
	      osabi = GDB_OSABI_ARM_APCS;
	      break;

	    default:
	      internal_error (__FILE__, __LINE__,
			      "arm_elf_osabi_sniffer: Unknown ARM EABI "
			      "version 0x%x", eflags);
	    }
	}
      break;

    case ELFOSABI_ARM:
      /* GNU tools use this value.  Check note sections in this case,
	 as well.  */
      bfd_map_over_sections (abfd,
			     generic_elf_osabi_sniff_abi_tag_sections, 
			     &osabi);
      if (osabi == GDB_OSABI_UNKNOWN)
	{
	  /* Assume APCS ABI.  */
	  osabi = GDB_OSABI_ARM_APCS;
	}
      break;

    case ELFOSABI_FREEBSD:
      osabi = GDB_OSABI_FREEBSD_ELF;
      break;

    case ELFOSABI_NETBSD:
      osabi = GDB_OSABI_NETBSD_ELF;
      break;

    case ELFOSABI_LINUX:
      osabi = GDB_OSABI_LINUX;
      break;
    }

  return osabi;
}


/* Initialize the current architecture based on INFO.  If possible,
   re-use an architecture from ARCHES, which is a list of
   architectures already created during this debugging session.

   Called e.g. at program startup, when reading a core file, and when
   reading a binary file.  */

static struct gdbarch *
arm_gdbarch_init (struct gdbarch_info info, struct gdbarch_list *arches)
{
  struct gdbarch_tdep *tdep;
  struct gdbarch *gdbarch;

  /* Try to deterimine the ABI of the object we are loading.  */

  if (info.abfd != NULL && info.osabi == GDB_OSABI_UNKNOWN)
    {
      switch (bfd_get_flavour (info.abfd))
	{
	case bfd_target_aout_flavour:
	  /* Assume it's an old APCS-style ABI.  */
	  info.osabi = GDB_OSABI_ARM_APCS;
	  break;

	case bfd_target_coff_flavour:
	  /* Assume it's an old APCS-style ABI.  */
	  /* XXX WinCE?  */
	  info.osabi = GDB_OSABI_ARM_APCS;
	  break;

	default:
	  /* Leave it as "unknown".  */
	  break;
	}
    }

  /* If there is already a candidate, use it.  */
  arches = gdbarch_list_lookup_by_info (arches, &info);
  if (arches != NULL)
    return arches->gdbarch;

  tdep = xmalloc (sizeof (struct gdbarch_tdep));
  gdbarch = gdbarch_alloc (&info, tdep);

  /* We used to default to FPA for generic ARM, but almost nobody uses that
     now, and we now provide a way for the user to force the model.  So 
     default to the most useful variant.  */
  tdep->fp_model = ARM_FLOAT_SOFT_FPA;

  /* Breakpoints.  */
  switch (info.byte_order)
    {
    case BFD_ENDIAN_BIG:
      tdep->arm_breakpoint = arm_default_arm_be_breakpoint;
      tdep->arm_breakpoint_size = sizeof (arm_default_arm_be_breakpoint);
      tdep->thumb_breakpoint = arm_default_thumb_be_breakpoint;
      tdep->thumb_breakpoint_size = sizeof (arm_default_thumb_be_breakpoint);

      break;

    case BFD_ENDIAN_LITTLE:
      tdep->arm_breakpoint = arm_default_arm_le_breakpoint;
      tdep->arm_breakpoint_size = sizeof (arm_default_arm_le_breakpoint);
      tdep->thumb_breakpoint = arm_default_thumb_le_breakpoint;
      tdep->thumb_breakpoint_size = sizeof (arm_default_thumb_le_breakpoint);

      break;

    default:
      internal_error (__FILE__, __LINE__,
		      "arm_gdbarch_init: bad byte order for float format");
    }

  /* On ARM targets char defaults to unsigned.  */
  set_gdbarch_char_signed (gdbarch, 0);

  /* This should be low enough for everything.  */
  tdep->lowest_pc = 0x20;
  tdep->jb_pc = -1;	/* Longjump support not enabled by default.  */

  set_gdbarch_deprecated_call_dummy_words (gdbarch, arm_call_dummy_words);
  set_gdbarch_deprecated_sizeof_call_dummy_words (gdbarch, 0);

  set_gdbarch_push_dummy_call (gdbarch, arm_push_dummy_call);

  set_gdbarch_write_pc (gdbarch, arm_write_pc);

  /* Frame handling.  */
  set_gdbarch_unwind_dummy_id (gdbarch, arm_unwind_dummy_id);
  set_gdbarch_unwind_pc (gdbarch, arm_unwind_pc);
  set_gdbarch_unwind_sp (gdbarch, arm_unwind_sp);

  set_gdbarch_deprecated_frameless_function_invocation (gdbarch, arm_frameless_function_invocation);

  frame_base_set_default (gdbarch, &arm_normal_base);

  /* Address manipulation.  */
  set_gdbarch_smash_text_address (gdbarch, arm_smash_text_address);
  set_gdbarch_addr_bits_remove (gdbarch, arm_addr_bits_remove);

  /* Advance PC across function entry code.  */
  set_gdbarch_skip_prologue (gdbarch, arm_skip_prologue);

  /* Get the PC when a frame might not be available.  */
  set_gdbarch_deprecated_saved_pc_after_call (gdbarch, arm_saved_pc_after_call);

  /* The stack grows downward.  */
  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);

  /* Breakpoint manipulation.  */
  set_gdbarch_breakpoint_from_pc (gdbarch, arm_breakpoint_from_pc);

  /* Information about registers, etc.  */
  set_gdbarch_print_float_info (gdbarch, arm_print_float_info);
  set_gdbarch_deprecated_fp_regnum (gdbarch, ARM_FP_REGNUM);	/* ??? */
  set_gdbarch_sp_regnum (gdbarch, ARM_SP_REGNUM);
  set_gdbarch_pc_regnum (gdbarch, ARM_PC_REGNUM);
  set_gdbarch_deprecated_register_byte (gdbarch, arm_register_byte);
  set_gdbarch_deprecated_register_bytes (gdbarch,
					 (NUM_GREGS * INT_REGISTER_SIZE
					  + NUM_FREGS * FP_REGISTER_SIZE
					  + NUM_SREGS * STATUS_REGISTER_SIZE));
  set_gdbarch_num_regs (gdbarch, NUM_GREGS + NUM_FREGS + NUM_SREGS);
  set_gdbarch_register_type (gdbarch, arm_register_type);

  /* Internal <-> external register number maps.  */
  set_gdbarch_register_sim_regno (gdbarch, arm_register_sim_regno);

  /* Integer registers are 4 bytes.  */
  set_gdbarch_deprecated_register_size (gdbarch, 4);
  set_gdbarch_register_name (gdbarch, arm_register_name);

  /* Returning results.  */
  set_gdbarch_extract_return_value (gdbarch, arm_extract_return_value);
  set_gdbarch_store_return_value (gdbarch, arm_store_return_value);
  set_gdbarch_use_struct_convention (gdbarch, arm_use_struct_convention);
  set_gdbarch_deprecated_extract_struct_value_address (gdbarch, arm_extract_struct_value_address);

  /* Single stepping.  */
  /* XXX For an RDI target we should ask the target if it can single-step.  */
  set_gdbarch_software_single_step (gdbarch, arm_software_single_step);

  /* Disassembly.  */
  set_gdbarch_print_insn (gdbarch, gdb_print_insn_arm);

  /* Minsymbol frobbing.  */
  set_gdbarch_elf_make_msymbol_special (gdbarch, arm_elf_make_msymbol_special);
  set_gdbarch_coff_make_msymbol_special (gdbarch,
					 arm_coff_make_msymbol_special);

  /* Hook in the ABI-specific overrides, if they have been registered.  */
  gdbarch_init_osabi (info, gdbarch);

  /* Add some default predicates.  */
  frame_unwind_append_sniffer (gdbarch, arm_sigtramp_unwind_sniffer);
  frame_unwind_append_sniffer (gdbarch, arm_prologue_unwind_sniffer);

  /* Now we have tuned the configuration, set a few final things,
     based on what the OS ABI has told us.  */

  if (tdep->jb_pc >= 0)
    set_gdbarch_get_longjmp_target (gdbarch, arm_get_longjmp_target);

  /* Floating point sizes and format.  */
  switch (info.byte_order)
    {
    case BFD_ENDIAN_BIG:
      set_gdbarch_float_format (gdbarch, &floatformat_ieee_single_big);
      set_gdbarch_double_format (gdbarch, &floatformat_ieee_double_big);
      set_gdbarch_long_double_format (gdbarch, &floatformat_ieee_double_big);
      
      break;

    case BFD_ENDIAN_LITTLE:
      set_gdbarch_float_format (gdbarch, &floatformat_ieee_single_little);
      arm_set_fp (gdbarch);
      break;

    default:
      internal_error (__FILE__, __LINE__,
		      "arm_gdbarch_init: bad byte order for float format");
    }

  return gdbarch;
}

static void
arm_dump_tdep (struct gdbarch *current_gdbarch, struct ui_file *file)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (tdep == NULL)
    return;

  fprintf_unfiltered (file, "arm_dump_tdep: Lowest pc = 0x%lx",
		      (unsigned long) tdep->lowest_pc);
}

static void
arm_init_abi_eabi_v1 (struct gdbarch_info info,
		      struct gdbarch *gdbarch)
{
  /* Place-holder.  */
}

static void
arm_init_abi_eabi_v2 (struct gdbarch_info info,
		      struct gdbarch *gdbarch)
{
  /* Place-holder.  */
}

static void
arm_init_abi_apcs (struct gdbarch_info info,
		   struct gdbarch *gdbarch)
{
  /* Place-holder.  */
}

extern initialize_file_ftype _initialize_arm_tdep; /* -Wmissing-prototypes */

void
_initialize_arm_tdep (void)
{
  struct ui_file *stb;
  long length;
  struct cmd_list_element *new_set, *new_show;
  const char *setname;
  const char *setdesc;
  const char **regnames;
  int numregs, i, j;
  static char *helptext;

  gdbarch_register (bfd_arch_arm, arm_gdbarch_init, arm_dump_tdep);

  /* Register an ELF OS ABI sniffer for ARM binaries.  */
  gdbarch_register_osabi_sniffer (bfd_arch_arm,
				  bfd_target_elf_flavour,
				  arm_elf_osabi_sniffer);

  /* Register some ABI variants for embedded systems.  */
  gdbarch_register_osabi (bfd_arch_arm, 0, GDB_OSABI_ARM_EABI_V1,
                          arm_init_abi_eabi_v1);
  gdbarch_register_osabi (bfd_arch_arm, 0, GDB_OSABI_ARM_EABI_V2,
                          arm_init_abi_eabi_v2);
  gdbarch_register_osabi (bfd_arch_arm, 0, GDB_OSABI_ARM_APCS,
                          arm_init_abi_apcs);

  /* Get the number of possible sets of register names defined in opcodes.  */
  num_disassembly_options = get_arm_regname_num_options ();

  /* Add root prefix command for all "set arm"/"show arm" commands.  */
  add_prefix_cmd ("arm", no_class, set_arm_command,
		  "Various ARM-specific commands.",
		  &setarmcmdlist, "set arm ", 0, &setlist);

  add_prefix_cmd ("arm", no_class, show_arm_command,
		  "Various ARM-specific commands.",
		  &showarmcmdlist, "show arm ", 0, &showlist);

  /* Sync the opcode insn printer with our register viewer.  */
  parse_arm_disassembler_option ("reg-names-std");

  /* Begin creating the help text.  */
  stb = mem_fileopen ();
  fprintf_unfiltered (stb, "Set the disassembly style.\n"
		      "The valid values are:\n");

  /* Initialize the array that will be passed to add_set_enum_cmd().  */
  valid_disassembly_styles
    = xmalloc ((num_disassembly_options + 1) * sizeof (char *));
  for (i = 0; i < num_disassembly_options; i++)
    {
      numregs = get_arm_regnames (i, &setname, &setdesc, &regnames);
      valid_disassembly_styles[i] = setname;
      fprintf_unfiltered (stb, "%s - %s\n", setname,
			  setdesc);
      /* Copy the default names (if found) and synchronize disassembler.  */
      if (!strcmp (setname, "std"))
	{
          disassembly_style = setname;
          current_option = i;
	  for (j = 0; j < numregs; j++)
            arm_register_names[j] = (char *) regnames[j];
          set_arm_regname_option (i);
	}
    }
  /* Mark the end of valid options.  */
  valid_disassembly_styles[num_disassembly_options] = NULL;

  /* Finish the creation of the help text.  */
  fprintf_unfiltered (stb, "The default is \"std\".");
  helptext = ui_file_xstrdup (stb, &length);
  ui_file_delete (stb);

  /* Add the deprecated disassembly-flavor command.  */
  new_set = add_set_enum_cmd ("disassembly-flavor", no_class,
			      valid_disassembly_styles,
			      &disassembly_style,
			      helptext,
			      &setlist);
  set_cmd_sfunc (new_set, set_disassembly_style_sfunc);
  deprecate_cmd (new_set, "set arm disassembly");
  deprecate_cmd (add_show_from_set (new_set, &showlist),
		 "show arm disassembly");

  /* And now add the new interface.  */
  new_set = add_set_enum_cmd ("disassembler", no_class,
			      valid_disassembly_styles, &disassembly_style,
			      helptext, &setarmcmdlist);

  set_cmd_sfunc (new_set, set_disassembly_style_sfunc);
  add_show_from_set (new_set, &showarmcmdlist);

  add_setshow_cmd_full ("apcs32", no_class,
			var_boolean, (char *) &arm_apcs_32,
			"Set usage of ARM 32-bit mode.",
			"Show usage of ARM 32-bit mode.",
			NULL, NULL,
			&setlist, &showlist, &new_set, &new_show);
  deprecate_cmd (new_set, "set arm apcs32");
  deprecate_cmd (new_show, "show arm apcs32");

  add_setshow_boolean_cmd ("apcs32", no_class, &arm_apcs_32,
			   "Set usage of ARM 32-bit mode.  "
			   "When off, a 26-bit PC will be used.",
			   "Show usage of ARM 32-bit mode.  "
			   "When off, a 26-bit PC will be used.",
			   NULL, NULL,
			   &setarmcmdlist, &showarmcmdlist);

  /* Add a command to allow the user to force the FPU model.  */
  new_set = add_set_enum_cmd
    ("fpu", no_class, fp_model_strings, &current_fp_model,
     "Set the floating point type.\n"
     "auto - Determine the FP typefrom the OS-ABI.\n"
     "softfpa - Software FP, mixed-endian doubles on little-endian ARMs.\n"
     "fpa - FPA co-processor (GCC compiled).\n"
     "softvfp - Software FP with pure-endian doubles.\n"
     "vfp - VFP co-processor.",
     &setarmcmdlist);
  set_cmd_sfunc (new_set, set_fp_model_sfunc);
  set_cmd_sfunc (add_show_from_set (new_set, &showarmcmdlist), show_fp_model);

  /* Add the deprecated "othernames" command.  */
  deprecate_cmd (add_com ("othernames", class_obscure, arm_othernames,
			  "Switch to the next set of register names."),
		 "set arm disassembly");

  /* Debugging flag.  */
  add_setshow_boolean_cmd ("arm", class_maintenance, &arm_debug,
			   "Set ARM debugging.  "
			   "When on, arm-specific debugging is enabled.",
			   "Show ARM debugging.  "
			   "When on, arm-specific debugging is enabled.",
			   NULL, NULL,
			   &setdebuglist, &showdebuglist);
}
