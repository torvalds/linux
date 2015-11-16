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
#ifndef __RTL8712_CMDCTRL_BITDEF_H__
#define __RTL8712_CMDCTRL_BITDEF_H__

/*
 * 2. Command Control Registers	 (Offset: 0x0040 - 0x004F)
 */
/*--------------------------------------------------------------------------*/
/*       8192S (CMD) command register bits	(Offset 0x40, 16 bits)*/
/*--------------------------------------------------------------------------*/
#define		_APSDOFF_STATUS		BIT(15)
#define		_APSDOFF		BIT(14)
#define		_BBRSTn			BIT(13)  /*Enable OFDM/CCK*/
#define		_BB_GLB_RSTn		BIT(12)   /*Enable BB*/
#define		_SCHEDULE_EN		BIT(10)  /*Enable MAC scheduler*/
#define		_MACRXEN		BIT(9)
#define		_MACTXEN		BIT(8)
#define		_DDMA_EN		BIT(7)  /*FW off load function enable*/
#define		_FW2HW_EN		BIT(6)  /*MAC every module reset */
#define		_RXDMA_EN		BIT(5)
#define		_TXDMA_EN		BIT(4)
#define		_HCI_RXDMA_EN		BIT(3)
#define		_HCI_TXDMA_EN		BIT(2)

/*TXPAUSE*/
#define	_STOPHCCA			BIT(6)
#define	_STOPHIGH			BIT(5)
#define	_STOPMGT			BIT(4)
#define	_STOPVO				BIT(3)
#define	_STOPVI				BIT(2)
#define	_STOPBE				BIT(1)
#define	_STOPBK				BIT(0)

/*TCR*/
#define	_DISCW				BIT(20)
#define	_ICV				BIT(19)
#define	_CFEND_FMT			BIT(17)
#define	_CRC				BIT(16)
#define	_FWRDY				BIT(7)
#define _BASECHG			BIT(6)
#define	_IMEM_RDY			BIT(5)
#define _DMEM_CODE_DONE			BIT(4)
#define _EMEM_CHK_RPT			BIT(3)
#define _EMEM_CODE_DONE			BIT(2)
#define _IMEM_CHK_RPT			BIT(1)
#define _IMEM_CODE_DONE			BIT(0)

#define	_TXDMA_INIT_VALUE	(_IMEM_CHK_RPT|_EMEM_CHK_RPT)

/*RCR*/
#define	_ENMBID				BIT(27)
#define	_APP_PHYST_RXFF			BIT(25)
#define	_APP_PHYST_STAFF		BIT(24)
#define	_CBSSID				BIT(23)
#define	_APWRMGT			BIT(22)
#define	_ADD3				BIT(21)
#define	_AMF				BIT(20)
#define	_ACF				BIT(19)
#define	_ADF				BIT(18)
#define	_APP_MIC			BIT(17)
#define	_APP_ICV			BIT(16)
#define	_RXFTH_MSK			0x0000E000
#define	_RXFTH_SHT			13
#define	_AICV				BIT(12)
#define	_RXPKTLMT_MSK			0x00000FC0
#define	_RXPKTLMT_SHT			6
#define	_ACRC32				BIT(5)
#define	_AB				BIT(3)
#define	_AM				BIT(2)
#define	_APM				BIT(1)
#define	_AAP				BIT(0)

/*MSR*/
#define	_NETTYPE_MSK			0x03
#define	_NETTYPE_SHT			0

/*BT*/
#define _BTMODE_MSK			0x06
#define _BTMODE_SHT			1
#define _ENBT				BIT(0)

/*MBIDCTRL*/
#define	_ENMBID_MODE			BIT(15)
#define	_BCNNO_MSK			0x7000
#define	_BCNNO_SHT			12
#define	_BCNSPACE_MSK			0x0FFF
#define	_BCNSPACE_SHT			0


#endif /* __RTL8712_CMDCTRL_BITDEF_H__*/

