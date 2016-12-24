/* Realtek USB SD/MMC Card Interface driver
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
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
 *   Roger Tseng <rogerable@realtek.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/card.h>
#include <linux/scatterlist.h>
#include <linux/pm_runtime.h>

#include <linux/mfd/rtsx_usb.h>
#include <asm/unaligned.h>

#if defined(CONFIG_LEDS_CLASS) || (defined(CONFIG_LEDS_CLASS_MODULE) && \
		defined(CONFIG_MMC_REALTEK_USB_MODULE))
#include <linux/leds.h>
#include <linux/workqueue.h>
#define RTSX_USB_USE_LEDS_CLASS
#endif

struct rtsx_usb_sdmmc {
	struct platform_device	*pdev;
	struct rtsx_ucr	*ucr;
	struct mmc_host		*mmc;
	struct mmc_request	*mrq;

	struct mutex		host_mutex;

	u8			ssc_depth;
	unsigned int		clock;
	bool			vpclk;
	bool			double_clk;
	bool			host_removal;
	bool			card_exist;
	bool			initial_mode;
	bool			ddr_mode;

	unsigned char		power_mode;

#ifdef RTSX_USB_USE_LEDS_CLASS
	struct led_classdev	led;
	char			led_name[32];
	struct work_struct	led_work;
#endif
};

static inline struct device *sdmmc_dev(struct rtsx_usb_sdmmc *host)
{
	return &(host->pdev->dev);
}

static inline void sd_clear_error(struct rtsx_usb_sdmmc *host)
{
	struct rtsx_ucr *ucr = host->ucr;
	rtsx_usb_ep0_write_register(ucr, CARD_STOP,
				  SD_STOP | SD_CLR_ERR,
				  SD_STOP | SD_CLR_ERR);

	rtsx_usb_clear_dma_err(ucr);
	rtsx_usb_clear_fsm_err(ucr);
}

#ifdef DEBUG
static void sd_print_debug_regs(struct rtsx_usb_sdmmc *host)
{
	struct rtsx_ucr *ucr = host->ucr;
	u8 val = 0;

	rtsx_usb_ep0_read_register(ucr, SD_STAT1, &val);
	dev_dbg(sdmmc_dev(host), "SD_STAT1: 0x%x\n", val);
	rtsx_usb_ep0_read_register(ucr, SD_STAT2, &val);
	dev_dbg(sdmmc_dev(host), "SD_STAT2: 0x%x\n", val);
	rtsx_usb_ep0_read_register(ucr, SD_BUS_STAT, &val);
	dev_dbg(sdmmc_dev(host), "SD_BUS_STAT: 0x%x\n", val);
}
#else
#define sd_print_debug_regs(host)
#endif /* DEBUG */

static int sd_read_data(struct rtsx_usb_sdmmc *host, struct mmc_command *cmd,
	       u16 byte_cnt, u8 *buf, int buf_len, int timeout)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;
	u8 trans_mode;

	if (!buf)
		buf_len = 0;

	rtsx_usb_init_cmd(ucr);
	if (cmd != NULL) {
		dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD%d\n", __func__
				, cmd->opcode);
		if (cmd->opcode == MMC_SEND_TUNING_BLOCK)
			trans_mode = SD_TM_AUTO_TUNING;
		else
			trans_mode = SD_TM_NORMAL_READ;

		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD0, 0xFF, (u8)(cmd->opcode) | 0x40);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD1, 0xFF, (u8)(cmd->arg >> 24));
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD2, 0xFF, (u8)(cmd->arg >> 16));
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD3, 0xFF, (u8)(cmd->arg >> 8));
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD4, 0xFF, (u8)cmd->arg);
	} else {
		trans_mode = SD_TM_AUTO_READ_3;
	}

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, (u8)byte_cnt);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BYTE_CNT_H,
			0xFF, (u8)(byte_cnt >> 8));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, 1);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF, 0);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CFG2, 0xFF,
			SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_6);
	if (trans_mode != SD_TM_AUTO_TUNING)
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				CARD_DATA_SOURCE, 0x01, PINGPONG_BUFFER);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_TRANSFER,
			0xFF, trans_mode | SD_TRANSFER_START);
	rtsx_usb_add_cmd(ucr, CHECK_REG_CMD, SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

	if (cmd != NULL) {
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_CMD1, 0, 0);
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_CMD2, 0, 0);
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_CMD3, 0, 0);
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_CMD4, 0, 0);
	}

	err = rtsx_usb_send_cmd(ucr, MODE_CR, timeout);
	if (err) {
		dev_dbg(sdmmc_dev(host),
			"rtsx_usb_send_cmd failed (err = %d)\n", err);
		return err;
	}

	err = rtsx_usb_get_rsp(ucr, !cmd ? 1 : 5, timeout);
	if (err || (ucr->rsp_buf[0] & SD_TRANSFER_ERR)) {
		sd_print_debug_regs(host);

		if (!err) {
			dev_dbg(sdmmc_dev(host),
				"Transfer failed (SD_TRANSFER = %02x)\n",
				ucr->rsp_buf[0]);
			err = -EIO;
		} else {
			dev_dbg(sdmmc_dev(host),
				"rtsx_usb_get_rsp failed (err = %d)\n", err);
		}

		return err;
	}

	if (cmd != NULL) {
		cmd->resp[0] = get_unaligned_be32(ucr->rsp_buf + 1);
		dev_dbg(sdmmc_dev(host), "cmd->resp[0] = 0x%08x\n",
				cmd->resp[0]);
	}

	if (buf && buf_len) {
		/* 2-byte aligned part */
		err = rtsx_usb_read_ppbuf(ucr, buf, byte_cnt - (byte_cnt % 2));
		if (err) {
			dev_dbg(sdmmc_dev(host),
				"rtsx_usb_read_ppbuf failed (err = %d)\n", err);
			return err;
		}

		/* unaligned byte */
		if (byte_cnt % 2)
			return rtsx_usb_read_register(ucr,
					PPBUF_BASE2 + byte_cnt,
					buf + byte_cnt - 1);
	}

	return 0;
}

