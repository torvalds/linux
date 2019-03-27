/* Tree inlining.
   Copyright 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Alexandre Oliva <aoliva@redhat.com>

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
#include "toplev.h"
#include "tree.h"
#include "tree-inline.h"
#include "rtl.h"
#include "expr.h"
#include "flags.h"
#include "params.h"
#include "input.h"
#include "insn-config.h"
#include "varray.h"
#include "hashtab.h"
#include "langhooks.h"
#include "basic-block.h"
#include "tree-iterator.h"
#include "cgraph.h"
#include "intl.h"
#include "tree-mudflap.h"
#include "tree-flow.h"
#include "function.h"
#include "ggc.h"
#include "tree-flow.h"
#include "diagnostic.h"
#include "except.h"
#include "debug.h"
#include "pointer-set.h"
#include "ipa-prop.h"

/* I'm not real happy about this, but we need to handle gimple and
   non-gimple trees.  */
#include "tree-gimple.h"

/* Inlining, Cloning, Versioning, Parallelization

   Inlining: a function body is duplicated, but the PARM_DECLs are
   remapped into VAR_DECLs, and non-void RETURN_EXPRs become
   MODIFY_EXPRs that store to a dedicated returned-value variable.
   The duplicated eh_region info of the copy will later be appended
   to the info for the caller; the eh_region info in copied throwing
   statements and RESX_EXPRs is adjusted accordingly.

   Cloning: (only in C++) We have one body for a con/de/structor, and
   multiple function decls, each with a unique parameter list.
   Duplicate the body, using the given splay tree; some parameters
   will become constants (like 0 or 1).

   Versioning: a function body is duplicated and the result is a new
   function rather than into blocks of an existing function as with
   inlining.  Some parameters will become constants.

   Parallelization: a region of a function is duplicated resulting in
   a new function.  Variables may be replaced with complex expressions
   to enable shared variable semantics.

   All of these will simultaneously lookup any callgraph edges.  If
   we're going to inline the duplicated function body, and the given
   function has some cloned callgraph nodes (one for each place this
   function will be inlined) those callgraph edges will be duplicated.
   If we're cloning the body, those callgraph edges will be
   updated to point into the new body.  (Note that the original
   callgraph node and edge list will not be altered.)

   See the CALL_EXPR handling case in copy_body_r ().  */

/* 0 if we should not perform inlining.
   1 if we should expand functions calls inline at the tree level.
   2 if we should consider *all* functions to be inline
   candidates.  */

int flag_inline_trees = 0;

/* To Do:

   o In order to make inlining-on-trees work, we pessimized
     function-local static constants.  In particular, they are now
     always output, even when not addressed.  Fix this by treating
     function-local static constants just like global static
     constants; the back-end already knows not to output them if they
     are not needed.

   o Provide heuristics to clamp inlining of recursive template
     calls?  */

/* Prototypes.  */

static tree declare_return_variable (copy_body_data *, tree, tree, tree *);
static tree copy_generic_body (copy_body_data *);
static bool inlinable_function_p (tree);
static void remap_block (tree *, copy_body_data *);
static tree remap_decls (tree, copy_body_data *);
static void copy_bind_expr (tree *, int *, copy_body_data *);
static tree mark_local_for_remap_r (tree *, int *, void *);
static void unsave_expr_1 (tree);
static tree unsave_r (tree *, int *, void *);
static void declare_inline_vars (tree, tree);
static void remap_save_expr (tree *, void *, int *);
static void add_lexical_block (tree current_block, tree new_block);
static tree copy_decl_to_var (tree, copy_body_data *);
static tree copy_result_decl_to_var (tree, copy_body_data *);
static tree copy_decl_no_change (tree, copy_body_data *);
static tree copy_decl_maybe_to_var (tree, copy_body_data *);

/* Insert a tree->tree mapping for ID.  Despite the name suggests
   that the trees should be variables, it is used for more than that.  */

void
insert_decl_map (copy_body_data *id, tree key, tree value)
{
  splay_tree_insert (id->decl_map, (splay_tree_key) key,
		     (splay_tree_value) value);

  /* Always insert an identity map as well.  If we see this same new
     node again, we won't want to duplicate it a second time.  */
  if (key != value)
    splay_tree_insert (id->decl_map, (splay_tree_key) value,
		       (splay_tree_value) value);
}

/* Remap DECL during the copying of the BLOCK tree for the function.  */

tree
remap_decl (tree decl, copy_body_data *id)
{
  splay_tree_node n;
  tree fn;

  /* We only remap local variables in the current function.  */
  fn = id->src_fn;

  /* See if we have remapped this declaration.  */

  n = splay_tree_lookup (id->decl_map, (splay_tree_key) decl);

  /* If we didn't already have an equivalent for this declaration,
     create one now.  */
  if (!n)
    {
      /* Make a copy of the variable or label.  */
      tree t = id->copy_decl (decl, id);
     
      /* Remember it, so that if we encounter this local entity again
	 we can reuse this copy.  Do this early because remap_type may
	 need this decl for TYPE_STUB_DECL.  */
      insert_decl_map (id, decl, t);

      if (!DECL_P (t))
	return t;

      /* Remap types, if necessary.  */
      TREE_TYPE (t) = remap_type (TREE_TYPE (t), id);
      if (TREE_CODE (t) == TYPE_DECL)
        DECL_ORIGINAL_TYPE (t) = remap_type (DECL_ORIGINAL_TYPE (t), id);

      /* Remap sizes as necessary.  */
      walk_tree (&DECL_SIZE (t), copy_body_r, id, NULL);
      walk_tree (&DECL_SIZE_UNIT (t), copy_body_r, id, NULL);

      /* If fields, do likewise for offset and qualifier.  */
      if (TREE_CODE (t) == FIELD_DECL)
	{
	  walk_tree (&DECL_FIELD_OFFSET (t), copy_body_r, id, NULL);
	  if (TREE_CODE (DECL_CONTEXT (t)) == QUAL_UNION_TYPE)
	    walk_tree (&DECL_QUALIFIER (t), copy_body_r, id, NULL);
	}

      return t;
    }

  return unshare_expr ((tree) n->value);
}

static tree
remap_type_1 (tree type, copy_body_data *id)
{
  splay_tree_node node;
  tree new, t;

  if (type == NULL)
    return type;

  /* See if we have remapped this type.  */
  node = splay_tree_lookup (id->decl_map, (splay_tree_key) type);
  if (node)
    return (tree) node->value;

  /* The type only needs remapping if it's variably modified.  */
  if (! variably_modified_type_p (type, id->src_fn))
    {
      insert_decl_map (id, type, type);
      return type;
    }

  /* We do need a copy.  build and register it now.  If this is a pointer or
     reference type, remap the designated type and make a new pointer or
     reference type.  */
  if (TREE_CODE (type) == POINTER_TYPE)
    {
      new = build_pointer_type_for_mode (remap_type (TREE_TYPE (type), id),
					 TYPE_MODE (type),
					 TYPE_REF_CAN_ALIAS_ALL (type));
      insert_decl_map (id, type, new);
      return new;
    }
  else if (TREE_CODE (type) == REFERENCE_TYPE)
    {
      new = build_reference_type_for_mode (remap_type (TREE_TYPE (type), id),
					    TYPE_MODE (type),
					    TYPE_REF_CAN_ALIAS_ALL (type));
      insert_decl_map (id, type, new);
      return new;
    }
  else
    new = copy_node (type);

  insert_decl_map (id, type, new);

  /* This is a new type, not a copy of an old type.  Need to reassociate
     variants.  We can handle everything except the main variant lazily.  */
  t = TYPE_MAIN_VARIANT (type);
  if (type != t)
    {
      t = remap_type (t, id);
      TYPE_MAIN_VARIANT (new) = t;
      TYPE_NEXT_VARIANT (new) = TYPE_MAIN_VARIANT (t);
      TYPE_NEXT_VARIANT (t) = new;
    }
  else
    {
      TYPE_MAIN_VARIANT (new) = new;
      TYPE_NEXT_VARIANT (new) = NULL;
    }

  if (TYPE_STUB_DECL (type))
    TYPE_STUB_DECL (new) = remap_decl (TYPE_STUB_DECL (type), id);

  /* Lazily create pointer and reference types.  */
  TYPE_POINTER_TO (new) = NULL;
  TYPE_REFERENCE_TO (new) = NULL;

  switch (TREE_CODE (new))
    {
    case INTEGER_TYPE:
    case REAL_TYPE:
    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
      t = TYPE_MIN_VALUE (new);
      if (t && TREE_CODE (t) != INTEGER_CST)
        walk_tree (&TYPE_MIN_VALUE (new), copy_body_r, id, NULL);

      t = TYPE_MAX_VALUE (new);
      if (t && TREE_CODE (t) != INTEGER_CST)
        walk_tree (&TYPE_MAX_VALUE (new), copy_body_r, id, NULL);
      return new;

    case FUNCTION_TYPE:
      TREE_TYPE (new) = remap_type (TREE_TYPE (new), id);
      walk_tree (&TYPE_ARG_TYPES (new), copy_body_r, id, NULL);
      return new;

    case ARRAY_TYPE:
      TREE_TYPE (new) = remap_type (TREE_TYPE (new), id);
      TYPE_DOMAIN (new) = remap_type (TYPE_DOMAIN (new), id);
      break;

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      {
	tree f, nf = NULL;

	for (f = TYPE_FIELDS (new); f ; f = TREE_CHAIN (f))
	  {
	    t = remap_decl (f, id);
	    DECL_CONTEXT (t) = new;
	    TREE_CHAIN (t) = nf;
	    nf = t;
	  }
	TYPE_FIELDS (new) = nreverse (nf);
      }
      break;

    case OFFSET_TYPE:
    default:
      /* Shouldn't have been thought variable sized.  */
      gcc_unreachable ();
    }

  walk_tree (&TYPE_SIZE (new), copy_body_r, id, NULL);
  walk_tree (&TYPE_SIZE_UNIT (new), copy_body_r, id, NULL);

  return new;
}

tree
remap_type (tree type, copy_body_data *id)
{
  splay_tree_node node;

  if (type == NULL)
    return type;

  /* See if we have remapped this type.  */
  node = splay_tree_lookup (id->decl_map, (splay_tree_key) type);
  if (node)
    return (tree) node->value;

  /* The type only needs remapping if it's variably modified.  */
  if (! variably_modified_type_p (type, id->src_fn))
    {
      insert_decl_map (id, type, type);
      return type;
    }

  return remap_type_1 (type, id);
}

static tree
remap_decls (tree decls, copy_body_data *id)
{
  tree old_var;
  tree new_decls = NULL_TREE;

  /* Remap its variables.  */
  for (old_var = decls; old_var; old_var = TREE_CHAIN (old_var))
    {
      tree new_var;

      /* We can not chain the local static declarations into the unexpanded_var_list
         as we can't duplicate them or break one decl rule.  Go ahead and link
         them into unexpanded_var_list.  */
      if (!lang_hooks.tree_inlining.auto_var_in_fn_p (old_var, id->src_fn)
	  && !DECL_EXTERNAL (old_var))
	{
	  cfun->unexpanded_var_list = tree_cons (NULL_TREE, old_var,
						 cfun->unexpanded_var_list);
	  continue;
	}

      /* Remap the variable.  */
      new_var = remap_decl (old_var, id);

      /* If we didn't remap this variable, so we can't mess with its
	 TREE_CHAIN.  If we remapped this variable to the return slot, it's
	 already declared somewhere else, so don't declare it here.  */
      if (!new_var || new_var == id->retvar)
	;
      else
	{
	  gcc_assert (DECL_P (new_var));
	  TREE_CHAIN (new_var) = new_decls;
	  new_decls = new_var;
	}
    }

  return nreverse (new_decls);
}

/* Copy the BLOCK to contain remapped versions of the variables
   therein.  And hook the new block into the block-tree.  */

static void
remap_block (tree *block, copy_body_data *id)
{
  tree old_block;
  tree new_block;
  tree fn;

  /* Make the new block.  */
  old_block = *block;
  new_block = make_node (BLOCK);
  TREE_USED (new_block) = TREE_USED (old_block);
  BLOCK_ABSTRACT_ORIGIN (new_block) = old_block;
  BLOCK_SOURCE_LOCATION (new_block) = BLOCK_SOURCE_LOCATION (old_block);
  *block = new_block;

  /* Remap its variables.  */
  BLOCK_VARS (new_block) = remap_decls (BLOCK_VARS (old_block), id);

  fn = id->dst_fn;

  if (id->transform_lang_insert_block)
    lang_hooks.decls.insert_block (new_block);

  /* Remember the remapped block.  */
  insert_decl_map (id, old_block, new_block);
}

