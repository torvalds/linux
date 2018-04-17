/* Realtek PCI-Express SD/MMC Card Interface driver
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
 *   Wei WANG <wei_wang@realsil.com.cn>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/card.h>
#include <linux/rtsx_pci.h>
#include <asm/unaligned.h>

struct realtek_pci_sdmmc {
	struct platform_device	*pdev;
	struct rtsx_pcr		*pcr;
	struct mmc_host		*mmc;
	struct mmc_request	*mrq;
#define SDMMC_WORKQ_NAME	"rtsx_pci_sdmmc_workq"

	struct work_struct	work;
	struct mutex		host_mutex;

	u8			ssc_depth;
	unsigned int		clock;
	bool			vpclk;
	bool			double_clk;
	bool			eject;
	bool			initial_mode;
	int			power_state;
#define SDMMC_POWER_ON		1
#define SDMMC_POWER_OFF		0

	int			sg_count;
	s32			cookie;
	int			cookie_sg_count;
	bool			using_cookie;
};

static inline struct device *sdmmc_dev(struct realtek_pci_sdmmc *host)
{
	return &(host->pdev->dev);
}

static inline void sd_clear_error(struct realtek_pci_sdmmc *host)
{
	rtsx_pci_write_register(host->pcr, CARD_STOP,
			SD_STOP | SD_CLR_ERR, SD_STOP | SD_CLR_ERR);
}

#ifdef DEBUG
static void dump_reg_range(struct realtek_pci_sdmmc *host, u16 start, u16 end)
{
	u16 len = end - start + 1;
	int i;
	u8 data[8];

	for (i = 0; i < len; i += 8) {
		int j;
		int n = min(8, len - i);

		memset(&data, 0, sizeof(data));
		for (j = 0; j < n; j++)
			rtsx_pci_read_register(host->pcr, start + i + j,
				data + j);
		dev_dbg(sdmmc_dev(host), "0x%04X(%d): %8ph\n",
			start + i, n, data);
	}
}

static void sd_print_debug_regs(struct realtek_pci_sdmmc *host)
{
	dump_reg_range(host, 0xFDA0, 0xFDB3);
	dump_reg_range(host, 0xFD52, 0xFD69);
}
#else
#define sd_print_debug_regs(host)
#endif /* DEBUG */

static inline int sd_get_cd_int(struct realtek_pci_sdmmc *host)
{
	return rtsx_pci_readl(host->pcr, RTSX_BIPR) & SD_EXIST;
}

static void sd_cmd_set_sd_cmd(struct rtsx_pcr *pcr, struct mmc_command *cmd)
{
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CMD0, 0xFF,
		SD_CMD_START | cmd->opcode);
	rtsx_pci_write_be32(pcr, SD_CMD1, cmd->arg);
}

static void sd_cmd_set_data_len(struct rtsx_pcr *pcr, u16 blocks, u16 blksz)
{
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, blocks);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF, blocks >> 8);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, blksz);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF, blksz >> 8);
}

static int sd_response_type(struct mmc_command *cmd)
{
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		return SD_RSP_TYPE_R0;
	case MMC_RSP_R1:
		return SD_RSP_TYPE_R1;
	case MMC_RSP_R1_NO_CRC:
		return SD_RSP_TYPE_R1 | SD_NO_CHECK_CRC7;
	case MMC_RSP_R1B:
		return SD_RSP_TYPE_R1b;
	case MMC_RSP_R2:
		return SD_RSP_TYPE_R2;
	case MMC_RSP_R3:
		return SD_RSP_TYPE_R3;
	default:
		return -EINVAL;
	}
}

static int sd_status_index(int resp_type)
{
	if (resp_type == SD_RSP_TYPE_R0)
		return 0;
	else if (resp_type == SD_RSP_TYPE_R2)
		return 16;

	return 5;
}
/*
 * sd_pre_dma_transfer - do dma_map_sg() or using cookie
 *
 * @pre: if called in pre_req()
 * return:
 *	0 - do dma_map_sg()
 *	1 - using cookie
 */
static int sd_pre_dma_transfer(struct realtek_pci_sdmmc *host,
		struct mmc_data *data, bool pre)
{
	struct rtsx_pcr *pcr = host->pcr;
	int read = data->flags & MMC_DATA_READ;
	int count = 0;
	int using_cookie = 0;

	if (!pre && data->host_cookie && data->host_cookie != host->cookie) {
		dev_err(sdmmc_dev(host),
			"error: data->host_cookie = %d, host->cookie = %d\n",
			data->host_cookie, host->cookie);
		data->host_cookie = 0;
	}

	if (pre || data->host_cookie != host->cookie) {
		count = rtsx_pci_dma_map_sg(pcr, data->sg, data->sg_len, read);
	} else {
		count = host->cookie_sg_count;
		using_cookie = 1;
	}

	if (pre) {
		host->cookie_sg_count = count;
		if (++host->cookie < 0)
			host->cookie = 1;
		data->host_cookie = host->cookie;
	} else {
		host->sg_count = count;
	}

	return using_cookie;
}

static void sdmmc_pre_req(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (data->host_cookie) {
		dev_err(sdmmc_dev(host),
			"error: reset data->host_cookie = %d\n",
			data->host_cookie);
		data->host_cookie = 0;
	}

	sd_pre_dma_transfer(host, data, true);
	dev_dbg(sdmmc_dev(host), "pre dma sg: %d\n", host->cookie_sg_count);
}

