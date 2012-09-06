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

Driver: gsc_hpdi
Description: General Standards Corporation High
    Speed Parallel Digital Interface rs485 boards
Author: Frank Mori Hess <fmhess@users.sourceforge.net>
Status: only receive mode works, transmit not supported
Updated: 2003-02-20
Devices: [General Standards Corporation] PCI-HPDI32 (gsc_hpdi),
  PMC-HPDI32

Configuration options:
   [0] - PCI bus of device (optional)
   [1] - PCI slot of device (optional)

There are some additional hpdi models available from GSC for which
support could be added to this driver.

*/

#include <linux/interrupt.h>
#include "../comedidev.h"
#include <linux/delay.h>

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
#define DEBUG_PRINT(format, args...)  printk(format , ## args)
#else
#define DEBUG_PRINT(format, args...)
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

int command_channel_valid(unsigned int channel)
{
	if (channel == 0 || channel > 6) {
		printk(KERN_WARNING
		       "gsc_hpdi: bug! invalid cable command channel\n");
		return 0;
	}
	return 1;
}

/* bit definitions */

enum firmware_revision_bits {
	FEATURES_REG_PRESENT_BIT = 0x8000,
};
int firmware_revision(uint32_t fwr_bits)
{
	return fwr_bits & 0xff;
}

int pcb_revision(uint32_t fwr_bits)
{
	return (fwr_bits >> 8) & 0xff;
}

int hpdi_subid(uint32_t fwr_bits)
{
	return (fwr_bits >> 16) & 0xff;
}

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
uint32_t command_discrete_output_bits(unsigned int channel, int output,
				      int output_value)
{
	uint32_t bits = 0;

	if (command_channel_valid(channel) == 0)
		return 0;
	if (output) {
		bits |= 0x1 << (16 + channel);
		if (output_value)
			bits |= 0x1 << (24 + channel);
	} else
		bits |= 0x1 << (24 + channel);

	return bits;
}

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

uint32_t almost_full_bits(unsigned int num_words)
{
/* XXX need to add or subtract one? */
	return (num_words << 16) & 0xff0000;
}

uint32_t almost_empty_bits(unsigned int num_words)
{
	return num_words & 0xffff;
}

unsigned int almost_full_num_words(uint32_t bits)
{
/* XXX need to add or subtract one? */
	return (bits >> 16) & 0xffff;
}

