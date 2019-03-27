/***********************************************************************
Copyright 2003-2006 Raza Microelectronics, Inc.(RMI).
This is a derived work from software originally provided by the external
entity identified below. The licensing terms and warranties specified in
the header of the original work apply to this derived work.
Contribution by RMI: 
*****************************#RMI_1#**********************************/
/* Target-dependent code for MIPS systems running NetBSD.
   Copyright 2002, 2003 Free Software Foundation, Inc.
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
#include "value.h"
#include "osabi.h"

#include "nbsd-tdep.h"     
#include "mipsfbsd-tdep.h" 
#include "mips-tdep.h"     

#include "solib-svr4.h"

#include <sys/procfs.h>
#include "gregset.h"
#include "trad-frame.h"
#include "frame.h"
#include "frame-unwind.h"
#include "bfd.h"
#include "objfiles.h"

/* Conveniently, GDB uses the same register numbering as the
   ptrace register structure used by NetBSD/mips.  */

void
mipsfbsd_supply_reg (char *regs, int regno)
{
  int i;

  for (i = 0; i <= PC_REGNUM; i++)
    {
      if (regno == i || regno == -1)
	{
	  if (CANNOT_FETCH_REGISTER (i))
	    supply_register (i, NULL);
	  else
            supply_register (i, regs + (i * mips_regsize (current_gdbarch)));
        }
    }
}
void 
supply_gregset (gdb_gregset_t *gregs)
{
  mipsfbsd_supply_reg((char *)gregs, -1);
}

void
mipsfbsd_fill_reg (char *regs, int regno)
{
  int i;

  for (i = 0; i <= PC_REGNUM; i++)
    if ((regno == i || regno == -1) && ! CANNOT_STORE_REGISTER (i))
      regcache_collect (i, regs + (i * mips_regsize (current_gdbarch)));
}

void 
fill_gregset (gdb_gregset_t  *gregs, int regno)
{
  mipsfbsd_fill_reg ((char *)gregs, regno);
}

void
mipsfbsd_supply_fpreg (char *fpregs, int regno)
{
  int i;

  for (i = FP0_REGNUM;
       i <= mips_regnum (current_gdbarch)->fp_implementation_revision;
       i++)
    {
      if (regno == i || regno == -1)
	{
	  if (CANNOT_FETCH_REGISTER (i))
	    supply_register (i, NULL);
	  else
            supply_register (i, 
		fpregs + ((i - FP0_REGNUM) * mips_regsize (current_gdbarch)));
	}
    }
}

void 
supply_fpregset (gdb_fpregset_t *fpregs)
{
  mipsfbsd_supply_fpreg((char *)fpregs, -1);
}

void
mipsfbsd_fill_fpreg (char *fpregs, int regno)
{
  int i;

  for (i = FP0_REGNUM; i <= mips_regnum (current_gdbarch)->fp_control_status;
       i++)
    if ((regno == i || regno == -1) && ! CANNOT_STORE_REGISTER (i))
      regcache_collect (i, 
	  fpregs + ((i - FP0_REGNUM) * mips_regsize (current_gdbarch)));
}

void 
fill_fpregset (gdb_fpregset_t *fpregs, int regno)
{
  mipsfbsd_fill_fpreg ((char *)fpregs, regno);
}

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size, int which,
                      CORE_ADDR ignore)
{
  char *regs, *fpregs;

  /* We get everything from one section.  */
  if (which != 0)
    return;

  regs = core_reg_sect;
  fpregs = core_reg_sect + SIZEOF_STRUCT_REG;

  /* Integer registers.  */
  mipsfbsd_supply_reg (regs, -1);

  /* Floating point registers.  */
  mipsfbsd_supply_fpreg (fpregs, -1);
}

static void
fetch_elfcore_registers (char *core_reg_sect, unsigned core_reg_size, int which,
                         CORE_ADDR ignore)
{
  switch (which)
    {
    case 0:  /* Integer registers.  */
      if (core_reg_size != SIZEOF_STRUCT_REG)
	warning ("Wrong size register set in core file.");
      else
	mipsfbsd_supply_reg (core_reg_sect, -1);
      break;

    case 2:  /* Floating point registers.  */
      if (core_reg_size != SIZEOF_STRUCT_FPREG)
	warning ("Wrong size register set in core file.");
      else
	mipsfbsd_supply_fpreg (core_reg_sect, -1);
      break;

    default:
      /* Don't know what kind of register request this is; just ignore it.  */
      break;
    }
}

static struct core_fns mipsfbsd_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

static struct core_fns mipsfbsd_elfcore_fns =
{
  bfd_target_elf_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_elfcore_registers,		/* core_read_registers */
  NULL					/* next */
};

