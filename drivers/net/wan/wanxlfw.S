.psize 0
/*
  wanXL serial card driver for Linux
  card firmware part

  Copyright (C) 2003 Krzysztof Halasa <khc@pm.waw.pl>

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License
  as published by the Free Software Foundation.




	DPRAM BDs:
	0x000 - 0x050 TX#0	0x050 - 0x140 RX#0
	0x140 - 0x190 TX#1	0x190 - 0x280 RX#1
	0x280 - 0x2D0 TX#2	0x2D0 - 0x3C0 RX#2
	0x3C0 - 0x410 TX#3	0x410 - 0x500 RX#3


	000 5FF 1536 Bytes Dual-Port RAM User Data / BDs
	600 6FF 256 Bytes Dual-Port RAM User Data / BDs
	700 7FF 256 Bytes Dual-Port RAM User Data / BDs
	C00 CBF 192 Bytes Dual-Port RAM Parameter RAM Page 1
	D00 DBF 192 Bytes Dual-Port RAM Parameter RAM Page 2
	E00 EBF 192 Bytes Dual-Port RAM Parameter RAM Page 3
	F00 FBF 192 Bytes Dual-Port RAM Parameter RAM Page 4

	local interrupts		    level
	NMI					7
	PIT timer, CPM (RX/TX complete)		4
	PCI9060	DMA and PCI doorbells		3
	Cable - not used			1
*/

#include <linux/hdlc.h>
#include "wanxl.h"

/* memory addresses and offsets */

MAX_RAM_SIZE	= 16 * 1024 * 1024	// max RAM supported by hardware

PCI9060_VECTOR	= 0x0000006C
CPM_IRQ_BASE	= 0x40
ERROR_VECTOR	= CPM_IRQ_BASE * 4
SCC1_VECTOR	= (CPM_IRQ_BASE + 0x1E) * 4
SCC2_VECTOR	= (CPM_IRQ_BASE + 0x1D) * 4
SCC3_VECTOR	= (CPM_IRQ_BASE + 0x1C) * 4
SCC4_VECTOR	= (CPM_IRQ_BASE + 0x1B) * 4
CPM_IRQ_LEVEL	= 4
TIMER_IRQ	= 128
TIMER_IRQ_LEVEL = 4
PITR_CONST	= 0x100 + 16		// 1 Hz timer

MBAR		= 0x0003FF00

VALUE_WINDOW	= 0x40000000
ORDER_WINDOW	= 0xC0000000

PLX		= 0xFFF90000

CSRA		= 0xFFFB0000
CSRB		= 0xFFFB0002
CSRC		= 0xFFFB0004
CSRD		= 0xFFFB0006
STATUS_CABLE_LL		= 0x2000
STATUS_CABLE_DTR	= 0x1000

DPRBASE		= 0xFFFC0000

SCC1_BASE	= DPRBASE + 0xC00
MISC_BASE	= DPRBASE + 0xCB0
SCC2_BASE	= DPRBASE + 0xD00
SCC3_BASE	= DPRBASE + 0xE00
SCC4_BASE	= DPRBASE + 0xF00

// offset from SCCx_BASE
// SCC_xBASE contain offsets from DPRBASE and must be divisible by 8
SCC_RBASE	= 0		// 16-bit RxBD base address
SCC_TBASE	= 2		// 16-bit TxBD base address
SCC_RFCR	= 4		// 8-bit Rx function code
SCC_TFCR	= 5		// 8-bit Tx function code
SCC_MRBLR	= 6		// 16-bit maximum Rx buffer length
SCC_C_MASK	= 0x34		// 32-bit CRC constant
SCC_C_PRES	= 0x38		// 32-bit CRC preset
SCC_MFLR	= 0x46		// 16-bit max Rx frame length (without flags)

REGBASE		= DPRBASE + 0x1000
PICR		= REGBASE + 0x026	// 16-bit periodic irq control
PITR		= REGBASE + 0x02A	// 16-bit periodic irq timing
OR1		= REGBASE + 0x064	// 32-bit RAM bank #1 options
CICR		= REGBASE + 0x540	// 32(24)-bit CP interrupt config
CIMR		= REGBASE + 0x548	// 32-bit CP interrupt mask
CISR		= REGBASE + 0x54C	// 32-bit CP interrupts in-service
PADIR		= REGBASE + 0x550	// 16-bit PortA data direction bitmap
PAPAR		= REGBASE + 0x552	// 16-bit PortA pin assignment bitmap
PAODR		= REGBASE + 0x554	// 16-bit PortA open drain bitmap
PADAT		= REGBASE + 0x556	// 16-bit PortA data register

