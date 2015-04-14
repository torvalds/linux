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
#include <linux/vmalloc.h>

#include "rtsx.h"
#include "rtsx_transport.h"
#include "rtsx_scsi.h"
#include "rtsx_card.h"
#include "xd.h"

static int xd_build_l2p_tbl(struct rtsx_chip *chip, int zone_no);
static int xd_init_page(struct rtsx_chip *chip, u32 phy_blk, u16 logoff,
			u8 start_page, u8 end_page);

static inline void xd_set_err_code(struct rtsx_chip *chip, u8 err_code)
{
	struct xd_info *xd_card = &(chip->xd_card);

	xd_card->err_code = err_code;
}

static inline int xd_check_err_code(struct rtsx_chip *chip, u8 err_code)
{
	struct xd_info *xd_card = &(chip->xd_card);

	return (xd_card->err_code == err_code);
}

static int xd_set_init_para(struct rtsx_chip *chip)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int retval;

	if (chip->asic_code)
		xd_card->xd_clock = 47;
	else
		xd_card->xd_clock = CLK_50;

	retval = switch_clock(chip, xd_card->xd_clock);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int xd_switch_clock(struct rtsx_chip *chip)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int retval;

	retval = select_card(chip, XD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = switch_clock(chip, xd_card->xd_clock);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int xd_read_id(struct rtsx_chip *chip, u8 id_cmd, u8 *id_buf, u8 buf_len)
{
	int retval, i;
	u8 *ptr;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_DAT, 0xFF, id_cmd);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
		XD_TRANSFER_START | XD_READ_ID);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER, XD_TRANSFER_END,
		XD_TRANSFER_END);

	for (i = 0; i < 4; i++)
		rtsx_add_cmd(chip, READ_REG_CMD, (u16)(XD_ADDRESS1 + i), 0, 0);

	retval = rtsx_send_cmd(chip, XD_CARD, 20);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	ptr = rtsx_get_cmd_data(chip) + 1;
	if (id_buf && buf_len) {
		if (buf_len > 4)
			buf_len = 4;
		memcpy(id_buf, ptr, buf_len);
	}

	return STATUS_SUCCESS;
}

static void xd_assign_phy_addr(struct rtsx_chip *chip, u32 addr, u8 mode)
{
	struct xd_info *xd_card = &(chip->xd_card);

	switch (mode) {
	case XD_RW_ADDR:
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_ADDRESS0, 0xFF, 0);
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_ADDRESS1, 0xFF, (u8)addr);
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_ADDRESS2,
			0xFF, (u8)(addr >> 8));
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_ADDRESS3,
			0xFF, (u8)(addr >> 16));
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CFG, 0xFF,
			xd_card->addr_cycle | XD_CALC_ECC | XD_BA_NO_TRANSFORM);
		break;

	case XD_ERASE_ADDR:
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_ADDRESS0, 0xFF, (u8)addr);
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_ADDRESS1,
			0xFF, (u8)(addr >> 8));
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_ADDRESS2,
			0xFF, (u8)(addr >> 16));
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CFG, 0xFF,
			(xd_card->addr_cycle - 1) | XD_CALC_ECC |
			XD_BA_NO_TRANSFORM);
		break;

	default:
		break;
	}
}

static int xd_read_redundant(struct rtsx_chip *chip, u32 page_addr,
			u8 *buf, int buf_len)
{
	int retval, i;

	rtsx_init_cmd(chip);

	xd_assign_phy_addr(chip, page_addr, XD_RW_ADDR);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER,
		0xFF, XD_TRANSFER_START | XD_READ_REDUNDANT);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
		XD_TRANSFER_END, XD_TRANSFER_END);

	for (i = 0; i < 6; i++)
		rtsx_add_cmd(chip, READ_REG_CMD, (u16)(XD_PAGE_STATUS + i),
			0, 0);
	for (i = 0; i < 4; i++)
		rtsx_add_cmd(chip, READ_REG_CMD, (u16)(XD_RESERVED0 + i),
			0, 0);
	rtsx_add_cmd(chip, READ_REG_CMD, XD_PARITY, 0, 0);

	retval = rtsx_send_cmd(chip, XD_CARD, 500);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (buf && buf_len) {
		u8 *ptr = rtsx_get_cmd_data(chip) + 1;

		if (buf_len > 11)
			buf_len = 11;
		memcpy(buf, ptr, buf_len);
	}

	return STATUS_SUCCESS;
}

static int xd_read_data_from_ppb(struct rtsx_chip *chip, int offset,
				u8 *buf, int buf_len)
{
	int retval, i;

	if (!buf || (buf_len < 0)) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	for (i = 0; i < buf_len; i++)
		rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + offset + i,
			0, 0);

	retval = rtsx_send_cmd(chip, 0, 250);
	if (retval < 0) {
		rtsx_clear_xd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	memcpy(buf, rtsx_get_cmd_data(chip), buf_len);

	return STATUS_SUCCESS;
}

static int xd_read_cis(struct rtsx_chip *chip, u32 page_addr, u8 *buf,
		int buf_len)
{
	int retval;
	u8 reg;

	if (!buf || (buf_len < 10)) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	xd_assign_phy_addr(chip, page_addr, XD_RW_ADDR);

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE,
		0x01, PINGPONG_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_CNT, 0xFF, 1);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CHK_DATA_STATUS,
		XD_AUTO_CHK_DATA_STATUS, XD_AUTO_CHK_DATA_STATUS);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
		XD_TRANSFER_START | XD_READ_PAGES);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER, XD_TRANSFER_END,
		XD_TRANSFER_END);

	retval = rtsx_send_cmd(chip, XD_CARD, 250);
	if (retval == -ETIMEDOUT) {
		rtsx_clear_xd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_read_register(chip, XD_PAGE_STATUS, &reg);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	if (reg != XD_GPG) {
		rtsx_clear_xd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_read_register(chip, XD_CTL, &reg);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	if (!(reg & XD_ECC1_ERROR) || !(reg & XD_ECC1_UNCORRECTABLE)) {
		retval = xd_read_data_from_ppb(chip, 0, buf, buf_len);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
		if (reg & XD_ECC1_ERROR) {
			u8 ecc_bit, ecc_byte;

			retval = rtsx_read_register(chip, XD_ECC_BIT1,
						    &ecc_bit);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_read_register(chip, XD_ECC_BYTE1,
						    &ecc_byte);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}

			dev_dbg(rtsx_dev(chip), "ECC_BIT1 = 0x%x, ECC_BYTE1 = 0x%x\n",
				ecc_bit, ecc_byte);
			if (ecc_byte < buf_len) {
				dev_dbg(rtsx_dev(chip), "Before correct: 0x%x\n",
					buf[ecc_byte]);
				buf[ecc_byte] ^= (1 << ecc_bit);
				dev_dbg(rtsx_dev(chip), "After correct: 0x%x\n",
					buf[ecc_byte]);
			}
		}
	} else if (!(reg & XD_ECC2_ERROR) || !(reg & XD_ECC2_UNCORRECTABLE)) {
		rtsx_clear_xd_error(chip);

		retval = xd_read_data_from_ppb(chip, 256, buf, buf_len);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
		if (reg & XD_ECC2_ERROR) {
			u8 ecc_bit, ecc_byte;

			retval = rtsx_read_register(chip, XD_ECC_BIT2,
						    &ecc_bit);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_read_register(chip, XD_ECC_BYTE2,
						    &ecc_byte);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}

			dev_dbg(rtsx_dev(chip), "ECC_BIT2 = 0x%x, ECC_BYTE2 = 0x%x\n",
				ecc_bit, ecc_byte);
			if (ecc_byte < buf_len) {
				dev_dbg(rtsx_dev(chip), "Before correct: 0x%x\n",
					buf[ecc_byte]);
				buf[ecc_byte] ^= (1 << ecc_bit);
				dev_dbg(rtsx_dev(chip), "After correct: 0x%x\n",
					buf[ecc_byte]);
			}
		}
	} else {
		rtsx_clear_xd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static void xd_fill_pull_ctl_disable(struct rtsx_chip *chip)
{
	if (CHECK_PID(chip, 0x5208)) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF,
			XD_D3_PD | XD_D2_PD | XD_D1_PD | XD_D0_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF,
			XD_D7_PD | XD_D6_PD | XD_D5_PD | XD_D4_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF,
			XD_WP_PD | XD_CE_PD | XD_CLE_PD | XD_CD_PU);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF,
			XD_RDY_PD | XD_WE_PD | XD_RE_PD | XD_ALE_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF,
			MS_INS_PU | SD_WP_PD | SD_CD_PU | SD_CMD_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF,
			MS_D5_PD | MS_D4_PD);
	} else if (CHECK_PID(chip, 0x5288)) {
		if (CHECK_BARO_PKG(chip, QFN)) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1,
				0xFF, 0x55);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2,
				0xFF, 0x55);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3,
				0xFF, 0x4B);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4,
				0xFF, 0x69);
		}
	}
}

