/* Target-dependent code for PowerPC systems running FreeBSD.

   Copyright 2002, 2003, 2004 Free Software Foundation, Inc.

   Contributed by Wasabi Systems, Inc.

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
#include "gdbcore.h"
#include "regcache.h"
#include "target.h"
#include "breakpoint.h"
#include "value.h"
#include "gdb_string.h"
#include "osabi.h"
#include "regset.h"

#include "ppc-tdep.h"
#include "ppcfbsd-tdep.h"
#include "trad-frame.h"
#include "gdb_assert.h"
#include "solib-svr4.h"

#define REG_FIXREG_OFFSET(x)	((x) * sizeof(register_t))
#define REG_LR_OFFSET		(32 * sizeof(register_t))
#define REG_CR_OFFSET		(33 * sizeof(register_t))
#define REG_XER_OFFSET		(34 * sizeof(register_t))
#define REG_CTR_OFFSET		(35 * sizeof(register_t))
#define REG_PC_OFFSET		(36 * sizeof(register_t))
#define SIZEOF_STRUCT_REG	(37 * sizeof(register_t))

#define FPREG_FPR_OFFSET(x)	((x) * 8)
#define FPREG_FPSCR_OFFSET	(32 * 8)
#define SIZEOF_STRUCT_FPREG	(33 * 8)

void
ppcfbsd_supply_reg (char *regs, int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int i;

  for (i = tdep->ppc_gp0_regnum; i <= tdep->ppc_gplast_regnum; i++)
    {
      if (regno == i || regno == -1)
	regcache_raw_supply (current_regcache, i, regs +
			     REG_FIXREG_OFFSET (i - tdep->ppc_gp0_regnum));
    }

  if (regno == tdep->ppc_lr_regnum || regno == -1)
    regcache_raw_supply (current_regcache, tdep->ppc_lr_regnum,
			 regs + REG_LR_OFFSET);

  if (regno == tdep->ppc_cr_regnum || regno == -1)
    regcache_raw_supply (current_regcache, tdep->ppc_cr_regnum,
			 regs + REG_CR_OFFSET);

  if (regno == tdep->ppc_xer_regnum || regno == -1)
    regcache_raw_supply (current_regcache, tdep->ppc_xer_regnum,
			 regs + REG_XER_OFFSET);

  if (regno == tdep->ppc_ctr_regnum || regno == -1)
    regcache_raw_supply (current_regcache, tdep->ppc_ctr_regnum,
			 regs + REG_CTR_OFFSET);

  if (regno == PC_REGNUM || regno == -1)
    regcache_raw_supply (current_regcache, PC_REGNUM,
			 regs + REG_PC_OFFSET);
}
static void
ppcfbsd_supply_gregset (const struct regset *regset,
			struct regcache *regcache,
			int regnum, void *gregs, size_t size)
{
  ppcfbsd_supply_reg (gregs, -1);
}

static struct regset ppcfbsd_gregset = {
  NULL, (void*)ppcfbsd_supply_gregset
};

void
ppcfbsd_fill_reg (char *regs, int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int i;

  for (i = tdep->ppc_gp0_regnum; i <= tdep->ppc_gplast_regnum; i++)
    {
      if (regno == i || regno == -1)
	regcache_raw_collect (current_regcache, i, regs +
			      REG_FIXREG_OFFSET (i - tdep->ppc_gp0_regnum));
    }

  if (regno == tdep->ppc_lr_regnum || regno == -1)
    regcache_raw_collect (current_regcache, tdep->ppc_lr_regnum,
			  regs + REG_LR_OFFSET);

  if (regno == tdep->ppc_cr_regnum || regno == -1)
    regcache_raw_collect (current_regcache, tdep->ppc_cr_regnum,
			  regs + REG_CR_OFFSET);

  if (regno == tdep->ppc_xer_regnum || regno == -1)
    regcache_raw_collect (current_regcache, tdep->ppc_xer_regnum,
			  regs + REG_XER_OFFSET);

  if (regno == tdep->ppc_ctr_regnum || regno == -1)
    regcache_raw_collect (current_regcache, tdep->ppc_ctr_regnum,
			  regs + REG_CTR_OFFSET);

  if (regno == PC_REGNUM || regno == -1)
    regcache_raw_collect (current_regcache, PC_REGNUM, regs + REG_PC_OFFSET);
}

void
ppcfbsd_supply_fpreg (char *fpregs, int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int i;

  /* FIXME: jimb/2004-05-05: Some PPC variants don't have floating
     point registers.  Traditionally, GDB's register set has still
     listed the floating point registers for such machines, so this
     code is harmless.  However, the new E500 port actually omits the
     floating point registers entirely from the register set --- they
     don't even have register numbers assigned to them.

     It's not clear to me how best to update this code, so this assert
     will alert the first person to encounter the NetBSD/E500
     combination to the problem.  */
  gdb_assert (ppc_floating_point_unit_p (current_gdbarch));

  for (i = FP0_REGNUM; i <= FPLAST_REGNUM; i++)
    {
      if (regno == i || regno == -1)
	regcache_raw_supply (current_regcache, i, fpregs +
			     FPREG_FPR_OFFSET (i - FP0_REGNUM));
    }

  if (regno == tdep->ppc_fpscr_regnum || regno == -1)
    regcache_raw_supply (current_regcache, tdep->ppc_fpscr_regnum,
			 fpregs + FPREG_FPSCR_OFFSET);
}

