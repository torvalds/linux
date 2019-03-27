/* Callgraph based analysis of static variables.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
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
02110-1301, USA.  
*/

/* This file gathers information about how variables whose scope is
   confined to the compilation unit are used.  

   There are two categories of information produced by this pass:

   1) The addressable (TREE_ADDRESSABLE) bit and readonly
   (TREE_READONLY) bit associated with these variables is properly set
   based on scanning all of the code withing the compilation unit.

   2) The transitive call site specific clobber effects are computed
   for the variables whose scope is contained within this compilation
   unit.

   First each function and static variable initialization is analyzed
   to determine which local static variables are either read, written,
   or have their address taken.  Any local static that has its address
   taken is removed from consideration.  Once the local read and
   writes are determined, a transitive closure of this information is
   performed over the call graph to determine the worst case set of
   side effects of each call.  In later parts of the compiler, these
   local and global sets are examined to make the call clobbering less
   traumatic, promote some statics to registers, and improve aliasing
   information.
   
   Currently must be run after inlining decisions have been made since
   otherwise, the local sets will not contain information that is
   consistent with post inlined state.  The global sets are not prone
   to this problem since they are by definition transitive.  
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "tree-pass.h"
#include "langhooks.h"
#include "pointer-set.h"
#include "ggc.h"
#include "ipa-utils.h"
#include "ipa-reference.h"
#include "c-common.h"
#include "tree-gimple.h"
#include "cgraph.h"
#include "output.h"
#include "flags.h"
#include "timevar.h"
#include "diagnostic.h"
#include "langhooks.h"

/* This splay tree contains all of the static variables that are
   being considered by the compilation level alias analysis.  For
   module_at_a_time compilation, this is the set of static but not
   public variables.  Any variables that either have their address
   taken or participate in otherwise unsavory operations are deleted
   from this list.  */
static GTY((param1_is(int), param2_is(tree)))
     splay_tree reference_vars_to_consider;

/* This bitmap is used to knock out the module static variables whose
   addresses have been taken and passed around.  */
static bitmap module_statics_escape;

/* This bitmap is used to knock out the module static variables that
   are not readonly.  */
static bitmap module_statics_written;

/* A bit is set for every module static we are considering.  This is
   ored into the local info when asm code is found that clobbers all
   memory. */
static bitmap all_module_statics;

static struct pointer_set_t *visited_nodes;

static bitmap_obstack ipa_obstack;

enum initialization_status_t
{
  UNINITIALIZED,
  RUNNING,
  FINISHED
};

tree memory_identifier_string;

/* Return the ipa_reference_vars structure starting from the cgraph NODE.  */
static inline ipa_reference_vars_info_t
get_reference_vars_info_from_cgraph (struct cgraph_node * node)
{
  return get_function_ann (node->decl)->reference_vars_info;
}

/* Get a bitmap that contains all of the locally referenced static
   variables for function FN.  */
static ipa_reference_local_vars_info_t
get_local_reference_vars_info (tree fn) 
{
  ipa_reference_vars_info_t info = get_function_ann (fn)->reference_vars_info;

  if (info)
    return info->local;
  else
    /* This phase was not run.  */ 
    return NULL;
}

/* Get a bitmap that contains all of the globally referenced static
   variables for function FN.  */
 
static ipa_reference_global_vars_info_t
get_global_reference_vars_info (tree fn) 
{
  ipa_reference_vars_info_t info = get_function_ann (fn)->reference_vars_info;

  if (info)
    return info->global;
  else
    /* This phase was not run.  */ 
    return NULL;
}

/* Return a bitmap indexed by VAR_DECL uid for the static variables
   that may be read locally by the execution of the function fn.
   Returns NULL if no data is available.  */

bitmap 
ipa_reference_get_read_local (tree fn)
{
  ipa_reference_local_vars_info_t l = get_local_reference_vars_info (fn);
  if (l) 
    return l->statics_read;
  else
    return NULL;
}

/* Return a bitmap indexed by VAR_DECL uid for the static variables
   that may be written locally by the execution of the function fn.
   Returns NULL if no data is available.  */

bitmap 
ipa_reference_get_written_local (tree fn)
{
  ipa_reference_local_vars_info_t l = get_local_reference_vars_info (fn);
  if (l) 
    return l->statics_written;
  else
    return NULL;
}

/* Return a bitmap indexed by VAR_DECL uid for the static variables
   that are read during the execution of the function FN.  Returns
   NULL if no data is available.  */

bitmap 
ipa_reference_get_read_global (tree fn) 
{
  ipa_reference_global_vars_info_t g = get_global_reference_vars_info (fn);
  if (g) 
    return g->statics_read;
  else
    return NULL;
}

/* Return a bitmap indexed by VAR_DECL uid for the static variables
   that are written during the execution of the function FN.  Note
   that variables written may or may not be read during the function
   call.  Returns NULL if no data is available.  */

bitmap 
ipa_reference_get_written_global (tree fn) 
{
  ipa_reference_global_vars_info_t g = get_global_reference_vars_info (fn);
  if (g) 
    return g->statics_written;
  else
    return NULL;
}

