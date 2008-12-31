#ifdef CONFIG_X86_32
# include "cmpxchg_32.h"
#else
# include "cmpxchg_64.h"
#endif
