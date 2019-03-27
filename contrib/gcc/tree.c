/* Language-independent node constructors for parse phase of GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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

/* This file contains the low level primitives for operating on tree nodes,
   including allocation, list operations, interning of identifiers,
   construction of data type nodes and statement nodes,
   and construction of type conversion nodes.  It also contains
   tables index by tree code that describe how to take apart
   nodes of that code.

   It is intended to be language-independent, but occasionally
   calls language-dependent routines defined (for C) in typecheck.c.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "flags.h"
#include "tree.h"
#include "real.h"
#include "tm_p.h"
#include "function.h"
#include "obstack.h"
#include "toplev.h"
#include "ggc.h"
#include "hashtab.h"
#include "output.h"
#include "target.h"
#include "langhooks.h"
#include "tree-iterator.h"
#include "basic-block.h"
#include "tree-flow.h"
#include "params.h"
#include "pointer-set.h"

/* Each tree code class has an associated string representation.
   These must correspond to the tree_code_class entries.  */

const char *const tree_code_class_strings[] =
{
  "exceptional",
  "constant",
  "type",
  "declaration",
  "reference",
  "comparison",
  "unary",
  "binary",
  "statement",
  "expression",
};

/* APPLE LOCAL begin 6353006  */
tree generic_block_literal_struct_type;
/* APPLE LOCAL end 6353006  */

/* obstack.[ch] explicitly declined to prototype this.  */
extern int _obstack_allocated_p (struct obstack *h, void *obj);

#ifdef GATHER_STATISTICS
/* Statistics-gathering stuff.  */

int tree_node_counts[(int) all_kinds];
int tree_node_sizes[(int) all_kinds];

/* Keep in sync with tree.h:enum tree_node_kind.  */
static const char * const tree_node_kind_names[] = {
  "decls",
  "types",
  "blocks",
  "stmts",
  "refs",
  "exprs",
  "constants",
  "identifiers",
  "perm_tree_lists",
  "temp_tree_lists",
  "vecs",
  "binfos",
  "phi_nodes",
  "ssa names",
  "constructors",
  "random kinds",
  "lang_decl kinds",
  "lang_type kinds",
  "omp clauses"
};
#endif /* GATHER_STATISTICS */

/* Unique id for next decl created.  */
static GTY(()) int next_decl_uid;
/* Unique id for next type created.  */
static GTY(()) int next_type_uid = 1;

/* Since we cannot rehash a type after it is in the table, we have to
   keep the hash code.  */

struct type_hash GTY(())
{
  unsigned long hash;
  tree type;
};

/* Initial size of the hash table (rounded to next prime).  */
#define TYPE_HASH_INITIAL_SIZE 1000

/* Now here is the hash table.  When recording a type, it is added to
   the slot whose index is the hash code.  Note that the hash table is
   used for several kinds of types (function types, array types and
   array index range types, for now).  While all these live in the
   same table, they are completely independent, and the hash code is
   computed differently for each of these.  */

static GTY ((if_marked ("type_hash_marked_p"), param_is (struct type_hash)))
     htab_t type_hash_table;

/* Hash table and temporary node for larger integer const values.  */
static GTY (()) tree int_cst_node;
static GTY ((if_marked ("ggc_marked_p"), param_is (union tree_node)))
     htab_t int_cst_hash_table;

/* General tree->tree mapping  structure for use in hash tables.  */


static GTY ((if_marked ("tree_map_marked_p"), param_is (struct tree_map))) 
     htab_t debug_expr_for_decl;

static GTY ((if_marked ("tree_map_marked_p"), param_is (struct tree_map))) 
     htab_t value_expr_for_decl;

static GTY ((if_marked ("tree_int_map_marked_p"), param_is (struct tree_int_map)))
  htab_t init_priority_for_decl;

static GTY ((if_marked ("tree_map_marked_p"), param_is (struct tree_map)))
  htab_t restrict_base_for_decl;

struct tree_int_map GTY(())
{
  tree from;
  unsigned short to;
};
static unsigned int tree_int_map_hash (const void *);
static int tree_int_map_eq (const void *, const void *);
static int tree_int_map_marked_p (const void *);
static void set_type_quals (tree, int);
static int type_hash_eq (const void *, const void *);
static hashval_t type_hash_hash (const void *);
static hashval_t int_cst_hash_hash (const void *);
static int int_cst_hash_eq (const void *, const void *);
static void print_type_hash_statistics (void);
static void print_debug_expr_statistics (void);
static void print_value_expr_statistics (void);
static int type_hash_marked_p (const void *);
static unsigned int type_hash_list (tree, hashval_t);
static unsigned int attribute_hash_list (tree, hashval_t);

tree global_trees[TI_MAX];
tree integer_types[itk_none];

unsigned char tree_contains_struct[256][64];

/* Number of operands for each OpenMP clause.  */
unsigned const char omp_clause_num_ops[] =
{
  0, /* OMP_CLAUSE_ERROR  */
  1, /* OMP_CLAUSE_PRIVATE  */
  1, /* OMP_CLAUSE_SHARED  */
  1, /* OMP_CLAUSE_FIRSTPRIVATE  */
  1, /* OMP_CLAUSE_LASTPRIVATE  */
  4, /* OMP_CLAUSE_REDUCTION  */
  1, /* OMP_CLAUSE_COPYIN  */
  1, /* OMP_CLAUSE_COPYPRIVATE  */
  1, /* OMP_CLAUSE_IF  */
  1, /* OMP_CLAUSE_NUM_THREADS  */
  1, /* OMP_CLAUSE_SCHEDULE  */
  0, /* OMP_CLAUSE_NOWAIT  */
  0, /* OMP_CLAUSE_ORDERED  */
  0  /* OMP_CLAUSE_DEFAULT  */
};

const char * const omp_clause_code_name[] =
{
  "error_clause",
  "private",
  "shared",
  "firstprivate",
  "lastprivate",
  "reduction",
  "copyin",
  "copyprivate",
  "if",
  "num_threads",
  "schedule",
  "nowait",
  "ordered",
  "default"
};

/* Init tree.c.  */

