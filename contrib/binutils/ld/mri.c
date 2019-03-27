/* mri.c -- handle MRI style linker scripts
   Copyright 1991, 1992, 1993, 1994, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2007 Free Software Foundation, Inc.

This file is part of GLD, the Gnu Linker.

GLD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GLD is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GLD; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
02110-1301, USA.

   This bit does the tree decoration when MRI style link scripts
   are parsed.

   Contributed by Steve Chamberlain <sac@cygnus.com>.  */

#include "sysdep.h"
#include "bfd.h"
#include "ld.h"
#include "ldexp.h"
#include "ldlang.h"
#include "ldmisc.h"
#include "mri.h"
#include <ldgram.h>
#include "libiberty.h"

struct section_name_struct {
  struct section_name_struct *next;
  const char *name;
  const char *alias;
  etree_type *vma;
  etree_type *align;
  etree_type *subalign;
  int ok_to_load;
};

static unsigned int symbol_truncate = 10000;
static struct section_name_struct *order;
static struct section_name_struct *only_load;
static struct section_name_struct *address;
static struct section_name_struct *alias;

static struct section_name_struct *alignment;
static struct section_name_struct *subalignment;

static struct section_name_struct **
lookup (const char *name, struct section_name_struct **list)
{
  struct section_name_struct **ptr = list;

  while (*ptr)
    {
      if (strcmp (name, (*ptr)->name) == 0)
	/* If this is a match, delete it, we only keep the last instance
	   of any name.  */
	*ptr = (*ptr)->next;
      else
	ptr = &((*ptr)->next);
    }

  *ptr = xmalloc (sizeof (struct section_name_struct));
  return ptr;
}

static void
mri_add_to_list (struct section_name_struct **list,
		 const char *name,
		 etree_type *vma,
		 const char *zalias,
		 etree_type *align,
		 etree_type *subalign)
{
  struct section_name_struct **ptr = lookup (name, list);

  (*ptr)->name = name;
  (*ptr)->vma = vma;
  (*ptr)->next = NULL;
  (*ptr)->ok_to_load = 0;
  (*ptr)->alias = zalias;
  (*ptr)->align = align;
  (*ptr)->subalign = subalign;
}

void
mri_output_section (const char *name, etree_type *vma)
{
  mri_add_to_list (&address, name, vma, 0, 0, 0);
}

/* If any ABSOLUTE <name> are in the script, only load those files
   marked thus.  */

void
mri_only_load (const char *name)
{
  mri_add_to_list (&only_load, name, 0, 0, 0, 0);
}

void
mri_base (etree_type *exp)
{
  base = exp;
}

static int done_tree = 0;

