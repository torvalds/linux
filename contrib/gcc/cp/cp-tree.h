/* Definitions for C++ parsing and type checking.
   Copyright (C) 1987, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006  Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com)

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

#ifndef GCC_CP_TREE_H
#define GCC_CP_TREE_H

#include "ggc.h"
#include "function.h"
#include "hashtab.h"
#include "splay-tree.h"
#include "vec.h"
#include "varray.h"
#include "c-common.h"
#include "name-lookup.h"
struct diagnostic_context;

/* Usage of TREE_LANG_FLAG_?:
   0: IDENTIFIER_MARKED (IDENTIFIER_NODEs)
      NEW_EXPR_USE_GLOBAL (in NEW_EXPR).
      DELETE_EXPR_USE_GLOBAL (in DELETE_EXPR).
      COMPOUND_EXPR_OVERLOADED (in COMPOUND_EXPR).
      TREE_INDIRECT_USING (in NAMESPACE_DECL).
      CLEANUP_P (in TRY_BLOCK)
      AGGR_INIT_VIA_CTOR_P (in AGGR_INIT_EXPR)
      PTRMEM_OK_P (in ADDR_EXPR, OFFSET_REF)
      PAREN_STRING_LITERAL (in STRING_CST)
      DECL_PRETTY_FUNCTION_P (in VAR_DECL)
      KOENIG_LOOKUP_P (in CALL_EXPR)
      STATEMENT_LIST_NO_SCOPE (in STATEMENT_LIST).
      EXPR_STMT_STMT_EXPR_RESULT (in EXPR_STMT)
      STMT_EXPR_NO_SCOPE (in STMT_EXPR)
      BIND_EXPR_TRY_BLOCK (in BIND_EXPR)
      TYPENAME_IS_ENUM_P (in TYPENAME_TYPE)
      REFERENCE_REF_P (in INDIRECT_EXPR)
      QUALIFIED_NAME_IS_TEMPLATE (in SCOPE_REF)
      OMP_ATOMIC_DEPENDENT_P (in OMP_ATOMIC)
      OMP_FOR_GIMPLIFYING_P (in OMP_FOR)
      BASELINK_QUALIFIED_P (in BASELINK)
      TARGET_EXPR_IMPLICIT_P (in TARGET_EXPR)
   1: IDENTIFIER_VIRTUAL_P (in IDENTIFIER_NODE)
      TI_PENDING_TEMPLATE_FLAG.
      TEMPLATE_PARMS_FOR_INLINE.
      DELETE_EXPR_USE_VEC (in DELETE_EXPR).
      (TREE_CALLS_NEW) (in _EXPR or _REF) (commented-out).
      ICS_ELLIPSIS_FLAG (in _CONV)
      DECL_INITIALIZED_P (in VAR_DECL)
      TYPENAME_IS_CLASS_P (in TYPENAME_TYPE)
      STMT_IS_FULL_EXPR_P (in _STMT)
   2: IDENTIFIER_OPNAME_P (in IDENTIFIER_NODE)
      ICS_THIS_FLAG (in _CONV)
      DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (in VAR_DECL)
      STATEMENT_LIST_TRY_BLOCK (in STATEMENT_LIST)
   3: (TREE_REFERENCE_EXPR) (in NON_LVALUE_EXPR) (commented-out).
      ICS_BAD_FLAG (in _CONV)
      FN_TRY_BLOCK_P (in TRY_BLOCK)
      IDENTIFIER_CTOR_OR_DTOR_P (in IDENTIFIER_NODE)
      BIND_EXPR_BODY_BLOCK (in BIND_EXPR)
      DECL_NON_TRIVIALLY_INITIALIZED_P (in VAR_DECL)
   4: TREE_HAS_CONSTRUCTOR (in INDIRECT_REF, SAVE_EXPR, CONSTRUCTOR,
	  or FIELD_DECL).
      IDENTIFIER_TYPENAME_P (in IDENTIFIER_NODE)
      DECL_TINFO_P (in VAR_DECL)
   5: C_IS_RESERVED_WORD (in IDENTIFIER_NODE)
      DECL_VTABLE_OR_VTT_P (in VAR_DECL)
   6: IDENTIFIER_REPO_CHOSEN (in IDENTIFIER_NODE)
      DECL_CONSTRUCTION_VTABLE_P (in VAR_DECL)
      TYPE_MARKED_P (in _TYPE)

   Usage of TYPE_LANG_FLAG_?:
   0: TYPE_DEPENDENT_P
   1: TYPE_HAS_CONSTRUCTOR.
   2: Unused
   3: TYPE_FOR_JAVA.
   4: TYPE_HAS_NONTRIVIAL_DESTRUCTOR
   5: IS_AGGR_TYPE.
   6: TYPE_DEPENDENT_P_VALID

   Usage of DECL_LANG_FLAG_?:
   0: DECL_ERROR_REPORTED (in VAR_DECL).
      DECL_TEMPLATE_PARM_P (in PARM_DECL, CONST_DECL, TYPE_DECL, or TEMPLATE_DECL)
      DECL_LOCAL_FUNCTION_P (in FUNCTION_DECL)
      DECL_MUTABLE_P (in FIELD_DECL)
      DECL_DEPENDENT_P (in USING_DECL)
   1: C_TYPEDEF_EXPLICITLY_SIGNED (in TYPE_DECL).
      DECL_TEMPLATE_INSTANTIATED (in a VAR_DECL or a FUNCTION_DECL)
      DECL_MEMBER_TEMPLATE_P (in TEMPLATE_DECL)
   2: DECL_THIS_EXTERN (in VAR_DECL or FUNCTION_DECL).
      DECL_IMPLICIT_TYPEDEF_P (in a TYPE_DECL)
   3: DECL_IN_AGGR_P.
   4: DECL_C_BIT_FIELD (in a FIELD_DECL)
      DECL_ANON_UNION_VAR_P (in a VAR_DECL)
      DECL_SELF_REFERENCE_P (in a TYPE_DECL)
      DECL_INVALID_OVERRIDER_P (in a FUNCTION_DECL)
   5: DECL_INTERFACE_KNOWN.
   6: DECL_THIS_STATIC (in VAR_DECL or FUNCTION_DECL).
      DECL_FIELD_IS_BASE (in FIELD_DECL)
   7: DECL_DEAD_FOR_LOCAL (in VAR_DECL).
      DECL_THUNK_P (in a member FUNCTION_DECL)

   Usage of language-independent fields in a language-dependent manner:

   TYPE_ALIAS_SET
     This field is used by TYPENAME_TYPEs, TEMPLATE_TYPE_PARMs, and so
     forth as a substitute for the mark bits provided in `lang_type'.
     At present, only the six low-order bits are used.

   TYPE_LANG_SLOT_1
     For an ENUMERAL_TYPE, this is ENUM_TEMPLATE_INFO.
     For a FUNCTION_TYPE or METHOD_TYPE, this is TYPE_RAISES_EXCEPTIONS

  BINFO_VIRTUALS
     For a binfo, this is a TREE_LIST.  There is an entry for each
     virtual function declared either in BINFO or its direct and
     indirect primary bases.

     The BV_DELTA of each node gives the amount by which to adjust the
     `this' pointer when calling the function.  If the method is an
     overridden version of a base class method, then it is assumed
     that, prior to adjustment, the this pointer points to an object
     of the base class.

     The BV_VCALL_INDEX of each node, if non-NULL, gives the vtable
     index of the vcall offset for this entry.

     The BV_FN is the declaration for the virtual function itself.

   BINFO_VTABLE
     This is an expression with POINTER_TYPE that gives the value
     to which the vptr should be initialized.  Use get_vtbl_decl_for_binfo
     to extract the VAR_DECL for the complete vtable.

   DECL_ARGUMENTS
     For a VAR_DECL this is DECL_ANON_UNION_ELEMS.

   DECL_VINDEX
     This field is NULL for a non-virtual function.  For a virtual
     function, it is eventually set to an INTEGER_CST indicating the
     index in the vtable at which this function can be found.  When
     a virtual function is declared, but before it is known what
     function is overridden, this field is the error_mark_node.

     Temporarily, it may be set to a TREE_LIST whose TREE_VALUE is
     the virtual function this one overrides, and whose TREE_CHAIN is
     the old DECL_VINDEX.  */

/* Language-specific tree checkers.  */

#define VAR_OR_FUNCTION_DECL_CHECK(NODE) \
  TREE_CHECK2(NODE,VAR_DECL,FUNCTION_DECL)

#define VAR_FUNCTION_OR_PARM_DECL_CHECK(NODE) \
  TREE_CHECK3(NODE,VAR_DECL,FUNCTION_DECL,PARM_DECL)

#define VAR_TEMPL_TYPE_OR_FUNCTION_DECL_CHECK(NODE) \
  TREE_CHECK4(NODE,VAR_DECL,FUNCTION_DECL,TYPE_DECL,TEMPLATE_DECL)

#define BOUND_TEMPLATE_TEMPLATE_PARM_TYPE_CHECK(NODE) \
  TREE_CHECK(NODE,BOUND_TEMPLATE_TEMPLATE_PARM)

#if defined ENABLE_TREE_CHECKING && (GCC_VERSION >= 2007)
#define NON_THUNK_FUNCTION_CHECK(NODE) __extension__			\
({  const tree __t = (NODE);						\
    if (TREE_CODE (__t) != FUNCTION_DECL &&				\
	TREE_CODE (__t) != TEMPLATE_DECL && __t->decl_common.lang_specific	\
	&& __t->decl_common.lang_specific->decl_flags.thunk_p)			\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__, 0);	\
    __t; })
#define THUNK_FUNCTION_CHECK(NODE) __extension__			\
({  const tree __t = (NODE);						\
    if (TREE_CODE (__t) != FUNCTION_DECL || !__t->decl_common.lang_specific	\
	|| !__t->decl_common.lang_specific->decl_flags.thunk_p)		\
      tree_check_failed (__t, __FILE__, __LINE__, __FUNCTION__, 0);	\
     __t; })
#else
#define NON_THUNK_FUNCTION_CHECK(NODE) (NODE)
#define THUNK_FUNCTION_CHECK(NODE) (NODE)
#endif

/* Language-dependent contents of an identifier.  */

struct lang_identifier GTY(())
{
  struct c_common_identifier c_common;
  cxx_binding *namespace_bindings;
  cxx_binding *bindings;
  tree class_template_info;
  tree label_value;
};

/* In an IDENTIFIER_NODE, nonzero if this identifier is actually a
   keyword.  C_RID_CODE (node) is then the RID_* value of the keyword,
   and C_RID_YYCODE is the token number wanted by Yacc.  */

#define C_IS_RESERVED_WORD(ID) TREE_LANG_FLAG_5 (ID)

#define LANG_IDENTIFIER_CAST(NODE) \
	((struct lang_identifier*)IDENTIFIER_NODE_CHECK (NODE))

struct template_parm_index_s GTY(())
{
  struct tree_common common;
  HOST_WIDE_INT index;
  HOST_WIDE_INT level;
  HOST_WIDE_INT orig_level;
  tree decl;
};
typedef struct template_parm_index_s template_parm_index;

struct tinst_level_s GTY(())
{
  struct tree_common common;
  tree decl;
  location_t locus;
  int in_system_header_p;
};
typedef struct tinst_level_s * tinst_level_t;

struct ptrmem_cst GTY(())
{
  struct tree_common common;
  /* This isn't used, but the middle-end expects all constants to have
     this field.  */
  rtx rtl;
  tree member;
};
typedef struct ptrmem_cst * ptrmem_cst_t;

#define IDENTIFIER_GLOBAL_VALUE(NODE) \
  namespace_binding ((NODE), global_namespace)
#define SET_IDENTIFIER_GLOBAL_VALUE(NODE, VAL) \
  set_namespace_binding ((NODE), global_namespace, (VAL))
#define IDENTIFIER_NAMESPACE_VALUE(NODE) \
  namespace_binding ((NODE), current_namespace)
#define SET_IDENTIFIER_NAMESPACE_VALUE(NODE, VAL) \
  set_namespace_binding ((NODE), current_namespace, (VAL))

#define CLEANUP_P(NODE)		TREE_LANG_FLAG_0 (TRY_BLOCK_CHECK (NODE))

#define BIND_EXPR_TRY_BLOCK(NODE) \
  TREE_LANG_FLAG_0 (BIND_EXPR_CHECK (NODE))

/* Used to mark the block around the member initializers and cleanups.  */
#define BIND_EXPR_BODY_BLOCK(NODE) \
  TREE_LANG_FLAG_3 (BIND_EXPR_CHECK (NODE))
#define FUNCTION_NEEDS_BODY_BLOCK(NODE) \
  (DECL_CONSTRUCTOR_P (NODE) || DECL_DESTRUCTOR_P (NODE))

#define STATEMENT_LIST_NO_SCOPE(NODE) \
  TREE_LANG_FLAG_0 (STATEMENT_LIST_CHECK (NODE))
#define STATEMENT_LIST_TRY_BLOCK(NODE) \
  TREE_LANG_FLAG_2 (STATEMENT_LIST_CHECK (NODE))

/* Nonzero if this statement should be considered a full-expression,
   i.e., if temporaries created during this statement should have
   their destructors run at the end of this statement.  */
#define STMT_IS_FULL_EXPR_P(NODE) TREE_LANG_FLAG_1 ((NODE))

/* Marks the result of a statement expression.  */
#define EXPR_STMT_STMT_EXPR_RESULT(NODE) \
  TREE_LANG_FLAG_0 (EXPR_STMT_CHECK (NODE))

/* Nonzero if this statement-expression does not have an associated scope.  */
#define STMT_EXPR_NO_SCOPE(NODE) \
   TREE_LANG_FLAG_0 (STMT_EXPR_CHECK (NODE))

/* Returns nonzero iff TYPE1 and TYPE2 are the same type, in the usual
   sense of `same'.  */
#define same_type_p(TYPE1, TYPE2) \
  comptypes ((TYPE1), (TYPE2), COMPARE_STRICT)

/* Returns nonzero iff TYPE1 and TYPE2 are the same type, ignoring
   top-level qualifiers.  */
#define same_type_ignoring_top_level_qualifiers_p(TYPE1, TYPE2) \
  same_type_p (TYPE_MAIN_VARIANT (TYPE1), TYPE_MAIN_VARIANT (TYPE2))

/* Nonzero if we are presently building a statement tree, rather
   than expanding each statement as we encounter it.  */
#define building_stmt_tree()  (cur_stmt_list != NULL_TREE)

/* Returns nonzero iff NODE is a declaration for the global function
   `main'.  */
#define DECL_MAIN_P(NODE)				\
   (DECL_EXTERN_C_FUNCTION_P (NODE)			\
    && DECL_NAME (NODE) != NULL_TREE			\
    && MAIN_NAME_P (DECL_NAME (NODE)))

/* The overloaded FUNCTION_DECL.  */
#define OVL_FUNCTION(NODE) \
  (((struct tree_overload*)OVERLOAD_CHECK (NODE))->function)
#define OVL_CHAIN(NODE)      TREE_CHAIN (NODE)
/* Polymorphic access to FUNCTION and CHAIN.  */
#define OVL_CURRENT(NODE)	\
  ((TREE_CODE (NODE) == OVERLOAD) ? OVL_FUNCTION (NODE) : (NODE))
#define OVL_NEXT(NODE)		\
  ((TREE_CODE (NODE) == OVERLOAD) ? TREE_CHAIN (NODE) : NULL_TREE)
/* If set, this was imported in a using declaration.
   This is not to confuse with being used somewhere, which
   is not important for this node.  */
#define OVL_USED(NODE)		TREE_USED (NODE)

struct tree_overload GTY(())
{
  struct tree_common common;
  tree function;
};

/* Returns true iff NODE is a BASELINK.  */
#define BASELINK_P(NODE) \
  (TREE_CODE (NODE) == BASELINK)
/* The BINFO indicating the base from which the BASELINK_FUNCTIONS came.  */
#define BASELINK_BINFO(NODE) \
  (((struct tree_baselink*) BASELINK_CHECK (NODE))->binfo)
/* The functions referred to by the BASELINK; either a FUNCTION_DECL,
   a TEMPLATE_DECL, an OVERLOAD, or a TEMPLATE_ID_EXPR.  */
#define BASELINK_FUNCTIONS(NODE) \
  (((struct tree_baselink*) BASELINK_CHECK (NODE))->functions)
/* The BINFO in which the search for the functions indicated by this baselink
   began.  This base is used to determine the accessibility of functions
   selected by overload resolution.  */
#define BASELINK_ACCESS_BINFO(NODE) \
  (((struct tree_baselink*) BASELINK_CHECK (NODE))->access_binfo)
/* For a type-conversion operator, the BASELINK_OPTYPE indicates the type
   to which the conversion should occur.  This value is important if
   the BASELINK_FUNCTIONS include a template conversion operator --
   the BASELINK_OPTYPE can be used to determine what type the user
   requested.  */
#define BASELINK_OPTYPE(NODE) \
  (TREE_CHAIN (BASELINK_CHECK (NODE)))
/* Non-zero if this baselink was from a qualified lookup.  */
#define BASELINK_QUALIFIED_P(NODE) \
  TREE_LANG_FLAG_0 (BASELINK_CHECK (NODE))

struct tree_baselink GTY(())
{
  struct tree_common common;
  tree binfo;
  tree functions;
  tree access_binfo;
};

/* The different kinds of ids that we encounter.  */

typedef enum cp_id_kind
{
  /* Not an id at all.  */
  CP_ID_KIND_NONE,
  /* An unqualified-id that is not a template-id.  */
  CP_ID_KIND_UNQUALIFIED,
  /* An unqualified-id that is a dependent name.  */
  CP_ID_KIND_UNQUALIFIED_DEPENDENT,
  /* An unqualified template-id.  */
  CP_ID_KIND_TEMPLATE_ID,
  /* A qualified-id.  */
  CP_ID_KIND_QUALIFIED
} cp_id_kind;

/* Macros for access to language-specific slots in an identifier.  */

#define IDENTIFIER_NAMESPACE_BINDINGS(NODE)	\
  (LANG_IDENTIFIER_CAST (NODE)->namespace_bindings)
#define IDENTIFIER_TEMPLATE(NODE)	\
  (LANG_IDENTIFIER_CAST (NODE)->class_template_info)

/* The IDENTIFIER_BINDING is the innermost cxx_binding for the
    identifier.  It's PREVIOUS is the next outermost binding.  Each
    VALUE field is a DECL for the associated declaration.  Thus,
    name lookup consists simply of pulling off the node at the front
    of the list (modulo oddities for looking up the names of types,
    and such.)  You can use SCOPE field to determine the scope
    that bound the name.  */
#define IDENTIFIER_BINDING(NODE) \
  (LANG_IDENTIFIER_CAST (NODE)->bindings)

/* TREE_TYPE only indicates on local and class scope the current
   type. For namespace scope, the presence of a type in any namespace
   is indicated with global_type_node, and the real type behind must
   be found through lookup.  */
#define IDENTIFIER_TYPE_VALUE(NODE) identifier_type_value (NODE)
#define REAL_IDENTIFIER_TYPE_VALUE(NODE) TREE_TYPE (NODE)
#define SET_IDENTIFIER_TYPE_VALUE(NODE,TYPE) (TREE_TYPE (NODE) = (TYPE))
#define IDENTIFIER_HAS_TYPE_VALUE(NODE) (IDENTIFIER_TYPE_VALUE (NODE) ? 1 : 0)

#define IDENTIFIER_LABEL_VALUE(NODE) \
  (LANG_IDENTIFIER_CAST (NODE)->label_value)
#define SET_IDENTIFIER_LABEL_VALUE(NODE, VALUE)   \
  IDENTIFIER_LABEL_VALUE (NODE) = (VALUE)

/* Nonzero if this identifier is used as a virtual function name somewhere
   (optimizes searches).  */
#define IDENTIFIER_VIRTUAL_P(NODE) TREE_LANG_FLAG_1 (NODE)

/* Nonzero if this identifier is the prefix for a mangled C++ operator
   name.  */
#define IDENTIFIER_OPNAME_P(NODE) TREE_LANG_FLAG_2 (NODE)

/* Nonzero if this identifier is the name of a type-conversion
   operator.  */
#define IDENTIFIER_TYPENAME_P(NODE) \
  TREE_LANG_FLAG_4 (NODE)

/* Nonzero if this identifier is the name of a constructor or
   destructor.  */
#define IDENTIFIER_CTOR_OR_DTOR_P(NODE) \
  TREE_LANG_FLAG_3 (NODE)

/* True iff NAME is the DECL_ASSEMBLER_NAME for an entity with vague
   linkage which the prelinker has assigned to this translation
   unit.  */
#define IDENTIFIER_REPO_CHOSEN(NAME) \
  (TREE_LANG_FLAG_6 (NAME))

/* In a RECORD_TYPE or UNION_TYPE, nonzero if any component is read-only.  */
#define C_TYPE_FIELDS_READONLY(TYPE) \
  (LANG_TYPE_CLASS_CHECK (TYPE)->fields_readonly)

/* The tokens stored in the default argument.  */

#define DEFARG_TOKENS(NODE) \
  (((struct tree_default_arg *)DEFAULT_ARG_CHECK (NODE))->tokens)
#define DEFARG_INSTANTIATIONS(NODE) \
  (((struct tree_default_arg *)DEFAULT_ARG_CHECK (NODE))->instantiations)

struct tree_default_arg GTY (())
{
  struct tree_common common;
  struct cp_token_cache *tokens;
  VEC(tree,gc) *instantiations;
};

enum cp_tree_node_structure_enum {
  TS_CP_GENERIC,
  TS_CP_IDENTIFIER,
  TS_CP_TPI,
  TS_CP_TINST_LEVEL,
  TS_CP_PTRMEM,
  TS_CP_BINDING,
  TS_CP_OVERLOAD,
  TS_CP_BASELINK,
  TS_CP_WRAPPER,
  TS_CP_DEFAULT_ARG,
  LAST_TS_CP_ENUM
};

/* The resulting tree type.  */
union lang_tree_node GTY((desc ("cp_tree_node_structure (&%h)"),
       chain_next ("(union lang_tree_node *)TREE_CHAIN (&%h.generic)")))
{
  union tree_node GTY ((tag ("TS_CP_GENERIC"),
			desc ("tree_node_structure (&%h)"))) generic;
  struct template_parm_index_s GTY ((tag ("TS_CP_TPI"))) tpi;
  struct tinst_level_s GTY ((tag ("TS_CP_TINST_LEVEL"))) tinst_level;
  struct ptrmem_cst GTY ((tag ("TS_CP_PTRMEM"))) ptrmem;
  struct tree_overload GTY ((tag ("TS_CP_OVERLOAD"))) overload;
  struct tree_baselink GTY ((tag ("TS_CP_BASELINK"))) baselink;
  struct tree_default_arg GTY ((tag ("TS_CP_DEFAULT_ARG"))) default_arg;
  struct lang_identifier GTY ((tag ("TS_CP_IDENTIFIER"))) identifier;
};


enum cp_tree_index
{
    CPTI_JAVA_BYTE_TYPE,
    CPTI_JAVA_SHORT_TYPE,
    CPTI_JAVA_INT_TYPE,
    CPTI_JAVA_LONG_TYPE,
    CPTI_JAVA_FLOAT_TYPE,
    CPTI_JAVA_DOUBLE_TYPE,
    CPTI_JAVA_CHAR_TYPE,
    CPTI_JAVA_BOOLEAN_TYPE,

    CPTI_WCHAR_DECL,
    CPTI_VTABLE_ENTRY_TYPE,
    CPTI_DELTA_TYPE,
    CPTI_VTABLE_INDEX_TYPE,
    CPTI_CLEANUP_TYPE,
    CPTI_VTT_PARM_TYPE,

    CPTI_CLASS_TYPE,
    CPTI_UNKNOWN_TYPE,
    CPTI_VTBL_TYPE,
    CPTI_VTBL_PTR_TYPE,
    CPTI_STD,
    CPTI_ABI,
    CPTI_CONST_TYPE_INFO_TYPE,
    CPTI_TYPE_INFO_PTR_TYPE,
    CPTI_ABORT_FNDECL,
    CPTI_GLOBAL_DELETE_FNDECL,
    CPTI_AGGR_TAG,

    CPTI_CTOR_IDENTIFIER,
    CPTI_COMPLETE_CTOR_IDENTIFIER,
    CPTI_BASE_CTOR_IDENTIFIER,
    CPTI_DTOR_IDENTIFIER,
    CPTI_COMPLETE_DTOR_IDENTIFIER,
    CPTI_BASE_DTOR_IDENTIFIER,
    CPTI_DELETING_DTOR_IDENTIFIER,
    CPTI_DELTA_IDENTIFIER,
    CPTI_IN_CHARGE_IDENTIFIER,
    CPTI_VTT_PARM_IDENTIFIER,
    CPTI_NELTS_IDENTIFIER,
    CPTI_THIS_IDENTIFIER,
    CPTI_PFN_IDENTIFIER,
    CPTI_VPTR_IDENTIFIER,
    CPTI_STD_IDENTIFIER,

    CPTI_LANG_NAME_C,
    CPTI_LANG_NAME_CPLUSPLUS,
    CPTI_LANG_NAME_JAVA,

    CPTI_EMPTY_EXCEPT_SPEC,
    CPTI_JCLASS,
    CPTI_TERMINATE,
    CPTI_CALL_UNEXPECTED,
    CPTI_ATEXIT,
    CPTI_DSO_HANDLE,
    CPTI_DCAST,

    CPTI_KEYED_CLASSES,

    CPTI_MAX
};

extern GTY(()) tree cp_global_trees[CPTI_MAX];

#define java_byte_type_node		cp_global_trees[CPTI_JAVA_BYTE_TYPE]
#define java_short_type_node		cp_global_trees[CPTI_JAVA_SHORT_TYPE]
#define java_int_type_node		cp_global_trees[CPTI_JAVA_INT_TYPE]
#define java_long_type_node		cp_global_trees[CPTI_JAVA_LONG_TYPE]
#define java_float_type_node		cp_global_trees[CPTI_JAVA_FLOAT_TYPE]
#define java_double_type_node		cp_global_trees[CPTI_JAVA_DOUBLE_TYPE]
#define java_char_type_node		cp_global_trees[CPTI_JAVA_CHAR_TYPE]
#define java_boolean_type_node		cp_global_trees[CPTI_JAVA_BOOLEAN_TYPE]

#define wchar_decl_node			cp_global_trees[CPTI_WCHAR_DECL]
#define vtable_entry_type		cp_global_trees[CPTI_VTABLE_ENTRY_TYPE]
/* The type used to represent an offset by which to adjust the `this'
   pointer in pointer-to-member types.  */
#define delta_type_node			cp_global_trees[CPTI_DELTA_TYPE]
/* The type used to represent an index into the vtable.  */
#define vtable_index_type		cp_global_trees[CPTI_VTABLE_INDEX_TYPE]

#define class_type_node			cp_global_trees[CPTI_CLASS_TYPE]
#define unknown_type_node		cp_global_trees[CPTI_UNKNOWN_TYPE]
#define vtbl_type_node			cp_global_trees[CPTI_VTBL_TYPE]
#define vtbl_ptr_type_node		cp_global_trees[CPTI_VTBL_PTR_TYPE]
#define std_node			cp_global_trees[CPTI_STD]
#define abi_node			cp_global_trees[CPTI_ABI]
#define const_type_info_type_node	cp_global_trees[CPTI_CONST_TYPE_INFO_TYPE]
#define type_info_ptr_type		cp_global_trees[CPTI_TYPE_INFO_PTR_TYPE]
#define abort_fndecl			cp_global_trees[CPTI_ABORT_FNDECL]
#define global_delete_fndecl		cp_global_trees[CPTI_GLOBAL_DELETE_FNDECL]
#define current_aggr			cp_global_trees[CPTI_AGGR_TAG]

