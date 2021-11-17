/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTL8712_TIMECTRL_BITDEF_H__
#define __RTL8712_TIMECTRL_BITDEF_H__

/*TSFTR*/
/*SLOT*/
/*USTIME*/

/*TUBASE*/
#define	_TUBASE_MSK			0x07FF

/*SIFS_CCK*/
#define	_SIFS_CCK_TRX_MSK		0xFF00
#define	_SIFS_CCK_TRX_SHT		0x8
#define	_SIFS_CCK_CTX_MSK		0x00FF
#define	_SIFS_CCK_CTX_SHT		0

/*SIFS_OFDM*/
#define	_SIFS_OFDM_TRX_MSK		0xFF00
#define	_SIFS_OFDM_TRX_SHT		0x8
#define	_SIFS_OFDM_CTX_MSK		0x00FF
#define	_SIFS_OFDM_CTX_SHT		0

/*PIFS*/
/*ACKTO*/
/*EIFS*/
/*BCNITV*/
/*ATIMWND*/

/*DRVERLYINT*/
#define	_ENSWBCN				BIT(15)
#define	_DRVERLY_TU_MSK			0x0FF0
#define	_DRVERLY_TU_SHT			4
#define	_DRVERLY_US_MSK			0x000F
#define	_DRVERLY_US_SHT			0

/*BCNDMATIM*/
#define	_BCNDMATIM_MSK			0x03FF

/*BCNERRTH*/
/*MLT*/


#endif /* __RTL8712_TIMECTRL_BITDEF_H__*/

