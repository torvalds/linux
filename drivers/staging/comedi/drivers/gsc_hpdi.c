/*
    comedi/drivers/gsc_hpdi.c
    This is a driver for the General Standards Corporation High
    Speed Parallel Digital Interface rs485 boards.

    Author:  Frank Mori Hess <fmhess@users.sourceforge.net>
    Copyright (C) 2003 Coherent Imaging Systems

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

************************************************************************/

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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "plx9080.h"
#include "comedi_fc.h"

static void abort_dma(struct comedi_device *dev, unsigned int channel);
static int hpdi_cmd(struct comedi_device *dev, struct comedi_subdevice *s);
static int hpdi_cmd_test(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_cmd *cmd);
static int hpdi_cancel(struct comedi_device *dev, struct comedi_subdevice *s);
static irqreturn_t handle_interrupt(int irq, void *d);
static int dio_config_block_size(struct comedi_device *dev, unsigned int *data);

#undef HPDI_DEBUG		/*  disable debugging messages */
/* #define HPDI_DEBUG      enable debugging code */

#ifdef HPDI_DEBUG
#define DEBUG_PRINT(format, args...)  pr_debug(format , ## args)
#else
#define DEBUG_PRINT(format, args...)  no_printk(pr_fmt(format), ## args)
#endif

#define TIMER_BASE 50		/*  20MHz master clock */
#define DMA_BUFFER_SIZE 0x10000
#define NUM_DMA_BUFFERS 4
#define NUM_DMA_DESCRIPTORS 256

/* indices of base address regions */
enum base_address_regions {
	PLX9080_BADDRINDEX = 0,
	HPDI_BADDRINDEX = 2,
};

enum hpdi_registers {
	FIRMWARE_REV_REG = 0x0,
	BOARD_CONTROL_REG = 0x4,
	BOARD_STATUS_REG = 0x8,
	TX_PROG_ALMOST_REG = 0xc,
	RX_PROG_ALMOST_REG = 0x10,
	FEATURES_REG = 0x14,
	FIFO_REG = 0x18,
	TX_STATUS_COUNT_REG = 0x1c,
	TX_LINE_VALID_COUNT_REG = 0x20,
	TX_LINE_INVALID_COUNT_REG = 0x24,
	RX_STATUS_COUNT_REG = 0x28,
	RX_LINE_COUNT_REG = 0x2c,
	INTERRUPT_CONTROL_REG = 0x30,
	INTERRUPT_STATUS_REG = 0x34,
	TX_CLOCK_DIVIDER_REG = 0x38,
	TX_FIFO_SIZE_REG = 0x40,
	RX_FIFO_SIZE_REG = 0x44,
	TX_FIFO_WORDS_REG = 0x48,
	RX_FIFO_WORDS_REG = 0x4c,
	INTERRUPT_EDGE_LEVEL_REG = 0x50,
	INTERRUPT_POLARITY_REG = 0x54,
};

/* bit definitions */

enum firmware_revision_bits {
	FEATURES_REG_PRESENT_BIT = 0x8000,
};

enum board_control_bits {
	BOARD_RESET_BIT = 0x1,	/* wait 10usec before accessing fifos */
	TX_FIFO_RESET_BIT = 0x2,
	RX_FIFO_RESET_BIT = 0x4,
	TX_ENABLE_BIT = 0x10,
	RX_ENABLE_BIT = 0x20,
	DEMAND_DMA_DIRECTION_TX_BIT = 0x40,
		/* for ch 0, ch 1 can only transmit (when present) */
	LINE_VALID_ON_STATUS_VALID_BIT = 0x80,
	START_TX_BIT = 0x10,
	CABLE_THROTTLE_ENABLE_BIT = 0x20,
	TEST_MODE_ENABLE_BIT = 0x80000000,
};

