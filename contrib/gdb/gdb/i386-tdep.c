/* Intel 386 target-dependent stuff.

   Copyright 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
   1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free Software
   Foundation, Inc.

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
#include "arch-utils.h"
#include "command.h"
#include "dummy-frame.h"
#include "dwarf2-frame.h"
#include "doublest.h"
#include "floatformat.h"
#include "frame.h"
#include "frame-base.h"
#include "frame-unwind.h"
#include "inferior.h"
#include "gdbcmd.h"
#include "gdbcore.h"
#include "objfiles.h"
#include "osabi.h"
#include "regcache.h"
#include "reggroups.h"
#include "regset.h"
#include "symfile.h"
#include "symtab.h"
#include "target.h"
#include "value.h"
#include "dis-asm.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "i386-tdep.h"
#include "i387-tdep.h"

/* Names of the registers.  The first 10 registers match the register
   numbering scheme used by GCC for stabs and DWARF.  */

static char *i386_register_names[] =
{
  "eax",   "ecx",    "edx",   "ebx",
  "esp",   "ebp",    "esi",   "edi",
  "eip",   "eflags", "cs",    "ss",
  "ds",    "es",     "fs",    "gs",
  "st0",   "st1",    "st2",   "st3",
  "st4",   "st5",    "st6",   "st7",
  "fctrl", "fstat",  "ftag",  "fiseg",
  "fioff", "foseg",  "fooff", "fop",
  "xmm0",  "xmm1",   "xmm2",  "xmm3",
  "xmm4",  "xmm5",   "xmm6",  "xmm7",
  "mxcsr"
};

static const int i386_num_register_names = ARRAY_SIZE (i386_register_names);

/* MMX registers.  */

static char *i386_mmx_names[] =
{
  "mm0", "mm1", "mm2", "mm3",
  "mm4", "mm5", "mm6", "mm7"
};

static const int i386_num_mmx_regs = ARRAY_SIZE (i386_mmx_names);

static int
i386_mmx_regnum_p (struct gdbarch *gdbarch, int regnum)
{
  int mm0_regnum = gdbarch_tdep (gdbarch)->mm0_regnum;

  if (mm0_regnum < 0)
    return 0;

  return (regnum >= mm0_regnum && regnum < mm0_regnum + i386_num_mmx_regs);
}

/* SSE register?  */

static int
i386_sse_regnum_p (struct gdbarch *gdbarch, int regnum)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

#define I387_ST0_REGNUM tdep->st0_regnum
#define I387_NUM_XMM_REGS tdep->num_xmm_regs

  if (I387_NUM_XMM_REGS == 0)
    return 0;

  return (I387_XMM0_REGNUM <= regnum && regnum < I387_MXCSR_REGNUM);

#undef I387_ST0_REGNUM
#undef I387_NUM_XMM_REGS
}

static int
i386_mxcsr_regnum_p (struct gdbarch *gdbarch, int regnum)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

#define I387_ST0_REGNUM tdep->st0_regnum
#define I387_NUM_XMM_REGS tdep->num_xmm_regs

  if (I387_NUM_XMM_REGS == 0)
    return 0;

  return (regnum == I387_MXCSR_REGNUM);

#undef I387_ST0_REGNUM
#undef I387_NUM_XMM_REGS
}

#define I387_ST0_REGNUM (gdbarch_tdep (current_gdbarch)->st0_regnum)
#define I387_MM0_REGNUM (gdbarch_tdep (current_gdbarch)->mm0_regnum)
#define I387_NUM_XMM_REGS (gdbarch_tdep (current_gdbarch)->num_xmm_regs)

/* FP register?  */

int
i386_fp_regnum_p (int regnum)
{
  if (I387_ST0_REGNUM < 0)
    return 0;

  return (I387_ST0_REGNUM <= regnum && regnum < I387_FCTRL_REGNUM);
}

int
i386_fpc_regnum_p (int regnum)
{
  if (I387_ST0_REGNUM < 0)
    return 0;

  return (I387_FCTRL_REGNUM <= regnum && regnum < I387_XMM0_REGNUM);
}

/* Return the name of register REG.  */

const char *
i386_register_name (int reg)
{
  if (i386_mmx_regnum_p (current_gdbarch, reg))
    return i386_mmx_names[reg - I387_MM0_REGNUM];

  if (reg >= 0 && reg < i386_num_register_names)
    return i386_register_names[reg];

  return NULL;
}

/* Convert stabs register number REG to the appropriate register
   number used by GDB.  */

static int
i386_stab_reg_to_regnum (int reg)
{
  /* This implements what GCC calls the "default" register map.  */
  if (reg >= 0 && reg <= 7)
    {
      /* General-purpose registers.  */
      return reg;
    }
  else if (reg >= 12 && reg <= 19)
    {
      /* Floating-point registers.  */
      return reg - 12 + I387_ST0_REGNUM;
    }
  else if (reg >= 21 && reg <= 28)
    {
      /* SSE registers.  */
      return reg - 21 + I387_XMM0_REGNUM;
    }
  else if (reg >= 29 && reg <= 36)
    {
      /* MMX registers.  */
      return reg - 29 + I387_MM0_REGNUM;
    }

  /* This will hopefully provoke a warning.  */
  return NUM_REGS + NUM_PSEUDO_REGS;
}

/* Convert DWARF register number REG to the appropriate register
   number used by GDB.  */

static int
i386_dwarf_reg_to_regnum (int reg)
{
  /* The DWARF register numbering includes %eip and %eflags, and
     numbers the floating point registers differently.  */
  if (reg >= 0 && reg <= 9)
    {
      /* General-purpose registers.  */
      return reg;
    }
  else if (reg >= 11 && reg <= 18)
    {
      /* Floating-point registers.  */
      return reg - 11 + I387_ST0_REGNUM;
    }
  else if (reg >= 21)
    {
      /* The SSE and MMX registers have identical numbers as in stabs.  */
      return i386_stab_reg_to_regnum (reg);
    }

  /* This will hopefully provoke a warning.  */
  return NUM_REGS + NUM_PSEUDO_REGS;
}

#undef I387_ST0_REGNUM
#undef I387_MM0_REGNUM
#undef I387_NUM_XMM_REGS


/* This is the variable that is set with "set disassembly-flavor", and
   its legitimate values.  */
static const char att_flavor[] = "att";
static const char intel_flavor[] = "intel";
static const char *valid_flavors[] =
{
  att_flavor,
  intel_flavor,
  NULL
};
static const char *disassembly_flavor = att_flavor;


/* Use the program counter to determine the contents and size of a
   breakpoint instruction.  Return a pointer to a string of bytes that
   encode a breakpoint instruction, store the length of the string in
   *LEN and optionally adjust *PC to point to the correct memory
   location for inserting the breakpoint.

   On the i386 we have a single breakpoint that fits in a single byte
   and can be inserted anywhere.

   This function is 64-bit safe.  */
   
static const unsigned char *
i386_breakpoint_from_pc (CORE_ADDR *pc, int *len)
{
  static unsigned char break_insn[] = { 0xcc };	/* int 3 */
  
  *len = sizeof (break_insn);
  return break_insn;
}

#ifdef I386_REGNO_TO_SYMMETRY
#error "The Sequent Symmetry is no longer supported."
#endif

/* According to the System V ABI, the registers %ebp, %ebx, %edi, %esi
   and %esp "belong" to the calling function.  Therefore these
   registers should be saved if they're going to be modified.  */

/* The maximum number of saved registers.  This should include all
   registers mentioned above, and %eip.  */
#define I386_NUM_SAVED_REGS	I386_NUM_GREGS

struct i386_frame_cache
{
  /* Base address.  */
  CORE_ADDR base;
  CORE_ADDR sp_offset;
  CORE_ADDR pc;

  /* Saved registers.  */
  CORE_ADDR saved_regs[I386_NUM_SAVED_REGS];
  CORE_ADDR saved_sp;
  int pc_in_eax;

  /* Stack space reserved for local variables.  */
  long locals;
};

/* Allocate and initialize a frame cache.  */

static struct i386_frame_cache *
i386_alloc_frame_cache (void)
{
  struct i386_frame_cache *cache;
  int i;

  cache = FRAME_OBSTACK_ZALLOC (struct i386_frame_cache);

  /* Base address.  */
  cache->base = 0;
  cache->sp_offset = -4;
  cache->pc = 0;

  /* Saved registers.  We initialize these to -1 since zero is a valid
     offset (that's where %ebp is supposed to be stored).  */
  for (i = 0; i < I386_NUM_SAVED_REGS; i++)
    cache->saved_regs[i] = -1;
  cache->saved_sp = 0;
  cache->pc_in_eax = 0;

  /* Frameless until proven otherwise.  */
  cache->locals = -1;

  return cache;
}

