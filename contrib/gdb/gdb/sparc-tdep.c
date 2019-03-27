/* Target-dependent code for SPARC.

   Copyright 2003, 2004 Free Software Foundation, Inc.

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
#include "dis-asm.h"
#include "floatformat.h"
#include "frame.h"
#include "frame-base.h"
#include "frame-unwind.h"
#include "gdbcore.h"
#include "gdbtypes.h"
#include "inferior.h"
#include "symtab.h"
#include "objfiles.h"
#include "osabi.h"
#include "regcache.h"
#include "target.h"
#include "value.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "sparc-tdep.h"

struct regset;

/* This file implements the The SPARC 32-bit ABI as defined by the
   section "Low-Level System Information" of the SPARC Compliance
   Definition (SCD) 2.4.1, which is the 32-bit System V psABI for
   SPARC.  The SCD lists changes with respect to the origional 32-bit
   psABI as defined in the "System V ABI, SPARC Processor
   Supplement".

   Note that if we talk about SunOS, we mean SunOS 4.x, which was
   BSD-based, which is sometimes (retroactively?) referred to as
   Solaris 1.x.  If we talk about Solaris we mean Solaris 2.x and
   above (Solaris 7, 8 and 9 are nothing but Solaris 2.7, 2.8 and 2.9
   suffering from severe version number inflation).  Solaris 2.x is
   also known as SunOS 5.x, since that's what uname(1) says.  Solaris
   2.x is SVR4-based.  */

/* Please use the sparc32_-prefix for 32-bit specific code, the
   sparc64_-prefix for 64-bit specific code and the sparc_-prefix for
   code that can handle both.  The 64-bit specific code lives in
   sparc64-tdep.c; don't add any here.  */

/* The SPARC Floating-Point Quad-Precision format is similar to
   big-endian IA-64 Quad-recision format.  */
#define floatformat_sparc_quad floatformat_ia64_quad_big

/* The stack pointer is offset from the stack frame by a BIAS of 2047
   (0x7ff) for 64-bit code.  BIAS is likely to be defined on SPARC
   hosts, so undefine it first.  */
#undef BIAS
#define BIAS 2047

/* Macros to extract fields from SPARC instructions.  */
#define X_OP(i) (((i) >> 30) & 0x3)
#define X_RD(i) (((i) >> 25) & 0x1f)
#define X_A(i) (((i) >> 29) & 1)
#define X_COND(i) (((i) >> 25) & 0xf)
#define X_OP2(i) (((i) >> 22) & 0x7)
#define X_IMM22(i) ((i) & 0x3fffff)
#define X_OP3(i) (((i) >> 19) & 0x3f)
#define X_I(i) (((i) >> 13) & 1)
/* Sign extension macros.  */
#define X_DISP22(i) ((X_IMM22 (i) ^ 0x200000) - 0x200000)
#define X_DISP19(i) ((((i) & 0x7ffff) ^ 0x40000) - 0x40000)

/* Fetch the instruction at PC.  Instructions are always big-endian
   even if the processor operates in little-endian mode.  */

unsigned long
sparc_fetch_instruction (CORE_ADDR pc)
{
  unsigned char buf[4];
  unsigned long insn;
  int i;

  /* If we can't read the instruction at PC, return zero.  */
  if (target_read_memory (pc, buf, sizeof (buf)))
    return 0;

  insn = 0;
  for (i = 0; i < sizeof (buf); i++)
    insn = (insn << 8) | buf[i];
  return insn;
}


/* OpenBSD/sparc includes StackGhost, which according to the author's
   website http://stackghost.cerias.purdue.edu "... transparently and
   automatically protects applications' stack frames; more
   specifically, it guards the return pointers.  The protection
   mechanisms require no application source or binary modification and
   imposes only a negligible performance penalty."

   The same website provides the following description of how
   StackGhost works:

   "StackGhost interfaces with the kernel trap handler that would
   normally write out registers to the stack and the handler that
   would read them back in.  By XORing a cookie into the
   return-address saved in the user stack when it is actually written
   to the stack, and then XOR it out when the return-address is pulled
   from the stack, StackGhost can cause attacker corrupted return
   pointers to behave in a manner the attacker cannot predict.
   StackGhost can also use several unused bits in the return pointer
   to detect a smashed return pointer and abort the process."

   For GDB this means that whenever we're reading %i7 from a stack
   frame's window save area, we'll have to XOR the cookie.

   More information on StackGuard can be found on in:

   Mike Frantzen and Mike Shuey. "StackGhost: Hardware Facilitated
   Stack Protection."  2001.  Published in USENIX Security Symposium
   '01.  */

/* Fetch StackGhost Per-Process XOR cookie.  */

ULONGEST
sparc_fetch_wcookie (void)
{
  struct target_ops *ops = &current_target;
  char buf[8];
  int len;

  len = target_read_partial (ops, TARGET_OBJECT_WCOOKIE, NULL, buf, 0, 8);
  if (len == -1)
    return 0;

  /* We should have either an 32-bit or an 64-bit cookie.  */
  gdb_assert (len == 4 || len == 8);

  return extract_unsigned_integer (buf, len);
}


/* Return the contents if register REGNUM as an address.  */

static CORE_ADDR
sparc_address_from_register (int regnum)
{
  ULONGEST addr;

  regcache_cooked_read_unsigned (current_regcache, regnum, &addr);
  return addr;
}


/* The functions on this page are intended to be used to classify
   function arguments.  */

/* Check whether TYPE is "Integral or Pointer".  */

static int
sparc_integral_or_pointer_p (const struct type *type)
{
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_INT:
    case TYPE_CODE_BOOL:
    case TYPE_CODE_CHAR:
    case TYPE_CODE_ENUM:
    case TYPE_CODE_RANGE:
      {
	/* We have byte, half-word, word and extended-word/doubleword
           integral types.  The doubleword is an extension to the
           origional 32-bit ABI by the SCD 2.4.x.  */
	int len = TYPE_LENGTH (type);
	return (len == 1 || len == 2 || len == 4 || len == 8);
      }
      return 1;
    case TYPE_CODE_PTR:
    case TYPE_CODE_REF:
      {
	/* Allow either 32-bit or 64-bit pointers.  */
	int len = TYPE_LENGTH (type);
	return (len == 4 || len == 8);
      }
      return 1;
    default:
      break;
    }

  return 0;
}

