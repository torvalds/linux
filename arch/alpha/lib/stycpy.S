#include "strcpy.S"
#ifdef CONFIG_ALPHA_EV67
#include "ev67-strcat.S"
#else
#include "strcat.S"
#endif
#ifdef CONFIG_ALPHA_EV6
#include "ev6-stxcpy.S"
#else
#include "stxcpy.S"
#endif
