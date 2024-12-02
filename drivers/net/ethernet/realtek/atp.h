/* SPDX-License-Identifier: GPL-2.0 */
/* Linux header file for the ATP pocket ethernet adapter. */
/* v1.09 8/9/2000 becker@scyld.com. */

#include <linux/if_ether.h>
#include <linux/types.h>

/* The header prepended to received packets. */
struct rx_header {
	ushort pad;		/* Pad. */
	ushort rx_count;
	ushort rx_status;	/* Unknown bit assignments :-<.  */
	ushort cur_addr;	/* Apparently the current buffer address(?) */
};

#define PAR_DATA	0
#define PAR_STATUS	1
#define PAR_CONTROL 2

#define Ctrl_LNibRead	0x08	/* LP_PSELECP */
#define Ctrl_HNibRead	0
#define Ctrl_LNibWrite	0x08	/* LP_PSELECP */
#define Ctrl_HNibWrite	0
#define Ctrl_SelData	0x04	/* LP_PINITP */
#define Ctrl_IRQEN	0x10	/* LP_PINTEN */

#define EOW	0xE0
#define EOC	0xE0
#define WrAddr	0x40	/* Set address of EPLC read, write register. */
#define RdAddr	0xC0
#define HNib	0x10

enum page0_regs {
	/* The first six registers hold
	 * the ethernet physical station address.
	 */
	PAR0 = 0, PAR1 = 1, PAR2 = 2, PAR3 = 3, PAR4 = 4, PAR5 = 5,
	TxCNT0 = 6, TxCNT1 = 7,		/* The transmit byte count. */
	TxSTAT = 8, RxSTAT = 9,		/* Tx and Rx status. */
	ISR = 10, IMR = 11,		/* Interrupt status and mask. */
	CMR1 = 12,			/* Command register 1. */
	CMR2 = 13,			/* Command register 2. */
	MODSEL = 14,		/* Mode select register. */
	MAR = 14,			/* Memory address register (?). */
	CMR2_h = 0x1d,
};

enum eepage_regs {
	PROM_CMD = 6,
	PROM_DATA = 7	/* Note that PROM_CMD is in the "high" bits. */
};

#define ISR_TxOK	0x01
#define ISR_RxOK	0x04
#define ISR_TxErr	0x02
#define ISRh_RxErr	0x11	/* ISR, high nibble */

#define CMR1h_MUX	0x08	/* Select printer multiplexor on 8012. */
#define CMR1h_RESET	0x04	/* Reset. */
#define CMR1h_RxENABLE	0x02	/* Rx unit enable.  */
#define CMR1h_TxENABLE	0x01	/* Tx unit enable.  */
#define CMR1h_TxRxOFF	0x00
#define CMR1_ReXmit	0x08	/* Trigger a retransmit. */
#define CMR1_Xmit	0x04	/* Trigger a transmit. */
#define	CMR1_IRQ	0x02	/* Interrupt active. */
#define	CMR1_BufEnb	0x01	/* Enable the buffer(?). */
#define	CMR1_NextPkt	0x01	/* Enable the buffer(?). */

#define CMR2_NULL	8
#define CMR2_IRQOUT	9
#define CMR2_RAMTEST	10
#define CMR2_EEPROM	12	/* Set to page 1, for reading the EEPROM. */

#define CMR2h_OFF	0	/* No accept mode. */
#define CMR2h_Physical	1	/* Accept a physical address match only. */
#define CMR2h_Normal	2	/* Accept physical and broadcast address. */
#define CMR2h_PROMISC	3	/* Promiscuous mode. */

/* An inline function used below: it differs from inb() by explicitly
 * return an unsigned char, saving a truncation.
 */
static inline unsigned char inbyte(unsigned short port)
{
	unsigned char _v;

	__asm__ __volatile__ ("inb %w1,%b0" : "=a" (_v) : "d" (port));
	return _v;
}

/* Read register OFFSET.
 * This command should always be terminated with read_end().
 */
static inline unsigned char read_nibble(short port, unsigned char offset)
{
	unsigned char retval;

	outb(EOC+offset, port + PAR_DATA);
	outb(RdAddr+offset, port + PAR_DATA);
	inbyte(port + PAR_STATUS);	/* Settling time delay */
	retval = inbyte(port + PAR_STATUS);
	outb(EOC+offset, port + PAR_DATA);

	return retval;
}

/* Functions for bulk data read.  The interrupt line is always disabled. */
/* Get a byte using read mode 0, reading data from the control lines. */
static inline unsigned char read_byte_mode0(short ioaddr)
{
	unsigned char low_nib;

	outb(Ctrl_LNibRead, ioaddr + PAR_CONTROL);
	inbyte(ioaddr + PAR_STATUS);
	low_nib = (inbyte(ioaddr + PAR_STATUS) >> 3) & 0x0f;
	outb(Ctrl_HNibRead, ioaddr + PAR_CONTROL);
	inbyte(ioaddr + PAR_STATUS);	/* Settling time delay -- needed!  */
	inbyte(ioaddr + PAR_STATUS);	/* Settling time delay -- needed!  */
	return low_nib | ((inbyte(ioaddr + PAR_STATUS) << 1) & 0xf0);
}

/* The same as read_byte_mode0(), but does multiple inb()s for stability. */
static inline unsigned char read_byte_mode2(short ioaddr)
{
	unsigned char low_nib;

	outb(Ctrl_LNibRead, ioaddr + PAR_CONTROL);
	inbyte(ioaddr + PAR_STATUS);
	low_nib = (inbyte(ioaddr + PAR_STATUS) >> 3) & 0x0f;
	outb(Ctrl_HNibRead, ioaddr + PAR_CONTROL);
	inbyte(ioaddr + PAR_STATUS);	/* Settling time delay -- needed!  */
	return low_nib | ((inbyte(ioaddr + PAR_STATUS) << 1) & 0xf0);
}

