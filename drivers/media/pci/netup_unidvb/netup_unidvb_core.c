/*
 * netup_unidvb_core.c
 *
 * Main module for NetUP Universal Dual DVB-CI
 *
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
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
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#include "netup_unidvb.h"
#include "cxd2841er.h"
#include "horus3a.h"
#include "ascot2e.h"
#include "helene.h"
#include "lnbh25.h"

static int spi_enable;
module_param(spi_enable, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

MODULE_DESCRIPTION("Driver for NetUP Dual Universal DVB CI PCIe card");
MODULE_AUTHOR("info@netup.ru");
MODULE_VERSION(NETUP_UNIDVB_VERSION);
MODULE_LICENSE("GPL");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* Avalon-MM PCI-E registers */
#define	AVL_PCIE_IENR		0x50
#define AVL_PCIE_ISR		0x40
#define AVL_IRQ_ENABLE		0x80
#define AVL_IRQ_ASSERTED	0x80
/* GPIO registers */
#define GPIO_REG_IO		0x4880
#define GPIO_REG_IO_TOGGLE	0x4882
#define GPIO_REG_IO_SET		0x4884
#define GPIO_REG_IO_CLEAR	0x4886
/* GPIO bits */
#define GPIO_FEA_RESET		(1 << 0)
#define GPIO_FEB_RESET		(1 << 1)
#define GPIO_RFA_CTL		(1 << 2)
#define GPIO_RFB_CTL		(1 << 3)
#define GPIO_FEA_TU_RESET	(1 << 4)
#define GPIO_FEB_TU_RESET	(1 << 5)
/* DMA base address */
#define NETUP_DMA0_ADDR		0x4900
#define NETUP_DMA1_ADDR		0x4940
/* 8 DMA blocks * 128 packets * 188 bytes*/
#define NETUP_DMA_BLOCKS_COUNT	8
#define NETUP_DMA_PACKETS_COUNT	128
/* DMA status bits */
#define BIT_DMA_RUN		1
#define BIT_DMA_ERROR		2
#define BIT_DMA_IRQ		0x200

/**
 * struct netup_dma_regs - the map of DMA module registers
 * @ctrlstat_set:	Control register, write to set control bits
 * @ctrlstat_clear:	Control register, write to clear control bits
 * @start_addr_lo:	DMA ring buffer start address, lower part
 * @start_addr_hi:	DMA ring buffer start address, higher part
 * @size:		DMA ring buffer size register
 *			* Bits [0-7]:	DMA packet size, 188 bytes
 *			* Bits [16-23]:	packets count in block, 128 packets
 *			* Bits [24-31]:	blocks count, 8 blocks
 * @timeout:		DMA timeout in units of 8ns
 *			For example, value of 375000000 equals to 3 sec
 * @curr_addr_lo:	Current ring buffer head address, lower part
 * @curr_addr_hi:	Current ring buffer head address, higher part
 * @stat_pkt_received:	Statistic register, not tested
 * @stat_pkt_accepted:	Statistic register, not tested
 * @stat_pkt_overruns:	Statistic register, not tested
 * @stat_pkt_underruns:	Statistic register, not tested
 * @stat_fifo_overruns:	Statistic register, not tested
 */
struct netup_dma_regs {
	__le32	ctrlstat_set;
	__le32	ctrlstat_clear;
	__le32	start_addr_lo;
	__le32	start_addr_hi;
	__le32	size;
	__le32	timeout;
	__le32	curr_addr_lo;
	__le32	curr_addr_hi;
	__le32	stat_pkt_received;
	__le32	stat_pkt_accepted;
	__le32	stat_pkt_overruns;
	__le32	stat_pkt_underruns;
	__le32	stat_fifo_overruns;
} __packed __aligned(1);

struct netup_unidvb_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head	list;
	u32			size;
};

static int netup_unidvb_tuner_ctrl(void *priv, int is_dvb_tc);
static void netup_unidvb_queue_cleanup(struct netup_dma *dma);

static struct cxd2841er_config demod_config = {
	.i2c_addr = 0xc8,
	.xtal = SONY_XTAL_24000,
	.flags = CXD2841ER_USE_GATECTRL | CXD2841ER_ASCOT
};

static struct horus3a_config horus3a_conf = {
	.i2c_address = 0xc0,
	.xtal_freq_mhz = 16,
	.set_tuner_callback = netup_unidvb_tuner_ctrl
};

static struct ascot2e_config ascot2e_conf = {
	.i2c_address = 0xc2,
	.set_tuner_callback = netup_unidvb_tuner_ctrl
};

static struct helene_config helene_conf = {
	.i2c_address = 0xc0,
	.xtal = SONY_HELENE_XTAL_24000,
	.set_tuner_callback = netup_unidvb_tuner_ctrl
};

static struct lnbh25_config lnbh25_conf = {
	.i2c_address = 0x10,
	.data2_config = LNBH25_TEN | LNBH25_EXTM
};

