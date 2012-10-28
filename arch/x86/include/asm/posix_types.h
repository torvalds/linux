#ifdef __KERNEL__
# ifdef CONFIG_X86_32
#  include <asm/posix_types_32.h>
# else
#  include <asm/posix_types_64.h>
# endif
#else
# ifdef __i386__
#  include <asm/posix_types_32.h>
# elif defined(__ILP32__)
#  include <asm/posix_types_x32.h>
# else
#  include <asm/posix_types_64.h>
# endif
#endif
