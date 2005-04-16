/*
 *  linux/arch/m68k/lib/semaphore.S
 *
 *  Copyright (C) 1996  Linus Torvalds
 *
 *  m68k version by Andreas Schwab
 *
 *  MAR/1999 -- modified to support ColdFire (gerg@snapgear.com)
 */

#include <linux/linkage.h>
#include <linux/config.h>
#include <asm/semaphore.h>

/*
 * "down_failed" is called with the eventual return address
 * in %a0, and the address of the semaphore in %a1. We need
 * to increment the number of waiters on the semaphore,
 * call "__down()", and then eventually return to try again.
 */
ENTRY(__down_failed)
#ifdef CONFIG_COLDFIRE
	subl #12,%sp
	moveml %a0/%d0/%d1,(%sp)
#else
	moveml %a0/%d0/%d1,-(%sp)
#endif
	movel %a1,-(%sp)
	jbsr __down
	movel (%sp)+,%a1
	movel (%sp)+,%d0
	movel (%sp)+,%d1
	rts

ENTRY(__down_failed_interruptible)
	movel %a0,-(%sp)
	movel %d1,-(%sp)
	movel %a1,-(%sp)
	jbsr __down_interruptible
	movel (%sp)+,%a1
	movel (%sp)+,%d1
	rts

ENTRY(__up_wakeup)
#ifdef CONFIG_COLDFIRE
	subl #12,%sp
	moveml %a0/%d0/%d1,(%sp)
#else
	moveml %a0/%d0/%d1,-(%sp)
#endif
	movel %a1,-(%sp)
	jbsr __up
	movel (%sp)+,%a1
	movel (%sp)+,%d0
	movel (%sp)+,%d1
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