static int netup_unidvb_tuner_ctrl(void *priv, int is_dvb_tc)
{
	u8 reg, mask;
	struct netup_dma *dma = priv;
	struct netup_unidvb_dev *ndev;

	if (!priv)
		return -EINVAL;
	ndev = dma->ndev;
	dev_dbg(&ndev->pci_dev->dev, "%s(): num %d is_dvb_tc %d\n",
		__func__, dma->num, is_dvb_tc);
	reg = readb(ndev->bmmio0 + GPIO_REG_IO);
	mask = (dma->num == 0) ? GPIO_RFA_CTL : GPIO_RFB_CTL;

	/* inverted tuner control in hw rev. 1.4 */
	if (ndev->rev == NETUP_HW_REV_1_4)
		is_dvb_tc = !is_dvb_tc;

	if (!is_dvb_tc)
		reg |= mask;
	else
		reg &= ~mask;
	writeb(reg, ndev->bmmio0 + GPIO_REG_IO);
	return 0;
}

static void netup_unidvb_dev_enable(struct netup_unidvb_dev *ndev)
{
	u16 gpio_reg;

	/* enable PCI-E interrupts */
	writel(AVL_IRQ_ENABLE, ndev->bmmio0 + AVL_PCIE_IENR);
	/* unreset frontends bits[0:1] */
	writeb(0x00, ndev->bmmio0 + GPIO_REG_IO);
	msleep(100);
	gpio_reg =
		GPIO_FEA_RESET | GPIO_FEB_RESET |
		GPIO_FEA_TU_RESET | GPIO_FEB_TU_RESET |
		GPIO_RFA_CTL | GPIO_RFB_CTL;
	writeb(gpio_reg, ndev->bmmio0 + GPIO_REG_IO);
	dev_dbg(&ndev->pci_dev->dev,
		"%s(): AVL_PCIE_IENR 0x%x GPIO_REG_IO 0x%x\n",
		__func__, readl(ndev->bmmio0 + AVL_PCIE_IENR),
		(int)readb(ndev->bmmio0 + GPIO_REG_IO));

}

static void netup_unidvb_dma_enable(struct netup_dma *dma, int enable)
{
	u32 irq_mask = (dma->num == 0 ?
		NETUP_UNIDVB_IRQ_DMA1 : NETUP_UNIDVB_IRQ_DMA2);

	dev_dbg(&dma->ndev->pci_dev->dev,
		"%s(): DMA%d enable %d\n", __func__, dma->num, enable);
	if (enable) {
		writel(BIT_DMA_RUN, &dma->regs->ctrlstat_set);
		writew(irq_mask, dma->ndev->bmmio0 + REG_IMASK_SET);
	} else {
		writel(BIT_DMA_RUN, &dma->regs->ctrlstat_clear);
		writew(irq_mask, dma->ndev->bmmio0 + REG_IMASK_CLEAR);
	}
}

