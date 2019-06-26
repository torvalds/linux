/* Realtek USB Memstick Card Interface driver
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
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/memstick.h>
#include <linux/kthread.h>
#include <linux/rtsx_usb.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <asm/unaligned.h>

struct rtsx_usb_ms {
	struct platform_device	*pdev;
	struct rtsx_ucr	*ucr;
	struct memstick_host	*msh;
	struct memstick_request	*req;

	struct mutex		host_mutex;
	struct work_struct	handle_req;
	struct delayed_work	poll_card;

	u8			ssc_depth;
	unsigned int		clock;
	int			power_mode;
	unsigned char           ifmode;
	bool			eject;
	bool			system_suspending;
};

static inline struct device *ms_dev(struct rtsx_usb_ms *host)
{
	return &(host->pdev->dev);
}

static inline void ms_clear_error(struct rtsx_usb_ms *host)
{
	struct rtsx_ucr *ucr = host->ucr;
	rtsx_usb_ep0_write_register(ucr, CARD_STOP,
				  MS_STOP | MS_CLR_ERR,
				  MS_STOP | MS_CLR_ERR);

	rtsx_usb_clear_dma_err(ucr);
	rtsx_usb_clear_fsm_err(ucr);
}

#ifdef DEBUG

static void ms_print_debug_regs(struct rtsx_usb_ms *host)
{
	struct rtsx_ucr *ucr = host->ucr;
	u16 i;
	u8 *ptr;

	/* Print MS host internal registers */
	rtsx_usb_init_cmd(ucr);

	/* MS_CFG to MS_INT_REG */
	for (i = 0xFD40; i <= 0xFD44; i++)
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, i, 0, 0);

	/* CARD_SHARE_MODE to CARD_GPIO */
	for (i = 0xFD51; i <= 0xFD56; i++)
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, i, 0, 0);

	/* CARD_PULL_CTLx */
	for (i = 0xFD60; i <= 0xFD65; i++)
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, i, 0, 0);

	/* CARD_DATA_SOURCE, CARD_SELECT, CARD_CLK_EN, CARD_PWR_CTL */
	rtsx_usb_add_cmd(ucr, READ_REG_CMD, CARD_DATA_SOURCE, 0, 0);
	rtsx_usb_add_cmd(ucr, READ_REG_CMD, CARD_SELECT, 0, 0);
	rtsx_usb_add_cmd(ucr, READ_REG_CMD, CARD_CLK_EN, 0, 0);
	rtsx_usb_add_cmd(ucr, READ_REG_CMD, CARD_PWR_CTL, 0, 0);

	rtsx_usb_send_cmd(ucr, MODE_CR, 100);
	rtsx_usb_get_rsp(ucr, 21, 100);

	ptr = ucr->rsp_buf;
	for (i = 0xFD40; i <= 0xFD44; i++)
		dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", i, *(ptr++));
	for (i = 0xFD51; i <= 0xFD56; i++)
		dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", i, *(ptr++));
	for (i = 0xFD60; i <= 0xFD65; i++)
		dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", i, *(ptr++));

	dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", CARD_DATA_SOURCE, *(ptr++));
	dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", CARD_SELECT, *(ptr++));
	dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", CARD_CLK_EN, *(ptr++));
	dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", CARD_PWR_CTL, *(ptr++));
}

#else

static void ms_print_debug_regs(struct rtsx_usb_ms *host)
{
}

#endif

static int ms_pull_ctl_disable_lqfp48(struct rtsx_ucr *ucr)
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

static int ms_pull_ctl_disable_qfn24(struct rtsx_ucr *ucr)
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

static int ms_pull_ctl_enable_lqfp48(struct rtsx_ucr *ucr)
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

static int ms_pull_ctl_enable_qfn24(struct rtsx_ucr *ucr)
{
	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0x65);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0x95);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x55);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0x59);

	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}

