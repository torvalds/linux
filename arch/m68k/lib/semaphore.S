/*
 *  linux/arch/m68k/lib/semaphore.S
 *
 *  Copyright (C) 1996  Linus Torvalds
 *
 *  m68k version by Andreas Schwab
 */

#include <linux/linkage.h>
#include <asm/semaphore.h>

/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 */
ENTRY(__down_failed)
	moveml %a0/%d0/%d1,-(%sp)
	movel %a1,-(%sp)
	jbsr __down
	movel (%sp)+,%a1
	moveml (%sp)+,%a0/%d0/%d1
	rts

ENTRY(__down_failed_interruptible)
	movel %a0,-(%sp)
	movel %d1,-(%sp)
	movel %a1,-(%sp)
	jbsr __down_interruptible
	movel (%sp)+,%a1
	movel (%sp)+,%d1
	movel (%sp)+,%a0
	rts

ENTRY(__down_failed_trylock)
	movel %a0,-(%sp)
	movel %d1,-(%sp)
	movel %a1,-(%sp)
	jbsr __down_trylock
	movel (%sp)+,%a1
	movel (%sp)+,%d1
	movel (%sp)+,%a0
	rts

ENTRY(__up_wakeup)
	moveml %a0/%d0/%d1,-(%sp)
	movel %a1,-(%sp)
	jbsr __up
	movel (%sp)+,%a1
	moveml (%sp)+,%a0/%d0/%d1
	rts