static irqreturn_t netup_dma_interrupt(struct netup_dma *dma)
{
	u64 addr_curr;
	u32 size;
	unsigned long flags;
	struct device *dev = &dma->ndev->pci_dev->dev;

	spin_lock_irqsave(&dma->lock, flags);
	addr_curr = ((u64)readl(&dma->regs->curr_addr_hi) << 32) |
		(u64)readl(&dma->regs->curr_addr_lo) | dma->high_addr;
	/* clear IRQ */
	writel(BIT_DMA_IRQ, &dma->regs->ctrlstat_clear);
	/* sanity check */
	if (addr_curr < dma->addr_phys ||
			addr_curr > dma->addr_phys +  dma->ring_buffer_size) {
		if (addr_curr != 0) {
			dev_err(dev,
				"%s(): addr 0x%llx not from 0x%llx:0x%llx\n",
				__func__, addr_curr, (u64)dma->addr_phys,
				(u64)(dma->addr_phys + dma->ring_buffer_size));
		}
		goto irq_handled;
	}
	size = (addr_curr >= dma->addr_last) ?
		(u32)(addr_curr - dma->addr_last) :
		(u32)(dma->ring_buffer_size - (dma->addr_last - addr_curr));
	if (dma->data_size != 0) {
		printk_ratelimited("%s(): lost interrupt, data size %d\n",
			__func__, dma->data_size);
		dma->data_size += size;
	}
	if (dma->data_size == 0 || dma->data_size > dma->ring_buffer_size) {
		dma->data_size = size;
		dma->data_offset = (u32)(dma->addr_last - dma->addr_phys);
	}
	dma->addr_last = addr_curr;
	queue_work(dma->ndev->wq, &dma->work);
irq_handled:
	spin_unlock_irqrestore(&dma->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t netup_unidvb_isr(int irq, void *dev_id)
{
	struct pci_dev *pci_dev = (struct pci_dev *)dev_id;
	struct netup_unidvb_dev *ndev = pci_get_drvdata(pci_dev);
	u32 reg40, reg_isr;
	irqreturn_t iret = IRQ_NONE;

	/* disable interrupts */
	writel(0, ndev->bmmio0 + AVL_PCIE_IENR);
	/* check IRQ source */
	reg40 = readl(ndev->bmmio0 + AVL_PCIE_ISR);
	if ((reg40 & AVL_IRQ_ASSERTED) != 0) {
		/* IRQ is being signaled */
		reg_isr = readw(ndev->bmmio0 + REG_ISR);
		if (reg_isr & NETUP_UNIDVB_IRQ_I2C0) {
			iret = netup_i2c_interrupt(&ndev->i2c[0]);
		} else if (reg_isr & NETUP_UNIDVB_IRQ_I2C1) {
			iret = netup_i2c_interrupt(&ndev->i2c[1]);
		} else if (reg_isr & NETUP_UNIDVB_IRQ_SPI) {
			iret = netup_spi_interrupt(ndev->spi);
		} else if (reg_isr & NETUP_UNIDVB_IRQ_DMA1) {
			iret = netup_dma_interrupt(&ndev->dma[0]);
		} else if (reg_isr & NETUP_UNIDVB_IRQ_DMA2) {
			iret = netup_dma_interrupt(&ndev->dma[1]);
		} else if (reg_isr & NETUP_UNIDVB_IRQ_CI) {
			iret = netup_ci_interrupt(ndev);
		} else {
			dev_err(&pci_dev->dev,
				"%s(): unknown interrupt 0x%x\n",
				__func__, reg_isr);
		}
	}
	/* re-enable interrupts */
	writel(AVL_IRQ_ENABLE, ndev->bmmio0 + AVL_PCIE_IENR);
	return iret;
}

static int netup_unidvb_queue_setup(struct vb2_queue *vq,
				    unsigned int *nbuffers,
				    unsigned int *nplanes,
				    unsigned int sizes[],
				    struct device *alloc_devs[])
{
	struct netup_dma *dma = vb2_get_drv_priv(vq);

	dev_dbg(&dma->ndev->pci_dev->dev, "%s()\n", __func__);

	*nplanes = 1;
	if (vq->num_buffers + *nbuffers < VIDEO_MAX_FRAME)
		*nbuffers = VIDEO_MAX_FRAME - vq->num_buffers;
	sizes[0] = PAGE_ALIGN(NETUP_DMA_PACKETS_COUNT * 188);
	dev_dbg(&dma->ndev->pci_dev->dev, "%s() nbuffers=%d sizes[0]=%d\n",
		__func__, *nbuffers, sizes[0]);
	return 0;
}

static int netup_unidvb_buf_prepare(struct vb2_buffer *vb)
{
	struct netup_dma *dma = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct netup_unidvb_buffer *buf = container_of(vbuf,
				struct netup_unidvb_buffer, vb);

	dev_dbg(&dma->ndev->pci_dev->dev, "%s(): buf 0x%p\n", __func__, buf);
	buf->size = 0;
	return 0;
}

static void netup_unidvb_buf_queue(struct vb2_buffer *vb)
{
	unsigned long flags;
	struct netup_dma *dma = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct netup_unidvb_buffer *buf = container_of(vbuf,
				struct netup_unidvb_buffer, vb);

	dev_dbg(&dma->ndev->pci_dev->dev, "%s(): %p\n", __func__, buf);
	spin_lock_irqsave(&dma->lock, flags);
	list_add_tail(&buf->list, &dma->free_buffers);
	spin_unlock_irqrestore(&dma->lock, flags);
	mod_timer(&dma->timeout, jiffies + msecs_to_jiffies(1000));
}

static int netup_unidvb_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct netup_dma *dma = vb2_get_drv_priv(q);

	dev_dbg(&dma->ndev->pci_dev->dev, "%s()\n", __func__);
	netup_unidvb_dma_enable(dma, 1);
	return 0;
}

static void netup_unidvb_stop_streaming(struct vb2_queue *q)
{
	struct netup_dma *dma = vb2_get_drv_priv(q);

	dev_dbg(&dma->ndev->pci_dev->dev, "%s()\n", __func__);
	netup_unidvb_dma_enable(dma, 0);
	netup_unidvb_queue_cleanup(dma);
}

static const struct vb2_ops dvb_qops = {
	.queue_setup		= netup_unidvb_queue_setup,
	.buf_prepare		= netup_unidvb_buf_prepare,
	.buf_queue		= netup_unidvb_buf_queue,
	.start_streaming	= netup_unidvb_start_streaming,
	.stop_streaming		= netup_unidvb_stop_streaming,
};

static int netup_unidvb_queue_init(struct netup_dma *dma,
				   struct vb2_queue *vb_queue)
{
	int res;

	/* Init videobuf2 queue structure */
	vb_queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vb_queue->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	vb_queue->drv_priv = dma;
	vb_queue->buf_struct_size = sizeof(struct netup_unidvb_buffer);
	vb_queue->ops = &dvb_qops;
	vb_queue->mem_ops = &vb2_vmalloc_memops;
	vb_queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	res = vb2_queue_init(vb_queue);
	if (res != 0) {
		dev_err(&dma->ndev->pci_dev->dev,
			"%s(): vb2_queue_init failed (%d)\n", __func__, res);
	}
	return res;
}

static int netup_unidvb_dvb_init(struct netup_unidvb_dev *ndev,
				 int num)
{
	int fe_count = 2;
	int i = 0;
	struct vb2_dvb_frontend *fes[2];
	u8 fe_name[32];

	if (ndev->rev == NETUP_HW_REV_1_3)
		demod_config.xtal = SONY_XTAL_20500;
	else
		demod_config.xtal = SONY_XTAL_24000;

	if (num < 0 || num > 1) {
		dev_dbg(&ndev->pci_dev->dev,
			"%s(): unable to init DVB bus %d\n", __func__, num);
		return -ENODEV;
	}
	mutex_init(&ndev->frontends[num].lock);
	INIT_LIST_HEAD(&ndev->frontends[num].felist);

	for (i = 0; i < fe_count; i++) {
		if (vb2_dvb_alloc_frontend(&ndev->frontends[num], i+1)
				== NULL) {
			dev_err(&ndev->pci_dev->dev,
					"%s(): unable to allocate vb2_dvb_frontend\n",
					__func__);
			return -ENOMEM;
		}
	}

	for (i = 0; i < fe_count; i++) {
		fes[i] = vb2_dvb_get_frontend(&ndev->frontends[num], i+1);
		if (fes[i] == NULL) {
			dev_err(&ndev->pci_dev->dev,
				"%s(): frontends has not been allocated\n",
				__func__);
			return -EINVAL;
		}
	}

	for (i = 0; i < fe_count; i++) {
		netup_unidvb_queue_init(&ndev->dma[num], &fes[i]->dvb.dvbq);
		snprintf(fe_name, sizeof(fe_name), "netup_fe%d", i);
		fes[i]->dvb.name = fe_name;
	}

	fes[0]->dvb.frontend = dvb_attach(cxd2841er_attach_s,
		&demod_config, &ndev->i2c[num].adap);
	if (fes[0]->dvb.frontend == NULL) {
		dev_dbg(&ndev->pci_dev->dev,
			"%s(): unable to attach DVB-S/S2 frontend\n",
			__func__);
		goto frontend_detach;
	}

	if (ndev->rev == NETUP_HW_REV_1_3) {
		horus3a_conf.set_tuner_priv = &ndev->dma[num];
		if (!dvb_attach(horus3a_attach, fes[0]->dvb.frontend,
					&horus3a_conf, &ndev->i2c[num].adap)) {
			dev_dbg(&ndev->pci_dev->dev,
					"%s(): unable to attach HORUS3A DVB-S/S2 tuner frontend\n",
					__func__);
			goto frontend_detach;
		}
	} else {
		helene_conf.set_tuner_priv = &ndev->dma[num];
		if (!dvb_attach(helene_attach_s, fes[0]->dvb.frontend,
					&helene_conf, &ndev->i2c[num].adap)) {
			dev_err(&ndev->pci_dev->dev,
					"%s(): unable to attach HELENE DVB-S/S2 tuner frontend\n",
					__func__);
			goto frontend_detach;
		}
	}

	if (!dvb_attach(lnbh25_attach, fes[0]->dvb.frontend,
			&lnbh25_conf, &ndev->i2c[num].adap)) {
		dev_dbg(&ndev->pci_dev->dev,
			"%s(): unable to attach SEC frontend\n", __func__);
		goto frontend_detach;
	}

	/* DVB-T/T2 frontend */
	fes[1]->dvb.frontend = dvb_attach(cxd2841er_attach_t_c,
		&demod_config, &ndev->i2c[num].adap);
	if (fes[1]->dvb.frontend == NULL) {
		dev_dbg(&ndev->pci_dev->dev,
			"%s(): unable to attach Ter frontend\n", __func__);
		goto frontend_detach;
	}
	fes[1]->dvb.frontend->id = 1;
	if (ndev->rev == NETUP_HW_REV_1_3) {
		ascot2e_conf.set_tuner_priv = &ndev->dma[num];
		if (!dvb_attach(ascot2e_attach, fes[1]->dvb.frontend,
					&ascot2e_conf, &ndev->i2c[num].adap)) {
			dev_dbg(&ndev->pci_dev->dev,
					"%s(): unable to attach Ter tuner frontend\n",
					__func__);
			goto frontend_detach;
		}
	} else {
		helene_conf.set_tuner_priv = &ndev->dma[num];
		if (!dvb_attach(helene_attach, fes[1]->dvb.frontend,
					&helene_conf, &ndev->i2c[num].adap)) {
			dev_err(&ndev->pci_dev->dev,
					"%s(): unable to attach HELENE Ter tuner frontend\n",
					__func__);
			goto frontend_detach;
		}
	}

	if (vb2_dvb_register_bus(&ndev->frontends[num],
				 THIS_MODULE, NULL,
				 &ndev->pci_dev->dev, NULL, adapter_nr, 1)) {
		dev_dbg(&ndev->pci_dev->dev,
			"%s(): unable to register DVB bus %d\n",
			__func__, num);
		goto frontend_detach;
	}
	dev_info(&ndev->pci_dev->dev, "DVB init done, num=%d\n", num);
	return 0;
frontend_detach:
	vb2_dvb_dealloc_frontends(&ndev->frontends[num]);
	return -EINVAL;
}

static void netup_unidvb_dvb_fini(struct netup_unidvb_dev *ndev, int num)
{
	if (num < 0 || num > 1) {
		dev_err(&ndev->pci_dev->dev,
			"%s(): unable to unregister DVB bus %d\n",
			__func__, num);
		return;
	}
	vb2_dvb_unregister_bus(&ndev->frontends[num]);
	dev_info(&ndev->pci_dev->dev,
		"%s(): DVB bus %d unregistered\n", __func__, num);
}

static int netup_unidvb_dvb_setup(struct netup_unidvb_dev *ndev)
{
	int res;

	res = netup_unidvb_dvb_init(ndev, 0);
	if (res)
		return res;
	res = netup_unidvb_dvb_init(ndev, 1);
	if (res) {
		netup_unidvb_dvb_fini(ndev, 0);
		return res;
	}
	return 0;
}

static int netup_unidvb_ring_copy(struct netup_dma *dma,
				  struct netup_unidvb_buffer *buf)
{
	u32 copy_bytes, ring_bytes;
	u32 buff_bytes = NETUP_DMA_PACKETS_COUNT * 188 - buf->size;
	u8 *p = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	struct netup_unidvb_dev *ndev = dma->ndev;

	if (p == NULL) {
		dev_err(&ndev->pci_dev->dev,
			"%s(): buffer is NULL\n", __func__);
		return -EINVAL;
	}
	p += buf->size;
	if (dma->data_offset + dma->data_size > dma->ring_buffer_size) {
		ring_bytes = dma->ring_buffer_size - dma->data_offset;
		copy_bytes = (ring_bytes > buff_bytes) ?
			buff_bytes : ring_bytes;
		memcpy_fromio(p, (u8 __iomem *)(dma->addr_virt + dma->data_offset), copy_bytes);
		p += copy_bytes;
		buf->size += copy_bytes;
		buff_bytes -= copy_bytes;
		dma->data_size -= copy_bytes;
		dma->data_offset += copy_bytes;
		if (dma->data_offset == dma->ring_buffer_size)
			dma->data_offset = 0;
	}
	if (buff_bytes > 0) {
		ring_bytes = dma->data_size;
		copy_bytes = (ring_bytes > buff_bytes) ?
				buff_bytes : ring_bytes;
		memcpy_fromio(p, (u8 __iomem *)(dma->addr_virt + dma->data_offset), copy_bytes);
		buf->size += copy_bytes;
		dma->data_size -= copy_bytes;
		dma->data_offset += copy_bytes;
		if (dma->data_offset == dma->ring_buffer_size)
			dma->data_offset = 0;
	}
	return 0;
}

static void netup_unidvb_dma_worker(struct work_struct *work)
{
	struct netup_dma *dma = container_of(work, struct netup_dma, work);
	struct netup_unidvb_dev *ndev = dma->ndev;
	struct netup_unidvb_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&dma->lock, flags);
	if (dma->data_size == 0) {
		dev_dbg(&ndev->pci_dev->dev,
			"%s(): data_size == 0\n", __func__);
		goto work_done;
	}
	while (dma->data_size > 0) {
		if (list_empty(&dma->free_buffers)) {
			dev_dbg(&ndev->pci_dev->dev,
				"%s(): no free buffers\n", __func__);
			goto work_done;
		}
		buf = list_first_entry(&dma->free_buffers,
			struct netup_unidvb_buffer, list);
		if (buf->size >= NETUP_DMA_PACKETS_COUNT * 188) {
			dev_dbg(&ndev->pci_dev->dev,
				"%s(): buffer overflow, size %d\n",
				__func__, buf->size);
			goto work_done;
		}
		if (netup_unidvb_ring_copy(dma, buf))
			goto work_done;
		if (buf->size == NETUP_DMA_PACKETS_COUNT * 188) {
			list_del(&buf->list);
			dev_dbg(&ndev->pci_dev->dev,
				"%s(): buffer %p done, size %d\n",
				__func__, buf, buf->size);
			buf->vb.vb2_buf.timestamp = ktime_get_ns();
			vb2_set_plane_payload(&buf->vb.vb2_buf, 0, buf->size);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		}
	}
work_done:
	dma->data_size = 0;
	spin_unlock_irqrestore(&dma->lock, flags);
}

