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

/***************************************************************************
*                                                                          *
*  Author : Juergen Pfeifer                                                *
*                                                                          *
***************************************************************************/

#include "form.priv.h"

MODULE_ID("$Id: fty_int.c,v 1.26 2012/02/23 10:02:15 tom Exp $")

#if USE_WIDEC_SUPPORT
#define isDigit(c) (iswdigit((wint_t)(c)) || isdigit(UChar(c)))
#else
#define isDigit(c) isdigit(UChar(c))
#endif

#define thisARG integerARG

typedef struct
  {
    int precision;
    long low;
    long high;
  }
thisARG;

typedef struct
  {
    int precision;
    long low;
    long high;
  }
integerPARM;

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static void *Generic_This_Type( void * arg )
|   
|   Description   :  Allocate structure for integer type argument.
|
|   Return Values :  Pointer to argument structure or NULL on error
+--------------------------------------------------------------------------*/
static void *
Generic_This_Type(void *arg)
{
  thisARG *argp = (thisARG *) 0;
  thisARG *param = (thisARG *) arg;

  if (param)
    {
      argp = typeMalloc(thisARG, 1);

      if (argp)
	{
	  T((T_CREATE("thisARG %p"), (void *)argp));
	  *argp = *param;
	}
    }
  return (void *)argp;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static void *Make_This_Type( va_list * ap )
|   
|   Description   :  Allocate structure for integer type argument.
|
|   Return Values :  Pointer to argument structure or NULL on error
+--------------------------------------------------------------------------*/
static void *
Make_This_Type(va_list *ap)
{
  thisARG arg;

  arg.precision = va_arg(*ap, int);
  arg.low = va_arg(*ap, long);
  arg.high = va_arg(*ap, long);

  return Generic_This_Type((void *)&arg);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static void *Copy_This_Type(const void * argp)
|   
|   Description   :  Copy structure for integer type argument.  
|
|   Return Values :  Pointer to argument structure or NULL on error.
+--------------------------------------------------------------------------*/
static void *
Copy_This_Type(const void *argp)
{
  const thisARG *ap = (const thisARG *)argp;
  thisARG *result = (thisARG *) 0;

  if (argp)
    {
      result = typeMalloc(thisARG, 1);
      if (result)
	{
	  T((T_CREATE("thisARG %p"), (void *)result));
	  *result = *ap;
	}
    }
  return (void *)result;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static void Free_This_Type(void * argp)
|   
|   Description   :  Free structure for integer type argument.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
static void
Free_This_Type(void *argp)
{
  if (argp)
    free(argp);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static bool Check_This_Field(
|                                                 FIELD * field,
|                                                 const void * argp)
|   
|   Description   :  Validate buffer content to be a valid integer value
|
|   Return Values :  TRUE  - field is valid
|                    FALSE - field is invalid
+--------------------------------------------------------------------------*/
static bool
Check_This_Field(FIELD *field, const void *argp)
{
  const thisARG *argi = (const thisARG *)argp;
  long low = argi->low;
  long high = argi->high;
  int prec = argi->precision;
  unsigned char *bp = (unsigned char *)field_buffer(field, 0);
  char *s = (char *)bp;
  long val;
  char buf[100];
  bool result = FALSE;

  while (*bp && *bp == ' ')
    bp++;
  if (*bp)
    {
      if (*bp == '-')
	bp++;
#if USE_WIDEC_SUPPORT
      if (*bp)
	{
	  bool blank = FALSE;
	  int len;
	  int n;
	  wchar_t *list = _nc_Widen_String((char *)bp, &len);

	  if (list != 0)
	    {
	      result = TRUE;
	      for (n = 0; n < len; ++n)
		{
		  if (blank)
		    {
		      if (list[n] != ' ')
			{
			  result = FALSE;
			  break;
			}
		    }
		  else if (list[n] == ' ')
		    {
		      blank = TRUE;
		    }
		  else if (!isDigit(list[n]))
		    {
		      result = FALSE;
		      break;
		    }
		}
	      free(list);
	    }
	}
#else
      while (*bp)
	{
	  if (!isdigit(UChar(*bp)))
	    break;
	  bp++;
	}
      while (*bp && *bp == ' ')
	bp++;
      result = (*bp == '\0');
#endif
      if (result)
	{
	  val = atol(s);
	  if (low < high)
	    {
	      if (val < low || val > high)
		result = FALSE;
	    }
	  if (result)
	    {
	      _nc_SPRINTF(buf, _nc_SLIMIT(sizeof(buf))
			  "%.*ld", (prec > 0 ? prec : 0), val);
	      set_field_buffer(field, 0, buf);
	    }
	}
    }
  return (result);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static bool Check_This_Character(
|                                      int c,
|                                      const void * argp)
|   
|   Description   :  Check a character for the integer type.
|
|   Return Values :  TRUE  - character is valid
|                    FALSE - character is invalid
+--------------------------------------------------------------------------*/
static bool
Check_This_Character(int c, const void *argp GCC_UNUSED)
{
  return ((isDigit(UChar(c)) || (c == '-')) ? TRUE : FALSE);
}

static FIELDTYPE typeTHIS =
{
  _HAS_ARGS | _RESIDENT,
  1,				/* this is mutable, so we can't be const */
  (FIELDTYPE *)0,
  (FIELDTYPE *)0,
  Make_This_Type,
  Copy_This_Type,
  Free_This_Type,
  INIT_FT_FUNC(Check_This_Field),
  INIT_FT_FUNC(Check_This_Character),
  INIT_FT_FUNC(NULL),
  INIT_FT_FUNC(NULL),
#if NCURSES_INTEROP_FUNCS
  Generic_This_Type
#endif
};

NCURSES_EXPORT_VAR(FIELDTYPE*) TYPE_INTEGER = &typeTHIS;

#if NCURSES_INTEROP_FUNCS
/* The next routines are to simplify the use of ncurses from
   programming languages with restictions on interop with C level
   constructs (e.g. variable access or va_list + ellipsis constructs)
*/
NCURSES_EXPORT(FIELDTYPE *)
_nc_TYPE_INTEGER(void)
{
  return TYPE_INTEGER;
}
#endif

/* fty_int.c ends here */
