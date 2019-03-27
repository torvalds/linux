/* Tree based points-to analysis
   Copyright (C) 2005, 2006 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dberlin@dberlin.org>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "obstack.h"
#include "bitmap.h"
#include "flags.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "errors.h"
#include "diagnostic.h"
#include "tree.h"
#include "c-common.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "varray.h"
#include "c-tree.h"
#include "tree-gimple.h"
#include "hashtab.h"
#include "function.h"
#include "cgraph.h"
#include "tree-pass.h"
#include "timevar.h"
#include "alloc-pool.h"
#include "splay-tree.h"
#include "params.h"
#include "tree-ssa-structalias.h"
#include "cgraph.h"
#include "pointer-set.h"

/* The idea behind this analyzer is to generate set constraints from the
   program, then solve the resulting constraints in order to generate the
   points-to sets.

   Set constraints are a way of modeling program analysis problems that
   involve sets.  They consist of an inclusion constraint language,
   describing the variables (each variable is a set) and operations that
   are involved on the variables, and a set of rules that derive facts
   from these operations.  To solve a system of set constraints, you derive
   all possible facts under the rules, which gives you the correct sets
   as a consequence.

   See  "Efficient Field-sensitive pointer analysis for C" by "David
   J. Pearce and Paul H. J. Kelly and Chris Hankin, at
   http://citeseer.ist.psu.edu/pearce04efficient.html

   Also see "Ultra-fast Aliasing Analysis using CLA: A Million Lines
   of C Code in a Second" by ""Nevin Heintze and Olivier Tardieu" at
   http://citeseer.ist.psu.edu/heintze01ultrafast.html

   There are three types of real constraint expressions, DEREF,
   ADDRESSOF, and SCALAR.  Each constraint expression consists
   of a constraint type, a variable, and an offset.

   SCALAR is a constraint expression type used to represent x, whether
   it appears on the LHS or the RHS of a statement.
   DEREF is a constraint expression type used to represent *x, whether
   it appears on the LHS or the RHS of a statement.
   ADDRESSOF is a constraint expression used to represent &x, whether
   it appears on the LHS or the RHS of a statement.

   Each pointer variable in the program is assigned an integer id, and
   each field of a structure variable is assigned an integer id as well.

   Structure variables are linked to their list of fields through a "next
   field" in each variable that points to the next field in offset
   order.
   Each variable for a structure field has

   1. "size", that tells the size in bits of that field.
   2. "fullsize, that tells the size in bits of the entire structure.
   3. "offset", that tells the offset in bits from the beginning of the
   structure to this field.

   Thus,
   struct f
   {
     int a;
     int b;
   } foo;
   int *bar;

   looks like

   foo.a -> id 1, size 32, offset 0, fullsize 64, next foo.b
   foo.b -> id 2, size 32, offset 32, fullsize 64, next NULL
   bar -> id 3, size 32, offset 0, fullsize 32, next NULL


  In order to solve the system of set constraints, the following is
  done:

  1. Each constraint variable x has a solution set associated with it,
  Sol(x).

  2. Constraints are separated into direct, copy, and complex.
  Direct constraints are ADDRESSOF constraints that require no extra
  processing, such as P = &Q
  Copy constraints are those of the form P = Q.
  Complex constraints are all the constraints involving dereferences
  and offsets (including offsetted copies).

  3. All direct constraints of the form P = &Q are processed, such
  that Q is added to Sol(P)

  4. All complex constraints for a given constraint variable are stored in a
  linked list attached to that variable's node.

  5. A directed graph is built out of the copy constraints. Each
  constraint variable is a node in the graph, and an edge from
  Q to P is added for each copy constraint of the form P = Q

  6. The graph is then walked, and solution sets are
  propagated along the copy edges, such that an edge from Q to P
  causes Sol(P) <- Sol(P) union Sol(Q).

  7.  As we visit each node, all complex constraints associated with
  that node are processed by adding appropriate copy edges to the graph, or the
  appropriate variables to the solution set.

  8. The process of walking the graph is iterated until no solution
  sets change.

  Prior to walking the graph in steps 6 and 7, We perform static
  cycle elimination on the constraint graph, as well
  as off-line variable substitution.

  TODO: Adding offsets to pointer-to-structures can be handled (IE not punted
  on and turned into anything), but isn't.  You can just see what offset
  inside the pointed-to struct it's going to access.

  TODO: Constant bounded arrays can be handled as if they were structs of the
  same number of elements.

  TODO: Modeling heap and incoming pointers becomes much better if we
  add fields to them as we discover them, which we could do.

  TODO: We could handle unions, but to be honest, it's probably not
  worth the pain or slowdown.  */

static GTY ((if_marked ("tree_map_marked_p"), param_is (struct tree_map))) htab_t heapvar_for_stmt;

/* One variable to represent all non-local accesses.  */
tree nonlocal_all;

static bool use_field_sensitive = true;
static int in_ipa_mode = 0;

/* Used for predecessor bitmaps. */
static bitmap_obstack predbitmap_obstack;

/* Used for points-to sets.  */
static bitmap_obstack pta_obstack;

/* Used for oldsolution members of variables. */
static bitmap_obstack oldpta_obstack;

/* Used for per-solver-iteration bitmaps.  */
static bitmap_obstack iteration_obstack;

static unsigned int create_variable_info_for (tree, const char *);
typedef struct constraint_graph *constraint_graph_t;
static void unify_nodes (constraint_graph_t, unsigned int, unsigned int, bool);

DEF_VEC_P(constraint_t);
DEF_VEC_ALLOC_P(constraint_t,heap);

#define EXECUTE_IF_IN_NONNULL_BITMAP(a, b, c, d)	\
  if (a)						\
    EXECUTE_IF_SET_IN_BITMAP (a, b, c, d)

static struct constraint_stats
{
  unsigned int total_vars;
  unsigned int nonpointer_vars;
  unsigned int unified_vars_static;
  unsigned int unified_vars_dynamic;
  unsigned int iterations;
  unsigned int num_edges;
  unsigned int num_implicit_edges;
  unsigned int points_to_sets_created;
} stats;

struct variable_info
{
  /* ID of this variable  */
  unsigned int id;

  /* Name of this variable */
  const char *name;

  /* Tree that this variable is associated with.  */
  tree decl;

  /* Offset of this variable, in bits, from the base variable  */
  unsigned HOST_WIDE_INT offset;

  /* Size of the variable, in bits.  */
  unsigned HOST_WIDE_INT size;

  /* Full size of the base variable, in bits.  */
  unsigned HOST_WIDE_INT fullsize;

  /* A link to the variable for the next field in this structure.  */
  struct variable_info *next;

  /* True if the variable is directly the target of a dereference.
     This is used to track which variables are *actually* dereferenced
     so we can prune their points to listed. */
  unsigned int directly_dereferenced:1;

  /* True if this is a variable created by the constraint analysis, such as
     heap variables and constraints we had to break up.  */
  unsigned int is_artificial_var:1;

  /* True if this is a special variable whose solution set should not be
     changed.  */
  unsigned int is_special_var:1;

  /* True for variables whose size is not known or variable.  */
  unsigned int is_unknown_size_var:1;

  /* True for variables that have unions somewhere in them.  */
  unsigned int has_union:1;

  /* True if this is a heap variable.  */
  unsigned int is_heap_var:1;

  /* Points-to set for this variable.  */
  bitmap solution;

  /* Old points-to set for this variable.  */
  bitmap oldsolution;

  /* Variable ids represented by this node.  */
  bitmap variables;

  /* Variable id this was collapsed to due to type unsafety.  This
     should be unused completely after build_succ_graph, or something
     is broken.  */
  struct variable_info *collapsed_to;
};
typedef struct variable_info *varinfo_t;

static varinfo_t first_vi_for_offset (varinfo_t, unsigned HOST_WIDE_INT);

/* Pool of variable info structures.  */
static alloc_pool variable_info_pool;

DEF_VEC_P(varinfo_t);

DEF_VEC_ALLOC_P(varinfo_t, heap);

/* Table of variable info structures for constraint variables.
   Indexed directly by variable info id.  */
static VEC(varinfo_t,heap) *varmap;

/* Return the varmap element N */

static inline varinfo_t
get_varinfo (unsigned int n)
{
  return VEC_index (varinfo_t, varmap, n);
}

/* Return the varmap element N, following the collapsed_to link.  */

static inline varinfo_t
get_varinfo_fc (unsigned int n)
{
  varinfo_t v = VEC_index (varinfo_t, varmap, n);

  if (v->collapsed_to)
    return v->collapsed_to;
  return v;
}

/* Variable that represents the unknown pointer.  */
static varinfo_t var_anything;
static tree anything_tree;
static unsigned int anything_id;

/* Variable that represents the NULL pointer.  */
static varinfo_t var_nothing;
static tree nothing_tree;
static unsigned int nothing_id;

/* Variable that represents read only memory.  */
static varinfo_t var_readonly;
static tree readonly_tree;
static unsigned int readonly_id;

/* Variable that represents integers.  This is used for when people do things
   like &0->a.b.  */
static varinfo_t var_integer;
static tree integer_tree;
static unsigned int integer_id;

/* Variable that represents escaped variables.  This is used to give
   incoming pointer variables a better set than ANYTHING.  */
static varinfo_t var_escaped_vars;
static tree escaped_vars_tree;
static unsigned int escaped_vars_id;

/* Variable that represents non-local variables before we expand it to
   one for each type.  */
static unsigned int nonlocal_vars_id;
/* Lookup a heap var for FROM, and return it if we find one.  */

static tree
heapvar_lookup (tree from)
{
  struct tree_map *h, in;
  in.from = from;

  h = htab_find_with_hash (heapvar_for_stmt, &in, htab_hash_pointer (from));
  if (h)
    return h->to;
  return NULL_TREE;
}

/* Insert a mapping FROM->TO in the heap var for statement
   hashtable.  */

static void
heapvar_insert (tree from, tree to)
{
  struct tree_map *h;
  void **loc;

  h = ggc_alloc (sizeof (struct tree_map));
  h->hash = htab_hash_pointer (from);
  h->from = from;
  h->to = to;
  loc = htab_find_slot_with_hash (heapvar_for_stmt, h, h->hash, INSERT);
  *(struct tree_map **) loc = h;
}

/* Return a new variable info structure consisting for a variable
   named NAME, and using constraint graph node NODE.  */

static varinfo_t
new_var_info (tree t, unsigned int id, const char *name)
{
  varinfo_t ret = pool_alloc (variable_info_pool);

  ret->id = id;
  ret->name = name;
  ret->decl = t;
  ret->directly_dereferenced = false;
  ret->is_artificial_var = false;
  ret->is_heap_var = false;
  ret->is_special_var = false;
  ret->is_unknown_size_var = false;
  ret->has_union = false;
  ret->solution = BITMAP_ALLOC (&pta_obstack);
  ret->oldsolution = BITMAP_ALLOC (&oldpta_obstack);
  ret->next = NULL;
  ret->collapsed_to = NULL;
  return ret;
}

typedef enum {SCALAR, DEREF, ADDRESSOF} constraint_expr_type;

/* An expression that appears in a constraint.  */

struct constraint_expr
{
  /* Constraint type.  */
  constraint_expr_type type;

  /* Variable we are referring to in the constraint.  */
  unsigned int var;

  /* Offset, in bits, of this constraint from the beginning of
     variables it ends up referring to.

     IOW, in a deref constraint, we would deref, get the result set,
     then add OFFSET to each member.   */
  unsigned HOST_WIDE_INT offset;
};

typedef struct constraint_expr ce_s;
DEF_VEC_O(ce_s);
DEF_VEC_ALLOC_O(ce_s, heap);
static void get_constraint_for (tree, VEC(ce_s, heap) **);
static void do_deref (VEC (ce_s, heap) **);

/* Our set constraints are made up of two constraint expressions, one
   LHS, and one RHS.

   As described in the introduction, our set constraints each represent an
   operation between set valued variables.
*/
struct constraint
{
  struct constraint_expr lhs;
  struct constraint_expr rhs;
};

/* List of constraints that we use to build the constraint graph from.  */

static VEC(constraint_t,heap) *constraints;
static alloc_pool constraint_pool;


DEF_VEC_I(int);
DEF_VEC_ALLOC_I(int, heap);

/* The constraint graph is represented as an array of bitmaps
   containing successor nodes.  */

struct constraint_graph
{
  /* Size of this graph, which may be different than the number of
     nodes in the variable map.  */
  unsigned int size;

  /* Explicit successors of each node. */
  bitmap *succs;

  /* Implicit predecessors of each node (Used for variable
     substitution). */
  bitmap *implicit_preds;

  /* Explicit predecessors of each node (Used for variable substitution).  */
  bitmap *preds;

  /* Indirect cycle representatives, or -1 if the node has no indirect
     cycles.  */
  int *indirect_cycles;

  /* Representative node for a node.  rep[a] == a unless the node has
     been unified. */
  unsigned int *rep;

  /* Equivalence class representative for a node.  This is used for
     variable substitution.  */
  int *eq_rep;

  /* Label for each node, used during variable substitution.  */
  unsigned int *label;

  /* Bitmap of nodes where the bit is set if the node is a direct
     node.  Used for variable substitution.  */
  sbitmap direct_nodes;

  /* Vector of complex constraints for each graph node.  Complex
     constraints are those involving dereferences or offsets that are
     not 0.  */
  VEC(constraint_t,heap) **complex;
};

static constraint_graph_t graph;

/* During variable substitution and the offline version of indirect
   cycle finding, we create nodes to represent dereferences and
   address taken constraints.  These represent where these start and
   end.  */
#define FIRST_REF_NODE (VEC_length (varinfo_t, varmap))
#define LAST_REF_NODE (FIRST_REF_NODE + (FIRST_REF_NODE - 1))
#define FIRST_ADDR_NODE (LAST_REF_NODE + 1)

/* Return the representative node for NODE, if NODE has been unioned
   with another NODE.
   This function performs path compression along the way to finding
   the representative.  */

static unsigned int
find (unsigned int node)
{
  gcc_assert (node < graph->size);
  if (graph->rep[node] != node)
    return graph->rep[node] = find (graph->rep[node]);
  return node;
}

/* Union the TO and FROM nodes to the TO nodes.
   Note that at some point in the future, we may want to do
   union-by-rank, in which case we are going to have to return the
   node we unified to.  */

static bool
unite (unsigned int to, unsigned int from)
{
  gcc_assert (to < graph->size && from < graph->size);
  if (to != from && graph->rep[from] != to)
    {
      graph->rep[from] = to;
      return true;
    }
  return false;
}

/* Create a new constraint consisting of LHS and RHS expressions.  */

static constraint_t
new_constraint (const struct constraint_expr lhs,
		const struct constraint_expr rhs)
{
  constraint_t ret = pool_alloc (constraint_pool);
  ret->lhs = lhs;
  ret->rhs = rhs;
  return ret;
}

/* Print out constraint C to FILE.  */

void
dump_constraint (FILE *file, constraint_t c)
{
  if (c->lhs.type == ADDRESSOF)
    fprintf (file, "&");
  else if (c->lhs.type == DEREF)
    fprintf (file, "*");
  fprintf (file, "%s", get_varinfo_fc (c->lhs.var)->name);
  if (c->lhs.offset != 0)
    fprintf (file, " + " HOST_WIDE_INT_PRINT_DEC, c->lhs.offset);
  fprintf (file, " = ");
  if (c->rhs.type == ADDRESSOF)
    fprintf (file, "&");
  else if (c->rhs.type == DEREF)
    fprintf (file, "*");
  fprintf (file, "%s", get_varinfo_fc (c->rhs.var)->name);
  if (c->rhs.offset != 0)
    fprintf (file, " + " HOST_WIDE_INT_PRINT_DEC, c->rhs.offset);
  fprintf (file, "\n");
}

/* Print out constraint C to stderr.  */

void
debug_constraint (constraint_t c)
{
  dump_constraint (stderr, c);
}

/* Print out all constraints to FILE */

void
dump_constraints (FILE *file)
{
  int i;
  constraint_t c;
  for (i = 0; VEC_iterate (constraint_t, constraints, i, c); i++)
    dump_constraint (file, c);
}

/* Print out all constraints to stderr.  */

void
debug_constraints (void)
{
  dump_constraints (stderr);
}

/* SOLVER FUNCTIONS

   The solver is a simple worklist solver, that works on the following
   algorithm:

   sbitmap changed_nodes = all zeroes;
   changed_count = 0;
   For each node that is not already collapsed:
       changed_count++;
       set bit in changed nodes

   while (changed_count > 0)
   {
     compute topological ordering for constraint graph

     find and collapse cycles in the constraint graph (updating
     changed if necessary)

     for each node (n) in the graph in topological order:
       changed_count--;

       Process each complex constraint associated with the node,
       updating changed if necessary.

       For each outgoing edge from n, propagate the solution from n to
       the destination of the edge, updating changed as necessary.

   }  */

/* Return true if two constraint expressions A and B are equal.  */

static bool
constraint_expr_equal (struct constraint_expr a, struct constraint_expr b)
{
  return a.type == b.type && a.var == b.var && a.offset == b.offset;
}

/* Return true if constraint expression A is less than constraint expression
   B.  This is just arbitrary, but consistent, in order to give them an
   ordering.  */

static bool
constraint_expr_less (struct constraint_expr a, struct constraint_expr b)
{
  if (a.type == b.type)
    {
      if (a.var == b.var)
	return a.offset < b.offset;
      else
	return a.var < b.var;
    }
  else
    return a.type < b.type;
}

/* Return true if constraint A is less than constraint B.  This is just
   arbitrary, but consistent, in order to give them an ordering.  */

static bool
constraint_less (const constraint_t a, const constraint_t b)
{
  if (constraint_expr_less (a->lhs, b->lhs))
    return true;
  else if (constraint_expr_less (b->lhs, a->lhs))
    return false;
  else
    return constraint_expr_less (a->rhs, b->rhs);
}

/* Return true if two constraints A and B are equal.  */

static bool
constraint_equal (struct constraint a, struct constraint b)
{
  return constraint_expr_equal (a.lhs, b.lhs)
    && constraint_expr_equal (a.rhs, b.rhs);
}


/* Find a constraint LOOKFOR in the sorted constraint vector VEC */

static constraint_t
constraint_vec_find (VEC(constraint_t,heap) *vec,
		     struct constraint lookfor)
{
  unsigned int place;
  constraint_t found;

  if (vec == NULL)
    return NULL;

  place = VEC_lower_bound (constraint_t, vec, &lookfor, constraint_less);
  if (place >= VEC_length (constraint_t, vec))
    return NULL;
  found = VEC_index (constraint_t, vec, place);
  if (!constraint_equal (*found, lookfor))
    return NULL;
  return found;
}

/* Union two constraint vectors, TO and FROM.  Put the result in TO.  */

static void
constraint_set_union (VEC(constraint_t,heap) **to,
		      VEC(constraint_t,heap) **from)
{
  int i;
  constraint_t c;

  for (i = 0; VEC_iterate (constraint_t, *from, i, c); i++)
    {
      if (constraint_vec_find (*to, *c) == NULL)
	{
	  unsigned int place = VEC_lower_bound (constraint_t, *to, c,
						constraint_less);
	  VEC_safe_insert (constraint_t, heap, *to, place, c);
	}
    }
}

/* Take a solution set SET, add OFFSET to each member of the set, and
   overwrite SET with the result when done.  */