static void netup_unidvb_queue_cleanup(struct netup_dma *dma)
{
	struct netup_unidvb_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&dma->lock, flags);
	while (!list_empty(&dma->free_buffers)) {
		buf = list_first_entry(&dma->free_buffers,
			struct netup_unidvb_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dma->lock, flags);
}

static void netup_unidvb_dma_timeout(struct timer_list *t)
{
	struct netup_dma *dma = from_timer(dma, t, timeout);
	struct netup_unidvb_dev *ndev = dma->ndev;

	dev_dbg(&ndev->pci_dev->dev, "%s()\n", __func__);
	netup_unidvb_queue_cleanup(dma);
}

static int netup_unidvb_dma_init(struct netup_unidvb_dev *ndev, int num)
{
	struct netup_dma *dma;
	struct device *dev = &ndev->pci_dev->dev;

	if (num < 0 || num > 1) {
		dev_err(dev, "%s(): unable to register DMA%d\n",
			__func__, num);
		return -ENODEV;
	}
	dma = &ndev->dma[num];
	dev_info(dev, "%s(): starting DMA%d\n", __func__, num);
	dma->num = num;
	dma->ndev = ndev;
	spin_lock_init(&dma->lock);
	INIT_WORK(&dma->work, netup_unidvb_dma_worker);
	INIT_LIST_HEAD(&dma->free_buffers);
	timer_setup(&dma->timeout, netup_unidvb_dma_timeout, 0);
	dma->ring_buffer_size = ndev->dma_size / 2;
	dma->addr_virt = ndev->dma_virt + dma->ring_buffer_size * num;
	dma->addr_phys = (dma_addr_t)((u64)ndev->dma_phys +
		dma->ring_buffer_size * num);
	dev_info(dev, "%s(): DMA%d buffer virt/phys 0x%p/0x%llx size %d\n",
		__func__, num, dma->addr_virt,
		(unsigned long long)dma->addr_phys,
		dma->ring_buffer_size);
	memset_io((u8 __iomem *)dma->addr_virt, 0, dma->ring_buffer_size);
	dma->addr_last = dma->addr_phys;
	dma->high_addr = (u32)(dma->addr_phys & 0xC0000000);
	dma->regs = (struct netup_dma_regs __iomem *)(num == 0 ?
		ndev->bmmio0 + NETUP_DMA0_ADDR :
		ndev->bmmio0 + NETUP_DMA1_ADDR);
	writel((NETUP_DMA_BLOCKS_COUNT << 24) |
		(NETUP_DMA_PACKETS_COUNT << 8) | 188, &dma->regs->size);
	writel((u32)(dma->addr_phys & 0x3FFFFFFF), &dma->regs->start_addr_lo);
	writel(0, &dma->regs->start_addr_hi);
	writel(dma->high_addr, ndev->bmmio0 + 0x1000);
	writel(375000000, &dma->regs->timeout);
	msleep(1000);
	writel(BIT_DMA_IRQ, &dma->regs->ctrlstat_clear);
	return 0;
}

