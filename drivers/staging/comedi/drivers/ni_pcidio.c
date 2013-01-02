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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

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
/* #define DEBUG 1 */
/* #define DEBUG_FLAGS */

#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include "../comedidev.h"

#include "comedi_fc.h"
#include "mite.h"

#undef DPRINTK
#ifdef DEBUG
#define DPRINTK(format, args...) pr_debug(format, ## args)
#else
#define DPRINTK(format, args...) do { } while (0)
#endif

#define PCI_DIO_SIZE 4096
#define PCI_MITE_SIZE 4096

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

static int ni_pcidio_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s);

struct nidio_board {
	int dev_id;
	const char *name;
	unsigned int uses_firmware:1;
};

static const struct nidio_board nidio_boards[] = {
	{
		.dev_id		= 0x1150,
		.name		= "pci-dio-32hs",
	}, {
		.dev_id		= 0x1320,
		.name		= "pxi-6533",
	}, {
		.dev_id		= 0x12b0,
		.name		= "pci-6534",
		.uses_firmware	= 1,
	},
};

#define n_nidio_boards ARRAY_SIZE(nidio_boards)
#define this_board ((const struct nidio_board *)dev->board_ptr)

struct nidio96_private {
	struct mite_struct *mite;
	int boardtype;
	int dio;
	unsigned short OpModeBits;
	struct mite_channel *di_mite_chan;
	struct mite_dma_descriptor_ring *di_mite_ring;
	spinlock_t mite_channel_lock;
};

static int ni_pcidio_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_cmd *cmd);
static int ni_pcidio_cmd(struct comedi_device *dev, struct comedi_subdevice *s);
static int ni_pcidio_inttrig(struct comedi_device *dev,
			     struct comedi_subdevice *s, unsigned int trignum);
static int ni_pcidio_ns_to_timer(int *nanosec, int round_mode);
static int setup_mite_dma(struct comedi_device *dev,
			  struct comedi_subdevice *s);

#ifdef DEBUG_FLAGS
static void ni_pcidio_print_flags(unsigned int flags);
static void ni_pcidio_print_status(unsigned int status);
#else
#define ni_pcidio_print_flags(x)
#define ni_pcidio_print_status(x)
#endif

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
		comedi_error(dev, "failed to reserve mite dma channel.");
		return -EBUSY;
	}
	devpriv->di_mite_chan->dir = COMEDI_INPUT;
	writeb(primary_DMAChannel_bits(devpriv->di_mite_chan->channel) |
	       secondary_DMAChannel_bits(devpriv->di_mite_chan->channel),
	       devpriv->mite->daq_io_addr + DMA_Line_Control_Group1);
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
		       devpriv->mite->daq_io_addr + DMA_Line_Control_Group1);
		mmiowb();
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}

static void ni_pcidio_event(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	if (s->
	    async->events & (COMEDI_CB_EOA | COMEDI_CB_ERROR |
			     COMEDI_CB_OVERFLOW)) {
		ni_pcidio_cancel(dev, s);
	}
	comedi_event(dev, s);
}

static int ni_pcidio_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct nidio96_private *devpriv = dev->private;
	unsigned long irq_flags;
	int count;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	spin_lock(&devpriv->mite_channel_lock);
	if (devpriv->di_mite_chan)
		mite_sync_input_dma(devpriv->di_mite_chan, s->async);
	spin_unlock(&devpriv->mite_channel_lock);
	count = s->async->buf_write_count - s->async->buf_read_count;
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
	return count;
}

