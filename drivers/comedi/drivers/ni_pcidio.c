// SPDX-License-Identifier: GPL-2.0+
/*
 * Comedi driver for National Instruments PCI-DIO-32HS
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1999,2002 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: ni_pcidio
 * Description: National Instruments PCI-DIO32HS, PCI-6533
 * Author: ds
 * Status: works
 * Devices: [National Instruments] PCI-DIO-32HS (ni_pcidio)
 *   [National Instruments] PXI-6533, PCI-6533 (pxi-6533)
 *   [National Instruments] PCI-6534 (pci-6534)
 * Updated: Mon, 09 Jan 2012 14:27:23 +0000
 *
 * The DIO32HS board appears as one subdevice, with 32 channels. Each
 * channel is individually I/O configurable. The channel order is 0=A0,
 * 1=A1, 2=A2, ... 8=B0, 16=C0, 24=D0. The driver only supports simple
 * digital I/O; no handshaking is supported.
 *
 * DMA mostly works for the PCI-DIO32HS, but only in timed input mode.
 *
 * The PCI-DIO-32HS/PCI-6533 has a configurable external trigger. Setting
 * scan_begin_arg to 0 or CR_EDGE triggers on the leading edge. Setting
 * scan_begin_arg to CR_INVERT or (CR_EDGE | CR_INVERT) triggers on the
 * trailing edge.
 *
 * This driver could be easily modified to support AT-MIO32HS and AT-MIO96.
 *
 * The PCI-6534 requires a firmware upload after power-up to work, the
 * firmware data and instructions for loading it with comedi_config
 * it are contained in the comedi_nonfree_firmware tarball available from
 * https://www.comedi.org
 */

#define USE_DMA

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/comedi/comedi_pci.h>

#include "mite.h"

/* defines for the PCI-DIO-32HS */

#define WINDOW_ADDRESS			4	/* W */
#define INTERRUPT_AND_WINDOW_STATUS	4	/* R */
#define INT_STATUS_1				BIT(0)
#define INT_STATUS_2				BIT(1)
#define WINDOW_ADDRESS_STATUS_MASK		0x7c

#define MASTER_DMA_AND_INTERRUPT_CONTROL 5	/* W */
#define INTERRUPT_LINE(x)			((x) & 3)
#define OPEN_INT				BIT(2)
#define GROUP_STATUS			5	/* R */
#define DATA_LEFT				BIT(0)
#define REQ					BIT(2)
#define STOP_TRIG				BIT(3)

#define GROUP_1_FLAGS			6	/* R */
#define GROUP_2_FLAGS			7	/* R */
#define TRANSFER_READY				BIT(0)
#define COUNT_EXPIRED				BIT(1)
#define WAITED					BIT(5)
#define PRIMARY_TC				BIT(6)
#define SECONDARY_TC				BIT(7)
  /* #define SerialRose */
  /* #define ReqRose */
  /* #define Paused */

#define GROUP_1_FIRST_CLEAR		6	/* W */
#define GROUP_2_FIRST_CLEAR		7	/* W */
#define CLEAR_WAITED				BIT(3)
#define CLEAR_PRIMARY_TC			BIT(4)
#define CLEAR_SECONDARY_TC			BIT(5)
#define DMA_RESET				BIT(6)
#define FIFO_RESET				BIT(7)
#define CLEAR_ALL				0xf8

#define GROUP_1_FIFO			8	/* W */
#define GROUP_2_FIFO			12	/* W */

#define TRANSFER_COUNT			20
#define CHIP_ID_D			24
#define CHIP_ID_I			25
#define CHIP_ID_O			26
#define CHIP_VERSION			27
#define PORT_IO(x)			(28 + (x))
#define PORT_PIN_DIRECTIONS(x)		(32 + (x))
#define PORT_PIN_MASK(x)		(36 + (x))
#define PORT_PIN_POLARITIES(x)		(40 + (x))

#define MASTER_CLOCK_ROUTING		45
#define RTSI_CLOCKING(x)			(((x) & 3) << 4)