/* We cache these tree nodes so as to call get_identifier less
   frequently.  */

/* The name of a constructor that takes an in-charge parameter to
   decide whether or not to construct virtual base classes.  */
#define ctor_identifier			cp_global_trees[CPTI_CTOR_IDENTIFIER]
/* The name of a constructor that constructs virtual base classes.  */
#define complete_ctor_identifier	cp_global_trees[CPTI_COMPLETE_CTOR_IDENTIFIER]
/* The name of a constructor that does not construct virtual base classes.  */
#define base_ctor_identifier		cp_global_trees[CPTI_BASE_CTOR_IDENTIFIER]
/* The name of a destructor that takes an in-charge parameter to
   decide whether or not to destroy virtual base classes and whether
   or not to delete the object.  */
#define dtor_identifier			cp_global_trees[CPTI_DTOR_IDENTIFIER]
/* The name of a destructor that destroys virtual base classes.  */
#define complete_dtor_identifier	cp_global_trees[CPTI_COMPLETE_DTOR_IDENTIFIER]
/* The name of a destructor that does not destroy virtual base
   classes.  */
#define base_dtor_identifier		cp_global_trees[CPTI_BASE_DTOR_IDENTIFIER]
/* The name of a destructor that destroys virtual base classes, and
   then deletes the entire object.  */
#define deleting_dtor_identifier	cp_global_trees[CPTI_DELETING_DTOR_IDENTIFIER]
#define delta_identifier		cp_global_trees[CPTI_DELTA_IDENTIFIER]
#define in_charge_identifier		cp_global_trees[CPTI_IN_CHARGE_IDENTIFIER]
/* The name of the parameter that contains a pointer to the VTT to use
   for this subobject constructor or destructor.  */
#define vtt_parm_identifier		cp_global_trees[CPTI_VTT_PARM_IDENTIFIER]
#define nelts_identifier		cp_global_trees[CPTI_NELTS_IDENTIFIER]
#define this_identifier			cp_global_trees[CPTI_THIS_IDENTIFIER]
#define pfn_identifier			cp_global_trees[CPTI_PFN_IDENTIFIER]
#define vptr_identifier			cp_global_trees[CPTI_VPTR_IDENTIFIER]
/* The name of the std namespace.  */
#define std_identifier			cp_global_trees[CPTI_STD_IDENTIFIER]
#define lang_name_c			cp_global_trees[CPTI_LANG_NAME_C]
#define lang_name_cplusplus		cp_global_trees[CPTI_LANG_NAME_CPLUSPLUS]
#define lang_name_java			cp_global_trees[CPTI_LANG_NAME_JAVA]

/* Exception specifier used for throw().  */
#define empty_except_spec		cp_global_trees[CPTI_EMPTY_EXCEPT_SPEC]

/* If non-NULL, a POINTER_TYPE equivalent to (java::lang::Class*).  */
#define jclass_node			cp_global_trees[CPTI_JCLASS]

/* The declaration for `std::terminate'.  */
#define terminate_node			cp_global_trees[CPTI_TERMINATE]

/* The declaration for "__cxa_call_unexpected".  */
#define call_unexpected_node		cp_global_trees[CPTI_CALL_UNEXPECTED]

/* A pointer to `std::atexit'.  */
#define atexit_node			cp_global_trees[CPTI_ATEXIT]

/* A pointer to `__dso_handle'.  */
#define dso_handle_node			cp_global_trees[CPTI_DSO_HANDLE]

/* The declaration of the dynamic_cast runtime.  */
#define dynamic_cast_node		cp_global_trees[CPTI_DCAST]

/* The type of a destructor.  */
#define cleanup_type			cp_global_trees[CPTI_CLEANUP_TYPE]

/* The type of the vtt parameter passed to subobject constructors and
   destructors.  */
#define vtt_parm_type			cp_global_trees[CPTI_VTT_PARM_TYPE]

/* A TREE_LIST of the dynamic classes whose vtables may have to be
   emitted in this translation unit.  */

#define keyed_classes			cp_global_trees[CPTI_KEYED_CLASSES]

/* Node to indicate default access. This must be distinct from the
   access nodes in tree.h.  */

#define access_default_node		null_node

/* Global state.  */

struct saved_scope GTY(())
{
  VEC(cxx_saved_binding,gc) *old_bindings;
  tree old_namespace;
  tree decl_ns_list;
  tree class_name;
  tree class_type;
  tree access_specifier;
  tree function_decl;
  VEC(tree,gc) *lang_base;
  tree lang_name;
  tree template_parms;
  struct cp_binding_level *x_previous_class_level;
  tree x_saved_tree;

  HOST_WIDE_INT x_processing_template_decl;
  int x_processing_specialization;
  bool x_processing_explicit_instantiation;
  int need_pop_function_context;
  bool skip_evaluation;

  struct stmt_tree_s x_stmt_tree;

  struct cp_binding_level *class_bindings;
  struct cp_binding_level *bindings;

  struct saved_scope *prev;
};

/* The current open namespace.  */

#define current_namespace scope_chain->old_namespace

/* The stack for namespaces of current declarations.  */

#define decl_namespace_list scope_chain->decl_ns_list

/* IDENTIFIER_NODE: name of current class */

#define current_class_name scope_chain->class_name

/* _TYPE: the type of the current class */

#define current_class_type scope_chain->class_type

/* When parsing a class definition, the access specifier most recently
   given by the user, or, if no access specifier was given, the
   default value appropriate for the kind of class (i.e., struct,
   class, or union).  */

#define current_access_specifier scope_chain->access_specifier

/* Pointer to the top of the language name stack.  */

#define current_lang_base scope_chain->lang_base
#define current_lang_name scope_chain->lang_name

/* Parsing a function declarator leaves a list of parameter names
   or a chain or parameter decls here.  */

#define current_template_parms scope_chain->template_parms

#define processing_template_decl scope_chain->x_processing_template_decl
#define processing_specialization scope_chain->x_processing_specialization
#define processing_explicit_instantiation scope_chain->x_processing_explicit_instantiation

/* The cached class binding level, from the most recently exited
   class, or NULL if none.  */

#define previous_class_level scope_chain->x_previous_class_level

/* A list of private types mentioned, for deferred access checking.  */

extern GTY(()) struct saved_scope *scope_chain;

struct cxx_int_tree_map GTY(())
{
  unsigned int uid;
  tree to;
};

extern unsigned int cxx_int_tree_map_hash (const void *);
extern int cxx_int_tree_map_eq (const void *, const void *);

/* Global state pertinent to the current function.  */

struct language_function GTY(())
{
  struct c_language_function base;

  tree x_cdtor_label;
  tree x_current_class_ptr;
  tree x_current_class_ref;
  tree x_eh_spec_block;
  tree x_in_charge_parm;
  tree x_vtt_parm;
  tree x_return_value;

  int returns_value;
  int returns_null;
  int returns_abnormally;
  int in_function_try_handler;
  int in_base_initializer;

  /* True if this function can throw an exception.  */
  BOOL_BITFIELD can_throw : 1;

  htab_t GTY((param_is(struct named_label_entry))) x_named_labels;
  struct cp_binding_level *bindings;
  VEC(tree,gc) *x_local_names;
  htab_t GTY((param_is (struct cxx_int_tree_map))) extern_decl_map;
};

/* The current C++-specific per-function global variables.  */

#define cp_function_chain (cfun->language)

/* In a constructor destructor, the point at which all derived class
   destroying/construction has been has been done. Ie. just before a
   constructor returns, or before any base class destroying will be done
   in a destructor.  */

#define cdtor_label cp_function_chain->x_cdtor_label

/* When we're processing a member function, current_class_ptr is the
   PARM_DECL for the `this' pointer.  The current_class_ref is an
   expression for `*this'.  */

#define current_class_ptr \
  (cfun ? cp_function_chain->x_current_class_ptr : NULL_TREE)
#define current_class_ref \
  (cfun ? cp_function_chain->x_current_class_ref : NULL_TREE)

/* The EH_SPEC_BLOCK for the exception-specifiers for the current
   function, if any.  */

#define current_eh_spec_block cp_function_chain->x_eh_spec_block

/* The `__in_chrg' parameter for the current function.  Only used for
   constructors and destructors.  */

#define current_in_charge_parm cp_function_chain->x_in_charge_parm

/* The `__vtt_parm' parameter for the current function.  Only used for
   constructors and destructors.  */

#define current_vtt_parm cp_function_chain->x_vtt_parm

/* Set to 0 at beginning of a function definition, set to 1 if
   a return statement that specifies a return value is seen.  */

#define current_function_returns_value cp_function_chain->returns_value

/* Set to 0 at beginning of a function definition, set to 1 if
   a return statement with no argument is seen.  */

#define current_function_returns_null cp_function_chain->returns_null

/* Set to 0 at beginning of a function definition, set to 1 if
   a call to a noreturn function is seen.  */

#define current_function_returns_abnormally \
  cp_function_chain->returns_abnormally

/* Nonzero if we are processing a base initializer.  Zero elsewhere.  */
#define in_base_initializer cp_function_chain->in_base_initializer

#define in_function_try_handler cp_function_chain->in_function_try_handler

/* Expression always returned from function, or error_mark_node
   otherwise, for use by the automatic named return value optimization.  */

#define current_function_return_value \
  (cp_function_chain->x_return_value)

/* True if NAME is the IDENTIFIER_NODE for an overloaded "operator
   new" or "operator delete".  */
#define NEW_DELETE_OPNAME_P(NAME)		\
  ((NAME) == ansi_opname (NEW_EXPR)		\
   || (NAME) == ansi_opname (VEC_NEW_EXPR)	\
   || (NAME) == ansi_opname (DELETE_EXPR)	\
   || (NAME) == ansi_opname (VEC_DELETE_EXPR))

#define ansi_opname(CODE) \
  (operator_name_info[(int) (CODE)].identifier)
#define ansi_assopname(CODE) \
  (assignment_operator_name_info[(int) (CODE)].identifier)

/* True if NODE is an erroneous expression.  */

#define error_operand_p(NODE)					\
  ((NODE) == error_mark_node					\
   || ((NODE) && TREE_TYPE ((NODE)) == error_mark_node))

/* C++ language-specific tree codes.  */
#define DEFTREECODE(SYM, NAME, TYPE, LENGTH) SYM,
enum cplus_tree_code {
  CP_DUMMY_TREE_CODE = LAST_C_TREE_CODE,
#include "cp-tree.def"
  LAST_CPLUS_TREE_CODE
};
#undef DEFTREECODE

/* TRUE if a tree code represents a statement.  */
extern bool statement_code_p[MAX_TREE_CODES];

#define STATEMENT_CODE_P(CODE) statement_code_p[(int) (CODE)]

enum languages { lang_c, lang_cplusplus, lang_java };

/* Macros to make error reporting functions' lives easier.  */
#define TYPE_IDENTIFIER(NODE) (DECL_NAME (TYPE_NAME (NODE)))
#define TYPE_LINKAGE_IDENTIFIER(NODE) \
  (TYPE_IDENTIFIER (TYPE_MAIN_VARIANT (NODE)))
#define TYPE_NAME_STRING(NODE) (IDENTIFIER_POINTER (TYPE_IDENTIFIER (NODE)))
#define TYPE_NAME_LENGTH(NODE) (IDENTIFIER_LENGTH (TYPE_IDENTIFIER (NODE)))

/* Nonzero if NODE has no name for linkage purposes.  */
#define TYPE_ANONYMOUS_P(NODE) \
  (TAGGED_TYPE_P (NODE) && ANON_AGGRNAME_P (TYPE_LINKAGE_IDENTIFIER (NODE)))

/* The _DECL for this _TYPE.  */
#define TYPE_MAIN_DECL(NODE) (TYPE_STUB_DECL (TYPE_MAIN_VARIANT (NODE)))

/* Nonzero if T is a class (or struct or union) type.  Also nonzero
   for template type parameters, typename types, and instantiated
   template template parameters.  Despite its name,
   this macro has nothing to do with the definition of aggregate given
   in the standard.  Think of this macro as MAYBE_CLASS_TYPE_P.  Keep
   these checks in ascending code order.  */
#define IS_AGGR_TYPE(T)					\
  (TREE_CODE (T) == TEMPLATE_TYPE_PARM			\
   || TREE_CODE (T) == TYPENAME_TYPE			\
   || TREE_CODE (T) == TYPEOF_TYPE			\
   || TREE_CODE (T) == BOUND_TEMPLATE_TEMPLATE_PARM	\
   || TYPE_LANG_FLAG_5 (T))

/* Set IS_AGGR_TYPE for T to VAL.  T must be a class, struct, or
   union type.  */
#define SET_IS_AGGR_TYPE(T, VAL) \
  (TYPE_LANG_FLAG_5 (T) = (VAL))

/* Nonzero if T is a class type.  Zero for template type parameters,
   typename types, and so forth.  */
#define CLASS_TYPE_P(T) \
  (IS_AGGR_TYPE_CODE (TREE_CODE (T)) && TYPE_LANG_FLAG_5 (T))

/* Keep these checks in ascending code order.  */
#define IS_AGGR_TYPE_CODE(T)	\
  ((T) == RECORD_TYPE || (T) == UNION_TYPE)
#define TAGGED_TYPE_P(T) \
  (CLASS_TYPE_P (T) || TREE_CODE (T) == ENUMERAL_TYPE)
#define IS_OVERLOAD_TYPE(T) TAGGED_TYPE_P (T)

/* True if this a "Java" type, defined in 'extern "Java"'.  */
#define TYPE_FOR_JAVA(NODE) TYPE_LANG_FLAG_3 (NODE)

/* True if this type is dependent.  This predicate is only valid if
   TYPE_DEPENDENT_P_VALID is true.  */
#define TYPE_DEPENDENT_P(NODE) TYPE_LANG_FLAG_0 (NODE)

/* True if dependent_type_p has been called for this type, with the
   result that TYPE_DEPENDENT_P is valid.  */
#define TYPE_DEPENDENT_P_VALID(NODE) TYPE_LANG_FLAG_6(NODE)

/* Nonzero if this type is const-qualified.  */
#define CP_TYPE_CONST_P(NODE)				\
  ((cp_type_quals (NODE) & TYPE_QUAL_CONST) != 0)

/* Nonzero if this type is volatile-qualified.  */
#define CP_TYPE_VOLATILE_P(NODE)			\
  ((cp_type_quals (NODE) & TYPE_QUAL_VOLATILE) != 0)

/* Nonzero if this type is restrict-qualified.  */
#define CP_TYPE_RESTRICT_P(NODE)			\
  ((cp_type_quals (NODE) & TYPE_QUAL_RESTRICT) != 0)

/* Nonzero if this type is const-qualified, but not
   volatile-qualified.  Other qualifiers are ignored.  This macro is
   used to test whether or not it is OK to bind an rvalue to a
   reference.  */
#define CP_TYPE_CONST_NON_VOLATILE_P(NODE)				\
  ((cp_type_quals (NODE) & (TYPE_QUAL_CONST | TYPE_QUAL_VOLATILE))	\
   == TYPE_QUAL_CONST)

#define FUNCTION_ARG_CHAIN(NODE) \
  TREE_CHAIN (TYPE_ARG_TYPES (TREE_TYPE (NODE)))

/* Given a FUNCTION_DECL, returns the first TREE_LIST out of TYPE_ARG_TYPES
   which refers to a user-written parameter.  */
#define FUNCTION_FIRST_USER_PARMTYPE(NODE) \
  skip_artificial_parms_for ((NODE), TYPE_ARG_TYPES (TREE_TYPE (NODE)))

/* Similarly, but for DECL_ARGUMENTS.  */
#define FUNCTION_FIRST_USER_PARM(NODE) \
  skip_artificial_parms_for ((NODE), DECL_ARGUMENTS (NODE))

#define PROMOTES_TO_AGGR_TYPE(NODE, CODE)	\
  (((CODE) == TREE_CODE (NODE)			\
    && IS_AGGR_TYPE (TREE_TYPE (NODE)))		\
   || IS_AGGR_TYPE (NODE))

/* Nonzero iff TYPE is derived from PARENT. Ignores accessibility and
   ambiguity issues.  */
#define DERIVED_FROM_P(PARENT, TYPE) \
  (lookup_base ((TYPE), (PARENT), ba_any, NULL) != NULL_TREE)
/* Nonzero iff TYPE is uniquely derived from PARENT. Ignores
   accessibility.  */
#define UNIQUELY_DERIVED_FROM_P(PARENT, TYPE) \
  (lookup_base ((TYPE), (PARENT), ba_unique | ba_quiet, NULL) != NULL_TREE)
/* Nonzero iff TYPE is publicly & uniquely derived from PARENT.  */
#define PUBLICLY_UNIQUELY_DERIVED_P(PARENT, TYPE) \
  (lookup_base ((TYPE), (PARENT), ba_ignore_scope | ba_check | ba_quiet, \
		NULL) != NULL_TREE)

/* Gives the visibility specification for a class type.  */
#define CLASSTYPE_VISIBILITY(TYPE)		\
	DECL_VISIBILITY (TYPE_NAME (TYPE))
#define CLASSTYPE_VISIBILITY_SPECIFIED(TYPE)	\
	DECL_VISIBILITY_SPECIFIED (TYPE_NAME (TYPE))

typedef struct tree_pair_s GTY (())
{
  tree purpose;
  tree value;
} tree_pair_s;
typedef tree_pair_s *tree_pair_p;
DEF_VEC_O (tree_pair_s);
DEF_VEC_ALLOC_O (tree_pair_s,gc);

/* This is a few header flags for 'struct lang_type'.  Actually,
   all but the first are used only for lang_type_class; they
   are put in this structure to save space.  */
struct lang_type_header GTY(())
{
  BOOL_BITFIELD is_lang_type_class : 1;

  BOOL_BITFIELD has_type_conversion : 1;
  BOOL_BITFIELD has_init_ref : 1;
  BOOL_BITFIELD has_default_ctor : 1;
  BOOL_BITFIELD const_needs_init : 1;
  BOOL_BITFIELD ref_needs_init : 1;
  BOOL_BITFIELD has_const_assign_ref : 1;

  BOOL_BITFIELD spare : 1;
};

/* This structure provides additional information above and beyond
   what is provide in the ordinary tree_type.  In the past, we used it
   for the types of class types, template parameters types, typename
   types, and so forth.  However, there can be many (tens to hundreds
   of thousands) of template parameter types in a compilation, and
   there's no need for this additional information in that case.
   Therefore, we now use this data structure only for class types.

   In the past, it was thought that there would be relatively few
   class types.  However, in the presence of heavy use of templates,
   many (i.e., thousands) of classes can easily be generated.
   Therefore, we should endeavor to keep the size of this structure to
   a minimum.  */
struct lang_type_class GTY(())
{
  struct lang_type_header h;

  unsigned char align;

  unsigned has_mutable : 1;
  unsigned com_interface : 1;
  unsigned non_pod_class : 1;
  unsigned nearly_empty_p : 1;
  unsigned user_align : 1;
  unsigned has_assign_ref : 1;
  unsigned has_new : 1;
  unsigned has_array_new : 1;

  unsigned gets_delete : 2;
  unsigned interface_only : 1;
  unsigned interface_unknown : 1;
  unsigned contains_empty_class_p : 1;
  unsigned anon_aggr : 1;
  unsigned non_zero_init : 1;
  unsigned empty_p : 1;

  unsigned vec_new_uses_cookie : 1;
  unsigned declared_class : 1;
  unsigned diamond_shaped : 1;
  unsigned repeated_base : 1;
  unsigned being_defined : 1;
  unsigned java_interface : 1;
  unsigned debug_requested : 1;
  unsigned fields_readonly : 1;

  unsigned use_template : 2;
  unsigned ptrmemfunc_flag : 1;
  unsigned was_anonymous : 1;
  unsigned lazy_default_ctor : 1;
  unsigned lazy_copy_ctor : 1;
  unsigned lazy_assignment_op : 1;
  unsigned lazy_destructor : 1;

  unsigned has_const_init_ref : 1;
  unsigned has_complex_init_ref : 1;
  unsigned has_complex_assign_ref : 1;
  unsigned non_aggregate : 1;

  /* APPLE LOCAL begin omit calls to empty destructors 5559195 */
  unsigned has_nontrivial_destructor_body : 1;
  unsigned destructor_nontrivial_because_of_base : 1;
  unsigned destructor_triviality_final : 1;
  /* APPLE LOCAL end omit calls to empty destructors 5559195 */


  /* When adding a flag here, consider whether or not it ought to
     apply to a template instance if it applies to the template.  If
     so, make sure to copy it in instantiate_class_template!  */

  /* There are some bits left to fill out a 32-bit word.  Keep track
     of this by updating the size of this bitfield whenever you add or
     remove a flag.  */
  /* APPLE LOCAL begin omit calls to empty destructors 5559195 */
  unsigned dummy : 10;
  /* APPLE LOCAL end omit calls to empty destructors 5559195 */

  tree primary_base;
  VEC(tree_pair_s,gc) *vcall_indices;
  tree vtables;
  tree typeinfo_var;
  VEC(tree,gc) *vbases;
  binding_table nested_udts;
  tree as_base;
  VEC(tree,gc) *pure_virtuals;
  tree friend_classes;
  VEC(tree,gc) * GTY((reorder ("resort_type_method_vec"))) methods;
  tree key_method;
  tree decl_list;
  tree template_info;
  tree befriending_classes;
  /* In a RECORD_TYPE, information specific to Objective-C++, such
     as a list of adopted protocols or a pointer to a corresponding
     @interface.  See objc/objc-act.h for details.  */
  tree objc_info;
};

struct lang_type_ptrmem GTY(())
{
  struct lang_type_header h;
  tree record;
};

struct lang_type GTY(())
{
  union lang_type_u
  {
    struct lang_type_header GTY((skip (""))) h;
    struct lang_type_class  GTY((tag ("1"))) c;
    struct lang_type_ptrmem GTY((tag ("0"))) ptrmem;
  } GTY((desc ("%h.h.is_lang_type_class"))) u;
};

#if defined ENABLE_TREE_CHECKING && (GCC_VERSION >= 2007)

#define LANG_TYPE_CLASS_CHECK(NODE) __extension__		\
({  struct lang_type *lt = TYPE_LANG_SPECIFIC (NODE);		\
    if (! lt->u.h.is_lang_type_class)				\
      lang_check_failed (__FILE__, __LINE__, __FUNCTION__);	\
    &lt->u.c; })

#define LANG_TYPE_PTRMEM_CHECK(NODE) __extension__		\
({  struct lang_type *lt = TYPE_LANG_SPECIFIC (NODE);		\
    if (lt->u.h.is_lang_type_class)				\
      lang_check_failed (__FILE__, __LINE__, __FUNCTION__);	\
    &lt->u.ptrmem; })

#else

#define LANG_TYPE_CLASS_CHECK(NODE) (&TYPE_LANG_SPECIFIC (NODE)->u.c)
#define LANG_TYPE_PTRMEM_CHECK(NODE) (&TYPE_LANG_SPECIFIC (NODE)->u.ptrmem)

#endif /* ENABLE_TREE_CHECKING */

/* Fields used for storing information before the class is defined.
   After the class is defined, these fields hold other information.  */

/* VEC(tree) of friends which were defined inline in this class
   definition.  */
#define CLASSTYPE_INLINE_FRIENDS(NODE) CLASSTYPE_PURE_VIRTUALS (NODE)

/* Nonzero for _CLASSTYPE means that operator delete is defined.  */
#define TYPE_GETS_DELETE(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->gets_delete)
#define TYPE_GETS_REG_DELETE(NODE) (TYPE_GETS_DELETE (NODE) & 1)

/* Nonzero if `new NODE[x]' should cause the allocation of extra
   storage to indicate how many array elements are in use.  */
#define TYPE_VEC_NEW_USES_COOKIE(NODE)			\
  (CLASS_TYPE_P (NODE)					\
   && LANG_TYPE_CLASS_CHECK (NODE)->vec_new_uses_cookie)

/* Nonzero means that this _CLASSTYPE node defines ways of converting
   itself to other types.  */
#define TYPE_HAS_CONVERSION(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->h.has_type_conversion)

/* Nonzero means that NODE (a class type) has a default constructor --
   but that it has not yet been declared.  */
#define CLASSTYPE_LAZY_DEFAULT_CTOR(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->lazy_default_ctor)

/* Nonzero means that NODE (a class type) has a copy constructor --
   but that it has not yet been declared.  */
#define CLASSTYPE_LAZY_COPY_CTOR(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->lazy_copy_ctor)

/* Nonzero means that NODE (a class type) has an assignment operator
   -- but that it has not yet been declared.  */
#define CLASSTYPE_LAZY_ASSIGNMENT_OP(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->lazy_assignment_op)

/* Nonzero means that NODE (a class type) has a destructor -- but that
   it has not yet been declared.  */
#define CLASSTYPE_LAZY_DESTRUCTOR(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->lazy_destructor)

/* Nonzero means that this _CLASSTYPE node overloads operator=(X&).  */
#define TYPE_HAS_ASSIGN_REF(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->has_assign_ref)

/* True iff the class type NODE has an "operator =" whose parameter
   has a parameter of type "const X&".  */
#define TYPE_HAS_CONST_ASSIGN_REF(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->h.has_const_assign_ref)

/* Nonzero means that this _CLASSTYPE node has an X(X&) constructor.  */
#define TYPE_HAS_INIT_REF(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->h.has_init_ref)
#define TYPE_HAS_CONST_INIT_REF(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->has_const_init_ref)

/* Nonzero if this class defines an overloaded operator new.  (An
   operator new [] doesn't count.)  */
#define TYPE_HAS_NEW_OPERATOR(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->has_new)

/* Nonzero if this class defines an overloaded operator new[].  */
#define TYPE_HAS_ARRAY_NEW_OPERATOR(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->has_array_new)

/* Nonzero means that this type is being defined.  I.e., the left brace
   starting the definition of this type has been seen.  */
#define TYPE_BEING_DEFINED(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->being_defined)

/* Mark bits for repeated base checks.  */
#define TYPE_MARKED_P(NODE) TREE_LANG_FLAG_6 (TYPE_CHECK (NODE))

/* Nonzero if the class NODE has multiple paths to the same (virtual)
   base object.  */
#define CLASSTYPE_DIAMOND_SHAPED_P(NODE) \
  (LANG_TYPE_CLASS_CHECK(NODE)->diamond_shaped)

/* Nonzero if the class NODE has multiple instances of the same base
   type.  */
#define CLASSTYPE_REPEATED_BASE_P(NODE) \
  (LANG_TYPE_CLASS_CHECK(NODE)->repeated_base)

/* The member function with which the vtable will be emitted:
   the first noninline non-pure-virtual member function.  NULL_TREE
   if there is no key function or if this is a class template */
#define CLASSTYPE_KEY_METHOD(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->key_method)

/* Vector member functions defined in this class.  Each element is
   either a FUNCTION_DECL, a TEMPLATE_DECL, or an OVERLOAD.  All
   functions with the same name end up in the same slot.  The first
   two elements are for constructors, and destructors, respectively.
   All template conversion operators to innermost template dependent
   types are overloaded on the next slot, if they exist.  Note, the
   names for these functions will not all be the same.  The
   non-template conversion operators & templated conversions to
   non-innermost template types are next, followed by ordinary member
   functions.  There may be empty entries at the end of the vector.
   The conversion operators are unsorted. The ordinary member
   functions are sorted, once the class is complete.  */
