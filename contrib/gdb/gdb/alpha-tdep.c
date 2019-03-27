/* Target-dependent code for the ALPHA architecture, for GDB, the GNU Debugger.
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
#include "doublest.h"
#include "frame.h"
#include "frame-unwind.h"
#include "frame-base.h"
#include "dwarf2-frame.h"
#include "inferior.h"
#include "symtab.h"
#include "value.h"
#include "gdbcmd.h"
#include "gdbcore.h"
#include "dis-asm.h"
#include "symfile.h"
#include "objfiles.h"
#include "gdb_string.h"
#include "linespec.h"
#include "regcache.h"
#include "reggroups.h"
#include "arch-utils.h"
#include "osabi.h"
#include "block.h"

#include "elf-bfd.h"

#include "alpha-tdep.h"


static const char *
alpha_register_name (int regno)
{
  static const char * const register_names[] =
  {
    "v0",   "t0",   "t1",   "t2",   "t3",   "t4",   "t5",   "t6",
    "t7",   "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "fp",
    "a0",   "a1",   "a2",   "a3",   "a4",   "a5",   "t8",   "t9",
    "t10",  "t11",  "ra",   "t12",  "at",   "gp",   "sp",   "zero",
    "f0",   "f1",   "f2",   "f3",   "f4",   "f5",   "f6",   "f7",
    "f8",   "f9",   "f10",  "f11",  "f12",  "f13",  "f14",  "f15",
    "f16",  "f17",  "f18",  "f19",  "f20",  "f21",  "f22",  "f23",
    "f24",  "f25",  "f26",  "f27",  "f28",  "f29",  "f30",  "fpcr",
    "pc",   "",     "unique"
  };

  if (regno < 0)
    return NULL;
  if (regno >= (sizeof(register_names) / sizeof(*register_names)))
    return NULL;
  return register_names[regno];
}

static int
alpha_cannot_fetch_register (int regno)
{
  return regno == ALPHA_ZERO_REGNUM;
}

static int
alpha_cannot_store_register (int regno)
{
  return regno == ALPHA_ZERO_REGNUM;
}

static struct type *
alpha_register_type (struct gdbarch *gdbarch, int regno)
{
  if (regno == ALPHA_SP_REGNUM || regno == ALPHA_GP_REGNUM)
    return builtin_type_void_data_ptr;
  if (regno == ALPHA_PC_REGNUM)
    return builtin_type_void_func_ptr;

  /* Don't need to worry about little vs big endian until 
     some jerk tries to port to alpha-unicosmk.  */
  if (regno >= ALPHA_FP0_REGNUM && regno < ALPHA_FP0_REGNUM + 31)
    return builtin_type_ieee_double_little;

  return builtin_type_int64;
}

/* Is REGNUM a member of REGGROUP?  */

static int
alpha_register_reggroup_p (struct gdbarch *gdbarch, int regnum,
			   struct reggroup *group)
{
  /* Filter out any registers eliminated, but whose regnum is 
     reserved for backward compatibility, e.g. the vfp.  */
  if (REGISTER_NAME (regnum) == NULL || *REGISTER_NAME (regnum) == '\0')
    return 0;

  if (group == all_reggroup)
    return 1;

  /* Zero should not be saved or restored.  Technically it is a general
     register (just as $f31 would be a float if we represented it), but
     there's no point displaying it during "info regs", so leave it out
     of all groups except for "all".  */
  if (regnum == ALPHA_ZERO_REGNUM)
    return 0;

  /* All other registers are saved and restored.  */
  if (group == save_reggroup || group == restore_reggroup)
    return 1;

  /* All other groups are non-overlapping.  */

  /* Since this is really a PALcode memory slot...  */
  if (regnum == ALPHA_UNIQUE_REGNUM)
    return group == system_reggroup;

  /* Force the FPCR to be considered part of the floating point state.  */
  if (regnum == ALPHA_FPCR_REGNUM)
    return group == float_reggroup;

  if (regnum >= ALPHA_FP0_REGNUM && regnum < ALPHA_FP0_REGNUM + 31)
    return group == float_reggroup;
  else
    return group == general_reggroup;
}

static int
alpha_register_byte (int regno)
{
  return (regno * 8);
}

static int
alpha_register_raw_size (int regno)
{
  return 8;
}

static int
alpha_register_virtual_size (int regno)
{
  return 8;
}

/* The following represents exactly the conversion performed by
   the LDS instruction.  This applies to both single-precision
   floating point and 32-bit integers.  */

static void
alpha_lds (void *out, const void *in)
{
  ULONGEST mem     = extract_unsigned_integer (in, 4);
  ULONGEST frac    = (mem >>  0) & 0x7fffff;
  ULONGEST sign    = (mem >> 31) & 1;
  ULONGEST exp_msb = (mem >> 30) & 1;
  ULONGEST exp_low = (mem >> 23) & 0x7f;
  ULONGEST exp, reg;

  exp = (exp_msb << 10) | exp_low;
  if (exp_msb)
    {
      if (exp_low == 0x7f)
	exp = 0x7ff;
    }
  else
    {
      if (exp_low != 0x00)
	exp |= 0x380;
    }

  reg = (sign << 63) | (exp << 52) | (frac << 29);
  store_unsigned_integer (out, 8, reg);
}

/* Similarly, this represents exactly the conversion performed by
   the STS instruction.  */

static void
alpha_sts (void *out, const void *in)
{
  ULONGEST reg, mem;

  reg = extract_unsigned_integer (in, 8);
  mem = ((reg >> 32) & 0xc0000000) | ((reg >> 29) & 0x3fffffff);
  store_unsigned_integer (out, 4, mem);
}

/* The alpha needs a conversion between register and memory format if the
   register is a floating point register and memory format is float, as the
   register format must be double or memory format is an integer with 4
   bytes or less, as the representation of integers in floating point
   registers is different. */

static int
alpha_convert_register_p (int regno, struct type *type)
{
  return (regno >= ALPHA_FP0_REGNUM && regno < ALPHA_FP0_REGNUM + 31);
}

static void
alpha_register_to_value (struct frame_info *frame, int regnum,
			 struct type *valtype, void *out)
{
  char in[MAX_REGISTER_SIZE];
  frame_register_read (frame, regnum, in);
  switch (TYPE_LENGTH (valtype))
    {
    case 4:
      alpha_sts (out, in);
      break;
    case 8:
      memcpy (out, in, 8);
      break;
    default:
      error ("Cannot retrieve value from floating point register");
    }
}

static void
alpha_value_to_register (struct frame_info *frame, int regnum,
			 struct type *valtype, const void *in)
{
  char out[MAX_REGISTER_SIZE];
  switch (TYPE_LENGTH (valtype))
    {
    case 4:
      alpha_lds (out, in);
      break;
    case 8:
      memcpy (out, in, 8);
      break;
    default:
      error ("Cannot store value in floating point register");
    }
  put_frame_register (frame, regnum, out);
}


