/*
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "defs.h"
#include "gdb_string.h"
#include "regcache.h"
#include "regset.h"
#include "solib-svr4.h"
#include "value.h"

#include "ia64-tdep.h"

#define FPREG_SUPPLIES(r) ((r) >= IA64_FR0_REGNUM && (r) <= IA64_FR127_REGNUM)
#define GREG_SUPPLIES(r)  (!FPREG_SUPPLIES(r))

static int reg_offset[462] = {
    -1,   96,  248,  256,  152,  160,  168,  176,       /* Regs 0-7. */
   264,  272,  280,  288,    0,   64,  296,  304,       /* Regs 8-15. */
   312,  320,  328,  336,  344,  352,  360,  368,       /* Regs 16-23. */
   376,  384,  392,  400,  408,  416,  424,  432,       /* Regs 24-31. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 32-39. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 40-47. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 48-55. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 56-63. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 64-71. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 72-79. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 80-87. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 88-95. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 96-103. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 104-111. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 112-119. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 120-127. */
    -1,   -1,    0,   16,   32,   48,  320,  336,       /* Regs 128-135. */
   352,  368,  384,  400,  416,  432,  448,  464,       /* Regs 136-143. */
    64,   80,   96,  112,  128,  144,  160,  176,       /* Regs 144-151. */
   192,  208,  224,  240,  256,  272,  288,  304,       /* Regs 152-159. */
   480,  496,  512,  528,  544,  560,  576,  592,       /* Regs 160-167. */
   608,  624,  640,  656,  672,  688,  704,  720,       /* Regs 168-175. */
   736,  752,  768,  784,  800,  816,  832,  848,       /* Regs 176-183. */
   864,  880,  896,  912,  928,  944,  960,  976,       /* Regs 184-191. */
   992, 1008, 1024, 1040, 1056, 1072, 1088, 1104,       /* Regs 192-199. */
  1120, 1136, 1152, 1168, 1184, 1200, 1216, 1232,       /* Regs 200-207. */
  1248, 1264, 1280, 1296, 1312, 1328, 1344, 1360,       /* Regs 208-215. */
  1376, 1392, 1408, 1424, 1440, 1456, 1472, 1488,       /* Regs 216-223. */
  1504, 1520, 1536, 1552, 1568, 1584, 1600, 1616,       /* Regs 224-231. */
  1632, 1648, 1664, 1680, 1696, 1712, 1728, 1744,       /* Regs 232-239. */
  1760, 1776, 1792, 1808, 1824, 1840, 1856, 1872,       /* Regs 240-247. */
  1888, 1904, 1920, 1936, 1952, 1968, 1984, 2000,       /* Regs 248-255. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 256-263. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 264-271. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 272-279. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 280-287. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 288-295. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 296-303. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 304-311. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 312-319. */
    16,  184,  192,  200,  208,  216,  440,  448,       /* Regs 320-327. */
    -1,   -1,   24,  120,   88,  112,   -1,   -1,       /* Regs 328-335. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 336-343. */
    -1,   -1,   -1,   -1,   -1,   -1,   72,  104,       /* Regs 344-351. */
    40,   48,   -1,   -1,   -1,   -1,   -1,  464,       /* Regs 352-359. */
   472,   -1,   -1,   -1,   -1,   -1,  456,   -1,       /* Regs 360-367. */
    -1,   -1,    8,   -1,   -1,   -1,   80,   -1,       /* Regs 368-375. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 376-383. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 384-391. */
    -1,   -1,   -1,   -1,   -1,   -1,   32,  224,       /* Regs 392-399. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 400-407. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 408-415. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 416-423. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 424-431. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 432-439. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 440-447. */
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,       /* Regs 448-455. */
    -1,   -1,   -1,   -1,   -1,   -1
};

static void
ia64_fbsd_regcache_collect (struct regcache *regcache, int regno,
			    void *regs)
{
  int ofs;

  if (regno < 0 || regno >= NUM_REGS)
    return;

  ofs = reg_offset[regno];
  if (regno == IA64_BSP_REGNUM)
    {
      uint64_t bsp, bspstore;
      regcache_raw_collect (regcache, regno, &bsp);
      regcache_raw_collect (regcache, IA64_BSPSTORE_REGNUM, &bspstore);
      *(uint64_t *)((char *)regs + ofs) = bsp - bspstore;
    }
  else
    {
      if (ofs >= 0)
	regcache_raw_collect (regcache, regno, (char*)regs + ofs);
    }
}

