/* Target support for C++ classes on Windows.
   Contributed by Danny Smith (dannysmith@users.sourceforge.net)
   Copyright (C) 2005
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
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "output.h"
#include "tree.h"
#include "cp/cp-tree.h" /* this is why we're a separate module */
#include "flags.h"
#include "tm_p.h"
#include "toplev.h"
#include "hashtab.h"

bool
i386_pe_type_dllimport_p (tree decl)
{
   gcc_assert (TREE_CODE (decl) == VAR_DECL 
               || TREE_CODE (decl) == FUNCTION_DECL);

   if (TARGET_NOP_FUN_DLLIMPORT && TREE_CODE (decl) == FUNCTION_DECL)
     return false;

   /* We ignore the dllimport attribute for inline member functions.
      This differs from MSVC behavior which treats it like GNUC
      'extern inline' extension.  Also ignore for template
      instantiations with linkonce semantics and artificial methods.  */
    if (TREE_CODE (decl) ==  FUNCTION_DECL
        && (DECL_DECLARED_INLINE_P (decl)
	    || DECL_TEMPLATE_INSTANTIATION (decl)
	    || DECL_ARTIFICIAL (decl)))
      return false;

   /* Since we can't treat a pointer to a dllimport'd symbol as a
       constant address, we turn off the attribute on C++ virtual
       methods to allow creation of vtables using thunks.  */
    else if (TREE_CODE (TREE_TYPE (decl)) == METHOD_TYPE
	     && DECL_VIRTUAL_P (decl))
      {
	/* Even though we ignore the attribute from the start, warn if we later see
	   an out-of class definition, as we do for other member functions in
	   tree.c:merge_dllimport_decl_attributes.  If this is the key method, the
	   definition may affect the import-export status of vtables, depending
           on how we handle MULTIPLE_SYMBOL_SPACES in cp/decl2.c.   */
	if (DECL_INITIAL (decl))
	  {
	    warning (OPT_Wattributes, "%q+D redeclared without dllimport attribute: "
		    "previous dllimport ignored", decl);
#ifdef PE_DLL_DEBUG
	    if (decl == CLASSTYPE_KEY_METHOD (DECL_CONTEXT (decl)))            
	      warning (OPT_Wattributes, "key method %q+D of dllimport'd class defined"
		       decl);
#endif
	  }
	return false;
      }

      /* Don't mark defined functions as dllimport.  This code will only be
         reached if we see a non-inline function defined out-of-class.  */
    else if (TREE_CODE (decl) ==  FUNCTION_DECL
	     && (DECL_INITIAL (decl)))
      return false;

    /*  Don't allow definitions of static data members in dllimport class,
        If vtable data is marked as DECL_EXTERNAL, import it; otherwise just
        ignore the class attribute.  */
    else if (TREE_CODE (decl) == VAR_DECL
	     && TREE_STATIC (decl) && TREE_PUBLIC (decl)
	     && !DECL_EXTERNAL (decl))
      {
	if (!DECL_VIRTUAL_P (decl))
	     error ("definition of static data member %q+D of "
		    "dllimport'd class", decl);
	return false;
      }

    return true;
}


bool
i386_pe_type_dllexport_p (tree decl)
{
   gcc_assert (TREE_CODE (decl) == VAR_DECL 
               || TREE_CODE (decl) == FUNCTION_DECL);
   /* Avoid exporting compiler-generated default dtors and copy ctors.
      The only artificial methods that need to be exported are virtual
      and non-virtual thunks.  */
   if (TREE_CODE (TREE_TYPE (decl)) == METHOD_TYPE
       && DECL_ARTIFICIAL (decl) && !DECL_THUNK_P (decl))
     return false;
   return true;
}

static inline void maybe_add_dllimport (tree decl) 
{
  if (i386_pe_type_dllimport_p (decl))
    DECL_DLLIMPORT_P (decl) = 1;   
}

void
i386_pe_adjust_class_at_definition (tree t)
{
  tree member;

  gcc_assert (CLASS_TYPE_P (t));

 /* We only look at dllimport.  The only thing that dllexport does is
    add stuff to a '.drectiv' section at end-of-file, so no need to do
    anything for dllexport'd classes until we generate RTL. */  
  if (lookup_attribute ("dllimport", TYPE_ATTRIBUTES (t)) == NULL_TREE)
    return;

  /* We don't actually add the attribute to the decl, just set the flag
     that signals that the address of this symbol is not a compile-time
     constant.   Any subsequent out-of-class declaration of members wil
     cause the DECL_DLLIMPORT_P flag to be unset.
     (See  tree.c: merge_dllimport_decl_attributes).
     That is just right since out-of class declarations can only be a
     definition.  We recheck the class members  at RTL generation to
     emit warnings if this has happened.  Definition of static data member
     of dllimport'd class always causes an error (as per MS compiler).
     */

  /* Check static VAR_DECL's.  */
  for (member = TYPE_FIELDS (t); member; member = TREE_CHAIN (member))
    if (TREE_CODE (member) == VAR_DECL)     
      maybe_add_dllimport (member);
    
  /* Check FUNCTION_DECL's.  */
  for (member = TYPE_METHODS (t); member;  member = TREE_CHAIN (member))
    if (TREE_CODE (member) == FUNCTION_DECL)
      maybe_add_dllimport (member);
 
  /* Check vtables  */
  for (member = CLASSTYPE_VTABLES (t); member;  member = TREE_CHAIN (member))
    if (TREE_CODE (member) == VAR_DECL) 
      maybe_add_dllimport (member);

/* We leave typeinfo tables alone.  We can't mark TI objects as
     dllimport, since the address of a secondary VTT may be needed
     for static initialization of a primary VTT.  VTT's  of
     dllimport'd classes should always be link-once COMDAT.  */ 
}