static irqreturn_t nidio_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct nidio96_private *devpriv = dev->private;
	struct comedi_subdevice *s = &dev->subdevices[0];
	struct comedi_async *async = s->async;
	struct mite_struct *mite = devpriv->mite;

	/* int i, j; */
	long int AuxData = 0;
	short data1 = 0;
	short data2 = 0;
	int flags;
	int status;
	int work = 0;
	unsigned int m_status = 0;

	/* interrupcions parasites */
	if (dev->attached == 0) {
		/* assume it's from another card */
		return IRQ_NONE;
	}

	/* Lock to avoid race with comedi_poll */
	spin_lock(&dev->spinlock);

	status = readb(devpriv->mite->daq_io_addr +
		       Interrupt_And_Window_Status);
	flags = readb(devpriv->mite->daq_io_addr + Group_1_Flags);

	DPRINTK("ni_pcidio_interrupt: status=0x%02x,flags=0x%02x\n",
		status, flags);
	ni_pcidio_print_flags(flags);
	ni_pcidio_print_status(status);

	spin_lock(&devpriv->mite_channel_lock);
	if (devpriv->di_mite_chan)
		m_status = mite_get_status(devpriv->di_mite_chan);
#ifdef MITE_DEBUG
	mite_print_chsr(m_status);
#endif

	/* mite_dump_regs(mite); */
	if (m_status & CHSR_INT) {
		if (m_status & CHSR_LINKC) {
			writel(CHOR_CLRLC,
			       mite->mite_io_addr +
			       MITE_CHOR(devpriv->di_mite_chan->channel));
			mite_sync_input_dma(devpriv->di_mite_chan, s->async);
			/* XXX need to byteswap */
		}
		if (m_status & ~(CHSR_INT | CHSR_LINKC | CHSR_DONE | CHSR_DRDY |
				 CHSR_DRQ1 | CHSR_MRDY)) {
			DPRINTK("unknown mite interrupt, disabling IRQ\n");
			async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
			disable_irq(dev->irq);
		}
	}
	spin_unlock(&devpriv->mite_channel_lock);

	while (status & DataLeft) {
		work++;
		if (work > 20) {
			DPRINTK("too much work in interrupt\n");
			writeb(0x00,
			       devpriv->mite->daq_io_addr +
			       Master_DMA_And_Interrupt_Control);
			break;
		}

		flags &= IntEn;

		if (flags & TransferReady) {
			/* DPRINTK("TransferReady\n"); */
			while (flags & TransferReady) {
				work++;
				if (work > 100) {
					DPRINTK("too much work in interrupt\n");
					writeb(0x00,
					       devpriv->mite->daq_io_addr +
					       Master_DMA_And_Interrupt_Control
					      );
					goto out;
				}
				AuxData =
				    readl(devpriv->mite->daq_io_addr +
					  Group_1_FIFO);
				data1 = AuxData & 0xffff;
				data2 = (AuxData & 0xffff0000) >> 16;
				comedi_buf_put(async, data1);
				comedi_buf_put(async, data2);
				/* DPRINTK("read:%d, %d\n",data1,data2); */
				flags = readb(devpriv->mite->daq_io_addr +
					      Group_1_Flags);
			}
			/* DPRINTK("buf_int_count: %d\n",
				async->buf_int_count); */
			/* DPRINTK("1) IntEn=%d,flags=%d,status=%d\n",
				IntEn,flags,status); */
			/* ni_pcidio_print_flags(flags); */
			/* ni_pcidio_print_status(status); */
			async->events |= COMEDI_CB_BLOCK;
		}

		if (flags & CountExpired) {
			DPRINTK("CountExpired\n");
			writeb(ClearExpired,
			       devpriv->mite->daq_io_addr +
			       Group_1_Second_Clear);
			async->events |= COMEDI_CB_EOA;

			writeb(0x00, devpriv->mite->daq_io_addr + OpMode);
			break;
		} else if (flags & Waited) {
			DPRINTK("Waited\n");
			writeb(ClearWaited,
			       devpriv->mite->daq_io_addr +
			       Group_1_First_Clear);
			async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
			break;
		} else if (flags & PrimaryTC) {
			DPRINTK("PrimaryTC\n");
			writeb(ClearPrimaryTC,
			       devpriv->mite->daq_io_addr +
			       Group_1_First_Clear);
			async->events |= COMEDI_CB_EOA;
		} else if (flags & SecondaryTC) {
			DPRINTK("SecondaryTC\n");
			writeb(ClearSecondaryTC,
			       devpriv->mite->daq_io_addr +
			       Group_1_First_Clear);
			async->events |= COMEDI_CB_EOA;
		}
#if 0
		else {
			DPRINTK("ni_pcidio: unknown interrupt\n");
			async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
			writeb(0x00,
			       devpriv->mite->daq_io_addr +
			       Master_DMA_And_Interrupt_Control);
		}
#endif
		flags = readb(devpriv->mite->daq_io_addr + Group_1_Flags);
		status = readb(devpriv->mite->daq_io_addr +
			       Interrupt_And_Window_Status);
		/* DPRINTK("loop end: IntEn=0x%02x,flags=0x%02x,"
			"status=0x%02x\n", IntEn, flags, status); */
		/* ni_pcidio_print_flags(flags); */
		/* ni_pcidio_print_status(status); */
	}