PCDIR		= REGBASE + 0x560	// 16-bit PortC data direction bitmap
PCPAR		= REGBASE + 0x562	// 16-bit PortC pin assignment bitmap
PCSO		= REGBASE + 0x564	// 16-bit PortC special options
PCDAT		= REGBASE + 0x566	// 16-bit PortC data register
PCINT		= REGBASE + 0x568	// 16-bit PortC interrupt control
CR		= REGBASE + 0x5C0	// 16-bit Command register

SCC1_REGS	= REGBASE + 0x600
SCC2_REGS	= REGBASE + 0x620
SCC3_REGS	= REGBASE + 0x640
SCC4_REGS	= REGBASE + 0x660
SICR		= REGBASE + 0x6EC	// 32-bit SI clock route

// offset from SCCx_REGS
SCC_GSMR_L	= 0x00	// 32 bits
SCC_GSMR_H	= 0x04	// 32 bits
SCC_PSMR	= 0x08	// 16 bits
SCC_TODR	= 0x0C	// 16 bits
SCC_DSR		= 0x0E	// 16 bits
SCC_SCCE	= 0x10	// 16 bits
SCC_SCCM	= 0x14	// 16 bits
SCC_SCCS	= 0x17	// 8 bits

#if QUICC_MEMCPY_USES_PLX
	.macro memcpy_from_pci src, dest, len // len must be < 8 MB
	addl #3, \len
	andl #0xFFFFFFFC, \len		// always copy n * 4 bytes
	movel \src, PLX_DMA_0_PCI
	movel \dest, PLX_DMA_0_LOCAL
	movel \len, PLX_DMA_0_LENGTH
	movel #0x0103, PLX_DMA_CMD_STS	// start channel 0 transfer
	bsr memcpy_from_pci_run
	.endm

	.macro memcpy_to_pci src, dest, len
	addl #3, \len
	andl #0xFFFFFFFC, \len		// always copy n * 4 bytes
	movel \src, PLX_DMA_1_LOCAL
	movel \dest, PLX_DMA_1_PCI
	movel \len, PLX_DMA_1_LENGTH
	movel #0x0301, PLX_DMA_CMD_STS	// start channel 1 transfer
	bsr memcpy_to_pci_run
	.endm

#else

	.macro memcpy src, dest, len	// len must be < 65536 bytes
	movel %d7, -(%sp)		// src and dest must be < 256 MB
	movel \len, %d7			// bits 0 and 1
	lsrl #2, \len
	andl \len, \len
	beq 99f				// only 0 - 3 bytes
	subl #1, \len			// for dbf
98:	movel (\src)+, (\dest)+
	dbfw \len, 98b
99:	movel %d7, \len
	btstl #1, \len
	beq 99f
	movew (\src)+, (\dest)+
99:	btstl #0, \len
	beq 99f
	moveb (\src)+, (\dest)+
99:
	movel (%sp)+, %d7
	.endm

	.macro memcpy_from_pci src, dest, len
	addl #VALUE_WINDOW, \src
	memcpy \src, \dest, \len
	.endm

	.macro memcpy_to_pci src, dest, len
	addl #VALUE_WINDOW, \dest
	memcpy \src, \dest, \len
	.endm
#endif


	.macro wait_for_command
99:	btstl #0, CR
	bne 99b
	.endm




/****************************** card initialization *******************/
	.text
	.global _start
_start:	bra init

	.org _start + 4
ch_status_addr:	.long 0, 0, 0, 0
rx_descs_addr:	.long 0

init:
#if DETECT_RAM
	movel OR1, %d0
	andl #0xF00007FF, %d0		// mask AMxx bits
	orl #0xFFFF800 & ~(MAX_RAM_SIZE - 1), %d0 // update RAM bank size
	movel %d0, OR1
#endif

	addl #VALUE_WINDOW, rx_descs_addr // PCI addresses of shared data
	clrl %d0			// D0 = 4 * port
init_1:	tstl ch_status_addr(%d0)
	beq init_2
	addl #VALUE_WINDOW, ch_status_addr(%d0)