/* Copy the whole block tree and root it in id->block.  */
static tree
remap_blocks (tree block, copy_body_data *id)
{
  tree t;
  tree new = block;

  if (!block)
    return NULL;

  remap_block (&new, id);
  gcc_assert (new != block);
  for (t = BLOCK_SUBBLOCKS (block); t ; t = BLOCK_CHAIN (t))
    add_lexical_block (new, remap_blocks (t, id));
  return new;
}

static void
copy_statement_list (tree *tp)
{
  tree_stmt_iterator oi, ni;
  tree new;

  new = alloc_stmt_list ();
  ni = tsi_start (new);
  oi = tsi_start (*tp);
  *tp = new;

  for (; !tsi_end_p (oi); tsi_next (&oi))
    tsi_link_after (&ni, tsi_stmt (oi), TSI_NEW_STMT);
}

static void
copy_bind_expr (tree *tp, int *walk_subtrees, copy_body_data *id)
{
  tree block = BIND_EXPR_BLOCK (*tp);
  /* Copy (and replace) the statement.  */
  copy_tree_r (tp, walk_subtrees, NULL);
  if (block)
    {
      remap_block (&block, id);
      BIND_EXPR_BLOCK (*tp) = block;
    }

  if (BIND_EXPR_VARS (*tp))
    /* This will remap a lot of the same decls again, but this should be
       harmless.  */
    BIND_EXPR_VARS (*tp) = remap_decls (BIND_EXPR_VARS (*tp), id);
}

/* Called from copy_body_id via walk_tree.  DATA is really an
   `copy_body_data *'.  */

tree
copy_body_r (tree *tp, int *walk_subtrees, void *data)
{
  copy_body_data *id = (copy_body_data *) data;
  tree fn = id->src_fn;
  tree new_block;

  /* Begin by recognizing trees that we'll completely rewrite for the
     inlining context.  Our output for these trees is completely
     different from out input (e.g. RETURN_EXPR is deleted, and morphs
     into an edge).  Further down, we'll handle trees that get
     duplicated and/or tweaked.  */

  /* When requested, RETURN_EXPRs should be transformed to just the
     contained MODIFY_EXPR.  The branch semantics of the return will
     be handled elsewhere by manipulating the CFG rather than a statement.  */
  if (TREE_CODE (*tp) == RETURN_EXPR && id->transform_return_to_modify)
    {
      tree assignment = TREE_OPERAND (*tp, 0);

      /* If we're returning something, just turn that into an
	 assignment into the equivalent of the original RESULT_DECL.
	 If the "assignment" is just the result decl, the result
	 decl has already been set (e.g. a recent "foo (&result_decl,
	 ...)"); just toss the entire RETURN_EXPR.  */
      if (assignment && TREE_CODE (assignment) == MODIFY_EXPR)
	{
	  /* Replace the RETURN_EXPR with (a copy of) the
	     MODIFY_EXPR hanging underneath.  */
	  *tp = copy_node (assignment);
	}
      else /* Else the RETURN_EXPR returns no value.  */
	{
	  *tp = NULL;
	  return (tree) (void *)1;
	}
    }

  /* Local variables and labels need to be replaced by equivalent
     variables.  We don't want to copy static variables; there's only
     one of those, no matter how many times we inline the containing
     function.  Similarly for globals from an outer function.  */
  else if (lang_hooks.tree_inlining.auto_var_in_fn_p (*tp, fn))
    {
      tree new_decl;

      /* Remap the declaration.  */
      new_decl = remap_decl (*tp, id);
      gcc_assert (new_decl);
      /* Replace this variable with the copy.  */
      STRIP_TYPE_NOPS (new_decl);
      *tp = new_decl;
      *walk_subtrees = 0;
    }
  else if (TREE_CODE (*tp) == STATEMENT_LIST)
    copy_statement_list (tp);
  else if (TREE_CODE (*tp) == SAVE_EXPR)
    remap_save_expr (tp, id->decl_map, walk_subtrees);
  else if (TREE_CODE (*tp) == LABEL_DECL
	   && (! DECL_CONTEXT (*tp)
	       || decl_function_context (*tp) == id->src_fn))
    /* These may need to be remapped for EH handling.  */
    *tp = remap_decl (*tp, id);
  else if (TREE_CODE (*tp) == BIND_EXPR)
    copy_bind_expr (tp, walk_subtrees, id);
  /* Types may need remapping as well.  */
  else if (TYPE_P (*tp))
    *tp = remap_type (*tp, id);

  /* If this is a constant, we have to copy the node iff the type will be
     remapped.  copy_tree_r will not copy a constant.  */
  else if (CONSTANT_CLASS_P (*tp))
    {
      tree new_type = remap_type (TREE_TYPE (*tp), id);

      if (new_type == TREE_TYPE (*tp))
	*walk_subtrees = 0;

      else if (TREE_CODE (*tp) == INTEGER_CST)
	*tp = build_int_cst_wide (new_type, TREE_INT_CST_LOW (*tp),
				  TREE_INT_CST_HIGH (*tp));
      else
	{
	  *tp = copy_node (*tp);
	  TREE_TYPE (*tp) = new_type;
	}
    }

  /* Otherwise, just copy the node.  Note that copy_tree_r already
     knows not to copy VAR_DECLs, etc., so this is safe.  */
  else
    {
      /* Here we handle trees that are not completely rewritten.
	 First we detect some inlining-induced bogosities for
	 discarding.  */
      if (TREE_CODE (*tp) == MODIFY_EXPR
	  && TREE_OPERAND (*tp, 0) == TREE_OPERAND (*tp, 1)
	  && (lang_hooks.tree_inlining.auto_var_in_fn_p
	      (TREE_OPERAND (*tp, 0), fn)))
	{
	  /* Some assignments VAR = VAR; don't generate any rtl code
	     and thus don't count as variable modification.  Avoid
	     keeping bogosities like 0 = 0.  */
	  tree decl = TREE_OPERAND (*tp, 0), value;
	  splay_tree_node n;

	  n = splay_tree_lookup (id->decl_map, (splay_tree_key) decl);
	  if (n)
	    {
	      value = (tree) n->value;
	      STRIP_TYPE_NOPS (value);
	      if (TREE_CONSTANT (value) || TREE_READONLY_DECL_P (value))
		{
		  *tp = build_empty_stmt ();
		  return copy_body_r (tp, walk_subtrees, data);
		}
	    }
	}
      else if (TREE_CODE (*tp) == INDIRECT_REF)
	{
	  /* Get rid of *& from inline substitutions that can happen when a
	     pointer argument is an ADDR_EXPR.  */
	  tree decl = TREE_OPERAND (*tp, 0);
	  splay_tree_node n;

	  n = splay_tree_lookup (id->decl_map, (splay_tree_key) decl);
	  if (n)
	    {
	      tree new;
	      tree old;
	      /* If we happen to get an ADDR_EXPR in n->value, strip
	         it manually here as we'll eventually get ADDR_EXPRs
		 which lie about their types pointed to.  In this case
		 build_fold_indirect_ref wouldn't strip the INDIRECT_REF,
		 but we absolutely rely on that.  As fold_indirect_ref
	         does other useful transformations, try that first, though.  */
	      tree type = TREE_TYPE (TREE_TYPE ((tree)n->value));
	      new = unshare_expr ((tree)n->value);
	      old = *tp;
	      *tp = fold_indirect_ref_1 (type, new);
	      if (! *tp)
	        {
		  if (TREE_CODE (new) == ADDR_EXPR)
		    *tp = TREE_OPERAND (new, 0);
	          else
		    {
	              *tp = build1 (INDIRECT_REF, type, new);
		      TREE_THIS_VOLATILE (*tp) = TREE_THIS_VOLATILE (old);
		    }
		}
	      *walk_subtrees = 0;
	      return NULL;
	    }
	}

      /* Here is the "usual case".  Copy this tree node, and then
	 tweak some special cases.  */
      copy_tree_r (tp, walk_subtrees, NULL);
       
      /* If EXPR has block defined, map it to newly constructed block.
         When inlining we want EXPRs without block appear in the block
	 of function call.  */
      if (IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (TREE_CODE (*tp))))
	{
	  new_block = id->block;
	  if (TREE_BLOCK (*tp))
	    {
	      splay_tree_node n;
	      n = splay_tree_lookup (id->decl_map,
				     (splay_tree_key) TREE_BLOCK (*tp));
	      gcc_assert (n);
	      new_block = (tree) n->value;
	    }
	  TREE_BLOCK (*tp) = new_block;
	}

      if (TREE_CODE (*tp) == RESX_EXPR && id->eh_region_offset)
	TREE_OPERAND (*tp, 0) =
	  build_int_cst
	    (NULL_TREE,
	     id->eh_region_offset + TREE_INT_CST_LOW (TREE_OPERAND (*tp, 0)));

      if (TREE_CODE (*tp) != OMP_CLAUSE)
	TREE_TYPE (*tp) = remap_type (TREE_TYPE (*tp), id);

      /* The copied TARGET_EXPR has never been expanded, even if the
	 original node was expanded already.  */
      if (TREE_CODE (*tp) == TARGET_EXPR && TREE_OPERAND (*tp, 3))
	{
	  TREE_OPERAND (*tp, 1) = TREE_OPERAND (*tp, 3);
	  TREE_OPERAND (*tp, 3) = NULL_TREE;
	}

      /* Variable substitution need not be simple.  In particular, the
	 INDIRECT_REF substitution above.  Make sure that TREE_CONSTANT
	 and friends are up-to-date.  */
      else if (TREE_CODE (*tp) == ADDR_EXPR)
	{
	  walk_tree (&TREE_OPERAND (*tp, 0), copy_body_r, id, NULL);
	  /* Handle the case where we substituted an INDIRECT_REF
	     into the operand of the ADDR_EXPR.  */
	  if (TREE_CODE (TREE_OPERAND (*tp, 0)) == INDIRECT_REF)
	    *tp = TREE_OPERAND (TREE_OPERAND (*tp, 0), 0);
	  else
	    recompute_tree_invariant_for_addr_expr (*tp);
	  *walk_subtrees = 0;
	}
    }

  /* Keep iterating.  */
  return NULL_TREE;
}

/* Copy basic block, scale profile accordingly.  Edges will be taken care of
   later  */

static basic_block
copy_bb (copy_body_data *id, basic_block bb, int frequency_scale, int count_scale)
{
  block_stmt_iterator bsi, copy_bsi;
  basic_block copy_basic_block;

  /* create_basic_block() will append every new block to
     basic_block_info automatically.  */
  copy_basic_block = create_basic_block (NULL, (void *) 0,
                                         (basic_block) bb->prev_bb->aux);
  copy_basic_block->count = bb->count * count_scale / REG_BR_PROB_BASE;
  copy_basic_block->frequency = (bb->frequency
				     * frequency_scale / REG_BR_PROB_BASE);
  copy_bsi = bsi_start (copy_basic_block);

  for (bsi = bsi_start (bb);
       !bsi_end_p (bsi); bsi_next (&bsi))
    {
      tree stmt = bsi_stmt (bsi);
      tree orig_stmt = stmt;

      walk_tree (&stmt, copy_body_r, id, NULL);

      /* RETURN_EXPR might be removed,
         this is signalled by making stmt pointer NULL.  */
      if (stmt)
	{
	  tree call, decl;

	  /* With return slot optimization we can end up with
	     non-gimple (foo *)&this->m, fix that here.  */
	  if (TREE_CODE (stmt) == MODIFY_EXPR
	      && TREE_CODE (TREE_OPERAND (stmt, 1)) == NOP_EXPR
	      && !is_gimple_val (TREE_OPERAND (TREE_OPERAND (stmt, 1), 0)))
	    gimplify_stmt (&stmt);

          bsi_insert_after (&copy_bsi, stmt, BSI_NEW_STMT);
	  call = get_call_expr_in (stmt);
	  /* We're duplicating a CALL_EXPR.  Find any corresponding
	     callgraph edges and update or duplicate them.  */
	  if (call && (decl = get_callee_fndecl (call)))
	    {
	      struct cgraph_node *node;
	      struct cgraph_edge *edge;
	     
	      switch (id->transform_call_graph_edges)
		{
		case CB_CGE_DUPLICATE:
		  edge = cgraph_edge (id->src_node, orig_stmt);
		  if (edge)
		    cgraph_clone_edge (edge, id->dst_node, stmt,
				       REG_BR_PROB_BASE, 1, true);
		  break;

		case CB_CGE_MOVE_CLONES:
		  for (node = id->dst_node->next_clone;
		       node;
		       node = node->next_clone)
		    {
		      edge = cgraph_edge (node, orig_stmt);
		      gcc_assert (edge);
		      cgraph_set_call_stmt (edge, stmt);
		    }
		  /* FALLTHRU */

		case CB_CGE_MOVE:
		  edge = cgraph_edge (id->dst_node, orig_stmt);
		  if (edge)
		    cgraph_set_call_stmt (edge, stmt);
		  break;

		default:
		  gcc_unreachable ();
		}
	    }
	  /* If you think we can abort here, you are wrong.
	     There is no region 0 in tree land.  */
	  gcc_assert (lookup_stmt_eh_region_fn (id->src_cfun, orig_stmt)
		      != 0);

	  if (tree_could_throw_p (stmt))
	    {
	      int region = lookup_stmt_eh_region_fn (id->src_cfun, orig_stmt);
	      /* Add an entry for the copied tree in the EH hashtable.
		 When cloning or versioning, use the hashtable in
		 cfun, and just copy the EH number.  When inlining, use the
		 hashtable in the caller, and adjust the region number.  */
	      if (region > 0)
		add_stmt_to_eh_region (stmt, region + id->eh_region_offset);

	      /* If this tree doesn't have a region associated with it,
		 and there is a "current region,"
		 then associate this tree with the current region
		 and add edges associated with this region.  */
	      if ((lookup_stmt_eh_region_fn (id->src_cfun,
					     orig_stmt) <= 0
		   && id->eh_region > 0)
		  && tree_could_throw_p (stmt))
		add_stmt_to_eh_region (stmt, id->eh_region);
	    }
	}
    }
  return copy_basic_block;
}