#define CLASSTYPE_METHOD_VEC(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->methods)

/* For class templates, this is a TREE_LIST of all member data,
   functions, types, and friends in the order of declaration.
   The TREE_PURPOSE of each TREE_LIST is NULL_TREE for a friend,
   and the RECORD_TYPE for the class template otherwise.  */
#define CLASSTYPE_DECL_LIST(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->decl_list)

/* The slot in the CLASSTYPE_METHOD_VEC where constructors go.  */
#define CLASSTYPE_CONSTRUCTOR_SLOT 0

/* The slot in the CLASSTYPE_METHOD_VEC where destructors go.  */
#define CLASSTYPE_DESTRUCTOR_SLOT 1

/* The first slot in the CLASSTYPE_METHOD_VEC where conversion
   operators can appear.  */
#define CLASSTYPE_FIRST_CONVERSION_SLOT 2

/* A FUNCTION_DECL or OVERLOAD for the constructors for NODE.  These
   are the constructors that take an in-charge parameter.  */
#define CLASSTYPE_CONSTRUCTORS(NODE) \
  (VEC_index (tree, CLASSTYPE_METHOD_VEC (NODE), CLASSTYPE_CONSTRUCTOR_SLOT))

/* A FUNCTION_DECL for the destructor for NODE.  These are the
   destructors that take an in-charge parameter.  If
   CLASSTYPE_LAZY_DESTRUCTOR is true, then this entry will be NULL
   until the destructor is created with lazily_declare_fn.  */
#define CLASSTYPE_DESTRUCTORS(NODE) \
  (CLASSTYPE_METHOD_VEC (NODE)						      \
   ? VEC_index (tree, CLASSTYPE_METHOD_VEC (NODE), CLASSTYPE_DESTRUCTOR_SLOT) \
   : NULL_TREE)

/* A dictionary of the nested user-defined-types (class-types, or enums)
   found within this class.  This table includes nested member class
   templates.  */
#define CLASSTYPE_NESTED_UTDS(NODE) \
   (LANG_TYPE_CLASS_CHECK (NODE)->nested_udts)

/* Nonzero if NODE has a primary base class, i.e., a base class with
   which it shares the virtual function table pointer.  */
#define CLASSTYPE_HAS_PRIMARY_BASE_P(NODE) \
  (CLASSTYPE_PRIMARY_BINFO (NODE) != NULL_TREE)

/* If non-NULL, this is the binfo for the primary base class, i.e.,
   the base class which contains the virtual function table pointer
   for this class.  */
#define CLASSTYPE_PRIMARY_BINFO(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->primary_base)

/* A vector of BINFOs for the direct and indirect virtual base classes
   that this type uses in a post-order depth-first left-to-right
   order.  (In other words, these bases appear in the order that they
   should be initialized.)  */
#define CLASSTYPE_VBASECLASSES(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->vbases)

/* The type corresponding to NODE when NODE is used as a base class,
   i.e., NODE without virtual base classes.  */

#define CLASSTYPE_AS_BASE(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->as_base)

/* True iff NODE is the CLASSTYPE_AS_BASE version of some type.  */

#define IS_FAKE_BASE_TYPE(NODE)					\
  (TREE_CODE (NODE) == RECORD_TYPE				\
   && TYPE_CONTEXT (NODE) && CLASS_TYPE_P (TYPE_CONTEXT (NODE))	\
   && CLASSTYPE_AS_BASE (TYPE_CONTEXT (NODE)) == (NODE))

/* These are the size and alignment of the type without its virtual
   base classes, for when we use this type as a base itself.  */
#define CLASSTYPE_SIZE(NODE) TYPE_SIZE (CLASSTYPE_AS_BASE (NODE))
#define CLASSTYPE_SIZE_UNIT(NODE) TYPE_SIZE_UNIT (CLASSTYPE_AS_BASE (NODE))
#define CLASSTYPE_ALIGN(NODE) TYPE_ALIGN (CLASSTYPE_AS_BASE (NODE))
#define CLASSTYPE_USER_ALIGN(NODE) TYPE_USER_ALIGN (CLASSTYPE_AS_BASE (NODE))

/* The alignment of NODE, without its virtual bases, in bytes.  */
#define CLASSTYPE_ALIGN_UNIT(NODE) \
  (CLASSTYPE_ALIGN (NODE) / BITS_PER_UNIT)

/* True if this a Java interface type, declared with
   '__attribute__ ((java_interface))'.  */
#define TYPE_JAVA_INTERFACE(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->java_interface)

/* A VEC(tree) of virtual functions which cannot be inherited by
   derived classes.  When deriving from this type, the derived
   class must provide its own definition for each of these functions.  */
#define CLASSTYPE_PURE_VIRTUALS(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->pure_virtuals)

/* Nonzero means that this type has an X() constructor.  */
#define TYPE_HAS_DEFAULT_CONSTRUCTOR(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->h.has_default_ctor)

/* Nonzero means that this type contains a mutable member.  */
#define CLASSTYPE_HAS_MUTABLE(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->has_mutable)
#define TYPE_HAS_MUTABLE_P(NODE) (cp_has_mutable_p (NODE))

/* Nonzero means that this class type is a non-POD class.  */
#define CLASSTYPE_NON_POD_P(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->non_pod_class)

/* Nonzero means that this class contains pod types whose default
   initialization is not a zero initialization (namely, pointers to
   data members).  */
#define CLASSTYPE_NON_ZERO_INIT_P(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->non_zero_init)

/* Nonzero if this class is "empty" in the sense of the C++ ABI.  */
#define CLASSTYPE_EMPTY_P(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->empty_p)

/* Nonzero if this class is "nearly empty", i.e., contains only a
   virtual function table pointer.  */
#define CLASSTYPE_NEARLY_EMPTY_P(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->nearly_empty_p)

/* Nonzero if this class contains an empty subobject.  */
#define CLASSTYPE_CONTAINS_EMPTY_CLASS_P(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->contains_empty_class_p)

/* A list of class types of which this type is a friend.  The
   TREE_VALUE is normally a TYPE, but will be a TEMPLATE_DECL in the
   case of a template friend.  */
#define CLASSTYPE_FRIEND_CLASSES(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->friend_classes)

/* A list of the classes which grant friendship to this class.  */
#define CLASSTYPE_BEFRIENDING_CLASSES(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->befriending_classes)

/* Say whether this node was declared as a "class" or a "struct".  */
#define CLASSTYPE_DECLARED_CLASS(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->declared_class)

/* Nonzero if this class has const members
   which have no specified initialization.  */
#define CLASSTYPE_READONLY_FIELDS_NEED_INIT(NODE)	\
  (TYPE_LANG_SPECIFIC (NODE)				\
   ? LANG_TYPE_CLASS_CHECK (NODE)->h.const_needs_init : 0)
#define SET_CLASSTYPE_READONLY_FIELDS_NEED_INIT(NODE, VALUE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->h.const_needs_init = (VALUE))

/* Nonzero if this class has ref members
   which have no specified initialization.  */
#define CLASSTYPE_REF_FIELDS_NEED_INIT(NODE)		\
  (TYPE_LANG_SPECIFIC (NODE)				\
   ? LANG_TYPE_CLASS_CHECK (NODE)->h.ref_needs_init : 0)
#define SET_CLASSTYPE_REF_FIELDS_NEED_INIT(NODE, VALUE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->h.ref_needs_init = (VALUE))

/* Nonzero if this class is included from a header file which employs
   `#pragma interface', and it is not included in its implementation file.  */
#define CLASSTYPE_INTERFACE_ONLY(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->interface_only)

/* True if we have already determined whether or not vtables, VTTs,
   typeinfo, and other similar per-class data should be emitted in
   this translation unit.  This flag does not indicate whether or not
   these items should be emitted; it only indicates that we know one
   way or the other.  */
#define CLASSTYPE_INTERFACE_KNOWN(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->interface_unknown == 0)
/* The opposite of CLASSTYPE_INTERFACE_KNOWN.  */
#define CLASSTYPE_INTERFACE_UNKNOWN(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->interface_unknown)

#define SET_CLASSTYPE_INTERFACE_UNKNOWN_X(NODE,X) \
  (LANG_TYPE_CLASS_CHECK (NODE)->interface_unknown = !!(X))
#define SET_CLASSTYPE_INTERFACE_UNKNOWN(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->interface_unknown = 1)
#define SET_CLASSTYPE_INTERFACE_KNOWN(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->interface_unknown = 0)

/* Nonzero if a _DECL node requires us to output debug info for this class.  */
#define CLASSTYPE_DEBUG_REQUESTED(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->debug_requested)

/* Additional macros for inheritance information.  */

/* Nonzero means that this class is on a path leading to a new vtable.  */
#define BINFO_VTABLE_PATH_MARKED(NODE) BINFO_FLAG_1 (NODE)

/* Nonzero means B (a BINFO) has its own vtable.  Any copies will not
   have this flag set.  */
#define BINFO_NEW_VTABLE_MARKED(B) (BINFO_FLAG_2 (B))

/* Compare a BINFO_TYPE with another type for equality.  For a binfo,
   this is functionally equivalent to using same_type_p, but
   measurably faster.  At least one of the arguments must be a
   BINFO_TYPE.  The other can be a BINFO_TYPE or a regular type.  If
   BINFO_TYPE(T) ever stops being the main variant of the class the
   binfo is for, this macro must change.  */
#define SAME_BINFO_TYPE_P(A, B) ((A) == (B))

/* Any subobject that needs a new vtable must have a vptr and must not
   be a non-virtual primary base (since it would then use the vtable from a
   derived class and never become non-primary.)  */
#define SET_BINFO_NEW_VTABLE_MARKED(B)					 \
  (BINFO_NEW_VTABLE_MARKED (B) = 1,					 \
   gcc_assert (!BINFO_PRIMARY_P (B) || BINFO_VIRTUAL_P (B)),		 \
   gcc_assert (TYPE_VFIELD (BINFO_TYPE (B))))

/* Nonzero if this binfo is for a dependent base - one that should not
   be searched.  */
#define BINFO_DEPENDENT_BASE_P(NODE) BINFO_FLAG_3 (NODE)

/* Nonzero if this binfo has lost its primary base binfo (because that
   is a nearly-empty virtual base that has been taken by some other
   base in the complete hierarchy.  */
#define BINFO_LOST_PRIMARY_P(NODE) BINFO_FLAG_4 (NODE)

/* Nonzero if this BINFO is a primary base class.  */
#define BINFO_PRIMARY_P(NODE) BINFO_FLAG_5(NODE)

/* Used by various search routines.  */
#define IDENTIFIER_MARKED(NODE) TREE_LANG_FLAG_0 (NODE)

/* A VEC(tree_pair_s) of the vcall indices associated with the class
   NODE.  The PURPOSE of each element is a FUNCTION_DECL for a virtual
   function.  The VALUE is the index into the virtual table where the
   vcall offset for that function is stored, when NODE is a virtual
   base.  */
#define CLASSTYPE_VCALL_INDICES(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->vcall_indices)

/* The various vtables for the class NODE.  The primary vtable will be
   first, followed by the construction vtables and VTT, if any.  */
#define CLASSTYPE_VTABLES(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->vtables)

/* The std::type_info variable representing this class, or NULL if no
   such variable has been created.  This field is only set for the
   TYPE_MAIN_VARIANT of the class.  */
#define CLASSTYPE_TYPEINFO_VAR(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->typeinfo_var)

/* Accessor macros for the BINFO_VIRTUALS list.  */

/* The number of bytes by which to adjust the `this' pointer when
   calling this virtual function.  Subtract this value from the this
   pointer. Always non-NULL, might be constant zero though.  */
#define BV_DELTA(NODE) (TREE_PURPOSE (NODE))

/* If non-NULL, the vtable index at which to find the vcall offset
   when calling this virtual function.  Add the value at that vtable
   index to the this pointer.  */
#define BV_VCALL_INDEX(NODE) (TREE_TYPE (NODE))

/* The function to call.  */
#define BV_FN(NODE) (TREE_VALUE (NODE))


/* For FUNCTION_TYPE or METHOD_TYPE, a list of the exceptions that
   this type can raise.  Each TREE_VALUE is a _TYPE.  The TREE_VALUE
   will be NULL_TREE to indicate a throw specification of `()', or
   no exceptions allowed.  */
#define TYPE_RAISES_EXCEPTIONS(NODE) TYPE_LANG_SLOT_1 (NODE)

/* For FUNCTION_TYPE or METHOD_TYPE, return 1 iff it is declared `throw()'.  */
#define TYPE_NOTHROW_P(NODE) \
  (TYPE_RAISES_EXCEPTIONS (NODE) \
   && TREE_VALUE (TYPE_RAISES_EXCEPTIONS (NODE)) == NULL_TREE)

/* The binding level associated with the namespace.  */
#define NAMESPACE_LEVEL(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.u.level)


/* If a DECL has DECL_LANG_SPECIFIC, it is either a lang_decl_flags or
   a lang_decl (which has lang_decl_flags as its initial prefix).
   This macro is nonzero for tree nodes whose DECL_LANG_SPECIFIC is
   the full lang_decl, and not just lang_decl_flags.  Keep these
   checks in ascending code order.  */
#define CAN_HAVE_FULL_LANG_DECL_P(NODE)			\
  (!(TREE_CODE (NODE) == FIELD_DECL			\
     || TREE_CODE (NODE) == VAR_DECL			\
     || TREE_CODE (NODE) == CONST_DECL			\
     || TREE_CODE (NODE) == USING_DECL))

struct lang_decl_flags GTY(())
{
  ENUM_BITFIELD(languages) language : 4;
  unsigned global_ctor_p : 1;
  unsigned global_dtor_p : 1;
  unsigned anticipated_p : 1;
  unsigned template_conv_p : 1;

  unsigned operator_attr : 1;
  unsigned constructor_attr : 1;
  unsigned destructor_attr : 1;
  unsigned friend_attr : 1;
  unsigned static_function : 1;
  unsigned pure_virtual : 1;
  unsigned has_in_charge_parm_p : 1;
  unsigned has_vtt_parm_p : 1;

  unsigned deferred : 1;
  unsigned use_template : 2;
  unsigned nonconverting : 1;
  unsigned not_really_extern : 1;
  unsigned initialized_in_class : 1;
  unsigned assignment_operator_p : 1;
  unsigned u1sel : 1;

  unsigned u2sel : 1;
  unsigned can_be_full : 1;
  unsigned thunk_p : 1;
  unsigned this_thunk_p : 1;
  unsigned repo_available_p : 1;
  unsigned hidden_friend_p : 1;
  unsigned threadprivate_p : 1;
  /* One unused bit.  */

  union lang_decl_u {
    /* In a FUNCTION_DECL for which DECL_THUNK_P holds, this is
       THUNK_ALIAS.
       In a FUNCTION_DECL for which DECL_THUNK_P does not hold,
       VAR_DECL, TYPE_DECL, or TEMPLATE_DECL, this is
       DECL_TEMPLATE_INFO.  */
    tree GTY ((tag ("0"))) template_info;

    /* In a NAMESPACE_DECL, this is NAMESPACE_LEVEL.  */
    struct cp_binding_level * GTY ((tag ("1"))) level;
  } GTY ((desc ("%1.u1sel"))) u;

  union lang_decl_u2 {
    /* In a FUNCTION_DECL for which DECL_THUNK_P holds, this is
       THUNK_VIRTUAL_OFFSET.
       Otherwise this is DECL_ACCESS.  */
    tree GTY ((tag ("0"))) access;

    /* For VAR_DECL in function, this is DECL_DISCRIMINATOR.  */
    int GTY ((tag ("1"))) discriminator;
  } GTY ((desc ("%1.u2sel"))) u2;
};

/* sorted_fields is sorted based on a pointer, so we need to be able
   to resort it if pointers get rearranged.  */

struct lang_decl GTY(())
{
  struct lang_decl_flags decl_flags;

  union lang_decl_u4
    {
      struct full_lang_decl
      {
	/* In an overloaded operator, this is the value of
	   DECL_OVERLOADED_OPERATOR_P.  */
	ENUM_BITFIELD (tree_code) operator_code : 8;

	unsigned u3sel : 1;
	unsigned pending_inline_p : 1;
	unsigned spare : 22;

	/* For a non-thunk function decl, this is a tree list of
	   friendly classes. For a thunk function decl, it is the
	   thunked to function decl.  */
	tree befriending_classes;

	/* For a non-virtual FUNCTION_DECL, this is
	   DECL_FRIEND_CONTEXT.  For a virtual FUNCTION_DECL for which
	   DECL_THIS_THUNK_P does not hold, this is DECL_THUNKS. Both
	   this pointer and result pointer adjusting thunks are
	   chained here.  This pointer thunks to return pointer thunks
	   will be chained on the return pointer thunk.  */
	tree context;

	union lang_decl_u5
	{
	  /* In a non-thunk FUNCTION_DECL or TEMPLATE_DECL, this is
	     DECL_CLONED_FUNCTION.  */
	  tree GTY ((tag ("0"))) cloned_function;

	  /* In a FUNCTION_DECL for which THUNK_P holds this is the
	     THUNK_FIXED_OFFSET.  */
	  HOST_WIDE_INT GTY ((tag ("1"))) fixed_offset;
	} GTY ((desc ("%0.decl_flags.thunk_p"))) u5;

	union lang_decl_u3
	{
	  struct sorted_fields_type * GTY ((tag ("0"), reorder ("resort_sorted_fields")))
	       sorted_fields;
	  struct cp_token_cache * GTY ((tag ("2"))) pending_inline_info;
	  struct language_function * GTY ((tag ("1")))
	       saved_language_function;
	} GTY ((desc ("%1.u3sel + %1.pending_inline_p"))) u;
      } GTY ((tag ("1"))) f;
  } GTY ((desc ("%1.decl_flags.can_be_full"))) u;
};

#if defined ENABLE_TREE_CHECKING && (GCC_VERSION >= 2007)

#define LANG_DECL_U2_CHECK(NODE, TF) __extension__		\
({  struct lang_decl *lt = DECL_LANG_SPECIFIC (NODE);		\
    if (lt->decl_flags.u2sel != TF)				\
      lang_check_failed (__FILE__, __LINE__, __FUNCTION__);	\
    &lt->decl_flags.u2; })

#else

#define LANG_DECL_U2_CHECK(NODE, TF) \
  (&DECL_LANG_SPECIFIC (NODE)->decl_flags.u2)

#endif /* ENABLE_TREE_CHECKING */

/* For a FUNCTION_DECL or a VAR_DECL, the language linkage for the
   declaration.  Some entities (like a member function in a local
   class, or a local variable) do not have linkage at all, and this
   macro should not be used in those cases.

   Implementation note: A FUNCTION_DECL without DECL_LANG_SPECIFIC was
   created by language-independent code, and has C linkage.  Most
   VAR_DECLs have C++ linkage, and do not have DECL_LANG_SPECIFIC, but
   we do create DECL_LANG_SPECIFIC for variables with non-C++ linkage.  */
#define DECL_LANGUAGE(NODE)				\
  (DECL_LANG_SPECIFIC (NODE)				\
   ? DECL_LANG_SPECIFIC (NODE)->decl_flags.language	\
   : (TREE_CODE (NODE) == FUNCTION_DECL			\
      ? lang_c : lang_cplusplus))

/* Set the language linkage for NODE to LANGUAGE.  */
#define SET_DECL_LANGUAGE(NODE, LANGUAGE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.language = (LANGUAGE))

/* For FUNCTION_DECLs: nonzero means that this function is a constructor.  */
#define DECL_CONSTRUCTOR_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.constructor_attr)

/* Nonzero if NODE (a FUNCTION_DECL) is a constructor for a complete
   object.  */
#define DECL_COMPLETE_CONSTRUCTOR_P(NODE)		\
  (DECL_CONSTRUCTOR_P (NODE)				\
   && DECL_NAME (NODE) == complete_ctor_identifier)

/* Nonzero if NODE (a FUNCTION_DECL) is a constructor for a base
   object.  */
#define DECL_BASE_CONSTRUCTOR_P(NODE)		\
  (DECL_CONSTRUCTOR_P (NODE)			\
   && DECL_NAME (NODE) == base_ctor_identifier)

/* Nonzero if NODE (a FUNCTION_DECL) is a constructor, but not either the
   specialized in-charge constructor or the specialized not-in-charge
   constructor.  */
#define DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P(NODE)		\
  (DECL_CONSTRUCTOR_P (NODE) && !DECL_CLONED_FUNCTION_P (NODE))

/* Nonzero if NODE (a FUNCTION_DECL) is a copy constructor.  */
#define DECL_COPY_CONSTRUCTOR_P(NODE) \
  (DECL_CONSTRUCTOR_P (NODE) && copy_fn_p (NODE) > 0)

/* Nonzero if NODE is a destructor.  */
#define DECL_DESTRUCTOR_P(NODE)				\
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.destructor_attr)

/* Nonzero if NODE (a FUNCTION_DECL) is a destructor, but not the
   specialized in-charge constructor, in-charge deleting constructor,
   or the base destructor.  */
#define DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P(NODE)			\
  (DECL_DESTRUCTOR_P (NODE) && !DECL_CLONED_FUNCTION_P (NODE))

/* Nonzero if NODE (a FUNCTION_DECL) is a destructor for a complete
   object.  */
#define DECL_COMPLETE_DESTRUCTOR_P(NODE)		\
  (DECL_DESTRUCTOR_P (NODE)				\
   && DECL_NAME (NODE) == complete_dtor_identifier)

/* Nonzero if NODE (a FUNCTION_DECL) is a destructor for a base
   object.  */
#define DECL_BASE_DESTRUCTOR_P(NODE)		\
  (DECL_DESTRUCTOR_P (NODE)			\
   && DECL_NAME (NODE) == base_dtor_identifier)

/* Nonzero if NODE (a FUNCTION_DECL) is a destructor for a complete
   object that deletes the object after it has been destroyed.  */
#define DECL_DELETING_DESTRUCTOR_P(NODE)		\
  (DECL_DESTRUCTOR_P (NODE)				\
   && DECL_NAME (NODE) == deleting_dtor_identifier)

/* Nonzero if NODE (a FUNCTION_DECL) is a cloned constructor or
   destructor.  */
#define DECL_CLONED_FUNCTION_P(NODE)			\
  ((TREE_CODE (NODE) == FUNCTION_DECL			\
    || TREE_CODE (NODE) == TEMPLATE_DECL)		\
   && DECL_LANG_SPECIFIC (NODE)				\
   && !DECL_LANG_SPECIFIC (NODE)->decl_flags.thunk_p	\
   && DECL_CLONED_FUNCTION (NODE) != NULL_TREE)

/* If DECL_CLONED_FUNCTION_P holds, this is the function that was
   cloned.  */
#define DECL_CLONED_FUNCTION(NODE) \
  (DECL_LANG_SPECIFIC (NON_THUNK_FUNCTION_CHECK(NODE))->u.f.u5.cloned_function)

/* Perform an action for each clone of FN, if FN is a function with
   clones.  This macro should be used like:

      FOR_EACH_CLONE (clone, fn)
	{ ... }

  */
#define FOR_EACH_CLONE(CLONE, FN)			\
  if (TREE_CODE (FN) == FUNCTION_DECL			\
      && (DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P (FN)	\
	  || DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P (FN)))	\
     for (CLONE = TREE_CHAIN (FN);			\
	  CLONE && DECL_CLONED_FUNCTION_P (CLONE);	\
	  CLONE = TREE_CHAIN (CLONE))

/* Nonzero if NODE has DECL_DISCRIMINATOR and not DECL_ACCESS.  */
#define DECL_DISCRIMINATOR_P(NODE)	\
  (TREE_CODE (NODE) == VAR_DECL		\
   && DECL_FUNCTION_SCOPE_P (NODE))

/* Discriminator for name mangling.  */
#define DECL_DISCRIMINATOR(NODE) (LANG_DECL_U2_CHECK (NODE, 1)->discriminator)

/* Nonzero if the VTT parm has been added to NODE.  */
#define DECL_HAS_VTT_PARM_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.has_vtt_parm_p)

/* Nonzero if NODE is a FUNCTION_DECL for which a VTT parameter is
   required.  */
#define DECL_NEEDS_VTT_PARM_P(NODE)			\
  (CLASSTYPE_VBASECLASSES (DECL_CONTEXT (NODE))		\
   && (DECL_BASE_CONSTRUCTOR_P (NODE)			\
       || DECL_BASE_DESTRUCTOR_P (NODE)))

/* Nonzero if NODE is a user-defined conversion operator.  */
#define DECL_CONV_FN_P(NODE) \
  (DECL_NAME (NODE) && IDENTIFIER_TYPENAME_P (DECL_NAME (NODE)))

/* If FN is a conversion operator, the type to which it converts.
   Otherwise, NULL_TREE.  */
#define DECL_CONV_FN_TYPE(FN) \
  (DECL_CONV_FN_P (FN) ? TREE_TYPE (DECL_NAME (FN)) : NULL_TREE)

/* Nonzero if NODE, which is a TEMPLATE_DECL, is a template
   conversion operator to a type dependent on the innermost template
   args.  */
#define DECL_TEMPLATE_CONV_FN_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.template_conv_p)

/* Set the overloaded operator code for NODE to CODE.  */
#define SET_OVERLOADED_OPERATOR_CODE(NODE, CODE) \
  (DECL_LANG_SPECIFIC (NODE)->u.f.operator_code = (CODE))

/* If NODE is an overloaded operator, then this returns the TREE_CODE
   associated with the overloaded operator.
   DECL_ASSIGNMENT_OPERATOR_P must also be checked to determine
   whether or not NODE is an assignment operator.  If NODE is not an
   overloaded operator, ERROR_MARK is returned.  Since the numerical
   value of ERROR_MARK is zero, this macro can be used as a predicate
   to test whether or not NODE is an overloaded operator.  */
#define DECL_OVERLOADED_OPERATOR_P(NODE)		\
  (IDENTIFIER_OPNAME_P (DECL_NAME (NODE))		\
   ? DECL_LANG_SPECIFIC (NODE)->u.f.operator_code : ERROR_MARK)

/* Nonzero if NODE is an assignment operator.  */
#define DECL_ASSIGNMENT_OPERATOR_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.assignment_operator_p)

/* For FUNCTION_DECLs: nonzero means that this function is a
   constructor or a destructor with an extra in-charge parameter to
   control whether or not virtual bases are constructed.  */
#define DECL_HAS_IN_CHARGE_PARM_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.has_in_charge_parm_p)

/* Nonzero if DECL is a declaration of __builtin_constant_p.  */
#define DECL_IS_BUILTIN_CONSTANT_P(NODE)		\
 (TREE_CODE (NODE) == FUNCTION_DECL			\
  && DECL_BUILT_IN_CLASS (NODE) == BUILT_IN_NORMAL	\
  && DECL_FUNCTION_CODE (NODE) == BUILT_IN_CONSTANT_P)

/* Nonzero for _DECL means that this decl appears in (or will appear
   in) as a member in a RECORD_TYPE or UNION_TYPE node.  It is also for
   detecting circularity in case members are multiply defined.  In the
   case of a VAR_DECL, it is also used to determine how program storage
   should be allocated.  */
#define DECL_IN_AGGR_P(NODE) (DECL_LANG_FLAG_3 (NODE))

/* Nonzero for a VAR_DECL means that the variable's initialization (if
   any) has been processed.  (In general, DECL_INITIALIZED_P is
   !DECL_EXTERN, but static data members may be initialized even if
   not defined.)  */
#define DECL_INITIALIZED_P(NODE) \
   (TREE_LANG_FLAG_1 (VAR_DECL_CHECK (NODE)))

