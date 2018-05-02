// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware driver for NI 660x devices
 */

/*
 * Driver: ni_660x
 * Description: National Instruments 660x counter/timer boards
 * Devices: [National Instruments] PCI-6601 (ni_660x), PCI-6602, PXI-6602,
 *   PXI-6608, PCI-6624, PXI-6624
 * Author: J.P. Mellor <jpmellor@rose-hulman.edu>,
 *   Herman.Bruyninckx@mech.kuleuven.ac.be,
 *   Wim.Meeussen@mech.kuleuven.ac.be,
 *   Klaas.Gadeyne@mech.kuleuven.ac.be,
 *   Frank Mori Hess <fmhess@users.sourceforge.net>
 * Updated: Mon, 16 Jan 2017 14:00:43 +0000
 * Status: experimental
 *
 * Encoders work.  PulseGeneration (both single pulse and pulse train)
 * works.  Buffered commands work for input but not output.
 *
 * References:
 * DAQ 660x Register-Level Programmer Manual  (NI 370505A-01)
 * DAQ 6601/6602 User Manual (NI 322137B-01)
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

#include "mite.h"
#include "ni_tio.h"

/* See Register-Level Programmer Manual page 3.1 */
enum ni_660x_register {
	/* see enum ni_gpct_register */
	NI660X_STC_DIO_PARALLEL_INPUT = NITIO_NUM_REGS,
	NI660X_STC_DIO_OUTPUT,
	NI660X_STC_DIO_CONTROL,
	NI660X_STC_DIO_SERIAL_INPUT,
	NI660X_DIO32_INPUT,
	NI660X_DIO32_OUTPUT,
	NI660X_CLK_CFG,
	NI660X_GLOBAL_INT_STATUS,
	NI660X_DMA_CFG,
	NI660X_GLOBAL_INT_CFG,
	NI660X_IO_CFG_0_1,
	NI660X_IO_CFG_2_3,
	NI660X_IO_CFG_4_5,
	NI660X_IO_CFG_6_7,
	NI660X_IO_CFG_8_9,
	NI660X_IO_CFG_10_11,
	NI660X_IO_CFG_12_13,
	NI660X_IO_CFG_14_15,
	NI660X_IO_CFG_16_17,
	NI660X_IO_CFG_18_19,
	NI660X_IO_CFG_20_21,
	NI660X_IO_CFG_22_23,
	NI660X_IO_CFG_24_25,
	NI660X_IO_CFG_26_27,
	NI660X_IO_CFG_28_29,
	NI660X_IO_CFG_30_31,
	NI660X_IO_CFG_32_33,
	NI660X_IO_CFG_34_35,
	NI660X_IO_CFG_36_37,
	NI660X_IO_CFG_38_39,
	NI660X_NUM_REGS,
};

#define NI660X_CLK_CFG_COUNTER_SWAP	BIT(21)

#define NI660X_GLOBAL_INT_COUNTER0	BIT(8)
#define NI660X_GLOBAL_INT_COUNTER1	BIT(9)
#define NI660X_GLOBAL_INT_COUNTER2	BIT(10)
#define NI660X_GLOBAL_INT_COUNTER3	BIT(11)
#define NI660X_GLOBAL_INT_CASCADE	BIT(29)
#define NI660X_GLOBAL_INT_GLOBAL_POL	BIT(30)
#define NI660X_GLOBAL_INT_GLOBAL	BIT(31)

#define NI660X_DMA_CFG_SEL(_c, _s)	(((_s) & 0x1f) << (8 * (_c)))
#define NI660X_DMA_CFG_SEL_MASK(_c)	NI660X_DMA_CFG_SEL((_c), 0x1f)
#define NI660X_DMA_CFG_SEL_NONE(_c)	NI660X_DMA_CFG_SEL((_c), 0x1f)
#define NI660X_DMA_CFG_RESET(_c)	NI660X_DMA_CFG_SEL((_c), 0x80)

#define NI660X_IO_CFG(x)		(NI660X_IO_CFG_0_1 + ((x) / 2))
#define NI660X_IO_CFG_OUT_SEL(_c, _s)	(((_s) & 0x3) << (((_c) % 2) ? 0 : 8))
#define NI660X_IO_CFG_OUT_SEL_MASK(_c)	NI660X_IO_CFG_OUT_SEL((_c), 0x3)
#define NI660X_IO_CFG_IN_SEL(_c, _s)	(((_s) & 0x7) << (((_c) % 2) ? 4 : 12))
#define NI660X_IO_CFG_IN_SEL_MASK(_c)	NI660X_IO_CFG_IN_SEL((_c), 0x7)

struct ni_660x_register_data {
	int offset;		/*  Offset from base address from GPCT chip */
	char size;		/* 2 or 4 bytes */
};