static void xd_fill_pull_ctl_stage1_barossa(struct rtsx_chip *chip)
{
	if (CHECK_BARO_PKG(chip, QFN)) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0x55);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0x55);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0x4B);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
	}
}

static void xd_fill_pull_ctl_enable(struct rtsx_chip *chip)
{
	if (CHECK_PID(chip, 0x5208)) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF,
			XD_D3_PD | XD_D2_PD | XD_D1_PD | XD_D0_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF,
			XD_D7_PD | XD_D6_PD | XD_D5_PD | XD_D4_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF,
			XD_WP_PD | XD_CE_PU | XD_CLE_PD | XD_CD_PU);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF,
			XD_RDY_PU | XD_WE_PU | XD_RE_PU | XD_ALE_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF,
			MS_INS_PU | SD_WP_PD | SD_CD_PU | SD_CMD_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF,
			MS_D5_PD | MS_D4_PD);
	} else if (CHECK_PID(chip, 0x5288)) {
		if (CHECK_BARO_PKG(chip, QFN)) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1,
				0xFF, 0x55);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2,
				0xFF, 0x55);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3,
				0xFF, 0x53);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4,
				0xFF, 0xA9);
		}
	}
}

static int xd_pull_ctl_disable(struct rtsx_chip *chip)
{
	int retval;

	if (CHECK_PID(chip, 0x5208)) {
		retval = rtsx_write_register(chip, CARD_PULL_CTL1, 0xFF,
					     XD_D3_PD | XD_D2_PD | XD_D1_PD | XD_D0_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL2, 0xFF,
					     XD_D7_PD | XD_D6_PD | XD_D5_PD | XD_D4_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL3, 0xFF,
					     XD_WP_PD | XD_CE_PD | XD_CLE_PD | XD_CD_PU);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL4, 0xFF,
					     XD_RDY_PD | XD_WE_PD | XD_RE_PD | XD_ALE_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL5, 0xFF,
					     MS_INS_PU | SD_WP_PD | SD_CD_PU | SD_CMD_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL6, 0xFF,
					     MS_D5_PD | MS_D4_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	} else if (CHECK_PID(chip, 0x5288)) {
		if (CHECK_BARO_PKG(chip, QFN)) {
			retval = rtsx_write_register(chip, CARD_PULL_CTL1,
						     0xFF, 0x55);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_write_register(chip, CARD_PULL_CTL2,
						     0xFF, 0x55);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_write_register(chip, CARD_PULL_CTL3,
						     0xFF, 0x4B);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_write_register(chip, CARD_PULL_CTL4,
						     0xFF, 0x69);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		}
	}

	return STATUS_SUCCESS;
}