void
init_ttree (void)
{
  /* Initialize the hash table of types.  */
  type_hash_table = htab_create_ggc (TYPE_HASH_INITIAL_SIZE, type_hash_hash,
				     type_hash_eq, 0);

  debug_expr_for_decl = htab_create_ggc (512, tree_map_hash,
					 tree_map_eq, 0);

  value_expr_for_decl = htab_create_ggc (512, tree_map_hash,
					 tree_map_eq, 0);
  init_priority_for_decl = htab_create_ggc (512, tree_int_map_hash,
					    tree_int_map_eq, 0);
  restrict_base_for_decl = htab_create_ggc (256, tree_map_hash,
					    tree_map_eq, 0);

  int_cst_hash_table = htab_create_ggc (1024, int_cst_hash_hash,
					int_cst_hash_eq, NULL);
  
  int_cst_node = make_node (INTEGER_CST);

  tree_contains_struct[FUNCTION_DECL][TS_DECL_NON_COMMON] = 1;
  tree_contains_struct[TRANSLATION_UNIT_DECL][TS_DECL_NON_COMMON] = 1;
  tree_contains_struct[TYPE_DECL][TS_DECL_NON_COMMON] = 1;
  

  tree_contains_struct[CONST_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[VAR_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[PARM_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[RESULT_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[FUNCTION_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[TYPE_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[TRANSLATION_UNIT_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[LABEL_DECL][TS_DECL_COMMON] = 1;
  tree_contains_struct[FIELD_DECL][TS_DECL_COMMON] = 1;


  tree_contains_struct[CONST_DECL][TS_DECL_WRTL] = 1;
  tree_contains_struct[VAR_DECL][TS_DECL_WRTL] = 1;
  tree_contains_struct[PARM_DECL][TS_DECL_WRTL] = 1;
  tree_contains_struct[RESULT_DECL][TS_DECL_WRTL] = 1;
  tree_contains_struct[FUNCTION_DECL][TS_DECL_WRTL] = 1;
  tree_contains_struct[LABEL_DECL][TS_DECL_WRTL] = 1; 

  tree_contains_struct[CONST_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[VAR_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[PARM_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[RESULT_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[FUNCTION_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[TYPE_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[TRANSLATION_UNIT_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[LABEL_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[FIELD_DECL][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[STRUCT_FIELD_TAG][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[NAME_MEMORY_TAG][TS_DECL_MINIMAL] = 1;
  tree_contains_struct[SYMBOL_MEMORY_TAG][TS_DECL_MINIMAL] = 1;

  tree_contains_struct[STRUCT_FIELD_TAG][TS_MEMORY_TAG] = 1;
  tree_contains_struct[NAME_MEMORY_TAG][TS_MEMORY_TAG] = 1;
  tree_contains_struct[SYMBOL_MEMORY_TAG][TS_MEMORY_TAG] = 1;

  tree_contains_struct[STRUCT_FIELD_TAG][TS_STRUCT_FIELD_TAG] = 1;

  tree_contains_struct[VAR_DECL][TS_DECL_WITH_VIS] = 1;
  tree_contains_struct[FUNCTION_DECL][TS_DECL_WITH_VIS] = 1;
  tree_contains_struct[TYPE_DECL][TS_DECL_WITH_VIS] = 1;
  tree_contains_struct[TRANSLATION_UNIT_DECL][TS_DECL_WITH_VIS] = 1;
  
  tree_contains_struct[VAR_DECL][TS_VAR_DECL] = 1;
  tree_contains_struct[FIELD_DECL][TS_FIELD_DECL] = 1;
  tree_contains_struct[PARM_DECL][TS_PARM_DECL] = 1;
  tree_contains_struct[LABEL_DECL][TS_LABEL_DECL] = 1;
  tree_contains_struct[RESULT_DECL][TS_RESULT_DECL] = 1;
  tree_contains_struct[CONST_DECL][TS_CONST_DECL] = 1;
  tree_contains_struct[TYPE_DECL][TS_TYPE_DECL] = 1;
  tree_contains_struct[FUNCTION_DECL][TS_FUNCTION_DECL] = 1;

  lang_hooks.init_ts ();
}


/* The name of the object as the assembler will see it (but before any
   translations made by ASM_OUTPUT_LABELREF).  Often this is the same
   as DECL_NAME.  It is an IDENTIFIER_NODE.  */
tree
decl_assembler_name (tree decl)
{
  if (!DECL_ASSEMBLER_NAME_SET_P (decl))
    lang_hooks.set_decl_assembler_name (decl);
  return DECL_WITH_VIS_CHECK (decl)->decl_with_vis.assembler_name;
}

/* Compute the number of bytes occupied by a tree with code CODE.
   This function cannot be used for TREE_VEC, PHI_NODE, or STRING_CST
   codes, which are of variable length.  */
size_t
tree_code_size (enum tree_code code)
{
  switch (TREE_CODE_CLASS (code))
    {
    case tcc_declaration:  /* A decl node */
      {
	switch (code)
	  {
	  case FIELD_DECL:
	    return sizeof (struct tree_field_decl);
	  case PARM_DECL:
	    return sizeof (struct tree_parm_decl);
	  case VAR_DECL:
	    return sizeof (struct tree_var_decl);
	  case LABEL_DECL:
	    return sizeof (struct tree_label_decl);
	  case RESULT_DECL:
	    return sizeof (struct tree_result_decl);
	  case CONST_DECL:
	    return sizeof (struct tree_const_decl);
	  case TYPE_DECL:
	    return sizeof (struct tree_type_decl);
	  case FUNCTION_DECL:
	    return sizeof (struct tree_function_decl);
	  case NAME_MEMORY_TAG:
	  case SYMBOL_MEMORY_TAG:
	    return sizeof (struct tree_memory_tag);
	  case STRUCT_FIELD_TAG:
	    return sizeof (struct tree_struct_field_tag);
	  default:
	    return sizeof (struct tree_decl_non_common);
	  }
      }

    case tcc_type:  /* a type node */
      return sizeof (struct tree_type);

    case tcc_reference:   /* a reference */
    case tcc_expression:  /* an expression */
    case tcc_statement:   /* an expression with side effects */
    case tcc_comparison:  /* a comparison expression */
    case tcc_unary:       /* a unary arithmetic expression */
    case tcc_binary:      /* a binary arithmetic expression */
      return (sizeof (struct tree_exp)
	      + (TREE_CODE_LENGTH (code) - 1) * sizeof (char *));

    case tcc_constant:  /* a constant */
      switch (code)
	{
	case INTEGER_CST:	return sizeof (struct tree_int_cst);
	case REAL_CST:		return sizeof (struct tree_real_cst);
	case COMPLEX_CST:	return sizeof (struct tree_complex);
	case VECTOR_CST:	return sizeof (struct tree_vector);
	case STRING_CST:	gcc_unreachable ();
	default:
	  return lang_hooks.tree_size (code);
	}

    case tcc_exceptional:  /* something random, like an identifier.  */
      switch (code)
	{
	case IDENTIFIER_NODE:	return lang_hooks.identifier_size;
	case TREE_LIST:		return sizeof (struct tree_list);

	case ERROR_MARK:
	case PLACEHOLDER_EXPR:	return sizeof (struct tree_common);

	case TREE_VEC:
	case OMP_CLAUSE:
	case PHI_NODE:		gcc_unreachable ();

	case SSA_NAME:		return sizeof (struct tree_ssa_name);

	case STATEMENT_LIST:	return sizeof (struct tree_statement_list);
	case BLOCK:		return sizeof (struct tree_block);
	case VALUE_HANDLE:	return sizeof (struct tree_value_handle);
	case CONSTRUCTOR:	return sizeof (struct tree_constructor);

	default:
	  return lang_hooks.tree_size (code);
	}

    default:
      gcc_unreachable ();
    }
}

/* Compute the number of bytes occupied by NODE.  This routine only
   looks at TREE_CODE, except for PHI_NODE and TREE_VEC nodes.  */
size_t
tree_size (tree node)
{
  enum tree_code code = TREE_CODE (node);
  switch (code)
    {
    case PHI_NODE:
      return (sizeof (struct tree_phi_node)
	      + (PHI_ARG_CAPACITY (node) - 1) * sizeof (struct phi_arg_d));

    case TREE_BINFO:
      return (offsetof (struct tree_binfo, base_binfos)
	      + VEC_embedded_size (tree, BINFO_N_BASE_BINFOS (node)));

    case TREE_VEC:
      return (sizeof (struct tree_vec)
	      + (TREE_VEC_LENGTH (node) - 1) * sizeof(char *));

    case STRING_CST:
      return TREE_STRING_LENGTH (node) + offsetof (struct tree_string, str) + 1;

    case OMP_CLAUSE:
      return (sizeof (struct tree_omp_clause)
	      + (omp_clause_num_ops[OMP_CLAUSE_CODE (node)] - 1)
	        * sizeof (tree));

    default:
      return tree_code_size (code);
    }
}

/* Return a newly allocated node of code CODE.  For decl and type
   nodes, some other fields are initialized.  The rest of the node is
   initialized to zero.  This function cannot be used for PHI_NODE,
   TREE_VEC or OMP_CLAUSE nodes, which is enforced by asserts in
   tree_code_size.

   Achoo!  I got a code in the node.  */

tree
make_node_stat (enum tree_code code MEM_STAT_DECL)
{
  tree t;
  enum tree_code_class type = TREE_CODE_CLASS (code);
  size_t length = tree_code_size (code);
#ifdef GATHER_STATISTICS
  tree_node_kind kind;

  switch (type)
    {
    case tcc_declaration:  /* A decl node */
      kind = d_kind;
      break;

    case tcc_type:  /* a type node */
      kind = t_kind;
      break;

    case tcc_statement:  /* an expression with side effects */
      kind = s_kind;
      break;

    case tcc_reference:  /* a reference */
      kind = r_kind;
      break;

    case tcc_expression:  /* an expression */
    case tcc_comparison:  /* a comparison expression */
    case tcc_unary:  /* a unary arithmetic expression */
    case tcc_binary:  /* a binary arithmetic expression */
      kind = e_kind;
      break;

    case tcc_constant:  /* a constant */
      kind = c_kind;
      break;

    case tcc_exceptional:  /* something random, like an identifier.  */
      switch (code)
	{
	case IDENTIFIER_NODE:
	  kind = id_kind;
	  break;

	case TREE_VEC:
	  kind = vec_kind;
	  break;

	case TREE_BINFO:
	  kind = binfo_kind;
	  break;

	case PHI_NODE:
	  kind = phi_kind;
	  break;

	case SSA_NAME:
	  kind = ssa_name_kind;
	  break;

	case BLOCK:
	  kind = b_kind;
	  break;

	case CONSTRUCTOR:
	  kind = constr_kind;
	  break;

	default:
	  kind = x_kind;
	  break;
	}
      break;
      
    default:
      gcc_unreachable ();
    }

  tree_node_counts[(int) kind]++;
  tree_node_sizes[(int) kind] += length;
#endif

  if (code == IDENTIFIER_NODE)
    t = ggc_alloc_zone_pass_stat (length, &tree_id_zone);
  else
    t = ggc_alloc_zone_pass_stat (length, &tree_zone);

  memset (t, 0, length);

  TREE_SET_CODE (t, code);

  switch (type)
    {
    case tcc_statement:
      TREE_SIDE_EFFECTS (t) = 1;
      break;

    case tcc_declaration:
      if (CODE_CONTAINS_STRUCT (code, TS_DECL_WITH_VIS))
	DECL_IN_SYSTEM_HEADER (t) = in_system_header;
      if (CODE_CONTAINS_STRUCT (code, TS_DECL_COMMON))
	{
	  if (code == FUNCTION_DECL)
	    {
	      DECL_ALIGN (t) = FUNCTION_BOUNDARY;
	      DECL_MODE (t) = FUNCTION_MODE;
	    }
	  else
	    DECL_ALIGN (t) = 1;
	  /* We have not yet computed the alias set for this declaration.  */
	  DECL_POINTER_ALIAS_SET (t) = -1;
	}
      DECL_SOURCE_LOCATION (t) = input_location;
      DECL_UID (t) = next_decl_uid++;

      break;

    case tcc_type:
      TYPE_UID (t) = next_type_uid++;
      TYPE_ALIGN (t) = BITS_PER_UNIT;
      TYPE_USER_ALIGN (t) = 0;
      TYPE_MAIN_VARIANT (t) = t;

      /* Default to no attributes for type, but let target change that.  */
      TYPE_ATTRIBUTES (t) = NULL_TREE;
      targetm.set_default_type_attributes (t);

      /* We have not yet computed the alias set for this type.  */
      TYPE_ALIAS_SET (t) = -1;
      break;

    case tcc_constant:
      TREE_CONSTANT (t) = 1;
      TREE_INVARIANT (t) = 1;
      break;

    case tcc_expression:
      switch (code)
	{
	case INIT_EXPR:
	case MODIFY_EXPR:
	case VA_ARG_EXPR:
	case PREDECREMENT_EXPR:
	case PREINCREMENT_EXPR:
	case POSTDECREMENT_EXPR:
	case POSTINCREMENT_EXPR:
	  /* All of these have side-effects, no matter what their
	     operands are.  */
	  TREE_SIDE_EFFECTS (t) = 1;
	  break;

	default:
	  break;
	}
      break;

    default:
      /* Other classes need no special treatment.  */
      break;
    }

  return t;
}

/* Return a new node with the same contents as NODE except that its
   TREE_CHAIN is zero and it has a fresh uid.  */

tree
copy_node_stat (tree node MEM_STAT_DECL)
{
  tree t;
  enum tree_code code = TREE_CODE (node);
  size_t length;

  gcc_assert (code != STATEMENT_LIST);

  length = tree_size (node);
  t = ggc_alloc_zone_pass_stat (length, &tree_zone);
  memcpy (t, node, length);

  TREE_CHAIN (t) = 0;
  TREE_ASM_WRITTEN (t) = 0;
  TREE_VISITED (t) = 0;
  t->common.ann = 0;

  if (TREE_CODE_CLASS (code) == tcc_declaration)
    {
      DECL_UID (t) = next_decl_uid++;
      if ((TREE_CODE (node) == PARM_DECL || TREE_CODE (node) == VAR_DECL)
	  && DECL_HAS_VALUE_EXPR_P (node))
	{
	  SET_DECL_VALUE_EXPR (t, DECL_VALUE_EXPR (node));
	  DECL_HAS_VALUE_EXPR_P (t) = 1;
	}
      if (TREE_CODE (node) == VAR_DECL && DECL_HAS_INIT_PRIORITY_P (node))
	{
	  SET_DECL_INIT_PRIORITY (t, DECL_INIT_PRIORITY (node));
	  DECL_HAS_INIT_PRIORITY_P (t) = 1;
	}
      if (TREE_CODE (node) == VAR_DECL && DECL_BASED_ON_RESTRICT_P (node))
	{
	  SET_DECL_RESTRICT_BASE (t, DECL_GET_RESTRICT_BASE (node));
	  DECL_BASED_ON_RESTRICT_P (t) = 1;
	}
    }
  else if (TREE_CODE_CLASS (code) == tcc_type)
    {
      TYPE_UID (t) = next_type_uid++;
      /* The following is so that the debug code for
	 the copy is different from the original type.
	 The two statements usually duplicate each other
	 (because they clear fields of the same union),
	 but the optimizer should catch that.  */
      TYPE_SYMTAB_POINTER (t) = 0;
      TYPE_SYMTAB_ADDRESS (t) = 0;
      
      /* Do not copy the values cache.  */
      if (TYPE_CACHED_VALUES_P(t))
	{
	  TYPE_CACHED_VALUES_P (t) = 0;
	  TYPE_CACHED_VALUES (t) = NULL_TREE;
	}
    }

  return t;
}

/* Return a copy of a chain of nodes, chained through the TREE_CHAIN field.
   For example, this can copy a list made of TREE_LIST nodes.  */

tree
copy_list (tree list)
{
  tree head;
  tree prev, next;

  if (list == 0)
    return 0;

  head = prev = copy_node (list);
  next = TREE_CHAIN (list);
  while (next)
    {
      TREE_CHAIN (prev) = copy_node (next);
      prev = TREE_CHAIN (prev);
      next = TREE_CHAIN (next);
    }
  return head;
}


/* Create an INT_CST node with a LOW value sign extended.  */

tree
build_int_cst (tree type, HOST_WIDE_INT low)
{
  return build_int_cst_wide (type, low, low < 0 ? -1 : 0);
}

/* Create an INT_CST node with a LOW value zero extended.  */

tree
build_int_cstu (tree type, unsigned HOST_WIDE_INT low)
{
  return build_int_cst_wide (type, low, 0);
}

/* Create an INT_CST node with a LOW value in TYPE.  The value is sign extended
   if it is negative.  This function is similar to build_int_cst, but
   the extra bits outside of the type precision are cleared.  Constants
   with these extra bits may confuse the fold so that it detects overflows
   even in cases when they do not occur, and in general should be avoided.
   We cannot however make this a default behavior of build_int_cst without
   more intrusive changes, since there are parts of gcc that rely on the extra
   precision of the integer constants.  */

tree
build_int_cst_type (tree type, HOST_WIDE_INT low)
{
  unsigned HOST_WIDE_INT val = (unsigned HOST_WIDE_INT) low;
  unsigned HOST_WIDE_INT hi, mask;
  unsigned bits;
  bool signed_p;
  bool negative;

  if (!type)
    type = integer_type_node;

  bits = TYPE_PRECISION (type);
  signed_p = !TYPE_UNSIGNED (type);

  if (bits >= HOST_BITS_PER_WIDE_INT)
    negative = (low < 0);
  else
    {
      /* If the sign bit is inside precision of LOW, use it to determine
	 the sign of the constant.  */
      negative = ((val >> (bits - 1)) & 1) != 0;

      /* Mask out the bits outside of the precision of the constant.  */
      mask = (((unsigned HOST_WIDE_INT) 2) << (bits - 1)) - 1;

      if (signed_p && negative)
	val |= ~mask;
      else
	val &= mask;
    }

  /* Determine the high bits.  */
  hi = (negative ? ~(unsigned HOST_WIDE_INT) 0 : 0);

  /* For unsigned type we need to mask out the bits outside of the type
     precision.  */
  if (!signed_p)
    {
      if (bits <= HOST_BITS_PER_WIDE_INT)
	hi = 0;
      else
	{
	  bits -= HOST_BITS_PER_WIDE_INT;
	  mask = (((unsigned HOST_WIDE_INT) 2) << (bits - 1)) - 1;
	  hi &= mask;
	}
    }

  return build_int_cst_wide (type, val, hi);
}

/* These are the hash table functions for the hash table of INTEGER_CST
   nodes of a sizetype.  */

/* Return the hash code code X, an INTEGER_CST.  */

static hashval_t
int_cst_hash_hash (const void *x)
{
  tree t = (tree) x;

  return (TREE_INT_CST_HIGH (t) ^ TREE_INT_CST_LOW (t)
	  ^ htab_hash_pointer (TREE_TYPE (t)));
}

/* Return nonzero if the value represented by *X (an INTEGER_CST tree node)
   is the same as that given by *Y, which is the same.  */

static int
int_cst_hash_eq (const void *x, const void *y)
{
  tree xt = (tree) x;
  tree yt = (tree) y;

  return (TREE_TYPE (xt) == TREE_TYPE (yt)
	  && TREE_INT_CST_HIGH (xt) == TREE_INT_CST_HIGH (yt)
	  && TREE_INT_CST_LOW (xt) == TREE_INT_CST_LOW (yt));
}

/* Create an INT_CST node of TYPE and value HI:LOW.  If TYPE is NULL,
   integer_type_node is used.  The returned node is always shared.
   For small integers we use a per-type vector cache, for larger ones
   we use a single hash table.  */

tree
build_int_cst_wide (tree type, unsigned HOST_WIDE_INT low, HOST_WIDE_INT hi)
{
  tree t;
  int ix = -1;
  int limit = 0;

  if (!type)
    type = integer_type_node;

  switch (TREE_CODE (type))
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      /* Cache NULL pointer.  */
      if (!hi && !low)
	{
	  limit = 1;
	  ix = 0;
	}
      break;

    case BOOLEAN_TYPE:
      /* Cache false or true.  */
      limit = 2;
      if (!hi && low < 2)
	ix = low;
      break;

    case INTEGER_TYPE:
    case OFFSET_TYPE:
      if (TYPE_UNSIGNED (type))
	{
	  /* Cache 0..N */
	  limit = INTEGER_SHARE_LIMIT;
	  if (!hi && low < (unsigned HOST_WIDE_INT)INTEGER_SHARE_LIMIT)
	    ix = low;
	}
      else
	{
	  /* Cache -1..N */
	  limit = INTEGER_SHARE_LIMIT + 1;
	  if (!hi && low < (unsigned HOST_WIDE_INT)INTEGER_SHARE_LIMIT)
	    ix = low + 1;
	  else if (hi == -1 && low == -(unsigned HOST_WIDE_INT)1)
	    ix = 0;
	}
      break;
    default:
      break;
    }

  if (ix >= 0)
    {
      /* Look for it in the type's vector of small shared ints.  */
      if (!TYPE_CACHED_VALUES_P (type))
	{
	  TYPE_CACHED_VALUES_P (type) = 1;
	  TYPE_CACHED_VALUES (type) = make_tree_vec (limit);
	}

      t = TREE_VEC_ELT (TYPE_CACHED_VALUES (type), ix);
      if (t)
	{
	  /* Make sure no one is clobbering the shared constant.  */
	  gcc_assert (TREE_TYPE (t) == type);
	  gcc_assert (TREE_INT_CST_LOW (t) == low);
	  gcc_assert (TREE_INT_CST_HIGH (t) == hi);
	}
      else
	{
	  /* Create a new shared int.  */
	  t = make_node (INTEGER_CST);

	  TREE_INT_CST_LOW (t) = low;
	  TREE_INT_CST_HIGH (t) = hi;
	  TREE_TYPE (t) = type;
	  
	  TREE_VEC_ELT (TYPE_CACHED_VALUES (type), ix) = t;
	}
    }
  else
    {
      /* Use the cache of larger shared ints.  */
      void **slot;

      TREE_INT_CST_LOW (int_cst_node) = low;
      TREE_INT_CST_HIGH (int_cst_node) = hi;
      TREE_TYPE (int_cst_node) = type;

      slot = htab_find_slot (int_cst_hash_table, int_cst_node, INSERT);
      t = *slot;
      if (!t)
	{
	  /* Insert this one into the hash table.  */
	  t = int_cst_node;
	  *slot = t;
	  /* Make a new node for next time round.  */
	  int_cst_node = make_node (INTEGER_CST);
	}
    }

  return t;
}

/* Builds an integer constant in TYPE such that lowest BITS bits are ones
   and the rest are zeros.  */

tree
build_low_bits_mask (tree type, unsigned bits)
{
  unsigned HOST_WIDE_INT low;
  HOST_WIDE_INT high;
  unsigned HOST_WIDE_INT all_ones = ~(unsigned HOST_WIDE_INT) 0;

  gcc_assert (bits <= TYPE_PRECISION (type));

  if (bits == TYPE_PRECISION (type)
      && !TYPE_UNSIGNED (type))
    {
      /* Sign extended all-ones mask.  */
      low = all_ones;
      high = -1;
    }
  else if (bits <= HOST_BITS_PER_WIDE_INT)
    {
      low = all_ones >> (HOST_BITS_PER_WIDE_INT - bits);
      high = 0;
    }
  else
    {
      bits -= HOST_BITS_PER_WIDE_INT;
      low = all_ones;
      high = all_ones >> (HOST_BITS_PER_WIDE_INT - bits);
    }

  return build_int_cst_wide (type, low, high);
}

/* Checks that X is integer constant that can be expressed in (unsigned)
   HOST_WIDE_INT without loss of precision.  */

bool
cst_and_fits_in_hwi (tree x)
{
  if (TREE_CODE (x) != INTEGER_CST)
    return false;

  if (TYPE_PRECISION (TREE_TYPE (x)) > HOST_BITS_PER_WIDE_INT)
    return false;

  return (TREE_INT_CST_HIGH (x) == 0
	  || TREE_INT_CST_HIGH (x) == -1);
}

/* Return a new VECTOR_CST node whose type is TYPE and whose values
   are in a list pointed to by VALS.  */

tree
build_vector (tree type, tree vals)
{
  tree v = make_node (VECTOR_CST);
  int over1 = 0, over2 = 0;
  tree link;

  TREE_VECTOR_CST_ELTS (v) = vals;
  TREE_TYPE (v) = type;

  /* Iterate through elements and check for overflow.  */
  for (link = vals; link; link = TREE_CHAIN (link))
    {
      tree value = TREE_VALUE (link);

      /* Don't crash if we get an address constant.  */
      if (!CONSTANT_CLASS_P (value))
	continue;

      over1 |= TREE_OVERFLOW (value);
      over2 |= TREE_CONSTANT_OVERFLOW (value);
    }

  TREE_OVERFLOW (v) = over1;
  TREE_CONSTANT_OVERFLOW (v) = over2;

  return v;
}

/* Return a new VECTOR_CST node whose type is TYPE and whose values
   are extracted from V, a vector of CONSTRUCTOR_ELT.  */

tree
build_vector_from_ctor (tree type, VEC(constructor_elt,gc) *v)
{
  tree list = NULL_TREE;
  unsigned HOST_WIDE_INT idx;
  tree value;

  FOR_EACH_CONSTRUCTOR_VALUE (v, idx, value)
    list = tree_cons (NULL_TREE, value, list);
  return build_vector (type, nreverse (list));
}

/* Return a new CONSTRUCTOR node whose type is TYPE and whose values
   are in the VEC pointed to by VALS.  */
tree
build_constructor (tree type, VEC(constructor_elt,gc) *vals)
{
  tree c = make_node (CONSTRUCTOR);
  TREE_TYPE (c) = type;
  CONSTRUCTOR_ELTS (c) = vals;
  return c;
}

/* Build a CONSTRUCTOR node made of a single initializer, with the specified
   INDEX and VALUE.  */
tree
build_constructor_single (tree type, tree index, tree value)
{
  VEC(constructor_elt,gc) *v;
  constructor_elt *elt;
  tree t;

  v = VEC_alloc (constructor_elt, gc, 1);
  elt = VEC_quick_push (constructor_elt, v, NULL);
  elt->index = index;
  elt->value = value;

  t = build_constructor (type, v);
  TREE_CONSTANT (t) = TREE_CONSTANT (value);
  return t;
}


/* Return a new CONSTRUCTOR node whose type is TYPE and whose values
   are in a list pointed to by VALS.  */
tree
build_constructor_from_list (tree type, tree vals)
{
  tree t, val;
  VEC(constructor_elt,gc) *v = NULL;
  bool constant_p = true;

  if (vals)
    {
      v = VEC_alloc (constructor_elt, gc, list_length (vals));
      for (t = vals; t; t = TREE_CHAIN (t))
	{
	  constructor_elt *elt = VEC_quick_push (constructor_elt, v, NULL);
	  val = TREE_VALUE (t);
	  elt->index = TREE_PURPOSE (t);
	  elt->value = val;
	  if (!TREE_CONSTANT (val))
	    constant_p = false;
	}
    }

  t = build_constructor (type, v);
  TREE_CONSTANT (t) = constant_p;
  return t;
}


/* Return a new REAL_CST node whose type is TYPE and value is D.  */

tree
build_real (tree type, REAL_VALUE_TYPE d)
{
  tree v;
  REAL_VALUE_TYPE *dp;
  int overflow = 0;

  /* ??? Used to check for overflow here via CHECK_FLOAT_TYPE.
     Consider doing it via real_convert now.  */

  v = make_node (REAL_CST);
  dp = ggc_alloc (sizeof (REAL_VALUE_TYPE));
  memcpy (dp, &d, sizeof (REAL_VALUE_TYPE));

  TREE_TYPE (v) = type;
  TREE_REAL_CST_PTR (v) = dp;
  TREE_OVERFLOW (v) = TREE_CONSTANT_OVERFLOW (v) = overflow;
  return v;
}

/* Return a new REAL_CST node whose type is TYPE
   and whose value is the integer value of the INTEGER_CST node I.  */

REAL_VALUE_TYPE
real_value_from_int_cst (tree type, tree i)
{
  REAL_VALUE_TYPE d;

  /* Clear all bits of the real value type so that we can later do
     bitwise comparisons to see if two values are the same.  */
  memset (&d, 0, sizeof d);

  real_from_integer (&d, type ? TYPE_MODE (type) : VOIDmode,
		     TREE_INT_CST_LOW (i), TREE_INT_CST_HIGH (i),
		     TYPE_UNSIGNED (TREE_TYPE (i)));
  return d;
}

/* Given a tree representing an integer constant I, return a tree
   representing the same value as a floating-point constant of type TYPE.  */

tree
build_real_from_int_cst (tree type, tree i)
{
  tree v;
  int overflow = TREE_OVERFLOW (i);

  v = build_real (type, real_value_from_int_cst (type, i));

  TREE_OVERFLOW (v) |= overflow;
  TREE_CONSTANT_OVERFLOW (v) |= overflow;
  return v;
}

/* Return a newly constructed STRING_CST node whose value is
   the LEN characters at STR.
   The TREE_TYPE is not initialized.  */

tree
build_string (int len, const char *str)
{
  tree s;
  size_t length;

  /* Do not waste bytes provided by padding of struct tree_string.  */
  length = len + offsetof (struct tree_string, str) + 1;

#ifdef GATHER_STATISTICS
  tree_node_counts[(int) c_kind]++;
  tree_node_sizes[(int) c_kind] += length;
#endif  

  s = ggc_alloc_tree (length);

  memset (s, 0, sizeof (struct tree_common));
  TREE_SET_CODE (s, STRING_CST);
  TREE_CONSTANT (s) = 1;
  TREE_INVARIANT (s) = 1;
  TREE_STRING_LENGTH (s) = len;
  memcpy ((char *) TREE_STRING_POINTER (s), str, len);
  ((char *) TREE_STRING_POINTER (s))[len] = '\0';

  return s;
}

/* Return a newly constructed COMPLEX_CST node whose value is
   specified by the real and imaginary parts REAL and IMAG.
   Both REAL and IMAG should be constant nodes.  TYPE, if specified,
   will be the type of the COMPLEX_CST; otherwise a new type will be made.  */

tree
build_complex (tree type, tree real, tree imag)
{
  tree t = make_node (COMPLEX_CST);

  TREE_REALPART (t) = real;
  TREE_IMAGPART (t) = imag;
  TREE_TYPE (t) = type ? type : build_complex_type (TREE_TYPE (real));
  TREE_OVERFLOW (t) = TREE_OVERFLOW (real) | TREE_OVERFLOW (imag);
  TREE_CONSTANT_OVERFLOW (t)
    = TREE_CONSTANT_OVERFLOW (real) | TREE_CONSTANT_OVERFLOW (imag);
  return t;
}

/* Return a constant of arithmetic type TYPE which is the
   multiplicative identity of the set TYPE.  */

tree
build_one_cst (tree type)
{
  switch (TREE_CODE (type))
    {
    case INTEGER_TYPE: case ENUMERAL_TYPE: case BOOLEAN_TYPE:
    case POINTER_TYPE: case REFERENCE_TYPE:
    case OFFSET_TYPE:
      return build_int_cst (type, 1);

    case REAL_TYPE:
      return build_real (type, dconst1);

    case VECTOR_TYPE:
      {
	tree scalar, cst;
	int i;

	scalar = build_one_cst (TREE_TYPE (type));

	/* Create 'vect_cst_ = {cst,cst,...,cst}'  */
	cst = NULL_TREE;
	for (i = TYPE_VECTOR_SUBPARTS (type); --i >= 0; )
	  cst = tree_cons (NULL_TREE, scalar, cst);

	return build_vector (type, cst);
      }

    case COMPLEX_TYPE:
      return build_complex (type,
			    build_one_cst (TREE_TYPE (type)),
			    fold_convert (TREE_TYPE (type), integer_zero_node));

    default:
      gcc_unreachable ();
    }
}

/* Build a BINFO with LEN language slots.  */

tree
make_tree_binfo_stat (unsigned base_binfos MEM_STAT_DECL)
{
  tree t;
  size_t length = (offsetof (struct tree_binfo, base_binfos)
		   + VEC_embedded_size (tree, base_binfos));

#ifdef GATHER_STATISTICS
  tree_node_counts[(int) binfo_kind]++;
  tree_node_sizes[(int) binfo_kind] += length;
#endif

  t = ggc_alloc_zone_pass_stat (length, &tree_zone);

  memset (t, 0, offsetof (struct tree_binfo, base_binfos));

  TREE_SET_CODE (t, TREE_BINFO);

  VEC_embedded_init (tree, BINFO_BASE_BINFOS (t), base_binfos);

  return t;
}


/* Build a newly constructed TREE_VEC node of length LEN.  */

tree
make_tree_vec_stat (int len MEM_STAT_DECL)
{
  tree t;
  int length = (len - 1) * sizeof (tree) + sizeof (struct tree_vec);

#ifdef GATHER_STATISTICS
  tree_node_counts[(int) vec_kind]++;
  tree_node_sizes[(int) vec_kind] += length;
#endif

  t = ggc_alloc_zone_pass_stat (length, &tree_zone);

  memset (t, 0, length);

  TREE_SET_CODE (t, TREE_VEC);
  TREE_VEC_LENGTH (t) = len;

  return t;
}

/* Return 1 if EXPR is the integer constant zero or a complex constant
   of zero.  */

int
integer_zerop (tree expr)
{
  STRIP_NOPS (expr);

  return ((TREE_CODE (expr) == INTEGER_CST
	   && TREE_INT_CST_LOW (expr) == 0
	   && TREE_INT_CST_HIGH (expr) == 0)
	  || (TREE_CODE (expr) == COMPLEX_CST
	      && integer_zerop (TREE_REALPART (expr))
	      && integer_zerop (TREE_IMAGPART (expr))));
}

/* Return 1 if EXPR is the integer constant one or the corresponding
   complex constant.  */

int
integer_onep (tree expr)
{
  STRIP_NOPS (expr);

  return ((TREE_CODE (expr) == INTEGER_CST
	   && TREE_INT_CST_LOW (expr) == 1
	   && TREE_INT_CST_HIGH (expr) == 0)
	  || (TREE_CODE (expr) == COMPLEX_CST
	      && integer_onep (TREE_REALPART (expr))
	      && integer_zerop (TREE_IMAGPART (expr))));
}

/* Return 1 if EXPR is an integer containing all 1's in as much precision as
   it contains.  Likewise for the corresponding complex constant.  */

int
integer_all_onesp (tree expr)
{
  int prec;
  int uns;

  STRIP_NOPS (expr);

  if (TREE_CODE (expr) == COMPLEX_CST
      && integer_all_onesp (TREE_REALPART (expr))
      && integer_zerop (TREE_IMAGPART (expr)))
    return 1;

  else if (TREE_CODE (expr) != INTEGER_CST)
    return 0;

  uns = TYPE_UNSIGNED (TREE_TYPE (expr));
  if (TREE_INT_CST_LOW (expr) == ~(unsigned HOST_WIDE_INT) 0
      && TREE_INT_CST_HIGH (expr) == -1)
    return 1;
  if (!uns)
    return 0;

  /* Note that using TYPE_PRECISION here is wrong.  We care about the
     actual bits, not the (arbitrary) range of the type.  */
  prec = GET_MODE_BITSIZE (TYPE_MODE (TREE_TYPE (expr)));
  if (prec >= HOST_BITS_PER_WIDE_INT)
    {
      HOST_WIDE_INT high_value;
      int shift_amount;

      shift_amount = prec - HOST_BITS_PER_WIDE_INT;

      /* Can not handle precisions greater than twice the host int size.  */
      gcc_assert (shift_amount <= HOST_BITS_PER_WIDE_INT);
      if (shift_amount == HOST_BITS_PER_WIDE_INT)
	/* Shifting by the host word size is undefined according to the ANSI
	   standard, so we must handle this as a special case.  */
	high_value = -1;
      else
	high_value = ((HOST_WIDE_INT) 1 << shift_amount) - 1;

      return (TREE_INT_CST_LOW (expr) == ~(unsigned HOST_WIDE_INT) 0
	      && TREE_INT_CST_HIGH (expr) == high_value);
    }
  else
    return TREE_INT_CST_LOW (expr) == ((unsigned HOST_WIDE_INT) 1 << prec) - 1;
}

/* Return 1 if EXPR is an integer constant that is a power of 2 (i.e., has only
   one bit on).  */

int
integer_pow2p (tree expr)
{
  int prec;
  HOST_WIDE_INT high, low;

  STRIP_NOPS (expr);

  if (TREE_CODE (expr) == COMPLEX_CST
      && integer_pow2p (TREE_REALPART (expr))
      && integer_zerop (TREE_IMAGPART (expr)))
    return 1;

  if (TREE_CODE (expr) != INTEGER_CST)
    return 0;

  prec = (POINTER_TYPE_P (TREE_TYPE (expr))
	  ? POINTER_SIZE : TYPE_PRECISION (TREE_TYPE (expr)));
  high = TREE_INT_CST_HIGH (expr);
  low = TREE_INT_CST_LOW (expr);

  /* First clear all bits that are beyond the type's precision in case
     we've been sign extended.  */

  if (prec == 2 * HOST_BITS_PER_WIDE_INT)
    ;
  else if (prec > HOST_BITS_PER_WIDE_INT)
    high &= ~((HOST_WIDE_INT) (-1) << (prec - HOST_BITS_PER_WIDE_INT));
  else
    {
      high = 0;
      if (prec < HOST_BITS_PER_WIDE_INT)
	low &= ~((HOST_WIDE_INT) (-1) << prec);
    }

  if (high == 0 && low == 0)
    return 0;

  return ((high == 0 && (low & (low - 1)) == 0)
	  || (low == 0 && (high & (high - 1)) == 0));
}

/* Return 1 if EXPR is an integer constant other than zero or a
   complex constant other than zero.  */

int
integer_nonzerop (tree expr)
{
  STRIP_NOPS (expr);

  return ((TREE_CODE (expr) == INTEGER_CST
	   && (TREE_INT_CST_LOW (expr) != 0
	       || TREE_INT_CST_HIGH (expr) != 0))
	  || (TREE_CODE (expr) == COMPLEX_CST
	      && (integer_nonzerop (TREE_REALPART (expr))
		  || integer_nonzerop (TREE_IMAGPART (expr)))));
}

/* Return the power of two represented by a tree node known to be a
   power of two.  */

int
tree_log2 (tree expr)
{
  int prec;
  HOST_WIDE_INT high, low;

  STRIP_NOPS (expr);

  if (TREE_CODE (expr) == COMPLEX_CST)
    return tree_log2 (TREE_REALPART (expr));

  prec = (POINTER_TYPE_P (TREE_TYPE (expr))
	  ? POINTER_SIZE : TYPE_PRECISION (TREE_TYPE (expr)));

  high = TREE_INT_CST_HIGH (expr);
  low = TREE_INT_CST_LOW (expr);

  /* First clear all bits that are beyond the type's precision in case
     we've been sign extended.  */

  if (prec == 2 * HOST_BITS_PER_WIDE_INT)
    ;
  else if (prec > HOST_BITS_PER_WIDE_INT)
    high &= ~((HOST_WIDE_INT) (-1) << (prec - HOST_BITS_PER_WIDE_INT));
  else
    {
      high = 0;
      if (prec < HOST_BITS_PER_WIDE_INT)
	low &= ~((HOST_WIDE_INT) (-1) << prec);
    }

  return (high != 0 ? HOST_BITS_PER_WIDE_INT + exact_log2 (high)
	  : exact_log2 (low));
}

/* Similar, but return the largest integer Y such that 2 ** Y is less
   than or equal to EXPR.  */

int
tree_floor_log2 (tree expr)
{
  int prec;
  HOST_WIDE_INT high, low;

  STRIP_NOPS (expr);

  if (TREE_CODE (expr) == COMPLEX_CST)
    return tree_log2 (TREE_REALPART (expr));

  prec = (POINTER_TYPE_P (TREE_TYPE (expr))
	  ? POINTER_SIZE : TYPE_PRECISION (TREE_TYPE (expr)));

  high = TREE_INT_CST_HIGH (expr);
  low = TREE_INT_CST_LOW (expr);

  /* First clear all bits that are beyond the type's precision in case
     we've been sign extended.  Ignore if type's precision hasn't been set
     since what we are doing is setting it.  */

  if (prec == 2 * HOST_BITS_PER_WIDE_INT || prec == 0)
    ;
  else if (prec > HOST_BITS_PER_WIDE_INT)
    high &= ~((HOST_WIDE_INT) (-1) << (prec - HOST_BITS_PER_WIDE_INT));
  else
    {
      high = 0;
      if (prec < HOST_BITS_PER_WIDE_INT)
	low &= ~((HOST_WIDE_INT) (-1) << prec);
    }

  return (high != 0 ? HOST_BITS_PER_WIDE_INT + floor_log2 (high)
	  : floor_log2 (low));
}

/* Return 1 if EXPR is the real constant zero.  */

int
real_zerop (tree expr)
{
  STRIP_NOPS (expr);

  return ((TREE_CODE (expr) == REAL_CST
	   && REAL_VALUES_EQUAL (TREE_REAL_CST (expr), dconst0))
	  || (TREE_CODE (expr) == COMPLEX_CST
	      && real_zerop (TREE_REALPART (expr))
	      && real_zerop (TREE_IMAGPART (expr))));
}

/* Return 1 if EXPR is the real constant one in real or complex form.  */

int
real_onep (tree expr)
{
  STRIP_NOPS (expr);

  return ((TREE_CODE (expr) == REAL_CST
	   && REAL_VALUES_EQUAL (TREE_REAL_CST (expr), dconst1))
	  || (TREE_CODE (expr) == COMPLEX_CST
	      && real_onep (TREE_REALPART (expr))
	      && real_zerop (TREE_IMAGPART (expr))));
}

/* Return 1 if EXPR is the real constant two.  */

int
real_twop (tree expr)
{
  STRIP_NOPS (expr);

  return ((TREE_CODE (expr) == REAL_CST
	   && REAL_VALUES_EQUAL (TREE_REAL_CST (expr), dconst2))
	  || (TREE_CODE (expr) == COMPLEX_CST
	      && real_twop (TREE_REALPART (expr))
	      && real_zerop (TREE_IMAGPART (expr))));
}

/* Return 1 if EXPR is the real constant minus one.  */

int
real_minus_onep (tree expr)
{
  STRIP_NOPS (expr);

  return ((TREE_CODE (expr) == REAL_CST
	   && REAL_VALUES_EQUAL (TREE_REAL_CST (expr), dconstm1))
	  || (TREE_CODE (expr) == COMPLEX_CST
	      && real_minus_onep (TREE_REALPART (expr))
	      && real_zerop (TREE_IMAGPART (expr))));
}

/* Nonzero if EXP is a constant or a cast of a constant.  */

int
really_constant_p (tree exp)
{
  /* This is not quite the same as STRIP_NOPS.  It does more.  */
  while (TREE_CODE (exp) == NOP_EXPR
	 || TREE_CODE (exp) == CONVERT_EXPR
	 || TREE_CODE (exp) == NON_LVALUE_EXPR)
    exp = TREE_OPERAND (exp, 0);
  return TREE_CONSTANT (exp);
}

/* Return first list element whose TREE_VALUE is ELEM.
   Return 0 if ELEM is not in LIST.  */

tree
value_member (tree elem, tree list)
{
  while (list)
    {
      if (elem == TREE_VALUE (list))
	return list;
      list = TREE_CHAIN (list);
    }
  return NULL_TREE;
}

/* Return first list element whose TREE_PURPOSE is ELEM.
   Return 0 if ELEM is not in LIST.  */

tree
purpose_member (tree elem, tree list)
{
  while (list)
    {
      if (elem == TREE_PURPOSE (list))
	return list;
      list = TREE_CHAIN (list);
    }
  return NULL_TREE;
}

/* Return nonzero if ELEM is part of the chain CHAIN.  */

int
chain_member (tree elem, tree chain)
{
  while (chain)
    {
      if (elem == chain)
	return 1;
      chain = TREE_CHAIN (chain);
    }

  return 0;
}

/* Return the length of a chain of nodes chained through TREE_CHAIN.
   We expect a null pointer to mark the end of the chain.
   This is the Lisp primitive `length'.  */

int
list_length (tree t)
{
  tree p = t;
#ifdef ENABLE_TREE_CHECKING
  tree q = t;
#endif
  int len = 0;

  while (p)
    {
      p = TREE_CHAIN (p);
#ifdef ENABLE_TREE_CHECKING
      if (len % 2)
	q = TREE_CHAIN (q);
      gcc_assert (p != q);
#endif
      len++;
    }

  return len;
}

/* Returns the number of FIELD_DECLs in TYPE.  */

int
fields_length (tree type)
{
  tree t = TYPE_FIELDS (type);
  int count = 0;

  for (; t; t = TREE_CHAIN (t))
    if (TREE_CODE (t) == FIELD_DECL)
      ++count;

  return count;
}

/* Concatenate two chains of nodes (chained through TREE_CHAIN)
   by modifying the last node in chain 1 to point to chain 2.
   This is the Lisp primitive `nconc'.  */

tree
chainon (tree op1, tree op2)
{
  tree t1;

  if (!op1)
    return op2;
  if (!op2)
    return op1;

  for (t1 = op1; TREE_CHAIN (t1); t1 = TREE_CHAIN (t1))
    continue;
  TREE_CHAIN (t1) = op2;

#ifdef ENABLE_TREE_CHECKING
  {
    tree t2;
    for (t2 = op2; t2; t2 = TREE_CHAIN (t2))
      gcc_assert (t2 != t1);
  }
#endif

  return op1;
}

/* Return the last node in a chain of nodes (chained through TREE_CHAIN).  */

tree
tree_last (tree chain)
{
  tree next;
  if (chain)
    while ((next = TREE_CHAIN (chain)))
      chain = next;
  return chain;
}

/* Reverse the order of elements in the chain T,
   and return the new head of the chain (old last element).  */

tree
nreverse (tree t)
{
  tree prev = 0, decl, next;
  for (decl = t; decl; decl = next)
    {
      next = TREE_CHAIN (decl);
      TREE_CHAIN (decl) = prev;
      prev = decl;
    }
  return prev;
}

/* Return a newly created TREE_LIST node whose
   purpose and value fields are PARM and VALUE.  */

tree
build_tree_list_stat (tree parm, tree value MEM_STAT_DECL)
{
  tree t = make_node_stat (TREE_LIST PASS_MEM_STAT);
  TREE_PURPOSE (t) = parm;
  TREE_VALUE (t) = value;
  return t;
}

/* Return a newly created TREE_LIST node whose
   purpose and value fields are PURPOSE and VALUE
   and whose TREE_CHAIN is CHAIN.  */

tree
tree_cons_stat (tree purpose, tree value, tree chain MEM_STAT_DECL)
{
  tree node;

  node = ggc_alloc_zone_pass_stat (sizeof (struct tree_list), &tree_zone);

  memset (node, 0, sizeof (struct tree_common));

#ifdef GATHER_STATISTICS
  tree_node_counts[(int) x_kind]++;
  tree_node_sizes[(int) x_kind] += sizeof (struct tree_list);
#endif

  TREE_SET_CODE (node, TREE_LIST);
  TREE_CHAIN (node) = chain;
  TREE_PURPOSE (node) = purpose;
  TREE_VALUE (node) = value;
  return node;
}


/* Return the size nominally occupied by an object of type TYPE
   when it resides in memory.  The value is measured in units of bytes,
   and its data type is that normally used for type sizes
   (which is the first type created by make_signed_type or
   make_unsigned_type).  */

tree
size_in_bytes (tree type)
{
  tree t;

  if (type == error_mark_node)
    return integer_zero_node;

  type = TYPE_MAIN_VARIANT (type);
  t = TYPE_SIZE_UNIT (type);

  if (t == 0)
    {
      lang_hooks.types.incomplete_type_error (NULL_TREE, type);
      return size_zero_node;
    }

  if (TREE_CODE (t) == INTEGER_CST)
    t = force_fit_type (t, 0, false, false);

  return t;
}

/* Return the size of TYPE (in bytes) as a wide integer
   or return -1 if the size can vary or is larger than an integer.  */

HOST_WIDE_INT
int_size_in_bytes (tree type)
{
  tree t;

  if (type == error_mark_node)
    return 0;

  type = TYPE_MAIN_VARIANT (type);
  t = TYPE_SIZE_UNIT (type);
  if (t == 0
      || TREE_CODE (t) != INTEGER_CST
      || TREE_INT_CST_HIGH (t) != 0
      /* If the result would appear negative, it's too big to represent.  */
      || (HOST_WIDE_INT) TREE_INT_CST_LOW (t) < 0)
    return -1;

  return TREE_INT_CST_LOW (t);
}

/* Return the maximum size of TYPE (in bytes) as a wide integer
   or return -1 if the size can vary or is larger than an integer.  */

HOST_WIDE_INT
max_int_size_in_bytes (tree type)
{
  HOST_WIDE_INT size = -1;
  tree size_tree;

  /* If this is an array type, check for a possible MAX_SIZE attached.  */

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      size_tree = TYPE_ARRAY_MAX_SIZE (type);

      if (size_tree && host_integerp (size_tree, 1))
	size = tree_low_cst (size_tree, 1);
    }

  /* If we still haven't been able to get a size, see if the language
     can compute a maximum size.  */

  if (size == -1)
    {
      size_tree = lang_hooks.types.max_size (type);

      if (size_tree && host_integerp (size_tree, 1))
	size = tree_low_cst (size_tree, 1);
    }

  return size;
}

/* Return the bit position of FIELD, in bits from the start of the record.
   This is a tree of type bitsizetype.  */

tree
bit_position (tree field)
{
  return bit_from_pos (DECL_FIELD_OFFSET (field),
		       DECL_FIELD_BIT_OFFSET (field));
}

/* Likewise, but return as an integer.  It must be representable in
   that way (since it could be a signed value, we don't have the
   option of returning -1 like int_size_in_byte can.  */

HOST_WIDE_INT
int_bit_position (tree field)
{
  return tree_low_cst (bit_position (field), 0);
}

/* Return the byte position of FIELD, in bytes from the start of the record.
   This is a tree of type sizetype.  */

tree
byte_position (tree field)
{
  return byte_from_pos (DECL_FIELD_OFFSET (field),
			DECL_FIELD_BIT_OFFSET (field));
}

/* Likewise, but return as an integer.  It must be representable in
   that way (since it could be a signed value, we don't have the
   option of returning -1 like int_size_in_byte can.  */

HOST_WIDE_INT
int_byte_position (tree field)
{
  return tree_low_cst (byte_position (field), 0);
}

/* Return the strictest alignment, in bits, that T is known to have.  */

unsigned int
expr_align (tree t)
{
  unsigned int align0, align1;

  switch (TREE_CODE (t))
    {
    case NOP_EXPR:  case CONVERT_EXPR:  case NON_LVALUE_EXPR:
      /* If we have conversions, we know that the alignment of the
	 object must meet each of the alignments of the types.  */
      align0 = expr_align (TREE_OPERAND (t, 0));
      align1 = TYPE_ALIGN (TREE_TYPE (t));
      return MAX (align0, align1);

    case SAVE_EXPR:         case COMPOUND_EXPR:       case MODIFY_EXPR:
    case INIT_EXPR:         case TARGET_EXPR:         case WITH_CLEANUP_EXPR:
    case CLEANUP_POINT_EXPR:
      /* These don't change the alignment of an object.  */
      return expr_align (TREE_OPERAND (t, 0));

    case COND_EXPR:
      /* The best we can do is say that the alignment is the least aligned
	 of the two arms.  */
      align0 = expr_align (TREE_OPERAND (t, 1));
      align1 = expr_align (TREE_OPERAND (t, 2));
      return MIN (align0, align1);

      /* FIXME: LABEL_DECL and CONST_DECL never have DECL_ALIGN set
	 meaningfully, it's always 1.  */
    case LABEL_DECL:     case CONST_DECL:
    case VAR_DECL:       case PARM_DECL:   case RESULT_DECL:
    case FUNCTION_DECL:
      gcc_assert (DECL_ALIGN (t) != 0);
      return DECL_ALIGN (t);

    default:
      break;
    }

  /* Otherwise take the alignment from that of the type.  */
  return TYPE_ALIGN (TREE_TYPE (t));
}

/* Return, as a tree node, the number of elements for TYPE (which is an
   ARRAY_TYPE) minus one. This counts only elements of the top array.  */

tree
array_type_nelts (tree type)
{
  tree index_type, min, max;

  /* If they did it with unspecified bounds, then we should have already
     given an error about it before we got here.  */
  if (! TYPE_DOMAIN (type))
    return error_mark_node;

  index_type = TYPE_DOMAIN (type);
  min = TYPE_MIN_VALUE (index_type);
  max = TYPE_MAX_VALUE (index_type);

  return (integer_zerop (min)
	  ? max
	  : fold_build2 (MINUS_EXPR, TREE_TYPE (max), max, min));
}

/* If arg is static -- a reference to an object in static storage -- then
   return the object.  This is not the same as the C meaning of `static'.
   If arg isn't static, return NULL.  */

tree
staticp (tree arg)
{
  switch (TREE_CODE (arg))
    {
    case FUNCTION_DECL:
      /* Nested functions are static, even though taking their address will
	 involve a trampoline as we unnest the nested function and create
	 the trampoline on the tree level.  */
      return arg;

    case VAR_DECL:
      return ((TREE_STATIC (arg) || DECL_EXTERNAL (arg))
	      && ! DECL_THREAD_LOCAL_P (arg)
	      && ! DECL_DLLIMPORT_P (arg)
	      ? arg : NULL);

    case CONST_DECL:
      return ((TREE_STATIC (arg) || DECL_EXTERNAL (arg))
	      ? arg : NULL);

    case CONSTRUCTOR:
      return TREE_STATIC (arg) ? arg : NULL;

    case LABEL_DECL:
    case STRING_CST:
      return arg;

    case COMPONENT_REF:
      /* If the thing being referenced is not a field, then it is
	 something language specific.  */
      if (TREE_CODE (TREE_OPERAND (arg, 1)) != FIELD_DECL)
	return (*lang_hooks.staticp) (arg);

      /* If we are referencing a bitfield, we can't evaluate an
	 ADDR_EXPR at compile time and so it isn't a constant.  */
      if (DECL_BIT_FIELD (TREE_OPERAND (arg, 1)))
	return NULL;

      return staticp (TREE_OPERAND (arg, 0));

    case BIT_FIELD_REF:
      return NULL;

    case MISALIGNED_INDIRECT_REF:
    case ALIGN_INDIRECT_REF:
    case INDIRECT_REF:
      return TREE_CONSTANT (TREE_OPERAND (arg, 0)) ? arg : NULL;

    case ARRAY_REF:
    case ARRAY_RANGE_REF:
      if (TREE_CODE (TYPE_SIZE (TREE_TYPE (arg))) == INTEGER_CST
	  && TREE_CODE (TREE_OPERAND (arg, 1)) == INTEGER_CST)
	return staticp (TREE_OPERAND (arg, 0));
      else
	return false;

    default:
      if ((unsigned int) TREE_CODE (arg)
	  >= (unsigned int) LAST_AND_UNUSED_TREE_CODE)
	return lang_hooks.staticp (arg);
      else
	return NULL;
    }
}

/* Wrap a SAVE_EXPR around EXPR, if appropriate.
   Do this to any expression which may be used in more than one place,
   but must be evaluated only once.

   Normally, expand_expr would reevaluate the expression each time.
   Calling save_expr produces something that is evaluated and recorded
   the first time expand_expr is called on it.  Subsequent calls to
   expand_expr just reuse the recorded value.

   The call to expand_expr that generates code that actually computes
   the value is the first call *at compile time*.  Subsequent calls
   *at compile time* generate code to use the saved value.
   This produces correct result provided that *at run time* control
   always flows through the insns made by the first expand_expr
   before reaching the other places where the save_expr was evaluated.
   You, the caller of save_expr, must make sure this is so.

   Constants, and certain read-only nodes, are returned with no
   SAVE_EXPR because that is safe.  Expressions containing placeholders
   are not touched; see tree.def for an explanation of what these
   are used for.  */

tree
save_expr (tree expr)
{
  tree t = fold (expr);
  tree inner;

  /* If the tree evaluates to a constant, then we don't want to hide that
     fact (i.e. this allows further folding, and direct checks for constants).
     However, a read-only object that has side effects cannot be bypassed.
     Since it is no problem to reevaluate literals, we just return the
     literal node.  */
  inner = skip_simple_arithmetic (t);

  if (TREE_INVARIANT (inner)
      || (TREE_READONLY (inner) && ! TREE_SIDE_EFFECTS (inner))
      || TREE_CODE (inner) == SAVE_EXPR
      || TREE_CODE (inner) == ERROR_MARK)
    return t;

  /* If INNER contains a PLACEHOLDER_EXPR, we must evaluate it each time, since
     it means that the size or offset of some field of an object depends on
     the value within another field.

     Note that it must not be the case that T contains both a PLACEHOLDER_EXPR
     and some variable since it would then need to be both evaluated once and
     evaluated more than once.  Front-ends must assure this case cannot
     happen by surrounding any such subexpressions in their own SAVE_EXPR
     and forcing evaluation at the proper time.  */
  if (contains_placeholder_p (inner))
    return t;

  t = build1 (SAVE_EXPR, TREE_TYPE (expr), t);

  /* This expression might be placed ahead of a jump to ensure that the
     value was computed on both sides of the jump.  So make sure it isn't
     eliminated as dead.  */
  TREE_SIDE_EFFECTS (t) = 1;
  TREE_INVARIANT (t) = 1;
  return t;
}

/* Look inside EXPR and into any simple arithmetic operations.  Return
   the innermost non-arithmetic node.  */

tree
skip_simple_arithmetic (tree expr)
{
  tree inner;

  /* We don't care about whether this can be used as an lvalue in this
     context.  */
  while (TREE_CODE (expr) == NON_LVALUE_EXPR)
    expr = TREE_OPERAND (expr, 0);

  /* If we have simple operations applied to a SAVE_EXPR or to a SAVE_EXPR and
     a constant, it will be more efficient to not make another SAVE_EXPR since
     it will allow better simplification and GCSE will be able to merge the
     computations if they actually occur.  */
  inner = expr;
  while (1)
    {
      if (UNARY_CLASS_P (inner))
	inner = TREE_OPERAND (inner, 0);
      else if (BINARY_CLASS_P (inner))
	{
	  if (TREE_INVARIANT (TREE_OPERAND (inner, 1)))
	    inner = TREE_OPERAND (inner, 0);
	  else if (TREE_INVARIANT (TREE_OPERAND (inner, 0)))
	    inner = TREE_OPERAND (inner, 1);
	  else
	    break;
	}
      else
	break;
    }

  return inner;
}

/* Return which tree structure is used by T.  */

enum tree_node_structure_enum
tree_node_structure (tree t)
{
  enum tree_code code = TREE_CODE (t);

  switch (TREE_CODE_CLASS (code))
    {      
    case tcc_declaration:
      {
	switch (code)
	  {
	  case FIELD_DECL:
	    return TS_FIELD_DECL;
	  case PARM_DECL:
	    return TS_PARM_DECL;
	  case VAR_DECL:
	    return TS_VAR_DECL;
	  case LABEL_DECL:
	    return TS_LABEL_DECL;
	  case RESULT_DECL:
	    return TS_RESULT_DECL;
	  case CONST_DECL:
	    return TS_CONST_DECL;
	  case TYPE_DECL:
	    return TS_TYPE_DECL;
	  case FUNCTION_DECL:
	    return TS_FUNCTION_DECL;
	  case SYMBOL_MEMORY_TAG:
	  case NAME_MEMORY_TAG:
	  case STRUCT_FIELD_TAG:
	    return TS_MEMORY_TAG;
	  default:
	    return TS_DECL_NON_COMMON;
	  }
      }
    case tcc_type:
      return TS_TYPE;
    case tcc_reference:
    case tcc_comparison:
    case tcc_unary:
    case tcc_binary:
    case tcc_expression:
    case tcc_statement:
      return TS_EXP;
    default:  /* tcc_constant and tcc_exceptional */
      break;
    }
  switch (code)
    {
      /* tcc_constant cases.  */
    case INTEGER_CST:		return TS_INT_CST;
    case REAL_CST:		return TS_REAL_CST;
    case COMPLEX_CST:		return TS_COMPLEX;
    case VECTOR_CST:		return TS_VECTOR;
    case STRING_CST:		return TS_STRING;
      /* tcc_exceptional cases.  */
    case ERROR_MARK:		return TS_COMMON;
    case IDENTIFIER_NODE:	return TS_IDENTIFIER;
    case TREE_LIST:		return TS_LIST;
    case TREE_VEC:		return TS_VEC;
    case PHI_NODE:		return TS_PHI_NODE;
    case SSA_NAME:		return TS_SSA_NAME;
    case PLACEHOLDER_EXPR:	return TS_COMMON;
    case STATEMENT_LIST:	return TS_STATEMENT_LIST;
    case BLOCK:			return TS_BLOCK;
    case CONSTRUCTOR:		return TS_CONSTRUCTOR;
    case TREE_BINFO:		return TS_BINFO;
    case VALUE_HANDLE:		return TS_VALUE_HANDLE;
    case OMP_CLAUSE:		return TS_OMP_CLAUSE;

    default:
      gcc_unreachable ();
    }
}

/* Return 1 if EXP contains a PLACEHOLDER_EXPR; i.e., if it represents a size
   or offset that depends on a field within a record.  */

bool
contains_placeholder_p (tree exp)
{
  enum tree_code code;

  if (!exp)
    return 0;

  code = TREE_CODE (exp);
  if (code == PLACEHOLDER_EXPR)
    return 1;

  switch (TREE_CODE_CLASS (code))
    {
    case tcc_reference:
      /* Don't look at any PLACEHOLDER_EXPRs that might be in index or bit
	 position computations since they will be converted into a
	 WITH_RECORD_EXPR involving the reference, which will assume
	 here will be valid.  */
      return CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 0));

    case tcc_exceptional:
      if (code == TREE_LIST)
	return (CONTAINS_PLACEHOLDER_P (TREE_VALUE (exp))
		|| CONTAINS_PLACEHOLDER_P (TREE_CHAIN (exp)));
      break;

    case tcc_unary:
    case tcc_binary:
    case tcc_comparison:
    case tcc_expression:
      switch (code)
	{
	case COMPOUND_EXPR:
	  /* Ignoring the first operand isn't quite right, but works best.  */
	  return CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 1));

	case COND_EXPR:
	  return (CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 0))
		  || CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 1))
		  || CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 2)));

	case CALL_EXPR:
	  return CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 1));

	default:
	  break;
	}

      switch (TREE_CODE_LENGTH (code))
	{
	case 1:
	  return CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 0));
	case 2:
	  return (CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 0))
		  || CONTAINS_PLACEHOLDER_P (TREE_OPERAND (exp, 1)));
	default:
	  return 0;
	}

    default:
      return 0;
    }
  return 0;
}