static const struct ni_660x_register_data ni_660x_reg_data[NI660X_NUM_REGS] = {
	[NITIO_G0_INT_ACK]		= { 0x004, 2 },	/* write */
	[NITIO_G0_STATUS]		= { 0x004, 2 },	/* read */
	[NITIO_G1_INT_ACK]		= { 0x006, 2 },	/* write */
	[NITIO_G1_STATUS]		= { 0x006, 2 },	/* read */
	[NITIO_G01_STATUS]		= { 0x008, 2 },	/* read */
	[NITIO_G0_CMD]			= { 0x00c, 2 },	/* write */
	[NI660X_STC_DIO_PARALLEL_INPUT]	= { 0x00e, 2 },	/* read */
	[NITIO_G1_CMD]			= { 0x00e, 2 },	/* write */
	[NITIO_G0_HW_SAVE]		= { 0x010, 4 },	/* read */
	[NITIO_G1_HW_SAVE]		= { 0x014, 4 },	/* read */
	[NI660X_STC_DIO_OUTPUT]		= { 0x014, 2 },	/* write */
	[NI660X_STC_DIO_CONTROL]	= { 0x016, 2 },	/* write */
	[NITIO_G0_SW_SAVE]		= { 0x018, 4 },	/* read */
	[NITIO_G1_SW_SAVE]		= { 0x01c, 4 },	/* read */
	[NITIO_G0_MODE]			= { 0x034, 2 },	/* write */
	[NITIO_G01_STATUS1]		= { 0x036, 2 },	/* read */
	[NITIO_G1_MODE]			= { 0x036, 2 },	/* write */
	[NI660X_STC_DIO_SERIAL_INPUT]	= { 0x038, 2 },	/* read */
	[NITIO_G0_LOADA]		= { 0x038, 4 },	/* write */
	[NITIO_G01_STATUS2]		= { 0x03a, 2 },	/* read */
	[NITIO_G0_LOADB]		= { 0x03c, 4 },	/* write */
	[NITIO_G1_LOADA]		= { 0x040, 4 },	/* write */
	[NITIO_G1_LOADB]		= { 0x044, 4 },	/* write */
	[NITIO_G0_INPUT_SEL]		= { 0x048, 2 },	/* write */
	[NITIO_G1_INPUT_SEL]		= { 0x04a, 2 },	/* write */
	[NITIO_G0_AUTO_INC]		= { 0x088, 2 },	/* write */
	[NITIO_G1_AUTO_INC]		= { 0x08a, 2 },	/* write */
	[NITIO_G01_RESET]		= { 0x090, 2 },	/* write */
	[NITIO_G0_INT_ENA]		= { 0x092, 2 },	/* write */
	[NITIO_G1_INT_ENA]		= { 0x096, 2 },	/* write */
	[NITIO_G0_CNT_MODE]		= { 0x0b0, 2 },	/* write */
	[NITIO_G1_CNT_MODE]		= { 0x0b2, 2 },	/* write */
	[NITIO_G0_GATE2]		= { 0x0b4, 2 },	/* write */
	[NITIO_G1_GATE2]		= { 0x0b6, 2 },	/* write */
	[NITIO_G0_DMA_CFG]		= { 0x0b8, 2 },	/* write */
	[NITIO_G0_DMA_STATUS]		= { 0x0b8, 2 },	/* read */
	[NITIO_G1_DMA_CFG]		= { 0x0ba, 2 },	/* write */
	[NITIO_G1_DMA_STATUS]		= { 0x0ba, 2 },	/* read */
	[NITIO_G2_INT_ACK]		= { 0x104, 2 },	/* write */
	[NITIO_G2_STATUS]		= { 0x104, 2 },	/* read */
	[NITIO_G3_INT_ACK]		= { 0x106, 2 },	/* write */
	[NITIO_G3_STATUS]		= { 0x106, 2 },	/* read */
	[NITIO_G23_STATUS]		= { 0x108, 2 },	/* read */
	[NITIO_G2_CMD]			= { 0x10c, 2 },	/* write */
	[NITIO_G3_CMD]			= { 0x10e, 2 },	/* write */
	[NITIO_G2_HW_SAVE]		= { 0x110, 4 },	/* read */
	[NITIO_G3_HW_SAVE]		= { 0x114, 4 },	/* read */
	[NITIO_G2_SW_SAVE]		= { 0x118, 4 },	/* read */
	[NITIO_G3_SW_SAVE]		= { 0x11c, 4 },	/* read */
	[NITIO_G2_MODE]			= { 0x134, 2 },	/* write */
	[NITIO_G23_STATUS1]		= { 0x136, 2 },	/* read */
	[NITIO_G3_MODE]			= { 0x136, 2 },	/* write */
	[NITIO_G2_LOADA]		= { 0x138, 4 },	/* write */
	[NITIO_G23_STATUS2]		= { 0x13a, 2 },	/* read */
	[NITIO_G2_LOADB]		= { 0x13c, 4 },	/* write */
	[NITIO_G3_LOADA]		= { 0x140, 4 },	/* write */
	[NITIO_G3_LOADB]		= { 0x144, 4 },	/* write */
	[NITIO_G2_INPUT_SEL]		= { 0x148, 2 },	/* write */
	[NITIO_G3_INPUT_SEL]		= { 0x14a, 2 },	/* write */
	[NITIO_G2_AUTO_INC]		= { 0x188, 2 },	/* write */
	[NITIO_G3_AUTO_INC]		= { 0x18a, 2 },	/* write */
	[NITIO_G23_RESET]		= { 0x190, 2 },	/* write */
	[NITIO_G2_INT_ENA]		= { 0x192, 2 },	/* write */
	[NITIO_G3_INT_ENA]		= { 0x196, 2 },	/* write */
	[NITIO_G2_CNT_MODE]		= { 0x1b0, 2 },	/* write */
	[NITIO_G3_CNT_MODE]		= { 0x1b2, 2 },	/* write */
	[NITIO_G2_GATE2]		= { 0x1b4, 2 },	/* write */
	[NITIO_G3_GATE2]		= { 0x1b6, 2 },	/* write */
	[NITIO_G2_DMA_CFG]		= { 0x1b8, 2 },	/* write */
	[NITIO_G2_DMA_STATUS]		= { 0x1b8, 2 },	/* read */
	[NITIO_G3_DMA_CFG]		= { 0x1ba, 2 },	/* write */
	[NITIO_G3_DMA_STATUS]		= { 0x1ba, 2 },	/* read */
	[NI660X_DIO32_INPUT]		= { 0x414, 4 },	/* read */
	[NI660X_DIO32_OUTPUT]		= { 0x510, 4 },	/* write */
	[NI660X_CLK_CFG]		= { 0x73c, 4 },	/* write */
	[NI660X_GLOBAL_INT_STATUS]	= { 0x754, 4 },	/* read */
	[NI660X_DMA_CFG]		= { 0x76c, 4 },	/* write */
	[NI660X_GLOBAL_INT_CFG]		= { 0x770, 4 },	/* write */
	[NI660X_IO_CFG_0_1]		= { 0x77c, 2 },	/* read/write */
	[NI660X_IO_CFG_2_3]		= { 0x77e, 2 },	/* read/write */
	[NI660X_IO_CFG_4_5]		= { 0x780, 2 },	/* read/write */
	[NI660X_IO_CFG_6_7]		= { 0x782, 2 },	/* read/write */
	[NI660X_IO_CFG_8_9]		= { 0x784, 2 },	/* read/write */
	[NI660X_IO_CFG_10_11]		= { 0x786, 2 },	/* read/write */
	[NI660X_IO_CFG_12_13]		= { 0x788, 2 },	/* read/write */
	[NI660X_IO_CFG_14_15]		= { 0x78a, 2 },	/* read/write */
	[NI660X_IO_CFG_16_17]		= { 0x78c, 2 },	/* read/write */
	[NI660X_IO_CFG_18_19]		= { 0x78e, 2 },	/* read/write */
	[NI660X_IO_CFG_20_21]		= { 0x790, 2 },	/* read/write */
	[NI660X_IO_CFG_22_23]		= { 0x792, 2 },	/* read/write */
	[NI660X_IO_CFG_24_25]		= { 0x794, 2 },	/* read/write */
	[NI660X_IO_CFG_26_27]		= { 0x796, 2 },	/* read/write */
	[NI660X_IO_CFG_28_29]		= { 0x798, 2 },	/* read/write */
	[NI660X_IO_CFG_30_31]		= { 0x79a, 2 },	/* read/write */
	[NI660X_IO_CFG_32_33]		= { 0x79c, 2 },	/* read/write */
	[NI660X_IO_CFG_34_35]		= { 0x79e, 2 },	/* read/write */
	[NI660X_IO_CFG_36_37]		= { 0x7a0, 2 },	/* read/write */
	[NI660X_IO_CFG_38_39]		= { 0x7a2, 2 }	/* read/write */
};

