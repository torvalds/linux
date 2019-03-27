/* bcmp
   This function is in the public domain.  */

/*

@deftypefn Supplemental int bcmp (char *@var{x}, char *@var{y}, int @var{count})

Compares the first @var{count} bytes of two areas of memory.  Returns
zero if they are the same, nonzero otherwise.  Returns zero if
@var{count} is zero.  A nonzero result only indicates a difference,
it does not indicate any sorting order (say, by having a positive
result mean @var{x} sorts before @var{y}).

@end deftypefn

*/

#include <stddef.h>

extern int memcmp(const void *, const void *, size_t);

int
bcmp (const void *s1, const void *s2, size_t count)
{
  return memcmp (s1, s2, count);
}

