/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
 *******************************************************************************/
#ifndef __RTL8821A_SPEC_H__
#define __RTL8821A_SPEC_H__

#include <drv_conf.h>
/* This file should based on "hal_com_reg.h" */
#include <hal_com_reg.h>
/* Because 8812a and 8821a is the same serial,
 * most of 8821a register definitions are the same as 8812a. */
#include <rtl8812a_spec.h>


/* ************************************************************
 * 8821A Regsiter offset definition
 * ************************************************************ */

/* ************************************************************
 * MAC register
 * ************************************************************ */

/* -----------------------------------------------------
 *	0x0000h ~ 0x00FFh	System Configuration
 * ----------------------------------------------------- */

/* -----------------------------------------------------
 *	0x0100h ~ 0x01FFh	MACTOP General Configuration
 * ----------------------------------------------------- */
#define REG_WOWLAN_WAKE_REASON          REG_MCUTST_WOWLAN

/* -----------------------------------------------------
 *	0x0200h ~ 0x027Fh	TXDMA Configuration
 * ----------------------------------------------------- */

/* -----------------------------------------------------
 *	0x0280h ~ 0x02FFh	RXDMA Configuration
 * ----------------------------------------------------- */

/* -----------------------------------------------------
 *	0x0300h ~ 0x03FFh	PCIe
 * ----------------------------------------------------- */

/* -----------------------------------------------------
 *	0x0400h ~ 0x047Fh	Protocol Configuration
 * ----------------------------------------------------- */

/* -----------------------------------------------------
 *	0x0500h ~ 0x05FFh	EDCA Configuration
 * ----------------------------------------------------- */

/* -----------------------------------------------------
 *	0x0600h ~ 0x07FFh	WMAC Configuration
 * ----------------------------------------------------- */


/* ************************************************************
 * SDIO Bus Specification
 * ************************************************************ */

/* -----------------------------------------------------
 * SDIO CMD Address Mapping
 * ----------------------------------------------------- */

/* -----------------------------------------------------
 * I/O bus domain (Host)
 * ----------------------------------------------------- */

/* -----------------------------------------------------
 * SDIO register
 * ----------------------------------------------------- */
#undef SDIO_REG_HCPWM1
#define SDIO_REG_FREE_TXPG2		0x024
#define SDIO_REG_HCPWM1			0x025


/* ************************************************************
 * Regsiter Bit and Content definition
 * ************************************************************ */

#endif /* __RTL8821A_SPEC_H__ */
