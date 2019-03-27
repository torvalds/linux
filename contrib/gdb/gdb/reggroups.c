/* Register groupings for GDB, the GNU debugger.

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

#include "defs.h"
#include "reggroups.h"
#include "gdbtypes.h"
#include "gdb_assert.h"
#include "regcache.h"
#include "command.h"
#include "gdbcmd.h"		/* For maintenanceprintlist.  */

/* Individual register groups.  */

struct reggroup
{
  const char *name;
  enum reggroup_type type;
};

struct reggroup *
reggroup_new (const char *name, enum reggroup_type type)
{
  struct reggroup *group = XMALLOC (struct reggroup);
  group->name = name;
  group->type = type;
  return group;
}

/* Register group attributes.  */

const char *
reggroup_name (struct reggroup *group)
{
  return group->name;
}

enum reggroup_type
reggroup_type (struct reggroup *group)
{
  return group->type;
}

/* A linked list of groups for the given architecture.  */

struct reggroup_el
{
  struct reggroup *group;
  struct reggroup_el *next;
};

struct reggroups
{
  struct reggroup_el *first;
  struct reggroup_el **last;
};

static struct gdbarch_data *reggroups_data;

static void *
reggroups_init (struct gdbarch *gdbarch)
{
  struct reggroups *groups = GDBARCH_OBSTACK_ZALLOC (gdbarch,
						     struct reggroups);
  groups->last = &groups->first;
  return groups;
}

/* Add a register group (with attribute values) to the pre-defined
   list.  */

static void
add_group (struct reggroups *groups, struct reggroup *group,
	   struct reggroup_el *el)
{
  gdb_assert (group != NULL);
  el->group = group;
  el->next = NULL;
  (*groups->last) = el;
  groups->last = &el->next;
}

void
reggroup_add (struct gdbarch *gdbarch, struct reggroup *group)
{
  struct reggroups *groups = gdbarch_data (gdbarch, reggroups_data);

  if (groups == NULL)
    {
      /* ULGH, called during architecture initialization.  Patch
         things up.  */
      groups = reggroups_init (gdbarch);
      set_gdbarch_data (gdbarch, reggroups_data, groups);
    }
  add_group (groups, group,
	     GDBARCH_OBSTACK_ZALLOC (gdbarch, struct reggroup_el));
}

/* The default register groups for an architecture.  */

static struct reggroups default_groups = { NULL, &default_groups.first };

/* A register group iterator.  */

struct reggroup *
reggroup_next (struct gdbarch *gdbarch, struct reggroup *last)
{
  struct reggroups *groups;
  struct reggroup_el *el;

  /* Don't allow this function to be called during architecture
     creation.  If there are no groups, use the default groups list.  */
  groups = gdbarch_data (gdbarch, reggroups_data);
  gdb_assert (groups != NULL);
  if (groups->first == NULL)
    groups = &default_groups;

  /* Return the first/next reggroup.  */
  if (last == NULL)
    return groups->first->group;
  for (el = groups->first; el != NULL; el = el->next)
    {
      if (el->group == last)
	{
	  if (el->next != NULL)
	    return el->next->group;
	  else
	    return NULL;
	}
    }
  return NULL;
}

/* Is REGNUM a member of REGGROUP?  */
int
default_register_reggroup_p (struct gdbarch *gdbarch, int regnum,
			     struct reggroup *group)
{
  int vector_p;
  int float_p;
  int raw_p;

  if (REGISTER_NAME (regnum) == NULL
      || *REGISTER_NAME (regnum) == '\0')
    return 0;
  if (group == all_reggroup)
    return 1;
  vector_p = TYPE_VECTOR (register_type (gdbarch, regnum));
  float_p = TYPE_CODE (register_type (gdbarch, regnum)) == TYPE_CODE_FLT;
  /* FIXME: cagney/2003-04-13: Can't yet use gdbarch_num_regs
     (gdbarch), as not all architectures are multi-arch.  */
  raw_p = regnum < NUM_REGS;
  if (group == float_reggroup)
    return float_p;
  if (group == vector_reggroup)
    return vector_p;
  if (group == general_reggroup)
    return (!vector_p && !float_p);
  if (group == save_reggroup || group == restore_reggroup)
    return raw_p;
  return 0;   
}

/* Dump out a table of register groups for the current architecture.  */

static void
reggroups_dump (struct gdbarch *gdbarch, struct ui_file *file)
{
  struct reggroup *group = NULL;

  do
    {
      /* Group name.  */
      {
	const char *name;
	if (group == NULL)
	  name = "Group";
	else
	  name = reggroup_name (group);
	fprintf_unfiltered (file, " %-10s", name);
      }
      
      /* Group type.  */
      {
	const char *type;
	if (group == NULL)
	  type = "Type";
	else
	  {
	    switch (reggroup_type (group))
	      {
	      case USER_REGGROUP:
		type = "user";
		break;
	      case INTERNAL_REGGROUP:
		type = "internal";
		break;
	      default:
		internal_error (__FILE__, __LINE__, "bad switch");
	      }
	  }
	fprintf_unfiltered (file, " %-10s", type);
      }

      /* Note: If you change this, be sure to also update the
         documentation.  */
      
      fprintf_unfiltered (file, "\n");

      group = reggroup_next (gdbarch, group);
    }
  while (group != NULL);
}

static void
maintenance_print_reggroups (char *args, int from_tty)
{
  if (args == NULL)
    reggroups_dump (current_gdbarch, gdb_stdout);
  else
    {
      struct ui_file *file = gdb_fopen (args, "w");
      if (file == NULL)
	perror_with_name ("maintenance print reggroups");
      reggroups_dump (current_gdbarch, file);    
      ui_file_delete (file);
    }
}

/* Pre-defined register groups.  */
static struct reggroup general_group = { "general", USER_REGGROUP };
static struct reggroup float_group = { "float", USER_REGGROUP };
static struct reggroup system_group = { "system", USER_REGGROUP };
static struct reggroup vector_group = { "vector", USER_REGGROUP };
static struct reggroup all_group = { "all", USER_REGGROUP };
static struct reggroup save_group = { "save", INTERNAL_REGGROUP };
static struct reggroup restore_group = { "restore", INTERNAL_REGGROUP };

struct reggroup *const general_reggroup = &general_group;
struct reggroup *const float_reggroup = &float_group;
struct reggroup *const system_reggroup = &system_group;
struct reggroup *const vector_reggroup = &vector_group;
struct reggroup *const all_reggroup = &all_group;
struct reggroup *const save_reggroup = &save_group;
struct reggroup *const restore_reggroup = &restore_group;

extern initialize_file_ftype _initialize_reggroup; /* -Wmissing-prototypes */

void
_initialize_reggroup (void)
{
  reggroups_data = register_gdbarch_data (reggroups_init);

  /* The pre-defined list of groups.  */
  add_group (&default_groups, general_reggroup, XMALLOC (struct reggroup_el));
  add_group (&default_groups, float_reggroup, XMALLOC (struct reggroup_el));
  add_group (&default_groups, system_reggroup, XMALLOC (struct reggroup_el));
  add_group (&default_groups, vector_reggroup, XMALLOC (struct reggroup_el));
  add_group (&default_groups, all_reggroup, XMALLOC (struct reggroup_el));
  add_group (&default_groups, save_reggroup, XMALLOC (struct reggroup_el));
  add_group (&default_groups, restore_reggroup, XMALLOC (struct reggroup_el));

  add_cmd ("reggroups", class_maintenance,
	   maintenance_print_reggroups, "\
Print the internal register group names.\n\
Takes an optional file parameter.",
	   &maintenanceprintlist);

}
