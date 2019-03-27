/****************************************************************************
 * Copyright (c) 2008-2010,2012 Free Software Foundation, Inc.              *
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

/***************************************************************************
*                                                                          *
*  Author : Juergen Pfeifer                                                *
*                                                                          *
***************************************************************************/

#include "form.priv.h"

MODULE_ID("$Id: fty_generic.c,v 1.6 2012/06/10 00:27:49 tom Exp $")

/*
 * This is not a full implementation of a field type, but adds some
 * support for higher level languages with some restrictions to interop
 * with C language. Especially the collection of arguments for the
 * various fieldtypes is not based on the vararg C mechanism, but on a
 * iterator based callback mechanism that allowes the high level language
 * to provide the arguments as a structure. Most languages have mechanisms
 * to layout structures so that they can be passed to C.
 * The languages can register a new generic fieldtype dynamically and store
 * a handle (key) to the calling object as an argument. Together with that
 * it can register a freearg callback, so that the high level language
 * remains in control of the memory management of the arguments they pass.
 * The design idea is, that the high-level language - typically a OO
 * language like C# or Java, uses it's own dispatching mechanisms
 * (polymorphism) to call the proper check routines responsible for the
 * argument type. So these language implement typically only one generic
 * fieldtype they register with the forms library using this call.
 *
 * For that purpose we have extended the fieldtype struc by a new element
 * that gets the arguments from a single struct passed by the caller. 
 * 
 */