#define GROUP_1_SECOND_CLEAR		46	/* W */
#define GROUP_2_SECOND_CLEAR		47	/* W */
#define CLEAR_EXPIRED				BIT(0)

#define PORT_PATTERN(x)			(48 + (x))

#define DATA_PATH			64
#define FIFO_ENABLE_A		BIT(0)
#define FIFO_ENABLE_B		BIT(1)
#define FIFO_ENABLE_C		BIT(2)
#define FIFO_ENABLE_D		BIT(3)
#define FUNNELING(x)		(((x) & 3) << 4)
#define GROUP_DIRECTION		BIT(7)

#define PROTOCOL_REGISTER_1		65
#define OP_MODE			PROTOCOL_REGISTER_1
#define RUN_MODE(x)		((x) & 7)
#define NUMBERED		BIT(3)

#define PROTOCOL_REGISTER_2		66
#define CLOCK_REG			PROTOCOL_REGISTER_2
#define CLOCK_LINE(x)		(((x) & 3) << 5)
#define INVERT_STOP_TRIG		BIT(7)
#define DATA_LATCHING(x)       (((x) & 3) << 5)

#define PROTOCOL_REGISTER_3		67
#define SEQUENCE			PROTOCOL_REGISTER_3

#define PROTOCOL_REGISTER_14		68	/* 16 bit */
#define CLOCK_SPEED			PROTOCOL_REGISTER_14

#define PROTOCOL_REGISTER_4		70
#define REQ_REG			PROTOCOL_REGISTER_4
#define REQ_CONDITIONING(x)	(((x) & 7) << 3)

#define PROTOCOL_REGISTER_5		71
#define BLOCK_MODE			PROTOCOL_REGISTER_5

#define FIFO_Control			72
#define READY_LEVEL(x)		((x) & 7)

#define PROTOCOL_REGISTER_6		73
#define LINE_POLARITIES		PROTOCOL_REGISTER_6
#define INVERT_ACK		BIT(0)
#define INVERT_REQ		BIT(1)
#define INVERT_CLOCK		BIT(2)
#define INVERT_SERIAL		BIT(3)
#define OPEN_ACK		BIT(4)
#define OPEN_CLOCK		BIT(5)

#define PROTOCOL_REGISTER_7		74
#define ACK_SER			PROTOCOL_REGISTER_7
#define ACK_LINE(x)		(((x) & 3) << 2)
#define EXCHANGE_PINS		BIT(7)

#define INTERRUPT_CONTROL		75
/* bits same as flags */

#define DMA_LINE_CONTROL_GROUP1		76
#define DMA_LINE_CONTROL_GROUP2		108

/* channel zero is none */
static inline unsigned int primary_DMAChannel_bits(unsigned int channel)
{
	return channel & 0x3;
}

static inline unsigned int secondary_DMAChannel_bits(unsigned int channel)
{
	return (channel << 2) & 0xc;
}

#define TRANSFER_SIZE_CONTROL		77
#define TRANSFER_WIDTH(x)	((x) & 3)
#define TRANSFER_LENGTH(x)	(((x) & 3) << 3)
#define REQUIRE_R_LEVEL        BIT(5)

#define PROTOCOL_REGISTER_15		79
#define DAQ_OPTIONS			PROTOCOL_REGISTER_15
#define START_SOURCE(x)			((x) & 0x3)
#define INVERT_START				BIT(2)
#define STOP_SOURCE(x)				(((x) & 0x3) << 3)
#define REQ_START				BIT(6)
#define PRE_START				BIT(7)

#define PATTERN_DETECTION		81
#define DETECTION_METHOD			BIT(0)
#define INVERT_MATCH				BIT(1)
#define IE_PATTERN_DETECTION			BIT(2)

#define PROTOCOL_REGISTER_9		82
#define REQ_DELAY			PROTOCOL_REGISTER_9

#define PROTOCOL_REGISTER_10		83
#define REQ_NOT_DELAY			PROTOCOL_REGISTER_10

