/*
 * gsc_hpdi.c
 * Comedi driver the General Standards Corporation
 * High Speed Parallel Digital Interface rs485 boards.
 *
 * Author:  Frank Mori Hess <fmhess@users.sourceforge.net>
 * Copyright (C) 2003 Coherent Imaging Systems
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>
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

/*
 * Driver: gsc_hpdi
 * Description: General Standards Corporation High
 *    Speed Parallel Digital Interface rs485 boards
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Status: only receive mode works, transmit not supported
 * Updated: Thu, 01 Nov 2012 16:17:38 +0000
 * Devices: [General Standards Corporation] PCI-HPDI32 (gsc_hpdi),
 *   PMC-HPDI32
 *
 * Configuration options:
 *    None.
 *
 * Manual configuration of supported devices is not supported; they are
 * configured automatically.
 *
 * There are some additional hpdi models available from GSC for which
 * support could be added to this driver.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "plx9080.h"
#include "comedi_fc.h"

/*
 * PCI BAR2 Register map (devpriv->mmio)
 */
#define FIRMWARE_REV_REG			0x00
#define FEATURES_REG_PRESENT_BIT		(1 << 15)
#define BOARD_CONTROL_REG			0x04
#define BOARD_RESET_BIT				(1 << 0)
#define TX_FIFO_RESET_BIT			(1 << 1)
#define RX_FIFO_RESET_BIT			(1 << 2)
#define TX_ENABLE_BIT				(1 << 4)
#define RX_ENABLE_BIT				(1 << 5)
#define DEMAND_DMA_DIRECTION_TX_BIT		(1 << 6)  /* ch 0 only */
#define LINE_VALID_ON_STATUS_VALID_BIT		(1 << 7)
#define START_TX_BIT				(1 << 8)
#define CABLE_THROTTLE_ENABLE_BIT		(1 << 9)
#define TEST_MODE_ENABLE_BIT			(1 << 31)
#define BOARD_STATUS_REG			0x08
#define COMMAND_LINE_STATUS_MASK		(0x7f << 0)
#define TX_IN_PROGRESS_BIT			(1 << 7)
#define TX_NOT_EMPTY_BIT			(1 << 8)
#define TX_NOT_ALMOST_EMPTY_BIT			(1 << 9)
#define TX_NOT_ALMOST_FULL_BIT			(1 << 10)
#define TX_NOT_FULL_BIT				(1 << 11)
#define RX_NOT_EMPTY_BIT			(1 << 12)
#define RX_NOT_ALMOST_EMPTY_BIT			(1 << 13)
#define RX_NOT_ALMOST_FULL_BIT			(1 << 14)
#define RX_NOT_FULL_BIT				(1 << 15)
#define BOARD_JUMPER0_INSTALLED_BIT		(1 << 16)
#define BOARD_JUMPER1_INSTALLED_BIT		(1 << 17)
#define TX_OVERRUN_BIT				(1 << 21)
#define RX_UNDERRUN_BIT				(1 << 22)
#define RX_OVERRUN_BIT				(1 << 23)
#define TX_PROG_ALMOST_REG			0x0c
#define RX_PROG_ALMOST_REG			0x10
#define ALMOST_EMPTY_BITS(x)			(((x) & 0xffff) << 0)
#define ALMOST_FULL_BITS(x)			(((x) & 0xff) << 16)
#define FEATURES_REG				0x14
#define FIFO_SIZE_PRESENT_BIT			(1 << 0)
#define FIFO_WORDS_PRESENT_BIT			(1 << 1)
#define LEVEL_EDGE_INTERRUPTS_PRESENT_BIT	(1 << 2)
#define GPIO_SUPPORTED_BIT			(1 << 3)
#define PLX_DMA_CH1_SUPPORTED_BIT		(1 << 4)
#define OVERRUN_UNDERRUN_SUPPORTED_BIT		(1 << 5)
#define FIFO_REG				0x18
#define TX_STATUS_COUNT_REG			0x1c
#define TX_LINE_VALID_COUNT_REG			0x20,
#define TX_LINE_INVALID_COUNT_REG		0x24
#define RX_STATUS_COUNT_REG			0x28
#define RX_LINE_COUNT_REG			0x2c
#define INTERRUPT_CONTROL_REG			0x30
#define FRAME_VALID_START_INTR			(1 << 0)
#define FRAME_VALID_END_INTR			(1 << 1)
#define TX_FIFO_EMPTY_INTR			(1 << 8)
#define TX_FIFO_ALMOST_EMPTY_INTR		(1 << 9)
#define TX_FIFO_ALMOST_FULL_INTR		(1 << 10)
#define TX_FIFO_FULL_INTR			(1 << 11)
#define RX_EMPTY_INTR				(1 << 12)
#define RX_ALMOST_EMPTY_INTR			(1 << 13)
#define RX_ALMOST_FULL_INTR			(1 << 14)
#define RX_FULL_INTR				(1 << 15)
#define INTERRUPT_STATUS_REG			0x34
#define TX_CLOCK_DIVIDER_REG			0x38
#define TX_FIFO_SIZE_REG			0x40
#define RX_FIFO_SIZE_REG			0x44
#define FIFO_SIZE_MASK				(0xfffff << 0)
#define TX_FIFO_WORDS_REG			0x48
#define RX_FIFO_WORDS_REG			0x4c
#define INTERRUPT_EDGE_LEVEL_REG		0x50
#define INTERRUPT_POLARITY_REG			0x54

