/*
 * Marvell 88SE64xx hardware specific head file
 *
 * Copyright 2007 Red Hat, Inc.
 * Copyright 2008 Marvell. <kewei@marvell.com>
 * Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
*/

#ifndef _MVS64XX_REG_H_
#define _MVS64XX_REG_H_

#include <linux/types.h>

#define MAX_LINK_RATE		SAS_LINK_RATE_3_0_GBPS

/* enhanced mode registers (BAR4) */
enum hw_registers {
	MVS_GBL_CTL		= 0x04,  /* global control */
	MVS_GBL_INT_STAT	= 0x08,  /* global irq status */
	MVS_GBL_PI		= 0x0C,  /* ports implemented bitmask */

	MVS_PHY_CTL		= 0x40,  /* SOC PHY Control */
	MVS_PORTS_IMP		= 0x9C,  /* SOC Port Implemented */

	MVS_GBL_PORT_TYPE	= 0xa0,  /* port type */

	MVS_CTL			= 0x100, /* SAS/SATA port configuration */
	MVS_PCS			= 0x104, /* SAS/SATA port control/status */
	MVS_CMD_LIST_LO		= 0x108, /* cmd list addr */
	MVS_CMD_LIST_HI		= 0x10C,
	MVS_RX_FIS_LO		= 0x110, /* RX FIS list addr */
	MVS_RX_FIS_HI		= 0x114,

	MVS_TX_CFG		= 0x120, /* TX configuration */
	MVS_TX_LO		= 0x124, /* TX (delivery) ring addr */
	MVS_TX_HI		= 0x128,

	MVS_TX_PROD_IDX		= 0x12C, /* TX producer pointer */
	MVS_TX_CONS_IDX		= 0x130, /* TX consumer pointer (RO) */
	MVS_RX_CFG		= 0x134, /* RX configuration */
	MVS_RX_LO		= 0x138, /* RX (completion) ring addr */
	MVS_RX_HI		= 0x13C,
	MVS_RX_CONS_IDX		= 0x140, /* RX consumer pointer (RO) */

	MVS_INT_COAL		= 0x148, /* Int coalescing config */
	MVS_INT_COAL_TMOUT	= 0x14C, /* Int coalescing timeout */
	MVS_INT_STAT		= 0x150, /* Central int status */
	MVS_INT_MASK		= 0x154, /* Central int enable */
	MVS_INT_STAT_SRS_0	= 0x158, /* SATA register set status */
	MVS_INT_MASK_SRS_0	= 0x15C,

					 /* ports 1-3 follow after this */
	MVS_P0_INT_STAT		= 0x160, /* port0 interrupt status */
	MVS_P0_INT_MASK		= 0x164, /* port0 interrupt mask */
					 /* ports 5-7 follow after this */
	MVS_P4_INT_STAT		= 0x200, /* Port4 interrupt status */
	MVS_P4_INT_MASK		= 0x204, /* Port4 interrupt enable mask */

					 /* ports 1-3 follow after this */
	MVS_P0_SER_CTLSTAT	= 0x180, /* port0 serial control/status */
					 /* ports 5-7 follow after this */
	MVS_P4_SER_CTLSTAT	= 0x220, /* port4 serial control/status */

	MVS_CMD_ADDR		= 0x1B8, /* Command register port (addr) */
	MVS_CMD_DATA		= 0x1BC, /* Command register port (data) */

					 /* ports 1-3 follow after this */
	MVS_P0_CFG_ADDR		= 0x1C0, /* port0 phy register address */
	MVS_P0_CFG_DATA		= 0x1C4, /* port0 phy register data */
					 /* ports 5-7 follow after this */
	MVS_P4_CFG_ADDR		= 0x230, /* Port4 config address */
	MVS_P4_CFG_DATA		= 0x234, /* Port4 config data */

					 /* ports 1-3 follow after this */
	MVS_P0_VSR_ADDR		= 0x1E0, /* port0 VSR address */
	MVS_P0_VSR_DATA		= 0x1E4, /* port0 VSR data */
					 /* ports 5-7 follow after this */
	MVS_P4_VSR_ADDR		= 0x250, /* port4 VSR addr */
	MVS_P4_VSR_DATA		= 0x254, /* port4 VSR data */
};

enum pci_cfg_registers {
	PCR_PHY_CTL		= 0x40,
	PCR_PHY_CTL2		= 0x90,
	PCR_DEV_CTRL		= 0xE8,
	PCR_LINK_STAT		= 0xF2,
};

/*  SAS/SATA Vendor Specific Port Registers */
enum sas_sata_vsp_regs {
	VSR_PHY_STAT		= 0x00, /* Phy Status */
	VSR_PHY_MODE1		= 0x01, /* phy tx */
	VSR_PHY_MODE2		= 0x02, /* tx scc */
	VSR_PHY_MODE3		= 0x03, /* pll */
	VSR_PHY_MODE4		= 0x04, /* VCO */
	VSR_PHY_MODE5		= 0x05, /* Rx */
	VSR_PHY_MODE6		= 0x06, /* CDR */
	VSR_PHY_MODE7		= 0x07, /* Impedance */
	VSR_PHY_MODE8		= 0x08, /* Voltage */
	VSR_PHY_MODE9		= 0x09, /* Test */
	VSR_PHY_MODE10		= 0x0A, /* Power */
	VSR_PHY_MODE11		= 0x0B, /* Phy Mode */
	VSR_PHY_VS0		= 0x0C, /* Vednor Specific 0 */
	VSR_PHY_VS1		= 0x0D, /* Vednor Specific 1 */
};

enum chip_register_bits {
	PHY_MIN_SPP_PHYS_LINK_RATE_MASK = (0xF << 8),
	PHY_MAX_SPP_PHYS_LINK_RATE_MASK = (0xF << 12),
	PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET = (16),
	PHY_NEG_SPP_PHYS_LINK_RATE_MASK =
			(0xF << PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET),
};

#define MAX_SG_ENTRY		64

struct mvs_prd {
	__le64			addr;		/* 64-bit buffer address */
	__le32			reserved;
	__le32			len;		/* 16-bit length */
};

#define SPI_CTRL_REG				0xc0
#define SPI_CTRL_VENDOR_ENABLE		(1U<<29)
#define SPI_CTRL_SPIRDY         		(1U<<22)
#define SPI_CTRL_SPISTART			(1U<<20)

#define SPI_CMD_REG		0xc4
#define SPI_DATA_REG		0xc8

#define SPI_CTRL_REG_64XX		0x10
#define SPI_CMD_REG_64XX		0x14
#define SPI_DATA_REG_64XX		0x18

#endif
