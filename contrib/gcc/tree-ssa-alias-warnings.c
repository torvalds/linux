/* Strict aliasing checks.
   Copyright (C) 2007 Free Software Foundation, Inc.
   Contributed by Silvius Rus <rus@google.com>.

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
#include "alloc-pool.h"
#include "tree.h"
#include "tree-dump.h"
#include "tree-flow.h"
#include "params.h"
#include "function.h"
#include "expr.h"
#include "toplev.h"
#include "diagnostic.h"
#include "tree-ssa-structalias.h"
#include "tree-ssa-propagate.h"
#include "langhooks.h"

/* Module to issue a warning when a program uses data through a type
   different from the type through which the data were defined.
   Implements -Wstrict-aliasing and -Wstrict-aliasing=n.
   These checks only happen when -fstrict-aliasing is present.

   The idea is to use the compiler to identify occurrences of nonstandard
   aliasing, and report them to programmers.  Programs free of such aliasing
   are more portable, maintainable, and can usually be optimized better.

   The current, as of April 2007, C and C++ language standards forbid
   accessing data of type A through an lvalue of another type B,
   with certain exceptions. See the C Standard ISO/IEC 9899:1999,
   section 6.5, paragraph 7, and the C++ Standard ISO/IEC 14882:1998,
   section 3.10, paragraph 15.

   Example 1:*a is used as int but was defined as a float, *b.
        int* a = ...;
        float* b = reinterpret_cast<float*> (a);
        *b = 2.0;
        return *a

   Unfortunately, the problem is in general undecidable if we take into
   account arithmetic expressions such as array indices or pointer arithmetic.
   (It is at least as hard as Peano arithmetic decidability.)
   Even ignoring arithmetic, the problem is still NP-hard, because it is
   at least as hard as flow-insensitive may-alias analysis, which was proved
   NP-hard by Horwitz et al, TOPLAS 1997.

   It is clear that we need to choose some heuristics.
   Unfortunately, various users have different goals which correspond to
   different time budgets so a common approach will not suit all.
   We present the user with three effort/accuracy levels.  By accuracy, we mean
   a common-sense mix of low count of false positives with a
   reasonably low number of false negatives.  We are heavily biased
   towards a low count of false positives.
   The effort (compilation time) is likely to increase with the level.

   -Wstrict-aliasing=1
   ===================
   Most aggressive, least accurate.  Possibly useful when higher levels
   do not warn but -fstrict-aliasing still breaks the code, as
   it has very few false negatives.
   Warn for all bad pointer conversions, even if never dereferenced.
   Implemented in the front end (c-common.c).
   Uses alias_sets_might_conflict to compare types.

   -Wstrict-aliasing=2
   ===================
   Aggressive, not too precise.
   May still have many false positives (not as many as level 1 though),
   and few false negatives (but possibly more than level 1).
   Runs only in the front end. Uses alias_sets_might_conflict to
   compare types. Does not check for pointer dereferences.
   Only warns when an address is taken. Warns about incomplete type punning.

   -Wstrict-aliasing=3 (default)
   ===================
   Should have very few false positives and few false negatives.
   Takes care of the common punn+dereference pattern in the front end:
   *(int*)&some_float.
   Takes care of multiple statement cases in the back end,
   using flow-sensitive points-to information (-O required).
   Uses alias_sets_conflict_p to compare types and only warns
   when the converted pointer is dereferenced.
   Does not warn about incomplete type punning.

   Future improvements can be included by adding higher levels.

   In summary, expression level analysis is performed in the front-end,
   and multiple-statement analysis is performed in the backend.
   The remainder of this discussion is only about the backend analysis.

   This implementation uses flow-sensitive points-to information.
   Flow-sensitivity refers to accesses to the pointer, and not the object
   pointed.  For instance, we do not warn about the following case.

   Example 2.
        int* a = (int*)malloc (...);
        float* b = reinterpret_cast<float*> (a);
        *b = 2.0;
        a = (int*)malloc (...);
        return *a;

   In SSA, it becomes clear that the INT value *A_2 referenced in the
   return statement is not aliased to the FLOAT defined through *B_1.
        int* a_1 = (int*)malloc (...);
        float* b_1 = reinterpret_cast<float*> (a_1);
        *b_1 = 2.0;
        a_2 = (int*)malloc (...);
        return *a_2;


   Algorithm Outline
   =================

   ForEach (ptr, object) in the points-to table
     If (incompatible_types (*ptr, object))
       If (referenced (ptr, current function)
           and referenced (object, current function))
         Issue warning (ptr, object, reference locations)

   The complexity is:
   O (sizeof (points-to table)
      + sizeof (function body) * lookup_time (points-to table))

   Pointer dereference locations are looked up on demand.  The search is
   a single scan of the function body, in which all references to pointers
   and objects in the points-to table are recorded.  However, this dominant
   time factor occurs rarely, only when cross-type aliasing was detected.


   Limitations of the Proposed Implementation
   ==========================================

   1. We do not catch the following case, because -fstrict-aliasing will
      associate different tags with MEM while building points-to information,
      thus before we get to analyze it.
      XXX: this could be solved by either running with -fno-strict-aliasing
      or by recording the points-to information before splitting the orignal
      tag based on type.

   Example 3.
        void* mem = malloc (...);
	int* pi = reinterpret_cast<int*> (mem);
	float* b = reinterpret_cast<float*> (mem);
	*b = 2.0;
	return *pi+1;

   2. We do not check whether the two conflicting (de)references can
      reach each other in the control flow sense.  If we fixed limitation
      1, we would wrongly issue a warning in the following case.

   Example 4.
        void* raw = malloc (...);
        if (...) {
         float* b = reinterpret_cast<float*> (raw);
         *b = 2.0;
         return (int)*b;
        } else {
         int* a = reinterpret_cast<int*> (raw);
         *a = 1;
         return *a;

   3. Only simple types are compared, thus no structures, unions or classes
      are analyzed.  A first attempt to deal with structures introduced much
      complication and has not showed much improvement in preliminary tests,
      so it was left out.

   4. All analysis is intraprocedural.  */


