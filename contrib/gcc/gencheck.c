/* Generate check macros for tree codes.
   Copyright (C) 1998, 1999, 2000, 2002, 2003, 2004
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#define DEFTREECODE(SYM, NAME, TYPE, LEN) #SYM,

static const char *const tree_codes[] = {
#include "tree.def"
#include "c-common.def"
#include "gencheck.h"
(char*) 0
};

static void usage (void);

static void
usage (void)
{
  fputs ("Usage: gencheck\n", stderr);
}

int
main (int argc, char ** ARG_UNUSED (argv))
{
  int i, j;

  switch (argc)
    {
    case 1:
      break;

    default:
      usage ();
      return (1);
    }

  puts ("/* This file is generated using gencheck. Do not edit. */\n");
  puts ("#ifndef GCC_TREE_CHECK_H");
  puts ("#define GCC_TREE_CHECK_H\n");

  /* Print macros for checks based on each of the tree code names.  However,
     since we include the tree nodes from all languages, we must check
     for duplicate names to avoid defining the same macro twice.  */
  for (i = 0; tree_codes[i]; i++)
    {
      for (j = 0; j < i; j++)
	if (strcmp (tree_codes[i], tree_codes[j]) == 0)
	  break;

      if (i == j)
	printf ("#define %s_CHECK(t)\tTREE_CHECK (t, %s)\n",
		tree_codes[i], tree_codes[i]);
    }

  puts ("\n#endif /* GCC_TREE_CHECK_H */");
  return 0;
}