/* If the instruction at PC is a jump, return the address of its
   target.  Otherwise, return PC.  */

static CORE_ADDR
i386_follow_jump (CORE_ADDR pc)
{
  unsigned char op;
  long delta = 0;
  int data16 = 0;

  op = read_memory_unsigned_integer (pc, 1);
  if (op == 0x66)
    {
      data16 = 1;
      op = read_memory_unsigned_integer (pc + 1, 1);
    }

  switch (op)
    {
    case 0xe9:
      /* Relative jump: if data16 == 0, disp32, else disp16.  */
      if (data16)
	{
	  delta = read_memory_integer (pc + 2, 2);

	  /* Include the size of the jmp instruction (including the
             0x66 prefix).  */
	  delta += 4;
	}
      else
	{
	  delta = read_memory_integer (pc + 1, 4);

	  /* Include the size of the jmp instruction.  */
	  delta += 5;
	}
      break;
    case 0xeb:
      /* Relative jump, disp8 (ignore data16).  */
      delta = read_memory_integer (pc + data16 + 1, 1);

      delta += data16 + 2;
      break;
    }

  return pc + delta;
}

/* Check whether PC points at a prologue for a function returning a
   structure or union.  If so, it updates CACHE and returns the
   address of the first instruction after the code sequence that
   removes the "hidden" argument from the stack or CURRENT_PC,
   whichever is smaller.  Otherwise, return PC.  */

static CORE_ADDR
i386_analyze_struct_return (CORE_ADDR pc, CORE_ADDR current_pc,
			    struct i386_frame_cache *cache)
{
  /* Functions that return a structure or union start with:

        popl %eax             0x58
        xchgl %eax, (%esp)    0x87 0x04 0x24
     or xchgl %eax, 0(%esp)   0x87 0x44 0x24 0x00

     (the System V compiler puts out the second `xchg' instruction,
     and the assembler doesn't try to optimize it, so the 'sib' form
     gets generated).  This sequence is used to get the address of the
     return buffer for a function that returns a structure.  */
  static unsigned char proto1[3] = { 0x87, 0x04, 0x24 };
  static unsigned char proto2[4] = { 0x87, 0x44, 0x24, 0x00 };
  unsigned char buf[4];
  unsigned char op;

  if (current_pc <= pc)
    return pc;

  op = read_memory_unsigned_integer (pc, 1);

  if (op != 0x58)		/* popl %eax */
    return pc;

  read_memory (pc + 1, buf, 4);
  if (memcmp (buf, proto1, 3) != 0 && memcmp (buf, proto2, 4) != 0)
    return pc;

  if (current_pc == pc)
    {
      cache->sp_offset += 4;
      return current_pc;
    }

  if (current_pc == pc + 1)
    {
      cache->pc_in_eax = 1;
      return current_pc;
    }
  
  if (buf[1] == proto1[1])
    return pc + 4;
  else
    return pc + 5;
}

static CORE_ADDR
i386_skip_probe (CORE_ADDR pc)
{
  /* A function may start with

        pushl constant
        call _probe
	addl $4, %esp
	   
     followed by

        pushl %ebp

     etc.  */
  unsigned char buf[8];
  unsigned char op;

  op = read_memory_unsigned_integer (pc, 1);

  if (op == 0x68 || op == 0x6a)
    {
      int delta;

      /* Skip past the `pushl' instruction; it has either a one-byte or a
	 four-byte operand, depending on the opcode.  */
      if (op == 0x68)
	delta = 5;
      else
	delta = 2;

      /* Read the following 8 bytes, which should be `call _probe' (6
	 bytes) followed by `addl $4,%esp' (2 bytes).  */
      read_memory (pc + delta, buf, sizeof (buf));
      if (buf[0] == 0xe8 && buf[6] == 0xc4 && buf[7] == 0x4)
	pc += delta + sizeof (buf);
    }

  return pc;
}

/* Check whether PC points at a code that sets up a new stack frame.
   If so, it updates CACHE and returns the address of the first
   instruction after the sequence that sets removes the "hidden"
   argument from the stack or CURRENT_PC, whichever is smaller.
   Otherwise, return PC.  */

static CORE_ADDR
i386_analyze_frame_setup (CORE_ADDR pc, CORE_ADDR current_pc,
			  struct i386_frame_cache *cache)
{
  unsigned char op;
  int skip = 0;

  if (current_pc <= pc)
    return current_pc;

  op = read_memory_unsigned_integer (pc, 1);

  if (op == 0x55)		/* pushl %ebp */
    {
      /* Take into account that we've executed the `pushl %ebp' that
	 starts this instruction sequence.  */
      cache->saved_regs[I386_EBP_REGNUM] = 0;
      cache->sp_offset += 4;

      /* If that's all, return now.  */
      if (current_pc <= pc + 1)
	return current_pc;

      op = read_memory_unsigned_integer (pc + 1, 1);

      /* Check for some special instructions that might be migrated
	 by GCC into the prologue.  We check for

	    xorl %ebx, %ebx
	    xorl %ecx, %ecx
	    xorl %edx, %edx
	    xorl %eax, %eax

	 and the equivalent

	    subl %ebx, %ebx
	    subl %ecx, %ecx
	    subl %edx, %edx
	    subl %eax, %eax

	 Because of the symmetry, there are actually two ways to
	 encode these instructions; with opcode bytes 0x29 and 0x2b
	 for `subl' and opcode bytes 0x31 and 0x33 for `xorl'.

	 Make sure we only skip these instructions if we later see the
	 `movl %esp, %ebp' that actually sets up the frame.  */
      while (op == 0x29 || op == 0x2b || op == 0x31 || op == 0x33)
	{
	  op = read_memory_unsigned_integer (pc + skip + 2, 1);
	  switch (op)
	    {
	    case 0xdb:	/* %ebx */
	    case 0xc9:	/* %ecx */
	    case 0xd2:	/* %edx */
	    case 0xc0:	/* %eax */
	      skip += 2;
	      break;
	    default:
	      return pc + 1;
	    }

	  op = read_memory_unsigned_integer (pc + skip + 1, 1);
	}

      /* Check for `movl %esp, %ebp' -- can be written in two ways.  */
      switch (op)
	{
	case 0x8b:
	  if (read_memory_unsigned_integer (pc + skip + 2, 1) != 0xec)
	    return pc + 1;
	  break;
	case 0x89:
	  if (read_memory_unsigned_integer (pc + skip + 2, 1) != 0xe5)
	    return pc + 1;
	  break;
	default:
	  return pc + 1;
	}

      /* OK, we actually have a frame.  We just don't know how large
	 it is yet.  Set its size to zero.  We'll adjust it if
	 necessary.  We also now commit to skipping the special
	 instructions mentioned before.  */
      cache->locals = 0;
      pc += skip;

      /* If that's all, return now.  */
      if (current_pc <= pc + 3)
	return current_pc;

      /* Check for stack adjustment 

	    subl $XXX, %esp

	 NOTE: You can't subtract a 16 bit immediate from a 32 bit
	 reg, so we don't have to worry about a data16 prefix.  */
      op = read_memory_unsigned_integer (pc + 3, 1);
      if (op == 0x83)
	{
	  /* `subl' with 8 bit immediate.  */
	  if (read_memory_unsigned_integer (pc + 4, 1) != 0xec)
	    /* Some instruction starting with 0x83 other than `subl'.  */
	    return pc + 3;

	  /* `subl' with signed byte immediate (though it wouldn't make
	     sense to be negative).  */
	  cache->locals = read_memory_integer (pc + 5, 1);
	  return pc + 6;
	}
      else if (op == 0x81)
	{
	  /* Maybe it is `subl' with a 32 bit immedediate.  */
	  if (read_memory_unsigned_integer (pc + 4, 1) != 0xec)
	    /* Some instruction starting with 0x81 other than `subl'.  */
	    return pc + 3;

	  /* It is `subl' with a 32 bit immediate.  */
	  cache->locals = read_memory_integer (pc + 5, 4);
	  return pc + 9;
	}
      else
	{
	  /* Some instruction other than `subl'.  */
	  return pc + 3;
	}
    }
  else if (op == 0xc8)		/* enter $XXX */
    {
      cache->locals = read_memory_unsigned_integer (pc + 1, 2);
      return pc + 4;
    }

