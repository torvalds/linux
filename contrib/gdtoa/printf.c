#ifdef __sun
#define Use_GDTOA_Qtype
#else
#if defined(__i386) || defined(__x86_64)
#define Use_GDTOA_for_i386_long_double
#endif
#endif
#include "printf.c0"
