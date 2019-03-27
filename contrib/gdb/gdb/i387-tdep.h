/* Target-dependent code for the i387.

   Copyright 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

#ifndef I387_TDEP_H
#define I387_TDEP_H

struct gdbarch;
struct frame_info;
struct regcache;
struct type;
struct ui_file;

/* Because the number of general-purpose registers is different for
   AMD64, the floating-point registers and SSE registers get shifted.
   The following definitions are intended to help writing code that
   needs the register numbers of floating-point registers and SSE
   registers.  In order to use these, one should provide a definition
   for I387_ST0_REGNUM, and possibly I387_NUM_XMM_REGS, preferably by
   using a local "#define" in the body of the function that uses this.
   Please "#undef" them before the end of the function.  */

#define I387_FCTRL_REGNUM	(I387_ST0_REGNUM + 8)
#define I387_FSTAT_REGNUM	(I387_FCTRL_REGNUM + 1)
#define I387_FTAG_REGNUM	(I387_FCTRL_REGNUM + 2)
#define I387_FISEG_REGNUM	(I387_FCTRL_REGNUM + 3)
#define I387_FIOFF_REGNUM	(I387_FCTRL_REGNUM + 4)
#define I387_FOSEG_REGNUM	(I387_FCTRL_REGNUM + 5)
#define I387_FOOFF_REGNUM	(I387_FCTRL_REGNUM + 6)
#define I387_FOP_REGNUM		(I387_FCTRL_REGNUM + 7)
#define I387_XMM0_REGNUM	(I387_ST0_REGNUM + 16)
#define I387_MXCSR_REGNUM	(I387_XMM0_REGNUM + I387_NUM_XMM_REGS)


/* Print out the i387 floating point state.  */

extern void i387_print_float_info (struct gdbarch *gdbarch,
				   struct ui_file *file,
				   struct frame_info *frame,
				   const char *args);

/* Read a value of type TYPE from register REGNUM in frame FRAME, and
   return its contents in TO.  */

extern void i387_register_to_value (struct frame_info *frame, int regnum,
				    struct type *type, void *to);

/* Write the contents FROM of a value of type TYPE into register
   REGNUM in frame FRAME.  */

extern void i387_value_to_register (struct frame_info *frame, int regnum,
				    struct type *type, const void *from);


/* Size of the memory area use by the 'fsave' and 'fxsave'
   instructions.  */
#define I387_SIZEOF_FSAVE	108
#define I387_SIZEOF_FXSAVE	512

/* Fill register REGNUM in REGCACHE with the appropriate value from
   *FSAVE.  This function masks off any of the reserved bits in
   *FSAVE.  */

extern void i387_supply_fsave (struct regcache *regcache, int regnum,
			       const void *fsave);

/* Fill register REGNUM (if it is a floating-point register) in *FSAVE
   with the value in GDB's register cache.  If REGNUM is -1, do this
   for all registers.  This function doesn't touch any of the reserved
   bits in *FSAVE.  */

extern void i387_fill_fsave (void *fsave, int regnum);

/* Fill register REGNUM in REGCACHE with the appropriate
   floating-point or SSE register value from *FXSAVE.  This function
   masks off any of the reserved bits in *FXSAVE.  */

extern void i387_supply_fxsave (struct regcache *regcache, int regnum,
				const void *fxsave);

/* Fill register REGNUM (if it is a floating-point or SSE register) in
   *FXSAVE with the value from REGCACHE.  If REGNUM is -1, do this for
   all registers.  This function doesn't touch any of the reserved
   bits in *FXSAVE.  */

extern void i387_collect_fxsave (const struct regcache *regcache, int regnum,
				 void *fxsave);

/* Fill register REGNUM (if it is a floating-point or SSE register) in
   *FXSAVE with the value in GDB's register cache.  If REGNUM is -1, do
   this for all registers.  This function doesn't touch any of the
   reserved bits in *FXSAVE.  */

extern void i387_fill_fxsave (void *fxsave, int regnum);

/* Prepare the FPU stack in REGCACHE for a function return.  */

extern void i387_return_value (struct gdbarch *gdbarch,
			       struct regcache *regcache);

#endif /* i387-tdep.h */