/* Return a bitmap indexed by_DECL_UID uid for the static variables
   that are not read during the execution of the function FN.  Returns
   NULL if no data is available.  */

bitmap 
ipa_reference_get_not_read_global (tree fn) 
{
  ipa_reference_global_vars_info_t g = get_global_reference_vars_info (fn);
  if (g) 
    return g->statics_not_read;
  else
    return NULL;
}

/* Return a bitmap indexed by DECL_UID uid for the static variables
   that are not written during the execution of the function FN.  Note
   that variables written may or may not be read during the function
   call.  Returns NULL if no data is available.  */

bitmap 
ipa_reference_get_not_written_global (tree fn) 
{
  ipa_reference_global_vars_info_t g = get_global_reference_vars_info (fn);
  if (g) 
    return g->statics_not_written;
  else
    return NULL;
}



/* Add VAR to all_module_statics and the two
   reference_vars_to_consider* sets.  */

static inline void 
add_static_var (tree var) 
{
  int uid = DECL_UID (var);
  if (!bitmap_bit_p (all_module_statics, uid))
    {
      splay_tree_insert (reference_vars_to_consider,
			 uid, (splay_tree_value)var);
      bitmap_set_bit (all_module_statics, uid);
    }
}

/* Return true if the variable T is the right kind of static variable to
   perform compilation unit scope escape analysis.  */

static inline bool 
has_proper_scope_for_analysis (tree t)
{
  /* If the variable has the "used" attribute, treat it as if it had a
     been touched by the devil.  */
  if (lookup_attribute ("used", DECL_ATTRIBUTES (t)))
    return false;

  /* Do not want to do anything with volatile except mark any
     function that uses one to be not const or pure.  */
  if (TREE_THIS_VOLATILE (t)) 
    return false;

  /* Do not care about a local automatic that is not static.  */
  if (!TREE_STATIC (t) && !DECL_EXTERNAL (t))
    return false;

  if (DECL_EXTERNAL (t) || TREE_PUBLIC (t))
    return false;

  /* This is a variable we care about.  Check if we have seen it
     before, and if not add it the set of variables we care about.  */
  if (!bitmap_bit_p (all_module_statics, DECL_UID (t)))
    add_static_var (t);

  return true;
}

/* If T is a VAR_DECL for a static that we are interested in, add the
   uid to the bitmap.  */

static void
check_operand (ipa_reference_local_vars_info_t local, 
	       tree t, bool checking_write)
{
  if (!t) return;

  if ((TREE_CODE (t) == VAR_DECL || TREE_CODE (t) == FUNCTION_DECL)
      && (has_proper_scope_for_analysis (t))) 
    {
      if (checking_write)
	{
	  if (local)
	    bitmap_set_bit (local->statics_written, DECL_UID (t));
	  /* Mark the write so we can tell which statics are
	     readonly.  */
	  bitmap_set_bit (module_statics_written, DECL_UID (t));
	}
      else if (local)
	bitmap_set_bit (local->statics_read, DECL_UID (t));
    }
}

/* Examine tree T for references to static variables. All internal
   references like array references or indirect references are added
   to the READ_BM. Direct references are added to either READ_BM or
   WRITE_BM depending on the value of CHECKING_WRITE.   */

static void
check_tree (ipa_reference_local_vars_info_t local, tree t, bool checking_write)
{
  if ((TREE_CODE (t) == EXC_PTR_EXPR) || (TREE_CODE (t) == FILTER_EXPR))
    return;

  while (TREE_CODE (t) == REALPART_EXPR 
	 || TREE_CODE (t) == IMAGPART_EXPR
	 || handled_component_p (t))
    {
      if (TREE_CODE (t) == ARRAY_REF)
	check_operand (local, TREE_OPERAND (t, 1), false);
      t = TREE_OPERAND (t, 0);
    }

  /* The bottom of an indirect reference can only be read, not
     written.  So just recurse and whatever we find, check it against
     the read bitmaps.  */

  /*  if (INDIRECT_REF_P (t) || TREE_CODE (t) == MEM_REF) */
  /* FIXME when we have array_ref's of pointers.  */
  if (INDIRECT_REF_P (t))
    check_tree (local, TREE_OPERAND (t, 0), false);

  if (SSA_VAR_P (t))
    check_operand (local, t, checking_write);
}

/* Scan tree T to see if there are any addresses taken in within T.  */

static void 
look_for_address_of (tree t)
{
  if (TREE_CODE (t) == ADDR_EXPR)
    {
      tree x = get_base_var (t);
      if (TREE_CODE (x) == VAR_DECL || TREE_CODE (x) == FUNCTION_DECL) 
	if (has_proper_scope_for_analysis (x))
	  bitmap_set_bit (module_statics_escape, DECL_UID (x));
    }
}

/* Check to see if T is a read or address of operation on a static var
   we are interested in analyzing.  LOCAL is passed in to get access
   to its bit vectors.  Local is NULL if this is called from a static
   initializer.  */

static void
check_rhs_var (ipa_reference_local_vars_info_t local, tree t)
{
  look_for_address_of (t);

  if (local == NULL) 
    return;

  check_tree(local, t, false);
}