init_2:	addl #4, %d0
	cmpl #4 * 4, %d0
	bne init_1

	movel #pci9060_interrupt, PCI9060_VECTOR
	movel #error_interrupt, ERROR_VECTOR
	movel #port_interrupt_1, SCC1_VECTOR
	movel #port_interrupt_2, SCC2_VECTOR
	movel #port_interrupt_3, SCC3_VECTOR
	movel #port_interrupt_4, SCC4_VECTOR
	movel #timer_interrupt, TIMER_IRQ * 4

	movel #0x78000000, CIMR		// only SCCx IRQs from CPM
	movew #(TIMER_IRQ_LEVEL << 8) + TIMER_IRQ, PICR	// interrupt from PIT
	movew #PITR_CONST, PITR

	// SCC1=SCCa SCC2=SCCb SCC3=SCCc SCC4=SCCd prio=4 HP=-1 IRQ=64-79
	movel #0xD41F40 + (CPM_IRQ_LEVEL << 13), CICR
	movel #0x543, PLX_DMA_0_MODE	// 32-bit, Ready, Burst, IRQ
	movel #0x543, PLX_DMA_1_MODE
	movel #0x0, PLX_DMA_0_DESC	// from PCI to local
	movel #0x8, PLX_DMA_1_DESC	// from local to PCI
	movel #0x101, PLX_DMA_CMD_STS	// enable both DMA channels
	// enable local IRQ, DMA, doorbells and PCI IRQ
	orl #0x000F0300, PLX_INTERRUPT_CS

#if DETECT_RAM
	bsr ram_test
#else
	movel #1, PLX_MAILBOX_5		// non-zero value = init complete
#endif
	bsr check_csr

	movew #0xFFFF, PAPAR		// all pins are clocks/data
	clrw PADIR			// first function
	clrw PCSO			// CD and CTS always active


/****************************** main loop *****************************/

main:	movel channel_stats, %d7	// D7 = doorbell + irq status
	clrl channel_stats

	tstl %d7
	bne main_1
	// nothing to do - wait for next event
	stop #0x2200			// supervisor + IRQ level 2
	movew #0x2700, %sr		// disable IRQs again
	bra main

main_1:	clrl %d0			// D0 = 4 * port
	clrl %d6			// D6 = doorbell to host value

main_l: btstl #DOORBELL_TO_CARD_CLOSE_0, %d7
	beq main_op
	bclrl #DOORBELL_TO_CARD_OPEN_0, %d7 // in case both bits are set
	bsr close_port
main_op:
	btstl #DOORBELL_TO_CARD_OPEN_0, %d7
	beq main_cl
	bsr open_port
main_cl:
	btstl #DOORBELL_TO_CARD_TX_0, %d7
	beq main_txend
	bsr tx
main_txend:
	btstl #TASK_SCC_0, %d7
	beq main_next
	bsr tx_end
	bsr rx

main_next:
	lsrl #1, %d7			// port status for next port
	addl #4, %d0			// D0 = 4 * next port
	cmpl #4 * 4, %d0
	bne main_l
	movel %d6, PLX_DOORBELL_FROM_CARD // signal the host
	bra main


/****************************** open port *****************************/

open_port:				// D0 = 4 * port, D6 = doorbell to host
	movel ch_status_addr(%d0), %a0	// A0 = port status address
	tstl STATUS_OPEN(%a0)
	bne open_port_ret		// port already open
	movel #1, STATUS_OPEN(%a0)	// confirm the port is open
// setup BDs
	clrl tx_in(%d0)
	clrl tx_out(%d0)
	clrl tx_count(%d0)
	clrl rx_in(%d0)

	movel SICR, %d1			// D1 = clock settings in SICR
	andl clocking_mask(%d0), %d1
	cmpl #CLOCK_TXFROMRX, STATUS_CLOCKING(%a0)
	bne open_port_clock_ext
	orl clocking_txfromrx(%d0), %d1
	bra open_port_set_clock

open_port_clock_ext:
	orl clocking_ext(%d0), %d1
open_port_set_clock:
	movel %d1, SICR			// update clock settings in SICR

	orw #STATUS_CABLE_DTR, csr_output(%d0)	// DTR on
	bsr check_csr			// call with disabled timer interrupt

// Setup TX descriptors
	movel first_buffer(%d0), %d1	// D1 = starting buffer address
	movel tx_first_bd(%d0), %a1	// A1 = starting TX BD address
	movel #TX_BUFFERS - 2, %d2	// D2 = TX_BUFFERS - 1 counter
	movel #0x18000000, %d3		// D3 = initial TX BD flags: Int + Last
	cmpl #PARITY_NONE, STATUS_PARITY(%a0)
	beq open_port_tx_loop
	bsetl #26, %d3			// TX BD flag: Transmit CRC
