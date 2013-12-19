/* Realtek PCI-Express Memstick Card Interface driver
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
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/memstick.h>
#include <linux/mfd/rtsx_pci.h>
#include <asm/unaligned.h>

struct realtek_pci_ms {
	struct platform_device	*pdev;
	struct rtsx_pcr		*pcr;
	struct memstick_host	*msh;
	struct memstick_request	*req;

	struct mutex		host_mutex;
	struct work_struct	handle_req;

	u8			ssc_depth;
	unsigned int		clock;
	unsigned char           ifmode;
	bool			eject;
};

static inline struct device *ms_dev(struct realtek_pci_ms *host)
{
	return &(host->pdev->dev);
}

static inline void ms_clear_error(struct realtek_pci_ms *host)
{
	rtsx_pci_write_register(host->pcr, CARD_STOP,
			MS_STOP | MS_CLR_ERR, MS_STOP | MS_CLR_ERR);
}

#ifdef DEBUG

static void ms_print_debug_regs(struct realtek_pci_ms *host)
{
	struct rtsx_pcr *pcr = host->pcr;
	u16 i;
	u8 *ptr;

	/* Print MS host internal registers */
	rtsx_pci_init_cmd(pcr);
	for (i = 0xFD40; i <= 0xFD44; i++)
		rtsx_pci_add_cmd(pcr, READ_REG_CMD, i, 0, 0);
	for (i = 0xFD52; i <= 0xFD69; i++)
		rtsx_pci_add_cmd(pcr, READ_REG_CMD, i, 0, 0);
	rtsx_pci_send_cmd(pcr, 100);

	ptr = rtsx_pci_get_cmd_data(pcr);
	for (i = 0xFD40; i <= 0xFD44; i++)
		dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", i, *(ptr++));
	for (i = 0xFD52; i <= 0xFD69; i++)
		dev_dbg(ms_dev(host), "0x%04X: 0x%02x\n", i, *(ptr++));
}

#else

#define ms_print_debug_regs(host)

#endif

static int ms_power_on(struct realtek_pci_ms *host)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_SELECT, 0x07, MS_MOD_SEL);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_SHARE_MODE,
			CARD_SHARE_MASK, CARD_SHARE_48_MS);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_CLK_EN,
			MS_CLK_EN, MS_CLK_EN);
	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	err = rtsx_pci_card_pull_ctl_enable(pcr, RTSX_MS_CARD);
	if (err < 0)
		return err;

	err = rtsx_pci_card_power_on(pcr, RTSX_MS_CARD);
	if (err < 0)
		return err;

	/* Wait ms power stable */
	msleep(150);

	err = rtsx_pci_write_register(pcr, CARD_OE,
			MS_OUTPUT_EN, MS_OUTPUT_EN);
	if (err < 0)
		return err;

	return 0;
}

static int ms_power_off(struct realtek_pci_ms *host)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;

	rtsx_pci_init_cmd(pcr);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_CLK_EN, MS_CLK_EN, 0);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_OE, MS_OUTPUT_EN, 0);

	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	err = rtsx_pci_card_power_off(pcr, RTSX_MS_CARD);
	if (err < 0)
		return err;

	return rtsx_pci_card_pull_ctl_disable(pcr, RTSX_MS_CARD);
}

static int ms_transfer_data(struct realtek_pci_ms *host, unsigned char data_dir,
		u8 tpc, u8 cfg, struct scatterlist *sg)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err;
	unsigned int length = sg->length;
	u16 sec_cnt = (u16)(length / 512);
	u8 val, trans_mode, dma_dir;

	dev_dbg(ms_dev(host), "%s: tpc = 0x%02x, data_dir = %s, length = %d\n",
			__func__, tpc, (data_dir == READ) ? "READ" : "WRITE",
			length);

	if (data_dir == READ) {
		dma_dir = DMA_DIR_FROM_CARD;
		trans_mode = MS_TM_AUTO_READ;
	} else {
		dma_dir = DMA_DIR_TO_CARD;
		trans_mode = MS_TM_AUTO_WRITE;
	}

	rtsx_pci_init_cmd(pcr);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_SECTOR_CNT_H,
			0xFF, (u8)(sec_cnt >> 8));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_SECTOR_CNT_L,
			0xFF, (u8)sec_cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, IRQSTAT0,
			DMA_DONE_INT, DMA_DONE_INT);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC3, 0xFF, (u8)(length >> 24));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC2, 0xFF, (u8)(length >> 16));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC1, 0xFF, (u8)(length >> 8));
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMATC0, 0xFF, (u8)length);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, DMACTL,
			0x03 | DMA_PACK_SIZE_MASK, dma_dir | DMA_EN | DMA_512);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, RING_BUFFER);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TRANSFER,
			0xFF, MS_TRANSFER_START | trans_mode);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, MS_TRANSFER,
			MS_TRANSFER_END, MS_TRANSFER_END);

	rtsx_pci_send_cmd_no_wait(pcr);

	err = rtsx_pci_transfer_data(pcr, sg, 1, data_dir == READ, 10000);
	if (err < 0) {
		ms_clear_error(host);
		return err;
	}

	rtsx_pci_read_register(pcr, MS_TRANS_CFG, &val);
	if (val & (MS_INT_CMDNK | MS_INT_ERR | MS_CRC16_ERR | MS_RDY_TIMEOUT))
		return -EIO;

	return 0;
}