static int reset_xd(struct rtsx_chip *chip)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int retval, i, j;
	u8 *ptr, id_buf[4], redunt[11];

	retval = select_card(chip, XD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CHK_DATA_STATUS, 0xFF,
		XD_PGSTS_NOT_FF);
	if (chip->asic_code) {
		if (!CHECK_PID(chip, 0x5288))
			xd_fill_pull_ctl_disable(chip);
		else
			xd_fill_pull_ctl_stage1_barossa(chip);
	} else {
		rtsx_add_cmd(chip, WRITE_REG_CMD, FPGA_PULL_CTL, 0xFF,
			(FPGA_XD_PULL_CTL_EN1 & FPGA_XD_PULL_CTL_EN3) | 0x20);
	}

	if (!chip->ft2_fast_mode)
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_INIT,
			XD_NO_AUTO_PWR_OFF, 0);

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_OE, XD_OUTPUT_EN, 0);

	retval = rtsx_send_cmd(chip, XD_CARD, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (!chip->ft2_fast_mode) {
		retval = card_power_off(chip, XD_CARD);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		wait_timeout(250);

		rtsx_init_cmd(chip);

		if (chip->asic_code) {
			xd_fill_pull_ctl_enable(chip);
		} else {
			rtsx_add_cmd(chip, WRITE_REG_CMD, FPGA_PULL_CTL, 0xFF,
				(FPGA_XD_PULL_CTL_EN1 & FPGA_XD_PULL_CTL_EN2) |
				0x20);
		}

		retval = rtsx_send_cmd(chip, XD_CARD, 100);
		if (retval < 0) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = card_power_on(chip, XD_CARD);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

#ifdef SUPPORT_OCP
		wait_timeout(50);
		if (chip->ocp_stat & (SD_OC_NOW | SD_OC_EVER)) {
			dev_dbg(rtsx_dev(chip), "Over current, OCPSTAT is 0x%x\n",
				chip->ocp_stat);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
#endif
	}

	rtsx_init_cmd(chip);

	if (chip->ft2_fast_mode) {
		if (chip->asic_code) {
			xd_fill_pull_ctl_enable(chip);
		} else {
			rtsx_add_cmd(chip, WRITE_REG_CMD, FPGA_PULL_CTL, 0xFF,
				(FPGA_XD_PULL_CTL_EN1 & FPGA_XD_PULL_CTL_EN2) |
				0x20);
		}
	}

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_OE, XD_OUTPUT_EN, XD_OUTPUT_EN);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CTL, XD_CE_DISEN, XD_CE_DISEN);

	retval = rtsx_send_cmd(chip, XD_CARD, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (!chip->ft2_fast_mode)
		wait_timeout(200);

	retval = xd_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	/* Read ID to check if the timing setting is right */
	for (i = 0; i < 4; i++) {
		rtsx_init_cmd(chip);

		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_DTCTL, 0xFF,
			XD_TIME_SETUP_STEP * 3 +
			XD_TIME_RW_STEP * (2 + i) + XD_TIME_RWN_STEP * i);
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CATCTL, 0xFF,
			XD_TIME_SETUP_STEP * 3 + XD_TIME_RW_STEP * (4 + i) +
			XD_TIME_RWN_STEP * (3 + i));

		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
			XD_TRANSFER_START | XD_RESET);
		rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
			XD_TRANSFER_END, XD_TRANSFER_END);

		rtsx_add_cmd(chip, READ_REG_CMD, XD_DAT, 0, 0);
		rtsx_add_cmd(chip, READ_REG_CMD, XD_CTL, 0, 0);

		retval = rtsx_send_cmd(chip, XD_CARD, 100);
		if (retval < 0) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		ptr = rtsx_get_cmd_data(chip) + 1;

		dev_dbg(rtsx_dev(chip), "XD_DAT: 0x%x, XD_CTL: 0x%x\n",
			ptr[0], ptr[1]);

		if (((ptr[0] & READY_FLAG) != READY_STATE) ||
			!(ptr[1] & XD_RDY))
			continue;

		retval = xd_read_id(chip, READ_ID, id_buf, 4);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		dev_dbg(rtsx_dev(chip), "READ_ID: 0x%x 0x%x 0x%x 0x%x\n",
			id_buf[0], id_buf[1], id_buf[2], id_buf[3]);

		xd_card->device_code = id_buf[1];

		/* Check if the xD card is supported */
		switch (xd_card->device_code) {
		case XD_4M_X8_512_1:
		case XD_4M_X8_512_2:
			xd_card->block_shift = 4;
			xd_card->page_off = 0x0F;
			xd_card->addr_cycle = 3;
			xd_card->zone_cnt = 1;
			xd_card->capacity = 8000;
			XD_SET_4MB(xd_card);
			break;
		case XD_8M_X8_512:
			xd_card->block_shift = 4;
			xd_card->page_off = 0x0F;
			xd_card->addr_cycle = 3;
			xd_card->zone_cnt = 1;
			xd_card->capacity = 16000;
			break;
		case XD_16M_X8_512:
			XD_PAGE_512(xd_card);
			xd_card->addr_cycle = 3;
			xd_card->zone_cnt = 1;
			xd_card->capacity = 32000;
			break;
		case XD_32M_X8_512:
			XD_PAGE_512(xd_card);
			xd_card->addr_cycle = 3;
			xd_card->zone_cnt = 2;
			xd_card->capacity = 64000;
			break;
		case XD_64M_X8_512:
			XD_PAGE_512(xd_card);
			xd_card->addr_cycle = 4;
			xd_card->zone_cnt = 4;
			xd_card->capacity = 128000;
			break;
		case XD_128M_X8_512:
			XD_PAGE_512(xd_card);
			xd_card->addr_cycle = 4;
			xd_card->zone_cnt = 8;
			xd_card->capacity = 256000;
			break;
		case XD_256M_X8_512:
			XD_PAGE_512(xd_card);
			xd_card->addr_cycle = 4;
			xd_card->zone_cnt = 16;
			xd_card->capacity = 512000;
			break;
		case XD_512M_X8:
			XD_PAGE_512(xd_card);
			xd_card->addr_cycle = 4;
			xd_card->zone_cnt = 32;
			xd_card->capacity = 1024000;
			break;
		case xD_1G_X8_512:
			XD_PAGE_512(xd_card);
			xd_card->addr_cycle = 4;
			xd_card->zone_cnt = 64;
			xd_card->capacity = 2048000;
			break;
		case xD_2G_X8_512:
			XD_PAGE_512(xd_card);
			xd_card->addr_cycle = 4;
			xd_card->zone_cnt = 128;
			xd_card->capacity = 4096000;
			break;
		default:
			continue;
		}

		/* Confirm timing setting */
		for (j = 0; j < 10; j++) {
			retval = xd_read_id(chip, READ_ID, id_buf, 4);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			if (id_buf[1] != xd_card->device_code)
				break;
		}

		if (j == 10)
			break;
	}

	if (i == 4) {
		xd_card->block_shift = 0;
		xd_card->page_off = 0;
		xd_card->addr_cycle = 0;
		xd_card->capacity = 0;

		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = xd_read_id(chip, READ_xD_ID, id_buf, 4);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}
	dev_dbg(rtsx_dev(chip), "READ_xD_ID: 0x%x 0x%x 0x%x 0x%x\n",
		id_buf[0], id_buf[1], id_buf[2], id_buf[3]);
	if (id_buf[2] != XD_ID_CODE) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	/* Search CIS block */
	for (i = 0; i < 24; i++) {
		u32 page_addr;

		if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		page_addr = (u32)i << xd_card->block_shift;

		for (j = 0; j < 3; j++) {
			retval = xd_read_redundant(chip, page_addr, redunt, 11);
			if (retval == STATUS_SUCCESS)
				break;
		}
		if (j == 3)
			continue;

		if (redunt[BLOCK_STATUS] != XD_GBLK)
			continue;

		j = 0;
		if (redunt[PAGE_STATUS] != XD_GPG) {
			for (j = 1; j <= 8; j++) {
				retval = xd_read_redundant(chip, page_addr + j,
							redunt, 11);
				if (retval == STATUS_SUCCESS) {
					if (redunt[PAGE_STATUS] == XD_GPG)
						break;
				}
			}

			if (j == 9)
				break;
		}

		/* Check CIS data */
		if ((redunt[BLOCK_STATUS] == XD_GBLK) &&
			(redunt[PARITY] & XD_BA1_ALL0)) {
			u8 buf[10];

			page_addr += j;

			retval = xd_read_cis(chip, page_addr, buf, 10);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			if ((buf[0] == 0x01) && (buf[1] == 0x03) &&
				(buf[2] == 0xD9)
					&& (buf[3] == 0x01) && (buf[4] == 0xFF)
					&& (buf[5] == 0x18) && (buf[6] == 0x02)
					&& (buf[7] == 0xDF) && (buf[8] == 0x01)
					&& (buf[9] == 0x20)) {
				xd_card->cis_block = (u16)i;
			}
		}

		break;
	}

	dev_dbg(rtsx_dev(chip), "CIS block: 0x%x\n", xd_card->cis_block);
	if (xd_card->cis_block == 0xFFFF) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	chip->capacity[chip->card2lun[XD_CARD]] = xd_card->capacity;

	return STATUS_SUCCESS;
}

static int xd_check_data_blank(u8 *redunt)
{
	int i;

	for (i = 0; i < 6; i++) {
		if (redunt[PAGE_STATUS + i] != 0xFF)
			return 0;
	}

	if ((redunt[PARITY] & (XD_ECC1_ALL1 | XD_ECC2_ALL1))
		!= (XD_ECC1_ALL1 | XD_ECC2_ALL1))
		return 0;


	for (i = 0; i < 4; i++) {
		if (redunt[RESERVED0 + i] != 0xFF)
			return 0;
	}

	return 1;
}

static u16 xd_load_log_block_addr(u8 *redunt)
{
	u16 addr = 0xFFFF;

	if (redunt[PARITY] & XD_BA1_BA2_EQL)
		addr = ((u16)redunt[BLOCK_ADDR1_H] << 8) |
			redunt[BLOCK_ADDR1_L];
	else if (redunt[PARITY] & XD_BA1_VALID)
		addr = ((u16)redunt[BLOCK_ADDR1_H] << 8) |
			redunt[BLOCK_ADDR1_L];
	else if (redunt[PARITY] & XD_BA2_VALID)
		addr = ((u16)redunt[BLOCK_ADDR2_H] << 8) |
			redunt[BLOCK_ADDR2_L];

	return addr;
}

static int xd_init_l2p_tbl(struct rtsx_chip *chip)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int size, i;

	dev_dbg(rtsx_dev(chip), "xd_init_l2p_tbl: zone_cnt = %d\n",
		xd_card->zone_cnt);

	if (xd_card->zone_cnt < 1) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	size = xd_card->zone_cnt * sizeof(struct zone_entry);
	dev_dbg(rtsx_dev(chip), "Buffer size for l2p table is %d\n", size);

	xd_card->zone = vmalloc(size);
	if (!xd_card->zone) {
		rtsx_trace(chip);
		return STATUS_ERROR;
	}

	for (i = 0; i < xd_card->zone_cnt; i++) {
		xd_card->zone[i].build_flag = 0;
		xd_card->zone[i].l2p_table = NULL;
		xd_card->zone[i].free_table = NULL;
		xd_card->zone[i].get_index = 0;
		xd_card->zone[i].set_index = 0;
		xd_card->zone[i].unused_blk_cnt = 0;
	}

	return STATUS_SUCCESS;
}

static inline void free_zone(struct zone_entry *zone)
{
	if (!zone)
		return;

	zone->build_flag = 0;
	zone->set_index = 0;
	zone->get_index = 0;
	zone->unused_blk_cnt = 0;
	if (zone->l2p_table) {
		vfree(zone->l2p_table);
		zone->l2p_table = NULL;
	}
	if (zone->free_table) {
		vfree(zone->free_table);
		zone->free_table = NULL;
	}
}