#define PROTOCOL_REGISTER_11		84
#define ACK_DELAY			PROTOCOL_REGISTER_11

#define PROTOCOL_REGISTER_12		85
#define ACK_NOT_DELAY			PROTOCOL_REGISTER_12

#define PROTOCOL_REGISTER_13		86
#define DATA_1_DELAY			PROTOCOL_REGISTER_13

#define PROTOCOL_REGISTER_8		88	/* 32 bit */
#define START_DELAY			PROTOCOL_REGISTER_8

/* Firmware files for PCI-6524 */
#define FW_PCI_6534_MAIN		"ni6534a.bin"
#define FW_PCI_6534_SCARAB_DI		"niscrb01.bin"
#define FW_PCI_6534_SCARAB_DO		"niscrb02.bin"
MODULE_FIRMWARE(FW_PCI_6534_MAIN);
MODULE_FIRMWARE(FW_PCI_6534_SCARAB_DI);
MODULE_FIRMWARE(FW_PCI_6534_SCARAB_DO);

enum pci_6534_firmware_registers {	/* 16 bit */
	Firmware_Control_Register = 0x100,
	Firmware_Status_Register = 0x104,
	Firmware_Data_Register = 0x108,
	Firmware_Mask_Register = 0x10c,
	Firmware_Debug_Register = 0x110,
};

/* main fpga registers (32 bit)*/
enum pci_6534_fpga_registers {
	FPGA_Control1_Register = 0x200,
	FPGA_Control2_Register = 0x204,
	FPGA_Irq_Mask_Register = 0x208,
	FPGA_Status_Register = 0x20c,
	FPGA_Signature_Register = 0x210,
	FPGA_SCALS_Counter_Register = 0x280,	/*write-clear */
	FPGA_SCAMS_Counter_Register = 0x284,	/*write-clear */
	FPGA_SCBLS_Counter_Register = 0x288,	/*write-clear */
	FPGA_SCBMS_Counter_Register = 0x28c,	/*write-clear */
	FPGA_Temp_Control_Register = 0x2a0,
	FPGA_DAR_Register = 0x2a8,
	FPGA_ELC_Read_Register = 0x2b8,
	FPGA_ELC_Write_Register = 0x2bc,
};

enum FPGA_Control_Bits {
	FPGA_Enable_Bit = 0x8000,
};

#define TIMER_BASE 50		/* nanoseconds */

#ifdef USE_DMA
#define INT_EN (COUNT_EXPIRED | WAITED | PRIMARY_TC | SECONDARY_TC)
#else
#define INT_EN (TRANSFER_READY | COUNT_EXPIRED | WAITED \
		| PRIMARY_TC | SECONDARY_TC)
#endif

enum nidio_boardid {
	BOARD_PCIDIO_32HS,
	BOARD_PXI6533,
	BOARD_PCI6534,
};

struct nidio_board {
	const char *name;
	unsigned int uses_firmware:1;
	unsigned int dio_speed;
};

static const struct nidio_board nidio_boards[] = {
	[BOARD_PCIDIO_32HS] = {
		.name		= "pci-dio-32hs",
		.dio_speed	= 50,
	},
	[BOARD_PXI6533] = {
		.name		= "pxi-6533",
		.dio_speed	= 50,
	},
	[BOARD_PCI6534] = {
		.name		= "pci-6534",
		.uses_firmware	= 1,
		.dio_speed	= 50,
	},
};

struct nidio96_private {
	struct mite *mite;
	int boardtype;
	int dio;
	unsigned short OP_MODEBits;
	struct mite_channel *di_mite_chan;
	struct mite_ring *di_mite_ring;
	spinlock_t mite_channel_lock;
};