/* The alpha passes the first six arguments in the registers, the rest on
   the stack.  The register arguments are stored in ARG_REG_BUFFER, and
   then moved into the register file; this simplifies the passing of a
   large struct which extends from the registers to the stack, plus avoids
   three ptrace invocations per word.

   We don't bother tracking which register values should go in integer
   regs or fp regs; we load the same values into both.

   If the called function is returning a structure, the address of the
   structure to be returned is passed as a hidden first argument.  */

static CORE_ADDR
alpha_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
		       struct regcache *regcache, CORE_ADDR bp_addr,
		       int nargs, struct value **args, CORE_ADDR sp,
		       int struct_return, CORE_ADDR struct_addr)
{
  int i;
  int accumulate_size = struct_return ? 8 : 0;
  struct alpha_arg
    {
      char *contents;
      int len;
      int offset;
    };
  struct alpha_arg *alpha_args
    = (struct alpha_arg *) alloca (nargs * sizeof (struct alpha_arg));
  struct alpha_arg *m_arg;
  char arg_reg_buffer[ALPHA_REGISTER_SIZE * ALPHA_NUM_ARG_REGS];
  int required_arg_regs;

  /* The ABI places the address of the called function in T12.  */
  regcache_cooked_write_signed (regcache, ALPHA_T12_REGNUM, func_addr);

  /* Set the return address register to point to the entry point
     of the program, where a breakpoint lies in wait.  */
  regcache_cooked_write_signed (regcache, ALPHA_RA_REGNUM, bp_addr);

  /* Lay out the arguments in memory.  */
  for (i = 0, m_arg = alpha_args; i < nargs; i++, m_arg++)
    {
      struct value *arg = args[i];
      struct type *arg_type = check_typedef (VALUE_TYPE (arg));

      /* Cast argument to long if necessary as the compiler does it too.  */
      switch (TYPE_CODE (arg_type))
	{
	case TYPE_CODE_INT:
	case TYPE_CODE_BOOL:
	case TYPE_CODE_CHAR:
	case TYPE_CODE_RANGE:
	case TYPE_CODE_ENUM:
	  if (TYPE_LENGTH (arg_type) == 4)
	    {
	      /* 32-bit values must be sign-extended to 64 bits
		 even if the base data type is unsigned.  */
	      arg_type = builtin_type_int32;
	      arg = value_cast (arg_type, arg);
	    }
	  if (TYPE_LENGTH (arg_type) < ALPHA_REGISTER_SIZE)
	    {
	      arg_type = builtin_type_int64;
	      arg = value_cast (arg_type, arg);
	    }
	  break;

	case TYPE_CODE_FLT:
	  /* "float" arguments loaded in registers must be passed in
	     register format, aka "double".  */
	  if (accumulate_size < sizeof (arg_reg_buffer)
	      && TYPE_LENGTH (arg_type) == 4)
	    {
	      arg_type = builtin_type_ieee_double_little;
	      arg = value_cast (arg_type, arg);
	    }
	  /* Tru64 5.1 has a 128-bit long double, and passes this by
	     invisible reference.  No one else uses this data type.  */
	  else if (TYPE_LENGTH (arg_type) == 16)
	    {
	      /* Allocate aligned storage.  */
	      sp = (sp & -16) - 16;

	      /* Write the real data into the stack.  */
	      write_memory (sp, VALUE_CONTENTS (arg), 16);

	      /* Construct the indirection.  */
	      arg_type = lookup_pointer_type (arg_type);
	      arg = value_from_pointer (arg_type, sp);
	    }
	  break;

	case TYPE_CODE_COMPLEX:
	  /* ??? The ABI says that complex values are passed as two
	     separate scalar values.  This distinction only matters
	     for complex float.  However, GCC does not implement this.  */

	  /* Tru64 5.1 has a 128-bit long double, and passes this by
	     invisible reference.  */
	  if (TYPE_LENGTH (arg_type) == 32)
	    {
	      /* Allocate aligned storage.  */
	      sp = (sp & -16) - 16;

	      /* Write the real data into the stack.  */
	      write_memory (sp, VALUE_CONTENTS (arg), 32);

	      /* Construct the indirection.  */
	      arg_type = lookup_pointer_type (arg_type);
	      arg = value_from_pointer (arg_type, sp);
	    }
	  break;

	default:
	  break;
	}
      m_arg->len = TYPE_LENGTH (arg_type);
      m_arg->offset = accumulate_size;
      accumulate_size = (accumulate_size + m_arg->len + 7) & ~7;
      m_arg->contents = VALUE_CONTENTS (arg);
    }

  /* Determine required argument register loads, loading an argument register
     is expensive as it uses three ptrace calls.  */
  required_arg_regs = accumulate_size / 8;
  if (required_arg_regs > ALPHA_NUM_ARG_REGS)
    required_arg_regs = ALPHA_NUM_ARG_REGS;

  /* Make room for the arguments on the stack.  */
  if (accumulate_size < sizeof(arg_reg_buffer))
    accumulate_size = 0;
  else
    accumulate_size -= sizeof(arg_reg_buffer);
  sp -= accumulate_size;

  /* Keep sp aligned to a multiple of 16 as the ABI requires.  */
  sp &= ~15;

  /* `Push' arguments on the stack.  */
  for (i = nargs; m_arg--, --i >= 0;)
    {
      char *contents = m_arg->contents;
      int offset = m_arg->offset;
      int len = m_arg->len;

      /* Copy the bytes destined for registers into arg_reg_buffer.  */
      if (offset < sizeof(arg_reg_buffer))
	{
	  if (offset + len <= sizeof(arg_reg_buffer))
	    {
	      memcpy (arg_reg_buffer + offset, contents, len);
	      continue;
	    }
	  else
	    {
	      int tlen = sizeof(arg_reg_buffer) - offset;
	      memcpy (arg_reg_buffer + offset, contents, tlen);
	      offset += tlen;
	      contents += tlen;
	      len -= tlen;
	    }
	}

      /* Everything else goes to the stack.  */
      write_memory (sp + offset - sizeof(arg_reg_buffer), contents, len);
    }
  if (struct_return)
    store_unsigned_integer (arg_reg_buffer, ALPHA_REGISTER_SIZE, struct_addr);

  /* Load the argument registers.  */
  for (i = 0; i < required_arg_regs; i++)
    {
      regcache_cooked_write (regcache, ALPHA_A0_REGNUM + i,
			     arg_reg_buffer + i*ALPHA_REGISTER_SIZE);
      regcache_cooked_write (regcache, ALPHA_FPA0_REGNUM + i,
			     arg_reg_buffer + i*ALPHA_REGISTER_SIZE);
    }

  /* Finally, update the stack pointer.  */
  regcache_cooked_write_signed (regcache, ALPHA_SP_REGNUM, sp);

  return sp;
}