unsigned int almost_empty_num_words(uint32_t bits)
{
	return bits & 0xffff;
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
int command_intr_source(unsigned int channel)
{
	if (command_channel_valid(channel) == 0)
		channel = 1;
	return channel + 1;
}

uint32_t intr_bit(int interrupt_source)
{
	return 0x1 << interrupt_source;
}

uint32_t tx_clock_divisor_bits(unsigned int divisor)
{
	return divisor & 0xff;
}

unsigned int fifo_size(uint32_t fifo_size_bits)
{
	return fifo_size_bits & 0xfffff;
}

unsigned int fifo_words(uint32_t fifo_words_bits)
{
	return fifo_words_bits & 0xfffff;
}

uint32_t intr_edge_bit(int interrupt_source)
{
	return 0x1 << interrupt_source;
}

uint32_t intr_active_high_bit(int interrupt_source)
{
	return 0x1 << interrupt_source;
}

struct hpdi_board {

	char *name;
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

static inline struct hpdi_board *board(const struct comedi_device *dev)
{
	return (struct hpdi_board *)dev->board_ptr;
}

struct hpdi_private {

	struct pci_dev *hw_dev;	/*  pointer to board's pci_dev struct */
	/*  base addresses (physical) */
	resource_size_t plx9080_phys_iobase;
	resource_size_t hpdi_phys_iobase;
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

static inline struct hpdi_private *priv(struct comedi_device *dev)
{
	return dev->private;
}

static int dio_config_insn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		priv(dev)->dio_config_output = 1;
		return insn->n;
		break;
	case INSN_CONFIG_DIO_INPUT:
		priv(dev)->dio_config_output = 0;
		return insn->n;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    priv(dev)->dio_config_output ? COMEDI_OUTPUT : COMEDI_INPUT;
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
	writel(0, priv(dev)->plx9080_iobase + PLX_INTRCS_REG);
}

/* initialize plx9080 chip */
static void init_plx9080(struct comedi_device *dev)
{
	uint32_t bits;
	void __iomem *plx_iobase = priv(dev)->plx9080_iobase;

	/*  plx9080 dump */
	DEBUG_PRINT(" plx interrupt status 0x%x\n",
		    readl(plx_iobase + PLX_INTRCS_REG));
	DEBUG_PRINT(" plx id bits 0x%x\n", readl(plx_iobase + PLX_ID_REG));
	DEBUG_PRINT(" plx control reg 0x%x\n",
		    readl(priv(dev)->plx9080_iobase + PLX_CONTROL_REG));

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
	writel(bits, priv(dev)->plx9080_iobase + PLX_BIGEND_REG);

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
	uint32_t plx_intcsr_bits;

	writel(BOARD_RESET_BIT, priv(dev)->hpdi_iobase + BOARD_CONTROL_REG);
	udelay(10);

	writel(almost_empty_bits(32) | almost_full_bits(32),
	       priv(dev)->hpdi_iobase + RX_PROG_ALMOST_REG);
	writel(almost_empty_bits(32) | almost_full_bits(32),
	       priv(dev)->hpdi_iobase + TX_PROG_ALMOST_REG);

	priv(dev)->tx_fifo_size = fifo_size(readl(priv(dev)->hpdi_iobase +
						  TX_FIFO_SIZE_REG));
	priv(dev)->rx_fifo_size = fifo_size(readl(priv(dev)->hpdi_iobase +
						  RX_FIFO_SIZE_REG));

	writel(0, priv(dev)->hpdi_iobase + INTERRUPT_CONTROL_REG);

	/*  enable interrupts */
	plx_intcsr_bits =
	    ICS_AERR | ICS_PERR | ICS_PIE | ICS_PLIE | ICS_PAIE | ICS_LIE |
	    ICS_DMA0_E;
	writel(plx_intcsr_bits, priv(dev)->plx9080_iobase + PLX_INTRCS_REG);

	return 0;
}

/* setup dma descriptors so a link completes every 'transfer_size' bytes */
static int setup_dma_descriptors(struct comedi_device *dev,
				 unsigned int transfer_size)
{
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
		    (unsigned long)priv(dev)->dma_desc_phys_addr);

	buffer_offset = 0;
	buffer_index = 0;
	for (i = 0; i < NUM_DMA_DESCRIPTORS &&
	     buffer_index < NUM_DMA_BUFFERS; i++) {
		priv(dev)->dma_desc[i].pci_start_addr =
		    cpu_to_le32(priv(dev)->dio_buffer_phys_addr[buffer_index] +
				buffer_offset);
		priv(dev)->dma_desc[i].local_start_addr = cpu_to_le32(FIFO_REG);
		priv(dev)->dma_desc[i].transfer_size =
		    cpu_to_le32(transfer_size);
		priv(dev)->dma_desc[i].next =
		    cpu_to_le32((priv(dev)->dma_desc_phys_addr + (i +
								  1) *
				 sizeof(priv(dev)->dma_desc[0])) | next_bits);

		priv(dev)->desc_dio_buffer[i] =
		    priv(dev)->dio_buffer[buffer_index] +
		    (buffer_offset / sizeof(uint32_t));

		buffer_offset += transfer_size;
		if (transfer_size + buffer_offset > DMA_BUFFER_SIZE) {
			buffer_offset = 0;
			buffer_index++;
		}

		DEBUG_PRINT(" desc %i\n", i);
		DEBUG_PRINT(" start addr virt 0x%p, phys 0x%lx\n",
			    priv(dev)->desc_dio_buffer[i],
			    (unsigned long)priv(dev)->dma_desc[i].
			    pci_start_addr);
		DEBUG_PRINT(" next 0x%lx\n",
			    (unsigned long)priv(dev)->dma_desc[i].next);
	}
	priv(dev)->num_dma_descriptors = i;
	/*  fix last descriptor to point back to first */
	priv(dev)->dma_desc[i - 1].next =
	    cpu_to_le32(priv(dev)->dma_desc_phys_addr | next_bits);
	DEBUG_PRINT(" desc %i next fixup 0x%lx\n", i - 1,
		    (unsigned long)priv(dev)->dma_desc[i - 1].next);

	priv(dev)->block_size = transfer_size;

	return transfer_size;
}

static int hpdi_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct pci_dev *pcidev;
	int i;
	int retval;

	printk(KERN_WARNING "comedi%d: gsc_hpdi\n", dev->minor);

	if (alloc_private(dev, sizeof(struct hpdi_private)) < 0)
		return -ENOMEM;

	pcidev = NULL;
	for (i = 0; i < ARRAY_SIZE(hpdi_boards) &&
		    dev->board_ptr == NULL; i++) {
		do {
			pcidev = pci_get_subsys(PCI_VENDOR_ID_PLX,
						hpdi_boards[i].device_id,
						PCI_VENDOR_ID_PLX,
						hpdi_boards[i].subdevice_id,
						pcidev);
			/*  was a particular bus/slot requested? */
			if (it->options[0] || it->options[1]) {
				/*  are we on the wrong bus/slot? */
				if (pcidev->bus->number != it->options[0] ||
				    PCI_SLOT(pcidev->devfn) != it->options[1])
					continue;
			}
			if (pcidev) {
				priv(dev)->hw_dev = pcidev;
				dev->board_ptr = hpdi_boards + i;
				break;
			}
		} while (pcidev != NULL);
	}
	if (dev->board_ptr == NULL) {
		printk(KERN_WARNING "gsc_hpdi: no hpdi card found\n");
		return -EIO;
	}

	printk(KERN_WARNING
	       "gsc_hpdi: found %s on bus %i, slot %i\n", board(dev)->name,
	       pcidev->bus->number, PCI_SLOT(pcidev->devfn));

	if (comedi_pci_enable(pcidev, dev->driver->driver_name)) {
		printk(KERN_WARNING
		       " failed enable PCI device and request regions\n");
		return -EIO;
	}
	pci_set_master(pcidev);

	/* Initialize dev->board_name */
	dev->board_name = board(dev)->name;

	priv(dev)->plx9080_phys_iobase =
	    pci_resource_start(pcidev, PLX9080_BADDRINDEX);
	priv(dev)->hpdi_phys_iobase =
	    pci_resource_start(pcidev, HPDI_BADDRINDEX);

	/*  remap, won't work with 2.0 kernels but who cares */
	priv(dev)->plx9080_iobase = ioremap(priv(dev)->plx9080_phys_iobase,
					    pci_resource_len(pcidev,
					    PLX9080_BADDRINDEX));
	priv(dev)->hpdi_iobase =
	    ioremap(priv(dev)->hpdi_phys_iobase,
		    pci_resource_len(pcidev, HPDI_BADDRINDEX));
	if (!priv(dev)->plx9080_iobase || !priv(dev)->hpdi_iobase) {
		printk(KERN_WARNING " failed to remap io memory\n");
		return -ENOMEM;
	}

	DEBUG_PRINT(" plx9080 remapped to 0x%p\n", priv(dev)->plx9080_iobase);
	DEBUG_PRINT(" hpdi remapped to 0x%p\n", priv(dev)->hpdi_iobase);

	init_plx9080(dev);

	/*  get irq */
	if (request_irq(pcidev->irq, handle_interrupt, IRQF_SHARED,
			dev->driver->driver_name, dev)) {
		printk(KERN_WARNING
		       " unable to allocate irq %u\n", pcidev->irq);
		return -EINVAL;
	}
	dev->irq = pcidev->irq;

	printk(KERN_WARNING " irq %u\n", dev->irq);

	/*  allocate pci dma buffers */
	for (i = 0; i < NUM_DMA_BUFFERS; i++) {
		priv(dev)->dio_buffer[i] =
		    pci_alloc_consistent(priv(dev)->hw_dev, DMA_BUFFER_SIZE,
					 &priv(dev)->dio_buffer_phys_addr[i]);
		DEBUG_PRINT("dio_buffer at virt 0x%p, phys 0x%lx\n",
			    priv(dev)->dio_buffer[i],
			    (unsigned long)priv(dev)->dio_buffer_phys_addr[i]);
	}
	/*  allocate dma descriptors */
	priv(dev)->dma_desc = pci_alloc_consistent(priv(dev)->hw_dev,
						   sizeof(struct plx_dma_desc) *
						   NUM_DMA_DESCRIPTORS,
						   &priv(dev)->
						   dma_desc_phys_addr);
	if (priv(dev)->dma_desc_phys_addr & 0xf) {
		printk(KERN_WARNING
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
	unsigned int i;

	if (dev->irq)
		free_irq(dev->irq, dev);
	if ((priv(dev)) && (priv(dev)->hw_dev)) {
		if (priv(dev)->plx9080_iobase) {
			disable_plx_interrupts(dev);
			iounmap(priv(dev)->plx9080_iobase);
		}
		if (priv(dev)->hpdi_iobase)
			iounmap(priv(dev)->hpdi_iobase);
		/*  free pci dma buffers */
		for (i = 0; i < NUM_DMA_BUFFERS; i++) {
			if (priv(dev)->dio_buffer[i])
				pci_free_consistent(priv(dev)->hw_dev,
						    DMA_BUFFER_SIZE,
						    priv(dev)->
						    dio_buffer[i],
						    priv
						    (dev)->dio_buffer_phys_addr
						    [i]);
		}
		/*  free dma descriptors */
		if (priv(dev)->dma_desc)
			pci_free_consistent(priv(dev)->hw_dev,
					    sizeof(struct plx_dma_desc)
					    * NUM_DMA_DESCRIPTORS,
					    priv(dev)->dma_desc,
					    priv(dev)->
					    dma_desc_phys_addr);
		if (priv(dev)->hpdi_phys_iobase)
			comedi_pci_disable(priv(dev)->hw_dev);
		pci_dev_put(priv(dev)->hw_dev);
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
	int tmp;
	int i;

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_NOW;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and mutually
	 * compatible */

	/*  uniqueness check */
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (!cmd->chanlist_len) {
		cmd->chanlist_len = 32;
		err++;
	}
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		if (!cmd->stop_arg) {
			cmd->stop_arg = 1;
			err++;
		}
		break;
	case TRIG_NONE:
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
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
	if (priv(dev)->dio_config_output)
		return -EINVAL;
	else
		return di_cmd_test(dev, s, cmd);
}

static inline void hpdi_writel(struct comedi_device *dev, uint32_t bits,
			       unsigned int offset)
{
	writel(bits | priv(dev)->bits[offset / sizeof(uint32_t)],
	       priv(dev)->hpdi_iobase + offset);
}

static int di_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	uint32_t bits;
	unsigned long flags;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;

	hpdi_writel(dev, RX_FIFO_RESET_BIT, BOARD_CONTROL_REG);

	DEBUG_PRINT("hpdi: in di_cmd\n");

	abort_dma(dev, 0);

	priv(dev)->dma_desc_index = 0;

	/* These register are supposedly unused during chained dma,
	 * but I have found that left over values from last operation
	 * occasionally cause problems with transfer of first dma
	 * block.  Initializing them to zero seems to fix the problem. */
	writel(0, priv(dev)->plx9080_iobase + PLX_DMA0_TRANSFER_SIZE_REG);
	writel(0, priv(dev)->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG);
	writel(0, priv(dev)->plx9080_iobase + PLX_DMA0_LOCAL_ADDRESS_REG);
	/*  give location of first dma descriptor */
	bits =
	    priv(dev)->dma_desc_phys_addr | PLX_DESC_IN_PCI_BIT |
	    PLX_INTR_TERM_COUNT | PLX_XFER_LOCAL_TO_PCI;
	writel(bits, priv(dev)->plx9080_iobase + PLX_DMA0_DESCRIPTOR_REG);

	/*  spinlock for plx dma control/status reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	/*  enable dma transfer */
	writeb(PLX_DMA_EN_BIT | PLX_DMA_START_BIT | PLX_CLEAR_DMA_INTR_BIT,
	       priv(dev)->plx9080_iobase + PLX_DMA0_CS_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	if (cmd->stop_src == TRIG_COUNT)
		priv(dev)->dio_count = cmd->stop_arg;
	else
		priv(dev)->dio_count = 1;

	/*  clear over/under run status flags */
	writel(RX_UNDERRUN_BIT | RX_OVERRUN_BIT,
	       priv(dev)->hpdi_iobase + BOARD_STATUS_REG);
	/*  enable interrupts */
	writel(intr_bit(RX_FULL_INTR),
	       priv(dev)->hpdi_iobase + INTERRUPT_CONTROL_REG);

	DEBUG_PRINT("hpdi: starting rx\n");
	hpdi_writel(dev, RX_ENABLE_BIT, BOARD_CONTROL_REG);

	return 0;
}

static int hpdi_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	if (priv(dev)->dio_config_output)
		return -EINVAL;
	else
		return di_cmd(dev, s);
}

static void drain_dma_buffers(struct comedi_device *dev, unsigned int channel)
{
	struct comedi_async *async = dev->read_subdev->async;
	uint32_t next_transfer_addr;
	int j;
	int num_samples = 0;
	void __iomem *pci_addr_reg;

	if (channel)
		pci_addr_reg =
		    priv(dev)->plx9080_iobase + PLX_DMA1_PCI_ADDRESS_REG;
	else
		pci_addr_reg =
		    priv(dev)->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG;

	/*  loop until we have read all the full buffers */
	j = 0;
	for (next_transfer_addr = readl(pci_addr_reg);
	     (next_transfer_addr <
	      le32_to_cpu(priv(dev)->dma_desc[priv(dev)->dma_desc_index].
			  pci_start_addr)
	      || next_transfer_addr >=
	      le32_to_cpu(priv(dev)->dma_desc[priv(dev)->dma_desc_index].
			  pci_start_addr) + priv(dev)->block_size)
	     && j < priv(dev)->num_dma_descriptors; j++) {
		/*  transfer data from dma buffer to comedi buffer */
		num_samples = priv(dev)->block_size / sizeof(uint32_t);
		if (async->cmd.stop_src == TRIG_COUNT) {
			if (num_samples > priv(dev)->dio_count)
				num_samples = priv(dev)->dio_count;
			priv(dev)->dio_count -= num_samples;
		}
		cfc_write_array_to_buffer(dev->read_subdev,
					  priv(dev)->desc_dio_buffer[priv(dev)->
								     dma_desc_index],
					  num_samples * sizeof(uint32_t));
		priv(dev)->dma_desc_index++;
		priv(dev)->dma_desc_index %= priv(dev)->num_dma_descriptors;

		DEBUG_PRINT("next desc addr 0x%lx\n", (unsigned long)
			    priv(dev)->dma_desc[priv(dev)->dma_desc_index].
			    next);
		DEBUG_PRINT("pci addr reg 0x%x\n", next_transfer_addr);
	}
	/*  XXX check for buffer overrun somehow */
}

static irqreturn_t handle_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	uint32_t hpdi_intr_status, hpdi_board_status;
	uint32_t plx_status;
	uint32_t plx_bits;
	uint8_t dma0_status, dma1_status;
	unsigned long flags;

	if (!dev->attached)
		return IRQ_NONE;

	plx_status = readl(priv(dev)->plx9080_iobase + PLX_INTRCS_REG);
	if ((plx_status & (ICS_DMA0_A | ICS_DMA1_A | ICS_LIA)) == 0)
		return IRQ_NONE;

	hpdi_intr_status = readl(priv(dev)->hpdi_iobase + INTERRUPT_STATUS_REG);
	hpdi_board_status = readl(priv(dev)->hpdi_iobase + BOARD_STATUS_REG);

	async->events = 0;

	if (hpdi_intr_status) {
		DEBUG_PRINT("hpdi: intr status 0x%x, ", hpdi_intr_status);
		writel(hpdi_intr_status,
		       priv(dev)->hpdi_iobase + INTERRUPT_STATUS_REG);
	}
	/*  spin lock makes sure no one else changes plx dma control reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	dma0_status = readb(priv(dev)->plx9080_iobase + PLX_DMA0_CS_REG);
	if (plx_status & ICS_DMA0_A) {	/*  dma chan 0 interrupt */
		writeb((dma0_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT,
		       priv(dev)->plx9080_iobase + PLX_DMA0_CS_REG);

		DEBUG_PRINT("dma0 status 0x%x\n", dma0_status);
		if (dma0_status & PLX_DMA_EN_BIT)
			drain_dma_buffers(dev, 0);
		DEBUG_PRINT(" cleared dma ch0 interrupt\n");
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  spin lock makes sure no one else changes plx dma control reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	dma1_status = readb(priv(dev)->plx9080_iobase + PLX_DMA1_CS_REG);
	if (plx_status & ICS_DMA1_A) {	/*  XXX *//*  dma chan 1 interrupt */
		writeb((dma1_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT,
		       priv(dev)->plx9080_iobase + PLX_DMA1_CS_REG);
		DEBUG_PRINT("dma1 status 0x%x\n", dma1_status);

		DEBUG_PRINT(" cleared dma ch1 interrupt\n");
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  clear possible plx9080 interrupt sources */
	if (plx_status & ICS_LDIA) {	/*  clear local doorbell interrupt */
		plx_bits = readl(priv(dev)->plx9080_iobase + PLX_DBR_OUT_REG);
		writel(plx_bits, priv(dev)->plx9080_iobase + PLX_DBR_OUT_REG);
		DEBUG_PRINT(" cleared local doorbell bits 0x%x\n", plx_bits);
	}

	if (hpdi_board_status & RX_OVERRUN_BIT) {
		comedi_error(dev, "rx fifo overrun");
		async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
		DEBUG_PRINT("dma0_status 0x%x\n",
			    (int)readb(priv(dev)->plx9080_iobase +
				       PLX_DMA0_CS_REG));
	}

	if (hpdi_board_status & RX_UNDERRUN_BIT) {
		comedi_error(dev, "rx fifo underrun");
		async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
	}

	if (priv(dev)->dio_count == 0)
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
	unsigned long flags;

	/*  spinlock for plx dma control/status reg */
	spin_lock_irqsave(&dev->spinlock, flags);

	plx9080_abort_dma(priv(dev)->plx9080_iobase, channel);

	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static int hpdi_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	hpdi_writel(dev, 0, BOARD_CONTROL_REG);

	writel(0, priv(dev)->hpdi_iobase + INTERRUPT_CONTROL_REG);

	abort_dma(dev, 0);

	return 0;
}

static struct comedi_driver gsc_hpdi_driver = {
	.driver_name	= "gsc_hpdi",
	.module		= THIS_MODULE,
	.attach		= hpdi_attach,
	.detach		= hpdi_detach,
};

static int __devinit gsc_hpdi_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &gsc_hpdi_driver);
}

static void __devexit gsc_hpdi_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
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
	.remove		= __devexit_p(gsc_hpdi_pci_remove)
};
module_comedi_pci_driver(gsc_hpdi_driver, gsc_hpdi_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