enum board_status_bits {
	COMMAND_LINE_STATUS_MASK = 0x7f,
	TX_IN_PROGRESS_BIT = 0x80,
	TX_NOT_EMPTY_BIT = 0x100,
	TX_NOT_ALMOST_EMPTY_BIT = 0x200,
	TX_NOT_ALMOST_FULL_BIT = 0x400,
	TX_NOT_FULL_BIT = 0x800,
	RX_NOT_EMPTY_BIT = 0x1000,
	RX_NOT_ALMOST_EMPTY_BIT = 0x2000,
	RX_NOT_ALMOST_FULL_BIT = 0x4000,
	RX_NOT_FULL_BIT = 0x8000,
	BOARD_JUMPER0_INSTALLED_BIT = 0x10000,
	BOARD_JUMPER1_INSTALLED_BIT = 0x20000,
	TX_OVERRUN_BIT = 0x200000,
	RX_UNDERRUN_BIT = 0x400000,
	RX_OVERRUN_BIT = 0x800000,
};

static uint32_t almost_full_bits(unsigned int num_words)
{
	/* XXX need to add or subtract one? */
	return (num_words << 16) & 0xff0000;
}

static uint32_t almost_empty_bits(unsigned int num_words)
{
	return num_words & 0xffff;
}

enum features_bits {
	FIFO_SIZE_PRESENT_BIT = 0x1,
	FIFO_WORDS_PRESENT_BIT = 0x2,
	LEVEL_EDGE_INTERRUPTS_PRESENT_BIT = 0x4,
	GPIO_SUPPORTED_BIT = 0x8,
	PLX_DMA_CH1_SUPPORTED_BIT = 0x10,
	OVERRUN_UNDERRUN_SUPPORTED_BIT = 0x20,
};

enum interrupt_sources {
	FRAME_VALID_START_INTR = 0,
	FRAME_VALID_END_INTR = 1,
	TX_FIFO_EMPTY_INTR = 8,
	TX_FIFO_ALMOST_EMPTY_INTR = 9,
	TX_FIFO_ALMOST_FULL_INTR = 10,
	TX_FIFO_FULL_INTR = 11,
	RX_EMPTY_INTR = 12,
	RX_ALMOST_EMPTY_INTR = 13,
	RX_ALMOST_FULL_INTR = 14,
	RX_FULL_INTR = 15,
};

static uint32_t intr_bit(int interrupt_source)
{
	return 0x1 << interrupt_source;
}

static unsigned int fifo_size(uint32_t fifo_size_bits)
{
	return fifo_size_bits & 0xfffff;
}

struct hpdi_board {
	const char *name;	/*  board name */
	int device_id;		/*  pci device id */
	int subdevice_id;	/*  pci subdevice id */
};

static const struct hpdi_board hpdi_boards[] = {
	{
	 .name = "pci-hpdi32",
	 .device_id = PCI_DEVICE_ID_PLX_9080,
	 .subdevice_id = 0x2400,
	 },
#if 0
	{
	 .name = "pxi-hpdi32",
	 .device_id = 0x9656,
	 .subdevice_id = 0x2705,
	 },
#endif
};

struct hpdi_private {
	/*  base addresses (ioremapped) */
	void __iomem *plx9080_iobase;
	void __iomem *hpdi_iobase;
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
	volatile unsigned int dma_desc_index;
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	volatile unsigned long dio_count;
	/* software copies of values written to hpdi registers */
	volatile uint32_t bits[24];
	/* number of bytes at which to generate COMEDI_CB_BLOCK events */
	volatile unsigned int block_size;
	unsigned dio_config_output:1;
};

static int dio_config_insn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct hpdi_private *devpriv = dev->private;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		devpriv->dio_config_output = 1;
		return insn->n;
		break;
	case INSN_CONFIG_DIO_INPUT:
		devpriv->dio_config_output = 0;
		return insn->n;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    devpriv->dio_config_output ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	case INSN_CONFIG_BLOCK_SIZE:
		return dio_config_block_size(dev, data);
		break;
	default:
		break;
	}

	return -EINVAL;
}

static void disable_plx_interrupts(struct comedi_device *dev)
{
	struct hpdi_private *devpriv = dev->private;

	writel(0, devpriv->plx9080_iobase + PLX_INTRCS_REG);
}