/* Local declarations.  */
static void find_references_in_function (void);



/* Get main type of tree TYPE, stripping array dimensions and qualifiers.  */

static tree
get_main_type (tree type)
{
  while (TREE_CODE (type) == ARRAY_TYPE)
    type = TREE_TYPE (type);
  return TYPE_MAIN_VARIANT (type);
}


/* Get the type of the given object.  If IS_PTR is true, get the type of the
   object pointed to or referenced by OBJECT instead.
   For arrays, return the element type.  Ignore all qualifiers.  */

static tree
get_otype (tree object, bool is_ptr)
{
  tree otype = TREE_TYPE (object);

  if (is_ptr)
    {
      gcc_assert (POINTER_TYPE_P (otype));
      otype = TREE_TYPE (otype);
    }
  return get_main_type (otype);
}


/* Return true if tree TYPE is struct, class or union.  */

static bool
struct_class_union_p (tree type)
{
  return (TREE_CODE (type) == RECORD_TYPE
	  || TREE_CODE (type) == UNION_TYPE
	  || TREE_CODE (type) == QUAL_UNION_TYPE);
}



/* Keep data during a search for an aliasing site.
   RHS = object or pointer aliased.  No LHS is specified because we are only
   looking in the UseDef paths of a given variable, so LHS will always be
   an SSA name of the same variable.
   When IS_RHS_POINTER = true, we are looking for ... = RHS.  Otherwise,
   we are looking for ... = &RHS.
   SITE is the output of a search, non-NULL if the search succeeded.  */

struct alias_match
{
  tree rhs;
  bool is_rhs_pointer;
  tree site;
};


