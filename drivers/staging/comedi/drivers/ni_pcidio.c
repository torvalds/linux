/*
    comedi/drivers/ni_pcidio.c
    driver for National Instruments PCI-DIO-32HS

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1999,2002 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/
/*
Driver: ni_pcidio
Description: National Instruments PCI-DIO32HS, PCI-6533
Author: ds
Status: works
Devices: [National Instruments] PCI-DIO-32HS (ni_pcidio)
	 [National Instruments] PXI-6533, PCI-6533 (pxi-6533)
	 [National Instruments] PCI-6534 (pci-6534)
Updated: Mon, 09 Jan 2012 14:27:23 +0000

The DIO32HS board appears as one subdevice, with 32 channels.
Each channel is individually I/O configurable.  The channel order
is 0=A0, 1=A1, 2=A2, ... 8=B0, 16=C0, 24=D0.  The driver only
supports simple digital I/O; no handshaking is supported.

DMA mostly works for the PCI-DIO32HS, but only in timed input mode.

The PCI-DIO-32HS/PCI-6533 has a configurable external trigger. Setting
scan_begin_arg to 0 or CR_EDGE triggers on the leading edge. Setting
scan_begin_arg to CR_INVERT or (CR_EDGE | CR_INVERT) triggers on the
trailing edge.

This driver could be easily modified to support AT-MIO32HS and
AT-MIO96.

The PCI-6534 requires a firmware upload after power-up to work, the
firmware data and instructions for loading it with comedi_config
it are contained in the
comedi_nonfree_firmware tarball available from http://www.comedi.org
*/

#define USE_DMA

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "mite.h"

/* defines for the PCI-DIO-32HS */

#define Window_Address			4	/* W */
#define Interrupt_And_Window_Status	4	/* R */
#define IntStatus1				(1<<0)
#define IntStatus2				(1<<1)
#define WindowAddressStatus_mask		0x7c

#define Master_DMA_And_Interrupt_Control 5	/* W */
#define InterruptLine(x)			((x)&3)
#define OpenInt				(1<<2)
#define Group_Status			5	/* R */
#define DataLeft				(1<<0)
#define Req					(1<<2)
#define StopTrig				(1<<3)

#define Group_1_Flags			6	/* R */
#define Group_2_Flags			7	/* R */
#define TransferReady				(1<<0)
#define CountExpired				(1<<1)
#define Waited				(1<<5)
#define PrimaryTC				(1<<6)
#define SecondaryTC				(1<<7)
  /* #define SerialRose */
  /* #define ReqRose */
  /* #define Paused */

#define Group_1_First_Clear		6	/* W */
#define Group_2_First_Clear		7	/* W */
#define ClearWaited				(1<<3)
#define ClearPrimaryTC			(1<<4)
#define ClearSecondaryTC			(1<<5)
#define DMAReset				(1<<6)
#define FIFOReset				(1<<7)
#define ClearAll				0xf8

#define Group_1_FIFO			8	/* W */
#define Group_2_FIFO			12	/* W */

#define Transfer_Count			20
#define Chip_ID_D			24
#define Chip_ID_I			25
#define Chip_ID_O			26
#define Chip_Version			27
#define Port_IO(x)			(28+(x))
#define Port_Pin_Directions(x)		(32+(x))
#define Port_Pin_Mask(x)		(36+(x))
#define Port_Pin_Polarities(x)		(40+(x))

#define Master_Clock_Routing		45
#define RTSIClocking(x)			(((x)&3)<<4)

#define Group_1_Second_Clear		46	/* W */
#define Group_2_Second_Clear		47	/* W */
#define ClearExpired				(1<<0)

#define Port_Pattern(x)			(48+(x))

#define Data_Path			64
#define FIFOEnableA		(1<<0)
#define FIFOEnableB		(1<<1)
#define FIFOEnableC		(1<<2)
#define FIFOEnableD		(1<<3)
#define Funneling(x)		(((x)&3)<<4)
#define GroupDirection	(1<<7)

#define Protocol_Register_1		65
#define OpMode				Protocol_Register_1
#define RunMode(x)		((x)&7)
#define Numbered		(1<<3)

#define Protocol_Register_2		66
#define ClockReg			Protocol_Register_2
#define ClockLine(x)		(((x)&3)<<5)
#define InvertStopTrig	(1<<7)
#define DataLatching(x)       (((x)&3)<<5)

#define Protocol_Register_3		67
#define Sequence			Protocol_Register_3

#define Protocol_Register_14		68	/* 16 bit */
#define ClockSpeed			Protocol_Register_14

#define Protocol_Register_4		70
#define ReqReg				Protocol_Register_4
#define ReqConditioning(x)	(((x)&7)<<3)