/* initialize plx9080 chip */
static void init_plx9080(struct comedi_device *dev)
{
	struct hpdi_private *devpriv = dev->private;
	uint32_t bits;
	void __iomem *plx_iobase = devpriv->plx9080_iobase;

	/*  plx9080 dump */
	DEBUG_PRINT(" plx interrupt status 0x%x\n",
		    readl(plx_iobase + PLX_INTRCS_REG));
	DEBUG_PRINT(" plx id bits 0x%x\n", readl(plx_iobase + PLX_ID_REG));
	DEBUG_PRINT(" plx control reg 0x%x\n",
		    readl(devpriv->plx9080_iobase + PLX_CONTROL_REG));

	DEBUG_PRINT(" plx revision 0x%x\n",
		    readl(plx_iobase + PLX_REVISION_REG));
	DEBUG_PRINT(" plx dma channel 0 mode 0x%x\n",
		    readl(plx_iobase + PLX_DMA0_MODE_REG));
	DEBUG_PRINT(" plx dma channel 1 mode 0x%x\n",
		    readl(plx_iobase + PLX_DMA1_MODE_REG));
	DEBUG_PRINT(" plx dma channel 0 pci address 0x%x\n",
		    readl(plx_iobase + PLX_DMA0_PCI_ADDRESS_REG));
	DEBUG_PRINT(" plx dma channel 0 local address 0x%x\n",
		    readl(plx_iobase + PLX_DMA0_LOCAL_ADDRESS_REG));
	DEBUG_PRINT(" plx dma channel 0 transfer size 0x%x\n",
		    readl(plx_iobase + PLX_DMA0_TRANSFER_SIZE_REG));
	DEBUG_PRINT(" plx dma channel 0 descriptor 0x%x\n",
		    readl(plx_iobase + PLX_DMA0_DESCRIPTOR_REG));
	DEBUG_PRINT(" plx dma channel 0 command status 0x%x\n",
		    readb(plx_iobase + PLX_DMA0_CS_REG));
	DEBUG_PRINT(" plx dma channel 0 threshold 0x%x\n",
		    readl(plx_iobase + PLX_DMA0_THRESHOLD_REG));
	DEBUG_PRINT(" plx bigend 0x%x\n", readl(plx_iobase + PLX_BIGEND_REG));
#ifdef __BIG_ENDIAN
	bits = BIGEND_DMA0 | BIGEND_DMA1;
#else
	bits = 0;
#endif
	writel(bits, devpriv->plx9080_iobase + PLX_BIGEND_REG);

	disable_plx_interrupts(dev);

	abort_dma(dev, 0);
	abort_dma(dev, 1);

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

/* Allocate and initialize the subdevice structures.
 */
static int setup_subdevices(struct comedi_device *dev)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog input subdevice */
	dev->read_subdev = s;
/*	dev->write_subdev = s; */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags =
	    SDF_READABLE | SDF_WRITEABLE | SDF_LSAMPL | SDF_CMD_READ;
	s->n_chan = 32;
	s->len_chanlist = 32;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_config = dio_config_insn;
	s->do_cmd = hpdi_cmd;
	s->do_cmdtest = hpdi_cmd_test;
	s->cancel = hpdi_cancel;

	return 0;
}

static int init_hpdi(struct comedi_device *dev)
{
	struct hpdi_private *devpriv = dev->private;
	uint32_t plx_intcsr_bits;

	writel(BOARD_RESET_BIT, devpriv->hpdi_iobase + BOARD_CONTROL_REG);
	udelay(10);

	writel(almost_empty_bits(32) | almost_full_bits(32),
	       devpriv->hpdi_iobase + RX_PROG_ALMOST_REG);
	writel(almost_empty_bits(32) | almost_full_bits(32),
	       devpriv->hpdi_iobase + TX_PROG_ALMOST_REG);

	devpriv->tx_fifo_size = fifo_size(readl(devpriv->hpdi_iobase +
						  TX_FIFO_SIZE_REG));
	devpriv->rx_fifo_size = fifo_size(readl(devpriv->hpdi_iobase +
						  RX_FIFO_SIZE_REG));

	writel(0, devpriv->hpdi_iobase + INTERRUPT_CONTROL_REG);

	/*  enable interrupts */
	plx_intcsr_bits =
	    ICS_AERR | ICS_PERR | ICS_PIE | ICS_PLIE | ICS_PAIE | ICS_LIE |
	    ICS_DMA0_E;
	writel(plx_intcsr_bits, devpriv->plx9080_iobase + PLX_INTRCS_REG);

	return 0;
}