/* Nonzero for a VAR_DECL iff an explicit initializer was provided.  */
#define DECL_NONTRIVIALLY_INITIALIZED_P(NODE)	\
   (TREE_LANG_FLAG_3 (VAR_DECL_CHECK (NODE)))

/* Nonzero for a VAR_DECL that was initialized with a
   constant-expression.  */
#define DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P(NODE) \
  (TREE_LANG_FLAG_2 (VAR_DECL_CHECK (NODE)))

/* Nonzero for a VAR_DECL that can be used in an integral constant
   expression.

      [expr.const]

      An integral constant-expression can only involve ... const
      variables of static or enumeration types initialized with
      constant expressions ...

   The standard does not require that the expression be non-volatile.
   G++ implements the proposed correction in DR 457.  */
#define DECL_INTEGRAL_CONSTANT_VAR_P(NODE)		\
  (TREE_CODE (NODE) == VAR_DECL				\
   && CP_TYPE_CONST_NON_VOLATILE_P (TREE_TYPE (NODE))	\
   && INTEGRAL_OR_ENUMERATION_TYPE_P (TREE_TYPE (NODE))	\
   && DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (NODE))

/* Nonzero if the DECL was initialized in the class definition itself,
   rather than outside the class.  This is used for both static member
   VAR_DECLS, and FUNTION_DECLS that are defined in the class.  */
#define DECL_INITIALIZED_IN_CLASS_P(DECL) \
 (DECL_LANG_SPECIFIC (DECL)->decl_flags.initialized_in_class)

/* Nonzero for DECL means that this decl is just a friend declaration,
   and should not be added to the list of members for this class.  */
#define DECL_FRIEND_P(NODE) (DECL_LANG_SPECIFIC (NODE)->decl_flags.friend_attr)

/* A TREE_LIST of the types which have befriended this FUNCTION_DECL.  */
#define DECL_BEFRIENDING_CLASSES(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->u.f.befriending_classes)

/* Nonzero for FUNCTION_DECL means that this decl is a static
   member function.  */
#define DECL_STATIC_FUNCTION_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.static_function)

/* Nonzero for FUNCTION_DECL means that this decl is a non-static
   member function.  */
#define DECL_NONSTATIC_MEMBER_FUNCTION_P(NODE) \
  (TREE_CODE (TREE_TYPE (NODE)) == METHOD_TYPE)

/* Nonzero for FUNCTION_DECL means that this decl is a member function
   (static or non-static).  */
#define DECL_FUNCTION_MEMBER_P(NODE) \
 (DECL_NONSTATIC_MEMBER_FUNCTION_P (NODE) || DECL_STATIC_FUNCTION_P (NODE))

/* Nonzero for FUNCTION_DECL means that this member function
   has `this' as const X *const.  */
#define DECL_CONST_MEMFUNC_P(NODE)					 \
  (DECL_NONSTATIC_MEMBER_FUNCTION_P (NODE)				 \
   && CP_TYPE_CONST_P (TREE_TYPE (TREE_VALUE				 \
				  (TYPE_ARG_TYPES (TREE_TYPE (NODE))))))

/* Nonzero for FUNCTION_DECL means that this member function
   has `this' as volatile X *const.  */
#define DECL_VOLATILE_MEMFUNC_P(NODE)					 \
  (DECL_NONSTATIC_MEMBER_FUNCTION_P (NODE)				 \
   && CP_TYPE_VOLATILE_P (TREE_TYPE (TREE_VALUE				 \
				  (TYPE_ARG_TYPES (TREE_TYPE (NODE))))))

/* Nonzero for a DECL means that this member is a non-static member.  */
#define DECL_NONSTATIC_MEMBER_P(NODE)		\
  ((TREE_CODE (NODE) == FUNCTION_DECL		\
    && DECL_NONSTATIC_MEMBER_FUNCTION_P (NODE))	\
   || TREE_CODE (NODE) == FIELD_DECL)

/* Nonzero for _DECL means that this member object type
   is mutable.  */
#define DECL_MUTABLE_P(NODE) (DECL_LANG_FLAG_0 (NODE))

/* Nonzero for _DECL means that this constructor is a non-converting
   constructor.  */
#define DECL_NONCONVERTING_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.nonconverting)

/* Nonzero for FUNCTION_DECL means that this member function is a pure
   virtual function.  */
#define DECL_PURE_VIRTUAL_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.pure_virtual)

/* True (in a FUNCTION_DECL) if NODE is a virtual function that is an
   invalid overrider for a function from a base class.  Once we have
   complained about an invalid overrider we avoid complaining about it
   again.  */
#define DECL_INVALID_OVERRIDER_P(NODE) \
  (DECL_LANG_FLAG_4 (NODE))

/* The thunks associated with NODE, a FUNCTION_DECL.  */
#define DECL_THUNKS(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->u.f.context)

/* Nonzero if NODE is a thunk, rather than an ordinary function.  */
#define DECL_THUNK_P(NODE)			\
  (TREE_CODE (NODE) == FUNCTION_DECL		\
   && DECL_LANG_SPECIFIC (NODE)			\
   && DECL_LANG_SPECIFIC (NODE)->decl_flags.thunk_p)

/* Set DECL_THUNK_P for node.  */
#define SET_DECL_THUNK_P(NODE, THIS_ADJUSTING)			\
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.thunk_p = 1,		\
   DECL_LANG_SPECIFIC (NODE)->u.f.u3sel = 1,			\
   DECL_LANG_SPECIFIC (NODE)->decl_flags.this_thunk_p = (THIS_ADJUSTING))

/* Nonzero if NODE is a this pointer adjusting thunk.  */
#define DECL_THIS_THUNK_P(NODE)			\
  (DECL_THUNK_P (NODE) && DECL_LANG_SPECIFIC (NODE)->decl_flags.this_thunk_p)

/* Nonzero if NODE is a result pointer adjusting thunk.  */
#define DECL_RESULT_THUNK_P(NODE)			\
  (DECL_THUNK_P (NODE) && !DECL_LANG_SPECIFIC (NODE)->decl_flags.this_thunk_p)

/* Nonzero if NODE is a FUNCTION_DECL, but not a thunk.  */
#define DECL_NON_THUNK_FUNCTION_P(NODE)				\
  (TREE_CODE (NODE) == FUNCTION_DECL && !DECL_THUNK_P (NODE))

/* Nonzero if NODE is `extern "C"'.  */
#define DECL_EXTERN_C_P(NODE) \
  (DECL_LANGUAGE (NODE) == lang_c)

/* Nonzero if NODE is an `extern "C"' function.  */
#define DECL_EXTERN_C_FUNCTION_P(NODE) \
  (DECL_NON_THUNK_FUNCTION_P (NODE) && DECL_EXTERN_C_P (NODE))

/* True iff DECL is an entity with vague linkage whose definition is
   available in this translation unit.  */
#define DECL_REPO_AVAILABLE_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.repo_available_p)

/* Nonzero if this DECL is the __PRETTY_FUNCTION__ variable in a
   template function.  */
#define DECL_PRETTY_FUNCTION_P(NODE) \
  (TREE_LANG_FLAG_0 (VAR_DECL_CHECK (NODE)))

/* The _TYPE context in which this _DECL appears.  This field holds the
   class where a virtual function instance is actually defined.  */
#define DECL_CLASS_CONTEXT(NODE) \
  (DECL_CLASS_SCOPE_P (NODE) ? DECL_CONTEXT (NODE) : NULL_TREE)

/* For a non-member friend function, the class (if any) in which this
   friend was defined.  For example, given:

     struct S { friend void f (); };

   the DECL_FRIEND_CONTEXT for `f' will be `S'.  */
#define DECL_FRIEND_CONTEXT(NODE)				\
  ((DECL_FRIEND_P (NODE) && !DECL_FUNCTION_MEMBER_P (NODE))	\
   ? DECL_LANG_SPECIFIC (NODE)->u.f.context			\
   : NULL_TREE)

/* Set the DECL_FRIEND_CONTEXT for NODE to CONTEXT.  */
#define SET_DECL_FRIEND_CONTEXT(NODE, CONTEXT) \
  (DECL_LANG_SPECIFIC (NODE)->u.f.context = (CONTEXT))

/* NULL_TREE in DECL_CONTEXT represents the global namespace.  */
#define CP_DECL_CONTEXT(NODE) \
  (DECL_CONTEXT (NODE) ? DECL_CONTEXT (NODE) : global_namespace)
#define CP_TYPE_CONTEXT(NODE) \
  (TYPE_CONTEXT (NODE) ? TYPE_CONTEXT (NODE) : global_namespace)
#define FROB_CONTEXT(NODE)   ((NODE) == global_namespace ? NULL_TREE : (NODE))

/* 1 iff NODE has namespace scope, including the global namespace.  */
#define DECL_NAMESPACE_SCOPE_P(NODE)				\
  (!DECL_TEMPLATE_PARM_P (NODE)					\
   && TREE_CODE (CP_DECL_CONTEXT (NODE)) == NAMESPACE_DECL)

#define TYPE_NAMESPACE_SCOPE_P(NODE)				\
  (TREE_CODE (CP_TYPE_CONTEXT (NODE)) == NAMESPACE_DECL)

/* 1 iff NODE is a class member.  */
#define DECL_CLASS_SCOPE_P(NODE) \
  (DECL_CONTEXT (NODE) && TYPE_P (DECL_CONTEXT (NODE)))

#define TYPE_CLASS_SCOPE_P(NODE) \
  (TYPE_CONTEXT (NODE) && TYPE_P (TYPE_CONTEXT (NODE)))

/* 1 iff NODE is function-local.  */
#define DECL_FUNCTION_SCOPE_P(NODE) \
  (DECL_CONTEXT (NODE) \
   && TREE_CODE (DECL_CONTEXT (NODE)) == FUNCTION_DECL)

#define TYPE_FUNCTION_SCOPE_P(NODE) \
  (TYPE_CONTEXT (NODE) \
   && TREE_CODE (TYPE_CONTEXT (NODE)) == FUNCTION_DECL)

/* 1 iff VAR_DECL node NODE is a type-info decl.  This flag is set for
   both the primary typeinfo object and the associated NTBS name.  */
#define DECL_TINFO_P(NODE) TREE_LANG_FLAG_4 (VAR_DECL_CHECK (NODE))

/* 1 iff VAR_DECL node NODE is virtual table or VTT.  */
#define DECL_VTABLE_OR_VTT_P(NODE) TREE_LANG_FLAG_5 (VAR_DECL_CHECK (NODE))

/* Returns 1 iff VAR_DECL is a construction virtual table.
   DECL_VTABLE_OR_VTT_P will be true in this case and must be checked
   before using this macro.  */
#define DECL_CONSTRUCTION_VTABLE_P(NODE) \
  TREE_LANG_FLAG_6 (VAR_DECL_CHECK (NODE))

/* 1 iff NODE is function-local, but for types.  */
#define LOCAL_CLASS_P(NODE)				\
  (decl_function_context (TYPE_MAIN_DECL (NODE)) != NULL_TREE)

/* For a NAMESPACE_DECL: the list of using namespace directives
   The PURPOSE is the used namespace, the value is the namespace
   that is the common ancestor.  */
#define DECL_NAMESPACE_USING(NODE) DECL_VINDEX (NAMESPACE_DECL_CHECK (NODE))

/* In a NAMESPACE_DECL, the DECL_INITIAL is used to record all users
   of a namespace, to record the transitive closure of using namespace.  */
#define DECL_NAMESPACE_USERS(NODE) DECL_INITIAL (NAMESPACE_DECL_CHECK (NODE))

/* In a NAMESPACE_DECL, the list of namespaces which have associated
   themselves with this one.  */
#define DECL_NAMESPACE_ASSOCIATIONS(NODE) \
  (NAMESPACE_DECL_CHECK (NODE)->decl_non_common.saved_tree)

/* In a NAMESPACE_DECL, points to the original namespace if this is
   a namespace alias.  */
#define DECL_NAMESPACE_ALIAS(NODE) \
	DECL_ABSTRACT_ORIGIN (NAMESPACE_DECL_CHECK (NODE))
#define ORIGINAL_NAMESPACE(NODE)  \
  (DECL_NAMESPACE_ALIAS (NODE) ? DECL_NAMESPACE_ALIAS (NODE) : (NODE))

/* Nonzero if NODE is the std namespace.  */
#define DECL_NAMESPACE_STD_P(NODE)			\
  (TREE_CODE (NODE) == NAMESPACE_DECL			\
   && CP_DECL_CONTEXT (NODE) == global_namespace	\
   && DECL_NAME (NODE) == std_identifier)

/* In a TREE_LIST concatenating using directives, indicate indirect
   directives  */
#define TREE_INDIRECT_USING(NODE) (TREE_LIST_CHECK (NODE)->common.lang_flag_0)

extern tree decl_shadowed_for_var_lookup (tree);
extern void decl_shadowed_for_var_insert (tree, tree);

/* Non zero if this is a using decl for a dependent scope. */
#define DECL_DEPENDENT_P(NODE) DECL_LANG_FLAG_0 (USING_DECL_CHECK (NODE))

/* The scope named in a using decl.  */
#define USING_DECL_SCOPE(NODE) TREE_TYPE (USING_DECL_CHECK (NODE))

/* The decls named by a using decl.  */
#define USING_DECL_DECLS(NODE) DECL_INITIAL (USING_DECL_CHECK (NODE))

/* In a VAR_DECL, true if we have a shadowed local variable
   in the shadowed var table for this VAR_DECL.  */
#define DECL_HAS_SHADOWED_FOR_VAR_P(NODE) \
  (VAR_DECL_CHECK (NODE)->decl_with_vis.shadowed_for_var_p)

/* In a VAR_DECL for a variable declared in a for statement,
   this is the shadowed (local) variable.  */
#define DECL_SHADOWED_FOR_VAR(NODE) \
  (DECL_HAS_SHADOWED_FOR_VAR_P(NODE) ? decl_shadowed_for_var_lookup (NODE) : NULL)

#define SET_DECL_SHADOWED_FOR_VAR(NODE, VAL) \
  (decl_shadowed_for_var_insert (NODE, VAL))

/* In a FUNCTION_DECL, this is nonzero if this function was defined in
   the class definition.  We have saved away the text of the function,
   but have not yet processed it.  */
#define DECL_PENDING_INLINE_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->u.f.pending_inline_p)

/* If DECL_PENDING_INLINE_P holds, this is the saved text of the
   function.  */
#define DECL_PENDING_INLINE_INFO(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->u.f.u.pending_inline_info)

/* For a TYPE_DECL: if this structure has many fields, we'll sort them
   and put them into a TREE_VEC.  */
#define DECL_SORTED_FIELDS(NODE) \
  (DECL_LANG_SPECIFIC (TYPE_DECL_CHECK (NODE))->u.f.u.sorted_fields)

/* True if on the deferred_fns (see decl2.c) list.  */
#define DECL_DEFERRED_FN(DECL) \
  (DECL_LANG_SPECIFIC (DECL)->decl_flags.deferred)

/* If non-NULL for a VAR_DECL, FUNCTION_DECL, TYPE_DECL or
   TEMPLATE_DECL, the entity is either a template specialization (if
   DECL_USE_TEMPLATE is non-zero) or the abstract instance of the
   template itself.

   In either case, DECL_TEMPLATE_INFO is a TREE_LIST, whose
   TREE_PURPOSE is the TEMPLATE_DECL of which this entity is a
   specialization or abstract instance.  The TREE_VALUE is the
   template arguments used to specialize the template.
   
   Consider:

      template <typename T> struct S { friend void f(T) {} };

   In this case, S<int>::f is, from the point of view of the compiler,
   an instantiation of a template -- but, from the point of view of
   the language, each instantiation of S results in a wholly unrelated
   global function f.  In this case, DECL_TEMPLATE_INFO for S<int>::f
   will be non-NULL, but DECL_USE_TEMPLATE will be zero.  */
#define DECL_TEMPLATE_INFO(NODE) \
  (DECL_LANG_SPECIFIC (VAR_TEMPL_TYPE_OR_FUNCTION_DECL_CHECK (NODE)) \
   ->decl_flags.u.template_info)

/* For a VAR_DECL, indicates that the variable is actually a
   non-static data member of anonymous union that has been promoted to
   variable status.  */
#define DECL_ANON_UNION_VAR_P(NODE) \
  (DECL_LANG_FLAG_4 (VAR_DECL_CHECK (NODE)))

/* Template information for a RECORD_TYPE or UNION_TYPE.  */
#define CLASSTYPE_TEMPLATE_INFO(NODE) \
  (LANG_TYPE_CLASS_CHECK (RECORD_OR_UNION_CHECK (NODE))->template_info)

/* Template information for an ENUMERAL_TYPE.  Although an enumeration may
   not be a primary template, it may be declared within the scope of a
   primary template and the enumeration constants may depend on
   non-type template parameters.  */
#define ENUM_TEMPLATE_INFO(NODE) \
  (TYPE_LANG_SLOT_1 (ENUMERAL_TYPE_CHECK (NODE)))

/* Template information for a template template parameter.  */
#define TEMPLATE_TEMPLATE_PARM_TEMPLATE_INFO(NODE) \
  (LANG_TYPE_CLASS_CHECK (BOUND_TEMPLATE_TEMPLATE_PARM_TYPE_CHECK (NODE)) \
   ->template_info)

/* Template information for an ENUMERAL_, RECORD_, or UNION_TYPE.  */
#define TYPE_TEMPLATE_INFO(NODE)			\
  (TREE_CODE (NODE) == ENUMERAL_TYPE			\
   ? ENUM_TEMPLATE_INFO (NODE) :			\
   (TREE_CODE (NODE) == BOUND_TEMPLATE_TEMPLATE_PARM	\
    ? TEMPLATE_TEMPLATE_PARM_TEMPLATE_INFO (NODE) :	\
    (TYPE_LANG_SPECIFIC (NODE)				\
     ? CLASSTYPE_TEMPLATE_INFO (NODE)			\
     : NULL_TREE)))

/* Set the template information for an ENUMERAL_, RECORD_, or
   UNION_TYPE to VAL.  */
#define SET_TYPE_TEMPLATE_INFO(NODE, VAL)	\
  (TREE_CODE (NODE) == ENUMERAL_TYPE		\
   ? (ENUM_TEMPLATE_INFO (NODE) = (VAL))	\
   : (CLASSTYPE_TEMPLATE_INFO (NODE) = (VAL)))

#define TI_TEMPLATE(NODE) (TREE_PURPOSE (NODE))
#define TI_ARGS(NODE) (TREE_VALUE (NODE))
#define TI_PENDING_TEMPLATE_FLAG(NODE) TREE_LANG_FLAG_1 (NODE)

/* We use TREE_VECs to hold template arguments.  If there is only one
   level of template arguments, then the TREE_VEC contains the
   arguments directly.  If there is more than one level of template
   arguments, then each entry in the TREE_VEC is itself a TREE_VEC,
   containing the template arguments for a single level.  The first
   entry in the outer TREE_VEC is the outermost level of template
   parameters; the last is the innermost.

   It is incorrect to ever form a template argument vector containing
   only one level of arguments, but which is a TREE_VEC containing as
   its only entry the TREE_VEC for that level.  */

/* Nonzero if the template arguments is actually a vector of vectors,
   rather than just a vector.  */
#define TMPL_ARGS_HAVE_MULTIPLE_LEVELS(NODE)		\
  (NODE && TREE_VEC_ELT (NODE, 0)			\
   && TREE_CODE (TREE_VEC_ELT (NODE, 0)) == TREE_VEC)

/* The depth of a template argument vector.  When called directly by
   the parser, we use a TREE_LIST rather than a TREE_VEC to represent
   template arguments.  In fact, we may even see NULL_TREE if there
   are no template arguments.  In both of those cases, there is only
   one level of template arguments.  */
#define TMPL_ARGS_DEPTH(NODE)					\
  (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (NODE) ? TREE_VEC_LENGTH (NODE) : 1)

/* The LEVELth level of the template ARGS.  The outermost level of
   args is level 1, not level 0.  */
#define TMPL_ARGS_LEVEL(ARGS, LEVEL)		\
  (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (ARGS)	\
   ? TREE_VEC_ELT (ARGS, (LEVEL) - 1) : (ARGS))

/* Set the LEVELth level of the template ARGS to VAL.  This macro does
   not work with single-level argument vectors.  */
#define SET_TMPL_ARGS_LEVEL(ARGS, LEVEL, VAL)	\
  (TREE_VEC_ELT (ARGS, (LEVEL) - 1) = (VAL))

/* Accesses the IDXth parameter in the LEVELth level of the ARGS.  */
#define TMPL_ARG(ARGS, LEVEL, IDX)				\
  (TREE_VEC_ELT (TMPL_ARGS_LEVEL (ARGS, LEVEL), IDX))

/* Given a single level of template arguments in NODE, return the
   number of arguments.  */
#define NUM_TMPL_ARGS(NODE)				\
  (TREE_VEC_LENGTH (NODE))

/* Returns the innermost level of template arguments in ARGS.  */
#define INNERMOST_TEMPLATE_ARGS(NODE) \
  (get_innermost_template_args ((NODE), 1))

/* The number of levels of template parameters given by NODE.  */
#define TMPL_PARMS_DEPTH(NODE) \
  ((HOST_WIDE_INT) TREE_INT_CST_LOW (TREE_PURPOSE (NODE)))

/* The TEMPLATE_DECL instantiated or specialized by NODE.  This
   TEMPLATE_DECL will be the immediate parent, not the most general
   template.  For example, in:

      template <class T> struct S { template <class U> void f(U); }

   the FUNCTION_DECL for S<int>::f<double> will have, as its
   DECL_TI_TEMPLATE, `template <class U> S<int>::f<U>'.

   As a special case, for a member friend template of a template
   class, this value will not be a TEMPLATE_DECL, but rather an
   IDENTIFIER_NODE or OVERLOAD indicating the name of the template and
   any explicit template arguments provided.  For example, in:

     template <class T> struct S { friend void f<int>(int, double); }

   the DECL_TI_TEMPLATE will be an IDENTIFIER_NODE for `f' and the
   DECL_TI_ARGS will be {int}.  */
#define DECL_TI_TEMPLATE(NODE)      TI_TEMPLATE (DECL_TEMPLATE_INFO (NODE))

/* The template arguments used to obtain this decl from the most
   general form of DECL_TI_TEMPLATE.  For the example given for
   DECL_TI_TEMPLATE, the DECL_TI_ARGS will be {int, double}.  These
   are always the full set of arguments required to instantiate this
   declaration from the most general template specialized here.  */
#define DECL_TI_ARGS(NODE)	    TI_ARGS (DECL_TEMPLATE_INFO (NODE))

/* The TEMPLATE_DECL associated with NODE, a class type.  Even if NODE
   will be generated from a partial specialization, the TEMPLATE_DECL
   referred to here will be the original template.  For example,
   given:

      template <typename T> struct S {};
      template <typename T> struct S<T*> {};
      
   the CLASSTPYE_TI_TEMPLATE for S<int*> will be S, not the S<T*>.  */
#define CLASSTYPE_TI_TEMPLATE(NODE) TI_TEMPLATE (CLASSTYPE_TEMPLATE_INFO (NODE))
#define CLASSTYPE_TI_ARGS(NODE)     TI_ARGS (CLASSTYPE_TEMPLATE_INFO (NODE))

/* For a template instantiation TYPE, returns the TYPE corresponding
   to the primary template.  Otherwise returns TYPE itself.  */
#define CLASSTYPE_PRIMARY_TEMPLATE_TYPE(TYPE)				\
  ((CLASSTYPE_USE_TEMPLATE ((TYPE))					\
    && !CLASSTYPE_TEMPLATE_SPECIALIZATION ((TYPE)))			\
   ? TREE_TYPE (DECL_TEMPLATE_RESULT (DECL_PRIMARY_TEMPLATE		\
				      (CLASSTYPE_TI_TEMPLATE ((TYPE))))) \
   : (TYPE))

/* Like CLASS_TI_TEMPLATE, but also works for ENUMERAL_TYPEs.  */
#define TYPE_TI_TEMPLATE(NODE)			\
  (TI_TEMPLATE (TYPE_TEMPLATE_INFO (NODE)))

/* Like DECL_TI_ARGS, but for an ENUMERAL_, RECORD_, or UNION_TYPE.  */
#define TYPE_TI_ARGS(NODE)			\
  (TI_ARGS (TYPE_TEMPLATE_INFO (NODE)))

#define INNERMOST_TEMPLATE_PARMS(NODE)  TREE_VALUE (NODE)

/* Nonzero if NODE (a TEMPLATE_DECL) is a member template, in the
   sense of [temp.mem].  */
#define DECL_MEMBER_TEMPLATE_P(NODE) \
  (DECL_LANG_FLAG_1 (TEMPLATE_DECL_CHECK (NODE)))

/* Nonzero if the NODE corresponds to the template parameters for a
   member template, whose inline definition is being processed after
   the class definition is complete.  */
#define TEMPLATE_PARMS_FOR_INLINE(NODE) TREE_LANG_FLAG_1 (NODE)

/* In a FUNCTION_DECL, the saved language-specific per-function data.  */
#define DECL_SAVED_FUNCTION_DATA(NODE)			\
  (DECL_LANG_SPECIFIC (FUNCTION_DECL_CHECK (NODE))	\
   ->u.f.u.saved_language_function)

/* Indicates an indirect_expr is for converting a reference.  */
#define REFERENCE_REF_P(NODE) \
  TREE_LANG_FLAG_0 (INDIRECT_REF_CHECK (NODE))

#define NEW_EXPR_USE_GLOBAL(NODE) \
  TREE_LANG_FLAG_0 (NEW_EXPR_CHECK (NODE))
#define DELETE_EXPR_USE_GLOBAL(NODE) \
  TREE_LANG_FLAG_0 (DELETE_EXPR_CHECK (NODE))
#define DELETE_EXPR_USE_VEC(NODE) \
  TREE_LANG_FLAG_1 (DELETE_EXPR_CHECK (NODE))

/* Indicates that this is a non-dependent COMPOUND_EXPR which will
   resolve to a function call.  */
#define COMPOUND_EXPR_OVERLOADED(NODE) \
  TREE_LANG_FLAG_0 (COMPOUND_EXPR_CHECK (NODE))

/* In a CALL_EXPR appearing in a template, true if Koenig lookup
   should be performed at instantiation time.  */
#define KOENIG_LOOKUP_P(NODE) TREE_LANG_FLAG_0 (CALL_EXPR_CHECK (NODE))

/* Indicates whether a string literal has been parenthesized. Such
   usages are disallowed in certain circumstances.  */

#define PAREN_STRING_LITERAL_P(NODE) \
  TREE_LANG_FLAG_0 (STRING_CST_CHECK (NODE))

/* Nonzero if this AGGR_INIT_EXPR provides for initialization via a
   constructor call, rather than an ordinary function call.  */
#define AGGR_INIT_VIA_CTOR_P(NODE) \
  TREE_LANG_FLAG_0 (AGGR_INIT_EXPR_CHECK (NODE))

/* The TYPE_MAIN_DECL for a class template type is a TYPE_DECL, not a
   TEMPLATE_DECL.  This macro determines whether or not a given class
   type is really a template type, as opposed to an instantiation or
   specialization of one.  */
#define CLASSTYPE_IS_TEMPLATE(NODE)  \
  (CLASSTYPE_TEMPLATE_INFO (NODE)    \
   && !CLASSTYPE_USE_TEMPLATE (NODE) \
   && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (NODE)))

/* The name used by the user to name the typename type.  Typically,
   this is an IDENTIFIER_NODE, and the same as the DECL_NAME on the
   corresponding TYPE_DECL.  However, this may also be a
   TEMPLATE_ID_EXPR if we had something like `typename X::Y<T>'.  */
