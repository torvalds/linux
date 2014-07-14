/*
    comedi/drivers/ni_mio_common.c
    Hardware driver for DAQ-STC based boards

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2001 David A. Schleef <ds@schleef.org>
    Copyright (C) 2002-2006 Frank Mori Hess <fmhess@users.sourceforge.net>

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
	This file is meant to be included by another file, e.g.,
	ni_atmio.c or ni_pcimio.c.

	Interrupt support originally added by Truxton Fulton
	<trux@truxton.com>

	References (from ftp://ftp.natinst.com/support/manuals):

	   340747b.pdf  AT-MIO E series Register Level Programmer Manual
	   341079b.pdf  PCI E Series RLPM
	   340934b.pdf  DAQ-STC reference manual
	67xx and 611x registers (from ftp://ftp.ni.com/support/daq/mhddk/documentation/)
	release_ni611x.pdf
	release_ni67xx.pdf
	Other possibly relevant info:

	   320517c.pdf  User manual (obsolete)
	   320517f.pdf  User manual (new)
	   320889a.pdf  delete
	   320906c.pdf  maximum signal ratings
	   321066a.pdf  about 16x
	   321791a.pdf  discontinuation of at-mio-16e-10 rev. c
	   321808a.pdf  about at-mio-16e-10 rev P
	   321837a.pdf  discontinuation of at-mio-16de-10 rev d
	   321838a.pdf  about at-mio-16de-10 rev N

	ISSUES:

	 - the interrupt routine needs to be cleaned up

	2006-02-07: S-Series PCI-6143: Support has been added but is not
		fully tested as yet. Terry Barnaby, BEAM Ltd.
*/

#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include "8255.h"
#include "mite.h"
#include "comedi_fc.h"

/* A timeout count */
#define NI_TIMEOUT 1000
static const unsigned old_RTSI_clock_channel = 7;

/* Note: this table must match the ai_gain_* definitions */
static const short ni_gainlkup[][16] = {
	[ai_gain_16] = {0, 1, 2, 3, 4, 5, 6, 7,
			0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107},
	[ai_gain_8] = {1, 2, 4, 7, 0x101, 0x102, 0x104, 0x107},
	[ai_gain_14] = {1, 2, 3, 4, 5, 6, 7,
			0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107},
	[ai_gain_4] = {0, 1, 4, 7},
	[ai_gain_611x] = {0x00a, 0x00b, 0x001, 0x002,
			  0x003, 0x004, 0x005, 0x006},
	[ai_gain_622x] = {0, 1, 4, 5},
	[ai_gain_628x] = {1, 2, 3, 4, 5, 6, 7},
	[ai_gain_6143] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

static const struct comedi_lrange range_ni_E_ai = {
	16, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.25),
		BIP_RANGE(0.1),
		BIP_RANGE(0.05),
		UNI_RANGE(20),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5),
		UNI_RANGE(0.2),
		UNI_RANGE(0.1)
	}
};

static const struct comedi_lrange range_ni_E_ai_limited = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(1),
		BIP_RANGE(0.1),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(1),
		UNI_RANGE(0.1)
	}
};

static const struct comedi_lrange range_ni_E_ai_limited14 = {
	14, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.2),
		BIP_RANGE(0.1),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5),
		UNI_RANGE(0.2),
		UNI_RANGE(0.1)
	}
};

static const struct comedi_lrange range_ni_E_ai_bipolar4 = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05)
	}
};

static const struct comedi_lrange range_ni_E_ai_611x = {
	8, {
		BIP_RANGE(50),
		BIP_RANGE(20),
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.2)
	}
};

static const struct comedi_lrange range_ni_M_ai_622x = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(1),
		BIP_RANGE(0.2)
	}
};

static const struct comedi_lrange range_ni_M_ai_628x = {
	7, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.2),
		BIP_RANGE(0.1)
	}
};

static const struct comedi_lrange range_ni_E_ao_ext = {
	4, {
		BIP_RANGE(10),
		UNI_RANGE(10),
		RANGE_ext(-1, 1),
		RANGE_ext(0, 1)
	}
};

static const struct comedi_lrange *const ni_range_lkup[] = {
	[ai_gain_16] = &range_ni_E_ai,
	[ai_gain_8] = &range_ni_E_ai_limited,
	[ai_gain_14] = &range_ni_E_ai_limited14,
	[ai_gain_4] = &range_ni_E_ai_bipolar4,
	[ai_gain_611x] = &range_ni_E_ai_611x,
	[ai_gain_622x] = &range_ni_M_ai_622x,
	[ai_gain_628x] = &range_ni_M_ai_628x,
	[ai_gain_6143] = &range_bipolar5
};

enum aimodes {
	AIMODE_NONE = 0,
	AIMODE_HALF_FULL = 1,
	AIMODE_SCAN = 2,
	AIMODE_SAMPLE = 3,
};

enum ni_common_subdevices {
	NI_AI_SUBDEV,
	NI_AO_SUBDEV,
	NI_DIO_SUBDEV,
	NI_8255_DIO_SUBDEV,
	NI_UNUSED_SUBDEV,
	NI_CALIBRATION_SUBDEV,
	NI_EEPROM_SUBDEV,
	NI_PFI_DIO_SUBDEV,
	NI_CS5529_CALIBRATION_SUBDEV,
	NI_SERIAL_SUBDEV,
	NI_RTSI_SUBDEV,
	NI_GPCT0_SUBDEV,
	NI_GPCT1_SUBDEV,
	NI_FREQ_OUT_SUBDEV,
	NI_NUM_SUBDEVICES
};
static inline unsigned NI_GPCT_SUBDEV(unsigned counter_index)
{
	switch (counter_index) {
	case 0:
		return NI_GPCT0_SUBDEV;
		break;
	case 1:
		return NI_GPCT1_SUBDEV;
		break;
	default:
		break;
	}
	BUG();
	return NI_GPCT0_SUBDEV;
}

enum timebase_nanoseconds {
	TIMEBASE_1_NS = 50,
	TIMEBASE_2_NS = 10000
};

#define SERIAL_DISABLED		0
#define SERIAL_600NS		600
#define SERIAL_1_2US		1200
#define SERIAL_10US			10000

static const int num_adc_stages_611x = 3;

static void ni_writel(struct comedi_device *dev, uint32_t data, int reg)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->mite)
		writel(data, devpriv->mite->daq_io_addr + reg);
	else
		outl(data, dev->iobase + reg);
}

static void ni_writew(struct comedi_device *dev, uint16_t data, int reg)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->mite)
		writew(data, devpriv->mite->daq_io_addr + reg);
	else
		outw(data, dev->iobase + reg);
}

static void ni_writeb(struct comedi_device *dev, uint8_t data, int reg)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->mite)
		writeb(data, devpriv->mite->daq_io_addr + reg);
	else
		outb(data, dev->iobase + reg);
}

static uint32_t ni_readl(struct comedi_device *dev, int reg)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->mite)
		return readl(devpriv->mite->daq_io_addr + reg);
	else
		return inl(dev->iobase + reg);
}

static uint16_t ni_readw(struct comedi_device *dev, int reg)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->mite)
		return readw(devpriv->mite->daq_io_addr + reg);
	else
		return inw(dev->iobase + reg);
}

static uint8_t ni_readb(struct comedi_device *dev, int reg)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->mite)
		return readb(devpriv->mite->daq_io_addr + reg);
	else
		return inb(dev->iobase + reg);
}

/*
 * We automatically take advantage of STC registers that can be
 * read/written directly in the I/O space of the board.
 *
 * The AT-MIO and DAQCard devices map the low 8 STC registers to
 * iobase+reg*2.
 *
 * Most PCIMIO devices also map the low 8 STC registers but the
 * 611x devices map the read registers to iobase+(addr-1)*2.
 * For now non-windowed STC access is disabled if a PCIMIO device
 * is detected (devpriv->mite has been initialized).
 *
 * The M series devices do not used windowed registers for the
 * STC registers. The functions below handle the mapping of the
 * windowed STC registers to the m series register offsets.
 */

static void m_series_stc_writel(struct comedi_device *dev,
				uint32_t data, int reg)
{
	unsigned offset;

	switch (reg) {
	case AI_SC_Load_A_Registers:
		offset = M_Offset_AI_SC_Load_A;
		break;
	case AI_SI_Load_A_Registers:
		offset = M_Offset_AI_SI_Load_A;
		break;
	case AO_BC_Load_A_Register:
		offset = M_Offset_AO_BC_Load_A;
		break;
	case AO_UC_Load_A_Register:
		offset = M_Offset_AO_UC_Load_A;
		break;
	case AO_UI_Load_A_Register:
		offset = M_Offset_AO_UI_Load_A;
		break;
	case G_Load_A_Register(0):
		offset = M_Offset_G0_Load_A;
		break;
	case G_Load_A_Register(1):
		offset = M_Offset_G1_Load_A;
		break;
	case G_Load_B_Register(0):
		offset = M_Offset_G0_Load_B;
		break;
	case G_Load_B_Register(1):
		offset = M_Offset_G1_Load_B;
		break;
	default:
		dev_warn(dev->class_dev,
			 "%s: bug! unhandled register=0x%x in switch\n",
			 __func__, reg);
		return;
	}
	ni_writel(dev, data, offset);
}

static void m_series_stc_writew(struct comedi_device *dev,
				uint16_t data, int reg)
{
	unsigned offset;

	switch (reg) {
	case ADC_FIFO_Clear:
		offset = M_Offset_AI_FIFO_Clear;
		break;
	case AI_Command_1_Register:
		offset = M_Offset_AI_Command_1;
		break;
	case AI_Command_2_Register:
		offset = M_Offset_AI_Command_2;
		break;
	case AI_Mode_1_Register:
		offset = M_Offset_AI_Mode_1;
		break;
	case AI_Mode_2_Register:
		offset = M_Offset_AI_Mode_2;
		break;
	case AI_Mode_3_Register:
		offset = M_Offset_AI_Mode_3;
		break;
	case AI_Output_Control_Register:
		offset = M_Offset_AI_Output_Control;
		break;
	case AI_Personal_Register:
		offset = M_Offset_AI_Personal;
		break;
	case AI_SI2_Load_A_Register:
		/* this is a 32 bit register on m series boards */
		ni_writel(dev, data, M_Offset_AI_SI2_Load_A);
		return;
	case AI_SI2_Load_B_Register:
		/* this is a 32 bit register on m series boards */
		ni_writel(dev, data, M_Offset_AI_SI2_Load_B);
		return;
	case AI_START_STOP_Select_Register:
		offset = M_Offset_AI_START_STOP_Select;
		break;
	case AI_Trigger_Select_Register:
		offset = M_Offset_AI_Trigger_Select;
		break;
	case Analog_Trigger_Etc_Register:
		offset = M_Offset_Analog_Trigger_Etc;
		break;
	case AO_Command_1_Register:
		offset = M_Offset_AO_Command_1;
		break;
	case AO_Command_2_Register:
		offset = M_Offset_AO_Command_2;
		break;
	case AO_Mode_1_Register:
		offset = M_Offset_AO_Mode_1;
		break;
	case AO_Mode_2_Register:
		offset = M_Offset_AO_Mode_2;
		break;
	case AO_Mode_3_Register:
		offset = M_Offset_AO_Mode_3;
		break;
	case AO_Output_Control_Register:
		offset = M_Offset_AO_Output_Control;
		break;
	case AO_Personal_Register:
		offset = M_Offset_AO_Personal;
		break;
	case AO_Start_Select_Register:
		offset = M_Offset_AO_Start_Select;
		break;
	case AO_Trigger_Select_Register:
		offset = M_Offset_AO_Trigger_Select;
		break;
	case Clock_and_FOUT_Register:
		offset = M_Offset_Clock_and_FOUT;
		break;
	case Configuration_Memory_Clear:
		offset = M_Offset_Configuration_Memory_Clear;
		break;
	case DAC_FIFO_Clear:
		offset = M_Offset_AO_FIFO_Clear;
		break;
	case DIO_Control_Register:
		dev_dbg(dev->class_dev,
			"%s: FIXME: register 0x%x does not map cleanly on to m-series boards\n",
			__func__, reg);
		return;
	case G_Autoincrement_Register(0):
		offset = M_Offset_G0_Autoincrement;
		break;
	case G_Autoincrement_Register(1):
		offset = M_Offset_G1_Autoincrement;
		break;
	case G_Command_Register(0):
		offset = M_Offset_G0_Command;
		break;
	case G_Command_Register(1):
		offset = M_Offset_G1_Command;
		break;
	case G_Input_Select_Register(0):
		offset = M_Offset_G0_Input_Select;
		break;
	case G_Input_Select_Register(1):
		offset = M_Offset_G1_Input_Select;
		break;
	case G_Mode_Register(0):
		offset = M_Offset_G0_Mode;
		break;
	case G_Mode_Register(1):
		offset = M_Offset_G1_Mode;
		break;
	case Interrupt_A_Ack_Register:
		offset = M_Offset_Interrupt_A_Ack;
		break;
	case Interrupt_A_Enable_Register:
		offset = M_Offset_Interrupt_A_Enable;
		break;
	case Interrupt_B_Ack_Register:
		offset = M_Offset_Interrupt_B_Ack;
		break;
	case Interrupt_B_Enable_Register:
		offset = M_Offset_Interrupt_B_Enable;
		break;
	case Interrupt_Control_Register:
		offset = M_Offset_Interrupt_Control;
		break;
	case IO_Bidirection_Pin_Register:
		offset = M_Offset_IO_Bidirection_Pin;
		break;
	case Joint_Reset_Register:
		offset = M_Offset_Joint_Reset;
		break;
	case RTSI_Trig_A_Output_Register:
		offset = M_Offset_RTSI_Trig_A_Output;
		break;
	case RTSI_Trig_B_Output_Register:
		offset = M_Offset_RTSI_Trig_B_Output;
		break;
	case RTSI_Trig_Direction_Register:
		offset = M_Offset_RTSI_Trig_Direction;
		break;
	/*
	 * FIXME: DIO_Output_Register (16 bit reg) is replaced by
	 * M_Offset_Static_Digital_Output (32 bit) and
	 * M_Offset_SCXI_Serial_Data_Out (8 bit)
	 */
	default:
		dev_warn(dev->class_dev,
			 "%s: bug! unhandled register=0x%x in switch\n",
			 __func__, reg);
		return;
	}
	ni_writew(dev, data, offset);
}

static uint32_t m_series_stc_readl(struct comedi_device *dev, int reg)
{
	unsigned offset;

	switch (reg) {
	case G_HW_Save_Register(0):
		offset = M_Offset_G0_HW_Save;
		break;
	case G_HW_Save_Register(1):
		offset = M_Offset_G1_HW_Save;
		break;
	case G_Save_Register(0):
		offset = M_Offset_G0_Save;
		break;
	case G_Save_Register(1):
		offset = M_Offset_G1_Save;
		break;
	default:
		dev_warn(dev->class_dev,
			 "%s: bug! unhandled register=0x%x in switch\n",
			 __func__, reg);
		return 0;
	}
	return ni_readl(dev, offset);
}

static uint16_t m_series_stc_readw(struct comedi_device *dev, int reg)
{
	unsigned offset;

	switch (reg) {
	case AI_Status_1_Register:
		offset = M_Offset_AI_Status_1;
		break;
	case AO_Status_1_Register:
		offset = M_Offset_AO_Status_1;
		break;
	case AO_Status_2_Register:
		offset = M_Offset_AO_Status_2;
		break;
	case DIO_Serial_Input_Register:
		return ni_readb(dev, M_Offset_SCXI_Serial_Data_In);
	case Joint_Status_1_Register:
		offset = M_Offset_Joint_Status_1;
		break;
	case Joint_Status_2_Register:
		offset = M_Offset_Joint_Status_2;
		break;
	case G_Status_Register:
		offset = M_Offset_G01_Status;
		break;
	default:
		dev_warn(dev->class_dev,
			 "%s: bug! unhandled register=0x%x in switch\n",
			 __func__, reg);
		return 0;
	}
	return ni_readw(dev, offset);
}

static void ni_stc_writew(struct comedi_device *dev, uint16_t data, int reg)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	if (devpriv->is_m_series) {
		m_series_stc_writew(dev, data, reg);
	} else {
		spin_lock_irqsave(&devpriv->window_lock, flags);
		if (!devpriv->mite && reg < 8) {
			ni_writew(dev, data, reg * 2);
		} else {
			ni_writew(dev, reg, Window_Address);
			ni_writew(dev, data, Window_Data);
		}
		spin_unlock_irqrestore(&devpriv->window_lock, flags);
	}
}

static void ni_stc_writel(struct comedi_device *dev, uint32_t data, int reg)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->is_m_series) {
		m_series_stc_writel(dev, data, reg);
	} else {
		ni_stc_writew(dev, data >> 16, reg);
		ni_stc_writew(dev, data & 0xffff, reg + 1);
	}
}

static uint16_t ni_stc_readw(struct comedi_device *dev, int reg)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;
	uint16_t val;

	if (devpriv->is_m_series) {
		val = m_series_stc_readw(dev, reg);
	} else {
		spin_lock_irqsave(&devpriv->window_lock, flags);
		if (!devpriv->mite && reg < 8) {
			val = ni_readw(dev, reg * 2);
		} else {
			ni_writew(dev, reg, Window_Address);
			val = ni_readw(dev, Window_Data);
		}
		spin_unlock_irqrestore(&devpriv->window_lock, flags);
	}
	return val;
}

static uint32_t ni_stc_readl(struct comedi_device *dev, int reg)
{
	struct ni_private *devpriv = dev->private;
	uint32_t val;

	if (devpriv->is_m_series) {
		val = m_series_stc_readl(dev, reg);
	} else {
		val = ni_stc_readw(dev, reg) << 16;
		val |= ni_stc_readw(dev, reg + 1);
	}
	return val;
}

static inline void ni_set_bitfield(struct comedi_device *dev, int reg,
				   unsigned bit_mask, unsigned bit_values)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->soft_reg_copy_lock, flags);
	switch (reg) {
	case Interrupt_A_Enable_Register:
		devpriv->int_a_enable_reg &= ~bit_mask;
		devpriv->int_a_enable_reg |= bit_values & bit_mask;
		ni_stc_writew(dev, devpriv->int_a_enable_reg,
			      Interrupt_A_Enable_Register);
		break;
	case Interrupt_B_Enable_Register:
		devpriv->int_b_enable_reg &= ~bit_mask;
		devpriv->int_b_enable_reg |= bit_values & bit_mask;
		ni_stc_writew(dev, devpriv->int_b_enable_reg,
			      Interrupt_B_Enable_Register);
		break;
	case IO_Bidirection_Pin_Register:
		devpriv->io_bidirection_pin_reg &= ~bit_mask;
		devpriv->io_bidirection_pin_reg |= bit_values & bit_mask;
		ni_stc_writew(dev, devpriv->io_bidirection_pin_reg,
			      IO_Bidirection_Pin_Register);
		break;
	case AI_AO_Select:
		devpriv->ai_ao_select_reg &= ~bit_mask;
		devpriv->ai_ao_select_reg |= bit_values & bit_mask;
		ni_writeb(dev, devpriv->ai_ao_select_reg, AI_AO_Select);
		break;
	case G0_G1_Select:
		devpriv->g0_g1_select_reg &= ~bit_mask;
		devpriv->g0_g1_select_reg |= bit_values & bit_mask;
		ni_writeb(dev, devpriv->g0_g1_select_reg, G0_G1_Select);
		break;
	default:
		printk("Warning %s() called with invalid register\n", __func__);
		printk("reg is %d\n", reg);
		break;
	}
	mmiowb();
	spin_unlock_irqrestore(&devpriv->soft_reg_copy_lock, flags);
}

#ifdef PCIDMA
/* DMA channel setup */

/* negative channel means no channel */
static inline void ni_set_ai_dma_channel(struct comedi_device *dev, int channel)
{
	unsigned bitfield;

	if (channel >= 0) {
		bitfield =
		    (ni_stc_dma_channel_select_bitfield(channel) <<
		     AI_DMA_Select_Shift) & AI_DMA_Select_Mask;
	} else {
		bitfield = 0;
	}
	ni_set_bitfield(dev, AI_AO_Select, AI_DMA_Select_Mask, bitfield);
}

/* negative channel means no channel */
static inline void ni_set_ao_dma_channel(struct comedi_device *dev, int channel)
{
	unsigned bitfield;

	if (channel >= 0) {
		bitfield =
		    (ni_stc_dma_channel_select_bitfield(channel) <<
		     AO_DMA_Select_Shift) & AO_DMA_Select_Mask;
	} else {
		bitfield = 0;
	}
	ni_set_bitfield(dev, AI_AO_Select, AO_DMA_Select_Mask, bitfield);
}

/* negative mite_channel means no channel */
static inline void ni_set_gpct_dma_channel(struct comedi_device *dev,
					   unsigned gpct_index,
					   int mite_channel)
{
	unsigned bitfield;

	if (mite_channel >= 0)
		bitfield = GPCT_DMA_Select_Bits(gpct_index, mite_channel);
	else
		bitfield = 0;
	ni_set_bitfield(dev, G0_G1_Select, GPCT_DMA_Select_Mask(gpct_index),
			bitfield);
}

/* negative mite_channel means no channel */
static inline void ni_set_cdo_dma_channel(struct comedi_device *dev,
					  int mite_channel)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->soft_reg_copy_lock, flags);
	devpriv->cdio_dma_select_reg &= ~CDO_DMA_Select_Mask;
	if (mite_channel >= 0) {
		/*XXX just guessing ni_stc_dma_channel_select_bitfield() returns the right bits,
		   under the assumption the cdio dma selection works just like ai/ao/gpct.
		   Definitely works for dma channels 0 and 1. */
		devpriv->cdio_dma_select_reg |=
		    (ni_stc_dma_channel_select_bitfield(mite_channel) <<
		     CDO_DMA_Select_Shift) & CDO_DMA_Select_Mask;
	}
	ni_writeb(dev, devpriv->cdio_dma_select_reg, M_Offset_CDIO_DMA_Select);
	mmiowb();
	spin_unlock_irqrestore(&devpriv->soft_reg_copy_lock, flags);
}

static int ni_request_ai_mite_channel(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	BUG_ON(devpriv->ai_mite_chan);
	devpriv->ai_mite_chan =
	    mite_request_channel(devpriv->mite, devpriv->ai_mite_ring);
	if (devpriv->ai_mite_chan == NULL) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		comedi_error(dev,
			     "failed to reserve mite dma channel for analog input.");
		return -EBUSY;
	}
	devpriv->ai_mite_chan->dir = COMEDI_INPUT;
	ni_set_ai_dma_channel(dev, devpriv->ai_mite_chan->channel);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static int ni_request_ao_mite_channel(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	BUG_ON(devpriv->ao_mite_chan);
	devpriv->ao_mite_chan =
	    mite_request_channel(devpriv->mite, devpriv->ao_mite_ring);
	if (devpriv->ao_mite_chan == NULL) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		comedi_error(dev,
			     "failed to reserve mite dma channel for analog outut.");
		return -EBUSY;
	}
	devpriv->ao_mite_chan->dir = COMEDI_OUTPUT;
	ni_set_ao_dma_channel(dev, devpriv->ao_mite_chan->channel);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static int ni_request_gpct_mite_channel(struct comedi_device *dev,
					unsigned gpct_index,
					enum comedi_io_direction direction)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;
	struct mite_channel *mite_chan;

	BUG_ON(gpct_index >= NUM_GPCT);
	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	BUG_ON(devpriv->counter_dev->counters[gpct_index].mite_chan);
	mite_chan =
	    mite_request_channel(devpriv->mite,
				 devpriv->gpct_mite_ring[gpct_index]);
	if (mite_chan == NULL) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		comedi_error(dev,
			     "failed to reserve mite dma channel for counter.");
		return -EBUSY;
	}
	mite_chan->dir = direction;
	ni_tio_set_mite_channel(&devpriv->counter_dev->counters[gpct_index],
				mite_chan);
	ni_set_gpct_dma_channel(dev, gpct_index, mite_chan->channel);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