/* Extract from REGCACHE the value about to be returned from a function
   and copy it into VALBUF.  */

static void
alpha_extract_return_value (struct type *valtype, struct regcache *regcache,
			    void *valbuf)
{
  int length = TYPE_LENGTH (valtype);
  char raw_buffer[ALPHA_REGISTER_SIZE];
  ULONGEST l;

  switch (TYPE_CODE (valtype))
    {
    case TYPE_CODE_FLT:
      switch (length)
	{
	case 4:
	  regcache_cooked_read (regcache, ALPHA_FP0_REGNUM, raw_buffer);
	  alpha_sts (valbuf, raw_buffer);
	  break;

	case 8:
	  regcache_cooked_read (regcache, ALPHA_FP0_REGNUM, valbuf);
	  break;

	case 16:
	  regcache_cooked_read_unsigned (regcache, ALPHA_V0_REGNUM, &l);
	  read_memory (l, valbuf, 16);
	  break;

	default:
	  internal_error (__FILE__, __LINE__, "unknown floating point width");
	}
      break;

    case TYPE_CODE_COMPLEX:
      switch (length)
	{
	case 8:
	  /* ??? This isn't correct wrt the ABI, but it's what GCC does.  */
	  regcache_cooked_read (regcache, ALPHA_FP0_REGNUM, valbuf);
	  break;

	case 16:
	  regcache_cooked_read (regcache, ALPHA_FP0_REGNUM, valbuf);
	  regcache_cooked_read (regcache, ALPHA_FP0_REGNUM+1,
				(char *)valbuf + 8);
	  break;

	case 32:
	  regcache_cooked_read_signed (regcache, ALPHA_V0_REGNUM, &l);
	  read_memory (l, valbuf, 32);
	  break;

	default:
	  internal_error (__FILE__, __LINE__, "unknown floating point width");
	}
      break;

    default:
      /* Assume everything else degenerates to an integer.  */
      regcache_cooked_read_unsigned (regcache, ALPHA_V0_REGNUM, &l);
      store_unsigned_integer (valbuf, length, l);
      break;
    }
}

/* Extract from REGCACHE the address of a structure about to be returned
   from a function.  */

static CORE_ADDR
alpha_extract_struct_value_address (struct regcache *regcache)
{
  ULONGEST addr;
  regcache_cooked_read_unsigned (regcache, ALPHA_V0_REGNUM, &addr);
  return addr;
}

/* Insert the given value into REGCACHE as if it was being 
   returned by a function.  */

static void
alpha_store_return_value (struct type *valtype, struct regcache *regcache,
			  const void *valbuf)
{
  int length = TYPE_LENGTH (valtype);
  char raw_buffer[ALPHA_REGISTER_SIZE];
  ULONGEST l;

  switch (TYPE_CODE (valtype))
    {
    case TYPE_CODE_FLT:
      switch (length)
	{
	case 4:
	  alpha_lds (raw_buffer, valbuf);
	  regcache_cooked_write (regcache, ALPHA_FP0_REGNUM, raw_buffer);
	  break;

	case 8:
	  regcache_cooked_write (regcache, ALPHA_FP0_REGNUM, valbuf);
	  break;

	case 16:
	  /* FIXME: 128-bit long doubles are returned like structures:
	     by writing into indirect storage provided by the caller
	     as the first argument.  */
	  error ("Cannot set a 128-bit long double return value.");

	default:
	  internal_error (__FILE__, __LINE__, "unknown floating point width");
	}
      break;

    case TYPE_CODE_COMPLEX:
      switch (length)
	{
	case 8:
	  /* ??? This isn't correct wrt the ABI, but it's what GCC does.  */
	  regcache_cooked_write (regcache, ALPHA_FP0_REGNUM, valbuf);
	  break;

	case 16:
	  regcache_cooked_write (regcache, ALPHA_FP0_REGNUM, valbuf);
	  regcache_cooked_write (regcache, ALPHA_FP0_REGNUM+1,
				 (const char *)valbuf + 8);
	  break;

	case 32:
	  /* FIXME: 128-bit long doubles are returned like structures:
	     by writing into indirect storage provided by the caller
	     as the first argument.  */
	  error ("Cannot set a 128-bit long double return value.");

	default:
	  internal_error (__FILE__, __LINE__, "unknown floating point width");
	}
      break;

    default:
      /* Assume everything else degenerates to an integer.  */
      /* 32-bit values must be sign-extended to 64 bits
	 even if the base data type is unsigned.  */
      if (length == 4)
	valtype = builtin_type_int32;
      l = unpack_long (valtype, valbuf);
      regcache_cooked_write_unsigned (regcache, ALPHA_V0_REGNUM, l);
      break;
    }
}


static const unsigned char *
alpha_breakpoint_from_pc (CORE_ADDR *pcptr, int *lenptr)
{
  static const unsigned char alpha_breakpoint[] =
    { 0x80, 0, 0, 0 };	/* call_pal bpt */

  *lenptr = sizeof(alpha_breakpoint);
  return (alpha_breakpoint);
}


/* This returns the PC of the first insn after the prologue.
   If we can't find the prologue, then return 0.  */

CORE_ADDR
alpha_after_prologue (CORE_ADDR pc)
{
  struct symtab_and_line sal;
  CORE_ADDR func_addr, func_end;

  if (!find_pc_partial_function (pc, NULL, &func_addr, &func_end))
    return 0;

  sal = find_pc_line (func_addr, 0);
  if (sal.end < func_end)
    return sal.end;

  /* The line after the prologue is after the end of the function.  In this
     case, tell the caller to find the prologue the hard way.  */
  return 0;
}

/* Read an instruction from memory at PC, looking through breakpoints.  */

unsigned int
alpha_read_insn (CORE_ADDR pc)
{
  char buf[4];
  int status;

  status = read_memory_nobpt (pc, buf, 4);
  if (status)
    memory_error (status, pc);
  return extract_unsigned_integer (buf, 4);
}

/* To skip prologues, I use this predicate.  Returns either PC itself
   if the code at PC does not look like a function prologue; otherwise
   returns an address that (if we're lucky) follows the prologue.  If
   LENIENT, then we must skip everything which is involved in setting
   up the frame (it's OK to skip more, just so long as we don't skip
   anything which might clobber the registers which are being saved.  */