/* Copy edges from BB into its copy constructed earlier, scale profile
   accordingly.  Edges will be taken care of later.  Assume aux
   pointers to point to the copies of each BB.  */
static void
copy_edges_for_bb (basic_block bb, int count_scale)
{
  basic_block new_bb = (basic_block) bb->aux;
  edge_iterator ei;
  edge old_edge;
  block_stmt_iterator bsi;
  int flags;

  /* Use the indices from the original blocks to create edges for the
     new ones.  */
  FOR_EACH_EDGE (old_edge, ei, bb->succs)
    if (!(old_edge->flags & EDGE_EH))
      {
	edge new;

	flags = old_edge->flags;

	/* Return edges do get a FALLTHRU flag when the get inlined.  */
	if (old_edge->dest->index == EXIT_BLOCK && !old_edge->flags
	    && old_edge->dest->aux != EXIT_BLOCK_PTR)
	  flags |= EDGE_FALLTHRU;
	new = make_edge (new_bb, (basic_block) old_edge->dest->aux, flags);
	new->count = old_edge->count * count_scale / REG_BR_PROB_BASE;
	new->probability = old_edge->probability;
      }

  if (bb->index == ENTRY_BLOCK || bb->index == EXIT_BLOCK)
    return;

  for (bsi = bsi_start (new_bb); !bsi_end_p (bsi);)
    {
      tree copy_stmt;

      copy_stmt = bsi_stmt (bsi);
      update_stmt (copy_stmt);
      /* Do this before the possible split_block.  */
      bsi_next (&bsi);

      /* If this tree could throw an exception, there are two
         cases where we need to add abnormal edge(s): the
         tree wasn't in a region and there is a "current
         region" in the caller; or the original tree had
         EH edges.  In both cases split the block after the tree,
         and add abnormal edge(s) as needed; we need both
         those from the callee and the caller.
         We check whether the copy can throw, because the const
         propagation can change an INDIRECT_REF which throws
         into a COMPONENT_REF which doesn't.  If the copy
         can throw, the original could also throw.  */

      if (tree_can_throw_internal (copy_stmt))
	{
	  if (!bsi_end_p (bsi))
	    /* Note that bb's predecessor edges aren't necessarily
	       right at this point; split_block doesn't care.  */
	    {
	      edge e = split_block (new_bb, copy_stmt);
	      new_bb = e->dest;
	      bsi = bsi_start (new_bb);
	    }

           make_eh_edges (copy_stmt);
	}
    }
}

/* Wrapper for remap_decl so it can be used as a callback.  */
static tree
remap_decl_1 (tree decl, void *data)
{
  return remap_decl (decl, (copy_body_data *) data);
}

/* Make a copy of the body of FN so that it can be inserted inline in
   another function.  Walks FN via CFG, returns new fndecl.  */

static tree
copy_cfg_body (copy_body_data * id, gcov_type count, int frequency,
	       basic_block entry_block_map, basic_block exit_block_map)
{
  tree callee_fndecl = id->src_fn;
  /* Original cfun for the callee, doesn't change.  */
  struct function *src_cfun = DECL_STRUCT_FUNCTION (callee_fndecl);
  /* Copy, built by this function.  */
  struct function *new_cfun;
  /* Place to copy from; when a copy of the function was saved off earlier,
     use that instead of the main copy.  */
  struct function *cfun_to_copy =
    (struct function *) ggc_alloc_cleared (sizeof (struct function));
  basic_block bb;
  tree new_fndecl = NULL;
  int count_scale, frequency_scale;

  if (ENTRY_BLOCK_PTR_FOR_FUNCTION (src_cfun)->count)
    count_scale = (REG_BR_PROB_BASE * count
		   / ENTRY_BLOCK_PTR_FOR_FUNCTION (src_cfun)->count);
  else
    count_scale = 1;

  if (ENTRY_BLOCK_PTR_FOR_FUNCTION (src_cfun)->frequency)
    frequency_scale = (REG_BR_PROB_BASE * frequency
		       /
		       ENTRY_BLOCK_PTR_FOR_FUNCTION (src_cfun)->frequency);
  else
    frequency_scale = count_scale;

  /* Register specific tree functions.  */
  tree_register_cfg_hooks ();

  /* Must have a CFG here at this point.  */
  gcc_assert (ENTRY_BLOCK_PTR_FOR_FUNCTION
	      (DECL_STRUCT_FUNCTION (callee_fndecl)));

  *cfun_to_copy = *DECL_STRUCT_FUNCTION (callee_fndecl);

  id->src_cfun = cfun_to_copy;

  /* If requested, create new basic_block_info and label_to_block_maps.
     Otherwise, insert our new blocks and labels into the existing cfg.  */
  if (id->transform_new_cfg)
    {
      new_cfun =
	(struct function *) ggc_alloc_cleared (sizeof (struct function));
      *new_cfun = *DECL_STRUCT_FUNCTION (callee_fndecl);
      new_cfun->cfg = NULL;
      new_cfun->decl = new_fndecl = copy_node (callee_fndecl);
      new_cfun->ib_boundaries_block = NULL;
      DECL_STRUCT_FUNCTION (new_fndecl) = new_cfun;
      push_cfun (new_cfun);
      init_empty_tree_cfg ();

      ENTRY_BLOCK_PTR->count =
	(ENTRY_BLOCK_PTR_FOR_FUNCTION (src_cfun)->count * count_scale /
	 REG_BR_PROB_BASE);
      ENTRY_BLOCK_PTR->frequency =
	(ENTRY_BLOCK_PTR_FOR_FUNCTION (src_cfun)->frequency *
	 frequency_scale / REG_BR_PROB_BASE);
      EXIT_BLOCK_PTR->count =
	(EXIT_BLOCK_PTR_FOR_FUNCTION (src_cfun)->count * count_scale /
	 REG_BR_PROB_BASE);
      EXIT_BLOCK_PTR->frequency =
	(EXIT_BLOCK_PTR_FOR_FUNCTION (src_cfun)->frequency *
	 frequency_scale / REG_BR_PROB_BASE);

      entry_block_map = ENTRY_BLOCK_PTR;
      exit_block_map = EXIT_BLOCK_PTR;
    }

  ENTRY_BLOCK_PTR_FOR_FUNCTION (cfun_to_copy)->aux = entry_block_map;
  EXIT_BLOCK_PTR_FOR_FUNCTION (cfun_to_copy)->aux = exit_block_map;

  /* Duplicate any exception-handling regions.  */
  if (cfun->eh)
    {
      if (id->transform_new_cfg)
        init_eh_for_function ();
      id->eh_region_offset
	= duplicate_eh_regions (cfun_to_copy, remap_decl_1, id,
				0, id->eh_region);
    }
  /* Use aux pointers to map the original blocks to copy.  */
  FOR_EACH_BB_FN (bb, cfun_to_copy)
    bb->aux = copy_bb (id, bb, frequency_scale, count_scale);
  /* Now that we've duplicated the blocks, duplicate their edges.  */
  FOR_ALL_BB_FN (bb, cfun_to_copy)
    copy_edges_for_bb (bb, count_scale);
  FOR_ALL_BB_FN (bb, cfun_to_copy)
    bb->aux = NULL;

  if (id->transform_new_cfg)
    pop_cfun ();

  return new_fndecl;
}

/* Make a copy of the body of FN so that it can be inserted inline in
   another function.  */

static tree
copy_generic_body (copy_body_data *id)
{
  tree body;
  tree fndecl = id->src_fn;

  body = DECL_SAVED_TREE (fndecl);
  walk_tree (&body, copy_body_r, id, NULL);

  return body;
}

static tree
copy_body (copy_body_data *id, gcov_type count, int frequency,
	   basic_block entry_block_map, basic_block exit_block_map)
{
  tree fndecl = id->src_fn;
  tree body;

  /* If this body has a CFG, walk CFG and copy.  */
  gcc_assert (ENTRY_BLOCK_PTR_FOR_FUNCTION (DECL_STRUCT_FUNCTION (fndecl)));
  body = copy_cfg_body (id, count, frequency, entry_block_map, exit_block_map);

  return body;
}

/* Return true if VALUE is an ADDR_EXPR of an automatic variable
   defined in function FN, or of a data member thereof.  */

static bool
self_inlining_addr_expr (tree value, tree fn)
{
  tree var;

  if (TREE_CODE (value) != ADDR_EXPR)
    return false;

  var = get_base_address (TREE_OPERAND (value, 0));

  return var && lang_hooks.tree_inlining.auto_var_in_fn_p (var, fn);
}

static void
setup_one_parameter (copy_body_data *id, tree p, tree value, tree fn,
		     basic_block bb, tree *vars)
{
  tree init_stmt;
  tree var;
  tree var_sub;

  /* If the parameter is never assigned to, we may not need to
     create a new variable here at all.  Instead, we may be able
     to just use the argument value.  */
  if (TREE_READONLY (p)
      && !TREE_ADDRESSABLE (p)
      && value && !TREE_SIDE_EFFECTS (value))
    {
      /* We may produce non-gimple trees by adding NOPs or introduce
	 invalid sharing when operand is not really constant.
	 It is not big deal to prohibit constant propagation here as
	 we will constant propagate in DOM1 pass anyway.  */
      if (is_gimple_min_invariant (value)
	  && lang_hooks.types_compatible_p (TREE_TYPE (value), TREE_TYPE (p))
	  /* We have to be very careful about ADDR_EXPR.  Make sure
	     the base variable isn't a local variable of the inlined
	     function, e.g., when doing recursive inlining, direct or
	     mutually-recursive or whatever, which is why we don't
	     just test whether fn == current_function_decl.  */
	  && ! self_inlining_addr_expr (value, fn))
	{
	  insert_decl_map (id, p, value);
	  return;
	}
    }

  /* Make an equivalent VAR_DECL.  Note that we must NOT remap the type
     here since the type of this decl must be visible to the calling
     function.  */
  var = copy_decl_to_var (p, id);

  /* See if the frontend wants to pass this by invisible reference.  If
     so, our new VAR_DECL will have REFERENCE_TYPE, and we need to
     replace uses of the PARM_DECL with dereferences.  */
  if (TREE_TYPE (var) != TREE_TYPE (p)
      && POINTER_TYPE_P (TREE_TYPE (var))
      && TREE_TYPE (TREE_TYPE (var)) == TREE_TYPE (p))
    {
      insert_decl_map (id, var, var);
      var_sub = build_fold_indirect_ref (var);
    }
  else
    var_sub = var;

  /* Register the VAR_DECL as the equivalent for the PARM_DECL;
     that way, when the PARM_DECL is encountered, it will be
     automatically replaced by the VAR_DECL.  */
  insert_decl_map (id, p, var_sub);

  /* Declare this new variable.  */
  TREE_CHAIN (var) = *vars;
  *vars = var;

  /* Make gimplifier happy about this variable.  */
  DECL_SEEN_IN_BIND_EXPR_P (var) = 1;

  /* Even if P was TREE_READONLY, the new VAR should not be.
     In the original code, we would have constructed a
     temporary, and then the function body would have never
     changed the value of P.  However, now, we will be
     constructing VAR directly.  The constructor body may
     change its value multiple times as it is being
     constructed.  Therefore, it must not be TREE_READONLY;
     the back-end assumes that TREE_READONLY variable is
     assigned to only once.  */
  if (TYPE_NEEDS_CONSTRUCTING (TREE_TYPE (p)))
    TREE_READONLY (var) = 0;

  /* Initialize this VAR_DECL from the equivalent argument.  Convert
     the argument to the proper type in case it was promoted.  */
  if (value)
    {
      tree rhs = fold_convert (TREE_TYPE (var), value);
      block_stmt_iterator bsi = bsi_last (bb);

      if (rhs == error_mark_node)
	return;

      STRIP_USELESS_TYPE_CONVERSION (rhs);

      /* We want to use MODIFY_EXPR, not INIT_EXPR here so that we
	 keep our trees in gimple form.  */
      init_stmt = build2 (MODIFY_EXPR, TREE_TYPE (var), var, rhs);

      /* If we did not create a gimple value and we did not create a gimple
	 cast of a gimple value, then we will need to gimplify INIT_STMTS
	 at the end.  Note that is_gimple_cast only checks the outer
	 tree code, not its operand.  Thus the explicit check that its
	 operand is a gimple value.  */
      if (!is_gimple_val (rhs)
	  && (!is_gimple_cast (rhs)
	      || !is_gimple_val (TREE_OPERAND (rhs, 0))))
	gimplify_stmt (&init_stmt);

      /* If VAR represents a zero-sized variable, it's possible that the
	 assignment statment may result in no gimple statements.  */
      if (init_stmt)
        bsi_insert_after (&bsi, init_stmt, BSI_NEW_STMT);
    }
}