static void
solution_set_add (bitmap set, unsigned HOST_WIDE_INT offset)
{
  bitmap result = BITMAP_ALLOC (&iteration_obstack);
  unsigned int i;
  bitmap_iterator bi;
  unsigned HOST_WIDE_INT min = -1, max = 0;

  /* Compute set of vars we can reach from set + offset.  */

  EXECUTE_IF_SET_IN_BITMAP (set, 0, i, bi)
    {
      if (get_varinfo (i)->is_artificial_var
	  || get_varinfo (i)->has_union
	  || get_varinfo (i)->is_unknown_size_var)
	continue;

      if (get_varinfo (i)->offset + offset < min)
	min = get_varinfo (i)->offset + offset;
      if (get_varinfo (i)->offset + get_varinfo (i)->size + offset > max)
	{
	  max = get_varinfo (i)->offset + get_varinfo (i)->size + offset;
	  if (max > get_varinfo (i)->fullsize)
	    max = get_varinfo (i)->fullsize;
	}
    }

  EXECUTE_IF_SET_IN_BITMAP (set, 0, i, bi)
    {
      /* If this is a properly sized variable, only add offset if it's
	 less than end.  Otherwise, it is globbed to a single
	 variable.  */

      if (get_varinfo (i)->offset + get_varinfo (i)->size - 1 >= min
	  && get_varinfo (i)->offset < max)
	{
	  bitmap_set_bit (result, i);
	}
      else if (get_varinfo (i)->is_artificial_var
	       || get_varinfo (i)->has_union
	       || get_varinfo (i)->is_unknown_size_var)
	{
	  bitmap_set_bit (result, i);
	}
    }

  bitmap_copy (set, result);
  BITMAP_FREE (result);
}

/* Union solution sets TO and FROM, and add INC to each member of FROM in the
   process.  */

static bool
set_union_with_increment  (bitmap to, bitmap from, unsigned HOST_WIDE_INT inc)
{
  if (inc == 0)
    return bitmap_ior_into (to, from);
  else
    {
      bitmap tmp;
      bool res;

      tmp = BITMAP_ALLOC (&iteration_obstack);
      bitmap_copy (tmp, from);
      solution_set_add (tmp, inc);
      res = bitmap_ior_into (to, tmp);
      BITMAP_FREE (tmp);
      return res;
    }
}

/* Insert constraint C into the list of complex constraints for graph
   node VAR.  */

static void
insert_into_complex (constraint_graph_t graph,
		     unsigned int var, constraint_t c)
{
  VEC (constraint_t, heap) *complex = graph->complex[var];
  unsigned int place = VEC_lower_bound (constraint_t, complex, c,
					constraint_less);

  /* Only insert constraints that do not already exist.  */
  if (place >= VEC_length (constraint_t, complex)
      || !constraint_equal (*c, *VEC_index (constraint_t, complex, place)))
    VEC_safe_insert (constraint_t, heap, graph->complex[var], place, c);
}


/* Condense two variable nodes into a single variable node, by moving
   all associated info from SRC to TO.  */

static void
merge_node_constraints (constraint_graph_t graph, unsigned int to,
			unsigned int from)
{
  unsigned int i;
  constraint_t c;

  gcc_assert (find (from) == to);

  /* Move all complex constraints from src node into to node  */
  for (i = 0; VEC_iterate (constraint_t, graph->complex[from], i, c); i++)
    {
      /* In complex constraints for node src, we may have either
	 a = *src, and *src = a, or an offseted constraint which are
	 always added to the rhs node's constraints.  */

      if (c->rhs.type == DEREF)
	c->rhs.var = to;
      else if (c->lhs.type == DEREF)
	c->lhs.var = to;
      else
	c->rhs.var = to;
    }
  constraint_set_union (&graph->complex[to], &graph->complex[from]);
  VEC_free (constraint_t, heap, graph->complex[from]);
  graph->complex[from] = NULL;
}


/* Remove edges involving NODE from GRAPH.  */

static void
clear_edges_for_node (constraint_graph_t graph, unsigned int node)
{
  if (graph->succs[node])
    BITMAP_FREE (graph->succs[node]);
}

/* Merge GRAPH nodes FROM and TO into node TO.  */

static void
merge_graph_nodes (constraint_graph_t graph, unsigned int to,
		   unsigned int from)
{
  if (graph->indirect_cycles[from] != -1)
    {
      /* If we have indirect cycles with the from node, and we have
	 none on the to node, the to node has indirect cycles from the
	 from node now that they are unified.
	 If indirect cycles exist on both, unify the nodes that they
	 are in a cycle with, since we know they are in a cycle with
	 each other.  */
      if (graph->indirect_cycles[to] == -1)
	{
	  graph->indirect_cycles[to] = graph->indirect_cycles[from];
	}
      else
	{
	  unsigned int tonode = find (graph->indirect_cycles[to]);
	  unsigned int fromnode = find (graph->indirect_cycles[from]);

	  if (unite (tonode, fromnode))
	    unify_nodes (graph, tonode, fromnode, true);
	}
    }

  /* Merge all the successor edges.  */
  if (graph->succs[from])
    {
      if (!graph->succs[to])
	graph->succs[to] = BITMAP_ALLOC (&pta_obstack);
      bitmap_ior_into (graph->succs[to],
		       graph->succs[from]);
    }

  clear_edges_for_node (graph, from);
}


/* Add an indirect graph edge to GRAPH, going from TO to FROM if
   it doesn't exist in the graph already.  */

static void
add_implicit_graph_edge (constraint_graph_t graph, unsigned int to,
			 unsigned int from)
{
  if (to == from)
    return;

  if (!graph->implicit_preds[to])
    graph->implicit_preds[to] = BITMAP_ALLOC (&predbitmap_obstack);

  if (!bitmap_bit_p (graph->implicit_preds[to], from))
    {
      stats.num_implicit_edges++;
      bitmap_set_bit (graph->implicit_preds[to], from);
    }
}

/* Add a predecessor graph edge to GRAPH, going from TO to FROM if
   it doesn't exist in the graph already.
   Return false if the edge already existed, true otherwise.  */

static void
add_pred_graph_edge (constraint_graph_t graph, unsigned int to,
		     unsigned int from)
{
  if (!graph->preds[to])
    graph->preds[to] = BITMAP_ALLOC (&predbitmap_obstack);
  if (!bitmap_bit_p (graph->preds[to], from))
    bitmap_set_bit (graph->preds[to], from);
}

/* Add a graph edge to GRAPH, going from FROM to TO if
   it doesn't exist in the graph already.
   Return false if the edge already existed, true otherwise.  */

static bool
add_graph_edge (constraint_graph_t graph, unsigned int to,
		unsigned int from)
{
  if (to == from)
    {
      return false;
    }
  else
    {
      bool r = false;

      if (!graph->succs[from])
	graph->succs[from] = BITMAP_ALLOC (&pta_obstack);
      if (!bitmap_bit_p (graph->succs[from], to))
	{
	  r = true;
	  if (to < FIRST_REF_NODE && from < FIRST_REF_NODE)
	    stats.num_edges++;
	  bitmap_set_bit (graph->succs[from], to);
	}
      return r;
    }
}


/* Return true if {DEST.SRC} is an existing graph edge in GRAPH.  */

static bool
valid_graph_edge (constraint_graph_t graph, unsigned int src,
		  unsigned int dest)
{
  return (graph->succs[dest]
	  && bitmap_bit_p (graph->succs[dest], src));
}

/* Build the constraint graph, adding only predecessor edges right now.  */

static void
build_pred_graph (void)
{
  int i;
  constraint_t c;
  unsigned int j;

  graph = XNEW (struct constraint_graph);
  graph->size = (VEC_length (varinfo_t, varmap)) * 3;
  graph->succs = XCNEWVEC (bitmap, graph->size);
  graph->implicit_preds = XCNEWVEC (bitmap, graph->size);
  graph->preds = XCNEWVEC (bitmap, graph->size);
  graph->indirect_cycles = XNEWVEC (int, VEC_length (varinfo_t, varmap));
  graph->label = XCNEWVEC (unsigned int, graph->size);
  graph->rep = XNEWVEC (unsigned int, graph->size);
  graph->eq_rep = XNEWVEC (int, graph->size);
  graph->complex = XCNEWVEC (VEC(constraint_t, heap) *,
			     VEC_length (varinfo_t, varmap));
  graph->direct_nodes = sbitmap_alloc (graph->size);
  sbitmap_zero (graph->direct_nodes);

  for (j = 0; j < FIRST_REF_NODE; j++)
    {
      if (!get_varinfo (j)->is_special_var)
	SET_BIT (graph->direct_nodes, j);
    }

  for (j = 0; j < graph->size; j++)
    {
      graph->rep[j] = j;
      graph->eq_rep[j] = -1;
    }

  for (j = 0; j < VEC_length (varinfo_t, varmap); j++)
    graph->indirect_cycles[j] = -1;

  for (i = 0; VEC_iterate (constraint_t, constraints, i, c); i++)
    {
      struct constraint_expr lhs = c->lhs;
      struct constraint_expr rhs = c->rhs;
      unsigned int lhsvar = get_varinfo_fc (lhs.var)->id;
      unsigned int rhsvar = get_varinfo_fc (rhs.var)->id;

      if (lhs.type == DEREF)
	{
	  /* *x = y.  */
	  if (rhs.offset == 0 && lhs.offset == 0 && rhs.type == SCALAR)
	    add_pred_graph_edge (graph, FIRST_REF_NODE + lhsvar, rhsvar);
	  if (rhs.type == ADDRESSOF)
	    RESET_BIT (graph->direct_nodes, rhsvar);
	}
      else if (rhs.type == DEREF)
	{
	  /* x = *y */
	  if (rhs.offset == 0 && lhs.offset == 0 && lhs.type == SCALAR)
	    add_pred_graph_edge (graph, lhsvar, FIRST_REF_NODE + rhsvar);
	  else
	    RESET_BIT (graph->direct_nodes, lhsvar);
	}
      else if (rhs.type == ADDRESSOF)
	{
	  /* x = &y */
	  add_pred_graph_edge (graph, lhsvar, FIRST_ADDR_NODE + rhsvar);
	  /* Implicitly, *x = y */
	  add_implicit_graph_edge (graph, FIRST_REF_NODE + lhsvar, rhsvar);

	  RESET_BIT (graph->direct_nodes, rhsvar);
	}
      else if (lhsvar > anything_id
	       && lhsvar != rhsvar && lhs.offset == 0 && rhs.offset == 0)
	{
	  /* x = y */
	  add_pred_graph_edge (graph, lhsvar, rhsvar);
	  /* Implicitly, *x = *y */
	  add_implicit_graph_edge (graph, FIRST_REF_NODE + lhsvar,
				   FIRST_REF_NODE + rhsvar);
	}
      else if (lhs.offset != 0 || rhs.offset != 0)
	{
	  if (rhs.offset != 0)
	    RESET_BIT (graph->direct_nodes, lhs.var);
	  if (lhs.offset != 0)
	    RESET_BIT (graph->direct_nodes, rhs.var);
	}
    }
}

/* Build the constraint graph, adding successor edges.  */

static void
build_succ_graph (void)
{
  int i;
  constraint_t c;

  for (i = 0; VEC_iterate (constraint_t, constraints, i, c); i++)
    {
      struct constraint_expr lhs;
      struct constraint_expr rhs;
      unsigned int lhsvar;
      unsigned int rhsvar;

      if (!c)
	continue;

      lhs = c->lhs;
      rhs = c->rhs;
      lhsvar = find (get_varinfo_fc (lhs.var)->id);
      rhsvar = find (get_varinfo_fc (rhs.var)->id);

      if (lhs.type == DEREF)
	{
	  if (rhs.offset == 0 && lhs.offset == 0 && rhs.type == SCALAR)
	    add_graph_edge (graph, FIRST_REF_NODE + lhsvar, rhsvar);
	}
      else if (rhs.type == DEREF)
	{
	  if (rhs.offset == 0 && lhs.offset == 0 && lhs.type == SCALAR)
	    add_graph_edge (graph, lhsvar, FIRST_REF_NODE + rhsvar);
	}
      else if (rhs.type == ADDRESSOF)
	{
	  /* x = &y */
	  gcc_assert (find (get_varinfo_fc (rhs.var)->id)
		      == get_varinfo_fc (rhs.var)->id);
	  bitmap_set_bit (get_varinfo (lhsvar)->solution, rhsvar);
	}
      else if (lhsvar > anything_id
	       && lhsvar != rhsvar && lhs.offset == 0 && rhs.offset == 0)
	{
	  add_graph_edge (graph, lhsvar, rhsvar);
	}
    }
}


/* Changed variables on the last iteration.  */
static unsigned int changed_count;
static sbitmap changed;

DEF_VEC_I(unsigned);
DEF_VEC_ALLOC_I(unsigned,heap);


/* Strongly Connected Component visitation info.  */

struct scc_info
{
  sbitmap visited;
  sbitmap roots;
  unsigned int *dfs;
  unsigned int *node_mapping;
  int current_index;
  VEC(unsigned,heap) *scc_stack;
};


/* Recursive routine to find strongly connected components in GRAPH.
   SI is the SCC info to store the information in, and N is the id of current
   graph node we are processing.

   This is Tarjan's strongly connected component finding algorithm, as
   modified by Nuutila to keep only non-root nodes on the stack.
   The algorithm can be found in "On finding the strongly connected
   connected components in a directed graph" by Esko Nuutila and Eljas
   Soisalon-Soininen, in Information Processing Letters volume 49,
   number 1, pages 9-14.  */

static void
scc_visit (constraint_graph_t graph, struct scc_info *si, unsigned int n)
{
  unsigned int i;
  bitmap_iterator bi;
  unsigned int my_dfs;

  SET_BIT (si->visited, n);
  si->dfs[n] = si->current_index ++;
  my_dfs = si->dfs[n];

  /* Visit all the successors.  */
  EXECUTE_IF_IN_NONNULL_BITMAP (graph->succs[n], 0, i, bi)
    {
      unsigned int w;

      if (i > LAST_REF_NODE)
	break;

      w = find (i);
      if (TEST_BIT (si->roots, w))
	continue;

      if (!TEST_BIT (si->visited, w))
	scc_visit (graph, si, w);
      {
	unsigned int t = find (w);
	unsigned int nnode = find (n);
	gcc_assert (nnode == n);

	if (si->dfs[t] < si->dfs[nnode])
	  si->dfs[n] = si->dfs[t];
      }
    }

  /* See if any components have been identified.  */
  if (si->dfs[n] == my_dfs)
    {
      if (VEC_length (unsigned, si->scc_stack) > 0
	  && si->dfs[VEC_last (unsigned, si->scc_stack)] >= my_dfs)
	{
	  bitmap scc = BITMAP_ALLOC (NULL);
	  bool have_ref_node = n >= FIRST_REF_NODE;
	  unsigned int lowest_node;
	  bitmap_iterator bi;

	  bitmap_set_bit (scc, n);

	  while (VEC_length (unsigned, si->scc_stack) != 0
		 && si->dfs[VEC_last (unsigned, si->scc_stack)] >= my_dfs)
	    {
	      unsigned int w = VEC_pop (unsigned, si->scc_stack);

	      bitmap_set_bit (scc, w);
	      if (w >= FIRST_REF_NODE)
		have_ref_node = true;
	    }

	  lowest_node = bitmap_first_set_bit (scc);
	  gcc_assert (lowest_node < FIRST_REF_NODE);
	  EXECUTE_IF_SET_IN_BITMAP (scc, 0, i, bi)
	    {
	      if (i < FIRST_REF_NODE)
		{
		  /* Mark this node for collapsing.  */
		  if (unite (lowest_node, i))
		    unify_nodes (graph, lowest_node, i, false);
		}
	      else
		{
		  unite (lowest_node, i);
		  graph->indirect_cycles[i - FIRST_REF_NODE] = lowest_node;
		}
	    }
	}
      SET_BIT (si->roots, n);
    }
  else
    VEC_safe_push (unsigned, heap, si->scc_stack, n);
}

/* Unify node FROM into node TO, updating the changed count if
   necessary when UPDATE_CHANGED is true.  */

static void
unify_nodes (constraint_graph_t graph, unsigned int to, unsigned int from,
	     bool update_changed)
{

  gcc_assert (to != from && find (to) == to);
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Unifying %s to %s\n",
	     get_varinfo (from)->name,
	     get_varinfo (to)->name);

  if (update_changed)
    stats.unified_vars_dynamic++;
  else
    stats.unified_vars_static++;

  merge_graph_nodes (graph, to, from);
  merge_node_constraints (graph, to, from);

  if (update_changed && TEST_BIT (changed, from))
    {
      RESET_BIT (changed, from);
      if (!TEST_BIT (changed, to))
	SET_BIT (changed, to);
      else
	{
	  gcc_assert (changed_count > 0);
	  changed_count--;
	}
    }

  /* If the solution changes because of the merging, we need to mark
     the variable as changed.  */
  if (bitmap_ior_into (get_varinfo (to)->solution,
		       get_varinfo (from)->solution))
    {
      if (update_changed && !TEST_BIT (changed, to))
	{
	  SET_BIT (changed, to);
	  changed_count++;
	}
    }

  BITMAP_FREE (get_varinfo (from)->solution);
  BITMAP_FREE (get_varinfo (from)->oldsolution);

  if (stats.iterations > 0)
    {
      BITMAP_FREE (get_varinfo (to)->oldsolution);
      get_varinfo (to)->oldsolution = BITMAP_ALLOC (&oldpta_obstack);
    }

  if (valid_graph_edge (graph, to, to))
    {
      if (graph->succs[to])
	bitmap_clear_bit (graph->succs[to], to);
    }
}

/* Information needed to compute the topological ordering of a graph.  */

struct topo_info
{
  /* sbitmap of visited nodes.  */
  sbitmap visited;
  /* Array that stores the topological order of the graph, *in
     reverse*.  */
  VEC(unsigned,heap) *topo_order;
};


/* Initialize and return a topological info structure.  */

static struct topo_info *
init_topo_info (void)
{
  size_t size = VEC_length (varinfo_t, varmap);
  struct topo_info *ti = XNEW (struct topo_info);
  ti->visited = sbitmap_alloc (size);
  sbitmap_zero (ti->visited);
  ti->topo_order = VEC_alloc (unsigned, heap, 1);
  return ti;
}


/* Free the topological sort info pointed to by TI.  */

static void
free_topo_info (struct topo_info *ti)
{
  sbitmap_free (ti->visited);
  VEC_free (unsigned, heap, ti->topo_order);
  free (ti);
}

/* Visit the graph in topological order, and store the order in the
   topo_info structure.  */

static void
topo_visit (constraint_graph_t graph, struct topo_info *ti,
	    unsigned int n)
{
  bitmap_iterator bi;
  unsigned int j;

  SET_BIT (ti->visited, n);

  if (graph->succs[n])
    EXECUTE_IF_SET_IN_BITMAP (graph->succs[n], 0, j, bi)
      {
	if (!TEST_BIT (ti->visited, j))
	  topo_visit (graph, ti, j);
      }

  VEC_safe_push (unsigned, heap, ti->topo_order, n);
}

/* Return true if variable N + OFFSET is a legal field of N.  */

static bool
type_safe (unsigned int n, unsigned HOST_WIDE_INT *offset)
{
  varinfo_t ninfo = get_varinfo (n);

  /* For things we've globbed to single variables, any offset into the
     variable acts like the entire variable, so that it becomes offset
     0.  */
  if (ninfo->is_special_var
      || ninfo->is_artificial_var
      || ninfo->is_unknown_size_var)
    {
      *offset = 0;
      return true;
    }
  return (get_varinfo (n)->offset + *offset) < get_varinfo (n)->fullsize;
}

/* Process a constraint C that represents *x = &y.  */

static void
do_da_constraint (constraint_graph_t graph ATTRIBUTE_UNUSED,
		  constraint_t c, bitmap delta)
{
  unsigned int rhs = c->rhs.var;
  unsigned int j;
  bitmap_iterator bi;

  /* For each member j of Delta (Sol(x)), add x to Sol(j)  */
  EXECUTE_IF_SET_IN_BITMAP (delta, 0, j, bi)
    {
      unsigned HOST_WIDE_INT offset = c->lhs.offset;
      if (type_safe (j, &offset) && !(get_varinfo (j)->is_special_var))
	{
	/* *x != NULL && *x != ANYTHING*/
	  varinfo_t v;
	  unsigned int t;
	  bitmap sol;
	  unsigned HOST_WIDE_INT fieldoffset = get_varinfo (j)->offset + offset;

	  v = first_vi_for_offset (get_varinfo (j), fieldoffset);
	  if (!v)
	    continue;
	  t = find (v->id);
	  sol = get_varinfo (t)->solution;
	  if (!bitmap_bit_p (sol, rhs))
	    {
	      bitmap_set_bit (sol, rhs);
	      if (!TEST_BIT (changed, t))
		{
		  SET_BIT (changed, t);
		  changed_count++;
		}
	    }
	}
      else if (0 && dump_file && !(get_varinfo (j)->is_special_var))
	fprintf (dump_file, "Untypesafe usage in do_da_constraint.\n");

    }
}

