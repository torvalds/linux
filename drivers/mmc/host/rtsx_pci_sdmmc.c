/* Realtek PCI-Express SD/MMC Card Interface driver
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
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
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/card.h>
#include <linux/mfd/rtsx_pci.h>
#include <asm/unaligned.h>

/* SD Tuning Data Structure
 * Record continuous timing phase path
 */
struct timing_phase_path {
	int start;
	int end;
	int mid;
	int len;
};

struct realtek_pci_sdmmc {
	struct platform_device	*pdev;
	struct rtsx_pcr		*pcr;
	struct mmc_host		*mmc;
	struct mmc_request	*mrq;

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
static void sd_print_debug_regs(struct realtek_pci_sdmmc *host)
{
	struct rtsx_pcr *pcr = host->pcr;
	u16 i;
	u8 *ptr;

	/* Print SD host internal registers */
	rtsx_pci_init_cmd(pcr);
	for (i = 0xFDA0; i <= 0xFDAE; i++)
		rtsx_pci_add_cmd(pcr, READ_REG_CMD, i, 0, 0);
	for (i = 0xFD52; i <= 0xFD69; i++)
		rtsx_pci_add_cmd(pcr, READ_REG_CMD, i, 0, 0);
	rtsx_pci_send_cmd(pcr, 100);

	ptr = rtsx_pci_get_cmd_data(pcr);
	for (i = 0xFDA0; i <= 0xFDAE; i++)
		dev_dbg(sdmmc_dev(host), "0x%04X: 0x%02x\n", i, *(ptr++));
	for (i = 0xFD52; i <= 0xFD69; i++)
		dev_dbg(sdmmc_dev(host), "0x%04X: 0x%02x\n", i, *(ptr++));
}
#else
#define sd_print_debug_regs(host)
#endif /* DEBUG */

static int sd_read_data(struct realtek_pci_sdmmc *host, u8 *cmd, u16 byte_cnt,
		u8 *buf, int buf_len, int timeout)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err, i;
	u8 trans_mode;

	dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD%d\n", __func__, cmd[0] - 0x40);

	if (!buf)
		buf_len = 0;

	if ((cmd[0] & 0x3F) == MMC_SEND_TUNING_BLOCK)
		trans_mode = SD_TM_AUTO_TUNING;
	else
		trans_mode = SD_TM_NORMAL_READ;

	rtsx_pci_init_cmd(pcr);

	for (i = 0; i < 5; i++)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CMD0 + i, 0xFF, cmd[i]);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, (u8)byte_cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BYTE_CNT_H,
			0xFF, (u8)(byte_cnt >> 8));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, 1);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF, 0);

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