open_port_tx_loop:
	movel %d3, (%a1)+		// TX flags + length
	movel %d1, (%a1)+		// buffer address
	addl #BUFFER_LENGTH, %d1
	dbfw %d2, open_port_tx_loop

	bsetl #29, %d3			// TX BD flag: Wrap (last BD)
	movel %d3, (%a1)+		// Final TX flags + length
	movel %d1, (%a1)+		// buffer address

// Setup RX descriptors			// A1 = starting RX BD address
	movel #RX_BUFFERS - 2, %d2	// D2 = RX_BUFFERS - 1 counter
open_port_rx_loop:
	movel #0x90000000, (%a1)+	// RX flags + length
	movel %d1, (%a1)+		// buffer address
	addl #BUFFER_LENGTH, %d1
	dbfw %d2, open_port_rx_loop

	movel #0xB0000000, (%a1)+	// Final RX flags + length
	movel %d1, (%a1)+		// buffer address

// Setup port parameters
	movel scc_base_addr(%d0), %a1	// A1 = SCC_BASE address
	movel scc_reg_addr(%d0), %a2	// A2 = SCC_REGS address

	movel #0xFFFF, SCC_SCCE(%a2)	// clear status bits
	movel #0x0000, SCC_SCCM(%a2)	// interrupt mask

	movel tx_first_bd(%d0), %d1
	movew %d1, SCC_TBASE(%a1)	// D1 = offset of first TxBD
	addl #TX_BUFFERS * 8, %d1
	movew %d1, SCC_RBASE(%a1)	// D1 = offset of first RxBD
	moveb #0x8, SCC_RFCR(%a1)	// Intel mode, 1000
	moveb #0x8, SCC_TFCR(%a1)

// Parity settings
	cmpl #PARITY_CRC16_PR1_CCITT, STATUS_PARITY(%a0)
	bne open_port_parity_1
	clrw SCC_PSMR(%a2)		// CRC16-CCITT
	movel #0xF0B8, SCC_C_MASK(%a1)
	movel #0xFFFF, SCC_C_PRES(%a1)
	movew #HDLC_MAX_MRU + 2, SCC_MFLR(%a1) // 2 bytes for CRC
	movew #2, parity_bytes(%d0)
	bra open_port_2

open_port_parity_1:
	cmpl #PARITY_CRC32_PR1_CCITT, STATUS_PARITY(%a0)
	bne open_port_parity_2
	movew #0x0800, SCC_PSMR(%a2)	// CRC32-CCITT
	movel #0xDEBB20E3, SCC_C_MASK(%a1)
	movel #0xFFFFFFFF, SCC_C_PRES(%a1)
	movew #HDLC_MAX_MRU + 4, SCC_MFLR(%a1) // 4 bytes for CRC
	movew #4, parity_bytes(%d0)
	bra open_port_2

open_port_parity_2:
	cmpl #PARITY_CRC16_PR0_CCITT, STATUS_PARITY(%a0)
	bne open_port_parity_3
	clrw SCC_PSMR(%a2)		// CRC16-CCITT preset 0
	movel #0xF0B8, SCC_C_MASK(%a1)
	clrl SCC_C_PRES(%a1)
	movew #HDLC_MAX_MRU + 2, SCC_MFLR(%a1) // 2 bytes for CRC
	movew #2, parity_bytes(%d0)
	bra open_port_2

open_port_parity_3:
	cmpl #PARITY_CRC32_PR0_CCITT, STATUS_PARITY(%a0)
	bne open_port_parity_4
	movew #0x0800, SCC_PSMR(%a2)	// CRC32-CCITT preset 0
	movel #0xDEBB20E3, SCC_C_MASK(%a1)
	clrl SCC_C_PRES(%a1)
	movew #HDLC_MAX_MRU + 4, SCC_MFLR(%a1) // 4 bytes for CRC
	movew #4, parity_bytes(%d0)
	bra open_port_2

open_port_parity_4:
	clrw SCC_PSMR(%a2)		// no parity
	movel #0xF0B8, SCC_C_MASK(%a1)
	movel #0xFFFF, SCC_C_PRES(%a1)
	movew #HDLC_MAX_MRU, SCC_MFLR(%a1) // 0 bytes for CRC
	clrw parity_bytes(%d0)

open_port_2:
	movel #0x00000003, SCC_GSMR_H(%a2) // RTSM
	cmpl #ENCODING_NRZI, STATUS_ENCODING(%a0)
	bne open_port_nrz
	movel #0x10040900, SCC_GSMR_L(%a2) // NRZI: TCI Tend RECN+TENC=1
	bra open_port_3

