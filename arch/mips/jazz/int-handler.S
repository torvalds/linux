/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle and Andreas Busse
 *
 * Jazz family specific interrupt stuff
 *
 * To do: On Jazz machines we remap some non-ISA interrupts to ISA
 *        interrupts.  These interrupts should use their own vectors.
 *        Squeeze the last cycles out of the handlers.  Only a dead
 *        cycle is a good cycle.
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/jazz.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/*
 * jazz_handle_int: Interrupt handler for the ACER Pica-61 boards
 */
		.set	noreorder

		NESTED(jazz_handle_int, PT_SIZE, ra)
		.set	noat
		SAVE_ALL
		CLI
		.set	at

		/*
		 * Get pending interrupts
		 */
		mfc0	t0,CP0_CAUSE		# get pending interrupts
		mfc0	t1,CP0_STATUS		# get enabled interrupts
		and	t0,t1			# isolate allowed ones
		andi	t0,0xff00		# isolate pending bits
		beqz	t0,3f
		sll	t0,16			# delay slot

		/*
		 * Find irq with highest priority
		 * FIXME: This is slow - use binary search
		 */
		la	t1,ll_vectors
1:		bltz	t0,2f			# found pending irq
		sll	t0,1
		b	1b
		subu	t1,PTRSIZE		# delay slot

		/*
		 * Do the low-level stuff
		 */
2:		lw	t0,(t1)
		jr	t0
		nop				# delay slot
		END(jazz_handle_int)

ll_sw0:		li	s1,~IE_SW0
		mfc0	t0,CP0_CAUSE
		and	t0,s1
		mtc0	t0,CP0_CAUSE
		PANIC("Unimplemented sw0 handler")

ll_sw1:		li	s1,~IE_SW1
		mfc0	t0,CP0_CAUSE
		and	t0,s1
		mtc0	t0,CP0_CAUSE
		PANIC("Unimplemented sw1 handler")

ll_local_dma:	li	s1,~IE_IRQ0
		PANIC("Unimplemented local_dma handler")

ll_local_dev:	lbu	t0,JAZZ_IO_IRQ_SOURCE
#if PTRSIZE == 8	/* True 64 bit kernel */
		dsll	t0,1
#endif
		.set	reorder
		LONG_L	t0,local_vector(t0)
		jr	t0
		.set	noreorder

/*
 * The braindead PICA hardware gives us no way to distinguish if we really
 * received interrupt 7 from the (E)ISA bus or if we just received an
 * interrupt with no findable cause.  This sometimes happens with braindead
 * cards.  Oh well - for all the Jazz boxes slots are more or less just
 * whistles and bells and we're aware of the problem.
 */
ll_isa_irq:	lw	a0, JAZZ_EISA_IRQ_ACK

		jal	do_IRQ
		 move	a1,sp

		j	ret_from_irq
		nop

/*
 * Hmm...  This is not just a plain PC clone so the question is
 * which devices on Jazz machines can generate an (E)ISA NMI?
 * (Writing to nonexistent memory?)
 */
ll_isa_nmi:	li	s1,~IE_IRQ3
		PANIC("Unimplemented isa_nmi handler")

/*
 * Timer IRQ - remapped to be more similar to an IBM compatible.
 *
 * The timer interrupt is handled specially to ensure that the jiffies
 * variable is updated at all times.  Specifically, the timer interrupt is
 * just like the complete handlers except that it is invoked with interrupts
 * disabled and should never re-enable them.  If other interrupts were
 * allowed to be processed while the timer interrupt is active, then the
 * other interrupts would have to avoid using the jiffies variable for delay
 * and interval timing operations to avoid hanging the system.
 */
ll_timer:	lw	zero,JAZZ_TIMER_REGISTER # timer irq cleared on read
		li	s1,~IE_IRQ4

		li	a0, JAZZ_TIMER_IRQ
		jal	do_IRQ
		 move	a1,sp

		mfc0	t0,CP0_STATUS		# disable interrupts again
		ori	t0,1
		xori	t0,1
		mtc0	t0,CP0_STATUS

		j	ret_from_irq
		 nop

/*
 * CPU count/compare IRQ (unused)
 */