/* Process a constraint C that represents x = *y, using DELTA as the
   starting solution.  */

static void
do_sd_constraint (constraint_graph_t graph, constraint_t c,
		  bitmap delta)
{
  unsigned int lhs = find (c->lhs.var);
  bool flag = false;
  bitmap sol = get_varinfo (lhs)->solution;
  unsigned int j;
  bitmap_iterator bi;

 if (bitmap_bit_p (delta, anything_id))
   {
     flag = !bitmap_bit_p (sol, anything_id);
     if (flag)
       bitmap_set_bit (sol, anything_id);
     goto done;
   }
  /* For each variable j in delta (Sol(y)), add
     an edge in the graph from j to x, and union Sol(j) into Sol(x).  */
  EXECUTE_IF_SET_IN_BITMAP (delta, 0, j, bi)
    {
      unsigned HOST_WIDE_INT roffset = c->rhs.offset;
      if (type_safe (j, &roffset))
	{
	  varinfo_t v;
	  unsigned HOST_WIDE_INT fieldoffset = get_varinfo (j)->offset + roffset;
	  unsigned int t;

	  v = first_vi_for_offset (get_varinfo (j), fieldoffset);
	  if (!v)
	    continue;
	  t = find (v->id);

	  /* Adding edges from the special vars is pointless.
	     They don't have sets that can change.  */
	  if (get_varinfo (t) ->is_special_var)
	    flag |= bitmap_ior_into (sol, get_varinfo (t)->solution);
	  else if (add_graph_edge (graph, lhs, t))
	    flag |= bitmap_ior_into (sol, get_varinfo (t)->solution);
	}
      else if (0 && dump_file && !(get_varinfo (j)->is_special_var))
	fprintf (dump_file, "Untypesafe usage in do_sd_constraint\n");

    }

done:
  /* If the LHS solution changed, mark the var as changed.  */
  if (flag)
    {
      get_varinfo (lhs)->solution = sol;
      if (!TEST_BIT (changed, lhs))
	{
	  SET_BIT (changed, lhs);
	  changed_count++;
	}
    }
}

/* Process a constraint C that represents *x = y.  */

static void
do_ds_constraint (constraint_t c, bitmap delta)
{
  unsigned int rhs = find (c->rhs.var);
  unsigned HOST_WIDE_INT roff = c->rhs.offset;
  bitmap sol = get_varinfo (rhs)->solution;
  unsigned int j;
  bitmap_iterator bi;

 if (bitmap_bit_p (sol, anything_id))
   {
     EXECUTE_IF_SET_IN_BITMAP (delta, 0, j, bi)
       {
	 varinfo_t jvi = get_varinfo (j);
	 unsigned int t;
	 unsigned int loff = c->lhs.offset;
	 unsigned HOST_WIDE_INT fieldoffset = jvi->offset + loff;
	 varinfo_t v;

	 v = first_vi_for_offset (get_varinfo (j), fieldoffset);
	 if (!v)
	   continue;
	 t = find (v->id);

	 if (!bitmap_bit_p (get_varinfo (t)->solution, anything_id))
	   {
	     bitmap_set_bit (get_varinfo (t)->solution, anything_id);
	     if (!TEST_BIT (changed, t))
	       {
		 SET_BIT (changed, t);
		 changed_count++;
	       }
	   }
       }
     return;
   }

  /* For each member j of delta (Sol(x)), add an edge from y to j and
     union Sol(y) into Sol(j) */
  EXECUTE_IF_SET_IN_BITMAP (delta, 0, j, bi)
    {
      unsigned HOST_WIDE_INT loff = c->lhs.offset;
      if (type_safe (j, &loff) && !(get_varinfo (j)->is_special_var))
	{
	  varinfo_t v;
	  unsigned int t;
	  unsigned HOST_WIDE_INT fieldoffset = get_varinfo (j)->offset + loff;
	  bitmap tmp;

	  v = first_vi_for_offset (get_varinfo (j), fieldoffset);
	  if (!v)
	    continue;
	  t = find (v->id);
	  tmp = get_varinfo (t)->solution;

	  if (set_union_with_increment (tmp, sol, roff))
	    {
	      get_varinfo (t)->solution = tmp;
	      if (t == rhs)
		sol = get_varinfo (rhs)->solution;
	      if (!TEST_BIT (changed, t))
		{
		  SET_BIT (changed, t);
		  changed_count++;
		}
	    }
	}
      else if (0 && dump_file && !(get_varinfo (j)->is_special_var))
	fprintf (dump_file, "Untypesafe usage in do_ds_constraint\n");
    }
}

/* Handle a non-simple (simple meaning requires no iteration),
   constraint (IE *x = &y, x = *y, *x = y, and x = y with offsets involved).  */

static void
do_complex_constraint (constraint_graph_t graph, constraint_t c, bitmap delta)
{
  if (c->lhs.type == DEREF)
    {
      if (c->rhs.type == ADDRESSOF)
	{
	  /* *x = &y */
	  do_da_constraint (graph, c, delta);
	}
      else
	{
	  /* *x = y */
	  do_ds_constraint (c, delta);
	}
    }
  else if (c->rhs.type == DEREF)
    {
      /* x = *y */
      if (!(get_varinfo (c->lhs.var)->is_special_var))
	do_sd_constraint (graph, c, delta);
    }
  else
    {
      bitmap tmp;
      bitmap solution;
      bool flag = false;
      unsigned int t;

      gcc_assert (c->rhs.type == SCALAR && c->lhs.type == SCALAR);
      t = find (c->rhs.var);
      solution = get_varinfo (t)->solution;
      t = find (c->lhs.var);
      tmp = get_varinfo (t)->solution;

      flag = set_union_with_increment (tmp, solution, c->rhs.offset);

      if (flag)
	{
	  get_varinfo (t)->solution = tmp;
	  if (!TEST_BIT (changed, t))
	    {
	      SET_BIT (changed, t);
	      changed_count++;
	    }
	}
    }
}

/* Initialize and return a new SCC info structure.  */

static struct scc_info *
init_scc_info (size_t size)
{
  struct scc_info *si = XNEW (struct scc_info);
  size_t i;

  si->current_index = 0;
  si->visited = sbitmap_alloc (size);
  sbitmap_zero (si->visited);
  si->roots = sbitmap_alloc (size);
  sbitmap_zero (si->roots);
  si->node_mapping = XNEWVEC (unsigned int, size);
  si->dfs = XCNEWVEC (unsigned int, size);

  for (i = 0; i < size; i++)
    si->node_mapping[i] = i;

  si->scc_stack = VEC_alloc (unsigned, heap, 1);
  return si;
}

/* Free an SCC info structure pointed to by SI */

static void
free_scc_info (struct scc_info *si)
{
  sbitmap_free (si->visited);
  sbitmap_free (si->roots);
  free (si->node_mapping);
  free (si->dfs);
  VEC_free (unsigned, heap, si->scc_stack);
  free (si);
}


/* Find indirect cycles in GRAPH that occur, using strongly connected
   components, and note them in the indirect cycles map.

   This technique comes from Ben Hardekopf and Calvin Lin,
   "It Pays to be Lazy: Fast and Accurate Pointer Analysis for Millions of
   Lines of Code", submitted to PLDI 2007.  */

static void
find_indirect_cycles (constraint_graph_t graph)
{
  unsigned int i;
  unsigned int size = graph->size;
  struct scc_info *si = init_scc_info (size);

  for (i = 0; i < MIN (LAST_REF_NODE, size); i ++ )
    if (!TEST_BIT (si->visited, i) && find (i) == i)
      scc_visit (graph, si, i);

  free_scc_info (si);
}

/* Compute a topological ordering for GRAPH, and store the result in the
   topo_info structure TI.  */

static void
compute_topo_order (constraint_graph_t graph,
		    struct topo_info *ti)
{
  unsigned int i;
  unsigned int size = VEC_length (varinfo_t, varmap);

  for (i = 0; i != size; ++i)
    if (!TEST_BIT (ti->visited, i) && find (i) == i)
      topo_visit (graph, ti, i);
}

/* Perform offline variable substitution.

   This is a linear time way of identifying variables that must have
   equivalent points-to sets, including those caused by static cycles,
   and single entry subgraphs, in the constraint graph.

   The technique is described in "Off-line variable substitution for
   scaling points-to analysis" by Atanas Rountev and Satish Chandra,
   in "ACM SIGPLAN Notices" volume 35, number 5, pages 47-56.

   There is an optimal way to do this involving hash based value
   numbering, once the technique is published i will implement it
   here.  

   The general method of finding equivalence classes is as follows:
   Add fake nodes (REF nodes) and edges for *a = b and a = *b constraints.
   Add fake nodes (ADDRESS nodes) and edges for a = &b constraints.
   Initialize all non-REF/ADDRESS nodes to be direct nodes
   For each SCC in the predecessor graph:
      for each member (x) of the SCC
         if x is not a direct node:
	   set rootnode(SCC) to be not a direct node
	 collapse node x into rootnode(SCC).
      if rootnode(SCC) is not a direct node:
        label rootnode(SCC) with a new equivalence class
      else:
        if all labeled predecessors of rootnode(SCC) have the same
	label:
	  label rootnode(SCC) with this label
	else:
	  label rootnode(SCC) with a new equivalence class

   All direct nodes with the same equivalence class can be replaced
   with a single representative node.
   All unlabeled nodes (label == 0) are not pointers and all edges
   involving them can be eliminated.
   We perform these optimizations during move_complex_constraints.
*/

static int equivalence_class;

/* Recursive routine to find strongly connected components in GRAPH,
   and label it's nodes with equivalence classes.
   This is used during variable substitution to find cycles involving
   the regular or implicit predecessors, and label them as equivalent.
   The SCC finding algorithm used is the same as that for scc_visit.  */

static void
label_visit (constraint_graph_t graph, struct scc_info *si, unsigned int n)
{
  unsigned int i;
  bitmap_iterator bi;
  unsigned int my_dfs;

  gcc_assert (si->node_mapping[n] == n);
  SET_BIT (si->visited, n);
  si->dfs[n] = si->current_index ++;
  my_dfs = si->dfs[n];

  /* Visit all the successors.  */
  EXECUTE_IF_IN_NONNULL_BITMAP (graph->preds[n], 0, i, bi)
    {
      unsigned int w = si->node_mapping[i];

      if (TEST_BIT (si->roots, w))
	continue;

      if (!TEST_BIT (si->visited, w))
	label_visit (graph, si, w);
      {
	unsigned int t = si->node_mapping[w];
	unsigned int nnode = si->node_mapping[n];
	gcc_assert (nnode == n);

	if (si->dfs[t] < si->dfs[nnode])
	  si->dfs[n] = si->dfs[t];
      }
    }

  /* Visit all the implicit predecessors.  */
  EXECUTE_IF_IN_NONNULL_BITMAP (graph->implicit_preds[n], 0, i, bi)
    {
      unsigned int w = si->node_mapping[i];

      if (TEST_BIT (si->roots, w))
	continue;

      if (!TEST_BIT (si->visited, w))
	label_visit (graph, si, w);
      {
	unsigned int t = si->node_mapping[w];
	unsigned int nnode = si->node_mapping[n];
	gcc_assert (nnode == n);

	if (si->dfs[t] < si->dfs[nnode])
	  si->dfs[n] = si->dfs[t];
      }
    }

  /* See if any components have been identified.  */
  if (si->dfs[n] == my_dfs)
    {
      while (VEC_length (unsigned, si->scc_stack) != 0
	     && si->dfs[VEC_last (unsigned, si->scc_stack)] >= my_dfs)
	{
	  unsigned int w = VEC_pop (unsigned, si->scc_stack);
	  si->node_mapping[w] = n;

	  if (!TEST_BIT (graph->direct_nodes, w))
	    RESET_BIT (graph->direct_nodes, n);
	}
      SET_BIT (si->roots, n);

      if (!TEST_BIT (graph->direct_nodes, n))
	{
	  graph->label[n] = equivalence_class++;
	}
      else
	{
	  unsigned int size = 0;
	  unsigned int firstlabel = ~0;

	  EXECUTE_IF_IN_NONNULL_BITMAP (graph->preds[n], 0, i, bi)
	    {
	      unsigned int j = si->node_mapping[i];

	      if (j == n || graph->label[j] == 0)
		continue;

	      if (firstlabel == (unsigned int)~0)
		{
		  firstlabel = graph->label[j];
		  size++;
		}
	      else if (graph->label[j] != firstlabel)
		size++;
	    }

	  if (size == 0)
	    graph->label[n] = 0;
	  else if (size == 1)
	    graph->label[n] = firstlabel;
	  else
	    graph->label[n] = equivalence_class++;
	}
    }
  else
    VEC_safe_push (unsigned, heap, si->scc_stack, n);
}

/* Perform offline variable substitution, discovering equivalence
   classes, and eliminating non-pointer variables.  */

static struct scc_info *
perform_var_substitution (constraint_graph_t graph)
{
  unsigned int i;
  unsigned int size = graph->size;
  struct scc_info *si = init_scc_info (size);

  bitmap_obstack_initialize (&iteration_obstack);
  equivalence_class = 0;

  /* We only need to visit the non-address nodes for labeling
     purposes, as the address nodes will never have any predecessors,
     because &x never appears on the LHS of a constraint.  */
  for (i = 0; i < LAST_REF_NODE; i++)
    if (!TEST_BIT (si->visited, si->node_mapping[i]))
      label_visit (graph, si, si->node_mapping[i]);

  if (dump_file && (dump_flags & TDF_DETAILS))
    for (i = 0; i < FIRST_REF_NODE; i++)
      {
	bool direct_node = TEST_BIT (graph->direct_nodes, i);
	fprintf (dump_file,
		 "Equivalence class for %s node id %d:%s is %d\n",
		 direct_node ? "Direct node" : "Indirect node", i,
		 get_varinfo (i)->name,
		 graph->label[si->node_mapping[i]]);
      }

  /* Quickly eliminate our non-pointer variables.  */

  for (i = 0; i < FIRST_REF_NODE; i++)
    {
      unsigned int node = si->node_mapping[i];

      if (graph->label[node] == 0 && TEST_BIT (graph->direct_nodes, node))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file,
		     "%s is a non-pointer variable, eliminating edges.\n",
		     get_varinfo (node)->name);
	  stats.nonpointer_vars++;
	  clear_edges_for_node (graph, node);
	}
    }
  return si;
}

/* Free information that was only necessary for variable
   substitution.  */

static void
free_var_substitution_info (struct scc_info *si)
{
  free_scc_info (si);
  free (graph->label);
  free (graph->eq_rep);
  sbitmap_free (graph->direct_nodes);
  bitmap_obstack_release (&iteration_obstack);
}

/* Return an existing node that is equivalent to NODE, which has
   equivalence class LABEL, if one exists.  Return NODE otherwise.  */

static unsigned int
find_equivalent_node (constraint_graph_t graph,
		      unsigned int node, unsigned int label)
{
  /* If the address version of this variable is unused, we can
     substitute it for anything else with the same label.
     Otherwise, we know the pointers are equivalent, but not the
     locations.  */

  if (graph->label[FIRST_ADDR_NODE + node] == 0)
    {
      gcc_assert (label < graph->size);

      if (graph->eq_rep[label] != -1)
	{
	  /* Unify the two variables since we know they are equivalent.  */
	  if (unite (graph->eq_rep[label], node))
	    unify_nodes (graph, graph->eq_rep[label], node, false);
	  return graph->eq_rep[label];
	}
      else
	{
	  graph->eq_rep[label] = node;
	}
    }
  return node;
}

/* Move complex constraints to the appropriate nodes, and collapse
   variables we've discovered are equivalent during variable
   substitution.  SI is the SCC_INFO that is the result of
   perform_variable_substitution.  */

static void
move_complex_constraints (constraint_graph_t graph,
			  struct scc_info *si)
{
  int i;
  unsigned int j;
  constraint_t c;

  for (j = 0; j < graph->size; j++)
    gcc_assert (find (j) == j);

  for (i = 0; VEC_iterate (constraint_t, constraints, i, c); i++)
    {
      struct constraint_expr lhs = c->lhs;
      struct constraint_expr rhs = c->rhs;
      unsigned int lhsvar = find (get_varinfo_fc (lhs.var)->id);
      unsigned int rhsvar = find (get_varinfo_fc (rhs.var)->id);
      unsigned int lhsnode, rhsnode;
      unsigned int lhslabel, rhslabel;

      lhsnode = si->node_mapping[lhsvar];
      rhsnode = si->node_mapping[rhsvar];
      lhslabel = graph->label[lhsnode];
      rhslabel = graph->label[rhsnode];

      /* See if it is really a non-pointer variable, and if so, ignore
	 the constraint.  */
      if (lhslabel == 0)
	{
	  if (!TEST_BIT (graph->direct_nodes, lhsnode))
	    lhslabel = graph->label[lhsnode] = equivalence_class++;
	  else
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{

		  fprintf (dump_file, "%s is a non-pointer variable,"
			   "ignoring constraint:",
			   get_varinfo (lhs.var)->name);
		  dump_constraint (dump_file, c);
		}
	      VEC_replace (constraint_t, constraints, i, NULL);
	      continue;
	    }
	}

      if (rhslabel == 0)
	{
	  if (!TEST_BIT (graph->direct_nodes, rhsnode))
	    rhslabel = graph->label[rhsnode] = equivalence_class++;
	  else
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{

		  fprintf (dump_file, "%s is a non-pointer variable,"
			   "ignoring constraint:",
			   get_varinfo (rhs.var)->name);
		  dump_constraint (dump_file, c);
		}
	      VEC_replace (constraint_t, constraints, i, NULL);
	      continue;
	    }
	}

      lhsvar = find_equivalent_node (graph, lhsvar, lhslabel);
      rhsvar = find_equivalent_node (graph, rhsvar, rhslabel);
      c->lhs.var = lhsvar;
      c->rhs.var = rhsvar;

      if (lhs.type == DEREF)
	{
	  if (rhs.type == ADDRESSOF || rhsvar > anything_id)
	    insert_into_complex (graph, lhsvar, c);
	}
      else if (rhs.type == DEREF)
	{
	  if (!(get_varinfo (lhsvar)->is_special_var))
	    insert_into_complex (graph, rhsvar, c);
	}
      else if (rhs.type != ADDRESSOF && lhsvar > anything_id
	       && (lhs.offset != 0 || rhs.offset != 0))
	{
	  insert_into_complex (graph, rhsvar, c);
	}

    }
}

/* Eliminate indirect cycles involving NODE.  Return true if NODE was
   part of an SCC, false otherwise.  */

static bool
eliminate_indirect_cycles (unsigned int node)
{
  if (graph->indirect_cycles[node] != -1
      && !bitmap_empty_p (get_varinfo (node)->solution))
    {
      unsigned int i;
      VEC(unsigned,heap) *queue = NULL;
      int queuepos;
      unsigned int to = find (graph->indirect_cycles[node]);
      bitmap_iterator bi;

      /* We can't touch the solution set and call unify_nodes
	 at the same time, because unify_nodes is going to do
	 bitmap unions into it. */

      EXECUTE_IF_SET_IN_BITMAP (get_varinfo (node)->solution, 0, i, bi)
	{
	  if (find (i) == i && i != to)
	    {
	      if (unite (to, i))
		VEC_safe_push (unsigned, heap, queue, i);
	    }
	}

      for (queuepos = 0;
	   VEC_iterate (unsigned, queue, queuepos, i);
	   queuepos++)
	{
	  unify_nodes (graph, to, i, true);
	}
      VEC_free (unsigned, heap, queue);
      return true;
    }
  return false;
}