#define NI660X_CHIP_OFFSET		0x800

enum ni_660x_boardid {
	BOARD_PCI6601,
	BOARD_PCI6602,
	BOARD_PXI6602,
	BOARD_PXI6608,
	BOARD_PCI6624,
	BOARD_PXI6624
};

struct ni_660x_board {
	const char *name;
	unsigned int n_chips;	/* total number of TIO chips */
};

static const struct ni_660x_board ni_660x_boards[] = {
	[BOARD_PCI6601] = {
		.name		= "PCI-6601",
		.n_chips	= 1,
	},
	[BOARD_PCI6602] = {
		.name		= "PCI-6602",
		.n_chips	= 2,
	},
	[BOARD_PXI6602] = {
		.name		= "PXI-6602",
		.n_chips	= 2,
	},
	[BOARD_PXI6608] = {
		.name		= "PXI-6608",
		.n_chips	= 2,
	},
	[BOARD_PCI6624] = {
		.name		= "PCI-6624",
		.n_chips	= 2,
	},
	[BOARD_PXI6624] = {
		.name		= "PXI-6624",
		.n_chips	= 2,
	},
};

#define NI660X_NUM_PFI_CHANNELS		40

/* there are only up to 3 dma channels, but the register layout allows for 4 */
#define NI660X_MAX_DMA_CHANNEL		4