/* Generate code to initialize the parameters of the function at the
   top of the stack in ID from the ARGS (presented as a TREE_LIST).  */

static void
initialize_inlined_parameters (copy_body_data *id, tree args, tree static_chain,
			       tree fn, basic_block bb)
{
  tree parms;
  tree a;
  tree p;
  tree vars = NULL_TREE;
  int argnum = 0;

  /* Figure out what the parameters are.  */
  parms = DECL_ARGUMENTS (fn);

  /* Loop through the parameter declarations, replacing each with an
     equivalent VAR_DECL, appropriately initialized.  */
  for (p = parms, a = args; p;
       a = a ? TREE_CHAIN (a) : a, p = TREE_CHAIN (p))
    {
      tree value;

      ++argnum;

      /* Find the initializer.  */
      value = lang_hooks.tree_inlining.convert_parm_for_inlining
	      (p, a ? TREE_VALUE (a) : NULL_TREE, fn, argnum);

      setup_one_parameter (id, p, value, fn, bb, &vars);
    }

  /* Initialize the static chain.  */
  p = DECL_STRUCT_FUNCTION (fn)->static_chain_decl;
  gcc_assert (fn != current_function_decl);
  if (p)
    {
      /* No static chain?  Seems like a bug in tree-nested.c.  */
      gcc_assert (static_chain);

      setup_one_parameter (id, p, static_chain, fn, bb, &vars);
    }

  declare_inline_vars (id->block, vars);
}

/* Declare a return variable to replace the RESULT_DECL for the
   function we are calling.  An appropriate DECL_STMT is returned.
   The USE_STMT is filled to contain a use of the declaration to
   indicate the return value of the function.

   RETURN_SLOT_ADDR, if non-null, was a fake parameter that
   took the address of the result.  MODIFY_DEST, if non-null, was the LHS of
   the MODIFY_EXPR to which this call is the RHS.

   The return value is a (possibly null) value that is the result of the
   function as seen by the callee.  *USE_P is a (possibly null) value that
   holds the result as seen by the caller.  */

static tree
declare_return_variable (copy_body_data *id, tree return_slot_addr,
			 tree modify_dest, tree *use_p)
{
  tree callee = id->src_fn;
  tree caller = id->dst_fn;
  tree result = DECL_RESULT (callee);
  tree callee_type = TREE_TYPE (result);
  tree caller_type = TREE_TYPE (TREE_TYPE (callee));
  tree var, use;

  /* We don't need to do anything for functions that don't return
     anything.  */
  if (!result || VOID_TYPE_P (callee_type))
    {
      *use_p = NULL_TREE;
      return NULL_TREE;
    }

  /* If there was a return slot, then the return value is the
     dereferenced address of that object.  */
  if (return_slot_addr)
    {
      /* The front end shouldn't have used both return_slot_addr and
	 a modify expression.  */
      gcc_assert (!modify_dest);
      if (DECL_BY_REFERENCE (result))
	var = return_slot_addr;
      else
	var = build_fold_indirect_ref (return_slot_addr);
      if (TREE_CODE (TREE_TYPE (result)) == COMPLEX_TYPE
	  && !DECL_COMPLEX_GIMPLE_REG_P (result)
	  && DECL_P (var))
	DECL_COMPLEX_GIMPLE_REG_P (var) = 0;
      use = NULL;
      goto done;
    }

  /* All types requiring non-trivial constructors should have been handled.  */
  gcc_assert (!TREE_ADDRESSABLE (callee_type));

  /* Attempt to avoid creating a new temporary variable.  */
  if (modify_dest)
    {
      bool use_it = false;

      /* We can't use MODIFY_DEST if there's type promotion involved.  */
      if (!lang_hooks.types_compatible_p (caller_type, callee_type))
	use_it = false;

      /* ??? If we're assigning to a variable sized type, then we must
	 reuse the destination variable, because we've no good way to
	 create variable sized temporaries at this point.  */
      else if (TREE_CODE (TYPE_SIZE_UNIT (caller_type)) != INTEGER_CST)
	use_it = true;

      /* If the callee cannot possibly modify MODIFY_DEST, then we can
	 reuse it as the result of the call directly.  Don't do this if
	 it would promote MODIFY_DEST to addressable.  */
      else if (TREE_ADDRESSABLE (result))
	use_it = false;
      else
	{
	  tree base_m = get_base_address (modify_dest);

	  /* If the base isn't a decl, then it's a pointer, and we don't
	     know where that's going to go.  */
	  if (!DECL_P (base_m))
	    use_it = false;
	  else if (is_global_var (base_m))
	    use_it = false;
	  else if (TREE_CODE (TREE_TYPE (result)) == COMPLEX_TYPE
		   && !DECL_COMPLEX_GIMPLE_REG_P (result)
		   && DECL_COMPLEX_GIMPLE_REG_P (base_m))
	    use_it = false;
	  else if (!TREE_ADDRESSABLE (base_m))
	    use_it = true;
	}

      if (use_it)
	{
	  var = modify_dest;
	  use = NULL;
	  goto done;
	}
    }

  gcc_assert (TREE_CODE (TYPE_SIZE_UNIT (callee_type)) == INTEGER_CST);

  var = copy_result_decl_to_var (result, id);

  DECL_SEEN_IN_BIND_EXPR_P (var) = 1;
  DECL_STRUCT_FUNCTION (caller)->unexpanded_var_list
    = tree_cons (NULL_TREE, var,
		 DECL_STRUCT_FUNCTION (caller)->unexpanded_var_list);

  /* Do not have the rest of GCC warn about this variable as it should
     not be visible to the user.  */
  TREE_NO_WARNING (var) = 1;

  declare_inline_vars (id->block, var);

  /* Build the use expr.  If the return type of the function was
     promoted, convert it back to the expected type.  */
  use = var;
  if (!lang_hooks.types_compatible_p (TREE_TYPE (var), caller_type))
    use = fold_convert (caller_type, var);
    
  STRIP_USELESS_TYPE_CONVERSION (use);

  if (DECL_BY_REFERENCE (result))
    var = build_fold_addr_expr (var);

 done:
  /* Register the VAR_DECL as the equivalent for the RESULT_DECL; that
     way, when the RESULT_DECL is encountered, it will be
     automatically replaced by the VAR_DECL.  */
  insert_decl_map (id, result, var);

  /* Remember this so we can ignore it in remap_decls.  */
  id->retvar = var;

  *use_p = use;
  return var;
}

/* Returns nonzero if a function can be inlined as a tree.  */

bool
tree_inlinable_function_p (tree fn)
{
  return inlinable_function_p (fn);
}

static const char *inline_forbidden_reason;

static tree
inline_forbidden_p_1 (tree *nodep, int *walk_subtrees ATTRIBUTE_UNUSED,
		      void *fnp)
{
  tree node = *nodep;
  tree fn = (tree) fnp;
  tree t;

  switch (TREE_CODE (node))
    {
    case CALL_EXPR:
      /* Refuse to inline alloca call unless user explicitly forced so as
	 this may change program's memory overhead drastically when the
	 function using alloca is called in loop.  In GCC present in
	 SPEC2000 inlining into schedule_block cause it to require 2GB of
	 RAM instead of 256MB.  */
      if (alloca_call_p (node)
	  && !lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn)))
	{
	  inline_forbidden_reason
	    = G_("function %q+F can never be inlined because it uses "
		 "alloca (override using the always_inline attribute)");
	  return node;
	}
      t = get_callee_fndecl (node);
      if (! t)
	break;

      /* We cannot inline functions that call setjmp.  */
      if (setjmp_call_p (t))
	{
	  inline_forbidden_reason
	    = G_("function %q+F can never be inlined because it uses setjmp");
	  return node;
	}

      if (DECL_BUILT_IN_CLASS (t) == BUILT_IN_NORMAL)
	switch (DECL_FUNCTION_CODE (t))
	  {
	    /* We cannot inline functions that take a variable number of
	       arguments.  */
	  case BUILT_IN_VA_START:
	  case BUILT_IN_STDARG_START:
	  case BUILT_IN_NEXT_ARG:
	  case BUILT_IN_VA_END:
	    inline_forbidden_reason
	      = G_("function %q+F can never be inlined because it "
		   "uses variable argument lists");
	    return node;

	  case BUILT_IN_LONGJMP:
	    /* We can't inline functions that call __builtin_longjmp at
	       all.  The non-local goto machinery really requires the
	       destination be in a different function.  If we allow the
	       function calling __builtin_longjmp to be inlined into the
	       function calling __builtin_setjmp, Things will Go Awry.  */
	    inline_forbidden_reason
	      = G_("function %q+F can never be inlined because "
		   "it uses setjmp-longjmp exception handling");
	    return node;

	  case BUILT_IN_NONLOCAL_GOTO:
	    /* Similarly.  */
	    inline_forbidden_reason
	      = G_("function %q+F can never be inlined because "
		   "it uses non-local goto");
	    return node;

	  case BUILT_IN_RETURN:
	  case BUILT_IN_APPLY_ARGS:
	    /* If a __builtin_apply_args caller would be inlined,
	       it would be saving arguments of the function it has
	       been inlined into.  Similarly __builtin_return would
	       return from the function the inline has been inlined into.  */
	    inline_forbidden_reason
	      = G_("function %q+F can never be inlined because "
		   "it uses __builtin_return or __builtin_apply_args");
	    return node;

	  default:
	    break;
	  }
      break;

    case GOTO_EXPR:
      t = TREE_OPERAND (node, 0);

      /* We will not inline a function which uses computed goto.  The
	 addresses of its local labels, which may be tucked into
	 global storage, are of course not constant across
	 instantiations, which causes unexpected behavior.  */
      if (TREE_CODE (t) != LABEL_DECL)
	{
	  inline_forbidden_reason
	    = G_("function %q+F can never be inlined "
		 "because it contains a computed goto");
	  return node;
	}
      break;

    case LABEL_EXPR:
      t = TREE_OPERAND (node, 0);
      if (DECL_NONLOCAL (t))
	{
	  /* We cannot inline a function that receives a non-local goto
	     because we cannot remap the destination label used in the
	     function that is performing the non-local goto.  */
	  inline_forbidden_reason
	    = G_("function %q+F can never be inlined "
		 "because it receives a non-local goto");
	  return node;
	}
      break;

    case RECORD_TYPE:
    case UNION_TYPE:
      /* We cannot inline a function of the form

	   void F (int i) { struct S { int ar[i]; } s; }

	 Attempting to do so produces a catch-22.
	 If walk_tree examines the TYPE_FIELDS chain of RECORD_TYPE/
	 UNION_TYPE nodes, then it goes into infinite recursion on a
	 structure containing a pointer to its own type.  If it doesn't,
	 then the type node for S doesn't get adjusted properly when
	 F is inlined. 

	 ??? This is likely no longer true, but it's too late in the 4.0
	 cycle to try to find out.  This should be checked for 4.1.  */
      for (t = TYPE_FIELDS (node); t; t = TREE_CHAIN (t))
	if (variably_modified_type_p (TREE_TYPE (t), NULL))
	  {
	    inline_forbidden_reason
	      = G_("function %q+F can never be inlined "
		   "because it uses variable sized variables");
	    return node;
	  }

    default:
      break;
    }

  return NULL_TREE;
}