/* setup dma descriptors so a link completes every 'transfer_size' bytes */
static int setup_dma_descriptors(struct comedi_device *dev,
				 unsigned int transfer_size)
{
	struct hpdi_private *devpriv = dev->private;
	unsigned int buffer_index, buffer_offset;
	uint32_t next_bits = PLX_DESC_IN_PCI_BIT | PLX_INTR_TERM_COUNT |
	    PLX_XFER_LOCAL_TO_PCI;
	unsigned int i;

	if (transfer_size > DMA_BUFFER_SIZE)
		transfer_size = DMA_BUFFER_SIZE;
	transfer_size -= transfer_size % sizeof(uint32_t);
	if (transfer_size == 0)
		return -1;

	DEBUG_PRINT(" transfer_size %i\n", transfer_size);
	DEBUG_PRINT(" descriptors at 0x%lx\n",
		    (unsigned long)devpriv->dma_desc_phys_addr);

	buffer_offset = 0;
	buffer_index = 0;
	for (i = 0; i < NUM_DMA_DESCRIPTORS &&
	     buffer_index < NUM_DMA_BUFFERS; i++) {
		devpriv->dma_desc[i].pci_start_addr =
		    cpu_to_le32(devpriv->dio_buffer_phys_addr[buffer_index] +
				buffer_offset);
		devpriv->dma_desc[i].local_start_addr = cpu_to_le32(FIFO_REG);
		devpriv->dma_desc[i].transfer_size =
		    cpu_to_le32(transfer_size);
		devpriv->dma_desc[i].next =
		    cpu_to_le32((devpriv->dma_desc_phys_addr + (i +
								  1) *
				 sizeof(devpriv->dma_desc[0])) | next_bits);

		devpriv->desc_dio_buffer[i] =
		    devpriv->dio_buffer[buffer_index] +
		    (buffer_offset / sizeof(uint32_t));

		buffer_offset += transfer_size;
		if (transfer_size + buffer_offset > DMA_BUFFER_SIZE) {
			buffer_offset = 0;
			buffer_index++;
		}

		DEBUG_PRINT(" desc %i\n", i);
		DEBUG_PRINT(" start addr virt 0x%p, phys 0x%lx\n",
			    devpriv->desc_dio_buffer[i],
			    (unsigned long)devpriv->dma_desc[i].
			    pci_start_addr);
		DEBUG_PRINT(" next 0x%lx\n",
			    (unsigned long)devpriv->dma_desc[i].next);
	}
	devpriv->num_dma_descriptors = i;
	/*  fix last descriptor to point back to first */
	devpriv->dma_desc[i - 1].next =
	    cpu_to_le32(devpriv->dma_desc_phys_addr | next_bits);
	DEBUG_PRINT(" desc %i next fixup 0x%lx\n", i - 1,
		    (unsigned long)devpriv->dma_desc[i - 1].next);

	devpriv->block_size = transfer_size;

	return transfer_size;
}

static const struct hpdi_board *hpdi_find_board(struct pci_dev *pcidev)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hpdi_boards); i++)
		if (pcidev->device == hpdi_boards[i].device_id &&
		    pcidev->subsystem_device == hpdi_boards[i].subdevice_id)
			return &hpdi_boards[i];
	return NULL;
}