out:
	ni_pcidio_event(dev, s);
#if 0
	if (!tag) {
		writeb(0x03,
		       devpriv->mite->daq_io_addr +
		       Master_DMA_And_Interrupt_Control);
	}
#endif

	spin_unlock(&dev->spinlock);
	return IRQ_HANDLED;
}

#ifdef DEBUG_FLAGS
static const char *bit_set_string(unsigned int bits, unsigned int bit,
				  const char *const strings[])
{
	return (bits & (1U << bit)) ? strings[bit] : "";
}

static const char *const flags_strings[] = {
	" TransferReady", " CountExpired", " 2", " 3",
	" 4", " Waited", " PrimaryTC", " SecondaryTC",
};


static void ni_pcidio_print_flags(unsigned int flags)
{
	pr_debug("group_1_flags:%s%s%s%s%s%s%s%s\n",
		 bit_set_string(flags, 7, flags_strings),
		 bit_set_string(flags, 6, flags_strings),
		 bit_set_string(flags, 5, flags_strings),
		 bit_set_string(flags, 4, flags_strings),
		 bit_set_string(flags, 3, flags_strings),
		 bit_set_string(flags, 2, flags_strings),
		 bit_set_string(flags, 1, flags_strings),
		 bit_set_string(flags, 0, flags_strings));
}

static const char *const status_strings[] = {
	" DataLeft1", " Reserved1", " Req1", " StopTrig1",
	" DataLeft2", " Reserved2", " Req2", " StopTrig2",
};

static void ni_pcidio_print_status(unsigned int flags)
{
	pr_debug("group_status:%s%s%s%s%s%s%s%s\n",
		 bit_set_string(flags, 7, status_strings),
		 bit_set_string(flags, 6, status_strings),
		 bit_set_string(flags, 5, status_strings),
		 bit_set_string(flags, 4, status_strings),
		 bit_set_string(flags, 3, status_strings),
		 bit_set_string(flags, 2, status_strings),
		 bit_set_string(flags, 1, status_strings),
		 bit_set_string(flags, 0, status_strings));
}
#endif

#ifdef unused
static void debug_int(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;
	int a, b;
	static int n_int;
	struct timeval tv;

	do_gettimeofday(&tv);
	a = readb(devpriv->mite->daq_io_addr + Group_Status);
	b = readb(devpriv->mite->daq_io_addr + Group_1_Flags);

	if (n_int < 10) {
		DPRINTK("status 0x%02x flags 0x%02x time %06d\n", a, b,
			(int)tv.tv_usec);
	}

	while (b & 1) {
		writew(0xff, devpriv->mite->daq_io_addr + Group_1_FIFO);
		b = readb(devpriv->mite->daq_io_addr + Group_1_Flags);
	}

	b = readb(devpriv->mite->daq_io_addr + Group_1_Flags);

	if (n_int < 10) {
		DPRINTK("new status 0x%02x\n", b);
		n_int++;
	}
}
#endif