/* Solve the constraint graph GRAPH using our worklist solver.
   This is based on the PW* family of solvers from the "Efficient Field
   Sensitive Pointer Analysis for C" paper.
   It works by iterating over all the graph nodes, processing the complex
   constraints and propagating the copy constraints, until everything stops
   changed.  This corresponds to steps 6-8 in the solving list given above.  */

static void
solve_graph (constraint_graph_t graph)
{
  unsigned int size = VEC_length (varinfo_t, varmap);
  unsigned int i;
  bitmap pts;

  changed_count = 0;
  changed = sbitmap_alloc (size);
  sbitmap_zero (changed);

  /* Mark all initial non-collapsed nodes as changed.  */
  for (i = 0; i < size; i++)
    {
      varinfo_t ivi = get_varinfo (i);
      if (find (i) == i && !bitmap_empty_p (ivi->solution)
	  && ((graph->succs[i] && !bitmap_empty_p (graph->succs[i]))
	      || VEC_length (constraint_t, graph->complex[i]) > 0))
	{
	  SET_BIT (changed, i);
	  changed_count++;
	}
    }

  /* Allocate a bitmap to be used to store the changed bits.  */
  pts = BITMAP_ALLOC (&pta_obstack);

  while (changed_count > 0)
    {
      unsigned int i;
      struct topo_info *ti = init_topo_info ();
      stats.iterations++;

      bitmap_obstack_initialize (&iteration_obstack);

      compute_topo_order (graph, ti);

      while (VEC_length (unsigned, ti->topo_order) != 0)
	{

	  i = VEC_pop (unsigned, ti->topo_order);

	  /* If this variable is not a representative, skip it.  */
	  if (find (i) != i)
	    continue;

	  /* In certain indirect cycle cases, we may merge this
	     variable to another.  */
	  if (eliminate_indirect_cycles (i) && find (i) != i)
	    continue;

	  /* If the node has changed, we need to process the
	     complex constraints and outgoing edges again.  */
	  if (TEST_BIT (changed, i))
	    {
	      unsigned int j;
	      constraint_t c;
	      bitmap solution;
	      VEC(constraint_t,heap) *complex = graph->complex[i];
	      bool solution_empty;

	      RESET_BIT (changed, i);
	      changed_count--;

	      /* Compute the changed set of solution bits.  */
	      bitmap_and_compl (pts, get_varinfo (i)->solution,
				get_varinfo (i)->oldsolution);

	      if (bitmap_empty_p (pts))
		continue;

	      bitmap_ior_into (get_varinfo (i)->oldsolution, pts);

	      solution = get_varinfo (i)->solution;
	      solution_empty = bitmap_empty_p (solution);

	      /* Process the complex constraints */
	      for (j = 0; VEC_iterate (constraint_t, complex, j, c); j++)
		{
		  /* The only complex constraint that can change our
		     solution to non-empty, given an empty solution,
		     is a constraint where the lhs side is receiving
		     some set from elsewhere.  */
		  if (!solution_empty || c->lhs.type != DEREF)
		    do_complex_constraint (graph, c, pts);
		}

	      solution_empty = bitmap_empty_p (solution);

	      if (!solution_empty)
		{
		  bitmap_iterator bi;

		  /* Propagate solution to all successors.  */
		  EXECUTE_IF_IN_NONNULL_BITMAP (graph->succs[i],
						0, j, bi)
		    {
		      bitmap tmp;
		      bool flag;

		      unsigned int to = find (j);
		      tmp = get_varinfo (to)->solution;
		      flag = false;

		      /* Don't try to propagate to ourselves.  */
		      if (to == i)
			continue;

		      flag = set_union_with_increment (tmp, pts, 0);

		      if (flag)
			{
			  get_varinfo (to)->solution = tmp;
			  if (!TEST_BIT (changed, to))
			    {
			      SET_BIT (changed, to);
			      changed_count++;
			    }
			}
		    }
		}
	    }
	}
      free_topo_info (ti);
      bitmap_obstack_release (&iteration_obstack);
    }

  BITMAP_FREE (pts);
  sbitmap_free (changed);
  bitmap_obstack_release (&oldpta_obstack);
}

/* Map from trees to variable infos.  */
static struct pointer_map_t *vi_for_tree;


/* Insert ID as the variable id for tree T in the vi_for_tree map.  */

static void
insert_vi_for_tree (tree t, varinfo_t vi)
{
  void **slot = pointer_map_insert (vi_for_tree, t);
  gcc_assert (vi);
  gcc_assert (*slot == NULL);
  *slot = vi;
}

/* Find the variable info for tree T in VI_FOR_TREE.  If T does not
   exist in the map, return NULL, otherwise, return the varinfo we found.  */

static varinfo_t
lookup_vi_for_tree (tree t)
{
  void **slot = pointer_map_contains (vi_for_tree, t);
  if (slot == NULL)
    return NULL;

  return (varinfo_t) *slot;
}

/* Return a printable name for DECL  */

static const char *
alias_get_name (tree decl)
{
  const char *res = get_name (decl);
  char *temp;
  int num_printed = 0;

  if (res != NULL)
    return res;

  res = "NULL";
  if (!dump_file)
    return res;

  if (TREE_CODE (decl) == SSA_NAME)
    {
      num_printed = asprintf (&temp, "%s_%u",
			      alias_get_name (SSA_NAME_VAR (decl)),
			      SSA_NAME_VERSION (decl));
    }
  else if (DECL_P (decl))
    {
      num_printed = asprintf (&temp, "D.%u", DECL_UID (decl));
    }
  if (num_printed > 0)
    {
      res = ggc_strdup (temp);
      free (temp);
    }
  return res;
}

/* Find the variable id for tree T in the map.
   If T doesn't exist in the map, create an entry for it and return it.  */

static varinfo_t
get_vi_for_tree (tree t)
{
  void **slot = pointer_map_contains (vi_for_tree, t);
  if (slot == NULL)
    return get_varinfo (create_variable_info_for (t, alias_get_name (t)));

  return (varinfo_t) *slot;
}

/* Get a constraint expression from an SSA_VAR_P node.  */

static struct constraint_expr
get_constraint_exp_from_ssa_var (tree t)
{
  struct constraint_expr cexpr;

  gcc_assert (SSA_VAR_P (t) || DECL_P (t));

  /* For parameters, get at the points-to set for the actual parm
     decl.  */
  if (TREE_CODE (t) == SSA_NAME
      && TREE_CODE (SSA_NAME_VAR (t)) == PARM_DECL
      && default_def (SSA_NAME_VAR (t)) == t)
    return get_constraint_exp_from_ssa_var (SSA_NAME_VAR (t));

  cexpr.type = SCALAR;

  cexpr.var = get_vi_for_tree (t)->id;
  /* If we determine the result is "anything", and we know this is readonly,
     say it points to readonly memory instead.  */
  if (cexpr.var == anything_id && TREE_READONLY (t))
    {
      cexpr.type = ADDRESSOF;
      cexpr.var = readonly_id;
    }

  cexpr.offset = 0;
  return cexpr;
}

/* Process a completed constraint T, and add it to the constraint
   list.  */

static void
process_constraint (constraint_t t)
{
  struct constraint_expr rhs = t->rhs;
  struct constraint_expr lhs = t->lhs;
  
  gcc_assert (rhs.var < VEC_length (varinfo_t, varmap));
  gcc_assert (lhs.var < VEC_length (varinfo_t, varmap));

  if (lhs.type == DEREF)
    get_varinfo (lhs.var)->directly_dereferenced = true;
  if (rhs.type == DEREF)
    get_varinfo (rhs.var)->directly_dereferenced = true;

  if (!use_field_sensitive)
    {
      t->rhs.offset = 0;
      t->lhs.offset = 0;
    }

  /* ANYTHING == ANYTHING is pointless.  */
  if (lhs.var == anything_id && rhs.var == anything_id)
    return;

  /* If we have &ANYTHING = something, convert to SOMETHING = &ANYTHING) */
  else if (lhs.var == anything_id && lhs.type == ADDRESSOF)
    {
      rhs = t->lhs;
      t->lhs = t->rhs;
      t->rhs = rhs;
      process_constraint (t);
    }
  /* This can happen in our IR with things like n->a = *p */
  else if (rhs.type == DEREF && lhs.type == DEREF && rhs.var != anything_id)
    {
      /* Split into tmp = *rhs, *lhs = tmp */
      tree rhsdecl = get_varinfo (rhs.var)->decl;
      tree pointertype = TREE_TYPE (rhsdecl);
      tree pointedtotype = TREE_TYPE (pointertype);
      tree tmpvar = create_tmp_var_raw (pointedtotype, "doubledereftmp");
      struct constraint_expr tmplhs = get_constraint_exp_from_ssa_var (tmpvar);

      /* If this is an aggregate of known size, we should have passed
	 this off to do_structure_copy, and it should have broken it
	 up.  */
      gcc_assert (!AGGREGATE_TYPE_P (pointedtotype)
		  || get_varinfo (rhs.var)->is_unknown_size_var);

      process_constraint (new_constraint (tmplhs, rhs));
      process_constraint (new_constraint (lhs, tmplhs));
    }
  else
    {
      gcc_assert (rhs.type != ADDRESSOF || rhs.offset == 0);
      VEC_safe_push (constraint_t, heap, constraints, t);
    }
}

/* Return true if T is a variable of a type that could contain
   pointers.  */

static bool
could_have_pointers (tree t)
{
  tree type = TREE_TYPE (t);

  if (POINTER_TYPE_P (type)
      || AGGREGATE_TYPE_P (type)
      || TREE_CODE (type) == COMPLEX_TYPE)
    return true;

  return false;
}

/* Return the position, in bits, of FIELD_DECL from the beginning of its
   structure.  */

static unsigned HOST_WIDE_INT
bitpos_of_field (const tree fdecl)
{

  if (TREE_CODE (DECL_FIELD_OFFSET (fdecl)) != INTEGER_CST
      || TREE_CODE (DECL_FIELD_BIT_OFFSET (fdecl)) != INTEGER_CST)
    return -1;

  return (tree_low_cst (DECL_FIELD_OFFSET (fdecl), 1) * 8)
	 + tree_low_cst (DECL_FIELD_BIT_OFFSET (fdecl), 1);
}


/* Return true if an access to [ACCESSPOS, ACCESSSIZE]
   overlaps with a field at [FIELDPOS, FIELDSIZE] */

static bool
offset_overlaps_with_access (const unsigned HOST_WIDE_INT fieldpos,
			     const unsigned HOST_WIDE_INT fieldsize,
			     const unsigned HOST_WIDE_INT accesspos,
			     const unsigned HOST_WIDE_INT accesssize)
{
  if (fieldpos == accesspos && fieldsize == accesssize)
    return true;
  if (accesspos >= fieldpos && accesspos < (fieldpos + fieldsize))
    return true;
  if (accesspos < fieldpos && (accesspos + accesssize > fieldpos))
    return true;

  return false;
}

/* Given a COMPONENT_REF T, return the constraint_expr for it.  */

static void
get_constraint_for_component_ref (tree t, VEC(ce_s, heap) **results)
{
  tree orig_t = t;
  HOST_WIDE_INT bitsize = -1;
  HOST_WIDE_INT bitmaxsize = -1;
  HOST_WIDE_INT bitpos;
  tree forzero;
  struct constraint_expr *result;
  unsigned int beforelength = VEC_length (ce_s, *results);

  /* Some people like to do cute things like take the address of
     &0->a.b */
  forzero = t;
  while (!SSA_VAR_P (forzero) && !CONSTANT_CLASS_P (forzero))
    forzero = TREE_OPERAND (forzero, 0);

  if (CONSTANT_CLASS_P (forzero) && integer_zerop (forzero))
    {
      struct constraint_expr temp;

      temp.offset = 0;
      temp.var = integer_id;
      temp.type = SCALAR;
      VEC_safe_push (ce_s, heap, *results, &temp);
      return;
    }

  t = get_ref_base_and_extent (t, &bitpos, &bitsize, &bitmaxsize);

  /* String constants are readonly, so there is nothing to really do
     here.  */
  if (TREE_CODE (t) == STRING_CST)
    return;

  get_constraint_for (t, results);
  result = VEC_last (ce_s, *results);
  result->offset = bitpos;

  gcc_assert (beforelength + 1 == VEC_length (ce_s, *results));

  /* This can also happen due to weird offsetof type macros.  */
  if (TREE_CODE (t) != ADDR_EXPR && result->type == ADDRESSOF)
    result->type = SCALAR;

  if (result->type == SCALAR)
    {
      /* In languages like C, you can access one past the end of an
	 array.  You aren't allowed to dereference it, so we can
	 ignore this constraint. When we handle pointer subtraction,
	 we may have to do something cute here.  */

      if (result->offset < get_varinfo (result->var)->fullsize
	  && bitmaxsize != 0)
	{
	  /* It's also not true that the constraint will actually start at the
	     right offset, it may start in some padding.  We only care about
	     setting the constraint to the first actual field it touches, so
	     walk to find it.  */
	  varinfo_t curr;
	  for (curr = get_varinfo (result->var); curr; curr = curr->next)
	    {
	      if (offset_overlaps_with_access (curr->offset, curr->size,
					       result->offset, bitmaxsize))
		{
		  result->var = curr->id;
		  break;
		}
	    }
	  /* assert that we found *some* field there. The user couldn't be
	     accessing *only* padding.  */
	  /* Still the user could access one past the end of an array
	     embedded in a struct resulting in accessing *only* padding.  */
	  gcc_assert (curr || ref_contains_array_ref (orig_t));
	}
      else if (bitmaxsize == 0)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "Access to zero-sized part of variable,"
		     "ignoring\n");
	}
      else
	if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file, "Access to past the end of variable, ignoring\n");

      result->offset = 0;
    }
}


/* Dereference the constraint expression CONS, and return the result.
   DEREF (ADDRESSOF) = SCALAR
   DEREF (SCALAR) = DEREF
   DEREF (DEREF) = (temp = DEREF1; result = DEREF(temp))
   This is needed so that we can handle dereferencing DEREF constraints.  */

static void
do_deref (VEC (ce_s, heap) **constraints)
{
  struct constraint_expr *c;
  unsigned int i = 0;

  for (i = 0; VEC_iterate (ce_s, *constraints, i, c); i++)
    {
      if (c->type == SCALAR)
	c->type = DEREF;
      else if (c->type == ADDRESSOF)
	c->type = SCALAR;
      else if (c->type == DEREF)
	{
	  tree tmpvar = create_tmp_var_raw (ptr_type_node, "dereftmp");
	  struct constraint_expr tmplhs = get_constraint_exp_from_ssa_var (tmpvar);
	  process_constraint (new_constraint (tmplhs, *c));
	  c->var = tmplhs.var;
	}
      else
	gcc_unreachable ();
    }
}

/* Create a nonlocal variable of TYPE to represent nonlocals we can
   alias.  */

static tree
create_nonlocal_var (tree type)
{
  tree nonlocal = create_tmp_var_raw (type, "NONLOCAL");
  
  if (referenced_vars)
    add_referenced_var (nonlocal);

  DECL_EXTERNAL (nonlocal) = 1;
  return nonlocal;
}

/* Given a tree T, return the constraint expression for it.  */

static void
get_constraint_for (tree t, VEC (ce_s, heap) **results)
{
  struct constraint_expr temp;

  /* x = integer is all glommed to a single variable, which doesn't
     point to anything by itself.  That is, of course, unless it is an
     integer constant being treated as a pointer, in which case, we
     will return that this is really the addressof anything.  This
     happens below, since it will fall into the default case. The only
     case we know something about an integer treated like a pointer is
     when it is the NULL pointer, and then we just say it points to
     NULL.  */
  if (TREE_CODE (t) == INTEGER_CST
      && !POINTER_TYPE_P (TREE_TYPE (t)))
    {
      temp.var = integer_id;
      temp.type = SCALAR;
      temp.offset = 0;
      VEC_safe_push (ce_s, heap, *results, &temp);
      return;
    }
  else if (TREE_CODE (t) == INTEGER_CST
	   && integer_zerop (t))
    {
      temp.var = nothing_id;
      temp.type = ADDRESSOF;
      temp.offset = 0;
      VEC_safe_push (ce_s, heap, *results, &temp);
      return;
    }

  switch (TREE_CODE_CLASS (TREE_CODE (t)))
    {
    case tcc_expression:
      {
	switch (TREE_CODE (t))
	  {
	  case ADDR_EXPR:
	    {
	      struct constraint_expr *c;
	      unsigned int i;
	      tree exp = TREE_OPERAND (t, 0);
	      tree pttype = TREE_TYPE (TREE_TYPE (t));

	      get_constraint_for (exp, results);

	      /* Make sure we capture constraints to all elements
		 of an array.  */
	      if ((handled_component_p (exp)
		   && ref_contains_array_ref (exp))
		  || TREE_CODE (TREE_TYPE (exp)) == ARRAY_TYPE)
		{
		  struct constraint_expr *origrhs;
		  varinfo_t origvar;
		  struct constraint_expr tmp;

		  if (VEC_length (ce_s, *results) == 0)
		    return;

		  gcc_assert (VEC_length (ce_s, *results) == 1);
		  origrhs = VEC_last (ce_s, *results);
		  tmp = *origrhs;
		  VEC_pop (ce_s, *results);
		  origvar = get_varinfo (origrhs->var);
		  for (; origvar; origvar = origvar->next)
		    {
		      tmp.var = origvar->id;
		      VEC_safe_push (ce_s, heap, *results, &tmp);
		    }
		}
	      else if (VEC_length (ce_s, *results) == 1
		       && (AGGREGATE_TYPE_P (pttype)
			   || TREE_CODE (pttype) == COMPLEX_TYPE))
		{
		  struct constraint_expr *origrhs;
		  varinfo_t origvar;
		  struct constraint_expr tmp;

		  gcc_assert (VEC_length (ce_s, *results) == 1);
		  origrhs = VEC_last (ce_s, *results);
		  tmp = *origrhs;
		  VEC_pop (ce_s, *results);
		  origvar = get_varinfo (origrhs->var);
		  for (; origvar; origvar = origvar->next)
		    {
		      tmp.var = origvar->id;
		      VEC_safe_push (ce_s, heap, *results, &tmp);
		    }
		}

	      for (i = 0; VEC_iterate (ce_s, *results, i, c); i++)
		{
		  if (c->type == DEREF)
		    c->type = SCALAR;
		  else
		    c->type = ADDRESSOF;
		}
	      return;
	    }
	    break;
	  case CALL_EXPR:
	    /* XXX: In interprocedural mode, if we didn't have the
	       body, we would need to do *each pointer argument =
	       &ANYTHING added.  */
	    if (call_expr_flags (t) & (ECF_MALLOC | ECF_MAY_BE_ALLOCA))
	      {
		varinfo_t vi;
		tree heapvar = heapvar_lookup (t);

		if (heapvar == NULL)
		  {
		    heapvar = create_tmp_var_raw (ptr_type_node, "HEAP");
		    DECL_EXTERNAL (heapvar) = 1;
		    if (referenced_vars)
		      add_referenced_var (heapvar);
		    heapvar_insert (t, heapvar);
		  }

		temp.var = create_variable_info_for (heapvar,
						     alias_get_name (heapvar));

		vi = get_varinfo (temp.var);
		vi->is_artificial_var = 1;
		vi->is_heap_var = 1;
		temp.type = ADDRESSOF;
		temp.offset = 0;
		VEC_safe_push (ce_s, heap, *results, &temp);
		return;
	      }
	    else
	      {
		temp.var = escaped_vars_id;
		temp.type = SCALAR;
		temp.offset = 0;
		VEC_safe_push (ce_s, heap, *results, &temp);
		return;
	      }
	    break;
	  default:
	    {
	      temp.type = ADDRESSOF;
	      temp.var = anything_id;
	      temp.offset = 0;
	      VEC_safe_push (ce_s, heap, *results, &temp);
	      return;
	    }
	  }
      }
    case tcc_reference:
      {
	switch (TREE_CODE (t))
	  {
	  case INDIRECT_REF:
	    {
	      get_constraint_for (TREE_OPERAND (t, 0), results);
	      do_deref (results);
	      return;
	    }
	  case ARRAY_REF:
	  case ARRAY_RANGE_REF:
	  case COMPONENT_REF:
	    get_constraint_for_component_ref (t, results);
	    return;
	  default:
	    {
	      temp.type = ADDRESSOF;
	      temp.var = anything_id;
	      temp.offset = 0;
	      VEC_safe_push (ce_s, heap, *results, &temp);
	      return;
	    }
	  }
      }
    case tcc_unary:
      {
	switch (TREE_CODE (t))
	  {
	  case NOP_EXPR:
	  case CONVERT_EXPR:
	  case NON_LVALUE_EXPR:
	    {
	      tree op = TREE_OPERAND (t, 0);

	      /* Cast from non-pointer to pointers are bad news for us.
		 Anything else, we see through */
	      if (!(POINTER_TYPE_P (TREE_TYPE (t))
		    && ! POINTER_TYPE_P (TREE_TYPE (op))))
		{
		  get_constraint_for (op, results);
		  return;
		}

	      /* FALLTHRU  */
	    }
	  default:
	    {
	      temp.type = ADDRESSOF;
	      temp.var = anything_id;
	      temp.offset = 0;
	      VEC_safe_push (ce_s, heap, *results, &temp);
	      return;
	    }
	  }
      }
    case tcc_exceptional:
      {
	switch (TREE_CODE (t))
	  {
	  case PHI_NODE:
	    {
	      get_constraint_for (PHI_RESULT (t), results);
	      return;
	    }
	    break;
	  case SSA_NAME:
	    {
	      struct constraint_expr temp;
	      temp = get_constraint_exp_from_ssa_var (t);
	      VEC_safe_push (ce_s, heap, *results, &temp);
	      return;
	    }
	    break;
	  default:
	    {
	      temp.type = ADDRESSOF;
	      temp.var = anything_id;
	      temp.offset = 0;
	      VEC_safe_push (ce_s, heap, *results, &temp);
	      return;
	    }
	  }
      }
    case tcc_declaration:
      {
	struct constraint_expr temp;
	temp = get_constraint_exp_from_ssa_var (t);
	VEC_safe_push (ce_s, heap, *results, &temp);
	return;
      }
    default:
      {
	temp.type = ADDRESSOF;
	temp.var = anything_id;
	temp.offset = 0;
	VEC_safe_push (ce_s, heap, *results, &temp);
	return;
      }
    }
}