static int hpdi_auto_attach(struct comedi_device *dev,
				      unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct hpdi_board *thisboard;
	struct hpdi_private *devpriv;
	int i;
	int retval;

	thisboard = hpdi_find_board(pcidev);
	if (!thisboard) {
		dev_err(dev->class_dev, "gsc_hpdi: pci %s not supported\n",
			pci_name(pcidev));
		return -EINVAL;
	}
	dev->board_ptr = thisboard;
	dev->board_name = thisboard->name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	if (comedi_pci_enable(pcidev, dev->board_name)) {
		dev_warn(dev->class_dev,
			 "failed enable PCI device and request regions\n");
		return -EIO;
	}
	dev->iobase = 1;	/* the "detach" needs this */
	pci_set_master(pcidev);

	devpriv->plx9080_iobase =
		ioremap(pci_resource_start(pcidev, PLX9080_BADDRINDEX),
			pci_resource_len(pcidev, PLX9080_BADDRINDEX));
	devpriv->hpdi_iobase =
		ioremap(pci_resource_start(pcidev, HPDI_BADDRINDEX),
			pci_resource_len(pcidev, HPDI_BADDRINDEX));
	if (!devpriv->plx9080_iobase || !devpriv->hpdi_iobase) {
		dev_warn(dev->class_dev, "failed to remap io memory\n");
		return -ENOMEM;
	}

	DEBUG_PRINT(" plx9080 remapped to 0x%p\n", devpriv->plx9080_iobase);
	DEBUG_PRINT(" hpdi remapped to 0x%p\n", devpriv->hpdi_iobase);

	init_plx9080(dev);

	/*  get irq */
	if (request_irq(pcidev->irq, handle_interrupt, IRQF_SHARED,
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
		DEBUG_PRINT("dio_buffer at virt 0x%p, phys 0x%lx\n",
			    devpriv->dio_buffer[i],
			    (unsigned long)devpriv->dio_buffer_phys_addr[i]);
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

	retval = setup_dma_descriptors(dev, 0x1000);
	if (retval < 0)
		return retval;

	retval = setup_subdevices(dev);
	if (retval < 0)
		return retval;

	return init_hpdi(dev);
}

static void hpdi_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct hpdi_private *devpriv = dev->private;
	unsigned int i;

	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->plx9080_iobase) {
			disable_plx_interrupts(dev);
			iounmap(devpriv->plx9080_iobase);
		}
		if (devpriv->hpdi_iobase)
			iounmap(devpriv->hpdi_iobase);
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
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static int dio_config_block_size(struct comedi_device *dev, unsigned int *data)
{
	unsigned int requested_block_size;
	int retval;

	requested_block_size = data[1];

	retval = setup_dma_descriptors(dev, requested_block_size);
	if (retval < 0)
		return retval;

	data[1] = retval;

	return 2;
}

static int di_cmd_test(struct comedi_device *dev, struct comedi_subdevice *s,
		       struct comedi_cmd *cmd)
{
	int err = 0;
	int i;

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

	if (!cmd->chanlist_len) {
		cmd->chanlist_len = 32;
		err |= -EINVAL;
	}
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
		break;
	case TRIG_NONE:
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (err)
		return 4;

	if (!cmd->chanlist)
		return 0;

	for (i = 1; i < cmd->chanlist_len; i++) {
		if (CR_CHAN(cmd->chanlist[i]) != i) {
			/*  XXX could support 8 or 16 channels */
			comedi_error(dev,
				     "chanlist must be ch 0 to 31 in order");
			err++;
			break;
		}
	}

	if (err)
		return 5;

	return 0;
}

static int hpdi_cmd_test(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_cmd *cmd)
{
	struct hpdi_private *devpriv = dev->private;

	if (devpriv->dio_config_output)
		return -EINVAL;
	else
		return di_cmd_test(dev, s, cmd);
}

static inline void hpdi_writel(struct comedi_device *dev, uint32_t bits,
			       unsigned int offset)
{
	struct hpdi_private *devpriv = dev->private;

	writel(bits | devpriv->bits[offset / sizeof(uint32_t)],
	       devpriv->hpdi_iobase + offset);
}

static int di_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct hpdi_private *devpriv = dev->private;
	uint32_t bits;
	unsigned long flags;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;

	hpdi_writel(dev, RX_FIFO_RESET_BIT, BOARD_CONTROL_REG);

	DEBUG_PRINT("hpdi: in di_cmd\n");

	abort_dma(dev, 0);

	devpriv->dma_desc_index = 0;

	/* These register are supposedly unused during chained dma,
	 * but I have found that left over values from last operation
	 * occasionally cause problems with transfer of first dma
	 * block.  Initializing them to zero seems to fix the problem. */
	writel(0, devpriv->plx9080_iobase + PLX_DMA0_TRANSFER_SIZE_REG);
	writel(0, devpriv->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG);
	writel(0, devpriv->plx9080_iobase + PLX_DMA0_LOCAL_ADDRESS_REG);
	/*  give location of first dma descriptor */
	bits =
	    devpriv->dma_desc_phys_addr | PLX_DESC_IN_PCI_BIT |
	    PLX_INTR_TERM_COUNT | PLX_XFER_LOCAL_TO_PCI;
	writel(bits, devpriv->plx9080_iobase + PLX_DMA0_DESCRIPTOR_REG);

	/*  spinlock for plx dma control/status reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	/*  enable dma transfer */
	writeb(PLX_DMA_EN_BIT | PLX_DMA_START_BIT | PLX_CLEAR_DMA_INTR_BIT,
	       devpriv->plx9080_iobase + PLX_DMA0_CS_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	if (cmd->stop_src == TRIG_COUNT)
		devpriv->dio_count = cmd->stop_arg;
	else
		devpriv->dio_count = 1;

	/*  clear over/under run status flags */
	writel(RX_UNDERRUN_BIT | RX_OVERRUN_BIT,
	       devpriv->hpdi_iobase + BOARD_STATUS_REG);
	/*  enable interrupts */
	writel(intr_bit(RX_FULL_INTR),
	       devpriv->hpdi_iobase + INTERRUPT_CONTROL_REG);

	DEBUG_PRINT("hpdi: starting rx\n");
	hpdi_writel(dev, RX_ENABLE_BIT, BOARD_CONTROL_REG);

	return 0;
}

static int hpdi_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct hpdi_private *devpriv = dev->private;

	if (devpriv->dio_config_output)
		return -EINVAL;
	else
		return di_cmd(dev, s);
}

