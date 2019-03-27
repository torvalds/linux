#ifndef GCC_GSTAB_H
#define GCC_GSTAB_H

#define __define_stab(NAME, CODE, STRING) NAME=CODE,

enum __stab_debug_code
{
#include "stab.def"
LAST_UNUSED_STAB_CODE
};

#undef __define_stab

#endif /* ! GCC_GSTAB_H */
