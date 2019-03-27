/* Front-end tree definitions for GNU compiler.
   Copyright (C) 1989, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

#ifndef GCC_TREE_H
#define GCC_TREE_H

#include <sys/param.h>
#ifndef __PAST_END
# define __PAST_END(array, offset) (((typeof(*(array)) *)(array))[offset])
#endif

#include "hashtab.h"
#include "machmode.h"
#include "input.h"
#include "statistics.h"
#include "vec.h"

/* Codes of tree nodes */

#define DEFTREECODE(SYM, STRING, TYPE, NARGS)   SYM,

enum tree_code {
#include "tree.def"

  LAST_AND_UNUSED_TREE_CODE	/* A convenient way to get a value for
				   NUM_TREE_CODES.  */
};

#undef DEFTREECODE

extern unsigned char tree_contains_struct[256][64];
#define CODE_CONTAINS_STRUCT(CODE, STRUCT) (tree_contains_struct[(CODE)][(STRUCT)])

/* Number of language-independent tree codes.  */
#define NUM_TREE_CODES ((int) LAST_AND_UNUSED_TREE_CODE)

/* Tree code classes.  */

/* Each tree_code has an associated code class represented by a
   TREE_CODE_CLASS.  */

enum tree_code_class {
  tcc_exceptional, /* An exceptional code (fits no category).  */
  tcc_constant,    /* A constant.  */
  /* Order of tcc_type and tcc_declaration is important.  */
  tcc_type,        /* A type object code.  */
  tcc_declaration, /* A declaration (also serving as variable refs).  */
  tcc_reference,   /* A reference to storage.  */
  tcc_comparison,  /* A comparison expression.  */
  tcc_unary,       /* A unary arithmetic expression.  */
  tcc_binary,      /* A binary arithmetic expression.  */
  tcc_statement,   /* A statement expression, which have side effects
		      but usually no interesting value.  */
  tcc_expression   /* Any other expression.  */
};

/* Each tree code class has an associated string representation.
   These must correspond to the tree_code_class entries.  */

extern const char *const tree_code_class_strings[];

/* Returns the string representing CLASS.  */

#define TREE_CODE_CLASS_STRING(CLASS)\
        tree_code_class_strings[(int) (CLASS)]

#define MAX_TREE_CODES 256
extern const enum tree_code_class tree_code_type[];
#define TREE_CODE_CLASS(CODE)	tree_code_type[(int) (CODE)]

/* Nonzero if CODE represents an exceptional code.  */

#define EXCEPTIONAL_CLASS_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_exceptional)

/* Nonzero if CODE represents a constant.  */

#define CONSTANT_CLASS_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_constant)

/* Nonzero if CODE represents a type.  */

#define TYPE_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_type)

/* Nonzero if CODE represents a declaration.  */

#define DECL_P(CODE)\
        (TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_declaration)

/* Nonzero if CODE represents a memory tag.  */

#define MTAG_P(CODE) \
  (TREE_CODE (CODE) == STRUCT_FIELD_TAG		\
   || TREE_CODE (CODE) == NAME_MEMORY_TAG	\
   || TREE_CODE (CODE) == SYMBOL_MEMORY_TAG)


/* Nonzero if DECL represents a VAR_DECL or FUNCTION_DECL.  */

#define VAR_OR_FUNCTION_DECL_P(DECL)\
  (TREE_CODE (DECL) == VAR_DECL || TREE_CODE (DECL) == FUNCTION_DECL)

/* Nonzero if CODE represents a INDIRECT_REF.  Keep these checks in
   ascending code order.  */

#define INDIRECT_REF_P(CODE)\
  (TREE_CODE (CODE) == INDIRECT_REF \
   || TREE_CODE (CODE) == ALIGN_INDIRECT_REF \
   || TREE_CODE (CODE) == MISALIGNED_INDIRECT_REF)

/* Nonzero if CODE represents a reference.  */

#define REFERENCE_CLASS_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_reference)

/* Nonzero if CODE represents a comparison.  */

#define COMPARISON_CLASS_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_comparison)

/* Nonzero if CODE represents a unary arithmetic expression.  */

#define UNARY_CLASS_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_unary)

/* Nonzero if CODE represents a binary arithmetic expression.  */

#define BINARY_CLASS_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_binary)

/* Nonzero if CODE represents a statement expression.  */

#define STATEMENT_CLASS_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_statement)

/* Nonzero if CODE represents any other expression.  */

#define EXPRESSION_CLASS_P(CODE)\
	(TREE_CODE_CLASS (TREE_CODE (CODE)) == tcc_expression)

/* Returns nonzero iff CODE represents a type or declaration.  */

#define IS_TYPE_OR_DECL_P(CODE)\
	(TYPE_P (CODE) || DECL_P (CODE))

/* Returns nonzero iff CLASS is the tree-code class of an
   expression.  */

#define IS_EXPR_CODE_CLASS(CLASS)\
	((CLASS) >= tcc_reference && (CLASS) <= tcc_expression)

/* Returns nonzero iff NODE is an expression of some kind.  */

#define EXPR_P(NODE) IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (TREE_CODE (NODE)))

/* Returns nonzero iff NODE is an OpenMP directive.  */

#define OMP_DIRECTIVE_P(NODE)				\
    (TREE_CODE (NODE) == OMP_PARALLEL			\
     || TREE_CODE (NODE) == OMP_FOR			\
     || TREE_CODE (NODE) == OMP_SECTIONS		\
     || TREE_CODE (NODE) == OMP_SINGLE			\
     || TREE_CODE (NODE) == OMP_SECTION			\
     || TREE_CODE (NODE) == OMP_MASTER			\
     || TREE_CODE (NODE) == OMP_ORDERED			\
     || TREE_CODE (NODE) == OMP_CRITICAL		\
     || TREE_CODE (NODE) == OMP_RETURN			\
     || TREE_CODE (NODE) == OMP_CONTINUE)

/* Number of argument-words in each kind of tree-node.  */

extern const unsigned char tree_code_length[];
#define TREE_CODE_LENGTH(CODE)	tree_code_length[(int) (CODE)]

/* Names of tree components.  */

extern const char *const tree_code_name[];

/* A vectors of trees.  */
DEF_VEC_P(tree);
DEF_VEC_ALLOC_P(tree,gc);
DEF_VEC_ALLOC_P(tree,heap);


/* Classify which part of the compiler has defined a given builtin function.
   Note that we assume below that this is no more than two bits.  */
enum built_in_class
{
  NOT_BUILT_IN = 0,
  BUILT_IN_FRONTEND,
  BUILT_IN_MD,
  BUILT_IN_NORMAL
};

/* Names for the above.  */
extern const char *const built_in_class_names[4];

/* Codes that identify the various built in functions
   so that expand_call can identify them quickly.  */

#define DEF_BUILTIN(ENUM, N, C, T, LT, B, F, NA, AT, IM, COND) ENUM,
enum built_in_function
{
#include "builtins.def"

  /* Complex division routines in libgcc.  These are done via builtins
     because emit_library_call_value can't handle complex values.  */
  BUILT_IN_COMPLEX_MUL_MIN,
  BUILT_IN_COMPLEX_MUL_MAX
    = BUILT_IN_COMPLEX_MUL_MIN
      + MAX_MODE_COMPLEX_FLOAT
      - MIN_MODE_COMPLEX_FLOAT,

  BUILT_IN_COMPLEX_DIV_MIN,
  BUILT_IN_COMPLEX_DIV_MAX
    = BUILT_IN_COMPLEX_DIV_MIN
      + MAX_MODE_COMPLEX_FLOAT
      - MIN_MODE_COMPLEX_FLOAT,

  /* Upper bound on non-language-specific builtins.  */
  END_BUILTINS
};
#undef DEF_BUILTIN

/* Names for the above.  */
extern const char * built_in_names[(int) END_BUILTINS];

/* Helper macros for math builtins.  */

#define BUILTIN_EXP10_P(FN) \
 ((FN) == BUILT_IN_EXP10 || (FN) == BUILT_IN_EXP10F || (FN) == BUILT_IN_EXP10L \
  || (FN) == BUILT_IN_POW10 || (FN) == BUILT_IN_POW10F || (FN) == BUILT_IN_POW10L)

#define BUILTIN_EXPONENT_P(FN) (BUILTIN_EXP10_P (FN) \
  || (FN) == BUILT_IN_EXP || (FN) == BUILT_IN_EXPF || (FN) == BUILT_IN_EXPL \
  || (FN) == BUILT_IN_EXP2 || (FN) == BUILT_IN_EXP2F || (FN) == BUILT_IN_EXP2L)

#define BUILTIN_SQRT_P(FN) \
 ((FN) == BUILT_IN_SQRT || (FN) == BUILT_IN_SQRTF || (FN) == BUILT_IN_SQRTL)

#define BUILTIN_CBRT_P(FN) \
 ((FN) == BUILT_IN_CBRT || (FN) == BUILT_IN_CBRTF || (FN) == BUILT_IN_CBRTL)

#define BUILTIN_ROOT_P(FN) (BUILTIN_SQRT_P (FN) || BUILTIN_CBRT_P (FN))

#define CASE_FLT_FN(FN) case FN: case FN##F: case FN##L
#define CASE_INT_FN(FN) case FN: case FN##L: case FN##LL

/* An array of _DECL trees for the above.  */
extern GTY(()) tree built_in_decls[(int) END_BUILTINS];
extern GTY(()) tree implicit_built_in_decls[(int) END_BUILTINS];

/* In an OMP_CLAUSE node.  */

/* Number of operands and names for each clause.  */
extern unsigned const char omp_clause_num_ops[];
extern const char * const omp_clause_code_name[];

/* Clause codes.  Do not reorder, as this is used to index into the tables
   omp_clause_num_ops and omp_clause_code_name.  */
enum omp_clause_code
{
  /* Clause zero is special-cased inside the parser
     (c_parser_omp_variable_list).  */
  OMP_CLAUSE_ERROR = 0,

  /* OpenMP clause: private (variable_list).  */
  OMP_CLAUSE_PRIVATE,

  /* OpenMP clause: shared (variable_list).  */
  OMP_CLAUSE_SHARED,

  /* OpenMP clause: firstprivate (variable_list).  */
  OMP_CLAUSE_FIRSTPRIVATE,

  /* OpenMP clause: lastprivate (variable_list).  */
  OMP_CLAUSE_LASTPRIVATE,

  /* OpenMP clause: reduction (operator:variable_list).
     OMP_CLAUSE_REDUCTION_CODE: The tree_code of the operator.
     Operand 1: OMP_CLAUSE_REDUCTION_INIT: Stmt-list to initialize the var.
     Operand 2: OMP_CLAUSE_REDUCTION_MERGE: Stmt-list to merge private var
                into the shared one.
     Operand 3: OMP_CLAUSE_REDUCTION_PLACEHOLDER: A dummy VAR_DECL
                placeholder used in OMP_CLAUSE_REDUCTION_MERGE.  */
  OMP_CLAUSE_REDUCTION,

  /* OpenMP clause: copyin (variable_list).  */
  OMP_CLAUSE_COPYIN,

  /* OpenMP clause: copyprivate (variable_list).  */
  OMP_CLAUSE_COPYPRIVATE,

  /* OpenMP clause: if (scalar-expression).  */
  OMP_CLAUSE_IF,

  /* OpenMP clause: num_threads (integer-expression).  */
  OMP_CLAUSE_NUM_THREADS,

  /* OpenMP clause: schedule.  */
  OMP_CLAUSE_SCHEDULE,

  /* OpenMP clause: nowait.  */
  OMP_CLAUSE_NOWAIT,

  /* OpenMP clause: ordered.  */
  OMP_CLAUSE_ORDERED,

  /* OpenMP clause: default.  */
  OMP_CLAUSE_DEFAULT
};

/* The definition of tree nodes fills the next several pages.  */

/* A tree node can represent a data type, a variable, an expression
   or a statement.  Each node has a TREE_CODE which says what kind of
   thing it represents.  Some common codes are:
   INTEGER_TYPE -- represents a type of integers.
   ARRAY_TYPE -- represents a type of pointer.
   VAR_DECL -- represents a declared variable.
   INTEGER_CST -- represents a constant integer value.
   PLUS_EXPR -- represents a sum (an expression).

   As for the contents of a tree node: there are some fields
   that all nodes share.  Each TREE_CODE has various special-purpose
   fields as well.  The fields of a node are never accessed directly,
   always through accessor macros.  */

/* Every kind of tree node starts with this structure,
   so all nodes have these fields.

   See the accessor macros, defined below, for documentation of the
   fields.  */
union tree_ann_d;

struct tree_common GTY(())
{
  tree chain;
  tree type;
  union tree_ann_d *ann;

  ENUM_BITFIELD(tree_code) code : 8;

  unsigned side_effects_flag : 1;
  unsigned constant_flag : 1;
  unsigned addressable_flag : 1;
  unsigned volatile_flag : 1;
  unsigned readonly_flag : 1;
  unsigned unsigned_flag : 1;
  unsigned asm_written_flag: 1;
  unsigned nowarning_flag : 1;

  unsigned used_flag : 1;
  unsigned nothrow_flag : 1;
  unsigned static_flag : 1;
  unsigned public_flag : 1;
  unsigned private_flag : 1;
  unsigned protected_flag : 1;
  unsigned deprecated_flag : 1;
  unsigned invariant_flag : 1;

  unsigned lang_flag_0 : 1;
  unsigned lang_flag_1 : 1;
  unsigned lang_flag_2 : 1;
  unsigned lang_flag_3 : 1;
  unsigned lang_flag_4 : 1;
  unsigned lang_flag_5 : 1;
  unsigned lang_flag_6 : 1;
  unsigned visited : 1;
  /* APPLE LOCAL "unavailable" attribute (Radar 2809697) --ilr */
  unsigned unavailable_flag : 1;
};

/* The following table lists the uses of each of the above flags and
   for which types of nodes they are defined.  Note that expressions
   include decls.

   addressable_flag:

       TREE_ADDRESSABLE in
	   VAR_DECL, FUNCTION_DECL, FIELD_DECL, CONSTRUCTOR, LABEL_DECL,
	   ..._TYPE, IDENTIFIER_NODE.
	   In a STMT_EXPR, it means we want the result of the enclosed
	   expression.
       CALL_EXPR_TAILCALL in CALL_EXPR
       CASE_LOW_SEEN in CASE_LABEL_EXPR

   static_flag:

       TREE_STATIC in
           VAR_DECL, FUNCTION_DECL, CONSTRUCTOR, ADDR_EXPR
       BINFO_VIRTUAL_P in
           TREE_BINFO
       TREE_CONSTANT_OVERFLOW in
           INTEGER_CST, REAL_CST, COMPLEX_CST, VECTOR_CST
       TREE_SYMBOL_REFERENCED in
           IDENTIFIER_NODE
       CLEANUP_EH_ONLY in
           TARGET_EXPR, WITH_CLEANUP_EXPR
       ASM_INPUT_P in
           ASM_EXPR
       EH_FILTER_MUST_NOT_THROW in EH_FILTER_EXPR
       TYPE_REF_CAN_ALIAS_ALL in
           POINTER_TYPE, REFERENCE_TYPE
       CASE_HIGH_SEEN in CASE_LABEL_EXPR

   public_flag:

       TREE_OVERFLOW in
           INTEGER_CST, REAL_CST, COMPLEX_CST, VECTOR_CST
       TREE_PUBLIC in
           VAR_DECL or FUNCTION_DECL or IDENTIFIER_NODE
       ASM_VOLATILE_P in
           ASM_EXPR
       TYPE_CACHED_VALUES_P in
          ..._TYPE
       SAVE_EXPR_RESOLVED_P in
	  SAVE_EXPR
       OMP_CLAUSE_LASTPRIVATE_FIRSTPRIVATE in
	  OMP_CLAUSE_LASTPRIVATE
       OMP_CLAUSE_PRIVATE_DEBUG in
	  OMP_CLAUSE_PRIVATE

   private_flag:

       TREE_PRIVATE in
           ..._DECL
       CALL_EXPR_RETURN_SLOT_OPT in
           CALL_EXPR
       DECL_BY_REFERENCE in
           PARM_DECL, RESULT_DECL
       OMP_RETURN_NOWAIT in
	   OMP_RETURN
       OMP_SECTION_LAST in
	   OMP_SECTION
       OMP_PARALLEL_COMBINED in
	   OMP_PARALLEL

   protected_flag:

       TREE_PROTECTED in
           BLOCK
	   ..._DECL
       CALL_FROM_THUNK_P in
           CALL_EXPR

   side_effects_flag:

       TREE_SIDE_EFFECTS in
           all expressions
	   all decls
	   all constants

       FORCED_LABEL in
	   LABEL_DECL

   volatile_flag:

       TREE_THIS_VOLATILE in
           all expressions
       TYPE_VOLATILE in
           ..._TYPE

   readonly_flag:

       TREE_READONLY in
           all expressions
       TYPE_READONLY in
           ..._TYPE

   constant_flag:

       TREE_CONSTANT in
           all expressions
	   all decls
	   all constants
       TYPE_SIZES_GIMPLIFIED
           ..._TYPE

   unsigned_flag:

       TYPE_UNSIGNED in
           all types
       DECL_UNSIGNED in
           all decls
       BIT_FIELD_REF_UNSIGNED in
           BIT_FIELD_REF

   asm_written_flag:

       TREE_ASM_WRITTEN in
           VAR_DECL, FUNCTION_DECL, RECORD_TYPE, UNION_TYPE, QUAL_UNION_TYPE
	   BLOCK, SSA_NAME

   used_flag:

       TREE_USED in
           expressions, IDENTIFIER_NODE

   nothrow_flag:

       TREE_NOTHROW in
           CALL_EXPR, FUNCTION_DECL

       TYPE_ALIGN_OK in
	   ..._TYPE

       TREE_THIS_NOTRAP in
          (ALIGN/MISALIGNED_)INDIRECT_REF, ARRAY_REF, ARRAY_RANGE_REF

   deprecated_flag:

	TREE_DEPRECATED in
	   ..._DECL

	IDENTIFIER_TRANSPARENT_ALIAS in
	   IDENTIFIER_NODE

   APPLE LOCAL begin "unavailable" attribute (Radar 2809697)
   unavailable_flag:

	TREE_UNAVAILABLE in
	   ..._DECL
   APPLE LOCAL end "unavailable" attribute (Radar 2809697)

   visited:

   	Used in tree traversals to mark visited nodes.

   invariant_flag:

	TREE_INVARIANT in
	    all expressions.

   nowarning_flag:

       TREE_NO_WARNING in
           ... any expr or decl node
*/
#undef DEFTREESTRUCT
#define DEFTREESTRUCT(ENUM, NAME) ENUM,
enum tree_node_structure_enum {
#include "treestruct.def"
  LAST_TS_ENUM
};
#undef DEFTREESTRUCT

/* Define accessors for the fields that all tree nodes have
   (though some fields are not used for all kinds of nodes).  */

/* The tree-code says what kind of node it is.
   Codes are defined in tree.def.  */
#define TREE_CODE(NODE) ((enum tree_code) (NODE)->common.code)
#define TREE_SET_CODE(NODE, VALUE) ((NODE)->common.code = (VALUE))

/* When checking is enabled, errors will be generated if a tree node
   is accessed incorrectly. The macros die with a fatal error.  */
#if defined ENABLE_TREE_CHECKING && (GCC_VERSION >= 2007)

#define TREE_CHECK(T, CODE) __extension__				\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) != (CODE))					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__, 	\
			 (CODE), 0);					\
    __t; })

#define TREE_NOT_CHECK(T, CODE) __extension__				\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) == (CODE))					\
      tree_not_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,	\
			     (CODE), 0);				\
    __t; })

#define TREE_CHECK2(T, CODE1, CODE2) __extension__			\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) != (CODE1)					\
	&& TREE_CODE (__t) != (CODE2))					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,		\
 			 (CODE1), (CODE2), 0);				\
    __t; })

#define TREE_NOT_CHECK2(T, CODE1, CODE2) __extension__			\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) == (CODE1)					\
	|| TREE_CODE (__t) == (CODE2))					\
      tree_not_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,	\
			     (CODE1), (CODE2), 0);			\
    __t; })

#define TREE_CHECK3(T, CODE1, CODE2, CODE3) __extension__		\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) != (CODE1)					\
	&& TREE_CODE (__t) != (CODE2)					\
	&& TREE_CODE (__t) != (CODE3))					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,		\
			     (CODE1), (CODE2), (CODE3), 0);		\
    __t; })

#define TREE_NOT_CHECK3(T, CODE1, CODE2, CODE3) __extension__		\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) == (CODE1)					\
	|| TREE_CODE (__t) == (CODE2)					\
	|| TREE_CODE (__t) == (CODE3))					\
      tree_not_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,	\
			     (CODE1), (CODE2), (CODE3), 0);		\
    __t; })

#define TREE_CHECK4(T, CODE1, CODE2, CODE3, CODE4) __extension__	\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) != (CODE1)					\
	&& TREE_CODE (__t) != (CODE2)					\
	&& TREE_CODE (__t) != (CODE3)					\
	&& TREE_CODE (__t) != (CODE4))					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,		\
			     (CODE1), (CODE2), (CODE3), (CODE4), 0);	\
    __t; })

#define NON_TREE_CHECK4(T, CODE1, CODE2, CODE3, CODE4) __extension__	\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) == (CODE1)					\
	|| TREE_CODE (__t) == (CODE2)					\
	|| TREE_CODE (__t) == (CODE3)					\
	|| TREE_CODE (__t) == (CODE4))					\
      tree_not_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,	\
			     (CODE1), (CODE2), (CODE3), (CODE4), 0);	\
    __t; })

#define TREE_CHECK5(T, CODE1, CODE2, CODE3, CODE4, CODE5) __extension__	\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) != (CODE1)					\
	&& TREE_CODE (__t) != (CODE2)					\
	&& TREE_CODE (__t) != (CODE3)					\
	&& TREE_CODE (__t) != (CODE4)					\
	&& TREE_CODE (__t) != (CODE5))					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,		\
			     (CODE1), (CODE2), (CODE3), (CODE4), (CODE5), 0);\
    __t; })

#define TREE_NOT_CHECK5(T, CODE1, CODE2, CODE3, CODE4, CODE5) __extension__ \
({  const tree __t = (T);						\
    if (TREE_CODE (__t) == (CODE1)					\
	|| TREE_CODE (__t) == (CODE2)					\
	|| TREE_CODE (__t) == (CODE3)					\
	|| TREE_CODE (__t) == (CODE4)					\
	|| TREE_CODE (__t) == (CODE5))					\
      tree_not_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,	\
			     (CODE1), (CODE2), (CODE3), (CODE4), (CODE5), 0);\
    __t; })

#define CONTAINS_STRUCT_CHECK(T, STRUCT) __extension__			\
({  const tree __t = (T);						\
  if (tree_contains_struct[TREE_CODE(__t)][(STRUCT)] != 1)		\
      tree_contains_struct_check_failed (__t, (STRUCT), __FILE__, __LINE__,	\
			       __FUNCTION__);				\
    __t; })

#define TREE_CLASS_CHECK(T, CLASS) __extension__			\
({  const tree __t = (T);						\
    if (TREE_CODE_CLASS (TREE_CODE(__t)) != (CLASS))			\
      tree_class_check_failed (__t, (CLASS), __FILE__, __LINE__,	\
			       __FUNCTION__);				\
    __t; })

#define TREE_RANGE_CHECK(T, CODE1, CODE2) __extension__			\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) < (CODE1) || TREE_CODE (__t) > (CODE2))		\
      tree_range_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,	\
			       (CODE1), (CODE2));			\
    __t; })

#define OMP_CLAUSE_SUBCODE_CHECK(T, CODE) __extension__			\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) != OMP_CLAUSE)					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,  	\
			 OMP_CLAUSE, 0);				\
    if (__t->omp_clause.code != (CODE))					\
      omp_clause_check_failed (__t, __FILE__, __LINE__, __FUNCTION__, 	\
			       (CODE));					\
    __t; })

#define OMP_CLAUSE_RANGE_CHECK(T, CODE1, CODE2) __extension__		\
({  const tree __t = (T);						\
    if (TREE_CODE (__t) != OMP_CLAUSE)					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,  	\
			 OMP_CLAUSE, 0);				\
    if ((int) __t->omp_clause.code < (int) (CODE1)			\
        || (int) __t->omp_clause.code > (int) (CODE2))			\
      omp_clause_range_check_failed (__t, __FILE__, __LINE__,		\
				     __FUNCTION__, (CODE1), (CODE2));	\
    __t; })

/* These checks have to be special cased.  */
#define EXPR_CHECK(T) __extension__					\
({  const tree __t = (T);						\
    char const __c = TREE_CODE_CLASS (TREE_CODE (__t));			\
    if (!IS_EXPR_CODE_CLASS (__c))					\
      tree_class_check_failed (__t, tcc_expression, __FILE__, __LINE__,	\
			       __FUNCTION__);				\
    __t; })

/* These checks have to be special cased.  */
#define NON_TYPE_CHECK(T) __extension__					\
({  const tree __t = (T);						\
    if (TYPE_P (__t))							\
      tree_not_class_check_failed (__t, tcc_type, __FILE__, __LINE__,	\
				   __FUNCTION__);			\
    __t; })

#define TREE_VEC_ELT_CHECK(T, I) __extension__				\
(*({const tree __t = (T);						\
    const int __i = (I);						\
    if (TREE_CODE (__t) != TREE_VEC)					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,		\
  			 TREE_VEC, 0);					\
    if (__i < 0 || __i >= __t->vec.length)				\
      tree_vec_elt_check_failed (__i, __t->vec.length,			\
				 __FILE__, __LINE__, __FUNCTION__);	\
    &__t->vec.a[__i]; }))

#define PHI_NODE_ELT_CHECK(t, i) __extension__				\
(*({const tree __t = t;							\
    const int __i = (i);						\
    if (TREE_CODE (__t) != PHI_NODE)					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,  	\
			 PHI_NODE, 0);					\
    if (__i < 0 || __i >= __t->phi.capacity)				\
      phi_node_elt_check_failed (__i, __t->phi.num_args,		\
				 __FILE__, __LINE__, __FUNCTION__);	\
    &__t->phi.a[__i]; }))

#define OMP_CLAUSE_ELT_CHECK(t, i) __extension__			\
(*({const tree __t = t;							\
    const int __i = (i);						\
    if (TREE_CODE (__t) != OMP_CLAUSE)					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__,  	\
			 OMP_CLAUSE, 0);				\
    if (__i < 0 || __i >= omp_clause_num_ops [__t->omp_clause.code])	\
      omp_clause_operand_check_failed (__i, __t, __FILE__, __LINE__,	\
	                               __FUNCTION__);			\
    &__t->omp_clause.ops[__i]; }))

