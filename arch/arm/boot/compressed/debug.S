#include <linux/linkage.h>
#include <asm/assembler.h>

#include CONFIG_DEBUG_LL_INCLUDE

ENTRY(putc)
	addruart r1, r2, r3
	waituart r3, r1
	senduart r0, r1
	busyuart r3, r1
	mov	 pc, lr
ENDPROC(putc)