static void
ppcfbsd_supply_fpregset (const struct regset *regset,
			 struct regcache * regcache,
			 int regnum, void *fpset, size_t size)
{
  ppcfbsd_supply_fpreg (fpset, -1);
}


static struct regset ppcfbsd_fpregset =
{
  NULL, (void*)ppcfbsd_supply_fpregset
};

void
ppcfbsd_fill_fpreg (char *fpregs, int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int i;

  /* FIXME: jimb/2004-05-05: Some PPC variants don't have floating
     point registers.  Traditionally, GDB's register set has still
     listed the floating point registers for such machines, so this
     code is harmless.  However, the new E500 port actually omits the
     floating point registers entirely from the register set --- they
     don't even have register numbers assigned to them.

     It's not clear to me how best to update this code, so this assert
     will alert the first person to encounter the NetBSD/E500
     combination to the problem.  */
  gdb_assert (ppc_floating_point_unit_p (current_gdbarch));

  for (i = FP0_REGNUM; i <= FPLAST_REGNUM; i++)
    {
      if (regno == i || regno == -1)
	regcache_raw_collect (current_regcache, i, fpregs +
			      FPREG_FPR_OFFSET (i - FP0_REGNUM));
    }

  if (regno == tdep->ppc_fpscr_regnum || regno == -1)
    regcache_raw_collect (current_regcache, tdep->ppc_fpscr_regnum,
			  fpregs + FPREG_FPSCR_OFFSET);
}

/* Return the appropriate register set for the core section identified
   by SECT_NAME and SECT_SIZE.  */

const struct regset *
ppcfbsd_regset_from_core_section (struct gdbarch *gdbarch,
				const char *sect_name, size_t sect_size)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  if (strcmp (sect_name, ".reg") == 0 && sect_size >= SIZEOF_STRUCT_REG)
    return &ppcfbsd_gregset;

  if (strcmp (sect_name, ".reg2") == 0 && sect_size >= SIZEOF_STRUCT_FPREG)
    return &ppcfbsd_fpregset;

  return NULL;
}


/* Macros for matching instructions.  Note that, since all the
   operands are masked off before they're or-ed into the instruction,
   you can use -1 to make masks.  */

#define insn_d(opcd, rts, ra, d)                \
  ((((opcd) & 0x3f) << 26)                      \
   | (((rts) & 0x1f) << 21)                     \
   | (((ra) & 0x1f) << 16)                      \
   | ((d) & 0xffff))

#define insn_ds(opcd, rts, ra, d, xo)           \
  ((((opcd) & 0x3f) << 26)                      \
   | (((rts) & 0x1f) << 21)                     \
   | (((ra) & 0x1f) << 16)                      \
   | ((d) & 0xfffc)                             \
   | ((xo) & 0x3))

