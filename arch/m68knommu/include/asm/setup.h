#ifdef __KERNEL__

#include <asm-m68k/setup.h>

/* We have a bigger command line buffer. */
#undef COMMAND_LINE_SIZE

#endif  /*  __KERNEL__  */

#define COMMAND_LINE_SIZE	512
