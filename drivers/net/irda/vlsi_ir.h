
/*********************************************************************
 *
 *	vlsi_ir.h:	VLSI82C147 PCI IrDA controller driver for Linux
 *
 *	Version:	0.5
 *
 *	Copyright (c) 2001-2003 Martin Diehl
 *
 *	This program is free software; you can redistribute it and/or 
 *	modify it under the terms of the GNU General Public License as 
 *	published by the Free Software Foundation; either version 2 of 
 *	the License, or (at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License 
 *	along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 ********************************************************************/

#ifndef IRDA_VLSI_FIR_H
#define IRDA_VLSI_FIR_H

/* ================================================================
 * compatibility stuff
 */

/* definitions not present in pci_ids.h */

#ifndef PCI_CLASS_WIRELESS_IRDA
#define PCI_CLASS_WIRELESS_IRDA		0x0d00
#endif

#ifndef PCI_CLASS_SUBCLASS_MASK
#define PCI_CLASS_SUBCLASS_MASK		0xffff
#endif

/* ================================================================ */

/* non-standard PCI registers */

enum vlsi_pci_regs {
	VLSI_PCI_CLKCTL		= 0x40,		/* chip clock input control */
	VLSI_PCI_MSTRPAGE	= 0x41,		/* addr [31:24] for all busmaster cycles */
	VLSI_PCI_IRMISC		= 0x42		/* mainly legacy UART related */
};

/* ------------------------------------------ */

/* VLSI_PCI_CLKCTL: Clock Control Register (u8, rw) */

/* Three possible clock sources: either on-chip 48MHz PLL or
 * external clock applied to EXTCLK pin. External clock may
 * be either 48MHz or 40MHz, which is indicated by XCKSEL.
 * CLKSTP controls whether the selected clock source gets
 * connected to the IrDA block.
 *
 * On my HP OB-800 the BIOS sets external 40MHz clock as source
 * when IrDA enabled and I've never detected any PLL lock success.
 * Apparently the 14.3...MHz OSC input required for the PLL to work
 * is not connected and the 40MHz EXTCLK is provided externally.
 * At least this is what makes the driver working for me.
 */

enum vlsi_pci_clkctl {

	/* PLL control */

	CLKCTL_PD_INV		= 0x04,		/* PD#: inverted power down signal,
						 * i.e. PLL is powered, if PD_INV set */
	CLKCTL_LOCK		= 0x40,		/* (ro) set, if PLL is locked */

	/* clock source selection */

	CLKCTL_EXTCLK		= 0x20,		/* set to select external clock input, not PLL */
	CLKCTL_XCKSEL		= 0x10,		/* set to indicate EXTCLK is 40MHz, not 48MHz */

	/* IrDA block control */

	CLKCTL_CLKSTP		= 0x80,		/* set to disconnect from selected clock source */
	CLKCTL_WAKE		= 0x08		/* set to enable wakeup feature: whenever IR activity
						 * is detected, PD_INV gets set(?) and CLKSTP cleared */
};

/* ------------------------------------------ */

/* VLSI_PCI_MSTRPAGE: Master Page Register (u8, rw) and busmastering stuff */