/* Check whether TYPE is "Floating".  */

static int
sparc_floating_p (const struct type *type)
{
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_FLT:
      {
	int len = TYPE_LENGTH (type);
	return (len == 4 || len == 8 || len == 16);
      }
    default:
      break;
    }

  return 0;
}

/* Check whether TYPE is "Structure or Union".  */

static int
sparc_structure_or_union_p (const struct type *type)
{
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      return 1;
    default:
      break;
    }

  return 0;
}

/* Register information.  */

static const char *sparc32_register_names[] =
{
  "g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
  "o0", "o1", "o2", "o3", "o4", "o5", "sp", "o7",
  "l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
  "i0", "i1", "i2", "i3", "i4", "i5", "fp", "i7",

  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
  "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
  "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
  "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",

  "y", "psr", "wim", "tbr", "pc", "npc", "fsr", "csr"
};

/* Total number of registers.  */
#define SPARC32_NUM_REGS ARRAY_SIZE (sparc32_register_names)

/* We provide the aliases %d0..%d30 for the floating registers as
   "psuedo" registers.  */

static const char *sparc32_pseudo_register_names[] =
{
  "d0", "d2", "d4", "d6", "d8", "d10", "d12", "d14",
  "d16", "d18", "d20", "d22", "d24", "d26", "d28", "d30"
};

/* Total number of pseudo registers.  */
#define SPARC32_NUM_PSEUDO_REGS ARRAY_SIZE (sparc32_pseudo_register_names)

/* Return the name of register REGNUM.  */

static const char *
sparc32_register_name (int regnum)
{
  if (regnum >= 0 && regnum < SPARC32_NUM_REGS)
    return sparc32_register_names[regnum];

  if (regnum < SPARC32_NUM_REGS + SPARC32_NUM_PSEUDO_REGS)
    return sparc32_pseudo_register_names[regnum - SPARC32_NUM_REGS];

  return NULL;
}

/* Return the GDB type object for the "standard" data type of data in
   register REGNUM. */

static struct type *
sparc32_register_type (struct gdbarch *gdbarch, int regnum)
{
  if (regnum >= SPARC_F0_REGNUM && regnum <= SPARC_F31_REGNUM)
    return builtin_type_float;

  if (regnum >= SPARC32_D0_REGNUM && regnum <= SPARC32_D30_REGNUM)
    return builtin_type_double;

  if (regnum == SPARC_SP_REGNUM || regnum == SPARC_FP_REGNUM)
    return builtin_type_void_data_ptr;

  if (regnum == SPARC32_PC_REGNUM || regnum == SPARC32_NPC_REGNUM)
    return builtin_type_void_func_ptr;

  return builtin_type_int32;
}

static void
sparc32_pseudo_register_read (struct gdbarch *gdbarch,
			      struct regcache *regcache,
			      int regnum, void *buf)
{
  gdb_assert (regnum >= SPARC32_D0_REGNUM && regnum <= SPARC32_D30_REGNUM);

  regnum = SPARC_F0_REGNUM + 2 * (regnum - SPARC32_D0_REGNUM);
  regcache_raw_read (regcache, regnum, buf);
  regcache_raw_read (regcache, regnum + 1, ((char *)buf) + 4);
}

static void
sparc32_pseudo_register_write (struct gdbarch *gdbarch,
			       struct regcache *regcache,
			       int regnum, const void *buf)
{
  gdb_assert (regnum >= SPARC32_D0_REGNUM && regnum <= SPARC32_D30_REGNUM);

  regnum = SPARC_F0_REGNUM + 2 * (regnum - SPARC32_D0_REGNUM);
  regcache_raw_write (regcache, regnum, buf);
  regcache_raw_write (regcache, regnum + 1, ((const char *)buf) + 4);
}


static CORE_ADDR
sparc32_push_dummy_code (struct gdbarch *gdbarch, CORE_ADDR sp,
			 CORE_ADDR funcaddr, int using_gcc,
			 struct value **args, int nargs,
			 struct type *value_type,
			 CORE_ADDR *real_pc, CORE_ADDR *bp_addr)
{
  *bp_addr = sp - 4;
  *real_pc = funcaddr;

  if (using_struct_return (value_type, using_gcc))
    {
      char buf[4];

      /* This is an UNIMP instruction.  */
      store_unsigned_integer (buf, 4, TYPE_LENGTH (value_type) & 0x1fff);
      write_memory (sp - 8, buf, 4);
      return sp - 8;
    }

  return sp - 4;
}

