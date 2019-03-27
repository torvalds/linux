/* Process declarations and variables for C compiler.
   Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

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

/* $FreeBSD$ */
/* Merged C99 inline changes from gcc trunk 122565 2007-03-05 */
/* Fixed problems with compiling inline-25.c and inline-26.c */
/* XXX still fails inline-29.c, inline-31.c, and inline-32.c */

/* Process declarations and symbol lookup for C front end.
   Also constructs types; the standard scalar types at initialization,
   and structure, union, array and enum types when they are declared.  */

/* ??? not all decl nodes are given the most useful possible
   line numbers.  For example, the CONST_DECLs for enum values.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "input.h"
#include "tm.h"
#include "intl.h"
#include "tree.h"
#include "tree-inline.h"
#include "rtl.h"
#include "flags.h"
#include "function.h"
#include "output.h"
#include "expr.h"
#include "c-tree.h"
#include "toplev.h"
#include "ggc.h"
#include "tm_p.h"
#include "cpplib.h"
#include "target.h"
#include "debug.h"
#include "opts.h"
#include "timevar.h"
#include "c-common.h"
#include "c-pragma.h"
#include "langhooks.h"
#include "tree-mudflap.h"
#include "tree-gimple.h"
#include "diagnostic.h"
#include "tree-dump.h"
#include "cgraph.h"
#include "hashtab.h"
#include "libfuncs.h"
#include "except.h"
#include "langhooks-def.h"
#include "pointer-set.h"

/* In grokdeclarator, distinguish syntactic contexts of declarators.  */
enum decl_context
{ NORMAL,			/* Ordinary declaration */
  FUNCDEF,			/* Function definition */
  /* APPLE LOCAL blocks 6339747 */
  BLOCKDEF,			/* Block literal declaration */
  PARM,				/* Declaration of parm before function body */
  FIELD,			/* Declaration inside struct or union */
  TYPENAME};			/* Typename (inside cast or sizeof)  */


/* Nonzero if we have seen an invalid cross reference
   to a struct, union, or enum, but not yet printed the message.  */
tree pending_invalid_xref;

/* File and line to appear in the eventual error message.  */
location_t pending_invalid_xref_location;

/* True means we've initialized exception handling.  */
bool c_eh_initialized_p;

/* While defining an enum type, this is 1 plus the last enumerator
   constant value.  Note that will do not have to save this or `enum_overflow'
   around nested function definition since such a definition could only
   occur in an enum value expression and we don't use these variables in
   that case.  */

static tree enum_next_value;

/* Nonzero means that there was overflow computing enum_next_value.  */

static int enum_overflow;

/* The file and line that the prototype came from if this is an
   old-style definition; used for diagnostics in
   store_parm_decls_oldstyle.  */

static location_t current_function_prototype_locus;

/* Whether this prototype was built-in.  */

static bool current_function_prototype_built_in;

/* The argument type information of this prototype.  */

static tree current_function_prototype_arg_types;

/* The argument information structure for the function currently being
   defined.  */

static struct c_arg_info *current_function_arg_info;

/* The obstack on which parser and related data structures, which are
   not live beyond their top-level declaration or definition, are
   allocated.  */
struct obstack parser_obstack;

/* The current statement tree.  */

static GTY(()) struct stmt_tree_s c_stmt_tree;

/* State saving variables.  */
tree c_break_label;
tree c_cont_label;

/* Linked list of TRANSLATION_UNIT_DECLS for the translation units
   included in this invocation.  Note that the current translation
   unit is not included in this list.  */

static GTY(()) tree all_translation_units;

/* A list of decls to be made automatically visible in each file scope.  */
static GTY(()) tree visible_builtins;

/* Set to 0 at beginning of a function definition, set to 1 if
   a return statement that specifies a return value is seen.  */

int current_function_returns_value;

/* Set to 0 at beginning of a function definition, set to 1 if
   a return statement with no argument is seen.  */

int current_function_returns_null;

/* Set to 0 at beginning of a function definition, set to 1 if
   a call to a noreturn function is seen.  */

int current_function_returns_abnormally;

/* Set to nonzero by `grokdeclarator' for a function
   whose return type is defaulted, if warnings for this are desired.  */

static int warn_about_return_type;

/* Nonzero when the current toplevel function contains a declaration
   of a nested function which is never defined.  */

static bool undef_nested_function;

/* True means global_bindings_p should return false even if the scope stack
   says we are in file scope.  */
bool c_override_global_bindings_to_false;


/* Each c_binding structure describes one binding of an identifier to
   a decl.  All the decls in a scope - irrespective of namespace - are
   chained together by the ->prev field, which (as the name implies)
   runs in reverse order.  All the decls in a given namespace bound to
   a given identifier are chained by the ->shadowed field, which runs
   from inner to outer scopes.

   The ->decl field usually points to a DECL node, but there are two
   exceptions.  In the namespace of type tags, the bound entity is a
   RECORD_TYPE, UNION_TYPE, or ENUMERAL_TYPE node.  If an undeclared
   identifier is encountered, it is bound to error_mark_node to
   suppress further errors about that identifier in the current
   function.

   The ->type field stores the type of the declaration in this scope;
   if NULL, the type is the type of the ->decl field.  This is only of
   relevance for objects with external or internal linkage which may
   be redeclared in inner scopes, forming composite types that only
   persist for the duration of those scopes.  In the external scope,
   this stores the composite of all the types declared for this
   object, visible or not.  The ->inner_comp field (used only at file
   scope) stores whether an incomplete array type at file scope was
   completed at an inner scope to an array size other than 1.

   The depth field is copied from the scope structure that holds this
   decl.  It is used to preserve the proper ordering of the ->shadowed
   field (see bind()) and also for a handful of special-case checks.
   Finally, the invisible bit is true for a decl which should be
   ignored for purposes of normal name lookup, and the nested bit is
   true for a decl that's been bound a second time in an inner scope;
   in all such cases, the binding in the outer scope will have its
   invisible bit true.  */

struct c_binding GTY((chain_next ("%h.prev")))
{
  tree decl;			/* the decl bound */
  tree type;			/* the type in this scope */
  tree id;			/* the identifier it's bound to */
  struct c_binding *prev;	/* the previous decl in this scope */
  struct c_binding *shadowed;	/* the innermost decl shadowed by this one */
  unsigned int depth : 28;      /* depth of this scope */
  BOOL_BITFIELD invisible : 1;  /* normal lookup should ignore this binding */
  BOOL_BITFIELD nested : 1;     /* do not set DECL_CONTEXT when popping */
  BOOL_BITFIELD inner_comp : 1; /* incomplete array completed in inner scope */
  /* one free bit */
};
#define B_IN_SCOPE(b1, b2) ((b1)->depth == (b2)->depth)
#define B_IN_CURRENT_SCOPE(b) ((b)->depth == current_scope->depth)
#define B_IN_FILE_SCOPE(b) ((b)->depth == 1 /*file_scope->depth*/)
#define B_IN_EXTERNAL_SCOPE(b) ((b)->depth == 0 /*external_scope->depth*/)

#define I_SYMBOL_BINDING(node) \
  (((struct lang_identifier *) IDENTIFIER_NODE_CHECK(node))->symbol_binding)
#define I_SYMBOL_DECL(node) \
 (I_SYMBOL_BINDING(node) ? I_SYMBOL_BINDING(node)->decl : 0)

#define I_TAG_BINDING(node) \
  (((struct lang_identifier *) IDENTIFIER_NODE_CHECK(node))->tag_binding)
#define I_TAG_DECL(node) \
 (I_TAG_BINDING(node) ? I_TAG_BINDING(node)->decl : 0)

#define I_LABEL_BINDING(node) \
  (((struct lang_identifier *) IDENTIFIER_NODE_CHECK(node))->label_binding)
#define I_LABEL_DECL(node) \
 (I_LABEL_BINDING(node) ? I_LABEL_BINDING(node)->decl : 0)

/* Each C symbol points to three linked lists of c_binding structures.
   These describe the values of the identifier in the three different
   namespaces defined by the language.  */

struct lang_identifier GTY(())
{
  struct c_common_identifier common_id;
  struct c_binding *symbol_binding; /* vars, funcs, constants, typedefs */
  struct c_binding *tag_binding;    /* struct/union/enum tags */
  struct c_binding *label_binding;  /* labels */
};

/* Validate c-lang.c's assumptions.  */
extern char C_SIZEOF_STRUCT_LANG_IDENTIFIER_isnt_accurate
[(sizeof(struct lang_identifier) == C_SIZEOF_STRUCT_LANG_IDENTIFIER) ? 1 : -1];

/* The resulting tree type.  */

union lang_tree_node
  GTY((desc ("TREE_CODE (&%h.generic) == IDENTIFIER_NODE"),
       chain_next ("TREE_CODE (&%h.generic) == INTEGER_TYPE ? (union lang_tree_node *) TYPE_NEXT_VARIANT (&%h.generic) : (union lang_tree_node *) TREE_CHAIN (&%h.generic)")))
{
  union tree_node GTY ((tag ("0"),
			desc ("tree_node_structure (&%h)")))
    generic;
  struct lang_identifier GTY ((tag ("1"))) identifier;
};

/* Each c_scope structure describes the complete contents of one
   scope.  Four scopes are distinguished specially: the innermost or
   current scope, the innermost function scope, the file scope (always
   the second to outermost) and the outermost or external scope.

   Most declarations are recorded in the current scope.

   All normal label declarations are recorded in the innermost
   function scope, as are bindings of undeclared identifiers to
   error_mark_node.  (GCC permits nested functions as an extension,
   hence the 'innermost' qualifier.)  Explicitly declared labels
   (using the __label__ extension) appear in the current scope.

   Being in the file scope (current_scope == file_scope) causes
   special behavior in several places below.  Also, under some
   conditions the Objective-C front end records declarations in the
   file scope even though that isn't the current scope.

   All declarations with external linkage are recorded in the external
   scope, even if they aren't visible there; this models the fact that
   such declarations are visible to the entire program, and (with a
   bit of cleverness, see pushdecl) allows diagnosis of some violations
   of C99 6.2.2p7 and 6.2.7p2:

     If, within the same translation unit, the same identifier appears
     with both internal and external linkage, the behavior is
     undefined.

     All declarations that refer to the same object or function shall
     have compatible type; otherwise, the behavior is undefined.

   Initially only the built-in declarations, which describe compiler
   intrinsic functions plus a subset of the standard library, are in
   this scope.

   The order of the blocks list matters, and it is frequently appended
   to.  To avoid having to walk all the way to the end of the list on
   each insertion, or reverse the list later, we maintain a pointer to
   the last list entry.  (FIXME: It should be feasible to use a reversed
   list here.)

   The bindings list is strictly in reverse order of declarations;
   pop_scope relies on this.  */


struct c_scope GTY((chain_next ("%h.outer")))
{
  /* The scope containing this one.  */
  struct c_scope *outer;

  /* The next outermost function scope.  */
  struct c_scope *outer_function;

  /* All bindings in this scope.  */
  struct c_binding *bindings;

  /* For each scope (except the global one), a chain of BLOCK nodes
     for all the scopes that were entered and exited one level down.  */
  tree blocks;
  tree blocks_last;

  /* The depth of this scope.  Used to keep the ->shadowed chain of
     bindings sorted innermost to outermost.  */
  unsigned int depth : 28;

  /* True if we are currently filling this scope with parameter
     declarations.  */
  BOOL_BITFIELD parm_flag : 1;

  /* True if we saw [*] in this scope.  Used to give an error messages
     if these appears in a function definition.  */
  BOOL_BITFIELD had_vla_unspec : 1;

  /* True if we already complained about forward parameter decls
     in this scope.  This prevents double warnings on
     foo (int a; int b; ...)  */
  BOOL_BITFIELD warned_forward_parm_decls : 1;

  /* True if this is the outermost block scope of a function body.
     This scope contains the parameters, the local variables declared
     in the outermost block, and all the labels (except those in
     nested functions, or declared at block scope with __label__).  */
  BOOL_BITFIELD function_body : 1;

  /* True means make a BLOCK for this scope no matter what.  */
  BOOL_BITFIELD keep : 1;
};

/* The scope currently in effect.  */

static GTY(()) struct c_scope *current_scope;

/* The innermost function scope.  Ordinary (not explicitly declared)
   labels, bindings to error_mark_node, and the lazily-created
   bindings of __func__ and its friends get this scope.  */

static GTY(()) struct c_scope *current_function_scope;

/* The C file scope.  This is reset for each input translation unit.  */

static GTY(()) struct c_scope *file_scope;

/* The outermost scope.  This is used for all declarations with
   external linkage, and only these, hence the name.  */

static GTY(()) struct c_scope *external_scope;

/* A chain of c_scope structures awaiting reuse.  */

static GTY((deletable)) struct c_scope *scope_freelist;

/* A chain of c_binding structures awaiting reuse.  */

static GTY((deletable)) struct c_binding *binding_freelist;