static int sd_write_data(struct rtsx_usb_sdmmc *host, struct mmc_command *cmd,
		u16 byte_cnt, u8 *buf, int buf_len, int timeout)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;
	u8 trans_mode;

	if (!buf)
		buf_len = 0;

	if (buf && buf_len) {
		err = rtsx_usb_write_ppbuf(ucr, buf, buf_len);
		if (err) {
			dev_dbg(sdmmc_dev(host),
				"rtsx_usb_write_ppbuf failed (err = %d)\n",
				err);
			return err;
		}
	}

	trans_mode = (cmd != NULL) ? SD_TM_AUTO_WRITE_2 : SD_TM_AUTO_WRITE_3;
	rtsx_usb_init_cmd(ucr);

	if (cmd != NULL) {
		dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD%d\n", __func__,
				cmd->opcode);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD0, 0xFF, (u8)(cmd->opcode) | 0x40);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD1, 0xFF, (u8)(cmd->arg >> 24));
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD2, 0xFF, (u8)(cmd->arg >> 16));
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD3, 0xFF, (u8)(cmd->arg >> 8));
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CMD4, 0xFF, (u8)cmd->arg);
	}

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, (u8)byte_cnt);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BYTE_CNT_H,
			0xFF, (u8)(byte_cnt >> 8));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, 1);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF, 0);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CFG2, 0xFF,
		SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
		SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_6);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
			CARD_DATA_SOURCE, 0x01, PINGPONG_BUFFER);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			trans_mode | SD_TRANSFER_START);
	rtsx_usb_add_cmd(ucr, CHECK_REG_CMD, SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

	if (cmd != NULL) {
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_CMD1, 0, 0);
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_CMD2, 0, 0);
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_CMD3, 0, 0);
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_CMD4, 0, 0);
	}

	err = rtsx_usb_send_cmd(ucr, MODE_CR, timeout);
	if (err) {
		dev_dbg(sdmmc_dev(host),
			"rtsx_usb_send_cmd failed (err = %d)\n", err);
		return err;
	}

	err = rtsx_usb_get_rsp(ucr, !cmd ? 1 : 5, timeout);
	if (err) {
		sd_print_debug_regs(host);
		dev_dbg(sdmmc_dev(host),
			"rtsx_usb_get_rsp failed (err = %d)\n", err);
		return err;
	}

	if (cmd != NULL) {
		cmd->resp[0] = get_unaligned_be32(ucr->rsp_buf + 1);
		dev_dbg(sdmmc_dev(host), "cmd->resp[0] = 0x%08x\n",
				cmd->resp[0]);
	}

	return 0;
}

static void sd_send_cmd_get_rsp(struct rtsx_usb_sdmmc *host,
		struct mmc_command *cmd)
{
	struct rtsx_ucr *ucr = host->ucr;
	u8 cmd_idx = (u8)cmd->opcode;
	u32 arg = cmd->arg;
	int err = 0;
	int timeout = 100;
	int i;
	u8 *ptr;
	int stat_idx = 0;
	int len = 2;
	u8 rsp_type;

	dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD %d, arg = 0x%08x\n",
			__func__, cmd_idx, arg);

	/* Response type:
	 * R0
	 * R1, R5, R6, R7
	 * R1b
	 * R2
	 * R3, R4
	 */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		rsp_type = SD_RSP_TYPE_R0;
		break;
	case MMC_RSP_R1:
		rsp_type = SD_RSP_TYPE_R1;
		break;
	case MMC_RSP_R1_NO_CRC:
		rsp_type = SD_RSP_TYPE_R1 | SD_NO_CHECK_CRC7;
		break;
	case MMC_RSP_R1B:
		rsp_type = SD_RSP_TYPE_R1b;
		break;
	case MMC_RSP_R2:
		rsp_type = SD_RSP_TYPE_R2;
		break;
	case MMC_RSP_R3:
		rsp_type = SD_RSP_TYPE_R3;
		break;
	default:
		dev_dbg(sdmmc_dev(host), "cmd->flag is not valid\n");
		err = -EINVAL;
		goto out;
	}

	if (rsp_type == SD_RSP_TYPE_R1b)
		timeout = 3000;

	if (cmd->opcode == SD_SWITCH_VOLTAGE) {
		err = rtsx_usb_write_register(ucr, SD_BUS_STAT,
				SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP,
				SD_CLK_TOGGLE_EN);
		if (err)
			goto out;
	}

	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CMD0, 0xFF, 0x40 | cmd_idx);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CMD1, 0xFF, (u8)(arg >> 24));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CMD2, 0xFF, (u8)(arg >> 16));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CMD3, 0xFF, (u8)(arg >> 8));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CMD4, 0xFF, (u8)arg);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CFG2, 0xFF, rsp_type);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, PINGPONG_BUFFER);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_TRANSFER,
			0xFF, SD_TM_CMD_RSP | SD_TRANSFER_START);
	rtsx_usb_add_cmd(ucr, CHECK_REG_CMD, SD_TRANSFER,
		     SD_TRANSFER_END | SD_STAT_IDLE,
		     SD_TRANSFER_END | SD_STAT_IDLE);

	if (rsp_type == SD_RSP_TYPE_R2) {
		/* Read data from ping-pong buffer */
		for (i = PPBUF_BASE2; i < PPBUF_BASE2 + 16; i++)
			rtsx_usb_add_cmd(ucr, READ_REG_CMD, (u16)i, 0, 0);
		stat_idx = 16;
	} else if (rsp_type != SD_RSP_TYPE_R0) {
		/* Read data from SD_CMDx registers */
		for (i = SD_CMD0; i <= SD_CMD4; i++)
			rtsx_usb_add_cmd(ucr, READ_REG_CMD, (u16)i, 0, 0);
		stat_idx = 5;
	}
	len += stat_idx;

	rtsx_usb_add_cmd(ucr, READ_REG_CMD, SD_STAT1, 0, 0);

	err = rtsx_usb_send_cmd(ucr, MODE_CR, 100);
	if (err) {
		dev_dbg(sdmmc_dev(host),
			"rtsx_usb_send_cmd error (err = %d)\n", err);
		goto out;
	}

	err = rtsx_usb_get_rsp(ucr, len, timeout);
	if (err || (ucr->rsp_buf[0] & SD_TRANSFER_ERR)) {
		sd_print_debug_regs(host);
		sd_clear_error(host);

		if (!err) {
			dev_dbg(sdmmc_dev(host),
				"Transfer failed (SD_TRANSFER = %02x)\n",
					ucr->rsp_buf[0]);
			err = -EIO;
		} else {
			dev_dbg(sdmmc_dev(host),
				"rtsx_usb_get_rsp failed (err = %d)\n", err);
		}

		goto out;
	}

	if (rsp_type == SD_RSP_TYPE_R0) {
		err = 0;
		goto out;
	}

	/* Skip result of CHECK_REG_CMD */
	ptr = ucr->rsp_buf + 1;

	/* Check (Start,Transmission) bit of Response */
	if ((ptr[0] & 0xC0) != 0) {
		err = -EILSEQ;
		dev_dbg(sdmmc_dev(host), "Invalid response bit\n");
		goto out;
	}

	/* Check CRC7 */
	if (!(rsp_type & SD_NO_CHECK_CRC7)) {
		if (ptr[stat_idx] & SD_CRC7_ERR) {
			err = -EILSEQ;
			dev_dbg(sdmmc_dev(host), "CRC7 error\n");
			goto out;
		}
	}

	if (rsp_type == SD_RSP_TYPE_R2) {
		/*
		 * The controller offloads the last byte {CRC-7, end bit 1'b1}
		 * of response type R2. Assign dummy CRC, 0, and end bit to the
		 * byte(ptr[16], goes into the LSB of resp[3] later).
		 */
		ptr[16] = 1;

		for (i = 0; i < 4; i++) {
			cmd->resp[i] = get_unaligned_be32(ptr + 1 + i * 4);
			dev_dbg(sdmmc_dev(host), "cmd->resp[%d] = 0x%08x\n",
					i, cmd->resp[i]);
		}
	} else {
		cmd->resp[0] = get_unaligned_be32(ptr + 1);
		dev_dbg(sdmmc_dev(host), "cmd->resp[0] = 0x%08x\n",
				cmd->resp[0]);
	}

out:
	cmd->error = err;
}

static int sd_rw_multi(struct rtsx_usb_sdmmc *host, struct mmc_request *mrq)
{
	struct rtsx_ucr *ucr = host->ucr;
	struct mmc_data *data = mrq->data;
	int read = (data->flags & MMC_DATA_READ) ? 1 : 0;
	u8 cfg2, trans_mode;
	int err;
	u8 flag;
	size_t data_len = data->blksz * data->blocks;
	unsigned int pipe;

	if (read) {
		dev_dbg(sdmmc_dev(host), "%s: read %zu bytes\n",
				__func__, data_len);
		cfg2 = SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_0;
		trans_mode = SD_TM_AUTO_READ_3;
	} else {
		dev_dbg(sdmmc_dev(host), "%s: write %zu bytes\n",
				__func__, data_len);
		cfg2 = SD_NO_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			SD_NO_WAIT_BUSY_END | SD_NO_CHECK_CRC7 | SD_RSP_LEN_0;
		trans_mode = SD_TM_AUTO_WRITE_3;
	}

	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0x00);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF, 0x02);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BLOCK_CNT_L,
			0xFF, (u8)data->blocks);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BLOCK_CNT_H,
			0xFF, (u8)(data->blocks >> 8));

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, RING_BUFFER);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_TC3,
			0xFF, (u8)(data_len >> 24));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_TC2,
			0xFF, (u8)(data_len >> 16));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_TC1,
			0xFF, (u8)(data_len >> 8));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_TC0,
			0xFF, (u8)data_len);
	if (read) {
		flag = MODE_CDIR;
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_CTL,
				0x03 | DMA_PACK_SIZE_MASK,
				DMA_DIR_FROM_CARD | DMA_EN | DMA_512);
	} else {
		flag = MODE_CDOR;
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_CTL,
				0x03 | DMA_PACK_SIZE_MASK,
				DMA_DIR_TO_CARD | DMA_EN | DMA_512);
	}

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CFG2, 0xFF, cfg2);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			trans_mode | SD_TRANSFER_START);
	rtsx_usb_add_cmd(ucr, CHECK_REG_CMD, SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

	err = rtsx_usb_send_cmd(ucr, flag, 100);
	if (err)
		return err;

	if (read)
		pipe = usb_rcvbulkpipe(ucr->pusb_dev, EP_BULK_IN);
	else
		pipe = usb_sndbulkpipe(ucr->pusb_dev, EP_BULK_OUT);

	err = rtsx_usb_transfer_data(ucr, pipe, data->sg, data_len,
			data->sg_len,  NULL, 10000);
	if (err) {
		dev_dbg(sdmmc_dev(host), "rtsx_usb_transfer_data error %d\n"
				, err);
		sd_clear_error(host);
		return err;
	}

	return rtsx_usb_get_rsp(ucr, 1, 2000);
}