static int ms_power_on(struct rtsx_usb_ms *host)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;

	dev_dbg(ms_dev(host), "%s\n", __func__);

	rtsx_usb_init_cmd(ucr);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_SELECT, 0x07, MS_MOD_SEL);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_SHARE_MODE,
			CARD_SHARE_MASK, CARD_SHARE_MS);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_CLK_EN,
			MS_CLK_EN, MS_CLK_EN);
	err = rtsx_usb_send_cmd(ucr, MODE_C, 100);
	if (err < 0)
		return err;

	if (CHECK_PKG(ucr, LQFP48))
		err = ms_pull_ctl_enable_lqfp48(ucr);
	else
		err = ms_pull_ctl_enable_qfn24(ucr);
	if (err < 0)
		return err;

	err = rtsx_usb_write_register(ucr, CARD_PWR_CTL,
			POWER_MASK, PARTIAL_POWER_ON);
	if (err)
		return err;

	usleep_range(800, 1000);

	rtsx_usb_init_cmd(ucr);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_PWR_CTL,
			POWER_MASK, POWER_ON);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_OE,
			MS_OUTPUT_EN, MS_OUTPUT_EN);

	return rtsx_usb_send_cmd(ucr, MODE_C, 100);
}

static int ms_power_off(struct rtsx_usb_ms *host)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;

	dev_dbg(ms_dev(host), "%s\n", __func__);

	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_CLK_EN, MS_CLK_EN, 0);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_OE, MS_OUTPUT_EN, 0);

	err = rtsx_usb_send_cmd(ucr, MODE_C, 100);
	if (err < 0)
		return err;

	if (CHECK_PKG(ucr, LQFP48))
		return ms_pull_ctl_disable_lqfp48(ucr);

	return ms_pull_ctl_disable_qfn24(ucr);
}

static int ms_transfer_data(struct rtsx_usb_ms *host, unsigned char data_dir,
		u8 tpc, u8 cfg, struct scatterlist *sg)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err;
	unsigned int length = sg->length;
	u16 sec_cnt = (u16)(length / 512);
	u8 trans_mode, dma_dir, flag;
	unsigned int pipe;
	struct memstick_dev *card = host->msh->card;

	dev_dbg(ms_dev(host), "%s: tpc = 0x%02x, data_dir = %s, length = %d\n",
			__func__, tpc, (data_dir == READ) ? "READ" : "WRITE",
			length);

	if (data_dir == READ) {
		flag = MODE_CDIR;
		dma_dir = DMA_DIR_FROM_CARD;
		if (card->id.type != MEMSTICK_TYPE_PRO)
			trans_mode = MS_TM_NORMAL_READ;
		else
			trans_mode = MS_TM_AUTO_READ;
		pipe = usb_rcvbulkpipe(ucr->pusb_dev, EP_BULK_IN);
	} else {
		flag = MODE_CDOR;
		dma_dir = DMA_DIR_TO_CARD;
		if (card->id.type != MEMSTICK_TYPE_PRO)
			trans_mode = MS_TM_NORMAL_WRITE;
		else
			trans_mode = MS_TM_AUTO_WRITE;
		pipe = usb_sndbulkpipe(ucr->pusb_dev, EP_BULK_OUT);
	}

	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	if (card->id.type == MEMSTICK_TYPE_PRO) {
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_SECTOR_CNT_H,
				0xFF, (u8)(sec_cnt >> 8));
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_SECTOR_CNT_L,
				0xFF, (u8)sec_cnt);
	}
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_TC3,
			0xFF, (u8)(length >> 24));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_TC2,
			0xFF, (u8)(length >> 16));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_TC1,
			0xFF, (u8)(length >> 8));
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_TC0, 0xFF,
			(u8)length);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MC_DMA_CTL,
			0x03 | DMA_PACK_SIZE_MASK, dma_dir | DMA_EN | DMA_512);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, RING_BUFFER);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TRANSFER,
			0xFF, MS_TRANSFER_START | trans_mode);
	rtsx_usb_add_cmd(ucr, CHECK_REG_CMD, MS_TRANSFER,
			MS_TRANSFER_END, MS_TRANSFER_END);

	err = rtsx_usb_send_cmd(ucr, flag | STAGE_MS_STATUS, 100);
	if (err)
		return err;

	err = rtsx_usb_transfer_data(ucr, pipe, sg, length,
			1, NULL, 10000);
	if (err)
		goto err_out;

	err = rtsx_usb_get_rsp(ucr, 3, 15000);
	if (err)
		goto err_out;

	if (ucr->rsp_buf[0] & MS_TRANSFER_ERR ||
	    ucr->rsp_buf[1] & (MS_CRC16_ERR | MS_RDY_TIMEOUT)) {
		err = -EIO;
		goto err_out;
	}
	return 0;
err_out:
	ms_clear_error(host);
	return err;
}

