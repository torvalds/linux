#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Function: legacy_pabort
 *
 * Params  : r0 = address of aborted instruction
 *
 * Returns : r0 = address of abort
 *	   : r1 = Simulated IFSR with section translation fault status
 *
 * Purpose : obtain information about current prefetch abort.
 */

	.align	5
ENTRY(legacy_pabort)
	mov	r1, #5
	mov	pc, lr
ENDPROC(legacy_pabort)
