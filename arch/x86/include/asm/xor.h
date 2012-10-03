#ifdef CONFIG_KMEMCHECK
/* kmemcheck doesn't handle MMX/SSE/SSE2 instructions */
# include <asm-generic/xor.h>
#else
#ifdef CONFIG_X86_32
# include <asm/xor_32.h>
#else
# include <asm/xor_64.h>
#endif
#endif