static CORE_ADDR
alpha_skip_prologue (CORE_ADDR pc)
{
  unsigned long inst;
  int offset;
  CORE_ADDR post_prologue_pc;
  char buf[4];

  /* Silently return the unaltered pc upon memory errors.
     This could happen on OSF/1 if decode_line_1 tries to skip the
     prologue for quickstarted shared library functions when the
     shared library is not yet mapped in.
     Reading target memory is slow over serial lines, so we perform
     this check only if the target has shared libraries (which all
     Alpha targets do).  */
  if (target_read_memory (pc, buf, 4))
    return pc;

  /* See if we can determine the end of the prologue via the symbol table.
     If so, then return either PC, or the PC after the prologue, whichever
     is greater.  */

  post_prologue_pc = alpha_after_prologue (pc);
  if (post_prologue_pc != 0)
    return max (pc, post_prologue_pc);

  /* Can't determine prologue from the symbol table, need to examine
     instructions.  */

  /* Skip the typical prologue instructions. These are the stack adjustment
     instruction and the instructions that save registers on the stack
     or in the gcc frame.  */
  for (offset = 0; offset < 100; offset += 4)
    {
      inst = alpha_read_insn (pc + offset);

      if ((inst & 0xffff0000) == 0x27bb0000)	/* ldah $gp,n($t12) */
	continue;
      if ((inst & 0xffff0000) == 0x23bd0000)	/* lda $gp,n($gp) */
	continue;
      if ((inst & 0xffff0000) == 0x23de0000)	/* lda $sp,n($sp) */
	continue;
      if ((inst & 0xffe01fff) == 0x43c0153e)	/* subq $sp,n,$sp */
	continue;

      if (((inst & 0xfc1f0000) == 0xb41e0000		/* stq reg,n($sp) */
	   || (inst & 0xfc1f0000) == 0x9c1e0000)	/* stt reg,n($sp) */
	  && (inst & 0x03e00000) != 0x03e00000)		/* reg != $zero */
	continue;

      if (inst == 0x47de040f)			/* bis sp,sp,fp */
	continue;
      if (inst == 0x47fe040f)			/* bis zero,sp,fp */
	continue;

      break;
    }
  return pc + offset;
}


/* Figure out where the longjmp will land.
   We expect the first arg to be a pointer to the jmp_buf structure from
   which we extract the PC (JB_PC) that we will land at.  The PC is copied
   into the "pc".  This routine returns true on success.  */

static int
alpha_get_longjmp_target (CORE_ADDR *pc)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  CORE_ADDR jb_addr;
  char raw_buffer[ALPHA_REGISTER_SIZE];

  jb_addr = read_register (ALPHA_A0_REGNUM);

  if (target_read_memory (jb_addr + (tdep->jb_pc * tdep->jb_elt_size),
			  raw_buffer, tdep->jb_elt_size))
    return 0;

  *pc = extract_unsigned_integer (raw_buffer, tdep->jb_elt_size);
  return 1;
}


/* Frame unwinder for signal trampolines.  We use alpha tdep bits that
   describe the location and shape of the sigcontext structure.  After
   that, all registers are in memory, so it's easy.  */
/* ??? Shouldn't we be able to do this generically, rather than with
   OSABI data specific to Alpha?  */

struct alpha_sigtramp_unwind_cache
{
  CORE_ADDR sigcontext_addr;
};

static struct alpha_sigtramp_unwind_cache *
alpha_sigtramp_frame_unwind_cache (struct frame_info *next_frame,
				   void **this_prologue_cache)
{
  struct alpha_sigtramp_unwind_cache *info;
  struct gdbarch_tdep *tdep;

  if (*this_prologue_cache)
    return *this_prologue_cache;

  info = FRAME_OBSTACK_ZALLOC (struct alpha_sigtramp_unwind_cache);
  *this_prologue_cache = info;

  tdep = gdbarch_tdep (current_gdbarch);
  info->sigcontext_addr = tdep->sigcontext_addr (next_frame);

  return info;
}

/* Return the address of REGNUM in a sigtramp frame.  Since this is
   all arithmetic, it doesn't seem worthwhile to cache it.  */

static CORE_ADDR
alpha_sigtramp_register_address (CORE_ADDR sigcontext_addr, int regnum)
{ 
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (regnum >= 0 && regnum < 32)
    return sigcontext_addr + tdep->sc_regs_offset + regnum * 8;
  else if (regnum >= ALPHA_FP0_REGNUM && regnum < ALPHA_FP0_REGNUM + 32)
    return sigcontext_addr + tdep->sc_fpregs_offset + regnum * 8;
  else if (regnum == ALPHA_PC_REGNUM)
    return sigcontext_addr + tdep->sc_pc_offset; 

  return 0;
}

/* Given a GDB frame, determine the address of the calling function's
   frame.  This will be used to create a new GDB frame struct.  */

static void
alpha_sigtramp_frame_this_id (struct frame_info *next_frame,
			      void **this_prologue_cache,
			      struct frame_id *this_id)
{
  struct alpha_sigtramp_unwind_cache *info
    = alpha_sigtramp_frame_unwind_cache (next_frame, this_prologue_cache);
  struct gdbarch_tdep *tdep;
  CORE_ADDR stack_addr, code_addr;

  /* If the OSABI couldn't locate the sigcontext, give up.  */
  if (info->sigcontext_addr == 0)
    return;

  /* If we have dynamic signal trampolines, find their start.
     If we do not, then we must assume there is a symbol record
     that can provide the start address.  */
  tdep = gdbarch_tdep (current_gdbarch);
  if (tdep->dynamic_sigtramp_offset)
    {
      int offset;
      code_addr = frame_pc_unwind (next_frame);
      offset = tdep->dynamic_sigtramp_offset (code_addr);
      if (offset >= 0)
	code_addr -= offset;
      else
	code_addr = 0;
    }
  else
    code_addr = frame_func_unwind (next_frame);

  /* The stack address is trivially read from the sigcontext.  */
  stack_addr = alpha_sigtramp_register_address (info->sigcontext_addr,
						ALPHA_SP_REGNUM);
  stack_addr = get_frame_memory_unsigned (next_frame, stack_addr,
					  ALPHA_REGISTER_SIZE);

  *this_id = frame_id_build (stack_addr, code_addr);
}

/* Retrieve the value of REGNUM in FRAME.  Don't give up!  */