#define NI660X_COUNTERS_PER_CHIP	4
#define NI660X_MAX_CHIPS		2
#define NI660X_MAX_COUNTERS		(NI660X_MAX_CHIPS *	\
					 NI660X_COUNTERS_PER_CHIP)

struct ni_660x_private {
	struct mite *mite;
	struct ni_gpct_device *counter_dev;
	struct mite_ring *ring[NI660X_MAX_CHIPS][NI660X_COUNTERS_PER_CHIP];
	/* protects mite channel request/release */
	spinlock_t mite_channel_lock;
	/* prevents races between interrupt and comedi_poll */
	spinlock_t interrupt_lock;
	unsigned int dma_cfg[NI660X_MAX_CHIPS];
	unsigned int io_cfg[NI660X_NUM_PFI_CHANNELS];
	u64 io_dir;
};

static void ni_660x_write(struct comedi_device *dev, unsigned int chip,
			  unsigned int bits, unsigned int reg)
{
	unsigned int addr = (chip * NI660X_CHIP_OFFSET) +
			    ni_660x_reg_data[reg].offset;

	if (ni_660x_reg_data[reg].size == 2)
		writew(bits, dev->mmio + addr);
	else
		writel(bits, dev->mmio + addr);
}

static unsigned int ni_660x_read(struct comedi_device *dev,
				 unsigned int chip, unsigned int reg)
{
	unsigned int addr = (chip * NI660X_CHIP_OFFSET) +
			    ni_660x_reg_data[reg].offset;

	if (ni_660x_reg_data[reg].size == 2)
		return readw(dev->mmio + addr);
	return readl(dev->mmio + addr);
}

static void ni_660x_gpct_write(struct ni_gpct *counter, unsigned int bits,
			       enum ni_gpct_register reg)
{
	struct comedi_device *dev = counter->counter_dev->dev;

	ni_660x_write(dev, counter->chip_index, bits, reg);
}

static unsigned int ni_660x_gpct_read(struct ni_gpct *counter,
				      enum ni_gpct_register reg)
{
	struct comedi_device *dev = counter->counter_dev->dev;

	return ni_660x_read(dev, counter->chip_index, reg);
}

static inline void ni_660x_set_dma_channel(struct comedi_device *dev,
					   unsigned int mite_channel,
					   struct ni_gpct *counter)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned int chip = counter->chip_index;

	devpriv->dma_cfg[chip] &= ~NI660X_DMA_CFG_SEL_MASK(mite_channel);
	devpriv->dma_cfg[chip] |= NI660X_DMA_CFG_SEL(mite_channel,
						     counter->counter_index);
	ni_660x_write(dev, chip, devpriv->dma_cfg[chip] |
		      NI660X_DMA_CFG_RESET(mite_channel),
		      NI660X_DMA_CFG);
	mmiowb();
}

static inline void ni_660x_unset_dma_channel(struct comedi_device *dev,
					     unsigned int mite_channel,
					     struct ni_gpct *counter)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned int chip = counter->chip_index;

	devpriv->dma_cfg[chip] &= ~NI660X_DMA_CFG_SEL_MASK(mite_channel);
	devpriv->dma_cfg[chip] |= NI660X_DMA_CFG_SEL_NONE(mite_channel);
	ni_660x_write(dev, chip, devpriv->dma_cfg[chip], NI660X_DMA_CFG);
	mmiowb();
}

