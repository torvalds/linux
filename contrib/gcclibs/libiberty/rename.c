/* rename -- rename a file
   This function is in the public domain. */

/*

@deftypefn Supplemental int rename (const char *@var{old}, const char *@var{new})

Renames a file from @var{old} to @var{new}.  If @var{new} already
exists, it is removed.

@end deftypefn

*/

#include "ansidecl.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

int
rename (const char *zfrom, const char *zto)
{
  if (link (zfrom, zto) < 0)
    {
      if (errno != EEXIST)
	return -1;
      if (unlink (zto) < 0
	  || link (zfrom, zto) < 0)
	return -1;
    }
  return unlink (zfrom);
}
