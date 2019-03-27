/* ternary.h - Ternary Search Trees
   Copyright 2001 Free Software Foundation, Inc.

   Contributed by Daniel Berlin (dan@cgsoftware.com)


   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.  */
#ifndef TERNARY_H_
#define TERNARY_H_
/* Ternary search trees */

typedef struct ternary_node_def *ternary_tree;

typedef struct ternary_node_def
{
  char splitchar;
  ternary_tree lokid;
  ternary_tree eqkid;
  ternary_tree hikid;
}
ternary_node;

/* Insert string S into tree P, associating it with DATA. 
   Return the data in the tree associated with the string if it's
   already there, and replace is 0.
   Otherwise, replaces if it it exists, inserts if it doesn't, and
   returns the data you passed in. */
PTR ternary_insert PARAMS ((ternary_tree *p, const char *s,
			    PTR data, int replace));

/* Delete the ternary search tree rooted at P. 
   Does NOT delete the data you associated with the strings. */
void ternary_cleanup PARAMS ((ternary_tree p));

/* Search the ternary tree for string S, returning the data associated
   with it if found. */
PTR ternary_search PARAMS ((const ternary_node *p, const char *s));
#endif