static void xd_set_unused_block(struct rtsx_chip *chip, u32 phy_blk)
{
	struct xd_info *xd_card = &(chip->xd_card);
	struct zone_entry *zone;
	int zone_no;

	zone_no = (int)phy_blk >> 10;
	if (zone_no >= xd_card->zone_cnt) {
		dev_dbg(rtsx_dev(chip), "Set unused block to invalid zone (zone_no = %d, zone_cnt = %d)\n",
			zone_no, xd_card->zone_cnt);
		return;
	}
	zone = &(xd_card->zone[zone_no]);

	if (zone->free_table == NULL) {
		if (xd_build_l2p_tbl(chip, zone_no) != STATUS_SUCCESS)
			return;
	}

	if ((zone->set_index >= XD_FREE_TABLE_CNT)
			|| (zone->set_index < 0)) {
		free_zone(zone);
		dev_dbg(rtsx_dev(chip), "Set unused block fail, invalid set_index\n");
		return;
	}

	dev_dbg(rtsx_dev(chip), "Set unused block to index %d\n",
		zone->set_index);

	zone->free_table[zone->set_index++] = (u16) (phy_blk & 0x3ff);
	if (zone->set_index >= XD_FREE_TABLE_CNT)
		zone->set_index = 0;
	zone->unused_blk_cnt++;
}

static u32 xd_get_unused_block(struct rtsx_chip *chip, int zone_no)
{
	struct xd_info *xd_card = &(chip->xd_card);
	struct zone_entry *zone;
	u32 phy_blk;

	if (zone_no >= xd_card->zone_cnt) {
		dev_dbg(rtsx_dev(chip), "Get unused block from invalid zone (zone_no = %d, zone_cnt = %d)\n",
			zone_no, xd_card->zone_cnt);
		return BLK_NOT_FOUND;
	}
	zone = &(xd_card->zone[zone_no]);

	if ((zone->unused_blk_cnt == 0) ||
		(zone->set_index == zone->get_index)) {
		free_zone(zone);
		dev_dbg(rtsx_dev(chip), "Get unused block fail, no unused block available\n");
		return BLK_NOT_FOUND;
	}
	if ((zone->get_index >= XD_FREE_TABLE_CNT) || (zone->get_index < 0)) {
		free_zone(zone);
		dev_dbg(rtsx_dev(chip), "Get unused block fail, invalid get_index\n");
		return BLK_NOT_FOUND;
	}

	dev_dbg(rtsx_dev(chip), "Get unused block from index %d\n",
		zone->get_index);

	phy_blk = zone->free_table[zone->get_index];
	zone->free_table[zone->get_index++] = 0xFFFF;
	if (zone->get_index >= XD_FREE_TABLE_CNT)
		zone->get_index = 0;
	zone->unused_blk_cnt--;

	phy_blk += ((u32)(zone_no) << 10);
	return phy_blk;
}

static void xd_set_l2p_tbl(struct rtsx_chip *chip,
			int zone_no, u16 log_off, u16 phy_off)
{
	struct xd_info *xd_card = &(chip->xd_card);
	struct zone_entry *zone;

	zone = &(xd_card->zone[zone_no]);
	zone->l2p_table[log_off] = phy_off;
}

static u32 xd_get_l2p_tbl(struct rtsx_chip *chip, int zone_no, u16 log_off)
{
	struct xd_info *xd_card = &(chip->xd_card);
	struct zone_entry *zone;
	int retval;

	zone = &(xd_card->zone[zone_no]);
	if (zone->l2p_table[log_off] == 0xFFFF) {
		u32 phy_blk = 0;
		int i;

#ifdef XD_DELAY_WRITE
		retval = xd_delay_write(chip);
		if (retval != STATUS_SUCCESS) {
			dev_dbg(rtsx_dev(chip), "In xd_get_l2p_tbl, delay write fail!\n");
			return BLK_NOT_FOUND;
		}
#endif

		if (zone->unused_blk_cnt <= 0) {
			dev_dbg(rtsx_dev(chip), "No unused block!\n");
			return BLK_NOT_FOUND;
		}

		for (i = 0; i < zone->unused_blk_cnt; i++) {
			phy_blk = xd_get_unused_block(chip, zone_no);
			if (phy_blk == BLK_NOT_FOUND) {
				dev_dbg(rtsx_dev(chip), "No unused block available!\n");
				return BLK_NOT_FOUND;
			}

			retval = xd_init_page(chip, phy_blk, log_off,
					0, xd_card->page_off + 1);
			if (retval == STATUS_SUCCESS)
				break;
		}
		if (i >= zone->unused_blk_cnt) {
			dev_dbg(rtsx_dev(chip), "No good unused block available!\n");
			return BLK_NOT_FOUND;
		}

		xd_set_l2p_tbl(chip, zone_no, log_off, (u16)(phy_blk & 0x3FF));
		return phy_blk;
	}

	return (u32)zone->l2p_table[log_off] + ((u32)(zone_no) << 10);
}

int reset_xd_card(struct rtsx_chip *chip)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int retval;

	memset(xd_card, 0, sizeof(struct xd_info));

	xd_card->block_shift = 0;
	xd_card->page_off = 0;
	xd_card->addr_cycle = 0;
	xd_card->capacity = 0;
	xd_card->zone_cnt = 0;
	xd_card->cis_block = 0xFFFF;
	xd_card->delay_write.delay_write_flag = 0;

	retval = enable_card_clock(chip, XD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = reset_xd(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = xd_init_l2p_tbl(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int xd_mark_bad_block(struct rtsx_chip *chip, u32 phy_blk)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int retval;
	u32 page_addr;
	u8 reg = 0;

	dev_dbg(rtsx_dev(chip), "mark block 0x%x as bad block\n", phy_blk);

	if (phy_blk == BLK_NOT_FOUND) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_STATUS, 0xFF, XD_GPG);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_STATUS, 0xFF, XD_LATER_BBLK);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_ADDR1_H, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_ADDR1_L, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_ADDR2_H, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_ADDR2_L, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_RESERVED0, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_RESERVED1, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_RESERVED2, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_RESERVED3, 0xFF, 0xFF);

	page_addr = phy_blk << xd_card->block_shift;

	xd_assign_phy_addr(chip, page_addr, XD_RW_ADDR);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_CNT, 0xFF,
		xd_card->page_off + 1);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
		XD_TRANSFER_START | XD_WRITE_REDUNDANT);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
		XD_TRANSFER_END, XD_TRANSFER_END);

	retval = rtsx_send_cmd(chip, XD_CARD, 500);
	if (retval < 0) {
		rtsx_clear_xd_error(chip);
		rtsx_read_register(chip, XD_DAT, &reg);
		if (reg & PROGRAM_ERROR)
			xd_set_err_code(chip, XD_PRG_ERROR);
		else
			xd_set_err_code(chip, XD_TO_ERROR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int xd_init_page(struct rtsx_chip *chip, u32 phy_blk,
			u16 logoff, u8 start_page, u8 end_page)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int retval;
	u32 page_addr;
	u8 reg = 0;

	dev_dbg(rtsx_dev(chip), "Init block 0x%x\n", phy_blk);

	if (start_page > end_page) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}
	if (phy_blk == BLK_NOT_FOUND) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_STATUS, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_STATUS, 0xFF, 0xFF);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_ADDR1_H,
		0xFF, (u8)(logoff >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_ADDR1_L, 0xFF, (u8)logoff);

	page_addr = (phy_blk << xd_card->block_shift) + start_page;

	xd_assign_phy_addr(chip, page_addr, XD_RW_ADDR);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CFG,
		XD_BA_TRANSFORM, XD_BA_TRANSFORM);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_CNT,
		0xFF, (end_page - start_page));

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER,
		0xFF, XD_TRANSFER_START | XD_WRITE_REDUNDANT);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
		XD_TRANSFER_END, XD_TRANSFER_END);

	retval = rtsx_send_cmd(chip, XD_CARD, 500);
	if (retval < 0) {
		rtsx_clear_xd_error(chip);
		rtsx_read_register(chip, XD_DAT, &reg);
		if (reg & PROGRAM_ERROR) {
			xd_mark_bad_block(chip, phy_blk);
			xd_set_err_code(chip, XD_PRG_ERROR);
		} else {
			xd_set_err_code(chip, XD_TO_ERROR);
		}
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int xd_copy_page(struct rtsx_chip *chip, u32 old_blk, u32 new_blk,
			u8 start_page, u8 end_page)
{
	struct xd_info *xd_card = &(chip->xd_card);
	u32 old_page, new_page;
	u8 i, reg = 0;
	int retval;

	dev_dbg(rtsx_dev(chip), "Copy page from block 0x%x to block 0x%x\n",
		old_blk, new_blk);

	if (start_page > end_page) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if ((old_blk == BLK_NOT_FOUND) || (new_blk == BLK_NOT_FOUND)) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	old_page = (old_blk << xd_card->block_shift) + start_page;
	new_page = (new_blk << xd_card->block_shift) + start_page;

	XD_CLR_BAD_NEWBLK(xd_card);

	retval = rtsx_write_register(chip, CARD_DATA_SOURCE, 0x01,
				     PINGPONG_BUFFER);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	for (i = start_page; i < end_page; i++) {
		if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
			rtsx_clear_xd_error(chip);
			xd_set_err_code(chip, XD_NO_CARD);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		rtsx_init_cmd(chip);

		xd_assign_phy_addr(chip, old_page, XD_RW_ADDR);

		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_CNT, 0xFF, 1);
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CHK_DATA_STATUS,
			XD_AUTO_CHK_DATA_STATUS, 0);
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
			XD_TRANSFER_START | XD_READ_PAGES);
		rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
			XD_TRANSFER_END, XD_TRANSFER_END);

		retval = rtsx_send_cmd(chip, XD_CARD, 500);
		if (retval < 0) {
			rtsx_clear_xd_error(chip);
			reg = 0;
			rtsx_read_register(chip, XD_CTL, &reg);
			if (reg & (XD_ECC1_ERROR | XD_ECC2_ERROR)) {
				wait_timeout(100);

				if (detect_card_cd(chip,
					XD_CARD) != STATUS_SUCCESS) {
					xd_set_err_code(chip, XD_NO_CARD);
					rtsx_trace(chip);
					return STATUS_FAIL;
				}

				if (((reg & (XD_ECC1_ERROR | XD_ECC1_UNCORRECTABLE)) ==
						(XD_ECC1_ERROR | XD_ECC1_UNCORRECTABLE))
					|| ((reg & (XD_ECC2_ERROR | XD_ECC2_UNCORRECTABLE)) ==
						(XD_ECC2_ERROR | XD_ECC2_UNCORRECTABLE))) {
					rtsx_write_register(chip,
							XD_PAGE_STATUS, 0xFF,
							XD_BPG);
					rtsx_write_register(chip,
							XD_BLOCK_STATUS, 0xFF,
							XD_GBLK);
					XD_SET_BAD_OLDBLK(xd_card);
					dev_dbg(rtsx_dev(chip), "old block 0x%x ecc error\n",
						old_blk);
				}
			} else {
				xd_set_err_code(chip, XD_TO_ERROR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}

		if (XD_CHK_BAD_OLDBLK(xd_card))
			rtsx_clear_xd_error(chip);

		rtsx_init_cmd(chip);

		xd_assign_phy_addr(chip, new_page, XD_RW_ADDR);
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_CNT, 0xFF, 1);
		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
			     XD_TRANSFER_START | XD_WRITE_PAGES);
		rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
			XD_TRANSFER_END, XD_TRANSFER_END);

		retval = rtsx_send_cmd(chip, XD_CARD, 300);
		if (retval < 0) {
			rtsx_clear_xd_error(chip);
			reg = 0;
			rtsx_read_register(chip, XD_DAT, &reg);
			if (reg & PROGRAM_ERROR) {
				xd_mark_bad_block(chip, new_blk);
				xd_set_err_code(chip, XD_PRG_ERROR);
				XD_SET_BAD_NEWBLK(xd_card);
			} else {
				xd_set_err_code(chip, XD_TO_ERROR);
			}
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		old_page++;
		new_page++;
	}

	return STATUS_SUCCESS;
}

