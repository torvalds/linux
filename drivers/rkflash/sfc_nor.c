// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#define pr_fmt(fmt) "sfc_nor: " fmt

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <asm/string.h>

#include "rkflash_debug.h"
#include "sfc_nor.h"

static struct flash_info spi_flash_tbl[] = {
	/* GD25Q32B */
	{ 0xc84016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 13, 9, 0 },
	/* GD25Q64B */
	{ 0xc84017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 14, 9, 0 },
	/* GD25Q127C and GD25Q128C/E */
	{ 0xc84018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
	/* GD25Q256B/C/D/E */
	{ 0xc84019, 128, 8, 0x13, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1C, 16, 6, 0 },
	/* GD25Q512MC */
	{ 0xc84020, 128, 8, 0x13, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1C, 17, 6, 0 },
	/* GD25LQ64C */
	{ 0xc86017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 14, 9, 0 },
	/* GD25LQ32E */
	{ 0xc86016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 13, 9, 0 },
	/* GD25B512MEYIG */
	{ 0xc8471A, 128, 8, 0x13, 0x12, 0x6C, 0x34, 0x21, 0xDC, 0x1C, 17, 0, 0 },

	/* W25Q32JV */
	{ 0xef4016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 13, 9, 0 },
	/* W25Q64JVSSIQ */
	{ 0xef4017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 9, 0 },
	/* W25Q128FV and W25Q128JV*/
	{ 0xef4018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
	/* W25Q256F/J */
	{ 0xef4019, 128, 8, 0x13, 0x02, 0x6C, 0x32, 0x20, 0xD8, 0x3C, 16, 9, 0 },
	/* W25Q32JW */
	{ 0xef6016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 13, 9, 0 },
	/* W25Q64FWSSIG */
	{ 0xef6017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 9, 0 },
	/* W25Q128JWSQ */
	{ 0xef6018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
	/* W25Q256JWEQ*/
	{ 0xef6019, 128, 8, 0x13, 0x02, 0x6C, 0x32, 0x20, 0xD8, 0x3C, 16, 9, 0 },
	/* W25Q128JVSIM */
	{ 0xef7018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
	/* W25Q256JVEM */
	{ 0xef7019, 128, 8, 0x13, 0x12, 0x6C, 0x34, 0x21, 0xDC, 0x3C, 16, 9, 0 },

	/* MX25L3233FM2I-08G */
	{ 0xc22016, 128, 8, 0x03, 0x02, 0x6B, 0x38, 0x20, 0xD8, 0x0E, 13, 6, 0 },
	/* MX25L6433F */
	{ 0xc22017, 128, 8, 0x03, 0x02, 0x6B, 0x38, 0x20, 0xD8, 0x0E, 14, 6, 0 },
	/* MX25L12835E/F MX25L12833FMI-10G */
	{ 0xc22018, 128, 8, 0x03, 0x02, 0x6B, 0x38, 0x20, 0xD8, 0x0E, 15, 6, 0 },
	/* MX25L25635E/F MX25L25645G MX25L25645GMI-08G */
	{ 0xc22019, 128, 8, 0x13, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1E, 16, 6, 0 },
	/* MX25L51245GMI */
	{ 0xc2201a, 128, 8, 0x13, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1E, 17, 6, 0 },
	/* MX25U51245G */
	{ 0xc2253a, 128, 8, 0x0C, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1E, 17, 6, 0 },
	/* MX25U3232F */
	{ 0xc22536, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0E, 13, 6, 0 },
	/* MX25U6432F */
	{ 0xc22537, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0E, 14, 6, 0 },
	/* MX25U12832F */
	{ 0xc22538, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0E, 15, 6, 0 },
	/* MX25U25645GZ4I-00 */
	{ 0xc22539, 128, 8, 0x13, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1E, 16, 6, 0 },

	/* XM25QH32C */
	{ 0x204016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 13, 9, 0 },
	/* XM25QH64B */
	{ 0x206017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 14, 6, 0 },
	/* XM25QH128B */
	{ 0x206018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 15, 6, 0 },
	/* XM25QH(QU)256B */
	{ 0x206019, 128, 8, 0x13, 0x12, 0x6C, 0x3E, 0x21, 0xDC, 0x1D, 16, 6, 0 },
	/* XM25QH64A */
	{ 0x207017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 0, 0 },

	/* XT25F128A XM25QH128A */
	{ 0x207018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 0, 0 },
	/* XT25F64BSSIGU-5 XT25F64F */
	{ 0x0b4017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 14, 9, 0 },
	/* XT25F128BSSIGU */
	{ 0x0b4018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 15, 9, 0 },
	/* XT25F256BSFIGU */
	{ 0x0b4019, 128, 8, 0x13, 0x12, 0x6C, 0x34, 0x21, 0xDC, 0x1C, 16, 9, 0 },
	/* XT25F32BS */
	{ 0x0b4016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 13, 9, 0 },
	/* XT25F16BS */
	{ 0x0b4015, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 12, 9, 0 },

	/* EN25QH64A */
	{ 0x1c7017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 0, 0 },
	/* EN25QH128A */
	{ 0x1c7018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 0, 0 },
	/* EN25QH32B */
	{ 0x1c7016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 13, 0, 0 },
	/* EN25S32A */
	{ 0x1c3816, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 13, 0, 0 },
	/* EN25S64A */
	{ 0x1c3817, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 0, 0 },
	/* EN25QH256A */
	{ 0x1c7019, 128, 8, 0x13, 0x12, 0x6C, 0x34, 0x21, 0xDC, 0x3C, 16, 0, 0 },

	/* P25Q64H */
	{ 0x856017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 9, 0 },
	/* P25Q128H */
	{ 0x856018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
	/* P25Q16H-SUH-IT */
	{ 0x856015, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 12, 9, 0 },
	/* FM25Q64A */
	{ 0xf83217, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 14, 9, 0 },
	/* FM25M64C */
	{ 0xf84317, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 14, 9, 0 },
	/* P25Q32SL */
	{ 0x856016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 13, 9, 0 },

	/* ZB25VQ64 */
	{ 0x5e4017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 9, 0 },
	/* ZB25VQ128 */
	{ 0x5e4018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
	/* ZB25LQ128 */
	{ 0x5e5018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },

	/* BH25Q128AS */
	{ 0x684018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
	/* BH25Q64BS */
	{ 0x684017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 9, 0 },

	/* P25Q64H */
	{ 0x856017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 9, 0 },
	/* P25Q32SH-SSH-IT */
	{ 0x856016, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 13, 9, 0 },

	/* FM25Q128A */
	{ 0xA14018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
	/* FM25Q64-SOB-T-G */
	{ 0xA14017, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 14, 9, 0 },

	/* FM25Q64A */
	{ 0xf83217, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 14, 9, 0 },
	/* FM25M4AA */
	{ 0xf84218, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0D, 15, 9, 0 },
	/* DS25M4AB-1AIB4 */
	{ 0xe54218, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },

	/* GM25Q128A */
	{ 0x1c4018, 128, 8, 0x03, 0x02, 0x6B, 0x32, 0x20, 0xD8, 0x0C, 15, 9, 0 },
};

static int snor_write_en(void)
{
	int ret;
	struct rk_sfc_op op;

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_WRITE_EN;

	op.sfctrl.d32 = 0;

	ret = sfc_request(&op, 0, NULL, 0);

	return ret;
}

int snor_reset_device(void)
{
	struct rk_sfc_op op;

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_ENABLE_RESER;

	op.sfctrl.d32 = 0;
	sfc_request(&op, 0, NULL, 0);

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_RESET_DEVICE;

	op.sfctrl.d32 = 0;
	sfc_request(&op, 0, NULL, 0);
	/* tRST=30us , delay 1ms here */
	sfc_delay(1000);

	return SFC_OK;
}

static int snor_enter_4byte_mode(void)
{
	int ret;
	struct rk_sfc_op op;

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_ENTER_4BYTE_MODE;

	op.sfctrl.d32 = 0;

	ret = sfc_request(&op, 0, NULL, 0);
	return ret;
}

static int snor_read_status(u32 reg_index, u8 *status)
{
	int ret;
	struct rk_sfc_op op;
	u8 read_stat_cmd[] = {CMD_READ_STATUS,
				CMD_READ_STATUS2, CMD_READ_STATUS3};
	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = read_stat_cmd[reg_index];

	op.sfctrl.d32 = 0;
	ret = sfc_request(&op, 0, status, 1);

	return ret;
}

static int snor_wait_busy(int timeout)
{
	int ret;
	struct rk_sfc_op op;
	int i;
	u32 status;

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_READ_STATUS;

	op.sfctrl.d32 = 0;

	for (i = 0; i < timeout; i++) {
		ret = sfc_request(&op, 0, &status, 1);
		if (ret != SFC_OK)
			return ret;

		if ((status & 0x01) == 0)
			return SFC_OK;

		sfc_delay(1);
	}
	rkflash_print_error("%s  error %x\n", __func__, timeout);

	return SFC_BUSY_TIMEOUT;
}

static int snor_write_status2(u32 reg_index, u8 status)
{
	int ret;
	struct rk_sfc_op op;
	u8 status2[2];

	status2[reg_index] = status;
	if (reg_index == 0)
		ret = snor_read_status(2, &status2[1]);
	else
		ret = snor_read_status(0, &status2[0]);
	if (ret != SFC_OK)
		return ret;

	snor_write_en();

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_WRITE_STATUS;
	op.sfcmd.b.rw = SFC_WRITE;

	op.sfctrl.d32 = 0;

	ret = sfc_request(&op, 0, &status2[0], 2);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(10000);    /* 10ms */

	return ret;
}

static int snor_write_status1(u32 reg_index, u8 status)
{
	int ret;
	struct rk_sfc_op op;
	u8 status2[2];
	u8 read_index;

	status2[reg_index] = status;
	read_index = (reg_index == 0) ? 1 : 0;
	ret = snor_read_status(read_index, &status2[read_index]);
	if (ret != SFC_OK)
		return ret;

	snor_write_en();

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_WRITE_STATUS;
	op.sfcmd.b.rw = SFC_WRITE;

	op.sfctrl.d32 = 0;

	ret = sfc_request(&op, 0, &status2[0], 2);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(10000);    /* 10ms */

	return ret;
}

static int snor_write_status(u32 reg_index, u8 status)
{
	int ret;
	struct rk_sfc_op op;
	u8 write_stat_cmd[] = {CMD_WRITE_STATUS,
			       CMD_WRITE_STATUS2, CMD_WRITE_STATUS3};
	snor_write_en();
	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = write_stat_cmd[reg_index];
	op.sfcmd.b.rw = SFC_WRITE;

	op.sfctrl.d32 = 0;

	ret = sfc_request(&op, 0, &status, 1);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(10000);    /* 10ms */

	return ret;
}

int snor_erase(struct SFNOR_DEV *p_dev,
	       u32 addr,
	       enum NOR_ERASE_TYPE erase_type)
{
	int ret;
	struct rk_sfc_op op;
	int timeout[] = {400, 2000, 40000};   /* ms */

	rkflash_print_dio("%s %x %x\n", __func__, addr, erase_type);

	if (erase_type > ERASE_CHIP)
		return SFC_PARAM_ERR;

	op.sfcmd.d32 = 0;
	if (erase_type == ERASE_BLOCK64K)
		op.sfcmd.b.cmd = p_dev->blk_erase_cmd;
	else if (erase_type == ERASE_SECTOR)
		op.sfcmd.b.cmd = p_dev->sec_erase_cmd;
	else
		op.sfcmd.b.cmd = CMD_CHIP_ERASE;

	op.sfcmd.b.addrbits = (erase_type != ERASE_CHIP) ?
				SFC_ADDR_24BITS : SFC_ADDR_0BITS;
	if (p_dev->addr_mode == ADDR_MODE_4BYTE && erase_type != ERASE_CHIP)
		op.sfcmd.b.addrbits = SFC_ADDR_32BITS;

	op.sfctrl.d32 = 0;

	snor_write_en();

	ret = sfc_request(&op, addr, NULL, 0);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(timeout[erase_type] * 1000);
	return ret;
}

int snor_prog_page(struct SFNOR_DEV *p_dev,
		   u32 addr,
		   void *p_data,
		   u32 size)
{
	int ret;
	struct rk_sfc_op op;

	rkflash_print_dio("%s %x %x\n", __func__, addr, *(u32 *)(p_data));

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = p_dev->prog_cmd;
	op.sfcmd.b.addrbits = SFC_ADDR_24BITS;
	op.sfcmd.b.rw = SFC_WRITE;

	op.sfctrl.d32 = 0;
	op.sfctrl.b.datalines = p_dev->prog_lines;
	op.sfctrl.b.enbledma = 1;
	op.sfctrl.b.addrlines = p_dev->prog_addr_lines;

	if (p_dev->addr_mode == ADDR_MODE_4BYTE)
		op.sfcmd.b.addrbits = SFC_ADDR_32BITS;

	snor_write_en();

	ret = sfc_request(&op, addr, p_data, size);
	if (ret != SFC_OK)
		return ret;

	ret = snor_wait_busy(10000);

	return ret;
}

static int snor_prog(struct SFNOR_DEV *p_dev, u32 addr, void *p_data, u32 size)
{
	int ret = SFC_OK;
	u32 page_size, len;
	u8 *p_buf =  (u8 *)p_data;

	page_size = NOR_PAGE_SIZE;
	while (size) {
		len = page_size < size ? page_size : size;
		ret = snor_prog_page(p_dev, addr, p_buf, len);
		if (ret != SFC_OK)
			return ret;

		size -= len;
		addr += len;
		p_buf += len;
	}

	return ret;
}

static int snor_enable_QE(struct SFNOR_DEV *p_dev)
{
	int ret = SFC_OK;
	int reg_index;
	int bit_offset;
	u8 status;

	reg_index = p_dev->QE_bits >> 3;
	bit_offset = p_dev->QE_bits & 0x7;
	ret = snor_read_status(reg_index, &status);
	if (ret != SFC_OK)
		return ret;

	if (status & (1 << bit_offset))   /* is QE bit set */
		return SFC_OK;

	status |= (1 << bit_offset);

	return p_dev->write_status(reg_index, status);
}

int snor_disable_QE(struct SFNOR_DEV *p_dev)
{
	int ret = SFC_OK;
	int reg_index;
	int bit_offset;
	u8 status;

	reg_index = p_dev->QE_bits >> 3;
	bit_offset = p_dev->QE_bits & 0x7;
	ret = snor_read_status(reg_index, &status);
	if (ret != SFC_OK)
		return ret;

	if (!(status & (1 << bit_offset)))
		return SFC_OK;

	status &= ~(1 << bit_offset);

	return p_dev->write_status(reg_index, status);
}

int snor_read_data(struct SFNOR_DEV *p_dev,
		   u32 addr,
		   void *p_data,
		   u32 size)
{
	int ret;
	struct rk_sfc_op op;

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = p_dev->read_cmd;
	op.sfcmd.b.addrbits = SFC_ADDR_24BITS;

	op.sfctrl.d32 = 0;
	op.sfctrl.b.datalines = p_dev->read_lines;
	if (!(size & 0x3) && size >= 4)
		op.sfctrl.b.enbledma = 1;

	if (p_dev->read_cmd == CMD_FAST_READ_X1 ||
	    p_dev->read_cmd == CMD_PAGE_FASTREAD4B ||
	    p_dev->read_cmd == CMD_FAST_READ_X4 ||
	    p_dev->read_cmd == CMD_FAST_READ_X2 ||
	    p_dev->read_cmd == CMD_FAST_4READ_X4) {
		op.sfcmd.b.dummybits = 8;
	} else if (p_dev->read_cmd == CMD_FAST_READ_A4) {
		op.sfcmd.b.addrbits = SFC_ADDR_32BITS;
		addr = (addr << 8) | 0xFF;	/* Set M[7:0] = 0xFF */
		op.sfcmd.b.dummybits = 4;
		op.sfctrl.b.addrlines = SFC_4BITS_LINE;
	}

	if (p_dev->addr_mode == ADDR_MODE_4BYTE)
		op.sfcmd.b.addrbits = SFC_ADDR_32BITS;

	ret = sfc_request(&op, addr, p_data, size);
	rkflash_print_dio("%s %x %x\n", __func__, addr, *(u32 *)(p_data));

	return ret;
}

int snor_read(struct SFNOR_DEV *p_dev, u32 sec, u32 n_sec, void *p_data)
{
	int ret = SFC_OK;
	u32 addr, size, len;
	u8 *p_buf =  (u8 *)p_data;

	rkflash_print_dio("%s %x %x\n", __func__, sec, n_sec);

	if ((sec + n_sec) > p_dev->capacity)
		return SFC_PARAM_ERR;

	addr = sec << 9;
	size = n_sec << 9;
	while (size) {
		len = size < p_dev->max_iosize ? size : p_dev->max_iosize;
		ret = snor_read_data(p_dev, addr, p_buf, len);
		if (ret != SFC_OK) {
			rkflash_print_error("snor_read_data %x ret= %x\n",
					    addr >> 9, ret);
			goto out;
		}

		size -= len;
		addr += len;
		p_buf += len;
	}
out:
	if (!ret)
		ret = n_sec;

	return ret;
}

int snor_write(struct SFNOR_DEV *p_dev, u32 sec, u32 n_sec, void *p_data)
{
	int ret = SFC_OK;
	u32 len, blk_size, offset;
	u8 *p_buf =  (u8 *)p_data;
	u32 total_sec = n_sec;

	rkflash_print_dio("%s %x %x\n", __func__, sec, n_sec);

	if ((sec + n_sec) > p_dev->capacity)
		return SFC_PARAM_ERR;

	while (n_sec) {
		if (sec < 512 || sec >= p_dev->capacity  - 512)
			blk_size = 8;
		else
			blk_size = p_dev->blk_size;

		offset = (sec & (blk_size - 1));
		if (!offset) {
			ret = snor_erase(p_dev, sec << 9, (blk_size == 8) ?
				ERASE_SECTOR : ERASE_BLOCK64K);
			if (ret != SFC_OK) {
				rkflash_print_error("snor_erase %x ret= %x\n",
						    sec, ret);
				goto out;
			}
		}
		len = (blk_size - offset) < n_sec ?
		      (blk_size - offset) : n_sec;
		ret = snor_prog(p_dev, sec << 9, p_buf, len << 9);
		if (ret != SFC_OK) {
			rkflash_print_error("snor_prog %x ret= %x\n", sec, ret);
			goto out;
		}
		n_sec -= len;
		sec += len;
		p_buf += len << 9;
	}
out:
	if (!ret)
		ret = total_sec;

	return ret;
}

int snor_read_id(u8 *data)
{
	int ret;
	struct rk_sfc_op op;

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_READ_JEDECID;

	op.sfctrl.d32 = 0;

	ret = sfc_request(&op, 0, data, 3);

	return ret;
}

static int snor_read_parameter(u32 addr, u8 *data)
{
	int ret;
	struct rk_sfc_op op;

	op.sfcmd.d32 = 0;
	op.sfcmd.b.cmd = CMD_READ_PARAMETER;
	op.sfcmd.b.addrbits = SFC_ADDR_24BITS;
	op.sfcmd.b.dummybits = 8;

	op.sfctrl.d32 = 0;

	ret = sfc_request(&op, addr, data, 1);

	return ret;
}

u32 snor_get_capacity(struct SFNOR_DEV *p_dev)
{
	return p_dev->capacity;
}

static struct flash_info *snor_get_flash_info(u8 *flash_id)
{
	u32 i;
	u32 id = (flash_id[0] << 16) | (flash_id[1] << 8) | (flash_id[2] << 0);

	for (i = 0; i < ARRAY_SIZE(spi_flash_tbl); i++) {
		if (spi_flash_tbl[i].id == id)
			return &spi_flash_tbl[i];
	}
	return NULL;
}

/* Adjust flash info in ram base on parameter */
static void *snor_flash_info_adjust(struct flash_info *spi_flash_info)
{
	u32 addr;
	u8 para_version;

	if (spi_flash_info->id == 0xc84019) {
		addr = 0x09;
		snor_read_parameter(addr, &para_version);
		if (para_version == 0x06) {
			spi_flash_info->QE_bits = 9;
			spi_flash_info->prog_cmd_4 = 0x34;
		}
	}
	return 0;
}

static int snor_parse_flash_table(struct SFNOR_DEV *p_dev,
				  struct flash_info *g_spi_flash_info)
{
	int i, ret;

	if (g_spi_flash_info) {
		snor_flash_info_adjust(g_spi_flash_info);
		p_dev->manufacturer = (g_spi_flash_info->id >> 16) & 0xFF;
		p_dev->mem_type = (g_spi_flash_info->id >> 8) & 0xFF;
		p_dev->capacity = 1 << g_spi_flash_info->density;
		p_dev->blk_size = g_spi_flash_info->block_size;
		p_dev->page_size = NOR_SECS_PAGE;
		p_dev->read_cmd = g_spi_flash_info->read_cmd;
		p_dev->prog_cmd = g_spi_flash_info->prog_cmd;
		p_dev->sec_erase_cmd = g_spi_flash_info->sector_erase_cmd;
		p_dev->blk_erase_cmd = g_spi_flash_info->block_erase_cmd;
		p_dev->prog_lines = DATA_LINES_X1;
		p_dev->read_lines = DATA_LINES_X1;
		p_dev->QE_bits = g_spi_flash_info->QE_bits;
		p_dev->addr_mode = ADDR_MODE_3BYTE;

		i = g_spi_flash_info->feature & FEA_READ_STATUE_MASK;
		if (i == 0)
			p_dev->write_status = snor_write_status;
		else if (i == 1)
			p_dev->write_status = snor_write_status1;
		else if (i == 2)
			p_dev->write_status = snor_write_status2;

		if (g_spi_flash_info->feature & FEA_4BIT_READ) {
			ret = SFC_OK;
			if (g_spi_flash_info->QE_bits)
				ret = snor_enable_QE(p_dev);
			if (ret == SFC_OK) {
				p_dev->read_lines = DATA_LINES_X4;
				p_dev->read_cmd = g_spi_flash_info->read_cmd_4;
			}
		}
		if (g_spi_flash_info->feature & FEA_4BIT_PROG &&
		    p_dev->read_lines == DATA_LINES_X4) {
			p_dev->prog_lines = DATA_LINES_X4;
			p_dev->prog_cmd = g_spi_flash_info->prog_cmd_4;
			if ((p_dev->manufacturer == MID_MACRONIX) &&
			    (p_dev->prog_cmd == CMD_PAGE_PROG_A4 ||
			     p_dev->prog_cmd == CMD_PAGE_PROG_4PP))
				p_dev->prog_addr_lines = DATA_LINES_X4;
		}

		if (g_spi_flash_info->feature & FEA_4BYTE_ADDR)
			p_dev->addr_mode = ADDR_MODE_4BYTE;

		if ((g_spi_flash_info->feature & FEA_4BYTE_ADDR_MODE))
			snor_enter_4byte_mode();
	}

	return SFC_OK;
}

int snor_init(struct SFNOR_DEV *p_dev)
{
	struct flash_info *g_spi_flash_info;
	u8 id_byte[5];

	if (!p_dev)
		return SFC_PARAM_ERR;

	memset((void *)p_dev, 0, sizeof(struct SFNOR_DEV));
	p_dev->max_iosize = sfc_get_max_iosize();

	snor_read_id(id_byte);
	rkflash_print_error("sfc nor id: %x %x %x\n",
			    id_byte[0], id_byte[1], id_byte[2]);
	if (0xFF == id_byte[0] || 0x00 == id_byte[0])
		return SFC_ERROR;

	g_spi_flash_info = snor_get_flash_info(id_byte);
	if (g_spi_flash_info) {
		snor_parse_flash_table(p_dev, g_spi_flash_info);
	} else {
		pr_err("The device not support yet!\n");

		p_dev->manufacturer = id_byte[0];
		p_dev->mem_type = id_byte[1];
		p_dev->capacity = 1 << (id_byte[2] - 9);
		p_dev->QE_bits = 0;
		p_dev->blk_size = NOR_SECS_BLK;
		p_dev->page_size = NOR_SECS_PAGE;
		p_dev->read_cmd = CMD_READ_DATA;
		p_dev->prog_cmd = CMD_PAGE_PROG;
		p_dev->sec_erase_cmd = CMD_SECTOR_ERASE;
		p_dev->blk_erase_cmd = CMD_BLOCK_ERASE;
		p_dev->prog_lines = DATA_LINES_X1;
		p_dev->prog_addr_lines = DATA_LINES_X1;
		p_dev->read_lines = DATA_LINES_X1;
		p_dev->write_status = snor_write_status;
		snor_reset_device();
	}

	rkflash_print_info("addr_mode: %x\n", p_dev->addr_mode);
	rkflash_print_info("read_lines: %x\n", p_dev->read_lines);
	rkflash_print_info("prog_lines: %x\n", p_dev->prog_lines);
	rkflash_print_info("read_cmd: %x\n", p_dev->read_cmd);
	rkflash_print_info("prog_cmd: %x\n", p_dev->prog_cmd);
	rkflash_print_info("blk_erase_cmd: %x\n", p_dev->blk_erase_cmd);
	rkflash_print_info("sec_erase_cmd: %x\n", p_dev->sec_erase_cmd);
	rkflash_print_info("capacity: %x\n", p_dev->capacity);

	return SFC_OK;
}

int snor_reinit_from_table_packet(struct SFNOR_DEV *p_dev,
				  struct snor_info_packet *packet)
{
	struct flash_info g_spi_flash_info;
	u8 id_byte[5];
	int ret;

	if (!p_dev || packet->id != SNOR_INFO_PACKET_ID)
		return SFC_PARAM_ERR;

	snor_read_id(id_byte);
	if (0xFF == id_byte[0] || 0x00 == id_byte[0])
		return SFC_ERROR;

	g_spi_flash_info.id = id_byte[0] << 16 | id_byte[1] << 8 | id_byte[2];
	g_spi_flash_info.block_size = NOR_SECS_BLK;
	g_spi_flash_info.sector_size = NOR_SECS_PAGE;
	g_spi_flash_info.read_cmd = packet->read_cmd;
	g_spi_flash_info.prog_cmd = packet->prog_cmd;
	g_spi_flash_info.read_cmd_4 = packet->read_cmd_4;
	g_spi_flash_info.prog_cmd_4 = packet->prog_cmd_4;
	if (id_byte[2] >=  0x19)
		g_spi_flash_info.read_cmd_4 = CMD_FAST_4READ_X4;
	g_spi_flash_info.sector_erase_cmd = packet->sector_erase_cmd;
	g_spi_flash_info.block_erase_cmd = packet->block_erase_cmd;
	g_spi_flash_info.feature = packet->feature;
	g_spi_flash_info.density = id_byte[2] - 9;
	g_spi_flash_info.QE_bits = packet->QE_bits;

	ret = snor_parse_flash_table(p_dev, &g_spi_flash_info);

	return ret;
}

