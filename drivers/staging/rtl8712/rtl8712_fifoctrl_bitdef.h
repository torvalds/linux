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
#ifndef __RTL8712_FIFOCTRL_BITDEF_H__
#define __RTL8712_FIFOCTRL_BITDEF_H__

/*PBP*/
#define	_PSTX_MSK			0xF0
#define	_PSTX_SHT			4
#define	_PSRX_MSK			0x0F
#define	_PSRX_SHT			0

/*TXFF_STATUS*/
#define	_TXSTATUS_OVF		BIT(15)

/*RXFF_STATUS*/
#define	_STATUSFF1_OVF		BIT(7)
#define	_STATUSFF1_EMPTY	BIT(6)
#define	_STATUSFF0_OVF		BIT(5)
#define	_STATUSFF0_EMPTY	BIT(4)
#define	_RXFF1_OVF			BIT(3)
#define	_RXFF1_EMPTY		BIT(2)
#define	_RXFF0_OVF			BIT(1)
#define	_RXFF0_EMPTY		BIT(0)

/*TXFF_EMPTY_TH*/
#define	_BKQ_EMPTY_TH_MSK	0x0F0000
#define	_BKQ_EMPTY_TH_SHT	16
#define	_BEQ_EMPTY_TH_MSK	0x00F000
#define	_BEQ_EMPTY_TH_SHT	12
#define	_VIQ_EMPTY_TH_MSK	0x000F00
#define	_VIQ_EMPTY_TH_SHT	8
#define	_VOQ_EMPTY_TH_MSK	0x0000F0
#define	_VOQ_EMPTY_TH_SHT	4
#define	_BMCQ_EMPTY_TH_MSK	0x00000F
#define	_BMCQ_EMPTY_TH_SHT	0

/*SDIO_RX_BLKSZ*/
#define	_SDIO_RX_BLKSZ_MSK	0x07

/*RXDMA_CTRL*/
#define	_C2HFF_POLL		BIT(4)
#define	_RXPKT_POLL		BIT(0)

/*RXPKT_NUM*/
#define	_RXCMD_NUM_MSK		0xFF00
#define	_RXCMD_NUM_SHT		8
#define	_RXFF0_NUM_MSK		0x00FF
#define	_RXFF0_NUM_SHT		0

/*FIFOPAGE2*/
#define	_PUB_AVAL_PG_MSK	0xFFFF0000
#define	_PUB_AVAL_PG_SHT	16
#define	_BCN_AVAL_PG_MSK	0x0000FFFF
#define	_BCN_AVAL_PG_SHT	0

/*RX0PKTNUM*/
#define	_RXFF0_DEC_POLL				BIT(15)
#define	_RXFF0_PKT_DEC_NUM_MSK		0x3F00
#define	_RXFF0_PKT_DEC_NUM_SHT		8
#define	_RXFF0_PKTNUM_RPT_MSK		0x00FF
#define	_RXFF0_PKTNUM_RPT_SHT		0

/*RX1PKTNUM*/
#define	_RXFF1_DEC_POLL				BIT(15)
#define	_RXFF1_PKT_DEC_NUM_MSK		0x3F00
#define	_RXFF1_PKT_DEC_NUM_SHT		8
#define	_RXFF1_PKTNUM_RPT_MSK		0x00FF
#define	_RXFF1_PKTNUM_RPT_SHT		0

/*RXFLTMAP0*/
#define	_MGTFLT13EN		BIT(13)
#define	_MGTFLT12EN		BIT(12)
#define	_MGTFLT11EN		BIT(11)
#define	_MGTFLT10EN		BIT(10)
#define	_MGTFLT9EN		BIT(9)
#define	_MGTFLT8EN		BIT(8)
#define	_MGTFLT5EN		BIT(5)
#define	_MGTFLT4EN		BIT(4)
#define	_MGTFLT3EN		BIT(3)
#define	_MGTFLT2EN		BIT(2)
#define	_MGTFLT1EN		BIT(1)
#define	_MGTFLT0EN		BIT(0)

/*RXFLTMAP1*/
#define	_CTRLFLT15EN	BIT(15)
#define	_CTRLFLT14EN	BIT(14)
#define	_CTRLFLT13EN	BIT(13)
#define	_CTRLFLT12EN	BIT(12)
#define	_CTRLFLT11EN	BIT(11)
#define	_CTRLFLT10EN	BIT(10)
#define	_CTRLFLT9EN		BIT(9)
#define	_CTRLFLT8EN		BIT(8)
#define	_CTRLFLT7EN		BIT(7)
#define	_CTRLFLT6EN		BIT(6)

/*RXFLTMAP2*/
#define	_DATAFLT15EN	BIT(15)
#define	_DATAFLT14EN	BIT(14)
#define	_DATAFLT13EN	BIT(13)
#define	_DATAFLT12EN	BIT(12)
#define	_DATAFLT11EN	BIT(11)
#define	_DATAFLT10EN	BIT(10)
#define	_DATAFLT9EN		BIT(9)
#define	_DATAFLT8EN		BIT(8)
#define	_DATAFLT7EN		BIT(7)
#define	_DATAFLT6EN		BIT(6)
#define	_DATAFLT5EN		BIT(5)
#define	_DATAFLT4EN		BIT(4)
#define	_DATAFLT3EN		BIT(3)
#define	_DATAFLT2EN		BIT(2)
#define	_DATAFLT1EN		BIT(1)
#define	_DATAFLT0EN		BIT(0)

/*RXFLTMAP3*/
#define	_MESHAFLT1EN		BIT(1)
#define	_MESHAFLT0EN		BIT(0)

/*TXPKT_NUM_CTRL*/
#define	_TXPKTNUM_DEC		BIT(8)
#define	_TXPKTNUM_MSK		0x00FF
#define	_TXPKTNUM_SHT		0

/*TXFF_PG_NUM*/
#define	_TXFF_PG_NUM_MSK	0x0FFF


#endif	/*	__RTL8712_FIFOCTRL_BITDEF_H__ */