ll_count:	j	ret_from_irq
		 mtc0	zero,CP0_COMPARE

#if 0
/*
 * Call the handler for the interrupt
 * (Currently unused)
 */
call_real:	/*
		 * temporarily disable interrupt
		 */
		mfc0	t2,CP0_STATUS
		and	t2,s1
		mtc0	t2,CP0_STATUS
		nor	s1,zero,s1
		jal	do_IRQ

		/*
		 * reenable interrupt
		 */
		mfc0	t2,CP0_STATUS
		or	t2,s1
		mtc0	t2,CP0_STATUS
		j	ret_from_irq
#endif

		.data
		PTR	ll_sw0			# SW0
		PTR	ll_sw1			# SW1
		PTR	ll_local_dma		# Local DMA
		PTR	ll_local_dev		# Local devices
		PTR	ll_isa_irq		# ISA IRQ
		PTR	ll_isa_nmi		# ISA NMI
		PTR	ll_timer		# Timer
ll_vectors:	PTR	ll_count		# Count/Compare IRQ

		/*
		 * Interrupt handlers for local devices.
		 */
		.text
		.set	reorder
loc_no_irq:	PANIC("Unimplemented loc_no_irq handler")
/*
 * Parallel port IRQ
 */
loc_parallel:	li	s1,~JAZZ_IE_PARALLEL
		li	a0,JAZZ_PARALLEL_IRQ
		b	loc_call

/*
 * Floppy IRQ
 */
loc_floppy:	li	s1,~JAZZ_IE_FLOPPY
		li	a0,JAZZ_FLOPPY_IRQ
		b	loc_call

/*
 * Sound IRQ
 */
loc_sound:	PANIC("Unimplemented loc_sound handler")
loc_video:	PANIC("Unimplemented loc_video handler")

/*
 * Ethernet interrupt handler
 */
loc_ethernet: 	li	s1,~JAZZ_IE_ETHERNET
		li	a0,JAZZ_ETHERNET_IRQ
		b	loc_call

/*
 * SCSI interrupt handler
 */
loc_scsi:	li	s1,~JAZZ_IE_SCSI
		li	a0,JAZZ_SCSI_IRQ
		b	loc_call

/*
 * Keyboard interrupt handler
 */
loc_keyboard:	li	s1,~JAZZ_IE_KEYBOARD
		li	a0,JAZZ_KEYBOARD_IRQ
		b	loc_call

/*
 * Mouse interrupt handler
 */
loc_mouse:	li	s1,~JAZZ_IE_MOUSE
		li	a0,JAZZ_MOUSE_IRQ
		b	loc_call

/*
 * Serial port 1 IRQ
 */
loc_serial1:	li	s1,~JAZZ_IE_SERIAL1
		li	a0,JAZZ_SERIAL1_IRQ
		b	loc_call

/*
 * Serial port 2 IRQ
 */
loc_serial2:	li	s1,~JAZZ_IE_SERIAL2
		li	a0,JAZZ_SERIAL2_IRQ
		b	loc_call

/*
 * Call the interrupt handler for an interrupt generated by a
 * local device.
 */
loc_call:	/*
		 * Temporarily disable interrupt source
		 */
		lhu	t2,JAZZ_IO_IRQ_ENABLE
		and	t2,s1
		sh	t2,JAZZ_IO_IRQ_ENABLE

 		nor	s1,zero,s1
		jal	do_IRQ

 		/*
 		 * Reenable interrupt
 		 */
		lhu	t2,JAZZ_IO_IRQ_ENABLE
 		or	t2,s1
		sh	t2,JAZZ_IO_IRQ_ENABLE

 		j	ret_from_irq

/*
 * "Jump extender" to reach spurious_interrupt
 */
3:		j	spurious_interrupt

/*
 * Vectors for interrupts generated by local devices
 */
		.data
local_vector:	PTR	loc_no_irq
		PTR	loc_parallel
		PTR	loc_floppy
		PTR	loc_sound
		PTR	loc_video
		PTR	loc_ethernet
		PTR	loc_scsi
		PTR	loc_keyboard
		PTR	loc_mouse
		PTR	loc_serial1
		PTR	loc_serial2
