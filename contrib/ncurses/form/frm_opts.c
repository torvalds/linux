/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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

MODULE_ID("$Id: frm_opts.c,v 1.17 2013/08/24 22:58:47 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int set_form_opts(FORM *form, Form_Options opts)
|   
|   Description   :  Turns on the named options and turns off all the
|                    remaining options for that form.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - invalid options
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_form_opts(FORM *form, Form_Options opts)
{
  T((T_CALLED("set_form_opts(%p,%d)"), (void *)form, opts));

  opts &= (Form_Options) ALL_FORM_OPTS;
  if ((unsigned)opts & ~ALL_FORM_OPTS)
    RETURN(E_BAD_ARGUMENT);
  else
    {
      Normalize_Form(form)->opts = opts;
      RETURN(E_OK);
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  Form_Options form_opts(const FORM *)
|   
|   Description   :  Retrieves the current form options.
|
|   Return Values :  The option flags.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(Form_Options)
form_opts(const FORM *form)
{
  T((T_CALLED("form_opts(%p)"), (const void *)form));
  returnCode((Form_Options) ((unsigned)Normalize_Form(form)->opts & ALL_FORM_OPTS));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int form_opts_on(FORM *form, Form_Options opts)
|   
|   Description   :  Turns on the named options; no other options are 
|                    changed.
|
|   Return Values :  E_OK            - success 
|                    E_BAD_ARGUMENT  - invalid options
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
form_opts_on(FORM *form, Form_Options opts)
{
  T((T_CALLED("form_opts_on(%p,%d)"), (void *)form, opts));

  opts &= (Form_Options) ALL_FORM_OPTS;
  if ((unsigned)opts & ~ALL_FORM_OPTS)
    RETURN(E_BAD_ARGUMENT);
  else
    {
      Normalize_Form(form)->opts |= opts;
      RETURN(E_OK);
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int form_opts_off(FORM *form, Form_Options opts)
|   
|   Description   :  Turns off the named options; no other options are 
|                    changed.
|
|   Return Values :  E_OK            - success 
|                    E_BAD_ARGUMENT  - invalid options
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
form_opts_off(FORM *form, Form_Options opts)
{
  T((T_CALLED("form_opts_off(%p,%d)"), (void *)form, opts));

  opts &= (Form_Options) ALL_FORM_OPTS;
  if ((unsigned)opts & ~ALL_FORM_OPTS)
    RETURN(E_BAD_ARGUMENT);
  else
    {
      Normalize_Form(form)->opts &= ~opts;
      RETURN(E_OK);
    }
}

/* frm_opts.c ends here */