static void sdmmc_post_req(struct mmc_host *mmc, struct mmc_request *mrq,
		int err)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct rtsx_pcr *pcr = host->pcr;
	struct mmc_data *data = mrq->data;
	int read = data->flags & MMC_DATA_READ;

	rtsx_pci_dma_unmap_sg(pcr, data->sg, data->sg_len, read);
	data->host_cookie = 0;
}

static void sd_send_cmd_get_rsp(struct realtek_pci_sdmmc *host,
		struct mmc_command *cmd)
{
	struct rtsx_pcr *pcr = host->pcr;
	u8 cmd_idx = (u8)cmd->opcode;
	u32 arg = cmd->arg;
	int err = 0;
	int timeout = 100;
	int i;
	u8 *ptr;
	int rsp_type;
	int stat_idx;
	bool clock_toggled = false;

	dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD %d, arg = 0x%08x\n",
			__func__, cmd_idx, arg);

	rsp_type = sd_response_type(cmd);
	if (rsp_type < 0)
		goto out;

	stat_idx = sd_status_index(rsp_type);

	if (rsp_type == SD_RSP_TYPE_R1b)
		timeout = cmd->busy_timeout ? cmd->busy_timeout : 3000;

	if (cmd->opcode == SD_SWITCH_VOLTAGE) {
		err = rtsx_pci_write_register(pcr, SD_BUS_STAT,
				0xFF, SD_CLK_TOGGLE_EN);
		if (err < 0)
			goto out;

		clock_toggled = true;
	}

	rtsx_pci_init_cmd(pcr);
	sd_cmd_set_sd_cmd(pcr, cmd);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG2, 0xFF, rsp_type);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, PINGPONG_BUFFER);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_TRANSFER,
			0xFF, SD_TM_CMD_RSP | SD_TRANSFER_START);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, SD_TRANSFER,
		     SD_TRANSFER_END | SD_STAT_IDLE,
		     SD_TRANSFER_END | SD_STAT_IDLE);

	if (rsp_type == SD_RSP_TYPE_R2) {
		/* Read data from ping-pong buffer */
		for (i = PPBUF_BASE2; i < PPBUF_BASE2 + 16; i++)
			rtsx_pci_add_cmd(pcr, READ_REG_CMD, (u16)i, 0, 0);
	} else if (rsp_type != SD_RSP_TYPE_R0) {
		/* Read data from SD_CMDx registers */
		for (i = SD_CMD0; i <= SD_CMD4; i++)
			rtsx_pci_add_cmd(pcr, READ_REG_CMD, (u16)i, 0, 0);
	}

	rtsx_pci_add_cmd(pcr, READ_REG_CMD, SD_STAT1, 0, 0);

	err = rtsx_pci_send_cmd(pcr, timeout);
	if (err < 0) {
		sd_print_debug_regs(host);
		sd_clear_error(host);
		dev_dbg(sdmmc_dev(host),
			"rtsx_pci_send_cmd error (err = %d)\n", err);
		goto out;
	}

	if (rsp_type == SD_RSP_TYPE_R0) {
		err = 0;
		goto out;
	}

	/* Eliminate returned value of CHECK_REG_CMD */
	ptr = rtsx_pci_get_cmd_data(pcr) + 1;

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

	if (err && clock_toggled)
		rtsx_pci_write_register(pcr, SD_BUS_STAT,
				SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP, 0);
}

static int sd_read_data(struct realtek_pci_sdmmc *host, struct mmc_command *cmd,
	u16 byte_cnt, u8 *buf, int buf_len, int timeout)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;
	u8 trans_mode;

	dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD %d, arg = 0x%08x\n",
		__func__, cmd->opcode, cmd->arg);

	if (!buf)
		buf_len = 0;

	if (cmd->opcode == MMC_SEND_TUNING_BLOCK)
		trans_mode = SD_TM_AUTO_TUNING;
	else
		trans_mode = SD_TM_NORMAL_READ;

	rtsx_pci_init_cmd(pcr);
	sd_cmd_set_sd_cmd(pcr, cmd);
	sd_cmd_set_data_len(pcr, 1, byte_cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG2, 0xFF,
			SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_6);
	if (trans_mode != SD_TM_AUTO_TUNING)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				CARD_DATA_SOURCE, 0x01, PINGPONG_BUFFER);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_TRANSFER,
			0xFF, trans_mode | SD_TRANSFER_START);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

	err = rtsx_pci_send_cmd(pcr, timeout);
	if (err < 0) {
		sd_print_debug_regs(host);
		dev_dbg(sdmmc_dev(host),
			"rtsx_pci_send_cmd fail (err = %d)\n", err);
		return err;
	}

	if (buf && buf_len) {
		err = rtsx_pci_read_ppbuf(pcr, buf, buf_len);
		if (err < 0) {
			dev_dbg(sdmmc_dev(host),
				"rtsx_pci_read_ppbuf fail (err = %d)\n", err);
			return err;
		}
	}

	return 0;
}