static inline void sd_enable_initial_mode(struct rtsx_usb_sdmmc *host)
{
	rtsx_usb_write_register(host->ucr, SD_CFG1,
			SD_CLK_DIVIDE_MASK, SD_CLK_DIVIDE_128);
}

static inline void sd_disable_initial_mode(struct rtsx_usb_sdmmc *host)
{
	rtsx_usb_write_register(host->ucr, SD_CFG1,
			SD_CLK_DIVIDE_MASK, SD_CLK_DIVIDE_0);
}

static void sd_normal_rw(struct rtsx_usb_sdmmc *host,
		struct mmc_request *mrq)
{
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;
	u8 *buf;

	buf = kzalloc(data->blksz, GFP_NOIO);
	if (!buf) {
		cmd->error = -ENOMEM;
		return;
	}

	if (data->flags & MMC_DATA_READ) {
		if (host->initial_mode)
			sd_disable_initial_mode(host);

		cmd->error = sd_read_data(host, cmd, (u16)data->blksz, buf,
				data->blksz, 200);

		if (host->initial_mode)
			sd_enable_initial_mode(host);

		sg_copy_from_buffer(data->sg, data->sg_len, buf, data->blksz);
	} else {
		sg_copy_to_buffer(data->sg, data->sg_len, buf, data->blksz);

		cmd->error = sd_write_data(host, cmd, (u16)data->blksz, buf,
				data->blksz, 200);
	}

	kfree(buf);
}

static int sd_change_phase(struct rtsx_usb_sdmmc *host, u8 sample_point, int tx)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;

	dev_dbg(sdmmc_dev(host), "%s: %s sample_point = %d\n",
			__func__, tx ? "TX" : "RX", sample_point);

	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CLK_DIV, CLK_CHANGE, CLK_CHANGE);

	if (tx)
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_VPCLK0_CTL,
				0x0F, sample_point);
	else
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_VPCLK1_CTL,
				0x0F, sample_point);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_VPCLK0_CTL, PHASE_NOT_RESET, 0);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_VPCLK0_CTL,
			PHASE_NOT_RESET, PHASE_NOT_RESET);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CLK_DIV, CLK_CHANGE, 0);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CFG1, SD_ASYNC_FIFO_RST, 0);

	err = rtsx_usb_send_cmd(ucr, MODE_C, 100);
	if (err)
		return err;

	return 0;
}

static inline u32 get_phase_point(u32 phase_map, unsigned int idx)
{
	idx &= MAX_PHASE;
	return phase_map & (1 << idx);
}

static int get_phase_len(u32 phase_map, unsigned int idx)
{
	int i;

	for (i = 0; i < MAX_PHASE + 1; i++) {
		if (get_phase_point(phase_map, idx + i) == 0)
			return i;
	}
	return MAX_PHASE + 1;
}

static u8 sd_search_final_phase(struct rtsx_usb_sdmmc *host, u32 phase_map)
{
	int start = 0, len = 0;
	int start_final = 0, len_final = 0;
	u8 final_phase = 0xFF;

	if (phase_map == 0) {
		dev_dbg(sdmmc_dev(host), "Phase: [map:%x]\n", phase_map);
		return final_phase;
	}

	while (start < MAX_PHASE + 1) {
		len = get_phase_len(phase_map, start);
		if (len_final < len) {
			start_final = start;
			len_final = len;
		}
		start += len ? len : 1;
	}

	final_phase = (start_final + len_final / 2) & MAX_PHASE;
	dev_dbg(sdmmc_dev(host), "Phase: [map:%x] [maxlen:%d] [final:%d]\n",
		phase_map, len_final, final_phase);

	return final_phase;
}

static void sd_wait_data_idle(struct rtsx_usb_sdmmc *host)
{
	int err, i;
	u8 val = 0;

	for (i = 0; i < 100; i++) {
		err = rtsx_usb_ep0_read_register(host->ucr,
				SD_DATA_STATE, &val);
		if (val & SD_DATA_IDLE)
			return;

		usleep_range(100, 1000);
	}
}

static int sd_tuning_rx_cmd(struct rtsx_usb_sdmmc *host,
		u8 opcode, u8 sample_point)
{
	int err;
	struct mmc_command cmd = {0};

	err = sd_change_phase(host, sample_point, 0);
	if (err)
		return err;

	cmd.opcode = MMC_SEND_TUNING_BLOCK;
	err = sd_read_data(host, &cmd, 0x40, NULL, 0, 100);
	if (err) {
		/* Wait till SD DATA IDLE */
		sd_wait_data_idle(host);
		sd_clear_error(host);
		return err;
	}

	return 0;
}