#define DMA_MASK_USED_BY_HW	0xffffffff
#define DMA_MASK_MSTRPAGE	0x00ffffff
#define MSTRPAGE_VALUE		(DMA_MASK_MSTRPAGE >> 24)

	/* PCI busmastering is somewhat special for this guy - in short:
	 *
	 * We select to operate using fixed MSTRPAGE=0, use ISA DMA
	 * address restrictions to make the PCI BM api aware of this,
	 * but ensure the hardware is dealing with real 32bit access.
	 *
	 * In detail:
	 * The chip executes normal 32bit busmaster cycles, i.e.
	 * drives all 32 address lines. These addresses however are
	 * composed of [0:23] taken from various busaddr-pointers
	 * and [24:31] taken from the MSTRPAGE register in the VLSI82C147
	 * config space. Therefore _all_ busmastering must be
	 * targeted to/from one single 16MB (busaddr-) superpage!
	 * The point is to make sure all the allocations for memory
	 * locations with busmaster access (ring descriptors, buffers)
	 * are indeed bus-mappable to the same 16MB range (for x86 this
	 * means they must reside in the same 16MB physical memory address
	 * range). The only constraint we have which supports "several objects
	 * mappable to common 16MB range" paradigma, is the old ISA DMA
	 * restriction to the first 16MB of physical address range.
	 * Hence the approach here is to enable PCI busmaster support using
	 * the correct 32bit dma-mask used by the chip. Afterwards the device's
	 * dma-mask gets restricted to 24bit, which must be honoured somehow by
	 * all allocations for memory areas to be exposed to the chip ...
	 *
	 * Note:
	 * Don't be surprised to get "Setting latency timer..." messages every
	 * time when PCI busmastering is enabled for the chip.
	 * The chip has its PCI latency timer RO fixed at 0 - which is not a
	 * problem here, because it is never requesting _burst_ transactions.
	 */

/* ------------------------------------------ */

/* VLSI_PCIIRMISC: IR Miscellaneous Register (u8, rw) */

/* legacy UART emulation - not used by this driver - would require:
 * (see below for some register-value definitions)
 *
 *	- IRMISC_UARTEN must be set to enable UART address decoding
 *	- IRMISC_UARTSEL configured
 *	- IRCFG_MASTER must be cleared
 *	- IRCFG_SIR must be set
 *	- IRENABLE_PHYANDCLOCK must be asserted 0->1 (and hence IRENABLE_SIR_ON)
 */

enum vlsi_pci_irmisc {

	/* IR transceiver control */

	IRMISC_IRRAIL		= 0x40,		/* (ro?) IR rail power indication (and control?)
						 * 0=3.3V / 1=5V. Probably set during power-on?
						 * unclear - not touched by driver */
	IRMISC_IRPD		= 0x08,		/* transceiver power down, if set */

	/* legacy UART control */

	IRMISC_UARTTST		= 0x80,		/* UART test mode - "always write 0" */
	IRMISC_UARTEN		= 0x04,		/* enable UART address decoding */

	/* bits [1:0] IRMISC_UARTSEL to select legacy UART address */

	IRMISC_UARTSEL_3f8	= 0x00,
	IRMISC_UARTSEL_2f8	= 0x01,
	IRMISC_UARTSEL_3e8	= 0x02,
	IRMISC_UARTSEL_2e8	= 0x03
};

/* ================================================================ */

/* registers mapped to 32 byte PCI IO space */

/* note: better access all registers at the indicated u8/u16 size
 *	 although some of them contain only 1 byte of information.
 *	 some of them (particaluarly PROMPT and IRCFG) ignore
 *	 access when using the wrong addressing mode!
 */

enum vlsi_pio_regs {
	VLSI_PIO_IRINTR		= 0x00,		/* interrupt enable/request (u8, rw) */
	VLSI_PIO_RINGPTR	= 0x02,		/* rx/tx ring pointer (u16, ro) */
	VLSI_PIO_RINGBASE	= 0x04,		/* [23:10] of ring address (u16, rw) */
	VLSI_PIO_RINGSIZE	= 0x06,		/* rx/tx ring size (u16, rw) */
	VLSI_PIO_PROMPT		= 0x08, 	/* triggers ring processing (u16, wo) */
	/* 0x0a-0x0f: reserved / duplicated UART regs */
	VLSI_PIO_IRCFG		= 0x10,		/* configuration select (u16, rw) */
	VLSI_PIO_SIRFLAG	= 0x12,		/* BOF/EOF for filtered SIR (u16, ro) */
	VLSI_PIO_IRENABLE	= 0x14,		/* enable and status register (u16, rw/ro) */
	VLSI_PIO_PHYCTL		= 0x16,		/* physical layer current status (u16, ro) */
	VLSI_PIO_NPHYCTL	= 0x18,		/* next physical layer select (u16, rw) */
	VLSI_PIO_MAXPKT		= 0x1a,		/* [11:0] max len for packet receive (u16, rw) */
	VLSI_PIO_RCVBCNT	= 0x1c		/* current receive-FIFO byte count (u16, ro) */
	/* 0x1e-0x1f: reserved / duplicated UART regs */
};