  return pc;
}

/* Check whether PC points at code that saves registers on the stack.
   If so, it updates CACHE and returns the address of the first
   instruction after the register saves or CURRENT_PC, whichever is
   smaller.  Otherwise, return PC.  */

static CORE_ADDR
i386_analyze_register_saves (CORE_ADDR pc, CORE_ADDR current_pc,
			     struct i386_frame_cache *cache)
{
  CORE_ADDR offset = 0;
  unsigned char op;
  int i;

  if (cache->locals > 0)
    offset -= cache->locals;
  for (i = 0; i < 8 && pc < current_pc; i++)
    {
      op = read_memory_unsigned_integer (pc, 1);
      if (op < 0x50 || op > 0x57)
	break;

      offset -= 4;
      cache->saved_regs[op - 0x50] = offset;
      cache->sp_offset += 4;
      pc++;
    }

  return pc;
}

/* Do a full analysis of the prologue at PC and update CACHE
   accordingly.  Bail out early if CURRENT_PC is reached.  Return the
   address where the analysis stopped.

   We handle these cases:

   The startup sequence can be at the start of the function, or the
   function can start with a branch to startup code at the end.

   %ebp can be set up with either the 'enter' instruction, or "pushl
   %ebp, movl %esp, %ebp" (`enter' is too slow to be useful, but was
   once used in the System V compiler).

   Local space is allocated just below the saved %ebp by either the
   'enter' instruction, or by "subl $<size>, %esp".  'enter' has a 16
   bit unsigned argument for space to allocate, and the 'addl'
   instruction could have either a signed byte, or 32 bit immediate.

   Next, the registers used by this function are pushed.  With the
   System V compiler they will always be in the order: %edi, %esi,
   %ebx (and sometimes a harmless bug causes it to also save but not
   restore %eax); however, the code below is willing to see the pushes
   in any order, and will handle up to 8 of them.
 
   If the setup sequence is at the end of the function, then the next
   instruction will be a branch back to the start.  */

static CORE_ADDR
i386_analyze_prologue (CORE_ADDR pc, CORE_ADDR current_pc,
		       struct i386_frame_cache *cache)
{
  pc = i386_follow_jump (pc);
  pc = i386_analyze_struct_return (pc, current_pc, cache);
  pc = i386_skip_probe (pc);
  pc = i386_analyze_frame_setup (pc, current_pc, cache);
  return i386_analyze_register_saves (pc, current_pc, cache);
}

/* Return PC of first real instruction.  */

static CORE_ADDR
i386_skip_prologue (CORE_ADDR start_pc)
{
  static unsigned char pic_pat[6] =
  {
    0xe8, 0, 0, 0, 0,		/* call 0x0 */
    0x5b,			/* popl %ebx */
  };
  struct i386_frame_cache cache;
  CORE_ADDR pc;
  unsigned char op;
  int i;

  cache.locals = -1;
  pc = i386_analyze_prologue (start_pc, 0xffffffff, &cache);
  if (cache.locals < 0)
    return start_pc;

  /* Found valid frame setup.  */

  /* The native cc on SVR4 in -K PIC mode inserts the following code
     to get the address of the global offset table (GOT) into register
     %ebx:

        call	0x0
	popl    %ebx
        movl    %ebx,x(%ebp)    (optional)
        addl    y,%ebx

     This code is with the rest of the prologue (at the end of the
     function), so we have to skip it to get to the first real
     instruction at the start of the function.  */

  for (i = 0; i < 6; i++)
    {
      op = read_memory_unsigned_integer (pc + i, 1);
      if (pic_pat[i] != op)
	break;
    }
  if (i == 6)
    {
      int delta = 6;

      op = read_memory_unsigned_integer (pc + delta, 1);

      if (op == 0x89)		/* movl %ebx, x(%ebp) */
	{
	  op = read_memory_unsigned_integer (pc + delta + 1, 1);

	  if (op == 0x5d)	/* One byte offset from %ebp.  */
	    delta += 3;
	  else if (op == 0x9d)	/* Four byte offset from %ebp.  */
	    delta += 6;
	  else			/* Unexpected instruction.  */
	    delta = 0;

	  op = read_memory_unsigned_integer (pc + delta, 1);
	}

      /* addl y,%ebx */
      if (delta > 0 && op == 0x81
	  && read_memory_unsigned_integer (pc + delta + 1, 1) == 0xc3)
	{
	  pc += delta + 6;
	}
    }

  return i386_follow_jump (pc);
}

/* This function is 64-bit safe.  */

static CORE_ADDR
i386_unwind_pc (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  char buf[8];

  frame_unwind_register (next_frame, PC_REGNUM, buf);
  return extract_typed_address (buf, builtin_type_void_func_ptr);
}


/* Normal frames.  */

static struct i386_frame_cache *
i386_frame_cache (struct frame_info *next_frame, void **this_cache)
{
  struct i386_frame_cache *cache;
  char buf[4];
  int i;

  if (*this_cache)
    return *this_cache;

  cache = i386_alloc_frame_cache ();
  *this_cache = cache;

  /* In principle, for normal frames, %ebp holds the frame pointer,
     which holds the base address for the current stack frame.
     However, for functions that don't need it, the frame pointer is
     optional.  For these "frameless" functions the frame pointer is
     actually the frame pointer of the calling frame.  Signal
     trampolines are just a special case of a "frameless" function.
     They (usually) share their frame pointer with the frame that was
     in progress when the signal occurred.  */

  frame_unwind_register (next_frame, I386_EBP_REGNUM, buf);
  cache->base = extract_unsigned_integer (buf, 4);
  if (cache->base == 0)
    return cache;

  /* For normal frames, %eip is stored at 4(%ebp).  */
  cache->saved_regs[I386_EIP_REGNUM] = 4;

  cache->pc = frame_func_unwind (next_frame);
  if (cache->pc != 0)
    i386_analyze_prologue (cache->pc, frame_pc_unwind (next_frame), cache);

  if (cache->locals < 0)
    {
      /* We didn't find a valid frame, which means that CACHE->base
	 currently holds the frame pointer for our calling frame.  If
	 we're at the start of a function, or somewhere half-way its
	 prologue, the function's frame probably hasn't been fully
	 setup yet.  Try to reconstruct the base address for the stack
	 frame by looking at the stack pointer.  For truly "frameless"
	 functions this might work too.  */

      frame_unwind_register (next_frame, I386_ESP_REGNUM, buf);
      cache->base = extract_unsigned_integer (buf, 4) + cache->sp_offset;
    }

  /* Now that we have the base address for the stack frame we can
     calculate the value of %esp in the calling frame.  */
  cache->saved_sp = cache->base + 8;

  /* Adjust all the saved registers such that they contain addresses
     instead of offsets.  */
  for (i = 0; i < I386_NUM_SAVED_REGS; i++)
    if (cache->saved_regs[i] != -1)
      cache->saved_regs[i] += cache->base;

  return cache;
}

static void
i386_frame_this_id (struct frame_info *next_frame, void **this_cache,
		    struct frame_id *this_id)
{
  struct i386_frame_cache *cache = i386_frame_cache (next_frame, this_cache);

  /* This marks the outermost frame.  */
  if (cache->base == 0)
    return;

  /* See the end of i386_push_dummy_call.  */
  (*this_id) = frame_id_build (cache->base + 8, cache->pc);
}