open_port_nrz:
	movel #0x10040000, SCC_GSMR_L(%a2) // NRZ: TCI Tend RECN+TENC=0
open_port_3:
	movew #BUFFER_LENGTH, SCC_MRBLR(%a1)
	movel %d0, %d1
	lsll #4, %d1			// D1 bits 7 and 6 = port
	orl #1, %d1
	movew %d1, CR			// Init SCC RX and TX params
	wait_for_command

	// TCI Tend ENR ENT
	movew #0x001F, SCC_SCCM(%a2)	// TXE RXF BSY TXB RXB interrupts
	orl #0x00000030, SCC_GSMR_L(%a2) // enable SCC
open_port_ret:
	rts


/****************************** close port ****************************/

close_port:				// D0 = 4 * port, D6 = doorbell to host
	movel scc_reg_addr(%d0), %a0	// A0 = SCC_REGS address
	clrw SCC_SCCM(%a0)		// no SCC interrupts
	andl #0xFFFFFFCF, SCC_GSMR_L(%a0) // Disable ENT and ENR

	andw #~STATUS_CABLE_DTR, csr_output(%d0) // DTR off
	bsr check_csr			// call with disabled timer interrupt

	movel ch_status_addr(%d0), %d1
	clrl STATUS_OPEN(%d1)		// confirm the port is closed
	rts


/****************************** transmit packet ***********************/
// queue packets for transmission
tx:					// D0 = 4 * port, D6 = doorbell to host
	cmpl #TX_BUFFERS, tx_count(%d0)
	beq tx_ret			// all DB's = descs in use

	movel tx_out(%d0), %d1
	movel %d1, %d2			// D1 = D2 = tx_out BD# = desc#
	mulul #DESC_LENGTH, %d2		// D2 = TX desc offset
	addl ch_status_addr(%d0), %d2
	addl #STATUS_TX_DESCS, %d2	// D2 = TX desc address
	cmpl #PACKET_FULL, (%d2)	// desc status
	bne tx_ret

// queue it
	movel 4(%d2), %a0		// PCI address
	lsll #3, %d1			// BD is 8-bytes long
	addl tx_first_bd(%d0), %d1	// D1 = current tx_out BD addr

	movel 4(%d1), %a1		// A1 = dest address
	movel 8(%d2), %d2		// D2 = length
	movew %d2, 2(%d1)		// length into BD
	memcpy_from_pci %a0, %a1, %d2
	bsetl #31, (%d1)		// CP go ahead

// update tx_out and tx_count
	movel tx_out(%d0), %d1
	addl #1, %d1
	cmpl #TX_BUFFERS, %d1
	bne tx_1
	clrl %d1
tx_1:	movel %d1, tx_out(%d0)

	addl #1, tx_count(%d0)
	bra tx

tx_ret: rts


/****************************** packet received ***********************/

// Service receive buffers		// D0 = 4 * port, D6 = doorbell to host
rx:	movel rx_in(%d0), %d1		// D1 = rx_in BD#
	lsll #3, %d1			// BD is 8-bytes long
	addl rx_first_bd(%d0), %d1	// D1 = current rx_in BD address
	movew (%d1), %d2		// D2 = RX BD flags
	btstl #15, %d2
	bne rx_ret			// BD still empty

	btstl #1, %d2
	bne rx_overrun

	tstw parity_bytes(%d0)
	bne rx_parity
	bclrl #2, %d2			// do not test for CRC errors
rx_parity:
	andw #0x0CBC, %d2		// mask status bits
	cmpw #0x0C00, %d2		// correct frame
	bne rx_bad_frame
	clrl %d3
	movew 2(%d1), %d3
	subw parity_bytes(%d0), %d3	// D3 = packet length
	cmpw #HDLC_MAX_MRU, %d3
	bgt rx_bad_frame

rx_good_frame:
	movel rx_out, %d2
	mulul #DESC_LENGTH, %d2
	addl rx_descs_addr, %d2		// D2 = RX desc address
	cmpl #PACKET_EMPTY, (%d2)	// desc stat
	bne rx_overrun

	movel %d3, 8(%d2)
	movel 4(%d1), %a0		// A0 = source address
	movel 4(%d2), %a1
	tstl %a1
	beq rx_ignore_data
	memcpy_to_pci %a0, %a1, %d3
rx_ignore_data:
	movel packet_full(%d0), (%d2)	// update desc stat