/* Return true if any part of the computation of TYPE involves a
   PLACEHOLDER_EXPR.  This includes size, bounds, qualifiers
   (for QUAL_UNION_TYPE) and field positions.  */

static bool
type_contains_placeholder_1 (tree type)
{
  /* If the size contains a placeholder or the parent type (component type in
     the case of arrays) type involves a placeholder, this type does.  */
  if (CONTAINS_PLACEHOLDER_P (TYPE_SIZE (type))
      || CONTAINS_PLACEHOLDER_P (TYPE_SIZE_UNIT (type))
      || (TREE_TYPE (type) != 0
	  && type_contains_placeholder_p (TREE_TYPE (type))))
    return true;

  /* Now do type-specific checks.  Note that the last part of the check above
     greatly limits what we have to do below.  */
  switch (TREE_CODE (type))
    {
    case VOID_TYPE:
    case COMPLEX_TYPE:
    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
    case POINTER_TYPE:
    case OFFSET_TYPE:
    case REFERENCE_TYPE:
    case METHOD_TYPE:
    case FUNCTION_TYPE:
    case VECTOR_TYPE:
      return false;

    case INTEGER_TYPE:
    case REAL_TYPE:
      /* Here we just check the bounds.  */
      return (CONTAINS_PLACEHOLDER_P (TYPE_MIN_VALUE (type))
	      || CONTAINS_PLACEHOLDER_P (TYPE_MAX_VALUE (type)));

    case ARRAY_TYPE:
      /* We're already checked the component type (TREE_TYPE), so just check
	 the index type.  */
      return type_contains_placeholder_p (TYPE_DOMAIN (type));

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      {
	tree field;

	for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	  if (TREE_CODE (field) == FIELD_DECL
	      && (CONTAINS_PLACEHOLDER_P (DECL_FIELD_OFFSET (field))
		  || (TREE_CODE (type) == QUAL_UNION_TYPE
		      && CONTAINS_PLACEHOLDER_P (DECL_QUALIFIER (field)))
		  || type_contains_placeholder_p (TREE_TYPE (field))))
	    return true;

	return false;
      }

    default:
      gcc_unreachable ();
    }
}

bool
type_contains_placeholder_p (tree type)
{
  bool result;

  /* If the contains_placeholder_bits field has been initialized,
     then we know the answer.  */
  if (TYPE_CONTAINS_PLACEHOLDER_INTERNAL (type) > 0)
    return TYPE_CONTAINS_PLACEHOLDER_INTERNAL (type) - 1;

  /* Indicate that we've seen this type node, and the answer is false.
     This is what we want to return if we run into recursion via fields.  */
  TYPE_CONTAINS_PLACEHOLDER_INTERNAL (type) = 1;

  /* Compute the real value.  */
  result = type_contains_placeholder_1 (type);

  /* Store the real value.  */
  TYPE_CONTAINS_PLACEHOLDER_INTERNAL (type) = result + 1;

  return result;
}

/* Given a tree EXP, a FIELD_DECL F, and a replacement value R,
   return a tree with all occurrences of references to F in a
   PLACEHOLDER_EXPR replaced by R.   Note that we assume here that EXP
   contains only arithmetic expressions or a CALL_EXPR with a
   PLACEHOLDER_EXPR occurring only in its arglist.  */

tree
substitute_in_expr (tree exp, tree f, tree r)
{
  enum tree_code code = TREE_CODE (exp);
  tree op0, op1, op2, op3;
  tree new;
  tree inner;

  /* We handle TREE_LIST and COMPONENT_REF separately.  */
  if (code == TREE_LIST)
    {
      op0 = SUBSTITUTE_IN_EXPR (TREE_CHAIN (exp), f, r);
      op1 = SUBSTITUTE_IN_EXPR (TREE_VALUE (exp), f, r);
      if (op0 == TREE_CHAIN (exp) && op1 == TREE_VALUE (exp))
	return exp;

      return tree_cons (TREE_PURPOSE (exp), op1, op0);
    }
  else if (code == COMPONENT_REF)
   {
     /* If this expression is getting a value from a PLACEHOLDER_EXPR
	and it is the right field, replace it with R.  */
     for (inner = TREE_OPERAND (exp, 0);
	  REFERENCE_CLASS_P (inner);
	  inner = TREE_OPERAND (inner, 0))
       ;
     if (TREE_CODE (inner) == PLACEHOLDER_EXPR
	 && TREE_OPERAND (exp, 1) == f)
       return r;

     /* If this expression hasn't been completed let, leave it alone.  */
     if (TREE_CODE (inner) == PLACEHOLDER_EXPR && TREE_TYPE (inner) == 0)
       return exp;

     op0 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 0), f, r);
     if (op0 == TREE_OPERAND (exp, 0))
       return exp;

     new = fold_build3 (COMPONENT_REF, TREE_TYPE (exp),
			op0, TREE_OPERAND (exp, 1), NULL_TREE);
   }
  else
    switch (TREE_CODE_CLASS (code))
      {
      case tcc_constant:
      case tcc_declaration:
	return exp;

      case tcc_exceptional:
      case tcc_unary:
      case tcc_binary:
      case tcc_comparison:
      case tcc_expression:
      case tcc_reference:
	switch (TREE_CODE_LENGTH (code))
	  {
	  case 0:
	    return exp;

	  case 1:
	    op0 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 0), f, r);
	    if (op0 == TREE_OPERAND (exp, 0))
	      return exp;

	    new = fold_build1 (code, TREE_TYPE (exp), op0);
	    break;

	  case 2:
	    op0 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 0), f, r);
	    op1 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 1), f, r);

	    if (op0 == TREE_OPERAND (exp, 0) && op1 == TREE_OPERAND (exp, 1))
	      return exp;

	    new = fold_build2 (code, TREE_TYPE (exp), op0, op1);
	    break;

	  case 3:
	    op0 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 0), f, r);
	    op1 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 1), f, r);
	    op2 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 2), f, r);

	    if (op0 == TREE_OPERAND (exp, 0) && op1 == TREE_OPERAND (exp, 1)
		&& op2 == TREE_OPERAND (exp, 2))
	      return exp;

	    new = fold_build3 (code, TREE_TYPE (exp), op0, op1, op2);
	    break;

	  case 4:
	    op0 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 0), f, r);
	    op1 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 1), f, r);
	    op2 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 2), f, r);
	    op3 = SUBSTITUTE_IN_EXPR (TREE_OPERAND (exp, 3), f, r);

	    if (op0 == TREE_OPERAND (exp, 0) && op1 == TREE_OPERAND (exp, 1)
		&& op2 == TREE_OPERAND (exp, 2)
		&& op3 == TREE_OPERAND (exp, 3))
	      return exp;

	    new = fold (build4 (code, TREE_TYPE (exp), op0, op1, op2, op3));
	    break;

	  default:
	    gcc_unreachable ();
	  }
	break;

      default:
	gcc_unreachable ();
      }

  TREE_READONLY (new) = TREE_READONLY (exp);
  return new;
}

/* Similar, but look for a PLACEHOLDER_EXPR in EXP and find a replacement
   for it within OBJ, a tree that is an object or a chain of references.  */

tree
substitute_placeholder_in_expr (tree exp, tree obj)
{
  enum tree_code code = TREE_CODE (exp);
  tree op0, op1, op2, op3;

  /* If this is a PLACEHOLDER_EXPR, see if we find a corresponding type
     in the chain of OBJ.  */
  if (code == PLACEHOLDER_EXPR)
    {
      tree need_type = TYPE_MAIN_VARIANT (TREE_TYPE (exp));
      tree elt;

      for (elt = obj; elt != 0;
	   elt = ((TREE_CODE (elt) == COMPOUND_EXPR
		   || TREE_CODE (elt) == COND_EXPR)
		  ? TREE_OPERAND (elt, 1)
		  : (REFERENCE_CLASS_P (elt)
		     || UNARY_CLASS_P (elt)
		     || BINARY_CLASS_P (elt)
		     || EXPRESSION_CLASS_P (elt))
		  ? TREE_OPERAND (elt, 0) : 0))
	if (TYPE_MAIN_VARIANT (TREE_TYPE (elt)) == need_type)
	  return elt;

      for (elt = obj; elt != 0;
	   elt = ((TREE_CODE (elt) == COMPOUND_EXPR
		   || TREE_CODE (elt) == COND_EXPR)
		  ? TREE_OPERAND (elt, 1)
		  : (REFERENCE_CLASS_P (elt)
		     || UNARY_CLASS_P (elt)
		     || BINARY_CLASS_P (elt)
		     || EXPRESSION_CLASS_P (elt))
		  ? TREE_OPERAND (elt, 0) : 0))
	if (POINTER_TYPE_P (TREE_TYPE (elt))
	    && (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (elt)))
		== need_type))
	  return fold_build1 (INDIRECT_REF, need_type, elt);

      /* If we didn't find it, return the original PLACEHOLDER_EXPR.  If it
	 survives until RTL generation, there will be an error.  */
      return exp;
    }

  /* TREE_LIST is special because we need to look at TREE_VALUE
     and TREE_CHAIN, not TREE_OPERANDS.  */
  else if (code == TREE_LIST)
    {
      op0 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_CHAIN (exp), obj);
      op1 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_VALUE (exp), obj);
      if (op0 == TREE_CHAIN (exp) && op1 == TREE_VALUE (exp))
	return exp;

      return tree_cons (TREE_PURPOSE (exp), op1, op0);
    }
  else
    switch (TREE_CODE_CLASS (code))
      {
      case tcc_constant:
      case tcc_declaration:
	return exp;

      case tcc_exceptional:
      case tcc_unary:
      case tcc_binary:
      case tcc_comparison:
      case tcc_expression:
      case tcc_reference:
      case tcc_statement:
	switch (TREE_CODE_LENGTH (code))
	  {
	  case 0:
	    return exp;

	  case 1:
	    op0 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 0), obj);
	    if (op0 == TREE_OPERAND (exp, 0))
	      return exp;
	    else
	      return fold_build1 (code, TREE_TYPE (exp), op0);

	  case 2:
	    op0 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 0), obj);
	    op1 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 1), obj);

	    if (op0 == TREE_OPERAND (exp, 0) && op1 == TREE_OPERAND (exp, 1))
	      return exp;
	    else
	      return fold_build2 (code, TREE_TYPE (exp), op0, op1);

	  case 3:
	    op0 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 0), obj);
	    op1 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 1), obj);
	    op2 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 2), obj);

	    if (op0 == TREE_OPERAND (exp, 0) && op1 == TREE_OPERAND (exp, 1)
		&& op2 == TREE_OPERAND (exp, 2))
	      return exp;
	    else
	      return fold_build3 (code, TREE_TYPE (exp), op0, op1, op2);

	  case 4:
	    op0 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 0), obj);
	    op1 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 1), obj);
	    op2 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 2), obj);
	    op3 = SUBSTITUTE_PLACEHOLDER_IN_EXPR (TREE_OPERAND (exp, 3), obj);

	    if (op0 == TREE_OPERAND (exp, 0) && op1 == TREE_OPERAND (exp, 1)
		&& op2 == TREE_OPERAND (exp, 2)
		&& op3 == TREE_OPERAND (exp, 3))
	      return exp;
	    else
	      return fold (build4 (code, TREE_TYPE (exp), op0, op1, op2, op3));

	  default:
	    gcc_unreachable ();
	  }
	break;

      default:
	gcc_unreachable ();
      }
}

/* Stabilize a reference so that we can use it any number of times
   without causing its operands to be evaluated more than once.
   Returns the stabilized reference.  This works by means of save_expr,
   so see the caveats in the comments about save_expr.

   Also allows conversion expressions whose operands are references.
   Any other kind of expression is returned unchanged.  */

tree
stabilize_reference (tree ref)
{
  tree result;
  enum tree_code code = TREE_CODE (ref);

  switch (code)
    {
    case VAR_DECL:
    case PARM_DECL:
    case RESULT_DECL:
      /* No action is needed in this case.  */
      return ref;

    case NOP_EXPR:
    case CONVERT_EXPR:
    case FLOAT_EXPR:
    case FIX_TRUNC_EXPR:
    case FIX_FLOOR_EXPR:
    case FIX_ROUND_EXPR:
    case FIX_CEIL_EXPR:
      result = build_nt (code, stabilize_reference (TREE_OPERAND (ref, 0)));
      break;

    case INDIRECT_REF:
      result = build_nt (INDIRECT_REF,
			 stabilize_reference_1 (TREE_OPERAND (ref, 0)));
      break;

    case COMPONENT_REF:
      result = build_nt (COMPONENT_REF,
			 stabilize_reference (TREE_OPERAND (ref, 0)),
			 TREE_OPERAND (ref, 1), NULL_TREE);
      break;

    case BIT_FIELD_REF:
      result = build_nt (BIT_FIELD_REF,
			 stabilize_reference (TREE_OPERAND (ref, 0)),
			 stabilize_reference_1 (TREE_OPERAND (ref, 1)),
			 stabilize_reference_1 (TREE_OPERAND (ref, 2)));
      break;

    case ARRAY_REF:
      result = build_nt (ARRAY_REF,
			 stabilize_reference (TREE_OPERAND (ref, 0)),
			 stabilize_reference_1 (TREE_OPERAND (ref, 1)),
			 TREE_OPERAND (ref, 2), TREE_OPERAND (ref, 3));
      break;

    case ARRAY_RANGE_REF:
      result = build_nt (ARRAY_RANGE_REF,
			 stabilize_reference (TREE_OPERAND (ref, 0)),
			 stabilize_reference_1 (TREE_OPERAND (ref, 1)),
			 TREE_OPERAND (ref, 2), TREE_OPERAND (ref, 3));
      break;

    case COMPOUND_EXPR:
      /* We cannot wrap the first expression in a SAVE_EXPR, as then
	 it wouldn't be ignored.  This matters when dealing with
	 volatiles.  */
      return stabilize_reference_1 (ref);

      /* If arg isn't a kind of lvalue we recognize, make no change.
	 Caller should recognize the error for an invalid lvalue.  */
    default:
      return ref;

    case ERROR_MARK:
      return error_mark_node;
    }

  TREE_TYPE (result) = TREE_TYPE (ref);
  TREE_READONLY (result) = TREE_READONLY (ref);
  TREE_SIDE_EFFECTS (result) = TREE_SIDE_EFFECTS (ref);
  TREE_THIS_VOLATILE (result) = TREE_THIS_VOLATILE (ref);

  return result;
}

/* Subroutine of stabilize_reference; this is called for subtrees of
   references.  Any expression with side-effects must be put in a SAVE_EXPR
   to ensure that it is only evaluated once.

   We don't put SAVE_EXPR nodes around everything, because assigning very
   simple expressions to temporaries causes us to miss good opportunities
   for optimizations.  Among other things, the opportunity to fold in the
   addition of a constant into an addressing mode often gets lost, e.g.
   "y[i+1] += x;".  In general, we take the approach that we should not make
   an assignment unless we are forced into it - i.e., that any non-side effect
   operator should be allowed, and that cse should take care of coalescing
   multiple utterances of the same expression should that prove fruitful.  */

tree
stabilize_reference_1 (tree e)
{
  tree result;
  enum tree_code code = TREE_CODE (e);

  /* We cannot ignore const expressions because it might be a reference
     to a const array but whose index contains side-effects.  But we can
     ignore things that are actual constant or that already have been
     handled by this function.  */

  if (TREE_INVARIANT (e))
    return e;

  switch (TREE_CODE_CLASS (code))
    {
    case tcc_exceptional:
    case tcc_type:
    case tcc_declaration:
    case tcc_comparison:
    case tcc_statement:
    case tcc_expression:
    case tcc_reference:
      /* If the expression has side-effects, then encase it in a SAVE_EXPR
	 so that it will only be evaluated once.  */
      /* The reference (r) and comparison (<) classes could be handled as
	 below, but it is generally faster to only evaluate them once.  */
      if (TREE_SIDE_EFFECTS (e))
	return save_expr (e);
      return e;

    case tcc_constant:
      /* Constants need no processing.  In fact, we should never reach
	 here.  */
      return e;

    case tcc_binary:
      /* Division is slow and tends to be compiled with jumps,
	 especially the division by powers of 2 that is often
	 found inside of an array reference.  So do it just once.  */
      if (code == TRUNC_DIV_EXPR || code == TRUNC_MOD_EXPR
	  || code == FLOOR_DIV_EXPR || code == FLOOR_MOD_EXPR
	  || code == CEIL_DIV_EXPR || code == CEIL_MOD_EXPR
	  || code == ROUND_DIV_EXPR || code == ROUND_MOD_EXPR)
	return save_expr (e);
      /* Recursively stabilize each operand.  */
      result = build_nt (code, stabilize_reference_1 (TREE_OPERAND (e, 0)),
			 stabilize_reference_1 (TREE_OPERAND (e, 1)));
      break;

    case tcc_unary:
      /* Recursively stabilize each operand.  */
      result = build_nt (code, stabilize_reference_1 (TREE_OPERAND (e, 0)));
      break;

    default:
      gcc_unreachable ();
    }

  TREE_TYPE (result) = TREE_TYPE (e);
  TREE_READONLY (result) = TREE_READONLY (e);
  TREE_SIDE_EFFECTS (result) = TREE_SIDE_EFFECTS (e);
  TREE_THIS_VOLATILE (result) = TREE_THIS_VOLATILE (e);
  TREE_INVARIANT (result) = 1;

  return result;
}

/* Low-level constructors for expressions.  */

/* A helper function for build1 and constant folders.  Set TREE_CONSTANT,
   TREE_INVARIANT, and TREE_SIDE_EFFECTS for an ADDR_EXPR.  */

void
recompute_tree_invariant_for_addr_expr (tree t)
{
  tree node;
  bool tc = true, ti = true, se = false;

  /* We started out assuming this address is both invariant and constant, but
     does not have side effects.  Now go down any handled components and see if
     any of them involve offsets that are either non-constant or non-invariant.
     Also check for side-effects.

     ??? Note that this code makes no attempt to deal with the case where
     taking the address of something causes a copy due to misalignment.  */

#define UPDATE_TITCSE(NODE)  \
do { tree _node = (NODE); \
     if (_node && !TREE_INVARIANT (_node)) ti = false; \
     if (_node && !TREE_CONSTANT (_node)) tc = false; \
     if (_node && TREE_SIDE_EFFECTS (_node)) se = true; } while (0)

  for (node = TREE_OPERAND (t, 0); handled_component_p (node);
       node = TREE_OPERAND (node, 0))
    {
      /* If the first operand doesn't have an ARRAY_TYPE, this is a bogus
	 array reference (probably made temporarily by the G++ front end),
	 so ignore all the operands.  */
      if ((TREE_CODE (node) == ARRAY_REF
	   || TREE_CODE (node) == ARRAY_RANGE_REF)
	  && TREE_CODE (TREE_TYPE (TREE_OPERAND (node, 0))) == ARRAY_TYPE)
	{
	  UPDATE_TITCSE (TREE_OPERAND (node, 1));
	  if (TREE_OPERAND (node, 2))
	    UPDATE_TITCSE (TREE_OPERAND (node, 2));
	  if (TREE_OPERAND (node, 3))
	    UPDATE_TITCSE (TREE_OPERAND (node, 3));
	}
      /* Likewise, just because this is a COMPONENT_REF doesn't mean we have a
	 FIELD_DECL, apparently.  The G++ front end can put something else
	 there, at least temporarily.  */
      else if (TREE_CODE (node) == COMPONENT_REF
	       && TREE_CODE (TREE_OPERAND (node, 1)) == FIELD_DECL)
	{
	  if (TREE_OPERAND (node, 2))
	    UPDATE_TITCSE (TREE_OPERAND (node, 2));
	}
      else if (TREE_CODE (node) == BIT_FIELD_REF)
	UPDATE_TITCSE (TREE_OPERAND (node, 2));
    }

  node = lang_hooks.expr_to_decl (node, &tc, &ti, &se);

  /* Now see what's inside.  If it's an INDIRECT_REF, copy our properties from
     the address, since &(*a)->b is a form of addition.  If it's a decl, it's
     invariant and constant if the decl is static.  It's also invariant if it's
     a decl in the current function.  Taking the address of a volatile variable
     is not volatile.  If it's a constant, the address is both invariant and
     constant.  Otherwise it's neither.  */
  if (TREE_CODE (node) == INDIRECT_REF)
    UPDATE_TITCSE (TREE_OPERAND (node, 0));
  else if (DECL_P (node))
    {
      if (staticp (node))
	;
      else if (decl_function_context (node) == current_function_decl
	       /* Addresses of thread-local variables are invariant.  */
	       || (TREE_CODE (node) == VAR_DECL
		   && DECL_THREAD_LOCAL_P (node)))
	tc = false;
      else
	ti = tc = false;
    }
  else if (CONSTANT_CLASS_P (node))
    ;
  else
    {
      ti = tc = false;
      se |= TREE_SIDE_EFFECTS (node);
    }

  TREE_CONSTANT (t) = tc;
  TREE_INVARIANT (t) = ti;
  TREE_SIDE_EFFECTS (t) = se;
#undef UPDATE_TITCSE
}

/* Build an expression of code CODE, data type TYPE, and operands as
   specified.  Expressions and reference nodes can be created this way.
   Constants, decls, types and misc nodes cannot be.

   We define 5 non-variadic functions, from 0 to 4 arguments.  This is
   enough for all extant tree codes.  */

tree
build0_stat (enum tree_code code, tree tt MEM_STAT_DECL)
{
  tree t;

  gcc_assert (TREE_CODE_LENGTH (code) == 0);

  t = make_node_stat (code PASS_MEM_STAT);
  TREE_TYPE (t) = tt;

  return t;
}

tree
build1_stat (enum tree_code code, tree type, tree node MEM_STAT_DECL)
{
  int length = sizeof (struct tree_exp);
#ifdef GATHER_STATISTICS
  tree_node_kind kind;
#endif
  tree t;

#ifdef GATHER_STATISTICS
  switch (TREE_CODE_CLASS (code))
    {
    case tcc_statement:  /* an expression with side effects */
      kind = s_kind;
      break;
    case tcc_reference:  /* a reference */
      kind = r_kind;
      break;
    default:
      kind = e_kind;
      break;
    }

  tree_node_counts[(int) kind]++;
  tree_node_sizes[(int) kind] += length;
#endif

  gcc_assert (TREE_CODE_LENGTH (code) == 1);

  t = ggc_alloc_zone_pass_stat (length, &tree_zone);

  memset (t, 0, sizeof (struct tree_common));

  TREE_SET_CODE (t, code);

  TREE_TYPE (t) = type;
#ifdef USE_MAPPED_LOCATION
  SET_EXPR_LOCATION (t, UNKNOWN_LOCATION);
#else
  SET_EXPR_LOCUS (t, NULL);
#endif
  TREE_COMPLEXITY (t) = 0;
  TREE_OPERAND (t, 0) = node;
  TREE_BLOCK (t) = NULL_TREE;
  if (node && !TYPE_P (node))
    {
      TREE_SIDE_EFFECTS (t) = TREE_SIDE_EFFECTS (node);
      TREE_READONLY (t) = TREE_READONLY (node);
    }

  if (TREE_CODE_CLASS (code) == tcc_statement)
    TREE_SIDE_EFFECTS (t) = 1;
  else switch (code)
    {
    case VA_ARG_EXPR:
      /* All of these have side-effects, no matter what their
	 operands are.  */
      TREE_SIDE_EFFECTS (t) = 1;
      TREE_READONLY (t) = 0;
      break;

    case MISALIGNED_INDIRECT_REF:
    case ALIGN_INDIRECT_REF:
    case INDIRECT_REF:
      /* Whether a dereference is readonly has nothing to do with whether
	 its operand is readonly.  */
      TREE_READONLY (t) = 0;
      break;

    case ADDR_EXPR:
      if (node)
	recompute_tree_invariant_for_addr_expr (t);
      break;

    default:
      if ((TREE_CODE_CLASS (code) == tcc_unary || code == VIEW_CONVERT_EXPR)
	  && node && !TYPE_P (node)
	  && TREE_CONSTANT (node))
	TREE_CONSTANT (t) = 1;
      if ((TREE_CODE_CLASS (code) == tcc_unary || code == VIEW_CONVERT_EXPR)
	  && node && TREE_INVARIANT (node))
	TREE_INVARIANT (t) = 1;
      if (TREE_CODE_CLASS (code) == tcc_reference
	  && node && TREE_THIS_VOLATILE (node))
	TREE_THIS_VOLATILE (t) = 1;
      break;
    }

  return t;
}