/* Special checks for TREE_OPERANDs.  */
#define TREE_OPERAND_CHECK(T, I) __extension__				\
(*({const tree __t = EXPR_CHECK (T);					\
    const int __i = (I);						\
    if (__i < 0 || __i >= TREE_CODE_LENGTH (TREE_CODE (__t)))		\
      tree_operand_check_failed (__i, TREE_CODE (__t),			\
				 __FILE__, __LINE__, __FUNCTION__);	\
    &__t->exp.operands[__i]; }))

#define TREE_OPERAND_CHECK_CODE(T, CODE, I) __extension__		\
(*({const tree __t = (T);						\
    const int __i = (I);						\
    if (TREE_CODE (__t) != CODE)					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__, (CODE), 0);\
    if (__i < 0 || __i >= TREE_CODE_LENGTH (CODE))			\
      tree_operand_check_failed (__i, (CODE),				\
				 __FILE__, __LINE__, __FUNCTION__);	\
    &__t->exp.operands[__i]; }))

#define TREE_RTL_OPERAND_CHECK(T, CODE, I) __extension__		\
(*(rtx *)								\
 ({const tree __t = (T);						\
    const int __i = (I);						\
    if (TREE_CODE (__t) != (CODE))					\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__, (CODE), 0); \
    if (__i < 0 || __i >= TREE_CODE_LENGTH ((CODE)))			\
      tree_operand_check_failed (__i, (CODE),				\
				 __FILE__, __LINE__, __FUNCTION__);	\
    &__t->exp.operands[__i]; }))

extern void tree_contains_struct_check_failed (const tree,
					       const enum tree_node_structure_enum,
					       const char *, int, const char *)
  ATTRIBUTE_NORETURN;

extern void tree_check_failed (const tree, const char *, int, const char *,
			       ...) ATTRIBUTE_NORETURN;
extern void tree_not_check_failed (const tree, const char *, int, const char *,
				   ...) ATTRIBUTE_NORETURN;
extern void tree_class_check_failed (const tree, const enum tree_code_class,
				     const char *, int, const char *)
    ATTRIBUTE_NORETURN;
extern void tree_range_check_failed (const tree, const char *, int,
				     const char *, enum tree_code,
				     enum tree_code);
extern void tree_not_class_check_failed (const tree,
					 const enum tree_code_class,
					 const char *, int, const char *)
    ATTRIBUTE_NORETURN;
extern void tree_vec_elt_check_failed (int, int, const char *,
				       int, const char *)
    ATTRIBUTE_NORETURN;
extern void phi_node_elt_check_failed (int, int, const char *,
				       int, const char *)
    ATTRIBUTE_NORETURN;
extern void tree_operand_check_failed (int, enum tree_code,
				       const char *, int, const char *)
    ATTRIBUTE_NORETURN;
extern void omp_clause_check_failed (const tree, const char *, int,
				     const char *, enum omp_clause_code)
    ATTRIBUTE_NORETURN;
extern void omp_clause_operand_check_failed (int, tree, const char *,
				             int, const char *)
    ATTRIBUTE_NORETURN;
extern void omp_clause_range_check_failed (const tree, const char *, int,
			       const char *, enum omp_clause_code,
			       enum omp_clause_code)
    ATTRIBUTE_NORETURN;

#else /* not ENABLE_TREE_CHECKING, or not gcc */

#define CONTAINS_STRUCT_CHECK(T, ENUM)          (T)
#define TREE_CHECK(T, CODE)			(T)
#define TREE_NOT_CHECK(T, CODE)			(T)
#define TREE_CHECK2(T, CODE1, CODE2)		(T)
#define TREE_NOT_CHECK2(T, CODE1, CODE2)	(T)
#define TREE_CHECK3(T, CODE1, CODE2, CODE3)	(T)
#define TREE_NOT_CHECK3(T, CODE1, CODE2, CODE3)	(T)
#define TREE_CHECK4(T, CODE1, CODE2, CODE3, CODE4) (T)
#define TREE_NOT_CHECK4(T, CODE1, CODE2, CODE3, CODE4) (T)
#define TREE_CHECK5(T, CODE1, CODE2, CODE3, CODE4, CODE5) (T)
#define TREE_NOT_CHECK5(T, CODE1, CODE2, CODE3, CODE4, CODE5) (T)
#define TREE_CLASS_CHECK(T, CODE)		(T)
#define TREE_RANGE_CHECK(T, CODE1, CODE2)	(T)
#define EXPR_CHECK(T)				(T)
#define NON_TYPE_CHECK(T)			(T)
#define TREE_VEC_ELT_CHECK(T, I)		__PAST_END((T)->vec.a, I)
#define TREE_OPERAND_CHECK(T, I)		__PAST_END((T)->exp.operands, I)
#define TREE_OPERAND_CHECK_CODE(T, CODE, I)	__PAST_END((T)->exp.operands, I)
#define TREE_RTL_OPERAND_CHECK(T, CODE, I)  (*(rtx *) &((T)->exp.operands[I]))
#define PHI_NODE_ELT_CHECK(T, i)	((T)->phi.a[i])
#define OMP_CLAUSE_ELT_CHECK(T, i)	        __PAST_END((T)->omp_clause.ops, i)
#define OMP_CLAUSE_RANGE_CHECK(T, CODE1, CODE2)	(T)
#define OMP_CLAUSE_SUBCODE_CHECK(T, CODE)	(T)

#endif

#define TREE_BLOCK(NODE)		(EXPR_CHECK (NODE)->exp.block)

#include "tree-check.h"

#define TYPE_CHECK(T)		TREE_CLASS_CHECK (T, tcc_type)
#define DECL_MINIMAL_CHECK(T)   CONTAINS_STRUCT_CHECK (T, TS_DECL_MINIMAL)
#define TREE_MEMORY_TAG_CHECK(T)       CONTAINS_STRUCT_CHECK (T, TS_MEMORY_TAG)
#define DECL_COMMON_CHECK(T)    CONTAINS_STRUCT_CHECK (T, TS_DECL_COMMON)
#define DECL_WRTL_CHECK(T)      CONTAINS_STRUCT_CHECK (T, TS_DECL_WRTL)
#define DECL_WITH_VIS_CHECK(T)  CONTAINS_STRUCT_CHECK (T, TS_DECL_WITH_VIS)
#define DECL_NON_COMMON_CHECK(T) CONTAINS_STRUCT_CHECK (T, TS_DECL_NON_COMMON)
#define CST_CHECK(T)		TREE_CLASS_CHECK (T, tcc_constant)
#define STMT_CHECK(T)		TREE_CLASS_CHECK (T, tcc_statement)
#define FUNC_OR_METHOD_CHECK(T)	TREE_CHECK2 (T, FUNCTION_TYPE, METHOD_TYPE)
/* APPLE LOCAL blocks 5862465 */
#define PTR_OR_REF_CHECK(T)	TREE_CHECK3 (T, POINTER_TYPE, REFERENCE_TYPE, BLOCK_POINTER_TYPE)

#define RECORD_OR_UNION_CHECK(T)	\
  TREE_CHECK3 (T, RECORD_TYPE, UNION_TYPE, QUAL_UNION_TYPE)
#define NOT_RECORD_OR_UNION_CHECK(T) \
  TREE_NOT_CHECK3 (T, RECORD_TYPE, UNION_TYPE, QUAL_UNION_TYPE)

#define NUMERICAL_TYPE_CHECK(T)					\
  TREE_CHECK4 (T, INTEGER_TYPE, ENUMERAL_TYPE, BOOLEAN_TYPE, REAL_TYPE)

/* In all nodes that are expressions, this is the data type of the expression.
   In POINTER_TYPE nodes, this is the type that the pointer points to.
   In ARRAY_TYPE nodes, this is the type of the elements.
   In VECTOR_TYPE nodes, this is the type of the elements.  */
#define TREE_TYPE(NODE) ((NODE)->common.type)

/* Here is how primitive or already-canonicalized types' hash codes
   are made.  */
#define TYPE_HASH(TYPE) (TYPE_UID (TYPE))

/* A simple hash function for an arbitrary tree node.  This must not be
   used in hash tables which are saved to a PCH.  */
#define TREE_HASH(NODE) ((size_t) (NODE) & 0777777)

/* Nodes are chained together for many purposes.
   Types are chained together to record them for being output to the debugger
   (see the function `chain_type').
   Decls in the same scope are chained together to record the contents
   of the scope.
   Statement nodes for successive statements used to be chained together.
   Often lists of things are represented by TREE_LIST nodes that
   are chained together.  */

#define TREE_CHAIN(NODE) ((NODE)->common.chain)

/* Given an expression as a tree, strip any NON_LVALUE_EXPRs and NOP_EXPRs
   that don't change the machine mode.  */

#define STRIP_NOPS(EXP)						\
  while ((TREE_CODE (EXP) == NOP_EXPR				\
	  || TREE_CODE (EXP) == CONVERT_EXPR			\
	  || TREE_CODE (EXP) == NON_LVALUE_EXPR)		\
	 && TREE_OPERAND (EXP, 0) != error_mark_node		\
	 && (TYPE_MODE (TREE_TYPE (EXP))			\
	     == TYPE_MODE (TREE_TYPE (TREE_OPERAND (EXP, 0)))))	\
    (EXP) = TREE_OPERAND (EXP, 0)

/* Like STRIP_NOPS, but don't let the signedness change either.  */

#define STRIP_SIGN_NOPS(EXP) \
  while ((TREE_CODE (EXP) == NOP_EXPR				\
	  || TREE_CODE (EXP) == CONVERT_EXPR			\
	  || TREE_CODE (EXP) == NON_LVALUE_EXPR)		\
	 && TREE_OPERAND (EXP, 0) != error_mark_node		\
	 && (TYPE_MODE (TREE_TYPE (EXP))			\
	     == TYPE_MODE (TREE_TYPE (TREE_OPERAND (EXP, 0))))	\
	 && (TYPE_UNSIGNED (TREE_TYPE (EXP))			\
	     == TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (EXP, 0))))) \
    (EXP) = TREE_OPERAND (EXP, 0)

/* Like STRIP_NOPS, but don't alter the TREE_TYPE either.  */

#define STRIP_TYPE_NOPS(EXP) \
  while ((TREE_CODE (EXP) == NOP_EXPR				\
	  || TREE_CODE (EXP) == CONVERT_EXPR			\
	  || TREE_CODE (EXP) == NON_LVALUE_EXPR)		\
	 && TREE_OPERAND (EXP, 0) != error_mark_node		\
	 && (TREE_TYPE (EXP)					\
	     == TREE_TYPE (TREE_OPERAND (EXP, 0))))		\
    (EXP) = TREE_OPERAND (EXP, 0)

/* Remove unnecessary type conversions according to
   tree_ssa_useless_type_conversion.  */

#define STRIP_USELESS_TYPE_CONVERSION(EXP)				\
      while (tree_ssa_useless_type_conversion (EXP))			\
	EXP = TREE_OPERAND (EXP, 0)

/* Nonzero if TYPE represents an integral type.  Note that we do not
   include COMPLEX types here.  Keep these checks in ascending code
   order.  */

#define INTEGRAL_TYPE_P(TYPE)  \
  (TREE_CODE (TYPE) == ENUMERAL_TYPE  \
   || TREE_CODE (TYPE) == BOOLEAN_TYPE \
   || TREE_CODE (TYPE) == INTEGER_TYPE)

/* Nonzero if TYPE represents a scalar floating-point type.  */

#define SCALAR_FLOAT_TYPE_P(TYPE) (TREE_CODE (TYPE) == REAL_TYPE)

/* Nonzero if TYPE represents a complex floating-point type.  */

#define COMPLEX_FLOAT_TYPE_P(TYPE)	\
  (TREE_CODE (TYPE) == COMPLEX_TYPE	\
   && TREE_CODE (TREE_TYPE (TYPE)) == REAL_TYPE)

/* Nonzero if TYPE represents a vector floating-point type.  */

#define VECTOR_FLOAT_TYPE_P(TYPE)	\
  (TREE_CODE (TYPE) == VECTOR_TYPE	\
   && TREE_CODE (TREE_TYPE (TYPE)) == REAL_TYPE)

/* Nonzero if TYPE represents a floating-point type, including complex
   and vector floating-point types.  The vector and complex check does
   not use the previous two macros to enable early folding.  */

#define FLOAT_TYPE_P(TYPE)			\
  (SCALAR_FLOAT_TYPE_P (TYPE)			\
   || ((TREE_CODE (TYPE) == COMPLEX_TYPE 	\
        || TREE_CODE (TYPE) == VECTOR_TYPE)	\
       && SCALAR_FLOAT_TYPE_P (TREE_TYPE (TYPE))))

/* Nonzero if TYPE represents a decimal floating-point type.  */
#define DECIMAL_FLOAT_TYPE_P(TYPE)		\
  (SCALAR_FLOAT_TYPE_P (TYPE)			\
   && DECIMAL_FLOAT_MODE_P (TYPE_MODE (TYPE)))

/* Nonzero if TYPE represents an aggregate (multi-component) type.
   Keep these checks in ascending code order.  */

#define AGGREGATE_TYPE_P(TYPE) \
  (TREE_CODE (TYPE) == ARRAY_TYPE || TREE_CODE (TYPE) == RECORD_TYPE \
   || TREE_CODE (TYPE) == UNION_TYPE || TREE_CODE (TYPE) == QUAL_UNION_TYPE)

/* Nonzero if TYPE represents a pointer or reference type.
   (It should be renamed to INDIRECT_TYPE_P.)  Keep these checks in
   ascending code order.  */

/* APPLE LOCAL begin blocks 5862465 */
#define POINTER_TYPE_P(TYPE) \
  (TREE_CODE (TYPE) == POINTER_TYPE \
   || TREE_CODE (TYPE) == REFERENCE_TYPE \
   || TREE_CODE (TYPE) == BLOCK_POINTER_TYPE)
/* APPLE LOCAL end blocks 5862465 */

/* Nonzero if this type is a complete type.  */
#define COMPLETE_TYPE_P(NODE) (TYPE_SIZE (NODE) != NULL_TREE)

/* Nonzero if this type is the (possibly qualified) void type.  */
#define VOID_TYPE_P(NODE) (TREE_CODE (NODE) == VOID_TYPE)

/* Nonzero if this type is complete or is cv void.  */
#define COMPLETE_OR_VOID_TYPE_P(NODE) \
  (COMPLETE_TYPE_P (NODE) || VOID_TYPE_P (NODE))

/* Nonzero if this type is complete or is an array with unspecified bound.  */
#define COMPLETE_OR_UNBOUND_ARRAY_TYPE_P(NODE) \
  (COMPLETE_TYPE_P (TREE_CODE (NODE) == ARRAY_TYPE ? TREE_TYPE (NODE) : (NODE)))


/* Define many boolean fields that all tree nodes have.  */

/* In VAR_DECL nodes, nonzero means address of this is needed.
   So it cannot be in a register.
   In a FUNCTION_DECL, nonzero means its address is needed.
   So it must be compiled even if it is an inline function.
   In a FIELD_DECL node, it means that the programmer is permitted to
   construct the address of this field.  This is used for aliasing
   purposes: see record_component_aliases.
   In CONSTRUCTOR nodes, it means object constructed must be in memory.
   In LABEL_DECL nodes, it means a goto for this label has been seen
   from a place outside all binding contours that restore stack levels.
   In ..._TYPE nodes, it means that objects of this type must
   be fully addressable.  This means that pieces of this
   object cannot go into register parameters, for example.
   In IDENTIFIER_NODEs, this means that some extern decl for this name
   had its address taken.  That matters for inline functions.  */
#define TREE_ADDRESSABLE(NODE) ((NODE)->common.addressable_flag)

/* Set on a CALL_EXPR if the call is in a tail position, ie. just before the
   exit of a function.  Calls for which this is true are candidates for tail
   call optimizations.  */
#define CALL_EXPR_TAILCALL(NODE) (CALL_EXPR_CHECK(NODE)->common.addressable_flag)

/* Used as a temporary field on a CASE_LABEL_EXPR to indicate that the
   CASE_LOW operand has been processed.  */
#define CASE_LOW_SEEN(NODE) \
  (CASE_LABEL_EXPR_CHECK (NODE)->common.addressable_flag)

/* In a VAR_DECL, nonzero means allocate static storage.
   In a FUNCTION_DECL, nonzero if function has been defined.
   In a CONSTRUCTOR, nonzero means allocate static storage.

   ??? This is also used in lots of other nodes in unclear ways which
   should be cleaned up some day.  */
#define TREE_STATIC(NODE) ((NODE)->common.static_flag)

/* In a TARGET_EXPR, WITH_CLEANUP_EXPR, means that the pertinent cleanup
   should only be executed if an exception is thrown, not on normal exit
   of its scope.  */
#define CLEANUP_EH_ONLY(NODE) ((NODE)->common.static_flag)

/* Used as a temporary field on a CASE_LABEL_EXPR to indicate that the
   CASE_HIGH operand has been processed.  */
#define CASE_HIGH_SEEN(NODE) \
  (CASE_LABEL_EXPR_CHECK (NODE)->common.static_flag)

/* In an expr node (usually a conversion) this means the node was made
   implicitly and should not lead to any sort of warning.  In a decl node,
   warnings concerning the decl should be suppressed.  This is used at
   least for used-before-set warnings, and it set after one warning is
   emitted.  */
#define TREE_NO_WARNING(NODE) ((NODE)->common.nowarning_flag)

/* In an INTEGER_CST, REAL_CST, COMPLEX_CST, or VECTOR_CST this means
   there was an overflow in folding.  This is distinct from
   TREE_OVERFLOW because ANSI C requires a diagnostic when overflows
   occur in constant expressions.  */
#define TREE_CONSTANT_OVERFLOW(NODE) (CST_CHECK (NODE)->common.static_flag)

/* In an IDENTIFIER_NODE, this means that assemble_name was called with
   this string as an argument.  */
#define TREE_SYMBOL_REFERENCED(NODE) \
  (IDENTIFIER_NODE_CHECK (NODE)->common.static_flag)

/* Nonzero in a pointer or reference type means the data pointed to
   by this type can alias anything.  */
#define TYPE_REF_CAN_ALIAS_ALL(NODE) \
  (PTR_OR_REF_CHECK (NODE)->common.static_flag)

/* In an INTEGER_CST, REAL_CST, COMPLEX_CST, or VECTOR_CST, this means
   there was an overflow in folding, and no warning has been issued
   for this subexpression.  TREE_OVERFLOW implies TREE_CONSTANT_OVERFLOW,
   but not vice versa.  */

#define TREE_OVERFLOW(NODE) (CST_CHECK (NODE)->common.public_flag)

/* TREE_OVERFLOW can only be true for EXPR of CONSTANT_CLASS_P. */

#define TREE_OVERFLOW_P(EXPR) \
  (CONSTANT_CLASS_P (EXPR) && TREE_OVERFLOW (EXPR))

/* In a VAR_DECL, FUNCTION_DECL, NAMESPACE_DECL or TYPE_DECL,
   nonzero means name is to be accessible from outside this module.
   In an IDENTIFIER_NODE, nonzero means an external declaration
   accessible from outside this module was previously seen
   for this name in an inner scope.  */
#define TREE_PUBLIC(NODE) ((NODE)->common.public_flag)

/* In a _TYPE, indicates whether TYPE_CACHED_VALUES contains a vector
   of cached values, or is something else.  */
#define TYPE_CACHED_VALUES_P(NODE) (TYPE_CHECK(NODE)->common.public_flag)

/* In a SAVE_EXPR, indicates that the original expression has already
   been substituted with a VAR_DECL that contains the value.  */
#define SAVE_EXPR_RESOLVED_P(NODE) \
  (TREE_CHECK (NODE, SAVE_EXPR)->common.public_flag)

/* In any expression, decl, or constant, nonzero means it has side effects or
   reevaluation of the whole expression could produce a different value.
   This is set if any subexpression is a function call, a side effect or a
   reference to a volatile variable.  In a ..._DECL, this is set only if the
   declaration said `volatile'.  This will never be set for a constant.  */
#define TREE_SIDE_EFFECTS(NODE) \
  (NON_TYPE_CHECK (NODE)->common.side_effects_flag)

/* In a LABEL_DECL, nonzero means this label had its address taken
   and therefore can never be deleted and is a jump target for
   computed gotos.  */
#define FORCED_LABEL(NODE) ((NODE)->common.side_effects_flag)

/* Nonzero means this expression is volatile in the C sense:
   its address should be of type `volatile WHATEVER *'.
   In other words, the declared item is volatile qualified.
   This is used in _DECL nodes and _REF nodes.
   On a FUNCTION_DECL node, this means the function does not
   return normally.  This is the same effect as setting
   the attribute noreturn on the function in C.

   In a ..._TYPE node, means this type is volatile-qualified.
   But use TYPE_VOLATILE instead of this macro when the node is a type,
   because eventually we may make that a different bit.

   If this bit is set in an expression, so is TREE_SIDE_EFFECTS.  */
#define TREE_THIS_VOLATILE(NODE) ((NODE)->common.volatile_flag)

/* Nonzero means this node will not trap.  In an INDIRECT_REF, means
   accessing the memory pointed to won't generate a trap.  However,
   this only applies to an object when used appropriately: it doesn't
   mean that writing a READONLY mem won't trap. Similarly for
   ALIGN_INDIRECT_REF and MISALIGNED_INDIRECT_REF.

   In ARRAY_REF and ARRAY_RANGE_REF means that we know that the index
   (or slice of the array) always belongs to the range of the array.
   I.e. that the access will not trap, provided that the access to
   the base to the array will not trap.  */
#define TREE_THIS_NOTRAP(NODE) ((NODE)->common.nothrow_flag)

/* In a VAR_DECL, PARM_DECL or FIELD_DECL, or any kind of ..._REF node,
   nonzero means it may not be the lhs of an assignment.  */
#define TREE_READONLY(NODE) (NON_TYPE_CHECK (NODE)->common.readonly_flag)

/* Nonzero if NODE is a _DECL with TREE_READONLY set.  */
#define TREE_READONLY_DECL_P(NODE)\
	(DECL_P (NODE) && TREE_READONLY (NODE))

/* Value of expression is constant.  Always on in all ..._CST nodes.  May
   also appear in an expression or decl where the value is constant.  */
#define TREE_CONSTANT(NODE) (NON_TYPE_CHECK (NODE)->common.constant_flag)

/* Nonzero if NODE, a type, has had its sizes gimplified.  */
#define TYPE_SIZES_GIMPLIFIED(NODE) (TYPE_CHECK (NODE)->common.constant_flag)

/* In a decl (most significantly a FIELD_DECL), means an unsigned field.  */
#define DECL_UNSIGNED(NODE) (DECL_COMMON_CHECK (NODE)->common.unsigned_flag)

/* In a BIT_FIELD_REF, means the bitfield is to be interpreted as unsigned.  */
#define BIT_FIELD_REF_UNSIGNED(NODE) \
  (BIT_FIELD_REF_CHECK (NODE)->common.unsigned_flag)

/* In integral and pointer types, means an unsigned type.  */
#define TYPE_UNSIGNED(NODE) (TYPE_CHECK (NODE)->common.unsigned_flag)

/* Nonzero in a VAR_DECL means assembler code has been written.
   Nonzero in a FUNCTION_DECL means that the function has been compiled.
   This is interesting in an inline function, since it might not need
   to be compiled separately.
   Nonzero in a RECORD_TYPE, UNION_TYPE, QUAL_UNION_TYPE or ENUMERAL_TYPE
   if the sdb debugging info for the type has been written.
   In a BLOCK node, nonzero if reorder_blocks has already seen this block.
   In an SSA_NAME node, nonzero if the SSA_NAME occurs in an abnormal
   PHI node.  */
#define TREE_ASM_WRITTEN(NODE) ((NODE)->common.asm_written_flag)

/* Nonzero in a _DECL if the name is used in its scope.
   Nonzero in an expr node means inhibit warning if value is unused.
   In IDENTIFIER_NODEs, this means that some extern decl for this name
   was used.
   In a BLOCK, this means that the block contains variables that are used.  */
#define TREE_USED(NODE) ((NODE)->common.used_flag)

/* In a FUNCTION_DECL, nonzero means a call to the function cannot throw
   an exception.  In a CALL_EXPR, nonzero means the call cannot throw.  */
#define TREE_NOTHROW(NODE) ((NODE)->common.nothrow_flag)

/* In a CALL_EXPR, means that it's safe to use the target of the call
   expansion as the return slot for a call that returns in memory.  */
#define CALL_EXPR_RETURN_SLOT_OPT(NODE) ((NODE)->common.private_flag)

/* In a RESULT_DECL or PARM_DECL, means that it is passed by invisible
   reference (and the TREE_TYPE is a pointer to the true type).  */
#define DECL_BY_REFERENCE(NODE) (DECL_COMMON_CHECK (NODE)->common.private_flag)

/* In a CALL_EXPR, means that the call is the jump from a thunk to the
   thunked-to function.  */
#define CALL_FROM_THUNK_P(NODE) (CALL_EXPR_CHECK (NODE)->common.protected_flag)

/* In a type, nonzero means that all objects of the type are guaranteed by the
   language or front-end to be properly aligned, so we can indicate that a MEM
   of this type is aligned at least to the alignment of the type, even if it
   doesn't appear that it is.  We see this, for example, in object-oriented
   languages where a tag field may show this is an object of a more-aligned
   variant of the more generic type.

   In an SSA_NAME node, nonzero if the SSA_NAME node is on the SSA_NAME
   freelist.  */
#define TYPE_ALIGN_OK(NODE) (TYPE_CHECK (NODE)->common.nothrow_flag)

/* Used in classes in C++.  */
#define TREE_PRIVATE(NODE) ((NODE)->common.private_flag)
/* Used in classes in C++.
   In a BLOCK node, this is BLOCK_HANDLER_BLOCK.  */
#define TREE_PROTECTED(NODE) ((NODE)->common.protected_flag)

/* Nonzero in a _DECL if the use of the name is defined as a
   deprecated feature by __attribute__((deprecated)).  */
#define TREE_DEPRECATED(NODE) \
  ((NODE)->common.deprecated_flag)

/* APPLE LOCAL begin "unavailable" attribute (Radar 2809697) */
/* Nonzero in a IDENTIFIER_NODE if the use of the name is defined as a
   unavailable feature by __attribute__((unavailable)).  */
#define TREE_UNAVAILABLE(NODE) ((NODE)->common.unavailable_flag)
/* APPLE LOCAL end "unavailable" attribute (Radar 2809697) */

/* Nonzero in an IDENTIFIER_NODE if the name is a local alias, whose
   uses are to be substituted for uses of the TREE_CHAINed identifier.  */
#define IDENTIFIER_TRANSPARENT_ALIAS(NODE) \
  (IDENTIFIER_NODE_CHECK (NODE)->common.deprecated_flag)