// update D6 and rx_out
	bsetl #DOORBELL_FROM_CARD_RX, %d6 // signal host that RX completed
	movel rx_out, %d2
	addl #1, %d2
	cmpl #RX_QUEUE_LENGTH, %d2
	bne rx_1
	clrl %d2
rx_1:	movel %d2, rx_out

rx_free_bd:
	andw #0xF000, (%d1)		// clear CM and error bits
	bsetl #31, (%d1)		// free BD
// update rx_in
	movel rx_in(%d0), %d1
	addl #1, %d1
	cmpl #RX_BUFFERS, %d1
	bne rx_2
	clrl %d1
rx_2:	movel %d1, rx_in(%d0)
	bra rx

rx_overrun:
	movel ch_status_addr(%d0), %d2
	addl #1, STATUS_RX_OVERRUNS(%d2)
	bra rx_free_bd

rx_bad_frame:
	movel ch_status_addr(%d0), %d2
	addl #1, STATUS_RX_FRAME_ERRORS(%d2)
	bra rx_free_bd

rx_ret: rts


/****************************** packet transmitted ********************/

// Service transmit buffers		// D0 = 4 * port, D6 = doorbell to host
tx_end:	tstl tx_count(%d0)
	beq tx_end_ret			// TX buffers already empty

	movel tx_in(%d0), %d1
	movel %d1, %d2			// D1 = D2 = tx_in BD# = desc#
	lsll #3, %d1			// BD is 8-bytes long
	addl tx_first_bd(%d0), %d1	// D1 = current tx_in BD address
	movew (%d1), %d3		// D3 = TX BD flags
	btstl #15, %d3
	bne tx_end_ret			// BD still being transmitted

// update D6, tx_in and tx_count
	orl bell_tx(%d0), %d6		// signal host that TX desc freed
	subl #1, tx_count(%d0)
	movel tx_in(%d0), %d1
	addl #1, %d1
	cmpl #TX_BUFFERS, %d1
	bne tx_end_1
	clrl %d1
tx_end_1:
	movel %d1, tx_in(%d0)

// free host's descriptor
	mulul #DESC_LENGTH, %d2		// D2 = TX desc offset
	addl ch_status_addr(%d0), %d2
	addl #STATUS_TX_DESCS, %d2	// D2 = TX desc address
	btstl #1, %d3
	bne tx_end_underrun
	movel #PACKET_SENT, (%d2)
	bra tx_end

tx_end_underrun:
	movel #PACKET_UNDERRUN, (%d2)
	bra tx_end

tx_end_ret: rts


/****************************** PLX PCI9060 DMA memcpy ****************/

#if QUICC_MEMCPY_USES_PLX
// called with interrupts disabled
memcpy_from_pci_run:
	movel %d0, -(%sp)
	movew %sr, -(%sp)
memcpy_1:
	movel PLX_DMA_CMD_STS, %d0	// do not btst PLX register directly
	btstl #4, %d0			// transfer done?
	bne memcpy_end
	stop #0x2200			// enable PCI9060 interrupts
	movew #0x2700, %sr		// disable interrupts again
	bra memcpy_1

memcpy_to_pci_run:
	movel %d0, -(%sp)
	movew %sr, -(%sp)
memcpy_2:
	movel PLX_DMA_CMD_STS, %d0	// do not btst PLX register directly
	btstl #12, %d0			// transfer done?
	bne memcpy_end
	stop #0x2200			// enable PCI9060 interrupts
	movew #0x2700, %sr		// disable interrupts again
	bra memcpy_2

memcpy_end:
	movew (%sp)+, %sr
	movel (%sp)+, %d0
	rts
#endif






/****************************** PLX PCI9060 interrupt *****************/

pci9060_interrupt:
	movel %d0, -(%sp)

	movel PLX_DOORBELL_TO_CARD, %d0
	movel %d0, PLX_DOORBELL_TO_CARD	// confirm all requests
	orl %d0, channel_stats

	movel #0x0909, PLX_DMA_CMD_STS	// clear DMA ch #0 and #1 interrupts

	movel (%sp)+, %d0
	rte

/****************************** SCC interrupts ************************/

port_interrupt_1:
	orl #0, SCC1_REGS + SCC_SCCE; // confirm SCC events
	orl #1 << TASK_SCC_0, channel_stats
	movel #0x40000000, CISR
	rte

port_interrupt_2:
	orl #0, SCC2_REGS + SCC_SCCE; // confirm SCC events
	orl #1 << TASK_SCC_1, channel_stats
	movel #0x20000000, CISR
	rte

