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

MODULE_ID("$Id: fld_opts.c,v 1.12 2010/01/23 21:14:36 tom Exp $")

/*----------------------------------------------------------------------------
  Field-Options manipulation routines
  --------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int set_field_opts(FIELD *field, Field_Options opts)
|   
|   Description   :  Turns on the named options for this field and turns
|                    off all the remaining options.
|
|   Return Values :  E_OK            - success
|                    E_CURRENT       - the field is the current field
|                    E_BAD_ARGUMENT  - invalid options
|                    E_SYSTEM_ERROR  - system error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_field_opts(FIELD *field, Field_Options opts)
{
  int res = E_BAD_ARGUMENT;

  T((T_CALLED("set_field_opts(%p,%d)"), (void *)field, opts));

  opts &= ALL_FIELD_OPTS;
  if (!(opts & ~ALL_FIELD_OPTS))
    res = _nc_Synchronize_Options(Normalize_Field(field), opts);
  RETURN(res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  Field_Options field_opts(const FIELD *field)
|   
|   Description   :  Retrieve the fields options.
|
|   Return Values :  The options.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(Field_Options)
field_opts(const FIELD *field)
{
  T((T_CALLED("field_opts(%p)"), (const void *)field));

  returnCode(ALL_FIELD_OPTS & Normalize_Field(field)->opts);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int field_opts_on(FIELD *field, Field_Options opts)
|   
|   Description   :  Turns on the named options for this field and all the 
|                    remaining options are unchanged.
|
|   Return Values :  E_OK            - success
|                    E_CURRENT       - the field is the current field
|                    E_BAD_ARGUMENT  - invalid options
|                    E_SYSTEM_ERROR  - system error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
field_opts_on(FIELD *field, Field_Options opts)
{
  int res = E_BAD_ARGUMENT;

  T((T_CALLED("field_opts_on(%p,%d)"), (void *)field, opts));

  opts &= ALL_FIELD_OPTS;
  if (!(opts & ~ALL_FIELD_OPTS))
    {
      Normalize_Field(field);
      res = _nc_Synchronize_Options(field, field->opts | opts);
    }
  RETURN(res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int field_opts_off(FIELD *field, Field_Options opts)
|   
|   Description   :  Turns off the named options for this field and all the 
|                    remaining options are unchanged.
|
|   Return Values :  E_OK            - success
|                    E_CURRENT       - the field is the current field
|                    E_BAD_ARGUMENT  - invalid options
|                    E_SYSTEM_ERROR  - system error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
field_opts_off(FIELD *field, Field_Options opts)
{
  int res = E_BAD_ARGUMENT;

  T((T_CALLED("field_opts_off(%p,%d)"), (void *)field, opts));

  opts &= ALL_FIELD_OPTS;
  if (!(opts & ~ALL_FIELD_OPTS))
    {
      Normalize_Field(field);
      res = _nc_Synchronize_Options(field, field->opts & ~opts);
    }
  RETURN(res);
}

/* fld_opts.c ends here */
