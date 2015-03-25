/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>

#include "rtsx.h"
#include "spi.h"

static inline void spi_set_err_code(struct rtsx_chip *chip, u8 err_code)
{
	struct spi_info *spi = &(chip->spi);

	spi->err_code = err_code;
}

static int spi_init(struct rtsx_chip *chip)
{
	int retval;

	retval = rtsx_write_register(chip, SPI_CONTROL, 0xFF,
				     CS_POLARITY_LOW | DTO_MSB_FIRST | SPI_MASTER | SPI_MODE0 | SPI_AUTO);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = rtsx_write_register(chip, SPI_TCTL, EDO_TIMING_MASK,
				     SAMPLE_DELAY_HALF);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

static int spi_set_init_para(struct rtsx_chip *chip)
{
	struct spi_info *spi = &(chip->spi);
	int retval;

	retval = rtsx_write_register(chip, SPI_CLK_DIVIDER1, 0xFF,
				     (u8)(spi->clk_div >> 8));
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = rtsx_write_register(chip, SPI_CLK_DIVIDER0, 0xFF,
				     (u8)(spi->clk_div));
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	retval = switch_clock(chip, spi->spi_clock);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = select_card(chip, SPI_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CARD_CLK_EN, SPI_CLK_EN,
				     SPI_CLK_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = rtsx_write_register(chip, CARD_OE, SPI_OUTPUT_EN,
				     SPI_OUTPUT_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	wait_timeout(10);

	retval = spi_init(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sf_polling_status(struct rtsx_chip *chip, int msec)
{
	int retval;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, SPI_RDSR);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_POLLING_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, msec);
	if (retval < 0) {
		rtsx_clear_spi_error(chip);
		spi_set_err_code(chip, SPI_BUSY_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sf_enable_write(struct rtsx_chip *chip, u8 ins)
{
	struct spi_info *spi = &(chip->spi);
	int retval;

	if (!spi->write_en)
		return STATUS_SUCCESS;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, ins);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF,
		SPI_COMMAND_BIT_8 | SPI_ADDRESS_BIT_24);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_C_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_clear_spi_error(chip);
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sf_disable_write(struct rtsx_chip *chip, u8 ins)
{
	struct spi_info *spi = &(chip->spi);
	int retval;

	if (!spi->write_en)
		return STATUS_SUCCESS;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, ins);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF,
		SPI_COMMAND_BIT_8 | SPI_ADDRESS_BIT_24);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_C_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_clear_spi_error(chip);
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static void sf_program(struct rtsx_chip *chip, u8 ins, u8 addr_mode, u32 addr,
		u16 len)
{
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, ins);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF,
		SPI_COMMAND_BIT_8 | SPI_ADDRESS_BIT_24);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH0, 0xFF, (u8)len);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH1, 0xFF, (u8)(len >> 8));
	if (addr_mode) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR0, 0xFF, (u8)addr);
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR1, 0xFF,
			(u8)(addr >> 8));
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR2, 0xFF,
			(u8)(addr >> 16));
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
			SPI_TRANSFER0_START | SPI_CADO_MODE0);
	} else {
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
			SPI_TRANSFER0_START | SPI_CDO_MODE0);
	}
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);
}