static int ni_pcidio_request_di_mite_channel(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	BUG_ON(devpriv->di_mite_chan);
	devpriv->di_mite_chan =
	    mite_request_channel_in_range(devpriv->mite,
					  devpriv->di_mite_ring, 1, 2);
	if (!devpriv->di_mite_chan) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		dev_err(dev->class_dev, "failed to reserve mite dma channel\n");
		return -EBUSY;
	}
	devpriv->di_mite_chan->dir = COMEDI_INPUT;
	writeb(primary_DMAChannel_bits(devpriv->di_mite_chan->channel) |
	       secondary_DMAChannel_bits(devpriv->di_mite_chan->channel),
	       dev->mmio + DMA_LINE_CONTROL_GROUP1);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static void ni_pcidio_release_di_mite_channel(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->di_mite_chan) {
		mite_release_channel(devpriv->di_mite_chan);
		devpriv->di_mite_chan = NULL;
		writeb(primary_DMAChannel_bits(0) |
		       secondary_DMAChannel_bits(0),
		       dev->mmio + DMA_LINE_CONTROL_GROUP1);
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}

static int setup_mite_dma(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct nidio96_private *devpriv = dev->private;
	int retval;
	unsigned long flags;

	retval = ni_pcidio_request_di_mite_channel(dev);
	if (retval)
		return retval;

	/* write alloc the entire buffer */
	comedi_buf_write_alloc(s, s->async->prealloc_bufsz);

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->di_mite_chan) {
		mite_prep_dma(devpriv->di_mite_chan, 32, 32);
		mite_dma_arm(devpriv->di_mite_chan);
	} else {
		retval = -EIO;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	return retval;
}

static int ni_pcidio_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct nidio96_private *devpriv = dev->private;
	unsigned long irq_flags;
	int count;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	spin_lock(&devpriv->mite_channel_lock);
	if (devpriv->di_mite_chan)
		mite_sync_dma(devpriv->di_mite_chan, s);
	spin_unlock(&devpriv->mite_channel_lock);
	count = comedi_buf_n_bytes_ready(s);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
	return count;
}

static irqreturn_t nidio_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct nidio96_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	unsigned int auxdata;
	int flags;
	int status;
	int work = 0;

	/* interrupcions parasites */
	if (!dev->attached) {
		/* assume it's from another card */
		return IRQ_NONE;
	}

	/* Lock to avoid race with comedi_poll */
	spin_lock(&dev->spinlock);

	status = readb(dev->mmio + INTERRUPT_AND_WINDOW_STATUS);
	flags = readb(dev->mmio + GROUP_1_FLAGS);

	spin_lock(&devpriv->mite_channel_lock);
	if (devpriv->di_mite_chan) {
		mite_ack_linkc(devpriv->di_mite_chan, s, false);
		/* XXX need to byteswap sync'ed dma */
	}
	spin_unlock(&devpriv->mite_channel_lock);

	while (status & DATA_LEFT) {
		work++;
		if (work > 20) {
			dev_dbg(dev->class_dev, "too much work in interrupt\n");
			writeb(0x00,
			       dev->mmio + MASTER_DMA_AND_INTERRUPT_CONTROL);
			break;
		}

		flags &= INT_EN;

		if (flags & TRANSFER_READY) {
			while (flags & TRANSFER_READY) {
				work++;
				if (work > 100) {
					dev_dbg(dev->class_dev,
						"too much work in interrupt\n");
					writeb(0x00, dev->mmio +
					       MASTER_DMA_AND_INTERRUPT_CONTROL
					      );
					goto out;
				}
				auxdata = readl(dev->mmio + GROUP_1_FIFO);
				comedi_buf_write_samples(s, &auxdata, 1);
				flags = readb(dev->mmio + GROUP_1_FLAGS);
			}
		}

		if (flags & COUNT_EXPIRED) {
			writeb(CLEAR_EXPIRED, dev->mmio + GROUP_1_SECOND_CLEAR);
			async->events |= COMEDI_CB_EOA;

			writeb(0x00, dev->mmio + OP_MODE);
			break;
		} else if (flags & WAITED) {
			writeb(CLEAR_WAITED, dev->mmio + GROUP_1_FIRST_CLEAR);
			async->events |= COMEDI_CB_ERROR;
			break;
		} else if (flags & PRIMARY_TC) {
			writeb(CLEAR_PRIMARY_TC,
			       dev->mmio + GROUP_1_FIRST_CLEAR);
			async->events |= COMEDI_CB_EOA;
		} else if (flags & SECONDARY_TC) {
			writeb(CLEAR_SECONDARY_TC,
			       dev->mmio + GROUP_1_FIRST_CLEAR);
			async->events |= COMEDI_CB_EOA;
		}

		flags = readb(dev->mmio + GROUP_1_FLAGS);
		status = readb(dev->mmio + INTERRUPT_AND_WINDOW_STATUS);
	}