static int ni_660x_request_mite_channel(struct comedi_device *dev,
					struct ni_gpct *counter,
					enum comedi_io_direction direction)
{
	struct ni_660x_private *devpriv = dev->private;
	struct mite_ring *ring;
	struct mite_channel *mite_chan;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	ring = devpriv->ring[counter->chip_index][counter->counter_index];
	mite_chan = mite_request_channel(devpriv->mite, ring);
	if (!mite_chan) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		dev_err(dev->class_dev,
			"failed to reserve mite dma channel for counter\n");
		return -EBUSY;
	}
	mite_chan->dir = direction;
	ni_tio_set_mite_channel(counter, mite_chan);
	ni_660x_set_dma_channel(dev, mite_chan->channel, counter);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static void ni_660x_release_mite_channel(struct comedi_device *dev,
					 struct ni_gpct *counter)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (counter->mite_chan) {
		struct mite_channel *mite_chan = counter->mite_chan;

		ni_660x_unset_dma_channel(dev, mite_chan->channel, counter);
		ni_tio_set_mite_channel(counter, NULL);
		mite_release_channel(mite_chan);
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}

static int ni_660x_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	int retval;

	retval = ni_660x_request_mite_channel(dev, counter, COMEDI_INPUT);
	if (retval) {
		dev_err(dev->class_dev,
			"no dma channel available for use by counter\n");
		return retval;
	}
	ni_tio_acknowledge(counter);

	return ni_tio_cmd(dev, s);
}

static int ni_660x_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	int retval;

	retval = ni_tio_cancel(counter);
	ni_660x_release_mite_channel(dev, counter);
	return retval;
}

static void set_tio_counterswap(struct comedi_device *dev, int chip)
{
	unsigned int bits = 0;

	/*
	 * See P. 3.5 of the Register-Level Programming manual.
	 * The CounterSwap bit has to be set on the second chip,
	 * otherwise it will try to use the same pins as the
	 * first chip.
	 */
	if (chip)
		bits = NI660X_CLK_CFG_COUNTER_SWAP;

	ni_660x_write(dev, chip, bits, NI660X_CLK_CFG);
}

static void ni_660x_handle_gpct_interrupt(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;

	ni_tio_handle_interrupt(counter, s);
	comedi_handle_events(dev, s);
}

static irqreturn_t ni_660x_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct ni_660x_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	unsigned int i;
	unsigned long flags;

	if (!dev->attached)
		return IRQ_NONE;
	/* make sure dev->attached is checked before doing anything else */
	smp_mb();

	/* lock to avoid race with comedi_poll */
	spin_lock_irqsave(&devpriv->interrupt_lock, flags);
	for (i = 0; i < dev->n_subdevices; ++i) {
		s = &dev->subdevices[i];
		if (s->type == COMEDI_SUBD_COUNTER)
			ni_660x_handle_gpct_interrupt(dev, s);
	}
	spin_unlock_irqrestore(&devpriv->interrupt_lock, flags);
	return IRQ_HANDLED;
}

static int ni_660x_input_poll(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct ni_660x_private *devpriv = dev->private;
	struct ni_gpct *counter = s->private;
	unsigned long flags;

	/* lock to avoid race with comedi_poll */
	spin_lock_irqsave(&devpriv->interrupt_lock, flags);
	mite_sync_dma(counter->mite_chan, s);
	spin_unlock_irqrestore(&devpriv->interrupt_lock, flags);
	return comedi_buf_read_n_available(s);
}

static int ni_660x_buf_change(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct ni_660x_private *devpriv = dev->private;
	struct ni_gpct *counter = s->private;
	struct mite_ring *ring;
	int ret;

	ring = devpriv->ring[counter->chip_index][counter->counter_index];
	ret = mite_buf_change(ring, s);
	if (ret < 0)
		return ret;

	return 0;
}

static int ni_660x_allocate_private(struct comedi_device *dev)
{
	struct ni_660x_private *devpriv;
	unsigned int i;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	spin_lock_init(&devpriv->mite_channel_lock);
	spin_lock_init(&devpriv->interrupt_lock);
	for (i = 0; i < NI660X_NUM_PFI_CHANNELS; ++i)
		devpriv->io_cfg[i] = NI_660X_PFI_OUTPUT_COUNTER;

	return 0;
}

static int ni_660x_alloc_mite_rings(struct comedi_device *dev)
{
	const struct ni_660x_board *board = dev->board_ptr;
	struct ni_660x_private *devpriv = dev->private;
	unsigned int i;
	unsigned int j;

	for (i = 0; i < board->n_chips; ++i) {
		for (j = 0; j < NI660X_COUNTERS_PER_CHIP; ++j) {
			devpriv->ring[i][j] = mite_alloc_ring(devpriv->mite);
			if (!devpriv->ring[i][j])
				return -ENOMEM;
		}
	}
	return 0;
}

static void ni_660x_free_mite_rings(struct comedi_device *dev)
{
	const struct ni_660x_board *board = dev->board_ptr;
	struct ni_660x_private *devpriv = dev->private;
	unsigned int i;
	unsigned int j;

	for (i = 0; i < board->n_chips; ++i) {
		for (j = 0; j < NI660X_COUNTERS_PER_CHIP; ++j)
			mite_free_ring(devpriv->ring[i][j]);
	}
}

