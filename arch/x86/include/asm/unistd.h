#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "unistd_32.h"
# else
#  include "unistd_64.h"
# endif
#else
# ifdef __i386__
#  include "unistd_32.h"
# else
#  include "unistd_64.h"
# endif
#endif
