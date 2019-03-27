/****************************************************************************
 * Copyright (c) 1998-2011,2012 Free Software Foundation, Inc.              *
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

MODULE_ID("$Id: fld_def.c,v 1.40 2012/03/11 00:37:16 tom Exp $")

/* this can't be readonly */
static FIELD default_field =
{
  0,				/* status  */
  0,				/* rows    */
  0,				/* cols    */
  0,				/* frow    */
  0,				/* fcol    */
  0,				/* drows   */
  0,				/* dcols   */
  0,				/* maxgrow */
  0,				/* nrow    */
  0,				/* nbuf    */
  NO_JUSTIFICATION,		/* just    */
  0,				/* page    */
  0,				/* index   */
  (int)' ',			/* pad     */
  A_NORMAL,			/* fore    */
  A_NORMAL,			/* back    */
  ALL_FIELD_OPTS,		/* opts    */
  (FIELD *)0,			/* snext   */
  (FIELD *)0,			/* sprev   */
  (FIELD *)0,			/* link    */
  (FORM *)0,			/* form    */
  (FIELDTYPE *)0,		/* type    */
  (char *)0,			/* arg     */
  (FIELD_CELL *)0,		/* buf     */
  (char *)0			/* usrptr  */
  NCURSES_FIELD_EXTENSION
};