static int sf_erase(struct rtsx_chip *chip, u8 ins, u8 addr_mode, u32 addr)
{
	int retval;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, ins);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF,
		SPI_COMMAND_BIT_8 | SPI_ADDRESS_BIT_24);
	if (addr_mode) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR0, 0xFF, (u8)addr);
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR1, 0xFF,
			(u8)(addr >> 8));
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR2, 0xFF,
			(u8)(addr >> 16));
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
			SPI_TRANSFER0_START | SPI_CA_MODE0);
	} else {
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
			SPI_TRANSFER0_START | SPI_C_MODE0);
	}
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_clear_spi_error(chip);
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int spi_init_eeprom(struct rtsx_chip *chip)
{
	int retval;
	int clk;

	if (chip->asic_code)
		clk = 30;
	else
		clk = CLK_30;

	retval = rtsx_write_register(chip, SPI_CLK_DIVIDER1, 0xFF, 0x00);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = rtsx_write_register(chip, SPI_CLK_DIVIDER0, 0xFF, 0x27);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	retval = switch_clock(chip, clk);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = select_card(chip, SPI_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CARD_CLK_EN, SPI_CLK_EN,
				     SPI_CLK_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = rtsx_write_register(chip, CARD_OE, SPI_OUTPUT_EN,
				     SPI_OUTPUT_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	wait_timeout(10);

	retval = rtsx_write_register(chip, SPI_CONTROL, 0xFF,
				     CS_POLARITY_HIGH | SPI_EEPROM_AUTO);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = rtsx_write_register(chip, SPI_TCTL, EDO_TIMING_MASK,
				     SAMPLE_DELAY_HALF);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

static int spi_eeprom_program_enable(struct rtsx_chip *chip)
{
	int retval;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF, 0x86);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, 0x13);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_CA_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

int spi_erase_eeprom_chip(struct rtsx_chip *chip)
{
	int retval;

	retval = spi_init_eeprom(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = spi_eeprom_program_enable(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_GPIO_DIR, 0x01, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, 0x12);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF, 0x84);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_CA_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CARD_GPIO_DIR, 0x01, 0x01);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

int spi_erase_eeprom_byte(struct rtsx_chip *chip, u16 addr)
{
	int retval;

	retval = spi_init_eeprom(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = spi_eeprom_program_enable(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_GPIO_DIR, 0x01, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, 0x07);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR0, 0xFF, (u8)addr);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR1, 0xFF, (u8)(addr >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF, 0x46);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_CA_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CARD_GPIO_DIR, 0x01, 0x01);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}


int spi_read_eeprom(struct rtsx_chip *chip, u16 addr, u8 *val)
{
	int retval;
	u8 data;

	retval = spi_init_eeprom(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_GPIO_DIR, 0x01, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, 0x06);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR0, 0xFF, (u8)addr);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR1, 0xFF, (u8)(addr >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF, 0x46);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH0, 0xFF, 1);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_CADI_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	wait_timeout(5);
	retval = rtsx_read_register(chip, SPI_DATA, &data);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	if (val)
		*val = data;

	retval = rtsx_write_register(chip, CARD_GPIO_DIR, 0x01, 0x01);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

int spi_write_eeprom(struct rtsx_chip *chip, u16 addr, u8 val)
{
	int retval;

	retval = spi_init_eeprom(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = spi_eeprom_program_enable(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_GPIO_DIR, 0x01, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, 0x05);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR0, 0xFF, val);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR1, 0xFF, (u8)addr);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR2, 0xFF, (u8)(addr >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF, 0x4E);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_CA_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CARD_GPIO_DIR, 0x01, 0x01);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}


int spi_get_status(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct spi_info *spi = &(chip->spi);

	dev_dbg(rtsx_dev(chip), "spi_get_status: err_code = 0x%x\n",
		spi->err_code);
	rtsx_stor_set_xfer_buf(&(spi->err_code),
			min_t(int, scsi_bufflen(srb), 1), srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - 1);

	return STATUS_SUCCESS;
}

int spi_set_parameter(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct spi_info *spi = &(chip->spi);

	spi_set_err_code(chip, SPI_NO_ERR);

	if (chip->asic_code)
		spi->spi_clock = ((u16)(srb->cmnd[8]) << 8) | srb->cmnd[9];
	else
		spi->spi_clock = srb->cmnd[3];

	spi->clk_div = ((u16)(srb->cmnd[4]) << 8) | srb->cmnd[5];
	spi->write_en = srb->cmnd[6];

	dev_dbg(rtsx_dev(chip), "spi_set_parameter: spi_clock = %d, clk_div = %d, write_en = %d\n",
		spi->spi_clock, spi->clk_div, spi->write_en);

	return STATUS_SUCCESS;
}

int spi_read_flash_id(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	u16 len;
	u8 *buf;

	spi_set_err_code(chip, SPI_NO_ERR);

	len = ((u16)(srb->cmnd[7]) << 8) | srb->cmnd[8];
	if (len > 512) {
		spi_set_err_code(chip, SPI_INVALID_COMMAND);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = spi_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		PINGPONG_BUFFER);

	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, srb->cmnd[3]);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR2, 0xFF, srb->cmnd[4]);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR1, 0xFF, srb->cmnd[5]);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR0, 0xFF, srb->cmnd[6]);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF,
		SPI_COMMAND_BIT_8 | SPI_ADDRESS_BIT_24);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH1, 0xFF, srb->cmnd[7]);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH0, 0xFF, srb->cmnd[8]);

	if (len == 0) {
		if (srb->cmnd[9]) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0,
				      0xFF, SPI_TRANSFER0_START | SPI_CA_MODE0);
		} else {
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0,
				      0xFF, SPI_TRANSFER0_START | SPI_C_MODE0);
		}
	} else {
		if (srb->cmnd[9]) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
				SPI_TRANSFER0_START | SPI_CADI_MODE0);
		} else {
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
				SPI_TRANSFER0_START | SPI_CDI_MODE0);
		}
	}

	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval < 0) {
		rtsx_clear_spi_error(chip);
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (len) {
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf) {
			rtsx_trace(chip);
			return STATUS_ERROR;
		}

		retval = rtsx_read_ppbuf(chip, buf, len);
		if (retval != STATUS_SUCCESS) {
			spi_set_err_code(chip, SPI_READ_ERR);
			kfree(buf);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		rtsx_stor_set_xfer_buf(buf, scsi_bufflen(srb), srb);
		scsi_set_resid(srb, 0);

		kfree(buf);
	}

	return STATUS_SUCCESS;
}