/* Check to see if T is an assignment to a static var we are
   interested in analyzing.  LOCAL is passed in to get access to its bit
   vectors.  */

static void
check_lhs_var (ipa_reference_local_vars_info_t local, tree t)
{
  if (local == NULL) 
    return;
   
  check_tree(local, t, true);
}

/* This is a scaled down version of get_asm_expr_operands from
   tree_ssa_operands.c.  The version there runs much later and assumes
   that aliasing information is already available. Here we are just
   trying to find if the set of inputs and outputs contain references
   or address of operations to local static variables.  FN is the
   function being analyzed and STMT is the actual asm statement.  */

static void
get_asm_expr_operands (ipa_reference_local_vars_info_t local, tree stmt)
{
  int noutputs = list_length (ASM_OUTPUTS (stmt));
  const char **oconstraints
    = (const char **) alloca ((noutputs) * sizeof (const char *));
  int i;
  tree link;
  const char *constraint;
  bool allows_mem, allows_reg, is_inout;
  
  for (i=0, link = ASM_OUTPUTS (stmt); link; ++i, link = TREE_CHAIN (link))
    {
      oconstraints[i] = constraint
	= TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (link)));
      parse_output_constraint (&constraint, i, 0, 0,
			       &allows_mem, &allows_reg, &is_inout);
      
      check_lhs_var (local, TREE_VALUE (link));
    }

  for (link = ASM_INPUTS (stmt); link; link = TREE_CHAIN (link))
    {
      constraint
	= TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (link)));
      parse_input_constraint (&constraint, 0, 0, noutputs, 0,
			      oconstraints, &allows_mem, &allows_reg);
      
      check_rhs_var (local, TREE_VALUE (link));
    }
  
  for (link = ASM_CLOBBERS (stmt); link; link = TREE_CHAIN (link))
    if (simple_cst_equal(TREE_VALUE (link), memory_identifier_string) == 1) 
      {
	/* Abandon all hope, ye who enter here. */
	local->calls_read_all = true;
	local->calls_write_all = true;
      }      
}

/* Check the parameters of a function call from CALLER to CALL_EXPR to
   see if any of them are static vars.  Also check to see if this is
   either an indirect call, a call outside the compilation unit, or
   has special attributes that effect the clobbers.  The caller
   parameter is the tree node for the caller and the second operand is
   the tree node for the entire call expression.  */

static void
check_call (ipa_reference_local_vars_info_t local, tree call_expr) 
{
  int flags = call_expr_flags (call_expr);
  tree operand_list = TREE_OPERAND (call_expr, 1);
  tree operand;
  tree callee_t = get_callee_fndecl (call_expr);
  enum availability avail = AVAIL_NOT_AVAILABLE;

  for (operand = operand_list;
       operand != NULL_TREE;
       operand = TREE_CHAIN (operand))
    {
      tree argument = TREE_VALUE (operand);
      check_rhs_var (local, argument);
    }

  if (callee_t)
    {
      struct cgraph_node* callee = cgraph_node(callee_t);
      avail = cgraph_function_body_availability (callee);
    }

  if (avail == AVAIL_NOT_AVAILABLE || avail == AVAIL_OVERWRITABLE)
    if (local) 
      {
	if (flags & ECF_PURE) 
	  local->calls_read_all = true;
	else 
	  {
	    local->calls_read_all = true;
	    local->calls_write_all = true;
	  }
      }
}

/* TP is the part of the tree currently under the microscope.
   WALK_SUBTREES is part of the walk_tree api but is unused here.
   DATA is cgraph_node of the function being walked.  */

/* FIXME: When this is converted to run over SSA form, this code
   should be converted to use the operand scanner.  */