static CORE_ADDR
sparc32_store_arguments (struct regcache *regcache, int nargs,
			 struct value **args, CORE_ADDR sp,
			 int struct_return, CORE_ADDR struct_addr)
{
  /* Number of words in the "parameter array".  */
  int num_elements = 0;
  int element = 0;
  int i;

  for (i = 0; i < nargs; i++)
    {
      struct type *type = VALUE_TYPE (args[i]);
      int len = TYPE_LENGTH (type);

      if (sparc_structure_or_union_p (type)
	  || (sparc_floating_p (type) && len == 16))
	{
	  /* Structure, Union and Quad-Precision Arguments.  */
	  sp -= len;

	  /* Use doubleword alignment for these values.  That's always
             correct, and wasting a few bytes shouldn't be a problem.  */
	  sp &= ~0x7;

	  write_memory (sp, VALUE_CONTENTS (args[i]), len);
	  args[i] = value_from_pointer (lookup_pointer_type (type), sp);
	  num_elements++;
	}
      else if (sparc_floating_p (type))
	{
	  /* Floating arguments.  */
	  gdb_assert (len == 4 || len == 8);
	  num_elements += (len / 4);
	}
      else
	{
	  /* Integral and pointer arguments.  */
	  gdb_assert (sparc_integral_or_pointer_p (type));

	  if (len < 4)
	    args[i] = value_cast (builtin_type_int32, args[i]);
	  num_elements += ((len + 3) / 4);
	}
    }

  /* Always allocate at least six words.  */
  sp -= max (6, num_elements) * 4;

  /* The psABI says that "Software convention requires space for the
     struct/union return value pointer, even if the word is unused."  */
  sp -= 4;

  /* The psABI says that "Although software convention and the
     operating system require every stack frame to be doubleword
     aligned."  */
  sp &= ~0x7;

  for (i = 0; i < nargs; i++)
    {
      char *valbuf = VALUE_CONTENTS (args[i]);
      struct type *type = VALUE_TYPE (args[i]);
      int len = TYPE_LENGTH (type);

      gdb_assert (len == 4 || len == 8);

      if (element < 6)
	{
	  int regnum = SPARC_O0_REGNUM + element;

	  regcache_cooked_write (regcache, regnum, valbuf);
	  if (len > 4 && element < 5)
	    regcache_cooked_write (regcache, regnum + 1, valbuf + 4);
	}

      /* Always store the argument in memory.  */
      write_memory (sp + 4 + element * 4, valbuf, len);
      element += len / 4;
    }

  gdb_assert (element == num_elements);

  if (struct_return)
    {
      char buf[4];

      store_unsigned_integer (buf, 4, struct_addr);
      write_memory (sp, buf, 4);
    }

  return sp;
}

static CORE_ADDR
sparc32_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
			 struct regcache *regcache, CORE_ADDR bp_addr,
			 int nargs, struct value **args, CORE_ADDR sp,
			 int struct_return, CORE_ADDR struct_addr)
{
  CORE_ADDR call_pc = (struct_return ? (bp_addr - 12) : (bp_addr - 8));

  /* Set return address.  */
  regcache_cooked_write_unsigned (regcache, SPARC_O7_REGNUM, call_pc);

  /* Set up function arguments.  */
  sp = sparc32_store_arguments (regcache, nargs, args, sp,
				struct_return, struct_addr);

  /* Allocate the 16-word window save area.  */
  sp -= 16 * 4;

  /* Stack should be doubleword aligned at this point.  */
  gdb_assert (sp % 8 == 0);

  /* Finally, update the stack pointer.  */
  regcache_cooked_write_unsigned (regcache, SPARC_SP_REGNUM, sp);

  return sp;
}


/* Use the program counter to determine the contents and size of a
   breakpoint instruction.  Return a pointer to a string of bytes that
   encode a breakpoint instruction, store the length of the string in
   *LEN and optionally adjust *PC to point to the correct memory
   location for inserting the breakpoint.  */
   
static const unsigned char *
sparc_breakpoint_from_pc (CORE_ADDR *pc, int *len)
{
  static unsigned char break_insn[] = { 0x91, 0xd0, 0x20, 0x01 };

  *len = sizeof (break_insn);
  return break_insn;
}


/* Allocate and initialize a frame cache.  */

static struct sparc_frame_cache *
sparc_alloc_frame_cache (void)
{
  struct sparc_frame_cache *cache;
  int i;

  cache = FRAME_OBSTACK_ZALLOC (struct sparc_frame_cache);

  /* Base address.  */
  cache->base = 0;
  cache->pc = 0;

  /* Frameless until proven otherwise.  */
  cache->frameless_p = 1;

  cache->struct_return_p = 0;

  return cache;
}

CORE_ADDR
sparc_analyze_prologue (CORE_ADDR pc, CORE_ADDR current_pc,
			struct sparc_frame_cache *cache)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  unsigned long insn;
  int offset = 0;
  int dest = -1;

  if (current_pc <= pc)
    return current_pc;

  /* We have to handle to "Procedure Linkage Table" (PLT) special.  On
     SPARC the linker usually defines a symbol (typically
     _PROCEDURE_LINKAGE_TABLE_) at the start of the .plt section.
     This symbol makes us end up here with PC pointing at the start of
     the PLT and CURRENT_PC probably pointing at a PLT entry.  If we
     would do our normal prologue analysis, we would probably conclude
     that we've got a frame when in reality we don't, since the
     dynamic linker patches up the first PLT with some code that
     starts with a SAVE instruction.  Patch up PC such that it points
     at the start of our PLT entry.  */
  if (tdep->plt_entry_size > 0 && in_plt_section (current_pc, NULL))
    pc = current_pc - ((current_pc - pc) % tdep->plt_entry_size);

  insn = sparc_fetch_instruction (pc);

  /* Recognize a SETHI insn and record its destination.  */
  if (X_OP (insn) == 0 && X_OP2 (insn) == 0x04)
    {
      dest = X_RD (insn);
      offset += 4;

      insn = sparc_fetch_instruction (pc + 4);
    }

  /* Allow for an arithmetic operation on DEST or %g1.  */
  if (X_OP (insn) == 2 && X_I (insn)
      && (X_RD (insn) == 1 || X_RD (insn) == dest))
    {
      offset += 4;

      insn = sparc_fetch_instruction (pc + 8);
    }

  /* Check for the SAVE instruction that sets up the frame.  */
  if (X_OP (insn) == 2 && X_OP3 (insn) == 0x3c)
    {
      cache->frameless_p = 0;
      return pc + offset + 4;
    }

  return pc;
}

static CORE_ADDR
sparc_unwind_pc (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  return frame_unwind_register_unsigned (next_frame, tdep->pc_regnum);
}

/* Return PC of first real instruction of the function starting at
   START_PC.  */