void
mri_draw_tree (void)
{
  if (done_tree)
    return;

  /* Now build the statements for the ldlang machine.  */

  /* Attach the addresses of any which have addresses,
     and add the ones not mentioned.  */
  if (address != NULL)
    {
      struct section_name_struct *alist;
      struct section_name_struct *olist;

      if (order == NULL)
	order = address;

      for (alist = address;
	   alist != NULL;
	   alist = alist->next)
	{
	  int done = 0;

	  for (olist = order; done == 0 && olist != NULL; olist = olist->next)
	    {
	      if (strcmp (alist->name, olist->name) == 0)
		{
		  olist->vma = alist->vma;
		  done = 1;
		}
	    }

	  if (!done)
	    {
	      /* Add this onto end of order list.  */
	      mri_add_to_list (&order, alist->name, alist->vma, 0, 0, 0);
	    }
	}
    }

  /* If we're only supposed to load a subset of them in, then prune
     the list.  */
  if (only_load != NULL)
    {
      struct section_name_struct *ptr1;
      struct section_name_struct *ptr2;

      if (order == NULL)
	order = only_load;

      /* See if this name is in the list, if it is then we can load it.  */
      for (ptr1 = only_load; ptr1; ptr1 = ptr1->next)
	for (ptr2 = order; ptr2; ptr2 = ptr2->next)
	  if (strcmp (ptr2->name, ptr1->name) == 0)
	    ptr2->ok_to_load = 1;
    }
  else
    {
      /* No only load list, so everything is ok to load.  */
      struct section_name_struct *ptr;

      for (ptr = order; ptr; ptr = ptr->next)
	ptr->ok_to_load = 1;
    }

  /* Create the order of sections to load.  */
  if (order != NULL)
    {
      /* Been told to output the sections in a certain order.  */
      struct section_name_struct *p = order;

      while (p)
	{
	  struct section_name_struct *aptr;
	  etree_type *align = 0;
	  etree_type *subalign = 0;
	  struct wildcard_list *tmp;

	  /* See if an alignment has been specified.  */
	  for (aptr = alignment; aptr; aptr = aptr->next)
	    if (strcmp (aptr->name, p->name) == 0)
	      align = aptr->align;

	  for (aptr = subalignment; aptr; aptr = aptr->next)
	    if (strcmp (aptr->name, p->name) == 0)
	      subalign = aptr->subalign;

	  if (base == 0)
	    base = p->vma ? p->vma : exp_nameop (NAME, ".");

	  lang_enter_output_section_statement (p->name, base,
					       p->ok_to_load ? 0 : noload_section,
					       align, subalign, NULL, 0);
	  base = 0;
	  tmp = xmalloc (sizeof *tmp);
	  tmp->next = NULL;
	  tmp->spec.name = p->name;
	  tmp->spec.exclude_name_list = NULL;
	  tmp->spec.sorted = none;
	  lang_add_wild (NULL, tmp, FALSE);

	  /* If there is an alias for this section, add it too.  */
	  for (aptr = alias; aptr; aptr = aptr->next)
	    if (strcmp (aptr->alias, p->name) == 0)
	      {
		tmp = xmalloc (sizeof *tmp);
		tmp->next = NULL;
		tmp->spec.name = aptr->name;
		tmp->spec.exclude_name_list = NULL;
		tmp->spec.sorted = none;
		lang_add_wild (NULL, tmp, FALSE);
	      }

	  lang_leave_output_section_statement (0, "*default*", NULL, NULL);

	  p = p->next;
	}
    }

  done_tree = 1;
}

void
mri_load (const char *name)
{
  base = 0;
  lang_add_input_file (name, lang_input_file_is_file_enum, NULL);
}

void
mri_order (const char *name)
{
  mri_add_to_list (&order, name, 0, 0, 0, 0);
}

void
mri_alias (const char *want, const char *is, int isn)
{
  if (!is)
    {
      char buf[20];

      /* Some sections are digits.  */
      sprintf (buf, "%d", isn);

      is = xstrdup (buf);

      if (is == NULL)
	abort ();
    }

  mri_add_to_list (&alias, is, 0, want, 0, 0);
}

void
mri_name (const char *name)
{
  lang_add_output (name, 1);
}

void
mri_format (const char *name)
{
  if (strcmp (name, "S") == 0)
    lang_add_output_format ("srec", NULL, NULL, 1);

  else if (strcmp (name, "IEEE") == 0)
    lang_add_output_format ("ieee", NULL, NULL, 1);

  else if (strcmp (name, "COFF") == 0)
    lang_add_output_format ("coff-m68k", NULL, NULL, 1);

  else
    einfo (_("%P%F: unknown format type %s\n"), name);
}

void
mri_public (const char *name, etree_type *exp)
{
  lang_add_assignment (exp_assop ('=', name, exp));
}

void
mri_align (const char *name, etree_type *exp)
{
  mri_add_to_list (&alignment, name, 0, 0, exp, 0);
}

void
mri_alignmod (const char *name, etree_type *exp)
{
  mri_add_to_list (&subalignment, name, 0, 0, 0, exp);
}

void
mri_truncate (unsigned int exp)
{
  symbol_truncate = exp;
}
