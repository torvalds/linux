/* Native-dependent code for SVR4 Unix running on i386's.
   Copyright 1988, 1989, 1991, 1992, 1996, 1997, 1998, 1999, 2000,
   2001, 2002
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
#include "value.h"
#include "inferior.h"
#include "regcache.h"

#ifdef HAVE_SYS_REG_H
#include <sys/reg.h>
#endif

#include "i386-tdep.h"
#include "i387-tdep.h"

#ifdef HAVE_SYS_PROCFS_H

#include <sys/procfs.h>

/* Prototypes for supply_gregset etc. */
#include "gregset.h"

/* The `/proc' interface divides the target machine's register set up
   into two different sets, the general purpose register set (gregset)
   and the floating-point register set (fpregset).  For each set,
   there is an ioctl to get the current register set and another ioctl
   to set the current values.

   The actual structure passed through the ioctl interface is, of
   course, naturally machine dependent, and is different for each set
   of registers.  For the i386 for example, the general-purpose
   register set is typically defined by:

   typedef int gregset_t[19];           (in <sys/regset.h>)

   #define GS   0                       (in <sys/reg.h>)
   #define FS   1
   ...
   #define UESP 17
   #define SS   18

   and the floating-point set by:

   typedef struct fpregset   {
           union {
                   struct fpchip_state            // fp extension state //
                   {
                           int     state[27];     // 287/387 saved state //
                           int     status;        // status word saved at //
                                                  // exception //
                   } fpchip_state;
                   struct fp_emul_space           // for emulators //
                   {
                           char    fp_emul[246];
                           char    fp_epad[2];
                   } fp_emul_space;
                   int     f_fpregs[62];          // union of the above //
           } fp_reg_set;
           long            f_wregs[33];           // saved weitek state //
   } fpregset_t;

   Incidentally fpchip_state contains the FPU state in the same format
   as used by the "fsave" instruction, and that's the only thing we
   support here.  I don't know how the emulator stores it state.  The
   Weitek stuff definitely isn't supported.

   The routines defined here, provide the packing and unpacking of
   gregset_t and fpregset_t formatted data.  */

#ifdef HAVE_GREGSET_T

/* Mapping between the general-purpose registers in `/proc'
   format and GDB's register array layout.  */
static int regmap[] =
{
  EAX, ECX, EDX, EBX,
  UESP, EBP, ESI, EDI,
  EIP, EFL, CS, SS,
  DS, ES, FS, GS,
};

/* Fill GDB's register array with the general-purpose register values
   in *GREGSETP.  */

void
supply_gregset (gregset_t *gregsetp)
{
  greg_t *regp = (greg_t *) gregsetp;
  int i;

  for (i = 0; i < I386_NUM_GREGS; i++)
    supply_register (i, (char *) (regp + regmap[i]));
}

/* Fill register REGNO (if it is a general-purpose register) in
   *GREGSETPS with the value in GDB's register array.  If REGNO is -1,
   do this for all registers.  */

void
fill_gregset (gregset_t *gregsetp, int regno)
{
  greg_t *regp = (greg_t *) gregsetp;
  int i;

  for (i = 0; i < I386_NUM_GREGS; i++)
    if (regno == -1 || regno == i)
      regcache_collect (i, regp + regmap[i]);
}

#endif /* HAVE_GREGSET_T */

#ifdef HAVE_FPREGSET_T

/* Fill GDB's register array with the floating-point register values in
   *FPREGSETP.  */

void
supply_fpregset (fpregset_t *fpregsetp)
{
  if (FP0_REGNUM == 0)
    return;

  i387_supply_fsave (current_regcache, -1, fpregsetp);
}

/* Fill register REGNO (if it is a floating-point register) in
   *FPREGSETP with the value in GDB's register array.  If REGNO is -1,
   do this for all registers.  */

void
fill_fpregset (fpregset_t *fpregsetp, int regno)
{
  if (FP0_REGNUM == 0)
    return;

  i387_fill_fsave ((char *) fpregsetp, regno);
}

#endif /* HAVE_FPREGSET_T */

#endif /* HAVE_SYS_PROCFS_H */