/*
 * MIPSFBSD Offsets
 * 0x7fff0000    User high mem -> USRSTACK [64K]
 * 
 * 0x7ffefff0    ps_strings    -> 16 bytes
 *
 * 0x7ffeffec    sigcode       -> 44 bytes
 *
 * 0x7ffeffc4    sigcode end   env strings etc start
 *
 * XXX This is out-of-date and varies by ABI.
 */
#define MIPS_FBSD_SIGTRAMP_START           (0x7ffeffc4)
#define MIPS_FBSD_SIGTRAMP_END             (0x7ffeffec)
#define MIPS_FBSD_SIGTRAMP_STACK_MOD_START (0x7ffeffc8)
#define MIPS_FBSD_SIGTRAMP_STACK_MOD_END   (0x7ffeffd8)

static LONGEST
mipsfbsd_sigtramp_offset (CORE_ADDR pc)
{
  return pc < MIPS_FBSD_SIGTRAMP_END && 
         pc >= MIPS_FBSD_SIGTRAMP_START ? 1 : -1;
}

static int
fbsd_pc_in_sigtramp (CORE_ADDR pc, char *name)
{ 
  return (name && strcmp (name, "__sigtramp") == 0);
}

static int
mipsfbsd_pc_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  return (fbsd_pc_in_sigtramp (pc, func_name)
	  || mipsfbsd_sigtramp_offset (pc) >= 0);
}

static int
is_sigtramp_sp_modified (CORE_ADDR pc)
{
  return (pc >= MIPS_FBSD_SIGTRAMP_STACK_MOD_START &&
          pc <= MIPS_FBSD_SIGTRAMP_STACK_MOD_END);
}
  

/* Figure out where the longjmp will land.  We expect that we have
   just entered longjmp and haven't yet setup the stack frame, so
   the args are still in the argument regs.  A0_REGNUM points at the
   jmp_buf structure from which we extract the PC that we will land
   at.  The PC is copied into *pc.  This routine returns true on
   success.  */

#define FBSD_MIPS_JB_PC			(12)
#define FBSD_MIPS_JB_ELEMENT_SIZE	mips_regsize (current_gdbarch)
#define FBSD_MIPS_JB_OFFSET		(FBSD_MIPS_JB_PC * \
					 FBSD_MIPS_JB_ELEMENT_SIZE)

static int
mipsfbsd_get_longjmp_target (CORE_ADDR *pc)
{
  CORE_ADDR jb_addr;
  char *buf;

  buf = alloca (FBSD_MIPS_JB_ELEMENT_SIZE);

  jb_addr = read_register (A0_REGNUM);

  if (target_read_memory (jb_addr + FBSD_MIPS_JB_OFFSET, buf,
  			  FBSD_MIPS_JB_ELEMENT_SIZE))
    return 0;

  *pc = extract_unsigned_integer (buf, FBSD_MIPS_JB_ELEMENT_SIZE);

  return 1;
}

static int
mipsfbsd_cannot_fetch_register (int regno)
{
  return (regno == ZERO_REGNUM
	  || regno == mips_regnum (current_gdbarch)->fp_implementation_revision);
  /* XXX TODO: Are there other registers that we cannot fetch ? */
}

static int
mipsfbsd_cannot_store_register (int regno)
{
  return (regno == ZERO_REGNUM
	  || regno == mips_regnum (current_gdbarch)->fp_implementation_revision);
  /* XXX TODO: Are there other registers that we cannot write ? */
}

/* 
 * This structure is defined in mips-tdep.c. 
 */
struct mips_frame_cache
{
  CORE_ADDR base;
  struct trad_frame_saved_reg *saved_regs;
};

/*
 * Prologue cache for sigtramp frame 
 * When we land in sigtramp, sigcontext is saved on the
 * stack just below the sigtramp's stack frame. We have
 * the Registers saved at fixed offsets on the stack.
 */

#define MIPS_FBSD_SIGTRAMP_STACK_SIZE    (48)
#define MIPS_FBSD_SIGCONTEXT_REG_OFFSET  (32)