static tree
scan_for_static_refs (tree *tp, 
		      int *walk_subtrees, 
		      void *data)
{
  struct cgraph_node *fn = data;
  tree t = *tp;
  ipa_reference_local_vars_info_t local = NULL;
  if (fn)
    local = get_reference_vars_info_from_cgraph (fn)->local;

  switch (TREE_CODE (t))  
    {
    case VAR_DECL:
      if (DECL_INITIAL (t))
	walk_tree (&DECL_INITIAL (t), scan_for_static_refs, fn, visited_nodes);
      *walk_subtrees = 0;
      break;

    case MODIFY_EXPR:
      {
	/* First look on the lhs and see what variable is stored to */
	tree lhs = TREE_OPERAND (t, 0);
	tree rhs = TREE_OPERAND (t, 1);
	check_lhs_var (local, lhs);

	/* For the purposes of figuring out what the cast affects */

	/* Next check the operands on the rhs to see if they are ok. */
	switch (TREE_CODE_CLASS (TREE_CODE (rhs))) 
	  {
	  case tcc_binary:	    
	  case tcc_comparison:	    
 	    {
 	      tree op0 = TREE_OPERAND (rhs, 0);
 	      tree op1 = TREE_OPERAND (rhs, 1);
 	      check_rhs_var (local, op0);
 	      check_rhs_var (local, op1);
	    }
	    break;
	  case tcc_unary:
 	    {
 	      tree op0 = TREE_OPERAND (rhs, 0);
 	      check_rhs_var (local, op0);
 	    }

	    break;
	  case tcc_reference:
	    check_rhs_var (local, rhs);
	    break;
	  case tcc_declaration:
	    check_rhs_var (local, rhs);
	    break;
	  case tcc_expression:
	    switch (TREE_CODE (rhs)) 
	      {
	      case ADDR_EXPR:
		check_rhs_var (local, rhs);
		break;
	      case CALL_EXPR: 
		check_call (local, rhs);
		break;
	      default:
		break;
	      }
	    break;
	  default:
	    break;
	  }
	*walk_subtrees = 0;
      }
      break;

    case ADDR_EXPR:
      /* This case is here to find addresses on rhs of constructors in
	 decl_initial of static variables. */
      check_rhs_var (local, t);
      *walk_subtrees = 0;
      break;

    case LABEL_EXPR:
      if (DECL_NONLOCAL (TREE_OPERAND (t, 0)))
	{
	  /* Target of long jump. */
	  local->calls_read_all = true;
	  local->calls_write_all = true;
	}
      break;

    case CALL_EXPR: 
      check_call (local, t);
      *walk_subtrees = 0;
      break;
      
    case ASM_EXPR:
      get_asm_expr_operands (local, t);
      *walk_subtrees = 0;
      break;
      
    default:
      break;
    }
  return NULL;
}


/* Lookup the tree node for the static variable that has UID.  */
static tree
get_static_decl (int index)
{
  splay_tree_node stn = 
    splay_tree_lookup (reference_vars_to_consider, index);
  if (stn)
    return (tree)stn->value;
  return NULL;
}

/* Lookup the tree node for the static variable that has UID and
   convert the name to a string for debugging.  */

static const char *
get_static_name (int index)
{
  splay_tree_node stn = 
    splay_tree_lookup (reference_vars_to_consider, index);
  if (stn)
    return lang_hooks.decl_printable_name ((tree)(stn->value), 2);
  return NULL;
}

/* Or in all of the bits from every callee into X, the caller's, bit
   vector.  There are several cases to check to avoid the sparse
   bitmap oring.  */

static void
propagate_bits (struct cgraph_node *x)
{
  ipa_reference_vars_info_t x_info = get_reference_vars_info_from_cgraph (x);
  ipa_reference_global_vars_info_t x_global = x_info->global;

  struct cgraph_edge *e;
  for (e = x->callees; e; e = e->next_callee) 
    {
      struct cgraph_node *y = e->callee;

      /* Only look at the master nodes and skip external nodes.  */
      y = cgraph_master_clone (y);
      if (y)
	{
	  if (get_reference_vars_info_from_cgraph (y))
	    {
	      ipa_reference_vars_info_t y_info = get_reference_vars_info_from_cgraph (y);
	      ipa_reference_global_vars_info_t y_global = y_info->global;
	      
	      if (x_global->statics_read
		  != all_module_statics)
		{
		  if (y_global->statics_read 
		      == all_module_statics)
		    {
		      BITMAP_FREE (x_global->statics_read);
		      x_global->statics_read 
			= all_module_statics;
		    }
		  /* Skip bitmaps that are pointer equal to node's bitmap
		     (no reason to spin within the cycle).  */
		  else if (x_global->statics_read 
			   != y_global->statics_read)
		    bitmap_ior_into (x_global->statics_read,
				     y_global->statics_read);
		}
	      
	      if (x_global->statics_written 
		  != all_module_statics)
		{
		  if (y_global->statics_written 
		      == all_module_statics)
		    {
		      BITMAP_FREE (x_global->statics_written);
		      x_global->statics_written 
			= all_module_statics;
		    }
		  /* Skip bitmaps that are pointer equal to node's bitmap
		     (no reason to spin within the cycle).  */
		  else if (x_global->statics_written 
			   != y_global->statics_written)
		    bitmap_ior_into (x_global->statics_written,
				     y_global->statics_written);
		}
	    }
	  else 
	    {
	      gcc_unreachable ();
	    }
	}
    }
}

/* Look at all of the callees of X to see which ones represent inlined
   calls.  For each of these callees, merge their local info into
   TARGET and check their children recursively.  

   This function goes away when Jan changes the inliner and IPA
   analysis so that this is not run between the time when inlining
   decisions are made and when the inlining actually occurs.  */

static void 
merge_callee_local_info (struct cgraph_node *target, 
			 struct cgraph_node *x)
{
  struct cgraph_edge *e;
  ipa_reference_local_vars_info_t x_l = 
    get_reference_vars_info_from_cgraph (target)->local;

  /* Make the world safe for tail recursion.  */
  struct ipa_dfs_info *node_info = x->aux;
  
  if (node_info->aux) 
    return;

  node_info->aux = x;