static void netup_unidvb_dma_fini(struct netup_unidvb_dev *ndev, int num)
{
	struct netup_dma *dma;

	if (num < 0 || num > 1)
		return;
	dev_dbg(&ndev->pci_dev->dev, "%s(): num %d\n", __func__, num);
	dma = &ndev->dma[num];
	netup_unidvb_dma_enable(dma, 0);
	msleep(50);
	cancel_work_sync(&dma->work);
	del_timer(&dma->timeout);
}

static int netup_unidvb_dma_setup(struct netup_unidvb_dev *ndev)
{
	int res;

	res = netup_unidvb_dma_init(ndev, 0);
	if (res)
		return res;
	res = netup_unidvb_dma_init(ndev, 1);
	if (res) {
		netup_unidvb_dma_fini(ndev, 0);
		return res;
	}
	netup_unidvb_dma_enable(&ndev->dma[0], 0);
	netup_unidvb_dma_enable(&ndev->dma[1], 0);
	return 0;
}

static int netup_unidvb_ci_setup(struct netup_unidvb_dev *ndev,
				 struct pci_dev *pci_dev)
{
	int res;

	writew(NETUP_UNIDVB_IRQ_CI, ndev->bmmio0 + REG_IMASK_SET);
	res = netup_unidvb_ci_register(ndev, 0, pci_dev);
	if (res)
		return res;
	res = netup_unidvb_ci_register(ndev, 1, pci_dev);
	if (res)
		netup_unidvb_ci_unregister(ndev, 0);
	return res;
}