#define insn_xfx(opcd, rts, spr, xo)            \
  ((((opcd) & 0x3f) << 26)                      \
   | (((rts) & 0x1f) << 21)                     \
   | (((spr) & 0x1f) << 16)                     \
   | (((spr) & 0x3e0) << 6)                     \
   | (((xo) & 0x3ff) << 1))

/* Read a PPC instruction from memory.  PPC instructions are always
   big-endian, no matter what endianness the program is running in, so
   we can't use read_memory_integer or one of its friends here.  */
static unsigned int
read_insn (CORE_ADDR pc)
{
  unsigned char buf[4];

  read_memory (pc, buf, 4);
  return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}


/* An instruction to match.  */
struct insn_pattern
{
  unsigned int mask;            /* mask the insn with this... */
  unsigned int data;            /* ...and see if it matches this. */
  int optional;                 /* If non-zero, this insn may be absent.  */
};

/* Return non-zero if the instructions at PC match the series
   described in PATTERN, or zero otherwise.  PATTERN is an array of
   'struct insn_pattern' objects, terminated by an entry whose mask is
   zero.

   When the match is successful, fill INSN[i] with what PATTERN[i]
   matched.  If PATTERN[i] is optional, and the instruction wasn't
   present, set INSN[i] to 0 (which is not a valid PPC instruction).
   INSN should have as many elements as PATTERN.  Note that, if
   PATTERN contains optional instructions which aren't present in
   memory, then INSN will have holes, so INSN[i] isn't necessarily the
   i'th instruction in memory.  */
static int
insns_match_pattern (CORE_ADDR pc,
                     struct insn_pattern *pattern,
                     unsigned int *insn)
{
  int i;

  for (i = 0; pattern[i].mask; i++)
    {
      insn[i] = read_insn (pc);
      if ((insn[i] & pattern[i].mask) == pattern[i].data)
        pc += 4;
      else if (pattern[i].optional)
        insn[i] = 0;
      else
        return 0;
    }

  return 1;
}


/* Return the 'd' field of the d-form instruction INSN, properly
   sign-extended.  */
static CORE_ADDR
insn_d_field (unsigned int insn)
{
  return ((((CORE_ADDR) insn & 0xffff) ^ 0x8000) - 0x8000);
}


/* Return the 'ds' field of the ds-form instruction INSN, with the two
   zero bits concatenated at the right, and properly
   sign-extended.  */
static CORE_ADDR
insn_ds_field (unsigned int insn)
{
  return ((((CORE_ADDR) insn & 0xfffc) ^ 0x8000) - 0x8000);
}


/* If DESC is the address of a 64-bit PowerPC FreeBSD function
   descriptor, return the descriptor's entry point.  */
static CORE_ADDR
ppc64_desc_entry_point (CORE_ADDR desc)
{
  /* The first word of the descriptor is the entry point.  */
  return (CORE_ADDR) read_memory_unsigned_integer (desc, 8);
}


/* Pattern for the standard linkage function.  These are built by
   build_plt_stub in elf64-ppc.c, whose GLINK argument is always
   zero.  */
static struct insn_pattern ppc64_standard_linkage[] =
  {
    /* addis r12, r2, <any> */
    { insn_d (-1, -1, -1, 0), insn_d (15, 12, 2, 0), 0 },

    /* std r2, 40(r1) */
    { -1, insn_ds (62, 2, 1, 40, 0), 0 },

    /* ld r11, <any>(r12) */
    { insn_ds (-1, -1, -1, 0, -1), insn_ds (58, 11, 12, 0, 0), 0 },

    /* addis r12, r12, 1 <optional> */
    { insn_d (-1, -1, -1, -1), insn_d (15, 12, 2, 1), 1 },

    /* ld r2, <any>(r12) */
    { insn_ds (-1, -1, -1, 0, -1), insn_ds (58, 2, 12, 0, 0), 0 },

    /* addis r12, r12, 1 <optional> */
    { insn_d (-1, -1, -1, -1), insn_d (15, 12, 2, 1), 1 },

    /* mtctr r11 */
    { insn_xfx (-1, -1, -1, -1), insn_xfx (31, 11, 9, 467),
      0 },

    /* ld r11, <any>(r12) */
    { insn_ds (-1, -1, -1, 0, -1), insn_ds (58, 11, 12, 0, 0), 0 },
      
    /* bctr */
    { -1, 0x4e800420, 0 },

    { 0, 0, 0 }
  };