  for (e = x->callees; e; e = e->next_callee) 
    {
      struct cgraph_node *y = e->callee;
      if (y->global.inlined_to) 
	{
	  ipa_reference_vars_info_t y_info;
	  ipa_reference_local_vars_info_t y_l;
	  struct cgraph_node* orig_y = y;
	 
	  y = cgraph_master_clone (y);
	  if (y)
	    {
	      y_info = get_reference_vars_info_from_cgraph (y);
	      y_l = y_info->local;
	      if (x_l != y_l)
		{
		  bitmap_ior_into (x_l->statics_read,
				   y_l->statics_read);
		  bitmap_ior_into (x_l->statics_written,
				   y_l->statics_written);
		}
	      x_l->calls_read_all |= y_l->calls_read_all;
	      x_l->calls_write_all |= y_l->calls_write_all;
	      merge_callee_local_info (target, y);
	    }
	  else 
	    {
	      fprintf(stderr, "suspect inlining of ");
	      dump_cgraph_node (stderr, orig_y);
	      fprintf(stderr, "\ninto ");
	      dump_cgraph_node (stderr, target);
	      dump_cgraph (stderr);
	      gcc_assert(false);
	    }
	}
    }

  node_info->aux = NULL;
}

/* The init routine for analyzing global static variable usage.  See
   comments at top for description.  */
static void 
ipa_init (void) 
{
  struct cgraph_node *node;
  memory_identifier_string = build_string(7, "memory");

  reference_vars_to_consider =
    splay_tree_new_ggc (splay_tree_compare_ints);

  bitmap_obstack_initialize (&ipa_obstack);
  module_statics_escape = BITMAP_ALLOC (&ipa_obstack);
  module_statics_written = BITMAP_ALLOC (&ipa_obstack);
  all_module_statics = BITMAP_ALLOC (&ipa_obstack);

  /* This will add NODE->DECL to the splay trees.  */
  for (node = cgraph_nodes; node; node = node->next)
    has_proper_scope_for_analysis (node->decl);

  /* There are some shared nodes, in particular the initializers on
     static declarations.  We do not need to scan them more than once
     since all we would be interested in are the addressof
     operations.  */
  visited_nodes = pointer_set_create ();
}

/* Check out the rhs of a static or global initialization VNODE to see
   if any of them contain addressof operations.  Note that some of
   these variables may  not even be referenced in the code in this
   compilation unit but their right hand sides may contain references
   to variables defined within this unit.  */

static void 
analyze_variable (struct cgraph_varpool_node *vnode)
{
  tree global = vnode->decl;
  if (TREE_CODE (global) == VAR_DECL)
    {
      if (DECL_INITIAL (global)) 
	walk_tree (&DECL_INITIAL (global), scan_for_static_refs, 
		   NULL, visited_nodes);
    } 
  else gcc_unreachable ();
}

/* This is the main routine for finding the reference patterns for
   global variables within a function FN.  */

static void
analyze_function (struct cgraph_node *fn)
{
  ipa_reference_vars_info_t info 
    = xcalloc (1, sizeof (struct ipa_reference_vars_info_d));
  ipa_reference_local_vars_info_t l
    = xcalloc (1, sizeof (struct ipa_reference_local_vars_info_d));
  tree decl = fn->decl;

  /* Add the info to the tree's annotation.  */
  get_function_ann (fn->decl)->reference_vars_info = info;

  info->local = l;
  l->statics_read = BITMAP_ALLOC (&ipa_obstack);
  l->statics_written = BITMAP_ALLOC (&ipa_obstack);

  if (dump_file)
    fprintf (dump_file, "\n local analysis of %s\n", cgraph_node_name (fn));
  
  {
    struct function *this_cfun = DECL_STRUCT_FUNCTION (decl);
    basic_block this_block;

    FOR_EACH_BB_FN (this_block, this_cfun)
      {
	block_stmt_iterator bsi;
	for (bsi = bsi_start (this_block); !bsi_end_p (bsi); bsi_next (&bsi))
	  walk_tree (bsi_stmt_ptr (bsi), scan_for_static_refs, 
		     fn, visited_nodes);
      }
  }

  /* There may be const decls with interesting right hand sides.  */
  if (DECL_STRUCT_FUNCTION (decl))
    {
      tree step;
      for (step = DECL_STRUCT_FUNCTION (decl)->unexpanded_var_list;
	   step;
	   step = TREE_CHAIN (step))
	{
	  tree var = TREE_VALUE (step);
	  if (TREE_CODE (var) == VAR_DECL 
	      && DECL_INITIAL (var)
	      && !TREE_STATIC (var))
	    walk_tree (&DECL_INITIAL (var), scan_for_static_refs, 
		       fn, visited_nodes);
	}
    }
}

/* If FN is avail == AVAIL_OVERWRITABLE, replace the effects bit
   vectors with worst case bit vectors.  We had to analyze it above to
   find out if it took the address of any statics. However, now that
   we know that, we can get rid of all of the other side effects.  */