static int ms_write_bytes(struct realtek_pci_ms *host, u8 tpc,
		u8 cfg, u8 cnt, u8 *data, u8 *int_reg)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err, i;

	dev_dbg(ms_dev(host), "%s: tpc = 0x%02x\n", __func__, tpc);

	if (!data)
		return -EINVAL;

	rtsx_pci_init_cmd(pcr);

	for (i = 0; i < cnt; i++)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				PPBUF_BASE2 + i, 0xFF, data[i]);
	if (cnt % 2)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				PPBUF_BASE2 + i, 0xFF, 0xFF);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_BYTE_CNT, 0xFF, cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, PINGPONG_BUFFER);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TRANSFER,
			0xFF, MS_TRANSFER_START | MS_TM_WRITE_BYTES);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, MS_TRANSFER,
			MS_TRANSFER_END, MS_TRANSFER_END);
	if (int_reg)
		rtsx_pci_add_cmd(pcr, READ_REG_CMD, MS_TRANS_CFG, 0, 0);

	err = rtsx_pci_send_cmd(pcr, 5000);
	if (err < 0) {
		u8 val;

		rtsx_pci_read_register(pcr, MS_TRANS_CFG, &val);
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

	if (int_reg) {
		u8 *ptr = rtsx_pci_get_cmd_data(pcr) + 1;
		*int_reg = *ptr & 0x0F;
	}

	return 0;
}

static int ms_read_bytes(struct realtek_pci_ms *host, u8 tpc,
		u8 cfg, u8 cnt, u8 *data, u8 *int_reg)
{
	struct rtsx_pcr *pcr = host->pcr;
	int err, i;
	u8 *ptr;

	dev_dbg(ms_dev(host), "%s: tpc = 0x%02x\n", __func__, tpc);

	if (!data)
		return -EINVAL;

	rtsx_pci_init_cmd(pcr);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_BYTE_CNT, 0xFF, cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, PINGPONG_BUFFER);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, MS_TRANSFER,
			0xFF, MS_TRANSFER_START | MS_TM_READ_BYTES);
	rtsx_pci_add_cmd(pcr, CHECK_REG_CMD, MS_TRANSFER,
			MS_TRANSFER_END, MS_TRANSFER_END);
	for (i = 0; i < cnt - 1; i++)
		rtsx_pci_add_cmd(pcr, READ_REG_CMD, PPBUF_BASE2 + i, 0, 0);
	if (cnt % 2)
		rtsx_pci_add_cmd(pcr, READ_REG_CMD, PPBUF_BASE2 + cnt, 0, 0);
	else
		rtsx_pci_add_cmd(pcr, READ_REG_CMD,
				PPBUF_BASE2 + cnt - 1, 0, 0);
	if (int_reg)
		rtsx_pci_add_cmd(pcr, READ_REG_CMD, MS_TRANS_CFG, 0, 0);

	err = rtsx_pci_send_cmd(pcr, 5000);
	if (err < 0) {
		u8 val;

		rtsx_pci_read_register(pcr, MS_TRANS_CFG, &val);
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

	ptr = rtsx_pci_get_cmd_data(pcr) + 1;
	for (i = 0; i < cnt; i++)
		data[i] = *ptr++;

	if (int_reg)
		*int_reg = *ptr & 0x0F;

	return 0;
}