static void sd_tuning_phase(struct rtsx_usb_sdmmc *host,
		u8 opcode, u16 *phase_map)
{
	int err, i;
	u16 raw_phase_map = 0;

	for (i = MAX_PHASE; i >= 0; i--) {
		err = sd_tuning_rx_cmd(host, opcode, (u8)i);
		if (!err)
			raw_phase_map |= 1 << i;
	}

	if (phase_map)
		*phase_map = raw_phase_map;
}

static int sd_tuning_rx(struct rtsx_usb_sdmmc *host, u8 opcode)
{
	int err, i;
	u16 raw_phase_map[RX_TUNING_CNT] = {0}, phase_map;
	u8 final_phase;

	/* setting fixed default TX phase */
	err = sd_change_phase(host, 0x01, 1);
	if (err) {
		dev_dbg(sdmmc_dev(host), "TX phase setting failed\n");
		return err;
	}

	/* tuning RX phase */
	for (i = 0; i < RX_TUNING_CNT; i++) {
		sd_tuning_phase(host, opcode, &(raw_phase_map[i]));

		if (raw_phase_map[i] == 0)
			break;
	}

	phase_map = 0xFFFF;
	for (i = 0; i < RX_TUNING_CNT; i++) {
		dev_dbg(sdmmc_dev(host), "RX raw_phase_map[%d] = 0x%04x\n",
				i, raw_phase_map[i]);
		phase_map &= raw_phase_map[i];
	}
	dev_dbg(sdmmc_dev(host), "RX phase_map = 0x%04x\n", phase_map);

	if (phase_map) {
		final_phase = sd_search_final_phase(host, phase_map);
		if (final_phase == 0xFF)
			return -EINVAL;

		err = sd_change_phase(host, final_phase, 0);
		if (err)
			return err;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int sdmmc_get_ro(struct mmc_host *mmc)
{
	struct rtsx_usb_sdmmc *host = mmc_priv(mmc);
	struct rtsx_ucr *ucr = host->ucr;
	int err;
	u16 val;

	if (host->host_removal)
		return -ENOMEDIUM;

	mutex_lock(&ucr->dev_mutex);

	/* Check SD card detect */
	err = rtsx_usb_get_card_status(ucr, &val);

	mutex_unlock(&ucr->dev_mutex);


	/* Treat failed detection as non-ro */
	if (err)
		return 0;

	if (val & SD_WP)
		return 1;

	return 0;
}

static int sdmmc_get_cd(struct mmc_host *mmc)
{
	struct rtsx_usb_sdmmc *host = mmc_priv(mmc);
	struct rtsx_ucr *ucr = host->ucr;
	int err;
	u16 val;

	if (host->host_removal)
		return -ENOMEDIUM;

	mutex_lock(&ucr->dev_mutex);

	/* Check SD card detect */
	err = rtsx_usb_get_card_status(ucr, &val);

	mutex_unlock(&ucr->dev_mutex);

	/* Treat failed detection as non-exist */
	if (err)
		goto no_card;

	if (val & SD_CD) {
		host->card_exist = true;
		return 1;
	}

no_card:
	host->card_exist = false;
	return 0;
}

static void sdmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct rtsx_usb_sdmmc *host = mmc_priv(mmc);
	struct rtsx_ucr *ucr = host->ucr;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;
	unsigned int data_size = 0;

	dev_dbg(sdmmc_dev(host), "%s\n", __func__);

	if (host->host_removal) {
		cmd->error = -ENOMEDIUM;
		goto finish;
	}

	if ((!host->card_exist)) {
		cmd->error = -ENOMEDIUM;
		goto finish_detect_card;
	}

	/*
	 * Reject SDIO CMDs to speed up card identification
	 * since unsupported
	 */
	if (cmd->opcode == SD_IO_SEND_OP_COND ||
	    cmd->opcode == SD_IO_RW_DIRECT ||
	    cmd->opcode == SD_IO_RW_EXTENDED) {
		cmd->error = -EINVAL;
		goto finish;
	}

	mutex_lock(&ucr->dev_mutex);

	mutex_lock(&host->host_mutex);
	host->mrq = mrq;
	mutex_unlock(&host->host_mutex);

	if (mrq->data)
		data_size = data->blocks * data->blksz;

	if (!data_size) {
		sd_send_cmd_get_rsp(host, cmd);
	} else if ((!(data_size % 512) && cmd->opcode != MMC_SEND_EXT_CSD) ||
		   mmc_op_multi(cmd->opcode)) {
		sd_send_cmd_get_rsp(host, cmd);

		if (!cmd->error) {
			sd_rw_multi(host, mrq);

			if (mmc_op_multi(cmd->opcode) && mrq->stop) {
				sd_send_cmd_get_rsp(host, mrq->stop);
				rtsx_usb_write_register(ucr, MC_FIFO_CTL,
						FIFO_FLUSH, FIFO_FLUSH);
			}
		}
	} else {
		sd_normal_rw(host, mrq);
	}

	if (mrq->data) {
		if (cmd->error || data->error)
			data->bytes_xfered = 0;
		else
			data->bytes_xfered = data->blocks * data->blksz;
	}

	mutex_unlock(&ucr->dev_mutex);

finish_detect_card:
	if (cmd->error) {
		/*
		 * detect card when fail to update card existence state and
		 * speed up card removal when retry
		 */
		sdmmc_get_cd(mmc);
		dev_dbg(sdmmc_dev(host), "cmd->error = %d\n", cmd->error);
	}

finish:
	mutex_lock(&host->host_mutex);
	host->mrq = NULL;
	mutex_unlock(&host->host_mutex);

	mmc_request_done(mmc, mrq);
}

static int sd_set_bus_width(struct rtsx_usb_sdmmc *host,
		unsigned char bus_width)
{
	int err = 0;
	u8 width[] = {
		[MMC_BUS_WIDTH_1] = SD_BUS_WIDTH_1BIT,
		[MMC_BUS_WIDTH_4] = SD_BUS_WIDTH_4BIT,
		[MMC_BUS_WIDTH_8] = SD_BUS_WIDTH_8BIT,
	};

	if (bus_width <= MMC_BUS_WIDTH_8)
		err = rtsx_usb_write_register(host->ucr, SD_CFG1,
				0x03, width[bus_width]);

	return err;
}

static int sd_pull_ctl_disable_lqfp48(struct rtsx_ucr *ucr)
{
	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0x95);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0xA5);

	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}