static void
ia64_fbsd_regcache_supply (struct regcache *regcache, int regno,
			   const void *regs)
{
  int ofs;

  if (regno < 0 || regno >= NUM_REGS)
    return;

  ofs = reg_offset[regno];
  if (regno == IA64_BSP_REGNUM)
    {
      /* BSP is synthesized. It's not actually present in struct reg,
	 but can be derived from bspstore and ndirty. The offset of
	 IA64_BSP_REGNUM in the reg_offset array above is that of the
	 ndirty field in struct reg. */
      uint64_t bsp;
      bsp = *((uint64_t*)((char *)regs + ofs));		/* ndirty */
      bsp += *((uint64_t*)((char *)regs + reg_offset[IA64_BSPSTORE_REGNUM]));
      regcache_raw_supply (regcache, regno, &bsp);
    }
  else
    {
      if (ofs < 0)
	regcache_raw_supply (regcache, regno, NULL);
      else
	regcache_raw_supply (regcache, regno, (char *)regs + ofs);
    }
}

void
fill_fpregset (void *fpregs, int regno)
{
  if (regno == -1)
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	{
	  if (FPREG_SUPPLIES(regno))
	    ia64_fbsd_regcache_collect (current_regcache, regno, fpregs);
	}
    }
  else
    {
      if (FPREG_SUPPLIES(regno))
	ia64_fbsd_regcache_collect (current_regcache, regno, fpregs);
    }
}

void
fill_gregset (void *gregs, int regno)
{
  if (regno == -1)
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	{
	  if (GREG_SUPPLIES(regno))
	    ia64_fbsd_regcache_collect (current_regcache, regno, gregs);
	}
    }
  else
    {
      if (GREG_SUPPLIES(regno))
	ia64_fbsd_regcache_collect (current_regcache, regno, gregs);
    }
}

void
supply_fpregset (const void *fpregs)
{
  int regno;

  for (regno = 0; regno < NUM_REGS; regno++)
    {
      if (FPREG_SUPPLIES(regno))
	ia64_fbsd_regcache_supply (current_regcache, regno, fpregs);
    }
}

void
supply_gregset (const void *gregs)
{
  int regno;

  for (regno = 0; regno < NUM_REGS; regno++)
    {
      if (GREG_SUPPLIES(regno))
	ia64_fbsd_regcache_supply (current_regcache, regno, gregs);
    }
}

static void
ia64_fbsd_supply_gregset (const struct regset *regset,
			  struct regcache *regcache, int regno,
			  const void *gregs, size_t len)
{
  if (regno == -1)
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	{
	  if (GREG_SUPPLIES(regno))
	    ia64_fbsd_regcache_supply (regcache, regno, gregs);
	}
    }
  else
    if (GREG_SUPPLIES(regno))
      ia64_fbsd_regcache_supply (regcache, regno, gregs);
}

static void
ia64_fbsd_supply_fpregset (const struct regset *regset,
			   struct regcache *regcache, int regno,
			   const void *fpregs, size_t len)
{
  if (regno == -1)
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	{
	  if (FPREG_SUPPLIES(regno))
	    ia64_fbsd_regcache_supply (regcache, regno, fpregs);
	}
    }
  else
    if (FPREG_SUPPLIES(regno))
      ia64_fbsd_regcache_supply (regcache, regno, fpregs);
}

static struct regset gregset = { NULL, ia64_fbsd_supply_gregset };
static struct regset fpregset = { NULL, ia64_fbsd_supply_fpregset };

static const struct regset *
ia64_fbsd_regset_from_core_section (struct gdbarch *gdbarch,
				    const char *sect_name, size_t sect_size)
{
  if (strcmp (sect_name, ".reg") == 0)
    return (&gregset);
  if (strcmp (sect_name, ".reg2") == 0)
    return (&fpregset);
  return (NULL);
}

static int
ia64_fbsd_pc_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  uint64_t gwpage = 5ULL << 61;
  return (pc >= gwpage && pc < (gwpage + 8192)) ? 1 : 0;
}

static void
ia64_fbsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  set_gdbarch_pc_in_sigtramp (gdbarch, ia64_fbsd_pc_in_sigtramp);
  set_gdbarch_regset_from_core_section (gdbarch,
                                        ia64_fbsd_regset_from_core_section);
  set_solib_svr4_fetch_link_map_offsets (gdbarch,
					 svr4_lp64_fetch_link_map_offsets);
  tdep->find_global_pointer = ia64_generic_find_global_pointer;
}

void
_initialize_ia64_fbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_ia64, 0ul, GDB_OSABI_FREEBSD_ELF,
                          ia64_fbsd_init_abi);
}