#define PPC64_STANDARD_LINKAGE_LEN \
  (sizeof (ppc64_standard_linkage) / sizeof (ppc64_standard_linkage[0]))

/* When the dynamic linker is doing lazy symbol resolution, the first
   call to a function in another object will go like this:

   - The user's function calls the linkage function:

     100007c4:	4b ff fc d5 	bl	10000498
     100007c8:	e8 41 00 28 	ld	r2,40(r1)

   - The linkage function loads the entry point (and other stuff) from
     the function descriptor in the PLT, and jumps to it:

     10000498:	3d 82 00 00 	addis	r12,r2,0
     1000049c:	f8 41 00 28 	std	r2,40(r1)
     100004a0:	e9 6c 80 98 	ld	r11,-32616(r12)
     100004a4:	e8 4c 80 a0 	ld	r2,-32608(r12)
     100004a8:	7d 69 03 a6 	mtctr	r11
     100004ac:	e9 6c 80 a8 	ld	r11,-32600(r12)
     100004b0:	4e 80 04 20 	bctr

   - But since this is the first time that PLT entry has been used, it
     sends control to its glink entry.  That loads the number of the
     PLT entry and jumps to the common glink0 code:

     10000c98:	38 00 00 00 	li	r0,0
     10000c9c:	4b ff ff dc 	b	10000c78

   - The common glink0 code then transfers control to the dynamic
     linker's fixup code:

     10000c78:	e8 41 00 28 	ld	r2,40(r1)
     10000c7c:	3d 82 00 00 	addis	r12,r2,0
     10000c80:	e9 6c 80 80 	ld	r11,-32640(r12)
     10000c84:	e8 4c 80 88 	ld	r2,-32632(r12)
     10000c88:	7d 69 03 a6 	mtctr	r11
     10000c8c:	e9 6c 80 90 	ld	r11,-32624(r12)
     10000c90:	4e 80 04 20 	bctr

   Eventually, this code will figure out how to skip all of this,
   including the dynamic linker.  At the moment, we just get through
   the linkage function.  */

/* If the current thread is about to execute a series of instructions
   at PC matching the ppc64_standard_linkage pattern, and INSN is the result
   from that pattern match, return the code address to which the
   standard linkage function will send them.  (This doesn't deal with
   dynamic linker lazy symbol resolution stubs.)  */
static CORE_ADDR
ppc64_standard_linkage_target (CORE_ADDR pc, unsigned int *insn)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  /* The address of the function descriptor this linkage function
     references.  */
  CORE_ADDR desc
    = ((CORE_ADDR) read_register (tdep->ppc_gp0_regnum + 2)
       + (insn_d_field (insn[0]) << 16)
       + insn_ds_field (insn[2]));

  /* The first word of the descriptor is the entry point.  Return that.  */
  return ppc64_desc_entry_point (desc);
}


/* Given that we've begun executing a call trampoline at PC, return
   the entry point of the function the trampoline will go to.  */
static CORE_ADDR
ppc64_skip_trampoline_code (CORE_ADDR pc)
{
  unsigned int ppc64_standard_linkage_insn[PPC64_STANDARD_LINKAGE_LEN];

  if (insns_match_pattern (pc, ppc64_standard_linkage,
                           ppc64_standard_linkage_insn))
    return ppc64_standard_linkage_target (pc, ppc64_standard_linkage_insn);
  else
    return 0;
}


