// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <https://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "solo6x10.h"

static int multi_p2m;
module_param(multi_p2m, uint, 0644);
MODULE_PARM_DESC(multi_p2m,
		 "Use multiple P2M DMA channels (default: no, 6010-only)");

static int desc_mode;
module_param(desc_mode, uint, 0644);
MODULE_PARM_DESC(desc_mode,
		 "Allow use of descriptor mode DMA (default: no, 6010-only)");

int solo_p2m_dma(struct solo_dev *solo_dev, int wr,
		 void *sys_addr, u32 ext_addr, u32 size,
		 int repeat, u32 ext_size)
{
	dma_addr_t dma_addr;
	int ret;

	if (WARN_ON_ONCE((unsigned long)sys_addr & 0x03))
		return -EINVAL;
	if (WARN_ON_ONCE(!size))
		return -EINVAL;

	dma_addr = dma_map_single(&solo_dev->pdev->dev, sys_addr, size,
				  wr ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (dma_mapping_error(&solo_dev->pdev->dev, dma_addr))
		return -ENOMEM;

	ret = solo_p2m_dma_t(solo_dev, wr, dma_addr, ext_addr, size,
			     repeat, ext_size);

	dma_unmap_single(&solo_dev->pdev->dev, dma_addr, size,
			 wr ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

	return ret;
}

/* Mutex must be held for p2m_id before calling this!! */
int solo_p2m_dma_desc(struct solo_dev *solo_dev,
		      struct solo_p2m_desc *desc, dma_addr_t desc_dma,
		      int desc_cnt)
{
	struct solo_p2m_dev *p2m_dev;
	unsigned long time_left;
	unsigned int config = 0;
	int ret = 0;
	unsigned int p2m_id = 0;

	/* Get next ID. According to Softlogic, 6110 has problems on !=0 P2M */
	if (solo_dev->type != SOLO_DEV_6110 && multi_p2m)
		p2m_id = atomic_inc_return(&solo_dev->p2m_count) % SOLO_NR_P2M;

	p2m_dev = &solo_dev->p2m_dev[p2m_id];

	if (mutex_lock_interruptible(&p2m_dev->mutex))
		return -EINTR;

	reinit_completion(&p2m_dev->completion);
	p2m_dev->error = 0;

	if (desc_cnt > 1 && solo_dev->type != SOLO_DEV_6110 && desc_mode) {
		/* For 6010 with more than one desc, we can do a one-shot */
		p2m_dev->desc_count = p2m_dev->desc_idx = 0;
		config = solo_reg_read(solo_dev, SOLO_P2M_CONFIG(p2m_id));

		solo_reg_write(solo_dev, SOLO_P2M_DES_ADR(p2m_id), desc_dma);
		solo_reg_write(solo_dev, SOLO_P2M_DESC_ID(p2m_id), desc_cnt);
		solo_reg_write(solo_dev, SOLO_P2M_CONFIG(p2m_id), config |
			       SOLO_P2M_DESC_MODE);
	} else {
		/* For single descriptors and 6110, we need to run each desc */
		p2m_dev->desc_count = desc_cnt;
		p2m_dev->desc_idx = 1;
		p2m_dev->descs = desc;

		solo_reg_write(solo_dev, SOLO_P2M_TAR_ADR(p2m_id),
			       desc[1].dma_addr);
		solo_reg_write(solo_dev, SOLO_P2M_EXT_ADR(p2m_id),
			       desc[1].ext_addr);
		solo_reg_write(solo_dev, SOLO_P2M_EXT_CFG(p2m_id),
			       desc[1].cfg);
		solo_reg_write(solo_dev, SOLO_P2M_CONTROL(p2m_id),
			       desc[1].ctrl);
	}

	time_left = wait_for_completion_timeout(&p2m_dev->completion,
						solo_dev->p2m_jiffies);

	if (WARN_ON_ONCE(p2m_dev->error))
		ret = -EIO;
	else if (time_left == 0) {
		solo_dev->p2m_timeouts++;
		ret = -EAGAIN;
	}

	solo_reg_write(solo_dev, SOLO_P2M_CONTROL(p2m_id), 0);

	/* Don't write here for the no_desc_mode case, because config is 0.
	 * We can't test no_desc_mode again, it might race. */
	if (desc_cnt > 1 && solo_dev->type != SOLO_DEV_6110 && config)
		solo_reg_write(solo_dev, SOLO_P2M_CONFIG(p2m_id), config);

	mutex_unlock(&p2m_dev->mutex);

	return ret;
}

void solo_p2m_fill_desc(struct solo_p2m_desc *desc, int wr,
			dma_addr_t dma_addr, u32 ext_addr, u32 size,
			int repeat, u32 ext_size)
{
	WARN_ON_ONCE(dma_addr & 0x03);
	WARN_ON_ONCE(!size);

	desc->cfg = SOLO_P2M_COPY_SIZE(size >> 2);
	desc->ctrl = SOLO_P2M_BURST_SIZE(SOLO_P2M_BURST_256) |
		(wr ? SOLO_P2M_WRITE : 0) | SOLO_P2M_TRANS_ON;

	if (repeat) {
		desc->cfg |= SOLO_P2M_EXT_INC(ext_size >> 2);
		desc->ctrl |=  SOLO_P2M_PCI_INC(size >> 2) |
			 SOLO_P2M_REPEAT(repeat);
	}

	desc->dma_addr = dma_addr;
	desc->ext_addr = ext_addr;
}

int solo_p2m_dma_t(struct solo_dev *solo_dev, int wr,
		   dma_addr_t dma_addr, u32 ext_addr, u32 size,
		   int repeat, u32 ext_size)
{
	struct solo_p2m_desc desc[2];

	solo_p2m_fill_desc(&desc[1], wr, dma_addr, ext_addr, size, repeat,
			   ext_size);

	/* No need for desc_dma since we know it is a single-shot */
	return solo_p2m_dma_desc(solo_dev, desc, 0, 1);
}

void solo_p2m_isr(struct solo_dev *solo_dev, int id)
{
	struct solo_p2m_dev *p2m_dev = &solo_dev->p2m_dev[id];
	struct solo_p2m_desc *desc;

	if (p2m_dev->desc_count <= p2m_dev->desc_idx) {
		complete(&p2m_dev->completion);
		return;
	}

	/* Setup next descriptor */
	p2m_dev->desc_idx++;
	desc = &p2m_dev->descs[p2m_dev->desc_idx];

	solo_reg_write(solo_dev, SOLO_P2M_CONTROL(id), 0);
	solo_reg_write(solo_dev, SOLO_P2M_TAR_ADR(id), desc->dma_addr);
	solo_reg_write(solo_dev, SOLO_P2M_EXT_ADR(id), desc->ext_addr);
	solo_reg_write(solo_dev, SOLO_P2M_EXT_CFG(id), desc->cfg);
	solo_reg_write(solo_dev, SOLO_P2M_CONTROL(id), desc->ctrl);
}

void solo_p2m_error_isr(struct solo_dev *solo_dev)
{
	unsigned int err = solo_reg_read(solo_dev, SOLO_PCI_ERR);
	struct solo_p2m_dev *p2m_dev;
	int i;

	if (!(err & (SOLO_PCI_ERR_P2M | SOLO_PCI_ERR_P2M_DESC)))
		return;

	for (i = 0; i < SOLO_NR_P2M; i++) {
		p2m_dev = &solo_dev->p2m_dev[i];
		p2m_dev->error = 1;
		solo_reg_write(solo_dev, SOLO_P2M_CONTROL(i), 0);
		complete(&p2m_dev->completion);
	}
}

void solo_p2m_exit(struct solo_dev *solo_dev)
{
	int i;

	for (i = 0; i < SOLO_NR_P2M; i++)
		solo_irq_off(solo_dev, SOLO_IRQ_P2M(i));
}

static int solo_p2m_test(struct solo_dev *solo_dev, int base, int size)
{
	u32 *wr_buf;
	u32 *rd_buf;
	int i;
	int ret = -EIO;
	int order = get_order(size);

	wr_buf = (u32 *)__get_free_pages(GFP_KERNEL, order);
	if (wr_buf == NULL)
		return -1;

	rd_buf = (u32 *)__get_free_pages(GFP_KERNEL, order);
	if (rd_buf == NULL) {
		free_pages((unsigned long)wr_buf, order);
		return -1;
	}

	for (i = 0; i < (size >> 3); i++)
		*(wr_buf + i) = (i << 16) | (i + 1);

	for (i = (size >> 3); i < (size >> 2); i++)
		*(wr_buf + i) = ~((i << 16) | (i + 1));

	memset(rd_buf, 0x55, size);

	if (solo_p2m_dma(solo_dev, 1, wr_buf, base, size, 0, 0))
		goto test_fail;

	if (solo_p2m_dma(solo_dev, 0, rd_buf, base, size, 0, 0))
		goto test_fail;

	for (i = 0; i < (size >> 2); i++) {
		if (*(wr_buf + i) != *(rd_buf + i))
			goto test_fail;
	}

	ret = 0;

test_fail:
	free_pages((unsigned long)wr_buf, order);
	free_pages((unsigned long)rd_buf, order);

	return ret;
}

int solo_p2m_init(struct solo_dev *solo_dev)
{
	struct solo_p2m_dev *p2m_dev;
	int i;

	for (i = 0; i < SOLO_NR_P2M; i++) {
		p2m_dev = &solo_dev->p2m_dev[i];

		mutex_init(&p2m_dev->mutex);
		init_completion(&p2m_dev->completion);

		solo_reg_write(solo_dev, SOLO_P2M_CONTROL(i), 0);
		solo_reg_write(solo_dev, SOLO_P2M_CONFIG(i),
			       SOLO_P2M_CSC_16BIT_565 |
			       SOLO_P2M_DESC_INTR_OPT |
			       SOLO_P2M_DMA_INTERVAL(0) |
			       SOLO_P2M_PCI_MASTER_MODE);
		solo_irq_on(solo_dev, SOLO_IRQ_P2M(i));
	}

	/* Find correct SDRAM size */
	for (solo_dev->sdram_size = 0, i = 2; i >= 0; i--) {
		solo_reg_write(solo_dev, SOLO_DMA_CTRL,
			       SOLO_DMA_CTRL_REFRESH_CYCLE(1) |
			       SOLO_DMA_CTRL_SDRAM_SIZE(i) |
			       SOLO_DMA_CTRL_SDRAM_CLK_INVERT |
			       SOLO_DMA_CTRL_READ_CLK_SELECT |
			       SOLO_DMA_CTRL_LATENCY(1));

		solo_reg_write(solo_dev, SOLO_SYS_CFG, solo_dev->sys_config |
			       SOLO_SYS_CFG_RESET);
		solo_reg_write(solo_dev, SOLO_SYS_CFG, solo_dev->sys_config);

		switch (i) {
		case 2:
			if (solo_p2m_test(solo_dev, 0x07ff0000, 0x00010000) ||
			    solo_p2m_test(solo_dev, 0x05ff0000, 0x00010000))
				continue;
			break;

		case 1:
			if (solo_p2m_test(solo_dev, 0x03ff0000, 0x00010000))
				continue;
			break;

		default:
			if (solo_p2m_test(solo_dev, 0x01ff0000, 0x00010000))
				continue;
		}

		solo_dev->sdram_size = (32 << 20) << i;
		break;
	}

	if (!solo_dev->sdram_size) {
		dev_err(&solo_dev->pdev->dev, "Error detecting SDRAM size\n");
		return -EIO;
	}

	if (SOLO_SDRAM_END(solo_dev) > solo_dev->sdram_size) {
		dev_err(&solo_dev->pdev->dev,
			"SDRAM is not large enough (%u < %u)\n",
			solo_dev->sdram_size, SOLO_SDRAM_END(solo_dev));
		return -EIO;
	}

	return 0;
}