static void
i386_frame_prev_register (struct frame_info *next_frame, void **this_cache,
			  int regnum, int *optimizedp,
			  enum lval_type *lvalp, CORE_ADDR *addrp,
			  int *realnump, void *valuep)
{
  struct i386_frame_cache *cache = i386_frame_cache (next_frame, this_cache);

  gdb_assert (regnum >= 0);

  /* The System V ABI says that:

     "The flags register contains the system flags, such as the
     direction flag and the carry flag.  The direction flag must be
     set to the forward (that is, zero) direction before entry and
     upon exit from a function.  Other user flags have no specified
     role in the standard calling sequence and are not preserved."

     To guarantee the "upon exit" part of that statement we fake a
     saved flags register that has its direction flag cleared.

     Note that GCC doesn't seem to rely on the fact that the direction
     flag is cleared after a function return; it always explicitly
     clears the flag before operations where it matters.

     FIXME: kettenis/20030316: I'm not quite sure whether this is the
     right thing to do.  The way we fake the flags register here makes
     it impossible to change it.  */

  if (regnum == I386_EFLAGS_REGNUM)
    {
      *optimizedp = 0;
      *lvalp = not_lval;
      *addrp = 0;
      *realnump = -1;
      if (valuep)
	{
	  ULONGEST val;

	  /* Clear the direction flag.  */
	  val = frame_unwind_register_unsigned (next_frame,
						I386_EFLAGS_REGNUM);
	  val &= ~(1 << 10);
	  store_unsigned_integer (valuep, 4, val);
	}

      return;
    }

  if (regnum == I386_EIP_REGNUM && cache->pc_in_eax)
    {
      frame_register_unwind (next_frame, I386_EAX_REGNUM,
			     optimizedp, lvalp, addrp, realnump, valuep);
      return;
    }

  if (regnum == I386_ESP_REGNUM && cache->saved_sp)
    {
      *optimizedp = 0;
      *lvalp = not_lval;
      *addrp = 0;
      *realnump = -1;
      if (valuep)
	{
	  /* Store the value.  */
	  store_unsigned_integer (valuep, 4, cache->saved_sp);
	}
      return;
    }

  if (regnum < I386_NUM_SAVED_REGS && cache->saved_regs[regnum] != -1)
    {
      *optimizedp = 0;
      *lvalp = lval_memory;
      *addrp = cache->saved_regs[regnum];
      *realnump = -1;
      if (valuep)
	{
	  /* Read the value in from memory.  */
	  read_memory (*addrp, valuep,
		       register_size (current_gdbarch, regnum));
	}
      return;
    }

  frame_register_unwind (next_frame, regnum,
			 optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind i386_frame_unwind =
{
  NORMAL_FRAME,
  i386_frame_this_id,
  i386_frame_prev_register
};

static const struct frame_unwind *
i386_frame_sniffer (struct frame_info *next_frame)
{
  return &i386_frame_unwind;
}


/* Signal trampolines.  */

static struct i386_frame_cache *
i386_sigtramp_frame_cache (struct frame_info *next_frame, void **this_cache)
{
  struct i386_frame_cache *cache;
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  CORE_ADDR addr;
  char buf[4];

  if (*this_cache)
    return *this_cache;

  cache = i386_alloc_frame_cache ();

  frame_unwind_register (next_frame, I386_ESP_REGNUM, buf);
  cache->base = extract_unsigned_integer (buf, 4) - 4;

  addr = tdep->sigcontext_addr (next_frame);
  if (tdep->sc_reg_offset)
    {
      int i;

      gdb_assert (tdep->sc_num_regs <= I386_NUM_SAVED_REGS);

      for (i = 0; i < tdep->sc_num_regs; i++)
	if (tdep->sc_reg_offset[i] != -1)
	  cache->saved_regs[i] = addr + tdep->sc_reg_offset[i];
    }
  else
    {
      cache->saved_regs[I386_EIP_REGNUM] = addr + tdep->sc_pc_offset;
      cache->saved_regs[I386_ESP_REGNUM] = addr + tdep->sc_sp_offset;
    }

  *this_cache = cache;
  return cache;
}

static void
i386_sigtramp_frame_this_id (struct frame_info *next_frame, void **this_cache,
			     struct frame_id *this_id)
{
  struct i386_frame_cache *cache =
    i386_sigtramp_frame_cache (next_frame, this_cache);

  /* See the end of i386_push_dummy_call.  */
  (*this_id) = frame_id_build (cache->base + 8, frame_pc_unwind (next_frame));
}

static void
i386_sigtramp_frame_prev_register (struct frame_info *next_frame,
				   void **this_cache,
				   int regnum, int *optimizedp,
				   enum lval_type *lvalp, CORE_ADDR *addrp,
				   int *realnump, void *valuep)
{
  /* Make sure we've initialized the cache.  */
  i386_sigtramp_frame_cache (next_frame, this_cache);

  i386_frame_prev_register (next_frame, this_cache, regnum,
			    optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind i386_sigtramp_frame_unwind =
{
  SIGTRAMP_FRAME,
  i386_sigtramp_frame_this_id,
  i386_sigtramp_frame_prev_register
};

static const struct frame_unwind *
i386_sigtramp_frame_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  char *name;

  /* We shouldn't even bother to try if the OSABI didn't register
     a sigcontext_addr handler.  */
  if (!gdbarch_tdep (current_gdbarch)->sigcontext_addr)
    return NULL;

  find_pc_partial_function (pc, &name, NULL, NULL);
  if (PC_IN_SIGTRAMP (pc, name))
    return &i386_sigtramp_frame_unwind;

  return NULL;
}


static CORE_ADDR
i386_frame_base_address (struct frame_info *next_frame, void **this_cache)
{
  struct i386_frame_cache *cache = i386_frame_cache (next_frame, this_cache);

  return cache->base;
}

static const struct frame_base i386_frame_base =
{
  &i386_frame_unwind,
  i386_frame_base_address,
  i386_frame_base_address,
  i386_frame_base_address
};

static struct frame_id
i386_unwind_dummy_id (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  char buf[4];
  CORE_ADDR fp;

  frame_unwind_register (next_frame, I386_EBP_REGNUM, buf);
  fp = extract_unsigned_integer (buf, 4);

  /* See the end of i386_push_dummy_call.  */
  return frame_id_build (fp + 8, frame_pc_unwind (next_frame));
}


/* Figure out where the longjmp will land.  Slurp the args out of the
   stack.  We expect the first arg to be a pointer to the jmp_buf
   structure from which we extract the address that we will land at.
   This address is copied into PC.  This routine returns non-zero on
   success.

   This function is 64-bit safe.  */

static int
i386_get_longjmp_target (CORE_ADDR *pc)
{
  char buf[8];
  CORE_ADDR sp, jb_addr;
  int jb_pc_offset = gdbarch_tdep (current_gdbarch)->jb_pc_offset;
  int len = TYPE_LENGTH (builtin_type_void_func_ptr);

  /* If JB_PC_OFFSET is -1, we have no way to find out where the
     longjmp will land.  */
  if (jb_pc_offset == -1)
    return 0;

  /* Don't use I386_ESP_REGNUM here, since this function is also used
     for AMD64.  */
  regcache_cooked_read (current_regcache, SP_REGNUM, buf);
  sp = extract_typed_address (buf, builtin_type_void_data_ptr);
  if (target_read_memory (sp + len, buf, len))
    return 0;

  jb_addr = extract_typed_address (buf, builtin_type_void_data_ptr);
  if (target_read_memory (jb_addr + jb_pc_offset, buf, len))
    return 0;

  *pc = extract_typed_address (buf, builtin_type_void_func_ptr);
  return 1;
}


static CORE_ADDR
i386_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
		      struct regcache *regcache, CORE_ADDR bp_addr, int nargs,
		      struct value **args, CORE_ADDR sp, int struct_return,
		      CORE_ADDR struct_addr)
{
  char buf[4];
  int i;

  /* Push arguments in reverse order.  */
  for (i = nargs - 1; i >= 0; i--)
    {
      int len = TYPE_LENGTH (VALUE_ENCLOSING_TYPE (args[i]));

      /* The System V ABI says that:

	 "An argument's size is increased, if necessary, to make it a
	 multiple of [32-bit] words.  This may require tail padding,
	 depending on the size of the argument."

	 This makes sure the stack says word-aligned.  */
      sp -= (len + 3) & ~3;
      write_memory (sp, VALUE_CONTENTS_ALL (args[i]), len);
    }

  /* Push value address.  */
  if (struct_return)
    {
      sp -= 4;
      store_unsigned_integer (buf, 4, struct_addr);
      write_memory (sp, buf, 4);
    }

  /* Store return address.  */
  sp -= 4;
  store_unsigned_integer (buf, 4, bp_addr);
  write_memory (sp, buf, 4);

  /* Finally, update the stack pointer...  */
  store_unsigned_integer (buf, 4, sp);
  regcache_cooked_write (regcache, I386_ESP_REGNUM, buf);

  /* ...and fake a frame pointer.  */
  regcache_cooked_write (regcache, I386_EBP_REGNUM, buf);

  /* MarkK wrote: This "+ 8" is all over the place:
     (i386_frame_this_id, i386_sigtramp_frame_this_id,
     i386_unwind_dummy_id).  It's there, since all frame unwinders for
     a given target have to agree (within a certain margin) on the
     defenition of the stack address of a frame.  Otherwise
     frame_id_inner() won't work correctly.  Since DWARF2/GCC uses the
     stack address *before* the function call as a frame's CFA.  On
     the i386, when %ebp is used as a frame pointer, the offset
     between the contents %ebp and the CFA as defined by GCC.  */
  return sp + 8;
}

/* These registers are used for returning integers (and on some
   targets also for returning `struct' and `union' values when their
   size and alignment match an integer type).  */
#define LOW_RETURN_REGNUM	I386_EAX_REGNUM /* %eax */
#define HIGH_RETURN_REGNUM	I386_EDX_REGNUM /* %edx */

/* Read, for architecture GDBARCH, a function return value of TYPE
   from REGCACHE, and copy that into VALBUF.  */

static void
i386_extract_return_value (struct gdbarch *gdbarch, struct type *type,
			   struct regcache *regcache, void *valbuf)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  int len = TYPE_LENGTH (type);
  char buf[I386_MAX_REGISTER_SIZE];

  if (TYPE_CODE (type) == TYPE_CODE_FLT)
    {
      if (tdep->st0_regnum < 0)
	{
	  warning ("Cannot find floating-point return value.");
	  memset (valbuf, 0, len);
	  return;
	}

      /* Floating-point return values can be found in %st(0).  Convert
	 its contents to the desired type.  This is probably not
	 exactly how it would happen on the target itself, but it is
	 the best we can do.  */
      regcache_raw_read (regcache, I386_ST0_REGNUM, buf);
      convert_typed_floating (buf, builtin_type_i387_ext, valbuf, type);
    }
  else
    {
      int low_size = register_size (current_gdbarch, LOW_RETURN_REGNUM);
      int high_size = register_size (current_gdbarch, HIGH_RETURN_REGNUM);

      if (len <= low_size)
	{
	  regcache_raw_read (regcache, LOW_RETURN_REGNUM, buf);
	  memcpy (valbuf, buf, len);
	}
      else if (len <= (low_size + high_size))
	{
	  regcache_raw_read (regcache, LOW_RETURN_REGNUM, buf);
	  memcpy (valbuf, buf, low_size);
	  regcache_raw_read (regcache, HIGH_RETURN_REGNUM, buf);
	  memcpy ((char *) valbuf + low_size, buf, len - low_size);
	}
      else
	internal_error (__FILE__, __LINE__,
			"Cannot extract return value of %d bytes long.", len);
    }
}

/* Write, for architecture GDBARCH, a function return value of TYPE
   from VALBUF into REGCACHE.  */

static void
i386_store_return_value (struct gdbarch *gdbarch, struct type *type,
			 struct regcache *regcache, const void *valbuf)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  int len = TYPE_LENGTH (type);

  /* Define I387_ST0_REGNUM such that we use the proper definitions
     for the architecture.  */
#define I387_ST0_REGNUM I386_ST0_REGNUM

  if (TYPE_CODE (type) == TYPE_CODE_FLT)
    {
      ULONGEST fstat;
      char buf[I386_MAX_REGISTER_SIZE];

      if (tdep->st0_regnum < 0)
	{
	  warning ("Cannot set floating-point return value.");
	  return;
	}

      /* Returning floating-point values is a bit tricky.  Apart from
         storing the return value in %st(0), we have to simulate the
         state of the FPU at function return point.  */

      /* Convert the value found in VALBUF to the extended
	 floating-point format used by the FPU.  This is probably
	 not exactly how it would happen on the target itself, but
	 it is the best we can do.  */
      convert_typed_floating (valbuf, type, buf, builtin_type_i387_ext);
      regcache_raw_write (regcache, I386_ST0_REGNUM, buf);

      /* Set the top of the floating-point register stack to 7.  The
         actual value doesn't really matter, but 7 is what a normal
         function return would end up with if the program started out
         with a freshly initialized FPU.  */
      regcache_raw_read_unsigned (regcache, I387_FSTAT_REGNUM, &fstat);
      fstat |= (7 << 11);
      regcache_raw_write_unsigned (regcache, I387_FSTAT_REGNUM, fstat);

      /* Mark %st(1) through %st(7) as empty.  Since we set the top of
         the floating-point register stack to 7, the appropriate value
         for the tag word is 0x3fff.  */
      regcache_raw_write_unsigned (regcache, I387_FTAG_REGNUM, 0x3fff);
    }
  else
    {
      int low_size = register_size (current_gdbarch, LOW_RETURN_REGNUM);
      int high_size = register_size (current_gdbarch, HIGH_RETURN_REGNUM);

      if (len <= low_size)
	regcache_raw_write_part (regcache, LOW_RETURN_REGNUM, 0, len, valbuf);
      else if (len <= (low_size + high_size))
	{
	  regcache_raw_write (regcache, LOW_RETURN_REGNUM, valbuf);
	  regcache_raw_write_part (regcache, HIGH_RETURN_REGNUM, 0,
				   len - low_size, (char *) valbuf + low_size);
	}
      else
	internal_error (__FILE__, __LINE__,
			"Cannot store return value of %d bytes long.", len);
    }

#undef I387_ST0_REGNUM
}