static int xd_reset_cmd(struct rtsx_chip *chip)
{
	int retval;
	u8 *ptr;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER,
		0xFF, XD_TRANSFER_START | XD_RESET);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
		XD_TRANSFER_END, XD_TRANSFER_END);
	rtsx_add_cmd(chip, READ_REG_CMD, XD_DAT, 0, 0);
	rtsx_add_cmd(chip, READ_REG_CMD, XD_CTL, 0, 0);

	retval = rtsx_send_cmd(chip, XD_CARD, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	ptr = rtsx_get_cmd_data(chip) + 1;
	if (((ptr[0] & READY_FLAG) == READY_STATE) && (ptr[1] & XD_RDY))
		return STATUS_SUCCESS;

	rtsx_trace(chip);
	return STATUS_FAIL;
}

static int xd_erase_block(struct rtsx_chip *chip, u32 phy_blk)
{
	struct xd_info *xd_card = &(chip->xd_card);
	u32 page_addr;
	u8 reg = 0, *ptr;
	int i, retval;

	if (phy_blk == BLK_NOT_FOUND) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	page_addr = phy_blk << xd_card->block_shift;

	for (i = 0; i < 3; i++) {
		rtsx_init_cmd(chip);

		xd_assign_phy_addr(chip, page_addr, XD_ERASE_ADDR);

		rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
			XD_TRANSFER_START | XD_ERASE);
		rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
			XD_TRANSFER_END, XD_TRANSFER_END);
		rtsx_add_cmd(chip, READ_REG_CMD, XD_DAT, 0, 0);

		retval = rtsx_send_cmd(chip, XD_CARD, 250);
		if (retval < 0) {
			rtsx_clear_xd_error(chip);
			rtsx_read_register(chip, XD_DAT, &reg);
			if (reg & PROGRAM_ERROR) {
				xd_mark_bad_block(chip, phy_blk);
				xd_set_err_code(chip, XD_PRG_ERROR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			} else {
				xd_set_err_code(chip, XD_ERASE_FAIL);
			}
			retval = xd_reset_cmd(chip);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			continue;
		}

		ptr = rtsx_get_cmd_data(chip) + 1;
		if (*ptr & PROGRAM_ERROR) {
			xd_mark_bad_block(chip, phy_blk);
			xd_set_err_code(chip, XD_PRG_ERROR);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		return STATUS_SUCCESS;
	}

	xd_mark_bad_block(chip, phy_blk);
	xd_set_err_code(chip, XD_ERASE_FAIL);
	rtsx_trace(chip);
	return STATUS_FAIL;
}


