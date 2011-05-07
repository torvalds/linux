/* MN10300 CPU core caching macros -*- asm -*-
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */


###############################################################################
#
# Invalidate the instruction cache.
#	A0: Should hold CHCTR
#	D0: Should have been read from CHCTR
#	D1: Will be clobbered
#
# On some cores it is necessary to disable the icache whilst we do this.
#
###############################################################################
	.macro invalidate_icache,disable_irq

#if defined(CONFIG_AM33_2) || defined(CONFIG_AM33_3)
	.if \disable_irq
	# don't want an interrupt routine seeing a disabled cache
	mov	epsw,d1
	and	~EPSW_IE,epsw
	or	EPSW_NMID,epsw
	nop
	nop
	.endif

	# disable the icache
	and	~CHCTR_ICEN,d0
	movhu	d0,(a0)

	# and wait for it to calm down
	setlb
	movhu	(a0),d0
	btst	CHCTR_ICBUSY,d0
	lne

	# invalidate
	or	CHCTR_ICINV,d0
	movhu	d0,(a0)

	# wait for the cache to finish
	setlb
	movhu	(a0),d0
	btst	CHCTR_ICBUSY,d0
	lne

	# and reenable it
	or	CHCTR_ICEN,d0
	movhu	d0,(a0)
	movhu	(a0),d0

	.if \disable_irq
	LOCAL_IRQ_RESTORE(d1)
	.endif

#else /* CONFIG_AM33_2 || CONFIG_AM33_3 */

	# invalidate
	or	CHCTR_ICINV,d0
	movhu	d0,(a0)
	movhu	(a0),d0

#endif /* CONFIG_AM33_2 || CONFIG_AM33_3 */
	.endm

###############################################################################
#
# Invalidate the data cache.
#	A0: Should hold CHCTR
#	D0: Should have been read from CHCTR
#	D1: Will be clobbered
#
# On some cores it is necessary to disable the dcache whilst we do this.
#
###############################################################################
	.macro invalidate_dcache,disable_irq

#if defined(CONFIG_AM33_2) || defined(CONFIG_AM33_3)
	.if \disable_irq
	# don't want an interrupt routine seeing a disabled cache
	mov	epsw,d1
	and	~EPSW_IE,epsw
	or	EPSW_NMID,epsw
	nop
	nop
	.endif
	
	# disable the dcache
	and	~CHCTR_DCEN,d0
	movhu	d0,(a0)

	# and wait for it to calm down
	setlb
	movhu	(a0),d0
	btst	CHCTR_DCBUSY,d0
	lne

	# invalidate
	or	CHCTR_DCINV,d0
	movhu	d0,(a0)

	# wait for the cache to finish
	setlb
	movhu	(a0),d0
	btst	CHCTR_DCBUSY,d0
	lne

	# and reenable it
	or	CHCTR_DCEN,d0
	movhu	d0,(a0)
	movhu	(a0),d0

	.if \disable_irq
	LOCAL_IRQ_RESTORE(d1)
	.endif

#else /* CONFIG_AM33_2 || CONFIG_AM33_3 */

	# invalidate
	or	CHCTR_DCINV,d0
	movhu	d0,(a0)
	movhu	(a0),d0

#endif /* CONFIG_AM33_2 || CONFIG_AM33_3 */
	.endm