/* This is the variable that is set with "set struct-convention", and
   its legitimate values.  */
static const char default_struct_convention[] = "default";
static const char pcc_struct_convention[] = "pcc";
static const char reg_struct_convention[] = "reg";
static const char *valid_conventions[] =
{
  default_struct_convention,
  pcc_struct_convention,
  reg_struct_convention,
  NULL
};
static const char *struct_convention = default_struct_convention;

/* Return non-zero if TYPE, which is assumed to be a structure or
   union type, should be returned in registers for architecture
   GDBARCH.  */

static int
i386_reg_struct_return_p (struct gdbarch *gdbarch, struct type *type)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  enum type_code code = TYPE_CODE (type);
  int len = TYPE_LENGTH (type);

  gdb_assert (code == TYPE_CODE_STRUCT || code == TYPE_CODE_UNION);

  if (struct_convention == pcc_struct_convention
      || (struct_convention == default_struct_convention
	  && tdep->struct_return == pcc_struct_return))
    return 0;

  return (len == 1 || len == 2 || len == 4 || len == 8);
}

/* Determine, for architecture GDBARCH, how a return value of TYPE
   should be returned.  If it is supposed to be returned in registers,
   and READBUF is non-zero, read the appropriate value from REGCACHE,
   and copy it into READBUF.  If WRITEBUF is non-zero, write the value
   from WRITEBUF into REGCACHE.  */

static enum return_value_convention
i386_return_value (struct gdbarch *gdbarch, struct type *type,
		   struct regcache *regcache, void *readbuf,
		   const void *writebuf)
{
  enum type_code code = TYPE_CODE (type);

  if ((code == TYPE_CODE_STRUCT || code == TYPE_CODE_UNION)
      && !i386_reg_struct_return_p (gdbarch, type))
    return RETURN_VALUE_STRUCT_CONVENTION;

  /* This special case is for structures consisting of a single
     `float' or `double' member.  These structures are returned in
     %st(0).  For these structures, we call ourselves recursively,
     changing TYPE into the type of the first member of the structure.
     Since that should work for all structures that have only one
     member, we don't bother to check the member's type here.  */
  if (code == TYPE_CODE_STRUCT && TYPE_NFIELDS (type) == 1)
    {
      type = check_typedef (TYPE_FIELD_TYPE (type, 0));
      return i386_return_value (gdbarch, type, regcache, readbuf, writebuf);
    }

  if (readbuf)
    i386_extract_return_value (gdbarch, type, regcache, readbuf);
  if (writebuf)
    i386_store_return_value (gdbarch, type, regcache, writebuf);

  return RETURN_VALUE_REGISTER_CONVENTION;
}


/* Return the GDB type object for the "standard" data type of data in
   register REGNUM.  Perhaps %esi and %edi should go here, but
   potentially they could be used for things other than address.  */

static struct type *
i386_register_type (struct gdbarch *gdbarch, int regnum)
{
  if (regnum == I386_EIP_REGNUM
      || regnum == I386_EBP_REGNUM || regnum == I386_ESP_REGNUM)
    return lookup_pointer_type (builtin_type_void);

  if (i386_fp_regnum_p (regnum))
    return builtin_type_i387_ext;

  if (i386_sse_regnum_p (gdbarch, regnum))
    return builtin_type_vec128i;

  if (i386_mmx_regnum_p (gdbarch, regnum))
    return builtin_type_vec64i;

  return builtin_type_int;
}

