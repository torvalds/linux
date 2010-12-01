/*
 * arch/arm/mach-tegra/include/mach/tegra2_fuse.h
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MACH_TEGRA2_FUSE_H
#define __MACH_TEGRA2_FUSE_H

#define SBK_DEVKEY_STATUS_SZ	sizeof(u32)

/* fuse io parameters */
enum fuse_io_param {
	DEVKEY,
	JTAG_DIS,
	/*
	 * Programming the odm production fuse at the same
	 * time as the sbk or dev_key is not allowed as it is not possible to
	 * verify that the sbk or dev_key were programmed correctly.
	 */
	ODM_PROD_MODE,
	SEC_BOOT_DEV_CFG,
	SEC_BOOT_DEV_SEL,
	SBK,
	SW_RSVD,
	IGNORE_DEV_SEL_STRAPS,
	ODM_RSVD,
	SBK_DEVKEY_STATUS,
	MASTER_ENB,
	_PARAMS_U32 = 0x7FFFFFFF
};

#define MAX_PARAMS ODM_RSVD

/* the order of the members is pre-decided. please do not change */
struct fuse_data {
	u32 devkey;
	u32 jtag_dis;
	u32 odm_prod_mode;
	u32 bootdev_cfg;
	u32 bootdev_sel;
	u32 sbk[4];
	u32 sw_rsvd;
	u32 ignore_devsel_straps;
	u32 odm_rsvd[8];
};

/* secondary boot device options */
enum {
	SECBOOTDEV_SDMMC,
	SECBOOTDEV_NOR,
	SECBOOTDEV_SPI,
	SECBOOTDEV_NAND,
	SECBOOTDEV_LBANAND,
	SECBOOTDEV_MUXONENAND,
	_SECBOOTDEV_MAX,
	_SECBOOTDEV_U32 = 0x7FFFFFFF
};

/*
 * read the fuse settings
 * @param: io_param_type - param type enum
 * @param: size - read size in bytes
 */
int tegra_fuse_read(u32 io_param_type, u32 *data, int size);

#define FLAGS_DEVKEY			BIT(DEVKEY)
#define FLAGS_JTAG_DIS			BIT(JTAG_DIS)
#define FLAGS_SBK_DEVKEY_STATUS	BIT(SBK_DEVKEY_STATUS)
#define FLAGS_ODM_PROD_MODE		BIT(ODM_PROD_MODE)
#define FLAGS_SEC_BOOT_DEV_CFG	BIT(SEC_BOOT_DEV_CFG)
#define FLAGS_SEC_BOOT_DEV_SEL	BIT(SEC_BOOT_DEV_SEL)
#define FLAGS_SBK			BIT(SBK)
#define FLAGS_SW_RSVD		BIT(SW_RSVD)
#define FLAGS_IGNORE_DEV_SEL_STRAPS	BIT(IGNORE_DEV_SEL_STRAPS)
#define FLAGS_ODMRSVD			BIT(ODM_RSVD)

/*
 * Prior to invoking this routine, the caller is responsible for supplying
 * valid fuse programming voltage.
 *
 * @param: pgm_data - entire data to be programmed
 * @flags: program flags (e.g. FLAGS_DEVKEY)
 */
int tegra_fuse_program(struct fuse_data *pgm_data, u32 flags);

/* Disables the fuse programming until the next system reset */
void tegra_fuse_program_disable(void);

#endif
