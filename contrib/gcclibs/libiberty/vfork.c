/* Emulate vfork using just plain fork, for systems without a real vfork.
   This function is in the public domain. */

/*

@deftypefn Supplemental int vfork (void)

Emulates @code{vfork} by calling @code{fork} and returning its value.

@end deftypefn

*/

#include "ansidecl.h"

extern int fork (void);

int
vfork (void)
{
  return (fork ());
}