static void drain_dma_buffers(struct comedi_device *dev, unsigned int channel)
{
	struct hpdi_private *devpriv = dev->private;
	struct comedi_async *async = dev->read_subdev->async;
	uint32_t next_transfer_addr;
	int j;
	int num_samples = 0;
	void __iomem *pci_addr_reg;

	if (channel)
		pci_addr_reg =
		    devpriv->plx9080_iobase + PLX_DMA1_PCI_ADDRESS_REG;
	else
		pci_addr_reg =
		    devpriv->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG;

	/*  loop until we have read all the full buffers */
	j = 0;
	for (next_transfer_addr = readl(pci_addr_reg);
	     (next_transfer_addr <
	      le32_to_cpu(devpriv->dma_desc[devpriv->dma_desc_index].
			  pci_start_addr)
	      || next_transfer_addr >=
	      le32_to_cpu(devpriv->dma_desc[devpriv->dma_desc_index].
			  pci_start_addr) + devpriv->block_size)
	     && j < devpriv->num_dma_descriptors; j++) {
		/*  transfer data from dma buffer to comedi buffer */
		num_samples = devpriv->block_size / sizeof(uint32_t);
		if (async->cmd.stop_src == TRIG_COUNT) {
			if (num_samples > devpriv->dio_count)
				num_samples = devpriv->dio_count;
			devpriv->dio_count -= num_samples;
		}
		cfc_write_array_to_buffer(dev->read_subdev,
					  devpriv->desc_dio_buffer[devpriv->
								     dma_desc_index],
					  num_samples * sizeof(uint32_t));
		devpriv->dma_desc_index++;
		devpriv->dma_desc_index %= devpriv->num_dma_descriptors;

		DEBUG_PRINT("next desc addr 0x%lx\n", (unsigned long)
			    devpriv->dma_desc[devpriv->dma_desc_index].
			    next);
		DEBUG_PRINT("pci addr reg 0x%x\n", next_transfer_addr);
	}
	/*  XXX check for buffer overrun somehow */
}

