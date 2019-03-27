/* Target-dependent code for the MIPS architecture, for GDB, the GNU Debugger.

   Copyright 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
   1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free Software
   Foundation, Inc.

   Contributed by Alessandro Forin(af@cs.cmu.edu) at CMU
   and by Per Bothner(bothner@cs.wisc.edu) at U.Wisconsin.

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
#include "gdb_string.h"
#include "gdb_assert.h"
#include "frame.h"
#include "inferior.h"
#include "symtab.h"
#include "value.h"
#include "gdbcmd.h"
#include "language.h"
#include "gdbcore.h"
#include "symfile.h"
#include "objfiles.h"
#include "gdbtypes.h"
#include "target.h"
#include "arch-utils.h"
#include "regcache.h"
#include "osabi.h"
#include "mips-tdep.h"
#include "block.h"
#include "reggroups.h"
#include "opcode/mips.h"
#include "elf/mips.h"
#include "elf-bfd.h"
#include "symcat.h"
#include "sim-regno.h"
#include "dis-asm.h"
#include "frame-unwind.h"
#include "frame-base.h"
#include "trad-frame.h"

static const struct objfile_data *mips_pdr_data;

static void set_reg_offset (CORE_ADDR *saved_regs, int regnum, CORE_ADDR off);
static struct type *mips_register_type (struct gdbarch *gdbarch, int regnum);

/* A useful bit in the CP0 status register (PS_REGNUM).  */
/* This bit is set if we are emulating 32-bit FPRs on a 64-bit chip.  */
#define ST0_FR (1 << 26)

/* The sizes of floating point registers.  */

enum
{
  MIPS_FPU_SINGLE_REGSIZE = 4,
  MIPS_FPU_DOUBLE_REGSIZE = 8
};


static const char *mips_abi_string;

static const char *mips_abi_strings[] = {
  "auto",
  "n32",
  "o32",
  "n64",
  "o64",
  "eabi32",
  "eabi64",
  NULL
};

struct frame_extra_info
{
  mips_extra_func_info_t proc_desc;
  int num_args;
};

/* Various MIPS ISA options (related to stack analysis) can be
   overridden dynamically.  Establish an enum/array for managing
   them. */

static const char size_auto[] = "auto";
static const char size_32[] = "32";
static const char size_64[] = "64";

static const char *size_enums[] = {
  size_auto,
  size_32,
  size_64,
  0
};

/* Some MIPS boards don't support floating point while others only
   support single-precision floating-point operations.  See also
   FP_REGISTER_DOUBLE. */

enum mips_fpu_type
{
  MIPS_FPU_DOUBLE,		/* Full double precision floating point.  */
  MIPS_FPU_SINGLE,		/* Single precision floating point (R4650).  */
  MIPS_FPU_NONE			/* No floating point.  */
};

#ifndef MIPS_DEFAULT_FPU_TYPE
#define MIPS_DEFAULT_FPU_TYPE MIPS_FPU_DOUBLE
#endif
static int mips_fpu_type_auto = 1;
static enum mips_fpu_type mips_fpu_type = MIPS_DEFAULT_FPU_TYPE;

static int mips_debug = 0;

/* MIPS specific per-architecture information */
struct gdbarch_tdep
{
  /* from the elf header */
  int elf_flags;

  /* mips options */
  enum mips_abi mips_abi;
  enum mips_abi found_abi;
  enum mips_fpu_type mips_fpu_type;
  int mips_last_arg_regnum;
  int mips_last_fp_arg_regnum;
  int mips_default_saved_regsize;
  int mips_fp_register_double;
  int mips_default_stack_argsize;
  int default_mask_address_p;
  /* Is the target using 64-bit raw integer registers but only
     storing a left-aligned 32-bit value in each?  */
  int mips64_transfers_32bit_regs_p;
  /* Indexes for various registers.  IRIX and embedded have
     different values.  This contains the "public" fields.  Don't
     add any that do not need to be public.  */
  const struct mips_regnum *regnum;
  /* Register names table for the current register set.  */
  const char **mips_processor_reg_names;
};

const struct mips_regnum *
mips_regnum (struct gdbarch *gdbarch)
{
  return gdbarch_tdep (gdbarch)->regnum;
}

static int
mips_fpa0_regnum (struct gdbarch *gdbarch)
{
  return mips_regnum (gdbarch)->fp0 + 12;
}

#define MIPS_EABI (gdbarch_tdep (current_gdbarch)->mips_abi == MIPS_ABI_EABI32 \
		   || gdbarch_tdep (current_gdbarch)->mips_abi == MIPS_ABI_EABI64)

#define MIPS_LAST_FP_ARG_REGNUM (gdbarch_tdep (current_gdbarch)->mips_last_fp_arg_regnum)

#define MIPS_LAST_ARG_REGNUM (gdbarch_tdep (current_gdbarch)->mips_last_arg_regnum)

#define MIPS_FPU_TYPE (gdbarch_tdep (current_gdbarch)->mips_fpu_type)

/* MIPS16 function addresses are odd (bit 0 is set).  Here are some
   functions to test, set, or clear bit 0 of addresses.  */

static CORE_ADDR
is_mips16_addr (CORE_ADDR addr)
{
  return ((addr) & 1);
}

static CORE_ADDR
make_mips16_addr (CORE_ADDR addr)
{
  return ((addr) | 1);
}

static CORE_ADDR
unmake_mips16_addr (CORE_ADDR addr)
{
  return ((addr) & ~1);
}

/* Return the contents of register REGNUM as a signed integer.  */

static LONGEST
read_signed_register (int regnum)
{
  void *buf = alloca (register_size (current_gdbarch, regnum));
  deprecated_read_register_gen (regnum, buf);
  return (extract_signed_integer
	  (buf, register_size (current_gdbarch, regnum)));
}

static LONGEST
read_signed_register_pid (int regnum, ptid_t ptid)
{
  ptid_t save_ptid;
  LONGEST retval;

  if (ptid_equal (ptid, inferior_ptid))
    return read_signed_register (regnum);

  save_ptid = inferior_ptid;

  inferior_ptid = ptid;

  retval = read_signed_register (regnum);

  inferior_ptid = save_ptid;

  return retval;
}

/* Return the MIPS ABI associated with GDBARCH.  */
enum mips_abi
mips_abi (struct gdbarch *gdbarch)
{
  return gdbarch_tdep (gdbarch)->mips_abi;
}

int
mips_regsize (struct gdbarch *gdbarch)
{
  return (gdbarch_bfd_arch_info (gdbarch)->bits_per_word
	  / gdbarch_bfd_arch_info (gdbarch)->bits_per_byte);
}

/* Return the currently configured (or set) saved register size. */

static const char *mips_saved_regsize_string = size_auto;

static unsigned int
mips_saved_regsize (struct gdbarch_tdep *tdep)
{
  if (mips_saved_regsize_string == size_auto)
    return tdep->mips_default_saved_regsize;
  else if (mips_saved_regsize_string == size_64)
    return 8;
  else				/* if (mips_saved_regsize_string == size_32) */
    return 4;
}

/* Functions for setting and testing a bit in a minimal symbol that
   marks it as 16-bit function.  The MSB of the minimal symbol's
   "info" field is used for this purpose.

   ELF_MAKE_MSYMBOL_SPECIAL tests whether an ELF symbol is "special",
   i.e. refers to a 16-bit function, and sets a "special" bit in a
   minimal symbol to mark it as a 16-bit function

   MSYMBOL_IS_SPECIAL   tests the "special" bit in a minimal symbol  */

static void
mips_elf_make_msymbol_special (asymbol * sym, struct minimal_symbol *msym)
{
  if (((elf_symbol_type *) (sym))->internal_elf_sym.st_other == STO_MIPS16)
    {
      MSYMBOL_INFO (msym) = (char *)
	(((long) MSYMBOL_INFO (msym)) | 0x80000000);
      SYMBOL_VALUE_ADDRESS (msym) |= 1;
    }
}

static int
msymbol_is_special (struct minimal_symbol *msym)
{
  return (((long) MSYMBOL_INFO (msym) & 0x80000000) != 0);
}

/* XFER a value from the big/little/left end of the register.
   Depending on the size of the value it might occupy the entire
   register or just part of it.  Make an allowance for this, aligning
   things accordingly.  */

static void
mips_xfer_register (struct regcache *regcache, int reg_num, int length,
		    enum bfd_endian endian, bfd_byte * in,
		    const bfd_byte * out, int buf_offset)
{
  int reg_offset = 0;
  gdb_assert (reg_num >= NUM_REGS);
  /* Need to transfer the left or right part of the register, based on
     the targets byte order.  */
  switch (endian)
    {
    case BFD_ENDIAN_BIG:
      reg_offset = register_size (current_gdbarch, reg_num) - length;
      break;
    case BFD_ENDIAN_LITTLE:
      reg_offset = 0;
      break;
    case BFD_ENDIAN_UNKNOWN:	/* Indicates no alignment.  */
      reg_offset = 0;
      break;
    default:
      internal_error (__FILE__, __LINE__, "bad switch");
    }
  if (mips_debug)
    fprintf_unfiltered (gdb_stderr,
			"xfer $%d, reg offset %d, buf offset %d, length %d, ",
			reg_num, reg_offset, buf_offset, length);
  if (mips_debug && out != NULL)
    {
      int i;
      fprintf_unfiltered (gdb_stdlog, "out ");
      for (i = 0; i < length; i++)
	fprintf_unfiltered (gdb_stdlog, "%02x", out[buf_offset + i]);
    }
  if (in != NULL)
    regcache_cooked_read_part (regcache, reg_num, reg_offset, length,
			       in + buf_offset);
  if (out != NULL)
    regcache_cooked_write_part (regcache, reg_num, reg_offset, length,
				out + buf_offset);
  if (mips_debug && in != NULL)
    {
      int i;
      fprintf_unfiltered (gdb_stdlog, "in ");
      for (i = 0; i < length; i++)
	fprintf_unfiltered (gdb_stdlog, "%02x", in[buf_offset + i]);
    }
  if (mips_debug)
    fprintf_unfiltered (gdb_stdlog, "\n");
}

/* Determine if a MIPS3 or later cpu is operating in MIPS{1,2} FPU
   compatiblity mode.  A return value of 1 means that we have
   physical 64-bit registers, but should treat them as 32-bit registers.  */

static int
mips2_fp_compat (void)
{
  /* MIPS1 and MIPS2 have only 32 bit FPRs, and the FR bit is not
     meaningful.  */
  if (register_size (current_gdbarch, mips_regnum (current_gdbarch)->fp0) ==
      4)
    return 0;

#if 0
  /* FIXME drow 2002-03-10: This is disabled until we can do it consistently,
     in all the places we deal with FP registers.  PR gdb/413.  */
  /* Otherwise check the FR bit in the status register - it controls
     the FP compatiblity mode.  If it is clear we are in compatibility
     mode.  */
  if ((read_register (PS_REGNUM) & ST0_FR) == 0)
    return 1;
#endif

  return 0;
}

/* Indicate that the ABI makes use of double-precision registers
   provided by the FPU (rather than combining pairs of registers to
   form double-precision values).  See also MIPS_FPU_TYPE.  */
#define FP_REGISTER_DOUBLE (gdbarch_tdep (current_gdbarch)->mips_fp_register_double)

/* The amount of space reserved on the stack for registers. This is
   different to MIPS_SAVED_REGSIZE as it determines the alignment of
   data allocated after the registers have run out. */

static const char *mips_stack_argsize_string = size_auto;

static unsigned int
mips_stack_argsize (struct gdbarch_tdep *tdep)
{
  if (mips_stack_argsize_string == size_auto)
    return tdep->mips_default_stack_argsize;
  else if (mips_stack_argsize_string == size_64)
    return 8;
  else				/* if (mips_stack_argsize_string == size_32) */
    return 4;
}

#define VM_MIN_ADDRESS (CORE_ADDR)0x400000

static mips_extra_func_info_t heuristic_proc_desc (CORE_ADDR, CORE_ADDR,
						   struct frame_info *, int);

static CORE_ADDR heuristic_proc_start (CORE_ADDR);

static CORE_ADDR read_next_frame_reg (struct frame_info *, int);

static void reinit_frame_cache_sfunc (char *, int, struct cmd_list_element *);

static mips_extra_func_info_t find_proc_desc (CORE_ADDR pc,
					      struct frame_info *next_frame,
					      int cur_frame);

static CORE_ADDR after_prologue (CORE_ADDR pc,
				 mips_extra_func_info_t proc_desc);

static struct type *mips_float_register_type (void);
static struct type *mips_double_register_type (void);

/* The list of available "set mips " and "show mips " commands */

static struct cmd_list_element *setmipscmdlist = NULL;
static struct cmd_list_element *showmipscmdlist = NULL;

/* Integer registers 0 thru 31 are handled explicitly by
   mips_register_name().  Processor specific registers 32 and above
   are listed in the followign tables.  */

enum
{ NUM_MIPS_PROCESSOR_REGS = (90 - 32) };

/* Generic MIPS.  */

static const char *mips_generic_reg_names[NUM_MIPS_PROCESSOR_REGS] = {
  "sr", "lo", "hi", "bad", "cause", "pc",
  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
  "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
  "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
  "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
  "fsr", "fir", "" /*"fp" */ , "",
  "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "",
};

/* Names of IDT R3041 registers.  */

static const char *mips_r3041_reg_names[] = {
  "sr", "lo", "hi", "bad", "cause", "pc",
  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
  "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
  "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
  "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
  "fsr", "fir", "", /*"fp" */ "",
  "", "", "bus", "ccfg", "", "", "", "",
  "", "", "port", "cmp", "", "", "epc", "prid",
};

/* Names of tx39 registers.  */

static const char *mips_tx39_reg_names[NUM_MIPS_PROCESSOR_REGS] = {
  "sr", "lo", "hi", "bad", "cause", "pc",
  "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "",
  "", "", "", "",
  "", "", "", "", "", "", "", "",
  "", "", "config", "cache", "debug", "depc", "epc", ""
};

/* Names of IRIX registers.  */
static const char *mips_irix_reg_names[NUM_MIPS_PROCESSOR_REGS] = {
  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
  "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
  "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
  "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
  "pc", "cause", "bad", "hi", "lo", "fsr", "fir"
};


/* Return the name of the register corresponding to REGNO.  */
static const char *
mips_register_name (int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  /* GPR names for all ABIs other than n32/n64.  */
  static char *mips_gpr_names[] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra",
  };

  /* GPR names for n32 and n64 ABIs.  */
  static char *mips_n32_n64_gpr_names[] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "a4", "a5", "a6", "a7", "t0", "t1", "t2", "t3",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra"
  };

  enum mips_abi abi = mips_abi (current_gdbarch);

  /* Map [NUM_REGS .. 2*NUM_REGS) onto the raw registers, but then
     don't make the raw register names visible.  */
  int rawnum = regno % NUM_REGS;
  if (regno < NUM_REGS)
    return "";

  /* The MIPS integer registers are always mapped from 0 to 31.  The
     names of the registers (which reflects the conventions regarding
     register use) vary depending on the ABI.  */
  if (0 <= rawnum && rawnum < 32)
    {
      if (abi == MIPS_ABI_N32 || abi == MIPS_ABI_N64)
	return mips_n32_n64_gpr_names[rawnum];
      else
	return mips_gpr_names[rawnum];
    }
  else if (32 <= rawnum && rawnum < NUM_REGS)
    {
      gdb_assert (rawnum - 32 < NUM_MIPS_PROCESSOR_REGS);
      return tdep->mips_processor_reg_names[rawnum - 32];
    }
  else
    internal_error (__FILE__, __LINE__,
		    "mips_register_name: bad register number %d", rawnum);
}

/* Return the groups that a MIPS register can be categorised into.  */

static int
mips_register_reggroup_p (struct gdbarch *gdbarch, int regnum,
			  struct reggroup *reggroup)
{
  int vector_p;
  int float_p;
  int raw_p;
  int rawnum = regnum % NUM_REGS;
  int pseudo = regnum / NUM_REGS;
  if (reggroup == all_reggroup)
    return pseudo;
  vector_p = TYPE_VECTOR (register_type (gdbarch, regnum));
  float_p = TYPE_CODE (register_type (gdbarch, regnum)) == TYPE_CODE_FLT;
  /* FIXME: cagney/2003-04-13: Can't yet use gdbarch_num_regs
     (gdbarch), as not all architectures are multi-arch.  */
  raw_p = rawnum < NUM_REGS;
  if (REGISTER_NAME (regnum) == NULL || REGISTER_NAME (regnum)[0] == '\0')
    return 0;
  if (reggroup == float_reggroup)
    return float_p && pseudo;
  if (reggroup == vector_reggroup)
    return vector_p && pseudo;
  if (reggroup == general_reggroup)
    return (!vector_p && !float_p) && pseudo;
  /* Save the pseudo registers.  Need to make certain that any code
     extracting register values from a saved register cache also uses
     pseudo registers.  */
  if (reggroup == save_reggroup)
    return raw_p && pseudo;
  /* Restore the same pseudo register.  */
  if (reggroup == restore_reggroup)
    return raw_p && pseudo;
  return 0;
}

/* Map the symbol table registers which live in the range [1 *
   NUM_REGS .. 2 * NUM_REGS) back onto the corresponding raw
   registers.  Take care of alignment and size problems.  */

static void
mips_pseudo_register_read (struct gdbarch *gdbarch, struct regcache *regcache,
			   int cookednum, void *buf)
{
  int rawnum = cookednum % NUM_REGS;
  gdb_assert (cookednum >= NUM_REGS && cookednum < 2 * NUM_REGS);
  if (register_size (gdbarch, rawnum) == register_size (gdbarch, cookednum))
    regcache_raw_read (regcache, rawnum, buf);
  else if (register_size (gdbarch, rawnum) >
	   register_size (gdbarch, cookednum))
    {
      if (gdbarch_tdep (gdbarch)->mips64_transfers_32bit_regs_p
	  || TARGET_BYTE_ORDER == BFD_ENDIAN_LITTLE)
	regcache_raw_read_part (regcache, rawnum, 0, 4, buf);
      else
	regcache_raw_read_part (regcache, rawnum, 4, 4, buf);
    }
  else
    internal_error (__FILE__, __LINE__, "bad register size");
}

static void
mips_pseudo_register_write (struct gdbarch *gdbarch,
			    struct regcache *regcache, int cookednum,
			    const void *buf)
{
  int rawnum = cookednum % NUM_REGS;
  gdb_assert (cookednum >= NUM_REGS && cookednum < 2 * NUM_REGS);
  if (register_size (gdbarch, rawnum) == register_size (gdbarch, cookednum))
    regcache_raw_write (regcache, rawnum, buf);
  else if (register_size (gdbarch, rawnum) >
	   register_size (gdbarch, cookednum))
    {
      if (gdbarch_tdep (gdbarch)->mips64_transfers_32bit_regs_p
	  || TARGET_BYTE_ORDER == BFD_ENDIAN_LITTLE)
	regcache_raw_write_part (regcache, rawnum, 0, 4, buf);
      else
	regcache_raw_write_part (regcache, rawnum, 4, 4, buf);
    }
  else
    internal_error (__FILE__, __LINE__, "bad register size");
}

/* Table to translate MIPS16 register field to actual register number.  */
static int mips16_to_32_reg[8] = { 16, 17, 2, 3, 4, 5, 6, 7 };

/* Heuristic_proc_start may hunt through the text section for a long
   time across a 2400 baud serial line.  Allows the user to limit this
   search.  */

static unsigned int heuristic_fence_post = 0;

#define PROC_LOW_ADDR(proc) ((proc)->pdr.adr)	/* least address */
#define PROC_HIGH_ADDR(proc) ((proc)->high_addr)	/* upper address bound */
#define PROC_FRAME_OFFSET(proc) ((proc)->pdr.frameoffset)
#define PROC_FRAME_REG(proc) ((proc)->pdr.framereg)
#define PROC_FRAME_ADJUST(proc)  ((proc)->frame_adjust)
#define PROC_REG_MASK(proc) ((proc)->pdr.regmask)
#define PROC_FREG_MASK(proc) ((proc)->pdr.fregmask)
#define PROC_REG_OFFSET(proc) ((proc)->pdr.regoffset)
#define PROC_FREG_OFFSET(proc) ((proc)->pdr.fregoffset)
#define PROC_PC_REG(proc) ((proc)->pdr.pcreg)
/* FIXME drow/2002-06-10: If a pointer on the host is bigger than a long,
   this will corrupt pdr.iline.  Fortunately we don't use it.  */
#define PROC_SYMBOL(proc) (*(struct symbol**)&(proc)->pdr.isym)
#define _PROC_MAGIC_ 0x0F0F0F0F
#define PROC_DESC_IS_DUMMY(proc) ((proc)->pdr.isym == _PROC_MAGIC_)
#define SET_PROC_DESC_IS_DUMMY(proc) ((proc)->pdr.isym = _PROC_MAGIC_)

struct linked_proc_info
{
  struct mips_extra_func_info info;
  struct linked_proc_info *next;
}
 *linked_proc_desc_table = NULL;

/* Number of bytes of storage in the actual machine representation for
   register N.  NOTE: This defines the pseudo register type so need to
   rebuild the architecture vector.  */

static int mips64_transfers_32bit_regs_p = 0;

static void
set_mips64_transfers_32bit_regs (char *args, int from_tty,
				 struct cmd_list_element *c)
{
  struct gdbarch_info info;
  gdbarch_info_init (&info);
  /* FIXME: cagney/2003-11-15: Should be setting a field in "info"
     instead of relying on globals.  Doing that would let generic code
     handle the search for this specific architecture.  */
  if (!gdbarch_update_p (info))
    {
      mips64_transfers_32bit_regs_p = 0;
      error ("32-bit compatibility mode not supported");
    }
}

/* Convert to/from a register and the corresponding memory value.  */

static int
mips_convert_register_p (int regnum, struct type *type)
{
  return (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
	  && register_size (current_gdbarch, regnum) == 4
	  && (regnum % NUM_REGS) >= mips_regnum (current_gdbarch)->fp0
	  && (regnum % NUM_REGS) < mips_regnum (current_gdbarch)->fp0 + 32
	  && TYPE_CODE (type) == TYPE_CODE_FLT && TYPE_LENGTH (type) == 8);
}

static void
mips_register_to_value (struct frame_info *frame, int regnum,
			struct type *type, void *to)
{
  get_frame_register (frame, regnum + 0, (char *) to + 4);
  get_frame_register (frame, regnum + 1, (char *) to + 0);
}

static void
mips_value_to_register (struct frame_info *frame, int regnum,
			struct type *type, const void *from)
{
  put_frame_register (frame, regnum + 0, (const char *) from + 4);
  put_frame_register (frame, regnum + 1, (const char *) from + 0);
}

/* Return the GDB type object for the "standard" data type of data in
   register REG.  */

static struct type *
mips_register_type (struct gdbarch *gdbarch, int regnum)
{
  gdb_assert (regnum >= 0 && regnum < 2 * NUM_REGS);
  if ((regnum % NUM_REGS) >= mips_regnum (current_gdbarch)->fp0
      && (regnum % NUM_REGS) < mips_regnum (current_gdbarch)->fp0 + 32)
    {
      /* The floating-point registers raw, or cooked, always match
         mips_regsize(), and also map 1:1, byte for byte.  */
      switch (gdbarch_byte_order (gdbarch))
	{
	case BFD_ENDIAN_BIG:
	  if (mips_regsize (gdbarch) == 4)
	    return builtin_type_ieee_single_big;
	  else
	    return builtin_type_ieee_double_big;
	case BFD_ENDIAN_LITTLE:
	  if (mips_regsize (gdbarch) == 4)
	    return builtin_type_ieee_single_little;
	  else
	    return builtin_type_ieee_double_little;
	case BFD_ENDIAN_UNKNOWN:
	default:
	  internal_error (__FILE__, __LINE__, "bad switch");
	}
    }
  else if (regnum >=
	   (NUM_REGS + mips_regnum (current_gdbarch)->fp_control_status)
	   && regnum <= NUM_REGS + LAST_EMBED_REGNUM)
    /* The pseudo/cooked view of the embedded registers is always
       32-bit.  The raw view is handled below.  */
    return builtin_type_int32;
  else if (regnum >= NUM_REGS && mips_regsize (gdbarch)
	   && gdbarch_tdep (gdbarch)->mips64_transfers_32bit_regs_p)
    /* The target, while using a 64-bit register buffer, is only
       transfering 32-bits of each integer register.  Reflect this in
       the cooked/pseudo register value.  */
    return builtin_type_int32;
  else if (mips_regsize (gdbarch) == 8)
    /* 64-bit ISA.  */
    return builtin_type_int64;
  else
    /* 32-bit ISA.  */
    return builtin_type_int32;
}

/* TARGET_READ_SP -- Remove useless bits from the stack pointer.  */

static CORE_ADDR
mips_read_sp (void)
{
  return read_signed_register (SP_REGNUM);
}

/* Should the upper word of 64-bit addresses be zeroed? */
enum auto_boolean mask_address_var = AUTO_BOOLEAN_AUTO;

static int
mips_mask_address_p (struct gdbarch_tdep *tdep)
{
  switch (mask_address_var)
    {
    case AUTO_BOOLEAN_TRUE:
      return 1;
    case AUTO_BOOLEAN_FALSE:
      return 0;
      break;
    case AUTO_BOOLEAN_AUTO:
      return tdep->default_mask_address_p;
    default:
      internal_error (__FILE__, __LINE__, "mips_mask_address_p: bad switch");
      return -1;
    }
}

static void
show_mask_address (char *cmd, int from_tty, struct cmd_list_element *c)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  switch (mask_address_var)
    {
    case AUTO_BOOLEAN_TRUE:
      printf_filtered ("The 32 bit mips address mask is enabled\n");
      break;
    case AUTO_BOOLEAN_FALSE:
      printf_filtered ("The 32 bit mips address mask is disabled\n");
      break;
    case AUTO_BOOLEAN_AUTO:
      printf_filtered
	("The 32 bit address mask is set automatically.  Currently %s\n",
	 mips_mask_address_p (tdep) ? "enabled" : "disabled");
      break;
    default:
      internal_error (__FILE__, __LINE__, "show_mask_address: bad switch");
      break;
    }
}

/* Tell if the program counter value in MEMADDR is in a MIPS16 function.  */

static int
pc_is_mips16 (bfd_vma memaddr)
{
  struct minimal_symbol *sym;

  /* If bit 0 of the address is set, assume this is a MIPS16 address. */
  if (is_mips16_addr (memaddr))
    return 1;

  /* A flag indicating that this is a MIPS16 function is stored by elfread.c in
     the high bit of the info field.  Use this to decide if the function is
     MIPS16 or normal MIPS.  */
  sym = lookup_minimal_symbol_by_pc (memaddr);
  if (sym)
    return msymbol_is_special (sym);
  else
    return 0;
}

/* MIPS believes that the PC has a sign extended value.  Perhaphs the
   all registers should be sign extended for simplicity? */

static CORE_ADDR
mips_read_pc (ptid_t ptid)
{
  return read_signed_register_pid (mips_regnum (current_gdbarch)->pc, ptid);
}

static CORE_ADDR
mips_unwind_pc (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  return frame_unwind_register_signed (next_frame,
				       NUM_REGS + mips_regnum (gdbarch)->pc);
}

/* Assuming NEXT_FRAME->prev is a dummy, return the frame ID of that
   dummy frame.  The frame ID's base needs to match the TOS value
   saved by save_dummy_frame_tos(), and the PC match the dummy frame's
   breakpoint.  */

static struct frame_id
mips_unwind_dummy_id (struct gdbarch *gdbarch, struct frame_info *next_frame)
{
  return frame_id_build (frame_unwind_register_signed (next_frame, NUM_REGS + SP_REGNUM),
			 frame_pc_unwind (next_frame));
}

static void
mips_write_pc (CORE_ADDR pc, ptid_t ptid)
{
  write_register_pid (mips_regnum (current_gdbarch)->pc, pc, ptid);
}

/* This returns the PC of the first inst after the prologue.  If we can't
   find the prologue, then return 0.  */

