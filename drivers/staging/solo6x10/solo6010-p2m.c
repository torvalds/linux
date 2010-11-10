/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/scatterlist.h>

#include "solo6010.h"

/* #define SOLO_TEST_P2M */

int solo_p2m_dma(struct solo6010_dev *solo_dev, u8 id, int wr,
		 void *sys_addr, u32 ext_addr, u32 size)
{
	dma_addr_t dma_addr;
	int ret;

	WARN_ON(!size);
	BUG_ON(id >= SOLO_NR_P2M);

	if (!size)
		return -EINVAL;

	dma_addr = pci_map_single(solo_dev->pdev, sys_addr, size,
				  wr ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);

	ret = solo_p2m_dma_t(solo_dev, id, wr, dma_addr, ext_addr, size);

	pci_unmap_single(solo_dev->pdev, dma_addr, size,
			 wr ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);

	return ret;
}

int solo_p2m_dma_t(struct solo6010_dev *solo_dev, u8 id, int wr,
		   dma_addr_t dma_addr, u32 ext_addr, u32 size)
{
	struct p2m_desc *desc = kzalloc(sizeof(*desc) * 2, GFP_DMA);
	int ret;

	if (desc == NULL)
		return -ENOMEM;

	solo_p2m_push_desc(&desc[1], wr, dma_addr, ext_addr, size, 0, 0);
	ret = solo_p2m_dma_desc(solo_dev, id, desc, 2);
	kfree(desc);

	return ret;
}

void solo_p2m_push_desc(struct p2m_desc *desc, int wr, dma_addr_t dma_addr,
			u32 ext_addr, u32 size, int repeat, u32 ext_size)
{
	desc->ta = dma_addr;
	desc->fa = ext_addr;

	desc->ext = SOLO_P2M_COPY_SIZE(size >> 2);
	desc->ctrl = SOLO_P2M_BURST_SIZE(SOLO_P2M_BURST_256) |
		(wr ? SOLO_P2M_WRITE : 0) | SOLO_P2M_TRANS_ON;

	/* Ext size only matters when we're repeating */
	if (repeat) {
		desc->ext |= SOLO_P2M_EXT_INC(ext_size >> 2);
		desc->ctrl |=  SOLO_P2M_PCI_INC(size >> 2) |
			SOLO_P2M_REPEAT(repeat);
	}
}

int solo_p2m_dma_desc(struct solo6010_dev *solo_dev, u8 id,
		      struct p2m_desc *desc, int desc_count)
{
	struct solo_p2m_dev *p2m_dev;
	unsigned int timeout;
	int ret = 0;
	u32 config = 0;
	dma_addr_t desc_dma = 0;

	BUG_ON(id >= SOLO_NR_P2M);
	BUG_ON(!desc_count || desc_count > SOLO_NR_P2M_DESC);

	p2m_dev = &solo_dev->p2m_dev[id];

	mutex_lock(&p2m_dev->mutex);

	solo_reg_write(solo_dev, SOLO_P2M_CONTROL(id), 0);

	INIT_COMPLETION(p2m_dev->completion);
	p2m_dev->error = 0;

	/* Enable the descriptors */
	config = solo_reg_read(solo_dev, SOLO_P2M_CONFIG(id));
	desc_dma = pci_map_single(solo_dev->pdev, desc,
				  desc_count * sizeof(*desc),
				  PCI_DMA_TODEVICE);
	solo_reg_write(solo_dev, SOLO_P2M_DES_ADR(id), desc_dma);
	solo_reg_write(solo_dev, SOLO_P2M_DESC_ID(id), desc_count - 1);
	solo_reg_write(solo_dev, SOLO_P2M_CONFIG(id), config |
		       SOLO_P2M_DESC_MODE);

	/* Should have all descriptors completed from one interrupt */
	timeout = wait_for_completion_timeout(&p2m_dev->completion, HZ);

	solo_reg_write(solo_dev, SOLO_P2M_CONTROL(id), 0);

	/* Reset back to non-descriptor mode */
	solo_reg_write(solo_dev, SOLO_P2M_CONFIG(id), config);
	solo_reg_write(solo_dev, SOLO_P2M_DESC_ID(id), 0);
	solo_reg_write(solo_dev, SOLO_P2M_DES_ADR(id), 0);
	pci_unmap_single(solo_dev->pdev, desc_dma,
			 desc_count * sizeof(*desc),
			 PCI_DMA_TODEVICE);

	if (p2m_dev->error)
		ret = -EIO;
	else if (timeout == 0)
		ret = -EAGAIN;

	mutex_unlock(&p2m_dev->mutex);

	WARN_ON_ONCE(ret);

	return ret;
}

