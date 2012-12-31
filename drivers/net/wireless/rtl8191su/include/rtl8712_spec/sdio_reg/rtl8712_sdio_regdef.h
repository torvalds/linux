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


#ifndef __RTL8712_SDIO_REGDEF_H__
#define __RTL8712_SDIO_REGDEF_H__


#define	RTL8712_SDIO_LOCAL_BASE		0X10100000
#define	RTL8712_SDIO_LOCAL_MSK		0x10101FFF

#define	SDIO_TX_CTRL			(RTL8712_SDIO_LOCAL_BASE + 0x0000)

#define	SDIO_BKQ_FREEPG			(RTL8712_SDIO_LOCAL_BASE + 0x0008)
#define	SDIO_BEQ_FREEPG			(RTL8712_SDIO_LOCAL_BASE + 0x0007)
#define	SDIO_VIQ_FREEPG			(RTL8712_SDIO_LOCAL_BASE + 0x0006)
#define	SDIO_VOQ_FREEPG			(RTL8712_SDIO_LOCAL_BASE + 0x0005)
#define	SDIO_HIQ_FREEPG			(RTL8712_SDIO_LOCAL_BASE + 0X0004)
#define	SDIO_CMDQ_FREEPG		(RTL8712_SDIO_LOCAL_BASE + 0x0003)
#define	SDIO_MGTQ_FREEPG		(RTL8712_SDIO_LOCAL_BASE + 0x0002)
#define	SDIO_BCNQ_FREEPG		(RTL8712_SDIO_LOCAL_BASE + 0x0001)

#define   SDIO_FREE_TXPG 		(RTL8712_SDIO_LOCAL_BASE + 0x0001)
// don't change the following define
#define BKQ_FREEPG_INX 7
#define BEQ_FREEPG_INX 6
#define VIQ_FREEPG_INX 5
#define VOQ_FREEPG_INX 4
#define HIQ_FREEPG_INX 3
#define CMDQ_FREEPG_INX 2
#define MGTQ_FREEPG_INX 1
#define BCNQ_FREEPG_INX 0


#define	SDIO_HIMR				(RTL8712_SDIO_LOCAL_BASE + 0x0009)
#define	SDIO_HISR				(RTL8712_SDIO_LOCAL_BASE + 0x0010)

#define	SDIO_RX0_RDYBLK_NUM		(RTL8712_SDIO_LOCAL_BASE + 0x0040)
#define	SDIO_C2H_RDYBLK_NUM		(RTL8712_SDIO_LOCAL_BASE + 0x0048)
#define	SDIO_RXDMA_OK_TIMER		(RTL8712_SDIO_LOCAL_BASE + 0x0042)

#define	SDIO_HRPWM				(RTL8712_SDIO_LOCAL_BASE + 0x0080)
#define	SDIO_HCPWM				(RTL8712_SDIO_LOCAL_BASE + 0x0081)

#define	SDIOERR_RPT				(RTL8712_SDIO_LOCAL_BASE + 0x00C0)
#define	SDIO_CMD_ERRCNT			(RTL8712_SDIO_LOCAL_BASE + 0x00C1)
#define	SDIO_DARA_ERRCNT		(RTL8712_SDIO_LOCAL_BASE + 0x00C2)
#define	SDIO_DATA_CRCERR_CTRL	(RTL8712_SDIO_LOCAL_BASE + 0x00FC)

#define	SDIO_DBG_SEL			(RTL8712_SDIO_LOCAL_BASE + 0x00FF)


#endif // __RTL8712_SDIO_REGDEF_H__