port_interrupt_3:
	orl #0, SCC3_REGS + SCC_SCCE; // confirm SCC events
	orl #1 << TASK_SCC_2, channel_stats
	movel #0x10000000, CISR
	rte

port_interrupt_4:
	orl #0, SCC4_REGS + SCC_SCCE; // confirm SCC events
	orl #1 << TASK_SCC_3, channel_stats
	movel #0x08000000, CISR
	rte

error_interrupt:
	rte


/****************************** cable and PM routine ******************/
// modified registers: none
check_csr:
	movel %d0, -(%sp)
	movel %d1, -(%sp)
	movel %d2, -(%sp)
	movel %a0, -(%sp)
	movel %a1, -(%sp)

	clrl %d0			// D0 = 4 * port
	movel #CSRA, %a0		// A0 = CSR address

check_csr_loop:
	movew (%a0), %d1		// D1 = CSR input bits
	andl #0xE7, %d1			// PM and cable sense bits (no DCE bit)
	cmpw #STATUS_CABLE_V35 * (1 + 1 << STATUS_CABLE_PM_SHIFT), %d1
	bne check_csr_1
	movew #0x0E08, %d1
	bra check_csr_valid

check_csr_1:
	cmpw #STATUS_CABLE_X21 * (1 + 1 << STATUS_CABLE_PM_SHIFT), %d1
	bne check_csr_2
	movew #0x0408, %d1
	bra check_csr_valid

check_csr_2:
	cmpw #STATUS_CABLE_V24 * (1 + 1 << STATUS_CABLE_PM_SHIFT), %d1
	bne check_csr_3
	movew #0x0208, %d1
	bra check_csr_valid

check_csr_3:
	cmpw #STATUS_CABLE_EIA530 * (1 + 1 << STATUS_CABLE_PM_SHIFT), %d1
	bne check_csr_disable
	movew #0x0D08, %d1
	bra check_csr_valid

check_csr_disable:
	movew #0x0008, %d1		// D1 = disable everything
	movew #0x80E7, %d2		// D2 = input mask: ignore DSR
	bra check_csr_write

check_csr_valid:			// D1 = mode and IRQ bits
	movew csr_output(%d0), %d2
	andw #0x3000, %d2		// D2 = requested LL and DTR bits
	orw %d2, %d1			// D1 = all requested output bits
	movew #0x80FF, %d2		// D2 = input mask: include DSR

check_csr_write:
	cmpw old_csr_output(%d0), %d1
	beq check_csr_input
	movew %d1, old_csr_output(%d0)
	movew %d1, (%a0)		// Write CSR output bits

check_csr_input:
	movew (PCDAT), %d1
	andw dcd_mask(%d0), %d1
	beq check_csr_dcd_on		// DCD and CTS signals are negated
	movew (%a0), %d1		// D1 = CSR input bits
	andw #~STATUS_CABLE_DCD, %d1	// DCD off
	bra check_csr_previous

check_csr_dcd_on:
	movew (%a0), %d1		// D1 = CSR input bits
	orw #STATUS_CABLE_DCD, %d1	// DCD on
check_csr_previous:
	andw %d2, %d1			// input mask
	movel ch_status_addr(%d0), %a1
	cmpl STATUS_CABLE(%a1), %d1	// check for change
	beq check_csr_next
	movel %d1, STATUS_CABLE(%a1)	// update status
	movel bell_cable(%d0), PLX_DOORBELL_FROM_CARD	// signal the host

check_csr_next:
	addl #2, %a0			// next CSR register
	addl #4, %d0			// D0 = 4 * next port
	cmpl #4 * 4, %d0
	bne check_csr_loop

	movel (%sp)+, %a1
	movel (%sp)+, %a0
	movel (%sp)+, %d2
	movel (%sp)+, %d1
	movel (%sp)+, %d0
	rts


/****************************** timer interrupt ***********************/

timer_interrupt:
	bsr check_csr
	rte


/****************************** RAM sizing and test *******************/
#if DETECT_RAM
ram_test:
	movel #0x12345678, %d1		// D1 = test value
	movel %d1, (128 * 1024 - 4)
	movel #128 * 1024, %d0		// D0 = RAM size tested
ram_test_size:
	cmpl #MAX_RAM_SIZE, %d0
	beq ram_test_size_found
	movel %d0, %a0
	addl #128 * 1024 - 4, %a0
	cmpl (%a0), %d1
	beq ram_test_size_check
ram_test_next_size:
	lsll #1, %d0
	bra ram_test_size