/* Callback for find_alias_site.  Return true if the right hand site
   of STMT matches DATA.  */

static bool
find_alias_site_helper (tree var ATTRIBUTE_UNUSED, tree stmt, void *data)
{
  struct alias_match *match = (struct alias_match *) data;
  tree rhs_pointer = get_rhs (stmt);
  tree to_match = NULL_TREE;

  while (TREE_CODE (rhs_pointer) == NOP_EXPR
         || TREE_CODE (rhs_pointer) == CONVERT_EXPR
         || TREE_CODE (rhs_pointer) == VIEW_CONVERT_EXPR)
    rhs_pointer = TREE_OPERAND (rhs_pointer, 0);

  if (!rhs_pointer)
    /* Not a type conversion.  */
    return false;

  if (TREE_CODE (rhs_pointer) == ADDR_EXPR && !match->is_rhs_pointer)
    to_match = TREE_OPERAND (rhs_pointer, 0);
  else if (POINTER_TYPE_P (rhs_pointer) && match->is_rhs_pointer)
    to_match = rhs_pointer;

  if (to_match != match->rhs)
    /* Type conversion, but not a name match.  */
    return false;

  /* Found it.  */
  match->site = stmt;
  return true;
}


/* Find the statement where OBJECT1 gets aliased to OBJECT2.
   If IS_PTR2 is true, consider OBJECT2 to be the name of a pointer or
   reference rather than the actual aliased object.
   For now, just implement the case where OBJECT1 is an SSA name defined
   by a PHI statement.  */

static tree
find_alias_site (tree object1, bool is_ptr1 ATTRIBUTE_UNUSED,
                 tree object2, bool is_ptr2)
{
  struct alias_match match;

  match.rhs = object2;
  match.is_rhs_pointer = is_ptr2;
  match.site = NULL_TREE;

  if (TREE_CODE (object1) != SSA_NAME)
    return NULL_TREE;

  walk_use_def_chains (object1, find_alias_site_helper, &match, false);
  return match.site;
}


/* Structure to store temporary results when trying to figure out whether
   an object is referenced.  Just its presence in the text is not enough,
   as we may just be taking its address.  */

struct match_info
{
  tree object;
  bool is_ptr;
  /* The difference between the number of references to OBJECT
     and the number of occurences of &OBJECT.  */
  int found;
};


/* Return the base if EXPR is an SSA name.  Return EXPR otherwise.  */

static tree
get_ssa_base (tree expr)
{
  if (TREE_CODE (expr) == SSA_NAME)
    return SSA_NAME_VAR (expr);
  else
    return expr;
}


/* Record references to objects and pointer dereferences across some piece of
   code.  The number of references is recorded for each item.
   References to an object just to take its address are not counted.
   For instance, if PTR is a pointer and OBJ is an object:
   1. Expression &obj + *ptr will have the following reference match structure:
   ptrs: <ptr, 1>
   objs: <ptr, 1>
   OBJ does not appear as referenced because we just take its address.
   2. Expression ptr + *ptr will have the following reference match structure:
   ptrs: <ptr, 1>
   objs: <ptr, 2>
   PTR shows up twice as an object, but is dereferenced only once.

   The elements of the hash tables are tree_map objects.  */
struct reference_matches
{
  htab_t ptrs;
  htab_t objs;
};


/* Return the match, if any.  Otherwise, return NULL_TREE.  It will
   return NULL_TREE even when a match was found, if the value associated
   to KEY is NULL_TREE.  */

static inline tree
match (htab_t ref_map, tree key)
{
  struct tree_map to_find;
  struct tree_map *found;
  void **slot = NULL;

  to_find.from = key;
  to_find.hash = htab_hash_pointer (key);
  slot = htab_find_slot (ref_map, &to_find, NO_INSERT);

  if (!slot)
    return NULL_TREE;

  found = (struct tree_map *) *slot;
  return found->to;
}