#define Protocol_Register_5		71
#define BlockMode			Protocol_Register_5

#define FIFO_Control			72
#define ReadyLevel(x)		((x)&7)

#define Protocol_Register_6		73
#define LinePolarities			Protocol_Register_6
#define InvertAck		(1<<0)
#define InvertReq		(1<<1)
#define InvertClock		(1<<2)
#define InvertSerial		(1<<3)
#define OpenAck		(1<<4)
#define OpenClock		(1<<5)

#define Protocol_Register_7		74
#define AckSer				Protocol_Register_7
#define AckLine(x)		(((x)&3)<<2)
#define ExchangePins		(1<<7)

#define Interrupt_Control		75
  /* bits same as flags */

#define DMA_Line_Control_Group1		76
#define DMA_Line_Control_Group2		108
/* channel zero is none */
static inline unsigned primary_DMAChannel_bits(unsigned channel)
{
	return channel & 0x3;
}

static inline unsigned secondary_DMAChannel_bits(unsigned channel)
{
	return (channel << 2) & 0xc;
}

#define Transfer_Size_Control		77
#define TransferWidth(x)	((x)&3)
#define TransferLength(x)	(((x)&3)<<3)
#define RequireRLevel		(1<<5)

#define Protocol_Register_15		79
#define DAQOptions			Protocol_Register_15
#define StartSource(x)			((x)&0x3)
#define InvertStart				(1<<2)
#define StopSource(x)				(((x)&0x3)<<3)
#define ReqStart				(1<<6)
#define PreStart				(1<<7)

#define Pattern_Detection		81
#define DetectionMethod			(1<<0)
#define InvertMatch				(1<<1)
#define IE_Pattern_Detection			(1<<2)

#define Protocol_Register_9		82
#define ReqDelay			Protocol_Register_9

#define Protocol_Register_10		83
#define ReqNotDelay			Protocol_Register_10

#define Protocol_Register_11		84
#define AckDelay			Protocol_Register_11

#define Protocol_Register_12		85
#define AckNotDelay			Protocol_Register_12

#define Protocol_Register_13		86
#define Data1Delay			Protocol_Register_13

#define Protocol_Register_8		88	/* 32 bit */
#define StartDelay			Protocol_Register_8

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
#define IntEn (CountExpired|Waited|PrimaryTC|SecondaryTC)
#else
#define IntEn (TransferReady|CountExpired|Waited|PrimaryTC|SecondaryTC)
#endif

enum nidio_boardid {
	BOARD_PCIDIO_32HS,
	BOARD_PXI6533,
	BOARD_PCI6534,
};

struct nidio_board {
	const char *name;
	unsigned int uses_firmware:1;
};

static const struct nidio_board nidio_boards[] = {
	[BOARD_PCIDIO_32HS] = {
		.name		= "pci-dio-32hs",
	},
	[BOARD_PXI6533] = {
		.name		= "pxi-6533",
	},
	[BOARD_PCI6534] = {
		.name		= "pci-6534",
		.uses_firmware	= 1,
	},
};