#define TYPENAME_TYPE_FULLNAME(NODE) (TYPENAME_TYPE_CHECK (NODE))->type.values

/* True if a TYPENAME_TYPE was declared as an "enum".  */
#define TYPENAME_IS_ENUM_P(NODE) \
  (TREE_LANG_FLAG_0 (TYPENAME_TYPE_CHECK (NODE)))

/* True if a TYPENAME_TYPE was declared as a "class", "struct", or
   "union".  */
#define TYPENAME_IS_CLASS_P(NODE) \
  (TREE_LANG_FLAG_1 (TYPENAME_TYPE_CHECK (NODE)))

/* Nonzero in INTEGER_CST means that this int is negative by dint of
   using a twos-complement negated operand.  */
#define TREE_NEGATED_INT(NODE) TREE_LANG_FLAG_0 (INTEGER_CST_CHECK (NODE))

/* [class.virtual]

   A class that declares or inherits a virtual function is called a
   polymorphic class.  */
#define TYPE_POLYMORPHIC_P(NODE) (TREE_LANG_FLAG_2 (NODE))

/* Nonzero if this class has a virtual function table pointer.  */
#define TYPE_CONTAINS_VPTR_P(NODE)		\
  (TYPE_POLYMORPHIC_P (NODE) || CLASSTYPE_VBASECLASSES (NODE))

/* This flag is true of a local VAR_DECL if it was declared in a for
   statement, but we are no longer in the scope of the for.  */
#define DECL_DEAD_FOR_LOCAL(NODE) DECL_LANG_FLAG_7 (VAR_DECL_CHECK (NODE))

/* This flag is set on a VAR_DECL that is a DECL_DEAD_FOR_LOCAL
   if we already emitted a warning about using it.  */
#define DECL_ERROR_REPORTED(NODE) DECL_LANG_FLAG_0 (VAR_DECL_CHECK (NODE))

/* Nonzero if NODE is a FUNCTION_DECL (for a function with global
   scope) declared in a local scope.  */
#define DECL_LOCAL_FUNCTION_P(NODE) \
  DECL_LANG_FLAG_0 (FUNCTION_DECL_CHECK (NODE))

/* Nonzero if NODE is a DECL which we know about but which has not
   been explicitly declared, such as a built-in function or a friend
   declared inside a class.  In the latter case DECL_HIDDEN_FRIEND_P
   will be set.  */
#define DECL_ANTICIPATED(NODE) \
  (DECL_LANG_SPECIFIC (DECL_COMMON_CHECK (NODE))->decl_flags.anticipated_p)

/* Nonzero if NODE is a FUNCTION_DECL which was declared as a friend
   within a class but has not been declared in the surrounding scope.
   The function is invisible except via argument dependent lookup.  */
#define DECL_HIDDEN_FRIEND_P(NODE) \
  (DECL_LANG_SPECIFIC (DECL_COMMON_CHECK (NODE))->decl_flags.hidden_friend_p)

/* Nonzero if DECL has been declared threadprivate by
   #pragma omp threadprivate.  */
#define CP_DECL_THREADPRIVATE_P(DECL) \
  (DECL_LANG_SPECIFIC (VAR_DECL_CHECK (DECL))->decl_flags.threadprivate_p)

/* Record whether a typedef for type `int' was actually `signed int'.  */
#define C_TYPEDEF_EXPLICITLY_SIGNED(EXP) DECL_LANG_FLAG_1 (EXP)

/* Returns nonzero if DECL has external linkage, as specified by the
   language standard.  (This predicate may hold even when the
   corresponding entity is not actually given external linkage in the
   object file; see decl_linkage for details.)  */
#define DECL_EXTERNAL_LINKAGE_P(DECL) \
  (decl_linkage (DECL) == lk_external)

/* Keep these codes in ascending code order.  */

#define INTEGRAL_CODE_P(CODE)	\
  ((CODE) == ENUMERAL_TYPE	\
   || (CODE) == BOOLEAN_TYPE	\
   || (CODE) == INTEGER_TYPE)

/* [basic.fundamental]

   Types  bool, char, wchar_t, and the signed and unsigned integer types
   are collectively called integral types.

   Note that INTEGRAL_TYPE_P, as defined in tree.h, allows enumeration
   types as well, which is incorrect in C++.  Keep these checks in
   ascending code order.  */
#define CP_INTEGRAL_TYPE_P(TYPE)		\
  (TREE_CODE (TYPE) == BOOLEAN_TYPE		\
   || TREE_CODE (TYPE) == INTEGER_TYPE)

/* Returns true if TYPE is an integral or enumeration name.  Keep
   these checks in ascending code order.  */
#define INTEGRAL_OR_ENUMERATION_TYPE_P(TYPE) \
   (TREE_CODE (TYPE) == ENUMERAL_TYPE || CP_INTEGRAL_TYPE_P (TYPE))

/* [basic.fundamental]

   Integral and floating types are collectively called arithmetic
   types.  

   As a GNU extension, we also accept complex types.

   Keep these checks in ascending code order.  */
#define ARITHMETIC_TYPE_P(TYPE) \
  (CP_INTEGRAL_TYPE_P (TYPE) \
   || TREE_CODE (TYPE) == REAL_TYPE \
   || TREE_CODE (TYPE) == COMPLEX_TYPE)

/* [basic.types]

   Arithmetic types, enumeration types, pointer types, and
   pointer-to-member types, are collectively called scalar types.
   
   Keep these checks in ascending code order.  */
#define SCALAR_TYPE_P(TYPE)			\
  (TYPE_PTRMEM_P (TYPE)				\
   || TREE_CODE (TYPE) == ENUMERAL_TYPE		\
   || ARITHMETIC_TYPE_P (TYPE)			\
   || TYPE_PTR_P (TYPE)				\
   /* APPLE LOCAL blocks 6040305 */		\
   || TREE_CODE (TYPE) == BLOCK_POINTER_TYPE	\
   || TYPE_PTRMEMFUNC_P (TYPE))

/* [dcl.init.aggr]

   An aggregate is an array or a class with no user-declared
   constructors, no private or protected non-static data members, no
   base classes, and no virtual functions.

   As an extension, we also treat vectors as aggregates.  Keep these
   checks in ascending code order.  */
#define CP_AGGREGATE_TYPE_P(TYPE)				\
  (TREE_CODE (TYPE) == VECTOR_TYPE				\
   ||TREE_CODE (TYPE) == ARRAY_TYPE				\
   || (CLASS_TYPE_P (TYPE) && !CLASSTYPE_NON_AGGREGATE (TYPE)))

/* Nonzero for a class type means that the class type has a
   user-declared constructor.  */
#define TYPE_HAS_CONSTRUCTOR(NODE) (TYPE_LANG_FLAG_1 (NODE))

/* When appearing in an INDIRECT_REF, it means that the tree structure
   underneath is actually a call to a constructor.  This is needed
   when the constructor must initialize local storage (which can
   be automatically destroyed), rather than allowing it to allocate
   space from the heap.

   When appearing in a SAVE_EXPR, it means that underneath
   is a call to a constructor.

   When appearing in a CONSTRUCTOR, the expression is a
   compound literal.

   When appearing in a FIELD_DECL, it means that this field
   has been duly initialized in its constructor.  */
#define TREE_HAS_CONSTRUCTOR(NODE) (TREE_LANG_FLAG_4 (NODE))

/* True if NODE is a brace-enclosed initializer.  */
#define BRACE_ENCLOSED_INITIALIZER_P(NODE) \
  (TREE_CODE (NODE) == CONSTRUCTOR && !TREE_TYPE (NODE))

/* True if NODE is a compound-literal, i.e., a brace-enclosed
   initializer cast to a particular type.  */
#define COMPOUND_LITERAL_P(NODE) \
  (TREE_CODE (NODE) == CONSTRUCTOR && TREE_HAS_CONSTRUCTOR (NODE))

#define EMPTY_CONSTRUCTOR_P(NODE) (TREE_CODE (NODE) == CONSTRUCTOR \
				   && VEC_empty (constructor_elt, \
						 CONSTRUCTOR_ELTS (NODE)) \
				   && !TREE_HAS_CONSTRUCTOR (NODE))

/* Nonzero means that an object of this type can not be initialized using
   an initializer list.  */
#define CLASSTYPE_NON_AGGREGATE(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->non_aggregate)
#define TYPE_NON_AGGREGATE_CLASS(NODE) \
  (IS_AGGR_TYPE (NODE) && CLASSTYPE_NON_AGGREGATE (NODE))

/* Nonzero if there is a user-defined X::op=(x&) for this class.  */
#define TYPE_HAS_COMPLEX_ASSIGN_REF(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->has_complex_assign_ref)
#define TYPE_HAS_COMPLEX_INIT_REF(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->has_complex_init_ref)

/* Nonzero if TYPE has a trivial destructor.  From [class.dtor]:

     A destructor is trivial if it is an implicitly declared
     destructor and if:

       - all of the direct base classes of its class have trivial
	 destructors,

       - for all of the non-static data members of its class that are
	 of class type (or array thereof), each such class has a
	 trivial destructor.  */
#define TYPE_HAS_TRIVIAL_DESTRUCTOR(NODE) \
  (!TYPE_HAS_NONTRIVIAL_DESTRUCTOR (NODE))

/* Nonzero for _TYPE node means that this type does not have a trivial
   destructor.  Therefore, destroying an object of this type will
   involve a call to a destructor.  This can apply to objects of
   ARRAY_TYPE is the type of the elements needs a destructor.  */
#define TYPE_HAS_NONTRIVIAL_DESTRUCTOR(NODE) \
  (TYPE_LANG_FLAG_4 (NODE))

/* APPLE LOCAL begin omit calls to empty destructors 5559195 */
/* One if the body of the destructor of class type NODE has been shown to do
 nothing, else zero. */
#define CLASSTYPE_HAS_NONTRIVIAL_DESTRUCTOR_BODY(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->has_nontrivial_destructor_body)

/* One if destructor of this type must be called by its base classes because
 one of its base classes' destructors must be called. */
#define CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->destructor_nontrivial_because_of_base)

/* One if the values of CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE
 and CLASSTYPE_HAS_NONTRIVIAL_DESTRUCTOR_BODY are final. */
#define CLASSTYPE_DESTRUCTOR_TRIVIALITY_FINAL(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->destructor_triviality_final)
/* APPLE LOCAL end omit calls to empty destructors 5559195 */

/* Nonzero for class type means that copy initialization of this type can use
   a bitwise copy.  */
#define TYPE_HAS_TRIVIAL_INIT_REF(NODE) \
  (TYPE_HAS_INIT_REF (NODE) && ! TYPE_HAS_COMPLEX_INIT_REF (NODE))

/* Nonzero for class type means that assignment of this type can use
   a bitwise copy.  */
#define TYPE_HAS_TRIVIAL_ASSIGN_REF(NODE) \
  (TYPE_HAS_ASSIGN_REF (NODE) && ! TYPE_HAS_COMPLEX_ASSIGN_REF (NODE))

/* Returns true if NODE is a pointer-to-data-member.  */
#define TYPE_PTRMEM_P(NODE)			\
  (TREE_CODE (NODE) == OFFSET_TYPE)
/* Returns true if NODE is a pointer.  */
#define TYPE_PTR_P(NODE)			\
  (TREE_CODE (NODE) == POINTER_TYPE)

/* Returns true if NODE is an object type:

     [basic.types]

     An object type is a (possibly cv-qualified) type that is not a
     function type, not a reference type, and not a void type.

   Keep these checks in ascending order, for speed.  */
#define TYPE_OBJ_P(NODE)			\
  (TREE_CODE (NODE) != REFERENCE_TYPE		\
   && TREE_CODE (NODE) != VOID_TYPE		\
   && TREE_CODE (NODE) != FUNCTION_TYPE		\
   && TREE_CODE (NODE) != METHOD_TYPE)

/* Returns true if NODE is a pointer to an object.  Keep these checks
   in ascending tree code order.  */
#define TYPE_PTROB_P(NODE)					\
  (TYPE_PTR_P (NODE) && TYPE_OBJ_P (TREE_TYPE (NODE)))

/* Returns true if NODE is a reference to an object.  Keep these checks
   in ascending tree code order.  */
#define TYPE_REF_OBJ_P(NODE)					\
  (TREE_CODE (NODE) == REFERENCE_TYPE && TYPE_OBJ_P (TREE_TYPE (NODE)))

/* Returns true if NODE is a pointer to an object, or a pointer to
   void.  Keep these checks in ascending tree code order.  */
#define TYPE_PTROBV_P(NODE)					\
  (TYPE_PTR_P (NODE)						\
   && !(TREE_CODE (TREE_TYPE (NODE)) == FUNCTION_TYPE		\
	|| TREE_CODE (TREE_TYPE (NODE)) == METHOD_TYPE))

/* Returns true if NODE is a pointer to function.  */
#define TYPE_PTRFN_P(NODE)				\
  (TREE_CODE (NODE) == POINTER_TYPE			\
   && TREE_CODE (TREE_TYPE (NODE)) == FUNCTION_TYPE)

/* Returns true if NODE is a reference to function.  */
#define TYPE_REFFN_P(NODE)				\
  (TREE_CODE (NODE) == REFERENCE_TYPE			\
   && TREE_CODE (TREE_TYPE (NODE)) == FUNCTION_TYPE)

/* Nonzero for _TYPE node means that this type is a pointer to member
   function type.  */
#define TYPE_PTRMEMFUNC_P(NODE)		\
  (TREE_CODE (NODE) == RECORD_TYPE	\
   && TYPE_LANG_SPECIFIC (NODE)		\
   && TYPE_PTRMEMFUNC_FLAG (NODE))

#define TYPE_PTRMEMFUNC_FLAG(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->ptrmemfunc_flag)

/* Returns true if NODE is a pointer-to-member.  */
#define TYPE_PTR_TO_MEMBER_P(NODE) \
  (TYPE_PTRMEM_P (NODE) || TYPE_PTRMEMFUNC_P (NODE))

/* Indicates when overload resolution may resolve to a pointer to
   member function. [expr.unary.op]/3 */
#define PTRMEM_OK_P(NODE) \
  TREE_LANG_FLAG_0 (TREE_CHECK2 ((NODE), ADDR_EXPR, OFFSET_REF))

/* Get the POINTER_TYPE to the METHOD_TYPE associated with this
   pointer to member function.  TYPE_PTRMEMFUNC_P _must_ be true,
   before using this macro.  */
#define TYPE_PTRMEMFUNC_FN_TYPE(NODE) \
  (TREE_TYPE (TYPE_FIELDS (NODE)))

/* Returns `A' for a type like `int (A::*)(double)' */
#define TYPE_PTRMEMFUNC_OBJECT_TYPE(NODE) \
  TYPE_METHOD_BASETYPE (TREE_TYPE (TYPE_PTRMEMFUNC_FN_TYPE (NODE)))

/* These are use to manipulate the canonical RECORD_TYPE from the
   hashed POINTER_TYPE, and can only be used on the POINTER_TYPE.  */
#define TYPE_GET_PTRMEMFUNC_TYPE(NODE) \
  (TYPE_LANG_SPECIFIC (NODE) ? LANG_TYPE_PTRMEM_CHECK (NODE)->record : NULL)
#define TYPE_SET_PTRMEMFUNC_TYPE(NODE, VALUE)				\
  do {									\
    if (TYPE_LANG_SPECIFIC (NODE) == NULL)				\
      {									\
	TYPE_LANG_SPECIFIC (NODE) = GGC_CNEWVAR				\
	 (struct lang_type, sizeof (struct lang_type_ptrmem));		\
	TYPE_LANG_SPECIFIC (NODE)->u.ptrmem.h.is_lang_type_class = 0;	\
      }									\
    TYPE_LANG_SPECIFIC (NODE)->u.ptrmem.record = (VALUE);		\
  } while (0)

/* For a pointer-to-member type of the form `T X::*', this is `X'.
   For a type like `void (X::*)() const', this type is `X', not `const
   X'.  To get at the `const X' you have to look at the
   TYPE_PTRMEM_POINTED_TO_TYPE; there, the first parameter will have
   type `const X*'.  */
#define TYPE_PTRMEM_CLASS_TYPE(NODE)			\
  (TYPE_PTRMEM_P (NODE)					\
   ? TYPE_OFFSET_BASETYPE (NODE)		\
   : TYPE_PTRMEMFUNC_OBJECT_TYPE (NODE))

/* For a pointer-to-member type of the form `T X::*', this is `T'.  */
#define TYPE_PTRMEM_POINTED_TO_TYPE(NODE)		\
   (TYPE_PTRMEM_P (NODE)				\
    ? TREE_TYPE (NODE)					\
    : TREE_TYPE (TYPE_PTRMEMFUNC_FN_TYPE (NODE)))

/* For a pointer-to-member constant `X::Y' this is the RECORD_TYPE for
   `X'.  */
#define PTRMEM_CST_CLASS(NODE) \
  TYPE_PTRMEM_CLASS_TYPE (TREE_TYPE (PTRMEM_CST_CHECK (NODE)))

/* For a pointer-to-member constant `X::Y' this is the _DECL for
   `Y'.  */
#define PTRMEM_CST_MEMBER(NODE) (((ptrmem_cst_t)PTRMEM_CST_CHECK (NODE))->member)

/* The expression in question for a TYPEOF_TYPE.  */
#define TYPEOF_TYPE_EXPR(NODE) (TYPEOF_TYPE_CHECK (NODE))->type.values

/* Nonzero for VAR_DECL and FUNCTION_DECL node means that `extern' was
   specified in its declaration.  This can also be set for an
   erroneously declared PARM_DECL.  */
#define DECL_THIS_EXTERN(NODE) \
  DECL_LANG_FLAG_2 (VAR_FUNCTION_OR_PARM_DECL_CHECK (NODE))

/* Nonzero for VAR_DECL and FUNCTION_DECL node means that `static' was
   specified in its declaration.  This can also be set for an
   erroneously declared PARM_DECL.  */
#define DECL_THIS_STATIC(NODE) \
  DECL_LANG_FLAG_6 (VAR_FUNCTION_OR_PARM_DECL_CHECK (NODE))

/* Nonzero for FIELD_DECL node means that this field is a base class
   of the parent object, as opposed to a member field.  */
#define DECL_FIELD_IS_BASE(NODE) \
  DECL_LANG_FLAG_6 (FIELD_DECL_CHECK (NODE))

/* Nonzero if TYPE is an anonymous union or struct type.  We have to use a
   flag for this because "A union for which objects or pointers are
   declared is not an anonymous union" [class.union].  */
#define ANON_AGGR_TYPE_P(NODE)				\
  (CLASS_TYPE_P (NODE) && LANG_TYPE_CLASS_CHECK (NODE)->anon_aggr)
#define SET_ANON_AGGR_TYPE_P(NODE)			\
  (LANG_TYPE_CLASS_CHECK (NODE)->anon_aggr = 1)

/* Nonzero if TYPE is an anonymous union type.  */
#define ANON_UNION_TYPE_P(NODE) \
  (TREE_CODE (NODE) == UNION_TYPE && ANON_AGGR_TYPE_P (NODE))

#define UNKNOWN_TYPE LANG_TYPE

/* Define fields and accessors for nodes representing declared names.  */

#define TYPE_WAS_ANONYMOUS(NODE) (LANG_TYPE_CLASS_CHECK (NODE)->was_anonymous)

/* C++: all of these are overloaded!  These apply only to TYPE_DECLs.  */

/* The format of each node in the DECL_FRIENDLIST is as follows:

   The TREE_PURPOSE will be the name of a function, i.e., an
   IDENTIFIER_NODE.  The TREE_VALUE will be itself a TREE_LIST, whose
   TREE_VALUEs are friends with the given name.  */
#define DECL_FRIENDLIST(NODE)		(DECL_INITIAL (NODE))
#define FRIEND_NAME(LIST) (TREE_PURPOSE (LIST))
#define FRIEND_DECLS(LIST) (TREE_VALUE (LIST))

/* The DECL_ACCESS, if non-NULL, is a TREE_LIST.  The TREE_PURPOSE of
   each node is a type; the TREE_VALUE is the access granted for this
   DECL in that type.  The DECL_ACCESS is set by access declarations.
   For example, if a member that would normally be public in a
   derived class is made protected, then the derived class and the
   protected_access_node will appear in the DECL_ACCESS for the node.  */
#define DECL_ACCESS(NODE) (LANG_DECL_U2_CHECK (NODE, 0)->access)

/* Nonzero if the FUNCTION_DECL is a global constructor.  */
#define DECL_GLOBAL_CTOR_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.global_ctor_p)

/* Nonzero if the FUNCTION_DECL is a global destructor.  */
#define DECL_GLOBAL_DTOR_P(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.global_dtor_p)

/* Accessor macros for C++ template decl nodes.  */

/* The DECL_TEMPLATE_PARMS are a list.  The TREE_PURPOSE of each node
   is a INT_CST whose TREE_INT_CST_LOW indicates the level of the
   template parameters, with 1 being the outermost set of template
   parameters.  The TREE_VALUE is a vector, whose elements are the
   template parameters at each level.  Each element in the vector is a
   TREE_LIST, whose TREE_VALUE is a PARM_DECL (if the parameter is a
   non-type parameter), or a TYPE_DECL (if the parameter is a type
   parameter).  The TREE_PURPOSE is the default value, if any.  The
   TEMPLATE_PARM_INDEX for the parameter is available as the
   DECL_INITIAL (for a PARM_DECL) or as the TREE_TYPE (for a
   TYPE_DECL).  */
#define DECL_TEMPLATE_PARMS(NODE)       DECL_NON_COMMON_CHECK (NODE)->decl_non_common.arguments
#define DECL_INNERMOST_TEMPLATE_PARMS(NODE) \
   INNERMOST_TEMPLATE_PARMS (DECL_TEMPLATE_PARMS (NODE))
#define DECL_NTPARMS(NODE) \
   TREE_VEC_LENGTH (DECL_INNERMOST_TEMPLATE_PARMS (NODE))
/* For function, method, class-data templates.  */
#define DECL_TEMPLATE_RESULT(NODE)      DECL_RESULT_FLD (NODE)
/* For a static member variable template, the
   DECL_TEMPLATE_INSTANTIATIONS list contains the explicitly and
   implicitly generated instantiations of the variable.  There are no
   partial instantiations of static member variables, so all of these
   will be full instantiations.

   For a class template the DECL_TEMPLATE_INSTANTIATIONS lists holds
   all instantiations and specializations of the class type, including
   partial instantiations and partial specializations.

   In both cases, the TREE_PURPOSE of each node contains the arguments
   used; the TREE_VALUE contains the generated variable.  The template
   arguments are always complete.  For example, given:

      template <class T> struct S1 {
	template <class U> struct S2 {};
	template <class U> struct S2<U*> {};
      };

   the record for the partial specialization will contain, as its
   argument list, { {T}, {U*} }, and will be on the
   DECL_TEMPLATE_INSTANTIATIONS list for `template <class T> template
   <class U> struct S1<T>::S2'.

   This list is not used for function templates.  */
#define DECL_TEMPLATE_INSTANTIATIONS(NODE) DECL_VINDEX (NODE)
/* For a function template, the DECL_TEMPLATE_SPECIALIZATIONS lists
   contains all instantiations and specializations of the function,
   including partial instantiations.  For a partial instantiation
   which is a specialization, this list holds only full
   specializations of the template that are instantiations of the
   partial instantiation.  For example, given:

      template <class T> struct S {
	template <class U> void f(U);
	template <> void f(T);
      };

   the `S<int>::f<int>(int)' function will appear on the
   DECL_TEMPLATE_SPECIALIZATIONS list for both `template <class T>
   template <class U> void S<T>::f(U)' and `template <class T> void
   S<int>::f(T)'.  In the latter case, however, it will have only the
   innermost set of arguments (T, in this case).  The DECL_TI_TEMPLATE
   for the function declaration will point at the specialization, not
   the fully general template.

   For a class template, this list contains the partial
   specializations of this template.  (Full specializations are not
   recorded on this list.)  The TREE_PURPOSE holds the arguments used
   in the partial specialization (e.g., for `template <class T> struct
   S<T*, int>' this will be `T*'.)  The arguments will also include
   any outer template arguments.  The TREE_VALUE holds the innermost
   template parameters for the specialization (e.g., `T' in the
   example above.)  The TREE_TYPE is the _TYPE node for the partial
   specialization.

   This list is not used for static variable templates.  */
#define DECL_TEMPLATE_SPECIALIZATIONS(NODE)     DECL_SIZE (NODE)

/* Nonzero for a DECL which is actually a template parameter.  Keep
   these checks in ascending tree code order.   */
#define DECL_TEMPLATE_PARM_P(NODE)		\
  (DECL_LANG_FLAG_0 (NODE)			\
   && (TREE_CODE (NODE) == CONST_DECL		\
       || TREE_CODE (NODE) == PARM_DECL		\
       || TREE_CODE (NODE) == TYPE_DECL		\
       || TREE_CODE (NODE) == TEMPLATE_DECL))

/* Mark NODE as a template parameter.  */
#define SET_DECL_TEMPLATE_PARM_P(NODE) \
  (DECL_LANG_FLAG_0 (NODE) = 1)

/* Nonzero if NODE is a template template parameter.  */
#define DECL_TEMPLATE_TEMPLATE_PARM_P(NODE) \
  (TREE_CODE (NODE) == TEMPLATE_DECL && DECL_TEMPLATE_PARM_P (NODE))

/* Nonzero if NODE is a TEMPLATE_DECL representing an
   UNBOUND_CLASS_TEMPLATE tree node.  */
#define DECL_UNBOUND_CLASS_TEMPLATE_P(NODE) \
  (TREE_CODE (NODE) == TEMPLATE_DECL && !DECL_TEMPLATE_RESULT (NODE))

#define DECL_FUNCTION_TEMPLATE_P(NODE)  \
  (TREE_CODE (NODE) == TEMPLATE_DECL \
   && !DECL_UNBOUND_CLASS_TEMPLATE_P (NODE) \
   && TREE_CODE (DECL_TEMPLATE_RESULT (NODE)) == FUNCTION_DECL)

/* Nonzero for a DECL that represents a template class.  */
#define DECL_CLASS_TEMPLATE_P(NODE) \
  (TREE_CODE (NODE) == TEMPLATE_DECL \
   && !DECL_UNBOUND_CLASS_TEMPLATE_P (NODE) \
   && TREE_CODE (DECL_TEMPLATE_RESULT (NODE)) == TYPE_DECL \
   && !DECL_TEMPLATE_TEMPLATE_PARM_P (NODE))

/* Nonzero if NODE which declares a type.  */
#define DECL_DECLARES_TYPE_P(NODE) \
  (TREE_CODE (NODE) == TYPE_DECL || DECL_CLASS_TEMPLATE_P (NODE))

/* Nonzero if NODE is the typedef implicitly generated for a type when
   the type is declared.  In C++, `struct S {};' is roughly
   equivalent to `struct S {}; typedef struct S S;' in C.
   DECL_IMPLICIT_TYPEDEF_P will hold for the typedef indicated in this
   example.  In C++, there is a second implicit typedef for each
   class, in the scope of `S' itself, so that you can say `S::S'.
   DECL_SELF_REFERENCE_P will hold for that second typedef.  */
#define DECL_IMPLICIT_TYPEDEF_P(NODE) \
  (TREE_CODE (NODE) == TYPE_DECL && DECL_LANG_FLAG_2 (NODE))
#define SET_DECL_IMPLICIT_TYPEDEF_P(NODE) \
  (DECL_LANG_FLAG_2 (NODE) = 1)
