/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
/* ************************************************************
 * File Name: hal8188ereg.h
 *
 * Description:
 *
 * This file is for RTL8188E register definition.
 *
 *
 * ************************************************************ */
#ifndef __HAL_8188E_REG_H__
#define __HAL_8188E_REG_H__

/*
 * Register Definition
 *   */
#define TRX_ANTDIV_PATH 0x860
#define RX_ANTDIV_PATH 0xb2c
#define ODM_R_A_AGC_CORE1_8188E 0xc50

#define REG_GPIO_EXT_CTRL 0x0060

#define REG_MCUFWDL_8188E 0x0080
#define REG_FW_DBG_STATUS_8188E 0x0088
#define REG_FW_DBG_CTRL_8188E 0x008F

#define REG_CR_8188E 0x0100

/*
 * Bitmap Definition
 *   */
#define BIT_FA_RESET_8188E BIT(0)
#define REG_ADAPTIVE_DATA_RATE_0 0x2B0
#define REG_DBI_WDATA_8188 0x0348 /* DBI Write data */
#define REG_DBI_RDATA_8188 0x034C /* DBI Read data */
#define REG_DBI_ADDR_8188 0x0350 /* DBI Address */
#define REG_DBI_FLAG_8188 0x0352 /* DBI Read/Write Flag */
#define REG_MDIO_WDATA_8188E 0x0354 /* MDIO for Write PCIE PHY */
#define REG_MDIO_RDATA_8188E 0x0356 /* MDIO for Reads PCIE PHY */
#define REG_MDIO_CTL_8188E 0x0358 /* MDIO for Control */

/* [0-63] */
#define REG_MACID_NO_LINK 0x484 /* No Link register (bit[x] enabled means dropping packets for MACID in HW queue) */

#endif