static CORE_ADDR
sparc32_skip_prologue (CORE_ADDR start_pc)
{
  struct symtab_and_line sal;
  CORE_ADDR func_start, func_end;
  struct sparc_frame_cache cache;

  /* This is the preferred method, find the end of the prologue by
     using the debugging information.  */
  if (find_pc_partial_function (start_pc, NULL, &func_start, &func_end))
    {
      sal = find_pc_line (func_start, 0);

      if (sal.end < func_end
	  && start_pc <= sal.end)
	return sal.end;
    }

  return sparc_analyze_prologue (start_pc, 0xffffffffUL, &cache);
}

/* Normal frames.  */

struct sparc_frame_cache *
sparc_frame_cache (struct frame_info *next_frame, void **this_cache)
{
  struct sparc_frame_cache *cache;

  if (*this_cache)
    return *this_cache;

  cache = sparc_alloc_frame_cache ();
  *this_cache = cache;

  cache->pc = frame_func_unwind (next_frame);
  if (cache->pc != 0)
    {
      CORE_ADDR addr_in_block = frame_unwind_address_in_block (next_frame);
      sparc_analyze_prologue (cache->pc, addr_in_block, cache);
    }

  if (cache->frameless_p)
    {
      /* This function is frameless, so %fp (%i6) holds the frame
         pointer for our calling frame.  Use %sp (%o6) as this frame's
         base address.  */
      cache->base =
	frame_unwind_register_unsigned (next_frame, SPARC_SP_REGNUM);
    }
  else
    {
      /* For normal frames, %fp (%i6) holds the frame pointer, the
         base address for the current stack frame.  */
      cache->base =
	frame_unwind_register_unsigned (next_frame, SPARC_FP_REGNUM);
    }

  return cache;
}

struct sparc_frame_cache *
sparc32_frame_cache (struct frame_info *next_frame, void **this_cache)
{
  struct sparc_frame_cache *cache;
  struct symbol *sym;

  if (*this_cache)
    return *this_cache;

  cache = sparc_frame_cache (next_frame, this_cache);

  sym = find_pc_function (cache->pc);
  if (sym)
    {
      struct type *type = check_typedef (SYMBOL_TYPE (sym));
      enum type_code code = TYPE_CODE (type);

      if (code == TYPE_CODE_FUNC || code == TYPE_CODE_METHOD)
	{
	  type = check_typedef (TYPE_TARGET_TYPE (type));
	  if (sparc_structure_or_union_p (type)
	      || (sparc_floating_p (type) && TYPE_LENGTH (type) == 16))
	    cache->struct_return_p = 1;
	}
    }

  return cache;
}

static void
sparc32_frame_this_id (struct frame_info *next_frame, void **this_cache,
		       struct frame_id *this_id)
{
  struct sparc_frame_cache *cache =
    sparc32_frame_cache (next_frame, this_cache);

  /* This marks the outermost frame.  */
  if (cache->base == 0)
    return;

  (*this_id) = frame_id_build (cache->base, cache->pc);
}