static int sd_write_data(struct realtek_pci_sdmmc *host, u8 *cmd, u16 byte_cnt,
		u8 *buf, int buf_len, int timeout)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err, i;
	u8 trans_mode;

	if (!buf)
		buf_len = 0;

	if (buf && buf_len) {
		err = rtsx_pci_write_ppbuf(pcr, buf, buf_len);
		if (err < 0) {
			dev_dbg(sdmmc_dev(host),
				"rtsx_pci_write_ppbuf fail (err = %d)\n", err);
			return err;
		}
	}

	trans_mode = cmd ? SD_TM_AUTO_WRITE_2 : SD_TM_AUTO_WRITE_3;
	rtsx_pci_init_cmd(pcr);

	if (cmd) {
		dev_dbg(sdmmc_dev(host), "%s: SD/MMC CMD %d\n", __func__,
				cmd[0] - 0x40);

		for (i = 0; i < 5; i++)
			rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
					SD_CMD0 + i, 0xFF, cmd[i]);
	}

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, (u8)byte_cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BYTE_CNT_H,
			0xFF, (u8)(byte_cnt >> 8));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, 1);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF, 0);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG2, 0xFF,
		SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
		SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_6);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			trans_mode | SD_TRANSFER_START);
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
	int stat_idx = 0;
	u8 rsp_type;
	int rsp_len = 5;

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
		rsp_len = 0;
		break;
	case MMC_RSP_R1:
		rsp_type = SD_RSP_TYPE_R1;
		break;
	case MMC_RSP_R1B:
		rsp_type = SD_RSP_TYPE_R1b;
		break;
	case MMC_RSP_R2:
		rsp_type = SD_RSP_TYPE_R2;
		rsp_len = 16;
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
		err = rtsx_pci_write_register(pcr, SD_BUS_STAT,
				0xFF, SD_CLK_TOGGLE_EN);
		if (err < 0)
			goto out;
	}

	rtsx_pci_init_cmd(pcr);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CMD0, 0xFF, 0x40 | cmd_idx);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CMD1, 0xFF, (u8)(arg >> 24));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CMD2, 0xFF, (u8)(arg >> 16));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CMD3, 0xFF, (u8)(arg >> 8));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CMD4, 0xFF, (u8)arg);

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
		stat_idx = 16;
	} else if (rsp_type != SD_RSP_TYPE_R0) {
		/* Read data from SD_CMDx registers */
		for (i = SD_CMD0; i <= SD_CMD4; i++)
			rtsx_pci_add_cmd(pcr, READ_REG_CMD, (u16)i, 0, 0);
		stat_idx = 5;
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

static int sd_rw_multi(struct realtek_pci_sdmmc *host, struct mmc_request *mrq)
{
	struct rtsx_pcr *pcr = host->pcr;
	struct mmc_host *mmc = host->mmc;
	struct mmc_card *card = mmc->card;
	struct mmc_data *data = mrq->data;
	int uhs = mmc_sd_card_uhs(card);
	int read = (data->flags & MMC_DATA_READ) ? 1 : 0;
	u8 cfg2, trans_mode;
	int err;
	size_t data_len = data->blksz * data->blocks;

	if (read) {
		cfg2 = SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_0;
		trans_mode = SD_TM_AUTO_READ_3;
	} else {
		cfg2 = SD_NO_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			SD_NO_WAIT_BUSY_END | SD_NO_CHECK_CRC7 | SD_RSP_LEN_0;
		trans_mode = SD_TM_AUTO_WRITE_3;
	}

	if (!uhs)
		cfg2 |= SD_NO_CHECK_WAIT_CRC_TO;

	rtsx_pci_init_cmd(pcr);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0x00);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF, 0x02);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BLOCK_CNT_L,
			0xFF, (u8)data->blocks);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_BLOCK_CNT_H,
			0xFF, (u8)(data->blocks >> 8));

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, IRQSTAT0,
			DMA_DONE_INT, DMA_DONE_INT);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC3,
			0xFF, (u8)(data_len >> 24));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC2,
			0xFF, (u8)(data_len >> 16));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC1,
			0xFF, (u8)(data_len >> 8));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC0, 0xFF, (u8)data_len);
	if (read) {
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMACTL,
				0x03 | DMA_PACK_SIZE_MASK,
				DMA_DIR_FROM_CARD | DMA_EN | DMA_512);
	} else {
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMACTL,
				0x03 | DMA_PACK_SIZE_MASK,
				DMA_DIR_TO_CARD | DMA_EN | DMA_512);
	}

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, RING_BUFFER);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG2, 0xFF, cfg2);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			trans_mode | SD_TRANSFER_START);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

	rtsx_pci_send_cmd_no_wait(pcr);

	err = rtsx_pci_transfer_data(pcr, data->sg, data->sg_len, read, 10000);
	if (err < 0) {
		sd_clear_error(host);
		return err;
	}

	return 0;
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
	u8 _cmd[5], *buf;

	_cmd[0] = 0x40 | (u8)cmd->opcode;
	put_unaligned_be32(cmd->arg, (u32 *)(&_cmd[1]));

	buf = kzalloc(data->blksz, GFP_NOIO);
	if (!buf) {
		cmd->error = -ENOMEM;
		return;
	}

	if (data->flags & MMC_DATA_READ) {
		if (host->initial_mode)
			sd_disable_initial_mode(host);

		cmd->error = sd_read_data(host, _cmd, (u16)data->blksz, buf,
				data->blksz, 200);

		if (host->initial_mode)
			sd_enable_initial_mode(host);

		sg_copy_from_buffer(data->sg, data->sg_len, buf, data->blksz);
	} else {
		sg_copy_to_buffer(data->sg, data->sg_len, buf, data->blksz);

		cmd->error = sd_write_data(host, _cmd, (u16)data->blksz, buf,
				data->blksz, 200);
	}

	kfree(buf);
}