#endif /*  PCIDMA */

static int ni_request_cdo_mite_channel(struct comedi_device *dev)
{
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	BUG_ON(devpriv->cdo_mite_chan);
	devpriv->cdo_mite_chan =
	    mite_request_channel(devpriv->mite, devpriv->cdo_mite_ring);
	if (devpriv->cdo_mite_chan == NULL) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		comedi_error(dev,
			     "failed to reserve mite dma channel for correlated digital outut.");
		return -EBUSY;
	}
	devpriv->cdo_mite_chan->dir = COMEDI_OUTPUT;
	ni_set_cdo_dma_channel(dev, devpriv->cdo_mite_chan->channel);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
#endif /*  PCIDMA */
	return 0;
}

static void ni_release_ai_mite_channel(struct comedi_device *dev)
{
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->ai_mite_chan) {
		ni_set_ai_dma_channel(dev, -1);
		mite_release_channel(devpriv->ai_mite_chan);
		devpriv->ai_mite_chan = NULL;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
#endif /*  PCIDMA */
}

static void ni_release_ao_mite_channel(struct comedi_device *dev)
{
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->ao_mite_chan) {
		ni_set_ao_dma_channel(dev, -1);
		mite_release_channel(devpriv->ao_mite_chan);
		devpriv->ao_mite_chan = NULL;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
#endif /*  PCIDMA */
}

#ifdef PCIDMA
static void ni_release_gpct_mite_channel(struct comedi_device *dev,
					 unsigned gpct_index)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	BUG_ON(gpct_index >= NUM_GPCT);
	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->counter_dev->counters[gpct_index].mite_chan) {
		struct mite_channel *mite_chan =
		    devpriv->counter_dev->counters[gpct_index].mite_chan;

		ni_set_gpct_dma_channel(dev, gpct_index, -1);
		ni_tio_set_mite_channel(&devpriv->
					counter_dev->counters[gpct_index],
					NULL);
		mite_release_channel(mite_chan);
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}
#endif /*  PCIDMA */

static void ni_release_cdo_mite_channel(struct comedi_device *dev)
{
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->cdo_mite_chan) {
		ni_set_cdo_dma_channel(dev, -1);
		mite_release_channel(devpriv->cdo_mite_chan);
		devpriv->cdo_mite_chan = NULL;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
#endif /*  PCIDMA */
}

#ifdef PCIDMA
static void ni_e_series_enable_second_irq(struct comedi_device *dev,
					  unsigned gpct_index, short enable)
{
	struct ni_private *devpriv = dev->private;
	uint16_t val = 0;
	int reg;

	if (devpriv->is_m_series || gpct_index > 1)
		return;

	/*
	 * e-series boards use the second irq signals to generate
	 * dma requests for their counters
	 */
	if (gpct_index == 0) {
		reg = Second_IRQ_A_Enable_Register;
		if (enable)
			val = G0_Gate_Second_Irq_Enable;
	} else {
		reg = Second_IRQ_B_Enable_Register;
		if (enable)
			val = G1_Gate_Second_Irq_Enable;
	}
	ni_stc_writew(dev, val, reg);
}
#endif /*  PCIDMA */

static void ni_clear_ai_fifo(struct comedi_device *dev)
{
	const struct ni_board_struct *board = comedi_board(dev);
	static const int timeout = 10000;
	int i;

	if (board->reg_type == ni_reg_6143) {
		/*  Flush the 6143 data FIFO */
		ni_writel(dev, 0x10, AIFIFO_Control_6143);
		ni_writel(dev, 0x00, AIFIFO_Control_6143);
		/*  Wait for complete */
		for (i = 0; i < timeout; i++) {
			if (!(ni_readl(dev, AIFIFO_Status_6143) & 0x10))
				break;
			udelay(1);
		}
		if (i == timeout) {
			comedi_error(dev, "FIFO flush timeout.");
		}
	} else {
		ni_stc_writew(dev, 1, ADC_FIFO_Clear);
		if (board->reg_type == ni_reg_625x) {
			ni_writeb(dev, 0, M_Offset_Static_AI_Control(0));
			ni_writeb(dev, 1, M_Offset_Static_AI_Control(0));
#if 0
			/* the NI example code does 3 convert pulses for 625x boards,
			   but that appears to be wrong in practice. */
			ni_stc_writew(dev, AI_CONVERT_Pulse,
				      AI_Command_1_Register);
			ni_stc_writew(dev, AI_CONVERT_Pulse,
				      AI_Command_1_Register);
			ni_stc_writew(dev, AI_CONVERT_Pulse,
				      AI_Command_1_Register);
#endif
		}
	}
}

static inline void ni_ao_win_outw(struct comedi_device *dev, uint16_t data,
				  int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	ni_writew(dev, addr, AO_Window_Address_611x);
	ni_writew(dev, data, AO_Window_Data_611x);
	spin_unlock_irqrestore(&devpriv->window_lock, flags);
}

static inline void ni_ao_win_outl(struct comedi_device *dev, uint32_t data,
				  int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	ni_writew(dev, addr, AO_Window_Address_611x);
	ni_writel(dev, data, AO_Window_Data_611x);
	spin_unlock_irqrestore(&devpriv->window_lock, flags);
}

static inline unsigned short ni_ao_win_inw(struct comedi_device *dev, int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;
	unsigned short data;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	ni_writew(dev, addr, AO_Window_Address_611x);
	data = ni_readw(dev, AO_Window_Data_611x);
	spin_unlock_irqrestore(&devpriv->window_lock, flags);
	return data;
}

/* ni_set_bits( ) allows different parts of the ni_mio_common driver to
* share registers (such as Interrupt_A_Register) without interfering with
* each other.
*
* NOTE: the switch/case statements are optimized out for a constant argument
* so this is actually quite fast---  If you must wrap another function around this
* make it inline to avoid a large speed penalty.
*
* value should only be 1 or 0.
*/
static inline void ni_set_bits(struct comedi_device *dev, int reg,
			       unsigned bits, unsigned value)
{
	unsigned bit_values;

	if (value)
		bit_values = bits;
	else
		bit_values = 0;
	ni_set_bitfield(dev, reg, bits, bit_values);
}

#ifdef PCIDMA
static void ni_sync_ai_dma(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->ai_mite_chan)
		mite_sync_input_dma(devpriv->ai_mite_chan, s);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}

static int ni_ai_drain_dma(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	int i;
	static const int timeout = 10000;
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->ai_mite_chan) {
		for (i = 0; i < timeout; i++) {
			if ((ni_stc_readw(dev, AI_Status_1_Register) &
			     AI_FIFO_Empty_St)
			    && mite_bytes_in_transit(devpriv->ai_mite_chan) ==
			    0)
				break;
			udelay(5);
		}
		if (i == timeout) {
			printk("ni_mio_common: wait for dma drain timed out\n");
			printk
			    ("mite_bytes_in_transit=%i, AI_Status1_Register=0x%x\n",
			     mite_bytes_in_transit(devpriv->ai_mite_chan),
			     ni_stc_readw(dev, AI_Status_1_Register));
			retval = -1;
		}
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	ni_sync_ai_dma(dev);

	return retval;
}

static void mite_handle_b_linkc(struct mite_struct *mite,
				struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->write_subdev;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->ao_mite_chan)
		mite_sync_output_dma(devpriv->ao_mite_chan, s);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}

static int ni_ao_wait_for_dma_load(struct comedi_device *dev)
{
	static const int timeout = 10000;
	int i;
	for (i = 0; i < timeout; i++) {
		unsigned short b_status;

		b_status = ni_stc_readw(dev, AO_Status_1_Register);
		if (b_status & AO_FIFO_Half_Full_St)
			break;
		/* if we poll too often, the pci bus activity seems
		   to slow the dma transfer down */
		udelay(10);
	}
	if (i == timeout) {
		comedi_error(dev, "timed out waiting for dma load");
		return -EPIPE;
	}
	return 0;
}
#endif /* PCIDMA */

#ifndef PCIDMA

static void ni_ao_fifo_load(struct comedi_device *dev,
			    struct comedi_subdevice *s, int n)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	int chan;
	int i;
	unsigned short d;
	u32 packed_data;
	int range;
	int err = 1;

	chan = async->cur_chan;
	for (i = 0; i < n; i++) {
		err &= comedi_buf_get(s, &d);
		if (err == 0)
			break;

		range = CR_RANGE(cmd->chanlist[chan]);

		if (board->reg_type & ni_reg_6xxx_mask) {
			packed_data = d & 0xffff;
			/* 6711 only has 16 bit wide ao fifo */
			if (board->reg_type != ni_reg_6711) {
				err &= comedi_buf_get(s, &d);
				if (err == 0)
					break;
				chan++;
				i++;
				packed_data |= (d << 16) & 0xffff0000;
			}
			ni_writel(dev, packed_data, DAC_FIFO_Data_611x);
		} else {
			ni_writew(dev, d, DAC_FIFO_Data);
		}
		chan++;
		chan %= cmd->chanlist_len;
	}
	async->cur_chan = chan;
	if (err == 0)
		async->events |= COMEDI_CB_OVERFLOW;
}

/*
 *  There's a small problem if the FIFO gets really low and we
 *  don't have the data to fill it.  Basically, if after we fill
 *  the FIFO with all the data available, the FIFO is _still_
 *  less than half full, we never clear the interrupt.  If the
 *  IRQ is in edge mode, we never get another interrupt, because
 *  this one wasn't cleared.  If in level mode, we get flooded
 *  with interrupts that we can't fulfill, because nothing ever
 *  gets put into the buffer.
 *
 *  This kind of situation is recoverable, but it is easier to
 *  just pretend we had a FIFO underrun, since there is a good
 *  chance it will happen anyway.  This is _not_ the case for
 *  RT code, as RT code might purposely be running close to the
 *  metal.  Needs to be fixed eventually.
 */
static int ni_ao_fifo_half_empty(struct comedi_device *dev,
				 struct comedi_subdevice *s)
{
	const struct ni_board_struct *board = comedi_board(dev);
	int n;

	n = comedi_buf_read_n_available(s);
	if (n == 0) {
		s->async->events |= COMEDI_CB_OVERFLOW;
		return 0;
	}

	n /= sizeof(short);
	if (n > board->ao_fifo_depth / 2)
		n = board->ao_fifo_depth / 2;

	ni_ao_fifo_load(dev, s, n);

	s->async->events |= COMEDI_CB_BLOCK;

	return 1;
}

static int ni_ao_prep_fifo(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	const struct ni_board_struct *board = comedi_board(dev);
	int n;

	/* reset fifo */
	ni_stc_writew(dev, 1, DAC_FIFO_Clear);
	if (board->reg_type & ni_reg_6xxx_mask)
		ni_ao_win_outl(dev, 0x6, AO_FIFO_Offset_Load_611x);

	/* load some data */
	n = comedi_buf_read_n_available(s);
	if (n == 0)
		return 0;

	n /= sizeof(short);
	if (n > board->ao_fifo_depth)
		n = board->ao_fifo_depth;

	ni_ao_fifo_load(dev, s, n);

	return n;
}

static void ni_ai_fifo_read(struct comedi_device *dev,
			    struct comedi_subdevice *s, int n)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	int i;

	if (board->reg_type == ni_reg_611x) {
		unsigned short data[2];
		u32 dl;

		for (i = 0; i < n / 2; i++) {
			dl = ni_readl(dev, ADC_FIFO_Data_611x);
			/* This may get the hi/lo data in the wrong order */
			data[0] = (dl >> 16) & 0xffff;
			data[1] = dl & 0xffff;
			cfc_write_array_to_buffer(s, data, sizeof(data));
		}
		/* Check if there's a single sample stuck in the FIFO */
		if (n % 2) {
			dl = ni_readl(dev, ADC_FIFO_Data_611x);
			data[0] = dl & 0xffff;
			cfc_write_to_buffer(s, data[0]);
		}
	} else if (board->reg_type == ni_reg_6143) {
		unsigned short data[2];
		u32 dl;

		/*  This just reads the FIFO assuming the data is present, no checks on the FIFO status are performed */
		for (i = 0; i < n / 2; i++) {
			dl = ni_readl(dev, AIFIFO_Data_6143);

			data[0] = (dl >> 16) & 0xffff;
			data[1] = dl & 0xffff;
			cfc_write_array_to_buffer(s, data, sizeof(data));
		}
		if (n % 2) {
			/* Assume there is a single sample stuck in the FIFO */
			/* Get stranded sample into FIFO */
			ni_writel(dev, 0x01, AIFIFO_Control_6143);
			dl = ni_readl(dev, AIFIFO_Data_6143);
			data[0] = (dl >> 16) & 0xffff;
			cfc_write_to_buffer(s, data[0]);
		}
	} else {
		if (n > sizeof(devpriv->ai_fifo_buffer) /
		    sizeof(devpriv->ai_fifo_buffer[0])) {
			comedi_error(dev, "bug! ai_fifo_buffer too small");
			async->events |= COMEDI_CB_ERROR;
			return;
		}
		for (i = 0; i < n; i++) {
			devpriv->ai_fifo_buffer[i] =
			    ni_readw(dev, ADC_FIFO_Data_Register);
		}
		cfc_write_array_to_buffer(s, devpriv->ai_fifo_buffer,
					  n *
					  sizeof(devpriv->ai_fifo_buffer[0]));
	}
}

static void ni_handle_fifo_half_full(struct comedi_device *dev)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct comedi_subdevice *s = dev->read_subdev;
	int n;

	n = board->ai_fifo_depth / 2;

	ni_ai_fifo_read(dev, s, n);
}
#endif

/*
   Empties the AI fifo
*/
static void ni_handle_fifo_dregs(struct comedi_device *dev)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned short data[2];
	u32 dl;
	unsigned short fifo_empty;
	int i;

	if (board->reg_type == ni_reg_611x) {
		while ((ni_stc_readw(dev, AI_Status_1_Register) &
			AI_FIFO_Empty_St) == 0) {
			dl = ni_readl(dev, ADC_FIFO_Data_611x);

			/* This may get the hi/lo data in the wrong order */
			data[0] = (dl >> 16);
			data[1] = (dl & 0xffff);
			cfc_write_array_to_buffer(s, data, sizeof(data));
		}
	} else if (board->reg_type == ni_reg_6143) {
		i = 0;
		while (ni_readl(dev, AIFIFO_Status_6143) & 0x04) {
			dl = ni_readl(dev, AIFIFO_Data_6143);

			/* This may get the hi/lo data in the wrong order */
			data[0] = (dl >> 16);
			data[1] = (dl & 0xffff);
			cfc_write_array_to_buffer(s, data, sizeof(data));
			i += 2;
		}
		/*  Check if stranded sample is present */
		if (ni_readl(dev, AIFIFO_Status_6143) & 0x01) {
			/* Get stranded sample into FIFO */
			ni_writel(dev, 0x01, AIFIFO_Control_6143);
			dl = ni_readl(dev, AIFIFO_Data_6143);
			data[0] = (dl >> 16) & 0xffff;
			cfc_write_to_buffer(s, data[0]);
		}

	} else {
		fifo_empty = ni_stc_readw(dev, AI_Status_1_Register) &
			     AI_FIFO_Empty_St;
		while (fifo_empty == 0) {
			for (i = 0;
			     i <
			     sizeof(devpriv->ai_fifo_buffer) /
			     sizeof(devpriv->ai_fifo_buffer[0]); i++) {
				fifo_empty = ni_stc_readw(dev,
							  AI_Status_1_Register) &
						AI_FIFO_Empty_St;
				if (fifo_empty)
					break;
				devpriv->ai_fifo_buffer[i] =
				    ni_readw(dev, ADC_FIFO_Data_Register);
			}
			cfc_write_array_to_buffer(s, devpriv->ai_fifo_buffer,
						  i *
						  sizeof(devpriv->
							 ai_fifo_buffer[0]));
		}
	}
}

static void get_last_sample_611x(struct comedi_device *dev)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned short data;
	u32 dl;

	if (board->reg_type != ni_reg_611x)
		return;

	/* Check if there's a single sample stuck in the FIFO */
	if (ni_readb(dev, XXX_Status) & 0x80) {
		dl = ni_readl(dev, ADC_FIFO_Data_611x);
		data = (dl & 0xffff);
		cfc_write_to_buffer(s, data);
	}
}

static void get_last_sample_6143(struct comedi_device *dev)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned short data;
	u32 dl;

	if (board->reg_type != ni_reg_6143)
		return;

	/* Check if there's a single sample stuck in the FIFO */
	if (ni_readl(dev, AIFIFO_Status_6143) & 0x01) {
		/* Get stranded sample into FIFO */
		ni_writel(dev, 0x01, AIFIFO_Control_6143);
		dl = ni_readl(dev, AIFIFO_Data_6143);

		/* This may get the hi/lo data in the wrong order */
		data = (dl >> 16) & 0xffff;
		cfc_write_to_buffer(s, data);
	}
}

static void shutdown_ai_command(struct comedi_device *dev)
{
	struct comedi_subdevice *s = dev->read_subdev;

#ifdef PCIDMA
	ni_ai_drain_dma(dev);
#endif
	ni_handle_fifo_dregs(dev);
	get_last_sample_611x(dev);
	get_last_sample_6143(dev);

	s->async->events |= COMEDI_CB_EOA;
}

static void ni_handle_eos(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->aimode == AIMODE_SCAN) {
#ifdef PCIDMA
		static const int timeout = 10;
		int i;

		for (i = 0; i < timeout; i++) {
			ni_sync_ai_dma(dev);
			if ((s->async->events & COMEDI_CB_EOS))
				break;
			udelay(1);
		}
#else
		ni_handle_fifo_dregs(dev);
		s->async->events |= COMEDI_CB_EOS;
#endif
	}
	/* handle special case of single scan using AI_End_On_End_Of_Scan */
	if ((devpriv->ai_cmd2 & AI_End_On_End_Of_Scan))
		shutdown_ai_command(dev);
}

static void handle_gpct_interrupt(struct comedi_device *dev,
				  unsigned short counter_index)
{
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s;

	s = &dev->subdevices[NI_GPCT_SUBDEV(counter_index)];

	ni_tio_handle_interrupt(&devpriv->counter_dev->counters[counter_index],
				s);
	cfc_handle_events(dev, s);
#endif
}

static void ack_a_interrupt(struct comedi_device *dev, unsigned short a_status)
{
	unsigned short ack = 0;

	if (a_status & AI_SC_TC_St)
		ack |= AI_SC_TC_Interrupt_Ack;
	if (a_status & AI_START1_St)
		ack |= AI_START1_Interrupt_Ack;
	if (a_status & AI_START_St)
		ack |= AI_START_Interrupt_Ack;
	if (a_status & AI_STOP_St)
		/* not sure why we used to ack the START here also, instead of doing it independently. Frank Hess 2007-07-06 */
		ack |= AI_STOP_Interrupt_Ack /*| AI_START_Interrupt_Ack */;
	if (ack)
		ni_stc_writew(dev, ack, Interrupt_A_Ack_Register);
}

static void handle_a_interrupt(struct comedi_device *dev, unsigned short status,
			       unsigned ai_mite_status)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;

	/* 67xx boards don't have ai subdevice, but their gpct0 might generate an a interrupt */
	if (s->type == COMEDI_SUBD_UNUSED)
		return;

#ifdef PCIDMA
	if (ai_mite_status & CHSR_LINKC)
		ni_sync_ai_dma(dev);

	if (ai_mite_status & ~(CHSR_INT | CHSR_LINKC | CHSR_DONE | CHSR_MRDY |
			       CHSR_DRDY | CHSR_DRQ1 | CHSR_DRQ0 | CHSR_ERROR |
			       CHSR_SABORT | CHSR_XFERR | CHSR_LxERR_mask)) {
		printk
		    ("unknown mite interrupt, ack! (ai_mite_status=%08x)\n",
		     ai_mite_status);
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		/* disable_irq(dev->irq); */
	}
#endif

	/* test for all uncommon interrupt events at the same time */
	if (status & (AI_Overrun_St | AI_Overflow_St | AI_SC_TC_Error_St |
		      AI_SC_TC_St | AI_START1_St)) {
		if (status == 0xffff) {
			printk
			    ("ni_mio_common: a_status=0xffff.  Card removed?\n");
			/* we probably aren't even running a command now,
			 * so it's a good idea to be careful. */
			if (comedi_is_subdevice_running(s)) {
				s->async->events |=
				    COMEDI_CB_ERROR | COMEDI_CB_EOA;
				cfc_handle_events(dev, s);
			}
			return;
		}
		if (status & (AI_Overrun_St | AI_Overflow_St |
			      AI_SC_TC_Error_St)) {
			printk("ni_mio_common: ai error a_status=%04x\n",
			       status);

			shutdown_ai_command(dev);

			s->async->events |= COMEDI_CB_ERROR;
			if (status & (AI_Overrun_St | AI_Overflow_St))
				s->async->events |= COMEDI_CB_OVERFLOW;

			cfc_handle_events(dev, s);
			return;
		}
		if (status & AI_SC_TC_St) {
			if (!devpriv->ai_continuous)
				shutdown_ai_command(dev);
		}
	}
#ifndef PCIDMA
	if (status & AI_FIFO_Half_Full_St) {
		int i;
		static const int timeout = 10;
		/* pcmcia cards (at least 6036) seem to stop producing interrupts if we
		 *fail to get the fifo less than half full, so loop to be sure.*/
		for (i = 0; i < timeout; ++i) {
			ni_handle_fifo_half_full(dev);
			if ((ni_stc_readw(dev, AI_Status_1_Register) &
			     AI_FIFO_Half_Full_St) == 0)
				break;
		}
	}
#endif /*  !PCIDMA */

	if ((status & AI_STOP_St))
		ni_handle_eos(dev, s);

	cfc_handle_events(dev, s);
}

static void ack_b_interrupt(struct comedi_device *dev, unsigned short b_status)
{
	unsigned short ack = 0;

	if (b_status & AO_BC_TC_St)
		ack |= AO_BC_TC_Interrupt_Ack;
	if (b_status & AO_Overrun_St)
		ack |= AO_Error_Interrupt_Ack;
	if (b_status & AO_START_St)
		ack |= AO_START_Interrupt_Ack;
	if (b_status & AO_START1_St)
		ack |= AO_START1_Interrupt_Ack;
	if (b_status & AO_UC_TC_St)
		ack |= AO_UC_TC_Interrupt_Ack;
	if (b_status & AO_UI2_TC_St)
		ack |= AO_UI2_TC_Interrupt_Ack;
	if (b_status & AO_UPDATE_St)
		ack |= AO_UPDATE_Interrupt_Ack;
	if (ack)
		ni_stc_writew(dev, ack, Interrupt_B_Ack_Register);
}