/* Append VAR to LIST in scope SCOPE.  */
#define SCOPE_LIST_APPEND(scope, list, decl) do {	\
  struct c_scope *s_ = (scope);				\
  tree d_ = (decl);					\
  if (s_->list##_last)					\
    TREE_CHAIN (s_->list##_last) = d_;			\
  else							\
    s_->list = d_;					\
  s_->list##_last = d_;					\
} while (0)

/* Concatenate FROM in scope FSCOPE onto TO in scope TSCOPE.  */
#define SCOPE_LIST_CONCAT(tscope, to, fscope, from) do {	\
  struct c_scope *t_ = (tscope);				\
  struct c_scope *f_ = (fscope);				\
  if (t_->to##_last)						\
    TREE_CHAIN (t_->to##_last) = f_->from;			\
  else								\
    t_->to = f_->from;						\
  t_->to##_last = f_->from##_last;				\
} while (0)

/* True means unconditionally make a BLOCK for the next scope pushed.  */

static bool keep_next_level_flag;

/* True means the next call to push_scope will be the outermost scope
   of a function body, so do not push a new scope, merely cease
   expecting parameter decls.  */

static bool next_is_function_body;

/* Functions called automatically at the beginning and end of execution.  */

static GTY(()) tree static_ctors;
static GTY(()) tree static_dtors;

/* Forward declarations.  */
static tree lookup_name_in_scope (tree, struct c_scope *);
static tree c_make_fname_decl (tree, int);
static tree grokdeclarator (const struct c_declarator *,
			    struct c_declspecs *,
			    enum decl_context, bool, tree *);
static tree grokparms (struct c_arg_info *, bool);
static void layout_array_type (tree);

/* T is a statement.  Add it to the statement-tree.  This is the
   C/ObjC version--C++ has a slightly different version of this
   function.  */

tree
add_stmt (tree t)
{
  enum tree_code code = TREE_CODE (t);

  if (EXPR_P (t) && code != LABEL_EXPR)
    {
      if (!EXPR_HAS_LOCATION (t))
	SET_EXPR_LOCATION (t, input_location);
    }

  if (code == LABEL_EXPR || code == CASE_LABEL_EXPR)
    STATEMENT_LIST_HAS_LABEL (cur_stmt_list) = 1;

  /* Add T to the statement-tree.  Non-side-effect statements need to be
     recorded during statement expressions.  */
  append_to_statement_list_force (t, &cur_stmt_list);

  return t;
}

/* States indicating how grokdeclarator() should handle declspecs marked
   with __attribute__((deprecated)).  An object declared as
   __attribute__((deprecated)) suppresses warnings of uses of other
   deprecated items.  */
/* APPLE LOCAL begin "unavailable" attribute (radar 2809697) */
/* Also add an __attribute__((unavailable)).  An object declared as
   __attribute__((unavailable)) suppresses any reports of being
   declared with unavailable or deprecated items.  */
/* APPLE LOCAL end "unavailable" attribute (radar 2809697) */

enum deprecated_states {
  DEPRECATED_NORMAL,
  DEPRECATED_SUPPRESS
  /* APPLE LOCAL "unavailable" attribute (radar 2809697) */
  , DEPRECATED_UNAVAILABLE_SUPPRESS
};

static enum deprecated_states deprecated_state = DEPRECATED_NORMAL;

void
c_print_identifier (FILE *file, tree node, int indent)
{
  print_node (file, "symbol", I_SYMBOL_DECL (node), indent + 4);
  print_node (file, "tag", I_TAG_DECL (node), indent + 4);
  print_node (file, "label", I_LABEL_DECL (node), indent + 4);
  if (C_IS_RESERVED_WORD (node))
    {
      tree rid = ridpointers[C_RID_CODE (node)];
      indent_to (file, indent + 4);
      fprintf (file, "rid %p \"%s\"",
	       (void *) rid, IDENTIFIER_POINTER (rid));
    }
}

/* Establish a binding between NAME, an IDENTIFIER_NODE, and DECL,
   which may be any of several kinds of DECL or TYPE or error_mark_node,
   in the scope SCOPE.  */
static void
bind (tree name, tree decl, struct c_scope *scope, bool invisible, bool nested)
{
  struct c_binding *b, **here;

  if (binding_freelist)
    {
      b = binding_freelist;
      binding_freelist = b->prev;
    }
  else
    b = GGC_NEW (struct c_binding);

  b->shadowed = 0;
  b->decl = decl;
  b->id = name;
  b->depth = scope->depth;
  b->invisible = invisible;
  b->nested = nested;
  b->inner_comp = 0;

  b->type = 0;

  b->prev = scope->bindings;
  scope->bindings = b;

  if (!name)
    return;

  switch (TREE_CODE (decl))
    {
    case LABEL_DECL:     here = &I_LABEL_BINDING (name);   break;
    case ENUMERAL_TYPE:
    case UNION_TYPE:
    case RECORD_TYPE:    here = &I_TAG_BINDING (name);     break;
    case VAR_DECL:
    case FUNCTION_DECL:
    case TYPE_DECL:
    case CONST_DECL:
    case PARM_DECL:
    case ERROR_MARK:     here = &I_SYMBOL_BINDING (name);  break;

    default:
      gcc_unreachable ();
    }

  /* Locate the appropriate place in the chain of shadowed decls
     to insert this binding.  Normally, scope == current_scope and
     this does nothing.  */
  while (*here && (*here)->depth > scope->depth)
    here = &(*here)->shadowed;

  b->shadowed = *here;
  *here = b;
}

/* Clear the binding structure B, stick it on the binding_freelist,
   and return the former value of b->prev.  This is used by pop_scope
   and get_parm_info to iterate destructively over all the bindings
   from a given scope.  */
static struct c_binding *
free_binding_and_advance (struct c_binding *b)
{
  struct c_binding *prev = b->prev;

  memset (b, 0, sizeof (struct c_binding));
  b->prev = binding_freelist;
  binding_freelist = b;

  return prev;
}


/* Hook called at end of compilation to assume 1 elt
   for a file-scope tentative array defn that wasn't complete before.  */

void
c_finish_incomplete_decl (tree decl)
{
  if (TREE_CODE (decl) == VAR_DECL)
    {
      tree type = TREE_TYPE (decl);
      if (type != error_mark_node
	  && TREE_CODE (type) == ARRAY_TYPE
	  && !DECL_EXTERNAL (decl)
	  && TYPE_DOMAIN (type) == 0)
	{
	  warning (0, "array %q+D assumed to have one element", decl);

	  complete_array_type (&TREE_TYPE (decl), NULL_TREE, true);

	  layout_decl (decl, 0);
	}
    }
}

/* The Objective-C front-end often needs to determine the current scope.  */

void *
objc_get_current_scope (void)
{
  return current_scope;
}

/* The following function is used only by Objective-C.  It needs to live here
   because it accesses the innards of c_scope.  */

void
objc_mark_locals_volatile (void *enclosing_blk)
{
  struct c_scope *scope;
  struct c_binding *b;

  for (scope = current_scope;
       scope && scope != enclosing_blk;
       scope = scope->outer)
    {
      for (b = scope->bindings; b; b = b->prev)
	objc_volatilize_decl (b->decl);

      /* Do not climb up past the current function.  */
      if (scope->function_body)
	break;
    }
}

/* Nonzero if we are currently in file scope.  */

int
global_bindings_p (void)
{
  return current_scope == file_scope && !c_override_global_bindings_to_false;
}

void
keep_next_level (void)
{
  keep_next_level_flag = true;
}

/* Identify this scope as currently being filled with parameters.  */

void
declare_parm_level (void)
{
  current_scope->parm_flag = true;
}

void
push_scope (void)
{
  if (next_is_function_body)
    {
      /* This is the transition from the parameters to the top level
	 of the function body.  These are the same scope
	 (C99 6.2.1p4,6) so we do not push another scope structure.
	 next_is_function_body is set only by store_parm_decls, which
	 in turn is called when and only when we are about to
	 encounter the opening curly brace for the function body.

	 The outermost block of a function always gets a BLOCK node,
	 because the debugging output routines expect that each
	 function has at least one BLOCK.  */
      current_scope->parm_flag         = false;
      current_scope->function_body     = true;
      current_scope->keep              = true;
      current_scope->outer_function    = current_function_scope;
      current_function_scope           = current_scope;

      keep_next_level_flag = false;
      next_is_function_body = false;
    }
  else
    {
      struct c_scope *scope;
      if (scope_freelist)
	{
	  scope = scope_freelist;
	  scope_freelist = scope->outer;
	}
      else
	scope = GGC_CNEW (struct c_scope);

      scope->keep          = keep_next_level_flag;
      scope->outer         = current_scope;
      scope->depth	   = current_scope ? (current_scope->depth + 1) : 0;

      /* Check for scope depth overflow.  Unlikely (2^28 == 268,435,456) but
	 possible.  */
      if (current_scope && scope->depth == 0)
	{
	  scope->depth--;
	  sorry ("GCC supports only %u nested scopes", scope->depth);
	}

      current_scope        = scope;
      keep_next_level_flag = false;
    }
}

/* Set the TYPE_CONTEXT of all of TYPE's variants to CONTEXT.  */

static void
set_type_context (tree type, tree context)
{
  for (type = TYPE_MAIN_VARIANT (type); type;
       type = TYPE_NEXT_VARIANT (type))
    TYPE_CONTEXT (type) = context;
}

/* Exit a scope.  Restore the state of the identifier-decl mappings
   that were in effect when this scope was entered.  Return a BLOCK
   node containing all the DECLs in this scope that are of interest
   to debug info generation.  */

tree
pop_scope (void)
{
  struct c_scope *scope = current_scope;
  tree block, context, p;
  struct c_binding *b;

  bool functionbody = scope->function_body;
  bool keep = functionbody || scope->keep || scope->bindings;

  c_end_vm_scope (scope->depth);

  /* If appropriate, create a BLOCK to record the decls for the life
     of this function.  */
  block = 0;
  if (keep)
    {
      block = make_node (BLOCK);
      BLOCK_SUBBLOCKS (block) = scope->blocks;
      TREE_USED (block) = 1;

      /* In each subblock, record that this is its superior.  */
      for (p = scope->blocks; p; p = TREE_CHAIN (p))
	BLOCK_SUPERCONTEXT (p) = block;

      BLOCK_VARS (block) = 0;
    }

  /* The TYPE_CONTEXTs for all of the tagged types belonging to this
     scope must be set so that they point to the appropriate
     construct, i.e.  either to the current FUNCTION_DECL node, or
     else to the BLOCK node we just constructed.

     Note that for tagged types whose scope is just the formal
     parameter list for some function type specification, we can't
     properly set their TYPE_CONTEXTs here, because we don't have a
     pointer to the appropriate FUNCTION_TYPE node readily available
     to us.  For those cases, the TYPE_CONTEXTs of the relevant tagged
     type nodes get set in `grokdeclarator' as soon as we have created
     the FUNCTION_TYPE node which will represent the "scope" for these
     "parameter list local" tagged types.  */
  if (scope->function_body)
    context = current_function_decl;
  else if (scope == file_scope)
    {
      tree file_decl = build_decl (TRANSLATION_UNIT_DECL, 0, 0);
      TREE_CHAIN (file_decl) = all_translation_units;
      all_translation_units = file_decl;
      context = file_decl;
    }
  else
    context = block;

  /* Clear all bindings in this scope.  */
  for (b = scope->bindings; b; b = free_binding_and_advance (b))
    {
      p = b->decl;
      switch (TREE_CODE (p))
	{
	case LABEL_DECL:
	  /* Warnings for unused labels, errors for undefined labels.  */
	  if (TREE_USED (p) && !DECL_INITIAL (p))
	    {
	      error ("label %q+D used but not defined", p);
	      DECL_INITIAL (p) = error_mark_node;
	    }
	  else if (!TREE_USED (p) && warn_unused_label)
	    {
	      if (DECL_INITIAL (p))
		warning (0, "label %q+D defined but not used", p);
	      else
		warning (0, "label %q+D declared but not defined", p);
	    }
	  /* Labels go in BLOCK_VARS.  */
	  TREE_CHAIN (p) = BLOCK_VARS (block);
	  BLOCK_VARS (block) = p;
	  gcc_assert (I_LABEL_BINDING (b->id) == b);
	  I_LABEL_BINDING (b->id) = b->shadowed;
	  break;

	case ENUMERAL_TYPE:
	case UNION_TYPE:
	case RECORD_TYPE:
	  set_type_context (p, context);

	  /* Types may not have tag-names, in which case the type
	     appears in the bindings list with b->id NULL.  */
	  if (b->id)
	    {
	      gcc_assert (I_TAG_BINDING (b->id) == b);
	      I_TAG_BINDING (b->id) = b->shadowed;
	    }
	  break;

	case FUNCTION_DECL:
	  /* Propagate TREE_ADDRESSABLE from nested functions to their
	     containing functions.  */
	  if (!TREE_ASM_WRITTEN (p)
	      && DECL_INITIAL (p) != 0
	      && TREE_ADDRESSABLE (p)
	      && DECL_ABSTRACT_ORIGIN (p) != 0
	      && DECL_ABSTRACT_ORIGIN (p) != p)
	    TREE_ADDRESSABLE (DECL_ABSTRACT_ORIGIN (p)) = 1;
	  if (!DECL_EXTERNAL (p)
	      && DECL_INITIAL (p) == 0
	      && scope != file_scope
	      && scope != external_scope)
	    {
	      error ("nested function %q+D declared but never defined", p);
	      undef_nested_function = true;
	    }
	  /* C99 6.7.4p6: "a function with external linkage... declared
	     with an inline function specifier ... shall also be defined in the
	     same translation unit."  */
	  else if (DECL_DECLARED_INLINE_P (p)
		   && TREE_PUBLIC (p)
		   && !DECL_INITIAL (p)
		   && !flag_gnu89_inline)
	    pedwarn ("inline function %q+D declared but never defined", p);

	  goto common_symbol;

	case VAR_DECL:
	  /* Warnings for unused variables.  */
	  if (!TREE_USED (p)
	      && !TREE_NO_WARNING (p)
	      && !DECL_IN_SYSTEM_HEADER (p)
	      && DECL_NAME (p)
	      && !DECL_ARTIFICIAL (p)
	      && scope != file_scope
	      && scope != external_scope)
	    warning (OPT_Wunused_variable, "unused variable %q+D", p);

	  if (b->inner_comp)
	    {
	      error ("type of array %q+D completed incompatibly with"
		     " implicit initialization", p);
	    }

	  /* Fall through.  */
	case TYPE_DECL:
	case CONST_DECL:
	common_symbol:
	  /* All of these go in BLOCK_VARS, but only if this is the
	     binding in the home scope.  */
	  if (!b->nested)
	    {
	      TREE_CHAIN (p) = BLOCK_VARS (block);
	      BLOCK_VARS (block) = p;
	    }
	  /* If this is the file scope, and we are processing more
	     than one translation unit in this compilation, set
	     DECL_CONTEXT of each decl to the TRANSLATION_UNIT_DECL.
	     This makes same_translation_unit_p work, and causes
	     static declarations to be given disambiguating suffixes.  */
	  if (scope == file_scope && num_in_fnames > 1)
	    {
	      DECL_CONTEXT (p) = context;
	      if (TREE_CODE (p) == TYPE_DECL)
		set_type_context (TREE_TYPE (p), context);
	    }

	  /* Fall through.  */
	  /* Parameters go in DECL_ARGUMENTS, not BLOCK_VARS, and have
	     already been put there by store_parm_decls.  Unused-
	     parameter warnings are handled by function.c.
	     error_mark_node obviously does not go in BLOCK_VARS and
	     does not get unused-variable warnings.  */
	case PARM_DECL:
	case ERROR_MARK:
	  /* It is possible for a decl not to have a name.  We get
	     here with b->id NULL in this case.  */
	  if (b->id)
	    {
	      gcc_assert (I_SYMBOL_BINDING (b->id) == b);
	      I_SYMBOL_BINDING (b->id) = b->shadowed;
	      if (b->shadowed && b->shadowed->type)
		TREE_TYPE (b->shadowed->decl) = b->shadowed->type;
	    }
	  break;

	default:
	  gcc_unreachable ();
	}
    }


  /* Dispose of the block that we just made inside some higher level.  */
  if ((scope->function_body || scope == file_scope) && context)
    {
      DECL_INITIAL (context) = block;
      BLOCK_SUPERCONTEXT (block) = context;
    }
  else if (scope->outer)
    {
      if (block)
	SCOPE_LIST_APPEND (scope->outer, blocks, block);
      /* If we did not make a block for the scope just exited, any
	 blocks made for inner scopes must be carried forward so they
	 will later become subblocks of something else.  */
      else if (scope->blocks)
	SCOPE_LIST_CONCAT (scope->outer, blocks, scope, blocks);
    }

  /* Pop the current scope, and free the structure for reuse.  */
  current_scope = scope->outer;
  if (scope->function_body)
    current_function_scope = scope->outer_function;

  memset (scope, 0, sizeof (struct c_scope));
  scope->outer = scope_freelist;
  scope_freelist = scope;

  return block;
}

void
push_file_scope (void)
{
  tree decl;

  if (file_scope)
    return;

  push_scope ();
  file_scope = current_scope;

  start_fname_decls ();

  for (decl = visible_builtins; decl; decl = TREE_CHAIN (decl))
    bind (DECL_NAME (decl), decl, file_scope,
	  /*invisible=*/false, /*nested=*/true);
}

void
pop_file_scope (void)
{
  /* In case there were missing closebraces, get us back to the global
     binding level.  */
  while (current_scope != file_scope)
    pop_scope ();

  /* __FUNCTION__ is defined at file scope ("").  This
     call may not be necessary as my tests indicate it
     still works without it.  */
  finish_fname_decls ();

  /* This is the point to write out a PCH if we're doing that.
     In that case we do not want to do anything else.  */
  if (pch_file)
    {
      c_common_write_pch ();
      return;
    }

  /* Pop off the file scope and close this translation unit.  */
  pop_scope ();
  file_scope = 0;

  maybe_apply_pending_pragma_weaks ();
  cgraph_finalize_compilation_unit ();
}

/* Insert BLOCK at the end of the list of subblocks of the current
   scope.  This is used when a BIND_EXPR is expanded, to handle the
   BLOCK node inside the BIND_EXPR.  */

void
insert_block (tree block)
{
  TREE_USED (block) = 1;
  SCOPE_LIST_APPEND (current_scope, blocks, block);
}

/* Push a definition or a declaration of struct, union or enum tag "name".
   "type" should be the type node.
   We assume that the tag "name" is not already defined.

   Note that the definition may really be just a forward reference.
   In that case, the TYPE_SIZE will be zero.  */

static void
pushtag (tree name, tree type)
{
  /* Record the identifier as the type's name if it has none.  */
  if (name && !TYPE_NAME (type))
    TYPE_NAME (type) = name;
  bind (name, type, current_scope, /*invisible=*/false, /*nested=*/false);

  /* Create a fake NULL-named TYPE_DECL node whose TREE_TYPE will be the
     tagged type we just added to the current scope.  This fake
     NULL-named TYPE_DECL node helps dwarfout.c to know when it needs
     to output a representation of a tagged type, and it also gives
     us a convenient place to record the "scope start" address for the
     tagged type.  */

  TYPE_STUB_DECL (type) = pushdecl (build_decl (TYPE_DECL, NULL_TREE, type));

  /* An approximation for now, so we can tell this is a function-scope tag.
     This will be updated in pop_scope.  */
  TYPE_CONTEXT (type) = DECL_CONTEXT (TYPE_STUB_DECL (type));
}

/* Subroutine of compare_decls.  Allow harmless mismatches in return
   and argument types provided that the type modes match.  This function
   return a unified type given a suitable match, and 0 otherwise.  */

static tree
match_builtin_function_types (tree newtype, tree oldtype)
{
  tree newrettype, oldrettype;
  tree newargs, oldargs;
  tree trytype, tryargs;

  /* Accept the return type of the new declaration if same modes.  */
  oldrettype = TREE_TYPE (oldtype);
  newrettype = TREE_TYPE (newtype);

  if (TYPE_MODE (oldrettype) != TYPE_MODE (newrettype))
    return 0;

  oldargs = TYPE_ARG_TYPES (oldtype);
  newargs = TYPE_ARG_TYPES (newtype);
  tryargs = newargs;

  while (oldargs || newargs)
    {
      if (!oldargs
	  || !newargs
	  || !TREE_VALUE (oldargs)
	  || !TREE_VALUE (newargs)
	  || TYPE_MODE (TREE_VALUE (oldargs))
	     != TYPE_MODE (TREE_VALUE (newargs)))
	return 0;

      oldargs = TREE_CHAIN (oldargs);
      newargs = TREE_CHAIN (newargs);
    }

  trytype = build_function_type (newrettype, tryargs);
  return build_type_attribute_variant (trytype, TYPE_ATTRIBUTES (oldtype));
}

/* Subroutine of diagnose_mismatched_decls.  Check for function type
   mismatch involving an empty arglist vs a nonempty one and give clearer
   diagnostics.  */
static void
diagnose_arglist_conflict (tree newdecl, tree olddecl,
			   tree newtype, tree oldtype)
{
  tree t;

  if (TREE_CODE (olddecl) != FUNCTION_DECL
      || !comptypes (TREE_TYPE (oldtype), TREE_TYPE (newtype))
      || !((TYPE_ARG_TYPES (oldtype) == 0 && DECL_INITIAL (olddecl) == 0)
	   ||
	   (TYPE_ARG_TYPES (newtype) == 0 && DECL_INITIAL (newdecl) == 0)))
    return;

  t = TYPE_ARG_TYPES (oldtype);
  if (t == 0)
    t = TYPE_ARG_TYPES (newtype);
  for (; t; t = TREE_CHAIN (t))
    {
      tree type = TREE_VALUE (t);

      if (TREE_CHAIN (t) == 0
	  && TYPE_MAIN_VARIANT (type) != void_type_node)
	{
	  inform ("a parameter list with an ellipsis can%'t match "
		  "an empty parameter name list declaration");
	  break;
	}

      if (c_type_promotes_to (type) != type)
	{
	  inform ("an argument type that has a default promotion can%'t match "
		  "an empty parameter name list declaration");
	  break;
	}
    }
}

/* Another subroutine of diagnose_mismatched_decls.  OLDDECL is an
   old-style function definition, NEWDECL is a prototype declaration.
   Diagnose inconsistencies in the argument list.  Returns TRUE if
   the prototype is compatible, FALSE if not.  */
static bool
validate_proto_after_old_defn (tree newdecl, tree newtype, tree oldtype)
{
  tree newargs, oldargs;
  int i;

#define END_OF_ARGLIST(t) ((t) == void_type_node)

  oldargs = TYPE_ACTUAL_ARG_TYPES (oldtype);
  newargs = TYPE_ARG_TYPES (newtype);
  i = 1;

  for (;;)
    {
      tree oldargtype = TREE_VALUE (oldargs);
      tree newargtype = TREE_VALUE (newargs);

      if (oldargtype == error_mark_node || newargtype == error_mark_node)
	return false;

      oldargtype = TYPE_MAIN_VARIANT (oldargtype);
      newargtype = TYPE_MAIN_VARIANT (newargtype);

      if (END_OF_ARGLIST (oldargtype) && END_OF_ARGLIST (newargtype))
	break;

      /* Reaching the end of just one list means the two decls don't
	 agree on the number of arguments.  */
      if (END_OF_ARGLIST (oldargtype))
	{
	  error ("prototype for %q+D declares more arguments "
		 "than previous old-style definition", newdecl);
	  return false;
	}
      else if (END_OF_ARGLIST (newargtype))
	{
	  error ("prototype for %q+D declares fewer arguments "
		 "than previous old-style definition", newdecl);
	  return false;
	}

      /* Type for passing arg must be consistent with that declared
	 for the arg.  */
      else if (!comptypes (oldargtype, newargtype))
	{
	  error ("prototype for %q+D declares argument %d"
		 " with incompatible type",
		 newdecl, i);
	  return false;
	}

      oldargs = TREE_CHAIN (oldargs);
      newargs = TREE_CHAIN (newargs);
      i++;
    }

  /* If we get here, no errors were found, but do issue a warning
     for this poor-style construct.  */
  warning (0, "prototype for %q+D follows non-prototype definition",
	   newdecl);
  return true;
#undef END_OF_ARGLIST
}

/* Subroutine of diagnose_mismatched_decls.  Report the location of DECL,
   first in a pair of mismatched declarations, using the diagnostic
   function DIAG.  */
static void
locate_old_decl (tree decl, void (*diag)(const char *, ...) ATTRIBUTE_GCC_CDIAG(1,2))
{
  if (TREE_CODE (decl) == FUNCTION_DECL && DECL_BUILT_IN (decl))
    ;
  else if (DECL_INITIAL (decl))
    diag (G_("previous definition of %q+D was here"), decl);
  else if (C_DECL_IMPLICIT (decl))
    diag (G_("previous implicit declaration of %q+D was here"), decl);
  else
    diag (G_("previous declaration of %q+D was here"), decl);
}

/* Subroutine of duplicate_decls.  Compare NEWDECL to OLDDECL.
   Returns true if the caller should proceed to merge the two, false
   if OLDDECL should simply be discarded.  As a side effect, issues
   all necessary diagnostics for invalid or poor-style combinations.
   If it returns true, writes the types of NEWDECL and OLDDECL to
   *NEWTYPEP and *OLDTYPEP - these may have been adjusted from
   TREE_TYPE (NEWDECL, OLDDECL) respectively.  */

static bool
diagnose_mismatched_decls (tree newdecl, tree olddecl,
			   tree *newtypep, tree *oldtypep)
{
  tree newtype, oldtype;
  bool pedwarned = false;
  bool warned = false;
  bool retval = true;

#define DECL_EXTERN_INLINE(DECL) (DECL_DECLARED_INLINE_P (DECL)  \
				  && DECL_EXTERNAL (DECL))

  /* If we have error_mark_node for either decl or type, just discard
     the previous decl - we're in an error cascade already.  */
  if (olddecl == error_mark_node || newdecl == error_mark_node)
    return false;
  *oldtypep = oldtype = TREE_TYPE (olddecl);
  *newtypep = newtype = TREE_TYPE (newdecl);
  if (oldtype == error_mark_node || newtype == error_mark_node)
    return false;

  /* Two different categories of symbol altogether.  This is an error
     unless OLDDECL is a builtin.  OLDDECL will be discarded in any case.  */
  if (TREE_CODE (olddecl) != TREE_CODE (newdecl))
    {
      if (!(TREE_CODE (olddecl) == FUNCTION_DECL
	    && DECL_BUILT_IN (olddecl)
	    && !C_DECL_DECLARED_BUILTIN (olddecl)))
	{
	  error ("%q+D redeclared as different kind of symbol", newdecl);
	  locate_old_decl (olddecl, error);
	}
      else if (TREE_PUBLIC (newdecl))
	warning (0, "built-in function %q+D declared as non-function",
		 newdecl);
      else
	warning (OPT_Wshadow, "declaration of %q+D shadows "
		 "a built-in function", newdecl);
      return false;
    }

  /* Enumerators have no linkage, so may only be declared once in a
     given scope.  */
  if (TREE_CODE (olddecl) == CONST_DECL)
    {
      error ("redeclaration of enumerator %q+D", newdecl);
      locate_old_decl (olddecl, error);
      return false;
    }

  if (!comptypes (oldtype, newtype))
    {
      if (TREE_CODE (olddecl) == FUNCTION_DECL
	  && DECL_BUILT_IN (olddecl) && !C_DECL_DECLARED_BUILTIN (olddecl))
	{
	  /* Accept harmless mismatch in function types.
	     This is for the ffs and fprintf builtins.  */
	  tree trytype = match_builtin_function_types (newtype, oldtype);

	  if (trytype && comptypes (newtype, trytype))
	    *oldtypep = oldtype = trytype;
	  else
	    {
	      /* If types don't match for a built-in, throw away the
		 built-in.  No point in calling locate_old_decl here, it
		 won't print anything.  */
	      warning (0, "conflicting types for built-in function %q+D",
		       newdecl);
	      return false;
	    }
	}
      else if (TREE_CODE (olddecl) == FUNCTION_DECL
	       && DECL_IS_BUILTIN (olddecl))
	{
	  /* A conflicting function declaration for a predeclared
	     function that isn't actually built in.  Objective C uses
	     these.  The new declaration silently overrides everything
	     but the volatility (i.e. noreturn) indication.  See also
	     below.  FIXME: Make Objective C use normal builtins.  */
	  TREE_THIS_VOLATILE (newdecl) |= TREE_THIS_VOLATILE (olddecl);
	  return false;
	}
      /* Permit void foo (...) to match int foo (...) if the latter is
	 the definition and implicit int was used.  See
	 c-torture/compile/920625-2.c.  */
      else if (TREE_CODE (newdecl) == FUNCTION_DECL && DECL_INITIAL (newdecl)
	       && TYPE_MAIN_VARIANT (TREE_TYPE (oldtype)) == void_type_node
	       && TYPE_MAIN_VARIANT (TREE_TYPE (newtype)) == integer_type_node
	       && C_FUNCTION_IMPLICIT_INT (newdecl) && !DECL_INITIAL (olddecl))
	{
	  pedwarn ("conflicting types for %q+D", newdecl);
	  /* Make sure we keep void as the return type.  */
	  TREE_TYPE (newdecl) = *newtypep = newtype = oldtype;
	  C_FUNCTION_IMPLICIT_INT (newdecl) = 0;
	  pedwarned = true;
	}
      /* Permit void foo (...) to match an earlier call to foo (...) with
	 no declared type (thus, implicitly int).  */
      else if (TREE_CODE (newdecl) == FUNCTION_DECL
	       && TYPE_MAIN_VARIANT (TREE_TYPE (newtype)) == void_type_node
	       && TYPE_MAIN_VARIANT (TREE_TYPE (oldtype)) == integer_type_node
	       && C_DECL_IMPLICIT (olddecl) && !DECL_INITIAL (olddecl))
	{
	  pedwarn ("conflicting types for %q+D", newdecl);
	  /* Make sure we keep void as the return type.  */
	  TREE_TYPE (olddecl) = *oldtypep = oldtype = newtype;
	  pedwarned = true;
	}
      else
	{
	  if (TYPE_QUALS (newtype) != TYPE_QUALS (oldtype))
	    error ("conflicting type qualifiers for %q+D", newdecl);
	  else
	    error ("conflicting types for %q+D", newdecl);
	  diagnose_arglist_conflict (newdecl, olddecl, newtype, oldtype);
	  locate_old_decl (olddecl, error);
	  return false;
	}
    }

  /* Redeclaration of a type is a constraint violation (6.7.2.3p1),
     but silently ignore the redeclaration if either is in a system
     header.  (Conflicting redeclarations were handled above.)  */
  if (TREE_CODE (newdecl) == TYPE_DECL)
    {
      if (DECL_IN_SYSTEM_HEADER (newdecl) || DECL_IN_SYSTEM_HEADER (olddecl))
	return true;  /* Allow OLDDECL to continue in use.  */

      error ("redefinition of typedef %q+D", newdecl);
      locate_old_decl (olddecl, error);
      return false;
    }

  /* Function declarations can either be 'static' or 'extern' (no
     qualifier is equivalent to 'extern' - C99 6.2.2p5) and therefore
     can never conflict with each other on account of linkage
     (6.2.2p4).  Multiple definitions are not allowed (6.9p3,5) but
     gnu89 mode permits two definitions if one is 'extern inline' and
     one is not.  The non- extern-inline definition supersedes the
     extern-inline definition.  */

  else if (TREE_CODE (newdecl) == FUNCTION_DECL)
    {
      /* If you declare a built-in function name as static, or
	 define the built-in with an old-style definition (so we
	 can't validate the argument list) the built-in definition is
	 overridden, but optionally warn this was a bad choice of name.  */
      if (DECL_BUILT_IN (olddecl)
	  && !C_DECL_DECLARED_BUILTIN (olddecl)
	  && (!TREE_PUBLIC (newdecl)
	      || (DECL_INITIAL (newdecl)
		  && !TYPE_ARG_TYPES (TREE_TYPE (newdecl)))))
	{
	  warning (OPT_Wshadow, "declaration of %q+D shadows "
		   "a built-in function", newdecl);
	  /* Discard the old built-in function.  */
	  return false;
	}

      if (DECL_INITIAL (newdecl))
	{
	  if (DECL_INITIAL (olddecl))
	    {
	      /* If both decls are in the same TU and the new declaration
		 isn't overriding an extern inline reject the new decl.
		 In c99, no overriding is allowed in the same translation
		 unit.  */
	      if ((!DECL_EXTERN_INLINE (olddecl)
		   || DECL_EXTERN_INLINE (newdecl)
		   || (!flag_gnu89_inline
		       && (!DECL_DECLARED_INLINE_P (olddecl)
			   || !lookup_attribute ("gnu_inline",
						 DECL_ATTRIBUTES (olddecl)))
		       && (!DECL_DECLARED_INLINE_P (newdecl)
			   || !lookup_attribute ("gnu_inline",
						 DECL_ATTRIBUTES (newdecl))))
		  )
		  && same_translation_unit_p (newdecl, olddecl))
		{
		  error ("redefinition of %q+D", newdecl);
		  locate_old_decl (olddecl, error);
		  return false;
		}
	    }
	}
      /* If we have a prototype after an old-style function definition,
	 the argument types must be checked specially.  */
      else if (DECL_INITIAL (olddecl)
	       && !TYPE_ARG_TYPES (oldtype) && TYPE_ARG_TYPES (newtype)
	       && TYPE_ACTUAL_ARG_TYPES (oldtype)
	       && !validate_proto_after_old_defn (newdecl, newtype, oldtype))
	{
	  locate_old_decl (olddecl, error);
	  return false;
	}
      /* A non-static declaration (even an "extern") followed by a
	 static declaration is undefined behavior per C99 6.2.2p3-5,7.
	 The same is true for a static forward declaration at block
	 scope followed by a non-static declaration/definition at file
	 scope.  Static followed by non-static at the same scope is
	 not undefined behavior, and is the most convenient way to get
	 some effects (see e.g.  what unwind-dw2-fde-glibc.c does to
	 the definition of _Unwind_Find_FDE in unwind-dw2-fde.c), but
	 we do diagnose it if -Wtraditional.  */
      if (TREE_PUBLIC (olddecl) && !TREE_PUBLIC (newdecl))
	{
	  /* Two exceptions to the rule.  If olddecl is an extern
	     inline, or a predeclared function that isn't actually
	     built in, newdecl silently overrides olddecl.  The latter
	     occur only in Objective C; see also above.  (FIXME: Make
	     Objective C use normal builtins.)  */
	  if (!DECL_IS_BUILTIN (olddecl)
	      && !DECL_EXTERN_INLINE (olddecl))
	    {
	      error ("static declaration of %q+D follows "
		     "non-static declaration", newdecl);
	      locate_old_decl (olddecl, error);
	    }
	  return false;
	}
      else if (TREE_PUBLIC (newdecl) && !TREE_PUBLIC (olddecl))
	{
	  if (DECL_CONTEXT (olddecl))
	    {
	      error ("non-static declaration of %q+D follows "
		     "static declaration", newdecl);
	      locate_old_decl (olddecl, error);
	      return false;
	    }
	  else if (warn_traditional)
	    {
	      warning (OPT_Wtraditional, "non-static declaration of %q+D "
		       "follows static declaration", newdecl);
	      warned = true;
	    }
	}

      /* Make sure gnu_inline attribute is either not present, or
	 present on all inline decls.  */
      if (DECL_DECLARED_INLINE_P (olddecl)
	  && DECL_DECLARED_INLINE_P (newdecl))
	{
	  bool newa = lookup_attribute ("gnu_inline",
					DECL_ATTRIBUTES (newdecl)) != NULL;
	  bool olda = lookup_attribute ("gnu_inline",
					DECL_ATTRIBUTES (olddecl)) != NULL;
	  if (newa != olda)
	    {
	      error ("%<gnu_inline%> attribute present on %q+D",
		     newa ? newdecl : olddecl);
	      error ("%Jbut not here", newa ? olddecl : newdecl);
	    }
	}
    }
  else if (TREE_CODE (newdecl) == VAR_DECL)
    {
      /* Only variables can be thread-local, and all declarations must
	 agree on this property.  */
      if (C_DECL_THREADPRIVATE_P (olddecl) && !DECL_THREAD_LOCAL_P (newdecl))
	{
	  /* Nothing to check.  Since OLDDECL is marked threadprivate
	     and NEWDECL does not have a thread-local attribute, we
	     will merge the threadprivate attribute into NEWDECL.  */
	  ;
	}
      else if (DECL_THREAD_LOCAL_P (newdecl) != DECL_THREAD_LOCAL_P (olddecl))
	{
	  if (DECL_THREAD_LOCAL_P (newdecl))
	    error ("thread-local declaration of %q+D follows "
		   "non-thread-local declaration", newdecl);
	  else
	    error ("non-thread-local declaration of %q+D follows "
		   "thread-local declaration", newdecl);

	  locate_old_decl (olddecl, error);
	  return false;
	}

      /* Multiple initialized definitions are not allowed (6.9p3,5).  */
      if (DECL_INITIAL (newdecl) && DECL_INITIAL (olddecl))
	{
	  error ("redefinition of %q+D", newdecl);
	  locate_old_decl (olddecl, error);
	  return false;
	}

      /* Objects declared at file scope: if the first declaration had
	 external linkage (even if it was an external reference) the
	 second must have external linkage as well, or the behavior is
	 undefined.  If the first declaration had internal linkage, then
	 the second must too, or else be an external reference (in which
	 case the composite declaration still has internal linkage).
	 As for function declarations, we warn about the static-then-
	 extern case only for -Wtraditional.  See generally 6.2.2p3-5,7.  */
      if (DECL_FILE_SCOPE_P (newdecl)
	  && TREE_PUBLIC (newdecl) != TREE_PUBLIC (olddecl))
	{
	  if (DECL_EXTERNAL (newdecl))
	    {
	      if (!DECL_FILE_SCOPE_P (olddecl))
		{
		  error ("extern declaration of %q+D follows "
			 "declaration with no linkage", newdecl);
		  locate_old_decl (olddecl, error);
		  return false;
		}
	      else if (warn_traditional)
		{
		  warning (OPT_Wtraditional, "non-static declaration of %q+D "
			   "follows static declaration", newdecl);
		  warned = true;
		}
	    }
	  else
	    {
	      if (TREE_PUBLIC (newdecl))
		error ("non-static declaration of %q+D follows "
		       "static declaration", newdecl);
	      else
		error ("static declaration of %q+D follows "
		       "non-static declaration", newdecl);

	      locate_old_decl (olddecl, error);
	      return false;
	    }
	}
      /* Two objects with the same name declared at the same block
	 scope must both be external references (6.7p3).  */
      else if (!DECL_FILE_SCOPE_P (newdecl))
	{
	  if (DECL_EXTERNAL (newdecl))
	    {
	      /* Extern with initializer at block scope, which will
		 already have received an error.  */
	    }
	  else if (DECL_EXTERNAL (olddecl))
	    {
	      error ("declaration of %q+D with no linkage follows "
		     "extern declaration", newdecl);
	      locate_old_decl (olddecl, error);
	    }
	  else
	    {
	      error ("redeclaration of %q+D with no linkage", newdecl);
	      locate_old_decl (olddecl, error);
	    }

	  return false;
	}
    }

  /* warnings */
  /* All decls must agree on a visibility.  */
  if (CODE_CONTAINS_STRUCT (TREE_CODE (newdecl), TS_DECL_WITH_VIS)
      && DECL_VISIBILITY_SPECIFIED (newdecl) && DECL_VISIBILITY_SPECIFIED (olddecl)
      && DECL_VISIBILITY (newdecl) != DECL_VISIBILITY (olddecl))
    {
      warning (0, "redeclaration of %q+D with different visibility "
	       "(old visibility preserved)", newdecl);
      warned = true;
    }

  if (TREE_CODE (newdecl) == FUNCTION_DECL)
    {
      /* Diagnose inline __attribute__ ((noinline)) which is silly.  */
      if (DECL_DECLARED_INLINE_P (newdecl)
	  && lookup_attribute ("noinline", DECL_ATTRIBUTES (olddecl)))
	{
	  warning (OPT_Wattributes, "inline declaration of %qD follows "
		   "declaration with attribute noinline", newdecl);
	  warned = true;
	}
      else if (DECL_DECLARED_INLINE_P (olddecl)
	       && lookup_attribute ("noinline", DECL_ATTRIBUTES (newdecl)))
	{
	  warning (OPT_Wattributes, "declaration of %q+D with attribute "
		   "noinline follows inline declaration ", newdecl);
	  warned = true;
	}

      /* Inline declaration after use or definition.
	 ??? Should we still warn about this now we have unit-at-a-time
	 mode and can get it right?
	 Definitely don't complain if the decls are in different translation
	 units.
	 C99 permits this, so don't warn in that case.  (The function
	 may not be inlined everywhere in function-at-a-time mode, but
	 we still shouldn't warn.)  */
      if (DECL_DECLARED_INLINE_P (newdecl) && !DECL_DECLARED_INLINE_P (olddecl)
	  && same_translation_unit_p (olddecl, newdecl)
	  && flag_gnu89_inline)
	{
	  if (TREE_USED (olddecl))
	    {
	      warning (0, "%q+D declared inline after being called", olddecl);
	      warned = true;
	    }
	  else if (DECL_INITIAL (olddecl))
	    {
	      warning (0, "%q+D declared inline after its definition", olddecl);
	      warned = true;
	    }
	}
    }
  else /* PARM_DECL, VAR_DECL */
    {
      /* Redeclaration of a parameter is a constraint violation (this is
	 not explicitly stated, but follows from C99 6.7p3 [no more than
	 one declaration of the same identifier with no linkage in the
	 same scope, except type tags] and 6.2.2p6 [parameters have no
	 linkage]).  We must check for a forward parameter declaration,
	 indicated by TREE_ASM_WRITTEN on the old declaration - this is
	 an extension, the mandatory diagnostic for which is handled by
	 mark_forward_parm_decls.  */

      if (TREE_CODE (newdecl) == PARM_DECL
	  && (!TREE_ASM_WRITTEN (olddecl) || TREE_ASM_WRITTEN (newdecl)))
	{
	  error ("redefinition of parameter %q+D", newdecl);
	  locate_old_decl (olddecl, error);
	  return false;
	}
    }

  /* Optional warning for completely redundant decls.  */
  if (!warned && !pedwarned
      && warn_redundant_decls
      /* Don't warn about a function declaration followed by a
	 definition.  */
      && !(TREE_CODE (newdecl) == FUNCTION_DECL
	   && DECL_INITIAL (newdecl) && !DECL_INITIAL (olddecl))
      /* Don't warn about redundant redeclarations of builtins.  */
      && !(TREE_CODE (newdecl) == FUNCTION_DECL
	   && !DECL_BUILT_IN (newdecl)
	   && DECL_BUILT_IN (olddecl)
	   && !C_DECL_DECLARED_BUILTIN (olddecl))
      /* Don't warn about an extern followed by a definition.  */
      && !(DECL_EXTERNAL (olddecl) && !DECL_EXTERNAL (newdecl))
      /* Don't warn about forward parameter decls.  */
      && !(TREE_CODE (newdecl) == PARM_DECL
	   && TREE_ASM_WRITTEN (olddecl) && !TREE_ASM_WRITTEN (newdecl))
      /* Don't warn about a variable definition following a declaration.  */
      && !(TREE_CODE (newdecl) == VAR_DECL
	   && DECL_INITIAL (newdecl) && !DECL_INITIAL (olddecl)))
    {
      warning (OPT_Wredundant_decls, "redundant redeclaration of %q+D",
	       newdecl);
      warned = true;
    }

  /* Report location of previous decl/defn in a consistent manner.  */
  if (warned || pedwarned)
    locate_old_decl (olddecl, pedwarned ? pedwarn : warning0);

#undef DECL_EXTERN_INLINE

  return retval;
}

/* Subroutine of duplicate_decls.  NEWDECL has been found to be
   consistent with OLDDECL, but carries new information.  Merge the
   new information into OLDDECL.  This function issues no
   diagnostics.  */

static void
merge_decls (tree newdecl, tree olddecl, tree newtype, tree oldtype)
{
  bool new_is_definition = (TREE_CODE (newdecl) == FUNCTION_DECL
			    && DECL_INITIAL (newdecl) != 0);
  bool new_is_prototype = (TREE_CODE (newdecl) == FUNCTION_DECL
			   && TYPE_ARG_TYPES (TREE_TYPE (newdecl)) != 0);
  bool old_is_prototype = (TREE_CODE (olddecl) == FUNCTION_DECL
			   && TYPE_ARG_TYPES (TREE_TYPE (olddecl)) != 0);
  bool extern_changed = false;

  /* For real parm decl following a forward decl, rechain the old decl
     in its new location and clear TREE_ASM_WRITTEN (it's not a
     forward decl anymore).  */
  if (TREE_CODE (newdecl) == PARM_DECL
      && TREE_ASM_WRITTEN (olddecl) && !TREE_ASM_WRITTEN (newdecl))
    {
      struct c_binding *b, **here;

      for (here = &current_scope->bindings; *here; here = &(*here)->prev)
	if ((*here)->decl == olddecl)
	  goto found;
      gcc_unreachable ();

    found:
      b = *here;
      *here = b->prev;
      b->prev = current_scope->bindings;
      current_scope->bindings = b;

      TREE_ASM_WRITTEN (olddecl) = 0;
    }

  DECL_ATTRIBUTES (newdecl)
    = targetm.merge_decl_attributes (olddecl, newdecl);

  /* Merge the data types specified in the two decls.  */
  TREE_TYPE (newdecl)
    = TREE_TYPE (olddecl)
    = composite_type (newtype, oldtype);

  /* Lay the type out, unless already done.  */
  if (!comptypes (oldtype, TREE_TYPE (newdecl)))
    {
      if (TREE_TYPE (newdecl) != error_mark_node)
	layout_type (TREE_TYPE (newdecl));
      if (TREE_CODE (newdecl) != FUNCTION_DECL
	  && TREE_CODE (newdecl) != TYPE_DECL
	  && TREE_CODE (newdecl) != CONST_DECL)
	layout_decl (newdecl, 0);
    }
  else
    {
      /* Since the type is OLDDECL's, make OLDDECL's size go with.  */
      DECL_SIZE (newdecl) = DECL_SIZE (olddecl);
      DECL_SIZE_UNIT (newdecl) = DECL_SIZE_UNIT (olddecl);
      DECL_MODE (newdecl) = DECL_MODE (olddecl);
      if (DECL_ALIGN (olddecl) > DECL_ALIGN (newdecl))
	{
	  DECL_ALIGN (newdecl) = DECL_ALIGN (olddecl);
	  DECL_USER_ALIGN (newdecl) |= DECL_ALIGN (olddecl);
	}
    }


  /* Merge the type qualifiers.  */
  if (TREE_READONLY (newdecl))
    TREE_READONLY (olddecl) = 1;

  if (TREE_THIS_VOLATILE (newdecl))
    TREE_THIS_VOLATILE (olddecl) = 1;

  /* Merge deprecatedness.  */
  if (TREE_DEPRECATED (newdecl))
    TREE_DEPRECATED (olddecl) = 1;

  /* APPLE LOCAL begin "unavailable" attribute (radar 2809697) */
  /* Merge unavailableness.  */
  if (TREE_UNAVAILABLE (newdecl))
    TREE_UNAVAILABLE (olddecl) = 1;
  /* APPLE LOCAL end "unavailable" attribute (radar 2809697) */

  /* Keep source location of definition rather than declaration and of
     prototype rather than non-prototype unless that prototype is
     built-in.  */
  if ((DECL_INITIAL (newdecl) == 0 && DECL_INITIAL (olddecl) != 0)
      || (old_is_prototype && !new_is_prototype
	  && !C_DECL_BUILTIN_PROTOTYPE (olddecl)))
    DECL_SOURCE_LOCATION (newdecl) = DECL_SOURCE_LOCATION (olddecl);

  /* Merge the initialization information.  */
   if (DECL_INITIAL (newdecl) == 0)
    DECL_INITIAL (newdecl) = DECL_INITIAL (olddecl);

  /* Merge the threadprivate attribute.  */
  if (TREE_CODE (olddecl) == VAR_DECL && C_DECL_THREADPRIVATE_P (olddecl))
    {
      DECL_TLS_MODEL (newdecl) = DECL_TLS_MODEL (olddecl);
      C_DECL_THREADPRIVATE_P (newdecl) = 1;
    }

  if (CODE_CONTAINS_STRUCT (TREE_CODE (olddecl), TS_DECL_WITH_VIS))
    {
      /* Merge the unused-warning information.  */
      if (DECL_IN_SYSTEM_HEADER (olddecl))
	DECL_IN_SYSTEM_HEADER (newdecl) = 1;
      else if (DECL_IN_SYSTEM_HEADER (newdecl))
	DECL_IN_SYSTEM_HEADER (olddecl) = 1;

      /* Merge the section attribute.
	 We want to issue an error if the sections conflict but that
	 must be done later in decl_attributes since we are called
	 before attributes are assigned.  */
      if (DECL_SECTION_NAME (newdecl) == NULL_TREE)
	DECL_SECTION_NAME (newdecl) = DECL_SECTION_NAME (olddecl);

      /* Copy the assembler name.
	 Currently, it can only be defined in the prototype.  */
      COPY_DECL_ASSEMBLER_NAME (olddecl, newdecl);

      /* Use visibility of whichever declaration had it specified */
      if (DECL_VISIBILITY_SPECIFIED (olddecl))
	{
	  DECL_VISIBILITY (newdecl) = DECL_VISIBILITY (olddecl);
	  DECL_VISIBILITY_SPECIFIED (newdecl) = 1;
	}

      if (TREE_CODE (newdecl) == FUNCTION_DECL)
	{
	  DECL_STATIC_CONSTRUCTOR(newdecl) |= DECL_STATIC_CONSTRUCTOR(olddecl);
	  DECL_STATIC_DESTRUCTOR (newdecl) |= DECL_STATIC_DESTRUCTOR (olddecl);
	  DECL_NO_LIMIT_STACK (newdecl) |= DECL_NO_LIMIT_STACK (olddecl);
	  DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (newdecl)
	    |= DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (olddecl);
	  TREE_THIS_VOLATILE (newdecl) |= TREE_THIS_VOLATILE (olddecl);
	  TREE_READONLY (newdecl) |= TREE_READONLY (olddecl);
	  DECL_IS_MALLOC (newdecl) |= DECL_IS_MALLOC (olddecl);
	  DECL_IS_PURE (newdecl) |= DECL_IS_PURE (olddecl);
	  DECL_IS_NOVOPS (newdecl) |= DECL_IS_NOVOPS (olddecl);
	}

      /* Merge the storage class information.  */
      merge_weak (newdecl, olddecl);

      /* For functions, static overrides non-static.  */
      if (TREE_CODE (newdecl) == FUNCTION_DECL)
	{
	  TREE_PUBLIC (newdecl) &= TREE_PUBLIC (olddecl);
	  /* This is since we don't automatically
	     copy the attributes of NEWDECL into OLDDECL.  */
	  TREE_PUBLIC (olddecl) = TREE_PUBLIC (newdecl);
	  /* If this clears `static', clear it in the identifier too.  */
	  if (!TREE_PUBLIC (olddecl))
	    TREE_PUBLIC (DECL_NAME (olddecl)) = 0;
	}
    }

  /* In c99, 'extern' declaration before (or after) 'inline' means this
     function is not DECL_EXTERNAL, unless 'gnu_inline' attribute
     is present.  */
  if (TREE_CODE (newdecl) == FUNCTION_DECL
      && !flag_gnu89_inline
      && (DECL_DECLARED_INLINE_P (newdecl)
	  || DECL_DECLARED_INLINE_P (olddecl))
      && (!DECL_DECLARED_INLINE_P (newdecl)
	  || !DECL_DECLARED_INLINE_P (olddecl)
	  || !DECL_EXTERNAL (olddecl))
      && DECL_EXTERNAL (newdecl)
      && !lookup_attribute ("gnu_inline", DECL_ATTRIBUTES (newdecl)))
    DECL_EXTERNAL (newdecl) = 0;

  if (DECL_EXTERNAL (newdecl))
    {
      TREE_STATIC (newdecl) = TREE_STATIC (olddecl);
      DECL_EXTERNAL (newdecl) = DECL_EXTERNAL (olddecl);

      /* An extern decl does not override previous storage class.  */
      TREE_PUBLIC (newdecl) = TREE_PUBLIC (olddecl);
      if (!DECL_EXTERNAL (newdecl))
	{
	  DECL_CONTEXT (newdecl) = DECL_CONTEXT (olddecl);
	  DECL_COMMON (newdecl) = DECL_COMMON (olddecl);
	}
    }
  else
    {
      TREE_STATIC (olddecl) = TREE_STATIC (newdecl);
      TREE_PUBLIC (olddecl) = TREE_PUBLIC (newdecl);
    }

  if (TREE_CODE (newdecl) == FUNCTION_DECL)
    {
      /* If we're redefining a function previously defined as extern
	 inline, make sure we emit debug info for the inline before we
	 throw it away, in case it was inlined into a function that
	 hasn't been written out yet.  */
      if (new_is_definition && DECL_INITIAL (olddecl))
	{
	  if (TREE_USED (olddecl)
	      /* In unit-at-a-time mode we never inline re-defined extern
		 inline functions.  */
	      && !flag_unit_at_a_time
	      && cgraph_function_possibly_inlined_p (olddecl))
	    (*debug_hooks->outlining_inline_function) (olddecl);

	  /* The new defn must not be inline.  */
	  DECL_INLINE (newdecl) = 0;
	  DECL_UNINLINABLE (newdecl) = 1;
	}
      else
	{
	  /* If either decl says `inline', this fn is inline, unless
	     its definition was passed already.  */
	  if (DECL_DECLARED_INLINE_P (newdecl)
	      || DECL_DECLARED_INLINE_P (olddecl))
	    DECL_DECLARED_INLINE_P (newdecl) = 1;

	  DECL_UNINLINABLE (newdecl) = DECL_UNINLINABLE (olddecl)
	    = (DECL_UNINLINABLE (newdecl) || DECL_UNINLINABLE (olddecl));
	}

      if (DECL_BUILT_IN (olddecl))
	{
	  /* If redeclaring a builtin function, it stays built in.
	     But it gets tagged as having been declared.  */
	  DECL_BUILT_IN_CLASS (newdecl) = DECL_BUILT_IN_CLASS (olddecl);
	  DECL_FUNCTION_CODE (newdecl) = DECL_FUNCTION_CODE (olddecl);
	  C_DECL_DECLARED_BUILTIN (newdecl) = 1;
	  if (new_is_prototype)
	    C_DECL_BUILTIN_PROTOTYPE (newdecl) = 0;
	  else
	    C_DECL_BUILTIN_PROTOTYPE (newdecl)
	      = C_DECL_BUILTIN_PROTOTYPE (olddecl);
	}

      /* Also preserve various other info from the definition.  */
      if (!new_is_definition)
	{
	  DECL_RESULT (newdecl) = DECL_RESULT (olddecl);
	  DECL_INITIAL (newdecl) = DECL_INITIAL (olddecl);
	  DECL_STRUCT_FUNCTION (newdecl) = DECL_STRUCT_FUNCTION (olddecl);
	  DECL_SAVED_TREE (newdecl) = DECL_SAVED_TREE (olddecl);
	  DECL_ARGUMENTS (newdecl) = DECL_ARGUMENTS (olddecl);

	  /* Set DECL_INLINE on the declaration if we've got a body
	     from which to instantiate.  */
	  if (DECL_INLINE (olddecl) && !DECL_UNINLINABLE (newdecl))
	    {
	      DECL_INLINE (newdecl) = 1;
	      DECL_ABSTRACT_ORIGIN (newdecl)
		= DECL_ABSTRACT_ORIGIN (olddecl);
	    }
	}
      else
	{
	  /* If a previous declaration said inline, mark the
	     definition as inlinable.  */
	  if (DECL_DECLARED_INLINE_P (newdecl)
	      && !DECL_UNINLINABLE (newdecl))
	    DECL_INLINE (newdecl) = 1;
	}
    }

   extern_changed = DECL_EXTERNAL (olddecl) && !DECL_EXTERNAL (newdecl);

  /* Copy most of the decl-specific fields of NEWDECL into OLDDECL.
     But preserve OLDDECL's DECL_UID and DECL_CONTEXT.  */
  {
    unsigned olddecl_uid = DECL_UID (olddecl);
    tree olddecl_context = DECL_CONTEXT (olddecl);

    memcpy ((char *) olddecl + sizeof (struct tree_common),
	    (char *) newdecl + sizeof (struct tree_common),
	    sizeof (struct tree_decl_common) - sizeof (struct tree_common));
    switch (TREE_CODE (olddecl))
      {
      case FIELD_DECL:
      case VAR_DECL:
      case PARM_DECL:
      case LABEL_DECL:
      case RESULT_DECL:
      case CONST_DECL:
      case TYPE_DECL:
      case FUNCTION_DECL:
	memcpy ((char *) olddecl + sizeof (struct tree_decl_common),
		(char *) newdecl + sizeof (struct tree_decl_common),
		tree_code_size (TREE_CODE (olddecl)) - sizeof (struct tree_decl_common));
	break;

      default:

	memcpy ((char *) olddecl + sizeof (struct tree_decl_common),
		(char *) newdecl + sizeof (struct tree_decl_common),
		sizeof (struct tree_decl_non_common) - sizeof (struct tree_decl_common));
      }
    DECL_UID (olddecl) = olddecl_uid;
    DECL_CONTEXT (olddecl) = olddecl_context;
  }

  /* If OLDDECL had its DECL_RTL instantiated, re-invoke make_decl_rtl
     so that encode_section_info has a chance to look at the new decl
     flags and attributes.  */
  if (DECL_RTL_SET_P (olddecl)
      && (TREE_CODE (olddecl) == FUNCTION_DECL
	  || (TREE_CODE (olddecl) == VAR_DECL
	      && TREE_STATIC (olddecl))))
    make_decl_rtl (olddecl);

  /* If we changed a function from DECL_EXTERNAL to !DECL_EXTERNAL,
     and the definition is coming from the old version, cgraph needs
     to be called again.  */
  if (extern_changed && !new_is_definition 
      && TREE_CODE (olddecl) == FUNCTION_DECL && DECL_INITIAL (olddecl))
    cgraph_finalize_function (olddecl, false);
}

/* Handle when a new declaration NEWDECL has the same name as an old
   one OLDDECL in the same binding contour.  Prints an error message
   if appropriate.

   If safely possible, alter OLDDECL to look like NEWDECL, and return
   true.  Otherwise, return false.  */

static bool
duplicate_decls (tree newdecl, tree olddecl)
{
  tree newtype = NULL, oldtype = NULL;

  if (!diagnose_mismatched_decls (newdecl, olddecl, &newtype, &oldtype))
    {
      /* Avoid `unused variable' and other warnings warnings for OLDDECL.  */
      TREE_NO_WARNING (olddecl) = 1;
      return false;
    }

  merge_decls (newdecl, olddecl, newtype, oldtype);
  return true;
}


/* Check whether decl-node NEW_DECL shadows an existing declaration.  */
static void
warn_if_shadowing (tree new_decl)
{
  struct c_binding *b;

  /* Shadow warnings wanted?  */
  if (!warn_shadow
      /* No shadow warnings for internally generated vars.  */
      || DECL_IS_BUILTIN (new_decl)
      /* No shadow warnings for vars made for inlining.  */
      || DECL_FROM_INLINE (new_decl))
    return;

  /* Is anything being shadowed?  Invisible decls do not count.  */
  for (b = I_SYMBOL_BINDING (DECL_NAME (new_decl)); b; b = b->shadowed)
    if (b->decl && b->decl != new_decl && !b->invisible)
      {
	tree old_decl = b->decl;

	if (old_decl == error_mark_node)
	  {
	    warning (OPT_Wshadow, "declaration of %q+D shadows previous "
		     "non-variable", new_decl);
	    break;
	  }
	else if (TREE_CODE (old_decl) == PARM_DECL)
	  warning (OPT_Wshadow, "declaration of %q+D shadows a parameter",
		   new_decl);
	else if (DECL_FILE_SCOPE_P (old_decl))
	  warning (OPT_Wshadow, "declaration of %q+D shadows a global "
		   "declaration", new_decl);
	else if (TREE_CODE (old_decl) == FUNCTION_DECL
		 && DECL_BUILT_IN (old_decl))
	  {
	    warning (OPT_Wshadow, "declaration of %q+D shadows "
		     "a built-in function", new_decl);
	    break;
	  }
	else
	  warning (OPT_Wshadow, "declaration of %q+D shadows a previous local",
		   new_decl);

	warning (OPT_Wshadow, "%Jshadowed declaration is here", old_decl);

	break;
      }
}


/* Subroutine of pushdecl.

   X is a TYPE_DECL for a typedef statement.  Create a brand new
   ..._TYPE node (which will be just a variant of the existing
   ..._TYPE node with identical properties) and then install X
   as the TYPE_NAME of this brand new (duplicate) ..._TYPE node.

   The whole point here is to end up with a situation where each
   and every ..._TYPE node the compiler creates will be uniquely
   associated with AT MOST one node representing a typedef name.
   This way, even though the compiler substitutes corresponding
   ..._TYPE nodes for TYPE_DECL (i.e. "typedef name") nodes very
   early on, later parts of the compiler can always do the reverse
   translation and get back the corresponding typedef name.  For
   example, given:

	typedef struct S MY_TYPE;
	MY_TYPE object;

   Later parts of the compiler might only know that `object' was of
   type `struct S' if it were not for code just below.  With this
   code however, later parts of the compiler see something like:

	struct S' == struct S
	typedef struct S' MY_TYPE;
	struct S' object;

    And they can then deduce (from the node for type struct S') that
    the original object declaration was:

		MY_TYPE object;

    Being able to do this is important for proper support of protoize,
    and also for generating precise symbolic debugging information
    which takes full account of the programmer's (typedef) vocabulary.

    Obviously, we don't want to generate a duplicate ..._TYPE node if
    the TYPE_DECL node that we are now processing really represents a
    standard built-in type.

    Since all standard types are effectively declared at line zero
    in the source file, we can easily check to see if we are working
    on a standard type by checking the current value of lineno.  */

static void
clone_underlying_type (tree x)
{
  if (DECL_IS_BUILTIN (x))
    {
      if (TYPE_NAME (TREE_TYPE (x)) == 0)
	TYPE_NAME (TREE_TYPE (x)) = x;
    }
  else if (TREE_TYPE (x) != error_mark_node
	   && DECL_ORIGINAL_TYPE (x) == NULL_TREE)
    {
      tree tt = TREE_TYPE (x);
      DECL_ORIGINAL_TYPE (x) = tt;
      tt = build_variant_type_copy (tt);
      TYPE_NAME (tt) = x;
      TREE_USED (tt) = TREE_USED (x);
      TREE_TYPE (x) = tt;
    }
}

/* Record a decl-node X as belonging to the current lexical scope.
   Check for errors (such as an incompatible declaration for the same
   name already seen in the same scope).

   Returns either X or an old decl for the same name.
   If an old decl is returned, it may have been smashed
   to agree with what X says.  */

tree
pushdecl (tree x)
{
  tree name = DECL_NAME (x);
  struct c_scope *scope = current_scope;
  struct c_binding *b;
  bool nested = false;

  /* Functions need the lang_decl data.  */
  if (TREE_CODE (x) == FUNCTION_DECL && !DECL_LANG_SPECIFIC (x))
    DECL_LANG_SPECIFIC (x) = GGC_CNEW (struct lang_decl);

  /* Must set DECL_CONTEXT for everything not at file scope or
     DECL_FILE_SCOPE_P won't work.  Local externs don't count
     unless they have initializers (which generate code).  */
  if (current_function_decl
      && ((TREE_CODE (x) != FUNCTION_DECL && TREE_CODE (x) != VAR_DECL)
	  || DECL_INITIAL (x) || !DECL_EXTERNAL (x)))
    DECL_CONTEXT (x) = current_function_decl;

  /* If this is of variably modified type, prevent jumping into its
     scope.  */
  if ((TREE_CODE (x) == VAR_DECL || TREE_CODE (x) == TYPE_DECL)
      && variably_modified_type_p (TREE_TYPE (x), NULL_TREE))
    c_begin_vm_scope (scope->depth);

  /* Anonymous decls are just inserted in the scope.  */
  if (!name)
    {
      bind (name, x, scope, /*invisible=*/false, /*nested=*/false);
      return x;
    }

  /* First, see if there is another declaration with the same name in
     the current scope.  If there is, duplicate_decls may do all the
     work for us.  If duplicate_decls returns false, that indicates
     two incompatible decls in the same scope; we are to silently
     replace the old one (duplicate_decls has issued all appropriate
     diagnostics).  In particular, we should not consider possible
     duplicates in the external scope, or shadowing.  */
  b = I_SYMBOL_BINDING (name);
  if (b && B_IN_SCOPE (b, scope))
    {
      struct c_binding *b_ext, *b_use;
      tree type = TREE_TYPE (x);
      tree visdecl = b->decl;
      tree vistype = TREE_TYPE (visdecl);
      if (TREE_CODE (TREE_TYPE (x)) == ARRAY_TYPE
	  && COMPLETE_TYPE_P (TREE_TYPE (x)))
	b->inner_comp = false;
      b_use = b;
      b_ext = b;
      /* If this is an external linkage declaration, we should check
	 for compatibility with the type in the external scope before
	 setting the type at this scope based on the visible
	 information only.  */
      if (TREE_PUBLIC (x) && TREE_PUBLIC (visdecl))
	{
	  while (b_ext && !B_IN_EXTERNAL_SCOPE (b_ext))
	    b_ext = b_ext->shadowed;
	  if (b_ext)
	    {
	      b_use = b_ext;
	      if (b_use->type)
		TREE_TYPE (b_use->decl) = b_use->type;
	    }
	}
      if (duplicate_decls (x, b_use->decl))
	{
	  if (b_use != b)
	    {
	      /* Save the updated type in the external scope and
		 restore the proper type for this scope.  */
	      tree thistype;
	      if (comptypes (vistype, type))
		thistype = composite_type (vistype, type);
	      else
		thistype = TREE_TYPE (b_use->decl);
	      b_use->type = TREE_TYPE (b_use->decl);
	      if (TREE_CODE (b_use->decl) == FUNCTION_DECL
		  && DECL_BUILT_IN (b_use->decl))
		thistype
		  = build_type_attribute_variant (thistype,
						  TYPE_ATTRIBUTES
						  (b_use->type));
	      TREE_TYPE (b_use->decl) = thistype;
	    }
	  return b_use->decl;
	}
      else
	goto skip_external_and_shadow_checks;
    }

  /* All declarations with external linkage, and all external
     references, go in the external scope, no matter what scope is
     current.  However, the binding in that scope is ignored for
     purposes of normal name lookup.  A separate binding structure is
     created in the requested scope; this governs the normal
     visibility of the symbol.

     The binding in the externals scope is used exclusively for
     detecting duplicate declarations of the same object, no matter
     what scope they are in; this is what we do here.  (C99 6.2.7p2:
     All declarations that refer to the same object or function shall
     have compatible type; otherwise, the behavior is undefined.)  */
  if (DECL_EXTERNAL (x) || scope == file_scope)
    {
      tree type = TREE_TYPE (x);
      tree vistype = 0;
      tree visdecl = 0;
      bool type_saved = false;
      if (b && !B_IN_EXTERNAL_SCOPE (b)
	  && (TREE_CODE (b->decl) == FUNCTION_DECL
	      || TREE_CODE (b->decl) == VAR_DECL)
	  && DECL_FILE_SCOPE_P (b->decl))
	{
	  visdecl = b->decl;
	  vistype = TREE_TYPE (visdecl);
	}
      if (scope != file_scope
	  && !DECL_IN_SYSTEM_HEADER (x))
	warning (OPT_Wnested_externs, "nested extern declaration of %qD", x);

      while (b && !B_IN_EXTERNAL_SCOPE (b))
	{
	  /* If this decl might be modified, save its type.  This is
	     done here rather than when the decl is first bound
	     because the type may change after first binding, through
	     being completed or through attributes being added.  If we
	     encounter multiple such decls, only the first should have
	     its type saved; the others will already have had their
	     proper types saved and the types will not have changed as
	     their scopes will not have been re-entered.  */
	  if (DECL_P (b->decl) && DECL_FILE_SCOPE_P (b->decl) && !type_saved)
	    {
	      b->type = TREE_TYPE (b->decl);
	      type_saved = true;
	    }
	  if (B_IN_FILE_SCOPE (b)
	      && TREE_CODE (b->decl) == VAR_DECL
	      && TREE_STATIC (b->decl)
	      && TREE_CODE (TREE_TYPE (b->decl)) == ARRAY_TYPE
	      && !TYPE_DOMAIN (TREE_TYPE (b->decl))
	      && TREE_CODE (type) == ARRAY_TYPE
	      && TYPE_DOMAIN (type)
	      && TYPE_MAX_VALUE (TYPE_DOMAIN (type))
	      && !integer_zerop (TYPE_MAX_VALUE (TYPE_DOMAIN (type))))
	    {
	      /* Array type completed in inner scope, which should be
		 diagnosed if the completion does not have size 1 and
		 it does not get completed in the file scope.  */
	      b->inner_comp = true;
	    }
	  b = b->shadowed;
	}

      /* If a matching external declaration has been found, set its
	 type to the composite of all the types of that declaration.
	 After the consistency checks, it will be reset to the
	 composite of the visible types only.  */
      if (b && (TREE_PUBLIC (x) || same_translation_unit_p (x, b->decl))
	  && b->type)
	TREE_TYPE (b->decl) = b->type;

      /* The point of the same_translation_unit_p check here is,
	 we want to detect a duplicate decl for a construct like
	 foo() { extern bar(); } ... static bar();  but not if
	 they are in different translation units.  In any case,
	 the static does not go in the externals scope.  */
      if (b
	  && (TREE_PUBLIC (x) || same_translation_unit_p (x, b->decl))
	  && duplicate_decls (x, b->decl))
	{
	  tree thistype;
	  if (vistype)
	    {
	      if (comptypes (vistype, type))
		thistype = composite_type (vistype, type);
	      else
		thistype = TREE_TYPE (b->decl);
	    }
	  else
	    thistype = type;
	  b->type = TREE_TYPE (b->decl);
	  if (TREE_CODE (b->decl) == FUNCTION_DECL && DECL_BUILT_IN (b->decl))
	    thistype
	      = build_type_attribute_variant (thistype,
					      TYPE_ATTRIBUTES (b->type));
	  TREE_TYPE (b->decl) = thistype;
	  bind (name, b->decl, scope, /*invisible=*/false, /*nested=*/true);
	  return b->decl;
	}
      else if (TREE_PUBLIC (x))
	{
	  if (visdecl && !b && duplicate_decls (x, visdecl))
	    {
	      /* An external declaration at block scope referring to a
		 visible entity with internal linkage.  The composite
		 type will already be correct for this scope, so we
		 just need to fall through to make the declaration in
		 this scope.  */
	      nested = true;
	      x = visdecl;
	    }
	  else
	    {
	      bind (name, x, external_scope, /*invisible=*/true,
		    /*nested=*/false);
	      nested = true;
	    }
	}
    }

  if (TREE_CODE (x) != PARM_DECL)
    warn_if_shadowing (x);

 skip_external_and_shadow_checks:
  if (TREE_CODE (x) == TYPE_DECL)
    clone_underlying_type (x);

  bind (name, x, scope, /*invisible=*/false, nested);

  /* If x's type is incomplete because it's based on a
     structure or union which has not yet been fully declared,
     attach it to that structure or union type, so we can go
     back and complete the variable declaration later, if the
     structure or union gets fully declared.

     If the input is erroneous, we can have error_mark in the type
     slot (e.g. "f(void a, ...)") - that doesn't count as an
     incomplete type.  */
  if (TREE_TYPE (x) != error_mark_node
      && !COMPLETE_TYPE_P (TREE_TYPE (x)))
    {
      tree element = TREE_TYPE (x);

      while (TREE_CODE (element) == ARRAY_TYPE)
	element = TREE_TYPE (element);
      element = TYPE_MAIN_VARIANT (element);

      if ((TREE_CODE (element) == RECORD_TYPE
	   || TREE_CODE (element) == UNION_TYPE)
	  && (TREE_CODE (x) != TYPE_DECL
	      || TREE_CODE (TREE_TYPE (x)) == ARRAY_TYPE)
	  && !COMPLETE_TYPE_P (element))
	C_TYPE_INCOMPLETE_VARS (element)
	  = tree_cons (NULL_TREE, x, C_TYPE_INCOMPLETE_VARS (element));
    }
  return x;
}

/* Record X as belonging to file scope.
   This is used only internally by the Objective-C front end,
   and is limited to its needs.  duplicate_decls is not called;
   if there is any preexisting decl for this identifier, it is an ICE.  */

tree
pushdecl_top_level (tree x)
{
  tree name;
  bool nested = false;
  gcc_assert (TREE_CODE (x) == VAR_DECL || TREE_CODE (x) == CONST_DECL);

  name = DECL_NAME (x);

 gcc_assert (TREE_CODE (x) == CONST_DECL || !I_SYMBOL_BINDING (name));

  if (TREE_PUBLIC (x))
    {
      bind (name, x, external_scope, /*invisible=*/true, /*nested=*/false);
      nested = true;
    }
  if (file_scope)
    bind (name, x, file_scope, /*invisible=*/false, nested);

  return x;
}

static void
implicit_decl_warning (tree id, tree olddecl)
{
  void (*diag) (const char *, ...) ATTRIBUTE_GCC_CDIAG(1,2);
  switch (mesg_implicit_function_declaration)
    {
    case 0: return;
    case 1: diag = warning0; break;
    case 2: diag = error;   break;
    default: gcc_unreachable ();
    }

  diag (G_("implicit declaration of function %qE"), id);
  if (olddecl)
    locate_old_decl (olddecl, diag);
}

/* Generate an implicit declaration for identifier FUNCTIONID as a
   function of type int ().  */

tree
implicitly_declare (tree functionid)
{
  struct c_binding *b;
  tree decl = 0;
  tree asmspec_tree;

  for (b = I_SYMBOL_BINDING (functionid); b; b = b->shadowed)
    {
      if (B_IN_SCOPE (b, external_scope))
	{
	  decl = b->decl;
	  break;
	}
    }

  if (decl)
    {
      if (decl == error_mark_node)
	return decl;

      /* FIXME: Objective-C has weird not-really-builtin functions
	 which are supposed to be visible automatically.  They wind up
	 in the external scope because they're pushed before the file
	 scope gets created.  Catch this here and rebind them into the
	 file scope.  */
      if (!DECL_BUILT_IN (decl) && DECL_IS_BUILTIN (decl))
	{
	  bind (functionid, decl, file_scope,
		/*invisible=*/false, /*nested=*/true);
	  return decl;
	}
      else
	{
	  tree newtype = default_function_type;
	  if (b->type)
	    TREE_TYPE (decl) = b->type;
	  /* Implicit declaration of a function already declared
	     (somehow) in a different scope, or as a built-in.
	     If this is the first time this has happened, warn;
	     then recycle the old declaration but with the new type.  */
	  if (!C_DECL_IMPLICIT (decl))
	    {
	      implicit_decl_warning (functionid, decl);
	      C_DECL_IMPLICIT (decl) = 1;
	    }
	  if (DECL_BUILT_IN (decl))
	    {
	      newtype = build_type_attribute_variant (newtype,
						      TYPE_ATTRIBUTES
						      (TREE_TYPE (decl)));
	      if (!comptypes (newtype, TREE_TYPE (decl)))
		{
		  warning (0, "incompatible implicit declaration of built-in"
			   " function %qD", decl);
		  newtype = TREE_TYPE (decl);
		}
	    }
	  else
	    {
	      if (!comptypes (newtype, TREE_TYPE (decl)))
		{
		  error ("incompatible implicit declaration of function %qD",
			 decl);
		  locate_old_decl (decl, error);
		}
	    }
	  b->type = TREE_TYPE (decl);
	  TREE_TYPE (decl) = newtype;
	  bind (functionid, decl, current_scope,
		/*invisible=*/false, /*nested=*/true);
	  return decl;
	}
    }

  /* Not seen before.  */
  decl = build_decl (FUNCTION_DECL, functionid, default_function_type);
  DECL_EXTERNAL (decl) = 1;
  TREE_PUBLIC (decl) = 1;
  C_DECL_IMPLICIT (decl) = 1;
  implicit_decl_warning (functionid, 0);
  asmspec_tree = maybe_apply_renaming_pragma (decl, /*asmname=*/NULL);
  if (asmspec_tree)
    set_user_assembler_name (decl, TREE_STRING_POINTER (asmspec_tree));

  /* C89 says implicit declarations are in the innermost block.
     So we record the decl in the standard fashion.  */
  decl = pushdecl (decl);

  /* No need to call objc_check_decl here - it's a function type.  */
  rest_of_decl_compilation (decl, 0, 0);

  /* Write a record describing this implicit function declaration
     to the prototypes file (if requested).  */
  gen_aux_info_record (decl, 0, 1, 0);

  /* Possibly apply some default attributes to this implicit declaration.  */
  decl_attributes (&decl, NULL_TREE, 0);

  return decl;
}

/* Issue an error message for a reference to an undeclared variable
   ID, including a reference to a builtin outside of function-call
   context.  Establish a binding of the identifier to error_mark_node
   in an appropriate scope, which will suppress further errors for the
   same identifier.  The error message should be given location LOC.  */
void
undeclared_variable (tree id, location_t loc)
{
  static bool already = false;
  struct c_scope *scope;

  if (current_function_decl == 0)
    {
      error ("%H%qE undeclared here (not in a function)", &loc, id);
      scope = current_scope;
    }
  else
    {
      error ("%H%qE undeclared (first use in this function)", &loc, id);

      if (!already)
	{
	  error ("%H(Each undeclared identifier is reported only once", &loc);
	  error ("%Hfor each function it appears in.)", &loc);
	  already = true;
	}

      /* If we are parsing old-style parameter decls, current_function_decl
	 will be nonnull but current_function_scope will be null.  */
      scope = current_function_scope ? current_function_scope : current_scope;
    }
  bind (id, error_mark_node, scope, /*invisible=*/false, /*nested=*/false);
}

/* Subroutine of lookup_label, declare_label, define_label: construct a
   LABEL_DECL with all the proper frills.  */

static tree
make_label (tree name, location_t location)
{
  tree label = build_decl (LABEL_DECL, name, void_type_node);

  DECL_CONTEXT (label) = current_function_decl;
  DECL_MODE (label) = VOIDmode;
  DECL_SOURCE_LOCATION (label) = location;

  return label;
}

/* Get the LABEL_DECL corresponding to identifier NAME as a label.
   Create one if none exists so far for the current function.
   This is called when a label is used in a goto expression or
   has its address taken.  */

tree
lookup_label (tree name)
{
  tree label;

  if (current_function_decl == 0)
    {
      error ("label %qE referenced outside of any function", name);
      return 0;
    }

  /* Use a label already defined or ref'd with this name, but not if
     it is inherited from a containing function and wasn't declared
     using __label__.  */
  label = I_LABEL_DECL (name);
  if (label && (DECL_CONTEXT (label) == current_function_decl
		|| C_DECLARED_LABEL_FLAG (label)))
    {
      /* If the label has only been declared, update its apparent
	 location to point here, for better diagnostics if it
	 turns out not to have been defined.  */
      if (!TREE_USED (label))
	DECL_SOURCE_LOCATION (label) = input_location;
      return label;
    }

  /* No label binding for that identifier; make one.  */
  label = make_label (name, input_location);

  /* Ordinary labels go in the current function scope.  */
  bind (name, label, current_function_scope,
	/*invisible=*/false, /*nested=*/false);
  return label;
}

/* Make a label named NAME in the current function, shadowing silently
   any that may be inherited from containing functions or containing
   scopes.  This is called for __label__ declarations.  */

tree
declare_label (tree name)
{
  struct c_binding *b = I_LABEL_BINDING (name);
  tree label;

  /* Check to make sure that the label hasn't already been declared
     at this scope */
  if (b && B_IN_CURRENT_SCOPE (b))
    {
      error ("duplicate label declaration %qE", name);
      locate_old_decl (b->decl, error);

      /* Just use the previous declaration.  */
      return b->decl;
    }

  label = make_label (name, input_location);
  C_DECLARED_LABEL_FLAG (label) = 1;

  /* Declared labels go in the current scope.  */
  bind (name, label, current_scope,
	/*invisible=*/false, /*nested=*/false);
  return label;
}

/* Define a label, specifying the location in the source file.
   Return the LABEL_DECL node for the label, if the definition is valid.
   Otherwise return 0.  */

tree
define_label (location_t location, tree name)
{
  /* Find any preexisting label with this name.  It is an error
     if that label has already been defined in this function, or
     if there is a containing function with a declared label with
     the same name.  */
  tree label = I_LABEL_DECL (name);
  struct c_label_list *nlist_se, *nlist_vm;

  if (label
      && ((DECL_CONTEXT (label) == current_function_decl
	   && DECL_INITIAL (label) != 0)
	  || (DECL_CONTEXT (label) != current_function_decl
	      && C_DECLARED_LABEL_FLAG (label))))
    {
      error ("%Hduplicate label %qD", &location, label);
      locate_old_decl (label, error);
      return 0;
    }
  else if (label && DECL_CONTEXT (label) == current_function_decl)
    {
      /* The label has been used or declared already in this function,
	 but not defined.  Update its location to point to this
	 definition.  */
      if (C_DECL_UNDEFINABLE_STMT_EXPR (label))
	error ("%Jjump into statement expression", label);
      if (C_DECL_UNDEFINABLE_VM (label))
	error ("%Jjump into scope of identifier with variably modified type",
	       label);
      DECL_SOURCE_LOCATION (label) = location;
    }
  else
    {
      /* No label binding for that identifier; make one.  */
      label = make_label (name, location);

      /* Ordinary labels go in the current function scope.  */
      bind (name, label, current_function_scope,
	    /*invisible=*/false, /*nested=*/false);
    }

  if (!in_system_header && lookup_name (name))
    warning (OPT_Wtraditional, "%Htraditional C lacks a separate namespace "
	     "for labels, identifier %qE conflicts", &location, name);

  nlist_se = XOBNEW (&parser_obstack, struct c_label_list);
  nlist_se->next = label_context_stack_se->labels_def;
  nlist_se->label = label;
  label_context_stack_se->labels_def = nlist_se;

  nlist_vm = XOBNEW (&parser_obstack, struct c_label_list);
  nlist_vm->next = label_context_stack_vm->labels_def;
  nlist_vm->label = label;
  label_context_stack_vm->labels_def = nlist_vm;

  /* Mark label as having been defined.  */
  DECL_INITIAL (label) = error_mark_node;
  return label;
}

/* Given NAME, an IDENTIFIER_NODE,
   return the structure (or union or enum) definition for that name.
   If THISLEVEL_ONLY is nonzero, searches only the current_scope.
   CODE says which kind of type the caller wants;
   it is RECORD_TYPE or UNION_TYPE or ENUMERAL_TYPE.
   If the wrong kind of type is found, an error is reported.  */

static tree
lookup_tag (enum tree_code code, tree name, int thislevel_only)
{
  struct c_binding *b = I_TAG_BINDING (name);
  int thislevel = 0;

  if (!b || !b->decl)
    return 0;

  /* We only care about whether it's in this level if
     thislevel_only was set or it might be a type clash.  */
  if (thislevel_only || TREE_CODE (b->decl) != code)
    {
      /* For our purposes, a tag in the external scope is the same as
	 a tag in the file scope.  (Primarily relevant to Objective-C
	 and its builtin structure tags, which get pushed before the
	 file scope is created.)  */
      if (B_IN_CURRENT_SCOPE (b)
	  || (current_scope == file_scope && B_IN_EXTERNAL_SCOPE (b)))
	thislevel = 1;
    }

  if (thislevel_only && !thislevel)
    return 0;

  if (TREE_CODE (b->decl) != code)
    {
      /* Definition isn't the kind we were looking for.  */
      pending_invalid_xref = name;
      pending_invalid_xref_location = input_location;

      /* If in the same binding level as a declaration as a tag
	 of a different type, this must not be allowed to
	 shadow that tag, so give the error immediately.
	 (For example, "struct foo; union foo;" is invalid.)  */
      if (thislevel)
	pending_xref_error ();
    }
  return b->decl;
}

/* Print an error message now
   for a recent invalid struct, union or enum cross reference.
   We don't print them immediately because they are not invalid
   when used in the `struct foo;' construct for shadowing.  */

void
pending_xref_error (void)
{
  if (pending_invalid_xref != 0)
    error ("%H%qE defined as wrong kind of tag",
	   &pending_invalid_xref_location, pending_invalid_xref);
  pending_invalid_xref = 0;
}


/* Look up NAME in the current scope and its superiors
   in the namespace of variables, functions and typedefs.
   Return a ..._DECL node of some kind representing its definition,
   or return 0 if it is undefined.  */

tree
lookup_name (tree name)
{
  struct c_binding *b = I_SYMBOL_BINDING (name);
  if (b && !b->invisible)
    return b->decl;
  return 0;
}

/* Similar to `lookup_name' but look only at the indicated scope.  */

static tree
lookup_name_in_scope (tree name, struct c_scope *scope)
{
  struct c_binding *b;

  for (b = I_SYMBOL_BINDING (name); b; b = b->shadowed)
    if (B_IN_SCOPE (b, scope))
      return b->decl;
  return 0;
}

/* Create the predefined scalar types of C,
   and some nodes representing standard constants (0, 1, (void *) 0).
   Initialize the global scope.
   Make definitions for built-in primitive functions.  */

void
c_init_decl_processing (void)
{
  location_t save_loc = input_location;

  /* Initialize reserved words for parser.  */
  c_parse_init ();

  current_function_decl = 0;

  gcc_obstack_init (&parser_obstack);

  /* Make the externals scope.  */
  push_scope ();
  external_scope = current_scope;

  /* Declarations from c_common_nodes_and_builtins must not be associated
     with this input file, lest we get differences between using and not
     using preprocessed headers.  */
#ifdef USE_MAPPED_LOCATION
  input_location = BUILTINS_LOCATION;
#else
  input_location.file = "<built-in>";
  input_location.line = 0;
#endif

  build_common_tree_nodes (flag_signed_char, false);

  c_common_nodes_and_builtins ();

  /* In C, comparisons and TRUTH_* expressions have type int.  */
  truthvalue_type_node = integer_type_node;
  truthvalue_true_node = integer_one_node;
  truthvalue_false_node = integer_zero_node;

  /* Even in C99, which has a real boolean type.  */
  pushdecl (build_decl (TYPE_DECL, get_identifier ("_Bool"),
			boolean_type_node));

  input_location = save_loc;

  pedantic_lvalues = true;

  make_fname_decl = c_make_fname_decl;
  start_fname_decls ();
}

/* Create the VAR_DECL for __FUNCTION__ etc. ID is the name to give the
   decl, NAME is the initialization string and TYPE_DEP indicates whether
   NAME depended on the type of the function.  As we don't yet implement
   delayed emission of static data, we mark the decl as emitted
   so it is not placed in the output.  Anything using it must therefore pull
   out the STRING_CST initializer directly.  FIXME.  */

static tree
c_make_fname_decl (tree id, int type_dep)
{
  const char *name = fname_as_string (type_dep);
  tree decl, type, init;
  size_t length = strlen (name);

  type = build_array_type (char_type_node,
			   build_index_type (size_int (length)));
  type = c_build_qualified_type (type, TYPE_QUAL_CONST);

  decl = build_decl (VAR_DECL, id, type);

  TREE_STATIC (decl) = 1;
  TREE_READONLY (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;

  init = build_string (length + 1, name);
  free ((char *) name);
  TREE_TYPE (init) = type;
  DECL_INITIAL (decl) = init;

  TREE_USED (decl) = 1;

  if (current_function_decl
      /* For invalid programs like this:
        
         void foo()
         const char* p = __FUNCTION__;
        
	 the __FUNCTION__ is believed to appear in K&R style function
	 parameter declarator.  In that case we still don't have
	 function_scope.  */
      && (!errorcount || current_function_scope))
    {
      DECL_CONTEXT (decl) = current_function_decl;
      bind (id, decl, current_function_scope,
	    /*invisible=*/false, /*nested=*/false);
    }

  finish_decl (decl, init, NULL_TREE);

  return decl;
}

/* Return a definition for a builtin function named NAME and whose data type
   is TYPE.  TYPE should be a function type with argument types.
   FUNCTION_CODE tells later passes how to compile calls to this function.
   See tree.h for its possible values.

   If LIBRARY_NAME is nonzero, use that for DECL_ASSEMBLER_NAME,
   the name to be called if we can't opencode the function.  If
   ATTRS is nonzero, use that for the function's attribute list.  */

tree
builtin_function (const char *name, tree type, int function_code,
		  enum built_in_class cl, const char *library_name,
		  tree attrs)
{
  tree id = get_identifier (name);
  tree decl = build_decl (FUNCTION_DECL, id, type);
  TREE_PUBLIC (decl) = 1;
  DECL_EXTERNAL (decl) = 1;
  DECL_LANG_SPECIFIC (decl) = GGC_CNEW (struct lang_decl);
  DECL_BUILT_IN_CLASS (decl) = cl;
  DECL_FUNCTION_CODE (decl) = function_code;
  C_DECL_BUILTIN_PROTOTYPE (decl) = (TYPE_ARG_TYPES (type) != 0);
  if (library_name)
    SET_DECL_ASSEMBLER_NAME (decl, get_identifier (library_name));

  /* Should never be called on a symbol with a preexisting meaning.  */
  gcc_assert (!I_SYMBOL_BINDING (id));

  bind (id, decl, external_scope, /*invisible=*/true, /*nested=*/false);

  /* Builtins in the implementation namespace are made visible without
     needing to be explicitly declared.  See push_file_scope.  */
  if (name[0] == '_' && (name[1] == '_' || ISUPPER (name[1])))
    {
      TREE_CHAIN (decl) = visible_builtins;
      visible_builtins = decl;
    }

  /* Possibly apply some default attributes to this built-in function.  */
  if (attrs)
    decl_attributes (&decl, attrs, ATTR_FLAG_BUILT_IN);
  else
    decl_attributes (&decl, NULL_TREE, 0);

  return decl;
}

/* Called when a declaration is seen that contains no names to declare.
   If its type is a reference to a structure, union or enum inherited
   from a containing scope, shadow that tag name for the current scope
   with a forward reference.
   If its type defines a new named structure or union
   or defines an enum, it is valid but we need not do anything here.
   Otherwise, it is an error.  */

void
shadow_tag (const struct c_declspecs *declspecs)
{
  shadow_tag_warned (declspecs, 0);
}

/* WARNED is 1 if we have done a pedwarn, 2 if we have done a warning,
   but no pedwarn.  */
void
shadow_tag_warned (const struct c_declspecs *declspecs, int warned)
{
  bool found_tag = false;

  if (declspecs->type && !declspecs->default_int_p && !declspecs->typedef_p)
    {
      tree value = declspecs->type;
      enum tree_code code = TREE_CODE (value);

      if (code == RECORD_TYPE || code == UNION_TYPE || code == ENUMERAL_TYPE)
	/* Used to test also that TYPE_SIZE (value) != 0.
	   That caused warning for `struct foo;' at top level in the file.  */
	{
	  tree name = TYPE_NAME (value);
	  tree t;

	  found_tag = true;

	  if (name == 0)
	    {
	      if (warned != 1 && code != ENUMERAL_TYPE)
		/* Empty unnamed enum OK */
		{
		  pedwarn ("unnamed struct/union that defines no instances");
		  warned = 1;
		}
	    }
	  else if (!declspecs->tag_defined_p
		   && declspecs->storage_class != csc_none)
	    {
	      if (warned != 1)
		pedwarn ("empty declaration with storage class specifier "
			 "does not redeclare tag");
	      warned = 1;
	      pending_xref_error ();
	    }
	  else if (!declspecs->tag_defined_p
		   && (declspecs->const_p
		       || declspecs->volatile_p
		       || declspecs->restrict_p))
	    {
	      if (warned != 1)
		pedwarn ("empty declaration with type qualifier "
			 "does not redeclare tag");
	      warned = 1;
	      pending_xref_error ();
	    }
	  else
	    {
	      pending_invalid_xref = 0;
	      t = lookup_tag (code, name, 1);

	      if (t == 0)
		{
		  t = make_node (code);
		  pushtag (name, t);
		}
	    }
	}
      else
	{
	  if (warned != 1 && !in_system_header)
	    {
	      pedwarn ("useless type name in empty declaration");
	      warned = 1;
	    }
	}
    }
  else if (warned != 1 && !in_system_header && declspecs->typedef_p)
    {
      pedwarn ("useless type name in empty declaration");
      warned = 1;
    }

  pending_invalid_xref = 0;

  if (declspecs->inline_p)
    {
      error ("%<inline%> in empty declaration");
      warned = 1;
    }

  if (current_scope == file_scope && declspecs->storage_class == csc_auto)
    {
      error ("%<auto%> in file-scope empty declaration");
      warned = 1;
    }

  if (current_scope == file_scope && declspecs->storage_class == csc_register)
    {
      error ("%<register%> in file-scope empty declaration");
      warned = 1;
    }

  if (!warned && !in_system_header && declspecs->storage_class != csc_none)
    {
      warning (0, "useless storage class specifier in empty declaration");
      warned = 2;
    }

  if (!warned && !in_system_header && declspecs->thread_p)
    {
      warning (0, "useless %<__thread%> in empty declaration");
      warned = 2;
    }

  if (!warned && !in_system_header && (declspecs->const_p
				       || declspecs->volatile_p
				       || declspecs->restrict_p))
    {
      warning (0, "useless type qualifier in empty declaration");
      warned = 2;
    }

  if (warned != 1)
    {
      if (!found_tag)
	pedwarn ("empty declaration");
    }
}


/* Return the qualifiers from SPECS as a bitwise OR of TYPE_QUAL_*
   bits.  SPECS represents declaration specifiers that the grammar
   only permits to contain type qualifiers and attributes.  */

int
quals_from_declspecs (const struct c_declspecs *specs)
{
  int quals = ((specs->const_p ? TYPE_QUAL_CONST : 0)
	       | (specs->volatile_p ? TYPE_QUAL_VOLATILE : 0)
	       | (specs->restrict_p ? TYPE_QUAL_RESTRICT : 0));
  gcc_assert (!specs->type
	      && !specs->decl_attr
	      && specs->typespec_word == cts_none
	      && specs->storage_class == csc_none
	      && !specs->typedef_p
	      && !specs->explicit_signed_p
	      && !specs->deprecated_p
	      && !specs->long_p
	      && !specs->long_long_p
	      && !specs->short_p
	      && !specs->signed_p
	      && !specs->unsigned_p
	      && !specs->complex_p
	      && !specs->inline_p
	      && !specs->thread_p);
  return quals;
}

/* Construct an array declarator.  EXPR is the expression inside [],
   or NULL_TREE.  QUALS are the type qualifiers inside the [] (to be
   applied to the pointer to which a parameter array is converted).
   STATIC_P is true if "static" is inside the [], false otherwise.
   VLA_UNSPEC_P is true if the array is [*], a VLA of unspecified
   length which is nevertheless a complete type, false otherwise.  The
   field for the contained declarator is left to be filled in by
   set_array_declarator_inner.  */

struct c_declarator *
build_array_declarator (tree expr, struct c_declspecs *quals, bool static_p,
			bool vla_unspec_p)
{
  struct c_declarator *declarator = XOBNEW (&parser_obstack,
					    struct c_declarator);
  declarator->kind = cdk_array;
  declarator->declarator = 0;
  declarator->u.array.dimen = expr;
  if (quals)
    {
      declarator->u.array.attrs = quals->attrs;
      declarator->u.array.quals = quals_from_declspecs (quals);
    }
  else
    {
      declarator->u.array.attrs = NULL_TREE;
      declarator->u.array.quals = 0;
    }
  declarator->u.array.static_p = static_p;
  declarator->u.array.vla_unspec_p = vla_unspec_p;
  if (pedantic && !flag_isoc99)
    {
      if (static_p || quals != NULL)
	pedwarn ("ISO C90 does not support %<static%> or type "
		 "qualifiers in parameter array declarators");
      if (vla_unspec_p)
	pedwarn ("ISO C90 does not support %<[*]%> array declarators");
    }
  if (vla_unspec_p)
    {
      if (!current_scope->parm_flag)
	{
	  /* C99 6.7.5.2p4 */
	  error ("%<[*]%> not allowed in other than function prototype scope");
	  declarator->u.array.vla_unspec_p = false;
	  return NULL;
	}
      current_scope->had_vla_unspec = true;
    }
  return declarator;
}

/* Set the contained declarator of an array declarator.  DECL is the
   declarator, as constructed by build_array_declarator; INNER is what
   appears on the left of the []. */

struct c_declarator *
set_array_declarator_inner (struct c_declarator *decl,
			    struct c_declarator *inner,
			    bool abstract_p __attribute__ ((__unused__)))
{
  decl->declarator = inner;
  return decl;
}

/* INIT is a constructor that forms DECL's initializer.  If the final
   element initializes a flexible array field, add the size of that
   initializer to DECL's size.  */

static void
add_flexible_array_elts_to_size (tree decl, tree init)
{
  tree elt, type;

  if (VEC_empty (constructor_elt, CONSTRUCTOR_ELTS (init)))
    return;

  elt = VEC_last (constructor_elt, CONSTRUCTOR_ELTS (init))->value;
  type = TREE_TYPE (elt);
  if (TREE_CODE (type) == ARRAY_TYPE
      && TYPE_SIZE (type) == NULL_TREE
      && TYPE_DOMAIN (type) != NULL_TREE
      && TYPE_MAX_VALUE (TYPE_DOMAIN (type)) == NULL_TREE)
    {
      complete_array_type (&type, elt, false);
      DECL_SIZE (decl)
	= size_binop (PLUS_EXPR, DECL_SIZE (decl), TYPE_SIZE (type));
      DECL_SIZE_UNIT (decl)
	= size_binop (PLUS_EXPR, DECL_SIZE_UNIT (decl), TYPE_SIZE_UNIT (type));
    }
}

/* APPLE LOCAL begin blocks 6339747 */
/* Decode a block literal type, such as "int **", returning a ...FUNCTION_DECL node.  */

tree
grokblockdecl (struct c_declspecs *specs, struct c_declarator *declarator)
{
  tree decl;
  tree attrs = specs->attrs;

  specs->attrs = NULL_TREE;

  decl = grokdeclarator (declarator, specs, BLOCKDEF,
			  false, NULL);

  /* Apply attributes.  */
  decl_attributes (&decl, attrs, 0);

  return decl;
}
/* APPLE LOCAL end blocks 6339747 */

/* Decode a "typename", such as "int **", returning a ..._TYPE node.  */

tree
groktypename (struct c_type_name *type_name)
{
  tree type;
  tree attrs = type_name->specs->attrs;

  type_name->specs->attrs = NULL_TREE;

  type = grokdeclarator (type_name->declarator, type_name->specs, TYPENAME,
			 false, NULL);

  /* Apply attributes.  */
  decl_attributes (&type, attrs, 0);

  return type;
}

/* Decode a declarator in an ordinary declaration or data definition.
   This is called as soon as the type information and variable name
   have been parsed, before parsing the initializer if any.
   Here we create the ..._DECL node, fill in its type,
   and put it on the list of decls for the current context.
   The ..._DECL node is returned as the value.

   Exception: for arrays where the length is not specified,
   the type is left null, to be filled in by `finish_decl'.

   Function definitions do not come here; they go to start_function
   instead.  However, external and forward declarations of functions
   do go through here.  Structure field declarations are done by
   grokfield and not through here.  */

tree
start_decl (struct c_declarator *declarator, struct c_declspecs *declspecs,
	    bool initialized, tree attributes)
{
  tree decl;
  tree tem;

  /* An object declared as __attribute__((deprecated)) suppresses
     warnings of uses of other deprecated items.  */
  /* APPLE LOCAL begin "unavailable" attribute (radar 2809697) */
  /* An object declared as __attribute__((unavailable)) suppresses
     any reports of being declared with unavailable or deprecated
     items.  An object declared as __attribute__((deprecated))
     suppresses warnings of uses of other deprecated items.  */
#ifdef A_LESS_INEFFICENT_WAY /* which I really don't want to do!  */
  if (lookup_attribute ("deprecated", attributes))
    deprecated_state = DEPRECATED_SUPPRESS;
  else if (lookup_attribute ("unavailable", attributes))
    deprecated_state = DEPRECATED_UNAVAILABLE_SUPPRESS;
#else /* a more efficient way doing what lookup_attribute would do */
  tree a;

  for (a = attributes; a; a = TREE_CHAIN (a))
    {
      tree name = TREE_PURPOSE (a);
      if (TREE_CODE (name) == IDENTIFIER_NODE)
        if (is_attribute_p ("deprecated", name))
	  {
	    deprecated_state = DEPRECATED_SUPPRESS;
	    break;
	  }
        if (is_attribute_p ("unavailable", name))
	  {
	    deprecated_state = DEPRECATED_UNAVAILABLE_SUPPRESS;
	    break;
	  }
    }
#endif
  /* APPLE LOCAL end "unavailable" attribute (radar 2809697) */

  decl = grokdeclarator (declarator, declspecs,
			 NORMAL, initialized, NULL);
  if (!decl)
    return 0;

  deprecated_state = DEPRECATED_NORMAL;

  if (warn_main > 0 && TREE_CODE (decl) != FUNCTION_DECL
      && MAIN_NAME_P (DECL_NAME (decl)))
    warning (OPT_Wmain, "%q+D is usually a function", decl);

  if (initialized)
    /* Is it valid for this decl to have an initializer at all?
       If not, set INITIALIZED to zero, which will indirectly
       tell 'finish_decl' to ignore the initializer once it is parsed.  */
    switch (TREE_CODE (decl))
      {
      case TYPE_DECL:
	error ("typedef %qD is initialized (use __typeof__ instead)", decl);
	initialized = 0;
	break;

      case FUNCTION_DECL:
	error ("function %qD is initialized like a variable", decl);
	initialized = 0;
	break;

      case PARM_DECL:
	/* DECL_INITIAL in a PARM_DECL is really DECL_ARG_TYPE.  */
	error ("parameter %qD is initialized", decl);
	initialized = 0;
	break;

      default:
	/* Don't allow initializations for incomplete types except for
	   arrays which might be completed by the initialization.  */

	/* This can happen if the array size is an undefined macro.
	   We already gave a warning, so we don't need another one.  */
	if (TREE_TYPE (decl) == error_mark_node)
	  initialized = 0;
	else if (COMPLETE_TYPE_P (TREE_TYPE (decl)))
	  {
	    /* A complete type is ok if size is fixed.  */

	    if (TREE_CODE (TYPE_SIZE (TREE_TYPE (decl))) != INTEGER_CST
		|| C_DECL_VARIABLE_SIZE (decl))
	      {
		error ("variable-sized object may not be initialized");
		initialized = 0;
	      }
	  }
	else if (TREE_CODE (TREE_TYPE (decl)) != ARRAY_TYPE)
	  {
	    error ("variable %qD has initializer but incomplete type", decl);
	    initialized = 0;
	  }
	else if (C_DECL_VARIABLE_SIZE (decl))
	  {
	    /* Although C99 is unclear about whether incomplete arrays
	       of VLAs themselves count as VLAs, it does not make
	       sense to permit them to be initialized given that
	       ordinary VLAs may not be initialized.  */
	    error ("variable-sized object may not be initialized");
	    initialized = 0;
	  }
      }

  if (initialized)
    {
      if (current_scope == file_scope)
	TREE_STATIC (decl) = 1;

      /* Tell 'pushdecl' this is an initialized decl
	 even though we don't yet have the initializer expression.
	 Also tell 'finish_decl' it may store the real initializer.  */
      DECL_INITIAL (decl) = error_mark_node;
    }

  /* If this is a function declaration, write a record describing it to the
     prototypes file (if requested).  */

  if (TREE_CODE (decl) == FUNCTION_DECL)
    gen_aux_info_record (decl, 0, 0, TYPE_ARG_TYPES (TREE_TYPE (decl)) != 0);

  /* ANSI specifies that a tentative definition which is not merged with
     a non-tentative definition behaves exactly like a definition with an
     initializer equal to zero.  (Section 3.7.2)

     -fno-common gives strict ANSI behavior, though this tends to break
     a large body of code that grew up without this rule.

     Thread-local variables are never common, since there's no entrenched
     body of code to break, and it allows more efficient variable references
     in the presence of dynamic linking.  */

  if (TREE_CODE (decl) == VAR_DECL
      && !initialized
      && TREE_PUBLIC (decl)
      && !DECL_THREAD_LOCAL_P (decl)
      && !flag_no_common)
    DECL_COMMON (decl) = 1;

  /* Set attributes here so if duplicate decl, will have proper attributes.  */
  decl_attributes (&decl, attributes, 0);

  /* Handle gnu_inline attribute.  */
  if (declspecs->inline_p
      && !flag_gnu89_inline
      && TREE_CODE (decl) == FUNCTION_DECL
      && lookup_attribute ("gnu_inline", DECL_ATTRIBUTES (decl)))
    {
      if (declspecs->storage_class == csc_auto && current_scope != file_scope)
	;
      else if (declspecs->storage_class != csc_static)
	DECL_EXTERNAL (decl) = !DECL_EXTERNAL (decl);
    }

  if (TREE_CODE (decl) == FUNCTION_DECL
      && targetm.calls.promote_prototypes (TREE_TYPE (decl)))
    {
      struct c_declarator *ce = declarator;

      if (ce->kind == cdk_pointer)
	ce = declarator->declarator;
      if (ce->kind == cdk_function)
	{
	  tree args = ce->u.arg_info->parms;
	  for (; args; args = TREE_CHAIN (args))
	    {
	      tree type = TREE_TYPE (args);
	      if (type && INTEGRAL_TYPE_P (type)
		  && TYPE_PRECISION (type) < TYPE_PRECISION (integer_type_node))
		DECL_ARG_TYPE (args) = integer_type_node;
	    }
	}
    }

  if (TREE_CODE (decl) == FUNCTION_DECL
      && DECL_DECLARED_INLINE_P (decl)
      && DECL_UNINLINABLE (decl)
      && lookup_attribute ("noinline", DECL_ATTRIBUTES (decl)))
    warning (OPT_Wattributes, "inline function %q+D given attribute noinline",
	     decl);

  /* C99 6.7.4p3: An inline definition of a function with external
     linkage shall not contain a definition of a modifiable object
     with static storage duration...  */
  if (TREE_CODE (decl) == VAR_DECL
      && current_scope != file_scope
      && TREE_STATIC (decl)
      && !TREE_READONLY (decl)
      && DECL_DECLARED_INLINE_P (current_function_decl)
      && DECL_EXTERNAL (current_function_decl))
    pedwarn ("%q+D is static but declared in inline function %qD "
	     "which is not static", decl, current_function_decl);

  /* Add this decl to the current scope.
     TEM may equal DECL or it may be a previous decl of the same name.  */
  tem = pushdecl (decl);

  if (initialized && DECL_EXTERNAL (tem))
    {
      DECL_EXTERNAL (tem) = 0;
      TREE_STATIC (tem) = 1;
    }

  return tem;
}

/* Initialize EH if not initialized yet and exceptions are enabled.  */

void
c_maybe_initialize_eh (void)
{
  if (!flag_exceptions || c_eh_initialized_p)
    return;

  c_eh_initialized_p = true;
  eh_personality_libfunc
    = init_one_libfunc (USING_SJLJ_EXCEPTIONS
			? "__gcc_personality_sj0"
			: "__gcc_personality_v0");
  default_init_unwind_resume_libfunc ();
  using_eh_for_cleanups ();
}

/* APPLE LOCAL begin radar 5932809 - copyable byref blocks (C++ cr) */
#define BLOCK_ALIGN_MAX 18
static tree block_byref_id_object_copy[BLOCK_BYREF_CURRENT_MAX*(BLOCK_ALIGN_MAX+1)];
static tree block_byref_id_object_dispose[BLOCK_BYREF_CURRENT_MAX*(BLOCK_ALIGN_MAX+1)];

/**
 This routine builds:

 void __Block_byref_id_object_copy(struct Block_byref_id_object *dst,
				   struct Block_byref_id_object *src) {
   _Block_object_assign (&_dest->object, _src->object, BLOCK_FIELD_IS_OBJECT[|BLOCK_FIELD_IS_WEAK]) // objects
   _Block_object_assign(&_dest->object, _src->object, BLOCK_FIELD_IS_BLOCK[|BLOCK_FIELD_IS_WEAK])  // blocks
 }  */
static void
synth_block_byref_id_object_copy_func (int flag, int kind)
{
  tree stmt, fnbody;
  tree dst_arg, src_arg;
  tree dst_obj, src_obj;
  struct c_arg_info * arg_info;
  tree call_exp;
  gcc_assert (block_byref_id_object_copy[kind]);
  /* Set up: (void* _dest, void*_src) parameters. */
  dst_arg = build_decl (PARM_DECL, get_identifier ("_dst"),
			 ptr_type_node);
  TREE_USED (dst_arg) = 1;
  DECL_ARG_TYPE (dst_arg) = ptr_type_node;
  src_arg = build_decl (PARM_DECL, get_identifier ("_src"),
			 ptr_type_node);
  TREE_USED (src_arg) = 1;
  DECL_ARG_TYPE (src_arg) = ptr_type_node;
  arg_info = xcalloc (1, sizeof (struct c_arg_info));
  TREE_CHAIN (dst_arg) = src_arg;
  arg_info->parms = dst_arg;
  arg_info->types = tree_cons (NULL_TREE, ptr_type_node,
				tree_cons (NULL_TREE,
					   ptr_type_node,
					   NULL_TREE));
  /* function header synthesis. */
  push_function_context ();
  start_block_helper_function (block_byref_id_object_copy[kind]);
  store_parm_decls_from (arg_info);

  /* Body of the function. */
  stmt = c_begin_compound_stmt (true);
  /* Build dst->object */
  dst_obj = build_indirect_object_id_exp (dst_arg);


  /* src_obj is: _src->object. */
  src_obj = build_indirect_object_id_exp (src_arg);

  /* APPLE LOCAL begin radar 6180456 */
  /* _Block_object_assign (&_dest->object, _src->object, BLOCK_FIELD_IS_OBJECT) or :
     _Block_object_assign (&_dest->object, _src->object, BLOCK_FIELD_IS_BLOCK) */
  /* APPLE LOCAL begin radar 6573923 */
  /* Also add the new flag when calling _Block_object_dispose
     from byref dispose helper. */
  flag |= BLOCK_BYREF_CALLER;
 /* APPLE LOCAL end radar 6573923 */
  call_exp = build_block_object_assign_call_exp (build_fold_addr_expr (dst_obj), src_obj, flag);
  add_stmt (call_exp);
  /* APPLE LOCAL end radar 6180456 */

  fnbody = c_end_compound_stmt (stmt, true);
  add_stmt (fnbody);
  finish_function ();
  pop_function_context ();
  free (arg_info);
}

/**
  This routine builds:

  void __Block_byref_id_object_dispose(struct Block_byref_id_object *_src) {
    _Block_object_dispose(_src->object, BLOCK_FIELD_IS_OBJECT[|BLOCK_FIELD_IS_WEAK]) // object
    _Block_object_dispose(_src->object, BLOCK_FIELD_IS_BLOCK[|BLOCK_FIELD_IS_WEAK]) // block
  }  */
static void
synth_block_byref_id_object_dispose_func (int flag, int kind)
{
  tree stmt, fnbody;
  tree src_arg, src_obj, rel_exp;
  struct c_arg_info * arg_info;
  gcc_assert (block_byref_id_object_dispose[kind]);
  /* Set up: (void *_src) parameter. */
  src_arg = build_decl (PARM_DECL, get_identifier ("_src"),
			 ptr_type_node);
  TREE_USED (src_arg) = 1;
  DECL_ARG_TYPE (src_arg) = ptr_type_node;
  arg_info = xcalloc (1, sizeof (struct c_arg_info));
  arg_info->parms = src_arg;
  arg_info->types = tree_cons (NULL_TREE, ptr_type_node,
				NULL_TREE);
  /* function header synthesis. */
  push_function_context ();
  start_block_helper_function (block_byref_id_object_dispose[kind]);
  store_parm_decls_from (arg_info);

  /* Body of the function. */
  stmt = c_begin_compound_stmt (true);
  src_obj = build_indirect_object_id_exp (src_arg);

  /* APPLE LOCAL begin radar 6180456 */
  /* _Block_object_dispose(_src->object, BLOCK_FIELD_IS_OBJECT) : or
     _Block_object_dispose(_src->object, BLOCK_FIELD_IS_BLOCK) */
  /* APPLE LOCAL begin radar 6573923 */
  /* Also add the new flag when calling _Block_object_dispose
     from byref dispose helper. */
  flag |= BLOCK_BYREF_CALLER;
  /* APPLE LOCAL end radar 6573923 */
  rel_exp = build_block_object_dispose_call_exp (src_obj, flag);
  /* APPLE LOCAL end radar 6180456 */
  add_stmt (rel_exp);

  fnbody = c_end_compound_stmt (stmt, true);
  add_stmt (fnbody);
  finish_function ();
  pop_function_context ();
  free (arg_info);
}

/* new_block_byref_decl - This routine changes a 'typex x' declared variable into:

  struct __Block_byref_x {
    // APPLE LOCAL radar 6244520
    void *__isa;			// NULL for everything except __weak pointers
    struct Block_byref_x *__forwarding;
    int32_t __flags;
    int32_t __size;
    void *__ByrefKeepFuncPtr;    // Only if variable is __block ObjC object
    void *__ByrefDestroyFuncPtr; // Only if variable is __block ObjC object
    typex x;
  } x;
*/

static tree
new_block_byref_decl (tree decl)
{
  static int unique_count;
  /* APPLE LOCAL radar 5847976 */
  int save_flag_objc_gc;
  tree Block_byref_type;
  tree field_decl_chain, field_decl;
  const char *prefix = "__Block_byref_";
  char *string = alloca (strlen (IDENTIFIER_POINTER (DECL_NAME (decl))) +
			  strlen (prefix) + 8 /* to hold the count */);

  sprintf (string, "%s%d_%s", prefix, ++unique_count,
	    IDENTIFIER_POINTER (DECL_NAME (decl)));

  push_to_top_level ();
  Block_byref_type = start_struct (RECORD_TYPE, get_identifier (string));

  /* APPLE LOCAL begin radar 6244520 */
  /* void *__isa; */
  field_decl = build_decl (FIELD_DECL, get_identifier ("__isa"), ptr_type_node);
  field_decl_chain = field_decl;
  /* APPLE LOCAL end radar 6244520 */

  /* struct Block_byref_x *__forwarding; */
  field_decl = build_decl (FIELD_DECL, get_identifier ("__forwarding"),
			    build_pointer_type (Block_byref_type));
  /* APPLE LOCAL radar 6244520 */
  chainon (field_decl_chain, field_decl);

  /* int32_t __flags; */
  field_decl = build_decl (FIELD_DECL, get_identifier ("__flags"),
			    unsigned_type_node);
  chainon (field_decl_chain, field_decl);

  /* int32_t __size; */
  field_decl = build_decl (FIELD_DECL, get_identifier ("__size"),
			    unsigned_type_node);
  chainon (field_decl_chain, field_decl);

  if (COPYABLE_BYREF_LOCAL_NONPOD (decl))
    {
      /* void *__ByrefKeepFuncPtr; */
      field_decl = build_decl (FIELD_DECL, get_identifier ("__ByrefKeepFuncPtr"),
			       ptr_type_node);
      chainon (field_decl_chain, field_decl);

      /* void *__ByrefDestroyFuncPtr; */
      field_decl = build_decl (FIELD_DECL, get_identifier ("__ByrefDestroyFuncPtr"),
			       ptr_type_node);
      chainon (field_decl_chain, field_decl);
  }

  /* typex x; */
  field_decl = build_decl (FIELD_DECL, DECL_NAME (decl), TREE_TYPE (decl));
  chainon (field_decl_chain, field_decl);

  pop_from_top_level ();
  /* APPLE LOCAL begin radar 5847976 */
  /* Hack so we don't issue warning on a field_decl having __weak attribute */
  save_flag_objc_gc = flag_objc_gc;
  flag_objc_gc = 0;
  finish_struct (Block_byref_type, field_decl_chain, NULL_TREE);
  flag_objc_gc = save_flag_objc_gc;
  /* APPLE LOCAL end radar 5847976 */

  TREE_TYPE (decl) = Block_byref_type;
  /* Force layout_decl to recompute these fields. */
  DECL_SIZE (decl) = DECL_SIZE_UNIT (decl) = 0;
  layout_decl (decl, 0);
  return decl;
}

/* init_byref_decl - This routine builds the initializer for the __Block_byref_x
   type in the form of:
   { NULL, &x, 0, sizeof(struct __Block_byref_x), initializer-expr};

   or:
   { NULL, &x, 0, sizeof(struct __Block_byref_x)};
   when INIT is NULL_TREE

   For __block ObjC objects, it also adds "byref_keep" and "byref_destroy"
   Funtion pointers. So the most general initializers would be:

   { NULL, &x, 0, sizeof(struct __Block_byref_x), &byref_keep, &byref_destroy,
     &initializer-expr};
*/
static tree
/* APPLE LOCAL radar 5847976 */
init_byref_decl (tree decl, tree init, int flag)
{
  tree initlist;
  tree block_byref_type = TREE_TYPE (decl);
  int size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT (block_byref_type));
  unsigned flags = 0;
  tree fields;

  if (COPYABLE_BYREF_LOCAL_NONPOD (decl))
    flags = BLOCK_HAS_COPY_DISPOSE;

  fields = TYPE_FIELDS (block_byref_type);
  /* APPLE LOCAL begin radar 6244520 */
  /* APPLE LOCAL begin radar 5847976 */
  initlist = tree_cons (fields, fold_convert (ptr_type_node, ((flag & BLOCK_FIELD_IS_WEAK) != 0) ? integer_one_node 
									        : integer_zero_node), 0);
  /* APPLE LOCAL end radar 5847976 */
  fields = TREE_CHAIN (fields);

  initlist = tree_cons (fields,
			 build_unary_op (ADDR_EXPR, decl, 0), initlist);
   /* APPLE LOCAL end radar 6244520 */
  fields = TREE_CHAIN (fields);

  initlist = tree_cons (fields, build_int_cst (TREE_TYPE (fields), flags),
			 initlist);
  fields = TREE_CHAIN (fields);
  initlist = tree_cons (fields, build_int_cst (TREE_TYPE (fields), size),
			 initlist);
  fields = TREE_CHAIN (fields);

  if (COPYABLE_BYREF_LOCAL_NONPOD (decl))
    {
      char name[64];
      int align = exact_log2 ((DECL_ALIGN (decl)+TYPE_ALIGN (ptr_type_node)-1) / TYPE_ALIGN (ptr_type_node));
      int kind;
      if (align == -1 || align > BLOCK_ALIGN_MAX) {
	error ("invalid alignment for __block variable");
	kind = 0;
      } else
	kind = align*BLOCK_BYREF_CURRENT_MAX + flag;
      /* Add &__Block_byref_id_object_copy, &__Block_byref_id_object_dispose
	 initializers. */
      if (!block_byref_id_object_copy[kind])
	{
	  /* Build a void __Block_byref_id_object_copy(void*, void*) type. */
	  tree func_type =
	    build_function_type (void_type_node,
				 tree_cons (NULL_TREE, ptr_type_node,
					    tree_cons (NULL_TREE, ptr_type_node,
						       void_list_node)));
	  sprintf (name, "__Block_byref_id_object_copy%d", kind);
	  block_byref_id_object_copy[kind] = build_helper_func_decl (get_identifier (name),
								     func_type);
	  /* Synthesize function definition. */
	  synth_block_byref_id_object_copy_func (flag, kind);
	}
      initlist = tree_cons (fields,
			    build_fold_addr_expr (block_byref_id_object_copy[kind]),
			    initlist);
      fields = TREE_CHAIN (fields);

      if (!block_byref_id_object_dispose[kind])
	{
	  /* Synthesize void __Block_byref_id_object_dispose (void*) and
	     build &__Block_byref_id_object_dispose. */
	  tree func_type =
	    build_function_type (void_type_node,
				 tree_cons (NULL_TREE, ptr_type_node, void_list_node));
	  sprintf (name, "__Block_byref_id_object_dispose%d", kind);
	  block_byref_id_object_dispose[kind] = build_helper_func_decl (get_identifier (name),
									func_type);
	  /* Synthesize function definition. */
	  synth_block_byref_id_object_dispose_func (flag, kind);
	}
      initlist = tree_cons (fields,
			    build_fold_addr_expr (block_byref_id_object_dispose[kind]),
			    initlist);
      fields = TREE_CHAIN (fields);
    }

  if (init)
    {
      init = do_digest_init (TREE_TYPE (fields), init);
      initlist = tree_cons (fields, init, initlist);
    }
  init =  build_constructor_from_list (block_byref_type, nreverse (initlist));
  return init;
}
/* APPLE LOCAL end radar 5932809 - copyable byref blocks (C++ cr) */

/* Finish processing of a declaration;
   install its initial value.
   If the length of an array type is not known before,
   it must be determined now, from the initial value, or it is an error.  */

void
finish_decl (tree decl, tree init, tree asmspec_tree)
{
  tree type;
  int was_incomplete = (DECL_SIZE (decl) == 0);
  const char *asmspec = 0;

  /* If a name was specified, get the string.  */
  if ((TREE_CODE (decl) == FUNCTION_DECL || TREE_CODE (decl) == VAR_DECL)
      && DECL_FILE_SCOPE_P (decl))
    asmspec_tree = maybe_apply_renaming_pragma (decl, asmspec_tree);
  if (asmspec_tree)
    asmspec = TREE_STRING_POINTER (asmspec_tree);

  /* If `start_decl' didn't like having an initialization, ignore it now.  */
  if (init != 0 && DECL_INITIAL (decl) == 0)
    init = 0;

  /* Don't crash if parm is initialized.  */
  if (TREE_CODE (decl) == PARM_DECL)
    init = 0;
  /* APPLE LOCAL begin radar 5932809 - copyable byref blocks (C++ cq) */
  /* We build a new type for each local variable declared as __block
     and initialize it to a list of initializers. */
  else if (TREE_CODE (decl) == VAR_DECL && COPYABLE_BYREF_LOCAL_VAR (decl))
    {
      if (DECL_EXTERNAL (decl) || TREE_STATIC (decl))
	{
	  error ("__block attribute on %q+D not allowed, only allowed on local variables", decl);
	  COPYABLE_BYREF_LOCAL_VAR (decl) = 0;
	  COPYABLE_BYREF_LOCAL_NONPOD (decl) = 0;
	}
      else
	{
	   int flag = 0;
	   if (objc_is_gcable_type (TREE_TYPE (decl)) == -1)
	    flag = BLOCK_FIELD_IS_WEAK;
	  if (block_requires_copying (decl))
	    {
	      if (TREE_CODE (TREE_TYPE (decl)) == BLOCK_POINTER_TYPE)
		flag |= BLOCK_FIELD_IS_BLOCK;
	      else 
		flag |= BLOCK_FIELD_IS_OBJECT;
	    }
	  decl = new_block_byref_decl (decl);
	  /* APPLE LOCAL begin radar 6289031 */
#if 0
	  if (! flag_objc_gc_only)
#endif
	    {
	       push_cleanup (decl, build_block_byref_release_exp (decl), false);
	    }
	  /* APPLE LOCAL end radar 6289031 */
	   /* APPLE LOCAL begin radar 5847976 */
	  COPYABLE_WEAK_BLOCK (decl) = ((flag & BLOCK_FIELD_IS_WEAK) != 0);
	  init = init_byref_decl (decl, init, flag);
	   /* APPLE LOCAL end radar 5847976 */
	}
    }
  /* APPLE LOCAL end radar 5932809 - copyable byref blocks (C++ cq) */

  if (init)
    store_init_value (decl, init);

  if (c_dialect_objc () && (TREE_CODE (decl) == VAR_DECL
			    || TREE_CODE (decl) == FUNCTION_DECL
			    || TREE_CODE (decl) == FIELD_DECL))
    objc_check_decl (decl);

  type = TREE_TYPE (decl);

  /* Deduce size of array from initialization, if not already known.  */
  if (TREE_CODE (type) == ARRAY_TYPE
      && TYPE_DOMAIN (type) == 0
      && TREE_CODE (decl) != TYPE_DECL)
    {
      bool do_default
	= (TREE_STATIC (decl)
	   /* Even if pedantic, an external linkage array
	      may have incomplete type at first.  */
	   ? pedantic && !TREE_PUBLIC (decl)
	   : !DECL_EXTERNAL (decl));
      int failure
	= complete_array_type (&TREE_TYPE (decl), DECL_INITIAL (decl),
			       do_default);

      /* Get the completed type made by complete_array_type.  */
      type = TREE_TYPE (decl);

      switch (failure)
	{
	case 1:
	  error ("initializer fails to determine size of %q+D", decl);
	  break;

	case 2:
	  if (do_default)
	    error ("array size missing in %q+D", decl);
	  /* If a `static' var's size isn't known,
	     make it extern as well as static, so it does not get
	     allocated.
	     If it is not `static', then do not mark extern;
	     finish_incomplete_decl will give it a default size
	     and it will get allocated.  */
	  else if (!pedantic && TREE_STATIC (decl) && !TREE_PUBLIC (decl))
	    DECL_EXTERNAL (decl) = 1;
	  break;

	case 3:
	  error ("zero or negative size array %q+D", decl);
	  break;

	case 0:
	  /* For global variables, update the copy of the type that
	     exists in the binding.  */
	  if (TREE_PUBLIC (decl))
	    {
	      struct c_binding *b_ext = I_SYMBOL_BINDING (DECL_NAME (decl));
	      while (b_ext && !B_IN_EXTERNAL_SCOPE (b_ext))
		b_ext = b_ext->shadowed;
	      if (b_ext)
		{
		  if (b_ext->type)
		    b_ext->type = composite_type (b_ext->type, type);
		  else
		    b_ext->type = type;
		}
	    }
	  break;

	default:
	  gcc_unreachable ();
	}

      if (DECL_INITIAL (decl))
	TREE_TYPE (DECL_INITIAL (decl)) = type;

      layout_decl (decl, 0);
    }

  if (TREE_CODE (decl) == VAR_DECL)
    {
      if (init && TREE_CODE (init) == CONSTRUCTOR)
	add_flexible_array_elts_to_size (decl, init);

      if (DECL_SIZE (decl) == 0 && TREE_TYPE (decl) != error_mark_node
	  && COMPLETE_TYPE_P (TREE_TYPE (decl)))
	layout_decl (decl, 0);

      if (DECL_SIZE (decl) == 0
	  /* Don't give an error if we already gave one earlier.  */
	  && TREE_TYPE (decl) != error_mark_node
	  && (TREE_STATIC (decl)
	      /* A static variable with an incomplete type
		 is an error if it is initialized.
		 Also if it is not file scope.
		 Otherwise, let it through, but if it is not `extern'
		 then it may cause an error message later.  */
	      ? (DECL_INITIAL (decl) != 0
		 || !DECL_FILE_SCOPE_P (decl))
	      /* An automatic variable with an incomplete type
		 is an error.  */
	      : !DECL_EXTERNAL (decl)))
	 {
	   error ("storage size of %q+D isn%'t known", decl);
	   TREE_TYPE (decl) = error_mark_node;
	 }

      if ((DECL_EXTERNAL (decl) || TREE_STATIC (decl))
	  && DECL_SIZE (decl) != 0)
	{
	  if (TREE_CODE (DECL_SIZE (decl)) == INTEGER_CST)
	    constant_expression_warning (DECL_SIZE (decl));
	  else
	    error ("storage size of %q+D isn%'t constant", decl);
	}

      if (TREE_USED (type))
	TREE_USED (decl) = 1;
    }

  /* If this is a function and an assembler name is specified, reset DECL_RTL
     so we can give it its new name.  Also, update built_in_decls if it
     was a normal built-in.  */
  if (TREE_CODE (decl) == FUNCTION_DECL && asmspec)
    {
      if (DECL_BUILT_IN_CLASS (decl) == BUILT_IN_NORMAL)
	set_builtin_user_assembler_name (decl, asmspec);
      set_user_assembler_name (decl, asmspec);
    }

  /* If #pragma weak was used, mark the decl weak now.  */
  maybe_apply_pragma_weak (decl);

  /* Output the assembler code and/or RTL code for variables and functions,
     unless the type is an undefined structure or union.
     If not, it will get done when the type is completed.  */

  if (TREE_CODE (decl) == VAR_DECL || TREE_CODE (decl) == FUNCTION_DECL)
    {
      /* Determine the ELF visibility.  */
      if (TREE_PUBLIC (decl))
	c_determine_visibility (decl);

      /* This is a no-op in c-lang.c or something real in objc-act.c.  */
      if (c_dialect_objc ())
	objc_check_decl (decl);

      if (asmspec)
	{
	  /* If this is not a static variable, issue a warning.
	     It doesn't make any sense to give an ASMSPEC for an
	     ordinary, non-register local variable.  Historically,
	     GCC has accepted -- but ignored -- the ASMSPEC in
	     this case.  */
	  if (!DECL_FILE_SCOPE_P (decl)
	      && TREE_CODE (decl) == VAR_DECL
	      && !C_DECL_REGISTER (decl)
	      && !TREE_STATIC (decl))
	    warning (0, "ignoring asm-specifier for non-static local "
		     "variable %q+D", decl);
	  else
	    set_user_assembler_name (decl, asmspec);
	}

      if (DECL_FILE_SCOPE_P (decl))
	{
	  if (DECL_INITIAL (decl) == NULL_TREE
	      || DECL_INITIAL (decl) == error_mark_node)
	    /* Don't output anything
	       when a tentative file-scope definition is seen.
	       But at end of compilation, do output code for them.  */
	    DECL_DEFER_OUTPUT (decl) = 1;
	  rest_of_decl_compilation (decl, true, 0);
	}
      else
	{
	  /* In conjunction with an ASMSPEC, the `register'
	     keyword indicates that we should place the variable
	     in a particular register.  */
	  if (asmspec && C_DECL_REGISTER (decl))
	    {
	      DECL_HARD_REGISTER (decl) = 1;
	      /* This cannot be done for a structure with volatile
		 fields, on which DECL_REGISTER will have been
		 reset.  */
	      if (!DECL_REGISTER (decl))
		error ("cannot put object with volatile field into register");
	    }

	  if (TREE_CODE (decl) != FUNCTION_DECL)
	    {
	      /* If we're building a variable sized type, and we might be
		 reachable other than via the top of the current binding
		 level, then create a new BIND_EXPR so that we deallocate
		 the object at the right time.  */
	      /* Note that DECL_SIZE can be null due to errors.  */
	      if (DECL_SIZE (decl)
		  && !TREE_CONSTANT (DECL_SIZE (decl))
		  && STATEMENT_LIST_HAS_LABEL (cur_stmt_list))
		{
		  tree bind;
		  bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
		  TREE_SIDE_EFFECTS (bind) = 1;
		  add_stmt (bind);
		  BIND_EXPR_BODY (bind) = push_stmt_list ();
		}
	      add_stmt (build_stmt (DECL_EXPR, decl));
	    }
	}


      if (!DECL_FILE_SCOPE_P (decl))
	{
	  /* Recompute the RTL of a local array now
	     if it used to be an incomplete type.  */
	  if (was_incomplete
	      && !TREE_STATIC (decl) && !DECL_EXTERNAL (decl))
	    {
	      /* If we used it already as memory, it must stay in memory.  */
	      TREE_ADDRESSABLE (decl) = TREE_USED (decl);
	      /* If it's still incomplete now, no init will save it.  */
	      if (DECL_SIZE (decl) == 0)
		DECL_INITIAL (decl) = 0;
	    }
	}
    }

  /* If this was marked 'used', be sure it will be output.  */
  if (!flag_unit_at_a_time && lookup_attribute ("used", DECL_ATTRIBUTES (decl)))
    mark_decl_referenced (decl);

  if (TREE_CODE (decl) == TYPE_DECL)
    {
      if (!DECL_FILE_SCOPE_P (decl)
	  && variably_modified_type_p (TREE_TYPE (decl), NULL_TREE))
	add_stmt (build_stmt (DECL_EXPR, decl));

      rest_of_decl_compilation (decl, DECL_FILE_SCOPE_P (decl), 0);
    }

  /* At the end of a declaration, throw away any variable type sizes
     of types defined inside that declaration.  There is no use
     computing them in the following function definition.  */
  if (current_scope == file_scope)
    get_pending_sizes ();

  /* Install a cleanup (aka destructor) if one was given.  */
  if (TREE_CODE (decl) == VAR_DECL && !TREE_STATIC (decl))
    {
      tree attr = lookup_attribute ("cleanup", DECL_ATTRIBUTES (decl));
      if (attr)
	{
	  tree cleanup_id = TREE_VALUE (TREE_VALUE (attr));
	  tree cleanup_decl = lookup_name (cleanup_id);
	  tree cleanup;

	  /* Build "cleanup(&decl)" for the destructor.  */
	  cleanup = build_unary_op (ADDR_EXPR, decl, 0);
	  cleanup = build_tree_list (NULL_TREE, cleanup);
	  cleanup = build_function_call (cleanup_decl, cleanup);

	  /* Don't warn about decl unused; the cleanup uses it.  */
	  TREE_USED (decl) = 1;
	  TREE_USED (cleanup_decl) = 1;

	  /* Initialize EH, if we've been told to do so.  */
	  c_maybe_initialize_eh ();

	  push_cleanup (decl, cleanup, false);
	}
    }
}

/* Given a parsed parameter declaration, decode it into a PARM_DECL.  */

tree
grokparm (const struct c_parm *parm)
{
  tree decl = grokdeclarator (parm->declarator, parm->specs, PARM, false,
			      NULL);

  decl_attributes (&decl, parm->attrs, 0);

  return decl;
}

/* Given a parsed parameter declaration, decode it into a PARM_DECL
   and push that on the current scope.  */

void
push_parm_decl (const struct c_parm *parm)
{
  tree decl;

  decl = grokdeclarator (parm->declarator, parm->specs, PARM, false, NULL);
  decl_attributes (&decl, parm->attrs, 0);

  decl = pushdecl (decl);

  finish_decl (decl, NULL_TREE, NULL_TREE);
}

/* Mark all the parameter declarations to date as forward decls.
   Also diagnose use of this extension.  */

void
mark_forward_parm_decls (void)
{
  struct c_binding *b;

  if (pedantic && !current_scope->warned_forward_parm_decls)
    {
      pedwarn ("ISO C forbids forward parameter declarations");
      current_scope->warned_forward_parm_decls = true;
    }

  for (b = current_scope->bindings; b; b = b->prev)
    if (TREE_CODE (b->decl) == PARM_DECL)
      TREE_ASM_WRITTEN (b->decl) = 1;
}

/* Build a COMPOUND_LITERAL_EXPR.  TYPE is the type given in the compound
   literal, which may be an incomplete array type completed by the
   initializer; INIT is a CONSTRUCTOR that initializes the compound
   literal.  */

tree
build_compound_literal (tree type, tree init)
{
  /* We do not use start_decl here because we have a type, not a declarator;
     and do not use finish_decl because the decl should be stored inside
     the COMPOUND_LITERAL_EXPR rather than added elsewhere as a DECL_EXPR.  */
  tree decl;
  tree complit;
  tree stmt;

  if (type == error_mark_node)
    return error_mark_node;

  decl = build_decl (VAR_DECL, NULL_TREE, type);
  DECL_EXTERNAL (decl) = 0;
  TREE_PUBLIC (decl) = 0;
  TREE_STATIC (decl) = (current_scope == file_scope);
  DECL_CONTEXT (decl) = current_function_decl;
  TREE_USED (decl) = 1;
  TREE_TYPE (decl) = type;
  TREE_READONLY (decl) = TYPE_READONLY (type);
  store_init_value (decl, init);

  if (TREE_CODE (type) == ARRAY_TYPE && !COMPLETE_TYPE_P (type))
    {
      int failure = complete_array_type (&TREE_TYPE (decl),
					 DECL_INITIAL (decl), true);
      gcc_assert (!failure);

      type = TREE_TYPE (decl);
      TREE_TYPE (DECL_INITIAL (decl)) = type;
    }

  if (type == error_mark_node || !COMPLETE_TYPE_P (type))
    return error_mark_node;

  stmt = build_stmt (DECL_EXPR, decl);
  complit = build1 (COMPOUND_LITERAL_EXPR, type, stmt);
  TREE_SIDE_EFFECTS (complit) = 1;

  layout_decl (decl, 0);

  if (TREE_STATIC (decl))
    {
      /* This decl needs a name for the assembler output.  */
      set_compound_literal_name (decl);
      DECL_DEFER_OUTPUT (decl) = 1;
      DECL_COMDAT (decl) = 1;
      DECL_ARTIFICIAL (decl) = 1;
      DECL_IGNORED_P (decl) = 1;
      pushdecl (decl);
      rest_of_decl_compilation (decl, 1, 0);
    }

  return complit;
}

/* Determine whether TYPE is a structure with a flexible array member,
   or a union containing such a structure (possibly recursively).  */

static bool
flexible_array_type_p (tree type)
{
  tree x;
  switch (TREE_CODE (type))
    {
    case RECORD_TYPE:
      x = TYPE_FIELDS (type);
      if (x == NULL_TREE)
	return false;
      while (TREE_CHAIN (x) != NULL_TREE)
	x = TREE_CHAIN (x);
      if (TREE_CODE (TREE_TYPE (x)) == ARRAY_TYPE
	  && TYPE_SIZE (TREE_TYPE (x)) == NULL_TREE
	  && TYPE_DOMAIN (TREE_TYPE (x)) != NULL_TREE
	  && TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (x))) == NULL_TREE)
	return true;
      return false;
    case UNION_TYPE:
      for (x = TYPE_FIELDS (type); x != NULL_TREE; x = TREE_CHAIN (x))
	{
	  if (flexible_array_type_p (TREE_TYPE (x)))
	    return true;
	}
      return false;
    default:
    return false;
  }
}