#define TIMER_BASE				50	/* 20MHz master clock */
#define DMA_BUFFER_SIZE				0x10000
#define NUM_DMA_BUFFERS				4
#define NUM_DMA_DESCRIPTORS			256

struct hpdi_board {
	const char *name;
	int device_id;
	int subdevice_id;
};

static const struct hpdi_board hpdi_boards[] = {
	{
		.name		= "pci-hpdi32",
		.device_id	= PCI_DEVICE_ID_PLX_9080,
		.subdevice_id	= 0x2400,
	 },
#if 0
	{
		.name		= "pxi-hpdi32",
		.device_id	= 0x9656,
		.subdevice_id	= 0x2705,
	 },
#endif
};

struct hpdi_private {
	void __iomem *plx9080_mmio;
	void __iomem *mmio;
	uint32_t *dio_buffer[NUM_DMA_BUFFERS];	/*  dma buffers */
	/* physical addresses of dma buffers */
	dma_addr_t dio_buffer_phys_addr[NUM_DMA_BUFFERS];
	/* array of dma descriptors read by plx9080, allocated to get proper
	 * alignment */
	struct plx_dma_desc *dma_desc;
	/* physical address of dma descriptor array */
	dma_addr_t dma_desc_phys_addr;
	unsigned int num_dma_descriptors;
	/* pointer to start of buffers indexed by descriptor */
	uint32_t *desc_dio_buffer[NUM_DMA_DESCRIPTORS];
	/* index of the dma descriptor that is currently being used */
	unsigned int dma_desc_index;
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	unsigned long dio_count;
	/* number of bytes at which to generate COMEDI_CB_BLOCK events */
	unsigned int block_size;
};

static void gsc_hpdi_drain_dma(struct comedi_device *dev, unsigned int channel)
{
	struct hpdi_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int idx;
	unsigned int start;
	unsigned int desc;
	unsigned int size;
	unsigned int next;

	if (channel)
		next = readl(devpriv->plx9080_mmio + PLX_DMA1_PCI_ADDRESS_REG);
	else
		next = readl(devpriv->plx9080_mmio + PLX_DMA0_PCI_ADDRESS_REG);

	idx = devpriv->dma_desc_index;
	start = le32_to_cpu(devpriv->dma_desc[idx].pci_start_addr);
	/* loop until we have read all the full buffers */
	for (desc = 0; (next < start || next >= start + devpriv->block_size) &&
	     desc < devpriv->num_dma_descriptors; desc++) {
		/* transfer data from dma buffer to comedi buffer */
		size = devpriv->block_size / sizeof(uint32_t);
		if (cmd->stop_src == TRIG_COUNT) {
			if (size > devpriv->dio_count)
				size = devpriv->dio_count;
			devpriv->dio_count -= size;
		}
		cfc_write_array_to_buffer(s, devpriv->desc_dio_buffer[idx],
					  size * sizeof(uint32_t));
		idx++;
		idx %= devpriv->num_dma_descriptors;
		start = le32_to_cpu(devpriv->dma_desc[idx].pci_start_addr);

		devpriv->dma_desc_index = idx;
	}
	/*  XXX check for buffer overrun somehow */
}