static void handle_b_interrupt(struct comedi_device *dev,
			       unsigned short b_status, unsigned ao_mite_status)
{
	struct comedi_subdevice *s = dev->write_subdev;
	/* unsigned short ack=0; */

#ifdef PCIDMA
	/* Currently, mite.c requires us to handle LINKC */
	if (ao_mite_status & CHSR_LINKC) {
		struct ni_private *devpriv = dev->private;

		mite_handle_b_linkc(devpriv->mite, dev);
	}

	if (ao_mite_status & ~(CHSR_INT | CHSR_LINKC | CHSR_DONE | CHSR_MRDY |
			       CHSR_DRDY | CHSR_DRQ1 | CHSR_DRQ0 | CHSR_ERROR |
			       CHSR_SABORT | CHSR_XFERR | CHSR_LxERR_mask)) {
		printk
		    ("unknown mite interrupt, ack! (ao_mite_status=%08x)\n",
		     ao_mite_status);
		s->async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
	}
#endif

	if (b_status == 0xffff)
		return;
	if (b_status & AO_Overrun_St) {
		printk
		    ("ni_mio_common: AO FIFO underrun status=0x%04x status2=0x%04x\n",
		     b_status, ni_stc_readw(dev, AO_Status_2_Register));
		s->async->events |= COMEDI_CB_OVERFLOW;
	}

	if (b_status & AO_BC_TC_St)
		s->async->events |= COMEDI_CB_EOA;

#ifndef PCIDMA
	if (b_status & AO_FIFO_Request_St) {
		int ret;

		ret = ni_ao_fifo_half_empty(dev, s);
		if (!ret) {
			printk("ni_mio_common: AO buffer underrun\n");
			ni_set_bits(dev, Interrupt_B_Enable_Register,
				    AO_FIFO_Interrupt_Enable |
				    AO_Error_Interrupt_Enable, 0);
			s->async->events |= COMEDI_CB_OVERFLOW;
		}
	}
#endif

	cfc_handle_events(dev, s);
}

static void ni_ai_munge(struct comedi_device *dev, struct comedi_subdevice *s,
			void *data, unsigned int num_bytes,
			unsigned int chan_index)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int length = num_bytes / bytes_per_sample(s);
	unsigned short *array = data;
	unsigned int *larray = data;
	unsigned int i;

	for (i = 0; i < length; i++) {
#ifdef PCIDMA
		if (s->subdev_flags & SDF_LSAMPL)
			larray[i] = le32_to_cpu(larray[i]);
		else
			array[i] = le16_to_cpu(array[i]);
#endif
		if (s->subdev_flags & SDF_LSAMPL)
			larray[i] += devpriv->ai_offset[chan_index];
		else
			array[i] += devpriv->ai_offset[chan_index];
		chan_index++;
		chan_index %= cmd->chanlist_len;
	}
}

#ifdef PCIDMA

static int ni_ai_setup_MITE_dma(struct comedi_device *dev)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	int retval;
	unsigned long flags;

	retval = ni_request_ai_mite_channel(dev);
	if (retval)
		return retval;
/* printk("comedi_debug: using mite channel %i for ai.\n", devpriv->ai_mite_chan->channel); */

	/* write alloc the entire buffer */
	comedi_buf_write_alloc(s, s->async->prealloc_bufsz);

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->ai_mite_chan == NULL) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		return -EIO;
	}

	switch (board->reg_type) {
	case ni_reg_611x:
	case ni_reg_6143:
		mite_prep_dma(devpriv->ai_mite_chan, 32, 16);
		break;
	case ni_reg_628x:
		mite_prep_dma(devpriv->ai_mite_chan, 32, 32);
		break;
	default:
		mite_prep_dma(devpriv->ai_mite_chan, 16, 16);
		break;
	}
	/*start the MITE */
	mite_dma_arm(devpriv->ai_mite_chan);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	return 0;
}

static int ni_ao_setup_MITE_dma(struct comedi_device *dev)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->write_subdev;
	int retval;
	unsigned long flags;

	retval = ni_request_ao_mite_channel(dev);
	if (retval)
		return retval;

	/* read alloc the entire buffer */
	comedi_buf_read_alloc(s, s->async->prealloc_bufsz);

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->ao_mite_chan) {
		if (board->reg_type & (ni_reg_611x | ni_reg_6713)) {
			mite_prep_dma(devpriv->ao_mite_chan, 32, 32);
		} else {
			/* doing 32 instead of 16 bit wide transfers from memory
			   makes the mite do 32 bit pci transfers, doubling pci bandwidth. */
			mite_prep_dma(devpriv->ao_mite_chan, 16, 32);
		}
		mite_dma_arm(devpriv->ao_mite_chan);
	} else
		retval = -EIO;
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	return retval;
}

#endif /*  PCIDMA */

/*
   used for both cancel ioctl and board initialization

   this is pretty harsh for a cancel, but it works...
 */

static int ni_ai_reset(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct ni_board_struct *board = comedi_board(dev);

	ni_release_ai_mite_channel(dev);
	/* ai configuration */
	ni_stc_writew(dev, AI_Configuration_Start | AI_Reset,
		      Joint_Reset_Register);

	ni_set_bits(dev, Interrupt_A_Enable_Register,
		    AI_SC_TC_Interrupt_Enable | AI_START1_Interrupt_Enable |
		    AI_START2_Interrupt_Enable | AI_START_Interrupt_Enable |
		    AI_STOP_Interrupt_Enable | AI_Error_Interrupt_Enable |
		    AI_FIFO_Interrupt_Enable, 0);

	ni_clear_ai_fifo(dev);

	if (board->reg_type != ni_reg_6143)
		ni_writeb(dev, 0, Misc_Command);

	ni_stc_writew(dev, AI_Disarm, AI_Command_1_Register); /* reset pulses */
	ni_stc_writew(dev, AI_Start_Stop | AI_Mode_1_Reserved
			    /*| AI_Trigger_Once */,
		      AI_Mode_1_Register);
	ni_stc_writew(dev, 0x0000, AI_Mode_2_Register);
	/* generate FIFO interrupts on non-empty */
	ni_stc_writew(dev, (0 << 6) | 0x0000, AI_Mode_3_Register);
	if (board->reg_type == ni_reg_611x) {
		ni_stc_writew(dev,
			      AI_SHIFTIN_Pulse_Width |
			      AI_SOC_Polarity |
			      AI_LOCALMUX_CLK_Pulse_Width,
			      AI_Personal_Register);
		ni_stc_writew(dev,
			      AI_SCAN_IN_PROG_Output_Select(3) |
			      AI_EXTMUX_CLK_Output_Select(0) |
			      AI_LOCALMUX_CLK_Output_Select(2) |
			      AI_SC_TC_Output_Select(3) |
			      AI_CONVERT_Output_Select
			      (AI_CONVERT_Output_Enable_High),
			      AI_Output_Control_Register);
	} else if (board->reg_type == ni_reg_6143) {
		ni_stc_writew(dev, AI_SHIFTIN_Pulse_Width |
				   AI_SOC_Polarity |
				   AI_LOCALMUX_CLK_Pulse_Width,
			      AI_Personal_Register);
		ni_stc_writew(dev,
			      AI_SCAN_IN_PROG_Output_Select(3) |
			      AI_EXTMUX_CLK_Output_Select(0) |
			      AI_LOCALMUX_CLK_Output_Select(2) |
			      AI_SC_TC_Output_Select(3) |
			      AI_CONVERT_Output_Select
			      (AI_CONVERT_Output_Enable_Low),
			      AI_Output_Control_Register);
	} else {
		unsigned ai_output_control_bits;
		ni_stc_writew(dev,
			      AI_SHIFTIN_Pulse_Width |
			      AI_SOC_Polarity |
			      AI_CONVERT_Pulse_Width |
			      AI_LOCALMUX_CLK_Pulse_Width,
			      AI_Personal_Register);
		ai_output_control_bits =
		    AI_SCAN_IN_PROG_Output_Select(3) |
		    AI_EXTMUX_CLK_Output_Select(0) |
		    AI_LOCALMUX_CLK_Output_Select(2) |
		    AI_SC_TC_Output_Select(3);
		if (board->reg_type == ni_reg_622x)
			ai_output_control_bits |=
			    AI_CONVERT_Output_Select
			    (AI_CONVERT_Output_Enable_High);
		else
			ai_output_control_bits |=
			    AI_CONVERT_Output_Select
			    (AI_CONVERT_Output_Enable_Low);
		ni_stc_writew(dev, ai_output_control_bits,
			      AI_Output_Control_Register);
	}
	/* the following registers should not be changed, because there
	 * are no backup registers in devpriv.  If you want to change
	 * any of these, add a backup register and other appropriate code:
	 *      AI_Mode_1_Register
	 *      AI_Mode_3_Register
	 *      AI_Personal_Register
	 *      AI_Output_Control_Register
	 */
	ni_stc_writew(dev,
		      AI_SC_TC_Error_Confirm |
		      AI_START_Interrupt_Ack |
		      AI_START2_Interrupt_Ack |
		      AI_START1_Interrupt_Ack |
		      AI_SC_TC_Interrupt_Ack |
		      AI_Error_Interrupt_Ack |
		      AI_STOP_Interrupt_Ack,
		      Interrupt_A_Ack_Register);	/* clear interrupts */

	ni_stc_writew(dev, AI_Configuration_End, Joint_Reset_Register);

	return 0;
}

static int ni_ai_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	unsigned long flags;
	int count;

	/*  lock to avoid race with interrupt handler */
	spin_lock_irqsave(&dev->spinlock, flags);
#ifndef PCIDMA
	ni_handle_fifo_dregs(dev);
#else
	ni_sync_ai_dma(dev);
#endif
	count = comedi_buf_n_bytes_ready(s);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return count;
}

static void ni_prime_channelgain_list(struct comedi_device *dev)
{
	int i;

	ni_stc_writew(dev, AI_CONVERT_Pulse, AI_Command_1_Register);
	for (i = 0; i < NI_TIMEOUT; ++i) {
		if (!(ni_stc_readw(dev, AI_Status_1_Register) &
		      AI_FIFO_Empty_St)) {
			ni_stc_writew(dev, 1, ADC_FIFO_Clear);
			return;
		}
		udelay(1);
	}
	printk("ni_mio_common: timeout loading channel/gain list\n");
}

static void ni_m_series_load_channelgain_list(struct comedi_device *dev,
					      unsigned int n_chan,
					      unsigned int *list)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	unsigned int chan, range, aref;
	unsigned int i;
	unsigned offset;
	unsigned int dither;
	unsigned range_code;

	ni_stc_writew(dev, 1, Configuration_Memory_Clear);

/* offset = 1 << (board->adbits - 1); */
	if ((list[0] & CR_ALT_SOURCE)) {
		unsigned bypass_bits;
		chan = CR_CHAN(list[0]);
		range = CR_RANGE(list[0]);
		range_code = ni_gainlkup[board->gainlkup][range];
		dither = ((list[0] & CR_ALT_FILTER) != 0);
		bypass_bits = MSeries_AI_Bypass_Config_FIFO_Bit;
		bypass_bits |= chan;
		bypass_bits |=
		    (devpriv->ai_calib_source) &
		    (MSeries_AI_Bypass_Cal_Sel_Pos_Mask |
		     MSeries_AI_Bypass_Cal_Sel_Neg_Mask |
		     MSeries_AI_Bypass_Mode_Mux_Mask |
		     MSeries_AO_Bypass_AO_Cal_Sel_Mask);
		bypass_bits |= MSeries_AI_Bypass_Gain_Bits(range_code);
		if (dither)
			bypass_bits |= MSeries_AI_Bypass_Dither_Bit;
		/*  don't use 2's complement encoding */
		bypass_bits |= MSeries_AI_Bypass_Polarity_Bit;
		ni_writel(dev, bypass_bits, M_Offset_AI_Config_FIFO_Bypass);
	} else {
		ni_writel(dev, 0, M_Offset_AI_Config_FIFO_Bypass);
	}
	offset = 0;
	for (i = 0; i < n_chan; i++) {
		unsigned config_bits = 0;
		chan = CR_CHAN(list[i]);
		aref = CR_AREF(list[i]);
		range = CR_RANGE(list[i]);
		dither = ((list[i] & CR_ALT_FILTER) != 0);

		range_code = ni_gainlkup[board->gainlkup][range];
		devpriv->ai_offset[i] = offset;
		switch (aref) {
		case AREF_DIFF:
			config_bits |=
			    MSeries_AI_Config_Channel_Type_Differential_Bits;
			break;
		case AREF_COMMON:
			config_bits |=
			    MSeries_AI_Config_Channel_Type_Common_Ref_Bits;
			break;
		case AREF_GROUND:
			config_bits |=
			    MSeries_AI_Config_Channel_Type_Ground_Ref_Bits;
			break;
		case AREF_OTHER:
			break;
		}
		config_bits |= MSeries_AI_Config_Channel_Bits(chan);
		config_bits |=
		    MSeries_AI_Config_Bank_Bits(board->reg_type, chan);
		config_bits |= MSeries_AI_Config_Gain_Bits(range_code);
		if (i == n_chan - 1)
			config_bits |= MSeries_AI_Config_Last_Channel_Bit;
		if (dither)
			config_bits |= MSeries_AI_Config_Dither_Bit;
		/*  don't use 2's complement encoding */
		config_bits |= MSeries_AI_Config_Polarity_Bit;
		ni_writew(dev, config_bits, M_Offset_AI_Config_FIFO_Data);
	}
	ni_prime_channelgain_list(dev);
}

/*
 * Notes on the 6110 and 6111:
 * These boards a slightly different than the rest of the series, since
 * they have multiple A/D converters.
 * From the driver side, the configuration memory is a
 * little different.
 * Configuration Memory Low:
 *   bits 15-9: same
 *   bit 8: unipolar/bipolar (should be 0 for bipolar)
 *   bits 0-3: gain.  This is 4 bits instead of 3 for the other boards
 *       1001 gain=0.1 (+/- 50)
 *       1010 0.2
 *       1011 0.1
 *       0001 1
 *       0010 2
 *       0011 5
 *       0100 10
 *       0101 20
 *       0110 50
 * Configuration Memory High:
 *   bits 12-14: Channel Type
 *       001 for differential
 *       000 for calibration
 *   bit 11: coupling  (this is not currently handled)
 *       1 AC coupling
 *       0 DC coupling
 *   bits 0-2: channel
 *       valid channels are 0-3
 */
static void ni_load_channelgain_list(struct comedi_device *dev,
				     unsigned int n_chan, unsigned int *list)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	unsigned int chan, range, aref;
	unsigned int i;
	unsigned int hi, lo;
	unsigned offset;
	unsigned int dither;

	if (devpriv->is_m_series) {
		ni_m_series_load_channelgain_list(dev, n_chan, list);
		return;
	}
	if (n_chan == 1 && (board->reg_type != ni_reg_611x)
	    && (board->reg_type != ni_reg_6143)) {
		if (devpriv->changain_state
		    && devpriv->changain_spec == list[0]) {
			/*  ready to go. */
			return;
		}
		devpriv->changain_state = 1;
		devpriv->changain_spec = list[0];
	} else {
		devpriv->changain_state = 0;
	}

	ni_stc_writew(dev, 1, Configuration_Memory_Clear);

	/*  Set up Calibration mode if required */
	if (board->reg_type == ni_reg_6143) {
		if ((list[0] & CR_ALT_SOURCE)
		    && !devpriv->ai_calib_source_enabled) {
			/*  Strobe Relay enable bit */
			ni_writew(dev, devpriv->ai_calib_source |
				       Calibration_Channel_6143_RelayOn,
				  Calibration_Channel_6143);
			ni_writew(dev, devpriv->ai_calib_source,
				  Calibration_Channel_6143);
			devpriv->ai_calib_source_enabled = 1;
			msleep_interruptible(100);	/*  Allow relays to change */
		} else if (!(list[0] & CR_ALT_SOURCE)
			   && devpriv->ai_calib_source_enabled) {
			/*  Strobe Relay disable bit */
			ni_writew(dev, devpriv->ai_calib_source |
				       Calibration_Channel_6143_RelayOff,
				  Calibration_Channel_6143);
			ni_writew(dev, devpriv->ai_calib_source,
				  Calibration_Channel_6143);
			devpriv->ai_calib_source_enabled = 0;
			msleep_interruptible(100);	/*  Allow relays to change */
		}
	}

	offset = 1 << (board->adbits - 1);
	for (i = 0; i < n_chan; i++) {
		if ((board->reg_type != ni_reg_6143)
		    && (list[i] & CR_ALT_SOURCE)) {
			chan = devpriv->ai_calib_source;
		} else {
			chan = CR_CHAN(list[i]);
		}
		aref = CR_AREF(list[i]);
		range = CR_RANGE(list[i]);
		dither = ((list[i] & CR_ALT_FILTER) != 0);

		/* fix the external/internal range differences */
		range = ni_gainlkup[board->gainlkup][range];
		if (board->reg_type == ni_reg_611x)
			devpriv->ai_offset[i] = offset;
		else
			devpriv->ai_offset[i] = (range & 0x100) ? 0 : offset;

		hi = 0;
		if ((list[i] & CR_ALT_SOURCE)) {
			if (board->reg_type == ni_reg_611x)
				ni_writew(dev, CR_CHAN(list[i]) & 0x0003,
					  Calibration_Channel_Select_611x);
		} else {
			if (board->reg_type == ni_reg_611x)
				aref = AREF_DIFF;
			else if (board->reg_type == ni_reg_6143)
				aref = AREF_OTHER;
			switch (aref) {
			case AREF_DIFF:
				hi |= AI_DIFFERENTIAL;
				break;
			case AREF_COMMON:
				hi |= AI_COMMON;
				break;
			case AREF_GROUND:
				hi |= AI_GROUND;
				break;
			case AREF_OTHER:
				break;
			}
		}
		hi |= AI_CONFIG_CHANNEL(chan);

		ni_writew(dev, hi, Configuration_Memory_High);

		if (board->reg_type != ni_reg_6143) {
			lo = range;
			if (i == n_chan - 1)
				lo |= AI_LAST_CHANNEL;
			if (dither)
				lo |= AI_DITHER;

			ni_writew(dev, lo, Configuration_Memory_Low);
		}
	}

	/* prime the channel/gain list */
	if ((board->reg_type != ni_reg_611x)
	    && (board->reg_type != ni_reg_6143)) {
		ni_prime_channelgain_list(dev);
	}
}

static int ni_ai_insn_read(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	int i, n;
	const unsigned int mask = (1 << board->adbits) - 1;
	unsigned signbits;
	unsigned short d;
	unsigned long dl;

	ni_load_channelgain_list(dev, 1, &insn->chanspec);

	ni_clear_ai_fifo(dev);

	signbits = devpriv->ai_offset[0];
	if (board->reg_type == ni_reg_611x) {
		for (n = 0; n < num_adc_stages_611x; n++) {
			ni_stc_writew(dev, AI_CONVERT_Pulse,
				      AI_Command_1_Register);
			udelay(1);
		}
		for (n = 0; n < insn->n; n++) {
			ni_stc_writew(dev, AI_CONVERT_Pulse,
				      AI_Command_1_Register);
			/* The 611x has screwy 32-bit FIFOs. */
			d = 0;
			for (i = 0; i < NI_TIMEOUT; i++) {
				if (ni_readb(dev, XXX_Status) & 0x80) {
					d = ni_readl(dev, ADC_FIFO_Data_611x);
					d >>= 16;
					d &= 0xffff;
					break;
				}
				if (!(ni_stc_readw(dev, AI_Status_1_Register) &
				      AI_FIFO_Empty_St)) {
					d = ni_readl(dev, ADC_FIFO_Data_611x);
					d &= 0xffff;
					break;
				}
			}
			if (i == NI_TIMEOUT) {
				printk
				    ("ni_mio_common: timeout in 611x ni_ai_insn_read\n");
				return -ETIME;
			}
			d += signbits;
			data[n] = d;
		}
	} else if (board->reg_type == ni_reg_6143) {
		for (n = 0; n < insn->n; n++) {
			ni_stc_writew(dev, AI_CONVERT_Pulse,
				      AI_Command_1_Register);

			/* The 6143 has 32-bit FIFOs. You need to strobe a bit to move a single 16bit stranded sample into the FIFO */
			dl = 0;
			for (i = 0; i < NI_TIMEOUT; i++) {
				if (ni_readl(dev, AIFIFO_Status_6143) & 0x01) {
					/* Get stranded sample into FIFO */
					ni_writel(dev, 0x01,
						  AIFIFO_Control_6143);
					dl = ni_readl(dev, AIFIFO_Data_6143);
					break;
				}
			}
			if (i == NI_TIMEOUT) {
				printk
				    ("ni_mio_common: timeout in 6143 ni_ai_insn_read\n");
				return -ETIME;
			}
			data[n] = (((dl >> 16) & 0xFFFF) + signbits) & 0xFFFF;
		}
	} else {
		for (n = 0; n < insn->n; n++) {
			ni_stc_writew(dev, AI_CONVERT_Pulse,
				      AI_Command_1_Register);
			for (i = 0; i < NI_TIMEOUT; i++) {
				if (!(ni_stc_readw(dev, AI_Status_1_Register) &
				      AI_FIFO_Empty_St))
					break;
			}
			if (i == NI_TIMEOUT) {
				printk
				    ("ni_mio_common: timeout in ni_ai_insn_read\n");
				return -ETIME;
			}
			if (devpriv->is_m_series) {
				dl = ni_readl(dev, M_Offset_AI_FIFO_Data);
				dl &= mask;
				data[n] = dl;
			} else {
				d = ni_readw(dev, ADC_FIFO_Data_Register);
				d += signbits;	/* subtle: needs to be short addition */
				data[n] = d;
			}
		}
	}
	return insn->n;
}

static int ni_ns_to_timer(const struct comedi_device *dev, unsigned nanosec,
			  int round_mode)
{
	struct ni_private *devpriv = dev->private;
	int divider;

	switch (round_mode) {
	case TRIG_ROUND_NEAREST:
	default:
		divider = (nanosec + devpriv->clock_ns / 2) / devpriv->clock_ns;
		break;
	case TRIG_ROUND_DOWN:
		divider = (nanosec) / devpriv->clock_ns;
		break;
	case TRIG_ROUND_UP:
		divider = (nanosec + devpriv->clock_ns - 1) / devpriv->clock_ns;
		break;
	}
	return divider - 1;
}

static unsigned ni_timer_to_ns(const struct comedi_device *dev, int timer)
{
	struct ni_private *devpriv = dev->private;

	return devpriv->clock_ns * (timer + 1);
}

static unsigned ni_min_ai_scan_period_ns(struct comedi_device *dev,
					 unsigned num_channels)
{
	const struct ni_board_struct *board = comedi_board(dev);

	switch (board->reg_type) {
	case ni_reg_611x:
	case ni_reg_6143:
		/*  simultaneously-sampled inputs */
		return board->ai_speed;
		break;
	default:
		/*  multiplexed inputs */
		break;
	}
	return board->ai_speed * num_channels;
}