static int sd_write_data(struct realtek_pci_sdmmc *host,
	struct mmc_command *cmd, u16 byte_cnt, u8 *buf, int buf_len,
	int timeout)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;

	dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD %d, arg = 0x%08x\n",
		__func__, cmd->opcode, cmd->arg);

	if (!buf)
		buf_len = 0;

	sd_send_cmd_get_rsp(host, cmd);
	if (cmd->error)
		return cmd->error;

	if (buf && buf_len) {
		err = rtsx_pci_write_ppbuf(pcr, buf, buf_len);
		if (err < 0) {
			dev_dbg(sdmmc_dev(host),
				"rtsx_pci_write_ppbuf fail (err = %d)\n", err);
			return err;
		}
	}

	rtsx_pci_init_cmd(pcr);
	sd_cmd_set_data_len(pcr, 1, byte_cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG2, 0xFF,
		SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
		SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_0);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			SD_TRANSFER_START | SD_TM_AUTO_WRITE_3);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

	err = rtsx_pci_send_cmd(pcr, timeout);
	if (err < 0) {
		sd_print_debug_regs(host);
		dev_dbg(sdmmc_dev(host),
			"rtsx_pci_send_cmd fail (err = %d)\n", err);
		return err;
	}

	return 0;
}

static int sd_read_long_data(struct realtek_pci_sdmmc *host,
	struct mmc_request *mrq)
{
	struct rtsx_pcr *pcr = host->pcr;
	struct mmc_host *mmc = host->mmc;
	struct mmc_card *card = mmc->card;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;
	int uhs = mmc_card_uhs(card);
	u8 cfg2 = 0;
	int err;
	int resp_type;
	size_t data_len = data->blksz * data->blocks;

	dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD %d, arg = 0x%08x\n",
		__func__, cmd->opcode, cmd->arg);

	resp_type = sd_response_type(cmd);
	if (resp_type < 0)
		return resp_type;

	if (!uhs)
		cfg2 |= SD_NO_CHECK_WAIT_CRC_TO;

	rtsx_pci_init_cmd(pcr);
	sd_cmd_set_sd_cmd(pcr, cmd);
	sd_cmd_set_data_len(pcr, data->blocks, data->blksz);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, IRQSTAT0,
			DMA_DONE_INT, DMA_DONE_INT);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC3,
		0xFF, (u8)(data_len >> 24));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC2,
		0xFF, (u8)(data_len >> 16));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC1,
		0xFF, (u8)(data_len >> 8));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC0, 0xFF, (u8)data_len);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMACTL,
		0x03 | DMA_PACK_SIZE_MASK,
		DMA_DIR_FROM_CARD | DMA_EN | DMA_512);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, RING_BUFFER);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG2, 0xFF, cfg2 | resp_type);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			SD_TRANSFER_START | SD_TM_AUTO_READ_2);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);
	rtsx_pci_send_cmd_no_wait(pcr);

	err = rtsx_pci_dma_transfer(pcr, data->sg, host->sg_count, 1, 10000);
	if (err < 0) {
		sd_print_debug_regs(host);
		sd_clear_error(host);
		return err;
	}

	return 0;
}

static int sd_write_long_data(struct realtek_pci_sdmmc *host,
	struct mmc_request *mrq)
{
	struct rtsx_pcr *pcr = host->pcr;
	struct mmc_host *mmc = host->mmc;
	struct mmc_card *card = mmc->card;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;
	int uhs = mmc_card_uhs(card);
	u8 cfg2;
	int err;
	size_t data_len = data->blksz * data->blocks;

	sd_send_cmd_get_rsp(host, cmd);
	if (cmd->error)
		return cmd->error;

	dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD %d, arg = 0x%08x\n",
		__func__, cmd->opcode, cmd->arg);

	cfg2 = SD_NO_CALCULATE_CRC7 | SD_CHECK_CRC16 |
		SD_NO_WAIT_BUSY_END | SD_NO_CHECK_CRC7 | SD_RSP_LEN_0;

	if (!uhs)
		cfg2 |= SD_NO_CHECK_WAIT_CRC_TO;

	rtsx_pci_init_cmd(pcr);
	sd_cmd_set_data_len(pcr, data->blocks, data->blksz);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, IRQSTAT0,
			DMA_DONE_INT, DMA_DONE_INT);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC3,
		0xFF, (u8)(data_len >> 24));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC2,
		0xFF, (u8)(data_len >> 16));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC1,
		0xFF, (u8)(data_len >> 8));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC0, 0xFF, (u8)data_len);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMACTL,
		0x03 | DMA_PACK_SIZE_MASK,
		DMA_DIR_TO_CARD | DMA_EN | DMA_512);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, RING_BUFFER);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG2, 0xFF, cfg2);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			SD_TRANSFER_START | SD_TM_AUTO_WRITE_3);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);
	rtsx_pci_send_cmd_no_wait(pcr);
	err = rtsx_pci_dma_transfer(pcr, data->sg, host->sg_count, 0, 10000);
	if (err < 0) {
		sd_clear_error(host);
		return err;
	}

	return 0;
}

static int sd_rw_multi(struct realtek_pci_sdmmc *host, struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;

	if (host->sg_count < 0) {
		data->error = host->sg_count;
		dev_dbg(sdmmc_dev(host), "%s: sg_count = %d is invalid\n",
			__func__, host->sg_count);
		return data->error;
	}

	if (data->flags & MMC_DATA_READ)
		return sd_read_long_data(host, mrq);

	return sd_write_long_data(host, mrq);
}

static inline void sd_enable_initial_mode(struct realtek_pci_sdmmc *host)
{
	rtsx_pci_write_register(host->pcr, SD_CFG1,
			SD_CLK_DIVIDE_MASK, SD_CLK_DIVIDE_128);
}

static inline void sd_disable_initial_mode(struct realtek_pci_sdmmc *host)
{
	rtsx_pci_write_register(host->pcr, SD_CFG1,
			SD_CLK_DIVIDE_MASK, SD_CLK_DIVIDE_0);
}