static int sd_pull_ctl_disable_qfn24(struct rtsx_ucr *ucr)
{
	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0x65);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0x95);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x56);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0x59);

	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}

static int sd_pull_ctl_enable_lqfp48(struct rtsx_ucr *ucr)
{
	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0xAA);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0xAA);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0xA9);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0xA5);

	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}

static int sd_pull_ctl_enable_qfn24(struct rtsx_ucr *ucr)
{
	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0xA5);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0x9A);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0xA5);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x9A);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x65);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0x5A);

	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}

static int sd_power_on(struct rtsx_usb_sdmmc *host)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;

	dev_dbg(sdmmc_dev(host), "%s\n", __func__);
	rtsx_usb_init_cmd(ucr);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_SELECT, 0x07, SD_MOD_SEL);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_SHARE_MODE,
			CARD_SHARE_MASK, CARD_SHARE_SD);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_CLK_EN,
			SD_CLK_EN, SD_CLK_EN);
	err = rtsx_usb_send_cmd(ucr, MODE_C, 100);
	if (err)
		return err;

	if (CHECK_PKG(ucr, LQFP48))
		err = sd_pull_ctl_enable_lqfp48(ucr);
	else
		err = sd_pull_ctl_enable_qfn24(ucr);
	if (err)
		return err;

	err = rtsx_usb_write_register(ucr, CARD_PWR_CTL,
			POWER_MASK, PARTIAL_POWER_ON);
	if (err)
		return err;

	usleep_range(800, 1000);

	rtsx_usb_init_cmd(ucr);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PWR_CTL,
			POWER_MASK|LDO3318_PWR_MASK, POWER_ON|LDO_ON);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_OE,
			SD_OUTPUT_EN, SD_OUTPUT_EN);

	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}

static int sd_power_off(struct rtsx_usb_sdmmc *host)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;

	dev_dbg(sdmmc_dev(host), "%s\n", __func__);
	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_CLK_EN, SD_CLK_EN, 0);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_OE, SD_OUTPUT_EN, 0);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PWR_CTL,
			POWER_MASK, POWER_OFF);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PWR_CTL,
			POWER_MASK|LDO3318_PWR_MASK, POWER_OFF|LDO_SUSPEND);

	err = rtsx_usb_send_cmd(ucr, MODE_C, 100);
	if (err)
		return err;

	if (CHECK_PKG(ucr, LQFP48))
			return sd_pull_ctl_disable_lqfp48(ucr);
	return sd_pull_ctl_disable_qfn24(ucr);
}

static int sd_set_power_mode(struct rtsx_usb_sdmmc *host,
		unsigned char power_mode)
{
	int err;

	if (power_mode != MMC_POWER_OFF)
		power_mode = MMC_POWER_ON;

	if (power_mode == host->power_mode)
		return 0;

	if (power_mode == MMC_POWER_OFF) {
		err = sd_power_off(host);
		pm_runtime_put(sdmmc_dev(host));
	} else {
		pm_runtime_get_sync(sdmmc_dev(host));
		err = sd_power_on(host);
	}

	if (!err)
		host->power_mode = power_mode;

	return err;
}

static int sd_set_timing(struct rtsx_usb_sdmmc *host,
		unsigned char timing, bool *ddr_mode)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;

	*ddr_mode = false;

	rtsx_usb_init_cmd(ucr);

	switch (timing) {
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_UHS_SDR50:
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CFG1,
				0x0C | SD_ASYNC_FIFO_RST,
				SD_30_MODE | SD_ASYNC_FIFO_RST);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
				CRC_VAR_CLK0 | SD30_FIX_CLK | SAMPLE_VAR_CLK1);
		break;

	case MMC_TIMING_UHS_DDR50:
		*ddr_mode = true;

		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CFG1,
				0x0C | SD_ASYNC_FIFO_RST,
				SD_DDR_MODE | SD_ASYNC_FIFO_RST);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
				CRC_VAR_CLK0 | SD30_FIX_CLK | SAMPLE_VAR_CLK1);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_PUSH_POINT_CTL,
				DDR_VAR_TX_CMD_DAT, DDR_VAR_TX_CMD_DAT);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL,
				DDR_VAR_RX_DAT | DDR_VAR_RX_CMD,
				DDR_VAR_RX_DAT | DDR_VAR_RX_CMD);
		break;

	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_CFG1,
				0x0C, SD_20_MODE);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
				CRC_FIX_CLK | SD30_VAR_CLK0 | SAMPLE_VAR_CLK1);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_PUSH_POINT_CTL,
				SD20_TX_SEL_MASK, SD20_TX_14_AHEAD);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL,
				SD20_RX_SEL_MASK, SD20_RX_14_DELAY);
		break;

	default:
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_CFG1, 0x0C, SD_20_MODE);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
				CRC_FIX_CLK | SD30_VAR_CLK0 | SAMPLE_VAR_CLK1);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				SD_PUSH_POINT_CTL, 0xFF, 0);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL,
				SD20_RX_SEL_MASK, SD20_RX_POS_EDGE);
		break;
	}

	err = rtsx_usb_send_cmd(ucr, MODE_C, 100);

	return err;
}