static void
sparc32_frame_prev_register (struct frame_info *next_frame, void **this_cache,
			     int regnum, int *optimizedp,
			     enum lval_type *lvalp, CORE_ADDR *addrp,
			     int *realnump, void *valuep)
{
  struct sparc_frame_cache *cache =
    sparc32_frame_cache (next_frame, this_cache);

  if (regnum == SPARC32_PC_REGNUM || regnum == SPARC32_NPC_REGNUM)
    {
      *optimizedp = 0;
      *lvalp = not_lval;
      *addrp = 0;
      *realnump = -1;
      if (valuep)
	{
	  CORE_ADDR pc = (regnum == SPARC32_NPC_REGNUM) ? 4 : 0;

	  /* If this functions has a Structure, Union or
             Quad-Precision return value, we have to skip the UNIMP
             instruction that encodes the size of the structure.  */
	  if (cache->struct_return_p)
	    pc += 4;

	  regnum = cache->frameless_p ? SPARC_O7_REGNUM : SPARC_I7_REGNUM;
	  pc += frame_unwind_register_unsigned (next_frame, regnum) + 8;
	  store_unsigned_integer (valuep, 4, pc);
	}
      return;
    }

  /* Handle StackGhost.  */
  {
    ULONGEST wcookie = sparc_fetch_wcookie ();

    if (wcookie != 0 && !cache->frameless_p && regnum == SPARC_I7_REGNUM)
      {
	*optimizedp = 0;
	*lvalp = not_lval;
	*addrp = 0;
	*realnump = -1;
	if (valuep)
	  {
	    CORE_ADDR addr = cache->base + (regnum - SPARC_L0_REGNUM) * 4;
	    ULONGEST i7;

	    /* Read the value in from memory.  */
	    i7 = get_frame_memory_unsigned (next_frame, addr, 4);
	    store_unsigned_integer (valuep, 4, i7 ^ wcookie);
	  }
	return;
      }
  }

  /* The previous frame's `local' and `in' registers have been saved
     in the register save area.  */
  if (!cache->frameless_p
      && regnum >= SPARC_L0_REGNUM && regnum <= SPARC_I7_REGNUM)
    {
      *optimizedp = 0;
      *lvalp = lval_memory;
      *addrp = cache->base + (regnum - SPARC_L0_REGNUM) * 4;
      *realnump = -1;
      if (valuep)
	{
	  struct gdbarch *gdbarch = get_frame_arch (next_frame);

	  /* Read the value in from memory.  */
	  read_memory (*addrp, valuep, register_size (gdbarch, regnum));
	}
      return;
    }

  /* The previous frame's `out' registers are accessable as the
     current frame's `in' registers.  */
  if (!cache->frameless_p
      && regnum >= SPARC_O0_REGNUM && regnum <= SPARC_O7_REGNUM)
    regnum += (SPARC_I0_REGNUM - SPARC_O0_REGNUM);

  frame_register_unwind (next_frame, regnum,
			 optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind sparc32_frame_unwind =
{
  NORMAL_FRAME,
  sparc32_frame_this_id,
  sparc32_frame_prev_register
};

static const struct frame_unwind *
sparc32_frame_sniffer (struct frame_info *next_frame)
{
  return &sparc32_frame_unwind;
}


static CORE_ADDR
sparc32_frame_base_address (struct frame_info *next_frame, void **this_cache)
{
  struct sparc_frame_cache *cache =
    sparc32_frame_cache (next_frame, this_cache);

  return cache->base;
}

static const struct frame_base sparc32_frame_base =
{
  &sparc32_frame_unwind,
  sparc32_frame_base_address,
  sparc32_frame_base_address,
  sparc32_frame_base_address
};

static struct frame_id
sparc_unwind_dummy_id (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  CORE_ADDR sp;

  sp = frame_unwind_register_unsigned (next_frame, SPARC_SP_REGNUM);
  return frame_id_build (sp, frame_pc_unwind (next_frame));
}


/* Extract from an array REGBUF containing the (raw) register state, a
   function return value of TYPE, and copy that into VALBUF.  */

static void
sparc32_extract_return_value (struct type *type, struct regcache *regcache,
			      void *valbuf)
{
  int len = TYPE_LENGTH (type);
  char buf[8];

  gdb_assert (!sparc_structure_or_union_p (type));
  gdb_assert (!(sparc_floating_p (type) && len == 16));

  if (sparc_floating_p (type))
    {
      /* Floating return values.  */
      regcache_cooked_read (regcache, SPARC_F0_REGNUM, buf);
      if (len > 4)
	regcache_cooked_read (regcache, SPARC_F1_REGNUM, buf + 4);
      memcpy (valbuf, buf, len);
    }
  else
    {
      /* Integral and pointer return values.  */
      gdb_assert (sparc_integral_or_pointer_p (type));

      regcache_cooked_read (regcache, SPARC_O0_REGNUM, buf);
      if (len > 4)
	{
	  regcache_cooked_read (regcache, SPARC_O1_REGNUM, buf + 4);
	  gdb_assert (len == 8);
	  memcpy (valbuf, buf, 8);
	}
      else
	{
	  /* Just stripping off any unused bytes should preserve the
	     signed-ness just fine.  */
	  memcpy (valbuf, buf + 4 - len, len);
	}
    }
}

/* Write into the appropriate registers a function return value stored
   in VALBUF of type TYPE.  */

static void
sparc32_store_return_value (struct type *type, struct regcache *regcache,
			    const void *valbuf)
{
  int len = TYPE_LENGTH (type);
  char buf[8];

  gdb_assert (!sparc_structure_or_union_p (type));
  gdb_assert (!(sparc_floating_p (type) && len == 16));

  if (sparc_floating_p (type))
    {
      /* Floating return values.  */
      memcpy (buf, valbuf, len);
      regcache_cooked_write (regcache, SPARC_F0_REGNUM, buf);
      if (len > 4)
	regcache_cooked_write (regcache, SPARC_F1_REGNUM, buf + 4);
    }
  else
    {
      /* Integral and pointer return values.  */
      gdb_assert (sparc_integral_or_pointer_p (type));

      if (len > 4)
	{
	  gdb_assert (len == 8);
	  memcpy (buf, valbuf, 8);
	  regcache_cooked_write (regcache, SPARC_O1_REGNUM, buf + 4);
	}
      else
	{
	  /* ??? Do we need to do any sign-extension here?  */
	  memcpy (buf + 4 - len, valbuf, len);
	}
      regcache_cooked_write (regcache, SPARC_O0_REGNUM, buf);
    }
}

static enum return_value_convention
sparc32_return_value (struct gdbarch *gdbarch, struct type *type,
		      struct regcache *regcache, void *readbuf,
		      const void *writebuf)
{
  if (sparc_structure_or_union_p (type)
      || (sparc_floating_p (type) && TYPE_LENGTH (type) == 16))
    return RETURN_VALUE_STRUCT_CONVENTION;

  if (readbuf)
    sparc32_extract_return_value (type, regcache, readbuf);
  if (writebuf)
    sparc32_store_return_value (type, regcache, writebuf);

  return RETURN_VALUE_REGISTER_CONVENTION;
}

#if 0
/* NOTE: cagney/2004-01-17: For the moment disable this method.  The
   architecture and CORE-gdb will need new code (and a replacement for
   DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS) before this can be made to
   work robustly.  Here is a possible function signature: */
/* NOTE: cagney/2004-01-17: So far only the 32-bit SPARC ABI has been
   identifed as having a way to robustly recover the address of a
   struct-convention return-value (after the function has returned).
   For all other ABIs so far examined, the calling convention makes no
   guarenteed that the register containing the return-value will be
   preserved and hence that the return-value's address can be
   recovered.  */
/* Extract from REGCACHE, which contains the (raw) register state, the
   address in which a function should return its structure value, as a
   CORE_ADDR.  */

static CORE_ADDR
sparc32_extract_struct_value_address (struct regcache *regcache)
{
  ULONGEST sp;

  regcache_cooked_read_unsigned (regcache, SPARC_SP_REGNUM, &sp);
  return read_memory_unsigned_integer (sp + 64, 4);
}
#endif

static int
sparc32_stabs_argument_has_addr (struct gdbarch *gdbarch, struct type *type)
{
  return (sparc_structure_or_union_p (type)
	  || (sparc_floating_p (type) && TYPE_LENGTH (type) == 16));
}


/* The SPARC Architecture doesn't have hardware single-step support,
   and most operating systems don't implement it either, so we provide
   software single-step mechanism.  */

static CORE_ADDR
sparc_analyze_control_transfer (CORE_ADDR pc, CORE_ADDR *npc)
{
  unsigned long insn = sparc_fetch_instruction (pc);
  int conditional_p = X_COND (insn) & 0x7;
  int branch_p = 0;
  long offset = 0;			/* Must be signed for sign-extend.  */

  if (X_OP (insn) == 0 && X_OP2 (insn) == 3 && (insn & 0x1000000) == 0)
    {
      /* Branch on Integer Register with Prediction (BPr).  */
      branch_p = 1;
      conditional_p = 1;
    }
  else if (X_OP (insn) == 0 && X_OP2 (insn) == 6)
    {
      /* Branch on Floating-Point Condition Codes (FBfcc).  */
      branch_p = 1;
      offset = 4 * X_DISP22 (insn);
    }
  else if (X_OP (insn) == 0 && X_OP2 (insn) == 5)
    {
      /* Branch on Floating-Point Condition Codes with Prediction
         (FBPfcc).  */
      branch_p = 1;
      offset = 4 * X_DISP19 (insn);
    }
  else if (X_OP (insn) == 0 && X_OP2 (insn) == 2)
    {
      /* Branch on Integer Condition Codes (Bicc).  */
      branch_p = 1;
      offset = 4 * X_DISP22 (insn);
    }
  else if (X_OP (insn) == 0 && X_OP2 (insn) == 1)
    {
      /* Branch on Integer Condition Codes with Prediction (BPcc).  */
      branch_p = 1;
      offset = 4 * X_DISP19 (insn);
    }

  /* FIXME: Handle DONE and RETRY instructions.  */

  /* FIXME: Handle the Trap instruction.  */

  if (branch_p)
    {
      if (conditional_p)
	{
	  /* For conditional branches, return nPC + 4 iff the annul
	     bit is 1.  */
	  return (X_A (insn) ? *npc + 4 : 0);
	}
      else
	{
	  /* For unconditional branches, return the target if its
	     specified condition is "always" and return nPC + 4 if the
	     condition is "never".  If the annul bit is 1, set *NPC to
	     zero.  */
	  if (X_COND (insn) == 0x0)
	    pc = *npc, offset = 4;
	  if (X_A (insn))
	    *npc = 0;

	  gdb_assert (offset != 0);
	  return pc + offset;
	}
    }

  return 0;
}

void
sparc_software_single_step (enum target_signal sig, int insert_breakpoints_p)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  static CORE_ADDR npc, nnpc;
  static char npc_save[4], nnpc_save[4];

  if (insert_breakpoints_p)
    {
      CORE_ADDR pc;

      pc = sparc_address_from_register (tdep->pc_regnum);
      npc = sparc_address_from_register (tdep->npc_regnum);

      /* Analyze the instruction at PC.  */
      nnpc = sparc_analyze_control_transfer (pc, &npc);
      if (npc != 0)
	target_insert_breakpoint (npc, npc_save);
      if (nnpc != 0)
	target_insert_breakpoint (nnpc, nnpc_save);

      /* Assert that we have set at least one breakpoint, and that
         they're not set at the same spot.  */
      gdb_assert (npc != 0 || nnpc != 0);
      gdb_assert (nnpc != npc);
    }
  else
    {
      if (npc != 0)
	target_remove_breakpoint (npc, npc_save);
      if (nnpc != 0)
	target_remove_breakpoint (nnpc, nnpc_save);
    }
}

static void
sparc_write_pc (CORE_ADDR pc, ptid_t ptid)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  write_register_pid (tdep->pc_regnum, pc, ptid);
  write_register_pid (tdep->npc_regnum, pc + 4, ptid);
}