out:
	comedi_handle_events(dev, s);
#if 0
	if (!tag)
		writeb(0x03, dev->mmio + MASTER_DMA_AND_INTERRUPT_CONTROL);
#endif

	spin_unlock(&dev->spinlock);
	return IRQ_HANDLED;
}

static int ni_pcidio_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	int ret;

	if (data[0] == INSN_CONFIG_GET_CMD_TIMING_CONSTRAINTS) {
		const struct nidio_board *board = dev->board_ptr;

		/* we don't care about actual channels */
		data[1] = board->dio_speed;
		data[2] = 0;
		return 0;
	}

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	writel(s->io_bits, dev->mmio + PORT_PIN_DIRECTIONS(0));

	return insn->n;
}

static int ni_pcidio_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		writel(s->state, dev->mmio + PORT_IO(0));

	data[1] = readl(dev->mmio + PORT_IO(0));

	return insn->n;
}

static int ni_pcidio_ns_to_timer(int *nanosec, unsigned int flags)
{
	int divider, base;

	base = TIMER_BASE;

	switch (flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_NEAREST:
	default:
		divider = DIV_ROUND_CLOSEST(*nanosec, base);
		break;
	case CMDF_ROUND_DOWN:
		divider = (*nanosec) / base;
		break;
	case CMDF_ROUND_UP:
		divider = DIV_ROUND_UP(*nanosec, base);
		break;
	}

	*nanosec = base * divider;
	return divider;
}

static int ni_pcidio_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

#define MAX_SPEED	(TIMER_BASE)	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    MAX_SPEED);
		/* no minimum speed */
	} else {
		/* TRIG_EXT */
		/* should be level/edge, hi/lo specification here */
		if ((cmd->scan_begin_arg & ~(CR_EDGE | CR_INVERT)) != 0) {
			cmd->scan_begin_arg &= (CR_EDGE | CR_INVERT);
			err |= -EINVAL;
		}
	}

	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		ni_pcidio_ns_to_timer(&arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (err)
		return 4;

	return 0;
}

static int ni_pcidio_inttrig(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     unsigned int trig_num)
{
	struct nidio96_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	writeb(devpriv->OP_MODEBits, dev->mmio + OP_MODE);
	s->async->inttrig = NULL;

	return 1;
}