static int netup_unidvb_request_mmio(struct pci_dev *pci_dev)
{
	if (!request_mem_region(pci_resource_start(pci_dev, 0),
			pci_resource_len(pci_dev, 0), NETUP_UNIDVB_NAME)) {
		dev_err(&pci_dev->dev,
			"%s(): unable to request MMIO bar 0 at 0x%llx\n",
			__func__,
			(unsigned long long)pci_resource_start(pci_dev, 0));
		return -EBUSY;
	}
	if (!request_mem_region(pci_resource_start(pci_dev, 1),
			pci_resource_len(pci_dev, 1), NETUP_UNIDVB_NAME)) {
		dev_err(&pci_dev->dev,
			"%s(): unable to request MMIO bar 1 at 0x%llx\n",
			__func__,
			(unsigned long long)pci_resource_start(pci_dev, 1));
		release_mem_region(pci_resource_start(pci_dev, 0),
			pci_resource_len(pci_dev, 0));
		return -EBUSY;
	}
	return 0;
}

static int netup_unidvb_request_modules(struct device *dev)
{
	static const char * const modules[] = {
		"lnbh25", "ascot2e", "horus3a", "cxd2841er", "helene", NULL
	};
	const char * const *curr_mod = modules;
	int err;

	while (*curr_mod != NULL) {
		err = request_module(*curr_mod);
		if (err) {
			dev_warn(dev, "request_module(%s) failed: %d\n",
				*curr_mod, err);
		}
		++curr_mod;
	}
	return 0;
}