/* Unglobalize NAME.  */

char *
sparc_stabs_unglobalize_name (char *name)
{
  /* The Sun compilers (Sun ONE Studio, Forte Developer, Sun WorkShop,
     SunPRO) convert file static variables into global values, a
     process known as globalization.  In order to do this, the
     compiler will create a unique prefix and prepend it to each file
     static variable.  For static variables within a function, this
     globalization prefix is followed by the function name (nested
     static variables within a function are supposed to generate a
     warning message, and are left alone).  The procedure is
     documented in the Stabs Interface Manual, which is distrubuted
     with the compilers, although version 4.0 of the manual seems to
     be incorrect in some places, at least for SPARC.  The
     globalization prefix is encoded into an N_OPT stab, with the form
     "G=<prefix>".  The globalization prefix always seems to start
     with a dollar sign '$'; a dot '.' is used as a seperator.  So we
     simply strip everything up until the last dot.  */

  if (name[0] == '$')
    {
      char *p = strrchr (name, '.');
      if (p)
	return p + 1;
    }

  return name;
}


/* Return the appropriate register set for the core section identified
   by SECT_NAME and SECT_SIZE.  */

const struct regset *
sparc_regset_from_core_section (struct gdbarch *gdbarch,
				const char *sect_name, size_t sect_size)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  if (strcmp (sect_name, ".reg") == 0 && sect_size >= tdep->sizeof_gregset)
    return tdep->gregset;

  if (strcmp (sect_name, ".reg2") == 0 && sect_size >= tdep->sizeof_fpregset)
    return tdep->fpregset;

  return NULL;
}