/* ------------------------------------------ */

/* VLSI_PIO_IRINTR: Interrupt Register (u8, rw) */

/* enable-bits:
 *		1 = enable / 0 = disable
 * interrupt condition bits:
 * 		set according to corresponding interrupt source
 *		(regardless of the state of the enable bits)
 *		enable bit status indicates whether interrupt gets raised
 *		write-to-clear
 * note: RPKTINT and TPKTINT behave different in legacy UART mode (which we don't use :-)
 */

enum vlsi_pio_irintr {
	IRINTR_ACTEN	= 0x80,	/* activity interrupt enable */
	IRINTR_ACTIVITY	= 0x40,	/* activity monitor (traffic detected) */
	IRINTR_RPKTEN	= 0x20,	/* receive packet interrupt enable*/
	IRINTR_RPKTINT	= 0x10,	/* rx-packet transferred from fifo to memory finished */
	IRINTR_TPKTEN	= 0x08,	/* transmit packet interrupt enable */
	IRINTR_TPKTINT	= 0x04,	/* last bit of tx-packet+crc shifted to ir-pulser */
	IRINTR_OE_EN	= 0x02,	/* UART rx fifo overrun error interrupt enable */
	IRINTR_OE_INT	= 0x01	/* UART rx fifo overrun error (read LSR to clear) */
};

/* we use this mask to check whether the (shared PCI) interrupt is ours */

#define IRINTR_INT_MASK		(IRINTR_ACTIVITY|IRINTR_RPKTINT|IRINTR_TPKTINT)

/* ------------------------------------------ */

/* VLSI_PIO_RINGPTR: Ring Pointer Read-Back Register (u16, ro) */

/* _both_ ring pointers are indices relative to the _entire_ rx,tx-ring!
 * i.e. the referenced descriptor is located
 * at RINGBASE + PTR * sizeof(descr) for rx and tx
 * therefore, the tx-pointer has offset MAX_RING_DESCR
 */

#define MAX_RING_DESCR		64	/* tx, rx rings may contain up to 64 descr each */

#define RINGPTR_RX_MASK		(MAX_RING_DESCR-1)
#define RINGPTR_TX_MASK		((MAX_RING_DESCR-1)<<8)

#define RINGPTR_GET_RX(p)	((p)&RINGPTR_RX_MASK)
#define RINGPTR_GET_TX(p)	(((p)&RINGPTR_TX_MASK)>>8)

/* ------------------------------------------ */

/* VLSI_PIO_RINGBASE: Ring Pointer Base Address Register (u16, ro) */

/* Contains [23:10] part of the ring base (bus-) address
 * which must be 1k-alinged. [31:24] is taken from
 * VLSI_PCI_MSTRPAGE above.
 * The controller initiates non-burst PCI BM cycles to
 * fetch and update the descriptors in the ring.
 * Once fetched, the descriptor remains cached onchip
 * until it gets closed and updated due to the ring
 * processing state machine.
 * The entire ring area is split in rx and tx areas with each
 * area consisting of 64 descriptors of 8 bytes each.
 * The rx(tx) ring is located at ringbase+0 (ringbase+64*8).
 */

#define BUS_TO_RINGBASE(p)	(((p)>>10)&0x3fff)

/* ------------------------------------------ */

/* VLSI_PIO_RINGSIZE: Ring Size Register (u16, rw) */

/* bit mask to indicate the ring size to be used for rx and tx.
 * 	possible values		encoded bits
 *		 4		   0000
 *		 8		   0001
 *		16		   0011
 *		32		   0111
 *		64		   1111
 * located at [15:12] for tx and [11:8] for rx ([7:0] unused)
 *
 * note: probably a good idea to have IRCFG_MSTR cleared when writing
 *	 this so the state machines are stopped and the RINGPTR is reset!
 */