static struct mips_frame_cache *
mipsfbsd_sigtramp_frame_cache (struct frame_info *next_frame,
                               void **this_cache)
{
  struct mips_frame_cache *cache;
  CORE_ADDR  gregs_addr, sp, pc;
  int regnum;
  int sigtramp_stack_size;

  if (*this_cache)
    return *this_cache;

  cache = FRAME_OBSTACK_ZALLOC (struct mips_frame_cache);
  *this_cache = cache;
 
  cache->saved_regs =  trad_frame_alloc_saved_regs (next_frame);
   
  /* 
   * Get sp of next frame which is the adjusted sp of
   * tramp code.
   */
  sp = frame_unwind_register_unsigned(next_frame, NUM_REGS + SP_REGNUM);
  pc = frame_unwind_register_unsigned(next_frame, NUM_REGS + PC_REGNUM);
  sigtramp_stack_size = is_sigtramp_sp_modified(pc) ? 
	  MIPS_FBSD_SIGTRAMP_STACK_SIZE : 0;
  gregs_addr = sp + sigtramp_stack_size + MIPS_FBSD_SIGCONTEXT_REG_OFFSET;

  for (regnum = 0; regnum < PC_REGNUM; regnum++) {
    cache->saved_regs[NUM_REGS + regnum].addr = gregs_addr + 
       regnum * mips_regsize (current_gdbarch);
  }
  /* Only retrieve PC and SP */
  cache->saved_regs[NUM_REGS + SP_REGNUM].addr = gregs_addr +
       SP_REGNUM * ( mips_regsize (current_gdbarch));

  cache->saved_regs[NUM_REGS + RA_REGNUM].addr = gregs_addr +
        RA_REGNUM * ( mips_regsize (current_gdbarch));

  cache->base = get_frame_memory_unsigned (next_frame,
    cache->saved_regs[NUM_REGS + SP_REGNUM].addr,
    mips_regsize (current_gdbarch)); 

  /* Todo: Floating point registers */

  cache->saved_regs[NUM_REGS + mips_regnum (current_gdbarch)->pc]
    =  cache->saved_regs[NUM_REGS + RA_REGNUM];

  return *this_cache;
}

static void 
mipsfbsd_sigtramp_frame_this_id (struct frame_info *next_frame,
                                 void **this_cache,
				 struct frame_id *this_id)
{
  struct mips_frame_cache *cache =
    mipsfbsd_sigtramp_frame_cache (next_frame, this_cache);

    (*this_id) = frame_id_build (cache->base, 
	cache->saved_regs[NUM_REGS + mips_regnum (current_gdbarch)->pc].addr);
}

static void
mipsfbsd_sigtramp_frame_prev_register (struct frame_info *next_frame,
                                       void **this_cache,
				       int regnum, int *optimizedp,
				       enum lval_type *lvalp,
				       CORE_ADDR *addrp,
				       int *realnump, void *valuep)
{
  struct mips_frame_cache *cache =
    mipsfbsd_sigtramp_frame_cache (next_frame, this_cache);

    trad_frame_prev_register (next_frame, cache->saved_regs, regnum,
			      optimizedp, lvalp, addrp, realnump, valuep);
}


static const struct frame_unwind mipsfbsd_sigtramp_frame_unwind = 
{
  SIGTRAMP_FRAME,
  mipsfbsd_sigtramp_frame_this_id,
  mipsfbsd_sigtramp_frame_prev_register
};

static const struct frame_unwind *
mipsfbsd_sigtramp_frame_sniffer (struct frame_info *next_frame)
{
  CORE_ADDR pc = frame_pc_unwind (next_frame);
  char *name;

  find_pc_partial_function (pc, &name, NULL, NULL);
  if (mipsfbsd_pc_in_sigtramp (pc, name) )
    return &mipsfbsd_sigtramp_frame_unwind;

  return NULL;
}

/*
 * Find out if PC has landed into dynamic library stub.
 * We can find it by seeing if the name of the object
 * file section where the PC lies is "MIPS.stubs"
 */

int 
mipsfbsd_in_stub_section (CORE_ADDR pc, char *name)
{
  struct obj_section *s;
  int retval = 0;

  s = find_pc_section (pc);

  retval = (s != NULL
            && s->the_bfd_section->name != NULL
            && strcmp (s->the_bfd_section->name, ".MIPS.stubs") == 0);
  return (retval);
}


/*
 * Prologue cache for dynamic library stub frame.
 * This stub does not modify the SP, so we set the
 * cache base to calling frame's SP
 */
static struct mips_frame_cache *
mipsfbsd_stub_frame_cache (struct frame_info *next_frame,
                           void **this_cache)
{
  struct mips_frame_cache *cache;

  if (*this_cache)
    return *this_cache;

  cache = FRAME_OBSTACK_ZALLOC (struct mips_frame_cache);
  *this_cache = cache;

  cache->saved_regs =  trad_frame_alloc_saved_regs (next_frame);


  cache->saved_regs[NUM_REGS + mips_regnum (current_gdbarch)->pc].realreg =
    NUM_REGS + RA_REGNUM;
  cache->base = frame_unwind_register_unsigned (next_frame,
   NUM_REGS + SP_REGNUM); 

  return (*this_cache);
}

 
static void
mipsfbsd_stub_frame_this_id (struct frame_info *next_frame,
                             void **this_cache,
                             struct frame_id *this_id)
{
  struct mips_frame_cache *cache =
    mipsfbsd_stub_frame_cache (next_frame, this_cache);

