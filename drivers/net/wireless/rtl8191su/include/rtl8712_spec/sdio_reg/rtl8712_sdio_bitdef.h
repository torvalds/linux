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
#include "rtl8712_sdio_regdef.h"

#ifndef __RTL8712_SDIO_BITDEF_H__
#define __RTL8712_SDIO_BITDEF_H__

#define _TX_CRC_RPT_EN_SHT			0
#define _TX_CRC_RPT_EN				BIT(_TX_CRC_RPT_EN_SHT)
#define _TX_CRC_RPT_EN_MSK			0xFE
#define _CMD_ERR_STOP_INT_EN_SHT	1
#define _CMD_ERR_STOP_INT_EN		BIT(_CMD_ERR_STOP_INT_EN_SHT)
#define _CMD_ERR_STOP_INT_EN_MSK	0xFD

#define	_RX0_BLK_NUM	0x0040
#define	_C2H_BLK_NUM	0x0041

//SDIO_HISR
#define	_RXDONE_SHT			0
#define	_RXDONE				BIT(_RXDONE_SHT)
#define _C2HCMD_SHT			1
#define	_C2HCMD				BIT(_C2HCMD_SHT)
#define _BMCQ_AVAL_IND_SHT	2
#define	_BMCQ_AVAL_IND		BIT(_BMCQ_AVAL_IND_SHT)
#define _VOQ_AVAL_IND_SHT	3
#define	_VOQ_AVAL_IND		BIT(_VOQ_AVAL_IND_SHT)
#define _VIQ_AVAL_IND_SHT	4
#define	_VIQ_AVAL_IND		BIT(_VIQ_AVAL_IND_SHT)
#define _BEQ_AVAL_IND_SHT	5
#define	_BEQ_AVAL_IND		BIT(_BEQ_AVAL_IND_SHT)
#define _BKQ_AVAL_IND_SHT	6
#define	_BKQ_AVAL_IND		BIT(_BKQ_AVAL_IND_SHT)
#define _CPWM_INT_SHT		7
#define _CPWM_INT			BIT(_CPWM_INT_SHT)
#define _RXOVF_SHT			8
#define	_RXOVF				BIT(_RXOVF_SHT)
#define _TXOVF_SHT			9
#define	_TXOVF				BIT(_TXOVF_SHT)
#define _CPUERR_SHT			10
#define	_CPUERR_			BIT(_CPUERR_SHT)

#endif // __RTL8712_TIMECTRL_BITDEF_H__


