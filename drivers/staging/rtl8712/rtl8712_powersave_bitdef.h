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
#ifndef __RTL8712_POWERSAVE_BITDEF_H__
#define __RTL8712_POWERSAVE_BITDEF_H__

/*WOWCTRL*/
#define	_UWF			BIT(3)
#define	_MAGIC			BIT(2)
#define	_WOW_EN			BIT(1)
#define	_PMEN			BIT(0)

/*PSSTATUS*/
#define	_PSSTATUS_SEL_MSK		0x0F

/*PSSWITCH*/
#define	_PSSWITCH_ACT			BIT(7)
#define	_PSSWITCH_SEL_MSK		0x0F
#define	_PSSWITCH_SEL_SHT		0

/*LPNAV_CTRL*/
#define	_LPNAV_EN			BIT(31)
#define	_LPNAV_EARLY_MSK		0x7FFF0000
#define	_LPNAV_EARLY_SHT		16
#define	_LPNAV_TH_MSK			0x0000FFFF
#define	_LPNAV_TH_SHT			0

/*RPWM*/
/*CPWM*/
#define	_TOGGLING			BIT(7)
#define	_WWLAN				BIT(3)
#define	_RPS_ST				BIT(2)
#define	_WLAN_TRX			BIT(1)
#define	_SYS_CLK			BIT(0)

#endif /* __RTL8712_POWERSAVE_BITDEF_H__*/