/* Return subexpression representing possible alloca call, if any.  */
static tree
inline_forbidden_p (tree fndecl)
{
  location_t saved_loc = input_location;
  block_stmt_iterator bsi;
  basic_block bb;
  tree ret = NULL_TREE;

  FOR_EACH_BB_FN (bb, DECL_STRUCT_FUNCTION (fndecl))
    for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
      {
	ret = walk_tree_without_duplicates (bsi_stmt_ptr (bsi),
				    inline_forbidden_p_1, fndecl);
	if (ret)
	  goto egress;
      }

egress:
  input_location = saved_loc;
  return ret;
}

/* Returns nonzero if FN is a function that does not have any
   fundamental inline blocking properties.  */

static bool
inlinable_function_p (tree fn)
{
  bool inlinable = true;

  /* If we've already decided this function shouldn't be inlined,
     there's no need to check again.  */
  if (DECL_UNINLINABLE (fn))
    return false;

  /* See if there is any language-specific reason it cannot be
     inlined.  (It is important that this hook be called early because
     in C++ it may result in template instantiation.)
     If the function is not inlinable for language-specific reasons,
     it is left up to the langhook to explain why.  */
  inlinable = !lang_hooks.tree_inlining.cannot_inline_tree_fn (&fn);

  /* If we don't have the function body available, we can't inline it.
     However, this should not be recorded since we also get here for
     forward declared inline functions.  Therefore, return at once.  */
  if (!DECL_SAVED_TREE (fn))
    return false;

  /* If we're not inlining at all, then we cannot inline this function.  */
  else if (!flag_inline_trees)
    inlinable = false;

  /* Only try to inline functions if DECL_INLINE is set.  This should be
     true for all functions declared `inline', and for all other functions
     as well with -finline-functions.

     Don't think of disregarding DECL_INLINE when flag_inline_trees == 2;
     it's the front-end that must set DECL_INLINE in this case, because
     dwarf2out loses if a function that does not have DECL_INLINE set is
     inlined anyway.  That is why we have both DECL_INLINE and
     DECL_DECLARED_INLINE_P.  */
  /* FIXME: When flag_inline_trees dies, the check for flag_unit_at_a_time
	    here should be redundant.  */
  else if (!DECL_INLINE (fn) && !flag_unit_at_a_time)
    inlinable = false;

  else if (inline_forbidden_p (fn))
    {
      /* See if we should warn about uninlinable functions.  Previously,
	 some of these warnings would be issued while trying to expand
	 the function inline, but that would cause multiple warnings
	 about functions that would for example call alloca.  But since
	 this a property of the function, just one warning is enough.
	 As a bonus we can now give more details about the reason why a
	 function is not inlinable.
	 We only warn for functions declared `inline' by the user.  */
      bool do_warning = (warn_inline
			 && DECL_INLINE (fn)
			 && DECL_DECLARED_INLINE_P (fn)
			 && !DECL_IN_SYSTEM_HEADER (fn));

      if (lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn)))
	sorry (inline_forbidden_reason, fn);
      else if (do_warning)
	warning (OPT_Winline, inline_forbidden_reason, fn);

      inlinable = false;
    }

  /* Squirrel away the result so that we don't have to check again.  */
  DECL_UNINLINABLE (fn) = !inlinable;

  return inlinable;
}

/* Estimate the cost of a memory move.  Use machine dependent
   word size and take possible memcpy call into account.  */

int
estimate_move_cost (tree type)
{
  HOST_WIDE_INT size;

  size = int_size_in_bytes (type);

  if (size < 0 || size > MOVE_MAX_PIECES * MOVE_RATIO)
    /* Cost of a memcpy call, 3 arguments and the call.  */
    return 4;
  else
    return ((size + MOVE_MAX_PIECES - 1) / MOVE_MAX_PIECES);
}

/* Used by estimate_num_insns.  Estimate number of instructions seen
   by given statement.  */

static tree
estimate_num_insns_1 (tree *tp, int *walk_subtrees, void *data)
{
  int *count = (int *) data;
  tree x = *tp;

  if (IS_TYPE_OR_DECL_P (x))
    {
      *walk_subtrees = 0;
      return NULL;
    }
  /* Assume that constants and references counts nothing.  These should
     be majorized by amount of operations among them we count later
     and are common target of CSE and similar optimizations.  */
  else if (CONSTANT_CLASS_P (x) || REFERENCE_CLASS_P (x))
    return NULL;

  switch (TREE_CODE (x))
    {
    /* Containers have no cost.  */
    case TREE_LIST:
    case TREE_VEC:
    case BLOCK:
    case COMPONENT_REF:
    case BIT_FIELD_REF:
    case INDIRECT_REF:
    case ALIGN_INDIRECT_REF:
    case MISALIGNED_INDIRECT_REF:
    case ARRAY_REF:
    case ARRAY_RANGE_REF:
    case OBJ_TYPE_REF:
    case EXC_PTR_EXPR: /* ??? */
    case FILTER_EXPR: /* ??? */
    case COMPOUND_EXPR:
    case BIND_EXPR:
    case WITH_CLEANUP_EXPR:
    case NOP_EXPR:
    case VIEW_CONVERT_EXPR:
    case SAVE_EXPR:
    case ADDR_EXPR:
    case COMPLEX_EXPR:
    case RANGE_EXPR:
    case CASE_LABEL_EXPR:
    case SSA_NAME:
    case CATCH_EXPR:
    case EH_FILTER_EXPR:
    case STATEMENT_LIST:
    case ERROR_MARK:
    case NON_LVALUE_EXPR:
    case FDESC_EXPR:
    case VA_ARG_EXPR:
    case TRY_CATCH_EXPR:
    case TRY_FINALLY_EXPR:
    case LABEL_EXPR:
    case GOTO_EXPR:
    case RETURN_EXPR:
    case EXIT_EXPR:
    case LOOP_EXPR:
    case PHI_NODE:
    case WITH_SIZE_EXPR:
    case OMP_CLAUSE:
    case OMP_RETURN:
    case OMP_CONTINUE:
      break;

    /* We don't account constants for now.  Assume that the cost is amortized
       by operations that do use them.  We may re-consider this decision once
       we are able to optimize the tree before estimating its size and break
       out static initializers.  */
    case IDENTIFIER_NODE:
    case INTEGER_CST:
    case REAL_CST:
    case COMPLEX_CST:
    case VECTOR_CST:
    case STRING_CST:
      *walk_subtrees = 0;
      return NULL;

    /* Try to estimate the cost of assignments.  We have three cases to
       deal with:
	1) Simple assignments to registers;
	2) Stores to things that must live in memory.  This includes
	   "normal" stores to scalars, but also assignments of large
	   structures, or constructors of big arrays;
	3) TARGET_EXPRs.

       Let us look at the first two cases, assuming we have "a = b + C":
       <modify_expr <var_decl "a"> <plus_expr <var_decl "b"> <constant C>>
       If "a" is a GIMPLE register, the assignment to it is free on almost
       any target, because "a" usually ends up in a real register.  Hence
       the only cost of this expression comes from the PLUS_EXPR, and we
       can ignore the MODIFY_EXPR.
       If "a" is not a GIMPLE register, the assignment to "a" will most
       likely be a real store, so the cost of the MODIFY_EXPR is the cost
       of moving something into "a", which we compute using the function
       estimate_move_cost.

       The third case deals with TARGET_EXPRs, for which the semantics are
       that a temporary is assigned, unless the TARGET_EXPR itself is being
       assigned to something else.  In the latter case we do not need the
       temporary.  E.g. in <modify_expr <var_decl "a"> <target_expr>>, the
       MODIFY_EXPR is free.  */
    case INIT_EXPR:
    case MODIFY_EXPR:
      /* Is the right and side a TARGET_EXPR?  */
      if (TREE_CODE (TREE_OPERAND (x, 1)) == TARGET_EXPR)
	break;
      /* ... fall through ...  */

    case TARGET_EXPR:
      x = TREE_OPERAND (x, 0);
      /* Is this an assignments to a register?  */
      if (is_gimple_reg (x))
	break;
      /* Otherwise it's a store, so fall through to compute the move cost.  */

    case CONSTRUCTOR:
      *count += estimate_move_cost (TREE_TYPE (x));
      break;

    /* Assign cost of 1 to usual operations.
       ??? We may consider mapping RTL costs to this.  */
    case COND_EXPR:
    case VEC_COND_EXPR:

    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:

    case FIX_TRUNC_EXPR:
    case FIX_CEIL_EXPR:
    case FIX_FLOOR_EXPR:
    case FIX_ROUND_EXPR:

    case NEGATE_EXPR:
    case FLOAT_EXPR:
    case MIN_EXPR:
    case MAX_EXPR:
    case ABS_EXPR:

    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case LROTATE_EXPR:
    case RROTATE_EXPR:
    case VEC_LSHIFT_EXPR:
    case VEC_RSHIFT_EXPR:

    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case BIT_AND_EXPR:
    case BIT_NOT_EXPR:

    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_XOR_EXPR:
    case TRUTH_NOT_EXPR:

    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case ORDERED_EXPR:
    case UNORDERED_EXPR:

    case UNLT_EXPR:
    case UNLE_EXPR:
    case UNGT_EXPR:
    case UNGE_EXPR:
    case UNEQ_EXPR:
    case LTGT_EXPR:

    case CONVERT_EXPR:

    case CONJ_EXPR:

    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:

    case SWITCH_EXPR:

    case ASM_EXPR:

    case REALIGN_LOAD_EXPR:

    case REDUC_MAX_EXPR:
    case REDUC_MIN_EXPR:
    case REDUC_PLUS_EXPR:
    case WIDEN_SUM_EXPR:
    case DOT_PROD_EXPR: 

    case WIDEN_MULT_EXPR:

    case RESX_EXPR:
      *count += 1;
      break;

    /* Few special cases of expensive operations.  This is useful
       to avoid inlining on functions having too many of these.  */
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case EXACT_DIV_EXPR:
    case TRUNC_MOD_EXPR:
    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case RDIV_EXPR:
      *count += 10;
      break;
    case CALL_EXPR:
      {
	tree decl = get_callee_fndecl (x);
	tree arg;

	if (decl && DECL_BUILT_IN_CLASS (decl) == BUILT_IN_NORMAL)
	  switch (DECL_FUNCTION_CODE (decl))
	    {
	    case BUILT_IN_CONSTANT_P:
	      *walk_subtrees = 0;
	      return NULL_TREE;
	    case BUILT_IN_EXPECT:
	      return NULL_TREE;
	    default:
	      break;
	    }

	/* Our cost must be kept in sync with cgraph_estimate_size_after_inlining
	   that does use function declaration to figure out the arguments.  */
	if (!decl)
	  {
	    for (arg = TREE_OPERAND (x, 1); arg; arg = TREE_CHAIN (arg))
	      *count += estimate_move_cost (TREE_TYPE (TREE_VALUE (arg)));
	  }
	else
	  {
	    for (arg = DECL_ARGUMENTS (decl); arg; arg = TREE_CHAIN (arg))
	      *count += estimate_move_cost (TREE_TYPE (arg));
	  }

	*count += PARAM_VALUE (PARAM_INLINE_CALL_COST);
	break;
      }

    case OMP_PARALLEL:
    case OMP_FOR:
    case OMP_SECTIONS:
    case OMP_SINGLE:
    case OMP_SECTION:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_CRITICAL:
    case OMP_ATOMIC:
      /* OpenMP directives are generally very expensive.  */
      *count += 40;
      break;

    default:
      gcc_unreachable ();
    }
  return NULL;
}

/* Estimate number of instructions that will be created by expanding EXPR.  */