NCURSES_EXPORT_VAR(FIELD *) _nc_Default_Field = &default_field;

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  TypeArgument *_nc_Make_Argument(
|                              const FIELDTYPE *typ,
|                              va_list *ap,
|                              int *err )
|
|   Description   :  Create an argument structure for the specified type.
|                    Use the type-dependent argument list to construct
|                    it.
|
|   Return Values :  Pointer to argument structure. Maybe NULL.
|                    In case of an error in *err an error counter is increased.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(TypeArgument *)
_nc_Make_Argument(const FIELDTYPE *typ, va_list *ap, int *err)
{
  TypeArgument *res = (TypeArgument *)0;
  TypeArgument *p;

  if (typ != 0 && (typ->status & _HAS_ARGS) != 0)
    {
      assert(err != 0 && ap != (va_list *)0);
      if ((typ->status & _LINKED_TYPE) != 0)
	{
	  p = typeMalloc(TypeArgument, 1);

	  if (p != 0)
	    {
	      p->left = _nc_Make_Argument(typ->left, ap, err);
	      p->right = _nc_Make_Argument(typ->right, ap, err);
	      return p;
	    }
	  else
	    {
	      *err += 1;
	    }
	}
      else
	{
	  assert(typ->makearg != (void *)0);
	  if (!(res = (TypeArgument *)typ->makearg(ap)))
	    {
	      *err += 1;
	    }
	}
    }
  return res;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  TypeArgument *_nc_Copy_Argument(const FIELDTYPE *typ,
|                                                    const TypeArgument *argp,
|                                                    int *err )
|
|   Description   :  Create a copy of an argument structure for the specified
|                    type.
|
|   Return Values :  Pointer to argument structure. Maybe NULL.
|                    In case of an error in *err an error counter is increased.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(TypeArgument *)
_nc_Copy_Argument(const FIELDTYPE *typ, const TypeArgument *argp, int *err)
{
  TypeArgument *res = (TypeArgument *)0;
  TypeArgument *p;

  if (typ != 0 && (typ->status & _HAS_ARGS) != 0)
    {
      assert(err != 0 && argp != 0);
      if ((typ->status & _LINKED_TYPE) != 0)
	{
	  p = typeMalloc(TypeArgument, 1);

	  if (p != 0)
	    {
	      p->left = _nc_Copy_Argument(typ, argp->left, err);
	      p->right = _nc_Copy_Argument(typ, argp->right, err);
	      return p;
	    }
	  *err += 1;
	}
      else
	{
	  if (typ->copyarg != (void *)0)
	    {
	      if (!(res = (TypeArgument *)(typ->copyarg((const void *)argp))))
		{
		  *err += 1;
		}
	    }
	  else
	    {
	      res = (TypeArgument *)argp;
	    }
	}
    }
  return res;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  void _nc_Free_Argument(const FIELDTYPE *typ,
|                                           TypeArgument * argp )
|
|   Description   :  Release memory associated with the argument structure
|                    for the given fieldtype.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_Free_Argument(const FIELDTYPE *typ, TypeArgument *argp)
{
  if (typ != 0 && (typ->status & _HAS_ARGS) != 0)
    {
      if ((typ->status & _LINKED_TYPE) != 0)
	{
	  if (argp != 0)
	    {
	      _nc_Free_Argument(typ->left, argp->left);
	      _nc_Free_Argument(typ->right, argp->right);
	      free(argp);
	    }
	}
      else
	{
	  if (typ->freearg != (void *)0)
	    {
	      typ->freearg((void *)argp);
	    }
	}
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  bool _nc_Copy_Type( FIELD *dst, FIELD const *src )
|
|   Description   :  Copy argument structure of field src to field dst
|
|   Return Values :  TRUE       - copy worked
|                    FALSE      - error occurred
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(bool)
_nc_Copy_Type(FIELD *dst, FIELD const *src)
{
  int err = 0;

  assert(dst != 0 && src != 0);

  dst->type = src->type;
  dst->arg = (void *)_nc_Copy_Argument(src->type, (TypeArgument *)(src->arg), &err);

  if (err != 0)
    {
      _nc_Free_Argument(dst->type, (TypeArgument *)(dst->arg));
      dst->type = (FIELDTYPE *)0;
      dst->arg = (void *)0;
      return FALSE;
    }
  else
    {
      if (dst->type != 0)
	{
	  dst->type->ref++;
	}
      return TRUE;
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  void _nc_Free_Type( FIELD *field )
|
|   Description   :  Release Argument structure for this field
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_Free_Type(FIELD *field)
{
  assert(field != 0);
  if (field->type != 0)
    {
      field->type->ref--;
      _nc_Free_Argument(field->type, (TypeArgument *)(field->arg));
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  FIELD *new_field( int rows, int cols,
|                                      int frow, int fcol,
|                                      int nrow, int nbuf )
|
|   Description   :  Create a new field with this many 'rows' and 'cols',
|                    starting at 'frow/fcol' in the subwindow of the form.
|                    Allocate 'nrow' off-screen rows and 'nbuf' additional
|                    buffers. If an error occurs, errno is set to
|
|                    E_BAD_ARGUMENT - invalid argument
|                    E_SYSTEM_ERROR - system error
|
|   Return Values :  Pointer to the new field or NULL if failure.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(FIELD *)
new_field(int rows, int cols, int frow, int fcol, int nrow, int nbuf)
{
  static const FIELD_CELL blank = BLANK;
  static const FIELD_CELL zeros = ZEROS;

  FIELD *New_Field = (FIELD *)0;
  int err = E_BAD_ARGUMENT;

  T((T_CALLED("new_field(%d,%d,%d,%d,%d,%d)"), rows, cols, frow, fcol, nrow, nbuf));
  if (rows > 0 &&
      cols > 0 &&
      frow >= 0 &&
      fcol >= 0 &&
      nrow >= 0 &&
      nbuf >= 0 &&
      ((err = E_SYSTEM_ERROR) != 0) &&	/* trick: this resets the default error */
      (New_Field = typeMalloc(FIELD, 1)) != 0)
    {
      T((T_CREATE("field %p"), (void *)New_Field));
      *New_Field = default_field;
      New_Field->rows = (short) rows;
      New_Field->cols = (short) cols;
      New_Field->drows = rows + nrow;
      New_Field->dcols = cols;
      New_Field->frow = (short) frow;
      New_Field->fcol = (short) fcol;
      New_Field->nrow = nrow;
      New_Field->nbuf = (short) nbuf;
      New_Field->link = New_Field;

#if USE_WIDEC_SUPPORT
      New_Field->working = newpad(1, Buffer_Length(New_Field) + 1);
      New_Field->expanded = typeCalloc(char *, 1 + (unsigned)nbuf);
#endif

      if (_nc_Copy_Type(New_Field, &default_field))
	{
	  size_t len;

	  len = Total_Buffer_Size(New_Field);
	  if ((New_Field->buf = (FIELD_CELL *)malloc(len)))
	    {
	      /* Prefill buffers with blanks and insert terminating zeroes
	         between buffers */
	      int i, j;
	      int cells = Buffer_Length(New_Field);

	      for (i = 0; i <= New_Field->nbuf; i++)
		{
		  FIELD_CELL *buffer = &(New_Field->buf[(cells + 1) * i]);

		  for (j = 0; j < cells; ++j)
		    {
		      buffer[j] = blank;
		    }
		  buffer[j] = zeros;
		}
	      returnField(New_Field);
	    }
	}
    }

  if (New_Field)
    free_field(New_Field);

  SET_ERROR(err);
  returnField((FIELD *)0);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int free_field( FIELD *field )
|
|   Description   :  Frees the storage allocated for the field.
|
|   Return Values :  E_OK           - success
|                    E_BAD_ARGUMENT - invalid field pointer
|                    E_CONNECTED    - field is connected
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
free_field(FIELD *field)
{
  T((T_CALLED("free_field(%p)"), (void *)field));
  if (!field)
    {
      RETURN(E_BAD_ARGUMENT);
    }
  else if (field->form != 0)
    {
      RETURN(E_CONNECTED);
    }
  else if (field == field->link)
    {
      if (field->buf != 0)
	free(field->buf);
    }
  else
    {
      FIELD *f;

      for (f = field; f->link != field; f = f->link)
	{
	}
      f->link = field->link;
    }
  _nc_Free_Type(field);
#if USE_WIDEC_SUPPORT
  if (field->expanded != 0)
    {
      int n;

      for (n = 0; n <= field->nbuf; ++n)
	{
	  FreeIfNeeded(field->expanded[n]);
	}
      free(field->expanded);
      (void)delwin(field->working);
    }
#endif
  free(field);
  RETURN(E_OK);
}

/* fld_def.c ends here */