static irqreturn_t handle_interrupt(int irq, void *d)
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

	plx_status = readl(devpriv->plx9080_iobase + PLX_INTRCS_REG);
	if ((plx_status & (ICS_DMA0_A | ICS_DMA1_A | ICS_LIA)) == 0)
		return IRQ_NONE;

	hpdi_intr_status = readl(devpriv->hpdi_iobase + INTERRUPT_STATUS_REG);
	hpdi_board_status = readl(devpriv->hpdi_iobase + BOARD_STATUS_REG);

	async->events = 0;

	if (hpdi_intr_status) {
		DEBUG_PRINT("hpdi: intr status 0x%x, ", hpdi_intr_status);
		writel(hpdi_intr_status,
		       devpriv->hpdi_iobase + INTERRUPT_STATUS_REG);
	}
	/*  spin lock makes sure no one else changes plx dma control reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	dma0_status = readb(devpriv->plx9080_iobase + PLX_DMA0_CS_REG);
	if (plx_status & ICS_DMA0_A) {	/*  dma chan 0 interrupt */
		writeb((dma0_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT,
		       devpriv->plx9080_iobase + PLX_DMA0_CS_REG);

		DEBUG_PRINT("dma0 status 0x%x\n", dma0_status);
		if (dma0_status & PLX_DMA_EN_BIT)
			drain_dma_buffers(dev, 0);
		DEBUG_PRINT(" cleared dma ch0 interrupt\n");
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  spin lock makes sure no one else changes plx dma control reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	dma1_status = readb(devpriv->plx9080_iobase + PLX_DMA1_CS_REG);
	if (plx_status & ICS_DMA1_A) {	/*  XXX *//*  dma chan 1 interrupt */
		writeb((dma1_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT,
		       devpriv->plx9080_iobase + PLX_DMA1_CS_REG);
		DEBUG_PRINT("dma1 status 0x%x\n", dma1_status);

		DEBUG_PRINT(" cleared dma ch1 interrupt\n");
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  clear possible plx9080 interrupt sources */
	if (plx_status & ICS_LDIA) {	/*  clear local doorbell interrupt */
		plx_bits = readl(devpriv->plx9080_iobase + PLX_DBR_OUT_REG);
		writel(plx_bits, devpriv->plx9080_iobase + PLX_DBR_OUT_REG);
		DEBUG_PRINT(" cleared local doorbell bits 0x%x\n", plx_bits);
	}

	if (hpdi_board_status & RX_OVERRUN_BIT) {
		comedi_error(dev, "rx fifo overrun");
		async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
		DEBUG_PRINT("dma0_status 0x%x\n",
			    (int)readb(devpriv->plx9080_iobase +
				       PLX_DMA0_CS_REG));
	}

	if (hpdi_board_status & RX_UNDERRUN_BIT) {
		comedi_error(dev, "rx fifo underrun");
		async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
	}

	if (devpriv->dio_count == 0)
		async->events |= COMEDI_CB_EOA;

	DEBUG_PRINT("board status 0x%x, ", hpdi_board_status);
	DEBUG_PRINT("plx status 0x%x\n", plx_status);
	if (async->events)
		DEBUG_PRINT(" events 0x%x\n", async->events);

	cfc_handle_events(dev, s);

	return IRQ_HANDLED;
}

static void abort_dma(struct comedi_device *dev, unsigned int channel)
{
	struct hpdi_private *devpriv = dev->private;
	unsigned long flags;

	/*  spinlock for plx dma control/status reg */
	spin_lock_irqsave(&dev->spinlock, flags);

	plx9080_abort_dma(devpriv->plx9080_iobase, channel);

	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static int hpdi_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct hpdi_private *devpriv = dev->private;

	hpdi_writel(dev, 0, BOARD_CONTROL_REG);

	writel(0, devpriv->hpdi_iobase + INTERRUPT_CONTROL_REG);

	abort_dma(dev, 0);

	return 0;
}

static struct comedi_driver gsc_hpdi_driver = {
	.driver_name	= "gsc_hpdi",
	.module		= THIS_MODULE,
	.auto_attach	= hpdi_auto_attach,
	.detach		= hpdi_detach,
};

static int gsc_hpdi_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &gsc_hpdi_driver);
}

static DEFINE_PCI_DEVICE_TABLE(gsc_hpdi_pci_table) = {
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