static int netup_unidvb_initdev(struct pci_dev *pci_dev,
				const struct pci_device_id *pci_id)
{
	u8 board_revision;
	u16 board_vendor;
	struct netup_unidvb_dev *ndev;
	int old_firmware = 0;

	netup_unidvb_request_modules(&pci_dev->dev);

	/* Check card revision */
	if (pci_dev->revision != NETUP_PCI_DEV_REVISION) {
		dev_err(&pci_dev->dev,
			"netup_unidvb: expected card revision %d, got %d\n",
			NETUP_PCI_DEV_REVISION, pci_dev->revision);
		dev_err(&pci_dev->dev,
			"Please upgrade firmware!\n");
		dev_err(&pci_dev->dev,
			"Instructions on http://www.netup.tv\n");
		old_firmware = 1;
		spi_enable = 1;
	}

	/* allocate device context */
	ndev = kzalloc(sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		goto dev_alloc_err;

	/* detect hardware revision */
	if (pci_dev->device == NETUP_HW_REV_1_3)
		ndev->rev = NETUP_HW_REV_1_3;
	else
		ndev->rev = NETUP_HW_REV_1_4;

	dev_info(&pci_dev->dev,
		"%s(): board (0x%x) hardware revision 0x%x\n",
		__func__, pci_dev->device, ndev->rev);

	ndev->old_fw = old_firmware;
	ndev->wq = create_singlethread_workqueue(NETUP_UNIDVB_NAME);
	if (!ndev->wq) {
		dev_err(&pci_dev->dev,
			"%s(): unable to create workqueue\n", __func__);
		goto wq_create_err;
	}
	ndev->pci_dev = pci_dev;
	ndev->pci_bus = pci_dev->bus->number;
	ndev->pci_slot = PCI_SLOT(pci_dev->devfn);
	ndev->pci_func = PCI_FUNC(pci_dev->devfn);
	ndev->board_num = ndev->pci_bus*10 + ndev->pci_slot;
	pci_set_drvdata(pci_dev, ndev);
	/* PCI init */
	dev_info(&pci_dev->dev, "%s(): PCI device (%d). Bus:0x%x Slot:0x%x\n",
		__func__, ndev->board_num, ndev->pci_bus, ndev->pci_slot);

	if (pci_enable_device(pci_dev)) {
		dev_err(&pci_dev->dev, "%s(): pci_enable_device failed\n",
			__func__);
		goto pci_enable_err;
	}
	/* read PCI info */
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &board_revision);
	pci_read_config_word(pci_dev, PCI_VENDOR_ID, &board_vendor);
	if (board_vendor != NETUP_VENDOR_ID) {
		dev_err(&pci_dev->dev, "%s(): unknown board vendor 0x%x",
			__func__, board_vendor);
		goto pci_detect_err;
	}
	dev_info(&pci_dev->dev,
		"%s(): board vendor 0x%x, revision 0x%x\n",
		__func__, board_vendor, board_revision);
	pci_set_master(pci_dev);
	if (pci_set_dma_mask(pci_dev, 0xffffffff) < 0) {
		dev_err(&pci_dev->dev,
			"%s(): 32bit PCI DMA is not supported\n", __func__);
		goto pci_detect_err;
	}
	dev_info(&pci_dev->dev, "%s(): using 32bit PCI DMA\n", __func__);
	/* Clear "no snoop" and "relaxed ordering" bits, use default MRRS. */
	pcie_capability_clear_and_set_word(pci_dev, PCI_EXP_DEVCTL,
		PCI_EXP_DEVCTL_READRQ | PCI_EXP_DEVCTL_RELAX_EN |
		PCI_EXP_DEVCTL_NOSNOOP_EN, 0);
	/* Adjust PCIe completion timeout. */
	pcie_capability_clear_and_set_word(pci_dev,
		PCI_EXP_DEVCTL2, PCI_EXP_DEVCTL2_COMP_TIMEOUT, 0x2);

	if (netup_unidvb_request_mmio(pci_dev)) {
		dev_err(&pci_dev->dev,
			"%s(): unable to request MMIO regions\n", __func__);
		goto pci_detect_err;
	}
	ndev->lmmio0 = ioremap(pci_resource_start(pci_dev, 0),
		pci_resource_len(pci_dev, 0));
	if (!ndev->lmmio0) {
		dev_err(&pci_dev->dev,
			"%s(): unable to remap MMIO bar 0\n", __func__);
		goto pci_bar0_error;
	}
	ndev->lmmio1 = ioremap(pci_resource_start(pci_dev, 1),
		pci_resource_len(pci_dev, 1));
	if (!ndev->lmmio1) {
		dev_err(&pci_dev->dev,
			"%s(): unable to remap MMIO bar 1\n", __func__);
		goto pci_bar1_error;
	}
	ndev->bmmio0 = (u8 __iomem *)ndev->lmmio0;
	ndev->bmmio1 = (u8 __iomem *)ndev->lmmio1;
	dev_info(&pci_dev->dev,
		"%s(): PCI MMIO at 0x%p (%d); 0x%p (%d); IRQ %d",
		__func__,
		ndev->lmmio0, (u32)pci_resource_len(pci_dev, 0),
		ndev->lmmio1, (u32)pci_resource_len(pci_dev, 1),
		pci_dev->irq);
	if (request_irq(pci_dev->irq, netup_unidvb_isr, IRQF_SHARED,
			"netup_unidvb", pci_dev) < 0) {
		dev_err(&pci_dev->dev,
			"%s(): can't get IRQ %d\n", __func__, pci_dev->irq);
		goto irq_request_err;
	}
	ndev->dma_size = 2 * 188 *
		NETUP_DMA_BLOCKS_COUNT * NETUP_DMA_PACKETS_COUNT;
	ndev->dma_virt = dma_alloc_coherent(&pci_dev->dev,
		ndev->dma_size, &ndev->dma_phys, GFP_KERNEL);
	if (!ndev->dma_virt) {
		dev_err(&pci_dev->dev, "%s(): unable to allocate DMA buffer\n",
			__func__);
		goto dma_alloc_err;
	}
	netup_unidvb_dev_enable(ndev);
	if (spi_enable && netup_spi_init(ndev)) {
		dev_warn(&pci_dev->dev,
			"netup_unidvb: SPI flash setup failed\n");
		goto spi_setup_err;
	}
	if (old_firmware) {
		dev_err(&pci_dev->dev,
			"netup_unidvb: card initialization was incomplete\n");
		return 0;
	}
	if (netup_i2c_register(ndev)) {
		dev_err(&pci_dev->dev, "netup_unidvb: I2C setup failed\n");
		goto i2c_setup_err;
	}
	/* enable I2C IRQs */
	writew(NETUP_UNIDVB_IRQ_I2C0 | NETUP_UNIDVB_IRQ_I2C1,
		ndev->bmmio0 + REG_IMASK_SET);
	usleep_range(5000, 10000);
	if (netup_unidvb_dvb_setup(ndev)) {
		dev_err(&pci_dev->dev, "netup_unidvb: DVB setup failed\n");
		goto dvb_setup_err;
	}
	if (netup_unidvb_ci_setup(ndev, pci_dev)) {
		dev_err(&pci_dev->dev, "netup_unidvb: CI setup failed\n");
		goto ci_setup_err;
	}
	if (netup_unidvb_dma_setup(ndev)) {
		dev_err(&pci_dev->dev, "netup_unidvb: DMA setup failed\n");
		goto dma_setup_err;
	}
	dev_info(&pci_dev->dev,
		"netup_unidvb: device has been initialized\n");
	return 0;
dma_setup_err:
	netup_unidvb_ci_unregister(ndev, 0);
	netup_unidvb_ci_unregister(ndev, 1);
ci_setup_err:
	netup_unidvb_dvb_fini(ndev, 0);
	netup_unidvb_dvb_fini(ndev, 1);
dvb_setup_err:
	netup_i2c_unregister(ndev);
i2c_setup_err:
	if (ndev->spi)
		netup_spi_release(ndev);
spi_setup_err:
	dma_free_coherent(&pci_dev->dev, ndev->dma_size,
			ndev->dma_virt, ndev->dma_phys);
dma_alloc_err:
	free_irq(pci_dev->irq, pci_dev);
irq_request_err:
	iounmap(ndev->lmmio1);
pci_bar1_error:
	iounmap(ndev->lmmio0);
pci_bar0_error:
	release_mem_region(pci_resource_start(pci_dev, 0),
		pci_resource_len(pci_dev, 0));
	release_mem_region(pci_resource_start(pci_dev, 1),
		pci_resource_len(pci_dev, 1));
pci_detect_err:
	pci_disable_device(pci_dev);
pci_enable_err:
	pci_set_drvdata(pci_dev, NULL);
	destroy_workqueue(ndev->wq);
wq_create_err:
	kfree(ndev);
dev_alloc_err:
	dev_err(&pci_dev->dev,
		"%s(): failed to initialize device\n", __func__);
	return -EIO;
}

