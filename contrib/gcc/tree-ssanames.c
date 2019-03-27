/* Generic routines for manipulating SSA_NAME expressions
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
                                                                               
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
#include "varray.h"
#include "ggc.h"
#include "tree-flow.h"

/* Rewriting a function into SSA form can create a huge number of SSA_NAMEs,
   many of which may be thrown away shortly after their creation if jumps
   were threaded through PHI nodes.  

   While our garbage collection mechanisms will handle this situation, it
   is extremely wasteful to create nodes and throw them away, especially
   when the nodes can be reused.

   For PR 8361, we can significantly reduce the number of nodes allocated
   and thus the total amount of memory allocated by managing SSA_NAMEs a
   little.  This additionally helps reduce the amount of work done by the
   garbage collector.  Similar results have been seen on a wider variety
   of tests (such as the compiler itself).

   Right now we maintain our free list on a per-function basis.  It may
   or may not make sense to maintain the free list for the duration of
   a compilation unit. 

   External code should rely solely upon HIGHEST_SSA_VERSION and the
   externally defined functions.  External code should not know about
   the details of the free list management.

   External code should also not assume the version number on nodes is
   monotonically increasing.  We reuse the version number when we
   reuse an SSA_NAME expression.  This helps keep arrays and bitmaps
   more compact.

   We could also use a zone allocator for these objects since they have
   a very well defined lifetime.  If someone wants to experiment with that
   this is the place to try it.  */
   
/* Array of all SSA_NAMEs used in the function.  */
VEC(tree,gc) *ssa_names;

/* Free list of SSA_NAMEs.  This list is wiped at the end of each function
   after we leave SSA form.  */
static GTY (()) tree free_ssanames;

/* Version numbers with special meanings.  We start allocating new version
   numbers after the special ones.  */
#define UNUSED_NAME_VERSION 0

#ifdef GATHER_STATISTICS
unsigned int ssa_name_nodes_reused;
unsigned int ssa_name_nodes_created;
#endif

/* Initialize management of SSA_NAMEs.  */

void
init_ssanames (void)
{
  ssa_names = VEC_alloc (tree, gc, 50);

  /* Version 0 is special, so reserve the first slot in the table.  Though
     currently unused, we may use version 0 in alias analysis as part of
     the heuristics used to group aliases when the alias sets are too
     large.

     We use VEC_quick_push here because we know that SSA_NAMES has at
     least 50 elements reserved in it.  */
  VEC_quick_push (tree, ssa_names, NULL_TREE);
  free_ssanames = NULL;
}

/* Finalize management of SSA_NAMEs.  */

void
fini_ssanames (void)
{
  VEC_free (tree, gc, ssa_names);
  free_ssanames = NULL;
}

/* Dump some simple statistics regarding the re-use of SSA_NAME nodes.  */

#ifdef GATHER_STATISTICS
void
ssanames_print_statistics (void)
{
  fprintf (stderr, "SSA_NAME nodes allocated: %u\n", ssa_name_nodes_created);
  fprintf (stderr, "SSA_NAME nodes reused: %u\n", ssa_name_nodes_reused);
}
#endif

/* Return an SSA_NAME node for variable VAR defined in statement STMT.
   STMT may be an empty statement for artificial references (e.g., default
   definitions created when a variable is used without a preceding
   definition).  */

tree
make_ssa_name (tree var, tree stmt)
{
  tree t;
  use_operand_p imm;

  gcc_assert (DECL_P (var)
	      || TREE_CODE (var) == INDIRECT_REF);

  gcc_assert (!stmt || EXPR_P (stmt) || TREE_CODE (stmt) == PHI_NODE);

  /* If our free list has an element, then use it.  */
  if (free_ssanames)
    {
      t = free_ssanames;
      free_ssanames = TREE_CHAIN (free_ssanames);
#ifdef GATHER_STATISTICS
      ssa_name_nodes_reused++;
#endif

      /* The node was cleared out when we put it on the free list, so
	 there is no need to do so again here.  */
      gcc_assert (ssa_name (SSA_NAME_VERSION (t)) == NULL);
      VEC_replace (tree, ssa_names, SSA_NAME_VERSION (t), t);
    }
  else
    {
      t = make_node (SSA_NAME);
      SSA_NAME_VERSION (t) = num_ssa_names;
      VEC_safe_push (tree, gc, ssa_names, t);
#ifdef GATHER_STATISTICS
      ssa_name_nodes_created++;
#endif
    }

  TREE_TYPE (t) = TREE_TYPE (var);
  SSA_NAME_VAR (t) = var;
  SSA_NAME_DEF_STMT (t) = stmt;
  SSA_NAME_PTR_INFO (t) = NULL;
  SSA_NAME_IN_FREE_LIST (t) = 0;
  imm = &(SSA_NAME_IMM_USE_NODE (t));
  imm->use = NULL;
  imm->prev = imm;
  imm->next = imm;
  imm->stmt = t;

  return t;
}