/* Performs sanity checks on the TYPE and WIDTH of the bit-field NAME,
   replacing with appropriate values if they are invalid.  */
static void
check_bitfield_type_and_width (tree *type, tree *width, const char *orig_name)
{
  tree type_mv;
  unsigned int max_width;
  unsigned HOST_WIDE_INT w;
  const char *name = orig_name ? orig_name: _("<anonymous>");

  /* Detect and ignore out of range field width and process valid
     field widths.  */
  if (!INTEGRAL_TYPE_P (TREE_TYPE (*width))
      || TREE_CODE (*width) != INTEGER_CST)
    {
      error ("bit-field %qs width not an integer constant", name);
      *width = integer_one_node;
    }
  else
    {
      constant_expression_warning (*width);
      if (tree_int_cst_sgn (*width) < 0)
	{
	  error ("negative width in bit-field %qs", name);
	  *width = integer_one_node;
	}
      else if (integer_zerop (*width) && orig_name)
	{
	  error ("zero width for bit-field %qs", name);
	  *width = integer_one_node;
	}
    }

  /* Detect invalid bit-field type.  */
  if (TREE_CODE (*type) != INTEGER_TYPE
      && TREE_CODE (*type) != BOOLEAN_TYPE
      && TREE_CODE (*type) != ENUMERAL_TYPE)
    {
      error ("bit-field %qs has invalid type", name);
      *type = unsigned_type_node;
    }

  type_mv = TYPE_MAIN_VARIANT (*type);
  if (pedantic
      && !in_system_header
      && type_mv != integer_type_node
      && type_mv != unsigned_type_node
      && type_mv != boolean_type_node)
    pedwarn ("type of bit-field %qs is a GCC extension", name);

  if (type_mv == boolean_type_node)
    max_width = CHAR_TYPE_SIZE;
  else
    max_width = TYPE_PRECISION (*type);

  if (0 < compare_tree_int (*width, max_width))
    {
      error ("width of %qs exceeds its type", name);
      w = max_width;
      *width = build_int_cst (NULL_TREE, w);
    }
  else
    w = tree_low_cst (*width, 1);

  if (TREE_CODE (*type) == ENUMERAL_TYPE)
    {
      struct lang_type *lt = TYPE_LANG_SPECIFIC (*type);
      if (!lt
	  || w < min_precision (lt->enum_min, TYPE_UNSIGNED (*type))
	  || w < min_precision (lt->enum_max, TYPE_UNSIGNED (*type)))
	warning (0, "%qs is narrower than values of its type", name);
    }
}



