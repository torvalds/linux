#define BUILD_VDSO32

#ifndef CONFIG_CC_OPTIMIZE_FOR_SIZE
#undef CONFIG_OPTIMIZE_INLINING
#endif

#undef CONFIG_X86_PPRO_FENCE

#include "../vclock_gettime.c"
