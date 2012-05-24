#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include "posix_types_32.h"
# else
#  include "posix_types_64.h"
# endif
#else
# ifdef __i386__
#  include "posix_types_32.h"
# elif defined(__LP64__)
#  include "posix_types_64.h"
# else
#  include "posix_types_x32.h"
# endif
#endif