/* Print warning about variable length array if necessary.  */

static void
warn_variable_length_array (const char *name, tree size)
{
  int ped = !flag_isoc99 && pedantic && warn_vla != 0;
  int const_size = TREE_CONSTANT (size);

  if (ped)
    {
      if (const_size)
	{
	  if (name)
	    pedwarn ("ISO C90 forbids array %qs whose size "
		     "can%'t be evaluated",
		     name);
	  else
	    pedwarn ("ISO C90 forbids array whose size "
		     "can%'t be evaluated");
	}
      else
	{
	  if (name) 
	    pedwarn ("ISO C90 forbids variable length array %qs",
		     name);
	  else
	    pedwarn ("ISO C90 forbids variable length array");
	}
    }
  else if (warn_vla > 0)
    {
      if (const_size)
        {
	  if (name)
	    warning (OPT_Wvla,
		     "the size of array %qs can"
		     "%'t be evaluated", name);
	  else
	    warning (OPT_Wvla,
		     "the size of array can %'t be evaluated");
	}
      else
	{
	  if (name)
	    warning (OPT_Wvla,
		     "variable length array %qs is used",
		     name);
	  else
	    warning (OPT_Wvla,
		     "variable length array is used");
	}
    }
}

/* Given declspecs and a declarator,
   determine the name and type of the object declared
   and construct a ..._DECL node for it.
   (In one case we can return a ..._TYPE node instead.
    For invalid input we sometimes return 0.)

   DECLSPECS is a c_declspecs structure for the declaration specifiers.

   DECL_CONTEXT says which syntactic context this declaration is in:
     NORMAL for most contexts.  Make a VAR_DECL or FUNCTION_DECL or TYPE_DECL.
     FUNCDEF for a function definition.  Like NORMAL but a few different
      error messages in each case.  Return value may be zero meaning
      this definition is too screwy to try to parse.
     PARM for a parameter declaration (either within a function prototype
      or before a function body).  Make a PARM_DECL, or return void_type_node.
     TYPENAME if for a typename (in a cast or sizeof).
      Don't make a DECL node; just return the ..._TYPE node.
     FIELD for a struct or union field; make a FIELD_DECL.
   INITIALIZED is true if the decl has an initializer.
   WIDTH is non-NULL for bit-fields, and is a pointer to an INTEGER_CST node
   representing the width of the bit-field.

   In the TYPENAME case, DECLARATOR is really an absolute declarator.
   It may also be so in the PARM case, for a prototype where the
   argument type is specified but not the name.

   This function is where the complicated C meanings of `static'
   and `extern' are interpreted.  */

static tree
grokdeclarator (const struct c_declarator *declarator,
		struct c_declspecs *declspecs,
		enum decl_context decl_context, bool initialized, tree *width)
{
  tree type = declspecs->type;
  bool threadp = declspecs->thread_p;
  enum c_storage_class storage_class = declspecs->storage_class;
  int constp;
  int restrictp;
  int volatilep;
  int type_quals = TYPE_UNQUALIFIED;
  const char *name, *orig_name;
  tree typedef_type = 0;
  bool funcdef_flag = false;
  bool funcdef_syntax = false;
  int size_varies = 0;
  tree decl_attr = declspecs->decl_attr;
  int array_ptr_quals = TYPE_UNQUALIFIED;
  tree array_ptr_attrs = NULL_TREE;
  int array_parm_static = 0;
  bool array_parm_vla_unspec_p = false;
  tree returned_attrs = NULL_TREE;
  bool bitfield = width != NULL;
  tree element_type;
  struct c_arg_info *arg_info = 0;

  if (decl_context == FUNCDEF)
    funcdef_flag = true, decl_context = NORMAL;

  /* Look inside a declarator for the name being declared
     and get it as a string, for an error message.  */
  {
    const struct c_declarator *decl = declarator;
    name = 0;

    while (decl)
      switch (decl->kind)
	{
	case cdk_function:
	case cdk_array:
	case cdk_pointer:
	 /* APPLE LOCAL radar 5732232 - blocks */
	case cdk_block_pointer:
	  funcdef_syntax = (decl->kind == cdk_function);
	  decl = decl->declarator;
	  break;

	case cdk_attrs:
	  decl = decl->declarator;
	  break;

	case cdk_id:
	  if (decl->u.id)
	    name = IDENTIFIER_POINTER (decl->u.id);
	  decl = 0;
	  break;

	default:
	  gcc_unreachable ();
	}
    orig_name = name;
    if (name == 0)
      name = "type name";
  }

