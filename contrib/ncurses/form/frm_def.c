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

MODULE_ID("$Id: frm_def.c,v 1.26 2012/03/11 00:37:16 tom Exp $")

/* this can't be readonly */
static FORM default_form =
{
  0,				/* status     */
  0,				/* rows       */
  0,				/* cols       */
  0,				/* currow     */
  0,				/* curcol     */
  0,				/* toprow     */
  0,				/* begincol   */
  -1,				/* maxfield   */
  -1,				/* maxpage    */
  -1,				/* curpage    */
  ALL_FORM_OPTS,		/* opts       */
  (WINDOW *)0,			/* win        */
  (WINDOW *)0,			/* sub        */
  (WINDOW *)0,			/* w          */
  (FIELD **)0,			/* field      */
  (FIELD *)0,			/* current    */
  (_PAGE *) 0,			/* page       */
  (char *)0,			/* usrptr     */
  NULL,				/* forminit   */
  NULL,				/* formterm   */
  NULL,				/* fieldinit  */
  NULL				/* fieldterm  */
};

NCURSES_EXPORT_VAR(FORM *) _nc_Default_Form = &default_form;

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static FIELD *Insert_Field_By_Position(
|                                     FIELD *new_field, 
|                                     FIELD *head )
|   
|   Description   :  Insert new_field into sorted fieldlist with head "head"
|                    and return new head of sorted fieldlist. Sorting
|                    criteria is (row,column). This is a circular list.
|
|   Return Values :  New head of sorted fieldlist
+--------------------------------------------------------------------------*/
static FIELD *
Insert_Field_By_Position(FIELD *newfield, FIELD *head)
{
  FIELD *current, *newhead;

  assert(newfield);

  if (!head)
    {				/* empty list is trivial */
      newhead = newfield->snext = newfield->sprev = newfield;
    }
  else
    {
      newhead = current = head;
      while ((current->frow < newfield->frow) ||
	     ((current->frow == newfield->frow) &&
	      (current->fcol < newfield->fcol)))
	{
	  current = current->snext;
	  if (current == head)
	    {			/* We cycled through. Reset head to indicate that */
	      head = (FIELD *)0;
	      break;
	    }
	}
      /* we leave the loop with current pointing to the field after newfield */
      newfield->snext = current;
      newfield->sprev = current->sprev;
      newfield->snext->sprev = newfield;
      newfield->sprev->snext = newfield;
      if (current == head)
	newhead = newfield;
    }
  return (newhead);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static void Disconnect_Fields(FORM *form)
|   
|   Description   :  Break association between form and array of fields.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
static void
Disconnect_Fields(FORM *form)
{
  if (form->field)
    {
      FIELD **fields;

      for (fields = form->field; *fields; fields++)
	{
	  if (form == (*fields)->form)
	    (*fields)->form = (FORM *)0;
	}

      form->rows = form->cols = 0;
      form->maxfield = form->maxpage = -1;
      form->field = (FIELD **)0;
      if (form->page)
	free(form->page);
      form->page = (_PAGE *) 0;
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static int Connect_Fields(FORM *form, FIELD **fields)
|   
|   Description   :  Set association between form and array of fields.
|
|   Return Values :  E_OK            - no error
|                    E_CONNECTED     - a field is already connected
|                    E_BAD_ARGUMENT  - Invalid form pointer or field array
|                    E_SYSTEM_ERROR  - not enough memory
+--------------------------------------------------------------------------*/
static int
Connect_Fields(FORM *form, FIELD **fields)
{
  int field_cnt, j;
  int page_nr;
  int maximum_row_in_field, maximum_col_in_field;
  _PAGE *pg;

  T((T_CALLED("Connect_Fields(%p,%p)"), (void *)form, (void *)fields));

  assert(form);

  form->field = fields;
  form->maxfield = 0;
  form->maxpage = 0;

  if (!fields)
    RETURN(E_OK);

  page_nr = 0;
  /* store formpointer in fields and count pages */
  for (field_cnt = 0; fields[field_cnt]; field_cnt++)
    {
      if (fields[field_cnt]->form)
	RETURN(E_CONNECTED);
      if (field_cnt == 0 ||
	  (fields[field_cnt]->status & _NEWPAGE))
	page_nr++;
      fields[field_cnt]->form = form;
    }
  if (field_cnt == 0 || (short)field_cnt < 0)
    RETURN(E_BAD_ARGUMENT);

  /* allocate page structures */
  if ((pg = typeMalloc(_PAGE, page_nr)) != (_PAGE *) 0)
    {
      T((T_CREATE("_PAGE %p"), (void *)pg));
      form->page = pg;
    }
  else
    RETURN(E_SYSTEM_ERROR);

  /* Cycle through fields and calculate page boundaries as well as
     size of the form */
  for (j = 0; j < field_cnt; j++)
    {
      if (j == 0)
	pg->pmin = (short) j;
      else
	{
	  if (fields[j]->status & _NEWPAGE)
	    {
	      pg->pmax = (short) (j - 1);
	      pg++;
	      pg->pmin = (short) j;
	    }
	}

      maximum_row_in_field = fields[j]->frow + fields[j]->rows;
      maximum_col_in_field = fields[j]->fcol + fields[j]->cols;

      if (form->rows < maximum_row_in_field)
	form->rows = (short) maximum_row_in_field;
      if (form->cols < maximum_col_in_field)
	form->cols = (short) maximum_col_in_field;
    }

  pg->pmax = (short) (field_cnt - 1);
  form->maxfield = (short) field_cnt;
  form->maxpage = (short) page_nr;

  /* Sort fields on form pages */
  for (page_nr = 0; page_nr < form->maxpage; page_nr++)
    {
      FIELD *fld = (FIELD *)0;

      for (j = form->page[page_nr].pmin; j <= form->page[page_nr].pmax; j++)
	{
	  fields[j]->index = (short) j;
	  fields[j]->page = (short) page_nr;
	  fld = Insert_Field_By_Position(fields[j], fld);
	}
      if (fld)
	{
	  form->page[page_nr].smin = fld->index;
	  form->page[page_nr].smax = fld->sprev->index;
	}
      else
	{
	  form->page[page_nr].smin = 0;
	  form->page[page_nr].smax = 0;
	}
    }
  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static int Associate_Fields(FORM *form, FIELD **fields)
|   
|   Description   :  Set association between form and array of fields. 
|                    If there are fields, position to first active field.
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - Invalid form pointer or field array
|                    E_CONNECTED     - a field is already connected
|                    E_SYSTEM_ERROR  - not enough memory
+--------------------------------------------------------------------------*/
NCURSES_INLINE static int
Associate_Fields(FORM *form, FIELD **fields)
{
  int res = Connect_Fields(form, fields);

  if (res == E_OK)
    {
      if (form->maxpage > 0)
	{
	  form->curpage = 0;
	  form_driver(form, FIRST_ACTIVE_MAGIC);
	}
      else
	{
	  form->curpage = -1;
	  form->current = (FIELD *)0;
	}
    }
  return (res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  FORM *new_form_sp(SCREEN* sp, FIELD** fields )
|   
|   Description   :  Create new form with given array of fields.
|
|   Return Values :  Pointer to form. NULL if error occurred.
!                    Set errno:
|                    E_OK            - success
|                    E_BAD_ARGUMENT  - Invalid form pointer or field array
|                    E_CONNECTED     - a field is already connected
|                    E_SYSTEM_ERROR  - not enough memory
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(FORM *)
NCURSES_SP_NAME(new_form) (NCURSES_SP_DCLx FIELD **fields)
{
  int err = E_SYSTEM_ERROR;
  FORM *form = (FORM *)0;

  T((T_CALLED("new_form(%p,%p)"), (void *)SP_PARM, (void *)fields));

  if (IsValidScreen(SP_PARM))
    {
      form = typeMalloc(FORM, 1);

      if (form)
	{
	  T((T_CREATE("form %p"), (void *)form));
	  *form = *_nc_Default_Form;
	  /* This ensures win and sub are always non-null,
	     so we can derive always the SCREEN that this form is
	     running on. */
	  form->win = StdScreen(SP_PARM);
	  form->sub = StdScreen(SP_PARM);
	  if ((err = Associate_Fields(form, fields)) != E_OK)
	    {
	      free_form(form);
	      form = (FORM *)0;
	    }
	}
    }

  if (!form)
    SET_ERROR(err);

  returnForm(form);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  FORM* new_form(FIELD** fields )
|   
|   Description   :  Create new form with given array of fields.
|
|   Return Values :  Pointer to form. NULL if error occurred.
!                    Set errno:
|                    E_OK            - success
|                    E_BAD_ARGUMENT  - Invalid form pointer or field array
|                    E_CONNECTED     - a field is already connected
|                    E_SYSTEM_ERROR  - not enough memory
+--------------------------------------------------------------------------*/
#if NCURSES_SP_FUNCS
NCURSES_EXPORT(FORM *)
new_form(FIELD **fields)
{
  return NCURSES_SP_NAME(new_form) (CURRENT_SCREEN, fields);
}
#endif

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int free_form( FORM *form )
|   
|   Description   :  Release internal memory associated with form.
|
|   Return Values :  E_OK           - no error
|                    E_BAD_ARGUMENT - invalid form pointer
|                    E_POSTED       - form is posted
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
free_form(FORM *form)
{
  T((T_CALLED("free_form(%p)"), (void *)form));

  if (!form)
    RETURN(E_BAD_ARGUMENT);

  if (form->status & _POSTED)
    RETURN(E_POSTED);

  Disconnect_Fields(form);
  if (form->page)
    free(form->page);
  free(form);

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int set_form_fields( FORM *form, FIELD **fields )
|   
|   Description   :  Set a new association of an array of fields to a form
|
|   Return Values :  E_OK            - no error
|                    E_BAD_ARGUMENT  - Invalid form pointer or field array
|                    E_CONNECTED     - a field is already connected
|                    E_POSTED        - form is posted
|                    E_SYSTEM_ERROR  - not enough memory
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_form_fields(FORM *form, FIELD **fields)
{
  FIELD **old;
  int res;

  T((T_CALLED("set_form_fields(%p,%p)"), (void *)form, (void *)fields));

  if (!form)
    RETURN(E_BAD_ARGUMENT);

  if (form->status & _POSTED)
    RETURN(E_POSTED);

  old = form->field;
  Disconnect_Fields(form);

  if ((res = Associate_Fields(form, fields)) != E_OK)
    Connect_Fields(form, old);

  RETURN(res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  FIELD **form_fields( const FORM *form )
|   
|   Description   :  Retrieve array of fields
|
|   Return Values :  Pointer to field array
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(FIELD **)
form_fields(const FORM *form)
{
  T((T_CALLED("form_field(%p)"), (const void *)form));
  returnFieldPtr(Normalize_Form(form)->field);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int field_count( const FORM *form )
|   
|   Description   :  Retrieve number of fields
|
|   Return Values :  Number of fields, -1 if none are defined
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
field_count(const FORM *form)
{
  T((T_CALLED("field_count(%p)"), (const void *)form));

  returnCode(Normalize_Form(form)->maxfield);
}

/* frm_def.c ends here */