/* Map a cooked register onto a raw register or memory.  For the i386,
   the MMX registers need to be mapped onto floating point registers.  */

static int
i386_mmx_regnum_to_fp_regnum (struct regcache *regcache, int regnum)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (get_regcache_arch (regcache));
  int mmxreg, fpreg;
  ULONGEST fstat;
  int tos;

  /* Define I387_ST0_REGNUM such that we use the proper definitions
     for REGCACHE's architecture.  */
#define I387_ST0_REGNUM tdep->st0_regnum

  mmxreg = regnum - tdep->mm0_regnum;
  regcache_raw_read_unsigned (regcache, I387_FSTAT_REGNUM, &fstat);
  tos = (fstat >> 11) & 0x7;
  fpreg = (mmxreg + tos) % 8;

  return (I387_ST0_REGNUM + fpreg);

#undef I387_ST0_REGNUM
}

static void
i386_pseudo_register_read (struct gdbarch *gdbarch, struct regcache *regcache,
			   int regnum, void *buf)
{
  if (i386_mmx_regnum_p (gdbarch, regnum))
    {
      char mmx_buf[MAX_REGISTER_SIZE];
      int fpnum = i386_mmx_regnum_to_fp_regnum (regcache, regnum);

      /* Extract (always little endian).  */
      regcache_raw_read (regcache, fpnum, mmx_buf);
      memcpy (buf, mmx_buf, register_size (gdbarch, regnum));
    }
  else
    regcache_raw_read (regcache, regnum, buf);
}

static void
i386_pseudo_register_write (struct gdbarch *gdbarch, struct regcache *regcache,
			    int regnum, const void *buf)
{
  if (i386_mmx_regnum_p (gdbarch, regnum))
    {
      char mmx_buf[MAX_REGISTER_SIZE];
      int fpnum = i386_mmx_regnum_to_fp_regnum (regcache, regnum);

      /* Read ...  */
      regcache_raw_read (regcache, fpnum, mmx_buf);
      /* ... Modify ... (always little endian).  */
      memcpy (mmx_buf, buf, register_size (gdbarch, regnum));
      /* ... Write.  */
      regcache_raw_write (regcache, fpnum, mmx_buf);
    }
  else
    regcache_raw_write (regcache, regnum, buf);
}


/* Return the register number of the register allocated by GCC after
   REGNUM, or -1 if there is no such register.  */

static int
i386_next_regnum (int regnum)
{
  /* GCC allocates the registers in the order:

     %eax, %edx, %ecx, %ebx, %esi, %edi, %ebp, %esp, ...

     Since storing a variable in %esp doesn't make any sense we return
     -1 for %ebp and for %esp itself.  */
  static int next_regnum[] =
  {
    I386_EDX_REGNUM,		/* Slot for %eax.  */
    I386_EBX_REGNUM,		/* Slot for %ecx.  */
    I386_ECX_REGNUM,		/* Slot for %edx.  */
    I386_ESI_REGNUM,		/* Slot for %ebx.  */
    -1, -1,			/* Slots for %esp and %ebp.  */
    I386_EDI_REGNUM,		/* Slot for %esi.  */
    I386_EBP_REGNUM		/* Slot for %edi.  */
  };

  if (regnum >= 0 && regnum < sizeof (next_regnum) / sizeof (next_regnum[0]))
    return next_regnum[regnum];

  return -1;
}

/* Return nonzero if a value of type TYPE stored in register REGNUM
   needs any special handling.  */

static int
i386_convert_register_p (int regnum, struct type *type)
{
  int len = TYPE_LENGTH (type);

  /* Values may be spread across multiple registers.  Most debugging
     formats aren't expressive enough to specify the locations, so
     some heuristics is involved.  Right now we only handle types that
     have a length that is a multiple of the word size, since GCC
     doesn't seem to put any other types into registers.  */
  if (len > 4 && len % 4 == 0)
    {
      int last_regnum = regnum;

      while (len > 4)
	{
	  last_regnum = i386_next_regnum (last_regnum);
	  len -= 4;
	}

      if (last_regnum != -1)
	return 1;
    }

  return i386_fp_regnum_p (regnum);
}

/* Read a value of type TYPE from register REGNUM in frame FRAME, and
   return its contents in TO.  */

static void
i386_register_to_value (struct frame_info *frame, int regnum,
			struct type *type, void *to)
{
  int len = TYPE_LENGTH (type);
  char *buf = to;

  /* FIXME: kettenis/20030609: What should we do if REGNUM isn't
     available in FRAME (i.e. if it wasn't saved)?  */

  if (i386_fp_regnum_p (regnum))
    {
      i387_register_to_value (frame, regnum, type, to);
      return;
    }

  /* Read a value spread accross multiple registers.  */

  gdb_assert (len > 4 && len % 4 == 0);

  while (len > 0)
    {
      gdb_assert (regnum != -1);
      gdb_assert (register_size (current_gdbarch, regnum) == 4);

      get_frame_register (frame, regnum, buf);
      regnum = i386_next_regnum (regnum);
      len -= 4;
      buf += 4;
    }
}

/* Write the contents FROM of a value of type TYPE into register
   REGNUM in frame FRAME.  */

static void
i386_value_to_register (struct frame_info *frame, int regnum,
			struct type *type, const void *from)
{
  int len = TYPE_LENGTH (type);
  const char *buf = from;

  if (i386_fp_regnum_p (regnum))
    {
      i387_value_to_register (frame, regnum, type, from);
      return;
    }

  /* Write a value spread accross multiple registers.  */

  gdb_assert (len > 4 && len % 4 == 0);

  while (len > 0)
    {
      gdb_assert (regnum != -1);
      gdb_assert (register_size (current_gdbarch, regnum) == 4);

      put_frame_register (frame, regnum, buf);
      regnum = i386_next_regnum (regnum);
      len -= 4;
      buf += 4;
    }
}

/* Supply register REGNUM from the general-purpose register set REGSET
   to register cache REGCACHE.  If REGNUM is -1, do this for all
   registers in REGSET.  */

void
i386_supply_gregset (const struct regset *regset, struct regcache *regcache,
		     int regnum, const void *gregs, size_t len)
{
  const struct gdbarch_tdep *tdep = regset->descr;
  const char *regs = gregs;
  int i;

  gdb_assert (len == tdep->sizeof_gregset);

  for (i = 0; i < tdep->gregset_num_regs; i++)
    {
      if ((regnum == i || regnum == -1)
	  && tdep->gregset_reg_offset[i] != -1)
	regcache_raw_supply (regcache, i, regs + tdep->gregset_reg_offset[i]);
    }
}

/* Supply register REGNUM from the floating-point register set REGSET
   to register cache REGCACHE.  If REGNUM is -1, do this for all
   registers in REGSET.  */

static void
i386_supply_fpregset (const struct regset *regset, struct regcache *regcache,
		      int regnum, const void *fpregs, size_t len)
{
  const struct gdbarch_tdep *tdep = regset->descr;

  if (len == I387_SIZEOF_FXSAVE)
    {
      i387_supply_fxsave (regcache, regnum, fpregs);
      return;
    }

  gdb_assert (len == tdep->sizeof_fpregset);
  i387_supply_fsave (regcache, regnum, fpregs);
}

/* Return the appropriate register set for the core section identified
   by SECT_NAME and SECT_SIZE.  */

const struct regset *
i386_regset_from_core_section (struct gdbarch *gdbarch,
			       const char *sect_name, size_t sect_size)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  if (strcmp (sect_name, ".reg") == 0 && sect_size == tdep->sizeof_gregset)
    {
      if (tdep->gregset == NULL)
	{
	  tdep->gregset = XMALLOC (struct regset);
	  tdep->gregset->descr = tdep;
	  tdep->gregset->supply_regset = i386_supply_gregset;
	}
      return tdep->gregset;
    }

  if ((strcmp (sect_name, ".reg2") == 0 && sect_size == tdep->sizeof_fpregset)
      || (strcmp (sect_name, ".reg-xfp") == 0
	  && sect_size == I387_SIZEOF_FXSAVE))
    {
      if (tdep->fpregset == NULL)
	{
	  tdep->fpregset = XMALLOC (struct regset);
	  tdep->fpregset->descr = tdep;
	  tdep->fpregset->supply_regset = i386_supply_fpregset;
	}
      return tdep->fpregset;
    }

  return NULL;
}