static int ms_write_bytes(struct rtsx_usb_ms *host, u8 tpc,
		u8 cfg, u8 cnt, u8 *data, u8 *int_reg)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err, i;

	dev_dbg(ms_dev(host), "%s: tpc = 0x%02x\n", __func__, tpc);

	rtsx_usb_init_cmd(ucr);

	for (i = 0; i < cnt; i++)
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				PPBUF_BASE2 + i, 0xFF, data[i]);

	if (cnt % 2)
		rtsx_usb_add_cmd(ucr, WRITE_REG_CMD,
				PPBUF_BASE2 + i, 0xFF, 0xFF);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_BYTE_CNT, 0xFF, cnt);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, PINGPONG_BUFFER);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TRANSFER,
			0xFF, MS_TRANSFER_START | MS_TM_WRITE_BYTES);
	rtsx_usb_add_cmd(ucr, CHECK_REG_CMD, MS_TRANSFER,
			MS_TRANSFER_END, MS_TRANSFER_END);
	rtsx_usb_add_cmd(ucr, READ_REG_CMD, MS_TRANS_CFG, 0, 0);

	err = rtsx_usb_send_cmd(ucr, MODE_CR, 100);
	if (err)
		return err;

	err = rtsx_usb_get_rsp(ucr, 2, 5000);
	if (err || (ucr->rsp_buf[0] & MS_TRANSFER_ERR)) {
		u8 val;

		rtsx_usb_ep0_read_register(ucr, MS_TRANS_CFG, &val);
		dev_dbg(ms_dev(host), "MS_TRANS_CFG: 0x%02x\n", val);

		if (int_reg)
			*int_reg = val & 0x0F;

		ms_print_debug_regs(host);

		ms_clear_error(host);

		if (!(tpc & 0x08)) {
			if (val & MS_CRC16_ERR)
				return -EIO;
		} else {
			if (!(val & 0x80)) {
				if (val & (MS_INT_ERR | MS_INT_CMDNK))
					return -EIO;
			}
		}

		return -ETIMEDOUT;
	}

	if (int_reg)
		*int_reg = ucr->rsp_buf[1] & 0x0F;

	return 0;
}

static int ms_read_bytes(struct rtsx_usb_ms *host, u8 tpc,
		u8 cfg, u8 cnt, u8 *data, u8 *int_reg)
{
	struct rtsx_ucr *ucr = host->ucr;
	int err, i;
	u8 *ptr;

	dev_dbg(ms_dev(host), "%s: tpc = 0x%02x\n", __func__, tpc);

	rtsx_usb_init_cmd(ucr);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_BYTE_CNT, 0xFF, cnt);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);
	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, PINGPONG_BUFFER);

	rtsx_usb_add_cmd(ucr, WRITE_REG_CMD, MS_TRANSFER,
			0xFF, MS_TRANSFER_START | MS_TM_READ_BYTES);
	rtsx_usb_add_cmd(ucr, CHECK_REG_CMD, MS_TRANSFER,
			MS_TRANSFER_END, MS_TRANSFER_END);
	for (i = 0; i < cnt - 1; i++)
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, PPBUF_BASE2 + i, 0, 0);
	if (cnt % 2)
		rtsx_usb_add_cmd(ucr, READ_REG_CMD, PPBUF_BASE2 + cnt, 0, 0);
	else
		rtsx_usb_add_cmd(ucr, READ_REG_CMD,
				PPBUF_BASE2 + cnt - 1, 0, 0);

	rtsx_usb_add_cmd(ucr, READ_REG_CMD, MS_TRANS_CFG, 0, 0);

	err = rtsx_usb_send_cmd(ucr, MODE_CR, 100);
	if (err)
		return err;

	err = rtsx_usb_get_rsp(ucr, cnt + 2, 5000);
	if (err || (ucr->rsp_buf[0] & MS_TRANSFER_ERR)) {
		u8 val;

		rtsx_usb_ep0_read_register(ucr, MS_TRANS_CFG, &val);
		dev_dbg(ms_dev(host), "MS_TRANS_CFG: 0x%02x\n", val);

		if (int_reg && (host->ifmode != MEMSTICK_SERIAL))
			*int_reg = val & 0x0F;

		ms_print_debug_regs(host);

		ms_clear_error(host);

		if (!(tpc & 0x08)) {
			if (val & MS_CRC16_ERR)
				return -EIO;
		} else {
			if (!(val & 0x80)) {
				if (val & (MS_INT_ERR | MS_INT_CMDNK))
					return -EIO;
			}
		}

		return -ETIMEDOUT;
	}

	ptr = ucr->rsp_buf + 1;
	for (i = 0; i < cnt; i++)
		data[i] = *ptr++;


	if (int_reg && (host->ifmode != MEMSTICK_SERIAL))
		*int_reg = *ptr & 0x0F;

	return 0;
}