static int ni_pcidio_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	struct nidio96_private *devpriv = dev->private;

	if (insn->n != 1)
		return -EINVAL;
	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= 1 << CR_CHAN(insn->chanspec);
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~(1 << CR_CHAN(insn->chanspec));
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->
		     io_bits & (1 << CR_CHAN(insn->chanspec))) ? COMEDI_OUTPUT :
		    COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
	}
	writel(s->io_bits, devpriv->mite->daq_io_addr + Port_Pin_Directions(0));

	return 1;
}

static int ni_pcidio_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct nidio96_private *devpriv = dev->private;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
		writel(s->state, devpriv->mite->daq_io_addr + Port_IO(0));
	}
	data[1] = readl(devpriv->mite->daq_io_addr + Port_IO(0));

	return insn->n;
}

static int ni_pcidio_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

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

	if (cmd->stop_src == TRIG_COUNT) {
		/* no limit */
	} else {	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		ni_pcidio_ns_to_timer(&cmd->scan_begin_arg,
				      cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}

	if (err)
		return 4;

	return 0;
}

static int ni_pcidio_ns_to_timer(int *nanosec, int round_mode)
{
	int divider, base;

	base = TIMER_BASE;

	switch (round_mode) {
	case TRIG_ROUND_NEAREST:
	default:
		divider = (*nanosec + base / 2) / base;
		break;
	case TRIG_ROUND_DOWN:
		divider = (*nanosec) / base;
		break;
	case TRIG_ROUND_UP:
		divider = (*nanosec + base - 1) / base;
		break;
	}

	*nanosec = base * divider;
	return divider;
}

static int ni_pcidio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct nidio96_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	/* XXX configure ports for input */
	writel(0x0000, devpriv->mite->daq_io_addr + Port_Pin_Directions(0));

	if (1) {
		/* enable fifos A B C D */
		writeb(0x0f, devpriv->mite->daq_io_addr + Data_Path);

		/* set transfer width a 32 bits */
		writeb(TransferWidth(0) | TransferLength(0),
		       devpriv->mite->daq_io_addr + Transfer_Size_Control);
	} else {
		writeb(0x03, devpriv->mite->daq_io_addr + Data_Path);
		writeb(TransferWidth(3) | TransferLength(0),
		       devpriv->mite->daq_io_addr + Transfer_Size_Control);
	}

	/* protocol configuration */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		/* page 4-5, "input with internal REQs" */
		writeb(0, devpriv->mite->daq_io_addr + OpMode);
		writeb(0x00, devpriv->mite->daq_io_addr + ClockReg);
		writeb(1, devpriv->mite->daq_io_addr + Sequence);
		writeb(0x04, devpriv->mite->daq_io_addr + ReqReg);
		writeb(4, devpriv->mite->daq_io_addr + BlockMode);
		writeb(3, devpriv->mite->daq_io_addr + LinePolarities);
		writeb(0xc0, devpriv->mite->daq_io_addr + AckSer);
		writel(ni_pcidio_ns_to_timer(&cmd->scan_begin_arg,
					     TRIG_ROUND_NEAREST),
		       devpriv->mite->daq_io_addr + StartDelay);
		writeb(1, devpriv->mite->daq_io_addr + ReqDelay);
		writeb(1, devpriv->mite->daq_io_addr + ReqNotDelay);
		writeb(1, devpriv->mite->daq_io_addr + AckDelay);
		writeb(0x0b, devpriv->mite->daq_io_addr + AckNotDelay);
		writeb(0x01, devpriv->mite->daq_io_addr + Data1Delay);
		/* manual, page 4-5: ClockSpeed comment is incorrectly listed
		 * on DAQOptions */
		writew(0, devpriv->mite->daq_io_addr + ClockSpeed);
		writeb(0, devpriv->mite->daq_io_addr + DAQOptions);
	} else {
		/* TRIG_EXT */
		/* page 4-5, "input with external REQs" */
		writeb(0, devpriv->mite->daq_io_addr + OpMode);
		writeb(0x00, devpriv->mite->daq_io_addr + ClockReg);
		writeb(0, devpriv->mite->daq_io_addr + Sequence);
		writeb(0x00, devpriv->mite->daq_io_addr + ReqReg);
		writeb(4, devpriv->mite->daq_io_addr + BlockMode);
		if (!(cmd->scan_begin_arg & CR_INVERT)) {
			/* Leading Edge pulse mode */
			writeb(0, devpriv->mite->daq_io_addr + LinePolarities);
		} else {
			/* Trailing Edge pulse mode */
			writeb(2, devpriv->mite->daq_io_addr + LinePolarities);
		}
		writeb(0x00, devpriv->mite->daq_io_addr + AckSer);
		writel(1, devpriv->mite->daq_io_addr + StartDelay);
		writeb(1, devpriv->mite->daq_io_addr + ReqDelay);
		writeb(1, devpriv->mite->daq_io_addr + ReqNotDelay);
		writeb(1, devpriv->mite->daq_io_addr + AckDelay);
		writeb(0x0C, devpriv->mite->daq_io_addr + AckNotDelay);
		writeb(0x10, devpriv->mite->daq_io_addr + Data1Delay);
		writew(0, devpriv->mite->daq_io_addr + ClockSpeed);
		writeb(0x60, devpriv->mite->daq_io_addr + DAQOptions);
	}

	if (cmd->stop_src == TRIG_COUNT) {
		writel(cmd->stop_arg,
		       devpriv->mite->daq_io_addr + Transfer_Count);
	} else {
		/* XXX */
	}

