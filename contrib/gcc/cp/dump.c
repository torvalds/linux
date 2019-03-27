/* Tree-dumping functionality for intermediate representation.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Written by Mark Mitchell <mark@codesourcery.com>

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
#include "tree-dump.h"

static void dump_access (dump_info_p, tree);

static void dump_op (dump_info_p, tree);

/* Dump a representation of the accessibility information associated
   with T.  */

static void
dump_access (dump_info_p di, tree t)
{
  if (TREE_PROTECTED(t))
    dump_string_field (di, "accs", "prot");
  else if (TREE_PRIVATE(t))
    dump_string_field (di, "accs", "priv");
  else
    dump_string_field (di, "accs", "pub");
}

/* Dump a representation of the specific operator for an overloaded
   operator associated with node t.  */

static void
dump_op (dump_info_p di, tree t)
{
  switch (DECL_OVERLOADED_OPERATOR_P (t)) {
    case NEW_EXPR:
      dump_string (di, "new");
      break;
    case VEC_NEW_EXPR:
      dump_string (di, "vecnew");
      break;
    case DELETE_EXPR:
      dump_string (di, "delete");
      break;
    case VEC_DELETE_EXPR:
      dump_string (di, "vecdelete");
      break;
    case UNARY_PLUS_EXPR:
      dump_string (di, "pos");
      break;
    case NEGATE_EXPR:
      dump_string (di, "neg");
      break;
    case ADDR_EXPR:
      dump_string (di, "addr");
      break;
    case INDIRECT_REF:
      dump_string(di, "deref");
      break;
    case BIT_NOT_EXPR:
      dump_string(di, "not");
      break;
    case TRUTH_NOT_EXPR:
      dump_string(di, "lnot");
      break;
    case PREINCREMENT_EXPR:
      dump_string(di, "preinc");
      break;
    case PREDECREMENT_EXPR:
      dump_string(di, "predec");
      break;
    case PLUS_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "plusassign");
      else
	dump_string(di, "plus");
      break;
    case MINUS_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "minusassign");
      else
	dump_string(di, "minus");
      break;
    case MULT_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "multassign");
      else
	dump_string (di, "mult");
      break;
    case TRUNC_DIV_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "divassign");
      else
	dump_string (di, "div");
      break;
    case TRUNC_MOD_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	 dump_string (di, "modassign");
      else
	dump_string (di, "mod");
      break;
    case BIT_AND_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "andassign");
      else
	dump_string (di, "and");
      break;
    case BIT_IOR_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "orassign");
      else
	dump_string (di, "or");
      break;
    case BIT_XOR_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "xorassign");
      else
	dump_string (di, "xor");
      break;
    case LSHIFT_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "lshiftassign");
      else
	dump_string (di, "lshift");
      break;
    case RSHIFT_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "rshiftassign");
      else
	dump_string (di, "rshift");
      break;
    case EQ_EXPR:
      dump_string (di, "eq");
      break;
    case NE_EXPR:
      dump_string (di, "ne");
      break;
    case LT_EXPR:
      dump_string (di, "lt");
      break;
    case GT_EXPR:
      dump_string (di, "gt");
      break;
    case LE_EXPR:
      dump_string (di, "le");
      break;
    case GE_EXPR:
      dump_string (di, "ge");
      break;
    case TRUTH_ANDIF_EXPR:
      dump_string (di, "land");
      break;
    case TRUTH_ORIF_EXPR:
      dump_string (di, "lor");
      break;
    case COMPOUND_EXPR:
      dump_string (di, "compound");
      break;
    case MEMBER_REF:
      dump_string (di, "memref");
      break;
    case COMPONENT_REF:
      dump_string (di, "ref");
      break;
    case ARRAY_REF:
      dump_string (di, "subs");
      break;
    case POSTINCREMENT_EXPR:
      dump_string (di, "postinc");
      break;
    case POSTDECREMENT_EXPR:
      dump_string (di, "postdec");
      break;
    case CALL_EXPR:
      dump_string (di, "call");
      break;
    case NOP_EXPR:
      if (DECL_ASSIGNMENT_OPERATOR_P (t))
	dump_string (di, "assign");
      break;
    default:
      break;
  }
}

