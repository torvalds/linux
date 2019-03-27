/****************************************************************************
 * Copyright (c) 1998-2013,2014 Free Software Foundation, Inc.              *
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

MODULE_ID("$Id: frm_driver.c,v 1.110 2014/02/10 00:42:48 tom Exp $")

/*----------------------------------------------------------------------------
  This is the core module of the form library. It contains the majority
  of the driver routines as well as the form_driver function.

  Essentially this module is nearly the whole library. This is because
  all the functions in this module depends on some others in the module,
  so it makes no sense to split them into separate files because they
  will always be linked together. The only acceptable concern is turnaround
  time for this module, but now we have all Pentiums or RISCs, so what!

  The driver routines are grouped into nine generic categories:

   a)   Page Navigation            ( all functions prefixed by PN_ )
        The current page of the form is left and some new page is
        entered.
   b)   Inter-Field Navigation     ( all functions prefixed by FN_ )
        The current field of the form is left and some new field is
        entered.
   c)   Intra-Field Navigation     ( all functions prefixed by IFN_ )
        The current position in the current field is changed.
   d)   Vertical Scrolling         ( all functions prefixed by VSC_ )
        Essentially this is a specialization of Intra-Field navigation.
        It has to check for a multi-line field.
   e)   Horizontal Scrolling       ( all functions prefixed by HSC_ )
        Essentially this is a specialization of Intra-Field navigation.
        It has to check for a single-line field.
   f)   Field Editing              ( all functions prefixed by FE_ )
        The content of the current field is changed
   g)   Edit Mode requests         ( all functions prefixed by EM_ )
        Switching between insert and overlay mode
   h)   Field-Validation requests  ( all functions prefixed by FV_ )
        Perform verifications of the field.
   i)   Choice requests            ( all functions prefixed by CR_ )
        Requests to enumerate possible field values
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Some remarks on the placements of assert() macros :
  I use them only on "strategic" places, i.e. top level entries where
  I want to make sure that things are set correctly. Throughout subordinate
  routines I omit them mostly.
  --------------------------------------------------------------------------*/

/*
Some options that may effect compatibility in behavior to SVr4 forms,
but they are here to allow a more intuitive and user friendly behavior of
our form implementation. This doesn't affect the API, so we feel it is
uncritical.

The initial implementation tries to stay very close with the behavior
of the original SVr4 implementation, although in some areas it is quite
clear that this isn't the most appropriate way. As far as possible this
sources will allow you to build a forms lib that behaves quite similar
to SVr4, but now and in the future we will give you better options.
Perhaps at some time we will make this configurable at runtime.
*/

/* Implement a more user-friendly previous/next word behavior */
#define FRIENDLY_PREV_NEXT_WORD (1)
/* Fix the wrong behavior for forms with all fields inactive */
#define FIX_FORM_INACTIVE_BUG (1)
/* Allow dynamic field growth also when navigating past the end */
#define GROW_IF_NAVIGATE (1)

#if USE_WIDEC_SUPPORT
#define myADDNSTR(w, s, n) wadd_wchnstr(w, s, n)
#define myINSNSTR(w, s, n) wins_wchnstr(w, s, n)
#define myINNSTR(w, s, n)  fix_wchnstr(w, s, n)
#define myWCWIDTH(w, y, x) cell_width(w, y, x)
#else
#define myADDNSTR(w, s, n) waddnstr(w, s, n)
#define myINSNSTR(w, s, n) winsnstr(w, s, n)
#define myINNSTR(w, s, n)  winnstr(w, s, n)
#define myWCWIDTH(w, y, x) 1
#endif

/*----------------------------------------------------------------------------
  Forward references to some internally used static functions
  --------------------------------------------------------------------------*/
static int Inter_Field_Navigation(int (*const fct) (FORM *), FORM *form);
static int FN_Next_Field(FORM *form);
static int FN_Previous_Field(FORM *form);
static int FE_New_Line(FORM *);
static int FE_Delete_Previous(FORM *);

/*----------------------------------------------------------------------------
  Macro Definitions.

  Some Remarks on that: I use the convention to use UPPERCASE for constants
  defined by Macros. If I provide a macro as a kind of inline routine to
  provide some logic, I use my Upper_Lower case style.
  --------------------------------------------------------------------------*/

/* Calculate the position of a single row in a field buffer */
#define Position_Of_Row_In_Buffer(field,row) ((row)*(field)->dcols)

/* Calculate start address for the fields buffer# N */
#define Address_Of_Nth_Buffer(field,N) \
  ((field)->buf + (N)*(1+Buffer_Length(field)))

/* Calculate the start address of the row in the fields specified buffer# N */
#define Address_Of_Row_In_Nth_Buffer(field,N,row) \
  (Address_Of_Nth_Buffer(field,N) + Position_Of_Row_In_Buffer(field,row))

/* Calculate the start address of the row in the fields primary buffer */
#define Address_Of_Row_In_Buffer(field,row) \
  Address_Of_Row_In_Nth_Buffer(field,0,row)

/* Calculate the start address of the row in the forms current field
   buffer# N */
#define Address_Of_Current_Row_In_Nth_Buffer(form,N) \
   Address_Of_Row_In_Nth_Buffer((form)->current,N,(form)->currow)

/* Calculate the start address of the row in the forms current field
   primary buffer */
#define Address_Of_Current_Row_In_Buffer(form) \
   Address_Of_Current_Row_In_Nth_Buffer(form,0)

/* Calculate the address of the cursor in the forms current field
   primary buffer */
#define Address_Of_Current_Position_In_Nth_Buffer(form,N) \
   (Address_Of_Current_Row_In_Nth_Buffer(form,N) + (form)->curcol)

/* Calculate the address of the cursor in the forms current field
   buffer# N */
#define Address_Of_Current_Position_In_Buffer(form) \
  Address_Of_Current_Position_In_Nth_Buffer(form,0)

/* Logic to decide whether or not a field is actually a field with
   vertical or horizontal scrolling */
#define Is_Scroll_Field(field)          \
   (((field)->drows > (field)->rows) || \
    ((field)->dcols > (field)->cols))

/* Logic to decide whether or not a field needs to have an individual window
   instead of a derived window because it contains invisible parts.
   This is true for non-public fields and for scrollable fields. */
#define Has_Invisible_Parts(field)     \
  (!((unsigned)(field)->opts & O_PUBLIC) || \
   Is_Scroll_Field(field))

/* Logic to decide whether or not a field needs justification */
#define Justification_Allowed(field)        \
   (((field)->just != NO_JUSTIFICATION)  && \
    (Single_Line_Field(field))           && \
    (((field)->dcols == (field)->cols)   && \
    ((unsigned)(field)->opts & O_STATIC)))

/* Logic to determine whether or not a dynamic field may still grow */
#define Growable(field) ((field)->status & _MAY_GROW)

/* Macro to set the attributes for a fields window */
#define Set_Field_Window_Attributes(field,win) \
(  wbkgdset((win),(chtype)((chtype)((field)->pad) | (field)->back)), \
   (void) wattrset((win), (int)(field)->fore) )

/* Logic to decide whether or not a field really appears on the form */
#define Field_Really_Appears(field)         \
  ((field->form)                          &&\
   (field->form->status & _POSTED)        &&\
   ((unsigned)field->opts & O_VISIBLE)    &&\
   (field->page == field->form->curpage))

/* Logic to determine whether or not we are on the first position in the
   current field */
#define First_Position_In_Current_Field(form) \
  (((form)->currow==0) && ((form)->curcol==0))

#define Minimum(a,b) (((a)<=(b)) ? (a) : (b))
#define Maximum(a,b) (((a)>=(b)) ? (a) : (b))

/*----------------------------------------------------------------------------
  Useful constants
  --------------------------------------------------------------------------*/
static FIELD_CELL myBLANK = BLANK;
static FIELD_CELL myZEROS;

#ifdef TRACE
static void
check_pos(FORM *form, int lineno)
{
  int y, x;

  if (form && form->w)
    {
      getyx(form->w, y, x);
      if (y != form->currow || x != form->curcol)
	{
	  T(("CHECKPOS %s@%d have position %d,%d vs want %d,%d",
	     __FILE__, lineno,
	     y, x,
	     form->currow, form->curcol));
	}
    }
}
#define CHECKPOS(form) check_pos(form, __LINE__)
#else
#define CHECKPOS(form)		/* nothing */
#endif

/*----------------------------------------------------------------------------
  Wide-character special functions
  --------------------------------------------------------------------------*/
#if USE_WIDEC_SUPPORT
/* like winsnstr */
static int
wins_wchnstr(WINDOW *w, cchar_t *s, int n)
{
  int code = ERR;
  int y, x;

  while (n-- > 0)
    {
      getyx(w, y, x);
      if ((code = wins_wch(w, s++)) != OK)
	break;
      if ((code = wmove(w, y, x + 1)) != OK)
	break;
    }
  return code;
}

/* win_wchnstr is inconsistent with winnstr, since it returns OK rather than
 * the number of items transferred.
 */
static int
fix_wchnstr(WINDOW *w, cchar_t *s, int n)
{
  int x;

  win_wchnstr(w, s, n);
  /*
   * This function is used to extract the text only from the window.
   * Strip attributes and color from the string so they will not be added
   * back when copying the string to the window.
   */
  for (x = 0; x < n; ++x)
    {
      RemAttr(s[x], A_ATTRIBUTES);
      SetPair(s[x], 0);
    }
  return n;
}

/*
 * Returns the column of the base of the given cell.
 */
static int
cell_base(WINDOW *win, int y, int x)
{
  int result = x;

  while (LEGALYX(win, y, x))
    {
      cchar_t *data = &(win->_line[y].text[x]);

      if (isWidecBase(CHDEREF(data)) || !isWidecExt(CHDEREF(data)))
	{
	  result = x;
	  break;
	}
      --x;
    }
  return result;
}

/*
 * Returns the number of columns needed for the given cell in a window.
 */
static int
cell_width(WINDOW *win, int y, int x)
{
  int result = 1;

  if (LEGALYX(win, y, x))
    {
      cchar_t *data = &(win->_line[y].text[x]);

      if (isWidecExt(CHDEREF(data)))
	{
	  /* recur, providing the number of columns to the next character */
	  result = cell_width(win, y, x - 1);
	}
      else
	{
	  result = wcwidth(CharOf(CHDEREF(data)));
	}
    }
  return result;
}

/*
 * There is no wide-character function such as wdel_wch(), so we must find
 * all of the cells that comprise a multi-column character and delete them
 * one-by-one.
 */
static void
delete_char(FORM *form)
{
  int cells = cell_width(form->w, form->currow, form->curcol);

  form->curcol = cell_base(form->w, form->currow, form->curcol);
  wmove(form->w, form->currow, form->curcol);
  while (cells-- > 0)
    {
      wdelch(form->w);
    }
}
#define DeleteChar(form) delete_char(form)
#else
#define DeleteChar(form) \
	  wmove((form)->w, (form)->currow, (form)->curcol), \
	  wdelch((form)->w)
