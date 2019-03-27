
 /*
  * What follows is an attempt to unify varargs.h and stdarg.h. I'd rather
  * have this than #ifdefs all over the code.
  */

#ifdef __STDC__
#include <stdarg.h>
#define	VARARGS(func,type,arg) func(type arg, ...)
#define	VASTART(ap,type,name)  va_start(ap,name)
#define	VAEND(ap)              va_end(ap)
#else
#include <varargs.h>
#define	VARARGS(func,type,arg) func(va_alist) va_dcl
#define	VASTART(ap,type,name)  {type name; va_start(ap); name = va_arg(ap, type)
#define	VAEND(ap)              va_end(ap);}
#endif

extern char *percent_m();