static int rtsx_pci_ms_issue_cmd(struct realtek_pci_ms *host)
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
		if (req->data_dir == READ) {
			err = ms_read_bytes(host, req->tpc, cfg,
					req->data_len, req->data, &int_reg);
		} else {
			err = ms_write_bytes(host, req->tpc, cfg,
					req->data_len, req->data, &int_reg);
		}
	}
	if (err < 0)
		return err;

	if (req->need_card_int && (host->ifmode == MEMSTICK_SERIAL)) {
		err = ms_read_bytes(host, MS_TPC_GET_INT,
				NO_WAIT_INT, 1, &int_reg, NULL);
		if (err < 0)
			return err;
	}

	if (req->need_card_int) {
		dev_dbg(ms_dev(host), "int_reg: 0x%02x\n", int_reg);

		if (int_reg & MS_INT_CMDNK)
			req->int_reg |= MEMSTICK_INT_CMDNAK;
		if (int_reg & MS_INT_BREQ)
			req->int_reg |= MEMSTICK_INT_BREQ;
		if (int_reg & MS_INT_ERR)
			req->int_reg |= MEMSTICK_INT_ERR;
		if (int_reg & MS_INT_CED)
			req->int_reg |= MEMSTICK_INT_CED;
	}

	return 0;
}

static void rtsx_pci_ms_handle_req(struct work_struct *work)
{
	struct realtek_pci_ms *host = container_of(work,
			struct realtek_pci_ms, handle_req);
	struct rtsx_pcr *pcr = host->pcr;
	struct memstick_host *msh = host->msh;
	int rc;

	mutex_lock(&pcr->pcr_mutex);

	rtsx_pci_start_run(pcr);

	rtsx_pci_switch_clock(host->pcr, host->clock, host->ssc_depth,
			false, true, false);
	rtsx_pci_write_register(pcr, CARD_SELECT, 0x07, MS_MOD_SEL);
	rtsx_pci_write_register(pcr, CARD_SHARE_MODE,
			CARD_SHARE_MASK, CARD_SHARE_48_MS);

	if (!host->req) {
		do {
			rc = memstick_next_req(msh, &host->req);
			dev_dbg(ms_dev(host), "next req %d\n", rc);

			if (!rc)
				host->req->error = rtsx_pci_ms_issue_cmd(host);
		} while (!rc);
	}

	mutex_unlock(&pcr->pcr_mutex);
}

static void rtsx_pci_ms_request(struct memstick_host *msh)
{
	struct realtek_pci_ms *host = memstick_priv(msh);

	dev_dbg(ms_dev(host), "--> %s\n", __func__);

	if (rtsx_pci_card_exclusive_check(host->pcr, RTSX_MS_CARD))
		return;

	schedule_work(&host->handle_req);
}

static int rtsx_pci_ms_set_param(struct memstick_host *msh,
		enum memstick_param param, int value)
{
	struct realtek_pci_ms *host = memstick_priv(msh);
	struct rtsx_pcr *pcr = host->pcr;
	unsigned int clock = 0;
	u8 ssc_depth = 0;
	int err;

	dev_dbg(ms_dev(host), "%s: param = %d, value = %d\n",
			__func__, param, value);

	err = rtsx_pci_card_exclusive_check(host->pcr, RTSX_MS_CARD);
	if (err)
		return err;

	switch (param) {
	case MEMSTICK_POWER:
		if (value == MEMSTICK_POWER_ON)
			err = ms_power_on(host);
		else if (value == MEMSTICK_POWER_OFF)
			err = ms_power_off(host);
		else
			return -EINVAL;
		break;

	case MEMSTICK_INTERFACE:
		if (value == MEMSTICK_SERIAL) {
			clock = 19000000;
			ssc_depth = RTSX_SSC_DEPTH_500K;

			err = rtsx_pci_write_register(pcr, MS_CFG,
					0x18, MS_BUS_WIDTH_1);
			if (err < 0)
				return err;
		} else if (value == MEMSTICK_PAR4) {
			clock = 39000000;
			ssc_depth = RTSX_SSC_DEPTH_1M;

			err = rtsx_pci_write_register(pcr, MS_CFG,
					0x58, MS_BUS_WIDTH_4 | PUSH_TIME_ODD);
			if (err < 0)
				return err;
		} else {
			return -EINVAL;
		}

		err = rtsx_pci_switch_clock(pcr, clock,
				ssc_depth, false, true, false);
		if (err < 0)
			return err;

		host->ssc_depth = ssc_depth;
		host->clock = clock;
		host->ifmode = value;
		break;
	}

	return 0;
}