static void
alpha_sigtramp_frame_prev_register (struct frame_info *next_frame,
				    void **this_prologue_cache,
				    int regnum, int *optimizedp,
				    enum lval_type *lvalp, CORE_ADDR *addrp,
				    int *realnump, void *bufferp)
{
  struct alpha_sigtramp_unwind_cache *info
    = alpha_sigtramp_frame_unwind_cache (next_frame, this_prologue_cache);
  CORE_ADDR addr;

  if (info->sigcontext_addr != 0)
    {
      /* All integer and fp registers are stored in memory.  */
      addr = alpha_sigtramp_register_address (info->sigcontext_addr, regnum);
      if (addr != 0)
	{
	  *optimizedp = 0;
	  *lvalp = lval_memory;
	  *addrp = addr;
	  *realnump = -1;
	  if (bufferp != NULL)
	    get_frame_memory (next_frame, addr, bufferp, ALPHA_REGISTER_SIZE);
	  return;
	}
    }

  /* This extra register may actually be in the sigcontext, but our
     current description of it in alpha_sigtramp_frame_unwind_cache
     doesn't include it.  Too bad.  Fall back on whatever's in the
     outer frame.  */
  frame_register (next_frame, regnum, optimizedp, lvalp, addrp,
		  realnump, bufferp);
}

static const struct frame_unwind alpha_sigtramp_frame_unwind = {
  SIGTRAMP_FRAME,
  alpha_sigtramp_frame_this_id,
  alpha_sigtramp_frame_prev_register
};

static const struct frame_unwind *
alpha_sigtramp_frame_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  char *name;

  /* We shouldn't even bother to try if the OSABI didn't register
     a sigcontext_addr handler.  */
  if (!gdbarch_tdep (current_gdbarch)->sigcontext_addr)
    return NULL;

  /* Otherwise we should be in a signal frame.  */
  find_pc_partial_function (pc, &name, NULL, NULL);
  if (PC_IN_SIGTRAMP (pc, name))
    return &alpha_sigtramp_frame_unwind;

  return NULL;
}

/* Fallback alpha frame unwinder.  Uses instruction scanning and knows
   something about the traditional layout of alpha stack frames.  */

struct alpha_heuristic_unwind_cache
{
  CORE_ADDR *saved_regs;
  CORE_ADDR vfp;
  CORE_ADDR start_pc;
  int return_reg;
};

/* Heuristic_proc_start may hunt through the text section for a long
   time across a 2400 baud serial line.  Allows the user to limit this
   search.  */
static unsigned int heuristic_fence_post = 0;

/* Attempt to locate the start of the function containing PC.  We assume that
   the previous function ends with an about_to_return insn.  Not foolproof by
   any means, since gcc is happy to put the epilogue in the middle of a
   function.  But we're guessing anyway...  */

static CORE_ADDR
alpha_heuristic_proc_start (CORE_ADDR pc)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  CORE_ADDR last_non_nop = pc;
  CORE_ADDR fence = pc - heuristic_fence_post;
  CORE_ADDR orig_pc = pc;
  CORE_ADDR func;

  if (pc == 0)
    return 0;

  /* First see if we can find the start of the function from minimal
     symbol information.  This can succeed with a binary that doesn't
     have debug info, but hasn't been stripped.  */
  func = get_pc_function_start (pc);
  if (func)
    return func;

  if (heuristic_fence_post == UINT_MAX
      || fence < tdep->vm_min_address)
    fence = tdep->vm_min_address;

  /* Search back for previous return; also stop at a 0, which might be
     seen for instance before the start of a code section.  Don't include
     nops, since this usually indicates padding between functions.  */
  for (pc -= 4; pc >= fence; pc -= 4)
    {
      unsigned int insn = alpha_read_insn (pc);
      switch (insn)
	{
	case 0:			/* invalid insn */
	case 0x6bfa8001:	/* ret $31,($26),1 */
	  return last_non_nop;

	case 0x2ffe0000:	/* unop: ldq_u $31,0($30) */
	case 0x47ff041f:	/* nop: bis $31,$31,$31 */
	  break;

	default:
	  last_non_nop = pc;
	  break;
	}
    }

  /* It's not clear to me why we reach this point when stopping quietly,
     but with this test, at least we don't print out warnings for every
     child forked (eg, on decstation).  22apr93 rich@cygnus.com.  */
  if (stop_soon == NO_STOP_QUIETLY)
    {
      static int blurb_printed = 0;

      if (fence == tdep->vm_min_address)
	warning ("Hit beginning of text section without finding");
      else
	warning ("Hit heuristic-fence-post without finding");
      warning ("enclosing function for address 0x%s", paddr_nz (orig_pc));

      if (!blurb_printed)
	{
	  printf_filtered ("\
This warning occurs if you are debugging a function without any symbols\n\
(for example, in a stripped executable).  In that case, you may wish to\n\
increase the size of the search with the `set heuristic-fence-post' command.\n\
\n\
Otherwise, you told GDB there was a function where there isn't one, or\n\
(more likely) you have encountered a bug in GDB.\n");
	  blurb_printed = 1;
	}
    }

  return 0;
}

