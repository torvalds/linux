###############################################################################
#
# Virtual DMA driver for MN10300 serial ports
#
# Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
# Written by David Howells (dhowells@redhat.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public Licence
# as published by the Free Software Foundation; either version
# 2 of the Licence, or (at your option) any later version.
#
###############################################################################
#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/smp.h>
#include <asm/cpu-regs.h>
#include <asm/frame.inc>
#include <asm/timer-regs.h>
#include <proc/cache.h>
#include <unit/timex.h>
#include "mn10300-serial.h"

#define	SCxCTR	0x00
#define	SCxICR	0x04
#define	SCxTXB	0x08
#define	SCxRXB	0x09
#define	SCxSTR	0x0c
#define	SCxTIM	0x0d

	.text

###############################################################################
#
# serial port interrupt virtual DMA entry point
# - intended to run at interrupt priority 1 (not affected by local_irq_disable)
#
###############################################################################
	.balign	L1_CACHE_BYTES
ENTRY(mn10300_serial_vdma_interrupt)
#	or	EPSW_IE,psw			# permit overriding by
						# debugging interrupts
	movm	[d2,d3,a2,a3,exreg0],(sp)

	movhu	(IAGR),a2			# see if which interrupt is
						# pending
	and	IAGR_GN,a2
	add	a2,a2
	add	mn10300_serial_int_tbl,a2

	mov	(a2+),a3
	mov	(__iobase,a3),e2
	mov	(a2),a2
	jmp	(a2)

###############################################################################
#
# serial port receive interrupt virtual DMA entry point
# - intended to run at interrupt priority 1 (not affected by local_irq_disable)
# - stores data/status byte pairs in the ring buffer
# - induces a scheduler tick timer interrupt when done, which we then subvert
# on entry:
#	A3	struct mn10300_serial_port *
#	E2	I/O port base
#
###############################################################################
ENTRY(mn10300_serial_vdma_rx_handler)
	mov	(__rx_icr,a3),e3
	mov	GxICR_DETECT,d2
	movbu	d2,(e3)				# ACK the interrupt
	movhu	(e3),d2				# flush

	mov	(__rx_inp,a3),d3
	mov	d3,a2
	add	2,d3
	and	MNSC_BUFFER_SIZE-1,d3
	mov	(__rx_outp,a3),d2
	cmp	d3,d2
	beq	mnsc_vdma_rx_overflow

	mov	(__rx_buffer,a3),d2
	add	d2,a2
	movhu	(SCxSTR,e2),d2
	movbu	d2,(1,a2)
	movbu	(SCxRXB,e2),d2
	movbu	d2,(a2)
	mov	d3,(__rx_inp,a3)
	bset	MNSCx_RX_AVAIL,(__intr_flags,a3)

mnsc_vdma_rx_done:
	mov	(__tm_icr,a3),a2
	mov	GxICR_LEVEL_6|GxICR_ENABLE|GxICR_REQUEST|GxICR_DETECT,d2
	movhu	d2,(a2)				# request a slow interrupt
	movhu	(a2),d2				# flush

	movm	(sp),[d2,d3,a2,a3,exreg0]
	rti

mnsc_vdma_rx_overflow:
	bset	MNSCx_RX_OVERF,(__intr_flags,a3)
	bra	mnsc_vdma_rx_done

###############################################################################
#
# serial port transmit interrupt virtual DMA entry point
# - intended to run at interrupt priority 1 (not affected by local_irq_disable)
# - retrieves data bytes from the ring buffer and passes them to the serial port
# - induces a scheduler tick timer interrupt when done, which we then subvert
#	A3	struct mn10300_serial_port *
#	E2	I/O port base
#
###############################################################################
	.balign	L1_CACHE_BYTES
ENTRY(mn10300_serial_vdma_tx_handler)
	mov	(__tx_icr,a3),e3
	mov	GxICR_DETECT,d2
	movbu	d2,(e3)			# ACK the interrupt
	movhu	(e3),d2			# flush

	btst	0x01,(__tx_break,a3)	# handle transmit break request
	bne	mnsc_vdma_tx_break

	movbu	(SCxSTR,e2),d2		# don't try and transmit a char if the
					# buffer is not empty
	btst	SC01STR_TBF,d2		# (may have tried to jumpstart)
	bne	mnsc_vdma_tx_noint

	movbu	(__tx_xchar,a3),d2	# handle hi-pri XON/XOFF
	or	d2,d2
	bne	mnsc_vdma_tx_xchar

	mov	(__uart_state,a3),a2	# see if the TTY Tx queue has anything in it
	mov	(__xmit_tail,a2),d3
	mov	(__xmit_head,a2),d2
	cmp	d3,d2
	beq	mnsc_vdma_tx_empty

	mov	(__xmit_buffer,a2),d2	# get a char from the buffer and
					# transmit it
	movbu	(d3,d2),d2
	movbu	d2,(SCxTXB,e2)		# Tx

	inc	d3			# advance the buffer pointer
	and	__UART_XMIT_SIZE-1,d3
	mov	(__xmit_head,a2),d2
	mov	d3,(__xmit_tail,a2)

	sub	d3,d2			# see if we've written everything
	beq	mnsc_vdma_tx_empty

	and	__UART_XMIT_SIZE-1,d2	# see if we just made a hole
	cmp	__UART_XMIT_SIZE-2,d2
	beq	mnsc_vdma_tx_made_hole

mnsc_vdma_tx_done:
	mov	(__tm_icr,a3),a2
	mov	GxICR_LEVEL_6|GxICR_ENABLE|GxICR_REQUEST|GxICR_DETECT,d2
	movhu	d2,(a2)			# request a slow interrupt
	movhu	(a2),d2			# flush

mnsc_vdma_tx_noint:
	movm	(sp),[d2,d3,a2,a3,exreg0]
	rti

mnsc_vdma_tx_empty:
	mov	+(NUM2GxICR_LEVEL(CONFIG_MN10300_SERIAL_IRQ_LEVEL)|GxICR_DETECT),d2
	movhu	d2,(e3)			# disable the interrupt
	movhu	(e3),d2			# flush

	bset	MNSCx_TX_EMPTY,(__intr_flags,a3)
	bra	mnsc_vdma_tx_done

mnsc_vdma_tx_break:
	movhu	(SCxCTR,e2),d2		# turn on break mode
	or	SC01CTR_BKE,d2
	movhu	d2,(SCxCTR,e2)
	mov	+(NUM2GxICR_LEVEL(CONFIG_MN10300_SERIAL_IRQ_LEVEL)|GxICR_DETECT),d2
	movhu	d2,(e3)			# disable transmit interrupts on this
					# channel
	movhu	(e3),d2			# flush
	bra	mnsc_vdma_tx_noint

mnsc_vdma_tx_xchar:
	bclr	0xff,(__tx_xchar,a3)
	movbu	d2,(SCxTXB,e2)
	bra	mnsc_vdma_tx_done

mnsc_vdma_tx_made_hole:
	bset	MNSCx_TX_SPACE,(__intr_flags,a3)
	bra	mnsc_vdma_tx_done