bool
cp_dump_tree (void* dump_info, tree t)
{
  enum tree_code code;
  dump_info_p di = (dump_info_p) dump_info;

  /* Figure out what kind of node this is.  */
  code = TREE_CODE (t);

  if (DECL_P (t))
    {
      if (DECL_LANG_SPECIFIC (t) && DECL_LANGUAGE (t) != lang_cplusplus)
	dump_string_field (di, "lang", language_to_string (DECL_LANGUAGE (t)));
    }

  switch (code)
    {
    case IDENTIFIER_NODE:
      if (IDENTIFIER_OPNAME_P (t))
	{
	  dump_string_field (di, "note", "operator");
	  return true;
	}
      else if (IDENTIFIER_TYPENAME_P (t))
	{
	  dump_child ("tynm", TREE_TYPE (t));
	  return true;
	}
      break;

    case OFFSET_TYPE:
      dump_string_field (di, "note", "ptrmem");
      dump_child ("ptd", TYPE_PTRMEM_POINTED_TO_TYPE (t));
      dump_child ("cls", TYPE_PTRMEM_CLASS_TYPE (t));
      return true;

    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (t))
	{
	  dump_string_field (di, "note", "ptrmem");
	  dump_child ("ptd", TYPE_PTRMEM_POINTED_TO_TYPE (t));
	  dump_child ("cls", TYPE_PTRMEM_CLASS_TYPE (t));
	  return true;
	}
      /* Fall through.  */

    case UNION_TYPE:
      /* Is it a type used as a base? */
      if (TYPE_CONTEXT (t) && TREE_CODE (TYPE_CONTEXT (t)) == TREE_CODE (t)
	  && CLASSTYPE_AS_BASE (TYPE_CONTEXT (t)) == t)
	{
	  dump_child ("bfld", TYPE_CONTEXT (t));
	  return true;
	}

      if (! IS_AGGR_TYPE (t))
	break;

      dump_child ("vfld", TYPE_VFIELD (t));
      if (CLASSTYPE_TEMPLATE_SPECIALIZATION(t))
	dump_string(di, "spec");

      if (!dump_flag (di, TDF_SLIM, t) && TYPE_BINFO (t))
	{
	  int i;
	  tree binfo;
	  tree base_binfo;

	  for (binfo = TYPE_BINFO (t), i = 0;
	       BINFO_BASE_ITERATE (binfo, i, base_binfo); ++i)
	    {
	      dump_child ("base", BINFO_TYPE (base_binfo));
	      if (BINFO_VIRTUAL_P (base_binfo))
		dump_string_field (di, "spec", "virt");
	      dump_access (di, base_binfo);
	    }
	}
      break;

    case FIELD_DECL:
      dump_access (di, t);
      if (DECL_MUTABLE_P (t))
	dump_string_field (di, "spec", "mutable");
      break;

    case VAR_DECL:
      if (TREE_CODE (CP_DECL_CONTEXT (t)) == RECORD_TYPE)
	dump_access (di, t);
      if (TREE_STATIC (t) && !TREE_PUBLIC (t))
	dump_string_field (di, "link", "static");
      break;

    case FUNCTION_DECL:
      if (!DECL_THUNK_P (t))
	{
	  if (DECL_OVERLOADED_OPERATOR_P (t)) {
	    dump_string_field (di, "note", "operator");
	    dump_op (di, t);
	  }
	  if (DECL_FUNCTION_MEMBER_P (t))
	    {
	      dump_string_field (di, "note", "member");
	      dump_access (di, t);
	    }
	  if (DECL_PURE_VIRTUAL_P (t))
	    dump_string_field (di, "spec", "pure");
	  if (DECL_VIRTUAL_P (t))
	    dump_string_field (di, "spec", "virt");
	  if (DECL_CONSTRUCTOR_P (t))
	    dump_string_field (di, "note", "constructor");
	  if (DECL_DESTRUCTOR_P (t))
	    dump_string_field (di, "note", "destructor");
	  if (DECL_CONV_FN_P (t))
	    dump_string_field (di, "note", "conversion");
	  if (DECL_GLOBAL_CTOR_P (t))
	    dump_string_field (di, "note", "global init");
	  if (DECL_GLOBAL_DTOR_P (t))
	    dump_string_field (di, "note", "global fini");
	  if (DECL_FRIEND_PSEUDO_TEMPLATE_INSTANTIATION (t))
	    dump_string_field (di, "note", "pseudo tmpl");
	}
      else
	{
	  tree virt = THUNK_VIRTUAL_OFFSET (t);

	  dump_string_field (di, "note", "thunk");
	  if (DECL_THIS_THUNK_P (t))
	    dump_string_field (di, "note", "this adjusting");
	  else
	    {
	      dump_string_field (di, "note", "result adjusting");
	      if (virt)
		virt = BINFO_VPTR_FIELD (virt);
	    }
	  dump_int (di, "fixd", THUNK_FIXED_OFFSET (t));
	  if (virt)
	    dump_int (di, "virt", tree_low_cst (virt, 0));
	  dump_child ("fn", DECL_INITIAL (t));
	}
      break;

    case NAMESPACE_DECL:
      if (DECL_NAMESPACE_ALIAS (t))
	dump_child ("alis", DECL_NAMESPACE_ALIAS (t));
      else if (!dump_flag (di, TDF_SLIM, t))
	dump_child ("dcls", cp_namespace_decls (t));
      break;

    case TEMPLATE_DECL:
      dump_child ("rslt", DECL_TEMPLATE_RESULT (t));
      dump_child ("inst", DECL_TEMPLATE_INSTANTIATIONS (t));
      dump_child ("spcs", DECL_TEMPLATE_SPECIALIZATIONS (t));
      dump_child ("prms", DECL_TEMPLATE_PARMS (t));
      break;

    case OVERLOAD:
      dump_child ("crnt", OVL_CURRENT (t));
      dump_child ("chan", OVL_CHAIN (t));
      break;

    case TRY_BLOCK:
      dump_stmt (di, t);
      if (CLEANUP_P (t))
	dump_string_field (di, "note", "cleanup");
      dump_child ("body", TRY_STMTS (t));
      dump_child ("hdlr", TRY_HANDLERS (t));
      break;

    case EH_SPEC_BLOCK:
      dump_stmt (di, t);
      dump_child ("body", EH_SPEC_STMTS (t));
      dump_child ("raises", EH_SPEC_RAISES (t));
      break;

    case PTRMEM_CST:
      dump_child ("clas", PTRMEM_CST_CLASS (t));
      dump_child ("mbr", PTRMEM_CST_MEMBER (t));
      break;

    case THROW_EXPR:
      /* These nodes are unary, but do not have code class `1'.  */
      dump_child ("op 0", TREE_OPERAND (t, 0));
      break;

    case AGGR_INIT_EXPR:
      dump_int (di, "ctor", AGGR_INIT_VIA_CTOR_P (t));
      dump_child ("fn", TREE_OPERAND (t, 0));
      dump_child ("args", TREE_OPERAND (t, 1));
      dump_child ("decl", TREE_OPERAND (t, 2));
      break;

    case HANDLER:
      dump_stmt (di, t);
      dump_child ("parm", HANDLER_PARMS (t));
      dump_child ("body", HANDLER_BODY (t));
      break;

    case MUST_NOT_THROW_EXPR:
      dump_stmt (di, t);
      dump_child ("body", TREE_OPERAND (t, 0));
      break;

    case USING_STMT:
      dump_stmt (di, t);
      dump_child ("nmsp", USING_STMT_NAMESPACE (t));
      break;

    case CLEANUP_STMT:
      dump_stmt (di, t);
      dump_child ("decl", CLEANUP_DECL (t));
      dump_child ("expr", CLEANUP_EXPR (t));
      dump_child ("body", CLEANUP_BODY (t));
      break;

    case IF_STMT:
      dump_stmt (di, t);
      dump_child ("cond", IF_COND (t));
      dump_child ("then", THEN_CLAUSE (t));
      dump_child ("else", ELSE_CLAUSE (t));
      break;

    case BREAK_STMT:
    case CONTINUE_STMT:
      dump_stmt (di, t);
      break;

    case DO_STMT:
      dump_stmt (di, t);
      dump_child ("body", DO_BODY (t));
      dump_child ("cond", DO_COND (t));
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
      dump_child ("attrs", DO_ATTRIBUTES (t));
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      break;

    case FOR_STMT:
      dump_stmt (di, t);
      dump_child ("init", FOR_INIT_STMT (t));
      dump_child ("cond", FOR_COND (t));
      dump_child ("expr", FOR_EXPR (t));
      dump_child ("body", FOR_BODY (t));
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
      dump_child ("attrs", FOR_ATTRIBUTES (t));
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      break;

    case SWITCH_STMT:
      dump_stmt (di, t);
      dump_child ("cond", SWITCH_STMT_COND (t));
      dump_child ("body", SWITCH_STMT_BODY (t));
      break;

    case WHILE_STMT:
      dump_stmt (di, t);
      dump_child ("cond", WHILE_COND (t));
      dump_child ("body", WHILE_BODY (t));
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
      dump_child ("attrs", WHILE_ATTRIBUTES (t));
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      break;

    case STMT_EXPR:
      dump_child ("stmt", STMT_EXPR_STMT (t));
      break;

    case EXPR_STMT:
      dump_stmt (di, t);
      dump_child ("expr", EXPR_STMT_EXPR (t));
      break;

    default:
      break;
    }

  return c_dump_tree (di, t);
}