static int sd_change_phase(struct realtek_pci_sdmmc *host,
		u8 sample_point, bool rx)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;

	dev_dbg(sdmmc_dev(host), "%s(%s): sample_point = %d\n",
			__func__, rx ? "RX" : "TX", sample_point);

	rtsx_pci_init_cmd(pcr);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL, CHANGE_CLK, CHANGE_CLK);
	if (rx)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				SD_VPRX_CTL, 0x1F, sample_point);
	else
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				SD_VPTX_CTL, 0x1F, sample_point);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_VPCLK0_CTL, PHASE_NOT_RESET, 0);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_VPCLK0_CTL,
			PHASE_NOT_RESET, PHASE_NOT_RESET);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL, CHANGE_CLK, 0);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_CFG1, SD_ASYNC_FIFO_NOT_RST, 0);

	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	return 0;
}

static u8 sd_search_final_phase(struct realtek_pci_sdmmc *host, u32 phase_map)
{
	struct timing_phase_path path[MAX_PHASE + 1];
	int i, j, cont_path_cnt;
	int new_block, max_len, final_path_idx;
	u8 final_phase = 0xFF;

	/* Parse phase_map, take it as a bit-ring */
	cont_path_cnt = 0;
	new_block = 1;
	j = 0;
	for (i = 0; i < MAX_PHASE + 1; i++) {
		if (phase_map & (1 << i)) {
			if (new_block) {
				new_block = 0;
				j = cont_path_cnt++;
				path[j].start = i;
				path[j].end = i;
			} else {
				path[j].end = i;
			}
		} else {
			new_block = 1;
			if (cont_path_cnt) {
				/* Calculate path length and middle point */
				int idx = cont_path_cnt - 1;
				path[idx].len =
					path[idx].end - path[idx].start + 1;
				path[idx].mid =
					path[idx].start + path[idx].len / 2;
			}
		}
	}

	if (cont_path_cnt == 0) {
		dev_dbg(sdmmc_dev(host), "No continuous phase path\n");
		goto finish;
	} else {
		/* Calculate last continuous path length and middle point */
		int idx = cont_path_cnt - 1;
		path[idx].len = path[idx].end - path[idx].start + 1;
		path[idx].mid = path[idx].start + path[idx].len / 2;
	}

	/* Connect the first and last continuous paths if they are adjacent */
	if (!path[0].start && (path[cont_path_cnt - 1].end == MAX_PHASE)) {
		/* Using negative index */
		path[0].start = path[cont_path_cnt - 1].start - MAX_PHASE - 1;
		path[0].len += path[cont_path_cnt - 1].len;
		path[0].mid = path[0].start + path[0].len / 2;
		/* Convert negative middle point index to positive one */
		if (path[0].mid < 0)
			path[0].mid += MAX_PHASE + 1;
		cont_path_cnt--;
	}

	/* Choose the longest continuous phase path */
	max_len = 0;
	final_phase = 0;
	final_path_idx = 0;
	for (i = 0; i < cont_path_cnt; i++) {
		if (path[i].len > max_len) {
			max_len = path[i].len;
			final_phase = (u8)path[i].mid;
			final_path_idx = i;
		}

		dev_dbg(sdmmc_dev(host), "path[%d].start = %d\n",
				i, path[i].start);
		dev_dbg(sdmmc_dev(host), "path[%d].end = %d\n",
				i, path[i].end);
		dev_dbg(sdmmc_dev(host), "path[%d].len = %d\n",
				i, path[i].len);
		dev_dbg(sdmmc_dev(host), "path[%d].mid = %d\n",
				i, path[i].mid);
	}

finish:
	dev_dbg(sdmmc_dev(host), "Final chosen phase: %d\n", final_phase);
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
	u8 cmd[5] = {0};

	err = sd_change_phase(host, sample_point, true);
	if (err < 0)
		return err;

	cmd[0] = 0x40 | opcode;
	err = sd_read_data(host, cmd, 0x40, NULL, 0, 100);
	if (err < 0) {
		/* Wait till SD DATA IDLE */
		sd_wait_data_idle(host);
		sd_clear_error(host);
		return err;
	}

	return 0;
}