/* Value of expression is function invariant.  A strict subset of
   TREE_CONSTANT, such an expression is constant over any one function
   invocation, though not across different invocations.  May appear in
   any expression node.  */
#define TREE_INVARIANT(NODE) ((NODE)->common.invariant_flag)

/* These flags are available for each language front end to use internally.  */
#define TREE_LANG_FLAG_0(NODE) ((NODE)->common.lang_flag_0)
#define TREE_LANG_FLAG_1(NODE) ((NODE)->common.lang_flag_1)
#define TREE_LANG_FLAG_2(NODE) ((NODE)->common.lang_flag_2)
#define TREE_LANG_FLAG_3(NODE) ((NODE)->common.lang_flag_3)
#define TREE_LANG_FLAG_4(NODE) ((NODE)->common.lang_flag_4)
#define TREE_LANG_FLAG_5(NODE) ((NODE)->common.lang_flag_5)
#define TREE_LANG_FLAG_6(NODE) ((NODE)->common.lang_flag_6)

/* Define additional fields and accessors for nodes representing constants.  */

/* In an INTEGER_CST node.  These two together make a 2-word integer.
   If the data type is signed, the value is sign-extended to 2 words
   even though not all of them may really be in use.
   In an unsigned constant shorter than 2 words, the extra bits are 0.  */
#define TREE_INT_CST(NODE) (INTEGER_CST_CHECK (NODE)->int_cst.int_cst)
#define TREE_INT_CST_LOW(NODE) (TREE_INT_CST (NODE).low)
#define TREE_INT_CST_HIGH(NODE) (TREE_INT_CST (NODE).high)

#define INT_CST_LT(A, B)				\
  (TREE_INT_CST_HIGH (A) < TREE_INT_CST_HIGH (B)	\
   || (TREE_INT_CST_HIGH (A) == TREE_INT_CST_HIGH (B)	\
       && TREE_INT_CST_LOW (A) < TREE_INT_CST_LOW (B)))

#define INT_CST_LT_UNSIGNED(A, B)				\
  (((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (A)		\
    < (unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (B))		\
   || (((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (A)		\
	== (unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (B))	\
       && TREE_INT_CST_LOW (A) < TREE_INT_CST_LOW (B)))

struct tree_int_cst GTY(())
{
  struct tree_common common;
  double_int int_cst;
};

/* In a REAL_CST node.  struct real_value is an opaque entity, with
   manipulators defined in real.h.  We don't want tree.h depending on
   real.h and transitively on tm.h.  */
struct real_value;

#define TREE_REAL_CST_PTR(NODE) (REAL_CST_CHECK (NODE)->real_cst.real_cst_ptr)
#define TREE_REAL_CST(NODE) (*TREE_REAL_CST_PTR (NODE))

struct tree_real_cst GTY(())
{
  struct tree_common common;
  struct real_value * real_cst_ptr;
};

/* In a STRING_CST */
#define TREE_STRING_LENGTH(NODE) (STRING_CST_CHECK (NODE)->string.length)
#define TREE_STRING_POINTER(NODE) \
  ((const char *)(STRING_CST_CHECK (NODE)->string.str))

struct tree_string GTY(())
{
  struct tree_common common;
  int length;
  char str[1];
};

/* In a COMPLEX_CST node.  */
#define TREE_REALPART(NODE) (COMPLEX_CST_CHECK (NODE)->complex.real)
#define TREE_IMAGPART(NODE) (COMPLEX_CST_CHECK (NODE)->complex.imag)

struct tree_complex GTY(())
{
  struct tree_common common;
  tree real;
  tree imag;
};

/* In a VECTOR_CST node.  */
#define TREE_VECTOR_CST_ELTS(NODE) (VECTOR_CST_CHECK (NODE)->vector.elements)

struct tree_vector GTY(())
{
  struct tree_common common;
  tree elements;
};

#include "symtab.h"

/* Define fields and accessors for some special-purpose tree nodes.  */

#define IDENTIFIER_LENGTH(NODE) \
  (IDENTIFIER_NODE_CHECK (NODE)->identifier.id.len)
#define IDENTIFIER_POINTER(NODE) \
  ((const char *) IDENTIFIER_NODE_CHECK (NODE)->identifier.id.str)
#define IDENTIFIER_HASH_VALUE(NODE) \
  (IDENTIFIER_NODE_CHECK (NODE)->identifier.id.hash_value)

/* Translate a hash table identifier pointer to a tree_identifier
   pointer, and vice versa.  */

#define HT_IDENT_TO_GCC_IDENT(NODE) \
  ((tree) ((char *) (NODE) - sizeof (struct tree_common)))
#define GCC_IDENT_TO_HT_IDENT(NODE) (&((struct tree_identifier *) (NODE))->id)

struct tree_identifier GTY(())
{
  struct tree_common common;
  struct ht_identifier id;
};

/* In a TREE_LIST node.  */
#define TREE_PURPOSE(NODE) (TREE_LIST_CHECK (NODE)->list.purpose)
#define TREE_VALUE(NODE) (TREE_LIST_CHECK (NODE)->list.value)

struct tree_list GTY(())
{
  struct tree_common common;
  tree purpose;
  tree value;
};

/* In a TREE_VEC node.  */
#define TREE_VEC_LENGTH(NODE) (TREE_VEC_CHECK (NODE)->vec.length)
#define TREE_VEC_END(NODE) \
  ((void) TREE_VEC_CHECK (NODE), &((NODE)->vec.a[(NODE)->vec.length]))

#define TREE_VEC_ELT(NODE,I) TREE_VEC_ELT_CHECK (NODE, I)

struct tree_vec GTY(())
{
  struct tree_common common;
  int length;
  tree GTY ((length ("TREE_VEC_LENGTH ((tree)&%h)"))) a[1];
};

/* In a CONSTRUCTOR node.  */
#define CONSTRUCTOR_ELTS(NODE) (CONSTRUCTOR_CHECK (NODE)->constructor.elts)

/* Iterate through the vector V of CONSTRUCTOR_ELT elements, yielding the
   value of each element (stored within VAL). IX must be a scratch variable
   of unsigned integer type.  */
#define FOR_EACH_CONSTRUCTOR_VALUE(V, IX, VAL) \
  for (IX = 0; (IX >= VEC_length (constructor_elt, V)) \
	       ? false \
	       : ((VAL = VEC_index (constructor_elt, V, IX)->value), \
	       true); \
       (IX)++)

/* Iterate through the vector V of CONSTRUCTOR_ELT elements, yielding both
   the value of each element (stored within VAL) and its index (stored
   within INDEX). IX must be a scratch variable of unsigned integer type.  */
#define FOR_EACH_CONSTRUCTOR_ELT(V, IX, INDEX, VAL) \
  for (IX = 0; (IX >= VEC_length (constructor_elt, V)) \
	       ? false \
	       : ((VAL = VEC_index (constructor_elt, V, IX)->value), \
		  (INDEX = VEC_index (constructor_elt, V, IX)->index), \
	       true); \
       (IX)++)

/* Append a new constructor element to V, with the specified INDEX and VAL.  */
#define CONSTRUCTOR_APPEND_ELT(V, INDEX, VALUE) \
  do { \
    constructor_elt *_ce___ = VEC_safe_push (constructor_elt, gc, V, NULL); \
    _ce___->index = INDEX; \
    _ce___->value = VALUE; \
  } while (0)

/* A single element of a CONSTRUCTOR. VALUE holds the actual value of the
   element. INDEX can optionally design the position of VALUE: in arrays,
   it is the index where VALUE has to be placed; in structures, it is the
   FIELD_DECL of the member.  */
typedef struct constructor_elt_d GTY(())
{
  tree index;
  tree value;
} constructor_elt;

DEF_VEC_O(constructor_elt);
DEF_VEC_ALLOC_O(constructor_elt,gc);

struct tree_constructor GTY(())
{
  struct tree_common common;
  VEC(constructor_elt,gc) *elts;
};

/* Define fields and accessors for some nodes that represent expressions.  */

/* Nonzero if NODE is an empty statement (NOP_EXPR <0>).  */
#define IS_EMPTY_STMT(NODE)	(TREE_CODE (NODE) == NOP_EXPR \
				 && VOID_TYPE_P (TREE_TYPE (NODE)) \
				 && integer_zerop (TREE_OPERAND (NODE, 0)))

/* In ordinary expression nodes.  */
#define TREE_OPERAND(NODE, I) TREE_OPERAND_CHECK (NODE, I)
#define TREE_COMPLEXITY(NODE) (EXPR_CHECK (NODE)->exp.complexity)

/* In a LOOP_EXPR node.  */
#define LOOP_EXPR_BODY(NODE) TREE_OPERAND_CHECK_CODE (NODE, LOOP_EXPR, 0)

#ifdef USE_MAPPED_LOCATION
/* The source location of this expression.  Non-tree_exp nodes such as
   decls and constants can be shared among multiple locations, so
   return nothing.  */
#define EXPR_LOCATION(NODE)					\
  (EXPR_P (NODE) ? (NODE)->exp.locus : UNKNOWN_LOCATION)
#define SET_EXPR_LOCATION(NODE, FROM) \
  (EXPR_CHECK (NODE)->exp.locus = (FROM))
#define EXPR_HAS_LOCATION(NODE) (EXPR_LOCATION (NODE) != UNKNOWN_LOCATION)
/* EXPR_LOCUS and SET_EXPR_LOCUS are deprecated.  */
#define EXPR_LOCUS(NODE)					\
  (EXPR_P (NODE) ? &(NODE)->exp.locus : (location_t *)NULL)
#define SET_EXPR_LOCUS(NODE, FROM) \
  do { source_location *loc_tmp = FROM; \
       EXPR_CHECK (NODE)->exp.locus \
       = loc_tmp == NULL ? UNKNOWN_LOCATION : *loc_tmp; } while (0)
#define EXPR_FILENAME(NODE) \
  LOCATION_FILE (EXPR_CHECK (NODE)->exp.locus)
#define EXPR_LINENO(NODE) \
  LOCATION_LINE (EXPR_CHECK (NODE)->exp.locus)
#else
/* The source location of this expression.  Non-tree_exp nodes such as
   decls and constants can be shared among multiple locations, so
   return nothing.  */
#define EXPR_LOCUS(NODE)					\
  (EXPR_P (NODE) ? (NODE)->exp.locus : (location_t *)NULL)
#define SET_EXPR_LOCUS(NODE, FROM) \
  (EXPR_CHECK (NODE)->exp.locus = (FROM))
#define SET_EXPR_LOCATION(NODE, FROM) annotate_with_locus (NODE, FROM)
#define EXPR_FILENAME(NODE) \
  (EXPR_CHECK (NODE)->exp.locus->file)
#define EXPR_LINENO(NODE) \
  (EXPR_CHECK (NODE)->exp.locus->line)
#define EXPR_HAS_LOCATION(NODE) (EXPR_LOCUS (NODE) != NULL)
#define EXPR_LOCATION(NODE) \
  (EXPR_HAS_LOCATION(NODE) ? *(NODE)->exp.locus : UNKNOWN_LOCATION)
#endif

/* In a TARGET_EXPR node.  */
#define TARGET_EXPR_SLOT(NODE) TREE_OPERAND_CHECK_CODE (NODE, TARGET_EXPR, 0)
#define TARGET_EXPR_INITIAL(NODE) TREE_OPERAND_CHECK_CODE (NODE, TARGET_EXPR, 1)
#define TARGET_EXPR_CLEANUP(NODE) TREE_OPERAND_CHECK_CODE (NODE, TARGET_EXPR, 2)

/* DECL_EXPR accessor. This gives access to the DECL associated with
   the given declaration statement.  */
#define DECL_EXPR_DECL(NODE)    TREE_OPERAND (DECL_EXPR_CHECK (NODE), 0)

#define EXIT_EXPR_COND(NODE)	     TREE_OPERAND (EXIT_EXPR_CHECK (NODE), 0)

/* SWITCH_EXPR accessors. These give access to the condition, body and
   original condition type (before any compiler conversions)
   of the switch statement, respectively.  */
#define SWITCH_COND(NODE)       TREE_OPERAND (SWITCH_EXPR_CHECK (NODE), 0)
#define SWITCH_BODY(NODE)       TREE_OPERAND (SWITCH_EXPR_CHECK (NODE), 1)
#define SWITCH_LABELS(NODE)     TREE_OPERAND (SWITCH_EXPR_CHECK (NODE), 2)

/* CASE_LABEL_EXPR accessors. These give access to the high and low values
   of a case label, respectively.  */
#define CASE_LOW(NODE)          	TREE_OPERAND (CASE_LABEL_EXPR_CHECK (NODE), 0)
#define CASE_HIGH(NODE)         	TREE_OPERAND (CASE_LABEL_EXPR_CHECK (NODE), 1)
#define CASE_LABEL(NODE)		TREE_OPERAND (CASE_LABEL_EXPR_CHECK (NODE), 2)

/* The operands of a TARGET_MEM_REF.  */
#define TMR_SYMBOL(NODE) (TREE_OPERAND (TARGET_MEM_REF_CHECK (NODE), 0))
#define TMR_BASE(NODE) (TREE_OPERAND (TARGET_MEM_REF_CHECK (NODE), 1))
#define TMR_INDEX(NODE) (TREE_OPERAND (TARGET_MEM_REF_CHECK (NODE), 2))
#define TMR_STEP(NODE) (TREE_OPERAND (TARGET_MEM_REF_CHECK (NODE), 3))
#define TMR_OFFSET(NODE) (TREE_OPERAND (TARGET_MEM_REF_CHECK (NODE), 4))
#define TMR_ORIGINAL(NODE) (TREE_OPERAND (TARGET_MEM_REF_CHECK (NODE), 5))
#define TMR_TAG(NODE) (TREE_OPERAND (TARGET_MEM_REF_CHECK (NODE), 6))

/* The operands of a BIND_EXPR.  */
#define BIND_EXPR_VARS(NODE) (TREE_OPERAND (BIND_EXPR_CHECK (NODE), 0))
#define BIND_EXPR_BODY(NODE) (TREE_OPERAND (BIND_EXPR_CHECK (NODE), 1))
#define BIND_EXPR_BLOCK(NODE) (TREE_OPERAND (BIND_EXPR_CHECK (NODE), 2))

/* GOTO_EXPR accessor. This gives access to the label associated with
   a goto statement.  */
#define GOTO_DESTINATION(NODE)  TREE_OPERAND ((NODE), 0)

/* ASM_EXPR accessors. ASM_STRING returns a STRING_CST for the
   instruction (e.g., "mov x, y"). ASM_OUTPUTS, ASM_INPUTS, and
   ASM_CLOBBERS represent the outputs, inputs, and clobbers for the
   statement.  */
#define ASM_STRING(NODE)        TREE_OPERAND (ASM_EXPR_CHECK (NODE), 0)
#define ASM_OUTPUTS(NODE)       TREE_OPERAND (ASM_EXPR_CHECK (NODE), 1)
#define ASM_INPUTS(NODE)        TREE_OPERAND (ASM_EXPR_CHECK (NODE), 2)
#define ASM_CLOBBERS(NODE)      TREE_OPERAND (ASM_EXPR_CHECK (NODE), 3)
/* Nonzero if we want to create an ASM_INPUT instead of an
   ASM_OPERAND with no operands.  */
#define ASM_INPUT_P(NODE) (TREE_STATIC (NODE))
#define ASM_VOLATILE_P(NODE) (TREE_PUBLIC (NODE))

/* COND_EXPR accessors.  */
#define COND_EXPR_COND(NODE)	(TREE_OPERAND (COND_EXPR_CHECK (NODE), 0))
#define COND_EXPR_THEN(NODE)	(TREE_OPERAND (COND_EXPR_CHECK (NODE), 1))
#define COND_EXPR_ELSE(NODE)	(TREE_OPERAND (COND_EXPR_CHECK (NODE), 2))

/* LABEL_EXPR accessor. This gives access to the label associated with
   the given label expression.  */
#define LABEL_EXPR_LABEL(NODE)  TREE_OPERAND (LABEL_EXPR_CHECK (NODE), 0)

/* VDEF_EXPR accessors are specified in tree-flow.h, along with the other
   accessors for SSA operands.  */

/* CATCH_EXPR accessors.  */
#define CATCH_TYPES(NODE)	TREE_OPERAND (CATCH_EXPR_CHECK (NODE), 0)
#define CATCH_BODY(NODE)	TREE_OPERAND (CATCH_EXPR_CHECK (NODE), 1)

/* EH_FILTER_EXPR accessors.  */
#define EH_FILTER_TYPES(NODE)	TREE_OPERAND (EH_FILTER_EXPR_CHECK (NODE), 0)
#define EH_FILTER_FAILURE(NODE)	TREE_OPERAND (EH_FILTER_EXPR_CHECK (NODE), 1)
#define EH_FILTER_MUST_NOT_THROW(NODE) TREE_STATIC (EH_FILTER_EXPR_CHECK (NODE))

/* OBJ_TYPE_REF accessors.  */
#define OBJ_TYPE_REF_EXPR(NODE)	  TREE_OPERAND (OBJ_TYPE_REF_CHECK (NODE), 0)
#define OBJ_TYPE_REF_OBJECT(NODE) TREE_OPERAND (OBJ_TYPE_REF_CHECK (NODE), 1)
#define OBJ_TYPE_REF_TOKEN(NODE)  TREE_OPERAND (OBJ_TYPE_REF_CHECK (NODE), 2)

/* ASSERT_EXPR accessors.  */
#define ASSERT_EXPR_VAR(NODE)	TREE_OPERAND (ASSERT_EXPR_CHECK (NODE), 0)
#define ASSERT_EXPR_COND(NODE)	TREE_OPERAND (ASSERT_EXPR_CHECK (NODE), 1)

/* OpenMP directive and clause accessors.  */

#define OMP_BODY(NODE) \
  TREE_OPERAND (TREE_RANGE_CHECK (NODE, OMP_PARALLEL, OMP_CRITICAL), 0)
#define OMP_CLAUSES(NODE) \
  TREE_OPERAND (TREE_RANGE_CHECK (NODE, OMP_PARALLEL, OMP_SINGLE), 1)

#define OMP_PARALLEL_BODY(NODE)    TREE_OPERAND (OMP_PARALLEL_CHECK (NODE), 0)
#define OMP_PARALLEL_CLAUSES(NODE) TREE_OPERAND (OMP_PARALLEL_CHECK (NODE), 1)
#define OMP_PARALLEL_FN(NODE) TREE_OPERAND (OMP_PARALLEL_CHECK (NODE), 2)
#define OMP_PARALLEL_DATA_ARG(NODE) TREE_OPERAND (OMP_PARALLEL_CHECK (NODE), 3)

#define OMP_FOR_BODY(NODE)	   TREE_OPERAND (OMP_FOR_CHECK (NODE), 0)
#define OMP_FOR_CLAUSES(NODE)	   TREE_OPERAND (OMP_FOR_CHECK (NODE), 1)
#define OMP_FOR_INIT(NODE)	   TREE_OPERAND (OMP_FOR_CHECK (NODE), 2)
#define OMP_FOR_COND(NODE)	   TREE_OPERAND (OMP_FOR_CHECK (NODE), 3)
#define OMP_FOR_INCR(NODE)	   TREE_OPERAND (OMP_FOR_CHECK (NODE), 4)
#define OMP_FOR_PRE_BODY(NODE)	   TREE_OPERAND (OMP_FOR_CHECK (NODE), 5)

#define OMP_SECTIONS_BODY(NODE)    TREE_OPERAND (OMP_SECTIONS_CHECK (NODE), 0)
#define OMP_SECTIONS_CLAUSES(NODE) TREE_OPERAND (OMP_SECTIONS_CHECK (NODE), 1)

#define OMP_SECTION_BODY(NODE)	   TREE_OPERAND (OMP_SECTION_CHECK (NODE), 0)

#define OMP_SINGLE_BODY(NODE)	   TREE_OPERAND (OMP_SINGLE_CHECK (NODE), 0)
#define OMP_SINGLE_CLAUSES(NODE)   TREE_OPERAND (OMP_SINGLE_CHECK (NODE), 1)

#define OMP_MASTER_BODY(NODE)	   TREE_OPERAND (OMP_MASTER_CHECK (NODE), 0)

#define OMP_ORDERED_BODY(NODE)	   TREE_OPERAND (OMP_ORDERED_CHECK (NODE), 0)

#define OMP_CRITICAL_BODY(NODE)    TREE_OPERAND (OMP_CRITICAL_CHECK (NODE), 0)
#define OMP_CRITICAL_NAME(NODE)    TREE_OPERAND (OMP_CRITICAL_CHECK (NODE), 1)

#define OMP_CLAUSE_CHAIN(NODE)     TREE_CHAIN (OMP_CLAUSE_CHECK (NODE))
#define OMP_CLAUSE_DECL(NODE)      					\
  OMP_CLAUSE_OPERAND (OMP_CLAUSE_RANGE_CHECK (OMP_CLAUSE_CHECK (NODE),	\
					      OMP_CLAUSE_PRIVATE,	\
	                                      OMP_CLAUSE_COPYPRIVATE), 0)

/* True on an OMP_SECTION statement that was the last lexical member.
   This status is meaningful in the implementation of lastprivate.  */
#define OMP_SECTION_LAST(NODE) \
  TREE_PRIVATE (OMP_SECTION_CHECK (NODE))

/* True on an OMP_RETURN statement if the return does not require a
   thread synchronization via some sort of barrier.  The exact barrier
   that would otherwise be emitted is dependent on the OMP statement
   with which this return is associated.  */
#define OMP_RETURN_NOWAIT(NODE) \
  TREE_PRIVATE (OMP_RETURN_CHECK (NODE))

/* True on an OMP_PARALLEL statement if it represents an explicit
   combined parallel work-sharing constructs.  */
#define OMP_PARALLEL_COMBINED(NODE) \
  TREE_PRIVATE (OMP_PARALLEL_CHECK (NODE))

/* True on a PRIVATE clause if its decl is kept around for debugging
   information only and its DECL_VALUE_EXPR is supposed to point
   to what it has been remapped to.  */
#define OMP_CLAUSE_PRIVATE_DEBUG(NODE) \
  TREE_PUBLIC (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_PRIVATE))

/* True on a LASTPRIVATE clause if a FIRSTPRIVATE clause for the same
   decl is present in the chain.  */
#define OMP_CLAUSE_LASTPRIVATE_FIRSTPRIVATE(NODE) \
  TREE_PUBLIC (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_LASTPRIVATE))

#define OMP_CLAUSE_IF_EXPR(NODE) \
  OMP_CLAUSE_OPERAND (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_IF), 0)
#define OMP_CLAUSE_NUM_THREADS_EXPR(NODE) \
  OMP_CLAUSE_OPERAND (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_NUM_THREADS),0)
#define OMP_CLAUSE_SCHEDULE_CHUNK_EXPR(NODE) \
  OMP_CLAUSE_OPERAND (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_SCHEDULE), 0)

#define OMP_CLAUSE_REDUCTION_CODE(NODE)	\
  (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_REDUCTION)->omp_clause.subcode.reduction_code)
#define OMP_CLAUSE_REDUCTION_INIT(NODE) \
  OMP_CLAUSE_OPERAND (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_REDUCTION), 1)
#define OMP_CLAUSE_REDUCTION_MERGE(NODE) \
  OMP_CLAUSE_OPERAND (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_REDUCTION), 2)
#define OMP_CLAUSE_REDUCTION_PLACEHOLDER(NODE) \
  OMP_CLAUSE_OPERAND (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_REDUCTION), 3)

enum omp_clause_schedule_kind
{
  OMP_CLAUSE_SCHEDULE_STATIC,
  OMP_CLAUSE_SCHEDULE_DYNAMIC,
  OMP_CLAUSE_SCHEDULE_GUIDED,
  OMP_CLAUSE_SCHEDULE_RUNTIME
};

#define OMP_CLAUSE_SCHEDULE_KIND(NODE) \
  (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_SCHEDULE)->omp_clause.subcode.schedule_kind)

enum omp_clause_default_kind
{
  OMP_CLAUSE_DEFAULT_UNSPECIFIED,
  OMP_CLAUSE_DEFAULT_SHARED,
  OMP_CLAUSE_DEFAULT_NONE,
  OMP_CLAUSE_DEFAULT_PRIVATE
};

#define OMP_CLAUSE_DEFAULT_KIND(NODE) \
  (OMP_CLAUSE_SUBCODE_CHECK (NODE, OMP_CLAUSE_DEFAULT)->omp_clause.subcode.default_kind)

struct tree_exp GTY(())
{
  struct tree_common common;
  source_locus locus;
  int complexity;
  tree block;
  tree GTY ((special ("tree_exp"),
	     desc ("TREE_CODE ((tree) &%0)")))
    operands[1];
};

/* SSA_NAME accessors.  */

/* Returns the variable being referenced.  Once released, this is the
   only field that can be relied upon.  */
#define SSA_NAME_VAR(NODE)	SSA_NAME_CHECK (NODE)->ssa_name.var

/* Returns the statement which defines this reference.   Note that
   we use the same field when chaining SSA_NAME nodes together on
   the SSA_NAME freelist.  */
#define SSA_NAME_DEF_STMT(NODE)	SSA_NAME_CHECK (NODE)->common.chain

/* Returns the SSA version number of this SSA name.  Note that in
   tree SSA, version numbers are not per variable and may be recycled.  */
#define SSA_NAME_VERSION(NODE)	SSA_NAME_CHECK (NODE)->ssa_name.version

/* Nonzero if this SSA name occurs in an abnormal PHI.  SSA_NAMES are
   never output, so we can safely use the ASM_WRITTEN_FLAG for this
   status bit.  */
#define SSA_NAME_OCCURS_IN_ABNORMAL_PHI(NODE) \
    SSA_NAME_CHECK (NODE)->common.asm_written_flag

/* Nonzero if this SSA_NAME expression is currently on the free list of
   SSA_NAMES.  Using NOTHROW_FLAG seems reasonably safe since throwing
   has no meaning for an SSA_NAME.  */
#define SSA_NAME_IN_FREE_LIST(NODE) \
    SSA_NAME_CHECK (NODE)->common.nothrow_flag

/* Attributes for SSA_NAMEs for pointer-type variables.  */
#define SSA_NAME_PTR_INFO(N) \
    SSA_NAME_CHECK (N)->ssa_name.ptr_info

/* Get the value of this SSA_NAME, if available.  */
#define SSA_NAME_VALUE(N) \
   SSA_NAME_CHECK (N)->ssa_name.value_handle

#ifndef _TREE_FLOW_H
struct ptr_info_def;
#endif



/* Immediate use linking structure.  This structure is used for maintaining
   a doubly linked list of uses of an SSA_NAME.  */
typedef struct ssa_use_operand_d GTY(())
{
  struct ssa_use_operand_d* GTY((skip(""))) prev;
  struct ssa_use_operand_d* GTY((skip(""))) next;
  tree GTY((skip(""))) stmt;
  tree *GTY((skip(""))) use;
} ssa_use_operand_t;

