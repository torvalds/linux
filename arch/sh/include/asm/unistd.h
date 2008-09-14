#ifdef __KERNEL__
# ifdef CONFIG_SUPERH32
#  include "unistd_32.h"
# else
#  include "unistd_64.h"
# endif
#else
# ifdef __SH5__
#  include "unistd_64.h"
# else
#  include "unistd_32.h"
# endif
#endif