/* Handle the structure copy case where we have a simple structure copy
   between LHS and RHS that is of SIZE (in bits)

   For each field of the lhs variable (lhsfield)
     For each field of the rhs variable at lhsfield.offset (rhsfield)
       add the constraint lhsfield = rhsfield

   If we fail due to some kind of type unsafety or other thing we
   can't handle, return false.  We expect the caller to collapse the
   variable in that case.  */

static bool
do_simple_structure_copy (const struct constraint_expr lhs,
			  const struct constraint_expr rhs,
			  const unsigned HOST_WIDE_INT size)
{
  varinfo_t p = get_varinfo (lhs.var);
  unsigned HOST_WIDE_INT pstart, last;
  pstart = p->offset;
  last = p->offset + size;
  for (; p && p->offset < last; p = p->next)
    {
      varinfo_t q;
      struct constraint_expr templhs = lhs;
      struct constraint_expr temprhs = rhs;
      unsigned HOST_WIDE_INT fieldoffset;

      templhs.var = p->id;
      q = get_varinfo (temprhs.var);
      fieldoffset = p->offset - pstart;
      q = first_vi_for_offset (q, q->offset + fieldoffset);
      if (!q)
	return false;
      temprhs.var = q->id;
      process_constraint (new_constraint (templhs, temprhs));
    }
  return true;
}


/* Handle the structure copy case where we have a  structure copy between a
   aggregate on the LHS and a dereference of a pointer on the RHS
   that is of SIZE (in bits)

   For each field of the lhs variable (lhsfield)
       rhs.offset = lhsfield->offset
       add the constraint lhsfield = rhs
*/

static void
do_rhs_deref_structure_copy (const struct constraint_expr lhs,
			     const struct constraint_expr rhs,
			     const unsigned HOST_WIDE_INT size)
{
  varinfo_t p = get_varinfo (lhs.var);
  unsigned HOST_WIDE_INT pstart,last;
  pstart = p->offset;
  last = p->offset + size;

  for (; p && p->offset < last; p = p->next)
    {
      varinfo_t q;
      struct constraint_expr templhs = lhs;
      struct constraint_expr temprhs = rhs;
      unsigned HOST_WIDE_INT fieldoffset;


      if (templhs.type == SCALAR)
	templhs.var = p->id;
      else
	templhs.offset = p->offset;

      q = get_varinfo (temprhs.var);
      fieldoffset = p->offset - pstart;
      temprhs.offset += fieldoffset;
      process_constraint (new_constraint (templhs, temprhs));
    }
}

/* Handle the structure copy case where we have a structure copy
   between a aggregate on the RHS and a dereference of a pointer on
   the LHS that is of SIZE (in bits)

   For each field of the rhs variable (rhsfield)
       lhs.offset = rhsfield->offset
       add the constraint lhs = rhsfield
*/

static void
do_lhs_deref_structure_copy (const struct constraint_expr lhs,
			     const struct constraint_expr rhs,
			     const unsigned HOST_WIDE_INT size)
{
  varinfo_t p = get_varinfo (rhs.var);
  unsigned HOST_WIDE_INT pstart,last;
  pstart = p->offset;
  last = p->offset + size;

  for (; p && p->offset < last; p = p->next)
    {
      varinfo_t q;
      struct constraint_expr templhs = lhs;
      struct constraint_expr temprhs = rhs;
      unsigned HOST_WIDE_INT fieldoffset;


      if (temprhs.type == SCALAR)
	temprhs.var = p->id;
      else
	temprhs.offset = p->offset;

      q = get_varinfo (templhs.var);
      fieldoffset = p->offset - pstart;
      templhs.offset += fieldoffset;
      process_constraint (new_constraint (templhs, temprhs));
    }
}

/* Sometimes, frontends like to give us bad type information.  This
   function will collapse all the fields from VAR to the end of VAR,
   into VAR, so that we treat those fields as a single variable.
   We return the variable they were collapsed into.  */

static unsigned int
collapse_rest_of_var (unsigned int var)
{
  varinfo_t currvar = get_varinfo (var);
  varinfo_t field;

  for (field = currvar->next; field; field = field->next)
    {
      if (dump_file)
	fprintf (dump_file, "Type safety: Collapsing var %s into %s\n",
		 field->name, currvar->name);

      gcc_assert (!field->collapsed_to);
      field->collapsed_to = currvar;
    }

  currvar->next = NULL;
  currvar->size = currvar->fullsize - currvar->offset;

  return currvar->id;
}

/* Handle aggregate copies by expanding into copies of the respective
   fields of the structures.  */

static void
do_structure_copy (tree lhsop, tree rhsop)
{
  struct constraint_expr lhs, rhs, tmp;
  VEC (ce_s, heap) *lhsc = NULL, *rhsc = NULL;
  varinfo_t p;
  unsigned HOST_WIDE_INT lhssize;
  unsigned HOST_WIDE_INT rhssize;

  get_constraint_for (lhsop, &lhsc);
  get_constraint_for (rhsop, &rhsc);
  gcc_assert (VEC_length (ce_s, lhsc) == 1);
  gcc_assert (VEC_length (ce_s, rhsc) == 1);
  lhs = *(VEC_last (ce_s, lhsc));
  rhs = *(VEC_last (ce_s, rhsc));

  VEC_free (ce_s, heap, lhsc);
  VEC_free (ce_s, heap, rhsc);

  /* If we have special var = x, swap it around.  */
  if (lhs.var <= integer_id && !(get_varinfo (rhs.var)->is_special_var))
    {
      tmp = lhs;
      lhs = rhs;
      rhs = tmp;
    }

  /*  This is fairly conservative for the RHS == ADDRESSOF case, in that it's
      possible it's something we could handle.  However, most cases falling
      into this are dealing with transparent unions, which are slightly
      weird. */
  if (rhs.type == ADDRESSOF && !(get_varinfo (rhs.var)->is_special_var))
    {
      rhs.type = ADDRESSOF;
      rhs.var = anything_id;
    }

  /* If the RHS is a special var, or an addressof, set all the LHS fields to
     that special var.  */
  if (rhs.var <= integer_id)
    {
      for (p = get_varinfo (lhs.var); p; p = p->next)
	{
	  struct constraint_expr templhs = lhs;
	  struct constraint_expr temprhs = rhs;

	  if (templhs.type == SCALAR )
	    templhs.var = p->id;
	  else
	    templhs.offset += p->offset;
	  process_constraint (new_constraint (templhs, temprhs));
	}
    }
  else
    {
      tree rhstype = TREE_TYPE (rhsop);
      tree lhstype = TREE_TYPE (lhsop);
      tree rhstypesize;
      tree lhstypesize;

      lhstypesize = DECL_P (lhsop) ? DECL_SIZE (lhsop) : TYPE_SIZE (lhstype);
      rhstypesize = DECL_P (rhsop) ? DECL_SIZE (rhsop) : TYPE_SIZE (rhstype);

      /* If we have a variably sized types on the rhs or lhs, and a deref
	 constraint, add the constraint, lhsconstraint = &ANYTHING.
	 This is conservatively correct because either the lhs is an unknown
	 sized var (if the constraint is SCALAR), or the lhs is a DEREF
	 constraint, and every variable it can point to must be unknown sized
	 anyway, so we don't need to worry about fields at all.  */
      if ((rhs.type == DEREF && TREE_CODE (rhstypesize) != INTEGER_CST)
	  || (lhs.type == DEREF && TREE_CODE (lhstypesize) != INTEGER_CST))
	{
	  rhs.var = anything_id;
	  rhs.type = ADDRESSOF;
	  rhs.offset = 0;
	  process_constraint (new_constraint (lhs, rhs));
	  return;
	}

      /* The size only really matters insofar as we don't set more or less of
	 the variable.  If we hit an unknown size var, the size should be the
	 whole darn thing.  */
      if (get_varinfo (rhs.var)->is_unknown_size_var)
	rhssize = ~0;
      else
	rhssize = TREE_INT_CST_LOW (rhstypesize);

      if (get_varinfo (lhs.var)->is_unknown_size_var)
	lhssize = ~0;
      else
	lhssize = TREE_INT_CST_LOW (lhstypesize);


      if (rhs.type == SCALAR && lhs.type == SCALAR)
	{
	  if (!do_simple_structure_copy (lhs, rhs, MIN (lhssize, rhssize)))
	    {
	      lhs.var = collapse_rest_of_var (lhs.var);
	      rhs.var = collapse_rest_of_var (rhs.var);
	      lhs.offset = 0;
	      rhs.offset = 0;
	      lhs.type = SCALAR;
	      rhs.type = SCALAR;
	      process_constraint (new_constraint (lhs, rhs));
	    }
	}
      else if (lhs.type != DEREF && rhs.type == DEREF)
	do_rhs_deref_structure_copy (lhs, rhs, MIN (lhssize, rhssize));
      else if (lhs.type == DEREF && rhs.type != DEREF)
	do_lhs_deref_structure_copy (lhs, rhs, MIN (lhssize, rhssize));
      else
	{
	  tree pointedtotype = lhstype;
	  tree tmpvar;

	  gcc_assert (rhs.type == DEREF && lhs.type == DEREF);
	  tmpvar = create_tmp_var_raw (pointedtotype, "structcopydereftmp");
	  do_structure_copy (tmpvar, rhsop);
	  do_structure_copy (lhsop, tmpvar);
	}
    }
}


/* Update related alias information kept in AI.  This is used when
   building name tags, alias sets and deciding grouping heuristics.
   STMT is the statement to process.  This function also updates
   ADDRESSABLE_VARS.  */

static void
update_alias_info (tree stmt, struct alias_info *ai)
{
  bitmap addr_taken;
  use_operand_p use_p;
  ssa_op_iter iter;
  enum escape_type stmt_escape_type = is_escape_site (stmt);
  tree op;

  if (stmt_escape_type == ESCAPE_TO_CALL
      || stmt_escape_type == ESCAPE_TO_PURE_CONST)
    {
      ai->num_calls_found++;
      if (stmt_escape_type == ESCAPE_TO_PURE_CONST)
	ai->num_pure_const_calls_found++;
    }

  /* Mark all the variables whose address are taken by the statement.  */
  addr_taken = addresses_taken (stmt);
  if (addr_taken)
    {
      bitmap_ior_into (addressable_vars, addr_taken);

      /* If STMT is an escape point, all the addresses taken by it are
	 call-clobbered.  */
      if (stmt_escape_type != NO_ESCAPE)
	{
	  bitmap_iterator bi;
	  unsigned i;

	  EXECUTE_IF_SET_IN_BITMAP (addr_taken, 0, i, bi)
	    {
	      tree rvar = referenced_var (i);
	      if (!unmodifiable_var_p (rvar))
		mark_call_clobbered (rvar, stmt_escape_type);
	    }
	}
    }

  /* Process each operand use.  If an operand may be aliased, keep
     track of how many times it's being used.  For pointers, determine
     whether they are dereferenced by the statement, or whether their
     value escapes, etc.  */
  FOR_EACH_PHI_OR_STMT_USE (use_p, stmt, iter, SSA_OP_USE)
    {
      tree op, var;
      var_ann_t v_ann;
      struct ptr_info_def *pi;
      bool is_store, is_potential_deref;
      unsigned num_uses, num_derefs;

      op = USE_FROM_PTR (use_p);

      /* If STMT is a PHI node, OP may be an ADDR_EXPR.  If so, add it
	 to the set of addressable variables.  */
      if (TREE_CODE (op) == ADDR_EXPR)
	{
	  gcc_assert (TREE_CODE (stmt) == PHI_NODE);

	  /* PHI nodes don't have annotations for pinning the set
	     of addresses taken, so we collect them here.

	     FIXME, should we allow PHI nodes to have annotations
	     so that they can be treated like regular statements?
	     Currently, they are treated as second-class
	     statements.  */
	  add_to_addressable_set (TREE_OPERAND (op, 0), &addressable_vars);
	  continue;
	}

      /* Ignore constants.  */
      if (TREE_CODE (op) != SSA_NAME)
	continue;

      var = SSA_NAME_VAR (op);
      v_ann = var_ann (var);

      /* The base variable of an ssa name must be a GIMPLE register, and thus
	 it cannot be aliased.  */
      gcc_assert (!may_be_aliased (var));

      /* We are only interested in pointers.  */
      if (!POINTER_TYPE_P (TREE_TYPE (op)))
	continue;

      pi = get_ptr_info (op);

      /* Add OP to AI->PROCESSED_PTRS, if it's not there already.  */
      if (!TEST_BIT (ai->ssa_names_visited, SSA_NAME_VERSION (op)))
	{
	  SET_BIT (ai->ssa_names_visited, SSA_NAME_VERSION (op));
	  VEC_safe_push (tree, heap, ai->processed_ptrs, op);
	}

      /* If STMT is a PHI node, then it will not have pointer
	 dereferences and it will not be an escape point.  */
      if (TREE_CODE (stmt) == PHI_NODE)
	continue;

      /* Determine whether OP is a dereferenced pointer, and if STMT
	 is an escape point, whether OP escapes.  */
      count_uses_and_derefs (op, stmt, &num_uses, &num_derefs, &is_store);

      /* Handle a corner case involving address expressions of the
	 form '&PTR->FLD'.  The problem with these expressions is that
	 they do not represent a dereference of PTR.  However, if some
	 other transformation propagates them into an INDIRECT_REF
	 expression, we end up with '*(&PTR->FLD)' which is folded
	 into 'PTR->FLD'.

	 So, if the original code had no other dereferences of PTR,
	 the aliaser will not create memory tags for it, and when
	 &PTR->FLD gets propagated to INDIRECT_REF expressions, the
	 memory operations will receive no V_MAY_DEF/VUSE operands.

	 One solution would be to have count_uses_and_derefs consider
	 &PTR->FLD a dereference of PTR.  But that is wrong, since it
	 is not really a dereference but an offset calculation.

	 What we do here is to recognize these special ADDR_EXPR
	 nodes.  Since these expressions are never GIMPLE values (they
	 are not GIMPLE invariants), they can only appear on the RHS
	 of an assignment and their base address is always an
	 INDIRECT_REF expression.  */
      is_potential_deref = false;
      if (TREE_CODE (stmt) == MODIFY_EXPR
	  && TREE_CODE (TREE_OPERAND (stmt, 1)) == ADDR_EXPR
	  && !is_gimple_val (TREE_OPERAND (stmt, 1)))
	{
	  /* If the RHS if of the form &PTR->FLD and PTR == OP, then
	     this represents a potential dereference of PTR.  */
	  tree rhs = TREE_OPERAND (stmt, 1);
	  tree base = get_base_address (TREE_OPERAND (rhs, 0));
	  if (TREE_CODE (base) == INDIRECT_REF
	      && TREE_OPERAND (base, 0) == op)
	    is_potential_deref = true;
	}

      if (num_derefs > 0 || is_potential_deref)
	{
	  /* Mark OP as dereferenced.  In a subsequent pass,
	     dereferenced pointers that point to a set of
	     variables will be assigned a name tag to alias
	     all the variables OP points to.  */
	  pi->is_dereferenced = 1;

	  /* Keep track of how many time we've dereferenced each
	     pointer.  */
	  NUM_REFERENCES_INC (v_ann);

	  /* If this is a store operation, mark OP as being
	     dereferenced to store, otherwise mark it as being
	     dereferenced to load.  */
	  if (is_store)
	    bitmap_set_bit (ai->dereferenced_ptrs_store, DECL_UID (var));
	  else
	    bitmap_set_bit (ai->dereferenced_ptrs_load, DECL_UID (var));
	}

      if (stmt_escape_type != NO_ESCAPE && num_derefs < num_uses)
	{
	  /* If STMT is an escape point and STMT contains at
	     least one direct use of OP, then the value of OP
	     escapes and so the pointed-to variables need to
	     be marked call-clobbered.  */
	  pi->value_escapes_p = 1;
	  pi->escape_mask |= stmt_escape_type;

	  /* If the statement makes a function call, assume
	     that pointer OP will be dereferenced in a store
	     operation inside the called function.  */
	  if (get_call_expr_in (stmt)
	      || stmt_escape_type == ESCAPE_STORED_IN_GLOBAL)
	    {
	      bitmap_set_bit (ai->dereferenced_ptrs_store, DECL_UID (var));
	      pi->is_dereferenced = 1;
	    }
	}
    }

  if (TREE_CODE (stmt) == PHI_NODE)
    return;

  /* Update reference counter for definitions to any
     potentially aliased variable.  This is used in the alias
     grouping heuristics.  */
  FOR_EACH_SSA_TREE_OPERAND (op, stmt, iter, SSA_OP_DEF)
    {
      tree var = SSA_NAME_VAR (op);
      var_ann_t ann = var_ann (var);
      bitmap_set_bit (ai->written_vars, DECL_UID (var));
      if (may_be_aliased (var))
	NUM_REFERENCES_INC (ann);
      
    }
  
  /* Mark variables in V_MAY_DEF operands as being written to.  */
  FOR_EACH_SSA_TREE_OPERAND (op, stmt, iter, SSA_OP_VIRTUAL_DEFS)
    {
      tree var = DECL_P (op) ? op : SSA_NAME_VAR (op);
      bitmap_set_bit (ai->written_vars, DECL_UID (var));
    }
}

/* Handle pointer arithmetic EXPR when creating aliasing constraints.
   Expressions of the type PTR + CST can be handled in two ways:

   1- If the constraint for PTR is ADDRESSOF for a non-structure
      variable, then we can use it directly because adding or
      subtracting a constant may not alter the original ADDRESSOF
      constraint (i.e., pointer arithmetic may not legally go outside
      an object's boundaries).

   2- If the constraint for PTR is ADDRESSOF for a structure variable,
      then if CST is a compile-time constant that can be used as an
      offset, we can determine which sub-variable will be pointed-to
      by the expression.

   Return true if the expression is handled.  For any other kind of
   expression, return false so that each operand can be added as a
   separate constraint by the caller.  */

