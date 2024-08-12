#include "strncpy.S"
#ifdef CONFIG_ALPHA_EV67
#include "ev67-strncat.S"
#else
#include "strncat.S"
#endif
#ifdef CONFIG_ALPHA_EV6
#include "ev6-stxncpy.S"
#else
#include "stxncpy.S"
#endif
