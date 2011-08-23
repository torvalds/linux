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
#ifndef __RTL8712_SECURITY_BITDEF_H__
#define __RTL8712_SECURITY_BITDEF_H__

/*CAMCMD*/
#define	_SECCAM_POLLING				BIT(31)
#define	_SECCAM_CLR					BIT(30)
#define	_SECCAM_WE					BIT(16)
#define	_SECCAM_ADR_MSK				0x000000FF
#define	_SECCAM_ADR_SHT				0

/*CAMDBG*/
#define	_SECCAM_INFO				BIT(31)
#define	_SEC_KEYFOUND				BIT(30)
#define	_SEC_CONFIG_MSK				0x3F000000
#define	_SEC_CONFIG_SHT				24
#define	_SEC_KEYCONTENT_MSK			0x00FFFFFF
#define	_SEC_KEYCONTENT_SHT			0

/*SECCFG*/
#define	_NOSKMC						BIT(5)
#define	_SKBYA2						BIT(4)
#define	_RXDEC						BIT(3)
#define	_TXENC						BIT(2)
#define	_RXUSEDK					BIT(1)
#define	_TXUSEDK					BIT(0)


#endif	/*__RTL8712_SECURITY_BITDEF_H__*/