static int xd_build_l2p_tbl(struct rtsx_chip *chip, int zone_no)
{
	struct xd_info *xd_card = &(chip->xd_card);
	struct zone_entry *zone;
	int retval;
	u32 start, end, i;
	u16 max_logoff, cur_fst_page_logoff;
	u16 cur_lst_page_logoff, ent_lst_page_logoff;
	u8 redunt[11];

	dev_dbg(rtsx_dev(chip), "xd_build_l2p_tbl: %d\n", zone_no);

	if (xd_card->zone == NULL) {
		retval = xd_init_l2p_tbl(chip);
		if (retval != STATUS_SUCCESS)
			return retval;
	}

	if (xd_card->zone[zone_no].build_flag) {
		dev_dbg(rtsx_dev(chip), "l2p table of zone %d has been built\n",
			zone_no);
		return STATUS_SUCCESS;
	}

	zone = &(xd_card->zone[zone_no]);

	if (zone->l2p_table == NULL) {
		zone->l2p_table = vmalloc(2000);
		if (zone->l2p_table == NULL) {
			rtsx_trace(chip);
			goto Build_Fail;
		}
	}
	memset((u8 *)(zone->l2p_table), 0xff, 2000);

	if (zone->free_table == NULL) {
		zone->free_table = vmalloc(XD_FREE_TABLE_CNT * 2);
		if (zone->free_table == NULL) {
			rtsx_trace(chip);
			goto Build_Fail;
		}
	}
	memset((u8 *)(zone->free_table), 0xff, XD_FREE_TABLE_CNT * 2);

	if (zone_no == 0) {
		if (xd_card->cis_block == 0xFFFF)
			start = 0;
		else
			start = xd_card->cis_block + 1;
		if (XD_CHK_4MB(xd_card)) {
			end = 0x200;
			max_logoff = 499;
		} else {
			end = 0x400;
			max_logoff = 999;
		}
	} else {
		start = (u32)(zone_no) << 10;
		end = (u32)(zone_no + 1) << 10;
		max_logoff = 999;
	}

	dev_dbg(rtsx_dev(chip), "start block 0x%x, end block 0x%x\n",
		start, end);

	zone->set_index = zone->get_index = 0;
	zone->unused_blk_cnt = 0;

	for (i = start; i < end; i++) {
		u32 page_addr = i << xd_card->block_shift;
		u32 phy_block;

		retval = xd_read_redundant(chip, page_addr, redunt, 11);
		if (retval != STATUS_SUCCESS)
			continue;

		if (redunt[BLOCK_STATUS] != 0xFF) {
			dev_dbg(rtsx_dev(chip), "bad block\n");
			continue;
		}

		if (xd_check_data_blank(redunt)) {
			dev_dbg(rtsx_dev(chip), "blank block\n");
			xd_set_unused_block(chip, i);
			continue;
		}

		cur_fst_page_logoff = xd_load_log_block_addr(redunt);
		if ((cur_fst_page_logoff == 0xFFFF) ||
			(cur_fst_page_logoff > max_logoff)) {
			retval = xd_erase_block(chip, i);
			if (retval == STATUS_SUCCESS)
				xd_set_unused_block(chip, i);
			continue;
		}

		if ((zone_no == 0) && (cur_fst_page_logoff == 0) &&
			(redunt[PAGE_STATUS] != XD_GPG))
			XD_SET_MBR_FAIL(xd_card);

		if (zone->l2p_table[cur_fst_page_logoff] == 0xFFFF) {
			zone->l2p_table[cur_fst_page_logoff] = (u16)(i & 0x3FF);
			continue;
		}

		phy_block = zone->l2p_table[cur_fst_page_logoff] +
			((u32)((zone_no) << 10));

		page_addr = ((i + 1) << xd_card->block_shift) - 1;

		retval = xd_read_redundant(chip, page_addr, redunt, 11);
		if (retval != STATUS_SUCCESS)
			continue;

		cur_lst_page_logoff = xd_load_log_block_addr(redunt);
		if (cur_lst_page_logoff == cur_fst_page_logoff) {
			int m;

			page_addr = ((phy_block + 1) <<
				xd_card->block_shift) - 1;

			for (m = 0; m < 3; m++) {
				retval = xd_read_redundant(chip, page_addr,
							redunt, 11);
				if (retval == STATUS_SUCCESS)
					break;
			}

			if (m == 3) {
				zone->l2p_table[cur_fst_page_logoff] =
					(u16)(i & 0x3FF);
				retval = xd_erase_block(chip, phy_block);
				if (retval == STATUS_SUCCESS)
					xd_set_unused_block(chip, phy_block);
				continue;
			}

			ent_lst_page_logoff = xd_load_log_block_addr(redunt);
			if (ent_lst_page_logoff != cur_fst_page_logoff) {
				zone->l2p_table[cur_fst_page_logoff] =
					(u16)(i & 0x3FF);
				retval = xd_erase_block(chip, phy_block);
				if (retval == STATUS_SUCCESS)
					xd_set_unused_block(chip, phy_block);
				continue;
			} else {
				retval = xd_erase_block(chip, i);
				if (retval == STATUS_SUCCESS)
					xd_set_unused_block(chip, i);
			}
		} else {
			retval = xd_erase_block(chip, i);
			if (retval == STATUS_SUCCESS)
				xd_set_unused_block(chip, i);
		}
	}

	if (XD_CHK_4MB(xd_card))
		end = 500;
	else
		end = 1000;

	i = 0;
	for (start = 0; start < end; start++) {
		if (zone->l2p_table[start] == 0xFFFF)
			i++;
	}

	dev_dbg(rtsx_dev(chip), "Block count %d, invalid L2P entry %d\n",
		end, i);
	dev_dbg(rtsx_dev(chip), "Total unused block: %d\n",
		zone->unused_blk_cnt);

	if ((zone->unused_blk_cnt - i) < 1)
		chip->card_wp |= XD_CARD;

	zone->build_flag = 1;

	return STATUS_SUCCESS;

Build_Fail:
	if (zone->l2p_table) {
		vfree(zone->l2p_table);
		zone->l2p_table = NULL;
	}
	if (zone->free_table) {
		vfree(zone->free_table);
		zone->free_table = NULL;
	}

	return STATUS_FAIL;
}

static int xd_send_cmd(struct rtsx_chip *chip, u8 cmd)
{
	int retval;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_DAT, 0xFF, cmd);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
		XD_TRANSFER_START | XD_SET_CMD);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
		XD_TRANSFER_END, XD_TRANSFER_END);

	retval = rtsx_send_cmd(chip, XD_CARD, 200);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int xd_read_multiple_pages(struct rtsx_chip *chip, u32 phy_blk,
				u32 log_blk, u8 start_page, u8 end_page,
				u8 *buf, unsigned int *index,
				unsigned int *offset)
{
	struct xd_info *xd_card = &(chip->xd_card);
	u32 page_addr, new_blk;
	u16 log_off;
	u8 reg_val, page_cnt;
	int zone_no, retval, i;

	if (start_page > end_page) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	page_cnt = end_page - start_page;
	zone_no = (int)(log_blk / 1000);
	log_off = (u16)(log_blk % 1000);

	if ((phy_blk & 0x3FF) == 0x3FF) {
		for (i = 0; i < 256; i++) {
			page_addr = ((u32)i) << xd_card->block_shift;

			retval = xd_read_redundant(chip, page_addr, NULL, 0);
			if (retval == STATUS_SUCCESS)
				break;

			if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
				xd_set_err_code(chip, XD_NO_CARD);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}
	}

	page_addr = (phy_blk << xd_card->block_shift) + start_page;

	rtsx_init_cmd(chip);

	xd_assign_phy_addr(chip, page_addr, XD_RW_ADDR);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CFG, XD_PPB_TO_SIE, XD_PPB_TO_SIE);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_CNT, 0xFF, page_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CHK_DATA_STATUS,
			XD_AUTO_CHK_DATA_STATUS, XD_AUTO_CHK_DATA_STATUS);

	trans_dma_enable(chip->srb->sc_data_direction, chip,
			page_cnt * 512, DMA_512);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER, 0xFF,
		XD_TRANSFER_START | XD_READ_PAGES);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
		XD_TRANSFER_END | XD_PPB_EMPTY, XD_TRANSFER_END | XD_PPB_EMPTY);

	rtsx_send_cmd_no_wait(chip);

	retval = rtsx_transfer_data_partial(chip, XD_CARD, buf, page_cnt * 512,
					scsi_sg_count(chip->srb),
					index, offset, DMA_FROM_DEVICE,
					chip->xd_timeout);
	if (retval < 0) {
		rtsx_clear_xd_error(chip);

		if (retval == -ETIMEDOUT) {
			xd_set_err_code(chip, XD_TO_ERROR);
			rtsx_trace(chip);
			return STATUS_FAIL;
		} else {
			rtsx_trace(chip);
			goto Fail;
		}
	}

	return STATUS_SUCCESS;