static int ni_pcidio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct nidio96_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	/* XXX configure ports for input */
	writel(0x0000, dev->mmio + PORT_PIN_DIRECTIONS(0));

	if (1) {
		/* enable fifos A B C D */
		writeb(0x0f, dev->mmio + DATA_PATH);

		/* set transfer width a 32 bits */
		writeb(TRANSFER_WIDTH(0) | TRANSFER_LENGTH(0),
		       dev->mmio + TRANSFER_SIZE_CONTROL);
	} else {
		writeb(0x03, dev->mmio + DATA_PATH);
		writeb(TRANSFER_WIDTH(3) | TRANSFER_LENGTH(0),
		       dev->mmio + TRANSFER_SIZE_CONTROL);
	}

	/* protocol configuration */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		/* page 4-5, "input with internal REQs" */
		writeb(0, dev->mmio + OP_MODE);
		writeb(0x00, dev->mmio + CLOCK_REG);
		writeb(1, dev->mmio + SEQUENCE);
		writeb(0x04, dev->mmio + REQ_REG);
		writeb(4, dev->mmio + BLOCK_MODE);
		writeb(3, dev->mmio + LINE_POLARITIES);
		writeb(0xc0, dev->mmio + ACK_SER);
		writel(ni_pcidio_ns_to_timer(&cmd->scan_begin_arg,
					     CMDF_ROUND_NEAREST),
		       dev->mmio + START_DELAY);
		writeb(1, dev->mmio + REQ_DELAY);
		writeb(1, dev->mmio + REQ_NOT_DELAY);
		writeb(1, dev->mmio + ACK_DELAY);
		writeb(0x0b, dev->mmio + ACK_NOT_DELAY);
		writeb(0x01, dev->mmio + DATA_1_DELAY);
		/*
		 * manual, page 4-5:
		 * CLOCK_SPEED comment is incorrectly listed on DAQ_OPTIONS
		 */
		writew(0, dev->mmio + CLOCK_SPEED);
		writeb(0, dev->mmio + DAQ_OPTIONS);
	} else {
		/* TRIG_EXT */
		/* page 4-5, "input with external REQs" */
		writeb(0, dev->mmio + OP_MODE);
		writeb(0x00, dev->mmio + CLOCK_REG);
		writeb(0, dev->mmio + SEQUENCE);
		writeb(0x00, dev->mmio + REQ_REG);
		writeb(4, dev->mmio + BLOCK_MODE);
		if (!(cmd->scan_begin_arg & CR_INVERT))	/* Leading Edge */
			writeb(0, dev->mmio + LINE_POLARITIES);
		else					/* Trailing Edge */
			writeb(2, dev->mmio + LINE_POLARITIES);
		writeb(0x00, dev->mmio + ACK_SER);
		writel(1, dev->mmio + START_DELAY);
		writeb(1, dev->mmio + REQ_DELAY);
		writeb(1, dev->mmio + REQ_NOT_DELAY);
		writeb(1, dev->mmio + ACK_DELAY);
		writeb(0x0C, dev->mmio + ACK_NOT_DELAY);
		writeb(0x10, dev->mmio + DATA_1_DELAY);
		writew(0, dev->mmio + CLOCK_SPEED);
		writeb(0x60, dev->mmio + DAQ_OPTIONS);
	}

	if (cmd->stop_src == TRIG_COUNT) {
		writel(cmd->stop_arg,
		       dev->mmio + TRANSFER_COUNT);
	} else {
		/* XXX */
	}

#ifdef USE_DMA
	writeb(CLEAR_PRIMARY_TC | CLEAR_SECONDARY_TC,
	       dev->mmio + GROUP_1_FIRST_CLEAR);

	{
		int retval = setup_mite_dma(dev, s);

		if (retval)
			return retval;
	}
#else
	writeb(0x00, dev->mmio + DMA_LINE_CONTROL_GROUP1);
#endif
	writeb(0x00, dev->mmio + DMA_LINE_CONTROL_GROUP2);

	/* clear and enable interrupts */
	writeb(0xff, dev->mmio + GROUP_1_FIRST_CLEAR);
	/* writeb(CLEAR_EXPIRED, dev->mmio+GROUP_1_SECOND_CLEAR); */

	writeb(INT_EN, dev->mmio + INTERRUPT_CONTROL);
	writeb(0x03, dev->mmio + MASTER_DMA_AND_INTERRUPT_CONTROL);

	if (cmd->stop_src == TRIG_NONE) {
		devpriv->OP_MODEBits = DATA_LATCHING(0) | RUN_MODE(7);
	} else {		/* TRIG_TIMER */
		devpriv->OP_MODEBits = NUMBERED | RUN_MODE(7);
	}
	if (cmd->start_src == TRIG_NOW) {
		/* start */
		writeb(devpriv->OP_MODEBits, dev->mmio + OP_MODE);
		s->async->inttrig = NULL;
	} else {
		/* TRIG_INT */
		s->async->inttrig = ni_pcidio_inttrig;
	}

	return 0;
}