#define DECL_SELF_REFERENCE_P(NODE) \
  (TREE_CODE (NODE) == TYPE_DECL && DECL_LANG_FLAG_4 (NODE))
#define SET_DECL_SELF_REFERENCE_P(NODE) \
  (DECL_LANG_FLAG_4 (NODE) = 1)

/* A `primary' template is one that has its own template header.  A
   member function of a class template is a template, but not primary.
   A member template is primary.  Friend templates are primary, too.  */

/* Returns the primary template corresponding to these parameters.  */
#define DECL_PRIMARY_TEMPLATE(NODE) \
  (TREE_TYPE (DECL_INNERMOST_TEMPLATE_PARMS (NODE)))

/* Returns nonzero if NODE is a primary template.  */
#define PRIMARY_TEMPLATE_P(NODE) (DECL_PRIMARY_TEMPLATE (NODE) == (NODE))

/* Non-zero iff NODE is a specialization of a template.  The value
   indicates the type of specializations:

     1=implicit instantiation

     2=partial or explicit specialization, e.g.:

        template <> int min<int> (int, int),

     3=explicit instantiation, e.g.:
  
        template int min<int> (int, int);

   Note that NODE will be marked as a specialization even if the
   template it is instantiating is not a primary template.  For
   example, given:

     template <typename T> struct O { 
       void f();
       struct I {}; 
     };
    
   both O<int>::f and O<int>::I will be marked as instantiations.

   If DECL_USE_TEMPLATE is non-zero, then DECL_TEMPLATE_INFO will also
   be non-NULL.  */
#define DECL_USE_TEMPLATE(NODE) (DECL_LANG_SPECIFIC (NODE)->decl_flags.use_template)

/* Like DECL_USE_TEMPLATE, but for class types.  */
#define CLASSTYPE_USE_TEMPLATE(NODE) \
  (LANG_TYPE_CLASS_CHECK (NODE)->use_template)

/* True if NODE is a specialization of a primary template.  */
#define CLASSTYPE_SPECIALIZATION_OF_PRIMARY_TEMPLATE_P(NODE)	\
  (CLASS_TYPE_P (NODE)						\
   && CLASSTYPE_USE_TEMPLATE (NODE)				\
   && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (arg)))  

#define DECL_TEMPLATE_INSTANTIATION(NODE) (DECL_USE_TEMPLATE (NODE) & 1)
#define CLASSTYPE_TEMPLATE_INSTANTIATION(NODE) \
  (CLASSTYPE_USE_TEMPLATE (NODE) & 1)

#define DECL_TEMPLATE_SPECIALIZATION(NODE) (DECL_USE_TEMPLATE (NODE) == 2)
#define SET_DECL_TEMPLATE_SPECIALIZATION(NODE) (DECL_USE_TEMPLATE (NODE) = 2)

/* Returns true for an explicit or partial specialization of a class
   template.  */
#define CLASSTYPE_TEMPLATE_SPECIALIZATION(NODE) \
  (CLASSTYPE_USE_TEMPLATE (NODE) == 2)
#define SET_CLASSTYPE_TEMPLATE_SPECIALIZATION(NODE) \
  (CLASSTYPE_USE_TEMPLATE (NODE) = 2)

#define DECL_IMPLICIT_INSTANTIATION(NODE) (DECL_USE_TEMPLATE (NODE) == 1)
#define SET_DECL_IMPLICIT_INSTANTIATION(NODE) (DECL_USE_TEMPLATE (NODE) = 1)
#define CLASSTYPE_IMPLICIT_INSTANTIATION(NODE) \
  (CLASSTYPE_USE_TEMPLATE (NODE) == 1)
#define SET_CLASSTYPE_IMPLICIT_INSTANTIATION(NODE) \
  (CLASSTYPE_USE_TEMPLATE (NODE) = 1)

#define DECL_EXPLICIT_INSTANTIATION(NODE) (DECL_USE_TEMPLATE (NODE) == 3)
#define SET_DECL_EXPLICIT_INSTANTIATION(NODE) (DECL_USE_TEMPLATE (NODE) = 3)
#define CLASSTYPE_EXPLICIT_INSTANTIATION(NODE) \
  (CLASSTYPE_USE_TEMPLATE (NODE) == 3)
#define SET_CLASSTYPE_EXPLICIT_INSTANTIATION(NODE) \
  (CLASSTYPE_USE_TEMPLATE (NODE) = 3)

/* Nonzero if DECL is a friend function which is an instantiation
   from the point of view of the compiler, but not from the point of
   view of the language.  For example given:
      template <class T> struct S { friend void f(T) {}; };
   the declaration of `void f(int)' generated when S<int> is
   instantiated will not be a DECL_TEMPLATE_INSTANTIATION, but will be
   a DECL_FRIEND_PSUEDO_TEMPLATE_INSTANTIATION.  */
#define DECL_FRIEND_PSEUDO_TEMPLATE_INSTANTIATION(DECL) \
  (DECL_TEMPLATE_INFO (DECL) && !DECL_USE_TEMPLATE (DECL))

/* Nonzero iff we are currently processing a declaration for an
   entity with its own template parameter list, and which is not a
   full specialization.  */
#define PROCESSING_REAL_TEMPLATE_DECL_P() \
  (processing_template_decl > template_class_depth (current_scope ()))

/* Nonzero if this VAR_DECL or FUNCTION_DECL has already been
   instantiated, i.e. its definition has been generated from the
   pattern given in the template.  */
#define DECL_TEMPLATE_INSTANTIATED(NODE) \
  DECL_LANG_FLAG_1 (VAR_OR_FUNCTION_DECL_CHECK (NODE))

/* We know what we're doing with this decl now.  */
#define DECL_INTERFACE_KNOWN(NODE) DECL_LANG_FLAG_5 (NODE)

/* DECL_EXTERNAL must be set on a decl until the decl is actually emitted,
   so that assemble_external will work properly.  So we have this flag to
   tell us whether the decl is really not external.  */
#define DECL_NOT_REALLY_EXTERN(NODE) \
  (DECL_LANG_SPECIFIC (NODE)->decl_flags.not_really_extern)

#define DECL_REALLY_EXTERN(NODE) \
  (DECL_EXTERNAL (NODE) && ! DECL_NOT_REALLY_EXTERN (NODE))

/* A thunk is a stub function.

   A thunk is an alternate entry point for an ordinary FUNCTION_DECL.
   The address of the ordinary FUNCTION_DECL is given by the
   DECL_INITIAL, which is always an ADDR_EXPR whose operand is a
   FUNCTION_DECL.  The job of the thunk is to either adjust the this
   pointer before transferring control to the FUNCTION_DECL, or call
   FUNCTION_DECL and then adjust the result value. Note, the result
   pointer adjusting thunk must perform a call to the thunked
   function, (or be implemented via passing some invisible parameter
   to the thunked function, which is modified to perform the
   adjustment just before returning).

   A thunk may perform either, or both, of the following operations:

   o Adjust the this or result pointer by a constant offset.
   o Adjust the this or result pointer by looking up a vcall or vbase offset
     in the vtable.

   A this pointer adjusting thunk converts from a base to a derived
   class, and hence adds the offsets. A result pointer adjusting thunk
   converts from a derived class to a base, and hence subtracts the
   offsets.  If both operations are performed, then the constant
   adjustment is performed first for this pointer adjustment and last
   for the result pointer adjustment.

   The constant adjustment is given by THUNK_FIXED_OFFSET.  If the
   vcall or vbase offset is required, THUNK_VIRTUAL_OFFSET is
   used. For this pointer adjusting thunks, it is the vcall offset
   into the vtable.  For result pointer adjusting thunks it is the
   binfo of the virtual base to convert to.  Use that binfo's vbase
   offset.

   It is possible to have equivalent covariant thunks.  These are
   distinct virtual covariant thunks whose vbase offsets happen to
   have the same value.  THUNK_ALIAS is used to pick one as the
   canonical thunk, which will get all the this pointer adjusting
   thunks attached to it.  */

/* An integer indicating how many bytes should be subtracted from the
   this or result pointer when this function is called.  */
#define THUNK_FIXED_OFFSET(DECL) \
  (DECL_LANG_SPECIFIC (THUNK_FUNCTION_CHECK (DECL))->u.f.u5.fixed_offset)

/* A tree indicating how to perform the virtual adjustment. For a this
   adjusting thunk it is the number of bytes to be added to the vtable
   to find the vcall offset. For a result adjusting thunk, it is the
   binfo of the relevant virtual base.  If NULL, then there is no
   virtual adjust.  (The vptr is always located at offset zero from
   the this or result pointer.)  (If the covariant type is within the
   class hierarchy being laid out, the vbase index is not yet known
   at the point we need to create the thunks, hence the need to use
   binfos.)  */

#define THUNK_VIRTUAL_OFFSET(DECL) \
  (LANG_DECL_U2_CHECK (FUNCTION_DECL_CHECK (DECL), 0)->access)

/* A thunk which is equivalent to another thunk.  */
#define THUNK_ALIAS(DECL) \
  (DECL_LANG_SPECIFIC (FUNCTION_DECL_CHECK (DECL))->decl_flags.u.template_info)

/* For thunk NODE, this is the FUNCTION_DECL thunked to.  It is
   possible for the target to be a thunk too.  */
#define THUNK_TARGET(NODE)				\
  (DECL_LANG_SPECIFIC (NODE)->u.f.befriending_classes)

/* True for a SCOPE_REF iff the "template" keyword was used to
   indicate that the qualified name denotes a template.  */
#define QUALIFIED_NAME_IS_TEMPLATE(NODE) \
  (TREE_LANG_FLAG_0 (SCOPE_REF_CHECK (NODE)))

/* True for an OMP_ATOMIC that has dependent parameters.  These are stored
   as bare LHS/RHS, and not as ADDR/RHS, as in the generic statement.  */
#define OMP_ATOMIC_DEPENDENT_P(NODE) \
  (TREE_LANG_FLAG_0 (OMP_ATOMIC_CHECK (NODE)))

/* Used to store the operation code when OMP_ATOMIC_DEPENDENT_P is set.  */
#define OMP_ATOMIC_CODE(NODE) \
  (OMP_ATOMIC_CHECK (NODE)->exp.complexity)

/* Used while gimplifying continue statements bound to OMP_FOR nodes.  */
#define OMP_FOR_GIMPLIFYING_P(NODE) \
  (TREE_LANG_FLAG_0 (OMP_FOR_CHECK (NODE)))

/* A language-specific token attached to the OpenMP data clauses to
   hold code (or code fragments) related to ctors, dtors, and op=.
   See semantics.c for details.  */
#define CP_OMP_CLAUSE_INFO(NODE) \
  TREE_TYPE (OMP_CLAUSE_RANGE_CHECK (NODE, OMP_CLAUSE_PRIVATE, \
				     OMP_CLAUSE_COPYPRIVATE))

/* These macros provide convenient access to the various _STMT nodes
   created when parsing template declarations.  */
#define TRY_STMTS(NODE)		TREE_OPERAND (TRY_BLOCK_CHECK (NODE), 0)
#define TRY_HANDLERS(NODE)	TREE_OPERAND (TRY_BLOCK_CHECK (NODE), 1)

#define EH_SPEC_STMTS(NODE)	TREE_OPERAND (EH_SPEC_BLOCK_CHECK (NODE), 0)
#define EH_SPEC_RAISES(NODE)	TREE_OPERAND (EH_SPEC_BLOCK_CHECK (NODE), 1)

#define USING_STMT_NAMESPACE(NODE) TREE_OPERAND (USING_STMT_CHECK (NODE), 0)

/* Nonzero if this try block is a function try block.  */
#define FN_TRY_BLOCK_P(NODE)	TREE_LANG_FLAG_3 (TRY_BLOCK_CHECK (NODE))
#define HANDLER_PARMS(NODE)	TREE_OPERAND (HANDLER_CHECK (NODE), 0)
#define HANDLER_BODY(NODE)	TREE_OPERAND (HANDLER_CHECK (NODE), 1)
#define HANDLER_TYPE(NODE)	TREE_TYPE (HANDLER_CHECK (NODE))

/* CLEANUP_STMT accessors.  The statement(s) covered, the cleanup to run
   and the VAR_DECL for which this cleanup exists.  */
#define CLEANUP_BODY(NODE)	TREE_OPERAND (CLEANUP_STMT_CHECK (NODE), 0)
#define CLEANUP_EXPR(NODE)	TREE_OPERAND (CLEANUP_STMT_CHECK (NODE), 1)
#define CLEANUP_DECL(NODE)	TREE_OPERAND (CLEANUP_STMT_CHECK (NODE), 2)

/* IF_STMT accessors. These give access to the condition of the if
   statement, the then block of the if statement, and the else block
   of the if statement if it exists.  */
#define IF_COND(NODE)		TREE_OPERAND (IF_STMT_CHECK (NODE), 0)
#define THEN_CLAUSE(NODE)	TREE_OPERAND (IF_STMT_CHECK (NODE), 1)
#define ELSE_CLAUSE(NODE)	TREE_OPERAND (IF_STMT_CHECK (NODE), 2)

/* WHILE_STMT accessors. These give access to the condition of the
   while statement and the body of the while statement, respectively.  */
#define WHILE_COND(NODE)	TREE_OPERAND (WHILE_STMT_CHECK (NODE), 0)
#define WHILE_BODY(NODE)	TREE_OPERAND (WHILE_STMT_CHECK (NODE), 1)
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
#define WHILE_ATTRIBUTES(NODE)	TREE_OPERAND (WHILE_STMT_CHECK (NODE), 2)

/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
/* DO_STMT accessors. These give access to the condition of the do
   statement and the body of the do statement, respectively.  */
#define DO_COND(NODE)		TREE_OPERAND (DO_STMT_CHECK (NODE), 0)
#define DO_BODY(NODE)		TREE_OPERAND (DO_STMT_CHECK (NODE), 1)
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
#define DO_ATTRIBUTES(NODE)	TREE_OPERAND (DO_STMT_CHECK (NODE), 2)
/* APPLE LOCAL begin C* language */
/* Used as a flag to indicate synthesized inner do-while loop of a 
   foreach statement.  Used for generation of break/continue statement 
   of the loop. */
#define DO_FOREACH(NODE)           TREE_OPERAND (DO_STMT_CHECK (NODE), 3)
/* APPLE LOCAL end C* language */

/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
/* FOR_STMT accessors. These give access to the init statement,
   condition, update expression, and body of the for statement,
   respectively.  */
#define FOR_INIT_STMT(NODE)	TREE_OPERAND (FOR_STMT_CHECK (NODE), 0)
#define FOR_COND(NODE)		TREE_OPERAND (FOR_STMT_CHECK (NODE), 1)
#define FOR_EXPR(NODE)		TREE_OPERAND (FOR_STMT_CHECK (NODE), 2)
#define FOR_BODY(NODE)		TREE_OPERAND (FOR_STMT_CHECK (NODE), 3)
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
#define FOR_ATTRIBUTES(NODE)	TREE_OPERAND (FOR_STMT_CHECK (NODE), 4)

/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
#define SWITCH_STMT_COND(NODE)	TREE_OPERAND (SWITCH_STMT_CHECK (NODE), 0)
#define SWITCH_STMT_BODY(NODE)	TREE_OPERAND (SWITCH_STMT_CHECK (NODE), 1)
#define SWITCH_STMT_TYPE(NODE)	TREE_OPERAND (SWITCH_STMT_CHECK (NODE), 2)

/* STMT_EXPR accessor.  */
#define STMT_EXPR_STMT(NODE)	TREE_OPERAND (STMT_EXPR_CHECK (NODE), 0)

/* EXPR_STMT accessor. This gives the expression associated with an
   expression statement.  */
#define EXPR_STMT_EXPR(NODE)	TREE_OPERAND (EXPR_STMT_CHECK (NODE), 0)

/* True if this TARGET_EXPR was created by build_cplus_new, and so we can
   discard it if it isn't useful.  */
#define TARGET_EXPR_IMPLICIT_P(NODE) \
  TREE_LANG_FLAG_0 (TARGET_EXPR_CHECK (NODE))

/* An enumeration of the kind of tags that C++ accepts.  */
enum tag_types {
  none_type = 0, /* Not a tag type.  */
  record_type,   /* "struct" types.  */
  class_type,    /* "class" types.  */
  union_type,    /* "union" types.  */
  enum_type,     /* "enum" types.  */
  typename_type  /* "typename" types.  */
};

/* The various kinds of lvalues we distinguish.  */
typedef enum cp_lvalue_kind {
  clk_none = 0,     /* Things that are not an lvalue.  */
  clk_ordinary = 1, /* An ordinary lvalue.  */
  clk_class = 2,    /* An rvalue of class-type.  */
  clk_bitfield = 4, /* An lvalue for a bit-field.  */
  clk_packed = 8    /* An lvalue for a packed field.  */
} cp_lvalue_kind;

/* Various kinds of template specialization, instantiation, etc.  */
typedef enum tmpl_spec_kind {
  tsk_none,		   /* Not a template at all.  */
  tsk_invalid_member_spec, /* An explicit member template
			      specialization, but the enclosing
			      classes have not all been explicitly
			      specialized.  */
  tsk_invalid_expl_inst,   /* An explicit instantiation containing
			      template parameter lists.  */
  tsk_excessive_parms,	   /* A template declaration with too many
			      template parameter lists.  */
  tsk_insufficient_parms,  /* A template declaration with too few
			      parameter lists.  */
  tsk_template,		   /* A template declaration.  */
  tsk_expl_spec,	   /* An explicit specialization.  */
  tsk_expl_inst		   /* An explicit instantiation.  */
} tmpl_spec_kind;

/* The various kinds of access.  BINFO_ACCESS depends on these being
   two bit quantities.  The numerical values are important; they are
   used to initialize RTTI data structures, so changing them changes
   the ABI.  */
typedef enum access_kind {
  ak_none = 0,		   /* Inaccessible.  */
  ak_public = 1,	   /* Accessible, as a `public' thing.  */
  ak_protected = 2,	   /* Accessible, as a `protected' thing.  */
  ak_private = 3	   /* Accessible, as a `private' thing.  */
} access_kind;

/* The various kinds of special functions.  If you add to this list,
   you should update special_function_p as well.  */
typedef enum special_function_kind {
  sfk_none = 0,		   /* Not a special function.  This enumeral
			      must have value zero; see
			      special_function_p.  */
  sfk_constructor,	   /* A constructor.  */
  sfk_copy_constructor,    /* A copy constructor.  */
  sfk_assignment_operator, /* An assignment operator.  */
  sfk_destructor,	   /* A destructor.  */
  sfk_complete_destructor, /* A destructor for complete objects.  */
  sfk_base_destructor,     /* A destructor for base subobjects.  */
  sfk_deleting_destructor, /* A destructor for complete objects that
			      deletes the object after it has been
			      destroyed.  */
  sfk_conversion	   /* A conversion operator.  */
} special_function_kind;

/* The various kinds of linkage.  From [basic.link],

      A name is said to have linkage when it might denote the same
      object, reference, function, type, template, namespace or value
      as a name introduced in another scope:

      -- When a name has external linkage, the entity it denotes can
	 be referred to from scopes of other translation units or from
	 other scopes of the same translation unit.

      -- When a name has internal linkage, the entity it denotes can
	 be referred to by names from other scopes in the same
	 translation unit.

      -- When a name has no linkage, the entity it denotes cannot be
	 referred to by names from other scopes.  */

typedef enum linkage_kind {
  lk_none,			/* No linkage.  */
  lk_internal,			/* Internal linkage.  */
  lk_external			/* External linkage.  */
} linkage_kind;

/* Bitmask flags to control type substitution.  */
typedef enum tsubst_flags_t {
  tf_none = 0,			/* nothing special */
  tf_error = 1 << 0,		/* give error messages  */
  tf_warning = 1 << 1,		/* give warnings too  */
  tf_ignore_bad_quals = 1 << 2,	/* ignore bad cvr qualifiers */
  tf_keep_type_decl = 1 << 3,	/* retain typedef type decls
				   (make_typename_type use) */
  tf_ptrmem_ok = 1 << 4,	/* pointers to member ok (internal
				   instantiate_type use) */
  tf_user = 1 << 5,		/* found template must be a user template
				   (lookup_template_class use) */
  tf_conv = 1 << 6,		/* We are determining what kind of
				   conversion might be permissible,
				   not actually performing the
				   conversion.  */
  /* Convenient substitution flags combinations.  */
  tf_warning_or_error = tf_warning | tf_error
} tsubst_flags_t;

/* The kind of checking we can do looking in a class hierarchy.  */
typedef enum base_access {
  ba_any = 0,  /* Do not check access, allow an ambiguous base,
		      prefer a non-virtual base */
  ba_unique = 1 << 0,  /* Must be a unique base.  */
  ba_check_bit = 1 << 1,   /* Check access.  */
  ba_check = ba_unique | ba_check_bit,
  ba_ignore_scope = 1 << 2, /* Ignore access allowed by local scope.  */
  ba_quiet = 1 << 3     /* Do not issue error messages.  */
} base_access;

/* The various kinds of access check during parsing.  */
typedef enum deferring_kind {
  dk_no_deferred = 0, /* Check access immediately */
  dk_deferred = 1,    /* Deferred check */
  dk_no_check = 2     /* No access check */
} deferring_kind;

/* The kind of base we can find, looking in a class hierarchy.
   Values <0 indicate we failed.  */
typedef enum base_kind {
  bk_inaccessible = -3,   /* The base is inaccessible */
  bk_ambig = -2,	  /* The base is ambiguous */
  bk_not_base = -1,	  /* It is not a base */
  bk_same_type = 0,	  /* It is the same type */
  bk_proper_base = 1,	  /* It is a proper base */
  bk_via_virtual = 2	  /* It is a proper base, but via a virtual
			     path. This might not be the canonical
			     binfo.  */
} base_kind;

/* Node for "pointer to (virtual) function".
   This may be distinct from ptr_type_node so gdb can distinguish them.  */
#define vfunc_ptr_type_node  vtable_entry_type


/* For building calls to `delete'.  */
extern GTY(()) tree integer_two_node;
extern GTY(()) tree integer_three_node;

/* The number of function bodies which we are currently processing.
   (Zero if we are at namespace scope, one inside the body of a
   function, two inside the body of a function in a local class, etc.)  */
extern int function_depth;

/* in pt.c  */

/* These values are used for the `STRICT' parameter to type_unification and
   fn_type_unification.  Their meanings are described with the
   documentation for fn_type_unification.  */

typedef enum unification_kind_t {
  DEDUCE_CALL,
  DEDUCE_CONV,
  DEDUCE_EXACT
} unification_kind_t;

/* Macros for operating on a template instantiation level node.  */

#define TINST_DECL(NODE) \
  (((tinst_level_t) TINST_LEVEL_CHECK (NODE))->decl)
#define TINST_LOCATION(NODE) \
  (((tinst_level_t) TINST_LEVEL_CHECK (NODE))->locus)
#define TINST_IN_SYSTEM_HEADER_P(NODE) \
  (((tinst_level_t) TINST_LEVEL_CHECK (NODE))->in_system_header_p)

/* in class.c */

extern int current_class_depth;

/* An array of all local classes present in this translation unit, in
   declaration order.  */
extern GTY(()) VEC(tree,gc) *local_classes;

/* Here's where we control how name mangling takes place.  */

/* Cannot use '$' up front, because this confuses gdb
   (names beginning with '$' are gdb-local identifiers).

   Note that all forms in which the '$' is significant are long enough
   for direct indexing (meaning that if we know there is a '$'
   at a particular location, we can index into the string at
   any other location that provides distinguishing characters).  */

/* Define NO_DOLLAR_IN_LABEL in your favorite tm file if your assembler
   doesn't allow '$' in symbol names.  */
#ifndef NO_DOLLAR_IN_LABEL

#define JOINER '$'

#define AUTO_TEMP_NAME "_$tmp_"
#define VFIELD_BASE "$vf"
#define VFIELD_NAME "_vptr$"
#define VFIELD_NAME_FORMAT "_vptr$%s"
#define ANON_AGGRNAME_FORMAT "$_%d"

#else /* NO_DOLLAR_IN_LABEL */

#ifndef NO_DOT_IN_LABEL

#define JOINER '.'

#define AUTO_TEMP_NAME "_.tmp_"
#define VFIELD_BASE ".vf"
#define VFIELD_NAME "_vptr."
#define VFIELD_NAME_FORMAT "_vptr.%s"

#define ANON_AGGRNAME_FORMAT "._%d"

#else /* NO_DOT_IN_LABEL */

#define IN_CHARGE_NAME "__in_chrg"
#define AUTO_TEMP_NAME "__tmp_"
#define TEMP_NAME_P(ID_NODE) \
  (!strncmp (IDENTIFIER_POINTER (ID_NODE), AUTO_TEMP_NAME, \
	     sizeof (AUTO_TEMP_NAME) - 1))
#define VTABLE_NAME "__vt_"
#define VTABLE_NAME_P(ID_NODE) \
  (!strncmp (IDENTIFIER_POINTER (ID_NODE), VTABLE_NAME, \
	     sizeof (VTABLE_NAME) - 1))
#define VFIELD_BASE "__vfb"
#define VFIELD_NAME "__vptr_"
#define VFIELD_NAME_P(ID_NODE) \
  (!strncmp (IDENTIFIER_POINTER (ID_NODE), VFIELD_NAME, \
	    sizeof (VFIELD_NAME) - 1))
#define VFIELD_NAME_FORMAT "__vptr_%s"

#define ANON_AGGRNAME_PREFIX "__anon_"
#define ANON_AGGRNAME_P(ID_NODE) \
  (!strncmp (IDENTIFIER_POINTER (ID_NODE), ANON_AGGRNAME_PREFIX, \
	     sizeof (ANON_AGGRNAME_PREFIX) - 1))
#define ANON_AGGRNAME_FORMAT "__anon_%d"

#endif	/* NO_DOT_IN_LABEL */
#endif	/* NO_DOLLAR_IN_LABEL */

#define THIS_NAME "this"

#define IN_CHARGE_NAME "__in_chrg"

#define VTBL_PTR_TYPE		"__vtbl_ptr_type"
#define VTABLE_DELTA_NAME	"__delta"
#define VTABLE_PFN_NAME		"__pfn"

#if !defined(NO_DOLLAR_IN_LABEL) || !defined(NO_DOT_IN_LABEL)

#define VTABLE_NAME_P(ID_NODE) (IDENTIFIER_POINTER (ID_NODE)[1] == 'v' \
  && IDENTIFIER_POINTER (ID_NODE)[2] == 't' \
  && IDENTIFIER_POINTER (ID_NODE)[3] == JOINER)

#define TEMP_NAME_P(ID_NODE) \
  (!strncmp (IDENTIFIER_POINTER (ID_NODE), AUTO_TEMP_NAME, sizeof (AUTO_TEMP_NAME)-1))
#define VFIELD_NAME_P(ID_NODE) \
  (!strncmp (IDENTIFIER_POINTER (ID_NODE), VFIELD_NAME, sizeof(VFIELD_NAME)-1))

/* For anonymous aggregate types, we need some sort of name to
   hold on to.  In practice, this should not appear, but it should
   not be harmful if it does.  */
#define ANON_AGGRNAME_P(ID_NODE) (IDENTIFIER_POINTER (ID_NODE)[0] == JOINER \
				  && IDENTIFIER_POINTER (ID_NODE)[1] == '_')
#endif /* !defined(NO_DOLLAR_IN_LABEL) || !defined(NO_DOT_IN_LABEL) */


/* Nonzero if we're done parsing and into end-of-file activities.  */

extern int at_eof;

/* A list of namespace-scope objects which have constructors or
   destructors which reside in the global scope.  The decl is stored
   in the TREE_VALUE slot and the initializer is stored in the
   TREE_PURPOSE slot.  */
