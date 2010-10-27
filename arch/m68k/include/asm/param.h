#ifndef _M68K_PARAM_H
#define _M68K_PARAM_H

#ifdef __uClinux__
#define EXEC_PAGESIZE	4096
#else
#define EXEC_PAGESIZE	8192
#endif

#include <asm-generic/param.h>

#endif /* _M68K_PARAM_H */
