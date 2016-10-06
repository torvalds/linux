/*-
 * Copyright 2013 Broadcom Corporation
 *
 * Copyright (c) 2009-2010 Micron Technology, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Henry Pan <hspan@micron.com>
 *
 * based on nand.h
 */
#ifndef __LINUX_MTD_SPI_NAND_H
#define __LINUX_MTD_SPI_NAND_H

#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/mtd/mtd.h>

/* cmd */
#define CMD_READ			0x13
#define CMD_READ_RDM			0x03
#define CMD_PROG_PAGE_CLRCACHE		0x02
#define CMD_PROG_PAGE			0x84
#define CMD_PROG_PAGE_EXC		0x10
#define CMD_ERASE_BLK			0xd8
#define CMD_WR_ENABLE			0x06
#define CMD_WR_DISABLE			0x04
#define CMD_READ_ID			0x9f
#define CMD_RESET			0xff
#define CMD_READ_REG			0x0f
#define CMD_WRITE_REG			0x1f

/* feature/ status reg */
#define REG_BLOCK_LOCK			0xa0
#define REG_OTP				0xb0
#define REG_STATUS			0xc0/* timing */

/* status */
#define STATUS_OIP_MASK			0x01
#define STATUS_READY			0
#define STATUS_BUSY			BIT(0)

#define STATUS_E_FAIL_MASK		0x04
#define STATUS_E_FAIL			BIT(2)

#define STATUS_P_FAIL_MASK		0x08
#define STATUS_P_FAIL			BIT(3)

#define STATUS_ECC_MASK			0x30
#define STATUS_ECC_1BIT_CORRECTED	BIT(4)
#define STATUS_ECC_ERROR		BIT(5)
#define STATUS_ECC_RESERVED		(BIT(5) | BIT(4))

/*ECC enable defines*/
#define OTP_ECC_MASK			0x10
#define OTP_ECC_OFF			0
#define OTP_ECC_ON			1

#define ECC_DISABLED
#define ECC_IN_NAND
#define ECC_SOFT

/* block lock */
#define BL_ALL_LOCKED      0x38
#define BL_1_2_LOCKED      0x30
#define BL_1_4_LOCKED      0x28
#define BL_1_8_LOCKED      0x20
#define BL_1_16_LOCKED     0x18
#define BL_1_32_LOCKED     0x10
#define BL_1_64_LOCKED     0x08
#define BL_ALL_UNLOCKED    0

struct spinand_info {
	struct spi_device *spi;
	void *priv;
};

struct spinand_state {
	u32	col;
	u32	row;
	int		buf_ptr;
	u8		*buf;
};

struct spinand_cmd {
	u8		cmd;
	u32		n_addr;		/* Number of address */
	u8		addr[3];	/* Reg Offset */
	u32		n_dummy;	/* Dummy use */
	u32		n_tx;		/* Number of tx bytes */
	u8		*tx_buf;	/* Tx buf */
	u32		n_rx;		/* Number of rx bytes */
	u8		*rx_buf;	/* Rx buf */
};

int spinand_mtd(struct mtd_info *mtd);
void spinand_mtd_release(struct mtd_info *mtd);

#endif /* __LINUX_MTD_SPI_NAND_H */