#define SIZE_TO_BITS(num)		((((num)-1)>>2)&0x0f)
#define TX_RX_TO_RINGSIZE(tx,rx)	((SIZE_TO_BITS(tx)<<12)|(SIZE_TO_BITS(rx)<<8))
#define RINGSIZE_TO_RXSIZE(rs)		((((rs)&0x0f00)>>6)+4)
#define RINGSIZE_TO_TXSIZE(rs)		((((rs)&0xf000)>>10)+4)


/* ------------------------------------------ */

/* VLSI_PIO_PROMPT: Ring Prompting Register (u16, write-to-start) */

/* writing any value kicks the ring processing state machines
 * for both tx, rx rings as follows:
 * 	- active rings (currently owning an active descriptor)
 *	  ignore the prompt and continue
 *	- idle rings fetch the next descr from the ring and start
 *	  their processing
 */

/* ------------------------------------------ */

/* VLSI_PIO_IRCFG: IR Config Register (u16, rw) */

/* notes:
 *	- not more than one SIR/MIR/FIR bit must be set at any time
 *	- SIR, MIR, FIR and CRC16 select the configuration which will
 *	  be applied on next 0->1 transition of IRENABLE_PHYANDCLOCK (see below).
 *	- besides allowing the PCI interface to execute busmaster cycles
 *	  and therefore the ring SM to operate, the MSTR bit has side-effects:
 *	  when MSTR is cleared, the RINGPTR's get reset and the legacy UART mode
 *	  (in contrast to busmaster access mode) gets enabled.
 *	- clearing ENRX or setting ENTX while data is received may stall the
 *	  receive fifo until ENRX reenabled _and_ another packet arrives
 *	- SIRFILT means the chip performs the required unwrapping of hardware
 *	  headers (XBOF's, BOF/EOF) and un-escaping in the _receive_ direction.
 *	  Only the resulting IrLAP payload is copied to the receive buffers -
 *	  but with the 16bit FCS still encluded. Question remains, whether it
 *	  was already checked or we should do it before passing the packet to IrLAP?
 */

enum vlsi_pio_ircfg {
	IRCFG_LOOP	= 0x4000,	/* enable loopback test mode */
	IRCFG_ENTX	= 0x1000,	/* transmit enable */
	IRCFG_ENRX	= 0x0800,	/* receive enable */
	IRCFG_MSTR	= 0x0400,	/* master enable */
	IRCFG_RXANY	= 0x0200,	/* receive any packet */
	IRCFG_CRC16	= 0x0080,	/* 16bit (not 32bit) CRC select for MIR/FIR */
	IRCFG_FIR	= 0x0040,	/* FIR 4PPM encoding mode enable */
	IRCFG_MIR	= 0x0020,	/* MIR HDLC encoding mode enable */
	IRCFG_SIR	= 0x0010,	/* SIR encoding mode enable */
	IRCFG_SIRFILT	= 0x0008,	/* enable SIR decode filter (receiver unwrapping) */
	IRCFG_SIRTEST	= 0x0004,	/* allow SIR decode filter when not in SIR mode */
	IRCFG_TXPOL	= 0x0002,	/* invert tx polarity when set */
	IRCFG_RXPOL	= 0x0001	/* invert rx polarity when set */
};

/* ------------------------------------------ */

/* VLSI_PIO_SIRFLAG: SIR Flag Register (u16, ro) */

/* register contains hardcoded BOF=0xc0 at [7:0] and EOF=0xc1 at [15:8]
 * which is used for unwrapping received frames in SIR decode-filter mode
 */

/* ------------------------------------------ */

/* VLSI_PIO_IRENABLE: IR Enable Register (u16, rw/ro) */

/* notes:
 *	- IREN acts as gate for latching the configured IR mode information
 *	  from IRCFG and IRPHYCTL when IREN=reset and applying them when
 *	  IREN gets set afterwards.
 *	- ENTXST reflects IRCFG_ENTX
 *	- ENRXST = IRCFG_ENRX && (!IRCFG_ENTX || IRCFG_LOOP)
 */

