/* Help friends in C++.
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

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
#include "rtl.h"
#include "expr.h"
#include "cp-tree.h"
#include "flags.h"
#include "output.h"
#include "toplev.h"

/* Friend data structures are described in cp-tree.h.  */

/* Returns nonzero if SUPPLICANT is a friend of TYPE.  */

int
is_friend (tree type, tree supplicant)
{
  int declp;
  tree list;
  tree context;

  if (supplicant == NULL_TREE || type == NULL_TREE)
    return 0;

  declp = DECL_P (supplicant);

  if (declp)
    /* It's a function decl.  */
    {
      tree list = DECL_FRIENDLIST (TYPE_MAIN_DECL (type));
      tree name = DECL_NAME (supplicant);

      for (; list ; list = TREE_CHAIN (list))
	{
	  if (name == FRIEND_NAME (list))
	    {
	      tree friends = FRIEND_DECLS (list);
	      for (; friends ; friends = TREE_CHAIN (friends))
		{
		  tree friend = TREE_VALUE (friends);

		  if (friend == NULL_TREE)
		    continue;

		  if (supplicant == friend)
		    return 1;

		  if (is_specialization_of_friend (supplicant, friend))
		    return 1;
		}
	      break;
	    }
	}
    }
  else
    /* It's a type.  */
    {
      if (same_type_p (supplicant, type))
	return 1;

      list = CLASSTYPE_FRIEND_CLASSES (TREE_TYPE (TYPE_MAIN_DECL (type)));
      for (; list ; list = TREE_CHAIN (list))
	{
	  tree t = TREE_VALUE (list);

	  if (TREE_CODE (t) == TEMPLATE_DECL ?
	      is_specialization_of_friend (TYPE_MAIN_DECL (supplicant), t) :
	      same_type_p (supplicant, t))
	    return 1;
	}
    }

  if (declp)
    {
      if (DECL_FUNCTION_MEMBER_P (supplicant))
	context = DECL_CONTEXT (supplicant);
      else
	context = NULL_TREE;
    }
  else
    {
      if (TYPE_CLASS_SCOPE_P (supplicant))
	/* Nested classes get the same access as their enclosing types, as
	   per DR 45 (this is a change from the standard).  */
	context = TYPE_CONTEXT (supplicant);
      else
	/* Local classes have the same access as the enclosing function.  */
	context = decl_function_context (TYPE_MAIN_DECL (supplicant));
    }

  /* A namespace is not friend to anybody.  */
  if (context && TREE_CODE (context) == NAMESPACE_DECL)
    context = NULL_TREE;

  if (context)
    return is_friend (type, context);

  return 0;
}

/* Add a new friend to the friends of the aggregate type TYPE.
   DECL is the FUNCTION_DECL of the friend being added.

   If COMPLAIN is true, warning about duplicate friend is issued.
   We want to have this diagnostics during parsing but not
   when a template is being instantiated.  */

void
add_friend (tree type, tree decl, bool complain)
{
  tree typedecl;
  tree list;
  tree name;
  tree ctx;

  if (decl == error_mark_node)
    return;

  typedecl = TYPE_MAIN_DECL (type);
  list = DECL_FRIENDLIST (typedecl);
  name = DECL_NAME (decl);
  type = TREE_TYPE (typedecl);

  while (list)
    {
      if (name == FRIEND_NAME (list))
	{
	  tree friends = FRIEND_DECLS (list);
	  for (; friends ; friends = TREE_CHAIN (friends))
	    {
	      if (decl == TREE_VALUE (friends))
		{
		  if (complain)
		    warning (0, "%qD is already a friend of class %qT",
			     decl, type);
		  return;
		}
	    }

	  maybe_add_class_template_decl_list (type, decl, /*friend_p=*/1);

	  TREE_VALUE (list) = tree_cons (NULL_TREE, decl,
					 TREE_VALUE (list));
	  return;
	}
      list = TREE_CHAIN (list);
    }

  ctx = DECL_CONTEXT (decl);
  if (ctx && CLASS_TYPE_P (ctx) && !uses_template_parms (ctx))
    perform_or_defer_access_check (TYPE_BINFO (ctx), decl, decl);

  maybe_add_class_template_decl_list (type, decl, /*friend_p=*/1);

  DECL_FRIENDLIST (typedecl)
    = tree_cons (DECL_NAME (decl), build_tree_list (NULL_TREE, decl),
		 DECL_FRIENDLIST (typedecl));
  if (!uses_template_parms (type))
    DECL_BEFRIENDING_CLASSES (decl)
      = tree_cons (NULL_TREE, type,
		   DECL_BEFRIENDING_CLASSES (decl));
}