static void sdmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct rtsx_usb_sdmmc *host = mmc_priv(mmc);
	struct rtsx_ucr *ucr = host->ucr;

	dev_dbg(sdmmc_dev(host), "%s\n", __func__);
	mutex_lock(&ucr->dev_mutex);

	sd_set_power_mode(host, ios->power_mode);
	sd_set_bus_width(host, ios->bus_width);
	sd_set_timing(host, ios->timing, &host->ddr_mode);

	host->vpclk = false;
	host->double_clk = true;

	switch (ios->timing) {
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_UHS_SDR50:
		host->ssc_depth = SSC_DEPTH_2M;
		host->vpclk = true;
		host->double_clk = false;
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_UHS_SDR25:
		host->ssc_depth = SSC_DEPTH_1M;
		break;
	default:
		host->ssc_depth = SSC_DEPTH_512K;
		break;
	}

	host->initial_mode = (ios->clock <= 1000000) ? true : false;
	host->clock = ios->clock;

	rtsx_usb_switch_clock(host->ucr, host->clock, host->ssc_depth,
			host->initial_mode, host->double_clk, host->vpclk);

	mutex_unlock(&ucr->dev_mutex);
	dev_dbg(sdmmc_dev(host), "%s end\n", __func__);
}

static int sdmmc_switch_voltage(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct rtsx_usb_sdmmc *host = mmc_priv(mmc);
	struct rtsx_ucr *ucr = host->ucr;
	int err = 0;

	dev_dbg(sdmmc_dev(host), "%s: signal_voltage = %d\n",
			__func__, ios->signal_voltage);

	if (host->host_removal)
		return -ENOMEDIUM;

	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_120)
		return -EPERM;

	mutex_lock(&ucr->dev_mutex);

	err = rtsx_usb_card_exclusive_check(ucr, RTSX_USB_SD_CARD);
	if (err) {
		mutex_unlock(&ucr->dev_mutex);
		return err;
	}

	/* Let mmc core do the busy checking, simply stop the forced-toggle
	 * clock(while issuing CMD11) and switch voltage.
	 */
	rtsx_usb_init_cmd(ucr);

	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330) {
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_PAD_CTL,
				SD_IO_USING_1V8, SD_IO_USING_3V3);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, LDO_POWER_CFG,
				TUNE_SD18_MASK, TUNE_SD18_3V3);
	} else {
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_BUS_STAT,
				SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP,
				SD_CLK_FORCE_STOP);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, SD_PAD_CTL,
				SD_IO_USING_1V8, SD_IO_USING_1V8);
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, LDO_POWER_CFG,
				TUNE_SD18_MASK, TUNE_SD18_1V8);
	}

	err = rtsx_usb_send_cmd(ucr, MODE_C, 100);
	mutex_unlock(&ucr->dev_mutex);

	return err;
}

static int sdmmc_card_busy(struct mmc_host *mmc)
{
	struct rtsx_usb_sdmmc *host = mmc_priv(mmc);
	struct rtsx_ucr *ucr = host->ucr;
	int err;
	u8 stat;
	u8 mask = SD_DAT3_STATUS | SD_DAT2_STATUS | SD_DAT1_STATUS
		| SD_DAT0_STATUS;

	dev_dbg(sdmmc_dev(host), "%s\n", __func__);

	mutex_lock(&ucr->dev_mutex);

	err = rtsx_usb_write_register(ucr, SD_BUS_STAT,
			SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP,
			SD_CLK_TOGGLE_EN);
	if (err)
		goto out;

	mdelay(1);

	err = rtsx_usb_read_register(ucr, SD_BUS_STAT, &stat);
	if (err)
		goto out;

	err = rtsx_usb_write_register(ucr, SD_BUS_STAT,
			SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP, 0);
out:
	mutex_unlock(&ucr->dev_mutex);

	if (err)
		return err;

	/* check if any pin between dat[0:3] is low */
	if ((stat & mask) != mask)
		return 1;
	else
		return 0;
}

static int sdmmc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct rtsx_usb_sdmmc *host = mmc_priv(mmc);
	struct rtsx_ucr *ucr = host->ucr;
	int err = 0;

	if (host->host_removal)
		return -ENOMEDIUM;

	mutex_lock(&ucr->dev_mutex);

	if (!host->ddr_mode)
		err = sd_tuning_rx(host, MMC_SEND_TUNING_BLOCK);

	mutex_unlock(&ucr->dev_mutex);

	return err;
}

static const struct mmc_host_ops rtsx_usb_sdmmc_ops = {
	.request = sdmmc_request,
	.set_ios = sdmmc_set_ios,
	.get_ro = sdmmc_get_ro,
	.get_cd = sdmmc_get_cd,
	.start_signal_voltage_switch = sdmmc_switch_voltage,
	.card_busy = sdmmc_card_busy,
	.execute_tuning = sdmmc_execute_tuning,
};