enum vlsi_pio_irenable {
	IRENABLE_PHYANDCLOCK	= 0x8000,  /* enable IR phy and gate the mode config (rw) */
	IRENABLE_CFGER		= 0x4000,  /* mode configuration error (ro) */
	IRENABLE_FIR_ON		= 0x2000,  /* FIR on status (ro) */
	IRENABLE_MIR_ON		= 0x1000,  /* MIR on status (ro) */
	IRENABLE_SIR_ON		= 0x0800,  /* SIR on status (ro) */
	IRENABLE_ENTXST		= 0x0400,  /* transmit enable status (ro) */
	IRENABLE_ENRXST		= 0x0200,  /* Receive enable status (ro) */
	IRENABLE_CRC16_ON	= 0x0100   /* 16bit (not 32bit) CRC enabled status (ro) */
};

#define	  IRENABLE_MASK	    0xff00  /* Read mask */

/* ------------------------------------------ */

/* VLSI_PIO_PHYCTL: IR Physical Layer Current Control Register (u16, ro) */

/* read-back of the currently applied physical layer status.
 * applied from VLSI_PIO_NPHYCTL at rising edge of IRENABLE_PHYANDCLOCK
 * contents identical to VLSI_PIO_NPHYCTL (see below)
 */

/* ------------------------------------------ */

/* VLSI_PIO_NPHYCTL: IR Physical Layer Next Control Register (u16, rw) */

/* latched during IRENABLE_PHYANDCLOCK=0 and applied at 0-1 transition
 *
 * consists of BAUD[15:10], PLSWID[9:5] and PREAMB[4:0] bits defined as follows:
 *
 * SIR-mode:	BAUD = (115.2kHz / baudrate) - 1
 *		PLSWID = (pulsetime * freq / (BAUD+1)) - 1
 *			where pulsetime is the requested IrPHY pulse width
 *			and freq is 8(16)MHz for 40(48)MHz primary input clock
 *		PREAMB: don't care for SIR
 *
 *		The nominal SIR pulse width is 3/16 bit time so we have PLSWID=12
 *		fixed for all SIR speeds at 40MHz input clock (PLSWID=24 at 48MHz).
 *		IrPHY also allows shorter pulses down to the nominal pulse duration
 *		at 115.2kbaud (minus some tolerance) which is 1.41 usec.
 *		Using the expression PLSWID = 12/(BAUD+1)-1 (multiplied by two for 48MHz)
 *		we get the minimum acceptable PLSWID values according to the VLSI
 *		specification, which provides 1.5 usec pulse width for all speeds (except
 *		for 2.4kbaud getting 6usec). This is fine with IrPHY v1.3 specs and
 *		reduces the transceiver power which drains the battery. At 9.6kbaud for
 *		example this amounts to more than 90% battery power saving!
 *
 * MIR-mode:	BAUD = 0
 *		PLSWID = 9(10) for 40(48) MHz input clock
 *			to get nominal MIR pulse width
 *		PREAMB = 1
 *
 * FIR-mode:	BAUD = 0
 *		PLSWID: don't care
 *		PREAMB = 15
 */

#define PHYCTL_BAUD_SHIFT	10
#define PHYCTL_BAUD_MASK	0xfc00
#define PHYCTL_PLSWID_SHIFT	5
#define PHYCTL_PLSWID_MASK	0x03e0
#define PHYCTL_PREAMB_SHIFT	0
#define PHYCTL_PREAMB_MASK	0x001f

#define PHYCTL_TO_BAUD(bwp)	(((bwp)&PHYCTL_BAUD_MASK)>>PHYCTL_BAUD_SHIFT)
#define PHYCTL_TO_PLSWID(bwp)	(((bwp)&PHYCTL_PLSWID_MASK)>>PHYCTL_PLSWID_SHIFT)
#define PHYCTL_TO_PREAMB(bwp)	(((bwp)&PHYCTL_PREAMB_MASK)>>PHYCTL_PREAMB_SHIFT)