Fail:
	retval = rtsx_read_register(chip, XD_PAGE_STATUS, &reg_val);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	if (reg_val !=  XD_GPG)
		xd_set_err_code(chip, XD_PRG_ERROR);

	retval = rtsx_read_register(chip, XD_CTL, &reg_val);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	if (((reg_val & (XD_ECC1_ERROR | XD_ECC1_UNCORRECTABLE))
				== (XD_ECC1_ERROR | XD_ECC1_UNCORRECTABLE))
		|| ((reg_val & (XD_ECC2_ERROR | XD_ECC2_UNCORRECTABLE))
			== (XD_ECC2_ERROR | XD_ECC2_UNCORRECTABLE))) {
		wait_timeout(100);

		if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
			xd_set_err_code(chip, XD_NO_CARD);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		xd_set_err_code(chip, XD_ECC_ERROR);

		new_blk = xd_get_unused_block(chip, zone_no);
		if (new_blk == NO_NEW_BLK) {
			XD_CLR_BAD_OLDBLK(xd_card);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = xd_copy_page(chip, phy_blk, new_blk, 0,
				xd_card->page_off + 1);
		if (retval != STATUS_SUCCESS) {
			if (!XD_CHK_BAD_NEWBLK(xd_card)) {
				retval = xd_erase_block(chip, new_blk);
				if (retval == STATUS_SUCCESS)
					xd_set_unused_block(chip, new_blk);
			} else {
				XD_CLR_BAD_NEWBLK(xd_card);
			}
			XD_CLR_BAD_OLDBLK(xd_card);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
		xd_set_l2p_tbl(chip, zone_no, log_off, (u16)(new_blk & 0x3FF));
		xd_erase_block(chip, phy_blk);
		xd_mark_bad_block(chip, phy_blk);
		XD_CLR_BAD_OLDBLK(xd_card);
	}

	rtsx_trace(chip);
	return STATUS_FAIL;
}

static int xd_finish_write(struct rtsx_chip *chip,
		u32 old_blk, u32 new_blk, u32 log_blk, u8 page_off)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int retval, zone_no;
	u16 log_off;

	dev_dbg(rtsx_dev(chip), "xd_finish_write, old_blk = 0x%x, new_blk = 0x%x, log_blk = 0x%x\n",
		old_blk, new_blk, log_blk);

	if (page_off > xd_card->page_off) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	zone_no = (int)(log_blk / 1000);
	log_off = (u16)(log_blk % 1000);

	if (old_blk == BLK_NOT_FOUND) {
		retval = xd_init_page(chip, new_blk, log_off,
				page_off, xd_card->page_off + 1);
		if (retval != STATUS_SUCCESS) {
			retval = xd_erase_block(chip, new_blk);
			if (retval == STATUS_SUCCESS)
				xd_set_unused_block(chip, new_blk);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		retval = xd_copy_page(chip, old_blk, new_blk,
				page_off, xd_card->page_off + 1);
		if (retval != STATUS_SUCCESS) {
			if (!XD_CHK_BAD_NEWBLK(xd_card)) {
				retval = xd_erase_block(chip, new_blk);
				if (retval == STATUS_SUCCESS)
					xd_set_unused_block(chip, new_blk);
			}
			XD_CLR_BAD_NEWBLK(xd_card);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = xd_erase_block(chip, old_blk);
		if (retval == STATUS_SUCCESS) {
			if (XD_CHK_BAD_OLDBLK(xd_card)) {
				xd_mark_bad_block(chip, old_blk);
				XD_CLR_BAD_OLDBLK(xd_card);
			} else {
				xd_set_unused_block(chip, old_blk);
			}
		} else {
			xd_set_err_code(chip, XD_NO_ERROR);
			XD_CLR_BAD_OLDBLK(xd_card);
		}
	}

	xd_set_l2p_tbl(chip, zone_no, log_off, (u16)(new_blk & 0x3FF));

	return STATUS_SUCCESS;
}

static int xd_prepare_write(struct rtsx_chip *chip,
		u32 old_blk, u32 new_blk, u32 log_blk, u8 page_off)
{
	int retval;

	dev_dbg(rtsx_dev(chip), "%s, old_blk = 0x%x, new_blk = 0x%x, log_blk = 0x%x, page_off = %d\n",
		__func__, old_blk, new_blk, log_blk, (int)page_off);

	if (page_off) {
		retval = xd_copy_page(chip, old_blk, new_blk, 0, page_off);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	return STATUS_SUCCESS;
}


static int xd_write_multiple_pages(struct rtsx_chip *chip, u32 old_blk,
				u32 new_blk, u32 log_blk, u8 start_page,
				u8 end_page, u8 *buf, unsigned int *index,
				unsigned int *offset)
{
	struct xd_info *xd_card = &(chip->xd_card);
	u32 page_addr;
	int zone_no, retval;
	u16 log_off;
	u8 page_cnt, reg_val;

	dev_dbg(rtsx_dev(chip), "%s, old_blk = 0x%x, new_blk = 0x%x, log_blk = 0x%x\n",
		__func__, old_blk, new_blk, log_blk);

	if (start_page > end_page) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	page_cnt = end_page - start_page;
	zone_no = (int)(log_blk / 1000);
	log_off = (u16)(log_blk % 1000);

	page_addr = (new_blk << xd_card->block_shift) + start_page;

	retval = xd_send_cmd(chip, READ1_1);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_ADDR1_H,
		0xFF, (u8)(log_off >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_ADDR1_L, 0xFF, (u8)log_off);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_BLOCK_STATUS, 0xFF, XD_GBLK);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_STATUS, 0xFF, XD_GPG);

	xd_assign_phy_addr(chip, page_addr, XD_RW_ADDR);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_CFG, XD_BA_TRANSFORM,
		XD_BA_TRANSFORM);
	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_PAGE_CNT, 0xFF, page_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);

	trans_dma_enable(chip->srb->sc_data_direction, chip,
			page_cnt * 512, DMA_512);

	rtsx_add_cmd(chip, WRITE_REG_CMD, XD_TRANSFER,
		0xFF, XD_TRANSFER_START | XD_WRITE_PAGES);
	rtsx_add_cmd(chip, CHECK_REG_CMD, XD_TRANSFER,
		XD_TRANSFER_END, XD_TRANSFER_END);

	rtsx_send_cmd_no_wait(chip);

	retval = rtsx_transfer_data_partial(chip, XD_CARD, buf, page_cnt * 512,
					scsi_sg_count(chip->srb),
			index, offset, DMA_TO_DEVICE, chip->xd_timeout);
	if (retval < 0) {
		rtsx_clear_xd_error(chip);

		if (retval == -ETIMEDOUT) {
			xd_set_err_code(chip, XD_TO_ERROR);
			rtsx_trace(chip);
			return STATUS_FAIL;
		} else {
			rtsx_trace(chip);
			goto Fail;
		}
	}

	if (end_page == (xd_card->page_off + 1)) {
		xd_card->delay_write.delay_write_flag = 0;

		if (old_blk != BLK_NOT_FOUND) {
			retval = xd_erase_block(chip, old_blk);
			if (retval == STATUS_SUCCESS) {
				if (XD_CHK_BAD_OLDBLK(xd_card)) {
					xd_mark_bad_block(chip, old_blk);
					XD_CLR_BAD_OLDBLK(xd_card);
				} else {
					xd_set_unused_block(chip, old_blk);
				}
			} else {
				xd_set_err_code(chip, XD_NO_ERROR);
				XD_CLR_BAD_OLDBLK(xd_card);
			}
		}
		xd_set_l2p_tbl(chip, zone_no, log_off, (u16)(new_blk & 0x3FF));
	}

	return STATUS_SUCCESS;

Fail:
	retval = rtsx_read_register(chip, XD_DAT, &reg_val);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	if (reg_val & PROGRAM_ERROR) {
		xd_set_err_code(chip, XD_PRG_ERROR);
		xd_mark_bad_block(chip, new_blk);
	}

	rtsx_trace(chip);
	return STATUS_FAIL;
}

