/****************************************************************************
 * Copyright (c) 1998-2010,2012 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *   Author:  Juergen Pfeifer, 1995,1997                                    *
 ****************************************************************************/

#include "form.priv.h"

MODULE_ID("$Id: frm_hook.c,v 1.16 2012/03/11 00:37:16 tom Exp $")

/* "Template" macro to generate function to set application specific hook */
#define GEN_HOOK_SET_FUNCTION( typ, name ) \
NCURSES_IMPEXP int NCURSES_API set_ ## typ ## _ ## name (FORM *form, Form_Hook func)\
{\
   T((T_CALLED("set_" #typ"_"#name"(%p,%p)"), (void *) form, func));\
   (Normalize_Form( form ) -> typ ## name) = func ;\
   RETURN(E_OK);\
}

/* "Template" macro to generate function to get application specific hook */
#define GEN_HOOK_GET_FUNCTION( typ, name ) \
NCURSES_IMPEXP Form_Hook NCURSES_API typ ## _ ## name ( const FORM *form )\
{\
   T((T_CALLED(#typ "_" #name "(%p)"), (const void *) form));\
   returnFormHook( Normalize_Form( form ) -> typ ## name );\
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_field_init(FORM *form, Form_Hook f)
|
|   Description   :  Assigns an application defined initialization function
|                    to be called when the form is posted and just after
|                    the current field changes.
|
|   Return Values :  E_OK      - success
+--------------------------------------------------------------------------*/
GEN_HOOK_SET_FUNCTION(field, init)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  Form_Hook field_init(const FORM *form)
|
|   Description   :  Retrieve field initialization routine address.
|
|   Return Values :  The address or NULL if no hook defined.
+--------------------------------------------------------------------------*/
GEN_HOOK_GET_FUNCTION(field, init)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_field_term(FORM *form, Form_Hook f)
|
|   Description   :  Assigns an application defined finalization function
|                    to be called when the form is unposted and just before
|                    the current field changes.
|
|   Return Values :  E_OK      - success
+--------------------------------------------------------------------------*/
GEN_HOOK_SET_FUNCTION(field, term)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  Form_Hook field_term(const FORM *form)
|
|   Description   :  Retrieve field finalization routine address.
|
|   Return Values :  The address or NULL if no hook defined.
+--------------------------------------------------------------------------*/
GEN_HOOK_GET_FUNCTION(field, term)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_form_init(FORM *form, Form_Hook f)
|
|   Description   :  Assigns an application defined initialization function
|                    to be called when the form is posted and just after
|                    a page change.
|
|   Return Values :  E_OK       - success
+--------------------------------------------------------------------------*/
GEN_HOOK_SET_FUNCTION(form, init)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  Form_Hook form_init(const FORM *form)
|
|   Description   :  Retrieve form initialization routine address.
|
|   Return Values :  The address or NULL if no hook defined.
+--------------------------------------------------------------------------*/
GEN_HOOK_GET_FUNCTION(form, init)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_form_term(FORM *form, Form_Hook f)
|
|   Description   :  Assigns an application defined finalization function
|                    to be called when the form is unposted and just before
|                    a page change.
|
|   Return Values :  E_OK       - success
+--------------------------------------------------------------------------*/
GEN_HOOK_SET_FUNCTION(form, term)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  Form_Hook form_term(const FORM *form)
|
|   Description   :  Retrieve form finalization routine address.
|
|   Return Values :  The address or NULL if no hook defined.
+--------------------------------------------------------------------------*/
GEN_HOOK_GET_FUNCTION(form, term)

/* frm_hook.c ends here */
