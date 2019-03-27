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

MODULE_ID("$Id: fty_regex.c,v 1.25 2012/10/27 20:12:53 tom Exp $")

#if HAVE_REGEX_H_FUNCS		/* We prefer POSIX regex */
#include <regex.h>

typedef struct
  {
    regex_t *pRegExp;
    unsigned long *refCount;
  }
RegExp_Arg;

#elif HAVE_REGEXP_H_FUNCS | HAVE_REGEXPR_H_FUNCS
#undef RETURN
static int reg_errno;

static char *
RegEx_Init(char *instring)
{
  reg_errno = 0;
  return instring;
}

static char *
RegEx_Error(int code)
{
  reg_errno = code;
  return 0;
}

#define INIT 		register char *sp = RegEx_Init(instring);
#define GETC()		(*sp++)
#define PEEKC()		(*sp)
#define UNGETC(c)	(--sp)
#define RETURN(c)	return(c)
#define ERROR(c)	return RegEx_Error(c)

#if HAVE_REGEXP_H_FUNCS
#include <regexp.h>
#else
#include <regexpr.h>
#endif

typedef struct
{
  char *compiled_expression;
  unsigned long *refCount;
}
RegExp_Arg;

/* Maximum Length we allow for a compiled regular expression */
#define MAX_RX_LEN   (2048)
#define RX_INCREMENT (256)

#endif