static CORE_ADDR
after_prologue (CORE_ADDR pc, mips_extra_func_info_t proc_desc)
{
  struct symtab_and_line sal;
  CORE_ADDR func_addr, func_end;

  /* Pass cur_frame == 0 to find_proc_desc.  We should not attempt
     to read the stack pointer from the current machine state, because
     the current machine state has nothing to do with the information
     we need from the proc_desc; and the process may or may not exist
     right now.  */
  if (!proc_desc)
    proc_desc = find_proc_desc (pc, NULL, 0);

  if (proc_desc)
    {
      /* If function is frameless, then we need to do it the hard way.  I
         strongly suspect that frameless always means prologueless... */
      if (PROC_FRAME_REG (proc_desc) == SP_REGNUM
	  && PROC_FRAME_OFFSET (proc_desc) == 0)
	return 0;
    }

  if (!find_pc_partial_function (pc, NULL, &func_addr, &func_end))
    return 0;			/* Unknown */

  sal = find_pc_line (func_addr, 0);

  if (sal.end < func_end)
    return sal.end;

  /* The line after the prologue is after the end of the function.  In this
     case, tell the caller to find the prologue the hard way.  */

  return 0;
}

/* Decode a MIPS32 instruction that saves a register in the stack, and
   set the appropriate bit in the general register mask or float register mask
   to indicate which register is saved.  This is a helper function
   for mips_find_saved_regs.  */

static void
mips32_decode_reg_save (t_inst inst, unsigned long *gen_mask,
			unsigned long *float_mask)
{
  int reg;

  if ((inst & 0xffe00000) == 0xafa00000	/* sw reg,n($sp) */
      || (inst & 0xffe00000) == 0xafc00000	/* sw reg,n($r30) */
      || (inst & 0xffe00000) == 0xffa00000)	/* sd reg,n($sp) */
    {
      /* It might be possible to use the instruction to
         find the offset, rather than the code below which
         is based on things being in a certain order in the
         frame, but figuring out what the instruction's offset
         is relative to might be a little tricky.  */
      reg = (inst & 0x001f0000) >> 16;
      *gen_mask |= (1 << reg);
    }
  else if ((inst & 0xffe00000) == 0xe7a00000	/* swc1 freg,n($sp) */
	   || (inst & 0xffe00000) == 0xe7c00000	/* swc1 freg,n($r30) */
	   || (inst & 0xffe00000) == 0xf7a00000)	/* sdc1 freg,n($sp) */

    {
      reg = ((inst & 0x001f0000) >> 16);
      *float_mask |= (1 << reg);
    }
}

/* Decode a MIPS16 instruction that saves a register in the stack, and
   set the appropriate bit in the general register or float register mask
   to indicate which register is saved.  This is a helper function
   for mips_find_saved_regs.  */

static void
mips16_decode_reg_save (t_inst inst, unsigned long *gen_mask)
{
  if ((inst & 0xf800) == 0xd000)	/* sw reg,n($sp) */
    {
      int reg = mips16_to_32_reg[(inst & 0x700) >> 8];
      *gen_mask |= (1 << reg);
    }
  else if ((inst & 0xff00) == 0xf900)	/* sd reg,n($sp) */
    {
      int reg = mips16_to_32_reg[(inst & 0xe0) >> 5];
      *gen_mask |= (1 << reg);
    }
  else if ((inst & 0xff00) == 0x6200	/* sw $ra,n($sp) */
	   || (inst & 0xff00) == 0xfa00)	/* sd $ra,n($sp) */
    *gen_mask |= (1 << RA_REGNUM);
}


/* Fetch and return instruction from the specified location.  If the PC
   is odd, assume it's a MIPS16 instruction; otherwise MIPS32.  */

static t_inst
mips_fetch_instruction (CORE_ADDR addr)
{
  char buf[MIPS_INSTLEN];
  int instlen;
  int status;

  if (pc_is_mips16 (addr))
    {
      instlen = MIPS16_INSTLEN;
      addr = unmake_mips16_addr (addr);
    }
  else
    instlen = MIPS_INSTLEN;
  status = read_memory_nobpt (addr, buf, instlen);
  if (status)
    memory_error (status, addr);
  return extract_unsigned_integer (buf, instlen);
}

static ULONGEST
mips16_fetch_instruction (CORE_ADDR addr)
{
  char buf[MIPS_INSTLEN];
  int instlen;
  int status;

  instlen = MIPS16_INSTLEN;
  addr = unmake_mips16_addr (addr);
  status = read_memory_nobpt (addr, buf, instlen);
  if (status)
    memory_error (status, addr);
  return extract_unsigned_integer (buf, instlen);
}

static ULONGEST
mips32_fetch_instruction (CORE_ADDR addr)
{
  char buf[MIPS_INSTLEN];
  int instlen;
  int status;
  instlen = MIPS_INSTLEN;
  status = read_memory_nobpt (addr, buf, instlen);
  if (status)
    memory_error (status, addr);
  return extract_unsigned_integer (buf, instlen);
}


/* These the fields of 32 bit mips instructions */
#define mips32_op(x) (x >> 26)
#define itype_op(x) (x >> 26)
#define itype_rs(x) ((x >> 21) & 0x1f)
#define itype_rt(x) ((x >> 16) & 0x1f)
#define itype_immediate(x) (x & 0xffff)

#define jtype_op(x) (x >> 26)
#define jtype_target(x) (x & 0x03ffffff)

#define rtype_op(x) (x >> 26)
#define rtype_rs(x) ((x >> 21) & 0x1f)
#define rtype_rt(x) ((x >> 16) & 0x1f)
#define rtype_rd(x) ((x >> 11) & 0x1f)
#define rtype_shamt(x) ((x >> 6) & 0x1f)
#define rtype_funct(x) (x & 0x3f)

static CORE_ADDR
mips32_relative_offset (unsigned long inst)
{
  long x;
  x = itype_immediate (inst);
  if (x & 0x8000)		/* sign bit set */
    {
      x |= 0xffff0000;		/* sign extension */
    }
  x = x << 2;
  return x;
}

/* Determine whate to set a single step breakpoint while considering
   branch prediction */
static CORE_ADDR
mips32_next_pc (CORE_ADDR pc)
{
  unsigned long inst;
  int op;
  inst = mips_fetch_instruction (pc);
  if ((inst & 0xe0000000) != 0)	/* Not a special, jump or branch instruction */
    {
      if (itype_op (inst) >> 2 == 5)
	/* BEQL, BNEL, BLEZL, BGTZL: bits 0101xx */
	{
	  op = (itype_op (inst) & 0x03);
	  switch (op)
	    {
	    case 0:		/* BEQL */
	      goto equal_branch;
	    case 1:		/* BNEL */
	      goto neq_branch;
	    case 2:		/* BLEZL */
	      goto less_branch;
	    case 3:		/* BGTZ */
	      goto greater_branch;
	    default:
	      pc += 4;
	    }
	}
      else if (itype_op (inst) == 17 && itype_rs (inst) == 8)
	/* BC1F, BC1FL, BC1T, BC1TL: 010001 01000 */
	{
	  int tf = itype_rt (inst) & 0x01;
	  int cnum = itype_rt (inst) >> 2;
	  int fcrcs =
	    read_signed_register (mips_regnum (current_gdbarch)->
				  fp_control_status);
	  int cond = ((fcrcs >> 24) & 0x0e) | ((fcrcs >> 23) & 0x01);

	  if (((cond >> cnum) & 0x01) == tf)
	    pc += mips32_relative_offset (inst) + 4;
	  else
	    pc += 8;
	}
      else
	pc += 4;		/* Not a branch, next instruction is easy */
    }
  else
    {				/* This gets way messy */

      /* Further subdivide into SPECIAL, REGIMM and other */
      switch (op = itype_op (inst) & 0x07)	/* extract bits 28,27,26 */
	{
	case 0:		/* SPECIAL */
	  op = rtype_funct (inst);
	  switch (op)
	    {
	    case 8:		/* JR */
	    case 9:		/* JALR */
	      /* Set PC to that address */
	      pc = read_signed_register (rtype_rs (inst));
	      break;
	    default:
	      pc += 4;
	    }

	  break;		/* end SPECIAL */
	case 1:		/* REGIMM */
	  {
	    op = itype_rt (inst);	/* branch condition */
	    switch (op)
	      {
	      case 0:		/* BLTZ */
	      case 2:		/* BLTZL */
	      case 16:		/* BLTZAL */
	      case 18:		/* BLTZALL */
	      less_branch:
		if (read_signed_register (itype_rs (inst)) < 0)
		  pc += mips32_relative_offset (inst) + 4;
		else
		  pc += 8;	/* after the delay slot */
		break;
	      case 1:		/* BGEZ */
	      case 3:		/* BGEZL */
	      case 17:		/* BGEZAL */
	      case 19:		/* BGEZALL */
		if (read_signed_register (itype_rs (inst)) >= 0)
		  pc += mips32_relative_offset (inst) + 4;
		else
		  pc += 8;	/* after the delay slot */
		break;
		/* All of the other instructions in the REGIMM category */
	      default:
		pc += 4;
	      }
	  }
	  break;		/* end REGIMM */
	case 2:		/* J */
	case 3:		/* JAL */
	  {
	    unsigned long reg;
	    reg = jtype_target (inst) << 2;
	    /* Upper four bits get never changed... */
	    pc = reg + ((pc + 4) & 0xf0000000);
	  }
	  break;
	  /* FIXME case JALX : */
	  {
	    unsigned long reg;
	    reg = jtype_target (inst) << 2;
	    pc = reg + ((pc + 4) & 0xf0000000) + 1;	/* yes, +1 */
	    /* Add 1 to indicate 16 bit mode - Invert ISA mode */
	  }
	  break;		/* The new PC will be alternate mode */
	case 4:		/* BEQ, BEQL */
	equal_branch:
	  if (read_signed_register (itype_rs (inst)) ==
	      read_signed_register (itype_rt (inst)))
	    pc += mips32_relative_offset (inst) + 4;
	  else
	    pc += 8;
	  break;
	case 5:		/* BNE, BNEL */
	neq_branch:
	  if (read_signed_register (itype_rs (inst)) !=
	      read_signed_register (itype_rt (inst)))
	    pc += mips32_relative_offset (inst) + 4;
	  else
	    pc += 8;
	  break;
	case 6:		/* BLEZ, BLEZL */
	  if (read_signed_register (itype_rs (inst) <= 0))
	    pc += mips32_relative_offset (inst) + 4;
	  else
	    pc += 8;
	  break;
	case 7:
	default:
	greater_branch:	/* BGTZ, BGTZL */
	  if (read_signed_register (itype_rs (inst) > 0))
	    pc += mips32_relative_offset (inst) + 4;
	  else
	    pc += 8;
	  break;
	}			/* switch */
    }				/* else */
  return pc;
}				/* mips32_next_pc */

/* Decoding the next place to set a breakpoint is irregular for the
   mips 16 variant, but fortunately, there fewer instructions. We have to cope
   ith extensions for 16 bit instructions and a pair of actual 32 bit instructions.
   We dont want to set a single step instruction on the extend instruction
   either.
 */

/* Lots of mips16 instruction formats */
/* Predicting jumps requires itype,ritype,i8type
   and their extensions      extItype,extritype,extI8type
 */
enum mips16_inst_fmts
{
  itype,			/* 0  immediate 5,10 */
  ritype,			/* 1   5,3,8 */
  rrtype,			/* 2   5,3,3,5 */
  rritype,			/* 3   5,3,3,5 */
  rrrtype,			/* 4   5,3,3,3,2 */
  rriatype,			/* 5   5,3,3,1,4 */
  shifttype,			/* 6   5,3,3,3,2 */
  i8type,			/* 7   5,3,8 */
  i8movtype,			/* 8   5,3,3,5 */
  i8mov32rtype,			/* 9   5,3,5,3 */
  i64type,			/* 10  5,3,8 */
  ri64type,			/* 11  5,3,3,5 */
  jalxtype,			/* 12  5,1,5,5,16 - a 32 bit instruction */
  exiItype,			/* 13  5,6,5,5,1,1,1,1,1,1,5 */
  extRitype,			/* 14  5,6,5,5,3,1,1,1,5 */
  extRRItype,			/* 15  5,5,5,5,3,3,5 */
  extRRIAtype,			/* 16  5,7,4,5,3,3,1,4 */
  EXTshifttype,			/* 17  5,5,1,1,1,1,1,1,5,3,3,1,1,1,2 */
  extI8type,			/* 18  5,6,5,5,3,1,1,1,5 */
  extI64type,			/* 19  5,6,5,5,3,1,1,1,5 */
  extRi64type,			/* 20  5,6,5,5,3,3,5 */
  extshift64type		/* 21  5,5,1,1,1,1,1,1,5,1,1,1,3,5 */
};
/* I am heaping all the fields of the formats into one structure and
   then, only the fields which are involved in instruction extension */
struct upk_mips16
{
  CORE_ADDR offset;
  unsigned int regx;		/* Function in i8 type */
  unsigned int regy;
};


/* The EXT-I, EXT-ri nad EXT-I8 instructions all have the same format
   for the bits which make up the immediatate extension.  */

static CORE_ADDR
extended_offset (unsigned int extension)
{
  CORE_ADDR value;
  value = (extension >> 21) & 0x3f;	/* * extract 15:11 */
  value = value << 6;
  value |= (extension >> 16) & 0x1f;	/* extrace 10:5 */
  value = value << 5;
  value |= extension & 0x01f;	/* extract 4:0 */
  return value;
}

/* Only call this function if you know that this is an extendable
   instruction, It wont malfunction, but why make excess remote memory references?
   If the immediate operands get sign extended or somthing, do it after
   the extension is performed.
 */
/* FIXME: Every one of these cases needs to worry about sign extension
   when the offset is to be used in relative addressing */


static unsigned int
fetch_mips_16 (CORE_ADDR pc)
{
  char buf[8];
  pc &= 0xfffffffe;		/* clear the low order bit */
  target_read_memory (pc, buf, 2);
  return extract_unsigned_integer (buf, 2);
}

static void
unpack_mips16 (CORE_ADDR pc,
	       unsigned int extension,
	       unsigned int inst,
	       enum mips16_inst_fmts insn_format, struct upk_mips16 *upk)
{
  CORE_ADDR offset;
  int regx;
  int regy;
  switch (insn_format)
    {
    case itype:
      {
	CORE_ADDR value;
	if (extension)
	  {
	    value = extended_offset (extension);
	    value = value << 11;	/* rom for the original value */
	    value |= inst & 0x7ff;	/* eleven bits from instruction */
	  }
	else
	  {
	    value = inst & 0x7ff;
	    /* FIXME : Consider sign extension */
	  }
	offset = value;
	regx = -1;
	regy = -1;
      }
      break;
    case ritype:
    case i8type:
      {				/* A register identifier and an offset */
	/* Most of the fields are the same as I type but the
	   immediate value is of a different length */
	CORE_ADDR value;
	if (extension)
	  {
	    value = extended_offset (extension);
	    value = value << 8;	/* from the original instruction */
	    value |= inst & 0xff;	/* eleven bits from instruction */
	    regx = (extension >> 8) & 0x07;	/* or i8 funct */
	    if (value & 0x4000)	/* test the sign bit , bit 26 */
	      {
		value &= ~0x3fff;	/* remove the sign bit */
		value = -value;
	      }
	  }
	else
	  {
	    value = inst & 0xff;	/* 8 bits */
	    regx = (inst >> 8) & 0x07;	/* or i8 funct */
	    /* FIXME: Do sign extension , this format needs it */
	    if (value & 0x80)	/* THIS CONFUSES ME */
	      {
		value &= 0xef;	/* remove the sign bit */
		value = -value;
	      }
	  }
	offset = value;
	regy = -1;
	break;
      }
    case jalxtype:
      {
	unsigned long value;
	unsigned int nexthalf;
	value = ((inst & 0x1f) << 5) | ((inst >> 5) & 0x1f);
	value = value << 16;
	nexthalf = mips_fetch_instruction (pc + 2);	/* low bit still set */
	value |= nexthalf;
	offset = value;
	regx = -1;
	regy = -1;
	break;
      }
    default:
      internal_error (__FILE__, __LINE__, "bad switch");
    }
  upk->offset = offset;
  upk->regx = regx;
  upk->regy = regy;
}


static CORE_ADDR
add_offset_16 (CORE_ADDR pc, int offset)
{
  return ((offset << 2) | ((pc + 2) & (0xf0000000)));
}

static CORE_ADDR
extended_mips16_next_pc (CORE_ADDR pc,
			 unsigned int extension, unsigned int insn)
{
  int op = (insn >> 11);
  switch (op)
    {
    case 2:			/* Branch */
      {
	CORE_ADDR offset;
	struct upk_mips16 upk;
	unpack_mips16 (pc, extension, insn, itype, &upk);
	offset = upk.offset;
	if (offset & 0x800)
	  {
	    offset &= 0xeff;
	    offset = -offset;
	  }
	pc += (offset << 1) + 2;
	break;
      }
    case 3:			/* JAL , JALX - Watch out, these are 32 bit instruction */
      {
	struct upk_mips16 upk;
	unpack_mips16 (pc, extension, insn, jalxtype, &upk);
	pc = add_offset_16 (pc, upk.offset);
	if ((insn >> 10) & 0x01)	/* Exchange mode */
	  pc = pc & ~0x01;	/* Clear low bit, indicate 32 bit mode */
	else
	  pc |= 0x01;
	break;
      }
    case 4:			/* beqz */
      {
	struct upk_mips16 upk;
	int reg;
	unpack_mips16 (pc, extension, insn, ritype, &upk);
	reg = read_signed_register (upk.regx);
	if (reg == 0)
	  pc += (upk.offset << 1) + 2;
	else
	  pc += 2;
	break;
      }
    case 5:			/* bnez */
      {
	struct upk_mips16 upk;
	int reg;
	unpack_mips16 (pc, extension, insn, ritype, &upk);
	reg = read_signed_register (upk.regx);
	if (reg != 0)
	  pc += (upk.offset << 1) + 2;
	else
	  pc += 2;
	break;
      }
    case 12:			/* I8 Formats btez btnez */
      {
	struct upk_mips16 upk;
	int reg;
	unpack_mips16 (pc, extension, insn, i8type, &upk);
	/* upk.regx contains the opcode */
	reg = read_signed_register (24);	/* Test register is 24 */
	if (((upk.regx == 0) && (reg == 0))	/* BTEZ */
	    || ((upk.regx == 1) && (reg != 0)))	/* BTNEZ */
	  /* pc = add_offset_16(pc,upk.offset) ; */
	  pc += (upk.offset << 1) + 2;
	else
	  pc += 2;
	break;
      }
    case 29:			/* RR Formats JR, JALR, JALR-RA */
      {
	struct upk_mips16 upk;
	/* upk.fmt = rrtype; */
	op = insn & 0x1f;
	if (op == 0)
	  {
	    int reg;
	    upk.regx = (insn >> 8) & 0x07;
	    upk.regy = (insn >> 5) & 0x07;
	    switch (upk.regy)
	      {
	      case 0:
		reg = upk.regx;
		break;
	      case 1:
		reg = 31;
		break;		/* Function return instruction */
	      case 2:
		reg = upk.regx;
		break;
	      default:
		reg = 31;
		break;		/* BOGUS Guess */
	      }
	    pc = read_signed_register (reg);
	  }
	else
	  pc += 2;
	break;
      }
    case 30:
      /* This is an instruction extension.  Fetch the real instruction
         (which follows the extension) and decode things based on
         that. */
      {
	pc += 2;
	pc = extended_mips16_next_pc (pc, insn, fetch_mips_16 (pc));
	break;
      }
    default:
      {
	pc += 2;
	break;
      }
    }
  return pc;
}

static CORE_ADDR
mips16_next_pc (CORE_ADDR pc)
{
  unsigned int insn = fetch_mips_16 (pc);
  return extended_mips16_next_pc (pc, 0, insn);
}

/* The mips_next_pc function supports single_step when the remote
   target monitor or stub is not developed enough to do a single_step.
   It works by decoding the current instruction and predicting where a
   branch will go. This isnt hard because all the data is available.
   The MIPS32 and MIPS16 variants are quite different */
CORE_ADDR
mips_next_pc (CORE_ADDR pc)
{
  if (pc & 0x01)
    return mips16_next_pc (pc);
  else
    return mips32_next_pc (pc);
}

struct mips_frame_cache
{
  CORE_ADDR base;
  struct trad_frame_saved_reg *saved_regs;
};


static struct mips_frame_cache *
mips_mdebug_frame_cache (struct frame_info *next_frame, void **this_cache)
{
  mips_extra_func_info_t proc_desc;
  struct mips_frame_cache *cache;
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  /* r0 bit means kernel trap */
  int kernel_trap;
  /* What registers have been saved?  Bitmasks.  */
  unsigned long gen_mask, float_mask;
  long reg_offset;

  if ((*this_cache) != NULL)
    return (*this_cache);
  cache = FRAME_OBSTACK_ZALLOC (struct mips_frame_cache);
  (*this_cache) = cache;
  cache->saved_regs = trad_frame_alloc_saved_regs (next_frame);

  /* Get the mdebug proc descriptor.  */
  proc_desc = find_proc_desc (frame_pc_unwind (next_frame), next_frame, 1);
  if (proc_desc == NULL)
    /* I'm not sure how/whether this can happen.  Normally when we
       can't find a proc_desc, we "synthesize" one using
       heuristic_proc_desc and set the saved_regs right away.  */
    return cache;

  /* Extract the frame's base.  */
  cache->base = (frame_unwind_register_signed (next_frame, NUM_REGS + PROC_FRAME_REG (proc_desc))
		 + PROC_FRAME_OFFSET (proc_desc) - PROC_FRAME_ADJUST (proc_desc));
  /* Save registers offset from scratching by following find_proc_desc call */
  reg_offset = PROC_REG_OFFSET (proc_desc);

  kernel_trap = PROC_REG_MASK (proc_desc) & 1;
  gen_mask = kernel_trap ? 0xFFFFFFFF : PROC_REG_MASK (proc_desc);
  float_mask = kernel_trap ? 0xFFFFFFFF : PROC_FREG_MASK (proc_desc);
  
  /* In any frame other than the innermost or a frame interrupted by a
     signal, we assume that all registers have been saved.  This
     assumes that all register saves in a function happen before the
     first function call.  */
  if (in_prologue (frame_pc_unwind (next_frame), PROC_LOW_ADDR (proc_desc))
      /* Not sure exactly what kernel_trap means, but if it means the
	 kernel saves the registers without a prologue doing it, we
	 better not examine the prologue to see whether registers
	 have been saved yet.  */
      && !kernel_trap)
    {
      /* We need to figure out whether the registers that the
         proc_desc claims are saved have been saved yet.  */

      CORE_ADDR addr;

      /* Bitmasks; set if we have found a save for the register.  */
      unsigned long gen_save_found = 0;
      unsigned long float_save_found = 0;
      int mips16;

      /* If the address is odd, assume this is MIPS16 code.  */
      addr = PROC_LOW_ADDR (proc_desc);
      mips16 = pc_is_mips16 (addr);

      /* Scan through this function's instructions preceding the
         current PC, and look for those that save registers.  */
      while (addr < frame_pc_unwind (next_frame))
	{
	  if (mips16)
	    {
	      mips16_decode_reg_save (mips16_fetch_instruction (addr),
				      &gen_save_found);
	      addr += MIPS16_INSTLEN;
	    }
	  else
	    {
	      mips32_decode_reg_save (mips32_fetch_instruction (addr),
				      &gen_save_found, &float_save_found);
	      addr += MIPS_INSTLEN;
	    }
	}
      gen_mask = gen_save_found;
      float_mask = float_save_found;
    }

  /* Fill in the offsets for the registers which gen_mask says were
     saved.  */
  {
    CORE_ADDR reg_position = (cache->base + reg_offset);
    int ireg;
    for (ireg = MIPS_NUMREGS - 1; gen_mask; --ireg, gen_mask <<= 1)
      if (gen_mask & 0x80000000)
	{
	  cache->saved_regs[NUM_REGS + ireg].addr = reg_position;
	  reg_position -= mips_saved_regsize (tdep);
	}
  }

  /* The MIPS16 entry instruction saves $s0 and $s1 in the reverse
     order of that normally used by gcc.  Therefore, we have to fetch
     the first instruction of the function, and if it's an entry
     instruction that saves $s0 or $s1, correct their saved addresses.  */
  if (pc_is_mips16 (PROC_LOW_ADDR (proc_desc)))
    {
      ULONGEST inst = mips16_fetch_instruction (PROC_LOW_ADDR (proc_desc));
      if ((inst & 0xf81f) == 0xe809 && (inst & 0x700) != 0x700)
	/* entry */
	{
	  int reg;
	  int sreg_count = (inst >> 6) & 3;

	  /* Check if the ra register was pushed on the stack.  */
	  CORE_ADDR reg_position = (cache->base
				    + PROC_REG_OFFSET (proc_desc));
	  if (inst & 0x20)
	    reg_position -= mips_saved_regsize (tdep);

	  /* Check if the s0 and s1 registers were pushed on the
	     stack.  */
	  /* NOTE: cagney/2004-02-08: Huh?  This is doing no such
             check.  */
	  for (reg = 16; reg < sreg_count + 16; reg++)
	    {
	      cache->saved_regs[NUM_REGS + reg].addr = reg_position;
	      reg_position -= mips_saved_regsize (tdep);
	    }
	}
    }

  /* Fill in the offsets for the registers which float_mask says were
     saved.  */
  {
    CORE_ADDR reg_position = (cache->base
			      + PROC_FREG_OFFSET (proc_desc));
    int ireg;
    /* Fill in the offsets for the float registers which float_mask
       says were saved.  */
    for (ireg = MIPS_NUMREGS - 1; float_mask; --ireg, float_mask <<= 1)
      if (float_mask & 0x80000000)
	{
	  if (mips_saved_regsize (tdep) == 4
	      && TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
	    {
	      /* On a big endian 32 bit ABI, floating point registers
	         are paired to form doubles such that the most
	         significant part is in $f[N+1] and the least
	         significant in $f[N] vis: $f[N+1] ||| $f[N].  The
	         registers are also spilled as a pair and stored as a
	         double.

	         When little-endian the least significant part is
	         stored first leading to the memory order $f[N] and
	         then $f[N+1].

	         Unfortunately, when big-endian the most significant
	         part of the double is stored first, and the least
	         significant is stored second.  This leads to the
	         registers being ordered in memory as firt $f[N+1] and
	         then $f[N].

	         For the big-endian case make certain that the
	         addresses point at the correct (swapped) locations
	         $f[N] and $f[N+1] pair (keep in mind that
	         reg_position is decremented each time through the
	         loop).  */
	      if ((ireg & 1))
		cache->saved_regs[NUM_REGS + mips_regnum (current_gdbarch)->fp0 + ireg]
		  .addr = reg_position - mips_saved_regsize (tdep);
	      else
		cache->saved_regs[NUM_REGS + mips_regnum (current_gdbarch)->fp0 + ireg]
		  .addr = reg_position + mips_saved_regsize (tdep);
	    }
	  else
	    cache->saved_regs[NUM_REGS + mips_regnum (current_gdbarch)->fp0 + ireg]
	      .addr = reg_position;
	  reg_position -= mips_saved_regsize (tdep);
	}

    cache->saved_regs[NUM_REGS + mips_regnum (current_gdbarch)->pc]
      = cache->saved_regs[NUM_REGS + RA_REGNUM];
  }

  /* SP_REGNUM, contains the value and not the address.  */
  trad_frame_set_value (cache->saved_regs, NUM_REGS + SP_REGNUM, cache->base);

  return (*this_cache);
}

static void
mips_mdebug_frame_this_id (struct frame_info *next_frame, void **this_cache,
			   struct frame_id *this_id)
{
  struct mips_frame_cache *info = mips_mdebug_frame_cache (next_frame,
							   this_cache);
  (*this_id) = frame_id_build (info->base, frame_func_unwind (next_frame));
}

static void
mips_mdebug_frame_prev_register (struct frame_info *next_frame,
				 void **this_cache,
				 int regnum, int *optimizedp,
				 enum lval_type *lvalp, CORE_ADDR *addrp,
				 int *realnump, void *valuep)
{
  struct mips_frame_cache *info = mips_mdebug_frame_cache (next_frame,
							   this_cache);
  trad_frame_prev_register (next_frame, info->saved_regs, regnum,
			    optimizedp, lvalp, addrp, realnump, valuep);
}

static const struct frame_unwind mips_mdebug_frame_unwind =
{
  NORMAL_FRAME,
  mips_mdebug_frame_this_id,
  mips_mdebug_frame_prev_register
};

static const struct frame_unwind *
mips_mdebug_frame_sniffer (struct frame_info *next_frame)
{
  return &mips_mdebug_frame_unwind;
}

static CORE_ADDR
mips_mdebug_frame_base_address (struct frame_info *next_frame,
				void **this_cache)
{
  struct mips_frame_cache *info = mips_mdebug_frame_cache (next_frame,
							   this_cache);
  return info->base;
}

static const struct frame_base mips_mdebug_frame_base = {
  &mips_mdebug_frame_unwind,
  mips_mdebug_frame_base_address,
  mips_mdebug_frame_base_address,
  mips_mdebug_frame_base_address
};

static const struct frame_base *
mips_mdebug_frame_base_sniffer (struct frame_info *next_frame)
{
  return &mips_mdebug_frame_base;
}

static CORE_ADDR
read_next_frame_reg (struct frame_info *fi, int regno)
{
  /* Always a pseudo.  */
  gdb_assert (regno >= NUM_REGS);
  if (fi == NULL)
    {
      LONGEST val;
      regcache_cooked_read_signed (current_regcache, regno, &val);
      return val;
    }
  else if ((regno % NUM_REGS) == SP_REGNUM)
    /* The SP_REGNUM is special, its value is stored in saved_regs.
       In fact, it is so special that it can even only be fetched
       using a raw register number!  Once this code as been converted
       to frame-unwind the problem goes away.  */
    return frame_unwind_register_signed (fi, regno % NUM_REGS);
  else
    return frame_unwind_register_signed (fi, regno);

}

/* mips_addr_bits_remove - remove useless address bits  */

static CORE_ADDR
mips_addr_bits_remove (CORE_ADDR addr)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  if (mips_mask_address_p (tdep) && (((ULONGEST) addr) >> 32 == 0xffffffffUL))
    /* This hack is a work-around for existing boards using PMON, the
       simulator, and any other 64-bit targets that doesn't have true
       64-bit addressing.  On these targets, the upper 32 bits of
       addresses are ignored by the hardware.  Thus, the PC or SP are
       likely to have been sign extended to all 1s by instruction
       sequences that load 32-bit addresses.  For example, a typical
       piece of code that loads an address is this:

       lui $r2, <upper 16 bits>
       ori $r2, <lower 16 bits>

       But the lui sign-extends the value such that the upper 32 bits
       may be all 1s.  The workaround is simply to mask off these
       bits.  In the future, gcc may be changed to support true 64-bit
       addressing, and this masking will have to be disabled.  */
    return addr &= 0xffffffffUL;
  else
    return addr;
}