/* Set the entry corresponding to KEY, but only if the entry
   already exists and its value is NULL_TREE.  Otherwise, do nothing.  */

static inline void
maybe_add_match (htab_t ref_map, struct tree_map *key)
{
  struct tree_map *found = htab_find (ref_map, key);

  if (found && !found->to)
    found->to = key->to;
}


/* Add an entry to HT, with key T and value NULL_TREE.  */

static void
add_key (htab_t ht, tree t, alloc_pool references_pool)
{
  void **slot;
  struct tree_map *tp = pool_alloc (references_pool);

  tp->from = t;
  tp->to = NULL_TREE;
  tp->hash = htab_hash_pointer(tp->from);

  slot = htab_find_slot (ht, tp, INSERT);
  *slot = (void *) tp;
}


/* Some memory to keep the objects in the reference table.  */

static alloc_pool ref_table_alloc_pool = NULL;


/* Get some memory to keep the objects in the reference table.  */

static inline alloc_pool
reference_table_alloc_pool (bool build)
{
  if (ref_table_alloc_pool || !build)
    return ref_table_alloc_pool;

  ref_table_alloc_pool =
    create_alloc_pool ("ref_table_alloc_pool", sizeof (struct tree_map), 20);

  return ref_table_alloc_pool;
}


/* Initialize the reference table by adding all pointers in the points-to
   table as keys, and NULL_TREE as associated values.  */

static struct reference_matches *
build_reference_table (void)
{
  unsigned int i;
  struct reference_matches *ref_table = NULL;
  alloc_pool references_pool = reference_table_alloc_pool (true);

  ref_table = XNEW (struct reference_matches);
  ref_table->objs = htab_create (10, tree_map_hash, tree_map_eq, NULL);
  ref_table->ptrs = htab_create (10, tree_map_hash, tree_map_eq, NULL);

  for (i = 1; i < num_ssa_names; i++)
    {
      tree ptr = ssa_name (i);
      struct ptr_info_def *pi;

      if (ptr == NULL_TREE)
	continue;

      pi = SSA_NAME_PTR_INFO (ptr);

      if (!SSA_NAME_IN_FREE_LIST (ptr) && pi && pi->name_mem_tag)
	{
	  /* Add pointer to the interesting dereference list.  */
	  add_key (ref_table->ptrs, ptr, references_pool);

	  /* Add all aliased names to the interesting reference list.  */
	  if (pi->pt_vars)
	    {
	      unsigned ix;
	      bitmap_iterator bi;

	      EXECUTE_IF_SET_IN_BITMAP (pi->pt_vars, 0, ix, bi)
		{
		  tree alias = referenced_var (ix);
		  add_key (ref_table->objs, alias, references_pool);
		}
	    }
	}
    }

  return ref_table;
}


/*  Reference table.  */

static struct reference_matches *ref_table = NULL;


/* Clean up the reference table if allocated.  */

static void
maybe_free_reference_table (void)
{
  if (ref_table)
    {
      htab_delete (ref_table->ptrs);
      htab_delete (ref_table->objs);
      free (ref_table);
      ref_table = NULL;
    }

  if (ref_table_alloc_pool)
    {
      free_alloc_pool (ref_table_alloc_pool);
      ref_table_alloc_pool = NULL;
    }
}


/* Get the reference table.  Initialize it if needed.  */

static inline struct reference_matches *
reference_table (bool build)
{
  if (ref_table || !build)
    return ref_table;

  ref_table = build_reference_table ();
  find_references_in_function ();
  return ref_table;
}


/* Callback for find_references_in_function.
   Check whether *TP is an object reference or pointer dereference for the
   variables given in ((struct match_info*)DATA)->OBJS or
   ((struct match_info*)DATA)->PTRS.  The total number of references
   is stored in the same structures.  */