static bool
handle_ptr_arith (VEC (ce_s, heap) *lhsc, tree expr)
{
  tree op0, op1;
  struct constraint_expr *c, *c2;
  unsigned int i = 0;
  unsigned int j = 0;
  VEC (ce_s, heap) *temp = NULL;
  unsigned HOST_WIDE_INT rhsoffset = 0;

  if (TREE_CODE (expr) != PLUS_EXPR
      && TREE_CODE (expr) != MINUS_EXPR)
    return false;

  op0 = TREE_OPERAND (expr, 0);
  op1 = TREE_OPERAND (expr, 1);

  get_constraint_for (op0, &temp);
  if (POINTER_TYPE_P (TREE_TYPE (op0))
      && host_integerp (op1, 1)
      && TREE_CODE (expr) == PLUS_EXPR)
    {
      if ((TREE_INT_CST_LOW (op1) * BITS_PER_UNIT) / BITS_PER_UNIT
	  != TREE_INT_CST_LOW (op1))
	return false;
      rhsoffset = TREE_INT_CST_LOW (op1) * BITS_PER_UNIT;
    }
  else
    return false;


  for (i = 0; VEC_iterate (ce_s, lhsc, i, c); i++)
    for (j = 0; VEC_iterate (ce_s, temp, j, c2); j++)
      {
	if (c2->type == ADDRESSOF && rhsoffset != 0)
	  {
	    varinfo_t temp = get_varinfo (c2->var);

	    /* An access one after the end of an array is valid,
	       so simply punt on accesses we cannot resolve.  */
	    temp = first_vi_for_offset (temp, rhsoffset);
	    if (temp == NULL)
	      continue;
	    c2->var = temp->id;
	    c2->offset = 0;
	  }
	else
	  c2->offset = rhsoffset;
	process_constraint (new_constraint (*c, *c2));
      }

  VEC_free (ce_s, heap, temp);

  return true;
}


/* Walk statement T setting up aliasing constraints according to the
   references found in T.  This function is the main part of the
   constraint builder.  AI points to auxiliary alias information used
   when building alias sets and computing alias grouping heuristics.  */

static void
find_func_aliases (tree origt)
{
  tree t = origt;
  VEC(ce_s, heap) *lhsc = NULL;
  VEC(ce_s, heap) *rhsc = NULL;
  struct constraint_expr *c;

  if (TREE_CODE (t) == RETURN_EXPR && TREE_OPERAND (t, 0))
    t = TREE_OPERAND (t, 0);

  /* Now build constraints expressions.  */
  if (TREE_CODE (t) == PHI_NODE)
    {
      gcc_assert (!AGGREGATE_TYPE_P (TREE_TYPE (PHI_RESULT (t))));

      /* Only care about pointers and structures containing
	 pointers.  */
      if (could_have_pointers (PHI_RESULT (t)))
	{
	  int i;
	  unsigned int j;

	  /* For a phi node, assign all the arguments to
	     the result.  */
	  get_constraint_for (PHI_RESULT (t), &lhsc);
	  for (i = 0; i < PHI_NUM_ARGS (t); i++)
	    {
	      tree rhstype;
	      tree strippedrhs = PHI_ARG_DEF (t, i);

	      STRIP_NOPS (strippedrhs);
	      rhstype = TREE_TYPE (strippedrhs);
	      get_constraint_for (PHI_ARG_DEF (t, i), &rhsc);

	      for (j = 0; VEC_iterate (ce_s, lhsc, j, c); j++)
		{
		  struct constraint_expr *c2;
		  while (VEC_length (ce_s, rhsc) > 0)
		    {
		      c2 = VEC_last (ce_s, rhsc);
		      process_constraint (new_constraint (*c, *c2));
		      VEC_pop (ce_s, rhsc);
		    }
		}
	    } 
	}
    }
  /* In IPA mode, we need to generate constraints to pass call
     arguments through their calls.   There are two case, either a
     modify_expr when we are returning a value, or just a plain
     call_expr when we are not.   */
  else if (in_ipa_mode
	   && ((TREE_CODE (t) == MODIFY_EXPR 
		&& TREE_CODE (TREE_OPERAND (t, 1)) == CALL_EXPR
	       && !(call_expr_flags (TREE_OPERAND (t, 1)) 
		    & (ECF_MALLOC | ECF_MAY_BE_ALLOCA)))
	       || (TREE_CODE (t) == CALL_EXPR 
		   && !(call_expr_flags (t) 
			& (ECF_MALLOC | ECF_MAY_BE_ALLOCA)))))
    {
      tree lhsop;
      tree rhsop;
      tree arglist;
      varinfo_t fi;
      int i = 1;
      tree decl;
      if (TREE_CODE (t) == MODIFY_EXPR)
	{
	  lhsop = TREE_OPERAND (t, 0);
	  rhsop = TREE_OPERAND (t, 1);
	}
      else
	{
	  lhsop = NULL;
	  rhsop = t;
	}
      decl = get_callee_fndecl (rhsop);

      /* If we can directly resolve the function being called, do so.
	 Otherwise, it must be some sort of indirect expression that
	 we should still be able to handle.  */
      if (decl)
	{
	  fi = get_vi_for_tree (decl);
	}
      else
	{
	  decl = TREE_OPERAND (rhsop, 0);
	  fi = get_vi_for_tree (decl);
	}

      /* Assign all the passed arguments to the appropriate incoming
	 parameters of the function.  */
      arglist = TREE_OPERAND (rhsop, 1);
	
      for (;arglist; arglist = TREE_CHAIN (arglist))
	{
	  tree arg = TREE_VALUE (arglist);
	  struct constraint_expr lhs ;
	  struct constraint_expr *rhsp;

	  get_constraint_for (arg, &rhsc);
	  if (TREE_CODE (decl) != FUNCTION_DECL)
	    {
	      lhs.type = DEREF;
	      lhs.var = fi->id;
	      lhs.offset = i;
	    }
	  else
	    {
	      lhs.type = SCALAR;
	      lhs.var = first_vi_for_offset (fi, i)->id;
	      lhs.offset = 0;
	    }
	  while (VEC_length (ce_s, rhsc) != 0)
	    {
	      rhsp = VEC_last (ce_s, rhsc);
	      process_constraint (new_constraint (lhs, *rhsp));
	      VEC_pop (ce_s, rhsc);
	    }
	  i++;
	}

      /* If we are returning a value, assign it to the result.  */
      if (lhsop)
	{
	  struct constraint_expr rhs;
	  struct constraint_expr *lhsp;
	  unsigned int j = 0;

	  get_constraint_for (lhsop, &lhsc);
	  if (TREE_CODE (decl) != FUNCTION_DECL)
	    {
	      rhs.type = DEREF;
	      rhs.var = fi->id;
	      rhs.offset = i;
	    }
	  else
	    {
	      rhs.type = SCALAR;
	      rhs.var = first_vi_for_offset (fi, i)->id;
	      rhs.offset = 0;
	    }
	  for (j = 0; VEC_iterate (ce_s, lhsc, j, lhsp); j++)
	    process_constraint (new_constraint (*lhsp, rhs));
	}
    }
  /* Otherwise, just a regular assignment statement.  */
  else if (TREE_CODE (t) == MODIFY_EXPR)
    {
      tree lhsop = TREE_OPERAND (t, 0);
      tree rhsop = TREE_OPERAND (t, 1);
      int i;

      if ((AGGREGATE_TYPE_P (TREE_TYPE (lhsop))
	   || TREE_CODE (TREE_TYPE (lhsop)) == COMPLEX_TYPE)
	  && (AGGREGATE_TYPE_P (TREE_TYPE (rhsop))
	      || TREE_CODE (TREE_TYPE (lhsop)) == COMPLEX_TYPE))
	{
	  do_structure_copy (lhsop, rhsop);
	}
      else
	{
	  /* Only care about operations with pointers, structures
	     containing pointers, dereferences, and call expressions.  */
	  if (could_have_pointers (lhsop)
	      || TREE_CODE (rhsop) == CALL_EXPR)
	    {
	      get_constraint_for (lhsop, &lhsc);
	      switch (TREE_CODE_CLASS (TREE_CODE (rhsop)))
		{
		  /* RHS that consist of unary operations,
		     exceptional types, or bare decls/constants, get
		     handled directly by get_constraint_for.  */
		  case tcc_reference:
		  case tcc_declaration:
		  case tcc_constant:
		  case tcc_exceptional:
		  case tcc_expression:
		  case tcc_unary:
		      {
			unsigned int j;

			get_constraint_for (rhsop, &rhsc);
			for (j = 0; VEC_iterate (ce_s, lhsc, j, c); j++)
			  {
			    struct constraint_expr *c2;
			    unsigned int k;

			    for (k = 0; VEC_iterate (ce_s, rhsc, k, c2); k++)
			      process_constraint (new_constraint (*c, *c2));
			  }

		      }
		    break;

		  case tcc_binary:
		      {
			/* For pointer arithmetic of the form
			   PTR + CST, we can simply use PTR's
			   constraint because pointer arithmetic is
			   not allowed to go out of bounds.  */
			if (handle_ptr_arith (lhsc, rhsop))
			  break;
		      }
		    /* FALLTHRU  */

		  /* Otherwise, walk each operand.  Notice that we
		     can't use the operand interface because we need
		     to process expressions other than simple operands
		     (e.g. INDIRECT_REF, ADDR_EXPR, CALL_EXPR).  */
		  default:
		    for (i = 0; i < TREE_CODE_LENGTH (TREE_CODE (rhsop)); i++)
		      {
			tree op = TREE_OPERAND (rhsop, i);
			unsigned int j;

			gcc_assert (VEC_length (ce_s, rhsc) == 0);
			get_constraint_for (op, &rhsc);
			for (j = 0; VEC_iterate (ce_s, lhsc, j, c); j++)
			  {
			    struct constraint_expr *c2;
			    while (VEC_length (ce_s, rhsc) > 0)
			      {
				c2 = VEC_last (ce_s, rhsc);
				process_constraint (new_constraint (*c, *c2));
				VEC_pop (ce_s, rhsc);
			      }
			  }
		      }
		}
	    }
	}
    }

  /* After promoting variables and computing aliasing we will
     need to re-scan most statements.  FIXME: Try to minimize the
     number of statements re-scanned.  It's not really necessary to
     re-scan *all* statements.  */
  mark_stmt_modified (origt);
  VEC_free (ce_s, heap, rhsc);
  VEC_free (ce_s, heap, lhsc);
}


/* Find the first varinfo in the same variable as START that overlaps with
   OFFSET.
   Effectively, walk the chain of fields for the variable START to find the
   first field that overlaps with OFFSET.
   Return NULL if we can't find one.  */

static varinfo_t
first_vi_for_offset (varinfo_t start, unsigned HOST_WIDE_INT offset)
{
  varinfo_t curr = start;
  while (curr)
    {
      /* We may not find a variable in the field list with the actual
	 offset when when we have glommed a structure to a variable.
	 In that case, however, offset should still be within the size
	 of the variable. */
      if (offset >= curr->offset && offset < (curr->offset +  curr->size))
	return curr;
      curr = curr->next;
    }
  return NULL;
}


/* Insert the varinfo FIELD into the field list for BASE, at the front
   of the list.  */

static void
insert_into_field_list (varinfo_t base, varinfo_t field)
{
  varinfo_t prev = base;
  varinfo_t curr = base->next;

  field->next = curr;
  prev->next = field;
}

/* Insert the varinfo FIELD into the field list for BASE, ordered by
   offset.  */

static void
insert_into_field_list_sorted (varinfo_t base, varinfo_t field)
{
  varinfo_t prev = base;
  varinfo_t curr = base->next;

  if (curr == NULL)
    {
      prev->next = field;
      field->next = NULL;
    }
  else
    {
      while (curr)
	{
	  if (field->offset <= curr->offset)
	    break;
	  prev = curr;
	  curr = curr->next;
	}
      field->next = prev->next;
      prev->next = field;
    }
}

/* qsort comparison function for two fieldoff's PA and PB */

static int
fieldoff_compare (const void *pa, const void *pb)
{
  const fieldoff_s *foa = (const fieldoff_s *)pa;
  const fieldoff_s *fob = (const fieldoff_s *)pb;
  HOST_WIDE_INT foasize, fobsize;

  if (foa->offset != fob->offset)
    return foa->offset - fob->offset;

  foasize = TREE_INT_CST_LOW (foa->size);
  fobsize = TREE_INT_CST_LOW (fob->size);
  return foasize - fobsize;
}

/* Sort a fieldstack according to the field offset and sizes.  */
void
sort_fieldstack (VEC(fieldoff_s,heap) *fieldstack)
{
  qsort (VEC_address (fieldoff_s, fieldstack),
	 VEC_length (fieldoff_s, fieldstack),
	 sizeof (fieldoff_s),
	 fieldoff_compare);
}

/* Given a TYPE, and a vector of field offsets FIELDSTACK, push all the fields
   of TYPE onto fieldstack, recording their offsets along the way.
   OFFSET is used to keep track of the offset in this entire structure, rather
   than just the immediately containing structure.  Returns the number
   of fields pushed.
   HAS_UNION is set to true if we find a union type as a field of
   TYPE.  */

int
push_fields_onto_fieldstack (tree type, VEC(fieldoff_s,heap) **fieldstack,
			     HOST_WIDE_INT offset, bool *has_union)
{
  tree field;
  int count = 0;
  unsigned HOST_WIDE_INT minoffset = -1;

  if (TREE_CODE (type) == COMPLEX_TYPE)
    {
      fieldoff_s *real_part, *img_part;
      real_part = VEC_safe_push (fieldoff_s, heap, *fieldstack, NULL);
      real_part->type = TREE_TYPE (type);
      real_part->size = TYPE_SIZE (TREE_TYPE (type));
      real_part->offset = offset;
      real_part->decl = NULL_TREE;

      img_part = VEC_safe_push (fieldoff_s, heap, *fieldstack, NULL);
      img_part->type = TREE_TYPE (type);
      img_part->size = TYPE_SIZE (TREE_TYPE (type));
      img_part->offset = offset + TREE_INT_CST_LOW (TYPE_SIZE (TREE_TYPE (type)));
      img_part->decl = NULL_TREE;

      return 2;
    }

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      tree sz = TYPE_SIZE (type);
      tree elsz = TYPE_SIZE (TREE_TYPE (type));
      HOST_WIDE_INT nr;
      int i;

      if (! sz
	  || ! host_integerp (sz, 1)
	  || TREE_INT_CST_LOW (sz) == 0
	  || ! elsz
	  || ! host_integerp (elsz, 1)
	  || TREE_INT_CST_LOW (elsz) == 0)
	return 0;

      nr = TREE_INT_CST_LOW (sz) / TREE_INT_CST_LOW (elsz);
      if (nr > SALIAS_MAX_ARRAY_ELEMENTS)
	return 0;

      for (i = 0; i < nr; ++i)
	{
	  bool push = false;
	  int pushed = 0;

	  if (has_union
	      && (TREE_CODE (TREE_TYPE (type)) == QUAL_UNION_TYPE
		  || TREE_CODE (TREE_TYPE (type)) == UNION_TYPE))
	    *has_union = true;

	  if (!AGGREGATE_TYPE_P (TREE_TYPE (type))) /* var_can_have_subvars */
	    push = true;
	  else if (!(pushed = push_fields_onto_fieldstack
		     (TREE_TYPE (type), fieldstack,
		      offset + i * TREE_INT_CST_LOW (elsz), has_union)))
	    /* Empty structures may have actual size, like in C++. So
	       see if we didn't push any subfields and the size is
	       nonzero, push the field onto the stack */
	    push = true;

	  if (push)
	    {
	      fieldoff_s *pair;

	      pair = VEC_safe_push (fieldoff_s, heap, *fieldstack, NULL);
	      pair->type = TREE_TYPE (type);
	      pair->size = elsz;
	      pair->decl = NULL_TREE;
	      pair->offset = offset + i * TREE_INT_CST_LOW (elsz);
	      count++;
	    }
	  else
	    count += pushed;
	}

      return count;
    }

  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
    if (TREE_CODE (field) == FIELD_DECL)
      {
	bool push = false;
	int pushed = 0;

	if (has_union
	    && (TREE_CODE (TREE_TYPE (field)) == QUAL_UNION_TYPE
		|| TREE_CODE (TREE_TYPE (field)) == UNION_TYPE))
	  *has_union = true;

	if (!var_can_have_subvars (field))
	  push = true;
	else if (!(pushed = push_fields_onto_fieldstack
		   (TREE_TYPE (field), fieldstack,
		    offset + bitpos_of_field (field), has_union))
		 && DECL_SIZE (field)
		 && !integer_zerop (DECL_SIZE (field)))
	  /* Empty structures may have actual size, like in C++. So
	     see if we didn't push any subfields and the size is
	     nonzero, push the field onto the stack */
	  push = true;

	if (push)
	  {
	    fieldoff_s *pair;

	    pair = VEC_safe_push (fieldoff_s, heap, *fieldstack, NULL);
	    pair->type = TREE_TYPE (field);
	    pair->size = DECL_SIZE (field);
	    pair->decl = field;
	    pair->offset = offset + bitpos_of_field (field);
	    count++;
	  }
	else
	  count += pushed;

	if (bitpos_of_field (field) < minoffset)
	  minoffset = bitpos_of_field (field);
      }

  /* We need to create a fake subvar for empty bases.  But _only_ for non-empty
     classes.  */
  if (minoffset != 0 && count != 0)
    {
      fieldoff_s *pair;

      pair = VEC_safe_push (fieldoff_s, heap, *fieldstack, NULL);
      pair->type = void_type_node;
      pair->size = build_int_cst (size_type_node, minoffset);
      pair->decl = NULL;
      pair->offset = offset;
      count++;
    }

  return count;
}

/* Create a constraint from ESCAPED_VARS variable to VI.  */
static void
make_constraint_from_escaped (varinfo_t vi)
{
  struct constraint_expr lhs, rhs;
  
  lhs.var = vi->id;
  lhs.offset = 0;
  lhs.type = SCALAR;
  
  rhs.var = escaped_vars_id;
  rhs.offset = 0;
  rhs.type = SCALAR;
  process_constraint (new_constraint (lhs, rhs));
}

/* Create a constraint to the ESCAPED_VARS variable from constraint
   expression RHS. */

static void
make_constraint_to_escaped (struct constraint_expr rhs)
{
  struct constraint_expr lhs;
  
  lhs.var = escaped_vars_id;
  lhs.offset = 0;
  lhs.type = SCALAR;

  process_constraint (new_constraint (lhs, rhs));
}

/* Count the number of arguments DECL has, and set IS_VARARGS to true
   if it is a varargs function.  */

static unsigned int
count_num_arguments (tree decl, bool *is_varargs)
{
  unsigned int i = 0;
  tree t;

  for (t = TYPE_ARG_TYPES (TREE_TYPE (decl));
       t;
       t = TREE_CHAIN (t))
    {
      if (TREE_VALUE (t) == void_type_node)
	break;
      i++;
    }

  if (!t)
    *is_varargs = true;
  return i;
}

/* Creation function node for DECL, using NAME, and return the index
   of the variable we've created for the function.  */