/* mips_software_single_step() is called just before we want to resume
   the inferior, if we want to single-step it but there is no hardware
   or kernel single-step support (MIPS on GNU/Linux for example).  We find
   the target of the coming instruction and breakpoint it.

   single_step is also called just after the inferior stops.  If we had
   set up a simulated single-step, we undo our damage.  */

void
mips_software_single_step (enum target_signal sig, int insert_breakpoints_p)
{
  static CORE_ADDR next_pc;
  typedef char binsn_quantum[BREAKPOINT_MAX];
  static binsn_quantum break_mem;
  CORE_ADDR pc;

  if (insert_breakpoints_p)
    {
      pc = read_register (mips_regnum (current_gdbarch)->pc);
      next_pc = mips_next_pc (pc);

      target_insert_breakpoint (next_pc, break_mem);
    }
  else
    target_remove_breakpoint (next_pc, break_mem);
}

static struct mips_extra_func_info temp_proc_desc;

/* This hack will go away once the get_prev_frame() code has been
   modified to set the frame's type first.  That is BEFORE init extra
   frame info et.al.  is called.  This is because it will become
   possible to skip the init extra info call for sigtramp and dummy
   frames.  */
static CORE_ADDR *temp_saved_regs;

/* Set a register's saved stack address in temp_saved_regs.  If an
   address has already been set for this register, do nothing; this
   way we will only recognize the first save of a given register in a
   function prologue.

   For simplicity, save the address in both [0 .. NUM_REGS) and
   [NUM_REGS .. 2*NUM_REGS).  Strictly speaking, only the second range
   is used as it is only second range (the ABI instead of ISA
   registers) that comes into play when finding saved registers in a
   frame.  */

static void
set_reg_offset (CORE_ADDR *saved_regs, int regno, CORE_ADDR offset)
{
  if (saved_regs[regno] == 0)
    {
      saved_regs[regno + 0 * NUM_REGS] = offset;
      saved_regs[regno + 1 * NUM_REGS] = offset;
    }
}


/* Test whether the PC points to the return instruction at the
   end of a function. */

static int
mips_about_to_return (CORE_ADDR pc)
{
  if (pc_is_mips16 (pc))
    /* This mips16 case isn't necessarily reliable.  Sometimes the compiler
       generates a "jr $ra"; other times it generates code to load
       the return address from the stack to an accessible register (such
       as $a3), then a "jr" using that register.  This second case
       is almost impossible to distinguish from an indirect jump
       used for switch statements, so we don't even try.  */
    return mips_fetch_instruction (pc) == 0xe820;	/* jr $ra */
  else
    return mips_fetch_instruction (pc) == 0x3e00008;	/* jr $ra */
}


/* This fencepost looks highly suspicious to me.  Removing it also
   seems suspicious as it could affect remote debugging across serial
   lines.  */

static CORE_ADDR
heuristic_proc_start (CORE_ADDR pc)
{
  CORE_ADDR start_pc;
  CORE_ADDR fence;
  int instlen;
  int seen_adjsp = 0;

  pc = ADDR_BITS_REMOVE (pc);
  start_pc = pc;
  fence = start_pc - heuristic_fence_post;
  if (start_pc == 0)
    return 0;

  if (heuristic_fence_post == UINT_MAX || fence < VM_MIN_ADDRESS)
    fence = VM_MIN_ADDRESS;

  instlen = pc_is_mips16 (pc) ? MIPS16_INSTLEN : MIPS_INSTLEN;

  /* search back for previous return */
  for (start_pc -= instlen;; start_pc -= instlen)
    if (start_pc < fence)
      {
	/* It's not clear to me why we reach this point when
	   stop_soon, but with this test, at least we
	   don't print out warnings for every child forked (eg, on
	   decstation).  22apr93 rich@cygnus.com.  */
	if (stop_soon == NO_STOP_QUIETLY)
	  {
	    static int blurb_printed = 0;

	    warning
	      ("Warning: GDB can't find the start of the function at 0x%s.",
	       paddr_nz (pc));

	    if (!blurb_printed)
	      {
		/* This actually happens frequently in embedded
		   development, when you first connect to a board
		   and your stack pointer and pc are nowhere in
		   particular.  This message needs to give people
		   in that situation enough information to
		   determine that it's no big deal.  */
		printf_filtered ("\n\
    GDB is unable to find the start of the function at 0x%s\n\
and thus can't determine the size of that function's stack frame.\n\
This means that GDB may be unable to access that stack frame, or\n\
the frames below it.\n\
    This problem is most likely caused by an invalid program counter or\n\
stack pointer.\n\
    However, if you think GDB should simply search farther back\n\
from 0x%s for code which looks like the beginning of a\n\
function, you can increase the range of the search using the `set\n\
heuristic-fence-post' command.\n", paddr_nz (pc), paddr_nz (pc));
		blurb_printed = 1;
	      }
	  }

	return 0;
      }
    else if (pc_is_mips16 (start_pc))
      {
	unsigned short inst;

	/* On MIPS16, any one of the following is likely to be the
	   start of a function:
	   entry
	   addiu sp,-n
	   daddiu sp,-n
	   extend -n followed by 'addiu sp,+n' or 'daddiu sp,+n'  */
	inst = mips_fetch_instruction (start_pc);
	if (((inst & 0xf81f) == 0xe809 && (inst & 0x700) != 0x700)	/* entry */
	    || (inst & 0xff80) == 0x6380	/* addiu sp,-n */
	    || (inst & 0xff80) == 0xfb80	/* daddiu sp,-n */
	    || ((inst & 0xf810) == 0xf010 && seen_adjsp))	/* extend -n */
	  break;
	else if ((inst & 0xff00) == 0x6300	/* addiu sp */
		 || (inst & 0xff00) == 0xfb00)	/* daddiu sp */
	  seen_adjsp = 1;
	else
	  seen_adjsp = 0;
      }
    else if (mips_about_to_return (start_pc))
      {
	start_pc += 2 * MIPS_INSTLEN;	/* skip return, and its delay slot */
	break;
      }

  return start_pc;
}

/* Fetch the immediate value from a MIPS16 instruction.
   If the previous instruction was an EXTEND, use it to extend
   the upper bits of the immediate value.  This is a helper function
   for mips16_heuristic_proc_desc.  */

static int
mips16_get_imm (unsigned short prev_inst,	/* previous instruction */
		unsigned short inst,	/* current instruction */
		int nbits,	/* number of bits in imm field */
		int scale,	/* scale factor to be applied to imm */
		int is_signed)	/* is the imm field signed? */
{
  int offset;

  if ((prev_inst & 0xf800) == 0xf000)	/* prev instruction was EXTEND? */
    {
      offset = ((prev_inst & 0x1f) << 11) | (prev_inst & 0x7e0);
      if (offset & 0x8000)	/* check for negative extend */
	offset = 0 - (0x10000 - (offset & 0xffff));
      return offset | (inst & 0x1f);
    }
  else
    {
      int max_imm = 1 << nbits;
      int mask = max_imm - 1;
      int sign_bit = max_imm >> 1;

      offset = inst & mask;
      if (is_signed && (offset & sign_bit))
	offset = 0 - (max_imm - offset);
      return offset * scale;
    }
}


/* Fill in values in temp_proc_desc based on the MIPS16 instruction
   stream from start_pc to limit_pc.  */

static void
mips16_heuristic_proc_desc (CORE_ADDR start_pc, CORE_ADDR limit_pc,
			    struct frame_info *next_frame, CORE_ADDR sp)
{
  CORE_ADDR cur_pc;
  CORE_ADDR frame_addr = 0;	/* Value of $r17, used as frame pointer */
  unsigned short prev_inst = 0;	/* saved copy of previous instruction */
  unsigned inst = 0;		/* current instruction */
  unsigned entry_inst = 0;	/* the entry instruction */
  int reg, offset;
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  PROC_FRAME_OFFSET (&temp_proc_desc) = 0;	/* size of stack frame */
  PROC_FRAME_ADJUST (&temp_proc_desc) = 0;	/* offset of FP from SP */

  for (cur_pc = start_pc; cur_pc < limit_pc; cur_pc += MIPS16_INSTLEN)
    {
      /* Save the previous instruction.  If it's an EXTEND, we'll extract
         the immediate offset extension from it in mips16_get_imm.  */
      prev_inst = inst;

      /* Fetch and decode the instruction.   */
      inst = (unsigned short) mips_fetch_instruction (cur_pc);
      if ((inst & 0xff00) == 0x6300	/* addiu sp */
	  || (inst & 0xff00) == 0xfb00)	/* daddiu sp */
	{
	  offset = mips16_get_imm (prev_inst, inst, 8, 8, 1);
	  if (offset < 0)	/* negative stack adjustment? */
	    PROC_FRAME_OFFSET (&temp_proc_desc) -= offset;
	  else
	    /* Exit loop if a positive stack adjustment is found, which
	       usually means that the stack cleanup code in the function
	       epilogue is reached.  */
	    break;
	}
      else if ((inst & 0xf800) == 0xd000)	/* sw reg,n($sp) */
	{
	  offset = mips16_get_imm (prev_inst, inst, 8, 4, 0);
	  reg = mips16_to_32_reg[(inst & 0x700) >> 8];
	  PROC_REG_MASK (&temp_proc_desc) |= (1 << reg);
	  set_reg_offset (temp_saved_regs, reg, sp + offset);
	}
      else if ((inst & 0xff00) == 0xf900)	/* sd reg,n($sp) */
	{
	  offset = mips16_get_imm (prev_inst, inst, 5, 8, 0);
	  reg = mips16_to_32_reg[(inst & 0xe0) >> 5];
	  PROC_REG_MASK (&temp_proc_desc) |= (1 << reg);
	  set_reg_offset (temp_saved_regs, reg, sp + offset);
	}
      else if ((inst & 0xff00) == 0x6200)	/* sw $ra,n($sp) */
	{
	  offset = mips16_get_imm (prev_inst, inst, 8, 4, 0);
	  PROC_REG_MASK (&temp_proc_desc) |= (1 << RA_REGNUM);
	  set_reg_offset (temp_saved_regs, RA_REGNUM, sp + offset);
	}
      else if ((inst & 0xff00) == 0xfa00)	/* sd $ra,n($sp) */
	{
	  offset = mips16_get_imm (prev_inst, inst, 8, 8, 0);
	  PROC_REG_MASK (&temp_proc_desc) |= (1 << RA_REGNUM);
	  set_reg_offset (temp_saved_regs, RA_REGNUM, sp + offset);
	}
      else if (inst == 0x673d)	/* move $s1, $sp */
	{
	  frame_addr = sp;
	  PROC_FRAME_REG (&temp_proc_desc) = 17;
	}
      else if ((inst & 0xff00) == 0x0100)	/* addiu $s1,sp,n */
	{
	  offset = mips16_get_imm (prev_inst, inst, 8, 4, 0);
	  frame_addr = sp + offset;
	  PROC_FRAME_REG (&temp_proc_desc) = 17;
	  PROC_FRAME_ADJUST (&temp_proc_desc) = offset;
	}
      else if ((inst & 0xFF00) == 0xd900)	/* sw reg,offset($s1) */
	{
	  offset = mips16_get_imm (prev_inst, inst, 5, 4, 0);
	  reg = mips16_to_32_reg[(inst & 0xe0) >> 5];
	  PROC_REG_MASK (&temp_proc_desc) |= 1 << reg;
	  set_reg_offset (temp_saved_regs, reg, frame_addr + offset);
	}
      else if ((inst & 0xFF00) == 0x7900)	/* sd reg,offset($s1) */
	{
	  offset = mips16_get_imm (prev_inst, inst, 5, 8, 0);
	  reg = mips16_to_32_reg[(inst & 0xe0) >> 5];
	  PROC_REG_MASK (&temp_proc_desc) |= 1 << reg;
	  set_reg_offset (temp_saved_regs, reg, frame_addr + offset);
	}
      else if ((inst & 0xf81f) == 0xe809 && (inst & 0x700) != 0x700)	/* entry */
	entry_inst = inst;	/* save for later processing */
      else if ((inst & 0xf800) == 0x1800)	/* jal(x) */
	cur_pc += MIPS16_INSTLEN;	/* 32-bit instruction */
    }

  /* The entry instruction is typically the first instruction in a function,
     and it stores registers at offsets relative to the value of the old SP
     (before the prologue).  But the value of the sp parameter to this
     function is the new SP (after the prologue has been executed).  So we
     can't calculate those offsets until we've seen the entire prologue,
     and can calculate what the old SP must have been. */
  if (entry_inst != 0)
    {
      int areg_count = (entry_inst >> 8) & 7;
      int sreg_count = (entry_inst >> 6) & 3;

      /* The entry instruction always subtracts 32 from the SP.  */
      PROC_FRAME_OFFSET (&temp_proc_desc) += 32;

      /* Now we can calculate what the SP must have been at the
         start of the function prologue.  */
      sp += PROC_FRAME_OFFSET (&temp_proc_desc);

      /* Check if a0-a3 were saved in the caller's argument save area.  */
      for (reg = 4, offset = 0; reg < areg_count + 4; reg++)
	{
	  PROC_REG_MASK (&temp_proc_desc) |= 1 << reg;
	  set_reg_offset (temp_saved_regs, reg, sp + offset);
	  offset += mips_saved_regsize (tdep);
	}

      /* Check if the ra register was pushed on the stack.  */
      offset = -4;
      if (entry_inst & 0x20)
	{
	  PROC_REG_MASK (&temp_proc_desc) |= 1 << RA_REGNUM;
	  set_reg_offset (temp_saved_regs, RA_REGNUM, sp + offset);
	  offset -= mips_saved_regsize (tdep);
	}

      /* Check if the s0 and s1 registers were pushed on the stack.  */
      for (reg = 16; reg < sreg_count + 16; reg++)
	{
	  PROC_REG_MASK (&temp_proc_desc) |= 1 << reg;
	  set_reg_offset (temp_saved_regs, reg, sp + offset);
	  offset -= mips_saved_regsize (tdep);
	}
    }
}

static void
mips32_heuristic_proc_desc (CORE_ADDR start_pc, CORE_ADDR limit_pc,
			    struct frame_info *next_frame, CORE_ADDR sp)
{
  CORE_ADDR cur_pc;
  CORE_ADDR frame_addr = 0;	/* Value of $r30. Used by gcc for frame-pointer */
restart:
  temp_saved_regs = xrealloc (temp_saved_regs, SIZEOF_FRAME_SAVED_REGS);
  memset (temp_saved_regs, '\0', SIZEOF_FRAME_SAVED_REGS);
  PROC_FRAME_OFFSET (&temp_proc_desc) = 0;
  PROC_FRAME_ADJUST (&temp_proc_desc) = 0;	/* offset of FP from SP */
  for (cur_pc = start_pc; cur_pc < limit_pc; cur_pc += MIPS_INSTLEN)
    {
      unsigned long inst, high_word, low_word;
      int reg;

      /* Fetch the instruction.   */
      inst = (unsigned long) mips_fetch_instruction (cur_pc);

      /* Save some code by pre-extracting some useful fields.  */
      high_word = (inst >> 16) & 0xffff;
      low_word = inst & 0xffff;
      reg = high_word & 0x1f;

      if (high_word == 0x27bd	/* addiu $sp,$sp,-i */
	  || high_word == 0x23bd	/* addi $sp,$sp,-i */
	  || high_word == 0x67bd)	/* daddiu $sp,$sp,-i */
	{
	  if (low_word & 0x8000)	/* negative stack adjustment? */
	    PROC_FRAME_OFFSET (&temp_proc_desc) += 0x10000 - low_word;
	  else
	    /* Exit loop if a positive stack adjustment is found, which
	       usually means that the stack cleanup code in the function
	       epilogue is reached.  */
	    break;
	}
      else if ((high_word & 0xFFE0) == 0xafa0)	/* sw reg,offset($sp) */
	{
	  PROC_REG_MASK (&temp_proc_desc) |= 1 << reg;
	  set_reg_offset (temp_saved_regs, reg, sp + low_word);
          /* Do we have registers offset yet? */
          if (!PROC_REG_OFFSET (&temp_proc_desc))
            PROC_REG_OFFSET (&temp_proc_desc) = low_word - PROC_FRAME_OFFSET (&temp_proc_desc);
	}
      else if ((high_word & 0xFFE0) == 0xffa0)	/* sd reg,offset($sp) */
	{
	  PROC_REG_MASK (&temp_proc_desc) |= 1 << reg;
	  set_reg_offset (temp_saved_regs, reg,
			  sp + low_word);
          /* Do we have registers offset yet? */
          if (!PROC_REG_OFFSET (&temp_proc_desc))
            PROC_REG_OFFSET (&temp_proc_desc) = low_word - PROC_FRAME_OFFSET (&temp_proc_desc);
	}
      else if (high_word == 0x27be)	/* addiu $30,$sp,size */
	{
	  /* Old gcc frame, r30 is virtual frame pointer.  */
	  if ((long) low_word != PROC_FRAME_OFFSET (&temp_proc_desc))
	    frame_addr = sp + low_word;
	  else if (PROC_FRAME_REG (&temp_proc_desc) == SP_REGNUM)
	    {
	      unsigned alloca_adjust;
	      PROC_FRAME_REG (&temp_proc_desc) = 30;
	      frame_addr = read_next_frame_reg (next_frame, NUM_REGS + 30);
	      alloca_adjust = (unsigned) (frame_addr - (sp + low_word));
	      if (alloca_adjust > 0)
		{
		  /* FP > SP + frame_size. This may be because
		   * of an alloca or somethings similar.
		   * Fix sp to "pre-alloca" value, and try again.
		   */
		  sp += alloca_adjust;
		  goto restart;
		}
	    }
	}
      /* move $30,$sp.  With different versions of gas this will be either
         `addu $30,$sp,$zero' or `or $30,$sp,$zero' or `daddu 30,sp,$0'.
         Accept any one of these.  */
      else if (inst == 0x03A0F021 || inst == 0x03a0f025 || inst == 0x03a0f02d)
	{
	  /* New gcc frame, virtual frame pointer is at r30 + frame_size.  */
	  if (PROC_FRAME_REG (&temp_proc_desc) == SP_REGNUM)
	    {
	      unsigned alloca_adjust;
	      PROC_FRAME_REG (&temp_proc_desc) = 30;
	      frame_addr = read_next_frame_reg (next_frame, NUM_REGS + 30);
	      alloca_adjust = (unsigned) (frame_addr - sp);
	      if (alloca_adjust > 0)
		{
		  /* FP > SP + frame_size. This may be because
		   * of an alloca or somethings similar.
		   * Fix sp to "pre-alloca" value, and try again.
		   */
		  sp += alloca_adjust;
		  goto restart;
		}
	    }
	}
      else if ((high_word & 0xFFE0) == 0xafc0)	/* sw reg,offset($30) */
	{
	  PROC_REG_MASK (&temp_proc_desc) |= 1 << reg;
	  set_reg_offset (temp_saved_regs, reg, frame_addr + low_word);
	}
    }
}

static mips_extra_func_info_t
heuristic_proc_desc (CORE_ADDR start_pc, CORE_ADDR limit_pc,
		     struct frame_info *next_frame, int cur_frame)
{
  CORE_ADDR sp;

  if (cur_frame)
    sp = read_next_frame_reg (next_frame, NUM_REGS + SP_REGNUM);
  else
    sp = 0;

  if (start_pc == 0)
    return NULL;
  memset (&temp_proc_desc, '\0', sizeof (temp_proc_desc));
  temp_saved_regs = xrealloc (temp_saved_regs, SIZEOF_FRAME_SAVED_REGS);
  memset (temp_saved_regs, '\0', SIZEOF_FRAME_SAVED_REGS);
  PROC_LOW_ADDR (&temp_proc_desc) = start_pc;
  PROC_FRAME_REG (&temp_proc_desc) = SP_REGNUM;
  PROC_PC_REG (&temp_proc_desc) = RA_REGNUM;

  if (start_pc + 200 < limit_pc)
    limit_pc = start_pc + 200;
  if (pc_is_mips16 (start_pc))
    mips16_heuristic_proc_desc (start_pc, limit_pc, next_frame, sp);
  else
    mips32_heuristic_proc_desc (start_pc, limit_pc, next_frame, sp);
  return &temp_proc_desc;
}

struct mips_objfile_private
{
  bfd_size_type size;
  char *contents;
};

/* Global used to communicate between non_heuristic_proc_desc and
   compare_pdr_entries within qsort ().  */
static bfd *the_bfd;

static int
compare_pdr_entries (const void *a, const void *b)
{
  CORE_ADDR lhs = bfd_get_32 (the_bfd, (bfd_byte *) a);
  CORE_ADDR rhs = bfd_get_32 (the_bfd, (bfd_byte *) b);

  if (lhs < rhs)
    return -1;
  else if (lhs == rhs)
    return 0;
  else
    return 1;
}

static mips_extra_func_info_t
non_heuristic_proc_desc (CORE_ADDR pc, CORE_ADDR *addrptr)
{
  CORE_ADDR startaddr;
  mips_extra_func_info_t proc_desc;
  struct block *b = block_for_pc (pc);
  struct symbol *sym;
  struct obj_section *sec;
  struct mips_objfile_private *priv;

  if (DEPRECATED_PC_IN_CALL_DUMMY (pc, 0, 0))
    return NULL;

  find_pc_partial_function (pc, NULL, &startaddr, NULL);
  if (addrptr)
    *addrptr = startaddr;

  priv = NULL;

  sec = find_pc_section (pc);
  if (sec != NULL)
    {
      priv = (struct mips_objfile_private *) objfile_data (sec->objfile, mips_pdr_data);

      /* Search the ".pdr" section generated by GAS.  This includes most of
         the information normally found in ECOFF PDRs.  */

      the_bfd = sec->objfile->obfd;
      if (priv == NULL
	  && (the_bfd->format == bfd_object
	      && bfd_get_flavour (the_bfd) == bfd_target_elf_flavour
	      && elf_elfheader (the_bfd)->e_ident[EI_CLASS] == ELFCLASS64))
	{
	  /* Right now GAS only outputs the address as a four-byte sequence.
	     This means that we should not bother with this method on 64-bit
	     targets (until that is fixed).  */

	  priv = obstack_alloc (&sec->objfile->objfile_obstack,
				sizeof (struct mips_objfile_private));
	  priv->size = 0;
	  set_objfile_data (sec->objfile, mips_pdr_data, priv);
	}
      else if (priv == NULL)
	{
	  asection *bfdsec;

	  priv = obstack_alloc (&sec->objfile->objfile_obstack,
				sizeof (struct mips_objfile_private));

	  bfdsec = bfd_get_section_by_name (sec->objfile->obfd, ".pdr");
	  if (bfdsec != NULL)
	    {
	      priv->size = bfd_section_size (sec->objfile->obfd, bfdsec);
	      priv->contents = obstack_alloc (&sec->objfile->objfile_obstack,
					      priv->size);
	      bfd_get_section_contents (sec->objfile->obfd, bfdsec,
					priv->contents, 0, priv->size);

	      /* In general, the .pdr section is sorted.  However, in the
	         presence of multiple code sections (and other corner cases)
	         it can become unsorted.  Sort it so that we can use a faster
	         binary search.  */
	      qsort (priv->contents, priv->size / 32, 32,
		     compare_pdr_entries);
	    }
	  else
	    priv->size = 0;

	  set_objfile_data (sec->objfile, mips_pdr_data, priv);
	}
      the_bfd = NULL;

      if (priv->size != 0)
	{
	  int low, mid, high;
	  char *ptr;

	  low = 0;
	  high = priv->size / 32;

	  do
	    {
	      CORE_ADDR pdr_pc;

	      mid = (low + high) / 2;

	      ptr = priv->contents + mid * 32;
	      pdr_pc = bfd_get_signed_32 (sec->objfile->obfd, ptr);
	      pdr_pc += ANOFFSET (sec->objfile->section_offsets,
				  SECT_OFF_TEXT (sec->objfile));
	      if (pdr_pc == startaddr)
		break;
	      if (pdr_pc > startaddr)
		high = mid;
	      else
		low = mid + 1;
	    }
	  while (low != high);

	  if (low != high)
	    {
	      struct symbol *sym = find_pc_function (pc);

	      /* Fill in what we need of the proc_desc.  */
	      proc_desc = (mips_extra_func_info_t)
		obstack_alloc (&sec->objfile->objfile_obstack,
			       sizeof (struct mips_extra_func_info));
	      PROC_LOW_ADDR (proc_desc) = startaddr;

	      /* Only used for dummy frames.  */
	      PROC_HIGH_ADDR (proc_desc) = 0;

	      PROC_FRAME_OFFSET (proc_desc)
		= bfd_get_32 (sec->objfile->obfd, ptr + 20);
	      PROC_FRAME_REG (proc_desc) = bfd_get_32 (sec->objfile->obfd,
						       ptr + 24);
	      PROC_FRAME_ADJUST (proc_desc) = 0;
	      PROC_REG_MASK (proc_desc) = bfd_get_32 (sec->objfile->obfd,
						      ptr + 4);
	      PROC_FREG_MASK (proc_desc) = bfd_get_32 (sec->objfile->obfd,
						       ptr + 12);
	      PROC_REG_OFFSET (proc_desc) = bfd_get_32 (sec->objfile->obfd,
							ptr + 8);
	      PROC_FREG_OFFSET (proc_desc)
		= bfd_get_32 (sec->objfile->obfd, ptr + 16);
	      PROC_PC_REG (proc_desc) = bfd_get_32 (sec->objfile->obfd,
						    ptr + 28);
	      proc_desc->pdr.isym = (long) sym;

	      return proc_desc;
	    }
	}
    }

  if (b == NULL)
    return NULL;

  if (startaddr > BLOCK_START (b))
    {
      /* This is the "pathological" case referred to in a comment in
         print_frame_info.  It might be better to move this check into
         symbol reading.  */
      return NULL;
    }

  sym = lookup_symbol (MIPS_EFI_SYMBOL_NAME, b, LABEL_DOMAIN, 0, NULL);

  /* If we never found a PDR for this function in symbol reading, then
     examine prologues to find the information.  */
  if (sym)
    {
      proc_desc = (mips_extra_func_info_t) SYMBOL_VALUE (sym);
      if (PROC_FRAME_REG (proc_desc) == -1)
	return NULL;
      else
	return proc_desc;
    }
  else
    return NULL;
}


