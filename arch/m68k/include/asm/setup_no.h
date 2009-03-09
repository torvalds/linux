#ifdef __KERNEL__

#include <asm/setup_mm.h>

/* We have a bigger command line buffer. */
#undef COMMAND_LINE_SIZE

#endif  /*  __KERNEL__  */

#define COMMAND_LINE_SIZE	512