#ifdef CONFIG_PM

static int rtsx_pci_ms_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct realtek_pci_ms *host = platform_get_drvdata(pdev);
	struct memstick_host *msh = host->msh;

	dev_dbg(ms_dev(host), "--> %s\n", __func__);

	memstick_suspend_host(msh);
	return 0;
}

static int rtsx_pci_ms_resume(struct platform_device *pdev)
{
	struct realtek_pci_ms *host = platform_get_drvdata(pdev);
	struct memstick_host *msh = host->msh;

	dev_dbg(ms_dev(host), "--> %s\n", __func__);

	memstick_resume_host(msh);
	return 0;
}

#else /* CONFIG_PM */

#define rtsx_pci_ms_suspend NULL
#define rtsx_pci_ms_resume NULL

#endif /* CONFIG_PM */

static void rtsx_pci_ms_card_event(struct platform_device *pdev)
{
	struct realtek_pci_ms *host = platform_get_drvdata(pdev);

	memstick_detect_change(host->msh);
}

static int rtsx_pci_ms_drv_probe(struct platform_device *pdev)
{
	struct memstick_host *msh;
	struct realtek_pci_ms *host;
	struct rtsx_pcr *pcr;
	struct pcr_handle *handle = pdev->dev.platform_data;
	int rc;

	if (!handle)
		return -ENXIO;

	pcr = handle->pcr;
	if (!pcr)
		return -ENXIO;

	dev_dbg(&(pdev->dev),
			": Realtek PCI-E Memstick controller found\n");

	msh = memstick_alloc_host(sizeof(*host), &pdev->dev);
	if (!msh)
		return -ENOMEM;

	host = memstick_priv(msh);
	host->pcr = pcr;
	host->msh = msh;
	host->pdev = pdev;
	platform_set_drvdata(pdev, host);
	pcr->slots[RTSX_MS_CARD].p_dev = pdev;
	pcr->slots[RTSX_MS_CARD].card_event = rtsx_pci_ms_card_event;

	mutex_init(&host->host_mutex);

	INIT_WORK(&host->handle_req, rtsx_pci_ms_handle_req);
	msh->request = rtsx_pci_ms_request;
	msh->set_param = rtsx_pci_ms_set_param;
	msh->caps = MEMSTICK_CAP_PAR4;

	rc = memstick_add_host(msh);
	if (rc) {
		memstick_free_host(msh);
		return rc;
	}

	return 0;
}

static int rtsx_pci_ms_drv_remove(struct platform_device *pdev)
{
	struct realtek_pci_ms *host = platform_get_drvdata(pdev);
	struct rtsx_pcr *pcr;
	struct memstick_host *msh;
	int rc;

	if (!host)
		return 0;

	pcr = host->pcr;
	pcr->slots[RTSX_MS_CARD].p_dev = NULL;
	pcr->slots[RTSX_MS_CARD].card_event = NULL;
	msh = host->msh;
	host->eject = true;

	mutex_lock(&host->host_mutex);
	if (host->req) {
		dev_dbg(&(pdev->dev),
			"%s: Controller removed during transfer\n",
			dev_name(&msh->dev));

		rtsx_pci_complete_unfinished_transfer(pcr);

		host->req->error = -ENOMEDIUM;
		do {
			rc = memstick_next_req(msh, &host->req);
			if (!rc)
				host->req->error = -ENOMEDIUM;
		} while (!rc);
	}
	mutex_unlock(&host->host_mutex);

	memstick_remove_host(msh);
	memstick_free_host(msh);

	dev_dbg(&(pdev->dev),
		": Realtek PCI-E Memstick controller has been removed\n");

	return 0;
}

static struct platform_device_id rtsx_pci_ms_ids[] = {
	{
		.name = DRV_NAME_RTSX_PCI_MS,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, rtsx_pci_ms_ids);

static struct platform_driver rtsx_pci_ms_driver = {
	.probe		= rtsx_pci_ms_drv_probe,
	.remove		= rtsx_pci_ms_drv_remove,
	.id_table       = rtsx_pci_ms_ids,
	.suspend	= rtsx_pci_ms_suspend,
	.resume		= rtsx_pci_ms_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DRV_NAME_RTSX_PCI_MS,
	},
};
module_platform_driver(rtsx_pci_ms_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wei WANG <wei_wang@realsil.com.cn>");
MODULE_DESCRIPTION("Realtek PCI-E Memstick Card Host Driver");