#if HAVE_REGEX_H_FUNCS | HAVE_REGEXP_H_FUNCS | HAVE_REGEXPR_H_FUNCS
# define MAYBE_UNUSED
#else
# define MAYBE_UNUSED GCC_UNUSED
#endif

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void *Generic_RegularExpression_Type(void * arg)
|
|   Description   :  Allocate structure for regex type argument.
|
|   Return Values :  Pointer to argument structure or NULL on error
+--------------------------------------------------------------------------*/
static void *
Generic_RegularExpression_Type(void *arg MAYBE_UNUSED)
{
#if HAVE_REGEX_H_FUNCS
  char *rx = (char *)arg;
  RegExp_Arg *preg = (RegExp_Arg *)0;

  if (rx)
    {
      preg = typeMalloc(RegExp_Arg, 1);

      if (preg)
	{
	  T((T_CREATE("RegExp_Arg %p"), (void *)preg));
	  if (((preg->pRegExp = typeMalloc(regex_t, 1)) != 0)
	      && !regcomp(preg->pRegExp, rx,
			  (REG_EXTENDED | REG_NOSUB | REG_NEWLINE)))
	    {
	      T((T_CREATE("regex_t %p"), (void *)preg->pRegExp));
	      if ((preg->refCount = typeMalloc(unsigned long, 1)) != 0)
		 *(preg->refCount) = 1;
	    }
	  else
	    {
	      if (preg->pRegExp)
		free(preg->pRegExp);
	      free(preg);
	      preg = (RegExp_Arg *)0;
	    }
	}
    }
  return ((void *)preg);
#elif HAVE_REGEXP_H_FUNCS | HAVE_REGEXPR_H_FUNCS
  char *rx = (char *)arg;
  RegExp_Arg *pArg = (RegExp_Arg *)0;

  if (rx)
    {
      pArg = typeMalloc(RegExp_Arg, 1);

      if (pArg)
	{
	  int blen = RX_INCREMENT;

	  T((T_CREATE("RegExp_Arg %p"), pArg));
	  pArg->compiled_expression = NULL;
	  if ((pArg->refCount = typeMalloc(unsigned long, 1)) != 0)
	     *(pArg->refCount) = 1;

	  do
	    {
	      char *buf = typeMalloc(char, blen);

	      if (buf)
		{
#if HAVE_REGEXP_H_FUNCS
		  char *last_pos = compile(rx, buf, &buf[blen], '\0');

#else /* HAVE_REGEXPR_H_FUNCS */
		  char *last_pos = compile(rx, buf, &buf[blen]);
#endif
		  if (reg_errno)
		    {
		      free(buf);
		      if (reg_errno == 50)
			blen += RX_INCREMENT;
		      else
			{
			  free(pArg);
			  pArg = NULL;
			  break;
			}
		    }
		  else
		    {
		      pArg->compiled_expression = buf;
		      break;
		    }
		}
	    }
	  while (blen <= MAX_RX_LEN);
	}
      if (pArg && !pArg->compiled_expression)
	{
	  free(pArg);
	  pArg = NULL;
	}
    }
  return (void *)pArg;
#else
  return 0;
#endif
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void *Make_RegularExpression_Type(va_list * ap)
|
|   Description   :  Allocate structure for regex type argument.
|
|   Return Values :  Pointer to argument structure or NULL on error
+--------------------------------------------------------------------------*/
static void *
Make_RegularExpression_Type(va_list *ap)
{
  char *rx = va_arg(*ap, char *);

  return Generic_RegularExpression_Type((void *)rx);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void *Copy_RegularExpression_Type(
|                                      const void * argp)
|
|   Description   :  Copy structure for regex type argument.
|
|   Return Values :  Pointer to argument structure or NULL on error.
+--------------------------------------------------------------------------*/
static void *
Copy_RegularExpression_Type(const void *argp MAYBE_UNUSED)
{
#if (HAVE_REGEX_H_FUNCS | HAVE_REGEXP_H_FUNCS | HAVE_REGEXPR_H_FUNCS)
  const RegExp_Arg *ap = (const RegExp_Arg *)argp;
  const RegExp_Arg *result = (const RegExp_Arg *)0;

  if (ap)
    {
      *(ap->refCount) += 1;
      result = ap;
    }
  return (void *)result;
#else
  return 0;
#endif
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static void Free_RegularExpression_Type(void * argp)
|
|   Description   :  Free structure for regex type argument.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
static void
Free_RegularExpression_Type(void *argp MAYBE_UNUSED)
{
#if HAVE_REGEX_H_FUNCS | HAVE_REGEXP_H_FUNCS | HAVE_REGEXPR_H_FUNCS
  RegExp_Arg *ap = (RegExp_Arg *)argp;

  if (ap)
    {
      if (--(*(ap->refCount)) == 0)
	{
#if HAVE_REGEX_H_FUNCS
	  if (ap->pRegExp)
	    {
	      free(ap->refCount);
	      regfree(ap->pRegExp);
	    }
#elif HAVE_REGEXP_H_FUNCS | HAVE_REGEXPR_H_FUNCS
	  if (ap->compiled_expression)
	    {
	      free(ap->refCount);
	      free(ap->compiled_expression);
	    }
#endif
	  free(ap);
	}
    }
#endif
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  static bool Check_RegularExpression_Field(
|                                      FIELD * field,
|                                      const void  * argp)
|
|   Description   :  Validate buffer content to be a valid regular expression
|
|   Return Values :  TRUE  - field is valid
|                    FALSE - field is invalid
+--------------------------------------------------------------------------*/
static bool
Check_RegularExpression_Field(FIELD *field MAYBE_UNUSED,
			      const void *argp MAYBE_UNUSED)
{
  bool match = FALSE;

#if HAVE_REGEX_H_FUNCS
  const RegExp_Arg *ap = (const RegExp_Arg *)argp;

  if (ap && ap->pRegExp)
    match = (regexec(ap->pRegExp, field_buffer(field, 0), 0, NULL, 0)
	     ? FALSE
	     : TRUE);
#elif HAVE_REGEXP_H_FUNCS | HAVE_REGEXPR_H_FUNCS
  RegExp_Arg *ap = (RegExp_Arg *)argp;

  if (ap && ap->compiled_expression)
    match = (step(field_buffer(field, 0), ap->compiled_expression)
	     ? TRUE
	     : FALSE);
#endif
  return match;
}

static FIELDTYPE typeREGEXP =
{
  _HAS_ARGS | _RESIDENT,
  1,				/* this is mutable, so we can't be const */
  (FIELDTYPE *)0,
  (FIELDTYPE *)0,
  Make_RegularExpression_Type,
  Copy_RegularExpression_Type,
  Free_RegularExpression_Type,
  INIT_FT_FUNC(Check_RegularExpression_Field),
  INIT_FT_FUNC(NULL),
  INIT_FT_FUNC(NULL),
  INIT_FT_FUNC(NULL),
#if NCURSES_INTEROP_FUNCS
  Generic_RegularExpression_Type
#endif
};

NCURSES_EXPORT_VAR(FIELDTYPE*) TYPE_REGEXP = &typeREGEXP;

#if NCURSES_INTEROP_FUNCS
/* The next routines are to simplify the use of ncurses from
   programming languages with restictions on interop with C level
   constructs (e.g. variable access or va_list + ellipsis constructs)
*/
NCURSES_EXPORT(FIELDTYPE *)
_nc_TYPE_REGEXP(void)
{
  return TYPE_REGEXP;
}
#endif

/* fty_regex.c ends here */