static void netup_unidvb_finidev(struct pci_dev *pci_dev)
{
	struct netup_unidvb_dev *ndev = pci_get_drvdata(pci_dev);

	dev_info(&pci_dev->dev, "%s(): trying to stop device\n", __func__);
	if (!ndev->old_fw) {
		netup_unidvb_dma_fini(ndev, 0);
		netup_unidvb_dma_fini(ndev, 1);
		netup_unidvb_ci_unregister(ndev, 0);
		netup_unidvb_ci_unregister(ndev, 1);
		netup_unidvb_dvb_fini(ndev, 0);
		netup_unidvb_dvb_fini(ndev, 1);
		netup_i2c_unregister(ndev);
	}
	if (ndev->spi)
		netup_spi_release(ndev);
	writew(0xffff, ndev->bmmio0 + REG_IMASK_CLEAR);
	dma_free_coherent(&ndev->pci_dev->dev, ndev->dma_size,
			ndev->dma_virt, ndev->dma_phys);
	free_irq(pci_dev->irq, pci_dev);
	iounmap(ndev->lmmio0);
	iounmap(ndev->lmmio1);
	release_mem_region(pci_resource_start(pci_dev, 0),
		pci_resource_len(pci_dev, 0));
	release_mem_region(pci_resource_start(pci_dev, 1),
		pci_resource_len(pci_dev, 1));
	pci_disable_device(pci_dev);
	pci_set_drvdata(pci_dev, NULL);
	destroy_workqueue(ndev->wq);
	kfree(ndev);
	dev_info(&pci_dev->dev,
		"%s(): device has been successfully stopped\n", __func__);
}


static const struct pci_device_id netup_unidvb_pci_tbl[] = {
	{ PCI_DEVICE(0x1b55, 0x18f6) }, /* hw rev. 1.3 */
	{ PCI_DEVICE(0x1b55, 0x18f7) }, /* hw rev. 1.4 */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, netup_unidvb_pci_tbl);

static struct pci_driver netup_unidvb_pci_driver = {
	.name     = "netup_unidvb",
	.id_table = netup_unidvb_pci_tbl,
	.probe    = netup_unidvb_initdev,
	.remove   = netup_unidvb_finidev,
	.suspend  = NULL,
	.resume   = NULL,
};

module_pci_driver(netup_unidvb_pci_driver);