#ifdef USE_DMA
	writeb(ClearPrimaryTC | ClearSecondaryTC,
	       devpriv->mite->daq_io_addr + Group_1_First_Clear);

	{
		int retval = setup_mite_dma(dev, s);
		if (retval)
			return retval;
	}
#else
	writeb(0x00, devpriv->mite->daq_io_addr + DMA_Line_Control_Group1);
#endif
	writeb(0x00, devpriv->mite->daq_io_addr + DMA_Line_Control_Group2);

	/* clear and enable interrupts */
	writeb(0xff, devpriv->mite->daq_io_addr + Group_1_First_Clear);
	/* writeb(ClearExpired,
	       devpriv->mite->daq_io_addr+Group_1_Second_Clear); */

	writeb(IntEn, devpriv->mite->daq_io_addr + Interrupt_Control);
	writeb(0x03,
	       devpriv->mite->daq_io_addr + Master_DMA_And_Interrupt_Control);

	if (cmd->stop_src == TRIG_NONE) {
		devpriv->OpModeBits = DataLatching(0) | RunMode(7);
	} else {		/* TRIG_TIMER */
		devpriv->OpModeBits = Numbered | RunMode(7);
	}
	if (cmd->start_src == TRIG_NOW) {
		/* start */
		writeb(devpriv->OpModeBits,
		       devpriv->mite->daq_io_addr + OpMode);
		s->async->inttrig = NULL;
	} else {
		/* TRIG_INT */
		s->async->inttrig = ni_pcidio_inttrig;
	}

	DPRINTK("ni_pcidio: command started\n");
	return 0;
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
	comedi_buf_write_alloc(s->async, s->async->prealloc_bufsz);

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->di_mite_chan) {
		mite_prep_dma(devpriv->di_mite_chan, 32, 32);
		mite_dma_arm(devpriv->di_mite_chan);
	} else
		retval = -EIO;
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	return retval;
}

static int ni_pcidio_inttrig(struct comedi_device *dev,
			     struct comedi_subdevice *s, unsigned int trignum)
{
	struct nidio96_private *devpriv = dev->private;

	if (trignum != 0)
		return -EINVAL;