static mips_extra_func_info_t
find_proc_desc (CORE_ADDR pc, struct frame_info *next_frame, int cur_frame)
{
  mips_extra_func_info_t proc_desc;
  CORE_ADDR startaddr = 0;

  proc_desc = non_heuristic_proc_desc (pc, &startaddr);

  if (proc_desc)
    {
      /* IF this is the topmost frame AND
       * (this proc does not have debugging information OR
       * the PC is in the procedure prologue)
       * THEN create a "heuristic" proc_desc (by analyzing
       * the actual code) to replace the "official" proc_desc.
       */
      if (next_frame == NULL)
	{
	  struct symtab_and_line val;
	  struct symbol *proc_symbol =
	    PROC_DESC_IS_DUMMY (proc_desc) ? 0 : PROC_SYMBOL (proc_desc);

	  if (proc_symbol)
	    {
	      val = find_pc_line (BLOCK_START
				  (SYMBOL_BLOCK_VALUE (proc_symbol)), 0);
	      val.pc = val.end ? val.end : pc;
	    }
	  if (!proc_symbol || pc < val.pc)
	    {
	      mips_extra_func_info_t found_heuristic =
		heuristic_proc_desc (PROC_LOW_ADDR (proc_desc),
				     pc, next_frame, cur_frame);
	      if (found_heuristic)
		proc_desc = found_heuristic;
	    }
	}
    }
  else
    {
      /* Is linked_proc_desc_table really necessary?  It only seems to be used
         by procedure call dummys.  However, the procedures being called ought
         to have their own proc_descs, and even if they don't,
         heuristic_proc_desc knows how to create them! */

      struct linked_proc_info *link;

      for (link = linked_proc_desc_table; link; link = link->next)
	if (PROC_LOW_ADDR (&link->info) <= pc
	    && PROC_HIGH_ADDR (&link->info) > pc)
	  return &link->info;

      if (startaddr == 0)
	startaddr = heuristic_proc_start (pc);

      proc_desc = heuristic_proc_desc (startaddr, pc, next_frame, cur_frame);
    }
  return proc_desc;
}

/* MIPS stack frames are almost impenetrable.  When execution stops,
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
setup_arbitrary_frame (int argc, CORE_ADDR *argv)
{
  if (argc != 2)
    error ("MIPS frame specifications require two arguments: sp and pc");

  return create_new_frame (argv[0], argv[1]);
}

/* According to the current ABI, should the type be passed in a
   floating-point register (assuming that there is space)?  When there
   is no FPU, FP are not even considered as possibile candidates for
   FP registers and, consequently this returns false - forces FP
   arguments into integer registers. */

static int
fp_register_arg_p (enum type_code typecode, struct type *arg_type)
{
  return ((typecode == TYPE_CODE_FLT
	   || (MIPS_EABI
	       && (typecode == TYPE_CODE_STRUCT
		   || typecode == TYPE_CODE_UNION)
	       && TYPE_NFIELDS (arg_type) == 1
	       && TYPE_CODE (TYPE_FIELD_TYPE (arg_type, 0)) == TYPE_CODE_FLT))
	  && MIPS_FPU_TYPE != MIPS_FPU_NONE);
}

/* On o32, argument passing in GPRs depends on the alignment of the type being
   passed.  Return 1 if this type must be aligned to a doubleword boundary. */

static int
mips_type_needs_double_align (struct type *type)
{
  enum type_code typecode = TYPE_CODE (type);

  if (typecode == TYPE_CODE_FLT && TYPE_LENGTH (type) == 8)
    return 1;
  else if (typecode == TYPE_CODE_STRUCT)
    {
      if (TYPE_NFIELDS (type) < 1)
	return 0;
      return mips_type_needs_double_align (TYPE_FIELD_TYPE (type, 0));
    }
  else if (typecode == TYPE_CODE_UNION)
    {
      int i, n;

      n = TYPE_NFIELDS (type);
      for (i = 0; i < n; i++)
	if (mips_type_needs_double_align (TYPE_FIELD_TYPE (type, i)))
	  return 1;
      return 0;
    }
  return 0;
}

/* Adjust the address downward (direction of stack growth) so that it
   is correctly aligned for a new stack frame.  */
static CORE_ADDR
mips_frame_align (struct gdbarch *gdbarch, CORE_ADDR addr)
{
  return align_down (addr, 16);
}

/* Determine how a return value is stored within the MIPS register
   file, given the return type `valtype'. */

struct return_value_word
{
  int len;
  int reg;
  int reg_offset;
  int buf_offset;
};

static void
return_value_location (struct type *valtype,
		       struct return_value_word *hi,
		       struct return_value_word *lo)
{
  int len = TYPE_LENGTH (valtype);
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (TYPE_CODE (valtype) == TYPE_CODE_FLT
      && ((MIPS_FPU_TYPE == MIPS_FPU_DOUBLE && (len == 4 || len == 8))
	  || (MIPS_FPU_TYPE == MIPS_FPU_SINGLE && len == 4)))
    {
      if (!FP_REGISTER_DOUBLE && len == 8)
	{
	  /* We need to break a 64bit float in two 32 bit halves and
	     spread them across a floating-point register pair. */
	  lo->buf_offset = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? 4 : 0;
	  hi->buf_offset = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? 0 : 4;
	  lo->reg_offset = ((TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
			     && register_size (current_gdbarch,
					       mips_regnum (current_gdbarch)->
					       fp0) == 8) ? 4 : 0);
	  hi->reg_offset = lo->reg_offset;
	  lo->reg = mips_regnum (current_gdbarch)->fp0 + 0;
	  hi->reg = mips_regnum (current_gdbarch)->fp0 + 1;
	  lo->len = 4;
	  hi->len = 4;
	}
      else
	{
	  /* The floating point value fits in a single floating-point
	     register. */
	  lo->reg_offset = ((TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
			     && register_size (current_gdbarch,
					       mips_regnum (current_gdbarch)->
					       fp0) == 8
			     && len == 4) ? 4 : 0);
	  lo->reg = mips_regnum (current_gdbarch)->fp0;
	  lo->len = len;
	  lo->buf_offset = 0;
	  hi->len = 0;
	  hi->reg_offset = 0;
	  hi->buf_offset = 0;
	  hi->reg = 0;
	}
    }
  else
    {
      /* Locate a result possibly spread across two registers. */
      int regnum = 2;
      lo->reg = regnum + 0;
      hi->reg = regnum + 1;
      if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
	  && len < mips_saved_regsize (tdep))
	{
	  /* "un-left-justify" the value in the low register */
	  lo->reg_offset = mips_saved_regsize (tdep) - len;
	  lo->len = len;
	  hi->reg_offset = 0;
	  hi->len = 0;
	}
      else if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG && len > mips_saved_regsize (tdep)	/* odd-size structs */
	       && len < mips_saved_regsize (tdep) * 2
	       && (TYPE_CODE (valtype) == TYPE_CODE_STRUCT ||
		   TYPE_CODE (valtype) == TYPE_CODE_UNION))
	{
	  /* "un-left-justify" the value spread across two registers. */
	  lo->reg_offset = 2 * mips_saved_regsize (tdep) - len;
	  lo->len = mips_saved_regsize (tdep) - lo->reg_offset;
	  hi->reg_offset = 0;
	  hi->len = len - lo->len;
	}
      else
	{
	  /* Only perform a partial copy of the second register. */
	  lo->reg_offset = 0;
	  hi->reg_offset = 0;
	  if (len > mips_saved_regsize (tdep))
	    {
	      lo->len = mips_saved_regsize (tdep);
	      hi->len = len - mips_saved_regsize (tdep);
	    }
	  else
	    {
	      lo->len = len;
	      hi->len = 0;
	    }
	}
      if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
	  && register_size (current_gdbarch, regnum) == 8
	  && mips_saved_regsize (tdep) == 4)
	{
	  /* Account for the fact that only the least-signficant part
	     of the register is being used */
	  lo->reg_offset += 4;
	  hi->reg_offset += 4;
	}
      lo->buf_offset = 0;
      hi->buf_offset = lo->len;
    }
}

/* Should call_function allocate stack space for a struct return?  */

static int
mips_eabi_use_struct_convention (int gcc_p, struct type *type)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  return (TYPE_LENGTH (type) > 2 * mips_saved_regsize (tdep));
}

/* Should call_function pass struct by reference? 
   For each architecture, structs are passed either by
   value or by reference, depending on their size.  */

static int
mips_eabi_reg_struct_has_addr (int gcc_p, struct type *type)
{
  enum type_code typecode = TYPE_CODE (check_typedef (type));
  int len = TYPE_LENGTH (check_typedef (type));
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (typecode == TYPE_CODE_STRUCT || typecode == TYPE_CODE_UNION)
    return (len > mips_saved_regsize (tdep));

  return 0;
}

static CORE_ADDR
mips_eabi_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
			   struct regcache *regcache, CORE_ADDR bp_addr,
			   int nargs, struct value **args, CORE_ADDR sp,
			   int struct_return, CORE_ADDR struct_addr)
{
  int argreg;
  int float_argreg;
  int argnum;
  int len = 0;
  int stack_offset = 0;
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* For shared libraries, "t9" needs to point at the function
     address.  */
  regcache_cooked_write_signed (regcache, T9_REGNUM, func_addr);

  /* Set the return address register to point to the entry point of
     the program, where a breakpoint lies in wait.  */
  regcache_cooked_write_signed (regcache, RA_REGNUM, bp_addr);

  /* First ensure that the stack and structure return address (if any)
     are properly aligned.  The stack has to be at least 64-bit
     aligned even on 32-bit machines, because doubles must be 64-bit
     aligned.  For n32 and n64, stack frames need to be 128-bit
     aligned, so we round to this widest known alignment.  */

  sp = align_down (sp, 16);
  struct_addr = align_down (struct_addr, 16);

  /* Now make space on the stack for the args.  We allocate more
     than necessary for EABI, because the first few arguments are
     passed in registers, but that's OK.  */
  for (argnum = 0; argnum < nargs; argnum++)
    len += align_up (TYPE_LENGTH (VALUE_TYPE (args[argnum])),
		     mips_stack_argsize (tdep));
  sp -= align_up (len, 16);

  if (mips_debug)
    fprintf_unfiltered (gdb_stdlog,
			"mips_eabi_push_dummy_call: sp=0x%s allocated %ld\n",
			paddr_nz (sp), (long) align_up (len, 16));

  /* Initialize the integer and float register pointers.  */
  argreg = A0_REGNUM;
  float_argreg = mips_fpa0_regnum (current_gdbarch);

  /* The struct_return pointer occupies the first parameter-passing reg.  */
  if (struct_return)
    {
      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "mips_eabi_push_dummy_call: struct_return reg=%d 0x%s\n",
			    argreg, paddr_nz (struct_addr));
      write_register (argreg++, struct_addr);
    }

  /* Now load as many as possible of the first arguments into
     registers, and push the rest onto the stack.  Loop thru args
     from first to last.  */
  for (argnum = 0; argnum < nargs; argnum++)
    {
      char *val;
      char valbuf[MAX_REGISTER_SIZE];
      struct value *arg = args[argnum];
      struct type *arg_type = check_typedef (VALUE_TYPE (arg));
      int len = TYPE_LENGTH (arg_type);
      enum type_code typecode = TYPE_CODE (arg_type);

      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "mips_eabi_push_dummy_call: %d len=%d type=%d",
			    argnum + 1, len, (int) typecode);

      /* The EABI passes structures that do not fit in a register by
         reference.  */
      if (len > mips_saved_regsize (tdep)
	  && (typecode == TYPE_CODE_STRUCT || typecode == TYPE_CODE_UNION))
	{
	  store_unsigned_integer (valbuf, mips_saved_regsize (tdep),
				  VALUE_ADDRESS (arg));
	  typecode = TYPE_CODE_PTR;
	  len = mips_saved_regsize (tdep);
	  val = valbuf;
	  if (mips_debug)
	    fprintf_unfiltered (gdb_stdlog, " push");
	}
      else
	val = (char *) VALUE_CONTENTS (arg);

      /* 32-bit ABIs always start floating point arguments in an
         even-numbered floating point register.  Round the FP register
         up before the check to see if there are any FP registers
         left.  Non MIPS_EABI targets also pass the FP in the integer
         registers so also round up normal registers.  */
      if (!FP_REGISTER_DOUBLE && fp_register_arg_p (typecode, arg_type))
	{
	  if ((float_argreg & 1))
	    float_argreg++;
	}

      /* Floating point arguments passed in registers have to be
         treated specially.  On 32-bit architectures, doubles
         are passed in register pairs; the even register gets
         the low word, and the odd register gets the high word.
         On non-EABI processors, the first two floating point arguments are
         also copied to general registers, because MIPS16 functions
         don't use float registers for arguments.  This duplication of
         arguments in general registers can't hurt non-MIPS16 functions
         because those registers are normally skipped.  */
      /* MIPS_EABI squeezes a struct that contains a single floating
         point value into an FP register instead of pushing it onto the
         stack.  */
      if (fp_register_arg_p (typecode, arg_type)
	  && float_argreg <= MIPS_LAST_FP_ARG_REGNUM)
	{
	  if (!FP_REGISTER_DOUBLE && len == 8)
	    {
	      int low_offset = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? 4 : 0;
	      unsigned long regval;

	      /* Write the low word of the double to the even register(s).  */
	      regval = extract_unsigned_integer (val + low_offset, 4);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, 4));
	      write_register (float_argreg++, regval);

	      /* Write the high word of the double to the odd register(s).  */
	      regval = extract_unsigned_integer (val + 4 - low_offset, 4);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, 4));
	      write_register (float_argreg++, regval);
	    }
	  else
	    {
	      /* This is a floating point value that fits entirely
	         in a single register.  */
	      /* On 32 bit ABI's the float_argreg is further adjusted
	         above to ensure that it is even register aligned.  */
	      LONGEST regval = extract_unsigned_integer (val, len);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, len));
	      write_register (float_argreg++, regval);
	    }
	}
      else
	{
	  /* Copy the argument to general registers or the stack in
	     register-sized pieces.  Large arguments are split between
	     registers and stack.  */
	  /* Note: structs whose size is not a multiple of
	     mips_regsize() are treated specially: Irix cc passes them
	     in registers where gcc sometimes puts them on the stack.
	     For maximum compatibility, we will put them in both
	     places.  */
	  int odd_sized_struct = ((len > mips_saved_regsize (tdep))
				  && (len % mips_saved_regsize (tdep) != 0));

	  /* Note: Floating-point values that didn't fit into an FP
	     register are only written to memory.  */
	  while (len > 0)
	    {
	      /* Remember if the argument was written to the stack.  */
	      int stack_used_p = 0;
	      int partial_len = (len < mips_saved_regsize (tdep)
				 ? len : mips_saved_regsize (tdep));

	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " -- partial=%d",
				    partial_len);

	      /* Write this portion of the argument to the stack.  */
	      if (argreg > MIPS_LAST_ARG_REGNUM
		  || odd_sized_struct
		  || fp_register_arg_p (typecode, arg_type))
		{
		  /* Should shorter than int integer values be
		     promoted to int before being stored? */
		  int longword_offset = 0;
		  CORE_ADDR addr;
		  stack_used_p = 1;
		  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
		    {
		      if (mips_stack_argsize (tdep) == 8
			  && (typecode == TYPE_CODE_INT
			      || typecode == TYPE_CODE_PTR
			      || typecode == TYPE_CODE_FLT) && len <= 4)
			longword_offset = mips_stack_argsize (tdep) - len;
		      else if ((typecode == TYPE_CODE_STRUCT
				|| typecode == TYPE_CODE_UNION)
			       && (TYPE_LENGTH (arg_type)
				   < mips_stack_argsize (tdep)))
			longword_offset = mips_stack_argsize (tdep) - len;
		    }

		  if (mips_debug)
		    {
		      fprintf_unfiltered (gdb_stdlog, " - stack_offset=0x%s",
					  paddr_nz (stack_offset));
		      fprintf_unfiltered (gdb_stdlog, " longword_offset=0x%s",
					  paddr_nz (longword_offset));
		    }

		  addr = sp + stack_offset + longword_offset;

		  if (mips_debug)
		    {
		      int i;
		      fprintf_unfiltered (gdb_stdlog, " @0x%s ",
					  paddr_nz (addr));
		      for (i = 0; i < partial_len; i++)
			{
			  fprintf_unfiltered (gdb_stdlog, "%02x",
					      val[i] & 0xff);
			}
		    }
		  write_memory (addr, val, partial_len);
		}

	      /* Note!!! This is NOT an else clause.  Odd sized
	         structs may go thru BOTH paths.  Floating point
	         arguments will not.  */
	      /* Write this portion of the argument to a general
	         purpose register.  */
	      if (argreg <= MIPS_LAST_ARG_REGNUM
		  && !fp_register_arg_p (typecode, arg_type))
		{
		  LONGEST regval =
		    extract_unsigned_integer (val, partial_len);

		  if (mips_debug)
		    fprintf_filtered (gdb_stdlog, " - reg=%d val=%s",
				      argreg,
				      phex (regval,
					    mips_saved_regsize (tdep)));
		  write_register (argreg, regval);
		  argreg++;
		}

	      len -= partial_len;
	      val += partial_len;

	      /* Compute the the offset into the stack at which we
	         will copy the next parameter.

	         In the new EABI (and the NABI32), the stack_offset
	         only needs to be adjusted when it has been used.  */

	      if (stack_used_p)
		stack_offset += align_up (partial_len,
					  mips_stack_argsize (tdep));
	    }
	}
      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog, "\n");
    }

  regcache_cooked_write_signed (regcache, SP_REGNUM, sp);

  /* Return adjusted stack pointer.  */
  return sp;
}

/* Given a return value in `regbuf' with a type `valtype', extract and
   copy its value into `valbuf'. */

static void
mips_eabi_extract_return_value (struct type *valtype,
				char regbuf[], char *valbuf)
{
  struct return_value_word lo;
  struct return_value_word hi;
  return_value_location (valtype, &hi, &lo);

  memcpy (valbuf + lo.buf_offset,
	  regbuf + DEPRECATED_REGISTER_BYTE (NUM_REGS + lo.reg) +
	  lo.reg_offset, lo.len);

  if (hi.len > 0)
    memcpy (valbuf + hi.buf_offset,
	    regbuf + DEPRECATED_REGISTER_BYTE (NUM_REGS + hi.reg) +
	    hi.reg_offset, hi.len);
}

/* Given a return value in `valbuf' with a type `valtype', write it's
   value into the appropriate register. */

static void
mips_eabi_store_return_value (struct type *valtype, char *valbuf)
{
  char raw_buffer[MAX_REGISTER_SIZE];
  struct return_value_word lo;
  struct return_value_word hi;
  return_value_location (valtype, &hi, &lo);

  memset (raw_buffer, 0, sizeof (raw_buffer));
  memcpy (raw_buffer + lo.reg_offset, valbuf + lo.buf_offset, lo.len);
  deprecated_write_register_bytes (DEPRECATED_REGISTER_BYTE (lo.reg),
				   raw_buffer, register_size (current_gdbarch,
							      lo.reg));

  if (hi.len > 0)
    {
      memset (raw_buffer, 0, sizeof (raw_buffer));
      memcpy (raw_buffer + hi.reg_offset, valbuf + hi.buf_offset, hi.len);
      deprecated_write_register_bytes (DEPRECATED_REGISTER_BYTE (hi.reg),
				       raw_buffer,
				       register_size (current_gdbarch,
						      hi.reg));
    }
}

/* N32/N64 ABI stuff.  */

static CORE_ADDR
mips_n32n64_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
			     struct regcache *regcache, CORE_ADDR bp_addr,
			     int nargs, struct value **args, CORE_ADDR sp,
			     int struct_return, CORE_ADDR struct_addr)
{
  int argreg;
  int float_argreg;
  int argnum;
  int len = 0;
  int stack_offset = 0;
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* For shared libraries, "t9" needs to point at the function
     address.  */
  regcache_cooked_write_signed (regcache, T9_REGNUM, func_addr);

  /* Set the return address register to point to the entry point of
     the program, where a breakpoint lies in wait.  */
  regcache_cooked_write_signed (regcache, RA_REGNUM, bp_addr);

  /* First ensure that the stack and structure return address (if any)
     are properly aligned.  The stack has to be at least 64-bit
     aligned even on 32-bit machines, because doubles must be 64-bit
     aligned.  For n32 and n64, stack frames need to be 128-bit
     aligned, so we round to this widest known alignment.  */

  sp = align_down (sp, 16);
  struct_addr = align_down (struct_addr, 16);

  /* Now make space on the stack for the args.  */
  for (argnum = 0; argnum < nargs; argnum++)
    len += align_up (TYPE_LENGTH (VALUE_TYPE (args[argnum])),
		     mips_stack_argsize (tdep));
  sp -= align_up (len, 16);

  if (mips_debug)
    fprintf_unfiltered (gdb_stdlog,
			"mips_n32n64_push_dummy_call: sp=0x%s allocated %ld\n",
			paddr_nz (sp), (long) align_up (len, 16));

  /* Initialize the integer and float register pointers.  */
  argreg = A0_REGNUM;
  float_argreg = mips_fpa0_regnum (current_gdbarch);

  /* The struct_return pointer occupies the first parameter-passing reg.  */
  if (struct_return)
    {
      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "mips_n32n64_push_dummy_call: struct_return reg=%d 0x%s\n",
			    argreg, paddr_nz (struct_addr));
      write_register (argreg++, struct_addr);
    }

  /* Now load as many as possible of the first arguments into
     registers, and push the rest onto the stack.  Loop thru args
     from first to last.  */
  for (argnum = 0; argnum < nargs; argnum++)
    {
      char *val;
      struct value *arg = args[argnum];
      struct type *arg_type = check_typedef (VALUE_TYPE (arg));
      int len = TYPE_LENGTH (arg_type);
      enum type_code typecode = TYPE_CODE (arg_type);

      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "mips_n32n64_push_dummy_call: %d len=%d type=%d",
			    argnum + 1, len, (int) typecode);

      val = (char *) VALUE_CONTENTS (arg);

      if (fp_register_arg_p (typecode, arg_type)
	  && float_argreg <= MIPS_LAST_FP_ARG_REGNUM)
	{
	  /* This is a floating point value that fits entirely
	     in a single register.  */
	  /* On 32 bit ABI's the float_argreg is further adjusted
	     above to ensure that it is even register aligned.  */
	  LONGEST regval = extract_unsigned_integer (val, len);
	  if (mips_debug)
	    fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				float_argreg, phex (regval, len));
	  write_register (float_argreg++, regval);

	  if (mips_debug)
	    fprintf_unfiltered (gdb_stdlog, " - reg=%d val=%s",
				argreg, phex (regval, len));
	  write_register (argreg, regval);
	  argreg += 1;
	}
      else
	{
	  /* Copy the argument to general registers or the stack in
	     register-sized pieces.  Large arguments are split between
	     registers and stack.  */
	  /* Note: structs whose size is not a multiple of
	     mips_regsize() are treated specially: Irix cc passes them
	     in registers where gcc sometimes puts them on the stack.
	     For maximum compatibility, we will put them in both
	     places.  */
	  int odd_sized_struct = ((len > mips_saved_regsize (tdep))
				  && (len % mips_saved_regsize (tdep) != 0));
	  /* Note: Floating-point values that didn't fit into an FP
	     register are only written to memory.  */
	  while (len > 0)
	    {
	      /* Rememer if the argument was written to the stack.  */
	      int stack_used_p = 0;
	      int partial_len = (len < mips_saved_regsize (tdep)
				 ? len : mips_saved_regsize (tdep));

	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " -- partial=%d",
				    partial_len);

	      /* Write this portion of the argument to the stack.  */
	      if (argreg > MIPS_LAST_ARG_REGNUM
		  || odd_sized_struct
		  || fp_register_arg_p (typecode, arg_type))
		{
		  /* Should shorter than int integer values be
		     promoted to int before being stored? */
		  int longword_offset = 0;
		  CORE_ADDR addr;
		  stack_used_p = 1;
		  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
		    {
		      if (mips_stack_argsize (tdep) == 8
			  && (typecode == TYPE_CODE_INT
			      || typecode == TYPE_CODE_PTR
			      || typecode == TYPE_CODE_FLT) && len <= 4)
			longword_offset = mips_stack_argsize (tdep) - len;
		    }

		  if (mips_debug)
		    {
		      fprintf_unfiltered (gdb_stdlog, " - stack_offset=0x%s",
					  paddr_nz (stack_offset));
		      fprintf_unfiltered (gdb_stdlog, " longword_offset=0x%s",
					  paddr_nz (longword_offset));
		    }

		  addr = sp + stack_offset + longword_offset;

		  if (mips_debug)
		    {
		      int i;
		      fprintf_unfiltered (gdb_stdlog, " @0x%s ",
					  paddr_nz (addr));
		      for (i = 0; i < partial_len; i++)
			{
			  fprintf_unfiltered (gdb_stdlog, "%02x",
					      val[i] & 0xff);
			}
		    }
		  write_memory (addr, val, partial_len);
		}

	      /* Note!!! This is NOT an else clause.  Odd sized
	         structs may go thru BOTH paths.  Floating point
	         arguments will not.  */
	      /* Write this portion of the argument to a general
	         purpose register.  */
	      if (argreg <= MIPS_LAST_ARG_REGNUM
		  && !fp_register_arg_p (typecode, arg_type))
		{
		  LONGEST regval =
		    extract_unsigned_integer (val, partial_len);

		  /* A non-floating-point argument being passed in a
		     general register.  If a struct or union, and if
		     the remaining length is smaller than the register
		     size, we have to adjust the register value on
		     big endian targets.

		     It does not seem to be necessary to do the
		     same for integral types.

		     cagney/2001-07-23: gdb/179: Also, GCC, when
		     outputting LE O32 with sizeof (struct) <
		     mips_saved_regsize(), generates a left shift as
		     part of storing the argument in a register a
		     register (the left shift isn't generated when
		     sizeof (struct) >= mips_saved_regsize()).  Since
		     it is quite possible that this is GCC
		     contradicting the LE/O32 ABI, GDB has not been
		     adjusted to accommodate this.  Either someone
		     needs to demonstrate that the LE/O32 ABI
		     specifies such a left shift OR this new ABI gets
		     identified as such and GDB gets tweaked
		     accordingly.  */

		  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
		      && partial_len < mips_saved_regsize (tdep)
		      && (typecode == TYPE_CODE_STRUCT ||
			  typecode == TYPE_CODE_UNION))
		    regval <<= ((mips_saved_regsize (tdep) - partial_len) *
				TARGET_CHAR_BIT);

		  if (mips_debug)
		    fprintf_filtered (gdb_stdlog, " - reg=%d val=%s",
				      argreg,
				      phex (regval,
					    mips_saved_regsize (tdep)));
		  write_register (argreg, regval);
		  argreg++;
		}

	      len -= partial_len;
	      val += partial_len;

	      /* Compute the the offset into the stack at which we
	         will copy the next parameter.

	         In N32 (N64?), the stack_offset only needs to be
	         adjusted when it has been used.  */

	      if (stack_used_p)
		stack_offset += align_up (partial_len,
					  mips_stack_argsize (tdep));
	    }
	}
      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog, "\n");
    }

  regcache_cooked_write_signed (regcache, SP_REGNUM, sp);

  /* Return adjusted stack pointer.  */
  return sp;
}