static struct gdbarch *
sparc32_gdbarch_init (struct gdbarch_info info, struct gdbarch_list *arches)
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

  tdep->pc_regnum = SPARC32_PC_REGNUM;
  tdep->npc_regnum = SPARC32_NPC_REGNUM;
  tdep->gregset = NULL;
  tdep->sizeof_gregset = 0;
  tdep->fpregset = NULL;
  tdep->sizeof_fpregset = 0;
  tdep->plt_entry_size = 0;

  set_gdbarch_long_double_bit (gdbarch, 128);
  set_gdbarch_long_double_format (gdbarch, &floatformat_sparc_quad);

  set_gdbarch_num_regs (gdbarch, SPARC32_NUM_REGS);
  set_gdbarch_register_name (gdbarch, sparc32_register_name);
  set_gdbarch_register_type (gdbarch, sparc32_register_type);
  set_gdbarch_num_pseudo_regs (gdbarch, SPARC32_NUM_PSEUDO_REGS);
  set_gdbarch_pseudo_register_read (gdbarch, sparc32_pseudo_register_read);
  set_gdbarch_pseudo_register_write (gdbarch, sparc32_pseudo_register_write);

  /* Register numbers of various important registers.  */
  set_gdbarch_sp_regnum (gdbarch, SPARC_SP_REGNUM); /* %sp */
  set_gdbarch_pc_regnum (gdbarch, SPARC32_PC_REGNUM); /* %pc */
  set_gdbarch_fp0_regnum (gdbarch, SPARC_F0_REGNUM); /* %f0 */

  /* Call dummy code.  */
  set_gdbarch_call_dummy_location (gdbarch, ON_STACK);
  set_gdbarch_push_dummy_code (gdbarch, sparc32_push_dummy_code);
  set_gdbarch_push_dummy_call (gdbarch, sparc32_push_dummy_call);

  set_gdbarch_return_value (gdbarch, sparc32_return_value);
  set_gdbarch_stabs_argument_has_addr
    (gdbarch, sparc32_stabs_argument_has_addr);

  set_gdbarch_skip_prologue (gdbarch, sparc32_skip_prologue);

  /* Stack grows downward.  */
  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);

  set_gdbarch_breakpoint_from_pc (gdbarch, sparc_breakpoint_from_pc);

  set_gdbarch_frame_args_skip (gdbarch, 8);

  set_gdbarch_print_insn (gdbarch, print_insn_sparc);

  set_gdbarch_software_single_step (gdbarch, sparc_software_single_step);
  set_gdbarch_write_pc (gdbarch, sparc_write_pc);

  set_gdbarch_unwind_dummy_id (gdbarch, sparc_unwind_dummy_id);

  set_gdbarch_unwind_pc (gdbarch, sparc_unwind_pc);

  frame_base_set_default (gdbarch, &sparc32_frame_base);

  /* Hook in ABI-specific overrides, if they have been registered.  */
  gdbarch_init_osabi (info, gdbarch);

  frame_unwind_append_sniffer (gdbarch, sparc32_frame_sniffer);

  /* If we have register sets, enable the generic core file support.  */
  if (tdep->gregset)
    set_gdbarch_regset_from_core_section (gdbarch,
					  sparc_regset_from_core_section);

  return gdbarch;
}

/* Helper functions for dealing with register windows.  */

void
sparc_supply_rwindow (struct regcache *regcache, CORE_ADDR sp, int regnum)
{
  int offset = 0;
  char buf[8];
  int i;

  if (sp & 1)
    {
      /* Registers are 64-bit.  */
      sp += BIAS;

      for (i = SPARC_L0_REGNUM; i <= SPARC_I7_REGNUM; i++)
	{
	  if (regnum == i || regnum == -1)
	    {
	      target_read_memory (sp + ((i - SPARC_L0_REGNUM) * 8), buf, 8);
	      regcache_raw_supply (regcache, i, buf);
	    }
	}
    }
  else
    {
      /* Registers are 32-bit.  Toss any sign-extension of the stack
	 pointer.  */
      sp &= 0xffffffffUL;

      /* Clear out the top half of the temporary buffer, and put the
	 register value in the bottom half if we're in 64-bit mode.  */
      if (gdbarch_ptr_bit (current_gdbarch) == 64)
	{
	  memset (buf, 0, 4);
	  offset = 4;
	}

      for (i = SPARC_L0_REGNUM; i <= SPARC_I7_REGNUM; i++)
	{
	  if (regnum == i || regnum == -1)
	    {
	      target_read_memory (sp + ((i - SPARC_L0_REGNUM) * 4),
				  buf + offset, 4);

	      /* Handle StackGhost.  */
	      if (i == SPARC_I7_REGNUM)
		{
		  ULONGEST wcookie = sparc_fetch_wcookie ();
		  ULONGEST i7 = extract_unsigned_integer (buf + offset, 4);

		  store_unsigned_integer (buf + offset, 4, i7 ^ wcookie);
		}

	      regcache_raw_supply (regcache, i, buf);
	    }
	}
    }
}

void
sparc_collect_rwindow (const struct regcache *regcache,
		       CORE_ADDR sp, int regnum)
{
  int offset = 0;
  char buf[8];
  int i;

  if (sp & 1)
    {
      /* Registers are 64-bit.  */
      sp += BIAS;

      for (i = SPARC_L0_REGNUM; i <= SPARC_I7_REGNUM; i++)
	{
	  if (regnum == -1 || regnum == SPARC_SP_REGNUM || regnum == i)
	    {
	      regcache_raw_collect (regcache, i, buf);
	      target_write_memory (sp + ((i - SPARC_L0_REGNUM) * 8), buf, 8);
	    }
	}
    }
  else
    {
      /* Registers are 32-bit.  Toss any sign-extension of the stack
	 pointer.  */
      sp &= 0xffffffffUL;

      /* Only use the bottom half if we're in 64-bit mode.  */
      if (gdbarch_ptr_bit (current_gdbarch) == 64)
	offset = 4;

      for (i = SPARC_L0_REGNUM; i <= SPARC_I7_REGNUM; i++)
	{
	  if (regnum == -1 || regnum == SPARC_SP_REGNUM || regnum == i)
	    {
	      regcache_raw_collect (regcache, i, buf);

	      /* Handle StackGhost.  */
	      if (i == SPARC_I7_REGNUM)
		{
		  ULONGEST wcookie = sparc_fetch_wcookie ();
		  ULONGEST i7 = extract_unsigned_integer (buf + offset, 4);

		  store_unsigned_integer (buf + offset, 4, i7 ^ wcookie);
		}

	      target_write_memory (sp + ((i - SPARC_L0_REGNUM) * 4),
				   buf + offset, 4);
	    }
	}
    }
}

/* Helper functions for dealing with register sets.  */

