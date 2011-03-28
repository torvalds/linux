#ifdef CONFIG_MMU
#include "signal_mm.c"
#else
#include "signal_no.c"
#endif
