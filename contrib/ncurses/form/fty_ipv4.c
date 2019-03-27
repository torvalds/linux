/****************************************************************************
 * Copyright (c) 1998-2006,2009 Free Software Foundation, Inc.              *
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
*  Author : Per Foreby, perf@efd.lth.se                                    *
*                                                                          *
***************************************************************************/

#include "form.priv.h"

MODULE_ID("$Id: fty_ipv4.c,v 1.10 2009/11/07 20:17:58 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static bool Check_IPV4_Field(
|                                      FIELD * field,
|                                      const void * argp)
|   
|   Description   :  Validate buffer content to be a valid IP number (Ver. 4)
|
|   Return Values :  TRUE  - field is valid
|                    FALSE - field is invalid
+--------------------------------------------------------------------------*/
static bool
Check_IPV4_Field(FIELD *field, const void *argp GCC_UNUSED)
{
  char *bp = field_buffer(field, 0);
  int num = 0, len;
  unsigned int d1, d2, d3, d4;

  if (isdigit(UChar(*bp)))	/* Must start with digit */
    {
      num = sscanf(bp, "%u.%u.%u.%u%n", &d1, &d2, &d3, &d4, &len);
      if (num == 4)
	{
	  bp += len;		/* Make bp point to what sscanf() left */
	  while (isspace(UChar(*bp)))
	    bp++;		/* Allow trailing whitespace */
	}
    }
  return ((num != 4 || *bp || d1 > 255 || d2 > 255
	   || d3 > 255 || d4 > 255) ? FALSE : TRUE);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static bool Check_IPV4_Character(
|                                      int c, 
|                                      const void *argp )
|   
|   Description   :  Check a character for unsigned type or period.
|
|   Return Values :  TRUE  - character is valid
|                    FALSE - character is invalid
+--------------------------------------------------------------------------*/
static bool
Check_IPV4_Character(int c, const void *argp GCC_UNUSED)
{
  return ((isdigit(UChar(c)) || (c == '.')) ? TRUE : FALSE);
}

static FIELDTYPE typeIPV4 =
{
  _RESIDENT,
  1,				/* this is mutable, so we can't be const */
  (FIELDTYPE *)0,
  (FIELDTYPE *)0,
  NULL,
  NULL,
  NULL,
  INIT_FT_FUNC(Check_IPV4_Field),
  INIT_FT_FUNC(Check_IPV4_Character),
  INIT_FT_FUNC(NULL),
  INIT_FT_FUNC(NULL),
#if NCURSES_INTEROP_FUNCS
  NULL
#endif
};

NCURSES_EXPORT_VAR(FIELDTYPE*) TYPE_IPV4 = &typeIPV4;

#if NCURSES_INTEROP_FUNCS
/* The next routines are to simplify the use of ncurses from
   programming languages with restictions on interop with C level
   constructs (e.g. variable access or va_list + ellipsis constructs)
*/
NCURSES_EXPORT(FIELDTYPE *)
_nc_TYPE_IPV4(void)
{
  return TYPE_IPV4;
}
#endif

/* fty_ipv4.c ends here */