static void
clean_function (struct cgraph_node *fn)
{
  ipa_reference_vars_info_t info = get_reference_vars_info_from_cgraph (fn);
  ipa_reference_local_vars_info_t l = info->local;
  ipa_reference_global_vars_info_t g = info->global;
  
  if (l)
    {
      if (l->statics_read
	  && l->statics_read != all_module_statics)
	BITMAP_FREE (l->statics_read);
      if (l->statics_written
	  &&l->statics_written != all_module_statics)
	BITMAP_FREE (l->statics_written);
      free (l);
    }
  
  if (g)
    {
      if (g->statics_read
	  && g->statics_read != all_module_statics)
	BITMAP_FREE (g->statics_read);
      
      if (g->statics_written
	  && g->statics_written != all_module_statics)
	BITMAP_FREE (g->statics_written);
      
      if (g->statics_not_read
	  && g->statics_not_read != all_module_statics)
	BITMAP_FREE (g->statics_not_read);
      
      if (g->statics_not_written
	  && g->statics_not_written != all_module_statics)
	BITMAP_FREE (g->statics_not_written);
      free (g);
    }

  
  free (get_function_ann (fn->decl)->reference_vars_info);
  get_function_ann (fn->decl)->reference_vars_info = NULL;
}


/* Produce the global information by preforming a transitive closure
   on the local information that was produced by ipa_analyze_function
   and ipa_analyze_variable.  */