static tree
find_references_in_tree_helper (tree *tp,
				int *walk_subtrees ATTRIBUTE_UNUSED,
				void *data)
{
  struct tree_map match;
  static int parent_tree_code = ERROR_MARK;

  /* Do not report references just for the purpose of taking an address.
     XXX: we rely on the fact that the tree walk is in preorder
     and that ADDR_EXPR is not a leaf, thus cannot be carried over across
     walks.  */
  if (parent_tree_code == ADDR_EXPR)
    goto finish;

  match.to = (tree) data;

  if (TREE_CODE (*tp) == INDIRECT_REF)
    {
      match.from = TREE_OPERAND (*tp, 0);
      match.hash = htab_hash_pointer (match.from);
      maybe_add_match (reference_table (true)->ptrs, &match);
    }
  else
    {
      match.from = *tp;
      match.hash = htab_hash_pointer (match.from);
      maybe_add_match (reference_table (true)->objs, &match);
    }

finish:
  parent_tree_code = TREE_CODE (*tp);
  return NULL_TREE;
}


/* Find all the references to aliased variables in the current function.  */

static void
find_references_in_function (void)
{
  basic_block bb;
  block_stmt_iterator i;

  FOR_EACH_BB (bb)
    for (i = bsi_start (bb); !bsi_end_p (i); bsi_next (&i))
      walk_tree (bsi_stmt_ptr (i), find_references_in_tree_helper,
		 (void *) *bsi_stmt_ptr (i), NULL);
}


/* Find the reference site for OBJECT.
   If IS_PTR is true, look for derferences of OBJECT instead.
   XXX: only the first site is returned in the current
   implementation.  If there are no matching sites, return NULL_TREE.  */

static tree
reference_site (tree object, bool is_ptr)
{
  if (is_ptr)
    return match (reference_table (true)->ptrs, object);
  else
    return match (reference_table (true)->objs, object);
}


/* Try to get more location info when something is missing.
   OBJECT1 and OBJECT2 are aliased names.  If IS_PTR1 or IS_PTR2, the alias
   is on the memory referenced or pointed to by OBJECT1 and OBJECT2.
   ALIAS_SITE, DEREF_SITE1 and DEREF_SITE2 are the statements where the
   alias takes place (some pointer assignment usually) and where the
   alias is referenced through OBJECT1 and OBJECT2 respectively.
   REF_TYPE1 and REF_TYPE2 will return the type of the reference at the
   respective sites.  Only the first matching reference is returned for
   each name.  If no statement is found, the function header is returned.  */

static void
maybe_find_missing_stmts (tree object1, bool is_ptr1,
                          tree object2, bool is_ptr2,
                          tree *alias_site,
                          tree *deref_site1,
                          tree *deref_site2)
{
  if (object1 && object2)
    {
      if (!*alias_site || !EXPR_HAS_LOCATION (*alias_site))
	*alias_site = find_alias_site (object1, is_ptr1, object2, is_ptr2);

      if (!*deref_site1 || !EXPR_HAS_LOCATION (*deref_site1))
	*deref_site1 = reference_site (object1, is_ptr1);

      if (!*deref_site2 || !EXPR_HAS_LOCATION (*deref_site2))
	*deref_site2 = reference_site (object2, is_ptr2);
    }

  /* If we could not find the alias site, set it to one of the dereference
     sites, if available.  */
  if (!*alias_site)
    {
      if (*deref_site1)
	*alias_site = *deref_site1;
      else if (*deref_site2)
	*alias_site = *deref_site2;
    }

  /* If we could not find the dereference sites, set them to the alias site,
     if known.  */
  if (!*deref_site1 && *alias_site)
    *deref_site1 = *alias_site;
  if (!*deref_site2 && *alias_site)
    *deref_site2 = *alias_site;
}


/* Callback for find_first_artificial_name.
   Find out if there are no artificial names at tree node *T.  */