static enum return_value_convention
mips_n32n64_return_value (struct gdbarch *gdbarch,
			  struct type *type, struct regcache *regcache,
			  void *readbuf, const void *writebuf)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  if (TYPE_CODE (type) == TYPE_CODE_STRUCT
      || TYPE_CODE (type) == TYPE_CODE_UNION
      || TYPE_CODE (type) == TYPE_CODE_ARRAY
      || TYPE_LENGTH (type) > 2 * mips_saved_regsize (tdep))
    return RETURN_VALUE_STRUCT_CONVENTION;
  else if (TYPE_CODE (type) == TYPE_CODE_FLT
	   && tdep->mips_fpu_type != MIPS_FPU_NONE)
    {
      /* A floating-point value belongs in the least significant part
         of FP0.  */
      if (mips_debug)
	fprintf_unfiltered (gdb_stderr, "Return float in $fp0\n");
      mips_xfer_register (regcache,
			  NUM_REGS + mips_regnum (current_gdbarch)->fp0,
			  TYPE_LENGTH (type),
			  TARGET_BYTE_ORDER, readbuf, writebuf, 0);
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  else if (TYPE_CODE (type) == TYPE_CODE_STRUCT
	   && TYPE_NFIELDS (type) <= 2
	   && TYPE_NFIELDS (type) >= 1
	   && ((TYPE_NFIELDS (type) == 1
		&& (TYPE_CODE (TYPE_FIELD_TYPE (type, 0))
		    == TYPE_CODE_FLT))
	       || (TYPE_NFIELDS (type) == 2
		   && (TYPE_CODE (TYPE_FIELD_TYPE (type, 0))
		       == TYPE_CODE_FLT)
		   && (TYPE_CODE (TYPE_FIELD_TYPE (type, 1))
		       == TYPE_CODE_FLT)))
	   && tdep->mips_fpu_type != MIPS_FPU_NONE)
    {
      /* A struct that contains one or two floats.  Each value is part
         in the least significant part of their floating point
         register..  */
      int regnum;
      int field;
      for (field = 0, regnum = mips_regnum (current_gdbarch)->fp0;
	   field < TYPE_NFIELDS (type); field++, regnum += 2)
	{
	  int offset = (FIELD_BITPOS (TYPE_FIELDS (type)[field])
			/ TARGET_CHAR_BIT);
	  if (mips_debug)
	    fprintf_unfiltered (gdb_stderr, "Return float struct+%d\n",
				offset);
	  mips_xfer_register (regcache, NUM_REGS + regnum,
			      TYPE_LENGTH (TYPE_FIELD_TYPE (type, field)),
			      TARGET_BYTE_ORDER, readbuf, writebuf, offset);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  else if (TYPE_CODE (type) == TYPE_CODE_STRUCT
	   || TYPE_CODE (type) == TYPE_CODE_UNION)
    {
      /* A structure or union.  Extract the left justified value,
         regardless of the byte order.  I.e. DO NOT USE
         mips_xfer_lower.  */
      int offset;
      int regnum;
      for (offset = 0, regnum = V0_REGNUM;
	   offset < TYPE_LENGTH (type);
	   offset += register_size (current_gdbarch, regnum), regnum++)
	{
	  int xfer = register_size (current_gdbarch, regnum);
	  if (offset + xfer > TYPE_LENGTH (type))
	    xfer = TYPE_LENGTH (type) - offset;
	  if (mips_debug)
	    fprintf_unfiltered (gdb_stderr, "Return struct+%d:%d in $%d\n",
				offset, xfer, regnum);
	  mips_xfer_register (regcache, NUM_REGS + regnum, xfer,
			      BFD_ENDIAN_UNKNOWN, readbuf, writebuf, offset);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  else
    {
      /* A scalar extract each part but least-significant-byte
         justified.  */
      int offset;
      int regnum;
      for (offset = 0, regnum = V0_REGNUM;
	   offset < TYPE_LENGTH (type);
	   offset += register_size (current_gdbarch, regnum), regnum++)
	{
	  int xfer = register_size (current_gdbarch, regnum);
	  if (offset + xfer > TYPE_LENGTH (type))
	    xfer = TYPE_LENGTH (type) - offset;
	  if (mips_debug)
	    fprintf_unfiltered (gdb_stderr, "Return scalar+%d:%d in $%d\n",
				offset, xfer, regnum);
	  mips_xfer_register (regcache, NUM_REGS + regnum, xfer,
			      TARGET_BYTE_ORDER, readbuf, writebuf, offset);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
}

/* O32 ABI stuff.  */

static CORE_ADDR
mips_o32_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
			  struct regcache *regcache, CORE_ADDR bp_addr,
			  int nargs, struct value **args, CORE_ADDR sp,
			  int struct_return, CORE_ADDR struct_addr)
{
  int argreg;
  int float_argreg;
  int argnum;
  int len = 0;
  int stack_offset = 0;
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* For shared libraries, "t9" needs to point at the function
     address.  */
  regcache_cooked_write_signed (regcache, T9_REGNUM, func_addr);

  /* Set the return address register to point to the entry point of
     the program, where a breakpoint lies in wait.  */
  regcache_cooked_write_signed (regcache, RA_REGNUM, bp_addr);

  /* First ensure that the stack and structure return address (if any)
     are properly aligned.  The stack has to be at least 64-bit
     aligned even on 32-bit machines, because doubles must be 64-bit
     aligned.  For n32 and n64, stack frames need to be 128-bit
     aligned, so we round to this widest known alignment.  */

  sp = align_down (sp, 16);
  struct_addr = align_down (struct_addr, 16);

  /* Now make space on the stack for the args.  */
  for (argnum = 0; argnum < nargs; argnum++)
    len += align_up (TYPE_LENGTH (VALUE_TYPE (args[argnum])),
		     mips_stack_argsize (tdep));
  sp -= align_up (len, 16);

  if (mips_debug)
    fprintf_unfiltered (gdb_stdlog,
			"mips_o32_push_dummy_call: sp=0x%s allocated %ld\n",
			paddr_nz (sp), (long) align_up (len, 16));

  /* Initialize the integer and float register pointers.  */
  argreg = A0_REGNUM;
  float_argreg = mips_fpa0_regnum (current_gdbarch);

  /* The struct_return pointer occupies the first parameter-passing reg.  */
  if (struct_return)
    {
      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "mips_o32_push_dummy_call: struct_return reg=%d 0x%s\n",
			    argreg, paddr_nz (struct_addr));
      write_register (argreg++, struct_addr);
      stack_offset += mips_stack_argsize (tdep);
    }

  /* Now load as many as possible of the first arguments into
     registers, and push the rest onto the stack.  Loop thru args
     from first to last.  */
  for (argnum = 0; argnum < nargs; argnum++)
    {
      char *val;
      struct value *arg = args[argnum];
      struct type *arg_type = check_typedef (VALUE_TYPE (arg));
      int len = TYPE_LENGTH (arg_type);
      enum type_code typecode = TYPE_CODE (arg_type);

      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "mips_o32_push_dummy_call: %d len=%d type=%d",
			    argnum + 1, len, (int) typecode);

      val = (char *) VALUE_CONTENTS (arg);

      /* 32-bit ABIs always start floating point arguments in an
         even-numbered floating point register.  Round the FP register
         up before the check to see if there are any FP registers
         left.  O32/O64 targets also pass the FP in the integer
         registers so also round up normal registers.  */
      if (!FP_REGISTER_DOUBLE && fp_register_arg_p (typecode, arg_type))
	{
	  if ((float_argreg & 1))
	    float_argreg++;
	}

      /* Floating point arguments passed in registers have to be
         treated specially.  On 32-bit architectures, doubles
         are passed in register pairs; the even register gets
         the low word, and the odd register gets the high word.
         On O32/O64, the first two floating point arguments are
         also copied to general registers, because MIPS16 functions
         don't use float registers for arguments.  This duplication of
         arguments in general registers can't hurt non-MIPS16 functions
         because those registers are normally skipped.  */

      if (fp_register_arg_p (typecode, arg_type)
	  && float_argreg <= MIPS_LAST_FP_ARG_REGNUM)
	{
	  if (!FP_REGISTER_DOUBLE && len == 8)
	    {
	      int low_offset = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? 4 : 0;
	      unsigned long regval;

	      /* Write the low word of the double to the even register(s).  */
	      regval = extract_unsigned_integer (val + low_offset, 4);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, 4));
	      write_register (float_argreg++, regval);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - reg=%d val=%s",
				    argreg, phex (regval, 4));
	      write_register (argreg++, regval);

	      /* Write the high word of the double to the odd register(s).  */
	      regval = extract_unsigned_integer (val + 4 - low_offset, 4);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, 4));
	      write_register (float_argreg++, regval);

	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - reg=%d val=%s",
				    argreg, phex (regval, 4));
	      write_register (argreg++, regval);
	    }
	  else
	    {
	      /* This is a floating point value that fits entirely
	         in a single register.  */
	      /* On 32 bit ABI's the float_argreg is further adjusted
	         above to ensure that it is even register aligned.  */
	      LONGEST regval = extract_unsigned_integer (val, len);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, len));
	      write_register (float_argreg++, regval);
	      /* CAGNEY: 32 bit MIPS ABI's always reserve two FP
	         registers for each argument.  The below is (my
	         guess) to ensure that the corresponding integer
	         register has reserved the same space.  */
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - reg=%d val=%s",
				    argreg, phex (regval, len));
	      write_register (argreg, regval);
	      argreg += FP_REGISTER_DOUBLE ? 1 : 2;
	    }
	  /* Reserve space for the FP register.  */
	  stack_offset += align_up (len, mips_stack_argsize (tdep));
	}
      else
	{
	  /* Copy the argument to general registers or the stack in
	     register-sized pieces.  Large arguments are split between
	     registers and stack.  */
	  /* Note: structs whose size is not a multiple of
	     mips_regsize() are treated specially: Irix cc passes them
	     in registers where gcc sometimes puts them on the stack.
	     For maximum compatibility, we will put them in both
	     places.  */
	  int odd_sized_struct = ((len > mips_saved_regsize (tdep))
				  && (len % mips_saved_regsize (tdep) != 0));
	  /* Structures should be aligned to eight bytes (even arg registers)
	     on MIPS_ABI_O32, if their first member has double precision.  */
	  if (mips_saved_regsize (tdep) < 8
	      && mips_type_needs_double_align (arg_type))
	    {
	      if ((argreg & 1))
		argreg++;
	    }
	  /* Note: Floating-point values that didn't fit into an FP
	     register are only written to memory.  */
	  while (len > 0)
	    {
	      /* Remember if the argument was written to the stack.  */
	      int stack_used_p = 0;
	      int partial_len = (len < mips_saved_regsize (tdep)
				 ? len : mips_saved_regsize (tdep));

	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " -- partial=%d",
				    partial_len);

	      /* Write this portion of the argument to the stack.  */
	      if (argreg > MIPS_LAST_ARG_REGNUM
		  || odd_sized_struct
		  || fp_register_arg_p (typecode, arg_type))
		{
		  /* Should shorter than int integer values be
		     promoted to int before being stored? */
		  int longword_offset = 0;
		  CORE_ADDR addr;
		  stack_used_p = 1;
		  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
		    {
		      if (mips_stack_argsize (tdep) == 8
			  && (typecode == TYPE_CODE_INT
			      || typecode == TYPE_CODE_PTR
			      || typecode == TYPE_CODE_FLT) && len <= 4)
			longword_offset = mips_stack_argsize (tdep) - len;
		    }

		  if (mips_debug)
		    {
		      fprintf_unfiltered (gdb_stdlog, " - stack_offset=0x%s",
					  paddr_nz (stack_offset));
		      fprintf_unfiltered (gdb_stdlog, " longword_offset=0x%s",
					  paddr_nz (longword_offset));
		    }

		  addr = sp + stack_offset + longword_offset;

		  if (mips_debug)
		    {
		      int i;
		      fprintf_unfiltered (gdb_stdlog, " @0x%s ",
					  paddr_nz (addr));
		      for (i = 0; i < partial_len; i++)
			{
			  fprintf_unfiltered (gdb_stdlog, "%02x",
					      val[i] & 0xff);
			}
		    }
		  write_memory (addr, val, partial_len);
		}

	      /* Note!!! This is NOT an else clause.  Odd sized
	         structs may go thru BOTH paths.  Floating point
	         arguments will not.  */
	      /* Write this portion of the argument to a general
	         purpose register.  */
	      if (argreg <= MIPS_LAST_ARG_REGNUM
		  && !fp_register_arg_p (typecode, arg_type))
		{
		  LONGEST regval = extract_signed_integer (val, partial_len);
		  /* Value may need to be sign extended, because
		     mips_regsize() != mips_saved_regsize().  */

		  /* A non-floating-point argument being passed in a
		     general register.  If a struct or union, and if
		     the remaining length is smaller than the register
		     size, we have to adjust the register value on
		     big endian targets.

		     It does not seem to be necessary to do the
		     same for integral types.

		     Also don't do this adjustment on O64 binaries.

		     cagney/2001-07-23: gdb/179: Also, GCC, when
		     outputting LE O32 with sizeof (struct) <
		     mips_saved_regsize(), generates a left shift as
		     part of storing the argument in a register a
		     register (the left shift isn't generated when
		     sizeof (struct) >= mips_saved_regsize()).  Since
		     it is quite possible that this is GCC
		     contradicting the LE/O32 ABI, GDB has not been
		     adjusted to accommodate this.  Either someone
		     needs to demonstrate that the LE/O32 ABI
		     specifies such a left shift OR this new ABI gets
		     identified as such and GDB gets tweaked
		     accordingly.  */

		  if (mips_saved_regsize (tdep) < 8
		      && TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
		      && partial_len < mips_saved_regsize (tdep)
		      && (typecode == TYPE_CODE_STRUCT ||
			  typecode == TYPE_CODE_UNION))
		    regval <<= ((mips_saved_regsize (tdep) - partial_len) *
				TARGET_CHAR_BIT);

		  if (mips_debug)
		    fprintf_filtered (gdb_stdlog, " - reg=%d val=%s",
				      argreg,
				      phex (regval,
					    mips_saved_regsize (tdep)));
		  write_register (argreg, regval);
		  argreg++;

		  /* Prevent subsequent floating point arguments from
		     being passed in floating point registers.  */
		  float_argreg = MIPS_LAST_FP_ARG_REGNUM + 1;
		}

	      len -= partial_len;
	      val += partial_len;

	      /* Compute the the offset into the stack at which we
	         will copy the next parameter.

	         In older ABIs, the caller reserved space for
	         registers that contained arguments.  This was loosely
	         refered to as their "home".  Consequently, space is
	         always allocated.  */

	      stack_offset += align_up (partial_len,
					mips_stack_argsize (tdep));
	    }
	}
      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog, "\n");
    }

  regcache_cooked_write_signed (regcache, SP_REGNUM, sp);

  /* Return adjusted stack pointer.  */
  return sp;
}

static enum return_value_convention
mips_o32_return_value (struct gdbarch *gdbarch, struct type *type,
		       struct regcache *regcache,
		       void *readbuf, const void *writebuf)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  if (TYPE_CODE (type) == TYPE_CODE_STRUCT
      || TYPE_CODE (type) == TYPE_CODE_UNION
      || TYPE_CODE (type) == TYPE_CODE_ARRAY)
    return RETURN_VALUE_STRUCT_CONVENTION;
  else if (TYPE_CODE (type) == TYPE_CODE_FLT
	   && TYPE_LENGTH (type) == 4 && tdep->mips_fpu_type != MIPS_FPU_NONE)
    {
      /* A single-precision floating-point value.  It fits in the
         least significant part of FP0.  */
      if (mips_debug)
	fprintf_unfiltered (gdb_stderr, "Return float in $fp0\n");
      mips_xfer_register (regcache,
			  NUM_REGS + mips_regnum (current_gdbarch)->fp0,
			  TYPE_LENGTH (type),
			  TARGET_BYTE_ORDER, readbuf, writebuf, 0);
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
  else if (TYPE_CODE (type) == TYPE_CODE_FLT
	   && TYPE_LENGTH (type) == 8 && tdep->mips_fpu_type != MIPS_FPU_NONE)
    {
      /* A double-precision floating-point value.  The most
         significant part goes in FP1, and the least significant in
         FP0.  */
      if (mips_debug)
	fprintf_unfiltered (gdb_stderr, "Return float in $fp1/$fp0\n");
      switch (TARGET_BYTE_ORDER)
	{
	case BFD_ENDIAN_LITTLE:
	  mips_xfer_register (regcache,
			      NUM_REGS + mips_regnum (current_gdbarch)->fp0 +
			      0, 4, TARGET_BYTE_ORDER, readbuf, writebuf, 0);
	  mips_xfer_register (regcache,
			      NUM_REGS + mips_regnum (current_gdbarch)->fp0 +
			      1, 4, TARGET_BYTE_ORDER, readbuf, writebuf, 4);
	  break;
	case BFD_ENDIAN_BIG:
	  mips_xfer_register (regcache,
			      NUM_REGS + mips_regnum (current_gdbarch)->fp0 +
			      1, 4, TARGET_BYTE_ORDER, readbuf, writebuf, 0);
	  mips_xfer_register (regcache,
			      NUM_REGS + mips_regnum (current_gdbarch)->fp0 +
			      0, 4, TARGET_BYTE_ORDER, readbuf, writebuf, 4);
	  break;
	default:
	  internal_error (__FILE__, __LINE__, "bad switch");
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
#if 0
  else if (TYPE_CODE (type) == TYPE_CODE_STRUCT
	   && TYPE_NFIELDS (type) <= 2
	   && TYPE_NFIELDS (type) >= 1
	   && ((TYPE_NFIELDS (type) == 1
		&& (TYPE_CODE (TYPE_FIELD_TYPE (type, 0))
		    == TYPE_CODE_FLT))
	       || (TYPE_NFIELDS (type) == 2
		   && (TYPE_CODE (TYPE_FIELD_TYPE (type, 0))
		       == TYPE_CODE_FLT)
		   && (TYPE_CODE (TYPE_FIELD_TYPE (type, 1))
		       == TYPE_CODE_FLT)))
	   && tdep->mips_fpu_type != MIPS_FPU_NONE)
    {
      /* A struct that contains one or two floats.  Each value is part
         in the least significant part of their floating point
         register..  */
      bfd_byte reg[MAX_REGISTER_SIZE];
      int regnum;
      int field;
      for (field = 0, regnum = mips_regnum (current_gdbarch)->fp0;
	   field < TYPE_NFIELDS (type); field++, regnum += 2)
	{
	  int offset = (FIELD_BITPOS (TYPE_FIELDS (type)[field])
			/ TARGET_CHAR_BIT);
	  if (mips_debug)
	    fprintf_unfiltered (gdb_stderr, "Return float struct+%d\n",
				offset);
	  mips_xfer_register (regcache, NUM_REGS + regnum,
			      TYPE_LENGTH (TYPE_FIELD_TYPE (type, field)),
			      TARGET_BYTE_ORDER, readbuf, writebuf, offset);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
#endif
#if 0
  else if (TYPE_CODE (type) == TYPE_CODE_STRUCT
	   || TYPE_CODE (type) == TYPE_CODE_UNION)
    {
      /* A structure or union.  Extract the left justified value,
         regardless of the byte order.  I.e. DO NOT USE
         mips_xfer_lower.  */
      int offset;
      int regnum;
      for (offset = 0, regnum = V0_REGNUM;
	   offset < TYPE_LENGTH (type);
	   offset += register_size (current_gdbarch, regnum), regnum++)
	{
	  int xfer = register_size (current_gdbarch, regnum);
	  if (offset + xfer > TYPE_LENGTH (type))
	    xfer = TYPE_LENGTH (type) - offset;
	  if (mips_debug)
	    fprintf_unfiltered (gdb_stderr, "Return struct+%d:%d in $%d\n",
				offset, xfer, regnum);
	  mips_xfer_register (regcache, NUM_REGS + regnum, xfer,
			      BFD_ENDIAN_UNKNOWN, readbuf, writebuf, offset);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
#endif
  else
    {
      /* A scalar extract each part but least-significant-byte
         justified.  o32 thinks registers are 4 byte, regardless of
         the ISA.  mips_stack_argsize controls this.  */
      int offset;
      int regnum;
      for (offset = 0, regnum = V0_REGNUM;
	   offset < TYPE_LENGTH (type);
	   offset += mips_stack_argsize (tdep), regnum++)
	{
	  int xfer = mips_stack_argsize (tdep);
	  if (offset + xfer > TYPE_LENGTH (type))
	    xfer = TYPE_LENGTH (type) - offset;
	  if (mips_debug)
	    fprintf_unfiltered (gdb_stderr, "Return scalar+%d:%d in $%d\n",
				offset, xfer, regnum);
	  mips_xfer_register (regcache, NUM_REGS + regnum, xfer,
			      TARGET_BYTE_ORDER, readbuf, writebuf, offset);
	}
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
}

/* O64 ABI.  This is a hacked up kind of 64-bit version of the o32
   ABI.  */

static CORE_ADDR
mips_o64_push_dummy_call (struct gdbarch *gdbarch, CORE_ADDR func_addr,
			  struct regcache *regcache, CORE_ADDR bp_addr,
			  int nargs,
			  struct value **args, CORE_ADDR sp,
			  int struct_return, CORE_ADDR struct_addr)
{
  int argreg;
  int float_argreg;
  int argnum;
  int len = 0;
  int stack_offset = 0;
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* For shared libraries, "t9" needs to point at the function
     address.  */
  regcache_cooked_write_signed (regcache, T9_REGNUM, func_addr);

  /* Set the return address register to point to the entry point of
     the program, where a breakpoint lies in wait.  */
  regcache_cooked_write_signed (regcache, RA_REGNUM, bp_addr);

  /* First ensure that the stack and structure return address (if any)
     are properly aligned.  The stack has to be at least 64-bit
     aligned even on 32-bit machines, because doubles must be 64-bit
     aligned.  For n32 and n64, stack frames need to be 128-bit
     aligned, so we round to this widest known alignment.  */

  sp = align_down (sp, 16);
  struct_addr = align_down (struct_addr, 16);

  /* Now make space on the stack for the args.  */
  for (argnum = 0; argnum < nargs; argnum++)
    len += align_up (TYPE_LENGTH (VALUE_TYPE (args[argnum])),
		     mips_stack_argsize (tdep));
  sp -= align_up (len, 16);

  if (mips_debug)
    fprintf_unfiltered (gdb_stdlog,
			"mips_o64_push_dummy_call: sp=0x%s allocated %ld\n",
			paddr_nz (sp), (long) align_up (len, 16));

  /* Initialize the integer and float register pointers.  */
  argreg = A0_REGNUM;
  float_argreg = mips_fpa0_regnum (current_gdbarch);

  /* The struct_return pointer occupies the first parameter-passing reg.  */
  if (struct_return)
    {
      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "mips_o64_push_dummy_call: struct_return reg=%d 0x%s\n",
			    argreg, paddr_nz (struct_addr));
      write_register (argreg++, struct_addr);
      stack_offset += mips_stack_argsize (tdep);
    }

  /* Now load as many as possible of the first arguments into
     registers, and push the rest onto the stack.  Loop thru args
     from first to last.  */
  for (argnum = 0; argnum < nargs; argnum++)
    {
      char *val;
      struct value *arg = args[argnum];
      struct type *arg_type = check_typedef (VALUE_TYPE (arg));
      int len = TYPE_LENGTH (arg_type);
      enum type_code typecode = TYPE_CODE (arg_type);

      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog,
			    "mips_o64_push_dummy_call: %d len=%d type=%d",
			    argnum + 1, len, (int) typecode);

      val = (char *) VALUE_CONTENTS (arg);

      /* 32-bit ABIs always start floating point arguments in an
         even-numbered floating point register.  Round the FP register
         up before the check to see if there are any FP registers
         left.  O32/O64 targets also pass the FP in the integer
         registers so also round up normal registers.  */
      if (!FP_REGISTER_DOUBLE && fp_register_arg_p (typecode, arg_type))
	{
	  if ((float_argreg & 1))
	    float_argreg++;
	}

      /* Floating point arguments passed in registers have to be
         treated specially.  On 32-bit architectures, doubles
         are passed in register pairs; the even register gets
         the low word, and the odd register gets the high word.
         On O32/O64, the first two floating point arguments are
         also copied to general registers, because MIPS16 functions
         don't use float registers for arguments.  This duplication of
         arguments in general registers can't hurt non-MIPS16 functions
         because those registers are normally skipped.  */

      if (fp_register_arg_p (typecode, arg_type)
	  && float_argreg <= MIPS_LAST_FP_ARG_REGNUM)
	{
	  if (!FP_REGISTER_DOUBLE && len == 8)
	    {
	      int low_offset = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? 4 : 0;
	      unsigned long regval;

	      /* Write the low word of the double to the even register(s).  */
	      regval = extract_unsigned_integer (val + low_offset, 4);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, 4));
	      write_register (float_argreg++, regval);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - reg=%d val=%s",
				    argreg, phex (regval, 4));
	      write_register (argreg++, regval);

	      /* Write the high word of the double to the odd register(s).  */
	      regval = extract_unsigned_integer (val + 4 - low_offset, 4);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, 4));
	      write_register (float_argreg++, regval);

	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - reg=%d val=%s",
				    argreg, phex (regval, 4));
	      write_register (argreg++, regval);
	    }
	  else
	    {
	      /* This is a floating point value that fits entirely
	         in a single register.  */
	      /* On 32 bit ABI's the float_argreg is further adjusted
	         above to ensure that it is even register aligned.  */
	      LONGEST regval = extract_unsigned_integer (val, len);
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - fpreg=%d val=%s",
				    float_argreg, phex (regval, len));
	      write_register (float_argreg++, regval);
	      /* CAGNEY: 32 bit MIPS ABI's always reserve two FP
	         registers for each argument.  The below is (my
	         guess) to ensure that the corresponding integer
	         register has reserved the same space.  */
	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " - reg=%d val=%s",
				    argreg, phex (regval, len));
	      write_register (argreg, regval);
	      argreg += FP_REGISTER_DOUBLE ? 1 : 2;
	    }
	  /* Reserve space for the FP register.  */
	  stack_offset += align_up (len, mips_stack_argsize (tdep));
	}
      else
	{
	  /* Copy the argument to general registers or the stack in
	     register-sized pieces.  Large arguments are split between
	     registers and stack.  */
	  /* Note: structs whose size is not a multiple of
	     mips_regsize() are treated specially: Irix cc passes them
	     in registers where gcc sometimes puts them on the stack.
	     For maximum compatibility, we will put them in both
	     places.  */
	  int odd_sized_struct = ((len > mips_saved_regsize (tdep))
				  && (len % mips_saved_regsize (tdep) != 0));
	  /* Structures should be aligned to eight bytes (even arg registers)
	     on MIPS_ABI_O32, if their first member has double precision.  */
	  if (mips_saved_regsize (tdep) < 8
	      && mips_type_needs_double_align (arg_type))
	    {
	      if ((argreg & 1))
		argreg++;
	    }
	  /* Note: Floating-point values that didn't fit into an FP
	     register are only written to memory.  */
	  while (len > 0)
	    {
	      /* Remember if the argument was written to the stack.  */
	      int stack_used_p = 0;
	      int partial_len = (len < mips_saved_regsize (tdep)
				 ? len : mips_saved_regsize (tdep));

	      if (mips_debug)
		fprintf_unfiltered (gdb_stdlog, " -- partial=%d",
				    partial_len);

	      /* Write this portion of the argument to the stack.  */
	      if (argreg > MIPS_LAST_ARG_REGNUM
		  || odd_sized_struct
		  || fp_register_arg_p (typecode, arg_type))
		{
		  /* Should shorter than int integer values be
		     promoted to int before being stored? */
		  int longword_offset = 0;
		  CORE_ADDR addr;
		  stack_used_p = 1;
		  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
		    {
		      if (mips_stack_argsize (tdep) == 8
			  && (typecode == TYPE_CODE_INT
			      || typecode == TYPE_CODE_PTR
			      || typecode == TYPE_CODE_FLT) && len <= 4)
			longword_offset = mips_stack_argsize (tdep) - len;
		    }

		  if (mips_debug)
		    {
		      fprintf_unfiltered (gdb_stdlog, " - stack_offset=0x%s",
					  paddr_nz (stack_offset));
		      fprintf_unfiltered (gdb_stdlog, " longword_offset=0x%s",
					  paddr_nz (longword_offset));
		    }

		  addr = sp + stack_offset + longword_offset;

		  if (mips_debug)
		    {
		      int i;
		      fprintf_unfiltered (gdb_stdlog, " @0x%s ",
					  paddr_nz (addr));
		      for (i = 0; i < partial_len; i++)
			{
			  fprintf_unfiltered (gdb_stdlog, "%02x",
					      val[i] & 0xff);
			}
		    }
		  write_memory (addr, val, partial_len);
		}

	      /* Note!!! This is NOT an else clause.  Odd sized
	         structs may go thru BOTH paths.  Floating point
	         arguments will not.  */
	      /* Write this portion of the argument to a general
	         purpose register.  */
	      if (argreg <= MIPS_LAST_ARG_REGNUM
		  && !fp_register_arg_p (typecode, arg_type))
		{
		  LONGEST regval = extract_signed_integer (val, partial_len);
		  /* Value may need to be sign extended, because
		     mips_regsize() != mips_saved_regsize().  */

		  /* A non-floating-point argument being passed in a
		     general register.  If a struct or union, and if
		     the remaining length is smaller than the register
		     size, we have to adjust the register value on
		     big endian targets.

		     It does not seem to be necessary to do the
		     same for integral types.

		     Also don't do this adjustment on O64 binaries.

		     cagney/2001-07-23: gdb/179: Also, GCC, when
		     outputting LE O32 with sizeof (struct) <
		     mips_saved_regsize(), generates a left shift as
		     part of storing the argument in a register a
		     register (the left shift isn't generated when
		     sizeof (struct) >= mips_saved_regsize()).  Since
		     it is quite possible that this is GCC
		     contradicting the LE/O32 ABI, GDB has not been
		     adjusted to accommodate this.  Either someone
		     needs to demonstrate that the LE/O32 ABI
		     specifies such a left shift OR this new ABI gets
		     identified as such and GDB gets tweaked
		     accordingly.  */

		  if (mips_saved_regsize (tdep) < 8
		      && TARGET_BYTE_ORDER == BFD_ENDIAN_BIG
		      && partial_len < mips_saved_regsize (tdep)
		      && (typecode == TYPE_CODE_STRUCT ||
			  typecode == TYPE_CODE_UNION))
		    regval <<= ((mips_saved_regsize (tdep) - partial_len) *
				TARGET_CHAR_BIT);

		  if (mips_debug)
		    fprintf_filtered (gdb_stdlog, " - reg=%d val=%s",
				      argreg,
				      phex (regval,
					    mips_saved_regsize (tdep)));
		  write_register (argreg, regval);
		  argreg++;

		  /* Prevent subsequent floating point arguments from
		     being passed in floating point registers.  */
		  float_argreg = MIPS_LAST_FP_ARG_REGNUM + 1;
		}

	      len -= partial_len;
	      val += partial_len;

	      /* Compute the the offset into the stack at which we
	         will copy the next parameter.

	         In older ABIs, the caller reserved space for
	         registers that contained arguments.  This was loosely
	         refered to as their "home".  Consequently, space is
	         always allocated.  */

	      stack_offset += align_up (partial_len,
					mips_stack_argsize (tdep));
	    }
	}
      if (mips_debug)
	fprintf_unfiltered (gdb_stdlog, "\n");
    }

  regcache_cooked_write_signed (regcache, SP_REGNUM, sp);

  /* Return adjusted stack pointer.  */
  return sp;
}