	writeb(devpriv->OpModeBits, devpriv->mite->daq_io_addr + OpMode);
	s->async->inttrig = NULL;

	return 1;
}

static int ni_pcidio_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct nidio96_private *devpriv = dev->private;

	writeb(0x00,
	       devpriv->mite->daq_io_addr + Master_DMA_And_Interrupt_Control);
	ni_pcidio_release_di_mite_channel(dev);

	return 0;
}

static int ni_pcidio_change(struct comedi_device *dev,
			    struct comedi_subdevice *s, unsigned long new_size)
{
	struct nidio96_private *devpriv = dev->private;
	int ret;

	ret = mite_buf_change(devpriv->di_mite_ring, s->async);
	if (ret < 0)
		return ret;

	memset(s->async->prealloc_buf, 0xaa, s->async->prealloc_bufsz);

	return 0;
}

static int pci_6534_load_fpga(struct comedi_device *dev, int fpga_index,
			      const u8 *data, size_t data_len)
{
	struct nidio96_private *devpriv = dev->private;
	static const int timeout = 1000;
	int i;
	size_t j;

	writew(0x80 | fpga_index,
	       devpriv->mite->daq_io_addr + Firmware_Control_Register);
	writew(0xc0 | fpga_index,
	       devpriv->mite->daq_io_addr + Firmware_Control_Register);
	for (i = 0;
	     (readw(devpriv->mite->daq_io_addr +
		    Firmware_Status_Register) & 0x2) == 0 && i < timeout; ++i) {
		udelay(1);
	}
	if (i == timeout) {
		dev_warn(dev->class_dev,
			 "ni_pcidio: failed to load fpga %i, waiting for status 0x2\n",
			 fpga_index);
		return -EIO;
	}
	writew(0x80 | fpga_index,
	       devpriv->mite->daq_io_addr + Firmware_Control_Register);
	for (i = 0;
	     readw(devpriv->mite->daq_io_addr + Firmware_Status_Register) !=
	     0x3 && i < timeout; ++i) {
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
		writew(value,
		       devpriv->mite->daq_io_addr + Firmware_Data_Register);
		for (i = 0;
		     (readw(devpriv->mite->daq_io_addr +
			    Firmware_Status_Register) & 0x2) == 0
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
	writew(0x0, devpriv->mite->daq_io_addr + Firmware_Control_Register);
	return 0;
}

static int pci_6534_reset_fpga(struct comedi_device *dev, int fpga_index)
{
	return pci_6534_load_fpga(dev, fpga_index, NULL, 0);
}

static int pci_6534_reset_fpgas(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;
	int ret;
	int i;

	writew(0x0, devpriv->mite->daq_io_addr + Firmware_Control_Register);
	for (i = 0; i < 3; ++i) {
		ret = pci_6534_reset_fpga(dev, i);
		if (ret < 0)
			break;
	}
	writew(0x0, devpriv->mite->daq_io_addr + Firmware_Mask_Register);
	return ret;
}

static void pci_6534_init_main_fpga(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;

	writel(0, devpriv->mite->daq_io_addr + FPGA_Control1_Register);
	writel(0, devpriv->mite->daq_io_addr + FPGA_Control2_Register);
	writel(0, devpriv->mite->daq_io_addr + FPGA_SCALS_Counter_Register);
	writel(0, devpriv->mite->daq_io_addr + FPGA_SCAMS_Counter_Register);
	writel(0, devpriv->mite->daq_io_addr + FPGA_SCBLS_Counter_Register);
	writel(0, devpriv->mite->daq_io_addr + FPGA_SCBMS_Counter_Register);
}

static int pci_6534_upload_firmware(struct comedi_device *dev)
{
	struct nidio96_private *devpriv = dev->private;
	int ret;
	const struct firmware *fw;
	static const char *const fw_file[3] = {
		FW_PCI_6534_SCARAB_DI,	/* loaded into scarab A for DI */
		FW_PCI_6534_SCARAB_DO,	/* loaded into scarab B for DO */
		FW_PCI_6534_MAIN,	/* loaded into main FPGA */
	};
	int n;

	ret = pci_6534_reset_fpgas(dev);
	if (ret < 0)
		return ret;
	/* load main FPGA first, then the two scarabs */
	for (n = 2; n >= 0; n--) {
		ret = request_firmware(&fw, fw_file[n],
				       &devpriv->mite->pcidev->dev);
		if (ret == 0) {
			ret = pci_6534_load_fpga(dev, n, fw->data, fw->size);
			if (ret == 0 && n == 2)
				pci_6534_init_main_fpga(dev);
			release_firmware(fw);
		}
		if (ret < 0)
			break;
	}
	return ret;
}

static const struct nidio_board *
nidio_find_boardinfo(struct pci_dev *pcidev)
{
	unsigned int dev_id = pcidev->device;
	unsigned int n;

	for (n = 0; n < ARRAY_SIZE(nidio_boards); n++) {
		const struct nidio_board *board = &nidio_boards[n];
		if (board->dev_id == dev_id)
			return board;
	}
	return NULL;
}

static int nidio_auto_attach(struct comedi_device *dev,
				       unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct nidio96_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	unsigned int irq;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	spin_lock_init(&devpriv->mite_channel_lock);

	dev->board_ptr = nidio_find_boardinfo(pcidev);
	if (!dev->board_ptr)
		return -ENODEV;
	devpriv->mite = mite_alloc(pcidev);
	if (!devpriv->mite)
		return -ENOMEM;

	ret = mite_setup(devpriv->mite);
	if (ret < 0) {
		dev_warn(dev->class_dev, "error setting up mite\n");
		return ret;
	}

	devpriv->di_mite_ring = mite_alloc_ring(devpriv->mite);
	if (devpriv->di_mite_ring == NULL)
		return -ENOMEM;

	dev->board_name = this_board->name;
	irq = mite_irq(devpriv->mite);
	if (this_board->uses_firmware) {
		ret = pci_6534_upload_firmware(dev);
		if (ret < 0)
			return ret;
	}

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	dev_info(dev->class_dev, "%s rev=%d\n", dev->board_name,
		 readb(devpriv->mite->daq_io_addr + Chip_Version));

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

	writel(0, devpriv->mite->daq_io_addr + Port_IO(0));
	writel(0, devpriv->mite->daq_io_addr + Port_Pin_Directions(0));
	writel(0, devpriv->mite->daq_io_addr + Port_Pin_Mask(0));

	/* disable interrupts on board */
	writeb(0x00,
		devpriv->mite->daq_io_addr +
		Master_DMA_And_Interrupt_Control);

	ret = request_irq(irq, nidio_interrupt, IRQF_SHARED,
				"ni_pcidio", dev);
	if (ret < 0)
		dev_warn(dev->class_dev, "irq not available\n");

	dev->irq = irq;

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
		if (devpriv->mite) {
			mite_unsetup(devpriv->mite);
			mite_free(devpriv->mite);
		}
	}
}

static struct comedi_driver ni_pcidio_driver = {
	.driver_name	= "ni_pcidio",
	.module		= THIS_MODULE,
	.auto_attach	= nidio_auto_attach,
	.detach		= nidio_detach,
};

static int ni_pcidio_pci_probe(struct pci_dev *dev,
					 const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &ni_pcidio_driver);
}

static void ni_pcidio_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(ni_pcidio_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, 0x1150) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, 0x1320) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, 0x12b0) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni_pcidio_pci_table);

static struct pci_driver ni_pcidio_pci_driver = {
	.name		= "ni_pcidio",
	.id_table	= ni_pcidio_pci_table,
	.probe		= ni_pcidio_pci_probe,
	.remove		= ni_pcidio_pci_remove,
};
module_comedi_pci_driver(ni_pcidio_driver, ni_pcidio_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
