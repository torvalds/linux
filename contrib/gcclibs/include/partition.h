/* List implementation of a partition of consecutive integers.
   Copyright (C) 2000, 2001, 2002 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* This package implements a partition of consecutive integers.  The
   elements are partitioned into classes.  Each class is represented
   by one of its elements, the canonical element, which is chosen
   arbitrarily from elements in the class.  The principal operations
   on a partition are FIND, which takes an element, determines its
   class, and returns the canonical element for that class, and UNION,
   which unites the two classes that contain two given elements into a
   single class.

   The list implementation used here provides constant-time finds.  By
   storing the size of each class with the class's canonical element,
   it is able to perform unions over all the classes in the partition
   in O (N log N) time.  */

#ifndef _PARTITION_H
#define _PARTITION_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "ansidecl.h"
#include <stdio.h>

struct partition_elem
{
  /* The canonical element that represents the class containing this
     element.  */
  int class_element;
  /* The next element in this class.  Elements in each class form a
     circular list.  */
  struct partition_elem* next;
  /* The number of elements in this class.  Valid only if this is the
     canonical element for its class.  */
  unsigned class_count;
};

typedef struct partition_def 
{
  /* The number of elements in this partition.  */
  int num_elements;
  /* The elements in the partition.  */
  struct partition_elem elements[1];
} *partition;

extern partition partition_new (int);
extern void partition_delete (partition);
extern int partition_union (partition, int, int);
extern void partition_print (partition,	FILE*);

/* Returns the canonical element corresponding to the class containing
   ELEMENT__ in PARTITION__.  */

#define partition_find(partition__, element__) \
    ((partition__)->elements[(element__)].class_element)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PARTITION_H */