static void
mips_o64_extract_return_value (struct type *valtype,
			       char regbuf[], char *valbuf)
{
  struct return_value_word lo;
  struct return_value_word hi;
  return_value_location (valtype, &hi, &lo);

  memcpy (valbuf + lo.buf_offset,
	  regbuf + DEPRECATED_REGISTER_BYTE (NUM_REGS + lo.reg) +
	  lo.reg_offset, lo.len);

  if (hi.len > 0)
    memcpy (valbuf + hi.buf_offset,
	    regbuf + DEPRECATED_REGISTER_BYTE (NUM_REGS + hi.reg) +
	    hi.reg_offset, hi.len);
}

static void
mips_o64_store_return_value (struct type *valtype, char *valbuf)
{
  char raw_buffer[MAX_REGISTER_SIZE];
  struct return_value_word lo;
  struct return_value_word hi;
  return_value_location (valtype, &hi, &lo);

  memset (raw_buffer, 0, sizeof (raw_buffer));
  memcpy (raw_buffer + lo.reg_offset, valbuf + lo.buf_offset, lo.len);
  deprecated_write_register_bytes (DEPRECATED_REGISTER_BYTE (lo.reg),
				   raw_buffer, register_size (current_gdbarch,
							      lo.reg));

  if (hi.len > 0)
    {
      memset (raw_buffer, 0, sizeof (raw_buffer));
      memcpy (raw_buffer + hi.reg_offset, valbuf + hi.buf_offset, hi.len);
      deprecated_write_register_bytes (DEPRECATED_REGISTER_BYTE (hi.reg),
				       raw_buffer,
				       register_size (current_gdbarch,
						      hi.reg));
    }
}

/* Floating point register management.

   Background: MIPS1 & 2 fp registers are 32 bits wide.  To support
   64bit operations, these early MIPS cpus treat fp register pairs
   (f0,f1) as a single register (d0).  Later MIPS cpu's have 64 bit fp
   registers and offer a compatibility mode that emulates the MIPS2 fp
   model.  When operating in MIPS2 fp compat mode, later cpu's split
   double precision floats into two 32-bit chunks and store them in
   consecutive fp regs.  To display 64-bit floats stored in this
   fashion, we have to combine 32 bits from f0 and 32 bits from f1.
   Throw in user-configurable endianness and you have a real mess.

   The way this works is:
     - If we are in 32-bit mode or on a 32-bit processor, then a 64-bit
       double-precision value will be split across two logical registers.
       The lower-numbered logical register will hold the low-order bits,
       regardless of the processor's endianness.
     - If we are on a 64-bit processor, and we are looking for a
       single-precision value, it will be in the low ordered bits
       of a 64-bit GPR (after mfc1, for example) or a 64-bit register
       save slot in memory.
     - If we are in 64-bit mode, everything is straightforward.

   Note that this code only deals with "live" registers at the top of the
   stack.  We will attempt to deal with saved registers later, when
   the raw/cooked register interface is in place. (We need a general
   interface that can deal with dynamic saved register sizes -- fp
   regs could be 32 bits wide in one frame and 64 on the frame above
   and below).  */

static struct type *
mips_float_register_type (void)
{
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    return builtin_type_ieee_single_big;
  else
    return builtin_type_ieee_single_little;
}

static struct type *
mips_double_register_type (void)
{
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    return builtin_type_ieee_double_big;
  else
    return builtin_type_ieee_double_little;
}

/* Copy a 32-bit single-precision value from the current frame
   into rare_buffer.  */

static void
mips_read_fp_register_single (struct frame_info *frame, int regno,
			      char *rare_buffer)
{
  int raw_size = register_size (current_gdbarch, regno);
  char *raw_buffer = alloca (raw_size);

  if (!frame_register_read (frame, regno, raw_buffer))
    error ("can't read register %d (%s)", regno, REGISTER_NAME (regno));
  if (raw_size == 8)
    {
      /* We have a 64-bit value for this register.  Find the low-order
         32 bits.  */
      int offset;

      if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
	offset = 4;
      else
	offset = 0;

      memcpy (rare_buffer, raw_buffer + offset, 4);
    }
  else
    {
      memcpy (rare_buffer, raw_buffer, 4);
    }
}

/* Copy a 64-bit double-precision value from the current frame into
   rare_buffer.  This may include getting half of it from the next
   register.  */

static void
mips_read_fp_register_double (struct frame_info *frame, int regno,
			      char *rare_buffer)
{
  int raw_size = register_size (current_gdbarch, regno);

  if (raw_size == 8 && !mips2_fp_compat ())
    {
      /* We have a 64-bit value for this register, and we should use
         all 64 bits.  */
      if (!frame_register_read (frame, regno, rare_buffer))
	error ("can't read register %d (%s)", regno, REGISTER_NAME (regno));
    }
  else
    {
      if ((regno - mips_regnum (current_gdbarch)->fp0) & 1)
	internal_error (__FILE__, __LINE__,
			"mips_read_fp_register_double: bad access to "
			"odd-numbered FP register");

      /* mips_read_fp_register_single will find the correct 32 bits from
         each register.  */
      if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
	{
	  mips_read_fp_register_single (frame, regno, rare_buffer + 4);
	  mips_read_fp_register_single (frame, regno + 1, rare_buffer);
	}
      else
	{
	  mips_read_fp_register_single (frame, regno, rare_buffer);
	  mips_read_fp_register_single (frame, regno + 1, rare_buffer + 4);
	}
    }
}

static void
mips_print_fp_register (struct ui_file *file, struct frame_info *frame,
			int regnum)
{				/* do values for FP (float) regs */
  char *raw_buffer;
  double doub, flt1;	/* doubles extracted from raw hex data */
  int inv1, inv2;

  raw_buffer =
    (char *) alloca (2 *
		     register_size (current_gdbarch,
				    mips_regnum (current_gdbarch)->fp0));

  fprintf_filtered (file, "%s:", REGISTER_NAME (regnum));
  fprintf_filtered (file, "%*s", 4 - (int) strlen (REGISTER_NAME (regnum)),
		    "");

  if (register_size (current_gdbarch, regnum) == 4 || mips2_fp_compat ())
    {
      /* 4-byte registers: Print hex and floating.  Also print even
         numbered registers as doubles.  */
      mips_read_fp_register_single (frame, regnum, raw_buffer);
      flt1 = unpack_double (mips_float_register_type (), raw_buffer, &inv1);

      print_scalar_formatted (raw_buffer, builtin_type_uint32, 'x', 'w',
			      file);

      fprintf_filtered (file, " flt: ");
      if (inv1)
	fprintf_filtered (file, " <invalid float> ");
      else
	fprintf_filtered (file, "%-17.9g", flt1);

      if (regnum % 2 == 0)
	{
	  mips_read_fp_register_double (frame, regnum, raw_buffer);
	  doub = unpack_double (mips_double_register_type (), raw_buffer,
				&inv2);

	  fprintf_filtered (file, " dbl: ");
	  if (inv2)
	    fprintf_filtered (file, "<invalid double>");
	  else
	    fprintf_filtered (file, "%-24.17g", doub);
	}
    }
  else
    {
      /* Eight byte registers: print each one as hex, float and double.  */
      mips_read_fp_register_single (frame, regnum, raw_buffer);
      flt1 = unpack_double (mips_float_register_type (), raw_buffer, &inv1);

      mips_read_fp_register_double (frame, regnum, raw_buffer);
      doub = unpack_double (mips_double_register_type (), raw_buffer, &inv2);


      print_scalar_formatted (raw_buffer, builtin_type_uint64, 'x', 'g',
			      file);

      fprintf_filtered (file, " flt: ");
      if (inv1)
	fprintf_filtered (file, "<invalid float>");
      else
	fprintf_filtered (file, "%-17.9g", flt1);

      fprintf_filtered (file, " dbl: ");
      if (inv2)
	fprintf_filtered (file, "<invalid double>");
      else
	fprintf_filtered (file, "%-24.17g", doub);
    }
}

static void
mips_print_register (struct ui_file *file, struct frame_info *frame,
		     int regnum, int all)
{
  struct gdbarch *gdbarch = get_frame_arch (frame);
  char raw_buffer[MAX_REGISTER_SIZE];
  int offset;

  if (TYPE_CODE (gdbarch_register_type (gdbarch, regnum)) == TYPE_CODE_FLT)
    {
      mips_print_fp_register (file, frame, regnum);
      return;
    }

  /* Get the data in raw format.  */
  if (!frame_register_read (frame, regnum, raw_buffer))
    {
      fprintf_filtered (file, "%s: [Invalid]", REGISTER_NAME (regnum));
      return;
    }

  fputs_filtered (REGISTER_NAME (regnum), file);

  /* The problem with printing numeric register names (r26, etc.) is that
     the user can't use them on input.  Probably the best solution is to
     fix it so that either the numeric or the funky (a2, etc.) names
     are accepted on input.  */
  if (regnum < MIPS_NUMREGS)
    fprintf_filtered (file, "(r%d): ", regnum);
  else
    fprintf_filtered (file, ": ");

  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    offset =
      register_size (current_gdbarch,
		     regnum) - register_size (current_gdbarch, regnum);
  else
    offset = 0;

  print_scalar_formatted (raw_buffer + offset,
			  gdbarch_register_type (gdbarch, regnum), 'x', 0,
			  file);
}

/* Replacement for generic do_registers_info.
   Print regs in pretty columns.  */

static int
print_fp_register_row (struct ui_file *file, struct frame_info *frame,
		       int regnum)
{
  fprintf_filtered (file, " ");
  mips_print_fp_register (file, frame, regnum);
  fprintf_filtered (file, "\n");
  return regnum + 1;
}


/* Print a row's worth of GP (int) registers, with name labels above */

static int
print_gp_register_row (struct ui_file *file, struct frame_info *frame,
		       int start_regnum)
{
  struct gdbarch *gdbarch = get_frame_arch (frame);
  /* do values for GP (int) regs */
  char raw_buffer[MAX_REGISTER_SIZE];
  int ncols = (mips_regsize (gdbarch) == 8 ? 4 : 8);	/* display cols per row */
  int col, byte;
  int regnum;

  /* For GP registers, we print a separate row of names above the vals */
  fprintf_filtered (file, "     ");
  for (col = 0, regnum = start_regnum;
       col < ncols && regnum < NUM_REGS + NUM_PSEUDO_REGS; regnum++)
    {
      if (*REGISTER_NAME (regnum) == '\0')
	continue;		/* unused register */
      if (TYPE_CODE (gdbarch_register_type (gdbarch, regnum)) ==
	  TYPE_CODE_FLT)
	break;			/* end the row: reached FP register */
      fprintf_filtered (file,
			mips_regsize (current_gdbarch) == 8 ? "%17s" : "%9s",
			REGISTER_NAME (regnum));
      col++;
    }
  /* print the R0 to R31 names */
  if ((start_regnum % NUM_REGS) < MIPS_NUMREGS)
    fprintf_filtered (file, "\n R%-4d", start_regnum % NUM_REGS);
  else
    fprintf_filtered (file, "\n      ");

  /* now print the values in hex, 4 or 8 to the row */
  for (col = 0, regnum = start_regnum;
       col < ncols && regnum < NUM_REGS + NUM_PSEUDO_REGS; regnum++)
    {
      if (*REGISTER_NAME (regnum) == '\0')
	continue;		/* unused register */
      if (TYPE_CODE (gdbarch_register_type (gdbarch, regnum)) ==
	  TYPE_CODE_FLT)
	break;			/* end row: reached FP register */
      /* OK: get the data in raw format.  */
      if (!frame_register_read (frame, regnum, raw_buffer))
	error ("can't read register %d (%s)", regnum, REGISTER_NAME (regnum));
      /* pad small registers */
      for (byte = 0;
	   byte < (mips_regsize (current_gdbarch)
		   - register_size (current_gdbarch, regnum)); byte++)
	printf_filtered ("  ");
      /* Now print the register value in hex, endian order. */
      if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
	for (byte =
	     register_size (current_gdbarch,
			    regnum) - register_size (current_gdbarch, regnum);
	     byte < register_size (current_gdbarch, regnum); byte++)
	  fprintf_filtered (file, "%02x", (unsigned char) raw_buffer[byte]);
      else
	for (byte = register_size (current_gdbarch, regnum) - 1;
	     byte >= 0; byte--)
	  fprintf_filtered (file, "%02x", (unsigned char) raw_buffer[byte]);
      fprintf_filtered (file, " ");
      col++;
    }
  if (col > 0)			/* ie. if we actually printed anything... */
    fprintf_filtered (file, "\n");

  return regnum;
}

/* MIPS_DO_REGISTERS_INFO(): called by "info register" command */

static void
mips_print_registers_info (struct gdbarch *gdbarch, struct ui_file *file,
			   struct frame_info *frame, int regnum, int all)
{
  if (regnum != -1)		/* do one specified register */
    {
      gdb_assert (regnum >= NUM_REGS);
      if (*(REGISTER_NAME (regnum)) == '\0')
	error ("Not a valid register for the current processor type");

      mips_print_register (file, frame, regnum, 0);
      fprintf_filtered (file, "\n");
    }
  else
    /* do all (or most) registers */
    {
      regnum = NUM_REGS;
      while (regnum < NUM_REGS + NUM_PSEUDO_REGS)
	{
	  if (TYPE_CODE (gdbarch_register_type (gdbarch, regnum)) ==
	      TYPE_CODE_FLT)
	    {
	      if (all)		/* true for "INFO ALL-REGISTERS" command */
		regnum = print_fp_register_row (file, frame, regnum);
	      else
		regnum += MIPS_NUMREGS;	/* skip floating point regs */
	    }
	  else
	    regnum = print_gp_register_row (file, frame, regnum);
	}
    }
}

/* Is this a branch with a delay slot?  */

static int is_delayed (unsigned long);

static int
is_delayed (unsigned long insn)
{
  int i;
  for (i = 0; i < NUMOPCODES; ++i)
    if (mips_opcodes[i].pinfo != INSN_MACRO
	&& (insn & mips_opcodes[i].mask) == mips_opcodes[i].match)
      break;
  return (i < NUMOPCODES
	  && (mips_opcodes[i].pinfo & (INSN_UNCOND_BRANCH_DELAY
				       | INSN_COND_BRANCH_DELAY
				       | INSN_COND_BRANCH_LIKELY)));
}

int
mips_step_skips_delay (CORE_ADDR pc)
{
  char buf[MIPS_INSTLEN];

  /* There is no branch delay slot on MIPS16.  */
  if (pc_is_mips16 (pc))
    return 0;

  if (target_read_memory (pc, buf, MIPS_INSTLEN) != 0)
    /* If error reading memory, guess that it is not a delayed branch.  */
    return 0;
  return is_delayed ((unsigned long)
		     extract_unsigned_integer (buf, MIPS_INSTLEN));
}

/* Skip the PC past function prologue instructions (32-bit version).
   This is a helper function for mips_skip_prologue.  */

static CORE_ADDR
mips32_skip_prologue (CORE_ADDR pc)
{
  t_inst inst;
  CORE_ADDR end_pc;
  int seen_sp_adjust = 0;
  int load_immediate_bytes = 0;

  /* Find an upper bound on the prologue.  */
  end_pc = skip_prologue_using_sal (pc);
  if (end_pc == 0)
    end_pc = pc + 100;		/* Magic.  */

  /* Skip the typical prologue instructions. These are the stack adjustment
     instruction and the instructions that save registers on the stack
     or in the gcc frame.  */
  for (; pc < end_pc; pc += MIPS_INSTLEN)
    {
      unsigned long high_word;

      inst = mips_fetch_instruction (pc);
      high_word = (inst >> 16) & 0xffff;

      if (high_word == 0x27bd	/* addiu $sp,$sp,offset */
	  || high_word == 0x67bd)	/* daddiu $sp,$sp,offset */
	seen_sp_adjust = 1;
      else if (inst == 0x03a1e823 ||	/* subu $sp,$sp,$at */
	       inst == 0x03a8e823)	/* subu $sp,$sp,$t0 */
	seen_sp_adjust = 1;
      else if (((inst & 0xFFE00000) == 0xAFA00000	/* sw reg,n($sp) */
		|| (inst & 0xFFE00000) == 0xFFA00000)	/* sd reg,n($sp) */
	       && (inst & 0x001F0000))	/* reg != $zero */
	continue;

      else if ((inst & 0xFFE00000) == 0xE7A00000)	/* swc1 freg,n($sp) */
	continue;
      else if ((inst & 0xF3E00000) == 0xA3C00000 && (inst & 0x001F0000))
	/* sx reg,n($s8) */
	continue;		/* reg != $zero */

      /* move $s8,$sp.  With different versions of gas this will be either
         `addu $s8,$sp,$zero' or `or $s8,$sp,$zero' or `daddu s8,sp,$0'.
         Accept any one of these.  */
      else if (inst == 0x03A0F021 || inst == 0x03a0f025 || inst == 0x03a0f02d)
	continue;

      else if ((inst & 0xFF9F07FF) == 0x00800021)	/* move reg,$a0-$a3 */
	continue;
      else if (high_word == 0x3c1c)	/* lui $gp,n */
	continue;
      else if (high_word == 0x279c)	/* addiu $gp,$gp,n */
	continue;
      else if (inst == 0x0399e021	/* addu $gp,$gp,$t9 */
	       || inst == 0x033ce021)	/* addu $gp,$t9,$gp */
	continue;
      /* The following instructions load $at or $t0 with an immediate
         value in preparation for a stack adjustment via
         subu $sp,$sp,[$at,$t0]. These instructions could also initialize
         a local variable, so we accept them only before a stack adjustment
         instruction was seen.  */
      else if (!seen_sp_adjust)
	{
	  if (high_word == 0x3c01 ||	/* lui $at,n */
	      high_word == 0x3c08)	/* lui $t0,n */
	    {
	      load_immediate_bytes += MIPS_INSTLEN;	/* FIXME!! */
	      continue;
	    }
	  else if (high_word == 0x3421 ||	/* ori $at,$at,n */
		   high_word == 0x3508 ||	/* ori $t0,$t0,n */
		   high_word == 0x3401 ||	/* ori $at,$zero,n */
		   high_word == 0x3408)	/* ori $t0,$zero,n */
	    {
	      load_immediate_bytes += MIPS_INSTLEN;	/* FIXME!! */
	      continue;
	    }
	  else
	    break;
	}
      else
	break;
    }

  /* In a frameless function, we might have incorrectly
     skipped some load immediate instructions. Undo the skipping
     if the load immediate was not followed by a stack adjustment.  */
  if (load_immediate_bytes && !seen_sp_adjust)
    pc -= load_immediate_bytes;
  return pc;
}

/* Skip the PC past function prologue instructions (16-bit version).
   This is a helper function for mips_skip_prologue.  */

static CORE_ADDR
mips16_skip_prologue (CORE_ADDR pc)
{
  CORE_ADDR end_pc;
  int extend_bytes = 0;
  int prev_extend_bytes;

  /* Table of instructions likely to be found in a function prologue.  */
  static struct
  {
    unsigned short inst;
    unsigned short mask;
  }
  table[] =
  {
    {
    0x6300, 0xff00}
    ,				/* addiu $sp,offset */
    {
    0xfb00, 0xff00}
    ,				/* daddiu $sp,offset */
    {
    0xd000, 0xf800}
    ,				/* sw reg,n($sp) */
    {
    0xf900, 0xff00}
    ,				/* sd reg,n($sp) */
    {
    0x6200, 0xff00}
    ,				/* sw $ra,n($sp) */
    {
    0xfa00, 0xff00}
    ,				/* sd $ra,n($sp) */
    {
    0x673d, 0xffff}
    ,				/* move $s1,sp */
    {
    0xd980, 0xff80}
    ,				/* sw $a0-$a3,n($s1) */
    {
    0x6704, 0xff1c}
    ,				/* move reg,$a0-$a3 */
    {
    0xe809, 0xf81f}
    ,				/* entry pseudo-op */
    {
    0x0100, 0xff00}
    ,				/* addiu $s1,$sp,n */
    {
    0, 0}			/* end of table marker */
  };

  /* Find an upper bound on the prologue.  */
  end_pc = skip_prologue_using_sal (pc);
  if (end_pc == 0)
    end_pc = pc + 100;		/* Magic.  */

  /* Skip the typical prologue instructions. These are the stack adjustment
     instruction and the instructions that save registers on the stack
     or in the gcc frame.  */
  for (; pc < end_pc; pc += MIPS16_INSTLEN)
    {
      unsigned short inst;
      int i;

      inst = mips_fetch_instruction (pc);

      /* Normally we ignore an extend instruction.  However, if it is
         not followed by a valid prologue instruction, we must adjust
         the pc back over the extend so that it won't be considered
         part of the prologue.  */
      if ((inst & 0xf800) == 0xf000)	/* extend */
	{
	  extend_bytes = MIPS16_INSTLEN;
	  continue;
	}
      prev_extend_bytes = extend_bytes;
      extend_bytes = 0;

      /* Check for other valid prologue instructions besides extend.  */
      for (i = 0; table[i].mask != 0; i++)
	if ((inst & table[i].mask) == table[i].inst)	/* found, get out */
	  break;
      if (table[i].mask != 0)	/* it was in table? */
	continue;		/* ignore it */
      else
	/* non-prologue */
	{
	  /* Return the current pc, adjusted backwards by 2 if
	     the previous instruction was an extend.  */
	  return pc - prev_extend_bytes;
	}
    }
  return pc;
}

/* To skip prologues, I use this predicate.  Returns either PC itself
   if the code at PC does not look like a function prologue; otherwise
   returns an address that (if we're lucky) follows the prologue.  If
   LENIENT, then we must skip everything which is involved in setting
   up the frame (it's OK to skip more, just so long as we don't skip
   anything which might clobber the registers which are being saved.
   We must skip more in the case where part of the prologue is in the
   delay slot of a non-prologue instruction).  */

static CORE_ADDR
mips_skip_prologue (CORE_ADDR pc)
{
  /* See if we can determine the end of the prologue via the symbol table.
     If so, then return either PC, or the PC after the prologue, whichever
     is greater.  */

  CORE_ADDR post_prologue_pc = after_prologue (pc, NULL);

  if (post_prologue_pc != 0)
    return max (pc, post_prologue_pc);

  /* Can't determine prologue from the symbol table, need to examine
     instructions.  */

  if (pc_is_mips16 (pc))
    return mips16_skip_prologue (pc);
  else
    return mips32_skip_prologue (pc);
}

/* Exported procedure: Is PC in the signal trampoline code */

static int
mips_pc_in_sigtramp (CORE_ADDR pc, char *ignore)
{
  if (sigtramp_address == 0)
    fixup_sigtramp ();
  return (pc >= sigtramp_address && pc < sigtramp_end);
}

/* Root of all "set mips "/"show mips " commands. This will eventually be
   used for all MIPS-specific commands.  */

static void
show_mips_command (char *args, int from_tty)
{
  help_list (showmipscmdlist, "show mips ", all_commands, gdb_stdout);
}

static void
set_mips_command (char *args, int from_tty)
{
  printf_unfiltered
    ("\"set mips\" must be followed by an appropriate subcommand.\n");
  help_list (setmipscmdlist, "set mips ", all_commands, gdb_stdout);
}

/* Commands to show/set the MIPS FPU type.  */

static void
show_mipsfpu_command (char *args, int from_tty)
{
  char *fpu;
  switch (MIPS_FPU_TYPE)
    {
    case MIPS_FPU_SINGLE:
      fpu = "single-precision";
      break;
    case MIPS_FPU_DOUBLE:
      fpu = "double-precision";
      break;
    case MIPS_FPU_NONE:
      fpu = "absent (none)";
      break;
    default:
      internal_error (__FILE__, __LINE__, "bad switch");
    }
  if (mips_fpu_type_auto)
    printf_unfiltered
      ("The MIPS floating-point coprocessor is set automatically (currently %s)\n",
       fpu);
  else
    printf_unfiltered
      ("The MIPS floating-point coprocessor is assumed to be %s\n", fpu);
}


static void
set_mipsfpu_command (char *args, int from_tty)
{
  printf_unfiltered
    ("\"set mipsfpu\" must be followed by \"double\", \"single\",\"none\" or \"auto\".\n");
  show_mipsfpu_command (args, from_tty);
}

static void
set_mipsfpu_single_command (char *args, int from_tty)
{
  struct gdbarch_info info;
  gdbarch_info_init (&info);
  mips_fpu_type = MIPS_FPU_SINGLE;
  mips_fpu_type_auto = 0;
  /* FIXME: cagney/2003-11-15: Should be setting a field in "info"
     instead of relying on globals.  Doing that would let generic code
     handle the search for this specific architecture.  */
  if (!gdbarch_update_p (info))
    internal_error (__FILE__, __LINE__, "set mipsfpu failed");
}

static void
set_mipsfpu_double_command (char *args, int from_tty)
{
  struct gdbarch_info info;
  gdbarch_info_init (&info);
  mips_fpu_type = MIPS_FPU_DOUBLE;
  mips_fpu_type_auto = 0;
  /* FIXME: cagney/2003-11-15: Should be setting a field in "info"
     instead of relying on globals.  Doing that would let generic code
     handle the search for this specific architecture.  */
  if (!gdbarch_update_p (info))
    internal_error (__FILE__, __LINE__, "set mipsfpu failed");
}