static int ni_660x_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int shift = CR_CHAN(insn->chanspec);
	unsigned int mask = data[0] << shift;
	unsigned int bits = data[1] << shift;

	/*
	 * There are 40 channels in this subdevice but only 32 are usable
	 * as DIO. The shift adjusts the mask/bits to account for the base
	 * channel in insn->chanspec. The state update can then be handled
	 * normally for the 32 usable channels.
	 */
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);
		ni_660x_write(dev, 0, s->state, NI660X_DIO32_OUTPUT);
	}

	/*
	 * Return the input channels, shifted back to account for the base
	 * channel.
	 */
	data[1] = ni_660x_read(dev, 0, NI660X_DIO32_INPUT) >> shift;

	return insn->n;
}

static void ni_660x_select_pfi_output(struct comedi_device *dev,
				      unsigned int chan, unsigned int out_sel)
{
	const struct ni_660x_board *board = dev->board_ptr;
	unsigned int active_chip = 0;
	unsigned int idle_chip = 0;
	unsigned int bits;

	if (board->n_chips > 1) {
		if (out_sel == NI_660X_PFI_OUTPUT_COUNTER &&
		    chan >= 8 && chan <= 23) {
			/* counters 4-7 pfi channels */
			active_chip = 1;
			idle_chip = 0;
		} else {
			/* counters 0-3 pfi channels */
			active_chip = 0;
			idle_chip = 1;
		}
	}

	if (idle_chip != active_chip) {
		/* set the pfi channel to high-z on the inactive chip */
		bits = ni_660x_read(dev, idle_chip, NI660X_IO_CFG(chan));
		bits &= ~NI660X_IO_CFG_OUT_SEL_MASK(chan);
		bits |= NI660X_IO_CFG_OUT_SEL(chan, 0);		/* high-z */
		ni_660x_write(dev, idle_chip, bits, NI660X_IO_CFG(chan));
	}

	/* set the pfi channel output on the active chip */
	bits = ni_660x_read(dev, active_chip, NI660X_IO_CFG(chan));
	bits &= ~NI660X_IO_CFG_OUT_SEL_MASK(chan);
	bits |= NI660X_IO_CFG_OUT_SEL(chan, out_sel);
	ni_660x_write(dev, active_chip, bits, NI660X_IO_CFG(chan));
}

static int ni_660x_set_pfi_routing(struct comedi_device *dev,
				   unsigned int chan, unsigned int source)
{
	struct ni_660x_private *devpriv = dev->private;

	switch (source) {
	case NI_660X_PFI_OUTPUT_COUNTER:
		if (chan < 8)
			return -EINVAL;
		break;
	case NI_660X_PFI_OUTPUT_DIO:
		if (chan > 31)
			return -EINVAL;
	default:
		return -EINVAL;
	}

	devpriv->io_cfg[chan] = source;
	if (devpriv->io_dir & (1ULL << chan))
		ni_660x_select_pfi_output(dev, chan, devpriv->io_cfg[chan]);
	return 0;
}

static int ni_660x_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	u64 bit = 1ULL << chan;
	unsigned int val;
	int ret;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		devpriv->io_dir |= bit;
		ni_660x_select_pfi_output(dev, chan, devpriv->io_cfg[chan]);
		break;

	case INSN_CONFIG_DIO_INPUT:
		devpriv->io_dir &= ~bit;
		ni_660x_select_pfi_output(dev, chan, 0);	/* high-z */
		break;

	case INSN_CONFIG_DIO_QUERY:
		data[1] = (devpriv->io_dir & bit) ? COMEDI_OUTPUT
						  : COMEDI_INPUT;
		break;

	case INSN_CONFIG_SET_ROUTING:
		ret = ni_660x_set_pfi_routing(dev, chan, data[1]);
		if (ret)
			return ret;
		break;

	case INSN_CONFIG_GET_ROUTING:
		data[1] = devpriv->io_cfg[chan];
		break;

	case INSN_CONFIG_FILTER:
		val = ni_660x_read(dev, 0, NI660X_IO_CFG(chan));
		val &= ~NI660X_IO_CFG_IN_SEL_MASK(chan);
		val |= NI660X_IO_CFG_IN_SEL(chan, data[1]);
		ni_660x_write(dev, 0, val, NI660X_IO_CFG(chan));
		break;

	default:
		return -EINVAL;
	}

	return insn->n;
}