#define BWP_TO_PHYCTL(b,w,p)	((((b)<<PHYCTL_BAUD_SHIFT)&PHYCTL_BAUD_MASK) \
				 | (((w)<<PHYCTL_PLSWID_SHIFT)&PHYCTL_PLSWID_MASK) \
				 | (((p)<<PHYCTL_PREAMB_SHIFT)&PHYCTL_PREAMB_MASK))

#define BAUD_BITS(br)		((115200/(br))-1)

static inline unsigned
calc_width_bits(unsigned baudrate, unsigned widthselect, unsigned clockselect)
{
	unsigned	tmp;

	if (widthselect)	/* nominal 3/16 puls width */
		return (clockselect) ? 12 : 24;

	tmp = ((clockselect) ? 12 : 24) / (BAUD_BITS(baudrate)+1);

	/* intermediate result of integer division needed here */

	return (tmp>0) ? (tmp-1) : 0;
}

#define PHYCTL_SIR(br,ws,cs)	BWP_TO_PHYCTL(BAUD_BITS(br),calc_width_bits((br),(ws),(cs)),0)
#define PHYCTL_MIR(cs)		BWP_TO_PHYCTL(0,((cs)?9:10),1)
#define PHYCTL_FIR		BWP_TO_PHYCTL(0,0,15)

/* quite ugly, I know. But implementing these calculations here avoids
 * having magic numbers in the code and allows some playing with pulsewidths
 * without risk to violate the standards.
 * FWIW, here is the table for reference:
 *
 * baudrate	BAUD	min-PLSWID	nom-PLSWID	PREAMB
 *     2400	  47	   0(0)		   12(24)	   0
 *     9600	  11	   0(0)		   12(24)	   0
 *    19200	   5	   1(2)		   12(24)	   0
 *    38400	   2	   3(6)	           12(24)	   0
 *    57600	   1	   5(10)	   12(24)	   0
 *   115200	   0	  11(22)	   12(24)	   0
 *	MIR	   0	    -		    9(10)	   1
 *	FIR	   0        -               0		  15
 *
 * note: x(y) means x-value for 40MHz / y-value for 48MHz primary input clock
 */

/* ------------------------------------------ */


/* VLSI_PIO_MAXPKT: Maximum Packet Length register (u16, rw) */

/* maximum acceptable length for received packets */

/* hw imposed limitation - register uses only [11:0] */
#define MAX_PACKET_LENGTH	0x0fff

/* IrLAP I-field (apparently not defined elsewhere) */
#define IRDA_MTU		2048

/* complete packet consists of A(1)+C(1)+I(<=IRDA_MTU) */
#define IRLAP_SKB_ALLOCSIZE	(1+1+IRDA_MTU)

/* the buffers we use to exchange frames with the hardware need to be
 * larger than IRLAP_SKB_ALLOCSIZE because we may have up to 4 bytes FCS
 * appended and, in SIR mode, a lot of frame wrapping bytes. The worst
 * case appears to be a SIR packet with I-size==IRDA_MTU and all bytes
 * requiring to be escaped to provide transparency. Furthermore, the peer
 * might ask for quite a number of additional XBOFs:
 *	up to 115+48 XBOFS		 163
 *	regular BOF			   1
 *	A-field				   1
 *	C-field				   1
 *	I-field, IRDA_MTU, all escaped	4096
 *	FCS (16 bit at SIR, escaped)	   4
 *	EOF				   1
 * AFAICS nothing in IrLAP guarantees A/C field not to need escaping
 * (f.e. 0xc0/0xc1 - i.e. BOF/EOF - are legal values there) so in the
 * worst case we have 4269 bytes total frame size.
 * However, the VLSI uses 12 bits only for all buffer length values,
 * which limits the maximum useable buffer size <= 4095.
 * Note this is not a limitation in the receive case because we use
 * the SIR filtering mode where the hw unwraps the frame and only the
 * bare packet+fcs is stored into the buffer - in contrast to the SIR
 * tx case where we have to pass frame-wrapped packets to the hw.
 * If this would ever become an issue in real life, the only workaround
 * I see would be using the legacy UART emulation in SIR mode.
 */