static struct alpha_heuristic_unwind_cache *
alpha_heuristic_frame_unwind_cache (struct frame_info *next_frame,
				    void **this_prologue_cache,
				    CORE_ADDR start_pc)
{
  struct alpha_heuristic_unwind_cache *info;
  ULONGEST val;
  CORE_ADDR limit_pc, cur_pc;
  int frame_reg, frame_size, return_reg, reg;

  if (*this_prologue_cache)
    return *this_prologue_cache;

  info = FRAME_OBSTACK_ZALLOC (struct alpha_heuristic_unwind_cache);
  *this_prologue_cache = info;
  info->saved_regs = frame_obstack_zalloc (SIZEOF_FRAME_SAVED_REGS);

  limit_pc = frame_pc_unwind (next_frame);
  if (start_pc == 0)
    start_pc = alpha_heuristic_proc_start (limit_pc);
  info->start_pc = start_pc;

  frame_reg = ALPHA_SP_REGNUM;
  frame_size = 0;
  return_reg = -1;

  /* If we've identified a likely place to start, do code scanning.  */
  if (start_pc != 0)
    {
      /* Limit the forward search to 50 instructions.  */
      if (start_pc + 200 < limit_pc)
	limit_pc = start_pc + 200;

      for (cur_pc = start_pc; cur_pc < limit_pc; cur_pc += 4)
	{
	  unsigned int word = alpha_read_insn (cur_pc);

	  if ((word & 0xffff0000) == 0x23de0000)	/* lda $sp,n($sp) */
	    {
	      if (word & 0x8000)
		{
		  /* Consider only the first stack allocation instruction
		     to contain the static size of the frame. */
		  if (frame_size == 0)
		    frame_size = (-word) & 0xffff;
		}
	      else
		{
		  /* Exit loop if a positive stack adjustment is found, which
		     usually means that the stack cleanup code in the function
		     epilogue is reached.  */
		  break;
		}
	    }
	  else if ((word & 0xfc1f0000) == 0xb41e0000)	/* stq reg,n($sp) */
	    {
	      reg = (word & 0x03e00000) >> 21;

              /* Ignore this instruction if we have already encountered
                 an instruction saving the same register earlier in the
                 function code.  The current instruction does not tell
                 us where the original value upon function entry is saved.
                 All it says is that the function we are scanning reused
                 that register for some computation of its own, and is now
                 saving its result.  */
              if (info->saved_regs[reg])
                continue;

	      if (reg == 31)
		continue;

	      /* Do not compute the address where the register was saved yet,
		 because we don't know yet if the offset will need to be
		 relative to $sp or $fp (we can not compute the address
		 relative to $sp if $sp is updated during the execution of
		 the current subroutine, for instance when doing some alloca).
		 So just store the offset for the moment, and compute the
		 address later when we know whether this frame has a frame
		 pointer or not.  */
	      /* Hack: temporarily add one, so that the offset is non-zero
		 and we can tell which registers have save offsets below.  */
	      info->saved_regs[reg] = (word & 0xffff) + 1;

	      /* Starting with OSF/1-3.2C, the system libraries are shipped
		 without local symbols, but they still contain procedure
		 descriptors without a symbol reference. GDB is currently
		 unable to find these procedure descriptors and uses
		 heuristic_proc_desc instead.
		 As some low level compiler support routines (__div*, __add*)
		 use a non-standard return address register, we have to
		 add some heuristics to determine the return address register,
		 or stepping over these routines will fail.
		 Usually the return address register is the first register
		 saved on the stack, but assembler optimization might
		 rearrange the register saves.
		 So we recognize only a few registers (t7, t9, ra) within
		 the procedure prologue as valid return address registers.
		 If we encounter a return instruction, we extract the
		 the return address register from it.

		 FIXME: Rewriting GDB to access the procedure descriptors,
		 e.g. via the minimal symbol table, might obviate this hack.  */
	      if (return_reg == -1
		  && cur_pc < (start_pc + 80)
		  && (reg == ALPHA_T7_REGNUM
		      || reg == ALPHA_T9_REGNUM
		      || reg == ALPHA_RA_REGNUM))
		return_reg = reg;
	    }
	  else if ((word & 0xffe0ffff) == 0x6be08001)	/* ret zero,reg,1 */
	    return_reg = (word >> 16) & 0x1f;
	  else if (word == 0x47de040f)			/* bis sp,sp,fp */
	    frame_reg = ALPHA_GCC_FP_REGNUM;
	  else if (word == 0x47fe040f)			/* bis zero,sp,fp */
	    frame_reg = ALPHA_GCC_FP_REGNUM;
	}

      /* If we haven't found a valid return address register yet, keep
	 searching in the procedure prologue.  */
      if (return_reg == -1)
	{
	  while (cur_pc < (limit_pc + 80) && cur_pc < (start_pc + 80))
	    {
	      unsigned int word = alpha_read_insn (cur_pc);

	      if ((word & 0xfc1f0000) == 0xb41e0000)	/* stq reg,n($sp) */
		{
		  reg = (word & 0x03e00000) >> 21;
		  if (reg == ALPHA_T7_REGNUM
		      || reg == ALPHA_T9_REGNUM
		      || reg == ALPHA_RA_REGNUM)
		    {
		      return_reg = reg;
		      break;
		    }
		}
	      else if ((word & 0xffe0ffff) == 0x6be08001) /* ret zero,reg,1 */
		{
		  return_reg = (word >> 16) & 0x1f;
		  break;
		}

	      cur_pc += 4;
	    }
	}
    }

  /* Failing that, do default to the customary RA.  */
  if (return_reg == -1)
    return_reg = ALPHA_RA_REGNUM;
  info->return_reg = return_reg;

  frame_unwind_unsigned_register (next_frame, frame_reg, &val);
  info->vfp = val + frame_size;

  /* Convert offsets to absolute addresses.  See above about adding
     one to the offsets to make all detected offsets non-zero.  */
  for (reg = 0; reg < ALPHA_NUM_REGS; ++reg)
    if (info->saved_regs[reg])
      info->saved_regs[reg] += val - 1;

  return info;
}

/* Given a GDB frame, determine the address of the calling function's
   frame.  This will be used to create a new GDB frame struct.  */

static void
alpha_heuristic_frame_this_id (struct frame_info *next_frame,
				 void **this_prologue_cache,
				 struct frame_id *this_id)
{
  struct alpha_heuristic_unwind_cache *info
    = alpha_heuristic_frame_unwind_cache (next_frame, this_prologue_cache, 0);

  *this_id = frame_id_build (info->vfp, info->start_pc);
}

/* Retrieve the value of REGNUM in FRAME.  Don't give up!  */