static void sd_normal_rw(struct realtek_pci_sdmmc *host,
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

static int sd_change_phase(struct realtek_pci_sdmmc *host,
		u8 sample_point, bool rx)
{
	struct rtsx_pcr *pcr = host->pcr;

	dev_dbg(sdmmc_dev(host), "%s(%s): sample_point = %d\n",
			__func__, rx ? "RX" : "TX", sample_point);

	rtsx_pci_write_register(pcr, CLK_CTL, CHANGE_CLK, CHANGE_CLK);
	if (rx)
		rtsx_pci_write_register(pcr, SD_VPRX_CTL,
			PHASE_SELECT_MASK, sample_point);
	else
		rtsx_pci_write_register(pcr, SD_VPTX_CTL,
			PHASE_SELECT_MASK, sample_point);
	rtsx_pci_write_register(pcr, SD_VPCLK0_CTL, PHASE_NOT_RESET, 0);
	rtsx_pci_write_register(pcr, SD_VPCLK0_CTL, PHASE_NOT_RESET,
				PHASE_NOT_RESET);
	rtsx_pci_write_register(pcr, CLK_CTL, CHANGE_CLK, 0);
	rtsx_pci_write_register(pcr, SD_CFG1, SD_ASYNC_FIFO_NOT_RST, 0);

	return 0;
}

static inline u32 test_phase_bit(u32 phase_map, unsigned int bit)
{
	bit %= RTSX_PHASE_MAX;
	return phase_map & (1 << bit);
}

static int sd_get_phase_len(u32 phase_map, unsigned int start_bit)
{
	int i;

	for (i = 0; i < RTSX_PHASE_MAX; i++) {
		if (test_phase_bit(phase_map, start_bit + i) == 0)
			return i;
	}
	return RTSX_PHASE_MAX;
}

static u8 sd_search_final_phase(struct realtek_pci_sdmmc *host, u32 phase_map)
{
	int start = 0, len = 0;
	int start_final = 0, len_final = 0;
	u8 final_phase = 0xFF;

	if (phase_map == 0) {
		dev_err(sdmmc_dev(host), "phase error: [map:%x]\n", phase_map);
		return final_phase;
	}

	while (start < RTSX_PHASE_MAX) {
		len = sd_get_phase_len(phase_map, start);
		if (len_final < len) {
			start_final = start;
			len_final = len;
		}
		start += len ? len : 1;
	}

	final_phase = (start_final + len_final / 2) % RTSX_PHASE_MAX;
	dev_dbg(sdmmc_dev(host), "phase: [map:%x] [maxlen:%d] [final:%d]\n",
		phase_map, len_final, final_phase);

	return final_phase;
}

static void sd_wait_data_idle(struct realtek_pci_sdmmc *host)
{
	int err, i;
	u8 val = 0;

	for (i = 0; i < 100; i++) {
		err = rtsx_pci_read_register(host->pcr, SD_DATA_STATE, &val);
		if (val & SD_DATA_IDLE)
			return;

		udelay(100);
	}
}

static int sd_tuning_rx_cmd(struct realtek_pci_sdmmc *host,
		u8 opcode, u8 sample_point)
{
	int err;
	struct mmc_command cmd = {};
	struct rtsx_pcr *pcr = host->pcr;

	sd_change_phase(host, sample_point, true);

	rtsx_pci_write_register(pcr, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
		SD_RSP_80CLK_TIMEOUT_EN);

	cmd.opcode = opcode;
	err = sd_read_data(host, &cmd, 0x40, NULL, 0, 100);
	if (err < 0) {
		/* Wait till SD DATA IDLE */
		sd_wait_data_idle(host);
		sd_clear_error(host);
		rtsx_pci_write_register(pcr, SD_CFG3,
			SD_RSP_80CLK_TIMEOUT_EN, 0);
		return err;
	}

	rtsx_pci_write_register(pcr, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN, 0);
	return 0;
}

static int sd_tuning_phase(struct realtek_pci_sdmmc *host,
		u8 opcode, u32 *phase_map)
{
	int err, i;
	u32 raw_phase_map = 0;

	for (i = 0; i < RTSX_PHASE_MAX; i++) {
		err = sd_tuning_rx_cmd(host, opcode, (u8)i);
		if (err == 0)
			raw_phase_map |= 1 << i;
	}

	if (phase_map)
		*phase_map = raw_phase_map;

	return 0;
}

static int sd_tuning_rx(struct realtek_pci_sdmmc *host, u8 opcode)
{
	int err, i;
	u32 raw_phase_map[RX_TUNING_CNT] = {0}, phase_map;
	u8 final_phase;

	for (i = 0; i < RX_TUNING_CNT; i++) {
		err = sd_tuning_phase(host, opcode, &(raw_phase_map[i]));
		if (err < 0)
			return err;

		if (raw_phase_map[i] == 0)
			break;
	}

	phase_map = 0xFFFFFFFF;
	for (i = 0; i < RX_TUNING_CNT; i++) {
		dev_dbg(sdmmc_dev(host), "RX raw_phase_map[%d] = 0x%08x\n",
				i, raw_phase_map[i]);
		phase_map &= raw_phase_map[i];
	}
	dev_dbg(sdmmc_dev(host), "RX phase_map = 0x%08x\n", phase_map);

	if (phase_map) {
		final_phase = sd_search_final_phase(host, phase_map);
		if (final_phase == 0xFF)
			return -EINVAL;

		err = sd_change_phase(host, final_phase, true);
		if (err < 0)
			return err;
	} else {
		return -EINVAL;
	}

	return 0;
}

static inline int sdio_extblock_cmd(struct mmc_command *cmd,
	struct mmc_data *data)
{
	return (cmd->opcode == SD_IO_RW_EXTENDED) && (data->blksz == 512);
}

static inline int sd_rw_cmd(struct mmc_command *cmd)
{
	return mmc_op_multi(cmd->opcode) ||
		(cmd->opcode == MMC_READ_SINGLE_BLOCK) ||
		(cmd->opcode == MMC_WRITE_BLOCK);
}

static void sd_request(struct work_struct *work)
{
	struct realtek_pci_sdmmc *host = container_of(work,
			struct realtek_pci_sdmmc, work);
	struct rtsx_pcr *pcr = host->pcr;

	struct mmc_host *mmc = host->mmc;
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;

	unsigned int data_size = 0;
	int err;

	if (host->eject || !sd_get_cd_int(host)) {
		cmd->error = -ENOMEDIUM;
		goto finish;
	}

	err = rtsx_pci_card_exclusive_check(host->pcr, RTSX_SD_CARD);
	if (err) {
		cmd->error = err;
		goto finish;
	}

	mutex_lock(&pcr->pcr_mutex);

	rtsx_pci_start_run(pcr);

	rtsx_pci_switch_clock(pcr, host->clock, host->ssc_depth,
			host->initial_mode, host->double_clk, host->vpclk);
	rtsx_pci_write_register(pcr, CARD_SELECT, 0x07, SD_MOD_SEL);
	rtsx_pci_write_register(pcr, CARD_SHARE_MODE,
			CARD_SHARE_MASK, CARD_SHARE_48_SD);

	mutex_lock(&host->host_mutex);
	host->mrq = mrq;
	mutex_unlock(&host->host_mutex);

	if (mrq->data)
		data_size = data->blocks * data->blksz;

	if (!data_size) {
		sd_send_cmd_get_rsp(host, cmd);
	} else if (sd_rw_cmd(cmd) || sdio_extblock_cmd(cmd, data)) {
		cmd->error = sd_rw_multi(host, mrq);
		if (!host->using_cookie)
			sdmmc_post_req(host->mmc, host->mrq, 0);

		if (mmc_op_multi(cmd->opcode) && mrq->stop)
			sd_send_cmd_get_rsp(host, mrq->stop);
	} else {
		sd_normal_rw(host, mrq);
	}

	if (mrq->data) {
		if (cmd->error || data->error)
			data->bytes_xfered = 0;
		else
			data->bytes_xfered = data->blocks * data->blksz;
	}

	mutex_unlock(&pcr->pcr_mutex);

finish:
	if (cmd->error) {
		dev_dbg(sdmmc_dev(host), "CMD %d 0x%08x error(%d)\n",
			cmd->opcode, cmd->arg, cmd->error);
	}

	mutex_lock(&host->host_mutex);
	host->mrq = NULL;
	mutex_unlock(&host->host_mutex);

	mmc_request_done(mmc, mrq);
}

static void sdmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	mutex_lock(&host->host_mutex);
	host->mrq = mrq;
	mutex_unlock(&host->host_mutex);

	if (sd_rw_cmd(mrq->cmd) || sdio_extblock_cmd(mrq->cmd, data))
		host->using_cookie = sd_pre_dma_transfer(host, data, false);

	schedule_work(&host->work);
}

