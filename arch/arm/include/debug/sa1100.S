/* SPDX-License-Identifier: GPL-2.0-only */
/* arch/arm/include/debug/sa1100.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
*/

#define UTCR3		0x0c
#define UTDR		0x14
#define UTSR1		0x20
#define UTCR3_TXE	0x00000002	/* Transmit Enable                 */
#define UTSR1_TBY	0x00000001	/* Transmitter BusY (read)         */
#define UTSR1_TNF	0x00000004	/* Transmit FIFO Not Full (read)   */

		.macro	addruart, rp, rv, tmp
		mrc	p15, 0, \rp, c1, c0
		tst	\rp, #1			@ MMU enabled?
		moveq	\rp, #0x80000000	@ physical base address
		movne	\rp, #0xf8000000	@ virtual address

		@ We probe for the active serial port here, coherently with
		@ the comment in arch/arm/mach-sa1100/include/mach/uncompress.h.
		@ We assume r1 can be clobbered.

		@ see if Ser3 is active
		add	\rp, \rp, #0x00050000
		ldr	\rv, [\rp, #UTCR3]
		tst	\rv, #UTCR3_TXE

		@ if Ser3 is inactive, then try Ser1
		addeq	\rp, \rp, #(0x00010000 - 0x00050000)
		ldreq	\rv, [\rp, #UTCR3]
		tsteq	\rv, #UTCR3_TXE

		@ if Ser1 is inactive, then try Ser2
		addeq	\rp, \rp, #(0x00030000 - 0x00010000)
		ldreq	\rv, [\rp, #UTCR3]
		tsteq	\rv, #UTCR3_TXE

		@ clear top bits, and generate both phys and virt addresses
		lsl	\rp, \rp, #8
		lsr	\rp, \rp, #8
		orr	\rv, \rp, #0xf8000000	@ virtual
		orr	\rp, \rp, #0x80000000	@ physical

		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #UTDR]
		.endm

		.macro	waituartcts,rd,rx
		.endm

		.macro	waituarttxrdy,rd,rx
1001:		ldr	\rd, [\rx, #UTSR1]
		tst	\rd, #UTSR1_TNF
		beq	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #UTSR1]
		tst	\rd, #UTSR1_TBY
		bne	1001b
		.endm
