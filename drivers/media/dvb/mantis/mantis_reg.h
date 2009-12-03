/*
	Mantis PCI bridge driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __MANTIS_REG_H
#define __MANTIS_REG_H

// Interrupts
#define MANTIS_INT_STAT			0x00
#define MANTIS_INT_MASK			0x04

#define MANTIS_INT_RISCSTAT		(0x0f << 28)
#define MANTIS_INT_RISCEN		(0x01 << 27)
#define MANTIS_INT_I2CRACK		(0x01 << 26)

//#define MANTIS_INT_GPIF			(0xff << 12)

#define MANTIS_INT_PCMCIA7		(0x01 << 19)
#define MANTIS_INT_PCMCIA6		(0x01 << 18)
#define MANTIS_INT_PCMCIA5		(0x01 << 17)
#define MANTIS_INT_PCMCIA4		(0x01 << 16)
#define MANTIS_INT_PCMCIA3		(0x01 << 15)
#define MANTIS_INT_PCMCIA2		(0x01 << 14)
#define MANTIS_INT_PCMCIA1		(0x01 << 13)
#define MANTIS_INT_PCMCIA0		(0x01 << 12)
#define MANTIS_INT_IRQ1			(0x01 << 11)
#define MANTIS_INT_IRQ0			(0x01 << 10)
#define MANTIS_INT_OCERR		(0x01 <<  8)
#define MANTIS_INT_PABORT		(0x01 <<  7)
#define MANTIS_INT_RIPERR		(0x01 <<  6)
#define MANTIS_INT_PPERR		(0x01 <<  5)
#define MANTIS_INT_FTRGT		(0x01 <<  3)
#define MANTIS_INT_RISCI		(0x01 <<  1)
#define MANTIS_INT_I2CDONE		(0x01 <<  0)

// DMA
#define MANTIS_DMA_CTL			0x08
#define	MANTIS_I2C_RD			(0x01 <<  7)
#define MANTIS_I2C_WR			(0x01 <<  6)
#define MANTIS_DCAP_MODE		(0x01 <<  5)
#define MANTIS_FIFO_TP_4		(0x00 <<  3)
#define MANTIS_FIFO_TP_8		(0x01 <<  3)
#define MANTIS_FIFO_TP_16		(0x02 <<  3)
#define MANTIS_FIFO_EN			(0x01 <<  2)
#define MANTIS_DCAP_EN			(0x01 <<  1)
#define MANTIS_RISC_EN			(0x01 <<  0)

#define MANTIS_RISC_START		0x10
#define MANTIS_RISC_PC			0x14

// I2C
#define MANTIS_I2CDATA_CTL		0x18
#define MANTIS_I2C_RATE_1		(0x00 <<  6)
#define MANTIS_I2C_RATE_2		(0x01 <<  6)
#define MANTIS_I2C_RATE_3		(0x02 <<  6)
#define MANTIS_I2C_RATE_4		(0x03 <<  6)
#define MANTIS_I2C_STOP			(0x01 <<  5)
#define MANTIS_I2C_PGMODE		(0x01 <<  3)

#define MANTIS_GPIF_IRQCFG		0x98
#define MANTIS_GPIF_IRQPOL		(0x01 <<  8)
#define MANTIS_MASK_WRACK		(0x01 <<  7)
#define MANTIS_MASK_BRRDY		(0x01 <<  6)
#define MANTIS_MASK_OVFLW		(0x01 <<  5)
#define MANTIS_MASK_OTHERR		(0x01 <<  4)
#define MANTIS_MASK_WSTO		(0x01 <<  3)
#define MANTIS_MASK_EXTIRQ		(0x01 <<  2)
#define MANTIS_MASK_PLUGIN		(0x01 <<  1)
#define MANTIS_MASK_PLUGOUT		(0x01 <<  0)

#define MANTIS_GPIF_STATUS		0x9c
#define MANTIS_SBUF_KILLOP		(0x01 << 15)
#define MANTIS_SBUF_OPDONE		(0x01 << 14)
#define MANTIS_SBUF_EMPTY		(0x01 << 13)
#define MANTIS_GPIF_DETSTAT		(0x01 <<  9)
#define MANTIS_GPIF_INTSTAT		(0x01 <<  8)
#define MANTIS_GPIF_WRACK		(0x01 <<  7)
#define MANTIS_GPIF_BRRDY		(0x01 <<  6)
#define MANTIS_SBUF_OVFLW		(0x01 <<  5)
#define MANTIS_GPIF_OTHERR		(0x01 <<  4)
#define MANTIS_SBUF_WSTO		(0x01 <<  3)
#define MANTIS_GPIF_EXTIRQ		(0x01 <<  2)
#define MANTIS_CARD_PLUGIN		(0x01 <<  1)
#define MANTIS_CARD_PLUGOUT		(0x01 <<  0)

#define MANTIS_GPIF_ADDR		0xb0
#define MANTIS_GPIF_RDWRN		(0x01 << 31)

#define MANTIS_GPIF_DOUT		0xb4
#define MANTIS_GPIF_DIN			0xb8


#endif //__MANTIS_REG_H
