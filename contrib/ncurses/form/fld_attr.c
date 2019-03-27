/****************************************************************************
 * Copyright (c) 1998-2004,2010 Free Software Foundation, Inc.              *
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

MODULE_ID("$Id: fld_attr.c,v 1.11 2010/01/23 21:12:08 tom Exp $")

/*----------------------------------------------------------------------------
  Field-Attribute manipulation routines
  --------------------------------------------------------------------------*/
/* "Template" macro to generate a function to set a fields attribute */
#define GEN_FIELD_ATTR_SET_FCT( name ) \
NCURSES_IMPEXP int NCURSES_API set_field_ ## name (FIELD * field, chtype attr)\
{\
   int res = E_BAD_ARGUMENT;\
   T((T_CALLED("set_field_" #name "(%p,%s)"), field, _traceattr(attr)));\
   if ( attr==A_NORMAL || ((attr & A_ATTRIBUTES)==attr) )\
     {\
       Normalize_Field( field );\
       if (field != 0) \
	 { \
	 if ((field -> name) != attr)\
	   {\
	     field -> name = attr;\
	     res = _nc_Synchronize_Attributes( field );\
	   }\
	 else\
	   {\
	     res = E_OK;\
	   }\
	 }\
     }\
   RETURN(res);\
}

/* "Template" macro to generate a function to get a fields attribute */
#define GEN_FIELD_ATTR_GET_FCT( name ) \
NCURSES_IMPEXP chtype NCURSES_API field_ ## name (const FIELD * field)\
{\
   T((T_CALLED("field_" #name "(%p)"), (const void *) field));\
   returnAttr( A_ATTRIBUTES & (Normalize_Field( field ) -> name) );\
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_field_fore(FIELD *field, chtype attr)
|
|   Description   :  Sets the foreground of the field used to display the
|                    field contents.
|
|   Return Values :  E_OK             - success
|                    E_BAD_ARGUMENT   - invalid attributes
|                    E_SYSTEM_ERROR   - system error
+--------------------------------------------------------------------------*/
GEN_FIELD_ATTR_SET_FCT(fore)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  chtype field_fore(const FIELD *)
|
|   Description   :  Retrieve fields foreground attribute
|
|   Return Values :  The foreground attribute
+--------------------------------------------------------------------------*/
GEN_FIELD_ATTR_GET_FCT(fore)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_field_back(FIELD *field, chtype attr)
|
|   Description   :  Sets the background of the field used to display the
|                    fields extend.
|
|   Return Values :  E_OK             - success
|                    E_BAD_ARGUMENT   - invalid attributes
|                    E_SYSTEM_ERROR   - system error
+--------------------------------------------------------------------------*/
GEN_FIELD_ATTR_SET_FCT(back)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  chtype field_back(const
|
|   Description   :  Retrieve fields background attribute
|
|   Return Values :  The background attribute
+--------------------------------------------------------------------------*/
GEN_FIELD_ATTR_GET_FCT(back)

/* fld_attr.c ends here */