#define XFER_BUF_SIZE		MAX_PACKET_LENGTH

/* ------------------------------------------ */

/* VLSI_PIO_RCVBCNT: Receive Byte Count Register (u16, ro) */

/* receive packet counter gets incremented on every non-filtered
 * byte which was put in the receive fifo and reset for each
 * new packet. Used to decide whether we are just in the middle
 * of receiving
 */

/* better apply the [11:0] mask when reading, as some docs say the
 * reserved [15:12] would return 1 when reading - which is wrong AFAICS
 */
#define RCVBCNT_MASK	0x0fff

/******************************************************************/

/* descriptors for rx/tx ring
 *
 * accessed by hardware - don't change!
 *
 * the descriptor is owned by hardware, when the ACTIVE status bit
 * is set and nothing (besides reading status to test the bit)
 * shall be done. The bit gets cleared by hw, when the descriptor
 * gets closed. Premature reaping of descriptors owned be the chip
 * can be achieved by disabling IRCFG_MSTR
 *
 * Attention: Writing addr overwrites status!
 *
 * ### FIXME: depends on endianess (but there ain't no non-i586 ob800 ;-)
 */

struct ring_descr_hw {
	volatile __le16	rd_count;	/* tx/rx count [11:0] */
	__le16		reserved;
	union {
		__le32	addr;		/* [23:0] of the buffer's busaddress */
		struct {
			u8		addr_res[3];
			volatile u8	status;		/* descriptor status */
		} __packed rd_s;
	} __packed rd_u;
} __packed;

#define rd_addr		rd_u.addr
#define rd_status	rd_u.rd_s.status

/* ring descriptor status bits */

#define RD_ACTIVE		0x80	/* descriptor owned by hw (both TX,RX) */

/* TX ring descriptor status */

#define	RD_TX_DISCRC		0x40	/* do not send CRC (for SIR) */
#define	RD_TX_BADCRC		0x20	/* force a bad CRC */
#define	RD_TX_PULSE		0x10	/* send indication pulse after this frame (MIR/FIR) */
#define	RD_TX_FRCEUND		0x08	/* force underrun */
#define	RD_TX_CLRENTX		0x04	/* clear ENTX after this frame */
#define	RD_TX_UNDRN		0x01	/* TX fifo underrun (probably PCI problem) */

/* RX ring descriptor status */

#define RD_RX_PHYERR		0x40	/* physical encoding error */
#define RD_RX_CRCERR		0x20	/* CRC error (MIR/FIR) */
#define RD_RX_LENGTH		0x10	/* frame exceeds buffer length */
#define RD_RX_OVER		0x08	/* RX fifo overrun (probably PCI problem) */
#define RD_RX_SIRBAD		0x04	/* EOF missing: BOF follows BOF (SIR, filtered) */

#define RD_RX_ERROR		0x7c	/* any error in received frame */

/* the memory required to hold the 2 descriptor rings */
#define HW_RING_AREA_SIZE	(2 * MAX_RING_DESCR * sizeof(struct ring_descr_hw))

/******************************************************************/

/* sw-ring descriptors consists of a bus-mapped transfer buffer with
 * associated skb and a pointer to the hw entry descriptor
 */

struct ring_descr {
	struct ring_descr_hw	*hw;
	struct sk_buff		*skb;
	void			*buf;
};

/* wrappers for operations on hw-exposed ring descriptors
 * access to the hw-part of the descriptors must use these.
 */

static inline int rd_is_active(struct ring_descr *rd)
{
	return (rd->hw->rd_status & RD_ACTIVE) != 0;
}

static inline void rd_activate(struct ring_descr *rd)
{
	rd->hw->rd_status |= RD_ACTIVE;
}

static inline void rd_set_status(struct ring_descr *rd, u8 s)
{
	rd->hw->rd_status = s;	 /* may pass ownership to the hardware */
}

