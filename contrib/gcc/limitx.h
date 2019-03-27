/* This administrivia gets added to the beginning of limits.h
   if the system has its own version of limits.h.  */

/* We use _GCC_LIMITS_H_ because we want this not to match
   any macros that the system's limits.h uses for its own purposes.  */
#ifndef _GCC_LIMITS_H_  /* Terminated in limity.h.  */
#define _GCC_LIMITS_H_

#ifndef _LIBC_LIMITS_H_
/* Use "..." so that we find syslimits.h only in this same directory.  */
#include "syslimits.h"
#endif