static void ni_660x_init_tio_chips(struct comedi_device *dev,
				   unsigned int n_chips)
{
	struct ni_660x_private *devpriv = dev->private;
	unsigned int chip;
	unsigned int chan;

	/*
	 * We use the ioconfig registers to control dio direction, so zero
	 * output enables in stc dio control reg.
	 */
	ni_660x_write(dev, 0, 0, NI660X_STC_DIO_CONTROL);

	for (chip = 0; chip < n_chips; ++chip) {
		/* init dma configuration register */
		devpriv->dma_cfg[chip] = 0;
		for (chan = 0; chan < NI660X_MAX_DMA_CHANNEL; ++chan)
			devpriv->dma_cfg[chip] |= NI660X_DMA_CFG_SEL_NONE(chan);
		ni_660x_write(dev, chip, devpriv->dma_cfg[chip],
			      NI660X_DMA_CFG);

		/* init ioconfig registers */
		for (chan = 0; chan < NI660X_NUM_PFI_CHANNELS; ++chan)
			ni_660x_write(dev, chip, 0, NI660X_IO_CFG(chan));
	}
}

static int ni_660x_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct ni_660x_board *board = NULL;
	struct ni_660x_private *devpriv;
	struct comedi_subdevice *s;
	struct ni_gpct_device *gpct_dev;
	unsigned int n_counters;
	int subdev;
	int ret;
	unsigned int i;
	unsigned int global_interrupt_config_bits;

	if (context < ARRAY_SIZE(ni_660x_boards))
		board = &ni_660x_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	ret = ni_660x_allocate_private(dev);
	if (ret < 0)
		return ret;
	devpriv = dev->private;

	devpriv->mite = mite_attach(dev, true);		/* use win1 */
	if (!devpriv->mite)
		return -ENOMEM;

	ret = ni_660x_alloc_mite_rings(dev);
	if (ret < 0)
		return ret;

	ni_660x_init_tio_chips(dev, board->n_chips);

	n_counters = board->n_chips * NI660X_COUNTERS_PER_CHIP;
	gpct_dev = ni_gpct_device_construct(dev,
					    ni_660x_gpct_write,
					    ni_660x_gpct_read,
					    ni_gpct_variant_660x,
					    n_counters);
	if (!gpct_dev)
		return -ENOMEM;
	devpriv->counter_dev = gpct_dev;

	ret = comedi_alloc_subdevices(dev, 2 + NI660X_MAX_COUNTERS);
	if (ret)
		return ret;

	subdev = 0;

	s = &dev->subdevices[subdev++];
	/* Old GENERAL-PURPOSE COUNTER/TIME (GPCT) subdevice, no longer used */
	s->type = COMEDI_SUBD_UNUSED;

	/*
	 * Digital I/O subdevice
	 *
	 * There are 40 channels but only the first 32 can be digital I/Os.
	 * The last 8 are dedicated to counters 0 and 1.
	 *
	 * Counter 0-3 signals are from the first TIO chip.
	 * Counter 4-7 signals are from the second TIO chip.
	 *
	 * Comedi	External
	 * PFI Chan	DIO Chan        Counter Signal
	 * -------	--------	--------------
	 *     0	    0
	 *     1	    1
	 *     2	    2
	 *     3	    3
	 *     4	    4
	 *     5	    5
	 *     6	    6
	 *     7	    7
	 *     8	    8		CTR 7 OUT
	 *     9	    9		CTR 7 AUX
	 *    10	   10		CTR 7 GATE
	 *    11	   11		CTR 7 SOURCE
	 *    12	   12		CTR 6 OUT
	 *    13	   13		CTR 6 AUX
	 *    14	   14		CTR 6 GATE
	 *    15	   15		CTR 6 SOURCE
	 *    16	   16		CTR 5 OUT
	 *    17	   17		CTR 5 AUX
	 *    18	   18		CTR 5 GATE
	 *    19	   19		CTR 5 SOURCE
	 *    20	   20		CTR 4 OUT
	 *    21	   21		CTR 4 AUX
	 *    22	   22		CTR 4 GATE
	 *    23	   23		CTR 4 SOURCE
	 *    24	   24		CTR 3 OUT
	 *    25	   25		CTR 3 AUX
	 *    26	   26		CTR 3 GATE
	 *    27	   27		CTR 3 SOURCE
	 *    28	   28		CTR 2 OUT
	 *    29	   29		CTR 2 AUX
	 *    30	   30		CTR 2 GATE
	 *    31	   31		CTR 2 SOURCE
	 *    32			CTR 1 OUT
	 *    33			CTR 1 AUX
	 *    34			CTR 1 GATE
	 *    35			CTR 1 SOURCE
	 *    36			CTR 0 OUT
	 *    37			CTR 0 AUX
	 *    38			CTR 0 GATE
	 *    39			CTR 0 SOURCE
	 */
	s = &dev->subdevices[subdev++];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= NI660X_NUM_PFI_CHANNELS;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= ni_660x_dio_insn_bits;
	s->insn_config	= ni_660x_dio_insn_config;

	 /*
	  * Default the DIO channels as:
	  *   chan 0-7:  DIO inputs
	  *   chan 8-39: counter signal inputs
	  */
	for (i = 0; i < s->n_chan; ++i) {
		unsigned int source = (i < 8) ? NI_660X_PFI_OUTPUT_DIO
					      : NI_660X_PFI_OUTPUT_COUNTER;

		ni_660x_set_pfi_routing(dev, i, source);
		ni_660x_select_pfi_output(dev, i, 0);		/* high-z */
	}

	/* Counter subdevices (4 NI TIO General Purpose Counters per chip) */
	for (i = 0; i < NI660X_MAX_COUNTERS; ++i) {
		s = &dev->subdevices[subdev++];
		if (i < n_counters) {
			struct ni_gpct *counter = &gpct_dev->counters[i];

			counter->chip_index = i / NI660X_COUNTERS_PER_CHIP;
			counter->counter_index = i % NI660X_COUNTERS_PER_CHIP;

			s->type		= COMEDI_SUBD_COUNTER;
			s->subdev_flags	= SDF_READABLE | SDF_WRITABLE |
					  SDF_LSAMPL | SDF_CMD_READ;
			s->n_chan	= 3;
			s->maxdata	= 0xffffffff;
			s->insn_read	= ni_tio_insn_read;
			s->insn_write	= ni_tio_insn_write;
			s->insn_config	= ni_tio_insn_config;
			s->len_chanlist	= 1;
			s->do_cmd	= ni_660x_cmd;
			s->do_cmdtest	= ni_tio_cmdtest;
			s->cancel	= ni_660x_cancel;
			s->poll		= ni_660x_input_poll;
			s->buf_change	= ni_660x_buf_change;
			s->async_dma_dir = DMA_BIDIRECTIONAL;
			s->private	= counter;

			ni_tio_init_counter(counter);
		} else {
			s->type		= COMEDI_SUBD_UNUSED;
		}
	}

	/*
	 * To be safe, set counterswap bits on tio chips after all the counter
	 * outputs have been set to high impedance mode.
	 */
	for (i = 0; i < board->n_chips; ++i)
		set_tio_counterswap(dev, i);

	ret = request_irq(pcidev->irq, ni_660x_interrupt, IRQF_SHARED,
			  dev->board_name, dev);
	if (ret < 0) {
		dev_warn(dev->class_dev, " irq not available\n");
		return ret;
	}
	dev->irq = pcidev->irq;
	global_interrupt_config_bits = NI660X_GLOBAL_INT_GLOBAL;
	if (board->n_chips > 1)
		global_interrupt_config_bits |= NI660X_GLOBAL_INT_CASCADE;
	ni_660x_write(dev, 0, global_interrupt_config_bits,
		      NI660X_GLOBAL_INT_CFG);

	return 0;
}