  /* A function definition's declarator must have the form of
     a function declarator.  */

  if (funcdef_flag && !funcdef_syntax)
    return 0;

  /* If this looks like a function definition, make it one,
     even if it occurs where parms are expected.
     Then store_parm_decls will reject it and not use it as a parm.  */
  /* APPLE LOCAL begin "unavailable" attribute (radar 2809697) */
  if (declspecs->unavailable_p)
    error_unavailable_use (declspecs->type);
  else
  /* APPLE LOCAL end "unavailable" attribute (radar 2809697) */
  if (decl_context == NORMAL && !funcdef_flag && current_scope->parm_flag)
    decl_context = PARM;

  if (declspecs->deprecated_p && deprecated_state != DEPRECATED_SUPPRESS)
    warn_deprecated_use (declspecs->type);

  if ((decl_context == NORMAL || decl_context == FIELD)
      && current_scope == file_scope
      && variably_modified_type_p (type, NULL_TREE))
    {
      error ("variably modified %qs at file scope", name);
      type = integer_type_node;
    }

  typedef_type = type;
  size_varies = C_TYPE_VARIABLE_SIZE (type);

  /* Diagnose defaulting to "int".  */

  if (declspecs->default_int_p && !in_system_header)
    {
      /* Issue a warning if this is an ISO C 99 program or if
	 -Wreturn-type and this is a function, or if -Wimplicit;
	 prefer the former warning since it is more explicit.  */
      if ((warn_implicit_int || warn_return_type || flag_isoc99)
	  && funcdef_flag)
	warn_about_return_type = 1;
      else if (warn_implicit_int || flag_isoc99)
	pedwarn_c99 ("type defaults to %<int%> in declaration of %qs", name);
    }

  /* Adjust the type if a bit-field is being declared,
     -funsigned-bitfields applied and the type is not explicitly
     "signed".  */
  if (bitfield && !flag_signed_bitfields && !declspecs->explicit_signed_p
      && TREE_CODE (type) == INTEGER_TYPE)
    type = c_common_unsigned_type (type);

  /* Figure out the type qualifiers for the declaration.  There are
     two ways a declaration can become qualified.  One is something
     like `const int i' where the `const' is explicit.  Another is
     something like `typedef const int CI; CI i' where the type of the
     declaration contains the `const'.  A third possibility is that
     there is a type qualifier on the element type of a typedefed
     array type, in which case we should extract that qualifier so
     that c_apply_type_quals_to_decls receives the full list of
     qualifiers to work with (C90 is not entirely clear about whether
     duplicate qualifiers should be diagnosed in this case, but it
     seems most appropriate to do so).  */
  element_type = strip_array_types (type);
  constp = declspecs->const_p + TYPE_READONLY (element_type);
  restrictp = declspecs->restrict_p + TYPE_RESTRICT (element_type);
  volatilep = declspecs->volatile_p + TYPE_VOLATILE (element_type);
  if (pedantic && !flag_isoc99)
    {
      if (constp > 1)
	pedwarn ("duplicate %<const%>");
      if (restrictp > 1)
	pedwarn ("duplicate %<restrict%>");
      if (volatilep > 1)
	pedwarn ("duplicate %<volatile%>");
    }
  if (!flag_gen_aux_info && (TYPE_QUALS (element_type)))
    type = TYPE_MAIN_VARIANT (type);
  type_quals = ((constp ? TYPE_QUAL_CONST : 0)
		| (restrictp ? TYPE_QUAL_RESTRICT : 0)
		| (volatilep ? TYPE_QUAL_VOLATILE : 0));

  /* Warn about storage classes that are invalid for certain
     kinds of declarations (parameters, typenames, etc.).  */

  if (funcdef_flag
      && (threadp
	  || storage_class == csc_auto
	  || storage_class == csc_register
	  || storage_class == csc_typedef))
    {
      if (storage_class == csc_auto
	  && (pedantic || current_scope == file_scope))
	pedwarn ("function definition declared %<auto%>");
      if (storage_class == csc_register)
	error ("function definition declared %<register%>");
      if (storage_class == csc_typedef)
	error ("function definition declared %<typedef%>");
      if (threadp)
	error ("function definition declared %<__thread%>");
      threadp = false;
      if (storage_class == csc_auto
	  || storage_class == csc_register
	  || storage_class == csc_typedef)
	storage_class = csc_none;
    }
  else if (decl_context != NORMAL && (storage_class != csc_none || threadp))
    {
      if (decl_context == PARM && storage_class == csc_register)
	;
      else
	{
	  switch (decl_context)
	    {
	    case FIELD:
	      error ("storage class specified for structure field %qs",
		     name);
	      break;
	    case PARM:
	      error ("storage class specified for parameter %qs", name);
	      break;
	    default:
	      error ("storage class specified for typename");
	      break;
	    }
	  storage_class = csc_none;
	  threadp = false;
	}
    }
  else if (storage_class == csc_extern
	   && initialized
	   && !funcdef_flag)
    {
      /* 'extern' with initialization is invalid if not at file scope.  */
       if (current_scope == file_scope)
         {
           /* It is fine to have 'extern const' when compiling at C
              and C++ intersection.  */
           if (!(warn_cxx_compat && constp))
             warning (0, "%qs initialized and declared %<extern%>", name);
         }
      else
	error ("%qs has both %<extern%> and initializer", name);
    }
  else if (current_scope == file_scope)
    {
      if (storage_class == csc_auto)
	error ("file-scope declaration of %qs specifies %<auto%>", name);
      if (pedantic && storage_class == csc_register)
	pedwarn ("file-scope declaration of %qs specifies %<register%>", name);
    }
  else
    {
      if (storage_class == csc_extern && funcdef_flag)
	error ("nested function %qs declared %<extern%>", name);
      else if (threadp && storage_class == csc_none)
	{
	  error ("function-scope %qs implicitly auto and declared "
		 "%<__thread%>",
		 name);
	  threadp = false;
	}
    }

  /* Now figure out the structure of the declarator proper.
     Descend through it, creating more complex types, until we reach
     the declared identifier (or NULL_TREE, in an absolute declarator).
     At each stage we maintain an unqualified version of the type
     together with any qualifiers that should be applied to it with
     c_build_qualified_type; this way, array types including
     multidimensional array types are first built up in unqualified
     form and then the qualified form is created with
     TYPE_MAIN_VARIANT pointing to the unqualified form.  */

  while (declarator && declarator->kind != cdk_id)
    {
      if (type == error_mark_node)
	{
	  declarator = declarator->declarator;
	  continue;
	}

      /* Each level of DECLARATOR is either a cdk_array (for ...[..]),
	 a cdk_pointer (for *...),
	 a cdk_function (for ...(...)),
	 a cdk_attrs (for nested attributes),
	 or a cdk_id (for the name being declared
	 or the place in an absolute declarator
	 where the name was omitted).
	 For the last case, we have just exited the loop.

	 At this point, TYPE is the type of elements of an array,
	 or for a function to return, or for a pointer to point to.
	 After this sequence of ifs, TYPE is the type of the
	 array or function or pointer, and DECLARATOR has had its
	 outermost layer removed.  */

      if (array_ptr_quals != TYPE_UNQUALIFIED
	  || array_ptr_attrs != NULL_TREE
	  || array_parm_static)
	{
	  /* Only the innermost declarator (making a parameter be of
	     array type which is converted to pointer type)
	     may have static or type qualifiers.  */
	  error ("static or type qualifiers in non-parameter array declarator");
	  array_ptr_quals = TYPE_UNQUALIFIED;
	  array_ptr_attrs = NULL_TREE;
	  array_parm_static = 0;
	}

      switch (declarator->kind)
	{
	case cdk_attrs:
	  {
	    /* A declarator with embedded attributes.  */
	    tree attrs = declarator->u.attrs;
	    const struct c_declarator *inner_decl;
	    int attr_flags = 0;
	    declarator = declarator->declarator;
	    inner_decl = declarator;
	    while (inner_decl->kind == cdk_attrs)
	      inner_decl = inner_decl->declarator;
	    if (inner_decl->kind == cdk_id)
	      attr_flags |= (int) ATTR_FLAG_DECL_NEXT;
	    else if (inner_decl->kind == cdk_function)
	      attr_flags |= (int) ATTR_FLAG_FUNCTION_NEXT;
	    else if (inner_decl->kind == cdk_array)
	      attr_flags |= (int) ATTR_FLAG_ARRAY_NEXT;
	    returned_attrs = decl_attributes (&type,
					      chainon (returned_attrs, attrs),
					      attr_flags);
	    break;
	  }
	case cdk_array:
	  {
	    tree itype = NULL_TREE;
	    tree size = declarator->u.array.dimen;
	    /* The index is a signed object `sizetype' bits wide.  */
	    tree index_type = c_common_signed_type (sizetype);

	    array_ptr_quals = declarator->u.array.quals;
	    array_ptr_attrs = declarator->u.array.attrs;
	    array_parm_static = declarator->u.array.static_p;
	    array_parm_vla_unspec_p = declarator->u.array.vla_unspec_p;

	    declarator = declarator->declarator;

	    /* Check for some types that there cannot be arrays of.  */

	    if (VOID_TYPE_P (type))
	      {
		error ("declaration of %qs as array of voids", name);
		type = error_mark_node;
	      }

	    if (TREE_CODE (type) == FUNCTION_TYPE)
	      {
		error ("declaration of %qs as array of functions", name);
		type = error_mark_node;
	      }

	    if (pedantic && !in_system_header && flexible_array_type_p (type))
	      pedwarn ("invalid use of structure with flexible array member");

	    if (size == error_mark_node)
	      type = error_mark_node;

	    if (type == error_mark_node)
	      continue;

	    /* If size was specified, set ITYPE to a range-type for
	       that size.  Otherwise, ITYPE remains null.  finish_decl
	       may figure it out from an initial value.  */

	    if (size)
	      {
		/* Strip NON_LVALUE_EXPRs since we aren't using as an
		   lvalue.  */
		STRIP_TYPE_NOPS (size);

		if (!INTEGRAL_TYPE_P (TREE_TYPE (size)))
		  {
		    error ("size of array %qs has non-integer type", name);
		    size = integer_one_node;
		  }

		if (pedantic && integer_zerop (size))
		  pedwarn ("ISO C forbids zero-size array %qs", name);

		if (TREE_CODE (size) == INTEGER_CST)
		  {
		    constant_expression_warning (size);
		    if (tree_int_cst_sgn (size) < 0)
		      {
			error ("size of array %qs is negative", name);
			size = integer_one_node;
		      }
		  }
		else if ((decl_context == NORMAL || decl_context == FIELD)
			 && current_scope == file_scope)
		  {
		    error ("variably modified %qs at file scope", name);
		    size = integer_one_node;
		  }
		else
		  {
		    /* Make sure the array size remains visibly
		       nonconstant even if it is (eg) a const variable
		       with known value.  */
		    size_varies = 1;
		    warn_variable_length_array (orig_name, size);
		    if (warn_variable_decl)
		      warning (0, "variable-sized array %qs", name);
		  }

		if (integer_zerop (size))
		  {
		    /* A zero-length array cannot be represented with
		       an unsigned index type, which is what we'll
		       get with build_index_type.  Create an
		       open-ended range instead.  */
		    itype = build_range_type (sizetype, size, NULL_TREE);
		  }
		else
		  {
		    /* Arrange for the SAVE_EXPR on the inside of the
		       MINUS_EXPR, which allows the -1 to get folded
		       with the +1 that happens when building TYPE_SIZE.  */
		    if (size_varies)
		      size = variable_size (size);

		    /* Compute the maximum valid index, that is, size
		       - 1.  Do the calculation in index_type, so that
		       if it is a variable the computations will be
		       done in the proper mode.  */
		    itype = fold_build2 (MINUS_EXPR, index_type,
					 convert (index_type, size),
					 convert (index_type,
						  size_one_node));

		    /* If that overflowed, the array is too big.  ???
		       While a size of INT_MAX+1 technically shouldn't
		       cause an overflow (because we subtract 1), the
		       overflow is recorded during the conversion to
		       index_type, before the subtraction.  Handling
		       this case seems like an unnecessary
		       complication.  */
		    if (TREE_CODE (itype) == INTEGER_CST
			&& TREE_OVERFLOW (itype))
		      {
			error ("size of array %qs is too large", name);
			type = error_mark_node;
			continue;
		      }

		    itype = build_index_type (itype);
		  }
	      }
	    else if (decl_context == FIELD)
	      {
		if (pedantic && !flag_isoc99 && !in_system_header)
		  pedwarn ("ISO C90 does not support flexible array members");

		/* ISO C99 Flexible array members are effectively
		   identical to GCC's zero-length array extension.  */
		itype = build_range_type (sizetype, size_zero_node, NULL_TREE);
	      }
	    else if (decl_context == PARM)
	      {
		if (array_parm_vla_unspec_p)
		  {
		    if (! orig_name)
		      {
			/* C99 6.7.5.2p4 */
			error ("%<[*]%> not allowed in other than a declaration");
		      }

		    itype = build_range_type (sizetype, size_zero_node, NULL_TREE);
		    size_varies = 1;
		  }
	      }
	    else if (decl_context == TYPENAME)
	      {
		if (array_parm_vla_unspec_p)
		  {
		    /* The error is printed elsewhere.  We use this to
		       avoid messing up with incomplete array types of
		       the same type, that would otherwise be modified
		       below.  */
		    itype = build_range_type (sizetype, size_zero_node,
					      NULL_TREE);
		  }
	      }

	     /* Complain about arrays of incomplete types.  */
	    if (!COMPLETE_TYPE_P (type))
	      {
		error ("array type has incomplete element type");
		type = error_mark_node;
	      }
	    else
	    /* When itype is NULL, a shared incomplete array type is
	       returned for all array of a given type.  Elsewhere we
	       make sure we don't complete that type before copying
	       it, but here we want to make sure we don't ever
	       modify the shared type, so we gcc_assert (itype)
	       below.  */
	      type = build_array_type (type, itype);

	    if (type != error_mark_node)
	      {
		if (size_varies)
		  {
		    /* It is ok to modify type here even if itype is
		       NULL: if size_varies, we're in a
		       multi-dimensional array and the inner type has
		       variable size, so the enclosing shared array type
		       must too.  */
		    if (size && TREE_CODE (size) == INTEGER_CST)
		      type
			= build_distinct_type_copy (TYPE_MAIN_VARIANT (type));
		    C_TYPE_VARIABLE_SIZE (type) = 1;
		  }

		/* The GCC extension for zero-length arrays differs from
		   ISO flexible array members in that sizeof yields
		   zero.  */
		if (size && integer_zerop (size))
		  {
		    gcc_assert (itype);
		    TYPE_SIZE (type) = bitsize_zero_node;
		    TYPE_SIZE_UNIT (type) = size_zero_node;
		  }
		if (array_parm_vla_unspec_p)
		  {
		    gcc_assert (itype);
		    /* The type is complete.  C99 6.7.5.2p4  */
		    TYPE_SIZE (type) = bitsize_zero_node;
		    TYPE_SIZE_UNIT (type) = size_zero_node;
		  }
	      }

	    if (decl_context != PARM
		&& (array_ptr_quals != TYPE_UNQUALIFIED
		    || array_ptr_attrs != NULL_TREE
		    || array_parm_static))
	      {
		error ("static or type qualifiers in non-parameter array declarator");
		array_ptr_quals = TYPE_UNQUALIFIED;
		array_ptr_attrs = NULL_TREE;
		array_parm_static = 0;
	      }
	    break;
	  }
	case cdk_function:
	  {
	    /* Say it's a definition only for the declarator closest
	       to the identifier, apart possibly from some
	       attributes.  */
	    bool really_funcdef = false;
	    tree arg_types;
	    if (funcdef_flag)
	      {
		const struct c_declarator *t = declarator->declarator;
		while (t->kind == cdk_attrs)
		  t = t->declarator;
		really_funcdef = (t->kind == cdk_id);
	      }

	    /* Declaring a function type.  Make sure we have a valid
	       type for the function to return.  */
	    if (type == error_mark_node)
	      continue;

	    size_varies = 0;

	    /* Warn about some types functions can't return.  */
	    if (TREE_CODE (type) == FUNCTION_TYPE)
	      {
		error ("%qs declared as function returning a function", name);
		type = integer_type_node;
	      }
	    if (TREE_CODE (type) == ARRAY_TYPE)
	      {
		error ("%qs declared as function returning an array", name);
		type = integer_type_node;
	      }

	    /* Construct the function type and go to the next
	       inner layer of declarator.  */
	    arg_info = declarator->u.arg_info;
	    arg_types = grokparms (arg_info, really_funcdef);
	    if (really_funcdef)
	      put_pending_sizes (arg_info->pending_sizes);

	    /* Type qualifiers before the return type of the function
	       qualify the return type, not the function type.  */
	    if (type_quals)
	      {
		/* Type qualifiers on a function return type are
		   normally permitted by the standard but have no
		   effect, so give a warning at -Wreturn-type.
		   Qualifiers on a void return type are banned on
		   function definitions in ISO C; GCC used to used
		   them for noreturn functions.  */
		if (VOID_TYPE_P (type) && really_funcdef)
		  pedwarn ("function definition has qualified void return type");
		else
		  warning (OPT_Wreturn_type,
			   "type qualifiers ignored on function return type");

		type = c_build_qualified_type (type, type_quals);
	      }
	    type_quals = TYPE_UNQUALIFIED;

	    type = build_function_type (type, arg_types);
	    declarator = declarator->declarator;

	    /* Set the TYPE_CONTEXTs for each tagged type which is local to
	       the formal parameter list of this FUNCTION_TYPE to point to
	       the FUNCTION_TYPE node itself.  */
	    {
	      tree link;

	      for (link = arg_info->tags;
		   link;
		   link = TREE_CHAIN (link))
		TYPE_CONTEXT (TREE_VALUE (link)) = type;
	    }
	    break;
	  }
	case cdk_pointer:
	  {
	    /* Merge any constancy or volatility into the target type
	       for the pointer.  */

	    if (pedantic && TREE_CODE (type) == FUNCTION_TYPE
		&& type_quals)
	      pedwarn ("ISO C forbids qualified function types");
	    if (type_quals)
	      type = c_build_qualified_type (type, type_quals);
	    size_varies = 0;

	    /* When the pointed-to type involves components of variable size,
	       care must be taken to ensure that the size evaluation code is
	       emitted early enough to dominate all the possible later uses
	       and late enough for the variables on which it depends to have
	       been assigned.

	       This is expected to happen automatically when the pointed-to
	       type has a name/declaration of it's own, but special attention
	       is required if the type is anonymous.

	       We handle the NORMAL and FIELD contexts here by attaching an
	       artificial TYPE_DECL to such pointed-to type.  This forces the
	       sizes evaluation at a safe point and ensures it is not deferred
	       until e.g. within a deeper conditional context.

	       We expect nothing to be needed here for PARM or TYPENAME.
	       Pushing a TYPE_DECL at this point for TYPENAME would actually
	       be incorrect, as we might be in the middle of an expression
	       with side effects on the pointed-to type size "arguments" prior
	       to the pointer declaration point and the fake TYPE_DECL in the
	       enclosing context would force the size evaluation prior to the
	       side effects.  */

	    if (!TYPE_NAME (type)
		&& (decl_context == NORMAL || decl_context == FIELD)
		&& variably_modified_type_p (type, NULL_TREE))
	      {
		tree decl = build_decl (TYPE_DECL, NULL_TREE, type);
		DECL_ARTIFICIAL (decl) = 1;
		pushdecl (decl);
		finish_decl (decl, NULL_TREE, NULL_TREE);
		TYPE_NAME (type) = decl;
	      }

	    type = build_pointer_type (type);

	    /* Process type qualifiers (such as const or volatile)
	       that were given inside the `*'.  */
	    type_quals = declarator->u.pointer_quals;

	    declarator = declarator->declarator;
	    break;
	  }


	 /* APPLE LOCAL begin radar 5732232 - blocks (C++ cj) */
	case cdk_block_pointer:
	  {
	    if (TREE_CODE (type) != FUNCTION_TYPE)
	      {
		error ("block pointer to non-function type is invalid");
		type = error_mark_node;
	      }
	    else
	      {
		type = build_block_pointer_type (type);
		/* APPLE LOCAL begin radar 5814025 (C++ cj) */
		/* Process type qualifiers (such as const or volatile)
		   that were given inside the `^'.  */
		type_quals = declarator->u.pointer_quals;
		/* APPLE LOCAL end radar 5814025 (C++ cj) */
		declarator = declarator->declarator;
	      }
	    break;
	  }
	  /* APPLE LOCAL end radar 5732232 - blocks (C++ cj) */

	default:
	  gcc_unreachable ();
	}
    }

  /* Now TYPE has the actual type, apart from any qualifiers in
     TYPE_QUALS.  */

  /* Check the type and width of a bit-field.  */
  if (bitfield)
    check_bitfield_type_and_width (&type, width, orig_name);

  /* Did array size calculations overflow?  */

  if (TREE_CODE (type) == ARRAY_TYPE
      && COMPLETE_TYPE_P (type)
      && TREE_CODE (TYPE_SIZE_UNIT (type)) == INTEGER_CST
      && TREE_OVERFLOW (TYPE_SIZE_UNIT (type)))
    {
      error ("size of array %qs is too large", name);
      /* If we proceed with the array type as it is, we'll eventually
	 crash in tree_low_cst().  */
      type = error_mark_node;
    }

  /* APPLE LOCAL begin blocks 6339747 */
  if (decl_context == BLOCKDEF)
    {
      tree decl;
      
      if (type == error_mark_node)
	return error_mark_node;
      
      if (TREE_CODE (type) != FUNCTION_TYPE)
      {
	tree arg_types;
	
	if (TREE_CODE (type) == ARRAY_TYPE)
	{
		error ("block declared as returning an array");
		return error_mark_node;
	}

	arg_info = XOBNEW (&parser_obstack, struct c_arg_info);
	arg_info->parms = 0;
	arg_info->tags = 0;
	arg_info->types = 0;
	arg_info->others = 0;
	arg_info->pending_sizes = 0;
	arg_info->had_vla_unspec = 0;
	arg_types = grokparms (arg_info, false);
	type_quals = TYPE_UNQUALIFIED;
	type = build_function_type (type, arg_types);
      }
      decl = build_decl (FUNCTION_DECL, NULL_TREE, type);
      DECL_ARGUMENTS (decl) = arg_info ? arg_info->parms : NULL_TREE;
      return decl;
    }
  /* APPLE LOCAL end blocks 6339747 */
  
  /* If this is declaring a typedef name, return a TYPE_DECL.  */

  if (storage_class == csc_typedef)
    {
      tree decl;
      if (pedantic && TREE_CODE (type) == FUNCTION_TYPE
	  && type_quals)
	pedwarn ("ISO C forbids qualified function types");
      if (type_quals)
	type = c_build_qualified_type (type, type_quals);
      decl = build_decl (TYPE_DECL, declarator->u.id, type);
      if (declspecs->explicit_signed_p)
	C_TYPEDEF_EXPLICITLY_SIGNED (decl) = 1;
      decl_attributes (&decl, returned_attrs, 0);
      if (declspecs->inline_p)
	pedwarn ("typedef %q+D declared %<inline%>", decl);
      return decl;
    }

  /* If this is a type name (such as, in a cast or sizeof),
     compute the type and return it now.  */

  if (decl_context == TYPENAME)
    {
      /* Note that the grammar rejects storage classes in typenames
	 and fields.  */
      gcc_assert (storage_class == csc_none && !threadp
		  && !declspecs->inline_p);
      if (pedantic && TREE_CODE (type) == FUNCTION_TYPE
	  && type_quals)
	pedwarn ("ISO C forbids const or volatile function types");
      if (type_quals)
	type = c_build_qualified_type (type, type_quals);
      decl_attributes (&type, returned_attrs, 0);
      return type;
    }

  if (pedantic && decl_context == FIELD
      && variably_modified_type_p (type, NULL_TREE))
    {
      /* C99 6.7.2.1p8 */
      pedwarn ("a member of a structure or union cannot have a variably modified type");
    }

  /* Aside from typedefs and type names (handle above),
     `void' at top level (not within pointer)
     is allowed only in public variables.
     We don't complain about parms either, but that is because
     a better error message can be made later.  */

  if (VOID_TYPE_P (type) && decl_context != PARM
      && !((decl_context != FIELD && TREE_CODE (type) != FUNCTION_TYPE)
	    && (storage_class == csc_extern
		|| (current_scope == file_scope
		    && !(storage_class == csc_static
			 || storage_class == csc_register)))))
    {
      error ("variable or field %qs declared void", name);
      type = integer_type_node;
    }

  /* Now create the decl, which may be a VAR_DECL, a PARM_DECL
     or a FUNCTION_DECL, depending on DECL_CONTEXT and TYPE.  */

  {
    tree decl;

    if (decl_context == PARM)
      {
	tree type_as_written;
	tree promoted_type;

	/* A parameter declared as an array of T is really a pointer to T.
	   One declared as a function is really a pointer to a function.  */

	if (TREE_CODE (type) == ARRAY_TYPE)
	  {
	    /* Transfer const-ness of array into that of type pointed to.  */
	    type = TREE_TYPE (type);
	    if (type_quals)
	      type = c_build_qualified_type (type, type_quals);
	    type = build_pointer_type (type);
	    type_quals = array_ptr_quals;

	    /* We don't yet implement attributes in this context.  */
	    if (array_ptr_attrs != NULL_TREE)
	      warning (OPT_Wattributes,
		       "attributes in parameter array declarator ignored");

	    size_varies = 0;
	  }
	else if (TREE_CODE (type) == FUNCTION_TYPE)
	  {
	    if (pedantic && type_quals)
	      pedwarn ("ISO C forbids qualified function types");
	    if (type_quals)
	      type = c_build_qualified_type (type, type_quals);
	    type = build_pointer_type (type);
	    type_quals = TYPE_UNQUALIFIED;
	  }
	else if (type_quals)
	  type = c_build_qualified_type (type, type_quals);

	type_as_written = type;

	decl = build_decl (PARM_DECL, declarator->u.id, type);
	if (size_varies)
	  C_DECL_VARIABLE_SIZE (decl) = 1;

	/* Compute the type actually passed in the parmlist,
	   for the case where there is no prototype.
	   (For example, shorts and chars are passed as ints.)
	   When there is a prototype, this is overridden later.  */

	if (type == error_mark_node)
	  promoted_type = type;
	else
	  promoted_type = c_type_promotes_to (type);

	DECL_ARG_TYPE (decl) = promoted_type;
	if (declspecs->inline_p)
	  pedwarn ("parameter %q+D declared %<inline%>", decl);
      }
    else if (decl_context == FIELD)
      {
	/* Note that the grammar rejects storage classes in typenames
	   and fields.  */
	gcc_assert (storage_class == csc_none && !threadp
		    && !declspecs->inline_p);

	/* Structure field.  It may not be a function.  */

	if (TREE_CODE (type) == FUNCTION_TYPE)
	  {
	    error ("field %qs declared as a function", name);
	    type = build_pointer_type (type);
	  }
	else if (TREE_CODE (type) != ERROR_MARK
		 && !COMPLETE_OR_UNBOUND_ARRAY_TYPE_P (type))
	  {
	    error ("field %qs has incomplete type", name);
	    type = error_mark_node;
	  }
	type = c_build_qualified_type (type, type_quals);
	decl = build_decl (FIELD_DECL, declarator->u.id, type);
	DECL_NONADDRESSABLE_P (decl) = bitfield;

	if (size_varies)
	  C_DECL_VARIABLE_SIZE (decl) = 1;
      }
    else if (TREE_CODE (type) == FUNCTION_TYPE)
      {
	if (storage_class == csc_register || threadp)
	  {
	    error ("invalid storage class for function %qs", name);
	   }
	else if (current_scope != file_scope)
	  {
	    /* Function declaration not at file scope.  Storage
	       classes other than `extern' are not allowed, C99
	       6.7.1p5, and `extern' makes no difference.  However,
	       GCC allows 'auto', perhaps with 'inline', to support
	       nested functions.  */
	    if (storage_class == csc_auto)
	      {
		if (pedantic)
		  pedwarn ("invalid storage class for function %qs", name);
	      }
	    else if (storage_class == csc_static)
	      {
		error ("invalid storage class for function %qs", name);
		if (funcdef_flag)
		  storage_class = declspecs->storage_class = csc_none;
		else
		  return 0;
	      }
	  }

	decl = build_decl (FUNCTION_DECL, declarator->u.id, type);
	decl = build_decl_attribute_variant (decl, decl_attr);

	DECL_LANG_SPECIFIC (decl) = GGC_CNEW (struct lang_decl);

	if (pedantic && type_quals && !DECL_IN_SYSTEM_HEADER (decl))
	  pedwarn ("ISO C forbids qualified function types");

	/* GNU C interprets a volatile-qualified function type to indicate
	   that the function does not return.  */
	if ((type_quals & TYPE_QUAL_VOLATILE)
	    && !VOID_TYPE_P (TREE_TYPE (TREE_TYPE (decl))))
	  warning (0, "%<noreturn%> function returns non-void value");

	/* Every function declaration is an external reference
	   (DECL_EXTERNAL) except for those which are not at file
	   scope and are explicitly declared "auto".  This is
	   forbidden by standard C (C99 6.7.1p5) and is interpreted by
	   GCC to signify a forward declaration of a nested function.  */
	if (storage_class == csc_auto && current_scope != file_scope)
	  DECL_EXTERNAL (decl) = 0;
	/* In C99, a function which is declared 'inline' with 'extern'
	   is not an external reference (which is confusing).  It
	   means that the later definition of the function must be output
	   in this file, C99 6.7.4p6.  In GNU C89, a function declared
	   'extern inline' is an external reference.  */
	else if (declspecs->inline_p && storage_class != csc_static)
	  DECL_EXTERNAL (decl) = ((storage_class == csc_extern)
				  == flag_gnu89_inline);
	else
	  DECL_EXTERNAL (decl) = !initialized;

	/* Record absence of global scope for `static' or `auto'.  */
	TREE_PUBLIC (decl)
	  = !(storage_class == csc_static || storage_class == csc_auto);

	/* For a function definition, record the argument information
	   block where store_parm_decls will look for it.  */
	if (funcdef_flag)
	  current_function_arg_info = arg_info;

	if (declspecs->default_int_p)
	  C_FUNCTION_IMPLICIT_INT (decl) = 1;

	/* Record presence of `inline', if it is reasonable.  */
	if (flag_hosted && MAIN_NAME_P (declarator->u.id))
	  {
	    if (declspecs->inline_p)
	      pedwarn ("cannot inline function %<main%>");
	  }
	else if (declspecs->inline_p)
	  {
	    /* Record that the function is declared `inline'.  */
	    DECL_DECLARED_INLINE_P (decl) = 1;

	    /* Do not mark bare declarations as DECL_INLINE.  Doing so
	       in the presence of multiple declarations can result in
	       the abstract origin pointing between the declarations,
	       which will confuse dwarf2out.  */
	    if (initialized)
	      DECL_INLINE (decl) = 1;
	  }
	/* If -finline-functions, assume it can be inlined.  This does
	   two things: let the function be deferred until it is actually
	   needed, and let dwarf2 know that the function is inlinable.  */
	else if (flag_inline_trees == 2 && initialized)
	  DECL_INLINE (decl) = 1;
      }
    else
      {
	/* It's a variable.  */
	/* An uninitialized decl with `extern' is a reference.  */
	int extern_ref = !initialized && storage_class == csc_extern;

	type = c_build_qualified_type (type, type_quals);

	/* C99 6.2.2p7: It is invalid (compile-time undefined
	   behavior) to create an 'extern' declaration for a
	   variable if there is a global declaration that is
	   'static' and the global declaration is not visible.
	   (If the static declaration _is_ currently visible,
	   the 'extern' declaration is taken to refer to that decl.) */
	if (extern_ref && current_scope != file_scope)
	  {
	    tree global_decl  = identifier_global_value (declarator->u.id);
	    tree visible_decl = lookup_name (declarator->u.id);

	    if (global_decl
		&& global_decl != visible_decl
		&& TREE_CODE (global_decl) == VAR_DECL
		&& !TREE_PUBLIC (global_decl))
	      error ("variable previously declared %<static%> redeclared "
		     "%<extern%>");
	  }

	decl = build_decl (VAR_DECL, declarator->u.id, type);
	DECL_SOURCE_LOCATION (decl) = declarator->id_loc;
	if (size_varies)
	  C_DECL_VARIABLE_SIZE (decl) = 1;

	if (declspecs->inline_p)
	  pedwarn ("variable %q+D declared %<inline%>", decl);

	/* At file scope, an initialized extern declaration may follow
	   a static declaration.  In that case, DECL_EXTERNAL will be
	   reset later in start_decl.  */
	DECL_EXTERNAL (decl) = (storage_class == csc_extern);

	/* At file scope, the presence of a `static' or `register' storage
	   class specifier, or the absence of all storage class specifiers
	   makes this declaration a definition (perhaps tentative).  Also,
	   the absence of `static' makes it public.  */
	if (current_scope == file_scope)
	  {
	    TREE_PUBLIC (decl) = storage_class != csc_static;
	    TREE_STATIC (decl) = !extern_ref;
	  }
	/* Not at file scope, only `static' makes a static definition.  */
	else
	  {
	    TREE_STATIC (decl) = (storage_class == csc_static);
	    TREE_PUBLIC (decl) = extern_ref;
	  }

	if (threadp)
	  {
	    if (targetm.have_tls)
	      DECL_TLS_MODEL (decl) = decl_default_tls_model (decl);
	    else
	      /* A mere warning is sure to result in improper semantics
		 at runtime.  Don't bother to allow this to compile.  */
	      error ("thread-local storage not supported for this target");
	  }
      }

    if (storage_class == csc_extern
	&& variably_modified_type_p (type, NULL_TREE))
      {
	/* C99 6.7.5.2p2 */
	error ("object with variably modified type must have no linkage");
      }

    /* Record `register' declaration for warnings on &
       and in case doing stupid register allocation.  */

    if (storage_class == csc_register)
      {
	C_DECL_REGISTER (decl) = 1;
	DECL_REGISTER (decl) = 1;
      }

    /* Record constancy and volatility.  */
    c_apply_type_quals_to_decl (type_quals, decl);

    /* If a type has volatile components, it should be stored in memory.
       Otherwise, the fact that those components are volatile
       will be ignored, and would even crash the compiler.
       Of course, this only makes sense on  VAR,PARM, and RESULT decl's.   */
    if (C_TYPE_FIELDS_VOLATILE (TREE_TYPE (decl))
	&& (TREE_CODE (decl) == VAR_DECL ||  TREE_CODE (decl) == PARM_DECL
	  || TREE_CODE (decl) == RESULT_DECL))
      {
	/* It is not an error for a structure with volatile fields to
	   be declared register, but reset DECL_REGISTER since it
	   cannot actually go in a register.  */
	int was_reg = C_DECL_REGISTER (decl);
	C_DECL_REGISTER (decl) = 0;
	DECL_REGISTER (decl) = 0;
	c_mark_addressable (decl);
	C_DECL_REGISTER (decl) = was_reg;
      }

  /* This is the earliest point at which we might know the assembler
     name of a variable.  Thus, if it's known before this, die horribly.  */
    gcc_assert (!DECL_ASSEMBLER_NAME_SET_P (decl));

    decl_attributes (&decl, returned_attrs, 0);

    return decl;
  }
}

/* Decode the parameter-list info for a function type or function definition.
   The argument is the value returned by `get_parm_info' (or made in c-parse.c
   if there is an identifier list instead of a parameter decl list).
   These two functions are separate because when a function returns
   or receives functions then each is called multiple times but the order
   of calls is different.  The last call to `grokparms' is always the one
   that contains the formal parameter names of a function definition.

   Return a list of arg types to use in the FUNCTION_TYPE for this function.

   FUNCDEF_FLAG is true for a function definition, false for
   a mere declaration.  A nonempty identifier-list gets an error message
   when FUNCDEF_FLAG is false.  */

static tree
grokparms (struct c_arg_info *arg_info, bool funcdef_flag)
{
  tree arg_types = arg_info->types;

  if (funcdef_flag && arg_info->had_vla_unspec)
    {
      /* A function definition isn't function prototype scope C99 6.2.1p4.  */
      /* C99 6.7.5.2p4 */
      error ("%<[*]%> not allowed in other than function prototype scope");
    }

  if (arg_types == 0 && !funcdef_flag && !in_system_header)
    warning (OPT_Wstrict_prototypes,
	     "function declaration isn%'t a prototype");

  if (arg_types == error_mark_node)
    return 0;  /* don't set TYPE_ARG_TYPES in this case */

  else if (arg_types && TREE_CODE (TREE_VALUE (arg_types)) == IDENTIFIER_NODE)
    {
      if (!funcdef_flag)
	pedwarn ("parameter names (without types) in function declaration");

      arg_info->parms = arg_info->types;
      arg_info->types = 0;
      return 0;
    }
  else
    {
      tree parm, type, typelt;
      unsigned int parmno;

      /* If there is a parameter of incomplete type in a definition,
	 this is an error.  In a declaration this is valid, and a
	 struct or union type may be completed later, before any calls
	 or definition of the function.  In the case where the tag was
	 first declared within the parameter list, a warning has
	 already been given.  If a parameter has void type, then
	 however the function cannot be defined or called, so
	 warn.  */

      for (parm = arg_info->parms, typelt = arg_types, parmno = 1;
	   parm;
	   parm = TREE_CHAIN (parm), typelt = TREE_CHAIN (typelt), parmno++)
	{
	  type = TREE_VALUE (typelt);
	  if (type == error_mark_node)
	    continue;

	  if (!COMPLETE_TYPE_P (type))
	    {
	      if (funcdef_flag)
		{
		  if (DECL_NAME (parm))
		    error ("parameter %u (%q+D) has incomplete type",
			   parmno, parm);
		  else
		    error ("%Jparameter %u has incomplete type",
			   parm, parmno);

		  TREE_VALUE (typelt) = error_mark_node;
		  TREE_TYPE (parm) = error_mark_node;
		}
	      else if (VOID_TYPE_P (type))
		{
		  if (DECL_NAME (parm))
		    warning (0, "parameter %u (%q+D) has void type",
			     parmno, parm);
		  else
		    warning (0, "%Jparameter %u has void type",
			     parm, parmno);
		}
	    }

	  if (DECL_NAME (parm) && TREE_USED (parm))
	    warn_if_shadowing (parm);
	}
      return arg_types;
    }
}

/* Take apart the current scope and return a c_arg_info structure with
   info on a parameter list just parsed.

   This structure is later fed to 'grokparms' and 'store_parm_decls'.

   ELLIPSIS being true means the argument list ended in '...' so don't
   append a sentinel (void_list_node) to the end of the type-list.  */

