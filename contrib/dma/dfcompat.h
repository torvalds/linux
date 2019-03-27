#ifndef DFCOMPAT_H
#define DFCOMPAT_H

#define _GNU_SOURCE

#include <sys/types.h>

#ifndef __DECONST
#define __DECONST(type, var)    ((type)(uintptr_t)(const void *)(var))
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_REALLOCF
void *reallocf(void *, size_t);
#endif

#ifndef HAVE_GETPROGNAME
const char *getprogname(void);
#endif

#endif /* DFCOMPAT_H */