static void ni_660x_detach(struct comedi_device *dev)
{
	struct ni_660x_private *devpriv = dev->private;

	if (dev->irq) {
		ni_660x_write(dev, 0, 0, NI660X_GLOBAL_INT_CFG);
		free_irq(dev->irq, dev);
	}
	if (devpriv) {
		ni_gpct_device_destroy(devpriv->counter_dev);
		ni_660x_free_mite_rings(dev);
		mite_detach(devpriv->mite);
	}
	if (dev->mmio)
		iounmap(dev->mmio);
	comedi_pci_disable(dev);
}

static struct comedi_driver ni_660x_driver = {
	.driver_name	= "ni_660x",
	.module		= THIS_MODULE,
	.auto_attach	= ni_660x_auto_attach,
	.detach		= ni_660x_detach,
};

static int ni_660x_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ni_660x_driver, id->driver_data);
}

static const struct pci_device_id ni_660x_pci_table[] = {
	{ PCI_VDEVICE(NI, 0x1310), BOARD_PCI6602 },
	{ PCI_VDEVICE(NI, 0x1360), BOARD_PXI6602 },
	{ PCI_VDEVICE(NI, 0x2c60), BOARD_PCI6601 },
	{ PCI_VDEVICE(NI, 0x2cc0), BOARD_PXI6608 },
	{ PCI_VDEVICE(NI, 0x1e30), BOARD_PCI6624 },
	{ PCI_VDEVICE(NI, 0x1e40), BOARD_PXI6624 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni_660x_pci_table);

static struct pci_driver ni_660x_pci_driver = {
	.name		= "ni_660x",
	.id_table	= ni_660x_pci_table,
	.probe		= ni_660x_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ni_660x_driver, ni_660x_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for NI 660x counter/timer boards");
MODULE_LICENSE("GPL");
