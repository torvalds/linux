#ifndef _H8300_SWAB_H
#define _H8300_SWAB_H

#include <asm/types.h>

#if defined(__GNUC__) && !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __SWAB_64_THRU_32__
#endif

#endif /* _H8300_SWAB_H */
