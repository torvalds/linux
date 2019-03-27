/****************************************************************************
 * Copyright (c) 1998-2009,2010 Free Software Foundation, Inc.              *
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

MODULE_ID("$Id: fld_newftyp.c,v 1.19 2010/01/23 21:14:36 tom Exp $")

static FIELDTYPE default_fieldtype =
{
  0,				/* status                                      */
  0L,				/* reference count                             */
  (FIELDTYPE *)0,		/* pointer to left  operand                    */
  (FIELDTYPE *)0,		/* pointer to right operand                    */
  NULL,				/* makearg function                            */
  NULL,				/* copyarg function                            */
  NULL,				/* freearg function                            */
  INIT_FT_FUNC(NULL),		/* field validation function                   */
  INIT_FT_FUNC(NULL),		/* Character check function                    */
  INIT_FT_FUNC(NULL),		/* enumerate next function                     */
  INIT_FT_FUNC(NULL),		/* enumerate previous function                 */
#if NCURSES_INTEROP_FUNCS
  NULL				/* generic callback alternative to makearg     */
#endif
};

NCURSES_EXPORT_VAR(FIELDTYPE *)
_nc_Default_FieldType = &default_fieldtype;

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  FIELDTYPE *new_fieldtype(
|                       bool (* const field_check)(FIELD *,const void *),
|                       bool (* const char_check) (int, const void *) )
|
|   Description   :  Create a new fieldtype. The application programmer must
|                    write a field_check and a char_check function and give
|                    them as input to this call.
|                    If an error occurs, errno is set to
|                       E_BAD_ARGUMENT  - invalid arguments
|                       E_SYSTEM_ERROR  - system error (no memory)
|
|   Return Values :  Fieldtype pointer or NULL if error occurred
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(FIELDTYPE *)
new_fieldtype(bool (*const field_check) (FIELD *, const void *),
	      bool (*const char_check) (int, const void *))
{
  FIELDTYPE *nftyp = (FIELDTYPE *)0;

  T((T_CALLED("new_fieldtype(%p,%p)"), field_check, char_check));
  if ((field_check) || (char_check))
    {
      nftyp = typeMalloc(FIELDTYPE, 1);

      if (nftyp)
	{
	  T((T_CREATE("fieldtype %p"), (void *)nftyp));
	  *nftyp = default_fieldtype;
#if NCURSES_INTEROP_FUNCS
	  nftyp->fieldcheck.ofcheck = field_check;
	  nftyp->charcheck.occheck = char_check;
#else
	  nftyp->fcheck = field_check;
	  nftyp->ccheck = char_check;
#endif
	}
      else
	{
	  SET_ERROR(E_SYSTEM_ERROR);
	}
    }
  else
    {
      SET_ERROR(E_BAD_ARGUMENT);
    }
  returnFieldType(nftyp);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int free_fieldtype(FIELDTYPE *typ)
|
|   Description   :  Release the memory associated with this fieldtype.
|
|   Return Values :  E_OK            - success
|                    E_CONNECTED     - there are fields referencing the type
|                    E_BAD_ARGUMENT  - invalid fieldtype pointer
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
free_fieldtype(FIELDTYPE *typ)
{
  T((T_CALLED("free_fieldtype(%p)"), (void *)typ));

  if (!typ)
    RETURN(E_BAD_ARGUMENT);

  if (typ->ref != 0)
    RETURN(E_CONNECTED);

  if (typ->status & _RESIDENT)
    RETURN(E_CONNECTED);

  if (typ->status & _LINKED_TYPE)
    {
      if (typ->left)
	typ->left->ref--;
      if (typ->right)
	typ->right->ref--;
    }
  free(typ);
  RETURN(E_OK);
}

/* fld_newftyp.c ends here */