#define PROCESS_ARG(N)			\
  do {					\
    TREE_OPERAND (t, N) = arg##N;	\
    if (arg##N &&!TYPE_P (arg##N))	\
      {					\
        if (TREE_SIDE_EFFECTS (arg##N))	\
	  side_effects = 1;		\
        if (!TREE_READONLY (arg##N))	\
	  read_only = 0;		\
        if (!TREE_CONSTANT (arg##N))	\
	  constant = 0;			\
	if (!TREE_INVARIANT (arg##N))	\
	  invariant = 0;		\
      }					\
  } while (0)

tree
build2_stat (enum tree_code code, tree tt, tree arg0, tree arg1 MEM_STAT_DECL)
{
  bool constant, read_only, side_effects, invariant;
  tree t;

  gcc_assert (TREE_CODE_LENGTH (code) == 2);

  t = make_node_stat (code PASS_MEM_STAT);
  TREE_TYPE (t) = tt;

  /* Below, we automatically set TREE_SIDE_EFFECTS and TREE_READONLY for the
     result based on those same flags for the arguments.  But if the
     arguments aren't really even `tree' expressions, we shouldn't be trying
     to do this.  */

  /* Expressions without side effects may be constant if their
     arguments are as well.  */
  constant = (TREE_CODE_CLASS (code) == tcc_comparison
	      || TREE_CODE_CLASS (code) == tcc_binary);
  read_only = 1;
  side_effects = TREE_SIDE_EFFECTS (t);
  invariant = constant;

  PROCESS_ARG(0);
  PROCESS_ARG(1);

  TREE_READONLY (t) = read_only;
  TREE_CONSTANT (t) = constant;
  TREE_INVARIANT (t) = invariant;
  TREE_SIDE_EFFECTS (t) = side_effects;
  TREE_THIS_VOLATILE (t)
    = (TREE_CODE_CLASS (code) == tcc_reference
       && arg0 && TREE_THIS_VOLATILE (arg0));

  return t;
}

tree
build3_stat (enum tree_code code, tree tt, tree arg0, tree arg1,
	     tree arg2 MEM_STAT_DECL)
{
  bool constant, read_only, side_effects, invariant;
  tree t;

  gcc_assert (TREE_CODE_LENGTH (code) == 3);

  t = make_node_stat (code PASS_MEM_STAT);
  TREE_TYPE (t) = tt;

  side_effects = TREE_SIDE_EFFECTS (t);

  PROCESS_ARG(0);
  PROCESS_ARG(1);
  PROCESS_ARG(2);

  if (code == CALL_EXPR && !side_effects)
    {
      tree node;
      int i;

      /* Calls have side-effects, except those to const or
	 pure functions.  */
      i = call_expr_flags (t);
      if (!(i & (ECF_CONST | ECF_PURE)))
	side_effects = 1;

      /* And even those have side-effects if their arguments do.  */
      else for (node = arg1; node; node = TREE_CHAIN (node))
	if (TREE_SIDE_EFFECTS (TREE_VALUE (node)))
	  {
	    side_effects = 1;
	    break;
	  }
    }

  TREE_SIDE_EFFECTS (t) = side_effects;
  TREE_THIS_VOLATILE (t)
    = (TREE_CODE_CLASS (code) == tcc_reference
       && arg0 && TREE_THIS_VOLATILE (arg0));

  return t;
}

tree
build4_stat (enum tree_code code, tree tt, tree arg0, tree arg1,
	     tree arg2, tree arg3 MEM_STAT_DECL)
{
  bool constant, read_only, side_effects, invariant;
  tree t;

  gcc_assert (TREE_CODE_LENGTH (code) == 4);

  t = make_node_stat (code PASS_MEM_STAT);
  TREE_TYPE (t) = tt;

  side_effects = TREE_SIDE_EFFECTS (t);

  PROCESS_ARG(0);
  PROCESS_ARG(1);
  PROCESS_ARG(2);
  PROCESS_ARG(3);

  TREE_SIDE_EFFECTS (t) = side_effects;
  TREE_THIS_VOLATILE (t)
    = (TREE_CODE_CLASS (code) == tcc_reference
       && arg0 && TREE_THIS_VOLATILE (arg0));

  return t;
}

tree
build5_stat (enum tree_code code, tree tt, tree arg0, tree arg1,
	     tree arg2, tree arg3, tree arg4 MEM_STAT_DECL)
{
  bool constant, read_only, side_effects, invariant;
  tree t;

  gcc_assert (TREE_CODE_LENGTH (code) == 5);

  t = make_node_stat (code PASS_MEM_STAT);
  TREE_TYPE (t) = tt;

  side_effects = TREE_SIDE_EFFECTS (t);

  PROCESS_ARG(0);
  PROCESS_ARG(1);
  PROCESS_ARG(2);
  PROCESS_ARG(3);
  PROCESS_ARG(4);

  TREE_SIDE_EFFECTS (t) = side_effects;
  TREE_THIS_VOLATILE (t)
    = (TREE_CODE_CLASS (code) == tcc_reference
       && arg0 && TREE_THIS_VOLATILE (arg0));

  return t;
}

tree
build7_stat (enum tree_code code, tree tt, tree arg0, tree arg1,
	     tree arg2, tree arg3, tree arg4, tree arg5,
	     tree arg6 MEM_STAT_DECL)
{
  bool constant, read_only, side_effects, invariant;
  tree t;

  gcc_assert (code == TARGET_MEM_REF);

  t = make_node_stat (code PASS_MEM_STAT);
  TREE_TYPE (t) = tt;

  side_effects = TREE_SIDE_EFFECTS (t);

  PROCESS_ARG(0);
  PROCESS_ARG(1);
  PROCESS_ARG(2);
  PROCESS_ARG(3);
  PROCESS_ARG(4);
  PROCESS_ARG(5);
  PROCESS_ARG(6);

  TREE_SIDE_EFFECTS (t) = side_effects;
  TREE_THIS_VOLATILE (t) = 0;

  return t;
}

/* Similar except don't specify the TREE_TYPE
   and leave the TREE_SIDE_EFFECTS as 0.
   It is permissible for arguments to be null,
   or even garbage if their values do not matter.  */

tree
build_nt (enum tree_code code, ...)
{
  tree t;
  int length;
  int i;
  va_list p;

  va_start (p, code);

  t = make_node (code);
  length = TREE_CODE_LENGTH (code);

  for (i = 0; i < length; i++)
    TREE_OPERAND (t, i) = va_arg (p, tree);

  va_end (p);
  return t;
}

/* Create a DECL_... node of code CODE, name NAME and data type TYPE.
   We do NOT enter this node in any sort of symbol table.

   layout_decl is used to set up the decl's storage layout.
   Other slots are initialized to 0 or null pointers.  */

tree
build_decl_stat (enum tree_code code, tree name, tree type MEM_STAT_DECL)
{
  tree t;

  t = make_node_stat (code PASS_MEM_STAT);

/*  if (type == error_mark_node)
    type = integer_type_node; */
/* That is not done, deliberately, so that having error_mark_node
   as the type can suppress useless errors in the use of this variable.  */

  DECL_NAME (t) = name;
  TREE_TYPE (t) = type;

  if (code == VAR_DECL || code == PARM_DECL || code == RESULT_DECL)
    layout_decl (t, 0);

  return t;
}

/* Builds and returns function declaration with NAME and TYPE.  */

tree
build_fn_decl (const char *name, tree type)
{
  tree id = get_identifier (name);
  tree decl = build_decl (FUNCTION_DECL, id, type);

  DECL_EXTERNAL (decl) = 1;
  TREE_PUBLIC (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  TREE_NOTHROW (decl) = 1;

  return decl;
}


/* BLOCK nodes are used to represent the structure of binding contours
   and declarations, once those contours have been exited and their contents
   compiled.  This information is used for outputting debugging info.  */

tree
build_block (tree vars, tree subblocks, tree supercontext, tree chain)
{
  tree block = make_node (BLOCK);

  BLOCK_VARS (block) = vars;
  BLOCK_SUBBLOCKS (block) = subblocks;
  BLOCK_SUPERCONTEXT (block) = supercontext;
  BLOCK_CHAIN (block) = chain;
  return block;
}

#if 1 /* ! defined(USE_MAPPED_LOCATION) */
/* ??? gengtype doesn't handle conditionals */
static GTY(()) source_locus last_annotated_node;
#endif

#ifdef USE_MAPPED_LOCATION

expanded_location
expand_location (source_location loc)
{
  expanded_location xloc;
  if (loc == 0) { xloc.file = NULL; xloc.line = 0;  xloc.column = 0; }
  else
    {
      const struct line_map *map = linemap_lookup (&line_table, loc);
      xloc.file = map->to_file;
      xloc.line = SOURCE_LINE (map, loc);
      xloc.column = SOURCE_COLUMN (map, loc);
    };
  return xloc;
}

#else

/* Record the exact location where an expression or an identifier were
   encountered.  */

void
annotate_with_file_line (tree node, const char *file, int line)
{
  /* Roughly one percent of the calls to this function are to annotate
     a node with the same information already attached to that node!
     Just return instead of wasting memory.  */
  if (EXPR_LOCUS (node)
      && EXPR_LINENO (node) == line
      && (EXPR_FILENAME (node) == file
	  || !strcmp (EXPR_FILENAME (node), file)))
    {
      last_annotated_node = EXPR_LOCUS (node);
      return;
    }

  /* In heavily macroized code (such as GCC itself) this single
     entry cache can reduce the number of allocations by more
     than half.  */
  if (last_annotated_node
      && last_annotated_node->line == line
      && (last_annotated_node->file == file
	  || !strcmp (last_annotated_node->file, file)))
    {
      SET_EXPR_LOCUS (node, last_annotated_node);
      return;
    }

  SET_EXPR_LOCUS (node, ggc_alloc (sizeof (location_t)));
  EXPR_LINENO (node) = line;
  EXPR_FILENAME (node) = file;
  last_annotated_node = EXPR_LOCUS (node);
}

void
annotate_with_locus (tree node, location_t locus)
{
  annotate_with_file_line (node, locus.file, locus.line);
}
#endif

/* Return a declaration like DDECL except that its DECL_ATTRIBUTES
   is ATTRIBUTE.  */

tree
build_decl_attribute_variant (tree ddecl, tree attribute)
{
  DECL_ATTRIBUTES (ddecl) = attribute;
  return ddecl;
}

/* Borrowed from hashtab.c iterative_hash implementation.  */
#define mix(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<< 8); \
  c -= a; c -= b; c ^= ((b&0xffffffff)>>13); \
  a -= b; a -= c; a ^= ((c&0xffffffff)>>12); \
  b -= c; b -= a; b = (b ^ (a<<16)) & 0xffffffff; \
  c -= a; c -= b; c = (c ^ (b>> 5)) & 0xffffffff; \
  a -= b; a -= c; a = (a ^ (c>> 3)) & 0xffffffff; \
  b -= c; b -= a; b = (b ^ (a<<10)) & 0xffffffff; \
  c -= a; c -= b; c = (c ^ (b>>15)) & 0xffffffff; \
}


/* Produce good hash value combining VAL and VAL2.  */
static inline hashval_t
iterative_hash_hashval_t (hashval_t val, hashval_t val2)
{
  /* the golden ratio; an arbitrary value.  */
  hashval_t a = 0x9e3779b9;

  mix (a, val, val2);
  return val2;
}

/* Produce good hash value combining PTR and VAL2.  */
static inline hashval_t
iterative_hash_pointer (void *ptr, hashval_t val2)
{
  if (sizeof (ptr) == sizeof (hashval_t))
    return iterative_hash_hashval_t ((size_t) ptr, val2);
  else
    {
      hashval_t a = (hashval_t) (size_t) ptr;
      /* Avoid warnings about shifting of more than the width of the type on
         hosts that won't execute this path.  */
      int zero = 0;
      hashval_t b = (hashval_t) ((size_t) ptr >> (sizeof (hashval_t) * 8 + zero));
      mix (a, b, val2);
      return val2;
    }
}

/* Produce good hash value combining VAL and VAL2.  */
static inline hashval_t
iterative_hash_host_wide_int (HOST_WIDE_INT val, hashval_t val2)
{
  if (sizeof (HOST_WIDE_INT) == sizeof (hashval_t))
    return iterative_hash_hashval_t (val, val2);
  else
    {
      hashval_t a = (hashval_t) val;
      /* Avoid warnings about shifting of more than the width of the type on
         hosts that won't execute this path.  */
      int zero = 0;
      hashval_t b = (hashval_t) (val >> (sizeof (hashval_t) * 8 + zero));
      mix (a, b, val2);
      if (sizeof (HOST_WIDE_INT) > 2 * sizeof (hashval_t))
	{
	  hashval_t a = (hashval_t) (val >> (sizeof (hashval_t) * 16 + zero));
	  hashval_t b = (hashval_t) (val >> (sizeof (hashval_t) * 24 + zero));
	  mix (a, b, val2);
	}
      return val2;
    }
}

/* Return a type like TTYPE except that its TYPE_ATTRIBUTE
   is ATTRIBUTE and its qualifiers are QUALS.

   Record such modified types already made so we don't make duplicates.  */

static tree
build_type_attribute_qual_variant (tree ttype, tree attribute, int quals)
{
  if (! attribute_list_equal (TYPE_ATTRIBUTES (ttype), attribute))
    {
      hashval_t hashcode = 0;
      tree ntype;
      enum tree_code code = TREE_CODE (ttype);

      ntype = copy_node (ttype);

      TYPE_POINTER_TO (ntype) = 0;
      TYPE_REFERENCE_TO (ntype) = 0;
      TYPE_ATTRIBUTES (ntype) = attribute;

      /* Create a new main variant of TYPE.  */
      TYPE_MAIN_VARIANT (ntype) = ntype;
      TYPE_NEXT_VARIANT (ntype) = 0;
      set_type_quals (ntype, TYPE_UNQUALIFIED);

      hashcode = iterative_hash_object (code, hashcode);
      if (TREE_TYPE (ntype))
	hashcode = iterative_hash_object (TYPE_HASH (TREE_TYPE (ntype)),
					  hashcode);
      hashcode = attribute_hash_list (attribute, hashcode);

      switch (TREE_CODE (ntype))
	{
	case FUNCTION_TYPE:
	  hashcode = type_hash_list (TYPE_ARG_TYPES (ntype), hashcode);
	  break;
	case ARRAY_TYPE:
	  hashcode = iterative_hash_object (TYPE_HASH (TYPE_DOMAIN (ntype)),
					    hashcode);
	  break;
	case INTEGER_TYPE:
	  hashcode = iterative_hash_object
	    (TREE_INT_CST_LOW (TYPE_MAX_VALUE (ntype)), hashcode);
	  hashcode = iterative_hash_object
	    (TREE_INT_CST_HIGH (TYPE_MAX_VALUE (ntype)), hashcode);
	  break;
	case REAL_TYPE:
	  {
	    unsigned int precision = TYPE_PRECISION (ntype);
	    hashcode = iterative_hash_object (precision, hashcode);
	  }
	  break;
	default:
	  break;
	}

      ntype = type_hash_canon (hashcode, ntype);
      ttype = build_qualified_type (ntype, quals);
    }

  return ttype;
}


/* Return a type like TTYPE except that its TYPE_ATTRIBUTE
   is ATTRIBUTE.

   Record such modified types already made so we don't make duplicates.  */

tree
build_type_attribute_variant (tree ttype, tree attribute)
{
  return build_type_attribute_qual_variant (ttype, attribute,
					    TYPE_QUALS (ttype));
}

/* Return nonzero if IDENT is a valid name for attribute ATTR,
   or zero if not.

   We try both `text' and `__text__', ATTR may be either one.  */
/* ??? It might be a reasonable simplification to require ATTR to be only
   `text'.  One might then also require attribute lists to be stored in
   their canonicalized form.  */

static int
is_attribute_with_length_p (const char *attr, int attr_len, tree ident)
{
  int ident_len;
  const char *p;

  if (TREE_CODE (ident) != IDENTIFIER_NODE)
    return 0;
  
  p = IDENTIFIER_POINTER (ident);
  ident_len = IDENTIFIER_LENGTH (ident);
  
  if (ident_len == attr_len
      && strcmp (attr, p) == 0)
    return 1;

  /* If ATTR is `__text__', IDENT must be `text'; and vice versa.  */
  if (attr[0] == '_')
    {
      gcc_assert (attr[1] == '_');
      gcc_assert (attr[attr_len - 2] == '_');
      gcc_assert (attr[attr_len - 1] == '_');
      if (ident_len == attr_len - 4
	  && strncmp (attr + 2, p, attr_len - 4) == 0)
	return 1;
    }
  else
    {
      if (ident_len == attr_len + 4
	  && p[0] == '_' && p[1] == '_'
	  && p[ident_len - 2] == '_' && p[ident_len - 1] == '_'
	  && strncmp (attr, p + 2, attr_len) == 0)
	return 1;
    }

  return 0;
}

/* Return nonzero if IDENT is a valid name for attribute ATTR,
   or zero if not.

   We try both `text' and `__text__', ATTR may be either one.  */

int
is_attribute_p (const char *attr, tree ident)
{
  return is_attribute_with_length_p (attr, strlen (attr), ident);
}

/* Given an attribute name and a list of attributes, return a pointer to the
   attribute's list element if the attribute is part of the list, or NULL_TREE
   if not found.  If the attribute appears more than once, this only
   returns the first occurrence; the TREE_CHAIN of the return value should
   be passed back in if further occurrences are wanted.  */

tree
lookup_attribute (const char *attr_name, tree list)
{
  tree l;
  size_t attr_len = strlen (attr_name);

  for (l = list; l; l = TREE_CHAIN (l))
    {
      gcc_assert (TREE_CODE (TREE_PURPOSE (l)) == IDENTIFIER_NODE);
      if (is_attribute_with_length_p (attr_name, attr_len, TREE_PURPOSE (l)))
	return l;
    }

  return NULL_TREE;
}

/* Remove any instances of attribute ATTR_NAME in LIST and return the
   modified list.  */

tree
remove_attribute (const char *attr_name, tree list)
{
  tree *p;
  size_t attr_len = strlen (attr_name);

  for (p = &list; *p; )
    {
      tree l = *p;
      gcc_assert (TREE_CODE (TREE_PURPOSE (l)) == IDENTIFIER_NODE);
      if (is_attribute_with_length_p (attr_name, attr_len, TREE_PURPOSE (l)))
	*p = TREE_CHAIN (l);
      else
	p = &TREE_CHAIN (l);
    }

  return list;
}

/* Return an attribute list that is the union of a1 and a2.  */

tree
merge_attributes (tree a1, tree a2)
{
  tree attributes;

  /* Either one unset?  Take the set one.  */

  if ((attributes = a1) == 0)
    attributes = a2;

  /* One that completely contains the other?  Take it.  */

  else if (a2 != 0 && ! attribute_list_contained (a1, a2))
    {
      if (attribute_list_contained (a2, a1))
	attributes = a2;
      else
	{
	  /* Pick the longest list, and hang on the other list.  */

	  if (list_length (a1) < list_length (a2))
	    attributes = a2, a2 = a1;

	  for (; a2 != 0; a2 = TREE_CHAIN (a2))
	    {
	      tree a;
	      for (a = lookup_attribute (IDENTIFIER_POINTER (TREE_PURPOSE (a2)),
					 attributes);
		   a != NULL_TREE;
		   a = lookup_attribute (IDENTIFIER_POINTER (TREE_PURPOSE (a2)),
					 TREE_CHAIN (a)))
		{
		  if (TREE_VALUE (a) != NULL
		      && TREE_CODE (TREE_VALUE (a)) == TREE_LIST
		      && TREE_VALUE (a2) != NULL
		      && TREE_CODE (TREE_VALUE (a2)) == TREE_LIST)
		    {
		      if (simple_cst_list_equal (TREE_VALUE (a),
						 TREE_VALUE (a2)) == 1)
			break;
		    }
		  else if (simple_cst_equal (TREE_VALUE (a),
					     TREE_VALUE (a2)) == 1)
		    break;
		}
	      if (a == NULL_TREE)
		{
		  a1 = copy_node (a2);
		  TREE_CHAIN (a1) = attributes;
		  attributes = a1;
		}
	    }
	}
    }
  return attributes;
}

/* Given types T1 and T2, merge their attributes and return
  the result.  */

tree
merge_type_attributes (tree t1, tree t2)
{
  return merge_attributes (TYPE_ATTRIBUTES (t1),
			   TYPE_ATTRIBUTES (t2));
}

/* Given decls OLDDECL and NEWDECL, merge their attributes and return
   the result.  */

tree
merge_decl_attributes (tree olddecl, tree newdecl)
{
  return merge_attributes (DECL_ATTRIBUTES (olddecl),
			   DECL_ATTRIBUTES (newdecl));
}

#if TARGET_DLLIMPORT_DECL_ATTRIBUTES

/* Specialization of merge_decl_attributes for various Windows targets.

   This handles the following situation:

     __declspec (dllimport) int foo;
     int foo;

   The second instance of `foo' nullifies the dllimport.  */

tree
merge_dllimport_decl_attributes (tree old, tree new)
{
  tree a;
  int delete_dllimport_p = 1;

  /* What we need to do here is remove from `old' dllimport if it doesn't
     appear in `new'.  dllimport behaves like extern: if a declaration is
     marked dllimport and a definition appears later, then the object
     is not dllimport'd.  We also remove a `new' dllimport if the old list
     contains dllexport:  dllexport always overrides dllimport, regardless
     of the order of declaration.  */     
  if (!VAR_OR_FUNCTION_DECL_P (new))
    delete_dllimport_p = 0;
  else if (DECL_DLLIMPORT_P (new)
     	   && lookup_attribute ("dllexport", DECL_ATTRIBUTES (old)))
    { 
      DECL_DLLIMPORT_P (new) = 0;
      warning (OPT_Wattributes, "%q+D already declared with dllexport attribute: "
	      "dllimport ignored", new);
    }
  else if (DECL_DLLIMPORT_P (old) && !DECL_DLLIMPORT_P (new))
    {
      /* Warn about overriding a symbol that has already been used. eg:
           extern int __attribute__ ((dllimport)) foo;
	   int* bar () {return &foo;}
	   int foo;
      */
      if (TREE_USED (old))
	{
	  warning (0, "%q+D redeclared without dllimport attribute "
		   "after being referenced with dll linkage", new);
	  /* If we have used a variable's address with dllimport linkage,
	      keep the old DECL_DLLIMPORT_P flag: the ADDR_EXPR using the
	      decl may already have had TREE_INVARIANT and TREE_CONSTANT
	      computed.
	      We still remove the attribute so that assembler code refers
	      to '&foo rather than '_imp__foo'.  */
	  if (TREE_CODE (old) == VAR_DECL && TREE_ADDRESSABLE (old))
	    DECL_DLLIMPORT_P (new) = 1;
	}

      /* Let an inline definition silently override the external reference,
	 but otherwise warn about attribute inconsistency.  */ 
      else if (TREE_CODE (new) == VAR_DECL
	       || !DECL_DECLARED_INLINE_P (new))
	warning (OPT_Wattributes, "%q+D redeclared without dllimport attribute: "
		  "previous dllimport ignored", new);
    }
  else
    delete_dllimport_p = 0;

  a = merge_attributes (DECL_ATTRIBUTES (old), DECL_ATTRIBUTES (new));

  if (delete_dllimport_p) 
    {
      tree prev, t;
      const size_t attr_len = strlen ("dllimport"); 
     
      /* Scan the list for dllimport and delete it.  */
      for (prev = NULL_TREE, t = a; t; prev = t, t = TREE_CHAIN (t))
	{
	  if (is_attribute_with_length_p ("dllimport", attr_len,
					  TREE_PURPOSE (t)))
	    {
	      if (prev == NULL_TREE)
		a = TREE_CHAIN (a);
	      else
		TREE_CHAIN (prev) = TREE_CHAIN (t);
	      break;
	    }
	}
    }

  return a;
}

/* Handle a "dllimport" or "dllexport" attribute; arguments as in
   struct attribute_spec.handler.  */

tree
handle_dll_attribute (tree * pnode, tree name, tree args, int flags,
		      bool *no_add_attrs)
{
  tree node = *pnode;

  /* These attributes may apply to structure and union types being created,
     but otherwise should pass to the declaration involved.  */
  if (!DECL_P (node))
    {
      if (flags & ((int) ATTR_FLAG_DECL_NEXT | (int) ATTR_FLAG_FUNCTION_NEXT
		   | (int) ATTR_FLAG_ARRAY_NEXT))
	{
	  *no_add_attrs = true;
	  return tree_cons (name, args, NULL_TREE);
	}
      if (TREE_CODE (node) != RECORD_TYPE && TREE_CODE (node) != UNION_TYPE)
	{
	  warning (OPT_Wattributes, "%qs attribute ignored",
		   IDENTIFIER_POINTER (name));
	  *no_add_attrs = true;
	}

      return NULL_TREE;
    }

  if (TREE_CODE (node) != FUNCTION_DECL
      && TREE_CODE (node) != VAR_DECL)
    {
      *no_add_attrs = true;
      warning (OPT_Wattributes, "%qs attribute ignored",
	       IDENTIFIER_POINTER (name));
      return NULL_TREE;
    }

  /* Report error on dllimport ambiguities seen now before they cause
     any damage.  */
  else if (is_attribute_p ("dllimport", name))
    {
      /* Honor any target-specific overrides. */ 
      if (!targetm.valid_dllimport_attribute_p (node))
	*no_add_attrs = true;

     else if (TREE_CODE (node) == FUNCTION_DECL
	        && DECL_DECLARED_INLINE_P (node))
	{
	  warning (OPT_Wattributes, "inline function %q+D declared as "
		  " dllimport: attribute ignored", node); 
	  *no_add_attrs = true;
	}
      /* Like MS, treat definition of dllimported variables and
	 non-inlined functions on declaration as syntax errors. */
     else if (TREE_CODE (node) == FUNCTION_DECL && DECL_INITIAL (node))
	{
	  error ("function %q+D definition is marked dllimport", node);
	  *no_add_attrs = true;
	}

     else if (TREE_CODE (node) == VAR_DECL)
	{
	  if (DECL_INITIAL (node))
	    {
	      error ("variable %q+D definition is marked dllimport",
		     node);
	      *no_add_attrs = true;
	    }

	  /* `extern' needn't be specified with dllimport.
	     Specify `extern' now and hope for the best.  Sigh.  */
	  DECL_EXTERNAL (node) = 1;
	  /* Also, implicitly give dllimport'd variables declared within
	     a function global scope, unless declared static.  */
	  if (current_function_decl != NULL_TREE && !TREE_STATIC (node))
	    TREE_PUBLIC (node) = 1;
	}

      if (*no_add_attrs == false)
        DECL_DLLIMPORT_P (node) = 1;
    }

  /*  Report error if symbol is not accessible at global scope.  */
  if (!TREE_PUBLIC (node)
      && (TREE_CODE (node) == VAR_DECL
	  || TREE_CODE (node) == FUNCTION_DECL))
    {
      error ("external linkage required for symbol %q+D because of "
	     "%qs attribute", node, IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

#endif /* TARGET_DLLIMPORT_DECL_ATTRIBUTES  */

/* Set the type qualifiers for TYPE to TYPE_QUALS, which is a bitmask
   of the various TYPE_QUAL values.  */

static void
set_type_quals (tree type, int type_quals)
{
  TYPE_READONLY (type) = (type_quals & TYPE_QUAL_CONST) != 0;
  TYPE_VOLATILE (type) = (type_quals & TYPE_QUAL_VOLATILE) != 0;
  TYPE_RESTRICT (type) = (type_quals & TYPE_QUAL_RESTRICT) != 0;
}

/* Returns true iff cand is equivalent to base with type_quals.  */

bool
check_qualified_type (tree cand, tree base, int type_quals)
{
  return (TYPE_QUALS (cand) == type_quals
	  && TYPE_NAME (cand) == TYPE_NAME (base)
	  /* Apparently this is needed for Objective-C.  */
	  && TYPE_CONTEXT (cand) == TYPE_CONTEXT (base)
	  && attribute_list_equal (TYPE_ATTRIBUTES (cand),
				   TYPE_ATTRIBUTES (base)));
}

/* Return a version of the TYPE, qualified as indicated by the
   TYPE_QUALS, if one exists.  If no qualified version exists yet,
   return NULL_TREE.  */

tree
get_qualified_type (tree type, int type_quals)
{
  tree t;

  if (TYPE_QUALS (type) == type_quals)
    return type;

  /* Search the chain of variants to see if there is already one there just
     like the one we need to have.  If so, use that existing one.  We must
     preserve the TYPE_NAME, since there is code that depends on this.  */
  for (t = TYPE_MAIN_VARIANT (type); t; t = TYPE_NEXT_VARIANT (t))
    if (check_qualified_type (t, type, type_quals))
      return t;

  return NULL_TREE;
}

/* Like get_qualified_type, but creates the type if it does not
   exist.  This function never returns NULL_TREE.  */

tree
build_qualified_type (tree type, int type_quals)
{
  tree t;

  /* See if we already have the appropriate qualified variant.  */
  t = get_qualified_type (type, type_quals);

  /* If not, build it.  */
  if (!t)
    {
      t = build_variant_type_copy (type);
      set_type_quals (t, type_quals);
    }

  return t;
}

/* Create a new distinct copy of TYPE.  The new type is made its own
   MAIN_VARIANT.  */

tree
build_distinct_type_copy (tree type)
{
  tree t = copy_node (type);
  
  TYPE_POINTER_TO (t) = 0;
  TYPE_REFERENCE_TO (t) = 0;

  /* Make it its own variant.  */
  TYPE_MAIN_VARIANT (t) = t;
  TYPE_NEXT_VARIANT (t) = 0;

  /* Note that it is now possible for TYPE_MIN_VALUE to be a value
     whose TREE_TYPE is not t.  This can also happen in the Ada
     frontend when using subtypes.  */

  return t;
}

/* Create a new variant of TYPE, equivalent but distinct.
   This is so the caller can modify it.  */

tree
build_variant_type_copy (tree type)
{
  tree t, m = TYPE_MAIN_VARIANT (type);

  t = build_distinct_type_copy (type);
  
  /* Add the new type to the chain of variants of TYPE.  */
  TYPE_NEXT_VARIANT (t) = TYPE_NEXT_VARIANT (m);
  TYPE_NEXT_VARIANT (m) = t;
  TYPE_MAIN_VARIANT (t) = m;

  return t;
}

/* Return true if the from tree in both tree maps are equal.  */

int
tree_map_eq (const void *va, const void *vb)
{
  const struct tree_map  *a = va, *b = vb;
  return (a->from == b->from);
}

/* Hash a from tree in a tree_map.  */

unsigned int
tree_map_hash (const void *item)
{
  return (((const struct tree_map *) item)->hash);
}

/* Return true if this tree map structure is marked for garbage collection
   purposes.  We simply return true if the from tree is marked, so that this
   structure goes away when the from tree goes away.  */

int
tree_map_marked_p (const void *p)
{
  tree from = ((struct tree_map *) p)->from;

  return ggc_marked_p (from);
}

/* Return true if the trees in the tree_int_map *'s VA and VB are equal.  */

static int
tree_int_map_eq (const void *va, const void *vb)
{
  const struct tree_int_map  *a = va, *b = vb;
  return (a->from == b->from);
}

/* Hash a from tree in the tree_int_map * ITEM.  */

static unsigned int
tree_int_map_hash (const void *item)
{
  return htab_hash_pointer (((const struct tree_int_map *)item)->from);
}

/* Return true if this tree int map structure is marked for garbage collection
   purposes.  We simply return true if the from tree_int_map *P's from tree is marked, so that this
   structure goes away when the from tree goes away.  */

static int
tree_int_map_marked_p (const void *p)
{
  tree from = ((struct tree_int_map *) p)->from;

  return ggc_marked_p (from);
}
/* Lookup an init priority for FROM, and return it if we find one.  */

unsigned short
decl_init_priority_lookup (tree from)
{
  struct tree_int_map *h, in;
  in.from = from;

  h = htab_find_with_hash (init_priority_for_decl, 
			   &in, htab_hash_pointer (from));
  if (h)
    return h->to;
  return 0;
}

/* Insert a mapping FROM->TO in the init priority hashtable.  */

void
decl_init_priority_insert (tree from, unsigned short to)
{
  struct tree_int_map *h;
  void **loc;

  h = ggc_alloc (sizeof (struct tree_int_map));
  h->from = from;
  h->to = to;
  loc = htab_find_slot_with_hash (init_priority_for_decl, h, 
				  htab_hash_pointer (from), INSERT);
  *(struct tree_int_map **) loc = h;
}  

/* Look up a restrict qualified base decl for FROM.  */

tree
decl_restrict_base_lookup (tree from)
{
  struct tree_map *h;
  struct tree_map in;

  in.from = from;
  h = htab_find_with_hash (restrict_base_for_decl, &in,
			   htab_hash_pointer (from));
  return h ? h->to : NULL_TREE;
}

/* Record the restrict qualified base TO for FROM.  */

void
decl_restrict_base_insert (tree from, tree to)
{
  struct tree_map *h;
  void **loc;

  h = ggc_alloc (sizeof (struct tree_map));
  h->hash = htab_hash_pointer (from);
  h->from = from;
  h->to = to;
  loc = htab_find_slot_with_hash (restrict_base_for_decl, h, h->hash, INSERT);
  *(struct tree_map **) loc = h;
}

/* Print out the statistics for the DECL_DEBUG_EXPR hash table.  */

static void
print_debug_expr_statistics (void)
{
  fprintf (stderr, "DECL_DEBUG_EXPR  hash: size %ld, %ld elements, %f collisions\n",
	   (long) htab_size (debug_expr_for_decl),
	   (long) htab_elements (debug_expr_for_decl),
	   htab_collisions (debug_expr_for_decl));
}

/* Print out the statistics for the DECL_VALUE_EXPR hash table.  */

static void
print_value_expr_statistics (void)
{
  fprintf (stderr, "DECL_VALUE_EXPR  hash: size %ld, %ld elements, %f collisions\n",
	   (long) htab_size (value_expr_for_decl),
	   (long) htab_elements (value_expr_for_decl),
	   htab_collisions (value_expr_for_decl));
}

/* Print out statistics for the RESTRICT_BASE_FOR_DECL hash table, but
   don't print anything if the table is empty.  */

static void
print_restrict_base_statistics (void)
{
  if (htab_elements (restrict_base_for_decl) != 0)
    fprintf (stderr,
	     "RESTRICT_BASE    hash: size %ld, %ld elements, %f collisions\n",
	     (long) htab_size (restrict_base_for_decl),
	     (long) htab_elements (restrict_base_for_decl),
	     htab_collisions (restrict_base_for_decl));
}

/* Lookup a debug expression for FROM, and return it if we find one.  */

tree 
decl_debug_expr_lookup (tree from)
{
  struct tree_map *h, in;
  in.from = from;

  h = htab_find_with_hash (debug_expr_for_decl, &in, htab_hash_pointer (from));
  if (h)
    return h->to;
  return NULL_TREE;
}

/* Insert a mapping FROM->TO in the debug expression hashtable.  */

void
decl_debug_expr_insert (tree from, tree to)
{
  struct tree_map *h;
  void **loc;

  h = ggc_alloc (sizeof (struct tree_map));
  h->hash = htab_hash_pointer (from);
  h->from = from;
  h->to = to;
  loc = htab_find_slot_with_hash (debug_expr_for_decl, h, h->hash, INSERT);
  *(struct tree_map **) loc = h;
}  

/* Lookup a value expression for FROM, and return it if we find one.  */

tree 
decl_value_expr_lookup (tree from)
{
  struct tree_map *h, in;
  in.from = from;

  h = htab_find_with_hash (value_expr_for_decl, &in, htab_hash_pointer (from));
  if (h)
    return h->to;
  return NULL_TREE;
}

/* Insert a mapping FROM->TO in the value expression hashtable.  */

void
decl_value_expr_insert (tree from, tree to)
{
  struct tree_map *h;
  void **loc;

  h = ggc_alloc (sizeof (struct tree_map));
  h->hash = htab_hash_pointer (from);
  h->from = from;
  h->to = to;
  loc = htab_find_slot_with_hash (value_expr_for_decl, h, h->hash, INSERT);
  *(struct tree_map **) loc = h;
}

/* Hashing of types so that we don't make duplicates.
   The entry point is `type_hash_canon'.  */

/* Compute a hash code for a list of types (chain of TREE_LIST nodes
   with types in the TREE_VALUE slots), by adding the hash codes
   of the individual types.  */

unsigned int
type_hash_list (tree list, hashval_t hashcode)
{
  tree tail;

  for (tail = list; tail; tail = TREE_CHAIN (tail))
    if (TREE_VALUE (tail) != error_mark_node)
      hashcode = iterative_hash_object (TYPE_HASH (TREE_VALUE (tail)),
					hashcode);

  return hashcode;
}

/* These are the Hashtable callback functions.  */

/* Returns true iff the types are equivalent.  */

static int
type_hash_eq (const void *va, const void *vb)
{
  const struct type_hash *a = va, *b = vb;

  /* First test the things that are the same for all types.  */
  if (a->hash != b->hash
      || TREE_CODE (a->type) != TREE_CODE (b->type)
      || TREE_TYPE (a->type) != TREE_TYPE (b->type)
      || !attribute_list_equal (TYPE_ATTRIBUTES (a->type),
				 TYPE_ATTRIBUTES (b->type))
      || TYPE_ALIGN (a->type) != TYPE_ALIGN (b->type)
      || TYPE_MODE (a->type) != TYPE_MODE (b->type))
    return 0;

  switch (TREE_CODE (a->type))
    {
    case VOID_TYPE:
    case COMPLEX_TYPE:
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      return 1;

    case VECTOR_TYPE:
      return TYPE_VECTOR_SUBPARTS (a->type) == TYPE_VECTOR_SUBPARTS (b->type);

    case ENUMERAL_TYPE:
      if (TYPE_VALUES (a->type) != TYPE_VALUES (b->type)
	  && !(TYPE_VALUES (a->type)
	       && TREE_CODE (TYPE_VALUES (a->type)) == TREE_LIST
	       && TYPE_VALUES (b->type)
	       && TREE_CODE (TYPE_VALUES (b->type)) == TREE_LIST
	       && type_list_equal (TYPE_VALUES (a->type),
				   TYPE_VALUES (b->type))))
	return 0;

      /* ... fall through ... */

    case INTEGER_TYPE:
    case REAL_TYPE:
    case BOOLEAN_TYPE:
      return ((TYPE_MAX_VALUE (a->type) == TYPE_MAX_VALUE (b->type)
	       || tree_int_cst_equal (TYPE_MAX_VALUE (a->type),
				      TYPE_MAX_VALUE (b->type)))
	      && (TYPE_MIN_VALUE (a->type) == TYPE_MIN_VALUE (b->type)
		  || tree_int_cst_equal (TYPE_MIN_VALUE (a->type),
					 TYPE_MIN_VALUE (b->type))));

    case OFFSET_TYPE:
      return TYPE_OFFSET_BASETYPE (a->type) == TYPE_OFFSET_BASETYPE (b->type);

    case METHOD_TYPE:
      return (TYPE_METHOD_BASETYPE (a->type) == TYPE_METHOD_BASETYPE (b->type)
	      && (TYPE_ARG_TYPES (a->type) == TYPE_ARG_TYPES (b->type)
		  || (TYPE_ARG_TYPES (a->type)
		      && TREE_CODE (TYPE_ARG_TYPES (a->type)) == TREE_LIST
		      && TYPE_ARG_TYPES (b->type)
		      && TREE_CODE (TYPE_ARG_TYPES (b->type)) == TREE_LIST
		      && type_list_equal (TYPE_ARG_TYPES (a->type),
					  TYPE_ARG_TYPES (b->type)))));

    case ARRAY_TYPE:
      return TYPE_DOMAIN (a->type) == TYPE_DOMAIN (b->type);

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      return (TYPE_FIELDS (a->type) == TYPE_FIELDS (b->type)
	      || (TYPE_FIELDS (a->type)
		  && TREE_CODE (TYPE_FIELDS (a->type)) == TREE_LIST
		  && TYPE_FIELDS (b->type)
		  && TREE_CODE (TYPE_FIELDS (b->type)) == TREE_LIST
		  && type_list_equal (TYPE_FIELDS (a->type),
				      TYPE_FIELDS (b->type))));

    case FUNCTION_TYPE:
      return (TYPE_ARG_TYPES (a->type) == TYPE_ARG_TYPES (b->type)
	      || (TYPE_ARG_TYPES (a->type)
		  && TREE_CODE (TYPE_ARG_TYPES (a->type)) == TREE_LIST
		  && TYPE_ARG_TYPES (b->type)
		  && TREE_CODE (TYPE_ARG_TYPES (b->type)) == TREE_LIST
		  && type_list_equal (TYPE_ARG_TYPES (a->type),
				      TYPE_ARG_TYPES (b->type))));

    default:
      return 0;
    }
}

/* Return the cached hash value.  */

static hashval_t
type_hash_hash (const void *item)
{
  return ((const struct type_hash *) item)->hash;
}

/* Look in the type hash table for a type isomorphic to TYPE.
   If one is found, return it.  Otherwise return 0.  */

tree
type_hash_lookup (hashval_t hashcode, tree type)
{
  struct type_hash *h, in;

  /* The TYPE_ALIGN field of a type is set by layout_type(), so we
     must call that routine before comparing TYPE_ALIGNs.  */
  layout_type (type);

  in.hash = hashcode;
  in.type = type;

  h = htab_find_with_hash (type_hash_table, &in, hashcode);
  if (h)
    return h->type;
  return NULL_TREE;
}

/* Add an entry to the type-hash-table
   for a type TYPE whose hash code is HASHCODE.  */

void
type_hash_add (hashval_t hashcode, tree type)
{
  struct type_hash *h;
  void **loc;

  h = ggc_alloc (sizeof (struct type_hash));
  h->hash = hashcode;
  h->type = type;
  loc = htab_find_slot_with_hash (type_hash_table, h, hashcode, INSERT);
  *(struct type_hash **) loc = h;
}

/* Given TYPE, and HASHCODE its hash code, return the canonical
   object for an identical type if one already exists.
   Otherwise, return TYPE, and record it as the canonical object.

   To use this function, first create a type of the sort you want.
   Then compute its hash code from the fields of the type that
   make it different from other similar types.
   Then call this function and use the value.  */

tree
type_hash_canon (unsigned int hashcode, tree type)
{
  tree t1;

  /* The hash table only contains main variants, so ensure that's what we're
     being passed.  */
  gcc_assert (TYPE_MAIN_VARIANT (type) == type);

  if (!lang_hooks.types.hash_types)
    return type;

  /* See if the type is in the hash table already.  If so, return it.
     Otherwise, add the type.  */
  t1 = type_hash_lookup (hashcode, type);
  if (t1 != 0)
    {
#ifdef GATHER_STATISTICS
      tree_node_counts[(int) t_kind]--;
      tree_node_sizes[(int) t_kind] -= sizeof (struct tree_type);
#endif
      return t1;
    }
  else
    {
      type_hash_add (hashcode, type);
      return type;
    }
}

/* See if the data pointed to by the type hash table is marked.  We consider
   it marked if the type is marked or if a debug type number or symbol
   table entry has been made for the type.  This reduces the amount of
   debugging output and eliminates that dependency of the debug output on
   the number of garbage collections.  */

static int
type_hash_marked_p (const void *p)
{
  tree type = ((struct type_hash *) p)->type;

  return ggc_marked_p (type) || TYPE_SYMTAB_POINTER (type);
}

static void
print_type_hash_statistics (void)
{
  fprintf (stderr, "Type hash: size %ld, %ld elements, %f collisions\n",
	   (long) htab_size (type_hash_table),
	   (long) htab_elements (type_hash_table),
	   htab_collisions (type_hash_table));
}

/* Compute a hash code for a list of attributes (chain of TREE_LIST nodes
   with names in the TREE_PURPOSE slots and args in the TREE_VALUE slots),
   by adding the hash codes of the individual attributes.  */

unsigned int
attribute_hash_list (tree list, hashval_t hashcode)
{
  tree tail;

  for (tail = list; tail; tail = TREE_CHAIN (tail))
    /* ??? Do we want to add in TREE_VALUE too? */
    hashcode = iterative_hash_object
      (IDENTIFIER_HASH_VALUE (TREE_PURPOSE (tail)), hashcode);
  return hashcode;
}

/* Given two lists of attributes, return true if list l2 is
   equivalent to l1.  */

int
attribute_list_equal (tree l1, tree l2)
{
  return attribute_list_contained (l1, l2)
	 && attribute_list_contained (l2, l1);
}

/* Given two lists of attributes, return true if list L2 is
   completely contained within L1.  */
/* ??? This would be faster if attribute names were stored in a canonicalized
   form.  Otherwise, if L1 uses `foo' and L2 uses `__foo__', the long method
   must be used to show these elements are equivalent (which they are).  */
/* ??? It's not clear that attributes with arguments will always be handled
   correctly.  */

int
attribute_list_contained (tree l1, tree l2)
{
  tree t1, t2;

  /* First check the obvious, maybe the lists are identical.  */
  if (l1 == l2)
    return 1;

  /* Maybe the lists are similar.  */
  for (t1 = l1, t2 = l2;
       t1 != 0 && t2 != 0
        && TREE_PURPOSE (t1) == TREE_PURPOSE (t2)
        && TREE_VALUE (t1) == TREE_VALUE (t2);
       t1 = TREE_CHAIN (t1), t2 = TREE_CHAIN (t2));

  /* Maybe the lists are equal.  */
  if (t1 == 0 && t2 == 0)
    return 1;

  for (; t2 != 0; t2 = TREE_CHAIN (t2))
    {
      tree attr;
      for (attr = lookup_attribute (IDENTIFIER_POINTER (TREE_PURPOSE (t2)), l1);
	   attr != NULL_TREE;
	   attr = lookup_attribute (IDENTIFIER_POINTER (TREE_PURPOSE (t2)),
				    TREE_CHAIN (attr)))
	{
	  if (TREE_VALUE (t2) != NULL
	      && TREE_CODE (TREE_VALUE (t2)) == TREE_LIST
	      && TREE_VALUE (attr) != NULL
	      && TREE_CODE (TREE_VALUE (attr)) == TREE_LIST)
	    {
	      if (simple_cst_list_equal (TREE_VALUE (t2),
					 TREE_VALUE (attr)) == 1)
		break;
	    }
	  else if (simple_cst_equal (TREE_VALUE (t2), TREE_VALUE (attr)) == 1)
	    break;
	}

      if (attr == 0)
	return 0;
    }

  return 1;
}

/* Given two lists of types
   (chains of TREE_LIST nodes with types in the TREE_VALUE slots)
   return 1 if the lists contain the same types in the same order.
   Also, the TREE_PURPOSEs must match.  */

int
type_list_equal (tree l1, tree l2)
{
  tree t1, t2;

  for (t1 = l1, t2 = l2; t1 && t2; t1 = TREE_CHAIN (t1), t2 = TREE_CHAIN (t2))
    if (TREE_VALUE (t1) != TREE_VALUE (t2)
	|| (TREE_PURPOSE (t1) != TREE_PURPOSE (t2)
	    && ! (1 == simple_cst_equal (TREE_PURPOSE (t1), TREE_PURPOSE (t2))
		  && (TREE_TYPE (TREE_PURPOSE (t1))
		      == TREE_TYPE (TREE_PURPOSE (t2))))))
      return 0;

  return t1 == t2;
}

/* Returns the number of arguments to the FUNCTION_TYPE or METHOD_TYPE
   given by TYPE.  If the argument list accepts variable arguments,
   then this function counts only the ordinary arguments.  */

int
type_num_arguments (tree type)
{
  int i = 0;
  tree t;

  for (t = TYPE_ARG_TYPES (type); t; t = TREE_CHAIN (t))
    /* If the function does not take a variable number of arguments,
       the last element in the list will have type `void'.  */
    if (VOID_TYPE_P (TREE_VALUE (t)))
      break;
    else
      ++i;

  return i;
}

/* Nonzero if integer constants T1 and T2
   represent the same constant value.  */

int
tree_int_cst_equal (tree t1, tree t2)
{
  if (t1 == t2)
    return 1;

  if (t1 == 0 || t2 == 0)
    return 0;

  if (TREE_CODE (t1) == INTEGER_CST
      && TREE_CODE (t2) == INTEGER_CST
      && TREE_INT_CST_LOW (t1) == TREE_INT_CST_LOW (t2)
      && TREE_INT_CST_HIGH (t1) == TREE_INT_CST_HIGH (t2))
    return 1;

  return 0;
}

/* Nonzero if integer constants T1 and T2 represent values that satisfy <.
   The precise way of comparison depends on their data type.  */

int
tree_int_cst_lt (tree t1, tree t2)
{
  if (t1 == t2)
    return 0;

  if (TYPE_UNSIGNED (TREE_TYPE (t1)) != TYPE_UNSIGNED (TREE_TYPE (t2)))
    {
      int t1_sgn = tree_int_cst_sgn (t1);
      int t2_sgn = tree_int_cst_sgn (t2);

      if (t1_sgn < t2_sgn)
	return 1;
      else if (t1_sgn > t2_sgn)
	return 0;
      /* Otherwise, both are non-negative, so we compare them as
	 unsigned just in case one of them would overflow a signed
	 type.  */
    }
  else if (!TYPE_UNSIGNED (TREE_TYPE (t1)))
    return INT_CST_LT (t1, t2);

  return INT_CST_LT_UNSIGNED (t1, t2);
}

/* Returns -1 if T1 < T2, 0 if T1 == T2, and 1 if T1 > T2.  */

int
tree_int_cst_compare (tree t1, tree t2)
{
  if (tree_int_cst_lt (t1, t2))
    return -1;
  else if (tree_int_cst_lt (t2, t1))
    return 1;
  else
    return 0;
}

/* Return 1 if T is an INTEGER_CST that can be manipulated efficiently on
   the host.  If POS is zero, the value can be represented in a single
   HOST_WIDE_INT.  If POS is nonzero, the value must be non-negative and can
   be represented in a single unsigned HOST_WIDE_INT.  */

int
host_integerp (tree t, int pos)
{
  return (TREE_CODE (t) == INTEGER_CST
	  && ((TREE_INT_CST_HIGH (t) == 0
	       && (HOST_WIDE_INT) TREE_INT_CST_LOW (t) >= 0)
	      || (! pos && TREE_INT_CST_HIGH (t) == -1
		  && (HOST_WIDE_INT) TREE_INT_CST_LOW (t) < 0
		  && (!TYPE_UNSIGNED (TREE_TYPE (t))
		      || TYPE_IS_SIZETYPE (TREE_TYPE (t))))
	      || (pos && TREE_INT_CST_HIGH (t) == 0)));
}

/* Return the HOST_WIDE_INT least significant bits of T if it is an
   INTEGER_CST and there is no overflow.  POS is nonzero if the result must
   be non-negative.  We must be able to satisfy the above conditions.  */

HOST_WIDE_INT
tree_low_cst (tree t, int pos)
{
  gcc_assert (host_integerp (t, pos));
  return TREE_INT_CST_LOW (t);
}

/* Return the most significant bit of the integer constant T.  */

int
tree_int_cst_msb (tree t)
{
  int prec;
  HOST_WIDE_INT h;
  unsigned HOST_WIDE_INT l;

  /* Note that using TYPE_PRECISION here is wrong.  We care about the
     actual bits, not the (arbitrary) range of the type.  */
  prec = GET_MODE_BITSIZE (TYPE_MODE (TREE_TYPE (t))) - 1;
  rshift_double (TREE_INT_CST_LOW (t), TREE_INT_CST_HIGH (t), prec,
		 2 * HOST_BITS_PER_WIDE_INT, &l, &h, 0);
  return (l & 1) == 1;
}

/* Return an indication of the sign of the integer constant T.
   The return value is -1 if T < 0, 0 if T == 0, and 1 if T > 0.
   Note that -1 will never be returned if T's type is unsigned.  */

int
tree_int_cst_sgn (tree t)
{
  if (TREE_INT_CST_LOW (t) == 0 && TREE_INT_CST_HIGH (t) == 0)
    return 0;
  else if (TYPE_UNSIGNED (TREE_TYPE (t)))
    return 1;
  else if (TREE_INT_CST_HIGH (t) < 0)
    return -1;
  else
    return 1;
}

/* Compare two constructor-element-type constants.  Return 1 if the lists
   are known to be equal; otherwise return 0.  */

int
simple_cst_list_equal (tree l1, tree l2)
{
  while (l1 != NULL_TREE && l2 != NULL_TREE)
    {
      if (simple_cst_equal (TREE_VALUE (l1), TREE_VALUE (l2)) != 1)
	return 0;

      l1 = TREE_CHAIN (l1);
      l2 = TREE_CHAIN (l2);
    }

  return l1 == l2;
}

/* Return truthvalue of whether T1 is the same tree structure as T2.
   Return 1 if they are the same.
   Return 0 if they are understandably different.
   Return -1 if either contains tree structure not understood by
   this function.  */

int
simple_cst_equal (tree t1, tree t2)
{
  enum tree_code code1, code2;
  int cmp;
  int i;

  if (t1 == t2)
    return 1;
  if (t1 == 0 || t2 == 0)
    return 0;

  code1 = TREE_CODE (t1);
  code2 = TREE_CODE (t2);

  if (code1 == NOP_EXPR || code1 == CONVERT_EXPR || code1 == NON_LVALUE_EXPR)
    {
      if (code2 == NOP_EXPR || code2 == CONVERT_EXPR
	  || code2 == NON_LVALUE_EXPR)
	return simple_cst_equal (TREE_OPERAND (t1, 0), TREE_OPERAND (t2, 0));
      else
	return simple_cst_equal (TREE_OPERAND (t1, 0), t2);
    }

  else if (code2 == NOP_EXPR || code2 == CONVERT_EXPR
	   || code2 == NON_LVALUE_EXPR)
    return simple_cst_equal (t1, TREE_OPERAND (t2, 0));

  if (code1 != code2)
    return 0;

  switch (code1)
    {
    case INTEGER_CST:
      return (TREE_INT_CST_LOW (t1) == TREE_INT_CST_LOW (t2)
	      && TREE_INT_CST_HIGH (t1) == TREE_INT_CST_HIGH (t2));

    case REAL_CST:
      return REAL_VALUES_IDENTICAL (TREE_REAL_CST (t1), TREE_REAL_CST (t2));

    case STRING_CST:
      return (TREE_STRING_LENGTH (t1) == TREE_STRING_LENGTH (t2)
	      && ! memcmp (TREE_STRING_POINTER (t1), TREE_STRING_POINTER (t2),
			 TREE_STRING_LENGTH (t1)));

    case CONSTRUCTOR:
      {
	unsigned HOST_WIDE_INT idx;
	VEC(constructor_elt, gc) *v1 = CONSTRUCTOR_ELTS (t1);
	VEC(constructor_elt, gc) *v2 = CONSTRUCTOR_ELTS (t2);

	if (VEC_length (constructor_elt, v1) != VEC_length (constructor_elt, v2))
	  return false;

        for (idx = 0; idx < VEC_length (constructor_elt, v1); ++idx)
	  /* ??? Should we handle also fields here? */
	  if (!simple_cst_equal (VEC_index (constructor_elt, v1, idx)->value,
				 VEC_index (constructor_elt, v2, idx)->value))
	    return false;
	return true;
      }

    case SAVE_EXPR:
      return simple_cst_equal (TREE_OPERAND (t1, 0), TREE_OPERAND (t2, 0));

    case CALL_EXPR:
      cmp = simple_cst_equal (TREE_OPERAND (t1, 0), TREE_OPERAND (t2, 0));
      if (cmp <= 0)
	return cmp;
      return
	simple_cst_list_equal (TREE_OPERAND (t1, 1), TREE_OPERAND (t2, 1));

    case TARGET_EXPR:
      /* Special case: if either target is an unallocated VAR_DECL,
	 it means that it's going to be unified with whatever the
	 TARGET_EXPR is really supposed to initialize, so treat it
	 as being equivalent to anything.  */
      if ((TREE_CODE (TREE_OPERAND (t1, 0)) == VAR_DECL
	   && DECL_NAME (TREE_OPERAND (t1, 0)) == NULL_TREE
	   && !DECL_RTL_SET_P (TREE_OPERAND (t1, 0)))
	  || (TREE_CODE (TREE_OPERAND (t2, 0)) == VAR_DECL
	      && DECL_NAME (TREE_OPERAND (t2, 0)) == NULL_TREE
	      && !DECL_RTL_SET_P (TREE_OPERAND (t2, 0))))
	cmp = 1;
      else
	cmp = simple_cst_equal (TREE_OPERAND (t1, 0), TREE_OPERAND (t2, 0));

      if (cmp <= 0)
	return cmp;

      return simple_cst_equal (TREE_OPERAND (t1, 1), TREE_OPERAND (t2, 1));

    case WITH_CLEANUP_EXPR:
      cmp = simple_cst_equal (TREE_OPERAND (t1, 0), TREE_OPERAND (t2, 0));
      if (cmp <= 0)
	return cmp;

      return simple_cst_equal (TREE_OPERAND (t1, 1), TREE_OPERAND (t1, 1));

    case COMPONENT_REF:
      if (TREE_OPERAND (t1, 1) == TREE_OPERAND (t2, 1))
	return simple_cst_equal (TREE_OPERAND (t1, 0), TREE_OPERAND (t2, 0));

      return 0;

    case VAR_DECL:
    case PARM_DECL:
    case CONST_DECL:
    case FUNCTION_DECL:
      return 0;

    default:
      break;
    }

  /* This general rule works for most tree codes.  All exceptions should be
     handled above.  If this is a language-specific tree code, we can't
     trust what might be in the operand, so say we don't know
     the situation.  */
  if ((int) code1 >= (int) LAST_AND_UNUSED_TREE_CODE)
    return -1;

  switch (TREE_CODE_CLASS (code1))
    {
    case tcc_unary:
    case tcc_binary:
    case tcc_comparison:
    case tcc_expression:
    case tcc_reference:
    case tcc_statement:
      cmp = 1;
      for (i = 0; i < TREE_CODE_LENGTH (code1); i++)
	{
	  cmp = simple_cst_equal (TREE_OPERAND (t1, i), TREE_OPERAND (t2, i));
	  if (cmp <= 0)
	    return cmp;
	}

      return cmp;

    default:
      return -1;
    }
}

/* Compare the value of T, an INTEGER_CST, with U, an unsigned integer value.
   Return -1, 0, or 1 if the value of T is less than, equal to, or greater
   than U, respectively.  */

int
compare_tree_int (tree t, unsigned HOST_WIDE_INT u)
{
  if (tree_int_cst_sgn (t) < 0)
    return -1;
  else if (TREE_INT_CST_HIGH (t) != 0)
    return 1;
  else if (TREE_INT_CST_LOW (t) == u)
    return 0;
  else if (TREE_INT_CST_LOW (t) < u)
    return -1;
  else
    return 1;
}

/* Return true if CODE represents an associative tree code.  Otherwise
   return false.  */
bool
associative_tree_code (enum tree_code code)
{
  switch (code)
    {
    case BIT_IOR_EXPR:
    case BIT_AND_EXPR:
    case BIT_XOR_EXPR:
    case PLUS_EXPR:
    case MULT_EXPR:
    case MIN_EXPR:
    case MAX_EXPR:
      return true;

    default:
      break;
    }
  return false;
}

/* Return true if CODE represents a commutative tree code.  Otherwise
   return false.  */
bool
commutative_tree_code (enum tree_code code)
{
  switch (code)
    {
    case PLUS_EXPR:
    case MULT_EXPR:
    case MIN_EXPR:
    case MAX_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case BIT_AND_EXPR:
    case NE_EXPR:
    case EQ_EXPR:
    case UNORDERED_EXPR:
    case ORDERED_EXPR:
    case UNEQ_EXPR:
    case LTGT_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_XOR_EXPR:
    case TRUTH_OR_EXPR:
      return true;

    default:
      break;
    }
  return false;
}

/* Generate a hash value for an expression.  This can be used iteratively
   by passing a previous result as the "val" argument.

   This function is intended to produce the same hash for expressions which
   would compare equal using operand_equal_p.  */

hashval_t
iterative_hash_expr (tree t, hashval_t val)
{
  int i;
  enum tree_code code;
  char class;

  if (t == NULL_TREE)
    return iterative_hash_pointer (t, val);

  code = TREE_CODE (t);

  switch (code)
    {
    /* Alas, constants aren't shared, so we can't rely on pointer
       identity.  */
    case INTEGER_CST:
      val = iterative_hash_host_wide_int (TREE_INT_CST_LOW (t), val);
      return iterative_hash_host_wide_int (TREE_INT_CST_HIGH (t), val);
    case REAL_CST:
      {
	unsigned int val2 = real_hash (TREE_REAL_CST_PTR (t));

	return iterative_hash_hashval_t (val2, val);
      }
    case STRING_CST:
      return iterative_hash (TREE_STRING_POINTER (t),
			     TREE_STRING_LENGTH (t), val);
    case COMPLEX_CST:
      val = iterative_hash_expr (TREE_REALPART (t), val);
      return iterative_hash_expr (TREE_IMAGPART (t), val);
    case VECTOR_CST:
      return iterative_hash_expr (TREE_VECTOR_CST_ELTS (t), val);

    case SSA_NAME:
    case VALUE_HANDLE:
      /* we can just compare by pointer.  */
      return iterative_hash_pointer (t, val);

    case TREE_LIST:
      /* A list of expressions, for a CALL_EXPR or as the elements of a
	 VECTOR_CST.  */
      for (; t; t = TREE_CHAIN (t))
	val = iterative_hash_expr (TREE_VALUE (t), val);
      return val;
    case CONSTRUCTOR:
      {
	unsigned HOST_WIDE_INT idx;
	tree field, value;
	FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (t), idx, field, value)
	  {
	    val = iterative_hash_expr (field, val);
	    val = iterative_hash_expr (value, val);
	  }
	return val;
      }
    case FUNCTION_DECL:
      /* When referring to a built-in FUNCTION_DECL, use the
	 __builtin__ form.  Otherwise nodes that compare equal
	 according to operand_equal_p might get different
	 hash codes.  */
      if (DECL_BUILT_IN (t))
	{
	  val = iterative_hash_pointer (built_in_decls[DECL_FUNCTION_CODE (t)], 
				      val);
	  return val;
	}
      /* else FALL THROUGH */
    default:
      class = TREE_CODE_CLASS (code);

      if (class == tcc_declaration)
	{
	  /* DECL's have a unique ID */
	  val = iterative_hash_host_wide_int (DECL_UID (t), val);
	}
      else
	{
	  gcc_assert (IS_EXPR_CODE_CLASS (class));
	  
	  val = iterative_hash_object (code, val);

	  /* Don't hash the type, that can lead to having nodes which
	     compare equal according to operand_equal_p, but which
	     have different hash codes.  */
	  if (code == NOP_EXPR
	      || code == CONVERT_EXPR
	      || code == NON_LVALUE_EXPR)
	    {
	      /* Make sure to include signness in the hash computation.  */
	      val += TYPE_UNSIGNED (TREE_TYPE (t));
	      val = iterative_hash_expr (TREE_OPERAND (t, 0), val);
	    }

	  else if (commutative_tree_code (code))
	    {
	      /* It's a commutative expression.  We want to hash it the same
		 however it appears.  We do this by first hashing both operands
		 and then rehashing based on the order of their independent
		 hashes.  */
	      hashval_t one = iterative_hash_expr (TREE_OPERAND (t, 0), 0);
	      hashval_t two = iterative_hash_expr (TREE_OPERAND (t, 1), 0);
	      hashval_t t;

	      if (one > two)
		t = one, one = two, two = t;

	      val = iterative_hash_hashval_t (one, val);
	      val = iterative_hash_hashval_t (two, val);
	    }
	  else
	    for (i = TREE_CODE_LENGTH (code) - 1; i >= 0; --i)
	      val = iterative_hash_expr (TREE_OPERAND (t, i), val);
	}
      return val;
      break;
    }
}

/* Constructors for pointer, array and function types.
   (RECORD_TYPE, UNION_TYPE and ENUMERAL_TYPE nodes are
   constructed by language-dependent code, not here.)  */

/* Construct, lay out and return the type of pointers to TO_TYPE with
   mode MODE.  If CAN_ALIAS_ALL is TRUE, indicate this type can
   reference all of memory. If such a type has already been
   constructed, reuse it.  */

tree
build_pointer_type_for_mode (tree to_type, enum machine_mode mode,
			     bool can_alias_all)
{
  tree t;

  if (to_type == error_mark_node)
    return error_mark_node;

  /* In some cases, languages will have things that aren't a POINTER_TYPE
     (such as a RECORD_TYPE for fat pointers in Ada) as TYPE_POINTER_TO.
     In that case, return that type without regard to the rest of our
     operands.

     ??? This is a kludge, but consistent with the way this function has
     always operated and there doesn't seem to be a good way to avoid this
     at the moment.  */
  if (TYPE_POINTER_TO (to_type) != 0
      && TREE_CODE (TYPE_POINTER_TO (to_type)) != POINTER_TYPE)
    return TYPE_POINTER_TO (to_type);

  /* First, if we already have a type for pointers to TO_TYPE and it's
     the proper mode, use it.  */
  for (t = TYPE_POINTER_TO (to_type); t; t = TYPE_NEXT_PTR_TO (t))
    if (TYPE_MODE (t) == mode && TYPE_REF_CAN_ALIAS_ALL (t) == can_alias_all)
      return t;

  t = make_node (POINTER_TYPE);

  TREE_TYPE (t) = to_type;
  TYPE_MODE (t) = mode;
  TYPE_REF_CAN_ALIAS_ALL (t) = can_alias_all;
  TYPE_NEXT_PTR_TO (t) = TYPE_POINTER_TO (to_type);
  TYPE_POINTER_TO (to_type) = t;

  /* Lay out the type.  This function has many callers that are concerned
     with expression-construction, and this simplifies them all.  */
  layout_type (t);

  return t;
}

/* By default build pointers in ptr_mode.  */

tree
build_pointer_type (tree to_type)
{
  return build_pointer_type_for_mode (to_type, ptr_mode, false);
}

/* APPLE LOCAL begin radar 5732232 - blocks */
tree
build_block_pointer_type (tree to_type)
{
  tree t;

  /* APPLE LOCAL begin radar 6300081 & 6353006 */
  if (!generic_block_literal_struct_type)
      generic_block_literal_struct_type = 
      lang_hooks.build_generic_block_struct_type ();
  /* APPLE LOCAL end radar 6300081 & 6353006 */

  t = make_node (BLOCK_POINTER_TYPE);

  TREE_TYPE (t) = to_type;
  TYPE_MODE (t) = ptr_mode;

  /* Lay out the type.  This function has many callers that are concerned
     with expression-construction, and this simplifies them all.  */
  layout_type (t);

  return t;
}
/* APPLE LOCAL end radar 5732232 - blocks */

/* Same as build_pointer_type_for_mode, but for REFERENCE_TYPE.  */

tree
build_reference_type_for_mode (tree to_type, enum machine_mode mode,
			       bool can_alias_all)
{
  tree t;

  /* In some cases, languages will have things that aren't a REFERENCE_TYPE
     (such as a RECORD_TYPE for fat pointers in Ada) as TYPE_REFERENCE_TO.
     In that case, return that type without regard to the rest of our
     operands.

     ??? This is a kludge, but consistent with the way this function has
     always operated and there doesn't seem to be a good way to avoid this
     at the moment.  */
  if (TYPE_REFERENCE_TO (to_type) != 0
      && TREE_CODE (TYPE_REFERENCE_TO (to_type)) != REFERENCE_TYPE)
    return TYPE_REFERENCE_TO (to_type);

  /* First, if we already have a type for pointers to TO_TYPE and it's
     the proper mode, use it.  */
  for (t = TYPE_REFERENCE_TO (to_type); t; t = TYPE_NEXT_REF_TO (t))
    if (TYPE_MODE (t) == mode && TYPE_REF_CAN_ALIAS_ALL (t) == can_alias_all)
      return t;

  t = make_node (REFERENCE_TYPE);

  TREE_TYPE (t) = to_type;
  TYPE_MODE (t) = mode;
  TYPE_REF_CAN_ALIAS_ALL (t) = can_alias_all;
  TYPE_NEXT_REF_TO (t) = TYPE_REFERENCE_TO (to_type);
  TYPE_REFERENCE_TO (to_type) = t;

  layout_type (t);

  return t;
}


/* Build the node for the type of references-to-TO_TYPE by default
   in ptr_mode.  */

tree
build_reference_type (tree to_type)
{
  return build_reference_type_for_mode (to_type, ptr_mode, false);
}

/* Build a type that is compatible with t but has no cv quals anywhere
   in its type, thus

   const char *const *const *  ->  char ***.  */

tree
build_type_no_quals (tree t)
{
  switch (TREE_CODE (t))
    {
    case POINTER_TYPE:
      return build_pointer_type_for_mode (build_type_no_quals (TREE_TYPE (t)),
					  TYPE_MODE (t),
					  TYPE_REF_CAN_ALIAS_ALL (t));
    case REFERENCE_TYPE:
      return
	build_reference_type_for_mode (build_type_no_quals (TREE_TYPE (t)),
				       TYPE_MODE (t),
				       TYPE_REF_CAN_ALIAS_ALL (t));
    default:
      return TYPE_MAIN_VARIANT (t);
    }
}

/* Create a type of integers to be the TYPE_DOMAIN of an ARRAY_TYPE.
   MAXVAL should be the maximum value in the domain
   (one less than the length of the array).

   The maximum value that MAXVAL can have is INT_MAX for a HOST_WIDE_INT.
   We don't enforce this limit, that is up to caller (e.g. language front end).
   The limit exists because the result is a signed type and we don't handle
   sizes that use more than one HOST_WIDE_INT.  */

tree
build_index_type (tree maxval)
{
  tree itype = make_node (INTEGER_TYPE);

  TREE_TYPE (itype) = sizetype;
  TYPE_PRECISION (itype) = TYPE_PRECISION (sizetype);
  TYPE_MIN_VALUE (itype) = size_zero_node;
  TYPE_MAX_VALUE (itype) = fold_convert (sizetype, maxval);
  TYPE_MODE (itype) = TYPE_MODE (sizetype);
  TYPE_SIZE (itype) = TYPE_SIZE (sizetype);
  TYPE_SIZE_UNIT (itype) = TYPE_SIZE_UNIT (sizetype);
  TYPE_ALIGN (itype) = TYPE_ALIGN (sizetype);
  TYPE_USER_ALIGN (itype) = TYPE_USER_ALIGN (sizetype);

  if (host_integerp (maxval, 1))
    return type_hash_canon (tree_low_cst (maxval, 1), itype);
  else
    return itype;
}

/* Builds a signed or unsigned integer type of precision PRECISION.
   Used for C bitfields whose precision does not match that of
   built-in target types.  */
tree
build_nonstandard_integer_type (unsigned HOST_WIDE_INT precision,
				int unsignedp)
{
  tree itype = make_node (INTEGER_TYPE);

  TYPE_PRECISION (itype) = precision;

  if (unsignedp)
    fixup_unsigned_type (itype);
  else
    fixup_signed_type (itype);

  if (host_integerp (TYPE_MAX_VALUE (itype), 1))
    return type_hash_canon (tree_low_cst (TYPE_MAX_VALUE (itype), 1), itype);

  return itype;
}

/* Create a range of some discrete type TYPE (an INTEGER_TYPE,
   ENUMERAL_TYPE or BOOLEAN_TYPE), with low bound LOWVAL and
   high bound HIGHVAL.  If TYPE is NULL, sizetype is used.  */

tree
build_range_type (tree type, tree lowval, tree highval)
{
  tree itype = make_node (INTEGER_TYPE);

  TREE_TYPE (itype) = type;
  if (type == NULL_TREE)
    type = sizetype;

  TYPE_MIN_VALUE (itype) = fold_convert (type, lowval);
  TYPE_MAX_VALUE (itype) = highval ? fold_convert (type, highval) : NULL;

  TYPE_PRECISION (itype) = TYPE_PRECISION (type);
  TYPE_MODE (itype) = TYPE_MODE (type);
  TYPE_SIZE (itype) = TYPE_SIZE (type);
  TYPE_SIZE_UNIT (itype) = TYPE_SIZE_UNIT (type);
  TYPE_ALIGN (itype) = TYPE_ALIGN (type);
  TYPE_USER_ALIGN (itype) = TYPE_USER_ALIGN (type);

  if (host_integerp (lowval, 0) && highval != 0 && host_integerp (highval, 0))
    return type_hash_canon (tree_low_cst (highval, 0)
			    - tree_low_cst (lowval, 0),
			    itype);
  else
    return itype;
}

/* Just like build_index_type, but takes lowval and highval instead
   of just highval (maxval).  */

tree
build_index_2_type (tree lowval, tree highval)
{
  return build_range_type (sizetype, lowval, highval);
}

/* Construct, lay out and return the type of arrays of elements with ELT_TYPE
   and number of elements specified by the range of values of INDEX_TYPE.
   If such a type has already been constructed, reuse it.  */

tree
build_array_type (tree elt_type, tree index_type)
{
  tree t;
  hashval_t hashcode = 0;

  if (TREE_CODE (elt_type) == FUNCTION_TYPE)
    {
      error ("arrays of functions are not meaningful");
      elt_type = integer_type_node;
    }

  t = make_node (ARRAY_TYPE);
  TREE_TYPE (t) = elt_type;
  TYPE_DOMAIN (t) = index_type;
  
  if (index_type == 0)
    {
      tree save = t;
      hashcode = iterative_hash_object (TYPE_HASH (elt_type), hashcode);
      t = type_hash_canon (hashcode, t);
      if (save == t)
	layout_type (t);
      return t;
    }

  hashcode = iterative_hash_object (TYPE_HASH (elt_type), hashcode);
  hashcode = iterative_hash_object (TYPE_HASH (index_type), hashcode);
  t = type_hash_canon (hashcode, t);

  if (!COMPLETE_TYPE_P (t))
    layout_type (t);
  return t;
}

/* Return the TYPE of the elements comprising
   the innermost dimension of ARRAY.  */

tree
get_inner_array_type (tree array)
{
  tree type = TREE_TYPE (array);

  while (TREE_CODE (type) == ARRAY_TYPE)
    type = TREE_TYPE (type);

  return type;
}

/* Construct, lay out and return
   the type of functions returning type VALUE_TYPE
   given arguments of types ARG_TYPES.
   ARG_TYPES is a chain of TREE_LIST nodes whose TREE_VALUEs
   are data type nodes for the arguments of the function.
   If such a type has already been constructed, reuse it.  */

tree
build_function_type (tree value_type, tree arg_types)
{
  tree t;
  hashval_t hashcode = 0;

  if (TREE_CODE (value_type) == FUNCTION_TYPE)
    {
      error ("function return type cannot be function");
      value_type = integer_type_node;
    }

  /* Make a node of the sort we want.  */
  t = make_node (FUNCTION_TYPE);
  TREE_TYPE (t) = value_type;
  TYPE_ARG_TYPES (t) = arg_types;

  /* If we already have such a type, use the old one.  */
  hashcode = iterative_hash_object (TYPE_HASH (value_type), hashcode);
  hashcode = type_hash_list (arg_types, hashcode);
  t = type_hash_canon (hashcode, t);

  if (!COMPLETE_TYPE_P (t))
    layout_type (t);
  return t;
}

/* Build a function type.  The RETURN_TYPE is the type returned by the
   function.  If additional arguments are provided, they are
   additional argument types.  The list of argument types must always
   be terminated by NULL_TREE.  */

tree
build_function_type_list (tree return_type, ...)
{
  tree t, args, last;
  va_list p;

  va_start (p, return_type);

  t = va_arg (p, tree);
  for (args = NULL_TREE; t != NULL_TREE; t = va_arg (p, tree))
    args = tree_cons (NULL_TREE, t, args);

  if (args == NULL_TREE)
    args = void_list_node;
  else
    {
      last = args;
      args = nreverse (args);
      TREE_CHAIN (last) = void_list_node;
    }
  args = build_function_type (return_type, args);

  va_end (p);
  return args;
}

/* Build a METHOD_TYPE for a member of BASETYPE.  The RETTYPE (a TYPE)
   and ARGTYPES (a TREE_LIST) are the return type and arguments types
   for the method.  An implicit additional parameter (of type
   pointer-to-BASETYPE) is added to the ARGTYPES.  */

tree
build_method_type_directly (tree basetype,
			    tree rettype,
			    tree argtypes)
{
  tree t;
  tree ptype;
  int hashcode = 0;

  /* Make a node of the sort we want.  */
  t = make_node (METHOD_TYPE);

  TYPE_METHOD_BASETYPE (t) = TYPE_MAIN_VARIANT (basetype);
  TREE_TYPE (t) = rettype;
  ptype = build_pointer_type (basetype);

  /* The actual arglist for this function includes a "hidden" argument
     which is "this".  Put it into the list of argument types.  */
  argtypes = tree_cons (NULL_TREE, ptype, argtypes);
  TYPE_ARG_TYPES (t) = argtypes;

  /* If we already have such a type, use the old one.  */
  hashcode = iterative_hash_object (TYPE_HASH (basetype), hashcode);
  hashcode = iterative_hash_object (TYPE_HASH (rettype), hashcode);
  hashcode = type_hash_list (argtypes, hashcode);
  t = type_hash_canon (hashcode, t);

  if (!COMPLETE_TYPE_P (t))
    layout_type (t);

  return t;
}

/* Construct, lay out and return the type of methods belonging to class
   BASETYPE and whose arguments and values are described by TYPE.
   If that type exists already, reuse it.
   TYPE must be a FUNCTION_TYPE node.  */

tree
build_method_type (tree basetype, tree type)
{
  gcc_assert (TREE_CODE (type) == FUNCTION_TYPE);

  return build_method_type_directly (basetype,
				     TREE_TYPE (type),
				     TYPE_ARG_TYPES (type));
}

/* Construct, lay out and return the type of offsets to a value
   of type TYPE, within an object of type BASETYPE.
   If a suitable offset type exists already, reuse it.  */

tree
build_offset_type (tree basetype, tree type)
{
  tree t;
  hashval_t hashcode = 0;

  /* Make a node of the sort we want.  */
  t = make_node (OFFSET_TYPE);

  TYPE_OFFSET_BASETYPE (t) = TYPE_MAIN_VARIANT (basetype);
  TREE_TYPE (t) = type;

  /* If we already have such a type, use the old one.  */
  hashcode = iterative_hash_object (TYPE_HASH (basetype), hashcode);
  hashcode = iterative_hash_object (TYPE_HASH (type), hashcode);
  t = type_hash_canon (hashcode, t);

  if (!COMPLETE_TYPE_P (t))
    layout_type (t);

  return t;
}

/* Create a complex type whose components are COMPONENT_TYPE.  */

tree
build_complex_type (tree component_type)
{
  tree t;
  hashval_t hashcode;

  /* Make a node of the sort we want.  */
  t = make_node (COMPLEX_TYPE);

  TREE_TYPE (t) = TYPE_MAIN_VARIANT (component_type);

  /* If we already have such a type, use the old one.  */
  hashcode = iterative_hash_object (TYPE_HASH (component_type), 0);
  t = type_hash_canon (hashcode, t);

  if (!COMPLETE_TYPE_P (t))
    layout_type (t);

  /* If we are writing Dwarf2 output we need to create a name,
     since complex is a fundamental type.  */
  if ((write_symbols == DWARF2_DEBUG || write_symbols == VMS_AND_DWARF2_DEBUG)
      && ! TYPE_NAME (t))
    {
      const char *name;
      if (component_type == char_type_node)
	name = "complex char";
      else if (component_type == signed_char_type_node)
	name = "complex signed char";
      else if (component_type == unsigned_char_type_node)
	name = "complex unsigned char";
      else if (component_type == short_integer_type_node)
	name = "complex short int";
      else if (component_type == short_unsigned_type_node)
	name = "complex short unsigned int";
      else if (component_type == integer_type_node)
	name = "complex int";
      else if (component_type == unsigned_type_node)
	name = "complex unsigned int";
      else if (component_type == long_integer_type_node)
	name = "complex long int";
      else if (component_type == long_unsigned_type_node)
	name = "complex long unsigned int";
      else if (component_type == long_long_integer_type_node)
	name = "complex long long int";
      else if (component_type == long_long_unsigned_type_node)
	name = "complex long long unsigned int";
      else
	name = 0;

      if (name != 0)
	TYPE_NAME (t) = get_identifier (name);
    }

  return build_qualified_type (t, TYPE_QUALS (component_type));
}

/* Return OP, stripped of any conversions to wider types as much as is safe.
   Converting the value back to OP's type makes a value equivalent to OP.

   If FOR_TYPE is nonzero, we return a value which, if converted to
   type FOR_TYPE, would be equivalent to converting OP to type FOR_TYPE.

   If FOR_TYPE is nonzero, unaligned bit-field references may be changed to the
   narrowest type that can hold the value, even if they don't exactly fit.
   Otherwise, bit-field references are changed to a narrower type
   only if they can be fetched directly from memory in that type.

   OP must have integer, real or enumeral type.  Pointers are not allowed!

   There are some cases where the obvious value we could return
   would regenerate to OP if converted to OP's type,
   but would not extend like OP to wider types.
   If FOR_TYPE indicates such extension is contemplated, we eschew such values.
   For example, if OP is (unsigned short)(signed char)-1,
   we avoid returning (signed char)-1 if FOR_TYPE is int,
   even though extending that to an unsigned short would regenerate OP,
   since the result of extending (signed char)-1 to (int)
   is different from (int) OP.  */

tree
get_unwidened (tree op, tree for_type)
{
  /* Set UNS initially if converting OP to FOR_TYPE is a zero-extension.  */
  tree type = TREE_TYPE (op);
  unsigned final_prec
    = TYPE_PRECISION (for_type != 0 ? for_type : type);
  int uns
    = (for_type != 0 && for_type != type
       && final_prec > TYPE_PRECISION (type)
       && TYPE_UNSIGNED (type));
  tree win = op;

  while (TREE_CODE (op) == NOP_EXPR
	 || TREE_CODE (op) == CONVERT_EXPR)
    {
      int bitschange;

      /* TYPE_PRECISION on vector types has different meaning
	 (TYPE_VECTOR_SUBPARTS) and casts from vectors are view conversions,
	 so avoid them here.  */
      if (TREE_CODE (TREE_TYPE (TREE_OPERAND (op, 0))) == VECTOR_TYPE)
	break;

      bitschange = TYPE_PRECISION (TREE_TYPE (op))
		   - TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (op, 0)));

      /* Truncations are many-one so cannot be removed.
	 Unless we are later going to truncate down even farther.  */
      if (bitschange < 0
	  && final_prec > TYPE_PRECISION (TREE_TYPE (op)))
	break;

      /* See what's inside this conversion.  If we decide to strip it,
	 we will set WIN.  */
      op = TREE_OPERAND (op, 0);

      /* If we have not stripped any zero-extensions (uns is 0),
	 we can strip any kind of extension.
	 If we have previously stripped a zero-extension,
	 only zero-extensions can safely be stripped.
	 Any extension can be stripped if the bits it would produce
	 are all going to be discarded later by truncating to FOR_TYPE.  */

      if (bitschange > 0)
	{
	  if (! uns || final_prec <= TYPE_PRECISION (TREE_TYPE (op)))
	    win = op;
	  /* TYPE_UNSIGNED says whether this is a zero-extension.
	     Let's avoid computing it if it does not affect WIN
	     and if UNS will not be needed again.  */
	  if ((uns
	       || TREE_CODE (op) == NOP_EXPR
	       || TREE_CODE (op) == CONVERT_EXPR)
	      && TYPE_UNSIGNED (TREE_TYPE (op)))
	    {
	      uns = 1;
	      win = op;
	    }
	}
    }

  if (TREE_CODE (op) == COMPONENT_REF
      /* Since type_for_size always gives an integer type.  */
      && TREE_CODE (type) != REAL_TYPE
      /* Don't crash if field not laid out yet.  */
      && DECL_SIZE (TREE_OPERAND (op, 1)) != 0
      && host_integerp (DECL_SIZE (TREE_OPERAND (op, 1)), 1))
    {
      unsigned int innerprec
	= tree_low_cst (DECL_SIZE (TREE_OPERAND (op, 1)), 1);
      int unsignedp = (DECL_UNSIGNED (TREE_OPERAND (op, 1))
		       || TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (op, 1))));
      type = lang_hooks.types.type_for_size (innerprec, unsignedp);

      /* We can get this structure field in the narrowest type it fits in.
	 If FOR_TYPE is 0, do this only for a field that matches the
	 narrower type exactly and is aligned for it
	 The resulting extension to its nominal type (a fullword type)
	 must fit the same conditions as for other extensions.  */

      if (type != 0
	  && INT_CST_LT_UNSIGNED (TYPE_SIZE (type), TYPE_SIZE (TREE_TYPE (op)))
	  && (for_type || ! DECL_BIT_FIELD (TREE_OPERAND (op, 1)))
	  && (! uns || final_prec <= innerprec || unsignedp))
	{
	  win = build3 (COMPONENT_REF, type, TREE_OPERAND (op, 0),
			TREE_OPERAND (op, 1), NULL_TREE);
	  TREE_SIDE_EFFECTS (win) = TREE_SIDE_EFFECTS (op);
	  TREE_THIS_VOLATILE (win) = TREE_THIS_VOLATILE (op);
	}
    }

  return win;
}

/* Return OP or a simpler expression for a narrower value
   which can be sign-extended or zero-extended to give back OP.
   Store in *UNSIGNEDP_PTR either 1 if the value should be zero-extended
   or 0 if the value should be sign-extended.  */

tree
get_narrower (tree op, int *unsignedp_ptr)
{
  int uns = 0;
  int first = 1;
  tree win = op;
  bool integral_p = INTEGRAL_TYPE_P (TREE_TYPE (op));

  while (TREE_CODE (op) == NOP_EXPR)
    {
      int bitschange
	= (TYPE_PRECISION (TREE_TYPE (op))
	   - TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (op, 0))));

      /* Truncations are many-one so cannot be removed.  */
      if (bitschange < 0)
	break;

      /* See what's inside this conversion.  If we decide to strip it,
	 we will set WIN.  */

      if (bitschange > 0)
	{
	  op = TREE_OPERAND (op, 0);
	  /* An extension: the outermost one can be stripped,
	     but remember whether it is zero or sign extension.  */
	  if (first)
	    uns = TYPE_UNSIGNED (TREE_TYPE (op));
	  /* Otherwise, if a sign extension has been stripped,
	     only sign extensions can now be stripped;
	     if a zero extension has been stripped, only zero-extensions.  */
	  else if (uns != TYPE_UNSIGNED (TREE_TYPE (op)))
	    break;
	  first = 0;
	}
      else /* bitschange == 0 */
	{
	  /* A change in nominal type can always be stripped, but we must
	     preserve the unsignedness.  */
	  if (first)
	    uns = TYPE_UNSIGNED (TREE_TYPE (op));
	  first = 0;
	  op = TREE_OPERAND (op, 0);
	  /* Keep trying to narrow, but don't assign op to win if it
	     would turn an integral type into something else.  */
	  if (INTEGRAL_TYPE_P (TREE_TYPE (op)) != integral_p)
	    continue;
	}

      win = op;
    }

  if (TREE_CODE (op) == COMPONENT_REF
      /* Since type_for_size always gives an integer type.  */
      && TREE_CODE (TREE_TYPE (op)) != REAL_TYPE
      /* Ensure field is laid out already.  */
      && DECL_SIZE (TREE_OPERAND (op, 1)) != 0
      && host_integerp (DECL_SIZE (TREE_OPERAND (op, 1)), 1))
    {
      unsigned HOST_WIDE_INT innerprec
	= tree_low_cst (DECL_SIZE (TREE_OPERAND (op, 1)), 1);
      int unsignedp = (DECL_UNSIGNED (TREE_OPERAND (op, 1))
		       || TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (op, 1))));
      tree type = lang_hooks.types.type_for_size (innerprec, unsignedp);

      /* We can get this structure field in a narrower type that fits it,
	 but the resulting extension to its nominal type (a fullword type)
	 must satisfy the same conditions as for other extensions.

	 Do this only for fields that are aligned (not bit-fields),
	 because when bit-field insns will be used there is no
	 advantage in doing this.  */

      if (innerprec < TYPE_PRECISION (TREE_TYPE (op))
	  && ! DECL_BIT_FIELD (TREE_OPERAND (op, 1))
	  && (first || uns == DECL_UNSIGNED (TREE_OPERAND (op, 1)))
	  && type != 0)
	{
	  if (first)
	    uns = DECL_UNSIGNED (TREE_OPERAND (op, 1));
	  win = fold_convert (type, op);
	}
    }

  *unsignedp_ptr = uns;
  return win;
}

/* Nonzero if integer constant C has a value that is permissible
   for type TYPE (an INTEGER_TYPE).  */

int
int_fits_type_p (tree c, tree type)
{
  tree type_low_bound = TYPE_MIN_VALUE (type);
  tree type_high_bound = TYPE_MAX_VALUE (type);
  bool ok_for_low_bound, ok_for_high_bound;
  tree tmp;

  /* If at least one bound of the type is a constant integer, we can check
     ourselves and maybe make a decision. If no such decision is possible, but
     this type is a subtype, try checking against that.  Otherwise, use
     force_fit_type, which checks against the precision.

     Compute the status for each possibly constant bound, and return if we see
     one does not match. Use ok_for_xxx_bound for this purpose, assigning -1
     for "unknown if constant fits", 0 for "constant known *not* to fit" and 1
     for "constant known to fit".  */

  /* Check if C >= type_low_bound.  */
  if (type_low_bound && TREE_CODE (type_low_bound) == INTEGER_CST)
    {
      if (tree_int_cst_lt (c, type_low_bound))
	return 0;
      ok_for_low_bound = true;
    }
  else
    ok_for_low_bound = false;

  /* Check if c <= type_high_bound.  */
  if (type_high_bound && TREE_CODE (type_high_bound) == INTEGER_CST)
    {
      if (tree_int_cst_lt (type_high_bound, c))
	return 0;
      ok_for_high_bound = true;
    }
  else
    ok_for_high_bound = false;

  /* If the constant fits both bounds, the result is known.  */
  if (ok_for_low_bound && ok_for_high_bound)
    return 1;

  /* Perform some generic filtering which may allow making a decision
     even if the bounds are not constant.  First, negative integers
     never fit in unsigned types, */
  if (TYPE_UNSIGNED (type) && tree_int_cst_sgn (c) < 0)
    return 0;

  /* Second, narrower types always fit in wider ones.  */
  if (TYPE_PRECISION (type) > TYPE_PRECISION (TREE_TYPE (c)))
    return 1;

  /* Third, unsigned integers with top bit set never fit signed types.  */
  if (! TYPE_UNSIGNED (type)
      && TYPE_UNSIGNED (TREE_TYPE (c))
      && tree_int_cst_msb (c))
    return 0;

  /* If we haven't been able to decide at this point, there nothing more we
     can check ourselves here.  Look at the base type if we have one and it
     has the same precision.  */
  if (TREE_CODE (type) == INTEGER_TYPE
      && TREE_TYPE (type) != 0
      && TYPE_PRECISION (type) == TYPE_PRECISION (TREE_TYPE (type)))
    return int_fits_type_p (c, TREE_TYPE (type));

  /* Or to force_fit_type, if nothing else.  */
  tmp = copy_node (c);
  TREE_TYPE (tmp) = type;
  tmp = force_fit_type (tmp, -1, false, false);
  return TREE_INT_CST_HIGH (tmp) == TREE_INT_CST_HIGH (c)
         && TREE_INT_CST_LOW (tmp) == TREE_INT_CST_LOW (c);
}

/* Subprogram of following function.  Called by walk_tree.

   Return *TP if it is an automatic variable or parameter of the
   function passed in as DATA.  */

static tree
find_var_from_fn (tree *tp, int *walk_subtrees, void *data)
{
  tree fn = (tree) data;

  if (TYPE_P (*tp))
    *walk_subtrees = 0;

  else if (DECL_P (*tp)
	   && lang_hooks.tree_inlining.auto_var_in_fn_p (*tp, fn))
    return *tp;

  return NULL_TREE;
}

/* Returns true if T is, contains, or refers to a type with variable
   size.  For METHOD_TYPEs and FUNCTION_TYPEs we exclude the
   arguments, but not the return type.  If FN is nonzero, only return
   true if a modifier of the type or position of FN is a variable or
   parameter inside FN.

   This concept is more general than that of C99 'variably modified types':
   in C99, a struct type is never variably modified because a VLA may not
   appear as a structure member.  However, in GNU C code like:

     struct S { int i[f()]; };

   is valid, and other languages may define similar constructs.  */

bool
variably_modified_type_p (tree type, tree fn)
{
  tree t;

/* Test if T is either variable (if FN is zero) or an expression containing
   a variable in FN.  */
#define RETURN_TRUE_IF_VAR(T)						\
  do { tree _t = (T);							\
    if (_t && _t != error_mark_node && TREE_CODE (_t) != INTEGER_CST	\
        && (!fn || walk_tree (&_t, find_var_from_fn, fn, NULL)))	\
      return true;  } while (0)

  if (type == error_mark_node)
    return false;

  /* If TYPE itself has variable size, it is variably modified.  */
  RETURN_TRUE_IF_VAR (TYPE_SIZE (type));
  RETURN_TRUE_IF_VAR (TYPE_SIZE_UNIT (type));

  switch (TREE_CODE (type))
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
    case VECTOR_TYPE:
      if (variably_modified_type_p (TREE_TYPE (type), fn))
	return true;
      break;

    case FUNCTION_TYPE:
    case METHOD_TYPE:
      /* If TYPE is a function type, it is variably modified if the
	 return type is variably modified.  */
      if (variably_modified_type_p (TREE_TYPE (type), fn))
	  return true;
      break;

    case INTEGER_TYPE:
    case REAL_TYPE:
    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
      /* Scalar types are variably modified if their end points
	 aren't constant.  */
      RETURN_TRUE_IF_VAR (TYPE_MIN_VALUE (type));
      RETURN_TRUE_IF_VAR (TYPE_MAX_VALUE (type));
      break;

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      /* We can't see if any of the fields are variably-modified by the
	 definition we normally use, since that would produce infinite
	 recursion via pointers.  */
      /* This is variably modified if some field's type is.  */
      for (t = TYPE_FIELDS (type); t; t = TREE_CHAIN (t))
	if (TREE_CODE (t) == FIELD_DECL)
	  {
	    RETURN_TRUE_IF_VAR (DECL_FIELD_OFFSET (t));
	    RETURN_TRUE_IF_VAR (DECL_SIZE (t));
	    RETURN_TRUE_IF_VAR (DECL_SIZE_UNIT (t));

	    if (TREE_CODE (type) == QUAL_UNION_TYPE)
	      RETURN_TRUE_IF_VAR (DECL_QUALIFIER (t));
	  }
	break;

    case ARRAY_TYPE:
      /* Do not call ourselves to avoid infinite recursion.  This is
	 variably modified if the element type is.  */
      RETURN_TRUE_IF_VAR (TYPE_SIZE (TREE_TYPE (type)));
      RETURN_TRUE_IF_VAR (TYPE_SIZE_UNIT (TREE_TYPE (type)));
      break;

    default:
      break;
    }

  /* The current language may have other cases to check, but in general,
     all other types are not variably modified.  */
  return lang_hooks.tree_inlining.var_mod_type_p (type, fn);

#undef RETURN_TRUE_IF_VAR
}

/* Given a DECL or TYPE, return the scope in which it was declared, or
   NULL_TREE if there is no containing scope.  */

tree
get_containing_scope (tree t)
{
  return (TYPE_P (t) ? TYPE_CONTEXT (t) : DECL_CONTEXT (t));
}

/* Return the innermost context enclosing DECL that is
   a FUNCTION_DECL, or zero if none.  */

tree
decl_function_context (tree decl)
{
  tree context;

  if (TREE_CODE (decl) == ERROR_MARK)
    return 0;

  /* C++ virtual functions use DECL_CONTEXT for the class of the vtable
     where we look up the function at runtime.  Such functions always take
     a first argument of type 'pointer to real context'.

     C++ should really be fixed to use DECL_CONTEXT for the real context,
     and use something else for the "virtual context".  */
  else if (TREE_CODE (decl) == FUNCTION_DECL && DECL_VINDEX (decl))
    context
      = TYPE_MAIN_VARIANT
	(TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (decl)))));
  else
    context = DECL_CONTEXT (decl);

  while (context && TREE_CODE (context) != FUNCTION_DECL)
    {
      if (TREE_CODE (context) == BLOCK)
	context = BLOCK_SUPERCONTEXT (context);
      else
	context = get_containing_scope (context);
    }

  return context;
}