int
estimate_num_insns (tree expr)
{
  int num = 0;
  struct pointer_set_t *visited_nodes;
  basic_block bb;
  block_stmt_iterator bsi;
  struct function *my_function;

  /* If we're given an entire function, walk the CFG.  */
  if (TREE_CODE (expr) == FUNCTION_DECL)
    {
      my_function = DECL_STRUCT_FUNCTION (expr);
      gcc_assert (my_function && my_function->cfg);
      visited_nodes = pointer_set_create ();
      FOR_EACH_BB_FN (bb, my_function)
	{
	  for (bsi = bsi_start (bb);
	       !bsi_end_p (bsi);
	       bsi_next (&bsi))
	    {
	      walk_tree (bsi_stmt_ptr (bsi), estimate_num_insns_1,
			 &num, visited_nodes);
	    }
	}
      pointer_set_destroy (visited_nodes);
    }
  else
    walk_tree_without_duplicates (&expr, estimate_num_insns_1, &num);

  return num;
}

typedef struct function *function_p;

DEF_VEC_P(function_p);
DEF_VEC_ALLOC_P(function_p,heap);

/* Initialized with NOGC, making this poisonous to the garbage collector.  */
static VEC(function_p,heap) *cfun_stack;

void
push_cfun (struct function *new_cfun)
{
  VEC_safe_push (function_p, heap, cfun_stack, cfun);
  cfun = new_cfun;
}

void
pop_cfun (void)
{
  cfun = VEC_pop (function_p, cfun_stack);
}

/* Install new lexical TREE_BLOCK underneath 'current_block'.  */
static void
add_lexical_block (tree current_block, tree new_block)
{
  tree *blk_p;

  /* Walk to the last sub-block.  */
  for (blk_p = &BLOCK_SUBBLOCKS (current_block);
       *blk_p;
       blk_p = &TREE_CHAIN (*blk_p))
    ;
  *blk_p = new_block;
  BLOCK_SUPERCONTEXT (new_block) = current_block;
}

/* If *TP is a CALL_EXPR, replace it with its inline expansion.  */

static bool
expand_call_inline (basic_block bb, tree stmt, tree *tp, void *data)
{
  copy_body_data *id;
  tree t;
  tree use_retvar;
  tree fn;
  splay_tree st;
  tree args;
  tree return_slot_addr;
  tree modify_dest;
  location_t saved_location;
  struct cgraph_edge *cg_edge;
  const char *reason;
  basic_block return_block;
  edge e;
  block_stmt_iterator bsi, stmt_bsi;
  bool successfully_inlined = FALSE;
  bool purge_dead_abnormal_edges;
  tree t_step;
  tree var;

  /* See what we've got.  */
  id = (copy_body_data *) data;
  t = *tp;

  /* Set input_location here so we get the right instantiation context
     if we call instantiate_decl from inlinable_function_p.  */
  saved_location = input_location;
  if (EXPR_HAS_LOCATION (t))
    input_location = EXPR_LOCATION (t);

  /* From here on, we're only interested in CALL_EXPRs.  */
  if (TREE_CODE (t) != CALL_EXPR)
    goto egress;

  /* First, see if we can figure out what function is being called.
     If we cannot, then there is no hope of inlining the function.  */
  fn = get_callee_fndecl (t);
  if (!fn)
    goto egress;

  /* Turn forward declarations into real ones.  */
  fn = cgraph_node (fn)->decl;

  /* If fn is a declaration of a function in a nested scope that was
     globally declared inline, we don't set its DECL_INITIAL.
     However, we can't blindly follow DECL_ABSTRACT_ORIGIN because the
     C++ front-end uses it for cdtors to refer to their internal
     declarations, that are not real functions.  Fortunately those
     don't have trees to be saved, so we can tell by checking their
     DECL_SAVED_TREE.  */
  if (! DECL_INITIAL (fn)
      && DECL_ABSTRACT_ORIGIN (fn)
      && DECL_SAVED_TREE (DECL_ABSTRACT_ORIGIN (fn)))
    fn = DECL_ABSTRACT_ORIGIN (fn);

  /* Objective C and fortran still calls tree_rest_of_compilation directly.
     Kill this check once this is fixed.  */
  if (!id->dst_node->analyzed)
    goto egress;

  cg_edge = cgraph_edge (id->dst_node, stmt);

  /* Constant propagation on argument done during previous inlining
     may create new direct call.  Produce an edge for it.  */
  if (!cg_edge)
    {
      struct cgraph_node *dest = cgraph_node (fn);

      /* We have missing edge in the callgraph.  This can happen in one case
         where previous inlining turned indirect call into direct call by
         constant propagating arguments.  In all other cases we hit a bug
         (incorrect node sharing is most common reason for missing edges.  */
      gcc_assert (dest->needed || !flag_unit_at_a_time);
      cgraph_create_edge (id->dst_node, dest, stmt,
			  bb->count, bb->loop_depth)->inline_failed
	= N_("originally indirect function call not considered for inlining");
      goto egress;
    }

  /* Don't try to inline functions that are not well-suited to
     inlining.  */
  if (!cgraph_inline_p (cg_edge, &reason))
    {
      if (lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn))
	  /* Avoid warnings during early inline pass. */
	  && (!flag_unit_at_a_time || cgraph_global_info_ready))
	{
	  sorry ("inlining failed in call to %q+F: %s", fn, reason);
	  sorry ("called from here");
	}
      else if (warn_inline && DECL_DECLARED_INLINE_P (fn)
	       && !DECL_IN_SYSTEM_HEADER (fn)
	       && strlen (reason)
	       && !lookup_attribute ("noinline", DECL_ATTRIBUTES (fn))
	       /* Avoid warnings during early inline pass. */
	       && (!flag_unit_at_a_time || cgraph_global_info_ready))
	{
	  warning (OPT_Winline, "inlining failed in call to %q+F: %s",
		   fn, reason);
	  warning (OPT_Winline, "called from here");
	}
      goto egress;
    }
  fn = cg_edge->callee->decl;

#ifdef ENABLE_CHECKING
  if (cg_edge->callee->decl != id->dst_node->decl)
    verify_cgraph_node (cg_edge->callee);
#endif

  /* We will be inlining this callee.  */
  id->eh_region = lookup_stmt_eh_region (stmt);

  /* Split the block holding the CALL_EXPR.  */
  e = split_block (bb, stmt);
  bb = e->src;
  return_block = e->dest;
  remove_edge (e);

  /* split_block splits after the statement; work around this by
     moving the call into the second block manually.  Not pretty,
     but seems easier than doing the CFG manipulation by hand
     when the CALL_EXPR is in the last statement of BB.  */
  stmt_bsi = bsi_last (bb);
  bsi_remove (&stmt_bsi, false);

  /* If the CALL_EXPR was in the last statement of BB, it may have
     been the source of abnormal edges.  In this case, schedule
     the removal of dead abnormal edges.  */
  bsi = bsi_start (return_block);
  if (bsi_end_p (bsi))
    {
      bsi_insert_after (&bsi, stmt, BSI_NEW_STMT);
      purge_dead_abnormal_edges = true;
    }
  else
    {
      bsi_insert_before (&bsi, stmt, BSI_NEW_STMT);
      purge_dead_abnormal_edges = false;
    }

  stmt_bsi = bsi_start (return_block);

  /* Build a block containing code to initialize the arguments, the
     actual inline expansion of the body, and a label for the return
     statements within the function to jump to.  The type of the
     statement expression is the return type of the function call.  */
  id->block = make_node (BLOCK);
  BLOCK_ABSTRACT_ORIGIN (id->block) = fn;
  BLOCK_SOURCE_LOCATION (id->block) = input_location;
  add_lexical_block (TREE_BLOCK (stmt), id->block);

  /* Local declarations will be replaced by their equivalents in this
     map.  */
  st = id->decl_map;
  id->decl_map = splay_tree_new (splay_tree_compare_pointers,
				 NULL, NULL);

  /* Initialize the parameters.  */
  args = TREE_OPERAND (t, 1);

  /* Record the function we are about to inline.  */
  id->src_fn = fn;
  id->src_node = cg_edge->callee;

  initialize_inlined_parameters (id, args, TREE_OPERAND (t, 2), fn, bb);

  if (DECL_INITIAL (fn))
    add_lexical_block (id->block, remap_blocks (DECL_INITIAL (fn), id));

  /* Return statements in the function body will be replaced by jumps
     to the RET_LABEL.  */

  gcc_assert (DECL_INITIAL (fn));
  gcc_assert (TREE_CODE (DECL_INITIAL (fn)) == BLOCK);

  /* Find the lhs to which the result of this call is assigned.  */
  return_slot_addr = NULL;
  if (TREE_CODE (stmt) == MODIFY_EXPR)
    {
      modify_dest = TREE_OPERAND (stmt, 0);

      /* The function which we are inlining might not return a value,
	 in which case we should issue a warning that the function
	 does not return a value.  In that case the optimizers will
	 see that the variable to which the value is assigned was not
	 initialized.  We do not want to issue a warning about that
	 uninitialized variable.  */
      if (DECL_P (modify_dest))
	TREE_NO_WARNING (modify_dest) = 1;
      if (CALL_EXPR_RETURN_SLOT_OPT (t))
	{
	  return_slot_addr = build_fold_addr_expr (modify_dest);
	  STRIP_USELESS_TYPE_CONVERSION (return_slot_addr);
	  modify_dest = NULL;
	}
    }
  else
    modify_dest = NULL;

  /* Declare the return variable for the function.  */
  declare_return_variable (id, return_slot_addr,
			   modify_dest, &use_retvar);

  /* This is it.  Duplicate the callee body.  Assume callee is
     pre-gimplified.  Note that we must not alter the caller
     function in any way before this point, as this CALL_EXPR may be
     a self-referential call; if we're calling ourselves, we need to
     duplicate our body before altering anything.  */
  copy_body (id, bb->count, bb->frequency, bb, return_block);

  /* Add local vars in this inlined callee to caller.  */
  t_step = id->src_cfun->unexpanded_var_list;
  for (; t_step; t_step = TREE_CHAIN (t_step))
    {
      var = TREE_VALUE (t_step);
      if (TREE_STATIC (var) && !TREE_ASM_WRITTEN (var))
	cfun->unexpanded_var_list = tree_cons (NULL_TREE, var,
					       cfun->unexpanded_var_list);
      else
	cfun->unexpanded_var_list = tree_cons (NULL_TREE, remap_decl (var, id),
					       cfun->unexpanded_var_list);
    }

  /* Clean up.  */
  splay_tree_delete (id->decl_map);
  id->decl_map = st;

  /* If the inlined function returns a result that we care about,
     clobber the CALL_EXPR with a reference to the return variable.  */
  if (use_retvar && (TREE_CODE (bsi_stmt (stmt_bsi)) != CALL_EXPR))
    {
      *tp = use_retvar;
      maybe_clean_or_replace_eh_stmt (stmt, stmt);
    }
  else
    /* We're modifying a TSI owned by gimple_expand_calls_inline();
       tsi_delink() will leave the iterator in a sane state.  */
    bsi_remove (&stmt_bsi, true);

  if (purge_dead_abnormal_edges)
    tree_purge_dead_abnormal_call_edges (return_block);

  /* If the value of the new expression is ignored, that's OK.  We
     don't warn about this for CALL_EXPRs, so we shouldn't warn about
     the equivalent inlined version either.  */
  TREE_USED (*tp) = 1;

  /* Output the inlining info for this abstract function, since it has been
     inlined.  If we don't do this now, we can lose the information about the
     variables in the function when the blocks get blown away as soon as we
     remove the cgraph node.  */
  (*debug_hooks->outlining_inline_function) (cg_edge->callee->decl);

  /* Update callgraph if needed.  */
  cgraph_remove_node (cg_edge->callee);

  id->block = NULL_TREE;
  successfully_inlined = TRUE;

 egress:
  input_location = saved_location;
  return successfully_inlined;
}

/* Expand call statements reachable from STMT_P.
   We can only have CALL_EXPRs as the "toplevel" tree code or nested
   in a MODIFY_EXPR.  See tree-gimple.c:get_call_expr_in().  We can
   unfortunately not use that function here because we need a pointer
   to the CALL_EXPR, not the tree itself.  */