extern GTY(()) tree static_aggregates;

/* Functions called along with real static constructors and destructors.  */

extern GTY(()) tree static_ctors;
extern GTY(()) tree static_dtors;

enum overload_flags { NO_SPECIAL = 0, DTOR_FLAG, OP_FLAG, TYPENAME_FLAG };

/* These are uses as bits in flags passed to various functions to
   control their behavior.  Despite the LOOKUP_ prefix, many of these
   do not control name lookup.  ??? Functions using these flags should
   probably be modified to accept explicit boolean flags for the
   behaviors relevant to them.  */
/* Check for access violations.  */
#define LOOKUP_PROTECT (1 << 0)
/* Complain if no suitable member function matching the arguments is
   found.  */
#define LOOKUP_COMPLAIN (1 << 1)
#define LOOKUP_NORMAL (LOOKUP_PROTECT | LOOKUP_COMPLAIN)
/* Even if the function found by lookup is a virtual function, it
   should be called directly.  */
#define LOOKUP_NONVIRTUAL (1 << 2)
/* Non-converting (i.e., "explicit") constructors are not tried.  */
#define LOOKUP_ONLYCONVERTING (1 << 3)
/* If a temporary is created, it should be created so that it lives
   as long as the current variable bindings; otherwise it only lives
   until the end of the complete-expression.  It also forces
   direct-initialization in cases where other parts of the compiler
   have already generated a temporary, such as reference
   initialization and the catch parameter.  */
#define DIRECT_BIND (1 << 4)
/* User-defined conversions are not permitted.  (Built-in conversions
   are permitted.)  */
#define LOOKUP_NO_CONVERSION (1 << 5)
/* The user has explicitly called a destructor.  (Therefore, we do
   not need to check that the object is non-NULL before calling the
   destructor.)  */
#define LOOKUP_DESTRUCTOR (1 << 6)
/* Do not permit references to bind to temporaries.  */
#define LOOKUP_NO_TEMP_BIND (1 << 7)
/* Do not accept objects, and possibly namespaces.  */
#define LOOKUP_PREFER_TYPES (1 << 8)
/* Do not accept objects, and possibly types.   */
#define LOOKUP_PREFER_NAMESPACES (1 << 9)
/* Accept types or namespaces.  */
#define LOOKUP_PREFER_BOTH (LOOKUP_PREFER_TYPES | LOOKUP_PREFER_NAMESPACES)
/* We are checking that a constructor can be called -- but we do not
   actually plan to call it.  */
#define LOOKUP_CONSTRUCTOR_CALLABLE (1 << 10)
/* Return friend declarations and un-declared builtin functions.
   (Normally, these entities are registered in the symbol table, but
   not found by lookup.)  */
#define LOOKUP_HIDDEN (LOOKUP_CONSTRUCTOR_CALLABLE << 1)

#define LOOKUP_NAMESPACES_ONLY(F)  \
  (((F) & LOOKUP_PREFER_NAMESPACES) && !((F) & LOOKUP_PREFER_TYPES))
#define LOOKUP_TYPES_ONLY(F)  \
  (!((F) & LOOKUP_PREFER_NAMESPACES) && ((F) & LOOKUP_PREFER_TYPES))
#define LOOKUP_QUALIFIERS_ONLY(F)     ((F) & LOOKUP_PREFER_BOTH)


/* These flags are used by the conversion code.
   CONV_IMPLICIT   :  Perform implicit conversions (standard and user-defined).
   CONV_STATIC     :  Perform the explicit conversions for static_cast.
   CONV_CONST      :  Perform the explicit conversions for const_cast.
   CONV_REINTERPRET:  Perform the explicit conversions for reinterpret_cast.
   CONV_PRIVATE    :  Perform upcasts to private bases.
   CONV_FORCE_TEMP :  Require a new temporary when converting to the same
		      aggregate type.  */

#define CONV_IMPLICIT    1
#define CONV_STATIC      2
#define CONV_CONST       4
#define CONV_REINTERPRET 8
#define CONV_PRIVATE	 16
/* #define CONV_NONCONVERTING 32 */
#define CONV_FORCE_TEMP  64
#define CONV_OLD_CONVERT (CONV_IMPLICIT | CONV_STATIC | CONV_CONST \
			  | CONV_REINTERPRET)
#define CONV_C_CAST      (CONV_IMPLICIT | CONV_STATIC | CONV_CONST \
			  | CONV_REINTERPRET | CONV_PRIVATE | CONV_FORCE_TEMP)

/* Used by build_expr_type_conversion to indicate which types are
   acceptable as arguments to the expression under consideration.  */

#define WANT_INT	1 /* integer types, including bool */
#define WANT_FLOAT	2 /* floating point types */
#define WANT_ENUM	4 /* enumerated types */
#define WANT_POINTER	8 /* pointer types */
#define WANT_NULL      16 /* null pointer constant */
#define WANT_VECTOR    32 /* vector types */
#define WANT_ARITH	(WANT_INT | WANT_FLOAT | WANT_VECTOR)

/* Used with comptypes, and related functions, to guide type
   comparison.  */

#define COMPARE_STRICT	      0 /* Just check if the types are the
				   same.  */
#define COMPARE_BASE	      1 /* Check to see if the second type is
				   derived from the first.  */
#define COMPARE_DERIVED	      2 /* Like COMPARE_BASE, but in
				   reverse.  */
#define COMPARE_REDECLARATION 4 /* The comparison is being done when
				   another declaration of an existing
				   entity is seen.  */

/* Used with push_overloaded_decl.  */
#define PUSH_GLOBAL	     0  /* Push the DECL into namespace scope,
				   regardless of the current scope.  */
#define PUSH_LOCAL	     1  /* Push the DECL into the current
				   scope.  */
#define PUSH_USING	     2  /* We are pushing this DECL as the
				   result of a using declaration.  */

/* Used with start function.  */
#define SF_DEFAULT	     0  /* No flags.  */
#define SF_PRE_PARSED	     1  /* The function declaration has
				   already been parsed.  */
#define SF_INCLASS_INLINE    2  /* The function is an inline, defined
				   in the class body.  */

/* Returns nonzero iff TYPE1 and TYPE2 are the same type, or if TYPE2
   is derived from TYPE1, or if TYPE2 is a pointer (reference) to a
   class derived from the type pointed to (referred to) by TYPE1.  */
#define same_or_base_type_p(TYPE1, TYPE2) \
  comptypes ((TYPE1), (TYPE2), COMPARE_BASE)

/* These macros are used to access a TEMPLATE_PARM_INDEX.  */
#define TEMPLATE_PARM_INDEX_CAST(NODE) \
	((template_parm_index*)TEMPLATE_PARM_INDEX_CHECK (NODE))
#define TEMPLATE_PARM_IDX(NODE) (TEMPLATE_PARM_INDEX_CAST (NODE)->index)
#define TEMPLATE_PARM_LEVEL(NODE) (TEMPLATE_PARM_INDEX_CAST (NODE)->level)
#define TEMPLATE_PARM_DESCENDANTS(NODE) (TREE_CHAIN (NODE))
#define TEMPLATE_PARM_ORIG_LEVEL(NODE) (TEMPLATE_PARM_INDEX_CAST (NODE)->orig_level)
#define TEMPLATE_PARM_DECL(NODE) (TEMPLATE_PARM_INDEX_CAST (NODE)->decl)

/* These macros are for accessing the fields of TEMPLATE_TYPE_PARM,
   TEMPLATE_TEMPLATE_PARM and BOUND_TEMPLATE_TEMPLATE_PARM nodes.  */
#define TEMPLATE_TYPE_PARM_INDEX(NODE)					 \
  (TREE_CHECK3 ((NODE), TEMPLATE_TYPE_PARM, TEMPLATE_TEMPLATE_PARM,	\
		BOUND_TEMPLATE_TEMPLATE_PARM))->type.values
#define TEMPLATE_TYPE_IDX(NODE) \
  (TEMPLATE_PARM_IDX (TEMPLATE_TYPE_PARM_INDEX (NODE)))
#define TEMPLATE_TYPE_LEVEL(NODE) \
  (TEMPLATE_PARM_LEVEL (TEMPLATE_TYPE_PARM_INDEX (NODE)))
#define TEMPLATE_TYPE_ORIG_LEVEL(NODE) \
  (TEMPLATE_PARM_ORIG_LEVEL (TEMPLATE_TYPE_PARM_INDEX (NODE)))
#define TEMPLATE_TYPE_DECL(NODE) \
  (TEMPLATE_PARM_DECL (TEMPLATE_TYPE_PARM_INDEX (NODE)))

/* These constants can used as bit flags in the process of tree formatting.

   TFF_PLAIN_IDENTIFIER: unqualified part of a name.
   TFF_SCOPE: include the class and namespace scope of the name.
   TFF_CHASE_TYPEDEF: print the original type-id instead of the typedef-name.
   TFF_DECL_SPECIFIERS: print decl-specifiers.
   TFF_CLASS_KEY_OR_ENUM: precede a class-type name (resp. enum name) with
       a class-key (resp. `enum').
   TFF_RETURN_TYPE: include function return type.
   TFF_FUNCTION_DEFAULT_ARGUMENTS: include function default parameter values.
   TFF_EXCEPTION_SPECIFICATION: show function exception specification.
   TFF_TEMPLATE_HEADER: show the template<...> header in a
       template-declaration.
   TFF_TEMPLATE_NAME: show only template-name.
   TFF_EXPR_IN_PARENS: parenthesize expressions.
   TFF_NO_FUNCTION_ARGUMENTS: don't show function arguments.  */

#define TFF_PLAIN_IDENTIFIER			(0)
#define TFF_SCOPE				(1)
#define TFF_CHASE_TYPEDEF			(1 << 1)
#define TFF_DECL_SPECIFIERS			(1 << 2)
#define TFF_CLASS_KEY_OR_ENUM			(1 << 3)
#define TFF_RETURN_TYPE				(1 << 4)
#define TFF_FUNCTION_DEFAULT_ARGUMENTS		(1 << 5)
#define TFF_EXCEPTION_SPECIFICATION		(1 << 6)
#define TFF_TEMPLATE_HEADER			(1 << 7)
#define TFF_TEMPLATE_NAME			(1 << 8)
#define TFF_EXPR_IN_PARENS			(1 << 9)
#define TFF_NO_FUNCTION_ARGUMENTS		(1 << 10)

/* Returns the TEMPLATE_DECL associated to a TEMPLATE_TEMPLATE_PARM
   node.  */
#define TEMPLATE_TEMPLATE_PARM_TEMPLATE_DECL(NODE)	\
  ((TREE_CODE (NODE) == BOUND_TEMPLATE_TEMPLATE_PARM)	\
   ? TYPE_TI_TEMPLATE (NODE)				\
   : TYPE_NAME (NODE))

/* in lex.c  */

extern void init_reswords (void);

/* Indexed by TREE_CODE, these tables give C-looking names to
   operators represented by TREE_CODES.  For example,
   opname_tab[(int) MINUS_EXPR] == "-".  */
extern const char **opname_tab, **assignop_tab;

typedef struct operator_name_info_t GTY(())
{
  /* The IDENTIFIER_NODE for the operator.  */
  tree identifier;
  /* The name of the operator.  */
  const char *name;
  /* The mangled name of the operator.  */
  const char *mangled_name;
  /* The arity of the operator.  */
  int arity;
} operator_name_info_t;

/* A mapping from tree codes to operator name information.  */
extern GTY(()) operator_name_info_t operator_name_info
  [(int) LAST_CPLUS_TREE_CODE];
/* Similar, but for assignment operators.  */
extern GTY(()) operator_name_info_t assignment_operator_name_info
  [(int) LAST_CPLUS_TREE_CODE];

/* A type-qualifier, or bitmask therefore, using the TYPE_QUAL
   constants.  */

typedef int cp_cv_quals;

/* A storage class.  */

typedef enum cp_storage_class {
  /* sc_none must be zero so that zeroing a cp_decl_specifier_seq
     sets the storage_class field to sc_none.  */
  sc_none = 0,
  sc_auto,
  sc_register,
  sc_static,
  sc_extern,
  sc_mutable
} cp_storage_class;

/* An individual decl-specifier.  */

typedef enum cp_decl_spec {
  ds_first,
  ds_signed = ds_first,
  ds_unsigned,
  ds_short,
  ds_long,
  ds_const,
  ds_volatile,
  ds_restrict,
  ds_inline,
  ds_virtual,
  ds_explicit,
  ds_friend,
  ds_typedef,
  ds_complex,
  ds_thread,
  ds_last
} cp_decl_spec;

/* A decl-specifier-seq.  */

typedef struct cp_decl_specifier_seq {
  /* The number of times each of the keywords has been seen.  */
  unsigned specs[(int) ds_last];
  /* The primary type, if any, given by the decl-specifier-seq.
     Modifiers, like "short", "const", and "unsigned" are not
     reflected here.  This field will be a TYPE, unless a typedef-name
     was used, in which case it will be a TYPE_DECL.  */
  tree type;
  /* The attributes, if any, provided with the specifier sequence.  */
  tree attributes;
  /* If non-NULL, a built-in type that the user attempted to redefine
     to some other type.  */
  tree redefined_builtin_type;
  /* The storage class specified -- or sc_none if no storage class was
     explicitly specified.  */
  cp_storage_class storage_class;
  /* True iff TYPE_SPEC indicates a user-defined type.  */
  BOOL_BITFIELD user_defined_type_p : 1;
  /* True iff multiple types were (erroneously) specified for this
     decl-specifier-seq.  */
  BOOL_BITFIELD multiple_types_p : 1;
  /* True iff multiple storage classes were (erroneously) specified
     for this decl-specifier-seq or a combination of a storage class
     with a typedef specifier.  */
  BOOL_BITFIELD conflicting_specifiers_p : 1;
  /* True iff at least one decl-specifier was found.  */
  BOOL_BITFIELD any_specifiers_p : 1;
  /* True iff "int" was explicitly provided.  */
  BOOL_BITFIELD explicit_int_p : 1;
  /* True iff "char" was explicitly provided.  */
  BOOL_BITFIELD explicit_char_p : 1;
} cp_decl_specifier_seq;

/* The various kinds of declarators.  */

typedef enum cp_declarator_kind {
  cdk_id,
  cdk_function,
  cdk_array,
  cdk_pointer,
  cdk_reference,
  cdk_ptrmem,
  /* APPLE LOCAL blocks 6040305 (ch) */
  cdk_block_pointer,
  cdk_error
} cp_declarator_kind;

/* A declarator.  */

typedef struct cp_declarator cp_declarator;

typedef struct cp_parameter_declarator cp_parameter_declarator;

/* A parameter, before it has been semantically analyzed.  */
struct cp_parameter_declarator {
  /* The next parameter, or NULL_TREE if none.  */
  cp_parameter_declarator *next;
  /* The decl-specifiers-seq for the parameter.  */
  cp_decl_specifier_seq decl_specifiers;
  /* The declarator for the parameter.  */
  cp_declarator *declarator;
  /* The default-argument expression, or NULL_TREE, if none.  */
  tree default_argument;
  /* True iff this is the first parameter in the list and the
     parameter sequence ends with an ellipsis.  */
  bool ellipsis_p;
};

/* A declarator.  */
struct cp_declarator {
  /* The kind of declarator.  */
  cp_declarator_kind kind;
  /* Attributes that apply to this declarator.  */
  tree attributes;
  /* For all but cdk_id and cdk_error, the contained declarator.  For
     cdk_id and cdk_error, guaranteed to be NULL.  */
  cp_declarator *declarator;
  location_t id_loc; /* Currently only set for cdk_id. */
  union {
    /* For identifiers.  */
    struct {
      /* If non-NULL, the qualifying scope (a NAMESPACE_DECL or
	 *_TYPE) for this identifier.  */
      tree qualifying_scope;
      /* The unqualified name of the entity -- an IDENTIFIER_NODE,
	 BIT_NOT_EXPR, or TEMPLATE_ID_EXPR.  */
      tree unqualified_name;
      /* If this is the name of a function, what kind of special
	 function (if any).  */
      special_function_kind sfk;
    } id;
    /* For functions.  */
    struct {
      /* The parameters to the function.  */
      cp_parameter_declarator *parameters;
      /* The cv-qualifiers for the function.  */
      cp_cv_quals qualifiers;
      /* The exception-specification for the function.  */
      tree exception_specification;
    } function;
    /* For arrays.  */
    struct {
      /* The bounds to the array.  */
      tree bounds;
    } array;
    /* For cdk_pointer, cdk_reference, and cdk_ptrmem.  */
    struct {
      /* The cv-qualifiers for the pointer.  */
      cp_cv_quals qualifiers;
      /* For cdk_ptrmem, the class type containing the member.  */
      tree class_type;
    } pointer;
    /* APPLE LOCAL begin blocks 6040305 (ch) */
    /* For cdk_block_pointer.  */
    struct {
	 /* The cv-qualifiers for the pointer.  */
	 cp_cv_quals qualifiers;
    } block_pointer;
    /* APPLE LOCAL end blocks 6040305 (ch) */
  } u;
};

/* A parameter list indicating for a function with no parameters,
   e.g  "int f(void)".  */
extern cp_parameter_declarator *no_parameters;

/* in call.c */
extern bool check_dtor_name			(tree, tree);

extern tree build_vfield_ref			(tree, tree);
extern tree build_conditional_expr		(tree, tree, tree);
extern tree build_addr_func			(tree);
extern tree build_call				(tree, tree);
extern bool null_ptr_cst_p			(tree);
extern bool sufficient_parms_p			(tree);
extern tree type_decays_to			(tree);
extern tree build_user_type_conversion		(tree, tree, int);
extern tree build_new_function_call		(tree, tree, bool);
extern tree build_operator_new_call		(tree, tree, tree *, tree *,
						 tree *);
extern tree build_new_method_call		(tree, tree, tree, tree, int,
						 tree *);
extern tree build_special_member_call		(tree, tree, tree, tree, int);
extern tree build_new_op			(enum tree_code, int, tree, tree, tree, bool *);
extern tree build_op_delete_call		(enum tree_code, tree, tree, bool, tree, tree);
extern bool can_convert				(tree, tree);
extern bool can_convert_arg			(tree, tree, tree, int);
extern bool can_convert_arg_bad			(tree, tree, tree);
extern bool enforce_access			(tree, tree, tree);
extern tree convert_default_arg			(tree, tree, tree, int);
extern tree convert_arg_to_ellipsis		(tree);
extern tree build_x_va_arg			(tree, tree);
extern tree cxx_type_promotes_to		(tree);
extern tree type_passed_as			(tree);
extern tree convert_for_arg_passing		(tree, tree);
extern bool is_properly_derived_from		(tree, tree);
extern tree initialize_reference		(tree, tree, tree, tree *);
extern tree make_temporary_var_for_ref_to_temp	(tree, tree);
extern tree strip_top_quals			(tree);
extern tree perform_implicit_conversion		(tree, tree);
extern tree perform_direct_initialization_if_possible (tree, tree, bool);
extern tree in_charge_arg_for_name		(tree);
extern tree build_cxx_call			(tree, tree);
#ifdef ENABLE_CHECKING
extern void validate_conversion_obstack		(void);
#endif /* ENABLE_CHECKING */

/* in class.c */
extern tree build_base_path			(enum tree_code, tree,
						 tree, int);
extern tree convert_to_base			(tree, tree, bool, bool);
extern tree convert_to_base_statically		(tree, tree);
extern tree build_vtbl_ref			(tree, tree);
extern tree build_vfn_ref			(tree, tree);
extern tree get_vtable_decl			(tree, int);
extern void resort_type_method_vec		(void *, void *,
						 gt_pointer_operator, void *);
extern bool add_method				(tree, tree, tree);
extern bool currently_open_class		(tree);
extern tree currently_open_derived_class	(tree);
extern tree finish_struct			(tree, tree);
extern void finish_struct_1			(tree);
extern int resolves_to_fixed_type_p		(tree, int *);
extern void init_class_processing		(void);
extern int is_empty_class			(tree);
extern void pushclass				(tree);
extern void popclass				(void);
extern void push_nested_class			(tree);
extern void pop_nested_class			(void);
extern int current_lang_depth			(void);
extern void push_lang_context			(tree);
extern void pop_lang_context			(void);
extern tree instantiate_type			(tree, tree, tsubst_flags_t);
extern void print_class_statistics		(void);
extern void cxx_print_statistics		(void);
extern void cxx_print_xnode			(FILE *, tree, int);
extern void cxx_print_decl			(FILE *, tree, int);
extern void cxx_print_type			(FILE *, tree, int);
extern void cxx_print_identifier		(FILE *, tree, int);
extern void cxx_print_error_function	(struct diagnostic_context *,
						 const char *);
extern void build_self_reference		(void);
extern int same_signature_p			(tree, tree);
extern void maybe_add_class_template_decl_list	(tree, tree, int);
extern void unreverse_member_declarations	(tree);
extern void invalidate_class_lookup_cache	(void);
extern void maybe_note_name_used_in_class	(tree, tree);
extern void note_name_declared_in_class		(tree, tree);
extern tree get_vtbl_decl_for_binfo		(tree);
extern void debug_class				(tree);
extern void debug_thunks			(tree);
extern tree cp_fold_obj_type_ref		(tree, tree);
extern void set_linkage_according_to_type	(tree, tree);
extern void determine_key_method		(tree);
extern void check_for_override			(tree, tree);
extern void push_class_stack			(void);
extern void pop_class_stack			(void);

/* in cvt.c */
extern tree convert_to_reference		(tree, tree, int, int, tree);
extern tree convert_from_reference		(tree);
extern tree force_rvalue			(tree);
extern tree ocp_convert				(tree, tree, int, int);
extern tree cp_convert				(tree, tree);
extern tree convert_to_void	(tree, const char */*implicit context*/);
extern tree convert_force			(tree, tree, int);
extern tree build_expr_type_conversion		(int, tree, bool);
extern tree type_promotes_to			(tree);
extern tree perform_qualification_conversions	(tree, tree);
extern void clone_function_decl			(tree, int);
extern void adjust_clone_args			(tree);

/* decl.c */
extern tree poplevel				(int, int, int);
extern void insert_block			(tree);
extern tree pushdecl				(tree);
extern tree pushdecl_maybe_friend		(tree, bool);
extern void cxx_init_decl_processing		(void);
enum cp_tree_node_structure_enum cp_tree_node_structure
						(union lang_tree_node *);
extern bool cxx_mark_addressable		(tree);
extern void cxx_push_function_context		(struct function *);
extern void cxx_pop_function_context		(struct function *);
extern void maybe_push_cleanup_level		(tree);
extern void finish_scope			(void);
extern void push_switch				(tree);
extern void pop_switch				(void);
extern tree pushtag				(tree, tree, tag_scope);
extern tree make_anon_name			(void);
extern int decls_match				(tree, tree);
extern tree duplicate_decls			(tree, tree, bool);
extern tree pushdecl_top_level_maybe_friend	(tree, bool);
extern tree pushdecl_top_level_and_finish	(tree, tree);
extern tree declare_local_label			(tree);
extern tree define_label			(location_t, tree);
extern void check_goto				(tree);
extern bool check_omp_return			(void);
extern tree make_typename_type			(tree, tree, enum tag_types, tsubst_flags_t);
extern tree make_unbound_class_template		(tree, tree, tree, tsubst_flags_t);
extern tree check_for_out_of_scope_variable	(tree);
extern tree build_library_fn			(tree, tree);
extern tree build_library_fn_ptr		(const char *, tree);
extern tree build_cp_library_fn_ptr		(const char *, tree);
extern tree push_library_fn			(tree, tree);
extern tree push_void_library_fn		(tree, tree);
extern tree push_throw_library_fn		(tree, tree);
extern tree check_tag_decl			(cp_decl_specifier_seq *);
extern tree shadow_tag				(cp_decl_specifier_seq *);
extern tree groktypename			(cp_decl_specifier_seq *, const cp_declarator *);
/* APPLE LOCAL 6339747 */
extern tree grokblockdecl			(cp_decl_specifier_seq *, const cp_declarator *);
extern tree start_decl				(const cp_declarator *, cp_decl_specifier_seq *, int, tree, tree, tree *);
extern void start_decl_1			(tree, bool);
extern void cp_finish_decl			(tree, tree, bool, tree, int);
extern void finish_decl				(tree, tree, tree);
extern int cp_complete_array_type		(tree *, tree, bool);
extern tree build_ptrmemfunc_type		(tree);
extern tree build_ptrmem_type			(tree, tree);
/* the grokdeclarator prototype is in decl.h */
extern tree build_this_parm			(tree, cp_cv_quals);
extern int copy_fn_p				(tree);
extern tree get_scope_of_declarator		(const cp_declarator *);
extern void grok_special_member_properties	(tree);
extern int grok_ctor_properties			(tree, tree);
extern bool grok_op_properties			(tree, bool);
extern tree xref_tag				(enum tag_types, tree, tag_scope, bool);
extern tree xref_tag_from_type			(tree, tree, tag_scope);
extern bool xref_basetypes			(tree, tree);
extern tree start_enum				(tree);
extern void finish_enum				(tree);
extern void build_enumerator			(tree, tree, tree);
extern void start_preparsed_function		(tree, tree, int);
extern int start_function			(cp_decl_specifier_seq *, const cp_declarator *, tree);
extern tree begin_function_body			(void);
extern void finish_function_body		(tree);
extern tree finish_function			(int);
extern tree start_method			(cp_decl_specifier_seq *, const cp_declarator *, tree);
extern tree finish_method			(tree);
extern void maybe_register_incomplete_var	(tree);
extern void complete_vars			(tree);
extern void finish_stmt				(void);
extern void print_other_binding_stack		(struct cp_binding_level *);
extern void revert_static_member_fn		(tree);
extern void fixup_anonymous_aggr		(tree);
extern int check_static_variable_definition	(tree, tree);
extern tree compute_array_index_type		(tree, tree);
extern tree check_default_argument		(tree, tree);
typedef int (*walk_namespaces_fn)		(tree, void *);
extern int walk_namespaces			(walk_namespaces_fn,
						 void *);
extern int wrapup_globals_for_namespace		(tree, void *);
extern tree create_implicit_typedef		(tree, tree);
extern tree maybe_push_decl			(tree);
extern tree force_target_expr			(tree, tree);
extern tree build_target_expr_with_type		(tree, tree);
extern int local_variable_p			(tree);
extern int nonstatic_local_decl_p		(tree);
extern tree register_dtor_fn			(tree);
extern tmpl_spec_kind current_tmpl_spec_kind	(int);
extern tree cp_fname_init			(const char *, tree *);
extern tree builtin_function			(const char *name, tree type,
						 int code,
						 enum built_in_class cl,
						 const char *libname,
						 tree attrs);
extern tree check_elaborated_type_specifier	(enum tag_types, tree, bool);
extern void warn_extern_redeclared_static	(tree, tree);
extern const char *cxx_comdat_group		(tree);
extern bool cp_missing_noreturn_ok_p		(tree);
extern void initialize_artificial_var		(tree, tree);
extern tree check_var_type			(tree, tree);
extern tree reshape_init (tree, tree);

/* in decl2.c */
extern bool check_java_method			(tree);
extern tree build_memfn_type			(tree, tree, cp_cv_quals);
extern void maybe_retrofit_in_chrg		(tree);
extern void maybe_make_one_only			(tree);
extern void grokclassfn				(tree, tree,
						 enum overload_flags);
extern tree grok_array_decl			(tree, tree);
extern tree delete_sanity			(tree, tree, bool, int);
extern tree check_classfn			(tree, tree, tree);
extern void check_member_template		(tree);
extern tree grokfield (const cp_declarator *, cp_decl_specifier_seq *,
		       tree, bool, tree, tree);