static unsigned int
create_function_info_for (tree decl, const char *name)
{
  unsigned int index = VEC_length (varinfo_t, varmap);
  varinfo_t vi;
  tree arg;
  unsigned int i;
  bool is_varargs = false;

  /* Create the variable info.  */

  vi = new_var_info (decl, index, name);
  vi->decl = decl;
  vi->offset = 0;
  vi->has_union = 0;
  vi->size = 1;
  vi->fullsize = count_num_arguments (decl, &is_varargs) + 1;
  insert_vi_for_tree (vi->decl, vi);
  VEC_safe_push (varinfo_t, heap, varmap, vi);

  stats.total_vars++;

  /* If it's varargs, we don't know how many arguments it has, so we
     can't do much.
  */
  if (is_varargs)
    {
      vi->fullsize = ~0;
      vi->size = ~0;
      vi->is_unknown_size_var = true;
      return index;
    }


  arg = DECL_ARGUMENTS (decl);

  /* Set up variables for each argument.  */
  for (i = 1; i < vi->fullsize; i++)
    {
      varinfo_t argvi;
      const char *newname;
      char *tempname;
      unsigned int newindex;
      tree argdecl = decl;

      if (arg)
	argdecl = arg;

      newindex = VEC_length (varinfo_t, varmap);
      asprintf (&tempname, "%s.arg%d", name, i-1);
      newname = ggc_strdup (tempname);
      free (tempname);

      argvi = new_var_info (argdecl, newindex, newname);
      argvi->decl = argdecl;
      VEC_safe_push (varinfo_t, heap, varmap, argvi);
      argvi->offset = i;
      argvi->size = 1;
      argvi->fullsize = vi->fullsize;
      argvi->has_union = false;
      insert_into_field_list_sorted (vi, argvi);
      stats.total_vars ++;
      if (arg)
	{
	  insert_vi_for_tree (arg, argvi);
	  arg = TREE_CHAIN (arg);
	}
    }

  /* Create a variable for the return var.  */
  if (DECL_RESULT (decl) != NULL
      || !VOID_TYPE_P (TREE_TYPE (TREE_TYPE (decl))))
    {
      varinfo_t resultvi;
      const char *newname;
      char *tempname;
      unsigned int newindex;
      tree resultdecl = decl;

      vi->fullsize ++;

      if (DECL_RESULT (decl))
	resultdecl = DECL_RESULT (decl);

      newindex = VEC_length (varinfo_t, varmap);
      asprintf (&tempname, "%s.result", name);
      newname = ggc_strdup (tempname);
      free (tempname);

      resultvi = new_var_info (resultdecl, newindex, newname);
      resultvi->decl = resultdecl;
      VEC_safe_push (varinfo_t, heap, varmap, resultvi);
      resultvi->offset = i;
      resultvi->size = 1;
      resultvi->fullsize = vi->fullsize;
      resultvi->has_union = false;
      insert_into_field_list_sorted (vi, resultvi);
      stats.total_vars ++;
      if (DECL_RESULT (decl))
	insert_vi_for_tree (DECL_RESULT (decl), resultvi);
    }
  return index;
}


/* Return true if FIELDSTACK contains fields that overlap.
   FIELDSTACK is assumed to be sorted by offset.  */

static bool
check_for_overlaps (VEC (fieldoff_s,heap) *fieldstack)
{
  fieldoff_s *fo = NULL;
  unsigned int i;
  HOST_WIDE_INT lastoffset = -1;

  for (i = 0; VEC_iterate (fieldoff_s, fieldstack, i, fo); i++)
    {
      if (fo->offset == lastoffset)
	return true;
      lastoffset = fo->offset;
    }
  return false;
}

/* This function is called through walk_tree to walk global
   initializers looking for constraints we need to add to the
   constraint list.  */

static tree
find_global_initializers (tree *tp, int *walk_subtrees ATTRIBUTE_UNUSED,
			  void *viv)
{
  varinfo_t vi = (varinfo_t)viv;
  tree t = *tp;

  switch (TREE_CODE (t))
    {
      /* Dereferences and addressofs are the only important things
	 here, and i don't even remember if dereferences are legal
	 here in initializers.  */
    case INDIRECT_REF:
    case ADDR_EXPR:
      {
	struct constraint_expr *c;
	size_t i;
	
	VEC(ce_s, heap) *rhsc = NULL;
	get_constraint_for (t, &rhsc);
	for (i = 0; VEC_iterate (ce_s, rhsc, i, c); i++)
	  {
	    struct constraint_expr lhs;
	    
	    lhs.var = vi->id;
	    lhs.type = SCALAR;
	    lhs.offset = 0;
	    process_constraint (new_constraint (lhs, *c));
	  }

	VEC_free (ce_s, heap, rhsc);
      }
      break;
    case VAR_DECL:
      /* We might not have walked this because we skip
	 DECL_EXTERNALs during the initial scan.  */
      if (referenced_vars)
	{
	  get_var_ann (t);
	  if (referenced_var_check_and_insert (t))
	    mark_sym_for_renaming (t);
	}
      break;
    default:
      break;
    }
  return NULL_TREE;
}

/* Create a varinfo structure for NAME and DECL, and add it to VARMAP.
   This will also create any varinfo structures necessary for fields
   of DECL.  */

static unsigned int
create_variable_info_for (tree decl, const char *name)
{
  unsigned int index = VEC_length (varinfo_t, varmap);
  varinfo_t vi;
  tree decltype = TREE_TYPE (decl);
  tree declsize = DECL_P (decl) ? DECL_SIZE (decl) : TYPE_SIZE (decltype);
  bool notokay = false;
  bool hasunion;
  bool is_global = DECL_P (decl) ? is_global_var (decl) : false;
  VEC (fieldoff_s,heap) *fieldstack = NULL;

  if (TREE_CODE (decl) == FUNCTION_DECL && in_ipa_mode)
    return create_function_info_for (decl, name);

  hasunion = TREE_CODE (decltype) == UNION_TYPE
	     || TREE_CODE (decltype) == QUAL_UNION_TYPE;
  if (var_can_have_subvars (decl) && use_field_sensitive && !hasunion)
    {
      push_fields_onto_fieldstack (decltype, &fieldstack, 0, &hasunion);
      if (hasunion)
	{
	  VEC_free (fieldoff_s, heap, fieldstack);
	  notokay = true;
	}
    }


  /* If the variable doesn't have subvars, we may end up needing to
     sort the field list and create fake variables for all the
     fields.  */
  vi = new_var_info (decl, index, name);
  vi->decl = decl;
  vi->offset = 0;
  vi->has_union = hasunion;
  if (!declsize
      || TREE_CODE (declsize) != INTEGER_CST
      || TREE_CODE (decltype) == UNION_TYPE
      || TREE_CODE (decltype) == QUAL_UNION_TYPE)
    {
      vi->is_unknown_size_var = true;
      vi->fullsize = ~0;
      vi->size = ~0;
    }
  else
    {
      vi->fullsize = TREE_INT_CST_LOW (declsize);
      vi->size = vi->fullsize;
    }

  insert_vi_for_tree (vi->decl, vi);
  VEC_safe_push (varinfo_t, heap, varmap, vi);
  if (is_global && (!flag_whole_program || !in_ipa_mode))
    {
      make_constraint_from_escaped (vi);

      /* If the variable can't be aliased, there is no point in
	 putting it in the set of nonlocal vars.  */
      if (may_be_aliased (vi->decl))
	{
	  struct constraint_expr rhs;
	  rhs.var = index;
	  rhs.type = ADDRESSOF;
	  rhs.offset = 0;
	  make_constraint_to_escaped (rhs);
	} 

      if (TREE_CODE (decl) != FUNCTION_DECL && DECL_INITIAL (decl))
	{
	  walk_tree_without_duplicates (&DECL_INITIAL (decl),
					find_global_initializers,
					(void *)vi);
	}
    }

  stats.total_vars++;
  if (use_field_sensitive
      && !notokay
      && !vi->is_unknown_size_var
      && var_can_have_subvars (decl)
      && VEC_length (fieldoff_s, fieldstack) <= MAX_FIELDS_FOR_FIELD_SENSITIVE)
    {
      unsigned int newindex = VEC_length (varinfo_t, varmap);
      fieldoff_s *fo = NULL;
      unsigned int i;

      for (i = 0; !notokay && VEC_iterate (fieldoff_s, fieldstack, i, fo); i++)
	{
	  if (! fo->size
	      || TREE_CODE (fo->size) != INTEGER_CST
	      || fo->offset < 0)
	    {
	      notokay = true;
	      break;
	    }
	}

      /* We can't sort them if we have a field with a variable sized type,
	 which will make notokay = true.  In that case, we are going to return
	 without creating varinfos for the fields anyway, so sorting them is a
	 waste to boot.  */
      if (!notokay)
	{
	  sort_fieldstack (fieldstack);
	  /* Due to some C++ FE issues, like PR 22488, we might end up
	     what appear to be overlapping fields even though they,
	     in reality, do not overlap.  Until the C++ FE is fixed,
	     we will simply disable field-sensitivity for these cases.  */
	  notokay = check_for_overlaps (fieldstack);
	}


      if (VEC_length (fieldoff_s, fieldstack) != 0)
	fo = VEC_index (fieldoff_s, fieldstack, 0);

      if (fo == NULL || notokay)
	{
	  vi->is_unknown_size_var = 1;
	  vi->fullsize = ~0;
	  vi->size = ~0;
	  VEC_free (fieldoff_s, heap, fieldstack);
	  return index;
	}

      vi->size = TREE_INT_CST_LOW (fo->size);
      vi->offset = fo->offset;
      for (i = VEC_length (fieldoff_s, fieldstack) - 1;
	   i >= 1 && VEC_iterate (fieldoff_s, fieldstack, i, fo);
	   i--)
	{
	  varinfo_t newvi;
	  const char *newname = "NULL";
	  char *tempname;

	  newindex = VEC_length (varinfo_t, varmap);
	  if (dump_file)
	    {
	      if (fo->decl)
		asprintf (&tempname, "%s.%s",
			  vi->name, alias_get_name (fo->decl));
	      else
		asprintf (&tempname, "%s." HOST_WIDE_INT_PRINT_DEC,
			  vi->name, fo->offset);
	      newname = ggc_strdup (tempname);
	      free (tempname);
	    }
	  newvi = new_var_info (decl, newindex, newname);
	  newvi->offset = fo->offset;
	  newvi->size = TREE_INT_CST_LOW (fo->size);
	  newvi->fullsize = vi->fullsize;
	  insert_into_field_list (vi, newvi);
	  VEC_safe_push (varinfo_t, heap, varmap, newvi);
	  if (is_global && (!flag_whole_program || !in_ipa_mode))
	    {
	      /* If the variable can't be aliased, there is no point in
		 putting it in the set of nonlocal vars.  */
	      if (may_be_aliased (vi->decl))
		{
		  struct constraint_expr rhs;
	      
		  rhs.var = newindex;
		  rhs.type = ADDRESSOF;
		  rhs.offset = 0;
		  make_constraint_to_escaped (rhs);
		} 
	      make_constraint_from_escaped (newvi);
	    }
	  
	  stats.total_vars++;
	}
      VEC_free (fieldoff_s, heap, fieldstack);
    }
  return index;
}

/* Print out the points-to solution for VAR to FILE.  */

void
dump_solution_for_var (FILE *file, unsigned int var)
{
  varinfo_t vi = get_varinfo (var);
  unsigned int i;
  bitmap_iterator bi;

  if (find (var) != var)
    {
      varinfo_t vipt = get_varinfo (find (var));
      fprintf (file, "%s = same as %s\n", vi->name, vipt->name);
    }
  else
    {
      fprintf (file, "%s = { ", vi->name);
      EXECUTE_IF_SET_IN_BITMAP (vi->solution, 0, i, bi)
	{
	  fprintf (file, "%s ", get_varinfo (i)->name);
	}
      fprintf (file, "}\n");
    }
}

/* Print the points-to solution for VAR to stdout.  */

void
debug_solution_for_var (unsigned int var)
{
  dump_solution_for_var (stdout, var);
}

/* Create varinfo structures for all of the variables in the
   function for intraprocedural mode.  */

static void
intra_create_variable_infos (void)
{
  tree t;
  struct constraint_expr lhs, rhs;
  varinfo_t nonlocal_vi;

  /* For each incoming pointer argument arg, ARG = ESCAPED_VARS or a
     dummy variable if flag_argument_noalias > 2. */
  for (t = DECL_ARGUMENTS (current_function_decl); t; t = TREE_CHAIN (t))
    {
      varinfo_t p;
      unsigned int arg_id;
      
      if (!could_have_pointers (t))
	continue;
      
      arg_id = get_vi_for_tree (t)->id;

      /* With flag_argument_noalias greater than two means that the incoming
         argument cannot alias anything except for itself so create a HEAP
         variable.  */
      if (POINTER_TYPE_P (TREE_TYPE (t))
	  && flag_argument_noalias > 2)
	{
	  varinfo_t vi;
	  tree heapvar = heapvar_lookup (t);
	  
	  lhs.offset = 0;
	  lhs.type = SCALAR;
	  lhs.var  = get_vi_for_tree (t)->id;
	  
	  if (heapvar == NULL_TREE)
	    {
	      heapvar = create_tmp_var_raw (TREE_TYPE (TREE_TYPE (t)), 
					    "PARM_NOALIAS");
	      DECL_EXTERNAL (heapvar) = 1;
	      if (referenced_vars)
		add_referenced_var (heapvar);
	      heapvar_insert (t, heapvar);
	    }

	  vi = get_vi_for_tree (heapvar);
	  vi->is_artificial_var = 1;
	  vi->is_heap_var = 1;
	  rhs.var = vi->id;
	  rhs.type = ADDRESSOF;
	  rhs.offset = 0;
          for (p = get_varinfo (lhs.var); p; p = p->next)
	    {
	      struct constraint_expr temp = lhs;
	      temp.var = p->id;
	      process_constraint (new_constraint (temp, rhs));
	    }
	}
      else      
	{
	  for (p = get_varinfo (arg_id); p; p = p->next)
	    make_constraint_from_escaped (p);
	}
    }
  if (!nonlocal_all)
    nonlocal_all = create_nonlocal_var (void_type_node);

  /* Create variable info for the nonlocal var if it does not
     exist.  */
  nonlocal_vars_id = create_variable_info_for (nonlocal_all,
					       get_name (nonlocal_all));
  nonlocal_vi = get_varinfo (nonlocal_vars_id);
  nonlocal_vi->is_artificial_var = 1;
  nonlocal_vi->is_heap_var = 1; 
  nonlocal_vi->is_unknown_size_var = 1;
  nonlocal_vi->directly_dereferenced = true;

  rhs.var = nonlocal_vars_id;
  rhs.type = ADDRESSOF;
  rhs.offset = 0;
  
  lhs.var = escaped_vars_id;
  lhs.type = SCALAR;
  lhs.offset = 0;
  
  process_constraint (new_constraint (lhs, rhs));
}

/* Structure used to put solution bitmaps in a hashtable so they can
   be shared among variables with the same points-to set.  */

typedef struct shared_bitmap_info
{
  bitmap pt_vars;
  hashval_t hashcode;
} *shared_bitmap_info_t;

static htab_t shared_bitmap_table;

/* Hash function for a shared_bitmap_info_t */

static hashval_t
shared_bitmap_hash (const void *p)
{
  const shared_bitmap_info_t bi = (shared_bitmap_info_t) p;
  return bi->hashcode;
}

/* Equality function for two shared_bitmap_info_t's. */

static int
shared_bitmap_eq (const void *p1, const void *p2)
{
  const shared_bitmap_info_t sbi1 = (shared_bitmap_info_t) p1;
  const shared_bitmap_info_t sbi2 = (shared_bitmap_info_t) p2;
  return bitmap_equal_p (sbi1->pt_vars, sbi2->pt_vars);
}

/* Lookup a bitmap in the shared bitmap hashtable, and return an already
   existing instance if there is one, NULL otherwise.  */

static bitmap
shared_bitmap_lookup (bitmap pt_vars)
{
  void **slot;
  struct shared_bitmap_info sbi;

  sbi.pt_vars = pt_vars;
  sbi.hashcode = bitmap_hash (pt_vars);
  
  slot = htab_find_slot_with_hash (shared_bitmap_table, &sbi,
				   sbi.hashcode, NO_INSERT);
  if (!slot)
    return NULL;
  else
    return ((shared_bitmap_info_t) *slot)->pt_vars;
}


/* Add a bitmap to the shared bitmap hashtable.  */

static void
shared_bitmap_add (bitmap pt_vars)
{
  void **slot;
  shared_bitmap_info_t sbi = XNEW (struct shared_bitmap_info);
  
  sbi->pt_vars = pt_vars;
  sbi->hashcode = bitmap_hash (pt_vars);
  
  slot = htab_find_slot_with_hash (shared_bitmap_table, sbi,
				   sbi->hashcode, INSERT);
  gcc_assert (!*slot);
  *slot = (void *) sbi;
}


/* Set bits in INTO corresponding to the variable uids in solution set
   FROM, which came from variable PTR.
   For variables that are actually dereferenced, we also use type
   based alias analysis to prune the points-to sets.  */

static void
set_uids_in_ptset (tree ptr, bitmap into, bitmap from)
{
  unsigned int i;
  bitmap_iterator bi;
  subvar_t sv;
  unsigned HOST_WIDE_INT ptr_alias_set = get_alias_set (TREE_TYPE (ptr));

  EXECUTE_IF_SET_IN_BITMAP (from, 0, i, bi)
    {
      varinfo_t vi = get_varinfo (i);
      unsigned HOST_WIDE_INT var_alias_set;
      
      /* The only artificial variables that are allowed in a may-alias
	 set are heap variables.  */
      if (vi->is_artificial_var && !vi->is_heap_var)
	continue;
      
      if (vi->has_union && get_subvars_for_var (vi->decl) != NULL)
	{
	  /* Variables containing unions may need to be converted to
	     their SFT's, because SFT's can have unions and we cannot.  */
	  for (sv = get_subvars_for_var (vi->decl); sv; sv = sv->next)
	    bitmap_set_bit (into, DECL_UID (sv->var));
	}
      else if (TREE_CODE (vi->decl) == VAR_DECL 
	       || TREE_CODE (vi->decl) == PARM_DECL
	       || TREE_CODE (vi->decl) == RESULT_DECL)
	{
	  if (var_can_have_subvars (vi->decl)
	      && get_subvars_for_var (vi->decl))
	    {
	      /* If VI->DECL is an aggregate for which we created
		 SFTs, add the SFT corresponding to VI->OFFSET.  */
	      tree sft = get_subvar_at (vi->decl, vi->offset);
	      if (sft)
		{
		  var_alias_set = get_alias_set (sft);
		  if (!vi->directly_dereferenced
		      || alias_sets_conflict_p (ptr_alias_set, var_alias_set))
		    bitmap_set_bit (into, DECL_UID (sft));
		}
	    }
	  else
	    {
	      /* Otherwise, just add VI->DECL to the alias set.
		 Don't type prune artificial vars.  */
	      if (vi->is_artificial_var)
		bitmap_set_bit (into, DECL_UID (vi->decl));
	      else
		{
		  var_alias_set = get_alias_set (vi->decl);
		  if (!vi->directly_dereferenced
		      || alias_sets_conflict_p (ptr_alias_set, var_alias_set))
		    bitmap_set_bit (into, DECL_UID (vi->decl));
		}
	    }
	}
    }
}


static bool have_alias_info = false;

/* Given a pointer variable P, fill in its points-to set, or return
   false if we can't.  */