/* Return the immediate_use information for an SSA_NAME. */
#define SSA_NAME_IMM_USE_NODE(NODE) SSA_NAME_CHECK (NODE)->ssa_name.imm_uses

struct tree_ssa_name GTY(())
{
  struct tree_common common;

  /* _DECL wrapped by this SSA name.  */
  tree var;

  /* SSA version number.  */
  unsigned int version;

  /* Pointer attributes used for alias analysis.  */
  struct ptr_info_def *ptr_info;

  /* Value for SSA name used by various passes.

     Right now only invariants are allowed to persist beyond a pass in
     this field; in the future we will allow VALUE_HANDLEs to persist
     as well.  */
  tree value_handle;

  /* Immediate uses list for this SSA_NAME.  */
  struct ssa_use_operand_d imm_uses;
};

/* In a PHI_NODE node.  */

/* These 2 macros should be considered off limits for use by developers.  If
   you wish to access the use or def fields of a PHI_NODE in the SSA
   optimizers, use the accessor macros found in tree-ssa-operands.h.
   These two macros are to be used only by those accessor macros, and other
   select places where we *absolutely* must take the address of the tree.  */

#define PHI_RESULT_TREE(NODE)		PHI_NODE_CHECK (NODE)->phi.result
#define PHI_ARG_DEF_TREE(NODE, I)	PHI_NODE_ELT_CHECK (NODE, I).def

/* PHI_NODEs for each basic block are chained together in a single linked
   list.  The head of the list is linked from the block annotation, and
   the link to the next PHI is in PHI_CHAIN.  */
#define PHI_CHAIN(NODE)		TREE_CHAIN (PHI_NODE_CHECK (NODE))

#define PHI_NUM_ARGS(NODE)		PHI_NODE_CHECK (NODE)->phi.num_args
#define PHI_ARG_CAPACITY(NODE)		PHI_NODE_CHECK (NODE)->phi.capacity
#define PHI_ARG_ELT(NODE, I)		PHI_NODE_ELT_CHECK (NODE, I)
#define PHI_ARG_EDGE(NODE, I) 		(EDGE_PRED (PHI_BB ((NODE)), (I)))
#define PHI_BB(NODE)			PHI_NODE_CHECK (NODE)->phi.bb
#define PHI_ARG_IMM_USE_NODE(NODE, I)	PHI_NODE_ELT_CHECK (NODE, I).imm_use

struct phi_arg_d GTY(())
{
  /* imm_use MUST be the first element in struct because we do some
     pointer arithmetic with it.  See phi_arg_index_from_use.  */
  struct ssa_use_operand_d imm_use;
  tree def;
};

struct tree_phi_node GTY(())
{
  struct tree_common common;
  tree result;
  int num_args;
  int capacity;

  /* Basic block to that the phi node belongs.  */
  struct basic_block_def *bb;

  /* Arguments of the PHI node.  These are maintained in the same
     order as predecessor edge vector BB->PREDS.  */
  struct phi_arg_d GTY ((length ("((tree)&%h)->phi.num_args"))) a[1];
};

#define OMP_CLAUSE_CODE(NODE)					\
	(OMP_CLAUSE_CHECK (NODE))->omp_clause.code

#define OMP_CLAUSE_SET_CODE(NODE, CODE)				\
	((OMP_CLAUSE_CHECK (NODE))->omp_clause.code = (CODE))

#define OMP_CLAUSE_CODE(NODE)					\
	(OMP_CLAUSE_CHECK (NODE))->omp_clause.code

#define OMP_CLAUSE_OPERAND(NODE, I)				\
	OMP_CLAUSE_ELT_CHECK (NODE, I)

struct tree_omp_clause GTY(())
{
  struct tree_common common;
  enum omp_clause_code code;
  union omp_clause_subcode {
    enum omp_clause_default_kind  default_kind;
    enum omp_clause_schedule_kind schedule_kind;
    enum tree_code                reduction_code;
  } GTY ((skip)) subcode;
  tree GTY ((length ("omp_clause_num_ops[OMP_CLAUSE_CODE ((tree)&%h)]"))) ops[1];
};


struct varray_head_tag;

/* In a BLOCK node.  */
#define BLOCK_VARS(NODE) (BLOCK_CHECK (NODE)->block.vars)
#define BLOCK_SUBBLOCKS(NODE) (BLOCK_CHECK (NODE)->block.subblocks)
#define BLOCK_SUPERCONTEXT(NODE) (BLOCK_CHECK (NODE)->block.supercontext)
/* Note: when changing this, make sure to find the places
   that use chainon or nreverse.  */
#define BLOCK_CHAIN(NODE) TREE_CHAIN (BLOCK_CHECK (NODE))
#define BLOCK_ABSTRACT_ORIGIN(NODE) (BLOCK_CHECK (NODE)->block.abstract_origin)
#define BLOCK_ABSTRACT(NODE) (BLOCK_CHECK (NODE)->block.abstract_flag)

/* Nonzero means that this block is prepared to handle exceptions
   listed in the BLOCK_VARS slot.  */
#define BLOCK_HANDLER_BLOCK(NODE) \
  (BLOCK_CHECK (NODE)->block.handler_block_flag)

/* An index number for this block.  These values are not guaranteed to
   be unique across functions -- whether or not they are depends on
   the debugging output format in use.  */
#define BLOCK_NUMBER(NODE) (BLOCK_CHECK (NODE)->block.block_num)

/* If block reordering splits a lexical block into discontiguous
   address ranges, we'll make a copy of the original block.

   Note that this is logically distinct from BLOCK_ABSTRACT_ORIGIN.
   In that case, we have one source block that has been replicated
   (through inlining or unrolling) into many logical blocks, and that
   these logical blocks have different physical variables in them.

   In this case, we have one logical block split into several
   non-contiguous address ranges.  Most debug formats can't actually
   represent this idea directly, so we fake it by creating multiple
   logical blocks with the same variables in them.  However, for those
   that do support non-contiguous regions, these allow the original
   logical block to be reconstructed, along with the set of address
   ranges.

   One of the logical block fragments is arbitrarily chosen to be
   the ORIGIN.  The other fragments will point to the origin via
   BLOCK_FRAGMENT_ORIGIN; the origin itself will have this pointer
   be null.  The list of fragments will be chained through
   BLOCK_FRAGMENT_CHAIN from the origin.  */

#define BLOCK_FRAGMENT_ORIGIN(NODE) (BLOCK_CHECK (NODE)->block.fragment_origin)
#define BLOCK_FRAGMENT_CHAIN(NODE) (BLOCK_CHECK (NODE)->block.fragment_chain)

/* For an inlined function, this gives the location where it was called
   from.  This is only set in the top level block, which corresponds to the
   inlined function scope.  This is used in the debug output routines.  */

#define BLOCK_SOURCE_LOCATION(NODE) (BLOCK_CHECK (NODE)->block.locus)

struct tree_block GTY(())
{
  struct tree_common common;

  unsigned handler_block_flag : 1;
  unsigned abstract_flag : 1;
  unsigned block_num : 30;

  tree vars;
  tree subblocks;
  tree supercontext;
  tree abstract_origin;
  tree fragment_origin;
  tree fragment_chain;
  location_t locus;
};

/* Define fields and accessors for nodes representing data types.  */

/* See tree.def for documentation of the use of these fields.
   Look at the documentation of the various ..._TYPE tree codes.

   Note that the type.values, type.minval, and type.maxval fields are
   overloaded and used for different macros in different kinds of types.
   Each macro must check to ensure the tree node is of the proper kind of
   type.  Note also that some of the front-ends also overload these fields,
   so they must be checked as well.  */

#define TYPE_UID(NODE) (TYPE_CHECK (NODE)->type.uid)
#define TYPE_SIZE(NODE) (TYPE_CHECK (NODE)->type.size)
#define TYPE_SIZE_UNIT(NODE) (TYPE_CHECK (NODE)->type.size_unit)
#define TYPE_MODE(NODE) (TYPE_CHECK (NODE)->type.mode)
#define TYPE_VALUES(NODE) (ENUMERAL_TYPE_CHECK (NODE)->type.values)
#define TYPE_DOMAIN(NODE) (ARRAY_TYPE_CHECK (NODE)->type.values)
#define TYPE_FIELDS(NODE) (RECORD_OR_UNION_CHECK (NODE)->type.values)
#define TYPE_CACHED_VALUES(NODE) (TYPE_CHECK(NODE)->type.values)
#define TYPE_ORIG_SIZE_TYPE(NODE)			\
  (INTEGER_TYPE_CHECK (NODE)->type.values		\
  ? TREE_TYPE ((NODE)->type.values) : NULL_TREE)
#define TYPE_METHODS(NODE) (RECORD_OR_UNION_CHECK (NODE)->type.maxval)
#define TYPE_VFIELD(NODE) (RECORD_OR_UNION_CHECK (NODE)->type.minval)
#define TYPE_ARG_TYPES(NODE) (FUNC_OR_METHOD_CHECK (NODE)->type.values)
#define TYPE_METHOD_BASETYPE(NODE) (FUNC_OR_METHOD_CHECK (NODE)->type.maxval)
#define TYPE_OFFSET_BASETYPE(NODE) (OFFSET_TYPE_CHECK (NODE)->type.maxval)
#define TYPE_POINTER_TO(NODE) (TYPE_CHECK (NODE)->type.pointer_to)
#define TYPE_REFERENCE_TO(NODE) (TYPE_CHECK (NODE)->type.reference_to)
#define TYPE_NEXT_PTR_TO(NODE) (POINTER_TYPE_CHECK (NODE)->type.minval)
#define TYPE_NEXT_REF_TO(NODE) (REFERENCE_TYPE_CHECK (NODE)->type.minval)
#define TYPE_MIN_VALUE(NODE) (NUMERICAL_TYPE_CHECK (NODE)->type.minval)
#define TYPE_MAX_VALUE(NODE) (NUMERICAL_TYPE_CHECK (NODE)->type.maxval)
#define TYPE_PRECISION(NODE) (TYPE_CHECK (NODE)->type.precision)
#define TYPE_SYMTAB_ADDRESS(NODE) (TYPE_CHECK (NODE)->type.symtab.address)
#define TYPE_SYMTAB_POINTER(NODE) (TYPE_CHECK (NODE)->type.symtab.pointer)
#define TYPE_SYMTAB_DIE(NODE) (TYPE_CHECK (NODE)->type.symtab.die)
#define TYPE_NAME(NODE) (TYPE_CHECK (NODE)->type.name)
#define TYPE_NEXT_VARIANT(NODE) (TYPE_CHECK (NODE)->type.next_variant)
#define TYPE_MAIN_VARIANT(NODE) (TYPE_CHECK (NODE)->type.main_variant)
#define TYPE_CONTEXT(NODE) (TYPE_CHECK (NODE)->type.context)
#define TYPE_LANG_SPECIFIC(NODE) (TYPE_CHECK (NODE)->type.lang_specific)

/* For a VECTOR_TYPE node, this describes a different type which is emitted
   in the debugging output.  We use this to describe a vector as a
   structure containing an array.  */
#define TYPE_DEBUG_REPRESENTATION_TYPE(NODE) (VECTOR_TYPE_CHECK (NODE)->type.values)

/* For record and union types, information about this type, as a base type
   for itself.  */
#define TYPE_BINFO(NODE) (RECORD_OR_UNION_CHECK(NODE)->type.binfo)

/* For non record and union types, used in a language-dependent way.  */
#define TYPE_LANG_SLOT_1(NODE) (NOT_RECORD_OR_UNION_CHECK(NODE)->type.binfo)

/* The (language-specific) typed-based alias set for this type.
   Objects whose TYPE_ALIAS_SETs are different cannot alias each
   other.  If the TYPE_ALIAS_SET is -1, no alias set has yet been
   assigned to this type.  If the TYPE_ALIAS_SET is 0, objects of this
   type can alias objects of any type.  */
#define TYPE_ALIAS_SET(NODE) (TYPE_CHECK (NODE)->type.alias_set)

/* Nonzero iff the typed-based alias set for this type has been
   calculated.  */
#define TYPE_ALIAS_SET_KNOWN_P(NODE) (TYPE_CHECK (NODE)->type.alias_set != -1)

/* A TREE_LIST of IDENTIFIER nodes of the attributes that apply
   to this type.  */
#define TYPE_ATTRIBUTES(NODE) (TYPE_CHECK (NODE)->type.attributes)

/* The alignment necessary for objects of this type.
   The value is an int, measured in bits.  */
#define TYPE_ALIGN(NODE) (TYPE_CHECK (NODE)->type.align)

/* 1 if the alignment for this type was requested by "aligned" attribute,
   0 if it is the default for this type.  */
#define TYPE_USER_ALIGN(NODE) (TYPE_CHECK (NODE)->type.user_align)

/* The alignment for NODE, in bytes.  */
#define TYPE_ALIGN_UNIT(NODE) (TYPE_ALIGN (NODE) / BITS_PER_UNIT)

/* If your language allows you to declare types, and you want debug info
   for them, then you need to generate corresponding TYPE_DECL nodes.
   These "stub" TYPE_DECL nodes have no name, and simply point at the
   type node.  You then set the TYPE_STUB_DECL field of the type node
   to point back at the TYPE_DECL node.  This allows the debug routines
   to know that the two nodes represent the same type, so that we only
   get one debug info record for them.  */
#define TYPE_STUB_DECL(NODE) TREE_CHAIN (NODE)

/* In a RECORD_TYPE, UNION_TYPE or QUAL_UNION_TYPE, it means the type
   has BLKmode only because it lacks the alignment requirement for
   its size.  */
#define TYPE_NO_FORCE_BLK(NODE) (TYPE_CHECK (NODE)->type.no_force_blk_flag)

/* In an INTEGER_TYPE, it means the type represents a size.  We use
   this both for validity checking and to permit optimizations that
   are unsafe for other types.  Note that the C `size_t' type should
   *not* have this flag set.  The `size_t' type is simply a typedef
   for an ordinary integer type that happens to be the type of an
   expression returned by `sizeof'; `size_t' has no special
   properties.  Expressions whose type have TYPE_IS_SIZETYPE set are
   always actual sizes.  */
#define TYPE_IS_SIZETYPE(NODE) \
  (INTEGER_TYPE_CHECK (NODE)->type.no_force_blk_flag)

/* In a FUNCTION_TYPE, indicates that the function returns with the stack
   pointer depressed.  */
#define TYPE_RETURNS_STACK_DEPRESSED(NODE) \
  (FUNCTION_TYPE_CHECK (NODE)->type.no_force_blk_flag)

/* Nonzero in a type considered volatile as a whole.  */
#define TYPE_VOLATILE(NODE) (TYPE_CHECK (NODE)->common.volatile_flag)

/* Means this type is const-qualified.  */
#define TYPE_READONLY(NODE) (TYPE_CHECK (NODE)->common.readonly_flag)

/* If nonzero, this type is `restrict'-qualified, in the C sense of
   the term.  */
#define TYPE_RESTRICT(NODE) (TYPE_CHECK (NODE)->type.restrict_flag)

/* There is a TYPE_QUAL value for each type qualifier.  They can be
   combined by bitwise-or to form the complete set of qualifiers for a
   type.  */

#define TYPE_UNQUALIFIED   0x0
#define TYPE_QUAL_CONST    0x1
#define TYPE_QUAL_VOLATILE 0x2
#define TYPE_QUAL_RESTRICT 0x4

/* The set of type qualifiers for this type.  */
#define TYPE_QUALS(NODE)					\
  ((TYPE_READONLY (NODE) * TYPE_QUAL_CONST)			\
   | (TYPE_VOLATILE (NODE) * TYPE_QUAL_VOLATILE)		\
   | (TYPE_RESTRICT (NODE) * TYPE_QUAL_RESTRICT))

/* These flags are available for each language front end to use internally.  */
#define TYPE_LANG_FLAG_0(NODE) (TYPE_CHECK (NODE)->type.lang_flag_0)
#define TYPE_LANG_FLAG_1(NODE) (TYPE_CHECK (NODE)->type.lang_flag_1)
#define TYPE_LANG_FLAG_2(NODE) (TYPE_CHECK (NODE)->type.lang_flag_2)
#define TYPE_LANG_FLAG_3(NODE) (TYPE_CHECK (NODE)->type.lang_flag_3)
#define TYPE_LANG_FLAG_4(NODE) (TYPE_CHECK (NODE)->type.lang_flag_4)
#define TYPE_LANG_FLAG_5(NODE) (TYPE_CHECK (NODE)->type.lang_flag_5)
#define TYPE_LANG_FLAG_6(NODE) (TYPE_CHECK (NODE)->type.lang_flag_6)

/* Used to keep track of visited nodes in tree traversals.  This is set to
   0 by copy_node and make_node.  */
#define TREE_VISITED(NODE) ((NODE)->common.visited)

/* If set in an ARRAY_TYPE, indicates a string type (for languages
   that distinguish string from array of char).
   If set in a INTEGER_TYPE, indicates a character type.  */
#define TYPE_STRING_FLAG(NODE) (TYPE_CHECK (NODE)->type.string_flag)

/* If non-NULL, this is an upper bound of the size (in bytes) of an
   object of the given ARRAY_TYPE.  This allows temporaries to be
   allocated.  */
#define TYPE_ARRAY_MAX_SIZE(ARRAY_TYPE) \
  (ARRAY_TYPE_CHECK (ARRAY_TYPE)->type.maxval)

/* For a VECTOR_TYPE, this is the number of sub-parts of the vector.  */
#define TYPE_VECTOR_SUBPARTS(VECTOR_TYPE) \
  (((unsigned HOST_WIDE_INT) 1) \
   << VECTOR_TYPE_CHECK (VECTOR_TYPE)->type.precision)

/* Set precision to n when we have 2^n sub-parts of the vector.  */
#define SET_TYPE_VECTOR_SUBPARTS(VECTOR_TYPE, X) \
  (VECTOR_TYPE_CHECK (VECTOR_TYPE)->type.precision = exact_log2 (X))

/* Indicates that objects of this type must be initialized by calling a
   function when they are created.  */
#define TYPE_NEEDS_CONSTRUCTING(NODE) \
  (TYPE_CHECK (NODE)->type.needs_constructing_flag)

/* Indicates that objects of this type (a UNION_TYPE), should be passed
   the same way that the first union alternative would be passed.  */
#define TYPE_TRANSPARENT_UNION(NODE)  \
  (UNION_TYPE_CHECK (NODE)->type.transparent_union_flag)

/* For an ARRAY_TYPE, indicates that it is not permitted to
   take the address of a component of the type.  */
#define TYPE_NONALIASED_COMPONENT(NODE) \
  (ARRAY_TYPE_CHECK (NODE)->type.transparent_union_flag)

/* Indicated that objects of this type should be laid out in as
   compact a way as possible.  */
#define TYPE_PACKED(NODE) (TYPE_CHECK (NODE)->type.packed_flag)

/* Used by type_contains_placeholder_p to avoid recomputation.
   Values are: 0 (unknown), 1 (false), 2 (true).  Never access
   this field directly.  */
#define TYPE_CONTAINS_PLACEHOLDER_INTERNAL(NODE) \
  (TYPE_CHECK (NODE)->type.contains_placeholder_bits)

/* APPLE LOCAL begin radar 5811943 - Fix type of pointers to blocks  */
/* Indicates that the struct type is a block struct, rather than
   a 'normal' struct, i.e. one of its fields is a function that can
   be called.  This uses the existing bit-field lang_flag_2 in the
   struct tree_type, rather than creating a new bit field, as 
   lang_flag_2 is currently unused and we don't want to increase the 
   size of trees if we can avoid it.  */
#define TYPE_BLOCK_IMPL_STRUCT(NODE) \
(TYPE_CHECK (NODE)->type.lang_flag_2)
/* APPLE LOCAL end radar 5811943 - Fix type of pointers to Blocks  */

struct die_struct;

struct tree_type GTY(())
{
  struct tree_common common;
  tree values;
  tree size;
  tree size_unit;
  tree attributes;
  unsigned int uid;

  unsigned int precision : 9;
  ENUM_BITFIELD(machine_mode) mode : 7;

  unsigned string_flag : 1;
  unsigned no_force_blk_flag : 1;
  unsigned needs_constructing_flag : 1;
  unsigned transparent_union_flag : 1;
  unsigned packed_flag : 1;
  unsigned restrict_flag : 1;
  unsigned contains_placeholder_bits : 2;

  unsigned lang_flag_0 : 1;
  unsigned lang_flag_1 : 1;
  /* APPLE LOCAL begin radar 5811943 - Fix type of pointers to Blocks  */
  /* Since it is currently completely unused, and in the interest of
     not making trees any bigger than they already are, lang_flag_2
     in the tree_type struct will be used to indicate that a struct is a 
     block struct.  The macro used for these purposes is 
     TYPE_BLOCK_IMPL_STRUCT, rather than TYPE_LANG_FLAG_2, in order to make 
     its uses in the code more clear.  */
  /* APPLE LOCAL end radar 5811943 - Fix type of pointers to Blocks  */
  unsigned lang_flag_2 : 1;
  unsigned lang_flag_3 : 1;
  unsigned lang_flag_4 : 1;
  unsigned lang_flag_5 : 1;
  unsigned lang_flag_6 : 1;
  unsigned user_align : 1;

  unsigned int align;
  tree pointer_to;
  tree reference_to;
  union tree_type_symtab {
    int GTY ((tag ("0"))) address;
    char * GTY ((tag ("1"))) pointer;
    struct die_struct * GTY ((tag ("2"))) die;
  } GTY ((desc ("debug_hooks == &sdb_debug_hooks ? 1 : debug_hooks == &dwarf2_debug_hooks ? 2 : 0"),
	  descbits ("2"))) symtab;
  tree name;
  tree minval;
  tree maxval;
  tree next_variant;
  tree main_variant;
  tree binfo;
  tree context;
  HOST_WIDE_INT alias_set;
  /* Points to a structure whose details depend on the language in use.  */
  struct lang_type *lang_specific;
};

/* Define accessor macros for information about type inheritance
   and basetypes.

   A "basetype" means a particular usage of a data type for inheritance
   in another type.  Each such basetype usage has its own "binfo"
   object to describe it.  The binfo object is a TREE_VEC node.

   Inheritance is represented by the binfo nodes allocated for a
   given type.  For example, given types C and D, such that D is
   inherited by C, 3 binfo nodes will be allocated: one for describing
   the binfo properties of C, similarly one for D, and one for
   describing the binfo properties of D as a base type for C.
   Thus, given a pointer to class C, one can get a pointer to the binfo
   of D acting as a basetype for C by looking at C's binfo's basetypes.  */

/* BINFO specific flags.  */

/* Nonzero means that the derivation chain is via a `virtual' declaration.  */
#define BINFO_VIRTUAL_P(NODE) (TREE_BINFO_CHECK (NODE)->common.static_flag)

/* Flags for language dependent use.  */
#define BINFO_MARKED(NODE) TREE_LANG_FLAG_0(TREE_BINFO_CHECK(NODE))
#define BINFO_FLAG_1(NODE) TREE_LANG_FLAG_1(TREE_BINFO_CHECK(NODE))
#define BINFO_FLAG_2(NODE) TREE_LANG_FLAG_2(TREE_BINFO_CHECK(NODE))
#define BINFO_FLAG_3(NODE) TREE_LANG_FLAG_3(TREE_BINFO_CHECK(NODE))
#define BINFO_FLAG_4(NODE) TREE_LANG_FLAG_4(TREE_BINFO_CHECK(NODE))
#define BINFO_FLAG_5(NODE) TREE_LANG_FLAG_5(TREE_BINFO_CHECK(NODE))
#define BINFO_FLAG_6(NODE) TREE_LANG_FLAG_6(TREE_BINFO_CHECK(NODE))

/* The actual data type node being inherited in this basetype.  */
#define BINFO_TYPE(NODE) TREE_TYPE (TREE_BINFO_CHECK(NODE))

/* The offset where this basetype appears in its containing type.
   BINFO_OFFSET slot holds the offset (in bytes)
   from the base of the complete object to the base of the part of the
   object that is allocated on behalf of this `type'.
   This is always 0 except when there is multiple inheritance.  */

#define BINFO_OFFSET(NODE) (TREE_BINFO_CHECK(NODE)->binfo.offset)
#define BINFO_OFFSET_ZEROP(NODE) (integer_zerop (BINFO_OFFSET (NODE)))

/* The virtual function table belonging to this basetype.  Virtual
   function tables provide a mechanism for run-time method dispatching.
   The entries of a virtual function table are language-dependent.  */

#define BINFO_VTABLE(NODE) (TREE_BINFO_CHECK(NODE)->binfo.vtable)

/* The virtual functions in the virtual function table.  This is
   a TREE_LIST that is used as an initial approximation for building
   a virtual function table for this basetype.  */
#define BINFO_VIRTUALS(NODE) (TREE_BINFO_CHECK(NODE)->binfo.virtuals)

/* A vector of binfos for the direct basetypes inherited by this
   basetype.

   If this basetype describes type D as inherited in C, and if the
   basetypes of D are E and F, then this vector contains binfos for
   inheritance of E and F by C.  */
#define BINFO_BASE_BINFOS(NODE) (&TREE_BINFO_CHECK(NODE)->binfo.base_binfos)

/* The number of basetypes for NODE.  */
#define BINFO_N_BASE_BINFOS(NODE) (VEC_length (tree, BINFO_BASE_BINFOS (NODE)))

/* Accessor macro to get to the Nth base binfo of this binfo.  */
#define BINFO_BASE_BINFO(NODE,N) \
 (VEC_index (tree, BINFO_BASE_BINFOS (NODE), (N)))
#define BINFO_BASE_ITERATE(NODE,N,B) \
 (VEC_iterate (tree, BINFO_BASE_BINFOS (NODE), (N), (B)))
#define BINFO_BASE_APPEND(NODE,T) \
 (VEC_quick_push (tree, BINFO_BASE_BINFOS (NODE), (T)))

/* For a BINFO record describing a virtual base class, i.e., one where
   TREE_VIA_VIRTUAL is set, this field assists in locating the virtual
   base.  The actual contents are language-dependent.  In the C++
   front-end this field is an INTEGER_CST giving an offset into the
   vtable where the offset to the virtual base can be found.  */
#define BINFO_VPTR_FIELD(NODE) (TREE_BINFO_CHECK(NODE)->binfo.vptr_field)

/* Indicates the accesses this binfo has to its bases. The values are
   access_public_node, access_protected_node or access_private_node.
   If this array is not present, public access is implied.  */
#define BINFO_BASE_ACCESSES(NODE) (TREE_BINFO_CHECK(NODE)->binfo.base_accesses)

#define BINFO_BASE_ACCESS(NODE,N) \
  VEC_index (tree, BINFO_BASE_ACCESSES (NODE), (N))