/* We no longer need the SSA_NAME expression VAR, release it so that
   it may be reused. 

   Note it is assumed that no calls to make_ssa_name will be made
   until all uses of the ssa name are released and that the only
   use of the SSA_NAME expression is to check its SSA_NAME_VAR.  All
   other fields must be assumed clobbered.  */

void
release_ssa_name (tree var)
{
  if (!var)
    return;

  /* Never release the default definition for a symbol.  It's a
     special SSA name that should always exist once it's created.  */
  if (var == default_def (SSA_NAME_VAR (var)))
    return;

  /* If VAR has been registered for SSA updating, don't remove it.
     After update_ssa has run, the name will be released.  */
  if (name_registered_for_update_p (var))
    {
      release_ssa_name_after_update_ssa (var);
      return;
    }

  /* release_ssa_name can be called multiple times on a single SSA_NAME.
     However, it should only end up on our free list one time.   We
     keep a status bit in the SSA_NAME node itself to indicate it has
     been put on the free list. 

     Note that once on the freelist you can not reference the SSA_NAME's
     defining statement.  */
  if (! SSA_NAME_IN_FREE_LIST (var))
    {
      tree saved_ssa_name_var = SSA_NAME_VAR (var);
      int saved_ssa_name_version = SSA_NAME_VERSION (var);
      use_operand_p imm = &(SSA_NAME_IMM_USE_NODE (var));

#ifdef ENABLE_CHECKING
      verify_imm_links (stderr, var);
#endif
      while (imm->next != imm)
	delink_imm_use (imm->next);

      VEC_replace (tree, ssa_names, SSA_NAME_VERSION (var), NULL_TREE);
      memset (var, 0, tree_size (var));

      imm->prev = imm;
      imm->next = imm;
      imm->stmt = var;
      /* First put back the right tree node so that the tree checking
	 macros do not complain.  */
      TREE_SET_CODE (var, SSA_NAME);

      /* Restore the version number.  */
      SSA_NAME_VERSION (var) = saved_ssa_name_version;

      /* Hopefully this can go away once we have the new incremental
         SSA updating code installed.  */
      SSA_NAME_VAR (var) = saved_ssa_name_var;

      /* Note this SSA_NAME is now in the first list.  */
      SSA_NAME_IN_FREE_LIST (var) = 1;

      /* And finally link it into the free list.  */
      TREE_CHAIN (var) = free_ssanames;
      free_ssanames = var;
    }
}

/* Creates a duplicate of a ssa name NAME defined in statement STMT.  */

tree
duplicate_ssa_name (tree name, tree stmt)
{
  tree new_name = make_ssa_name (SSA_NAME_VAR (name), stmt);
  struct ptr_info_def *old_ptr_info = SSA_NAME_PTR_INFO (name);

  if (old_ptr_info)
    duplicate_ssa_name_ptr_info (new_name, old_ptr_info);

  return new_name;
}


/* Creates a duplicate of the ptr_info_def at PTR_INFO for use by
   the SSA name NAME.  */

void
duplicate_ssa_name_ptr_info (tree name, struct ptr_info_def *ptr_info)
{
  struct ptr_info_def *new_ptr_info;

  gcc_assert (POINTER_TYPE_P (TREE_TYPE (name)));
  gcc_assert (!SSA_NAME_PTR_INFO (name));

  if (!ptr_info)
    return;

  new_ptr_info = ggc_alloc (sizeof (struct ptr_info_def));
  *new_ptr_info = *ptr_info;

  if (ptr_info->pt_vars)
    {
      new_ptr_info->pt_vars = BITMAP_GGC_ALLOC ();
      bitmap_copy (new_ptr_info->pt_vars, ptr_info->pt_vars);
    }

  SSA_NAME_PTR_INFO (name) = new_ptr_info;
}


/* Release all the SSA_NAMEs created by STMT.  */

void
release_defs (tree stmt)
{
  tree def;
  ssa_op_iter iter;

  /* Make sure that we are in SSA.  Otherwise, operand cache may point
     to garbage.  */
  gcc_assert (in_ssa_p);

  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_ALL_DEFS)
    if (TREE_CODE (def) == SSA_NAME)
      release_ssa_name (def);
}


/* Replace the symbol associated with SSA_NAME with SYM.  */

void
replace_ssa_name_symbol (tree ssa_name, tree sym)
{
  SSA_NAME_VAR (ssa_name) = sym;
  TREE_TYPE (ssa_name) = TREE_TYPE (sym);
}

#include "gt-tree-ssanames.h"
