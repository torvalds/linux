/* COLLATE COMPARE, COMPARES DIGITS NUMERICALLY AND OTHERS IN ASCII */

/*
 *   Copyright 2001, 2015, Harlan Stenn.  Used by NTP with permission.
 *
 *   Author: Harlan Stenn <harlan@pfcs.com>
 *
 *   Copying and distribution of this file, with or without modification,
 *   are permitted in any medium without royalty provided the copyright
 *   notice and this notice are preserved. This file is offered as-is,
 *   without any warranty.
 */

/*
 * Expected collate order for numeric "pieces" is:
 * 0 - 9	followed by
 * 00 - 99	followed by
 * 000 - 999	followed by
 * ...
 */

#include <ctype.h>

/*
 * Older versions of isdigit() require the argument be isascii()
 */

#if 0
# define MyIsDigit(x)	\
      (isascii ((unsigned char) (x)) && isdigit ((unsigned char) (x)))
#else
# define MyIsDigit(x)	isdigit ((unsigned char) (x))
#endif


int 
colcomp (s1, s2)
     register char *s1;
     register char *s2;
{
  int hilo = 0;			/* comparison value */

  while (*s1 && *s2)
    {
      if  (  MyIsDigit(*s1)
          && MyIsDigit(*s2))
	{
	  hilo = (*s1 < *s2) ? -1 : (*s1 > *s2) ? 1 : 0;
	  ++s1;
	  ++s2;
	  while (MyIsDigit(*s1)
	     &&  MyIsDigit(*s2))
	    {
	      if (!hilo)
		hilo = (*s1 < *s2) ? -1 : (*s1 > *s2) ? 1 : 0;
	      ++s1;
	      ++s2;
	    }
	  if (MyIsDigit(*s1))
	    hilo = 1;		/* s2 is first */
	  if (MyIsDigit(*s2))
	    hilo = -1;		/* s1 is first */
	  if (hilo)
	    break;
	  continue;
	}
      if (MyIsDigit(*s1))
	{
	  hilo = -1;		/* s1 must come first */
	  break;
	}
      if (MyIsDigit(*s2))
	{
	  hilo = 1;		/* s2 must come first */
	  break;
	}
      hilo = (*s1 < *s2) ? -1 : (*s1 > *s2) ? 1 : 0;
      if (hilo)
	break;
      ++s1;
      ++s2;
    }
  if (*s1 && *s2)
    return (hilo);
  if (hilo)
    return (hilo);
  return ((*s1 < *s2) ? -1 : (*s1 > *s2) ? 1 : 0);
}

#ifdef TEST

#include <stdlib.h>

static int  qcmp(   const void      *fi1,
                    const void      *fi2)
{
    return colcomp(*(char**)fi1, *(char**)fi2);
}

int main( int argc, char *argv[], char *environ[]) {
  void *base;
  size_t nmemb = 0;
  size_t size = sizeof(char *);
  char *ca[] = {
    "999", "0", "10", "1", "01", "100", "010", "99", "00", "001", "099", "9"
  };
  char **cp;
  int i;

  if (argc > 1) {
    /* Sort use-provided list */
  } else {
    base = (void *) ca;
    nmemb = sizeof ca / size;
  }
  printf("argc is <%d>, nmemb = <%d>\n", argc, nmemb);

  printf("Before:\n");
  cp = (char **)base;
  for (i = 0; i < nmemb; ++i) {
    printf("%s\n", *cp++);
  }

  qsort((void *)base, nmemb, size, qcmp);

  printf("After:\n");
  cp = (char **)base;
  for (i = 0; i < nmemb; ++i) {
    printf("%s\n", *cp++);
  }

  exit(0);
}

#endif