static int sd_set_bus_width(struct realtek_pci_sdmmc *host,
		unsigned char bus_width)
{
	int err = 0;
	u8 width[] = {
		[MMC_BUS_WIDTH_1] = SD_BUS_WIDTH_1BIT,
		[MMC_BUS_WIDTH_4] = SD_BUS_WIDTH_4BIT,
		[MMC_BUS_WIDTH_8] = SD_BUS_WIDTH_8BIT,
	};

	if (bus_width <= MMC_BUS_WIDTH_8)
		err = rtsx_pci_write_register(host->pcr, SD_CFG1,
				0x03, width[bus_width]);

	return err;
}

static int sd_power_on(struct realtek_pci_sdmmc *host)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;

	if (host->power_state == SDMMC_POWER_ON)
		return 0;

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_SELECT, 0x07, SD_MOD_SEL);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_SHARE_MODE,
			CARD_SHARE_MASK, CARD_SHARE_48_SD);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_CLK_EN,
			SD_CLK_EN, SD_CLK_EN);
	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	err = rtsx_pci_card_pull_ctl_enable(pcr, RTSX_SD_CARD);
	if (err < 0)
		return err;

	err = rtsx_pci_card_power_on(pcr, RTSX_SD_CARD);
	if (err < 0)
		return err;

	err = rtsx_pci_write_register(pcr, CARD_OE, SD_OUTPUT_EN, SD_OUTPUT_EN);
	if (err < 0)
		return err;

	host->power_state = SDMMC_POWER_ON;
	return 0;
}

static int sd_power_off(struct realtek_pci_sdmmc *host)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;

	host->power_state = SDMMC_POWER_OFF;

	rtsx_pci_init_cmd(pcr);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_CLK_EN, SD_CLK_EN, 0);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_OE, SD_OUTPUT_EN, 0);

	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	err = rtsx_pci_card_power_off(pcr, RTSX_SD_CARD);
	if (err < 0)
		return err;

	return rtsx_pci_card_pull_ctl_disable(pcr, RTSX_SD_CARD);
}