#define BINFO_BASE_ACCESS_APPEND(NODE,T) \
  VEC_quick_push (tree, BINFO_BASE_ACCESSES (NODE), (T))

/* The index in the VTT where this subobject's sub-VTT can be found.
   NULL_TREE if there is no sub-VTT.  */
#define BINFO_SUBVTT_INDEX(NODE) (TREE_BINFO_CHECK(NODE)->binfo.vtt_subvtt)

/* The index in the VTT where the vptr for this subobject can be
   found.  NULL_TREE if there is no secondary vptr in the VTT.  */
#define BINFO_VPTR_INDEX(NODE) (TREE_BINFO_CHECK(NODE)->binfo.vtt_vptr)

/* The BINFO_INHERITANCE_CHAIN points at the binfo for the base
   inheriting this base for non-virtual bases. For virtual bases it
   points either to the binfo for which this is a primary binfo, or to
   the binfo of the most derived type.  */
#define BINFO_INHERITANCE_CHAIN(NODE) \
	(TREE_BINFO_CHECK(NODE)->binfo.inheritance)

struct tree_binfo GTY (())
{
  struct tree_common common;

  tree offset;
  tree vtable;
  tree virtuals;
  tree vptr_field;
  VEC(tree,gc) *base_accesses;
  tree inheritance;

  tree vtt_subvtt;
  tree vtt_vptr;

  VEC(tree,none) base_binfos;
};


/* Define fields and accessors for nodes representing declared names.  */

/* Nonzero if DECL represents a variable for the SSA passes.  */
#define SSA_VAR_P(DECL)							\
	(TREE_CODE (DECL) == VAR_DECL					\
	 || TREE_CODE (DECL) == PARM_DECL				\
	 || TREE_CODE (DECL) == RESULT_DECL				\
	 || MTAG_P (DECL)						\
	 || (TREE_CODE (DECL) == SSA_NAME				\
	     && (TREE_CODE (SSA_NAME_VAR (DECL)) == VAR_DECL		\
		 || TREE_CODE (SSA_NAME_VAR (DECL)) == PARM_DECL	\
		 || TREE_CODE (SSA_NAME_VAR (DECL)) == RESULT_DECL	\
		 || MTAG_P (SSA_NAME_VAR (DECL)))))




/* Enumerate visibility settings.  */
#ifndef SYMBOL_VISIBILITY_DEFINED
#define SYMBOL_VISIBILITY_DEFINED
enum symbol_visibility
{
  VISIBILITY_DEFAULT,
  VISIBILITY_PROTECTED,
  VISIBILITY_HIDDEN,
  VISIBILITY_INTERNAL
};
#endif

struct function;


/* This is the name of the object as written by the user.
   It is an IDENTIFIER_NODE.  */
#define DECL_NAME(NODE) (DECL_MINIMAL_CHECK (NODE)->decl_minimal.name)

/* Every ..._DECL node gets a unique number.  */
#define DECL_UID(NODE) (DECL_MINIMAL_CHECK (NODE)->decl_minimal.uid)

/* These two fields describe where in the source code the declaration
   was.  If the declaration appears in several places (as for a C
   function that is declared first and then defined later), this
   information should refer to the definition.  */
#define DECL_SOURCE_LOCATION(NODE) (DECL_MINIMAL_CHECK (NODE)->decl_minimal.locus)
#define DECL_SOURCE_FILE(NODE) LOCATION_FILE (DECL_SOURCE_LOCATION (NODE))
#define DECL_SOURCE_LINE(NODE) LOCATION_LINE (DECL_SOURCE_LOCATION (NODE))
#ifdef USE_MAPPED_LOCATION
#define DECL_IS_BUILTIN(DECL) \
  (DECL_SOURCE_LOCATION (DECL) <= BUILTINS_LOCATION)
#else
#define DECL_IS_BUILTIN(DECL) (DECL_SOURCE_LINE(DECL) == 0)
#endif

/*  For FIELD_DECLs, this is the RECORD_TYPE, UNION_TYPE, or
    QUAL_UNION_TYPE node that the field is a member of.  For VAR_DECL,
    PARM_DECL, FUNCTION_DECL, LABEL_DECL, and CONST_DECL nodes, this
    points to either the FUNCTION_DECL for the containing function,
    the RECORD_TYPE or UNION_TYPE for the containing type, or
    NULL_TREE or a TRANSLATION_UNIT_DECL if the given decl has "file
    scope".  */
#define DECL_CONTEXT(NODE) (DECL_MINIMAL_CHECK (NODE)->decl_minimal.context)
#define DECL_FIELD_CONTEXT(NODE) (FIELD_DECL_CHECK (NODE)->decl_minimal.context)
struct tree_decl_minimal GTY(())
{
  struct tree_common common;
  location_t locus;
  unsigned int uid;
  tree name;
  tree context;
};

/* When computing aliasing information, we represent the memory pointed-to
   by pointers with artificial variables called "memory tags" (MT).  There
   are two kinds of tags, namely symbol and name:

   Symbol tags (SMT) are used in flow-insensitive alias analysis, they
   represent all the pointed-to locations and variables pointed-to by
   the same pointer symbol.  Usually, this set is computed using
   type-based analysis (i.e., alias set classes), but this may not
   always be the case.

   Name tags (NMT) are used in flow-sensitive points-to alias
   analysis, they represent the variables and memory locations
   pointed-to by a specific SSA_NAME pointer.

   In general, given a pointer P with a symbol tag SMT, the alias set
   of SMT should be the union of all the alias sets of the NMTs of
   every SSA_NAME for P.  */
struct tree_memory_tag GTY(())
{
  struct tree_decl_minimal common;
  unsigned int is_global:1;
  unsigned int is_used_alone:1;
  unsigned int old_used_alone:1;
};

#define MTAG_GLOBAL(NODE) (TREE_MEMORY_TAG_CHECK (NODE)->mtag.is_global)

/* This flag is true if a SMT is used as the V_MAY_DEF or VUSE operand
   directly, because the access had all of the SMT's aliases pruned
   from it.  */
#define SMT_USED_ALONE(NODE) (SYMBOL_MEMORY_TAG_CHECK (NODE)->mtag.is_used_alone)

/* This flag is used to temporarily store the old value of the used alone
   flag when updating so we know whether to mark the symbol for
   renaming.  */
#define SMT_OLD_USED_ALONE(NODE) (SYMBOL_MEMORY_TAG_CHECK (NODE)->mtag.old_used_alone)

struct tree_struct_field_tag GTY(())
{
  struct tree_memory_tag common;

  /* Parent variable.  */
  tree parent_var;

  /* Offset inside structure.  */
  unsigned HOST_WIDE_INT offset;

  /* Size of the field.  */
  unsigned HOST_WIDE_INT size;

};
#define SFT_PARENT_VAR(NODE) (STRUCT_FIELD_TAG_CHECK (NODE)->sft.parent_var)
#define SFT_OFFSET(NODE) (STRUCT_FIELD_TAG_CHECK (NODE)->sft.offset)
#define SFT_SIZE(NODE) (STRUCT_FIELD_TAG_CHECK (NODE)->sft.size)

/* For any sort of a ..._DECL node, this points to the original (abstract)
   decl node which this decl is an instance of, or else it is NULL indicating
   that this decl is not an instance of some other decl.  For example,
   in a nested declaration of an inline function, this points back to the
   definition.  */
#define DECL_ABSTRACT_ORIGIN(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.abstract_origin)

/* Like DECL_ABSTRACT_ORIGIN, but returns NODE if there's no abstract
   origin.  This is useful when setting the DECL_ABSTRACT_ORIGIN.  */
#define DECL_ORIGIN(NODE) \
  (DECL_ABSTRACT_ORIGIN (NODE) ? DECL_ABSTRACT_ORIGIN (NODE) : (NODE))

/* Nonzero for any sort of ..._DECL node means this decl node represents an
   inline instance of some original (abstract) decl from an inline function;
   suppress any warnings about shadowing some other variable.  FUNCTION_DECL
   nodes can also have their abstract origin set to themselves.  */
#define DECL_FROM_INLINE(NODE) (DECL_ABSTRACT_ORIGIN (NODE) != NULL_TREE \
				&& DECL_ABSTRACT_ORIGIN (NODE) != (NODE))

/* In a DECL this is the field where attributes are stored.  */
#define DECL_ATTRIBUTES(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.attributes)

/* For a FUNCTION_DECL, holds the tree of BINDINGs.
   For a TRANSLATION_UNIT_DECL, holds the namespace's BLOCK.
   For a VAR_DECL, holds the initial value.
   For a PARM_DECL, not used--default
   values for parameters are encoded in the type of the function,
   not in the PARM_DECL slot.
   For a FIELD_DECL, this is used for enumeration values and the C
   frontend uses it for temporarily storing bitwidth of bitfields.

   ??? Need to figure out some way to check this isn't a PARM_DECL.  */
#define DECL_INITIAL(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.initial)

/* Holds the size of the datum, in bits, as a tree expression.
   Need not be constant.  */
#define DECL_SIZE(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.size)
/* Likewise for the size in bytes.  */
#define DECL_SIZE_UNIT(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.size_unit)
/* Holds the alignment required for the datum, in bits.  */
#define DECL_ALIGN(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.align)
/* The alignment of NODE, in bytes.  */
#define DECL_ALIGN_UNIT(NODE) (DECL_ALIGN (NODE) / BITS_PER_UNIT)
/* Set if the alignment of this DECL has been set by the user, for
   example with an 'aligned' attribute.  */
#define DECL_USER_ALIGN(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.user_align)
/* Holds the machine mode corresponding to the declaration of a variable or
   field.  Always equal to TYPE_MODE (TREE_TYPE (decl)) except for a
   FIELD_DECL.  */
#define DECL_MODE(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.mode)

/* For FUNCTION_DECL, if it is built-in, this identifies which built-in
   operation it is.  Note, however, that this field is overloaded, with
   DECL_BUILT_IN_CLASS as the discriminant, so the latter must always be
   checked before any access to the former.  */
#define DECL_FUNCTION_CODE(NODE) \
  (FUNCTION_DECL_CHECK (NODE)->function_decl.function_code)
#define DECL_DEBUG_EXPR_IS_FROM(NODE) \
  (DECL_COMMON_CHECK (NODE)->decl_common.debug_expr_is_from)

/* Nonzero for a given ..._DECL node means that the name of this node should
   be ignored for symbolic debug purposes.  */
#define DECL_IGNORED_P(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.ignored_flag)

/* Nonzero for a given ..._DECL node means that this node represents an
   "abstract instance" of the given declaration (e.g. in the original
   declaration of an inline function).  When generating symbolic debugging
   information, we mustn't try to generate any address information for nodes
   marked as "abstract instances" because we don't actually generate
   any code or allocate any data space for such instances.  */
#define DECL_ABSTRACT(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.abstract_flag)

/* Language-specific decl information.  */
#define DECL_LANG_SPECIFIC(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_specific)

/* In a VAR_DECL or FUNCTION_DECL, nonzero means external reference:
   do not allocate storage, and refer to a definition elsewhere.  Note that
   this does not necessarily imply the entity represented by NODE
   has no program source-level definition in this translation unit.  For
   example, for a FUNCTION_DECL, DECL_SAVED_TREE may be non-NULL and
   DECL_EXTERNAL may be true simultaneously; that can be the case for
   a C99 "extern inline" function.  */
#define DECL_EXTERNAL(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.decl_flag_2)

/* In a VAR_DECL for a RECORD_TYPE, sets number for non-init_priority
   initializations.  */
#define DEFAULT_INIT_PRIORITY 65535
#define MAX_INIT_PRIORITY 65535
#define MAX_RESERVED_INIT_PRIORITY 100


/* Nonzero in a ..._DECL means this variable is ref'd from a nested function.
   For VAR_DECL nodes, PARM_DECL nodes, and FUNCTION_DECL nodes.

   For LABEL_DECL nodes, nonzero if nonlocal gotos to the label are permitted.

   Also set in some languages for variables, etc., outside the normal
   lexical scope, such as class instance variables.  */
#define DECL_NONLOCAL(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.nonlocal_flag)

/* Used in VAR_DECLs to indicate that the variable is a vtable.
   Used in FIELD_DECLs for vtable pointers.
   Used in FUNCTION_DECLs to indicate that the function is virtual.  */
#define DECL_VIRTUAL_P(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.virtual_flag)

/* Used to indicate that this DECL represents a compiler-generated entity.  */
#define DECL_ARTIFICIAL(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.artificial_flag)

/* Additional flags for language-specific uses.  */
#define DECL_LANG_FLAG_0(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_flag_0)
#define DECL_LANG_FLAG_1(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_flag_1)
#define DECL_LANG_FLAG_2(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_flag_2)
#define DECL_LANG_FLAG_3(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_flag_3)
#define DECL_LANG_FLAG_4(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_flag_4)
#define DECL_LANG_FLAG_5(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_flag_5)
#define DECL_LANG_FLAG_6(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_flag_6)
#define DECL_LANG_FLAG_7(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.lang_flag_7)

/* Used to indicate an alias set for the memory pointed to by this
   particular FIELD_DECL, PARM_DECL, or VAR_DECL, which must have
   pointer (or reference) type.  */
#define DECL_POINTER_ALIAS_SET(NODE) \
  (DECL_COMMON_CHECK (NODE)->decl_common.pointer_alias_set)

/* Nonzero if an alias set has been assigned to this declaration.  */
#define DECL_POINTER_ALIAS_SET_KNOWN_P(NODE) \
  (DECL_POINTER_ALIAS_SET (NODE) != - 1)

/* Nonzero for a decl which is at file scope.  */
#define DECL_FILE_SCOPE_P(EXP) 					\
  (! DECL_CONTEXT (EXP)						\
   || TREE_CODE (DECL_CONTEXT (EXP)) == TRANSLATION_UNIT_DECL)

/* Nonzero for a decl that is decorated using attribute used.
   This indicates compiler tools that this decl needs to be preserved.  */
#define DECL_PRESERVE_P(DECL) \
  DECL_COMMON_CHECK (DECL)->decl_common.preserve_flag

/* For function local variables of COMPLEX type, indicates that the
   variable is not aliased, and that all modifications to the variable
   have been adjusted so that they are killing assignments.  Thus the
   variable may now be treated as a GIMPLE register, and use real
   instead of virtual ops in SSA form.  */
#define DECL_COMPLEX_GIMPLE_REG_P(DECL) \
  DECL_COMMON_CHECK (DECL)->decl_common.gimple_reg_flag

/* This is true if DECL is call clobbered in the current function.
   The result of this flag should always be the same as
   bitmap_bit_p (call_clobbered_vars, DECL_UID (decl)).  */
#define DECL_CALL_CLOBBERED(DECL) \
  DECL_COMMON_CHECK (DECL)->decl_common.call_clobbered_flag

struct tree_decl_common GTY(())
{
  struct tree_decl_minimal common;
  tree size;

  ENUM_BITFIELD(machine_mode) mode : 8;

  unsigned nonlocal_flag : 1;
  unsigned virtual_flag : 1;
  unsigned ignored_flag : 1;
  unsigned abstract_flag : 1;
  unsigned artificial_flag : 1;
  unsigned user_align : 1;
  unsigned preserve_flag: 1;
  unsigned debug_expr_is_from : 1;

  unsigned lang_flag_0 : 1;
  unsigned lang_flag_1 : 1;
  unsigned lang_flag_2 : 1;
  unsigned lang_flag_3 : 1;
  unsigned lang_flag_4 : 1;
  unsigned lang_flag_5 : 1;
  unsigned lang_flag_6 : 1;
  unsigned lang_flag_7 : 1;

  /* In LABEL_DECL, this is DECL_ERROR_ISSUED.
     In VAR_DECL and PARM_DECL, this is DECL_REGISTER.  */
  unsigned decl_flag_0 : 1;
  /* In FIELD_DECL, this is DECL_PACKED.  */
  unsigned decl_flag_1 : 1;
  /* In FIELD_DECL, this is DECL_BIT_FIELD
     In VAR_DECL and FUNCTION_DECL, this is DECL_EXTERNAL.
     In TYPE_DECL, this is TYPE_DECL_SUPRESS_DEBUG.  */
  unsigned decl_flag_2 : 1;
  /* In FIELD_DECL, this is DECL_NONADDRESSABLE_P
     In VAR_DECL and PARM_DECL, this is DECL_HAS_VALUE_EXPR.  */
  unsigned decl_flag_3 : 1;
  /* Logically, these two would go in a theoretical base shared by var and
     parm decl. */
  unsigned gimple_reg_flag : 1;
  unsigned call_clobbered_flag : 1;

  /* APPLE LOCAL duplicate decls in multiple files. */
  unsigned duplicate_decl : 1;

  unsigned int align : 24;
  /* DECL_OFFSET_ALIGN, used only for FIELD_DECLs.  */
  unsigned int off_align : 8;

  tree size_unit;
  tree initial;
  tree attributes;
  tree abstract_origin;

  HOST_WIDE_INT pointer_alias_set;
  /* Points to a structure whose details depend on the language in use.  */
  struct lang_decl *lang_specific;
};

extern tree decl_value_expr_lookup (tree);
extern void decl_value_expr_insert (tree, tree);

/* In a VAR_DECL or PARM_DECL, the location at which the value may be found,
   if transformations have made this more complicated than evaluating the
   decl itself.  This should only be used for debugging; once this field has
   been set, the decl itself may not legitimately appear in the function.  */
#define DECL_HAS_VALUE_EXPR_P(NODE) \
  (TREE_CHECK2 (NODE, VAR_DECL, PARM_DECL)->decl_common.decl_flag_3)
#define DECL_VALUE_EXPR(NODE) \
  (decl_value_expr_lookup (DECL_WRTL_CHECK (NODE)))
#define SET_DECL_VALUE_EXPR(NODE, VAL)			\
  (decl_value_expr_insert (DECL_WRTL_CHECK (NODE), VAL))

/* Holds the RTL expression for the value of a variable or function.
   This value can be evaluated lazily for functions, variables with
   static storage duration, and labels.  */
#define DECL_RTL(NODE)					\
  (DECL_WRTL_CHECK (NODE)->decl_with_rtl.rtl		\
   ? (NODE)->decl_with_rtl.rtl					\
   : (make_decl_rtl (NODE), (NODE)->decl_with_rtl.rtl))

/* Set the DECL_RTL for NODE to RTL.  */
#define SET_DECL_RTL(NODE, RTL) set_decl_rtl (NODE, RTL)

/* Returns nonzero if NODE is a tree node that can contain RTL.  */
#define HAS_RTL_P(NODE) (CODE_CONTAINS_STRUCT (TREE_CODE (NODE), TS_DECL_WRTL))

/* Returns nonzero if the DECL_RTL for NODE has already been set.  */
#define DECL_RTL_SET_P(NODE)  (HAS_RTL_P (NODE) && DECL_WRTL_CHECK (NODE)->decl_with_rtl.rtl != NULL)

/* Copy the RTL from NODE1 to NODE2.  If the RTL was not set for
   NODE1, it will not be set for NODE2; this is a lazy copy.  */
#define COPY_DECL_RTL(NODE1, NODE2) \
  (DECL_WRTL_CHECK (NODE2)->decl_with_rtl.rtl = DECL_WRTL_CHECK (NODE1)->decl_with_rtl.rtl)

/* The DECL_RTL for NODE, if it is set, or NULL, if it is not set.  */
#define DECL_RTL_IF_SET(NODE) (DECL_RTL_SET_P (NODE) ? DECL_RTL (NODE) : NULL)

/* In VAR_DECL and PARM_DECL nodes, nonzero means declared `register'.  */
#define DECL_REGISTER(NODE) (DECL_WRTL_CHECK (NODE)->decl_common.decl_flag_0)

struct tree_decl_with_rtl GTY(())
{
  struct tree_decl_common common;
  rtx rtl;
};

/* In a FIELD_DECL, this is the field position, counting in bytes, of the
   DECL_OFFSET_ALIGN-bit-sized word containing the bit closest to the beginning
   of the structure.  */
#define DECL_FIELD_OFFSET(NODE) (FIELD_DECL_CHECK (NODE)->field_decl.offset)

/* In a FIELD_DECL, this is the offset, in bits, of the first bit of the
   field from DECL_FIELD_OFFSET.  This field may be nonzero even for fields
   that are not bit fields (since DECL_OFFSET_ALIGN may be larger than the
   natural alignment of the field's type).  */
#define DECL_FIELD_BIT_OFFSET(NODE) (FIELD_DECL_CHECK (NODE)->field_decl.bit_offset)

/* In a FIELD_DECL, this indicates whether the field was a bit-field and
   if so, the type that was originally specified for it.
   TREE_TYPE may have been modified (in finish_struct).  */
#define DECL_BIT_FIELD_TYPE(NODE) (FIELD_DECL_CHECK (NODE)->field_decl.bit_field_type)

/* For a FIELD_DECL in a QUAL_UNION_TYPE, records the expression, which
   if nonzero, indicates that the field occupies the type.  */
#define DECL_QUALIFIER(NODE) (FIELD_DECL_CHECK (NODE)->field_decl.qualifier)

/* For FIELD_DECLs, off_align holds the number of low-order bits of
   DECL_FIELD_OFFSET which are known to be always zero.
   DECL_OFFSET_ALIGN thus returns the alignment that DECL_FIELD_OFFSET
   has.  */
#define DECL_OFFSET_ALIGN(NODE) \
  (((unsigned HOST_WIDE_INT)1) << FIELD_DECL_CHECK (NODE)->decl_common.off_align)

/* Specify that DECL_ALIGN(NODE) is a multiple of X.  */
#define SET_DECL_OFFSET_ALIGN(NODE, X) \
  (FIELD_DECL_CHECK (NODE)->decl_common.off_align = exact_log2 ((X) & -(X)))
/* 1 if the alignment for this type was requested by "aligned" attribute,
   0 if it is the default for this type.  */

/* For FIELD_DECLS, DECL_FCONTEXT is the *first* baseclass in
   which this FIELD_DECL is defined.  This information is needed when
   writing debugging information about vfield and vbase decls for C++.  */
#define DECL_FCONTEXT(NODE) (FIELD_DECL_CHECK (NODE)->field_decl.fcontext)

/* In a FIELD_DECL, indicates this field should be bit-packed.  */
#define DECL_PACKED(NODE) (FIELD_DECL_CHECK (NODE)->decl_common.decl_flag_1)

/* Nonzero in a FIELD_DECL means it is a bit field, and must be accessed
   specially.  */
#define DECL_BIT_FIELD(NODE) (FIELD_DECL_CHECK (NODE)->decl_common.decl_flag_2)

/* Used in a FIELD_DECL to indicate that we cannot form the address of
   this component.  */
#define DECL_NONADDRESSABLE_P(NODE) \
  (FIELD_DECL_CHECK (NODE)->decl_common.decl_flag_3)

struct tree_field_decl GTY(())
{
  struct tree_decl_common common;

  tree offset;
  tree bit_field_type;
  tree qualifier;
  tree bit_offset;
  tree fcontext;

};

/* A numeric unique identifier for a LABEL_DECL.  The UID allocation is
   dense, unique within any one function, and may be used to index arrays.
   If the value is -1, then no UID has been assigned.  */
#define LABEL_DECL_UID(NODE) \
  (LABEL_DECL_CHECK (NODE)->decl_common.pointer_alias_set)

/* In LABEL_DECL nodes, nonzero means that an error message about
   jumping into such a binding contour has been printed for this label.  */
#define DECL_ERROR_ISSUED(NODE) (LABEL_DECL_CHECK (NODE)->decl_common.decl_flag_0)

struct tree_label_decl GTY(())
{
  struct tree_decl_with_rtl common;
  /* Java's verifier has some need to store information about labels,
     and was using fields that no longer exist on labels.
     Once the verifier doesn't need these anymore, they should be removed.  */
  tree java_field_1;
  tree java_field_2;
  tree java_field_3;
  unsigned int java_field_4;

};

struct tree_result_decl GTY(())
{
  struct tree_decl_with_rtl common;
};

struct tree_const_decl GTY(())
{
  struct tree_decl_with_rtl common;
};

/* For a PARM_DECL, records the data type used to pass the argument,
   which may be different from the type seen in the program.  */
#define DECL_ARG_TYPE(NODE) (PARM_DECL_CHECK (NODE)->decl_common.initial)

/* For PARM_DECL, holds an RTL for the stack slot or register
   where the data was actually passed.  */
#define DECL_INCOMING_RTL(NODE) (PARM_DECL_CHECK (NODE)->parm_decl.incoming_rtl)

struct tree_parm_decl GTY(())
{
  struct tree_decl_with_rtl common;
  rtx incoming_rtl;
};


/* Nonzero in a decl means that the gimplifier has seen (or placed)
   this variable in a BIND_EXPR.  */
#define DECL_SEEN_IN_BIND_EXPR_P(NODE) \
  (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.seen_in_bind_expr)

/* Used to indicate that the linkage status of this DECL is not yet known,
   so it should not be output now.  */
#define DECL_DEFER_OUTPUT(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.defer_output)

/* APPLE LOCAL duplicate decls in multiple files. */
#define DECL_DUPLICATE_DECL(NODE) (DECL_COMMON_CHECK (NODE)->decl_common.duplicate_decl)

/* Nonzero for a given ..._DECL node means that no warnings should be
   generated just because this node is unused.  */
#define DECL_IN_SYSTEM_HEADER(NODE) \
  (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.in_system_header_flag)

  /* Used to indicate that this DECL has weak linkage.  */
#define DECL_WEAK(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.weak_flag)

/* Internal to the gimplifier.  Indicates that the value is a formal
   temporary controlled by the gimplifier.  */
#define DECL_GIMPLE_FORMAL_TEMP_P(DECL) \
  DECL_WITH_VIS_CHECK (DECL)->decl_with_vis.gimple_formal_temp

/* Used to indicate that the DECL is a dllimport.  */
#define DECL_DLLIMPORT_P(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.dllimport_flag)

/* DECL_BASED_ON_RESTRICT_P records whether a VAR_DECL is a temporary
   based on a variable with a restrict qualified type.  If it is,
   DECL_RESTRICT_BASE returns the restrict qualified variable on which
   it is based.  */

#define DECL_BASED_ON_RESTRICT_P(NODE) \
  (VAR_DECL_CHECK (NODE)->decl_with_vis.based_on_restrict_p)
#define DECL_GET_RESTRICT_BASE(NODE) \
  (decl_restrict_base_lookup (VAR_DECL_CHECK (NODE)))
#define SET_DECL_RESTRICT_BASE(NODE, VAL) \
  (decl_restrict_base_insert (VAR_DECL_CHECK (NODE), (VAL)))

extern tree decl_restrict_base_lookup (tree);
extern void decl_restrict_base_insert (tree, tree);

