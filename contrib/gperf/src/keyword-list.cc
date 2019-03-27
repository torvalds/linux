/* Keyword list.

   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>.

   This file is part of GNU GPERF.

   GNU GPERF is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GNU GPERF is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Specification. */
#include "keyword-list.h"

#include <stddef.h>

/* -------------------------- Keyword_List class --------------------------- */

/* Constructor.  */
Keyword_List::Keyword_List (Keyword *car)
  : _cdr (NULL), _car (car)
{
}

/* ------------------------- KeywordExt_List class ------------------------- */

/* Constructor.  */
KeywordExt_List::KeywordExt_List (KeywordExt *car)
  : Keyword_List (car)
{
}

/* ------------------------ Keyword_List functions ------------------------- */

/* Copies a linear list, sharing the list elements.  */
Keyword_List *
copy_list (Keyword_List *list)
{
  Keyword_List *result;
  Keyword_List **lastp = &result;
  while (list != NULL)
    {
      Keyword_List *new_cons = new Keyword_List (list->first());
      *lastp = new_cons;
      lastp = &new_cons->rest();
      list = list->rest();
    }
  *lastp = NULL;
  return result;
}

/* Copies a linear list, sharing the list elements.  */
KeywordExt_List *
copy_list (KeywordExt_List *list)
{
  return static_cast<KeywordExt_List *> (copy_list (static_cast<Keyword_List *> (list)));
}

/* Deletes a linear list, keeping the list elements in memory.  */
void
delete_list (Keyword_List *list)
{
  while (list != NULL)
    {
      Keyword_List *rest = list->rest();
      delete list;
      list = rest;
    }
}

/* Type of a comparison function.  */
typedef bool (*Keyword_Comparison) (Keyword *keyword1, Keyword *keyword2);

/* Merges two sorted lists together to form one sorted list.  */
static Keyword_List *
merge (Keyword_List *list1, Keyword_List *list2, Keyword_Comparison less)
{
  Keyword_List *result;
  Keyword_List **resultp = &result;
  for (;;)
    {
      if (!list1)
        {
          *resultp = list2;
          break;
        }
      if (!list2)
        {
          *resultp = list1;
          break;
        }
      if (less (list2->first(), list1->first()))
        {
          *resultp = list2;
          resultp = &list2->rest();
          /* We would have a stable sorting if the next line would read:
             list2 = *resultp;  */
          list2 = list1; list1 = *resultp;
        }
      else
        {
          *resultp = list1;
          resultp = &list1->rest();
          list1 = *resultp;
        }
    }
  return result;
}

/* Sorts a linear list, given a comparison function.
   Note: This uses a variant of mergesort that is *not* a stable sorting
   algorithm.  */
Keyword_List *
mergesort_list (Keyword_List *list, Keyword_Comparison less)
{
  if (list == NULL || list->rest() == NULL)
    /* List of length 0 or 1.  Nothing to do.  */
    return list;
  else
    {
      /* Determine a list node in the middle.  */
      Keyword_List *middle = list;
      for (Keyword_List *temp = list->rest();;)
        {
          temp = temp->rest();
          if (temp == NULL)
            break;
          temp = temp->rest();
          middle = middle->rest();
          if (temp == NULL)
            break;
        }

      /* Cut the list into two halves.
         If the list has n elements, the left half has ceiling(n/2) elements
         and the right half has floor(n/2) elements.  */
      Keyword_List *right_half = middle->rest();
      middle->rest() = NULL;

      /* Sort the two halves, then merge them.  */
      return merge (mergesort_list (list, less),
                    mergesort_list (right_half, less),
                    less);
    }
}

KeywordExt_List *
mergesort_list (KeywordExt_List *list,
                bool (*less) (KeywordExt *keyword1, KeywordExt *keyword2))
{
  return
    static_cast<KeywordExt_List *>
      (mergesort_list (static_cast<Keyword_List *> (list),
                       reinterpret_cast<Keyword_Comparison> (less)));
}


#ifndef __OPTIMIZE__

#define INLINE /* not inline */
#include "keyword-list.icc"
#undef INLINE

#endif /* not defined __OPTIMIZE__ */