static int sd_set_power_mode(struct realtek_pci_sdmmc *host,
		unsigned char power_mode)
{
	int err;

	if (power_mode == MMC_POWER_OFF)
		err = sd_power_off(host);
	else
		err = sd_power_on(host);

	return err;
}

static int sd_set_timing(struct realtek_pci_sdmmc *host, unsigned char timing)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err = 0;

	rtsx_pci_init_cmd(pcr);

	switch (timing) {
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_UHS_SDR50:
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG1,
				0x0C | SD_ASYNC_FIFO_NOT_RST,
				SD_30_MODE | SD_ASYNC_FIFO_NOT_RST);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL,
				CLK_LOW_FREQ, CLK_LOW_FREQ);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
				CRC_VAR_CLK0 | SD30_FIX_CLK | SAMPLE_VAR_CLK1);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL, CLK_LOW_FREQ, 0);
		break;

	case MMC_TIMING_MMC_DDR52:
	case MMC_TIMING_UHS_DDR50:
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG1,
				0x0C | SD_ASYNC_FIFO_NOT_RST,
				SD_DDR_MODE | SD_ASYNC_FIFO_NOT_RST);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL,
				CLK_LOW_FREQ, CLK_LOW_FREQ);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
				CRC_VAR_CLK0 | SD30_FIX_CLK | SAMPLE_VAR_CLK1);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL, CLK_LOW_FREQ, 0);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_PUSH_POINT_CTL,
				DDR_VAR_TX_CMD_DAT, DDR_VAR_TX_CMD_DAT);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL,
				DDR_VAR_RX_DAT | DDR_VAR_RX_CMD,
				DDR_VAR_RX_DAT | DDR_VAR_RX_CMD);
		break;

	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG1,
				0x0C, SD_20_MODE);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL,
				CLK_LOW_FREQ, CLK_LOW_FREQ);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
				CRC_FIX_CLK | SD30_VAR_CLK0 | SAMPLE_VAR_CLK1);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL, CLK_LOW_FREQ, 0);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_PUSH_POINT_CTL,
				SD20_TX_SEL_MASK, SD20_TX_14_AHEAD);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL,
				SD20_RX_SEL_MASK, SD20_RX_14_DELAY);
		break;

	default:
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				SD_CFG1, 0x0C, SD_20_MODE);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL,
				CLK_LOW_FREQ, CLK_LOW_FREQ);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
				CRC_FIX_CLK | SD30_VAR_CLK0 | SAMPLE_VAR_CLK1);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL, CLK_LOW_FREQ, 0);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				SD_PUSH_POINT_CTL, 0xFF, 0);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL,
				SD20_RX_SEL_MASK, SD20_RX_POS_EDGE);
		break;
	}

	err = rtsx_pci_send_cmd(pcr, 100);

	return err;
}

static void sdmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct rtsx_pcr *pcr = host->pcr;

	if (host->eject)
		return;

	if (rtsx_pci_card_exclusive_check(host->pcr, RTSX_SD_CARD))
		return;

	mutex_lock(&pcr->pcr_mutex);

	rtsx_pci_start_run(pcr);

	sd_set_bus_width(host, ios->bus_width);
	sd_set_power_mode(host, ios->power_mode);
	sd_set_timing(host, ios->timing);

	host->vpclk = false;
	host->double_clk = true;

	switch (ios->timing) {
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_UHS_SDR50:
		host->ssc_depth = RTSX_SSC_DEPTH_2M;
		host->vpclk = true;
		host->double_clk = false;
		break;
	case MMC_TIMING_MMC_DDR52:
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_UHS_SDR25:
		host->ssc_depth = RTSX_SSC_DEPTH_1M;
		break;
	default:
		host->ssc_depth = RTSX_SSC_DEPTH_500K;
		break;
	}

	host->initial_mode = (ios->clock <= 1000000) ? true : false;

	host->clock = ios->clock;
	rtsx_pci_switch_clock(pcr, ios->clock, host->ssc_depth,
			host->initial_mode, host->double_clk, host->vpclk);

	mutex_unlock(&pcr->pcr_mutex);
}

static int sdmmc_get_ro(struct mmc_host *mmc)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct rtsx_pcr *pcr = host->pcr;
	int ro = 0;
	u32 val;

	if (host->eject)
		return -ENOMEDIUM;

	mutex_lock(&pcr->pcr_mutex);

	rtsx_pci_start_run(pcr);

	/* Check SD mechanical write-protect switch */
	val = rtsx_pci_readl(pcr, RTSX_BIPR);
	dev_dbg(sdmmc_dev(host), "%s: RTSX_BIPR = 0x%08x\n", __func__, val);
	if (val & SD_WRITE_PROTECT)
		ro = 1;

	mutex_unlock(&pcr->pcr_mutex);

	return ro;
}

static int sdmmc_get_cd(struct mmc_host *mmc)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct rtsx_pcr *pcr = host->pcr;
	int cd = 0;
	u32 val;

	if (host->eject)
		return cd;

	mutex_lock(&pcr->pcr_mutex);

	rtsx_pci_start_run(pcr);

	/* Check SD card detect */
	val = rtsx_pci_card_exist(pcr);
	dev_dbg(sdmmc_dev(host), "%s: RTSX_BIPR = 0x%08x\n", __func__, val);
	if (val & SD_EXIST)
		cd = 1;

	mutex_unlock(&pcr->pcr_mutex);

	return cd;
}