static tree
ffan_walker (tree *t,
             int *go_below ATTRIBUTE_UNUSED,
             void *data ATTRIBUTE_UNUSED)
{
  if (TREE_CODE (*t) == VAR_DECL || TREE_CODE (*t) == PARM_DECL)
    if (DECL_ARTIFICIAL (*t))
      return *t;
  
  return NULL_TREE;
}

/* Return the first artificial name within EXPR, or NULL_TREE if
   none exists.  */

static tree
find_first_artificial_name (tree expr)
{
  return walk_tree_without_duplicates (&expr, ffan_walker, NULL);
}


/* Get a name from the original program for VAR.  */

static const char *
get_var_name (tree var)
{
  if (TREE_CODE (var) == SSA_NAME)
    return get_var_name (get_ssa_base (var));

  if (find_first_artificial_name (var))
    return "{unknown}";

  if (TREE_CODE (var) == VAR_DECL || TREE_CODE (var) == PARM_DECL)
    if (DECL_NAME (var))
      return IDENTIFIER_POINTER (DECL_NAME (var));

  return "{unknown}";
}


/* Return true if VAR contains an artificial name.  */

static bool
contains_artificial_name_p (tree var)
{
  if (TREE_CODE (var) == SSA_NAME)
    return contains_artificial_name_p (get_ssa_base (var));

  return find_first_artificial_name (var) != NULL_TREE;
}


/* Return "*" if OBJECT is not the actual alias but a pointer to it, or
   "" otherwise.
   IS_PTR is true when OBJECT is not the actual alias.
   In addition to checking IS_PTR, we also make sure that OBJECT is a pointer
   since IS_PTR would also be true for C++ references, but we should only
   print a * before a pointer and not before a reference.  */

static const char *
get_maybe_star_prefix (tree object, bool is_ptr)
{
  gcc_assert (object);
  return (is_ptr
          && TREE_CODE (TREE_TYPE (object)) == POINTER_TYPE) ? "*" : "";
}


/* Callback for contains_node_type_p.
   Returns true if *T has tree code *(int*)DATA.  */

static tree
contains_node_type_p_callback (tree *t,
			       int *go_below ATTRIBUTE_UNUSED,
			       void *data)
{
  return ((int) TREE_CODE (*t) == *((int *) data)) ? *t : NULL_TREE;
}


/* Return true if T contains a node with tree code TYPE.  */

static bool
contains_node_type_p (tree t, int type)
{
  return (walk_tree_without_duplicates (&t, contains_node_type_p_callback,
					(void *) &type)
	  != NULL_TREE);
}


/* Return true if a warning was issued in the front end at STMT.  */

static bool
already_warned_in_frontend_p (tree stmt)
{
  tree rhs_pointer;

  if (stmt == NULL_TREE)
    return false;

  rhs_pointer = get_rhs (stmt);

  if ((TREE_CODE (rhs_pointer) == NOP_EXPR
       || TREE_CODE (rhs_pointer) == CONVERT_EXPR
       || TREE_CODE (rhs_pointer) == VIEW_CONVERT_EXPR)
      && TREE_NO_WARNING (rhs_pointer))
    return true;
  else
    return false;
}


/* Return true if and only if TYPE is a function or method pointer type,
   or pointer to a pointer to ... to a function or method.  */

static bool
is_method_pointer (tree type)
{
  while (TREE_CODE (type) == POINTER_TYPE)
    type = TREE_TYPE (type);
  return TREE_CODE (type) == METHOD_TYPE || TREE_CODE (type) == FUNCTION_TYPE;
}


/* Issue a -Wstrict-aliasing warning.
   OBJECT1 and OBJECT2 are aliased names.
   If IS_PTR1 and/or IS_PTR2 is true, then the corresponding name
   OBJECT1/OBJECT2 is a pointer or reference to the aliased memory,
   rather than actual storage.
   ALIAS_SITE is a statement where the alias took place.  In the most common
   case, that is where a pointer was assigned to the address of an object.  */