/* Support for CONVERT_FROM_FUNC_PTR_ADDR (ARCH, ADDR, TARG) on PPC64
   GNU/Linux and FreeBSD.

   Usually a function pointer's representation is simply the address
   of the function. On GNU/Linux on the 64-bit PowerPC however, a
   function pointer is represented by a pointer to a TOC entry. This
   TOC entry contains three words, the first word is the address of
   the function, the second word is the TOC pointer (r2), and the
   third word is the static chain value.  Throughout GDB it is
   currently assumed that a function pointer contains the address of
   the function, which is not easy to fix.  In addition, the
   conversion of a function address to a function pointer would
   require allocation of a TOC entry in the inferior's memory space,
   with all its drawbacks.  To be able to call C++ virtual methods in
   the inferior (which are called via function pointers),
   find_function_addr uses this function to get the function address
   from a function pointer.  */

/* If ADDR points at what is clearly a function descriptor, transform
   it into the address of the corresponding function.  Be
   conservative, otherwize GDB will do the transformation on any
   random addresses such as occures when there is no symbol table.  */

static CORE_ADDR
ppc64_fbsd_convert_from_func_ptr_addr (struct gdbarch *gdbarch,
				       CORE_ADDR addr,
				       struct target_ops *targ)
{
  struct section_table *s = target_section_by_addr (targ, addr);
  
  /* Check if ADDR points to a function descriptor.  */
  if (s && strcmp (s->the_bfd_section->name, ".opd") == 0)
    return get_target_memory_unsigned (targ, addr, 8);

  return addr;
}

static int
ppcfbsd_pc_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  return (pc >= 0x7fffef00U) ? 1 : 0;
}

/* NetBSD is confused.  It appears that 1.5 was using the correct SVr4
   convention but, 1.6 switched to the below broken convention.  For
   the moment use the broken convention.  Ulgh!.  */

static enum return_value_convention
ppcfbsd_return_value (struct gdbarch *gdbarch, struct type *valtype,
		      struct regcache *regcache, void *readbuf,
		      const void *writebuf)
{
  if ((TYPE_CODE (valtype) == TYPE_CODE_STRUCT
       || TYPE_CODE (valtype) == TYPE_CODE_UNION)
      && !((TYPE_LENGTH (valtype) == 16 || TYPE_LENGTH (valtype) == 8))
      && !(TYPE_LENGTH (valtype) == 1
	   || TYPE_LENGTH (valtype) == 2
	   || TYPE_LENGTH (valtype) == 4
	   || TYPE_LENGTH (valtype) == 8)) 
    return RETURN_VALUE_STRUCT_CONVENTION; 
  else 
    return ppc_sysv_abi_broken_return_value (gdbarch, valtype, regcache,
					     readbuf, writebuf); 
}

static void
ppcfbsd_init_abi (struct gdbarch_info info,
                  struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* FreeBSD doesn't support the 128-bit `long double' from the psABI. */
  set_gdbarch_long_double_bit (gdbarch, 64);

  set_gdbarch_pc_in_sigtramp (gdbarch, ppcfbsd_pc_in_sigtramp);

  if (tdep->wordsize == 4)
    {
      set_gdbarch_return_value (gdbarch, ppcfbsd_return_value);
      set_solib_svr4_fetch_link_map_offsets (gdbarch,
					     svr4_ilp32_fetch_link_map_offsets);
    }

  if (tdep->wordsize == 8)
    {
      set_gdbarch_convert_from_func_ptr_addr
        (gdbarch, ppc64_fbsd_convert_from_func_ptr_addr);

      set_gdbarch_skip_trampoline_code (gdbarch, ppc64_skip_trampoline_code);

      set_solib_svr4_fetch_link_map_offsets (gdbarch,
					     svr4_lp64_fetch_link_map_offsets);
    }

  set_gdbarch_regset_from_core_section (gdbarch,
					ppcfbsd_regset_from_core_section);
}

void
_initialize_ppcfbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_powerpc, bfd_mach_ppc,
			  GDB_OSABI_FREEBSD_ELF, ppcfbsd_init_abi);
  gdbarch_register_osabi (bfd_arch_powerpc, bfd_mach_ppc64,
			  GDB_OSABI_FREEBSD_ELF, ppcfbsd_init_abi);
  gdbarch_register_osabi (bfd_arch_rs6000, 0, GDB_OSABI_FREEBSD_ELF,
			  ppcfbsd_init_abi);
}