#ifdef STATIC_TRANSFORM_NAME
/* SunPRO encodes the static variables.  This is not related to C++
   mangling, it is done for C too.  */

char *
sunpro_static_transform_name (char *name)
{
  char *p;
  if (IS_STATIC_TRANSFORM_NAME (name))
    {
      /* For file-local statics there will be a period, a bunch of
         junk (the contents of which match a string given in the
         N_OPT), a period and the name.  For function-local statics
         there will be a bunch of junk (which seems to change the
         second character from 'A' to 'B'), a period, the name of the
         function, and the name.  So just skip everything before the
         last period.  */
      p = strrchr (name, '.');
      if (p != NULL)
	name = p + 1;
    }
  return name;
}
#endif /* STATIC_TRANSFORM_NAME */


/* Stuff for WIN32 PE style DLL's but is pretty generic really.  */

CORE_ADDR
i386_pe_skip_trampoline_code (CORE_ADDR pc, char *name)
{
  if (pc && read_memory_unsigned_integer (pc, 2) == 0x25ff) /* jmp *(dest) */
    {
      unsigned long indirect = read_memory_unsigned_integer (pc + 2, 4);
      struct minimal_symbol *indsym =
	indirect ? lookup_minimal_symbol_by_pc (indirect) : 0;
      char *symname = indsym ? SYMBOL_LINKAGE_NAME (indsym) : 0;

      if (symname)
	{
	  if (strncmp (symname, "__imp_", 6) == 0
	      || strncmp (symname, "_imp_", 5) == 0)
	    return name ? 1 : read_memory_unsigned_integer (indirect, 4);
	}
    }
  return 0;			/* Not a trampoline.  */
}


/* Return non-zero if PC and NAME show that we are in a signal
   trampoline.  */

static int
i386_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  return (name && strcmp ("_sigtramp", name) == 0);
}


/* We have two flavours of disassembly.  The machinery on this page
   deals with switching between those.  */

static int
i386_print_insn (bfd_vma pc, struct disassemble_info *info)
{
  gdb_assert (disassembly_flavor == att_flavor
	      || disassembly_flavor == intel_flavor);

  /* FIXME: kettenis/20020915: Until disassembler_options is properly
     constified, cast to prevent a compiler warning.  */
  info->disassembler_options = (char *) disassembly_flavor;
  info->mach = gdbarch_bfd_arch_info (current_gdbarch)->mach;

  return print_insn_i386 (pc, info);
}


/* There are a few i386 architecture variants that differ only
   slightly from the generic i386 target.  For now, we don't give them
   their own source file, but include them here.  As a consequence,
   they'll always be included.  */

/* System V Release 4 (SVR4).  */

static int
i386_svr4_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  /* UnixWare uses _sigacthandler.  The origin of the other symbols is
     currently unknown.  */
  return (name && (strcmp ("_sigreturn", name) == 0
		   || strcmp ("_sigacthandler", name) == 0
		   || strcmp ("sigvechandler", name) == 0));
}

/* Assuming NEXT_FRAME is for a frame following a SVR4 sigtramp
   routine, return the address of the associated sigcontext (ucontext)
   structure.  */

static CORE_ADDR
i386_svr4_sigcontext_addr (struct frame_info *next_frame)
{
  char buf[4];
  CORE_ADDR sp;

  frame_unwind_register (next_frame, I386_ESP_REGNUM, buf);
  sp = extract_unsigned_integer (buf, 4);

  return read_memory_unsigned_integer (sp + 8, 4);
}


/* DJGPP.  */

static int
i386_go32_pc_in_sigtramp (CORE_ADDR pc, char *name)
{
  /* DJGPP doesn't have any special frames for signal handlers.  */
  return 0;
}


/* Generic ELF.  */

void
i386_elf_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  /* We typically use stabs-in-ELF with the DWARF register numbering.  */
  set_gdbarch_stab_reg_to_regnum (gdbarch, i386_dwarf_reg_to_regnum);
}

/* System V Release 4 (SVR4).  */

void
i386_svr4_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* System V Release 4 uses ELF.  */
  i386_elf_init_abi (info, gdbarch);

  /* System V Release 4 has shared libraries.  */
  set_gdbarch_in_solib_call_trampoline (gdbarch, in_plt_section);
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);

  set_gdbarch_pc_in_sigtramp (gdbarch, i386_svr4_pc_in_sigtramp);
  tdep->sigcontext_addr = i386_svr4_sigcontext_addr;
  tdep->sc_pc_offset = 36 + 14 * 4;
  tdep->sc_sp_offset = 36 + 17 * 4;

  tdep->jb_pc_offset = 20;
}

/* DJGPP.  */

static void
i386_go32_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  set_gdbarch_pc_in_sigtramp (gdbarch, i386_go32_pc_in_sigtramp);

  tdep->jb_pc_offset = 36;
}

/* NetWare.  */

static void
i386_nw_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  tdep->jb_pc_offset = 24;
}


/* i386 register groups.  In addition to the normal groups, add "mmx"
   and "sse".  */

static struct reggroup *i386_sse_reggroup;
static struct reggroup *i386_mmx_reggroup;

static void
i386_init_reggroups (void)
{
  i386_sse_reggroup = reggroup_new ("sse", USER_REGGROUP);
  i386_mmx_reggroup = reggroup_new ("mmx", USER_REGGROUP);
}

static void
i386_add_reggroups (struct gdbarch *gdbarch)
{
  reggroup_add (gdbarch, i386_sse_reggroup);
  reggroup_add (gdbarch, i386_mmx_reggroup);
  reggroup_add (gdbarch, general_reggroup);
  reggroup_add (gdbarch, float_reggroup);
  reggroup_add (gdbarch, all_reggroup);
  reggroup_add (gdbarch, save_reggroup);
  reggroup_add (gdbarch, restore_reggroup);
  reggroup_add (gdbarch, vector_reggroup);
  reggroup_add (gdbarch, system_reggroup);
}

int
i386_register_reggroup_p (struct gdbarch *gdbarch, int regnum,
			  struct reggroup *group)
{
  int sse_regnum_p = (i386_sse_regnum_p (gdbarch, regnum)
		      || i386_mxcsr_regnum_p (gdbarch, regnum));
  int fp_regnum_p = (i386_fp_regnum_p (regnum)
		     || i386_fpc_regnum_p (regnum));
  int mmx_regnum_p = (i386_mmx_regnum_p (gdbarch, regnum));

  if (group == i386_mmx_reggroup)
    return mmx_regnum_p;
  if (group == i386_sse_reggroup)
    return sse_regnum_p;
  if (group == vector_reggroup)
    return (mmx_regnum_p || sse_regnum_p);
  if (group == float_reggroup)
    return fp_regnum_p;
  if (group == general_reggroup)
    return (!fp_regnum_p && !mmx_regnum_p && !sse_regnum_p);

  return default_register_reggroup_p (gdbarch, regnum, group);
}


/* Get the ARGIth function argument for the current function.  */

static CORE_ADDR
i386_fetch_pointer_argument (struct frame_info *frame, int argi, 
			     struct type *type)
{
  CORE_ADDR sp = get_frame_register_unsigned  (frame, I386_ESP_REGNUM);
  return read_memory_unsigned_integer (sp + (4 * (argi + 1)), 4);
}