static bool
strict_aliasing_warn (tree alias_site,
                      tree object1, bool is_ptr1,
                      tree object2, bool is_ptr2,
		      bool filter_artificials)
{
  tree ref_site1 = NULL_TREE;
  tree ref_site2 = NULL_TREE;
  const char *name1;
  const char *name2;
  location_t alias_loc;
  location_t ref1_loc;
  location_t ref2_loc;
  gcc_assert (object1);
  gcc_assert (object2);

  if (contains_artificial_name_p (object1)
      || contains_artificial_name_p (object2))
    return false;

  name1 = get_var_name (object1);
  name2 = get_var_name (object2);

  if (is_method_pointer (get_main_type (TREE_TYPE (object2))))
    return false;

  maybe_find_missing_stmts (object1, is_ptr1, object2, is_ptr2, &alias_site,
                            &ref_site1, &ref_site2);

  if (!alias_site)
    return false;

  if (EXPR_HAS_LOCATION (alias_site))
    alias_loc = EXPR_LOCATION (alias_site);
  else
    return false;

  if (EXPR_HAS_LOCATION (ref_site1))
    ref1_loc = EXPR_LOCATION (ref_site1);
  else
    ref1_loc = alias_loc;

  if (EXPR_HAS_LOCATION (ref_site2))
    ref2_loc = EXPR_LOCATION (ref_site2);
  else
    ref2_loc = alias_loc;

  if (already_warned_in_frontend_p (alias_site))
    return false;

  /* If they are not SSA names, but contain SSA names, drop the warning
     because it cannot be displayed well.
     Also drop it if they both contain artificials.
     XXX: this is a hack, must figure out a better way to display them.  */
  if (filter_artificials)
    if ((find_first_artificial_name (get_ssa_base (object1))
	 && find_first_artificial_name (get_ssa_base (object2)))
	|| (TREE_CODE (object1) != SSA_NAME
	    && contains_node_type_p (object1, SSA_NAME))
	|| (TREE_CODE (object2) != SSA_NAME
	    && contains_node_type_p (object2, SSA_NAME)))
      return false;

  /* XXX: In the following format string, %s:%d should be replaced by %H.
     However, in my tests only the first %H printed ok, while the
     second and third were printed as blanks.  */
  warning (OPT_Wstrict_aliasing,
	   "%Hlikely type-punning may break strict-aliasing rules: "
	   "object %<%s%s%> of main type %qT is referenced at or around "
	   "%s:%d and may be "
	   "aliased to object %<%s%s%> of main type %qT which is referenced "
	   "at or around %s:%d.",
	   &alias_loc,
	   get_maybe_star_prefix (object1, is_ptr1),
	   name1, get_otype (object1, is_ptr1),
	   LOCATION_FILE (ref1_loc), LOCATION_LINE (ref1_loc),
	   get_maybe_star_prefix (object2, is_ptr2),
	   name2, get_otype (object2, is_ptr2),
	   LOCATION_FILE (ref2_loc), LOCATION_LINE (ref2_loc));

  return true;
}



/* Return true when any objects of TYPE1 and TYPE2 respectively
   may not be aliased according to the language standard.  */

static bool
nonstandard_alias_types_p (tree type1, tree type2)
{
  HOST_WIDE_INT set1;
  HOST_WIDE_INT set2;

  if (VOID_TYPE_P (type1) || VOID_TYPE_P (type2))
    return false;

  set1 = get_alias_set (type1);
  set2 = get_alias_set (type2);
  return !alias_sets_conflict_p (set1, set2);
}



/* Returns true if the given name is a struct field tag (SFT).  */

static bool
struct_field_tag_p (tree var)
{
  return TREE_CODE (var) == STRUCT_FIELD_TAG;
}


/* Returns true when *PTR may not be aliased to ALIAS.
   See C standard 6.5p7 and C++ standard 3.10p15.
   If PTR_PTR is true, ALIAS represents a pointer or reference to the
   aliased storage rather than its actual name.  */

