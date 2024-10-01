#include <asm/bitsperlong.h>

#if __BITS_PER_LONG == 64
#include <asm/syscall_table_64.h>
#else
#include <asm/syscall_table_32.h>
#endif