static int ni_ai_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_cmd *cmd)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	int err = 0;
	unsigned int tmp;
	unsigned int sources;

	/* Step 1 : check if triggers are trivially valid */

	if ((cmd->flags & CMDF_WRITE))
		cmd->flags &= ~CMDF_WRITE;

	err |= cfc_check_trigger_src(&cmd->start_src,
					TRIG_NOW | TRIG_INT | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT);

	sources = TRIG_TIMER | TRIG_EXT;
	if (board->reg_type == ni_reg_611x ||
	    board->reg_type == ni_reg_6143)
		sources |= TRIG_NOW;
	err |= cfc_check_trigger_src(&cmd->convert_src, sources);

	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_NOW:
	case TRIG_INT:
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		tmp = CR_CHAN(cmd->start_arg);

		if (tmp > 16)
			tmp = 16;
		tmp |= (cmd->start_arg & (CR_INVERT | CR_EDGE));
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, tmp);
		break;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
			ni_min_ai_scan_period_ns(dev, cmd->chanlist_len));
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg,
						 devpriv->clock_ns * 0xffffff);
	} else if (cmd->scan_begin_src == TRIG_EXT) {
		/* external trigger */
		unsigned int tmp = CR_CHAN(cmd->scan_begin_arg);

		if (tmp > 16)
			tmp = 16;
		tmp |= (cmd->scan_begin_arg & (CR_INVERT | CR_EDGE));
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, tmp);
	} else {		/* TRIG_OTHER */
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	}

	if (cmd->convert_src == TRIG_TIMER) {
		if ((board->reg_type == ni_reg_611x)
		    || (board->reg_type == ni_reg_6143)) {
			err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
		} else {
			err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
							 board->ai_speed);
			err |= cfc_check_trigger_arg_max(&cmd->convert_arg,
						devpriv->clock_ns * 0xffff);
		}
	} else if (cmd->convert_src == TRIG_EXT) {
		/* external trigger */
		unsigned int tmp = CR_CHAN(cmd->convert_arg);

		if (tmp > 16)
			tmp = 16;
		tmp |= (cmd->convert_arg & (CR_ALT_FILTER | CR_INVERT));
		err |= cfc_check_trigger_arg_is(&cmd->convert_arg, tmp);
	} else if (cmd->convert_src == TRIG_NOW) {
		err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	}

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT) {
		unsigned int max_count = 0x01000000;

		if (board->reg_type == ni_reg_611x)
			max_count -= num_adc_stages_611x;
		err |= cfc_check_trigger_arg_max(&cmd->stop_arg, max_count);
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	} else {
		/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		cmd->scan_begin_arg =
		    ni_timer_to_ns(dev, ni_ns_to_timer(dev,
						       cmd->scan_begin_arg,
						       cmd->
						       flags &
						       TRIG_ROUND_MASK));
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if ((board->reg_type != ni_reg_611x)
		    && (board->reg_type != ni_reg_6143)) {
			tmp = cmd->convert_arg;
			cmd->convert_arg =
			    ni_timer_to_ns(dev, ni_ns_to_timer(dev,
							       cmd->convert_arg,
							       cmd->
							       flags &
							       TRIG_ROUND_MASK));
			if (tmp != cmd->convert_arg)
				err++;
			if (cmd->scan_begin_src == TRIG_TIMER &&
			    cmd->scan_begin_arg <
			    cmd->convert_arg * cmd->scan_end_arg) {
				cmd->scan_begin_arg =
				    cmd->convert_arg * cmd->scan_end_arg;
				err++;
			}
		}
	}

	if (err)
		return 4;

	return 0;
}

static int ni_ai_inttrig(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 unsigned int trig_num)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	ni_stc_writew(dev, AI_START1_Pulse | devpriv->ai_cmd2,
		      AI_Command_2_Register);
	s->async->inttrig = NULL;

	return 1;
}

static int ni_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	const struct comedi_cmd *cmd = &s->async->cmd;
	int timer;
	int mode1 = 0;		/* mode1 is needed for both stop and convert */
	int mode2 = 0;
	int start_stop_select = 0;
	unsigned int stop_count;
	int interrupt_a_enable = 0;

	if (dev->irq == 0) {
		comedi_error(dev, "cannot run command without an irq");
		return -EIO;
	}
	ni_clear_ai_fifo(dev);

	ni_load_channelgain_list(dev, cmd->chanlist_len, cmd->chanlist);

	/* start configuration */
	ni_stc_writew(dev, AI_Configuration_Start, Joint_Reset_Register);

	/* disable analog triggering for now, since it
	 * interferes with the use of pfi0 */
	devpriv->an_trig_etc_reg &= ~Analog_Trigger_Enable;
	ni_stc_writew(dev, devpriv->an_trig_etc_reg,
		      Analog_Trigger_Etc_Register);

	switch (cmd->start_src) {
	case TRIG_INT:
	case TRIG_NOW:
		ni_stc_writew(dev,
			      AI_START2_Select(0) |
			      AI_START1_Sync | AI_START1_Edge |
			      AI_START1_Select(0),
			      AI_Trigger_Select_Register);
		break;
	case TRIG_EXT:
		{
			int chan = CR_CHAN(cmd->start_arg);
			unsigned int bits = AI_START2_Select(0) |
			    AI_START1_Sync | AI_START1_Select(chan + 1);

			if (cmd->start_arg & CR_INVERT)
				bits |= AI_START1_Polarity;
			if (cmd->start_arg & CR_EDGE)
				bits |= AI_START1_Edge;
			ni_stc_writew(dev, bits, AI_Trigger_Select_Register);
			break;
		}
	}

	mode2 &= ~AI_Pre_Trigger;
	mode2 &= ~AI_SC_Initial_Load_Source;
	mode2 &= ~AI_SC_Reload_Mode;
	ni_stc_writew(dev, mode2, AI_Mode_2_Register);

	if (cmd->chanlist_len == 1 || (board->reg_type == ni_reg_611x)
	    || (board->reg_type == ni_reg_6143)) {
		start_stop_select |= AI_STOP_Polarity;
		start_stop_select |= AI_STOP_Select(31);	/*  logic low */
		start_stop_select |= AI_STOP_Sync;
	} else {
		start_stop_select |= AI_STOP_Select(19);	/*  ai configuration memory */
	}
	ni_stc_writew(dev, start_stop_select, AI_START_STOP_Select_Register);

	devpriv->ai_cmd2 = 0;
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		stop_count = cmd->stop_arg - 1;

		if (board->reg_type == ni_reg_611x) {
			/*  have to take 3 stage adc pipeline into account */
			stop_count += num_adc_stages_611x;
		}
		/* stage number of scans */
		ni_stc_writel(dev, stop_count, AI_SC_Load_A_Registers);

		mode1 |= AI_Start_Stop | AI_Mode_1_Reserved | AI_Trigger_Once;
		ni_stc_writew(dev, mode1, AI_Mode_1_Register);
		/* load SC (Scan Count) */
		ni_stc_writew(dev, AI_SC_Load, AI_Command_1_Register);

		devpriv->ai_continuous = 0;
		if (stop_count == 0) {
			devpriv->ai_cmd2 |= AI_End_On_End_Of_Scan;
			interrupt_a_enable |= AI_STOP_Interrupt_Enable;
			/*  this is required to get the last sample for chanlist_len > 1, not sure why */
			if (cmd->chanlist_len > 1)
				start_stop_select |=
				    AI_STOP_Polarity | AI_STOP_Edge;
		}
		break;
	case TRIG_NONE:
		/* stage number of scans */
		ni_stc_writel(dev, 0, AI_SC_Load_A_Registers);

		mode1 |= AI_Start_Stop | AI_Mode_1_Reserved | AI_Continuous;
		ni_stc_writew(dev, mode1, AI_Mode_1_Register);

		/* load SC (Scan Count) */
		ni_stc_writew(dev, AI_SC_Load, AI_Command_1_Register);

		devpriv->ai_continuous = 1;

		break;
	}

	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		/*
		   stop bits for non 611x boards
		   AI_SI_Special_Trigger_Delay=0
		   AI_Pre_Trigger=0
		   AI_START_STOP_Select_Register:
		   AI_START_Polarity=0 (?)      rising edge
		   AI_START_Edge=1              edge triggered
		   AI_START_Sync=1 (?)
		   AI_START_Select=0            SI_TC
		   AI_STOP_Polarity=0           rising edge
		   AI_STOP_Edge=0               level
		   AI_STOP_Sync=1
		   AI_STOP_Select=19            external pin (configuration mem)
		 */
		start_stop_select |= AI_START_Edge | AI_START_Sync;
		ni_stc_writew(dev, start_stop_select,
			      AI_START_STOP_Select_Register);

		mode2 |= AI_SI_Reload_Mode(0);
		/* AI_SI_Initial_Load_Source=A */
		mode2 &= ~AI_SI_Initial_Load_Source;
		/* mode2 |= AI_SC_Reload_Mode; */
		ni_stc_writew(dev, mode2, AI_Mode_2_Register);

		/* load SI */
		timer = ni_ns_to_timer(dev, cmd->scan_begin_arg,
				       TRIG_ROUND_NEAREST);
		ni_stc_writel(dev, timer, AI_SI_Load_A_Registers);
		ni_stc_writew(dev, AI_SI_Load, AI_Command_1_Register);
		break;
	case TRIG_EXT:
		if (cmd->scan_begin_arg & CR_EDGE)
			start_stop_select |= AI_START_Edge;
		/* AI_START_Polarity==1 is falling edge */
		if (cmd->scan_begin_arg & CR_INVERT)
			start_stop_select |= AI_START_Polarity;
		if (cmd->scan_begin_src != cmd->convert_src ||
		    (cmd->scan_begin_arg & ~CR_EDGE) !=
		    (cmd->convert_arg & ~CR_EDGE))
			start_stop_select |= AI_START_Sync;
		start_stop_select |=
		    AI_START_Select(1 + CR_CHAN(cmd->scan_begin_arg));
		ni_stc_writew(dev, start_stop_select,
				    AI_START_STOP_Select_Register);
		break;
	}

	switch (cmd->convert_src) {
	case TRIG_TIMER:
	case TRIG_NOW:
		if (cmd->convert_arg == 0 || cmd->convert_src == TRIG_NOW)
			timer = 1;
		else
			timer = ni_ns_to_timer(dev, cmd->convert_arg,
					       TRIG_ROUND_NEAREST);
		/* 0,0 does not work */
		ni_stc_writew(dev, 1, AI_SI2_Load_A_Register);
		ni_stc_writew(dev, timer, AI_SI2_Load_B_Register);

		/* AI_SI2_Reload_Mode = alternate */
		/* AI_SI2_Initial_Load_Source = A */
		mode2 &= ~AI_SI2_Initial_Load_Source;
		mode2 |= AI_SI2_Reload_Mode;
		ni_stc_writew(dev, mode2, AI_Mode_2_Register);

		/* AI_SI2_Load */
		ni_stc_writew(dev, AI_SI2_Load, AI_Command_1_Register);

		mode2 |= AI_SI2_Reload_Mode;	/*  alternate */
		mode2 |= AI_SI2_Initial_Load_Source;	/*  B */

		ni_stc_writew(dev, mode2, AI_Mode_2_Register);
		break;
	case TRIG_EXT:
		mode1 |= AI_CONVERT_Source_Select(1 + cmd->convert_arg);
		if ((cmd->convert_arg & CR_INVERT) == 0)
			mode1 |= AI_CONVERT_Source_Polarity;
		ni_stc_writew(dev, mode1, AI_Mode_1_Register);

		mode2 |= AI_Start_Stop_Gate_Enable | AI_SC_Gate_Enable;
		ni_stc_writew(dev, mode2, AI_Mode_2_Register);

		break;
	}

	if (dev->irq) {

		/* interrupt on FIFO, errors, SC_TC */
		interrupt_a_enable |= AI_Error_Interrupt_Enable |
		    AI_SC_TC_Interrupt_Enable;

#ifndef PCIDMA
		interrupt_a_enable |= AI_FIFO_Interrupt_Enable;
#endif

		if (cmd->flags & TRIG_WAKE_EOS
		    || (devpriv->ai_cmd2 & AI_End_On_End_Of_Scan)) {
			/* wake on end-of-scan */
			devpriv->aimode = AIMODE_SCAN;
		} else {
			devpriv->aimode = AIMODE_HALF_FULL;
		}

		switch (devpriv->aimode) {
		case AIMODE_HALF_FULL:
			/*generate FIFO interrupts and DMA requests on half-full */
#ifdef PCIDMA
			ni_stc_writew(dev, AI_FIFO_Mode_HF_to_E,
				      AI_Mode_3_Register);
#else
			ni_stc_writew(dev, AI_FIFO_Mode_HF,
				      AI_Mode_3_Register);
#endif
			break;
		case AIMODE_SAMPLE:
			/*generate FIFO interrupts on non-empty */
			ni_stc_writew(dev, AI_FIFO_Mode_NE,
				      AI_Mode_3_Register);
			break;
		case AIMODE_SCAN:
#ifdef PCIDMA
			ni_stc_writew(dev, AI_FIFO_Mode_NE,
				      AI_Mode_3_Register);
#else
			ni_stc_writew(dev, AI_FIFO_Mode_HF,
				      AI_Mode_3_Register);
#endif
			interrupt_a_enable |= AI_STOP_Interrupt_Enable;
			break;
		default:
			break;
		}

		/* clear interrupts */
		ni_stc_writew(dev,
			      AI_Error_Interrupt_Ack |
			      AI_STOP_Interrupt_Ack |
			      AI_START_Interrupt_Ack |
			      AI_START2_Interrupt_Ack |
			      AI_START1_Interrupt_Ack |
			      AI_SC_TC_Interrupt_Ack |
			      AI_SC_TC_Error_Confirm,
			      Interrupt_A_Ack_Register);

		ni_set_bits(dev, Interrupt_A_Enable_Register,
			    interrupt_a_enable, 1);
	} else {
		/* interrupt on nothing */
		ni_set_bits(dev, Interrupt_A_Enable_Register, ~0, 0);

		/* XXX start polling if necessary */
	}

	/* end configuration */
	ni_stc_writew(dev, AI_Configuration_End, Joint_Reset_Register);

	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		ni_stc_writew(dev,
			      AI_SI2_Arm | AI_SI_Arm | AI_DIV_Arm | AI_SC_Arm,
			      AI_Command_1_Register);
		break;
	case TRIG_EXT:
		/* XXX AI_SI_Arm? */
		ni_stc_writew(dev,
			      AI_SI2_Arm | AI_SI_Arm | AI_DIV_Arm | AI_SC_Arm,
			      AI_Command_1_Register);
		break;
	}

#ifdef PCIDMA
	{
		int retval = ni_ai_setup_MITE_dma(dev);
		if (retval)
			return retval;
	}
#endif

	if (cmd->start_src == TRIG_NOW) {
		/* AI_START1_Pulse */
		ni_stc_writew(dev, AI_START1_Pulse | devpriv->ai_cmd2,
			      AI_Command_2_Register);
		s->async->inttrig = NULL;
	} else if (cmd->start_src == TRIG_EXT) {
		s->async->inttrig = NULL;
	} else {	/* TRIG_INT */
		s->async->inttrig = ni_ai_inttrig;
	}

	return 0;
}

static int ni_ai_config_analog_trig(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	unsigned int a, b, modebits;
	int err = 0;

	/* data[1] is flags
	 * data[2] is analog line
	 * data[3] is set level
	 * data[4] is reset level */
	if (!board->has_analog_trig)
		return -EINVAL;
	if ((data[1] & 0xffff0000) != COMEDI_EV_SCAN_BEGIN) {
		data[1] &= (COMEDI_EV_SCAN_BEGIN | 0xffff);
		err++;
	}
	if (data[2] >= board->n_adchan) {
		data[2] = board->n_adchan - 1;
		err++;
	}
	if (data[3] > 255) {	/* a */
		data[3] = 255;
		err++;
	}
	if (data[4] > 255) {	/* b */
		data[4] = 255;
		err++;
	}
	/*
	 * 00 ignore
	 * 01 set
	 * 10 reset
	 *
	 * modes:
	 *   1 level:                    +b-   +a-
	 *     high mode                00 00 01 10
	 *     low mode                 00 00 10 01
	 *   2 level: (a<b)
	 *     hysteresis low mode      10 00 00 01
	 *     hysteresis high mode     01 00 00 10
	 *     middle mode              10 01 01 10
	 */

	a = data[3];
	b = data[4];
	modebits = data[1] & 0xff;
	if (modebits & 0xf0) {
		/* two level mode */
		if (b < a) {
			/* swap order */
			a = data[4];
			b = data[3];
			modebits =
			    ((data[1] & 0xf) << 4) | ((data[1] & 0xf0) >> 4);
		}
		devpriv->atrig_low = a;
		devpriv->atrig_high = b;
		switch (modebits) {
		case 0x81:	/* low hysteresis mode */
			devpriv->atrig_mode = 6;
			break;
		case 0x42:	/* high hysteresis mode */
			devpriv->atrig_mode = 3;
			break;
		case 0x96:	/* middle window mode */
			devpriv->atrig_mode = 2;
			break;
		default:
			data[1] &= ~0xff;
			err++;
		}
	} else {
		/* one level mode */
		if (b != 0) {
			data[4] = 0;
			err++;
		}
		switch (modebits) {
		case 0x06:	/* high window mode */
			devpriv->atrig_high = a;
			devpriv->atrig_mode = 0;
			break;
		case 0x09:	/* low window mode */
			devpriv->atrig_low = a;
			devpriv->atrig_mode = 1;
			break;
		default:
			data[1] &= ~0xff;
			err++;
		}
	}
	if (err)
		return -EAGAIN;
	return 5;
}

static int ni_ai_insn_config(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;

	if (insn->n < 1)
		return -EINVAL;

	switch (data[0]) {
	case INSN_CONFIG_ANALOG_TRIG:
		return ni_ai_config_analog_trig(dev, s, insn, data);
	case INSN_CONFIG_ALT_SOURCE:
		if (devpriv->is_m_series) {
			if (data[1] & ~(MSeries_AI_Bypass_Cal_Sel_Pos_Mask |
					MSeries_AI_Bypass_Cal_Sel_Neg_Mask |
					MSeries_AI_Bypass_Mode_Mux_Mask |
					MSeries_AO_Bypass_AO_Cal_Sel_Mask)) {
				return -EINVAL;
			}
			devpriv->ai_calib_source = data[1];
		} else if (board->reg_type == ni_reg_6143) {
			unsigned int calib_source;

			calib_source = data[1] & 0xf;

			if (calib_source > 0xF)
				return -EINVAL;

			devpriv->ai_calib_source = calib_source;
			ni_writew(dev, calib_source, Calibration_Channel_6143);
		} else {
			unsigned int calib_source;
			unsigned int calib_source_adjust;

			calib_source = data[1] & 0xf;
			calib_source_adjust = (data[1] >> 4) & 0xff;

			if (calib_source >= 8)
				return -EINVAL;
			devpriv->ai_calib_source = calib_source;
			if (board->reg_type == ni_reg_611x) {
				ni_writeb(dev, calib_source_adjust,
					  Cal_Gain_Select_611x);
			}
		}
		return 2;
	default:
		break;
	}

	return -EINVAL;
}

/* munge data from unsigned to 2's complement for analog output bipolar modes */
static void ni_ao_munge(struct comedi_device *dev, struct comedi_subdevice *s,
			void *data, unsigned int num_bytes,
			unsigned int chan_index)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int length = num_bytes / sizeof(short);
	unsigned int offset = 1 << (board->aobits - 1);
	unsigned short *array = data;
	unsigned int range;
	unsigned int i;

	for (i = 0; i < length; i++) {
		range = CR_RANGE(cmd->chanlist[chan_index]);

		if (comedi_range_is_bipolar(s, range))
			array[i] -= offset;
#ifdef PCIDMA
		array[i] = cpu_to_le16(array[i]);
#endif
		chan_index++;
		chan_index %= cmd->chanlist_len;
	}
}

static int ni_m_series_ao_config_chanlist(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  unsigned int chanspec[],
					  unsigned int n_chans, int timed)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	unsigned int range;
	unsigned int chan;
	unsigned int conf;
	int i;
	int invert = 0;

	if (timed) {
		for (i = 0; i < board->n_aochan; ++i) {
			devpriv->ao_conf[i] &= ~MSeries_AO_Update_Timed_Bit;
			ni_writeb(dev, devpriv->ao_conf[i],
				  M_Offset_AO_Config_Bank(i));
			ni_writeb(dev, 0xf, M_Offset_AO_Waveform_Order(i));
		}
	}
	for (i = 0; i < n_chans; i++) {
		const struct comedi_krange *krange;
		chan = CR_CHAN(chanspec[i]);
		range = CR_RANGE(chanspec[i]);
		krange = s->range_table->range + range;
		invert = 0;
		conf = 0;
		switch (krange->max - krange->min) {
		case 20000000:
			conf |= MSeries_AO_DAC_Reference_10V_Internal_Bits;
			ni_writeb(dev, 0,
				  M_Offset_AO_Reference_Attenuation(chan));
			break;
		case 10000000:
			conf |= MSeries_AO_DAC_Reference_5V_Internal_Bits;
			ni_writeb(dev, 0,
				  M_Offset_AO_Reference_Attenuation(chan));
			break;
		case 4000000:
			conf |= MSeries_AO_DAC_Reference_10V_Internal_Bits;
			ni_writeb(dev, MSeries_Attenuate_x5_Bit,
				  M_Offset_AO_Reference_Attenuation(chan));
			break;
		case 2000000:
			conf |= MSeries_AO_DAC_Reference_5V_Internal_Bits;
			ni_writeb(dev, MSeries_Attenuate_x5_Bit,
				  M_Offset_AO_Reference_Attenuation(chan));
			break;
		default:
			printk("%s: bug! unhandled ao reference voltage\n",
			       __func__);
			break;
		}
		switch (krange->max + krange->min) {
		case 0:
			conf |= MSeries_AO_DAC_Offset_0V_Bits;
			break;
		case 10000000:
			conf |= MSeries_AO_DAC_Offset_5V_Bits;
			break;
		default:
			printk("%s: bug! unhandled ao offset voltage\n",
			       __func__);
			break;
		}
		if (timed)
			conf |= MSeries_AO_Update_Timed_Bit;
		ni_writeb(dev, conf, M_Offset_AO_Config_Bank(chan));
		devpriv->ao_conf[chan] = conf;
		ni_writeb(dev, i, M_Offset_AO_Waveform_Order(chan));
	}
	return invert;
}

static int ni_old_ao_config_chanlist(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     unsigned int chanspec[],
				     unsigned int n_chans)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	unsigned int range;
	unsigned int chan;
	unsigned int conf;
	int i;
	int invert = 0;

	for (i = 0; i < n_chans; i++) {
		chan = CR_CHAN(chanspec[i]);
		range = CR_RANGE(chanspec[i]);
		conf = AO_Channel(chan);

		if (comedi_range_is_bipolar(s, range)) {
			conf |= AO_Bipolar;
			invert = (1 << (board->aobits - 1));
		} else {
			invert = 0;
		}
		if (comedi_range_is_external(s, range))
			conf |= AO_Ext_Ref;

		/* not all boards can deglitch, but this shouldn't hurt */
		if (chanspec[i] & CR_DEGLITCH)
			conf |= AO_Deglitch;

		/* analog reference */
		/* AREF_OTHER connects AO ground to AI ground, i think */
		conf |= (CR_AREF(chanspec[i]) ==
			 AREF_OTHER) ? AO_Ground_Ref : 0;

		ni_writew(dev, conf, AO_Configuration);
		devpriv->ao_conf[chan] = conf;
	}
	return invert;
}

static int ni_ao_config_chanlist(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 unsigned int chanspec[], unsigned int n_chans,
				 int timed)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->is_m_series)
		return ni_m_series_ao_config_chanlist(dev, s, chanspec, n_chans,
						      timed);
	else
		return ni_old_ao_config_chanlist(dev, s, chanspec, n_chans);
}

static int ni_ao_insn_read(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	data[0] = devpriv->ao[CR_CHAN(insn->chanspec)];

	return 1;
}