extern tree grokbitfield (const cp_declarator *, cp_decl_specifier_seq *,
			  tree);
extern void cplus_decl_attributes		(tree *, tree, int);
extern void finish_anon_union			(tree);
extern void cp_finish_file			(void);
extern tree coerce_new_type			(tree);
extern tree coerce_delete_type			(tree);
extern void comdat_linkage			(tree);
extern void determine_visibility		(tree);
extern void constrain_class_visibility		(tree);
extern void update_member_visibility		(tree);
extern void import_export_decl			(tree);
extern tree build_cleanup			(tree);
extern tree build_offset_ref_call_from_tree	(tree, tree);
extern void check_default_args			(tree);
extern void mark_used				(tree);
extern void finish_static_data_member_decl	(tree, tree, bool, tree, int);
extern tree cp_build_parm_decl			(tree, tree);
extern tree get_guard				(tree);
extern tree get_guard_cond			(tree);
extern tree set_guard				(tree);
extern tree cxx_callgraph_analyze_expr		(tree *, int *, tree);
extern void mark_needed				(tree);
extern bool decl_needed_p			(tree);
extern void note_vague_linkage_fn		(tree);
extern tree build_artificial_parm		(tree, tree);

/* in error.c */
extern void init_error				(void);
extern const char *type_as_string		(tree, int);
extern const char *decl_as_string		(tree, int);
extern const char *expr_as_string		(tree, int);
extern const char *lang_decl_name		(tree, int);
extern const char *language_to_string		(enum languages);
extern const char *class_key_or_enum_as_string	(tree);
extern void print_instantiation_context		(void);

/* in except.c */
extern void init_exception_processing		(void);
extern tree expand_start_catch_block		(tree);
extern void expand_end_catch_block		(void);
extern tree build_exc_ptr			(void);
extern tree build_throw				(tree);
extern int nothrow_libfn_p			(tree);
extern void check_handlers			(tree);
extern void choose_personality_routine		(enum languages);
extern tree eh_type_info			(tree);

/* in expr.c */
extern rtx cxx_expand_expr			(tree, rtx,
						 enum machine_mode,
						 int, rtx *);
extern tree cplus_expand_constant		(tree);

/* friend.c */
extern int is_friend				(tree, tree);
extern void make_friend_class			(tree, tree, bool);
extern void add_friend				(tree, tree, bool);
extern tree do_friend				(tree, tree, tree, tree, enum overload_flags, bool);

/* in init.c */
extern tree expand_member_init			(tree);
extern void emit_mem_initializers		(tree);
extern tree build_aggr_init			(tree, tree, int);
extern int is_aggr_type				(tree, int);
extern tree get_type_value			(tree);
extern tree build_zero_init			(tree, tree, bool);
extern tree build_offset_ref			(tree, tree, bool);
extern tree build_new				(tree, tree, tree, tree, int);
extern tree build_vec_init			(tree, tree, tree, bool, int);
extern tree build_delete			(tree, tree,
						 special_function_kind,
						 int, int);
extern void push_base_cleanups			(void);
extern tree build_vec_delete			(tree, tree,
						 special_function_kind, int);
extern tree create_temporary_var		(tree);
extern void initialize_vtbl_ptrs		(tree);
extern tree build_java_class_ref		(tree);
extern tree integral_constant_value		(tree);

/* in lex.c */
extern void cxx_dup_lang_specific_decl		(tree);
extern void yyungetc				(int, int);

extern tree unqualified_name_lookup_error	(tree);
extern tree unqualified_fn_lookup_error		(tree);
extern tree build_lang_decl			(enum tree_code, tree, tree);
extern void retrofit_lang_decl			(tree);
extern tree copy_decl				(tree);
extern tree copy_type				(tree);
extern tree cxx_make_type			(enum tree_code);
extern tree make_aggr_type			(enum tree_code);
extern void yyerror				(const char *);
extern void yyhook				(int);
extern bool cxx_init				(void);
extern void cxx_finish				(void);
extern bool in_main_input_context		(void);

/* in method.c */
extern void init_method				(void);
extern tree make_thunk				(tree, bool, tree, tree);
extern void finish_thunk			(tree);
extern void use_thunk				(tree, bool);
extern void synthesize_method			(tree);
extern tree lazily_declare_fn			(special_function_kind,
						 tree);
extern tree skip_artificial_parms_for		(tree, tree);
extern tree make_alias_for			(tree, tree);

/* In optimize.c */
extern bool maybe_clone_body			(tree);

/* in pt.c */
extern void check_template_shadow		(tree);
extern tree get_innermost_template_args		(tree, int);
extern void maybe_begin_member_template_processing (tree);
extern void maybe_end_member_template_processing (void);
extern tree finish_member_template_decl		(tree);
extern void begin_template_parm_list		(void);
extern bool begin_specialization		(void);
extern void reset_specialization		(void);
extern void end_specialization			(void);
extern void begin_explicit_instantiation	(void);
extern void end_explicit_instantiation		(void);
extern tree check_explicit_specialization	(tree, tree, int, int);
extern tree process_template_parm		(tree, tree, bool);
extern tree end_template_parm_list		(tree);
extern void end_template_decl			(void);
extern tree push_template_decl			(tree);
extern tree push_template_decl_real		(tree, bool);
extern bool redeclare_class_template		(tree, tree);
extern tree lookup_template_class		(tree, tree, tree, tree,
						 int, tsubst_flags_t);
extern tree lookup_template_function		(tree, tree);
extern int uses_template_parms			(tree);
extern int uses_template_parms_level		(tree, int);
extern tree instantiate_class_template		(tree);
extern tree instantiate_template		(tree, tree, tsubst_flags_t);
extern int fn_type_unification			(tree, tree, tree, tree,
						 tree, unification_kind_t, int);
extern void mark_decl_instantiated		(tree, int);
extern int more_specialized_fn			(tree, tree, int);
extern void do_decl_instantiation		(tree, tree);
extern void do_type_instantiation		(tree, tree, tsubst_flags_t);
extern tree instantiate_decl			(tree, int, bool);
extern int comp_template_parms			(tree, tree);
extern int template_class_depth			(tree);
extern int is_specialization_of			(tree, tree);
extern bool is_specialization_of_friend		(tree, tree);
extern int comp_template_args			(tree, tree);
extern tree maybe_process_partial_specialization (tree);
extern tree most_specialized_instantiation	(tree);
extern void print_candidates			(tree);
extern void instantiate_pending_templates	(int);
extern tree tsubst_default_argument		(tree, tree, tree);
extern tree tsubst_copy_and_build		(tree, tree, tsubst_flags_t,
						 tree, bool, bool);
extern tree most_general_template		(tree);
extern tree get_mostly_instantiated_function_type (tree);
extern int problematic_instantiation_changed	(void);
extern void record_last_problematic_instantiation (void);
extern tree current_instantiation		(void);
extern tree maybe_get_template_decl_from_type_decl (tree);
extern int processing_template_parmlist;
extern bool dependent_type_p			(tree);
extern bool any_dependent_template_arguments_p  (tree);
extern bool dependent_template_p		(tree);
extern bool dependent_template_id_p		(tree, tree);
extern bool type_dependent_expression_p		(tree);
extern bool any_type_dependent_arguments_p      (tree);
extern bool value_dependent_expression_p	(tree);
extern bool any_value_dependent_elements_p      (tree);
extern tree resolve_typename_type		(tree, bool);
extern tree template_for_substitution		(tree);
extern tree build_non_dependent_expr		(tree);
extern tree build_non_dependent_args		(tree);
extern bool reregister_specialization		(tree, tree, tree);
extern tree fold_non_dependent_expr		(tree);
extern bool explicit_class_specialization_p     (tree);
extern tree outermost_tinst_level		(void);

/* in repo.c */
extern void init_repo				(void);
extern int repo_emit_p				(tree);
extern bool repo_export_class_p			(tree);
extern void finish_repo				(void);

/* in rtti.c */
/* A vector of all tinfo decls that haven't been emitted yet.  */
extern GTY(()) VEC(tree,gc) *unemitted_tinfo_decls;

extern void init_rtti_processing		(void);
extern tree build_typeid			(tree);
extern tree get_tinfo_decl			(tree);
extern tree get_typeid				(tree);
extern tree build_dynamic_cast			(tree, tree);
extern void emit_support_tinfos			(void);
extern bool emit_tinfo_decl			(tree);

/* in search.c */
extern bool accessible_base_p			(tree, tree, bool);
extern tree lookup_base				(tree, tree, base_access,
						 base_kind *);
extern tree dcast_base_hint			(tree, tree);
extern int accessible_p				(tree, tree, bool);
extern tree lookup_field_1			(tree, tree, bool);
extern tree lookup_field			(tree, tree, int, bool);
extern int lookup_fnfields_1			(tree, tree);
extern int class_method_index_for_fn		(tree, tree);
extern tree lookup_fnfields			(tree, tree, int);
extern tree lookup_member			(tree, tree, int, bool);
extern int look_for_overrides			(tree, tree);
extern void get_pure_virtuals			(tree);
extern void maybe_suppress_debug_info		(tree);
extern void note_debug_info_needed		(tree);
extern void print_search_statistics		(void);
extern void reinit_search_statistics		(void);
extern tree current_scope			(void);
extern int at_function_scope_p			(void);
extern bool at_class_scope_p			(void);
extern bool at_namespace_scope_p		(void);
extern tree context_for_name_lookup		(tree);
extern tree lookup_conversions			(tree);
extern tree binfo_from_vbase			(tree);
extern tree binfo_for_vbase			(tree, tree);
extern tree look_for_overrides_here		(tree, tree);
#define dfs_skip_bases ((tree)1)
extern tree dfs_walk_all (tree, tree (*) (tree, void *),
			  tree (*) (tree, void *), void *);
extern tree dfs_walk_once (tree, tree (*) (tree, void *),
			   tree (*) (tree, void *), void *);
extern tree binfo_via_virtual			(tree, tree);
extern tree build_baselink			(tree, tree, tree, tree);
extern tree adjust_result_of_qualified_name_lookup
						(tree, tree, tree);
extern tree copied_binfo			(tree, tree);
extern tree original_binfo			(tree, tree);
extern int shared_member_p			(tree);


/* The representation of a deferred access check.  */

typedef struct deferred_access_check GTY(())
{
  /* The base class in which the declaration is referenced. */
  tree binfo;
  /* The declaration whose access must be checked.  */
  tree decl;
  /* The declaration that should be used in the error message.  */
  tree diag_decl;
} deferred_access_check;
DEF_VEC_O(deferred_access_check);
DEF_VEC_ALLOC_O(deferred_access_check,gc);

/* in semantics.c */
extern void push_deferring_access_checks	(deferring_kind);
extern void resume_deferring_access_checks	(void);
extern void stop_deferring_access_checks	(void);
extern void pop_deferring_access_checks		(void);
extern VEC (deferred_access_check,gc)* get_deferred_access_checks		(void);
extern void pop_to_parent_deferring_access_checks (void);
extern void perform_access_checks		(VEC (deferred_access_check,gc)*);
extern void perform_deferred_access_checks	(void);
extern void perform_or_defer_access_check	(tree, tree, tree);
extern int stmts_are_full_exprs_p		(void);
extern void init_cp_semantics			(void);
extern tree do_poplevel				(tree);
extern void add_decl_expr			(tree);
extern tree finish_expr_stmt			(tree);
extern tree begin_if_stmt			(void);
extern void finish_if_stmt_cond			(tree, tree);
extern tree finish_then_clause			(tree);
extern void begin_else_clause			(tree);
extern void finish_else_clause			(tree);
extern void finish_if_stmt			(tree);
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
extern tree begin_while_stmt			(tree);
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
extern void finish_while_stmt_cond		(tree, tree);
extern void finish_while_stmt			(tree);
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
extern tree begin_do_stmt			(tree);
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
extern void finish_do_body			(tree);
extern void finish_do_stmt			(tree, tree);
extern tree finish_return_stmt			(tree);
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
extern tree begin_for_stmt			(tree);
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
extern void finish_for_init_stmt		(tree);
extern void finish_for_cond			(tree, tree);
extern void finish_for_expr			(tree, tree);
extern void finish_for_stmt			(tree);
extern tree finish_break_stmt			(void);
extern tree finish_continue_stmt		(void);
extern tree begin_switch_stmt			(void);
extern void finish_switch_cond			(tree, tree);
extern void finish_switch_stmt			(tree);
extern tree finish_case_label			(tree, tree);
extern tree finish_goto_stmt			(tree);
extern tree begin_try_block			(void);
extern void finish_try_block			(tree);
extern tree begin_eh_spec_block			(void);
extern void finish_eh_spec_block		(tree, tree);
extern void finish_handler_sequence		(tree);
extern tree begin_function_try_block		(tree *);
extern void finish_function_try_block		(tree);
extern void finish_function_handler_sequence    (tree, tree);
extern void finish_cleanup_try_block		(tree);
extern tree begin_handler			(void);
extern void finish_handler_parms		(tree, tree);
extern void finish_handler			(tree);
extern void finish_cleanup			(tree, tree);

enum {
  BCS_NO_SCOPE = 1,
  BCS_TRY_BLOCK = 2,
  BCS_FN_BODY = 4
};
extern tree begin_compound_stmt			(unsigned int);

extern void finish_compound_stmt		(tree);
extern tree finish_asm_stmt			(int, tree, tree, tree, tree);
extern tree finish_label_stmt			(tree);
extern void finish_label_decl			(tree);
extern tree finish_parenthesized_expr		(tree);
extern tree finish_non_static_data_member       (tree, tree, tree);
extern tree begin_stmt_expr			(void);
extern tree finish_stmt_expr_expr		(tree, tree);
extern tree finish_stmt_expr			(tree, bool);
extern tree perform_koenig_lookup		(tree, tree);
extern tree finish_call_expr			(tree, tree, bool, bool);
extern tree finish_increment_expr		(tree, enum tree_code);
extern tree finish_this_expr			(void);
extern tree finish_pseudo_destructor_expr       (tree, tree, tree);
extern tree finish_unary_op_expr		(enum tree_code, tree);
extern tree finish_compound_literal		(tree, VEC(constructor_elt,gc) *);
extern tree finish_fname			(tree);
extern void finish_translation_unit		(void);
extern tree finish_template_type_parm		(tree, tree);
extern tree finish_template_template_parm       (tree, tree);
extern tree begin_class_definition		(tree, tree);
extern void finish_template_decl		(tree);
extern tree finish_template_type		(tree, tree, int);
extern tree finish_base_specifier		(tree, tree, bool);
extern void finish_member_declaration		(tree);
extern void qualified_name_lookup_error		(tree, tree, tree);
extern void check_template_keyword		(tree);
extern tree finish_id_expression		(tree, tree, tree,
						 cp_id_kind *,
						 bool, bool, bool *,
						 bool, bool, bool, bool,
						 const char **);
extern tree finish_typeof			(tree);
extern tree finish_offsetof			(tree);
extern void finish_decl_cleanup			(tree, tree);
extern void finish_eh_cleanup			(tree);
extern void expand_body				(tree);
extern void finish_mem_initializers		(tree);
extern tree check_template_template_default_arg (tree);
extern void expand_or_defer_fn			(tree);
extern void check_accessibility_of_qualified_id (tree, tree, tree);
extern tree finish_qualified_id_expr		(tree, tree, bool, bool,
						 bool, bool);
extern void simplify_aggr_init_expr		(tree *);
extern void finalize_nrv			(tree *, tree, tree);
extern void note_decl_for_pch			(tree);
extern tree finish_omp_clauses			(tree);
extern void finish_omp_threadprivate		(tree);
extern tree begin_omp_structured_block		(void);
extern tree finish_omp_structured_block		(tree);
extern tree begin_omp_parallel			(void);
extern tree finish_omp_parallel			(tree, tree);
extern tree finish_omp_for			(location_t, tree, tree,
						 tree, tree, tree, tree);
extern void finish_omp_atomic			(enum tree_code, tree, tree);
extern void finish_omp_barrier			(void);
extern void finish_omp_flush			(void);
extern enum omp_clause_default_kind cxx_omp_predetermined_sharing (tree);
extern tree cxx_omp_clause_default_ctor		(tree, tree);
extern tree cxx_omp_clause_copy_ctor		(tree, tree, tree);
extern tree cxx_omp_clause_assign_op		(tree, tree, tree);
extern tree cxx_omp_clause_dtor			(tree, tree);
extern bool cxx_omp_privatize_by_reference	(tree);
extern tree baselink_for_fns                    (tree);

/* in tree.c */
extern void lang_check_failed			(const char *, int,
						 const char *) ATTRIBUTE_NORETURN;
extern tree stabilize_expr			(tree, tree *);
extern void stabilize_call			(tree, tree *);
extern bool stabilize_init			(tree, tree *);
extern tree add_stmt_to_compound		(tree, tree);
extern tree cxx_maybe_build_cleanup		(tree);
extern void init_tree				(void);
extern int pod_type_p				(tree);
extern bool class_tmpl_impl_spec_p		(tree);
extern int zero_init_p				(tree);
extern tree canonical_type_variant		(tree);
extern tree copy_binfo				(tree, tree, tree,
						 tree *, int);
extern int member_p				(tree);
extern cp_lvalue_kind real_lvalue_p		(tree);
extern bool builtin_valid_in_constant_expr_p    (tree);
extern tree build_min				(enum tree_code, tree, ...);
extern tree build_min_nt			(enum tree_code, ...);
extern tree build_min_non_dep			(enum tree_code, tree, ...);
extern tree build_cplus_new			(tree, tree);
extern tree get_target_expr			(tree);
extern tree build_cplus_array_type		(tree, tree);
extern tree hash_tree_cons			(tree, tree, tree);
extern tree hash_tree_chain			(tree, tree);
extern tree build_qualified_name		(tree, tree, tree, bool);
extern int is_overloaded_fn			(tree);
extern tree get_first_fn			(tree);
extern tree ovl_cons				(tree, tree);
extern tree build_overload			(tree, tree);
extern const char *cxx_printable_name		(tree, int);
extern tree build_exception_variant		(tree, tree);
extern tree bind_template_template_parm		(tree, tree);
extern tree array_type_nelts_total		(tree);
extern tree array_type_nelts_top		(tree);
extern tree break_out_target_exprs		(tree);
extern tree get_type_decl			(tree);
extern tree decl_namespace_context		(tree);
extern bool decl_anon_ns_mem_p			(tree);
extern tree lvalue_type				(tree);
extern tree error_type				(tree);
extern int varargs_function_p			(tree);
extern bool really_overloaded_fn		(tree);
extern bool cp_tree_equal			(tree, tree);
extern tree no_linkage_check			(tree, bool);
extern void debug_binfo				(tree);
extern tree build_dummy_object			(tree);
extern tree maybe_dummy_object			(tree, tree *);
extern int is_dummy_object			(tree);
extern const struct attribute_spec cxx_attribute_table[];
extern tree make_ptrmem_cst			(tree, tree);
extern tree cp_build_type_attribute_variant     (tree, tree);
extern tree cp_build_qualified_type_real	(tree, int, tsubst_flags_t);
#define cp_build_qualified_type(TYPE, QUALS) \
  cp_build_qualified_type_real ((TYPE), (QUALS), tf_warning_or_error)
extern special_function_kind special_function_p (tree);
extern int count_trees				(tree);
extern int char_type_p				(tree);
extern void verify_stmt_tree			(tree);
extern linkage_kind decl_linkage		(tree);
extern tree cp_walk_subtrees (tree*, int*, walk_tree_fn,
			      void*, struct pointer_set_t*);
extern int cp_cannot_inline_tree_fn		(tree*);
extern tree cp_add_pending_fn_decls		(void*,tree);
extern int cp_auto_var_in_fn_p			(tree,tree);
extern tree fold_if_not_in_template		(tree);
extern tree rvalue				(tree);
extern tree convert_bitfield_to_declared_type   (tree);
extern tree cp_save_expr			(tree);
extern bool cast_valid_in_integral_constant_expression_p (tree);

/* in typeck.c */
extern int string_conv_p			(tree, tree, int);
extern tree cp_truthvalue_conversion		(tree);
extern tree condition_conversion		(tree);
extern tree require_complete_type		(tree);
extern tree complete_type			(tree);
extern tree complete_type_or_else		(tree, tree);
extern int type_unknown_p			(tree);
extern bool comp_except_specs			(tree, tree, bool);
extern bool comptypes				(tree, tree, int);
extern bool compparms				(tree, tree);
extern int comp_cv_qualification		(tree, tree);
extern int comp_cv_qual_signature		(tree, tree);
extern tree cxx_sizeof_or_alignof_expr		(tree, enum tree_code);
extern tree cxx_sizeof_or_alignof_type		(tree, enum tree_code, bool);
#define cxx_sizeof_nowarn(T) cxx_sizeof_or_alignof_type (T, SIZEOF_EXPR, false)
extern tree inline_conversion			(tree);
extern tree is_bitfield_expr_with_lowered_type  (tree);
extern tree unlowered_expr_type                 (tree);
extern tree decay_conversion			(tree);
extern tree build_class_member_access_expr      (tree, tree, tree, bool);
extern tree finish_class_member_access_expr     (tree, tree, bool);
extern tree build_x_indirect_ref		(tree, const char *);
extern tree build_indirect_ref			(tree, const char *);
extern tree build_array_ref			(tree, tree);
extern tree get_member_function_from_ptrfunc	(tree *, tree);
extern tree build_x_binary_op			(enum tree_code, tree,
						 enum tree_code, tree,
						 enum tree_code, bool *);
extern tree build_x_unary_op			(enum tree_code, tree);
extern tree unary_complex_lvalue		(enum tree_code, tree);
extern tree build_x_conditional_expr		(tree, tree, tree);
extern tree build_x_compound_expr_from_list	(tree, const char *);
extern tree build_x_compound_expr		(tree, tree);
extern tree build_compound_expr			(tree, tree);
extern tree build_static_cast			(tree, tree);
extern tree build_reinterpret_cast		(tree, tree);
extern tree build_const_cast			(tree, tree);
extern tree build_c_cast			(tree, tree);
extern tree build_x_modify_expr			(tree, enum tree_code, tree);
extern tree build_modify_expr			(tree, enum tree_code, tree);
extern tree convert_for_initialization		(tree, tree, tree, int,
						 const char *, tree, int);
extern int comp_ptr_ttypes			(tree, tree);
extern bool comp_ptr_ttypes_const		(tree, tree);
extern int ptr_reasonably_similar		(tree, tree);
extern tree build_ptrmemfunc			(tree, tree, int, bool);
extern int cp_type_quals			(tree);
extern bool cp_type_readonly			(tree);
extern bool cp_has_mutable_p			(tree);
extern bool at_least_as_qualified_p		(tree, tree);
extern void cp_apply_type_quals_to_decl		(int, tree);
extern tree build_ptrmemfunc1			(tree, tree, tree);
extern void expand_ptrmemfunc_cst		(tree, tree *, tree *);
extern tree type_after_usual_arithmetic_conversions (tree, tree);
extern tree composite_pointer_type		(tree, tree, tree, tree,
						 const char*);
extern tree merge_types				(tree, tree);
extern tree check_return_expr			(tree, bool *);
#define cp_build_binary_op(code, arg1, arg2) \
  build_binary_op(code, arg1, arg2, 1)
#define cxx_sizeof(T)  cxx_sizeof_or_alignof_type (T, SIZEOF_EXPR, true)
extern tree build_ptrmemfunc_access_expr	(tree, tree);
extern tree build_address			(tree);
extern tree build_nop				(tree, tree);
extern tree non_reference			(tree);
extern tree lookup_anon_field			(tree, tree);
extern bool invalid_nonstatic_memfn_p		(tree);
extern tree convert_member_func_to_ptr		(tree, tree);
extern tree convert_ptrmem			(tree, tree, bool, bool);
extern int lvalue_or_else			(tree, enum lvalue_use);
extern int lvalue_p				(tree);

/* in typeck2.c */
extern void require_complete_eh_spec_types	(tree, tree);
extern void cxx_incomplete_type_diagnostic	(tree, tree, int);
#undef cxx_incomplete_type_error
extern void cxx_incomplete_type_error		(tree, tree);
#define cxx_incomplete_type_error(V,T) \
  (cxx_incomplete_type_diagnostic ((V), (T), 0))
extern tree error_not_base_type			(tree, tree);
extern tree binfo_or_else			(tree, tree);
extern void readonly_error			(tree, const char *, int);
extern void complete_type_check_abstract	(tree);
extern int abstract_virtuals_error		(tree, tree);

extern tree store_init_value			(tree, tree);
extern tree digest_init				(tree, tree);
extern tree build_scoped_ref			(tree, tree, tree *);
extern tree build_x_arrow			(tree);
extern tree build_m_component_ref		(tree, tree);
extern tree build_functional_cast		(tree, tree);
extern tree add_exception_specifier		(tree, tree, int);
extern tree merge_exception_specifiers		(tree, tree);

/* in mangle.c */
extern void init_mangle				(void);
extern void mangle_decl				(tree);
extern const char *mangle_type_string		(tree);
extern tree mangle_typeinfo_for_type		(tree);
extern tree mangle_typeinfo_string_for_type	(tree);
extern tree mangle_vtbl_for_type		(tree);
extern tree mangle_vtt_for_type			(tree);
extern tree mangle_ctor_vtbl_for_type		(tree, tree);
extern tree mangle_thunk			(tree, int, tree, tree);
extern tree mangle_conv_op_name_for_type	(tree);
extern tree mangle_guard_variable		(tree);
extern tree mangle_ref_init_variable		(tree);

/* in dump.c */
extern bool cp_dump_tree			(void *, tree);

/* In cp/cp-objcp-common.c.  */

extern HOST_WIDE_INT cxx_get_alias_set		(tree);
extern bool cxx_warn_unused_global_decl		(tree);
extern tree cp_expr_size			(tree);
extern size_t cp_tree_size			(enum tree_code);
extern bool cp_var_mod_type_p			(tree, tree);
extern void cxx_initialize_diagnostics		(struct diagnostic_context *);
extern int cxx_types_compatible_p		(tree, tree);
extern void init_shadowed_var_for_decl		(void);
extern tree cxx_staticp                         (tree);

/* in cp-gimplify.c */
extern int cp_gimplify_expr			(tree *, tree *, tree *);
extern void cp_genericize			(tree);

/* -- end of C++ */

/* In order for the format checking to accept the C++ frontend
   diagnostic framework extensions, you must include this file before
   toplev.h, not after.  We override the definition of GCC_DIAG_STYLE
   in c-common.h.  */
#undef GCC_DIAG_STYLE
#define GCC_DIAG_STYLE __gcc_cxxdiag__
#if GCC_VERSION >= 4001
#define ATTRIBUTE_GCC_CXXDIAG(m, n) __attribute__ ((__format__ (GCC_DIAG_STYLE, m, n))) ATTRIBUTE_NONNULL(m)
#else
#define ATTRIBUTE_GCC_CXXDIAG(m, n) ATTRIBUTE_NONNULL(m)
#endif
extern void cp_cpp_error			(cpp_reader *, int,
						 const char *, va_list *)
     ATTRIBUTE_GCC_CXXDIAG(3,0);
/* APPLE LOCAL radar 5741070  */
extern tree c_return_interface_record_type (tree);

/* APPLE LOCAL begin blocks 6040305 (cg) */
extern cp_declarator* make_block_pointer_declarator (tree, cp_cv_quals,
											 cp_declarator *);
/* APPLE LOCAL end blocks 6040305 (cg) */
#endif /* ! GCC_CP_TREE_H */