static int ni_pcidio_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	writeb(0x00, dev->mmio + MASTER_DMA_AND_INTERRUPT_CONTROL);
	ni_pcidio_release_di_mite_channel(dev);

	return 0;
}

static int ni_pcidio_change(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct nidio96_private *devpriv = dev->private;
	int ret;

	ret = mite_buf_change(devpriv->di_mite_ring, s);
	if (ret < 0)
		return ret;

	memset(s->async->prealloc_buf, 0xaa, s->async->prealloc_bufsz);

	return 0;
}

static int pci_6534_load_fpga(struct comedi_device *dev,
			      const u8 *data, size_t data_len,
			      unsigned long context)
{
	static const int timeout = 1000;
	int fpga_index = context;
	int i;
	size_t j;

	writew(0x80 | fpga_index, dev->mmio + Firmware_Control_Register);
	writew(0xc0 | fpga_index, dev->mmio + Firmware_Control_Register);
	for (i = 0;
	     (readw(dev->mmio + Firmware_Status_Register) & 0x2) == 0 &&
	     i < timeout; ++i) {
		udelay(1);
	}
	if (i == timeout) {
		dev_warn(dev->class_dev,
			 "ni_pcidio: failed to load fpga %i, waiting for status 0x2\n",
			 fpga_index);
		return -EIO;
	}
	writew(0x80 | fpga_index, dev->mmio + Firmware_Control_Register);
	for (i = 0;
	     readw(dev->mmio + Firmware_Status_Register) != 0x3 &&
	     i < timeout; ++i) {
		udelay(1);
	}
	if (i == timeout) {
		dev_warn(dev->class_dev,
			 "ni_pcidio: failed to load fpga %i, waiting for status 0x3\n",
			 fpga_index);
		return -EIO;
	}
	for (j = 0; j + 1 < data_len;) {
		unsigned int value = data[j++];

		value |= data[j++] << 8;
		writew(value, dev->mmio + Firmware_Data_Register);
		for (i = 0;
		     (readw(dev->mmio + Firmware_Status_Register) & 0x2) == 0
		     && i < timeout; ++i) {
			udelay(1);
		}
		if (i == timeout) {
			dev_warn(dev->class_dev,
				 "ni_pcidio: failed to load word into fpga %i\n",
				 fpga_index);
			return -EIO;
		}
		if (need_resched())
			schedule();
	}
	writew(0x0, dev->mmio + Firmware_Control_Register);
	return 0;
}

static int pci_6534_reset_fpga(struct comedi_device *dev, int fpga_index)
{
	return pci_6534_load_fpga(dev, NULL, 0, fpga_index);
}

static int pci_6534_reset_fpgas(struct comedi_device *dev)
{
	int ret;
	int i;

	writew(0x0, dev->mmio + Firmware_Control_Register);
	for (i = 0; i < 3; ++i) {
		ret = pci_6534_reset_fpga(dev, i);
		if (ret < 0)
			break;
	}
	writew(0x0, dev->mmio + Firmware_Mask_Register);
	return ret;
}

static void pci_6534_init_main_fpga(struct comedi_device *dev)
{
	writel(0, dev->mmio + FPGA_Control1_Register);
	writel(0, dev->mmio + FPGA_Control2_Register);
	writel(0, dev->mmio + FPGA_SCALS_Counter_Register);
	writel(0, dev->mmio + FPGA_SCAMS_Counter_Register);
	writel(0, dev->mmio + FPGA_SCBLS_Counter_Register);
	writel(0, dev->mmio + FPGA_SCBMS_Counter_Register);
}

static int pci_6534_upload_firmware(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;
	static const char *const fw_file[3] = {
		FW_PCI_6534_SCARAB_DI,	/* loaded into scarab A for DI */
		FW_PCI_6534_SCARAB_DO,	/* loaded into scarab B for DO */
		FW_PCI_6534_MAIN,	/* loaded into main FPGA */
	};
	int ret;
	int n;

	ret = pci_6534_reset_fpgas(dev);
	if (ret < 0)
		return ret;
	/* load main FPGA first, then the two scarabs */
	for (n = 2; n >= 0; n--) {
		ret = comedi_load_firmware(dev, &devpriv->mite->pcidev->dev,
					   fw_file[n],
					   pci_6534_load_fpga, n);
		if (ret == 0 && n == 2)
			pci_6534_init_main_fpga(dev);
		if (ret < 0)
			break;
	}
	return ret;
}