static int ni_ao_insn_write(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	int reg;
	int i;

	if (devpriv->is_m_series)
		reg = M_Offset_DAC_Direct_Data(chan);
	else
		reg = (chan) ? DAC1_Direct_Data : DAC0_Direct_Data;

	ni_ao_config_chanlist(dev, s, &insn->chanspec, 1, 0);

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		devpriv->ao[chan] = val;

		if (devpriv->is_m_series) {
			/* M-series board always use offset binary values */
			ni_writew(dev, val, reg);
		} else {
			/*
			 * Non-M series boards need two's complement values
			 * for bipolar ranges.
			 */
			if (comedi_range_is_bipolar(s, range))
				val = comedi_offset_munge(s, val);

			ni_writew(dev, val, reg);
		}
	}

	return insn->n;
}

static int ni_ao_insn_write_671x(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	ni_ao_win_outw(dev, 1 << chan, AO_Immediate_671x);

	ni_ao_config_chanlist(dev, s, &insn->chanspec, 1, 0);

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		devpriv->ao[chan] = val;

		/*
		 * 671x boards have +/-10V outputs
		 * munge the unsigned comedi values to 2's complement
		 */
		val = comedi_offset_munge(s, val);

		ni_ao_win_outw(dev, val, DACx_Direct_Data_671x(chan));
	}

	return insn->n;
}

static int ni_ao_insn_config(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;

	switch (data[0]) {
	case INSN_CONFIG_GET_HARDWARE_BUFFER_SIZE:
		switch (data[1]) {
		case COMEDI_OUTPUT:
			data[2] = 1 + board->ao_fifo_depth * sizeof(short);
			if (devpriv->mite)
				data[2] += devpriv->mite->fifo_size;
			break;
		case COMEDI_INPUT:
			data[2] = 0;
			break;
		default:
			return -EINVAL;
			break;
		}
		return 0;
	default:
		break;
	}

	return -EINVAL;
}

static int ni_ao_inttrig(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 unsigned int trig_num)
{
	const struct ni_board_struct *board __maybe_unused = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret;
	int interrupt_b_bits;
	int i;
	static const int timeout = 1000;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	/* Null trig at beginning prevent ao start trigger from executing more than
	   once per command (and doing things like trying to allocate the ao dma channel
	   multiple times) */
	s->async->inttrig = NULL;

	ni_set_bits(dev, Interrupt_B_Enable_Register,
		    AO_FIFO_Interrupt_Enable | AO_Error_Interrupt_Enable, 0);
	interrupt_b_bits = AO_Error_Interrupt_Enable;
#ifdef PCIDMA
	ni_stc_writew(dev, 1, DAC_FIFO_Clear);
	if (board->reg_type & ni_reg_6xxx_mask)
		ni_ao_win_outl(dev, 0x6, AO_FIFO_Offset_Load_611x);
	ret = ni_ao_setup_MITE_dma(dev);
	if (ret)
		return ret;
	ret = ni_ao_wait_for_dma_load(dev);
	if (ret < 0)
		return ret;
#else
	ret = ni_ao_prep_fifo(dev, s);
	if (ret == 0)
		return -EPIPE;

	interrupt_b_bits |= AO_FIFO_Interrupt_Enable;
#endif

	ni_stc_writew(dev, devpriv->ao_mode3 | AO_Not_An_UPDATE,
		      AO_Mode_3_Register);
	ni_stc_writew(dev, devpriv->ao_mode3, AO_Mode_3_Register);
	/* wait for DACs to be loaded */
	for (i = 0; i < timeout; i++) {
		udelay(1);
		if ((ni_stc_readw(dev, Joint_Status_2_Register) &
		     AO_TMRDACWRs_In_Progress_St) == 0)
			break;
	}
	if (i == timeout) {
		comedi_error(dev,
			     "timed out waiting for AO_TMRDACWRs_In_Progress_St to clear");
		return -EIO;
	}
	/*
	 * stc manual says we are need to clear error interrupt after
	 * AO_TMRDACWRs_In_Progress_St clears
	 */
	ni_stc_writew(dev, AO_Error_Interrupt_Ack, Interrupt_B_Ack_Register);

	ni_set_bits(dev, Interrupt_B_Enable_Register, interrupt_b_bits, 1);

	ni_stc_writew(dev, devpriv->ao_cmd1 |
		      AO_UI_Arm | AO_UC_Arm | AO_BC_Arm |
		      AO_DAC1_Update_Mode | AO_DAC0_Update_Mode,
		      AO_Command_1_Register);

	ni_stc_writew(dev, devpriv->ao_cmd2 | AO_START1_Pulse,
		      AO_Command_2_Register);

	return 0;
}

static int ni_ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	const struct comedi_cmd *cmd = &s->async->cmd;
	int bits;
	int i;
	unsigned trigvar;

	if (dev->irq == 0) {
		comedi_error(dev, "cannot run command without an irq");
		return -EIO;
	}

	ni_stc_writew(dev, AO_Configuration_Start, Joint_Reset_Register);

	ni_stc_writew(dev, AO_Disarm, AO_Command_1_Register);

	if (board->reg_type & ni_reg_6xxx_mask) {
		ni_ao_win_outw(dev, CLEAR_WG, AO_Misc_611x);

		bits = 0;
		for (i = 0; i < cmd->chanlist_len; i++) {
			int chan;

			chan = CR_CHAN(cmd->chanlist[i]);
			bits |= 1 << chan;
			ni_ao_win_outw(dev, chan, AO_Waveform_Generation_611x);
		}
		ni_ao_win_outw(dev, bits, AO_Timed_611x);
	}

	ni_ao_config_chanlist(dev, s, cmd->chanlist, cmd->chanlist_len, 1);

	if (cmd->stop_src == TRIG_NONE) {
		devpriv->ao_mode1 |= AO_Continuous;
		devpriv->ao_mode1 &= ~AO_Trigger_Once;
	} else {
		devpriv->ao_mode1 &= ~AO_Continuous;
		devpriv->ao_mode1 |= AO_Trigger_Once;
	}
	ni_stc_writew(dev, devpriv->ao_mode1, AO_Mode_1_Register);
	switch (cmd->start_src) {
	case TRIG_INT:
	case TRIG_NOW:
		devpriv->ao_trigger_select &=
		    ~(AO_START1_Polarity | AO_START1_Select(-1));
		devpriv->ao_trigger_select |= AO_START1_Edge | AO_START1_Sync;
		ni_stc_writew(dev, devpriv->ao_trigger_select,
			      AO_Trigger_Select_Register);
		break;
	case TRIG_EXT:
		devpriv->ao_trigger_select =
		    AO_START1_Select(CR_CHAN(cmd->start_arg) + 1);
		if (cmd->start_arg & CR_INVERT)
			devpriv->ao_trigger_select |= AO_START1_Polarity;	/*  0=active high, 1=active low. see daq-stc 3-24 (p186) */
		if (cmd->start_arg & CR_EDGE)
			devpriv->ao_trigger_select |= AO_START1_Edge;	/*  0=edge detection disabled, 1=enabled */
		ni_stc_writew(dev, devpriv->ao_trigger_select,
			      AO_Trigger_Select_Register);
		break;
	default:
		BUG();
		break;
	}
	devpriv->ao_mode3 &= ~AO_Trigger_Length;
	ni_stc_writew(dev, devpriv->ao_mode3, AO_Mode_3_Register);

	ni_stc_writew(dev, devpriv->ao_mode1, AO_Mode_1_Register);
	devpriv->ao_mode2 &= ~AO_BC_Initial_Load_Source;
	ni_stc_writew(dev, devpriv->ao_mode2, AO_Mode_2_Register);
	if (cmd->stop_src == TRIG_NONE)
		ni_stc_writel(dev, 0xffffff, AO_BC_Load_A_Register);
	else
		ni_stc_writel(dev, 0, AO_BC_Load_A_Register);
	ni_stc_writew(dev, AO_BC_Load, AO_Command_1_Register);
	devpriv->ao_mode2 &= ~AO_UC_Initial_Load_Source;
	ni_stc_writew(dev, devpriv->ao_mode2, AO_Mode_2_Register);
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		if (devpriv->is_m_series) {
			/*  this is how the NI example code does it for m-series boards, verified correct with 6259 */
			ni_stc_writel(dev, cmd->stop_arg - 1,
				      AO_UC_Load_A_Register);
			ni_stc_writew(dev, AO_UC_Load, AO_Command_1_Register);
		} else {
			ni_stc_writel(dev, cmd->stop_arg,
				      AO_UC_Load_A_Register);
			ni_stc_writew(dev, AO_UC_Load, AO_Command_1_Register);
			ni_stc_writel(dev, cmd->stop_arg - 1,
				      AO_UC_Load_A_Register);
		}
		break;
	case TRIG_NONE:
		ni_stc_writel(dev, 0xffffff, AO_UC_Load_A_Register);
		ni_stc_writew(dev, AO_UC_Load, AO_Command_1_Register);
		ni_stc_writel(dev, 0xffffff, AO_UC_Load_A_Register);
		break;
	default:
		ni_stc_writel(dev, 0, AO_UC_Load_A_Register);
		ni_stc_writew(dev, AO_UC_Load, AO_Command_1_Register);
		ni_stc_writel(dev, cmd->stop_arg, AO_UC_Load_A_Register);
	}

	devpriv->ao_mode1 &=
	    ~(AO_UI_Source_Select(0x1f) | AO_UI_Source_Polarity |
	      AO_UPDATE_Source_Select(0x1f) | AO_UPDATE_Source_Polarity);
	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		devpriv->ao_cmd2 &= ~AO_BC_Gate_Enable;
		trigvar =
		    ni_ns_to_timer(dev, cmd->scan_begin_arg,
				   TRIG_ROUND_NEAREST);
		ni_stc_writel(dev, 1, AO_UI_Load_A_Register);
		ni_stc_writew(dev, AO_UI_Load, AO_Command_1_Register);
		ni_stc_writel(dev, trigvar, AO_UI_Load_A_Register);
		break;
	case TRIG_EXT:
		devpriv->ao_mode1 |=
		    AO_UPDATE_Source_Select(cmd->scan_begin_arg);
		if (cmd->scan_begin_arg & CR_INVERT)
			devpriv->ao_mode1 |= AO_UPDATE_Source_Polarity;
		devpriv->ao_cmd2 |= AO_BC_Gate_Enable;
		break;
	default:
		BUG();
		break;
	}
	ni_stc_writew(dev, devpriv->ao_cmd2, AO_Command_2_Register);
	ni_stc_writew(dev, devpriv->ao_mode1, AO_Mode_1_Register);
	devpriv->ao_mode2 &=
	    ~(AO_UI_Reload_Mode(3) | AO_UI_Initial_Load_Source);
	ni_stc_writew(dev, devpriv->ao_mode2, AO_Mode_2_Register);

	if (cmd->scan_end_arg > 1) {
		devpriv->ao_mode1 |= AO_Multiple_Channels;
		ni_stc_writew(dev,
			      AO_Number_Of_Channels(cmd->scan_end_arg - 1) |
			      AO_UPDATE_Output_Select(AO_Update_Output_High_Z),
			      AO_Output_Control_Register);
	} else {
		unsigned bits;
		devpriv->ao_mode1 &= ~AO_Multiple_Channels;
		bits = AO_UPDATE_Output_Select(AO_Update_Output_High_Z);
		if (devpriv->is_m_series ||
		    board->reg_type & ni_reg_6xxx_mask) {
			bits |= AO_Number_Of_Channels(0);
		} else {
			bits |=
			    AO_Number_Of_Channels(CR_CHAN(cmd->chanlist[0]));
		}
		ni_stc_writew(dev, bits, AO_Output_Control_Register);
	}
	ni_stc_writew(dev, devpriv->ao_mode1, AO_Mode_1_Register);

	ni_stc_writew(dev, AO_DAC0_Update_Mode | AO_DAC1_Update_Mode,
		      AO_Command_1_Register);

	devpriv->ao_mode3 |= AO_Stop_On_Overrun_Error;
	ni_stc_writew(dev, devpriv->ao_mode3, AO_Mode_3_Register);

	devpriv->ao_mode2 &= ~AO_FIFO_Mode_Mask;
#ifdef PCIDMA
	devpriv->ao_mode2 |= AO_FIFO_Mode_HF_to_F;
#else
	devpriv->ao_mode2 |= AO_FIFO_Mode_HF;
#endif
	devpriv->ao_mode2 &= ~AO_FIFO_Retransmit_Enable;
	ni_stc_writew(dev, devpriv->ao_mode2, AO_Mode_2_Register);

	bits = AO_BC_Source_Select | AO_UPDATE_Pulse_Width |
	    AO_TMRDACWR_Pulse_Width;
	if (board->ao_fifo_depth)
		bits |= AO_FIFO_Enable;
	else
		bits |= AO_DMA_PIO_Control;
#if 0
	/* F Hess: windows driver does not set AO_Number_Of_DAC_Packages bit for 6281,
	   verified with bus analyzer. */
	if (devpriv->is_m_series)
		bits |= AO_Number_Of_DAC_Packages;
#endif
	ni_stc_writew(dev, bits, AO_Personal_Register);
	/*  enable sending of ao dma requests */
	ni_stc_writew(dev, AO_AOFREQ_Enable, AO_Start_Select_Register);

	ni_stc_writew(dev, AO_Configuration_End, Joint_Reset_Register);

	if (cmd->stop_src == TRIG_COUNT) {
		ni_stc_writew(dev, AO_BC_TC_Interrupt_Ack,
			      Interrupt_B_Ack_Register);
		ni_set_bits(dev, Interrupt_B_Enable_Register,
			    AO_BC_TC_Interrupt_Enable, 1);
	}

	s->async->inttrig = ni_ao_inttrig;

	return 0;
}

static int ni_ao_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_cmd *cmd)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	int err = 0;
	unsigned int tmp;

	/* Step 1 : check if triggers are trivially valid */

	if ((cmd->flags & CMDF_WRITE) == 0)
		cmd->flags |= CMDF_WRITE;

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_INT | TRIG_EXT);
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

	switch (cmd->start_src) {
	case TRIG_INT:
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		tmp = CR_CHAN(cmd->start_arg);

		if (tmp > 18)
			tmp = 18;
		tmp |= (cmd->start_arg & (CR_INVERT | CR_EDGE));
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, tmp);
		break;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 board->ao_speed);
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg,
						 devpriv->clock_ns * 0xffffff);
	}

	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_max(&cmd->stop_arg, 0x00ffffff);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		cmd->scan_begin_arg =
		    ni_timer_to_ns(dev, ni_ns_to_timer(dev,
						       cmd->scan_begin_arg,
						       cmd->
						       flags &
						       TRIG_ROUND_MASK));
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (err)
		return 4;

	return 0;
}

static int ni_ao_reset(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;

	ni_release_ao_mite_channel(dev);

	ni_stc_writew(dev, AO_Configuration_Start, Joint_Reset_Register);
	ni_stc_writew(dev, AO_Disarm, AO_Command_1_Register);
	ni_set_bits(dev, Interrupt_B_Enable_Register, ~0, 0);
	ni_stc_writew(dev, AO_BC_Source_Select, AO_Personal_Register);
	ni_stc_writew(dev, 0x3f98, Interrupt_B_Ack_Register);
	ni_stc_writew(dev, AO_BC_Source_Select | AO_UPDATE_Pulse_Width |
		      AO_TMRDACWR_Pulse_Width, AO_Personal_Register);
	ni_stc_writew(dev, 0, AO_Output_Control_Register);
	ni_stc_writew(dev, 0, AO_Start_Select_Register);
	devpriv->ao_cmd1 = 0;
	ni_stc_writew(dev, devpriv->ao_cmd1, AO_Command_1_Register);
	devpriv->ao_cmd2 = 0;
	ni_stc_writew(dev, devpriv->ao_cmd2, AO_Command_2_Register);
	devpriv->ao_mode1 = 0;
	ni_stc_writew(dev, devpriv->ao_mode1, AO_Mode_1_Register);
	devpriv->ao_mode2 = 0;
	ni_stc_writew(dev, devpriv->ao_mode2, AO_Mode_2_Register);
	if (devpriv->is_m_series)
		devpriv->ao_mode3 = AO_Last_Gate_Disable;
	else
		devpriv->ao_mode3 = 0;
	ni_stc_writew(dev, devpriv->ao_mode3, AO_Mode_3_Register);
	devpriv->ao_trigger_select = 0;
	ni_stc_writew(dev, devpriv->ao_trigger_select,
		      AO_Trigger_Select_Register);
	if (board->reg_type & ni_reg_6xxx_mask) {
		unsigned immediate_bits = 0;
		unsigned i;
		for (i = 0; i < s->n_chan; ++i)
			immediate_bits |= 1 << i;
		ni_ao_win_outw(dev, immediate_bits, AO_Immediate_671x);
		ni_ao_win_outw(dev, CLEAR_WG, AO_Misc_611x);
	}
	ni_stc_writew(dev, AO_Configuration_End, Joint_Reset_Register);

	return 0;
}

/* digital io */

static int ni_dio_insn_config(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	devpriv->dio_control &= ~DIO_Pins_Dir_Mask;
	devpriv->dio_control |= DIO_Pins_Dir(s->io_bits);
	ni_stc_writew(dev, devpriv->dio_control, DIO_Control_Register);

	return insn->n;
}

static int ni_dio_insn_bits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	/* Make sure we're not using the serial part of the dio */
	if ((data[0] & (DIO_SDIN | DIO_SDOUT)) && devpriv->serial_interval_ns)
		return -EBUSY;

	if (comedi_dio_update_state(s, data)) {
		devpriv->dio_output &= ~DIO_Parallel_Data_Mask;
		devpriv->dio_output |= DIO_Parallel_Data_Out(s->state);
		ni_stc_writew(dev, devpriv->dio_output, DIO_Output_Register);
	}

	data[1] = ni_stc_readw(dev, DIO_Parallel_Input_Register);

	return insn->n;
}

static int ni_m_series_dio_insn_config(struct comedi_device *dev,
				       struct comedi_subdevice *s,
				       struct comedi_insn *insn,
				       unsigned int *data)
{
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	ni_writel(dev, s->io_bits, M_Offset_DIO_Direction);

	return insn->n;
}

static int ni_m_series_dio_insn_bits(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		ni_writel(dev, s->state, M_Offset_Static_Digital_Output);

	data[1] = ni_readl(dev, M_Offset_Static_Digital_Input);

	return insn->n;
}

static int ni_cdio_check_chanlist(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_cmd *cmd)
{
	int i;

	for (i = 0; i < cmd->chanlist_len; ++i) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);

		if (chan != i)
			return -EINVAL;
	}

	return 0;
}

static int ni_cdio_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	tmp = cmd->scan_begin_arg;
	tmp &= CR_PACK_FLAGS(CDO_Sample_Source_Select_Mask, 0, 0, CR_INVERT);
	if (tmp != cmd->scan_begin_arg)
		err |= -EINVAL;

	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);
	err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= ni_cdio_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int ni_cdo_inttrig(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  unsigned int trig_num)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	const unsigned timeout = 1000;
	int retval = 0;
	unsigned i;
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
	unsigned long flags;
#endif

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	s->async->inttrig = NULL;

	/* read alloc the entire buffer */
	comedi_buf_read_alloc(s, s->async->prealloc_bufsz);

#ifdef PCIDMA
	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->cdo_mite_chan) {
		mite_prep_dma(devpriv->cdo_mite_chan, 32, 32);
		mite_dma_arm(devpriv->cdo_mite_chan);
	} else {
		comedi_error(dev, "BUG: no cdo mite channel?");
		retval = -EIO;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	if (retval < 0)
		return retval;
#endif
/*
* XXX not sure what interrupt C group does
* ni_writeb(dev, Interrupt_Group_C_Enable_Bit,
* M_Offset_Interrupt_C_Enable); wait for dma to fill output fifo
*/
	for (i = 0; i < timeout; ++i) {
		if (ni_readl(dev, M_Offset_CDIO_Status) & CDO_FIFO_Full_Bit)
			break;
		udelay(10);
	}
	if (i == timeout) {
		comedi_error(dev, "dma failed to fill cdo fifo!");
		s->cancel(dev, s);
		return -EIO;
	}
	ni_writel(dev, CDO_Arm_Bit | CDO_Error_Interrupt_Enable_Set_Bit |
		       CDO_Empty_FIFO_Interrupt_Enable_Set_Bit,
		  M_Offset_CDIO_Command);
	return retval;
}

static int ni_cdio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct comedi_cmd *cmd = &s->async->cmd;
	unsigned cdo_mode_bits = CDO_FIFO_Mode_Bit | CDO_Halt_On_Error_Bit;
	int retval;

	ni_writel(dev, CDO_Reset_Bit, M_Offset_CDIO_Command);
	switch (cmd->scan_begin_src) {
	case TRIG_EXT:
		cdo_mode_bits |=
		    CR_CHAN(cmd->scan_begin_arg) &
		    CDO_Sample_Source_Select_Mask;
		break;
	default:
		BUG();
		break;
	}
	if (cmd->scan_begin_arg & CR_INVERT)
		cdo_mode_bits |= CDO_Polarity_Bit;
	ni_writel(dev, cdo_mode_bits, M_Offset_CDO_Mode);
	if (s->io_bits) {
		ni_writel(dev, s->state, M_Offset_CDO_FIFO_Data);
		ni_writel(dev, CDO_SW_Update_Bit, M_Offset_CDIO_Command);
		ni_writel(dev, s->io_bits, M_Offset_CDO_Mask_Enable);
	} else {
		comedi_error(dev,
			     "attempted to run digital output command with no lines configured as outputs");
		return -EIO;
	}
	retval = ni_request_cdo_mite_channel(dev);
	if (retval < 0)
		return retval;

	s->async->inttrig = ni_cdo_inttrig;

	return 0;
}

static int ni_cdio_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	ni_writel(dev, CDO_Disarm_Bit | CDO_Error_Interrupt_Enable_Clear_Bit |
		       CDO_Empty_FIFO_Interrupt_Enable_Clear_Bit |
		       CDO_FIFO_Request_Interrupt_Enable_Clear_Bit,
		  M_Offset_CDIO_Command);
/*
* XXX not sure what interrupt C group does ni_writeb(dev, 0,
* M_Offset_Interrupt_C_Enable);
*/
	ni_writel(dev, 0, M_Offset_CDO_Mask_Enable);
	ni_release_cdo_mite_channel(dev);
	return 0;
}

static void handle_cdio_interrupt(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	unsigned cdio_status;
	struct comedi_subdevice *s = &dev->subdevices[NI_DIO_SUBDEV];
#ifdef PCIDMA
	unsigned long flags;
#endif

	if (!devpriv->is_m_series)
		return;
#ifdef PCIDMA
	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->cdo_mite_chan) {
		unsigned cdo_mite_status =
		    mite_get_status(devpriv->cdo_mite_chan);
		if (cdo_mite_status & CHSR_LINKC) {
			writel(CHOR_CLRLC,
			       devpriv->mite->mite_io_addr +
			       MITE_CHOR(devpriv->cdo_mite_chan->channel));
		}
		mite_sync_output_dma(devpriv->cdo_mite_chan, s);
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
#endif

	cdio_status = ni_readl(dev, M_Offset_CDIO_Status);
	if (cdio_status & (CDO_Overrun_Bit | CDO_Underflow_Bit)) {
		/* printk("cdio error: statux=0x%x\n", cdio_status); */
		/* XXX just guessing this is needed and does something useful */
		ni_writel(dev, CDO_Error_Interrupt_Confirm_Bit,
			  M_Offset_CDIO_Command);
		s->async->events |= COMEDI_CB_OVERFLOW;
	}
	if (cdio_status & CDO_FIFO_Empty_Bit) {
		/* printk("cdio fifo empty\n"); */
		ni_writel(dev, CDO_Empty_FIFO_Interrupt_Enable_Clear_Bit,
			  M_Offset_CDIO_Command);
		/* s->async->events |= COMEDI_CB_EOA; */
	}
	cfc_handle_events(dev, s);
}