int solo_p2m_dma_sg(struct solo6010_dev *solo_dev, u8 id,
		    struct p2m_desc *pdesc, int wr,
		    struct scatterlist *sg, u32 sg_off,
		    u32 ext_addr, u32 size)
{
	int i;
	int idx;

	BUG_ON(id >= SOLO_NR_P2M);

	if (WARN_ON_ONCE(!size))
		return -EINVAL;

	memset(pdesc, 0, sizeof(*pdesc));

	/* Should rewrite this to handle > SOLO_NR_P2M_DESC transactions */
	for (i = 0, idx = 1; idx < SOLO_NR_P2M_DESC && sg && size > 0;
	     i++, sg = sg_next(sg)) {
		struct p2m_desc *desc = &pdesc[idx];
		u32 sg_len = sg_dma_len(sg);
		u32 len;

		if (sg_off >= sg_len) {
			sg_off -= sg_len;
			continue;
		}

		sg_len -= sg_off;
		len = min(sg_len, size);

		solo_p2m_push_desc(desc, wr, sg_dma_address(sg) + sg_off,
				   ext_addr, len, 0, 0);

		size -= len;
		ext_addr += len;
		idx++;

		sg_off = 0;
	}

	WARN_ON_ONCE(size || i >= SOLO_NR_P2M_DESC);

	return solo_p2m_dma_desc(solo_dev, id, pdesc, idx);
}

#ifdef SOLO_TEST_P2M

#define P2M_TEST_CHAR		0xbe

static unsigned long long p2m_test(struct solo6010_dev *solo_dev, u8 id,
				   u32 base, int size)
{
	u8 *wr_buf;
	u8 *rd_buf;
	int i;
	unsigned long long err_cnt = 0;

	wr_buf = kmalloc(size, GFP_KERNEL);
	if (!wr_buf) {
		printk(SOLO6010_NAME ": Failed to malloc for p2m_test\n");
		return size;
	}

	rd_buf = kmalloc(size, GFP_KERNEL);
	if (!rd_buf) {
		printk(SOLO6010_NAME ": Failed to malloc for p2m_test\n");
		kfree(wr_buf);
		return size;
	}

	memset(wr_buf, P2M_TEST_CHAR, size);
	memset(rd_buf, P2M_TEST_CHAR + 1, size);

	solo_p2m_dma(solo_dev, id, 1, wr_buf, base, size);
	solo_p2m_dma(solo_dev, id, 0, rd_buf, base, size);

	for (i = 0; i < size; i++)
		if (wr_buf[i] != rd_buf[i])
			err_cnt++;

	kfree(wr_buf);
	kfree(rd_buf);

	return err_cnt;
}

#define TEST_CHUNK_SIZE		(8 * 1024)

static void run_p2m_test(struct solo6010_dev *solo_dev)
{
	unsigned long long errs = 0;
	u32 size = SOLO_JPEG_EXT_ADDR(solo_dev) + SOLO_JPEG_EXT_SIZE(solo_dev);
	int i, d;

	printk(KERN_WARNING "%s: Testing %u bytes of external ram\n",
	       SOLO6010_NAME, size);

	for (i = 0; i < size; i += TEST_CHUNK_SIZE)
		for (d = 0; d < 4; d++)
			errs += p2m_test(solo_dev, d, i, TEST_CHUNK_SIZE);

	printk(KERN_WARNING "%s: Found %llu errors during p2m test\n",
	       SOLO6010_NAME, errs);

	return;
}
#else
#define run_p2m_test(__solo)	do {} while (0)
#endif

void solo_p2m_isr(struct solo6010_dev *solo_dev, int id)
{
	struct solo_p2m_dev *p2m_dev = &solo_dev->p2m_dev[id];

	solo_reg_write(solo_dev, SOLO_IRQ_STAT, SOLO_IRQ_P2M(id));

	complete(&p2m_dev->completion);
}

void solo_p2m_error_isr(struct solo6010_dev *solo_dev, u32 status)
{
	struct solo_p2m_dev *p2m_dev;
	int i;

	if (!(status & SOLO_PCI_ERR_P2M))
		return;

	for (i = 0; i < SOLO_NR_P2M; i++) {
		p2m_dev = &solo_dev->p2m_dev[i];
		p2m_dev->error = 1;
		solo_reg_write(solo_dev, SOLO_P2M_CONTROL(i), 0);
		complete(&p2m_dev->completion);
	}
}

void solo_p2m_exit(struct solo6010_dev *solo_dev)
{
	int i;

	for (i = 0; i < SOLO_NR_P2M; i++)
		solo6010_irq_off(solo_dev, SOLO_IRQ_P2M(i));
}

int solo_p2m_init(struct solo6010_dev *solo_dev)
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
			       SOLO_P2M_DMA_INTERVAL(3) |
			       SOLO_P2M_DESC_INTR_OPT |
			       SOLO_P2M_PCI_MASTER_MODE);
		solo6010_irq_on(solo_dev, SOLO_IRQ_P2M(i));
	}

	run_p2m_test(solo_dev);

	return 0;
}