static void nidio_reset_board(struct comedi_device *dev)
{
	writel(0, dev->mmio + PORT_IO(0));
	writel(0, dev->mmio + PORT_PIN_DIRECTIONS(0));
	writel(0, dev->mmio + PORT_PIN_MASK(0));

	/* disable interrupts on board */
	writeb(0, dev->mmio + MASTER_DMA_AND_INTERRUPT_CONTROL);
}

static int nidio_auto_attach(struct comedi_device *dev,
			     unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct nidio_board *board = NULL;
	struct nidio96_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	unsigned int irq;

	if (context < ARRAY_SIZE(nidio_boards))
		board = &nidio_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	spin_lock_init(&devpriv->mite_channel_lock);

	devpriv->mite = mite_attach(dev, false);	/* use win0 */
	if (!devpriv->mite)
		return -ENOMEM;

	devpriv->di_mite_ring = mite_alloc_ring(devpriv->mite);
	if (!devpriv->di_mite_ring)
		return -ENOMEM;

	if (board->uses_firmware) {
		ret = pci_6534_upload_firmware(dev);
		if (ret < 0)
			return ret;
	}

	nidio_reset_board(dev);

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	dev_info(dev->class_dev, "%s rev=%d\n", dev->board_name,
		 readb(dev->mmio + CHIP_VERSION));

	s = &dev->subdevices[0];

	dev->read_subdev = s;
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags =
		SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL | SDF_PACKED |
		SDF_CMD_READ;
	s->n_chan = 32;
	s->range_table = &range_digital;
	s->maxdata = 1;
	s->insn_config = &ni_pcidio_insn_config;
	s->insn_bits = &ni_pcidio_insn_bits;
	s->do_cmd = &ni_pcidio_cmd;
	s->do_cmdtest = &ni_pcidio_cmdtest;
	s->cancel = &ni_pcidio_cancel;
	s->len_chanlist = 32;	/* XXX */
	s->buf_change = &ni_pcidio_change;
	s->async_dma_dir = DMA_BIDIRECTIONAL;
	s->poll = &ni_pcidio_poll;

	irq = pcidev->irq;
	if (irq) {
		ret = request_irq(irq, nidio_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = irq;
	}

	return 0;
}

static void nidio_detach(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;

	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->di_mite_ring) {
			mite_free_ring(devpriv->di_mite_ring);
			devpriv->di_mite_ring = NULL;
		}
		mite_detach(devpriv->mite);
	}
	if (dev->mmio)
		iounmap(dev->mmio);
	comedi_pci_disable(dev);
}

static struct comedi_driver ni_pcidio_driver = {
	.driver_name	= "ni_pcidio",
	.module		= THIS_MODULE,
	.auto_attach	= nidio_auto_attach,
	.detach		= nidio_detach,
};

static int ni_pcidio_pci_probe(struct pci_dev *dev,
			       const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ni_pcidio_driver, id->driver_data);
}

static const struct pci_device_id ni_pcidio_pci_table[] = {
	{ PCI_VDEVICE(NI, 0x1150), BOARD_PCIDIO_32HS },
	{ PCI_VDEVICE(NI, 0x12b0), BOARD_PCI6534 },
	{ PCI_VDEVICE(NI, 0x1320), BOARD_PXI6533 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni_pcidio_pci_table);

static struct pci_driver ni_pcidio_pci_driver = {
	.name		= "ni_pcidio",
	.id_table	= ni_pcidio_pci_table,
	.probe		= ni_pcidio_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ni_pcidio_driver, ni_pcidio_pci_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