struct c_arg_info *
get_parm_info (bool ellipsis)
{
  struct c_binding *b = current_scope->bindings;
  struct c_arg_info *arg_info = XOBNEW (&parser_obstack,
					struct c_arg_info);
  tree parms    = 0;
  tree tags     = 0;
  tree types    = 0;
  tree others   = 0;

  static bool explained_incomplete_types = false;
  bool gave_void_only_once_err = false;

  arg_info->parms = 0;
  arg_info->tags = 0;
  arg_info->types = 0;
  arg_info->others = 0;
  arg_info->pending_sizes = 0;
  arg_info->had_vla_unspec = current_scope->had_vla_unspec;

  /* The bindings in this scope must not get put into a block.
     We will take care of deleting the binding nodes.  */
  current_scope->bindings = 0;

  /* This function is only called if there was *something* on the
     parameter list.  */
  gcc_assert (b);

  /* A parameter list consisting solely of 'void' indicates that the
     function takes no arguments.  But if the 'void' is qualified
     (by 'const' or 'volatile'), or has a storage class specifier
     ('register'), then the behavior is undefined; issue an error.
     Typedefs for 'void' are OK (see DR#157).  */
  if (b->prev == 0			    /* one binding */
      && TREE_CODE (b->decl) == PARM_DECL   /* which is a parameter */
      && !DECL_NAME (b->decl)               /* anonymous */
      && VOID_TYPE_P (TREE_TYPE (b->decl))) /* of void type */
    {
      if (TREE_THIS_VOLATILE (b->decl)
	  || TREE_READONLY (b->decl)
	  || C_DECL_REGISTER (b->decl))
	error ("%<void%> as only parameter may not be qualified");

      /* There cannot be an ellipsis.  */
      if (ellipsis)
	error ("%<void%> must be the only parameter");

      arg_info->types = void_list_node;
      return arg_info;
    }

  if (!ellipsis)
    types = void_list_node;

  /* Break up the bindings list into parms, tags, types, and others;
     apply sanity checks; purge the name-to-decl bindings.  */
  while (b)
    {
      tree decl = b->decl;
      tree type = TREE_TYPE (decl);
      const char *keyword;

      switch (TREE_CODE (decl))
	{
	case PARM_DECL:
	  if (b->id)
	    {
	      gcc_assert (I_SYMBOL_BINDING (b->id) == b);
	      I_SYMBOL_BINDING (b->id) = b->shadowed;
	    }

	  /* Check for forward decls that never got their actual decl.  */
	  if (TREE_ASM_WRITTEN (decl))
	    error ("parameter %q+D has just a forward declaration", decl);
	  /* Check for (..., void, ...) and issue an error.  */
	  else if (VOID_TYPE_P (type) && !DECL_NAME (decl))
	    {
	      if (!gave_void_only_once_err)
		{
		  error ("%<void%> must be the only parameter");
		  gave_void_only_once_err = true;
		}
	    }
	  else
	    {
	      /* Valid parameter, add it to the list.  */
	      TREE_CHAIN (decl) = parms;
	      parms = decl;

	      /* Since there is a prototype, args are passed in their
		 declared types.  The back end may override this later.  */
	      DECL_ARG_TYPE (decl) = type;
	      types = tree_cons (0, type, types);
	    }
	  break;

	case ENUMERAL_TYPE: keyword = "enum"; goto tag;
	case UNION_TYPE:    keyword = "union"; goto tag;
	case RECORD_TYPE:   keyword = "struct"; goto tag;
	tag:
	  /* Types may not have tag-names, in which case the type
	     appears in the bindings list with b->id NULL.  */
	  if (b->id)
	    {
	      gcc_assert (I_TAG_BINDING (b->id) == b);
	      I_TAG_BINDING (b->id) = b->shadowed;
	    }

	  /* Warn about any struct, union or enum tags defined in a
	     parameter list.  The scope of such types is limited to
	     the parameter list, which is rarely if ever desirable
	     (it's impossible to call such a function with type-
	     correct arguments).  An anonymous union parm type is
	     meaningful as a GNU extension, so don't warn for that.  */
	  if (TREE_CODE (decl) != UNION_TYPE || b->id != 0)
	    {
	      if (b->id)
		/* The %s will be one of 'struct', 'union', or 'enum'.  */
		warning (0, "%<%s %E%> declared inside parameter list",
			 keyword, b->id);
	      else
		/* The %s will be one of 'struct', 'union', or 'enum'.  */
		warning (0, "anonymous %s declared inside parameter list",
			 keyword);

	      if (!explained_incomplete_types)
		{
		  warning (0, "its scope is only this definition or declaration,"
			   " which is probably not what you want");
		  explained_incomplete_types = true;
		}
	    }

	  tags = tree_cons (b->id, decl, tags);
	  break;

	case CONST_DECL:
	case TYPE_DECL:
	case FUNCTION_DECL:
	  /* CONST_DECLs appear here when we have an embedded enum,
	     and TYPE_DECLs appear here when we have an embedded struct
	     or union.  No warnings for this - we already warned about the
	     type itself.  FUNCTION_DECLs appear when there is an implicit
	     function declaration in the parameter list.  */

	  TREE_CHAIN (decl) = others;
	  others = decl;
	  /* fall through */

	case ERROR_MARK:
	  /* error_mark_node appears here when we have an undeclared
	     variable.  Just throw it away.  */
	  if (b->id)
	    {
	      gcc_assert (I_SYMBOL_BINDING (b->id) == b);
	      I_SYMBOL_BINDING (b->id) = b->shadowed;
	    }
	  break;

	  /* Other things that might be encountered.  */
	case LABEL_DECL:
	case VAR_DECL:
	default:
	  gcc_unreachable ();
	}

      b = free_binding_and_advance (b);
    }

  arg_info->parms = parms;
  arg_info->tags = tags;
  arg_info->types = types;
  arg_info->others = others;
  arg_info->pending_sizes = get_pending_sizes ();
  return arg_info;
}

/* Get the struct, enum or union (CODE says which) with tag NAME.
   Define the tag as a forward-reference if it is not defined.
   Return a c_typespec structure for the type specifier.  */

struct c_typespec
parser_xref_tag (enum tree_code code, tree name)
{
  struct c_typespec ret;
  /* If a cross reference is requested, look up the type
     already defined for this tag and return it.  */

  tree ref = lookup_tag (code, name, 0);
  /* If this is the right type of tag, return what we found.
     (This reference will be shadowed by shadow_tag later if appropriate.)
     If this is the wrong type of tag, do not return it.  If it was the
     wrong type in the same scope, we will have had an error
     message already; if in a different scope and declaring
     a name, pending_xref_error will give an error message; but if in a
     different scope and not declaring a name, this tag should
     shadow the previous declaration of a different type of tag, and
     this would not work properly if we return the reference found.
     (For example, with "struct foo" in an outer scope, "union foo;"
     must shadow that tag with a new one of union type.)  */
  ret.kind = (ref ? ctsk_tagref : ctsk_tagfirstref);
  if (ref && TREE_CODE (ref) == code)
    {
      ret.spec = ref;
      return ret;
    }

  /* If no such tag is yet defined, create a forward-reference node
     and record it as the "definition".
     When a real declaration of this type is found,
     the forward-reference will be altered into a real type.  */

  ref = make_node (code);
  if (code == ENUMERAL_TYPE)
    {
      /* Give the type a default layout like unsigned int
	 to avoid crashing if it does not get defined.  */
      TYPE_MODE (ref) = TYPE_MODE (unsigned_type_node);
      TYPE_ALIGN (ref) = TYPE_ALIGN (unsigned_type_node);
      TYPE_USER_ALIGN (ref) = 0;
      TYPE_UNSIGNED (ref) = 1;
      TYPE_PRECISION (ref) = TYPE_PRECISION (unsigned_type_node);
      TYPE_MIN_VALUE (ref) = TYPE_MIN_VALUE (unsigned_type_node);
      TYPE_MAX_VALUE (ref) = TYPE_MAX_VALUE (unsigned_type_node);
    }

  pushtag (name, ref);

  ret.spec = ref;
  return ret;
}

/* Get the struct, enum or union (CODE says which) with tag NAME.
   Define the tag as a forward-reference if it is not defined.
   Return a tree for the type.  */

tree
xref_tag (enum tree_code code, tree name)
{
  return parser_xref_tag (code, name).spec;
}

/* Make sure that the tag NAME is defined *in the current scope*
   at least as a forward reference.
   CODE says which kind of tag NAME ought to be.  */

tree
start_struct (enum tree_code code, tree name)
{
  /* If there is already a tag defined at this scope
     (as a forward reference), just return it.  */

  tree ref = 0;

  if (name != 0)
    ref = lookup_tag (code, name, 1);
  if (ref && TREE_CODE (ref) == code)
    {
      if (TYPE_SIZE (ref))
	{
	  if (code == UNION_TYPE)
	    error ("redefinition of %<union %E%>", name);
	  else
	    error ("redefinition of %<struct %E%>", name);
	}
      else if (C_TYPE_BEING_DEFINED (ref))
	{
	  if (code == UNION_TYPE)
	    error ("nested redefinition of %<union %E%>", name);
	  else
	    error ("nested redefinition of %<struct %E%>", name);
	  /* Don't create structures that contain themselves.  */
	  ref = NULL_TREE;
	}
    }

  /* Otherwise create a forward-reference just so the tag is in scope.  */

  if (ref == NULL_TREE || TREE_CODE (ref) != code)
    {
      ref = make_node (code);
      pushtag (name, ref);
    }

  C_TYPE_BEING_DEFINED (ref) = 1;
  TYPE_PACKED (ref) = flag_pack_struct;
  return ref;
}

/* Process the specs, declarator and width (NULL if omitted)
   of a structure component, returning a FIELD_DECL node.
   WIDTH is non-NULL for bit-fields only, and is an INTEGER_CST node.

   This is done during the parsing of the struct declaration.
   The FIELD_DECL nodes are chained together and the lot of them
   are ultimately passed to `build_struct' to make the RECORD_TYPE node.  */

tree
grokfield (struct c_declarator *declarator, struct c_declspecs *declspecs,
	   tree width)
{
  tree value;

  if (declarator->kind == cdk_id && declarator->u.id == NULL_TREE
      && width == NULL_TREE)
    {
      /* This is an unnamed decl.

	 If we have something of the form "union { list } ;" then this
	 is the anonymous union extension.  Similarly for struct.

	 If this is something of the form "struct foo;", then
	   If MS extensions are enabled, this is handled as an
	     anonymous struct.
	   Otherwise this is a forward declaration of a structure tag.

	 If this is something of the form "foo;" and foo is a TYPE_DECL, then
	   If MS extensions are enabled and foo names a structure, then
	     again this is an anonymous struct.
	   Otherwise this is an error.

	 Oh what a horrid tangled web we weave.  I wonder if MS consciously
	 took this from Plan 9 or if it was an accident of implementation
	 that took root before someone noticed the bug...  */

      tree type = declspecs->type;
      bool type_ok = (TREE_CODE (type) == RECORD_TYPE
		      || TREE_CODE (type) == UNION_TYPE);
      bool ok = false;

      if (type_ok
	  && (flag_ms_extensions || !declspecs->typedef_p))
	{
	  if (flag_ms_extensions)
	    ok = true;
	  else if (flag_iso)
	    ok = false;
	  else if (TYPE_NAME (type) == NULL)
	    ok = true;
	  else
	    ok = false;
	}
      if (!ok)
	{
	  pedwarn ("declaration does not declare anything");
	  return NULL_TREE;
	}
      if (pedantic)
	pedwarn ("ISO C doesn%'t support unnamed structs/unions");
    }

  value = grokdeclarator (declarator, declspecs, FIELD, false,
			  width ? &width : NULL);

  finish_decl (value, NULL_TREE, NULL_TREE);
  DECL_INITIAL (value) = width;

  return value;
}

/* Generate an error for any duplicate field names in FIELDLIST.  Munge
   the list such that this does not present a problem later.  */

static void
detect_field_duplicates (tree fieldlist)
{
  tree x, y;
  int timeout = 10;

  /* First, see if there are more than "a few" fields.
     This is trivially true if there are zero or one fields.  */
  if (!fieldlist)
    return;
  x = TREE_CHAIN (fieldlist);
  if (!x)
    return;
  do {
    timeout--;
    x = TREE_CHAIN (x);
  } while (timeout > 0 && x);

  /* If there were "few" fields, avoid the overhead of allocating
     a hash table.  Instead just do the nested traversal thing.  */
  if (timeout > 0)
    {
      for (x = TREE_CHAIN (fieldlist); x ; x = TREE_CHAIN (x))
	if (DECL_NAME (x))
	  {
	    for (y = fieldlist; y != x; y = TREE_CHAIN (y))
	      if (DECL_NAME (y) == DECL_NAME (x))
		{
		  error ("duplicate member %q+D", x);
		  DECL_NAME (x) = NULL_TREE;
		}
	  }
    }
  else
    {
      htab_t htab = htab_create (37, htab_hash_pointer, htab_eq_pointer, NULL);
      void **slot;

      for (x = fieldlist; x ; x = TREE_CHAIN (x))
	if ((y = DECL_NAME (x)) != 0)
	  {
	    slot = htab_find_slot (htab, y, INSERT);
	    if (*slot)
	      {
		error ("duplicate member %q+D", x);
		DECL_NAME (x) = NULL_TREE;
	      }
	    *slot = y;
	  }

      htab_delete (htab);
    }
}

/* Fill in the fields of a RECORD_TYPE or UNION_TYPE node, T.
   FIELDLIST is a chain of FIELD_DECL nodes for the fields.
   ATTRIBUTES are attributes to be applied to the structure.  */

tree
finish_struct (tree t, tree fieldlist, tree attributes)
{
  tree x;
  bool toplevel = file_scope == current_scope;
  int saw_named_field;

  /* If this type was previously laid out as a forward reference,
     make sure we lay it out again.  */

  TYPE_SIZE (t) = 0;

  decl_attributes (&t, attributes, (int) ATTR_FLAG_TYPE_IN_PLACE);

  if (pedantic)
    {
      for (x = fieldlist; x; x = TREE_CHAIN (x))
	if (DECL_NAME (x) != 0)
	  break;

      if (x == 0)
	{
	  if (TREE_CODE (t) == UNION_TYPE)
	    {
	      if (fieldlist)
		pedwarn ("union has no named members");
	      else
		pedwarn ("union has no members");
	    }
	  else
	    {
	      if (fieldlist)
		pedwarn ("struct has no named members");
	      else
		pedwarn ("struct has no members");
	    }
	}
    }

  /* Install struct as DECL_CONTEXT of each field decl.
     Also process specified field sizes, found in the DECL_INITIAL,
     storing 0 there after the type has been changed to precision equal
     to its width, rather than the precision of the specified standard
     type.  (Correct layout requires the original type to have been preserved
     until now.)  */

  saw_named_field = 0;
  for (x = fieldlist; x; x = TREE_CHAIN (x))
    {
      if (TREE_TYPE (x) == error_mark_node)
	continue;

      DECL_CONTEXT (x) = t;

      if (TYPE_PACKED (t) && TYPE_ALIGN (TREE_TYPE (x)) > BITS_PER_UNIT)
	DECL_PACKED (x) = 1;

      /* If any field is const, the structure type is pseudo-const.  */
      if (TREE_READONLY (x))
	C_TYPE_FIELDS_READONLY (t) = 1;
      else
	{
	  /* A field that is pseudo-const makes the structure likewise.  */
	  tree t1 = TREE_TYPE (x);
	  while (TREE_CODE (t1) == ARRAY_TYPE)
	    t1 = TREE_TYPE (t1);
	  if ((TREE_CODE (t1) == RECORD_TYPE || TREE_CODE (t1) == UNION_TYPE)
	      && C_TYPE_FIELDS_READONLY (t1))
	    C_TYPE_FIELDS_READONLY (t) = 1;
	}

      /* Any field that is volatile means variables of this type must be
	 treated in some ways as volatile.  */
      if (TREE_THIS_VOLATILE (x))
	C_TYPE_FIELDS_VOLATILE (t) = 1;

      /* Any field of nominal variable size implies structure is too.  */
      if (C_DECL_VARIABLE_SIZE (x))
	C_TYPE_VARIABLE_SIZE (t) = 1;

      if (DECL_INITIAL (x))
	{
	  unsigned HOST_WIDE_INT width = tree_low_cst (DECL_INITIAL (x), 1);
	  DECL_SIZE (x) = bitsize_int (width);
	  DECL_BIT_FIELD (x) = 1;
	  SET_DECL_C_BIT_FIELD (x);
	}

      /* Detect flexible array member in an invalid context.  */
      if (TREE_CODE (TREE_TYPE (x)) == ARRAY_TYPE
	  && TYPE_SIZE (TREE_TYPE (x)) == NULL_TREE
	  && TYPE_DOMAIN (TREE_TYPE (x)) != NULL_TREE
	  && TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (x))) == NULL_TREE)
	{
	  if (TREE_CODE (t) == UNION_TYPE)
	    {
	      error ("%Jflexible array member in union", x);
	      TREE_TYPE (x) = error_mark_node;
	    }
	  else if (TREE_CHAIN (x) != NULL_TREE)
	    {
	      error ("%Jflexible array member not at end of struct", x);
	      TREE_TYPE (x) = error_mark_node;
	    }
	  else if (!saw_named_field)
	    {
	      error ("%Jflexible array member in otherwise empty struct", x);
	      TREE_TYPE (x) = error_mark_node;
	    }
	}

      if (pedantic && !in_system_header && TREE_CODE (t) == RECORD_TYPE
	  && flexible_array_type_p (TREE_TYPE (x)))
	pedwarn ("%Jinvalid use of structure with flexible array member", x);

      if (DECL_NAME (x))
	saw_named_field = 1;
    }

  detect_field_duplicates (fieldlist);

  /* Now we have the nearly final fieldlist.  Record it,
     then lay out the structure or union (including the fields).  */

  TYPE_FIELDS (t) = fieldlist;

  layout_type (t);

  /* Give bit-fields their proper types.  */
  {
    tree *fieldlistp = &fieldlist;
    while (*fieldlistp)
      if (TREE_CODE (*fieldlistp) == FIELD_DECL && DECL_INITIAL (*fieldlistp)
	  && TREE_TYPE (*fieldlistp) != error_mark_node)
	{
	  unsigned HOST_WIDE_INT width
	    = tree_low_cst (DECL_INITIAL (*fieldlistp), 1);
	  tree type = TREE_TYPE (*fieldlistp);
	  if (width != TYPE_PRECISION (type))
	    {
	      TREE_TYPE (*fieldlistp)
		= c_build_bitfield_integer_type (width, TYPE_UNSIGNED (type));
	      DECL_MODE (*fieldlistp) = TYPE_MODE (TREE_TYPE (*fieldlistp));
	    }
	  DECL_INITIAL (*fieldlistp) = 0;
	}
      else
	fieldlistp = &TREE_CHAIN (*fieldlistp);
  }

  /* Now we have the truly final field list.
     Store it in this type and in the variants.  */

  TYPE_FIELDS (t) = fieldlist;

  /* If there are lots of fields, sort so we can look through them fast.
     We arbitrarily consider 16 or more elts to be "a lot".  */

  {
    int len = 0;

    for (x = fieldlist; x; x = TREE_CHAIN (x))
      {
	if (len > 15 || DECL_NAME (x) == NULL)
	  break;
	len += 1;
      }

    if (len > 15)
      {
	tree *field_array;
	struct lang_type *space;
	struct sorted_fields_type *space2;

	len += list_length (x);

	/* Use the same allocation policy here that make_node uses, to
	  ensure that this lives as long as the rest of the struct decl.
	  All decls in an inline function need to be saved.  */

	space = GGC_CNEW (struct lang_type);
	space2 = GGC_NEWVAR (struct sorted_fields_type,
			     sizeof (struct sorted_fields_type) + len * sizeof (tree));

	len = 0;
	space->s = space2;
	field_array = &space2->elts[0];
	for (x = fieldlist; x; x = TREE_CHAIN (x))
	  {
	    field_array[len++] = x;

	    /* If there is anonymous struct or union, break out of the loop.  */
	    if (DECL_NAME (x) == NULL)
	      break;
	  }
	/* Found no anonymous struct/union.  Add the TYPE_LANG_SPECIFIC.  */
	if (x == NULL)
	  {
	    TYPE_LANG_SPECIFIC (t) = space;
	    TYPE_LANG_SPECIFIC (t)->s->len = len;
	    field_array = TYPE_LANG_SPECIFIC (t)->s->elts;
	    qsort (field_array, len, sizeof (tree), field_decl_cmp);
	  }
      }
  }

  for (x = TYPE_MAIN_VARIANT (t); x; x = TYPE_NEXT_VARIANT (x))
    {
      TYPE_FIELDS (x) = TYPE_FIELDS (t);
      TYPE_LANG_SPECIFIC (x) = TYPE_LANG_SPECIFIC (t);
      C_TYPE_FIELDS_READONLY (x) = C_TYPE_FIELDS_READONLY (t);
      C_TYPE_FIELDS_VOLATILE (x) = C_TYPE_FIELDS_VOLATILE (t);
      C_TYPE_VARIABLE_SIZE (x) = C_TYPE_VARIABLE_SIZE (t);
    }

  /* If this was supposed to be a transparent union, but we can't
     make it one, warn and turn off the flag.  */
  if (TREE_CODE (t) == UNION_TYPE
      && TYPE_TRANSPARENT_UNION (t)
      && (!TYPE_FIELDS (t) || TYPE_MODE (t) != DECL_MODE (TYPE_FIELDS (t))))
    {
      TYPE_TRANSPARENT_UNION (t) = 0;
      warning (0, "union cannot be made transparent");
    }

  /* If this structure or union completes the type of any previous
     variable declaration, lay it out and output its rtl.  */
  for (x = C_TYPE_INCOMPLETE_VARS (TYPE_MAIN_VARIANT (t));
       x;
       x = TREE_CHAIN (x))
    {
      tree decl = TREE_VALUE (x);
      if (TREE_CODE (TREE_TYPE (decl)) == ARRAY_TYPE)
	layout_array_type (TREE_TYPE (decl));
      if (TREE_CODE (decl) != TYPE_DECL)
	{
	  layout_decl (decl, 0);
	  if (c_dialect_objc ())
	    objc_check_decl (decl);
	  rest_of_decl_compilation (decl, toplevel, 0);
	  if (!toplevel)
	    expand_decl (decl);
	}
    }
  C_TYPE_INCOMPLETE_VARS (TYPE_MAIN_VARIANT (t)) = 0;

  /* Finish debugging output for this type.  */
  rest_of_type_compilation (t, toplevel);

  /* If we're inside a function proper, i.e. not file-scope and not still
     parsing parameters, then arrange for the size of a variable sized type
     to be bound now.  */
  if (cur_stmt_list && variably_modified_type_p (t, NULL_TREE))
    add_stmt (build_stmt (DECL_EXPR, build_decl (TYPE_DECL, NULL, t)));

  return t;
}

/* Lay out the type T, and its element type, and so on.  */

static void
layout_array_type (tree t)
{
  if (TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE)
    layout_array_type (TREE_TYPE (t));
  layout_type (t);
}

/* Begin compiling the definition of an enumeration type.
   NAME is its name (or null if anonymous).
   Returns the type object, as yet incomplete.
   Also records info about it so that build_enumerator
   may be used to declare the individual values as they are read.  */

tree
start_enum (tree name)
{
  tree enumtype = 0;

  /* If this is the real definition for a previous forward reference,
     fill in the contents in the same object that used to be the
     forward reference.  */

  if (name != 0)
    enumtype = lookup_tag (ENUMERAL_TYPE, name, 1);

  if (enumtype == 0 || TREE_CODE (enumtype) != ENUMERAL_TYPE)
    {
      enumtype = make_node (ENUMERAL_TYPE);
      pushtag (name, enumtype);
    }

  if (C_TYPE_BEING_DEFINED (enumtype))
    error ("nested redefinition of %<enum %E%>", name);

  C_TYPE_BEING_DEFINED (enumtype) = 1;

  if (TYPE_VALUES (enumtype) != 0)
    {
      /* This enum is a named one that has been declared already.  */
      error ("redeclaration of %<enum %E%>", name);

      /* Completely replace its old definition.
	 The old enumerators remain defined, however.  */
      TYPE_VALUES (enumtype) = 0;
    }

  enum_next_value = integer_zero_node;
  enum_overflow = 0;

  if (flag_short_enums)
    TYPE_PACKED (enumtype) = 1;

  return enumtype;
}

/* After processing and defining all the values of an enumeration type,
   install their decls in the enumeration type and finish it off.
   ENUMTYPE is the type object, VALUES a list of decl-value pairs,
   and ATTRIBUTES are the specified attributes.
   Returns ENUMTYPE.  */

tree
finish_enum (tree enumtype, tree values, tree attributes)
{
  tree pair, tem;
  tree minnode = 0, maxnode = 0;
  int precision, unsign;
  bool toplevel = (file_scope == current_scope);
  struct lang_type *lt;

  decl_attributes (&enumtype, attributes, (int) ATTR_FLAG_TYPE_IN_PLACE);

  /* Calculate the maximum value of any enumerator in this type.  */

  if (values == error_mark_node)
    minnode = maxnode = integer_zero_node;
  else
    {
      minnode = maxnode = TREE_VALUE (values);
      for (pair = TREE_CHAIN (values); pair; pair = TREE_CHAIN (pair))
	{
	  tree value = TREE_VALUE (pair);
	  if (tree_int_cst_lt (maxnode, value))
	    maxnode = value;
	  if (tree_int_cst_lt (value, minnode))
	    minnode = value;
	}
    }

  /* Construct the final type of this enumeration.  It is the same
     as one of the integral types - the narrowest one that fits, except
     that normally we only go as narrow as int - and signed iff any of
     the values are negative.  */
  unsign = (tree_int_cst_sgn (minnode) >= 0);
  precision = MAX (min_precision (minnode, unsign),
		   min_precision (maxnode, unsign));

  if (TYPE_PACKED (enumtype) || precision > TYPE_PRECISION (integer_type_node))
    {
      tem = c_common_type_for_size (precision, unsign);
      if (tem == NULL)
	{
	  warning (0, "enumeration values exceed range of largest integer");
	  tem = long_long_integer_type_node;
	}
    }
  else
    tem = unsign ? unsigned_type_node : integer_type_node;

  TYPE_MIN_VALUE (enumtype) = TYPE_MIN_VALUE (tem);
  TYPE_MAX_VALUE (enumtype) = TYPE_MAX_VALUE (tem);
  TYPE_UNSIGNED (enumtype) = TYPE_UNSIGNED (tem);
  TYPE_SIZE (enumtype) = 0;

  /* If the precision of the type was specific with an attribute and it
     was too small, give an error.  Otherwise, use it.  */
  if (TYPE_PRECISION (enumtype))
    {
      if (precision > TYPE_PRECISION (enumtype))
	error ("specified mode too small for enumeral values");
    }
  else
    TYPE_PRECISION (enumtype) = TYPE_PRECISION (tem);

  layout_type (enumtype);

  if (values != error_mark_node)
    {
      /* Change the type of the enumerators to be the enum type.  We
	 need to do this irrespective of the size of the enum, for
	 proper type checking.  Replace the DECL_INITIALs of the
	 enumerators, and the value slots of the list, with copies
	 that have the enum type; they cannot be modified in place
	 because they may be shared (e.g.  integer_zero_node) Finally,
	 change the purpose slots to point to the names of the decls.  */
      for (pair = values; pair; pair = TREE_CHAIN (pair))
	{
	  tree enu = TREE_PURPOSE (pair);
	  tree ini = DECL_INITIAL (enu);

	  TREE_TYPE (enu) = enumtype;

	  /* The ISO C Standard mandates enumerators to have type int,
	     even though the underlying type of an enum type is
	     unspecified.  Here we convert any enumerators that fit in
	     an int to type int, to avoid promotions to unsigned types
	     when comparing integers with enumerators that fit in the
	     int range.  When -pedantic is given, build_enumerator()
	     would have already taken care of those that don't fit.  */
	  if (int_fits_type_p (ini, integer_type_node))
	    tem = integer_type_node;
	  else
	    tem = enumtype;
	  ini = convert (tem, ini);

	  DECL_INITIAL (enu) = ini;
	  TREE_PURPOSE (pair) = DECL_NAME (enu);
	  TREE_VALUE (pair) = ini;
	}

      TYPE_VALUES (enumtype) = values;
    }

  /* Record the min/max values so that we can warn about bit-field
     enumerations that are too small for the values.  */
  lt = GGC_CNEW (struct lang_type);
  lt->enum_min = minnode;
  lt->enum_max = maxnode;
  TYPE_LANG_SPECIFIC (enumtype) = lt;

  /* Fix up all variant types of this enum type.  */
  for (tem = TYPE_MAIN_VARIANT (enumtype); tem; tem = TYPE_NEXT_VARIANT (tem))
    {
      if (tem == enumtype)
	continue;
      TYPE_VALUES (tem) = TYPE_VALUES (enumtype);
      TYPE_MIN_VALUE (tem) = TYPE_MIN_VALUE (enumtype);
      TYPE_MAX_VALUE (tem) = TYPE_MAX_VALUE (enumtype);
      TYPE_SIZE (tem) = TYPE_SIZE (enumtype);
      TYPE_SIZE_UNIT (tem) = TYPE_SIZE_UNIT (enumtype);
      TYPE_MODE (tem) = TYPE_MODE (enumtype);
      TYPE_PRECISION (tem) = TYPE_PRECISION (enumtype);
      TYPE_ALIGN (tem) = TYPE_ALIGN (enumtype);
      TYPE_USER_ALIGN (tem) = TYPE_USER_ALIGN (enumtype);
      TYPE_UNSIGNED (tem) = TYPE_UNSIGNED (enumtype);
      TYPE_LANG_SPECIFIC (tem) = TYPE_LANG_SPECIFIC (enumtype);
    }

  /* Finish debugging output for this type.  */
  rest_of_type_compilation (enumtype, toplevel);

  return enumtype;
}

/* Build and install a CONST_DECL for one value of the
   current enumeration type (one that was begun with start_enum).
   Return a tree-list containing the CONST_DECL and its value.
   Assignment of sequential values by default is handled here.  */

tree
build_enumerator (tree name, tree value)
{
  tree decl, type;

  /* Validate and default VALUE.  */

  if (value != 0)
    {
      /* Don't issue more errors for error_mark_node (i.e. an
	 undeclared identifier) - just ignore the value expression.  */
      if (value == error_mark_node)
	value = 0;
      else if (!INTEGRAL_TYPE_P (TREE_TYPE (value))
	       || TREE_CODE (value) != INTEGER_CST)
	{
	  error ("enumerator value for %qE is not an integer constant", name);
	  value = 0;
	}
      else
	{
	  value = default_conversion (value);
	  constant_expression_warning (value);
	}
    }

  /* Default based on previous value.  */
  /* It should no longer be possible to have NON_LVALUE_EXPR
     in the default.  */
  if (value == 0)
    {
      value = enum_next_value;
      if (enum_overflow)
	error ("overflow in enumeration values");
    }

  if (pedantic && !int_fits_type_p (value, integer_type_node))
    {
      pedwarn ("ISO C restricts enumerator values to range of %<int%>");
      /* XXX This causes -pedantic to change the meaning of the program.
	 Remove?  -zw 2004-03-15  */
      value = convert (integer_type_node, value);
    }

  /* Set basis for default for next value.  */
  enum_next_value = build_binary_op (PLUS_EXPR, value, integer_one_node, 0);
  enum_overflow = tree_int_cst_lt (enum_next_value, value);

  /* Now create a declaration for the enum value name.  */

  type = TREE_TYPE (value);
  type = c_common_type_for_size (MAX (TYPE_PRECISION (type),
				      TYPE_PRECISION (integer_type_node)),
				 (TYPE_PRECISION (type)
				  >= TYPE_PRECISION (integer_type_node)
				  && TYPE_UNSIGNED (type)));

  decl = build_decl (CONST_DECL, name, type);
  DECL_INITIAL (decl) = convert (type, value);
  pushdecl (decl);

  return tree_cons (decl, value, NULL_TREE);
}


/* Create the FUNCTION_DECL for a function definition.
   DECLSPECS, DECLARATOR and ATTRIBUTES are the parts of
   the declaration; they describe the function's name and the type it returns,
   but twisted together in a fashion that parallels the syntax of C.

   This function creates a binding context for the function body
   as well as setting up the FUNCTION_DECL in current_function_decl.

   Returns 1 on success.  If the DECLARATOR is not suitable for a function
   (it defines a datum instead), we return 0, which tells
   yyparse to report a parse error.  */

int
start_function (struct c_declspecs *declspecs, struct c_declarator *declarator,
		tree attributes)
{
  tree decl1, old_decl;
  tree restype, resdecl;
  struct c_label_context_se *nstack_se;
  struct c_label_context_vm *nstack_vm;

  current_function_returns_value = 0;  /* Assume, until we see it does.  */
  current_function_returns_null = 0;
  current_function_returns_abnormally = 0;
  warn_about_return_type = 0;
  c_switch_stack = NULL;

  nstack_se = XOBNEW (&parser_obstack, struct c_label_context_se);
  nstack_se->labels_def = NULL;
  nstack_se->labels_used = NULL;
  nstack_se->next = label_context_stack_se;
  label_context_stack_se = nstack_se;

  nstack_vm = XOBNEW (&parser_obstack, struct c_label_context_vm);
  nstack_vm->labels_def = NULL;
  nstack_vm->labels_used = NULL;
  nstack_vm->scope = 0;
  nstack_vm->next = label_context_stack_vm;
  label_context_stack_vm = nstack_vm;

  /* Indicate no valid break/continue context by setting these variables
     to some non-null, non-label value.  We'll notice and emit the proper
     error message in c_finish_bc_stmt.  */
  c_break_label = c_cont_label = size_zero_node;

  decl1 = grokdeclarator (declarator, declspecs, FUNCDEF, true, NULL);

  /* If the declarator is not suitable for a function definition,
     cause a syntax error.  */
  if (decl1 == 0)
    {
      label_context_stack_se = label_context_stack_se->next;
      label_context_stack_vm = label_context_stack_vm->next;
      return 0;
    }

  decl_attributes (&decl1, attributes, 0);

  if (DECL_DECLARED_INLINE_P (decl1)
      && DECL_UNINLINABLE (decl1)
      && lookup_attribute ("noinline", DECL_ATTRIBUTES (decl1)))
    warning (OPT_Wattributes, "inline function %q+D given attribute noinline",
	     decl1);

  /* Handle gnu_inline attribute.  */
  if (declspecs->inline_p
      && !flag_gnu89_inline
      && TREE_CODE (decl1) == FUNCTION_DECL
      && lookup_attribute ("gnu_inline", DECL_ATTRIBUTES (decl1)))
    {
      if (declspecs->storage_class != csc_static)
	DECL_EXTERNAL (decl1) = !DECL_EXTERNAL (decl1);
    }

  announce_function (decl1);

  if (!COMPLETE_OR_VOID_TYPE_P (TREE_TYPE (TREE_TYPE (decl1))))
    {
      error ("return type is an incomplete type");
      /* Make it return void instead.  */
      TREE_TYPE (decl1)
	= build_function_type (void_type_node,
			       TYPE_ARG_TYPES (TREE_TYPE (decl1)));
    }

  if (warn_about_return_type)
    pedwarn_c99 ("return type defaults to %<int%>");

  /* Make the init_value nonzero so pushdecl knows this is not tentative.
     error_mark_node is replaced below (in pop_scope) with the BLOCK.  */
  DECL_INITIAL (decl1) = error_mark_node;

  /* If this definition isn't a prototype and we had a prototype declaration
     before, copy the arg type info from that prototype.  */
  old_decl = lookup_name_in_scope (DECL_NAME (decl1), current_scope);
  if (old_decl && TREE_CODE (old_decl) != FUNCTION_DECL)
    old_decl = 0;
  current_function_prototype_locus = UNKNOWN_LOCATION;
  current_function_prototype_built_in = false;
  current_function_prototype_arg_types = NULL_TREE;
  if (TYPE_ARG_TYPES (TREE_TYPE (decl1)) == 0)
    {
      if (old_decl != 0 && TREE_CODE (TREE_TYPE (old_decl)) == FUNCTION_TYPE
	  && comptypes (TREE_TYPE (TREE_TYPE (decl1)),
			TREE_TYPE (TREE_TYPE (old_decl))))
	{
	  TREE_TYPE (decl1) = composite_type (TREE_TYPE (old_decl),
					      TREE_TYPE (decl1));
	  current_function_prototype_locus = DECL_SOURCE_LOCATION (old_decl);
	  current_function_prototype_built_in
	    = C_DECL_BUILTIN_PROTOTYPE (old_decl);
	  current_function_prototype_arg_types
	    = TYPE_ARG_TYPES (TREE_TYPE (decl1));
	}
      if (TREE_PUBLIC (decl1))
	{
	  /* If there is an external prototype declaration of this
	     function, record its location but do not copy information
	     to this decl.  This may be an invisible declaration
	     (built-in or in a scope which has finished) or simply
	     have more refined argument types than any declaration
	     found above.  */
	  struct c_binding *b;
	  for (b = I_SYMBOL_BINDING (DECL_NAME (decl1)); b; b = b->shadowed)
	    if (B_IN_SCOPE (b, external_scope))
	      break;
	  if (b)
	    {
	      tree ext_decl, ext_type;
	      ext_decl = b->decl;
	      ext_type = b->type ? b->type : TREE_TYPE (ext_decl);
	      if (TREE_CODE (ext_type) == FUNCTION_TYPE
		  && comptypes (TREE_TYPE (TREE_TYPE (decl1)),
				TREE_TYPE (ext_type)))
		{
		  current_function_prototype_locus
		    = DECL_SOURCE_LOCATION (ext_decl);
		  current_function_prototype_built_in
		    = C_DECL_BUILTIN_PROTOTYPE (ext_decl);
		  current_function_prototype_arg_types
		    = TYPE_ARG_TYPES (ext_type);
		}
	    }
	}
    }

  /* Optionally warn of old-fashioned def with no previous prototype.  */
  if (warn_strict_prototypes
      && old_decl != error_mark_node
      && TYPE_ARG_TYPES (TREE_TYPE (decl1)) == 0
      && C_DECL_ISNT_PROTOTYPE (old_decl))
    warning (OPT_Wstrict_prototypes,
	     "function declaration isn%'t a prototype");
  /* Optionally warn of any global def with no previous prototype.  */
  else if (warn_missing_prototypes
	   && old_decl != error_mark_node
	   && TREE_PUBLIC (decl1)
	   && !MAIN_NAME_P (DECL_NAME (decl1))
	   && C_DECL_ISNT_PROTOTYPE (old_decl))
    warning (OPT_Wmissing_prototypes, "no previous prototype for %q+D", decl1);
  /* Optionally warn of any def with no previous prototype
     if the function has already been used.  */
  else if (warn_missing_prototypes
	   && old_decl != 0
	   && old_decl != error_mark_node
	   && TREE_USED (old_decl)
	   && TYPE_ARG_TYPES (TREE_TYPE (old_decl)) == 0)
    warning (OPT_Wmissing_prototypes,
	     "%q+D was used with no prototype before its definition", decl1);
  /* Optionally warn of any global def with no previous declaration.  */
  else if (warn_missing_declarations
	   && TREE_PUBLIC (decl1)
	   && old_decl == 0
	   && !MAIN_NAME_P (DECL_NAME (decl1)))
    warning (OPT_Wmissing_declarations, "no previous declaration for %q+D",
	     decl1);
  /* Optionally warn of any def with no previous declaration
     if the function has already been used.  */
  else if (warn_missing_declarations
	   && old_decl != 0
	   && old_decl != error_mark_node
	   && TREE_USED (old_decl)
	   && C_DECL_IMPLICIT (old_decl))
    warning (OPT_Wmissing_declarations,
	     "%q+D was used with no declaration before its definition", decl1);

  /* This function exists in static storage.
     (This does not mean `static' in the C sense!)  */
  TREE_STATIC (decl1) = 1;

  /* A nested function is not global.  */
  if (current_function_decl != 0)
    TREE_PUBLIC (decl1) = 0;

  /* This is the earliest point at which we might know the assembler
     name of the function.  Thus, if it's set before this, die horribly.  */
  gcc_assert (!DECL_ASSEMBLER_NAME_SET_P (decl1));

  /* If #pragma weak was used, mark the decl weak now.  */
  if (current_scope == file_scope)
    maybe_apply_pragma_weak (decl1);

  /* Warn for unlikely, improbable, or stupid declarations of `main'.  */
  if (warn_main > 0 && MAIN_NAME_P (DECL_NAME (decl1)))
    {
      tree args;
      int argct = 0;

      if (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (decl1)))
	  != integer_type_node)
	pedwarn ("return type of %q+D is not %<int%>", decl1);

      for (args = TYPE_ARG_TYPES (TREE_TYPE (decl1)); args;
	   args = TREE_CHAIN (args))
	{
	  tree type = args ? TREE_VALUE (args) : 0;

	  if (type == void_type_node)
	    break;

	  ++argct;
	  switch (argct)
	    {
	    case 1:
	      if (TYPE_MAIN_VARIANT (type) != integer_type_node)
		pedwarn ("first argument of %q+D should be %<int%>", decl1);
	      break;

	    case 2:
	      if (TREE_CODE (type) != POINTER_TYPE
		  || TREE_CODE (TREE_TYPE (type)) != POINTER_TYPE
		  || (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (type)))
		      != char_type_node))
		pedwarn ("second argument of %q+D should be %<char **%>",
			 decl1);
	      break;

	    case 3:
	      if (TREE_CODE (type) != POINTER_TYPE
		  || TREE_CODE (TREE_TYPE (type)) != POINTER_TYPE
		  || (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (type)))
		      != char_type_node))
		pedwarn ("third argument of %q+D should probably be "
			 "%<char **%>", decl1);
	      break;
	    }
	}

      /* It is intentional that this message does not mention the third
	 argument because it's only mentioned in an appendix of the
	 standard.  */
      if (argct > 0 && (argct < 2 || argct > 3))
	pedwarn ("%q+D takes only zero or two arguments", decl1);

      if (!TREE_PUBLIC (decl1))
	pedwarn ("%q+D is normally a non-static function", decl1);
    }

  /* Record the decl so that the function name is defined.
     If we already have a decl for this name, and it is a FUNCTION_DECL,
     use the old decl.  */

  current_function_decl = pushdecl (decl1);

  push_scope ();
  declare_parm_level ();

  restype = TREE_TYPE (TREE_TYPE (current_function_decl));
  /* Promote the value to int before returning it.  */
  if (c_promoting_integer_type_p (restype))
    {
      /* It retains unsignedness if not really getting wider.  */
      if (TYPE_UNSIGNED (restype)
	  && (TYPE_PRECISION (restype)
		  == TYPE_PRECISION (integer_type_node)))
	restype = unsigned_type_node;
      else
	restype = integer_type_node;
    }

  resdecl = build_decl (RESULT_DECL, NULL_TREE, restype);
  DECL_ARTIFICIAL (resdecl) = 1;
  DECL_IGNORED_P (resdecl) = 1;
  DECL_RESULT (current_function_decl) = resdecl;

  start_fname_decls ();

  return 1;
}

