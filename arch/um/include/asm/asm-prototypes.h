#include <asm-generic/asm-prototypes.h>
#include <asm/checksum.h>

#ifdef CONFIG_UML_X86
extern void cmpxchg8b_emu(void);
#endif