struct nidio96_private {
	struct mite_struct *mite;
	int boardtype;
	int dio;
	unsigned short OpModeBits;
	struct mite_channel *di_mite_chan;
	struct mite_dma_descriptor_ring *di_mite_ring;
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
	if (devpriv->di_mite_chan == NULL) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		dev_err(dev->class_dev, "failed to reserve mite dma channel\n");
		return -EBUSY;
	}
	devpriv->di_mite_chan->dir = COMEDI_INPUT;
	writeb(primary_DMAChannel_bits(devpriv->di_mite_chan->channel) |
	       secondary_DMAChannel_bits(devpriv->di_mite_chan->channel),
	       dev->mmio + DMA_Line_Control_Group1);
	mmiowb();
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static void ni_pcidio_release_di_mite_channel(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->di_mite_chan) {
		mite_dma_disarm(devpriv->di_mite_chan);
		mite_dma_reset(devpriv->di_mite_chan);
		mite_release_channel(devpriv->di_mite_chan);
		devpriv->di_mite_chan = NULL;
		writeb(primary_DMAChannel_bits(0) |
		       secondary_DMAChannel_bits(0),
		       dev->mmio + DMA_Line_Control_Group1);
		mmiowb();
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
	} else
		retval = -EIO;
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
		mite_sync_input_dma(devpriv->di_mite_chan, s);
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
	struct mite_struct *mite = devpriv->mite;

	/* int i, j; */
	unsigned int auxdata = 0;
	unsigned short data1 = 0;
	unsigned short data2 = 0;
	int flags;
	int status;
	int work = 0;
	unsigned int m_status = 0;

	/* interrupcions parasites */
	if (!dev->attached) {
		/* assume it's from another card */
		return IRQ_NONE;
	}

	/* Lock to avoid race with comedi_poll */
	spin_lock(&dev->spinlock);

	status = readb(dev->mmio + Interrupt_And_Window_Status);
	flags = readb(dev->mmio + Group_1_Flags);

	spin_lock(&devpriv->mite_channel_lock);
	if (devpriv->di_mite_chan)
		m_status = mite_get_status(devpriv->di_mite_chan);

	if (m_status & CHSR_INT) {
		if (m_status & CHSR_LINKC) {
			writel(CHOR_CLRLC,
			       mite->mite_io_addr +
			       MITE_CHOR(devpriv->di_mite_chan->channel));
			mite_sync_input_dma(devpriv->di_mite_chan, s);
			/* XXX need to byteswap */
		}
		if (m_status & ~(CHSR_INT | CHSR_LINKC | CHSR_DONE | CHSR_DRDY |
				 CHSR_DRQ1 | CHSR_MRDY)) {
			dev_dbg(dev->class_dev,
				"unknown mite interrupt, disabling IRQ\n");
			async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
			disable_irq(dev->irq);
		}
	}
	spin_unlock(&devpriv->mite_channel_lock);

	while (status & DataLeft) {
		work++;
		if (work > 20) {
			dev_dbg(dev->class_dev, "too much work in interrupt\n");
			writeb(0x00,
			       dev->mmio + Master_DMA_And_Interrupt_Control);
			break;
		}

		flags &= IntEn;

		if (flags & TransferReady) {
			while (flags & TransferReady) {
				work++;
				if (work > 100) {
					dev_dbg(dev->class_dev,
						"too much work in interrupt\n");
					writeb(0x00, dev->mmio +
					       Master_DMA_And_Interrupt_Control
					      );
					goto out;
				}
				auxdata = readl(dev->mmio + Group_1_FIFO);
				data1 = auxdata & 0xffff;
				data2 = (auxdata & 0xffff0000) >> 16;
				comedi_buf_put(s, data1);
				comedi_buf_put(s, data2);
				flags = readb(dev->mmio + Group_1_Flags);
			}
			async->events |= COMEDI_CB_BLOCK;
		}

		if (flags & CountExpired) {
			writeb(ClearExpired, dev->mmio + Group_1_Second_Clear);
			async->events |= COMEDI_CB_EOA;

			writeb(0x00, dev->mmio + OpMode);
			break;
		} else if (flags & Waited) {
			writeb(ClearWaited, dev->mmio + Group_1_First_Clear);
			async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
			break;
		} else if (flags & PrimaryTC) {
			writeb(ClearPrimaryTC,
			       dev->mmio + Group_1_First_Clear);
			async->events |= COMEDI_CB_EOA;
		} else if (flags & SecondaryTC) {
			writeb(ClearSecondaryTC,
			       dev->mmio + Group_1_First_Clear);
			async->events |= COMEDI_CB_EOA;
		}

		flags = readb(dev->mmio + Group_1_Flags);
		status = readb(dev->mmio + Interrupt_And_Window_Status);
	}

out:
	cfc_handle_events(dev, s);
#if 0
	if (!tag)
		writeb(0x03, dev->mmio + Master_DMA_And_Interrupt_Control);
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

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	writel(s->io_bits, dev->mmio + Port_Pin_Directions(0));

	return insn->n;
}

static int ni_pcidio_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		writel(s->state, dev->mmio + Port_IO(0));

	data[1] = readl(dev->mmio + Port_IO(0));

	return insn->n;
}