/* Subroutine of store_parm_decls which handles new-style function
   definitions (prototype format). The parms already have decls, so we
   need only record them as in effect and complain if any redundant
   old-style parm decls were written.  */
static void
store_parm_decls_newstyle (tree fndecl, const struct c_arg_info *arg_info)
{
  tree decl;

  if (current_scope->bindings)
    {
      error ("%Jold-style parameter declarations in prototyped "
	     "function definition", fndecl);

      /* Get rid of the old-style declarations.  */
      pop_scope ();
      push_scope ();
    }
  /* Don't issue this warning for nested functions, and don't issue this
     warning if we got here because ARG_INFO_TYPES was error_mark_node
     (this happens when a function definition has just an ellipsis in
     its parameter list).  */
  else if (!in_system_header && !current_function_scope
	   && arg_info->types != error_mark_node)
    warning (OPT_Wtraditional,
	     "%Jtraditional C rejects ISO C style function definitions",
	     fndecl);

  /* Now make all the parameter declarations visible in the function body.
     We can bypass most of the grunt work of pushdecl.  */
  for (decl = arg_info->parms; decl; decl = TREE_CHAIN (decl))
    {
      DECL_CONTEXT (decl) = current_function_decl;
      if (DECL_NAME (decl))
	{
	  bind (DECL_NAME (decl), decl, current_scope,
		/*invisible=*/false, /*nested=*/false);
	  if (!TREE_USED (decl))
	    warn_if_shadowing (decl);
	}
      else
	error ("%Jparameter name omitted", decl);
    }

  /* Record the parameter list in the function declaration.  */
  DECL_ARGUMENTS (fndecl) = arg_info->parms;

  /* Now make all the ancillary declarations visible, likewise.  */
  for (decl = arg_info->others; decl; decl = TREE_CHAIN (decl))
    {
      DECL_CONTEXT (decl) = current_function_decl;
      if (DECL_NAME (decl))
	bind (DECL_NAME (decl), decl, current_scope,
	      /*invisible=*/false, /*nested=*/false);
    }

  /* And all the tag declarations.  */
  for (decl = arg_info->tags; decl; decl = TREE_CHAIN (decl))
    if (TREE_PURPOSE (decl))
      bind (TREE_PURPOSE (decl), TREE_VALUE (decl), current_scope,
	    /*invisible=*/false, /*nested=*/false);
}

/* Subroutine of store_parm_decls which handles old-style function
   definitions (separate parameter list and declarations).  */

static void
store_parm_decls_oldstyle (tree fndecl, const struct c_arg_info *arg_info)
{
  struct c_binding *b;
  tree parm, decl, last;
  tree parmids = arg_info->parms;
  struct pointer_set_t *seen_args = pointer_set_create ();

  if (!in_system_header)
    warning (OPT_Wold_style_definition, "%Jold-style function definition",
	     fndecl);

  /* Match each formal parameter name with its declaration.  Save each
     decl in the appropriate TREE_PURPOSE slot of the parmids chain.  */
  for (parm = parmids; parm; parm = TREE_CHAIN (parm))
    {
      if (TREE_VALUE (parm) == 0)
	{
	  error ("%Jparameter name missing from parameter list", fndecl);
	  TREE_PURPOSE (parm) = 0;
	  continue;
	}

      b = I_SYMBOL_BINDING (TREE_VALUE (parm));
      if (b && B_IN_CURRENT_SCOPE (b))
	{
	  decl = b->decl;
	  /* If we got something other than a PARM_DECL it is an error.  */
	  if (TREE_CODE (decl) != PARM_DECL)
	    error ("%q+D declared as a non-parameter", decl);
	  /* If the declaration is already marked, we have a duplicate
	     name.  Complain and ignore the duplicate.  */
	  else if (pointer_set_contains (seen_args, decl))
	    {
	      error ("multiple parameters named %q+D", decl);
	      TREE_PURPOSE (parm) = 0;
	      continue;
	    }
	  /* If the declaration says "void", complain and turn it into
	     an int.  */
	  else if (VOID_TYPE_P (TREE_TYPE (decl)))
	    {
	      error ("parameter %q+D declared with void type", decl);
	      TREE_TYPE (decl) = integer_type_node;
	      DECL_ARG_TYPE (decl) = integer_type_node;
	      layout_decl (decl, 0);
	    }
	  warn_if_shadowing (decl);
	}
      /* If no declaration found, default to int.  */
      else
	{
	  decl = build_decl (PARM_DECL, TREE_VALUE (parm), integer_type_node);
	  DECL_ARG_TYPE (decl) = TREE_TYPE (decl);
	  DECL_SOURCE_LOCATION (decl) = DECL_SOURCE_LOCATION (fndecl);
	  pushdecl (decl);
	  warn_if_shadowing (decl);

	  if (flag_isoc99)
	    pedwarn ("type of %q+D defaults to %<int%>", decl);
	  else if (extra_warnings)
	    warning (OPT_Wextra, "type of %q+D defaults to %<int%>", decl);
	}

      TREE_PURPOSE (parm) = decl;
      pointer_set_insert (seen_args, decl);
    }

  /* Now examine the parms chain for incomplete declarations
     and declarations with no corresponding names.  */

  for (b = current_scope->bindings; b; b = b->prev)
    {
      parm = b->decl;
      if (TREE_CODE (parm) != PARM_DECL)
	continue;

      if (TREE_TYPE (parm) != error_mark_node
	  && !COMPLETE_TYPE_P (TREE_TYPE (parm)))
	{
	  error ("parameter %q+D has incomplete type", parm);
	  TREE_TYPE (parm) = error_mark_node;
	}

      if (!pointer_set_contains (seen_args, parm))
	{
	  error ("declaration for parameter %q+D but no such parameter", parm);

	  /* Pretend the parameter was not missing.
	     This gets us to a standard state and minimizes
	     further error messages.  */
	  parmids = chainon (parmids, tree_cons (parm, 0, 0));
	}
    }

  /* Chain the declarations together in the order of the list of
     names.  Store that chain in the function decl, replacing the
     list of names.  Update the current scope to match.  */
  DECL_ARGUMENTS (fndecl) = 0;

  for (parm = parmids; parm; parm = TREE_CHAIN (parm))
    if (TREE_PURPOSE (parm))
      break;
  if (parm && TREE_PURPOSE (parm))
    {
      last = TREE_PURPOSE (parm);
      DECL_ARGUMENTS (fndecl) = last;

      for (parm = TREE_CHAIN (parm); parm; parm = TREE_CHAIN (parm))
	if (TREE_PURPOSE (parm))
	  {
	    TREE_CHAIN (last) = TREE_PURPOSE (parm);
	    last = TREE_PURPOSE (parm);
	  }
      TREE_CHAIN (last) = 0;
    }

  pointer_set_destroy (seen_args);

  /* If there was a previous prototype,
     set the DECL_ARG_TYPE of each argument according to
     the type previously specified, and report any mismatches.  */

  if (current_function_prototype_arg_types)
    {
      tree type;
      for (parm = DECL_ARGUMENTS (fndecl),
	     type = current_function_prototype_arg_types;
	   parm || (type && (TYPE_MAIN_VARIANT (TREE_VALUE (type))
			     != void_type_node));
	   parm = TREE_CHAIN (parm), type = TREE_CHAIN (type))
	{
	  if (parm == 0 || type == 0
	      || TYPE_MAIN_VARIANT (TREE_VALUE (type)) == void_type_node)
	    {
	      if (current_function_prototype_built_in)
		warning (0, "number of arguments doesn%'t match "
			 "built-in prototype");
	      else
		{
		  error ("number of arguments doesn%'t match prototype");
		  error ("%Hprototype declaration",
			 &current_function_prototype_locus);
		}
	      break;
	    }
	  /* Type for passing arg must be consistent with that
	     declared for the arg.  ISO C says we take the unqualified
	     type for parameters declared with qualified type.  */
	  if (!comptypes (TYPE_MAIN_VARIANT (DECL_ARG_TYPE (parm)),
			  TYPE_MAIN_VARIANT (TREE_VALUE (type))))
	    {
	      if (TYPE_MAIN_VARIANT (TREE_TYPE (parm))
		  == TYPE_MAIN_VARIANT (TREE_VALUE (type)))
		{
		  /* Adjust argument to match prototype.  E.g. a previous
		     `int foo(float);' prototype causes
		     `int foo(x) float x; {...}' to be treated like
		     `int foo(float x) {...}'.  This is particularly
		     useful for argument types like uid_t.  */
		  DECL_ARG_TYPE (parm) = TREE_TYPE (parm);

		  if (targetm.calls.promote_prototypes (TREE_TYPE (current_function_decl))
		      && INTEGRAL_TYPE_P (TREE_TYPE (parm))
		      && TYPE_PRECISION (TREE_TYPE (parm))
		      < TYPE_PRECISION (integer_type_node))
		    DECL_ARG_TYPE (parm) = integer_type_node;

		  if (pedantic)
		    {
		      /* ??? Is it possible to get here with a
			 built-in prototype or will it always have
			 been diagnosed as conflicting with an
			 old-style definition and discarded?  */
		      if (current_function_prototype_built_in)
			warning (0, "promoted argument %qD "
				 "doesn%'t match built-in prototype", parm);
		      else
			{
			  pedwarn ("promoted argument %qD "
				   "doesn%'t match prototype", parm);
			  pedwarn ("%Hprototype declaration",
				   &current_function_prototype_locus);
			}
		    }
		}
	      else
		{
		  if (current_function_prototype_built_in)
		    warning (0, "argument %qD doesn%'t match "
			     "built-in prototype", parm);
		  else
		    {
		      error ("argument %qD doesn%'t match prototype", parm);
		      error ("%Hprototype declaration",
			     &current_function_prototype_locus);
		    }
		}
	    }
	}
      TYPE_ACTUAL_ARG_TYPES (TREE_TYPE (fndecl)) = 0;
    }

  /* Otherwise, create a prototype that would match.  */

  else
    {
      tree actual = 0, last = 0, type;

      for (parm = DECL_ARGUMENTS (fndecl); parm; parm = TREE_CHAIN (parm))
	{
	  type = tree_cons (NULL_TREE, DECL_ARG_TYPE (parm), NULL_TREE);
	  if (last)
	    TREE_CHAIN (last) = type;
	  else
	    actual = type;
	  last = type;
	}
      type = tree_cons (NULL_TREE, void_type_node, NULL_TREE);
      if (last)
	TREE_CHAIN (last) = type;
      else
	actual = type;

      /* We are going to assign a new value for the TYPE_ACTUAL_ARG_TYPES
	 of the type of this function, but we need to avoid having this
	 affect the types of other similarly-typed functions, so we must
	 first force the generation of an identical (but separate) type
	 node for the relevant function type.  The new node we create
	 will be a variant of the main variant of the original function
	 type.  */

      TREE_TYPE (fndecl) = build_variant_type_copy (TREE_TYPE (fndecl));

      TYPE_ACTUAL_ARG_TYPES (TREE_TYPE (fndecl)) = actual;
    }
}

/* Store parameter declarations passed in ARG_INFO into the current
   function declaration.  */

void
store_parm_decls_from (struct c_arg_info *arg_info)
{
  current_function_arg_info = arg_info;
  store_parm_decls ();
}

/* Store the parameter declarations into the current function declaration.
   This is called after parsing the parameter declarations, before
   digesting the body of the function.

   For an old-style definition, construct a prototype out of the old-style
   parameter declarations and inject it into the function's type.  */

void
store_parm_decls (void)
{
  tree fndecl = current_function_decl;
  bool proto;

  /* The argument information block for FNDECL.  */
  struct c_arg_info *arg_info = current_function_arg_info;
  current_function_arg_info = 0;

  /* True if this definition is written with a prototype.  Note:
     despite C99 6.7.5.3p14, we can *not* treat an empty argument
     list in a function definition as equivalent to (void) -- an
     empty argument list specifies the function has no parameters,
     but only (void) sets up a prototype for future calls.  */
  proto = arg_info->types != 0;

  if (proto)
    store_parm_decls_newstyle (fndecl, arg_info);
  else
    store_parm_decls_oldstyle (fndecl, arg_info);

  /* The next call to push_scope will be a function body.  */

  next_is_function_body = true;

  /* Write a record describing this function definition to the prototypes
     file (if requested).  */

  gen_aux_info_record (fndecl, 1, 0, proto);

  /* Initialize the RTL code for the function.  */
  allocate_struct_function (fndecl);

  /* Begin the statement tree for this function.  */
  DECL_SAVED_TREE (fndecl) = push_stmt_list ();

  /* ??? Insert the contents of the pending sizes list into the function
     to be evaluated.  The only reason left to have this is
	void foo(int n, int array[n++])
     because we throw away the array type in favor of a pointer type, and
     thus won't naturally see the SAVE_EXPR containing the increment.  All
     other pending sizes would be handled by gimplify_parameters.  */
  {
    tree t;
    for (t = nreverse (get_pending_sizes ()); t ; t = TREE_CHAIN (t))
      add_stmt (TREE_VALUE (t));
  }

  /* Even though we're inside a function body, we still don't want to
     call expand_expr to calculate the size of a variable-sized array.
     We haven't necessarily assigned RTL to all variables yet, so it's
     not safe to try to expand expressions involving them.  */
  cfun->x_dont_save_pending_sizes_p = 1;
}

/* Emit diagnostics that require gimple input for detection.  Operate on
   FNDECL and all its nested functions.  */

static void
c_gimple_diagnostics_recursively (tree fndecl)
{
  struct cgraph_node *cgn;

  /* Handle attribute((warn_unused_result)).  Relies on gimple input.  */
  c_warn_unused_result (&DECL_SAVED_TREE (fndecl));

  /* Notice when OpenMP structured block constraints are violated.  */
  if (flag_openmp)
    diagnose_omp_structured_block_errors (fndecl);

  /* Finalize all nested functions now.  */
  cgn = cgraph_node (fndecl);
  for (cgn = cgn->nested; cgn ; cgn = cgn->next_nested)
    c_gimple_diagnostics_recursively (cgn->decl);
}

/* Finish up a function declaration and compile that function
   all the way to assembler language output.  The free the storage
   for the function definition.

   This is called after parsing the body of the function definition.  */

void
finish_function (void)
{
  tree fndecl = current_function_decl;

  label_context_stack_se = label_context_stack_se->next;
  label_context_stack_vm = label_context_stack_vm->next;

  if (TREE_CODE (fndecl) == FUNCTION_DECL
      && targetm.calls.promote_prototypes (TREE_TYPE (fndecl)))
    {
      tree args = DECL_ARGUMENTS (fndecl);
      for (; args; args = TREE_CHAIN (args))
	{
	  tree type = TREE_TYPE (args);
	  if (INTEGRAL_TYPE_P (type)
	      && TYPE_PRECISION (type) < TYPE_PRECISION (integer_type_node))
	    DECL_ARG_TYPE (args) = integer_type_node;
	}
    }

  if (DECL_INITIAL (fndecl) && DECL_INITIAL (fndecl) != error_mark_node)
    BLOCK_SUPERCONTEXT (DECL_INITIAL (fndecl)) = fndecl;

  /* Must mark the RESULT_DECL as being in this function.  */

  if (DECL_RESULT (fndecl) && DECL_RESULT (fndecl) != error_mark_node)
    DECL_CONTEXT (DECL_RESULT (fndecl)) = fndecl;

  if (MAIN_NAME_P (DECL_NAME (fndecl)) && flag_hosted)
    {
      if (TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (fndecl)))
	  != integer_type_node)
	{
	  /* If warn_main is 1 (-Wmain) or 2 (-Wall), we have already warned.
	     If warn_main is -1 (-Wno-main) we don't want to be warned.  */
	  if (!warn_main)
	    pedwarn ("return type of %q+D is not %<int%>", fndecl);
	}
      else
	{
	  if (flag_isoc99)
	    {
	      tree stmt = c_finish_return (integer_zero_node);
#ifdef USE_MAPPED_LOCATION
	      /* Hack.  We don't want the middle-end to warn that this return
		 is unreachable, so we mark its location as special.  Using
		 UNKNOWN_LOCATION has the problem that it gets clobbered in
		 annotate_one_with_locus.  A cleaner solution might be to
		 ensure ! should_carry_locus_p (stmt), but that needs a flag.
	      */
	      SET_EXPR_LOCATION (stmt, BUILTINS_LOCATION);
#else
	      /* Hack.  We don't want the middle-end to warn that this
		 return is unreachable, so put the statement on the
		 special line 0.  */
	      annotate_with_file_line (stmt, input_filename, 0);
#endif
	    }
	}
    }

  /* Tie off the statement tree for this function.  */
  DECL_SAVED_TREE (fndecl) = pop_stmt_list (DECL_SAVED_TREE (fndecl));

  finish_fname_decls ();

  /* Complain if there's just no return statement.  */
  if (warn_return_type
      && TREE_CODE (TREE_TYPE (TREE_TYPE (fndecl))) != VOID_TYPE
      && !current_function_returns_value && !current_function_returns_null
      /* Don't complain if we are no-return.  */
      && !current_function_returns_abnormally
      /* Don't warn for main().  */
      && !MAIN_NAME_P (DECL_NAME (fndecl))
      /* Or if they didn't actually specify a return type.  */
      && !C_FUNCTION_IMPLICIT_INT (fndecl)
      /* Normally, with -Wreturn-type, flow will complain.  Unless we're an
	 inline function, as we might never be compiled separately.  */
      && DECL_INLINE (fndecl))
    {
      warning (OPT_Wreturn_type,
	       "no return statement in function returning non-void");
      TREE_NO_WARNING (fndecl) = 1;
    }

  /* With just -Wextra, complain only if function returns both with
     and without a value.  */
  if (extra_warnings
      && current_function_returns_value
      && current_function_returns_null)
    warning (OPT_Wextra, "this function may return with or without a value");

  /* Store the end of the function, so that we get good line number
     info for the epilogue.  */
  cfun->function_end_locus = input_location;

  /* If we don't have ctors/dtors sections, and this is a static
     constructor or destructor, it must be recorded now.  */
  if (DECL_STATIC_CONSTRUCTOR (fndecl)
      && !targetm.have_ctors_dtors)
    static_ctors = tree_cons (NULL_TREE, fndecl, static_ctors);
  if (DECL_STATIC_DESTRUCTOR (fndecl)
      && !targetm.have_ctors_dtors)
    static_dtors = tree_cons (NULL_TREE, fndecl, static_dtors);

  /* Finalize the ELF visibility for the function.  */
  c_determine_visibility (fndecl);

  /* Genericize before inlining.  Delay genericizing nested functions
     until their parent function is genericized.  Since finalizing
     requires GENERIC, delay that as well.  */

  if (DECL_INITIAL (fndecl) && DECL_INITIAL (fndecl) != error_mark_node
      && !undef_nested_function)
    {
      if (!decl_function_context (fndecl))
	{
	  c_genericize (fndecl);
	  c_gimple_diagnostics_recursively (fndecl);

	  /* ??? Objc emits functions after finalizing the compilation unit.
	     This should be cleaned up later and this conditional removed.  */
	  if (cgraph_global_info_ready)
	    {
	      c_expand_body (fndecl);
	      return;
	    }

	  cgraph_finalize_function (fndecl, false);
	}
      else
	{
	  /* Register this function with cgraph just far enough to get it
	    added to our parent's nested function list.  Handy, since the
	    C front end doesn't have such a list.  */
	  (void) cgraph_node (fndecl);
	}
    }

  if (!decl_function_context (fndecl))
    undef_nested_function = false;

  /* We're leaving the context of this function, so zap cfun.
     It's still in DECL_STRUCT_FUNCTION, and we'll restore it in
     tree_rest_of_compilation.  */
  cfun = NULL;
  current_function_decl = NULL;
}

/* Generate the RTL for the body of FNDECL.  */

void
c_expand_body (tree fndecl)
{

  if (!DECL_INITIAL (fndecl)
      || DECL_INITIAL (fndecl) == error_mark_node)
    return;

  tree_rest_of_compilation (fndecl);

  if (DECL_STATIC_CONSTRUCTOR (fndecl)
      && targetm.have_ctors_dtors)
    targetm.asm_out.constructor (XEXP (DECL_RTL (fndecl), 0),
				 DEFAULT_INIT_PRIORITY);
  if (DECL_STATIC_DESTRUCTOR (fndecl)
      && targetm.have_ctors_dtors)
    targetm.asm_out.destructor (XEXP (DECL_RTL (fndecl), 0),
				DEFAULT_INIT_PRIORITY);
}

/* Check the declarations given in a for-loop for satisfying the C99
   constraints.  If exactly one such decl is found, return it.  */

tree
check_for_loop_decls (void)
{
  struct c_binding *b;
  tree one_decl = NULL_TREE;
  int n_decls = 0;


  if (!flag_isoc99)
    {
      /* If we get here, declarations have been used in a for loop without
	 the C99 for loop scope.  This doesn't make much sense, so don't
	 allow it.  */
      error ("%<for%> loop initial declaration used outside C99 mode");
      return NULL_TREE;
    }
  /* C99 subclause 6.8.5 paragraph 3:

       [#3]  The  declaration  part  of  a for statement shall only
       declare identifiers for objects having storage class auto or
       register.

     It isn't clear whether, in this sentence, "identifiers" binds to
     "shall only declare" or to "objects" - that is, whether all identifiers
     declared must be identifiers for objects, or whether the restriction
     only applies to those that are.  (A question on this in comp.std.c
     in November 2000 received no answer.)  We implement the strictest
     interpretation, to avoid creating an extension which later causes
     problems.  */

  for (b = current_scope->bindings; b; b = b->prev)
    {
      tree id = b->id;
      tree decl = b->decl;

      if (!id)
	continue;

      switch (TREE_CODE (decl))
	{
	case VAR_DECL:
	  if (TREE_STATIC (decl))
	    error ("declaration of static variable %q+D in %<for%> loop "
		   "initial declaration", decl);
	  else if (DECL_EXTERNAL (decl))
	    error ("declaration of %<extern%> variable %q+D in %<for%> loop "
		   "initial declaration", decl);
	  break;

	case RECORD_TYPE:
	  error ("%<struct %E%> declared in %<for%> loop initial declaration",
		 id);
	  break;
	case UNION_TYPE:
	  error ("%<union %E%> declared in %<for%> loop initial declaration",
		 id);
	  break;
	case ENUMERAL_TYPE:
	  error ("%<enum %E%> declared in %<for%> loop initial declaration",
		 id);
	  break;
	/* APPLE LOCAL begin radar 6268817 */
	 case FUNCTION_DECL:
	/* Block helper function can be declared in the statement block
	 for the for-loop declarations. */
	if (BLOCK_SYNTHESIZED_FUNC (decl))
		break;
	/* APPLE LOCAL end radar 6268817 */
	default:
	  error ("declaration of non-variable %q+D in %<for%> loop "
		 "initial declaration", decl);
	}

      n_decls++;
      one_decl = decl;
    }

  return n_decls == 1 ? one_decl : NULL_TREE;
}

/* Save and reinitialize the variables
   used during compilation of a C function.  */

void
c_push_function_context (struct function *f)
{
  struct language_function *p;
  p = GGC_NEW (struct language_function);
  f->language = p;

  p->base.x_stmt_tree = c_stmt_tree;
  p->x_break_label = c_break_label;
  p->x_cont_label = c_cont_label;
  p->x_switch_stack = c_switch_stack;
  p->arg_info = current_function_arg_info;
  p->returns_value = current_function_returns_value;
  p->returns_null = current_function_returns_null;
  p->returns_abnormally = current_function_returns_abnormally;
  p->warn_about_return_type = warn_about_return_type;
}

/* Restore the variables used during compilation of a C function.  */

void
c_pop_function_context (struct function *f)
{
  struct language_function *p = f->language;

  /* APPLE LOCAL begin blocks 6040305 */
  if (current_function_decl
      && DECL_STRUCT_FUNCTION (current_function_decl) == 0
      /* APPLE LOCAL end blocks 6040305 */
      && DECL_SAVED_TREE (current_function_decl) == NULL_TREE)
    {
      /* Stop pointing to the local nodes about to be freed.  */
      /* But DECL_INITIAL must remain nonzero so we know this
	 was an actual function definition.  */
      DECL_INITIAL (current_function_decl) = error_mark_node;
      DECL_ARGUMENTS (current_function_decl) = 0;
    }

  c_stmt_tree = p->base.x_stmt_tree;
  c_break_label = p->x_break_label;
  c_cont_label = p->x_cont_label;
  c_switch_stack = p->x_switch_stack;
  current_function_arg_info = p->arg_info;
  current_function_returns_value = p->returns_value;
  current_function_returns_null = p->returns_null;
  current_function_returns_abnormally = p->returns_abnormally;
  warn_about_return_type = p->warn_about_return_type;

  f->language = NULL;
}

/* Copy the DECL_LANG_SPECIFIC data associated with DECL.  */

void
c_dup_lang_specific_decl (tree decl)
{
  struct lang_decl *ld;

  if (!DECL_LANG_SPECIFIC (decl))
    return;

  ld = GGC_NEW (struct lang_decl);
  memcpy (ld, DECL_LANG_SPECIFIC (decl), sizeof (struct lang_decl));
  DECL_LANG_SPECIFIC (decl) = ld;
}

/* The functions below are required for functionality of doing
   function at once processing in the C front end. Currently these
   functions are not called from anywhere in the C front end, but as
   these changes continue, that will change.  */

/* Returns the stmt_tree (if any) to which statements are currently
   being added.  If there is no active statement-tree, NULL is
   returned.  */

stmt_tree
current_stmt_tree (void)
{
  return &c_stmt_tree;
}

/* Nonzero if TYPE is an anonymous union or struct type.  Always 0 in
   C.  */

int
anon_aggr_type_p (tree ARG_UNUSED (node))
{
  return 0;
}

/* Return the global value of T as a symbol.  */

tree
identifier_global_value	(tree t)
{
  struct c_binding *b;

  for (b = I_SYMBOL_BINDING (t); b; b = b->shadowed)
    if (B_IN_FILE_SCOPE (b) || B_IN_EXTERNAL_SCOPE (b))
      return b->decl;

  return 0;
}

/* Record a builtin type for C.  If NAME is non-NULL, it is the name used;
   otherwise the name is found in ridpointers from RID_INDEX.  */

void
record_builtin_type (enum rid rid_index, const char *name, tree type)
{
  tree id, decl;
  if (name == 0)
    id = ridpointers[(int) rid_index];
  else
    id = get_identifier (name);
  decl = build_decl (TYPE_DECL, id, type);
  pushdecl (decl);
  if (debug_hooks->type_decl)
    debug_hooks->type_decl (decl, false);
}

/* Build the void_list_node (void_type_node having been created).  */
tree
build_void_list_node (void)
{
  tree t = build_tree_list (NULL_TREE, void_type_node);
  return t;
}

/* Return a c_parm structure with the given SPECS, ATTRS and DECLARATOR.  */

struct c_parm *
build_c_parm (struct c_declspecs *specs, tree attrs,
	      struct c_declarator *declarator)
{
  struct c_parm *ret = XOBNEW (&parser_obstack, struct c_parm);
  ret->specs = specs;
  ret->attrs = attrs;
  ret->declarator = declarator;
  return ret;
}

/* Return a declarator with nested attributes.  TARGET is the inner
   declarator to which these attributes apply.  ATTRS are the
   attributes.  */

struct c_declarator *
build_attrs_declarator (tree attrs, struct c_declarator *target)
{
  struct c_declarator *ret = XOBNEW (&parser_obstack, struct c_declarator);
  ret->kind = cdk_attrs;
  ret->declarator = target;
  ret->u.attrs = attrs;
  return ret;
}

/* Return a declarator for a function with arguments specified by ARGS
   and return type specified by TARGET.  */

struct c_declarator *
build_function_declarator (struct c_arg_info *args,
			   struct c_declarator *target)
{
  struct c_declarator *ret = XOBNEW (&parser_obstack, struct c_declarator);
  ret->kind = cdk_function;
  ret->declarator = target;
  ret->u.arg_info = args;
  return ret;
}

/* Return a declarator for the identifier IDENT (which may be
   NULL_TREE for an abstract declarator).  */

struct c_declarator *
build_id_declarator (tree ident)
{
  struct c_declarator *ret = XOBNEW (&parser_obstack, struct c_declarator);
  ret->kind = cdk_id;
  ret->declarator = 0;
  ret->u.id = ident;
  /* Default value - may get reset to a more precise location. */
  ret->id_loc = input_location;
  return ret;
}

/* Return something to represent absolute declarators containing a *.
   TARGET is the absolute declarator that the * contains.
   TYPE_QUALS_ATTRS is a structure for type qualifiers and attributes
   to apply to the pointer type.  */

struct c_declarator *
make_pointer_declarator (struct c_declspecs *type_quals_attrs,
			 struct c_declarator *target)
{
  tree attrs;
  int quals = 0;
  struct c_declarator *itarget = target;
  struct c_declarator *ret = XOBNEW (&parser_obstack, struct c_declarator);
  if (type_quals_attrs)
    {
      attrs = type_quals_attrs->attrs;
      quals = quals_from_declspecs (type_quals_attrs);
      if (attrs != NULL_TREE)
	itarget = build_attrs_declarator (attrs, target);
    }
  ret->kind = cdk_pointer;
  ret->declarator = itarget;
  ret->u.pointer_quals = quals;
  return ret;
}

/* APPLE LOCAL begin radar 5932809 - copyable byref blocks (C++ ch) */
/* build_byref_local_var_access - converts EXPR to:
   EXPR.__forwarding-><decl-name>.
*/
tree
build_byref_local_var_access (tree expr, tree decl_name)
{
  tree exp = build_component_ref (expr, get_identifier ("__forwarding"));
  exp = build_indirect_ref (exp, "unary *");
  exp = build_component_ref (exp, decl_name);
  return exp;
}
/* APPLE LOCAL end radar 5932809 - copyable byref blocks (C++ ch) */
/* APPLE LOCAL begin radar 5732232 - blocks (C++ ch) */
/**
  build_block_byref_decl - This routine inserts a variable declared as a
  'byref' variable using the |...| syntax in helper function's outer-most scope.
*/
tree
build_block_byref_decl (tree name, tree decl, tree exp)
{
  struct c_scope *scope = current_scope;
  tree ptr_type, byref_decl;
  /* APPLE LOCAL begin radar 6225809 */
  if (cur_block->prev_block_info) {
    /* Traverse enclosing blocks. Insert a __block variable in
     each enclosing block which has no declaration of this
     variable. This is to ensure that the current (inner) block
     gets the __block version of the variable; */
    struct block_sema_info *cb = cur_block->prev_block_info;
    while (cb) {
      struct c_binding *b = I_SYMBOL_BINDING (name);
      /* Find the first declaration not in current block. */
      while (b && b->decl
	      && (TREE_CODE (b->decl) == VAR_DECL
	          || TREE_CODE (b->decl) == PARM_DECL)
	      && b->depth >= cur_block->the_scope->depth)
	 b = b->shadowed;
      
      /* Is the next declaration not in enclosing block? */
      if (b && b->decl
	   && (TREE_CODE (b->decl) == VAR_DECL
	       || TREE_CODE (b->decl) == PARM_DECL)
	   && b->depth < cb->the_scope->depth) {
	 /* No declaration of variable seen in the block. Must insert one. */
	 struct c_scope *save_scope = current_scope;
	 struct block_sema_info *save_current_block = cur_block;
	 current_scope = cb->the_scope;
	 cur_block = cb;
	 decl = build_block_byref_decl (name, decl, exp);
	 cur_block = save_current_block;
	 current_scope = save_scope;
      }
      cb = cb->prev_block_info;
    }
  }
  /* APPLE LOCAL end radar 6225809 */
  
  /* If it is already a byref declaration, do not add the pointer type
     because such declarations already have the pointer type
     added. This happens when we have two nested byref declarations in
     nested blocks. */
  ptr_type = (TREE_CODE (decl) == VAR_DECL && BLOCK_DECL_BYREF (decl))
	       ? TREE_TYPE (decl) : build_pointer_type (TREE_TYPE (decl));
  byref_decl = build_decl (VAR_DECL, name, ptr_type);
  /* APPLE LOCAL begin radars 6144664 & 6145471  */
  DECL_SOURCE_LOCATION (byref_decl) = DECL_SOURCE_LOCATION 
	                                        (cur_block->helper_func_decl);
  /* APPLE LOCAL end radars 6144664 & 6145471  */
  BLOCK_DECL_BYREF (byref_decl) = 1;

  /* APPLE LOCAL begin radar 5932809 - copyable byref blocks (C++ ch) */
  if (TREE_CODE (decl) == VAR_DECL && COPYABLE_BYREF_LOCAL_VAR (decl))
    {
      COPYABLE_BYREF_LOCAL_VAR (byref_decl) = 1;
      COPYABLE_BYREF_LOCAL_NONPOD (byref_decl) = COPYABLE_BYREF_LOCAL_NONPOD (decl);
      /* APPLE LOCAL radar 5847976 */
      COPYABLE_WEAK_BLOCK (byref_decl) = COPYABLE_WEAK_BLOCK (decl);
    }
  /* APPLE LOCAL end radar 5932809 - copyable byref blocks (C++ ch) */

  /* In the presence of nested "{" 
     move up the scope chain until reaching the main function body's scope.
  */
  while (scope && !scope->function_body)
    scope = scope->outer;
  /* Current scope must be that of the main function body. */
  gcc_assert (scope && scope->function_body);
  bind (name, byref_decl,
	 scope, /*invisible=*/false, /*nested=*/false);
  cur_block->block_byref_decl_list =
    tree_cons (NULL_TREE, byref_decl, cur_block->block_byref_decl_list);
  /* APPLE LOCAL radar 5847213 - building block_original_byref_decl_list list removed. */
  /* APPLE LOCAL begin radar 6289031 */
#if 0
  if (! flag_objc_gc_only)
#endif
    push_cleanup (byref_decl, build_block_byref_release_exp (byref_decl), false);
  /* APPLE LOCAL end radar 6289031 */

  return byref_decl;
}

/**
  build_block_ref_decl - This routine inserts a copied-in variable (a variable
  referenced in the block but whose scope is outside the block) in helper
  function's outer-most scope. It also sets its type to 'const' as such
  variables are read-only.
*/
tree
build_block_ref_decl (tree name, tree decl)
{
  struct c_scope *scope = current_scope;
  tree ref_decl;
  /* APPLE LOCAL radar 6212722 */
  tree type, exp;
  /* APPLE LOCAL begin radar 5932809 - copyable byref blocks (C++ ch) */
  /* 'decl' was previously declared as __block. Simply, copy the value
     embedded in the above variable. */
  if (TREE_CODE (decl) == VAR_DECL && COPYABLE_BYREF_LOCAL_VAR (decl))
    decl = build_byref_local_var_access (decl, DECL_NAME (decl));
  else {
    /* APPLE LOCAL begin radar 5988451 (C++ ch) */
    if (cur_block->prev_block_info) {
      /* Traverse enclosing blocks. Insert a copied-in variable in
	  each enclosing block which has no declaration of this
	  variable. This is to ensure that the current (inner) block
	  has the 'frozen' value of the copied-in variable; which means
	  the value of the copied in variable is at the point of the
	  block declaration and *not* when the inner block is
	  invoked.  */
      struct block_sema_info *cb = cur_block->prev_block_info;
      while (cb) {
	 struct c_binding *b = I_SYMBOL_BINDING (name);
	 /* Find the first declaration not in current block. */
	 while (b && b->decl
	        && (TREE_CODE (b->decl) == VAR_DECL
	            || TREE_CODE (b->decl) == PARM_DECL)
	        && b->depth >= cur_block->the_scope->depth)
	   b = b->shadowed;

	 /* Is the next declaration not in enclosing block? */
	 if (b && b->decl
	     && (TREE_CODE (b->decl) == VAR_DECL
	         || TREE_CODE (b->decl) == PARM_DECL)
	     && b->depth < cb->the_scope->depth) {
	   /* No declaration of variable seen in the block. Must insert one,
	      so it 'freezes' the variable in this block. */
	   struct c_scope *save_scope = current_scope;
	   struct block_sema_info *save_current_block = cur_block;
	   current_scope = cb->the_scope;
	   cur_block = cb;
	   decl = build_block_ref_decl (name, decl);
	   cur_block = save_current_block;
	   current_scope = save_scope;
	 }
	 cb = cb->prev_block_info;
      }
    }
    /* APPLE LOCAL end radar 5988451 (C++ ch) */
  }
  /* APPLE LOCAL end radar 5932809 - copyable byref blocks (C++ ch) */
  /* APPLE LOCAL begin radar 6212722 */
  exp = decl;
  type = TREE_TYPE (exp);
  if (TREE_CODE (type) == ARRAY_TYPE) {
    exp = array_to_pointer_conversion (decl);
    type = TREE_TYPE (exp);
  }
  else if (TREE_CODE (type) == FUNCTION_TYPE) {
    exp = function_to_pointer_conversion (exp);
    type = TREE_TYPE (exp);
  }
  ref_decl = build_decl (VAR_DECL, name,
	                  build_qualified_type (type, TYPE_QUAL_CONST));
  /* APPLE LOCAL end radar 6212722 */
  /* APPLE LOCAL begin radars 6144664 & 6145471  */
  DECL_SOURCE_LOCATION (ref_decl) = DECL_SOURCE_LOCATION 
	                                        (cur_block->helper_func_decl);
  /* APPLE LOCAL end radars 6144664 & 6145471  */
  DECL_INITIAL (ref_decl) = error_mark_node;
  /* APPLE LOCAL radar 5805175 - blocks (C++ ch) */
  c_apply_type_quals_to_decl (TYPE_QUAL_CONST, ref_decl);
  BLOCK_DECL_COPIED (ref_decl) = 1;

  /* Find the scope for function body (outer-most scope) and insert
     this variable in that scope. This is to avoid duplicate
     declaration of the save variable. */
  while (scope && !scope->function_body)
    scope = scope->outer;
  /* We are enterring the copied-in variable in helper function's
     outer scope; that of its main body. */
  gcc_assert (scope);
  bind (name, ref_decl,
	 scope, /*invisible=*/false, /*nested=*/false);
  cur_block->block_ref_decl_list =
    tree_cons (NULL_TREE, ref_decl, cur_block->block_ref_decl_list);
  cur_block->block_original_ref_decl_list =
    /* APPLE LOCAL radar 6212722 */
    tree_cons (NULL_TREE, exp, cur_block->block_original_ref_decl_list);
  return ref_decl;
}

