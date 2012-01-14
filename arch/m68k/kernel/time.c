#if defined(CONFIG_MMU) && !defined(CONFIG_COLDFIRE)
#include "time_mm.c"
#else
#include "time_no.c"
#endif