ram_test_size_check:
	eorl #0xFFFFFFFF, %d1
	movel %d1, (128 * 1024 - 4)
	cmpl (%a0), %d1
	bne ram_test_next_size

ram_test_size_found:			// D0 = RAM size
	movel %d0, %a0			// A0 = fill ptr
	subl #firmware_end + 4, %d0
	lsrl #2, %d0
	movel %d0, %d1			// D1 = DBf counter
ram_test_fill:
	movel %a0, -(%a0)
	dbfw %d1, ram_test_fill
	subl #0x10000, %d1
	cmpl #0xFFFFFFFF, %d1
	bne ram_test_fill

ram_test_loop:				// D0 = DBf counter
	cmpl (%a0)+, %a0
	dbnew %d0, ram_test_loop
	bne ram_test_found_bad
	subl #0x10000, %d0
	cmpl #0xFFFFFFFF, %d0
	bne ram_test_loop
	bra ram_test_all_ok

ram_test_found_bad:
	subl #4, %a0
ram_test_all_ok:
	movel %a0, PLX_MAILBOX_5
	rts
#endif


/****************************** constants *****************************/

scc_reg_addr:
	.long SCC1_REGS, SCC2_REGS, SCC3_REGS, SCC4_REGS
scc_base_addr:
	.long SCC1_BASE, SCC2_BASE, SCC3_BASE, SCC4_BASE

tx_first_bd:
	.long DPRBASE
	.long DPRBASE + (TX_BUFFERS + RX_BUFFERS) * 8
	.long DPRBASE + (TX_BUFFERS + RX_BUFFERS) * 8 * 2
	.long DPRBASE + (TX_BUFFERS + RX_BUFFERS) * 8 * 3

rx_first_bd:
	.long DPRBASE + TX_BUFFERS * 8
	.long DPRBASE + TX_BUFFERS * 8 + (TX_BUFFERS + RX_BUFFERS) * 8
	.long DPRBASE + TX_BUFFERS * 8 + (TX_BUFFERS + RX_BUFFERS) * 8 * 2
	.long DPRBASE + TX_BUFFERS * 8 + (TX_BUFFERS + RX_BUFFERS) * 8 * 3

first_buffer:
	.long BUFFERS_ADDR
	.long BUFFERS_ADDR + (TX_BUFFERS + RX_BUFFERS) * BUFFER_LENGTH
	.long BUFFERS_ADDR + (TX_BUFFERS + RX_BUFFERS) * BUFFER_LENGTH * 2
	.long BUFFERS_ADDR + (TX_BUFFERS + RX_BUFFERS) * BUFFER_LENGTH * 3

bell_tx:
	.long 1 << DOORBELL_FROM_CARD_TX_0, 1 << DOORBELL_FROM_CARD_TX_1
	.long 1 << DOORBELL_FROM_CARD_TX_2, 1 << DOORBELL_FROM_CARD_TX_3

bell_cable:
	.long 1 << DOORBELL_FROM_CARD_CABLE_0, 1 << DOORBELL_FROM_CARD_CABLE_1
	.long 1 << DOORBELL_FROM_CARD_CABLE_2, 1 << DOORBELL_FROM_CARD_CABLE_3

packet_full:
	.long PACKET_FULL, PACKET_FULL + 1, PACKET_FULL + 2, PACKET_FULL + 3

clocking_ext:
	.long 0x0000002C, 0x00003E00, 0x002C0000, 0x3E000000
clocking_txfromrx:
	.long 0x0000002D, 0x00003F00, 0x002D0000, 0x3F000000
clocking_mask:
	.long 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
dcd_mask:
	.word 0x020, 0, 0x080, 0, 0x200, 0, 0x800

	.ascii "wanXL firmware\n"
	.asciz "Copyright (C) 2003 Krzysztof Halasa <khc@pm.waw.pl>\n"


/****************************** variables *****************************/

		.align 4
channel_stats:	.long 0

tx_in:		.long 0, 0, 0, 0	// transmitted
tx_out:		.long 0, 0, 0, 0	// received from host for transmission
tx_count:	.long 0, 0, 0, 0	// currently in transmit queue

rx_in:		.long 0, 0, 0, 0	// received from port
rx_out:		.long 0			// transmitted to host
parity_bytes:	.word 0, 0, 0, 0, 0, 0, 0 // only 4 words are used

csr_output:	.word 0
old_csr_output:	.word 0, 0, 0, 0, 0, 0, 0
		.align 4
firmware_end:				// must be dword-aligned