static irqreturn_t gsc_hpdi_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct hpdi_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	uint32_t hpdi_intr_status, hpdi_board_status;
	uint32_t plx_status;
	uint32_t plx_bits;
	uint8_t dma0_status, dma1_status;
	unsigned long flags;

	if (!dev->attached)
		return IRQ_NONE;

	plx_status = readl(devpriv->plx9080_mmio + PLX_INTRCS_REG);
	if ((plx_status & (ICS_DMA0_A | ICS_DMA1_A | ICS_LIA)) == 0)
		return IRQ_NONE;

	hpdi_intr_status = readl(devpriv->mmio + INTERRUPT_STATUS_REG);
	hpdi_board_status = readl(devpriv->mmio + BOARD_STATUS_REG);

	if (hpdi_intr_status)
		writel(hpdi_intr_status, devpriv->mmio + INTERRUPT_STATUS_REG);

	/*  spin lock makes sure no one else changes plx dma control reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	dma0_status = readb(devpriv->plx9080_mmio + PLX_DMA0_CS_REG);
	if (plx_status & ICS_DMA0_A) {	/*  dma chan 0 interrupt */
		writeb((dma0_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT,
		       devpriv->plx9080_mmio + PLX_DMA0_CS_REG);

		if (dma0_status & PLX_DMA_EN_BIT)
			gsc_hpdi_drain_dma(dev, 0);
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  spin lock makes sure no one else changes plx dma control reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	dma1_status = readb(devpriv->plx9080_mmio + PLX_DMA1_CS_REG);
	if (plx_status & ICS_DMA1_A) {	/*  XXX *//*  dma chan 1 interrupt */
		writeb((dma1_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT,
		       devpriv->plx9080_mmio + PLX_DMA1_CS_REG);
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  clear possible plx9080 interrupt sources */
	if (plx_status & ICS_LDIA) {	/*  clear local doorbell interrupt */
		plx_bits = readl(devpriv->plx9080_mmio + PLX_DBR_OUT_REG);
		writel(plx_bits, devpriv->plx9080_mmio + PLX_DBR_OUT_REG);
	}

	if (hpdi_board_status & RX_OVERRUN_BIT) {
		dev_err(dev->class_dev, "rx fifo overrun\n");
		async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
	}

	if (hpdi_board_status & RX_UNDERRUN_BIT) {
		dev_err(dev->class_dev, "rx fifo underrun\n");
		async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
	}

	if (devpriv->dio_count == 0)
		async->events |= COMEDI_CB_EOA;

	cfc_handle_events(dev, s);

	return IRQ_HANDLED;
}

static void gsc_hpdi_abort_dma(struct comedi_device *dev, unsigned int channel)
{
	struct hpdi_private *devpriv = dev->private;
	unsigned long flags;

	/*  spinlock for plx dma control/status reg */
	spin_lock_irqsave(&dev->spinlock, flags);

	plx9080_abort_dma(devpriv->plx9080_mmio, channel);

	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static int gsc_hpdi_cancel(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct hpdi_private *devpriv = dev->private;

	writel(0, devpriv->mmio + BOARD_CONTROL_REG);
	writel(0, devpriv->mmio + INTERRUPT_CONTROL_REG);

	gsc_hpdi_abort_dma(dev, 0);

	return 0;
}

static int gsc_hpdi_cmd(struct comedi_device *dev,
			struct comedi_subdevice *s)
{
	struct hpdi_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned long flags;
	uint32_t bits;

	if (s->io_bits)
		return -EINVAL;

	writel(RX_FIFO_RESET_BIT, devpriv->mmio + BOARD_CONTROL_REG);

	gsc_hpdi_abort_dma(dev, 0);

	devpriv->dma_desc_index = 0;

	/*
	 * These register are supposedly unused during chained dma,
	 * but I have found that left over values from last operation
	 * occasionally cause problems with transfer of first dma
	 * block.  Initializing them to zero seems to fix the problem.
	 */
	writel(0, devpriv->plx9080_mmio + PLX_DMA0_TRANSFER_SIZE_REG);
	writel(0, devpriv->plx9080_mmio + PLX_DMA0_PCI_ADDRESS_REG);
	writel(0, devpriv->plx9080_mmio + PLX_DMA0_LOCAL_ADDRESS_REG);

	/* give location of first dma descriptor */
	bits = devpriv->dma_desc_phys_addr | PLX_DESC_IN_PCI_BIT |
	       PLX_INTR_TERM_COUNT | PLX_XFER_LOCAL_TO_PCI;
	writel(bits, devpriv->plx9080_mmio + PLX_DMA0_DESCRIPTOR_REG);

	/* enable dma transfer */
	spin_lock_irqsave(&dev->spinlock, flags);
	writeb(PLX_DMA_EN_BIT | PLX_DMA_START_BIT | PLX_CLEAR_DMA_INTR_BIT,
	       devpriv->plx9080_mmio + PLX_DMA0_CS_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	if (cmd->stop_src == TRIG_COUNT)
		devpriv->dio_count = cmd->stop_arg;
	else
		devpriv->dio_count = 1;

	/* clear over/under run status flags */
	writel(RX_UNDERRUN_BIT | RX_OVERRUN_BIT,
	       devpriv->mmio + BOARD_STATUS_REG);

	/* enable interrupts */
	writel(RX_FULL_INTR, devpriv->mmio + INTERRUPT_CONTROL_REG);

	writel(RX_ENABLE_BIT, devpriv->mmio + BOARD_CONTROL_REG);

	return 0;
}

static int gsc_hpdi_check_chanlist(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_cmd *cmd)
{
	int i;

	for (i = 0; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);

		if (chan != i) {
			dev_dbg(dev->class_dev,
				"chanlist must be ch 0 to 31 in order\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int gsc_hpdi_cmd_test(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_cmd *cmd)
{
	int err = 0;

	if (s->io_bits)
		return -EINVAL;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (!cmd->chanlist_len || !cmd->chanlist) {
		cmd->chanlist_len = 32;
		err |= -EINVAL;
	}
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= gsc_hpdi_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;

}

/* setup dma descriptors so a link completes every 'len' bytes */
static int gsc_hpdi_setup_dma_descriptors(struct comedi_device *dev,
					  unsigned int len)
{
	struct hpdi_private *devpriv = dev->private;
	dma_addr_t phys_addr = devpriv->dma_desc_phys_addr;
	uint32_t next_bits = PLX_DESC_IN_PCI_BIT | PLX_INTR_TERM_COUNT |
			     PLX_XFER_LOCAL_TO_PCI;
	unsigned int offset = 0;
	unsigned int idx = 0;
	unsigned int i;

	if (len > DMA_BUFFER_SIZE)
		len = DMA_BUFFER_SIZE;
	len -= len % sizeof(uint32_t);
	if (len == 0)
		return -EINVAL;

	for (i = 0; i < NUM_DMA_DESCRIPTORS && idx < NUM_DMA_BUFFERS; i++) {
		devpriv->dma_desc[i].pci_start_addr =
		    cpu_to_le32(devpriv->dio_buffer_phys_addr[idx] + offset);
		devpriv->dma_desc[i].local_start_addr = cpu_to_le32(FIFO_REG);
		devpriv->dma_desc[i].transfer_size = cpu_to_le32(len);
		devpriv->dma_desc[i].next = cpu_to_le32((phys_addr +
			(i + 1) * sizeof(devpriv->dma_desc[0])) | next_bits);

		devpriv->desc_dio_buffer[i] = devpriv->dio_buffer[idx] +
					      (offset / sizeof(uint32_t));

		offset += len;
		if (len + offset > DMA_BUFFER_SIZE) {
			offset = 0;
			idx++;
		}
	}
	devpriv->num_dma_descriptors = i;
	/* fix last descriptor to point back to first */
	devpriv->dma_desc[i - 1].next = cpu_to_le32(phys_addr | next_bits);

	devpriv->block_size = len;

	return len;
}

static int gsc_hpdi_dio_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	int ret;

	switch (data[0]) {
	case INSN_CONFIG_BLOCK_SIZE:
		ret = gsc_hpdi_setup_dma_descriptors(dev, data[1]);
		if (ret)
			return ret;

		data[1] = ret;
		break;
	default:
		ret = comedi_dio_insn_config(dev, s, insn, data, 0xffffffff);
		if (ret)
			return ret;
		break;
	}

	return insn->n;
}

static int gsc_hpdi_init(struct comedi_device *dev)
{
	struct hpdi_private *devpriv = dev->private;
	uint32_t plx_intcsr_bits;

	/* wait 10usec after reset before accessing fifos */
	writel(BOARD_RESET_BIT, devpriv->mmio + BOARD_CONTROL_REG);
	udelay(10);

	writel(ALMOST_EMPTY_BITS(32) | ALMOST_FULL_BITS(32),
	       devpriv->mmio + RX_PROG_ALMOST_REG);
	writel(ALMOST_EMPTY_BITS(32) | ALMOST_FULL_BITS(32),
	       devpriv->mmio + TX_PROG_ALMOST_REG);

	devpriv->tx_fifo_size = readl(devpriv->mmio + TX_FIFO_SIZE_REG) &
				FIFO_SIZE_MASK;
	devpriv->rx_fifo_size = readl(devpriv->mmio + RX_FIFO_SIZE_REG) &
				FIFO_SIZE_MASK;

	writel(0, devpriv->mmio + INTERRUPT_CONTROL_REG);

	/*  enable interrupts */
	plx_intcsr_bits =
	    ICS_AERR | ICS_PERR | ICS_PIE | ICS_PLIE | ICS_PAIE | ICS_LIE |
	    ICS_DMA0_E;
	writel(plx_intcsr_bits, devpriv->plx9080_mmio + PLX_INTRCS_REG);

	return 0;
}

static void gsc_hpdi_init_plx9080(struct comedi_device *dev)
{
	struct hpdi_private *devpriv = dev->private;
	uint32_t bits;
	void __iomem *plx_iobase = devpriv->plx9080_mmio;

#ifdef __BIG_ENDIAN
	bits = BIGEND_DMA0 | BIGEND_DMA1;
#else
	bits = 0;
#endif
	writel(bits, devpriv->plx9080_mmio + PLX_BIGEND_REG);

	writel(0, devpriv->plx9080_mmio + PLX_INTRCS_REG);

	gsc_hpdi_abort_dma(dev, 0);
	gsc_hpdi_abort_dma(dev, 1);

	/*  configure dma0 mode */
	bits = 0;
	/*  enable ready input */
	bits |= PLX_DMA_EN_READYIN_BIT;
	/*  enable dma chaining */
	bits |= PLX_EN_CHAIN_BIT;
	/*  enable interrupt on dma done
	 *  (probably don't need this, since chain never finishes) */
	bits |= PLX_EN_DMA_DONE_INTR_BIT;
	/*  don't increment local address during transfers
	 *  (we are transferring from a fixed fifo register) */
	bits |= PLX_LOCAL_ADDR_CONST_BIT;
	/*  route dma interrupt to pci bus */
	bits |= PLX_DMA_INTR_PCI_BIT;
	/*  enable demand mode */
	bits |= PLX_DEMAND_MODE_BIT;
	/*  enable local burst mode */
	bits |= PLX_DMA_LOCAL_BURST_EN_BIT;
	bits |= PLX_LOCAL_BUS_32_WIDE_BITS;
	writel(bits, plx_iobase + PLX_DMA0_MODE_REG);
}

static const struct hpdi_board *gsc_hpdi_find_board(struct pci_dev *pcidev)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hpdi_boards); i++)
		if (pcidev->device == hpdi_boards[i].device_id &&
		    pcidev->subsystem_device == hpdi_boards[i].subdevice_id)
			return &hpdi_boards[i];
	return NULL;
}

static int gsc_hpdi_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct hpdi_board *thisboard;
	struct hpdi_private *devpriv;
	struct comedi_subdevice *s;
	int i;
	int retval;

	thisboard = gsc_hpdi_find_board(pcidev);
	if (!thisboard) {
		dev_err(dev->class_dev, "gsc_hpdi: pci %s not supported\n",
			pci_name(pcidev));
		return -EINVAL;
	}
	dev->board_ptr = thisboard;
	dev->board_name = thisboard->name;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	retval = comedi_pci_enable(dev);
	if (retval)
		return retval;
	pci_set_master(pcidev);

	devpriv->plx9080_mmio = pci_ioremap_bar(pcidev, 0);
	devpriv->mmio = pci_ioremap_bar(pcidev, 2);
	if (!devpriv->plx9080_mmio || !devpriv->mmio) {
		dev_warn(dev->class_dev, "failed to remap io memory\n");
		return -ENOMEM;
	}

	gsc_hpdi_init_plx9080(dev);

	/*  get irq */
	if (request_irq(pcidev->irq, gsc_hpdi_interrupt, IRQF_SHARED,
			dev->board_name, dev)) {
		dev_warn(dev->class_dev,
			 "unable to allocate irq %u\n", pcidev->irq);
		return -EINVAL;
	}
	dev->irq = pcidev->irq;

	dev_dbg(dev->class_dev, " irq %u\n", dev->irq);

	/*  allocate pci dma buffers */
	for (i = 0; i < NUM_DMA_BUFFERS; i++) {
		devpriv->dio_buffer[i] =
		    pci_alloc_consistent(pcidev, DMA_BUFFER_SIZE,
					 &devpriv->dio_buffer_phys_addr[i]);
	}
	/*  allocate dma descriptors */
	devpriv->dma_desc = pci_alloc_consistent(pcidev,
						 sizeof(struct plx_dma_desc) *
						 NUM_DMA_DESCRIPTORS,
						 &devpriv->dma_desc_phys_addr);
	if (devpriv->dma_desc_phys_addr & 0xf) {
		dev_warn(dev->class_dev,
			 " dma descriptors not quad-word aligned (bug)\n");
		return -EIO;
	}

	retval = gsc_hpdi_setup_dma_descriptors(dev, 0x1000);
	if (retval < 0)
		return retval;

	retval = comedi_alloc_subdevices(dev, 1);
	if (retval)
		return retval;

	/* Digital I/O subdevice */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITEABLE | SDF_LSAMPL |
			  SDF_CMD_READ;
	s->n_chan	= 32;
	s->len_chanlist	= 32;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_config	= gsc_hpdi_dio_insn_config;
	s->do_cmd	= gsc_hpdi_cmd;
	s->do_cmdtest	= gsc_hpdi_cmd_test;
	s->cancel	= gsc_hpdi_cancel;

	return gsc_hpdi_init(dev);
}

static void gsc_hpdi_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct hpdi_private *devpriv = dev->private;
	unsigned int i;

	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->plx9080_mmio) {
			writel(0, devpriv->plx9080_mmio + PLX_INTRCS_REG);
			iounmap(devpriv->plx9080_mmio);
		}
		if (devpriv->mmio)
			iounmap(devpriv->mmio);
		/*  free pci dma buffers */
		for (i = 0; i < NUM_DMA_BUFFERS; i++) {
			if (devpriv->dio_buffer[i])
				pci_free_consistent(pcidev,
						    DMA_BUFFER_SIZE,
						    devpriv->dio_buffer[i],
						    devpriv->
						    dio_buffer_phys_addr[i]);
		}
		/*  free dma descriptors */
		if (devpriv->dma_desc)
			pci_free_consistent(pcidev,
					    sizeof(struct plx_dma_desc) *
					    NUM_DMA_DESCRIPTORS,
					    devpriv->dma_desc,
					    devpriv->dma_desc_phys_addr);
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver gsc_hpdi_driver = {
	.driver_name	= "gsc_hpdi",
	.module		= THIS_MODULE,
	.auto_attach	= gsc_hpdi_auto_attach,
	.detach		= gsc_hpdi_detach,
};

static int gsc_hpdi_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &gsc_hpdi_driver, id->driver_data);
}

static const struct pci_device_id gsc_hpdi_pci_table[] = {
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9080, PCI_VENDOR_ID_PLX,
		    0x2400, 0, 0, 0},
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, gsc_hpdi_pci_table);

static struct pci_driver gsc_hpdi_pci_driver = {
	.name		= "gsc_hpdi",
	.id_table	= gsc_hpdi_pci_table,
	.probe		= gsc_hpdi_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(gsc_hpdi_driver, gsc_hpdi_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
