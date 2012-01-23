#include <linux/linkage.h>

/**stack point address in sram*/
#define SP_IN_SRAM	0xf0003ff0

ENTRY(save_sp)
	mov r0, r13
   	ldr  r13, =SP_IN_SRAM
	mov pc,lr
ENDPROC(save_sp)

ENTRY(restore_sp)
      	mov r13, r0
      	mov pc,lr
ENDPROC(restore_sp)