static int ni_serial_hw_readwrite8(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned char data_out,
				   unsigned char *data_in)
{
	struct ni_private *devpriv = dev->private;
	unsigned int status1;
	int err = 0, count = 20;

	devpriv->dio_output &= ~DIO_Serial_Data_Mask;
	devpriv->dio_output |= DIO_Serial_Data_Out(data_out);
	ni_stc_writew(dev, devpriv->dio_output, DIO_Output_Register);

	status1 = ni_stc_readw(dev, Joint_Status_1_Register);
	if (status1 & DIO_Serial_IO_In_Progress_St) {
		err = -EBUSY;
		goto Error;
	}

	devpriv->dio_control |= DIO_HW_Serial_Start;
	ni_stc_writew(dev, devpriv->dio_control, DIO_Control_Register);
	devpriv->dio_control &= ~DIO_HW_Serial_Start;

	/* Wait until STC says we're done, but don't loop infinitely. */
	while ((status1 = ni_stc_readw(dev, Joint_Status_1_Register)) &
	       DIO_Serial_IO_In_Progress_St) {
		/* Delay one bit per loop */
		udelay((devpriv->serial_interval_ns + 999) / 1000);
		if (--count < 0) {
			printk
			    ("ni_serial_hw_readwrite8: SPI serial I/O didn't finish in time!\n");
			err = -ETIME;
			goto Error;
		}
	}

	/* Delay for last bit. This delay is absolutely necessary, because
	   DIO_Serial_IO_In_Progress_St goes high one bit too early. */
	udelay((devpriv->serial_interval_ns + 999) / 1000);

	if (data_in != NULL)
		*data_in = ni_stc_readw(dev, DIO_Serial_Input_Register);

Error:
	ni_stc_writew(dev, devpriv->dio_control, DIO_Control_Register);

	return err;
}

static int ni_serial_sw_readwrite8(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned char data_out,
				   unsigned char *data_in)
{
	struct ni_private *devpriv = dev->private;
	unsigned char mask, input = 0;

	/* Wait for one bit before transfer */
	udelay((devpriv->serial_interval_ns + 999) / 1000);

	for (mask = 0x80; mask; mask >>= 1) {
		/* Output current bit; note that we cannot touch s->state
		   because it is a per-subdevice field, and serial is
		   a separate subdevice from DIO. */
		devpriv->dio_output &= ~DIO_SDOUT;
		if (data_out & mask)
			devpriv->dio_output |= DIO_SDOUT;
		ni_stc_writew(dev, devpriv->dio_output, DIO_Output_Register);

		/* Assert SDCLK (active low, inverted), wait for half of
		   the delay, deassert SDCLK, and wait for the other half. */
		devpriv->dio_control |= DIO_Software_Serial_Control;
		ni_stc_writew(dev, devpriv->dio_control, DIO_Control_Register);

		udelay((devpriv->serial_interval_ns + 999) / 2000);

		devpriv->dio_control &= ~DIO_Software_Serial_Control;
		ni_stc_writew(dev, devpriv->dio_control, DIO_Control_Register);

		udelay((devpriv->serial_interval_ns + 999) / 2000);

		/* Input current bit */
		if (ni_stc_readw(dev, DIO_Parallel_Input_Register) & DIO_SDIN) {
			/* printk("DIO_P_I_R: 0x%x\n", ni_stc_readw(dev, DIO_Parallel_Input_Register)); */
			input |= mask;
		}
	}

	if (data_in)
		*data_in = input;

	return 0;
}

static int ni_serial_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	int err = insn->n;
	unsigned char byte_out, byte_in = 0;

	if (insn->n != 2)
		return -EINVAL;

	switch (data[0]) {
	case INSN_CONFIG_SERIAL_CLOCK:
		devpriv->serial_hw_mode = 1;
		devpriv->dio_control |= DIO_HW_Serial_Enable;

		if (data[1] == SERIAL_DISABLED) {
			devpriv->serial_hw_mode = 0;
			devpriv->dio_control &= ~(DIO_HW_Serial_Enable |
						  DIO_Software_Serial_Control);
			data[1] = SERIAL_DISABLED;
			devpriv->serial_interval_ns = data[1];
		} else if (data[1] <= SERIAL_600NS) {
			/* Warning: this clock speed is too fast to reliably
			   control SCXI. */
			devpriv->dio_control &= ~DIO_HW_Serial_Timebase;
			devpriv->clock_and_fout |= Slow_Internal_Timebase;
			devpriv->clock_and_fout &= ~DIO_Serial_Out_Divide_By_2;
			data[1] = SERIAL_600NS;
			devpriv->serial_interval_ns = data[1];
		} else if (data[1] <= SERIAL_1_2US) {
			devpriv->dio_control &= ~DIO_HW_Serial_Timebase;
			devpriv->clock_and_fout |= Slow_Internal_Timebase |
			    DIO_Serial_Out_Divide_By_2;
			data[1] = SERIAL_1_2US;
			devpriv->serial_interval_ns = data[1];
		} else if (data[1] <= SERIAL_10US) {
			devpriv->dio_control |= DIO_HW_Serial_Timebase;
			devpriv->clock_and_fout |= Slow_Internal_Timebase |
			    DIO_Serial_Out_Divide_By_2;
			/* Note: DIO_Serial_Out_Divide_By_2 only affects
			   600ns/1.2us. If you turn divide_by_2 off with the
			   slow clock, you will still get 10us, except then
			   all your delays are wrong. */
			data[1] = SERIAL_10US;
			devpriv->serial_interval_ns = data[1];
		} else {
			devpriv->dio_control &= ~(DIO_HW_Serial_Enable |
						  DIO_Software_Serial_Control);
			devpriv->serial_hw_mode = 0;
			data[1] = (data[1] / 1000) * 1000;
			devpriv->serial_interval_ns = data[1];
		}

		ni_stc_writew(dev, devpriv->dio_control, DIO_Control_Register);
		ni_stc_writew(dev, devpriv->clock_and_fout,
			      Clock_and_FOUT_Register);
		return 1;

		break;

	case INSN_CONFIG_BIDIRECTIONAL_DATA:

		if (devpriv->serial_interval_ns == 0)
			return -EINVAL;

		byte_out = data[1] & 0xFF;

		if (devpriv->serial_hw_mode) {
			err = ni_serial_hw_readwrite8(dev, s, byte_out,
						      &byte_in);
		} else if (devpriv->serial_interval_ns > 0) {
			err = ni_serial_sw_readwrite8(dev, s, byte_out,
						      &byte_in);
		} else {
			printk("ni_serial_insn_config: serial disabled!\n");
			return -EINVAL;
		}
		if (err < 0)
			return err;
		data[1] = byte_in & 0xFF;
		return insn->n;

		break;
	default:
		return -EINVAL;
	}

}

static void init_ao_67xx(struct comedi_device *dev, struct comedi_subdevice *s)
{
	int i;

	for (i = 0; i < s->n_chan; i++) {
		ni_ao_win_outw(dev, AO_Channel(i) | 0x0,
			       AO_Configuration_2_67xx);
	}
	ni_ao_win_outw(dev, 0x0, AO_Later_Single_Point_Updates);
}

static unsigned ni_gpct_to_stc_register(enum ni_gpct_register reg)
{
	unsigned stc_register;
	switch (reg) {
	case NITIO_G0_AUTO_INC:
		stc_register = G_Autoincrement_Register(0);
		break;
	case NITIO_G1_AUTO_INC:
		stc_register = G_Autoincrement_Register(1);
		break;
	case NITIO_G0_CMD:
		stc_register = G_Command_Register(0);
		break;
	case NITIO_G1_CMD:
		stc_register = G_Command_Register(1);
		break;
	case NITIO_G0_HW_SAVE:
		stc_register = G_HW_Save_Register(0);
		break;
	case NITIO_G1_HW_SAVE:
		stc_register = G_HW_Save_Register(1);
		break;
	case NITIO_G0_SW_SAVE:
		stc_register = G_Save_Register(0);
		break;
	case NITIO_G1_SW_SAVE:
		stc_register = G_Save_Register(1);
		break;
	case NITIO_G0_MODE:
		stc_register = G_Mode_Register(0);
		break;
	case NITIO_G1_MODE:
		stc_register = G_Mode_Register(1);
		break;
	case NITIO_G0_LOADA:
		stc_register = G_Load_A_Register(0);
		break;
	case NITIO_G1_LOADA:
		stc_register = G_Load_A_Register(1);
		break;
	case NITIO_G0_LOADB:
		stc_register = G_Load_B_Register(0);
		break;
	case NITIO_G1_LOADB:
		stc_register = G_Load_B_Register(1);
		break;
	case NITIO_G0_INPUT_SEL:
		stc_register = G_Input_Select_Register(0);
		break;
	case NITIO_G1_INPUT_SEL:
		stc_register = G_Input_Select_Register(1);
		break;
	case NITIO_G01_STATUS:
		stc_register = G_Status_Register;
		break;
	case NITIO_G01_RESET:
		stc_register = Joint_Reset_Register;
		break;
	case NITIO_G01_STATUS1:
		stc_register = Joint_Status_1_Register;
		break;
	case NITIO_G01_STATUS2:
		stc_register = Joint_Status_2_Register;
		break;
	case NITIO_G0_INT_ACK:
		stc_register = Interrupt_A_Ack_Register;
		break;
	case NITIO_G1_INT_ACK:
		stc_register = Interrupt_B_Ack_Register;
		break;
	case NITIO_G0_STATUS:
		stc_register = AI_Status_1_Register;
		break;
	case NITIO_G1_STATUS:
		stc_register = AO_Status_1_Register;
		break;
	case NITIO_G0_INT_ENA:
		stc_register = Interrupt_A_Enable_Register;
		break;
	case NITIO_G1_INT_ENA:
		stc_register = Interrupt_B_Enable_Register;
		break;
	default:
		printk("%s: unhandled register 0x%x in switch.\n",
		       __func__, reg);
		BUG();
		return 0;
		break;
	}
	return stc_register;
}

static void ni_gpct_write_register(struct ni_gpct *counter, unsigned bits,
				   enum ni_gpct_register reg)
{
	struct comedi_device *dev = counter->counter_dev->dev;
	unsigned stc_register;
	/* bits in the join reset register which are relevant to counters */
	static const unsigned gpct_joint_reset_mask = G0_Reset | G1_Reset;
	static const unsigned gpct_interrupt_a_enable_mask =
	    G0_Gate_Interrupt_Enable | G0_TC_Interrupt_Enable;
	static const unsigned gpct_interrupt_b_enable_mask =
	    G1_Gate_Interrupt_Enable | G1_TC_Interrupt_Enable;

	switch (reg) {
		/* m-series-only registers */
	case NITIO_G0_CNT_MODE:
		ni_writew(dev, bits, M_Offset_G0_Counting_Mode);
		break;
	case NITIO_G1_CNT_MODE:
		ni_writew(dev, bits, M_Offset_G1_Counting_Mode);
		break;
	case NITIO_G0_GATE2:
		ni_writew(dev, bits, M_Offset_G0_Second_Gate);
		break;
	case NITIO_G1_GATE2:
		ni_writew(dev, bits, M_Offset_G1_Second_Gate);
		break;
	case NITIO_G0_DMA_CFG:
		ni_writew(dev, bits, M_Offset_G0_DMA_Config);
		break;
	case NITIO_G1_DMA_CFG:
		ni_writew(dev, bits, M_Offset_G1_DMA_Config);
		break;
	case NITIO_G0_ABZ:
		ni_writew(dev, bits, M_Offset_G0_MSeries_ABZ);
		break;
	case NITIO_G1_ABZ:
		ni_writew(dev, bits, M_Offset_G1_MSeries_ABZ);
		break;

		/* 32 bit registers */
	case NITIO_G0_LOADA:
	case NITIO_G1_LOADA:
	case NITIO_G0_LOADB:
	case NITIO_G1_LOADB:
		stc_register = ni_gpct_to_stc_register(reg);
		ni_stc_writel(dev, bits, stc_register);
		break;

		/* 16 bit registers */
	case NITIO_G0_INT_ENA:
		BUG_ON(bits & ~gpct_interrupt_a_enable_mask);
		ni_set_bitfield(dev, Interrupt_A_Enable_Register,
				gpct_interrupt_a_enable_mask, bits);
		break;
	case NITIO_G1_INT_ENA:
		BUG_ON(bits & ~gpct_interrupt_b_enable_mask);
		ni_set_bitfield(dev, Interrupt_B_Enable_Register,
				gpct_interrupt_b_enable_mask, bits);
		break;
	case NITIO_G01_RESET:
		BUG_ON(bits & ~gpct_joint_reset_mask);
		/* fall-through */
	default:
		stc_register = ni_gpct_to_stc_register(reg);
		ni_stc_writew(dev, bits, stc_register);
	}
}

static unsigned ni_gpct_read_register(struct ni_gpct *counter,
				      enum ni_gpct_register reg)
{
	struct comedi_device *dev = counter->counter_dev->dev;
	unsigned stc_register;

	switch (reg) {
		/* m-series only registers */
	case NITIO_G0_DMA_STATUS:
		return ni_readw(dev, M_Offset_G0_DMA_Status);
	case NITIO_G1_DMA_STATUS:
		return ni_readw(dev, M_Offset_G1_DMA_Status);

		/* 32 bit registers */
	case NITIO_G0_HW_SAVE:
	case NITIO_G1_HW_SAVE:
	case NITIO_G0_SW_SAVE:
	case NITIO_G1_SW_SAVE:
		stc_register = ni_gpct_to_stc_register(reg);
		return ni_stc_readl(dev, stc_register);

		/* 16 bit registers */
	default:
		stc_register = ni_gpct_to_stc_register(reg);
		return ni_stc_readw(dev, stc_register);
		break;
	}
	return 0;
}

static int ni_freq_out_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned int val = devpriv->clock_and_fout & FOUT_Divider_mask;
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = val;

	return insn->n;
}

static int ni_freq_out_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	if (insn->n) {
		devpriv->clock_and_fout &= ~FOUT_Enable;
		ni_stc_writew(dev, devpriv->clock_and_fout,
			      Clock_and_FOUT_Register);
		devpriv->clock_and_fout &= ~FOUT_Divider_mask;

		/* use the last data value to set the fout divider */
		devpriv->clock_and_fout |= FOUT_Divider(data[insn->n - 1]);

		devpriv->clock_and_fout |= FOUT_Enable;
		ni_stc_writew(dev, devpriv->clock_and_fout,
			      Clock_and_FOUT_Register);
	}
	return insn->n;
}

static int ni_freq_out_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	switch (data[0]) {
	case INSN_CONFIG_SET_CLOCK_SRC:
		switch (data[1]) {
		case NI_FREQ_OUT_TIMEBASE_1_DIV_2_CLOCK_SRC:
			devpriv->clock_and_fout &= ~FOUT_Timebase_Select;
			break;
		case NI_FREQ_OUT_TIMEBASE_2_CLOCK_SRC:
			devpriv->clock_and_fout |= FOUT_Timebase_Select;
			break;
		default:
			return -EINVAL;
		}
		ni_stc_writew(dev, devpriv->clock_and_fout,
			      Clock_and_FOUT_Register);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		if (devpriv->clock_and_fout & FOUT_Timebase_Select) {
			data[1] = NI_FREQ_OUT_TIMEBASE_2_CLOCK_SRC;
			data[2] = TIMEBASE_2_NS;
		} else {
			data[1] = NI_FREQ_OUT_TIMEBASE_1_DIV_2_CLOCK_SRC;
			data[2] = TIMEBASE_1_NS * 2;
		}
		break;
	default:
		return -EINVAL;
	}
	return insn->n;
}

static int ni_8255_callback(int dir, int port, int data, unsigned long arg)
{
	struct comedi_device *dev = (struct comedi_device *)arg;

	if (dir) {
		ni_writeb(dev, data, Port_A + 2 * port);
		return 0;
	} else {
		return ni_readb(dev, Port_A + 2 * port);
	}
}

static int ni_get_pwm_config(struct comedi_device *dev, unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	data[1] = devpriv->pwm_up_count * devpriv->clock_ns;
	data[2] = devpriv->pwm_down_count * devpriv->clock_ns;
	return 3;
}

static int ni_m_series_pwm_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned up_count, down_count;

	switch (data[0]) {
	case INSN_CONFIG_PWM_OUTPUT:
		switch (data[1]) {
		case TRIG_ROUND_NEAREST:
			up_count =
			    (data[2] +
			     devpriv->clock_ns / 2) / devpriv->clock_ns;
			break;
		case TRIG_ROUND_DOWN:
			up_count = data[2] / devpriv->clock_ns;
			break;
		case TRIG_ROUND_UP:
			up_count =
			    (data[2] + devpriv->clock_ns -
			     1) / devpriv->clock_ns;
			break;
		default:
			return -EINVAL;
			break;
		}
		switch (data[3]) {
		case TRIG_ROUND_NEAREST:
			down_count =
			    (data[4] +
			     devpriv->clock_ns / 2) / devpriv->clock_ns;
			break;
		case TRIG_ROUND_DOWN:
			down_count = data[4] / devpriv->clock_ns;
			break;
		case TRIG_ROUND_UP:
			down_count =
			    (data[4] + devpriv->clock_ns -
			     1) / devpriv->clock_ns;
			break;
		default:
			return -EINVAL;
			break;
		}
		if (up_count * devpriv->clock_ns != data[2] ||
		    down_count * devpriv->clock_ns != data[4]) {
			data[2] = up_count * devpriv->clock_ns;
			data[4] = down_count * devpriv->clock_ns;
			return -EAGAIN;
		}
		ni_writel(dev, MSeries_Cal_PWM_High_Time_Bits(up_count) |
			       MSeries_Cal_PWM_Low_Time_Bits(down_count),
			  M_Offset_Cal_PWM);
		devpriv->pwm_up_count = up_count;
		devpriv->pwm_down_count = down_count;
		return 5;
		break;
	case INSN_CONFIG_GET_PWM_OUTPUT:
		return ni_get_pwm_config(dev, data);
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static int ni_6143_pwm_config(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned up_count, down_count;

	switch (data[0]) {
	case INSN_CONFIG_PWM_OUTPUT:
		switch (data[1]) {
		case TRIG_ROUND_NEAREST:
			up_count =
			    (data[2] +
			     devpriv->clock_ns / 2) / devpriv->clock_ns;
			break;
		case TRIG_ROUND_DOWN:
			up_count = data[2] / devpriv->clock_ns;
			break;
		case TRIG_ROUND_UP:
			up_count =
			    (data[2] + devpriv->clock_ns -
			     1) / devpriv->clock_ns;
			break;
		default:
			return -EINVAL;
			break;
		}
		switch (data[3]) {
		case TRIG_ROUND_NEAREST:
			down_count =
			    (data[4] +
			     devpriv->clock_ns / 2) / devpriv->clock_ns;
			break;
		case TRIG_ROUND_DOWN:
			down_count = data[4] / devpriv->clock_ns;
			break;
		case TRIG_ROUND_UP:
			down_count =
			    (data[4] + devpriv->clock_ns -
			     1) / devpriv->clock_ns;
			break;
		default:
			return -EINVAL;
			break;
		}
		if (up_count * devpriv->clock_ns != data[2] ||
		    down_count * devpriv->clock_ns != data[4]) {
			data[2] = up_count * devpriv->clock_ns;
			data[4] = down_count * devpriv->clock_ns;
			return -EAGAIN;
		}
		ni_writel(dev, up_count, Calibration_HighTime_6143);
		devpriv->pwm_up_count = up_count;
		ni_writel(dev, down_count, Calibration_LowTime_6143);
		devpriv->pwm_down_count = down_count;
		return 5;
		break;
	case INSN_CONFIG_GET_PWM_OUTPUT:
		return ni_get_pwm_config(dev, data);
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static int pack_mb88341(int addr, int val, int *bitstring)
{
	/*
	   Fujitsu MB 88341
	   Note that address bits are reversed.  Thanks to
	   Ingo Keen for noticing this.

	   Note also that the 88341 expects address values from
	   1-12, whereas we use channel numbers 0-11.  The NI
	   docs use 1-12, also, so be careful here.
	 */
	addr++;
	*bitstring = ((addr & 0x1) << 11) |
	    ((addr & 0x2) << 9) |
	    ((addr & 0x4) << 7) | ((addr & 0x8) << 5) | (val & 0xff);
	return 12;
}

static int pack_dac8800(int addr, int val, int *bitstring)
{
	*bitstring = ((addr & 0x7) << 8) | (val & 0xff);
	return 11;
}

static int pack_dac8043(int addr, int val, int *bitstring)
{
	*bitstring = val & 0xfff;
	return 12;
}

static int pack_ad8522(int addr, int val, int *bitstring)
{
	*bitstring = (val & 0xfff) | (addr ? 0xc000 : 0xa000);
	return 16;
}

static int pack_ad8804(int addr, int val, int *bitstring)
{
	*bitstring = ((addr & 0xf) << 8) | (val & 0xff);
	return 12;
}

static int pack_ad8842(int addr, int val, int *bitstring)
{
	*bitstring = ((addr + 1) << 8) | (val & 0xff);
	return 12;
}

struct caldac_struct {
	int n_chans;
	int n_bits;
	int (*packbits)(int, int, int *);
};

static struct caldac_struct caldacs[] = {
	[mb88341] = {12, 8, pack_mb88341},
	[dac8800] = {8, 8, pack_dac8800},
	[dac8043] = {1, 12, pack_dac8043},
	[ad8522] = {2, 12, pack_ad8522},
	[ad8804] = {12, 8, pack_ad8804},
	[ad8842] = {8, 8, pack_ad8842},
	[ad8804_debug] = {16, 8, pack_ad8804},
};

static void ni_write_caldac(struct comedi_device *dev, int addr, int val)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	unsigned int loadbit = 0, bits = 0, bit, bitstring = 0;
	int i;
	int type;

	/* printk("ni_write_caldac: chan=%d val=%d\n",addr,val); */
	if (devpriv->caldacs[addr] == val)
		return;
	devpriv->caldacs[addr] = val;

	for (i = 0; i < 3; i++) {
		type = board->caldac[i];
		if (type == caldac_none)
			break;
		if (addr < caldacs[type].n_chans) {
			bits = caldacs[type].packbits(addr, val, &bitstring);
			loadbit = SerDacLd(i);
			/* printk("caldac: using i=%d addr=%d %x\n",i,addr,bitstring); */
			break;
		}
		addr -= caldacs[type].n_chans;
	}

	for (bit = 1 << (bits - 1); bit; bit >>= 1) {
		ni_writeb(dev, ((bit & bitstring) ? 0x02 : 0), Serial_Command);
		udelay(1);
		ni_writeb(dev, 1 | ((bit & bitstring) ? 0x02 : 0),
			  Serial_Command);
		udelay(1);
	}
	ni_writeb(dev, loadbit, Serial_Command);
	udelay(1);
	ni_writeb(dev, 0, Serial_Command);
}

static int ni_calib_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	ni_write_caldac(dev, CR_CHAN(insn->chanspec), data[0]);

	return 1;
}

static int ni_calib_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	data[0] = devpriv->caldacs[CR_CHAN(insn->chanspec)];

	return 1;
}

