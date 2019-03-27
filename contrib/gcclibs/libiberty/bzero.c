/* Portable version of bzero for systems without it.
   This function is in the public domain.  */

/*

@deftypefn Supplemental void bzero (char *@var{mem}, int @var{count})

Zeros @var{count} bytes starting at @var{mem}.  Use of this function
is deprecated in favor of @code{memset}.

@end deftypefn

*/

#include <stddef.h>

extern void *memset(void *, int, size_t);

void
bzero (void *to, size_t count)
{
  memset (to, 0, count);
}
