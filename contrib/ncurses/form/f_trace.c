/****************************************************************************
 * Copyright (c) 2004,2010 Free Software Foundation, Inc.                   *
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
 *   Author:  Thomas E. Dickey                                              *
 ****************************************************************************/

#include "form.priv.h"

MODULE_ID("$Id: f_trace.c,v 1.2 2010/01/23 21:14:36 tom Exp $")

NCURSES_EXPORT(FIELD **)
_nc_retrace_field_ptr(FIELD **code)
{
  T((T_RETURN("%p"), (void *)code));
  return code;
}

NCURSES_EXPORT(FIELD *)
_nc_retrace_field(FIELD *code)
{
  T((T_RETURN("%p"), (void *)code));
  return code;
}

NCURSES_EXPORT(FIELDTYPE *)
_nc_retrace_field_type(FIELDTYPE *code)
{
  T((T_RETURN("%p"), (void *)code));
  return code;
}

NCURSES_EXPORT(FORM *)
_nc_retrace_form(FORM *code)
{
  T((T_RETURN("%p"), (void *)code));
  return code;
}

NCURSES_EXPORT(Form_Hook)
_nc_retrace_form_hook(Form_Hook code)
{
  T((T_RETURN("%p"), code));
  return code;
}