static void
alpha_heuristic_frame_prev_register (struct frame_info *next_frame,
				     void **this_prologue_cache,
				     int regnum, int *optimizedp,
				     enum lval_type *lvalp, CORE_ADDR *addrp,
				     int *realnump, void *bufferp)
{
  struct alpha_heuristic_unwind_cache *info
    = alpha_heuristic_frame_unwind_cache (next_frame, this_prologue_cache, 0);

  /* The PC of the previous frame is stored in the link register of
     the current frame.  Frob regnum so that we pull the value from
     the correct place.  */
  if (regnum == ALPHA_PC_REGNUM)
    regnum = info->return_reg;
  
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

static const struct frame_unwind alpha_heuristic_frame_unwind = {
  NORMAL_FRAME,
  alpha_heuristic_frame_this_id,
  alpha_heuristic_frame_prev_register
};

static const struct frame_unwind *
alpha_heuristic_frame_sniffer (struct frame_info *next_frame)
{
  return &alpha_heuristic_frame_unwind;
}

static CORE_ADDR
alpha_heuristic_frame_base_address (struct frame_info *next_frame,
				    void **this_prologue_cache)
{
  struct alpha_heuristic_unwind_cache *info
    = alpha_heuristic_frame_unwind_cache (next_frame, this_prologue_cache, 0);

  return info->vfp;
}

static const struct frame_base alpha_heuristic_frame_base = {
  &alpha_heuristic_frame_unwind,
  alpha_heuristic_frame_base_address,
  alpha_heuristic_frame_base_address,
  alpha_heuristic_frame_base_address
};

/* Just like reinit_frame_cache, but with the right arguments to be
   callable as an sfunc.  Used by the "set heuristic-fence-post" command.  */

static void
reinit_frame_cache_sfunc (char *args, int from_tty, struct cmd_list_element *c)
{
  reinit_frame_cache ();
}


/* ALPHA stack frames are almost impenetrable.  When execution stops,
   we basically have to look at symbol information for the function
   that we stopped in, which tells us *which* register (if any) is
   the base of the frame pointer, and what offset from that register
   the frame itself is at.  

   This presents a problem when trying to examine a stack in memory
   (that isn't executing at the moment), using the "frame" command.  We
   don't have a PC, nor do we have any registers except SP.

   This routine takes two arguments, SP and PC, and tries to make the
   cached frames look as if these two arguments defined a frame on the
   cache.  This allows the rest of info frame to extract the important
   arguments without difficulty.  */

struct frame_info *
alpha_setup_arbitrary_frame (int argc, CORE_ADDR *argv)
{
  if (argc != 2)
    error ("ALPHA frame specifications require two arguments: sp and pc");

  return create_new_frame (argv[0], argv[1]);
}

/* Assuming NEXT_FRAME->prev is a dummy, return the frame ID of that
   dummy frame.  The frame ID's base needs to match the TOS value
   saved by save_dummy_frame_tos(), and the PC match the dummy frame's
   breakpoint.  */

static struct frame_id
alpha_unwind_dummy_id (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  ULONGEST base;
  frame_unwind_unsigned_register (next_frame, ALPHA_SP_REGNUM, &base);
  return frame_id_build (base, frame_pc_unwind (next_frame));
}

static CORE_ADDR
alpha_unwind_pc (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  ULONGEST pc;
  frame_unwind_unsigned_register (next_frame, ALPHA_PC_REGNUM, &pc);
  return pc;
}


/* Helper routines for alpha*-nat.c files to move register sets to and
   from core files.  The UNIQUE pointer is allowed to be NULL, as most
   targets don't supply this value in their core files.  */

void
alpha_supply_int_regs (int regno, const void *r0_r30,
		       const void *pc, const void *unique)
{
  int i;

  for (i = 0; i < 31; ++i)
    if (regno == i || regno == -1)
      supply_register (i, (const char *)r0_r30 + i*8);

  if (regno == ALPHA_ZERO_REGNUM || regno == -1)
    supply_register (ALPHA_ZERO_REGNUM, NULL);

  if (regno == ALPHA_PC_REGNUM || regno == -1)
    supply_register (ALPHA_PC_REGNUM, pc);

  if (regno == ALPHA_UNIQUE_REGNUM || regno == -1)
    supply_register (ALPHA_UNIQUE_REGNUM, unique);
}

void
alpha_fill_int_regs (int regno, void *r0_r30, void *pc, void *unique)
{
  int i;

  for (i = 0; i < 31; ++i)
    if (regno == i || regno == -1)
      regcache_collect (i, (char *)r0_r30 + i*8);

  if (regno == ALPHA_PC_REGNUM || regno == -1)
    regcache_collect (ALPHA_PC_REGNUM, pc);

  if (unique && (regno == ALPHA_UNIQUE_REGNUM || regno == -1))
    regcache_collect (ALPHA_UNIQUE_REGNUM, unique);
}

void
alpha_supply_fp_regs (int regno, const void *f0_f30, const void *fpcr)
{
  int i;

  for (i = ALPHA_FP0_REGNUM; i < ALPHA_FP0_REGNUM + 31; ++i)
    if (regno == i || regno == -1)
      supply_register (i, (const char *)f0_f30 + (i - ALPHA_FP0_REGNUM) * 8);

  if (regno == ALPHA_FPCR_REGNUM || regno == -1)
    supply_register (ALPHA_FPCR_REGNUM, fpcr);
}

void
alpha_fill_fp_regs (int regno, void *f0_f30, void *fpcr)
{
  int i;

  for (i = ALPHA_FP0_REGNUM; i < ALPHA_FP0_REGNUM + 31; ++i)
    if (regno == i || regno == -1)
      regcache_collect (i, (char *)f0_f30 + (i - ALPHA_FP0_REGNUM) * 8);

  if (regno == ALPHA_FPCR_REGNUM || regno == -1)
    regcache_collect (ALPHA_FPCR_REGNUM, fpcr);
}


/* alpha_software_single_step() is called just before we want to resume
   the inferior, if we want to single-step it but there is no hardware
   or kernel single-step support (NetBSD on Alpha, for example).  We find
   the target of the coming instruction and breakpoint it.

   single_step is also called just after the inferior stops.  If we had
   set up a simulated single-step, we undo our damage.  */

static CORE_ADDR
alpha_next_pc (CORE_ADDR pc)
{
  unsigned int insn;
  unsigned int op;
  int offset;
  LONGEST rav;

  insn = alpha_read_insn (pc);

  /* Opcode is top 6 bits. */
  op = (insn >> 26) & 0x3f;

  if (op == 0x1a)
    {
      /* Jump format: target PC is:
	 RB & ~3  */
      return (read_register ((insn >> 16) & 0x1f) & ~3);
    }

  if ((op & 0x30) == 0x30)
    {
      /* Branch format: target PC is:
	 (new PC) + (4 * sext(displacement))  */
      if (op == 0x30 ||		/* BR */
	  op == 0x34)		/* BSR */
	{
 branch_taken:
          offset = (insn & 0x001fffff);
	  if (offset & 0x00100000)
	    offset  |= 0xffe00000;
	  offset *= 4;
	  return (pc + 4 + offset);
	}

      /* Need to determine if branch is taken; read RA.  */
      rav = (LONGEST) read_register ((insn >> 21) & 0x1f);
      switch (op)
	{
	case 0x38:		/* BLBC */
	  if ((rav & 1) == 0)
	    goto branch_taken;
	  break;
	case 0x3c:		/* BLBS */
	  if (rav & 1)
	    goto branch_taken;
	  break;
	case 0x39:		/* BEQ */
	  if (rav == 0)
	    goto branch_taken;
	  break;
	case 0x3d:		/* BNE */
	  if (rav != 0)
	    goto branch_taken;
	  break;
	case 0x3a:		/* BLT */
	  if (rav < 0)
	    goto branch_taken;
	  break;
	case 0x3b:		/* BLE */
	  if (rav <= 0)
	    goto branch_taken;
	  break;
	case 0x3f:		/* BGT */
	  if (rav > 0)
	    goto branch_taken;
	  break;
	case 0x3e:		/* BGE */
	  if (rav >= 0)
	    goto branch_taken;
	  break;

	/* ??? Missing floating-point branches.  */
	}
    }

  /* Not a branch or branch not taken; target PC is:
     pc + 4  */
  return (pc + 4);
}

void
alpha_software_single_step (enum target_signal sig, int insert_breakpoints_p)
{
  static CORE_ADDR next_pc;
  typedef char binsn_quantum[BREAKPOINT_MAX];
  static binsn_quantum break_mem;
  CORE_ADDR pc;

  if (insert_breakpoints_p)
    {
      pc = read_pc ();
      next_pc = alpha_next_pc (pc);

      target_insert_breakpoint (next_pc, break_mem);
    }
  else
    {
      target_remove_breakpoint (next_pc, break_mem);
      write_pc (next_pc);
    }
}


/* Initialize the current architecture based on INFO.  If possible, re-use an
   architecture from ARCHES, which is a list of architectures already created
   during this debugging session.

   Called e.g. at program startup, when reading a core file, and when reading
   a binary file.  */

static struct gdbarch *
alpha_gdbarch_init (struct gdbarch_info info, struct gdbarch_list *arches)
{
  struct gdbarch_tdep *tdep;
  struct gdbarch *gdbarch;

  /* Try to determine the ABI of the object we are loading.  */
  if (info.abfd != NULL && info.osabi == GDB_OSABI_UNKNOWN)
    {
      /* If it's an ECOFF file, assume it's OSF/1.  */
      if (bfd_get_flavour (info.abfd) == bfd_target_ecoff_flavour)
	info.osabi = GDB_OSABI_OSF1;
    }

  /* Find a candidate among extant architectures.  */
  arches = gdbarch_list_lookup_by_info (arches, &info);
  if (arches != NULL)
    return arches->gdbarch;

  tdep = xmalloc (sizeof (struct gdbarch_tdep));
  gdbarch = gdbarch_alloc (&info, tdep);

  /* Lowest text address.  This is used by heuristic_proc_start()
     to decide when to stop looking.  */
  tdep->vm_min_address = (CORE_ADDR) 0x120000000;

  tdep->dynamic_sigtramp_offset = NULL;
  tdep->sigcontext_addr = NULL;
  tdep->sc_pc_offset = 2 * 8;
  tdep->sc_regs_offset = 4 * 8;
  tdep->sc_fpregs_offset = tdep->sc_regs_offset + 32 * 8 + 8;

  tdep->jb_pc = -1;	/* longjmp support not enabled by default  */

  /* Type sizes */
  set_gdbarch_short_bit (gdbarch, 16);
  set_gdbarch_int_bit (gdbarch, 32);
  set_gdbarch_long_bit (gdbarch, 64);
  set_gdbarch_long_long_bit (gdbarch, 64);
  set_gdbarch_float_bit (gdbarch, 32);
  set_gdbarch_double_bit (gdbarch, 64);
  set_gdbarch_long_double_bit (gdbarch, 64);
  set_gdbarch_ptr_bit (gdbarch, 64);

  /* Register info */
  set_gdbarch_num_regs (gdbarch, ALPHA_NUM_REGS);
  set_gdbarch_sp_regnum (gdbarch, ALPHA_SP_REGNUM);
  set_gdbarch_pc_regnum (gdbarch, ALPHA_PC_REGNUM);
  set_gdbarch_fp0_regnum (gdbarch, ALPHA_FP0_REGNUM);

  set_gdbarch_register_name (gdbarch, alpha_register_name);
  set_gdbarch_deprecated_register_byte (gdbarch, alpha_register_byte);
  set_gdbarch_deprecated_register_raw_size (gdbarch, alpha_register_raw_size);
  set_gdbarch_deprecated_register_virtual_size (gdbarch, alpha_register_virtual_size);
  set_gdbarch_register_type (gdbarch, alpha_register_type);

  set_gdbarch_cannot_fetch_register (gdbarch, alpha_cannot_fetch_register);
  set_gdbarch_cannot_store_register (gdbarch, alpha_cannot_store_register);

  set_gdbarch_convert_register_p (gdbarch, alpha_convert_register_p);
  set_gdbarch_register_to_value (gdbarch, alpha_register_to_value);
  set_gdbarch_value_to_register (gdbarch, alpha_value_to_register);

  set_gdbarch_register_reggroup_p (gdbarch, alpha_register_reggroup_p);

  /* Prologue heuristics.  */
  set_gdbarch_skip_prologue (gdbarch, alpha_skip_prologue);

  /* Disassembler.  */
  set_gdbarch_print_insn (gdbarch, print_insn_alpha);

  /* Call info.  */

  set_gdbarch_use_struct_convention (gdbarch, always_use_struct_convention);
  set_gdbarch_extract_return_value (gdbarch, alpha_extract_return_value);
  set_gdbarch_store_return_value (gdbarch, alpha_store_return_value);
  set_gdbarch_deprecated_extract_struct_value_address (gdbarch, alpha_extract_struct_value_address);

  /* Settings for calling functions in the inferior.  */
  set_gdbarch_push_dummy_call (gdbarch, alpha_push_dummy_call);

  /* Methods for saving / extracting a dummy frame's ID.  */
  set_gdbarch_unwind_dummy_id (gdbarch, alpha_unwind_dummy_id);

  /* Return the unwound PC value.  */
  set_gdbarch_unwind_pc (gdbarch, alpha_unwind_pc);

  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);

  set_gdbarch_breakpoint_from_pc (gdbarch, alpha_breakpoint_from_pc);
  set_gdbarch_decr_pc_after_break (gdbarch, 4);

  /* Hook in ABI-specific overrides, if they have been registered.  */
  gdbarch_init_osabi (info, gdbarch);

  /* Now that we have tuned the configuration, set a few final things
     based on what the OS ABI has told us.  */

  if (tdep->jb_pc >= 0)
    set_gdbarch_get_longjmp_target (gdbarch, alpha_get_longjmp_target);

  frame_unwind_append_sniffer (gdbarch, alpha_sigtramp_frame_sniffer);
  frame_unwind_append_sniffer (gdbarch, alpha_heuristic_frame_sniffer);

  frame_base_set_default (gdbarch, &alpha_heuristic_frame_base);

  return gdbarch;
}