    (*this_id) = frame_id_build (cache->base,
        cache->saved_regs[NUM_REGS + mips_regnum (current_gdbarch)->pc].addr);
}

static void
mipsfbsd_stub_frame_prev_register (struct frame_info *next_frame,
                                   void **this_cache,
                                   int regnum, int *optimizedp,
				   enum lval_type *lvalp, CORE_ADDR *addrp,
				   int *realnump, void *valuep)
{
  struct mips_frame_cache *cache = 
    mipsfbsd_stub_frame_cache (next_frame, this_cache);

  trad_frame_prev_register (next_frame, cache->saved_regs, regnum,
                              optimizedp, lvalp, addrp, realnump, valuep);
}

 
 
static const struct frame_unwind mipsfbsd_stub_frame_unwind = {
  NORMAL_FRAME,
  mipsfbsd_stub_frame_this_id,
  mipsfbsd_stub_frame_prev_register
};

static const struct frame_unwind *
mipsfbsd_stub_frame_sniffer (struct frame_info *next_frame)
{
   CORE_ADDR pc = frame_pc_unwind (next_frame);
   
   if (mipsfbsd_in_stub_section(pc, NULL)) 
     return &mipsfbsd_stub_frame_unwind;
   
   return NULL;
}
     
/*
 *  typedef struct link_map {
 *          caddr_t         l_addr;                 /* Base Address of library
 *  #ifdef __mips__
 *          caddr_t         l_offs;                 /* Load Offset of library
 *  #endif
 *          const char      *l_name;                /* Absolute Path to Library
 *          const void      *l_ld;                  /* Pointer to .dynamic in memory
 *          struct link_map *l_next, *l_prev;       /* linked list of of mapped libs
 *  } Link_map;
 *
 *  struct r_debug {
 *          int             r_version;              /* not used
 *          struct link_map *r_map;                 /* list of loaded images
 *          void            (*r_brk)(struct r_debug *, struct link_map *);
 *                                                  /* pointer to break point
 *          enum {
 *              RT_CONSISTENT,                      /* things are stable
 *              RT_ADD,                             /* adding a shared library
 *              RT_DELETE                           /* removing a shared library
 *          }               r_state;
 *  };
 *
 */

static struct link_map_offsets *
mipsfbsd_ilp32_solib_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_debug_size = 16;

      lmo.r_map_offset = 4;
      lmo.r_map_size   = 4;

      lmo.link_map_size = 24;

      lmo.l_addr_offset = 0;
      lmo.l_addr_size   = 4;

      lmo.l_name_offset = 8;
      lmo.l_name_size   = 4;

      lmo.l_next_offset = 16;
      lmo.l_next_size   = 4;

      lmo.l_prev_offset = 20;
      lmo.l_prev_size   = 4;
    }

  return lmp;
}

static void
mipsfbsd_init_abi (struct gdbarch_info info,
                   struct gdbarch *gdbarch)
{
  set_gdbarch_pc_in_sigtramp (gdbarch, mipsfbsd_pc_in_sigtramp);

  set_gdbarch_get_longjmp_target (gdbarch, mipsfbsd_get_longjmp_target);

  set_gdbarch_cannot_fetch_register (gdbarch, mipsfbsd_cannot_fetch_register);
  set_gdbarch_cannot_store_register (gdbarch, mipsfbsd_cannot_store_register);

  set_gdbarch_software_single_step (gdbarch, mips_software_single_step);
  set_solib_svr4_fetch_link_map_offsets (gdbarch,
      			    mipsfbsd_ilp32_solib_svr4_fetch_link_map_offsets);
  set_gdbarch_skip_trampoline_code (gdbarch, find_solib_trampoline_target);
  set_gdbarch_in_solib_call_trampoline (gdbarch, mipsfbsd_in_stub_section);

  /* frame sniffers */
  frame_unwind_append_sniffer (gdbarch, mipsfbsd_sigtramp_frame_sniffer);
  frame_unwind_append_sniffer (gdbarch, mipsfbsd_stub_frame_sniffer);

}

void
_initialize_mipsfbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_mips, 0, GDB_OSABI_FREEBSD_ELF,
			  mipsfbsd_init_abi);
  add_core_fns (&mipsfbsd_core_fns);
  add_core_fns (&mipsfbsd_elfcore_fns);
}