/* Return the innermost context enclosing DECL that is
   a RECORD_TYPE, UNION_TYPE or QUAL_UNION_TYPE, or zero if none.
   TYPE_DECLs and FUNCTION_DECLs are transparent to this function.  */

tree
decl_type_context (tree decl)
{
  tree context = DECL_CONTEXT (decl);

  while (context)
    switch (TREE_CODE (context))
      {
      case NAMESPACE_DECL:
      case TRANSLATION_UNIT_DECL:
	return NULL_TREE;

      case RECORD_TYPE:
      case UNION_TYPE:
      case QUAL_UNION_TYPE:
	return context;

      case TYPE_DECL:
      case FUNCTION_DECL:
	context = DECL_CONTEXT (context);
	break;

      case BLOCK:
	context = BLOCK_SUPERCONTEXT (context);
	break;

      default:
	gcc_unreachable ();
      }

  return NULL_TREE;
}

/* CALL is a CALL_EXPR.  Return the declaration for the function
   called, or NULL_TREE if the called function cannot be
   determined.  */

tree
get_callee_fndecl (tree call)
{
  tree addr;

  if (call == error_mark_node)
    return call;

  /* It's invalid to call this function with anything but a
     CALL_EXPR.  */
  gcc_assert (TREE_CODE (call) == CALL_EXPR);

  /* The first operand to the CALL is the address of the function
     called.  */
  addr = TREE_OPERAND (call, 0);

  STRIP_NOPS (addr);

  /* If this is a readonly function pointer, extract its initial value.  */
  if (DECL_P (addr) && TREE_CODE (addr) != FUNCTION_DECL
      && TREE_READONLY (addr) && ! TREE_THIS_VOLATILE (addr)
      && DECL_INITIAL (addr))
    addr = DECL_INITIAL (addr);

  /* If the address is just `&f' for some function `f', then we know
     that `f' is being called.  */
  if (TREE_CODE (addr) == ADDR_EXPR
      && TREE_CODE (TREE_OPERAND (addr, 0)) == FUNCTION_DECL)
    return TREE_OPERAND (addr, 0);

  /* We couldn't figure out what was being called.  Maybe the front
     end has some idea.  */
  return lang_hooks.lang_get_callee_fndecl (call);
}