static void
set_mipsfpu_none_command (char *args, int from_tty)
{
  struct gdbarch_info info;
  gdbarch_info_init (&info);
  mips_fpu_type = MIPS_FPU_NONE;
  mips_fpu_type_auto = 0;
  /* FIXME: cagney/2003-11-15: Should be setting a field in "info"
     instead of relying on globals.  Doing that would let generic code
     handle the search for this specific architecture.  */
  if (!gdbarch_update_p (info))
    internal_error (__FILE__, __LINE__, "set mipsfpu failed");
}

static void
set_mipsfpu_auto_command (char *args, int from_tty)
{
  mips_fpu_type_auto = 1;
}

/* Attempt to identify the particular processor model by reading the
   processor id.  NOTE: cagney/2003-11-15: Firstly it isn't clear that
   the relevant processor still exists (it dates back to '94) and
   secondly this is not the way to do this.  The processor type should
   be set by forcing an architecture change.  */

void
deprecated_mips_set_processor_regs_hack (void)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  CORE_ADDR prid;

  prid = read_register (PRID_REGNUM);

  if ((prid & ~0xf) == 0x700)
    tdep->mips_processor_reg_names = mips_r3041_reg_names;
}

/* Just like reinit_frame_cache, but with the right arguments to be
   callable as an sfunc.  */

static void
reinit_frame_cache_sfunc (char *args, int from_tty,
			  struct cmd_list_element *c)
{
  reinit_frame_cache ();
}

static int
gdb_print_insn_mips (bfd_vma memaddr, struct disassemble_info *info)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  mips_extra_func_info_t proc_desc;

  /* Search for the function containing this address.  Set the low bit
     of the address when searching, in case we were given an even address
     that is the start of a 16-bit function.  If we didn't do this,
     the search would fail because the symbol table says the function
     starts at an odd address, i.e. 1 byte past the given address.  */
  memaddr = ADDR_BITS_REMOVE (memaddr);
  proc_desc = non_heuristic_proc_desc (make_mips16_addr (memaddr), NULL);

  /* Make an attempt to determine if this is a 16-bit function.  If
     the procedure descriptor exists and the address therein is odd,
     it's definitely a 16-bit function.  Otherwise, we have to just
     guess that if the address passed in is odd, it's 16-bits.  */
  /* FIXME: cagney/2003-06-26: Is this even necessary?  The
     disassembler needs to be able to locally determine the ISA, and
     not rely on GDB.  Otherwize the stand-alone 'objdump -d' will not
     work.  */
  if (proc_desc)
    {
      if (pc_is_mips16 (PROC_LOW_ADDR (proc_desc)))
	info->mach = bfd_mach_mips16;
    }
  else
    {
      if (pc_is_mips16 (memaddr))
	info->mach = bfd_mach_mips16;
    }

  /* Round down the instruction address to the appropriate boundary.  */
  memaddr &= (info->mach == bfd_mach_mips16 ? ~1 : ~3);

  /* Set the disassembler options.  */
  if (tdep->mips_abi == MIPS_ABI_N32 || tdep->mips_abi == MIPS_ABI_N64)
    {
      /* Set up the disassembler info, so that we get the right
         register names from libopcodes.  */
      if (tdep->mips_abi == MIPS_ABI_N32)
	info->disassembler_options = "gpr-names=n32";
      else
	info->disassembler_options = "gpr-names=64";
      info->flavour = bfd_target_elf_flavour;
    }
  else
    /* This string is not recognized explicitly by the disassembler,
       but it tells the disassembler to not try to guess the ABI from
       the bfd elf headers, such that, if the user overrides the ABI
       of a program linked as NewABI, the disassembly will follow the
       register naming conventions specified by the user.  */
    info->disassembler_options = "gpr-names=32";

  /* Call the appropriate disassembler based on the target endian-ness.  */
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    return print_insn_big_mips (memaddr, info);
  else
    return print_insn_little_mips (memaddr, info);
}

/* This function implements the BREAKPOINT_FROM_PC macro.  It uses the program
   counter value to determine whether a 16- or 32-bit breakpoint should be
   used.  It returns a pointer to a string of bytes that encode a breakpoint
   instruction, stores the length of the string to *lenptr, and adjusts pc
   (if necessary) to point to the actual memory location where the
   breakpoint should be inserted.  */

static const unsigned char *
mips_breakpoint_from_pc (CORE_ADDR *pcptr, int *lenptr)
{
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      if (pc_is_mips16 (*pcptr))
	{
	  static unsigned char mips16_big_breakpoint[] = { 0xe8, 0xa5 };
	  *pcptr = unmake_mips16_addr (*pcptr);
	  *lenptr = sizeof (mips16_big_breakpoint);
	  return mips16_big_breakpoint;
	}
      else
	{
	  /* The IDT board uses an unusual breakpoint value, and
	     sometimes gets confused when it sees the usual MIPS
	     breakpoint instruction.  */
	  static unsigned char big_breakpoint[] = { 0, 0x5, 0, 0xd };
	  static unsigned char pmon_big_breakpoint[] = { 0, 0, 0, 0xd };
	  static unsigned char idt_big_breakpoint[] = { 0, 0, 0x0a, 0xd };

	  *lenptr = sizeof (big_breakpoint);

	  if (strcmp (target_shortname, "mips") == 0)
	    return idt_big_breakpoint;
	  else if (strcmp (target_shortname, "ddb") == 0
		   || strcmp (target_shortname, "pmon") == 0
		   || strcmp (target_shortname, "lsi") == 0)
	    return pmon_big_breakpoint;
	  else
	    return big_breakpoint;
	}
    }
  else
    {
      if (pc_is_mips16 (*pcptr))
	{
	  static unsigned char mips16_little_breakpoint[] = { 0xa5, 0xe8 };
	  *pcptr = unmake_mips16_addr (*pcptr);
	  *lenptr = sizeof (mips16_little_breakpoint);
	  return mips16_little_breakpoint;
	}
      else
	{
	  static unsigned char little_breakpoint[] = { 0xd, 0, 0x5, 0 };
	  static unsigned char pmon_little_breakpoint[] = { 0xd, 0, 0, 0 };
	  static unsigned char idt_little_breakpoint[] = { 0xd, 0x0a, 0, 0 };

	  *lenptr = sizeof (little_breakpoint);

	  if (strcmp (target_shortname, "mips") == 0)
	    return idt_little_breakpoint;
	  else if (strcmp (target_shortname, "ddb") == 0
		   || strcmp (target_shortname, "pmon") == 0
		   || strcmp (target_shortname, "lsi") == 0)
	    return pmon_little_breakpoint;
	  else
	    return little_breakpoint;
	}
    }
}

/* If PC is in a mips16 call or return stub, return the address of the target
   PC, which is either the callee or the caller.  There are several
   cases which must be handled:

   * If the PC is in __mips16_ret_{d,s}f, this is a return stub and the
   target PC is in $31 ($ra).
   * If the PC is in __mips16_call_stub_{1..10}, this is a call stub
   and the target PC is in $2.
   * If the PC at the start of __mips16_call_stub_{s,d}f_{0..10}, i.e.
   before the jal instruction, this is effectively a call stub
   and the the target PC is in $2.  Otherwise this is effectively
   a return stub and the target PC is in $18.

   See the source code for the stubs in gcc/config/mips/mips16.S for
   gory details.

   This function implements the SKIP_TRAMPOLINE_CODE macro.
 */

static CORE_ADDR
mips_skip_stub (CORE_ADDR pc)
{
  char *name;
  CORE_ADDR start_addr;

  /* Find the starting address and name of the function containing the PC.  */
  if (find_pc_partial_function (pc, &name, &start_addr, NULL) == 0)
    return 0;

  /* If the PC is in __mips16_ret_{d,s}f, this is a return stub and the
     target PC is in $31 ($ra).  */
  if (strcmp (name, "__mips16_ret_sf") == 0
      || strcmp (name, "__mips16_ret_df") == 0)
    return read_signed_register (RA_REGNUM);

  if (strncmp (name, "__mips16_call_stub_", 19) == 0)
    {
      /* If the PC is in __mips16_call_stub_{1..10}, this is a call stub
         and the target PC is in $2.  */
      if (name[19] >= '0' && name[19] <= '9')
	return read_signed_register (2);

      /* If the PC at the start of __mips16_call_stub_{s,d}f_{0..10}, i.e.
         before the jal instruction, this is effectively a call stub
         and the the target PC is in $2.  Otherwise this is effectively
         a return stub and the target PC is in $18.  */
      else if (name[19] == 's' || name[19] == 'd')
	{
	  if (pc == start_addr)
	    {
	      /* Check if the target of the stub is a compiler-generated
	         stub.  Such a stub for a function bar might have a name
	         like __fn_stub_bar, and might look like this:
	         mfc1    $4,$f13
	         mfc1    $5,$f12
	         mfc1    $6,$f15
	         mfc1    $7,$f14
	         la      $1,bar   (becomes a lui/addiu pair)
	         jr      $1
	         So scan down to the lui/addi and extract the target
	         address from those two instructions.  */

	      CORE_ADDR target_pc = read_signed_register (2);
	      t_inst inst;
	      int i;

	      /* See if the name of the target function is  __fn_stub_*.  */
	      if (find_pc_partial_function (target_pc, &name, NULL, NULL) ==
		  0)
		return target_pc;
	      if (strncmp (name, "__fn_stub_", 10) != 0
		  && strcmp (name, "etext") != 0
		  && strcmp (name, "_etext") != 0)
		return target_pc;

	      /* Scan through this _fn_stub_ code for the lui/addiu pair.
	         The limit on the search is arbitrarily set to 20
	         instructions.  FIXME.  */
	      for (i = 0, pc = 0; i < 20; i++, target_pc += MIPS_INSTLEN)
		{
		  inst = mips_fetch_instruction (target_pc);
		  if ((inst & 0xffff0000) == 0x3c010000)	/* lui $at */
		    pc = (inst << 16) & 0xffff0000;	/* high word */
		  else if ((inst & 0xffff0000) == 0x24210000)	/* addiu $at */
		    return pc | (inst & 0xffff);	/* low word */
		}

	      /* Couldn't find the lui/addui pair, so return stub address.  */
	      return target_pc;
	    }
	  else
	    /* This is the 'return' part of a call stub.  The return
	       address is in $r18.  */
	    return read_signed_register (18);
	}
    }
  return 0;			/* not a stub */
}


/* Return non-zero if the PC is inside a call thunk (aka stub or trampoline).
   This implements the IN_SOLIB_CALL_TRAMPOLINE macro.  */

static int
mips_in_call_stub (CORE_ADDR pc, char *name)
{
  CORE_ADDR start_addr;

  /* Find the starting address of the function containing the PC.  If the
     caller didn't give us a name, look it up at the same time.  */
  if (find_pc_partial_function (pc, name ? NULL : &name, &start_addr, NULL) ==
      0)
    return 0;

  if (strncmp (name, "__mips16_call_stub_", 19) == 0)
    {
      /* If the PC is in __mips16_call_stub_{1..10}, this is a call stub.  */
      if (name[19] >= '0' && name[19] <= '9')
	return 1;
      /* If the PC at the start of __mips16_call_stub_{s,d}f_{0..10}, i.e.
         before the jal instruction, this is effectively a call stub.  */
      else if (name[19] == 's' || name[19] == 'd')
	return pc == start_addr;
    }

  return 0;			/* not a stub */
}


/* Return non-zero if the PC is inside a return thunk (aka stub or trampoline).
   This implements the IN_SOLIB_RETURN_TRAMPOLINE macro.  */

static int
mips_in_return_stub (CORE_ADDR pc, char *name)
{
  CORE_ADDR start_addr;

  /* Find the starting address of the function containing the PC.  */
  if (find_pc_partial_function (pc, NULL, &start_addr, NULL) == 0)
    return 0;

  /* If the PC is in __mips16_ret_{d,s}f, this is a return stub.  */
  if (strcmp (name, "__mips16_ret_sf") == 0
      || strcmp (name, "__mips16_ret_df") == 0)
    return 1;

  /* If the PC is in __mips16_call_stub_{s,d}f_{0..10} but not at the start,
     i.e. after the jal instruction, this is effectively a return stub.  */
  if (strncmp (name, "__mips16_call_stub_", 19) == 0
      && (name[19] == 's' || name[19] == 'd') && pc != start_addr)
    return 1;

  return 0;			/* not a stub */
}


/* Return non-zero if the PC is in a library helper function that should
   be ignored.  This implements the IGNORE_HELPER_CALL macro.  */

int
mips_ignore_helper (CORE_ADDR pc)
{
  char *name;

  /* Find the starting address and name of the function containing the PC.  */
  if (find_pc_partial_function (pc, &name, NULL, NULL) == 0)
    return 0;

  /* If the PC is in __mips16_ret_{d,s}f, this is a library helper function
     that we want to ignore.  */
  return (strcmp (name, "__mips16_ret_sf") == 0
	  || strcmp (name, "__mips16_ret_df") == 0);
}


/* Convert a dbx stab register number (from `r' declaration) to a GDB
   [1 * NUM_REGS .. 2 * NUM_REGS) REGNUM.  */

static int
mips_stab_reg_to_regnum (int num)
{
  int regnum;
  if (num >= 0 && num < 32)
    regnum = num;
  else if (num >= 38 && num < 70)
    regnum = num + mips_regnum (current_gdbarch)->fp0 - 38;
  else if (num == 70)
    regnum = mips_regnum (current_gdbarch)->hi;
  else if (num == 71)
    regnum = mips_regnum (current_gdbarch)->lo;
  else
    /* This will hopefully (eventually) provoke a warning.  Should
       we be calling complaint() here?  */
    return NUM_REGS + NUM_PSEUDO_REGS;
  return NUM_REGS + regnum;
}


/* Convert a dwarf, dwarf2, or ecoff register number to a GDB [1 *
   NUM_REGS .. 2 * NUM_REGS) REGNUM.  */

static int
mips_dwarf_dwarf2_ecoff_reg_to_regnum (int num)
{
  int regnum;
  if (num >= 0 && num < 32)
    regnum = num;
  else if (num >= 32 && num < 64)
    regnum = num + mips_regnum (current_gdbarch)->fp0 - 32;
  else if (num == 64)
    regnum = mips_regnum (current_gdbarch)->hi;
  else if (num == 65)
    regnum = mips_regnum (current_gdbarch)->lo;
  else
    /* This will hopefully (eventually) provoke a warning.  Should we
       be calling complaint() here?  */
    return NUM_REGS + NUM_PSEUDO_REGS;
  return NUM_REGS + regnum;
}

static int
mips_register_sim_regno (int regnum)
{
  /* Only makes sense to supply raw registers.  */
  gdb_assert (regnum >= 0 && regnum < NUM_REGS);
  /* FIXME: cagney/2002-05-13: Need to look at the pseudo register to
     decide if it is valid.  Should instead define a standard sim/gdb
     register numbering scheme.  */
  if (REGISTER_NAME (NUM_REGS + regnum) != NULL
      && REGISTER_NAME (NUM_REGS + regnum)[0] != '\0')
    return regnum;
  else
    return LEGACY_SIM_REGNO_IGNORE;
}


/* Convert an integer into an address.  By first converting the value
   into a pointer and then extracting it signed, the address is
   guarenteed to be correctly sign extended.  */

static CORE_ADDR
mips_integer_to_address (struct type *type, void *buf)
{
  char *tmp = alloca (TYPE_LENGTH (builtin_type_void_data_ptr));
  LONGEST val = unpack_long (type, buf);
  store_signed_integer (tmp, TYPE_LENGTH (builtin_type_void_data_ptr), val);
  return extract_signed_integer (tmp,
				 TYPE_LENGTH (builtin_type_void_data_ptr));
}

static void
mips_find_abi_section (bfd *abfd, asection *sect, void *obj)
{
  enum mips_abi *abip = (enum mips_abi *) obj;
  const char *name = bfd_get_section_name (abfd, sect);

  if (*abip != MIPS_ABI_UNKNOWN)
    return;

  if (strncmp (name, ".mdebug.", 8) != 0)
    return;

  if (strcmp (name, ".mdebug.abi32") == 0)
    *abip = MIPS_ABI_O32;
  else if (strcmp (name, ".mdebug.abiN32") == 0)
    *abip = MIPS_ABI_N32;
  else if (strcmp (name, ".mdebug.abi64") == 0)
    *abip = MIPS_ABI_N64;
  else if (strcmp (name, ".mdebug.abiO64") == 0)
    *abip = MIPS_ABI_O64;
  else if (strcmp (name, ".mdebug.eabi32") == 0)
    *abip = MIPS_ABI_EABI32;
  else if (strcmp (name, ".mdebug.eabi64") == 0)
    *abip = MIPS_ABI_EABI64;
  else
    warning ("unsupported ABI %s.", name + 8);
}

static enum mips_abi
global_mips_abi (void)
{
  int i;

  for (i = 0; mips_abi_strings[i] != NULL; i++)
    if (mips_abi_strings[i] == mips_abi_string)
      return (enum mips_abi) i;

  internal_error (__FILE__, __LINE__, "unknown ABI string");
}

static struct gdbarch *
mips_gdbarch_init (struct gdbarch_info info, struct gdbarch_list *arches)
{
  struct gdbarch *gdbarch;
  struct gdbarch_tdep *tdep;
  int elf_flags;
  enum mips_abi mips_abi, found_abi, wanted_abi;
  int num_regs;
  enum mips_fpu_type fpu_type;

  /* First of all, extract the elf_flags, if available.  */
  if (info.abfd && bfd_get_flavour (info.abfd) == bfd_target_elf_flavour)
    elf_flags = elf_elfheader (info.abfd)->e_flags;
  else if (arches != NULL)
    elf_flags = gdbarch_tdep (arches->gdbarch)->elf_flags;
  else
    elf_flags = 0;
  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog,
			"mips_gdbarch_init: elf_flags = 0x%08x\n", elf_flags);

  /* Check ELF_FLAGS to see if it specifies the ABI being used.  */
  switch ((elf_flags & EF_MIPS_ABI))
    {
    case E_MIPS_ABI_O32:
      found_abi = MIPS_ABI_O32;
      break;
    case E_MIPS_ABI_O64:
      found_abi = MIPS_ABI_O64;
      break;
    case E_MIPS_ABI_EABI32:
      found_abi = MIPS_ABI_EABI32;
      break;
    case E_MIPS_ABI_EABI64:
      found_abi = MIPS_ABI_EABI64;
      break;
    default:
      if ((elf_flags & EF_MIPS_ABI2))
	found_abi = MIPS_ABI_N32;
      else
	found_abi = MIPS_ABI_UNKNOWN;
      break;
    }

  /* GCC creates a pseudo-section whose name describes the ABI.  */
  if (found_abi == MIPS_ABI_UNKNOWN && info.abfd != NULL)
    bfd_map_over_sections (info.abfd, mips_find_abi_section, &found_abi);

  /* If we have no usefu BFD information, use the ABI from the last
     MIPS architecture (if there is one).  */
  if (found_abi == MIPS_ABI_UNKNOWN && info.abfd == NULL && arches != NULL)
    found_abi = gdbarch_tdep (arches->gdbarch)->found_abi;

  /* Try the architecture for any hint of the correct ABI.  */
  if (found_abi == MIPS_ABI_UNKNOWN
      && info.bfd_arch_info != NULL
      && info.bfd_arch_info->arch == bfd_arch_mips)
    {
      switch (info.bfd_arch_info->mach)
	{
	case bfd_mach_mips3900:
	  found_abi = MIPS_ABI_EABI32;
	  break;
	case bfd_mach_mips4100:
	case bfd_mach_mips5000:
	  found_abi = MIPS_ABI_EABI64;
	  break;
	case bfd_mach_mips8000:
	case bfd_mach_mips10000:
	  /* On Irix, ELF64 executables use the N64 ABI.  The
	     pseudo-sections which describe the ABI aren't present
	     on IRIX.  (Even for executables created by gcc.)  */
	  if (bfd_get_flavour (info.abfd) == bfd_target_elf_flavour
	      && elf_elfheader (info.abfd)->e_ident[EI_CLASS] == ELFCLASS64)
	    found_abi = MIPS_ABI_N64;
	  else
	    found_abi = MIPS_ABI_N32;
	  break;
	}
    }

  /* Default 64-bit objects to N64 instead of O32.  */
  if (found_abi == MIPS_ABI_UNKNOWN
      && info.abfd != NULL
      && bfd_get_flavour (info.abfd) == bfd_target_elf_flavour
      && elf_elfheader (info.abfd)->e_ident[EI_CLASS] == ELFCLASS64)
    found_abi = MIPS_ABI_N64;

  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog, "mips_gdbarch_init: found_abi = %d\n",
			found_abi);

  /* What has the user specified from the command line?  */
  wanted_abi = global_mips_abi ();
  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog, "mips_gdbarch_init: wanted_abi = %d\n",
			wanted_abi);

  /* Now that we have found what the ABI for this binary would be,
     check whether the user is overriding it.  */
  if (wanted_abi != MIPS_ABI_UNKNOWN)
    mips_abi = wanted_abi;
  else if (found_abi != MIPS_ABI_UNKNOWN)
    mips_abi = found_abi;
  else
    mips_abi = MIPS_ABI_O32;
  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog, "mips_gdbarch_init: mips_abi = %d\n",
			mips_abi);

  /* Also used when doing an architecture lookup.  */
  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog,
			"mips_gdbarch_init: mips64_transfers_32bit_regs_p = %d\n",
			mips64_transfers_32bit_regs_p);

  /* Determine the MIPS FPU type.  */
  if (!mips_fpu_type_auto)
    fpu_type = mips_fpu_type;
  else if (info.bfd_arch_info != NULL
	   && info.bfd_arch_info->arch == bfd_arch_mips)
    switch (info.bfd_arch_info->mach)
      {
      case bfd_mach_mips3900:
      case bfd_mach_mips4100:
      case bfd_mach_mips4111:
	fpu_type = MIPS_FPU_NONE;
	break;
      case bfd_mach_mips4650:
	fpu_type = MIPS_FPU_SINGLE;
	break;
      default:
	fpu_type = MIPS_FPU_DOUBLE;
	break;
      }
  else if (arches != NULL)
    fpu_type = gdbarch_tdep (arches->gdbarch)->mips_fpu_type;
  else
    fpu_type = MIPS_FPU_DOUBLE;
  if (gdbarch_debug)
    fprintf_unfiltered (gdb_stdlog,
			"mips_gdbarch_init: fpu_type = %d\n", fpu_type);

  /* try to find a pre-existing architecture */
  for (arches = gdbarch_list_lookup_by_info (arches, &info);
       arches != NULL;
       arches = gdbarch_list_lookup_by_info (arches->next, &info))
    {
      /* MIPS needs to be pedantic about which ABI the object is
         using.  */
      if (gdbarch_tdep (arches->gdbarch)->elf_flags != elf_flags)
	continue;
      if (gdbarch_tdep (arches->gdbarch)->mips_abi != mips_abi)
	continue;
      /* Need to be pedantic about which register virtual size is
         used.  */
      if (gdbarch_tdep (arches->gdbarch)->mips64_transfers_32bit_regs_p
	  != mips64_transfers_32bit_regs_p)
	continue;
      /* Be pedantic about which FPU is selected.  */
      if (gdbarch_tdep (arches->gdbarch)->mips_fpu_type != fpu_type)
	continue;
      return arches->gdbarch;
    }

  /* Need a new architecture.  Fill in a target specific vector.  */
  tdep = (struct gdbarch_tdep *) xmalloc (sizeof (struct gdbarch_tdep));
  gdbarch = gdbarch_alloc (&info, tdep);
  tdep->elf_flags = elf_flags;
  tdep->mips64_transfers_32bit_regs_p = mips64_transfers_32bit_regs_p;
  tdep->found_abi = found_abi;
  tdep->mips_abi = mips_abi;
  tdep->mips_fpu_type = fpu_type;

  /* Initially set everything according to the default ABI/ISA.  */
  set_gdbarch_short_bit (gdbarch, 16);
  set_gdbarch_int_bit (gdbarch, 32);
  set_gdbarch_float_bit (gdbarch, 32);
  set_gdbarch_double_bit (gdbarch, 64);
  set_gdbarch_long_double_bit (gdbarch, 64);
  set_gdbarch_register_reggroup_p (gdbarch, mips_register_reggroup_p);
  set_gdbarch_pseudo_register_read (gdbarch, mips_pseudo_register_read);
  set_gdbarch_pseudo_register_write (gdbarch, mips_pseudo_register_write);

  set_gdbarch_elf_make_msymbol_special (gdbarch,
					mips_elf_make_msymbol_special);

  /* Fill in the OS dependant register numbers and names.  */
  {
    const char **reg_names;
    struct mips_regnum *regnum = GDBARCH_OBSTACK_ZALLOC (gdbarch,
							 struct mips_regnum);
    if (info.osabi == GDB_OSABI_IRIX)
      {
	regnum->fp0 = 32;
	regnum->pc = 64;
	regnum->cause = 65;
	regnum->badvaddr = 66;
	regnum->hi = 67;
	regnum->lo = 68;
	regnum->fp_control_status = 69;
	regnum->fp_implementation_revision = 70;
	num_regs = 71;
	reg_names = mips_irix_reg_names;
      }
    else
      {
	regnum->lo = MIPS_EMBED_LO_REGNUM;
	regnum->hi = MIPS_EMBED_HI_REGNUM;
	regnum->badvaddr = MIPS_EMBED_BADVADDR_REGNUM;
	regnum->cause = MIPS_EMBED_CAUSE_REGNUM;
	regnum->pc = MIPS_EMBED_PC_REGNUM;
	regnum->fp0 = MIPS_EMBED_FP0_REGNUM;
	regnum->fp_control_status = 70;
	regnum->fp_implementation_revision = 71;
	num_regs = 90;
	if (info.bfd_arch_info != NULL
	    && info.bfd_arch_info->mach == bfd_mach_mips3900)
	  reg_names = mips_tx39_reg_names;
	else
	  reg_names = mips_generic_reg_names;
      }
    /* FIXME: cagney/2003-11-15: For MIPS, hasn't PC_REGNUM been
       replaced by read_pc?  */
    set_gdbarch_pc_regnum (gdbarch, regnum->pc);
    set_gdbarch_fp0_regnum (gdbarch, regnum->fp0);
    set_gdbarch_num_regs (gdbarch, num_regs);
    set_gdbarch_num_pseudo_regs (gdbarch, num_regs);
    set_gdbarch_register_name (gdbarch, mips_register_name);
    tdep->mips_processor_reg_names = reg_names;
    tdep->regnum = regnum;
  }

  switch (mips_abi)
    {
    case MIPS_ABI_O32:
      set_gdbarch_push_dummy_call (gdbarch, mips_o32_push_dummy_call);
      set_gdbarch_return_value (gdbarch, mips_o32_return_value);
      tdep->mips_default_saved_regsize = 4;
      tdep->mips_default_stack_argsize = 4;
      tdep->mips_fp_register_double = 0;
      tdep->mips_last_arg_regnum = A0_REGNUM + 4 - 1;
      tdep->mips_last_fp_arg_regnum = tdep->regnum->fp0 + 12 + 4 - 1;
      tdep->default_mask_address_p = 0;
      set_gdbarch_long_bit (gdbarch, 32);
      set_gdbarch_ptr_bit (gdbarch, 32);
      set_gdbarch_long_long_bit (gdbarch, 64);
      break;
    case MIPS_ABI_O64:
      set_gdbarch_push_dummy_call (gdbarch, mips_o64_push_dummy_call);
      set_gdbarch_deprecated_store_return_value (gdbarch,
						 mips_o64_store_return_value);
      set_gdbarch_deprecated_extract_return_value (gdbarch,
						   mips_o64_extract_return_value);
      tdep->mips_default_saved_regsize = 8;
      tdep->mips_default_stack_argsize = 8;
      tdep->mips_fp_register_double = 1;
      tdep->mips_last_arg_regnum = A0_REGNUM + 4 - 1;
      tdep->mips_last_fp_arg_regnum = tdep->regnum->fp0 + 12 + 4 - 1;
      tdep->default_mask_address_p = 0;
      set_gdbarch_long_bit (gdbarch, 32);
      set_gdbarch_ptr_bit (gdbarch, 32);
      set_gdbarch_long_long_bit (gdbarch, 64);
      set_gdbarch_use_struct_convention (gdbarch,
					 always_use_struct_convention);
      break;
    case MIPS_ABI_EABI32:
      set_gdbarch_push_dummy_call (gdbarch, mips_eabi_push_dummy_call);
      set_gdbarch_deprecated_store_return_value (gdbarch,
						 mips_eabi_store_return_value);
      set_gdbarch_deprecated_extract_return_value (gdbarch,
						   mips_eabi_extract_return_value);
      tdep->mips_default_saved_regsize = 4;
      tdep->mips_default_stack_argsize = 4;
      tdep->mips_fp_register_double = 0;
      tdep->mips_last_arg_regnum = A0_REGNUM + 8 - 1;
      tdep->mips_last_fp_arg_regnum = tdep->regnum->fp0 + 12 + 8 - 1;
      tdep->default_mask_address_p = 0;
      set_gdbarch_long_bit (gdbarch, 32);
      set_gdbarch_ptr_bit (gdbarch, 32);
      set_gdbarch_long_long_bit (gdbarch, 64);
      set_gdbarch_deprecated_reg_struct_has_addr
	(gdbarch, mips_eabi_reg_struct_has_addr);
      set_gdbarch_use_struct_convention (gdbarch,
					 mips_eabi_use_struct_convention);
      break;
    case MIPS_ABI_EABI64:
      set_gdbarch_push_dummy_call (gdbarch, mips_eabi_push_dummy_call);
      set_gdbarch_deprecated_store_return_value (gdbarch,
						 mips_eabi_store_return_value);
      set_gdbarch_deprecated_extract_return_value (gdbarch,
						   mips_eabi_extract_return_value);
      tdep->mips_default_saved_regsize = 8;
      tdep->mips_default_stack_argsize = 8;
      tdep->mips_fp_register_double = 1;
      tdep->mips_last_arg_regnum = A0_REGNUM + 8 - 1;
      tdep->mips_last_fp_arg_regnum = tdep->regnum->fp0 + 12 + 8 - 1;
      tdep->default_mask_address_p = 0;
      set_gdbarch_long_bit (gdbarch, 64);
      set_gdbarch_ptr_bit (gdbarch, 64);
      set_gdbarch_long_long_bit (gdbarch, 64);
      set_gdbarch_deprecated_reg_struct_has_addr
	(gdbarch, mips_eabi_reg_struct_has_addr);
      set_gdbarch_use_struct_convention (gdbarch,
					 mips_eabi_use_struct_convention);
      break;
    case MIPS_ABI_N32:
      set_gdbarch_push_dummy_call (gdbarch, mips_n32n64_push_dummy_call);
      set_gdbarch_return_value (gdbarch, mips_n32n64_return_value);
      tdep->mips_default_saved_regsize = 8;
      tdep->mips_default_stack_argsize = 8;
      tdep->mips_fp_register_double = 1;
      tdep->mips_last_arg_regnum = A0_REGNUM + 8 - 1;
      tdep->mips_last_fp_arg_regnum = tdep->regnum->fp0 + 12 + 8 - 1;
      tdep->default_mask_address_p = 0;
      set_gdbarch_long_bit (gdbarch, 32);
      set_gdbarch_ptr_bit (gdbarch, 32);
      set_gdbarch_long_long_bit (gdbarch, 64);
      break;
    case MIPS_ABI_N64:
      set_gdbarch_push_dummy_call (gdbarch, mips_n32n64_push_dummy_call);
      set_gdbarch_return_value (gdbarch, mips_n32n64_return_value);
      tdep->mips_default_saved_regsize = 8;
      tdep->mips_default_stack_argsize = 8;
      tdep->mips_fp_register_double = 1;
      tdep->mips_last_arg_regnum = A0_REGNUM + 8 - 1;
      tdep->mips_last_fp_arg_regnum = tdep->regnum->fp0 + 12 + 8 - 1;
      tdep->default_mask_address_p = 0;
      set_gdbarch_long_bit (gdbarch, 64);
      set_gdbarch_ptr_bit (gdbarch, 64);
      set_gdbarch_long_long_bit (gdbarch, 64);
      break;
    default:
      internal_error (__FILE__, __LINE__, "unknown ABI in switch");
    }

  /* FIXME: jlarmour/2000-04-07: There *is* a flag EF_MIPS_32BIT_MODE
     that could indicate -gp32 BUT gas/config/tc-mips.c contains the
     comment:

     ``We deliberately don't allow "-gp32" to set the MIPS_32BITMODE
     flag in object files because to do so would make it impossible to
     link with libraries compiled without "-gp32".  This is
     unnecessarily restrictive.

     We could solve this problem by adding "-gp32" multilibs to gcc,
     but to set this flag before gcc is built with such multilibs will
     break too many systems.''

     But even more unhelpfully, the default linker output target for
     mips64-elf is elf32-bigmips, and has EF_MIPS_32BIT_MODE set, even
     for 64-bit programs - you need to change the ABI to change this,
     and not all gcc targets support that currently.  Therefore using
     this flag to detect 32-bit mode would do the wrong thing given
     the current gcc - it would make GDB treat these 64-bit programs
     as 32-bit programs by default.  */

  set_gdbarch_read_pc (gdbarch, mips_read_pc);
  set_gdbarch_write_pc (gdbarch, mips_write_pc);
  set_gdbarch_read_sp (gdbarch, mips_read_sp);

  /* Add/remove bits from an address.  The MIPS needs be careful to
     ensure that all 32 bit addresses are sign extended to 64 bits.  */
  set_gdbarch_addr_bits_remove (gdbarch, mips_addr_bits_remove);

  /* Unwind the frame.  */
  set_gdbarch_unwind_pc (gdbarch, mips_unwind_pc);
  frame_unwind_append_sniffer (gdbarch, mips_mdebug_frame_sniffer);
  set_gdbarch_unwind_dummy_id (gdbarch, mips_unwind_dummy_id);
  frame_base_append_sniffer (gdbarch, mips_mdebug_frame_base_sniffer);

  /* Map debug register numbers onto internal register numbers.  */
  set_gdbarch_stab_reg_to_regnum (gdbarch, mips_stab_reg_to_regnum);
  set_gdbarch_ecoff_reg_to_regnum (gdbarch,
				   mips_dwarf_dwarf2_ecoff_reg_to_regnum);
  set_gdbarch_dwarf_reg_to_regnum (gdbarch,
				   mips_dwarf_dwarf2_ecoff_reg_to_regnum);
  set_gdbarch_dwarf2_reg_to_regnum (gdbarch,
				    mips_dwarf_dwarf2_ecoff_reg_to_regnum);
  set_gdbarch_register_sim_regno (gdbarch, mips_register_sim_regno);

  /* MIPS version of CALL_DUMMY */

  /* NOTE: cagney/2003-08-05: Eventually call dummy location will be
     replaced by a command, and all targets will default to on stack
     (regardless of the stack's execute status).  */
  set_gdbarch_call_dummy_location (gdbarch, AT_SYMBOL);
  set_gdbarch_frame_align (gdbarch, mips_frame_align);

  set_gdbarch_convert_register_p (gdbarch, mips_convert_register_p);
  set_gdbarch_register_to_value (gdbarch, mips_register_to_value);
  set_gdbarch_value_to_register (gdbarch, mips_value_to_register);

  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);
  set_gdbarch_breakpoint_from_pc (gdbarch, mips_breakpoint_from_pc);

  set_gdbarch_skip_prologue (gdbarch, mips_skip_prologue);

  set_gdbarch_pointer_to_address (gdbarch, signed_pointer_to_address);
  set_gdbarch_address_to_pointer (gdbarch, address_to_signed_pointer);
  set_gdbarch_integer_to_address (gdbarch, mips_integer_to_address);

  set_gdbarch_register_type (gdbarch, mips_register_type);

  set_gdbarch_print_registers_info (gdbarch, mips_print_registers_info);
  set_gdbarch_pc_in_sigtramp (gdbarch, mips_pc_in_sigtramp);

  set_gdbarch_print_insn (gdbarch, gdb_print_insn_mips);

  /* FIXME: cagney/2003-08-29: The macros HAVE_STEPPABLE_WATCHPOINT,
     HAVE_NONSTEPPABLE_WATCHPOINT, and HAVE_CONTINUABLE_WATCHPOINT
     need to all be folded into the target vector.  Since they are
     being used as guards for STOPPED_BY_WATCHPOINT, why not have
     STOPPED_BY_WATCHPOINT return the type of watchpoint that the code
     is sitting on?  */
  set_gdbarch_have_nonsteppable_watchpoint (gdbarch, 1);

  set_gdbarch_skip_trampoline_code (gdbarch, mips_skip_stub);

  /* NOTE drow/2004-02-11: We overload the core solib trampoline code
     to support MIPS16.  This is a bad thing.  Make sure not to do it
     if we have an OS ABI that actually supports shared libraries, since
     shared library support is more important.  If we have an OS someday
     that supports both shared libraries and MIPS16, we'll have to find
     a better place for these.  */
  if (info.osabi == GDB_OSABI_UNKNOWN)
    {
      set_gdbarch_in_solib_call_trampoline (gdbarch, mips_in_call_stub);
      set_gdbarch_in_solib_return_trampoline (gdbarch, mips_in_return_stub);
    }

  /* Hook in OS ABI-specific overrides, if they have been registered.  */
  gdbarch_init_osabi (info, gdbarch);

  return gdbarch;
}