bool
find_what_p_points_to (tree p)
{
  tree lookup_p = p;
  varinfo_t vi;

  if (!have_alias_info)
    return false;

  /* For parameters, get at the points-to set for the actual parm
     decl.  */
  if (TREE_CODE (p) == SSA_NAME 
      && TREE_CODE (SSA_NAME_VAR (p)) == PARM_DECL 
      && default_def (SSA_NAME_VAR (p)) == p)
    lookup_p = SSA_NAME_VAR (p);

  vi = lookup_vi_for_tree (lookup_p);
  if (vi)
    {
      
      if (vi->is_artificial_var)
	return false;

      /* See if this is a field or a structure.  */
      if (vi->size != vi->fullsize)
	{
	  /* Nothing currently asks about structure fields directly,
	     but when they do, we need code here to hand back the
	     points-to set.  */
	  if (!var_can_have_subvars (vi->decl)
	      || get_subvars_for_var (vi->decl) == NULL)
	    return false;
	} 
      else
	{
	  struct ptr_info_def *pi = get_ptr_info (p);
	  unsigned int i;
	  bitmap_iterator bi;
	  bitmap finished_solution;
	  bitmap result;
	  
	  /* This variable may have been collapsed, let's get the real
	     variable.  */
	  vi = get_varinfo (find (vi->id));
	  
	  /* Translate artificial variables into SSA_NAME_PTR_INFO
	     attributes.  */
	  EXECUTE_IF_SET_IN_BITMAP (vi->solution, 0, i, bi)
	    {
	      varinfo_t vi = get_varinfo (i);

	      if (vi->is_artificial_var)
		{
		  /* FIXME.  READONLY should be handled better so that
		     flow insensitive aliasing can disregard writable
		     aliases.  */
		  if (vi->id == nothing_id)
		    pi->pt_null = 1;
		  else if (vi->id == anything_id)
		    pi->pt_anything = 1;
		  else if (vi->id == readonly_id)
		    pi->pt_anything = 1;
		  else if (vi->id == integer_id)
		    pi->pt_anything = 1;
		  else if (vi->is_heap_var)
		    pi->pt_global_mem = 1;
		}
	    }

	  if (pi->pt_anything)
	    return false;

	  finished_solution = BITMAP_GGC_ALLOC ();
	  set_uids_in_ptset (vi->decl, finished_solution, vi->solution);
	  result = shared_bitmap_lookup (finished_solution);

	  if (!result)
	    {
	      shared_bitmap_add (finished_solution);
	      pi->pt_vars = finished_solution;
	    }
	  else
	    {
	      pi->pt_vars = result;
	      bitmap_clear (finished_solution);
	    }

	  if (bitmap_empty_p (pi->pt_vars))
	    pi->pt_vars = NULL;

	  return true;
	}
    }

  return false;
}



/* Dump points-to information to OUTFILE.  */

void
dump_sa_points_to_info (FILE *outfile)
{
  unsigned int i;

  fprintf (outfile, "\nPoints-to sets\n\n");

  if (dump_flags & TDF_STATS)
    {
      fprintf (outfile, "Stats:\n");
      fprintf (outfile, "Total vars:               %d\n", stats.total_vars);
      fprintf (outfile, "Non-pointer vars:          %d\n",
	       stats.nonpointer_vars);
      fprintf (outfile, "Statically unified vars:  %d\n",
	       stats.unified_vars_static);
      fprintf (outfile, "Dynamically unified vars: %d\n",
	       stats.unified_vars_dynamic);
      fprintf (outfile, "Iterations:               %d\n", stats.iterations);
      fprintf (outfile, "Number of edges:          %d\n", stats.num_edges);
      fprintf (outfile, "Number of implicit edges: %d\n",
	       stats.num_implicit_edges);
    }

  for (i = 0; i < VEC_length (varinfo_t, varmap); i++)
    dump_solution_for_var (outfile, i);
}


/* Debug points-to information to stderr.  */

void
debug_sa_points_to_info (void)
{
  dump_sa_points_to_info (stderr);
}


/* Initialize the always-existing constraint variables for NULL
   ANYTHING, READONLY, and INTEGER */

static void
init_base_vars (void)
{
  struct constraint_expr lhs, rhs;

  /* Create the NULL variable, used to represent that a variable points
     to NULL.  */
  nothing_tree = create_tmp_var_raw (void_type_node, "NULL");
  var_nothing = new_var_info (nothing_tree, 0, "NULL");
  insert_vi_for_tree (nothing_tree, var_nothing);
  var_nothing->is_artificial_var = 1;
  var_nothing->offset = 0;
  var_nothing->size = ~0;
  var_nothing->fullsize = ~0;
  var_nothing->is_special_var = 1;
  nothing_id = 0;
  VEC_safe_push (varinfo_t, heap, varmap, var_nothing);

  /* Create the ANYTHING variable, used to represent that a variable
     points to some unknown piece of memory.  */
  anything_tree = create_tmp_var_raw (void_type_node, "ANYTHING");
  var_anything = new_var_info (anything_tree, 1, "ANYTHING"); 
  insert_vi_for_tree (anything_tree, var_anything);
  var_anything->is_artificial_var = 1;
  var_anything->size = ~0;
  var_anything->offset = 0;
  var_anything->next = NULL;
  var_anything->fullsize = ~0;
  var_anything->is_special_var = 1;
  anything_id = 1;

  /* Anything points to anything.  This makes deref constraints just
     work in the presence of linked list and other p = *p type loops, 
     by saying that *ANYTHING = ANYTHING. */
  VEC_safe_push (varinfo_t, heap, varmap, var_anything);
  lhs.type = SCALAR;
  lhs.var = anything_id;
  lhs.offset = 0;
  rhs.type = ADDRESSOF;
  rhs.var = anything_id;
  rhs.offset = 0;

  /* This specifically does not use process_constraint because
     process_constraint ignores all anything = anything constraints, since all
     but this one are redundant.  */
  VEC_safe_push (constraint_t, heap, constraints, new_constraint (lhs, rhs));
  
  /* Create the READONLY variable, used to represent that a variable
     points to readonly memory.  */
  readonly_tree = create_tmp_var_raw (void_type_node, "READONLY");
  var_readonly = new_var_info (readonly_tree, 2, "READONLY");
  var_readonly->is_artificial_var = 1;
  var_readonly->offset = 0;
  var_readonly->size = ~0;
  var_readonly->fullsize = ~0;
  var_readonly->next = NULL;
  var_readonly->is_special_var = 1;
  insert_vi_for_tree (readonly_tree, var_readonly);
  readonly_id = 2;
  VEC_safe_push (varinfo_t, heap, varmap, var_readonly);

  /* readonly memory points to anything, in order to make deref
     easier.  In reality, it points to anything the particular
     readonly variable can point to, but we don't track this
     separately. */
  lhs.type = SCALAR;
  lhs.var = readonly_id;
  lhs.offset = 0;
  rhs.type = ADDRESSOF;
  rhs.var = anything_id;
  rhs.offset = 0;
  
  process_constraint (new_constraint (lhs, rhs));
  
  /* Create the INTEGER variable, used to represent that a variable points
     to an INTEGER.  */
  integer_tree = create_tmp_var_raw (void_type_node, "INTEGER");
  var_integer = new_var_info (integer_tree, 3, "INTEGER");
  insert_vi_for_tree (integer_tree, var_integer);
  var_integer->is_artificial_var = 1;
  var_integer->size = ~0;
  var_integer->fullsize = ~0;
  var_integer->offset = 0;
  var_integer->next = NULL;
  var_integer->is_special_var = 1;
  integer_id = 3;
  VEC_safe_push (varinfo_t, heap, varmap, var_integer);

  /* INTEGER = ANYTHING, because we don't know where a dereference of
     a random integer will point to.  */
  lhs.type = SCALAR;
  lhs.var = integer_id;
  lhs.offset = 0;
  rhs.type = ADDRESSOF;
  rhs.var = anything_id;
  rhs.offset = 0;
  process_constraint (new_constraint (lhs, rhs));
  
  /* Create the ESCAPED_VARS variable used to represent variables that
     escape this function.  */
  escaped_vars_tree = create_tmp_var_raw (void_type_node, "ESCAPED_VARS");
  var_escaped_vars = new_var_info (escaped_vars_tree, 4, "ESCAPED_VARS");
  insert_vi_for_tree (escaped_vars_tree, var_escaped_vars);
  var_escaped_vars->is_artificial_var = 1;
  var_escaped_vars->size = ~0;
  var_escaped_vars->fullsize = ~0;
  var_escaped_vars->offset = 0;
  var_escaped_vars->next = NULL;
  escaped_vars_id = 4;
  VEC_safe_push (varinfo_t, heap, varmap, var_escaped_vars);

  /* ESCAPED_VARS = *ESCAPED_VARS */
  lhs.type = SCALAR;
  lhs.var = escaped_vars_id;
  lhs.offset = 0;
  rhs.type = DEREF;
  rhs.var = escaped_vars_id;
  rhs.offset = 0;
  process_constraint (new_constraint (lhs, rhs));
  
}  

/* Initialize things necessary to perform PTA */

static void
init_alias_vars (void)
{
  bitmap_obstack_initialize (&pta_obstack);
  bitmap_obstack_initialize (&oldpta_obstack);
  bitmap_obstack_initialize (&predbitmap_obstack);

  constraint_pool = create_alloc_pool ("Constraint pool",
				       sizeof (struct constraint), 30);
  variable_info_pool = create_alloc_pool ("Variable info pool",
					  sizeof (struct variable_info), 30);
  constraints = VEC_alloc (constraint_t, heap, 8);
  varmap = VEC_alloc (varinfo_t, heap, 8);
  vi_for_tree = pointer_map_create ();

  memset (&stats, 0, sizeof (stats));
  shared_bitmap_table = htab_create (511, shared_bitmap_hash,
				     shared_bitmap_eq, free);
  init_base_vars ();
}

/* Given a statement STMT, generate necessary constraints to
   escaped_vars for the escaping variables.  */

static void
find_escape_constraints (tree stmt)
{
  enum escape_type stmt_escape_type = is_escape_site (stmt);
  tree rhs;
  VEC(ce_s, heap) *rhsc = NULL;
  struct constraint_expr *c;
  size_t i;

  if (stmt_escape_type == NO_ESCAPE)
    return;

  if (TREE_CODE (stmt) == RETURN_EXPR)
    {
      /* Returns are either bare, with an embedded MODIFY_EXPR, or
	 just a plain old expression.  */
      if (!TREE_OPERAND (stmt, 0))
	return;
      if (TREE_CODE (TREE_OPERAND (stmt, 0)) == MODIFY_EXPR)
	rhs = TREE_OPERAND (TREE_OPERAND (stmt, 0), 1);
      else
	rhs = TREE_OPERAND (stmt, 0);

      get_constraint_for (rhs, &rhsc);
      for (i = 0; VEC_iterate (ce_s, rhsc, i, c); i++)
	make_constraint_to_escaped (*c);
      VEC_free (ce_s, heap, rhsc);
      return;
    }
  else if (TREE_CODE (stmt) == ASM_EXPR)
    {
      /* Whatever the inputs of the ASM are, escape.  */
      tree arg;

      for (arg = ASM_INPUTS (stmt); arg; arg = TREE_CHAIN (arg))
	{
	  rhsc = NULL;
	  get_constraint_for (TREE_VALUE (arg), &rhsc);
	  for (i = 0; VEC_iterate (ce_s, rhsc, i, c); i++)
	    make_constraint_to_escaped (*c);
	  VEC_free (ce_s, heap, rhsc);
	}
      return;
    }
  else if (TREE_CODE (stmt) == CALL_EXPR
	   || (TREE_CODE (stmt) == MODIFY_EXPR
	       && TREE_CODE (TREE_OPERAND (stmt, 1)) == CALL_EXPR))
    {
      /* Calls cause all of the arguments passed in to escape.  */
      tree arg;

      if (TREE_CODE (stmt) == MODIFY_EXPR)
	stmt = TREE_OPERAND (stmt, 1);
      for (arg = TREE_OPERAND (stmt, 1); arg; arg = TREE_CHAIN (arg))
	{
	  if (POINTER_TYPE_P (TREE_TYPE (TREE_VALUE (arg))))
	    {
	      rhsc = NULL;
	      get_constraint_for (TREE_VALUE (arg), &rhsc);
	      for (i = 0; VEC_iterate (ce_s, rhsc, i, c); i++)
		make_constraint_to_escaped (*c);
	      VEC_free (ce_s, heap, rhsc);
	    }
	}
      return;
    }
  else
    {
      gcc_assert (TREE_CODE (stmt) == MODIFY_EXPR);
    }

  gcc_assert (stmt_escape_type == ESCAPE_BAD_CAST
	      || stmt_escape_type == ESCAPE_STORED_IN_GLOBAL
	      || stmt_escape_type == ESCAPE_UNKNOWN);
  rhs = TREE_OPERAND (stmt, 1);
  
  /* Look through casts for the real escaping variable.
     Constants don't really escape, so ignore them.
     Otherwise, whatever escapes must be on our RHS.  */
  if (TREE_CODE (rhs) == NOP_EXPR
      || TREE_CODE (rhs) == CONVERT_EXPR
      || TREE_CODE (rhs) == NON_LVALUE_EXPR)
    {
      get_constraint_for (TREE_OPERAND (rhs, 0), &rhsc);
    }
  else if (CONSTANT_CLASS_P (rhs))
    return;
  else
    {
      get_constraint_for (rhs, &rhsc);
    }
  for (i = 0; VEC_iterate (ce_s, rhsc, i, c); i++)
    make_constraint_to_escaped (*c);
  VEC_free (ce_s, heap, rhsc);
}


/* Remove the REF and ADDRESS edges from GRAPH, as well as all the
   predecessor edges.  */

static void
remove_preds_and_fake_succs (constraint_graph_t graph)
{
  unsigned int i;

  /* Clear the implicit ref and address nodes from the successor
     lists.  */
  for (i = 0; i < FIRST_REF_NODE; i++)
    {
      if (graph->succs[i])
	bitmap_clear_range (graph->succs[i], FIRST_REF_NODE,
			    FIRST_REF_NODE * 2);
    }

  /* Free the successor list for the non-ref nodes.  */
  for (i = FIRST_REF_NODE; i < graph->size; i++)
    {
      if (graph->succs[i])
	BITMAP_FREE (graph->succs[i]);
    }

  /* Now reallocate the size of the successor list as, and blow away
     the predecessor bitmaps.  */
  graph->size = VEC_length (varinfo_t, varmap);
  graph->succs = xrealloc (graph->succs, graph->size * sizeof (bitmap));

  free (graph->implicit_preds);
  graph->implicit_preds = NULL;
  free (graph->preds);
  graph->preds = NULL;
  bitmap_obstack_release (&predbitmap_obstack);
}

/* Create points-to sets for the current function.  See the comments
   at the start of the file for an algorithmic overview.  */

void
compute_points_to_sets (struct alias_info *ai)
{
  basic_block bb;
  struct scc_info *si;

  timevar_push (TV_TREE_PTA);

  init_alias_vars ();
  init_alias_heapvars ();
  
  intra_create_variable_infos ();

  /* Now walk all statements and derive aliases.  */
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi; 
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	{
	  if (is_gimple_reg (PHI_RESULT (phi)))
	    {
	      find_func_aliases (phi);
	      /* Update various related attributes like escaped
		 addresses, pointer dereferences for loads and stores.
		 This is used when creating name tags and alias
		 sets.  */
	      update_alias_info (phi, ai);
	    }
	}

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);

	  find_func_aliases (stmt);
	  find_escape_constraints (stmt);
	  /* Update various related attributes like escaped
	     addresses, pointer dereferences for loads and stores.
	     This is used when creating name tags and alias
	     sets.  */
	  update_alias_info (stmt, ai);
	}
    }


  if (dump_file)
    {
      fprintf (dump_file, "Points-to analysis\n\nConstraints:\n\n");
      dump_constraints (dump_file);
    }

  if (dump_file)
    fprintf (dump_file,
	     "\nCollapsing static cycles and doing variable "
	     "substitution:\n");

  build_pred_graph ();
  si = perform_var_substitution (graph);
  move_complex_constraints (graph, si);
  free_var_substitution_info (si);
  
  build_succ_graph ();
  find_indirect_cycles (graph);

  /* Implicit nodes and predecessors are no longer necessary at this
     point. */
  remove_preds_and_fake_succs (graph);

  if (dump_file)
    fprintf (dump_file, "\nSolving graph:\n");

  solve_graph (graph);

  if (dump_file)
    dump_sa_points_to_info (dump_file);
  have_alias_info = true;

  timevar_pop (TV_TREE_PTA);
}

/* Delete created points-to sets.  */

void
delete_points_to_sets (void)
{
  varinfo_t v;
  int i;

  htab_delete (shared_bitmap_table);
  if (dump_file && (dump_flags & TDF_STATS))
    fprintf (dump_file, "Points to sets created:%d\n",
	     stats.points_to_sets_created);

  pointer_map_destroy (vi_for_tree);
  bitmap_obstack_release (&pta_obstack);
  VEC_free (constraint_t, heap, constraints);

  for (i = 0; VEC_iterate (varinfo_t, varmap, i, v); i++)
    VEC_free (constraint_t, heap, graph->complex[i]);
  free (graph->complex);

  free (graph->rep);
  free (graph->succs);
  free (graph->indirect_cycles);
  free (graph);

  VEC_free (varinfo_t, heap, varmap);
  free_alloc_pool (variable_info_pool);
  free_alloc_pool (constraint_pool);
  have_alias_info = false;
}

/* Return true if we should execute IPA PTA.  */
static bool
gate_ipa_pta (void)
{
  return (flag_unit_at_a_time != 0
          && flag_ipa_pta
	  /* Don't bother doing anything if the program has errors.  */
	  && !(errorcount || sorrycount));
}

/* Execute the driver for IPA PTA.  */
static unsigned int
ipa_pta_execute (void)
{
#if 0
  struct cgraph_node *node;
  in_ipa_mode = 1;
  init_alias_heapvars ();
  init_alias_vars ();
   
  for (node = cgraph_nodes; node; node = node->next)
    {
      if (!node->analyzed || cgraph_is_master_clone (node))
	{
	  unsigned int varid;
	  
	  varid = create_function_info_for (node->decl, 
					    cgraph_node_name (node));
	  if (node->local.externally_visible)
	    {
	      varinfo_t fi = get_varinfo (varid);
	      for (; fi; fi = fi->next)
		make_constraint_from_escaped (fi);
	    }
	}
    }
  for (node = cgraph_nodes; node; node = node->next)
    {
      if (node->analyzed && cgraph_is_master_clone (node))
	{
	  struct function *cfun = DECL_STRUCT_FUNCTION (node->decl);
	  basic_block bb;
	  tree old_func_decl = current_function_decl;
	  if (dump_file)
	    fprintf (dump_file, 
		     "Generating constraints for %s\n", 
		     cgraph_node_name (node)); 
	  push_cfun (cfun);
	  current_function_decl = node->decl;

	  FOR_EACH_BB_FN (bb, cfun)
	    {
	      block_stmt_iterator bsi; 
	      tree phi;
	      
	      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
		{
		  if (is_gimple_reg (PHI_RESULT (phi)))
		    {
		      find_func_aliases (phi);
		    }
		}
	      
	      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
		{
		  tree stmt = bsi_stmt (bsi);
		  find_func_aliases (stmt);
		}
	    }	
	  current_function_decl = old_func_decl;
	  pop_cfun ();	  
	}
      else
	{
	  /* Make point to anything.  */
	}
    }

  build_constraint_graph ();

  if (dump_file)
    {
      fprintf (dump_file, "Points-to analysis\n\nConstraints:\n\n");
      dump_constraints (dump_file);
    }
  
  if (dump_file)
    fprintf (dump_file, 
	     "\nCollapsing static cycles and doing variable "
	     "substitution:\n");
      
  find_and_collapse_graph_cycles (graph, false);
  perform_var_substitution (graph);
      
  if (dump_file)
    fprintf (dump_file, "\nSolving graph:\n");
      
  solve_graph (graph);
  
  if (dump_file)
    dump_sa_points_to_info (dump_file);
  in_ipa_mode = 0;
  delete_alias_heapvars ();
  delete_points_to_sets ();
#endif
  return 0;
}
  
struct tree_opt_pass pass_ipa_pta =
{
  "pta",		                /* name */
  gate_ipa_pta,			/* gate */
  ipa_pta_execute,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_IPA_PTA,		        /* tv_id */
  0,	                                /* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0					/* letter */
};

/* Initialize the heapvar for statement mapping.  */
void
init_alias_heapvars (void)
{
  if (!heapvar_for_stmt)
    heapvar_for_stmt = htab_create_ggc (11, tree_map_hash, tree_map_eq,
					NULL);
  nonlocal_all = NULL_TREE;
}

void
delete_alias_heapvars (void)
{
  nonlocal_all = NULL_TREE;
  htab_delete (heapvar_for_stmt);
  heapvar_for_stmt = NULL;
}
  
#include "gt-tree-ssa-structalias.h"