/* Used in a DECL to indicate that, even if it TREE_PUBLIC, it need
   not be put out unless it is needed in this translation unit.
   Entities like this are shared across translation units (like weak
   entities), but are guaranteed to be generated by any translation
   unit that needs them, and therefore need not be put out anywhere
   where they are not needed.  DECL_COMDAT is just a hint to the
   back-end; it is up to front-ends which set this flag to ensure
   that there will never be any harm, other than bloat, in putting out
   something which is DECL_COMDAT.  */
#define DECL_COMDAT(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.comdat_flag)

/* A replaceable function is one which may be replaced at link-time
   with an entirely different definition, provided that the
   replacement has the same type.  For example, functions declared
   with __attribute__((weak)) on most systems are replaceable.  

   COMDAT functions are not replaceable, since all definitions of the
   function must be equivalent.  It is important that COMDAT functions
   not be treated as replaceable so that use of C++ template
   instantiations is not penalized.  

   For example, DECL_REPLACEABLE is used to determine whether or not a
   function (including a template instantiation) which is not
   explicitly declared "inline" can be inlined.  If the function is
   DECL_REPLACEABLE then it is not safe to do the inlining, since the
   implementation chosen at link-time may be different.  However, a
   function that is not DECL_REPLACEABLE can be inlined, since all
   versions of the function will be functionally identical.  */
#define DECL_REPLACEABLE_P(NODE) \
  (!DECL_COMDAT (NODE) && !targetm.binds_local_p (NODE))

/* The name of the object as the assembler will see it (but before any
   translations made by ASM_OUTPUT_LABELREF).  Often this is the same
   as DECL_NAME.  It is an IDENTIFIER_NODE.  */
#define DECL_ASSEMBLER_NAME(NODE) decl_assembler_name (NODE)

/* Return true if NODE is a NODE that can contain a DECL_ASSEMBLER_NAME.
   This is true of all DECL nodes except FIELD_DECL.  */
#define HAS_DECL_ASSEMBLER_NAME_P(NODE) \
  (CODE_CONTAINS_STRUCT (TREE_CODE (NODE), TS_DECL_WITH_VIS))

/* Returns nonzero if the DECL_ASSEMBLER_NAME for NODE has been set.  If zero,
   the NODE might still have a DECL_ASSEMBLER_NAME -- it just hasn't been set
   yet.  */
#define DECL_ASSEMBLER_NAME_SET_P(NODE) \
  (HAS_DECL_ASSEMBLER_NAME_P (NODE) &&  DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.assembler_name != NULL_TREE)

/* Set the DECL_ASSEMBLER_NAME for NODE to NAME.  */
#define SET_DECL_ASSEMBLER_NAME(NODE, NAME) \
  (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.assembler_name = (NAME))

/* Copy the DECL_ASSEMBLER_NAME from DECL1 to DECL2.  Note that if DECL1's
   DECL_ASSEMBLER_NAME has not yet been set, using this macro will not cause
   the DECL_ASSEMBLER_NAME of either DECL to be set.  In other words, the
   semantics of using this macro, are different than saying:

     SET_DECL_ASSEMBLER_NAME(DECL2, DECL_ASSEMBLER_NAME (DECL1))

   which will try to set the DECL_ASSEMBLER_NAME for DECL1.  */

#define COPY_DECL_ASSEMBLER_NAME(DECL1, DECL2)				\
  (DECL_ASSEMBLER_NAME_SET_P (DECL1)					\
   ? (void) SET_DECL_ASSEMBLER_NAME (DECL2,				\
				     DECL_ASSEMBLER_NAME (DECL1))	\
   : (void) 0)

/* Records the section name in a section attribute.  Used to pass
   the name from decl_attributes to make_function_rtl and make_decl_rtl.  */
#define DECL_SECTION_NAME(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.section_name)

/* Value of the decls's visibility attribute */
#define DECL_VISIBILITY(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.visibility)

/* Nonzero means that the decl had its visibility specified rather than
   being inferred.  */
#define DECL_VISIBILITY_SPECIFIED(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.visibility_specified)

/* Used in TREE_PUBLIC decls to indicate that copies of this DECL in
   multiple translation units should be merged.  */
#define DECL_ONE_ONLY(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.one_only)

struct tree_decl_with_vis GTY(())
{
 struct tree_decl_with_rtl common;
 tree assembler_name;
 tree section_name;

 /* Belong to VAR_DECL exclusively.  */
 unsigned defer_output:1;
 unsigned hard_register:1;
 unsigned thread_local:1;
 unsigned common_flag:1;
 unsigned in_text_section : 1;
 unsigned gimple_formal_temp : 1;
 unsigned dllimport_flag : 1;
 unsigned based_on_restrict_p : 1;
 /* Used by C++.  Might become a generic decl flag.  */
 unsigned shadowed_for_var_p : 1;

 /* Don't belong to VAR_DECL exclusively.  */
 unsigned in_system_header_flag : 1;
 unsigned weak_flag:1;
 unsigned seen_in_bind_expr : 1;
 unsigned comdat_flag : 1;
 ENUM_BITFIELD(symbol_visibility) visibility : 2;
 unsigned visibility_specified : 1;
 /* Belong to FUNCTION_DECL exclusively.  */
 unsigned one_only : 1;
 unsigned init_priority_p:1;

 /* Belongs to VAR_DECL exclusively.  */
 ENUM_BITFIELD(tls_model) tls_model : 3;

 /* APPLE LOCAL begin radar 5732232 - blocks */
 /* Belong to VAR_DECL exclusively. */
 unsigned block_decl_byref : 1;
 unsigned block_decl_copied : 1;
 /* APPLE LOCAL begin radar 5932809 - copyable byref blocks */
 unsigned copyable_byref_local_var : 1;
 unsigned copyable_byref_local_nonpod : 1;
 /* APPLE LOCAL radar 6172148 */
 unsigned block_synthesized_function : 1;
 /* APPLE LOCAL radar 5847976 */
 unsigned block_weak : 1;
 /* 5 unused bits. */
 /* APPLE LOCAL end radar 5932809 - copyable byref blocks */
 /* APPLE LOCAL end radar 5732232 - blocks */
};

/* In a VAR_DECL that's static,
   nonzero if the space is in the text section.  */
#define DECL_IN_TEXT_SECTION(NODE) (VAR_DECL_CHECK (NODE)->decl_with_vis.in_text_section)

/* Nonzero for a given ..._DECL node means that this node should be
   put in .common, if possible.  If a DECL_INITIAL is given, and it
   is not error_mark_node, then the decl cannot be put in .common.  */
#define DECL_COMMON(NODE) (DECL_WITH_VIS_CHECK (NODE)->decl_with_vis.common_flag)

/* In a VAR_DECL, nonzero if the decl is a register variable with
   an explicit asm specification.  */
#define DECL_HARD_REGISTER(NODE)  (VAR_DECL_CHECK (NODE)->decl_with_vis.hard_register)

extern tree decl_debug_expr_lookup (tree);
extern void decl_debug_expr_insert (tree, tree);
/* For VAR_DECL, this is set to either an expression that it was split
   from (if DECL_DEBUG_EXPR_IS_FROM is true), otherwise a tree_list of
   subexpressions that it was split into.  */
#define DECL_DEBUG_EXPR(NODE) \
  (decl_debug_expr_lookup (VAR_DECL_CHECK (NODE)))

#define SET_DECL_DEBUG_EXPR(NODE, VAL) \
  (decl_debug_expr_insert (VAR_DECL_CHECK (NODE), VAL))


extern unsigned short decl_init_priority_lookup (tree);
extern void decl_init_priority_insert (tree, unsigned short);

/* In a non-local VAR_DECL with static storage duration, this is the
   initialization priority.  If this value is zero, the NODE will be
   initialized at the DEFAULT_INIT_PRIORITY.  Only used by C++ FE*/

#define DECL_HAS_INIT_PRIORITY_P(NODE) \
  (VAR_DECL_CHECK (NODE)->decl_with_vis.init_priority_p)
#define DECL_INIT_PRIORITY(NODE) \
  (decl_init_priority_lookup (VAR_DECL_CHECK (NODE)))
#define SET_DECL_INIT_PRIORITY(NODE, VAL) \
  (decl_init_priority_insert (VAR_DECL_CHECK (NODE), VAL))

/* In a VAR_DECL, the model to use if the data should be allocated from
   thread-local storage.  */
#define DECL_TLS_MODEL(NODE) (VAR_DECL_CHECK (NODE)->decl_with_vis.tls_model)

/* APPLE LOCAL begin radar 5732232 - blocks */
#define BLOCK_DECL_BYREF(NODE) (VAR_DECL_CHECK (NODE)->decl_with_vis.block_decl_byref)
#define BLOCK_DECL_COPIED(NODE) (VAR_DECL_CHECK (NODE)->decl_with_vis.block_decl_copied)
/* APPLE LOCAL end radar 5732232 - blocks */
/* APPLE LOCAL radar 6172148 */
#define BLOCK_SYNTHESIZED_FUNC(NODE) (FUNCTION_DECL_CHECK (NODE)->decl_with_vis.block_synthesized_function)
/* APPLE LOCAL begin radar 5932809 - copyable byref blocks */
#define COPYABLE_BYREF_LOCAL_VAR(NODE) (VAR_DECL_CHECK (NODE)->decl_with_vis.copyable_byref_local_var)
#define COPYABLE_BYREF_LOCAL_NONPOD(NODE) (VAR_DECL_CHECK (NODE)->decl_with_vis.copyable_byref_local_nonpod)
/* APPLE LOCAL radar 5847976 */
#define COPYABLE_WEAK_BLOCK(NODE) (VAR_DECL_CHECK (NODE)->decl_with_vis.block_weak)
/* APPLE LOCAL end radar 5932809 - copyable byref blocks */

/* In a VAR_DECL, nonzero if the data should be allocated from
   thread-local storage.  */
#define DECL_THREAD_LOCAL_P(NODE) \
  (VAR_DECL_CHECK (NODE)->decl_with_vis.tls_model != TLS_MODEL_NONE)

struct tree_var_decl GTY(())
{
  struct tree_decl_with_vis common;
};


/* This field is used to reference anything in decl.result and is meant only
   for use by the garbage collector.  */
#define DECL_RESULT_FLD(NODE) (DECL_NON_COMMON_CHECK (NODE)->decl_non_common.result)

/* The DECL_VINDEX is used for FUNCTION_DECLS in two different ways.
   Before the struct containing the FUNCTION_DECL is laid out,
   DECL_VINDEX may point to a FUNCTION_DECL in a base class which
   is the FUNCTION_DECL which this FUNCTION_DECL will replace as a virtual
   function.  When the class is laid out, this pointer is changed
   to an INTEGER_CST node which is suitable for use as an index
   into the virtual function table.
   C++ also uses this field in namespaces, hence the DECL_NON_COMMON_CHECK.  */
#define DECL_VINDEX(NODE) (DECL_NON_COMMON_CHECK (NODE)->decl_non_common.vindex)

struct tree_decl_non_common GTY(())

{
  struct tree_decl_with_vis common;
  /* C++ uses this in namespaces.  */
  tree saved_tree;
  /* C++ uses this in templates.  */
  tree arguments;
  /* Almost all FE's use this.  */
  tree result;
  /* C++ uses this in namespaces.  */
  tree vindex;
};

/* In FUNCTION_DECL, holds the decl for the return value.  */
#define DECL_RESULT(NODE) (FUNCTION_DECL_CHECK (NODE)->decl_non_common.result)

/* In a FUNCTION_DECL, nonzero if the function cannot be inlined.  */
#define DECL_UNINLINABLE(NODE) (FUNCTION_DECL_CHECK (NODE)->function_decl.uninlinable)

/* In a FUNCTION_DECL, the saved representation of the body of the
   entire function.  */
#define DECL_SAVED_TREE(NODE) (FUNCTION_DECL_CHECK (NODE)->decl_non_common.saved_tree)

/* Nonzero in a FUNCTION_DECL means this function should be treated
   as if it were a malloc, meaning it returns a pointer that is
   not an alias.  */
#define DECL_IS_MALLOC(NODE) (FUNCTION_DECL_CHECK (NODE)->function_decl.malloc_flag)

/* Nonzero in a FUNCTION_DECL means this function may return more
   than once.  */
#define DECL_IS_RETURNS_TWICE(NODE) \
  (FUNCTION_DECL_CHECK (NODE)->function_decl.returns_twice_flag)

/* Nonzero in a FUNCTION_DECL means this function should be treated
   as "pure" function (like const function, but may read global memory).  */
#define DECL_IS_PURE(NODE) (FUNCTION_DECL_CHECK (NODE)->function_decl.pure_flag)

/* Nonzero in a FUNCTION_DECL means this function should be treated
   as "novops" function (function that does not read global memory,
   but may have arbitrary side effects).  */
#define DECL_IS_NOVOPS(NODE) (FUNCTION_DECL_CHECK (NODE)->function_decl.novops_flag)

/* Used in FUNCTION_DECLs to indicate that they should be run automatically
   at the beginning or end of execution.  */
#define DECL_STATIC_CONSTRUCTOR(NODE) \
  (FUNCTION_DECL_CHECK (NODE)->function_decl.static_ctor_flag)

#define DECL_STATIC_DESTRUCTOR(NODE) \
(FUNCTION_DECL_CHECK (NODE)->function_decl.static_dtor_flag)

/* Used in FUNCTION_DECLs to indicate that function entry and exit should
   be instrumented with calls to support routines.  */
#define DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT(NODE) \
  (FUNCTION_DECL_CHECK (NODE)->function_decl.no_instrument_function_entry_exit)

/* Used in FUNCTION_DECLs to indicate that limit-stack-* should be
   disabled in this function.  */
#define DECL_NO_LIMIT_STACK(NODE) \
  (FUNCTION_DECL_CHECK (NODE)->function_decl.no_limit_stack)

/* In a FUNCTION_DECL with a nonzero DECL_CONTEXT, indicates that a
   static chain is not needed.  */
#define DECL_NO_STATIC_CHAIN(NODE) \
  (FUNCTION_DECL_CHECK (NODE)->function_decl.regdecl_flag)

/* Nonzero for a decl that cgraph has decided should be inlined into
   at least one call site.  It is not meaningful to look at this
   directly; always use cgraph_function_possibly_inlined_p.  */
#define DECL_POSSIBLY_INLINED(DECL) \
  FUNCTION_DECL_CHECK (DECL)->function_decl.possibly_inlined

/* Nonzero in a FUNCTION_DECL means this function can be substituted
   where it is called.  */
#define DECL_INLINE(NODE) (FUNCTION_DECL_CHECK (NODE)->function_decl.inline_flag)

/* Nonzero in a FUNCTION_DECL means that this function was declared inline,
   such as via the `inline' keyword in C/C++.  This flag controls the linkage
   semantics of 'inline'; whether or not the function is inlined is
   controlled by DECL_INLINE.  */
#define DECL_DECLARED_INLINE_P(NODE) \
  (FUNCTION_DECL_CHECK (NODE)->function_decl.declared_inline_flag)

/* For FUNCTION_DECL, this holds a pointer to a structure ("struct function")
   that describes the status of this function.  */
#define DECL_STRUCT_FUNCTION(NODE) (FUNCTION_DECL_CHECK (NODE)->function_decl.f)

/* In a FUNCTION_DECL, nonzero means a built in function.  */
#define DECL_BUILT_IN(NODE) (DECL_BUILT_IN_CLASS (NODE) != NOT_BUILT_IN)

/* For a builtin function, identify which part of the compiler defined it.  */
#define DECL_BUILT_IN_CLASS(NODE) \
   (FUNCTION_DECL_CHECK (NODE)->function_decl.built_in_class)

/* In FUNCTION_DECL, a chain of ..._DECL nodes.
   VAR_DECL and PARM_DECL reserve the arguments slot for language-specific
   uses.  */
#define DECL_ARGUMENTS(NODE) (FUNCTION_DECL_CHECK (NODE)->decl_non_common.arguments)
#define DECL_ARGUMENT_FLD(NODE) (DECL_NON_COMMON_CHECK (NODE)->decl_non_common.arguments)

/* FUNCTION_DECL inherits from DECL_NON_COMMON because of the use of the
   arguments/result/saved_tree fields by front ends.   It was either inherit
   FUNCTION_DECL from non_common, or inherit non_common from FUNCTION_DECL,
   which seemed a bit strange.  */

struct tree_function_decl GTY(())
{
  struct tree_decl_non_common common;

  /* In a FUNCTION_DECL for which DECL_BUILT_IN holds, this is
     DECL_FUNCTION_CODE.  Otherwise unused.  */
  enum built_in_function function_code;

  unsigned static_ctor_flag : 1;
  unsigned static_dtor_flag : 1;
  unsigned uninlinable : 1;
  unsigned possibly_inlined : 1;
  unsigned novops_flag : 1;
  unsigned returns_twice_flag : 1;
  unsigned malloc_flag : 1;
  unsigned pure_flag : 1;

  unsigned declared_inline_flag : 1;
  unsigned regdecl_flag : 1;
  unsigned inline_flag : 1;
  unsigned no_instrument_function_entry_exit : 1;
  unsigned no_limit_stack : 1;
  ENUM_BITFIELD(built_in_class) built_in_class : 2;

  struct function *f;
};

/* For a TYPE_DECL, holds the "original" type.  (TREE_TYPE has the copy.) */
#define DECL_ORIGINAL_TYPE(NODE) (TYPE_DECL_CHECK (NODE)->decl_non_common.result)

/* In a TYPE_DECL nonzero means the detail info about this type is not dumped
   into stabs.  Instead it will generate cross reference ('x') of names.
   This uses the same flag as DECL_EXTERNAL.  */
#define TYPE_DECL_SUPPRESS_DEBUG(NODE) \
  (TYPE_DECL_CHECK (NODE)->decl_common.decl_flag_2)

struct tree_type_decl GTY(())
{
  struct tree_decl_non_common common;

};

/* A STATEMENT_LIST chains statements together in GENERIC and GIMPLE.
   To reduce overhead, the nodes containing the statements are not trees.
   This avoids the overhead of tree_common on all linked list elements.

   Use the interface in tree-iterator.h to access this node.  */

#define STATEMENT_LIST_HEAD(NODE) \
  (STATEMENT_LIST_CHECK (NODE)->stmt_list.head)
#define STATEMENT_LIST_TAIL(NODE) \
  (STATEMENT_LIST_CHECK (NODE)->stmt_list.tail)

struct tree_statement_list_node
  GTY ((chain_next ("%h.next"), chain_prev ("%h.prev")))
{
  struct tree_statement_list_node *prev;
  struct tree_statement_list_node *next;
  tree stmt;
};

struct tree_statement_list
  GTY(())
{
  struct tree_common common;
  struct tree_statement_list_node *head;
  struct tree_statement_list_node *tail;
};

#define VALUE_HANDLE_ID(NODE)		\
  (VALUE_HANDLE_CHECK (NODE)->value_handle.id)

#define VALUE_HANDLE_EXPR_SET(NODE)	\
  (VALUE_HANDLE_CHECK (NODE)->value_handle.expr_set)

#define VALUE_HANDLE_VUSES(NODE)        \
  (VALUE_HANDLE_CHECK (NODE)->value_handle.vuses)

/* Defined and used in tree-ssa-pre.c.  */
struct value_set;

struct tree_value_handle GTY(())
{
  struct tree_common common;

  /* The set of expressions represented by this handle.  */
  struct value_set * GTY ((skip)) expr_set;

  /* Unique ID for this value handle.  IDs are handed out in a
     conveniently dense form starting at 0, so that we can make
     bitmaps of value handles.  */
  unsigned int id;

  /* Set of virtual uses represented by this handle.  */
  VEC (tree, gc) *vuses;
};

/* Define the overall contents of a tree node.
   It may be any of the structures declared above
   for various types of node.  */

union tree_node GTY ((ptr_alias (union lang_tree_node),
		      desc ("tree_node_structure (&%h)")))
{
  struct tree_common GTY ((tag ("TS_COMMON"))) common;
  struct tree_int_cst GTY ((tag ("TS_INT_CST"))) int_cst;
  struct tree_real_cst GTY ((tag ("TS_REAL_CST"))) real_cst;
  struct tree_vector GTY ((tag ("TS_VECTOR"))) vector;
  struct tree_string GTY ((tag ("TS_STRING"))) string;
  struct tree_complex GTY ((tag ("TS_COMPLEX"))) complex;
  struct tree_identifier GTY ((tag ("TS_IDENTIFIER"))) identifier;
  struct tree_decl_minimal GTY((tag ("TS_DECL_MINIMAL"))) decl_minimal;
  struct tree_decl_common GTY ((tag ("TS_DECL_COMMON"))) decl_common;
  struct tree_decl_with_rtl GTY ((tag ("TS_DECL_WRTL"))) decl_with_rtl;
  struct tree_decl_non_common  GTY ((tag ("TS_DECL_NON_COMMON"))) decl_non_common;
  struct tree_parm_decl  GTY  ((tag ("TS_PARM_DECL"))) parm_decl;
  struct tree_decl_with_vis GTY ((tag ("TS_DECL_WITH_VIS"))) decl_with_vis;
  struct tree_var_decl GTY ((tag ("TS_VAR_DECL"))) var_decl;
  struct tree_field_decl GTY ((tag ("TS_FIELD_DECL"))) field_decl;
  struct tree_label_decl GTY ((tag ("TS_LABEL_DECL"))) label_decl;
  struct tree_result_decl GTY ((tag ("TS_RESULT_DECL"))) result_decl;
  struct tree_const_decl GTY ((tag ("TS_CONST_DECL"))) const_decl;
  struct tree_type_decl GTY ((tag ("TS_TYPE_DECL"))) type_decl;
  struct tree_function_decl GTY ((tag ("TS_FUNCTION_DECL"))) function_decl;
  struct tree_type GTY ((tag ("TS_TYPE"))) type;
  struct tree_list GTY ((tag ("TS_LIST"))) list;
  struct tree_vec GTY ((tag ("TS_VEC"))) vec;
  struct tree_exp GTY ((tag ("TS_EXP"))) exp;
  struct tree_ssa_name GTY ((tag ("TS_SSA_NAME"))) ssa_name;
  struct tree_phi_node GTY ((tag ("TS_PHI_NODE"))) phi;
  struct tree_block GTY ((tag ("TS_BLOCK"))) block;
  struct tree_binfo GTY ((tag ("TS_BINFO"))) binfo;
  struct tree_statement_list GTY ((tag ("TS_STATEMENT_LIST"))) stmt_list;
  struct tree_value_handle GTY ((tag ("TS_VALUE_HANDLE"))) value_handle;
  struct tree_constructor GTY ((tag ("TS_CONSTRUCTOR"))) constructor;
  struct tree_memory_tag GTY ((tag ("TS_MEMORY_TAG"))) mtag;
  struct tree_struct_field_tag GTY ((tag ("TS_STRUCT_FIELD_TAG"))) sft;
  struct tree_omp_clause GTY ((tag ("TS_OMP_CLAUSE"))) omp_clause;
};

/* Standard named or nameless data types of the C compiler.  */

enum tree_index
{
  TI_ERROR_MARK,
  TI_INTQI_TYPE,
  TI_INTHI_TYPE,
  TI_INTSI_TYPE,
  TI_INTDI_TYPE,
  TI_INTTI_TYPE,

  TI_UINTQI_TYPE,
  TI_UINTHI_TYPE,
  TI_UINTSI_TYPE,
  TI_UINTDI_TYPE,
  TI_UINTTI_TYPE,

  TI_UINT32_TYPE,
  TI_UINT64_TYPE,

  TI_INTEGER_ZERO,
  TI_INTEGER_ONE,
  TI_INTEGER_MINUS_ONE,
  TI_NULL_POINTER,

  TI_SIZE_ZERO,
  TI_SIZE_ONE,

  TI_BITSIZE_ZERO,
  TI_BITSIZE_ONE,
  TI_BITSIZE_UNIT,

  TI_PUBLIC,
  TI_PROTECTED,
  TI_PRIVATE,

  TI_BOOLEAN_FALSE,
  TI_BOOLEAN_TRUE,

  TI_COMPLEX_INTEGER_TYPE,
  TI_COMPLEX_FLOAT_TYPE,
  TI_COMPLEX_DOUBLE_TYPE,
  TI_COMPLEX_LONG_DOUBLE_TYPE,

  TI_FLOAT_TYPE,
  TI_DOUBLE_TYPE,
  TI_LONG_DOUBLE_TYPE,

  TI_FLOAT_PTR_TYPE,
  TI_DOUBLE_PTR_TYPE,
  TI_LONG_DOUBLE_PTR_TYPE,
  TI_INTEGER_PTR_TYPE,

  TI_VOID_TYPE,
  TI_PTR_TYPE,
  TI_CONST_PTR_TYPE,
  TI_SIZE_TYPE,
  TI_PID_TYPE,
  TI_PTRDIFF_TYPE,
  TI_VA_LIST_TYPE,
  TI_VA_LIST_GPR_COUNTER_FIELD,
  TI_VA_LIST_FPR_COUNTER_FIELD,
  TI_BOOLEAN_TYPE,
  TI_FILEPTR_TYPE,

  TI_DFLOAT32_TYPE,
  TI_DFLOAT64_TYPE,
  TI_DFLOAT128_TYPE,
  TI_DFLOAT32_PTR_TYPE,
  TI_DFLOAT64_PTR_TYPE,
  TI_DFLOAT128_PTR_TYPE,

  TI_VOID_LIST_NODE,

  TI_MAIN_IDENTIFIER,

  TI_MAX
};

extern GTY(()) tree global_trees[TI_MAX];

#define error_mark_node			global_trees[TI_ERROR_MARK]

#define intQI_type_node			global_trees[TI_INTQI_TYPE]
#define intHI_type_node			global_trees[TI_INTHI_TYPE]
#define intSI_type_node			global_trees[TI_INTSI_TYPE]
#define intDI_type_node			global_trees[TI_INTDI_TYPE]
#define intTI_type_node			global_trees[TI_INTTI_TYPE]

#define unsigned_intQI_type_node	global_trees[TI_UINTQI_TYPE]
#define unsigned_intHI_type_node	global_trees[TI_UINTHI_TYPE]
#define unsigned_intSI_type_node	global_trees[TI_UINTSI_TYPE]
#define unsigned_intDI_type_node	global_trees[TI_UINTDI_TYPE]
#define unsigned_intTI_type_node	global_trees[TI_UINTTI_TYPE]

#define uint32_type_node		global_trees[TI_UINT32_TYPE]
#define uint64_type_node		global_trees[TI_UINT64_TYPE]