void
sparc32_supply_gregset (const struct sparc_gregset *gregset,
			struct regcache *regcache,
			int regnum, const void *gregs)
{
  const char *regs = gregs;
  int i;

  if (regnum == SPARC32_PSR_REGNUM || regnum == -1)
    regcache_raw_supply (regcache, SPARC32_PSR_REGNUM,
			 regs + gregset->r_psr_offset);

  if (regnum == SPARC32_PC_REGNUM || regnum == -1)
    regcache_raw_supply (regcache, SPARC32_PC_REGNUM,
			 regs + gregset->r_pc_offset);

  if (regnum == SPARC32_NPC_REGNUM || regnum == -1)
    regcache_raw_supply (regcache, SPARC32_NPC_REGNUM,
			 regs + gregset->r_npc_offset);

  if (regnum == SPARC32_Y_REGNUM || regnum == -1)
    regcache_raw_supply (regcache, SPARC32_Y_REGNUM,
			 regs + gregset->r_y_offset);

  if (regnum == SPARC_G0_REGNUM || regnum == -1)
    regcache_raw_supply (regcache, SPARC_G0_REGNUM, NULL);

  if ((regnum >= SPARC_G1_REGNUM && regnum <= SPARC_O7_REGNUM) || regnum == -1)
    {
      int offset = gregset->r_g1_offset;

      for (i = SPARC_G1_REGNUM; i <= SPARC_O7_REGNUM; i++)
	{
	  if (regnum == i || regnum == -1)
	    regcache_raw_supply (regcache, i, regs + offset);
	  offset += 4;
	}
    }

  if ((regnum >= SPARC_L0_REGNUM && regnum <= SPARC_I7_REGNUM) || regnum == -1)
    {
      /* Not all of the register set variants include Locals and
         Inputs.  For those that don't, we read them off the stack.  */
      if (gregset->r_l0_offset == -1)
	{
	  ULONGEST sp;

	  regcache_cooked_read_unsigned (regcache, SPARC_SP_REGNUM, &sp);
	  sparc_supply_rwindow (regcache, sp, regnum);
	}
      else
	{
	  int offset = gregset->r_l0_offset;

	  for (i = SPARC_L0_REGNUM; i <= SPARC_I7_REGNUM; i++)
	    {
	      if (regnum == i || regnum == -1)
		regcache_raw_supply (regcache, i, regs + offset);
	      offset += 4;
	    }
	}
    }
}

void
sparc32_collect_gregset (const struct sparc_gregset *gregset,
			 const struct regcache *regcache,
			 int regnum, void *gregs)
{
  char *regs = gregs;
  int i;

  if (regnum == SPARC32_PSR_REGNUM || regnum == -1)
    regcache_raw_collect (regcache, SPARC32_PSR_REGNUM,
			  regs + gregset->r_psr_offset);

  if (regnum == SPARC32_PC_REGNUM || regnum == -1)
    regcache_raw_collect (regcache, SPARC32_PC_REGNUM,
			  regs + gregset->r_pc_offset);

  if (regnum == SPARC32_NPC_REGNUM || regnum == -1)
    regcache_raw_collect (regcache, SPARC32_NPC_REGNUM,
			  regs + gregset->r_npc_offset);

  if (regnum == SPARC32_Y_REGNUM || regnum == -1)
    regcache_raw_collect (regcache, SPARC32_Y_REGNUM,
			  regs + gregset->r_y_offset);

  if ((regnum >= SPARC_G1_REGNUM && regnum <= SPARC_O7_REGNUM) || regnum == -1)
    {
      int offset = gregset->r_g1_offset;

      /* %g0 is always zero.  */
      for (i = SPARC_G1_REGNUM; i <= SPARC_O7_REGNUM; i++)
	{
	  if (regnum == i || regnum == -1)
	    regcache_raw_collect (regcache, i, regs + offset);
	  offset += 4;
	}
    }

  if ((regnum >= SPARC_L0_REGNUM && regnum <= SPARC_I7_REGNUM) || regnum == -1)
    {
      /* Not all of the register set variants include Locals and
         Inputs.  For those that don't, we read them off the stack.  */
      if (gregset->r_l0_offset != -1)
	{
	  int offset = gregset->r_l0_offset;

	  for (i = SPARC_L0_REGNUM; i <= SPARC_I7_REGNUM; i++)
	    {
	      if (regnum == i || regnum == -1)
		regcache_raw_collect (regcache, i, regs + offset);
	      offset += 4;
	    }
	}
    }
}

void
sparc32_supply_fpregset (struct regcache *regcache,
			 int regnum, const void *fpregs)
{
  const char *regs = fpregs;
  int i;

  for (i = 0; i < 32; i++)
    {
      if (regnum == (SPARC_F0_REGNUM + i) || regnum == -1)
	regcache_raw_supply (regcache, SPARC_F0_REGNUM + i, regs + (i * 4));
    }

  if (regnum == SPARC32_FSR_REGNUM || regnum == -1)
    regcache_raw_supply (regcache, SPARC32_FSR_REGNUM, regs + (32 * 4) + 4);
}

void
sparc32_collect_fpregset (const struct regcache *regcache,
			  int regnum, void *fpregs)
{
  char *regs = fpregs;
  int i;

  for (i = 0; i < 32; i++)
    {
      if (regnum == (SPARC_F0_REGNUM + i) || regnum == -1)
	regcache_raw_collect (regcache, SPARC_F0_REGNUM + i, regs + (i * 4));
    }

  if (regnum == SPARC32_FSR_REGNUM || regnum == -1)
    regcache_raw_collect (regcache, SPARC32_FSR_REGNUM, regs + (32 * 4) + 4);
}


/* SunOS 4.  */

/* From <machine/reg.h>.  */
const struct sparc_gregset sparc32_sunos4_gregset =
{
  0 * 4,			/* %psr */
  1 * 4,			/* %pc */
  2 * 4,			/* %npc */
  3 * 4,			/* %y */
  -1,				/* %wim */
  -1,				/* %tbr */
  4 * 4,			/* %g1 */
  -1				/* %l0 */
};


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_sparc_tdep (void);

void
_initialize_sparc_tdep (void)
{
  register_gdbarch_init (bfd_arch_sparc, sparc32_gdbarch_init);
}