/* Print debugging information about tree nodes generated during the compile,
   and any language-specific information.  */

void
dump_tree_statistics (void)
{
#ifdef GATHER_STATISTICS
  int i;
  int total_nodes, total_bytes;
#endif

  fprintf (stderr, "\n??? tree nodes created\n\n");
#ifdef GATHER_STATISTICS
  fprintf (stderr, "Kind                   Nodes      Bytes\n");
  fprintf (stderr, "---------------------------------------\n");
  total_nodes = total_bytes = 0;
  for (i = 0; i < (int) all_kinds; i++)
    {
      fprintf (stderr, "%-20s %7d %10d\n", tree_node_kind_names[i],
	       tree_node_counts[i], tree_node_sizes[i]);
      total_nodes += tree_node_counts[i];
      total_bytes += tree_node_sizes[i];
    }
  fprintf (stderr, "---------------------------------------\n");
  fprintf (stderr, "%-20s %7d %10d\n", "Total", total_nodes, total_bytes);
  fprintf (stderr, "---------------------------------------\n");
  ssanames_print_statistics ();
  phinodes_print_statistics ();
#else
  fprintf (stderr, "(No per-node statistics)\n");
#endif
  print_type_hash_statistics ();
  print_debug_expr_statistics ();
  print_value_expr_statistics ();
  print_restrict_base_statistics ();
  lang_hooks.print_statistics ();
}

