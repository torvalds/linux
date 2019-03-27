/* DPX2 host interface.
   Copyright 1988, 1989, 1991, 1993, 1995, 2000
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
#include "gdbcore.h"

#include "gdb_string.h"
#include <sys/types.h>
#include <sys/param.h>
#include <sys/dir.h>
#include <signal.h>
#include <sys/user.h>
#include <sys/reg.h>
#include <sys/utsname.h>


/* This table must line up with REGISTER_NAME in "m68k-tdep.c".  */
/* symbols like 'A0' come from <sys/reg.h> */
static int regmap[] =
{
  R0, R1, R2, R3, R4, R5, R6, R7,
  A0, A1, A2, A3, A4, A5, A6, SP,
  PS, PC,
  FP0, FP1, FP2, FP3, FP4, FP5, FP6, FP7,
  FP_CR, FP_SR, FP_IAR
};

/* blockend is the value of u.u_ar0, and points to the
 * place where D0 is stored
 */

int
dpx2_register_u_addr (int blockend, int regnum)
{
  if (regnum < FP0_REGNUM)
    return (blockend + 4 * regmap[regnum]);
  else
    return (int) &(((struct user *) 0)->u_fpstate[regmap[regnum]]);
}

/* This is the amount to subtract from u.u_ar0
   to get the offset in the core file of the register values.
   Unfortunately this is not provided in the system header files.
   To make matters worse, this value also differs between
   the dpx/2200 and dpx/2300 models and nlist is not available on the dpx2.
   We use utsname() to decide on which model we are running.
   FIXME: This breaks cross examination of core files (it would not be hard
   to check whether u.u_ar0 is between 0x7fff5000 and 0x7fffc000 and if so
   use 0x7fff5000 and if not use 0x7fffc000.  FIXME).  */

#define KERNEL_U_ADDR_200 0x7fff5000
#define KERNEL_U_ADDR_300 0x7fffc000

CORE_ADDR kernel_u_addr;

void
_initialize_dpx2_nat (void)
{
  struct utsname uts;

  if (uname (&uts) == 0 && strcmp (uts.machine, "DPX/2200") == 0)
    kernel_u_addr = KERNEL_U_ADDR_200;
  else
    kernel_u_addr = KERNEL_U_ADDR_300;
}