static int ni_pcidio_ns_to_timer(int *nanosec, unsigned int flags)
{
	int divider, base;

	base = TIMER_BASE;

	switch (flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_NEAREST:
	default:
		divider = (*nanosec + base / 2) / base;
		break;
	case CMDF_ROUND_DOWN:
		divider = (*nanosec) / base;
		break;
	case CMDF_ROUND_UP:
		divider = (*nanosec + base - 1) / base;
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

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

#define MAX_SPEED	(TIMER_BASE)	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
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

	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		ni_pcidio_ns_to_timer(&arg, cmd->flags);
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
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

	writeb(devpriv->OpModeBits, dev->mmio + OpMode);
	s->async->inttrig = NULL;

	return 1;
}

static int ni_pcidio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct nidio96_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	/* XXX configure ports for input */
	writel(0x0000, dev->mmio + Port_Pin_Directions(0));

	if (1) {
		/* enable fifos A B C D */
		writeb(0x0f, dev->mmio + Data_Path);

		/* set transfer width a 32 bits */
		writeb(TransferWidth(0) | TransferLength(0),
		       dev->mmio + Transfer_Size_Control);
	} else {
		writeb(0x03, dev->mmio + Data_Path);
		writeb(TransferWidth(3) | TransferLength(0),
		       dev->mmio + Transfer_Size_Control);
	}

	/* protocol configuration */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		/* page 4-5, "input with internal REQs" */
		writeb(0, dev->mmio + OpMode);
		writeb(0x00, dev->mmio + ClockReg);
		writeb(1, dev->mmio + Sequence);
		writeb(0x04, dev->mmio + ReqReg);
		writeb(4, dev->mmio + BlockMode);
		writeb(3, dev->mmio + LinePolarities);
		writeb(0xc0, dev->mmio + AckSer);
		writel(ni_pcidio_ns_to_timer(&cmd->scan_begin_arg,
					     CMDF_ROUND_NEAREST),
		       dev->mmio + StartDelay);
		writeb(1, dev->mmio + ReqDelay);
		writeb(1, dev->mmio + ReqNotDelay);
		writeb(1, dev->mmio + AckDelay);
		writeb(0x0b, dev->mmio + AckNotDelay);
		writeb(0x01, dev->mmio + Data1Delay);
		/* manual, page 4-5: ClockSpeed comment is incorrectly listed
		 * on DAQOptions */
		writew(0, dev->mmio + ClockSpeed);
		writeb(0, dev->mmio + DAQOptions);
	} else {
		/* TRIG_EXT */
		/* page 4-5, "input with external REQs" */
		writeb(0, dev->mmio + OpMode);
		writeb(0x00, dev->mmio + ClockReg);
		writeb(0, dev->mmio + Sequence);
		writeb(0x00, dev->mmio + ReqReg);
		writeb(4, dev->mmio + BlockMode);
		if (!(cmd->scan_begin_arg & CR_INVERT))	/* Leading Edge */
			writeb(0, dev->mmio + LinePolarities);
		else					/* Trailing Edge */
			writeb(2, dev->mmio + LinePolarities);
		writeb(0x00, dev->mmio + AckSer);
		writel(1, dev->mmio + StartDelay);
		writeb(1, dev->mmio + ReqDelay);
		writeb(1, dev->mmio + ReqNotDelay);
		writeb(1, dev->mmio + AckDelay);
		writeb(0x0C, dev->mmio + AckNotDelay);
		writeb(0x10, dev->mmio + Data1Delay);
		writew(0, dev->mmio + ClockSpeed);
		writeb(0x60, dev->mmio + DAQOptions);
	}

	if (cmd->stop_src == TRIG_COUNT) {
		writel(cmd->stop_arg,
		       dev->mmio + Transfer_Count);
	} else {
		/* XXX */
	}

#ifdef USE_DMA
	writeb(ClearPrimaryTC | ClearSecondaryTC,
	       dev->mmio + Group_1_First_Clear);

	{
		int retval = setup_mite_dma(dev, s);

		if (retval)
			return retval;
	}
#else
	writeb(0x00, dev->mmio + DMA_Line_Control_Group1);
#endif
	writeb(0x00, dev->mmio + DMA_Line_Control_Group2);

	/* clear and enable interrupts */
	writeb(0xff, dev->mmio + Group_1_First_Clear);
	/* writeb(ClearExpired, dev->mmio+Group_1_Second_Clear); */

	writeb(IntEn, dev->mmio + Interrupt_Control);
	writeb(0x03, dev->mmio + Master_DMA_And_Interrupt_Control);

	if (cmd->stop_src == TRIG_NONE) {
		devpriv->OpModeBits = DataLatching(0) | RunMode(7);
	} else {		/* TRIG_TIMER */
		devpriv->OpModeBits = Numbered | RunMode(7);
	}
	if (cmd->start_src == TRIG_NOW) {
		/* start */
		writeb(devpriv->OpModeBits, dev->mmio + OpMode);
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
	writeb(0x00, dev->mmio + Master_DMA_And_Interrupt_Control);
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
	writel(0, dev->mmio + Port_IO(0));
	writel(0, dev->mmio + Port_Pin_Directions(0));
	writel(0, dev->mmio + Port_Pin_Mask(0));

	/* disable interrupts on board */
	writeb(0, dev->mmio + Master_DMA_And_Interrupt_Control);
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

	devpriv->mite = mite_alloc(pcidev);
	if (!devpriv->mite)
		return -ENOMEM;

	ret = mite_setup(dev, devpriv->mite);
	if (ret < 0)
		return ret;

	devpriv->di_mite_ring = mite_alloc_ring(devpriv->mite);
	if (devpriv->di_mite_ring == NULL)
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
		 readb(dev->mmio + Chip_Version));

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

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