static void caldac_setup(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	int i, j;
	int n_dacs;
	int n_chans = 0;
	int n_bits;
	int diffbits = 0;
	int type;
	int chan;

	type = board->caldac[0];
	if (type == caldac_none)
		return;
	n_bits = caldacs[type].n_bits;
	for (i = 0; i < 3; i++) {
		type = board->caldac[i];
		if (type == caldac_none)
			break;
		if (caldacs[type].n_bits != n_bits)
			diffbits = 1;
		n_chans += caldacs[type].n_chans;
	}
	n_dacs = i;
	s->n_chan = n_chans;

	if (diffbits) {
		unsigned int *maxdata_list;

		if (n_chans > MAX_N_CALDACS)
			printk("BUG! MAX_N_CALDACS too small\n");
		s->maxdata_list = maxdata_list = devpriv->caldac_maxdata_list;
		chan = 0;
		for (i = 0; i < n_dacs; i++) {
			type = board->caldac[i];
			for (j = 0; j < caldacs[type].n_chans; j++) {
				maxdata_list[chan] =
				    (1 << caldacs[type].n_bits) - 1;
				chan++;
			}
		}

		for (chan = 0; chan < s->n_chan; chan++)
			ni_write_caldac(dev, i, s->maxdata_list[i] / 2);
	} else {
		type = board->caldac[0];
		s->maxdata = (1 << caldacs[type].n_bits) - 1;

		for (chan = 0; chan < s->n_chan; chan++)
			ni_write_caldac(dev, i, s->maxdata / 2);
	}
}

static int ni_read_eeprom(struct comedi_device *dev, int addr)
{
	int bit;
	int bitstring;

	bitstring = 0x0300 | ((addr & 0x100) << 3) | (addr & 0xff);
	ni_writeb(dev, 0x04, Serial_Command);
	for (bit = 0x8000; bit; bit >>= 1) {
		ni_writeb(dev, 0x04 | ((bit & bitstring) ? 0x02 : 0),
			  Serial_Command);
		ni_writeb(dev, 0x05 | ((bit & bitstring) ? 0x02 : 0),
			  Serial_Command);
	}
	bitstring = 0;
	for (bit = 0x80; bit; bit >>= 1) {
		ni_writeb(dev, 0x04, Serial_Command);
		ni_writeb(dev, 0x05, Serial_Command);
		bitstring |= ((ni_readb(dev, XXX_Status) & PROMOUT) ? bit : 0);
	}
	ni_writeb(dev, 0x00, Serial_Command);

	return bitstring;
}

static int ni_eeprom_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	data[0] = ni_read_eeprom(dev, CR_CHAN(insn->chanspec));

	return 1;
}

static int ni_m_series_eeprom_insn_read(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn,
					unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	data[0] = devpriv->eeprom_buffer[CR_CHAN(insn->chanspec)];

	return 1;
}

static unsigned ni_old_get_pfi_routing(struct comedi_device *dev,
				       unsigned chan)
{
	/*  pre-m-series boards have fixed signals on pfi pins */
	switch (chan) {
	case 0:
		return NI_PFI_OUTPUT_AI_START1;
		break;
	case 1:
		return NI_PFI_OUTPUT_AI_START2;
		break;
	case 2:
		return NI_PFI_OUTPUT_AI_CONVERT;
		break;
	case 3:
		return NI_PFI_OUTPUT_G_SRC1;
		break;
	case 4:
		return NI_PFI_OUTPUT_G_GATE1;
		break;
	case 5:
		return NI_PFI_OUTPUT_AO_UPDATE_N;
		break;
	case 6:
		return NI_PFI_OUTPUT_AO_START1;
		break;
	case 7:
		return NI_PFI_OUTPUT_AI_START_PULSE;
		break;
	case 8:
		return NI_PFI_OUTPUT_G_SRC0;
		break;
	case 9:
		return NI_PFI_OUTPUT_G_GATE0;
		break;
	default:
		printk("%s: bug, unhandled case in switch.\n", __func__);
		break;
	}
	return 0;
}

static int ni_old_set_pfi_routing(struct comedi_device *dev,
				  unsigned chan, unsigned source)
{
	/*  pre-m-series boards have fixed signals on pfi pins */
	if (source != ni_old_get_pfi_routing(dev, chan))
		return -EINVAL;
	return 2;
}

static unsigned ni_m_series_get_pfi_routing(struct comedi_device *dev,
					    unsigned chan)
{
	struct ni_private *devpriv = dev->private;
	const unsigned array_offset = chan / 3;

	return MSeries_PFI_Output_Select_Source(chan,
				devpriv->pfi_output_select_reg[array_offset]);
}

static int ni_m_series_set_pfi_routing(struct comedi_device *dev,
				       unsigned chan, unsigned source)
{
	struct ni_private *devpriv = dev->private;
	unsigned pfi_reg_index;
	unsigned array_offset;

	if ((source & 0x1f) != source)
		return -EINVAL;
	pfi_reg_index = 1 + chan / 3;
	array_offset = pfi_reg_index - 1;
	devpriv->pfi_output_select_reg[array_offset] &=
	    ~MSeries_PFI_Output_Select_Mask(chan);
	devpriv->pfi_output_select_reg[array_offset] |=
	    MSeries_PFI_Output_Select_Bits(chan, source);
	ni_writew(dev, devpriv->pfi_output_select_reg[array_offset],
		  M_Offset_PFI_Output_Select(pfi_reg_index));
	return 2;
}

static unsigned ni_get_pfi_routing(struct comedi_device *dev, unsigned chan)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->is_m_series)
		return ni_m_series_get_pfi_routing(dev, chan);
	else
		return ni_old_get_pfi_routing(dev, chan);
}

static int ni_set_pfi_routing(struct comedi_device *dev, unsigned chan,
			      unsigned source)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->is_m_series)
		return ni_m_series_set_pfi_routing(dev, chan, source);
	else
		return ni_old_set_pfi_routing(dev, chan, source);
}

static int ni_config_filter(struct comedi_device *dev,
			    unsigned pfi_channel,
			    enum ni_pfi_filter_select filter)
{
	struct ni_private *devpriv = dev->private;
	unsigned bits;

	if (!devpriv->is_m_series)
		return -ENOTSUPP;

	bits = ni_readl(dev, M_Offset_PFI_Filter);
	bits &= ~MSeries_PFI_Filter_Select_Mask(pfi_channel);
	bits |= MSeries_PFI_Filter_Select_Bits(pfi_channel, filter);
	ni_writel(dev, bits, M_Offset_PFI_Filter);
	return 0;
}

static int ni_pfi_insn_config(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned int chan;

	if (insn->n < 1)
		return -EINVAL;

	chan = CR_CHAN(insn->chanspec);

	switch (data[0]) {
	case COMEDI_OUTPUT:
		ni_set_bits(dev, IO_Bidirection_Pin_Register, 1 << chan, 1);
		break;
	case COMEDI_INPUT:
		ni_set_bits(dev, IO_Bidirection_Pin_Register, 1 << chan, 0);
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (devpriv->io_bidirection_pin_reg & (1 << chan)) ?
		    COMEDI_OUTPUT : COMEDI_INPUT;
		return 0;
		break;
	case INSN_CONFIG_SET_ROUTING:
		return ni_set_pfi_routing(dev, chan, data[1]);
		break;
	case INSN_CONFIG_GET_ROUTING:
		data[1] = ni_get_pfi_routing(dev, chan);
		break;
	case INSN_CONFIG_FILTER:
		return ni_config_filter(dev, chan, data[1]);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ni_pfi_insn_bits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	if (!devpriv->is_m_series)
		return -ENOTSUPP;

	if (comedi_dio_update_state(s, data))
		ni_writew(dev, s->state, M_Offset_PFI_DO);

	data[1] = ni_readw(dev, M_Offset_PFI_DI);

	return insn->n;
}

static int cs5529_wait_for_idle(struct comedi_device *dev)
{
	unsigned short status;
	const int timeout = HZ;
	int i;

	for (i = 0; i < timeout; i++) {
		status = ni_ao_win_inw(dev, CAL_ADC_Status_67xx);
		if ((status & CSS_ADC_BUSY) == 0)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		if (schedule_timeout(1))
			return -EIO;
	}
/* printk("looped %i times waiting for idle\n", i); */
	if (i == timeout) {
		printk("%s: %s: timeout\n", __FILE__, __func__);
		return -ETIME;
	}
	return 0;
}

static void cs5529_command(struct comedi_device *dev, unsigned short value)
{
	static const int timeout = 100;
	int i;

	ni_ao_win_outw(dev, value, CAL_ADC_Command_67xx);
	/* give time for command to start being serially clocked into cs5529.
	 * this insures that the CSS_ADC_BUSY bit will get properly
	 * set before we exit this function.
	 */
	for (i = 0; i < timeout; i++) {
		if ((ni_ao_win_inw(dev, CAL_ADC_Status_67xx) & CSS_ADC_BUSY))
			break;
		udelay(1);
	}
/* printk("looped %i times writing command to cs5529\n", i); */
	if (i == timeout)
		comedi_error(dev, "possible problem - never saw adc go busy?");
}

static int cs5529_do_conversion(struct comedi_device *dev,
				unsigned short *data)
{
	int retval;
	unsigned short status;

	cs5529_command(dev, CSCMD_COMMAND | CSCMD_SINGLE_CONVERSION);
	retval = cs5529_wait_for_idle(dev);
	if (retval) {
		comedi_error(dev,
			     "timeout or signal in cs5529_do_conversion()");
		return -ETIME;
	}
	status = ni_ao_win_inw(dev, CAL_ADC_Status_67xx);
	if (status & CSS_OSC_DETECT) {
		printk
		    ("ni_mio_common: cs5529 conversion error, status CSS_OSC_DETECT\n");
		return -EIO;
	}
	if (status & CSS_OVERRANGE) {
		printk
		    ("ni_mio_common: cs5529 conversion error, overrange (ignoring)\n");
	}
	if (data) {
		*data = ni_ao_win_inw(dev, CAL_ADC_Data_67xx);
		/* cs5529 returns 16 bit signed data in bipolar mode */
		*data ^= (1 << 15);
	}
	return 0;
}

static int cs5529_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	int n, retval;
	unsigned short sample;
	unsigned int channel_select;
	const unsigned int INTERNAL_REF = 0x1000;

	/* Set calibration adc source.  Docs lie, reference select bits 8 to 11
	 * do nothing. bit 12 seems to chooses internal reference voltage, bit
	 * 13 causes the adc input to go overrange (maybe reads external reference?) */
	if (insn->chanspec & CR_ALT_SOURCE)
		channel_select = INTERNAL_REF;
	else
		channel_select = CR_CHAN(insn->chanspec);
	ni_ao_win_outw(dev, channel_select, AO_Calibration_Channel_Select_67xx);

	for (n = 0; n < insn->n; n++) {
		retval = cs5529_do_conversion(dev, &sample);
		if (retval < 0)
			return retval;
		data[n] = sample;
	}
	return insn->n;
}

static void cs5529_config_write(struct comedi_device *dev, unsigned int value,
				unsigned int reg_select_bits)
{
	ni_ao_win_outw(dev, ((value >> 16) & 0xff),
		       CAL_ADC_Config_Data_High_Word_67xx);
	ni_ao_win_outw(dev, (value & 0xffff),
		       CAL_ADC_Config_Data_Low_Word_67xx);
	reg_select_bits &= CSCMD_REGISTER_SELECT_MASK;
	cs5529_command(dev, CSCMD_COMMAND | reg_select_bits);
	if (cs5529_wait_for_idle(dev))
		comedi_error(dev, "time or signal in cs5529_config_write()");
}

static int init_cs5529(struct comedi_device *dev)
{
	unsigned int config_bits =
	    CSCFG_PORT_MODE | CSCFG_WORD_RATE_2180_CYCLES;

#if 1
	/* do self-calibration */
	cs5529_config_write(dev, config_bits | CSCFG_SELF_CAL_OFFSET_GAIN,
			    CSCMD_CONFIG_REGISTER);
	/* need to force a conversion for calibration to run */
	cs5529_do_conversion(dev, NULL);
#else
	/* force gain calibration to 1 */
	cs5529_config_write(dev, 0x400000, CSCMD_GAIN_REGISTER);
	cs5529_config_write(dev, config_bits | CSCFG_SELF_CAL_OFFSET,
			    CSCMD_CONFIG_REGISTER);
	if (cs5529_wait_for_idle(dev))
		comedi_error(dev, "timeout or signal in init_cs5529()\n");
#endif
	return 0;
}

/*
 * Find best multiplier/divider to try and get the PLL running at 80 MHz
 * given an arbitrary frequency input clock.
 */
static int ni_mseries_get_pll_parameters(unsigned reference_period_ns,
					 unsigned *freq_divider,
					 unsigned *freq_multiplier,
					 unsigned *actual_period_ns)
{
	unsigned div;
	unsigned best_div = 1;
	static const unsigned max_div = 0x10;
	unsigned mult;
	unsigned best_mult = 1;
	static const unsigned max_mult = 0x100;
	static const unsigned pico_per_nano = 1000;

	const unsigned reference_picosec = reference_period_ns * pico_per_nano;
	/* m-series wants the phased-locked loop to output 80MHz, which is divided by 4 to
	 * 20 MHz for most timing clocks */
	static const unsigned target_picosec = 12500;
	static const unsigned fudge_factor_80_to_20Mhz = 4;
	int best_period_picosec = 0;
	for (div = 1; div <= max_div; ++div) {
		for (mult = 1; mult <= max_mult; ++mult) {
			unsigned new_period_ps =
			    (reference_picosec * div) / mult;
			if (abs(new_period_ps - target_picosec) <
			    abs(best_period_picosec - target_picosec)) {
				best_period_picosec = new_period_ps;
				best_div = div;
				best_mult = mult;
			}
		}
	}
	if (best_period_picosec == 0) {
		printk("%s: bug, failed to find pll parameters\n", __func__);
		return -EIO;
	}
	*freq_divider = best_div;
	*freq_multiplier = best_mult;
	*actual_period_ns =
	    (best_period_picosec * fudge_factor_80_to_20Mhz +
	     (pico_per_nano / 2)) / pico_per_nano;
	return 0;
}

static int ni_mseries_set_pll_master_clock(struct comedi_device *dev,
					   unsigned source, unsigned period_ns)
{
	struct ni_private *devpriv = dev->private;
	static const unsigned min_period_ns = 50;
	static const unsigned max_period_ns = 1000;
	static const unsigned timeout = 1000;
	unsigned pll_control_bits;
	unsigned freq_divider;
	unsigned freq_multiplier;
	unsigned i;
	int retval;

	if (source == NI_MIO_PLL_PXI10_CLOCK)
		period_ns = 100;
	/*  these limits are somewhat arbitrary, but NI advertises 1 to 20MHz range so we'll use that */
	if (period_ns < min_period_ns || period_ns > max_period_ns) {
		printk
		    ("%s: you must specify an input clock frequency between %i and %i nanosec "
		     "for the phased-lock loop.\n", __func__,
		     min_period_ns, max_period_ns);
		return -EINVAL;
	}
	devpriv->rtsi_trig_direction_reg &= ~Use_RTSI_Clock_Bit;
	ni_stc_writew(dev, devpriv->rtsi_trig_direction_reg,
		      RTSI_Trig_Direction_Register);
	pll_control_bits =
	    MSeries_PLL_Enable_Bit | MSeries_PLL_VCO_Mode_75_150MHz_Bits;
	devpriv->clock_and_fout2 |=
	    MSeries_Timebase1_Select_Bit | MSeries_Timebase3_Select_Bit;
	devpriv->clock_and_fout2 &= ~MSeries_PLL_In_Source_Select_Mask;
	switch (source) {
	case NI_MIO_PLL_PXI_STAR_TRIGGER_CLOCK:
		devpriv->clock_and_fout2 |=
		    MSeries_PLL_In_Source_Select_Star_Trigger_Bits;
		retval = ni_mseries_get_pll_parameters(period_ns, &freq_divider,
						       &freq_multiplier,
						       &devpriv->clock_ns);
		if (retval < 0)
			return retval;
		break;
	case NI_MIO_PLL_PXI10_CLOCK:
		/* pxi clock is 10MHz */
		devpriv->clock_and_fout2 |=
		    MSeries_PLL_In_Source_Select_PXI_Clock10;
		retval = ni_mseries_get_pll_parameters(period_ns, &freq_divider,
						       &freq_multiplier,
						       &devpriv->clock_ns);
		if (retval < 0)
			return retval;
		break;
	default:
		{
			unsigned rtsi_channel;
			static const unsigned max_rtsi_channel = 7;
			for (rtsi_channel = 0; rtsi_channel <= max_rtsi_channel;
			     ++rtsi_channel) {
				if (source ==
				    NI_MIO_PLL_RTSI_CLOCK(rtsi_channel)) {
					devpriv->clock_and_fout2 |=
					    MSeries_PLL_In_Source_Select_RTSI_Bits
					    (rtsi_channel);
					break;
				}
			}
			if (rtsi_channel > max_rtsi_channel)
				return -EINVAL;
			retval = ni_mseries_get_pll_parameters(period_ns,
							       &freq_divider,
							       &freq_multiplier,
							       &devpriv->
							       clock_ns);
			if (retval < 0)
				return retval;
		}
		break;
	}
	ni_writew(dev, devpriv->clock_and_fout2, M_Offset_Clock_and_Fout2);
	pll_control_bits |=
	    MSeries_PLL_Divisor_Bits(freq_divider) |
	    MSeries_PLL_Multiplier_Bits(freq_multiplier);

	/* printk("using divider=%i, multiplier=%i for PLL. pll_control_bits = 0x%x\n",
	 * freq_divider, freq_multiplier, pll_control_bits); */
	/* printk("clock_ns=%d\n", devpriv->clock_ns); */
	ni_writew(dev, pll_control_bits, M_Offset_PLL_Control);
	devpriv->clock_source = source;
	/* it seems to typically take a few hundred microseconds for PLL to lock */
	for (i = 0; i < timeout; ++i) {
		if (ni_readw(dev, M_Offset_PLL_Status) & MSeries_PLL_Locked_Bit)
			break;
		udelay(1);
	}
	if (i == timeout) {
		printk
		    ("%s: timed out waiting for PLL to lock to reference clock source %i with period %i ns.\n",
		     __func__, source, period_ns);
		return -ETIMEDOUT;
	}
	return 3;
}

static int ni_set_master_clock(struct comedi_device *dev,
			       unsigned source, unsigned period_ns)
{
	struct ni_private *devpriv = dev->private;

	if (source == NI_MIO_INTERNAL_CLOCK) {
		devpriv->rtsi_trig_direction_reg &= ~Use_RTSI_Clock_Bit;
		ni_stc_writew(dev, devpriv->rtsi_trig_direction_reg,
			      RTSI_Trig_Direction_Register);
		devpriv->clock_ns = TIMEBASE_1_NS;
		if (devpriv->is_m_series) {
			devpriv->clock_and_fout2 &=
			    ~(MSeries_Timebase1_Select_Bit |
			      MSeries_Timebase3_Select_Bit);
			ni_writew(dev, devpriv->clock_and_fout2,
				  M_Offset_Clock_and_Fout2);
			ni_writew(dev, 0, M_Offset_PLL_Control);
		}
		devpriv->clock_source = source;
	} else {
		if (devpriv->is_m_series) {
			return ni_mseries_set_pll_master_clock(dev, source,
							       period_ns);
		} else {
			if (source == NI_MIO_RTSI_CLOCK) {
				devpriv->rtsi_trig_direction_reg |=
				    Use_RTSI_Clock_Bit;
				ni_stc_writew(dev,
					      devpriv->rtsi_trig_direction_reg,
					      RTSI_Trig_Direction_Register);
				if (period_ns == 0) {
					printk
					    ("%s: we don't handle an unspecified clock period correctly yet, returning error.\n",
					     __func__);
					return -EINVAL;
				} else {
					devpriv->clock_ns = period_ns;
				}
				devpriv->clock_source = source;
			} else
				return -EINVAL;
		}
	}
	return 3;
}

static unsigned num_configurable_rtsi_channels(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->is_m_series)
		return 8;
	else
		return 7;
}

static int ni_valid_rtsi_output_source(struct comedi_device *dev,
				       unsigned chan, unsigned source)
{
	struct ni_private *devpriv = dev->private;

	if (chan >= num_configurable_rtsi_channels(dev)) {
		if (chan == old_RTSI_clock_channel) {
			if (source == NI_RTSI_OUTPUT_RTSI_OSC)
				return 1;
			else {
				printk
				    ("%s: invalid source for channel=%i, channel %i is always the RTSI clock for pre-m-series boards.\n",
				     __func__, chan, old_RTSI_clock_channel);
				return 0;
			}
		}
		return 0;
	}
	switch (source) {
	case NI_RTSI_OUTPUT_ADR_START1:
	case NI_RTSI_OUTPUT_ADR_START2:
	case NI_RTSI_OUTPUT_SCLKG:
	case NI_RTSI_OUTPUT_DACUPDN:
	case NI_RTSI_OUTPUT_DA_START1:
	case NI_RTSI_OUTPUT_G_SRC0:
	case NI_RTSI_OUTPUT_G_GATE0:
	case NI_RTSI_OUTPUT_RGOUT0:
	case NI_RTSI_OUTPUT_RTSI_BRD_0:
		return 1;
		break;
	case NI_RTSI_OUTPUT_RTSI_OSC:
		if (devpriv->is_m_series)
			return 1;
		else
			return 0;
		break;
	default:
		return 0;
		break;
	}
}

static int ni_set_rtsi_routing(struct comedi_device *dev,
			       unsigned chan, unsigned source)
{
	struct ni_private *devpriv = dev->private;

	if (ni_valid_rtsi_output_source(dev, chan, source) == 0)
		return -EINVAL;
	if (chan < 4) {
		devpriv->rtsi_trig_a_output_reg &= ~RTSI_Trig_Output_Mask(chan);
		devpriv->rtsi_trig_a_output_reg |=
		    RTSI_Trig_Output_Bits(chan, source);
		ni_stc_writew(dev, devpriv->rtsi_trig_a_output_reg,
			      RTSI_Trig_A_Output_Register);
	} else if (chan < 8) {
		devpriv->rtsi_trig_b_output_reg &= ~RTSI_Trig_Output_Mask(chan);
		devpriv->rtsi_trig_b_output_reg |=
		    RTSI_Trig_Output_Bits(chan, source);
		ni_stc_writew(dev, devpriv->rtsi_trig_b_output_reg,
			      RTSI_Trig_B_Output_Register);
	}
	return 2;
}

static unsigned ni_get_rtsi_routing(struct comedi_device *dev, unsigned chan)
{
	struct ni_private *devpriv = dev->private;

	if (chan < 4) {
		return RTSI_Trig_Output_Source(chan,
					       devpriv->rtsi_trig_a_output_reg);
	} else if (chan < num_configurable_rtsi_channels(dev)) {
		return RTSI_Trig_Output_Source(chan,
					       devpriv->rtsi_trig_b_output_reg);
	} else {
		if (chan == old_RTSI_clock_channel)
			return NI_RTSI_OUTPUT_RTSI_OSC;
		printk("%s: bug! should never get here?\n", __func__);
		return 0;
	}
}