static unsigned int
static_execute (void)
{
  struct cgraph_node *node;
  struct cgraph_varpool_node *vnode;
  struct cgraph_node *w;
  struct cgraph_node **order =
    xcalloc (cgraph_n_nodes, sizeof (struct cgraph_node *));
  int order_pos = order_pos = ipa_utils_reduced_inorder (order, false, true);
  int i;

  ipa_init ();

  /* Process all of the variables first.  */
  for (vnode = cgraph_varpool_nodes_queue; vnode; vnode = vnode->next_needed)
    analyze_variable (vnode);

  /* Process all of the functions next. 

     We do not want to process any of the clones so we check that this
     is a master clone.  However, we do need to process any
     AVAIL_OVERWRITABLE functions (these are never clones) because
     they may cause a static variable to escape.  The code that can
     overwrite such a function cannot access the statics because it
     would not be in the same compilation unit.  When the analysis is
     finished, the computed information of these AVAIL_OVERWRITABLE is
     replaced with worst case info.  
  */
  for (node = cgraph_nodes; node; node = node->next)
    if (node->analyzed 
	&& (cgraph_is_master_clone (node)
	    || (cgraph_function_body_availability (node) 
		== AVAIL_OVERWRITABLE)))
      analyze_function (node);

  pointer_set_destroy (visited_nodes);
  visited_nodes = NULL;
  if (dump_file) 
    dump_cgraph (dump_file);

  /* Prune out the variables that were found to behave badly
     (i.e. have their address taken).  */
  {
    unsigned int index;
    bitmap_iterator bi;
    bitmap module_statics_readonly = BITMAP_ALLOC (&ipa_obstack);
    bitmap module_statics_const = BITMAP_ALLOC (&ipa_obstack);
    bitmap bm_temp = BITMAP_ALLOC (&ipa_obstack);

    EXECUTE_IF_SET_IN_BITMAP (module_statics_escape, 0, index, bi)
      {
	splay_tree_remove (reference_vars_to_consider, index);
      }

    bitmap_and_compl_into (all_module_statics, 
			   module_statics_escape);

    bitmap_and_compl (module_statics_readonly, all_module_statics,
		      module_statics_written);

    /* If the address is not taken, we can unset the addressable bit
       on this variable.  */
    EXECUTE_IF_SET_IN_BITMAP (all_module_statics, 0, index, bi)
      {
	tree var = get_static_decl (index);
 	TREE_ADDRESSABLE (var) = 0;
	if (dump_file) 
	  fprintf (dump_file, "Not TREE_ADDRESSABLE var %s\n",
		   get_static_name (index));
      }

    /* If the variable is never written, we can set the TREE_READONLY
       flag.  Additionally if it has a DECL_INITIAL that is made up of
       constants we can treat the entire global as a constant.  */

    bitmap_and_compl (module_statics_readonly, all_module_statics,
		      module_statics_written);
    EXECUTE_IF_SET_IN_BITMAP (module_statics_readonly, 0, index, bi)
      {
	tree var = get_static_decl (index);

	/* Readonly on a function decl is very different from the
	   variable.  */
	if (TREE_CODE (var) == FUNCTION_DECL)
	  continue;

	/* Ignore variables in named sections - changing TREE_READONLY
	   changes the section flags, potentially causing conflicts with
	   other variables in the same named section.  */
	if (DECL_SECTION_NAME (var) == NULL_TREE)
	  {
	    TREE_READONLY (var) = 1;
	    if (dump_file)
	      fprintf (dump_file, "read-only var %s\n", 
		       get_static_name (index));
	  }
	if (DECL_INITIAL (var)
	    && is_gimple_min_invariant (DECL_INITIAL (var)))
	  {
 	    bitmap_set_bit (module_statics_const, index);
	    if (dump_file)
	      fprintf (dump_file, "read-only constant %s\n",
		       get_static_name (index));
	  }
      }

    BITMAP_FREE(module_statics_escape);
    BITMAP_FREE(module_statics_written);

    if (dump_file)
      EXECUTE_IF_SET_IN_BITMAP (all_module_statics, 0, index, bi)
	{
	  fprintf (dump_file, "\nPromotable global:%s",
		   get_static_name (index));
	}

    for (i = 0; i < order_pos; i++ )
      {
	ipa_reference_local_vars_info_t l;
	node = order[i];
	l = get_reference_vars_info_from_cgraph (node)->local;

	/* Any variables that are not in all_module_statics are
	   removed from the local maps.  This will include all of the
	   variables that were found to escape in the function
	   scanning.  */
	bitmap_and_into (l->statics_read, 
		         all_module_statics);
	bitmap_and_into (l->statics_written, 
		         all_module_statics);
      }

    BITMAP_FREE(module_statics_readonly);
    BITMAP_FREE(module_statics_const);
    BITMAP_FREE(bm_temp);
  }

  if (dump_file)
    {
      for (i = 0; i < order_pos; i++ )
	{
	  unsigned int index;
	  ipa_reference_local_vars_info_t l;
	  bitmap_iterator bi;

	  node = order[i];
	  l = get_reference_vars_info_from_cgraph (node)->local;
	  fprintf (dump_file, 
		   "\nFunction name:%s/%i:", 
		   cgraph_node_name (node), node->uid);
	  fprintf (dump_file, "\n  locals read: ");
	  EXECUTE_IF_SET_IN_BITMAP (l->statics_read,
				    0, index, bi)
	    {
	      fprintf (dump_file, "%s ",
		       get_static_name (index));
	    }
	  fprintf (dump_file, "\n  locals written: ");
	  EXECUTE_IF_SET_IN_BITMAP (l->statics_written,
				    0, index, bi)
	    {
	      fprintf(dump_file, "%s ",
		      get_static_name (index));
	    }
	}
    }

  /* Propagate the local information thru the call graph to produce
     the global information.  All the nodes within a cycle will have
     the same info so we collapse cycles first.  Then we can do the
     propagation in one pass from the leaves to the roots.  */
  order_pos = ipa_utils_reduced_inorder (order, true, true);
  if (dump_file)
    ipa_utils_print_order(dump_file, "reduced", order, order_pos);

  for (i = 0; i < order_pos; i++ )
    {
      ipa_reference_vars_info_t node_info;
      ipa_reference_global_vars_info_t node_g = 
	xcalloc (1, sizeof (struct ipa_reference_global_vars_info_d));
      ipa_reference_local_vars_info_t node_l;
      
      bool read_all;
      bool write_all;
      struct ipa_dfs_info * w_info;

      node = order[i];
      node_info = get_reference_vars_info_from_cgraph (node);
      if (!node_info) 
	{
	  dump_cgraph_node (stderr, node);
	  dump_cgraph (stderr);
	  gcc_unreachable ();
	}

      node_info->global = node_g;
      node_l = node_info->local;

      read_all = node_l->calls_read_all;
      write_all = node_l->calls_write_all;

      /* If any node in a cycle is calls_read_all or calls_write_all
	 they all are. */
      w_info = node->aux;
      w = w_info->next_cycle;
      while (w)
	{
	  ipa_reference_local_vars_info_t w_l = 
	    get_reference_vars_info_from_cgraph (w)->local;
	  read_all |= w_l->calls_read_all;
	  write_all |= w_l->calls_write_all;

	  w_info = w->aux;
	  w = w_info->next_cycle;
	}

      /* Initialized the bitmaps for the reduced nodes */
      if (read_all) 
	node_g->statics_read = all_module_statics;
      else 
	{
	  node_g->statics_read = BITMAP_ALLOC (&ipa_obstack);
	  bitmap_copy (node_g->statics_read, 
		       node_l->statics_read);
	}

      if (write_all) 
	node_g->statics_written = all_module_statics;
      else
	{
	  node_g->statics_written = BITMAP_ALLOC (&ipa_obstack);
	  bitmap_copy (node_g->statics_written, 
		       node_l->statics_written);
	}

      w_info = node->aux;
      w = w_info->next_cycle;
      while (w)
	{
	  ipa_reference_vars_info_t w_ri = 
	    get_reference_vars_info_from_cgraph (w);
	  ipa_reference_local_vars_info_t w_l = w_ri->local;

	  /* All nodes within a cycle share the same global info bitmaps.  */
	  w_ri->global = node_g;
	  
	  /* These global bitmaps are initialized from the local info
	     of all of the nodes in the region.  However there is no
	     need to do any work if the bitmaps were set to
	     all_module_statics.  */
	  if (!read_all)
	    bitmap_ior_into (node_g->statics_read,
			     w_l->statics_read);
	  if (!write_all)
	    bitmap_ior_into (node_g->statics_written,
			     w_l->statics_written);
	  w_info = w->aux;
	  w = w_info->next_cycle;
	}

      w = node;
      while (w)
	{
	  propagate_bits (w);
	  w_info = w->aux;
	  w = w_info->next_cycle;
	}
    }

  /* Need to fix up the local information sets.  The information that
     has been gathered so far is preinlining.  However, the
     compilation will progress post inlining so the local sets for the
     inlined calls need to be merged into the callers.  Note that the
     local sets are not shared between all of the nodes in a cycle so
     those nodes in the cycle must be processed explicitly.  */
  for (i = 0; i < order_pos; i++ )
    {
      struct ipa_dfs_info * w_info;
      node = order[i];
      merge_callee_local_info (node, node);
      
      w_info = node->aux;
      w = w_info->next_cycle;
      while (w)
	{
	  merge_callee_local_info (w, w);
	  w_info = w->aux;
	  w = w_info->next_cycle;
	}
    }

  if (dump_file)
    {
      for (i = 0; i < order_pos; i++ )
	{
	  ipa_reference_vars_info_t node_info;
	  ipa_reference_global_vars_info_t node_g;
	  ipa_reference_local_vars_info_t node_l;
	  unsigned int index;
	  bitmap_iterator bi;
	  struct ipa_dfs_info * w_info;

	  node = order[i];
	  node_info = get_reference_vars_info_from_cgraph (node);
	  node_g = node_info->global;
	  node_l = node_info->local;
	  fprintf (dump_file, 
		   "\nFunction name:%s/%i:", 
		   cgraph_node_name (node), node->uid);
	  fprintf (dump_file, "\n  locals read: ");
	  EXECUTE_IF_SET_IN_BITMAP (node_l->statics_read,
				    0, index, bi)
	    {
	      fprintf (dump_file, "%s ",
		       get_static_name (index));
	    }
	  fprintf (dump_file, "\n  locals written: ");
	  EXECUTE_IF_SET_IN_BITMAP (node_l->statics_written,
				    0, index, bi)
	    {
	      fprintf(dump_file, "%s ",
		      get_static_name (index));
	    }

	  w_info = node->aux;
	  w = w_info->next_cycle;
	  while (w) 
	    {
	      ipa_reference_vars_info_t w_ri = 
		get_reference_vars_info_from_cgraph (w);
	      ipa_reference_local_vars_info_t w_l = w_ri->local;
	      fprintf (dump_file, "\n  next cycle: %s/%i ",
		       cgraph_node_name (w), w->uid);
 	      fprintf (dump_file, "\n    locals read: ");
	      EXECUTE_IF_SET_IN_BITMAP (w_l->statics_read,
					0, index, bi)
		{
		  fprintf (dump_file, "%s ",
			   get_static_name (index));
		}

	      fprintf (dump_file, "\n    locals written: ");
	      EXECUTE_IF_SET_IN_BITMAP (w_l->statics_written,
					0, index, bi)
		{
		  fprintf(dump_file, "%s ",
			  get_static_name (index));
		}
	      

	      w_info = w->aux;
	      w = w_info->next_cycle;
	    }
	  fprintf (dump_file, "\n  globals read: ");
	  EXECUTE_IF_SET_IN_BITMAP (node_g->statics_read,
				    0, index, bi)
	    {
	      fprintf (dump_file, "%s ",
		       get_static_name (index));
	    }
	  fprintf (dump_file, "\n  globals written: ");
	  EXECUTE_IF_SET_IN_BITMAP (node_g->statics_written,
				    0, index, bi)
	    {
	      fprintf (dump_file, "%s ",
		       get_static_name (index));
	    }
	}
    }

  /* Cleanup. */
  for (i = 0; i < order_pos; i++ )
    {
      ipa_reference_vars_info_t node_info;
      ipa_reference_global_vars_info_t node_g;
      node = order[i];
      node_info = get_reference_vars_info_from_cgraph (node);
      node_g = node_info->global;
      
      /* Create the complimentary sets.  These are more useful for
	 certain apis.  */
      node_g->statics_not_read = BITMAP_ALLOC (&ipa_obstack);
      node_g->statics_not_written = BITMAP_ALLOC (&ipa_obstack);

      if (node_g->statics_read != all_module_statics) 
	{
	  bitmap_and_compl (node_g->statics_not_read, 
			    all_module_statics,
			    node_g->statics_read);
	}

      if (node_g->statics_written 
	  != all_module_statics) 
	bitmap_and_compl (node_g->statics_not_written, 
			  all_module_statics,
			  node_g->statics_written);
   }

  free (order);

  for (node = cgraph_nodes; node; node = node->next)
    {
      /* Get rid of the aux information.  */
      
      if (node->aux)
	{
	  free (node->aux);
	  node->aux = NULL;
	}
      
      if (node->analyzed 
	  && (cgraph_function_body_availability (node) == AVAIL_OVERWRITABLE))
	clean_function (node);
    }
  return 0;
}


static bool
gate_reference (void)
{
  return (flag_unit_at_a_time != 0  && flag_ipa_reference
	  /* Don't bother doing anything if the program has errors.  */
	  && !(errorcount || sorrycount));
}

struct tree_opt_pass pass_ipa_reference =
{
  "static-var",				/* name */
  gate_reference,			/* gate */
  static_execute,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_IPA_REFERENCE,		        /* tv_id */
  0,	                                /* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0					/* letter */
};

#include "gt-ipa-reference.h"

