#ifdef CONFIG_MMU
#include "checksum_mm.c"
#else
#include "checksum_no.c"
#endif