void
alpha_dwarf2_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  frame_unwind_append_sniffer (gdbarch, dwarf2_frame_sniffer);
  frame_base_append_sniffer (gdbarch, dwarf2_frame_base_sniffer);
}

extern initialize_file_ftype _initialize_alpha_tdep; /* -Wmissing-prototypes */

void
_initialize_alpha_tdep (void)
{
  struct cmd_list_element *c;

  gdbarch_register (bfd_arch_alpha, alpha_gdbarch_init, NULL);

  /* Let the user set the fence post for heuristic_proc_start.  */

  /* We really would like to have both "0" and "unlimited" work, but
     command.c doesn't deal with that.  So make it a var_zinteger
     because the user can always use "999999" or some such for unlimited.  */
  c = add_set_cmd ("heuristic-fence-post", class_support, var_zinteger,
		   (char *) &heuristic_fence_post,
		   "\
Set the distance searched for the start of a function.\n\
If you are debugging a stripped executable, GDB needs to search through the\n\
program for the start of a function.  This command sets the distance of the\n\
search.  The only need to set it is when debugging a stripped executable.",
		   &setlist);
  /* We need to throw away the frame cache when we set this, since it
     might change our ability to get backtraces.  */
  set_cmd_sfunc (c, reinit_frame_cache_sfunc);
  add_show_from_set (c, &showlist);
}