static int sd_tuning_phase(struct realtek_pci_sdmmc *host,
		u8 opcode, u32 *phase_map)
{
	int err, i;
	u32 raw_phase_map = 0;

	for (i = MAX_PHASE; i >= 0; i--) {
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

static void sdmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct realtek_pci_sdmmc *host = mmc_priv(mmc);
	struct rtsx_pcr *pcr = host->pcr;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;
	unsigned int data_size = 0;
	int err;

	if (host->eject) {
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

	if (!data_size || mmc_op_multi(cmd->opcode) ||
			(cmd->opcode == MMC_READ_SINGLE_BLOCK) ||
			(cmd->opcode == MMC_WRITE_BLOCK)) {
		sd_send_cmd_get_rsp(host, cmd);

		if (!cmd->error && data_size) {
			sd_rw_multi(host, mrq);

			if (mmc_op_multi(cmd->opcode) && mrq->stop)
				sd_send_cmd_get_rsp(host, mrq->stop);
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

	mutex_unlock(&pcr->pcr_mutex);

finish:
	if (cmd->error)
		dev_dbg(sdmmc_dev(host), "cmd->error = %d\n", cmd->error);

	mutex_lock(&host->host_mutex);
	host->mrq = NULL;
	mutex_unlock(&host->host_mutex);

	mmc_request_done(mmc, mrq);
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
		return -ENOMEDIUM;

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

	/* Stop toggle SD clock in idle */
	err = rtsx_pci_write_register(pcr, SD_BUS_STAT,
			SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP, 0);

out:
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
	.request = sdmmc_request,
	.set_ios = sdmmc_set_ios,
	.get_ro = sdmmc_get_ro,
	.get_cd = sdmmc_get_cd,
	.start_signal_voltage_switch = sdmmc_switch_voltage,
	.execute_tuning = sdmmc_execute_tuning,
};

#ifdef CONFIG_PM
static int rtsx_pci_sdmmc_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	struct realtek_pci_sdmmc *host = platform_get_drvdata(pdev);
	struct mmc_host *mmc = host->mmc;
	int err;

	dev_dbg(sdmmc_dev(host), "--> %s\n", __func__);

	err = mmc_suspend_host(mmc);
	if (err)
		return err;

	return 0;
}

static int rtsx_pci_sdmmc_resume(struct platform_device *pdev)
{
	struct realtek_pci_sdmmc *host = platform_get_drvdata(pdev);
	struct mmc_host *mmc = host->mmc;

	dev_dbg(sdmmc_dev(host), "--> %s\n", __func__);

	return mmc_resume_host(mmc);
}
#else /* CONFIG_PM */
#define rtsx_pci_sdmmc_suspend NULL
#define rtsx_pci_sdmmc_resume NULL
#endif /* CONFIG_PM */

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
		MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25;
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
	host->power_state = SDMMC_POWER_OFF;
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
	host->eject = true;

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
	mmc_free_host(mmc);

	dev_dbg(&(pdev->dev),
		": Realtek PCI-E SDMMC controller has been removed\n");

	return 0;
}

static struct platform_device_id rtsx_pci_sdmmc_ids[] = {
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
	.suspend	= rtsx_pci_sdmmc_suspend,
	.resume		= rtsx_pci_sdmmc_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DRV_NAME_RTSX_PCI_SDMMC,
	},
};
module_platform_driver(rtsx_pci_sdmmc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wei WANG <wei_wang@realsil.com.cn>");
MODULE_DESCRIPTION("Realtek PCI-E SD/MMC Card Host Driver");
