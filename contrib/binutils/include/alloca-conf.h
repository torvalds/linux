#include "config.h"

#if defined(__GNUC__) && !defined(C_ALLOCA)
# ifndef alloca
#  define alloca __builtin_alloca
# endif
#else /* ! defined (__GNUC__) */
#  ifdef _AIX
 #pragma alloca
#  else
#  if defined(HAVE_ALLOCA_H) && !defined(C_ALLOCA)
#   include <alloca.h>
#  else /* ! defined (HAVE_ALLOCA_H) */
#   ifdef __STDC__
extern PTR alloca (size_t);
#   else /* ! defined (__STDC__) */
extern PTR alloca ();
#   endif /* ! defined (__STDC__) */
#  endif /* ! defined (HAVE_ALLOCA_H) */
#  ifdef _WIN32
#   include <malloc.h>
#  endif
# endif /* ! defined (_AIX) */
#endif /* ! defined (__GNUC__) */