/* Make FRIEND_TYPE a friend class to TYPE.  If FRIEND_TYPE has already
   been defined, we make all of its member functions friends of
   TYPE.  If not, we make it a pending friend, which can later be added
   when its definition is seen.  If a type is defined, then its TYPE_DECL's
   DECL_UNDEFINED_FRIENDS contains a (possibly empty) list of friend
   classes that are not defined.  If a type has not yet been defined,
   then the DECL_WAITING_FRIENDS contains a list of types
   waiting to make it their friend.  Note that these two can both
   be in use at the same time!

   If COMPLAIN is true, warning about duplicate friend is issued.
   We want to have this diagnostics during parsing but not
   when a template is being instantiated.  */

void
make_friend_class (tree type, tree friend_type, bool complain)
{
  tree classes;

  /* CLASS_TEMPLATE_DEPTH counts the number of template headers for
     the enclosing class.  FRIEND_DEPTH counts the number of template
     headers used for this friend declaration.  TEMPLATE_MEMBER_P,
     defined inside the `if' block for TYPENAME_TYPE case, is true if
     a template header in FRIEND_DEPTH is intended for DECLARATOR.
     For example, the code

       template <class T> struct A {
	 template <class U> struct B {
	   template <class V> template <class W>
	     friend class C<V>::D;
	 };
       };

     will eventually give the following results

     1. CLASS_TEMPLATE_DEPTH equals 2 (for `T' and `U').
     2. FRIEND_DEPTH equals 2 (for `V' and `W').
     3. TEMPLATE_MEMBER_P is true (for `W').

     The friend is a template friend iff FRIEND_DEPTH is nonzero.  */

  int class_template_depth = template_class_depth (type);
  int friend_depth = processing_template_decl - class_template_depth;

  if (! IS_AGGR_TYPE (friend_type))
    {
      error ("invalid type %qT declared %<friend%>", friend_type);
      return;
    }

  if (friend_depth)
    /* If the TYPE is a template then it makes sense for it to be
       friends with itself; this means that each instantiation is
       friends with all other instantiations.  */
    {
      if (CLASS_TYPE_P (friend_type)
	  && CLASSTYPE_TEMPLATE_SPECIALIZATION (friend_type)
	  && uses_template_parms (friend_type))
	{
	  /* [temp.friend]
	     Friend declarations shall not declare partial
	     specializations.  */
	  error ("partial specialization %qT declared %<friend%>",
		 friend_type);
	  return;
	}
    }
  else if (same_type_p (type, friend_type))
    {
      if (complain)
	pedwarn ("class %qT is implicitly friends with itself",
		 type);
      return;
    }

  /* [temp.friend]

     A friend of a class or class template can be a function or
     class template, a specialization of a function template or
     class template, or an ordinary (nontemplate) function or
     class.  */
  if (!friend_depth)
    ;/* ok */
  else if (TREE_CODE (friend_type) == TYPENAME_TYPE)
    {
      if (TREE_CODE (TYPENAME_TYPE_FULLNAME (friend_type))
	  == TEMPLATE_ID_EXPR)
	{
	  /* template <class U> friend class T::X<U>; */
	  /* [temp.friend]
	     Friend declarations shall not declare partial
	     specializations.  */
	  error ("partial specialization %qT declared %<friend%>",
		 friend_type);
	  return;
	}
      else
	{
	  /* We will figure this out later.  */
	  bool template_member_p = false;

	  tree ctype = TYPE_CONTEXT (friend_type);
	  tree name = TYPE_IDENTIFIER (friend_type);
	  tree decl;

	  if (!uses_template_parms_level (ctype, class_template_depth
						 + friend_depth))
	    template_member_p = true;

	  if (class_template_depth)
	    {
	      /* We rely on tsubst_friend_class to check the
		 validity of the declaration later.  */
	      if (template_member_p)
		friend_type
		  = make_unbound_class_template (ctype,
						 name,
						 current_template_parms,
						 tf_error);
	      else
		friend_type
		  = make_typename_type (ctype, name, class_type, tf_error);
	    }
	  else
	    {
	      decl = lookup_member (ctype, name, 0, true);
	      if (!decl)
		{
		  error ("%qT is not a member of %qT", name, ctype);
		  return;
		}
	      if (template_member_p && !DECL_CLASS_TEMPLATE_P (decl))
		{
		  error ("%qT is not a member class template of %qT",
			 name, ctype);
		  error ("%q+D declared here", decl);
		  return;
		}
	      if (!template_member_p && (TREE_CODE (decl) != TYPE_DECL
					 || !CLASS_TYPE_P (TREE_TYPE (decl))))
		{
		  error ("%qT is not a nested class of %qT",
			 name, ctype);
		  error ("%q+D declared here", decl);
		  return;
		}

	      friend_type = CLASSTYPE_TI_TEMPLATE (TREE_TYPE (decl));
	    }
	}
    }
  else if (TREE_CODE (friend_type) == TEMPLATE_TYPE_PARM)
    {
      /* template <class T> friend class T; */
      error ("template parameter type %qT declared %<friend%>", friend_type);
      return;
    }
  else if (!CLASSTYPE_TEMPLATE_INFO (friend_type))
    {
      /* template <class T> friend class A; where A is not a template */
      error ("%q#T is not a template", friend_type);
      return;
    }
  else
    /* template <class T> friend class A; where A is a template */
    friend_type = CLASSTYPE_TI_TEMPLATE (friend_type);

  if (friend_type == error_mark_node)
    return;

  /* See if it is already a friend.  */
  for (classes = CLASSTYPE_FRIEND_CLASSES (type);
       classes;
       classes = TREE_CHAIN (classes))
    {
      tree probe = TREE_VALUE (classes);

      if (TREE_CODE (friend_type) == TEMPLATE_DECL)
	{
	  if (friend_type == probe)
	    {
	      if (complain)
		warning (0, "%qD is already a friend of %qT", probe, type);
	      break;
	    }
	}
      else if (TREE_CODE (probe) != TEMPLATE_DECL)
	{
	  if (same_type_p (probe, friend_type))
	    {
	      if (complain)
		warning (0, "%qT is already a friend of %qT", probe, type);
	      break;
	    }
	}
    }

  if (!classes)
    {
      maybe_add_class_template_decl_list (type, friend_type, /*friend_p=*/1);

      CLASSTYPE_FRIEND_CLASSES (type)
	= tree_cons (NULL_TREE, friend_type, CLASSTYPE_FRIEND_CLASSES (type));
      if (TREE_CODE (friend_type) == TEMPLATE_DECL)
	friend_type = TREE_TYPE (friend_type);
      if (!uses_template_parms (type))
	CLASSTYPE_BEFRIENDING_CLASSES (friend_type)
	  = tree_cons (NULL_TREE, type,
		       CLASSTYPE_BEFRIENDING_CLASSES (friend_type));
    }
}