/* APPLE LOCAL begin radar 5847213 - radar 6329245 */
static GTY (())  tree descriptor_ptr_type;
static GTY (())  tree descriptor_ptr_type_with_copydispose;
/** build_block_descriptor_type - This routine builds following internal type:
 struct __block_descriptor {
 unsigned long int reserved;     // NULL
 unsigned long int Size;  // sizeof(struct Block_literal_1)
 
 // optional helper functions
 void *CopyFuncPtr; // When BLOCK_HAS_COPY_DISPOSE is set (withCopyDispose true)
 void *DestroyFuncPtr; // When BLOCK_HAS_COPY_DISPOSE is set (withCopyDispose true)
} *descriptor_ptr_type;

Objects of this type will always be static. This is one main component of abi change.
*/
tree
build_block_descriptor_type (bool withCopyDispose)
{
  tree field_decl_chain, field_decl;
  tree main_type;
  
  if (withCopyDispose && descriptor_ptr_type_with_copydispose)
    return descriptor_ptr_type_with_copydispose;
  if (!withCopyDispose && descriptor_ptr_type)
    return descriptor_ptr_type;
  
  main_type = 
    withCopyDispose ? 
      start_struct (RECORD_TYPE, get_identifier ("__block_descriptor_withcopydispose"))
      : start_struct (RECORD_TYPE, get_identifier ("__block_descriptor"));
  
  /* unsigned long int reserved; */
  field_decl = build_decl (FIELD_DECL, get_identifier ("reserved"), long_unsigned_type_node);
  field_decl_chain = field_decl;
  
  /* unsigned long int Size; */
  field_decl = build_decl (FIELD_DECL, get_identifier ("Size"), long_unsigned_type_node);
  chainon (field_decl_chain, field_decl);
  
  if (withCopyDispose)
  {
    /* void *CopyFuncPtr; */
    field_decl = build_decl (FIELD_DECL, get_identifier ("CopyFuncPtr"), ptr_type_node);
    chainon (field_decl_chain, field_decl);
    /* void *DestroyFuncPtr; */
    field_decl = build_decl (FIELD_DECL, get_identifier ("DestroyFuncPtr"), ptr_type_node);
    chainon (field_decl_chain, field_decl);
  }
  
   /* Mark this struct as being a block struct rather than a 'normal'
      struct.  */
  TYPE_BLOCK_IMPL_STRUCT (main_type) = 1;
  finish_struct (main_type, field_decl_chain, NULL_TREE);

  main_type = build_pointer_type (main_type);
  if (withCopyDispose)
    descriptor_ptr_type_with_copydispose = main_type;
  else
    descriptor_ptr_type = main_type;
  return main_type;
}
/* APPLE LOCAL end radar 5847213 - radar 6329245 */

/* APPLE LOCAL begin radar 5814025 (C++ ch) */
struct c_declarator *
make_block_pointer_declarator (struct c_declspecs *type_quals_attrs,
	                          struct c_declarator *target)
{
  int quals = 0;
  struct c_declarator *itarget = target;
  struct c_declarator *ret = XOBNEW (&parser_obstack, struct c_declarator);

  if (type_quals_attrs)
  {
    tree attrs = type_quals_attrs->attrs;
    quals = quals_from_declspecs (type_quals_attrs);
    if (attrs != NULL_TREE)
      itarget = build_attrs_declarator (attrs, target);
  }
  ret->kind = cdk_block_pointer;
  /* APPLE LOCAL radar 5882266 (C++ ch) */
  ret->declarator = itarget;
  ret->u.pointer_quals = quals;
  return ret;
}
/* APPLE LOCAL end radar 5814025 (C++ ch) */

tree
begin_block (void)
{
  struct block_sema_info *csi;
#if 0
  push_scope ();
#endif
  csi = (struct block_sema_info*)xcalloc (1, sizeof (struct block_sema_info));
  csi->prev_block_info = cur_block;
  cur_block = csi;
  return NULL_TREE;
}

struct block_sema_info *
finish_block (tree block __attribute__ ((__unused__)))
{
  struct block_sema_info *csi = cur_block;
  cur_block = cur_block->prev_block_info;
#if 0
  pop_scope ();
#endif
  return csi;
}

bool
in_imm_block (void)
{
  /* APPLE LOCAL radar 5988451 (C++ ch) */
  return (cur_block && cur_block->the_scope == current_scope);
}

/* This routine returns 'true' if 'name' has a declaration inside the
   current block, 'false' otherwise.  If 'name' has no declaration in
   the current block, it returns in DECL the user declaration for
   'name' found in the enclosing scope.  Note that if it is declared
   in current declaration, it can be either a user declaration or a
   byref/copied-in declaration added in current block's scope by the
   compiler.  */
bool
lookup_name_in_block (tree name, tree *decl)
{
  if (cur_block)
    {
      struct c_binding *b = I_SYMBOL_BINDING (name);
      if (b->depth >= cur_block->the_scope->depth)
	return true;

      /* Check for common case of block nested inside a non-block. */
      if (!cur_block->prev_block_info)
	return false;
      /* Check for less common case of nested blocks. */
      /* Declaration not in current block. Find the first user
	 declaration of 'name' in outer scope. */
      /* APPLE LOCAL begin radar 5988451 (C++ ch) */
      /* Check for variables only, as we may have parameters, such as
	 'self' */
      /* Note that if a copied-in variable (BLOCK_DECL_COPIED) in the
	  enclosing block is found, it must be returned as this is
	  where the variable in current (nested block) will have to get
	  its value. */
      while (b && b->decl
	     && (TREE_CODE (b->decl) == VAR_DECL)
	      && BLOCK_DECL_BYREF (b->decl))
	 b = b->shadowed;
	/* APPLE LOCAL end radar 5988451 (C++ ch) */
      if (b && b->decl)
	*decl = b->decl;
    }
  return false;
}

static struct c_scope *save_current_scope;
static tree save_current_function_decl;
void
push_to_top_level (void)
{
  save_current_scope = current_scope;
  save_current_function_decl = current_function_decl;
  current_scope = file_scope;
  current_function_decl = NULL_TREE;
}

void
pop_from_top_level (void)
{
  current_scope = save_current_scope;
  current_function_decl = save_current_function_decl;
}

/**
  build_helper_func_decl - This routine builds a FUNCTION_DECL for
  a block helper function.
*/
tree
build_helper_func_decl (tree ident, tree type)
{
  tree func_decl = build_decl (FUNCTION_DECL, ident, type);
  DECL_EXTERNAL (func_decl) = 0;
  TREE_PUBLIC (func_decl) = 0;
  TREE_USED (func_decl) = 1;
  TREE_NOTHROW (func_decl) = 0;
  /* APPLE LOCAL radar 6172148 */
  BLOCK_SYNTHESIZED_FUNC (func_decl) = 1;
  return func_decl;
}

/**
 start_block_helper_function - This is a light-weight version of start_function().
 It has removed all the fuss in the start_function().
 */
void
start_block_helper_function (tree decl1)
{
  struct c_label_context_se *nstack_se;
  struct c_label_context_vm *nstack_vm;
  tree restype, resdecl;

  current_function_returns_value = 0;  /* Assume, until we see it does.  */
  current_function_returns_null = 0;
  current_function_returns_abnormally = 0;
  warn_about_return_type = 0;
  c_switch_stack = NULL;

  nstack_se = XOBNEW (&parser_obstack, struct c_label_context_se);
  nstack_se->labels_def = NULL;
  nstack_se->labels_used = NULL;
  nstack_se->next = label_context_stack_se;
  label_context_stack_se = nstack_se;

  nstack_vm = XOBNEW (&parser_obstack, struct c_label_context_vm);
  nstack_vm->labels_def = NULL;
  nstack_vm->labels_used = NULL;
  nstack_vm->scope = 0;
  nstack_vm->next = label_context_stack_vm;
  label_context_stack_vm = nstack_vm;

  /* Indicate no valid break/continue context by setting these variables
   to some non-null, non-label value.  We'll notice and emit the proper
   error message in c_finish_bc_stmt.  */
  c_break_label = c_cont_label = size_zero_node;

  announce_function (decl1);

  /* Make the init_value nonzero so pushdecl knows this is not tentative.
   error_mark_node is replaced below (in pop_scope) with the BLOCK.  */
  DECL_INITIAL (decl1) = error_mark_node;

  current_function_prototype_locus = UNKNOWN_LOCATION;
  current_function_prototype_built_in = false;
  current_function_prototype_arg_types = NULL_TREE;

  /* This function exists in static storage.
   (This does not mean `static' in the C sense!)  */
  TREE_STATIC (decl1) = 1;
  /* A helper function is not global */
  TREE_PUBLIC (decl1) = 0;

  /* This is the earliest point at which we might know the assembler
   name of the function.  Thus, if it's set before this, die horribly.  */
  gcc_assert (!DECL_ASSEMBLER_NAME_SET_P (decl1));
  current_function_decl = pushdecl (decl1);

  push_scope ();
  declare_parm_level ();

  restype = TREE_TYPE (TREE_TYPE (current_function_decl));
  resdecl = build_decl (RESULT_DECL, NULL_TREE, restype);
  DECL_ARTIFICIAL (resdecl) = 1;
  DECL_IGNORED_P (resdecl) = 1;
  DECL_RESULT (current_function_decl) = resdecl;

  start_fname_decls ();
}

/**
 declare_block_prologue_local_vars - utility routine to do the actual
 declaration and initialization for each referecned block variable.
*/
static void
declare_block_prologue_local_vars (tree self_parm, tree component,
				   tree stmt)
{
  tree decl, block_component;
  tree_stmt_iterator i;
  tree decl_stmt;

  decl = component;
  block_component = build_component_ref (build_indirect_ref (self_parm, "->"),
					 DECL_NAME (component));
  gcc_assert (block_component);
  DECL_EXTERNAL (decl) = 0;
  TREE_STATIC (decl) = 0;
  TREE_USED (decl) = 1;
  DECL_CONTEXT (decl) = current_function_decl;
  DECL_ARTIFICIAL (decl) = 1;
  DECL_INITIAL (decl) = block_component;
  /* Prepend a DECL_EXPR statement to the statement list. */
  i = tsi_start (stmt);
  decl_stmt = build_stmt (DECL_EXPR, decl);
  /* APPLE LOCAL Radar 5811961, Fix location of block prologue vars (C++ ch) */
  SET_EXPR_LOCATION (decl_stmt, DECL_SOURCE_LOCATION (decl));
  /* APPLE LOCAL begin radar 6163705, Blocks prologues  */
  /* Give the prologue statements a line number of one before the beginning of
     the function, to make them easily identifiable later.  */
  EXPR_LINENO (decl_stmt) =  DECL_SOURCE_LINE (decl) - 1;
  /* APPLE LOCAL end radar 6163705, Blocks prologues  */
  tsi_link_before (&i, decl_stmt, TSI_SAME_STMT);
}

/**
 block_build_prologue
 - This routine builds the declarations for the
 variables referenced in the block; as in:
 int *y = .block_descriptor->y;
 int x = .block_descriptor->x;

 The decl_expr declaration for each initialization is enterred at the
 beginning of the helper function's statement-list which is passed
 in block_impl->block_body.
*/
void
block_build_prologue (struct block_sema_info *block_impl)
{
  tree chain;
  /* APPLE LOCAL radar 6404979 */
  tree self_parm = lookup_name (get_identifier (".block_descriptor"));
  gcc_assert (self_parm);

  for (chain = block_impl->block_ref_decl_list; chain;
	chain = TREE_CHAIN (chain))
    declare_block_prologue_local_vars (self_parm, TREE_VALUE (chain),
				       block_impl->block_body);

  for (chain = block_impl->block_byref_decl_list; chain;
	chain = TREE_CHAIN (chain))
    declare_block_prologue_local_vars (self_parm, TREE_VALUE (chain),
				       block_impl->block_body);
}
/* APPLE LOCAL end radar 5732232 - blocks (C++ ch) */

/* Return a pointer to a structure for an empty list of declaration
   specifiers.  */

struct c_declspecs *
build_null_declspecs (void)
{
  struct c_declspecs *ret = XOBNEW (&parser_obstack, struct c_declspecs);
  ret->type = 0;
  ret->decl_attr = 0;
  ret->attrs = 0;
  ret->typespec_word = cts_none;
  ret->storage_class = csc_none;
  ret->declspecs_seen_p = false;
  ret->type_seen_p = false;
  ret->non_sc_seen_p = false;
  ret->typedef_p = false;
  ret->tag_defined_p = false;
  ret->explicit_signed_p = false;
  ret->deprecated_p = false;
  /* APPLE LOCAL "unavailable" attribute (radar 2809697) */
  ret->unavailable_p = false;
  ret->default_int_p = false;
  ret->long_p = false;
  ret->long_long_p = false;
  ret->short_p = false;
  ret->signed_p = false;
  ret->unsigned_p = false;
  ret->complex_p = false;
  ret->inline_p = false;
  ret->thread_p = false;
  ret->const_p = false;
  ret->volatile_p = false;
  ret->restrict_p = false;
  return ret;
}

/* Add the type qualifier QUAL to the declaration specifiers SPECS,
   returning SPECS.  */

struct c_declspecs *
declspecs_add_qual (struct c_declspecs *specs, tree qual)
{
  enum rid i;
  bool dupe = false;
  specs->non_sc_seen_p = true;
  specs->declspecs_seen_p = true;
  gcc_assert (TREE_CODE (qual) == IDENTIFIER_NODE
	      && C_IS_RESERVED_WORD (qual));
  i = C_RID_CODE (qual);
  switch (i)
    {
    case RID_CONST:
      dupe = specs->const_p;
      specs->const_p = true;
      break;
    case RID_VOLATILE:
      dupe = specs->volatile_p;
      specs->volatile_p = true;
      break;
    case RID_RESTRICT:
      dupe = specs->restrict_p;
      specs->restrict_p = true;
      break;
    default:
      gcc_unreachable ();
    }
  if (dupe && pedantic && !flag_isoc99)
    pedwarn ("duplicate %qE", qual);
  return specs;
}

/* Add the type specifier TYPE to the declaration specifiers SPECS,
   returning SPECS.  */

struct c_declspecs *
declspecs_add_type (struct c_declspecs *specs, struct c_typespec spec)
{
  tree type = spec.spec;
  specs->non_sc_seen_p = true;
  specs->declspecs_seen_p = true;
  specs->type_seen_p = true;
  if (TREE_DEPRECATED (type))
    specs->deprecated_p = true;

  /* APPLE LOCAL begin "unavailable" attribute (radar 2809697) */
  if (TREE_UNAVAILABLE (type))
    specs->unavailable_p = true;
  /* APPLE LOCAL end "unavailable" attribute (radar 2809697) */

  /* Handle type specifier keywords.  */
  if (TREE_CODE (type) == IDENTIFIER_NODE && C_IS_RESERVED_WORD (type))
    {
      enum rid i = C_RID_CODE (type);
      if (specs->type)
	{
	  error ("two or more data types in declaration specifiers");
	  return specs;
	}
      if ((int) i <= (int) RID_LAST_MODIFIER)
	{
	  /* "long", "short", "signed", "unsigned" or "_Complex".  */
	  bool dupe = false;
	  switch (i)
	    {
	    case RID_LONG:
	      if (specs->long_long_p)
		{
		  error ("%<long long long%> is too long for GCC");
		  break;
		}
	      if (specs->long_p)
		{
		  if (specs->typespec_word == cts_double)
		    {
		      error ("both %<long long%> and %<double%> in "
			     "declaration specifiers");
		      break;
		    }
		  if (pedantic && !flag_isoc99 && !in_system_header
		      && warn_long_long)
		    pedwarn ("ISO C90 does not support %<long long%>");
		  specs->long_long_p = 1;
		  break;
		}
	      if (specs->short_p)
		error ("both %<long%> and %<short%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_void)
		error ("both %<long%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_bool)
		error ("both %<long%> and %<_Bool%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_char)
		error ("both %<long%> and %<char%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_float)
		error ("both %<long%> and %<float%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat32)
		error ("both %<long%> and %<_Decimal32%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat64)
		error ("both %<long%> and %<_Decimal64%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat128)
		error ("both %<long%> and %<_Decimal128%> in "
		       "declaration specifiers");
	      else
		specs->long_p = true;
	      break;
	    case RID_SHORT:
	      dupe = specs->short_p;
	      if (specs->long_p)
		error ("both %<long%> and %<short%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_void)
		error ("both %<short%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_bool)
		error ("both %<short%> and %<_Bool%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_char)
		error ("both %<short%> and %<char%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_float)
		error ("both %<short%> and %<float%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_double)
		error ("both %<short%> and %<double%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat32)
                error ("both %<short%> and %<_Decimal32%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat64)
		error ("both %<short%> and %<_Decimal64%> in "
		                        "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat128)
		error ("both %<short%> and %<_Decimal128%> in "
		       "declaration specifiers");
	      else
		specs->short_p = true;
	      break;
	    case RID_SIGNED:
	      dupe = specs->signed_p;
	      if (specs->unsigned_p)
		error ("both %<signed%> and %<unsigned%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_void)
		error ("both %<signed%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_bool)
		error ("both %<signed%> and %<_Bool%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_float)
		error ("both %<signed%> and %<float%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_double)
		error ("both %<signed%> and %<double%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat32)
		error ("both %<signed%> and %<_Decimal32%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat64)
		error ("both %<signed%> and %<_Decimal64%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat128)
		error ("both %<signed%> and %<_Decimal128%> in "
		       "declaration specifiers");
	      else
		specs->signed_p = true;
	      break;
	    case RID_UNSIGNED:
	      dupe = specs->unsigned_p;
	      if (specs->signed_p)
		error ("both %<signed%> and %<unsigned%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_void)
		error ("both %<unsigned%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_bool)
		error ("both %<unsigned%> and %<_Bool%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_float)
		error ("both %<unsigned%> and %<float%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_double)
		error ("both %<unsigned%> and %<double%> in "
		       "declaration specifiers");
              else if (specs->typespec_word == cts_dfloat32)
		error ("both %<unsigned%> and %<_Decimal32%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat64)
		error ("both %<unsigned%> and %<_Decimal64%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat128)
		error ("both %<unsigned%> and %<_Decimal128%> in "
		       "declaration specifiers");
	      else
		specs->unsigned_p = true;
	      break;
	    case RID_COMPLEX:
	      dupe = specs->complex_p;
	      if (pedantic && !flag_isoc99 && !in_system_header)
		pedwarn ("ISO C90 does not support complex types");
	      if (specs->typespec_word == cts_void)
		error ("both %<complex%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_bool)
		error ("both %<complex%> and %<_Bool%> in "
		       "declaration specifiers");
              else if (specs->typespec_word == cts_dfloat32)
		error ("both %<complex%> and %<_Decimal32%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat64)
		error ("both %<complex%> and %<_Decimal64%> in "
		       "declaration specifiers");
	      else if (specs->typespec_word == cts_dfloat128)
		error ("both %<complex%> and %<_Decimal128%> in "
		       "declaration specifiers");
	      else
		specs->complex_p = true;
	      break;
	    default:
	      gcc_unreachable ();
	    }

	  if (dupe)
	    error ("duplicate %qE", type);

	  return specs;
	}
      else
	{
	  /* "void", "_Bool", "char", "int", "float" or "double".  */
	  if (specs->typespec_word != cts_none)
	    {
	      error ("two or more data types in declaration specifiers");
	      return specs;
	    }
	  switch (i)
	    {
	    case RID_VOID:
	      if (specs->long_p)
		error ("both %<long%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->short_p)
		error ("both %<short%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->signed_p)
		error ("both %<signed%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->unsigned_p)
		error ("both %<unsigned%> and %<void%> in "
		       "declaration specifiers");
	      else if (specs->complex_p)
		error ("both %<complex%> and %<void%> in "
		       "declaration specifiers");
	      else
		specs->typespec_word = cts_void;
	      return specs;
	    case RID_BOOL:
	      if (specs->long_p)
		error ("both %<long%> and %<_Bool%> in "
		       "declaration specifiers");
	      else if (specs->short_p)
		error ("both %<short%> and %<_Bool%> in "
		       "declaration specifiers");
	      else if (specs->signed_p)
		error ("both %<signed%> and %<_Bool%> in "
		       "declaration specifiers");
	      else if (specs->unsigned_p)
		error ("both %<unsigned%> and %<_Bool%> in "
		       "declaration specifiers");
	      else if (specs->complex_p)
		error ("both %<complex%> and %<_Bool%> in "
		       "declaration specifiers");
	      else
		specs->typespec_word = cts_bool;
	      return specs;
	    case RID_CHAR:
	      if (specs->long_p)
		error ("both %<long%> and %<char%> in "
		       "declaration specifiers");
	      else if (specs->short_p)
		error ("both %<short%> and %<char%> in "
		       "declaration specifiers");
	      else
		specs->typespec_word = cts_char;
	      return specs;
	    case RID_INT:
	      specs->typespec_word = cts_int;
	      return specs;
	    case RID_FLOAT:
	      if (specs->long_p)
		error ("both %<long%> and %<float%> in "
		       "declaration specifiers");
	      else if (specs->short_p)
		error ("both %<short%> and %<float%> in "
		       "declaration specifiers");
	      else if (specs->signed_p)
		error ("both %<signed%> and %<float%> in "
		       "declaration specifiers");
	      else if (specs->unsigned_p)
		error ("both %<unsigned%> and %<float%> in "
		       "declaration specifiers");
	      else
		specs->typespec_word = cts_float;
	      return specs;
	    case RID_DOUBLE:
	      if (specs->long_long_p)
		error ("both %<long long%> and %<double%> in "
		       "declaration specifiers");
	      else if (specs->short_p)
		error ("both %<short%> and %<double%> in "
		       "declaration specifiers");
	      else if (specs->signed_p)
		error ("both %<signed%> and %<double%> in "
		       "declaration specifiers");
	      else if (specs->unsigned_p)
		error ("both %<unsigned%> and %<double%> in "
		       "declaration specifiers");
	      else
		specs->typespec_word = cts_double;
	      return specs;
	    case RID_DFLOAT32:
	    case RID_DFLOAT64:
	    case RID_DFLOAT128:
	      { 
		const char *str;
		if (i == RID_DFLOAT32)
		  str = "_Decimal32";
		else if (i == RID_DFLOAT64)
		  str = "_Decimal64";
		else
		  str = "_Decimal128";
		if (specs->long_long_p)
		  error ("both %<long long%> and %<%s%> in "
			 "declaration specifiers", str);
		if (specs->long_p)
		  error ("both %<long%> and %<%s%> in "
			 "declaration specifiers", str);
		else if (specs->short_p)
		  error ("both %<short%> and %<%s%> in "
			 "declaration specifiers", str);
		else if (specs->signed_p)
		  error ("both %<signed%> and %<%s%> in "
			 "declaration specifiers", str);
		else if (specs->unsigned_p)
		  error ("both %<unsigned%> and %<%s%> in "
			 "declaration specifiers", str);
                else if (specs->complex_p)
                  error ("both %<complex%> and %<%s%> in "
                         "declaration specifiers", str);
		else if (i == RID_DFLOAT32)
		  specs->typespec_word = cts_dfloat32;
		else if (i == RID_DFLOAT64)
		  specs->typespec_word = cts_dfloat64;
		else
		  specs->typespec_word = cts_dfloat128;
	      }
	      if (!targetm.decimal_float_supported_p ())
		error ("decimal floating point not supported for this target");
	      if (pedantic)
		pedwarn ("ISO C does not support decimal floating point");
	      return specs;
	    default:
	      /* ObjC reserved word "id", handled below.  */
	      break;
	    }
	}
    }

  /* Now we have a typedef (a TYPE_DECL node), an identifier (some
     form of ObjC type, cases such as "int" and "long" being handled
     above), a TYPE (struct, union, enum and typeof specifiers) or an
     ERROR_MARK.  In none of these cases may there have previously
     been any type specifiers.  */
  if (specs->type || specs->typespec_word != cts_none
      || specs->long_p || specs->short_p || specs->signed_p
      || specs->unsigned_p || specs->complex_p)
    error ("two or more data types in declaration specifiers");
  else if (TREE_CODE (type) == TYPE_DECL)
    {
      if (TREE_TYPE (type) == error_mark_node)
	; /* Allow the type to default to int to avoid cascading errors.  */
      else
	{
	  specs->type = TREE_TYPE (type);
	  specs->decl_attr = DECL_ATTRIBUTES (type);
	  specs->typedef_p = true;
	  specs->explicit_signed_p = C_TYPEDEF_EXPLICITLY_SIGNED (type);
	}
    }
  else if (TREE_CODE (type) == IDENTIFIER_NODE)
    {
      tree t = lookup_name (type);
      if (!t || TREE_CODE (t) != TYPE_DECL)
	error ("%qE fails to be a typedef or built in type", type);
      else if (TREE_TYPE (t) == error_mark_node)
	;
      else
	specs->type = TREE_TYPE (t);
    }
  else if (TREE_CODE (type) != ERROR_MARK)
    {
      if (spec.kind == ctsk_tagdef || spec.kind == ctsk_tagfirstref)
	specs->tag_defined_p = true;
      if (spec.kind == ctsk_typeof)
	specs->typedef_p = true;
      specs->type = type;
    }

  return specs;
}

/* Add the storage class specifier or function specifier SCSPEC to the
   declaration specifiers SPECS, returning SPECS.  */

struct c_declspecs *
declspecs_add_scspec (struct c_declspecs *specs, tree scspec)
{
  enum rid i;
  enum c_storage_class n = csc_none;
  bool dupe = false;
  specs->declspecs_seen_p = true;
  gcc_assert (TREE_CODE (scspec) == IDENTIFIER_NODE
	      && C_IS_RESERVED_WORD (scspec));
  i = C_RID_CODE (scspec);
  if (extra_warnings && specs->non_sc_seen_p)
    warning (OPT_Wextra, "%qE is not at beginning of declaration", scspec);
  switch (i)
    {
    case RID_INLINE:
      /* C99 permits duplicate inline.  Although of doubtful utility,
	 it seems simplest to permit it in gnu89 mode as well, as
	 there is also little utility in maintaining this as a
	 difference between gnu89 and C99 inline.  */
      dupe = false;
      specs->inline_p = true;
      break;
    case RID_THREAD:
      dupe = specs->thread_p;
      if (specs->storage_class == csc_auto)
	error ("%<__thread%> used with %<auto%>");
      else if (specs->storage_class == csc_register)
	error ("%<__thread%> used with %<register%>");
      else if (specs->storage_class == csc_typedef)
	error ("%<__thread%> used with %<typedef%>");
      else
	specs->thread_p = true;
      break;
    case RID_AUTO:
      n = csc_auto;
      break;
    case RID_EXTERN:
      n = csc_extern;
      /* Diagnose "__thread extern".  */
      if (specs->thread_p)
	error ("%<__thread%> before %<extern%>");
      break;
    case RID_REGISTER:
      n = csc_register;
      break;
    case RID_STATIC:
      n = csc_static;
      /* Diagnose "__thread static".  */
      if (specs->thread_p)
	error ("%<__thread%> before %<static%>");
      break;
    case RID_TYPEDEF:
      n = csc_typedef;
      break;
    default:
      gcc_unreachable ();
    }
  if (n != csc_none && n == specs->storage_class)
    dupe = true;
  if (dupe)
    error ("duplicate %qE", scspec);
  if (n != csc_none)
    {
      if (specs->storage_class != csc_none && n != specs->storage_class)
	{
	  error ("multiple storage classes in declaration specifiers");
	}
      else
	{
	  specs->storage_class = n;
	  if (n != csc_extern && n != csc_static && specs->thread_p)
	    {
	      error ("%<__thread%> used with %qE", scspec);
	      specs->thread_p = false;
	    }
	}
    }
  return specs;
}

/* Add the attributes ATTRS to the declaration specifiers SPECS,
   returning SPECS.  */

struct c_declspecs *
declspecs_add_attrs (struct c_declspecs *specs, tree attrs)
{
  specs->attrs = chainon (attrs, specs->attrs);
  specs->declspecs_seen_p = true;
  return specs;
}

/* Combine "long", "short", "signed", "unsigned" and "_Complex" type
   specifiers with any other type specifier to determine the resulting
   type.  This is where ISO C checks on complex types are made, since
   "_Complex long" is a prefix of the valid ISO C type "_Complex long
   double".  */

struct c_declspecs *
finish_declspecs (struct c_declspecs *specs)
{
  /* If a type was specified as a whole, we have no modifiers and are
     done.  */
  if (specs->type != NULL_TREE)
    {
      gcc_assert (!specs->long_p && !specs->long_long_p && !specs->short_p
		  && !specs->signed_p && !specs->unsigned_p
		  && !specs->complex_p);
      return specs;
    }

  /* If none of "void", "_Bool", "char", "int", "float" or "double"
     has been specified, treat it as "int" unless "_Complex" is
     present and there are no other specifiers.  If we just have
     "_Complex", it is equivalent to "_Complex double", but e.g.
     "_Complex short" is equivalent to "_Complex short int".  */
  if (specs->typespec_word == cts_none)
    {
      if (specs->long_p || specs->short_p
	  || specs->signed_p || specs->unsigned_p)
	{
	  specs->typespec_word = cts_int;
	}
      else if (specs->complex_p)
	{
	  specs->typespec_word = cts_double;
	  if (pedantic)
	    pedwarn ("ISO C does not support plain %<complex%> meaning "
		     "%<double complex%>");
	}
      else
	{
	  specs->typespec_word = cts_int;
	  specs->default_int_p = true;
	  /* We don't diagnose this here because grokdeclarator will
	     give more specific diagnostics according to whether it is
	     a function definition.  */
	}
    }

  /* If "signed" was specified, record this to distinguish "int" and
     "signed int" in the case of a bit-field with
     -funsigned-bitfields.  */
  specs->explicit_signed_p = specs->signed_p;

  /* Now compute the actual type.  */
  switch (specs->typespec_word)
    {
    case cts_void:
      gcc_assert (!specs->long_p && !specs->short_p
		  && !specs->signed_p && !specs->unsigned_p
		  && !specs->complex_p);
      specs->type = void_type_node;
      break;
    case cts_bool:
      gcc_assert (!specs->long_p && !specs->short_p
		  && !specs->signed_p && !specs->unsigned_p
		  && !specs->complex_p);
      specs->type = boolean_type_node;
      break;
    case cts_char:
      gcc_assert (!specs->long_p && !specs->short_p);
      gcc_assert (!(specs->signed_p && specs->unsigned_p));
      if (specs->signed_p)
	specs->type = signed_char_type_node;
      else if (specs->unsigned_p)
	specs->type = unsigned_char_type_node;
      else
	specs->type = char_type_node;
      if (specs->complex_p)
	{
	  if (pedantic)
	    pedwarn ("ISO C does not support complex integer types");
	  specs->type = build_complex_type (specs->type);
	}
      break;
    case cts_int:
      gcc_assert (!(specs->long_p && specs->short_p));
      gcc_assert (!(specs->signed_p && specs->unsigned_p));
      if (specs->long_long_p)
	specs->type = (specs->unsigned_p
		       ? long_long_unsigned_type_node
		       : long_long_integer_type_node);
      else if (specs->long_p)
	specs->type = (specs->unsigned_p
		       ? long_unsigned_type_node
		       : long_integer_type_node);
      else if (specs->short_p)
	specs->type = (specs->unsigned_p
		       ? short_unsigned_type_node
		       : short_integer_type_node);
      else
	specs->type = (specs->unsigned_p
		       ? unsigned_type_node
		       : integer_type_node);
      if (specs->complex_p)
	{
	  if (pedantic)
	    pedwarn ("ISO C does not support complex integer types");
	  specs->type = build_complex_type (specs->type);
	}
      break;
    case cts_float:
      gcc_assert (!specs->long_p && !specs->short_p
		  && !specs->signed_p && !specs->unsigned_p);
      specs->type = (specs->complex_p
		     ? complex_float_type_node
		     : float_type_node);
      break;
    case cts_double:
      gcc_assert (!specs->long_long_p && !specs->short_p
		  && !specs->signed_p && !specs->unsigned_p);
      if (specs->long_p)
	{
	  specs->type = (specs->complex_p
			 ? complex_long_double_type_node
			 : long_double_type_node);
	}
      else
	{
	  specs->type = (specs->complex_p
			 ? complex_double_type_node
			 : double_type_node);
	}
      break;
    case cts_dfloat32:
    case cts_dfloat64:
    case cts_dfloat128:
      gcc_assert (!specs->long_p && !specs->long_long_p && !specs->short_p
		  && !specs->signed_p && !specs->unsigned_p && !specs->complex_p);
      if (specs->typespec_word == cts_dfloat32)
	specs->type = dfloat32_type_node;
      else if (specs->typespec_word == cts_dfloat64)
	specs->type = dfloat64_type_node;
      else
	specs->type = dfloat128_type_node;
      break;
    default:
      gcc_unreachable ();
    }

  return specs;
}

/* Synthesize a function which calls all the global ctors or global
   dtors in this file.  This is only used for targets which do not
   support .ctors/.dtors sections.  FIXME: Migrate into cgraph.  */
static void
build_cdtor (int method_type, tree cdtors)
{
  tree body = 0;

  if (!cdtors)
    return;

  for (; cdtors; cdtors = TREE_CHAIN (cdtors))
    append_to_statement_list (build_function_call (TREE_VALUE (cdtors), 0),
			      &body);

  cgraph_build_static_cdtor (method_type, body, DEFAULT_INIT_PRIORITY);
}

/* A subroutine of c_write_global_declarations.  Perform final processing
   on one file scope's declarations (or the external scope's declarations),
   GLOBALS.  */

static void
c_write_global_declarations_1 (tree globals)
{
  tree decl;
  bool reconsider;

  /* Process the decls in the order they were written.  */
  for (decl = globals; decl; decl = TREE_CHAIN (decl))
    {
      /* Check for used but undefined static functions using the C
	 standard's definition of "used", and set TREE_NO_WARNING so
	 that check_global_declarations doesn't repeat the check.  */
      if (TREE_CODE (decl) == FUNCTION_DECL
	  && DECL_INITIAL (decl) == 0
	  && DECL_EXTERNAL (decl)
	  && !TREE_PUBLIC (decl)
	  && C_DECL_USED (decl))
	{
	  pedwarn ("%q+F used but never defined", decl);
	  TREE_NO_WARNING (decl) = 1;
	}

      wrapup_global_declaration_1 (decl);
    }

  do
    {
      reconsider = false;
      for (decl = globals; decl; decl = TREE_CHAIN (decl))
	reconsider |= wrapup_global_declaration_2 (decl);
    }
  while (reconsider);

  for (decl = globals; decl; decl = TREE_CHAIN (decl))
    check_global_declaration_1 (decl);
}

/* A subroutine of c_write_global_declarations Emit debug information for each
   of the declarations in GLOBALS.  */

static void
c_write_global_declarations_2 (tree globals)
{
  tree decl;

  for (decl = globals; decl ; decl = TREE_CHAIN (decl))
    debug_hooks->global_decl (decl);
}

/* Preserve the external declarations scope across a garbage collect.  */
static GTY(()) tree ext_block;

void
c_write_global_declarations (void)
{
  tree t;

  /* We don't want to do this if generating a PCH.  */
  if (pch_file)
    return;

  /* Don't waste time on further processing if -fsyntax-only or we've
     encountered errors.  */
  if (flag_syntax_only || errorcount || sorrycount || cpp_errors (parse_in))
    return;

  /* Close the external scope.  */
  ext_block = pop_scope ();
  external_scope = 0;
  gcc_assert (!current_scope);

  if (ext_block)
    {
      tree tmp = BLOCK_VARS (ext_block);
      int flags;
      FILE * stream = dump_begin (TDI_tu, &flags);
      if (stream && tmp)
	{
	  dump_node (tmp, flags & ~TDF_SLIM, stream);
	  dump_end (TDI_tu, stream);
	}
    }

  /* Process all file scopes in this compilation, and the external_scope,
     through wrapup_global_declarations and check_global_declarations.  */
  for (t = all_translation_units; t; t = TREE_CHAIN (t))
    c_write_global_declarations_1 (BLOCK_VARS (DECL_INITIAL (t)));
  c_write_global_declarations_1 (BLOCK_VARS (ext_block));

  /* Generate functions to call static constructors and destructors
     for targets that do not support .ctors/.dtors sections.  These
     functions have magic names which are detected by collect2.  */
  build_cdtor ('I', static_ctors); static_ctors = 0;
  build_cdtor ('D', static_dtors); static_dtors = 0;

  /* We're done parsing; proceed to optimize and emit assembly.
     FIXME: shouldn't be the front end's responsibility to call this.  */
  cgraph_optimize ();

  /* After cgraph has had a chance to emit everything that's going to
     be emitted, output debug information for globals.  */
  if (errorcount == 0 && sorrycount == 0)
    {
      timevar_push (TV_SYMOUT);
      for (t = all_translation_units; t; t = TREE_CHAIN (t))
	c_write_global_declarations_2 (BLOCK_VARS (DECL_INITIAL (t)));
      c_write_global_declarations_2 (BLOCK_VARS (ext_block));
      timevar_pop (TV_SYMOUT);
    }

  ext_block = NULL;
}

/* APPLE LOCAL begin radar 5741070  */

/* Given an IDENTIFIER tree for a class interface, find (if possible) and
   return the record type for the class interface.  */

tree
c_return_interface_record_type (tree typename)
{
  enum tree_code_class class;
  enum tree_code code;
  tree retval = NULL;

  if (typename == NULL)
    return retval;

  code = TREE_CODE (typename);
  class = TREE_CODE_CLASS (code);

  if (code != IDENTIFIER_NODE
      || class != tcc_exceptional)
    return retval;

  retval = I_TAG_DECL (typename);

  if (TREE_CODE (retval) != RECORD_TYPE)
    retval = NULL;

  return retval;
}
/* APPLE LOCAL end radar 5741070  */

#include "gt-c-decl.h"