static int sd_wait_voltage_stable_1(struct realtek_pci_sdmmc *host)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;
	u8 stat;

	/* Reference to Signal Voltage Switch Sequence in SD spec.
	 * Wait for a period of time so that the card can drive SD_CMD and
	 * SD_DAT[3:0] to low after sending back CMD11 response.
	 */
	mdelay(1);

	/* SD_CMD, SD_DAT[3:0] should be driven to low by card;
	 * If either one of SD_CMD,SD_DAT[3:0] is not low,
	 * abort the voltage switch sequence;
	 */
	err = rtsx_pci_read_register(pcr, SD_BUS_STAT, &stat);
	if (err < 0)
		return err;

	if (stat & (SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
				SD_DAT1_STATUS | SD_DAT0_STATUS))
		return -EINVAL;

	/* Stop toggle SD clock */
	err = rtsx_pci_write_register(pcr, SD_BUS_STAT,
			0xFF, SD_CLK_FORCE_STOP);
	if (err < 0)
		return err;

	return 0;
}

static int sd_wait_voltage_stable_2(struct realtek_pci_sdmmc *host)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;
	u8 stat, mask, val;

	/* Wait 1.8V output of voltage regulator in card stable */
	msleep(50);

	/* Toggle SD clock again */
	err = rtsx_pci_write_register(pcr, SD_BUS_STAT, 0xFF, SD_CLK_TOGGLE_EN);
	if (err < 0)
		return err;

	/* Wait for a period of time so that the card can drive
	 * SD_DAT[3:0] to high at 1.8V
	 */
	msleep(20);

	/* SD_CMD, SD_DAT[3:0] should be pulled high by host */
	err = rtsx_pci_read_register(pcr, SD_BUS_STAT, &stat);
	if (err < 0)
		return err;

	mask = SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
		SD_DAT1_STATUS | SD_DAT0_STATUS;
	val = SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
		SD_DAT1_STATUS | SD_DAT0_STATUS;
	if ((stat & mask) != val) {
		dev_dbg(sdmmc_dev(host),
			"%s: SD_BUS_STAT = 0x%x\n", __func__, stat);
		rtsx_pci_write_register(pcr, SD_BUS_STAT,
				SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP, 0);
		rtsx_pci_write_register(pcr, CARD_CLK_EN, 0xFF, 0);
		return -EINVAL;
	}

	return 0;
}

static int sdmmc_switch_voltage(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct rtsx_pcr *pcr = host->pcr;
	int err = 0;
	u8 voltage;

	dev_dbg(sdmmc_dev(host), "%s: signal_voltage = %d\n",
			__func__, ios->signal_voltage);

	if (host->eject)
		return -ENOMEDIUM;

	err = rtsx_pci_card_exclusive_check(host->pcr, RTSX_SD_CARD);
	if (err)
		return err;

	mutex_lock(&pcr->pcr_mutex);

	rtsx_pci_start_run(pcr);

	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330)
		voltage = OUTPUT_3V3;
	else
		voltage = OUTPUT_1V8;

	if (voltage == OUTPUT_1V8) {
		err = sd_wait_voltage_stable_1(host);
		if (err < 0)
			goto out;
	}

	err = rtsx_pci_switch_output_voltage(pcr, voltage);
	if (err < 0)
		goto out;

	if (voltage == OUTPUT_1V8) {
		err = sd_wait_voltage_stable_2(host);
		if (err < 0)
			goto out;
	}

out:
	/* Stop toggle SD clock in idle */
	err = rtsx_pci_write_register(pcr, SD_BUS_STAT,
			SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP, 0);

	mutex_unlock(&pcr->pcr_mutex);

	return err;
}

static int sdmmc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct rtsx_pcr *pcr = host->pcr;
	int err = 0;

	if (host->eject)
		return -ENOMEDIUM;

	err = rtsx_pci_card_exclusive_check(host->pcr, RTSX_SD_CARD);
	if (err)
		return err;

	mutex_lock(&pcr->pcr_mutex);

	rtsx_pci_start_run(pcr);

	/* Set initial TX phase */
	switch (mmc->ios.timing) {
	case MMC_TIMING_UHS_SDR104:
		err = sd_change_phase(host, SDR104_TX_PHASE(pcr), false);
		break;

	case MMC_TIMING_UHS_SDR50:
		err = sd_change_phase(host, SDR50_TX_PHASE(pcr), false);
		break;

	case MMC_TIMING_UHS_DDR50:
		err = sd_change_phase(host, DDR50_TX_PHASE(pcr), false);
		break;

	default:
		err = 0;
	}

	if (err)
		goto out;

	/* Tuning RX phase */
	if ((mmc->ios.timing == MMC_TIMING_UHS_SDR104) ||
			(mmc->ios.timing == MMC_TIMING_UHS_SDR50))
		err = sd_tuning_rx(host, opcode);
	else if (mmc->ios.timing == MMC_TIMING_UHS_DDR50)
		err = sd_change_phase(host, DDR50_RX_PHASE(pcr), true);

out:
	mutex_unlock(&pcr->pcr_mutex);

	return err;
}

static const struct mmc_host_ops realtek_pci_sdmmc_ops = {
	.pre_req = sdmmc_pre_req,
	.post_req = sdmmc_post_req,
	.request = sdmmc_request,
	.set_ios = sdmmc_set_ios,
	.get_ro = sdmmc_get_ro,
	.get_cd = sdmmc_get_cd,
	.start_signal_voltage_switch = sdmmc_switch_voltage,
	.execute_tuning = sdmmc_execute_tuning,
};