#define integer_zero_node		global_trees[TI_INTEGER_ZERO]
#define integer_one_node		global_trees[TI_INTEGER_ONE]
#define integer_minus_one_node		global_trees[TI_INTEGER_MINUS_ONE]
#define size_zero_node			global_trees[TI_SIZE_ZERO]
#define size_one_node			global_trees[TI_SIZE_ONE]
#define bitsize_zero_node		global_trees[TI_BITSIZE_ZERO]
#define bitsize_one_node		global_trees[TI_BITSIZE_ONE]
#define bitsize_unit_node		global_trees[TI_BITSIZE_UNIT]

/* Base access nodes.  */
#define access_public_node		global_trees[TI_PUBLIC]
#define access_protected_node	        global_trees[TI_PROTECTED]
#define access_private_node		global_trees[TI_PRIVATE]

#define null_pointer_node		global_trees[TI_NULL_POINTER]

#define float_type_node			global_trees[TI_FLOAT_TYPE]
#define double_type_node		global_trees[TI_DOUBLE_TYPE]
#define long_double_type_node		global_trees[TI_LONG_DOUBLE_TYPE]

#define float_ptr_type_node		global_trees[TI_FLOAT_PTR_TYPE]
#define double_ptr_type_node		global_trees[TI_DOUBLE_PTR_TYPE]
#define long_double_ptr_type_node	global_trees[TI_LONG_DOUBLE_PTR_TYPE]
#define integer_ptr_type_node		global_trees[TI_INTEGER_PTR_TYPE]

#define complex_integer_type_node	global_trees[TI_COMPLEX_INTEGER_TYPE]
#define complex_float_type_node		global_trees[TI_COMPLEX_FLOAT_TYPE]
#define complex_double_type_node	global_trees[TI_COMPLEX_DOUBLE_TYPE]
#define complex_long_double_type_node	global_trees[TI_COMPLEX_LONG_DOUBLE_TYPE]

#define void_type_node			global_trees[TI_VOID_TYPE]
/* The C type `void *'.  */
#define ptr_type_node			global_trees[TI_PTR_TYPE]
/* The C type `const void *'.  */
#define const_ptr_type_node		global_trees[TI_CONST_PTR_TYPE]
/* The C type `size_t'.  */
#define size_type_node                  global_trees[TI_SIZE_TYPE]
#define pid_type_node                   global_trees[TI_PID_TYPE]
#define ptrdiff_type_node		global_trees[TI_PTRDIFF_TYPE]
#define va_list_type_node		global_trees[TI_VA_LIST_TYPE]
#define va_list_gpr_counter_field	global_trees[TI_VA_LIST_GPR_COUNTER_FIELD]
#define va_list_fpr_counter_field	global_trees[TI_VA_LIST_FPR_COUNTER_FIELD]
/* The C type `FILE *'.  */
#define fileptr_type_node		global_trees[TI_FILEPTR_TYPE]

#define boolean_type_node		global_trees[TI_BOOLEAN_TYPE]
#define boolean_false_node		global_trees[TI_BOOLEAN_FALSE]
#define boolean_true_node		global_trees[TI_BOOLEAN_TRUE]

/* The decimal floating point types. */
#define dfloat32_type_node              global_trees[TI_DFLOAT32_TYPE]
#define dfloat64_type_node              global_trees[TI_DFLOAT64_TYPE]
#define dfloat128_type_node             global_trees[TI_DFLOAT128_TYPE]
#define dfloat32_ptr_type_node          global_trees[TI_DFLOAT32_PTR_TYPE]
#define dfloat64_ptr_type_node          global_trees[TI_DFLOAT64_PTR_TYPE]
#define dfloat128_ptr_type_node         global_trees[TI_DFLOAT128_PTR_TYPE]

/* The node that should be placed at the end of a parameter list to
   indicate that the function does not take a variable number of
   arguments.  The TREE_VALUE will be void_type_node and there will be
   no TREE_CHAIN.  Language-independent code should not assume
   anything else about this node.  */
#define void_list_node                  global_trees[TI_VOID_LIST_NODE]

#define main_identifier_node		global_trees[TI_MAIN_IDENTIFIER]
#define MAIN_NAME_P(NODE) (IDENTIFIER_NODE_CHECK (NODE) == main_identifier_node)

/* An enumeration of the standard C integer types.  These must be
   ordered so that shorter types appear before longer ones, and so
   that signed types appear before unsigned ones, for the correct
   functioning of interpret_integer() in c-lex.c.  */
enum integer_type_kind
{
  itk_char,
  itk_signed_char,
  itk_unsigned_char,
  itk_short,
  itk_unsigned_short,
  itk_int,
  itk_unsigned_int,
  itk_long,
  itk_unsigned_long,
  itk_long_long,
  itk_unsigned_long_long,
  itk_none
};

typedef enum integer_type_kind integer_type_kind;

/* The standard C integer types.  Use integer_type_kind to index into
   this array.  */
extern GTY(()) tree integer_types[itk_none];

#define char_type_node			integer_types[itk_char]
#define signed_char_type_node		integer_types[itk_signed_char]
#define unsigned_char_type_node		integer_types[itk_unsigned_char]
#define short_integer_type_node		integer_types[itk_short]
#define short_unsigned_type_node	integer_types[itk_unsigned_short]
#define integer_type_node		integer_types[itk_int]
#define unsigned_type_node		integer_types[itk_unsigned_int]
#define long_integer_type_node		integer_types[itk_long]
#define long_unsigned_type_node		integer_types[itk_unsigned_long]
#define long_long_integer_type_node	integer_types[itk_long_long]
#define long_long_unsigned_type_node	integer_types[itk_unsigned_long_long]

/* Set to the default thread-local storage (tls) model to use.  */

extern enum tls_model flag_tls_default;


/* A pointer-to-function member type looks like:

     struct {
       __P __pfn;
       ptrdiff_t __delta;
     };

   If __pfn is NULL, it is a NULL pointer-to-member-function.

   (Because the vtable is always the first thing in the object, we
   don't need its offset.)  If the function is virtual, then PFN is
   one plus twice the index into the vtable; otherwise, it is just a
   pointer to the function.

   Unfortunately, using the lowest bit of PFN doesn't work in
   architectures that don't impose alignment requirements on function
   addresses, or that use the lowest bit to tell one ISA from another,
   for example.  For such architectures, we use the lowest bit of
   DELTA instead of the lowest bit of the PFN, and DELTA will be
   multiplied by 2.  */

enum ptrmemfunc_vbit_where_t
{
  ptrmemfunc_vbit_in_pfn,
  ptrmemfunc_vbit_in_delta
};

#define NULL_TREE (tree) NULL

extern tree decl_assembler_name (tree);

/* Compute the number of bytes occupied by 'node'.  This routine only
   looks at TREE_CODE and, if the code is TREE_VEC, TREE_VEC_LENGTH.  */

extern size_t tree_size (tree);

/* Compute the number of bytes occupied by a tree with code CODE.  This
   function cannot be used for TREE_VEC or PHI_NODE codes, which are of
   variable length.  */
extern size_t tree_code_size (enum tree_code);

/* Lowest level primitive for allocating a node.
   The TREE_CODE is the only argument.  Contents are initialized
   to zero except for a few of the common fields.  */

extern tree make_node_stat (enum tree_code MEM_STAT_DECL);
#define make_node(t) make_node_stat (t MEM_STAT_INFO)

/* Make a copy of a node, with all the same contents.  */

extern tree copy_node_stat (tree MEM_STAT_DECL);
#define copy_node(t) copy_node_stat (t MEM_STAT_INFO)

/* Make a copy of a chain of TREE_LIST nodes.  */

extern tree copy_list (tree);

/* Make a BINFO.  */
extern tree make_tree_binfo_stat (unsigned MEM_STAT_DECL);
#define make_tree_binfo(t) make_tree_binfo_stat (t MEM_STAT_INFO)

/* Make a TREE_VEC.  */

extern tree make_tree_vec_stat (int MEM_STAT_DECL);
#define make_tree_vec(t) make_tree_vec_stat (t MEM_STAT_INFO)

/* Tree nodes for SSA analysis.  */

extern void init_phinodes (void);
extern void fini_phinodes (void);
extern void release_phi_node (tree);
#ifdef GATHER_STATISTICS
extern void phinodes_print_statistics (void);
#endif

extern void init_ssanames (void);
extern void fini_ssanames (void);
extern tree make_ssa_name (tree, tree);
extern tree duplicate_ssa_name (tree, tree);
extern void duplicate_ssa_name_ptr_info (tree, struct ptr_info_def *);
extern void release_ssa_name (tree);
extern void release_defs (tree);
extern void replace_ssa_name_symbol (tree, tree);

#ifdef GATHER_STATISTICS
extern void ssanames_print_statistics (void);
#endif

/* Return the (unique) IDENTIFIER_NODE node for a given name.
   The name is supplied as a char *.  */

extern tree get_identifier (const char *);

#if GCC_VERSION >= 3000
#define get_identifier(str) \
  (__builtin_constant_p (str)				\
    ? get_identifier_with_length ((str), strlen (str))  \
    : get_identifier (str))
#endif


/* Identical to get_identifier, except that the length is assumed
   known.  */

extern tree get_identifier_with_length (const char *, size_t);

/* If an identifier with the name TEXT (a null-terminated string) has
   previously been referred to, return that node; otherwise return
   NULL_TREE.  */

extern tree maybe_get_identifier (const char *);

/* Construct various types of nodes.  */

extern tree build_nt (enum tree_code, ...);

extern tree build0_stat (enum tree_code, tree MEM_STAT_DECL);
#define build0(c,t) build0_stat (c,t MEM_STAT_INFO)
extern tree build1_stat (enum tree_code, tree, tree MEM_STAT_DECL);
#define build1(c,t1,t2) build1_stat (c,t1,t2 MEM_STAT_INFO)
extern tree build2_stat (enum tree_code, tree, tree, tree MEM_STAT_DECL);
#define build2(c,t1,t2,t3) build2_stat (c,t1,t2,t3 MEM_STAT_INFO)
extern tree build3_stat (enum tree_code, tree, tree, tree, tree MEM_STAT_DECL);
#define build3(c,t1,t2,t3,t4) build3_stat (c,t1,t2,t3,t4 MEM_STAT_INFO)
extern tree build4_stat (enum tree_code, tree, tree, tree, tree,
			 tree MEM_STAT_DECL);
#define build4(c,t1,t2,t3,t4,t5) build4_stat (c,t1,t2,t3,t4,t5 MEM_STAT_INFO)
extern tree build5_stat (enum tree_code, tree, tree, tree, tree, tree,
			 tree MEM_STAT_DECL);
#define build5(c,t1,t2,t3,t4,t5,t6) build5_stat (c,t1,t2,t3,t4,t5,t6 MEM_STAT_INFO)
extern tree build7_stat (enum tree_code, tree, tree, tree, tree, tree,
			 tree, tree, tree MEM_STAT_DECL);
#define build7(c,t1,t2,t3,t4,t5,t6,t7,t8) \
  build7_stat (c,t1,t2,t3,t4,t5,t6,t7,t8 MEM_STAT_INFO)

extern tree build_int_cst (tree, HOST_WIDE_INT);
extern tree build_int_cst_type (tree, HOST_WIDE_INT);
extern tree build_int_cstu (tree, unsigned HOST_WIDE_INT);
extern tree build_int_cst_wide (tree, unsigned HOST_WIDE_INT, HOST_WIDE_INT);
extern tree build_vector (tree, tree);
extern tree build_vector_from_ctor (tree, VEC(constructor_elt,gc) *);
extern tree build_constructor (tree, VEC(constructor_elt,gc) *);
extern tree build_constructor_single (tree, tree, tree);
extern tree build_constructor_from_list (tree, tree);
extern tree build_real_from_int_cst (tree, tree);
extern tree build_complex (tree, tree, tree);
extern tree build_one_cst (tree);
extern tree build_string (int, const char *);
extern tree build_tree_list_stat (tree, tree MEM_STAT_DECL);
#define build_tree_list(t,q) build_tree_list_stat(t,q MEM_STAT_INFO)
extern tree build_decl_stat (enum tree_code, tree, tree MEM_STAT_DECL);
extern tree build_fn_decl (const char *, tree);
#define build_decl(c,t,q) build_decl_stat (c,t,q MEM_STAT_INFO)
extern tree build_block (tree, tree, tree, tree);
#ifndef USE_MAPPED_LOCATION
extern void annotate_with_file_line (tree, const char *, int);
extern void annotate_with_locus (tree, location_t);
#endif
extern tree build_empty_stmt (void);
extern tree build_omp_clause (enum omp_clause_code);

/* Construct various nodes representing data types.  */

extern tree make_signed_type (int);
extern tree make_unsigned_type (int);
extern tree signed_type_for (tree);
extern tree unsigned_type_for (tree);
extern void initialize_sizetypes (bool);
extern void set_sizetype (tree);
extern void fixup_unsigned_type (tree);
extern tree build_pointer_type_for_mode (tree, enum machine_mode, bool);
extern tree build_pointer_type (tree);
/* APPLE LOCAL radar 5732232 - blocks */
extern tree build_block_pointer_type (tree);
extern tree build_reference_type_for_mode (tree, enum machine_mode, bool);
extern tree build_reference_type (tree);
extern tree build_vector_type_for_mode (tree, enum machine_mode);
extern tree build_vector_type (tree innertype, int nunits);
extern tree build_type_no_quals (tree);
extern tree build_index_type (tree);
extern tree build_index_2_type (tree, tree);
extern tree build_array_type (tree, tree);
extern tree build_function_type (tree, tree);
extern tree build_function_type_list (tree, ...);
extern tree build_method_type_directly (tree, tree, tree);
extern tree build_method_type (tree, tree);
extern tree build_offset_type (tree, tree);
extern tree build_complex_type (tree);
extern tree build_resx (int);
extern tree array_type_nelts (tree);
extern bool in_array_bounds_p (tree);
extern bool range_in_array_bounds_p (tree);

extern tree value_member (tree, tree);
extern tree purpose_member (tree, tree);

extern int attribute_list_equal (tree, tree);
extern int attribute_list_contained (tree, tree);
extern int tree_int_cst_equal (tree, tree);
extern int tree_int_cst_lt (tree, tree);
extern int tree_int_cst_compare (tree, tree);
extern int host_integerp (tree, int);
extern HOST_WIDE_INT tree_low_cst (tree, int);
extern int tree_int_cst_msb (tree);
extern int tree_int_cst_sgn (tree);
extern int tree_int_cst_sign_bit (tree);
extern int tree_expr_nonnegative_p (tree);
extern int tree_expr_nonnegative_warnv_p (tree, bool *);
extern bool may_negate_without_overflow_p (tree);
extern tree get_inner_array_type (tree);

/* From expmed.c.  Since rtl.h is included after tree.h, we can't
   put the prototype here.  Rtl.h does declare the prototype if
   tree.h had been included.  */

extern tree make_tree (tree, rtx);

/* Return a type like TTYPE except that its TYPE_ATTRIBUTES
   is ATTRIBUTE.

   Such modified types already made are recorded so that duplicates
   are not made.  */

extern tree build_type_attribute_variant (tree, tree);
extern tree build_decl_attribute_variant (tree, tree);

/* Structure describing an attribute and a function to handle it.  */
struct attribute_spec
{
  /* The name of the attribute (without any leading or trailing __),
     or NULL to mark the end of a table of attributes.  */
  const char *const name;
  /* The minimum length of the list of arguments of the attribute.  */
  const int min_length;
  /* The maximum length of the list of arguments of the attribute
     (-1 for no maximum).  */
  const int max_length;
  /* Whether this attribute requires a DECL.  If it does, it will be passed
     from types of DECLs, function return types and array element types to
     the DECLs, function types and array types respectively; but when
     applied to a type in any other circumstances, it will be ignored with
     a warning.  (If greater control is desired for a given attribute,
     this should be false, and the flags argument to the handler may be
     used to gain greater control in that case.)  */
  const bool decl_required;
  /* Whether this attribute requires a type.  If it does, it will be passed
     from a DECL to the type of that DECL.  */
  const bool type_required;
  /* Whether this attribute requires a function (or method) type.  If it does,
     it will be passed from a function pointer type to the target type,
     and from a function return type (which is not itself a function
     pointer type) to the function type.  */
  const bool function_type_required;
  /* Function to handle this attribute.  NODE points to the node to which
     the attribute is to be applied.  If a DECL, it should be modified in
     place; if a TYPE, a copy should be created.  NAME is the name of the
     attribute (possibly with leading or trailing __).  ARGS is the TREE_LIST
     of the arguments (which may be NULL).  FLAGS gives further information
     about the context of the attribute.  Afterwards, the attributes will
     be added to the DECL_ATTRIBUTES or TYPE_ATTRIBUTES, as appropriate,
     unless *NO_ADD_ATTRS is set to true (which should be done on error,
     as well as in any other cases when the attributes should not be added
     to the DECL or TYPE).  Depending on FLAGS, any attributes to be
     applied to another type or DECL later may be returned;
     otherwise the return value should be NULL_TREE.  This pointer may be
     NULL if no special handling is required beyond the checks implied
     by the rest of this structure.  */
  tree (*const handler) (tree *node, tree name, tree args,
				 int flags, bool *no_add_attrs);
};

/* Flags that may be passed in the third argument of decl_attributes, and
   to handler functions for attributes.  */
enum attribute_flags
{
  /* The type passed in is the type of a DECL, and any attributes that
     should be passed in again to be applied to the DECL rather than the
     type should be returned.  */
  ATTR_FLAG_DECL_NEXT = 1,
  /* The type passed in is a function return type, and any attributes that
     should be passed in again to be applied to the function type rather
     than the return type should be returned.  */
  ATTR_FLAG_FUNCTION_NEXT = 2,
  /* The type passed in is an array element type, and any attributes that
     should be passed in again to be applied to the array type rather
     than the element type should be returned.  */
  ATTR_FLAG_ARRAY_NEXT = 4,
  /* The type passed in is a structure, union or enumeration type being
     created, and should be modified in place.  */
  ATTR_FLAG_TYPE_IN_PLACE = 8,
  /* The attributes are being applied by default to a library function whose
     name indicates known behavior, and should be silently ignored if they
     are not in fact compatible with the function type.  */
  ATTR_FLAG_BUILT_IN = 16
};

/* Default versions of target-overridable functions.  */

extern tree merge_decl_attributes (tree, tree);
extern tree merge_type_attributes (tree, tree);

/* Given a tree node and a string, return nonzero if the tree node is
   a valid attribute name for the string.  */

extern int is_attribute_p (const char *, tree);

/* Given an attribute name and a list of attributes, return the list element
   of the attribute or NULL_TREE if not found.  */

extern tree lookup_attribute (const char *, tree);

/* Remove any instances of attribute ATTR_NAME in LIST and return the
   modified list.  */

extern tree remove_attribute (const char *, tree);

/* Given two attributes lists, return a list of their union.  */

extern tree merge_attributes (tree, tree);

#if TARGET_DLLIMPORT_DECL_ATTRIBUTES
/* Given two Windows decl attributes lists, possibly including
   dllimport, return a list of their union .  */
extern tree merge_dllimport_decl_attributes (tree, tree);

/* Handle a "dllimport" or "dllexport" attribute.  */
extern tree handle_dll_attribute (tree *, tree, tree, int, bool *);
#endif

/* Check whether CAND is suitable to be returned from get_qualified_type
   (BASE, TYPE_QUALS).  */

extern bool check_qualified_type (tree, tree, int);

/* Return a version of the TYPE, qualified as indicated by the
   TYPE_QUALS, if one exists.  If no qualified version exists yet,
   return NULL_TREE.  */

extern tree get_qualified_type (tree, int);

/* Like get_qualified_type, but creates the type if it does not
   exist.  This function never returns NULL_TREE.  */

extern tree build_qualified_type (tree, int);

/* Like build_qualified_type, but only deals with the `const' and
   `volatile' qualifiers.  This interface is retained for backwards
   compatibility with the various front-ends; new code should use
   build_qualified_type instead.  */

#define build_type_variant(TYPE, CONST_P, VOLATILE_P)			\
  build_qualified_type ((TYPE),						\
			((CONST_P) ? TYPE_QUAL_CONST : 0)		\
			| ((VOLATILE_P) ? TYPE_QUAL_VOLATILE : 0))

/* Make a copy of a type node.  */

extern tree build_distinct_type_copy (tree);
extern tree build_variant_type_copy (tree);

/* Finish up a builtin RECORD_TYPE. Give it a name and provide its
   fields. Optionally specify an alignment, and then lay it out.  */

extern void finish_builtin_struct (tree, const char *,
							 tree, tree);

/* Given a ..._TYPE node, calculate the TYPE_SIZE, TYPE_SIZE_UNIT,
   TYPE_ALIGN and TYPE_MODE fields.  If called more than once on one
   node, does nothing except for the first time.  */

extern void layout_type (tree);

/* These functions allow a front-end to perform a manual layout of a
   RECORD_TYPE.  (For instance, if the placement of subsequent fields
   depends on the placement of fields so far.)  Begin by calling
   start_record_layout.  Then, call place_field for each of the
   fields.  Then, call finish_record_layout.  See layout_type for the
   default way in which these functions are used.  */

typedef struct record_layout_info_s
{
  /* The RECORD_TYPE that we are laying out.  */
  tree t;
  /* The offset into the record so far, in bytes, not including bits in
     BITPOS.  */
  tree offset;
  /* The last known alignment of SIZE.  */
  unsigned int offset_align;
  /* The bit position within the last OFFSET_ALIGN bits, in bits.  */
  tree bitpos;
  /* The alignment of the record so far, in bits.  */
  unsigned int record_align;
  /* The alignment of the record so far, ignoring #pragma pack and
     __attribute__ ((packed)), in bits.  */
  unsigned int unpacked_align;
  /* The previous field layed out.  */
  tree prev_field;
  /* The static variables (i.e., class variables, as opposed to
     instance variables) encountered in T.  */
  tree pending_statics;
  /* Bits remaining in the current alignment group */
  int remaining_in_alignment;
  /* True if we've seen a packed field that didn't have normal
     alignment anyway.  */
  int packed_maybe_necessary;
} *record_layout_info;

extern void set_lang_adjust_rli (void (*) (record_layout_info));
extern record_layout_info start_record_layout (tree);
extern tree bit_from_pos (tree, tree);
extern tree byte_from_pos (tree, tree);
extern void pos_from_bit (tree *, tree *, unsigned int, tree);
extern void normalize_offset (tree *, tree *, unsigned int);
extern tree rli_size_unit_so_far (record_layout_info);
extern tree rli_size_so_far (record_layout_info);
extern void normalize_rli (record_layout_info);
extern void place_field (record_layout_info, tree);
extern void compute_record_mode (tree);
extern void finish_record_layout (record_layout_info, int);

/* Given a hashcode and a ..._TYPE node (for which the hashcode was made),
   return a canonicalized ..._TYPE node, so that duplicates are not made.
   How the hash code is computed is up to the caller, as long as any two
   callers that could hash identical-looking type nodes agree.  */

extern tree type_hash_canon (unsigned int, tree);

/* Given a VAR_DECL, PARM_DECL, RESULT_DECL or FIELD_DECL node,
   calculates the DECL_SIZE, DECL_SIZE_UNIT, DECL_ALIGN and DECL_MODE
   fields.  Call this only once for any given decl node.

   Second argument is the boundary that this field can be assumed to
   be starting at (in bits).  Zero means it can be assumed aligned
   on any boundary that may be needed.  */

extern void layout_decl (tree, unsigned);

/* Given a VAR_DECL, PARM_DECL or RESULT_DECL, clears the results of
   a previous call to layout_decl and calls it again.  */

extern void relayout_decl (tree);

/* Return the mode for data of a given size SIZE and mode class CLASS.
   If LIMIT is nonzero, then don't use modes bigger than MAX_FIXED_MODE_SIZE.
   The value is BLKmode if no other mode is found.  This is like
   mode_for_size, but is passed a tree.  */

extern enum machine_mode mode_for_size_tree (tree, enum mode_class, int);

/* Return an expr equal to X but certainly not valid as an lvalue.  */

extern tree non_lvalue (tree);

extern tree convert (tree, tree);
extern unsigned int expr_align (tree);
extern tree expr_first (tree);
extern tree expr_last (tree);
extern tree expr_only (tree);
extern tree size_in_bytes (tree);
extern HOST_WIDE_INT int_size_in_bytes (tree);
extern HOST_WIDE_INT max_int_size_in_bytes (tree);
extern tree bit_position (tree);
extern HOST_WIDE_INT int_bit_position (tree);
extern tree byte_position (tree);
extern HOST_WIDE_INT int_byte_position (tree);

/* Define data structures, macros, and functions for handling sizes
   and the various types used to represent sizes.  */

enum size_type_kind
{
  SIZETYPE,		/* Normal representation of sizes in bytes.  */
  SSIZETYPE,		/* Signed representation of sizes in bytes.  */
  BITSIZETYPE,		/* Normal representation of sizes in bits.  */
  SBITSIZETYPE,		/* Signed representation of sizes in bits.  */
  TYPE_KIND_LAST};

extern GTY(()) tree sizetype_tab[(int) TYPE_KIND_LAST];

#define sizetype sizetype_tab[(int) SIZETYPE]
#define bitsizetype sizetype_tab[(int) BITSIZETYPE]
#define ssizetype sizetype_tab[(int) SSIZETYPE]
#define sbitsizetype sizetype_tab[(int) SBITSIZETYPE]

extern tree size_int_kind (HOST_WIDE_INT, enum size_type_kind);
extern tree size_binop (enum tree_code, tree, tree);
extern tree size_diffop (tree, tree);

#define size_int(L) size_int_kind (L, SIZETYPE)
#define ssize_int(L) size_int_kind (L, SSIZETYPE)
#define bitsize_int(L) size_int_kind (L, BITSIZETYPE)
#define sbitsize_int(L) size_int_kind (L, SBITSIZETYPE)

extern tree round_up (tree, int);
extern tree round_down (tree, int);
extern tree get_pending_sizes (void);
extern void put_pending_size (tree);
extern void put_pending_sizes (tree);

/* Type for sizes of data-type.  */

#define BITS_PER_UNIT_LOG \
  ((BITS_PER_UNIT > 1) + (BITS_PER_UNIT > 2) + (BITS_PER_UNIT > 4) \
   + (BITS_PER_UNIT > 8) + (BITS_PER_UNIT > 16) + (BITS_PER_UNIT > 32) \
   + (BITS_PER_UNIT > 64) + (BITS_PER_UNIT > 128) + (BITS_PER_UNIT > 256))

/* If nonzero, an upper limit on alignment of structure fields, in bits,  */
extern unsigned int maximum_field_alignment;
/* and its original value in bytes, specified via -fpack-struct=<value>.  */
extern unsigned int initial_max_fld_align;

/* Concatenate two lists (chains of TREE_LIST nodes) X and Y
   by making the last node in X point to Y.
   Returns X, except if X is 0 returns Y.  */

extern tree chainon (tree, tree);

/* Make a new TREE_LIST node from specified PURPOSE, VALUE and CHAIN.  */