static bool
gimple_expand_calls_inline (basic_block bb, copy_body_data *id)
{
  block_stmt_iterator bsi;

  /* Register specific tree functions.  */
  tree_register_cfg_hooks ();
  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      tree *expr_p = bsi_stmt_ptr (bsi);
      tree stmt = *expr_p;

      if (TREE_CODE (*expr_p) == MODIFY_EXPR)
	expr_p = &TREE_OPERAND (*expr_p, 1);
      if (TREE_CODE (*expr_p) == WITH_SIZE_EXPR)
	expr_p = &TREE_OPERAND (*expr_p, 0);
      if (TREE_CODE (*expr_p) == CALL_EXPR)
	if (expand_call_inline (bb, stmt, expr_p, id))
	  return true;
    }
  return false;
}

/* Expand calls to inline functions in the body of FN.  */

void
optimize_inline_calls (tree fn)
{
  copy_body_data id;
  tree prev_fn;
  basic_block bb;
  /* There is no point in performing inlining if errors have already
     occurred -- and we might crash if we try to inline invalid
     code.  */
  if (errorcount || sorrycount)
    return;

  /* Clear out ID.  */
  memset (&id, 0, sizeof (id));

  id.src_node = id.dst_node = cgraph_node (fn);
  id.dst_fn = fn;
  /* Or any functions that aren't finished yet.  */
  prev_fn = NULL_TREE;
  if (current_function_decl)
    {
      id.dst_fn = current_function_decl;
      prev_fn = current_function_decl;
    }

  id.copy_decl = copy_decl_maybe_to_var;
  id.transform_call_graph_edges = CB_CGE_DUPLICATE;
  id.transform_new_cfg = false;
  id.transform_return_to_modify = true;
  id.transform_lang_insert_block = false;

  push_gimplify_context ();

  /* Reach the trees by walking over the CFG, and note the
     enclosing basic-blocks in the call edges.  */
  /* We walk the blocks going forward, because inlined function bodies
     will split id->current_basic_block, and the new blocks will
     follow it; we'll trudge through them, processing their CALL_EXPRs
     along the way.  */
  FOR_EACH_BB (bb)
    gimple_expand_calls_inline (bb, &id);

  pop_gimplify_context (NULL);
  /* Renumber the (code) basic_blocks consecutively.  */
  compact_blocks ();
  /* Renumber the lexical scoping (non-code) blocks consecutively.  */
  number_blocks (fn);

#ifdef ENABLE_CHECKING
    {
      struct cgraph_edge *e;

      verify_cgraph_node (id.dst_node);

      /* Double check that we inlined everything we are supposed to inline.  */
      for (e = id.dst_node->callees; e; e = e->next_callee)
	gcc_assert (e->inline_failed);
    }
#endif
  /* We need to rescale frequencies again to peak at REG_BR_PROB_BASE
     as inlining loops might increase the maximum.  */
  if (ENTRY_BLOCK_PTR->count)
    counts_to_freqs ();
  fold_cond_expr_cond ();
}

/* FN is a function that has a complete body, and CLONE is a function whose
   body is to be set to a copy of FN, mapping argument declarations according
   to the ARG_MAP splay_tree.  */

void
clone_body (tree clone, tree fn, void *arg_map)
{
  copy_body_data id;

  /* Clone the body, as if we were making an inline call.  But, remap the
     parameters in the callee to the parameters of caller.  */
  memset (&id, 0, sizeof (id));
  id.src_fn = fn;
  id.dst_fn = clone;
  id.src_cfun = DECL_STRUCT_FUNCTION (fn);
  id.decl_map = (splay_tree)arg_map;

  id.copy_decl = copy_decl_no_change;
  id.transform_call_graph_edges = CB_CGE_DUPLICATE;
  id.transform_new_cfg = true;
  id.transform_return_to_modify = false;
  id.transform_lang_insert_block = true;

  /* We're not inside any EH region.  */
  id.eh_region = -1;

  /* Actually copy the body.  */
  append_to_statement_list_force (copy_generic_body (&id), &DECL_SAVED_TREE (clone));
}

/* Passed to walk_tree.  Copies the node pointed to, if appropriate.  */

tree
copy_tree_r (tree *tp, int *walk_subtrees, void *data ATTRIBUTE_UNUSED)
{
  enum tree_code code = TREE_CODE (*tp);

  /* We make copies of most nodes.  */
  if (IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (code))
      || code == TREE_LIST
      || code == TREE_VEC
      || code == TYPE_DECL
      || code == OMP_CLAUSE)
    {
      /* Because the chain gets clobbered when we make a copy, we save it
	 here.  */
      tree chain = TREE_CHAIN (*tp);
      tree new;

      /* Copy the node.  */
      new = copy_node (*tp);

      /* Propagate mudflap marked-ness.  */
      if (flag_mudflap && mf_marked_p (*tp))
        mf_mark (new);

      *tp = new;

      /* Now, restore the chain, if appropriate.  That will cause
	 walk_tree to walk into the chain as well.  */
      if (code == PARM_DECL
	  || code == TREE_LIST
	  || code == OMP_CLAUSE)
	TREE_CHAIN (*tp) = chain;

      /* For now, we don't update BLOCKs when we make copies.  So, we
	 have to nullify all BIND_EXPRs.  */
      if (TREE_CODE (*tp) == BIND_EXPR)
	BIND_EXPR_BLOCK (*tp) = NULL_TREE;
    }
  else if (code == CONSTRUCTOR)
    {
      /* CONSTRUCTOR nodes need special handling because
         we need to duplicate the vector of elements.  */
      tree new;

      new = copy_node (*tp);

      /* Propagate mudflap marked-ness.  */
      if (flag_mudflap && mf_marked_p (*tp))
        mf_mark (new);

      CONSTRUCTOR_ELTS (new) = VEC_copy (constructor_elt, gc,
					 CONSTRUCTOR_ELTS (*tp));
      *tp = new;
    }
  else if (TREE_CODE_CLASS (code) == tcc_type)
    *walk_subtrees = 0;
  else if (TREE_CODE_CLASS (code) == tcc_declaration)
    *walk_subtrees = 0;
  else if (TREE_CODE_CLASS (code) == tcc_constant)
    *walk_subtrees = 0;
  else
    gcc_assert (code != STATEMENT_LIST);
  return NULL_TREE;
}

/* The SAVE_EXPR pointed to by TP is being copied.  If ST contains
   information indicating to what new SAVE_EXPR this one should be mapped,
   use that one.  Otherwise, create a new node and enter it in ST.  FN is
   the function into which the copy will be placed.  */

static void
remap_save_expr (tree *tp, void *st_, int *walk_subtrees)
{
  splay_tree st = (splay_tree) st_;
  splay_tree_node n;
  tree t;

  /* See if we already encountered this SAVE_EXPR.  */
  n = splay_tree_lookup (st, (splay_tree_key) *tp);

  /* If we didn't already remap this SAVE_EXPR, do so now.  */
  if (!n)
    {
      t = copy_node (*tp);

      /* Remember this SAVE_EXPR.  */
      splay_tree_insert (st, (splay_tree_key) *tp, (splay_tree_value) t);
      /* Make sure we don't remap an already-remapped SAVE_EXPR.  */
      splay_tree_insert (st, (splay_tree_key) t, (splay_tree_value) t);
    }
  else
    {
      /* We've already walked into this SAVE_EXPR; don't do it again.  */
      *walk_subtrees = 0;
      t = (tree) n->value;
    }

  /* Replace this SAVE_EXPR with the copy.  */
  *tp = t;
}

/* Called via walk_tree.  If *TP points to a DECL_STMT for a local label,
   copies the declaration and enters it in the splay_tree in DATA (which is
   really an `copy_body_data *').  */

static tree
mark_local_for_remap_r (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			void *data)
{
  copy_body_data *id = (copy_body_data *) data;

  /* Don't walk into types.  */
  if (TYPE_P (*tp))
    *walk_subtrees = 0;

  else if (TREE_CODE (*tp) == LABEL_EXPR)
    {
      tree decl = TREE_OPERAND (*tp, 0);

      /* Copy the decl and remember the copy.  */
      insert_decl_map (id, decl, id->copy_decl (decl, id));
    }

  return NULL_TREE;
}

/* Perform any modifications to EXPR required when it is unsaved.  Does
   not recurse into EXPR's subtrees.  */

static void
unsave_expr_1 (tree expr)
{
  switch (TREE_CODE (expr))
    {
    case TARGET_EXPR:
      /* Don't mess with a TARGET_EXPR that hasn't been expanded.
         It's OK for this to happen if it was part of a subtree that
         isn't immediately expanded, such as operand 2 of another
         TARGET_EXPR.  */
      if (TREE_OPERAND (expr, 1))
	break;

      TREE_OPERAND (expr, 1) = TREE_OPERAND (expr, 3);
      TREE_OPERAND (expr, 3) = NULL_TREE;
      break;

    default:
      break;
    }
}

/* Called via walk_tree when an expression is unsaved.  Using the
   splay_tree pointed to by ST (which is really a `splay_tree'),
   remaps all local declarations to appropriate replacements.  */

static tree
unsave_r (tree *tp, int *walk_subtrees, void *data)
{
  copy_body_data *id = (copy_body_data *) data;
  splay_tree st = id->decl_map;
  splay_tree_node n;

  /* Only a local declaration (variable or label).  */
  if ((TREE_CODE (*tp) == VAR_DECL && !TREE_STATIC (*tp))
      || TREE_CODE (*tp) == LABEL_DECL)
    {
      /* Lookup the declaration.  */
      n = splay_tree_lookup (st, (splay_tree_key) *tp);

      /* If it's there, remap it.  */
      if (n)
	*tp = (tree) n->value;
    }

  else if (TREE_CODE (*tp) == STATEMENT_LIST)
    copy_statement_list (tp);
  else if (TREE_CODE (*tp) == BIND_EXPR)
    copy_bind_expr (tp, walk_subtrees, id);
  else if (TREE_CODE (*tp) == SAVE_EXPR)
    remap_save_expr (tp, st, walk_subtrees);
  else
    {
      copy_tree_r (tp, walk_subtrees, NULL);

      /* Do whatever unsaving is required.  */
      unsave_expr_1 (*tp);
    }

  /* Keep iterating.  */
  return NULL_TREE;
}

/* Copies everything in EXPR and replaces variables, labels
   and SAVE_EXPRs local to EXPR.  */

tree
unsave_expr_now (tree expr)
{
  copy_body_data id;

  /* There's nothing to do for NULL_TREE.  */
  if (expr == 0)
    return expr;

  /* Set up ID.  */
  memset (&id, 0, sizeof (id));
  id.src_fn = current_function_decl;
  id.dst_fn = current_function_decl;
  id.decl_map = splay_tree_new (splay_tree_compare_pointers, NULL, NULL);

  id.copy_decl = copy_decl_no_change;
  id.transform_call_graph_edges = CB_CGE_DUPLICATE;
  id.transform_new_cfg = false;
  id.transform_return_to_modify = false;
  id.transform_lang_insert_block = false;

  /* Walk the tree once to find local labels.  */
  walk_tree_without_duplicates (&expr, mark_local_for_remap_r, &id);

  /* Walk the tree again, copying, remapping, and unsaving.  */
  walk_tree (&expr, unsave_r, &id, NULL);

  /* Clean up.  */
  splay_tree_delete (id.decl_map);

  return expr;
}

/* Allow someone to determine if SEARCH is a child of TOP from gdb.  */

static tree
debug_find_tree_1 (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED, void *data)
{
  if (*tp == data)
    return (tree) data;
  else
    return NULL;
}

bool
debug_find_tree (tree top, tree search)
{
  return walk_tree_without_duplicates (&top, debug_find_tree_1, search) != 0;
}


/* Declare the variables created by the inliner.  Add all the variables in
   VARS to BIND_EXPR.  */

static void
declare_inline_vars (tree block, tree vars)
{
  tree t;
  for (t = vars; t; t = TREE_CHAIN (t))
    {
      DECL_SEEN_IN_BIND_EXPR_P (t) = 1;
      gcc_assert (!TREE_STATIC (t) && !TREE_ASM_WRITTEN (t));
      cfun->unexpanded_var_list =
	tree_cons (NULL_TREE, t,
		   cfun->unexpanded_var_list);
    }

  if (block)
    BLOCK_VARS (block) = chainon (BLOCK_VARS (block), vars);
}


/* Copy NODE (which must be a DECL).  The DECL originally was in the FROM_FN,
   but now it will be in the TO_FN.  PARM_TO_VAR means enable PARM_DECL to
   VAR_DECL translation.  */