static struct gdbarch *
i386_gdbarch_init (struct gdbarch_info info, struct gdbarch_list *arches)
{
  struct gdbarch_tdep *tdep;
  struct gdbarch *gdbarch;

  /* If there is already a candidate, use it.  */
  arches = gdbarch_list_lookup_by_info (arches, &info);
  if (arches != NULL)
    return arches->gdbarch;

  /* Allocate space for the new architecture.  */
  tdep = XMALLOC (struct gdbarch_tdep);
  gdbarch = gdbarch_alloc (&info, tdep);

  /* General-purpose registers.  */
  tdep->gregset = NULL;
  tdep->gregset_reg_offset = NULL;
  tdep->gregset_num_regs = I386_NUM_GREGS;
  tdep->sizeof_gregset = 0;

  /* Floating-point registers.  */
  tdep->fpregset = NULL;
  tdep->sizeof_fpregset = I387_SIZEOF_FSAVE;

  /* The default settings include the FPU registers, the MMX registers
     and the SSE registers.  This can be overidden for a specific ABI
     by adjusting the members `st0_regnum', `mm0_regnum' and
     `num_xmm_regs' of `struct gdbarch_tdep', otherwise the registers
     will show up in the output of "info all-registers".  Ideally we
     should try to autodetect whether they are available, such that we
     can prevent "info all-registers" from displaying registers that
     aren't available.

     NOTE: kevinb/2003-07-13: ... if it's a choice between printing
     [the SSE registers] always (even when they don't exist) or never
     showing them to the user (even when they do exist), I prefer the
     former over the latter.  */

  tdep->st0_regnum = I386_ST0_REGNUM;

  /* The MMX registers are implemented as pseudo-registers.  Put off
     caclulating the register number for %mm0 until we know the number
     of raw registers.  */
  tdep->mm0_regnum = 0;

  /* I386_NUM_XREGS includes %mxcsr, so substract one.  */
  tdep->num_xmm_regs = I386_NUM_XREGS - 1;

  tdep->jb_pc_offset = -1;
  tdep->struct_return = pcc_struct_return;
  tdep->sigtramp_start = 0;
  tdep->sigtramp_end = 0;
  tdep->sigcontext_addr = NULL;
  tdep->sc_reg_offset = NULL;
  tdep->sc_pc_offset = -1;
  tdep->sc_sp_offset = -1;

  /* The format used for `long double' on almost all i386 targets is
     the i387 extended floating-point format.  In fact, of all targets
     in the GCC 2.95 tree, only OSF/1 does it different, and insists
     on having a `long double' that's not `long' at all.  */
  set_gdbarch_long_double_format (gdbarch, &floatformat_i387_ext);

  /* Although the i387 extended floating-point has only 80 significant
     bits, a `long double' actually takes up 96, probably to enforce
     alignment.  */
  set_gdbarch_long_double_bit (gdbarch, 96);

  /* The default ABI includes general-purpose registers, 
     floating-point registers, and the SSE registers.  */
  set_gdbarch_num_regs (gdbarch, I386_SSE_NUM_REGS);
  set_gdbarch_register_name (gdbarch, i386_register_name);
  set_gdbarch_register_type (gdbarch, i386_register_type);

  /* Register numbers of various important registers.  */
  set_gdbarch_sp_regnum (gdbarch, I386_ESP_REGNUM); /* %esp */
  set_gdbarch_pc_regnum (gdbarch, I386_EIP_REGNUM); /* %eip */
  set_gdbarch_ps_regnum (gdbarch, I386_EFLAGS_REGNUM); /* %eflags */
  set_gdbarch_fp0_regnum (gdbarch, I386_ST0_REGNUM); /* %st(0) */

  /* Use the "default" register numbering scheme for stabs and COFF.  */
  set_gdbarch_stab_reg_to_regnum (gdbarch, i386_stab_reg_to_regnum);
  set_gdbarch_sdb_reg_to_regnum (gdbarch, i386_stab_reg_to_regnum);

  /* Use the DWARF register numbering scheme for DWARF and DWARF 2.  */
  set_gdbarch_dwarf_reg_to_regnum (gdbarch, i386_dwarf_reg_to_regnum);
  set_gdbarch_dwarf2_reg_to_regnum (gdbarch, i386_dwarf_reg_to_regnum);

  /* We don't define ECOFF_REG_TO_REGNUM, since ECOFF doesn't seem to
     be in use on any of the supported i386 targets.  */

  set_gdbarch_print_float_info (gdbarch, i387_print_float_info);

  set_gdbarch_get_longjmp_target (gdbarch, i386_get_longjmp_target);

  /* Call dummy code.  */
  set_gdbarch_push_dummy_call (gdbarch, i386_push_dummy_call);

  set_gdbarch_convert_register_p (gdbarch, i386_convert_register_p);
  set_gdbarch_register_to_value (gdbarch,  i386_register_to_value);
  set_gdbarch_value_to_register (gdbarch, i386_value_to_register);

  set_gdbarch_return_value (gdbarch, i386_return_value);

  set_gdbarch_skip_prologue (gdbarch, i386_skip_prologue);

  /* Stack grows downward.  */
  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);

  set_gdbarch_breakpoint_from_pc (gdbarch, i386_breakpoint_from_pc);
  set_gdbarch_decr_pc_after_break (gdbarch, 1);

  set_gdbarch_frame_args_skip (gdbarch, 8);
  set_gdbarch_pc_in_sigtramp (gdbarch, i386_pc_in_sigtramp);

  /* Wire in the MMX registers.  */
  set_gdbarch_num_pseudo_regs (gdbarch, i386_num_mmx_regs);
  set_gdbarch_pseudo_register_read (gdbarch, i386_pseudo_register_read);
  set_gdbarch_pseudo_register_write (gdbarch, i386_pseudo_register_write);

  set_gdbarch_print_insn (gdbarch, i386_print_insn);

  set_gdbarch_unwind_dummy_id (gdbarch, i386_unwind_dummy_id);

  set_gdbarch_unwind_pc (gdbarch, i386_unwind_pc);

  /* Add the i386 register groups.  */
  i386_add_reggroups (gdbarch);
  set_gdbarch_register_reggroup_p (gdbarch, i386_register_reggroup_p);

  /* Helper for function argument information.  */
  set_gdbarch_fetch_pointer_argument (gdbarch, i386_fetch_pointer_argument);

  /* Hook in the DWARF CFI frame unwinder.  */
  frame_unwind_append_sniffer (gdbarch, dwarf2_frame_sniffer);

  frame_base_set_default (gdbarch, &i386_frame_base);

  /* Hook in ABI-specific overrides, if they have been registered.  */
  gdbarch_init_osabi (info, gdbarch);

  frame_unwind_append_sniffer (gdbarch, i386_sigtramp_frame_sniffer);
  frame_unwind_append_sniffer (gdbarch, i386_frame_sniffer);

  /* If we have a register mapping, enable the generic core file
     support, unless it has already been enabled.  */
  if (tdep->gregset_reg_offset
      && !gdbarch_regset_from_core_section_p (gdbarch))
    set_gdbarch_regset_from_core_section (gdbarch,
					  i386_regset_from_core_section);

  /* Unless support for MMX has been disabled, make %mm0 the first
     pseudo-register.  */
  if (tdep->mm0_regnum == 0)
    tdep->mm0_regnum = gdbarch_num_regs (gdbarch);

  return gdbarch;
}

static enum gdb_osabi
i386_coff_osabi_sniffer (bfd *abfd)
{
  if (strcmp (bfd_get_target (abfd), "coff-go32-exe") == 0
      || strcmp (bfd_get_target (abfd), "coff-go32") == 0)
    return GDB_OSABI_GO32;

  return GDB_OSABI_UNKNOWN;
}

static enum gdb_osabi
i386_nlm_osabi_sniffer (bfd *abfd)
{
  return GDB_OSABI_NETWARE;
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_i386_tdep (void);

void
_initialize_i386_tdep (void)
{
  register_gdbarch_init (bfd_arch_i386, i386_gdbarch_init);

  /* Add the variable that controls the disassembly flavor.  */
  {
    struct cmd_list_element *new_cmd;

    new_cmd = add_set_enum_cmd ("disassembly-flavor", no_class,
				valid_flavors,
				&disassembly_flavor,
				"\
Set the disassembly flavor, the valid values are \"att\" and \"intel\", \
and the default value is \"att\".",
				&setlist);
    add_show_from_set (new_cmd, &showlist);
  }

  /* Add the variable that controls the convention for returning
     structs.  */
  {
    struct cmd_list_element *new_cmd;

    new_cmd = add_set_enum_cmd ("struct-convention", no_class,
				valid_conventions,
				&struct_convention, "\
Set the convention for returning small structs, valid values \
are \"default\", \"pcc\" and \"reg\", and the default value is \"default\".",
                                &setlist);
    add_show_from_set (new_cmd, &showlist);
  }

  gdbarch_register_osabi_sniffer (bfd_arch_i386, bfd_target_coff_flavour,
				  i386_coff_osabi_sniffer);
  gdbarch_register_osabi_sniffer (bfd_arch_i386, bfd_target_nlm_flavour,
				  i386_nlm_osabi_sniffer);

  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_SVR4,
			  i386_svr4_init_abi);
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_GO32,
			  i386_go32_init_abi);
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_NETWARE,
			  i386_nw_init_abi);

  /* Initialize the i386 specific register groups.  */
  i386_init_reggroups ();
}