static void
mips_abi_update (char *ignore_args, int from_tty, struct cmd_list_element *c)
{
  struct gdbarch_info info;

  /* Force the architecture to update, and (if it's a MIPS architecture)
     mips_gdbarch_init will take care of the rest.  */
  gdbarch_info_init (&info);
  gdbarch_update_p (info);
}

/* Print out which MIPS ABI is in use.  */

static void
show_mips_abi (char *ignore_args, int from_tty)
{
  if (gdbarch_bfd_arch_info (current_gdbarch)->arch != bfd_arch_mips)
    printf_filtered
      ("The MIPS ABI is unknown because the current architecture is not MIPS.\n");
  else
    {
      enum mips_abi global_abi = global_mips_abi ();
      enum mips_abi actual_abi = mips_abi (current_gdbarch);
      const char *actual_abi_str = mips_abi_strings[actual_abi];

      if (global_abi == MIPS_ABI_UNKNOWN)
	printf_filtered
	  ("The MIPS ABI is set automatically (currently \"%s\").\n",
	   actual_abi_str);
      else if (global_abi == actual_abi)
	printf_filtered
	  ("The MIPS ABI is assumed to be \"%s\" (due to user setting).\n",
	   actual_abi_str);
      else
	{
	  /* Probably shouldn't happen...  */
	  printf_filtered
	    ("The (auto detected) MIPS ABI \"%s\" is in use even though the user setting was \"%s\".\n",
	     actual_abi_str, mips_abi_strings[global_abi]);
	}
    }
}

static void
mips_dump_tdep (struct gdbarch *current_gdbarch, struct ui_file *file)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  if (tdep != NULL)
    {
      int ef_mips_arch;
      int ef_mips_32bitmode;
      /* determine the ISA */
      switch (tdep->elf_flags & EF_MIPS_ARCH)
	{
	case E_MIPS_ARCH_1:
	  ef_mips_arch = 1;
	  break;
	case E_MIPS_ARCH_2:
	  ef_mips_arch = 2;
	  break;
	case E_MIPS_ARCH_3:
	  ef_mips_arch = 3;
	  break;
	case E_MIPS_ARCH_4:
	  ef_mips_arch = 4;
	  break;
	default:
	  ef_mips_arch = 0;
	  break;
	}
      /* determine the size of a pointer */
      ef_mips_32bitmode = (tdep->elf_flags & EF_MIPS_32BITMODE);
      fprintf_unfiltered (file,
			  "mips_dump_tdep: tdep->elf_flags = 0x%x\n",
			  tdep->elf_flags);
      fprintf_unfiltered (file,
			  "mips_dump_tdep: ef_mips_32bitmode = %d\n",
			  ef_mips_32bitmode);
      fprintf_unfiltered (file,
			  "mips_dump_tdep: ef_mips_arch = %d\n",
			  ef_mips_arch);
      fprintf_unfiltered (file,
			  "mips_dump_tdep: tdep->mips_abi = %d (%s)\n",
			  tdep->mips_abi, mips_abi_strings[tdep->mips_abi]);
      fprintf_unfiltered (file,
			  "mips_dump_tdep: mips_mask_address_p() %d (default %d)\n",
			  mips_mask_address_p (tdep),
			  tdep->default_mask_address_p);
    }
  fprintf_unfiltered (file,
		      "mips_dump_tdep: FP_REGISTER_DOUBLE = %d\n",
		      FP_REGISTER_DOUBLE);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MIPS_DEFAULT_FPU_TYPE = %d (%s)\n",
		      MIPS_DEFAULT_FPU_TYPE,
		      (MIPS_DEFAULT_FPU_TYPE == MIPS_FPU_NONE ? "none"
		       : MIPS_DEFAULT_FPU_TYPE == MIPS_FPU_SINGLE ? "single"
		       : MIPS_DEFAULT_FPU_TYPE == MIPS_FPU_DOUBLE ? "double"
		       : "???"));
  fprintf_unfiltered (file, "mips_dump_tdep: MIPS_EABI = %d\n", MIPS_EABI);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MIPS_FPU_TYPE = %d (%s)\n",
		      MIPS_FPU_TYPE,
		      (MIPS_FPU_TYPE == MIPS_FPU_NONE ? "none"
		       : MIPS_FPU_TYPE == MIPS_FPU_SINGLE ? "single"
		       : MIPS_FPU_TYPE == MIPS_FPU_DOUBLE ? "double"
		       : "???"));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: FP_REGISTER_DOUBLE = %d\n",
		      FP_REGISTER_DOUBLE);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: mips_stack_argsize() = %d\n",
		      mips_stack_argsize (tdep));
  fprintf_unfiltered (file, "mips_dump_tdep: A0_REGNUM = %d\n", A0_REGNUM);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: ADDR_BITS_REMOVE # %s\n",
		      XSTRING (ADDR_BITS_REMOVE (ADDR)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: ATTACH_DETACH # %s\n",
		      XSTRING (ATTACH_DETACH));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: DWARF_REG_TO_REGNUM # %s\n",
		      XSTRING (DWARF_REG_TO_REGNUM (REGNUM)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: ECOFF_REG_TO_REGNUM # %s\n",
		      XSTRING (ECOFF_REG_TO_REGNUM (REGNUM)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: FIRST_EMBED_REGNUM = %d\n",
		      FIRST_EMBED_REGNUM);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: IGNORE_HELPER_CALL # %s\n",
		      XSTRING (IGNORE_HELPER_CALL (PC)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: IN_SOLIB_CALL_TRAMPOLINE # %s\n",
		      XSTRING (IN_SOLIB_CALL_TRAMPOLINE (PC, NAME)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: IN_SOLIB_RETURN_TRAMPOLINE # %s\n",
		      XSTRING (IN_SOLIB_RETURN_TRAMPOLINE (PC, NAME)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: LAST_EMBED_REGNUM = %d\n",
		      LAST_EMBED_REGNUM);
#ifdef MACHINE_CPROC_FP_OFFSET
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MACHINE_CPROC_FP_OFFSET = %d\n",
		      MACHINE_CPROC_FP_OFFSET);
#endif
#ifdef MACHINE_CPROC_PC_OFFSET
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MACHINE_CPROC_PC_OFFSET = %d\n",
		      MACHINE_CPROC_PC_OFFSET);
#endif
#ifdef MACHINE_CPROC_SP_OFFSET
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MACHINE_CPROC_SP_OFFSET = %d\n",
		      MACHINE_CPROC_SP_OFFSET);
#endif
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MIPS16_INSTLEN = %d\n",
		      MIPS16_INSTLEN);
  fprintf_unfiltered (file, "mips_dump_tdep: MIPS_DEFAULT_ABI = FIXME!\n");
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MIPS_EFI_SYMBOL_NAME = multi-arch!!\n");
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MIPS_INSTLEN = %d\n", MIPS_INSTLEN);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MIPS_LAST_ARG_REGNUM = %d (%d regs)\n",
		      MIPS_LAST_ARG_REGNUM,
		      MIPS_LAST_ARG_REGNUM - A0_REGNUM + 1);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: MIPS_NUMREGS = %d\n", MIPS_NUMREGS);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: mips_saved_regsize() = %d\n",
		      mips_saved_regsize (tdep));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: PRID_REGNUM = %d\n", PRID_REGNUM);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: PROC_DESC_IS_DUMMY = function?\n");
  fprintf_unfiltered (file,
		      "mips_dump_tdep: PROC_FRAME_ADJUST = function?\n");
  fprintf_unfiltered (file,
		      "mips_dump_tdep: PROC_FRAME_OFFSET = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_FRAME_REG = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_FREG_MASK = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_FREG_OFFSET = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_HIGH_ADDR = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_LOW_ADDR = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_PC_REG = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_REG_MASK = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_REG_OFFSET = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PROC_SYMBOL = function?\n");
  fprintf_unfiltered (file, "mips_dump_tdep: PS_REGNUM = %d\n", PS_REGNUM);
  fprintf_unfiltered (file, "mips_dump_tdep: RA_REGNUM = %d\n", RA_REGNUM);
#ifdef SAVED_BYTES
  fprintf_unfiltered (file,
		      "mips_dump_tdep: SAVED_BYTES = %d\n", SAVED_BYTES);
#endif
#ifdef SAVED_FP
  fprintf_unfiltered (file, "mips_dump_tdep: SAVED_FP = %d\n", SAVED_FP);
#endif
#ifdef SAVED_PC
  fprintf_unfiltered (file, "mips_dump_tdep: SAVED_PC = %d\n", SAVED_PC);
#endif
  fprintf_unfiltered (file,
		      "mips_dump_tdep: SETUP_ARBITRARY_FRAME # %s\n",
		      XSTRING (SETUP_ARBITRARY_FRAME (NUMARGS, ARGS)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: SET_PROC_DESC_IS_DUMMY = function?\n");
  fprintf_unfiltered (file,
		      "mips_dump_tdep: SKIP_TRAMPOLINE_CODE # %s\n",
		      XSTRING (SKIP_TRAMPOLINE_CODE (PC)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: SOFTWARE_SINGLE_STEP # %s\n",
		      XSTRING (SOFTWARE_SINGLE_STEP (SIG, BP_P)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: SOFTWARE_SINGLE_STEP_P () = %d\n",
		      SOFTWARE_SINGLE_STEP_P ());
  fprintf_unfiltered (file,
		      "mips_dump_tdep: STAB_REG_TO_REGNUM # %s\n",
		      XSTRING (STAB_REG_TO_REGNUM (REGNUM)));
#ifdef STACK_END_ADDR
  fprintf_unfiltered (file,
		      "mips_dump_tdep: STACK_END_ADDR = %d\n",
		      STACK_END_ADDR);
#endif
  fprintf_unfiltered (file,
		      "mips_dump_tdep: STEP_SKIPS_DELAY # %s\n",
		      XSTRING (STEP_SKIPS_DELAY (PC)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: STEP_SKIPS_DELAY_P = %d\n",
		      STEP_SKIPS_DELAY_P);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: STOPPED_BY_WATCHPOINT # %s\n",
		      XSTRING (STOPPED_BY_WATCHPOINT (WS)));
  fprintf_unfiltered (file, "mips_dump_tdep: T9_REGNUM = %d\n", T9_REGNUM);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: TABULAR_REGISTER_OUTPUT = used?\n");
  fprintf_unfiltered (file,
		      "mips_dump_tdep: TARGET_CAN_USE_HARDWARE_WATCHPOINT # %s\n",
		      XSTRING (TARGET_CAN_USE_HARDWARE_WATCHPOINT
			       (TYPE, CNT, OTHERTYPE)));
  fprintf_unfiltered (file,
		      "mips_dump_tdep: TARGET_HAS_HARDWARE_WATCHPOINTS # %s\n",
		      XSTRING (TARGET_HAS_HARDWARE_WATCHPOINTS));
#ifdef TRACE_CLEAR
  fprintf_unfiltered (file,
		      "mips_dump_tdep: TRACE_CLEAR # %s\n",
		      XSTRING (TRACE_CLEAR (THREAD, STATE)));
#endif
#ifdef TRACE_FLAVOR
  fprintf_unfiltered (file,
		      "mips_dump_tdep: TRACE_FLAVOR = %d\n", TRACE_FLAVOR);
#endif
#ifdef TRACE_FLAVOR_SIZE
  fprintf_unfiltered (file,
		      "mips_dump_tdep: TRACE_FLAVOR_SIZE = %d\n",
		      TRACE_FLAVOR_SIZE);
#endif
#ifdef TRACE_SET
  fprintf_unfiltered (file,
		      "mips_dump_tdep: TRACE_SET # %s\n",
		      XSTRING (TRACE_SET (X, STATE)));
#endif
#ifdef UNUSED_REGNUM
  fprintf_unfiltered (file,
		      "mips_dump_tdep: UNUSED_REGNUM = %d\n", UNUSED_REGNUM);
#endif
  fprintf_unfiltered (file, "mips_dump_tdep: V0_REGNUM = %d\n", V0_REGNUM);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: VM_MIN_ADDRESS = %ld\n",
		      (long) VM_MIN_ADDRESS);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: ZERO_REGNUM = %d\n", ZERO_REGNUM);
  fprintf_unfiltered (file,
		      "mips_dump_tdep: _PROC_MAGIC_ = %d\n", _PROC_MAGIC_);
}

extern initialize_file_ftype _initialize_mips_tdep;	/* -Wmissing-prototypes */

void
_initialize_mips_tdep (void)
{
  static struct cmd_list_element *mipsfpulist = NULL;
  struct cmd_list_element *c;

  mips_abi_string = mips_abi_strings[MIPS_ABI_UNKNOWN];
  if (MIPS_ABI_LAST + 1
      != sizeof (mips_abi_strings) / sizeof (mips_abi_strings[0]))
    internal_error (__FILE__, __LINE__, "mips_abi_strings out of sync");

  gdbarch_register (bfd_arch_mips, mips_gdbarch_init, mips_dump_tdep);

  mips_pdr_data = register_objfile_data ();

  /* Add root prefix command for all "set mips"/"show mips" commands */
  add_prefix_cmd ("mips", no_class, set_mips_command,
		  "Various MIPS specific commands.",
		  &setmipscmdlist, "set mips ", 0, &setlist);

  add_prefix_cmd ("mips", no_class, show_mips_command,
		  "Various MIPS specific commands.",
		  &showmipscmdlist, "show mips ", 0, &showlist);

  /* Allow the user to override the saved register size. */
  add_show_from_set (add_set_enum_cmd ("saved-gpreg-size",
				       class_obscure,
				       size_enums,
				       &mips_saved_regsize_string, "\
Set size of general purpose registers saved on the stack.\n\
This option can be set to one of:\n\
  32    - Force GDB to treat saved GP registers as 32-bit\n\
  64    - Force GDB to treat saved GP registers as 64-bit\n\
  auto  - Allow GDB to use the target's default setting or autodetect the\n\
          saved GP register size from information contained in the executable.\n\
          (default: auto)", &setmipscmdlist), &showmipscmdlist);

  /* Allow the user to override the argument stack size. */
  add_show_from_set (add_set_enum_cmd ("stack-arg-size",
				       class_obscure,
				       size_enums,
				       &mips_stack_argsize_string, "\
Set the amount of stack space reserved for each argument.\n\
This option can be set to one of:\n\
  32    - Force GDB to allocate 32-bit chunks per argument\n\
  64    - Force GDB to allocate 64-bit chunks per argument\n\
  auto  - Allow GDB to determine the correct setting from the current\n\
          target and executable (default)", &setmipscmdlist), &showmipscmdlist);

  /* Allow the user to override the ABI. */
  c = add_set_enum_cmd
    ("abi", class_obscure, mips_abi_strings, &mips_abi_string,
     "Set the ABI used by this program.\n"
     "This option can be set to one of:\n"
     "  auto  - the default ABI associated with the current binary\n"
     "  o32\n"
     "  o64\n" "  n32\n" "  n64\n" "  eabi32\n" "  eabi64", &setmipscmdlist);
  set_cmd_sfunc (c, mips_abi_update);
  add_cmd ("abi", class_obscure, show_mips_abi,
	   "Show ABI in use by MIPS target", &showmipscmdlist);

  /* Let the user turn off floating point and set the fence post for
     heuristic_proc_start.  */

  add_prefix_cmd ("mipsfpu", class_support, set_mipsfpu_command,
		  "Set use of MIPS floating-point coprocessor.",
		  &mipsfpulist, "set mipsfpu ", 0, &setlist);
  add_cmd ("single", class_support, set_mipsfpu_single_command,
	   "Select single-precision MIPS floating-point coprocessor.",
	   &mipsfpulist);
  add_cmd ("double", class_support, set_mipsfpu_double_command,
	   "Select double-precision MIPS floating-point coprocessor.",
	   &mipsfpulist);
  add_alias_cmd ("on", "double", class_support, 1, &mipsfpulist);
  add_alias_cmd ("yes", "double", class_support, 1, &mipsfpulist);
  add_alias_cmd ("1", "double", class_support, 1, &mipsfpulist);
  add_cmd ("none", class_support, set_mipsfpu_none_command,
	   "Select no MIPS floating-point coprocessor.", &mipsfpulist);
  add_alias_cmd ("off", "none", class_support, 1, &mipsfpulist);
  add_alias_cmd ("no", "none", class_support, 1, &mipsfpulist);
  add_alias_cmd ("0", "none", class_support, 1, &mipsfpulist);
  add_cmd ("auto", class_support, set_mipsfpu_auto_command,
	   "Select MIPS floating-point coprocessor automatically.",
	   &mipsfpulist);
  add_cmd ("mipsfpu", class_support, show_mipsfpu_command,
	   "Show current use of MIPS floating-point coprocessor target.",
	   &showlist);

  /* We really would like to have both "0" and "unlimited" work, but
     command.c doesn't deal with that.  So make it a var_zinteger
     because the user can always use "999999" or some such for unlimited.  */
  c = add_set_cmd ("heuristic-fence-post", class_support, var_zinteger,
		   (char *) &heuristic_fence_post, "\
Set the distance searched for the start of a function.\n\
If you are debugging a stripped executable, GDB needs to search through the\n\
program for the start of a function.  This command sets the distance of the\n\
search.  The only need to set it is when debugging a stripped executable.", &setlist);
  /* We need to throw away the frame cache when we set this, since it
     might change our ability to get backtraces.  */
  set_cmd_sfunc (c, reinit_frame_cache_sfunc);
  add_show_from_set (c, &showlist);

  /* Allow the user to control whether the upper bits of 64-bit
     addresses should be zeroed.  */
  add_setshow_auto_boolean_cmd ("mask-address", no_class, &mask_address_var, "\
Set zeroing of upper 32 bits of 64-bit addresses.\n\
Use \"on\" to enable the masking, \"off\" to disable it and \"auto\" to \n\
allow GDB to determine the correct value.\n", "\
Show zeroing of upper 32 bits of 64-bit addresses.",
				NULL, show_mask_address, &setmipscmdlist, &showmipscmdlist);

  /* Allow the user to control the size of 32 bit registers within the
     raw remote packet.  */
  add_setshow_cmd ("remote-mips64-transfers-32bit-regs", class_obscure,
		   var_boolean, &mips64_transfers_32bit_regs_p, "\
Set compatibility with 64-bit MIPS targets that transfer 32-bit quantities.\n\
Use \"on\" to enable backward compatibility with older MIPS 64 GDB+target\n\
that would transfer 32 bits for some registers (e.g. SR, FSR) and\n\
64 bits for others.  Use \"off\" to disable compatibility mode", "\
Show compatibility with 64-bit MIPS targets that transfer 32-bit quantities.\n\
Use \"on\" to enable backward compatibility with older MIPS 64 GDB+target\n\
that would transfer 32 bits for some registers (e.g. SR, FSR) and\n\
64 bits for others.  Use \"off\" to disable compatibility mode", set_mips64_transfers_32bit_regs, NULL, &setlist, &showlist);

  /* Debug this files internals. */
  add_show_from_set (add_set_cmd ("mips", class_maintenance, var_zinteger,
				  &mips_debug, "Set mips debugging.\n\
When non-zero, mips specific debugging is enabled.", &setdebuglist), &showdebuglist);
}