static inline void rd_set_addr_status(struct ring_descr *rd, dma_addr_t a, u8 s)
{
	/* order is important for two reasons:
	 *  - overlayed: writing addr overwrites status
	 *  - we want to write status last so we have valid address in
	 *    case status has RD_ACTIVE set
	 */

	if ((a & ~DMA_MASK_MSTRPAGE)>>24 != MSTRPAGE_VALUE) {
		IRDA_ERROR("%s: pci busaddr inconsistency!\n", __func__);
		dump_stack();
		return;
	}

	a &= DMA_MASK_MSTRPAGE;  /* clear highbyte to make sure we won't write
				  * to status - just in case MSTRPAGE_VALUE!=0
				  */
	rd->hw->rd_addr = cpu_to_le32(a);
	wmb();
	rd_set_status(rd, s);	 /* may pass ownership to the hardware */
}

static inline void rd_set_count(struct ring_descr *rd, u16 c)
{
	rd->hw->rd_count = cpu_to_le16(c);
}

static inline u8 rd_get_status(struct ring_descr *rd)
{
	return rd->hw->rd_status;
}

static inline dma_addr_t rd_get_addr(struct ring_descr *rd)
{
	dma_addr_t	a;

	a = le32_to_cpu(rd->hw->rd_addr);
	return (a & DMA_MASK_MSTRPAGE) | (MSTRPAGE_VALUE << 24);
}

static inline u16 rd_get_count(struct ring_descr *rd)
{
	return le16_to_cpu(rd->hw->rd_count);
}

/******************************************************************/

/* sw descriptor rings for rx, tx:
 *
 * operations follow producer-consumer paradigm, with the hw
 * in the middle doing the processing.
 * ring size must be power of two.
 *
 * producer advances r->tail after inserting for processing
 * consumer advances r->head after removing processed rd
 * ring is empty if head==tail / full if (tail+1)==head
 */

struct vlsi_ring {
	struct pci_dev		*pdev;
	int			dir;
	unsigned		len;
	unsigned		size;
	unsigned		mask;
	atomic_t		head, tail;
	struct ring_descr	*rd;
};

/* ring processing helpers */

static inline struct ring_descr *ring_last(struct vlsi_ring *r)
{
	int t;

	t = atomic_read(&r->tail) & r->mask;
	return (((t+1) & r->mask) == (atomic_read(&r->head) & r->mask)) ? NULL : &r->rd[t];
}

static inline struct ring_descr *ring_put(struct vlsi_ring *r)
{
	atomic_inc(&r->tail);
	return ring_last(r);
}

static inline struct ring_descr *ring_first(struct vlsi_ring *r)
{
	int h;

	h = atomic_read(&r->head) & r->mask;
	return (h == (atomic_read(&r->tail) & r->mask)) ? NULL : &r->rd[h];
}

static inline struct ring_descr *ring_get(struct vlsi_ring *r)
{
	atomic_inc(&r->head);
	return ring_first(r);
}

/******************************************************************/

/* our private compound VLSI-PCI-IRDA device information */

typedef struct vlsi_irda_dev {
	struct pci_dev		*pdev;

	struct irlap_cb		*irlap;

	struct qos_info		qos;

	unsigned		mode;
	int			baud, new_baud;

	dma_addr_t		busaddr;
	void			*virtaddr;
	struct vlsi_ring	*tx_ring, *rx_ring;

	struct timeval		last_rx;

	spinlock_t		lock;
	struct mutex		mtx;

	u8			resume_ok;	
	struct proc_dir_entry	*proc_entry;

} vlsi_irda_dev_t;

/********************************************************/

/* the remapped error flags we use for returning from frame
 * post-processing in vlsi_process_tx/rx() after it was completed
 * by the hardware. These functions either return the >=0 number
 * of transferred bytes in case of success or the negative (-)
 * of the or'ed error flags.
 */

#define VLSI_TX_DROP		0x0001
#define VLSI_TX_FIFO		0x0002

#define VLSI_RX_DROP		0x0100
#define VLSI_RX_OVER		0x0200
#define VLSI_RX_LENGTH  	0x0400
#define VLSI_RX_FRAME		0x0800
#define VLSI_RX_CRC		0x1000

/********************************************************/

#endif /* IRDA_VLSI_FIR_H */

