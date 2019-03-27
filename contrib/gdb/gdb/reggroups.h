/* Register groupings for GDB, the GNU debugger.

   Copyright 2002 Free Software Foundation, Inc.

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

#ifndef REGGROUPS_H
#define REGGROUPS_H

struct gdbarch;
struct reggroup;

enum reggroup_type { USER_REGGROUP, INTERNAL_REGGROUP };

/* Pre-defined, user visible, register groups.  */
extern struct reggroup *const general_reggroup;
extern struct reggroup *const float_reggroup;
extern struct reggroup *const system_reggroup;
extern struct reggroup *const vector_reggroup;
extern struct reggroup *const all_reggroup;

/* Pre-defined, internal, register groups.  */
extern struct reggroup *const save_reggroup;
extern struct reggroup *const restore_reggroup;

/* Create a new local register group.  */
extern struct reggroup *reggroup_new (const char *name,
				      enum reggroup_type type);

/* Add a register group (with attribute values) to the pre-defined list.  */
extern void reggroup_add (struct gdbarch *gdbarch, struct reggroup *group);

/* Register group attributes.  */
extern const char *reggroup_name (struct reggroup *reggroup);
extern enum reggroup_type reggroup_type (struct reggroup *reggroup);

/* Interator for the architecture's register groups.  Pass in NULL,
   returns the first group.  Pass in a group, returns the next group,
   or NULL when the last group is reached.  */
extern struct reggroup *reggroup_next (struct gdbarch *gdbarch,
				       struct reggroup *last);

/* Is REGNUM a member of REGGROUP?  */
extern int default_register_reggroup_p (struct gdbarch *gdbarch, int regnum,
					struct reggroup *reggroup);

#endif
