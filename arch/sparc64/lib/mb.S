/* mb.S: Out of line memory barriers.
 *
 * Copyright (C) 2005 David S. Miller (davem@davemloft.net)
 */

	/* These are here in an effort to more fully work around
	 * Spitfire Errata #51.  Essentially, if a memory barrier
	 * occurs soon after a mispredicted branch, the chip can stop
	 * executing instructions until a trap occurs.  Therefore, if
	 * interrupts are disabled, the chip can hang forever.
	 *
	 * It used to be believed that the memory barrier had to be
	 * right in the delay slot, but a case has been traced
	 * recently wherein the memory barrier was one instruction
	 * after the branch delay slot and the chip still hung.  The
	 * offending sequence was the following in sym_wakeup_done()
	 * of the sym53c8xx_2 driver:
	 *
	 *	call	sym_ccb_from_dsa, 0
	 *	 movge	%icc, 0, %l0
	 *	brz,pn	%o0, .LL1303
	 *	 mov	%o0, %l2
	 *	membar	#LoadLoad
	 *
	 * The branch has to be mispredicted for the bug to occur.
	 * Therefore, we put the memory barrier explicitly into a
	 * "branch always, predicted taken" delay slot to avoid the
	 * problem case.
	 */

	.text

99:	retl
	 nop

	.globl	mb
mb:	ba,pt	%xcc, 99b
	 membar	#LoadLoad | #LoadStore | #StoreStore | #StoreLoad
	.size	mb, .-mb

	.globl	rmb
rmb:	ba,pt	%xcc, 99b
	 membar	#LoadLoad
	.size	rmb, .-rmb

	.globl	wmb
wmb:	ba,pt	%xcc, 99b
	 membar	#StoreStore
	.size	wmb, .-wmb

	.globl	membar_storeload
membar_storeload:
	ba,pt	%xcc, 99b
	 membar	#StoreLoad
	.size	membar_storeload, .-membar_storeload

	.globl	membar_storeload_storestore
membar_storeload_storestore:
	ba,pt	%xcc, 99b
	 membar	#StoreLoad | #StoreStore
	.size	membar_storeload_storestore, .-membar_storeload_storestore

	.globl	membar_storeload_loadload
membar_storeload_loadload:
	ba,pt	%xcc, 99b
	 membar	#StoreLoad | #LoadLoad
	.size	membar_storeload_loadload, .-membar_storeload_loadload

	.globl	membar_storestore_loadstore
membar_storestore_loadstore:
	ba,pt	%xcc, 99b
	 membar	#StoreStore | #LoadStore
	.size	membar_storestore_loadstore, .-membar_storestore_loadstore