extern tree tree_cons_stat (tree, tree, tree MEM_STAT_DECL);
#define tree_cons(t,q,w) tree_cons_stat (t,q,w MEM_STAT_INFO)

/* Return the last tree node in a chain.  */

extern tree tree_last (tree);

/* Reverse the order of elements in a chain, and return the new head.  */

extern tree nreverse (tree);

/* Returns the length of a chain of nodes
   (number of chain pointers to follow before reaching a null pointer).  */

extern int list_length (tree);

/* Returns the number of FIELD_DECLs in a type.  */

extern int fields_length (tree);

/* Given an initializer INIT, return TRUE if INIT is zero or some
   aggregate of zeros.  Otherwise return FALSE.  */

extern bool initializer_zerop (tree);

/* Examine CTOR to discover:
   * how many scalar fields are set to nonzero values,
     and place it in *P_NZ_ELTS;
   * how many scalar fields in total are in CTOR,
     and place it in *P_ELT_COUNT.
   * if a type is a union, and the initializer from the constructor
     is not the largest element in the union, then set *p_must_clear.

   Return whether or not CTOR is a valid static constant initializer, the same
   as "initializer_constant_valid_p (CTOR, TREE_TYPE (CTOR)) != 0".  */

extern bool categorize_ctor_elements (tree, HOST_WIDE_INT *, HOST_WIDE_INT *,
				      bool *);

extern HOST_WIDE_INT count_type_elements (tree, bool);

/* integer_zerop (tree x) is nonzero if X is an integer constant of value 0.  */

extern int integer_zerop (tree);

/* integer_onep (tree x) is nonzero if X is an integer constant of value 1.  */

extern int integer_onep (tree);

/* integer_all_onesp (tree x) is nonzero if X is an integer constant
   all of whose significant bits are 1.  */

extern int integer_all_onesp (tree);

/* integer_pow2p (tree x) is nonzero is X is an integer constant with
   exactly one bit 1.  */

extern int integer_pow2p (tree);

/* integer_nonzerop (tree x) is nonzero if X is an integer constant
   with a nonzero value.  */

extern int integer_nonzerop (tree);

extern bool zero_p (tree);
extern bool cst_and_fits_in_hwi (tree);
extern tree num_ending_zeros (tree);

/* staticp (tree x) is nonzero if X is a reference to data allocated
   at a fixed address in memory.  Returns the outermost data.  */

extern tree staticp (tree);

/* save_expr (EXP) returns an expression equivalent to EXP
   but it can be used multiple times within context CTX
   and only evaluate EXP once.  */

extern tree save_expr (tree);

/* Look inside EXPR and into any simple arithmetic operations.  Return
   the innermost non-arithmetic node.  */

extern tree skip_simple_arithmetic (tree);

/* Return which tree structure is used by T.  */

enum tree_node_structure_enum tree_node_structure (tree);

/* Return 1 if EXP contains a PLACEHOLDER_EXPR; i.e., if it represents a size
   or offset that depends on a field within a record.

   Note that we only allow such expressions within simple arithmetic
   or a COND_EXPR.  */

extern bool contains_placeholder_p (tree);

/* This macro calls the above function but short-circuits the common
   case of a constant to save time.  Also check for null.  */

#define CONTAINS_PLACEHOLDER_P(EXP) \
  ((EXP) != 0 && ! TREE_CONSTANT (EXP) && contains_placeholder_p (EXP))

/* Return 1 if any part of the computation of TYPE involves a PLACEHOLDER_EXPR.
   This includes size, bounds, qualifiers (for QUAL_UNION_TYPE) and field
   positions.  */

extern bool type_contains_placeholder_p (tree);

/* Given a tree EXP, a FIELD_DECL F, and a replacement value R,
   return a tree with all occurrences of references to F in a
   PLACEHOLDER_EXPR replaced by R.   Note that we assume here that EXP
   contains only arithmetic expressions.  */

extern tree substitute_in_expr (tree, tree, tree);

/* This macro calls the above function but short-circuits the common
   case of a constant to save time and also checks for NULL.  */

#define SUBSTITUTE_IN_EXPR(EXP, F, R) \
  ((EXP) == 0 || TREE_CONSTANT (EXP) ? (EXP) : substitute_in_expr (EXP, F, R))

/* Similar, but look for a PLACEHOLDER_EXPR in EXP and find a replacement
   for it within OBJ, a tree that is an object or a chain of references.  */

extern tree substitute_placeholder_in_expr (tree, tree);

/* This macro calls the above function but short-circuits the common
   case of a constant to save time and also checks for NULL.  */

#define SUBSTITUTE_PLACEHOLDER_IN_EXPR(EXP, OBJ) \
  ((EXP) == 0 || TREE_CONSTANT (EXP) ? (EXP)	\
   : substitute_placeholder_in_expr (EXP, OBJ))

/* variable_size (EXP) is like save_expr (EXP) except that it
   is for the special case of something that is part of a
   variable size for a data type.  It makes special arrangements
   to compute the value at the right time when the data type
   belongs to a function parameter.  */

extern tree variable_size (tree);

/* stabilize_reference (EXP) returns a reference equivalent to EXP
   but it can be used multiple times
   and only evaluate the subexpressions once.  */

extern tree stabilize_reference (tree);

/* Subroutine of stabilize_reference; this is called for subtrees of
   references.  Any expression with side-effects must be put in a SAVE_EXPR
   to ensure that it is only evaluated once.  */

extern tree stabilize_reference_1 (tree);

/* Return EXP, stripped of any conversions to wider types
   in such a way that the result of converting to type FOR_TYPE
   is the same as if EXP were converted to FOR_TYPE.
   If FOR_TYPE is 0, it signifies EXP's type.  */

extern tree get_unwidened (tree, tree);

/* Return OP or a simpler expression for a narrower value
   which can be sign-extended or zero-extended to give back OP.
   Store in *UNSIGNEDP_PTR either 1 if the value should be zero-extended
   or 0 if the value should be sign-extended.  */

extern tree get_narrower (tree, int *);

/* Given an expression EXP that may be a COMPONENT_REF or an ARRAY_REF,
   look for nested component-refs or array-refs at constant positions
   and find the ultimate containing object, which is returned.  */

extern tree get_inner_reference (tree, HOST_WIDE_INT *, HOST_WIDE_INT *,
				 tree *, enum machine_mode *, int *, int *,
				 bool);

/* Given an expression EXP that may be a COMPONENT_REF or an ARRAY_REF,
   look for whether EXP or any nested component-refs within EXP is marked
   as PACKED.  */

extern bool contains_packed_reference (tree exp);

/* Return 1 if T is an expression that get_inner_reference handles.  */

extern int handled_component_p (tree);

/* Return a tree of sizetype representing the size, in bytes, of the element
   of EXP, an ARRAY_REF.  */

extern tree array_ref_element_size (tree);

/* Return a tree representing the lower bound of the array mentioned in
   EXP, an ARRAY_REF.  */

extern tree array_ref_low_bound (tree);

/* Return a tree representing the upper bound of the array mentioned in
   EXP, an ARRAY_REF.  */

extern tree array_ref_up_bound (tree);

/* Return a tree representing the offset, in bytes, of the field referenced
   by EXP.  This does not include any offset in DECL_FIELD_BIT_OFFSET.  */

extern tree component_ref_field_offset (tree);

/* Given a DECL or TYPE, return the scope in which it was declared, or
   NUL_TREE if there is no containing scope.  */

extern tree get_containing_scope (tree);

/* Return the FUNCTION_DECL which provides this _DECL with its context,
   or zero if none.  */
extern tree decl_function_context (tree);

/* Return the RECORD_TYPE, UNION_TYPE, or QUAL_UNION_TYPE which provides
   this _DECL with its context, or zero if none.  */
extern tree decl_type_context (tree);

/* Return 1 if EXPR is the real constant zero.  */
extern int real_zerop (tree);

/* Declare commonly used variables for tree structure.  */

/* Nonzero means lvalues are limited to those valid in pedantic ANSI C.
   Zero means allow extended lvalues.  */

extern int pedantic_lvalues;

/* Points to the FUNCTION_DECL of the function whose body we are reading.  */

extern GTY(()) tree current_function_decl;

/* Nonzero means a FUNC_BEGIN label was emitted.  */
extern GTY(()) const char * current_function_func_begin_label;

/* In tree.c */
extern unsigned crc32_string (unsigned, const char *);
extern void clean_symbol_name (char *);
extern tree get_file_function_name (const char *);
extern tree get_callee_fndecl (tree);
extern void change_decl_assembler_name (tree, tree);
extern int type_num_arguments (tree);
extern bool associative_tree_code (enum tree_code);
extern bool commutative_tree_code (enum tree_code);
extern tree upper_bound_in_type (tree, tree);
extern tree lower_bound_in_type (tree, tree);
extern int operand_equal_for_phi_arg_p (tree, tree);
extern bool empty_body_p (tree);

/* In stmt.c */

extern void expand_expr_stmt (tree);
extern int warn_if_unused_value (tree, location_t);
extern void expand_label (tree);
extern void expand_goto (tree);

extern rtx expand_stack_save (void);
extern void expand_stack_restore (tree);
extern void expand_return (tree);
extern int is_body_block (tree);

/* In tree-eh.c */
extern void using_eh_for_cleanups (void);

/* In fold-const.c */

/* Non-zero if we are folding constants inside an initializer; zero
   otherwise.  */
extern int folding_initializer;

/* Fold constants as much as possible in an expression.
   Returns the simplified expression.
   Acts only on the top level of the expression;
   if the argument itself cannot be simplified, its
   subexpressions are not changed.  */

extern tree fold (tree);
extern tree fold_unary (enum tree_code, tree, tree);
extern tree fold_binary (enum tree_code, tree, tree, tree);
extern tree fold_ternary (enum tree_code, tree, tree, tree, tree);
extern tree fold_build1_stat (enum tree_code, tree, tree MEM_STAT_DECL);
#define fold_build1(c,t1,t2) fold_build1_stat (c, t1, t2 MEM_STAT_INFO)
extern tree fold_build2_stat (enum tree_code, tree, tree, tree MEM_STAT_DECL);
#define fold_build2(c,t1,t2,t3) fold_build2_stat (c, t1, t2, t3 MEM_STAT_INFO)
extern tree fold_build3_stat (enum tree_code, tree, tree, tree, tree MEM_STAT_DECL);
#define fold_build3(c,t1,t2,t3,t4) fold_build3_stat (c, t1, t2, t3, t4 MEM_STAT_INFO)
extern tree fold_build1_initializer (enum tree_code, tree, tree);
extern tree fold_build2_initializer (enum tree_code, tree, tree, tree);
extern tree fold_build3_initializer (enum tree_code, tree, tree, tree, tree);
extern tree fold_convert (tree, tree);
extern tree fold_single_bit_test (enum tree_code, tree, tree, tree);
extern tree fold_ignored_result (tree);
extern tree fold_abs_const (tree, tree);
extern tree fold_indirect_ref_1 (tree, tree);
extern void fold_defer_overflow_warnings (void);
extern void fold_undefer_overflow_warnings (bool, tree, int);
extern void fold_undefer_and_ignore_overflow_warnings (void);
extern bool fold_deferring_overflow_warnings_p (void);

extern tree force_fit_type (tree, int, bool, bool);

extern int add_double_with_sign (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
				 unsigned HOST_WIDE_INT, HOST_WIDE_INT,
				 unsigned HOST_WIDE_INT *, HOST_WIDE_INT *,
				 bool);
#define add_double(l1,h1,l2,h2,lv,hv) \
  add_double_with_sign (l1, h1, l2, h2, lv, hv, false)
extern int neg_double (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
		       unsigned HOST_WIDE_INT *, HOST_WIDE_INT *);
extern int mul_double_with_sign (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
				 unsigned HOST_WIDE_INT, HOST_WIDE_INT,
				 unsigned HOST_WIDE_INT *, HOST_WIDE_INT *,
				 bool);
#define mul_double(l1,h1,l2,h2,lv,hv) \
  mul_double_with_sign (l1, h1, l2, h2, lv, hv, false)
extern void lshift_double (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
			   HOST_WIDE_INT, unsigned int,
			   unsigned HOST_WIDE_INT *, HOST_WIDE_INT *, int);
extern void rshift_double (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
			   HOST_WIDE_INT, unsigned int,
			   unsigned HOST_WIDE_INT *, HOST_WIDE_INT *, int);
extern void lrotate_double (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
			    HOST_WIDE_INT, unsigned int,
			    unsigned HOST_WIDE_INT *, HOST_WIDE_INT *);
extern void rrotate_double (unsigned HOST_WIDE_INT, HOST_WIDE_INT,
			    HOST_WIDE_INT, unsigned int,
			    unsigned HOST_WIDE_INT *, HOST_WIDE_INT *);

extern int div_and_round_double (enum tree_code, int, unsigned HOST_WIDE_INT,
				 HOST_WIDE_INT, unsigned HOST_WIDE_INT,
				 HOST_WIDE_INT, unsigned HOST_WIDE_INT *,
				 HOST_WIDE_INT *, unsigned HOST_WIDE_INT *,
				 HOST_WIDE_INT *);

enum operand_equal_flag
{
  OEP_ONLY_CONST = 1,
  OEP_PURE_SAME = 2
};

extern int operand_equal_p (tree, tree, unsigned int);

extern tree omit_one_operand (tree, tree, tree);
extern tree omit_two_operands (tree, tree, tree, tree);
extern tree invert_truthvalue (tree);
extern tree fold_truth_not_expr (tree);
extern tree fold_unary_to_constant (enum tree_code, tree, tree);
extern tree fold_binary_to_constant (enum tree_code, tree, tree, tree);
extern tree fold_read_from_constant_string (tree);
extern tree int_const_binop (enum tree_code, tree, tree, int);
extern tree build_fold_addr_expr (tree);
extern tree fold_build_cleanup_point_expr (tree type, tree expr);
extern tree fold_strip_sign_ops (tree);
extern tree build_fold_addr_expr_with_type (tree, tree);
extern tree build_fold_indirect_ref (tree);
extern tree fold_indirect_ref (tree);
extern tree constant_boolean_node (int, tree);
extern tree build_low_bits_mask (tree, unsigned);

extern bool tree_swap_operands_p (tree, tree, bool);
extern void swap_tree_operands (tree, tree *, tree *);
extern enum tree_code swap_tree_comparison (enum tree_code);

extern bool ptr_difference_const (tree, tree, HOST_WIDE_INT *);
extern enum tree_code invert_tree_comparison (enum tree_code, bool);

extern bool tree_expr_nonzero_p (tree);
extern bool tree_expr_nonzero_warnv_p (tree, bool *);

/* In builtins.c */
extern tree fold_builtin (tree, tree, bool);
extern tree fold_builtin_fputs (tree, bool, bool, tree);
extern tree fold_builtin_strcpy (tree, tree, tree);
extern tree fold_builtin_strncpy (tree, tree, tree);
extern tree fold_builtin_memory_chk (tree, tree, tree, bool,
				     enum built_in_function);
extern tree fold_builtin_stxcpy_chk (tree, tree, tree, bool,
				     enum built_in_function);
extern tree fold_builtin_strncpy_chk (tree, tree);
extern tree fold_builtin_snprintf_chk (tree, tree, enum built_in_function);
extern bool fold_builtin_next_arg (tree);
extern enum built_in_function builtin_mathfn_code (tree);
extern tree build_function_call_expr (tree, tree);
extern tree mathfn_built_in (tree, enum built_in_function fn);
extern tree strip_float_extensions (tree);
extern tree c_strlen (tree, int);
extern tree std_gimplify_va_arg_expr (tree, tree, tree *, tree *);
extern tree build_va_arg_indirect_ref (tree);
tree build_string_literal (int, const char *);

/* In convert.c */
extern tree strip_float_extensions (tree);

/* In alias.c */
extern void record_component_aliases (tree);
extern HOST_WIDE_INT get_alias_set (tree);
extern int alias_sets_conflict_p (HOST_WIDE_INT, HOST_WIDE_INT);
extern int alias_sets_might_conflict_p (HOST_WIDE_INT, HOST_WIDE_INT);
extern int objects_must_conflict_p (tree, tree);

/* In tree.c */
extern int really_constant_p (tree);
extern int int_fits_type_p (tree, tree);
extern bool variably_modified_type_p (tree, tree);
extern int tree_log2 (tree);
extern int tree_floor_log2 (tree);
extern int simple_cst_equal (tree, tree);
extern hashval_t iterative_hash_expr (tree, hashval_t);
extern int compare_tree_int (tree, unsigned HOST_WIDE_INT);
extern int type_list_equal (tree, tree);
extern int chain_member (tree, tree);
extern tree type_hash_lookup (unsigned int, tree);
extern void type_hash_add (unsigned int, tree);
extern int simple_cst_list_equal (tree, tree);
extern void dump_tree_statistics (void);
extern void expand_function_end (void);
extern void expand_function_start (tree);
extern void stack_protect_prologue (void);
extern void stack_protect_epilogue (void);
extern void recompute_tree_invariant_for_addr_expr (tree);
extern bool is_global_var (tree t);
extern bool needs_to_live_in_memory (tree);
extern tree reconstruct_complex_type (tree, tree);

extern int real_onep (tree);
extern int real_twop (tree);
extern int real_minus_onep (tree);
extern void init_ttree (void);
extern void build_common_tree_nodes (bool, bool);
extern void build_common_tree_nodes_2 (int);
extern void build_common_builtin_nodes (void);
extern tree build_nonstandard_integer_type (unsigned HOST_WIDE_INT, int);
extern tree build_range_type (tree, tree, tree);
extern HOST_WIDE_INT int_cst_value (tree);
extern tree tree_fold_gcd (tree, tree);
extern tree build_addr (tree, tree);

extern bool fields_compatible_p (tree, tree);
extern tree find_compatible_field (tree, tree);

/* In function.c */
extern void expand_main_function (void);
extern void init_dummy_function_start (void);
extern void expand_dummy_function_end (void);
extern unsigned int init_function_for_compilation (void);
extern void allocate_struct_function (tree);
extern void init_function_start (tree);
extern bool use_register_for_decl (tree);
extern void setjmp_vars_warning (tree);
extern void setjmp_args_warning (void);
extern void init_temp_slots (void);
extern void free_temp_slots (void);
extern void pop_temp_slots (void);
extern void push_temp_slots (void);
extern void preserve_temp_slots (rtx);
extern int aggregate_value_p (tree, tree);
extern void push_function_context (void);
extern void pop_function_context (void);
extern void push_function_context_to (tree);
extern void pop_function_context_from (tree);
extern tree gimplify_parameters (void);

/* In print-rtl.c */
#ifdef BUFSIZ
extern void print_rtl (FILE *, rtx);
#endif

/* In print-tree.c */
extern void debug_tree (tree);
#ifdef BUFSIZ
extern void dump_addr (FILE*, const char *, void *);
extern void print_node (FILE *, const char *, tree, int);
extern void print_node_brief (FILE *, const char *, tree, int);
extern void indent_to (FILE *, int);
#endif

/* In tree-inline.c:  */
extern bool debug_find_tree (tree, tree);
/* This is in tree-inline.c since the routine uses
   data structures from the inliner.  */
extern tree unsave_expr_now (tree);
extern tree build_duplicate_type (tree);

/* In emit-rtl.c */
extern rtx emit_line_note (location_t);

/* In calls.c */

/* Nonzero if this is a call to a function whose return value depends
   solely on its arguments, has no side effects, and does not read
   global memory.  */
#define ECF_CONST		1
/* Nonzero if this call will never return.  */
#define ECF_NORETURN		2
/* Nonzero if this is a call to malloc or a related function.  */
#define ECF_MALLOC		4
/* Nonzero if it is plausible that this is a call to alloca.  */
#define ECF_MAY_BE_ALLOCA	8
/* Nonzero if this is a call to a function that won't throw an exception.  */
#define ECF_NOTHROW		16
/* Nonzero if this is a call to setjmp or a related function.  */
#define ECF_RETURNS_TWICE	32
/* Nonzero if this call replaces the current stack frame.  */
#define ECF_SIBCALL		64
/* Nonzero if this is a call to "pure" function (like const function,
   but may read memory.  */
#define ECF_PURE		128
/* Nonzero if this is a call to a function that returns with the stack
   pointer depressed.  */
#define ECF_SP_DEPRESSED	256
/* Create libcall block around the call.  */
#define ECF_LIBCALL_BLOCK	512
/* Function does not read or write memory (but may have side effects, so
   it does not necessarily fit ECF_CONST).  */
#define ECF_NOVOPS		1024

extern int flags_from_decl_or_type (tree);
extern int call_expr_flags (tree);

extern int setjmp_call_p (tree);
extern bool alloca_call_p (tree);
extern bool must_pass_in_stack_var_size (enum machine_mode, tree);
extern bool must_pass_in_stack_var_size_or_pad (enum machine_mode, tree);

/* In attribs.c.  */

/* Process the attributes listed in ATTRIBUTES and install them in *NODE,
   which is either a DECL (including a TYPE_DECL) or a TYPE.  If a DECL,
   it should be modified in place; if a TYPE, a copy should be created
   unless ATTR_FLAG_TYPE_IN_PLACE is set in FLAGS.  FLAGS gives further
   information, in the form of a bitwise OR of flags in enum attribute_flags
   from tree.h.  Depending on these flags, some attributes may be
   returned to be applied at a later stage (for example, to apply
   a decl attribute to the declaration rather than to its type).  */
extern tree decl_attributes (tree *, tree, int);

/* In integrate.c */
extern void set_decl_abstract_flags (tree, int);
extern void set_decl_origin_self (tree);

/* In stor-layout.c */
extern void set_min_and_max_values_for_integral_type (tree, int, bool);
extern void fixup_signed_type (tree);
extern void internal_reference_types (void);
extern unsigned int update_alignment_for_field (record_layout_info, tree,
                                                unsigned int);
/* varasm.c */
extern void make_decl_rtl (tree);
extern void make_decl_one_only (tree);
extern int supports_one_only (void);
extern void resolve_unique_section (tree, int, int);
extern void mark_referenced (tree);
extern void mark_decl_referenced (tree);
extern void notice_global_symbol (tree);
extern void set_user_assembler_name (tree, const char *);
extern void process_pending_assemble_externals (void);
extern void finish_aliases_1 (void);
extern void finish_aliases_2 (void);

/* In stmt.c */
extern void expand_computed_goto (tree);
extern bool parse_output_constraint (const char **, int, int, int,
				     bool *, bool *, bool *);
extern bool parse_input_constraint (const char **, int, int, int, int,
				    const char * const *, bool *, bool *);
extern void expand_asm_expr (tree);
extern tree resolve_asm_operand_names (tree, tree, tree);
extern void expand_case (tree);
extern void expand_decl (tree);
extern void expand_anon_union_decl (tree, tree, tree);
#ifdef HARD_CONST
/* Silly ifdef to avoid having all includers depend on hard-reg-set.h.  */
extern tree tree_overlaps_hard_reg_set (tree, HARD_REG_SET *);
#endif

/* In gimplify.c.  */
extern tree create_artificial_label (void);
extern void gimplify_function_tree (tree);
extern const char *get_name (tree);
extern tree unshare_expr (tree);
extern void sort_case_labels (tree);

/* Interface of the DWARF2 unwind info support.  */

/* Generate a new label for the CFI info to refer to.  */

extern char *dwarf2out_cfi_label (void);

/* Entry point to update the canonical frame address (CFA).  */

extern void dwarf2out_def_cfa (const char *, unsigned, HOST_WIDE_INT);

/* Add the CFI for saving a register window.  */

extern void dwarf2out_window_save (const char *);

/* Add a CFI to update the running total of the size of arguments pushed
   onto the stack.  */

extern void dwarf2out_args_size (const char *, HOST_WIDE_INT);

/* Entry point for saving a register to the stack.  */

extern void dwarf2out_reg_save (const char *, unsigned, HOST_WIDE_INT);

/* Entry point for saving the return address in the stack.  */

extern void dwarf2out_return_save (const char *, HOST_WIDE_INT);

/* Entry point for saving the return address in a register.  */

extern void dwarf2out_return_reg (const char *, unsigned);

/* Entry point for saving the first register into the second.  */

extern void dwarf2out_reg_save_reg (const char *, rtx, rtx);

/* In tree-inline.c  */

/* The type of a set of already-visited pointers.  Functions for creating
   and manipulating it are declared in pointer-set.h */
struct pointer_set_t;

/* The type of a callback function for walking over tree structure.  */

typedef tree (*walk_tree_fn) (tree *, int *, void *);
extern tree walk_tree (tree*, walk_tree_fn, void*, struct pointer_set_t*);
extern tree walk_tree_without_duplicates (tree*, walk_tree_fn, void*);

/* Assign the RTX to declaration.  */

extern void set_decl_rtl (tree, rtx);
extern void set_decl_incoming_rtl (tree, rtx);

/* Enum and arrays used for tree allocation stats.
   Keep in sync with tree.c:tree_node_kind_names.  */
typedef enum
{
  d_kind,
  t_kind,
  b_kind,
  s_kind,
  r_kind,
  e_kind,
  c_kind,
  id_kind,
  perm_list_kind,
  temp_list_kind,
  vec_kind,
  binfo_kind,
  phi_kind,
  ssa_name_kind,
  constr_kind,
  x_kind,
  lang_decl,
  lang_type,
  omp_clause_kind,
  all_kinds
} tree_node_kind;

extern int tree_node_counts[];
extern int tree_node_sizes[];

/* True if we are in gimple form and the actions of the folders need to
   be restricted.  False if we are not in gimple form and folding is not
   restricted to creating gimple expressions.  */
extern bool in_gimple_form;

/* In tree-gimple.c.  */
extern tree get_base_address (tree t);

/* In tree-vectorizer.c.  */
extern void vect_set_verbosity_level (const char *);

struct tree_map GTY(())
{
  unsigned int hash;
  tree from;
  tree to;
};

extern unsigned int tree_map_hash (const void *);
extern int tree_map_marked_p (const void *);
extern int tree_map_eq (const void *, const void *);

/* In tree-ssa-address.c.  */
extern tree tree_mem_ref_addr (tree, tree);
extern void copy_mem_ref_info (tree, tree);

/* In tree-vrp.c */
extern bool ssa_name_nonzero_p (tree);
extern bool ssa_name_nonnegative_p (tree);

/* In tree-object-size.c.  */
extern void init_object_sizes (void);
extern void fini_object_sizes (void);
extern unsigned HOST_WIDE_INT compute_builtin_object_size (tree, int);

/* In expr.c.  */
extern unsigned HOST_WIDE_INT highest_pow2_factor (tree);

/* APPLE LOCAL begin radar 6300081  */
extern GTY(()) tree generic_block_literal_struct_type;
/* APPLE LOCAL end radar 6300081  */

#endif  /* GCC_TREE_H  */