#ifdef RTSX_USB_USE_LEDS_CLASS
static void rtsx_usb_led_control(struct led_classdev *led,
	enum led_brightness brightness)
{
	struct rtsx_usb_sdmmc *host = container_of(led,
			struct rtsx_usb_sdmmc, led);

	if (host->host_removal)
		return;

	host->led.brightness = brightness;
	schedule_work(&host->led_work);
}

static void rtsx_usb_update_led(struct work_struct *work)
{
	struct rtsx_usb_sdmmc *host =
		container_of(work, struct rtsx_usb_sdmmc, led_work);
	struct rtsx_ucr *ucr = host->ucr;

	pm_runtime_get_sync(sdmmc_dev(host));
	mutex_lock(&ucr->dev_mutex);

	if (host->led.brightness == LED_OFF)
		rtsx_usb_turn_off_led(ucr);
	else
		rtsx_usb_turn_on_led(ucr);

	mutex_unlock(&ucr->dev_mutex);
	pm_runtime_put(sdmmc_dev(host));
}
#endif

static void rtsx_usb_init_host(struct rtsx_usb_sdmmc *host)
{
	struct mmc_host *mmc = host->mmc;

	mmc->f_min = 250000;
	mmc->f_max = 208000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SD_HIGHSPEED |
		MMC_CAP_MMC_HIGHSPEED | MMC_CAP_BUS_WIDTH_TEST |
		MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50 |
		MMC_CAP_NEEDS_POLL;
	mmc->caps2 = MMC_CAP2_NO_PRESCAN_POWERUP | MMC_CAP2_FULL_PWR_CYCLE;

	mmc->max_current_330 = 400;
	mmc->max_current_180 = 800;
	mmc->ops = &rtsx_usb_sdmmc_ops;
	mmc->max_segs = 256;
	mmc->max_seg_size = 65536;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = 65535;
	mmc->max_req_size = 524288;

	host->power_mode = MMC_POWER_OFF;
}

static int rtsx_usb_sdmmc_drv_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct rtsx_usb_sdmmc *host;
	struct rtsx_ucr *ucr;
#ifdef RTSX_USB_USE_LEDS_CLASS
	int err;
#endif

	ucr = usb_get_intfdata(to_usb_interface(pdev->dev.parent));
	if (!ucr)
		return -ENXIO;

	dev_dbg(&(pdev->dev), ": Realtek USB SD/MMC controller found\n");

	mmc = mmc_alloc_host(sizeof(*host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->ucr = ucr;
	host->mmc = mmc;
	host->pdev = pdev;
	platform_set_drvdata(pdev, host);

	mutex_init(&host->host_mutex);
	rtsx_usb_init_host(host);
	pm_runtime_enable(&pdev->dev);

#ifdef RTSX_USB_USE_LEDS_CLASS
	snprintf(host->led_name, sizeof(host->led_name),
		"%s::", mmc_hostname(mmc));
	host->led.name = host->led_name;
	host->led.brightness = LED_OFF;
	host->led.default_trigger = mmc_hostname(mmc);
	host->led.brightness_set = rtsx_usb_led_control;

	err = led_classdev_register(mmc_dev(mmc), &host->led);
	if (err)
		dev_err(&(pdev->dev),
				"Failed to register LED device: %d\n", err);
	INIT_WORK(&host->led_work, rtsx_usb_update_led);

#endif
	mmc_add_host(mmc);

	return 0;
}

static int rtsx_usb_sdmmc_drv_remove(struct platform_device *pdev)
{
	struct rtsx_usb_sdmmc *host = platform_get_drvdata(pdev);
	struct mmc_host *mmc;

	if (!host)
		return 0;

	mmc = host->mmc;
	host->host_removal = true;

	mutex_lock(&host->host_mutex);
	if (host->mrq) {
		dev_dbg(&(pdev->dev),
			"%s: Controller removed during transfer\n",
			mmc_hostname(mmc));
		host->mrq->cmd->error = -ENOMEDIUM;
		if (host->mrq->stop)
			host->mrq->stop->error = -ENOMEDIUM;
		mmc_request_done(mmc, host->mrq);
	}
	mutex_unlock(&host->host_mutex);

	mmc_remove_host(mmc);

#ifdef RTSX_USB_USE_LEDS_CLASS
	cancel_work_sync(&host->led_work);
	led_classdev_unregister(&host->led);
#endif

	mmc_free_host(mmc);
	pm_runtime_disable(&pdev->dev);
	platform_set_drvdata(pdev, NULL);

	dev_dbg(&(pdev->dev),
		": Realtek USB SD/MMC module has been removed\n");

	return 0;
}

static const struct platform_device_id rtsx_usb_sdmmc_ids[] = {
	{
		.name = "rtsx_usb_sdmmc",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, rtsx_usb_sdmmc_ids);

static struct platform_driver rtsx_usb_sdmmc_driver = {
	.probe		= rtsx_usb_sdmmc_drv_probe,
	.remove		= rtsx_usb_sdmmc_drv_remove,
	.id_table       = rtsx_usb_sdmmc_ids,
	.driver		= {
		.name	= "rtsx_usb_sdmmc",
	},
};
module_platform_driver(rtsx_usb_sdmmc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Roger Tseng <rogerable@realtek.com>");
MODULE_DESCRIPTION("Realtek USB SD/MMC Card Host Driver");