int spi_read_flash(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	unsigned int index = 0, offset = 0;
	u8 ins, slow_read;
	u32 addr;
	u16 len;
	u8 *buf;

	spi_set_err_code(chip, SPI_NO_ERR);

	ins = srb->cmnd[3];
	addr = ((u32)(srb->cmnd[4]) << 16) | ((u32)(srb->cmnd[5])
					<< 8) | srb->cmnd[6];
	len = ((u16)(srb->cmnd[7]) << 8) | srb->cmnd[8];
	slow_read = srb->cmnd[9];

	retval = spi_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	buf = kmalloc(SF_PAGE_LEN, GFP_KERNEL);
	if (buf == NULL) {
		rtsx_trace(chip);
		return STATUS_ERROR;
	}

	while (len) {
		u16 pagelen = SF_PAGE_LEN - (u8)addr;

		if (pagelen > len)
			pagelen = len;

		rtsx_init_cmd(chip);

		trans_dma_enable(DMA_FROM_DEVICE, chip, 256, DMA_256);

		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, ins);

		if (slow_read) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR0, 0xFF,
				(u8)addr);
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR1, 0xFF,
				(u8)(addr >> 8));
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR2, 0xFF,
				(u8)(addr >> 16));
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF,
				SPI_COMMAND_BIT_8 | SPI_ADDRESS_BIT_24);
		} else {
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR1, 0xFF,
				(u8)addr);
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR2, 0xFF,
				(u8)(addr >> 8));
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_ADDR3, 0xFF,
				(u8)(addr >> 16));
			rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF,
				SPI_COMMAND_BIT_8 | SPI_ADDRESS_BIT_32);
		}

		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH1, 0xFF,
			(u8)(pagelen >> 8));
		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH0, 0xFF,
			(u8)pagelen);

		rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
			SPI_TRANSFER0_START | SPI_CADI_MODE0);
		rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0,
			SPI_TRANSFER0_END, SPI_TRANSFER0_END);

		rtsx_send_cmd_no_wait(chip);

		retval = rtsx_transfer_data(chip, 0, buf, pagelen, 0,
					DMA_FROM_DEVICE, 10000);
		if (retval < 0) {
			kfree(buf);
			rtsx_clear_spi_error(chip);
			spi_set_err_code(chip, SPI_HW_ERR);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		rtsx_stor_access_xfer_buf(buf, pagelen, srb, &index, &offset,
					TO_XFER_BUF);

		addr += pagelen;
		len -= pagelen;
	}

	scsi_set_resid(srb, 0);
	kfree(buf);

	return STATUS_SUCCESS;
}