#ifdef XD_DELAY_WRITE
int xd_delay_write(struct rtsx_chip *chip)
{
	struct xd_info *xd_card = &(chip->xd_card);
	struct xd_delay_write_tag *delay_write = &(xd_card->delay_write);
	int retval;

	if (delay_write->delay_write_flag) {
		dev_dbg(rtsx_dev(chip), "xd_delay_write\n");
		retval = xd_switch_clock(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		delay_write->delay_write_flag = 0;
		retval = xd_finish_write(chip,
				delay_write->old_phyblock,
					delay_write->new_phyblock,
				delay_write->logblock, delay_write->pageoff);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	return STATUS_SUCCESS;
}
#endif

int xd_rw(struct scsi_cmnd *srb, struct rtsx_chip *chip,
	u32 start_sector, u16 sector_cnt)
{
	struct xd_info *xd_card = &(chip->xd_card);
	unsigned int lun = SCSI_LUN(srb);
#ifdef XD_DELAY_WRITE
	struct xd_delay_write_tag *delay_write = &(xd_card->delay_write);
#endif
	int retval, zone_no;
	unsigned int index = 0, offset = 0;
	u32 log_blk, old_blk = 0, new_blk = 0;
	u16 log_off, total_sec_cnt = sector_cnt;
	u8 start_page, end_page = 0, page_cnt;
	u8 *ptr;

	xd_set_err_code(chip, XD_NO_ERROR);

	xd_card->cleanup_counter = 0;

	dev_dbg(rtsx_dev(chip), "xd_rw: scsi_sg_count = %d\n",
		scsi_sg_count(srb));

	ptr = (u8 *)scsi_sglist(srb);

	retval = xd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}


	if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
		chip->card_fail |= XD_CARD;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	log_blk = start_sector >> xd_card->block_shift;
	start_page = (u8)start_sector & xd_card->page_off;
	zone_no = (int)(log_blk / 1000);
	log_off = (u16)(log_blk % 1000);

	if (xd_card->zone[zone_no].build_flag == 0) {
		retval = xd_build_l2p_tbl(chip, zone_no);
		if (retval != STATUS_SUCCESS) {
			chip->card_fail |= XD_CARD;
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	if (srb->sc_data_direction == DMA_TO_DEVICE) {
#ifdef XD_DELAY_WRITE
		if (delay_write->delay_write_flag &&
				(delay_write->logblock == log_blk) &&
				(start_page > delay_write->pageoff)) {
			delay_write->delay_write_flag = 0;
			if (delay_write->old_phyblock != BLK_NOT_FOUND) {
				retval = xd_copy_page(chip,
					delay_write->old_phyblock,
					delay_write->new_phyblock,
					delay_write->pageoff, start_page);
				if (retval != STATUS_SUCCESS) {
					set_sense_type(chip, lun,
						SENSE_TYPE_MEDIA_WRITE_ERR);
					rtsx_trace(chip);
					return STATUS_FAIL;
				}
			}
			old_blk = delay_write->old_phyblock;
			new_blk = delay_write->new_phyblock;
		} else if (delay_write->delay_write_flag &&
				(delay_write->logblock == log_blk) &&
				(start_page == delay_write->pageoff)) {
			delay_write->delay_write_flag = 0;
			old_blk = delay_write->old_phyblock;
			new_blk = delay_write->new_phyblock;
		} else {
			retval = xd_delay_write(chip);
			if (retval != STATUS_SUCCESS) {
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_WRITE_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
#endif
			old_blk = xd_get_l2p_tbl(chip, zone_no, log_off);
			new_blk  = xd_get_unused_block(chip, zone_no);
			if ((old_blk == BLK_NOT_FOUND) ||
				(new_blk == BLK_NOT_FOUND)) {
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_WRITE_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			retval = xd_prepare_write(chip, old_blk, new_blk,
						log_blk, start_page);
			if (retval != STATUS_SUCCESS) {
				if (detect_card_cd(chip, XD_CARD) !=
					STATUS_SUCCESS) {
					set_sense_type(chip, lun,
						SENSE_TYPE_MEDIA_NOT_PRESENT);
					rtsx_trace(chip);
					return STATUS_FAIL;
				}
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_WRITE_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
#ifdef XD_DELAY_WRITE
		}
#endif
	} else {
#ifdef XD_DELAY_WRITE
		retval = xd_delay_write(chip);
		if (retval != STATUS_SUCCESS) {
			if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_NOT_PRESENT);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
#endif

		old_blk = xd_get_l2p_tbl(chip, zone_no, log_off);
		if (old_blk == BLK_NOT_FOUND) {
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	dev_dbg(rtsx_dev(chip), "old_blk = 0x%x\n", old_blk);

	while (total_sec_cnt) {
		if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
			chip->card_fail |= XD_CARD;
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		if ((start_page + total_sec_cnt) > (xd_card->page_off + 1))
			end_page = xd_card->page_off + 1;
		else
			end_page = start_page + (u8)total_sec_cnt;

		page_cnt = end_page - start_page;
		if (srb->sc_data_direction == DMA_FROM_DEVICE) {
			retval = xd_read_multiple_pages(chip, old_blk, log_blk,
					start_page, end_page, ptr,
							&index, &offset);
			if (retval != STATUS_SUCCESS) {
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		} else {
			retval = xd_write_multiple_pages(chip, old_blk,
							new_blk, log_blk,
					start_page, end_page, ptr,
							&index, &offset);
			if (retval != STATUS_SUCCESS) {
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_WRITE_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}

		total_sec_cnt -= page_cnt;
		if (scsi_sg_count(srb) == 0)
			ptr += page_cnt * 512;

		if (total_sec_cnt == 0)
			break;

		log_blk++;
		zone_no = (int)(log_blk / 1000);
		log_off = (u16)(log_blk % 1000);

		if (xd_card->zone[zone_no].build_flag == 0) {
			retval = xd_build_l2p_tbl(chip, zone_no);
			if (retval != STATUS_SUCCESS) {
				chip->card_fail |= XD_CARD;
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_NOT_PRESENT);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}

		old_blk = xd_get_l2p_tbl(chip, zone_no, log_off);
		if (old_blk == BLK_NOT_FOUND) {
			if (srb->sc_data_direction == DMA_FROM_DEVICE)
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			else
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_WRITE_ERR);

			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		if (srb->sc_data_direction == DMA_TO_DEVICE) {
			new_blk = xd_get_unused_block(chip, zone_no);
			if (new_blk == BLK_NOT_FOUND) {
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_WRITE_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}

		start_page = 0;
	}

	if ((srb->sc_data_direction == DMA_TO_DEVICE) &&
			(end_page != (xd_card->page_off + 1))) {
#ifdef XD_DELAY_WRITE
		delay_write->delay_write_flag = 1;
		delay_write->old_phyblock = old_blk;
		delay_write->new_phyblock = new_blk;
		delay_write->logblock = log_blk;
		delay_write->pageoff = end_page;
#else
		if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
			chip->card_fail |= XD_CARD;
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = xd_finish_write(chip, old_blk, new_blk,
					log_blk, end_page);
		if (retval != STATUS_SUCCESS) {
			if (detect_card_cd(chip, XD_CARD) != STATUS_SUCCESS) {
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_NOT_PRESENT);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
#endif
	}

	scsi_set_resid(srb, 0);

	return STATUS_SUCCESS;
}

void xd_free_l2p_tbl(struct rtsx_chip *chip)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int i = 0;

	if (xd_card->zone != NULL) {
		for (i = 0; i < xd_card->zone_cnt; i++) {
			if (xd_card->zone[i].l2p_table != NULL) {
				vfree(xd_card->zone[i].l2p_table);
				xd_card->zone[i].l2p_table = NULL;
			}
			if (xd_card->zone[i].free_table != NULL) {
				vfree(xd_card->zone[i].free_table);
				xd_card->zone[i].free_table = NULL;
			}
		}
		vfree(xd_card->zone);
		xd_card->zone = NULL;
	}
}

void xd_cleanup_work(struct rtsx_chip *chip)
{
#ifdef XD_DELAY_WRITE
	struct xd_info *xd_card = &(chip->xd_card);

	if (xd_card->delay_write.delay_write_flag) {
		dev_dbg(rtsx_dev(chip), "xD: delay write\n");
		xd_delay_write(chip);
		xd_card->cleanup_counter = 0;
	}
#endif
}

int xd_power_off_card3v3(struct rtsx_chip *chip)
{
	int retval;

	retval = disable_card_clock(chip, XD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CARD_OE, XD_OUTPUT_EN, 0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	if (!chip->ft2_fast_mode) {
		retval = card_power_off(chip, XD_CARD);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		wait_timeout(50);
	}

	if (chip->asic_code) {
		retval = xd_pull_ctl_disable(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		retval = rtsx_write_register(chip, FPGA_PULL_CTL, 0xFF, 0xDF);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	}

	return STATUS_SUCCESS;
}

int release_xd_card(struct rtsx_chip *chip)
{
	struct xd_info *xd_card = &(chip->xd_card);
	int retval;

	chip->card_ready &= ~XD_CARD;
	chip->card_fail &= ~XD_CARD;
	chip->card_wp &= ~XD_CARD;

	xd_card->delay_write.delay_write_flag = 0;

	xd_free_l2p_tbl(chip);

	retval = xd_power_off_card3v3(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}
