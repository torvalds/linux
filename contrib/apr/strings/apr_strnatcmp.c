/* -*- mode: c; c-file-style: "k&r" -*-

  strnatcmp.c -- Perform 'natural order' comparisons of strings in C.
  Copyright (C) 2000 by Martin Pool <mbp@humbug.org.au>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <ctype.h>
#include <string.h>
#include "apr_strings.h"
#include "apr_lib.h"            /* for apr_is*() */

#if defined(__GNUC__)
#  define UNUSED __attribute__((__unused__))
#else
#  define UNUSED
#endif

/* based on "strnatcmp.c,v 1.6 2000/04/20 07:30:11 mbp Exp $" */

static int
compare_right(char const *a, char const *b)
{
     int bias = 0;
     
     /* The longest run of digits wins.  That aside, the greatest
	value wins, but we can't know that it will until we've scanned
	both numbers to know that they have the same magnitude, so we
	remember it in BIAS. */
     for (;; a++, b++) {
	  if (!apr_isdigit(*a)  &&  !apr_isdigit(*b))
	       break;
	  else if (!apr_isdigit(*a))
	       return -1;
	  else if (!apr_isdigit(*b))
	       return +1;
	  else if (*a < *b) {
	       if (!bias)
		    bias = -1;
	  } else if (*a > *b) {
	       if (!bias)
		    bias = +1;
	  } else if (!*a  &&  !*b)
	       break;
     }

     return bias;
}


static int
compare_left(char const *a, char const *b)
{
     /* Compare two left-aligned numbers: the first to have a
        different value wins. */
     for (;; a++, b++) {
	  if (!apr_isdigit(*a)  &&  !apr_isdigit(*b))
	       break;
	  else if (!apr_isdigit(*a))
	       return -1;
	  else if (!apr_isdigit(*b))
	       return +1;
	  else if (*a < *b)
	       return -1;
	  else if (*a > *b)
	       return +1;
     }
	  
     return 0;
}


static int strnatcmp0(char const *a, char const *b, int fold_case)
{
     int ai, bi;
     char ca, cb;
     int fractional, result;
     ai = bi = 0;
     while (1) {
	  ca = a[ai]; cb = b[bi];

	  /* skip over leading spaces or zeros */
	  while (apr_isspace(ca))
	       ca = a[++ai];

	  while (apr_isspace(cb))
	       cb = b[++bi];

	  /* process run of digits */
	  if (apr_isdigit(ca)  &&  apr_isdigit(cb)) {
	       fractional = (ca == '0' || cb == '0');

	       if (fractional) {
		    if ((result = compare_left(a+ai, b+bi)) != 0)
			 return result;
	       } else {
		    if ((result = compare_right(a+ai, b+bi)) != 0)
			 return result;
	       }
	  }

	  if (!ca && !cb) {
	       /* The strings compare the same.  Perhaps the caller
                  will want to call strcmp to break the tie. */
	       return 0;
	  }

	  if (fold_case) {
	       ca = apr_toupper(ca);
	       cb = apr_toupper(cb);
	  }
	  
	  if (ca < cb)
	       return -1;
	  else if (ca > cb)
	       return +1;

	  ++ai; ++bi;
     }
}



APR_DECLARE(int) apr_strnatcmp(char const *a, char const *b)
{
     return strnatcmp0(a, b, 0);
}


/* Compare, recognizing numeric string and ignoring case. */
APR_DECLARE(int) apr_strnatcasecmp(char const *a, char const *b)
{
     return strnatcmp0(a, b, 1);
}