/* Record DECL (a FUNCTION_DECL) as a friend of the
   CURRENT_CLASS_TYPE.  If DECL is a member function, CTYPE is the
   class of which it is a member, as named in the friend declaration.
   DECLARATOR is the name of the friend.  FUNCDEF_FLAG is true if the
   friend declaration is a definition of the function.  FLAGS is as
   for grokclass fn.  */

tree
do_friend (tree ctype, tree declarator, tree decl,
	   tree attrlist, enum overload_flags flags,
	   bool funcdef_flag)
{
  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL);
  gcc_assert (!ctype || IS_AGGR_TYPE (ctype));

  /* Every decl that gets here is a friend of something.  */
  DECL_FRIEND_P (decl) = 1;

  if (TREE_CODE (declarator) == TEMPLATE_ID_EXPR)
    {
      declarator = TREE_OPERAND (declarator, 0);
      if (is_overloaded_fn (declarator))
	declarator = DECL_NAME (get_first_fn (declarator));
    }

  if (ctype)
    {
      /* CLASS_TEMPLATE_DEPTH counts the number of template headers for
	 the enclosing class.  FRIEND_DEPTH counts the number of template
	 headers used for this friend declaration.  TEMPLATE_MEMBER_P is
	 true if a template header in FRIEND_DEPTH is intended for
	 DECLARATOR.  For example, the code

	   template <class T> struct A {
	     template <class U> struct B {
	       template <class V> template <class W>
		 friend void C<V>::f(W);
	     };
	   };

	 will eventually give the following results

	 1. CLASS_TEMPLATE_DEPTH equals 2 (for `T' and `U').
	 2. FRIEND_DEPTH equals 2 (for `V' and `W').
	 3. TEMPLATE_MEMBER_P is true (for `W').  */

      int class_template_depth = template_class_depth (current_class_type);
      int friend_depth = processing_template_decl - class_template_depth;
      /* We will figure this out later.  */
      bool template_member_p = false;

      tree cname = TYPE_NAME (ctype);
      if (TREE_CODE (cname) == TYPE_DECL)
	cname = DECL_NAME (cname);

      /* A method friend.  */
      if (flags == NO_SPECIAL && declarator == cname)
	DECL_CONSTRUCTOR_P (decl) = 1;

      grokclassfn (ctype, decl, flags);

      if (friend_depth)
	{
	  if (!uses_template_parms_level (ctype, class_template_depth
						 + friend_depth))
	    template_member_p = true;
	}

      /* A nested class may declare a member of an enclosing class
	 to be a friend, so we do lookup here even if CTYPE is in
	 the process of being defined.  */
      if (class_template_depth
	  || COMPLETE_TYPE_P (ctype)
	  || TYPE_BEING_DEFINED (ctype))
	{
	  if (DECL_TEMPLATE_INFO (decl))
	    /* DECL is a template specialization.  No need to
	       build a new TEMPLATE_DECL.  */
	    ;
	  else if (class_template_depth)
	    /* We rely on tsubst_friend_function to check the
	       validity of the declaration later.  */
	    decl = push_template_decl_real (decl, /*is_friend=*/true);
	  else
	    decl = check_classfn (ctype, decl,
				  template_member_p
				  ? current_template_parms
				  : NULL_TREE);

	  if (template_member_p && decl && TREE_CODE (decl) == FUNCTION_DECL)
	    decl = DECL_TI_TEMPLATE (decl);

	  if (decl)
	    add_friend (current_class_type, decl, /*complain=*/true);
	}
      else
	error ("member %qD declared as friend before type %qT defined",
		  decl, ctype);
    }
  /* A global friend.
     @@ or possibly a friend from a base class ?!?  */
  else if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      int is_friend_template = PROCESSING_REAL_TEMPLATE_DECL_P ();

      /* Friends must all go through the overload machinery,
	 even though they may not technically be overloaded.

	 Note that because classes all wind up being top-level
	 in their scope, their friend wind up in top-level scope as well.  */
      if (funcdef_flag)
	SET_DECL_FRIEND_CONTEXT (decl, current_class_type);

      if (! DECL_USE_TEMPLATE (decl))
	{
	  /* We must check whether the decl refers to template
	     arguments before push_template_decl_real adds a
	     reference to the containing template class.  */
	  int warn = (warn_nontemplate_friend
		      && ! funcdef_flag && ! is_friend_template
		      && current_template_parms
		      && uses_template_parms (decl));

	  if (is_friend_template
	      || template_class_depth (current_class_type) != 0)
	    /* We can't call pushdecl for a template class, since in
	       general, such a declaration depends on template
	       parameters.  Instead, we call pushdecl when the class
	       is instantiated.  */
	    decl = push_template_decl_real (decl, /*is_friend=*/true);
	  else if (current_function_decl)
	    /* This must be a local class, so pushdecl will be ok, and
	       insert an unqualified friend into the local scope
	       (rather than the containing namespace scope, which the
	       next choice will do).  */
	    decl = pushdecl_maybe_friend (decl, /*is_friend=*/true);
	  else
	    {
	      /* We can't use pushdecl, as we might be in a template
		 class specialization, and pushdecl will insert an
		 unqualified friend decl into the template parameter
		 scope, rather than the namespace containing it.  */
	      tree ns = decl_namespace_context (decl);

	      push_nested_namespace (ns);
	      decl = pushdecl_namespace_level (decl, /*is_friend=*/true);
	      pop_nested_namespace (ns);
	    }

	  if (warn)
	    {
	      static int explained;
	      warning (0, "friend declaration %q#D declares a non-template "
		       "function", decl);
	      if (! explained)
		{
		  warning (0, "(if this is not what you intended, make sure "
			   "the function template has already been declared "
			   "and add <> after the function name here) "
			   "-Wno-non-template-friend disables this warning");
		  explained = 1;
		}
	    }
	}

      if (decl == error_mark_node)
	return error_mark_node;

      add_friend (current_class_type,
		  is_friend_template ? DECL_TI_TEMPLATE (decl) : decl,
		  /*complain=*/true);
      DECL_FRIEND_P (decl) = 1;
    }

  /* Unfortunately, we have to handle attributes here.  Normally we would
     handle them in start_decl_1, but since this is a friend decl start_decl_1
     never gets to see it.  */

  /* Set attributes here so if duplicate decl, will have proper attributes.  */
  cplus_decl_attributes (&decl, attrlist, 0);

  return decl;
}
