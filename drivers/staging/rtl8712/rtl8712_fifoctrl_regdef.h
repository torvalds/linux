/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTL8712_FIFOCTRL_REGDEF_H__
#define __RTL8712_FIFOCTRL_REGDEF_H__

#define RQPN			(RTL8712_FIFOCTRL_ + 0x00)
#define RXFF_BNDY		(RTL8712_FIFOCTRL_ + 0x0C)
#define RXRPT_BNDY		(RTL8712_FIFOCTRL_ + 0x10)
#define TXPKTBUF_PGBNDY		(RTL8712_FIFOCTRL_ + 0x14)
#define PBP			(RTL8712_FIFOCTRL_ + 0x15)
#define RX_DRVINFO_SZ		(RTL8712_FIFOCTRL_ + 0x16)
#define TXFF_STATUS		(RTL8712_FIFOCTRL_ + 0x17)
#define RXFF_STATUS		(RTL8712_FIFOCTRL_ + 0x18)
#define TXFF_EMPTY_TH		(RTL8712_FIFOCTRL_ + 0x19)
#define SDIO_RX_BLKSZ		(RTL8712_FIFOCTRL_ + 0x1C)
#define RXDMA_RXCTRL		(RTL8712_FIFOCTRL_ + 0x1D)
#define RXPKT_NUM		(RTL8712_FIFOCTRL_ + 0x1E)
#define RXPKT_NUM_C2H		(RTL8712_FIFOCTRL_ + 0x1F)
#define C2HCMD_UDT_SIZE		(RTL8712_FIFOCTRL_ + 0x20)
#define C2HCMD_UDT_ADDR		(RTL8712_FIFOCTRL_ + 0x22)
#define FIFOPAGE2		(RTL8712_FIFOCTRL_ + 0x24)
#define FIFOPAGE1		(RTL8712_FIFOCTRL_ + 0x28)
#define FW_RSVD_PG_CTRL		(RTL8712_FIFOCTRL_ + 0x30)
#define TXRPTFF_RDPTR		(RTL8712_FIFOCTRL_ + 0x40)
#define TXRPTFF_WTPTR		(RTL8712_FIFOCTRL_ + 0x44)
#define C2HFF_RDPTR		(RTL8712_FIFOCTRL_ + 0x48)
#define C2HFF_WTPTR		(RTL8712_FIFOCTRL_ + 0x4C)
#define RXFF0_RDPTR		(RTL8712_FIFOCTRL_ + 0x50)
#define RXFF0_WTPTR		(RTL8712_FIFOCTRL_ + 0x54)
#define RXFF1_RDPTR		(RTL8712_FIFOCTRL_ + 0x58)
#define RXFF1_WTPTR		(RTL8712_FIFOCTRL_ + 0x5C)
#define RXRPT0FF_RDPTR		(RTL8712_FIFOCTRL_ + 0x60)
#define RXRPT0FF_WTPTR		(RTL8712_FIFOCTRL_ + 0x64)
#define RXRPT1FF_RDPTR		(RTL8712_FIFOCTRL_ + 0x68)
#define RXRPT1FF_WTPTR		(RTL8712_FIFOCTRL_ + 0x6C)
#define RX0PKTNUM		(RTL8712_FIFOCTRL_ + 0x72)
#define RX1PKTNUM		(RTL8712_FIFOCTRL_ + 0x74)
#define RXFLTMAP0		(RTL8712_FIFOCTRL_ + 0x76)
#define RXFLTMAP1		(RTL8712_FIFOCTRL_ + 0x78)
#define RXFLTMAP2		(RTL8712_FIFOCTRL_ + 0x7A)
#define RXFLTMAP3		(RTL8712_FIFOCTRL_ + 0x7c)
#define TBDA			(RTL8712_FIFOCTRL_ + 0x84)
#define THPDA			(RTL8712_FIFOCTRL_ + 0x88)
#define TCDA			(RTL8712_FIFOCTRL_ + 0x8C)
#define TMDA			(RTL8712_FIFOCTRL_ + 0x90)
#define HDA			(RTL8712_FIFOCTRL_ + 0x94)
#define TVODA			(RTL8712_FIFOCTRL_ + 0x98)
#define TVIDA			(RTL8712_FIFOCTRL_ + 0x9C)
#define TBEDA			(RTL8712_FIFOCTRL_ + 0xA0)
#define TBKDA			(RTL8712_FIFOCTRL_ + 0xA4)
#define RCDA			(RTL8712_FIFOCTRL_ + 0xA8)
#define RDSA			(RTL8712_FIFOCTRL_ + 0xAC)
#define TXPKT_NUM_CTRL		(RTL8712_FIFOCTRL_ + 0xB0)
#define TXQ_PGADD		(RTL8712_FIFOCTRL_ + 0xB3)
#define TXFF_PG_NUM		(RTL8712_FIFOCTRL_ + 0xB4)



#endif	/* __RTL8712_FIFOCTRL_REGDEF_H__ */