#define FILE_FUNCTION_FORMAT "_GLOBAL__%s_%s"

/* Generate a crc32 of a string.  */

unsigned
crc32_string (unsigned chksum, const char *string)
{
  do
    {
      unsigned value = *string << 24;
      unsigned ix;

      for (ix = 8; ix--; value <<= 1)
  	{
  	  unsigned feedback;

  	  feedback = (value ^ chksum) & 0x80000000 ? 0x04c11db7 : 0;
 	  chksum <<= 1;
 	  chksum ^= feedback;
  	}
    }
  while (*string++);
  return chksum;
}

/* P is a string that will be used in a symbol.  Mask out any characters
   that are not valid in that context.  */

void
clean_symbol_name (char *p)
{
  for (; *p; p++)
    if (! (ISALNUM (*p)
#ifndef NO_DOLLAR_IN_LABEL	/* this for `$'; unlikely, but... -- kr */
	    || *p == '$'
#endif
#ifndef NO_DOT_IN_LABEL		/* this for `.'; unlikely, but...  */
	    || *p == '.'
#endif
	   ))
      *p = '_';
}

/* Generate a name for a special-purpose function function.
   The generated name may need to be unique across the whole link.
   TYPE is some string to identify the purpose of this function to the
   linker or collect2; it must start with an uppercase letter,
   one of:
   I - for constructors
   D - for destructors
   N - for C++ anonymous namespaces
   F - for DWARF unwind frame information.  */

tree
get_file_function_name (const char *type)
{
  char *buf;
  const char *p;
  char *q;

  /* If we already have a name we know to be unique, just use that.  */
  if (first_global_object_name)
    p = first_global_object_name;
  /* If the target is handling the constructors/destructors, they
     will be local to this file and the name is only necessary for
     debugging purposes.  */
  else if ((type[0] == 'I' || type[0] == 'D') && targetm.have_ctors_dtors)
    {
      const char *file = main_input_filename;
      if (! file)
	file = input_filename;
      /* Just use the file's basename, because the full pathname
	 might be quite long.  */
      p = strrchr (file, '/');
      if (p)
	p++;
      else
	p = file;
      p = q = ASTRDUP (p);
      clean_symbol_name (q);
    }
  else
    {
      /* Otherwise, the name must be unique across the entire link.
	 We don't have anything that we know to be unique to this translation
	 unit, so use what we do have and throw in some randomness.  */
      unsigned len;
      const char *name = weak_global_object_name;
      const char *file = main_input_filename;

      if (! name)
	name = "";
      if (! file)
	file = input_filename;

      len = strlen (file);
      q = alloca (9 * 2 + len + 1);
      memcpy (q, file, len + 1);
      clean_symbol_name (q);

      sprintf (q + len, "_%08X_%08X", crc32_string (0, name),
	       crc32_string (0, flag_random_seed));

      p = q;
    }

  buf = alloca (sizeof (FILE_FUNCTION_FORMAT) + strlen (p) + strlen (type));

  /* Set up the name of the file-level functions we may need.
     Use a global object (which is already required to be unique over
     the program) rather than the file name (which imposes extra
     constraints).  */
  sprintf (buf, FILE_FUNCTION_FORMAT, type, p);

  return get_identifier (buf);
}

#if defined ENABLE_TREE_CHECKING && (GCC_VERSION >= 2007)

/* Complain that the tree code of NODE does not match the expected 0
   terminated list of trailing codes. The trailing code list can be
   empty, for a more vague error message.  FILE, LINE, and FUNCTION
   are of the caller.  */

void
tree_check_failed (const tree node, const char *file,
		   int line, const char *function, ...)
{
  va_list args;
  char *buffer;
  unsigned length = 0;
  int code;

  va_start (args, function);
  while ((code = va_arg (args, int)))
    length += 4 + strlen (tree_code_name[code]);
  va_end (args);
  if (length)
    {
      va_start (args, function);
      length += strlen ("expected ");
      buffer = alloca (length);
      length = 0;
      while ((code = va_arg (args, int)))
	{
	  const char *prefix = length ? " or " : "expected ";
	  
	  strcpy (buffer + length, prefix);
	  length += strlen (prefix);
	  strcpy (buffer + length, tree_code_name[code]);
	  length += strlen (tree_code_name[code]);
	}
      va_end (args);
    }
  else
    buffer = (char *)"unexpected node";

  internal_error ("tree check: %s, have %s in %s, at %s:%d",
		  buffer, tree_code_name[TREE_CODE (node)],
		  function, trim_filename (file), line);
}

/* Complain that the tree code of NODE does match the expected 0
   terminated list of trailing codes. FILE, LINE, and FUNCTION are of
   the caller.  */

void
tree_not_check_failed (const tree node, const char *file,
		       int line, const char *function, ...)
{
  va_list args;
  char *buffer;
  unsigned length = 0;
  int code;

  va_start (args, function);
  while ((code = va_arg (args, int)))
    length += 4 + strlen (tree_code_name[code]);
  va_end (args);
  va_start (args, function);
  buffer = alloca (length);
  length = 0;
  while ((code = va_arg (args, int)))
    {
      if (length)
	{
	  strcpy (buffer + length, " or ");
	  length += 4;
	}
      strcpy (buffer + length, tree_code_name[code]);
      length += strlen (tree_code_name[code]);
    }
  va_end (args);

  internal_error ("tree check: expected none of %s, have %s in %s, at %s:%d",
		  buffer, tree_code_name[TREE_CODE (node)],
		  function, trim_filename (file), line);
}

/* Similar to tree_check_failed, except that we check for a class of tree
   code, given in CL.  */

void
tree_class_check_failed (const tree node, const enum tree_code_class cl,
			 const char *file, int line, const char *function)
{
  internal_error
    ("tree check: expected class %qs, have %qs (%s) in %s, at %s:%d",
     TREE_CODE_CLASS_STRING (cl),
     TREE_CODE_CLASS_STRING (TREE_CODE_CLASS (TREE_CODE (node))),
     tree_code_name[TREE_CODE (node)], function, trim_filename (file), line);
}

/* Similar to tree_check_failed, except that instead of specifying a
   dozen codes, use the knowledge that they're all sequential.  */

void
tree_range_check_failed (const tree node, const char *file, int line,
			 const char *function, enum tree_code c1,
			 enum tree_code c2)
{
  char *buffer;
  unsigned length = 0;
  enum tree_code c;

  for (c = c1; c <= c2; ++c)
    length += 4 + strlen (tree_code_name[c]);

  length += strlen ("expected ");
  buffer = alloca (length);
  length = 0;

  for (c = c1; c <= c2; ++c)
    {
      const char *prefix = length ? " or " : "expected ";

      strcpy (buffer + length, prefix);
      length += strlen (prefix);
      strcpy (buffer + length, tree_code_name[c]);
      length += strlen (tree_code_name[c]);
    }

  internal_error ("tree check: %s, have %s in %s, at %s:%d",
		  buffer, tree_code_name[TREE_CODE (node)],
		  function, trim_filename (file), line);
}


/* Similar to tree_check_failed, except that we check that a tree does
   not have the specified code, given in CL.  */

void
tree_not_class_check_failed (const tree node, const enum tree_code_class cl,
			     const char *file, int line, const char *function)
{
  internal_error
    ("tree check: did not expect class %qs, have %qs (%s) in %s, at %s:%d",
     TREE_CODE_CLASS_STRING (cl),
     TREE_CODE_CLASS_STRING (TREE_CODE_CLASS (TREE_CODE (node))),
     tree_code_name[TREE_CODE (node)], function, trim_filename (file), line);
}


/* Similar to tree_check_failed but applied to OMP_CLAUSE codes.  */

void
omp_clause_check_failed (const tree node, const char *file, int line,
                         const char *function, enum omp_clause_code code)
{
  internal_error ("tree check: expected omp_clause %s, have %s in %s, at %s:%d",
		  omp_clause_code_name[code], tree_code_name[TREE_CODE (node)],
		  function, trim_filename (file), line);
}


/* Similar to tree_range_check_failed but applied to OMP_CLAUSE codes.  */

void
omp_clause_range_check_failed (const tree node, const char *file, int line,
			       const char *function, enum omp_clause_code c1,
			       enum omp_clause_code c2)
{
  char *buffer;
  unsigned length = 0;
  enum omp_clause_code c;

  for (c = c1; c <= c2; ++c)
    length += 4 + strlen (omp_clause_code_name[c]);

  length += strlen ("expected ");
  buffer = alloca (length);
  length = 0;

  for (c = c1; c <= c2; ++c)
    {
      const char *prefix = length ? " or " : "expected ";

      strcpy (buffer + length, prefix);
      length += strlen (prefix);
      strcpy (buffer + length, omp_clause_code_name[c]);
      length += strlen (omp_clause_code_name[c]);
    }

  internal_error ("tree check: %s, have %s in %s, at %s:%d",
		  buffer, omp_clause_code_name[TREE_CODE (node)],
		  function, trim_filename (file), line);
}


#undef DEFTREESTRUCT
#define DEFTREESTRUCT(VAL, NAME) NAME,

static const char *ts_enum_names[] = {
#include "treestruct.def"
};
#undef DEFTREESTRUCT

#define TS_ENUM_NAME(EN) (ts_enum_names[(EN)])

/* Similar to tree_class_check_failed, except that we check for
   whether CODE contains the tree structure identified by EN.  */

void
tree_contains_struct_check_failed (const tree node, 
				   const enum tree_node_structure_enum en,
				   const char *file, int line, 
				   const char *function)
{
  internal_error
    ("tree check: expected tree that contains %qs structure, have %qs  in %s, at %s:%d",
     TS_ENUM_NAME(en),
     tree_code_name[TREE_CODE (node)], function, trim_filename (file), line);
}


/* Similar to above, except that the check is for the bounds of a TREE_VEC's
   (dynamically sized) vector.  */

void
tree_vec_elt_check_failed (int idx, int len, const char *file, int line,
			   const char *function)
{
  internal_error
    ("tree check: accessed elt %d of tree_vec with %d elts in %s, at %s:%d",
     idx + 1, len, function, trim_filename (file), line);
}

/* Similar to above, except that the check is for the bounds of a PHI_NODE's
   (dynamically sized) vector.  */

void
phi_node_elt_check_failed (int idx, int len, const char *file, int line,
			    const char *function)
{
  internal_error
    ("tree check: accessed elt %d of phi_node with %d elts in %s, at %s:%d",
     idx + 1, len, function, trim_filename (file), line);
}

/* Similar to above, except that the check is for the bounds of the operand
   vector of an expression node.  */

void
tree_operand_check_failed (int idx, enum tree_code code, const char *file,
			   int line, const char *function)
{
  internal_error
    ("tree check: accessed operand %d of %s with %d operands in %s, at %s:%d",
     idx + 1, tree_code_name[code], TREE_CODE_LENGTH (code),
     function, trim_filename (file), line);
}

/* Similar to above, except that the check is for the number of
   operands of an OMP_CLAUSE node.  */

void
omp_clause_operand_check_failed (int idx, tree t, const char *file,
			         int line, const char *function)
{
  internal_error
    ("tree check: accessed operand %d of omp_clause %s with %d operands "
     "in %s, at %s:%d", idx + 1, omp_clause_code_name[OMP_CLAUSE_CODE (t)],
     omp_clause_num_ops [OMP_CLAUSE_CODE (t)], function,
     trim_filename (file), line);
}
#endif /* ENABLE_TREE_CHECKING */

/* Create a new vector type node holding SUBPARTS units of type INNERTYPE,
   and mapped to the machine mode MODE.  Initialize its fields and build
   the information necessary for debugging output.  */

static tree
make_vector_type (tree innertype, int nunits, enum machine_mode mode)
{
  tree t;
  hashval_t hashcode = 0;

  /* Build a main variant, based on the main variant of the inner type, then
     use it to build the variant we return.  */
  if ((TYPE_ATTRIBUTES (innertype) || TYPE_QUALS (innertype))
      && TYPE_MAIN_VARIANT (innertype) != innertype)
    return build_type_attribute_qual_variant (
	    make_vector_type (TYPE_MAIN_VARIANT (innertype), nunits, mode),
	    TYPE_ATTRIBUTES (innertype),
	    TYPE_QUALS (innertype));

  t = make_node (VECTOR_TYPE);
  TREE_TYPE (t) = TYPE_MAIN_VARIANT (innertype);
  SET_TYPE_VECTOR_SUBPARTS (t, nunits);
  TYPE_MODE (t) = mode;
  TYPE_READONLY (t) = TYPE_READONLY (innertype);
  TYPE_VOLATILE (t) = TYPE_VOLATILE (innertype);

  layout_type (t);

  {
    tree index = build_int_cst (NULL_TREE, nunits - 1);
    tree array = build_array_type (innertype, build_index_type (index));
    tree rt = make_node (RECORD_TYPE);

    TYPE_FIELDS (rt) = build_decl (FIELD_DECL, get_identifier ("f"), array);
    DECL_CONTEXT (TYPE_FIELDS (rt)) = rt;
    layout_type (rt);
    TYPE_DEBUG_REPRESENTATION_TYPE (t) = rt;
    /* In dwarfout.c, type lookup uses TYPE_UID numbers.  We want to output
       the representation type, and we want to find that die when looking up
       the vector type.  This is most easily achieved by making the TYPE_UID
       numbers equal.  */
    TYPE_UID (rt) = TYPE_UID (t);
  }

  hashcode = iterative_hash_host_wide_int (VECTOR_TYPE, hashcode);
  hashcode = iterative_hash_host_wide_int (mode, hashcode);
  hashcode = iterative_hash_object (TYPE_HASH (innertype), hashcode);
  return type_hash_canon (hashcode, t);
}

static tree
make_or_reuse_type (unsigned size, int unsignedp)
{
  if (size == INT_TYPE_SIZE)
    return unsignedp ? unsigned_type_node : integer_type_node;
  if (size == CHAR_TYPE_SIZE)
    return unsignedp ? unsigned_char_type_node : signed_char_type_node;
  if (size == SHORT_TYPE_SIZE)
    return unsignedp ? short_unsigned_type_node : short_integer_type_node;
  if (size == LONG_TYPE_SIZE)
    return unsignedp ? long_unsigned_type_node : long_integer_type_node;
  if (size == LONG_LONG_TYPE_SIZE)
    return (unsignedp ? long_long_unsigned_type_node
            : long_long_integer_type_node);

  if (unsignedp)
    return make_unsigned_type (size);
  else
    return make_signed_type (size);
}

/* Create nodes for all integer types (and error_mark_node) using the sizes
   of C datatypes.  The caller should call set_sizetype soon after calling
   this function to select one of the types as sizetype.  */

void
build_common_tree_nodes (bool signed_char, bool signed_sizetype)
{
  error_mark_node = make_node (ERROR_MARK);
  TREE_TYPE (error_mark_node) = error_mark_node;

  initialize_sizetypes (signed_sizetype);

  /* Define both `signed char' and `unsigned char'.  */
  signed_char_type_node = make_signed_type (CHAR_TYPE_SIZE);
  TYPE_STRING_FLAG (signed_char_type_node) = 1;
  unsigned_char_type_node = make_unsigned_type (CHAR_TYPE_SIZE);
  TYPE_STRING_FLAG (unsigned_char_type_node) = 1;

  /* Define `char', which is like either `signed char' or `unsigned char'
     but not the same as either.  */
  char_type_node
    = (signed_char
       ? make_signed_type (CHAR_TYPE_SIZE)
       : make_unsigned_type (CHAR_TYPE_SIZE));
  TYPE_STRING_FLAG (char_type_node) = 1;

  short_integer_type_node = make_signed_type (SHORT_TYPE_SIZE);
  short_unsigned_type_node = make_unsigned_type (SHORT_TYPE_SIZE);
  integer_type_node = make_signed_type (INT_TYPE_SIZE);
  unsigned_type_node = make_unsigned_type (INT_TYPE_SIZE);
  long_integer_type_node = make_signed_type (LONG_TYPE_SIZE);
  long_unsigned_type_node = make_unsigned_type (LONG_TYPE_SIZE);
  long_long_integer_type_node = make_signed_type (LONG_LONG_TYPE_SIZE);
  long_long_unsigned_type_node = make_unsigned_type (LONG_LONG_TYPE_SIZE);

  /* Define a boolean type.  This type only represents boolean values but
     may be larger than char depending on the value of BOOL_TYPE_SIZE.
     Front ends which want to override this size (i.e. Java) can redefine
     boolean_type_node before calling build_common_tree_nodes_2.  */
  boolean_type_node = make_unsigned_type (BOOL_TYPE_SIZE);
  TREE_SET_CODE (boolean_type_node, BOOLEAN_TYPE);
  TYPE_MAX_VALUE (boolean_type_node) = build_int_cst (boolean_type_node, 1);
  TYPE_PRECISION (boolean_type_node) = 1;

  /* Fill in the rest of the sized types.  Reuse existing type nodes
     when possible.  */
  intQI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (QImode), 0);
  intHI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (HImode), 0);
  intSI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (SImode), 0);
  intDI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (DImode), 0);
  intTI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (TImode), 0);

  unsigned_intQI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (QImode), 1);
  unsigned_intHI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (HImode), 1);
  unsigned_intSI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (SImode), 1);
  unsigned_intDI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (DImode), 1);
  unsigned_intTI_type_node = make_or_reuse_type (GET_MODE_BITSIZE (TImode), 1);

  access_public_node = get_identifier ("public");
  access_protected_node = get_identifier ("protected");
  access_private_node = get_identifier ("private");
}

/* Call this function after calling build_common_tree_nodes and set_sizetype.
   It will create several other common tree nodes.  */

void
build_common_tree_nodes_2 (int short_double)
{
  /* Define these next since types below may used them.  */
  integer_zero_node = build_int_cst (NULL_TREE, 0);
  integer_one_node = build_int_cst (NULL_TREE, 1);
  integer_minus_one_node = build_int_cst (NULL_TREE, -1);

  size_zero_node = size_int (0);
  size_one_node = size_int (1);
  bitsize_zero_node = bitsize_int (0);
  bitsize_one_node = bitsize_int (1);
  bitsize_unit_node = bitsize_int (BITS_PER_UNIT);

  boolean_false_node = TYPE_MIN_VALUE (boolean_type_node);
  boolean_true_node = TYPE_MAX_VALUE (boolean_type_node);

  void_type_node = make_node (VOID_TYPE);
  layout_type (void_type_node);

  /* We are not going to have real types in C with less than byte alignment,
     so we might as well not have any types that claim to have it.  */
  TYPE_ALIGN (void_type_node) = BITS_PER_UNIT;
  TYPE_USER_ALIGN (void_type_node) = 0;

  null_pointer_node = build_int_cst (build_pointer_type (void_type_node), 0);
  layout_type (TREE_TYPE (null_pointer_node));

  ptr_type_node = build_pointer_type (void_type_node);
  const_ptr_type_node
    = build_pointer_type (build_type_variant (void_type_node, 1, 0));
  fileptr_type_node = ptr_type_node;

  float_type_node = make_node (REAL_TYPE);
  TYPE_PRECISION (float_type_node) = FLOAT_TYPE_SIZE;
  layout_type (float_type_node);

  double_type_node = make_node (REAL_TYPE);
  if (short_double)
    TYPE_PRECISION (double_type_node) = FLOAT_TYPE_SIZE;
  else
    TYPE_PRECISION (double_type_node) = DOUBLE_TYPE_SIZE;
  layout_type (double_type_node);

  long_double_type_node = make_node (REAL_TYPE);
  TYPE_PRECISION (long_double_type_node) = LONG_DOUBLE_TYPE_SIZE;
  layout_type (long_double_type_node);

  float_ptr_type_node = build_pointer_type (float_type_node);
  double_ptr_type_node = build_pointer_type (double_type_node);
  long_double_ptr_type_node = build_pointer_type (long_double_type_node);
  integer_ptr_type_node = build_pointer_type (integer_type_node);

  /* Fixed size integer types.  */
  uint32_type_node = build_nonstandard_integer_type (32, true);
  uint64_type_node = build_nonstandard_integer_type (64, true);

  /* Decimal float types. */
  dfloat32_type_node = make_node (REAL_TYPE);
  TYPE_PRECISION (dfloat32_type_node) = DECIMAL32_TYPE_SIZE; 
  layout_type (dfloat32_type_node);
  TYPE_MODE (dfloat32_type_node) = SDmode;
  dfloat32_ptr_type_node = build_pointer_type (dfloat32_type_node);

  dfloat64_type_node = make_node (REAL_TYPE);
  TYPE_PRECISION (dfloat64_type_node) = DECIMAL64_TYPE_SIZE;
  layout_type (dfloat64_type_node);
  TYPE_MODE (dfloat64_type_node) = DDmode;
  dfloat64_ptr_type_node = build_pointer_type (dfloat64_type_node);

  dfloat128_type_node = make_node (REAL_TYPE);
  TYPE_PRECISION (dfloat128_type_node) = DECIMAL128_TYPE_SIZE; 
  layout_type (dfloat128_type_node);
  TYPE_MODE (dfloat128_type_node) = TDmode;
  dfloat128_ptr_type_node = build_pointer_type (dfloat128_type_node);

  complex_integer_type_node = make_node (COMPLEX_TYPE);
  TREE_TYPE (complex_integer_type_node) = integer_type_node;
  layout_type (complex_integer_type_node);

  complex_float_type_node = make_node (COMPLEX_TYPE);
  TREE_TYPE (complex_float_type_node) = float_type_node;
  layout_type (complex_float_type_node);

  complex_double_type_node = make_node (COMPLEX_TYPE);
  TREE_TYPE (complex_double_type_node) = double_type_node;
  layout_type (complex_double_type_node);

  complex_long_double_type_node = make_node (COMPLEX_TYPE);
  TREE_TYPE (complex_long_double_type_node) = long_double_type_node;
  layout_type (complex_long_double_type_node);

  {
    tree t = targetm.build_builtin_va_list ();

    /* Many back-ends define record types without setting TYPE_NAME.
       If we copied the record type here, we'd keep the original
       record type without a name.  This breaks name mangling.  So,
       don't copy record types and let c_common_nodes_and_builtins()
       declare the type to be __builtin_va_list.  */
    if (TREE_CODE (t) != RECORD_TYPE)
      t = build_variant_type_copy (t);

    va_list_type_node = t;
  }
}

/* A subroutine of build_common_builtin_nodes.  Define a builtin function.  */

static void
local_define_builtin (const char *name, tree type, enum built_in_function code,
                      const char *library_name, int ecf_flags)
{
  tree decl;

  decl = lang_hooks.builtin_function (name, type, code, BUILT_IN_NORMAL,
				      library_name, NULL_TREE);
  if (ecf_flags & ECF_CONST)
    TREE_READONLY (decl) = 1;
  if (ecf_flags & ECF_PURE)
    DECL_IS_PURE (decl) = 1;
  if (ecf_flags & ECF_NORETURN)
    TREE_THIS_VOLATILE (decl) = 1;
  if (ecf_flags & ECF_NOTHROW)
    TREE_NOTHROW (decl) = 1;
  if (ecf_flags & ECF_MALLOC)
    DECL_IS_MALLOC (decl) = 1;

  built_in_decls[code] = decl;
  implicit_built_in_decls[code] = decl;
}

/* Call this function after instantiating all builtins that the language
   front end cares about.  This will build the rest of the builtins that
   are relied upon by the tree optimizers and the middle-end.  */

void
build_common_builtin_nodes (void)
{
  tree tmp, ftype;

  if (built_in_decls[BUILT_IN_MEMCPY] == NULL
      || built_in_decls[BUILT_IN_MEMMOVE] == NULL)
    {
      tmp = tree_cons (NULL_TREE, size_type_node, void_list_node);
      tmp = tree_cons (NULL_TREE, const_ptr_type_node, tmp);
      tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
      ftype = build_function_type (ptr_type_node, tmp);

      if (built_in_decls[BUILT_IN_MEMCPY] == NULL)
	local_define_builtin ("__builtin_memcpy", ftype, BUILT_IN_MEMCPY,
			      "memcpy", ECF_NOTHROW);
      if (built_in_decls[BUILT_IN_MEMMOVE] == NULL)
	local_define_builtin ("__builtin_memmove", ftype, BUILT_IN_MEMMOVE,
			      "memmove", ECF_NOTHROW);
    }

  if (built_in_decls[BUILT_IN_MEMCMP] == NULL)
    {
      tmp = tree_cons (NULL_TREE, size_type_node, void_list_node);
      tmp = tree_cons (NULL_TREE, const_ptr_type_node, tmp);
      tmp = tree_cons (NULL_TREE, const_ptr_type_node, tmp);
      ftype = build_function_type (integer_type_node, tmp);
      local_define_builtin ("__builtin_memcmp", ftype, BUILT_IN_MEMCMP,
			    "memcmp", ECF_PURE | ECF_NOTHROW);
    }

  if (built_in_decls[BUILT_IN_MEMSET] == NULL)
    {
      tmp = tree_cons (NULL_TREE, size_type_node, void_list_node);
      tmp = tree_cons (NULL_TREE, integer_type_node, tmp);
      tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
      ftype = build_function_type (ptr_type_node, tmp);
      local_define_builtin ("__builtin_memset", ftype, BUILT_IN_MEMSET,
			    "memset", ECF_NOTHROW);
    }

  if (built_in_decls[BUILT_IN_ALLOCA] == NULL)
    {
      tmp = tree_cons (NULL_TREE, size_type_node, void_list_node);
      ftype = build_function_type (ptr_type_node, tmp);
      local_define_builtin ("__builtin_alloca", ftype, BUILT_IN_ALLOCA,
			    "alloca", ECF_NOTHROW | ECF_MALLOC);
    }

  tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
  tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
  tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
  ftype = build_function_type (void_type_node, tmp);
  local_define_builtin ("__builtin_init_trampoline", ftype,
			BUILT_IN_INIT_TRAMPOLINE,
			"__builtin_init_trampoline", ECF_NOTHROW);

  tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
  ftype = build_function_type (ptr_type_node, tmp);
  local_define_builtin ("__builtin_adjust_trampoline", ftype,
			BUILT_IN_ADJUST_TRAMPOLINE,
			"__builtin_adjust_trampoline",
			ECF_CONST | ECF_NOTHROW);

  tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
  tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
  ftype = build_function_type (void_type_node, tmp);
  local_define_builtin ("__builtin_nonlocal_goto", ftype,
			BUILT_IN_NONLOCAL_GOTO,
			"__builtin_nonlocal_goto",
			ECF_NORETURN | ECF_NOTHROW);

  tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
  tmp = tree_cons (NULL_TREE, ptr_type_node, tmp);
  ftype = build_function_type (void_type_node, tmp);
  local_define_builtin ("__builtin_setjmp_setup", ftype,
			BUILT_IN_SETJMP_SETUP,
			"__builtin_setjmp_setup", ECF_NOTHROW);

  tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
  ftype = build_function_type (ptr_type_node, tmp);
  local_define_builtin ("__builtin_setjmp_dispatcher", ftype,
			BUILT_IN_SETJMP_DISPATCHER,
			"__builtin_setjmp_dispatcher",
			ECF_PURE | ECF_NOTHROW);

  tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
  ftype = build_function_type (void_type_node, tmp);
  local_define_builtin ("__builtin_setjmp_receiver", ftype,
			BUILT_IN_SETJMP_RECEIVER,
			"__builtin_setjmp_receiver", ECF_NOTHROW);

  ftype = build_function_type (ptr_type_node, void_list_node);
  local_define_builtin ("__builtin_stack_save", ftype, BUILT_IN_STACK_SAVE,
			"__builtin_stack_save", ECF_NOTHROW);

  tmp = tree_cons (NULL_TREE, ptr_type_node, void_list_node);
  ftype = build_function_type (void_type_node, tmp);
  local_define_builtin ("__builtin_stack_restore", ftype,
			BUILT_IN_STACK_RESTORE,
			"__builtin_stack_restore", ECF_NOTHROW);

  ftype = build_function_type (void_type_node, void_list_node);
  local_define_builtin ("__builtin_profile_func_enter", ftype,
			BUILT_IN_PROFILE_FUNC_ENTER, "profile_func_enter", 0);
  local_define_builtin ("__builtin_profile_func_exit", ftype,
			BUILT_IN_PROFILE_FUNC_EXIT, "profile_func_exit", 0);

  /* Complex multiplication and division.  These are handled as builtins
     rather than optabs because emit_library_call_value doesn't support
     complex.  Further, we can do slightly better with folding these 
     beasties if the real and complex parts of the arguments are separate.  */
  {
    enum machine_mode mode;

    for (mode = MIN_MODE_COMPLEX_FLOAT; mode <= MAX_MODE_COMPLEX_FLOAT; ++mode)
      {
	char mode_name_buf[4], *q;
	const char *p;
	enum built_in_function mcode, dcode;
	tree type, inner_type;

	type = lang_hooks.types.type_for_mode (mode, 0);
	if (type == NULL)
	  continue;
	inner_type = TREE_TYPE (type);

	tmp = tree_cons (NULL_TREE, inner_type, void_list_node);
	tmp = tree_cons (NULL_TREE, inner_type, tmp);
	tmp = tree_cons (NULL_TREE, inner_type, tmp);
	tmp = tree_cons (NULL_TREE, inner_type, tmp);
	ftype = build_function_type (type, tmp);

        mcode = BUILT_IN_COMPLEX_MUL_MIN + mode - MIN_MODE_COMPLEX_FLOAT;
        dcode = BUILT_IN_COMPLEX_DIV_MIN + mode - MIN_MODE_COMPLEX_FLOAT;

        for (p = GET_MODE_NAME (mode), q = mode_name_buf; *p; p++, q++)
	  *q = TOLOWER (*p);
	*q = '\0';

	built_in_names[mcode] = concat ("__mul", mode_name_buf, "3", NULL);
        local_define_builtin (built_in_names[mcode], ftype, mcode,
			      built_in_names[mcode], ECF_CONST | ECF_NOTHROW);

	built_in_names[dcode] = concat ("__div", mode_name_buf, "3", NULL);
        local_define_builtin (built_in_names[dcode], ftype, dcode,
			      built_in_names[dcode], ECF_CONST | ECF_NOTHROW);
      }
  }
}