static void init_extra_caps(struct realtek_pci_sdmmc *host)
{
	struct mmc_host *mmc = host->mmc;
	struct rtsx_pcr *pcr = host->pcr;

	dev_dbg(sdmmc_dev(host), "pcr->extra_caps = 0x%x\n", pcr->extra_caps);

	if (pcr->extra_caps & EXTRA_CAPS_SD_SDR50)
		mmc->caps |= MMC_CAP_UHS_SDR50;
	if (pcr->extra_caps & EXTRA_CAPS_SD_SDR104)
		mmc->caps |= MMC_CAP_UHS_SDR104;
	if (pcr->extra_caps & EXTRA_CAPS_SD_DDR50)
		mmc->caps |= MMC_CAP_UHS_DDR50;
	if (pcr->extra_caps & EXTRA_CAPS_MMC_HSDDR)
		mmc->caps |= MMC_CAP_1_8V_DDR;
	if (pcr->extra_caps & EXTRA_CAPS_MMC_8BIT)
		mmc->caps |= MMC_CAP_8_BIT_DATA;
}

static void realtek_init_host(struct realtek_pci_sdmmc *host)
{
	struct mmc_host *mmc = host->mmc;

	mmc->f_min = 250000;
	mmc->f_max = 208000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SD_HIGHSPEED |
		MMC_CAP_MMC_HIGHSPEED | MMC_CAP_BUS_WIDTH_TEST |
		MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_ERASE;
	mmc->caps2 = MMC_CAP2_NO_PRESCAN_POWERUP | MMC_CAP2_FULL_PWR_CYCLE;
	mmc->max_current_330 = 400;
	mmc->max_current_180 = 800;
	mmc->ops = &realtek_pci_sdmmc_ops;

	init_extra_caps(host);

	mmc->max_segs = 256;
	mmc->max_seg_size = 65536;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = 65535;
	mmc->max_req_size = 524288;
}

static void rtsx_pci_sdmmc_card_event(struct platform_device *pdev)
{
	struct realtek_pci_sdmmc *host = platform_get_drvdata(pdev);

	host->cookie = -1;
	mmc_detect_change(host->mmc, 0);
}

static int rtsx_pci_sdmmc_drv_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct realtek_pci_sdmmc *host;
	struct rtsx_pcr *pcr;
	struct pcr_handle *handle = pdev->dev.platform_data;

	if (!handle)
		return -ENXIO;

	pcr = handle->pcr;
	if (!pcr)
		return -ENXIO;

	dev_dbg(&(pdev->dev), ": Realtek PCI-E SDMMC controller found\n");

	mmc = mmc_alloc_host(sizeof(*host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->pcr = pcr;
	host->mmc = mmc;
	host->pdev = pdev;
	host->cookie = -1;
	host->power_state = SDMMC_POWER_OFF;
	INIT_WORK(&host->work, sd_request);
	platform_set_drvdata(pdev, host);
	pcr->slots[RTSX_SD_CARD].p_dev = pdev;
	pcr->slots[RTSX_SD_CARD].card_event = rtsx_pci_sdmmc_card_event;

	mutex_init(&host->host_mutex);

	realtek_init_host(host);

	mmc_add_host(mmc);

	return 0;
}

static int rtsx_pci_sdmmc_drv_remove(struct platform_device *pdev)
{
	struct realtek_pci_sdmmc *host = platform_get_drvdata(pdev);
	struct rtsx_pcr *pcr;
	struct mmc_host *mmc;

	if (!host)
		return 0;

	pcr = host->pcr;
	pcr->slots[RTSX_SD_CARD].p_dev = NULL;
	pcr->slots[RTSX_SD_CARD].card_event = NULL;
	mmc = host->mmc;

	cancel_work_sync(&host->work);

	mutex_lock(&host->host_mutex);
	if (host->mrq) {
		dev_dbg(&(pdev->dev),
			"%s: Controller removed during transfer\n",
			mmc_hostname(mmc));

		rtsx_pci_complete_unfinished_transfer(pcr);

		host->mrq->cmd->error = -ENOMEDIUM;
		if (host->mrq->stop)
			host->mrq->stop->error = -ENOMEDIUM;
		mmc_request_done(mmc, host->mrq);
	}
	mutex_unlock(&host->host_mutex);

	mmc_remove_host(mmc);
	host->eject = true;

	flush_work(&host->work);

	mmc_free_host(mmc);

	dev_dbg(&(pdev->dev),
		": Realtek PCI-E SDMMC controller has been removed\n");

	return 0;
}

static const struct platform_device_id rtsx_pci_sdmmc_ids[] = {
	{
		.name = DRV_NAME_RTSX_PCI_SDMMC,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, rtsx_pci_sdmmc_ids);

static struct platform_driver rtsx_pci_sdmmc_driver = {
	.probe		= rtsx_pci_sdmmc_drv_probe,
	.remove		= rtsx_pci_sdmmc_drv_remove,
	.id_table       = rtsx_pci_sdmmc_ids,
	.driver		= {
		.name	= DRV_NAME_RTSX_PCI_SDMMC,
	},
};
module_platform_driver(rtsx_pci_sdmmc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wei WANG <wei_wang@realsil.com.cn>");
MODULE_DESCRIPTION("Realtek PCI-E SD/MMC Card Host Driver");
