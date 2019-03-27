/* Language-dependent hooks for C++.
   Copyright 2001, 2002, 2004 Free Software Foundation, Inc.
   Contributed by Alexandre Oliva  <aoliva@redhat.com>

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
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "cp-tree.h"
#include "c-common.h"
#include "toplev.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "diagnostic.h"
#include "debug.h"
#include "cp-objcp-common.h"
#include "hashtab.h"

enum c_language_kind c_language = clk_cxx;
static void cp_init_ts (void);

/* Lang hooks common to C++ and ObjC++ are declared in cp/cp-objcp-common.h;
   consequently, there should be very few hooks below.  */

#undef LANG_HOOKS_NAME
#define LANG_HOOKS_NAME "GNU C++"
#undef LANG_HOOKS_INIT
#define LANG_HOOKS_INIT cxx_init
#undef LANG_HOOKS_GENERIC_TYPE_P
#define LANG_HOOKS_GENERIC_TYPE_P class_tmpl_impl_spec_p
#undef LANG_HOOKS_DECL_PRINTABLE_NAME
#define LANG_HOOKS_DECL_PRINTABLE_NAME	cxx_printable_name
#undef LANG_HOOKS_FOLD_OBJ_TYPE_REF
#define LANG_HOOKS_FOLD_OBJ_TYPE_REF cp_fold_obj_type_ref
#undef LANG_HOOKS_INIT_TS
#define LANG_HOOKS_INIT_TS cp_init_ts

/* Each front end provides its own lang hook initializer.  */
const struct lang_hooks lang_hooks = LANG_HOOKS_INITIALIZER;

/* Tree code classes.  */

#define DEFTREECODE(SYM, NAME, TYPE, LENGTH) TYPE,

const enum tree_code_class tree_code_type[] = {
#include "tree.def"
  tcc_exceptional,
#include "c-common.def"
  tcc_exceptional,
#include "cp-tree.def"
};
#undef DEFTREECODE

/* Table indexed by tree code giving number of expression
   operands beyond the fixed part of the node structure.
   Not used for types or decls.  */

#define DEFTREECODE(SYM, NAME, TYPE, LENGTH) LENGTH,

const unsigned char tree_code_length[] = {
#include "tree.def"
  0,
#include "c-common.def"
  0,
#include "cp-tree.def"
};
#undef DEFTREECODE

/* Names of tree components.
   Used for printing out the tree and error messages.  */
#define DEFTREECODE(SYM, NAME, TYPE, LEN) NAME,

const char *const tree_code_name[] = {
#include "tree.def"
  "@@dummy",
#include "c-common.def"
  "@@dummy",
#include "cp-tree.def"
};
#undef DEFTREECODE

/* Lang hook routines common to C++ and ObjC++ appear in cp/cp-objcp-common.c;
   there should be very few routines below.  */

/* The following function does something real, but only in Objective-C++.  */

tree
objcp_tsubst_copy_and_build (tree t ATTRIBUTE_UNUSED,
			     tree args ATTRIBUTE_UNUSED,
			     tsubst_flags_t complain ATTRIBUTE_UNUSED,
			     tree in_decl ATTRIBUTE_UNUSED,
			     bool function_p ATTRIBUTE_UNUSED)
{
  return NULL_TREE;
}


static void
cp_init_ts (void)
{
  tree_contains_struct[NAMESPACE_DECL][TS_DECL_NON_COMMON] = 1;
  tree_contains_struct[USING_DECL][TS_DECL_NON_COMMON] = 1;
  tree_contains_struct[TEMPLATE_DECL][TS_DECL_NON_COMMON] = 1;

  tree_contains_struct[NAMESPACE_DECL][TS_DECL_WITH_VIS] = 1;
  tree_contains_struct[USING_DECL][TS_DECL_WITH_VIS] = 1;
  tree_contains_struct[TEMPLATE_DECL][TS_DECL_WITH_VIS] = 1;

  tree_contains_struct[NAMESPACE_DECL][TS_DECL_WRTL] = 1;
  tree_contains_struct[USING_DECL][TS_DECL_WRTL] = 1;
  tree_contains_struct[TEMPLATE_DECL][TS_DECL_WRTL] = 1;

  tree_contains_struct[NAMESPACE_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[USING_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[TEMPLATE_DECL][TS_DECL_COMMON] = 1;

  tree_contains_struct[NAMESPACE_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[USING_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[TEMPLATE_DECL][TS_DECL_MINIMAL] = 1;

  init_shadowed_var_for_decl ();

}

void
finish_file (void)
{
  cp_finish_file ();
}

#include "gtype-cp.h"