static int ni_rtsi_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		if (chan < num_configurable_rtsi_channels(dev)) {
			devpriv->rtsi_trig_direction_reg |=
			    RTSI_Output_Bit(chan, devpriv->is_m_series);
		} else if (chan == old_RTSI_clock_channel) {
			devpriv->rtsi_trig_direction_reg |=
			    Drive_RTSI_Clock_Bit;
		}
		ni_stc_writew(dev, devpriv->rtsi_trig_direction_reg,
			      RTSI_Trig_Direction_Register);
		break;
	case INSN_CONFIG_DIO_INPUT:
		if (chan < num_configurable_rtsi_channels(dev)) {
			devpriv->rtsi_trig_direction_reg &=
			    ~RTSI_Output_Bit(chan, devpriv->is_m_series);
		} else if (chan == old_RTSI_clock_channel) {
			devpriv->rtsi_trig_direction_reg &=
			    ~Drive_RTSI_Clock_Bit;
		}
		ni_stc_writew(dev, devpriv->rtsi_trig_direction_reg,
			      RTSI_Trig_Direction_Register);
		break;
	case INSN_CONFIG_DIO_QUERY:
		if (chan < num_configurable_rtsi_channels(dev)) {
			data[1] =
			    (devpriv->rtsi_trig_direction_reg &
			     RTSI_Output_Bit(chan, devpriv->is_m_series))
				? INSN_CONFIG_DIO_OUTPUT
				: INSN_CONFIG_DIO_INPUT;
		} else if (chan == old_RTSI_clock_channel) {
			data[1] =
			    (devpriv->rtsi_trig_direction_reg &
			     Drive_RTSI_Clock_Bit)
			    ? INSN_CONFIG_DIO_OUTPUT : INSN_CONFIG_DIO_INPUT;
		}
		return 2;
		break;
	case INSN_CONFIG_SET_CLOCK_SRC:
		return ni_set_master_clock(dev, data[1], data[2]);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		data[1] = devpriv->clock_source;
		data[2] = devpriv->clock_ns;
		return 3;
		break;
	case INSN_CONFIG_SET_ROUTING:
		return ni_set_rtsi_routing(dev, chan, data[1]);
		break;
	case INSN_CONFIG_GET_ROUTING:
		data[1] = ni_get_rtsi_routing(dev, chan);
		return 2;
		break;
	default:
		return -EINVAL;
		break;
	}
	return 1;
}

static int ni_rtsi_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	data[1] = 0;

	return insn->n;
}

static void ni_rtsi_init(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;

	/*  Initialises the RTSI bus signal switch to a default state */

	/*  Set clock mode to internal */
	devpriv->clock_and_fout2 = MSeries_RTSI_10MHz_Bit;
	if (ni_set_master_clock(dev, NI_MIO_INTERNAL_CLOCK, 0) < 0)
		printk("ni_set_master_clock failed, bug?");
	/*  default internal lines routing to RTSI bus lines */
	devpriv->rtsi_trig_a_output_reg =
	    RTSI_Trig_Output_Bits(0,
				  NI_RTSI_OUTPUT_ADR_START1) |
	    RTSI_Trig_Output_Bits(1,
				  NI_RTSI_OUTPUT_ADR_START2) |
	    RTSI_Trig_Output_Bits(2,
				  NI_RTSI_OUTPUT_SCLKG) |
	    RTSI_Trig_Output_Bits(3, NI_RTSI_OUTPUT_DACUPDN);
	ni_stc_writew(dev, devpriv->rtsi_trig_a_output_reg,
		      RTSI_Trig_A_Output_Register);
	devpriv->rtsi_trig_b_output_reg =
	    RTSI_Trig_Output_Bits(4,
				  NI_RTSI_OUTPUT_DA_START1) |
	    RTSI_Trig_Output_Bits(5,
				  NI_RTSI_OUTPUT_G_SRC0) |
	    RTSI_Trig_Output_Bits(6, NI_RTSI_OUTPUT_G_GATE0);
	if (devpriv->is_m_series)
		devpriv->rtsi_trig_b_output_reg |=
		    RTSI_Trig_Output_Bits(7, NI_RTSI_OUTPUT_RTSI_OSC);
	ni_stc_writew(dev, devpriv->rtsi_trig_b_output_reg,
		      RTSI_Trig_B_Output_Register);

/*
* Sets the source and direction of the 4 on board lines
* ni_stc_writew(dev, 0x0000, RTSI_Board_Register);
*/
}

#ifdef PCIDMA
static int ni_gpct_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	int retval;

	retval = ni_request_gpct_mite_channel(dev, counter->counter_index,
					      COMEDI_INPUT);
	if (retval) {
		comedi_error(dev,
			     "no dma channel available for use by counter");
		return retval;
	}
	ni_tio_acknowledge_and_confirm(counter, NULL, NULL, NULL, NULL);
	ni_e_series_enable_second_irq(dev, counter->counter_index, 1);

	return ni_tio_cmd(dev, s);
}

static int ni_gpct_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	int retval;

	retval = ni_tio_cancel(counter);
	ni_e_series_enable_second_irq(dev, counter->counter_index, 0);
	ni_release_gpct_mite_channel(dev, counter->counter_index);
	return retval;
}
#endif

#if 0
/*
 *	Read the GPCTs current value.
 */
static int GPCT_G_Watch(struct comedi_device *dev, int chan)
{
	unsigned int hi1, hi2, lo;

	devpriv->gpct_command[chan] &= ~G_Save_Trace;
	ni_stc_writew(dev, devpriv->gpct_command[chan],
		      G_Command_Register(chan));

	devpriv->gpct_command[chan] |= G_Save_Trace;
	ni_stc_writew(dev, devpriv->gpct_command[chan],
		      G_Command_Register(chan));

	/* This procedure is used because the two registers cannot
	 * be read atomically. */
	do {
		hi1 = ni_stc_readw(dev, G_Save_Register_High(chan));
		lo = ni_stc_readw(dev, G_Save_Register_Low(chan));
		hi2 = ni_stc_readw(dev, G_Save_Register_High(chan));
	} while (hi1 != hi2);

	return (hi1 << 16) | lo;
}

static void GPCT_Reset(struct comedi_device *dev, int chan)
{
	int temp_ack_reg = 0;

	/* printk("GPCT_Reset..."); */
	devpriv->gpct_cur_operation[chan] = GPCT_RESET;

	switch (chan) {
	case 0:
		ni_stc_writew(dev, G0_Reset, Joint_Reset_Register);
		ni_set_bits(dev, Interrupt_A_Enable_Register,
			    G0_TC_Interrupt_Enable, 0);
		ni_set_bits(dev, Interrupt_A_Enable_Register,
			    G0_Gate_Interrupt_Enable, 0);
		temp_ack_reg |= G0_Gate_Error_Confirm;
		temp_ack_reg |= G0_TC_Error_Confirm;
		temp_ack_reg |= G0_TC_Interrupt_Ack;
		temp_ack_reg |= G0_Gate_Interrupt_Ack;
		ni_stc_writew(dev, temp_ack_reg, Interrupt_A_Ack_Register);

		/* problem...this interferes with the other ctr... */
		devpriv->an_trig_etc_reg |= GPFO_0_Output_Enable;
		ni_stc_writew(dev, devpriv->an_trig_etc_reg,
			      Analog_Trigger_Etc_Register);
		break;
	case 1:
		ni_stc_writew(dev, G1_Reset, Joint_Reset_Register);
		ni_set_bits(dev, Interrupt_B_Enable_Register,
			    G1_TC_Interrupt_Enable, 0);
		ni_set_bits(dev, Interrupt_B_Enable_Register,
			    G0_Gate_Interrupt_Enable, 0);
		temp_ack_reg |= G1_Gate_Error_Confirm;
		temp_ack_reg |= G1_TC_Error_Confirm;
		temp_ack_reg |= G1_TC_Interrupt_Ack;
		temp_ack_reg |= G1_Gate_Interrupt_Ack;
		ni_stc_writew(dev, temp_ack_reg, Interrupt_B_Ack_Register);

		devpriv->an_trig_etc_reg |= GPFO_1_Output_Enable;
		ni_stc_writew(dev, devpriv->an_trig_etc_reg,
			      Analog_Trigger_Etc_Register);
		break;
	}

	devpriv->gpct_mode[chan] = 0;
	devpriv->gpct_input_select[chan] = 0;
	devpriv->gpct_command[chan] = 0;

	devpriv->gpct_command[chan] |= G_Synchronized_Gate;

	ni_stc_writew(dev, devpriv->gpct_mode[chan], G_Mode_Register(chan));
	ni_stc_writew(dev, devpriv->gpct_input_select[chan],
		      G_Input_Select_Register(chan));
	ni_stc_writew(dev, 0, G_Autoincrement_Register(chan));

	/* printk("exit GPCT_Reset\n"); */
}
#endif

static irqreturn_t ni_E_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned short a_status;
	unsigned short b_status;
	unsigned int ai_mite_status = 0;
	unsigned int ao_mite_status = 0;
	unsigned long flags;
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
	struct mite_struct *mite = devpriv->mite;
#endif

	if (!dev->attached)
		return IRQ_NONE;
	smp_mb();		/*  make sure dev->attached is checked before handler does anything else. */

	/*  lock to avoid race with comedi_poll */
	spin_lock_irqsave(&dev->spinlock, flags);
	a_status = ni_stc_readw(dev, AI_Status_1_Register);
	b_status = ni_stc_readw(dev, AO_Status_1_Register);
#ifdef PCIDMA
	if (mite) {
		struct ni_private *devpriv = dev->private;
		unsigned long flags_too;

		spin_lock_irqsave(&devpriv->mite_channel_lock, flags_too);
		if (devpriv->ai_mite_chan) {
			ai_mite_status = mite_get_status(devpriv->ai_mite_chan);
			if (ai_mite_status & CHSR_LINKC)
				writel(CHOR_CLRLC,
				       devpriv->mite->mite_io_addr +
				       MITE_CHOR(devpriv->
						 ai_mite_chan->channel));
		}
		if (devpriv->ao_mite_chan) {
			ao_mite_status = mite_get_status(devpriv->ao_mite_chan);
			if (ao_mite_status & CHSR_LINKC)
				writel(CHOR_CLRLC,
				       mite->mite_io_addr +
				       MITE_CHOR(devpriv->
						 ao_mite_chan->channel));
		}
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags_too);
	}
#endif
	ack_a_interrupt(dev, a_status);
	ack_b_interrupt(dev, b_status);
	if ((a_status & Interrupt_A_St) || (ai_mite_status & CHSR_INT))
		handle_a_interrupt(dev, a_status, ai_mite_status);
	if ((b_status & Interrupt_B_St) || (ao_mite_status & CHSR_INT))
		handle_b_interrupt(dev, b_status, ao_mite_status);
	handle_gpct_interrupt(dev, 0);
	handle_gpct_interrupt(dev, 1);
	handle_cdio_interrupt(dev);

	spin_unlock_irqrestore(&dev->spinlock, flags);
	return IRQ_HANDLED;
}

static int ni_alloc_private(struct comedi_device *dev)
{
	struct ni_private *devpriv;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	spin_lock_init(&devpriv->window_lock);
	spin_lock_init(&devpriv->soft_reg_copy_lock);
	spin_lock_init(&devpriv->mite_channel_lock);

	return 0;
}

static int ni_E_init(struct comedi_device *dev,
		     unsigned interrupt_pin, unsigned irq_polarity)
{
	const struct ni_board_struct *board = comedi_board(dev);
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	unsigned j;
	enum ni_gpct_variant counter_variant;
	int ret;

	if (board->n_aochan > MAX_N_AO_CHAN) {
		printk("bug! n_aochan > MAX_N_AO_CHAN\n");
		return -EINVAL;
	}

	ret = comedi_alloc_subdevices(dev, NI_NUM_SUBDEVICES);
	if (ret)
		return ret;

	/* analog input subdevice */

	s = &dev->subdevices[NI_AI_SUBDEV];
	dev->read_subdev = s;
	if (board->n_adchan) {
		s->type = COMEDI_SUBD_AI;
		s->subdev_flags =
		    SDF_READABLE | SDF_DIFF | SDF_DITHER | SDF_CMD_READ;
		if (board->reg_type != ni_reg_611x)
			s->subdev_flags |= SDF_GROUND | SDF_COMMON | SDF_OTHER;
		if (board->adbits > 16)
			s->subdev_flags |= SDF_LSAMPL;
		if (devpriv->is_m_series)
			s->subdev_flags |= SDF_SOFT_CALIBRATED;
		s->n_chan = board->n_adchan;
		s->len_chanlist = 512;
		s->maxdata = (1 << board->adbits) - 1;
		s->range_table = ni_range_lkup[board->gainlkup];
		s->insn_read = &ni_ai_insn_read;
		s->insn_config = &ni_ai_insn_config;
		s->do_cmdtest = &ni_ai_cmdtest;
		s->do_cmd = &ni_ai_cmd;
		s->cancel = &ni_ai_reset;
		s->poll = &ni_ai_poll;
		s->munge = &ni_ai_munge;
#ifdef PCIDMA
		s->async_dma_dir = DMA_FROM_DEVICE;
#endif
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* analog output subdevice */

	s = &dev->subdevices[NI_AO_SUBDEV];
	if (board->n_aochan) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITABLE | SDF_DEGLITCH | SDF_GROUND;
		if (devpriv->is_m_series)
			s->subdev_flags |= SDF_SOFT_CALIBRATED;
		s->n_chan = board->n_aochan;
		s->maxdata = (1 << board->aobits) - 1;
		s->range_table = board->ao_range_table;
		s->insn_read = &ni_ao_insn_read;
		if (board->reg_type & ni_reg_6xxx_mask)
			s->insn_write = &ni_ao_insn_write_671x;
		else
			s->insn_write = &ni_ao_insn_write;
		s->insn_config = &ni_ao_insn_config;
#ifdef PCIDMA
		if (board->n_aochan) {
			s->async_dma_dir = DMA_TO_DEVICE;
#else
		if (board->ao_fifo_depth) {
#endif
			dev->write_subdev = s;
			s->subdev_flags |= SDF_CMD_WRITE;
			s->do_cmd = &ni_ao_cmd;
			s->do_cmdtest = &ni_ao_cmdtest;
			s->len_chanlist = board->n_aochan;
			if (!devpriv->is_m_series)
				s->munge = ni_ao_munge;
		}
		s->cancel = &ni_ao_reset;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	if ((board->reg_type & ni_reg_67xx_mask))
		init_ao_67xx(dev, s);

	/* digital i/o subdevice */

	s = &dev->subdevices[NI_DIO_SUBDEV];
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->maxdata = 1;
	s->io_bits = 0;		/* all bits input */
	s->range_table = &range_digital;
	s->n_chan = board->num_p0_dio_channels;
	if (devpriv->is_m_series) {
		s->subdev_flags |=
		    SDF_LSAMPL | SDF_CMD_WRITE /* | SDF_CMD_READ */;
		s->insn_bits = &ni_m_series_dio_insn_bits;
		s->insn_config = &ni_m_series_dio_insn_config;
		s->do_cmd = &ni_cdio_cmd;
		s->do_cmdtest = &ni_cdio_cmdtest;
		s->cancel = &ni_cdio_cancel;
		s->async_dma_dir = DMA_BIDIRECTIONAL;
		s->len_chanlist = s->n_chan;

		ni_writel(dev, CDO_Reset_Bit | CDI_Reset_Bit,
			  M_Offset_CDIO_Command);
		ni_writel(dev, s->io_bits, M_Offset_DIO_Direction);
	} else {
		s->insn_bits = &ni_dio_insn_bits;
		s->insn_config = &ni_dio_insn_config;
		devpriv->dio_control = DIO_Pins_Dir(s->io_bits);
		ni_writew(dev, devpriv->dio_control, DIO_Control_Register);
	}

	/* 8255 device */
	s = &dev->subdevices[NI_8255_DIO_SUBDEV];
	if (board->has_8255) {
		ret = subdev_8255_init(dev, s, ni_8255_callback,
				       (unsigned long)dev);
		if (ret)
			return ret;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* formerly general purpose counter/timer device, but no longer used */
	s = &dev->subdevices[NI_UNUSED_SUBDEV];
	s->type = COMEDI_SUBD_UNUSED;

	/* calibration subdevice -- ai and ao */
	s = &dev->subdevices[NI_CALIBRATION_SUBDEV];
	s->type = COMEDI_SUBD_CALIB;
	if (devpriv->is_m_series) {
		/*  internal PWM analog output used for AI nonlinearity calibration */
		s->subdev_flags = SDF_INTERNAL;
		s->insn_config = &ni_m_series_pwm_config;
		s->n_chan = 1;
		s->maxdata = 0;
		ni_writel(dev, 0x0, M_Offset_Cal_PWM);
	} else if (board->reg_type == ni_reg_6143) {
		/*  internal PWM analog output used for AI nonlinearity calibration */
		s->subdev_flags = SDF_INTERNAL;
		s->insn_config = &ni_6143_pwm_config;
		s->n_chan = 1;
		s->maxdata = 0;
	} else {
		s->subdev_flags = SDF_WRITABLE | SDF_INTERNAL;
		s->insn_read = &ni_calib_insn_read;
		s->insn_write = &ni_calib_insn_write;
		caldac_setup(dev, s);
	}

	/* EEPROM */
	s = &dev->subdevices[NI_EEPROM_SUBDEV];
	s->type = COMEDI_SUBD_MEMORY;
	s->subdev_flags = SDF_READABLE | SDF_INTERNAL;
	s->maxdata = 0xff;
	if (devpriv->is_m_series) {
		s->n_chan = M_SERIES_EEPROM_SIZE;
		s->insn_read = &ni_m_series_eeprom_insn_read;
	} else {
		s->n_chan = 512;
		s->insn_read = &ni_eeprom_insn_read;
	}

	/* PFI */
	s = &dev->subdevices[NI_PFI_DIO_SUBDEV];
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
	if (devpriv->is_m_series) {
		unsigned i;
		s->n_chan = 16;
		ni_writew(dev, s->state, M_Offset_PFI_DO);
		for (i = 0; i < NUM_PFI_OUTPUT_SELECT_REGS; ++i) {
			ni_writew(dev, devpriv->pfi_output_select_reg[i],
				  M_Offset_PFI_Output_Select(i + 1));
		}
	} else {
		s->n_chan = 10;
	}
	s->maxdata = 1;
	if (devpriv->is_m_series)
		s->insn_bits = &ni_pfi_insn_bits;
	s->insn_config = &ni_pfi_insn_config;
	ni_set_bits(dev, IO_Bidirection_Pin_Register, ~0, 0);

	/* cs5529 calibration adc */
	s = &dev->subdevices[NI_CS5529_CALIBRATION_SUBDEV];
	if (board->reg_type & ni_reg_67xx_mask) {
		s->type = COMEDI_SUBD_AI;
		s->subdev_flags = SDF_READABLE | SDF_DIFF | SDF_INTERNAL;
		/*  one channel for each analog output channel */
		s->n_chan = board->n_aochan;
		s->maxdata = (1 << 16) - 1;
		s->range_table = &range_unknown;	/* XXX */
		s->insn_read = cs5529_ai_insn_read;
		s->insn_config = NULL;
		init_cs5529(dev);
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* Serial */
	s = &dev->subdevices[NI_SERIAL_SUBDEV];
	s->type = COMEDI_SUBD_SERIAL;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
	s->n_chan = 1;
	s->maxdata = 0xff;
	s->insn_config = ni_serial_insn_config;
	devpriv->serial_interval_ns = 0;
	devpriv->serial_hw_mode = 0;

	/* RTSI */
	s = &dev->subdevices[NI_RTSI_SUBDEV];
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
	s->n_chan = 8;
	s->maxdata = 1;
	s->insn_bits = ni_rtsi_insn_bits;
	s->insn_config = ni_rtsi_insn_config;
	ni_rtsi_init(dev);

	if (devpriv->is_m_series)
		counter_variant = ni_gpct_variant_m_series;
	else
		counter_variant = ni_gpct_variant_e_series;
	devpriv->counter_dev = ni_gpct_device_construct(dev,
							&ni_gpct_write_register,
							&ni_gpct_read_register,
							counter_variant,
							NUM_GPCT);
	if (!devpriv->counter_dev)
		return -ENOMEM;

	/* General purpose counters */
	for (j = 0; j < NUM_GPCT; ++j) {
		s = &dev->subdevices[NI_GPCT_SUBDEV(j)];
		s->type = COMEDI_SUBD_COUNTER;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL;
		s->n_chan = 3;
		if (devpriv->is_m_series)
			s->maxdata = 0xffffffff;
		else
			s->maxdata = 0xffffff;
		s->insn_read = ni_tio_insn_read;
		s->insn_write = ni_tio_insn_read;
		s->insn_config = ni_tio_insn_config;
#ifdef PCIDMA
		s->subdev_flags |= SDF_CMD_READ /* | SDF_CMD_WRITE */;
		s->do_cmd = &ni_gpct_cmd;
		s->len_chanlist = 1;
		s->do_cmdtest = ni_tio_cmdtest;
		s->cancel = &ni_gpct_cancel;
		s->async_dma_dir = DMA_BIDIRECTIONAL;
#endif
		s->private = &devpriv->counter_dev->counters[j];

		devpriv->counter_dev->counters[j].chip_index = 0;
		devpriv->counter_dev->counters[j].counter_index = j;
		ni_tio_init_counter(&devpriv->counter_dev->counters[j]);
	}

	/* Frequency output */
	s = &dev->subdevices[NI_FREQ_OUT_SUBDEV];
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 1;
	s->maxdata = 0xf;
	s->insn_read = &ni_freq_out_insn_read;
	s->insn_write = &ni_freq_out_insn_write;
	s->insn_config = &ni_freq_out_insn_config;

	/* ai configuration */
	s = &dev->subdevices[NI_AI_SUBDEV];
	ni_ai_reset(dev, s);
	if ((board->reg_type & ni_reg_6xxx_mask) == 0) {
		/*  BEAM is this needed for PCI-6143 ?? */
		devpriv->clock_and_fout =
		    Slow_Internal_Time_Divide_By_2 |
		    Slow_Internal_Timebase |
		    Clock_To_Board_Divide_By_2 |
		    Clock_To_Board |
		    AI_Output_Divide_By_2 | AO_Output_Divide_By_2;
	} else {
		devpriv->clock_and_fout =
		    Slow_Internal_Time_Divide_By_2 |
		    Slow_Internal_Timebase |
		    Clock_To_Board_Divide_By_2 | Clock_To_Board;
	}
	ni_stc_writew(dev, devpriv->clock_and_fout, Clock_and_FOUT_Register);

	/* analog output configuration */
	s = &dev->subdevices[NI_AO_SUBDEV];
	ni_ao_reset(dev, s);

	if (dev->irq) {
		ni_stc_writew(dev,
			      (irq_polarity ? Interrupt_Output_Polarity : 0) |
			      (Interrupt_Output_On_3_Pins & 0) |
			      Interrupt_A_Enable | Interrupt_B_Enable |
			      Interrupt_A_Output_Select(interrupt_pin) |
			      Interrupt_B_Output_Select(interrupt_pin),
			      Interrupt_Control_Register);
	}

	/* DMA setup */
	ni_writeb(dev, devpriv->ai_ao_select_reg, AI_AO_Select);
	ni_writeb(dev, devpriv->g0_g1_select_reg, G0_G1_Select);

	if (board->reg_type & ni_reg_6xxx_mask) {
		ni_writeb(dev, 0, Magic_611x);
	} else if (devpriv->is_m_series) {
		int channel;
		for (channel = 0; channel < board->n_aochan; ++channel) {
			ni_writeb(dev, 0xf,
				  M_Offset_AO_Waveform_Order(channel));
			ni_writeb(dev, 0x0,
				  M_Offset_AO_Reference_Attenuation(channel));
		}
		ni_writeb(dev, 0x0, M_Offset_AO_Calibration);
	}

	return 0;
}

static void mio_common_detach(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv) {
		if (devpriv->counter_dev)
			ni_gpct_device_destroy(devpriv->counter_dev);
	}
}