static tree
copy_decl_for_dup_finish (copy_body_data *id, tree decl, tree copy)
{
  /* Don't generate debug information for the copy if we wouldn't have
     generated it for the copy either.  */
  DECL_ARTIFICIAL (copy) = DECL_ARTIFICIAL (decl);
  DECL_IGNORED_P (copy) = DECL_IGNORED_P (decl);

  /* Set the DECL_ABSTRACT_ORIGIN so the debugging routines know what
     declaration inspired this copy.  */ 
  DECL_ABSTRACT_ORIGIN (copy) = DECL_ORIGIN (decl);

  /* The new variable/label has no RTL, yet.  */
  if (CODE_CONTAINS_STRUCT (TREE_CODE (copy), TS_DECL_WRTL)
      && !TREE_STATIC (copy) && !DECL_EXTERNAL (copy))
    SET_DECL_RTL (copy, NULL_RTX);
  
  /* These args would always appear unused, if not for this.  */
  TREE_USED (copy) = 1;

  /* Set the context for the new declaration.  */
  if (!DECL_CONTEXT (decl))
    /* Globals stay global.  */
    ;
  else if (DECL_CONTEXT (decl) != id->src_fn)
    /* Things that weren't in the scope of the function we're inlining
       from aren't in the scope we're inlining to, either.  */
    ;
  else if (TREE_STATIC (decl))
    /* Function-scoped static variables should stay in the original
       function.  */
    ;
  else
    /* Ordinary automatic local variables are now in the scope of the
       new function.  */
    DECL_CONTEXT (copy) = id->dst_fn;

  return copy;
}

static tree
copy_decl_to_var (tree decl, copy_body_data *id)
{
  tree copy, type;

  gcc_assert (TREE_CODE (decl) == PARM_DECL
	      || TREE_CODE (decl) == RESULT_DECL);

  type = TREE_TYPE (decl);

  copy = build_decl (VAR_DECL, DECL_NAME (decl), type);
  TREE_ADDRESSABLE (copy) = TREE_ADDRESSABLE (decl);
  TREE_READONLY (copy) = TREE_READONLY (decl);
  TREE_THIS_VOLATILE (copy) = TREE_THIS_VOLATILE (decl);
  DECL_COMPLEX_GIMPLE_REG_P (copy) = DECL_COMPLEX_GIMPLE_REG_P (decl);

  return copy_decl_for_dup_finish (id, decl, copy);
}

/* Like copy_decl_to_var, but create a return slot object instead of a
   pointer variable for return by invisible reference.  */

static tree
copy_result_decl_to_var (tree decl, copy_body_data *id)
{
  tree copy, type;

  gcc_assert (TREE_CODE (decl) == PARM_DECL
	      || TREE_CODE (decl) == RESULT_DECL);

  type = TREE_TYPE (decl);
  if (DECL_BY_REFERENCE (decl))
    type = TREE_TYPE (type);

  copy = build_decl (VAR_DECL, DECL_NAME (decl), type);
  TREE_READONLY (copy) = TREE_READONLY (decl);
  TREE_THIS_VOLATILE (copy) = TREE_THIS_VOLATILE (decl);
  if (!DECL_BY_REFERENCE (decl))
    {
      TREE_ADDRESSABLE (copy) = TREE_ADDRESSABLE (decl);
      DECL_COMPLEX_GIMPLE_REG_P (copy) = DECL_COMPLEX_GIMPLE_REG_P (decl);
    }

  return copy_decl_for_dup_finish (id, decl, copy);
}


static tree
copy_decl_no_change (tree decl, copy_body_data *id)
{
  tree copy;

  copy = copy_node (decl);

  /* The COPY is not abstract; it will be generated in DST_FN.  */
  DECL_ABSTRACT (copy) = 0;
  lang_hooks.dup_lang_specific_decl (copy);

  /* TREE_ADDRESSABLE isn't used to indicate that a label's address has
     been taken; it's for internal bookkeeping in expand_goto_internal.  */
  if (TREE_CODE (copy) == LABEL_DECL)
    {
      TREE_ADDRESSABLE (copy) = 0;
      LABEL_DECL_UID (copy) = -1;
    }

  return copy_decl_for_dup_finish (id, decl, copy);
}

static tree
copy_decl_maybe_to_var (tree decl, copy_body_data *id)
{
  if (TREE_CODE (decl) == PARM_DECL || TREE_CODE (decl) == RESULT_DECL)
    return copy_decl_to_var (decl, id);
  else
    return copy_decl_no_change (decl, id);
}

/* Return a copy of the function's argument tree.  */
static tree
copy_arguments_for_versioning (tree orig_parm, copy_body_data * id)
{
  tree *arg_copy, *parg;

  arg_copy = &orig_parm;
  for (parg = arg_copy; *parg; parg = &TREE_CHAIN (*parg))
    {
      tree new = remap_decl (*parg, id);
      lang_hooks.dup_lang_specific_decl (new);
      TREE_CHAIN (new) = TREE_CHAIN (*parg);
      *parg = new;
    }
  return orig_parm;
}

/* Return a copy of the function's static chain.  */
static tree
copy_static_chain (tree static_chain, copy_body_data * id)
{
  tree *chain_copy, *pvar;

  chain_copy = &static_chain;
  for (pvar = chain_copy; *pvar; pvar = &TREE_CHAIN (*pvar))
    {
      tree new = remap_decl (*pvar, id);
      lang_hooks.dup_lang_specific_decl (new);
      TREE_CHAIN (new) = TREE_CHAIN (*pvar);
      *pvar = new;
    }
  return static_chain;
}

/* Return true if the function is allowed to be versioned.
   This is a guard for the versioning functionality.  */
bool
tree_versionable_function_p (tree fndecl)
{
  if (fndecl == NULL_TREE)
    return false;
  /* ??? There are cases where a function is
     uninlinable but can be versioned.  */
  if (!tree_inlinable_function_p (fndecl))
    return false;
  
  return true;
}

/* Create a copy of a function's tree.
   OLD_DECL and NEW_DECL are FUNCTION_DECL tree nodes
   of the original function and the new copied function
   respectively.  In case we want to replace a DECL 
   tree with another tree while duplicating the function's 
   body, TREE_MAP represents the mapping between these 
   trees. If UPDATE_CLONES is set, the call_stmt fields
   of edges of clones of the function will be updated.  */
void
tree_function_versioning (tree old_decl, tree new_decl, varray_type tree_map,
			  bool update_clones)
{
  struct cgraph_node *old_version_node;
  struct cgraph_node *new_version_node;
  copy_body_data id;
  tree p, new_fndecl;
  unsigned i;
  struct ipa_replace_map *replace_info;
  basic_block old_entry_block;
  tree t_step;

  gcc_assert (TREE_CODE (old_decl) == FUNCTION_DECL
	      && TREE_CODE (new_decl) == FUNCTION_DECL);
  DECL_POSSIBLY_INLINED (old_decl) = 1;

  old_version_node = cgraph_node (old_decl);
  new_version_node = cgraph_node (new_decl);

  allocate_struct_function (new_decl);
  /* Cfun points to the new allocated function struct at this point.  */
  cfun->function_end_locus = DECL_SOURCE_LOCATION (new_decl);

  DECL_ARTIFICIAL (new_decl) = 1;
  DECL_ABSTRACT_ORIGIN (new_decl) = DECL_ORIGIN (old_decl);

  /* Generate a new name for the new version. */
  if (!update_clones)
    DECL_NAME (new_decl) = create_tmp_var_name (NULL);
  /* Create a new SYMBOL_REF rtx for the new name. */
  if (DECL_RTL (old_decl) != NULL)
    {
      SET_DECL_RTL (new_decl, copy_rtx (DECL_RTL (old_decl)));
      XEXP (DECL_RTL (new_decl), 0) =
	gen_rtx_SYMBOL_REF (GET_MODE (XEXP (DECL_RTL (old_decl), 0)),
			    IDENTIFIER_POINTER (DECL_NAME (new_decl)));
    }

  /* Prepare the data structures for the tree copy.  */
  memset (&id, 0, sizeof (id));
  
  id.decl_map = splay_tree_new (splay_tree_compare_pointers, NULL, NULL);
  id.src_fn = old_decl;
  id.dst_fn = new_decl;
  id.src_node = old_version_node;
  id.dst_node = new_version_node;
  id.src_cfun = DECL_STRUCT_FUNCTION (old_decl);
  
  id.copy_decl = copy_decl_no_change;
  id.transform_call_graph_edges
    = update_clones ? CB_CGE_MOVE_CLONES : CB_CGE_MOVE;
  id.transform_new_cfg = true;
  id.transform_return_to_modify = false;
  id.transform_lang_insert_block = false;

  current_function_decl = new_decl;
  
  /* Copy the function's static chain.  */
  p = DECL_STRUCT_FUNCTION (old_decl)->static_chain_decl;
  if (p)
    DECL_STRUCT_FUNCTION (new_decl)->static_chain_decl =
      copy_static_chain (DECL_STRUCT_FUNCTION (old_decl)->static_chain_decl,
			 &id);
  /* Copy the function's arguments.  */
  if (DECL_ARGUMENTS (old_decl) != NULL_TREE)
    DECL_ARGUMENTS (new_decl) =
      copy_arguments_for_versioning (DECL_ARGUMENTS (old_decl), &id);
  
  /* If there's a tree_map, prepare for substitution.  */
  if (tree_map)
    for (i = 0; i < VARRAY_ACTIVE_SIZE (tree_map); i++)
      {
	replace_info = VARRAY_GENERIC_PTR (tree_map, i);
	if (replace_info->replace_p)
	  insert_decl_map (&id, replace_info->old_tree,
			   replace_info->new_tree);
      }
  
  DECL_INITIAL (new_decl) = remap_blocks (DECL_INITIAL (id.src_fn), &id);
  
  /* Renumber the lexical scoping (non-code) blocks consecutively.  */
  number_blocks (id.dst_fn);
  
  if (DECL_STRUCT_FUNCTION (old_decl)->unexpanded_var_list != NULL_TREE)
    /* Add local vars.  */
    for (t_step = DECL_STRUCT_FUNCTION (old_decl)->unexpanded_var_list;
	 t_step; t_step = TREE_CHAIN (t_step))
      {
	tree var = TREE_VALUE (t_step);
	if (TREE_STATIC (var) && !TREE_ASM_WRITTEN (var))
	  cfun->unexpanded_var_list = tree_cons (NULL_TREE, var,
						 cfun->unexpanded_var_list);
	else
	  cfun->unexpanded_var_list =
	    tree_cons (NULL_TREE, remap_decl (var, &id),
		       cfun->unexpanded_var_list);
      }
  
  /* Copy the Function's body.  */
  old_entry_block = ENTRY_BLOCK_PTR_FOR_FUNCTION
    (DECL_STRUCT_FUNCTION (old_decl));
  new_fndecl = copy_body (&id,
			  old_entry_block->count,
			  old_entry_block->frequency, NULL, NULL);
  
  DECL_SAVED_TREE (new_decl) = DECL_SAVED_TREE (new_fndecl);

  DECL_STRUCT_FUNCTION (new_decl)->cfg =
    DECL_STRUCT_FUNCTION (new_fndecl)->cfg;
  DECL_STRUCT_FUNCTION (new_decl)->eh = DECL_STRUCT_FUNCTION (new_fndecl)->eh;
  DECL_STRUCT_FUNCTION (new_decl)->ib_boundaries_block =
    DECL_STRUCT_FUNCTION (new_fndecl)->ib_boundaries_block;
  DECL_STRUCT_FUNCTION (new_decl)->last_label_uid =
    DECL_STRUCT_FUNCTION (new_fndecl)->last_label_uid;

  if (DECL_RESULT (old_decl) != NULL_TREE)
    {
      tree *res_decl = &DECL_RESULT (old_decl);
      DECL_RESULT (new_decl) = remap_decl (*res_decl, &id);
      lang_hooks.dup_lang_specific_decl (DECL_RESULT (new_decl));
    }
  
  current_function_decl = NULL;
  /* Renumber the lexical scoping (non-code) blocks consecutively.  */
  number_blocks (new_decl);

  /* Clean up.  */
  splay_tree_delete (id.decl_map);
  fold_cond_expr_cond ();
  return;
}

/* Duplicate a type, fields and all.  */

tree
build_duplicate_type (tree type)
{
  struct copy_body_data id;

  memset (&id, 0, sizeof (id));
  id.src_fn = current_function_decl;
  id.dst_fn = current_function_decl;
  id.src_cfun = cfun;
  id.decl_map = splay_tree_new (splay_tree_compare_pointers, NULL, NULL);

  type = remap_type_1 (type, &id);

  splay_tree_delete (id.decl_map);

  return type;
}