static int rtsx_usb_ms_issue_cmd(struct rtsx_usb_ms *host)
{
	struct memstick_request *req = host->req;
	int err = 0;
	u8 cfg = 0, int_reg;

	dev_dbg(ms_dev(host), "%s\n", __func__);

	if (req->need_card_int) {
		if (host->ifmode != MEMSTICK_SERIAL)
			cfg = WAIT_INT;
	}

	if (req->long_data) {
		err = ms_transfer_data(host, req->data_dir,
				req->tpc, cfg, &(req->sg));
	} else {
		if (req->data_dir == READ)
			err = ms_read_bytes(host, req->tpc, cfg,
					req->data_len, req->data, &int_reg);
		else
			err = ms_write_bytes(host, req->tpc, cfg,
					req->data_len, req->data, &int_reg);
	}
	if (err < 0)
		return err;

	if (req->need_card_int) {
		if (host->ifmode == MEMSTICK_SERIAL) {
			err = ms_read_bytes(host, MS_TPC_GET_INT,
					NO_WAIT_INT, 1, &req->int_reg, NULL);
			if (err < 0)
				return err;
		} else {

			if (int_reg & MS_INT_CMDNK)
				req->int_reg |= MEMSTICK_INT_CMDNAK;
			if (int_reg & MS_INT_BREQ)
				req->int_reg |= MEMSTICK_INT_BREQ;
			if (int_reg & MS_INT_ERR)
				req->int_reg |= MEMSTICK_INT_ERR;
			if (int_reg & MS_INT_CED)
				req->int_reg |= MEMSTICK_INT_CED;
		}
		dev_dbg(ms_dev(host), "int_reg: 0x%02x\n", req->int_reg);
	}

	return 0;
}

static void rtsx_usb_ms_handle_req(struct work_struct *work)
{
	struct rtsx_usb_ms *host = container_of(work,
			struct rtsx_usb_ms, handle_req);
	struct rtsx_ucr *ucr = host->ucr;
	struct memstick_host *msh = host->msh;
	int rc;

	if (!host->req) {
		pm_runtime_get_sync(ms_dev(host));
		do {
			rc = memstick_next_req(msh, &host->req);
			dev_dbg(ms_dev(host), "next req %d\n", rc);

			if (!rc) {
				mutex_lock(&ucr->dev_mutex);

				if (rtsx_usb_card_exclusive_check(ucr,
							RTSX_USB_MS_CARD))
					host->req->error = -EIO;
				else
					host->req->error =
						rtsx_usb_ms_issue_cmd(host);

				mutex_unlock(&ucr->dev_mutex);

				dev_dbg(ms_dev(host), "req result %d\n",
						host->req->error);
			}
		} while (!rc);
		pm_runtime_put_sync(ms_dev(host));
	}

}

static void rtsx_usb_ms_request(struct memstick_host *msh)
{
	struct rtsx_usb_ms *host = memstick_priv(msh);

	dev_dbg(ms_dev(host), "--> %s\n", __func__);

	if (!host->eject)
		schedule_work(&host->handle_req);
}

static int rtsx_usb_ms_set_param(struct memstick_host *msh,
		enum memstick_param param, int value)
{
	struct rtsx_usb_ms *host = memstick_priv(msh);
	struct rtsx_ucr *ucr = host->ucr;
	unsigned int clock = 0;
	u8 ssc_depth = 0;
	int err;

	dev_dbg(ms_dev(host), "%s: param = %d, value = %d\n",
			__func__, param, value);

	pm_runtime_get_sync(ms_dev(host));
	mutex_lock(&ucr->dev_mutex);

	err = rtsx_usb_card_exclusive_check(ucr, RTSX_USB_MS_CARD);
	if (err)
		goto out;