#if NCURSES_INTEROP_FUNCS

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static void *Generic_This_Type( void * arg )
|   
|   Description   :  We interpret the passed arg just as a handle the
|                    calling language uses to keep track of its allocated
|                    argument structures. We can simply copy it back.
|
|   Return Values :  Pointer to argument structure
+--------------------------------------------------------------------------*/
static void *
Generic_This_Type(void *arg)
{
  return (arg);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  FIELDTYPE *_nc_generic_fieldtype(
|                       bool (* const field_check)(FIELD *,const void *),
|                       bool (* const char_check) (int, const void *),
|   		        bool (*const next)(FORM*,FIELD*,const void*),
|		        bool (*const prev)(FORM*,FIELD*,const void*),
|                       void (*freecallback)(void*))
|
|   Description   :  Create a new fieldtype. The application programmer must
|                    write a field_check and a char_check function and give
|                    them as input to this call. A callback to allow the
|                    release of the allocated memory must also be provided.
|                    For generic field types, we provide some more 
|                    information about the field as parameters.
|
|                    If an error occurs, errno is set to
|                       E_BAD_ARGUMENT  - invalid arguments
|                       E_SYSTEM_ERROR  - system error (no memory)
|
|   Return Values :  Fieldtype pointer or NULL if error occurred
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(FIELDTYPE *)
_nc_generic_fieldtype(bool (*const field_check) (FORM *, FIELD *, const void *),
		      bool (*const char_check) (int, FORM *, FIELD *, const
						void *),
		      bool (*const next) (FORM *, FIELD *, const void *),
		      bool (*const prev) (FORM *, FIELD *, const void *),
		      void (*freecallback) (void *))
{
  int code = E_SYSTEM_ERROR;
  FIELDTYPE *res = (FIELDTYPE *)0;

  T((T_CALLED("_nc_generic_fieldtype(%p,%p,%p,%p,%p)"),
     field_check, char_check, next, prev, freecallback));

  if (field_check || char_check)
    {
      res = typeMalloc(FIELDTYPE, 1);

      if (res)
	{
	  *res = *_nc_Default_FieldType;
	  SetStatus(res, (_HAS_ARGS | _GENERIC));
	  res->fieldcheck.gfcheck = field_check;
	  res->charcheck.gccheck = char_check;
	  res->genericarg = Generic_This_Type;
	  res->freearg = freecallback;
	  res->enum_next.gnext = next;
	  res->enum_prev.gprev = prev;
	  code = E_OK;
	}
    }
  else
    code = E_BAD_ARGUMENT;

  if (E_OK != code)
    SET_ERROR(code);

  returnFieldType(res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static TypeArgument *GenericArgument(
|                      const FIELDTYPE* typ,
|                      int (*argiterator)(void**),
|                      int* err)
|   
|   Description   :  The iterator callback must browse through all fieldtype
|                    parameters that have an argument associated with the
|                    type. The iterator returns 1 if the operation to get
|                    the next element was successfull, 0 otherwise. If the
|                    iterator could move to the next argument, it fills
|                    the void* pointer representing the argument into the
|                    location provided as argument to the iterator.
|                    The err reference is used to keep track of errors.
|
|   Return Values :  Pointer to argument structure
+--------------------------------------------------------------------------*/
static TypeArgument *
GenericArgument(const FIELDTYPE *typ,
		int (*argiterator) (void **), int *err)
{
  TypeArgument *res = (TypeArgument *)0;

  if (typ != 0 && (typ->status & _HAS_ARGS) != 0 && err != 0 && argiterator != 0)
    {
      if (typ->status & _LINKED_TYPE)
	{
	  /* Composite fieldtypes keep track internally of their own memory */
	  TypeArgument *p = typeMalloc(TypeArgument, 1);

	  if (p)
	    {
	      p->left = GenericArgument(typ->left, argiterator, err);
	      p->right = GenericArgument(typ->right, argiterator, err);
	      return p;
	    }
	  else
	    *err += 1;
	}
      else
	{
	  assert(typ->genericarg != (void *)0);
	  if (typ->genericarg == 0)
	    *err += 1;
	  else
	    {
	      void *argp;
	      int valid = argiterator(&argp);

	      if (valid == 0 || argp == 0 ||
		  !(res = (TypeArgument *)typ->genericarg(argp)))
		{
		  *err += 1;
		}
	    }
	}
    }
  return res;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int _nc_set_generic_fieldtype(
|                      FIELD* field,
|                      FIELDTYPE* ftyp,
|                      int (*argiterator)(void**))
|   
|   Description   :  Assign the fieldtype to the field and use the iterator
|                    mechanism to get the arguments when a check is 
|                    performed.
|
|   Return Values :  E_OK if all went well
|                    E_SYSTEM_ERROR if an error occurred
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_set_generic_fieldtype(FIELD *field,
			  FIELDTYPE *ftyp,
			  int (*argiterator) (void **))
{
  int code = E_SYSTEM_ERROR;
  int err = 0;

  if (field)
    {
      if (field && field->type)
	_nc_Free_Type(field);

      field->type = ftyp;
      if (ftyp)
	{
	  if (argiterator)
	    {
	      /* The precondition is that the iterator is reset */
	      field->arg = (void *)GenericArgument(field->type, argiterator, &err);

	      if (err)
		{
		  _nc_Free_Argument(field->type, (TypeArgument *)(field->arg));
		  field->type = (FIELDTYPE *)0;
		  field->arg = (void *)0;
		}
	      else
		{
		  code = E_OK;
		  if (field->type)
		    field->type->ref++;
		}
	    }
	}
      else
	{
	  field->arg = (void *)0;
	  code = E_OK;
	}
    }
  return code;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  WINDOW* _nc_form_cursor(
|                      FORM* form,
|                      int *pRow, int *pCol)
|   
|   Description   :  Get the current position of the form cursor position
|                    We also return the field window
|
|   Return Values :  The fields Window or NULL on error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(WINDOW *)
_nc_form_cursor(const FORM *form, int *pRow, int *pCol)
{
  int code = E_SYSTEM_ERROR;
  WINDOW *res = (WINDOW *)0;

  if (!(form == 0 || pRow == 0 || pCol == 0))
    {
      *pRow = form->currow;
      *pCol = form->curcol;
      res = form->w;
      code = E_OK;
    }
  if (code != E_OK)
    SET_ERROR(code);
  return res;
}

#else
extern void _nc_fty_generic(void);
void
_nc_fty_generic(void)
{
}
#endif

/* fty_generic.c ends here */