static bool
nonstandard_alias_p (tree ptr, tree alias, bool ptr_ptr)
{
  /* Find the types to compare.  */
  tree ptr_type = get_otype (ptr, true);
  tree alias_type = get_otype (alias, ptr_ptr);

  /* XXX: for now, say it's OK if the alias escapes.
     Not sure this is needed in general, but otherwise GCC will not
     bootstrap.  */
  if (var_ann (get_ssa_base (alias))->escape_mask != NO_ESCAPE)
      return false;

  /* XXX: don't get into structures for now.  It brings much complication
     and little benefit.  */
  if (struct_class_union_p (ptr_type) || struct_class_union_p (alias_type))
    return false;

  /* XXX: In 4.2.1, field resolution in alias is not as good as in pre-4.3
     This fixes problems found during the backport, where a pointer to the
     first field of a struct appears to be aliased to the whole struct.  */
  if (struct_field_tag_p (alias))
     return false;

  /* If they are both SSA names of artificials, let it go, the warning
     is too confusing.  */
  if (find_first_artificial_name (ptr) && find_first_artificial_name (alias))
    return false;

  /* Compare the types.  */
  return nonstandard_alias_types_p (ptr_type, alias_type);
}


/* Return true when we should skip analysis for pointer PTR based on the
   fact that their alias information *PI is not considered relevant.  */

static bool
skip_this_pointer (tree ptr ATTRIBUTE_UNUSED, struct ptr_info_def *pi)
{
  /* If it is not dereferenced, it is not a problem (locally).  */
  if (!pi->is_dereferenced)
    return true;

  /* This would probably cause too many false positives.  */
  if (pi->value_escapes_p || pi->pt_anything)
    return true;

  return false;
}


/* Find aliasing to named objects for pointer PTR.  */

static void
dsa_named_for (tree ptr)
{
  struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);

  if (pi)
    {
      if (skip_this_pointer (ptr, pi))
	return;

      /* For all the variables it could be aliased to.  */
      if (pi->pt_vars)
	{
	  unsigned ix;
	  bitmap_iterator bi;

	  EXECUTE_IF_SET_IN_BITMAP (pi->pt_vars, 0, ix, bi)
	    {
	      tree alias = referenced_var (ix);

              if (is_global_var (alias))
                continue;

	      if (nonstandard_alias_p (ptr, alias, false))
		strict_aliasing_warn (SSA_NAME_DEF_STMT (ptr),
				      ptr, true, alias, false, true);
	    }
	}
    }
}


/* Detect and report strict aliasing violation of named objects.  */

static void
detect_strict_aliasing_named (void)
{
  unsigned int i;

  for (i = 1; i < num_ssa_names; i++)
    {
      tree ptr = ssa_name (i);
      struct ptr_info_def *pi;

      if (ptr == NULL_TREE)
	continue;

      pi = SSA_NAME_PTR_INFO (ptr);

      if (!SSA_NAME_IN_FREE_LIST (ptr) && pi && pi->name_mem_tag)
	dsa_named_for (ptr);
    }
}


/* Return false only the first time I see each instance of FUNC.  */

static bool
processed_func_p (tree func)
{
  static htab_t seen = NULL;
  void **slot;

  if (!seen)
    seen = htab_create (100, htab_hash_pointer, htab_eq_pointer, NULL);

  slot = htab_find_slot (seen, func, INSERT);
  gcc_assert (slot);

  if (*slot)
    return true;

  *slot = func;
  return false;
}


/* Detect and warn about type-punning using points-to information.  */

void
strict_aliasing_warning_backend (void)
{
  if (!(flag_strict_aliasing
        && warn_strict_aliasing == 3
        && !processed_func_p (current_function_decl)))
    return;

  detect_strict_aliasing_named ();
  maybe_free_reference_table ();
}