	switch (param) {
	case MEMSTICK_POWER:
		if (value == host->power_mode)
			break;

		if (value == MEMSTICK_POWER_ON) {
			pm_runtime_get_noresume(ms_dev(host));
			err = ms_power_on(host);
			if (err)
				pm_runtime_put_noidle(ms_dev(host));
		} else if (value == MEMSTICK_POWER_OFF) {
			err = ms_power_off(host);
			if (!err)
				pm_runtime_put_noidle(ms_dev(host));
		} else
			err = -EINVAL;
		if (!err)
			host->power_mode = value;
		break;

	case MEMSTICK_INTERFACE:
		if (value == MEMSTICK_SERIAL) {
			clock = 19000000;
			ssc_depth = SSC_DEPTH_512K;
			err = rtsx_usb_write_register(ucr, MS_CFG, 0x5A,
				       MS_BUS_WIDTH_1 | PUSH_TIME_DEFAULT);
			if (err < 0)
				break;
		} else if (value == MEMSTICK_PAR4) {
			clock = 39000000;
			ssc_depth = SSC_DEPTH_1M;

			err = rtsx_usb_write_register(ucr, MS_CFG, 0x5A,
					MS_BUS_WIDTH_4 | PUSH_TIME_ODD |
					MS_NO_CHECK_INT);
			if (err < 0)
				break;
		} else {
			err = -EINVAL;
			break;
		}

		err = rtsx_usb_switch_clock(ucr, clock,
				ssc_depth, false, true, false);
		if (err < 0) {
			dev_dbg(ms_dev(host), "switch clock failed\n");
			break;
		}

		host->ssc_depth = ssc_depth;
		host->clock = clock;
		host->ifmode = value;
		break;
	default:
		err = -EINVAL;
		break;
	}
out:
	mutex_unlock(&ucr->dev_mutex);
	pm_runtime_put_sync(ms_dev(host));

	/* power-on delay */
	if (param == MEMSTICK_POWER && value == MEMSTICK_POWER_ON) {
		usleep_range(10000, 12000);

		if (!host->eject)
			schedule_delayed_work(&host->poll_card, 100);
	}

	dev_dbg(ms_dev(host), "%s: return = %d\n", __func__, err);
	return err;
}

#ifdef CONFIG_PM_SLEEP
static int rtsx_usb_ms_suspend(struct device *dev)
{
	struct rtsx_usb_ms *host = dev_get_drvdata(dev);
	struct memstick_host *msh = host->msh;

	/* Since we use rtsx_usb's resume callback to runtime resume its
	 * children to implement remote wakeup signaling, this causes
	 * rtsx_usb_ms' runtime resume callback runs after its suspend
	 * callback:
	 * rtsx_usb_ms_suspend()
	 * rtsx_usb_resume()
	 *   -> rtsx_usb_ms_runtime_resume()
	 *     -> memstick_detect_change()
	 *
	 * rtsx_usb_suspend()
	 *
	 * To avoid this, skip runtime resume/suspend if system suspend is
	 * underway.
	 */

	host->system_suspending = true;
	memstick_suspend_host(msh);

	return 0;
}

static int rtsx_usb_ms_resume(struct device *dev)
{
	struct rtsx_usb_ms *host = dev_get_drvdata(dev);
	struct memstick_host *msh = host->msh;

	memstick_resume_host(msh);
	host->system_suspending = false;

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int rtsx_usb_ms_runtime_suspend(struct device *dev)
{
	struct rtsx_usb_ms *host = dev_get_drvdata(dev);

	if (host->system_suspending)
		return 0;

	if (host->msh->card || host->power_mode != MEMSTICK_POWER_OFF)
		return -EAGAIN;

	return 0;
}

static int rtsx_usb_ms_runtime_resume(struct device *dev)
{
	struct rtsx_usb_ms *host = dev_get_drvdata(dev);


	if (host->system_suspending)
		return 0;

	memstick_detect_change(host->msh);

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops rtsx_usb_ms_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rtsx_usb_ms_suspend, rtsx_usb_ms_resume)
	SET_RUNTIME_PM_OPS(rtsx_usb_ms_runtime_suspend, rtsx_usb_ms_runtime_resume, NULL)
};


