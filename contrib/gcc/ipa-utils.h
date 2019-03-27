/* Utilities for ipa analysis.
   Copyright (C) 2004-2005 Free Software Foundation, Inc.
   Contributed by Kenneth Zadeck <zadeck@naturalbridge.com>

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

#ifndef GCC_IPA_UTILS_H
#define GCC_IPA_UTILS_H
#include "tree.h"
#include "cgraph.h"

/* Used for parsing attributes of asm code.  */
extern tree memory_identifier_string;

struct ipa_dfs_info {
  int dfn_number;
  int low_link;
  bool new;
  bool on_stack;
  struct cgraph_node* next_cycle;
  PTR aux;
};



/* In ipa-utils.c  */
void ipa_utils_print_order (FILE*, const char *, struct cgraph_node**, int);
int ipa_utils_reduced_inorder (struct cgraph_node **, bool, bool);
tree get_base_var (tree);

 
#endif  /* GCC_IPA_UTILS_H  */


