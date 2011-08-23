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
#ifndef __RTL8712_MACSETTING_BITDEF_H__
#define __RTL8712_MACSETTING_BITDEF_H__


/*MACID*/
/*BSSID*/

/*HWVID*/
#define	_HWVID_MSK				0x0F

/*MAR*/
/*MBIDCANCONTENT*/

/*MBIDCANCFG*/
#define	_POOLING				BIT(31)
#define	_WRITE_EN				BIT(16)
#define	_CAM_ADDR_MSK			0x001F
#define	_CAM_ADDR_SHT			0

/*BUILDTIME*/
#define _BUILDTIME_MSK			0x3FFFFFFF

/*BUILDUSER*/



#endif /* __RTL8712_MACSETTING_BITDEF_H__*/

