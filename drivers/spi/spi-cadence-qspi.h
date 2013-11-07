/*
 * Driver for Cadence QSPI Controller
 *
 * Copyright (C) 2012 Altera Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __CADENCE_QSPI_H__
#define __CADENCE_QSPI_H__

#define CQSPI_MAX_TRANS				(2)

#define CQSPI_MAX_CHIP_SELECT			(16)

struct cqspi_flash_pdata {
	unsigned int page_size;
	unsigned int block_size;
	unsigned int quad;
	unsigned int read_delay;
	unsigned int tshsl_ns;
	unsigned int tsd2d_ns;
	unsigned int tchsh_ns;
	unsigned int tslch_ns;
};

struct cqspi_platform_data {
	unsigned int bus_num;
	unsigned int num_chipselect;
	unsigned int qspi_ahb_phy;
	unsigned int master_ref_clk_hz;
	unsigned int ext_decoder;
	unsigned int fifo_depth;
	unsigned int enable_dma;
	unsigned int tx_dma_peri_id;
	unsigned int rx_dma_peri_id;
	struct cqspi_flash_pdata f_pdata[CQSPI_MAX_CHIP_SELECT];
};

struct struct_cqspi
{
	struct work_struct work;
	struct workqueue_struct *workqueue;
	wait_queue_head_t waitqueue;
	struct list_head msg_queue;
	struct platform_device *pdev;

	/* lock protects queue and registers */
	spinlock_t lock;
	/* Virtual base address of the controller */
	void __iomem *iobase;
	/* QSPI AHB virtual address */
	void __iomem *qspi_ahb_virt;
	/* phys mem */
	struct resource *res;
	/* AHB phys mem */
	struct resource *res_ahb;
	/* Interrupt */
	int irq;
	/* Interrupt status */
	unsigned int irq_status;
	/* Current chip select */
	int current_cs;
	/* Is queue running */
	bool running;
	/* DMA support */
	struct dma_chan *txchan;
	struct dma_chan *rxchan;
	dma_addr_t dma_addr;
	int dma_done;
};

/* Kernel function hook */
#define CQSPI_WRITEL		__raw_writel
#define CQSPI_READL		__raw_readl
unsigned int cadence_qspi_init_timeout(const unsigned long timeout_in_ms);
unsigned int cadence_qspi_check_timeout(const unsigned long timeout);

#endif /* __CADENCE_QSPI_H__ */