int spi_write_flash(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	u8 ins, program_mode;
	u32 addr;
	u16 len;
	u8 *buf;
	unsigned int index = 0, offset = 0;

	spi_set_err_code(chip, SPI_NO_ERR);

	ins = srb->cmnd[3];
	addr = ((u32)(srb->cmnd[4]) << 16) | ((u32)(srb->cmnd[5])
					<< 8) | srb->cmnd[6];
	len = ((u16)(srb->cmnd[7]) << 8) | srb->cmnd[8];
	program_mode = srb->cmnd[9];

	retval = spi_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (program_mode == BYTE_PROGRAM) {
		buf = kmalloc(4, GFP_KERNEL);
		if (!buf) {
			rtsx_trace(chip);
			return STATUS_ERROR;
		}

		while (len) {
			retval = sf_enable_write(chip, SPI_WREN);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			rtsx_stor_access_xfer_buf(buf, 1, srb, &index, &offset,
						FROM_XFER_BUF);

			rtsx_init_cmd(chip);

			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE,
				0x01, PINGPONG_BUFFER);
			rtsx_add_cmd(chip, WRITE_REG_CMD, PPBUF_BASE2, 0xFF,
				buf[0]);
			sf_program(chip, ins, 1, addr, 1);

			retval = rtsx_send_cmd(chip, 0, 100);
			if (retval < 0) {
				kfree(buf);
				rtsx_clear_spi_error(chip);
				spi_set_err_code(chip, SPI_HW_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			retval = sf_polling_status(chip, 100);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			addr++;
			len--;
		}

		kfree(buf);

	} else if (program_mode == AAI_PROGRAM) {
		int first_byte = 1;

		retval = sf_enable_write(chip, SPI_WREN);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		buf = kmalloc(4, GFP_KERNEL);
		if (!buf) {
			rtsx_trace(chip);
			return STATUS_ERROR;
		}

		while (len) {
			rtsx_stor_access_xfer_buf(buf, 1, srb, &index, &offset,
						FROM_XFER_BUF);

			rtsx_init_cmd(chip);

			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE,
				0x01, PINGPONG_BUFFER);
			rtsx_add_cmd(chip, WRITE_REG_CMD, PPBUF_BASE2, 0xFF,
				buf[0]);
			if (first_byte) {
				sf_program(chip, ins, 1, addr, 1);
				first_byte = 0;
			} else {
				sf_program(chip, ins, 0, 0, 1);
			}

			retval = rtsx_send_cmd(chip, 0, 100);
			if (retval < 0) {
				kfree(buf);
				rtsx_clear_spi_error(chip);
				spi_set_err_code(chip, SPI_HW_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			retval = sf_polling_status(chip, 100);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			len--;
		}

		kfree(buf);

		retval = sf_disable_write(chip, SPI_WRDI);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = sf_polling_status(chip, 100);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else if (program_mode == PAGE_PROGRAM) {
		buf = kmalloc(SF_PAGE_LEN, GFP_KERNEL);
		if (!buf) {
			rtsx_trace(chip);
			return STATUS_NOMEM;
		}

		while (len) {
			u16 pagelen = SF_PAGE_LEN - (u8)addr;

			if (pagelen > len)
				pagelen = len;

			retval = sf_enable_write(chip, SPI_WREN);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			rtsx_init_cmd(chip);

			trans_dma_enable(DMA_TO_DEVICE, chip, 256, DMA_256);
			sf_program(chip, ins, 1, addr, pagelen);

			rtsx_send_cmd_no_wait(chip);

			rtsx_stor_access_xfer_buf(buf, pagelen, srb, &index,
						&offset, FROM_XFER_BUF);

			retval = rtsx_transfer_data(chip, 0, buf, pagelen, 0,
						DMA_TO_DEVICE, 100);
			if (retval < 0) {
				kfree(buf);
				rtsx_clear_spi_error(chip);
				spi_set_err_code(chip, SPI_HW_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			retval = sf_polling_status(chip, 100);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			addr += pagelen;
			len -= pagelen;
		}

		kfree(buf);
	} else {
		spi_set_err_code(chip, SPI_INVALID_COMMAND);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

int spi_erase_flash(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	u8 ins, erase_mode;
	u32 addr;

	spi_set_err_code(chip, SPI_NO_ERR);

	ins = srb->cmnd[3];
	addr = ((u32)(srb->cmnd[4]) << 16) | ((u32)(srb->cmnd[5])
					<< 8) | srb->cmnd[6];
	erase_mode = srb->cmnd[9];

	retval = spi_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (erase_mode == PAGE_ERASE) {
		retval = sf_enable_write(chip, SPI_WREN);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = sf_erase(chip, ins, 1, addr);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else if (erase_mode == CHIP_ERASE) {
		retval = sf_enable_write(chip, SPI_WREN);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = sf_erase(chip, ins, 0, 0);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		spi_set_err_code(chip, SPI_INVALID_COMMAND);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

int spi_write_flash_status(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	u8 ins, status, ewsr;

	ins = srb->cmnd[3];
	status = srb->cmnd[4];
	ewsr = srb->cmnd[5];

	retval = spi_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sf_enable_write(chip, ewsr);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		PINGPONG_BUFFER);

	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_COMMAND, 0xFF, ins);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_CA_NUMBER, 0xFF,
		SPI_COMMAND_BIT_8 | SPI_ADDRESS_BIT_24);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH1, 0xFF, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_LENGTH0, 0xFF, 1);
	rtsx_add_cmd(chip, WRITE_REG_CMD, PPBUF_BASE2, 0xFF, status);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SPI_TRANSFER0, 0xFF,
		SPI_TRANSFER0_START | SPI_CDO_MODE0);
	rtsx_add_cmd(chip, CHECK_REG_CMD, SPI_TRANSFER0, SPI_TRANSFER0_END,
		SPI_TRANSFER0_END);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval != STATUS_SUCCESS) {
		rtsx_clear_spi_error(chip);
		spi_set_err_code(chip, SPI_HW_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}
