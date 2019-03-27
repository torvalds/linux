/* Per-frame user registers, for GDB, the GNU debugger.

   Copyright 2002, 2003 Free Software Foundation, Inc.

   Contributed by Red Hat.

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

#ifndef USER_REGS_H
#define USER_REGS_H

/* Implement both builtin, and architecture specific, per-frame user
   visible registers.

   Builtin registers apply to all architectures, where as architecture
   specific registers are present when the architecture is selected.

   These registers are assigned register numbers outside the
   architecture's register range [0 .. NUM_REGS + NUM_PSEUDO_REGS).
   Their values should be constructed using per-frame information.  */

/* TODO: cagney/2003-06-27: Need to think more about how these
   registers are added, read, and modified.  At present they are kind
   of assumed to be read-only.  Should it, for instance, return a
   register descriptor that contains all the relvent access methods.  */

struct frame_info;
struct gdbarch;

/* Given an architecture, map a user visible register name onto its
   index.  */

extern int user_reg_map_name_to_regnum (struct gdbarch *gdbarch,
					const char *str, int len);

extern const char *user_reg_map_regnum_to_name (struct gdbarch *gdbarch,
						int regnum);

/* Return the value of the frame register in the specified frame.

   Note; These methods return a "struct value" instead of the raw
   bytes as, at the time the register is being added, the type needed
   to describe the register has not bee initialized.  */

typedef struct value *(user_reg_read_ftype) (struct frame_info *frame);
extern struct value *value_of_user_reg (int regnum, struct frame_info *frame);

/* Add a builtin register (present in all architectures).  */
extern void user_reg_add_builtin (const char *name,
				  user_reg_read_ftype *read);

/* Add a per-architecture frame register.  */
extern void user_reg_add (struct gdbarch *gdbarch, const char *name, 
			  user_reg_read_ftype *read);

#endif