/* Read a byte through the data register. */
static inline unsigned char read_byte_mode4(short ioaddr)
{
	unsigned char low_nib;

	outb(RdAddr | MAR, ioaddr + PAR_DATA);
	low_nib = (inbyte(ioaddr + PAR_STATUS) >> 3) & 0x0f;
	outb(RdAddr | HNib | MAR, ioaddr + PAR_DATA);
	return low_nib | ((inbyte(ioaddr + PAR_STATUS) << 1) & 0xf0);
}

/* Read a byte through the data register, double reading to allow settling. */
static inline unsigned char read_byte_mode6(short ioaddr)
{
	unsigned char low_nib;

	outb(RdAddr | MAR, ioaddr + PAR_DATA);
	inbyte(ioaddr + PAR_STATUS);
	low_nib = (inbyte(ioaddr + PAR_STATUS) >> 3) & 0x0f;
	outb(RdAddr | HNib | MAR, ioaddr + PAR_DATA);
	inbyte(ioaddr + PAR_STATUS);
	return low_nib | ((inbyte(ioaddr + PAR_STATUS) << 1) & 0xf0);
}

static inline void
write_reg(short port, unsigned char reg, unsigned char value)
{
	unsigned char outval;

	outb(EOC | reg, port + PAR_DATA);
	outval = WrAddr | reg;
	outb(outval, port + PAR_DATA);
	outb(outval, port + PAR_DATA);	/* Double write for PS/2. */

	outval &= 0xf0;
	outval |= value;
	outb(outval, port + PAR_DATA);
	outval &= 0x1f;
	outb(outval, port + PAR_DATA);
	outb(outval, port + PAR_DATA);

	outb(EOC | outval, port + PAR_DATA);
}

static inline void
write_reg_high(short port, unsigned char reg, unsigned char value)
{
	unsigned char outval = EOC | HNib | reg;

	outb(outval, port + PAR_DATA);
	outval &= WrAddr | HNib | 0x0f;
	outb(outval, port + PAR_DATA);
	outb(outval, port + PAR_DATA);	/* Double write for PS/2. */

	outval = WrAddr | HNib | value;
	outb(outval, port + PAR_DATA);
	outval &= HNib | 0x0f;		/* HNib | value */
	outb(outval, port + PAR_DATA);
	outb(outval, port + PAR_DATA);

	outb(EOC | HNib | outval, port + PAR_DATA);
}

/* Write a byte out using nibble mode.  The low nibble is written first. */
static inline void
write_reg_byte(short port, unsigned char reg, unsigned char value)
{
	unsigned char outval;

	outb(EOC | reg, port + PAR_DATA); /* Reset the address register. */
	outval = WrAddr | reg;
	outb(outval, port + PAR_DATA);
	outb(outval, port + PAR_DATA);	/* Double write for PS/2. */

	outb((outval & 0xf0) | (value & 0x0f), port + PAR_DATA);
	outb(value & 0x0f, port + PAR_DATA);
	value >>= 4;
	outb(value, port + PAR_DATA);
	outb(0x10 | value, port + PAR_DATA);
	outb(0x10 | value, port + PAR_DATA);

	outb(EOC  | value, port + PAR_DATA); /* Reset the address register. */
}

/* Bulk data writes to the packet buffer.  The interrupt line remains enabled.
 * The first, faster method uses only the dataport (data modes 0, 2 & 4).
 * The second (backup) method uses data and control regs (modes 1, 3 & 5).
 * It should only be needed when there is skew between the individual data
 * lines.
 */
static inline void write_byte_mode0(short ioaddr, unsigned char value)
{
	outb(value & 0x0f, ioaddr + PAR_DATA);
	outb((value>>4) | 0x10, ioaddr + PAR_DATA);
}

static inline void write_byte_mode1(short ioaddr, unsigned char value)
{
	outb(value & 0x0f, ioaddr + PAR_DATA);
	outb(Ctrl_IRQEN | Ctrl_LNibWrite, ioaddr + PAR_CONTROL);
	outb((value>>4) | 0x10, ioaddr + PAR_DATA);
	outb(Ctrl_IRQEN | Ctrl_HNibWrite, ioaddr + PAR_CONTROL);
}

/* Write 16bit VALUE to the packet buffer: the same as above just doubled. */
static inline void write_word_mode0(short ioaddr, unsigned short value)
{
	outb(value & 0x0f, ioaddr + PAR_DATA);
	value >>= 4;
	outb((value & 0x0f) | 0x10, ioaddr + PAR_DATA);
	value >>= 4;
	outb(value & 0x0f, ioaddr + PAR_DATA);
	value >>= 4;
	outb((value & 0x0f) | 0x10, ioaddr + PAR_DATA);
}

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x04	/* EEPROM shift clock. */
#define EE_CS		0x02	/* EEPROM chip select. */
#define EE_CLK_HIGH	0x12
#define EE_CLK_LOW	0x16
#define EE_DATA_WRITE	0x01	/* EEPROM chip data in. */
#define EE_DATA_READ	0x08	/* EEPROM chip data out. */

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD(offset)	(((5 << 6) + (offset)) << 17)
#define EE_READ(offset)		(((6 << 6) + (offset)) << 17)
#define EE_ERASE(offset)	(((7 << 6) + (offset)) << 17)
#define EE_CMD_SIZE	27	/* The command+address+data size. */