static void rtsx_usb_ms_poll_card(struct work_struct *work)
{
	struct rtsx_usb_ms *host = container_of(work, struct rtsx_usb_ms,
			poll_card.work);
	struct rtsx_ucr *ucr = host->ucr;
	int err;
	u8 val;

	if (host->eject || host->power_mode != MEMSTICK_POWER_ON)
		return;

	pm_runtime_get_sync(ms_dev(host));
	mutex_lock(&ucr->dev_mutex);

	/* Check pending MS card changes */
	err = rtsx_usb_read_register(ucr, CARD_INT_PEND, &val);
	if (err) {
		mutex_unlock(&ucr->dev_mutex);
		goto poll_again;
	}

	/* Clear the pending */
	rtsx_usb_write_register(ucr, CARD_INT_PEND,
			XD_INT | MS_INT | SD_INT,
			XD_INT | MS_INT | SD_INT);

	mutex_unlock(&ucr->dev_mutex);

	if (val & MS_INT) {
		dev_dbg(ms_dev(host), "MS slot change detected\n");
		memstick_detect_change(host->msh);
	}

poll_again:
	pm_runtime_put_sync(ms_dev(host));

	if (!host->eject && host->power_mode == MEMSTICK_POWER_ON)
		schedule_delayed_work(&host->poll_card, 100);
}

static int rtsx_usb_ms_drv_probe(struct platform_device *pdev)
{
	struct memstick_host *msh;
	struct rtsx_usb_ms *host;
	struct rtsx_ucr *ucr;
	int err;

	ucr = usb_get_intfdata(to_usb_interface(pdev->dev.parent));
	if (!ucr)
		return -ENXIO;

	dev_dbg(&(pdev->dev),
			"Realtek USB Memstick controller found\n");

	msh = memstick_alloc_host(sizeof(*host), &pdev->dev);
	if (!msh)
		return -ENOMEM;

	host = memstick_priv(msh);
	host->ucr = ucr;
	host->msh = msh;
	host->pdev = pdev;
	host->power_mode = MEMSTICK_POWER_OFF;
	platform_set_drvdata(pdev, host);

	mutex_init(&host->host_mutex);
	INIT_WORK(&host->handle_req, rtsx_usb_ms_handle_req);

	INIT_DELAYED_WORK(&host->poll_card, rtsx_usb_ms_poll_card);

	msh->request = rtsx_usb_ms_request;
	msh->set_param = rtsx_usb_ms_set_param;
	msh->caps = MEMSTICK_CAP_PAR4;

	pm_runtime_get_noresume(ms_dev(host));
	pm_runtime_set_active(ms_dev(host));
	pm_runtime_enable(ms_dev(host));

	err = memstick_add_host(msh);
	if (err)
		goto err_out;

	pm_runtime_put(ms_dev(host));

	return 0;
err_out:
	memstick_free_host(msh);
	pm_runtime_disable(ms_dev(host));
	pm_runtime_put_noidle(ms_dev(host));
	return err;
}

static int rtsx_usb_ms_drv_remove(struct platform_device *pdev)
{
	struct rtsx_usb_ms *host = platform_get_drvdata(pdev);
	struct memstick_host *msh = host->msh;
	int err;

	host->eject = true;
	cancel_work_sync(&host->handle_req);

	mutex_lock(&host->host_mutex);
	if (host->req) {
		dev_dbg(ms_dev(host),
			"%s: Controller removed during transfer\n",
			dev_name(&msh->dev));
		host->req->error = -ENOMEDIUM;
		do {
			err = memstick_next_req(msh, &host->req);
			if (!err)
				host->req->error = -ENOMEDIUM;
		} while (!err);
	}
	mutex_unlock(&host->host_mutex);

	memstick_remove_host(msh);
	memstick_free_host(msh);

	/* Balance possible unbalanced usage count
	 * e.g. unconditional module removal
	 */
	if (pm_runtime_active(ms_dev(host)))
		pm_runtime_put(ms_dev(host));

	pm_runtime_disable(ms_dev(host));
	platform_set_drvdata(pdev, NULL);

	dev_dbg(ms_dev(host),
		": Realtek USB Memstick controller has been removed\n");

	return 0;
}

static struct platform_device_id rtsx_usb_ms_ids[] = {
	{
		.name = "rtsx_usb_ms",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, rtsx_usb_ms_ids);

static struct platform_driver rtsx_usb_ms_driver = {
	.probe		= rtsx_usb_ms_drv_probe,
	.remove		= rtsx_usb_ms_drv_remove,
	.id_table       = rtsx_usb_ms_ids,
	.driver		= {
		.name	= "rtsx_usb_ms",
		.pm	= &rtsx_usb_ms_pm_ops,
	},
};
module_platform_driver(rtsx_usb_ms_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Roger Tseng <rogerable@realtek.com>");
MODULE_DESCRIPTION("Realtek USB Memstick Card Host Driver");