#endif

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static char *Get_Start_Of_Data(char * buf, int blen)
|
|   Description   :  Return pointer to first non-blank position in buffer.
|                    If buffer is empty return pointer to buffer itself.
|
|   Return Values :  Pointer to first non-blank position in buffer
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD_CELL *
Get_Start_Of_Data(FIELD_CELL *buf, int blen)
{
  FIELD_CELL *p = buf;
  FIELD_CELL *end = &buf[blen];

  assert(buf && blen >= 0);
  while ((p < end) && ISBLANK(*p))
    p++;
  return ((p == end) ? buf : p);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static char *After_End_Of_Data(char * buf, int blen)
|
|   Description   :  Return pointer after last non-blank position in buffer.
|                    If buffer is empty, return pointer to buffer itself.
|
|   Return Values :  Pointer to position after last non-blank position in
|                    buffer.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD_CELL *
After_End_Of_Data(FIELD_CELL *buf, int blen)
{
  FIELD_CELL *p = &buf[blen];

  assert(buf && blen >= 0);
  while ((p > buf) && ISBLANK(p[-1]))
    p--;
  return (p);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static char *Get_First_Whitespace_Character(
|                                     char * buf, int   blen)
|
|   Description   :  Position to the first whitespace character.
|
|   Return Values :  Pointer to first whitespace character in buffer.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD_CELL *
Get_First_Whitespace_Character(FIELD_CELL *buf, int blen)
{
  FIELD_CELL *p = buf;
  FIELD_CELL *end = &p[blen];

  assert(buf && blen >= 0);
  while ((p < end) && !ISBLANK(*p))
    p++;
  return ((p == end) ? buf : p);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static char *After_Last_Whitespace_Character(
|                                     char * buf, int blen)
|
|   Description   :  Get the position after the last whitespace character.
|
|   Return Values :  Pointer to position after last whitespace character in
|                    buffer.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD_CELL *
After_Last_Whitespace_Character(FIELD_CELL *buf, int blen)
{
  FIELD_CELL *p = &buf[blen];

  assert(buf && blen >= 0);
  while ((p > buf) && !ISBLANK(p[-1]))
    p--;
  return (p);
}

/* Set this to 1 to use the div_t version. This is a good idea if your
   compiler has an intrinsic div() support. Unfortunately GNU-C has it
   not yet.
   N.B.: This only works if form->curcol follows immediately form->currow
         and both are of type int.
*/
#define USE_DIV_T (0)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void Adjust_Cursor_Position(
|                                       FORM * form, const char * pos)
|
|   Description   :  Set current row and column of the form to values
|                    corresponding to the buffer position.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_INLINE static void
Adjust_Cursor_Position(FORM *form, const FIELD_CELL *pos)
{
  FIELD *field;
  int idx;

  field = form->current;
  assert(pos >= field->buf && field->dcols > 0);
  idx = (int)(pos - field->buf);
#if USE_DIV_T
  *((div_t *) & (form->currow)) = div(idx, field->dcols);
#else
  form->currow = idx / field->dcols;
  form->curcol = idx - field->cols * form->currow;
#endif
  if (field->drows < form->currow)
    form->currow = 0;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void Buffer_To_Window(
|                                      const FIELD  * field,
|                                      WINDOW * win)
|
|   Description   :  Copy the buffer to the window. If it is a multi-line
|                    field, the buffer is split to the lines of the
|                    window without any editing.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
static void
Buffer_To_Window(const FIELD *field, WINDOW *win)
{
  int width, height;
  int y, x;
  int len;
  int row;
  FIELD_CELL *pBuffer;

  assert(win && field);

  getyx(win, y, x);
  width = getmaxx(win);
  height = getmaxy(win);

  for (row = 0, pBuffer = field->buf;
       row < height;
       row++, pBuffer += width)
    {
      if ((len = (int)(After_End_Of_Data(pBuffer, width) - pBuffer)) > 0)
	{
	  wmove(win, row, 0);
	  myADDNSTR(win, pBuffer, len);
	}
    }
  wmove(win, y, x);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  void _nc_get_fieldbuffer(
|                                          WINDOW * win,
|                                          FIELD  * field,
|                                          FIELD_CELL * buf)
|
|   Description   :  Copy the content of the window into the buffer.
|                    The multiple lines of a window are simply
|                    concatenated into the buffer. Pad characters in
|                    the window will be replaced by blanks in the buffer.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_get_fieldbuffer(FORM *form, FIELD *field, FIELD_CELL *buf)
{
  int pad;
  int len = 0;
  FIELD_CELL *p;
  int row, height;
  WINDOW *win;

  assert(form && field && buf);

  win = form->w;
  assert(win);

  pad = field->pad;
  p = buf;
  height = getmaxy(win);

  for (row = 0; (row < height) && (row < field->drows); row++)
    {
      wmove(win, row, 0);
      len += myINNSTR(win, p + len, field->dcols);
    }
  p[len] = myZEROS;

  /* replace visual padding character by blanks in buffer */
  if (pad != C_BLANK)
    {
      int i;

      for (i = 0; i < len; i++, p++)
	{
	  if ((unsigned long)CharOf(*p) == ChCharOf(pad)
#if USE_WIDEC_SUPPORT
	      && p->chars[1] == 0
#endif
	    )
	    *p = myBLANK;
	}
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void Window_To_Buffer(
|                                          FORM   * form,
|                                          FIELD  * field)
|
|   Description   :  Copy the content of the window into the buffer.
|                    The multiple lines of a window are simply
|                    concatenated into the buffer. Pad characters in
|                    the window will be replaced by blanks in the buffer.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
static void
Window_To_Buffer(FORM *form, FIELD *field)
{
  _nc_get_fieldbuffer(form, field, field->buf);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void Synchronize_Buffer(FORM * form)
|
|   Description   :  If there was a change, copy the content of the
|                    window into the buffer, so the buffer is synchronized
|                    with the windows content. We have to indicate that the
|                    buffer needs validation due to the change.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_INLINE static void
Synchronize_Buffer(FORM *form)
{
  if (form->status & _WINDOW_MODIFIED)
    {
      ClrStatus(form, _WINDOW_MODIFIED);
      SetStatus(form, _FCHECK_REQUIRED);
      Window_To_Buffer(form, form->current);
      wmove(form->w, form->currow, form->curcol);
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static bool Field_Grown( FIELD *field, int amount)
|
|   Description   :  This function is called for growable dynamic fields
|                    only. It has to increase the buffers and to allocate
|                    a new window for this field.
|                    This function has the side effect to set a new
|                    field-buffer pointer, the dcols and drows values
|                    as well as a new current Window for the field.
|
|   Return Values :  TRUE     - field successfully increased
|                    FALSE    - there was some error
+--------------------------------------------------------------------------*/
static bool
Field_Grown(FIELD *field, int amount)
{
  bool result = FALSE;

  if (field && Growable(field))
    {
      bool single_line_field = Single_Line_Field(field);
      int old_buflen = Buffer_Length(field);
      int new_buflen;
      int old_dcols = field->dcols;
      int old_drows = field->drows;
      FIELD_CELL *oldbuf = field->buf;
      FIELD_CELL *newbuf;

      int growth;
      FORM *form = field->form;
      bool need_visual_update = ((form != (FORM *)0) &&
				 (form->status & _POSTED) &&
				 (form->current == field));

      if (need_visual_update)
	Synchronize_Buffer(form);

      if (single_line_field)
	{
	  growth = field->cols * amount;
	  if (field->maxgrow)
	    growth = Minimum(field->maxgrow - field->dcols, growth);
	  field->dcols += growth;
	  if (field->dcols == field->maxgrow)
	    ClrStatus(field, _MAY_GROW);
	}
      else
	{
	  growth = (field->rows + field->nrow) * amount;
	  if (field->maxgrow)
	    growth = Minimum(field->maxgrow - field->drows, growth);
	  field->drows += growth;
	  if (field->drows == field->maxgrow)
	    ClrStatus(field, _MAY_GROW);
	}
      /* drows, dcols changed, so we get really the new buffer length */
      new_buflen = Buffer_Length(field);
      newbuf = (FIELD_CELL *)malloc(Total_Buffer_Size(field));
      if (!newbuf)
	{
	  /* restore to previous state */
	  field->dcols = old_dcols;
	  field->drows = old_drows;
	  if ((single_line_field && (field->dcols != field->maxgrow)) ||
	      (!single_line_field && (field->drows != field->maxgrow)))
	    SetStatus(field, _MAY_GROW);
	}
      else
	{
	  /* Copy all the buffers.  This is the reason why we can't just use
	   * realloc().
	   */
	  int i, j;
	  FIELD_CELL *old_bp;
	  FIELD_CELL *new_bp;

	  result = TRUE;	/* allow sharing of recovery on failure */

	  T((T_CREATE("fieldcell %p"), (void *)newbuf));
	  field->buf = newbuf;
	  for (i = 0; i <= field->nbuf; i++)
	    {
	      new_bp = Address_Of_Nth_Buffer(field, i);
	      old_bp = oldbuf + i * (1 + old_buflen);
	      for (j = 0; j < old_buflen; ++j)
		new_bp[j] = old_bp[j];
	      while (j < new_buflen)
		new_bp[j++] = myBLANK;
	      new_bp[new_buflen] = myZEROS;
	    }

#if USE_WIDEC_SUPPORT && NCURSES_EXT_FUNCS
	  if (wresize(field->working, 1, Buffer_Length(field) + 1) == ERR)
	    result = FALSE;
#endif

	  if (need_visual_update && result)
	    {
	      WINDOW *new_window = newpad(field->drows, field->dcols);

	      if (new_window != 0)
		{
		  assert(form != (FORM *)0);
		  if (form->w)
		    delwin(form->w);
		  form->w = new_window;
		  Set_Field_Window_Attributes(field, form->w);
		  werase(form->w);
		  Buffer_To_Window(field, form->w);
		  untouchwin(form->w);
		  wmove(form->w, form->currow, form->curcol);
		}
	      else
		result = FALSE;
	    }

	  if (result)
	    {
	      free(oldbuf);
	      /* reflect changes in linked fields */
	      if (field != field->link)
		{
		  FIELD *linked_field;

		  for (linked_field = field->link;
		       linked_field != field;
		       linked_field = linked_field->link)
		    {
		      linked_field->buf = field->buf;
		      linked_field->drows = field->drows;
		      linked_field->dcols = field->dcols;
		    }
		}
	    }
	  else
	    {
	      /* restore old state */
	      field->dcols = old_dcols;
	      field->drows = old_drows;
	      field->buf = oldbuf;
	      if ((single_line_field &&
		   (field->dcols != field->maxgrow)) ||
		  (!single_line_field &&
		   (field->drows != field->maxgrow)))
		SetStatus(field, _MAY_GROW);
	      free(newbuf);
	    }
	}
    }
  return (result);
}

#ifdef NCURSES_MOUSE_VERSION
/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int Field_encloses(FIELD *field, int ry, int rx)
|
|   Description   :  Check if the given coordinates lie within the given field.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - invalid form pointer
|                    E_SYSTEM_ERROR    - form has no current field or
|                                        field-window
+--------------------------------------------------------------------------*/
static int
Field_encloses(FIELD *field, int ry, int rx)
{
  T((T_CALLED("Field_encloses(%p)"), (void *)field));
  if (field != 0
      && field->frow <= ry
      && (field->frow + field->rows) > ry
      && field->fcol <= rx
      && (field->fcol + field->cols) > rx)
    {
      RETURN(E_OK);
    }
  RETURN(E_INVALID_FIELD);
}
#endif

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int _nc_Position_Form_Cursor(FORM * form)
|
|   Description   :  Position the cursor in the window for the current
|                    field to be in sync. with the currow and curcol
|                    values.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - invalid form pointer
|                    E_SYSTEM_ERROR    - form has no current field or
|                                        field-window
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_Position_Form_Cursor(FORM *form)
{
  FIELD *field;
  WINDOW *formwin;

  if (!form)
    return (E_BAD_ARGUMENT);

  if (!form->w || !form->current)
    return (E_SYSTEM_ERROR);

  field = form->current;
  formwin = Get_Form_Window(form);

  wmove(form->w, form->currow, form->curcol);
  if (Has_Invisible_Parts(field))
    {
      /* in this case fieldwin isn't derived from formwin, so we have
         to move the cursor in formwin by hand... */
      wmove(formwin,
	    field->frow + form->currow - form->toprow,
	    field->fcol + form->curcol - form->begincol);
      wcursyncup(formwin);
    }
  else
    wcursyncup(form->w);
  return (E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int _nc_Refresh_Current_Field(FORM * form)
|
|   Description   :  Propagate the changes in the fields window to the
|                    window of the form.
|
|   Return Values :  E_OK              - on success
|                    E_BAD_ARGUMENT    - invalid form pointer
|                    E_SYSTEM_ERROR    - general error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_Refresh_Current_Field(FORM *form)
{
  WINDOW *formwin;
  FIELD *field;

  T((T_CALLED("_nc_Refresh_Current_Field(%p)"), (void *)form));

  if (!form)
    RETURN(E_BAD_ARGUMENT);

  if (!form->w || !form->current)
    RETURN(E_SYSTEM_ERROR);

  field = form->current;
  formwin = Get_Form_Window(form);

  if ((unsigned)field->opts & O_PUBLIC)
    {
      if (Is_Scroll_Field(field))
	{
	  /* Again, in this case the fieldwin isn't derived from formwin,
	     so we have to perform a copy operation. */
	  if (Single_Line_Field(field))
	    {
	      /* horizontal scrolling */
	      if (form->curcol < form->begincol)
		form->begincol = form->curcol;
	      else
		{
		  if (form->curcol >= (form->begincol + field->cols))
		    form->begincol = form->curcol - field->cols + 1;
		}
	      copywin(form->w,
		      formwin,
		      0,
		      form->begincol,
		      field->frow,
		      field->fcol,
		      field->frow,
		      field->cols + field->fcol - 1,
		      0);
	    }
	  else
	    {
	      /* A multi-line, i.e. vertical scrolling field */
	      int row_after_bottom, first_modified_row, first_unmodified_row;

	      if (field->drows > field->rows)
		{
		  row_after_bottom = form->toprow + field->rows;
		  if (form->currow < form->toprow)
		    {
		      form->toprow = form->currow;
		      SetStatus(field, _NEWTOP);
		    }
		  if (form->currow >= row_after_bottom)
		    {
		      form->toprow = form->currow - field->rows + 1;
		      SetStatus(field, _NEWTOP);
		    }
		  if (field->status & _NEWTOP)
		    {
		      /* means we have to copy whole range */
		      first_modified_row = form->toprow;
		      first_unmodified_row = first_modified_row + field->rows;
		      ClrStatus(field, _NEWTOP);
		    }
		  else
		    {
		      /* we try to optimize : finding the range of touched
		         lines */
		      first_modified_row = form->toprow;
		      while (first_modified_row < row_after_bottom)
			{
			  if (is_linetouched(form->w, first_modified_row))
			    break;
			  first_modified_row++;
			}
		      first_unmodified_row = first_modified_row;
		      while (first_unmodified_row < row_after_bottom)
			{
			  if (!is_linetouched(form->w, first_unmodified_row))
			    break;
			  first_unmodified_row++;
			}
		    }
		}
	      else
		{
		  first_modified_row = form->toprow;
		  first_unmodified_row = first_modified_row + field->rows;
		}
	      if (first_unmodified_row != first_modified_row)
		copywin(form->w,
			formwin,
			first_modified_row,
			0,
			field->frow + first_modified_row - form->toprow,
			field->fcol,
			field->frow + first_unmodified_row - form->toprow - 1,
			field->cols + field->fcol - 1,
			0);
	    }
	  wsyncup(formwin);
	}
      else
	{
	  /* if the field-window is simply a derived window, i.e. contains no
	   * invisible parts, the whole thing is trivial
	   */
	  wsyncup(form->w);
	}
    }
  untouchwin(form->w);
  returnCode(_nc_Position_Form_Cursor(form));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void Perform_Justification(
|                                        FIELD  * field,
|                                        WINDOW * win)
|
|   Description   :  Output field with requested justification
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
static void
Perform_Justification(FIELD *field, WINDOW *win)
{
  FIELD_CELL *bp;
  int len;
  int col = 0;

  bp = Get_Start_Of_Data(field->buf, Buffer_Length(field));
  len = (int)(After_End_Of_Data(field->buf, Buffer_Length(field)) - bp);

  if (len > 0)
    {
      assert(win && (field->drows == 1) && (field->dcols == field->cols));

      switch (field->just)
	{
	case JUSTIFY_LEFT:
	  break;
	case JUSTIFY_CENTER:
	  col = (field->cols - len) / 2;
	  break;
	case JUSTIFY_RIGHT:
	  col = field->cols - len;
	  break;
	default:
	  break;
	}

      wmove(win, 0, col);
      myADDNSTR(win, bp, len);
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void Undo_Justification(
|                                     FIELD  * field,
|                                     WINDOW * win)
|
|   Description   :  Display field without any justification, i.e.
|                    left justified
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
static void
Undo_Justification(FIELD *field, WINDOW *win)
{
  FIELD_CELL *bp;
  int len;

  bp = Get_Start_Of_Data(field->buf, Buffer_Length(field));
  len = (int)(After_End_Of_Data(field->buf, Buffer_Length(field)) - bp);

  if (len > 0)
    {
      assert(win);
      wmove(win, 0, 0);
      myADDNSTR(win, bp, len);
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static bool Check_Char(FORM  *form,
|                                           FIELD *field,
|                                           FIELDTYPE * typ,
|                                           int ch,
|                                           TypeArgument *argp)
|
|   Description   :  Perform a single character check for character ch
|                    according to the fieldtype instance.
|
|   Return Values :  TRUE             - Character is valid
|                    FALSE            - Character is invalid
+--------------------------------------------------------------------------*/
static bool
Check_Char(FORM *form,
	   FIELD *field,
	   FIELDTYPE *typ,
	   int ch,
	   TypeArgument *argp)
{
  if (typ)
    {
      if (typ->status & _LINKED_TYPE)
	{
	  assert(argp);
	  return (
		   Check_Char(form, field, typ->left, ch, argp->left) ||
		   Check_Char(form, field, typ->right, ch, argp->right));
	}
      else
	{
#if NCURSES_INTEROP_FUNCS
	  if (typ->charcheck.occheck)
	    {
	      if (typ->status & _GENERIC)
		return typ->charcheck.gccheck(ch, form, field, (void *)argp);
	      else
		return typ->charcheck.occheck(ch, (void *)argp);
	    }
#else
	  if (typ->ccheck)
	    return typ->ccheck(ch, (void *)argp);
#endif
	}
    }
  return (!iscntrl(UChar(ch)) ? TRUE : FALSE);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Display_Or_Erase_Field(
|                                           FIELD * field,
|                                           bool bEraseFlag)
|
|   Description   :  Create a subwindow for the field and display the
|                    buffer contents (apply justification if required)
|                    or simply erase the field.
|
|   Return Values :  E_OK           - on success
|                    E_SYSTEM_ERROR - some error (typical no memory)
+--------------------------------------------------------------------------*/
static int
Display_Or_Erase_Field(FIELD *field, bool bEraseFlag)
{
  WINDOW *win;
  WINDOW *fwin;

  if (!field)
    return E_SYSTEM_ERROR;

  fwin = Get_Form_Window(field->form);
  win = derwin(fwin,
	       field->rows, field->cols, field->frow, field->fcol);

  if (!win)
    return E_SYSTEM_ERROR;
  else
    {
      if ((unsigned)field->opts & O_VISIBLE)
	{
	  Set_Field_Window_Attributes(field, win);
	}
      else
	{
	  (void)wattrset(win, (int)WINDOW_ATTRS(fwin));
	}
      werase(win);
    }

  if (!bEraseFlag)
    {
      if ((unsigned)field->opts & O_PUBLIC)
	{
	  if (Justification_Allowed(field))
	    Perform_Justification(field, win);
	  else
	    Buffer_To_Window(field, win);
	}
      ClrStatus(field, _NEWTOP);
    }
  wsyncup(win);
  delwin(win);
  return E_OK;
}

/* Macros to preset the bEraseFlag */
#define Display_Field(field) Display_Or_Erase_Field(field,FALSE)
#define Erase_Field(field)   Display_Or_Erase_Field(field,TRUE)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Synchronize_Field(FIELD * field)
|
|   Description   :  Synchronize the windows content with the value in
|                    the buffer.
|
|   Return Values :  E_OK                - success
|                    E_BAD_ARGUMENT      - invalid field pointer
|                    E_SYSTEM_ERROR      - some severe basic error
+--------------------------------------------------------------------------*/
static int
Synchronize_Field(FIELD *field)
{
  FORM *form;
  int res = E_OK;

  if (!field)
    return (E_BAD_ARGUMENT);

  if (((form = field->form) != (FORM *)0)
      && Field_Really_Appears(field))
    {
      if (field == form->current)
	{
	  form->currow = form->curcol = form->toprow = form->begincol = 0;
	  werase(form->w);

	  if (((unsigned)field->opts & O_PUBLIC) && Justification_Allowed(field))
	    Undo_Justification(field, form->w);
	  else
	    Buffer_To_Window(field, form->w);

	  SetStatus(field, _NEWTOP);
	  res = _nc_Refresh_Current_Field(form);
	}
      else
	res = Display_Field(field);
    }
  SetStatus(field, _CHANGED);
  return (res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Synchronize_Linked_Fields(FIELD * field)
|
|   Description   :  Propagate the Synchronize_Field function to all linked
|                    fields. The first error that occurs in the sequence
|                    of updates is the return value.
|
|   Return Values :  E_OK                - success
|                    E_BAD_ARGUMENT      - invalid field pointer
|                    E_SYSTEM_ERROR      - some severe basic error
+--------------------------------------------------------------------------*/
static int
Synchronize_Linked_Fields(FIELD *field)
{
  FIELD *linked_field;
  int res = E_OK;
  int syncres;

  if (!field)
    return (E_BAD_ARGUMENT);

  if (!field->link)
    return (E_SYSTEM_ERROR);

  for (linked_field = field->link;
       (linked_field != field) && (linked_field != 0);
       linked_field = linked_field->link)
    {
      if (((syncres = Synchronize_Field(linked_field)) != E_OK) &&
	  (res == E_OK))
	res = syncres;
    }
  return (res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int _nc_Synchronize_Attributes(FIELD * field)
|
|   Description   :  If a fields visual attributes have changed, this
|                    routine is called to propagate those changes to the
|                    screen.
|
|   Return Values :  E_OK             - success
|                    E_BAD_ARGUMENT   - invalid field pointer
|                    E_SYSTEM_ERROR   - some severe basic error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_Synchronize_Attributes(FIELD *field)
{
  FORM *form;
  int res = E_OK;
  WINDOW *formwin;

  T((T_CALLED("_nc_Synchronize_Attributes(%p)"), (void *)field));

  if (!field)
    returnCode(E_BAD_ARGUMENT);

  CHECKPOS(field->form);
  if (((form = field->form) != (FORM *)0)
      && Field_Really_Appears(field))
    {
      if (form->current == field)
	{
	  Synchronize_Buffer(form);
	  Set_Field_Window_Attributes(field, form->w);
	  werase(form->w);
	  wmove(form->w, form->currow, form->curcol);

	  if ((unsigned)field->opts & O_PUBLIC)
	    {
	      if (Justification_Allowed(field))
		Undo_Justification(field, form->w);
	      else
		Buffer_To_Window(field, form->w);
	    }
	  else
	    {
	      formwin = Get_Form_Window(form);
	      copywin(form->w, formwin,
		      0, 0,
		      field->frow, field->fcol,
		      field->rows - 1, field->cols - 1, 0);
	      wsyncup(formwin);
	      Buffer_To_Window(field, form->w);
	      SetStatus(field, _NEWTOP);	/* fake refresh to paint all */
	      _nc_Refresh_Current_Field(form);
	    }
	}
      else
	{
	  res = Display_Field(field);
	}
    }
  CHECKPOS(form);
  returnCode(res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int _nc_Synchronize_Options(FIELD * field,
|                                                Field_Options newopts)
|
|   Description   :  If a fields options have changed, this routine is
|                    called to propagate these changes to the screen and
|                    to really change the behavior of the field.
|
|   Return Values :  E_OK                - success
|                    E_BAD_ARGUMENT      - invalid field pointer
|                    E_CURRENT           - field is the current one
|                    E_SYSTEM_ERROR      - some severe basic error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_Synchronize_Options(FIELD *field, Field_Options newopts)
{
  Field_Options oldopts;
  Field_Options changed_opts;
  FORM *form;
  int res = E_OK;

  T((T_CALLED("_nc_Synchronize_Options(%p,%#x)"), (void *)field, newopts));

  if (!field)
    returnCode(E_BAD_ARGUMENT);

  oldopts = field->opts;
  changed_opts = oldopts ^ newopts;
  field->opts = newopts;
  form = field->form;

  if (form)
    {
      if (form->status & _POSTED)
	{
	  if (form->current == field)
	    {
	      field->opts = oldopts;
	      returnCode(E_CURRENT);
	    }
	  if (form->curpage == field->page)
	    {
	      if ((unsigned)changed_opts & O_VISIBLE)
		{
		  if ((unsigned)newopts & O_VISIBLE)
		    res = Display_Field(field);
		  else
		    res = Erase_Field(field);
		}
	      else
		{
		  if (((unsigned)changed_opts & O_PUBLIC) &&
		      ((unsigned)newopts & O_VISIBLE))
		    res = Display_Field(field);
		}
	    }
	}
    }

  if ((unsigned)changed_opts & O_STATIC)
    {
      bool single_line_field = Single_Line_Field(field);
      int res2 = E_OK;

      if ((unsigned)newopts & O_STATIC)
	{
	  /* the field becomes now static */
	  ClrStatus(field, _MAY_GROW);
	  /* if actually we have no hidden columns, justification may
	     occur again */
	  if (single_line_field &&
	      (field->cols == field->dcols) &&
	      (field->just != NO_JUSTIFICATION) &&
	      Field_Really_Appears(field))
	    {
	      res2 = Display_Field(field);
	    }
	}
      else
	{
	  /* field is no longer static */
	  if ((field->maxgrow == 0) ||
	      (single_line_field && (field->dcols < field->maxgrow)) ||
	      (!single_line_field && (field->drows < field->maxgrow)))
	    {
	      SetStatus(field, _MAY_GROW);
	      /* a field with justification now changes its behavior,
	         so we must redisplay it */
	      if (single_line_field &&
		  (field->just != NO_JUSTIFICATION) &&
		  Field_Really_Appears(field))
		{
		  res2 = Display_Field(field);
		}
	    }
	}
      if (res2 != E_OK)
	res = res2;
    }

  returnCode(res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int _nc_Set_Current_Field(FORM  * form,
|                                              FIELD * newfield)
|
|   Description   :  Make the newfield the new current field.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - invalid form or field pointer
|                    E_SYSTEM_ERROR    - some severe basic error
|                    E_NOT_CONNECTED   - no fields are connected to the form
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_Set_Current_Field(FORM *form, FIELD *newfield)
{
  FIELD *field;
  WINDOW *new_window;

  T((T_CALLED("_nc_Set_Current_Field(%p,%p)"), (void *)form, (void *)newfield));

  if (!form || !newfield || !form->current || (newfield->form != form))
    returnCode(E_BAD_ARGUMENT);

  if ((form->status & _IN_DRIVER))
    returnCode(E_BAD_STATE);

  if (!(form->field))
    returnCode(E_NOT_CONNECTED);

  field = form->current;

  if ((field != newfield) ||
      !(form->status & _POSTED))
    {
      if ((form->w) &&
	  ((unsigned)field->opts & O_VISIBLE) &&
	  (field->form->curpage == field->page))
	{
	  _nc_Refresh_Current_Field(form);
	  if ((unsigned)field->opts & O_PUBLIC)
	    {
	      if (field->drows > field->rows)
		{
		  if (form->toprow == 0)
		    ClrStatus(field, _NEWTOP);
		  else
		    SetStatus(field, _NEWTOP);
		}
	      else
		{
		  if (Justification_Allowed(field))
		    {
		      Window_To_Buffer(form, field);
		      werase(form->w);
		      Perform_Justification(field, form->w);
		      wsyncup(form->w);
		    }
		}
	    }
	  delwin(form->w);
	  form->w = (WINDOW *)0;
	}

      field = newfield;

      if (Has_Invisible_Parts(field))
	new_window = newpad(field->drows, field->dcols);
      else
	new_window = derwin(Get_Form_Window(form),
			    field->rows, field->cols, field->frow, field->fcol);

      if (!new_window)
	returnCode(E_SYSTEM_ERROR);

      form->current = field;

      if (form->w)
	delwin(form->w);
      form->w = new_window;

      ClrStatus(form, _WINDOW_MODIFIED);
      Set_Field_Window_Attributes(field, form->w);

      if (Has_Invisible_Parts(field))
	{
	  werase(form->w);
	  Buffer_To_Window(field, form->w);
	}
      else
	{
	  if (Justification_Allowed(field))
	    {
	      werase(form->w);
	      Undo_Justification(field, form->w);
	      wsyncup(form->w);
	    }
	}

      untouchwin(form->w);
    }

  form->currow = form->curcol = form->toprow = form->begincol = 0;
  returnCode(E_OK);
}

/*----------------------------------------------------------------------------
  Intra-Field Navigation routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Next_Character(FORM * form)
|
|   Description   :  Move to the next character in the field. In a multi-line
|                    field this wraps at the end of the line.
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - at the rightmost position
+--------------------------------------------------------------------------*/
static int
IFN_Next_Character(FORM *form)
{
  FIELD *field = form->current;
  int step = myWCWIDTH(form->w, form->currow, form->curcol);

  T((T_CALLED("IFN_Next_Character(%p)"), (void *)form));
  if ((form->curcol += step) == field->dcols)
    {
      if ((++(form->currow)) == field->drows)
	{
#if GROW_IF_NAVIGATE
	  if (!Single_Line_Field(field) && Field_Grown(field, 1))
	    {
	      form->curcol = 0;
	      returnCode(E_OK);
	    }
#endif
	  form->currow--;
#if GROW_IF_NAVIGATE
	  if (Single_Line_Field(field) && Field_Grown(field, 1))
	    returnCode(E_OK);
#endif
	  form->curcol -= step;
	  returnCode(E_REQUEST_DENIED);
	}
      form->curcol = 0;
    }
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Previous_Character(FORM * form)
|
|   Description   :  Move to the previous character in the field. In a
|                    multi-line field this wraps and the beginning of the
|                    line.
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - at the leftmost position
+--------------------------------------------------------------------------*/
static int
IFN_Previous_Character(FORM *form)
{
  int amount = myWCWIDTH(form->w, form->currow, form->curcol - 1);
  int oldcol = form->curcol;

  T((T_CALLED("IFN_Previous_Character(%p)"), (void *)form));
  if ((form->curcol -= amount) < 0)
    {
      if ((--(form->currow)) < 0)
	{
	  form->currow++;
	  form->curcol = oldcol;
	  returnCode(E_REQUEST_DENIED);
	}
      form->curcol = form->current->dcols - 1;
    }
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Next_Line(FORM * form)
|
|   Description   :  Move to the beginning of the next line in the field
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - at the last line
+--------------------------------------------------------------------------*/
static int
IFN_Next_Line(FORM *form)
{
  FIELD *field = form->current;

  T((T_CALLED("IFN_Next_Line(%p)"), (void *)form));
  if ((++(form->currow)) == field->drows)
    {
#if GROW_IF_NAVIGATE
      if (!Single_Line_Field(field) && Field_Grown(field, 1))
	returnCode(E_OK);
#endif
      form->currow--;
      returnCode(E_REQUEST_DENIED);
    }
  form->curcol = 0;
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Previous_Line(FORM * form)
|
|   Description   :  Move to the beginning of the previous line in the field
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - at the first line
+--------------------------------------------------------------------------*/
static int
IFN_Previous_Line(FORM *form)
{
  T((T_CALLED("IFN_Previous_Line(%p)"), (void *)form));
  if ((--(form->currow)) < 0)
    {
      form->currow++;
      returnCode(E_REQUEST_DENIED);
    }
  form->curcol = 0;
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Next_Word(FORM * form)
|
|   Description   :  Move to the beginning of the next word in the field.
|
|   Return Values :  E_OK             - success
|                    E_REQUEST_DENIED - there is no next word
+--------------------------------------------------------------------------*/
static int
IFN_Next_Word(FORM *form)
{
  FIELD *field = form->current;
  FIELD_CELL *bp = Address_Of_Current_Position_In_Buffer(form);
  FIELD_CELL *s;
  FIELD_CELL *t;

  T((T_CALLED("IFN_Next_Word(%p)"), (void *)form));

  /* We really need access to the data, so we have to synchronize */
  Synchronize_Buffer(form);

  /* Go to the first whitespace after the current position (including
     current position). This is then the starting point to look for the
     next non-blank data */
  s = Get_First_Whitespace_Character(bp, Buffer_Length(field) -
				     (int)(bp - field->buf));

  /* Find the start of the next word */
  t = Get_Start_Of_Data(s, Buffer_Length(field) -
			(int)(s - field->buf));
#if !FRIENDLY_PREV_NEXT_WORD
  if (s == t)
    returnCode(E_REQUEST_DENIED);
  else
#endif
    {
      Adjust_Cursor_Position(form, t);
      returnCode(E_OK);
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Previous_Word(FORM * form)
|
|   Description   :  Move to the beginning of the previous word in the field.
|
|   Return Values :  E_OK             - success
|                    E_REQUEST_DENIED - there is no previous word
+--------------------------------------------------------------------------*/
static int
IFN_Previous_Word(FORM *form)
{
  FIELD *field = form->current;
  FIELD_CELL *bp = Address_Of_Current_Position_In_Buffer(form);
  FIELD_CELL *s;
  FIELD_CELL *t;
  bool again = FALSE;

  T((T_CALLED("IFN_Previous_Word(%p)"), (void *)form));

  /* We really need access to the data, so we have to synchronize */
  Synchronize_Buffer(form);

  s = After_End_Of_Data(field->buf, (int)(bp - field->buf));
  /* s points now right after the last non-blank in the buffer before bp.
     If bp was in a word, s equals bp. In this case we must find the last
     whitespace in the buffer before bp and repeat the game to really find
     the previous word! */
  if (s == bp)
    again = TRUE;

  /* And next call now goes backward to look for the last whitespace
     before that, pointing right after this, so it points to the begin
     of the previous word.
   */
  t = After_Last_Whitespace_Character(field->buf, (int)(s - field->buf));
#if !FRIENDLY_PREV_NEXT_WORD
  if (s == t)
    returnCode(E_REQUEST_DENIED);
#endif
  if (again)
    {
      /* and do it again, replacing bp by t */
      s = After_End_Of_Data(field->buf, (int)(t - field->buf));
      t = After_Last_Whitespace_Character(field->buf, (int)(s - field->buf));
#if !FRIENDLY_PREV_NEXT_WORD
      if (s == t)
	returnCode(E_REQUEST_DENIED);
#endif
    }
  Adjust_Cursor_Position(form, t);
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Beginning_Of_Field(FORM * form)
|
|   Description   :  Place the cursor at the first non-pad character in
|                    the field.
|
|   Return Values :  E_OK             - success
+--------------------------------------------------------------------------*/
static int
IFN_Beginning_Of_Field(FORM *form)
{
  FIELD *field = form->current;

  T((T_CALLED("IFN_Beginning_Of_Field(%p)"), (void *)form));
  Synchronize_Buffer(form);
  Adjust_Cursor_Position(form,
			 Get_Start_Of_Data(field->buf, Buffer_Length(field)));
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_End_Of_Field(FORM * form)
|
|   Description   :  Place the cursor after the last non-pad character in
|                    the field. If the field occupies the last position in
|                    the buffer, the cursor is positioned on the last
|                    character.
|
|   Return Values :  E_OK              - success
+--------------------------------------------------------------------------*/
static int
IFN_End_Of_Field(FORM *form)
{
  FIELD *field = form->current;
  FIELD_CELL *pos;

  T((T_CALLED("IFN_End_Of_Field(%p)"), (void *)form));
  Synchronize_Buffer(form);
  pos = After_End_Of_Data(field->buf, Buffer_Length(field));
  if (pos == (field->buf + Buffer_Length(field)))
    pos--;
  Adjust_Cursor_Position(form, pos);
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Beginning_Of_Line(FORM * form)
|
|   Description   :  Place the cursor on the first non-pad character in
|                    the current line of the field.
|
|   Return Values :  E_OK         - success
+--------------------------------------------------------------------------*/
static int
IFN_Beginning_Of_Line(FORM *form)
{
  FIELD *field = form->current;

  T((T_CALLED("IFN_Beginning_Of_Line(%p)"), (void *)form));
  Synchronize_Buffer(form);
  Adjust_Cursor_Position(form,
			 Get_Start_Of_Data(Address_Of_Current_Row_In_Buffer(form),
					   field->dcols));
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_End_Of_Line(FORM * form)
|
|   Description   :  Place the cursor after the last non-pad character in the
|                    current line of the field. If the field occupies the
|                    last column in the line, the cursor is positioned on the
|                    last character of the line.
|
|   Return Values :  E_OK        - success
+--------------------------------------------------------------------------*/
static int
IFN_End_Of_Line(FORM *form)
{
  FIELD *field = form->current;
  FIELD_CELL *pos;
  FIELD_CELL *bp;

  T((T_CALLED("IFN_End_Of_Line(%p)"), (void *)form));
  Synchronize_Buffer(form);
  bp = Address_Of_Current_Row_In_Buffer(form);
  pos = After_End_Of_Data(bp, field->dcols);
  if (pos == (bp + field->dcols))
    pos--;
  Adjust_Cursor_Position(form, pos);
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Left_Character(FORM * form)
|
|   Description   :  Move one character to the left in the current line.
|                    This doesn't cycle.
|
|   Return Values :  E_OK             - success
|                    E_REQUEST_DENIED - already in first column
+--------------------------------------------------------------------------*/
static int
IFN_Left_Character(FORM *form)
{
  int amount = myWCWIDTH(form->w, form->currow, form->curcol - 1);
  int oldcol = form->curcol;

  T((T_CALLED("IFN_Left_Character(%p)"), (void *)form));
  if ((form->curcol -= amount) < 0)
    {
      form->curcol = oldcol;
      returnCode(E_REQUEST_DENIED);
    }
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Right_Character(FORM * form)
|
|   Description   :  Move one character to the right in the current line.
|                    This doesn't cycle.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - already in last column
+--------------------------------------------------------------------------*/
static int
IFN_Right_Character(FORM *form)
{
  int amount = myWCWIDTH(form->w, form->currow, form->curcol);
  int oldcol = form->curcol;

  T((T_CALLED("IFN_Right_Character(%p)"), (void *)form));
  if ((form->curcol += amount) >= form->current->dcols)
    {
#if GROW_IF_NAVIGATE
      FIELD *field = form->current;

      if (Single_Line_Field(field) && Field_Grown(field, 1))
	returnCode(E_OK);
#endif
      form->curcol = oldcol;
      returnCode(E_REQUEST_DENIED);
    }
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Up_Character(FORM * form)
|
|   Description   :  Move one line up. This doesn't cycle through the lines
|                    of the field.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - already in last column
+--------------------------------------------------------------------------*/
static int
IFN_Up_Character(FORM *form)
{
  T((T_CALLED("IFN_Up_Character(%p)"), (void *)form));
  if ((--(form->currow)) < 0)
    {
      form->currow++;
      returnCode(E_REQUEST_DENIED);
    }
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int IFN_Down_Character(FORM * form)
|
|   Description   :  Move one line down. This doesn't cycle through the
|                    lines of the field.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - already in last column
+--------------------------------------------------------------------------*/
static int
IFN_Down_Character(FORM *form)
{
  FIELD *field = form->current;

  T((T_CALLED("IFN_Down_Character(%p)"), (void *)form));
  if ((++(form->currow)) == field->drows)
    {
#if GROW_IF_NAVIGATE
      if (!Single_Line_Field(field) && Field_Grown(field, 1))
	returnCode(E_OK);
#endif
      --(form->currow);
      returnCode(E_REQUEST_DENIED);
    }
  returnCode(E_OK);
}
/*----------------------------------------------------------------------------
  END of Intra-Field Navigation routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Vertical scrolling helper routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int VSC_Generic(FORM *form, int nlines)
|
|   Description   :  Scroll multi-line field forward (nlines>0) or
|                    backward (nlines<0) this many lines.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - can't scroll
+--------------------------------------------------------------------------*/
static int
VSC_Generic(FORM *form, int nlines)
{
  FIELD *field = form->current;
  int res = E_REQUEST_DENIED;
  int rows_to_go = (nlines > 0 ? nlines : -nlines);

  if (nlines > 0)
    {
      if ((rows_to_go + form->toprow) > (field->drows - field->rows))
	rows_to_go = (field->drows - field->rows - form->toprow);

      if (rows_to_go > 0)
	{
	  form->currow += rows_to_go;
	  form->toprow += rows_to_go;
	  res = E_OK;
	}
    }
  else
    {
      if (rows_to_go > form->toprow)
	rows_to_go = form->toprow;

      if (rows_to_go > 0)
	{
	  form->currow -= rows_to_go;
	  form->toprow -= rows_to_go;
	  res = E_OK;
	}
    }
  return (res);
}
/*----------------------------------------------------------------------------
  End of Vertical scrolling helper routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Vertical scrolling routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Vertical_Scrolling(
|                                           int (* const fct) (FORM *),
|                                           FORM * form)
|
|   Description   :  Performs the generic vertical scrolling routines.
|                    This has to check for a multi-line field and to set
|                    the _NEWTOP flag if scrolling really occurred.
|
|   Return Values :  Propagated error code from low-level driver calls
+--------------------------------------------------------------------------*/
static int
Vertical_Scrolling(int (*const fct) (FORM *), FORM *form)
{
  int res = E_REQUEST_DENIED;

  if (!Single_Line_Field(form->current))
    {
      res = fct(form);
      if (res == E_OK)
	SetStatus(form, _NEWTOP);
    }
  return (res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int VSC_Scroll_Line_Forward(FORM * form)
|
|   Description   :  Scroll multi-line field forward a line
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - no data ahead
+--------------------------------------------------------------------------*/
static int
VSC_Scroll_Line_Forward(FORM *form)
{
  T((T_CALLED("VSC_Scroll_Line_Forward(%p)"), (void *)form));
  returnCode(VSC_Generic(form, 1));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int VSC_Scroll_Line_Backward(FORM * form)
|
|   Description   :  Scroll multi-line field backward a line
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - no data behind
+--------------------------------------------------------------------------*/
static int
VSC_Scroll_Line_Backward(FORM *form)
{
  T((T_CALLED("VSC_Scroll_Line_Backward(%p)"), (void *)form));
  returnCode(VSC_Generic(form, -1));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int VSC_Scroll_Page_Forward(FORM * form)
|
|   Description   :  Scroll a multi-line field forward a page
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - no data ahead
+--------------------------------------------------------------------------*/
static int
VSC_Scroll_Page_Forward(FORM *form)
{
  T((T_CALLED("VSC_Scroll_Page_Forward(%p)"), (void *)form));
  returnCode(VSC_Generic(form, form->current->rows));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int VSC_Scroll_Half_Page_Forward(FORM * form)
|
|   Description   :  Scroll a multi-line field forward half a page
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - no data ahead
+--------------------------------------------------------------------------*/
static int
VSC_Scroll_Half_Page_Forward(FORM *form)
{
  T((T_CALLED("VSC_Scroll_Half_Page_Forward(%p)"), (void *)form));
  returnCode(VSC_Generic(form, (form->current->rows + 1) / 2));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int VSC_Scroll_Page_Backward(FORM * form)
|
|   Description   :  Scroll a multi-line field backward a page
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - no data behind
+--------------------------------------------------------------------------*/
static int
VSC_Scroll_Page_Backward(FORM *form)
{
  T((T_CALLED("VSC_Scroll_Page_Backward(%p)"), (void *)form));
  returnCode(VSC_Generic(form, -(form->current->rows)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int VSC_Scroll_Half_Page_Backward(FORM * form)
|
|   Description   :  Scroll a multi-line field backward half a page
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - no data behind
+--------------------------------------------------------------------------*/
static int
VSC_Scroll_Half_Page_Backward(FORM *form)
{
  T((T_CALLED("VSC_Scroll_Half_Page_Backward(%p)"), (void *)form));
  returnCode(VSC_Generic(form, -((form->current->rows + 1) / 2)));
}
/*----------------------------------------------------------------------------
  End of Vertical scrolling routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Horizontal scrolling helper routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int HSC_Generic(FORM *form, int ncolumns)
|
|   Description   :  Scroll single-line field forward (ncolumns>0) or
|                    backward (ncolumns<0) this many columns.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - can't scroll
+--------------------------------------------------------------------------*/
static int
HSC_Generic(FORM *form, int ncolumns)
{
  FIELD *field = form->current;
  int res = E_REQUEST_DENIED;
  int cols_to_go = (ncolumns > 0 ? ncolumns : -ncolumns);

  if (ncolumns > 0)
    {
      if ((cols_to_go + form->begincol) > (field->dcols - field->cols))
	cols_to_go = field->dcols - field->cols - form->begincol;

      if (cols_to_go > 0)
	{
	  form->curcol += cols_to_go;
	  form->begincol += cols_to_go;
	  res = E_OK;
	}
    }
  else
    {
      if (cols_to_go > form->begincol)
	cols_to_go = form->begincol;

      if (cols_to_go > 0)
	{
	  form->curcol -= cols_to_go;
	  form->begincol -= cols_to_go;
	  res = E_OK;
	}
    }
  return (res);
}
/*----------------------------------------------------------------------------
  End of Horizontal scrolling helper routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Horizontal scrolling routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Horizontal_Scrolling(
|                                          int (* const fct) (FORM *),
|                                          FORM * form)
|
|   Description   :  Performs the generic horizontal scrolling routines.
|                    This has to check for a single-line field.
|
|   Return Values :  Propagated error code from low-level driver calls
+--------------------------------------------------------------------------*/
static int
Horizontal_Scrolling(int (*const fct) (FORM *), FORM *form)
{
  if (Single_Line_Field(form->current))
    return fct(form);
  else
    return (E_REQUEST_DENIED);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int HSC_Scroll_Char_Forward(FORM * form)
|
|   Description   :  Scroll single-line field forward a character
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - no data ahead
+--------------------------------------------------------------------------*/
static int
HSC_Scroll_Char_Forward(FORM *form)
{
  T((T_CALLED("HSC_Scroll_Char_Forward(%p)"), (void *)form));
  returnCode(HSC_Generic(form, 1));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int HSC_Scroll_Char_Backward(FORM * form)
|
|   Description   :  Scroll single-line field backward a character
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - no data behind
+--------------------------------------------------------------------------*/
static int
HSC_Scroll_Char_Backward(FORM *form)
{
  T((T_CALLED("HSC_Scroll_Char_Backward(%p)"), (void *)form));
  returnCode(HSC_Generic(form, -1));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int HSC_Horizontal_Line_Forward(FORM* form)
|
|   Description   :  Scroll single-line field forward a line
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - no data ahead
+--------------------------------------------------------------------------*/
static int
HSC_Horizontal_Line_Forward(FORM *form)
{
  T((T_CALLED("HSC_Horizontal_Line_Forward(%p)"), (void *)form));
  returnCode(HSC_Generic(form, form->current->cols));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int HSC_Horizontal_Half_Line_Forward(FORM* form)
|
|   Description   :  Scroll single-line field forward half a line
|
|   Return Values :  E_OK               - success
|                    E_REQUEST_DENIED   - no data ahead
+--------------------------------------------------------------------------*/
static int
HSC_Horizontal_Half_Line_Forward(FORM *form)
{
  T((T_CALLED("HSC_Horizontal_Half_Line_Forward(%p)"), (void *)form));
  returnCode(HSC_Generic(form, (form->current->cols + 1) / 2));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int HSC_Horizontal_Line_Backward(FORM* form)
|
|   Description   :  Scroll single-line field backward a line
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - no data behind
+--------------------------------------------------------------------------*/
static int
HSC_Horizontal_Line_Backward(FORM *form)
{
  T((T_CALLED("HSC_Horizontal_Line_Backward(%p)"), (void *)form));
  returnCode(HSC_Generic(form, -(form->current->cols)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int HSC_Horizontal_Half_Line_Backward(FORM* form)
|
|   Description   :  Scroll single-line field backward half a line
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - no data behind
+--------------------------------------------------------------------------*/
static int
HSC_Horizontal_Half_Line_Backward(FORM *form)
{
  T((T_CALLED("HSC_Horizontal_Half_Line_Backward(%p)"), (void *)form));
  returnCode(HSC_Generic(form, -((form->current->cols + 1) / 2)));
}

/*----------------------------------------------------------------------------
  End of Horizontal scrolling routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Helper routines for Field Editing
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static bool Is_There_Room_For_A_Line(FORM * form)
|
|   Description   :  Check whether or not there is enough room in the
|                    buffer to enter a whole line.
|
|   Return Values :  TRUE   - there is enough space
|                    FALSE  - there is not enough space
+--------------------------------------------------------------------------*/
NCURSES_INLINE static bool
Is_There_Room_For_A_Line(FORM *form)
{
  FIELD *field = form->current;
  FIELD_CELL *begin_of_last_line, *s;

  Synchronize_Buffer(form);
  begin_of_last_line = Address_Of_Row_In_Buffer(field, (field->drows - 1));
  s = After_End_Of_Data(begin_of_last_line, field->dcols);
  return ((s == begin_of_last_line) ? TRUE : FALSE);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static bool Is_There_Room_For_A_Char_In_Line(FORM * form)
|
|   Description   :  Checks whether or not there is room for a new character
|                    in the current line.
|
|   Return Values :  TRUE    - there is room
|                    FALSE   - there is not enough room (line full)
+--------------------------------------------------------------------------*/
NCURSES_INLINE static bool
Is_There_Room_For_A_Char_In_Line(FORM *form)
{
  int last_char_in_line;

  wmove(form->w, form->currow, form->current->dcols - 1);
  last_char_in_line = (int)(winch(form->w) & A_CHARTEXT);
  wmove(form->w, form->currow, form->curcol);
  return (((last_char_in_line == form->current->pad) ||
	   is_blank(last_char_in_line)) ? TRUE : FALSE);
}

#define There_Is_No_Room_For_A_Char_In_Line(f) \
  !Is_There_Room_For_A_Char_In_Line(f)

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Insert_String(
|                                             FORM * form,
|                                             int row,
|                                             char *txt,
|                                             int  len )
|
|   Description   :  Insert the 'len' characters beginning at pointer 'txt'
|                    into the 'row' of the 'form'. The insertion occurs
|                    on the beginning of the row, all other characters are
|                    moved to the right. After the text a pad character will
|                    be inserted to separate the text from the rest. If
|                    necessary the insertion moves characters on the next
|                    line to make place for the requested insertion string.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  -
|                    E_SYSTEM_ERROR    - system error
+--------------------------------------------------------------------------*/
static int
Insert_String(FORM *form, int row, FIELD_CELL *txt, int len)
{
  FIELD *field = form->current;
  FIELD_CELL *bp = Address_Of_Row_In_Buffer(field, row);
  int datalen = (int)(After_End_Of_Data(bp, field->dcols) - bp);
  int freelen = field->dcols - datalen;
  int requiredlen = len + 1;
  FIELD_CELL *split;
  int result = E_REQUEST_DENIED;

  if (freelen >= requiredlen)
    {
      wmove(form->w, row, 0);
      myINSNSTR(form->w, txt, len);
      wmove(form->w, row, len);
      myINSNSTR(form->w, &myBLANK, 1);
      return E_OK;
    }
  else
    {
      /* we have to move characters on the next line. If we are on the
         last line this may work, if the field is growable */
      if ((row == (field->drows - 1)) && Growable(field))
	{
	  if (!Field_Grown(field, 1))
	    return (E_SYSTEM_ERROR);
	  /* !!!Side-Effect : might be changed due to growth!!! */
	  bp = Address_Of_Row_In_Buffer(field, row);
	}

      if (row < (field->drows - 1))
	{
	  split =
	    After_Last_Whitespace_Character(bp,
					    (int)(Get_Start_Of_Data(bp
								    + field->dcols
								    - requiredlen,
								    requiredlen)
						  - bp));
	  /* split points now to the first character of the portion of the
	     line that must be moved to the next line */
	  datalen = (int)(split - bp);	/* + freelen has to stay on this line   */
	  freelen = field->dcols - (datalen + freelen);		/* for the next line */

	  if ((result = Insert_String(form, row + 1, split, freelen)) == E_OK)
	    {
	      wmove(form->w, row, datalen);
	      wclrtoeol(form->w);
	      wmove(form->w, row, 0);
	      myINSNSTR(form->w, txt, len);
	      wmove(form->w, row, len);
	      myINSNSTR(form->w, &myBLANK, 1);
	      return E_OK;
	    }
	}
      return (result);
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Wrapping_Not_Necessary_Or_Wrapping_Ok(
|                                             FORM * form)
|
|   Description   :  If a character has been entered into a field, it may
|                    be that wrapping has to occur. This routine checks
|                    whether or not wrapping is required and if so, performs
|                    the wrapping.
|
|   Return Values :  E_OK              - no wrapping required or wrapping
|                                        was successful
|                    E_REQUEST_DENIED  -
|                    E_SYSTEM_ERROR    - some system error
+--------------------------------------------------------------------------*/
static int
Wrapping_Not_Necessary_Or_Wrapping_Ok(FORM *form)
{
  FIELD *field = form->current;
  int result = E_REQUEST_DENIED;
  bool Last_Row = ((field->drows - 1) == form->currow);

  if (((unsigned)field->opts & O_WRAP) &&	/* wrapping wanted     */
      (!Single_Line_Field(field)) &&	/* must be multi-line  */
      (There_Is_No_Room_For_A_Char_In_Line(form)) &&	/* line is full        */
      (!Last_Row || Growable(field)))	/* there are more lines */
    {
      FIELD_CELL *bp;
      FIELD_CELL *split;
      int chars_to_be_wrapped;
      int chars_to_remain_on_line;

      if (Last_Row)
	{
	  /* the above logic already ensures, that in this case the field
	     is growable */
	  if (!Field_Grown(field, 1))
	    return E_SYSTEM_ERROR;
	}
      bp = Address_Of_Current_Row_In_Buffer(form);
      Window_To_Buffer(form, field);
      split = After_Last_Whitespace_Character(bp, field->dcols);
      /* split points to the first character of the sequence to be brought
         on the next line */
      chars_to_remain_on_line = (int)(split - bp);
      chars_to_be_wrapped = field->dcols - chars_to_remain_on_line;
      if (chars_to_remain_on_line > 0)
	{
	  if ((result = Insert_String(form, form->currow + 1, split,
				      chars_to_be_wrapped)) == E_OK)
	    {
	      wmove(form->w, form->currow, chars_to_remain_on_line);
	      wclrtoeol(form->w);
	      if (form->curcol >= chars_to_remain_on_line)
		{
		  form->currow++;
		  form->curcol -= chars_to_remain_on_line;
		}
	      return E_OK;
	    }
	}
      else
	return E_OK;
      if (result != E_OK)
	{
	  DeleteChar(form);
	  Window_To_Buffer(form, field);
	  result = E_REQUEST_DENIED;
	}
    }
  else
    result = E_OK;		/* wrapping was not necessary */
  return (result);
}

/*----------------------------------------------------------------------------
  Field Editing routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Field_Editing(
|                                    int (* const fct) (FORM *),
|                                    FORM * form)
|
|   Description   :  Generic routine for field editing requests. The driver
|                    routines are only called for editable fields, the
|                    _WINDOW_MODIFIED flag is set if editing occurred.
|                    This is somewhat special due to the overload semantics
|                    of the NEW_LINE and DEL_PREV requests.
|
|   Return Values :  Error code from low level drivers.
+--------------------------------------------------------------------------*/
static int
Field_Editing(int (*const fct) (FORM *), FORM *form)
{
  int res = E_REQUEST_DENIED;

  /* We have to deal here with the specific case of the overloaded
     behavior of New_Line and Delete_Previous requests.
     They may end up in navigational requests if we are on the first
     character in a field. But navigation is also allowed on non-
     editable fields.
   */
  if ((fct == FE_Delete_Previous) &&
      ((unsigned)form->opts & O_BS_OVERLOAD) &&
      First_Position_In_Current_Field(form))
    {
      res = Inter_Field_Navigation(FN_Previous_Field, form);
    }
  else
    {
      if (fct == FE_New_Line)
	{
	  if (((unsigned)form->opts & O_NL_OVERLOAD) &&
	      First_Position_In_Current_Field(form))
	    {
	      res = Inter_Field_Navigation(FN_Next_Field, form);
	    }
	  else
	    /* FE_New_Line deals itself with the _WINDOW_MODIFIED flag */
	    res = fct(form);
	}
      else
	{
	  /* From now on, everything must be editable */
	  if ((unsigned)form->current->opts & O_EDIT)
	    {
	      res = fct(form);
	      if (res == E_OK)
		SetStatus(form, _WINDOW_MODIFIED);
	    }
	}
    }
  return res;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_New_Line(FORM * form)
|
|   Description   :  Perform a new line request. This is rather complex
|                    compared to other routines in this code due to the
|                    rather difficult to understand description in the
|                    manuals.
|
|   Return Values :  E_OK               - success
|                    E_REQUEST_DENIED   - new line not allowed
|                    E_SYSTEM_ERROR     - system error
+--------------------------------------------------------------------------*/
static int
FE_New_Line(FORM *form)
{
  FIELD *field = form->current;
  FIELD_CELL *bp, *t;
  bool Last_Row = ((field->drows - 1) == form->currow);

  T((T_CALLED("FE_New_Line(%p)"), (void *)form));
  if (form->status & _OVLMODE)
    {
      if (Last_Row &&
	  (!(Growable(field) && !Single_Line_Field(field))))
	{
	  if (!((unsigned)form->opts & O_NL_OVERLOAD))
	    returnCode(E_REQUEST_DENIED);
	  wmove(form->w, form->currow, form->curcol);
	  wclrtoeol(form->w);
	  /* we have to set this here, although it is also
	     handled in the generic routine. The reason is,
	     that FN_Next_Field may fail, but the form is
	     definitively changed */
	  SetStatus(form, _WINDOW_MODIFIED);
	  returnCode(Inter_Field_Navigation(FN_Next_Field, form));
	}
      else
	{
	  if (Last_Row && !Field_Grown(field, 1))
	    {
	      /* N.B.: due to the logic in the 'if', LastRow==TRUE
	         means here that the field is growable and not
	         a single-line field */
	      returnCode(E_SYSTEM_ERROR);
	    }
	  wmove(form->w, form->currow, form->curcol);
	  wclrtoeol(form->w);
	  form->currow++;
	  form->curcol = 0;
	  SetStatus(form, _WINDOW_MODIFIED);
	  returnCode(E_OK);
	}
    }
  else
    {
      /* Insert Mode */
      if (Last_Row &&
	  !(Growable(field) && !Single_Line_Field(field)))
	{
	  if (!((unsigned)form->opts & O_NL_OVERLOAD))
	    returnCode(E_REQUEST_DENIED);
	  returnCode(Inter_Field_Navigation(FN_Next_Field, form));
	}
      else
	{
	  bool May_Do_It = !Last_Row && Is_There_Room_For_A_Line(form);

	  if (!(May_Do_It || Growable(field)))
	    returnCode(E_REQUEST_DENIED);
	  if (!May_Do_It && !Field_Grown(field, 1))
	    returnCode(E_SYSTEM_ERROR);

	  bp = Address_Of_Current_Position_In_Buffer(form);
	  t = After_End_Of_Data(bp, field->dcols - form->curcol);
	  wmove(form->w, form->currow, form->curcol);
	  wclrtoeol(form->w);
	  form->currow++;
	  form->curcol = 0;
	  wmove(form->w, form->currow, form->curcol);
	  winsertln(form->w);
	  myADDNSTR(form->w, bp, (int)(t - bp));
	  SetStatus(form, _WINDOW_MODIFIED);
	  returnCode(E_OK);
	}
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Insert_Character(FORM * form)
|
|   Description   :  Insert blank character at the cursor position
|
|   Return Values :  E_OK
|                    E_REQUEST_DENIED
+--------------------------------------------------------------------------*/
static int
FE_Insert_Character(FORM *form)
{
  FIELD *field = form->current;
  int result = E_REQUEST_DENIED;

  T((T_CALLED("FE_Insert_Character(%p)"), (void *)form));
  if (Check_Char(form, field, field->type, (int)C_BLANK,
		 (TypeArgument *)(field->arg)))
    {
      bool There_Is_Room = Is_There_Room_For_A_Char_In_Line(form);

      if (There_Is_Room ||
	  ((Single_Line_Field(field) && Growable(field))))
	{
	  if (!There_Is_Room && !Field_Grown(field, 1))
	    result = E_SYSTEM_ERROR;
	  else
	    {
	      winsch(form->w, (chtype)C_BLANK);
	      result = Wrapping_Not_Necessary_Or_Wrapping_Ok(form);
	    }
	}
    }
  returnCode(result);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Insert_Line(FORM * form)
|
|   Description   :  Insert a blank line at the cursor position
|
|   Return Values :  E_OK               - success
|                    E_REQUEST_DENIED   - line can not be inserted
+--------------------------------------------------------------------------*/
static int
FE_Insert_Line(FORM *form)
{
  FIELD *field = form->current;
  int result = E_REQUEST_DENIED;

  T((T_CALLED("FE_Insert_Line(%p)"), (void *)form));
  if (Check_Char(form, field,
		 field->type, (int)C_BLANK, (TypeArgument *)(field->arg)))
    {
      bool Maybe_Done = (form->currow != (field->drows - 1)) &&
      Is_There_Room_For_A_Line(form);

      if (!Single_Line_Field(field) &&
	  (Maybe_Done || Growable(field)))
	{
	  if (!Maybe_Done && !Field_Grown(field, 1))
	    result = E_SYSTEM_ERROR;
	  else
	    {
	      form->curcol = 0;
	      winsertln(form->w);
	      result = E_OK;
	    }
	}
    }
  returnCode(result);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Delete_Character(FORM * form)
|
|   Description   :  Delete character at the cursor position
|
|   Return Values :  E_OK    - success
+--------------------------------------------------------------------------*/
static int
FE_Delete_Character(FORM *form)
{
  T((T_CALLED("FE_Delete_Character(%p)"), (void *)form));
  DeleteChar(form);
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Delete_Previous(FORM * form)
|
|   Description   :  Delete character before cursor. Again this is a rather
|                    difficult piece compared to others due to the overloading
|                    semantics of backspace.
|                    N.B.: The case of overloaded BS on first field position
|                          is already handled in the generic routine.
|
|   Return Values :  E_OK                - success
|                    E_REQUEST_DENIED    - Character can't be deleted
+--------------------------------------------------------------------------*/
static int
FE_Delete_Previous(FORM *form)
{
  FIELD *field = form->current;

  T((T_CALLED("FE_Delete_Previous(%p)"), (void *)form));
  if (First_Position_In_Current_Field(form))
    returnCode(E_REQUEST_DENIED);

  if ((--(form->curcol)) < 0)
    {
      FIELD_CELL *this_line, *prev_line, *prev_end, *this_end;
      int this_row = form->currow;

      form->curcol++;
      if (form->status & _OVLMODE)
	returnCode(E_REQUEST_DENIED);

      prev_line = Address_Of_Row_In_Buffer(field, (form->currow - 1));
      this_line = Address_Of_Row_In_Buffer(field, (form->currow));
      Synchronize_Buffer(form);
      prev_end = After_End_Of_Data(prev_line, field->dcols);
      this_end = After_End_Of_Data(this_line, field->dcols);
      if ((int)(this_end - this_line) >
	  (field->cols - (int)(prev_end - prev_line)))
	returnCode(E_REQUEST_DENIED);
      wmove(form->w, form->currow, form->curcol);
      wdeleteln(form->w);
      Adjust_Cursor_Position(form, prev_end);
      /*
       * If we did not really move to the previous line, help the user a
       * little.  It is however a little inconsistent.  Normally, when
       * backspacing around the point where text wraps to a new line in a
       * multi-line form, we absorb one keystroke for the wrapping point.  That
       * is consistent with SVr4 forms.  However, SVr4 does not allow typing
       * into the last column of the field, and requires the user to enter a
       * newline to move to the next line.  Therefore it can consistently eat
       * that keystroke.  Since ncurses allows the last column, it wraps
       * automatically (given the proper options).  But we cannot eat the
       * keystroke to back over the wrapping point, since that would put the
       * cursor past the end of the form field.  In this case, just delete the
       * character at the end of the field.
       */
      if (form->currow == this_row && this_row > 0)
	{
	  form->currow -= 1;
	  form->curcol = field->dcols - 1;
	  DeleteChar(form);
	}
      else
	{
	  wmove(form->w, form->currow, form->curcol);
	  myADDNSTR(form->w, this_line, (int)(this_end - this_line));
	}
    }
  else
    {
      DeleteChar(form);
    }
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Delete_Line(FORM * form)
|
|   Description   :  Delete line at cursor position.
|
|   Return Values :  E_OK  - success
+--------------------------------------------------------------------------*/
static int
FE_Delete_Line(FORM *form)
{
  T((T_CALLED("FE_Delete_Line(%p)"), (void *)form));
  form->curcol = 0;
  wdeleteln(form->w);
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Delete_Word(FORM * form)
|
|   Description   :  Delete word at cursor position
|
|   Return Values :  E_OK               - success
|                    E_REQUEST_DENIED   - failure
+--------------------------------------------------------------------------*/
static int
FE_Delete_Word(FORM *form)
{
  FIELD *field = form->current;
  FIELD_CELL *bp = Address_Of_Current_Row_In_Buffer(form);
  FIELD_CELL *ep = bp + field->dcols;
  FIELD_CELL *cp = bp + form->curcol;
  FIELD_CELL *s;

  T((T_CALLED("FE_Delete_Word(%p)"), (void *)form));
  Synchronize_Buffer(form);
  if (ISBLANK(*cp))
    returnCode(E_REQUEST_DENIED);	/* not in word */

  /* move cursor to begin of word and erase to end of screen-line */
  Adjust_Cursor_Position(form,
			 After_Last_Whitespace_Character(bp, form->curcol));
  wmove(form->w, form->currow, form->curcol);
  wclrtoeol(form->w);

  /* skip over word in buffer */
  s = Get_First_Whitespace_Character(cp, (int)(ep - cp));
  /* to begin of next word    */
  s = Get_Start_Of_Data(s, (int)(ep - s));
  if ((s != cp) && !ISBLANK(*s))
    {
      /* copy remaining line to window */
      myADDNSTR(form->w, s, (int)(s - After_End_Of_Data(s, (int)(ep - s))));
    }
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Clear_To_End_Of_Line(FORM * form)
|
|   Description   :  Clear to end of current line.
|
|   Return Values :  E_OK   - success
+--------------------------------------------------------------------------*/
static int
FE_Clear_To_End_Of_Line(FORM *form)
{
  T((T_CALLED("FE_Clear_To_End_Of_Line(%p)"), (void *)form));
  wmove(form->w, form->currow, form->curcol);
  wclrtoeol(form->w);
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Clear_To_End_Of_Field(FORM * form)
|
|   Description   :  Clear to end of field.
|
|   Return Values :  E_OK   - success
+--------------------------------------------------------------------------*/
static int
FE_Clear_To_End_Of_Field(FORM *form)
{
  T((T_CALLED("FE_Clear_To_End_Of_Field(%p)"), (void *)form));
  wmove(form->w, form->currow, form->curcol);
  wclrtobot(form->w);
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FE_Clear_Field(FORM * form)
|
|   Description   :  Clear entire field.
|
|   Return Values :  E_OK   - success
+--------------------------------------------------------------------------*/
static int
FE_Clear_Field(FORM *form)
{
  T((T_CALLED("FE_Clear_Field(%p)"), (void *)form));
  form->currow = form->curcol = 0;
  werase(form->w);
  returnCode(E_OK);
}
/*----------------------------------------------------------------------------
  END of Field Editing routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Edit Mode routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int EM_Overlay_Mode(FORM * form)
|
|   Description   :  Switch to overlay mode.
|
|   Return Values :  E_OK   - success
+--------------------------------------------------------------------------*/
static int
EM_Overlay_Mode(FORM *form)
{
  T((T_CALLED("EM_Overlay_Mode(%p)"), (void *)form));
  SetStatus(form, _OVLMODE);
  returnCode(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int EM_Insert_Mode(FORM * form)
|
|   Description   :  Switch to insert mode
|
|   Return Values :  E_OK   - success
+--------------------------------------------------------------------------*/
static int
EM_Insert_Mode(FORM *form)
{
  T((T_CALLED("EM_Insert_Mode(%p)"), (void *)form));
  ClrStatus(form, _OVLMODE);
  returnCode(E_OK);
}

/*----------------------------------------------------------------------------
  END of Edit Mode routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Helper routines for Choice Requests
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static bool Next_Choice(FORM * form,
|                                            FIELDTYPE * typ,
|                                            FIELD * field,
|                                            TypeArgument *argp)
|
|   Description   :  Get the next field choice. For linked types this is
|                    done recursively.
|
|   Return Values :  TRUE    - next choice successfully retrieved
|                    FALSE   - couldn't retrieve next choice
+--------------------------------------------------------------------------*/
static bool
Next_Choice(FORM *form, FIELDTYPE *typ, FIELD *field, TypeArgument *argp)
{
  if (!typ || !(typ->status & _HAS_CHOICE))
    return FALSE;

  if (typ->status & _LINKED_TYPE)
    {
      assert(argp);
      return (
	       Next_Choice(form, typ->left, field, argp->left) ||
	       Next_Choice(form, typ->right, field, argp->right));
    }
  else
    {
#if NCURSES_INTEROP_FUNCS
      assert(typ->enum_next.onext);
      if (typ->status & _GENERIC)
	return typ->enum_next.gnext(form, field, (void *)argp);
      else
	return typ->enum_next.onext(field, (void *)argp);
#else
      assert(typ->next);
      return typ->next(field, (void *)argp);
#endif
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static bool Previous_Choice(FORM * form,
|                                                FIELDTYPE * typ,
|                                                FIELD * field,
|                                                TypeArgument *argp)
|
|   Description   :  Get the previous field choice. For linked types this
|                    is done recursively.
|
|   Return Values :  TRUE    - previous choice successfully retrieved
|                    FALSE   - couldn't retrieve previous choice
+--------------------------------------------------------------------------*/
static bool
Previous_Choice(FORM *form, FIELDTYPE *typ, FIELD *field, TypeArgument *argp)
{
  if (!typ || !(typ->status & _HAS_CHOICE))
    return FALSE;

  if (typ->status & _LINKED_TYPE)
    {
      assert(argp);
      return (
	       Previous_Choice(form, typ->left, field, argp->left) ||
	       Previous_Choice(form, typ->right, field, argp->right));
    }
  else
    {
#if NCURSES_INTEROP_FUNCS
      assert(typ->enum_prev.oprev);
      if (typ->status & _GENERIC)
	return typ->enum_prev.gprev(form, field, (void *)argp);
      else
	return typ->enum_prev.oprev(field, (void *)argp);
#else
      assert(typ->prev);
      return typ->prev(field, (void *)argp);
#endif
    }
}
/*----------------------------------------------------------------------------
  End of Helper routines for Choice Requests
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Routines for Choice Requests
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int CR_Next_Choice(FORM * form)
|
|   Description   :  Get the next field choice.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - next choice couldn't be retrieved
+--------------------------------------------------------------------------*/
static int
CR_Next_Choice(FORM *form)
{
  FIELD *field = form->current;

  T((T_CALLED("CR_Next_Choice(%p)"), (void *)form));
  Synchronize_Buffer(form);
  returnCode((Next_Choice(form, field->type, field, (TypeArgument *)(field->arg)))
	     ? E_OK
	     : E_REQUEST_DENIED);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int CR_Previous_Choice(FORM * form)
|
|   Description   :  Get the previous field choice.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - prev. choice couldn't be retrieved
+--------------------------------------------------------------------------*/
static int
CR_Previous_Choice(FORM *form)
{
  FIELD *field = form->current;

  T((T_CALLED("CR_Previous_Choice(%p)"), (void *)form));
  Synchronize_Buffer(form);
  returnCode((Previous_Choice(form, field->type, field, (TypeArgument *)(field->arg)))
	     ? E_OK
	     : E_REQUEST_DENIED);
}
/*----------------------------------------------------------------------------
  End of Routines for Choice Requests
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Helper routines for Field Validations.
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static bool Check_Field(FORM* form,
|                                            FIELDTYPE * typ,
|                                            FIELD * field,
|                                            TypeArgument * argp)
|
|   Description   :  Check the field according to its fieldtype and its
|                    actual arguments. For linked fieldtypes this is done
|                    recursively.
|
|   Return Values :  TRUE       - field is valid
|                    FALSE      - field is invalid.
+--------------------------------------------------------------------------*/
static bool
Check_Field(FORM *form, FIELDTYPE *typ, FIELD *field, TypeArgument *argp)
{
  if (typ)
    {
      if ((unsigned)field->opts & O_NULLOK)
	{
	  FIELD_CELL *bp = field->buf;

	  assert(bp);
	  while (ISBLANK(*bp))
	    {
	      bp++;
	    }
	  if (CharOf(*bp) == 0)
	    return TRUE;
	}

      if (typ->status & _LINKED_TYPE)
	{
	  assert(argp);
	  return (
		   Check_Field(form, typ->left, field, argp->left) ||
		   Check_Field(form, typ->right, field, argp->right));
	}
      else
	{
#if NCURSES_INTEROP_FUNCS
	  if (typ->fieldcheck.ofcheck)
	    {
	      if (typ->status & _GENERIC)
		return typ->fieldcheck.gfcheck(form, field, (void *)argp);
	      else
		return typ->fieldcheck.ofcheck(field, (void *)argp);
	    }
#else
	  if (typ->fcheck)
	    return typ->fcheck(field, (void *)argp);
#endif
	}
    }
  return TRUE;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  bool _nc_Internal_Validation(FORM * form )
|
|   Description   :  Validate the current field of the form.
|
|   Return Values :  TRUE  - field is valid
|                    FALSE - field is invalid
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(bool)
_nc_Internal_Validation(FORM *form)
{
  FIELD *field;

  field = form->current;

  Synchronize_Buffer(form);
  if ((form->status & _FCHECK_REQUIRED) ||
      (!((unsigned)field->opts & O_PASSOK)))
    {
      if (!Check_Field(form, field->type, field, (TypeArgument *)(field->arg)))
	return FALSE;
      ClrStatus(form, _FCHECK_REQUIRED);
      SetStatus(field, _CHANGED);
      Synchronize_Linked_Fields(field);
    }
  return TRUE;
}
/*----------------------------------------------------------------------------
  End of Helper routines for Field Validations.
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Routines for Field Validation.
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FV_Validation(FORM * form)
|
|   Description   :  Validate the current field of the form.
|
|   Return Values :  E_OK             - field valid
|                    E_INVALID_FIELD  - field not valid
+--------------------------------------------------------------------------*/
static int
FV_Validation(FORM *form)
{
  T((T_CALLED("FV_Validation(%p)"), (void *)form));
  if (_nc_Internal_Validation(form))
    returnCode(E_OK);
  else
    returnCode(E_INVALID_FIELD);
}
/*----------------------------------------------------------------------------
  End of routines for Field Validation.
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Helper routines for Inter-Field Navigation
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static FIELD *Next_Field_On_Page(FIELD * field)
|
|   Description   :  Get the next field after the given field on the current
|                    page. The order of fields is the one defined by the
|                    fields array. Only visible and active fields are
|                    counted.
|
|   Return Values :  Pointer to the next field.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD *
Next_Field_On_Page(FIELD *field)
{
  FORM *form = field->form;
  FIELD **field_on_page = &form->field[field->index];
  FIELD **first_on_page = &form->field[form->page[form->curpage].pmin];
  FIELD **last_on_page = &form->field[form->page[form->curpage].pmax];

  do
    {
      field_on_page =
	(field_on_page == last_on_page) ? first_on_page : field_on_page + 1;
      if (Field_Is_Selectable(*field_on_page))
	break;
    }
  while (field != (*field_on_page));
  return (*field_on_page);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  FIELD* _nc_First_Active_Field(FORM * form)
|
|   Description   :  Get the first active field on the current page,
|                    if there are such. If there are none, get the first
|                    visible field on the page. If there are also none,
|                    we return the first field on page and hope the best.
|
|   Return Values :  Pointer to calculated field.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(FIELD *)
_nc_First_Active_Field(FORM *form)
{
  FIELD **last_on_page = &form->field[form->page[form->curpage].pmax];
  FIELD *proposed = Next_Field_On_Page(*last_on_page);

  if (proposed == *last_on_page)
    {
      /* there might be the special situation, where there is no
         active and visible field on the current page. We then select
         the first visible field on this readonly page
       */
      if (Field_Is_Not_Selectable(proposed))
	{
	  FIELD **field = &form->field[proposed->index];
	  FIELD **first = &form->field[form->page[form->curpage].pmin];

	  do
	    {
	      field = (field == last_on_page) ? first : field + 1;
	      if (((unsigned)(*field)->opts & O_VISIBLE))
		break;
	    }
	  while (proposed != (*field));

	  proposed = *field;

	  if ((proposed == *last_on_page) &&
	      !((unsigned)proposed->opts & O_VISIBLE))
	    {
	      /* This means, there is also no visible field on the page.
	         So we propose the first one and hope the very best...
	         Some very clever user has designed a readonly and invisible
	         page on this form.
	       */
	      proposed = *first;
	    }
	}
    }
  return (proposed);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static FIELD *Previous_Field_On_Page(FIELD * field)
|
|   Description   :  Get the previous field before the given field on the
|                    current page. The order of fields is the one defined by
|                    the fields array. Only visible and active fields are
|                    counted.
|
|   Return Values :  Pointer to the previous field.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD *
Previous_Field_On_Page(FIELD *field)
{
  FORM *form = field->form;
  FIELD **field_on_page = &form->field[field->index];
  FIELD **first_on_page = &form->field[form->page[form->curpage].pmin];
  FIELD **last_on_page = &form->field[form->page[form->curpage].pmax];

  do
    {
      field_on_page =
	(field_on_page == first_on_page) ? last_on_page : field_on_page - 1;
      if (Field_Is_Selectable(*field_on_page))
	break;
    }
  while (field != (*field_on_page));

  return (*field_on_page);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static FIELD *Sorted_Next_Field(FIELD * field)
|
|   Description   :  Get the next field after the given field on the current
|                    page. The order of fields is the one defined by the
|                    (row,column) geometry, rows are major.
|
|   Return Values :  Pointer to the next field.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD *
Sorted_Next_Field(FIELD *field)
{
  FIELD *field_on_page = field;

  do
    {
      field_on_page = field_on_page->snext;
      if (Field_Is_Selectable(field_on_page))
	break;
    }
  while (field_on_page != field);

  return (field_on_page);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static FIELD *Sorted_Previous_Field(FIELD * field)
|
|   Description   :  Get the previous field before the given field on the
|                    current page. The order of fields is the one defined
|                    by the (row,column) geometry, rows are major.
|
|   Return Values :  Pointer to the previous field.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD *
Sorted_Previous_Field(FIELD *field)
{
  FIELD *field_on_page = field;

  do
    {
      field_on_page = field_on_page->sprev;
      if (Field_Is_Selectable(field_on_page))
	break;
    }
  while (field_on_page != field);

  return (field_on_page);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static FIELD *Left_Neighbor_Field(FIELD * field)
|
|   Description   :  Get the left neighbor of the field on the same line
|                    and the same page. Cycles through the line.
|
|   Return Values :  Pointer to left neighbor field.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD *
Left_Neighbor_Field(FIELD *field)
{
  FIELD *field_on_page = field;

  /* For a field that has really a left neighbor, the while clause
     immediately fails and the loop is left, positioned at the right
     neighbor. Otherwise we cycle backwards through the sorted field list
     until we enter the same line (from the right end).
   */
  do
    {
      field_on_page = Sorted_Previous_Field(field_on_page);
    }
  while (field_on_page->frow != field->frow);

  return (field_on_page);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static FIELD *Right_Neighbor_Field(FIELD * field)
|
|   Description   :  Get the right neighbor of the field on the same line
|                    and the same page.
|
|   Return Values :  Pointer to right neighbor field.
+--------------------------------------------------------------------------*/
NCURSES_INLINE static FIELD *
Right_Neighbor_Field(FIELD *field)
{
  FIELD *field_on_page = field;

  /* See the comments on Left_Neighbor_Field to understand how it works */
  do
    {
      field_on_page = Sorted_Next_Field(field_on_page);
    }
  while (field_on_page->frow != field->frow);

  return (field_on_page);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static FIELD *Upper_Neighbor_Field(FIELD * field)
|
|   Description   :  Because of the row-major nature of sorting the fields,
|                    it is more difficult to define whats the upper neighbor
|                    field really means. We define that it must be on a
|                    'previous' line (cyclic order!) and is the rightmost
|                    field laying on the left side of the given field. If
|                    this set is empty, we take the first field on the line.
|
|   Return Values :  Pointer to the upper neighbor field.
+--------------------------------------------------------------------------*/
static FIELD *
Upper_Neighbor_Field(FIELD *field)
{
  FIELD *field_on_page = field;
  int frow = field->frow;
  int fcol = field->fcol;

  /* Walk back to the 'previous' line. The second term in the while clause
     just guarantees that we stop if we cycled through the line because
     there might be no 'previous' line if the page has just one line.
   */
  do
    {
      field_on_page = Sorted_Previous_Field(field_on_page);
    }
  while (field_on_page->frow == frow && field_on_page->fcol != fcol);

  if (field_on_page->frow != frow)
    {
      /* We really found a 'previous' line. We are positioned at the
         rightmost field on this line */
      frow = field_on_page->frow;

      /* We walk to the left as long as we are really right of the
         field. */
      while (field_on_page->frow == frow && field_on_page->fcol > fcol)
	field_on_page = Sorted_Previous_Field(field_on_page);

      /* If we wrapped, just go to the right which is the first field on
         the row */
      if (field_on_page->frow != frow)
	field_on_page = Sorted_Next_Field(field_on_page);
    }

  return (field_on_page);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static FIELD *Down_Neighbor_Field(FIELD * field)
|
|   Description   :  Because of the row-major nature of sorting the fields,
|                    its more difficult to define whats the down neighbor
|                    field really means. We define that it must be on a
|                    'next' line (cyclic order!) and is the leftmost
|                    field laying on the right side of the given field. If
|                    this set is empty, we take the last field on the line.
|
|   Return Values :  Pointer to the upper neighbor field.
+--------------------------------------------------------------------------*/
static FIELD *
Down_Neighbor_Field(FIELD *field)
{
  FIELD *field_on_page = field;
  int frow = field->frow;
  int fcol = field->fcol;

  /* Walk forward to the 'next' line. The second term in the while clause
     just guarantees that we stop if we cycled through the line because
     there might be no 'next' line if the page has just one line.
   */
  do
    {
      field_on_page = Sorted_Next_Field(field_on_page);
    }
  while (field_on_page->frow == frow && field_on_page->fcol != fcol);

  if (field_on_page->frow != frow)
    {
      /* We really found a 'next' line. We are positioned at the rightmost
         field on this line */
      frow = field_on_page->frow;

      /* We walk to the right as long as we are really left of the
         field. */
      while (field_on_page->frow == frow && field_on_page->fcol < fcol)
	field_on_page = Sorted_Next_Field(field_on_page);

      /* If we wrapped, just go to the left which is the last field on
         the row */
      if (field_on_page->frow != frow)
	field_on_page = Sorted_Previous_Field(field_on_page);
    }

  return (field_on_page);
}

/*----------------------------------------------------------------------------
  Inter-Field Navigation routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Inter_Field_Navigation(
|                                           int (* const fct) (FORM *),
|                                           FORM * form)
|
|   Description   :  Generic behavior for changing the current field, the
|                    field is left and a new field is entered. So the field
|                    must be validated and the field init/term hooks must
|                    be called.
|
|   Return Values :  E_OK                - success
|                    E_INVALID_FIELD     - field is invalid
|                    some other          - error from subordinate call
+--------------------------------------------------------------------------*/
static int
Inter_Field_Navigation(int (*const fct) (FORM *), FORM *form)
{
  int res;

  if (!_nc_Internal_Validation(form))
    res = E_INVALID_FIELD;
  else
    {
      Call_Hook(form, fieldterm);
      res = fct(form);
      Call_Hook(form, fieldinit);
    }
  return res;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Next_Field(FORM * form)
|
|   Description   :  Move to the next field on the current page of the form
|
|   Return Values :  E_OK                 - success
|                    != E_OK              - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Next_Field(FORM *form)
{
  T((T_CALLED("FN_Next_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Next_Field_On_Page(form->current)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Previous_Field(FORM * form)
|
|   Description   :  Move to the previous field on the current page of the
|                    form
|
|   Return Values :  E_OK                 - success
|                    != E_OK              - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Previous_Field(FORM *form)
{
  T((T_CALLED("FN_Previous_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Previous_Field_On_Page(form->current)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_First_Field(FORM * form)
|
|   Description   :  Move to the first field on the current page of the form
|
|   Return Values :  E_OK                 - success
|                    != E_OK              - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_First_Field(FORM *form)
{
  T((T_CALLED("FN_First_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Next_Field_On_Page(form->field[form->page[form->curpage].pmax])));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Last_Field(FORM * form)
|
|   Description   :  Move to the last field on the current page of the form
|
|   Return Values :  E_OK                 - success
|                    != E_OK              - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Last_Field(FORM *form)
{
  T((T_CALLED("FN_Last_Field(%p)"), (void *)form));
  returnCode(
	      _nc_Set_Current_Field(form,
				    Previous_Field_On_Page(form->field[form->page[form->curpage].pmin])));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Sorted_Next_Field(FORM * form)
|
|   Description   :  Move to the sorted next field on the current page
|                    of the form.
|
|   Return Values :  E_OK            - success
|                    != E_OK         - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Sorted_Next_Field(FORM *form)
{
  T((T_CALLED("FN_Sorted_Next_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Sorted_Next_Field(form->current)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Sorted_Previous_Field(FORM * form)
|
|   Description   :  Move to the sorted previous field on the current page
|                    of the form.
|
|   Return Values :  E_OK            - success
|                    != E_OK         - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Sorted_Previous_Field(FORM *form)
{
  T((T_CALLED("FN_Sorted_Previous_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Sorted_Previous_Field(form->current)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Sorted_First_Field(FORM * form)
|
|   Description   :  Move to the sorted first field on the current page
|                    of the form.
|
|   Return Values :  E_OK            - success
|                    != E_OK         - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Sorted_First_Field(FORM *form)
{
  T((T_CALLED("FN_Sorted_First_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Sorted_Next_Field(form->field[form->page[form->curpage].smax])));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Sorted_Last_Field(FORM * form)
|
|   Description   :  Move to the sorted last field on the current page
|                    of the form.
|
|   Return Values :  E_OK            - success
|                    != E_OK         - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Sorted_Last_Field(FORM *form)
{
  T((T_CALLED("FN_Sorted_Last_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Sorted_Previous_Field(form->field[form->page[form->curpage].smin])));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Left_Field(FORM * form)
|
|   Description   :  Get the field on the left of the current field on the
|                    same line and the same page. Cycles through the line.
|
|   Return Values :  E_OK            - success
|                    != E_OK         - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Left_Field(FORM *form)
{
  T((T_CALLED("FN_Left_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Left_Neighbor_Field(form->current)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Right_Field(FORM * form)
|
|   Description   :  Get the field on the right of the current field on the
|                    same line and the same page. Cycles through the line.
|
|   Return Values :  E_OK            - success
|                    != E_OK         - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Right_Field(FORM *form)
{
  T((T_CALLED("FN_Right_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Right_Neighbor_Field(form->current)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Up_Field(FORM * form)
|
|   Description   :  Get the upper neighbor of the current field. This
|                    cycles through the page. See the comments of the
|                    Upper_Neighbor_Field function to understand how
|                    'upper' is defined.
|
|   Return Values :  E_OK            - success
|                    != E_OK         - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Up_Field(FORM *form)
{
  T((T_CALLED("FN_Up_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Upper_Neighbor_Field(form->current)));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int FN_Down_Field(FORM * form)
|
|   Description   :  Get the down neighbor of the current field. This
|                    cycles through the page. See the comments of the
|                    Down_Neighbor_Field function to understand how
|                    'down' is defined.
|
|   Return Values :  E_OK            - success
|                    != E_OK         - error from subordinate call
+--------------------------------------------------------------------------*/
static int
FN_Down_Field(FORM *form)
{
  T((T_CALLED("FN_Down_Field(%p)"), (void *)form));
  returnCode(_nc_Set_Current_Field(form,
				   Down_Neighbor_Field(form->current)));
}
/*----------------------------------------------------------------------------
  END of Field Navigation routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Helper routines for Page Navigation
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int _nc_Set_Form_Page(FORM * form,
|                                          int page,
|                                          FIELD * field)
|
|   Description   :  Make the given page number the current page and make
|                    the given field the current field on the page. If
|                    for the field NULL is given, make the first field on
|                    the page the current field. The routine acts only
|                    if the requested page is not the current page.
|
|   Return Values :  E_OK                - success
|                    != E_OK             - error from subordinate call
|                    E_BAD_ARGUMENT      - invalid field pointer
|                    E_SYSTEM_ERROR      - some severe basic error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_Set_Form_Page(FORM *form, int page, FIELD *field)
{
  int res = E_OK;

  if ((form->curpage != page))
    {
      FIELD *last_field, *field_on_page;

      werase(Get_Form_Window(form));
      form->curpage = (short)page;
      last_field = field_on_page = form->field[form->page[page].smin];
      do
	{
	  if ((unsigned)field_on_page->opts & O_VISIBLE)
	    if ((res = Display_Field(field_on_page)) != E_OK)
	      return (res);
	  field_on_page = field_on_page->snext;
	}
      while (field_on_page != last_field);

      if (field)
	res = _nc_Set_Current_Field(form, field);
      else
	/* N.B.: we don't encapsulate this by Inter_Field_Navigation(),
	   because this is already executed in a page navigation
	   context that contains field navigation
	 */
	res = FN_First_Field(form);
    }
  return (res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Next_Page_Number(const FORM * form)
|
|   Description   :  Calculate the page number following the current page
|                    number. This cycles if the highest page number is
|                    reached.
|
|   Return Values :  The next page number
+--------------------------------------------------------------------------*/
NCURSES_INLINE static int
Next_Page_Number(const FORM *form)
{
  return (form->curpage + 1) % form->maxpage;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Previous_Page_Number(const FORM * form)
|
|   Description   :  Calculate the page number before the current page
|                    number. This cycles if the first page number is
|                    reached.
|
|   Return Values :  The previous page number
+--------------------------------------------------------------------------*/
NCURSES_INLINE static int
Previous_Page_Number(const FORM *form)
{
  return (form->curpage != 0 ? form->curpage - 1 : form->maxpage - 1);
}

/*----------------------------------------------------------------------------
  Page Navigation routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Page_Navigation(
|                                               int (* const fct) (FORM *),
|                                               FORM * form)
|
|   Description   :  Generic behavior for changing a page. This means
|                    that the field is left and a new field is entered.
|                    So the field must be validated and the field init/term
|                    hooks must be called. Because also the page is changed,
|                    the forms init/term hooks must be called also.
|
|   Return Values :  E_OK                - success
|                    E_INVALID_FIELD     - field is invalid
|                    some other          - error from subordinate call
+--------------------------------------------------------------------------*/
static int
Page_Navigation(int (*const fct) (FORM *), FORM *form)
{
  int res;

  if (!_nc_Internal_Validation(form))
    res = E_INVALID_FIELD;
  else
    {
      Call_Hook(form, fieldterm);
      Call_Hook(form, formterm);
      res = fct(form);
      Call_Hook(form, forminit);
      Call_Hook(form, fieldinit);
    }
  return res;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int PN_Next_Page(FORM * form)
|
|   Description   :  Move to the next page of the form
|
|   Return Values :  E_OK                - success
|                    != E_OK             - error from subordinate call
+--------------------------------------------------------------------------*/
static int
PN_Next_Page(FORM *form)
{
  T((T_CALLED("PN_Next_Page(%p)"), (void *)form));
  returnCode(_nc_Set_Form_Page(form, Next_Page_Number(form), (FIELD *)0));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int PN_Previous_Page(FORM * form)
|
|   Description   :  Move to the previous page of the form
|
|   Return Values :  E_OK              - success
|                    != E_OK           - error from subordinate call
+--------------------------------------------------------------------------*/
static int
PN_Previous_Page(FORM *form)
{
  T((T_CALLED("PN_Previous_Page(%p)"), (void *)form));
  returnCode(_nc_Set_Form_Page(form, Previous_Page_Number(form), (FIELD *)0));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int PN_First_Page(FORM * form)
|
|   Description   :  Move to the first page of the form
|
|   Return Values :  E_OK              - success
|                    != E_OK           - error from subordinate call
+--------------------------------------------------------------------------*/
static int
PN_First_Page(FORM *form)
{
  T((T_CALLED("PN_First_Page(%p)"), (void *)form));
  returnCode(_nc_Set_Form_Page(form, 0, (FIELD *)0));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int PN_Last_Page(FORM * form)
|
|   Description   :  Move to the last page of the form
|
|   Return Values :  E_OK              - success
|                    != E_OK           - error from subordinate call
+--------------------------------------------------------------------------*/
static int
PN_Last_Page(FORM *form)
{
  T((T_CALLED("PN_Last_Page(%p)"), (void *)form));
  returnCode(_nc_Set_Form_Page(form, form->maxpage - 1, (FIELD *)0));
}

/*----------------------------------------------------------------------------
  END of Field Navigation routines
  --------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Helper routines for the core form driver.
  --------------------------------------------------------------------------*/

# if USE_WIDEC_SUPPORT
/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Data_Entry_w(FORM * form, wchar_t c)
|
|   Description   :  Enter the wide character c into at the current
|                    position of the current field of the form.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - driver could not process the request
|                    E_SYSTEM_ERROR    -
+--------------------------------------------------------------------------*/
static int
Data_Entry_w(FORM *form, wchar_t c)
{
  FIELD *field = form->current;
  int result = E_REQUEST_DENIED;

  T((T_CALLED("Data_Entry(%p,%s)"), (void *)form, _tracechtype((chtype)c)));
  if (((unsigned)field->opts & O_EDIT)
#if FIX_FORM_INACTIVE_BUG
      && ((unsigned)field->opts & O_ACTIVE)
#endif
    )
    {
      wchar_t given[2];
      cchar_t temp_ch;

      given[0] = c;
      given[1] = 1;
      setcchar(&temp_ch, given, 0, 0, (void *)0);
      if (((unsigned)field->opts & O_BLANK) &&
	  First_Position_In_Current_Field(form) &&
	  !(form->status & _FCHECK_REQUIRED) &&
	  !(form->status & _WINDOW_MODIFIED))
	werase(form->w);

      if (form->status & _OVLMODE)
	{
	  wadd_wch(form->w, &temp_ch);
	}
      else
	/* no _OVLMODE */
	{
	  bool There_Is_Room = Is_There_Room_For_A_Char_In_Line(form);

	  if (!(There_Is_Room ||
		((Single_Line_Field(field) && Growable(field)))))
	    RETURN(E_REQUEST_DENIED);

	  if (!There_Is_Room && !Field_Grown(field, 1))
	    RETURN(E_SYSTEM_ERROR);

	  wins_wch(form->w, &temp_ch);
	}

      if ((result = Wrapping_Not_Necessary_Or_Wrapping_Ok(form)) == E_OK)
	{
	  bool End_Of_Field = (((field->drows - 1) == form->currow) &&
			       ((field->dcols - 1) == form->curcol));

	  form->status |= _WINDOW_MODIFIED;
	  if (End_Of_Field && !Growable(field) && ((unsigned)field->opts & O_AUTOSKIP))
	    result = Inter_Field_Navigation(FN_Next_Field, form);
	  else
	    {
	      if (End_Of_Field && Growable(field) && !Field_Grown(field, 1))
		result = E_SYSTEM_ERROR;
	      else
		{
		  /*
		   * We have just added a byte to the form field.  It may have
		   * been part of a multibyte character.  If it was, the
		   * addch_used field is nonzero and we should not try to move
		   * to a new column.
		   */
		  if (WINDOW_EXT(form->w, addch_used) == 0)
		    IFN_Next_Character(form);

		  result = E_OK;
		}
	    }
	}
    }
  RETURN(result);
}
# endif

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static int Data_Entry(FORM * form,int c)
|
|   Description   :  Enter character c into at the current position of the
|                    current field of the form.
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - driver could not process the request
|                    E_SYSTEM_ERROR    -
+--------------------------------------------------------------------------*/
static int
Data_Entry(FORM *form, int c)
{
  FIELD *field = form->current;
  int result = E_REQUEST_DENIED;

  T((T_CALLED("Data_Entry(%p,%s)"), (void *)form, _tracechtype((chtype)c)));
  if (((unsigned)field->opts & O_EDIT)
#if FIX_FORM_INACTIVE_BUG
      && ((unsigned)field->opts & O_ACTIVE)
#endif
    )
    {
      if (((unsigned)field->opts & O_BLANK) &&
	  First_Position_In_Current_Field(form) &&
	  !(form->status & _FCHECK_REQUIRED) &&
	  !(form->status & _WINDOW_MODIFIED))
	werase(form->w);

      if (form->status & _OVLMODE)
	{
	  waddch(form->w, (chtype)c);
	}
      else
	/* no _OVLMODE */
	{
	  bool There_Is_Room = Is_There_Room_For_A_Char_In_Line(form);

	  if (!(There_Is_Room ||
		((Single_Line_Field(field) && Growable(field)))))
	    RETURN(E_REQUEST_DENIED);

	  if (!There_Is_Room && !Field_Grown(field, 1))
	    RETURN(E_SYSTEM_ERROR);

	  winsch(form->w, (chtype)c);
	}

      if ((result = Wrapping_Not_Necessary_Or_Wrapping_Ok(form)) == E_OK)
	{
	  bool End_Of_Field = (((field->drows - 1) == form->currow) &&
			       ((field->dcols - 1) == form->curcol));

	  SetStatus(form, _WINDOW_MODIFIED);
	  if (End_Of_Field && !Growable(field) && ((unsigned)field->opts & O_AUTOSKIP))
	    result = Inter_Field_Navigation(FN_Next_Field, form);
	  else
	    {
	      if (End_Of_Field && Growable(field) && !Field_Grown(field, 1))
		result = E_SYSTEM_ERROR;
	      else
		{
#if USE_WIDEC_SUPPORT
		  /*
		   * We have just added a byte to the form field.  It may have
		   * been part of a multibyte character.  If it was, the
		   * addch_used field is nonzero and we should not try to move
		   * to a new column.
		   */
		  if (WINDOW_EXT(form->w, addch_used) == 0)
		    IFN_Next_Character(form);
#else
		  IFN_Next_Character(form);
#endif
		  result = E_OK;
		}
	    }
	}
    }
  RETURN(result);
}

/* Structure to describe the binding of a request code to a function.
   The member keycode codes the request value as well as the generic
   routine to use for the request. The code for the generic routine
   is coded in the upper 16 Bits while the request code is coded in
   the lower 16 bits.

   In terms of C++ you might think of a request as a class with a
   virtual method "perform". The different types of request are
   derived from this base class and overload (or not) the base class
   implementation of perform.
*/
typedef struct
{
  int keycode;			/* must be at least 32 bit: hi:mode, lo: key */
  int (*cmd) (FORM *);		/* low level driver routine for this key     */
}
Binding_Info;

/* You may see this is the class-id of the request type class */
#define ID_PN    (0x00000000)	/* Page navigation           */
#define ID_FN    (0x00010000)	/* Inter-Field navigation    */
#define ID_IFN   (0x00020000)	/* Intra-Field navigation    */
#define ID_VSC   (0x00030000)	/* Vertical Scrolling        */
#define ID_HSC   (0x00040000)	/* Horizontal Scrolling      */
#define ID_FE    (0x00050000)	/* Field Editing             */
#define ID_EM    (0x00060000)	/* Edit Mode                 */
#define ID_FV    (0x00070000)	/* Field Validation          */
#define ID_CH    (0x00080000)	/* Choice                    */
#define ID_Mask  (0xffff0000)
#define Key_Mask (0x0000ffff)
#define ID_Shft  (16)

/* This array holds all the Binding Infos */
/* *INDENT-OFF* */
static const Binding_Info bindings[MAX_FORM_COMMAND - MIN_FORM_COMMAND + 1] =
{
  { REQ_NEXT_PAGE    |ID_PN  ,PN_Next_Page},
  { REQ_PREV_PAGE    |ID_PN  ,PN_Previous_Page},
  { REQ_FIRST_PAGE   |ID_PN  ,PN_First_Page},
  { REQ_LAST_PAGE    |ID_PN  ,PN_Last_Page},

  { REQ_NEXT_FIELD   |ID_FN  ,FN_Next_Field},
  { REQ_PREV_FIELD   |ID_FN  ,FN_Previous_Field},
  { REQ_FIRST_FIELD  |ID_FN  ,FN_First_Field},
  { REQ_LAST_FIELD   |ID_FN  ,FN_Last_Field},
  { REQ_SNEXT_FIELD  |ID_FN  ,FN_Sorted_Next_Field},
  { REQ_SPREV_FIELD  |ID_FN  ,FN_Sorted_Previous_Field},
  { REQ_SFIRST_FIELD |ID_FN  ,FN_Sorted_First_Field},
  { REQ_SLAST_FIELD  |ID_FN  ,FN_Sorted_Last_Field},
  { REQ_LEFT_FIELD   |ID_FN  ,FN_Left_Field},
  { REQ_RIGHT_FIELD  |ID_FN  ,FN_Right_Field},
  { REQ_UP_FIELD     |ID_FN  ,FN_Up_Field},
  { REQ_DOWN_FIELD   |ID_FN  ,FN_Down_Field},

  { REQ_NEXT_CHAR    |ID_IFN ,IFN_Next_Character},
  { REQ_PREV_CHAR    |ID_IFN ,IFN_Previous_Character},
  { REQ_NEXT_LINE    |ID_IFN ,IFN_Next_Line},
  { REQ_PREV_LINE    |ID_IFN ,IFN_Previous_Line},
  { REQ_NEXT_WORD    |ID_IFN ,IFN_Next_Word},
  { REQ_PREV_WORD    |ID_IFN ,IFN_Previous_Word},
  { REQ_BEG_FIELD    |ID_IFN ,IFN_Beginning_Of_Field},
  { REQ_END_FIELD    |ID_IFN ,IFN_End_Of_Field},
  { REQ_BEG_LINE     |ID_IFN ,IFN_Beginning_Of_Line},
  { REQ_END_LINE     |ID_IFN ,IFN_End_Of_Line},
  { REQ_LEFT_CHAR    |ID_IFN ,IFN_Left_Character},
  { REQ_RIGHT_CHAR   |ID_IFN ,IFN_Right_Character},
  { REQ_UP_CHAR      |ID_IFN ,IFN_Up_Character},
  { REQ_DOWN_CHAR    |ID_IFN ,IFN_Down_Character},

  { REQ_NEW_LINE     |ID_FE  ,FE_New_Line},
  { REQ_INS_CHAR     |ID_FE  ,FE_Insert_Character},
  { REQ_INS_LINE     |ID_FE  ,FE_Insert_Line},
  { REQ_DEL_CHAR     |ID_FE  ,FE_Delete_Character},
  { REQ_DEL_PREV     |ID_FE  ,FE_Delete_Previous},
  { REQ_DEL_LINE     |ID_FE  ,FE_Delete_Line},
  { REQ_DEL_WORD     |ID_FE  ,FE_Delete_Word},
  { REQ_CLR_EOL      |ID_FE  ,FE_Clear_To_End_Of_Line},
  { REQ_CLR_EOF      |ID_FE  ,FE_Clear_To_End_Of_Field},
  { REQ_CLR_FIELD    |ID_FE  ,FE_Clear_Field},

  { REQ_OVL_MODE     |ID_EM  ,EM_Overlay_Mode},
  { REQ_INS_MODE     |ID_EM  ,EM_Insert_Mode},

  { REQ_SCR_FLINE    |ID_VSC ,VSC_Scroll_Line_Forward},
  { REQ_SCR_BLINE    |ID_VSC ,VSC_Scroll_Line_Backward},
  { REQ_SCR_FPAGE    |ID_VSC ,VSC_Scroll_Page_Forward},
  { REQ_SCR_BPAGE    |ID_VSC ,VSC_Scroll_Page_Backward},
  { REQ_SCR_FHPAGE   |ID_VSC ,VSC_Scroll_Half_Page_Forward},
  { REQ_SCR_BHPAGE   |ID_VSC ,VSC_Scroll_Half_Page_Backward},

  { REQ_SCR_FCHAR    |ID_HSC ,HSC_Scroll_Char_Forward},
  { REQ_SCR_BCHAR    |ID_HSC ,HSC_Scroll_Char_Backward},
  { REQ_SCR_HFLINE   |ID_HSC ,HSC_Horizontal_Line_Forward},
  { REQ_SCR_HBLINE   |ID_HSC ,HSC_Horizontal_Line_Backward},
  { REQ_SCR_HFHALF   |ID_HSC ,HSC_Horizontal_Half_Line_Forward},
  { REQ_SCR_HBHALF   |ID_HSC ,HSC_Horizontal_Half_Line_Backward},

  { REQ_VALIDATION   |ID_FV  ,FV_Validation},

  { REQ_NEXT_CHOICE  |ID_CH  ,CR_Next_Choice},
  { REQ_PREV_CHOICE  |ID_CH  ,CR_Previous_Choice}
};
/* *INDENT-ON* */

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int form_driver(FORM * form,int  c)
|
|   Description   :  This is the workhorse of the forms system. It checks
|                    to determine whether the character c is a request or
|                    data. If it is a request, the form driver executes
|                    the request and returns the result. If it is data
|                    (printable character), it enters the data into the
|                    current position in the current field. If it is not
|                    recognized, the form driver assumes it is an application
|                    defined command and returns E_UNKNOWN_COMMAND.
|                    Application defined command should be defined relative
|                    to MAX_FORM_COMMAND, the maximum value of a request.
|
|   Return Values :  E_OK              - success
|                    E_SYSTEM_ERROR    - system error
|                    E_BAD_ARGUMENT    - an argument is incorrect
|                    E_NOT_POSTED      - form is not posted
|                    E_INVALID_FIELD   - field contents are invalid
|                    E_BAD_STATE       - called from inside a hook routine
|                    E_REQUEST_DENIED  - request failed
|                    E_NOT_CONNECTED   - no fields are connected to the form
|                    E_UNKNOWN_COMMAND - command not known
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
form_driver(FORM *form, int c)
{
  const Binding_Info *BI = (Binding_Info *) 0;
  int res = E_UNKNOWN_COMMAND;

  T((T_CALLED("form_driver(%p,%d)"), (void *)form, c));

  if (!form)
    RETURN(E_BAD_ARGUMENT);

  if (!(form->field))
    RETURN(E_NOT_CONNECTED);

  assert(form->page);

  if (c == FIRST_ACTIVE_MAGIC)
    {
      form->current = _nc_First_Active_Field(form);
      RETURN(E_OK);
    }

  assert(form->current &&
	 form->current->buf &&
	 (form->current->form == form)
    );

  if (form->status & _IN_DRIVER)
    RETURN(E_BAD_STATE);

  if (!(form->status & _POSTED))
    RETURN(E_NOT_POSTED);

  if ((c >= MIN_FORM_COMMAND && c <= MAX_FORM_COMMAND) &&
      ((bindings[c - MIN_FORM_COMMAND].keycode & Key_Mask) == c))
    {
      TR(TRACE_CALLS, ("form_request %s", form_request_name(c)));
      BI = &(bindings[c - MIN_FORM_COMMAND]);
    }

  if (BI)
    {
      typedef int (*Generic_Method) (int (*const) (FORM *), FORM *);
      static const Generic_Method Generic_Methods[] =
      {
	Page_Navigation,	/* overloaded to call field&form hooks */
	Inter_Field_Navigation,	/* overloaded to call field hooks      */
	NULL,			/* Intra-Field is generic              */
	Vertical_Scrolling,	/* Overloaded to check multi-line      */
	Horizontal_Scrolling,	/* Overloaded to check single-line     */
	Field_Editing,		/* Overloaded to mark modification     */
	NULL,			/* Edit Mode is generic                */
	NULL,			/* Field Validation is generic         */
	NULL			/* Choice Request is generic           */
      };
      size_t nMethods = (sizeof(Generic_Methods) / sizeof(Generic_Methods[0]));
      size_t method = (size_t) ((BI->keycode >> ID_Shft) & 0xffff);	/* see ID_Mask */

      if ((method >= nMethods) || !(BI->cmd))
	res = E_SYSTEM_ERROR;
      else
	{
	  Generic_Method fct = Generic_Methods[method];

	  if (fct)
	    {
	      res = fct(BI->cmd, form);
	    }
	  else
	    {
	      res = (BI->cmd) (form);
	    }
	}
    }
#ifdef NCURSES_MOUSE_VERSION
  else if (KEY_MOUSE == c)
    {
      MEVENT event;
      WINDOW *win = form->win ? form->win : StdScreen(Get_Form_Screen(form));
      WINDOW *sub = form->sub ? form->sub : win;

      getmouse(&event);
      if ((event.bstate & (BUTTON1_CLICKED |
			   BUTTON1_DOUBLE_CLICKED |
			   BUTTON1_TRIPLE_CLICKED))
	  && wenclose(win, event.y, event.x))
	{			/* we react only if the click was in the userwin, that means
				 * inside the form display area or at the decoration window.
				 */
	  int ry = event.y, rx = event.x;	/* screen coordinates */

	  res = E_REQUEST_DENIED;
	  if (mouse_trafo(&ry, &rx, FALSE))
	    {			/* rx, ry are now "curses" coordinates */
	      if (ry < sub->_begy)
		{		/* we clicked above the display region; this is
				 * interpreted as "scroll up" request
				 */
		  if (event.bstate & BUTTON1_CLICKED)
		    res = form_driver(form, REQ_PREV_FIELD);
		  else if (event.bstate & BUTTON1_DOUBLE_CLICKED)
		    res = form_driver(form, REQ_PREV_PAGE);
		  else if (event.bstate & BUTTON1_TRIPLE_CLICKED)
		    res = form_driver(form, REQ_FIRST_FIELD);
		}
	      else if (ry > sub->_begy + sub->_maxy)
		{		/* we clicked below the display region; this is
				 * interpreted as "scroll down" request
				 */
		  if (event.bstate & BUTTON1_CLICKED)
		    res = form_driver(form, REQ_NEXT_FIELD);
		  else if (event.bstate & BUTTON1_DOUBLE_CLICKED)
		    res = form_driver(form, REQ_NEXT_PAGE);
		  else if (event.bstate & BUTTON1_TRIPLE_CLICKED)
		    res = form_driver(form, REQ_LAST_FIELD);
		}
	      else if (wenclose(sub, event.y, event.x))
		{		/* Inside the area we try to find the hit item */
		  int i;

		  ry = event.y;
		  rx = event.x;
		  if (wmouse_trafo(sub, &ry, &rx, FALSE))
		    {
		      int min_field = form->page[form->curpage].pmin;
		      int max_field = form->page[form->curpage].pmax;

		      for (i = min_field; i <= max_field; ++i)
			{
			  FIELD *field = form->field[i];

			  if (Field_Is_Selectable(field)
			      && Field_encloses(field, ry, rx) == E_OK)
			    {
			      res = _nc_Set_Current_Field(form, field);
			      if (res == E_OK)
				res = _nc_Position_Form_Cursor(form);
			      if (res == E_OK
				  && (event.bstate & BUTTON1_DOUBLE_CLICKED))
				res = E_UNKNOWN_COMMAND;
			      break;
			    }
			}
		    }
		}
	    }
	}
      else
	res = E_REQUEST_DENIED;
    }
#endif /* NCURSES_MOUSE_VERSION */
  else if (!(c & (~(int)MAX_REGULAR_CHARACTER)))
    {
      /*
       * If we're using 8-bit characters, iscntrl+isprint cover the whole set.
       * But with multibyte characters, there is a third possibility, i.e.,
       * parts of characters that build up into printable characters which are
       * not considered printable.
       *
       * FIXME: the wide-character branch should also use Check_Char().
       */
#if USE_WIDEC_SUPPORT
      if (!iscntrl(UChar(c)))
#else
      if (isprint(UChar(c)) &&
	  Check_Char(form, form->current, form->current->type, c,
		     (TypeArgument *)(form->current->arg)))
#endif
	res = Data_Entry(form, c);
    }
  _nc_Refresh_Current_Field(form);
  RETURN(res);
}

# if USE_WIDEC_SUPPORT
/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int form_driver_w(FORM * form,int type,wchar_t  c)
|
|   Description   :  This is the workhorse of the forms system.
|
|                    Input is either a key code (request) or a wide char
|                    returned by e.g. get_wch (). The type must be passed
|                    as well,so that we are able to determine whether the char
|                    is a multibyte char or a request.

|                    If it is a request, the form driver executes
|                    the request and returns the result. If it is data
|                    (printable character), it enters the data into the
|                    current position in the current field. If it is not
|                    recognized, the form driver assumes it is an application
|                    defined command and returns E_UNKNOWN_COMMAND.
|                    Application defined command should be defined relative
|                    to MAX_FORM_COMMAND, the maximum value of a request.
|
|   Return Values :  E_OK              - success
|                    E_SYSTEM_ERROR    - system error
|                    E_BAD_ARGUMENT    - an argument is incorrect
|                    E_NOT_POSTED      - form is not posted
|                    E_INVALID_FIELD   - field contents are invalid
|                    E_BAD_STATE       - called from inside a hook routine
|                    E_REQUEST_DENIED  - request failed
|                    E_NOT_CONNECTED   - no fields are connected to the form
|                    E_UNKNOWN_COMMAND - command not known
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
form_driver_w(FORM *form, int type, wchar_t c)
{
  const Binding_Info *BI = (Binding_Info *) 0;
  int res = E_UNKNOWN_COMMAND;

  T((T_CALLED("form_driver(%p,%d)"), (void *)form, (int) c));

  if (!form)
    RETURN(E_BAD_ARGUMENT);

  if (!(form->field))
    RETURN(E_NOT_CONNECTED);

  assert(form->page);

  if (c == (wchar_t)FIRST_ACTIVE_MAGIC)
    {
      form->current = _nc_First_Active_Field(form);
      RETURN(E_OK);
    }

  assert(form->current &&
	 form->current->buf &&
	 (form->current->form == form)
    );

  if (form->status & _IN_DRIVER)
    RETURN(E_BAD_STATE);

  if (!(form->status & _POSTED))
    RETURN(E_NOT_POSTED);

  /* check if this is a keycode or a (wide) char */
  if (type == KEY_CODE_YES)
    {
      if ((c >= MIN_FORM_COMMAND && c <= MAX_FORM_COMMAND) &&
	  ((bindings[c - MIN_FORM_COMMAND].keycode & Key_Mask) == c))
	BI = &(bindings[c - MIN_FORM_COMMAND]);
    }

  if (BI)
    {
      typedef int (*Generic_Method) (int (*const) (FORM *), FORM *);
      static const Generic_Method Generic_Methods[] =
      {
	Page_Navigation,	/* overloaded to call field&form hooks */
	Inter_Field_Navigation,	/* overloaded to call field hooks      */
	NULL,			/* Intra-Field is generic              */
	Vertical_Scrolling,	/* Overloaded to check multi-line      */
	Horizontal_Scrolling,	/* Overloaded to check single-line     */
	Field_Editing,		/* Overloaded to mark modification     */
	NULL,			/* Edit Mode is generic                */
	NULL,			/* Field Validation is generic         */
	NULL			/* Choice Request is generic           */
      };
      size_t nMethods = (sizeof(Generic_Methods) / sizeof(Generic_Methods[0]));
      size_t method = (size_t) (BI->keycode >> ID_Shft) & 0xffff;	/* see ID_Mask */

      if ((method >= nMethods) || !(BI->cmd))
	res = E_SYSTEM_ERROR;
      else
	{
	  Generic_Method fct = Generic_Methods[method];

	  if (fct)
	    res = fct(BI->cmd, form);
	  else
	    res = (BI->cmd) (form);
	}
    }
#ifdef NCURSES_MOUSE_VERSION
  else if (KEY_MOUSE == c)
    {
      MEVENT event;
      WINDOW *win = form->win ? form->win : StdScreen(Get_Form_Screen(form));
      WINDOW *sub = form->sub ? form->sub : win;

      getmouse(&event);
      if ((event.bstate & (BUTTON1_CLICKED |
			   BUTTON1_DOUBLE_CLICKED |
			   BUTTON1_TRIPLE_CLICKED))
	  && wenclose(win, event.y, event.x))
	{			/* we react only if the click was in the userwin, that means
				   * inside the form display area or at the decoration window.
				 */
	  int ry = event.y, rx = event.x;	/* screen coordinates */

	  res = E_REQUEST_DENIED;
	  if (mouse_trafo(&ry, &rx, FALSE))
	    {			/* rx, ry are now "curses" coordinates */
	      if (ry < sub->_begy)
		{		/* we clicked above the display region; this is
				   * interpreted as "scroll up" request
				 */
		  if (event.bstate & BUTTON1_CLICKED)
		    res = form_driver(form, REQ_PREV_FIELD);
		  else if (event.bstate & BUTTON1_DOUBLE_CLICKED)
		    res = form_driver(form, REQ_PREV_PAGE);
		  else if (event.bstate & BUTTON1_TRIPLE_CLICKED)
		    res = form_driver(form, REQ_FIRST_FIELD);
		}
	      else if (ry > sub->_begy + sub->_maxy)
		{		/* we clicked below the display region; this is
				   * interpreted as "scroll down" request
				 */
		  if (event.bstate & BUTTON1_CLICKED)
		    res = form_driver(form, REQ_NEXT_FIELD);
		  else if (event.bstate & BUTTON1_DOUBLE_CLICKED)
		    res = form_driver(form, REQ_NEXT_PAGE);
		  else if (event.bstate & BUTTON1_TRIPLE_CLICKED)
		    res = form_driver(form, REQ_LAST_FIELD);
		}
	      else if (wenclose(sub, event.y, event.x))
		{		/* Inside the area we try to find the hit item */
		  int i;

		  ry = event.y;
		  rx = event.x;
		  if (wmouse_trafo(sub, &ry, &rx, FALSE))
		    {
		      int min_field = form->page[form->curpage].pmin;
		      int max_field = form->page[form->curpage].pmax;

		      for (i = min_field; i <= max_field; ++i)
			{
			  FIELD *field = form->field[i];

			  if (Field_Is_Selectable(field)
			      && Field_encloses(field, ry, rx) == E_OK)
			    {
			      res = _nc_Set_Current_Field(form, field);
			      if (res == E_OK)
				res = _nc_Position_Form_Cursor(form);
			      if (res == E_OK
				  && (event.bstate & BUTTON1_DOUBLE_CLICKED))
				res = E_UNKNOWN_COMMAND;
			      break;
			    }
			}
		    }
		}
	    }
	}
      else
	res = E_REQUEST_DENIED;
    }
#endif /* NCURSES_MOUSE_VERSION */
  else if (type == OK)
    {
      res = Data_Entry_w(form, c);
    }

  _nc_Refresh_Current_Field(form);
  RETURN(res);
}
# endif	/* USE_WIDEC_SUPPORT */

/*----------------------------------------------------------------------------
  Field-Buffer manipulation routines.
  The effects of setting a buffer are tightly coupled to the core of the form
  driver logic. This is especially true in the case of growable fields.
  So I don't separate this into a separate module.
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_field_buffer(FIELD *field,
|                                         int buffer, char *value)
|
|   Description   :  Set the given buffer of the field to the given value.
|                    Buffer 0 stores the displayed content of the field.
|                    For dynamic fields this may grow the fieldbuffers if
|                    the length of the value exceeds the current buffer
|                    length. For buffer 0 only printable values are allowed.
|                    For static fields, the value needs not to be zero ter-
|                    minated. It is copied up to the length of the buffer.
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - invalid argument
|                    E_SYSTEM_ERROR  - system error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_field_buffer(FIELD *field, int buffer, const char *value)
{
  FIELD_CELL *p;
  int res = E_OK;
  int i;
  int len;

#if USE_WIDEC_SUPPORT
  FIELD_CELL *widevalue = 0;
#endif

  T((T_CALLED("set_field_buffer(%p,%d,%s)"), (void *)field, buffer, _nc_visbuf(value)));

  if (!field || !value || ((buffer < 0) || (buffer > field->nbuf)))
    RETURN(E_BAD_ARGUMENT);

  len = Buffer_Length(field);

  if (Growable(field))
    {
      /* for a growable field we must assume zero terminated strings, because
         somehow we have to detect the length of what should be copied.
       */
      int vlen = (int)strlen(value);

      if (vlen > len)
	{
	  if (!Field_Grown(field,
			   (int)(1 + (vlen - len) / ((field->rows + field->nrow)
						     * field->cols))))
	    RETURN(E_SYSTEM_ERROR);

#if !USE_WIDEC_SUPPORT
	  len = vlen;
#endif
	}
    }

  p = Address_Of_Nth_Buffer(field, buffer);

#if USE_WIDEC_SUPPORT
  /*
   * Use addstr's logic for converting a string to an array of cchar_t's.
   * There should be a better way, but this handles nonspacing characters
   * and other special cases that we really do not want to handle here.
   */
#if NCURSES_EXT_FUNCS
  if (wresize(field->working, 1, Buffer_Length(field) + 1) == ERR)
#endif
    {
      delwin(field->working);
      field->working = newpad(1, Buffer_Length(field) + 1);
    }
  len = Buffer_Length(field);
  wclear(field->working);
  (void)mvwaddstr(field->working, 0, 0, value);

  if ((widevalue = typeCalloc(FIELD_CELL, len + 1)) == 0)
    {
      RETURN(E_SYSTEM_ERROR);
    }
  else
    {
      for (i = 0; i < field->drows; ++i)
	{
	  (void)mvwin_wchnstr(field->working, 0, (int)i * field->dcols,
			      widevalue + ((int)i * field->dcols),
			      field->dcols);
	}
      for (i = 0; i < len; ++i)
	{
	  if (CharEq(myZEROS, widevalue[i]))
	    {
	      while (i < len)
		p[i++] = myBLANK;
	      break;
	    }
	  p[i] = widevalue[i];
	}
      free(widevalue);
    }
#else
  for (i = 0; i < len; ++i)
    {
      if (value[i] == '\0')
	{
	  while (i < len)
	    p[i++] = myBLANK;
	  break;
	}
      p[i] = value[i];
    }
#endif

  if (buffer == 0)
    {
      int syncres;

      if (((syncres = Synchronize_Field(field)) != E_OK) &&
	  (res == E_OK))
	res = syncres;
      if (((syncres = Synchronize_Linked_Fields(field)) != E_OK) &&
	  (res == E_OK))
	res = syncres;
    }
  RETURN(res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  char *field_buffer(const FIELD *field,int buffer)
|
|   Description   :  Return the address of the buffer for the field.
|
|   Return Values :  Pointer to buffer or NULL if arguments were invalid.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(char *)
field_buffer(const FIELD *field, int buffer)
{
  char *result = 0;

  T((T_CALLED("field_buffer(%p,%d)"), (const void *)field, buffer));

  if (field && (buffer >= 0) && (buffer <= field->nbuf))
    {
#if USE_WIDEC_SUPPORT
      FIELD_CELL *data = Address_Of_Nth_Buffer(field, buffer);
      size_t need = 0;
      int size = Buffer_Length(field);
      int n;

      /* determine the number of bytes needed to store the expanded string */
      for (n = 0; n < size; ++n)
	{
	  if (!isWidecExt(data[n]) && data[n].chars[0] != L'\0')
	    {
	      mbstate_t state;
	      size_t next;

	      init_mb(state);
	      next = _nc_wcrtomb(0, data[n].chars[0], &state);
	      if (next > 0)
		need += next;
	    }
	}

      /* allocate a place to store the expanded string */
      if (field->expanded[buffer] != 0)
	free(field->expanded[buffer]);
      field->expanded[buffer] = typeMalloc(char, need + 1);

      /*
       * Expand the multibyte data.
       *
       * It may also be multi-column data.  In that case, the data for a row
       * may be null-padded to align to the dcols/drows layout (or it may
       * contain embedded wide-character extensions).  Change the null-padding
       * to blanks as needed.
       */
      if ((result = field->expanded[buffer]) != 0)
	{
	  wclear(field->working);
	  wmove(field->working, 0, 0);
	  for (n = 0; n < size; ++n)
	    {
	      if (!isWidecExt(data[n]) && data[n].chars[0] != L'\0')
		wadd_wch(field->working, &data[n]);
	    }
	  wmove(field->working, 0, 0);
	  winnstr(field->working, result, (int)need);
	}
#else
      result = Address_Of_Nth_Buffer(field, buffer);
#endif
    }
  returnPtr(result);
}

#if USE_WIDEC_SUPPORT

/*---------------------------------------------------------------------------
| Convert a multibyte string to a wide-character string.  The result must be
| freed by the caller.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(wchar_t *)
_nc_Widen_String(char *source, int *lengthp)
{
  wchar_t *result = 0;
  wchar_t wch;
  size_t given = strlen(source);
  size_t tries;
  int pass;
  int status;

#ifndef state_unused
  mbstate_t state;
#endif

  for (pass = 0; pass < 2; ++pass)
    {
      unsigned need = 0;
      size_t passed = 0;

      while (passed < given)
	{
	  bool found = FALSE;

	  for (tries = 1, status = 0; tries <= (given - passed); ++tries)
	    {
	      int save = source[passed + tries];

	      source[passed + tries] = 0;
	      reset_mbytes(state);
	      status = check_mbytes(wch, source + passed, tries, state);
	      source[passed + tries] = (char)save;

	      if (status > 0)
		{
		  found = TRUE;
		  break;
		}
	    }
	  if (found)
	    {
	      if (pass)
		{
		  result[need] = wch;
		}
	      passed += (size_t) status;
	      ++need;
	    }
	  else
	    {
	      if (pass)
		{
		  result[need] = source[passed];
		}
	      ++need;
	      ++passed;
	    }
	}

      if (!pass)
	{
	  if (!need)
	    break;
	  result = typeCalloc(wchar_t, need);

	  *lengthp = (int)need;
	  if (result == 0)
	    break;
	}
    }

  return result;
}
#endif

/* frm_driver.c ends here */