/* HACK.  GROSS.  This is absolutely disgusting.  I wish there was a
   better way.

   If we requested a pointer to a vector, build up the pointers that
   we stripped off while looking for the inner type.  Similarly for
   return values from functions.

   The argument TYPE is the top of the chain, and BOTTOM is the
   new type which we will point to.  */

tree
reconstruct_complex_type (tree type, tree bottom)
{
  tree inner, outer;

  if (POINTER_TYPE_P (type))
    {
      inner = reconstruct_complex_type (TREE_TYPE (type), bottom);
      outer = build_pointer_type (inner);
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    {
      inner = reconstruct_complex_type (TREE_TYPE (type), bottom);
      outer = build_array_type (inner, TYPE_DOMAIN (type));
    }
  else if (TREE_CODE (type) == FUNCTION_TYPE)
    {
      inner = reconstruct_complex_type (TREE_TYPE (type), bottom);
      outer = build_function_type (inner, TYPE_ARG_TYPES (type));
    }
  else if (TREE_CODE (type) == METHOD_TYPE)
    {
      tree argtypes;
      inner = reconstruct_complex_type (TREE_TYPE (type), bottom);
      /* The build_method_type_directly() routine prepends 'this' to argument list,
         so we must compensate by getting rid of it.  */
      argtypes = TYPE_ARG_TYPES (type);
      outer = build_method_type_directly (TYPE_METHOD_BASETYPE (type),
					  inner,
					  TYPE_ARG_TYPES (type));
      TYPE_ARG_TYPES (outer) = argtypes;
    }
  else
    return bottom;

  TYPE_READONLY (outer) = TYPE_READONLY (type);
  TYPE_VOLATILE (outer) = TYPE_VOLATILE (type);

  return outer;
}

/* Returns a vector tree node given a mode (integer, vector, or BLKmode) and
   the inner type.  */
tree
build_vector_type_for_mode (tree innertype, enum machine_mode mode)
{
  int nunits;

  switch (GET_MODE_CLASS (mode))
    {
    case MODE_VECTOR_INT:
    case MODE_VECTOR_FLOAT:
      nunits = GET_MODE_NUNITS (mode);
      break;

    case MODE_INT:
      /* Check that there are no leftover bits.  */
      gcc_assert (GET_MODE_BITSIZE (mode)
		  % TREE_INT_CST_LOW (TYPE_SIZE (innertype)) == 0);

      nunits = GET_MODE_BITSIZE (mode)
	       / TREE_INT_CST_LOW (TYPE_SIZE (innertype));
      break;

    default:
      gcc_unreachable ();
    }

  return make_vector_type (innertype, nunits, mode);
}

/* Similarly, but takes the inner type and number of units, which must be
   a power of two.  */

tree
build_vector_type (tree innertype, int nunits)
{
  return make_vector_type (innertype, nunits, VOIDmode);
}


/* Build RESX_EXPR with given REGION_NUMBER.  */
tree
build_resx (int region_number)
{
  tree t;
  t = build1 (RESX_EXPR, void_type_node,
	      build_int_cst (NULL_TREE, region_number));
  return t;
}

/* Given an initializer INIT, return TRUE if INIT is zero or some
   aggregate of zeros.  Otherwise return FALSE.  */
bool
initializer_zerop (tree init)
{
  tree elt;

  STRIP_NOPS (init);

  switch (TREE_CODE (init))
    {
    case INTEGER_CST:
      return integer_zerop (init);

    case REAL_CST:
      /* ??? Note that this is not correct for C4X float formats.  There,
	 a bit pattern of all zeros is 1.0; 0.0 is encoded with the most
	 negative exponent.  */
      return real_zerop (init)
	&& ! REAL_VALUE_MINUS_ZERO (TREE_REAL_CST (init));

    case COMPLEX_CST:
      return integer_zerop (init)
	|| (real_zerop (init)
	    && ! REAL_VALUE_MINUS_ZERO (TREE_REAL_CST (TREE_REALPART (init)))
	    && ! REAL_VALUE_MINUS_ZERO (TREE_REAL_CST (TREE_IMAGPART (init))));

    case VECTOR_CST:
      for (elt = TREE_VECTOR_CST_ELTS (init); elt; elt = TREE_CHAIN (elt))
	if (!initializer_zerop (TREE_VALUE (elt)))
	  return false;
      return true;

    case CONSTRUCTOR:
      {
	unsigned HOST_WIDE_INT idx;

	FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (init), idx, elt)
	  if (!initializer_zerop (elt))
	    return false;
	return true;
      }

    default:
      return false;
    }
}

/* Build an empty statement.  */

tree
build_empty_stmt (void)
{
  return build1 (NOP_EXPR, void_type_node, size_zero_node);
}


/* Build an OpenMP clause with code CODE.  */

tree
build_omp_clause (enum omp_clause_code code)
{
  tree t;
  int size, length;

  length = omp_clause_num_ops[code];
  size = (sizeof (struct tree_omp_clause) + (length - 1) * sizeof (tree));

  t = ggc_alloc (size);
  memset (t, 0, size);
  TREE_SET_CODE (t, OMP_CLAUSE);
  OMP_CLAUSE_SET_CODE (t, code);

#ifdef GATHER_STATISTICS
  tree_node_counts[(int) omp_clause_kind]++;
  tree_node_sizes[(int) omp_clause_kind] += size;
#endif
  
  return t;
}


/* Returns true if it is possible to prove that the index of
   an array access REF (an ARRAY_REF expression) falls into the
   array bounds.  */

bool
in_array_bounds_p (tree ref)
{
  tree idx = TREE_OPERAND (ref, 1);
  tree min, max;

  if (TREE_CODE (idx) != INTEGER_CST)
    return false;

  min = array_ref_low_bound (ref);
  max = array_ref_up_bound (ref);
  if (!min
      || !max
      || TREE_CODE (min) != INTEGER_CST
      || TREE_CODE (max) != INTEGER_CST)
    return false;

  if (tree_int_cst_lt (idx, min)
      || tree_int_cst_lt (max, idx))
    return false;

  return true;
}

/* Returns true if it is possible to prove that the range of
   an array access REF (an ARRAY_RANGE_REF expression) falls
   into the array bounds.  */

bool
range_in_array_bounds_p (tree ref)
{
  tree domain_type = TYPE_DOMAIN (TREE_TYPE (ref));
  tree range_min, range_max, min, max;

  range_min = TYPE_MIN_VALUE (domain_type);
  range_max = TYPE_MAX_VALUE (domain_type);
  if (!range_min
      || !range_max
      || TREE_CODE (range_min) != INTEGER_CST
      || TREE_CODE (range_max) != INTEGER_CST)
    return false;

  min = array_ref_low_bound (ref);
  max = array_ref_up_bound (ref);
  if (!min
      || !max
      || TREE_CODE (min) != INTEGER_CST
      || TREE_CODE (max) != INTEGER_CST)
    return false;

  if (tree_int_cst_lt (range_min, min)
      || tree_int_cst_lt (max, range_max))
    return false;

  return true;
}

/* Return true if T (assumed to be a DECL) is a global variable.  */

bool
is_global_var (tree t)
{
  if (MTAG_P (t))
    return (TREE_STATIC (t) || MTAG_GLOBAL (t));
  else
    return (TREE_STATIC (t) || DECL_EXTERNAL (t));
}

/* Return true if T (assumed to be a DECL) must be assigned a memory
   location.  */

bool
needs_to_live_in_memory (tree t)
{
  return (TREE_ADDRESSABLE (t)
	  || is_global_var (t)
	  || (TREE_CODE (t) == RESULT_DECL
	      && aggregate_value_p (t, current_function_decl)));
}

/* There are situations in which a language considers record types
   compatible which have different field lists.  Decide if two fields
   are compatible.  It is assumed that the parent records are compatible.  */

bool
fields_compatible_p (tree f1, tree f2)
{
  if (!operand_equal_p (DECL_FIELD_BIT_OFFSET (f1),
			DECL_FIELD_BIT_OFFSET (f2), OEP_ONLY_CONST))
    return false;

  if (!operand_equal_p (DECL_FIELD_OFFSET (f1),
                        DECL_FIELD_OFFSET (f2), OEP_ONLY_CONST))
    return false;

  if (!lang_hooks.types_compatible_p (TREE_TYPE (f1), TREE_TYPE (f2)))
    return false;

  return true;
}

/* Locate within RECORD a field that is compatible with ORIG_FIELD.  */

tree
find_compatible_field (tree record, tree orig_field)
{
  tree f;

  for (f = TYPE_FIELDS (record); f ; f = TREE_CHAIN (f))
    if (TREE_CODE (f) == FIELD_DECL
	&& fields_compatible_p (f, orig_field))
      return f;

  /* ??? Why isn't this on the main fields list?  */
  f = TYPE_VFIELD (record);
  if (f && TREE_CODE (f) == FIELD_DECL
      && fields_compatible_p (f, orig_field))
    return f;

  /* ??? We should abort here, but Java appears to do Bad Things
     with inherited fields.  */
  return orig_field;
}

/* Return value of a constant X.  */

HOST_WIDE_INT
int_cst_value (tree x)
{
  unsigned bits = TYPE_PRECISION (TREE_TYPE (x));
  unsigned HOST_WIDE_INT val = TREE_INT_CST_LOW (x);
  bool negative = ((val >> (bits - 1)) & 1) != 0;

  gcc_assert (bits <= HOST_BITS_PER_WIDE_INT);

  if (negative)
    val |= (~(unsigned HOST_WIDE_INT) 0) << (bits - 1) << 1;
  else
    val &= ~((~(unsigned HOST_WIDE_INT) 0) << (bits - 1) << 1);

  return val;
}

/* Returns the greatest common divisor of A and B, which must be
   INTEGER_CSTs.  */

tree
tree_fold_gcd (tree a, tree b)
{
  tree a_mod_b;
  tree type = TREE_TYPE (a);

  gcc_assert (TREE_CODE (a) == INTEGER_CST);
  gcc_assert (TREE_CODE (b) == INTEGER_CST);

  if (integer_zerop (a))
    return b;

  if (integer_zerop (b))
    return a;

  if (tree_int_cst_sgn (a) == -1)
    a = fold_build2 (MULT_EXPR, type, a,
		     build_int_cst (type, -1));

  if (tree_int_cst_sgn (b) == -1)
    b = fold_build2 (MULT_EXPR, type, b,
		     build_int_cst (type, -1));

  while (1)
    {
      a_mod_b = fold_build2 (FLOOR_MOD_EXPR, type, a, b);

      if (!TREE_INT_CST_LOW (a_mod_b)
	  && !TREE_INT_CST_HIGH (a_mod_b))
	return b;

      a = b;
      b = a_mod_b;
    }
}

/* Returns unsigned variant of TYPE.  */

tree
unsigned_type_for (tree type)
{
  if (POINTER_TYPE_P (type))
    return lang_hooks.types.unsigned_type (size_type_node);
  return lang_hooks.types.unsigned_type (type);
}

/* Returns signed variant of TYPE.  */

tree
signed_type_for (tree type)
{
  if (POINTER_TYPE_P (type))
    return lang_hooks.types.signed_type (size_type_node);
  return lang_hooks.types.signed_type (type);
}

/* Returns the largest value obtainable by casting something in INNER type to
   OUTER type.  */

tree
upper_bound_in_type (tree outer, tree inner)
{
  unsigned HOST_WIDE_INT lo, hi;
  unsigned int det = 0;
  unsigned oprec = TYPE_PRECISION (outer);
  unsigned iprec = TYPE_PRECISION (inner);
  unsigned prec;

  /* Compute a unique number for every combination.  */
  det |= (oprec > iprec) ? 4 : 0;
  det |= TYPE_UNSIGNED (outer) ? 2 : 0;
  det |= TYPE_UNSIGNED (inner) ? 1 : 0;

  /* Determine the exponent to use.  */
  switch (det)
    {
    case 0:
    case 1:
      /* oprec <= iprec, outer: signed, inner: don't care.  */
      prec = oprec - 1;
      break;
    case 2:
    case 3:
      /* oprec <= iprec, outer: unsigned, inner: don't care.  */
      prec = oprec;
      break;
    case 4:
      /* oprec > iprec, outer: signed, inner: signed.  */
      prec = iprec - 1;
      break;
    case 5:
      /* oprec > iprec, outer: signed, inner: unsigned.  */
      prec = iprec;
      break;
    case 6:
      /* oprec > iprec, outer: unsigned, inner: signed.  */
      prec = oprec;
      break;
    case 7:
      /* oprec > iprec, outer: unsigned, inner: unsigned.  */
      prec = iprec;
      break;
    default:
      gcc_unreachable ();
    }

  /* Compute 2^^prec - 1.  */
  if (prec <= HOST_BITS_PER_WIDE_INT)
    {
      hi = 0;
      lo = ((~(unsigned HOST_WIDE_INT) 0)
	    >> (HOST_BITS_PER_WIDE_INT - prec));
    }
  else
    {
      hi = ((~(unsigned HOST_WIDE_INT) 0)
	    >> (2 * HOST_BITS_PER_WIDE_INT - prec));
      lo = ~(unsigned HOST_WIDE_INT) 0;
    }

  return build_int_cst_wide (outer, lo, hi);
}

/* Returns the smallest value obtainable by casting something in INNER type to
   OUTER type.  */

tree
lower_bound_in_type (tree outer, tree inner)
{
  unsigned HOST_WIDE_INT lo, hi;
  unsigned oprec = TYPE_PRECISION (outer);
  unsigned iprec = TYPE_PRECISION (inner);

  /* If OUTER type is unsigned, we can definitely cast 0 to OUTER type
     and obtain 0.  */
  if (TYPE_UNSIGNED (outer)
      /* If we are widening something of an unsigned type, OUTER type
	 contains all values of INNER type.  In particular, both INNER
	 and OUTER types have zero in common.  */
      || (oprec > iprec && TYPE_UNSIGNED (inner)))
    lo = hi = 0;
  else
    {
      /* If we are widening a signed type to another signed type, we
	 want to obtain -2^^(iprec-1).  If we are keeping the
	 precision or narrowing to a signed type, we want to obtain
	 -2^(oprec-1).  */
      unsigned prec = oprec > iprec ? iprec : oprec;

      if (prec <= HOST_BITS_PER_WIDE_INT)
	{
	  hi = ~(unsigned HOST_WIDE_INT) 0;
	  lo = (~(unsigned HOST_WIDE_INT) 0) << (prec - 1);
	}
      else
	{
	  hi = ((~(unsigned HOST_WIDE_INT) 0)
		<< (prec - HOST_BITS_PER_WIDE_INT - 1));
	  lo = 0;
	}
    }

  return build_int_cst_wide (outer, lo, hi);
}

/* Return nonzero if two operands that are suitable for PHI nodes are
   necessarily equal.  Specifically, both ARG0 and ARG1 must be either
   SSA_NAME or invariant.  Note that this is strictly an optimization.
   That is, callers of this function can directly call operand_equal_p
   and get the same result, only slower.  */

int
operand_equal_for_phi_arg_p (tree arg0, tree arg1)
{
  if (arg0 == arg1)
    return 1;
  if (TREE_CODE (arg0) == SSA_NAME || TREE_CODE (arg1) == SSA_NAME)
    return 0;
  return operand_equal_p (arg0, arg1, 0);
}

/* Returns number of zeros at the end of binary representation of X.
   
   ??? Use ffs if available?  */

tree
num_ending_zeros (tree x)
{
  unsigned HOST_WIDE_INT fr, nfr;
  unsigned num, abits;
  tree type = TREE_TYPE (x);

  if (TREE_INT_CST_LOW (x) == 0)
    {
      num = HOST_BITS_PER_WIDE_INT;
      fr = TREE_INT_CST_HIGH (x);
    }
  else
    {
      num = 0;
      fr = TREE_INT_CST_LOW (x);
    }

  for (abits = HOST_BITS_PER_WIDE_INT / 2; abits; abits /= 2)
    {
      nfr = fr >> abits;
      if (nfr << abits == fr)
	{
	  num += abits;
	  fr = nfr;
	}
    }

  if (num > TYPE_PRECISION (type))
    num = TYPE_PRECISION (type);

  return build_int_cst_type (type, num);
}


#define WALK_SUBTREE(NODE)				\
  do							\
    {							\
      result = walk_tree (&(NODE), func, data, pset);	\
      if (result)					\
	return result;					\
    }							\
  while (0)

/* This is a subroutine of walk_tree that walks field of TYPE that are to
   be walked whenever a type is seen in the tree.  Rest of operands and return
   value are as for walk_tree.  */

static tree
walk_type_fields (tree type, walk_tree_fn func, void *data,
		  struct pointer_set_t *pset)
{
  tree result = NULL_TREE;

  switch (TREE_CODE (type))
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      /* We have to worry about mutually recursive pointers.  These can't
	 be written in C.  They can in Ada.  It's pathological, but
	 there's an ACATS test (c38102a) that checks it.  Deal with this
	 by checking if we're pointing to another pointer, that one
	 points to another pointer, that one does too, and we have no htab.
	 If so, get a hash table.  We check three levels deep to avoid
	 the cost of the hash table if we don't need one.  */
      if (POINTER_TYPE_P (TREE_TYPE (type))
	  && POINTER_TYPE_P (TREE_TYPE (TREE_TYPE (type)))
	  && POINTER_TYPE_P (TREE_TYPE (TREE_TYPE (TREE_TYPE (type))))
	  && !pset)
	{
	  result = walk_tree_without_duplicates (&TREE_TYPE (type),
						 func, data);
	  if (result)
	    return result;

	  break;
	}

      /* ... fall through ... */

    case COMPLEX_TYPE:
      WALK_SUBTREE (TREE_TYPE (type));
      break;

    case METHOD_TYPE:
      WALK_SUBTREE (TYPE_METHOD_BASETYPE (type));

      /* Fall through.  */

    case FUNCTION_TYPE:
      WALK_SUBTREE (TREE_TYPE (type));
      {
	tree arg;

	/* We never want to walk into default arguments.  */
	for (arg = TYPE_ARG_TYPES (type); arg; arg = TREE_CHAIN (arg))
	  WALK_SUBTREE (TREE_VALUE (arg));
      }
      break;

    case ARRAY_TYPE:
      /* Don't follow this nodes's type if a pointer for fear that
	 we'll have infinite recursion.  If we have a PSET, then we
	 need not fear.  */
      if (pset
	  || (!POINTER_TYPE_P (TREE_TYPE (type))
	      && TREE_CODE (TREE_TYPE (type)) != OFFSET_TYPE))
	WALK_SUBTREE (TREE_TYPE (type));
      WALK_SUBTREE (TYPE_DOMAIN (type));
      break;

    case BOOLEAN_TYPE:
    case ENUMERAL_TYPE:
    case INTEGER_TYPE:
    case REAL_TYPE:
      WALK_SUBTREE (TYPE_MIN_VALUE (type));
      WALK_SUBTREE (TYPE_MAX_VALUE (type));
      break;

    case OFFSET_TYPE:
      WALK_SUBTREE (TREE_TYPE (type));
      WALK_SUBTREE (TYPE_OFFSET_BASETYPE (type));
      break;

    default:
      break;
    }

  return NULL_TREE;
}

/* Apply FUNC to all the sub-trees of TP in a pre-order traversal.  FUNC is
   called with the DATA and the address of each sub-tree.  If FUNC returns a
   non-NULL value, the traversal is stopped, and the value returned by FUNC
   is returned.  If PSET is non-NULL it is used to record the nodes visited,
   and to avoid visiting a node more than once.  */

tree
walk_tree (tree *tp, walk_tree_fn func, void *data, struct pointer_set_t *pset)
{
  enum tree_code code;
  int walk_subtrees;
  tree result;

#define WALK_SUBTREE_TAIL(NODE)				\
  do							\
    {							\
       tp = & (NODE);					\
       goto tail_recurse;				\
    }							\
  while (0)

 tail_recurse:
  /* Skip empty subtrees.  */
  if (!*tp)
    return NULL_TREE;

  /* Don't walk the same tree twice, if the user has requested
     that we avoid doing so.  */
  if (pset && pointer_set_insert (pset, *tp))
    return NULL_TREE;

  /* Call the function.  */
  walk_subtrees = 1;
  result = (*func) (tp, &walk_subtrees, data);

  /* If we found something, return it.  */
  if (result)
    return result;

  code = TREE_CODE (*tp);

  /* Even if we didn't, FUNC may have decided that there was nothing
     interesting below this point in the tree.  */
  if (!walk_subtrees)
    {
      /* But we still need to check our siblings.  */
      if (code == TREE_LIST)
	WALK_SUBTREE_TAIL (TREE_CHAIN (*tp));
      else if (code == OMP_CLAUSE)
	WALK_SUBTREE_TAIL (OMP_CLAUSE_CHAIN (*tp));
      else
	return NULL_TREE;
    }

  result = lang_hooks.tree_inlining.walk_subtrees (tp, &walk_subtrees, func,
						   data, pset);
  if (result || ! walk_subtrees)
    return result;

  switch (code)
    {
    case ERROR_MARK:
    case IDENTIFIER_NODE:
    case INTEGER_CST:
    case REAL_CST:
    case VECTOR_CST:
    case STRING_CST:
    case BLOCK:
    case PLACEHOLDER_EXPR:
    case SSA_NAME:
    case FIELD_DECL:
    case RESULT_DECL:
      /* None of these have subtrees other than those already walked
	 above.  */
      break;

    case TREE_LIST:
      WALK_SUBTREE (TREE_VALUE (*tp));
      WALK_SUBTREE_TAIL (TREE_CHAIN (*tp));
      break;

    case TREE_VEC:
      {
	int len = TREE_VEC_LENGTH (*tp);

	if (len == 0)
	  break;

	/* Walk all elements but the first.  */
	while (--len)
	  WALK_SUBTREE (TREE_VEC_ELT (*tp, len));

	/* Now walk the first one as a tail call.  */
	WALK_SUBTREE_TAIL (TREE_VEC_ELT (*tp, 0));
      }

    case COMPLEX_CST:
      WALK_SUBTREE (TREE_REALPART (*tp));
      WALK_SUBTREE_TAIL (TREE_IMAGPART (*tp));

    case CONSTRUCTOR:
      {
	unsigned HOST_WIDE_INT idx;
	constructor_elt *ce;

	for (idx = 0;
	     VEC_iterate(constructor_elt, CONSTRUCTOR_ELTS (*tp), idx, ce);
	     idx++)
	  WALK_SUBTREE (ce->value);
      }
      break;

    case SAVE_EXPR:
      WALK_SUBTREE_TAIL (TREE_OPERAND (*tp, 0));

    case BIND_EXPR:
      {
	tree decl;
	for (decl = BIND_EXPR_VARS (*tp); decl; decl = TREE_CHAIN (decl))
	  {
	    /* Walk the DECL_INITIAL and DECL_SIZE.  We don't want to walk
	       into declarations that are just mentioned, rather than
	       declared; they don't really belong to this part of the tree.
	       And, we can see cycles: the initializer for a declaration
	       can refer to the declaration itself.  */
	    WALK_SUBTREE (DECL_INITIAL (decl));
	    WALK_SUBTREE (DECL_SIZE (decl));
	    WALK_SUBTREE (DECL_SIZE_UNIT (decl));
	  }
	WALK_SUBTREE_TAIL (BIND_EXPR_BODY (*tp));
      }

    case STATEMENT_LIST:
      {
	tree_stmt_iterator i;
	for (i = tsi_start (*tp); !tsi_end_p (i); tsi_next (&i))
	  WALK_SUBTREE (*tsi_stmt_ptr (i));
      }
      break;

    case OMP_CLAUSE:
      switch (OMP_CLAUSE_CODE (*tp))
	{
	case OMP_CLAUSE_PRIVATE:
	case OMP_CLAUSE_SHARED:
	case OMP_CLAUSE_FIRSTPRIVATE:
	case OMP_CLAUSE_LASTPRIVATE:
	case OMP_CLAUSE_COPYIN:
	case OMP_CLAUSE_COPYPRIVATE:
	case OMP_CLAUSE_IF:
	case OMP_CLAUSE_NUM_THREADS:
	case OMP_CLAUSE_SCHEDULE:
	  WALK_SUBTREE (OMP_CLAUSE_OPERAND (*tp, 0));
	  /* FALLTHRU */

	case OMP_CLAUSE_NOWAIT:
	case OMP_CLAUSE_ORDERED:
	case OMP_CLAUSE_DEFAULT:
	  WALK_SUBTREE_TAIL (OMP_CLAUSE_CHAIN (*tp));

	case OMP_CLAUSE_REDUCTION:
	  {
	    int i;
	    for (i = 0; i < 4; i++)
	      WALK_SUBTREE (OMP_CLAUSE_OPERAND (*tp, i));
	    WALK_SUBTREE_TAIL (OMP_CLAUSE_CHAIN (*tp));
	  }

	default:
	  gcc_unreachable ();
	}
      break;

    case TARGET_EXPR:
      {
	int i, len;

	/* TARGET_EXPRs are peculiar: operands 1 and 3 can be the same.
	   But, we only want to walk once.  */
	len = (TREE_OPERAND (*tp, 3) == TREE_OPERAND (*tp, 1)) ? 2 : 3;
	for (i = 0; i < len; ++i)
	  WALK_SUBTREE (TREE_OPERAND (*tp, i));
	WALK_SUBTREE_TAIL (TREE_OPERAND (*tp, len));
      }

    case DECL_EXPR:
      /* Walk into various fields of the type that it's defining.  We only
	 want to walk into these fields of a type in this case.  Note that
	 decls get walked as part of the processing of a BIND_EXPR.

	 ??? Precisely which fields of types that we are supposed to walk in
	 this case vs. the normal case aren't well defined.  */
      if (TREE_CODE (DECL_EXPR_DECL (*tp)) == TYPE_DECL
	  && TREE_CODE (TREE_TYPE (DECL_EXPR_DECL (*tp))) != ERROR_MARK)
	{
	  tree *type_p = &TREE_TYPE (DECL_EXPR_DECL (*tp));

	  /* Call the function for the type.  See if it returns anything or
	     doesn't want us to continue.  If we are to continue, walk both
	     the normal fields and those for the declaration case.  */
	  result = (*func) (type_p, &walk_subtrees, data);
	  if (result || !walk_subtrees)
	    return NULL_TREE;

	  result = walk_type_fields (*type_p, func, data, pset);
	  if (result)
	    return result;

	  /* If this is a record type, also walk the fields.  */
	  if (TREE_CODE (*type_p) == RECORD_TYPE
	      || TREE_CODE (*type_p) == UNION_TYPE
	      || TREE_CODE (*type_p) == QUAL_UNION_TYPE)
	    {
	      tree field;

	      for (field = TYPE_FIELDS (*type_p); field;
		   field = TREE_CHAIN (field))
		{
		  /* We'd like to look at the type of the field, but we can
		     easily get infinite recursion.  So assume it's pointed
		     to elsewhere in the tree.  Also, ignore things that
		     aren't fields.  */
		  if (TREE_CODE (field) != FIELD_DECL)
		    continue;

		  WALK_SUBTREE (DECL_FIELD_OFFSET (field));
		  WALK_SUBTREE (DECL_SIZE (field));
		  WALK_SUBTREE (DECL_SIZE_UNIT (field));
		  if (TREE_CODE (*type_p) == QUAL_UNION_TYPE)
		    WALK_SUBTREE (DECL_QUALIFIER (field));
		}
	    }

	  WALK_SUBTREE (TYPE_SIZE (*type_p));
	  WALK_SUBTREE_TAIL (TYPE_SIZE_UNIT (*type_p));
	}
      /* FALLTHRU */

    default:
      if (IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (code)))
	{
	  int i, len;

	  /* Walk over all the sub-trees of this operand.  */
	  len = TREE_CODE_LENGTH (code);

	  /* Go through the subtrees.  We need to do this in forward order so
	     that the scope of a FOR_EXPR is handled properly.  */
	  if (len)
	    {
	      for (i = 0; i < len - 1; ++i)
		WALK_SUBTREE (TREE_OPERAND (*tp, i));
	      WALK_SUBTREE_TAIL (TREE_OPERAND (*tp, len - 1));
	    }
	}

      /* If this is a type, walk the needed fields in the type.  */
      else if (TYPE_P (*tp))
	return walk_type_fields (*tp, func, data, pset);
      break;
    }

  /* We didn't find what we were looking for.  */
  return NULL_TREE;

#undef WALK_SUBTREE_TAIL
}
#undef WALK_SUBTREE

/* Like walk_tree, but does not walk duplicate nodes more than once.  */

tree
walk_tree_without_duplicates (tree *tp, walk_tree_fn func, void *data)
{
  tree result;
  struct pointer_set_t *pset;

  pset = pointer_set_create ();
  result = walk_tree (tp, func, data, pset);
  pointer_set_destroy (pset);
  return result;
}


/* Return true if STMT is an empty statement or contains nothing but
   empty statements.  */

bool
empty_body_p (tree stmt)
{
  tree_stmt_iterator i;
  tree body;

  if (IS_EMPTY_STMT (stmt))
    return true;
  else if (TREE_CODE (stmt) == BIND_EXPR)
    body = BIND_EXPR_BODY (stmt);
  else if (TREE_CODE (stmt) == STATEMENT_LIST)
    body = stmt;
  else
    return false;

  for (i = tsi_start (body); !tsi_end_p (i); tsi_next (&i))
    if (!empty_body_p (tsi_stmt (i)))
      return false;

  return true;
}


#include "gt-tree.h"
