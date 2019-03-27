/* ldctor.h - linker constructor support
   Copyright 1991, 1992, 1993, 1994, 1995, 1998, 2000, 2002, 2003
   Free Software Foundation, Inc.

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
02110-1301, USA.  */

#ifndef LDCTOR_H
#define LDCTOR_H

/* List of statements needed to handle constructors */
extern lang_statement_list_type constructor_list;

/* Whether the constructors should be sorted.  Note that this is
   global for the entire link; we assume that there is only a single
   CONSTRUCTORS command in the linker script.  */
extern bfd_boolean constructors_sorted;

/* We keep a list of these structures for each set we build.  */

struct set_info {
  struct set_info *next;		/* Next set.  */
  struct bfd_link_hash_entry *h;	/* Hash table entry.  */
  bfd_reloc_code_real_type reloc;	/* Reloc to use for an entry.  */
  size_t count;				/* Number of elements.  */
  struct set_element *elements;		/* Elements in set.  */
};

struct set_element {
  struct set_element *next;		/* Next element.  */
  const char *name;			/* Name in set (may be NULL).  */
  asection *section;			/* Section of value in set.  */
  bfd_vma value;			/* Value in set.  */
};

/* The sets we have seen.  */

extern struct set_info *sets;

extern void ldctor_add_set_entry
  (struct bfd_link_hash_entry *, bfd_reloc_code_real_type, const char *,
   asection *, bfd_vma);
extern void ldctor_build_sets
  (void);

#endif
